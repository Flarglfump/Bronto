/* Tests: rules of the Snake sample game (examples/snake/snake_*.c). */
#include "test.h"
#include "snake.h"
#include <string.h>

static snake_game B;   /* a few KB: keep off the stack */

static snake_cell cell(int x, int y) { snake_cell c; c.x = x; c.y = y; return c; }

static void test_snake_reset_and_turns(void) {
    snake_reset(&B, 1);
    CHECK_EQ_INT(B.body.length, SNAKE_START_LENGTH);
    CHECK(snake_cell_eq(snake_head(&B), cell(8, SNAKE_ROWS / 2)));
    CHECK(snake_cell_eq(snake_segment(&B, 3), cell(5, SNAKE_ROWS / 2)));
    CHECK(snake_in_bounds(B.food) && !B.body.occupied[B.food.y][B.food.x]);
    CHECK(!snake_turn(&B, DIR_LEFT));     /* reversal */
    CHECK(!snake_turn(&B, DIR_RIGHT));    /* same direction */
    CHECK(snake_turn(&B, DIR_UP));
    CHECK(!snake_turn(&B, DIR_DOWN));     /* reversal of the *queued* turn */
    CHECK(snake_turn(&B, DIR_LEFT));
    CHECK(snake_turn(&B, DIR_DOWN));
    CHECK(!snake_turn(&B, DIR_RIGHT));    /* queue full (3) */
    CHECK_EQ_INT(B.queued, 3);
}

static void test_snake_move_eat_grow(void) {
    snake_result r;
    snake_reset(&B, 2);
    B.food = cell(12, 0);                 /* out of the way */
    r = snake_step(&B);
    CHECK(r == SNAKE_MOVED);
    CHECK(snake_cell_eq(snake_head(&B), cell(9, 8)));
    CHECK(!B.body.occupied[8][5] && B.body.occupied[8][9]);   /* tail left, head arrived */
    B.food = cell(10, 8);                 /* directly ahead */
    r = snake_step(&B);
    CHECK(r == SNAKE_ATE);
    CHECK_EQ_INT(B.score, 10);
    CHECK_EQ_INT(B.body.length, SNAKE_START_LENGTH + 1);
    CHECK(!snake_cell_eq(B.food, cell(10, 8)) && !B.body.occupied[B.food.y][B.food.x]);
    CHECK_NEAR(snake_speed(&B), 8.25, 1e-6);
}

static void test_snake_walls_and_tail(void) {
    int i;
    snake_result r = SNAKE_MOVED;
    /* Chasing your own tail is legal: the tail moves out of the way. */
    snake_reset(&B, 3);
    B.food = cell(30, 0);
    snake_turn(&B, DIR_DOWN); snake_step(&B);
    snake_turn(&B, DIR_LEFT); snake_step(&B);
    snake_turn(&B, DIR_UP);
    CHECK(snake_step(&B) == SNAKE_MOVED);
    /* ... but not with one more segment. */
    snake_reset(&B, 3);
    B.food = cell(9, 8);
    CHECK(snake_step(&B) == SNAKE_ATE);
    B.food = cell(30, 0);
    snake_turn(&B, DIR_DOWN); snake_step(&B);
    snake_turn(&B, DIR_LEFT); snake_step(&B);
    snake_turn(&B, DIR_UP);
    CHECK(snake_step(&B) == SNAKE_DIED);
    CHECK(B.dead && snake_step(&B) == SNAKE_DIED);
    /* walls */
    snake_reset(&B, 4);
    B.food = cell(30, 15);
    snake_turn(&B, DIR_UP);
    for (i = 0; i < 20 && r != SNAKE_DIED; ++i) r = snake_step(&B);
    CHECK(r == SNAKE_DIED);
    CHECK_EQ_INT(i, SNAKE_ROWS / 2 + 1);
}

static void test_snake_bonus(void) {
    int i;
    snake_reset(&B, 5);
    for (i = 0; i < SNAKE_BONUS_EVERY; ++i) {
        B.food = snake_move(snake_head(&B), B.dir);
        CHECK(snake_step(&B) == SNAKE_ATE);
    }
    CHECK_EQ_INT(B.apples, SNAKE_BONUS_EVERY);
    CHECK_EQ_INT(B.bonus_ticks, SNAKE_BONUS_TICKS);
    CHECK(snake_in_bounds(B.bonus) && !B.body.occupied[B.bonus.y][B.bonus.x]);
    CHECK(!snake_cell_eq(B.bonus, B.food));
    /* grab it: points scale with the time left */
    B.bonus = snake_move(snake_head(&B), B.dir);
    i = B.score;
    CHECK(snake_step(&B) == SNAKE_ATE_BONUS);
    CHECK_EQ_INT(B.score - i, 50 + SNAKE_BONUS_TICKS);
    CHECK_EQ_INT(B.body.grow, 2);              /* 3 segments, one already added */
    CHECK_EQ_INT(B.bonus_ticks, 0);
}

/* The rules report what happened as events for sound and effects. */
static void test_snake_events(void) {
    snake_reset(&B, 6);
    CHECK_EQ_INT(B.event_count, 0);
    B.food = cell(9, 8);
    snake_step(&B);
    CHECK_EQ_INT(B.event_count, 1);
    CHECK(B.events[0].type == SNAKE_EV_ATE && snake_cell_eq(B.events[0].cell, cell(9, 8)));
    CHECK_EQ_INT(B.events[0].points, SNAKE_APPLE_POINTS);
    CHECK_EQ_INT(B.events[0].score, SNAKE_APPLE_POINTS);
    snake_clear_events(&B);
    B.bonus = cell(10, 8);
    B.bonus_ticks = 20;
    snake_step(&B);
    CHECK(B.event_count == 1 && B.events[0].type == SNAKE_EV_ATE_BONUS);
    CHECK_EQ_INT(B.events[0].points, SNAKE_BONUS_POINTS + 20);
    snake_clear_events(&B);
    snake_turn(&B, DIR_UP);
    while (!B.dead) snake_step(&B);                   /* into the top wall */
    CHECK(B.event_count == 1 && B.events[0].type == SNAKE_EV_DIED);
    CHECK(B.events[0].cell.y == -1);
    CHECK_EQ_INT(B.events[0].score, B.score);
}

static void test_snake_autopilot_and_determinism(void) {
    static snake_game other;
    int ticks = 0;
    snake_reset(&B, 77);
    snake_reset(&other, 77);
    while (!B.dead && !B.won && ticks < 3000) {
        snake_dir d = snake_autopilot(&B);
        snake_turn(&B, d);
        snake_turn(&other, d);
        snake_step(&B);
        snake_step(&other);
        ticks++;
    }
    CHECK(memcmp(&B, &other, sizeof(B)) == 0);   /* same seed + inputs = same game */
    CHECK(!B.dead);
    CHECK(B.apples >= 60);                        /* the AI is decent */
    printf("      autopilot: %d apples, score %d, length %d in %d ticks (%s)\n",
           B.apples, B.score, B.body.length, ticks, B.won ? "won" : B.dead ? "died" : "time limit");
}

void suite_snake(void) {
    RUN(test_snake_reset_and_turns);
    RUN(test_snake_move_eat_grow);
    RUN(test_snake_walls_and_tail);
    RUN(test_snake_bonus);
    RUN(test_snake_events);
    RUN(test_snake_autopilot_and_determinism);
}
