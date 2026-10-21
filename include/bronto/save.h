/*
 * Bronto Engine - saved data: files a game keeps between runs (save games,
 * high scores, options), in the place each system keeps them for the user:
 *
 *   Windows   %APPDATA%\<game>                          (C:\Users\you\AppData\Roaming\<game>)
 *   macOS     ~/Library/Application Support/<game>
 *   Linux     $XDG_DATA_HOME/<game>, or ~/.local/share/<game>
 *   Web       the browser's storage for the game's page (localStorage)
 *
 * <game> is the game's name: the engine sets it from br_engine_config's
 * save_name, or else its title, and bronto_player from the project's name.
 * A game only names its files:
 *
 *   br_save_write("slot1.sav", &state, sizeof(state));
 *   void *data = br_save_read("slot1.sav", &size);       (br_free it)
 *
 * Writing replaces a file in one step: the new data is written beside the
 * old file and then swapped in, so a crash or power cut part-way through
 * leaves the old save, never half of the new one.
 *
 * Names are file names, not paths: 1 to 64 letters, digits, spaces and
 * "_-.()" (not starting with '.' or a space). Everything else is refused,
 * so a name can't reach outside the folder.
 *
 * Scripts use the same files through save_data() and load_data()
 * (docs/scripting.md).
 */
#ifndef BRONTO_SAVE_H
#define BRONTO_SAVE_H

#include "types.h"
#include "fs.h"

#define BR_SAVE_NAME_MAX 64

/* The game's name, which names its folder. Characters a folder name can't
 * have on some system (/\:*?"<>| and control characters) become '_'.
 * NULL or "" = "Bronto Game". */
void br_save_set_game(const char *name);
const char *br_save_game(void);

/* Keeps saved data in `folder` instead of the system's place (tests, a copy
 * of a game that runs from a USB stick). NULL or "" = the system's place.
 * The environment variable BRONTO_SAVE_DIR does the same for any game. */
void br_save_set_folder(const char *folder);

/* The folder, created if needed. False if the system has no place for it
 * (no home folder) or it can't be made. */
bool br_save_folder(char *out, size_t size);

/* Whether `name` is allowed as a saved file's name. */
bool br_save_name_ok(const char *name);
/* A saved file's full path (false: a bad name, or no folder). */
bool br_save_path(const char *name, char *out, size_t size);

bool  br_save_write(const char *name, const void *data, size_t size);
void *br_save_read(const char *name, size_t *size);   /* NUL-terminated; br_free; NULL if there is none */
bool  br_save_exists(const char *name);
bool  br_save_delete(const char *name);               /* true if it's gone (or never was) */
/* The saved files, by name (folders and unfinished writes left out). Free
 * with br_dir_list_free(). */
bool  br_save_list(br_dir_list *out);

#endif /* BRONTO_SAVE_H */
