/*
 * Fighting: picking the enemy to go for, standing where the spoon reaches
 * it without touching it, facing it and poking. Also staying alive: the game
 * over screen (try again), and keeping clear of things that hurt.
 */
#include "bot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct foe {
    br_entity e;
    br_vec2 p;
    float size;
    bool appearing, guard, boss;
    bool hittable;             /* a poke would hurt it now */
    br_vec2 dir;               /* which way it faces (front_guard) */
} foe;

#define MAX_FOES 48
static foe g_foes[MAX_FOES];
static int g_foe_count;

static br_vec2 parse_vec(const char *s) {
    float x = 0, y = 0;
    const char *p = strstr(s, "(");
    if (p) sscanf(p + 1, "%f , %f", &x, &y);
    return br_v2(x, y);
}

/* Can a poke hurt it now? (The guardians each have their moments.) */
static bool foe_hittable(br_entity e) {
    const char *title = estr(e, "title"), *mode;
    double v;
    br_world *w = bot_world();
    if (w && br_script_get_number(w, e, "attached", &v) && v == 0) return false;     /* a noodle head, loose */
    if (!title[0]) return true;
    mode = estr(e, "mode");
    if (strcmp(title, "NOODLE HYDRA") == 0) return false;                         /* iron: its heads */
    if (strcmp(title, "BIG DILL") == 0) return strcmp(mode, "dizzy") == 0;
    if (strcmp(title, "SUGAR GOLEM") == 0) return strcmp(mode, "stuck") == 0;
    if (strcmp(title, "JAMMERWOCK") == 0) return strcmp(mode, "up") == 0;
    if (strcmp(title, "SPICE WRAITH") == 0) return strcmp(mode, "solid") == 0 || strcmp(mode, "gather") == 0;
    if (strcmp(title, "LUCKY COOKIE") == 0) return strcmp(estr(e, "crumbs"), "[]") == 0;
    if (strcmp(title, "GUMDROP KING") == 0) return !(enum_(e, "king", 0) > 0 && enum_(e, "whole", 1) <= 0);
    return true;
}

/* The enemies of the room Pip is in. */
int scan_foes(void) {
    br_entity list[MAX_FOES];
    br_world *w = bot_world();
    br_entity room = BR_ENTITY_NULL, game = ent_named("Game");
    int n, i;
    g_foe_count = 0;
    if (!w || !game) return 0;
    br_script_get_entity(w, game, "room", &room);
    n = ents_named("Enemy", list, MAX_FOES);
    for (i = 0; i < n; ++i) {
        foe *f;
        br_vec2 p;
        if (room && br_parent_of(w, list[i]) != room) continue;
        p = ent_pos(list[i]);
        if (p.x < -50) continue;
        f = &g_foes[g_foe_count++];
        f->e = list[i];
        f->p = p;
        f->size = (float)enum_(list[i], "size", 6);
        f->appearing = enum_(list[i], "appear_t", 0) > 0;
        f->guard = enum_(list[i], "front_guard", 0) > 0;
        f->dir = f->guard ? parse_vec(estr(list[i], "dir")) : br_v2(0, 0);
        f->boss = estr(list[i], "title")[0] != 0;
        f->hittable = foe_hittable(list[i]);
    }
    return g_foe_count;
}

int foe_count(void) { return scan_foes(); }

int foe_positions(br_vec2 *at, float *size, int cap) {
    int i, n = 0;
    scan_foes();
    for (i = 0; i < g_foe_count && n < cap; ++i) { at[n] = g_foes[i].p; size[n] = g_foes[i].size; n++; }
    return n;
}

int spawners_left(void) {
    br_entity list[32];
    return ents_named("Spawner", list, 32);
}

/* ---- staying alive ------------------------------------------------------------------- */

int g_deaths;

/* After a game over: try again. True if it happened (whatever was being done is off). */
bool handle_death(void) {
    const char *st = game_state();
    if (strcmp(st, "dead") != 0 && strcmp(st, "over") != 0) return false;
    g_deaths++;
    bot_log("Pip fell (%d so far) in %s", g_deaths, room_name());
    keys_release_all();
    while (strcmp(game_state(), "dead") == 0) bot_frame();
    bot_wait(70);                                       /* the choices come up after a second */
    while (strcmp(game_state(), "over") == 0) key_tap(BR_KEY_ENTER);
    wait_play(600);
    return true;
}

/* ---- where to stand ------------------------------------------------------------------ */

static const br_vec2 DIRS[4] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };   /* up down left right */

/* Can Pip poke this foe from where he is, facing `d`? */
static bool in_reach(const foe *f, br_vec2 h, int d) {
    br_vec2 tip = br_v2(h.x + DIRS[d].x * 14, h.y + DIRS[d].y * 14);
    float dx = f->p.x - tip.x, dy = f->p.y - tip.y;
    float along = (f->p.x - h.x) * DIRS[d].x + (f->p.y - h.y) * DIRS[d].y;
    if (along < 4) return false;
    if (f->guard && (DIRS[d].x * f->dir.x + DIRS[d].y * f->dir.y) < -0.5f) return false;
    return sqrtf(dx * dx + dy * dy) < 10 + f->size - 3;
}

/* Lined up for the spark (at full health it flies on from the spoon). */
static bool lined_up(const foe *f, br_vec2 h, int d) {
    float along = (f->p.x - h.x) * DIRS[d].x + (f->p.y - h.y) * DIRS[d].y;
    float across = fabsf((f->p.x - h.x) * DIRS[d].y) + fabsf((f->p.y - h.y) * DIRS[d].x);
    if (along < 10 || along > 150) return false;
    if (f->guard && (DIRS[d].x * f->dir.x + DIRS[d].y * f->dir.y) < -0.5f) return false;
    return across < 5;
}

static bool full_health(void) { return gnum("hp") >= gnum("hearts_max") * 2; }

static int dir_index_of_facing(void) {
    const char *f = hero_facing();
    if (strcmp(f, "up") == 0) return 0;
    if (strcmp(f, "down") == 0) return 1;
    if (strcmp(f, "left") == 0) return 2;
    return 3;
}

/* If something can be hit right now, turn to it and poke. True if he did. */
bool strike_if_able(void) {
    br_vec2 h;
    int i, d, best = -1, bd = -1;
    float bestd = 1e9f;
    if (gnum("weapon") <= 0 || strcmp(game_state(), "play") != 0) return false;
    if (hero_busy()) return false;
    {
        br_entity hero = ent_named("Hero");
        if (enum_(hero, "cooldown", 0) > 0) return false;
    }
    scan_foes();
    h = hero_pos();
    for (i = 0; i < g_foe_count; ++i) {
        foe *f = &g_foes[i];
        if (f->appearing || !f->hittable) continue;
        for (d = 0; d < 4; ++d) {
            bool ok = in_reach(f, h, d) || (full_health() && lined_up(f, h, d));
            float dist;
            if (!ok) continue;
            dist = fabsf(f->p.x - h.x) + fabsf(f->p.y - h.y);
            if (d == dir_index_of_facing()) dist -= 8;
            if (dist < bestd) { bestd = dist; best = i; bd = d; }
        }
    }
    if (best < 0) return false;
    if (bd != dir_index_of_facing()) face((bot_dir)bd);
    poke();
    return true;
}

/* ---- what they throw ------------------------------------------------------------------ */

/* Something flying at Pip that will reach him soon: turn the pot lid to it
 * (facing it, not poking), or step aside if the lid won't stop it. True if
 * he did something about it this frame. */
bool dodge_shots(void) {
    br_entity shots[48];
    int n = ents_named("Shot", shots, 48), i, d;
    br_vec2 h = hero_pos();
    bool tray = has_item("tray");
    float soonest = 1e9f;
    int threat = -1;
    br_vec2 tv = br_v2(0, 0);
    if (n == 0 || strcmp(game_state(), "play") != 0 || hero_busy()) return false;
    for (i = 0; i < n; ++i) {
        br_vec2 p = ent_pos(shots[i]);
        br_vec2 v = parse_vec(estr(shots[i], "vel"));
        float rx = p.x - h.x, ry = p.y - h.y, vv = v.x * v.x + v.y * v.y, t, cx, cy;
        if (vv < 1) continue;
        t = -(rx * v.x + ry * v.y) / vv;
        if (t < 0 || t > 0.22f) continue;
        cx = rx + v.x * t;
        cy = ry + v.y * t;
        if (fabsf(cx) > 10 || fabsf(cy) > 10) continue;
        if (t < soonest) { soonest = t; threat = i; tv = v; }
    }
    if (threat < 0) return false;
    {
        bool blockable = enum_(shots[threat], "blockable", 1) > 0 || tray;
        static const br_vec2 DV[4] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
        float len = sqrtf(tv.x * tv.x + tv.y * tv.y), bestdot = 0;
        int best = -1;
        for (d = 0; d < 4; ++d) {
            float dt_ = (tv.x * DV[d].x + tv.y * DV[d].y) / len;
            if (dt_ < bestdot) { bestdot = dt_; best = d; }
        }
        if (blockable && best >= 0 && bestdot < -0.75f) {
            static const char *names[4] = { "up", "down", "left", "right" };
            if (strcmp(hero_facing(), names[best]) != 0) face((bot_dir)best);
            else { stop_walking(); bot_frame(); }
            return true;
        }
        /* out of its way: across its path */
        {
            br_vec2 side = br_v2(-tv.y / len, tv.x / len);
            br_vec2 a = br_v2(h.x + side.x * 14, h.y + side.y * 14), b = br_v2(h.x - side.x * 14, h.y - side.y * 14);
            br_vec2 to = hero_fits(a) ? a : b;
            key_hold(BR_KEY_LEFT, to.x < h.x - 1);
            key_hold(BR_KEY_RIGHT, to.x > h.x + 1);
            key_hold(BR_KEY_UP, to.y < h.y - 1);
            key_hold(BR_KEY_DOWN, to.y > h.y + 1);
            bot_frame();
            return true;
        }
    }
}

/* ---- what they leave behind ------------------------------------------------------------- */

/* A drop worth walking over: beans, a heart when hurt, bombs. Not things for sale. */
static bool drop_wanted(br_entity p) {
    const char *k = estr(p, "kind");
    if (enum_(p, "price", 0) > 0 || enum_(p, "shown", 1) <= 0) return false;
    if (enum_(p, "timeout", 0) <= 0) return false;          /* only drops (they vanish): the rest is the route's */
    if (strcmp(k, "bean") == 0 || strcmp(k, "bean5") == 0) return true;
    if (strcmp(k, "bomb") == 0) return gnum("bombs") < gnum("bombs_max");
    if (strcmp(k, "heart") == 0) return gnum("hp") < gnum("hearts_max") * 2;
    return false;
}

static br_entity nearest_drop(float within) {
    br_entity list[32];
    br_world *w = bot_world();
    br_entity room = BR_ENTITY_NULL, game = ent_named("Game"), best = BR_ENTITY_NULL;
    br_vec2 h = hero_pos();
    float bd = within;
    int n, i;
    if (!w || !game) return BR_ENTITY_NULL;
    br_script_get_entity(w, game, "room", &room);
    n = ents_named("Pickup", list, 32);
    for (i = 0; i < n; ++i) {
        br_vec2 p;
        float d;
        if (room && br_parent_of(w, list[i]) != room) continue;
        if (!drop_wanted(list[i])) continue;
        p = ent_pos(list[i]);
        d = fabsf(p.x - h.x) + fabsf(p.y - h.y);
        if (d < bd) { bd = d; best = list[i]; }
    }
    return best;
}

bool goto_point(br_vec2 p, float tol, int max_frames);
extern bool g_in_hook;

/* Picks up the drops lying about (within `within` pixels). */
void collect_drops(float within) {
    int guard = 0;
    bool was = g_in_hook;
    g_in_hook = true;
    while (guard++ < 12) {
        br_entity d;
        uint64_t before = bot_frames();
        if (strcmp(game_state(), "play") != 0) break;
        d = nearest_drop(within);
        if (!d) break;
        goto_point(ent_pos(d), 7, 240);
        if (bot_frames() == before) break;
        if (ent_alive(d) && guard > 3) break;
    }
    g_in_hook = was;
}

/* While walking: hit what's in reach, and pick up a drop close by. */
bool walk_hook_default(void) {
    br_entity d;
    if (strike_if_able()) return true;
    if (dodge_shots()) return true;
    if (strcmp(game_state(), "play") != 0) return false;
    d = nearest_drop(56);
    if (d) {
        uint64_t before = bot_frames();
        collect_drops(56);
        return bot_frames() != before;
    }
    return false;
}

/* ---- going for them ---------------------------------------------------------------------- */

bool goto_point(br_vec2 p, float tol, int max_frames);

/* A spot to strike a foe from: on one of its sides, 19 pixels off, where Pip fits. */
static bool attack_spot(const foe *f, br_vec2 *out) {
    br_vec2 h = hero_pos();
    int d;
    float best = 1e9f;
    bool found = false;
    float off = f->size + 11 < 17 ? 17 : f->size + 11;     /* (touching is within 6 + size) */
    for (d = 0; d < 4; ++d) {
        br_vec2 p = br_v2(f->p.x - DIRS[d].x * off, f->p.y - DIRS[d].y * off);
        float dist;
        if (f->guard && (DIRS[d].x * f->dir.x + DIRS[d].y * f->dir.y) < -0.5f) continue;
        if (p.x < 8 || p.x > 248 || p.y < 72 || p.y > 232) continue;
        if (!hero_fits(p)) continue;
        dist = fabsf(p.x - h.x) + fabsf(p.y - h.y);
        if (dist < best) { best = dist; *out = p; found = true; }
    }
    return found;
}

/* Fights until the room has no enemies left (or the time runs out). */
extern bool g_in_hook;

/* Keeps away from every enemy (and what they throw): goes a few frames
 * towards the reachable spot farthest from them. */
void evade(int frames) {
    br_vec2 h = hero_pos(), best = h;
    float best_score = -1e9f;
    int c, r, i;
    br_entity shots[48];
    int ns = ents_named("Shot", shots, 48);
    scan_foes();
    for (r = 2; r <= 9; ++r) {
        for (c = 1; c <= 14; ++c) {
            br_vec2 p = cell_center(c, r);
            float near = 1e9f, score;
            p.y -= 4;
            if (!hero_fits(p)) continue;
            for (i = 0; i < g_foe_count; ++i) {
                float dx = g_foes[i].p.x - p.x, dy = g_foes[i].p.y - p.y, d = sqrtf(dx * dx + dy * dy) - g_foes[i].size;
                if (d < near) near = d;
            }
            for (i = 0; i < ns; ++i) {
                br_vec2 sp = ent_pos(shots[i]);
                float dx = sp.x - p.x, dy = sp.y - p.y, d = sqrtf(dx * dx + dy * dy);
                if (d < near) near = d;
            }
            score = (near > 90 ? 90 : near) - 0.15f * (fabsf(p.x - h.x) + fabsf(p.y - h.y));
            if (score > best_score) { best_score = score; best = p; }
        }
    }
    {
        uint64_t before = bot_frames();
        goto_point(best, 3, frames);
        if (bot_frames() == before) { stop_walking(); bot_frame(); }
    }
}

/* Fights until the room has no enemies left (or the time runs out). */
/* One step of going for the nearest enemy that can be hurt (not `ignore`):
 * a strike, or a few frames towards a spot to strike from. False if there's
 * nothing to go for. */
bool fight_step(br_entity ignore) {
    int i, target = -1;
    float best = 1e9f;
    br_vec2 h, spot;
    bool was = g_in_hook;
    g_in_hook = true;
    if (strike_if_able()) { g_in_hook = was; return true; }
    if (dodge_shots()) { g_in_hook = was; return true; }
    scan_foes();
    h = hero_pos();
    for (i = 0; i < g_foe_count; ++i) {
        float d = fabsf(g_foes[i].p.x - h.x) + fabsf(g_foes[i].p.y - h.y);
        if (!g_foes[i].hittable || g_foes[i].e == ignore) continue;
        if (g_foes[i].appearing) d += 200;
        if (d < best) { best = d; target = i; }
    }
    if (target < 0) { g_in_hook = was; return false; }
    if (!attack_spot(&g_foes[target], &spot)) { stop_walking(); bot_frame(); g_in_hook = was; return true; }
    {
        uint64_t before = bot_frames();
        goto_point(spot, 3, 8);
        if (bot_frames() == before) { stop_walking(); bot_frame(); }
    }
    g_in_hook = was;
    return true;
}

bool fight_room(int max_frames) {
    uint64_t start = bot_frames();
    int f = 0, stuck_shot = 0;
    bool was = g_in_hook, result = false;
    g_in_hook = true;                                   /* (no striking from inside the walking) */
    while ((f = (int)(bot_frames() - start)) < max_frames) {
        int i, target = -1;
        float best = 1e9f;
        br_vec2 h, spot;
        if (handle_death()) break;
        if (!in_game()) break;
        if (strcmp(game_state(), "play") != 0) { stop_walking(); bot_frame(); continue; }
        if (scan_foes() == 0 && spawners_left() == 0) {
            stop_walking();
            collect_drops(400);
            result = true;
            break;
        }
        if (strike_if_able()) continue;
        if (dodge_shots()) continue;
        h = hero_pos();
        for (i = 0; i < g_foe_count; ++i) {
            float d = fabsf(g_foes[i].p.x - h.x) + fabsf(g_foes[i].p.y - h.y);
            if (!g_foes[i].hittable) continue;
            if (g_foes[i].appearing) d += 200;
            if (d < best) { best = d; target = i; }
        }
        if (target < 0) { evade(8); continue; }          /* nothing to hit just now: keep clear */
        if (!attack_spot(&g_foes[target], &spot)) {
            stop_walking();                              /* nowhere to stand yet: wait for it to move */
            bot_frame();
            continue;
        }
        {
            uint64_t before = bot_frames();
            goto_point(spot, 3, 8);                      /* a few frames towards it, then look again */
            if (bot_frames() == before) {                /* no way there just now */
                if (stuck_shot++ == 60) bot_shot("no_way");
                stop_walking();
                bot_frame();
            }
        }
    }
    if (!result) bot_shot("fight_timeout");
    stop_walking();
    g_in_hook = was;
    return result;
}
