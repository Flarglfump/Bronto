/*
 * A cellar, start to finish: every room's enemies, every key, the map and
 * compass, the treasure, the guardian, the heart pie and the recipe page -
 * nearest first, spending keys (and bombs) only when nothing else is in reach.
 */
#include "bot.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool fight_boss(int cellar);

/* Takes a pickup at a world point in this room (fights the room first if it waits for that). */
bool take_pickup_at(br_vec2 p, const char *flag) {
    int tries;
    for (tries = 0; tries < 6; ++tries) {
        if (flag && flag[0] && has_flag(flag)) return true;
        if (foe_count() > 0 || spawners_left() > 0) {
            if (world_is_boss_room(room_name())) fight_boss(0);
            else fight_room(3600);
            continue;
        }
        goto_point(p, 8.0f, 600);                         /* (touching it is enough) */
        bot_wait(4);
        wait_play(600);
    }
    return !flag || !flag[0] || has_flag(flag);
}

/* Goes and gets a pickup of a kind in a room (a shop's: buys it). */
bool get_pickup(const char *room, const char *kind) {
    cellar_thing t;
    int tries;
    if (!world_pickup(room, kind, &t)) { bot_log("no %s in %s", kind, room); return false; }
    if (t.flag[0] && has_flag(t.flag)) return true;
    for (tries = 0; tries < 4; ++tries) {
        if (!travel(room, t.c, t.r)) continue;
        if (take_pickup_at(t.at, t.flag[0] ? t.flag : NULL)) {
            wait_play(1200);
            bot_log("got the %s in %s", kind, room);
            return true;
        }
    }
    return false;
}

/* Opens a hidden way in on the land: a cracked cliff (a bomb) or a dry bush
 * (the fire chili, or a bomb). Stands below it, facing it. */
bool reveal_warp(const char *room, const char *to, const char *item) {
    int c, r, tries;
    if (!world_warp_cell(room, to, &c, &r)) { bot_log("no way into %s in %s", to, room); return false; }
    for (tries = 0; tries < 4; ++tries) {
        if (tile_cell(c, r) == 'C' && strcmp(room_name(), room) == 0) return true;
        if (!travel(room, c, r + 1)) continue;
        if (tile_cell(c, r) == 'C') return true;
        if (!select_item(item)) { bot_log("no %s to open %s", item, to); return false; }
        goto_point(br_v2(cell_center(c, r + 1).x, cell_center(c, r + 1).y), 1.5f, 300);
        face(DIR_UP);
        use_item();
        bot_wait(strcmp(item, "bombs") == 0 ? 90 : 40);
        wait_play(600);
    }
    return tile_cell(c, r) == 'C';
}

/* Bombs the cracked wall on a side of this cellar room. */
static const int INSIDE[4][2] = { {7, 2}, {7, 8}, {13, 5}, {2, 5} };   /* n s e w: where to stand */

bool bomb_side(char side) {
    int s = side == 'n' ? 0 : side == 's' ? 1 : side == 'e' ? 2 : 3;
    br_vec2 at = cell_center(INSIDE[s][0], INSIDE[s][1]);
    bot_dir d = side == 'n' ? DIR_UP : side == 's' ? DIR_DOWN : side == 'e' ? DIR_RIGHT : DIR_LEFT;
    int tries;
    if (side == 'n' || side == 's') at.x += 8;                /* (between the doorway's two columns) */
    for (tries = 0; tries < 3; ++tries) {
        if (gnum("bombs") <= 0) { bot_log("no bombs for the wall"); return false; }
        if (!select_item("bombs")) return false;
        goto_point(at, 2, 900);
        face(d);
        use_item();
        goto_point(cell_center(7, 5), 6, 60);
        bot_wait(80);
        {
            int c = side == 'n' ? 7 : side == 's' ? 7 : side == 'e' ? 14 : 1;
            int r = side == 'n' ? 1 : side == 's' ? 9 : 5;
            if (tile_cell(c, r) != 'X') return true;
        }
    }
    return false;
}

/* Leaves the room by a doorway and comes straight back in. */
void step_out_and_back(void) {
    char here[32];
    const char *sides = "nsew";
    int i;
    snprintf(here, sizeof(here), "%s", room_name());
    for (i = 0; i < 4; ++i) {
        if (!leave_room(sides[i])) continue;
        if (strcmp(room_name(), here) != 0) {
            char back = sides[i] == 'n' ? 's' : sides[i] == 's' ? 'n' : sides[i] == 'e' ? 'w' : 'e';
            leave_room(back);
            return;
        }
    }
}

bool explore_cellar(int n) {
    char area[8], door[32], page[24];
    cellar_thing things[64];
    char rooms[64][32];
    int cs[64], rs[64], count, puzzles, i, rounds = 0, fails = 0;
    snprintf(area, sizeof(area), "d%d", n);
    snprintf(door, sizeof(door), "d%d_2_4", n);
    snprintf(page, sizeof(page), "page:d%d", n);
    while (rounds++ < 300) {
        int pick;
        bool keys = false, bombs = false;
        const char *here;
        if (has_flag(page)) { wait_play(1200); return true; }
        if (has_flag("won") || strcmp(game_state(), "won") == 0) return true;
        if (!wait_play(1200)) return false;
        here = room_name();
        if (strncmp(here, area, strlen(area)) != 0 || here[strlen(area)] != '_') {
            if (!travel(door, -1, -1)) return false;       /* (back in after a game over outside) */
            continue;
        }
        count = world_cellar_pickups(area, things, 64);
        {   /* (something waiting for a puzzle is only there once it's solved) */
            int k = 0;
            for (i = 0; i < count; ++i) {
                char solved[48];
                snprintf(solved, sizeof(solved), "solved:%.31s", things[i].room);
                if (things[i].on_solve && !has_flag(solved)) continue;
                things[k++] = things[i];
            }
            count = k;
        }
        puzzles = world_puzzles(area, things + count, 64 - count);
        for (i = 0; i < count + puzzles; ++i) { snprintf(rooms[i], 32, "%.31s", things[i].room); cs[i] = things[i].c; rs[i] = things[i].r; }
        if (count + puzzles == 0) return true;
        pick = world_nearest((const char (*)[32])rooms, cs, rs, count, false, false);
        if (pick < 0 && puzzles) {
            pick = world_nearest((const char (*)[32])(rooms + count), cs + count, rs + count, puzzles, false, false);
            if (pick >= 0) pick += count;
        }
        if (pick < 0) { keys = true; pick = world_nearest((const char (*)[32])rooms, cs, rs, count + puzzles, true, false); }
        if (pick < 0 && gnum("bombs") > 0) { bombs = true; pick = world_nearest((const char (*)[32])rooms, cs, rs, count + puzzles, true, true); }
        if (pick < 0) {
            bot_log("%s: nothing left in reach (%d things not taken)", area, count + puzzles);
            return false;
        }
        if (pick >= count) {
            bot_log("%s: for the puzzle in %s%s%s", area, things[pick].room, keys ? " (spending a key)" : "", bombs ? " (with a bomb)" : "");
            if (!travel_with(things[pick].room, -1, -1, keys, bombs)) {
                if (++fails > 8) return false;
                continue;
            }
            if (!solve_puzzle() && ++fails > 8) return false;
            continue;
        }
        bot_log("%s: for the %s in %s%s%s", area, things[pick].kind, things[pick].room, keys ? " (spending a key)" : "",
                bombs ? " (with a bomb)" : "");
        if (!travel_with(things[pick].room, things[pick].c, things[pick].r, keys, bombs)) {
            bot_log("%s: couldn't get to %s", area, things[pick].room);
            if (++fails > 8) return false;
            if (strcmp(room_name(), things[pick].room) == 0) step_out_and_back();   /* (crates go back where they were) */
            continue;
        }
        if (!take_pickup_at(things[pick].at, things[pick].flag)) {
            bot_log("%s: the %s wasn't taken", area, things[pick].kind);
            if (++fails > 8) return false;
        }
        wait_play(1200);
    }
    return false;
}

/* ---- the guardians ---------------------------------------------------------------- */

br_entity boss_entity(void) {
    br_entity b = BR_ENTITY_NULL;
    br_world *w = bot_world();
    br_entity game = ent_named("Game");
    if (w && game) br_script_get_entity(w, game, "boss", &b);
    return b;
}

static br_vec2 vec_var(br_entity e, const char *var) {
    float x = 0, y = 0;
    const char *s = estr(e, var), *p = strchr(s, '(');
    if (p) sscanf(p + 1, "%f , %f", &x, &y);
    return br_v2(x, y);
}

/* Its entrance: bars, a roar, its name. Enter skips the end of it. */
static void boss_intro(void) {
    int n = 0;
    while (n++ < 600) {
        const char *st = game_state();
        if (strcmp(st, "play") == 0 && ent_alive(boss_entity())) return;
        if (strcmp(st, "intro") == 0 && gnum("intro_t") > 1.05 && gnum("intro_t") < 2.9) { key_tap(BR_KEY_ENTER); continue; }
        stop_walking();
        bot_frame();
    }
}

static bool boss_gone(void) {
    if (handle_death()) return false;
    return !ent_alive(boss_entity()) && strcmp(game_state(), "intro") != 0;
}

/* Steps a few frames towards a point (no striking on the way). */
extern bool g_in_hook;
static void step_to(br_vec2 p, int frames) {
    bool was = g_in_hook;
    uint64_t before = bot_frames();
    bool ok;
    g_in_hook = true;
    ok = goto_point(p, 2, frames);
    if (getenv("BOT_DEBUG")) bot_log("step_to %.0f,%.0f: %s after %d frames", p.x, p.y, ok ? "there" : "not there", (int)(bot_frames() - before));
    g_in_hook = was;
    if (bot_frames() == before) { stop_walking(); bot_frame(); }    /* (no way there now: let time pass) */
}

/* Strikes the boss if it's in reach (the general rule, with its size). */
bool strike_if_able(void);

/* ---- 2: Big Dill. It charges along a line at Pip and only feels a hit while
 * it sits dizzy after slamming into a wall. Keep out of its line; hit it dizzy. */
static bool boss_big_dill(void) {
    uint64_t start = bot_frames();
    while (bot_frames() - start < 30000) {
        br_entity b = boss_entity();
        const char *mode;
        br_vec2 bp, h;
        if (boss_gone()) return true;
        if (!wait_play(1200)) return false;
        b = boss_entity();
        if (!ent_alive(b)) continue;
        mode = estr(b, "mode");
        bp = ent_pos(b);
        h = hero_pos();
        if ((bot_frames() - start) % 300 == 0) bot_log("big dill: %s hp %g, Pip %g/%g", mode, enum_(b, "hp", 0), gnum("hp") / 2, gnum("hearts_max"));
        if (strcmp(mode, "dizzy") == 0) {
            if (!strike_if_able()) {
                /* beside it, on the side away from the wall it hit */
                br_vec2 d = vec_var(b, "dir");
                br_vec2 spot = br_v2(bp.x - d.x * 24, bp.y - d.y * 24);
                if (!hero_fits(spot)) spot = br_v2(bp.x + d.y * 24, bp.y + d.x * 24);
                if (!hero_fits(spot)) spot = br_v2(bp.x - d.y * 24, bp.y - d.x * 24);
                step_to(spot, 4);
            }
            continue;
        }
        if (strcmp(mode, "rumble") == 0 || strcmp(mode, "charge") == 0) {
            br_vec2 d = vec_var(b, "dir");
            float perp = d.x != 0 ? h.y - bp.y : h.x - bp.x;
            float ahead = d.x != 0 ? (h.x - bp.x) * d.x : (h.y - bp.y) * d.y;
            if (fabsf(perp) < 34 && ahead > -20) {
                /* out of the way: across its line, the side with room */
                float s1 = perp >= 0 ? 1.0f : -1.0f;
                br_vec2 a = d.x != 0 ? br_v2(h.x, h.y + s1 * 40) : br_v2(h.x + s1 * 40, h.y);
                br_vec2 c = d.x != 0 ? br_v2(h.x, h.y - s1 * 40) : br_v2(h.x - s1 * 40, h.y);
                step_to(hero_fits(a) ? a : c, 4);
            } else {
                stop_walking();
                bot_frame();
            }
            continue;
        }
        /* watching: wait near the middle, not too close */
        {
            br_vec2 mid = br_v2(128, 150);
            float dx = h.x - bp.x, dy = h.y - bp.y;
            if (dx * dx + dy * dy < 50 * 50) {
                br_vec2 away = br_v2(h.x + (dx >= 0 ? 30.0f : -30.0f), h.y + (dy >= 0 ? 30.0f : -30.0f));
                if (away.x < 40) away.x = 40;
                if (away.x > 216) away.x = 216;
                if (away.y < 100) away.y = 100;
                if (away.y > 210) away.y = 210;
                step_to(away, 4);
            } else if (fabsf(h.x - mid.x) + fabsf(h.y - mid.y) > 30) {
                step_to(mid, 4);
            } else {
                stop_walking();
                bot_frame();
            }
        }
    }
    return false;
}

/* ---- 3: the Spice Wraith. It flits from spot to spot and bursts into rings
 * of spice (which the pot lid can't stop yet), and can only be hurt while
 * solid. Skewers, from well off, lined up with it; and out of the spice's way. */
static bool boss_wraith(void) {
    uint64_t start = bot_frames();
    while (bot_frames() - start < 40000) {
        br_entity b, sk[2];
        const char *mode;
        br_vec2 bp, h;
        bool open;
        if (boss_gone()) return true;
        if (!wait_play(1200)) return false;
        b = boss_entity();
        if (!ent_alive(b)) { bot_frame(); continue; }
        if (dodge_shots()) continue;
        mode = estr(b, "mode");
        open = strcmp(mode, "solid") == 0 || strcmp(mode, "gather") == 0;
        bp = ent_pos(b);
        h = hero_pos();
        if (!has_item("skewers") || gnum("beans") <= 0) {           /* (no skewers: the spoon, as anything else) */
            if (!fight_step(BR_ENTITY_NULL)) evade(6);
            continue;
        }
        {
            /* a spot in line with it, 60 to 120 pixels off */
            br_vec2 best = h;
            float bd = 1e9f;
            int k, dist;
            static const br_vec2 D[4] = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };
            for (k = 0; k < 4; ++k)
                for (dist = 60; dist <= 120; dist += 6) {
                    br_vec2 p = br_v2(bp.x - D[k].x * (float)dist, bp.y - D[k].y * (float)dist);
                    float d;
                    if (p.x < 24 || p.x > 232 || p.y < 84 || p.y > 228 || !hero_fits(p)) continue;
                    d = fabsf(p.x - h.x) + fabsf(p.y - h.y);
                    if (d < bd) { bd = d; best = p; }
                }
            if (bd < 3 || (open && (fabsf(h.x - bp.x) < 5 || fabsf(h.y - bp.y) < 5) && fabsf(h.x - bp.x) + fabsf(h.y - bp.y) > 40)) {
                if (open && ents_named("Skewer", sk, 2) == 0) {
                    float dx = bp.x - h.x, dy = bp.y - h.y;
                    select_item("skewers");
                    face(fabsf(dx) > fabsf(dy) ? (dx > 0 ? DIR_RIGHT : DIR_LEFT) : (dy > 0 ? DIR_DOWN : DIR_UP));
                    use_item();
                } else {
                    stop_walking();
                    bot_frame();
                }
            } else {
                step_to(best, 4);
            }
        }
    }
    return false;
}

/* ---- 9: Baron Gorge, in three courses ------------------------------------------------ */

static void hold_keys(int x, int y) {
    key_hold(BR_KEY_LEFT, x < 0);
    key_hold(BR_KEY_RIGHT, x > 0);
    key_hold(BR_KEY_UP, y < 0);
    key_hold(BR_KEY_DOWN, y > 0);
}

/* A waiter or a meatball close by: deal with it first. */
static bool other_foe_near(br_entity baron, float within) {
    br_vec2 at[64];
    float size[64];
    int n = foe_positions(at, size, 64), i;
    br_vec2 h = hero_pos(), bp = ent_pos(baron);
    for (i = 0; i < n; ++i) {
        if (fabsf(at[i].x - bp.x) < 1 && fabsf(at[i].y - bp.y) < 1) continue;
        if (fabsf(at[i].x - h.x) + fabsf(at[i].y - h.y) < within) return true;
    }
    return false;
}

static bool hero_attacking(void) { return enum_(ent_named("Hero"), "attack_t", 0) > 0; }
static bool hero_ready(void) {
    br_entity h = ent_named("Hero");
    return enum_(h, "attack_t", 0) <= 0 && enum_(h, "cooldown", 0) <= 0 && enum_(h, "knock_t", 0) <= 0;
}

static bool boss_baron(void) {
    uint64_t start = bot_frames();
    int last_phase = 0;
    while (bot_frames() - start < 60000) {
        br_entity b;
        const char *mode;
        int phase;
        br_vec2 bp, h;
        double mt;
        if (boss_gone()) return true;
        if (!wait_play(1200)) return false;
        b = boss_entity();
        if (!ent_alive(b)) { bot_frame(); continue; }
        mode = estr(b, "mode");
        phase = (int)enum_(b, "phase", 1);
        mt = enum_(b, "mode_t", 0);
        bp = ent_pos(b);
        h = hero_pos();
        if ((bot_frames() - start) % 60 == 0) {
            bot_log("baron %d %s t=%.2f hp %g at %.0f,%.0f; Pip %.1f at %.0f,%.0f facing %s", phase, mode, mt, enum_(b, "hp", 0), bp.x, bp.y,
                    gnum("hp") / 2, h.x, h.y, hero_facing());
            if ((bot_frames() - start) % 240 == 0) bot_shot("baron");
        }
        if (phase != last_phase) { bot_log("baron: course %d (hp %g), Pip %g/%g", phase, enum_(b, "hp", 0), gnum("hp") / 2, gnum("hearts_max")); last_phase = phase; }
        if (strcmp(mode, "rage") == 0) { stop_walking(); evade(6); continue; }
        if (other_foe_near(b, phase == 3 ? 44 : 70) && strcmp(mode, "stunned") != 0 && strcmp(mode, "lean") != 0) {
            if (!fight_step(b)) evade(6);                /* a waiter or a meatball at Pip's heels */
            continue;
        }
        if (phase == 1) {
            /* under the table's edge, facing up (the pot lid takes the dishes), lined up with him */
            float ty = 143;
            if (h.y > ty + 3 || strcmp(hero_facing(), "up") != 0) {
                step_to(br_v2(bp.x, ty), 6);
                if (strcmp(hero_facing(), "up") != 0) face(DIR_UP);
                continue;
            }
            if (strcmp(mode, "lean") == 0 && mt < 1.12 && mt > 0.08 && fabsf(h.x - bp.x) < 12) {
                stop_walking();
                if (hero_ready()) poke(); else bot_frame();
                continue;
            }
            if (hero_attacking()) { stop_walking(); bot_frame(); continue; }
            hold_keys(h.x < bp.x - 2 ? 1 : h.x > bp.x + 2 ? -1 : 0, -1);     /* (up held: he keeps facing up) */
            bot_frame();
            continue;
        }
        if (phase == 2) {
            float dx = h.x - bp.x, dy = h.y - bp.y, dist = sqrtf(dx * dx + dy * dy);
            br_entity bombs[4];
            if (strcmp(mode, "stunned") == 0) {
                /* in reach but not touching: 29 pixels off, below or beside */
                if (!strike_if_able()) {
                    br_vec2 spot = br_v2(bp.x, bp.y + 29);
                    if (!hero_fits(spot)) spot = br_v2(bp.x + (dx >= 0 ? 29.0f : -29.0f), bp.y);
                    step_to(spot, 4);
                }
                continue;
            }
            if (strcmp(mode, "inhale") == 0 && mt > 1.2 && dist < 90 && ents_named("Bomb", bombs, 4) == 0 && gnum("bombs") > 0) {
                select_item("bombs");
                face(fabsf(dx) > fabsf(dy) ? (dx > 0 ? DIR_LEFT : DIR_RIGHT) : (dy > 0 ? DIR_UP : DIR_DOWN));
                use_item();
                continue;
            }
            if (gnum("bombs") <= 0) collect_drops(400);  /* (he drops bombs in while Pip has none) */
            /* keep 60 to 90 pixels off */
            if (dist < 60 || dist > 95) {
                br_vec2 want;
                float k = dist > 1 ? 75 / dist : 1;
                want = br_v2(bp.x + dx * k, bp.y + dy * k);
                if (want.x < 24) want.x = 24;
                if (want.x > 232) want.x = 232;
                if (want.y < 88) want.y = 88;
                if (want.y > 224) want.y = 224;
                if (!hero_fits(want) || (fabsf(want.x - h.x) < 4 && fabsf(want.y - h.y) < 4 && dist < 60)) evade(6);
                else step_to(want, 6);
            } else {
                stop_walking();
                bot_frame();
            }
            continue;
        }
        /* 3: under one of three covers; he shows himself as they lift */
        {
            br_vec2 spot = br_v2(bp.x, bp.y + 26);
            if (strcmp(mode, "lift") == 0 && mt < 1.6 && fabsf(h.x - bp.x) < 10 && fabsf(h.y - spot.y) < 6) {
                if (strcmp(hero_facing(), "up") != 0) face(DIR_UP);
                if (hero_ready()) poke(); else { stop_walking(); bot_frame(); }
                continue;
            }
            if (fabsf(h.x - spot.x) > 2 || fabsf(h.y - spot.y) > 2) step_to(spot, 4);
            else { if (strcmp(hero_facing(), "up") != 0) face(DIR_UP); stop_walking(); bot_frame(); }
        }
    }
    return false;
}

bool fight_boss(int cellar) {
    bool ok;
    boss_intro();
    switch (cellar) {
    case 2: ok = boss_big_dill(); break;
    case 3: ok = boss_wraith(); break;
    case 9: ok = boss_baron(); break;
    default: ok = fight_room(36000); break;
    }
    if (ok) ok = fight_room(7200);                       /* anything left */
    return ok;
}
