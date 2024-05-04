#include "logger.h"
#include "asserts.h"
#include "platform/platform.h"

// TODO: temporary
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void report_assertion_failure(const char * expression, const char * message, const char * file, i32 line) { // NOTE: declared in asserts.h
    log_output(LOG_LEVEL_FATAL, "Assertion Failure: %s, message: '%s', in file: %s, line: %d\n", expression, message, file, line);
}

b8 initialize_logging() {
    // TODO: create log file
    return true;
}

void shutdown_logging() {
    // TODO: cleanup logging/write queued entries

}

void log_output(log_level level, const char * message, ...) {
    const char * level_strings[6] = {"[FATAL]: ", "[ERROR]: ", "[WARN]: ", "[INFO]: ", "[DEBUG]: ", "[TRACE]: "};
    // FATAL (0) or ERROR (1) level
    b8 is_error = level < LOG_LEVEL_WARN; 

    // NOTE: 32K char limit on messages - probably won't happen...
    const i32 msg_len = 32000;
    char outmsg[msg_len];
    memset(outmsg, 0, sizeof(outmsg));

    __builtin_va_list arg_ptr;
    va_start(arg_ptr, message);
    vsnprintf(outmsg, msg_len, message, arg_ptr); 
    va_end(arg_ptr);

    char outmsg2[msg_len];
    sprintf(outmsg2, "%s%s\n", level_strings[level], outmsg);

    // Platform-specific output
    if (is_error) {
        platform_console_write_error(outmsg2, level);
    } else {
        platform_console_write(outmsg2, level);
    }
}