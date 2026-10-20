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

/* ---- project settings (BR_PROJECT_FILE) ----------------------------------
 * The project file holds `key = value` lines after its first line
 * ("bronto_project 1"); '#' starts a comment. The editor's Project
 * Settings edit them, and the player reads them to run the game. */
typedef struct br_project {
    char name[64];                 /* the game's name: window title, built program's name */
    char main_scene[256];          /* the scene the game starts with (default main.scene) */
    int width, height;             /* the game's screen, pixels (default 320 x 180) */
    int window_scale;              /* the window starts at width x height times this (default 3) */
    bool strict_typing;            /* every script as if it started with @strict */
    char icon[256];                /* an image for the window and taskbar ("" = the default) */
    char build_dir[256];           /* where Build puts games, in the project (default builds) */
} br_project;

void br_project_defaults(br_project *p, const char *name);
/* Reads settings from the file's text over what *p already holds. */
bool br_project_parse(const char *text, br_project *p);
/* <folder>/project.bronto (folder "" = the current one, or the mounted
 * pack's). Fills in defaults first; false if there is no project file. */
bool br_project_load(const char *folder, br_project *p);
bool br_project_save(const char *folder, const br_project *p);

#endif /* BRONTO_ASSETS_H */
