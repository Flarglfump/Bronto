/*
 * Linux / BSD backend: X11 (also works under XWayland).
 *
 * libX11 is loaded with dlopen() at run time and the handful of Xlib types
 * we need are declared below, so building Bronto needs no X11 development
 * headers and no -lX11 - only a C compiler. (tests/x11_layout_check.c
 * verifies these declarations against the real <X11/Xlib.h>.)
 *
 * Presentation: the framebuffer is scaled in software into an XImage the
 * size of the window and sent with XPutImage. Audio: ALSA (audio_alsa.c).
 * Gamepads: evdev (gamepad_evdev.c).
 *
 * Included by platform_native.c, which sets the POSIX feature macros.
 */
#include "bronto/platform.h"
#include "bronto/log.h"
#include "bronto/memory.h"

#include <dlfcn.h>
#include <string.h>
#include <time.h>

#include "audio_alsa.c"
#include "gamepad_evdev.c"

/* ==== Minimal Xlib declarations (ABI-compatible with <X11/Xlib.h>) ===== */
typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;
typedef XID Pixmap;
typedef XID Cursor;
typedef XID Drawable;
typedef unsigned long Atom;
typedef unsigned long KeySym;
typedef unsigned long Time;
typedef int Bool;
typedef int Status;
typedef struct _XGC *GC;
typedef char *XPointer;

typedef struct {
    void *ext_data;
    unsigned long visualid;
    int c_class;
    unsigned long red_mask, green_mask, blue_mask;
    int bits_per_rgb;
    int map_entries;
} Visual;

typedef struct _XImage {
    int width, height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask, green_mask, blue_mask;
    XPointer obdata;
    struct {
        struct _XImage *(*create_image)(void);
        int (*destroy_image)(struct _XImage *);
        unsigned long (*get_pixel)(void);
        int (*put_pixel)(void);
        struct _XImage *(*sub_image)(void);
        int (*add_pixel)(void);
    } f;
} XImage;

typedef struct { int type; unsigned long serial; Bool send_event; Display *display; Window window; } XAnyEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; unsigned int state; unsigned int keycode; Bool same_screen;
} XKeyEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; unsigned int state; unsigned int button; Bool same_screen;
} XButtonEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; unsigned int state; char is_hint; Bool same_screen;
} XMotionEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window;
    Window root, subwindow; Time time; int x, y, x_root, y_root; int mode; int detail; Bool same_screen; Bool focus; unsigned int state;
} XCrossingEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window; int mode; int detail;
} XFocusChangeEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window event; Window window;
    int x, y, width, height, border_width; Window above; Bool override_redirect;
} XConfigureEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window;
    Atom message_type; int format; union { char b[20]; short s[10]; long l[5]; } data;
} XClientMessageEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window; Atom atom; Time time; int state;
} XPropertyEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window owner, requestor;
    Atom selection, target, property; Time time;
} XSelectionRequestEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window requestor;
    Atom selection, target, property; Time time;
} XSelectionEvent;
typedef struct {
    int type; unsigned long serial; Bool send_event; Display *display; Window window; Atom selection; Time time;
} XSelectionClearEvent;
typedef union _XEvent {
    int type;
    XAnyEvent xany;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XCrossingEvent xcrossing;
    XFocusChangeEvent xfocus;
    XConfigureEvent xconfigure;
    XClientMessageEvent xclient;
    XPropertyEvent xproperty;
    XSelectionRequestEvent xselectionrequest;
    XSelectionEvent xselection;
    XSelectionClearEvent xselectionclear;
    long pad[24];
} XEvent;

typedef struct {
    long flags;
    int x, y, width, height;
    int min_width, min_height, max_width, max_height;
    int width_inc, height_inc;
    struct { int x, y; } min_aspect, max_aspect;
    int base_width, base_height;
    int win_gravity;
} XSizeHints;

typedef struct { unsigned long pixel; unsigned short red, green, blue; char flags; char pad; } XColor;
typedef struct { int type; Display *display; XID resourceid; unsigned long serial;
                 unsigned char error_code, request_code, minor_code; } XErrorEvent;

#define X_KeyPress 2
#define X_KeyRelease 3
#define X_ButtonPress 4
#define X_ButtonRelease 5
#define X_MotionNotify 6
#define X_EnterNotify 7
#define X_LeaveNotify 8
#define X_FocusIn 9
#define X_FocusOut 10
#define X_UnmapNotify 18
#define X_MapNotify 19
#define X_ConfigureNotify 22
#define X_PropertyNotify 28
#define X_SelectionClear 29
#define X_SelectionRequest 30
#define X_SelectionNotify 31
#define X_ClientMessage 33

#define X_KeyPressMask (1L << 0)
#define X_KeyReleaseMask (1L << 1)
#define X_ButtonPressMask (1L << 2)
#define X_ButtonReleaseMask (1L << 3)
#define X_EnterWindowMask (1L << 4)
#define X_LeaveWindowMask (1L << 5)
#define X_PointerMotionMask (1L << 6)
#define X_ExposureMask (1L << 15)
#define X_StructureNotifyMask (1L << 17)
#define X_SubstructureNotifyMask (1L << 19)
#define X_SubstructureRedirectMask (1L << 20)
#define X_FocusChangeMask (1L << 21)
#define X_PropertyChangeMask (1L << 22)

#define X_ShiftMask (1u << 0)
#define X_LockMask (1u << 1)
#define X_ControlMask (1u << 2)
#define X_Mod1Mask (1u << 3)
#define X_Mod4Mask (1u << 6)

#define X_ZPixmap 2
#define X_PropModeReplace 0
#define X_XA_ATOM ((Atom)4)
#define X_XA_STRING ((Atom)31)
#define X_PMinSize (1L << 4)
#define X_PMaxSize (1L << 5)
#define X_TrueColor 4

/* ==== dynamically loaded entry points ================================== */
#define X11_FUNCS(F) \
    F(Display *, XOpenDisplay, (const char *)) \
    F(int, XCloseDisplay, (Display *)) \
    F(int, XDefaultScreen, (Display *)) \
    F(Window, XRootWindow, (Display *, int)) \
    F(Visual *, XDefaultVisual, (Display *, int)) \
    F(int, XDefaultDepth, (Display *, int)) \
    F(unsigned long, XBlackPixel, (Display *, int)) \
    F(int, XDisplayWidth, (Display *, int)) \
    F(int, XDisplayHeight, (Display *, int)) \
    F(Window, XCreateSimpleWindow, (Display *, Window, int, int, unsigned int, unsigned int, unsigned int, unsigned long, unsigned long)) \
    F(int, XDestroyWindow, (Display *, Window)) \
    F(int, XSelectInput, (Display *, Window, long)) \
    F(int, XMapWindow, (Display *, Window)) \
    F(int, XStoreName, (Display *, Window, const char *)) \
    F(Atom, XInternAtom, (Display *, const char *, Bool)) \
    F(Status, XSetWMProtocols, (Display *, Window, Atom *, int)) \
    F(int, XChangeProperty, (Display *, Window, Atom, Atom, int, int, const unsigned char *, int)) \
    F(int, XGetWindowProperty, (Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *, unsigned long *, unsigned long *, unsigned char **)) \
    F(void, XSetWMNormalHints, (Display *, Window, XSizeHints *)) \
    F(int, XPending, (Display *)) \
    F(int, XNextEvent, (Display *, XEvent *)) \
    F(Status, XSendEvent, (Display *, Window, Bool, long, XEvent *)) \
    F(KeySym, XLookupKeysym, (XKeyEvent *, int)) \
    F(int, XLookupString, (XKeyEvent *, char *, int, KeySym *, void *)) \
    F(GC, XCreateGC, (Display *, Drawable, unsigned long, void *)) \
    F(int, XFreeGC, (Display *, GC)) \
    F(XImage *, XCreateImage, (Display *, Visual *, unsigned int, int, int, char *, unsigned int, unsigned int, int, int)) \
    F(int, XPutImage, (Display *, Drawable, GC, XImage *, int, int, int, int, unsigned int, unsigned int)) \
    F(int, XFlush, (Display *)) \
    F(int, XSync, (Display *, Bool)) \
    F(int, XFree, (void *)) \
    F(int, XResizeWindow, (Display *, Window, unsigned int, unsigned int)) \
    F(int, XMoveResizeWindow, (Display *, Window, int, int, unsigned int, unsigned int)) \
    F(Pixmap, XCreateBitmapFromData, (Display *, Drawable, const char *, unsigned int, unsigned int)) \
    F(Cursor, XCreatePixmapCursor, (Display *, Pixmap, Pixmap, XColor *, XColor *, unsigned int, unsigned int)) \
    F(int, XFreePixmap, (Display *, Pixmap)) \
    F(int, XDefineCursor, (Display *, Window, Cursor)) \
    F(int, XUndefineCursor, (Display *, Window)) \
    F(int, XFreeCursor, (Display *, Cursor)) \
    F(void *, XSetErrorHandler, (int (*)(Display *, XErrorEvent *))) \
    F(int, XSetSelectionOwner, (Display *, Atom, Window, Time)) \
    F(Window, XGetSelectionOwner, (Display *, Atom)) \
    F(int, XConvertSelection, (Display *, Atom, Atom, Atom, Window, Time)) \
    F(Bool, XCheckTypedWindowEvent, (Display *, Window, int, XEvent *)) \
    F(int, XDeleteProperty, (Display *, Window, Atom))

#define X11_DECLARE(ret, name, args) typedef ret (*name##_fn) args; static name##_fn p##name;
X11_FUNCS(X11_DECLARE)
typedef Bool (*XkbSetDetectableAutoRepeat_fn)(Display *, Bool, Bool *);
static XkbSetDetectableAutoRepeat_fn pXkbSetDetectableAutoRepeat;

/* ==== state ============================================================== */
static struct {
    void *lib;
    Display *dpy;
    int screen;
    Window root, win;
    Visual *visual;
    int depth;
    GC gc;
    XImage *image;
    uint32_t *pixels;
    int rshift, gshift, bshift;
    Atom wm_delete, wm_protocols, net_wm_state, net_wm_state_fullscreen, net_wm_state_hidden,
         net_wm_state_max_v, net_wm_state_max_h, net_wm_name, utf8_string,
         clipboard, targets, text_atom, incr, paste_property;
    char *clip_text;              /* what we put on the clipboard (we serve it while we own it) */
    size_t clip_len;
    bool has_wm;
    int width, height;            /* client size, pixels */
    int saved_x, saved_y, saved_w, saved_h;
    bool fullscreen, minimized, maximized, mapped, hidden;
    bool cursor_hidden;
    Cursor blank_cursor;
    float last_mx, last_my;
    bool key_down[256];
    br_events *events;
    br_platform_config cfg;
} X;

static int x11_error_handler(Display *d, XErrorEvent *e) {
    BR_UNUSED(d);
    BR_WARN("X11 error: code %u (request %u.%u)", e->error_code, e->request_code, e->minor_code);
    return 0;
}

static bool load_x11(void) {
    const char *names[] = { "libX11.so.6", "libX11.so" };
    size_t i;
    for (i = 0; i < BR_ARRAY_COUNT(names) && !X.lib; ++i) X.lib = dlopen(names[i], RTLD_LAZY | RTLD_LOCAL);
    if (!X.lib) { BR_ERROR("X11: could not load libX11 (%s)", dlerror()); return false; }
    /* dlsym returns void*; converting through a union avoids the ISO C
     * object-to-function-pointer warning. */
#define X11_LOAD(ret, name, args) \
    { union { void *o; name##_fn f; } u_; u_.o = dlsym(X.lib, #name); p##name = u_.f; \
      if (!p##name) { BR_ERROR("X11: missing symbol %s", #name); return false; } }
    X11_FUNCS(X11_LOAD)
#undef X11_LOAD
    {
        union { void *o; XkbSetDetectableAutoRepeat_fn f; } u;
        u.o = dlsym(X.lib, "XkbSetDetectableAutoRepeat");
        pXkbSetDetectableAutoRepeat = u.f;   /* optional */
    }
    return true;
}

static int x11_mask_shift(unsigned long m) {
    int s = 0;
    if (!m) return 0;
    while (!(m & 1ul)) { m >>= 1; ++s; }
    return s;
}

static void x11_post(br_event *e) { if (X.events) br_events_post(X.events, e); }

static void x11_post_simple(uint32_t type) { br_event e = br_event_make(type); x11_post(&e); }

/* ==== keyboard ============================================================= */
static br_key translate_keysym(KeySym ks) {
    if (ks >= 'a' && ks <= 'z') return (br_key)(BR_KEY_A + (int)(ks - 'a'));
    if (ks >= 'A' && ks <= 'Z') return (br_key)(BR_KEY_A + (int)(ks - 'A'));
    if (ks >= '0' && ks <= '9') return (br_key)(BR_KEY_0 + (int)(ks - '0'));
    switch (ks) {
    case ' ': return BR_KEY_SPACE;
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
    case 0xff1b: return BR_KEY_ESCAPE;
    case 0xff0d: return BR_KEY_ENTER;
    case 0xff09: case 0xfe20: return BR_KEY_TAB;   /* Tab, ISO_Left_Tab */
    case 0xff08: return BR_KEY_BACKSPACE;
    case 0xff63: return BR_KEY_INSERT;
    case 0xffff: return BR_KEY_DELETE;
    case 0xff53: return BR_KEY_RIGHT;
    case 0xff51: return BR_KEY_LEFT;
    case 0xff54: return BR_KEY_DOWN;
    case 0xff52: return BR_KEY_UP;
    case 0xff55: return BR_KEY_PAGE_UP;
    case 0xff56: return BR_KEY_PAGE_DOWN;
    case 0xff50: return BR_KEY_HOME;
    case 0xff57: return BR_KEY_END;
    case 0xffe5: return BR_KEY_CAPS_LOCK;
    case 0xff14: return BR_KEY_SCROLL_LOCK;
    case 0xff7f: return BR_KEY_NUM_LOCK;
    case 0xff61: return BR_KEY_PRINT_SCREEN;
    case 0xff13: return BR_KEY_PAUSE;
    case 0xffae: case 0xff9f: return BR_KEY_KP_DECIMAL;
    case 0xffaf: return BR_KEY_KP_DIVIDE;
    case 0xffaa: return BR_KEY_KP_MULTIPLY;
    case 0xffad: return BR_KEY_KP_SUBTRACT;
    case 0xffab: return BR_KEY_KP_ADD;
    case 0xff8d: return BR_KEY_KP_ENTER;
    case 0xffbd: return BR_KEY_KP_EQUAL;
    case 0xff9e: return BR_KEY_KP_0;
    case 0xff9c: return BR_KEY_KP_1;
    case 0xff99: return BR_KEY_KP_2;
    case 0xff9b: return BR_KEY_KP_3;
    case 0xff96: return BR_KEY_KP_4;
    case 0xff9d: return BR_KEY_KP_5;
    case 0xff98: return BR_KEY_KP_6;
    case 0xff95: return BR_KEY_KP_7;
    case 0xff97: return BR_KEY_KP_8;
    case 0xff9a: return BR_KEY_KP_9;
    case 0xffe1: return BR_KEY_LEFT_SHIFT;
    case 0xffe2: return BR_KEY_RIGHT_SHIFT;
    case 0xffe3: return BR_KEY_LEFT_CONTROL;
    case 0xffe4: return BR_KEY_RIGHT_CONTROL;
    case 0xffe9: return BR_KEY_LEFT_ALT;
    case 0xffea: case 0xfe03: return BR_KEY_RIGHT_ALT;  /* Alt_R, ISO_Level3_Shift (AltGr) */
    case 0xffeb: return BR_KEY_LEFT_SUPER;
    case 0xffec: return BR_KEY_RIGHT_SUPER;
    case 0xff67: return BR_KEY_MENU;
    default: break;
    }
    if (ks >= 0xffb0 && ks <= 0xffb9) return (br_key)(BR_KEY_KP_0 + (int)(ks - 0xffb0));
    if (ks >= 0xffbe && ks <= 0xffc9) return (br_key)(BR_KEY_F1 + (int)(ks - 0xffbe));
    return BR_KEY_UNKNOWN;
}

static uint32_t translate_mods(unsigned int state) {
    uint32_t m = 0;
    if (state & X_ShiftMask) m |= BR_MOD_SHIFT;
    if (state & X_ControlMask) m |= BR_MOD_CONTROL;
    if (state & X_Mod1Mask) m |= BR_MOD_ALT;
    if (state & X_Mod4Mask) m |= BR_MOD_SUPER;
    if (state & X_LockMask) m |= BR_MOD_CAPS_LOCK;
    return m;
}

static uint32_t keysym_to_codepoint(KeySym ks) {
    if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff)) return (uint32_t)ks;
    if (ks >= 0x01000100ul && ks <= 0x0110ffffull) return (uint32_t)(ks - 0x01000000ul);
    return 0;
}

static void handle_key(XKeyEvent *ke, bool down) {
    KeySym ks = pXLookupKeysym(ke, 0);
    br_key key = translate_keysym(ks);
    unsigned int kc = ke->keycode & 0xffu;
    bool repeat = down && X.key_down[kc];
    br_event e = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, key, translate_mods(ke->state), repeat);
    e.u.key.scancode = (int)ke->keycode;
    X.key_down[kc] = down;
    x11_post(&e);
    if (down && !(ke->state & (X_ControlMask | X_Mod1Mask))) {
        char buf[32];
        KeySym shifted = 0;
        int n = pXLookupString(ke, buf, (int)sizeof(buf), &shifted, NULL);
        uint32_t cp = keysym_to_codepoint(shifted);
        if (!cp && n == 1 && (unsigned char)buf[0] >= 0x20 && (unsigned char)buf[0] != 0x7f) cp = (unsigned char)buf[0];
        if (cp) {
            br_event t = br_event_make(BR_EVENT_TEXT_INPUT);
            t.u.text.codepoint = cp;
            x11_post(&t);
        }
    }
}

/* ==== window state ========================================================= */
static bool atom_list_has(const Atom *atoms, unsigned long n, Atom a) {
    unsigned long i;
    for (i = 0; i < n; ++i) if (atoms[i] == a) return true;
    return false;
}

static void read_wm_state(void) {
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char *data = NULL;
    bool fs = false, hidden = false, maxed = false;
    if (pXGetWindowProperty(X.dpy, X.win, X.net_wm_state, 0, 64, 0, X_XA_ATOM, &type, &format, &n, &after, &data) == 0 && data) {
        const Atom *atoms = (const Atom *)(void *)data;
        fs = atom_list_has(atoms, n, X.net_wm_state_fullscreen);
        hidden = atom_list_has(atoms, n, X.net_wm_state_hidden);
        maxed = atom_list_has(atoms, n, X.net_wm_state_max_v) && atom_list_has(atoms, n, X.net_wm_state_max_h);
        pXFree(data);
    }
    if (fs != X.fullscreen) {
        br_event e = br_event_make(BR_EVENT_WINDOW_FULLSCREEN);
        X.fullscreen = fs;
        e.u.window.fullscreen = fs;
        x11_post(&e);
    }
    if (hidden != X.minimized) {
        X.minimized = hidden;
        x11_post_simple(hidden ? BR_EVENT_WINDOW_MINIMIZED : BR_EVENT_WINDOW_RESTORED);
    }
    if (maxed != X.maximized) {
        X.maximized = maxed;
        if (!X.minimized) x11_post_simple(maxed ? BR_EVENT_WINDOW_MAXIMIZED : BR_EVENT_WINDOW_RESTORED);
    }
}

static bool detect_wm(void) {
    Atom check = pXInternAtom(X.dpy, "_NET_SUPPORTING_WM_CHECK", 0), type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char *data = NULL;
    bool found = false;
    if (pXGetWindowProperty(X.dpy, X.root, check, 0, 1, 0, 0 /* AnyPropertyType */, &type, &format, &n, &after, &data) == 0 && data) {
        found = n > 0;
        pXFree(data);
    }
    return found;
}

static void apply_size_hints(void) {
    XSizeHints h;
    memset(&h, 0, sizeof(h));
    if (!X.cfg.resizable && !X.fullscreen) {
        h.flags = X_PMinSize | X_PMaxSize;
        h.min_width = h.max_width = X.width;
        h.min_height = h.max_height = X.height;
    } else if (X.cfg.min_width > 0 && X.cfg.min_height > 0) {
        h.flags = X_PMinSize;
        h.min_width = X.cfg.min_width;
        h.min_height = X.cfg.min_height;
    }
    pXSetWMNormalHints(X.dpy, X.win, &h);
}

static void x11_set_title(const char *title) {
    pXStoreName(X.dpy, X.win, title);
    pXChangeProperty(X.dpy, X.win, X.net_wm_name, X.utf8_string, 8, X_PropModeReplace,
                     (const unsigned char *)title, (int)strlen(title));
    pXFlush(X.dpy);
}

/* _NET_WM_ICON: several sizes, each "width, height, then ARGB pixels"
 * (straight alpha), one per `long` - 64 bits on 64-bit systems. */
static void x11_set_icon(const br_image *icon) {
    static const int sizes[] = { 16, 24, 32, 48, 64, 128 };
    Atom net_wm_icon = pXInternAtom(X.dpy, "_NET_WM_ICON", 0);
    long *data;
    size_t n = 0, cap = 0;
    int i;
    if (!X.dpy || !X.win) return;
    if (!icon) {
        pXChangeProperty(X.dpy, X.win, net_wm_icon, (Atom)6 /* CARDINAL */, 32, X_PropModeReplace, NULL, 0);
        pXFlush(X.dpy);
        return;
    }
    for (i = 0; i < (int)BR_ARRAY_COUNT(sizes); ++i) cap += 2 + (size_t)sizes[i] * (size_t)sizes[i];
    if (!(data = (long *)br_alloc(cap * sizeof(long)))) return;
    for (i = 0; i < (int)BR_ARRAY_COUNT(sizes); ++i) {
        br_image small;
        int s = sizes[i], p;
        if (s > icon->width && s > 16) break;          /* never enlarge (except to the smallest size) */
        if (!br_image_downscale(icon, &small, s, s)) continue;
        data[n++] = s;
        data[n++] = s;
        for (p = 0; p < s * s; ++p) {
            br_color c = small.pixels[p];
            data[n++] = (long)(((unsigned long)c.a << 24) | ((unsigned long)c.r << 16) | ((unsigned long)c.g << 8) | c.b);
        }
        br_image_destroy(&small);
    }
    pXChangeProperty(X.dpy, X.win, net_wm_icon, (Atom)6, 32, X_PropModeReplace, (const unsigned char *)data, (int)n);
    pXFlush(X.dpy);
    br_free(data);
}

static void x11_set_fullscreen(bool fs) {
    if (X.has_wm) {
        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = X_ClientMessage;
        ev.xclient.window = X.win;
        ev.xclient.message_type = X.net_wm_state;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = fs ? 1 : 0;    /* _NET_WM_STATE_ADD / REMOVE */
        ev.xclient.data.l[1] = (long)X.net_wm_state_fullscreen;
        ev.xclient.data.l[3] = 1;             /* source: application */
        pXSendEvent(X.dpy, X.root, 0, X_SubstructureNotifyMask | X_SubstructureRedirectMask, &ev);
        /* The WM confirms through _NET_WM_STATE (PropertyNotify). */
    } else {
        /* No window manager: cover the screen ourselves. */
        br_event e = br_event_make(BR_EVENT_WINDOW_FULLSCREEN);
        if (fs == X.fullscreen) return;
        if (fs) {
            X.saved_w = X.width; X.saved_h = X.height;
            X.fullscreen = true;
            apply_size_hints();
            pXMoveResizeWindow(X.dpy, X.win, 0, 0, (unsigned)pXDisplayWidth(X.dpy, X.screen),
                               (unsigned)pXDisplayHeight(X.dpy, X.screen));
        } else {
            X.fullscreen = false;
            pXMoveResizeWindow(X.dpy, X.win, 0, 0, (unsigned)X.saved_w, (unsigned)X.saved_h);
            apply_size_hints();
        }
        e.u.window.fullscreen = fs;
        x11_post(&e);
    }
    pXFlush(X.dpy);
}

static void x11_set_window_size(int w, int h) {
    if (X.fullscreen) return;
    if (w < X.cfg.min_width) w = X.cfg.min_width;
    if (h < X.cfg.min_height) h = X.cfg.min_height;
    pXResizeWindow(X.dpy, X.win, (unsigned)w, (unsigned)h);
    if (!X.cfg.resizable) { X.width = w; X.height = h; apply_size_hints(); }
    pXFlush(X.dpy);
}

static void x11_set_cursor_visible(bool visible) {
    if (visible == !X.cursor_hidden) return;
    if (!visible) {
        if (!X.blank_cursor) {
            static const char zero[8] = { 0 };
            XColor black;
            Pixmap bm = pXCreateBitmapFromData(X.dpy, X.win, zero, 8, 8);
            memset(&black, 0, sizeof(black));
            X.blank_cursor = pXCreatePixmapCursor(X.dpy, bm, bm, &black, &black, 0, 0);
            pXFreePixmap(X.dpy, bm);
        }
        pXDefineCursor(X.dpy, X.win, X.blank_cursor);
    } else {
        pXUndefineCursor(X.dpy, X.win);
    }
    X.cursor_hidden = !visible;
    pXFlush(X.dpy);
}

/* ==== lifecycle ============================================================== */
static bool x11_init(const br_platform_config *cfg) {
    Atom protocols[1];
    static const char wm_class[] = "bronto\0Bronto";
    memset(&X, 0, sizeof(X));
    X.cfg = *cfg;
    if (!load_x11()) return false;
    X.dpy = pXOpenDisplay(NULL);
    if (!X.dpy) { BR_ERROR("X11: cannot open display (is DISPLAY set?)"); return false; }
    pXSetErrorHandler(x11_error_handler);
    X.screen = pXDefaultScreen(X.dpy);
    X.root = pXRootWindow(X.dpy, X.screen);
    X.visual = pXDefaultVisual(X.dpy, X.screen);
    X.depth = pXDefaultDepth(X.dpy, X.screen);
    if (X.visual->c_class != X_TrueColor || X.depth < 24) {
        BR_ERROR("X11: need a 24/32-bit TrueColor visual (got class %d, depth %d)", X.visual->c_class, X.depth);
        pXCloseDisplay(X.dpy);
        return false;
    }
    X.rshift = x11_mask_shift(X.visual->red_mask);
    X.gshift = x11_mask_shift(X.visual->green_mask);
    X.bshift = x11_mask_shift(X.visual->blue_mask);

    X.width = cfg->width > 0 ? cfg->width : 960;
    X.height = cfg->height > 0 ? cfg->height : 540;
    X.win = pXCreateSimpleWindow(X.dpy, X.root, 0, 0, (unsigned)X.width, (unsigned)X.height, 0,
                                 pXBlackPixel(X.dpy, X.screen), pXBlackPixel(X.dpy, X.screen));
    pXSelectInput(X.dpy, X.win, X_KeyPressMask | X_KeyReleaseMask | X_ButtonPressMask | X_ButtonReleaseMask |
                  X_PointerMotionMask | X_EnterWindowMask | X_LeaveWindowMask | X_StructureNotifyMask |
                  X_FocusChangeMask | X_PropertyChangeMask | X_ExposureMask);

    X.wm_protocols = pXInternAtom(X.dpy, "WM_PROTOCOLS", 0);
    X.wm_delete = pXInternAtom(X.dpy, "WM_DELETE_WINDOW", 0);
    X.net_wm_state = pXInternAtom(X.dpy, "_NET_WM_STATE", 0);
    X.net_wm_state_fullscreen = pXInternAtom(X.dpy, "_NET_WM_STATE_FULLSCREEN", 0);
    X.net_wm_state_hidden = pXInternAtom(X.dpy, "_NET_WM_STATE_HIDDEN", 0);
    X.net_wm_state_max_v = pXInternAtom(X.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", 0);
    X.net_wm_state_max_h = pXInternAtom(X.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", 0);
    X.net_wm_name = pXInternAtom(X.dpy, "_NET_WM_NAME", 0);
    X.utf8_string = pXInternAtom(X.dpy, "UTF8_STRING", 0);
    X.clipboard = pXInternAtom(X.dpy, "CLIPBOARD", 0);
    X.targets = pXInternAtom(X.dpy, "TARGETS", 0);
    X.text_atom = pXInternAtom(X.dpy, "TEXT", 0);
    X.incr = pXInternAtom(X.dpy, "INCR", 0);
    X.paste_property = pXInternAtom(X.dpy, "BRONTO_PASTE", 0);
    protocols[0] = X.wm_delete;
    pXSetWMProtocols(X.dpy, X.win, protocols, 1);
    pXChangeProperty(X.dpy, X.win, pXInternAtom(X.dpy, "WM_CLASS", 0), X_XA_STRING, 8, X_PropModeReplace,
                     (const unsigned char *)wm_class, (int)sizeof(wm_class));
    x11_set_title(cfg->title ? cfg->title : "Bronto");
    apply_size_hints();
    if (pXkbSetDetectableAutoRepeat) pXkbSetDetectableAutoRepeat(X.dpy, 1, NULL);
    X.has_wm = detect_wm();

    X.gc = pXCreateGC(X.dpy, X.win, 0, NULL);
    X.hidden = cfg->hidden;
    if (!cfg->hidden) { pXMapWindow(X.dpy, X.win); X.mapped = true; }
    if (cfg->fullscreen) x11_set_fullscreen(true);
    pXSync(X.dpy, 0);
    if (cfg->audio_sample_rate > 0) alsa_open(cfg->audio_sample_rate, cfg->audio_latency_ms);
    evdev_init();
    BR_INFO("X11: %dx%d window, depth %d, window manager %s", X.width, X.height, X.depth,
            X.has_wm ? "detected" : "not detected");
    return true;
}

static void destroy_image(void) {
    if (X.image) {
        X.image->data = NULL;               /* our buffer, freed below */
        X.image->f.destroy_image(X.image);
        X.image = NULL;
    }
    br_free(X.pixels);
    X.pixels = NULL;
}

static void x11_shutdown(void) {
    alsa_close();
    evdev_shutdown();
    if (!X.dpy) return;
    destroy_image();
    br_free(X.clip_text);
    X.clip_text = NULL;
    if (X.blank_cursor) pXFreeCursor(X.dpy, X.blank_cursor);
    if (X.gc) pXFreeGC(X.dpy, X.gc);
    pXDestroyWindow(X.dpy, X.win);
    pXCloseDisplay(X.dpy);
    X.dpy = NULL;
    /* libX11 stays loaded: unloading it is unsafe with some Xlib
     * extensions registered (and cheap to keep). */
}

/* ==== clipboard ===============================================================
 * X11 has no clipboard storage: the program that copied owns the CLIPBOARD
 * selection and hands the text to whoever asks (SelectionRequest), so it is
 * served from x11_poll. Pasting asks the owner to convert it into a property
 * of our window and waits briefly for the answer. (Text copied here is gone
 * when the program exits, unless a clipboard manager keeps it.) */
static void x11_serve_selection(const XSelectionRequestEvent *r) {
    XEvent reply;
    Atom prop = r->property ? r->property : r->target;     /* (old clients leave it out) */
    memset(&reply, 0, sizeof(reply));
    reply.xselection.type = X_SelectionNotify;
    reply.xselection.display = r->display;
    reply.xselection.requestor = r->requestor;
    reply.xselection.selection = r->selection;
    reply.xselection.target = r->target;
    reply.xselection.time = r->time;
    reply.xselection.property = 0;                          /* None: refused */
    if (r->selection == X.clipboard && X.clip_text) {
        if (r->target == X.targets) {
            Atom list[4];
            list[0] = X.targets; list[1] = X.utf8_string; list[2] = X_XA_STRING; list[3] = X.text_atom;
            pXChangeProperty(X.dpy, r->requestor, prop, X_XA_ATOM, 32, X_PropModeReplace, (const unsigned char *)list, 4);
            reply.xselection.property = prop;
        } else if (r->target == X.utf8_string || r->target == X_XA_STRING || r->target == X.text_atom) {
            pXChangeProperty(X.dpy, r->requestor, prop, r->target == X_XA_STRING ? X_XA_STRING : X.utf8_string, 8,
                             X_PropModeReplace, (const unsigned char *)X.clip_text, (int)X.clip_len);
            reply.xselection.property = prop;
        }
    }
    pXSendEvent(X.dpy, r->requestor, 0, 0, &reply);
    pXFlush(X.dpy);
}

static bool x11_clipboard_set(const char *text) {
    size_t n = strlen(text);
    char *copy = (char *)br_alloc(n + 1);
    if (!copy || !X.dpy) { br_free(copy); return false; }
    memcpy(copy, text, n + 1);
    br_free(X.clip_text);
    X.clip_text = copy;
    X.clip_len = n;
    pXSetSelectionOwner(X.dpy, X.clipboard, X.win, 0 /* CurrentTime */);
    return pXGetSelectionOwner(X.dpy, X.clipboard) == X.win;
}

static double x11_time(void);
static void x11_sleep(double s);

static char *x11_clipboard_get(void) {
    Atom ask[2];
    int attempt;
    Window owner;
    if (!X.dpy) return NULL;
    owner = pXGetSelectionOwner(X.dpy, X.clipboard);
    if (owner == 0) return NULL;                            /* nobody has copied anything */
    if (owner == X.win) {
        char *copy;
        if (!X.clip_text || !(copy = (char *)br_alloc(X.clip_len + 1))) return NULL;
        memcpy(copy, X.clip_text, X.clip_len + 1);
        return copy;
    }
    ask[0] = X.utf8_string;
    ask[1] = X_XA_STRING;
    for (attempt = 0; attempt < 2; ++attempt) {
        double give_up = x11_time() + 0.5;
        XEvent ev;
        bool answered = false;
        pXDeleteProperty(X.dpy, X.win, X.paste_property);
        pXConvertSelection(X.dpy, X.clipboard, ask[attempt], X.paste_property, X.win, 0);
        pXFlush(X.dpy);
        while (x11_time() < give_up) {
            if (pXCheckTypedWindowEvent(X.dpy, X.win, X_SelectionNotify, &ev)) { answered = true; break; }
            x11_sleep(0.002);
        }
        if (!answered) return NULL;                         /* the owner didn't answer */
        if (ev.xselection.property != 0) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char *data = NULL;
            char *out = NULL;
            if (pXGetWindowProperty(X.dpy, X.win, X.paste_property, 0, 0x1FFFFFFF, 1, 0 /* AnyPropertyType */,
                                    &type, &format, &count, &after, &data) == 0 && data) {
                if (type != X.incr && format == 8 && (out = (char *)br_alloc(count * 2 + 1)) != NULL) {
                    unsigned long i, n = 0;                 /* (INCR: too big to be sent in one go) */
                    for (i = 0; i < count; ++i) {
                        unsigned char c = data[i];
                        if (type == X_XA_STRING && c >= 0x80) {   /* STRING is Latin-1 */
                            out[n++] = (char)(0xC0 | (c >> 6));
                            out[n++] = (char)(0x80 | (c & 0x3F));
                        } else {
                            out[n++] = (char)c;
                        }
                    }
                    out[n] = 0;
                }
                pXFree(data);
            }
            return out;
        }
    }
    return NULL;
}

static void x11_poll(br_events *events) {
    X.events = events;
    while (pXPending(X.dpy)) {
        XEvent ev;
        pXNextEvent(X.dpy, &ev);
        switch (ev.type) {
        case X_KeyPress:   handle_key(&ev.xkey, true); break;
        case X_KeyRelease: handle_key(&ev.xkey, false); break;
        case X_ButtonPress:
        case X_ButtonRelease: {
            bool down = ev.type == X_ButtonPress;
            unsigned int b = ev.xbutton.button;
            if (b >= 4 && b <= 7) {           /* wheel: presses only */
                if (down) {
                    br_event w = br_event_make(BR_EVENT_MOUSE_WHEEL);
                    w.u.wheel.dy = b == 4 ? 1.0f : (b == 5 ? -1.0f : 0.0f);
                    w.u.wheel.dx = b == 6 ? -1.0f : (b == 7 ? 1.0f : 0.0f);
                    x11_post(&w);
                }
            } else {
                br_mouse_button mb = b == 1 ? BR_MOUSE_LEFT : b == 2 ? BR_MOUSE_MIDDLE : b == 3 ? BR_MOUSE_RIGHT
                                   : b == 8 ? BR_MOUSE_X1 : b == 9 ? BR_MOUSE_X2 : BR_MOUSE_BUTTON_COUNT;
                if (mb != BR_MOUSE_BUTTON_COUNT) {
                    br_event e = br_event_mouse_button(down ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP,
                                                       mb, (float)ev.xbutton.x, (float)ev.xbutton.y);
                    x11_post(&e);
                }
            }
            break;
        }
        case X_MotionNotify: {
            float mx = (float)ev.xmotion.x, my = (float)ev.xmotion.y;
            br_event e = br_event_mouse_move(mx, my, mx - X.last_mx, my - X.last_my);
            X.last_mx = mx;
            X.last_my = my;
            x11_post(&e);
            break;
        }
        case X_EnterNotify: x11_post_simple(BR_EVENT_MOUSE_ENTER); break;
        case X_LeaveNotify: x11_post_simple(BR_EVENT_MOUSE_LEAVE); break;
        case X_FocusIn:
            if (ev.xfocus.mode == 0 || ev.xfocus.mode == 3) x11_post_simple(BR_EVENT_WINDOW_FOCUS_GAINED);
            break;
        case X_FocusOut:
            if (ev.xfocus.mode == 0 || ev.xfocus.mode == 3) {
                memset(X.key_down, 0, sizeof(X.key_down));
                x11_post_simple(BR_EVENT_WINDOW_FOCUS_LOST);
            }
            break;
        case X_ConfigureNotify:
            if (ev.xconfigure.width != X.width || ev.xconfigure.height != X.height) {
                br_event e = br_event_make(BR_EVENT_WINDOW_RESIZED);
                X.width = ev.xconfigure.width;
                X.height = ev.xconfigure.height;
                e.u.resize.width = X.width;
                e.u.resize.height = X.height;
                x11_post(&e);
            }
            break;
        case X_UnmapNotify:
            X.mapped = false;
            if (!X.hidden && !X.minimized) { X.minimized = true; x11_post_simple(BR_EVENT_WINDOW_MINIMIZED); }
            break;
        case X_MapNotify:
            X.mapped = true;
            if (X.minimized) { X.minimized = false; x11_post_simple(BR_EVENT_WINDOW_RESTORED); }
            break;
        case X_PropertyNotify:
            if (ev.xproperty.atom == X.net_wm_state) read_wm_state();
            break;
        case X_ClientMessage:
            if (ev.xclient.message_type == X.wm_protocols && (Atom)ev.xclient.data.l[0] == X.wm_delete)
                x11_post_simple(BR_EVENT_WINDOW_CLOSE_REQUESTED);
            break;
        case X_SelectionRequest: x11_serve_selection(&ev.xselectionrequest); break;
        case X_SelectionClear:                     /* another program owns the clipboard now */
            if (ev.xselectionclear.selection == X.clipboard) { br_free(X.clip_text); X.clip_text = NULL; }
            break;
        default:
            break;
        }
    }
    evdev_poll(events);
}

static void x11_present(const br_image *fb, br_recti vp) {
    if (!X.mapped || X.width <= 0 || X.height <= 0) return;
    if (!X.image || X.image->width != X.width || X.image->height != X.height) {
        destroy_image();
        X.pixels = (uint32_t *)br_alloc((size_t)X.width * (size_t)X.height * 4u);
        if (!X.pixels) return;
        X.image = pXCreateImage(X.dpy, X.visual, (unsigned)X.depth, X_ZPixmap, 0, (char *)X.pixels,
                                (unsigned)X.width, (unsigned)X.height, 32, 0);
        if (!X.image) { br_free(X.pixels); X.pixels = NULL; return; }
    }
    br_viewport_blit(fb, vp, X.pixels, X.width, X.height, (size_t)X.image->bytes_per_line / 4u,
                     X.rshift, X.gshift, X.bshift, -1);
    pXPutImage(X.dpy, X.win, X.gc, X.image, 0, 0, 0, 0, (unsigned)X.width, (unsigned)X.height);
    pXFlush(X.dpy);
}

static double x11_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void x11_sleep(double s) {
    struct timespec ts;
    if (s <= 0) return;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

static uint32_t x11_audio_wanted(void) { return alsa_wanted(); }
static void x11_audio_submit(const int16_t *d, uint32_t n) { alsa_submit(d, n); }
static void x11_get_window_size(int *w, int *h) { *w = X.width; *h = X.height; }
static float x11_get_window_scale(void) { return 1.0f; }

static bool x11_set_gamepad_rumble(int pad, float lo, float hi) { return evdev_rumble(pad, lo, hi); }

#include "desktop_unix.c"

static const br_platform k_x11 = {
    "x11", x11_init, x11_shutdown, x11_poll, x11_present, x11_time, x11_sleep,
    x11_audio_wanted, x11_audio_submit,
    x11_get_window_size, x11_get_window_scale, x11_set_title, x11_set_window_size,
    x11_set_fullscreen, x11_set_cursor_visible, x11_set_icon, du_pick_folder, du_open_path, du_launch,
    x11_clipboard_set, x11_clipboard_get,
    x11_set_gamepad_rumble,
    NULL
};

const br_platform *br_platform_native(void) { return &k_x11; }

/* Test hook: the X window id, so integration tests can drive it. */
unsigned long br_x11_window_id(void);
unsigned long br_x11_window_id(void) { return (unsigned long)X.win; }
