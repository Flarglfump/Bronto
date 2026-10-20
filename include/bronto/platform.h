/*
 * Bronto Engine - platform abstraction layer.
 *
 * Everything that touches the operating system goes through this table of
 * function pointers; the core engine never includes an OS header. A backend:
 *
 *   - opens one window and blits the RGBA8 framebuffer into a viewport
 *     rectangle the engine computes (and clears the area around it),
 *   - translates native input/window activity into br_events, with mouse
 *     positions in window *pixels* (the engine maps them to the framebuffer),
 *   - provides a monotonic clock and a sleep,
 *   - accepts interleaved stereo int16 audio pushed from the main thread,
 *   - reads game controllers (XInput, evdev, GameController framework).
 *
 * Backends:
 *   headless - no window/audio device; deterministic virtual clock and a
 *              simulated window (resizable, fullscreen...), event injection
 *              and frame/audio capture. Always available. Tests/servers/CI.
 *   native   - Win32+GDI (Windows), Cocoa (macOS), X11 (Linux, loaded at
 *              run time so no X11 headers or -lX11 are needed to build).
 */
#ifndef BRONTO_PLATFORM_H
#define BRONTO_PLATFORM_H

#include "types.h"
#include "event.h"
#include "render.h"
#include "window.h"

typedef struct br_platform_config {
    const char *title;
    int width, height;          /* initial client size, points */
    int min_width, min_height;  /* points; 0 = no minimum */
    bool resizable;
    bool fullscreen;
    bool hidden;                /* create without showing (tools/tests) */
    int audio_sample_rate;      /* 0 = no audio device */
    int audio_latency_ms;       /* target output buffering */
} br_platform_config;

typedef bool (*br_frame_fn)(void *user);   /* return false to stop the loop */

typedef struct br_platform {
    const char *name;
    bool   (*init)(const br_platform_config *cfg);
    void   (*shutdown)(void);
    void   (*poll_events)(br_events *events);
    /* Blit `framebuffer` scaled into `viewport` (window pixels); everything
     * outside the viewport is cleared to black. */
    void   (*present)(const br_image *framebuffer, br_recti viewport);
    double (*time)(void);                     /* seconds, monotonic */
    void   (*sleep)(double seconds);
    /* Stereo frames to submit now to bring the device's queue back up to
     * the target latency (0 when there is no device). */
    uint32_t (*audio_frames_wanted)(void);
    void   (*audio_submit)(const int16_t *interleaved, uint32_t frames);

    /* ---- window ---- */
    void   (*get_window_size)(int *width_px, int *height_px);
    float  (*get_window_scale)(void);         /* pixels per point */
    void   (*set_title)(const char *title);
    void   (*set_window_size)(int width_points, int height_points);
    void   (*set_fullscreen)(bool fullscreen);
    void   (*set_cursor_visible)(bool visible);
    /* The window's (and, on macOS, the Dock's) icon; NULL restores the
     * default. Backends make the sizes they need from it. May be NULL. */
    void   (*set_icon)(const br_image *icon);

    /* ---- desktop ----
     * pick_folder: the OS's folder dialog (modal). `start` may be NULL.
     * Returns 1 with the folder's absolute path in `out`, 0 if the user
     * cancelled, -1 if this platform has no dialog. open_path opens a file
     * or folder in the program the OS uses for it. Either may be NULL. */
    int    (*pick_folder)(const char *title, const char *start, char *out, size_t size);
    bool   (*open_path)(const char *path);

    /* ---- gamepads ----
     * Controllers are polled inside poll_events and reported as GAMEPAD_*
     * events (via br_gamepads_sync). Rumble: motor strengths 0..1; returns
     * false when the pad or platform has no rumble. May be NULL. */
    bool   (*set_gamepad_rumble)(int pad, float low_freq, float high_freq);

    /* Optional: drive the main loop (the web needs the browser to own the
     * loop). NULL means the engine runs a plain while-loop. */
    void   (*run_loop)(br_frame_fn frame, void *user);
} br_platform;

const br_platform *br_platform_headless(void);
const br_platform *br_platform_native(void);   /* NULL if no native backend built */

/* ---- headless controls (tests / tools) ------------------------------------ */
/* Virtual seconds advanced per poll_events call (default 1/60). */
void br_headless_set_timestep(double seconds);
/* Queue an event for delivery on the next poll. */
void br_headless_inject(const br_event *e);
/* Post BR_EVENT_QUIT after this many polls (0 = never). */
void br_headless_quit_after(uint64_t polls);
/* Last presented framebuffer (owned by the engine), its viewport, and count. */
const br_image *br_headless_last_frame(void);
br_recti br_headless_last_viewport(void);
uint64_t br_headless_present_count(void);
/* Simulate the user resizing the window (pixels) / OS scale changes. */
void br_headless_resize(int width_px, int height_px);
void br_headless_set_scale(float scale);
/* Total seconds the engine asked to sleep (frame limiter checks). */
double br_headless_slept(void);
bool br_headless_cursor_visible(void);
const char *br_headless_title(void);
const br_image *br_headless_icon(void);       /* the last icon set (NULL: none) */
/* Scripted folder dialog: what the next pick_folder answers (path, or NULL
 * = the user cancels); `available` false makes it report no dialog. */
void br_headless_set_folder_answer(const char *path, bool available);
const char *br_headless_last_opened(void);   /* the last open_path, "" if none */
/* Simulated controllers: set a pad's current snapshot (connected=false to
 * unplug); read back the last rumble request. */
void br_headless_set_gamepad(int pad, const br_gamepad_raw *state);
void br_headless_get_rumble(int pad, float *low_freq, float *high_freq);
/* Capture submitted audio into a caller buffer (interleaved stereo). */
void br_headless_capture_audio(int16_t *buffer, uint32_t capacity_frames);
uint32_t br_headless_captured_frames(void);

#endif /* BRONTO_PLATFORM_H */
