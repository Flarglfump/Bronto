/*
 * Reading Pantry Quest: where Pip is, the room's tiles, the Game's variables.
 */
#include "bot.h"

#include <stdio.h>
#include <string.h>

static br_world *g_cache_w;
static br_entity g_game, g_hero;

static void refresh(void) {
    br_world *w = bot_world();
    if (w != g_cache_w || !br_entity_alive(w, g_game) || !br_entity_alive(w, g_hero)) {
        g_cache_w = w;
        g_game = ent_named("Game");
        g_hero = ent_named("Hero");
    }
}

int ents_named(const char *name, br_entity *out, int cap) {
    br_world *w = bot_world();
    br_builtin_ids ids;
    const br_component_pool *pool;
    uint32_t i;
    int n = 0;
    if (!w || !br_builtin_ids_of(w, &ids)) return 0;
    pool = &w->pools[ids.name];
    for (i = 0; i < pool->count; ++i) {
        const br_name *nm = (const br_name *)(const void *)(pool->data + (size_t)i * pool->stride);
        if (strcmp(nm->text, name) == 0) {
            if (n < cap) out[n] = pool->entities[i];
            n++;
        }
    }
    return n < cap ? n : cap;
}

br_entity ent_named(const char *name) {
    br_entity e = BR_ENTITY_NULL;
    return ents_named(name, &e, 1) ? e : BR_ENTITY_NULL;
}

bool ent_alive(br_entity e) {
    br_world *w = bot_world();
    return w && e != BR_ENTITY_NULL && br_entity_alive(w, e);
}

br_vec2 ent_pos(br_entity e) {
    br_world *w = bot_world();
    if (!w || !br_entity_alive(w, e)) return br_v2(-1000, -1000);
    return br_world_transform(w, e).position;
}

double enum_(br_entity e, const char *var, double otherwise) {
    double v;
    br_world *w = bot_world();
    if (w && br_entity_alive(w, e) && br_script_get_number(w, e, var, &v)) return v;
    return otherwise;
}

const char *estr(br_entity e, const char *var) {
    static char bufs[4][8192];
    static int at;
    char *buf = bufs[at = (at + 1) % 4];
    br_world *w = bot_world();
    buf[0] = 0;
    if (w && br_entity_alive(w, e)) br_script_get_string(w, e, var, buf, sizeof(bufs[0]));
    return buf;
}

bool in_game(void) { refresh(); return g_game != BR_ENTITY_NULL && g_hero != BR_ENTITY_NULL; }

double gnum(const char *var) { refresh(); return enum_(g_game, var, 0); }
const char *gstr(const char *var) { refresh(); return estr(g_game, var); }
bool gstr_is(const char *var, const char *value) { return strcmp(gstr(var), value) == 0; }
const char *room_name(void) { return gstr("room_name"); }
const char *game_state(void) { refresh(); return g_game ? gstr("state") : ""; }
br_vec2 hero_pos(void) { refresh(); return ent_pos(g_hero); }

/* Is `item` one of a list's strings (as the script prints a list)? */
static bool list_has(const char *list, const char *item) {
    size_t n = strlen(item);
    const char *p = list;
    while ((p = strstr(p, item)) != NULL) {
        char before = p == list ? '[' : p[-1], after = p[n];
        if ((before == '"' || before == '[' || before == ' ' || before == ',') &&
            (after == '"' || after == ']' || after == ',' || after == 0))
            return true;
        p += n;
    }
    return false;
}

bool has_flag(const char *flag) { return list_has(gstr("flags"), flag); }
bool has_item(const char *item) {
    if (list_has(gstr("items"), item)) return true;
    return list_has(gstr("treasures"), item);
}

br_entity room_map(void) {
    br_entity m = BR_ENTITY_NULL;
    br_world *w = bot_world();
    refresh();
    if (w && g_game) br_script_get_entity(w, g_game, "map", &m);
    return m;
}

char tile_at(br_vec2 p) {
    br_world *w = bot_world();
    br_entity m = room_map();
    if (!w || !m) return 0;
    return br_tilemap_get(w, m, p, NULL, NULL);
}

br_vec2 cell_center(int c, int r) { return br_v2((float)(c * 16 + 8), (float)(64 + r * 16 + 8)); }
void cell_of(br_vec2 p, int *c, int *r) {
    *c = (int)(p.x / 16.0f);
    *r = (int)((p.y - 64.0f) / 16.0f);
    if (p.x < 0) *c = -1;
    if (p.y < 64) *r = -1;
}
char tile_cell(int c, int r) {
    if (c < 0 || c > 15 || r < 0 || r > 10) return 0;
    return tile_at(cell_center(c, r));
}
bool tile_solid_cell(int c, int r) {
    br_world *w = bot_world();
    br_entity m = room_map();
    if (c < 0 || c > 15 || r < 0 || r > 10 || !w || !m) return true;
    return br_tilemap_solid(w, m, cell_center(c, r));
}
