/*
 * Bronto Engine - web backend (included by platform_native.c when building
 * for WebAssembly): the game runs in a browser tab.
 *
 *   window      a <canvas> filling the page (or the frame it's embedded
 *               in); the framebuffer is drawn into it, scaled, each frame
 *   input       keyboard, mouse and wheel events from the page; controllers
 *               through the Gamepad API (with rumble where the browser has it)
 *   audio       WebAudio: each frame's mix is queued to play right after
 *               the last one
 *   loop        the browser owns it: run_loop hands the frame function to
 *               requestAnimationFrame and leaves main() (see bw.h)
 *   clipboard   copying goes to the system clipboard; pasting reads what the
 *               page last saw pasted (browsers only give it out on a paste)
 *
 * The other half is tools/web/bronto.js: the functions imported here, and
 * the bronto_web_* functions exported for it to call.
 */
#include "web/bw.h"
#include "bronto/memory.h"

#include <stddef.h>
#include <string.h>

/* ---- imported from bronto.js ---- */
BW_IMPORT(canvas_width)   int    bw_js_canvas_width(void);           /* device pixels */
BW_IMPORT(canvas_height)  int    bw_js_canvas_height(void);
BW_IMPORT(pixel_ratio)    double bw_js_pixel_ratio(void);
BW_IMPORT(present)        void   bw_js_present(const void *rgba, int width, int height, int vx, int vy, int vw, int vh);
BW_IMPORT(set_title)      void   bw_js_set_title(const char *title);
BW_IMPORT(set_size)       void   bw_js_set_size(int width_points, int height_points);
BW_IMPORT(set_fullscreen) void   bw_js_set_fullscreen(int on);
BW_IMPORT(set_cursor)     void   bw_js_set_cursor(int visible);
BW_IMPORT(set_icon)       void   bw_js_set_icon(const void *rgba, int width, int height);
BW_IMPORT(open_url)       int    bw_js_open_url(const char *url);
BW_IMPORT(clipboard_set)  void   bw_js_clipboard_set(const char *utf8);
BW_IMPORT(audio_open)     int    bw_js_audio_open(int sample_rate, int latency_ms);
BW_IMPORT(audio_close)    void   bw_js_audio_close(void);
BW_IMPORT(audio_wanted)   int    bw_js_audio_wanted(void);           /* frames to queue now */
BW_IMPORT(audio_submit)   void   bw_js_audio_submit(const int16_t *interleaved, int frames);
BW_IMPORT(poll_gamepads)  void   bw_js_poll_gamepads(void);          /* calls bronto_web_gamepad for each */
BW_IMPORT(rumble)         int    bw_js_rumble(int pad, double low, double high);
BW_IMPORT(run_loop)       void   bw_js_run_loop(void);               /* starts calling bronto_web_frame */
BW_IMPORT(overlay)        void   bw_js_overlay(const void *items, int count);   /* br_overlay_item[count], over the canvas */
BW_IMPORT(set_mouse_lock) void   bw_js_set_mouse_lock(int on);       /* pointer lock: now if allowed, else on the next click */

static struct {
    bool open;
    int audio_rate;
    /* events from the page, waiting for the next poll */
    br_event *queue;
    size_t count, cap;
    br_gamepad_tracker pad_tracker;
    br_gamepad_raw pads[BR_MAX_GAMEPADS];
    char pad_name[BR_GAMEPAD_NAME_MAX];
    float mouse_x, mouse_y;
    char *clipboard;                 /* the last text pasted into the page */
    br_frame_fn frame;
    void *frame_user;
    double wake;                     /* sleep(): the next frame not before this */
    bool stopped;
} W;

static void web_queue(const br_event *e) {
    if (W.count == W.cap) {
        size_t cap = W.cap ? W.cap * 2 : 64;
        br_event *grown = (br_event *)br_realloc(W.queue, cap * sizeof(br_event));
        if (!grown) return;
        W.queue = grown;
        W.cap = cap;
    }
    W.queue[W.count++] = *e;
}

static double web_time(void) { return bw_js_now() / 1000.0; }

/* ==== the platform ======================================================== */
static bool web_init(const br_platform_config *cfg) {
    memset(&W, 0, sizeof(W));
    W.open = true;
    bw_js_set_title(cfg->title ? cfg->title : "Bronto");
    bw_js_set_size(cfg->width, cfg->height);
    if (cfg->audio_sample_rate > 0 && bw_js_audio_open(cfg->audio_sample_rate, cfg->audio_latency_ms))
        W.audio_rate = cfg->audio_sample_rate;
    if (cfg->fullscreen) bw_js_set_fullscreen(1);
    return true;
}

static void web_shutdown(void) {
    int i;
    if (W.audio_rate) bw_js_audio_close();
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) if (W.pads[i].connected) bw_js_rumble(i, 0, 0);
    br_free(W.queue);
    br_free(W.clipboard);
    W.queue = NULL;
    W.clipboard = NULL;
    W.count = W.cap = 0;
    W.open = false;
}

static void web_poll(br_events *events) {
    size_t i;
    for (i = 0; i < W.count; ++i) br_events_post(events, &W.queue[i]);
    W.count = 0;
    bw_js_poll_gamepads();
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) br_gamepads_sync(&W.pad_tracker, (int)i, &W.pads[i], events);
}

static void web_present(const br_image *fb, br_recti vp) {
    bw_js_present(fb->pixels, fb->width, fb->height, vp.x, vp.y, vp.w, vp.h);
}

/* (bronto.js reads the items by these offsets) */
typedef char bw_overlay_layout[(sizeof(br_overlay_item) == 40 && offsetof(br_overlay_item, fill) == 20 &&
                                offsetof(br_overlay_item, text) == 32) ? 1 : -1];
static void web_present_overlay(const br_overlay_item *items, int count) { bw_js_overlay(items, count); }
static void web_set_mouse_locked(bool locked) { bw_js_set_mouse_lock(locked ? 1 : 0); }

static void web_sleep(double seconds) { if (seconds > 0) W.wake = web_time() + seconds; }

static uint32_t web_audio_wanted(void) {
    int n = W.audio_rate ? bw_js_audio_wanted() : 0;
    return n > 0 ? (uint32_t)n : 0;
}
static void web_audio_submit(const int16_t *data, uint32_t frames) {
    if (W.audio_rate && frames) bw_js_audio_submit(data, (int)frames);
}

static void web_window_size(int *w, int *h) { *w = bw_js_canvas_width(); *h = bw_js_canvas_height(); }
static float web_window_scale(void) { double r = bw_js_pixel_ratio(); return r > 0 ? (float)r : 1.0f; }
static void web_set_title(const char *t) { bw_js_set_title(t ? t : ""); }
static void web_set_window_size(int w, int h) { bw_js_set_size(w, h); }
static void web_set_fullscreen(bool on) { bw_js_set_fullscreen(on ? 1 : 0); }
static void web_set_cursor(bool visible) { bw_js_set_cursor(visible ? 1 : 0); }
static void web_set_icon(const br_image *icon) { if (icon) bw_js_set_icon(icon->pixels, icon->width, icon->height); }
static int web_pick_folder(const char *title, const char *start, char *out, size_t size) {
    BR_UNUSED(title); BR_UNUSED(start); BR_UNUSED(out); BR_UNUSED(size);
    return -1;                                              /* a page can't browse the computer's folders */
}
static bool web_open_path(const char *path) { return bw_js_open_url(path) != 0; }   /* web addresses open in a new tab */
static bool web_launch(const char *cmd) { BR_UNUSED(cmd); return false; }

static bool web_clipboard_set(const char *utf8) {
    size_t n = strlen(utf8);
    char *copy = (char *)br_alloc(n + 1);
    if (!copy) return false;
    memcpy(copy, utf8, n + 1);
    br_free(W.clipboard);
    W.clipboard = copy;                                     /* pasting inside the game works even when */
    bw_js_clipboard_set(utf8);                              /* the browser won't let the page write it */
    return true;
}
static char *web_clipboard_get(void) {
    size_t n;
    char *copy;
    if (!W.clipboard) return NULL;
    n = strlen(W.clipboard);
    if ((copy = (char *)br_alloc(n + 1)) != NULL) memcpy(copy, W.clipboard, n + 1);
    return copy;
}

static bool web_rumble(int pad, float lo, float hi) {
    if (pad < 0 || pad >= BR_MAX_GAMEPADS || !W.pads[pad].connected) return false;
    return bw_js_rumble(pad, lo, hi) != 0;
}

static void web_run_loop(br_frame_fn frame, void *user) {
    W.frame = frame;
    W.frame_user = user;
    W.stopped = false;
    W.wake = 0;
    bw_js_run_loop();
    bw_return_to_browser();                                 /* main() is over; frames come from the page */
}

static const br_platform k_web = {
    "web", web_init, web_shutdown, web_poll, web_present, web_time, web_sleep,
    web_audio_wanted, web_audio_submit,
    web_window_size, web_window_scale, web_set_title, web_set_window_size, web_set_fullscreen, web_set_cursor, web_set_icon,
    web_pick_folder, web_open_path, web_launch,
    web_clipboard_set, web_clipboard_get,
    web_rumble,
    web_run_loop,
    web_present_overlay,
    web_set_mouse_locked
};

const br_platform *br_platform_native(void) { return &k_web; }

/* ==== called by the page ================================================== */
BW_EXPORT(bronto_web_frame) int bronto_web_frame(void);
BW_EXPORT(bronto_web_key) void bronto_web_key(int key, int scancode, int mods, int down, int repeat);
BW_EXPORT(bronto_web_text) void bronto_web_text(uint32_t codepoint);
BW_EXPORT(bronto_web_mouse_move) void bronto_web_mouse_move(float x, float y);
BW_EXPORT(bronto_web_mouse_motion) void bronto_web_mouse_motion(float dx, float dy);
BW_EXPORT(bronto_web_mouse_button) void bronto_web_mouse_button(int button, int down, float x, float y);
BW_EXPORT(bronto_web_wheel) void bronto_web_wheel(float dx, float dy);
BW_EXPORT(bronto_web_touch) void bronto_web_touch(int phase, int id, float x, float y);
BW_EXPORT(bronto_web_mouse_inside) void bronto_web_mouse_inside(int inside);
BW_EXPORT(bronto_web_resized) void bronto_web_resized(void);
BW_EXPORT(bronto_web_scale_changed) void bronto_web_scale_changed(void);
BW_EXPORT(bronto_web_focus) void bronto_web_focus(int gained);
BW_EXPORT(bronto_web_fullscreen) void bronto_web_fullscreen(int on);
BW_EXPORT(bronto_web_close) void bronto_web_close(void);
BW_EXPORT(bronto_web_paste) void bronto_web_paste(const char *utf8);
BW_EXPORT(bronto_web_gamepad_name) char *bronto_web_gamepad_name(void);
BW_EXPORT(bronto_web_gamepad) void bronto_web_gamepad(int pad, int connected, uint32_t buttons,
                                                      float lx, float ly, float rx, float ry, float lt, float rt);

/* One animation frame: 1 to keep going, 0 when the game has ended. */
int bronto_web_frame(void) {
    if (!W.frame || W.stopped) return 0;
    if (W.wake > 0 && web_time() < W.wake - 0.002) return 1;   /* faster screens: keep to max_fps */
    W.wake = 0;
    if (!W.frame(W.frame_user)) {
        W.stopped = true;
        bw_stdio_flush_all();
        return 0;
    }
    return 1;
}

void bronto_web_key(int key, int scancode, int mods, int down, int repeat) {
    br_event e = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, (br_key)key, (uint32_t)mods, repeat != 0);
    e.u.key.scancode = scancode;
    web_queue(&e);
}

void bronto_web_text(uint32_t codepoint) {
    br_event e = br_event_make(BR_EVENT_TEXT_INPUT);
    e.u.text.codepoint = codepoint;
    web_queue(&e);
}

void bronto_web_mouse_move(float x, float y) {
    br_event e = br_event_mouse_move(x, y, x - W.mouse_x, y - W.mouse_y);
    W.mouse_x = x;
    W.mouse_y = y;
    web_queue(&e);
}

/* Pointer locked (mouse-look): the mouse's own motion, in device pixels; the pointer stays put. */
void bronto_web_mouse_motion(float dx, float dy) {
    br_event e = br_event_mouse_move(W.mouse_x, W.mouse_y, dx, dy);
    web_queue(&e);
}

void bronto_web_mouse_button(int button, int down, float x, float y) {
    br_event e;
    if (button < 0 || button >= BR_MOUSE_BUTTON_COUNT) return;
    e = br_event_mouse_button(down ? BR_EVENT_MOUSE_BUTTON_DOWN : BR_EVENT_MOUSE_BUTTON_UP, (br_mouse_button)button, x, y);
    web_queue(&e);
}

/* A finger: phase 0 down, 1 moved, 2 up (or cancelled). */
void bronto_web_touch(int phase, int id, float x, float y) {
    br_event e = br_event_touch(phase == 0 ? BR_EVENT_TOUCH_DOWN : phase == 1 ? BR_EVENT_TOUCH_MOVE : BR_EVENT_TOUCH_UP, id, x, y);
    web_queue(&e);
}

void bronto_web_wheel(float dx, float dy) {
    br_event e = br_event_make(BR_EVENT_MOUSE_WHEEL);
    e.u.wheel.dx = dx;
    e.u.wheel.dy = dy;
    web_queue(&e);
}

void bronto_web_mouse_inside(int inside) {
    br_event e = br_event_make(inside ? BR_EVENT_MOUSE_ENTER : BR_EVENT_MOUSE_LEAVE);
    web_queue(&e);
}

void bronto_web_resized(void) {
    br_event e = br_event_make(BR_EVENT_WINDOW_RESIZED);
    e.u.resize.width = bw_js_canvas_width();
    e.u.resize.height = bw_js_canvas_height();
    web_queue(&e);
}

void bronto_web_scale_changed(void) {
    br_event e = br_event_make(BR_EVENT_WINDOW_SCALE_CHANGED);
    e.u.window.scale = web_window_scale();
    e.u.window.width = bw_js_canvas_width();
    e.u.window.height = bw_js_canvas_height();
    web_queue(&e);
}

void bronto_web_focus(int gained) {
    br_event e = br_event_make(gained ? BR_EVENT_WINDOW_FOCUS_GAINED : BR_EVENT_WINDOW_FOCUS_LOST);
    web_queue(&e);
}

void bronto_web_fullscreen(int on) {
    br_event e = br_event_make(BR_EVENT_WINDOW_FULLSCREEN);
    e.u.window.fullscreen = on != 0;
    e.u.window.width = bw_js_canvas_width();
    e.u.window.height = bw_js_canvas_height();
    web_queue(&e);
}

void bronto_web_close(void) {
    br_event e = br_event_make(BR_EVENT_QUIT);
    web_queue(&e);
}

void bronto_web_paste(const char *utf8) {
    size_t n = strlen(utf8);
    char *copy = (char *)br_alloc(n + 1);
    if (!copy) return;
    memcpy(copy, utf8, n + 1);
    br_free(W.clipboard);
    W.clipboard = copy;
}

char *bronto_web_gamepad_name(void) { return W.pad_name; }

void bronto_web_gamepad(int pad, int connected, uint32_t buttons, float lx, float ly, float rx, float ry, float lt, float rt) {
    br_gamepad_raw *p;
    if (pad < 0 || pad >= BR_MAX_GAMEPADS) return;
    p = &W.pads[pad];
    p->connected = connected != 0;
    p->buttons = connected ? buttons : 0;
    p->axes[BR_PAD_AXIS_LEFT_X] = lx;
    p->axes[BR_PAD_AXIS_LEFT_Y] = ly;
    p->axes[BR_PAD_AXIS_RIGHT_X] = rx;
    p->axes[BR_PAD_AXIS_RIGHT_Y] = ry;
    p->axes[BR_PAD_AXIS_LEFT_TRIGGER] = lt;
    p->axes[BR_PAD_AXIS_RIGHT_TRIGGER] = rt;
    memcpy(p->name, W.pad_name, sizeof(p->name));
    p->name[sizeof(p->name) - 1] = 0;
}
