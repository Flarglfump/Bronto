/*
 * Bronto Engine - asset cache (see assets.h).
 */
#include "bronto/assets.h"
#include "bronto/memory.h"
#include "bronto/log.h"
#include "bronto/fs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ---- project settings ---------------------------------------------------------- */

void br_project_defaults(br_project *p, const char *name) {
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name && name[0] ? name : "Game");
    snprintf(p->main_scene, sizeof(p->main_scene), "main.scene");
    p->width = 320;
    p->height = 180;
    p->window_scale = 3;
    snprintf(p->build_dir, sizeof(p->build_dir), "builds");
}

static void pj_trim(char *s) {
    size_t n = strlen(s), i = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[i] && isspace((unsigned char)s[i])) ++i;
    if (i) memmove(s, s + i, n - i + 1);
}

bool br_project_parse(const char *text, br_project *p) {
    const char *line = text;
    bool header = false;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[512], *eq;
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = 0;
        line = end ? end + 1 : NULL;
        pj_trim(buf);
        if (!buf[0] || buf[0] == '#') continue;
        if (!header) { header = strncmp(buf, "bronto_project", 14) == 0; if (header) continue; return false; }
        if (!(eq = strchr(buf, '='))) continue;
        *eq = 0;
        pj_trim(buf);
        pj_trim(eq + 1);
        {
            const char *key = buf, *v = eq + 1;
            if (strcmp(key, "name") == 0) snprintf(p->name, sizeof(p->name), "%s", v);
            else if (strcmp(key, "main_scene") == 0) snprintf(p->main_scene, sizeof(p->main_scene), "%s", v);
            else if (strcmp(key, "width") == 0) p->width = atoi(v) > 0 ? atoi(v) : p->width;
            else if (strcmp(key, "height") == 0) p->height = atoi(v) > 0 ? atoi(v) : p->height;
            else if (strcmp(key, "window_scale") == 0) p->window_scale = atoi(v) > 0 ? atoi(v) : p->window_scale;
            else if (strcmp(key, "strict_typing") == 0) p->strict_typing = strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
            else if (strcmp(key, "icon") == 0) snprintf(p->icon, sizeof(p->icon), "%s", v);
            else if (strcmp(key, "build_dir") == 0) snprintf(p->build_dir, sizeof(p->build_dir), "%s", v[0] ? v : "builds");
            else if (strcmp(key, "exclude") == 0) snprintf(p->exclude, sizeof(p->exclude), "%s", v);
            else if (strcmp(key, "touch_controls") == 0) snprintf(p->touch_controls, sizeof(p->touch_controls), "%s", v);
        }
    }
    return header;
}

bool br_project_load(const char *folder, br_project *p) {
    char path[ASSET_PATH_MAX], abs[ASSET_PATH_MAX];
    size_t size;
    char *text;
    bool ok;
    br_project_defaults(p, br_fs_absolute(folder && folder[0] ? folder : ".", abs, sizeof(abs)) ? br_path_name(abs) : "Game");
    br_path_join(path, sizeof(path), folder ? folder : "", BR_PROJECT_FILE);
    if (!(text = (char *)br_file_read_all(path, &size))) return false;
    ok = br_project_parse(text, p);
    br_free(text);
    return ok;
}

/* Does `rel` name `item` or something inside it? */
static bool path_under(const char *rel, const char *item, size_t n) {
    return n > 0 && strncmp(rel, item, n) == 0 && (rel[n] == 0 || rel[n] == '/');
}

bool br_project_excludes(const br_project *p, const char *rel) {
    const char *s = p->exclude;
    if (path_under(rel, p->build_dir, strlen(p->build_dir))) return true;
    while (*s) {
        const char *start, *end;
        while (*s == ',' || *s == ' ') ++s;
        start = s;
        while (*s && *s != ',') ++s;
        end = s;
        while (end > start && (end[-1] == ' ' || end[-1] == '/')) --end;
        if (path_under(rel, start, (size_t)(end - start))) return true;
    }
    return false;
}

bool br_project_save(const char *folder, const br_project *p) {
    char path[ASSET_PATH_MAX], text[2048];
    int n = snprintf(text, sizeof(text),
        "bronto_project 1\n"
        "# Asset paths in this project's scenes are relative to this folder.\n"
        "# Project settings (the editor's Build > Project Settings):\n"
        "name = %s\nmain_scene = %s\nwidth = %d\nheight = %d\nwindow_scale = %d\nstrict_typing = %s\nicon = %s\nbuild_dir = %s\n"
        "exclude = %s\n%s%s%s",
        p->name, p->main_scene, p->width, p->height, p->window_scale, p->strict_typing ? "true" : "false", p->icon, p->build_dir,
        p->exclude, p->touch_controls[0] ? "touch_controls = " : "", p->touch_controls, p->touch_controls[0] ? "\n" : "");
    br_path_join(path, sizeof(path), folder ? folder : "", BR_PROJECT_FILE);
    return n > 0 && (size_t)n < sizeof(text) && br_file_write_all(path, text, (size_t)n);
}
