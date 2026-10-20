#include "bronto/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static br_log_level g_level = BR_LOG_INFO;
static br_log_fn g_fn = NULL;
static void *g_user = NULL;

static const char *k_names[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "NONE" };

const char *br_log_level_name(br_log_level level) {
    if ((int)level < 0 || level > BR_LOG_NONE) return "?";
    return k_names[level];
}

void br_log_set_level(br_log_level min_level) { g_level = min_level; }
br_log_level br_log_get_level(void) { return g_level; }
void br_log_set_callback(br_log_fn fn, void *user) { g_fn = fn; g_user = user; }

static const char *basename_of(const char *path) {
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *s = a > b ? a : b;
    return s ? s + 1 : path;
}

void br_log_write(br_log_level level, const char *file, int line, const char *fmt, ...) {
    char msg[1024];
    va_list args;
    if (level < g_level || level >= BR_LOG_NONE) return;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (g_fn) {
        g_fn(level, file, line, msg, g_user);
    } else {
        fprintf(stderr, "[%-5s] %s:%d: %s\n", br_log_level_name(level), basename_of(file), line, msg);
    }
}
