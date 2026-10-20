/*
 * SNAKE - how the rule modules talk to each other.
 *
 * Only snake_*.c files include this. The presentation layer uses snake.h.
 */
#ifndef SNAKE_INTERNAL_H
#define SNAKE_INTERNAL_H

#include "snake.h"

/* snake_body.c ------------------------------------------------------------ */
/* A straight snake with its head at `head`, facing `facing`. */
void snake_body_reset(snake_body *b, snake_cell head, snake_dir facing, int length);
snake_cell snake_body_segment(const snake_body *b, int i);     /* 0 = head */
snake_cell snake_body_tail(const snake_body *b);
/* Would moving the head into `c` crash? Walls and the body do; the tail
 * cell is safe if the tail moves out of the way this tick. */
bool snake_body_blocked(const snake_body *b, snake_cell c, bool tail_moves);
/* The head moves into `next`. The tail follows, unless the snake is growing. */
void snake_body_advance(snake_body *b, snake_cell next);

/* snake_food.c ------------------------------------------------------------ */
/* A uniformly random cell that holds no snake, apple or bonus. */
bool snake_free_cell(snake_game *g, snake_cell *out);
void snake_food_after_apple(snake_game *g);   /* a new apple; every Nth time, a bonus */
void snake_bonus_tick(snake_game *g);         /* count the bonus down */
int  snake_bonus_value(const snake_game *g);  /* points if eaten now */

/* snake_game.c ------------------------------------------------------------ */
snake_event *snake_emit(snake_game *g, snake_event_type type, snake_cell cell);  /* never NULL */

#endif /* SNAKE_INTERNAL_H */
