/*
 * SNAKE - the snake's body: its cells on the grid.
 *
 * The body is a ring buffer of cells (the head moves forward through the
 * buffer, the tail follows behind it), plus an occupancy grid so "is
 * there snake here?" is a single lookup.
 */
#include "snake_internal.h"

static const int k_dx[4] = { 0, 1, 0, -1 };   /* up, right, down, left */
static const int k_dy[4] = { -1, 0, 1, 0 };

/* ---- cells and directions --------------------------------------------- */

snake_cell snake_move(snake_cell c, snake_dir d) {
    snake_cell r;
    r.x = c.x + k_dx[d];
    r.y = c.y + k_dy[d];
    return r;
}

snake_dir snake_opposite(snake_dir d) { return (snake_dir)((d + 2) % 4); }
bool snake_in_bounds(snake_cell c) { return c.x >= 0 && c.y >= 0 && c.x < SNAKE_COLS && c.y < SNAKE_ROWS; }
bool snake_cell_eq(snake_cell a, snake_cell b) { return a.x == b.x && a.y == b.y; }

/* ---- the body ------------------------------------------------------------ */

void snake_body_reset(snake_body *b, snake_cell head, snake_dir facing, int length) {
    snake_dir back = snake_opposite(facing);
    snake_cell c = head;
    int i;
    b->length = length;
    b->head = length - 1;
    b->grow = 0;
    for (i = 0; i < length; ++i) {        /* head first, then backwards */
        b->cells[b->head - i] = c;
        b->occupied[c.y][c.x] = 1;
        c = snake_move(c, back);
    }
}

snake_cell snake_body_segment(const snake_body *b, int i) {
    return b->cells[(b->head - i + SNAKE_CELLS) % SNAKE_CELLS];
}

snake_cell snake_body_tail(const snake_body *b) { return snake_body_segment(b, b->length - 1); }

bool snake_body_blocked(const snake_body *b, snake_cell c, bool tail_moves) {
    if (!snake_in_bounds(c)) return true;                     /* wall */
    if (!b->occupied[c.y][c.x]) return false;
    return !(tail_moves && snake_cell_eq(c, snake_body_tail(b)));
}

void snake_body_advance(snake_body *b, snake_cell next) {
    if (b->grow > 0) {
        b->grow--;                                            /* tail stays: one longer */
    } else {
        snake_cell tail = snake_body_tail(b);
        b->occupied[tail.y][tail.x] = 0;
        b->length--;
    }
    b->head = (b->head + 1) % SNAKE_CELLS;
    b->cells[b->head] = next;
    b->occupied[next.y][next.x] = 1;
    b->length++;
}
