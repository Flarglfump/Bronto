/* Tests: the terminal backend (platform_terminal.c) talking through pipes
 * instead of a terminal, and the project's renderer setting. */
#if !defined(_WIN32) && !defined(__wasm__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif
#include "test.h"
#include <stdlib.h>
#include <string.h>

static void test_project_renderer(void) {
    br_project p;
    int r = -1;
    br_project_defaults(&p, "t");
    CHECK_EQ_INT(p.renderer, BR_RENDERER_WINDOW);
    CHECK(br_project_parse("bronto_project 1\nrenderer = terminal_text\n", &p));
    CHECK_EQ_INT(p.renderer, BR_RENDERER_TERMINAL_TEXT);
    CHECK(br_project_parse("bronto_project 1\nrenderer = terminal\n", &p));
    CHECK_EQ_INT(p.renderer, BR_RENDERER_TERMINAL_BLOCKS);
    QUIET(CHECK(br_project_parse("bronto_project 1\nrenderer = hologram\n", &p)));
    CHECK_EQ_INT(p.renderer, BR_RENDERER_TERMINAL_BLOCKS);              /* unknown: unchanged */
    CHECK(br_project_renderer_parse("window", &r) && r == BR_RENDERER_WINDOW);
    CHECK(!br_project_renderer_parse("nope", &r));
    CHECK(strcmp(br_project_renderer_name(BR_RENDERER_TERMINAL_TEXT), "terminal_text") == 0);
}

#if !defined(_WIN32) && !defined(__wasm__)
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

static int g_in[2], g_out[2];
static char g_buf[65536];
static size_t g_len;

static void nap(double s) {
    struct timespec ts;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

/* What the backend wrote since last time (NUL-terminated in g_buf). */
static const char *drain(void) {
    ssize_t n;
    g_len = 0;
    while ((n = read(g_out[0], g_buf + g_len, sizeof(g_buf) - 1 - g_len)) > 0) g_len += (size_t)n;
    g_buf[g_len] = 0;
    return g_buf;
}

static void type(const char *bytes) {
    ssize_t w = write(g_in[1], bytes, strlen(bytes));
    BR_UNUSED(w);
}

static size_t count(const char *hay, const char *needle) {
    size_t n = 0, k = strlen(needle);
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) { ++n; p += k; }
    return n;
}

/* Polls once and finds the first event of `type` (copied to *out). */
static bool next_of(br_events *ev, uint32_t type, br_event *out) {
    br_event e;
    while (br_events_poll(ev, &e))
        if (e.type == type) { if (out) *out = e; return true; }
    return false;
}

static const br_platform *open_terminal(br_terminal_style style, int cols, int rows) {
    const br_platform *p;
    br_platform_config cfg;
    if (pipe(g_in) != 0 || pipe(g_out) != 0) return NULL;
    fcntl(g_out[0], F_SETFL, fcntl(g_out[0], F_GETFL) | O_NONBLOCK);
    if (!br_terminal_attach_fds(g_in[0], g_out[1], cols, rows)) return NULL;
    p = br_platform_terminal(style);
    memset(&cfg, 0, sizeof(cfg));
    cfg.title = "Test";
    if (!p || !p->init(&cfg)) return NULL;
    return p;
}

/* Shuts it down; what it wrote last is left in g_buf. */
static void close_terminal(const br_platform *p) {
    p->shutdown();
    drain();
    close(g_in[0]); close(g_in[1]); close(g_out[0]); close(g_out[1]);
}

static void fill_rows(br_image *img, int y0, int y1, br_color c) {
    int x, y;
    for (y = y0; y < y1; ++y)
        for (x = 0; x < img->width; ++x) img->pixels[(size_t)y * (size_t)img->width + (size_t)x] = c;
}

static void test_terminal_blocks(void) {
    const br_platform *p;
    br_image img;
    int w, h;
    const char *out;
    setenv("BRONTO_TERMINAL_COLORS", "truecolor", 1);
    p = open_terminal(BR_TERMINAL_BLOCKS, 20, 6);
    CHECK(p != NULL);
    if (!p) return;
    out = drain();
    CHECK(strstr(out, "\x1b[?1049h") != NULL);                    /* the alternate screen */
    CHECK(strstr(out, "\x1b]0;Test\x07") != NULL);                /* the title */
    p->get_window_size(&w, &h);
    CHECK_EQ_INT(w, 20);
    CHECK_EQ_INT(h, 12);                                          /* two pixels per cell, one above the other */
    CHECK(br_image_init(&img, 20, 12));
    fill_rows(&img, 0, 5, br_rgb(255, 0, 0));
    fill_rows(&img, 5, 12, br_rgb(0, 0, 255));
    p->present(&img, br_recti_make(0, 0, 20, 12));
    out = drain();
    /* cell row 2 is pixels 4 (red) and 5 (blue): upper half blocks, red over blue */
    CHECK(strstr(out, "\x1b[38;2;255;0;0;48;2;0;0;255m\xe2\x96\x80") != NULL);
    CHECK_EQ_INT(count(out, "\xe2\x96\x80"), 20);
    CHECK(strstr(out, "48;2;255;0;0m ") != NULL);                 /* solid rows: blank cells on their colour */
    CHECK(strstr(out, "\x1b[?2026h") != NULL && strstr(out, "\x1b[?2026l") != NULL);
    p->present(&img, br_recti_make(0, 0, 20, 12));               /* nothing changed: nothing sent */
    out = drain();
    CHECK(strcmp(out, "\x1b[?2026h\x1b[?2026l") == 0);
    img.pixels[11 * 20 + 7] = br_rgb(0, 255, 0);                 /* one pixel: one cell, at row 6 column 8 */
    p->present(&img, br_recti_make(0, 0, 20, 12));
    out = drain();
    CHECK(strstr(out, "\x1b[6;8H\x1b[38;2;0;0;255;48;2;0;255;0m\xe2\x96\x80") != NULL);
    CHECK_EQ_INT(count(out, "H"), 1);
    /* box filtering: a 40x24 framebuffer in a 20x12 viewport averages 2x2 squares */
    br_image_destroy(&img);
    CHECK(br_image_init(&img, 40, 24));
    {
        int x, y;
        for (y = 0; y < 24; ++y)
            for (x = 0; x < 40; ++x) img.pixels[(size_t)y * 40 + (size_t)x] = (x + y) % 2 ? br_rgb(200, 100, 0) : br_rgb(0, 0, 0);
    }
    p->present(&img, br_recti_make(0, 0, 20, 12));
    out = drain();
    CHECK(strstr(out, "48;2;100;50;0m ") != NULL);
    /* letterboxing: outside the viewport is black */
    p->present(&img, br_recti_make(5, 0, 10, 12));
    out = drain();
    CHECK(strstr(out, "48;2;0;0;0m") != NULL);
    br_image_destroy(&img);
    close_terminal(p);
    CHECK(strstr(g_buf, "\x1b[?1049l") != NULL);                /* the terminal is given back */
}

static void test_terminal_text_and_256(void) {
    const br_platform *p;
    br_image img;
    const char *out;
    setenv("BRONTO_TERMINAL_COLORS", "256", 1);
    p = open_terminal(BR_TERMINAL_TEXT, 8, 2);
    CHECK(p != NULL);
    if (!p) { unsetenv("BRONTO_TERMINAL_COLORS"); return; }
    drain();
    CHECK(br_image_init(&img, 8, 4));
    fill_rows(&img, 0, 2, br_rgb(255, 0, 0));                    /* row 1: red; row 2: dark red */
    fill_rows(&img, 2, 4, br_rgb(30, 0, 0));
    p->present(&img, br_recti_make(0, 0, 8, 4));
    out = drain();
    CHECK(strstr(out, "38;5;196") != NULL);                      /* pure red is palette 196 */
    CHECK_EQ_INT(count(out, ":"), 8);                             /* pure red is dim (30% luminance): ':' */
    CHECK(strstr(out, "@") == NULL);
    CHECK(strstr(out, "\xe2\x96\x80") == NULL);                  /* no blocks in text */
    fill_rows(&img, 0, 4, br_rgb(255, 255, 255));
    p->present(&img, br_recti_make(0, 0, 8, 4));
    out = drain();
    CHECK_EQ_INT(count(out, "@"), 16);                            /* white: the densest character */
    br_image_destroy(&img);
    close_terminal(p);
    unsetenv("BRONTO_TERMINAL_COLORS");
}

static void test_terminal_input(void) {
    const br_platform *p;
    br_events ev;
    br_event e;
    setenv("BRONTO_TERMINAL_KEY_HOLD", "0.05", 1);
    p = open_terminal(BR_TERMINAL_BLOCKS, 20, 6);
    CHECK(p != NULL);
    if (!p) { unsetenv("BRONTO_TERMINAL_KEY_HOLD"); return; }
    CHECK(br_events_init(&ev, 64));
    drain();

    /* Plain terminals: a key is held until its repeats stop. */
    type("a");
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_A && !e.u.key.repeat);
    p->poll_events(&ev);
    CHECK(!next_of(&ev, BR_EVENT_KEY_UP, NULL));
    type("a");                                                    /* autorepeat */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.repeat);
    nap(0.12);
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_UP, &e) && e.u.key.key == BR_KEY_A);

    type("Q");                                                    /* Shift: the key, and the text typed */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_Q && (e.u.key.mods & BR_MOD_SHIFT));
    br_events_clear(&ev);
    type("Q");
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_TEXT_INPUT, &e) && e.u.text.codepoint == 'Q');
    br_events_clear(&ev);

    type("\x1b[A\x1b[1;5C\x1bOP\x1b[15~");                        /* up, Ctrl+right, F1, F5 */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_UP);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_RIGHT && (e.u.key.mods & BR_MOD_CONTROL));
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_F1);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_F5);
    br_events_clear(&ev);

    type("\x1b");                                                 /* a lone ESC waits a moment: it may start a sequence */
    p->poll_events(&ev);
    CHECK(!next_of(&ev, BR_EVENT_KEY_DOWN, NULL));
    nap(0.05);
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_ESCAPE);
    br_events_clear(&ev);

    type("\x1b[<0;5;3M");                                         /* the mouse: cell (5, 3) is pixel (4.5, 5) */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_MOUSE_MOVE, &e) && e.u.mouse_move.x == 4.5f && e.u.mouse_move.y == 5.0f);
    CHECK(next_of(&ev, BR_EVENT_MOUSE_BUTTON_DOWN, &e) && e.u.mouse_button.button == BR_MOUSE_LEFT);
    type("\x1b[<0;5;3m\x1b[<65;5;3M");
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_MOUSE_BUTTON_UP, &e) && e.u.mouse_button.button == BR_MOUSE_LEFT);
    CHECK(next_of(&ev, BR_EVENT_MOUSE_WHEEL, &e) && e.u.wheel.dy < 0);
    br_events_clear(&ev);

    type("\x1b[O");                                               /* focus lost */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_WINDOW_FOCUS_LOST, NULL));
    type("\x1b[I");
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_WINDOW_FOCUS_GAINED, NULL));
    br_events_clear(&ev);

    /* The kitty keyboard protocol: the terminal answers the query, then reports releases. */
    type("\x1b[?31u\x1b[97;1:1u");
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_A);
    nap(0.12);
    p->poll_events(&ev);
    CHECK(!next_of(&ev, BR_EVENT_KEY_UP, NULL));                  /* still held: no timer now */
    type("\x1b[97;1:3u");
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_KEY_UP, &e) && e.u.key.key == BR_KEY_A);
    type("\x1b[97:65;2;65u\x1b[57441;2:1u\x1b[1;1:3D");          /* Shift+a typing "A", left Shift down, Left up */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_TEXT_INPUT, &e) && e.u.text.codepoint == 'A');
    CHECK(next_of(&ev, BR_EVENT_KEY_DOWN, &e) && e.u.key.key == BR_KEY_LEFT_SHIFT);
    br_events_clear(&ev);

    type("\x03");                                                 /* Ctrl+C quits */
    p->poll_events(&ev);
    CHECK(next_of(&ev, BR_EVENT_QUIT, NULL));

    br_events_destroy(&ev);
    close_terminal(p);
    CHECK(strstr(g_buf, "\x1b[?1049l") != NULL);
    unsetenv("BRONTO_TERMINAL_KEY_HOLD");
}

/* The engine on the terminal backend: it sizes the window from the terminal. */
static void test_terminal_engine(void) {
    br_engine *e;
    br_engine_config cfg = br_engine_default_config();
    const br_platform *p;
    if (pipe(g_in) != 0 || pipe(g_out) != 0) { CHECK(false); return; }
    fcntl(g_out[0], F_SETFL, fcntl(g_out[0], F_GETFL) | O_NONBLOCK);
    CHECK(br_terminal_attach_fds(g_in[0], g_out[1], 32, 9));
    p = br_platform_terminal(BR_TERMINAL_BLOCKS);
    CHECK(p != NULL);
    if (!p) return;
    cfg.platform = p;
    cfg.width = 64;
    cfg.height = 36;
    cfg.audio_sample_rate = 0;
    cfg.max_fps = 0;
    e = BR_NEW(br_engine);
    CHECK(e && br_engine_init(e, &cfg));
    if (e && e->platform) {
        int i;
        for (i = 0; i < 3; ++i) { br_engine_frame(e); drain(); }
        CHECK_EQ_INT(e->window.width, 32);
        CHECK_EQ_INT(e->window.height, 18);
        CHECK_EQ_INT(e->window.viewport.rect.w, 32);           /* 64x36 fits 32x18 exactly */
        CHECK_EQ_INT(e->window.viewport.rect.h, 18);
        br_engine_shutdown(e);
    }
    br_free(e);
    close(g_in[0]); close(g_in[1]); close(g_out[0]); close(g_out[1]);
}
#endif

void suite_terminal(void) {
    RUN(test_project_renderer);
#if !defined(_WIN32) && !defined(__wasm__)
    RUN(test_terminal_blocks);
    RUN(test_terminal_text_and_256);
    RUN(test_terminal_input);
    RUN(test_terminal_engine);
#endif
}
