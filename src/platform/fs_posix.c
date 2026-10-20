/*
 * Bronto Engine - file system backend for POSIX systems (Linux, macOS,
 * BSD, and Emscripten's virtual file system on the web).
 * Included by fs.c.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

static void fs_fill_entry(br_dir_entry *e, const char *name, const struct stat *st) {
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = S_ISDIR(st->st_mode);
    e->size = e->is_dir ? 0 : (uint64_t)st->st_size;
    e->modified = (int64_t)st->st_mtime;
}

static bool fs_read_dir(const char *dir, bool hidden, br_dir_list *out) {
    DIR *d = opendir(dir);
    struct dirent *de;
    int cap = 0;
    char full[BR_PATH_MAX * 2];
    if (!d) return false;
    while ((de = readdir(d)) != NULL) {
        struct stat st;
        br_dir_entry *e;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!hidden && de->d_name[0] == '.') continue;
        br_path_join(full, sizeof(full), dir, de->d_name);
        if (stat(full, &st) != 0) continue;                   /* vanished, or a broken link */
        if (!(e = fs_push(out, &cap))) { closedir(d); return false; }
        fs_fill_entry(e, de->d_name, &st);
    }
    closedir(d);
    return true;
}

bool br_fs_stat(const char *path, br_dir_entry *out) {
    struct stat st;
    if (!path || stat(path[0] ? path : ".", &st) != 0) return false;
    memset(out, 0, sizeof(*out));
    fs_fill_entry(out, br_path_name(path), &st);
    return true;
}

bool br_fs_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) return true;
    return errno == EEXIST && br_fs_is_dir(path);
}

bool br_fs_rename(const char *from, const char *to) {
    if (br_fs_exists(to)) return false;                       /* rename() would overwrite it */
    return rename(from, to) == 0;
}

bool br_fs_remove(const char *path) { return remove(path) == 0; }

bool br_fs_executable_path(char *out, size_t size) {
#if defined(__APPLE__)
    char buf[BR_PATH_MAX];
    uint32_t n = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return false;
    return br_fs_absolute(buf, out, size);
#elif defined(__EMSCRIPTEN__)
    BR_UNUSED(out); BR_UNUSED(size);
    return false;
#else
    static const char *const links[] = { "/proc/self/exe", "/proc/curproc/file", "/proc/curproc/exe" };   /* Linux, BSDs */
    char buf[BR_PATH_MAX];
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(links); ++i) {
        ssize_t n = readlink(links[i], buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; snprintf(out, size, "%s", buf); return true; }
    }
    return false;
#endif
}

bool br_fs_make_executable(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return chmod(path, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) == 0;
}

bool br_fs_absolute(const char *path, char *out, size_t size) {
    char *full = realpath(path && path[0] ? path : ".", NULL);
    if (!full) return false;
    snprintf(out, size, "%s", full);
    free(full);
    return true;
}
