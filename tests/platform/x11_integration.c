/*
 * X11 backend integration test (developer tool, Linux only).
 *
 * Runs the engine with the real X11 backend and drives it from a second
 * X connection: synthetic keyboard/mouse/close events, external resizes,
 * fullscreen, pixel checks on what actually reached the window, and the
 * clipboard both ways (another "program" is a forked child with its own
 * connection).
 *
 * Needs an X server and the Xlib headers (for the *driver* side only):
 *   make x11-test                     (uses xvfb-run if DISPLAY is unset)
 */
#define _POSIX_C_SOURCE 200809L
#include "bronto/bronto.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

/* ---- another program that has copied some text --------------------------------- */
#define OTHER_TEXT "from another app \xe2\x9c\x93"

static void other_program(int ready_fd) {
    Display *d = XOpenDisplay(NULL);
    Window win;
    Atom clip, utf8;
    if (!d) _exit(1);
    win = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    clip = XInternAtom(d, "CLIPBOARD", False);
    utf8 = XInternAtom(d, "UTF8_STRING", False);
    XSetSelectionOwner(d, clip, win, CurrentTime);
    XSync(d, False);
    if (write(ready_fd, "r", 1) != 1) _exit(1);
    for (;;) {                                   /* serve it until someone else copies */
        XEvent ev, re;
        XNextEvent(d, &ev);
        if (ev.type == SelectionClear) break;
        if (ev.type != SelectionRequest) continue;
        memset(&re, 0, sizeof(re));
        re.xselection.type = SelectionNotify;
        re.xselection.requestor = ev.xselectionrequest.requestor;
        re.xselection.selection = ev.xselectionrequest.selection;
        re.xselection.target = ev.xselectionrequest.target;
        re.xselection.time = ev.xselectionrequest.time;
        re.xselection.property = None;
        if (ev.xselectionrequest.target == utf8) {
            XChangeProperty(d, ev.xselectionrequest.requestor, ev.xselectionrequest.property, utf8, 8, PropModeReplace,
                            (const unsigned char *)OTHER_TEXT, (int)strlen(OTHER_TEXT));
            re.xselection.property = ev.xselectionrequest.property;
        }
        XSendEvent(d, ev.xselectionrequest.requestor, False, 0, &re);
        XFlush(d);
    }
    XCloseDisplay(d);
    _exit(0);
}

static void test_clipboard(br_engine *e) {
    Window dw = XCreateSimpleWindow(drv, DefaultRootWindow(drv), 0, 0, 1, 1, 0, 0, 0);
    Atom clip = XInternAtom(drv, "CLIPBOARD", False), utf8 = XInternAtom(drv, "UTF8_STRING", False);
    Atom prop = XInternAtom(drv, "DRIVER_PASTE", False);
    XEvent ev;
    bool answered = false;
    char *text;
    int i, fds[2], status = 0;
    pid_t child;

    /* copied in the engine, pasted elsewhere: served from the engine's event loop */
    CHECK(br_clipboard_set(e, "copied \xc3\xa9\nline 2"));
    CHECK(XGetSelectionOwner(drv, clip) == (Window)br_x11_window_id());
    XConvertSelection(drv, clip, utf8, prop, dw, CurrentTime);
    for (i = 0; i < 200 && !answered; ++i) {
        pump(e, 1);
        answered = XCheckTypedWindowEvent(drv, dw, SelectionNotify, &ev);
    }
    CHECK(answered && ev.xselection.property == prop);
    if (answered && ev.xselection.property == prop) {
        Atom type;
        int format;
        unsigned long n, after;
        unsigned char *data = NULL;
        XGetWindowProperty(drv, dw, prop, 0, 1024, True, AnyPropertyType, &type, &format, &n, &after, &data);
        CHECK(type == utf8 && format == 8 && n == 16 && data && memcmp(data, "copied \xc3\xa9\nline 2", 16) == 0);
        if (data) XFree(data);
    }
    text = br_clipboard_get(e);                  /* our own: straight from memory */
    CHECK(text && strcmp(text, "copied \xc3\xa9\nline 2") == 0);
    br_free(text);

    /* copied in another program, pasted in the engine */
    if (pipe(fds) != 0) { CHECK(!"pipe"); return; }
    child = fork();
    if (child == 0) { close(fds[0]); other_program(fds[1]); }
    close(fds[1]);
    {
        char r = 0;
        CHECK(read(fds[0], &r, 1) == 1 && r == 'r');
        close(fds[0]);
    }
    text = br_clipboard_get(e);
    CHECK(text && strcmp(text, OTHER_TEXT) == 0);
    br_free(text);
    CHECK(br_clipboard_set(e, "mine again"));   /* the other program loses it, and ends */
    for (i = 0; i < 200 && waitpid(child, &status, WNOHANG) == 0; ++i) {
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, NULL);
    }
    if (i == 200) { kill(child, SIGKILL); waitpid(child, &status, 0); CHECK(!"the other program didn't see the copy"); }
    text = br_clipboard_get(e);
    CHECK(text && strcmp(text, "mine again") == 0);
    br_free(text);
    XDestroyWindow(drv, dw);
}

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

    test_clipboard(e);

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
