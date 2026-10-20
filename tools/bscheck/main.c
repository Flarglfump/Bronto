/*
 * bscheck - checks BrontoScript files without running them.
 *
 *   bscheck player.bs coin.bs ...      prints  file:line: message  for each mistake
 *
 * Exit status 0 when every file parses, 1 otherwise. Editors run it on save
 * (the VS Code extension in tools/vscode does) to underline mistakes; it
 * finds what the engine finds before running a script: syntax errors,
 * unknown names and functions, and wrong argument counts.
 */
#include "bronto/bronto.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int i, bad = 0;
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        printf("usage: bscheck file.bs ...\n");
        return argc < 2 ? 2 : 0;
    }
    if (strcmp(argv[1], "--builtins") == 0) {               /* the function list, for tools */
        for (i = 0; i < br_script_builtin_count(); ++i) printf("%s\n", br_script_builtin_name(i));
        return 0;
    }
    for (i = 1; i < argc; ++i) {
        size_t size;
        char err[300];
        char *text = (char *)br_file_read_all(argv[i], &size);
        if (!text) { printf("%s:1: can't read the file\n", argv[i]); bad = 1; continue; }
        if (!br_script_check(text, argv[i], err, sizeof(err))) {
            /* the message starts with the file's name; print the path given instead */
            const char *rest = strchr(err, ':');
            printf("%s%s\n", argv[i], rest ? rest : err);
            bad = 1;
        }
        br_free(text);
    }
    return bad;
}
