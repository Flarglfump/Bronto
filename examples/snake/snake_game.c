/*
 * SNAKE - the game: state, one tick of play, and steering.
 *
 * This is the file to read first: snake_step() is the whole game, and
 * snake_turn() is everything the player can do.
 */
#include "snake_internal.h"
#include <string.h>

void snake_reset(snake_game *g, uint64_t seed) {
    snake_cell start;
    memset(g, 0, sizeof(*g));
    br_rng_seed(&g->rng, seed);
    start.x = 8;                        /* a few cells in from the left wall */
    start.y = SNAKE_ROWS / 2;
    snake_body_reset(&g->body, start, DIR_RIGHT, SNAKE_START_LENGTH);
    g->dir = DIR_RIGHT;
    g->food.x = g->food.y = -1;
    snake_free_cell(g, &g->food);
}

snake_event *snake_emit(snake_game *g, snake_event_type type, snake_cell cell) {
    static snake_event overflow;        /* absorbs writes once the list is full */
    snake_event *e = g->event_count < SNAKE_MAX_EVENTS ? &g->events[g->event_count++] : &overflow;
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->cell = cell;
    e->score = g->score;
    return e;
}

void snake_clear_events(snake_game *g) { g->event_count = 0; }

snake_cell snake_head(const snake_game *g) { return snake_body_segment(&g->body, 0); }
snake_cell snake_segment(const snake_game *g, int i) { return snake_body_segment(&g->body, i); }

float snake_speed(const snake_game *g) {
    float s = SNAKE_SPEED_START + (float)g->apples * SNAKE_SPEED_PER_APPLE;
    return s > SNAKE_SPEED_MAX ? SNAKE_SPEED_MAX : s;
}

/* ---- steering ------------------------------------------------------------ */

bool snake_turn(snake_game *g, snake_dir d) {
    snake_dir last = g->queued ? g->queue[g->queued - 1] : g->dir;   /* compare with the newest turn */
    if (g->dead || d == last || d == snake_opposite(last) || g->queued >= SNAKE_TURN_QUEUE) return false;
    g->queue[g->queued++] = d;
    return true;
}

static void apply_queued_turn(snake_game *g) {
    if (g->queued == 0) return;
    g->dir = g->queue[0];
    memmove(g->queue, g->queue + 1, (size_t)(g->queued - 1) * sizeof(snake_dir));
    g->queued--;
}

/* ---- one tick ------------------------------------------------------------ */

snake_result snake_step(snake_game *g) {
    snake_cell next;
    bool ate, ate_bonus;
    snake_result result = SNAKE_MOVED;
    if (g->dead) return SNAKE_DIED;
    if (g->won) return SNAKE_WON;

    apply_queued_turn(g);                                          /* 1. turn           */
    next = snake_move(snake_head(g), g->dir);                      /* 2. look ahead     */
    ate = snake_cell_eq(next, g->food);
    ate_bonus = g->bonus_ticks > 0 && snake_cell_eq(next, g->bonus);

    /* 3. crash? The tail only moves away if the snake isn't about to grow. */
    if (snake_body_blocked(&g->body, next, g->body.grow == 0 && !ate && !ate_bonus)) {
        g->dead = true;
        snake_emit(g, SNAKE_EV_DIED, next);
        return SNAKE_DIED;
    }

    if (ate) {                                                     /* 4. eat            */
        g->body.grow += SNAKE_APPLE_GROWTH;
        g->score += SNAKE_APPLE_POINTS;
        g->apples++;
        snake_emit(g, SNAKE_EV_ATE, next)->points = SNAKE_APPLE_POINTS;
        result = SNAKE_ATE;
    }
    if (ate_bonus) {
        int points = snake_bonus_value(g);                         /* quicker = more    */
        g->body.grow += SNAKE_BONUS_GROWTH;
        g->score += points;
        g->bonus_ticks = 0;
        snake_emit(g, SNAKE_EV_ATE_BONUS, next)->points = points;
        result = SNAKE_ATE_BONUS;
    }

    snake_body_advance(&g->body, next);                            /* 5. move / grow    */
    g->ticks++;
    snake_bonus_tick(g);                                           /* 6. bonus timer    */

    if (g->body.length >= SNAKE_CELLS) {                           /* 7. won?           */
        g->won = true;
        snake_emit(g, SNAKE_EV_WON, next);
        return SNAKE_WON;
    }
    if (ate) snake_food_after_apple(g);                            /* 8. new food       */
    return result;
}
