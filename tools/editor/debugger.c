/*
 * The script debugger: the Scripts tab under the scene view, around the
 * script editor (code.c).
 *
 * The editor shows a script with line numbers; clicking a line's number sets
 * (or clears) a breakpoint there. When a playing game's script reaches one, the
 * engine calls our hook from inside the script (script.h, br_script_debug_*):
 * the game is held exactly there while the editor keeps drawing
 * (br_engine_frame_paused), showing the line, the call stack and the
 * variables, until Continue / Step Over / Step Into / Step Out / Stop.
 *
 * While stopped, everything but the Debug tab is read-only: the game is in
 * the middle of a step, so the scene mustn't change under it.
 */
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* `path`: a project-relative script ("scripts/player.bs") or a full one. */
bool ed_debug_show_file(editor *ed, const char *path, int line) { return ed_code_open(ed, path, line); }

/* The scripts the scene uses, for the list in the bar. */
static int scene_scripts(editor *ed, const char **out, int max) {
    br_component sc = br_component_find(&ed->doc.world, "Script");
    const br_component_pool *p;
    uint32_t i;
    int n = 0, k;
    if (sc == BR_COMPONENT_INVALID) return 0;
    p = &ed->doc.world.pools[sc];
    for (i = 0; i < p->count && n < max; ++i) {
        const br_script *s = (const br_script *)(p->data + (size_t)i * p->stride);
        bool seen = false;
        if (!s->file[0]) continue;
        for (k = 0; k < n; ++k) if (strcmp(out[k], s->file) == 0) seen = true;
        if (!seen) out[n++] = s->file;
    }
    return n;
}

/* ---- stopped: the hook ---------------------------------------------------------------------- */

static void take_frame(ed_debug *g, int frame) {
    g->frame = frame;
    g->nlocals = br_script_debug_locals(frame, g->locals, ED_DEBUG_VARS);
    g->nglobals = br_script_debug_globals(frame, g->globals, ED_DEBUG_VARS);
}

static void show_frame(editor *ed, int frame) {
    ed_debug *g = &ed->dbg;
    if (frame < 0 || frame >= g->nframes) return;
    take_frame(g, frame);
    ed_debug_show_file(ed, g->frames[frame].file, g->frames[frame].line);
    g->here = g->frames[frame].line;
    snprintf(g->here_file, sizeof(g->here_file), "%s", g->file);
}

static br_debug_action hook(const br_debug_stop *stop, void *user) {
    editor *ed = (editor *)user;
    ed_debug *g = &ed->dbg;
    int i, waited = 0;
    g->stopped = true;
    g->action = -1;
    g->entity = stop->entity;
    snprintf(g->reason, sizeof(g->reason), "%s", stop->reason);
    g->nframes = br_script_debug_frame_count();
    if (g->nframes > ED_DEBUG_FRAMES) g->nframes = ED_DEBUG_FRAMES;
    for (i = 0; i < g->nframes; ++i) {
        const char *fn = "", *file = "";
        int line = 0;
        br_script_debug_frame(i, &fn, &file, &line);
        snprintf(g->frames[i].fn, sizeof(g->frames[i].fn), "%s", fn);
        snprintf(g->frames[i].file, sizeof(g->frames[i].file), "%s", file);
        g->frames[i].line = line;
    }
    if (g->nframes) show_frame(ed, 0);
    else { ed_debug_show_file(ed, stop->file, stop->line); g->here = stop->line; snprintf(g->here_file, sizeof(g->here_file), "%s", g->file); g->nlocals = 0; g->nglobals = br_script_debug_globals(0, g->globals, ED_DEBUG_VARS); }
    ed->bottom_tab = ED_TAB_DEBUG;
    ed_panel_set_open(ed, ED_PANEL_BOTTOM, true);
    ed_status(ed, "Stopped at %s:%d (%s)", br_path_name(stop->file), stop->line, stop->reason);
    while (g->action < 0) {
        const br_input *in = &ed->engine->input;
        bool shift = (in->mods & BR_MOD_SHIFT) != 0;
        if (!br_engine_frame_paused(ed->engine)) { g->action = BR_DEBUG_DETACH; g->stop_after = true; break; }
        ++waited;
        if (g->auto_frames > 0 && waited >= g->auto_frames) {                /* (the self-test) */
            if (g->auto_shot) { br_image_save_bmp(&ed->engine->framebuffer, g->auto_shot); g->auto_shot = NULL; }
            g->action = g->auto_action;
            break;
        }
        if (br_key_pressed(in, BR_KEY_F5)) g->action = BR_DEBUG_CONTINUE;
        else if (br_key_pressed(in, BR_KEY_F10)) g->action = BR_DEBUG_STEP_OVER;
        else if (br_key_pressed(in, BR_KEY_F11)) g->action = shift ? BR_DEBUG_STEP_OUT : BR_DEBUG_STEP_INTO;
        else if (br_key_pressed(in, BR_KEY_ESCAPE)) { g->action = BR_DEBUG_DETACH; g->stop_after = true; }
    }
    g->stopped = false;
    g->stops++;
    g->last_line = stop->line;
    if (g->action == BR_DEBUG_DETACH) g->stop_after = true;     /* Stop: the game stops playing after this step */
    return (br_debug_action)g->action;
}

void ed_debug_init(editor *ed) {
    memset(&ed->dbg, 0, sizeof(ed->dbg));
    br_script_debug_attach(hook, ed);
}

void ed_debug_destroy(editor *ed) {
    BR_UNUSED(ed);
    br_script_debug_attach(NULL, NULL);
}

/* After a frame: Stop pressed while stopped stops playing (outside the script). */
void ed_debug_after_frame(editor *ed) {
    if (ed->dbg.stop_after) {
        ed->dbg.stop_after = false;
        if (ed->doc.playing) ed_cmd_play(ed);
        br_script_debug_resume();                       /* (breakpoints count again next time) */
    }
}

bool ed_debug_toggle_breakpoint(editor *ed, int line) {
    bool on;
    if (!ed->dbg.file[0] || line < 1) return false;
    on = !br_script_debug_has_breakpoint(ed->dbg.file, line);
    br_script_debug_set_breakpoint(ed->dbg.file, line, on);
    return on;
}

/* ---- the panel ---------------------------------------------------------------------------- */

typedef struct dbg_strip { br_ui *ui; float x, y, h; } dbg_strip;

static bool bar_button(dbg_strip *s, const char *label, float w, bool on, bool enabled) {
    bool hit;
    br_ui_set_disabled(s->ui, !enabled);
    br_ui_place(s->ui, br_rect_make(s->x, s->y, w, s->h));
    hit = br_ui_toggle(s->ui, label, on);
    br_ui_set_disabled(s->ui, false);
    s->x += w + 4;
    return hit && enabled;
}

static void draw_state(editor *ed, br_rect rect) {
    ed_debug *g = &ed->dbg;
    br_ui *ui = &ed->ui;
    int i;
    char line[200], who[64];
    br_ui_panel_begin(ui, "debug_state", rect);
    if (!g->stopped) {
        br_ui_heading(ui, "Not stopped");
        br_ui_label_colored(ui, ed->doc.playing ? "Running. Pause stops at the next line any script runs."
                                                : "Set breakpoints, then press Play.", ui->style.text_dim);
        {
            int count = br_script_debug_breakpoint(-1, NULL, NULL);
            if (count) {
                br_ui_spacing(ui, 6);
                br_ui_heading(ui, "Breakpoints");
                for (i = 0; i < count; ++i) {
                    const char *file;
                    int at;
                    br_script_debug_breakpoint(i, &file, &at);
                    snprintf(line, sizeof(line), "%s:%d##bp%d", br_path_name(file), at, i);
                    if (br_ui_selectable(ui, line, false)) ed_debug_show_file(ed, file, at);
                }
            }
        }
        br_ui_panel_end(ui);
        return;
    }
    br_ui_heading(ui, "Call stack");
    for (i = 0; i < g->nframes; ++i) {
        snprintf(line, sizeof(line), "%s()  %s:%d##fr%d", g->frames[i].fn, br_path_name(g->frames[i].file), g->frames[i].line, i);
        if (br_ui_selectable(ui, line, i == g->frame)) show_frame(ed, i);
    }
    if (!g->nframes) br_ui_label_colored(ui, "(setting up the script's variables)", ui->style.text_dim);
    br_ui_spacing(ui, 6);
    br_ui_heading(ui, "Variables");
    if (!g->nlocals) br_ui_label_colored(ui, "(none here)", ui->style.text_dim);
    for (i = 0; i < g->nlocals; ++i) {
        snprintf(line, sizeof(line), "%s = %s", g->locals[i].name, g->locals[i].value);
        br_ui_label(ui, line);
    }
    br_ui_spacing(ui, 6);
    snprintf(line, sizeof(line), "%s's script", br_entity_alive(&ed->doc.world, g->entity) ? ed_doc_name(&ed->doc, g->entity, who, sizeof(who)) : "Its");
    br_ui_heading(ui, line);
    if (!g->nglobals) br_ui_label_colored(ui, "(no variables)", ui->style.text_dim);
    for (i = 0; i < g->nglobals; ++i) {
        snprintf(line, sizeof(line), "%s = %s", g->globals[i].name, g->globals[i].value);
        br_ui_label(ui, line);
    }
    br_ui_panel_end(ui);
}

/* The tabs of the open scripts, then the scene's scripts that aren't open. */
static void file_tabs(editor *ed, br_rect bar) {
    br_ui *ui = &ed->ui;
    ed_code *c = &ed->code;
    dbg_strip s;
    const char *scripts[64];
    int n = scene_scripts(ed, scripts, 64), i, close = -1;
    br_ui_panel_begin(ui, "code_tabs", bar);
    s.ui = ui;
    s.x = bar.x + 6;
    s.y = bar.y + 2;
    s.h = ui->style.row;
    for (i = 0; i < c->count; ++i) {
        const ed_code_file *f = c->files[i];
        char label[128];
        float w;
        snprintf(label, sizeof(label), "%s%s##code%d", f->name, f->dirty ? " *" : "", i);
        w = br_measure_text(f->name, 1).x + (f->dirty ? 16 : 0) + 14;
        if (s.x + w + 18 > bar.x + bar.w - 4) break;
        if (bar_button(&s, label, w, i == c->current, true)) { c->current = i; snprintf(ed->dbg.file, sizeof(ed->dbg.file), "%s", f->path); }
        s.x -= 4;
        snprintf(label, sizeof(label), "x##close%d", i);
        if (bar_button(&s, label, 16, false, true)) close = i;
        s.x += 4;
    }
    if (n) s.x += 8;
    for (i = 0; i < n; ++i) {                                         /* the scene's scripts, to open */
        char label[96];
        float w;
        if (ed_code_find(ed, scripts[i]) >= 0) continue;
        snprintf(label, sizeof(label), "+ %s##scr%d", br_path_name(scripts[i]), i);
        w = br_measure_text(br_path_name(scripts[i]), 1).x + 30;
        if (s.x + w > bar.x + bar.w - 4) break;
        if (bar_button(&s, label, w, false, true) && !ed_code_open(ed, scripts[i], 0)) ed_status(ed, "Can't read %s", scripts[i]);
    }
    br_ui_panel_end(ui);
    if (close >= 0 && ed_code_close(ed, close, false)) ed_status(ed, "Closed the script");
}

void ed_debug_panel(editor *ed, br_rect rect) {
    ed_debug *g = &ed->dbg;
    br_ui *ui = &ed->ui;
    br_rect bar = br_rect_make(rect.x, rect.y, rect.w, 24);
    br_rect tabs = br_rect_make(rect.x, rect.y + 24, rect.w, 22);
    float side = g->stopped || ed->doc.playing ? (rect.w > 640 ? 300 : rect.w * 0.4f) : (rect.w > 640 ? 230 : rect.w * 0.3f);
    br_rect src = br_rect_make(rect.x, rect.y + 46, rect.w - side, rect.h - 46);
    br_rect state = br_rect_make(rect.x + rect.w - side, rect.y + 46, side, rect.h - 46);
    dbg_strip s;
    bool playing = ed->doc.playing;
    ed_code_file *f = ed_code_current(ed);
    br_ui_panel_begin(ui, "debug_bar", bar);
    s.ui = ui;
    s.x = bar.x + 6;
    s.y = bar.y + 4;
    s.h = ui->style.row;
    if (bar_button(&s, "Save##code", 44, false, f && f->dirty && !playing && !g->stopped)) ed_code_save(ed, ed->code.current);
    s.x += 8;
    if (bar_button(&s, "Continue F5", 92, false, g->stopped)) g->action = BR_DEBUG_CONTINUE;
    if (bar_button(&s, "Over F10", 72, false, g->stopped)) g->action = BR_DEBUG_STEP_OVER;
    if (bar_button(&s, "Into F11", 72, false, g->stopped)) g->action = BR_DEBUG_STEP_INTO;
    if (bar_button(&s, "Out", 40, false, g->stopped)) g->action = BR_DEBUG_STEP_OUT;
    if (bar_button(&s, "Pause", 52, false, playing && !g->stopped)) { br_script_debug_pause(); ed_status(ed, "Pausing at the next line a script runs"); }
    if (bar_button(&s, "Stop##dbg", 44, false, g->stopped)) { g->action = BR_DEBUG_DETACH; g->stop_after = true; }
    s.x += 8;
    if (bar_button(&s, "Clear Breakpoints", 136, false, br_script_debug_breakpoint(-1, NULL, NULL) > 0)) {
        br_script_debug_clear_breakpoints();
        ed_status(ed, "Breakpoints cleared");
    }
    br_ui_panel_end(ui);
    file_tabs(ed, tabs);
    f = ed_code_current(ed);
    ed_code_view(ed, src, g->stopped && f && strcmp(f->path, g->here_file) == 0 ? g->here : 0);
    draw_state(ed, state);
}
