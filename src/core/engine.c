#include "bronto/engine.h"
#include "bronto/log.h"

#include <string.h>

/* Event priorities: window state + input see everything first (and never
 * consume), scenes next, the engine's own handling (quit / close) last so
 * scenes can veto by consuming the event. */
#define PRIORITY_WINDOW 2000
#define PRIORITY_INPUT  1000
#define PRIORITY_SCENES 0
#define PRIORITY_ENGINE (-1000)

br_engine_config br_engine_default_config(void) {
    br_engine_config c;
    memset(&c, 0, sizeof(c));
    c.title = "Bronto";
    c.width = 320;
    c.height = 180;
    c.window_scale = 3;
    c.resizable = true;
    c.scale_mode = BR_SCALE_FIT;
    c.max_fps = 60;
    c.pause_when_minimized = true;
    c.close_quits = true;
    c.fixed_timestep = 1.0 / 60.0;
    c.max_fixed_steps = 5;
    c.max_frame_time = 0.25;
    c.audio_sample_rate = 44100;
    c.audio_latency_ms = 75;
    c.frame_arena_size = BR_MB(1);
    c.auto_clear = true;
    c.clear_color = br_rgb(20, 20, 28);
    c.quit_when_no_scenes = true;
    return c;
}

/* ---- window sync --------------------------------------------------------- */

static void resize_framebuffer(br_engine *e, int w, int h) {
    br_image fresh;
    if (w == e->framebuffer.width && h == e->framebuffer.height) return;
    if (!br_image_init(&fresh, w, h)) {
        BR_ERROR("could not resize framebuffer to %dx%d", w, h);
        return;
    }
    br_image_destroy(&e->framebuffer);
    e->framebuffer = fresh;
    br_renderer_set_target(&e->renderer, &e->framebuffer);
}

void br_engine_sync_window(br_engine *e) {
    br_window_state *w = &e->window;
    int ww = w->width, wh = w->height;
    if (e->platform->get_window_size) e->platform->get_window_size(&ww, &wh);
    w->width = ww;
    w->height = wh;
    if (e->platform->get_window_scale) w->scale = e->platform->get_window_scale();
    if (w->scale <= 0) w->scale = 1.0f;
    if (w->scale_mode == BR_SCALE_RESIZE && ww > 0 && wh > 0) {
        int fw, fh;
        br_viewport_resize_dims(ww, wh, w->pixel_scale, &fw, &fh);
        resize_framebuffer(e, fw, fh);
    }
    w->viewport = br_viewport_compute(e->framebuffer.width, e->framebuffer.height, ww, wh, w->scale_mode);
}

/* Platforms report mouse positions in window pixels; map them into the
 * framebuffer before anyone sees them. */
static void map_mouse_events(br_engine *e) {
    size_t i;
    const br_viewport *vp = &e->window.viewport;
    if (vp->rect.w <= 0 || vp->rect.h <= 0) return;
    for (i = 0; i < br_events_pending(&e->events); ++i) {
        br_event *ev = br_events_peek(&e->events, i);
        if (ev->type == BR_EVENT_MOUSE_MOVE) {
            br_vec2 p;
            ev->u.mouse_move.wx = ev->u.mouse_move.x;
            ev->u.mouse_move.wy = ev->u.mouse_move.y;
            p = br_viewport_to_framebuffer(vp, br_v2(ev->u.mouse_move.x, ev->u.mouse_move.y));
            ev->u.mouse_move.x = p.x;
            ev->u.mouse_move.y = p.y;
            ev->u.mouse_move.dx /= vp->scale_x;
            ev->u.mouse_move.dy /= vp->scale_y;
        } else if (ev->type == BR_EVENT_MOUSE_BUTTON_DOWN || ev->type == BR_EVENT_MOUSE_BUTTON_UP) {
            br_vec2 p;
            ev->u.mouse_button.wx = ev->u.mouse_button.x;
            ev->u.mouse_button.wy = ev->u.mouse_button.y;
            p = br_viewport_to_framebuffer(vp, br_v2(ev->u.mouse_button.x, ev->u.mouse_button.y));
            ev->u.mouse_button.x = p.x;
            ev->u.mouse_button.y = p.y;
        }
    }
}

/* ---- event handlers ------------------------------------------------------- */

static bool on_window_event(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    br_window_state *w = &e->window;
    switch (ev->type) {
    case BR_EVENT_WINDOW_RESIZED:
    case BR_EVENT_WINDOW_SCALE_CHANGED:
        br_engine_sync_window(e);
        break;
    case BR_EVENT_WINDOW_FOCUS_GAINED: w->focused = true; break;
    case BR_EVENT_WINDOW_FOCUS_LOST:   w->focused = false; break;
    case BR_EVENT_WINDOW_MINIMIZED:    w->minimized = true; break;
    case BR_EVENT_WINDOW_MAXIMIZED:    w->maximized = true; w->minimized = false; break;
    case BR_EVENT_WINDOW_RESTORED:     w->minimized = false; w->maximized = false; break;
    case BR_EVENT_WINDOW_FULLSCREEN:   w->fullscreen = ev->u.window.fullscreen; break;
    case BR_EVENT_WINDOW_MOVED:        w->x = ev->u.window.x; w->y = ev->u.window.y; break;
    case BR_EVENT_MOUSE_ENTER:         w->mouse_inside = true; break;
    case BR_EVENT_MOUSE_LEAVE:         w->mouse_inside = false; break;
    default: break;
    }
    return false;  /* never consumes */
}

static bool on_input_event(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    br_input_handle_event(&e->input, ev);
    return false;
}

static bool on_scene_event(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    return br_scenes_event(&e->scenes, ev);
}

static bool on_engine_event(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    if (ev->type == BR_EVENT_QUIT ||
        (ev->type == BR_EVENT_WINDOW_CLOSE_REQUESTED && e->config.close_quits)) {
        e->running = false;
        return true;
    }
    return false;
}

/* ---- init ------------------------------------------------------------------ */

static void fill_defaults(br_engine_config *c) {
    br_engine_config d = br_engine_default_config();
    if (!c->title) c->title = d.title;
    if (c->width <= 0) c->width = d.width;
    if (c->height <= 0) c->height = d.height;
    if (c->window_scale <= 0) c->window_scale = d.window_scale;
    if (c->window_width <= 0) c->window_width = c->width * c->window_scale;
    if (c->window_height <= 0) c->window_height = c->height * c->window_scale;
    if (c->min_window_width <= 0) c->min_window_width = c->width;
    if (c->min_window_height <= 0) c->min_window_height = c->height;
    if (c->pixel_scale <= 0) c->pixel_scale = c->window_scale;
    if (c->max_fps < 0) c->max_fps = 0;
    if (c->fixed_timestep <= 0) c->fixed_timestep = d.fixed_timestep;
    if (c->max_fixed_steps <= 0) c->max_fixed_steps = d.max_fixed_steps;
    if (c->max_frame_time <= 0) c->max_frame_time = d.max_frame_time;
    if (c->audio_sample_rate < 0) c->audio_sample_rate = 0;
    if (c->audio_latency_ms <= 0) c->audio_latency_ms = d.audio_latency_ms;
    if (!c->frame_arena_size) c->frame_arena_size = d.frame_arena_size;
}

bool br_engine_init(br_engine *e, const br_engine_config *cfg) {
    br_platform_config pc;
    memset(e, 0, sizeof(*e));
    e->config = cfg ? *cfg : br_engine_default_config();
    fill_defaults(&e->config);
    e->user = e->config.user;

    e->platform = e->config.platform ? e->config.platform : br_platform_native();
    if (!e->platform) {
        BR_WARN("no native platform backend for this target; running headless");
        e->platform = br_platform_headless();
    }

    memset(&pc, 0, sizeof(pc));
    pc.title = e->config.title;
    pc.width = e->config.window_width;
    pc.height = e->config.window_height;
    pc.min_width = e->config.min_window_width;
    pc.min_height = e->config.min_window_height;
    pc.resizable = e->config.resizable;
    pc.fullscreen = e->config.fullscreen;
    pc.hidden = e->config.hidden_window;
    pc.audio_sample_rate = e->config.audio_sample_rate;
    pc.audio_latency_ms = e->config.audio_latency_ms;
    if (!e->platform->init(&pc)) {
        BR_ERROR("platform '%s' failed to initialise", e->platform->name);
        return false;
    }

    if (!br_events_init(&e->events, 256) ||
        !br_image_init(&e->framebuffer, e->config.width, e->config.height) ||
        !br_arena_init(&e->frame_arena, e->config.frame_arena_size)) {
        BR_ERROR("engine: out of memory during init");
        e->initialized = true;
        br_engine_shutdown(e);
        return false;
    }
    br_input_init(&e->input);
    br_renderer_init(&e->renderer, &e->framebuffer);
    br_mixer_init(&e->mixer, e->config.audio_sample_rate ? e->config.audio_sample_rate : 44100);
    br_scenes_init(&e->scenes, e, &e->events);
    br_rng_seed(&e->rng, 0xB40270ull);

    e->window.focused = true;
    e->window.cursor_visible = true;
    e->window.fullscreen = e->config.fullscreen;
    e->window.scale_mode = e->config.scale_mode;
    e->window.pixel_scale = e->config.pixel_scale;
    br_engine_sync_window(e);

    br_events_subscribe(&e->events, BR_EVENT_ANY, on_window_event, e, PRIORITY_WINDOW);
    br_events_subscribe(&e->events, BR_EVENT_ANY, on_input_event, e, PRIORITY_INPUT);
    br_events_subscribe(&e->events, BR_EVENT_ANY, on_scene_event, e, PRIORITY_SCENES);
    br_events_subscribe(&e->events, BR_EVENT_ANY, on_engine_event, e, PRIORITY_ENGINE);

    e->initialized = true;
    e->running = true;
    e->last_clock = e->platform->time();

    if (e->config.on_init && !e->config.on_init(e)) {
        BR_ERROR("on_init hook failed");
        br_engine_shutdown(e);
        return false;
    }
    if (e->config.initial_scene) {
        br_scene_push(&e->scenes, e->config.initial_scene);
        br_scenes_apply(&e->scenes);
    }
    BR_INFO("Bronto %d.%d.%d initialised (%dx%d framebuffer, %dx%d window, platform: %s)",
            BRONTO_VERSION_MAJOR, BRONTO_VERSION_MINOR, BRONTO_VERSION_PATCH,
            e->framebuffer.width, e->framebuffer.height, e->window.width, e->window.height,
            e->platform->name);
    return true;
}

static void pump_audio(br_engine *e) {
    uint32_t wanted;
    if (!e->config.audio_sample_rate || !e->platform->audio_frames_wanted) return;
    wanted = e->platform->audio_frames_wanted();
    if (!wanted) return;
    if (wanted > e->audio_buffer_frames) {
        int16_t *buf = (int16_t *)br_realloc(e->audio_buffer, (size_t)wanted * 2 * sizeof(int16_t));
        if (!buf) return;
        e->audio_buffer = buf;
        e->audio_buffer_frames = wanted;
    }
    br_mixer_mix_s16(&e->mixer, e->audio_buffer, wanted);
    e->platform->audio_submit(e->audio_buffer, wanted);
}

static void limit_frame_rate(br_engine *e, double frame_start) {
    double target, now;
    int fps = e->config.max_fps;
    /* minimised: throttle, but stay above the audio buffer's refill rate */
    if (e->window.minimized && e->config.pause_when_minimized && (fps <= 0 || fps > 20)) fps = 20;
    if (fps <= 0 || !e->platform->sleep) return;
    target = frame_start + 1.0 / (double)fps;
    now = e->platform->time();
    if (now < target) e->platform->sleep(target - now);
}

static void update_rumble(br_engine *e) {
    int i;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i) {
        if (e->rumble_until[i] > 0 && e->time >= e->rumble_until[i]) {
            e->rumble_until[i] = 0;
            if (e->platform->set_gamepad_rumble) e->platform->set_gamepad_rumble(i, 0, 0);
        }
    }
}

/* ---- frame ------------------------------------------------------------------- */

bool br_engine_frame(br_engine *e) {
    double now, dt;
    int steps = 0;
    float fdt;
    bool paused;
    if (!e->initialized || !e->running) return false;

    /* 1. input begin-frame + OS events (polled before reading the clock so
     *    the frame's dt includes any time the platform spent pumping). */
    br_input_begin_frame(&e->input);
    e->events.time = e->time;
    e->platform->poll_events(&e->events);

    /* 2. clock, window, mouse mapping */
    now = e->platform->time();
    dt = now - e->last_clock;
    e->last_clock = now;
    if (dt < 0) dt = 0;
    if (dt > e->config.max_frame_time) dt = e->config.max_frame_time;
    e->dt = dt;
    if (dt > 0) e->fps = e->fps * 0.9 + (1.0 / dt) * 0.1;
    br_arena_reset(&e->frame_arena);
    br_engine_sync_window(e);
    map_mouse_events(e);

    /* 3. dispatch events: window/input -> scenes -> engine */
    br_events_dispatch(&e->events);
    br_input_end_events(&e->input);

    paused = e->window.minimized && e->config.pause_when_minimized;
    if (!paused) {
        /* 4. fixed updates */
        fdt = (float)e->config.fixed_timestep;
        e->accumulator += dt;
        while (e->accumulator >= e->config.fixed_timestep && steps < e->config.max_fixed_steps) {
            br_scenes_fixed_update(&e->scenes, fdt);
            e->accumulator -= e->config.fixed_timestep;
            e->fixed_steps++;
            steps++;
        }
        if (steps == e->config.max_fixed_steps && e->accumulator >= e->config.fixed_timestep)
            e->accumulator = 0;  /* running behind: drop the backlog */

        /* 5. update */
        br_scenes_update(&e->scenes, (float)dt);

        /* 6. render + present */
        br_renderer_reset_stats(&e->renderer);
        br_end_camera(&e->renderer);
        br_reset_clip(&e->renderer);
        br_set_blend(&e->renderer, BR_BLEND_ALPHA);
        if (e->config.auto_clear) br_clear(&e->renderer, e->config.clear_color);
        br_scenes_render(&e->scenes);
        if (e->window.viewport.rect.w > 0 && e->window.viewport.rect.h > 0)
            e->platform->present(&e->framebuffer, e->window.viewport.rect);
        e->time += dt;
    } else {
        e->accumulator = 0;   /* no catch-up burst when restored */
    }

    /* 7. audio keeps flowing (silence/music) even while paused */
    pump_audio(e);
    update_rumble(e);

    /* 8. transitions & bookkeeping */
    br_scenes_apply(&e->scenes);
    e->frame++;
    if (e->config.quit_when_no_scenes && br_scenes_depth(&e->scenes) == 0 && e->scenes.op_count == 0)
        e->running = false;
    if (e->config.max_frames && e->frame >= e->config.max_frames) e->running = false;

    /* 9. frame limiter */
    if (e->running) limit_frame_rate(e, now);
    return e->running;
}

void br_engine_quit(br_engine *e) { e->running = false; }

bool br_gamepad_rumble(br_engine *e, int pad, float lo, float hi, float seconds) {
    if (pad < 0 || pad >= BR_MAX_GAMEPADS || !e->platform->set_gamepad_rumble) return false;
    lo = br_clampf(lo, 0, 1);
    hi = br_clampf(hi, 0, 1);
    if (!e->platform->set_gamepad_rumble(pad, lo, hi)) return false;
    e->rumble_until[pad] = (seconds > 0 && (lo > 0 || hi > 0)) ? e->time + seconds : 0;
    return true;
}


void br_engine_shutdown(br_engine *e) {
    if (!e->initialized) return;
    br_scenes_shutdown(&e->scenes);
    if (e->config.on_shutdown) e->config.on_shutdown(e);
    {   /* never leave a controller vibrating */
        int i;
        for (i = 0; i < BR_MAX_GAMEPADS; ++i)
            if (e->platform && e->platform->set_gamepad_rumble) e->platform->set_gamepad_rumble(i, 0, 0);
    }
    br_mixer_stop_all(&e->mixer);
    if (e->platform) e->platform->shutdown();
    br_free(e->audio_buffer);
    br_arena_destroy(&e->frame_arena);
    br_image_destroy(&e->framebuffer);
    br_events_destroy(&e->events);
    e->initialized = false;
    e->running = false;
}

float br_engine_alpha(const br_engine *e) {
    return (float)(e->accumulator / e->config.fixed_timestep);
}

static bool run_frame(void *user) { return br_engine_frame((br_engine *)user); }

int br_engine_run(const br_engine_config *cfg) {
    br_engine *e = BR_NEW(br_engine);
    if (!e) return 1;
    if (!br_engine_init(e, cfg)) { br_free(e); return 1; }
    if (e->platform->run_loop) {
        /* The platform owns the loop (web): it stops when frame() returns false. */
        e->platform->run_loop(run_frame, e);
    } else {
        while (br_engine_frame(e)) { /* loop */ }
    }
    br_engine_shutdown(e);
    br_free(e);
    return 0;
}
