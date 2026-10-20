/*
 * Bronto web runtime - the in-memory file system behind fopen and the
 * engine's fs.h on the web (see bw.h).
 *
 * The page puts the game's pack here before the program starts, plus the
 * files the game saved on earlier visits; the program reads and writes
 * files as usual, and each file it writes is handed to the page when it's
 * closed (or flushed) so the page can keep it in the browser's storage.
 */
#include "bw.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BW_PATH_MAX 1024

static bw_node **g_nodes;
static size_t g_count, g_cap;

static int64_t now_seconds(void) { return (int64_t)time(NULL); }

bool bw_fs_normalize(const char *path, char *out, size_t size) {
    size_t n = 0;
    const char *p = path;
    if (!path || size < 2) return false;
    out[n++] = '/';
    while (*p) {
        const char *part;
        size_t len;
        while (*p == '/') ++p;
        if (!*p) break;
        part = p;
        while (*p && *p != '/') ++p;
        len = (size_t)(p - part);
        if (len == 1 && part[0] == '.') continue;
        if (len == 2 && part[0] == '.' && part[1] == '.') {          /* up one, never past "/" */
            while (n > 1 && out[n - 1] != '/') --n;
            if (n > 1) --n;
            continue;
        }
        if (n > 1) { if (n + 1 >= size) return false; out[n++] = '/'; }
        if (n + len >= size) return false;
        memcpy(out + n, part, len);
        n += len;
    }
    out[n] = 0;
    return true;
}

static bw_node *find_norm(const char *norm) {
    size_t i;
    for (i = 0; i < g_count; ++i) if (strcmp(g_nodes[i]->path, norm) == 0) return g_nodes[i];
    return NULL;
}

bw_node *bw_fs_find(const char *path) {
    char norm[BW_PATH_MAX];
    if (!bw_fs_normalize(path, norm, sizeof(norm))) return NULL;
    if (strcmp(norm, "/") == 0) {                     /* the root always exists */
        static bw_node root = { (char *)"/", true, false, false, NULL, 0, 0, 0, 0, false };
        return &root;
    }
    return find_norm(norm);
}

/* The folder `norm` is in exists? */
static bool parent_exists(const char *norm) {
    char parent[BW_PATH_MAX];
    const char *slash = strrchr(norm, '/');
    size_t len = (size_t)(slash - norm);
    bw_node *n;
    if (len == 0) return true;                        /* in "/" */
    memcpy(parent, norm, len);
    parent[len] = 0;
    n = find_norm(parent);
    return n && n->is_dir;
}

static bw_node *add_node(const char *norm, bool is_dir) {
    bw_node *n;
    if (g_count == g_cap) {
        size_t cap = g_cap ? g_cap * 2 : 64;
        bw_node **grown = (bw_node **)realloc(g_nodes, cap * sizeof(*grown));
        if (!grown) return NULL;
        g_nodes = grown;
        g_cap = cap;
    }
    if (!(n = (bw_node *)calloc(1, sizeof(*n)))) return NULL;
    if (!(n->path = strdup(norm))) { free(n); return NULL; }
    n->is_dir = is_dir;
    n->persist = true;
    n->mtime = now_seconds();
    g_nodes[g_count++] = n;
    return n;
}

static void destroy_node(bw_node *n) {
    free(n->data);
    free(n->path);
    free(n);
}

/* Takes n out of the tree; it's freed now, or when its last FILE closes. */
static void unlink_node(bw_node *n) {
    size_t i;
    for (i = 0; i < g_count; ++i) {
        if (g_nodes[i] == n) { g_nodes[i] = g_nodes[--g_count]; break; }
    }
    if (n->open_count > 0) n->removed = true;
    else destroy_node(n);
}

bw_node *bw_fs_create_file(const char *path, int *err) {
    char norm[BW_PATH_MAX];
    bw_node *n;
    *err = 0;
    if (!bw_fs_normalize(path, norm, sizeof(norm)) || strcmp(norm, "/") == 0) { *err = EINVAL; return NULL; }
    if ((n = find_norm(norm)) != NULL) {
        if (n->is_dir) { *err = EISDIR; return NULL; }
        n->size = 0;
        bw_fs_touch(n);
        return n;
    }
    if (!parent_exists(norm)) { *err = ENOENT; return NULL; }
    if (!(n = add_node(norm, false))) { *err = ENOMEM; return NULL; }
    n->dirty = true;
    return n;
}

bool bw_fs_reserve(bw_node *n, size_t size) {
    unsigned char *grown;
    size_t cap;
    if (size <= n->cap) return true;
    cap = n->cap ? n->cap : 256;
    while (cap < size) cap = cap > ((size_t)-1) / 2 ? size : cap * 2;
    if (!(grown = (unsigned char *)realloc(n->data, cap))) return false;
    n->data = grown;
    n->cap = cap;
    return true;
}

void bw_fs_touch(bw_node *n) { n->mtime = now_seconds(); n->dirty = true; }

void bw_fs_save(bw_node *n) {
    if (!n->dirty || n->removed || n->is_dir) return;
    n->dirty = false;
    if (n->persist) bw_js_file_saved(n->path, n->data, n->size);
}

void bw_fs_release(bw_node *n) {
    if (--n->open_count == 0 && n->removed) destroy_node(n);
}

int bw_fs_mkdir(const char *path) {
    char norm[BW_PATH_MAX];
    if (!bw_fs_normalize(path, norm, sizeof(norm))) return EINVAL;
    if (strcmp(norm, "/") == 0 || find_norm(norm)) return EEXIST;
    if (!parent_exists(norm)) return ENOENT;
    return add_node(norm, true) ? 0 : ENOMEM;
}

/* Is `path` inside the folder `dir` (both normalised)? */
static bool is_inside(const char *path, const char *dir) {
    size_t n = strlen(dir);
    return strncmp(path, dir, n) == 0 && path[n] == '/';
}

int bw_fs_remove(const char *path) {
    char norm[BW_PATH_MAX];
    bw_node *n;
    size_t i;
    if (!bw_fs_normalize(path, norm, sizeof(norm))) return EINVAL;
    if (strcmp(norm, "/") == 0) return EPERM;
    if (!(n = find_norm(norm))) return ENOENT;
    if (n->is_dir) {
        for (i = 0; i < g_count; ++i) if (is_inside(g_nodes[i]->path, norm)) return ENOTEMPTY;
    } else if (n->persist) {
        bw_js_file_removed(n->path);
    }
    unlink_node(n);
    return 0;
}

/* Renames one node: tells the page the file moved. */
static int move_node(bw_node *n, const char *to) {
    char *path = strdup(to);
    if (!path) return ENOMEM;
    if (!n->is_dir && n->persist) bw_js_file_removed(n->path);
    free(n->path);
    n->path = path;
    if (!n->is_dir) { n->dirty = true; bw_fs_save(n); }
    return 0;
}

int bw_fs_rename(const char *from, const char *to) {
    char a[BW_PATH_MAX], b[BW_PATH_MAX], moved[BW_PATH_MAX * 2];
    bw_node *src, *dst;
    size_t i, alen;
    if (!bw_fs_normalize(from, a, sizeof(a)) || !bw_fs_normalize(to, b, sizeof(b))) return EINVAL;
    if (strcmp(a, "/") == 0 || strcmp(b, "/") == 0) return EPERM;
    if (!(src = find_norm(a))) return ENOENT;
    if (strcmp(a, b) == 0) return 0;
    if (src->is_dir && is_inside(b, a)) return EINVAL;  /* into itself */
    if (!parent_exists(b)) return ENOENT;
    if ((dst = find_norm(b)) != NULL) {                /* replace it, as POSIX rename does */
        if (dst->is_dir != src->is_dir) return dst->is_dir ? EISDIR : ENOTDIR;
        if (dst->is_dir) { for (i = 0; i < g_count; ++i) if (is_inside(g_nodes[i]->path, b)) return ENOTEMPTY; }
        else if (dst->persist) bw_js_file_removed(dst->path);
        unlink_node(dst);
    }
    alen = strlen(a);
    if (src->is_dir) {                                 /* everything inside moves too */
        for (i = 0; i < g_count; ++i) {
            bw_node *n = g_nodes[i];
            if (!is_inside(n->path, a)) continue;
            snprintf(moved, sizeof(moved), "%s%s", b, n->path + alen);
            if (move_node(n, moved) != 0) return ENOMEM;
        }
    }
    return move_node(src, b);
}

bool bw_fs_list(const char *dir, void (*fn)(const bw_node *n, const char *name, void *user), void *user) {
    char norm[BW_PATH_MAX];
    bw_node *d;
    size_t i, len;
    if (!bw_fs_normalize(dir, norm, sizeof(norm))) return false;
    if (!(d = bw_fs_find(norm)) || !d->is_dir) return false;
    len = strcmp(norm, "/") == 0 ? 0 : strlen(norm);
    for (i = 0; i < g_count; ++i) {
        const char *p = g_nodes[i]->path;
        if (strncmp(p, norm, len) != 0 || p[len] != '/' || !p[len + 1]) continue;
        if (strchr(p + len + 1, '/')) continue;        /* deeper down */
        fn(g_nodes[i], p + len + 1, user);
    }
    return true;
}

/* ---- for the page ---------------------------------------------------------
 * bronto_web_mkdir: makes a folder and the folders it's in. */
BW_EXPORT(bronto_web_mkdir) int bronto_web_mkdir(const char *path);
int bronto_web_mkdir(const char *path) {
    char norm[BW_PATH_MAX], part[BW_PATH_MAX];
    size_t i;
    if (!bw_fs_normalize(path, norm, sizeof(norm))) return 0;
    for (i = 1; ; ++i) {
        if (norm[i] != '/' && norm[i] != 0) continue;
        memcpy(part, norm, i);
        part[i] = 0;
        bw_fs_mkdir(part);
        if (!norm[i]) break;
    }
    return bw_fs_find(norm) != NULL;
}

/*
 * bronto_web_file: makes (or replaces) a file of `size` bytes, creating its
 * folders, and returns where to copy its contents (NULL: out of memory).
 * persist = false for files the page will bring again next time (the pack). */
BW_EXPORT(bronto_web_file) void *bronto_web_file(const char *path, size_t size, int persist);
void *bronto_web_file(const char *path, size_t size, int persist) {
    char norm[BW_PATH_MAX], part[BW_PATH_MAX];
    bw_node *n;
    size_t i;
    int err;
    if (!bw_fs_normalize(path, norm, sizeof(norm))) return NULL;
    for (i = 1; norm[i]; ++i) {                        /* its folders */
        if (norm[i] != '/') continue;
        memcpy(part, norm, i);
        part[i] = 0;
        bw_fs_mkdir(part);
    }
    if (!(n = bw_fs_create_file(norm, &err)) || !bw_fs_reserve(n, size ? size : 1)) return NULL;
    n->size = size;
    n->persist = persist != 0;
    n->dirty = false;                                  /* the page has it already */
    return n->data;
}
