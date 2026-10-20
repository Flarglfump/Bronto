/*
 * Bronto Engine - file system: folders and file information.
 *
 * The C standard library can open files but can't list a folder or make
 * one, so these calls are implemented per OS in the platform layer
 * (src/platform/fs_posix.c for Linux, macOS and the web; fs_win32.c for
 * Windows). Paths are UTF-8 and use '/' everywhere; Windows also accepts '\'.
 *
 * The br_path_* helpers only work on strings and never touch the disk.
 */
#ifndef BRONTO_FS_H
#define BRONTO_FS_H

#include "types.h"

#define BR_PATH_MAX 512

typedef struct br_dir_entry {
    char name[256];             /* just the name, no folder */
    bool is_dir;
    uint64_t size;              /* bytes (0 for folders) */
    int64_t modified;           /* seconds since 1970 */
} br_dir_entry;

typedef struct br_dir_list {
    br_dir_entry *entries;
    int count;
} br_dir_list;

/* Lists a folder without "." and "..": folders first, then files, each in
 * name order (ignoring case). Names starting with '.' are left out unless
 * `hidden`. Returns false if the folder can't be read. Free with
 * br_dir_list_free(). */
bool br_fs_list(const char *dir, bool hidden, br_dir_list *out);
void br_dir_list_free(br_dir_list *list);

bool br_fs_stat(const char *path, br_dir_entry *out);
bool br_fs_exists(const char *path);
bool br_fs_is_dir(const char *path);
bool br_fs_mkdir(const char *path);                    /* one level; true if it's already a folder */
bool br_fs_rename(const char *from, const char *to);   /* refuses to overwrite `to` */
bool br_fs_remove(const char *path);                   /* a file, or an empty folder */
/* The absolute form of an existing path, with '/' separators ("" = the
 * current directory). False if it doesn't exist. */
bool br_fs_absolute(const char *path, char *out, size_t size);

/* ---- path strings ----------------------------------------------------------- */
void br_path_join(char *out, size_t size, const char *a, const char *b);   /* "a/b"; "" + b = b */
void br_path_parent(char *out, size_t size, const char *path);            /* "a/b/c" -> "a/b", "c" -> "" */
const char *br_path_name(const char *path);                                 /* "a/b/c.bmp" -> "c.bmp" */
const char *br_path_ext(const char *path);                                  /* ".bmp", or "" */
bool br_path_has_ext(const char *path, const char *ext);                    /* ignores case */
void br_path_stem(char *out, size_t size, const char *path);              /* "a/hero.bmp" -> "hero" */

#endif /* BRONTO_FS_H */
