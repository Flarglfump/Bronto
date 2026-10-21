/*
 * Bronto Engine - saved data (see save.h): one folder per game, in the
 * system's place for it (fs.h br_fs_data_dir), files replaced in one step.
 */
#include "bronto/save.h"
#include "bronto/log.h"
#include "bronto/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV_TEMP_SUFFIX ".saving"

static char g_sv_game[BR_SAVE_NAME_MAX + 1] = "Bronto Game";
static char g_sv_folder[BR_PATH_MAX];            /* br_save_set_folder(): "" = the system's place */

void br_save_set_game(const char *name) {
    size_t i, n = 0;
    if (!name) name = "";
    for (i = 0; name[i] && n < BR_SAVE_NAME_MAX; ++i) {
        unsigned char c = (unsigned char)name[i];
        g_sv_game[n++] = (char)(c < 32 || strchr("/\\:*?\"<>|", c) ? '_' : c);
    }
    while (n && (g_sv_game[n - 1] == ' ' || g_sv_game[n - 1] == '.')) n--;   /* Windows drops these */
    g_sv_game[n] = 0;
    i = 0;
    while (g_sv_game[i] == ' ' || g_sv_game[i] == '.') i++;
    if (i) memmove(g_sv_game, g_sv_game + i, strlen(g_sv_game + i) + 1);
    if (!g_sv_game[0]) snprintf(g_sv_game, sizeof(g_sv_game), "Bronto Game");
}

const char *br_save_game(void) { return g_sv_game; }

void br_save_set_folder(const char *folder) { snprintf(g_sv_folder, sizeof(g_sv_folder), "%s", folder ? folder : ""); }

bool br_save_folder(char *out, size_t size) {
    const char *env = getenv("BRONTO_SAVE_DIR");
    const char *own = g_sv_folder[0] ? g_sv_folder : (env && env[0] ? env : NULL);
    if (own) {                                    /* (made absolute: a relative path could find a pack's files) */
        if (!br_fs_is_dir(own) && !br_fs_mkdir(own)) return false;
        return br_fs_absolute(own, out, size);
    }
    return br_fs_data_dir(g_sv_game, out, size);
}

bool br_save_name_ok(const char *name) {
    size_t i, n = name ? strlen(name) : 0;
    if (n == 0 || n > BR_SAVE_NAME_MAX || name[0] == '.' || name[0] == ' ' || name[n - 1] == ' ' || name[n - 1] == '.') return false;
    for (i = 0; i < n; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" _-.()", c))) return false;
    }
    return strstr(name, SV_TEMP_SUFFIX) == NULL;    /* (the swap's own temporary name) */
}

bool br_save_path(const char *name, char *out, size_t size) {
    char folder[BR_PATH_MAX];
    if (!br_save_name_ok(name) || !br_save_folder(folder, sizeof(folder))) return false;
    br_path_join(out, size, folder, name);
    return true;
}

bool br_save_write(const char *name, const void *data, size_t size) {
    char path[BR_PATH_MAX], temp[BR_PATH_MAX + 16];
    if (!br_save_path(name, path, sizeof(path))) {
        BR_WARN("save: can't save \"%s\" (%s)", name ? name : "", br_save_name_ok(name) ? "no folder for saved data" : "not an allowed name");
        return false;
    }
    snprintf(temp, sizeof(temp), "%s" SV_TEMP_SUFFIX, path);
    if (!br_file_write_all(temp, data, size)) {
        BR_WARN("save: can't write %s", temp);
        br_fs_remove(temp);
        return false;
    }
    if (!br_fs_replace(temp, path)) {
        BR_WARN("save: can't put %s in place", path);
        br_fs_remove(temp);
        return false;
    }
    return true;
}

void *br_save_read(const char *name, size_t *size) {
    char path[BR_PATH_MAX];
    if (size) *size = 0;
    if (!br_save_path(name, path, sizeof(path))) return NULL;
    return br_file_read_all(path, size);
}

bool br_save_exists(const char *name) {
    char path[BR_PATH_MAX];
    br_dir_entry e;
    return br_save_path(name, path, sizeof(path)) && br_fs_stat(path, &e) && !e.is_dir;
}

bool br_save_delete(const char *name) {
    char path[BR_PATH_MAX];
    if (!br_save_path(name, path, sizeof(path))) return false;
    return !br_fs_exists(path) || br_fs_remove(path);
}

bool br_save_list(br_dir_list *out) {
    char folder[BR_PATH_MAX];
    int i, n = 0;
    memset(out, 0, sizeof(*out));
    if (!br_save_folder(folder, sizeof(folder)) || !br_fs_list(folder, false, out)) return false;
    for (i = 0; i < out->count; ++i)
        if (!out->entries[i].is_dir && br_save_name_ok(out->entries[i].name)) out->entries[n++] = out->entries[i];
    out->count = n;
    return true;
}
