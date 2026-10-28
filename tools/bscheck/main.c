/*
 * bscheck - checks BrontoScript files without running them.
 *
 *   bscheck player.bs coin.bs ...      prints  file:line: message  for each mistake
 *   bscheck --strict file.bs           as if each file began with @strict (types required)
 *   bscheck --project DIR file.bs      use DIR's project.bronto (its strict_typing); without
 *                                      it, each file's own project folder is found
 *
 * Exit status 0 when every file parses, 1 otherwise. Editors run it on save
 * (the VS Code extension in tools/vscode does) to underline mistakes; it
 * finds what the engine finds before running a script: syntax errors,
 * unknown names and functions, wrong argument counts, and type mistakes.
 */
#include "bronto/bronto.h"
#include <stdio.h>
#include <string.h>

/* Strict typing for a file: --strict, or its project's setting. */
static bool strict_for(const char *file, const char *project, int forced) {
    br_project p;
    char root[1024];
    if (forced) return true;
    if (project) return br_project_load(project, &p) && p.strict_typing;
    br_assets_find_root(file, root, sizeof(root));
    return br_project_load(root, &p) && p.strict_typing;
}

int main(int argc, char **argv) {
    int i, bad = 0, forced = 0, files = 0;
    const char *project = NULL;
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        printf("usage: bscheck [--strict] [--project DIR] file.bs ...\n");
        return argc < 2 ? 2 : 0;
    }
    if (strcmp(argv[1], "--builtins") == 0) {               /* the function list, for tools */
        for (i = 0; i < br_script_builtin_count(); ++i) printf("%s\n", br_script_builtin_name(i));
        return 0;
    }
    for (i = 1; i < argc; ++i) {
        size_t size;
        char err[300];
        char *text;
        if (strcmp(argv[i], "--strict") == 0) { forced = 1; continue; }
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) { project = argv[++i]; continue; }
        ++files;
        if (!(text = (char *)br_file_read_all(argv[i], &size))) { printf("%s:1: can't read the file\n", argv[i]); bad = 1; continue; }
        br_script_set_strict(strict_for(argv[i], project, forced));
        if (strstr(text, "component") && br_script_scan_components(text, argv[i], err, sizeof(err)) < 0) {   /* its components */
            printf("%s\n", err);
            bad = 1;
        } else if (!br_script_check(text, argv[i], err, sizeof(err))) {
            /* the message starts with the file's name; print the path given instead */
            const char *rest = strchr(err, ':');
            printf("%s%s\n", argv[i], rest ? rest : err);
            bad = 1;
        }
        br_free(text);
    }
    if (!files) { printf("bscheck: no files given\n"); return 2; }
    return bad;
}
