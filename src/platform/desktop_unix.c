/*
 * Bronto Engine - desktop helpers for Linux and BSD (included by the X11
 * backend). X11 itself has no folder dialog, so the desktop's own dialog
 * program is run if one is installed: zenity (GNOME and most others) or
 * kdialog (KDE). Nothing is linked; when neither exists the caller is told
 * there is no dialog and can show its own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* `s` inside single quotes for /bin/sh: ' becomes '\'' */
static void du_quote(char *out, size_t size, const char *s) {
    size_t n = 0;
    if (size < 3) { if (size) out[0] = 0; return; }
    out[n++] = '\'';
    for (; *s && n + 5 < size; ++s) {
        if (*s == '\'') { memcpy(out + n, "'\\''", 4); n += 4; }
        else out[n++] = *s;
    }
    out[n++] = '\'';
    out[n] = 0;
}

static bool du_have(const char *program) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", program);
    return system(cmd) == 0;
}

static int du_pick_folder(const char *title, const char *start, char *out, size_t size) {
    char cmd[2048], qt[512], qs[1100], line[1024];
    FILE *f;
    size_t n;
    int status;
    du_quote(qt, sizeof(qt), title);
    if (start && start[0]) {
        char dir[1050];
        snprintf(dir, sizeof(dir), "%s/", start);
        du_quote(qs, sizeof(qs), dir);
    } else {
        snprintf(qs, sizeof(qs), "''");
    }
    if (du_have("zenity"))
        snprintf(cmd, sizeof(cmd), "zenity --file-selection --directory --title=%s %s%s 2>/dev/null",
                 qt, start && start[0] ? "--filename=" : "", start && start[0] ? qs : "");
    else if (du_have("kdialog"))
        snprintf(cmd, sizeof(cmd), "kdialog --title %s --getexistingdirectory %s 2>/dev/null", qt, start && start[0] ? qs : "~");
    else
        return -1;
    if (!(f = popen(cmd, "r"))) return -1;
    line[0] = 0;
    if (!fgets(line, sizeof(line), f)) line[0] = 0;
    status = pclose(f);
    n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (status != 0 || n == 0) return 0;                       /* cancelled */
    snprintf(out, size, "%s", line);
    return 1;
}

static bool du_launch(const char *command_line) {
    char cmd[2100];
    snprintf(cmd, sizeof(cmd), "(%s) >/dev/null 2>&1 &", command_line);
    return system(cmd) == 0;
}

static bool du_open_path(const char *path) {
    char cmd[1200], q[1100];
    du_quote(q, sizeof(q), path);
    snprintf(cmd, sizeof(cmd), "xdg-open %s >/dev/null 2>&1 &", q);
    return du_have("xdg-open") && system(cmd) == 0;
}
