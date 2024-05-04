#include "logger.h"
#include "asserts.h"

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
    // b8 is_error = level < 2; 

    // NOTE: 32K char limit on messages - probably won't happen...
    char outmsg[32000];
    memset(outmsg, 0, sizeof(outmsg));

    __builtin_va_list arg_ptr;
    va_start(arg_ptr, message);
    vsnprintf(outmsg, 32000, message, arg_ptr); 
    va_end(arg_ptr);

    char outmsg2[32000];
    sprintf(outmsg2, "%s%s\n", level_strings[level], outmsg);

    // TODO: platform-specific output
    printf("%s", outmsg2);
}