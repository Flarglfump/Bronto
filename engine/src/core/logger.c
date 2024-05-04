#include "logger.h"

// TODO: temporary
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

b8 initialize_logging() {
    // TODO: create log file
    return true;
}

void shutdown_logging() {
    // TODO: cleanup logging/write queued entries

}

B_API void log_output(log_level level, const char* message, ...) {
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