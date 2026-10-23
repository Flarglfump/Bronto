/*
 * The Banquet Hall's puzzles: braziers to light with the fire chili (all at
 * once), pressure plates to cover with crates (the oven mitt), and a gong to
 * ring with a skewer once a crate is out of the way.
 */
#include "bot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void stop_walking(void);

/* ---- braziers ------------------------------------------------------------------- */

static int unlit_braziers(br_entity *out, int cap) {
    br_entity list[8];
    int n = ents_named("Brazier", list, 8), i, k = 0;
    for (i = 0; i < n; ++i) if (enum_(list[i], "lit", 0) <= 0) { if (k < cap) out[k] = list[i]; k++; }
    return k;
}

static bool light_braziers(void) {
    int rounds = 0;
    if (!has_item("chili")) return false;
    while (rounds++ < 40) {
        br_entity left[8], best = BR_ENTITY_NULL;
        int n = unlit_braziers(left, 8), i, d;
        float bd = 1e9f;
        br_vec2 h = hero_pos(), stand = h;
        bot_dir face_to = DIR_UP;
        if (n == 0) return true;
        if (!wait_play(600)) return false;
        for (i = 0; i < n; ++i) {
            br_vec2 p = ent_pos(left[i]);
            float dist = fabsf(p.x - h.x) + fabsf(p.y - h.y);
            if (dist < bd) { bd = dist; best = left[i]; }
        }
        {
            br_vec2 b = ent_pos(best);
            int bc, br_;
            static const int dc[4] = { 0, 0, -1, 1 }, dr[4] = { -1, 1, 0, 0 };   /* stand above below left right */
            static const bot_dir faces[4] = { DIR_DOWN, DIR_UP, DIR_RIGHT, DIR_LEFT };
            float sd = 1e9f;
            cell_of(b, &bc, &br_);
            for (d = 0; d < 4; ++d) {
                br_vec2 p = cell_center(bc + dc[d], br_ + dr[d]);
                float dist;
                p.y -= 4;
                if (!hero_fits(p)) continue;
                dist = fabsf(p.x - h.x) + fabsf(p.y - h.y);
                if (dist < sd) { sd = dist; stand = p; face_to = faces[d]; }
            }
        }
        select_item("chili");
        goto_point(stand, 1.5f, 600);
        {
            br_entity fires[4];
            int g = 0;
            while (ents_named("Fire", fires, 4) > 0 && g++ < 120) { stop_walking(); bot_frame(); }
        }
        face(face_to);
        use_item();
        bot_wait(3);
    }
    return unlit_braziers(NULL, 0) == 0;
}

/* ---- pressure plates ------------------------------------------------------------------ */

static int g_plate_c[16], g_plate_r[16], g_plates;

static bool plates_covered(const void *state, int cell, void *u) {
    int i;
    BR_UNUSED(cell);
    BR_UNUSED(u);
    for (i = 0; i < g_plates; ++i) if (!sk_state_crate(state, g_plate_c[i], g_plate_r[i])) return false;
    return true;
}

static bool cover_plates(void) {
    int c, r, tries;
    g_plates = 0;
    for (r = 0; r < 11; ++r)
        for (c = 0; c < 16; ++c)
            if (tile_cell(c, r) == 'B' && g_plates < 16) { g_plate_c[g_plates] = c; g_plate_r[g_plates] = r; g_plates++; }
    if (g_plates == 0) return true;
    for (tries = 0; tries < 30; ++tries) {
        int got = sokoban(plates_covered, NULL);
        if (got == 0) return true;
        if (got < 0) return false;
    }
    return false;
}

/* ---- the gong ------------------------------------------------------------------------- */

static int g_gong_c, g_gong_r;

/* Lined up with the gong along a row, with nothing solid between (jam is fine). */
static bool gong_in_line(const void *state, int cell, void *u) {
    int c = cell % 16, r = cell / 16, k;
    BR_UNUSED(u);
    if (r != g_gong_r || c == g_gong_c) return false;
    for (k = (c < g_gong_c ? c : g_gong_c) + 1; k < (c < g_gong_c ? g_gong_c : c); ++k) {
        char t = tile_cell(k, r);
        if (sk_state_crate(state, k, r)) return false;
        if (t != '~' && t != '.' && t != ',' && t != 'B') return false;
    }
    return true;
}

static bool ring_gong(void) {
    br_entity g = ent_named("Gong");
    int tries;
    if (!g || enum_(g, "rung", 0) > 0) return true;
    if (!has_item("skewers") || gnum("beans") <= 0) { bot_log("no skewers (or beans) for the gong"); return false; }
    cell_of(ent_pos(g), &g_gong_c, &g_gong_r);
    for (tries = 0; tries < 12 && enum_(g, "rung", 0) <= 0; ++tries) {
        int got = sokoban(gong_in_line, NULL);
        if (got < 0) return false;
        if (got == 1) continue;
        {
            int c = g_sk_goal_cell % 16, r = g_sk_goal_cell / 16;
            br_vec2 p = cell_center(c, r);
            p.y = ent_pos(g).y - 2;
            goto_point(p, 1.5f, 600);
            select_item("skewers");
            face(c > g_gong_c ? DIR_LEFT : DIR_RIGHT);
            use_item();
            bot_wait(40);
        }
    }
    return enum_(g, "rung", 0) > 0;
}

/* Solves the puzzle of the room Pip is in. */
bool solve_puzzle(void) {
    char flag[48];
    int tries;
    snprintf(flag, sizeof(flag), "solved:%s", room_name());
    for (tries = 0; tries < 4 && !has_flag(flag); ++tries) {
        if (foe_count() > 0 || spawners_left() > 0) fight_room(7200);
        if (ent_named("Brazier")) light_braziers();
        cover_plates();
        if (ent_named("Gong")) ring_gong();
        bot_wait(10);
    }
    bot_log("puzzle in %s: %s", room_name(), has_flag(flag) ? "solved" : "not solved");
    return has_flag(flag);
}
