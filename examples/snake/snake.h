/*
 * SNAKE - the game rules (public API).
 *
 * The rules are plain, deterministic C with no engine calls: no window,
 * no audio, no clock. Randomness comes from a seeded generator inside the
 * game, so the same seed and the same turns always replay the same game.
 * The presentation layer (view_*.c, sfx.c, main.c) reads this state to
 * draw it, calls snake_turn() when the player steers, and reacts to the
 * events the rules emit.
 *
 * One file per part of the game:
 *
 *   snake_body.c       the snake itself: its cells on the grid
 *   snake_food.c       apples and the timed bonus: where they appear
 *   snake_game.c       the game: state, one tick of play, steering
 *   snake_autopilot.c  a computer player (attract mode, demo, tests)
 *
 * One call to snake_step() advances the game one tick:
 *
 *   turn -> look ahead -> crash? -> eat -> move / grow -> bonus timer -> won? -> new food
 */
#ifndef SNAKE_H
#define SNAKE_H

#include "bronto/bmath.h"

/* ---- the board and the tuning ------------------------------------------- */
#define SNAKE_COLS 32
#define SNAKE_ROWS 16
#define SNAKE_CELLS (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_START_LENGTH 4
#define SNAKE_TURN_QUEUE 3          /* turns buffered ahead (fast double-taps) */

#define SNAKE_APPLE_POINTS 10
#define SNAKE_APPLE_GROWTH 1
#define SNAKE_BONUS_EVERY 5         /* a bonus appears after every N apples */
#define SNAKE_BONUS_TICKS 60        /* ... and stays this long */
#define SNAKE_BONUS_POINTS 50       /* ... worth this plus 1 per tick left */
#define SNAKE_BONUS_GROWTH 3

#define SNAKE_SPEED_START 8.0f      /* ticks per second */
#define SNAKE_SPEED_PER_APPLE 0.25f
#define SNAKE_SPEED_MAX 18.0f

/* ---- cells and directions ------------------------------------------------- */
typedef enum snake_dir { DIR_UP = 0, DIR_RIGHT, DIR_DOWN, DIR_LEFT } snake_dir;
typedef struct snake_cell { int x, y; } snake_cell;

snake_cell snake_move(snake_cell c, snake_dir d);   /* the neighbour in that direction */
snake_dir snake_opposite(snake_dir d);
bool snake_in_bounds(snake_cell c);
bool snake_cell_eq(snake_cell a, snake_cell b);

/* ---- the snake's body (snake_body.c) --------------------------------------- */
typedef struct snake_body {
    snake_cell cells[SNAKE_CELLS];  /* ring buffer: cells[head] is the head */
    int head, length;
    int grow;                       /* segments still to add */
    uint8_t occupied[SNAKE_ROWS][SNAKE_COLS];
} snake_body;

/* ---- what happened (for sound, effects and the HUD) ---------------------- */
typedef enum snake_event_type {
    SNAKE_EV_ATE = 0,           /* an apple                                  */
    SNAKE_EV_ATE_BONUS,         /* the bonus                                 */
    SNAKE_EV_DIED,              /* cell = where the snake crashed            */
    SNAKE_EV_WON                /* the snake fills the board                 */
} snake_event_type;

typedef struct snake_event {
    snake_event_type type;
    snake_cell cell;
    int points;                 /* points this event scored */
    int score;                  /* total score afterwards   */
} snake_event;

#define SNAKE_MAX_EVENTS 8

/* ---- the game -------------------------------------------------------------- */
typedef enum snake_result {
    SNAKE_MOVED = 0,
    SNAKE_ATE,
    SNAKE_ATE_BONUS,
    SNAKE_DIED,
    SNAKE_WON
} snake_result;

typedef struct snake_game {
    snake_body body;
    snake_dir dir;
    snake_dir queue[SNAKE_TURN_QUEUE];  /* turns waiting to be applied, one per tick */
    int queued;
    snake_cell food;                    /* (-1, -1) when there is no room left */
    snake_cell bonus;
    int bonus_ticks;                    /* > 0 while the bonus is on the board */
    int score, apples, ticks;
    bool dead, won;
    br_rng rng;
    snake_event events[SNAKE_MAX_EVENTS];   /* collected until snake_clear_events */
    int event_count;
} snake_game;

void snake_reset(snake_game *g, uint64_t seed);
/* Queue a turn. Reversing, repeating the current direction and a full
 * queue are ignored. Returns true if the turn was queued. */
bool snake_turn(snake_game *g, snake_dir d);
snake_result snake_step(snake_game *g);
void snake_clear_events(snake_game *g);

snake_cell snake_head(const snake_game *g);
snake_cell snake_segment(const snake_game *g, int i);   /* 0 = head */
float snake_speed(const snake_game *g);                  /* ticks per second */

/* ---- the computer player (snake_autopilot.c) ------------------------------- */
/* Shortest safe path to the food; otherwise follow its own tail. */
snake_dir snake_autopilot(const snake_game *g);

#endif /* SNAKE_H */
