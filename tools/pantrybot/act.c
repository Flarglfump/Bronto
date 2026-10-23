/*
 * Doing things: walking (a path found over the room's tiles, as Pip's own
 * collision sees them), facing, poking, using the B item.
 */
#include "bot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Pip's feet against the room (scripts/hero.bs: fits, solid_at) ---------------- */

bool g_allow_unlock;                    /* walk through locked doors (spending keys) */

static bool plank_ok(br_vec2 p) {
    if (!has_item("plank") || tile_at(p) != '~') return false;
    if (tile_at(br_v2(p.x - 16, p.y)) != '~' && tile_at(br_v2(p.x + 16, p.y)) != '~') return true;
    if (tile_at(br_v2(p.x, p.y - 16)) != '~' && tile_at(br_v2(p.x, p.y + 16)) != '~') return true;
    return false;
}

static bool solid_px(br_vec2 p) {
    br_world *w = bot_world();
    br_entity m = room_map();
    char t;
    if (!w || !m) return true;
    if (!br_tilemap_solid(w, m, p)) return false;
    t = tile_at(p);
    if (t == 'L' && g_allow_unlock && gnum("keys") > 0) return false;
    if (t == '~' && plank_ok(p)) return false;
    return true;
}

bool hero_fits(br_vec2 p) {
    return !(solid_px(br_v2(p.x - 7, p.y + 1)) || solid_px(br_v2(p.x + 6, p.y + 1)) ||
             solid_px(br_v2(p.x - 7, p.y + 7)) || solid_px(br_v2(p.x + 6, p.y + 7)));
}

/* ---- a path over a 2-pixel grid of Pip's positions ------------------------------------ */

#define GW 129                           /* x 0..256 */
#define GH 89                            /* y 64..240 */
#define GX(i) ((float)((i) * 2))
#define GY(j) ((float)(64 + (j) * 2))

static signed char g_fit[GW * GH];       /* -1 unknown, 0 no, 1 yes */
static int g_prev[GW * GH];
static int g_queue[GW * GH];
static float g_danger[GW * GH];          /* extra cost near enemies */

static bool fit_ij(int i, int j) {
    int k = j * GW + i;
    if (g_fit[k] < 0) g_fit[k] = hero_fits(br_v2(GX(i), GY(j))) ? 1 : 0;
    return g_fit[k] == 1;
}

/* Finds a path from Pip to the nearest grid point where `goal` says yes.
 * Fills `out` with points (the last is the goal) and returns how many; 0
 * if there is none (or he is there). `bounds`: stay inside the room's
 * walkable rectangle (not over its edges). */
typedef bool (*goal_fn)(float x, float y, void *u);

/* Keeps paths clear of enemies: the places where one would touch Pip. */
bool g_avoid_foes = true;
static void mark_danger(void) {
    br_vec2 at[64];
    float size[64];
    int n, i, a, b;
    memset(g_danger, 0, sizeof(g_danger));
    if (!g_avoid_foes) return;
    n = foe_positions(at, size, 64);
    for (i = 0; i < n; ++i) {
        float r = 6 + size[i] + 3;
        int i0 = (int)((at[i].x - r) / 2), i1 = (int)((at[i].x + r) / 2) + 1;
        int j0 = (int)((at[i].y - 64 - r) / 2), j1 = (int)((at[i].y - 64 + r) / 2) + 1;
        for (b = j0; b <= j1; ++b)
            for (a = i0; a <= i1; ++a)
                if (a >= 0 && b >= 0 && a < GW && b < GH) g_danger[b * GW + a] = 1;
    }
}

static int find_path(goal_fn goal, void *u, br_vec2 *out, int cap, bool bounds) {
    br_vec2 h = hero_pos();
    int si = (int)floorf(h.x / 2 + 0.5f), sj = (int)floorf((h.y - 64) / 2 + 0.5f);
    int head = 0, tail = 0, found = -1, k, n;
    static const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    memset(g_fit, -1, sizeof(g_fit));
    for (k = 0; k < GW * GH; ++k) g_prev[k] = -2;
    mark_danger();
    if (si < 0) si = 0;
    if (si >= GW) si = GW - 1;
    if (sj < 0) sj = 0;
    if (sj >= GH) sj = GH - 1;
    g_prev[sj * GW + si] = -1;
    g_queue[tail++] = sj * GW + si;
    while (head < tail) {
        int cur = g_queue[head++], ci = cur % GW, cj = cur / GW, d;
        if (goal(GX(ci), GY(cj), u)) { found = cur; break; }
        for (d = 0; d < 8; ++d) {
            int ni = ci + dx[d], nj = cj + dy[d], nk;
            float x, y;
            if (ni < 0 || nj < 0 || ni >= GW || nj >= GH) continue;
            x = GX(ni); y = GY(nj);
            if (bounds && (x < 6 || x > 250 || y < 70 || y > 234)) continue;
            nk = nj * GW + ni;
            if (g_prev[nk] != -2) continue;
            if (!fit_ij(ni, nj)) continue;
            if (d >= 4 && (!fit_ij(ci + dx[d], cj) || !fit_ij(ci, cj + dy[d]))) continue;   /* no cutting corners */
            if (g_danger[nk] > 0.5f && !goal(x, y, u)) continue;
            g_prev[nk] = cur;
            g_queue[tail++] = nk;
        }
    }
    if (found < 0) return -1;
    n = 0;
    for (k = found; k >= 0 && g_prev[k] != -1; k = g_prev[k]) n++;
    if (n > cap) n = cap;
    {
        int idx = n - 1;
        for (k = found; k >= 0 && g_prev[k] != -1 && idx >= 0; k = g_prev[k]) out[idx--] = br_v2(GX(k % GW), GY(k / GW));
    }
    return n;
}

/* ---- keys for walking ------------------------------------------------------------------ */

static void walk_keys(int x, int y) {
    key_hold(BR_KEY_LEFT, x < 0);
    key_hold(BR_KEY_RIGHT, x > 0);
    key_hold(BR_KEY_UP, y < 0);
    key_hold(BR_KEY_DOWN, y > 0);
}

void stop_walking(void) { walk_keys(0, 0); }

/* Steers towards a point (one frame's keys). */
static void steer(br_vec2 to) {
    br_vec2 h = hero_pos();
    float dx = to.x - h.x, dy = to.y - h.y;
    int kx = dx > 0.8f ? 1 : dx < -0.8f ? -1 : 0, ky = dy > 0.8f ? 1 : dy < -0.8f ? -1 : 0;
    walk_keys(kx, ky);
}

static bool playing(void) { return strcmp(game_state(), "play") == 0; }

bool wait_play(int max_frames) {
    while (max_frames-- > 0) {
        if (in_game()) handle_death();
        if (in_game() && playing()) return true;
        stop_walking();
        bot_frame();
    }
    return false;
}

/* Something to do while walking (by default: hit whatever is in reach). */
bool walk_hook_default(void);
bool (*g_walk_hook)(void) = walk_hook_default;
bool g_in_hook;

typedef struct { br_vec2 p; float tol; } pt_goal;
static bool goal_point(float x, float y, void *u) {
    pt_goal *g = (pt_goal *)u;
    return fabsf(x - g->p.x) <= g->tol && fabsf(y - g->p.y) <= g->tol;
}

/* ---- crates (the oven mitt): leaning on one slides it a tile ------------------------------ */

static bool tile_walk(int c, int r) {
    br_vec2 p;
    if (c < 0 || c > 15 || r < 0 || r > 10) return false;
    p = cell_center(c, r);
    return hero_fits(br_v2(p.x, p.y - 4));            /* (Pip's feet in the lower half of the tile) */
}

/* No path: is there one that pushes crates out of the way? A small search
 * over (where Pip stands, where the crates are), as a Sokoban puzzle. Does
 * the first push on the way and returns true if the crate moved (the
 * caller looks again). */
typedef struct { uint64_t crates[3]; unsigned char cell; } sk_state;
#define SK_MAX 120000
static sk_state g_sk[SK_MAX];
static int g_sk_prev[SK_MAX];
static unsigned char g_sk_push[SK_MAX];           /* 0: a step; 1 + dir: a push in dir */
static int g_sk_hash[1 << 18];
int g_sk_goal_cell;                       /* where the search ended, when no push was needed */

static bool sk_has(const sk_state *s, int cell) { return (s->crates[cell >> 6] >> (cell & 63)) & 1u; }
static void sk_set(sk_state *s, int cell, bool on) {
    if (on) s->crates[cell >> 6] |= (uint64_t)1 << (cell & 63);
    else s->crates[cell >> 6] &= ~((uint64_t)1 << (cell & 63));
}
static unsigned sk_h(const sk_state *s) {
    uint64_t h = s->crates[0] * 0x9E3779B97F4A7C15ull ^ s->crates[1] * 0xC2B2AE3D27D4EB4Full ^ s->crates[2] * 0x165667B19E3779F9ull ^ s->cell;
    return (unsigned)((h ^ (h >> 29)) & ((1u << 18) - 1));
}

/* The search: from where Pip and the crates are now to a state `done`
 * accepts. Returns -1 if there's none, 0 if `done` holds now (no push
 * needed), 1 after making the first push on the way (the caller looks again). */

static bool sk_crate(const sk_state *s, int c, int r) { return c >= 0 && c < 16 && r >= 0 && r < 11 && sk_has(s, r * 16 + c); }
bool sk_state_crate(const void *state, int c, int r) { return sk_crate((const sk_state *)state, c, r); }

int sokoban(sk_goal_fn done, void *u) {
    static const int dc[4] = { 1, -1, 0, 0 }, dr[4] = { 0, 0, 1, -1 };
    static bool base[176], blocked[176];
    br_vec2 h = hero_pos();
    int hc, hr, head = 0, count = 0, found = -1, k;
    cell_of(br_v2(h.x, h.y + 4), &hc, &hr);
    if (hc < 0 || hc > 15 || hr < 0 || hr > 10) return -1;
    memset(blocked, 0, sizeof(blocked));                /* no crate goes onto a pickup */
    {
        br_entity list[48];
        int n = ents_named("Pickup", list, 48), i, c, r;
        for (i = 0; i < n; ++i) {
            br_vec2 p = ent_pos(list[i]);
            if (enum_(list[i], "shown", 1) <= 0) continue;
            for (r = 0; r < 11; ++r)
                for (c = 0; c < 16; ++c) {
                    br_vec2 cc = cell_center(c, r);
                    if (fabsf(cc.x - p.x) < 12 && fabsf(cc.y - p.y) < 12) blocked[r * 16 + c] = true;
                }
        }
    }
    memset(g_sk_hash, -1, sizeof(g_sk_hash));
    memset(&g_sk[0], 0, sizeof(g_sk[0]));
    for (k = 0; k < 176; ++k) {
        int c = k % 16, r = k / 16;
        if (tile_cell(c, r) == 'O') { sk_set(&g_sk[0], k, true); base[k] = true; }
        else base[k] = tile_walk(c, r);
    }
    g_sk[0].cell = (unsigned char)(hr * 16 + hc);
    g_sk_prev[0] = -1;
    g_sk_push[0] = 0;
    g_sk_hash[sk_h(&g_sk[0])] = 0;
    count = 1;
    while (head < count) {
        int cur = head++, d;
        if (done(&g_sk[cur], g_sk[cur].cell, u)) { found = cur; break; }
        for (d = 0; d < 4; ++d) {
            int c = g_sk[cur].cell % 16, r = g_sk[cur].cell / 16;
            int nc = c + dc[d], nr = r + dr[d], n = nr * 16 + nc;
            sk_state next;
            unsigned hh;
            bool push = false;
            if (nc < 0 || nc > 15 || nr < 0 || nr > 10 || !base[n]) continue;
            next = g_sk[cur];
            if (sk_has(&next, n)) {
                int bc = nc + dc[d], br = nr + dr[d], b = br * 16 + bc;
                char t;
                if (!has_item("mitt")) continue;
                if (bc < 2 || bc > 13 || br < 2 || br > 8 || sk_has(&next, b) || blocked[b]) continue;
                t = tile_cell(bc, br);
                if (t != '.' && t != 'B' && t != 'O') continue;
                sk_set(&next, n, false);
                sk_set(&next, b, true);
                push = true;
            }
            next.cell = (unsigned char)n;
            hh = sk_h(&next);
            while (g_sk_hash[hh] >= 0 && memcmp(&g_sk[g_sk_hash[hh]], &next, sizeof(next)) != 0) hh = (hh + 1) & ((1u << 18) - 1);
            if (g_sk_hash[hh] >= 0) continue;
            if (count >= SK_MAX) break;
            g_sk[count] = next;
            g_sk_prev[count] = cur;
            g_sk_push[count] = (unsigned char)(push ? 1 + d : 0);
            g_sk_hash[hh] = count;
            count++;
        }
    }
    if (found < 0) return -1;
    {
        int first = -1;
        for (k = found; k > 0; k = g_sk_prev[k]) if (g_sk_push[k]) first = k;
        if (first < 0) {
            g_sk_goal_cell = g_sk[found].cell;
            return 0;
        }
        {
            int from = g_sk[g_sk_prev[first]].cell, d = g_sk_push[first] - 1;
            int fc = from % 16, fr = from / 16, tc = fc + dc[d], tr = fr + dr[d];
            br_vec2 stand = cell_center(fc, fr);
            int f;
            stand.y -= 4;
            if (!goto_point(stand, 1.5f, 600)) return -1;
            for (f = 0; f < 50 && tile_cell(tc, tr) == 'O'; ++f) {
                walk_keys(dc[d], dr[d]);
                bot_frame();
            }
            stop_walking();
            bot_log("pushed the crate at (%d,%d)%s", tc, tr, tile_cell(tc, tr) == 'O' ? " - it didn't move" : "");
            return tile_cell(tc, tr) != 'O' ? 1 : -1;
        }
    }
}

typedef struct { goal_fn goal; void *u; } walk_goal;
static bool sk_walk_done(const void *s, int cell, void *u) {
    walk_goal *g = (walk_goal *)u;
    br_vec2 cc = cell_center(cell % 16, cell / 16);
    BR_UNUSED(s);
    return g->goal(cc.x, cc.y - 4, g->u) || g->goal(cc.x, cc.y, g->u);
}

static bool push_a_crate(goal_fn goal, void *u) {
    walk_goal g;
    int c, r, crates = 0;
    if (!has_item("mitt")) return false;
    for (r = 2; r <= 8; ++r) for (c = 2; c <= 13; ++c) if (tile_cell(c, r) == 'O') crates++;
    if (!crates) return false;
    g.goal = goal;
    g.u = u;
    return sokoban(sk_walk_done, &g) == 1;
}

/* Walks along a path to wherever `goal` is satisfied. Replans every few
 * frames. Returns true on arrival (or when `done` says so). */
static bool walk_to_goal(goal_fn goal, void *u, int max_frames, bool bounds, bool (*done)(void *), void *du) {
    br_vec2 path[2048];
    int n = 0, at = 0, since = 1000, stuck = 0, f;
    br_vec2 last = hero_pos();
    const char *room0;
    char room[32];
    room0 = room_name();
    snprintf(room, sizeof(room), "%s", room0);
    for (f = 0; f < max_frames; ++f) {
        br_vec2 h;
        if (!in_game()) return false;
        if (handle_death()) return false;
        if (done && done(du)) { stop_walking(); return true; }
        if (!playing()) {
            stop_walking();
            if (strcmp(room_name(), room) != 0) return done ? done(du) : false;
            bot_frame();
            continue;
        }
        if (g_walk_hook && !g_in_hook) {
            bool did;
            g_in_hook = true;
            did = g_walk_hook();
            g_in_hook = false;
            if (did) { since = 1000; f += 10; continue; }
        }
        h = hero_pos();
        if (goal(h.x, h.y, u) && !done) { stop_walking(); return true; }
        if (since > 12 || at >= n) {
            n = find_path(goal, u, path, 2048, bounds);
            if (n < 0 && g_avoid_foes) {                 /* enemies in the way: through them, then */
                g_avoid_foes = false;
                n = find_path(goal, u, path, 2048, bounds);
                g_avoid_foes = true;
            }
            if (n < 0 && push_a_crate(goal, u)) { since = 1000; continue; }
            if (n < 0) { stop_walking(); return false; }
            at = 0;
            since = 0;
            if (n == 0) {
                if (!done) { stop_walking(); return true; }
            }
        }
        /* the next point well ahead that's still in a straight line */
        while (at < n - 1 && fabsf(path[at].x - h.x) <= 1.5f && fabsf(path[at].y - h.y) <= 1.5f) at++;
        {
            int look = at;
            while (look + 1 < n && look < at + 3) look++;
            if (n > 0) steer(path[look]);
        }
        bot_frame();
        since++;
        if (fabsf(hero_pos().x - last.x) < 0.01f && fabsf(hero_pos().y - last.y) < 0.01f) {
            if (++stuck > 20) { since = 1000; stuck = 0; }
        } else {
            stuck = 0;
        }
        last = hero_pos();
    }
    stop_walking();
    return false;
}

bool goto_point(br_vec2 p, float tol, int max_frames) {
    pt_goal g;
    g.p = p;
    g.tol = tol;
    return walk_to_goal(goal_point, &g, max_frames, true, NULL, NULL);
}

bool goto_cell(int c, int r, int max_frames) { return goto_point(cell_center(c, r), 2.5f, max_frames); }

/* ---- leaving a room by its edge ------------------------------------------------------------ */

typedef struct { char side; char room[32]; } leave_goal;
static bool goal_edge(float x, float y, void *u) {
    leave_goal *g = (leave_goal *)u;
    switch (g->side) {
    case 'w': return x < 3;
    case 'e': return x > 253;
    case 'n': return y < 67;
    default:  return y > 237;
    }
}
static bool left_room(void *u) {
    leave_goal *g = (leave_goal *)u;
    return strcmp(room_name(), g->room) != 0;
}

bool leave_room(char side) {
    leave_goal g;
    bool ok;
    g.side = side;
    snprintf(g.room, sizeof(g.room), "%s", room_name());
    ok = walk_to_goal(goal_edge, &g, 1200, false, left_room, &g);
    stop_walking();
    if (!ok) return false;
    return wait_play(600);
}

/* ---- facing and the spoon ------------------------------------------------------------------- */

static const char *dir_name(bot_dir d) {
    switch (d) { case DIR_UP: return "up"; case DIR_DOWN: return "down"; case DIR_LEFT: return "left"; default: return "right"; }
}
static br_key dir_key(bot_dir d) {
    switch (d) { case DIR_UP: return BR_KEY_UP; case DIR_DOWN: return BR_KEY_DOWN; case DIR_LEFT: return BR_KEY_LEFT; default: return BR_KEY_RIGHT; }
}

const char *hero_facing(void) { return estr(ent_named("Hero"), "facing"); }

void face(bot_dir d) {
    int tries = 0;
    stop_walking();
    while (strcmp(hero_facing(), dir_name(d)) != 0 && tries++ < 4) {
        key_hold(dir_key(d), true);
        bot_frame();
        key_hold(dir_key(d), false);
    }
}

bool hero_busy(void) {
    br_entity h = ent_named("Hero");
    return enum_(h, "attack_t", 0) > 0 || enum_(h, "knock_t", 0) > 0;
}

void poke(void) {
    br_entity h = ent_named("Hero");
    int n = 0;
    stop_walking();
    while ((enum_(h, "attack_t", 0) > 0 || enum_(h, "cooldown", 0) > 0) && n++ < 30) bot_frame();
    key_tap(BR_KEY_X);
}

void use_item(void) { stop_walking(); key_tap(BR_KEY_Z); }

bool select_item(const char *item) {
    int n;
    if (!has_item(item)) return false;
    for (n = 0; n < 8 && !gstr_is("b_item", item); ++n) key_tap(BR_KEY_E);
    return gstr_is("b_item", item);
}

/* Walks up into the way in (a warp) at a cell, until the room changes. */
bool enter_warp_at(int c, int r) {
    char room[32];
    int f;
    br_vec2 at = cell_center(c, r);
    snprintf(room, sizeof(room), "%s", room_name());
    if (!goto_point(br_v2(at.x, at.y + 6), 2.5f, 1500)) return false;
    for (f = 0; f < 240; ++f) {
        br_vec2 h;
        if (strcmp(room_name(), room) != 0) break;
        h = hero_pos();
        if (playing()) walk_keys(h.x < at.x - 1 ? 1 : h.x > at.x + 1 ? -1 : 0, -1); else stop_walking();
        bot_frame();
    }
    stop_walking();
    if (strcmp(room_name(), room) == 0) return false;
    return wait_play(600);
}
