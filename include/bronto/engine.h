/*
 * Bronto Engine - engine core and main loop.
 *
 * Frame order:
 *   1. input begin-frame, platform polls OS events into the queue
 *   2. advance clock (clamped dt); sync window size, map mouse positions
 *      from window pixels to framebuffer pixels
 *   3. dispatch events: window state + input -> scenes (top-down) ->
 *      engine (quit / close)
 *   4. fixed-timestep updates (0..N per frame, accumulator based)
 *   5. variable update + late update
 *   6. render (ECS render systems, then scene render) and present
 *   7. mix + submit audio
 *   8. apply pending scene transitions
 *   9. frame limiter (sleep until 1/max_fps has elapsed)
 *
 * br_engine_frame() runs exactly one frame, which lets the web backend hand
 * control of the loop to the browser; br_engine_run() wraps everything.
 */
#ifndef BRONTO_ENGINE_H
#define BRONTO_ENGINE_H

#include "types.h"
#include "memory.h"
#include "event.h"
#include "input.h"
#include "ecs.h"
#include "scene.h"
#include "render.h"
#include "audio.h"
#include "platform.h"
#include "window.h"
#include "bmath.h"

typedef struct br_engine br_engine;

typedef struct br_engine_config {
    const char *title;
    int width, height;            /* framebuffer resolution (default 320x180) */
    int window_scale;             /* initial window = framebuffer * scale (default 3) */
    int window_width, window_height;   /* override initial window size, points */
    int min_window_width, min_window_height; /* points; default = framebuffer size */
    bool resizable;               /* default true */
    bool fullscreen;              /* start fullscreen */
    bool hidden_window;           /* create the window hidden (tools/tests) */
    br_scale_mode scale_mode;     /* default BR_SCALE_FIT */
    int pixel_scale;              /* BR_SCALE_RESIZE only: window px per fb px (default window_scale) */
    int max_fps;                  /* frame limiter; default 60, 0 = unlimited */
    bool pause_when_minimized;    /* skip update/render while minimized (default true) */
    bool close_quits;             /* unconsumed WINDOW_CLOSE_REQUESTED quits (default true) */
    double fixed_timestep;        /* default 1/60 s */
    int max_fixed_steps;          /* per frame, avoids spiral of death (default 5) */
    double max_frame_time;        /* dt clamp (default 0.25 s) */
    int audio_sample_rate;        /* default 44100; 0 disables audio */
    int audio_latency_ms;         /* output buffering (default 75 ms): lower = snappier,
                                     higher = safer against frame hitches */
    uint64_t max_frames;          /* stop after N frames (0 = run until quit) */
    size_t frame_arena_size;      /* per-frame scratch memory (default 1 MB) */
    bool auto_clear;              /* clear framebuffer each frame (default true) */
    br_color clear_color;
    bool quit_when_no_scenes;     /* default true */
    const br_platform *platform;  /* NULL = native, falling back to headless */
    const br_scene_desc *initial_scene;
    void *user;
    /* Optional hooks */
    bool (*on_init)(br_engine *e);
    void (*on_shutdown)(br_engine *e);
} br_engine_config;

struct br_engine {
    br_engine_config config;
    const br_platform *platform;

    br_events events;
    br_input input;
    br_image framebuffer;
    br_renderer renderer;
    br_mixer mixer;
    br_scene_manager scenes;
    br_window_state window;
    char *clipboard;                 /* the private clipboard, where the platform has none */
    br_arena frame_arena;          /* reset at the start of every frame */
    br_rng rng;

    double time;                   /* game time in seconds   */
    double dt;                     /* last frame delta        */
    double accumulator;
    double last_clock;
    double fps;
    uint64_t frame;
    uint64_t fixed_steps;          /* total fixed updates run */

    double rumble_until[BR_MAX_GAMEPADS];   /* engine time when rumble stops */

    int16_t *audio_buffer;
    uint32_t audio_buffer_frames;

    bool running;
    bool initialized;
    void *user;
};

br_engine_config br_engine_default_config(void);

bool br_engine_init(br_engine *e, const br_engine_config *cfg);
bool br_engine_frame(br_engine *e);   /* false once the engine should stop */
void br_engine_shutdown(br_engine *e);
void br_engine_quit(br_engine *e);

/* Convenience: init, run the loop (or hand it to the platform), shutdown.
 * Returns 0 on success. */
int br_engine_run(const br_engine_config *cfg);

/* Re-query the window size/scale from the platform, resize the framebuffer
 * in BR_SCALE_RESIZE mode and recompute the viewport. Called automatically. */
void br_engine_sync_window(br_engine *e);

/* Vibrate a gamepad: motor strengths 0..1 (low = heavy left motor, high =
 * light right motor) for `seconds` (<= 0 = until changed). Returns false if
 * the pad or platform cannot rumble. */
bool br_gamepad_rumble(br_engine *e, int pad, float low_freq, float high_freq, float seconds);

/* Fraction of a fixed step left in the accumulator (render interpolation). */
float br_engine_alpha(const br_engine *e);

/* Helpers for scene code. */
#define BR_SCENE_ENGINE(s) ((s)->engine)
#define BR_WORLD_ENGINE(w) ((br_engine *)(w)->context)

#endif /* BRONTO_ENGINE_H */
