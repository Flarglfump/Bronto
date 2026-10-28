/*
 * BRONTO EDITOR - the script editor: the Scripts tab under the scene view.
 *
 * Scripts open here (double-click one in Files, "Edit Script" on a Script
 * component, a message's file:line in the console), each in a tab of its own:
 *
 *   - typing, selecting with the mouse or Shift + the arrows, Ctrl+A / C / X / V,
 *     Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) to undo and redo, Ctrl+S to save;
 *   - Enter keeps the line's indent (one more after a '{'), Tab / Shift+Tab
 *     indent and unindent (the selected lines, if there are several), a '}'
 *     typed on an empty line steps back, Ctrl+/ comments lines out and in,
 *     Ctrl+D copies the line below itself;
 *   - BrontoScript in colour, the line numbers down the side, and mistakes:
 *     the script is checked as you type (br_script_check), the line with the
 *     first mistake is marked and the message is under the text;
 *   - the debugger's breakpoints: click a line's number. They move with the
 *     lines when lines are added or taken out above them.
 *
 * A file changed by another program is read again (when it has no unsaved
 * changes here). Play saves every changed script first, so the game runs what
 * you see. While a game is playing (or stopped at a breakpoint) the text is
 * read-only: the keyboard is the game's.
 *
 * The font is the engine's 8x8 one, so every byte is one column (a tab is
 * shown as one space; Tab inserts spaces).
 */
#include "editor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_H 13.0f
#define GUTTER 52.0f
#define CHAR_W 8.0f
#define PAD 4.0f
#define STATUS_H 16.0f
#define UNDO_MAX 200
#define INDENT 4

enum { G_NONE = 0, G_TYPE, G_DELETE, G_OTHER };

/* ---- the text --------------------------------------------------------------------------- */

static bool reserve(ed_code_file *f, int len) {
    if (len + 1 > f->cap) {
        int cap = f->cap ? f->cap : 256;
        char *t;
        while (cap < len + 1) cap *= 2;
        if (!(t = (char *)br_realloc(f->text, (size_t)cap))) return false;
        f->text = t;
        f->cap = cap;
    }
    return true;
}

static void relines(ed_code_file *f) {
    int i, n = 1;
    for (i = 0; i < f->len; ++i) if (f->text[i] == '\n') ++n;
    if (n + 1 > f->line_cap) {
        int cap = f->line_cap ? f->line_cap : 64, *ls;
        while (cap < n + 1) cap *= 2;
        if (!(ls = (int *)br_realloc(f->line_start, sizeof(int) * (size_t)cap))) return;
        f->line_start = ls;
        f->line_cap = cap;
    }
    f->line_start[0] = 0;
    n = 1;
    for (i = 0; i < f->len; ++i) if (f->text[i] == '\n') f->line_start[n++] = i + 1;
    f->line_start[n] = f->len + 1;
    f->line_count = n;
}

int ed_code_line_of(const ed_code_file *f, int pos) {          /* 0-based */
    int lo = 0, hi = f->line_count - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (f->line_start[mid] <= pos) lo = mid; else hi = mid - 1;
    }
    return lo;
}
static int line_end(const ed_code_file *f, int line) { return f->line_start[line + 1] - 1; }
static int line_len(const ed_code_file *f, int line) { return line_end(f, line) - f->line_start[line]; }
static int col_of(const ed_code_file *f, int pos) { return pos - f->line_start[ed_code_line_of(f, pos)]; }
static int pos_at(const ed_code_file *f, int line, int col) {
    if (line < 0) return 0;
    if (line >= f->line_count) return f->len;
    if (col < 0) col = 0;
    if (col > line_len(f, line)) col = line_len(f, line);
    return f->line_start[line] + col;
}
static int sel_lo(const ed_code_file *f) { return f->cursor < f->anchor ? f->cursor : f->anchor; }
static int sel_hi(const ed_code_file *f) { return f->cursor > f->anchor ? f->cursor : f->anchor; }
static bool has_sel(const ed_code_file *f) { return f->cursor != f->anchor; }
static bool word_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

/* ---- breakpoints follow their lines ----------------------------------------------------------- */

/* Lines after `after` (1-based) moved by `delta`; with delta < 0 the lines
 * after+1 .. after-delta went away, and their breakpoints with them. */
static void shift_breakpoints(const ed_code_file *f, int after, int delta) {
    int count = br_script_debug_breakpoint(-1, NULL, NULL), i, n = 0;
    int lines[256];
    if (!delta || !count) return;
    for (i = 0; i < count && n < 256; ++i) {
        const char *file;
        int at;
        br_script_debug_breakpoint(i, &file, &at);
        if (strcmp(file, f->path) == 0 && at > after) lines[n++] = at;
    }
    for (i = 0; i < n; ++i) br_script_debug_set_breakpoint(f->path, lines[i], false);
    for (i = 0; i < n; ++i) {
        int to = lines[i] + delta;
        if (delta < 0 && lines[i] <= after - delta) continue;      /* its line was taken out */
        if (to >= 1) br_script_debug_set_breakpoint(f->path, to, true);
    }
}

static int count_newlines(const char *s, int n) {
    int i, k = 0;
    for (i = 0; i < n; ++i) if (s[i] == '\n') ++k;
    return k;
}

/* ---- undo --------------------------------------------------------------------------------- */

static void snap_free(ed_code_snap *s) { br_free(s->text); s->text = NULL; }

static bool snap_take(const ed_code_file *f, ed_code_snap *s) {
    s->text = (char *)br_alloc((size_t)f->len + 1);
    if (!s->text) return false;
    memcpy(s->text, f->text, (size_t)f->len + 1);
    s->len = f->len;
    s->cursor = f->cursor;
    s->anchor = f->anchor;
    return true;
}

static void stack_push(ed_code_snap **stack, int *count, const ed_code_file *f) {
    ed_code_snap s;
    if (!*stack && !(*stack = (ed_code_snap *)br_alloc(sizeof(ed_code_snap) * UNDO_MAX))) return;
    if (!snap_take(f, &s)) return;
    if (*count == UNDO_MAX) {                                  /* the oldest goes */
        snap_free(&(*stack)[0]);
        memmove(*stack, *stack + 1, sizeof(ed_code_snap) * (UNDO_MAX - 1));
        --*count;
    }
    (*stack)[(*count)++] = s;
}

static void stack_clear(ed_code_snap *stack, int *count) {
    while (*count > 0) snap_free(&stack[--*count]);
}

static void schedule_check(ed_code_file *f, double now, double wait) { f->check_at = now + wait; }

/* Puts a whole text back (undo / redo / reading the file again). */
static void restore(ed_code_file *f, const char *text, int len, int cursor, int anchor, double now) {
    int prefix = 0, old_line, old_count = f->line_count;
    while (prefix < len && prefix < f->len && text[prefix] == f->text[prefix]) ++prefix;
    old_line = ed_code_line_of(f, prefix) + 1;
    if (!reserve(f, len)) return;
    memcpy(f->text, text, (size_t)len);
    f->len = len;
    f->text[len] = 0;
    relines(f);
    shift_breakpoints(f, old_line, f->line_count - old_count);
    f->cursor = cursor < 0 ? 0 : cursor > len ? len : cursor;
    f->anchor = anchor < 0 ? 0 : anchor > len ? len : anchor;
    schedule_check(f, now, 0.3);
}

static void undo_step(ed_code_file *f, bool redo, double now) {
    ed_code_snap **from = redo ? &f->redo : &f->undo, **to = redo ? &f->undo : &f->redo;
    int *from_n = redo ? &f->redo_count : &f->undo_count, *to_n = redo ? &f->undo_count : &f->redo_count;
    ed_code_snap s;
    if (*from_n == 0) return;
    stack_push(to, to_n, f);
    s = (*from)[--*from_n];
    restore(f, s.text, s.len, s.cursor, s.anchor, now);
    snap_free(&s);
    f->group = G_NONE;
    f->dirty = true;
}

/* Before an edit: start a new undo step unless it carries on the one before
 * (typing along, a second or so apart). */
static void begin_edit(ed_code_file *f, int kind, double now) {
    if (kind != f->group || kind == G_OTHER || now - f->group_time > 1.5 || f->cursor != f->group_end || has_sel(f)) {
        stack_push(&f->undo, &f->undo_count, f);
        if (f->redo) stack_clear(f->redo, &f->redo_count);
    }
    f->group = kind;
}

/* The edit itself: [lo, hi) becomes `s` (n bytes); the cursor ends after it. */
static void replace(ed_code_file *f, int lo, int hi, const char *s, int n, double now) {
    int gone = count_newlines(f->text + lo, hi - lo), added = count_newlines(s, n);
    int line = ed_code_line_of(f, lo) + 1;
    int col = lo - f->line_start[line - 1];
    if (!reserve(f, f->len - (hi - lo) + n)) return;
    memmove(f->text + lo + n, f->text + hi, (size_t)(f->len - hi + 1));
    memcpy(f->text + lo, s, (size_t)n);
    f->len += n - (hi - lo);
    relines(f);
    /* lines put in at the very start of a line push that line (and its breakpoint) down */
    shift_breakpoints(f, (col == 0 && hi == lo && n > 0 && s[n - 1] == '\n') ? line - 1 : line, added - gone);
    f->cursor = f->anchor = lo + n;
    f->dirty = true;
    f->group_time = now;
    f->group_end = f->cursor;
    f->want_col = -1;
    schedule_check(f, now, 0.4);
}

static void insert_text(ed_code_file *f, const char *s, int n, int kind, double now) {
    begin_edit(f, kind, now);
    replace(f, sel_lo(f), sel_hi(f), s, n, now);
}

/* ---- the files ---------------------------------------------------------------------------- */

static void full_path(editor *ed, const char *path, char *out, size_t size) {
    BR_UNUSED(ed);
    if (path[0] == '/' || (path[0] && path[1] == ':')) snprintf(out, size, "%s", path);
    else snprintf(out, size, "%s%s", br_assets_root(), path);
}

static int64_t file_time(const char *path) {
    br_dir_entry st;
    return br_fs_stat(path, &st) ? st.modified : 0;
}

static void file_free(ed_code_file *f) {
    if (!f) return;
    if (f->undo) { stack_clear(f->undo, &f->undo_count); br_free(f->undo); }
    if (f->redo) { stack_clear(f->redo, &f->redo_count); br_free(f->redo); }
    br_free(f->text);
    br_free(f->line_start);
    br_free(f);
}

ed_code_file *ed_code_current(editor *ed) {
    ed_code *c = &ed->code;
    return c->current >= 0 && c->current < c->count ? c->files[c->current] : NULL;
}

int ed_code_find(editor *ed, const char *path) {
    char full[ED_PATH_MAX * 2];
    int i;
    full_path(ed, path, full, sizeof(full));
    for (i = 0; i < ed->code.count; ++i) if (strcmp(ed->code.files[i]->path, full) == 0) return i;
    return -1;
}

/* Checks the text now: the first mistake's message and line. */
static void check_now(ed_code_file *f) {
    char err[256];
    const char *p;
    f->check_at = 0;
    f->error[0] = 0;
    f->error_line = 0;
    if (br_script_check(f->text, f->name, err, sizeof(err))) return;
    snprintf(f->error, sizeof(f->error), "%s", err);
    for (p = err; *p; ++p)                                        /* "player.bs:12: ..." */
        if (*p == ':' && p[1] >= '0' && p[1] <= '9') {
            const char *q = p + 1;
            int line = 0;
            while (*q >= '0' && *q <= '9') line = line * 10 + (*q++ - '0');
            if (*q == ':') { f->error_line = line; break; }
        }
}

static bool read_into(ed_code_file *f, double now, bool keep_place) {
    size_t size = 0;
    char *text = (char *)br_file_read_all(f->path, &size);
    int cursor = keep_place ? f->cursor : 0;
    if (!text) return false;
    if (f->text) restore(f, text, (int)size, cursor, cursor, now);
    else {
        if (!reserve(f, (int)size)) { br_free(text); return false; }
        memcpy(f->text, text, size);
        f->len = (int)size;
        f->text[size] = 0;
        relines(f);
    }
    br_free(text);
    f->dirty = false;
    f->modified = file_time(f->path);
    check_now(f);
    return true;
}

bool ed_code_open(editor *ed, const char *path, int line) {
    ed_code *c = &ed->code;
    ed_code_file *f;
    int at;
    if (!path || !path[0]) return false;
    if ((at = ed_code_find(ed, path)) < 0) {
        if (c->count == ED_CODE_FILES) {                          /* full: the oldest saved one makes room */
            int k;
            for (k = 0; k < c->count && c->files[k]->dirty; ++k) {}
            if (k == c->count) { ed_status(ed, "Too many scripts open with unsaved changes: save or close one"); return false; }
            ed_code_close(ed, k, true);
        }
        if (!(f = (ed_code_file *)br_alloc(sizeof(*f)))) return false;
        memset(f, 0, sizeof(*f));
        full_path(ed, path, f->path, sizeof(f->path));
        snprintf(f->name, sizeof(f->name), "%s", br_path_name(f->path));
        f->want_col = -1;
        if (!read_into(f, ed->engine ? ed->engine->time : 0, false)) { file_free(f); return false; }
        at = c->count;
        c->files[c->count++] = f;
    }
    c->current = at;
    f = c->files[at];
    snprintf(ed->dbg.file, sizeof(ed->dbg.file), "%s", f->path);
    if (line > 0) {
        f->cursor = f->anchor = pos_at(f, line - 1, 0);
        while (f->cursor < f->len && (f->text[f->cursor] == ' ' || f->text[f->cursor] == '\t')) f->anchor = ++f->cursor;
        c->scroll_to = line;
    }
    ed->bottom_tab = ED_TAB_DEBUG;
    ed_panel_set_open(ed, ED_PANEL_BOTTOM, true);
    return true;
}

bool ed_code_save(editor *ed, int index) {
    ed_code_file *f = index >= 0 && index < ed->code.count ? ed->code.files[index] : NULL;
    if (!f) return false;
    if (!br_file_write_all(f->path, f->text, (size_t)f->len)) { ed_status(ed, "Couldn't save %s", f->name); return false; }
    f->dirty = false;
    f->close_asked = false;
    f->modified = file_time(f->path);
    check_now(f);
    if (f->error[0]) ed_status(ed, "Saved %s - it has a mistake: %s", f->name, f->error);
    else ed_status(ed, "Saved %s", f->name);
    return true;
}

int ed_code_dirty_count(const editor *ed) {
    int i, n = 0;
    for (i = 0; i < ed->code.count; ++i) n += ed->code.files[i]->dirty;
    return n;
}

int ed_code_save_all(editor *ed) {
    int i, n = 0;
    for (i = 0; i < ed->code.count; ++i) if (ed->code.files[i]->dirty && ed_code_save(ed, i)) ++n;
    return n;
}

/* Closing one with unsaved changes asks first (close again to drop them), unless `force`. */
bool ed_code_close(editor *ed, int index, bool force) {
    ed_code *c = &ed->code;
    ed_code_file *f = index >= 0 && index < c->count ? c->files[index] : NULL;
    if (!f) return false;
    if (f->dirty && !force && !f->close_asked) {
        f->close_asked = true;
        ed_status(ed, "%s has unsaved changes: Ctrl+S saves them, closing it again throws them away", f->name);
        return false;
    }
    if (strcmp(ed->dbg.file, f->path) == 0) ed->dbg.file[0] = 0;
    file_free(f);
    memmove(c->files + index, c->files + index + 1, sizeof(c->files[0]) * (size_t)(c->count - index - 1));
    c->count--;
    if (c->current >= c->count) c->current = c->count - 1;
    else if (c->current > index) c->current--;
    if (ed_code_current(ed)) snprintf(ed->dbg.file, sizeof(ed->dbg.file), "%s", ed_code_current(ed)->path);
    return true;
}

void ed_code_close_all(editor *ed) {
    while (ed->code.count) ed_code_close(ed, ed->code.count - 1, true);
    ed->code.focused = false;
}

void ed_code_init(editor *ed) {
    memset(&ed->code, 0, sizeof(ed->code));
    ed->code.current = -1;
}

void ed_code_destroy(editor *ed) { ed_code_close_all(ed); }

bool ed_code_has_keyboard(const editor *ed) { return ed->code.focused && ed->code.count > 0; }

/* Each frame: files changed elsewhere, and the checks that are due. */
void ed_code_poll(editor *ed) {
    ed_code *c = &ed->code;
    double now = ed->engine ? ed->engine->time : 0;
    int i;
    if (!c->drawn) c->focused = false;                            /* the tab isn't showing */
    c->drawn = false;
    for (i = 0; i < c->count; ++i) {
        ed_code_file *f = c->files[i];
        if (f->check_at > 0 && now >= f->check_at) check_now(f);
    }
    if (now < c->poll_at) return;
    c->poll_at = now + 1.0;
    for (i = 0; i < c->count; ++i) {
        ed_code_file *f = c->files[i];
        int64_t t = file_time(f->path);
        if (!t || t == f->modified) continue;
        if (f->dirty) { f->modified = t; ed_status(ed, "%s was changed by another program: saving here will replace that", f->name); }
        else if (read_into(f, now, true)) ed_status(ed, "%s changed on disk: read it again", f->name);
    }
}

/* ---- editing commands -------------------------------------------------------------------------- */

static void indent_lines(ed_code_file *f, bool out, double now) {
    int first = ed_code_line_of(f, sel_lo(f)), last = ed_code_line_of(f, sel_hi(f)), l;
    int cur_line = ed_code_line_of(f, f->cursor), cur_col = col_of(f, f->cursor);
    int anc_line = ed_code_line_of(f, f->anchor), anc_col = col_of(f, f->anchor);
    int shift_cur = 0, shift_anc = 0;
    if (last > first && f->line_start[last] == sel_hi(f)) --last;  /* (the selection ends at a line's start) */
    begin_edit(f, G_OTHER, now);
    for (l = first; l <= last; ++l) {
        int start = f->line_start[l], n = 0;
        if (out) {
            while (n < INDENT && start + n < f->len && f->text[start + n] == ' ') ++n;
            if (n) replace(f, start, start + n, "", 0, now);
            n = -n;
        } else if (line_len(f, l) > 0 || first == last) {
            replace(f, start, start, "    ", INDENT, now);
            n = INDENT;
        }
        if (l == cur_line) shift_cur = n;
        if (l == anc_line) shift_anc = n;
    }
    f->cursor = pos_at(f, cur_line, cur_col + shift_cur);
    f->anchor = pos_at(f, anc_line, anc_col + shift_anc);
}

static void toggle_comment(ed_code_file *f, double now) {
    int first = ed_code_line_of(f, sel_lo(f)), last = ed_code_line_of(f, sel_hi(f)), l;
    int cur_line = ed_code_line_of(f, f->cursor), cur_col = col_of(f, f->cursor), shift = 0;
    bool all = true, multi;
    if (last > first && f->line_start[last] == sel_hi(f)) --last;
    multi = last > first;
    for (l = first; l <= last; ++l) {                              /* every (non-empty) line commented: take them out */
        int p = f->line_start[l], e = line_end(f, l);
        while (p < e && f->text[p] == ' ') ++p;
        if (p < e && f->text[p] != '#') all = false;
    }
    begin_edit(f, G_OTHER, now);
    for (l = first; l <= last; ++l) {
        int p = f->line_start[l], e = line_end(f, l), n;
        while (p < e && f->text[p] == ' ') ++p;
        if (p == e) continue;
        if (all) { n = p + 1 < e && f->text[p + 1] == ' ' ? 2 : 1; replace(f, p, p + n, "", 0, now); n = -n; }
        else { replace(f, p, p, "# ", 2, now); n = 2; }
        if (l == cur_line && cur_col >= p - f->line_start[l]) shift = n;
    }
    if (multi) { f->anchor = f->line_start[first]; f->cursor = line_end(f, last); }
    else f->cursor = f->anchor = pos_at(f, cur_line, cur_col + shift);
}

static void newline(ed_code_file *f, double now) {
    int line = ed_code_line_of(f, sel_lo(f)), start = f->line_start[line], p = start, back;
    char buf[160];
    int n = 0;
    buf[n++] = '\n';
    while (p < sel_lo(f) && f->text[p] == ' ' && n < 120) { buf[n++] = ' '; ++p; }
    back = sel_lo(f);
    while (back > start && f->text[back - 1] == ' ') --back;
    if (back > start && f->text[back - 1] == '{') { int k; for (k = 0; k < INDENT; ++k) buf[n++] = ' '; }
    begin_edit(f, G_OTHER, now);
    replace(f, sel_lo(f), sel_hi(f), buf, n, now);
}

static void type_char(ed_code_file *f, char ch, double now) {
    if (ch == '}' && !has_sel(f)) {                                /* on an empty line: back one step first */
        int line = ed_code_line_of(f, f->cursor), p = f->line_start[line], spaces = 0;
        while (p < f->cursor && f->text[p] == ' ') { ++p; ++spaces; }
        if (p == f->cursor && spaces >= INDENT) {
            begin_edit(f, G_OTHER, now);
            replace(f, f->cursor - INDENT, f->cursor, "}", 1, now);
            return;
        }
    }
    insert_text(f, &ch, 1, G_TYPE, now);
}

static void erase(ed_code_file *f, bool forward, bool word, double now) {
    int lo = sel_lo(f), hi = sel_hi(f);
    if (lo == hi) {
        if (forward) {
            if (hi >= f->len) return;
            ++hi;
            if (word) while (hi < f->len && word_char(f->text[hi])) ++hi;
        } else {
            int col = col_of(f, lo), start = lo - col, k = 0;
            if (lo == 0) return;
            while (k < col && f->text[start + k] == ' ') ++k;
            if (!word && col > 0 && k == col && col % INDENT == 0) lo -= INDENT;   /* indent: a step at a time */
            else if (!word && col > 0 && k == col) lo -= col % INDENT;
            else {
                --lo;
                if (word) while (lo > 0 && word_char(f->text[lo - 1])) --lo;
            }
        }
    }
    begin_edit(f, G_DELETE, now);
    replace(f, lo, hi, "", 0, now);
}

static void copy_selection(editor *ed, ed_code_file *f, bool cut, double now) {
    int lo = sel_lo(f), hi = sel_hi(f);
    char *text;
    if (lo == hi) {                                                /* nothing selected: the whole line */
        int line = ed_code_line_of(f, lo);
        lo = f->line_start[line];
        hi = line + 1 < f->line_count ? f->line_start[line + 1] : f->len;
    }
    if (!(text = (char *)br_alloc((size_t)(hi - lo) + 1))) return;
    memcpy(text, f->text + lo, (size_t)(hi - lo));
    text[hi - lo] = 0;
    if (ed->engine) br_clipboard_set(ed->engine, text);
    br_free(text);
    if (cut) { begin_edit(f, G_OTHER, now); replace(f, lo, hi, "", 0, now); }
}

static void paste(editor *ed, ed_code_file *f, double now) {
    char *text = ed->engine ? br_clipboard_get(ed->engine) : NULL;
    char *clean;
    int i, n = 0;
    if (!text) return;
    if ((clean = (char *)br_alloc(strlen(text) * INDENT + 1)) != NULL) {
        for (i = 0; text[i]; ++i) {                                /* tabs as spaces, no \r, only what the font has */
            unsigned char ch = (unsigned char)text[i];
            if (ch == '\r') continue;
            if (ch == '\t') { int k; for (k = 0; k < INDENT; ++k) clean[n++] = ' '; continue; }
            if (ch == '\n' || (ch >= 32 && ch < 127)) clean[n++] = (char)ch;
        }
        insert_text(f, clean, n, G_OTHER, now);
        br_free(clean);
    }
    br_free(text);
}

static void duplicate_line(ed_code_file *f, double now) {
    int line = ed_code_line_of(f, f->cursor), col = col_of(f, f->cursor);
    int start = f->line_start[line], end = line_end(f, line), n = end - start;
    char *copy = (char *)br_alloc((size_t)n + 2);
    if (!copy) return;
    copy[0] = '\n';
    memcpy(copy + 1, f->text + start, (size_t)n);
    begin_edit(f, G_OTHER, now);
    replace(f, end, end, copy, n + 1, now);
    br_free(copy);
    f->cursor = f->anchor = pos_at(f, line + 1, col);
}

/* ---- moving about -------------------------------------------------------------------------------- */

static void move_to(ed_code_file *f, int pos, bool select) {
    f->cursor = pos < 0 ? 0 : pos > f->len ? f->len : pos;
    if (!select) f->anchor = f->cursor;
    f->group = G_NONE;
}

static int word_left(const ed_code_file *f, int p) {
    while (p > 0 && !word_char(f->text[p - 1])) --p;
    while (p > 0 && word_char(f->text[p - 1])) --p;
    return p;
}
static int word_right(const ed_code_file *f, int p) {
    while (p < f->len && !word_char(f->text[p])) ++p;
    while (p < f->len && word_char(f->text[p])) ++p;
    return p;
}

static void vertical(ed_code_file *f, int lines, bool select) {
    int line = ed_code_line_of(f, f->cursor);
    if (f->want_col < 0) f->want_col = col_of(f, f->cursor);
    if (line + lines < 0) { move_to(f, 0, select); return; }
    if (line + lines >= f->line_count) { move_to(f, f->len, select); return; }
    move_to(f, pos_at(f, line + lines, f->want_col), select);
}

static void keep_visible(ed_code *c, ed_code_file *f, br_rect text) {
    int line = ed_code_line_of(f, f->cursor), col = col_of(f, f->cursor);
    float y = (float)line * LINE_H, x = (float)col * CHAR_W, w = text.w - GUTTER - PAD * 2;
    BR_UNUSED(c);
    if (y < f->scroll_y) f->scroll_y = y;
    if (y + LINE_H > f->scroll_y + text.h) f->scroll_y = y + LINE_H - text.h;
    if (x < f->scroll_x) f->scroll_x = floorf(x / CHAR_W / 8) * 8 * CHAR_W;
    if (x + CHAR_W > f->scroll_x + w) f->scroll_x = ceilf((x + CHAR_W - w) / CHAR_W / 8) * 8 * CHAR_W;
}

/* The keyboard, while the text has it. Returns true if the cursor moved or the text changed. */
static bool keys(editor *ed, ed_code_file *f, bool read_only) {
    const br_input *in = ed->ui.in;
    double now = ed->engine ? ed->engine->time : 0;
    bool cmd = (in->mods & (BR_MOD_CONTROL | BR_MOD_SUPER)) != 0, shift = (in->mods & BR_MOD_SHIFT) != 0;
    bool alt = (in->mods & BR_MOD_ALT) != 0;
    int before = f->cursor, len = f->len, i, page = 20;
    bool edit = !read_only, tried = false;
#define K(k) br_key_repeated(in, k)
    /* moving and selecting work either way */
    if (K(BR_KEY_LEFT)) move_to(f, has_sel(f) && !shift ? sel_lo(f) : cmd || alt ? word_left(f, f->cursor) : f->cursor - 1, shift), f->want_col = -1;
    if (K(BR_KEY_RIGHT)) move_to(f, has_sel(f) && !shift ? sel_hi(f) : cmd || alt ? word_right(f, f->cursor) : f->cursor + 1, shift), f->want_col = -1;
    if (K(BR_KEY_UP)) vertical(f, -1, shift);
    if (K(BR_KEY_DOWN)) vertical(f, 1, shift);
    if (K(BR_KEY_PAGE_UP)) vertical(f, -page, shift);
    if (K(BR_KEY_PAGE_DOWN)) vertical(f, page, shift);
    if (K(BR_KEY_HOME)) {
        if (cmd) move_to(f, 0, shift);
        else {                                                     /* the first non-space, then the line's start */
            int line = ed_code_line_of(f, f->cursor), p = f->line_start[line];
            while (p < line_end(f, line) && f->text[p] == ' ') ++p;
            move_to(f, f->cursor == p ? f->line_start[line] : p, shift);
        }
        f->want_col = -1;
    }
    if (K(BR_KEY_END)) { move_to(f, cmd ? f->len : line_end(f, ed_code_line_of(f, f->cursor)), shift); f->want_col = -1; }
    if (cmd && br_key_pressed(in, BR_KEY_A)) { f->anchor = 0; f->cursor = f->len; }
    if (cmd && br_key_pressed(in, BR_KEY_C)) copy_selection(ed, f, false, now);
    if (cmd && br_key_pressed(in, BR_KEY_S)) { if (f->dirty) ed_code_save(ed, ed->code.current); else ed_status(ed, "%s is saved", f->name); }
    if (br_key_pressed(in, BR_KEY_ESCAPE)) { if (has_sel(f)) f->anchor = f->cursor; else ed->code.focused = false; }
    if (edit) {
        for (i = 0; i < in->text_count; ++i) {
            uint32_t cp = in->text[i];
            if (cmd && !alt) continue;                             /* (shortcuts, not text) */
            if (cp >= 32 && cp < 127) type_char(f, (char)cp, now);
        }
        if (K(BR_KEY_ENTER) || K(BR_KEY_KP_ENTER)) newline(f, now);
        if (K(BR_KEY_BACKSPACE)) erase(f, false, cmd || alt, now);
        if (K(BR_KEY_DELETE)) erase(f, true, cmd || alt, now);
        if (K(BR_KEY_TAB)) {
            if (shift || ed_code_line_of(f, sel_lo(f)) != ed_code_line_of(f, sel_hi(f))) indent_lines(f, shift, now);
            else { int col = col_of(f, sel_lo(f)), n = INDENT - col % INDENT; insert_text(f, "    ", n, G_OTHER, now); }
        }
        if (cmd && br_key_pressed(in, BR_KEY_X)) copy_selection(ed, f, true, now);
        if (cmd && br_key_pressed(in, BR_KEY_V)) paste(ed, f, now);
        if (cmd && br_key_pressed(in, BR_KEY_D)) duplicate_line(f, now);
        if (cmd && br_key_pressed(in, BR_KEY_SLASH)) toggle_comment(f, now);
        if (cmd && br_key_pressed(in, BR_KEY_Z)) undo_step(f, shift, now);
        if (cmd && br_key_pressed(in, BR_KEY_Y)) undo_step(f, true, now);
    } else {
        tried = (in->text_count > 0 && !(cmd && !alt)) || K(BR_KEY_ENTER) || K(BR_KEY_BACKSPACE) || K(BR_KEY_DELETE) || K(BR_KEY_TAB) ||
                (cmd && (br_key_pressed(in, BR_KEY_V) || br_key_pressed(in, BR_KEY_X) || br_key_pressed(in, BR_KEY_Z)));
        if (tried) ed_status(ed, ed->dbg.stopped ? "Stopped at a breakpoint: the script can't change now" : "The game is playing: stop it (F5) to change its scripts");
    }
#undef K
    return f->cursor != before || f->len != len;
}

/* ---- colours -------------------------------------------------------------------------------------- */

static const char *const k_words[] = {
    "var", "func", "if", "elif", "else", "while", "for", "in", "return", "break", "continue", "and", "or", "not",
    "class", "struct", "public", "private", "protected", "static", "const", "override", "this", "super",
    "unsigned", "component", "enum", "self",
};
static const char *const k_values[] = { "true", "false", "nil" };
static const char *const k_types[] = {
    "int", "number", "float", "bool", "string", "vec2", "vec3", "color", "entity", "void", "list", "map", "any",
};

static bool in_list(const char *const *list, int n, const char *s, int len) {
    int i;
    for (i = 0; i < n; ++i) if ((int)strlen(list[i]) == len && strncmp(list[i], s, (size_t)len) == 0) return true;
    return false;
}

static bool is_builtin(const char *s, int len) {
    int i, n = br_script_builtin_count();
    for (i = 0; i < n; ++i) {
        const char *b = br_script_builtin_name(i);
        if ((int)strlen(b) == len && strncmp(b, s, (size_t)len) == 0) return true;
    }
    return false;
}

typedef enum { C_TEXT = 0, C_WORD, C_TYPE, C_VALUE, C_BUILTIN, C_STRING, C_NUMBER, C_COMMENT, C_AT, C_COUNT } code_colour;

static br_color colour(code_colour k) {
    static const br_color c[C_COUNT] = {
        { 222, 224, 230, 255 },      /* text */
        { 198, 146, 234, 255 },      /* keywords */
        { 102, 204, 204, 255 },      /* types */
        { 240, 140, 110, 255 },      /* true / false / nil */
        { 120, 170, 245, 255 },      /* the engine's functions */
        { 230, 190, 120, 255 },      /* strings */
        { 160, 214, 130, 255 },      /* numbers */
        { 120, 128, 140, 255 },      /* comments */
        { 236, 210, 100, 255 },      /* @strict */
    };
    return c[k];
}

/* Colours for one line's bytes. */
static void colour_line(const char *s, int n, unsigned char *out) {
    int i = 0;
    memset(out, C_TEXT, (size_t)n);
    while (i < n) {
        char ch = s[i];
        int j = i + 1;
        if (ch == '#') { memset(out + i, C_COMMENT, (size_t)(n - i)); return; }
        if (ch == '"' || ch == '\'') {
            while (j < n && s[j] != ch) { if (s[j] == '\\' && j + 1 < n) ++j; ++j; }
            if (j < n) ++j;
            memset(out + i, C_STRING, (size_t)(j - i));
        } else if (ch == '@') {
            while (j < n && word_char(s[j])) ++j;
            memset(out + i, C_AT, (size_t)(j - i));
        } else if ((ch >= '0' && ch <= '9') || (ch == '.' && j < n && s[j] >= '0' && s[j] <= '9')) {
            while (j < n && (word_char(s[j]) || s[j] == '.')) ++j;
            memset(out + i, C_NUMBER, (size_t)(j - i));
        } else if (word_char(ch)) {
            int k = j;
            code_colour c = C_TEXT;
            while (j < n && word_char(s[j])) ++j;
            if (in_list(k_words, (int)BR_ARRAY_COUNT(k_words), s + i, j - i)) c = C_WORD;
            else if (in_list(k_types, (int)BR_ARRAY_COUNT(k_types), s + i, j - i)) c = C_TYPE;
            else if (in_list(k_values, (int)BR_ARRAY_COUNT(k_values), s + i, j - i)) c = C_VALUE;
            else {
                while (k < n && s[k] == ' ') ++k;
                if (k < n && s[k] == '(' && (i == 0 || s[i - 1] != '.') && is_builtin(s + i, j - i)) c = C_BUILTIN;
            }
            memset(out + i, (int)c, (size_t)(j - i));
        }
        i = j;
    }
}

/* ---- drawing -------------------------------------------------------------------------------------- */

static int hit_pos(const ed_code_file *f, br_rect text, br_vec2 m) {
    int line = (int)floorf((m.y - text.y + f->scroll_y) / LINE_H);
    int col = (int)floorf((m.x - text.x - GUTTER - PAD + f->scroll_x) / CHAR_W + 0.5f);
    if (line < 0) return 0;
    if (line >= f->line_count) return f->len;
    return pos_at(f, line, col);
}

/* The text of the current file, in `rect`. `here`: the line a stopped script
 * is at (0: none). Read-only while playing. */
void ed_code_view(editor *ed, br_rect rect, int here) {
    ed_code *c = &ed->code;
    br_ui *ui = &ed->ui;
    br_renderer *r = ui->r;
    ed_code_file *f = ed_code_current(ed);
    bool read_only = ed->doc.playing || ed->dbg.stopped;
    br_rect text = br_rect_make(rect.x, rect.y, rect.w, rect.h - STATUS_H);
    br_rect status = br_rect_make(rect.x, rect.y + rect.h - STATUS_H, rect.w, STATUS_H);
    int flags, from, to, k;
    double now = ed->engine ? ed->engine->time : 0;
    c->drawn = true;
    br_ui_panel_begin(ui, "code_view", rect);
    br_fill_rect(r, rect, ui->style.field);
    if (!f) {
        br_ui_label_colored(ui, "No script open. Double-click one in Files, or pick one of the scene's above.", ui->style.text_dim);
        br_ui_label_colored(ui, "Click a line's number to set a breakpoint: a playing game stops when it gets there.", ui->style.text_dim);
        br_ui_panel_end(ui);
        c->focused = false;
        return;
    }
    if (c->scroll_to > 0) {                                        /* bring that line into view (a third of the way down) */
        float want = (float)(c->scroll_to - 1) * LINE_H - text.h / 3;
        f->scroll_y = want < 0 ? 0 : want;
        c->scroll_to = 0;
    }
    /* the mouse */
    br_ui_place(ui, text);
    flags = br_ui_area(ui, "##code", 0);
    if (ui->pressed && !(flags & BR_UI_PRESSED) && !br_ui_menu_active(ui)) c->focused = false;   /* a click elsewhere */
    if ((flags & BR_UI_PRESSED) && ui->mouse.x < text.x + GUTTER) {
        int line = (int)floorf((ui->mouse.y - text.y + f->scroll_y) / LINE_H) + 1;
        if (line >= 1 && line <= f->line_count) {
            bool on = !br_script_debug_has_breakpoint(f->path, line);
            br_script_debug_set_breakpoint(f->path, line, on);
            ed_status(ed, on ? "Breakpoint at line %d: the game stops when it gets there" : "No breakpoint at line %d", line);
        }
    } else if (flags & BR_UI_PRESSED) {
        int p = hit_pos(f, text, ui->mouse);
        c->focused = true;
        c->selecting = true;
        if (flags & BR_UI_DOUBLE_CLICKED) {                        /* a word */
            int a = p, b = p;
            while (a > 0 && word_char(f->text[a - 1])) --a;
            while (b < f->len && word_char(f->text[b])) ++b;
            f->anchor = a;
            f->cursor = b;
            c->selecting = false;
        } else move_to(f, p, (ui->in->mods & BR_MOD_SHIFT) != 0);
        f->want_col = -1;
        c->blink = now;
    } else if (c->selecting && ui->down) {
        float my = ui->mouse.y;
        if (my < text.y) f->scroll_y -= LINE_H;                    /* dragging past an edge scrolls */
        if (my > text.y + text.h) f->scroll_y += LINE_H;
        f->cursor = hit_pos(f, text, ui->mouse);
    }
    if (!ui->down) c->selecting = false;
    if (br_rect_contains(rect, ui->mouse) && ui->wheel != 0 && !ui->wheel_used && !ui->disabled) {
        if (ui->in->mods & BR_MOD_SHIFT) f->scroll_x -= ui->wheel * 6 * CHAR_W;
        else f->scroll_y -= ui->wheel * 3 * LINE_H;
        ui->wheel_used = true;
    }
    if (ui->in->wheel_x != 0 && br_rect_contains(rect, ui->mouse)) f->scroll_x += ui->in->wheel_x * 6 * CHAR_W;
    /* the keyboard */
    if (c->focused && !ui->disabled && !br_ui_wants_keyboard(ui) && keys(ed, f, read_only)) { keep_visible(c, f, text); c->blink = now; }
    {   /* scrolling stays within the text */
        float most = (float)f->line_count * LINE_H - text.h + LINE_H * 3;
        if (f->scroll_y > most) f->scroll_y = most;
        if (f->scroll_y < 0) f->scroll_y = 0;
        if (f->scroll_x < 0) f->scroll_x = 0;
        f->scroll_x = floorf(f->scroll_x / CHAR_W) * CHAR_W;
    }
    /* the lines */
    from = (int)floorf(f->scroll_y / LINE_H);
    to = (int)ceilf((f->scroll_y + text.h) / LINE_H);
    if (to > f->line_count) to = f->line_count;
    {
        int col0 = (int)(f->scroll_x / CHAR_W), cols = (int)((text.w - GUTTER - PAD) / CHAR_W) + 1;
        int cur_line = ed_code_line_of(f, f->cursor), lo = sel_lo(f), hi = sel_hi(f);
        float x0 = text.x + GUTTER + PAD;
        br_recti saved = r->clip;
        br_recti clip = { (int)(text.x + GUTTER), (int)text.y, (int)(text.w - GUTTER), (int)text.h };
        br_set_clip(r, clip);
        for (k = from; k < to; ++k) {
            float y = text.y + (float)k * LINE_H - f->scroll_y;
            int a = f->line_start[k], n = line_len(f, k), i;
            unsigned char cols_of[512];
            if (k + 1 == here) br_fill_rect(r, br_rect_make(text.x, y, text.w, LINE_H), br_rgba(240, 200, 70, 70));
            else if (k + 1 == f->error_line) br_fill_rect(r, br_rect_make(text.x, y, text.w, LINE_H), br_rgba(230, 80, 70, 46));
            else if (c->focused && k == cur_line && !has_sel(f)) br_fill_rect(r, br_rect_make(text.x, y, text.w, LINE_H), br_rgba(255, 255, 255, 10));
            if (lo != hi && a + n >= lo && a <= hi) {               /* the selection on this line */
                int s0 = lo > a ? lo - a : 0, s1 = hi < a + n ? hi - a : n + (hi > a + n ? 1 : 0);
                if (s1 > s0) br_fill_rect(r, br_rect_make(x0 + (float)(s0 - col0) * CHAR_W, y, (float)(s1 - s0) * CHAR_W, LINE_H), ui->style.selection);
            }
            if (n > (int)sizeof(cols_of)) n = (int)sizeof(cols_of);
            colour_line(f->text + a, n, cols_of);
            for (i = col0; i < n && i < col0 + cols; ) {           /* runs of one colour */
                char run[160];
                int j = i, m = 0;
                while (j < n && j < col0 + cols && cols_of[j] == cols_of[i] && m < (int)sizeof(run) - 1) {
                    char ch = f->text[a + j];
                    run[m++] = (ch == '\t' || ch == '\r') ? ' ' : ch;
                    ++j;
                }
                run[m] = 0;
                br_draw_text(r, run, x0 + (float)(i - col0) * CHAR_W, y + 3, 1, colour((code_colour)cols_of[i]));
                i = j;
            }
            if (k + 1 == f->error_line) {                          /* a wavy line under the mistake's line */
                float x, w = (float)(n > col0 ? n - col0 : 1) * CHAR_W;
                for (x = 0; x < w; x += 4) br_fill_rect(r, br_rect_make(x0 + x, y + LINE_H - ((int)(x / 4) % 2 ? 2 : 1), 2, 1), br_rgb(240, 90, 80));
            }
        }
        if (c->focused && fmod(now - c->blink, 1.0) < 0.6) {      /* the cursor */
            int col = col_of(f, f->cursor);
            float y = text.y + (float)cur_line * LINE_H - f->scroll_y;
            br_fill_rect(r, br_rect_make(x0 + (float)(col - col0) * CHAR_W - 1, y, 2, LINE_H), read_only ? ui->style.text_dim : ui->style.accent);
        }
        br_set_clip(r, saved);
        /* the gutter: line numbers, breakpoints, where it's stopped */
        br_fill_rect(r, br_rect_make(text.x, text.y, GUTTER, text.h), br_rgba(0, 0, 0, 40));
        for (k = from; k < to; ++k) {
            float y = text.y + (float)k * LINE_H - f->scroll_y;
            char num[12];
            if (y + LINE_H <= text.y || y >= text.y + text.h) continue;
            if (br_script_debug_has_breakpoint(f->path, k + 1)) br_fill_circle(r, br_v2(text.x + 7, y + LINE_H / 2), 4, br_rgb(230, 80, 70));
            if (k + 1 == here) br_fill_triangle(r, br_v2(text.x + 42, y + 2), br_v2(text.x + 42, y + LINE_H - 2), br_v2(text.x + 50, y + LINE_H / 2), br_rgb(250, 210, 80));
            else if (k + 1 == f->error_line) br_fill_rect(r, br_rect_make(text.x + GUTTER - 4, y + 1, 3, LINE_H - 2), br_rgb(240, 90, 80));
            snprintf(num, sizeof(num), "%4d", k + 1);
            br_draw_text(r, num, text.x + 8, y + 3, 1, k == cur_line ? ui->style.text : ui->style.text_dim);
        }
    }
    {   /* under the text: where the cursor is, and the first mistake */
        char info[320];
        int line = ed_code_line_of(f, f->cursor);
        br_fill_rect(r, status, br_rgba(0, 0, 0, 60));
        snprintf(info, sizeof(info), "Ln %d, Col %d%s", line + 1, col_of(f, f->cursor) + 1, read_only ? "  (read-only while playing)" : "");
        br_draw_text(r, info, status.x + 8, status.y + 4, 1, ui->style.text_dim);
        if (f->error[0]) {
            float x = status.x + 8 + br_measure_text(info, 1).x + 16;
            br_rect where = br_rect_make(x - 4, status.y, status.w - (x - status.x), STATUS_H);
            br_draw_text(r, f->error, x, status.y + 4, 1, br_rgb(240, 110, 100));
            if (br_rect_contains(where, ui->mouse) && ui->pressed && f->error_line > 0) {   /* click: go there */
                f->cursor = f->anchor = pos_at(f, f->error_line - 1, 0);
                c->scroll_to = f->error_line;
                c->focused = true;
            }
        } else {
            const char *ok = f->dirty ? "No mistakes found - Ctrl+S saves" : "No mistakes found";
            br_draw_text(r, ok, status.x + status.w - br_measure_text(ok, 1).x - 8, status.y + 4, 1, ui->style.text_dim);
        }
    }
    if (c->focused) br_draw_rect(r, text, br_rgba(ui->style.accent.r, ui->style.accent.g, ui->style.accent.b, 120));
    br_ui_panel_end(ui);
}
