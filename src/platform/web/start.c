/*
 * Bronto web runtime - the program's start (see bw.h).
 */
#include "bw.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdlib.h>

/* ==== the program's start ================================================= */
/* clang names `int main(int, char **)` __main_argc_argv on WebAssembly, and
 * `int main(void)` plain main; either may be the program's. */
int __main_argc_argv(int argc, char **argv);
extern int bw_main_void(void) __asm__("main") __attribute__((weak));
__attribute__((weak)) int __main_argc_argv(int argc, char **argv) {
    (void)argc; (void)argv;
    return bw_main_void ? bw_main_void() : 1;
}

static jmp_buf g_browser_jb;
static bool g_in_main;

void bw_return_to_browser(void) {
    bw_stdio_flush_all();
    if (!g_in_main) __builtin_trap();
    longjmp(g_browser_jb, 1);
}

/* Runs main(). Returns 1 if the program is still running (the browser
 * drives its frames now), 0 if main returned: the page then calls
 * bronto_exit, which ends it as returning from main does. (Calling exit()
 * in here would leave the setjmp's function with nowhere to go, which
 * WebAssembly's validator rejects.) */
static int g_main_status;
BW_EXPORT(bronto_start) int bronto_start(int argc, char **argv);
BW_EXPORT(bronto_exit) void bronto_exit(void);
__attribute__((noinline)) static void run_main(int argc, char **argv) { g_main_status = __main_argc_argv(argc, argv); }
int bronto_start(int argc, char **argv) {
    if (setjmp(g_browser_jb) != 0) { g_in_main = false; return 1; }
    g_in_main = true;
    run_main(argc, argv);          /* (a separate function: LLVM 18 mis-structures the call inline) */
    g_in_main = false;
    return 0;
}
void bronto_exit(void) { exit(g_main_status); }

