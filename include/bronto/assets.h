/*
 * Bronto Engine - asset cache.
 *
 * Images and sounds are loaded the first time they are asked for and kept until
 * br_assets_clear() (the engine calls it at shutdown). Relative paths are
 * resolved against the asset root, so a scene's sprites can be found next
 * to its scene file wherever the game is started from.
 */
#ifndef BRONTO_ASSETS_H
#define BRONTO_ASSETS_H

#include "render.h"
#include "audio.h"

/* The file that marks a game's project folder. Asset paths in scenes are
 * relative to the project folder, so scenes in subfolders share one set of
 * paths; the editor creates it when a scene is saved below the project. */
#define BR_PROJECT_FILE "project.bronto"

/* The project folder of a scene file: the nearest folder at or above the
 * scene's own that holds BR_PROJECT_FILE, or else the scene's folder
 * ("" = the current directory). */
void br_assets_find_root(const char *scene_path, char *out, size_t size);

/* Directory prepended to relative asset paths ("" = current directory). */
void br_assets_set_root(const char *dir);
const char *br_assets_root(void);

/* What these return stays at the same address until that file is
 * forgotten or the cache is cleared.
 *
 * The image at `path` (BMP or TGA), loaded once. NULL if it can't be
 * loaded; the failure is logged once, not every frame. */
const br_image *br_asset_image(const char *path);

/* The WAV sound at `path`, loaded once; NULL if it can't be loaded. Stop
 * voices that use cached sounds before br_assets_clear() (the engine does). */
const br_sound *br_asset_sound(const char *path);

/* The mixer that plays cached sounds (the engine sets its own): forgetting
 * or clearing a sound silences its voices there first, so nothing plays
 * freed memory. */
void br_assets_set_mixer(br_mixer *m);

/* Drops one cached file (image or sound) so the next request reloads it,
 * e.g. after it changed on disk. */
void br_assets_forget(const char *path);

void br_assets_clear(void);
uint32_t br_assets_count(void);

#endif /* BRONTO_ASSETS_H */
