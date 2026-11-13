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
 *   Web       builds/web/index.html              one page: the web player (WebAssembly), its
 *                                                runtime and the pack, all inside it
 *                                                (tools/web/webpage.c)
 *
 * The player for this computer sits beside the editor. Players for the
 * other systems are built on them and copied into players/ beside the
 * editor, named bronto_player-linux, bronto_player-macos and
 * bronto_player-windows.exe; their Build items work once they are there.
 * The web player is built on any of them by "make web", into web/ beside
 * the editor (bronto_player.wasm, bronto.js and shell.html); a copy of
 * that folder in players/web works too.
 *
 * Before packing, every script in the project is checked (strictly, if the
 * project asks for strict typing); mistakes go to the console, where a
 * double-click opens them.
 *
 * A project with C code (game.h) has no ready-made player: native.c compiles
 * one with the code in, for this computer or the web (a game with C code for
 * another system is built on that system).
 *
 * Build Settings (saved in project.bronto) choose what Build makes and how:
 * the platform, the renderer, the version, the Mac app's identifier, whether a
 * Windows game has a console window, and the C compiler and its options.
 */
#include "editor.h"
#include "../web/webpage.h"
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

bool ed_target_from_name(const char *name, ed_target *t) {
    int i;
    if (strcmp(name, "host") == 0 || strcmp(name, "native") == 0) { *t = ed_host_target(); return true; }
    for (i = 0; i < ED_TARGET_COUNT; ++i) {
        if (strcmp(name, k_target_dirs[i]) == 0) { *t = (ed_target)i; return true; }
    }
    if (strcmp(name, "mac") == 0 || strcmp(name, "osx") == 0) { *t = ED_TARGET_MACOS; return true; }
    return false;
}

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
    if (!editor_dir(dir, sizeof(dir))) return false;
    if (t == ED_TARGET_WEB) {                               /* web/ (make web), or players/web/ */
        char js[BP], shell[BP];
        return webpage_find_runtime(dir, out, js, shell, size < sizeof(js) ? size : sizeof(js));
    }
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
    br_save_set_game(ed->project.name);                     /* Play saves where the built game will */
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

typedef struct pack_ctx { const br_project *project; } pack_ctx;

/* What a game doesn't need: built games, what the project leaves out, build output. */
static bool pack_filter(const char *rel, bool is_dir, void *user) {
    const pack_ctx *pc = (const pack_ctx *)user;
    if (br_project_excludes(pc->project, rel)) return false;
    if (is_dir && strcmp(rel, "build") == 0) return false;
    if (!is_dir && (br_path_has_ext(rel, ".brpak") || br_path_has_ext(rel, ".o"))) return false;
    return true;
}

static bool write_plist(const char *path, const br_project *p, const char *file_name) {
    char text[2048], id[160];
    const char *name = p->name;
    size_t i, o = 0;
    if (p->bundle_id[0]) snprintf(id, sizeof(id), "%s", p->bundle_id);
    else {
        o = (size_t)snprintf(id, sizeof(id), "com.bronto.");
        for (i = 0; file_name[i] && o + 1 < sizeof(id); ++i)
            if (isalnum((unsigned char)file_name[i])) id[o++] = (char)tolower((unsigned char)file_name[i]);
        if (o == strlen("com.bronto.")) o += (size_t)snprintf(id + o, sizeof(id) - o, "game");
        id[o] = 0;
    }
    snprintf(text, sizeof(text),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
             "<plist version=\"1.0\">\n<dict>\n"
             "  <key>CFBundleExecutable</key><string>%s</string>\n"
             "  <key>CFBundleIdentifier</key><string>%s</string>\n"
             "  <key>CFBundleName</key><string>%s</string>\n"
             "  <key>CFBundleDisplayName</key><string>%s</string>\n"
             "  <key>CFBundlePackageType</key><string>APPL</string>\n"
             "  <key>CFBundleShortVersionString</key><string>%s</string>\n"
             "  <key>CFBundleVersion</key><string>%s</string>\n"
             "  <key>NSHighResolutionCapable</key><true/>\n"
             "</dict>\n</plist>\n",
             file_name, id, name, name, p->version[0] ? p->version : "1.0", p->version[0] ? p->version : "1.0");
    return br_file_write_all(path, text, strlen(text));
}

/* Web: one page, index.html, with the web player, its runtime and the pack inside. */
static bool build_web(editor *ed, const char *root, const char *player, const char *dir, int scripts, char *out, size_t size) {
    char js[BP], shell[BP], page_path[BP], err[300], key[160];
    br_pack_stats st;
    br_image icon;
    bool have_icon = false, ok;
    webpage page;
    size_t wasm_size = 0, pack_size = 0;
    void *wasm, *pack;
    br_path_parent(js, sizeof(js), player);                 /* the runtime is beside the player */
    br_path_join(shell, sizeof(shell), js, "shell.html");
    br_path_join(page_path, sizeof(page_path), js, "bronto.js");
    snprintf(js, sizeof(js), "%s", page_path);
    if (!make_dirs(dir)) { ed_status(ed, "Build: can't make the folder %s", dir); return false; }
    br_path_join(page_path, sizeof(page_path), dir, "index.html");
    if (!(pack = webpage_pack_project(root, &ed->project, &pack_size, &st, err, sizeof(err)))) { ed_status(ed, "Build: %s", err); return false; }
    if (!(wasm = br_file_read_all(player, &wasm_size))) { br_free(pack); ed_status(ed, "Build: can't read %s", player); return false; }
    if (ed->project.icon[0]) {                              /* the project's icon, or Bronto's */
        char icon_path[BP];
        br_path_join(icon_path, sizeof(icon_path), root, ed->project.icon);
        have_icon = br_image_load(&icon, icon_path);
    }
    if (!have_icon) { ed_make_icon(&icon, 128); have_icon = icon.pixels != NULL; }
    snprintf(key, sizeof(key), "%s", ed->project.name);    /* where the browser keeps its saved files */
    memset(&page, 0, sizeof(page));
    page.title = ed->project.name;
    page.icon = have_icon ? &icon : NULL;
    page.storage_key = key;
    page.wasm = wasm;
    page.wasm_size = wasm_size;
    page.pack = pack;
    page.pack_size = pack_size;
    ok = webpage_write(&page, js, shell, page_path, err, sizeof(err));
    if (have_icon) br_image_destroy(&icon);
    br_free(wasm);
    br_free(pack);
    if (!ok) { ed_status(ed, "Build: %s", err); return false; }
    snprintf(out, size, "%s", page_path);
    printf("Build: packed %u files (%.1f KB, from %.1f KB, %d script%s checked) into %s\n", st.files, (double)st.bytes / 1024.0, (double)st.unpacked / 1024.0, scripts,
           scripts == 1 ? "" : "s", out);
    ed_status(ed, "Built %s for the web: %s (open it in a browser, or put it on a web site)", ed->project.name, out);
    return true;
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
        ed_panel_set_open(ed, ED_PANEL_BOTTOM, true);
        return false;
    }
    if (ed_native_has_code(ed)) {                           /* C code: a player of its own, compiled now */
        ed->native.diag_count = 0;
        if (!ed_native_player(ed, t, player, sizeof(player))) {
            ed_status(ed, "Build failed: the C code didn't compile into the game - see the console");
            ed->bottom_tab = ED_TAB_CONSOLE;
            ed_panel_set_open(ed, ED_PANEL_BOTTOM, true);
            return false;
        }
    } else if (!ed_find_player(ed, t, player, sizeof(player))) {
        if (t == ED_TARGET_WEB) ed_status(ed, "Build: no web player beside the editor - build it with \"make web\" (needs LLVM's clang)");
        else if (t == ed_host_target()) ed_status(ed, "Build: bronto_player isn't beside the editor - build it with \"make tools\"");
        else ed_status(ed, "Build: no %s player - build bronto_player on %s and copy it to players/ beside the editor", ed_target_name(t), ed_target_name(t));
        if (!ed->build_cli) fprintf(stderr, "[ERROR] build: %s\n", ed->status);   /* (--build says it itself) */
        return false;
    }
    file_name_of(ed->project.name, name, sizeof(name));
    if (ed->build_out[0]) {                                 /* --build --out: that folder */
        if (!make_dirs(ed->build_out) || !br_fs_absolute(ed->build_out, dir, sizeof(dir))) {
            fprintf(stderr, "[ERROR] build: can't make the folder %s\n", ed->build_out);
            return false;
        }
    } else {                                                /* the project's builds/<target> */
        char sub[BP];
        br_path_join(dir, sizeof(dir), root, ed->project.build_dir);
        br_path_join(sub, sizeof(sub), dir, k_target_dirs[t]);
        snprintf(dir, sizeof(dir), "%s", sub);
    }
    pc.project = &ed->project;
    if (t == ED_TARGET_WEB) return build_web(ed, root, player, dir, scripts, out, size);
    if (t == ED_TARGET_MACOS) {                             /* Name.app/Contents/{MacOS/Name, Resources/game.brpak, Info.plist} */
        char app[BP], macos[BP + 64], res[BP + 64], plist[BP + 64], file[160];
        snprintf(file, sizeof(file), "%s.app", name);
        br_path_join(app, sizeof(app), dir, file);
        snprintf(macos, sizeof(macos), "%s/Contents/MacOS", app);
        snprintf(res, sizeof(res), "%s/Contents/Resources", app);
        snprintf(plist, sizeof(plist), "%s/Contents/Info.plist", app);
        br_path_join(exe, sizeof(exe), macos, name);
        br_path_join(pak, sizeof(pak), res, "game.brpak");
        if (!make_dirs(macos) || !make_dirs(res) || !write_plist(plist, &ed->project, name)) {
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
        if (t == ED_TARGET_WINDOWS) {                       /* a window only, or a console window as well */
            bool console = ed->project.windows_console || ed->project.renderer != BR_RENDERER_WINDOW;
            if (!ed_pe_set_subsystem(exe, !console)) printf("Build: couldn't mark %s as a %s program\n", file, console ? "console" : "windowed");
        }
        if (!br_pack_create(root, exe, true, pack_filter, &pc, &st, err, sizeof(err))) { ed_status(ed, "Build: %s", err); return false; }
        br_fs_make_executable(exe);
        snprintf(out, size, "%s", exe);
    }
    printf("Build: packed %u files (%.1f KB, from %.1f KB, %d script%s checked) into %s\n", st.files, (double)st.bytes / 1024.0, (double)st.unpacked / 1024.0, scripts,
           scripts == 1 ? "" : "s", out);
    ed_status(ed, "Built %s for %s: %s", ed->project.name, ed_target_name(t), out);
    return true;
}

/* Runs a built terminal game in a terminal window of its own: Windows' console
 * (start), macOS's Terminal, or the first terminal found on Linux. */
static void terminal_command(ed_target t, const char *out, char *cmd, size_t size) {
    if (t == ED_TARGET_WINDOWS) { snprintf(cmd, size, "start \"\" \"%s\"", out); return; }
    if (t == ED_TARGET_MACOS) {                             /* the program inside Name.app */
        char stem[160];
        br_path_stem(stem, sizeof(stem), out);
        snprintf(cmd, size, "open -a Terminal \"%s/Contents/MacOS/%s\"", out, stem);
        return;
    }
    snprintf(cmd, size,
             "g=\"%s\"; for t in x-terminal-emulator gnome-terminal konsole xfce4-terminal kitty alacritty wezterm foot xterm; do "
             "command -v $t >/dev/null 2>&1 && break; t=; done; "
             "case \"$t\" in gnome-terminal) exec $t -- \"$g\";; kitty|foot) exec $t \"$g\";; "
             "wezterm) exec $t start -- \"$g\";; '') exit 1;; *) exec $t -e \"$g\";; esac",
             out);
}

void ed_cmd_build(editor *ed, ed_target t, bool run) {
    char out[BP], cmd[BP + 400];
    if (!ed_build(ed, t, out, sizeof(out)) || !run) return;
    if (t == ED_TARGET_WEB) {                               /* the page, in the browser */
        if (br_open_path(ed->engine, out)) ed_status(ed, "Opened %s in the browser", br_path_name(out));
        else ed_status(ed, "Built, but couldn't open %s", out);
        return;
    }
    if (t != ed_host_target()) { ed_status(ed, "Built for %s - it runs there, not here", ed_target_name(t)); return; }
    if (ed->project.renderer != BR_RENDERER_WINDOW) {      /* a terminal game: in a new terminal */
        terminal_command(t, out, cmd, sizeof(cmd));
    } else if (t == ED_TARGET_MACOS) snprintf(cmd, sizeof(cmd), "open \"%s\"", out);
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

/* The game's folder for saved data (save.h), which Play and the built game share. */
void ed_open_save_folder(editor *ed) {
    char dir[BP];
    ed_project_reload(ed);
    if (!br_save_folder(dir, sizeof(dir))) ed_status(ed, "This computer has no folder for saved data");
    else if (!br_open_path(ed->engine, dir)) ed_status(ed, "Couldn't open %s", dir);
    else ed_status(ed, "Saved data for %s: %s", br_save_game(), dir);
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
    br_rect box = br_rect_make(floorf(screen.x + (screen.w - 520) / 2), floorf(screen.y + (screen.h - 330) / 2), 520, 330);
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
    br_ui_text_field(ui, "Touch controls", p->touch_controls, sizeof(p->touch_controls));
    br_ui_label_colored(ui, "For phones: stick, a, b, x, y, lb, rb, back, start ...", ui->style.text_dim);
    br_ui_label_colored(ui, "How games are built (platform, renderer, C code): Build > Build Settings.", ui->style.text_dim);
    br_ui_spacing(ui, 8);
    br_ui_columns(ui, 2);
    if (br_ui_button(ui, "Save##project")) {
        br_project fresh = ed->project;                     /* (the build settings as they are on disk) */
        snprintf(fresh.name, sizeof(fresh.name), "%s", p->name);
        snprintf(fresh.main_scene, sizeof(fresh.main_scene), "%s", p->main_scene);
        fresh.width = p->width;
        fresh.height = p->height;
        fresh.window_scale = p->window_scale;
        fresh.strict_typing = p->strict_typing;
        snprintf(fresh.icon, sizeof(fresh.icon), "%s", p->icon);
        snprintf(fresh.touch_controls, sizeof(fresh.touch_controls), "%s", p->touch_controls);
        if (ed_project_save(ed, &fresh)) { ed->project_visible = false; ed_status(ed, "Project settings saved to %s", BR_PROJECT_FILE); }
        else ed_status(ed, "Couldn't write %s", BR_PROJECT_FILE);
    }
    if (br_ui_button(ui, "Cancel##project") || br_key_pressed(&ed->engine->input, BR_KEY_ESCAPE)) ed->project_visible = false;
    br_draw_rect(ui->r, box, ui->style.accent);
    br_ui_panel_end(ui);
}

/* ---- Build Settings ------------------------------------------------------------------------- */

static ed_target target_of(const br_project *p) {
    ed_target t;
    if (!p->build_target[0] || strcmp(p->build_target, "host") == 0 || !ed_target_from_name(p->build_target, &t)) return ed_host_target();
    return t;
}

ed_target ed_build_target(editor *ed) { return target_of(&ed->project); }

void ed_cmd_compile(editor *ed) {
    ed_project_reload(ed);
    if (!ed_native_has_code(ed)) {
        ed_status(ed, "No C code: .c files in the project's %s folder are the game's code (Build Settings)", ed->project.code_dir);
        return;
    }
    if (ed->doc.playing) { ed_status(ed, "Stop playing first: the code is loaded again when it compiles"); return; }
    ed_code_save_all(ed);
    if (!ed_native_apply(ed, true)) {
        ed->bottom_tab = ED_TAB_CONSOLE;
        ed_panel_set_open(ed, ED_PANEL_BOTTOM, true);
    }
}

void ed_build_settings_open(editor *ed) {
    ed_project_reload(ed);
    ed->build_edit = ed->project;
    ed->build_visible = true;
    ed->native.has_code = ed_native_has_code(ed);
    ed_native_describe(ed, ed->build_compilers[0], sizeof(ed->build_compilers[0]), ed->build_compilers[1], sizeof(ed->build_compilers[1]));
}

/* A label broken into lines of at most `cols` characters (between words). */
static void label_wrapped(br_ui *ui, const char *text, br_color colour, int cols) {
    const char *p = text;
    while (*p) {
        char line[160];
        int n = (int)strlen(p), cut = n;
        if (cols > (int)sizeof(line) - 1) cols = (int)sizeof(line) - 1;
        if (n > cols) {
            for (cut = cols; cut > 0 && p[cut] != ' '; --cut) {}
            if (cut == 0) cut = cols;                              /* (one long word: a path) */
        }
        snprintf(line, sizeof(line), "%.*s", cut, p);
        br_ui_label_colored(ui, line, colour);
        p += cut;
        while (*p == ' ') ++p;
    }
}

/* What Build will do for target t, in a line. */
static void target_status(editor *ed, ed_target t, char *out, size_t size, bool *ready) {
    char path[BP];
    bool code = ed->native.has_code;
    *ready = true;
    if (code && t == ED_TARGET_WEB) snprintf(out, size, "The game's C code is compiled for the web with %s.", ed->build_compilers[1]);
    else if (code && t == ed_host_target()) snprintf(out, size, "The game's C code is compiled in with %s.", ed->build_compilers[0]);
    else if (code) { snprintf(out, size, "A game with C code is built on %s itself: open the project in the editor there.", ed_target_name(t)); *ready = false; }
    else if (ed_find_player(ed, t, path, sizeof(path))) snprintf(out, size, "Ready: the player is %.400s.", path);
    else if (t == ED_TARGET_WEB) { snprintf(out, size, "Needs the web player: run \"make web\" (or build.bat web) where the editor was built."); *ready = false; }
    else if (t == ed_host_target()) { snprintf(out, size, "Needs bronto_player beside the editor: run \"make tools\"."); *ready = false; }
    else {
        snprintf(out, size, "Needs bronto_player-%s%s in players/ beside the editor (built on %s with make tools).", k_target_dirs[t],
                 t == ED_TARGET_WINDOWS ? ".exe" : "", ed_target_name(t));
        *ready = false;
    }
}

/* Saves the dialog's build settings, keeping the project settings as they are on disk. */
static bool save_build_settings(editor *ed) {
    br_project fresh, *b = &ed->build_edit;
    ed_project_reload(ed);
    fresh = ed->project;
    if (!b->build_dir[0] || strchr(b->build_dir, '/') || strchr(b->build_dir, '\\') || b->build_dir[0] == '.') {
        ed_status(ed, "The builds folder is one folder name inside the project, like builds");
        return false;
    }
    if (!b->code_dir[0] || b->code_dir[0] == '/' || b->code_dir[0] == '.' || strchr(b->code_dir, '\\') || strchr(b->code_dir, ':')) {
        ed_status(ed, "The code folder is a folder inside the project, like src");
        return false;
    }
    snprintf(fresh.build_target, sizeof(fresh.build_target), "%s", b->build_target);
    snprintf(fresh.build_dir, sizeof(fresh.build_dir), "%s", b->build_dir);
    snprintf(fresh.exclude, sizeof(fresh.exclude), "%s", b->exclude);
    fresh.renderer = b->renderer;
    snprintf(fresh.version, sizeof(fresh.version), "%s", b->version[0] ? b->version : "1.0");
    snprintf(fresh.bundle_id, sizeof(fresh.bundle_id), "%s", b->bundle_id);
    fresh.windows_console = b->windows_console;
    snprintf(fresh.code_dir, sizeof(fresh.code_dir), "%s", b->code_dir);
    snprintf(fresh.compiler, sizeof(fresh.compiler), "%s", b->compiler);
    snprintf(fresh.web_compiler, sizeof(fresh.web_compiler), "%s", b->web_compiler);
    snprintf(fresh.cflags, sizeof(fresh.cflags), "%s", b->cflags);
    snprintf(fresh.ldflags, sizeof(fresh.ldflags), "%s", b->ldflags);
    fresh.debug_build = b->debug_build;
    if (!ed_project_save(ed, &fresh)) { ed_status(ed, "Couldn't write %s", BR_PROJECT_FILE); return false; }
    ed->build_edit = ed->project;
    return true;
}

void ed_build_dialog(editor *ed, br_rect screen) {
    static const char *const renderers[BR_RENDERER_COUNT] = { "Window", "Terminal: colour blocks", "Terminal: text" };
    br_ui *ui = &ed->ui;
    br_project *p = &ed->build_edit;
    float w = 700, h = screen.h - 60 < 600 ? screen.h - 60 : 600;
    br_rect box = br_rect_make(floorf(screen.x + (screen.w - w) / 2), floorf(screen.y + (screen.h - h) / 2), w, h);
    br_rect left = br_rect_make(box.x, box.y, 190, box.h - 40);
    br_rect right = br_rect_make(box.x + 190, box.y, box.w - 190, box.h - 40);
    br_rect bottom = br_rect_make(box.x, box.y + box.h - 40, box.w, 40);
    ed_target t = target_of(p), host = ed_host_target(), i;
    char line[600];
    bool ready, close = false, build = false, run = false;
    int cols = (int)((right.w - 2 * ui->style.padding - 14) / 8);
    br_color dim = ui->style.text_dim;
    if (!ed->build_visible) return;
    br_fill_rect(ui->r, screen, br_rgba(0, 0, 0, 140));
    br_ui_set_disabled(ui, false);

    /* the platforms */
    br_ui_panel_begin(ui, "build_platforms", left);
    br_ui_heading(ui, "Platform");
    for (i = ED_TARGET_LINUX; i < ED_TARGET_COUNT; i = (ed_target)(i + 1)) {
        char label[64];
        snprintf(label, sizeof(label), "%s%s", ed_target_name(i), i == host ? " (this computer)" : "");
        if (br_ui_selectable(ui, label, i == t))
            snprintf(p->build_target, sizeof(p->build_target), "%s", i == host ? "host" : k_target_dirs[i]);
    }
    br_ui_spacing(ui, 6);
    label_wrapped(ui, "Build (Ctrl+B) makes the game for the platform chosen here.", ui->style.text_dim, (int)((left.w - 2 * ui->style.padding) / 8));
    br_ui_panel_end(ui);

    /* its settings */
    br_ui_panel_begin(ui, "build_options", right);
    snprintf(line, sizeof(line), "%s build", ed_target_name(t));
    br_ui_heading(ui, line);
    target_status(ed, t, line, sizeof(line), &ready);
    label_wrapped(ui, line, ready ? dim : br_rgb(255, 170, 90), cols);
    br_ui_text_field(ui, "Builds folder", p->build_dir, sizeof(p->build_dir));
    snprintf(line, sizeof(line), "The game goes in %s/%s/ in the project.", p->build_dir, k_target_dirs[t]);
    label_wrapped(ui, line, dim, cols);
    br_ui_text_field(ui, "Leave out", p->exclude, sizeof(p->exclude));
    label_wrapped(ui, "Folders and files the game doesn't need: tools, notes.txt", dim, cols);
    br_ui_text_field(ui, "Version", p->version, sizeof(p->version));
    if (t != ED_TARGET_WEB) {
        if (p->renderer < 0 || p->renderer >= BR_RENDERER_COUNT) p->renderer = BR_RENDERER_WINDOW;
        br_ui_combo(ui, "Renderer", &p->renderer, renderers, BR_RENDERER_COUNT);
        label_wrapped(ui, "A window, or the terminal the game is started from.", dim, cols);
    } else {
        label_wrapped(ui, "The page draws in a canvas. Touch controls for phones: Project Settings.", dim, cols);
    }
    if (t == ED_TARGET_MACOS) {
        br_ui_text_field(ui, "Bundle ID", p->bundle_id, sizeof(p->bundle_id));
        label_wrapped(ui, "The app's identifier, like com.yourname.game (empty: com.bronto.<name>).", dim, cols);
    }
    if (t == ED_TARGET_WINDOWS) {
        bool terminal = p->renderer != BR_RENDERER_WINDOW;
        br_ui_checkbox(ui, "Console window", &p->windows_console);
        label_wrapped(ui, terminal ? "A terminal game always has one: it draws in it." : "Off: just the game's window. On: a console beside it, with what the game prints.",
                      dim, cols);
    }

    br_ui_separator(ui);
    br_ui_heading(ui, "C code");
    br_ui_text_field(ui, "Code folder", p->code_dir, sizeof(p->code_dir));
    if (ed->native.has_code) {
        label_wrapped(ui, ed->native.summary[0] ? ed->native.summary : "C code found: it's compiled when it changes.",
                      ed->native.failed ? br_rgb(255, 120, 120) : dim, cols);
    } else {
        snprintf(line, sizeof(line), "No .c files in %s/ yet: + C File (under Files) makes one. See docs/c-code.md.", p->code_dir);
        label_wrapped(ui, line, dim, cols);
    }
    br_ui_text_field(ui, "Compiler", p->compiler, sizeof(p->compiler));
    snprintf(line, sizeof(line), "Empty: the one found, %s", ed->build_compilers[0]);
    label_wrapped(ui, line, dim, cols);
    br_ui_text_field(ui, "Web compiler", p->web_compiler, sizeof(p->web_compiler));
    snprintf(line, sizeof(line), "Empty: %s (for web builds)", ed->build_compilers[1]);
    label_wrapped(ui, line, dim, cols);
    br_ui_text_field(ui, "Compiler options", p->cflags, sizeof(p->cflags));
    br_ui_text_field(ui, "Linker options", p->ldflags, sizeof(p->ldflags));
    label_wrapped(ui, "Like -DLEVELS=3 -Ivendor and -lsqlite3 (with cl: /DLEVELS=3 and mylib.lib).", dim, cols);
    br_ui_checkbox(ui, "Debug build", &p->debug_build);
    label_wrapped(ui, "No optimising, and debug info: for running the game in a debugger.", dim, cols);
    if (ed->native.has_code && br_ui_button(ui, "Compile now")) {
        if (save_build_settings(ed)) ed_cmd_compile(ed);
    }
    br_ui_panel_end(ui);

    /* the buttons */
    br_ui_panel_begin(ui, "build_buttons", bottom);
    br_ui_columns(ui, 4);
    if (br_ui_button(ui, "Save##build")) { if (save_build_settings(ed)) ed_status(ed, "Build settings saved to %s", BR_PROJECT_FILE); }
    if (br_ui_button(ui, "Build##settings")) build = true;
    if (br_ui_button(ui, t == ED_TARGET_WEB ? "Build and Open##settings" : "Build and Run##settings")) build = run = true;
    if (br_ui_button(ui, "Close##build") || br_key_pressed(&ed->engine->input, BR_KEY_ESCAPE)) close = true;
    br_ui_panel_end(ui);
    br_draw_rect(ui->r, box, ui->style.accent);
    if (build && save_build_settings(ed)) {
        ed->build_visible = false;
        ed_cmd_build(ed, t, run);
    }
    if (close) ed->build_visible = false;
}
