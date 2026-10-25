/*
 * Recording and replaying a run (replay.h).
 *
 * The replay platform wraps the real one. Each frame it either holds the
 * engine still (paused: br_engine.suspended) or lets exactly one frame of
 * the run happen: the clock moves on 1/60 s, and the keys recorded for that
 * frame go down or up. Everything the real devices report for the game -
 * keys, mouse, text, gamepads, focus - is taken out of the queue, so what
 * the game sees is only the recording.
 */
#include "bronto/replay.h"
#include "bronto/engine.h"
#include "bronto/memory.h"
#include "bronto/save.h"
#include "bronto/fs.h"
#include "bronto/log.h"
#include "bronto/render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RP_STEP (1.0 / 60.0)

typedef struct rp_key { uint64_t frame; uint16_t key; uint8_t down; } rp_key;
typedef struct rp_hash { uint64_t frame; uint32_t hash; } rp_hash;
typedef struct rp_seg { uint64_t start; char name[48]; } rp_seg;

static struct {
    br_replay_mode mode;
    bool live;
    const br_platform *inner;
    br_platform wrapped;
    br_engine *engine;
    char game[96];

    double now;                 /* virtual clock */
    uint64_t frame;             /* frames run */
    uint64_t length;            /* frames in the recording */

    rp_key *keys;    size_t key_count, key_cap, key_at;
    rp_hash *hashes; size_t hash_count, hash_cap, hash_at;
    rp_seg segs[BR_REPLAY_MAX_SEGMENTS];
    int seg_count;
    uint32_t hash_every;

    /* recording: changes asked for, applied at the next frame */
    struct { uint16_t key; uint8_t down; } pending[64];
    int pending_count;
    bool held[BR_KEY_COUNT];

    /* playback */
    int speed;                  /* 1, 2, 4, 8; 0 = flat out */
    bool paused, step_one, ended, status;
    uint64_t seek_to;           /* run unseen up to this frame (0: not seeking) */
    bool seek_resume_paused;
    int64_t restart_to;         /* -1: none; else go back and seek to this frame */
    bool restarting;
    void (*restart_hook)(br_engine *e);
    double deadline;            /* real time the next frame is due (live, paced) */
    double shown_at;            /* real time a frame was last shown (skimming) */
    bool browser;               /* the platform runs the loop (the web): frames come per screen refresh */
    double anchor_t;            /* (browser) real time when frame anchor_frame was due; 0: anchor again */
    uint64_t anchor_frame;

    uint32_t last_hash;
    bool draw_all;              /* draw every frame even unwatched (screenshots) */
    uint32_t checked, mismatches;
    uint64_t first_mismatch;

    br_image overlay;
    br_renderer overlay_r;
} g_rp;

/* ---- storage ------------------------------------------------------------------ */

static bool rp_push_key(uint64_t frame, br_key key, bool down) {
    if (g_rp.key_count == g_rp.key_cap) {
        size_t cap = g_rp.key_cap ? g_rp.key_cap * 2 : 1024;
        rp_key *k = (rp_key *)br_realloc(g_rp.keys, cap * sizeof(rp_key));
        if (!k) return false;
        g_rp.keys = k;
        g_rp.key_cap = cap;
    }
    g_rp.keys[g_rp.key_count].frame = frame;
    g_rp.keys[g_rp.key_count].key = (uint16_t)key;
    g_rp.keys[g_rp.key_count].down = down ? 1 : 0;
    g_rp.key_count++;
    return true;
}

static bool rp_push_hash(uint64_t frame, uint32_t hash) {
    if (g_rp.hash_count == g_rp.hash_cap) {
        size_t cap = g_rp.hash_cap ? g_rp.hash_cap * 2 : 1024;
        rp_hash *h = (rp_hash *)br_realloc(g_rp.hashes, cap * sizeof(rp_hash));
        if (!h) return false;
        g_rp.hashes = h;
        g_rp.hash_cap = cap;
    }
    g_rp.hashes[g_rp.hash_count].frame = frame;
    g_rp.hashes[g_rp.hash_count].hash = hash;
    g_rp.hash_count++;
    return true;
}

static void rp_add_segment(uint64_t start, const char *name) {
    rp_seg *s;
    if (g_rp.seg_count >= BR_REPLAY_MAX_SEGMENTS) return;
    s = &g_rp.segs[g_rp.seg_count++];
    s->start = start;
    snprintf(s->name, sizeof(s->name), "%s", name);
}

static void rp_forget(void) {
    const br_platform *inner = g_rp.inner;               /* (the platform stays wrapped) */
    br_platform wrapped = g_rp.wrapped;
    bool live = g_rp.live, browser = g_rp.browser;
    br_free(g_rp.keys);
    br_free(g_rp.hashes);
    br_image_destroy(&g_rp.overlay);
    memset(&g_rp, 0, sizeof(g_rp));
    g_rp.restart_to = -1;
    g_rp.inner = inner;
    g_rp.wrapped = wrapped;
    g_rp.live = live;
    g_rp.browser = browser;
}

/* ---- recording -------------------------------------------------------------------- */

bool br_replay_record(const char *game, uint32_t hash_every) {
    rp_forget();
    g_rp.mode = BR_REPLAY_RECORD;
    g_rp.hash_every = hash_every ? hash_every : 60;
    g_rp.speed = 0;
    snprintf(g_rp.game, sizeof(g_rp.game), "%s", game ? game : "");
    return true;
}

void br_replay_key(br_key key, bool down) {
    int i;
    if (g_rp.mode != BR_REPLAY_RECORD || (int)key <= 0 || (int)key >= BR_KEY_COUNT) return;
    for (i = 0; i < g_rp.pending_count; ++i)            /* the last word for a key this frame wins */
        if (g_rp.pending[i].key == (uint16_t)key) { g_rp.pending[i].down = down ? 1 : 0; return; }
    if (g_rp.pending_count < (int)(sizeof(g_rp.pending) / sizeof(g_rp.pending[0]))) {
        g_rp.pending[g_rp.pending_count].key = (uint16_t)key;
        g_rp.pending[g_rp.pending_count].down = down ? 1 : 0;
        g_rp.pending_count++;
    }
}

void br_replay_segment(const char *name) {
    if (g_rp.mode != BR_REPLAY_RECORD) return;
    if (g_rp.seg_count > 0 && g_rp.segs[g_rp.seg_count - 1].start == g_rp.frame) {
        snprintf(g_rp.segs[g_rp.seg_count - 1].name, sizeof(g_rp.segs[0].name), "%s", name);
        return;
    }
    rp_add_segment(g_rp.frame, name);
}

bool br_replay_finish(const char *path) {
    FILE *f;
    size_t ki = 0, hi = 0;
    int si = 0;
    bool ok = true;
    if (g_rp.mode != BR_REPLAY_RECORD) { rp_forget(); return false; }
    f = fopen(path, "w");
    if (!f) { BR_ERROR("replay: can't write %s", path); rp_forget(); return false; }
    fprintf(f, "brec 1\ngame %s\n", g_rp.game);
    /* in frame order: segments, then keys, then the hash at the end of it */
    while (si < g_rp.seg_count || ki < g_rp.key_count || hi < g_rp.hash_count) {
        uint64_t fs = si < g_rp.seg_count ? g_rp.segs[si].start : UINT64_MAX;
        uint64_t fk = ki < g_rp.key_count ? g_rp.keys[ki].frame : UINT64_MAX;
        uint64_t fh = hi < g_rp.hash_count ? g_rp.hashes[hi].frame : UINT64_MAX;
        if (fs <= fk && fs <= fh) {
            fprintf(f, "seg %llu %s\n", (unsigned long long)fs, g_rp.segs[si].name);
            si++;
        } else if (fk <= fh) {
            fprintf(f, "k %llu %c%u\n", (unsigned long long)fk, g_rp.keys[ki].down ? '+' : '-', (unsigned)g_rp.keys[ki].key);
            ki++;
        } else {
            fprintf(f, "h %llu %08x\n", (unsigned long long)fh, (unsigned)g_rp.hashes[hi].hash);
            hi++;
        }
    }
    fprintf(f, "end %llu\n", (unsigned long long)g_rp.frame);
    if (fclose(f) != 0) ok = false;
    rp_forget();
    return ok;
}

/* ---- loading --------------------------------------------------------------------------- */

static uint64_t rp_parse_u64(const char **p) {
    uint64_t v = 0;
    while (**p == ' ') (*p)++;
    while (**p >= '0' && **p <= '9') { v = v * 10 + (uint64_t)(**p - '0'); (*p)++; }
    return v;
}

bool br_replay_load_text(const char *text, size_t size) {
    const char *p = text, *end = text + size;
    rp_forget();
    g_rp.mode = BR_REPLAY_PLAY;
    g_rp.speed = 1;
    g_rp.status = true;
    if (size < 6 || strncmp(text, "brec 1", 6) != 0) { BR_ERROR("replay: not a recording"); rp_forget(); return false; }
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\n') eol++;
        if (strncmp(p, "seg ", 4) == 0) {
            char name[48];
            size_t n;
            const char *q = p + 4;
            uint64_t fr = rp_parse_u64(&q);
            while (q < eol && *q == ' ') q++;
            n = (size_t)(eol - q);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, q, n);
            name[n] = 0;
            if (n && name[n - 1] == '\r') name[n - 1] = 0;
            rp_add_segment(fr, name);
        } else if (strncmp(p, "k ", 2) == 0) {
            const char *q = p + 2;
            uint64_t fr = rp_parse_u64(&q);
            bool down;
            while (*q == ' ') q++;
            down = *q == '+';
            q++;
            rp_push_key(fr, (br_key)rp_parse_u64(&q), down);
        } else if (strncmp(p, "h ", 2) == 0) {
            const char *q = p + 2;
            uint64_t fr = rp_parse_u64(&q);
            rp_push_hash(fr, (uint32_t)strtoul(q, NULL, 16));
        } else if (strncmp(p, "end ", 4) == 0) {
            const char *q = p + 4;
            g_rp.length = rp_parse_u64(&q);
        } else if (strncmp(p, "game ", 5) == 0) {
            size_t n = (size_t)(eol - (p + 5));
            if (n >= sizeof(g_rp.game)) n = sizeof(g_rp.game) - 1;
            memcpy(g_rp.game, p + 5, n);
            g_rp.game[n] = 0;
        }
        p = eol + 1;
    }
    return true;
}

bool br_replay_load(const char *path) {
    size_t size = 0;
    char *text = (char *)br_file_read_all(path, &size);
    bool ok;
    if (!text) { BR_ERROR("replay: can't read %s", path); return false; }
    ok = br_replay_load_text(text, size);
    br_free(text);
    return ok;
}

void br_replay_start_at_segment(int segment) {
    if (g_rp.mode != BR_REPLAY_PLAY || segment <= 0 || segment >= g_rp.seg_count) return;
    g_rp.seek_to = g_rp.segs[segment].start;
    g_rp.seek_resume_paused = false;
}

/* ---- the platform ---------------------------------------------------------------------------- */

static uint32_t rp_hash_image(const br_image *img) {
    uint32_t h = 2166136261u;
    const unsigned char *b = (const unsigned char *)img->pixels;
    size_t i, n = (size_t)img->width * (size_t)img->height * sizeof(br_color);
    for (i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static bool rp_is_device_input(uint32_t t) {
    switch (t) {
    case BR_EVENT_KEY_DOWN: case BR_EVENT_KEY_UP: case BR_EVENT_TEXT_INPUT:
    case BR_EVENT_MOUSE_MOVE: case BR_EVENT_MOUSE_BUTTON_DOWN: case BR_EVENT_MOUSE_BUTTON_UP:
    case BR_EVENT_MOUSE_WHEEL: case BR_EVENT_MOUSE_ENTER: case BR_EVENT_MOUSE_LEAVE:
    case BR_EVENT_TOUCH_DOWN: case BR_EVENT_TOUCH_MOVE: case BR_EVENT_TOUCH_UP:
    case BR_EVENT_GAMEPAD_CONNECTED: case BR_EVENT_GAMEPAD_DISCONNECTED:
    case BR_EVENT_GAMEPAD_BUTTON_DOWN: case BR_EVENT_GAMEPAD_BUTTON_UP: case BR_EVENT_GAMEPAD_AXIS:
    case BR_EVENT_WINDOW_FOCUS_GAINED: case BR_EVENT_WINDOW_FOCUS_LOST:
    case BR_EVENT_WINDOW_MINIMIZED:
        return true;
    default:
        return false;
    }
}

static int rp_current_segment(void) { return br_replay_segment_at(g_rp.frame); }

static void rp_seek(uint64_t to) {
    if (to <= g_rp.frame) return;
    if (!g_rp.seek_to) g_rp.seek_resume_paused = g_rp.paused;
    g_rp.seek_to = to;
    g_rp.paused = false;
}

/* The playback keys (a live replay). */
static void rp_control(br_key k) {
    int seg = rp_current_segment();
    g_rp.anchor_t = 0;                                   /* (any change: pace from here) */
    switch ((int)k) {
    case BR_KEY_SPACE:  g_rp.paused = !g_rp.paused; g_rp.seek_to = 0; break;
    case BR_KEY_PERIOD: if (g_rp.paused) g_rp.step_one = true; break;
    case BR_KEY_1: g_rp.speed = 1; break;
    case BR_KEY_2: g_rp.speed = 2; break;
    case BR_KEY_3: g_rp.speed = 4; break;
    case BR_KEY_4: g_rp.speed = 8; break;
    case BR_KEY_0: g_rp.speed = 0; break;
    case BR_KEY_H: g_rp.status = !g_rp.status; break;
    case BR_KEY_N: case BR_KEY_RIGHT_BRACKET:
        if (seg + 1 < g_rp.seg_count) rp_seek(g_rp.segs[seg + 1].start);
        break;
    case BR_KEY_P: case BR_KEY_LEFT_BRACKET:
        if (g_rp.restart_hook && seg >= 0) {
            int to = seg;                                /* near a segment's start: the one before */
            if (g_rp.frame < g_rp.segs[seg].start + 90 && seg > 0) to = seg - 1;
            g_rp.restart_to = (int64_t)g_rp.segs[to].start;
            g_rp.seek_resume_paused = g_rp.paused;
        }
        break;
    default: break;
    }
}

static double rp_real_time(void) { return g_rp.inner->time(); }

static void rp_draw_status(const br_image *fb) {
    char line1[64], line2[64];
    int seg;
    br_renderer *r = &g_rp.overlay_r;
    float y;
    if (g_rp.overlay.width != fb->width || g_rp.overlay.height != fb->height) {
        br_image_destroy(&g_rp.overlay);
        if (!br_image_init(&g_rp.overlay, fb->width, fb->height)) return;
        br_renderer_init(r, &g_rp.overlay);
    }
    memcpy(g_rp.overlay.pixels, fb->pixels, (size_t)fb->width * (size_t)fb->height * sizeof(br_color));
    if (!g_rp.status) return;
    seg = rp_current_segment();
    {
        int fit = (fb->width - 4) / 8;                       /* (the 8x8 font: characters that fit) */
        char where[16];
        uint64_t f = g_rp.frame;
        unsigned s = (unsigned)(f / 60);
        const char *state = g_rp.ended ? "END" : g_rp.seek_to ? ">>" : g_rp.paused ? "PAUSED" : "";
        char speed[8];
        int n;
        snprintf(line1, sizeof(line1), "%s", seg >= 0 ? g_rp.segs[seg].name : "");
        if (fit > 0 && fit < (int)sizeof(line1) && (int)strlen(line1) > fit) {
            line1[fit] = 0;
            if (fit > 2) line1[fit - 1] = line1[fit - 2] = '.';
        }
        if (g_rp.speed == 0) snprintf(speed, sizeof(speed), "MAX");
        else snprintf(speed, sizeof(speed), "x%d", g_rp.speed);
        snprintf(line2, sizeof(line2), "%u:%02u:%02u %s %s", s / 3600, (s / 60) % 60, s % 60, speed, state);
        snprintf(where, sizeof(where), "%d/%d", seg + 1, g_rp.seg_count);
        n = (int)strlen(line2);
        y = (float)fb->height - 19;
        br_set_blend(r, BR_BLEND_ALPHA);
        br_fill_rect(r, br_rect_make(0, y, (float)fb->width, 19), br_rgba(0, 0, 0, 170));
        br_draw_text(r, line1, 2, y + 1, 1, br_rgb(255, 230, 140));
        br_draw_text(r, line2, 2, y + 10, 1, g_rp.mismatches ? br_rgb(255, 110, 110) : br_rgb(200, 220, 255));
        if (n + 1 + (int)strlen(where) <= fit)                /* the segment's number, on the right */
            br_draw_text(r, where, (float)(fb->width - 2 - 8 * (int)strlen(where)), y + 10, 1, br_rgb(150, 160, 175));
    }
}

static void rp_show(const br_image *fb) {
    if (!g_rp.engine) return;
    if (g_rp.seek_to) {                                  /* skimming: a glimpse 30 times a second is plenty */
        double t = rp_real_time();
        if (t - g_rp.shown_at < 1.0 / 30.0) return;
        g_rp.shown_at = t;
    }
    rp_draw_status(fb);
    if (g_rp.overlay.pixels && g_rp.engine->window.viewport.rect.w > 0)
        g_rp.inner->present(&g_rp.overlay, g_rp.engine->window.viewport.rect);
}

static void rp_poll(br_events *events) {
    size_t i, before = br_events_pending(events);
    uint64_t cur;
    g_rp.inner->poll_events(events);
    for (i = before; i < br_events_pending(events); ++i) {
        br_event *ev = br_events_peek(events, i);
        if (g_rp.live && g_rp.mode == BR_REPLAY_PLAY && ev->type == BR_EVENT_KEY_DOWN && !ev->u.key.repeat) rp_control(ev->u.key.key);
        if (rp_is_device_input(ev->type)) ev->type = BR_EVENT_NONE;
    }
    if (!g_rp.engine) return;
    if (g_rp.seek_to && g_rp.frame >= g_rp.seek_to) {
        g_rp.seek_to = 0;
        g_rp.paused = g_rp.seek_resume_paused;
        g_rp.deadline = 0;
        g_rp.anchor_t = 0;
    }
    if (g_rp.mode == BR_REPLAY_PLAY && g_rp.length && g_rp.frame >= g_rp.length) {
        g_rp.ended = true;
        g_rp.seek_to = 0;
    }
    if (g_rp.restart_to >= 0 || g_rp.ended || (g_rp.paused && !g_rp.step_one && !g_rp.seek_to)) {
        g_rp.engine->suspended = true;                   /* nothing happens this frame */
        if (g_rp.live) {
            if (g_rp.engine->framebuffer.pixels) rp_show(&g_rp.engine->framebuffer);
            if (g_rp.inner->sleep && !g_rp.browser) g_rp.inner->sleep(RP_STEP);
        }
        g_rp.deadline = 0;
        g_rp.anchor_t = 0;
        return;
    }
    if (g_rp.browser && g_rp.live && !g_rp.seek_to && g_rp.speed > 0 && !g_rp.step_one) {
        double t = rp_real_time();
        if (g_rp.anchor_t <= 0 || t - g_rp.anchor_t > 1e6) { g_rp.anchor_t = t; g_rp.anchor_frame = g_rp.frame; }
        else if ((double)g_rp.frame >= (double)g_rp.anchor_frame + (t - g_rp.anchor_t) * 60.0 * g_rp.speed + 1.0) {
            g_rp.engine->suspended = true;               /* early (a fast screen): this refresh shows the last frame */
            return;
        }
    }
    g_rp.engine->suspended = false;
    g_rp.step_one = false;

    /* One frame of the run: its keys, and 1/60 s on the clock. */
    cur = g_rp.frame;
    if (g_rp.mode == BR_REPLAY_PLAY) {
        while (g_rp.key_at < g_rp.key_count && g_rp.keys[g_rp.key_at].frame < cur) g_rp.key_at++;
        while (g_rp.key_at < g_rp.key_count && g_rp.keys[g_rp.key_at].frame == cur) {
            const rp_key *k = &g_rp.keys[g_rp.key_at++];
            br_event e = br_event_key(k->down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, (br_key)k->key, 0, false);
            br_events_post(events, &e);
        }
    } else if (g_rp.mode == BR_REPLAY_RECORD) {
        int k;
        for (k = 0; k < g_rp.pending_count; ++k) {
            br_key key = (br_key)g_rp.pending[k].key;
            bool down = g_rp.pending[k].down != 0;
            br_event e;
            if (g_rp.held[key] == down) continue;
            g_rp.held[key] = down;
            rp_push_key(cur, key, down);
            e = br_event_key(down ? BR_EVENT_KEY_DOWN : BR_EVENT_KEY_UP, key, 0, false);
            br_events_post(events, &e);
        }
        g_rp.pending_count = 0;
    }
    /* Unwatched (or skimming), a frame is only drawn when its picture is
     * checked (or kept): much faster. */
    if (!g_rp.live || g_rp.seek_to) {
        bool draw = false;
        if (g_rp.mode == BR_REPLAY_RECORD) draw = cur % g_rp.hash_every == 0;
        else if (g_rp.mode == BR_REPLAY_PLAY) {
            size_t h = g_rp.hash_at;
            while (h < g_rp.hash_count && g_rp.hashes[h].frame < cur) h++;
            draw = (h < g_rp.hash_count && g_rp.hashes[h].frame == cur) || cur + 1 >= g_rp.length;
        }
        g_rp.engine->skip_render = !draw && !g_rp.draw_all;
    } else {
        g_rp.engine->skip_render = false;
    }
    g_rp.now += RP_STEP;
    g_rp.frame++;

    /* Pacing: a live replay keeps to real time at its speed. */
    if (g_rp.live && !g_rp.seek_to && g_rp.speed > 0 && g_rp.inner->sleep && !g_rp.browser) {
        double t = rp_real_time(), per = RP_STEP / (double)g_rp.speed;
        if (g_rp.deadline <= 0 || t > g_rp.deadline + 0.25) g_rp.deadline = t;
        g_rp.deadline += per;
        if (g_rp.deadline > t) g_rp.inner->sleep(g_rp.deadline - t);
    }
}

static void rp_present(const br_image *fb, br_recti viewport) {
    uint64_t cur = g_rp.frame ? g_rp.frame - 1 : 0;
    g_rp.last_hash = rp_hash_image(fb);
    if (g_rp.mode == BR_REPLAY_PLAY) {
        while (g_rp.hash_at < g_rp.hash_count && g_rp.hashes[g_rp.hash_at].frame < cur) g_rp.hash_at++;
        if (g_rp.hash_at < g_rp.hash_count && g_rp.hashes[g_rp.hash_at].frame == cur) {
            g_rp.checked++;
            if (g_rp.hashes[g_rp.hash_at].hash != g_rp.last_hash) {
                if (!g_rp.mismatches) {
                    g_rp.first_mismatch = cur;
                    BR_WARN("replay: frame %llu differs from the recording", (unsigned long long)cur);
                }
                g_rp.mismatches++;
            }
            g_rp.hash_at++;
        }
    } else if (g_rp.mode == BR_REPLAY_RECORD) {
        if (cur % g_rp.hash_every == 0) rp_push_hash(cur, g_rp.last_hash);
    }
    if (!g_rp.live) return;
    BR_UNUSED(viewport);
    rp_show(fb);
}

static double rp_time(void) { return g_rp.now; }
static void rp_sleep(double s) { BR_UNUSED(s); }           /* (pacing is done in poll) */
static bool rp_init(const br_platform_config *cfg) { return g_rp.restarting ? true : g_rp.inner->init(cfg); }
static void rp_shutdown(void) { if (!g_rp.restarting) g_rp.inner->shutdown(); }
/* Sound only plays at x1 (faster, it would be noise): otherwise silence goes
 * to the device instead. (The game's mixer runs as ever: nothing it can see
 * changes.) */
static void rp_audio_submit(const int16_t *data, uint32_t frames) {
    static int16_t quiet[2 * 4096];
    bool mute = g_rp.speed != 1 || g_rp.seek_to || g_rp.paused;
    if (!mute) { g_rp.inner->audio_submit(data, frames); return; }
    while (frames > 0) {
        uint32_t n = frames > 4096 ? 4096 : frames;
        g_rp.inner->audio_submit(quiet, n);
        frames -= n;
    }
}

const br_platform *br_replay_platform(const br_platform *inner, bool live) {
    g_rp.inner = inner;
    g_rp.live = live;
    g_rp.browser = inner->run_loop != NULL;
    g_rp.wrapped = *inner;
    g_rp.wrapped.init = rp_init;
    g_rp.wrapped.shutdown = rp_shutdown;
    g_rp.wrapped.poll_events = rp_poll;
    g_rp.wrapped.present = rp_present;
    g_rp.wrapped.time = rp_time;
    g_rp.wrapped.sleep = rp_sleep;
    if (inner->audio_submit) g_rp.wrapped.audio_submit = rp_audio_submit;
    return &g_rp.wrapped;
}

void br_replay_attach(br_engine *e) {
    g_rp.engine = e;
    if (g_rp.mode == BR_REPLAY_PLAY) BR_INFO("replay: %llu frames in %d segments", (unsigned long long)g_rp.length, g_rp.seg_count);
}

void br_replay_draw_every_frame(bool on) { g_rp.draw_all = on; }

void br_replay_set_restart_hook(void (*fn)(br_engine *e)) { g_rp.restart_hook = fn; }

void br_replay_clear_saves(void) {
    br_dir_list list;
    int i;
    if (!br_save_list(&list)) return;
    for (i = 0; i < list.count; ++i) if (!list.entries[i].is_dir) br_save_delete(list.entries[i].name);
    br_dir_list_free(&list);
}

static void rp_restart(br_engine *e) {
    br_engine_config cfg = e->config;
    void (*hook)(br_engine *) = e->loop_hook;
    uint64_t to = (uint64_t)g_rp.restart_to;
    g_rp.restarting = true;
    br_engine_shutdown(e);
    g_rp.restart_hook(e);
    br_replay_clear_saves();
    g_rp.now = 0;
    g_rp.frame = 0;
    g_rp.key_at = g_rp.hash_at = 0;
    g_rp.ended = false;
    g_rp.restart_to = -1;
    g_rp.deadline = 0;
    if (!br_engine_init(e, &cfg)) { g_rp.restarting = false; return; }
    e->loop_hook = hook;
    g_rp.restarting = false;
    g_rp.engine = e;
    g_rp.seek_to = 0;
    g_rp.paused = false;
    if (to > 0) { g_rp.seek_to = to; }
    else g_rp.paused = g_rp.seek_resume_paused;
}

bool br_replay_after_frame(br_engine *e) {
    if (g_rp.mode != BR_REPLAY_PLAY) return true;
    if (g_rp.restart_to >= 0) { rp_restart(e); return true; }
    if (g_rp.length && g_rp.frame >= g_rp.length && !g_rp.ended) {
        g_rp.ended = true;
        BR_INFO("replay: ended at frame %llu; %u of %u hashes checked differed",
                (unsigned long long)g_rp.frame, g_rp.mismatches, g_rp.checked);
    }
    if (g_rp.ended && !g_rp.live) return false;
    /* The browser gives one frame per screen refresh: the frames a faster
     * speed (or skimming) needs on top are run here, within a time budget. */
    if (g_rp.live && g_rp.browser && !g_rp.paused && !g_rp.ended) {
        double t0 = rp_real_time();
        bool flat = g_rp.speed == 0 || g_rp.seek_to;
        while (e->running && !g_rp.ended && !g_rp.paused && g_rp.restart_to < 0) {
            double t = rp_real_time();
            if (t - t0 > 0.012) break;
            if (!flat) {
                double due;
                if (g_rp.anchor_t <= 0) break;
                due = (double)g_rp.anchor_frame + (t - g_rp.anchor_t) * 60.0 * g_rp.speed;
                if ((double)g_rp.frame >= due) break;
            }
            if (!br_engine_frame(e)) break;
            if (g_rp.length && g_rp.frame >= g_rp.length) break;
        }
    }
    return true;
}

br_replay_mode br_replay_mode_now(void) { return g_rp.mode; }
uint64_t br_replay_frame(void) { return g_rp.frame; }
uint64_t br_replay_length(void) { return g_rp.length; }
int br_replay_segment_count(void) { return g_rp.seg_count; }
int br_replay_segment_at(uint64_t frame) {
    int i, at = -1;
    for (i = 0; i < g_rp.seg_count; ++i) if (g_rp.segs[i].start <= frame) at = i;
    return at;
}
const char *br_replay_segment_name(int i) { return i >= 0 && i < g_rp.seg_count ? g_rp.segs[i].name : ""; }
uint64_t br_replay_segment_start(int i) { return i >= 0 && i < g_rp.seg_count ? g_rp.segs[i].start : 0; }
bool br_replay_ended(void) { return g_rp.ended; }
uint32_t br_replay_mismatches(uint32_t *checked, uint64_t *first) {
    if (checked) *checked = g_rp.checked;
    if (first) *first = g_rp.first_mismatch;
    return g_rp.mismatches;
}
uint32_t br_replay_last_hash(void) { return g_rp.last_hash; }
