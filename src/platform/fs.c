/*
 * Bronto Engine - file system (see fs.h): the shared part, plus the OS
 * backend for this build (fs_posix.c or fs_win32.c), included below.
 *
 * Each backend provides:
 *   static bool fs_read_dir(const char *dir, bool hidden, br_dir_list *out);   (unsorted)
 *   bool br_fs_stat / br_fs_mkdir / br_fs_rename / br_fs_remove
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
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

bool br_fs_config_dir(const char *app, char *out, size_t size) {
    char base[BR_PATH_MAX];
    const char *env;
#if defined(_WIN32)
    if (!(env = getenv("APPDATA")) || !env[0]) return false;
    snprintf(base, sizeof(base), "%s", env);
#elif defined(__APPLE__)
    if (!(env = getenv("HOME")) || !env[0]) return false;
    snprintf(base, sizeof(base), "%s/Library/Application Support", env);
#else
    if ((env = getenv("XDG_CONFIG_HOME")) != NULL && env[0]) snprintf(base, sizeof(base), "%s", env);
    else if ((env = getenv("HOME")) != NULL && env[0]) snprintf(base, sizeof(base), "%s/.config", env);
    else return false;
#endif
    br_fs_mkdir(base);
    br_path_join(out, size, base, app);
    return br_fs_mkdir(out);
}

bool br_fs_exists(const char *path) { br_dir_entry e; return br_fs_stat(path, &e); }
bool br_fs_is_dir(const char *path) { br_dir_entry e; return br_fs_stat(path, &e) && e.is_dir; }
