/*
 * Bronto web pages: one index.html that runs a game in a browser.
 *
 * The page is tools/web/shell.html with everything put inside it: the
 * runtime (bronto.js), the program (a .wasm: the Bronto player, or a game
 * written in C), the game's pack for the player, the title and the icon.
 * Being one file, it runs from any web server and also straight from the
 * disk (double-click it).
 *
 * Used by the editor's Build > Web and by the bronto_web tool.
 */
#ifndef BRONTO_WEBPAGE_H
#define BRONTO_WEBPAGE_H

#include "bronto/bronto.h"

typedef struct webpage {
    const char *title;              /* the page's title */
    const br_image *icon;           /* for the tab and the start card; may be NULL */
    const char *storage_key;        /* where the browser keeps the game's saved files; NULL: from the title */
    const void *wasm;               /* the program */
    size_t wasm_size;
    const void *pack;               /* the game's pack (game.brpak for the player); may be NULL */
    size_t pack_size;
    const char *const *args;        /* the program's command line (after argv[0]) */
    int arg_count;
} webpage;

/* The web runtime: bronto_player.wasm, bronto.js and shell.html, as `make web`
 * puts them in build/web. Looks in `dir` (or its web/ or players/web/
 * folders). Fills the three paths; false if they aren't all there. */
bool webpage_find_runtime(const char *dir, char *wasm, char *runtime_js, char *shell, size_t size);

/* Writes the page to `out_path`, using the runtime files given. */
bool webpage_write(const webpage *page, const char *runtime_js_path, const char *shell_path, const char *out_path,
                   char *err, size_t err_size);

/* A project folder as a pack in memory, leaving out what a game doesn't
 * need: its builds folder and the project's `exclude` list. */
void *webpage_pack_project(const char *folder, const br_project *project, size_t *size, br_pack_stats *stats,
                           char *err, size_t err_size);

#endif /* BRONTO_WEBPAGE_H */
