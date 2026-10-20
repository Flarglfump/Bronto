/*
 * BRONTO_WEB - makes the web page (one index.html) for a game.
 *
 *   bronto_web <project folder> [-o page.html]
 *       a game made with the editor: the web player with the project packed in
 *       (what the editor's Build > Web does)
 *   bronto_web --wasm game.wasm [--title T] [--icon image] [-o page.html] [-- arguments]
 *       a game written in C, compiled to WebAssembly (make web builds the examples)
 *
 *   --runtime dir   where bronto_player.wasm, bronto.js and shell.html are
 *                   (default: beside this program, as make web leaves them)
 *   --title T       the page's title (default: the project's name / the file's)
 *   --icon image    a BMP or TGA for the tab and the start card (default: the project's icon)
 *   -o page.html    where to write it (default: <project>/builds/web/index.html, or index.html)
 *
 * The page runs from a web server or straight from the disk.
 */
#include "webpage.h"

#include <stdio.h>
#include <string.h>

#define P 1024

static void usage(void) {
    fprintf(stderr,
            "usage: bronto_web <project folder> [-o page.html] [--runtime dir]\n"
            "       bronto_web --wasm game.wasm [--title T] [--icon image] [-o page.html] [--runtime dir] [-- args]\n");
}

static bool make_dirs(const char *path) {
    char part[P];
    size_t i, n = strlen(path);
    if (n >= sizeof(part)) return false;
    for (i = 1; i <= n; ++i) {
        if (path[i] == '/' || path[i] == '\\' || path[i] == 0) {
            memcpy(part, path, i);
            part[i] = 0;
            br_fs_mkdir(part);
        }
    }
    return br_fs_is_dir(path);
}

int main(int argc, char **argv) {
    const char *project = NULL, *wasm_arg = NULL, *title = NULL, *icon_path = NULL, *out_arg = NULL, *runtime = NULL;
    const char **args = NULL;
    int i, arg_count = 0;
    char exe[P], dir[P], player[P], js[P], shell[P], out[P], err[300], name[128];
    br_project proj;
    br_image icon;
    bool have_icon = false, ok;
    webpage page;
    void *wasm = NULL, *pack = NULL;
    size_t wasm_size = 0, pack_size = 0;
    br_pack_stats st;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) { args = (const char **)(argv + i + 1); arg_count = argc - i - 1; break; }
        else if (strcmp(argv[i], "--wasm") == 0 && i + 1 < argc) wasm_arg = argv[++i];
        else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) title = argv[++i];
        else if (strcmp(argv[i], "--icon") == 0 && i + 1 < argc) icon_path = argv[++i];
        else if (strcmp(argv[i], "--runtime") == 0 && i + 1 < argc) runtime = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_arg = argv[++i];
        else if (argv[i][0] == '-') { usage(); return 2; }
        else if (!project) project = argv[i];
        else { usage(); return 2; }
    }
    if (!project == !wasm_arg) { usage(); return 2; }

    /* the runtime */
    if (runtime) snprintf(dir, sizeof(dir), "%s", runtime);
    else if (br_fs_executable_path(exe, sizeof(exe))) br_path_parent(dir, sizeof(dir), exe);
    else snprintf(dir, sizeof(dir), ".");
    if (!webpage_find_runtime(dir, player, js, shell, sizeof(player))) {
        /* a C game needs only bronto.js and shell.html */
        br_path_join(js, sizeof(js), dir, "web/bronto.js");
        br_path_join(shell, sizeof(shell), dir, "web/shell.html");
        if (!wasm_arg || !br_fs_exists(js)) {
            br_path_join(js, sizeof(js), dir, "bronto.js");
            br_path_join(shell, sizeof(shell), dir, "shell.html");
        }
        if (!wasm_arg || !br_fs_exists(js) || !br_fs_exists(shell)) {
            fprintf(stderr, "bronto_web: no web runtime in %s (build it with \"make web\")\n", dir);
            return 1;
        }
    }

    br_project_defaults(&proj, "Bronto game");
    if (project) {
        if (!br_fs_is_dir(project)) { fprintf(stderr, "bronto_web: %s isn't a folder\n", project); return 1; }
        br_project_load(project, &proj);
        if (!(pack = webpage_pack_project(project, proj.build_dir, &pack_size, &st, err, sizeof(err)))) {
            fprintf(stderr, "bronto_web: can't pack %s: %s\n", project, err);
            return 1;
        }
        wasm = br_file_read_all(player, &wasm_size);
        if (!title) title = proj.name;
        if (!icon_path && proj.icon[0]) {
            static char icon_full[P];
            br_path_join(icon_full, sizeof(icon_full), project, proj.icon);
            icon_path = icon_full;
        }
    } else {
        wasm = br_file_read_all(wasm_arg, &wasm_size);
        if (!title) { br_path_stem(name, sizeof(name), wasm_arg); title = name; }
    }
    if (!wasm) { fprintf(stderr, "bronto_web: can't read %s\n", project ? player : wasm_arg); br_free(pack); return 1; }
    if (icon_path) {
        have_icon = br_image_load(&icon, icon_path);
        if (!have_icon) fprintf(stderr, "bronto_web: can't load the icon %s (BMP or TGA); going without\n", icon_path);
    }

    if (out_arg) snprintf(out, sizeof(out), "%s", out_arg);
    else if (project) {
        char builds[P];
        br_path_join(builds, sizeof(builds), project, proj.build_dir);
        br_path_join(out, sizeof(out), builds, "web/index.html");
    } else snprintf(out, sizeof(out), "index.html");
    br_path_parent(dir, sizeof(dir), out);
    if (dir[0] && !make_dirs(dir)) { fprintf(stderr, "bronto_web: can't make the folder %s\n", dir); return 1; }

    memset(&page, 0, sizeof(page));
    page.title = title;
    page.icon = have_icon ? &icon : NULL;
    page.wasm = wasm;
    page.wasm_size = wasm_size;
    page.pack = pack;
    page.pack_size = pack_size;
    page.args = args;
    page.arg_count = arg_count;
    ok = webpage_write(&page, js, shell, out, err, sizeof(err));
    if (ok) {
        br_dir_entry e;
        printf("bronto_web: wrote %s (%.0f KB)", out, br_fs_stat(out, &e) ? (double)e.size / 1024.0 : 0.0);
        if (pack) printf(" with %u project files packed in", st.files);
        printf("\n");
    } else {
        fprintf(stderr, "bronto_web: %s\n", err);
    }
    if (have_icon) br_image_destroy(&icon);
    br_free(wasm);
    br_free(pack);
    return ok ? 0 : 1;
}
