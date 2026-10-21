/*
 * Bronto Engine - file system backend for the web (included by fs.c).
 *
 * The web build has an in-memory file system (src/platform/web/memfs.c):
 * the page puts the game's pack in it, and keeps whatever the game writes
 * in the browser's storage for its next visit.
 */
#include "web/bw.h"

#include <errno.h>
#include <stdio.h>

typedef struct fsw_list { br_dir_list *out; int cap; bool hidden; bool failed; } fsw_list;

static void fsw_fill(br_dir_entry *e, const bw_node *n, const char *name) {
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = n->is_dir;
    e->size = n->is_dir ? 0 : (uint64_t)n->size;
    e->modified = n->mtime;
}

static void fsw_add(const bw_node *n, const char *name, void *user) {
    fsw_list *l = (fsw_list *)user;
    br_dir_entry *e;
    if (l->failed || (!l->hidden && name[0] == '.')) return;
    if (!(e = fs_push(l->out, &l->cap))) { l->failed = true; return; }
    fsw_fill(e, n, name);
}

static bool fs_read_dir(const char *dir, bool hidden, br_dir_list *out) {
    fsw_list l;
    l.out = out;
    l.cap = 0;
    l.hidden = hidden;
    l.failed = false;
    return bw_fs_list(dir, fsw_add, &l) && !l.failed;
}

bool br_fs_stat(const char *path, br_dir_entry *out) {
    const bw_node *n = path ? bw_fs_find(path[0] ? path : ".") : NULL;
    if (!n) return false;
    memset(out, 0, sizeof(*out));
    fsw_fill(out, n, br_path_name(path));
    return true;
}

bool br_fs_mkdir(const char *path) {
    int err = bw_fs_mkdir(path);
    return err == 0 || (err == EEXIST && br_fs_is_dir(path));
}

bool br_fs_rename(const char *from, const char *to) {
    if (br_fs_exists(to)) return false;                 /* don't overwrite */
    return bw_fs_rename(from, to) == 0;
}

bool br_fs_replace(const char *from, const char *to) { return bw_fs_rename(from, to) == 0; }   /* replaces, as POSIX does */

bool br_fs_remove(const char *path) { return bw_fs_remove(path) == 0; }

bool br_fs_executable_path(char *out, size_t size) {   /* a page has no program file */
    BR_UNUSED(out); BR_UNUSED(size);
    return false;
}

bool br_fs_make_executable(const char *path) { return br_fs_exists(path); }

bool br_fs_absolute(const char *path, char *out, size_t size) {
    return bw_fs_find(path && path[0] ? path : ".") != NULL && bw_fs_normalize(path && path[0] ? path : ".", out, size);
}
