/*
 * The whole world, from the room files (the rooms folder): every room's tiles,
 * its ways in and out (edges, doorways, cave mouths and stairs), and what
 * lies in it. travel() plans a way from room to room over it, then walks it
 * one room at a time; inside a room the walking is done on the live tiles.
 */
#include "bot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void stop_walking(void);
extern bool g_allow_unlock;
extern bool g_fight_in_cellars;

#define MAX_ROOMS 320
#define MAX_THINGS 24

typedef struct { float x, y; char to[32]; float px, py; bool hidden; } wr_warp;
typedef struct { float x, y; char kind[24]; char flag[32]; int price, min_hearts; bool on_clear, on_solve; } wr_pickup;

typedef struct wr_room {
    char name[32];
    char area[8];              /* "ow", "d1".., "cave" */
    int rx, ry;
    char tiles[11][17];
    char solid[32];
    char exit_[4][32];         /* n s e w */
    float exit_px, exit_py;
    wr_warp warps[4];
    int warp_count;
    wr_pickup pickups[MAX_THINGS];
    int pickup_count;
    int enemies;
    bool stays_clear;
    char shut[8], sealed[8];
    int braziers, gongs, plates, need_pages;
    int tier;                  /* its stage of the game (the land: how tough its enemies are) */
} wr_room;

static wr_room g_rooms[MAX_ROOMS];
static int g_room_count;
static char g_game_dir[512];

static const char *SIDES = "nsew";

int world_find(const char *name) {
    int i;
    for (i = 0; i < g_room_count; ++i) if (strcmp(g_rooms[i].name, name) == 0) return i;
    return -1;
}

/* ---- loading ------------------------------------------------------------------- */

static void unquote(const char *s, char *out, size_t size) {
    size_t n = 0;
    while (*s == ' ') s++;
    if (*s == '"') s++;
    while (*s && *s != '"' && *s != '\n' && n + 1 < size) {
        if (*s == '\\' && s[1]) s++;
        out[n++] = *s++;
    }
    out[n] = 0;
}

/* a var from a script's vars string: name = value */
static bool var_of(const char *vars, const char *name, char *out, size_t size) {
    char pat[64];
    const char *p;
    snprintf(pat, sizeof(pat), "%s = ", name);
    p = vars;
    while ((p = strstr(p, pat)) != NULL) {
        if (p == vars || p[-1] == ' ' || p[-1] == ',') {
            const char *v = p + strlen(pat);
            size_t n = 0;
            if (*v == '"') {
                v++;
                while (*v && *v != '"' && n + 1 < size) out[n++] = *v++;
            } else if (strncmp(v, "vec2(", 5) == 0) {
                while (*v && *v != ')' && n + 1 < size) out[n++] = *v++;
                if (n + 1 < size) out[n++] = ')';
            } else {
                while (*v && *v != ',' && n + 1 < size) out[n++] = *v++;
            }
            out[n] = 0;
            return true;
        }
        p += strlen(pat);
    }
    return false;
}

static void vec_of(const char *s, float *x, float *y) {
    const char *p = strstr(s, "vec2(");
    *x = *y = 0;
    if (p) sscanf(p + 5, "%f , %f", x, y);
}

/* The room vars string unescaped: \" -> " */
static void unescape(const char *in, char *out, size_t size) {
    size_t n = 0;
    while (*in == ' ') in++;
    if (*in == '"') in++;
    while (*in && n + 1 < size) {
        if (*in == '\\' && in[1]) { in++; out[n++] = *in++; continue; }
        if (*in == '"') break;
        out[n++] = *in++;
    }
    out[n] = 0;
}

static void load_room(const char *path, const char *name) {
    size_t size;
    char *text = (char *)br_file_read_all(path, &size), *line, *save = NULL;
    wr_room *R;
    char ename[32] = "", script[64] = "", vars[2048] = "";
    float px = 0, py = 0;
    if (!text || g_room_count >= MAX_ROOMS) { br_free(text); return; }
    R = &g_rooms[g_room_count++];
    memset(R, 0, sizeof(*R));
    snprintf(R->name, sizeof(R->name), "%.31s", name);
    if (strncmp(name, "cave", 4) == 0) snprintf(R->area, sizeof(R->area), "cave");
    else {
        char a[8];
        int x, y;
        if (sscanf(name, "%7[^_]_%d_%d", a, &x, &y) == 3) { snprintf(R->area, sizeof(R->area), "%s", a); R->rx = x; R->ry = y; }
    }
    R->exit_px = 128; R->exit_py = 200;
    /* entity by entity: flush when the next starts */
#define FLUSH() do { \
        if (strcmp(ename, "Room") == 0) { \
            char v[31]; \
            if (var_of(vars, "exit_n", v, sizeof(v))) snprintf(R->exit_[0], 32, "%s", v); \
            if (var_of(vars, "exit_s", v, sizeof(v))) snprintf(R->exit_[1], 32, "%s", v); \
            if (var_of(vars, "exit_e", v, sizeof(v))) snprintf(R->exit_[2], 32, "%s", v); \
            if (var_of(vars, "exit_w", v, sizeof(v))) snprintf(R->exit_[3], 32, "%s", v); \
            if (var_of(vars, "exit_pos", v, sizeof(v))) vec_of(v, &R->exit_px, &R->exit_py); \
            if (var_of(vars, "stays_clear", v, sizeof(v))) R->stays_clear = strcmp(v, "true") == 0; \
            if (var_of(vars, "shut", v, sizeof(v))) snprintf(R->shut, sizeof(R->shut), "%.7s", v); \
            if (var_of(vars, "sealed", v, sizeof(v))) snprintf(R->sealed, sizeof(R->sealed), "%.7s", v); \
            if (var_of(vars, "need_pages", v, sizeof(v))) R->need_pages = atoi(v); \
            if (var_of(vars, "tier", v, sizeof(v))) R->tier = atoi(v); \
        } else if (strcmp(ename, "Warp") == 0 && R->warp_count < 4) { \
            wr_warp *W = &R->warps[R->warp_count++]; \
            char v[31]; \
            W->x = px; W->y = py + 64; \
            if (var_of(vars, "to", v, sizeof(v))) snprintf(W->to, sizeof(W->to), "%s", v); \
            W->px = 128; W->py = 216; \
            if (var_of(vars, "pos", v, sizeof(v))) vec_of(v, &W->px, &W->py); \
            W->hidden = var_of(vars, "hidden", v, sizeof(v)) && strcmp(v, "true") == 0; \
        } else if (strcmp(ename, "Pickup") == 0 && R->pickup_count < MAX_THINGS) { \
            wr_pickup *P = &R->pickups[R->pickup_count++]; \
            char v[31]; \
            P->x = px; P->y = py + 64; \
            snprintf(P->kind, sizeof(P->kind), "bean"); \
            if (var_of(vars, "kind", v, sizeof(v))) snprintf(P->kind, sizeof(P->kind), "%.23s", v); \
            if (var_of(vars, "flag", v, sizeof(v))) snprintf(P->flag, sizeof(P->flag), "%s", v); \
            if (var_of(vars, "price", v, sizeof(v))) P->price = atoi(v); \
            if (var_of(vars, "min_hearts", v, sizeof(v))) P->min_hearts = atoi(v); \
            P->on_clear = var_of(vars, "on_clear", v, sizeof(v)) && strcmp(v, "true") == 0; \
            P->on_solve = var_of(vars, "on_solve", v, sizeof(v)) && strcmp(v, "true") == 0; \
        } else if (strcmp(ename, "Enemy") == 0) { \
            R->enemies++; \
        } else if (strcmp(ename, "Brazier") == 0) { \
            R->braziers++; \
        } else if (strcmp(ename, "Gong") == 0) { \
            R->gongs++; \
        } \
        ename[0] = 0; script[0] = 0; vars[0] = 0; px = py = 0; \
    } while (0)
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *l = line;
        if (strcmp(line, "entity") == 0) { FLUSH(); continue; }
        while (*l == ' ') l++;
        if (strncmp(l, "text = ", 7) == 0 && !ename[0]) unquote(l + 7, ename, sizeof(ename));
        else if (strncmp(l, "position = ", 11) == 0) sscanf(l + 11, "%f %f", &px, &py);
        else if (strncmp(l, "file = ", 7) == 0) unquote(l + 7, script, sizeof(script));
        else if (strncmp(l, "vars = ", 7) == 0) unescape(l + 7, vars, sizeof(vars));
        else if (strncmp(l, "solid = ", 8) == 0) unquote(l + 8, R->solid, sizeof(R->solid));
        else if (strncmp(l, "tiles = ", 8) == 0) {
            char t[200];
            int r;
            unquote(l + 8, t, sizeof(t));
            if (strlen(t) == 176) for (r = 0; r < 11; ++r) { memcpy(R->tiles[r], t + r * 16, 16); R->tiles[r][16] = 0; }
            if (strlen(t) == 176) { int q; for (q = 0; q < 176; ++q) if (t[q] == 'B') R->plates++; }
        }
    }
    FLUSH();
#undef FLUSH
    br_free(text);
}

void world_load(const char *game_dir) {
    char dir[600];
    br_dir_list list;
    int i;
    snprintf(g_game_dir, sizeof(g_game_dir), "%s", game_dir);
    snprintf(dir, sizeof(dir), "%s/rooms", game_dir);
    if (!br_fs_list(dir, false, &list)) { fprintf(stderr, "pantrybot: no rooms in %s\n", dir); return; }
    for (i = 0; i < list.count; ++i) {
        char path[900], stem[64];
        if (!br_path_has_ext(list.entries[i].name, ".scene")) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, list.entries[i].name);
        br_path_stem(stem, sizeof(stem), list.entries[i].name);
        load_room(path, stem);
    }
    br_dir_list_free(&list);
}

/* ---- what is known of a room now --------------------------------------------------- */

static int side_index(char s) { return s == 'n' ? 0 : s == 's' ? 1 : s == 'e' ? 2 : 3; }

static const int DOOR_C[4][4][2] = {
    { {7, 0}, {8, 0}, {7, 1}, {8, 1} }, { {7, 9}, {8, 9}, {7, 10}, {8, 10} },
    { {14, 5}, {15, 5}, {-1, -1}, {-1, -1} }, { {0, 5}, {1, 5}, {-1, -1}, {-1, -1} }
};

/* The room's tile at a cell: the live map for the room Pip is in, else
 * the file's, with the doors opened since (flags). */
static char room_tile(int ri, int c, int r) {
    wr_room *R = &g_rooms[ri];
    char t;
    int s, k;
    if (c < 0 || c > 15 || r < 0 || r > 10) return 0;
    if (strcmp(room_name(), R->name) == 0 && in_game() && strcmp(game_state(), "play") == 0) return tile_cell(c, r);
    t = R->tiles[r][c];
    if (R->area[0] == 'd') {
        for (s = 0; s < 4; ++s) {
            char flag[64];
            for (k = 0; k < 4; ++k) if (DOOR_C[s][k][0] == c && DOOR_C[s][k][1] == r) break;
            if (k == 4) continue;
            snprintf(flag, sizeof(flag), "open:%s:%c", R->name, SIDES[s]);
            if (has_flag(flag)) t = ',';
            if (strchr(R->sealed, SIDES[s])) {                       /* shut until its puzzle is solved */
                bool solved;
                snprintf(flag, sizeof(flag), "solved:%s", R->name);
                solved = has_flag(flag) || (R->need_pages > 0 && gnum("pages") >= R->need_pages);
                if (!solved) t = 'H';
            }
        }
    }
    return t;
}

typedef struct plan_opts {
    bool use_keys;             /* locked doors count as open (each costs a key) */
    bool use_bombs;            /* cracked walls count as open */
    bool avoid_unclear;        /* (unused) */
} plan_opts;

static plan_opts g_opts;
static int g_max_tier = 99;               /* keep out of the land's tougher parts (planning) */
static int g_plan_from = -1, g_plan_to = -1;

static bool walkable(int ri, int c, int r) {
    wr_room *R = &g_rooms[ri];
    char t = room_tile(ri, c, r);
    if (!t) return false;
    if (!strchr(R->solid, t)) return true;
    if (t == 'L' && g_opts.use_keys) return true;
    if (t == '~' && has_item("plank")) {
        char a = room_tile(ri, c - 1, r), b = room_tile(ri, c + 1, r), u = room_tile(ri, c, r - 1), d = room_tile(ri, c, r + 1);
        if ((a != '~' && b != '~') || (u != '~' && d != '~')) return true;
    }
    if (t == 'O' && has_item("mitt")) return true;          /* (the rooms are made so pushing opens the way) */
    return false;
}

/* Where a cave's way back out leads: the warp that leads into it. */
static bool cave_return(const char *cave, int *ri, int *c, int *r) {
    int i, k;
    for (i = 0; i < g_room_count; ++i) {
        if (strcmp(g_rooms[i].area, "ow") != 0) continue;
        for (k = 0; k < g_rooms[i].warp_count; ++k) {
            if (strcmp(g_rooms[i].warps[k].to, cave) == 0) {
                *ri = i;
                *c = (int)(g_rooms[i].warps[k].x / 16);
                *r = (int)((g_rooms[i].warps[k].y - 64) / 16) + 1;
                return true;
            }
        }
    }
    return false;
}

static bool warp_open(int ri, int k) {
    wr_warp *W = &g_rooms[ri].warps[k];
    char flag[96];
    if (!W->hidden) return true;
    snprintf(flag, sizeof(flag), "found:%s:%s", g_rooms[ri].name, W->to);
    if (has_flag(flag)) return true;
    /* in the room now: the live map shows it opened */
    if (strcmp(room_name(), g_rooms[ri].name) == 0) {
        int c = (int)(W->x / 16), r = (int)((W->y - 64) / 16);
        return tile_cell(c, r) == 'C';
    }
    return false;
}

/* ---- planning: a breadth-first search over (room, cell) ---------------------------- */

#define NODE(ri, c, r) (((ri) * 11 + (r)) * 16 + (c))
#define NODES (MAX_ROOMS * 176)

typedef struct { int prev; unsigned char how; char side; signed char warp; } plan_link;
static plan_link g_link[NODES];
static int g_q[NODES];
static unsigned char g_is_goal[NODES];
static bool g_goal_set;                  /* plan() stops at any node marked in g_is_goal */
static int g_found_node;

enum { HOW_START = 1, HOW_WALK, HOW_EDGE, HOW_WARP, HOW_EXIT };

/* The step after leaving a room by a side: the room and cell Pip comes to. */
static bool edge_target(int ri, char side, int c, int r, int *tri, int *tc, int *tr) {
    wr_room *R = &g_rooms[ri];
    const char *ex = R->exit_[side_index(side)];
    if (ex[0]) {
        if (strcmp(ex, "back") == 0) return cave_return(R->name, tri, tc, tr);
        *tri = world_find(ex);
        if (*tri < 0) return false;
        *tc = (int)(R->exit_px / 16);
        *tr = (int)((R->exit_py - 64) / 16);
        return true;
    }
    if (strcmp(R->area, "cave") == 0) return false;
    {
        char name[32];
        int nx = R->rx, ny = R->ry;
        if (side == 'n') ny--;
        if (side == 's') ny++;
        if (side == 'e') nx++;
        if (side == 'w') nx--;
        snprintf(name, sizeof(name), "%s_%d_%d", R->area, nx, ny);
        *tri = world_find(name);
        if (*tri < 0) return false;
        *tc = c; *tr = r;
        if (side == 'n') *tr = 10;
        if (side == 's') *tr = 0;
        if (side == 'e') *tc = 0;
        if (side == 'w') *tc = 15;
        if (R->area[0] == 'd') {                         /* cellars: two tiles on into the next room */
            if (side == 'n') *tr = 8;
            if (side == 's') *tr = 2;
            if (side == 'e') *tc = 2;
            if (side == 'w') *tc = 13;
        }
        return true;
    }
}

/* A way, as steps: each is a room, the cell to reach in it, and how to go on from there. */
typedef struct { int room; int c, r; unsigned char how; char side; int warp; } plan_step;
static plan_step g_plan[2048];
static int g_plan_len;

static bool room_has_goal(int ri) {
    int k;
    for (k = 0; k < 176; ++k) if (g_is_goal[ri * 176 + k]) return true;
    return false;
}

static bool plan_once(int from_ri, int fc, int fr, int to_ri, int tc, int tr);

/* Plans keeping to the parts of the land Pip is ready for (a stage or so past
 * the cellars he has cleared), and only through the rest if there's no
 * other way. */
static bool plan(int from_ri, int fc, int fr, int to_ri, int tc, int tr) {
    int pages = (int)gnum("pages");
    g_plan_from = from_ri;
    g_plan_to = to_ri;
    g_max_tier = pages + 2;
    if (gnum("weapon") >= 4) g_max_tier = 99;
    if (plan_once(from_ri, fc, fr, to_ri, tc, tr)) return true;
    g_max_tier = 99;
    return plan_once(from_ri, fc, fr, to_ri, tc, tr);
}

static bool plan_once(int from_ri, int fc, int fr, int to_ri, int tc, int tr) {
    int head = 0, tail = 0, k, found = -1;
    for (k = 0; k < g_room_count * 176; ++k) g_link[k].how = 0;
    g_link[NODE(from_ri, fc, fr)].how = HOW_START;
    g_link[NODE(from_ri, fc, fr)].prev = -1;
    g_q[tail++] = NODE(from_ri, fc, fr);
    while (head < tail) {
        int cur = g_q[head++], ri = cur / 176, rest = cur % 176, r = rest / 16, c = rest % 16, d;
        wr_room *R = &g_rooms[ri];
        if (g_goal_set ? g_is_goal[cur] != 0 : (ri == to_ri && (tc < 0 || (c == tc && r == tr)))) { found = cur; break; }
        /* walking */
        for (d = 0; d < 4; ++d) {
            int nc = c + (d == 0 ? 1 : d == 1 ? -1 : 0), nr = r + (d == 2 ? 1 : d == 3 ? -1 : 0);
            int n;
            if (nc < 0 || nc > 15 || nr < 0 || nr > 10) {
                char side = nc < 0 ? 'w' : nc > 15 ? 'e' : nr < 0 ? 'n' : 's';
                int tri, ttc, ttr;
                if (!edge_target(ri, side, c, r, &tri, &ttc, &ttr)) continue;
                if (g_rooms[tri].area[0] == 'o' && g_rooms[tri].tier > g_max_tier && tri != g_plan_to && !(g_goal_set && room_has_goal(tri))) continue;
                n = NODE(tri, ttc, ttr);
                if (g_link[n].how) continue;
                g_link[n].how = HOW_EDGE;
                g_link[n].prev = cur;
                g_link[n].side = side;
                g_q[tail++] = n;
                continue;
            }
            if (!walkable(ri, nc, nr)) continue;
            n = NODE(ri, nc, nr);
            if (g_link[n].how) continue;
            g_link[n].how = HOW_WALK;
            g_link[n].prev = cur;
            g_q[tail++] = n;
        }
        /* warps in this cell */
        for (k = 0; k < R->warp_count; ++k) {
            int wc = (int)(R->warps[k].x / 16), wr = (int)((R->warps[k].y - 64) / 16), tri, n;
            if (wc != c || wr != r || !warp_open(ri, k)) continue;
            tri = world_find(R->warps[k].to);
            if (tri < 0) continue;
            n = NODE(tri, (int)(R->warps[k].px / 16), (int)((R->warps[k].py - 64) / 16));
            if (g_link[n].how) continue;
            g_link[n].how = HOW_WARP;
            g_link[n].prev = cur;
            g_link[n].warp = (signed char)k;
            g_q[tail++] = n;
        }
    }
    g_found_node = found;
    if (found < 0) return false;
    /* back from the goal: the steps are the cells where Pip leaves each room */
    {
        int n = found, count = 0, i;
        plan_step tmp[2048];
        tmp[count].room = found / 176; tmp[count].c = found % 16; tmp[count].r = (found % 176) / 16;
        tmp[count].how = 0; tmp[count].side = 0; tmp[count].warp = -1;
        count++;
        while (g_link[n].how != HOW_START && count < 2048) {
            int p = g_link[n].prev;
            if (g_link[n].how == HOW_EDGE || g_link[n].how == HOW_WARP) {
                tmp[count].room = p / 176; tmp[count].c = p % 16; tmp[count].r = (p % 176) / 16;
                tmp[count].how = g_link[n].how; tmp[count].side = g_link[n].side; tmp[count].warp = g_link[n].warp;
                count++;
            }
            n = p;
        }
        g_plan_len = count;
        for (i = 0; i < count; ++i) g_plan[i] = tmp[count - 1 - i];
    }
    return true;
}

/* ---- going there ------------------------------------------------------------------------ */

static bool hero_cell(int *c, int *r) {
    br_vec2 h = hero_pos();
    cell_of(br_v2(h.x, h.y + 4), c, r);
    if (*c < 0) *c = 0;
    if (*c > 15) *c = 15;
    if (*r < 0) *r = 0;
    if (*r > 10) *r = 10;
    return true;
}

bool leave_room(char side);

/* Goes to a cell of a room (c < 0: anywhere in the room). */
bool fight_boss(int cellar);
bool bomb_side(char side);

/* In a cellar, a room's enemies are fought as Pip comes in: its shutters
 * won't open until they're gone, and they stay gone. */
static bool clear_here(void) {
    int ri = world_find(room_name());
    if (ri < 0 || g_rooms[ri].area[0] != 'd') return true;
    if (foe_count() == 0 && spawners_left() == 0) return true;
    if (world_is_boss_room(room_name())) return fight_boss(g_rooms[ri].area[1] - '0');
    return fight_room(7200);
}

static bool travel_ex(const char *room, int c, int r, bool keys, bool bombs) {
    int tri = world_find(room), guard = 0, stuck_here = 0, shutter_waits = 0;
    if (tri < 0) { bot_log("travel: no room %s", room); return false; }
    while (guard++ < 200) {
        int ri, hc, hr;
        plan_step *s;
        if (!wait_play(1200)) return false;
        if (g_fight_in_cellars && !clear_here()) {
            bot_log("travel: %s not cleared", room_name());
            if (!wait_play(1200)) return false;
            if (++guard > 20) return false;
            continue;
        }
        ri = world_find(room_name());
        if (ri < 0) { bot_log("travel: where is %s?", room_name()); return false; }
        hero_cell(&hc, &hr);
        if (ri == tri && c < 0) return true;
        g_opts.use_keys = keys;
        g_opts.use_bombs = bombs;
        if (!plan(ri, hc, hr, tri, c, r)) {
            /* from a neighbouring cell (he may be standing in a doorway's edge) */
            bot_log("travel: no way from %s (%d,%d) to %s (%d,%d); %d foes, %d spawners, state %s", room_name(), hc, hr, room, c, r,
                    foe_count(), spawners_left(), game_state());
            {
                int rr, cc;
                char line[17];
                for (rr = 0; rr < 11; ++rr) { for (cc = 0; cc < 16; ++cc) line[cc] = tile_cell(cc, rr); line[16] = 0; bot_log("  %s", line); }
            }
            if (g_rooms[ri].area[0] == 'd' && ++shutter_waits < 6) {  /* shutters still down: they'll open */
                bot_wait(45);
                if (foe_count() > 0 || spawners_left() > 0) fight_room(3600);
                continue;
            }
            return false;
        }
        s = &g_plan[0];
        if (s->room != ri) { bot_log("travel: the plan starts elsewhere"); return false; }
        if (s->how == 0) {                               /* the goal is in this room */
            g_allow_unlock = keys;
            {
                bool ok = goto_cell(s->c, s->r, 2400);
                g_allow_unlock = false;
                if (!ok) {
                    int rr, cc;
                    char line[17];
                    br_vec2 h = hero_pos();
                    bot_log("travel: couldn't walk to (%d,%d) in %s from %g,%g", s->c, s->r, room_name(), h.x, h.y);
                    for (rr = 0; rr < 11; ++rr) { for (cc = 0; cc < 16; ++cc) line[cc] = tile_cell(cc, rr); line[16] = 0; bot_log("  %s", line); }
                }
                return ok;
            }
        }
        g_allow_unlock = keys;
        if (s->how == HOW_EDGE) {
            char before[32];
            snprintf(before, sizeof(before), "%s", room_name());
            goto_cell(s->c, s->r, 2400);
            if (g_rooms[ri].area[0] == 'd') {
                int sd = side_index(s->side);
                if (room_tile(ri, DOOR_C[sd][0][0], DOOR_C[sd][0][1]) == 'X' && !bomb_side(s->side)) {
                    bot_log("travel: couldn't bomb the wall");
                    return false;
                }
            }
            if (!leave_room(s->side)) {
                g_allow_unlock = false;
                if (strcmp(before, room_name()) == 0) {
                    if (++stuck_here > 4) {
                        bot_log("travel: couldn't leave %s by %c", before, s->side);
                        return false;
                    }
                    bot_log("travel: in the way leaving %s by %c (%d foes): clearing it", before, s->side, foe_count());
                    bot_shot("stuck_leaving");
                    if (foe_count() > 0) fight_room(1800);
                    continue;
                }
            }
            stuck_here = 0;
        } else if (s->how == HOW_WARP) {
            wr_warp *W = &g_rooms[ri].warps[s->warp];
            if (!enter_warp_at((int)(W->x / 16), (int)((W->y - 64) / 16))) {
                g_allow_unlock = false;
                bot_log("travel: couldn't take the way into %s", W->to);
                return false;
            }
        }
        g_allow_unlock = false;
    }
    return false;
}

bool g_fight_in_cellars = true;
bool travel(const char *room, int c, int r) { return travel_ex(room, c, r, false, false); }
bool travel_with(const char *room, int c, int r, bool keys, bool bombs) { return travel_ex(room, c, r, keys, bombs); }
bool travel_keys(const char *room, int c, int r) { return travel_ex(room, c, r, true, false); }
bool goto_room(const char *room) { return travel_ex(room, -1, -1, false, false); }

/* ---- the world's things, for the route ------------------------------------------------- */

int world_pickups(const char *room, br_vec2 *at, const char **kinds, int cap) {
    int ri = world_find(room), i, n = 0;
    if (ri < 0) return 0;
    for (i = 0; i < g_rooms[ri].pickup_count && n < cap; ++i) {
        at[n] = br_v2(g_rooms[ri].pickups[i].x, g_rooms[ri].pickups[i].y);
        kinds[n] = g_rooms[ri].pickups[i].kind;
        n++;
    }
    return n;
}

bool world_warp_cell(const char *room, const char *to, int *c, int *r) {
    int ri = world_find(room), k;
    if (ri < 0) return false;
    for (k = 0; k < g_rooms[ri].warp_count; ++k) {
        if (strcmp(g_rooms[ri].warps[k].to, to) == 0) {
            *c = (int)(g_rooms[ri].warps[k].x / 16);
            *r = (int)((g_rooms[ri].warps[k].y - 64) / 16);
            return true;
        }
    }
    return false;
}

int world_room_count(void) { return g_room_count; }

/* Which of these targets (room, cell) is nearest by the way Pip would go,
 * or -1 if none can be reached. */
int world_nearest(const char (*rooms)[32], const int *cs, const int *rs, int n, bool keys, bool bombs) {
    int i, ri, hc, hr, k;
    bool ok;
    if (!wait_play(600)) return -1;
    ri = world_find(room_name());
    if (ri < 0) return -1;
    hero_cell(&hc, &hr);
    memset(g_is_goal, 0, (size_t)g_room_count * 176);
    for (i = 0; i < n; ++i) {
        int t = world_find(rooms[i]);
        if (t >= 0 && cs[i] >= 0 && cs[i] < 16 && rs[i] >= 0 && rs[i] < 11) g_is_goal[NODE(t, cs[i], rs[i])] = 1;
    }
    g_opts.use_keys = keys;
    g_opts.use_bombs = bombs;
    g_goal_set = true;
    ok = plan(ri, hc, hr, -1, -1, -1);
    g_goal_set = false;
    if (!ok) return -1;
    for (k = 0; k < n; ++k) {
        int t = world_find(rooms[k]);
        if (t >= 0 && NODE(t, cs[k], rs[k]) == g_found_node) return k;
    }
    return -1;
}

/* A cellar's pickups not yet taken. */
int world_cellar_pickups(const char *area, cellar_thing *out, int cap) {
    int i, k, n = 0;
    for (i = 0; i < g_room_count; ++i) {
        wr_room *R = &g_rooms[i];
        if (strcmp(R->area, area) != 0) continue;
        for (k = 0; k < R->pickup_count && n < cap; ++k) {
            wr_pickup *P = &R->pickups[k];
            if (!P->flag[0] || has_flag(P->flag)) continue;      /* (drops and the like have none) */
            snprintf(out[n].room, sizeof(out[n].room), "%.31s", R->name);
            out[n].at = br_v2(P->x, P->y);
            snprintf(out[n].kind, sizeof(out[n].kind), "%s", P->kind);
            snprintf(out[n].flag, sizeof(out[n].flag), "%s", P->flag);
            out[n].on_clear = P->on_clear;
            out[n].on_solve = P->on_solve;
            out[n].price = P->price;
            out[n].min_hearts = P->min_hearts;
            cell_of(out[n].at, &out[n].c, &out[n].r);
            if (strchr(R->solid, R->tiles[out[n].r][out[n].c])) {        /* on a statue: the floor beside it */
                static const int off[6][2] = { {0, 1}, {1, 1}, {-1, 1}, {0, -1}, {1, 0}, {-1, 0} };
                int o;
                for (o = 0; o < 6; ++o) {
                    int cc = out[n].c + off[o][0], rr = out[n].r + off[o][1];
                    if (cc >= 0 && cc < 16 && rr >= 0 && rr < 11 && !strchr(R->solid, R->tiles[rr][cc])) { out[n].c = cc; out[n].r = rr; break; }
                }
            }
            n++;
        }
    }
    return n;
}

/* A pickup of a kind in a room, from its file: where it is, and a cell to reach it from. */
bool world_pickup(const char *room, const char *kind, cellar_thing *out) {
    int ri = world_find(room), k;
    if (ri < 0) return false;
    for (k = 0; k < g_rooms[ri].pickup_count; ++k) {
        wr_pickup *P = &g_rooms[ri].pickups[k];
        wr_room *R = &g_rooms[ri];
        if (strcmp(P->kind, kind) != 0) continue;
        memset(out, 0, sizeof(*out));
        snprintf(out->room, sizeof(out->room), "%s", room);
        snprintf(out->kind, sizeof(out->kind), "%s", P->kind);
        snprintf(out->flag, sizeof(out->flag), "%s", P->flag);
        out->at = br_v2(P->x, P->y);
        out->price = P->price;
        out->min_hearts = P->min_hearts;
        out->on_clear = P->on_clear;
        cell_of(out->at, &out->c, &out->r);
        if (strchr(R->solid, R->tiles[out->r][out->c])) {
            static const int off[6][2] = { {0, 1}, {1, 1}, {-1, 1}, {0, -1}, {1, 0}, {-1, 0} };
            int o;
            for (o = 0; o < 6; ++o) {
                int cc = out->c + off[o][0], rr = out->r + off[o][1];
                if (cc >= 0 && cc < 16 && rr >= 0 && rr < 11 && !strchr(R->solid, R->tiles[rr][cc])) { out->c = cc; out->r = rr; break; }
            }
        }
        return true;
    }
    return false;
}

/* A cellar's rooms with a puzzle still to solve (a walkable cell in each). */
int world_puzzles(const char *area, cellar_thing *out, int cap) {
    int i, n = 0;
    for (i = 0; i < g_room_count && n < cap; ++i) {
        wr_room *R = &g_rooms[i];
        char flag[48];
        int c, r, best = 1000;
        if (strcmp(R->area, area) != 0 || (!R->braziers && !R->gongs && !R->plates)) continue;
        snprintf(flag, sizeof(flag), "solved:%s", R->name);
        if (has_flag(flag)) continue;
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].room, sizeof(out[n].room), "%s", R->name);
        snprintf(out[n].kind, sizeof(out[n].kind), "puzzle");
        out[n].c = 7; out[n].r = 5;
        for (r = 2; r <= 8; ++r)
            for (c = 2; c <= 13; ++c) {
                int d = abs(c - 7) + abs(r - 5);
                if (!strchr(R->solid, R->tiles[r][c]) && d < best) { best = d; out[n].c = c; out[n].r = r; }
            }
        n++;
    }
    return n;
}

/* Does the room (from its file) hold a guardian? (Its heart pie waits for it.) */
bool world_is_boss_room(const char *room) {
    int ri = world_find(room), k;
    if (ri < 0 || g_rooms[ri].area[0] != 'd') return false;
    for (k = 0; k < g_rooms[ri].pickup_count; ++k)
        if (strcmp(g_rooms[ri].pickups[k].kind, "heart_pie") == 0) return true;
    return false;
}
