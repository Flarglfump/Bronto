/*
 * SNAKE - presentation layer (everything the player sees and hears).
 *
 *   view_board.c   the board, the snake, apples, the HUD
 *   view_fx.c      particles, score pop-ups, screen shake (ECS entities)
 *   sfx.c          synthesised sound effects and music
 *   main.c         engine setup, scenes, input -> snake_turn()
 *
 * None of these change the rules' state except main.c, and only through
 * the commands in snake.h.
 */
#ifndef SNAKE_VIEW_H
#define SNAKE_VIEW_H

#include "bronto/bronto.h"
#include "snake.h"

#define FB_W 320
#define FB_H 180
#define CELL 10                 /* pixels per board cell */
#define HUD_H 20
#define BOARD_Y HUD_H           /* the board sits under the HUD strip */

/* ---- view_board.c ------------------------------------------------------- */
br_vec2 view_cell_pos(snake_cell c);        /* top-left pixel of a cell */
br_vec2 view_cell_center(snake_cell c);
void view_draw_board(br_renderer *r, const snake_game *g, float time);
void view_draw_hud(br_renderer *r, const snake_game *g, int best);
void view_draw_text_centered(br_renderer *r, const char *text, float y, float scale, br_color c);

/* ---- view_fx.c ------------------------------------------------------------ */
typedef struct view_fx {
    br_world *world;
    br_component particle, popup;
    br_rng rng;
    float shake;                /* seconds of screen shake left */
} view_fx;

/* Registers the components and systems: effects update with the scene and
 * draw at render order 10, inside the board's camera. */
void view_fx_init(view_fx *fx, br_world *world, uint64_t seed);
void view_fx_event(view_fx *fx, const snake_event *e, const snake_game *g);
br_camera2d view_fx_camera(view_fx *fx);    /* the board camera, shaken */

/* ---- sfx.c ------------------------------------------------------------------ */
void sfx_init(br_engine *e, uint32_t event_type);   /* plays sounds for rule events */
void sfx_shutdown(br_engine *e);
void sfx_start(br_engine *e);                        /* a new game begins */
void sfx_toggle_music(br_engine *e);

#endif /* SNAKE_VIEW_H */
