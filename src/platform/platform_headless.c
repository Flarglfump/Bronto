/*
 * Headless platform backend: no window, no audio device.
 *
 * Time is virtual and advances a fixed step on every poll, so a run is
 * fully deterministic - the same inputs always produce the same frames.
 * The window is simulated (size, scale, fullscreen, cursor, title) so the
 * engine's window management can be tested without a display.
 */
#include "bronto/platform.h"

#include <stdio.h>
#include <string.h>

#define HEADLESS_MAX_INJECTED 1024

static struct {
    br_platform_config cfg;
    double now, step, slept;
    uint64_t polls, quit_after, presents;
    br_event injected[HEADLESS_MAX_INJECTED];
    uint32_t injected_count;
    const br_image *last_frame;
    br_recti last_viewport;
    int16_t *capture;
    uint32_t capture_cap, captured;
    br_image icon;
    char folder_answer[512], opened[512], launched[1024];
    bool folder_set, folder_cancel, folder_none;
    double audio_debt;   /* fractional frames owed to the virtual device */
    int win_w, win_h;    /* pixels */
    int saved_w, saved_h;
    float scale;
    bool fullscreen, cursor_visible;
    char title[256];
    br_gamepad_raw pads[BR_MAX_GAMEPADS];
    br_gamepad_tracker pad_tracker;
    float rumble[BR_MAX_GAMEPADS][2];
} g_hl;  /* zero-initialised; step 0 means the default 1/60 s */

static double hl_step(void) { return g_hl.step > 0 ? g_hl.step : 1.0 / 60.0; }
static float hl_scale(void) { return g_hl.scale > 0 ? g_hl.scale : 1.0f; }

static void hl_queue(const br_event *e) {
    if (g_hl.injected_count < HEADLESS_MAX_INJECTED) g_hl.injected[g_hl.injected_count++] = *e;
}

static void hl_post_resize(void) {
    br_event e = br_event_make(BR_EVENT_WINDOW_RESIZED);
    e.u.resize.width = g_hl.win_w;
    e.u.resize.height = g_hl.win_h;
    hl_queue(&e);
}

static bool hl_init(const br_platform_config *cfg) {
    g_hl.cfg = *cfg;
    g_hl.now = 0;
    g_hl.slept = 0;
    g_hl.polls = 0;
    g_hl.presents = 0;
    g_hl.last_frame = NULL;
    g_hl.audio_debt = 0;
    g_hl.win_w = (int)((float)cfg->width * hl_scale() + 0.5f);
    g_hl.win_h = (int)((float)cfg->height * hl_scale() + 0.5f);
    g_hl.fullscreen = cfg->fullscreen;
    g_hl.cursor_visible = true;
    strncpy(g_hl.title, cfg->title ? cfg->title : "", sizeof(g_hl.title) - 1);
    return true;
}

static void hl_shutdown(void) {
    br_image_destroy(&g_hl.icon);
    g_hl.folder_set = false;
    g_hl.opened[0] = 0;
    g_hl.launched[0] = 0;
    g_hl.last_frame = NULL;
    g_hl.injected_count = 0;
    g_hl.quit_after = 0;
    g_hl.capture = NULL;   /* captured count is kept so tests can inspect it */
    g_hl.capture_cap = 0;
    g_hl.scale = 0;
    memset(g_hl.pads, 0, sizeof(g_hl.pads));
    memset(&g_hl.pad_tracker, 0, sizeof(g_hl.pad_tracker));
    memset(g_hl.rumble, 0, sizeof(g_hl.rumble));
}

static void hl_poll(br_events *events) {
    uint32_t i;
    g_hl.now += hl_step();
    g_hl.polls++;
    for (i = 0; i < g_hl.injected_count; ++i) {
        br_event e = g_hl.injected[i];
        e.timestamp = g_hl.now;
        br_events_post(events, &e);
    }
    g_hl.injected_count = 0;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) br_gamepads_sync(&g_hl.pad_tracker, (int)i, &g_hl.pads[i], events);
    if (g_hl.quit_after && g_hl.polls >= g_hl.quit_after) {
        br_event q = br_event_make(BR_EVENT_QUIT);
        br_events_post(events, &q);
    }
}

static void hl_present(const br_image *fb, br_recti viewport) {
    g_hl.last_frame = fb;
    g_hl.last_viewport = viewport;
    g_hl.presents++;
}
static double hl_time(void) { return g_hl.now; }
static void hl_sleep(double s) { if (s > 0) g_hl.slept += s; }

static uint32_t hl_audio_wanted(void) {
    double want;
    if (!g_hl.cfg.audio_sample_rate) return 0;
    /* Behave like a device consuming audio in real (virtual) time. */
    want = hl_step() * (double)g_hl.cfg.audio_sample_rate + g_hl.audio_debt;
    g_hl.audio_debt = want - (double)(uint32_t)want;
    return (uint32_t)want;
}

static void hl_audio_submit(const int16_t *data, uint32_t frames) {
    uint32_t n;
    if (!g_hl.capture) return;
    n = frames;
    if (g_hl.captured + n > g_hl.capture_cap) n = g_hl.capture_cap - g_hl.captured;
    memcpy(g_hl.capture + (size_t)g_hl.captured * 2, data, (size_t)n * 2 * sizeof(int16_t));
    g_hl.captured += n;
}

static void hl_get_window_size(int *w, int *h) { *w = g_hl.win_w; *h = g_hl.win_h; }
static float hl_get_window_scale(void) { return hl_scale(); }
static void hl_set_icon(const br_image *icon) {
    br_image_destroy(&g_hl.icon);
    if (icon && br_image_init(&g_hl.icon, icon->width, icon->height))
        memcpy(g_hl.icon.pixels, icon->pixels, (size_t)icon->width * (size_t)icon->height * sizeof(br_color));
}
const br_image *br_headless_icon(void) { return g_hl.icon.pixels ? &g_hl.icon : NULL; }

void br_headless_set_folder_answer(const char *path, bool available) {
    g_hl.folder_set = true;
    g_hl.folder_none = !available;
    g_hl.folder_cancel = path == NULL;
    snprintf(g_hl.folder_answer, sizeof(g_hl.folder_answer), "%s", path ? path : "");
}

static int hl_pick_folder(const char *title, const char *start, char *out, size_t size) {
    BR_UNUSED(title); BR_UNUSED(start);
    if (!g_hl.folder_set || g_hl.folder_none) return -1;     /* like a desktop without a dialog */
    if (g_hl.folder_cancel) return 0;
    snprintf(out, size, "%s", g_hl.folder_answer);
    return 1;
}

static bool hl_open_path(const char *path) { snprintf(g_hl.opened, sizeof(g_hl.opened), "%s", path); return true; }
const char *br_headless_last_opened(void) { return g_hl.opened; }
static bool hl_launch(const char *cmd) { snprintf(g_hl.launched, sizeof(g_hl.launched), "%s", cmd); return true; }
const char *br_headless_last_launched(void) { return g_hl.launched; }

static void hl_set_title(const char *t) { strncpy(g_hl.title, t, sizeof(g_hl.title) - 1); }

static void hl_set_window_size(int w, int h) {
    if (g_hl.fullscreen) return;
    if (w < g_hl.cfg.min_width) w = g_hl.cfg.min_width;
    if (h < g_hl.cfg.min_height) h = g_hl.cfg.min_height;
    g_hl.win_w = (int)((float)w * hl_scale() + 0.5f);
    g_hl.win_h = (int)((float)h * hl_scale() + 0.5f);
    hl_post_resize();
}

static void hl_set_fullscreen(bool fs) {
    br_event e;
    if (fs == g_hl.fullscreen) return;
    g_hl.fullscreen = fs;
    if (fs) {   /* pretend the monitor is 1920x1080 */
        g_hl.saved_w = g_hl.win_w;
        g_hl.saved_h = g_hl.win_h;
        g_hl.win_w = 1920;
        g_hl.win_h = 1080;
    } else {
        g_hl.win_w = g_hl.saved_w;
        g_hl.win_h = g_hl.saved_h;
    }
    e = br_event_make(BR_EVENT_WINDOW_FULLSCREEN);
    e.u.window.fullscreen = fs;
    e.u.window.width = g_hl.win_w;
    e.u.window.height = g_hl.win_h;
    hl_queue(&e);
    hl_post_resize();
}

static void hl_set_cursor_visible(bool v) { g_hl.cursor_visible = v; }

static bool hl_set_gamepad_rumble(int pad, float lo, float hi) {
    if (pad < 0 || pad >= BR_MAX_GAMEPADS || !g_hl.pads[pad].connected) return false;
    g_hl.rumble[pad][0] = lo;
    g_hl.rumble[pad][1] = hi;
    return true;
}

static const br_platform k_headless = {
    "headless", hl_init, hl_shutdown, hl_poll, hl_present, hl_time, hl_sleep,
    hl_audio_wanted, hl_audio_submit,
    hl_get_window_size, hl_get_window_scale, hl_set_title, hl_set_window_size,
    hl_set_fullscreen, hl_set_cursor_visible, hl_set_icon, hl_pick_folder, hl_open_path, hl_launch,
    NULL, NULL,                                   /* clipboard: the engine keeps a private one */
    hl_set_gamepad_rumble,
    NULL
};

const br_platform *br_platform_headless(void) { return &k_headless; }

void br_headless_set_timestep(double seconds) { g_hl.step = seconds > 0 ? seconds : 1.0 / 60.0; }
void br_headless_inject(const br_event *e) { hl_queue(e); }
void br_headless_quit_after(uint64_t polls) { g_hl.quit_after = polls; }
const br_image *br_headless_last_frame(void) { return g_hl.last_frame; }
br_recti br_headless_last_viewport(void) { return g_hl.last_viewport; }
uint64_t br_headless_present_count(void) { return g_hl.presents; }
double br_headless_slept(void) { return g_hl.slept; }
bool br_headless_cursor_visible(void) { return g_hl.cursor_visible; }
const char *br_headless_title(void) { return g_hl.title; }

void br_headless_set_gamepad(int pad, const br_gamepad_raw *state) {
    if (pad >= 0 && pad < BR_MAX_GAMEPADS) g_hl.pads[pad] = *state;
}

void br_headless_get_rumble(int pad, float *lo, float *hi) {
    *lo = *hi = 0;
    if (pad >= 0 && pad < BR_MAX_GAMEPADS) { *lo = g_hl.rumble[pad][0]; *hi = g_hl.rumble[pad][1]; }
}

void br_headless_resize(int w, int h) {
    g_hl.win_w = w;
    g_hl.win_h = h;
    hl_post_resize();
}

void br_headless_set_scale(float scale) {
    br_event e = br_event_make(BR_EVENT_WINDOW_SCALE_CHANGED);
    g_hl.scale = scale;
    e.u.window.scale = scale;
    hl_queue(&e);
}

void br_headless_capture_audio(int16_t *buffer, uint32_t capacity_frames) {
    g_hl.capture = buffer;
    g_hl.capture_cap = buffer ? capacity_frames : 0;
    g_hl.captured = 0;
}
uint32_t br_headless_captured_frames(void) { return g_hl.captured; }
