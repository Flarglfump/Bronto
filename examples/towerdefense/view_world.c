/*
 * POP DEFENSE - drawing the playing field.
 *
 * The grass, path, pond and trees never change, so they are painted once
 * into an image and copied to the screen each frame. Towers, balloons and
 * projectiles are drawn on top from the current game state.
 */
#include "view.h"
#include <math.h>
#include <string.h>

static br_image g_background;

/* ---- small drawing helpers -------------------------------------------------- */

static br_color shade(br_color c, float k) {   /* k < 1 darker, > 1 lighter */
    float r = (float)c.r * k, g = (float)c.g * k, b = (float)c.b * k;
    return br_rgba((uint8_t)br_clampf(r, 0, 255), (uint8_t)br_clampf(g, 0, 255), (uint8_t)br_clampf(b, 0, 255), c.a);
}

static void fill_quad(br_renderer *r, br_vec2 a, br_vec2 b, br_vec2 c, br_vec2 d, br_color col) {
    br_fill_triangle(r, a, b, c, col);
    br_fill_triangle(r, a, c, d, col);
}

/* A bar from `from` along unit `dir`: length and half-width. */
static void fill_bar(br_renderer *r, br_vec2 from, br_vec2 dir, float length, float half_width, br_color col) {
    br_vec2 n = br_v2_scale(br_v2_perp(dir), half_width);
    br_vec2 to = br_v2_add(from, br_v2_scale(dir, length));
    fill_quad(r, br_v2_add(from, n), br_v2_add(to, n), br_v2_sub(to, n), br_v2_sub(from, n), col);
}

/* ---- background --------------------------------------------------------------- */

/* Smooth value noise in 0..1. Plain arithmetic, no libm, so the painted
 * background is bit-identical on every CPU and C library. */
static float lattice(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

static float value_noise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    float top = br_lerpf(lattice(ix, iy), lattice(ix + 1, iy), sx);
    float bottom = br_lerpf(lattice(ix, iy + 1), lattice(ix + 1, iy + 1), sx);
    return br_lerpf(top, bottom, sy);
}

/* Two random numbers in a fixed order. (Two br_rng calls as arguments of
 * one function call could run in either order, so the picture would differ
 * between compilers.) */
static br_vec2 random_v2(br_rng *rng, float x0, float x1, float y0, float y1) {
    br_vec2 v;
    v.x = br_rng_range(rng, x0, x1);
    v.y = br_rng_range(rng, y0, y1);
    return v;
}

void view_world_init(const td_map *map) {
    br_renderer r;
    br_rng rng;
    int x, y, i;
    float d;
    br_image_init(&g_background, TD_MAP_W, TD_MAP_H);
    br_renderer_init(&r, &g_background);
    br_rng_seed(&rng, 2024);

    /* grass with soft, rolling variation */
    for (y = 0; y < TD_MAP_H; ++y)
        for (x = 0; x < TD_MAP_W; ++x) {
            float k = 0.87f + 0.10f * value_noise((float)x / 48.0f, (float)y / 40.0f)
                      + 0.05f * value_noise((float)x / 13.0f, (float)y / 11.0f) + br_rng_range(&rng, -0.025f, 0.025f);
            br_image_set(&g_background, x, y, shade(br_rgb(92, 166, 70), k));
        }
    for (i = 0; i < 1400; ++i) {             /* tufts */
        br_vec2 p = random_v2(&rng, 0, TD_MAP_W, 0, TD_MAP_H);
        br_color c = br_rng_float(&rng) < 0.5f ? br_rgb(70, 140, 55) : br_rgb(118, 190, 88);
        br_vec2 tip = random_v2(&rng, -1, 1, -3.5f, -1.5f);
        br_draw_line(&r, p, br_v2_add(p, tip), c);
    }
    for (i = 0; i < 90; ++i) {               /* flowers, kept off the path */
        static const uint32_t colors[] = { 0xFFFFFFFF, 0xFFE45AFF, 0xFF8FC8FF, 0xB9A7FFFF };
        br_vec2 p = random_v2(&rng, 4, TD_MAP_W - 4, 4, TD_MAP_H - 4);
        br_color c = br_hex(colors[br_rng_int(&rng, 0, 3)]);
        if (td_map_distance_to_path(map, p) < map->path_radius + 4) continue;
        br_fill_rect(&r, br_rect_make(p.x, p.y, 2, 2), c);
        br_draw_pixel(&r, p.x + 0.5f, p.y + 0.5f, br_rgb(250, 210, 60));
    }

    /* the path: darker rim, dirt, then pebbles */
    for (d = -40; d < map->length + 40; d += 2)
        br_fill_circle(&r, td_map_point(map, d), map->path_radius + 2.0f, br_rgb(150, 112, 70));
    for (d = -40; d < map->length + 40; d += 2)
        br_fill_circle(&r, td_map_point(map, d), map->path_radius, br_rgb(214, 180, 122));
    for (i = 0; i < 700; ++i) {
        float at = br_rng_range(&rng, 0, map->length);
        br_vec2 p = br_v2_add(td_map_point(map, at), random_v2(&rng, -9, 9, -9, 9));
        if (td_map_distance_to_path(map, p) > map->path_radius - 2) continue;
        br_draw_pixel(&r, p.x, p.y, br_rng_float(&rng) < 0.5f ? br_rgb(186, 150, 96) : br_rgb(232, 204, 150));
    }

    /* scenery */
    for (i = 0; i < map->obstacle_count; ++i) {
        const td_obstacle *o = &map->obstacles[i];
        br_vec2 p = o->pos;
        float rad = o->radius;
        if (o->kind != TD_OBSTACLE_POND) continue;
        br_fill_circle(&r, p, rad + 3, br_rgb(214, 196, 140));      /* sandy shore */
    }
    for (i = 0; i < map->obstacle_count; ++i) {
        const td_obstacle *o = &map->obstacles[i];
        if (o->kind == TD_OBSTACLE_POND) br_fill_circle(&r, o->pos, o->radius, br_rgb(64, 140, 206));
    }
    for (i = 0; i < map->obstacle_count; ++i) {
        const td_obstacle *o = &map->obstacles[i];
        br_vec2 p = o->pos;
        float rad = o->radius;
        switch (o->kind) {
        case TD_OBSTACLE_POND:
            br_fill_circle(&r, br_v2(p.x + 2, p.y + 2), rad * 0.6f, br_rgb(52, 120, 190));
            br_draw_line(&r, br_v2(p.x - rad * 0.5f, p.y - rad * 0.3f), br_v2(p.x - rad * 0.1f, p.y - rad * 0.3f), br_rgb(170, 215, 245));
            br_draw_line(&r, br_v2(p.x + rad * 0.1f, p.y + rad * 0.2f), br_v2(p.x + rad * 0.45f, p.y + rad * 0.2f), br_rgb(150, 200, 240));
            break;
        case TD_OBSTACLE_TREE:
            br_fill_circle(&r, br_v2(p.x + 3, p.y + 4), rad, br_rgba(0, 0, 0, 70));
            br_fill_circle(&r, p, rad, br_rgb(36, 104, 44));
            br_fill_circle(&r, br_v2(p.x - 1, p.y - 1), rad * 0.78f, br_rgb(54, 136, 56));
            br_fill_circle(&r, br_v2(p.x - rad * 0.3f, p.y - rad * 0.3f), rad * 0.42f, br_rgb(88, 170, 72));
            break;
        case TD_OBSTACLE_ROCK:
            br_fill_circle(&r, br_v2(p.x + 2, p.y + 2), rad, br_rgba(0, 0, 0, 60));
            br_fill_circle(&r, p, rad, br_rgb(126, 128, 124));
            br_fill_circle(&r, br_v2(p.x - rad * 0.3f, p.y - rad * 0.3f), rad * 0.5f, br_rgb(170, 172, 166));
            break;
        }
    }
}

void view_world_shutdown(void) { br_image_destroy(&g_background); }

/* ---- balloons ------------------------------------------------------------------ */

static const uint32_t k_balloon_colors[TD_BALLOON_TYPE_COUNT] = {
    0xE0302EFF, 0x2F86E8FF, 0x3CBF3CFF, 0xF8D73AFF, 0xF77FB5FF, 0x26262CFF,
    0xF2F2F2FF, 0x7C828CFF, 0xF2F2F2FF, 0xE0302EFF, 0xB0703CFF, 0x5E86B8FF,
};

/* Colour of the pixel at offset (dx, dy) inside a balloon of radius `rad`. */
static br_color balloon_pixel(td_balloon_type type, bool camo, float dx, float dy, float rad, br_color base) {
    br_color c = base;
    if (type == TD_ZEBRA) {
        c = (((int)floorf((dy + dx * 0.4f) / (rad * 0.36f))) & 1) ? br_rgb(30, 30, 34) : br_rgb(240, 240, 240);
    } else if (type == TD_RAINBOW) {
        static const uint32_t bands[6] = { 0xE0302EFF, 0xF5922AFF, 0xF8D73AFF, 0x3CBF3CFF, 0x2F86E8FF, 0x9A50D8FF };
        int band = (int)((dy / rad + 1.0f) * 3.0f);
        c = br_hex(bands[band < 0 ? 0 : band > 5 ? 5 : band]);
    }
    if (camo) {                                   /* blotchy green-brown camouflage */
        int cell = ((int)floorf(dx * 0.5f + 8) * 7 + (int)floorf(dy * 0.5f + 8) * 13) % 5;
        if (cell == 0) c = br_color_lerp(c, br_rgb(70, 92, 48), 0.7f);
        else if (cell == 1) c = br_color_lerp(c, br_rgb(120, 104, 64), 0.6f);
    }
    return c;
}

void view_draw_balloon(br_renderer *r, td_balloon_type type, bool camo, br_vec2 pos, float scale) {
    const td_balloon_def *def = td_balloon_info(type);
    br_color base = br_hex(k_balloon_colors[type]);
    float rad = def->radius * scale, rx = rad * 0.86f, ry = rad;
    float cy = pos.y - rad * 0.12f;
    int x, y, x0 = (int)floorf(pos.x - rx - 1), x1 = (int)ceilf(pos.x + rx + 1);
    int y0 = (int)floorf(cy - ry - 1), y1 = (int)ceilf(cy + ry + 1);

    /* knot */
    br_fill_triangle(r, br_v2(pos.x, cy + ry - 1), br_v2(pos.x - 2 * scale, cy + ry + 2.5f * scale),
                     br_v2(pos.x + 2 * scale, cy + ry + 2.5f * scale), shade(base, 0.7f));
    /* body: shaded ellipse with a rim and a highlight */
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x) {
            float dx = (float)x + 0.5f - pos.x, dy = (float)y + 0.5f - cy;
            float e = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
            br_color c;
            if (e > 1.0f) continue;
            c = balloon_pixel(type, camo, dx, dy, rad, base);
            if (e > 0.72f) c = shade(c, 0.72f);
            else {
                float hx = dx / rx + 0.38f, hy = dy / ry + 0.42f;
                if (hx * hx + hy * hy < 0.06f) c = br_color_lerp(c, BR_WHITE, 0.65f);
            }
            br_draw_pixel(r, (float)x + 0.5f, (float)y + 0.5f, c);
        }
    if (type == TD_LEAD)   /* metallic sheen */
        br_draw_line(r, br_v2(pos.x - rx * 0.2f, cy - ry * 0.6f), br_v2(pos.x + rx * 0.5f, cy + ry * 0.3f), br_rgb(175, 180, 190));
}

static void draw_blimp(br_renderer *r, const td_balloon *b, br_vec2 dir) {
    const float rx = 27, ry = 13;
    br_vec2 n = br_v2_perp(dir), p = b->pos;
    br_color body = br_hex(k_balloon_colors[TD_BLIMP]);
    float damage = 1.0f - (float)b->hp / (float)td_balloon_info(TD_BLIMP)->hp;
    int x, y;
    /* tail fins */
    br_vec2 tail = br_v2_sub(p, br_v2_scale(dir, rx * 0.8f));
    br_fill_triangle(r, tail, br_v2_add(br_v2_sub(tail, br_v2_scale(dir, 10)), br_v2_scale(n, 14)),
                     br_v2_add(tail, br_v2_scale(n, 4)), shade(body, 0.6f));
    br_fill_triangle(r, tail, br_v2_sub(br_v2_sub(tail, br_v2_scale(dir, 10)), br_v2_scale(n, 14)),
                     br_v2_sub(tail, br_v2_scale(n, 4)), shade(body, 0.6f));
    for (y = (int)(p.y - rx - 1); y <= (int)(p.y + rx + 1); ++y)
        for (x = (int)(p.x - rx - 1); x <= (int)(p.x + rx + 1); ++x) {
            float dx = (float)x + 0.5f - p.x, dy = (float)y + 0.5f - p.y;
            float u = dx * dir.x + dy * dir.y, v = dx * n.x + dy * n.y;      /* along / across */
            float e = (u * u) / (rx * rx) + (v * v) / (ry * ry);
            br_color c;
            if (e > 1.0f) continue;
            c = v > 0 ? shade(body, 0.85f) : body;                          /* underside */
            if (e > 0.8f) c = shade(c, 0.6f);
            else if (v < -ry * 0.35f && v > -ry * 0.6f && u > -rx * 0.5f && u < rx * 0.4f) c = shade(c, 1.35f);
            if (damage > 0.3f && ((x * 7 + y * 3) % 23) == 0) c = br_rgb(40, 40, 50);   /* wear */
            br_draw_pixel(r, (float)x + 0.5f, (float)y + 0.5f, c);
        }
    /* health bar */
    br_fill_rect(r, br_rect_make(p.x - 16, p.y - 22, 32, 3), br_rgba(0, 0, 0, 160));
    br_fill_rect(r, br_rect_make(p.x - 16, p.y - 22, 32.0f * (1.0f - damage), 3), br_rgb(120, 230, 90));
}

static void draw_balloon_state(br_renderer *r, const td_game *g, const td_balloon *b, float time) {
    float rad = td_balloon_info(b->type)->radius;
    if (b->type == TD_BLIMP) { draw_blimp(r, b, td_map_direction(&g->map, b->dist)); return; }
    view_draw_balloon(r, b->type, b->camo, b->pos, 1.0f);
    if (b->type == TD_CERAMIC && b->hp < td_balloon_info(TD_CERAMIC)->hp) {   /* cracks */
        br_color crack = br_rgb(70, 40, 20);
        br_draw_line(r, br_v2(b->pos.x - 4, b->pos.y - 5), br_v2(b->pos.x, b->pos.y - 1), crack);
        if (b->hp < 7) br_draw_line(r, br_v2(b->pos.x, b->pos.y - 1), br_v2(b->pos.x + 4, b->pos.y + 3), crack);
        if (b->hp < 4) br_draw_line(r, br_v2(b->pos.x - 5, b->pos.y + 2), br_v2(b->pos.x - 1, b->pos.y + 5), crack);
    }
    if (b->frozen > 0) {                         /* encased in ice */
        br_fill_circle(r, b->pos, rad + 2, br_rgba(190, 235, 255, 120));
        br_draw_circle(r, b->pos, rad + 2, br_rgba(240, 250, 255, 220));
    } else if (b->slowed > 0) {
        br_draw_circle(r, b->pos, rad + 1, br_rgba(150, 210, 255, 150));
    }
    if (b->stunned > 0 && b->frozen <= 0) {      /* circling stars */
        float a = time * 9.0f;
        br_draw_pixel(r, b->pos.x + cosf(a) * (rad + 2), b->pos.y - rad - 1 + sinf(a) * 2, BR_YELLOW);
        br_draw_pixel(r, b->pos.x - cosf(a) * (rad + 2), b->pos.y - rad - 1 - sinf(a) * 2, BR_YELLOW);
    }
}

/* ---- towers ---------------------------------------------------------------------- */

void view_draw_range(br_renderer *r, br_vec2 pos, float range, br_color color) {
    br_color fill = color;
    fill.a = 38;
    br_fill_circle(r, pos, range, fill);
    br_draw_circle(r, pos, range, color);
}

void view_draw_tower(br_renderer *r, td_tower_type type, br_vec2 pos, br_vec2 aim, const int tiers[TD_PATHS], float s) {
    int a = tiers ? tiers[0] : 0, b = tiers ? tiers[1] : 0, i;
    br_vec2 n = br_v2_perp(aim);

    /* stone plinth */
    br_fill_circle(r, br_v2(pos.x + 1.5f * s, pos.y + 2 * s), 10 * s, br_rgba(0, 0, 0, 70));
    br_fill_circle(r, pos, 10 * s, br_rgb(96, 92, 86));
    br_fill_circle(r, pos, 8.5f * s, br_rgb(150, 144, 132));

    switch (type) {
    case TD_DART: {
        br_color body = br_rgb(70, 170, 80);
        int barrels = b >= 3 ? 3 : 1;
        for (i = 0; i < barrels; ++i) {
            br_vec2 d = barrels == 1 ? aim : br_v2_norm(br_v2_add(aim, br_v2_scale(n, (float)(i - 1) * 0.35f)));
            fill_bar(r, pos, d, 10 * s, 1.4f * s, br_rgb(110, 80, 50));
            br_fill_circle(r, br_v2_add(pos, br_v2_scale(d, 10 * s)), 1.2f * s, br_rgb(220, 220, 220));
        }
        br_fill_circle(r, pos, 5.5f * s, body);
        br_fill_circle(r, br_v2(pos.x - 1.5f * s, pos.y - 1.5f * s), 2 * s, br_rgb(130, 215, 130));
        if (a >= 3) br_fill_circle(r, br_v2_add(pos, br_v2_scale(aim, 9 * s)), 3 * s, br_rgb(120, 120, 120));
        break;
    }
    case TD_TACK: {
        br_color body = b >= 3 ? br_rgb(230, 100, 30) : a >= 3 ? br_rgb(200, 200, 215) : br_rgb(160, 80, 200);
        int spikes = b >= 2 || a >= 3 ? 12 : 8;
        for (i = 0; i < spikes; ++i) {
            float ang = (float)i * BR_TAU / (float)spikes;
            br_fill_circle(r, br_v2(pos.x + cosf(ang) * 7 * s, pos.y + sinf(ang) * 7 * s), 1.6f * s, shade(body, 0.7f));
        }
        br_fill_circle(r, pos, 6.5f * s, body);
        br_fill_circle(r, pos, 3 * s, b >= 3 ? br_rgb(255, 220, 90) : shade(body, 1.3f));
        break;
    }
    case TD_BOMB: {
        br_color body = br_rgb(60, 62, 70);
        fill_bar(r, pos, aim, 11 * s, (a >= 1 ? 3.4f : 2.8f) * s, br_rgb(36, 36, 42));
        if (b >= 2) fill_bar(r, br_v2_add(pos, br_v2_scale(aim, 8 * s)), aim, 3 * s, 3 * s, br_rgb(210, 60, 50));
        br_fill_circle(r, pos, 6 * s, body);
        br_fill_circle(r, br_v2(pos.x - 1.5f * s, pos.y - 1.5f * s), 2 * s, br_rgb(110, 112, 124));
        if (b >= 3) for (i = 0; i < 3; ++i)
            br_fill_circle(r, br_v2_add(pos, br_v2_scale(br_v2_add(br_v2_scale(aim, -0.6f), br_v2_scale(n, (float)(i - 1) * 0.7f)), 6 * s)),
                           1.5f * s, br_rgb(20, 20, 24));
        break;
    }
    case TD_ICE: {
        br_color ice = b >= 3 ? br_rgb(220, 245, 255) : br_rgb(140, 215, 245);
        br_vec2 up = br_v2(0, -8 * s), side = br_v2(5 * s, 0);
        br_fill_triangle(r, br_v2_add(pos, up), br_v2_add(pos, side), br_v2_sub(pos, side), ice);
        br_fill_triangle(r, br_v2_sub(pos, up), br_v2_add(pos, side), br_v2_sub(pos, side), shade(ice, 0.8f));
        br_fill_triangle(r, br_v2(pos.x - 2 * s, pos.y - 5 * s), br_v2(pos.x - 0.5f * s, pos.y - 5 * s), br_v2(pos.x - 2.5f * s, pos.y), BR_WHITE);
        if (a >= 3) for (i = 0; i < 4; ++i) {
            float ang = (float)i * BR_TAU / 4.0f + 0.785f;
            br_draw_line(r, pos, br_v2(pos.x + cosf(ang) * 9 * s, pos.y + sinf(ang) * 9 * s), br_rgb(230, 250, 255));
        }
        break;
    }
    case TD_SNIPER: {
        br_color body = br_rgb(150, 130, 80);
        fill_bar(r, pos, aim, (b >= 2 ? 15.0f : 14.0f) * s, 1.1f * s, br_rgb(40, 40, 40));
        br_fill_circle(r, pos, 5.5f * s, body);
        br_fill_circle(r, br_v2_add(pos, br_v2_scale(n, 2 * s)), 1.6f * s, a >= 3 ? br_rgb(240, 60, 60) : br_rgb(60, 60, 60));
        if (b >= 3) br_fill_rect(r, br_rect_make(pos.x - 3 * s, pos.y + 2 * s, 6 * s, 3 * s), br_rgb(250, 210, 60));
        break;
    }
    default: break;
    }

    /* upgrade pips: orange = first path, blue = second */
    if (tiers && s <= 1.0f) {
        for (i = 0; i < a; ++i) br_fill_rect(r, br_rect_make(pos.x - 7 + (float)i * 3, pos.y + 8, 2, 2), br_rgb(255, 170, 40));
        for (i = 0; i < b; ++i) br_fill_rect(r, br_rect_make(pos.x + 2 + (float)i * 3, pos.y + 8, 2, 2), br_rgb(80, 190, 255));
    }
}

/* ---- projectiles ------------------------------------------------------------------- */

static void draw_projectile(br_renderer *r, const td_projectile *p, float time) {
    br_vec2 dir = br_v2_norm(p->vel);
    switch (p->kind) {
    case TD_PROJ_DART:
        br_draw_line(r, br_v2_sub(p->pos, br_v2_scale(dir, 6)), p->pos, br_rgb(90, 60, 30));
        br_draw_line(r, br_v2_add(br_v2_sub(p->pos, br_v2_scale(dir, 6)), br_v2(0.6f, 0.6f)), br_v2_add(p->pos, br_v2(0.6f, 0.6f)), br_rgb(90, 60, 30));
        br_draw_pixel(r, p->pos.x, p->pos.y, br_rgb(235, 235, 235));
        break;
    case TD_PROJ_TACK:
        br_draw_line(r, br_v2_sub(p->pos, br_v2_scale(dir, 3)), br_v2_add(p->pos, br_v2_scale(dir, 1)), br_rgb(205, 205, 220));
        break;
    case TD_PROJ_SPIKEBALL: {
        int i;
        for (i = 0; i < 8; ++i) {
            float a = (float)i * BR_TAU / 8.0f + time * 12.0f;
            br_draw_line(r, p->pos, br_v2(p->pos.x + cosf(a) * (p->radius + 2), p->pos.y + sinf(a) * (p->radius + 2)), br_rgb(60, 60, 60));
        }
        br_fill_circle(r, p->pos, p->radius - 1, br_rgb(130, 130, 136));
        break;
    }
    case TD_PROJ_BOMB:
    case TD_PROJ_BOMBLET:
        br_fill_circle(r, p->pos, p->kind == TD_PROJ_BOMB ? 4.0f : 2.5f, br_rgb(26, 26, 30));
        br_draw_pixel(r, p->pos.x - 1, p->pos.y - 1, br_rgb(120, 120, 130));
        if (((int)(time * 30.0f)) & 1) br_draw_pixel(r, p->pos.x - dir.x * 4, p->pos.y - dir.y * 4, br_rgb(255, 180, 40));
        break;
    }
}

/* ---- the whole field ------------------------------------------------------------------ */

void view_draw_world(br_renderer *r, const td_game *g, float time, int selected) {
    int i;
    br_draw_image(r, &g_background, NULL, br_rect_make(0, 0, TD_MAP_W, TD_MAP_H), BR_WHITE);

    if (selected >= 0 && selected < g->tower_count && g->towers[selected].stats.attack != TD_ATK_HITSCAN)
        view_draw_range(r, g->towers[selected].pos, g->towers[selected].stats.range, br_rgba(255, 255, 255, 170));
    for (i = 0; i < g->tower_count; ++i) {      /* arctic wind shimmer */
        const td_tower *t = &g->towers[i];
        if (t->stats.aura) br_draw_circle(r, t->pos, t->stats.range * (0.6f + 0.4f * (time - floorf(time))), br_rgba(220, 245, 255, 90));
    }

    for (i = 0; i < g->balloon_count; ++i)       /* blimps underneath, the rest on top */
        if (g->balloons[i].type == TD_BLIMP) draw_balloon_state(r, g, &g->balloons[i], time);
    for (i = 0; i < g->balloon_count; ++i)
        if (g->balloons[i].type != TD_BLIMP) draw_balloon_state(r, g, &g->balloons[i], time);

    for (i = 0; i < g->tower_count; ++i) {
        const td_tower *t = &g->towers[i];
        view_draw_tower(r, t->type, t->pos, t->aim, t->tier, 1.0f);
        if (i == selected) br_draw_circle(r, t->pos, 11.5f, br_rgba(255, 255, 255, 220));
    }
    for (i = 0; i < g->projectile_count; ++i) draw_projectile(r, &g->projectiles[i], time);
}
