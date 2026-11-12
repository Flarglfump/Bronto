/*
 * Terminal backend: the game drawn as coloured text in the terminal it was
 * started from, instead of in a window.
 *
 *   BR_TERMINAL_BLOCKS  every character cell shows two pixels, one above the
 *                       other: the upper half block (U+2580) in the top
 *                       pixel's colour over a background of the bottom one's.
 *   BR_TERMINAL_TEXT    every cell is one character from a ramp of densities
 *                       (" .:-=+*#%@") for the brightness of its two pixels,
 *                       in their hue, on black: ASCII art, live.
 *
 * Either way the "window" is the terminal: one pixel wide and two high per
 * cell (cells are about twice as tall as wide, so pixels come out square),
 * and the engine's scale modes place the framebuffer in it as in a window.
 * Resizing the terminal resizes the window. The framebuffer is box-filtered
 * down to the terminal's size, only the cells that changed since the last
 * frame are sent, and each frame goes out in one write inside a
 * "synchronized output" bracket, so terminals that know it never show half
 * a frame.
 *
 * Colour: 24-bit where the terminal says it has it (COLORTERM=truecolor or
 * 24bit, Windows' console, and most terminals of the last few years), else
 * the xterm 256-colour palette. BRONTO_TERMINAL_COLORS=truecolor|256 forces
 * one.
 *
 * Input. Windows' console reports real key presses and releases. Elsewhere a
 * terminal sends bytes when a key is typed and nothing when it is let go, so:
 *   - terminals that speak the kitty keyboard protocol (kitty, WezTerm,
 *     foot, Ghostty, Alacritty, iTerm2, Rio...) are asked for it, and then
 *     report presses, repeats and releases of every key, modifiers too;
 *   - with the others, a key counts as held from its press until its
 *     autorepeat stops arriving (BRONTO_TERMINAL_KEY_HOLD seconds after the
 *     press, default 0.42, then 0.15 after each repeat). Holding one key
 *     while tapping another stops the first one's repeats (the terminal only
 *     repeats the last key), so it lets go. Use a kitty-protocol terminal for
 *     games that need two keys held at once.
 * The mouse (any-motion, SGR reporting) and focus changes are reported too.
 * Ctrl+C quits.
 *
 * Sound and gamepads use the native backend's (ALSA and evdev, waveOut and
 * XInput, Core Audio and GameController): this file is compiled inside
 * platform_native.c, after the backend for the OS.
 *
 * Anything the program writes to stdout or stderr while it owns the
 * terminal (the engine's log, print() in scripts) would scribble over the
 * picture, so it is kept in a temporary file and written to the terminal
 * when the game ends.
 */
#include "bronto/platform.h"
#include "bronto/memory.h"
#include "bronto/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__wasm__)

const br_platform *br_platform_terminal(br_terminal_style style) { BR_UNUSED(style); return NULL; }
bool br_terminal_attach_fds(int in_fd, int out_fd, int cols, int rows) {
    BR_UNUSED(in_fd); BR_UNUSED(out_fd); BR_UNUSED(cols); BR_UNUSED(rows);
    return false;
}

#else

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#  define TM_DUP _dup
#  define TM_DUP2 _dup2
#  define TM_FILENO _fileno
#  define TM_ISATTY _isatty
#  define TM_CLOSE _close
#  ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#    define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#  endif
#  ifndef DISABLE_NEWLINE_AUTO_RETURN
#    define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#  endif
#  ifndef ENABLE_EXTENDED_FLAGS
#    define ENABLE_EXTENDED_FLAGS 0x0080
#  endif
#  ifndef MOUSE_HWHEELED
#    define MOUSE_HWHEELED 0x0008
#  endif
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <time.h>
#  include <unistd.h>
#  define TM_DUP dup
#  define TM_DUP2 dup2
#  define TM_FILENO fileno
#  define TM_ISATTY isatty
#  define TM_CLOSE close
#endif

/* ---- the native backend's sound and gamepads ----------------------------------- */
#if defined(BR_NO_NATIVE_PLATFORM)
#  define TM_AUDIO_OPEN(rate, ms) false
#  define TM_AUDIO_CLOSE() ((void)0)
#  define TM_AUDIO_WANTED() 0u
#  define TM_AUDIO_SUBMIT(d, n) ((void)(d), (void)(n))
#  define TM_PADS_INIT() ((void)0)
#  define TM_PADS_POLL(ev) ((void)(ev))
#  define TM_PADS_SHUTDOWN() ((void)0)
#  define TM_PADS_RUMBLE(p, lo, hi) ((void)(p), (void)(lo), (void)(hi), false)
#elif defined(_WIN32)
#  define TM_AUDIO_OPEN(rate, ms) wo_open(rate, ms)
#  define TM_AUDIO_CLOSE() wo_close()
#  define TM_AUDIO_WANTED() wo_wanted()
#  define TM_AUDIO_SUBMIT(d, n) wo_submit(d, n)
#  define TM_PADS_INIT() xi_init()
#  define TM_PADS_POLL(ev) xi_poll(ev)
#  define TM_PADS_SHUTDOWN() xi_shutdown()
#  define TM_PADS_RUMBLE(p, lo, hi) xi_rumble(p, lo, hi)
#elif defined(__APPLE__)
static void tm_gc_poll(br_events *events) {
    void *pool = objc_autoreleasePoolPush();
    gc_poll(events);
    objc_autoreleasePoolPop(pool);
}
#  define TM_AUDIO_OPEN(rate, ms) ca_open(rate, ms)
#  define TM_AUDIO_CLOSE() ca_close()
#  define TM_AUDIO_WANTED() ca_wanted()
#  define TM_AUDIO_SUBMIT(d, n) ca_submit(d, n)
#  define TM_PADS_INIT() ((void)0)
#  define TM_PADS_POLL(ev) tm_gc_poll(ev)
#  define TM_PADS_SHUTDOWN() gc_shutdown()
#  define TM_PADS_RUMBLE(p, lo, hi) ((void)(p), (void)(lo), (void)(hi), false)
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  define TM_AUDIO_OPEN(rate, ms) alsa_open(rate, ms)
#  define TM_AUDIO_CLOSE() alsa_close()
#  define TM_AUDIO_WANTED() alsa_wanted()
#  define TM_AUDIO_SUBMIT(d, n) alsa_submit(d, n)
#  define TM_PADS_INIT() evdev_init()
#  define TM_PADS_POLL(ev) evdev_poll(ev)
#  define TM_PADS_SHUTDOWN() evdev_shutdown()
#  define TM_PADS_RUMBLE(p, lo, hi) evdev_rumble(p, lo, hi)
#else
#  define TM_AUDIO_OPEN(rate, ms) false
#  define TM_AUDIO_CLOSE() ((void)0)
#  define TM_AUDIO_WANTED() 0u
#  define TM_AUDIO_SUBMIT(d, n) ((void)(d), (void)(n))
#  define TM_PADS_INIT() ((void)0)
#  define TM_PADS_POLL(ev) ((void)(ev))
#  define TM_PADS_SHUTDOWN() ((void)0)
#  define TM_PADS_RUMBLE(p, lo, hi) ((void)(p), (void)(lo), (void)(hi), false)
#endif

#define TM_GLYPH_HALF 0x2580u            /* ▀ upper half block */
#define TM_INPUT_MAX 4096
#define TM_PENDING_MAX 256
#define TM_RAMP " .:-=+*#%@"
#define TM_RAMP_LEN 10
#define TM_TEXT_FLOOR 40u                /* text style: cells darker than this are blank */

typedef struct tm_cell { uint32_t glyph, fg, bg; } tm_cell;   /* colours: tm_key() values */

static struct {
    int style;                           /* br_terminal_style */
    br_platform_config cfg;
    bool active, truecolor, audio, full_redraw, focused;
    int cols, rows;                      /* the terminal, in cells */
    int fixed_cols, fixed_rows;          /* (test hook) the size, instead of asking the terminal */
    /* drawing */
    uint32_t *pixels;                    /* cols x rows*2, 0xRRGGBB */
    size_t pixels_cap;
    tm_cell *cells, *prev;
    size_t cells_cap;
    char *out;
    size_t out_len, out_cap;
    char title[256];
    /* input */
    br_events *events;                   /* during poll */
    br_event pending[TM_PENDING_MAX];    /* posted outside poll (shutdown of held keys...) */
    int pending_count;
    unsigned char in[TM_INPUT_MAX];
    size_t in_len;
    double esc_since;                    /* when a lone ESC arrived (it may begin a sequence) */
    bool kitty;                          /* the terminal reports key releases */
    bool held[BR_KEY_COUNT];
    bool held_timed[BR_KEY_COUNT];       /* held until held_until (no releases reported) */
    double held_until[BR_KEY_COUNT];
    double first_hold, repeat_hold;
    float mouse_x, mouse_y;
    bool mouse_known;
    uint32_t high_surrogate;             /* Windows: the first half of a UTF-16 pair */
    double time_origin;
    /* output that isn't the picture */
    FILE *log_file;
    char log_path[512];
    int saved_stdout, saved_stderr;
#if defined(_WIN32)
    HANDLE hin, hout;
    DWORD in_mode, out_mode;
    UINT out_cp;
    bool modes_saved;
    LARGE_INTEGER freq;
#else
    int in_fd, out_fd;
    bool own_fd;                         /* opened /dev/tty ourselves */
    struct termios saved_termios;
    bool termios_saved;
    bool test_fds;
#endif
} T;   /* zero: style BLOCKS until br_platform_terminal sets it */

/* ---- time ------------------------------------------------------------------- */
static double tm_time(void) {
#if defined(_WIN32)
    LARGE_INTEGER now;
    if (!T.freq.QuadPart) QueryPerformanceFrequency(&T.freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)T.freq.QuadPart - T.time_origin;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9 - T.time_origin;
#endif
}

static void tm_sleep(double s) {
    if (s <= 0) return;
#if defined(_WIN32)
    {
        double target = tm_time() + s;
        if (s > 0.002) Sleep((DWORD)((s - 0.0015) * 1000.0));
        while (tm_time() < target) Sleep(0);
    }
#else
    {
        struct timespec ts;
        ts.tv_sec = (time_t)s;
        ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
    }
#endif
}

/* ---- writing to the terminal ----------------------------------------------- */
static bool tm_reserve(size_t extra) {
    if (T.out_len + extra <= T.out_cap) return true;
    {
        size_t cap = T.out_cap ? T.out_cap : 65536;
        char *p;
        while (cap < T.out_len + extra) cap *= 2;
        p = (char *)br_realloc(T.out, cap);
        if (!p) return false;
        T.out = p;
        T.out_cap = cap;
    }
    return true;
}

static void tm_put(const char *s, size_t n) {
    if (!tm_reserve(n)) return;
    memcpy(T.out + T.out_len, s, n);
    T.out_len += n;
}
static void tm_puts(const char *s) { tm_put(s, strlen(s)); }

static void tm_put_uint(uint32_t v) {
    char buf[12];
    int n = 0;
    do { buf[11 - n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    tm_put(buf + 12 - n, (size_t)n);
}

static void tm_put_utf8(uint32_t cp) {
    char b[4];
    if (cp < 0x80) { b[0] = (char)cp; tm_put(b, 1); }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); tm_put(b, 2); }
    else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F));
        tm_put(b, 3);
    } else {
        b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F));
        tm_put(b, 4);
    }
}

static void tm_write_raw(const char *s, size_t n) {
#if defined(_WIN32)
    while (n > 0) {
        DWORD wrote = 0;
        if (!WriteFile(T.hout, s, (DWORD)(n > 0x10000000u ? 0x10000000u : n), &wrote, NULL) || wrote == 0) return;
        s += wrote;
        n -= wrote;
    }
#else
    while (n > 0) {
        ssize_t w = write(T.out_fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { tm_sleep(0.001); continue; }
            return;
        }
        s += w;
        n -= (size_t)w;
    }
#endif
}

static void tm_flush(void) {
    if (T.out_len) tm_write_raw(T.out, T.out_len);
    T.out_len = 0;
}

/* ---- colour ------------------------------------------------------------------ */
static const uint8_t k_cube[6] = { 0, 95, 135, 175, 215, 255 };

static int tm_cube_index(int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; }

/* The colour as the terminal will show it: 0xRRGGBB in truecolor, else
 * 0x1000000 | an xterm-256 palette index (the nearer of the 6x6x6 cube and
 * the grey ramp). Equal keys look the same, so unchanged cells are skipped. */
static uint32_t tm_key(uint32_t rgb) {
    int r = (int)(rgb >> 16) & 255, g = (int)(rgb >> 8) & 255, b = (int)rgb & 255;
    int ci, cr, cg, cb, grey, gi, gv;
    long dc, dg;
    if (T.truecolor) return rgb;
    cr = tm_cube_index(r); cg = tm_cube_index(g); cb = tm_cube_index(b);
    ci = 16 + 36 * cr + 6 * cg + cb;
    grey = (r + g + b) / 3;
    gi = grey > 238 ? 23 : (grey - 3) / 10;
    if (gi < 0) gi = 0;
    gv = 8 + 10 * gi;
    dc = (long)(r - k_cube[cr]) * (r - k_cube[cr]) + (long)(g - k_cube[cg]) * (g - k_cube[cg]) + (long)(b - k_cube[cb]) * (b - k_cube[cb]);
    dg = (long)(r - gv) * (r - gv) + (long)(g - gv) * (g - gv) + (long)(b - gv) * (b - gv);
    return 0x1000000u | (uint32_t)(dg < dc ? 232 + gi : ci);
}

static void tm_put_color(bool fg, uint32_t key) {
    tm_puts(fg ? "38;" : "48;");
    if (key & 0x1000000u) { tm_puts("5;"); tm_put_uint(key & 255u); }
    else {
        tm_puts("2;");
        tm_put_uint((key >> 16) & 255u); tm_put(";", 1);
        tm_put_uint((key >> 8) & 255u); tm_put(";", 1);
        tm_put_uint(key & 255u);
    }
}

/* ---- the picture --------------------------------------------------------------- */
static bool tm_grow(void **p, size_t *cap, size_t want, size_t elem) {
    void *n;
    if (want <= *cap) return true;
    n = br_realloc(*p, want * elem);
    if (!n) return false;
    *p = n;
    *cap = want;
    return true;
}

/* The framebuffer, box-filtered into vp of a w x h image of 0xRRGGBB (black around it). */
static void tm_sample(const br_image *fb, br_recti vp, uint32_t *dst, int w, int h) {
    int x, y;
    for (y = 0; y < h; ++y) {
        uint32_t *row = dst + (size_t)y * (size_t)w;
        if (y < vp.y || y >= vp.y + vp.h || !fb->pixels || vp.w <= 0) { memset(row, 0, (size_t)w * sizeof(uint32_t)); continue; }
        {
            int sy0 = (int)((long)(y - vp.y) * fb->height / vp.h), sy1 = (int)((long)(y - vp.y + 1) * fb->height / vp.h);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            if (sy1 > fb->height) sy1 = fb->height;
            for (x = 0; x < w; ++x) {
                int sx0, sx1, sx, sy;
                uint32_t r = 0, g = 0, b = 0, n;
                if (x < vp.x || x >= vp.x + vp.w) { row[x] = 0; continue; }
                sx0 = (int)((long)(x - vp.x) * fb->width / vp.w);
                sx1 = (int)((long)(x - vp.x + 1) * fb->width / vp.w);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sx1 > fb->width) sx1 = fb->width;
                for (sy = sy0; sy < sy1; ++sy) {
                    const br_color *src = fb->pixels + (size_t)sy * (size_t)fb->width;
                    for (sx = sx0; sx < sx1; ++sx) { r += src[sx].r; g += src[sx].g; b += src[sx].b; }
                }
                n = (uint32_t)((sy1 - sy0) * (sx1 - sx0));
                if (!n) { row[x] = 0; continue; }
                row[x] = ((r + n / 2) / n) << 16 | ((g + n / 2) / n) << 8 | ((b + n / 2) / n);
            }
        }
    }
}

static uint32_t tm_avg(uint32_t a, uint32_t b) {
    return (((a >> 16 & 255) + (b >> 16 & 255) + 1) / 2) << 16 | (((a >> 8 & 255) + (b >> 8 & 255) + 1) / 2) << 8 |
           (((a & 255) + (b & 255) + 1) / 2);
}

/* What one cell shows for its top and bottom pixels. */
static tm_cell tm_make_cell(uint32_t top, uint32_t bottom) {
    tm_cell c;
    if (T.style == BR_TERMINAL_TEXT) {
        uint32_t avg = tm_avg(top, bottom);
        uint32_t r = avg >> 16 & 255, g = avg >> 8 & 255, b = avg & 255;
        uint32_t lum = (77u * r + 150u * g + 29u * b) >> 8, mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        /* Below TM_TEXT_FLOOR is blank, so a dark backdrop stays dark instead of a field of dots. */
        uint32_t i = lum <= TM_TEXT_FLOOR ? 0 : 1 + (lum - TM_TEXT_FLOOR - 1) * (TM_RAMP_LEN - 1) / (256u - TM_TEXT_FLOOR - 1);
        c.bg = tm_key(0);
        if (i == 0 || mx == 0) { c.glyph = ' '; c.fg = c.bg; return c; }
        /* The glyph carries the brightness, so its colour is the hue at full strength. */
        c.glyph = (uint32_t)(unsigned char)TM_RAMP[i];
        c.fg = tm_key(((r * 255u + mx / 2) / mx) << 16 | ((g * 255u + mx / 2) / mx) << 8 | ((b * 255u + mx / 2) / mx));
        return c;
    }
    c.fg = tm_key(top);
    c.bg = tm_key(bottom);
    if (c.fg == c.bg) c.glyph = ' ';     /* one colour: a blank cell says it in fewer bytes */
    else c.glyph = TM_GLYPH_HALF;
    return c;
}

/* Sends the cells that differ from what the terminal shows. */
static void tm_emit_cells(void) {
    int x, y;
    uint32_t cur_fg = 0, cur_bg = 0;
    bool fg_known = false, bg_known = false;
    tm_puts("\x1b[?2026h");                                  /* synchronized output: begin */
    if (T.full_redraw) tm_puts("\x1b[0m\x1b[2J");
    for (y = 0; y < T.rows; ++y) {
        bool at = false;                                     /* is the cursor where the next cell goes? */
        for (x = 0; x < T.cols; ++x) {
            size_t i = (size_t)y * (size_t)T.cols + (size_t)x;
            tm_cell c = T.cells[i];
            bool need_fg, need_bg;
            if (!T.full_redraw && c.glyph == T.prev[i].glyph && c.bg == T.prev[i].bg && (c.glyph == ' ' || c.fg == T.prev[i].fg)) {
                at = false;
                continue;
            }
            if (!at) {
                tm_puts("\x1b[");
                tm_put_uint((uint32_t)y + 1);
                tm_put(";", 1);
                tm_put_uint((uint32_t)x + 1);
                tm_put("H", 1);
                at = true;
            }
            need_bg = !bg_known || cur_bg != c.bg;
            need_fg = c.glyph != ' ' && (!fg_known || cur_fg != c.fg);
            if (need_fg || need_bg) {
                tm_puts("\x1b[");
                if (need_fg) tm_put_color(true, c.fg);
                if (need_fg && need_bg) tm_put(";", 1);
                if (need_bg) tm_put_color(false, c.bg);
                tm_put("m", 1);
                if (need_fg) { cur_fg = c.fg; fg_known = true; }
                if (need_bg) { cur_bg = c.bg; bg_known = true; }
            }
            tm_put_utf8(c.glyph);
            T.prev[i] = c;
        }
    }
    tm_puts("\x1b[?2026l");                                  /* synchronized output: end */
    T.full_redraw = false;
}

static void tm_present(const br_image *fb, br_recti vp) {
    int w = T.cols, h = T.rows * 2, x, y;
    size_t ncells = (size_t)T.cols * (size_t)T.rows;
    if (!T.active || T.cols <= 0 || T.rows <= 0) return;
    if (!tm_grow((void **)&T.pixels, &T.pixels_cap, (size_t)w * (size_t)h, sizeof(uint32_t))) return;
    if (ncells > T.cells_cap) {
        size_t cap = T.cells_cap;
        if (!tm_grow((void **)&T.cells, &cap, ncells, sizeof(tm_cell))) return;
        cap = T.cells_cap;
        if (!tm_grow((void **)&T.prev, &cap, ncells, sizeof(tm_cell))) return;
        T.cells_cap = cap;
        T.full_redraw = true;
    }
    tm_sample(fb, vp, T.pixels, w, h);
    for (y = 0; y < T.rows; ++y)
        for (x = 0; x < T.cols; ++x)
            T.cells[(size_t)y * (size_t)T.cols + (size_t)x] =
                tm_make_cell(T.pixels[(size_t)(2 * y) * (size_t)w + (size_t)x], T.pixels[(size_t)(2 * y + 1) * (size_t)w + (size_t)x]);
    tm_emit_cells();
    tm_flush();
}

/* ---- events -------------------------------------------------------------------- */
static void tm_post(const br_event *e) {
    if (T.events) br_events_post(T.events, e);
    else if (T.pending_count < TM_PENDING_MAX) T.pending[T.pending_count++] = *e;
}

static void tm_key_up(br_key key, uint32_t mods) {
    br_event e;
    if (key <= BR_KEY_UNKNOWN || key >= BR_KEY_COUNT || !T.held[key]) return;
    T.held[key] = false;
    T.held_timed[key] = false;
    e = br_event_key(BR_EVENT_KEY_UP, key, mods, false);
    tm_post(&e);
}

static void tm_release_all(void) {
    int k;
    for (k = 1; k < BR_KEY_COUNT; ++k) tm_key_up((br_key)k, 0);
}

static void tm_text(uint32_t cp) {
    br_event t;
    if (cp < 0x20 || cp == 0x7f || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return;
    t = br_event_make(BR_EVENT_TEXT_INPUT);
    t.u.text.codepoint = cp;
    tm_post(&t);
}

/* A key went down (or repeated). `timed`: the terminal won't say when it's let
 * go, so it's let go when its repeats stop. `text`: what it types, or 0. */
static void tm_key_down(br_key key, uint32_t mods, bool timed, uint32_t text) {
    br_event e;
    bool repeat;
    if (key > BR_KEY_UNKNOWN && key < BR_KEY_COUNT) {
        repeat = T.held[key];
        if (timed) {
            double now = tm_time();
            T.held_until[key] = now + (repeat ? T.repeat_hold : T.first_hold);
            T.held_timed[key] = true;
        } else {
            T.held_timed[key] = false;
        }
        T.held[key] = true;
        e = br_event_key(BR_EVENT_KEY_DOWN, key, mods, repeat);
        tm_post(&e);
    }
    if (text && !(mods & (BR_MOD_CONTROL | BR_MOD_SUPER)) && !((mods & BR_MOD_ALT) && text < 0x80)) tm_text(text);
}

static void tm_release_expired(void) {
    double now = tm_time();
    int k;
    for (k = 1; k < BR_KEY_COUNT; ++k)
        if (T.held[k] && T.held_timed[k] && now >= T.held_until[k]) tm_key_up((br_key)k, 0);
}

static void tm_quit(void) {
    br_event q = br_event_make(BR_EVENT_QUIT);
    tm_post(&q);
}

static void tm_mouse_to(float x, float y) {
    br_event e;
    if (T.mouse_known && x == T.mouse_x && y == T.mouse_y) return;
    e = br_event_mouse_move(x, y, T.mouse_known ? x - T.mouse_x : 0, T.mouse_known ? y - T.mouse_y : 0);
    T.mouse_x = x;
    T.mouse_y = y;
    T.mouse_known = true;
    tm_post(&e);
}

static void tm_mouse_button(br_mouse_button b, bool down) {
    br_event e = br_event_mouse_button(down ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP, b, T.mouse_x, T.mouse_y);
    e.u.mouse_button.clicks = 1;
    tm_post(&e);
}

static void tm_wheel(float dx, float dy) {
    br_event w = br_event_make(BR_EVENT_MOUSE_WHEEL);
    w.u.wheel.dx = dx;
    w.u.wheel.dy = dy;
    tm_post(&w);
}

static void tm_focus(bool gained) {
    br_event e;
    if (gained == T.focused) return;
    T.focused = gained;
    if (!gained) tm_release_all();
    e = br_event_make(gained ? BR_EVENT_WINDOW_FOCUS_GAINED : BR_EVENT_WINDOW_FOCUS_LOST);
    tm_post(&e);
}

/* The terminal's size, in cells; posts a resize when it changed. */
static void tm_check_size(void) {
    int cols = T.cols, rows = T.rows;
    if (T.fixed_cols > 0) { cols = T.fixed_cols; rows = T.fixed_rows; }
    else {
#if defined(_WIN32)
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(T.hout, &info)) {
            cols = info.srWindow.Right - info.srWindow.Left + 1;
            rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        }
#else
        struct winsize ws;
        if (ioctl(T.out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            cols = ws.ws_col;
            rows = ws.ws_row;
        }
#endif
    }
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 24;
    if (cols != T.cols || rows != T.rows) {
        br_event e = br_event_make(BR_EVENT_WINDOW_RESIZED);
        bool first = T.cols == 0;
        T.cols = cols;
        T.rows = rows;
        T.full_redraw = true;
        if (!first) {
            e.u.resize.width = cols;
            e.u.resize.height = rows * 2;
            tm_post(&e);
        }
    }
}

/* ---- keys: what terminals send ------------------------------------------------- */

/* A printable ASCII character: its key on a US layout, and whether Shift made it. */
static br_key tm_ascii_key(uint32_t ch, bool *shift) {
    static const char shifted[] = "!@#$%^&*()_+{}|:\"<>?~";
    static const char base[] = "1234567890-=[]\\;',./`";
    const char *p;
    *shift = false;
    if (ch >= 'a' && ch <= 'z') return (br_key)(BR_KEY_A + (int)(ch - 'a'));
    if (ch >= 'A' && ch <= 'Z') { *shift = true; return (br_key)(BR_KEY_A + (int)(ch - 'A')); }
    if (ch == ' ') return BR_KEY_SPACE;
    if (ch >= 0x80 || ch < 0x20) return BR_KEY_UNKNOWN;
    if ((p = strchr(shifted, (int)ch)) != NULL && ch) { *shift = true; ch = (uint32_t)(unsigned char)base[p - shifted]; }
    switch (ch) {
    case '\'': return BR_KEY_APOSTROPHE;
    case ',': return BR_KEY_COMMA;
    case '-': return BR_KEY_MINUS;
    case '.': return BR_KEY_PERIOD;
    case '/': return BR_KEY_SLASH;
    case ';': return BR_KEY_SEMICOLON;
    case '=': return BR_KEY_EQUAL;
    case '[': return BR_KEY_LEFT_BRACKET;
    case '\\': return BR_KEY_BACKSLASH;
    case ']': return BR_KEY_RIGHT_BRACKET;
    case '`': return BR_KEY_GRAVE;
    default: break;
    }
    if (ch >= '0' && ch <= '9') return (br_key)(BR_KEY_0 + (int)(ch - '0'));
    return BR_KEY_UNKNOWN;
}

/* xterm's modifier parameter (1 + bits) -> BR_MOD_*. */
static uint32_t tm_mods(unsigned param) {
    unsigned m = param > 0 ? param - 1 : 0;
    uint32_t r = 0;
    if (m & 1u) r |= BR_MOD_SHIFT;
    if (m & 2u) r |= BR_MOD_ALT;
    if (m & 4u) r |= BR_MOD_CONTROL;
    if (m & 8u) r |= BR_MOD_SUPER;
    if (m & 64u) r |= BR_MOD_CAPS_LOCK;
    return r;
}

/* A kitty-protocol key number -> br_key (text keys are their lower-case codepoint). */
static br_key tm_kitty_key(unsigned code) {
    bool shift;
    switch (code) {
    case 27: return BR_KEY_ESCAPE;
    case 13: return BR_KEY_ENTER;
    case 9: return BR_KEY_TAB;
    case 127: case 8: return BR_KEY_BACKSPACE;
    case 57358: return BR_KEY_CAPS_LOCK;
    case 57359: return BR_KEY_SCROLL_LOCK;
    case 57360: return BR_KEY_NUM_LOCK;
    case 57361: return BR_KEY_PRINT_SCREEN;
    case 57362: return BR_KEY_PAUSE;
    case 57363: return BR_KEY_MENU;
    case 57409: return BR_KEY_KP_DECIMAL;
    case 57410: return BR_KEY_KP_DIVIDE;
    case 57411: return BR_KEY_KP_MULTIPLY;
    case 57412: return BR_KEY_KP_SUBTRACT;
    case 57413: return BR_KEY_KP_ADD;
    case 57414: return BR_KEY_KP_ENTER;
    case 57415: return BR_KEY_KP_EQUAL;
    case 57417: return BR_KEY_LEFT;
    case 57418: return BR_KEY_RIGHT;
    case 57419: return BR_KEY_UP;
    case 57420: return BR_KEY_DOWN;
    case 57421: return BR_KEY_PAGE_UP;
    case 57422: return BR_KEY_PAGE_DOWN;
    case 57423: return BR_KEY_HOME;
    case 57424: return BR_KEY_END;
    case 57425: return BR_KEY_INSERT;
    case 57426: return BR_KEY_DELETE;
    case 57441: return BR_KEY_LEFT_SHIFT;
    case 57442: return BR_KEY_LEFT_CONTROL;
    case 57443: return BR_KEY_LEFT_ALT;
    case 57444: return BR_KEY_LEFT_SUPER;
    case 57447: return BR_KEY_RIGHT_SHIFT;
    case 57448: return BR_KEY_RIGHT_CONTROL;
    case 57449: return BR_KEY_RIGHT_ALT;
    case 57450: return BR_KEY_RIGHT_SUPER;
    default: break;
    }
    if (code >= 57399 && code <= 57408) return (br_key)(BR_KEY_KP_0 + (int)(code - 57399));
    if (code < 128) return tm_ascii_key(code, &shift);
    return BR_KEY_UNKNOWN;
}

/* CSI <number> ~ */
static br_key tm_tilde_key(unsigned n) {
    switch (n) {
    case 1: case 7: return BR_KEY_HOME;
    case 2: return BR_KEY_INSERT;
    case 3: return BR_KEY_DELETE;
    case 4: case 8: return BR_KEY_END;
    case 5: return BR_KEY_PAGE_UP;
    case 6: return BR_KEY_PAGE_DOWN;
    case 11: return BR_KEY_F1;
    case 12: return BR_KEY_F2;
    case 13: return BR_KEY_F3;
    case 14: return BR_KEY_F4;
    case 15: return BR_KEY_F5;
    case 17: return BR_KEY_F6;
    case 18: return BR_KEY_F7;
    case 19: return BR_KEY_F8;
    case 20: return BR_KEY_F9;
    case 21: return BR_KEY_F10;
    case 23: return BR_KEY_F11;
    case 24: return BR_KEY_F12;
    case 57427: return BR_KEY_KP_5;  /* kitty: the keypad's centre key */
    default: return BR_KEY_UNKNOWN;
    }
}

/* CSI/SS3 final letter -> key (arrows, Home/End, F1-F4). */
static br_key tm_letter_key(unsigned char f) {
    switch (f) {
    case 'A': return BR_KEY_UP;
    case 'B': return BR_KEY_DOWN;
    case 'C': return BR_KEY_RIGHT;
    case 'D': return BR_KEY_LEFT;
    case 'H': return BR_KEY_HOME;
    case 'F': return BR_KEY_END;
    case 'E': return BR_KEY_KP_5;
    case 'P': return BR_KEY_F1;
    case 'Q': return BR_KEY_F2;
    case 'R': return BR_KEY_F3;
    case 'S': return BR_KEY_F4;
    default: return BR_KEY_UNKNOWN;
    }
}

/* A key event from a sequence that may carry a kitty event type
 * (1 press, 2 repeat, 3 release; 0 = not given: a legacy press). */
static void tm_key_event(br_key key, uint32_t mods, unsigned type, uint32_t text) {
    if (key == BR_KEY_C && (mods & BR_MOD_CONTROL) && !(mods & (BR_MOD_ALT | BR_MOD_SHIFT)) && type != 3) { tm_quit(); return; }
    if (type == 3) { tm_key_up(key, mods); return; }
    tm_key_down(key, mods, type == 0, text);
}

/* Parameters of a CSI sequence: up to 8 fields separated by ';', each with
 * up to 4 sub-fields separated by ':'. Missing ones are 0. */
typedef struct tm_params { unsigned v[8][4]; int count; char prefix; } tm_params;

static void tm_parse_params(const unsigned char *p, size_t n, tm_params *out) {
    size_t i = 0;
    int f = 0, s = 0;
    memset(out, 0, sizeof(*out));
    if (n && (p[0] == '<' || p[0] == '=' || p[0] == '>' || p[0] == '?')) { out->prefix = (char)p[0]; i = 1; }
    out->count = n > i ? 1 : 0;
    for (; i < n; ++i) {
        unsigned char c = p[i];
        if (c >= '0' && c <= '9') { if (f < 8 && s < 4 && out->v[f][s] < 100000000u) out->v[f][s] = out->v[f][s] * 10u + (unsigned)(c - '0'); }
        else if (c == ';') { ++f; s = 0; if (f < 8) out->count = f + 1; }
        else if (c == ':') ++s;
    }
}

static void tm_csi(const unsigned char *params, size_t plen, unsigned char final) {
    tm_params P;
    tm_parse_params(params, plen, &P);
    if (P.prefix == '<' && (final == 'M' || final == 'm')) {          /* SGR mouse */
        unsigned b = P.v[0][0];
        float x = (float)(P.v[1][0] ? P.v[1][0] - 1 : 0) + 0.5f, y = (float)((P.v[2][0] ? P.v[2][0] - 1 : 0) * 2) + 1.0f;
        tm_mouse_to(x, y);
        if (b & 64u) {
            unsigned w = b & 3u;
            if (final == 'M') tm_wheel(w == 2 ? -1.0f : w == 3 ? 1.0f : 0.0f, w == 0 ? 1.0f : w == 1 ? -1.0f : 0.0f);
        } else if (!(b & 32u)) {
            unsigned btn = b & 3u;
            if (btn < 3) tm_mouse_button(btn == 0 ? BR_MOUSE_LEFT : btn == 1 ? BR_MOUSE_MIDDLE : BR_MOUSE_RIGHT, final == 'M');
        }
        return;
    }
    if (P.prefix == '?' && final == 'u') {                              /* the kitty protocol is on */
        if (!T.kitty) { T.kitty = true; tm_release_all(); }
        return;
    }
    if (P.prefix) return;                                               /* other replies */
    if (final == 'I') { tm_focus(true); return; }
    if (final == 'O') { tm_focus(false); return; }
    if (final == 'Z') { tm_key_event(BR_KEY_TAB, BR_MOD_SHIFT, 0, 0); return; }
    if (final == 'u') {                                                 /* kitty: code[:shifted] ; mods[:type] ; text */
        unsigned code = P.v[0][0], shifted = P.v[0][1];
        uint32_t mods = tm_mods(P.v[1][0]);
        unsigned type = P.count >= 2 ? (P.v[1][1] ? P.v[1][1] : 1) : 1;
        uint32_t text = 0;
        br_key key = tm_kitty_key(code);
        if (P.count >= 3 && P.v[2][0]) text = P.v[2][0];
        else if (type != 3 && code >= 32 && code < 0x10FFFF && code != 127 && !(code >= 57344 && code <= 63743))
            text = (mods & BR_MOD_SHIFT) && shifted ? shifted : code;
        if (code == 27 || code == 13 || code == 9 || code == 127 || code == 8) text = 0;
        tm_key_event(key, mods, type, text);
        return;
    }
    if (final == '~') {
        unsigned type = P.count >= 2 ? P.v[1][1] : 0;
        if (P.v[0][0] == 200 || P.v[0][0] == 201) return;               /* bracketed paste marks */
        tm_key_event(tm_tilde_key(P.v[0][0]), tm_mods(P.v[1][0]), type, 0);
        return;
    }
    {
        br_key key = tm_letter_key(final);
        if (key != BR_KEY_UNKNOWN) tm_key_event(key, tm_mods(P.count >= 2 ? P.v[1][0] : 0), P.count >= 2 ? P.v[1][1] : 0, 0);
    }
}

/* One byte (or a UTF-8 sequence) typed as itself. `alt`: ESC came first. */
static void tm_plain(uint32_t ch, bool alt) {
    uint32_t mods = alt ? BR_MOD_ALT : 0;
    bool shift = false;
    br_key key;
    if (ch == 3 && !alt) { tm_quit(); return; }
    switch (ch) {
    case 13: case 10: tm_key_down(BR_KEY_ENTER, mods, true, 0); return;
    case 9: tm_key_down(BR_KEY_TAB, mods, true, 0); return;
    case 127: case 8: tm_key_down(BR_KEY_BACKSPACE, mods, true, 0); return;
    case 27: tm_key_down(BR_KEY_ESCAPE, mods, true, 0); return;
    case 0: tm_key_down(BR_KEY_SPACE, mods | BR_MOD_CONTROL, true, 0); return;
    default: break;
    }
    if (ch >= 1 && ch <= 26) { tm_key_down((br_key)(BR_KEY_A + (int)(ch - 1)), mods | BR_MOD_CONTROL, true, 0); return; }
    if (ch < 0x20) return;
    key = tm_ascii_key(ch, &shift);
    if (shift) mods |= BR_MOD_SHIFT;
    tm_key_down(key, mods, true, ch);
}

/* Length of the UTF-8 sequence that starts with b (1 for a stray byte). */
static size_t tm_utf8_len(unsigned char b) { return b < 0xC0 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF8 ? 4 : 1; }

static uint32_t tm_utf8_decode(const unsigned char *p, size_t n) {
    uint32_t cp;
    size_t i;
    if (n == 1) return p[0];
    cp = p[0] & (n == 2 ? 0x1Fu : n == 3 ? 0x0Fu : 0x07u);
    for (i = 1; i < n; ++i) cp = (cp << 6) | (p[i] & 0x3Fu);
    return cp;
}

/* Handles everything complete in T.in, keeping an unfinished sequence for later. */
static void tm_parse_input(void) {
    size_t i = 0;
    double now = tm_time();
    while (i < T.in_len) {
        const unsigned char *p = T.in + i;
        size_t left = T.in_len - i;
        if (p[0] == 0x1b) {
            if (left == 1) {                                    /* ESC alone: the key, or the start of something */
                if (T.esc_since < 0) T.esc_since = now;
                if (now - T.esc_since < 0.03) break;
                T.esc_since = -1;
                tm_plain(27, false);
                i += 1;
                continue;
            }
            T.esc_since = -1;
            if (p[1] == '[') {                                  /* CSI params final */
                size_t j = 2;
                while (j < left && p[j] >= 0x20 && p[j] <= 0x3F) ++j;
                while (j < left && p[j] >= 0x20 && p[j] <= 0x2F) ++j;
                if (j >= left) { if (left < 64) break; i += left; continue; }
                tm_csi(p + 2, j - 2, p[j]);
                i += j + 1;
                continue;
            }
            if (p[1] == 'O') {                                  /* SS3 final */
                if (left < 3) break;
                tm_key_event(tm_letter_key(p[2]), 0, 0, 0);
                i += 3;
                continue;
            }
            if (p[1] == ']' || p[1] == 'P' || p[1] == '_') {   /* OSC / DCS / APC reply: skip to its end */
                size_t j = 2;
                while (j < left && p[j] != 0x07 && !(p[j] == 0x1b && j + 1 < left && p[j + 1] == '\\')) ++j;
                if (j >= left) { if (left < 512) break; i += left; continue; }
                i += j + (p[j] == 0x07 ? 1 : 2);
                continue;
            }
            if (p[1] == 0x1b) { tm_plain(27, false); i += 1; continue; }
            {                                                   /* Alt + a key */
                size_t n = tm_utf8_len(p[1]);
                if (left < 1 + n) break;
                tm_plain(tm_utf8_decode(p + 1, n), true);
                i += 1 + n;
                continue;
            }
        }
        {
            size_t n = tm_utf8_len(p[0]);
            if (left < n) break;
            if (n == 1) tm_plain(p[0], false);
            else tm_text(tm_utf8_decode(p, n));
            i += n;
        }
    }
    if (i > 0) {
        memmove(T.in, T.in + i, T.in_len - i);
        T.in_len -= i;
    }
}

/* ---- Windows: the console's own input records ---------------------------------- */
#if defined(_WIN32)
static uint32_t tm_win_mods(DWORD s) {
    uint32_t m = 0;
    if (s & SHIFT_PRESSED) m |= BR_MOD_SHIFT;
    if (s & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) m |= BR_MOD_CONTROL;
    if (s & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) m |= BR_MOD_ALT;
    if (s & CAPSLOCK_ON) m |= BR_MOD_CAPS_LOCK;
    return m;
}

static void tm_win_key(const KEY_EVENT_RECORD *k) {
    LPARAM lp = (LPARAM)(((DWORD)k->wVirtualScanCode & 0xFFu) << 16) | ((k->dwControlKeyState & ENHANCED_KEY) ? (LPARAM)1 << 24 : 0);
    br_key key = w32_translate_key((WPARAM)k->wVirtualKeyCode, lp);
    uint32_t mods = tm_win_mods(k->dwControlKeyState), cp = (uint32_t)k->uChar.UnicodeChar;
    bool ctrl_only = (mods & BR_MOD_CONTROL) && !(mods & BR_MOD_ALT);    /* (Ctrl+Alt is AltGr) */
    WORD n, r = k->wRepeatCount ? k->wRepeatCount : 1;
    if (!k->bKeyDown) {
        if (cp >= 0xD800 && cp <= 0xDBFF) T.high_surrogate = cp;
        tm_key_up(key, mods);
        return;
    }
    if (key == BR_KEY_C && ctrl_only && !(mods & BR_MOD_SHIFT)) { tm_quit(); return; }
    if (cp >= 0xD800 && cp <= 0xDBFF) { T.high_surrogate = cp; cp = 0; }
    else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        cp = T.high_surrogate ? 0x10000u + ((T.high_surrogate - 0xD800u) << 10) + (cp - 0xDC00u) : 0;
        T.high_surrogate = 0;
    }
    if (ctrl_only) cp = 0;
    for (n = 0; n < r; ++n) {
        if (key != BR_KEY_UNKNOWN) tm_key_down(key, mods, false, 0);
        if (cp) tm_text(cp);
    }
}

static void tm_win_mouse(const MOUSE_EVENT_RECORD *m) {
    static DWORD last_buttons;
    CONSOLE_SCREEN_BUFFER_INFO info;
    int top = 0, left = 0;
    DWORD b = m->dwButtonState;
    if (GetConsoleScreenBufferInfo(T.hout, &info)) { top = info.srWindow.Top; left = info.srWindow.Left; }
    tm_mouse_to((float)(m->dwMousePosition.X - left) + 0.5f, (float)((m->dwMousePosition.Y - top) * 2) + 1.0f);
    if (m->dwEventFlags & MOUSE_WHEELED) { tm_wheel(0, (short)HIWORD(b) > 0 ? 1.0f : -1.0f); return; }
    if (m->dwEventFlags & MOUSE_HWHEELED) { tm_wheel((short)HIWORD(b) > 0 ? 1.0f : -1.0f, 0); return; }
    if ((b ^ last_buttons) & FROM_LEFT_1ST_BUTTON_PRESSED) tm_mouse_button(BR_MOUSE_LEFT, (b & FROM_LEFT_1ST_BUTTON_PRESSED) != 0);
    if ((b ^ last_buttons) & RIGHTMOST_BUTTON_PRESSED) tm_mouse_button(BR_MOUSE_RIGHT, (b & RIGHTMOST_BUTTON_PRESSED) != 0);
    if ((b ^ last_buttons) & FROM_LEFT_2ND_BUTTON_PRESSED) tm_mouse_button(BR_MOUSE_MIDDLE, (b & FROM_LEFT_2ND_BUTTON_PRESSED) != 0);
    last_buttons = b & 0xFFFFu;
}

static void tm_read_input(void) {
    INPUT_RECORD recs[64];
    DWORD count = 0, i;
    for (;;) {
        if (!GetNumberOfConsoleInputEvents(T.hin, &count) || count == 0) return;
        if (!ReadConsoleInputW(T.hin, recs, 64, &count) || count == 0) return;
        for (i = 0; i < count; ++i) {
            switch (recs[i].EventType) {
            case KEY_EVENT: tm_win_key(&recs[i].Event.KeyEvent); break;
            case MOUSE_EVENT: tm_win_mouse(&recs[i].Event.MouseEvent); break;
            case FOCUS_EVENT: tm_focus(recs[i].Event.FocusEvent.bSetFocus != 0); break;
            case WINDOW_BUFFER_SIZE_EVENT: tm_check_size(); break;
            default: break;
            }
        }
    }
}

/* Closing the console window, logging off: put the console back first. */
static void tm_restore_terminal(void);
static BOOL WINAPI tm_ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) { tm_quit(); return TRUE; }
    tm_restore_terminal();
    return FALSE;
}
#else
static void tm_read_input(void) {
    unsigned char buf[1024];
    for (;;) {
        ssize_t n = read(T.in_fd, buf, sizeof(buf));
        if (n > 0) {
            size_t k = (size_t)n;
            if (T.in_len + k > TM_INPUT_MAX) T.in_len = 0;        /* nonsense filled it: drop it */
            if (k > TM_INPUT_MAX) k = TM_INPUT_MAX;
            memcpy(T.in + T.in_len, buf, k);
            T.in_len += k;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}
#endif

static void tm_poll(br_events *events) {
    int i;
    T.events = events;
    for (i = 0; i < T.pending_count; ++i) br_events_post(events, &T.pending[i]);
    T.pending_count = 0;
    tm_check_size();
    tm_read_input();
#if !defined(_WIN32)
    tm_parse_input();
#endif
    tm_release_expired();
    TM_PADS_POLL(events);
    T.events = NULL;
}

/* ---- owning the terminal, and giving it back ------------------------------------ */
static const char k_enter[] =
    "\x1b[22;0t"            /* keep the title, to put back */
    "\x1b[?1049h"           /* the alternate screen */
    "\x1b[?25l"             /* no cursor */
    "\x1b[?7l"              /* no wrapping at the right edge */
    "\x1b[?1003h\x1b[?1006h"/* the mouse: every movement, in SGR form */
    "\x1b[?1004h"           /* focus in / out */
    "\x1b[>31u"             /* kitty keyboard: presses, repeats, releases, every key, with its text */
    "\x1b[?u"               /* ... and does it understand? (it answers CSI ? flags u) */
    "\x1b[0m\x1b[2J";
static const char k_leave[] =
    "\x1b[<u"
    "\x1b[?1004l\x1b[?1006l\x1b[?1003l"
    "\x1b[0m\x1b[2J\x1b[H"
    "\x1b[?7h\x1b[?25h"
    "\x1b[?1049l"
    "\x1b[23;0t";

/* Called from signal handlers too: only async-signal-safe calls. */
static void tm_restore_terminal(void) {
    if (!T.active) return;
    T.active = false;
#if defined(_WIN32)
    {
        DWORD w;
        WriteFile(T.hout, k_leave, (DWORD)(sizeof(k_leave) - 1), &w, NULL);
        if (T.modes_saved) {
            SetConsoleMode(T.hin, T.in_mode);
            SetConsoleMode(T.hout, T.out_mode);
            SetConsoleOutputCP(T.out_cp);
        }
    }
#else
    {
        ssize_t r = write(T.out_fd, k_leave, sizeof(k_leave) - 1);
        BR_UNUSED(r);
        if (T.termios_saved) tcsetattr(T.in_fd, TCSAFLUSH, &T.saved_termios);
    }
#endif
}

/* stdout and stderr into a temporary file while the game runs (if they are the terminal). */
static void tm_capture_output(void) {
    int out_tty = TM_ISATTY(1), err_tty = TM_ISATTY(2);
    T.saved_stdout = T.saved_stderr = -1;
    if (!out_tty && !err_tty) return;
#if defined(_WIN32)
    {
        char dir[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, dir);
        if (n == 0 || n >= MAX_PATH || !GetTempFileNameA(dir, "bro", 0, T.log_path)) return;
        T.log_file = fopen(T.log_path, "w+b");
    }
#else
    T.log_file = tmpfile();
#endif
    if (!T.log_file) return;
    fflush(stdout);
    fflush(stderr);
    if (out_tty) { T.saved_stdout = TM_DUP(1); if (T.saved_stdout >= 0) TM_DUP2(TM_FILENO(T.log_file), 1); }
    if (err_tty) { T.saved_stderr = TM_DUP(2); if (T.saved_stderr >= 0) TM_DUP2(TM_FILENO(T.log_file), 2); }
}

/* ... and back, writing what was kept to the terminal. */
static void tm_release_output(void) {
    char buf[4096];
    size_t n;
    if (!T.log_file) return;
    fflush(stdout);
    fflush(stderr);
    if (T.saved_stdout >= 0) { TM_DUP2(T.saved_stdout, 1); TM_CLOSE(T.saved_stdout); }
    if (T.saved_stderr >= 0) { TM_DUP2(T.saved_stderr, 2); TM_CLOSE(T.saved_stderr); }
    T.saved_stdout = T.saved_stderr = -1;
    rewind(T.log_file);
    while ((n = fread(buf, 1, sizeof(buf), T.log_file)) > 0) fwrite(buf, 1, n, stderr);
    fflush(stderr);
    fclose(T.log_file);
    T.log_file = NULL;
    if (T.log_path[0]) { remove(T.log_path); T.log_path[0] = 0; }
}

#if !defined(_WIN32)
static void tm_signal(int sig) {
    tm_restore_terminal();
    signal(sig, SIG_DFL);
    raise(sig);
}
static const int k_signals[] = { SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
#endif

static bool g_tm_atexit;
static void tm_at_exit(void) {
    if (!T.active) return;
    tm_restore_terminal();
    tm_release_output();
}

static void tm_set_title(const char *title) {
    snprintf(T.title, sizeof(T.title), "%s", title ? title : "");
    if (!T.active) return;
    tm_puts("\x1b]0;");
    {
        const char *s;
        for (s = T.title; *s; ++s) if ((unsigned char)*s >= 0x20 && *s != 0x7f) tm_put(s, 1);
    }
    tm_puts("\x07");
    tm_flush();
}

static double tm_env_seconds(const char *name, double fallback) {
    const char *v = getenv(name);
    double d;
    if (!v || !v[0]) return fallback;
    d = atof(v);
    return d > 0 && d < 5 ? d : fallback;
}

static bool tm_detect_truecolor(void) {
    const char *force = getenv("BRONTO_TERMINAL_COLORS");
    if (force && force[0]) return strcmp(force, "256") != 0;
#if defined(_WIN32)
    return true;                                     /* Windows' console has had 24-bit colour since 2016 */
#else
    const char *ct = getenv("COLORTERM"), *tp = getenv("TERM_PROGRAM");
    if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"))) return true;
    if (tp && strcmp(tp, "Apple_Terminal") == 0) return false;   /* (Terminal.app before macOS 26 has 256) */
    {
        const char *term = getenv("TERM");
        if (term && (strstr(term, "kitty") || strstr(term, "direct") || strstr(term, "wezterm") || strstr(term, "foot") ||
                     strstr(term, "ghostty") || strstr(term, "alacritty")))
            return true;
    }
    return getenv("WT_SESSION") != NULL || (tp && (strcmp(tp, "iTerm.app") == 0 || strcmp(tp, "vscode") == 0 || strcmp(tp, "WezTerm") == 0));
#endif
}

static bool tm_init(const br_platform_config *cfg) {
    memset(T.held, 0, sizeof(T.held));
    memset(T.held_timed, 0, sizeof(T.held_timed));
    T.cfg = *cfg;
    T.kitty = false;
    T.in_len = 0;
    T.esc_since = -1;
    T.pending_count = 0;
    T.mouse_known = false;
    T.focused = true;
    T.high_surrogate = 0;
    T.time_origin = 0;
    T.time_origin = tm_time();
    T.first_hold = tm_env_seconds("BRONTO_TERMINAL_KEY_HOLD", 0.42);
    T.repeat_hold = T.first_hold < 0.15 ? T.first_hold : 0.15;
    T.truecolor = tm_detect_truecolor();
    T.cols = T.rows = 0;
#if defined(_WIN32)
    T.hin = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    T.hout = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (T.hin == INVALID_HANDLE_VALUE || T.hout == INVALID_HANDLE_VALUE || !GetConsoleMode(T.hin, &T.in_mode) || !GetConsoleMode(T.hout, &T.out_mode)) {
        if (T.hin != INVALID_HANDLE_VALUE) CloseHandle(T.hin);
        if (T.hout != INVALID_HANDLE_VALUE) CloseHandle(T.hout);
        BR_ERROR("terminal: there's no console to draw in");
        return false;
    }
    T.out_cp = GetConsoleOutputCP();
    T.modes_saved = true;
    if (!SetConsoleMode(T.hout, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN)) {
        BR_ERROR("terminal: this console doesn't understand escape sequences (Windows 10 1809 or later is needed)");
        CloseHandle(T.hin);
        CloseHandle(T.hout);
        return false;
    }
    SetConsoleMode(T.hin, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);   /* no quick edit, no line input */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(tm_ctrl_handler, TRUE);
    FlushConsoleInputBuffer(T.hin);
#else
    if (!T.test_fds) {
        T.in_fd = T.out_fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (T.in_fd < 0) { BR_ERROR("terminal: not started from a terminal (no /dev/tty)"); return false; }
        T.own_fd = true;
    }
    T.termios_saved = false;
    if (isatty(T.in_fd) && tcgetattr(T.in_fd, &T.saved_termios) == 0) {
        struct termios raw = T.saved_termios;
        raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(tcflag_t)OPOST;
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        T.termios_saved = tcsetattr(T.in_fd, TCSAFLUSH, &raw) == 0;
    } else if (!T.test_fds) {
        BR_ERROR("terminal: can't switch the terminal to raw input");
        close(T.in_fd);
        return false;
    }
    if (T.test_fds) fcntl(T.in_fd, F_SETFL, fcntl(T.in_fd, F_GETFL) | O_NONBLOCK);
    {
        size_t i;
        for (i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); ++i) signal(k_signals[i], tm_signal);
    }
#endif
    T.active = true;
    if (!g_tm_atexit) { g_tm_atexit = true; atexit(tm_at_exit); }
    tm_check_size();
    T.full_redraw = true;
#if !defined(_WIN32)
    if (!T.test_fds) tm_capture_output();
#else
    tm_capture_output();
#endif
    tm_puts(k_enter);
    tm_flush();
    if (cfg->title) tm_set_title(cfg->title);
    BR_INFO("terminal: %dx%d cells, %s, %s colour", T.cols, T.rows, T.style == BR_TERMINAL_TEXT ? "text" : "colour blocks",
            T.truecolor ? "24-bit" : "256");
    T.audio = cfg->audio_sample_rate > 0 && TM_AUDIO_OPEN(cfg->audio_sample_rate, cfg->audio_latency_ms);
    TM_PADS_INIT();
    return true;
}

static void tm_shutdown(void) {
    if (T.active) BR_INFO("terminal: %s", T.kitty ? "the kitty keyboard protocol reported key releases"
                                                : "keys were held until their repeats stopped (no kitty keyboard protocol)");
    if (T.audio) TM_AUDIO_CLOSE();
    T.audio = false;
    TM_PADS_SHUTDOWN();
    tm_restore_terminal();
    tm_release_output();
#if defined(_WIN32)
    SetConsoleCtrlHandler(tm_ctrl_handler, FALSE);
    CloseHandle(T.hin);
    CloseHandle(T.hout);
#else
    {
        size_t i;
        for (i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); ++i) signal(k_signals[i], SIG_DFL);
    }
    if (T.own_fd) close(T.in_fd);
    T.own_fd = false;
    T.test_fds = false;
#endif
    T.fixed_cols = T.fixed_rows = 0;
    br_free(T.pixels); T.pixels = NULL; T.pixels_cap = 0;
    br_free(T.cells); T.cells = NULL;
    br_free(T.prev); T.prev = NULL; T.cells_cap = 0;
    br_free(T.out); T.out = NULL; T.out_cap = T.out_len = 0;
    T.cols = T.rows = 0;
}

static uint32_t tm_audio_wanted(void) { return T.audio ? TM_AUDIO_WANTED() : 0; }
static void tm_audio_submit(const int16_t *d, uint32_t n) { if (T.audio) TM_AUDIO_SUBMIT(d, n); }
static void tm_get_window_size(int *w, int *h) { *w = T.cols; *h = T.rows * 2; }
static float tm_get_window_scale(void) { return 1.0f; }
static bool tm_rumble(int pad, float lo, float hi) { return TM_PADS_RUMBLE(pad, lo, hi); }

static const br_platform k_terminal = {
    "terminal", tm_init, tm_shutdown, tm_poll, tm_present, tm_time, tm_sleep,
    tm_audio_wanted, tm_audio_submit,
    tm_get_window_size, tm_get_window_scale, tm_set_title,
    NULL, NULL, NULL, NULL,                       /* window size, fullscreen, cursor, icon: the terminal's own */
    NULL, NULL, NULL,                             /* folder dialog, open, launch */
    NULL, NULL,                                   /* clipboard: the engine keeps a private one */
    tm_rumble,
    NULL,
    NULL,                                         /* present_overlay: the engine draws into the framebuffer */
    NULL                                          /* mouse lock */
};

const br_platform *br_platform_terminal(br_terminal_style style) {
    T.style = style == BR_TERMINAL_TEXT ? BR_TERMINAL_TEXT : BR_TERMINAL_BLOCKS;
#if defined(_WIN32)
    {
        DWORD mode;
        HANDLE h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        bool ok = h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (!ok) return NULL;
    }
#else
    if (!T.test_fds) {
        int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (fd < 0) return NULL;
        close(fd);
    }
#endif
    return &k_terminal;
}

bool br_terminal_attach_fds(int in_fd, int out_fd, int cols, int rows) {
#if defined(_WIN32)
    BR_UNUSED(in_fd); BR_UNUSED(out_fd); BR_UNUSED(cols); BR_UNUSED(rows);
    return false;
#else
    T.in_fd = in_fd;
    T.out_fd = out_fd;
    T.own_fd = false;
    T.test_fds = in_fd >= 0;
    T.fixed_cols = cols > 0 && rows > 0 ? cols : 0;
    T.fixed_rows = rows;
    return true;
#endif
}

#endif /* !__wasm__ */
