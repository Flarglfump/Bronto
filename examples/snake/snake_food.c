/*
 * SNAKE - apples and the timed bonus.
 *
 * There is always one apple on the board. Every SNAKE_BONUS_EVERY apples a
 * bonus appears too; it counts down for SNAKE_BONUS_TICKS ticks, and the
 * sooner the snake reaches it, the more it is worth.
 */
#include "snake_internal.h"

/* Free = no snake, no apple and no bonus there. */
static bool cell_free(const snake_game *g, snake_cell c) {
    if (g->body.occupied[c.y][c.x] || snake_cell_eq(c, g->food)) return false;
    return !(g->bonus_ticks > 0 && snake_cell_eq(c, g->bonus));
}

bool snake_free_cell(snake_game *g, snake_cell *out) {
    int free_count = 0, pick;
    snake_cell c;
    for (c.y = 0; c.y < SNAKE_ROWS; ++c.y)
        for (c.x = 0; c.x < SNAKE_COLS; ++c.x)
            if (cell_free(g, c)) free_count++;
    if (free_count == 0) return false;
    pick = br_rng_int(&g->rng, 0, free_count - 1);        /* the pick-th free cell */
    for (c.y = 0; c.y < SNAKE_ROWS; ++c.y)
        for (c.x = 0; c.x < SNAKE_COLS; ++c.x)
            if (cell_free(g, c) && pick-- == 0) { *out = c; return true; }
    return false;
}

void snake_food_after_apple(snake_game *g) {
    if (!snake_free_cell(g, &g->food)) {                   /* the board is full of snake */
        g->food.x = g->food.y = -1;
    }
    if (g->apples % SNAKE_BONUS_EVERY == 0 && g->bonus_ticks == 0 && snake_free_cell(g, &g->bonus)) {
        g->bonus_ticks = SNAKE_BONUS_TICKS;
    }
}

void snake_bonus_tick(snake_game *g) {
    if (g->bonus_ticks > 0) g->bonus_ticks--;
}

int snake_bonus_value(const snake_game *g) { return SNAKE_BONUS_POINTS + g->bonus_ticks; }
