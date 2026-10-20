/*
 * Bronto web runtime - internal interface between the C library pieces,
 * the web platform backend and the page's JavaScript (tools/web/bronto.js).
 *
 * Bronto's web build needs no Emscripten: clang compiles C straight to
 * WebAssembly (--target=wasm32), and this small C library stands in for
 * the system one. What only the browser can do (the clock, the console,
 * Math.sin, keeping saved files) is imported from bronto.js; everything
 * imported is declared here, so the JavaScript side has one list to match.
 */
#ifndef BRONTO_WEB_BW_H
#define BRONTO_WEB_BW_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BW_IMPORT(name) __attribute__((import_module("bronto"), import_name(#name)))
#define BW_EXPORT(name) __attribute__((export_name(#name), used))

/* ---- imported from bronto.js ------------------------------------------- */
BW_IMPORT(write)       void   bw_js_write(int stream, const char *data, size_t size);   /* 1 stdout, 2 stderr */
BW_IMPORT(exit)        void   bw_js_exit(int status);          /* stops the program (throws in JS) */
BW_IMPORT(now)         double bw_js_now(void);                 /* ms, monotonic (performance.now) */
BW_IMPORT(date_now)    double bw_js_date_now(void);            /* ms since 1970 (Date.now) */
BW_IMPORT(tz_offset)   int    bw_js_tz_offset(double ms);      /* minutes east of UTC at that time */
BW_IMPORT(parse_double) double bw_js_parse_double(const char *s, size_t n);
BW_IMPORT(math1)       double bw_js_math1(int op, double x);   /* BW_M_* below */
BW_IMPORT(math2)       double bw_js_math2(int op, double x, double y);
BW_IMPORT(file_saved)  void   bw_js_file_saved(const char *path, const void *data, size_t size);
BW_IMPORT(file_removed) void  bw_js_file_removed(const char *path);

enum {   /* bw_js_math1 */
    BW_M_SIN, BW_M_COS, BW_M_TAN, BW_M_ASIN, BW_M_ACOS, BW_M_ATAN, BW_M_SINH, BW_M_COSH, BW_M_TANH,
    BW_M_ASINH, BW_M_ACOSH, BW_M_ATANH, BW_M_EXP, BW_M_EXPM1, BW_M_LOG, BW_M_LOG2, BW_M_LOG10,
    BW_M_LOG1P, BW_M_CBRT
};
enum { BW_M_ATAN2, BW_M_POW, BW_M_FMOD, BW_M_HYPOT };   /* bw_js_math2 */

/* ---- the in-memory file system (memfs.c) --------------------------------
 * Paths are '/'-separated; relative ones start at "/". Files the program
 * writes are reported to the page (bw_js_file_saved) so it can keep them
 * across visits; files the page adds with persist = false (the game's
 * pack) aren't. */
typedef struct bw_node {
    char *path;                 /* normalised, absolute */
    bool is_dir, persist, dirty;
    unsigned char *data;
    size_t size, cap;
    int64_t mtime;              /* seconds since 1970 */
    int open_count;
    bool removed;               /* unlinked while open: freed on the last close */
} bw_node;

bool     bw_fs_normalize(const char *path, char *out, size_t size);
bw_node *bw_fs_find(const char *path);                    /* NULL if there's nothing there */
bw_node *bw_fs_create_file(const char *path, int *err);   /* the parent folder must exist; truncates */
bool     bw_fs_reserve(bw_node *n, size_t size);
void     bw_fs_touch(bw_node *n);
void     bw_fs_save(bw_node *n);                          /* tells the page, if it changed */
int      bw_fs_mkdir(const char *path);                   /* 0 or an errno value */
int      bw_fs_remove(const char *path);
int      bw_fs_rename(const char *from, const char *to);
void     bw_fs_release(bw_node *n);                       /* a FILE let go of it */
/* Calls fn for each thing directly inside `dir` (names only). False if `dir` isn't a folder. */
bool     bw_fs_list(const char *dir, void (*fn)(const bw_node *n, const char *name, void *user), void *user);

/* ---- start and the browser-owned loop (libc.c) --------------------------
 * The page calls bronto_start(argc, argv), which runs main(). A program
 * whose loop the browser has to drive (platform_web.c's run_loop) calls
 * bw_return_to_browser() after setting up its frame callback: that leaves
 * main() for good and bronto_start returns 1, "still running". */
__attribute__((noreturn)) void bw_return_to_browser(void);
void bw_stdio_flush_all(void);

#endif /* BRONTO_WEB_BW_H */
