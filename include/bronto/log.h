/*
 * Bronto Engine - logging.
 */
#ifndef BRONTO_LOG_H
#define BRONTO_LOG_H

#include "types.h"

typedef enum br_log_level {
    BR_LOG_TRACE = 0,
    BR_LOG_DEBUG,
    BR_LOG_INFO,
    BR_LOG_WARN,
    BR_LOG_ERROR,
    BR_LOG_FATAL,
    BR_LOG_NONE
} br_log_level;

typedef void (*br_log_fn)(br_log_level level, const char *file, int line,
                          const char *message, void *user);

void br_log_set_level(br_log_level min_level);
br_log_level br_log_get_level(void);
/* Install a sink. NULL restores the default (stderr). */
void br_log_set_callback(br_log_fn fn, void *user);
void br_log_write(br_log_level level, const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
const char *br_log_level_name(br_log_level level);

#define BR_TRACE(...) br_log_write(BR_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define BR_DEBUG(...) br_log_write(BR_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define BR_INFO(...)  br_log_write(BR_LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define BR_WARN(...)  br_log_write(BR_LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define BR_ERROR(...) br_log_write(BR_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* BRONTO_LOG_H */
