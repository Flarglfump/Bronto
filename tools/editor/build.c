/*
 * BRONTO EDITOR - the Build menu: making a game people can run without the editor.
 *
 * A built game is a copy of the Bronto player (tools/player, built next to
 * the editor by "make tools") with the project packed in (pack.h):
 *
 *   Linux     builds/linux/<Name>                one file: the player, the pack added to its end
 *   Windows   builds/windows/<Name>.exe          the same
 *   macOS     builds/macos/<Name>.app            a bundle: Contents/MacOS/<Name>, and the
 *                                                pack in Contents/Resources/game.brpak
 *
 * The player for this computer sits beside the editor. Players for the
 * other systems are built on them and copied into players/ beside the
 * editor, named bronto_player-linux, bronto_player-macos and
 * bronto_player-windows.exe; their Build items work once they are there.
 *
 * Before packing, every script in the project is checked (strictly, if the
 * project asks for strict typing); mistakes go to the console, where a
 * double-click opens them.
 */
#include "editor.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BP 1024                               /* paths here can be long: project folder + builds + bundle */

static const char *const k_target_names[ED_TARGET_COUNT] = { "Linux", "macOS", "Windows", "Web" };
static const char *const k_target_dirs[ED_TARGET_COUNT] = { "linux", "macos", "windows", "web" };

ed_target ed_host_target(void) {
#if defined(_WIN32)
    return ED_TARGET_WINDOWS;
#elif defined(__APPLE__)
    return ED_TARGET_MACOS;
#else
    return ED_TARGET_LINUX;
#endif
}

const char *ed_target_name(ed_target t) { return t < ED_TARGET_COUNT ? k_target_names[t] : "?"; }

/* The folder the editor's program is in (where bronto_player is too). */
static bool editor_dir(char *out, size_t size) {
    char exe[BP];
    if (!br_fs_executable_path(exe, sizeof(exe))) return false;
    br_path_parent(out, size, exe);
    return true;
}

bool ed_find_player(editor *ed, ed_target t, char *out, size_t size) {
    char dir[BP], name[64];
    BR_UNUSED(ed);
    if (t == ED_TARGET_WEB || !editor_dir(dir, sizeof(dir))) return false;
    if (t == ed_host_target()) {
        br_path_join(out, size, dir, t == ED_TARGET_WINDOWS ? "bronto_player.exe" : "bronto_player");
        if (br_fs_exists(out)) return true;
    }
    snprintf(name, sizeof(name), "players/bronto_player-%s%s", k_target_dirs[t], t == ED_TARGET_WINDOWS ? ".exe" : "");
    br_path_join(out, size, dir, name);
    return br_fs_exists(out);
}

/* ---- the project's settings ------------------------------------------------------ */

void ed_project_reload(editor *ed) {
    char abs[BP];
    const char *root = ed->assets.root[0] ? ed->assets.root : ".";
    br_project_load(root, &ed->project);
    if (!ed->project.name[0] && br_fs_absolute(root, abs, sizeof(abs))) snprintf(ed->project.name, sizeof(ed->project.name), "%s", br_path_name(abs));
    if (br_script_strict() != ed->project.strict_typing) {
        br_script_set_strict(ed->project.strict_typing);     /* the editor's own Play follows the project */
        br_script_reload(&ed->doc.world);
    }
}

bool ed_project_save(editor *ed, const br_project *p) {
    const char *root = ed->assets.root[0] ? ed->assets.root : ".";
    if (!br_project_save(root, p)) return false;
    ed_project_reload(ed);
    return true;
}

/* ---- checking scripts ------------------------------------------------------------- */

typedef struct check_state { editor *ed; int scripts, errors; } check_state;

static void check_dir(check_state *cs, const char *rel_dir, int depth) {
    editor *ed = cs->ed;
    char full[BP];
    br_dir_list l;
    int i;
    ed_asset_path(ed, rel_dir, full, sizeof(full));
    if (depth > 12 || !br_fs_list(full[0] ? full : ".", false, &l)) return;
    for (i = 0; i < l.count; ++i) {
        const br_dir_entry *e = &l.entries[i];
        char rel[BP], path[BP], err[300];
        if (rel_dir[0]) snprintf(rel, sizeof(rel), "%s/%s", rel_dir, e->name);
        else snprintf(rel, sizeof(rel), "%s", e->name);
        if (e->is_dir) {
            if (depth == 0 && (strcmp(e->name, ed->project.build_dir) == 0 || strcmp(e->name, "build") == 0)) continue;
            check_dir(cs, rel, depth + 1);
            continue;
        }
        if (!br_path_has_ext(e->name, ".bs")) continue;
        cs->scripts++;
        ed_asset_path(ed, rel, path, sizeof(path));
        {
            size_t size;
            char *text = (char *)br_file_read_all(path, &size);
            if (!text) { fprintf(stderr, "%s: can't read it\n", rel); cs->errors++; continue; }
            if (!br_script_check(text, rel, err, sizeof(err))) {    /* "scripts/player.bs:12: ...": the console opens it */
                size_t base = strlen(e->name);
                if (strncmp(err, e->name, base) == 0) fprintf(stderr, "[ERROR] build: %s%s\n", rel, err + base);
                else fprintf(stderr, "[ERROR] build: %s\n", err);
                cs->errors++;
            }
            br_free(text);
        }
    }
    br_dir_list_free(&l);
}

int ed_check_scripts(editor *ed, int *scripts) {
    check_state cs;
    bool was = br_script_strict();
    cs.ed = ed;
    cs.scripts = cs.errors = 0;
    br_script_set_strict(ed->project.strict_typing);
    check_dir(&cs, "", 0);
    br_script_set_strict(was);
    if (scripts) *scripts = cs.scripts;
    return cs.errors;
}

/* ---- building -------------------------------------------------------------------- */

static bool make_dirs(const char *path) {               /* mkdir -p */
    char part[BP];
    size_t i, n = strlen(path);
    if (n >= sizeof(part)) return false;
    for (i = 1; i <= n; ++i) {
        if (path[i] == '/' || path[i] == '\\' || path[i] == 0) {
            memcpy(part, path, i);
            part[i] = 0;
            if (part[0]) br_fs_mkdir(part);              /* (the ones that exist just fail) */
        }
    }
    return br_fs_is_dir(path);
}

/* The project's name as a file name. */
static void file_name_of(const char *name, char *out, size_t size) {
    size_t i, o = 0;
    for (i = 0; name[i] && o + 1 < size; ++i) {
        unsigned char c = (unsigned char)name[i];
        out[o++] = (char)(isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.' ? c : '_');
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.')) --o;
    out[o] = 0;
    if (!out[0] || out[0] == '.') snprintf(out, size, "Game");
}

typedef struct pack_ctx { const char *build_dir; } pack_ctx;

/* What a game doesn't need: built games, the editor's trash, build output. */
static bool pack_filter(const char *rel, bool is_dir, void *user) {
    const pack_ctx *pc = (const pack_ctx *)user;
    if (is_dir && !strchr(rel, '/') && (strcmp(rel, pc->build_dir) == 0 || strcmp(rel, "build") == 0)) return false;
    if (!is_dir && (br_path_has_ext(rel, ".brpak") || br_path_has_ext(rel, ".o"))) return false;
    return true;
}

static bool write_plist(const char *path, const char *name, const char *file_name) {
    char text[2048], id[96];
    size_t i, o = 0;
    for (i = 0; file_name[i] && o + 1 < sizeof(id); ++i)
        if (isalnum((unsigned char)file_name[i])) id[o++] = (char)tolower((unsigned char)file_name[i]);
    id[o] = 0;
    snprintf(text, sizeof(text),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
             "<plist version=\"1.0\">\n<dict>\n"
             "  <key>CFBundleExecutable</key><string>%s</string>\n"
             "  <key>CFBundleIdentifier</key><string>com.bronto.%s</string>\n"
             "  <key>CFBundleName</key><string>%s</string>\n"
             "  <key>CFBundleDisplayName</key><string>%s</string>\n"
             "  <key>CFBundlePackageType</key><string>APPL</string>\n"
             "  <key>CFBundleShortVersionString</key><string>1.0</string>\n"
             "  <key>CFBundleVersion</key><string>1</string>\n"
             "  <key>NSHighResolutionCapable</key><true/>\n"
             "</dict>\n</plist>\n",
             file_name, id[0] ? id : "game", name, name);
    return br_file_write_all(path, text, strlen(text));
}

bool ed_build(editor *ed, ed_target t, char *out, size_t size) {
    char root[BP], player[BP], dir[BP], name[96], exe[BP], pak[BP], main_scene[BP], err[256];
    br_pack_stats st;
    pack_ctx pc;
    int errors, scripts = 0;
    out[0] = 0;
    if (!br_fs_absolute(ed->assets.root[0] ? ed->assets.root : ".", root, sizeof(root))) { ed_status(ed, "Build: can't find the project folder"); return false; }
    ed_project_reload(ed);
    if (ed->doc.playing) ed_cmd_play(ed);                   /* back to the scene as it is edited */
    if (ed_doc_dirty(&ed->doc) && ed->doc.path[0]) {        /* the open scene, saved, so the game has it */
        snprintf(ed->path_field, sizeof(ed->path_field), "%s", ed->doc.path);
        ed_cmd_save(ed);
        printf("Build: saved %s first\n", br_path_name(ed->doc.path));
    }
    printf("Build: %s for %s\n", ed->project.name, ed_target_name(t));
    br_path_join(main_scene, sizeof(main_scene), root, ed->project.main_scene);
    if (!br_fs_exists(main_scene)) {
        fprintf(stderr, "[ERROR] build: the main scene %s isn't in the project (Build > Project Settings chooses it)\n", ed->project.main_scene);
        ed_status(ed, "Build failed: no main scene %s (see Project Settings)", ed->project.main_scene);
        return false;
    }
    if ((errors = ed_check_scripts(ed, &scripts)) > 0) {
        ed_status(ed, "Build failed: %d script%s with mistakes - see the console", errors, errors == 1 ? "" : "s");
        ed->bottom_tab = ED_TAB_CONSOLE;
        return false;
    }
    if (!ed_find_player(ed, t, player, sizeof(player))) {
        if (t == ED_TARGET_WEB) ed_status(ed, "Build: web builds aren't available yet");
        else if (t == ed_host_target()) ed_status(ed, "Build: bronto_player isn't beside the editor - build it with \"make tools\"");
        else ed_status(ed, "Build: no %s player - build bronto_player on %s and copy it to players/ beside the editor", ed_target_name(t), ed_target_name(t));
        fprintf(stderr, "[ERROR] build: %s\n", ed->status);
        return false;
    }
    file_name_of(ed->project.name, name, sizeof(name));
    br_path_join(dir, sizeof(dir), root, ed->project.build_dir);
    {
        char sub[BP];
        br_path_join(sub, sizeof(sub), dir, k_target_dirs[t]);
        snprintf(dir, sizeof(dir), "%s", sub);
    }
    pc.build_dir = ed->project.build_dir;
    if (t == ED_TARGET_MACOS) {                             /* Name.app/Contents/{MacOS/Name, Resources/game.brpak, Info.plist} */
        char app[BP], macos[BP + 64], res[BP + 64], plist[BP + 64], file[160];
        snprintf(file, sizeof(file), "%s.app", name);
        br_path_join(app, sizeof(app), dir, file);
        snprintf(macos, sizeof(macos), "%s/Contents/MacOS", app);
        snprintf(res, sizeof(res), "%s/Contents/Resources", app);
        snprintf(plist, sizeof(plist), "%s/Contents/Info.plist", app);
        br_path_join(exe, sizeof(exe), macos, name);
        br_path_join(pak, sizeof(pak), res, "game.brpak");
        if (!make_dirs(macos) || !make_dirs(res) || !write_plist(plist, ed->project.name, name)) {
            ed_status(ed, "Build: can't write %s", app);
            return false;
        }
        if (!br_fs_copy_file(player, exe) || !br_fs_make_executable(exe)) { ed_status(ed, "Build: can't copy the player into %s", app); return false; }
        if (!br_pack_create(root, pak, false, pack_filter, &pc, &st, err, sizeof(err))) { ed_status(ed, "Build: %s", err); return false; }
        snprintf(out, size, "%s", app);
    } else {                                                /* one file: the player with the pack on its end */
        char file[160];
        snprintf(file, sizeof(file), "%s%s", name, t == ED_TARGET_WINDOWS ? ".exe" : "");
        if (!make_dirs(dir)) { ed_status(ed, "Build: can't make the folder %s", dir); return false; }
        br_path_join(exe, sizeof(exe), dir, file);
        if (!br_fs_copy_file(player, exe)) { ed_status(ed, "Build: can't write %s", exe); return false; }
        if (!br_pack_create(root, exe, true, pack_filter, &pc, &st, err, sizeof(err))) { ed_status(ed, "Build: %s", err); return false; }
        br_fs_make_executable(exe);
        snprintf(out, size, "%s", exe);
    }
    printf("Build: packed %u files (%.1f KB, %d script%s checked) into %s\n", st.files, (double)st.bytes / 1024.0, scripts,
           scripts == 1 ? "" : "s", out);
    ed_status(ed, "Built %s for %s: %s", ed->project.name, ed_target_name(t), out);
    return true;
}

void ed_cmd_build(editor *ed, ed_target t, bool run) {
    char out[BP], cmd[BP + 32];
    if (!ed_build(ed, t, out, sizeof(out)) || !run) return;
    if (t != ed_host_target()) { ed_status(ed, "Built for %s - it runs there, not here", ed_target_name(t)); return; }
    if (t == ED_TARGET_MACOS) snprintf(cmd, sizeof(cmd), "open \"%s\"", out);
    else snprintf(cmd, sizeof(cmd), "\"%s\"", out);
    if (br_launch(ed->engine, cmd)) ed_status(ed, "Running %s", br_path_name(out));
    else ed_status(ed, "Built, but couldn't start %s", out);
}

void ed_open_builds_folder(editor *ed) {
    char root[BP], dir[BP];
    if (!br_fs_absolute(ed->assets.root[0] ? ed->assets.root : ".", root, sizeof(root))) return;
    br_path_join(dir, sizeof(dir), root, ed->project.build_dir);
    br_fs_mkdir(dir);
    if (!br_open_path(ed->engine, dir)) ed_status(ed, "Couldn't open %s", dir);
}

/* ---- the Project Settings dialog ------------------------------------------------------------ */

void ed_project_settings_open(editor *ed) {
    ed_project_reload(ed);
    ed->project_edit = ed->project;
    ed->project_visible = true;
}

void ed_project_dialog(editor *ed, br_rect screen) {
    br_ui *ui = &ed->ui;
    br_project *p = &ed->project_edit;
    br_rect box = br_rect_make(floorf(screen.x + (screen.w - 520) / 2), floorf(screen.y + (screen.h - 300) / 2), 520, 300);
    if (!ed->project_visible) return;
    br_fill_rect(ui->r, screen, br_rgba(0, 0, 0, 140));
    br_ui_set_disabled(ui, false);
    br_ui_panel_begin(ui, "project", box);
    br_ui_heading(ui, "Project Settings");
    br_ui_text_field(ui, "Name", p->name, sizeof(p->name));
    br_ui_text_field(ui, "Main scene", p->main_scene, sizeof(p->main_scene));
    if (ed->doc.path[0]) {
        char root[BP], scene[BP];
        if (br_fs_absolute(ed->assets.root[0] ? ed->assets.root : ".", root, sizeof(root)) && br_fs_absolute(ed->doc.path, scene, sizeof(scene)) &&
            strncmp(scene, root, strlen(root)) == 0 && scene[strlen(root)] == '/' && br_ui_button(ui, "Use the open scene"))
            snprintf(p->main_scene, sizeof(p->main_scene), "%s", scene + strlen(root) + 1);
    }
    br_ui_columns(ui, 2);
    br_ui_drag_int(ui, "Width", &p->width, 1, 16, 4096);
    br_ui_drag_int(ui, "Height", &p->height, 1, 16, 4096);
    br_ui_drag_int(ui, "Window scale", &p->window_scale, 0.05f, 1, 8);
    br_ui_label_colored(ui, "The game screen in pixels; window = size x scale.", ui->style.text_dim);
    br_ui_checkbox(ui, "Strict typing", &p->strict_typing);
    br_ui_label_colored(ui, "On: every script works as if it began with @strict.", ui->style.text_dim);
    br_ui_text_field(ui, "Icon", p->icon, sizeof(p->icon));
    br_ui_text_field(ui, "Builds folder", p->build_dir, sizeof(p->build_dir));
    br_ui_spacing(ui, 8);
    br_ui_columns(ui, 2);
    if (br_ui_button(ui, "Save##project")) {
        if (!p->build_dir[0] || strchr(p->build_dir, '/') || strchr(p->build_dir, '\\') || p->build_dir[0] == '.')
            ed_status(ed, "The builds folder is one folder name inside the project, like builds");
        else if (ed_project_save(ed, p)) { ed->project_visible = false; ed_status(ed, "Project settings saved to %s", BR_PROJECT_FILE); }
        else ed_status(ed, "Couldn't write %s", BR_PROJECT_FILE);
    }
    if (br_ui_button(ui, "Cancel##project") || br_key_pressed(&ed->engine->input, BR_KEY_ESCAPE)) ed->project_visible = false;
    br_draw_rect(ui->r, box, ui->style.accent);
    br_ui_panel_end(ui);
}
