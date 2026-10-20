/*
 * SNAKE - drawing the board, the snake, the apples and the HUD.
 */
#include "view.h"
#include <math.h>
#include <stdio.h>

br_vec2 view_cell_pos(snake_cell c) { return br_v2((float)(c.x * CELL), (float)(BOARD_Y + c.y * CELL)); }

br_vec2 view_cell_center(snake_cell c) {
    br_vec2 p = view_cell_pos(c);
    return br_v2(p.x + CELL / 2, p.y + CELL / 2);
}

void view_draw_text_centered(br_renderer *r, const char *text, float y, float scale, br_color c) {
    br_vec2 size = br_measure_text(text, scale);
    br_draw_text(r, text, ((float)FB_W - size.x) / 2.0f + scale, y + scale, scale, br_rgba(0, 0, 0, 160));
    br_draw_text(r, text, ((float)FB_W - size.x) / 2.0f, y, scale, c);
}

static void draw_grass(br_renderer *r) {
    int x, y;
    for (y = 0; y < SNAKE_ROWS; ++y)
        for (x = 0; x < SNAKE_COLS; ++x)
            br_fill_rect(r, br_rect_make((float)(x * CELL), (float)(BOARD_Y + y * CELL), CELL, CELL),
                         ((x + y) & 1) ? br_rgb(36, 52, 40) : br_rgb(31, 45, 35));
}

/* Apple: pulsing, with a highlight and a leaf. */
static void draw_apple(br_renderer *r, snake_cell at, float t) {
    br_vec2 p = view_cell_pos(at);
    float rad = 3.6f + 0.5f * sinf(t * 6.0f);
    br_fill_circle(r, br_v2(p.x + 5, p.y + 5.5f), rad, br_rgb(226, 64, 64));
    br_fill_rect(r, br_rect_make(p.x + 3, p.y + 3, 1, 1), br_rgb(255, 190, 190));
    br_fill_triangle(r, br_v2(p.x + 5, p.y + 2), br_v2(p.x + 8, p.y), br_v2(p.x + 6, p.y + 3), br_rgb(90, 200, 90));
}

/* Bonus: a spinning gold diamond that blinks when about to vanish. */
static void draw_bonus(br_renderer *r, const snake_game *g, float t) {
    br_vec2 c;
    float w;
    br_color gold;
    if (g->bonus_ticks <= 0 || (g->bonus_ticks <= 15 && !(((int)(t * 12.0f)) & 1))) return;
    c = br_v2(view_cell_pos(g->bonus).x + 5, view_cell_pos(g->bonus).y + 5);
    w = 4.5f * br_absf(cosf(t * 4.0f)) + 0.8f;
    gold = br_color_lerp(br_rgb(255, 200, 40), br_rgb(255, 250, 180), 0.5f + 0.5f * sinf(t * 10.0f));
    br_fill_triangle(r, br_v2(c.x, c.y - 5), br_v2(c.x + w, c.y), br_v2(c.x - w, c.y), gold);
    br_fill_triangle(r, br_v2(c.x - w, c.y), br_v2(c.x + w, c.y), br_v2(c.x, c.y + 5), br_rgb(220, 150, 20));
}

/* Tail to head so the head is on top; each segment bridges to the next. */
static void draw_body(br_renderer *r, const snake_game *g) {
    int i, n = g->body.length;
    for (i = n - 1; i >= 0; --i) {
        snake_cell s = snake_segment(g, i);
        br_vec2 p = view_cell_pos(s);
        float k = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
        br_color col = br_color_lerp(br_rgb(140, 230, 90), br_rgb(40, 140, 110), k);
        if (g->dead) col = br_color_lerp(col, br_rgb(120, 120, 120), 0.6f);
        br_fill_rect(r, br_rect_make(p.x + 1, p.y + 1, CELL - 2, CELL - 2), col);
        if (i > 0) {   /* bridge towards the next segment (nearer the head) */
            snake_cell next = snake_segment(g, i - 1);
            float bx = p.x + 1 + (float)(next.x - s.x) * 2.0f, by = p.y + 1 + (float)(next.y - s.y) * 2.0f;
            br_fill_rect(r, br_rect_make(bx, by, CELL - 2, CELL - 2), col);
        }
    }
}

/* The head, with eyes looking where it's going. */
static void draw_head(br_renderer *r, const snake_game *g) {
    br_vec2 p = view_cell_pos(snake_head(g)), e1, e2;
    float ex = (float)(g->dir == DIR_RIGHT) - (float)(g->dir == DIR_LEFT);
    float ey = (float)(g->dir == DIR_DOWN) - (float)(g->dir == DIR_UP);
    br_fill_rect(r, br_rect_make(p.x, p.y, CELL, CELL), g->dead ? br_rgb(200, 90, 90) : br_rgb(160, 245, 100));
    if (ex != 0) { e1 = br_v2(p.x + 4 + ex * 2, p.y + 2); e2 = br_v2(p.x + 4 + ex * 2, p.y + 6); }
    else         { e1 = br_v2(p.x + 2, p.y + 4 + ey * 2); e2 = br_v2(p.x + 6, p.y + 4 + ey * 2); }
    br_fill_rect(r, br_rect_make(e1.x, e1.y, 2, 2), BR_WHITE);
    br_fill_rect(r, br_rect_make(e2.x, e2.y, 2, 2), BR_WHITE);
    if (!g->dead) {   /* pupils sit on the side the snake is facing */
        float px = ex > 0 ? 1.0f : 0.0f, py = ey > 0 ? 1.0f : 0.0f;
        br_fill_rect(r, br_rect_make(e1.x + px, e1.y + py, 1, 1), BR_BLACK);
        br_fill_rect(r, br_rect_make(e2.x + px, e2.y + py, 1, 1), BR_BLACK);
    }
}

void view_draw_board(br_renderer *r, const snake_game *g, float t) {
    draw_grass(r);
    if (snake_in_bounds(g->food)) draw_apple(r, g->food, t);
    draw_bonus(r, g, t);
    draw_body(r, g);
    draw_head(r, g);
}

void view_draw_hud(br_renderer *r, const snake_game *g, int best) {
    char buf[64];
    br_fill_rect(r, br_rect_make(0, 0, FB_W, HUD_H), br_rgb(18, 24, 22));
    br_fill_rect(r, br_rect_make(0, HUD_H - 1, FB_W, 1), br_rgb(60, 90, 70));
    snprintf(buf, sizeof(buf), "SCORE %05d", g->score);
    br_draw_text(r, buf, 6, 6, 1, BR_WHITE);
    snprintf(buf, sizeof(buf), "LEN %d", g->body.length);
    br_draw_text(r, buf, 136, 6, 1, br_rgb(150, 220, 150));
    snprintf(buf, sizeof(buf), "BEST %05d", g->score > best ? g->score : best);
    br_draw_text(r, buf, (float)FB_W - br_measure_text(buf, 1).x - 6, 6, 1, BR_YELLOW);
}
