/*
 * macOS backend: Cocoa window driven from plain C through the Objective-C
 * runtime (objc_msgSend), presented with Core Animation.
 *
 * - The framebuffer becomes a CGImage set as the contents of a CALayer
 *   positioned at the engine's viewport, with nearest-neighbour filtering;
 *   the view's backing layer is black, which gives the bars for free.
 * - Retina aware: sizes are requested in points; the engine sees pixels.
 * - Native fullscreen (its own Space), the standard app menu with Quit
 *   (Cmd+Q) and Toggle Full Screen (Ctrl+Cmd+F).
 * - Audio: Core Audio AudioQueue (audio_coreaudio.c).
 * - Gamepads: GameController framework (gamepad_gc.c).
 * - Link: -framework Cocoa -framework QuartzCore -framework AudioToolbox
 *         -framework GameController.
 *
 * Every objc_msgSend call is cast to the exact prototype of the method:
 * required on arm64, where variadic and fixed-argument calls differ.
 *
 * Included by platform_native.c.
 */
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bronto/platform.h"
#include "bronto/log.h"
#include "bronto/memory.h"
#include "bronto/audio.h"

#include "audio_coreaudio.c"

/* Exported by libobjc but not declared in the public headers. */
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);
/* Foundation / QuartzCore string constants (NSString *). */
extern id NSDefaultRunLoopMode;
extern id kCAFilterNearest;

/* ---- AppKit constants ------------------------------------------------------ */
#define NS_WINDOW_TITLED (1ul << 0)
#define NS_WINDOW_CLOSABLE (1ul << 1)
#define NS_WINDOW_MINIATURIZABLE (1ul << 2)
#define NS_WINDOW_RESIZABLE (1ul << 3)
#define NS_BACKING_BUFFERED 2ul
#define NS_COLLECTION_FULLSCREEN_PRIMARY (1ul << 7)
#define NS_ACTIVATION_POLICY_REGULAR 0l
#define NS_TERMINATE_CANCEL 0ul
#define NS_EVENT_MASK_ANY (~0ull)
#define NS_TRACKING_ENTERED_EXITED 0x01ul
#define NS_TRACKING_ACTIVE_ALWAYS 0x80ul
#define NS_TRACKING_IN_VISIBLE_RECT 0x200ul

#define NS_LEFT_MOUSE_DOWN 1ul
#define NS_LEFT_MOUSE_UP 2ul
#define NS_RIGHT_MOUSE_DOWN 3ul
#define NS_RIGHT_MOUSE_UP 4ul
#define NS_MOUSE_MOVED 5ul
#define NS_LEFT_MOUSE_DRAGGED 6ul
#define NS_RIGHT_MOUSE_DRAGGED 7ul
#define NS_KEY_DOWN 10ul
#define NS_KEY_UP 11ul
#define NS_FLAGS_CHANGED 12ul
#define NS_SCROLL_WHEEL 22ul
#define NS_OTHER_MOUSE_DOWN 25ul
#define NS_OTHER_MOUSE_UP 26ul
#define NS_OTHER_MOUSE_DRAGGED 27ul

#define NS_MOD_CAPS (1ul << 16)
#define NS_MOD_SHIFT (1ul << 17)
#define NS_MOD_CONTROL (1ul << 18)
#define NS_MOD_OPTION (1ul << 19)
#define NS_MOD_COMMAND (1ul << 20)
/* Device-dependent bits distinguish left/right modifier keys. */
#define NX_LCTL 0x0001ul
#define NX_LSHIFT 0x0002ul
#define NX_RSHIFT 0x0004ul
#define NX_LCMD 0x0008ul
#define NX_RCMD 0x0010ul
#define NX_LALT 0x0020ul
#define NX_RALT 0x0040ul
#define NX_RCTL 0x2000ul

#if defined(__arm64__) || defined(__aarch64__)
#  define BOOL_ENC "B"
#else
#  define BOOL_ENC "c"
#endif

/* ---- typed message helpers --------------------------------------------------- */
#define SEL_(s) sel_registerName(s)
static id cls(const char *name) { return (id)objc_getClass(name); }
static id msg_id(id o, const char *s) { return ((id (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static id msg_id_id(id o, const char *s, id a) { return ((id (*)(id, SEL, id))objc_msgSend)(o, SEL_(s), a); }
static void msg_v(id o, const char *s) { ((void (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static void msg_v_id(id o, const char *s, id a) { ((void (*)(id, SEL, id))objc_msgSend)(o, SEL_(s), a); }
static void msg_v_bool(id o, const char *s, BOOL b) { ((void (*)(id, SEL, BOOL))objc_msgSend)(o, SEL_(s), b); }
static void msg_v_long(id o, const char *s, long v) { ((void (*)(id, SEL, long))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_ulong(id o, const char *s, unsigned long v) { ((void (*)(id, SEL, unsigned long))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_double(id o, const char *s, double v) { ((void (*)(id, SEL, double))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_size(id o, const char *s, CGSize v) { ((void (*)(id, SEL, CGSize))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_rect(id o, const char *s, CGRect v) { ((void (*)(id, SEL, CGRect))objc_msgSend)(o, SEL_(s), v); }
static void msg_v_ptr(id o, const char *s, const void *p) { ((void (*)(id, SEL, const void *))objc_msgSend)(o, SEL_(s), p); }
static double msg_double(id o, const char *s) { return ((double (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static BOOL msg_bool(id o, const char *s) { return ((BOOL (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static unsigned long msg_ulong(id o, const char *s) { return ((unsigned long (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static long msg_long(id o, const char *s) { return ((long (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static unsigned short msg_ushort(id o, const char *s) { return ((unsigned short (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static const char *msg_cstr(id o, const char *s) { return ((const char *(*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static CGPoint msg_point(id o, const char *s) { return ((CGPoint (*)(id, SEL))objc_msgSend)(o, SEL_(s)); }
static CGRect msg_rect(id o, const char *s) {
#if defined(__x86_64__)
    CGRect r;   /* 32-byte structs are returned in memory on x86-64 */
    ((void (*)(CGRect *, id, SEL))objc_msgSend_stret)(&r, o, SEL_(s));
    return r;
#else
    return ((CGRect (*)(id, SEL))objc_msgSend)(o, SEL_(s));
#endif
}
static id nsstring(const char *utf8) {
    return ((id (*)(id, SEL, const char *))objc_msgSend)(cls("NSString"), SEL_("stringWithUTF8String:"), utf8 ? utf8 : "");
}
static id alloc_init(const char *class_name) { return msg_id(msg_id(cls(class_name), "alloc"), "init"); }

#include "gamepad_gc.c"

/* ---- state ----------------------------------------------------------------------- */
static struct {
    id app, window, view, image_layer, window_delegate, app_delegate, tracking;
    br_platform_config cfg;
    br_events *events;
    br_event pending[512];      /* events raised by delegates outside poll */
    int pending_count;
    bool fullscreen, minimized, cursor_hidden, cursor_hidden_now, mouse_inside, initialized;
    float last_mx, last_my;
    mach_timebase_info_data_t timebase;
    CGColorSpaceRef colorspace;
} M;

static void mac_post(const br_event *e) {
    if (M.events) br_events_post(M.events, e);
    else if (M.pending_count < (int)BR_ARRAY_COUNT(M.pending)) M.pending[M.pending_count++] = *e;
}
static void mac_post_simple(uint32_t type) { br_event e = br_event_make(type); mac_post(&e); }

static double mac_scale(void) {
    double s = M.window ? msg_double(M.window, "backingScaleFactor") : 1.0;
    return s > 0 ? s : 1.0;
}

static void mac_get_window_size(int *w, int *h) {
    CGRect b = msg_rect(M.view, "bounds");
    double s = mac_scale();
    *w = (int)(b.size.width * s + 0.5);
    *h = (int)(b.size.height * s + 0.5);
}

static void post_resize(void) {
    br_event e = br_event_make(BR_EVENT_WINDOW_RESIZED);
    mac_get_window_size(&e.u.resize.width, &e.u.resize.height);
    mac_post(&e);
}

/* ---- delegate / view methods (C functions registered with the runtime) ------- */
static unsigned long app_should_terminate(id self, SEL s, id sender) {
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(sender);
    mac_post_simple(BR_EVENT_QUIT);          /* Cmd+Q: the engine decides */
    return NS_TERMINATE_CANCEL;
}
static BOOL win_should_close(id self, SEL s, id sender) {
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(sender);
    mac_post_simple(BR_EVENT_WINDOW_CLOSE_REQUESTED);
    return NO;
}
static void win_did_resize(id self, SEL s, id n) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n); post_resize(); }
static void win_did_miniaturize(id self, SEL s, id n) {
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n);
    M.minimized = true;
    mac_post_simple(BR_EVENT_WINDOW_MINIMIZED);
}
static void win_did_deminiaturize(id self, SEL s, id n) {
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n);
    M.minimized = false;
    mac_post_simple(BR_EVENT_WINDOW_RESTORED);
}
static void win_did_become_key(id self, SEL s, id n) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n); mac_post_simple(BR_EVENT_WINDOW_FOCUS_GAINED); }
static void win_did_resign_key(id self, SEL s, id n) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n); mac_post_simple(BR_EVENT_WINDOW_FOCUS_LOST); }
static void win_did_move(id self, SEL s, id n) {
    CGRect f = msg_rect(M.window, "frame");
    br_event e = br_event_make(BR_EVENT_WINDOW_MOVED);
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n);
    e.u.window.x = (int)f.origin.x;
    e.u.window.y = (int)f.origin.y;
    mac_post(&e);
}
static void win_did_change_backing(id self, SEL s, id n) {
    br_event e = br_event_make(BR_EVENT_WINDOW_SCALE_CHANGED);
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n);
    e.u.window.scale = (float)mac_scale();
    mac_post(&e);
    post_resize();
}
static void fullscreen_changed(bool fs) {
    br_event e = br_event_make(BR_EVENT_WINDOW_FULLSCREEN);
    M.fullscreen = fs;
    e.u.window.fullscreen = fs;
    mac_post(&e);
    post_resize();
}
static void win_did_enter_fs(id self, SEL s, id n) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n); fullscreen_changed(true); }
static void win_did_exit_fs(id self, SEL s, id n) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(n); fullscreen_changed(false); }

static BOOL view_yes(id self, SEL s) { BR_UNUSED(self); BR_UNUSED(s); return YES; }
static BOOL view_yes_id(id self, SEL s, id e) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(e); return YES; }
/* Keys are translated in poll; these stop AppKit from beeping. */
static void view_ignore_event(id self, SEL s, id e) { BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(e); }
static void apply_cursor(void) {
    bool want_hidden = M.cursor_hidden && M.mouse_inside;
    if (want_hidden && !M.cursor_hidden_now) msg_v(cls("NSCursor"), "hide");
    if (!want_hidden && M.cursor_hidden_now) msg_v(cls("NSCursor"), "unhide");
    M.cursor_hidden_now = want_hidden;
}
static void view_mouse_entered(id self, SEL s, id e) {
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(e);
    M.mouse_inside = true;
    apply_cursor();
    mac_post_simple(BR_EVENT_MOUSE_ENTER);
}
static void view_mouse_exited(id self, SEL s, id e) {
    BR_UNUSED(self); BR_UNUSED(s); BR_UNUSED(e);
    M.mouse_inside = false;
    apply_cursor();
    mac_post_simple(BR_EVENT_MOUSE_LEAVE);
}

static void add_method(Class c, const char *sel, IMP imp, const char *types) {
    class_addMethod(c, SEL_(sel), imp, types);
}

static void register_classes(void) {
    Class c;
    if (!objc_getClass("BrontoAppDelegate")) {
        c = objc_allocateClassPair(objc_getClass("NSObject"), "BrontoAppDelegate", 0);
        add_method(c, "applicationShouldTerminate:", (IMP)app_should_terminate, "Q@:@");
        objc_registerClassPair(c);
    }
    if (!objc_getClass("BrontoWindowDelegate")) {
        c = objc_allocateClassPair(objc_getClass("NSObject"), "BrontoWindowDelegate", 0);
        add_method(c, "windowShouldClose:", (IMP)win_should_close, BOOL_ENC "@:@");
        add_method(c, "windowDidResize:", (IMP)win_did_resize, "v@:@");
        add_method(c, "windowDidMiniaturize:", (IMP)win_did_miniaturize, "v@:@");
        add_method(c, "windowDidDeminiaturize:", (IMP)win_did_deminiaturize, "v@:@");
        add_method(c, "windowDidBecomeKey:", (IMP)win_did_become_key, "v@:@");
        add_method(c, "windowDidResignKey:", (IMP)win_did_resign_key, "v@:@");
        add_method(c, "windowDidMove:", (IMP)win_did_move, "v@:@");
        add_method(c, "windowDidChangeBackingProperties:", (IMP)win_did_change_backing, "v@:@");
        add_method(c, "windowDidEnterFullScreen:", (IMP)win_did_enter_fs, "v@:@");
        add_method(c, "windowDidExitFullScreen:", (IMP)win_did_exit_fs, "v@:@");
        objc_registerClassPair(c);
    }
    if (!objc_getClass("BrontoView")) {
        c = objc_allocateClassPair(objc_getClass("NSView"), "BrontoView", 0);
        add_method(c, "acceptsFirstResponder", (IMP)view_yes, BOOL_ENC "@:");
        add_method(c, "isOpaque", (IMP)view_yes, BOOL_ENC "@:");
        add_method(c, "acceptsFirstMouse:", (IMP)view_yes_id, BOOL_ENC "@:@");
        add_method(c, "keyDown:", (IMP)view_ignore_event, "v@:@");
        add_method(c, "keyUp:", (IMP)view_ignore_event, "v@:@");
        add_method(c, "flagsChanged:", (IMP)view_ignore_event, "v@:@");
        add_method(c, "mouseEntered:", (IMP)view_mouse_entered, "v@:@");
        add_method(c, "mouseExited:", (IMP)view_mouse_exited, "v@:@");
        objc_registerClassPair(c);
    }
}

/* ---- keyboard ----------------------------------------------------------------- */
/* Indexed by macOS virtual key code (kVK_* in HIToolbox/Events.h). Codes are
 * physical positions, so WASD stays WASD on any keyboard layout. */
static const br_key k_mac_keys[128] = {
    /* 0x00 */ BR_KEY_A, BR_KEY_S, BR_KEY_D, BR_KEY_F, BR_KEY_H, BR_KEY_G, BR_KEY_Z, BR_KEY_X,
    /* 0x08 */ BR_KEY_C, BR_KEY_V, BR_KEY_UNKNOWN, BR_KEY_B, BR_KEY_Q, BR_KEY_W, BR_KEY_E, BR_KEY_R,
    /* 0x10 */ BR_KEY_Y, BR_KEY_T, BR_KEY_1, BR_KEY_2, BR_KEY_3, BR_KEY_4, BR_KEY_6, BR_KEY_5,
    /* 0x18 */ BR_KEY_EQUAL, BR_KEY_9, BR_KEY_7, BR_KEY_MINUS, BR_KEY_8, BR_KEY_0, BR_KEY_RIGHT_BRACKET, BR_KEY_O,
    /* 0x20 */ BR_KEY_U, BR_KEY_LEFT_BRACKET, BR_KEY_I, BR_KEY_P, BR_KEY_ENTER, BR_KEY_L, BR_KEY_J, BR_KEY_APOSTROPHE,
    /* 0x28 */ BR_KEY_K, BR_KEY_SEMICOLON, BR_KEY_BACKSLASH, BR_KEY_COMMA, BR_KEY_SLASH, BR_KEY_N, BR_KEY_M, BR_KEY_PERIOD,
    /* 0x30 */ BR_KEY_TAB, BR_KEY_SPACE, BR_KEY_GRAVE, BR_KEY_BACKSPACE, BR_KEY_UNKNOWN, BR_KEY_ESCAPE, BR_KEY_RIGHT_SUPER, BR_KEY_LEFT_SUPER,
    /* 0x38 */ BR_KEY_LEFT_SHIFT, BR_KEY_CAPS_LOCK, BR_KEY_LEFT_ALT, BR_KEY_LEFT_CONTROL, BR_KEY_RIGHT_SHIFT, BR_KEY_RIGHT_ALT, BR_KEY_RIGHT_CONTROL, BR_KEY_UNKNOWN,
    /* 0x40 */ BR_KEY_UNKNOWN, BR_KEY_KP_DECIMAL, BR_KEY_UNKNOWN, BR_KEY_KP_MULTIPLY, BR_KEY_UNKNOWN, BR_KEY_KP_ADD, BR_KEY_UNKNOWN, BR_KEY_NUM_LOCK,
    /* 0x48 */ BR_KEY_UNKNOWN, BR_KEY_UNKNOWN, BR_KEY_UNKNOWN, BR_KEY_KP_DIVIDE, BR_KEY_KP_ENTER, BR_KEY_UNKNOWN, BR_KEY_KP_SUBTRACT, BR_KEY_UNKNOWN,
    /* 0x50 */ BR_KEY_UNKNOWN, BR_KEY_KP_EQUAL, BR_KEY_KP_0, BR_KEY_KP_1, BR_KEY_KP_2, BR_KEY_KP_3, BR_KEY_KP_4, BR_KEY_KP_5,
    /* 0x58 */ BR_KEY_KP_6, BR_KEY_KP_7, BR_KEY_UNKNOWN, BR_KEY_KP_8, BR_KEY_KP_9, BR_KEY_UNKNOWN, BR_KEY_UNKNOWN, BR_KEY_UNKNOWN,
    /* 0x60 */ BR_KEY_F5, BR_KEY_F6, BR_KEY_F7, BR_KEY_F3, BR_KEY_F8, BR_KEY_F9, BR_KEY_UNKNOWN, BR_KEY_F11,
    /* 0x68 */ BR_KEY_UNKNOWN, BR_KEY_PRINT_SCREEN, BR_KEY_UNKNOWN, BR_KEY_SCROLL_LOCK, BR_KEY_UNKNOWN, BR_KEY_F10, BR_KEY_MENU, BR_KEY_F12,
    /* 0x70 */ BR_KEY_UNKNOWN, BR_KEY_PAUSE, BR_KEY_INSERT, BR_KEY_HOME, BR_KEY_PAGE_UP, BR_KEY_DELETE, BR_KEY_F4, BR_KEY_END,
    /* 0x78 */ BR_KEY_F2, BR_KEY_PAGE_DOWN, BR_KEY_F1, BR_KEY_LEFT, BR_KEY_RIGHT, BR_KEY_DOWN, BR_KEY_UP, BR_KEY_UNKNOWN
};

static br_key mac_translate_key(unsigned short kc) { return kc < 128 ? k_mac_keys[kc] : BR_KEY_UNKNOWN; }

static uint32_t mac_mods(unsigned long f) {
    uint32_t m = 0;
    if (f & NS_MOD_SHIFT) m |= BR_MOD_SHIFT;
    if (f & NS_MOD_CONTROL) m |= BR_MOD_CONTROL;
    if (f & NS_MOD_OPTION) m |= BR_MOD_ALT;
    if (f & NS_MOD_COMMAND) m |= BR_MOD_SUPER;
    if (f & NS_MOD_CAPS) m |= BR_MOD_CAPS_LOCK;
    return m;
}

/* Which modifier-flag bit says a given modifier key is down. */
static unsigned long modifier_bit(br_key k) {
    switch (k) {
    case BR_KEY_LEFT_SHIFT: return NX_LSHIFT;
    case BR_KEY_RIGHT_SHIFT: return NX_RSHIFT;
    case BR_KEY_LEFT_CONTROL: return NX_LCTL;
    case BR_KEY_RIGHT_CONTROL: return NX_RCTL;
    case BR_KEY_LEFT_ALT: return NX_LALT;
    case BR_KEY_RIGHT_ALT: return NX_RALT;
    case BR_KEY_LEFT_SUPER: return NX_LCMD;
    case BR_KEY_RIGHT_SUPER: return NX_RCMD;
    case BR_KEY_CAPS_LOCK: return NS_MOD_CAPS;
    default: return 0;
    }
}

/* Decodes UTF-8 and posts text events (skipping control characters and
 * Apple's private-use function-key characters). */
static void post_text(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (p && *p) {
        uint32_t cp;
        int n, i;
        if (*p < 0x80) { cp = *p; n = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1Fu; n = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0Fu; n = 3; }
        else { cp = *p & 0x07u; n = 4; }
        for (i = 1; i < n; ++i) {
            if ((p[i] & 0xC0) != 0x80) return;
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        p += n;
        if (cp < 32 || cp == 127 || (cp >= 0xF700 && cp <= 0xF8FF)) continue;
        {
            br_event e = br_event_make(BR_EVENT_TEXT_INPUT);
            e.u.text.codepoint = cp;
            mac_post(&e);
        }
    }
}

/* ---- event translation ---------------------------------------------------------- */
static CGPoint event_pixel_pos(id ev) {
    CGPoint p = msg_point(ev, "locationInWindow");
    CGPoint v = ((CGPoint (*)(id, SEL, CGPoint, id))objc_msgSend)(M.view, SEL_("convertPoint:fromView:"), p, (id)0);
    CGRect b = msg_rect(M.view, "bounds");
    double s = mac_scale();
    CGPoint out;
    out.x = v.x * s;
    out.y = (b.size.height - v.y) * s;   /* AppKit is y-up; Bronto is y-down */
    return out;
}

static void translate_event(id ev) {
    unsigned long type = msg_ulong(ev, "type");
    switch (type) {
    case NS_KEY_DOWN:
    case NS_KEY_UP: {
        unsigned long flags = msg_ulong(ev, "modifierFlags");
        unsigned short kc = msg_ushort(ev, "keyCode");
        bool down = type == NS_KEY_DOWN;
        br_event e = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, mac_translate_key(kc),
                                  mac_mods(flags), down && msg_bool(ev, "isARepeat"));
        e.u.key.scancode = kc;
        mac_post(&e);
        if (down && !(flags & (NS_MOD_COMMAND | NS_MOD_CONTROL)))
            post_text(msg_cstr(msg_id(ev, "characters"), "UTF8String"));
        break;
    }
    case NS_FLAGS_CHANGED: {
        unsigned long flags = msg_ulong(ev, "modifierFlags");
        br_key key = mac_translate_key(msg_ushort(ev, "keyCode"));
        unsigned long bit = modifier_bit(key);
        if (!bit) break;
        if (key == BR_KEY_CAPS_LOCK) {          /* a toggle: report a tap */
            br_event d = br_event_key(BR_EVENT_KEY_DOWN, key, mac_mods(flags), false);
            br_event u = br_event_key(BR_EVENT_KEY_UP, key, mac_mods(flags), false);
            mac_post(&d);
            mac_post(&u);
        } else {
            bool down = (flags & bit) != 0;
            br_event e = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, key, mac_mods(flags), false);
            mac_post(&e);
        }
        break;
    }
    case NS_MOUSE_MOVED: case NS_LEFT_MOUSE_DRAGGED: case NS_RIGHT_MOUSE_DRAGGED: case NS_OTHER_MOUSE_DRAGGED: {
        CGPoint p = event_pixel_pos(ev);
        br_event e = br_event_mouse_move((float)p.x, (float)p.y, (float)p.x - M.last_mx, (float)p.y - M.last_my);
        M.last_mx = (float)p.x;
        M.last_my = (float)p.y;
        mac_post(&e);
        break;
    }
    case NS_LEFT_MOUSE_DOWN: case NS_LEFT_MOUSE_UP:
    case NS_RIGHT_MOUSE_DOWN: case NS_RIGHT_MOUSE_UP:
    case NS_OTHER_MOUSE_DOWN: case NS_OTHER_MOUSE_UP: {
        CGPoint p = event_pixel_pos(ev);
        bool down = type == NS_LEFT_MOUSE_DOWN || type == NS_RIGHT_MOUSE_DOWN || type == NS_OTHER_MOUSE_DOWN;
        br_mouse_button b;
        br_event e;
        if (type <= NS_LEFT_MOUSE_UP) b = BR_MOUSE_LEFT;
        else if (type <= NS_RIGHT_MOUSE_UP) b = BR_MOUSE_RIGHT;
        else {
            long n = msg_long(ev, "buttonNumber");
            b = n == 2 ? BR_MOUSE_MIDDLE : n == 3 ? BR_MOUSE_X1 : n == 4 ? BR_MOUSE_X2 : BR_MOUSE_BUTTON_COUNT;
            if (b == BR_MOUSE_BUTTON_COUNT) break;
        }
        if (down && (p.y < 0 || p.x < 0)) break;   /* title bar clicks */
        e = br_event_mouse_button(down ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP, b, (float)p.x, (float)p.y);
        mac_post(&e);
        break;
    }
    case NS_SCROLL_WHEEL: {
        br_event e = br_event_make(BR_EVENT_MOUSE_WHEEL);
        double dx = msg_double(ev, "scrollingDeltaX"), dy = msg_double(ev, "scrollingDeltaY");
        if (msg_bool(ev, "hasPreciseScrollingDeltas")) { dx *= 0.1; dy *= 0.1; }  /* trackpad pixels -> lines */
        e.u.wheel.dx = (float)dx;
        e.u.wheel.dy = (float)dy;
        mac_post(&e);
        break;
    }
    default:
        break;
    }
}

/* ---- menu ------------------------------------------------------------------------ */
static id menu_item(const char *title, const char *action, const char *key) {
    return ((id (*)(id, SEL, id, SEL, id))objc_msgSend)(msg_id(cls("NSMenuItem"), "alloc"),
                                                       SEL_("initWithTitle:action:keyEquivalent:"),
                                                       nsstring(title), SEL_(action), nsstring(key));
}

static void build_menu(const char *title) {
    id bar = alloc_init("NSMenu"), app_item = alloc_init("NSMenuItem"), app_menu = alloc_init("NSMenu");
    id item;
    char quit[300];
    msg_v_id(bar, "addItem:", app_item);
    msg_v_id(app_item, "setSubmenu:", app_menu);
    item = menu_item("Hide", "hide:", "h");
    msg_v_id(app_menu, "addItem:", item);
    item = menu_item("Toggle Full Screen", "toggleFullScreen:", "f");
    msg_v_ulong(item, "setKeyEquivalentModifierMask:", NS_MOD_CONTROL | NS_MOD_COMMAND);
    msg_v_id(app_menu, "addItem:", item);
    msg_v_id(app_menu, "addItem:", msg_id(cls("NSMenuItem"), "separatorItem"));
    snprintf(quit, sizeof(quit), "Quit %s", title);
    item = menu_item(quit, "terminate:", "q");
    msg_v_id(app_menu, "addItem:", item);
    msg_v_id(M.app, "setMainMenu:", bar);
}

/* ---- window control -------------------------------------------------------------- */
/* The Dock icon: an NSImage holding one NSBitmapImageRep (premultiplied RGBA). */
static void mac_set_icon(const br_image *icon) {
    id rep, image;
    unsigned char *dst;
    int p;
    if (!M.app) return;
    if (!icon) { msg_v_id(M.app, "setApplicationIconImage:", (id)0); return; }
    rep = ((id (*)(id, SEL, unsigned char **, long, long, long, long, BOOL, BOOL, id, long, long))objc_msgSend)(
        msg_id(cls("NSBitmapImageRep"), "alloc"),
        SEL_("initWithBitmapDataPlanes:pixelsWide:pixelsHigh:bitsPerSample:samplesPerPixel:hasAlpha:isPlanar:colorSpaceName:bytesPerRow:bitsPerPixel:"),
        (unsigned char **)0, (long)icon->width, (long)icon->height, 8L, 4L, YES, NO,
        nsstring("NSDeviceRGBColorSpace"), (long)icon->width * 4, 32L);
    if (!rep) return;
    dst = ((unsigned char *(*)(id, SEL))objc_msgSend)(rep, SEL_("bitmapData"));
    for (p = 0; dst && p < icon->width * icon->height; ++p) {
        br_color c = icon->pixels[p];
        dst[p * 4 + 0] = (unsigned char)((c.r * c.a + 127) / 255);
        dst[p * 4 + 1] = (unsigned char)((c.g * c.a + 127) / 255);
        dst[p * 4 + 2] = (unsigned char)((c.b * c.a + 127) / 255);
        dst[p * 4 + 3] = c.a;
    }
    {
        CGSize size;
        size.width = (CGFloat)icon->width;
        size.height = (CGFloat)icon->height;
        image = ((id (*)(id, SEL, CGSize))objc_msgSend)(msg_id(cls("NSImage"), "alloc"), SEL_("initWithSize:"), size);
    }
    msg_v_id(image, "addRepresentation:", rep);
    msg_v_id(M.app, "setApplicationIconImage:", image);
    msg_v(rep, "release");
    msg_v(image, "release");
}

/* NSOpenPanel in folder mode, run modally. */
static int mac_pick_folder(const char *title, const char *start, char *out, size_t size) {
    id panel = msg_id(cls("NSOpenPanel"), "openPanel"), url, path;
    long response;
    const char *utf8;
    if (!panel) return -1;
    msg_v_bool(panel, "setCanChooseDirectories:", YES);
    msg_v_bool(panel, "setCanChooseFiles:", NO);
    msg_v_bool(panel, "setAllowsMultipleSelection:", NO);
    msg_v_bool(panel, "setCanCreateDirectories:", YES);
    msg_v_id(panel, "setMessage:", nsstring(title));
    msg_v_id(panel, "setPrompt:", nsstring("Open"));
    if (start && start[0])
        msg_v_id(panel, "setDirectoryURL:", msg_id_id(cls("NSURL"), "fileURLWithPath:", nsstring(start)));
    response = msg_long(panel, "runModal");
    if (M.window) msg_v_id(M.window, "makeKeyAndOrderFront:", (id)0);
    if (response != 1 /* NSModalResponseOK */) return 0;
    url = msg_id(panel, "URL");
    path = url ? msg_id(url, "path") : (id)0;
    utf8 = path ? msg_cstr(path, "UTF8String") : NULL;
    if (!utf8) return 0;
    snprintf(out, size, "%s", utf8);
    return 1;
}

static bool mac_launch(const char *command_line) {       /* through the shell, in the background */
    char cmd[2100];
    snprintf(cmd, sizeof(cmd), "(%s) >/dev/null 2>&1 &", command_line);
    return system(cmd) == 0;
}

static bool mac_open_path(const char *path) {
    id url = msg_id_id(cls("NSURL"), "fileURLWithPath:", nsstring(path));
    id ws = msg_id(cls("NSWorkspace"), "sharedWorkspace");
    return url && ws && ((BOOL (*)(id, SEL, id))objc_msgSend)(ws, SEL_("openURL:"), url);
}

static void mac_set_title(const char *title) { msg_v_id(M.window, "setTitle:", nsstring(title)); }

static void mac_set_window_size(int w, int h) {
    CGSize s;
    if (M.fullscreen) return;
    if (w < M.cfg.min_width) w = M.cfg.min_width;
    if (h < M.cfg.min_height) h = M.cfg.min_height;
    s.width = w;
    s.height = h;
    msg_v_size(M.window, "setContentSize:", s);
    post_resize();
}

static void mac_set_fullscreen(bool fs) {
    if (fs == M.fullscreen) return;
    msg_v_id(M.window, "toggleFullScreen:", (id)0);   /* the delegate reports the result */
}

static void mac_set_cursor_visible(bool visible) {
    M.cursor_hidden = !visible;
    apply_cursor();
}

/* ---- lifecycle ------------------------------------------------------------------- */
static bool mac_init(const br_platform_config *cfg) {
    void *pool;
    CGRect r;
    unsigned long style;
    id layer;
    memset(&M, 0, sizeof(M));
    M.cfg = *cfg;
    mach_timebase_info(&M.timebase);
    pool = objc_autoreleasePoolPush();
    register_classes();

    M.app = msg_id(cls("NSApplication"), "sharedApplication");
    msg_v_long(M.app, "setActivationPolicy:", NS_ACTIVATION_POLICY_REGULAR);
    M.app_delegate = alloc_init("BrontoAppDelegate");
    msg_v_id(M.app, "setDelegate:", M.app_delegate);
    build_menu(cfg->title ? cfg->title : "Bronto");
    msg_v(M.app, "finishLaunching");

    r.origin.x = r.origin.y = 0;
    r.size.width = cfg->width > 0 ? cfg->width : 960;
    r.size.height = cfg->height > 0 ? cfg->height : 540;
    style = NS_WINDOW_TITLED | NS_WINDOW_CLOSABLE | NS_WINDOW_MINIATURIZABLE |
            (cfg->resizable ? NS_WINDOW_RESIZABLE : 0ul);
    M.window = ((id (*)(id, SEL, CGRect, unsigned long, unsigned long, BOOL))objc_msgSend)(
        msg_id(cls("NSWindow"), "alloc"), SEL_("initWithContentRect:styleMask:backing:defer:"),
        r, style, NS_BACKING_BUFFERED, NO);
    if (!M.window) { BR_ERROR("Cocoa: could not create window"); objc_autoreleasePoolPop(pool); return false; }
    msg_v_bool(M.window, "setReleasedWhenClosed:", NO);
    msg_v_bool(M.window, "setAcceptsMouseMovedEvents:", YES);
    msg_v_bool(M.window, "setRestorable:", NO);
    msg_v_ulong(M.window, "setCollectionBehavior:", NS_COLLECTION_FULLSCREEN_PRIMARY);
    if (cfg->min_width > 0 && cfg->min_height > 0) {
        CGSize min;
        min.width = cfg->min_width;
        min.height = cfg->min_height;
        msg_v_size(M.window, "setContentMinSize:", min);
    }
    mac_set_title(cfg->title ? cfg->title : "Bronto");

    M.view = ((id (*)(id, SEL, CGRect))objc_msgSend)(msg_id(cls("BrontoView"), "alloc"), SEL_("initWithFrame:"), r);
    msg_v_bool(M.view, "setWantsLayer:", YES);
    layer = msg_id(M.view, "layer");
    msg_v_ptr(layer, "setBackgroundColor:", CGColorGetConstantColor(kCGColorBlack));
    M.image_layer = alloc_init("CALayer");
    msg_v_id(M.image_layer, "setMagnificationFilter:", kCAFilterNearest);
    msg_v_id(M.image_layer, "setMinificationFilter:", kCAFilterNearest);
    msg_v_id(layer, "addSublayer:", M.image_layer);

    M.tracking = ((id (*)(id, SEL, CGRect, unsigned long, id, id))objc_msgSend)(
        msg_id(cls("NSTrackingArea"), "alloc"), SEL_("initWithRect:options:owner:userInfo:"), r,
        NS_TRACKING_ENTERED_EXITED | NS_TRACKING_ACTIVE_ALWAYS | NS_TRACKING_IN_VISIBLE_RECT, M.view, (id)0);
    msg_v_id(M.view, "addTrackingArea:", M.tracking);

    M.window_delegate = alloc_init("BrontoWindowDelegate");
    msg_v_id(M.window, "setDelegate:", M.window_delegate);
    msg_v_id(M.window, "setContentView:", M.view);
    msg_id_id(M.window, "makeFirstResponder:", M.view);
    msg_v(M.window, "center");
    if (!cfg->hidden) {
        msg_v_id(M.window, "makeKeyAndOrderFront:", (id)0);
        msg_v_bool(M.app, "activateIgnoringOtherApps:", YES);
    }
    M.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    M.initialized = true;
    M.pending_count = 0;    /* setup-time notifications are not news */
    if (cfg->fullscreen) mac_set_fullscreen(true);
    if (cfg->audio_sample_rate > 0) ca_open(cfg->audio_sample_rate, cfg->audio_latency_ms);
    objc_autoreleasePoolPop(pool);
    {
        int w, h;
        mac_get_window_size(&w, &h);
        BR_INFO("Cocoa: %dx%d pixel window (%.1fx backing scale)", w, h, mac_scale());
    }
    return true;
}

static void mac_shutdown(void) {
    void *pool;
    ca_close();
    gc_shutdown();
    if (!M.initialized) return;
    pool = objc_autoreleasePoolPush();
    if (M.cursor_hidden_now) msg_v(cls("NSCursor"), "unhide");
    msg_v_id(M.window, "setDelegate:", (id)0);
    msg_v_id(M.window, "orderOut:", (id)0);
    msg_v(M.window, "close");
    msg_v(M.image_layer, "release");
    msg_v(M.tracking, "release");
    msg_v(M.view, "release");
    msg_v(M.window, "release");
    msg_v(M.window_delegate, "release");
    CGColorSpaceRelease(M.colorspace);
    objc_autoreleasePoolPop(pool);
    memset(&M, 0, sizeof(M));
}

static void mac_poll(br_events *events) {
    void *pool = objc_autoreleasePoolPush();
    id distant_past = msg_id(cls("NSDate"), "distantPast");
    int i;
    M.events = events;
    for (i = 0; i < M.pending_count; ++i) br_events_post(events, &M.pending[i]);
    M.pending_count = 0;
    for (;;) {
        id ev = ((id (*)(id, SEL, unsigned long long, id, id, BOOL))objc_msgSend)(
            M.app, SEL_("nextEventMatchingMask:untilDate:inMode:dequeue:"),
            NS_EVENT_MASK_ANY, distant_past, NSDefaultRunLoopMode, YES);
        if (!ev) break;
        if (msg_id(ev, "window") == M.window) translate_event(ev);
        msg_v_id(M.app, "sendEvent:", ev);
    }
    msg_v(M.app, "updateWindows");
    gc_poll(events);
    M.events = NULL;   /* delegate callbacks outside poll are buffered */
    objc_autoreleasePoolPop(pool);
}

static void mac_present(const br_image *fb, br_recti vp) {
    void *pool;
    CFDataRef data;
    CGDataProviderRef provider;
    CGImageRef image;
    CGRect frame, bounds;
    double s;
    if (M.minimized) return;
    pool = objc_autoreleasePoolPush();
    /* Copy the pixels: Core Animation reads the image after we return. */
    data = CFDataCreate(NULL, (const UInt8 *)fb->pixels, (CFIndex)((size_t)fb->width * (size_t)fb->height * 4u));
    provider = CGDataProviderCreateWithCFData(data);
    image = CGImageCreate((size_t)fb->width, (size_t)fb->height, 8, 32, (size_t)fb->width * 4u, M.colorspace,
                          (CGBitmapInfo)kCGImageAlphaNoneSkipLast, provider, NULL, false, kCGRenderingIntentDefault);
    s = mac_scale();
    bounds = msg_rect(M.view, "bounds");
    frame.origin.x = (double)vp.x / s;
    frame.size.width = (double)vp.w / s;
    frame.size.height = (double)vp.h / s;
    frame.origin.y = bounds.size.height - ((double)vp.y + (double)vp.h) / s;   /* y-up layer space */

    msg_v(cls("CATransaction"), "begin");
    msg_v_bool(cls("CATransaction"), "setDisableActions:", YES);   /* no implicit animations */
    msg_v_ptr(M.image_layer, "setContents:", image);
    msg_v_rect(M.image_layer, "setFrame:", frame);
    msg_v_double(M.image_layer, "setContentsScale:", s);
    msg_v(cls("CATransaction"), "commit");
    msg_v(cls("CATransaction"), "flush");

    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CFRelease(data);
    objc_autoreleasePoolPop(pool);
}

static double mac_time(void) {
    uint64_t t = mach_absolute_time();
    return (double)t * (double)M.timebase.numer / (double)M.timebase.denom * 1e-9;
}

static void mac_sleep(double s) {
    struct timespec ts;
    if (s <= 0) return;
    ts.tv_sec = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

static uint32_t mac_audio_wanted(void) { return ca_wanted(); }
static void mac_audio_submit(const int16_t *d, uint32_t n) { ca_submit(d, n); }
static float mac_get_window_scale(void) { return (float)mac_scale(); }

/* Rumble needs Core Haptics on macOS - not implemented yet. */
static bool mac_set_gamepad_rumble(int pad, float lo, float hi) { BR_UNUSED(pad); BR_UNUSED(lo); BR_UNUSED(hi); return false; }

/* ---- clipboard (NSPasteboard) ------------------------------------------------------ */

#define MAC_PASTEBOARD_TEXT "public.utf8-plain-text"     /* NSPasteboardTypeString */

static bool mac_clipboard_set(const char *text) {
    void *pool = objc_autoreleasePoolPush();
    id pb = msg_id(cls("NSPasteboard"), "generalPasteboard");
    BOOL ok = NO;
    if (pb) {
        msg_long(pb, "clearContents");
        ok = ((BOOL (*)(id, SEL, id, id))objc_msgSend)(pb, SEL_("setString:forType:"), nsstring(text), nsstring(MAC_PASTEBOARD_TEXT));
    }
    objc_autoreleasePoolPop(pool);
    return ok ? true : false;
}

static char *mac_clipboard_get(void) {
    void *pool = objc_autoreleasePoolPush();
    id pb = msg_id(cls("NSPasteboard"), "generalPasteboard");
    id str = pb ? msg_id_id(pb, "stringForType:", nsstring(MAC_PASTEBOARD_TEXT)) : NULL;
    const char *utf8 = str ? msg_cstr(str, "UTF8String") : NULL;
    char *out = NULL;
    if (utf8) {
        size_t n = strlen(utf8);
        if ((out = (char *)br_alloc(n + 1)) != NULL) memcpy(out, utf8, n + 1);
    }
    objc_autoreleasePoolPop(pool);                       /* (utf8 belongs to the pool: copied first) */
    return out;
}

static const br_platform k_macos = {
    "cocoa", mac_init, mac_shutdown, mac_poll, mac_present, mac_time, mac_sleep,
    mac_audio_wanted, mac_audio_submit,
    mac_get_window_size, mac_get_window_scale, mac_set_title, mac_set_window_size,
    mac_set_fullscreen, mac_set_cursor_visible, mac_set_icon, mac_pick_folder, mac_open_path, mac_launch,
    mac_clipboard_set, mac_clipboard_get,
    mac_set_gamepad_rumble,
    NULL,
    NULL                                          /* present_overlay: the engine draws into the framebuffer */
};

const br_platform *br_platform_native(void) { return &k_macos; }
