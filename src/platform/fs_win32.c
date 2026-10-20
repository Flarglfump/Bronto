/*
 * Bronto Engine - file system backend for Windows (UTF-8 paths, converted
 * to the wide-character API). Included by fs.c.
 */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>

static bool fs_to_wide(const char *utf8, wchar_t *out, int count) {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, count) > 0;
}

static void fs_to_utf8(const wchar_t *wide, char *out, int size) {
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, size, NULL, NULL) <= 0) out[0] = 0;
}

/* FILETIME counts 100 ns steps since 1601. */
static int64_t fs_unix_time(FILETIME ft) {
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (int64_t)(t.QuadPart / 10000000ULL) - 11644473600LL;
}

static void fs_fill_entry(br_dir_entry *e, const wchar_t *name, DWORD attrs, DWORD size_hi, DWORD size_lo, FILETIME modified) {
    fs_to_utf8(name, e->name, (int)sizeof(e->name));
    e->is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e->size = e->is_dir ? 0 : ((uint64_t)size_hi << 32) | size_lo;
    e->modified = fs_unix_time(modified);
}

static bool fs_read_dir(const char *dir, bool hidden, br_dir_list *out) {
    char pattern[BR_PATH_MAX * 2];
    wchar_t wpattern[BR_PATH_MAX * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int cap = 0;
    br_path_join(pattern, sizeof(pattern), dir, "*");
    if (!fs_to_wide(pattern, wpattern, (int)(sizeof(wpattern) / sizeof(wpattern[0])))) return false;
    h = FindFirstFileW(wpattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;   /* empty */
    do {
        br_dir_entry *e;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!hidden && (fd.cFileName[0] == L'.' || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))) continue;
        if (!(e = fs_push(out, &cap))) { FindClose(h); return false; }
        fs_fill_entry(e, fd.cFileName, fd.dwFileAttributes, fd.nFileSizeHigh, fd.nFileSizeLow, fd.ftLastWriteTime);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

bool br_fs_stat(const char *path, br_dir_entry *out) {
    wchar_t wpath[BR_PATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA a;
    wchar_t wname[256];
    if (!path || !fs_to_wide(path[0] ? path : ".", wpath, BR_PATH_MAX)) return false;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &a)) return false;
    memset(out, 0, sizeof(*out));
    if (!fs_to_wide(br_path_name(path), wname, 256)) wname[0] = 0;
    fs_fill_entry(out, wname, a.dwFileAttributes, a.nFileSizeHigh, a.nFileSizeLow, a.ftLastWriteTime);
    return true;
}

bool br_fs_mkdir(const char *path) {
    wchar_t w[BR_PATH_MAX];
    if (!fs_to_wide(path, w, BR_PATH_MAX)) return false;
    if (CreateDirectoryW(w, NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && br_fs_is_dir(path);
}

bool br_fs_rename(const char *from, const char *to) {
    wchar_t a[BR_PATH_MAX], b[BR_PATH_MAX];
    if (!fs_to_wide(from, a, BR_PATH_MAX) || !fs_to_wide(to, b, BR_PATH_MAX)) return false;
    return MoveFileW(a, b) != 0;                                 /* fails if `to` exists */
}

bool br_fs_absolute(const char *path, char *out, size_t size) {
    wchar_t w[BR_PATH_MAX], full[BR_PATH_MAX];
    char *p;
    DWORD n;
    if (!fs_to_wide(path && path[0] ? path : ".", w, BR_PATH_MAX)) return false;
    n = GetFullPathNameW(w, BR_PATH_MAX, full, NULL);
    if (n == 0 || n >= BR_PATH_MAX || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) return false;
    fs_to_utf8(full, out, (int)size);
    for (p = out; *p; ++p) if (*p == '\\') *p = '/';
    return true;
}

bool br_fs_executable_path(char *out, size_t size) {
    wchar_t w[BR_PATH_MAX];
    char *p;
    DWORD n = GetModuleFileNameW(NULL, w, BR_PATH_MAX);
    if (n == 0 || n >= BR_PATH_MAX) return false;
    fs_to_utf8(w, out, (int)size);
    for (p = out; *p; ++p) if (*p == '\\') *p = '/';
    return true;
}

bool br_fs_make_executable(const char *path) { return br_fs_exists(path); }   /* .exe files just run */

bool br_fs_remove(const char *path) {
    wchar_t w[BR_PATH_MAX];
    if (!fs_to_wide(path, w, BR_PATH_MAX)) return false;
    return br_fs_is_dir(path) ? RemoveDirectoryW(w) != 0 : DeleteFileW(w) != 0;
}
