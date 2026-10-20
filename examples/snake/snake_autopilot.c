/*
 * SNAKE - the computer player.
 *
 * Each tick it asks two questions:
 *   1. Is there a shortest path to the food that still leaves the snake a
 *      way out (its tail reachable) once it has eaten? Then take it.
 *   2. Otherwise, chase its own tail the long way round, which keeps
 *      reshaping the board until the food becomes safe. If no move keeps
 *      the tail in reach, take the one with the most room.
 *
 * It only reads the game and plays moves on copies of it, so it can never
 * cheat. It plays the attract mode, the headless demo and the tests.
 */
#include "snake_internal.h"
#include <string.h>

/* Cells the head may enter next tick: free, or the tail if it moves away. */
static bool passable(const snake_body *b, snake_cell c, snake_cell tail, bool tail_moves) {
    if (!snake_in_bounds(c)) return false;
    if (!b->occupied[c.y][c.x]) return true;
    return tail_moves && snake_cell_eq(c, tail);
}

/* Breadth-first search from the head to `target`: the first direction of
 * a shortest path, or -1 if there is none. */
static int path_first_dir(const snake_game *g, snake_cell target) {
    static int16_t parent_dir[SNAKE_ROWS][SNAKE_COLS];
    static snake_cell queue[SNAKE_CELLS];
    int qh = 0, qt = 0, x, y;
    snake_cell head = snake_head(g), tail = snake_body_tail(&g->body);
    bool tail_moves = g->body.grow == 0;
    if (snake_cell_eq(head, target)) return -1;
    for (y = 0; y < SNAKE_ROWS; ++y) for (x = 0; x < SNAKE_COLS; ++x) parent_dir[y][x] = -1;
    queue[qt++] = head;
    parent_dir[head.y][head.x] = 4;   /* visited: the root */
    while (qh < qt) {
        snake_cell c = queue[qh++];
        int d;
        if (snake_cell_eq(c, target)) {
            while (1) {                /* walk back to the cell next to the head */
                int pd = parent_dir[c.y][c.x];
                snake_cell prev = snake_move(c, snake_opposite((snake_dir)pd));
                if (snake_cell_eq(prev, head)) return pd;
                c = prev;
            }
        }
        for (d = 0; d < 4; ++d) {
            snake_cell n = snake_move(c, (snake_dir)d);
            if (!passable(&g->body, n, tail, tail_moves) || parent_dir[n.y][n.x] != -1) continue;
            parent_dir[n.y][n.x] = (int16_t)d;
            queue[qt++] = n;
        }
    }
    return -1;
}

/* Free cells reachable from the head, and whether the tail is among them. */
static int reachable_area(const snake_game *g, bool *tail_reachable) {
    static uint8_t seen[SNAKE_ROWS][SNAKE_COLS];
    static snake_cell queue[SNAKE_CELLS];
    int qh = 0, qt = 0, area = 0;
    snake_cell head = snake_head(g), tail = snake_body_tail(&g->body);
    memset(seen, 0, sizeof(seen));
    *tail_reachable = false;
    queue[qt++] = head;
    seen[head.y][head.x] = 1;
    while (qh < qt) {
        snake_cell c = queue[qh++];
        int d;
        for (d = 0; d < 4; ++d) {
            snake_cell n = snake_move(c, (snake_dir)d);
            if (!snake_in_bounds(n) || seen[n.y][n.x]) continue;
            if (snake_cell_eq(n, tail)) *tail_reachable = true;
            if (g->body.occupied[n.y][n.x] && !snake_cell_eq(n, tail)) continue;
            seen[n.y][n.x] = 1;
            queue[qt++] = n;
            area++;
        }
    }
    return area;
}

/* Steps from the head to the tail (0 if unreachable). */
static int tail_distance(const snake_game *g) {
    static int16_t dist[SNAKE_ROWS][SNAKE_COLS];
    static snake_cell queue[SNAKE_CELLS];
    int qh = 0, qt = 0, x, y;
    snake_cell head = snake_head(g), tail = snake_body_tail(&g->body);
    for (y = 0; y < SNAKE_ROWS; ++y) for (x = 0; x < SNAKE_COLS; ++x) dist[y][x] = -1;
    dist[head.y][head.x] = 0;
    queue[qt++] = head;
    while (qh < qt) {
        snake_cell c = queue[qh++];
        int d;
        for (d = 0; d < 4; ++d) {
            snake_cell n = snake_move(c, (snake_dir)d);
            if (!snake_in_bounds(n) || dist[n.y][n.x] != -1) continue;
            if (snake_cell_eq(n, tail)) return dist[c.y][c.x] + 1;
            if (g->body.occupied[n.y][n.x]) continue;
            dist[n.y][n.x] = (int16_t)(dist[c.y][c.x] + 1);
            queue[qt++] = n;
        }
    }
    return 0;
}

/* Question 1: play the path out on a copy - is the tail still reachable
 * after eating? */
static bool path_is_safe(const snake_game *g, snake_cell target) {
    static snake_game sim;            /* games are a few KB: keep them off the stack */
    int guard = SNAKE_CELLS;
    bool tail_ok;
    sim = *g;
    sim.queued = 0;
    while (guard-- > 0) {
        int step_dir;
        snake_result r;
        if (snake_cell_eq(snake_head(&sim), target)) break;      /* bonus expired en route */
        step_dir = path_first_dir(&sim, target);
        if (step_dir < 0) return false;
        sim.dir = (snake_dir)step_dir;
        r = snake_step(&sim);
        if (r == SNAKE_DIED) return false;
        if (r != SNAKE_MOVED) break;                              /* ate (or won) */
    }
    reachable_area(&sim, &tail_ok);
    return tail_ok || sim.won;
}

/* Question 2: the move that keeps the tail furthest away but reachable. */
static snake_dir follow_tail(const snake_game *g) {
    static snake_game sim;
    int d, best = -1, best_score = -1;
    for (d = 0; d < 4; ++d) {
        bool tail_ok;
        int score, area;
        if (d == (int)snake_opposite(g->dir)) continue;
        sim = *g;
        sim.queued = 0;
        sim.dir = (snake_dir)d;
        if (snake_step(&sim) == SNAKE_DIED) continue;
        area = reachable_area(&sim, &tail_ok);
        score = tail_ok ? 100000 + 100 * tail_distance(&sim) + area : area;
        if (score > best_score) { best_score = score; best = d; }
    }
    return best >= 0 ? (snake_dir)best : g->dir;
}

snake_dir snake_autopilot(const snake_game *g) {
    snake_cell target = g->bonus_ticks > 0 ? g->bonus : g->food;
    int first = snake_in_bounds(target) ? path_first_dir(g, target) : -1;
    if (first >= 0 && path_is_safe(g, target)) return (snake_dir)first;
    return follow_tail(g);
}
