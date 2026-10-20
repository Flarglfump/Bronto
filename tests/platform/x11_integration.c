/*
 * X11 backend integration test (developer tool, Linux only).
 *
 * Runs the engine with the real X11 backend and drives it from a second
 * X connection: synthetic keyboard/mouse/close events, external resizes,
 * fullscreen, and pixel checks on what actually reached the window.
 *
 * Needs an X server and the Xlib headers (for the *driver* side only):
 *   make x11-test                     (uses xvfb-run if DISPLAY is unset)
 */
#include "bronto/bronto.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>

unsigned long br_x11_window_id(void);

static int g_checks, g_fail;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Display *drv;   /* the test's own connection */

/* ---- the game under test ---------------------------------------------------- */
typedef struct test_state { int closes, frames, texts; uint32_t last_text; bool veto; } test_state;
static test_state S;

static void render(br_scene *s) {
    br_renderer *r = &s->engine->renderer;
    int w = s->engine->framebuffer.width, h = s->engine->framebuffer.height;
    br_fill_rect(r, br_rect_make(0, 0, (float)w / 2, (float)h), br_rgb(255, 0, 0));
    br_fill_rect(r, br_rect_make((float)w / 2, 0, (float)w / 2, (float)h), br_rgb(0, 0, 255));
    br_draw_text(r, "X11", 4, 4, 1, BR_WHITE);
    S.frames++;
}
static bool on_event(br_scene *s, const br_event *e) {
    BR_UNUSED(s);
    if (e->type == BR_EVENT_WINDOW_CLOSE_REQUESTED) { S.closes++; return S.veto; }
    if (e->type == BR_EVENT_TEXT_INPUT) { S.texts++; S.last_text = e->u.text.codepoint; }
    return false;
}
static const br_scene_desc k_scene = { "x11test", 0, false, false, NULL, NULL, NULL, NULL, NULL, NULL, render, on_event };

/* ---- driver helpers ------------------------------------------------------------ */
static void pump(br_engine *e, int frames) {
    int i;
    for (i = 0; i < frames; ++i) { XSync(drv, False); br_engine_frame(e); }
}

static unsigned long pixel_at(Window w, int x, int y) {
    XImage *img = XGetImage(drv, w, x, y, 1, 1, AllPlanes, ZPixmap);
    unsigned long p;
    if (!img) return 0xDEADBEEF;
    p = XGetPixel(img, 0, 0) & 0xFFFFFF;
    XDestroyImage(img);
    return p;
}

static void send_key(Window w, KeySym ks, bool down, unsigned int state) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xkey.type = down ? KeyPress : KeyRelease;
    ev.xkey.display = drv;
    ev.xkey.window = w;
    ev.xkey.root = DefaultRootWindow(drv);
    ev.xkey.keycode = XKeysymToKeycode(drv, ks);
    ev.xkey.state = state;
    ev.xkey.same_screen = True;
    XSendEvent(drv, w, True, down ? KeyPressMask : KeyReleaseMask, &ev);
}

static void send_button(Window w, unsigned int button, bool down, int x, int y) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xbutton.type = down ? ButtonPress : ButtonRelease;
    ev.xbutton.display = drv;
    ev.xbutton.window = w;
    ev.xbutton.button = button;
    ev.xbutton.x = x;
    ev.xbutton.y = y;
    ev.xbutton.same_screen = True;
    XSendEvent(drv, w, True, down ? ButtonPressMask : ButtonReleaseMask, &ev);
}

static void send_motion(Window w, int x, int y) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xmotion.type = MotionNotify;
    ev.xmotion.display = drv;
    ev.xmotion.window = w;
    ev.xmotion.x = x;
    ev.xmotion.y = y;
    ev.xmotion.same_screen = True;
    XSendEvent(drv, w, True, PointerMotionMask, &ev);
}

static void send_close(Window w) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(drv, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)XInternAtom(drv, "WM_DELETE_WINDOW", False);
    XSendEvent(drv, w, False, NoEventMask, &ev);
}

static void save_window(Window w, int ww, int wh, const char *path) {
    XImage *img = XGetImage(drv, w, 0, 0, (unsigned)ww, (unsigned)wh, AllPlanes, ZPixmap);
    br_image out;
    int x, y;
    if (!img) return;
    br_image_init(&out, ww, wh);
    for (y = 0; y < wh; ++y)
        for (x = 0; x < ww; ++x) {
            unsigned long p = XGetPixel(img, x, y);
            br_image_set(&out, x, y, br_rgb((uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p));
        }
    br_image_save_bmp(&out, path);
    br_image_destroy(&out);
    XDestroyImage(img);
}

#define RED  0xFF0000ul
#define BLUE 0x0000FFul

int main(int argc, char **argv) {
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = br_engine_default_config();
    const char *out = argc > 1 ? argv[1] : ".";
    char path[512];
    Window w;
    char *name = NULL;

    drv = XOpenDisplay(NULL);
    if (!drv) { fprintf(stderr, "no X display\n"); return 2; }

    c.title = "Bronto X11 test";
    c.width = 160; c.height = 90; c.window_scale = 2;      /* 320x180 window */
    c.initial_scene = &k_scene;
    c.max_fps = 0;
    CHECK(br_engine_init(e, &c));
    CHECK(strcmp(e->platform->name, "x11") == 0);
    w = (Window)br_x11_window_id();
    pump(e, 5);

    /* window exists with our title and size */
    CHECK(XFetchName(drv, w, &name) && name && strcmp(name, "Bronto X11 test") == 0);
    if (name) XFree(name);

    /* icon: _NET_WM_ICON holds 16x16 first, then larger sizes up to the image's */
    {
        br_image icon;
        Atom type;
        int format;
        unsigned long count = 0, after;
        unsigned char *data = NULL;
        CHECK(br_image_init(&icon, 64, 64));
        br_image_fill(&icon, br_rgb(10, 200, 30));
        br_window_set_icon(e, &icon);
        br_image_destroy(&icon);
        pump(e, 1);
        XGetWindowProperty(drv, w, XInternAtom(drv, "_NET_WM_ICON", False), 0, 1 << 20, False, AnyPropertyType,
                           &type, &format, &count, &after, &data);
        CHECK(format == 32 && count == (2 + 256) + (2 + 576) + (2 + 1024) + (2 + 2304) + (2 + 4096));
        if (data) {
            const long *v = (const long *)data;
            CHECK(v[0] == 16 && v[1] == 16 && ((unsigned long)v[2] & 0xFFFFFFFFUL) == 0xFF0AC81EUL);   /* ARGB */
            XFree(data);
        }
    }
    CHECK(e->window.width == 320 && e->window.height == 180);
    /* pixels reached the screen: left half red, right half blue, 2x scaled */
    CHECK(pixel_at(w, 40, 120) == RED);
    CHECK(pixel_at(w, 280, 120) == BLUE);
    snprintf(path, sizeof(path), "%s/x11_initial.bmp", out);
    save_window(w, 320, 180, path);

    /* keyboard: 'a' with shift -> key A down + text 'A'; release */
    send_key(w, XK_a, true, ShiftMask);
    pump(e, 2);
    CHECK(br_key_down(&e->input, BR_KEY_A));
    CHECK(S.texts == 1 && S.last_text == 'A');
    send_key(w, XK_a, false, 0);
    send_key(w, XK_Escape, true, 0);
    pump(e, 2);
    CHECK(!br_key_down(&e->input, BR_KEY_A) && br_key_down(&e->input, BR_KEY_ESCAPE));
    send_key(w, XK_Escape, false, 0);
    send_key(w, XK_Left, true, 0);
    pump(e, 2);
    CHECK(br_key_down(&e->input, BR_KEY_LEFT));
    send_key(w, XK_Left, false, 0);

    /* mouse: window (100, 50) -> framebuffer (50, 25) at 2x */
    send_motion(w, 100, 50);
    send_button(w, 1, true, 100, 50);
    pump(e, 2);
    CHECK(br_mouse_down(&e->input, BR_MOUSE_LEFT));
    CHECK(e->input.mouse_x > 49.9f && e->input.mouse_x < 50.1f);
    CHECK(e->input.mouse_y > 24.9f && e->input.mouse_y < 25.1f);
    send_button(w, 1, false, 100, 50);
    send_button(w, 4, true, 100, 50);              /* wheel up */
    pump(e, 2);
    CHECK(!br_mouse_down(&e->input, BR_MOUSE_LEFT));

    /* external resize to a wide window -> pillarboxed viewport, black bars */
    XResizeWindow(drv, w, 640, 180);
    pump(e, 5);
    CHECK(e->window.width == 640 && e->window.height == 180);
    CHECK(e->window.viewport.rect.x == 160 && e->window.viewport.rect.w == 320);
    CHECK(pixel_at(w, 10, 90) == 0);               /* left bar */
    CHECK(pixel_at(w, 630, 90) == 0);              /* right bar */
    CHECK(pixel_at(w, 200, 90) == RED);
    CHECK(pixel_at(w, 440, 90) == BLUE);
    snprintf(path, sizeof(path), "%s/x11_pillarbox.bmp", out);
    save_window(w, 640, 180, path);

    /* engine-driven resize */
    br_window_set_size(e, 480, 270);
    pump(e, 5);
    CHECK(e->window.width == 480 && e->window.height == 270);
    CHECK(pixel_at(w, 100, 135) == RED && pixel_at(w, 380, 135) == BLUE);

    /* fullscreen (no WM under Xvfb: the backend covers the screen itself) */
    br_window_set_fullscreen(e, true);
    pump(e, 5);
    CHECK(br_window_is_fullscreen(e));
    CHECK(e->window.width == DisplayWidth(drv, DefaultScreen(drv)));
    CHECK(e->window.height == DisplayHeight(drv, DefaultScreen(drv)));
    br_window_set_fullscreen(e, false);
    pump(e, 5);
    CHECK(!br_window_is_fullscreen(e) && e->window.width == 480);

    /* cursor + title at run time */
    br_window_set_cursor_visible(e, false);
    br_window_set_cursor_visible(e, true);
    br_window_set_title(e, "Renamed \xc3\xa9");
    pump(e, 1);
    CHECK(XFetchName(drv, w, &name) && name && strncmp(name, "Renamed", 7) == 0);
    if (name) XFree(name);

    /* close button: vetoed once, then quits */
    S.veto = true;
    send_close(w);
    pump(e, 2);
    CHECK(S.closes == 1 && e->running);
    S.veto = false;
    send_close(w);
    XSync(drv, False);
    while (br_engine_frame(e)) {}
    CHECK(S.closes == 2 && !e->running);
    CHECK(S.frames > 30);

    br_engine_shutdown(e);
    br_free(e);
    XCloseDisplay(drv);
    printf("x11 integration: %d checks, %d failed -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
