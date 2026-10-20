/*
 * Bronto Engine - asset cache (see assets.h).
 */
#include "bronto/assets.h"
#include "bronto/memory.h"
#include "bronto/log.h"
#include "bronto/fs.h"

#include <stdio.h>
#include <string.h>

#define ASSET_PATH_MAX 512

typedef enum { ASSET_IMAGE, ASSET_SOUND } asset_kind;

typedef struct asset_entry {
    char *path;          /* as asked for */
    asset_kind kind;
    br_image image;
    br_sound sound;
    bool ok;
} asset_entry;

static struct {
    asset_entry **entries;          /* each entry on its own: what we hand out never moves */
    uint32_t count, cap;
    char root[ASSET_PATH_MAX];
    br_mixer *mixer;
} S;

void br_assets_set_mixer(br_mixer *m) { S.mixer = m; }

void br_assets_set_root(const char *dir) {
    size_t n;
    snprintf(S.root, sizeof(S.root), "%s", dir ? dir : "");
    n = strlen(S.root);
    if (n > 0 && S.root[n - 1] != '/' && S.root[n - 1] != '\\' && n + 1 < sizeof(S.root)) {
        S.root[n] = '/';
        S.root[n + 1] = 0;
    }
}

const char *br_assets_root(void) { return S.root; }

void br_assets_find_root(const char *scene_path, char *out, size_t size) {
    char dir[ASSET_PATH_MAX], marker[ASSET_PATH_MAX], up[ASSET_PATH_MAX];
    br_path_parent(dir, sizeof(dir), scene_path ? scene_path : "");
    snprintf(out, size, "%s", dir);
    for (;;) {
        br_path_join(marker, sizeof(marker), dir, BR_PROJECT_FILE);
        if (br_fs_exists(marker)) { snprintf(out, size, "%s", dir); return; }
        if (!dir[0]) return;
        br_path_parent(up, sizeof(up), dir);
        if (strcmp(up, dir) == 0) return;               /* "/" */
        memcpy(dir, up, sizeof(dir));
    }
}

static bool is_absolute(const char *p) {
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

static asset_entry *find_or_load(const char *path, asset_kind kind) {
    asset_entry *e;
    char full[ASSET_PATH_MAX * 2];
    uint32_t i;
    if (!path || !path[0]) return NULL;
    for (i = 0; i < S.count; ++i)
        if (S.entries[i]->kind == kind && strcmp(S.entries[i]->path, path) == 0) return S.entries[i];
    if (S.count == S.cap) {
        uint32_t cap = S.cap ? S.cap * 2 : 32;
        asset_entry **grown = (asset_entry **)br_realloc(S.entries, cap * sizeof(asset_entry *));
        if (!grown) return NULL;
        S.entries = grown;
        S.cap = cap;
    }
    if (!(e = (asset_entry *)br_alloc(sizeof(asset_entry)))) return NULL;
    memset(e, 0, sizeof(*e));
    S.entries[S.count++] = e;
    e->path = br_strdup(path);
    e->kind = kind;
    snprintf(full, sizeof(full), "%s%s", is_absolute(path) ? "" : S.root, path);
    if (kind == ASSET_IMAGE) {
        e->ok = br_image_load(&e->image, full);
        if (!e->ok) BR_WARN("asset: can't load image '%s'", full);
    } else {
        e->ok = br_sound_load_wav(&e->sound, full);
        if (!e->ok) BR_WARN("asset: can't load sound '%s'", full);
    }
    return e;
}

const br_image *br_asset_image(const char *path) {
    asset_entry *e = find_or_load(path, ASSET_IMAGE);
    return e && e->ok ? &e->image : NULL;
}

const br_sound *br_asset_sound(const char *path) {
    asset_entry *e = find_or_load(path, ASSET_SOUND);
    return e && e->ok ? &e->sound : NULL;
}

static void release_entry(asset_entry *e) {
    if (e->ok && e->kind == ASSET_IMAGE) br_image_destroy(&e->image);
    if (e->ok && e->kind == ASSET_SOUND) {
        if (S.mixer) br_mixer_stop_sound(S.mixer, &e->sound);
        br_sound_destroy(&e->sound);
    }
    br_free(e->path);
    br_free(e);
}

void br_assets_forget(const char *path) {
    uint32_t i = 0;
    while (path && i < S.count) {
        if (strcmp(S.entries[i]->path, path) == 0) {
            release_entry(S.entries[i]);
            S.entries[i] = S.entries[--S.count];
        } else {
            ++i;
        }
    }
}

void br_assets_clear(void) {
    uint32_t i;
    for (i = 0; i < S.count; ++i) release_entry(S.entries[i]);
    br_free(S.entries);
    S.entries = NULL;
    S.count = S.cap = 0;
}

uint32_t br_assets_count(void) { return S.count; }
