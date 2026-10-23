/*
 * pantrybot - plays Pantry Quest from the title to the ending, and records
 * the run (replay.h) in segments: the land, then a cellar, then the land...
 *
 * The bot only presses keys. It reads the game to decide which (where Pip
 * is, the room's tiles, the enemies, the Game script's variables), but
 * nothing it does reaches the game except through the keyboard, so the
 * recording replays exactly.
 *
 * The bot is written as plain sequential code (route.c): it runs on a
 * thread of its own, and every bot_frame() hands one frame to the engine
 * and waits for it. The two never run at the same time.
 */
#ifndef PANTRYBOT_H
#define PANTRYBOT_H

#include "bronto/bronto.h"
#include <stdbool.h>

/* ---- the frame loop (main.c) ---- */
void bot_frame(void);                  /* lets one frame happen */
void bot_wait(int frames);
br_world *bot_world(void);             /* the scene being played */
br_engine *bot_engine(void);
uint64_t bot_frames(void);
void bot_log(const char *fmt, ...);
void bot_fail(const char *fmt, ...);   /* stops the run (the recording so far is kept) */

/* ---- keys ---- */
void key_hold(br_key k, bool down);
void keys_release_all(void);
void key_tap(br_key k);                /* down for a frame, then up for a frame */
void key_hold_for(br_key k, int frames);

/* ---- reading the game (game.c) ---- */
br_entity ent_named(const char *name);
int ents_named(const char *name, br_entity *out, int cap);
br_vec2 ent_pos(br_entity e);          /* world position */
bool ent_alive(br_entity e);
double gnum(const char *var);          /* a Game script variable */
const char *gstr(const char *var);     /* (a static buffer, until the next call) */
bool gstr_is(const char *var, const char *value);
double enum_(br_entity e, const char *var, double otherwise);
const char *estr(br_entity e, const char *var);
bool has_flag(const char *flag);
bool has_item(const char *item);       /* in Game.items or Game.treasures */
const char *room_name(void);
const char *game_state(void);          /* Game.state ("" outside the game) */
br_vec2 hero_pos(void);
bool in_game(void);                    /* main.scene is up */
br_entity room_map(void);
char tile_at(br_vec2 p);               /* the current room's tile under a world point (0 outside) */
char tile_cell(int c, int r);
bool tile_solid_cell(int c, int r);
br_vec2 cell_center(int c, int r);
void cell_of(br_vec2 p, int *c, int *r);

/* ---- doing things (act.c) ---- */
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } bot_dir;
void face(bot_dir d);
void poke(void);
void use_item(void);
bool select_item(const char *item);    /* puts it on B (E/Q) */
bool wait_play(int max_frames);        /* until Game.state is "play" */
bool goto_cell(int c, int r, int max_frames);        /* in this room */
bool goto_point(br_vec2 p, float tol, int max_frames);
bool leave_room(char side);            /* walks off the room's edge ('n','s','e','w') */
bool goto_room(const char *room);      /* across the land / a cellar, room by room */
bool travel(const char *room, int c, int r);         /* to a cell of a room */
bool fight_room(int max_frames);       /* until the room's enemies are gone */
bool enter_warp_at(int c, int r);      /* walks up into a cave mouth / stairs / door */

/* ---- the world (world.c) ---- */
void world_load(const char *game_dir);
int world_find(const char *room);
bool travel_keys(const char *room, int c, int r);     /* may spend keys on locked doors */
int world_pickups(const char *room, br_vec2 *at, const char **kinds, int cap);
typedef struct cellar_thing {
    char room[32], kind[24], flag[32];
    br_vec2 at;
    int c, r, price, min_hearts;
    bool on_clear, on_solve;
} cellar_thing;
int world_cellar_pickups(const char *area, cellar_thing *out, int cap);
int world_nearest(const char (*rooms)[32], const int *cs, const int *rs, int n, bool keys, bool bombs);
bool world_is_boss_room(const char *room);
bool world_pickup(const char *room, const char *kind, cellar_thing *out);
bool get_pickup(const char *room, const char *kind);
bool take_pickup_at(br_vec2 p, const char *flag);
bool reveal_warp(const char *room, const char *to, const char *item);
void collect_drops(float within);
bool travel_with(const char *room, int c, int r, bool keys, bool bombs);
extern bool g_fight_in_cellars;
bool explore_cellar(int n);
bool fight_boss(int cellar);
bool bomb_side(char side);
bool world_warp_cell(const char *room, const char *to, int *c, int *r);
void stop_walking(void);
bool hero_fits(br_vec2 p);
const char *hero_facing(void);
bool hero_busy(void);
extern bool (*g_walk_hook)(void);

/* ---- crates (act.c): a Sokoban search ---- */
typedef bool (*sk_goal_fn)(const void *state, int hero_cell, void *u);
int sokoban(sk_goal_fn done, void *u);       /* -1 none, 0 done now (g_sk_goal_cell), 1 pushed one */
bool sk_state_crate(const void *state, int c, int r);
extern int g_sk_goal_cell;

/* ---- puzzles (puzzle.c) ---- */
int world_puzzles(const char *area, cellar_thing *out, int cap);
bool solve_puzzle(void);

/* ---- fighting (fight.c) ---- */
int scan_foes(void);
int foe_count(void);
int foe_positions(br_vec2 *at, float *size, int cap);
int spawners_left(void);
bool strike_if_able(void);
void evade(int frames);
bool dodge_shots(void);
bool fight_step(br_entity ignore);
bool handle_death(void);
extern int g_deaths;
void bot_shot(const char *name);

/* ---- the route (route.c) ---- */
void bot_route(void);

#endif
