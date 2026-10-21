/*
 * Bronto Engine - file system (see fs.h): the shared part, plus the OS
 * backend for this build (fs_posix.c, fs_win32.c or fs_web.c), included below.
 *
 * Each backend provides:
 *   static bool fs_read_dir(const char *dir, bool hidden, br_dir_list *out);   (unsorted)
 *   bool br_fs_stat / br_fs_mkdir / br_fs_rename / br_fs_replace / br_fs_remove
 *   static bool fs_getenv(const char *name, char *out, size_t size);   (UTF-8; false if unset or empty; not on the web)
 */
#if !defined(_WIN32) && !defined(__wasm__) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(__wasm__) && !defined(_XOPEN_SOURCE)
#  define _XOPEN_SOURCE 700                   /* realpath */
#endif
#include "bronto/fs.h"
#include "bronto/memory.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Grows a listing by one entry (NULL when out of memory). */
static br_dir_entry *fs_push(br_dir_list *l, int *cap) {
    if (l->count == *cap) {
        int n = *cap ? *cap * 2 : 32;
        br_dir_entry *grown = (br_dir_entry *)br_realloc(l->entries, (size_t)n * sizeof(br_dir_entry));
        if (!grown) return NULL;
        l->entries = grown;
        *cap = n;
    }
    memset(&l->entries[l->count], 0, sizeof(br_dir_entry));
    return &l->entries[l->count++];
}

#if defined(_WIN32)
#  include "fs_win32.c"
#elif defined(__wasm__)
#  include "fs_web.c"
#else
#  include "fs_posix.c"
#endif

static int fs_compare_entries(const void *pa, const void *pb) {
    const br_dir_entry *a = (const br_dir_entry *)pa, *b = (const br_dir_entry *)pb;
    const char *x = a->name, *y = b->name;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;     /* folders first */
    for (; *x && *y; ++x, ++y) {
        int cx = tolower((unsigned char)*x), cy = tolower((unsigned char)*y);
        if (cx != cy) return cx - cy;
    }
    if (*x || *y) return *x ? 1 : -1;
    return strcmp(a->name, b->name);                          /* "A" before "a" */
}

bool br_fs_list(const char *dir, bool hidden, br_dir_list *out) {
    out->entries = NULL;
    out->count = 0;
    if (!fs_read_dir(dir && dir[0] ? dir : ".", hidden, out)) {
        br_dir_list_free(out);
        return false;
    }
    if (out->count > 1) qsort(out->entries, (size_t)out->count, sizeof(br_dir_entry), fs_compare_entries);
    return true;
}

void br_dir_list_free(br_dir_list *list) {
    br_free(list->entries);
    list->entries = NULL;
    list->count = 0;
}

/* Makes `path` and any folders it is in that are missing. */
static bool fs_mkdirs(const char *path) {
    char parent[BR_PATH_MAX];
    if (br_fs_is_dir(path)) return true;
    br_path_parent(parent, sizeof(parent), path);
    if (parent[0] && strcmp(parent, path) != 0 && !br_fs_is_dir(parent) && !fs_mkdirs(parent)) return false;
    return br_fs_mkdir(path);
}

/* The folder settings (config) or saved data go in, for this user. */
static bool fs_user_base(bool data, char *base, size_t size) {
    char env[BR_PATH_MAX];
#if defined(__wasm__)
    BR_UNUSED(env);
    snprintf(base, size, "%s", data ? "/saves" : "/config");   /* kept in the browser's storage */
#elif defined(_WIN32)
    BR_UNUSED(data);
    if (!fs_getenv("APPDATA", env, sizeof(env))) return false;
    snprintf(base, size, "%s", env);
#elif defined(__APPLE__)
    BR_UNUSED(data);
    if (!fs_getenv("HOME", env, sizeof(env))) return false;
    br_path_join(base, size, env, "Library/Application Support");
#else
    if (fs_getenv(data ? "XDG_DATA_HOME" : "XDG_CONFIG_HOME", env, sizeof(env)) && env[0] == '/') snprintf(base, size, "%s", env);
    else if (fs_getenv("HOME", env, sizeof(env))) br_path_join(base, size, env, data ? ".local/share" : ".config");
    else return false;
#endif
    return fs_mkdirs(base);
}

static bool fs_user_dir(bool data, const char *app, char *out, size_t size) {
    char base[BR_PATH_MAX];
    if (!app || !app[0] || !fs_user_base(data, base, sizeof(base))) return false;
    br_path_join(out, size, base, app);
    return br_fs_mkdir(out);
}

bool br_fs_config_dir(const char *app, char *out, size_t size) { return fs_user_dir(false, app, out, size); }
bool br_fs_data_dir(const char *app, char *out, size_t size) { return fs_user_dir(true, app, out, size); }

bool br_fs_copy_file(const char *from, const char *to) {
    size_t size;
    void *data = br_file_read_all(from, &size);
    bool ok;
    if (!data) return false;
    ok = br_file_write_all(to, data, size);
    br_free(data);
    return ok;
}

bool br_fs_exists(const char *path) { br_dir_entry e; return br_fs_stat(path, &e); }
bool br_fs_is_dir(const char *path) { br_dir_entry e; return br_fs_stat(path, &e) && e.is_dir; }
