/*
 * Bronto Engine - path strings (see fs.h). No file system access.
 */
#include "bronto/fs.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool is_sep(char c) { return c == '/' || c == '\\'; }

void br_path_join(char *out, size_t size, const char *a, const char *b) {
    size_t n;
    if (!a || !a[0] || (b && is_sep(b[0]))) { snprintf(out, size, "%s", b ? b : ""); return; }
    n = strlen(a);
    if (!b || !b[0]) snprintf(out, size, "%s", a);
    else if (is_sep(a[n - 1])) snprintf(out, size, "%s%s", a, b);
    else snprintf(out, size, "%s/%s", a, b);
}

const char *br_path_name(const char *path) {
    const char *p = path, *name = path;
    for (; *p; ++p) if (is_sep(*p)) name = p + 1;
    return name;
}

void br_path_parent(char *out, size_t size, const char *path) {
    size_t n = (size_t)(br_path_name(path) - path);   /* up to and including the last '/' */
    if (n > 1) n--;                                    /* "a/b/c" -> "a/b"; "/c" keeps "/" */
    if (size == 0) return;
    if (n >= size) n = size - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

const char *br_path_ext(const char *path) {
    const char *name = br_path_name(path), *dot = strrchr(name, '.');
    return dot && dot != name ? dot : name + strlen(name);
}

bool br_path_has_ext(const char *path, const char *ext) {
    const char *e = br_path_ext(path);
    while (*e && *ext) {
        if (tolower((unsigned char)*e) != tolower((unsigned char)*ext)) return false;
        ++e;
        ++ext;
    }
    return *e == 0 && *ext == 0;
}

void br_path_stem(char *out, size_t size, const char *path) {
    const char *name = br_path_name(path), *ext = br_path_ext(path);
    size_t n = (size_t)(ext - name);
    if (size == 0) return;
    if (n >= size) n = size - 1;
    memcpy(out, name, n);
    out[n] = 0;
}
