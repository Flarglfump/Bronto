/* Software rasterizer. */
#include "bronto/render.h"
#include "bronto/log.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- affine ------------------------------------------------------------- */

br_affine br_affine_identity(void) { br_affine m = { 1, 0, 0, 1, 0, 0 }; return m; }

br_affine br_affine_mul(br_affine o, br_affine i) {
    br_affine r;
    r.a = o.a * i.a + o.c * i.b;
    r.b = o.b * i.a + o.d * i.b;
    r.c = o.a * i.c + o.c * i.d;
    r.d = o.b * i.c + o.d * i.d;
    r.tx = o.a * i.tx + o.c * i.ty + o.tx;
    r.ty = o.b * i.tx + o.d * i.ty + o.ty;
    return r;
}

br_affine br_affine_inverse(br_affine m) {
    br_affine r;
    float det = m.a * m.d - m.b * m.c;
    if (br_absf(det) < 1e-12f) return br_affine_identity();
    det = 1.0f / det;
    r.a = m.d * det;
    r.b = -m.b * det;
    r.c = -m.c * det;
    r.d = m.a * det;
    r.tx = -(r.a * m.tx + r.c * m.ty);
    r.ty = -(r.b * m.tx + r.d * m.ty);
    return r;
}

br_vec2 br_affine_apply(br_affine m, br_vec2 p) {
    return br_v2(m.a * p.x + m.c * p.y + m.tx, m.b * p.x + m.d * p.y + m.ty);
}

br_affine br_affine_trs(br_vec2 t, float rotation, br_vec2 s) {
    br_affine m;
    float cs = cosf(rotation), sn = sinf(rotation);
    m.a = cs * s.x;  m.b = sn * s.x;
    m.c = -sn * s.y; m.d = cs * s.y;
    m.tx = t.x;      m.ty = t.y;
    return m;
}

/* ---- pixel ops ---------------------------------------------------------- */

static uint8_t div255(uint32_t t) { t += 128u; return (uint8_t)((t + (t >> 8)) >> 8); }
static uint8_t mul255(uint32_t a, uint32_t b) { return div255(a * b); }

static br_color modulate(br_color c, br_color tint) {
    if (tint.r == 255 && tint.g == 255 && tint.b == 255 && tint.a == 255) return c;
    return br_rgba(mul255(c.r, tint.r), mul255(c.g, tint.g), mul255(c.b, tint.b), mul255(c.a, tint.a));
}

static void blend_px(br_color *d, br_color s, br_blend_mode m) {
    switch (m) {
    case BR_BLEND_NONE:
        *d = s;
        return;
    case BR_BLEND_ALPHA: {
        uint32_t ia;
        if (s.a == 255) { *d = s; return; }
        if (s.a == 0) return;
        ia = 255u - s.a;
        d->r = div255((uint32_t)s.r * s.a + (uint32_t)d->r * ia);
        d->g = div255((uint32_t)s.g * s.a + (uint32_t)d->g * ia);
        d->b = div255((uint32_t)s.b * s.a + (uint32_t)d->b * ia);
        d->a = (uint8_t)(s.a + mul255(d->a, ia));
        return;
    }
    case BR_BLEND_ADD: {
        uint32_t r = d->r + (uint32_t)mul255(s.r, s.a);
        uint32_t g = d->g + (uint32_t)mul255(s.g, s.a);
        uint32_t b = d->b + (uint32_t)mul255(s.b, s.a);
        d->r = (uint8_t)(r > 255 ? 255 : r);
        d->g = (uint8_t)(g > 255 ? 255 : g);
        d->b = (uint8_t)(b > 255 ? 255 : b);
        if (s.a > d->a) d->a = s.a;
        return;
    }
    case BR_BLEND_MULTIPLY:
        d->r = mul255(d->r, 255u - mul255(255u - s.r, s.a));
        d->g = mul255(d->g, 255u - mul255(255u - s.g, s.a));
        d->b = mul255(d->b, 255u - mul255(255u - s.b, s.a));
        return;
    }
}

/* ---- renderer state ------------------------------------------------------ */

void br_renderer_init(br_renderer *r, br_image *target) {
    memset(r, 0, sizeof(*r));
    r->blend = BR_BLEND_ALPHA;
    r->view = r->inv_view = br_affine_identity();
    br_renderer_set_target(r, target);
}

void br_renderer_set_target(br_renderer *r, br_image *target) {
    r->target = target;
    br_reset_clip(r);
}

void br_renderer_reset_stats(br_renderer *r) { memset(&r->stats, 0, sizeof(r->stats)); }

void br_reset_clip(br_renderer *r) {
    r->clip = r->target ? br_recti_make(0, 0, r->target->width, r->target->height)
                        : br_recti_make(0, 0, 0, 0);
}

void br_set_clip(br_renderer *r, br_recti clip) {
    br_recti full = r->target ? br_recti_make(0, 0, r->target->width, r->target->height)
                              : br_recti_make(0, 0, 0, 0);
    r->clip = br_recti_intersect(clip, full);
}

void br_set_blend(br_renderer *r, br_blend_mode mode) { r->blend = mode; }
void br_blend_color(br_color *dst, br_color src, br_blend_mode mode) { blend_px(dst, src, mode); }

br_camera2d br_camera2d_make(br_vec2 target, br_vec2 offset) {
    br_camera2d c;
    c.target = target;
    c.offset = offset;
    c.zoom = 1.0f;
    c.rotation = 0.0f;
    return c;
}

void br_begin_camera(br_renderer *r, const br_camera2d *cam) {
    br_affine to_origin = br_affine_identity();
    float zoom = cam->zoom > 0 ? cam->zoom : 1.0f;
    r->camera = *cam;
    r->camera_active = true;
    to_origin.tx = -cam->target.x;
    to_origin.ty = -cam->target.y;
    r->view = br_affine_mul(br_affine_trs(cam->offset, cam->rotation, br_v2(zoom, zoom)), to_origin);
    r->inv_view = br_affine_inverse(r->view);
}

void br_end_camera(br_renderer *r) {
    r->camera_active = false;
    r->view = r->inv_view = br_affine_identity();
}

br_vec2 br_world_to_screen(const br_renderer *r, br_vec2 p) { return br_affine_apply(r->view, p); }
br_vec2 br_screen_to_world(const br_renderer *r, br_vec2 p) { return br_affine_apply(r->inv_view, p); }

static bool view_rotated(const br_renderer *r) { return r->view.b != 0.0f || r->view.c != 0.0f; }
static float view_scale(const br_renderer *r) { return sqrtf(br_absf(r->view.a * r->view.d - r->view.b * r->view.c)); }

/* ---- low level ----------------------------------------------------------- */

static void put(br_renderer *r, int x, int y, br_color c) {
    if (x < r->clip.x || y < r->clip.y || x >= r->clip.x + r->clip.w || y >= r->clip.y + r->clip.h) return;
    blend_px(&r->target->pixels[(size_t)y * (size_t)r->target->width + (size_t)x], c, r->blend);
    r->stats.pixels_written++;
}

/* Fills pixels [x0, x1) on row y. */
static void span(br_renderer *r, int y, int x0, int x1, br_color c) {
    br_color *row;
    int x;
    if (y < r->clip.y || y >= r->clip.y + r->clip.h) return;
    if (x0 < r->clip.x) x0 = r->clip.x;
    if (x1 > r->clip.x + r->clip.w) x1 = r->clip.x + r->clip.w;
    if (x0 >= x1) return;
    row = r->target->pixels + (size_t)y * (size_t)r->target->width;
    if (r->blend == BR_BLEND_NONE || (r->blend == BR_BLEND_ALPHA && c.a == 255)) {
        for (x = x0; x < x1; ++x) row[x] = c;
    } else {
        for (x = x0; x < x1; ++x) blend_px(&row[x], c, r->blend);
    }
    r->stats.pixels_written += (uint64_t)(x1 - x0);
}

/* First pixel whose centre is >= v. */
static int pix_ceil(float v) { return (int)ceilf(v - 0.5f); }

void br_clear(br_renderer *r, br_color c) {
    if (!r->target) return;
    br_image_fill(r->target, c);
    r->stats.draw_calls++;
}

void br_draw_pixel(br_renderer *r, float x, float y, br_color c) {
    br_vec2 p = br_affine_apply(r->view, br_v2(x, y));
    if (!r->target) return;
    put(r, (int)floorf(p.x), (int)floorf(p.y), c);
    r->stats.draw_calls++;
}

/* ---- lines ----------------------------------------------------------------- */

/* Liang-Barsky clip of segment against [xmin,xmax]x[ymin,ymax]. */
static bool clip_segment(float *x0, float *y0, float *x1, float *y1,
                         float xmin, float ymin, float xmax, float ymax) {
    float t0 = 0, t1 = 1, dx = *x1 - *x0, dy = *y1 - *y0;
    float p[4], q[4];
    int i;
    p[0] = -dx; q[0] = *x0 - xmin;
    p[1] = dx;  q[1] = xmax - *x0;
    p[2] = -dy; q[2] = *y0 - ymin;
    p[3] = dy;  q[3] = ymax - *y0;
    for (i = 0; i < 4; ++i) {
        if (p[i] == 0) { if (q[i] < 0) return false; continue; }
        {
            float t = q[i] / p[i];
            if (p[i] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
            else          { if (t < t0) return false; if (t < t1) t1 = t; }
        }
    }
    *x1 = *x0 + t1 * dx; *y1 = *y0 + t1 * dy;
    *x0 = *x0 + t0 * dx; *y0 = *y0 + t0 * dy;
    return true;
}

static void line_screen(br_renderer *r, br_vec2 a, br_vec2 b, br_color c) {
    int x0, y0, x1, y1, dx, dy, sx, sy, err;
    if (!clip_segment(&a.x, &a.y, &b.x, &b.y, (float)r->clip.x - 1, (float)r->clip.y - 1,
                      (float)(r->clip.x + r->clip.w) + 1, (float)(r->clip.y + r->clip.h) + 1))
        return;
    x0 = (int)floorf(a.x); y0 = (int)floorf(a.y);
    x1 = (int)floorf(b.x); y1 = (int)floorf(b.y);
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;   /* negative */
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        int e2;
        put(r, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void br_draw_line(br_renderer *r, br_vec2 a, br_vec2 b, br_color c) {
    if (!r->target) return;
    line_screen(r, br_affine_apply(r->view, a), br_affine_apply(r->view, b), c);
    r->stats.draw_calls++;
}

/* ---- triangles ------------------------------------------------------------- */

/* Triangles are rasterised in fixed point (1/256 pixel) with exact integer
 * edge functions. In floating point the same edge evaluated from the two
 * triangles that share it can disagree by a rounding error, leaving pixel
 * cracks or double-drawn pixels along it; integers can't disagree. */
#define SUBPIXEL 256
typedef long long fx64;
typedef struct fxpt { fx64 x, y; } fxpt;

static fxpt to_fixed(br_vec2 v) {
    fxpt p;
    p.x = (fx64)floor((double)v.x * SUBPIXEL + 0.5);
    p.y = (fx64)floor((double)v.y * SUBPIXEL + 0.5);
    return p;
}

static fx64 edge_fx(fxpt a, fxpt b, fx64 px, fx64 py) {
    return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

/* Top-left rule: a pixel centre exactly on a shared edge belongs to one side only. */
static bool edge_owns(fxpt a, fxpt b) {
    fx64 dy = b.y - a.y, dx = b.x - a.x;
    return dy < 0 || (dy == 0 && dx > 0);
}

static void raster_triangle(br_renderer *r, br_vec2 f0, br_vec2 f1, br_vec2 f2,
                            const br_color *colors, br_color flat) {
    fxpt v0, v1, v2;
    fx64 area, minx, miny, maxx, maxy;
    float inv_area;
    br_color c0 = flat, c1 = flat, c2 = flat;
    bool o0, o1, o2;
    int x, y, ix0, iy0, ix1, iy1;
    if (colors) { c0 = colors[0]; c1 = colors[1]; c2 = colors[2]; }
    if (!(f0.x == f0.x && f0.y == f0.y && f1.x == f1.x && f1.y == f1.y && f2.x == f2.x && f2.y == f2.y)) return;
    if (fabsf(f0.x) > 1e6f || fabsf(f0.y) > 1e6f || fabsf(f1.x) > 1e6f || fabsf(f1.y) > 1e6f ||
        fabsf(f2.x) > 1e6f || fabsf(f2.y) > 1e6f) return;          /* absurdly far off screen */
    v0 = to_fixed(f0); v1 = to_fixed(f1); v2 = to_fixed(f2);
    area = edge_fx(v0, v1, v2.x, v2.y);
    if (area == 0) return;
    if (area < 0) {
        fxpt t = v1; br_color tc = c1;
        v1 = v2; v2 = t; c1 = c2; c2 = tc;
        area = -area;
    }
    inv_area = 1.0f / (float)area;
    minx = BR_MIN(v0.x, BR_MIN(v1.x, v2.x)); maxx = BR_MAX(v0.x, BR_MAX(v1.x, v2.x));
    miny = BR_MIN(v0.y, BR_MIN(v1.y, v2.y)); maxy = BR_MAX(v0.y, BR_MAX(v1.y, v2.y));
    /* a box around every pixel centre that could be inside */
    ix0 = BR_MAX((int)floor((double)(minx - SUBPIXEL / 2) / SUBPIXEL), r->clip.x);
    iy0 = BR_MAX((int)floor((double)(miny - SUBPIXEL / 2) / SUBPIXEL), r->clip.y);
    ix1 = BR_MIN((int)floor((double)(maxx - SUBPIXEL / 2) / SUBPIXEL) + 1, r->clip.x + r->clip.w);
    iy1 = BR_MIN((int)floor((double)(maxy - SUBPIXEL / 2) / SUBPIXEL) + 1, r->clip.y + r->clip.h);
    o0 = edge_owns(v1, v2); o1 = edge_owns(v2, v0); o2 = edge_owns(v0, v1);

    for (y = iy0; y < iy1; ++y) {
        br_color *row = r->target->pixels + (size_t)y * (size_t)r->target->width;
        fx64 py = (fx64)y * SUBPIXEL + SUBPIXEL / 2;
        for (x = ix0; x < ix1; ++x) {
            fx64 px = (fx64)x * SUBPIXEL + SUBPIXEL / 2;
            fx64 w0 = edge_fx(v1, v2, px, py), w1 = edge_fx(v2, v0, px, py), w2 = edge_fx(v0, v1, px, py);
            br_color c;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if ((w0 == 0 && !o0) || (w1 == 0 && !o1) || (w2 == 0 && !o2)) continue;
            if (colors) {
                float b0 = (float)w0 * inv_area, b1 = (float)w1 * inv_area, b2 = (float)w2 * inv_area;
                c = br_rgba((uint8_t)(c0.r * b0 + c1.r * b1 + c2.r * b2 + 0.5f),
                            (uint8_t)(c0.g * b0 + c1.g * b1 + c2.g * b2 + 0.5f),
                            (uint8_t)(c0.b * b0 + c1.b * b1 + c2.b * b2 + 0.5f),
                            (uint8_t)(c0.a * b0 + c1.a * b1 + c2.a * b2 + 0.5f));
            } else {
                c = flat;
            }
            blend_px(&row[x], c, r->blend);
            r->stats.pixels_written++;
        }
    }
}

void br_fill_triangle(br_renderer *r, br_vec2 a, br_vec2 b, br_vec2 c, br_color color) {
    if (!r->target) return;
    raster_triangle(r, br_affine_apply(r->view, a), br_affine_apply(r->view, b),
                    br_affine_apply(r->view, c), NULL, color);
    r->stats.draw_calls++;
}

void br_fill_triangle_colors(br_renderer *r, br_vec2 a, br_vec2 b, br_vec2 c,
                             br_color ca, br_color cb, br_color cc) {
    br_color cols[3];
    if (!r->target) return;
    cols[0] = ca; cols[1] = cb; cols[2] = cc;
    raster_triangle(r, br_affine_apply(r->view, a), br_affine_apply(r->view, b),
                    br_affine_apply(r->view, c), cols, ca);
    r->stats.draw_calls++;
}

/* ---- rectangles ------------------------------------------------------------ */

static void fill_screen_rect(br_renderer *r, float x0, float y0, float x1, float y1, br_color c) {
    int ix0, iy0, ix1, iy1, y;
    if (x1 < x0) { float t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { float t = y0; y0 = y1; y1 = t; }
    ix0 = pix_ceil(x0); ix1 = pix_ceil(x1);
    iy0 = BR_MAX(pix_ceil(y0), r->clip.y);
    iy1 = BR_MIN(pix_ceil(y1), r->clip.y + r->clip.h);
    for (y = iy0; y < iy1; ++y) span(r, y, ix0, ix1, c);
}

void br_fill_rect(br_renderer *r, br_rect rc, br_color c) {
    if (!r->target) return;
    r->stats.draw_calls++;
    if (!view_rotated(r)) {
        br_vec2 p0 = br_affine_apply(r->view, br_v2(rc.x, rc.y));
        br_vec2 p1 = br_affine_apply(r->view, br_v2(rc.x + rc.w, rc.y + rc.h));
        fill_screen_rect(r, p0.x, p0.y, p1.x, p1.y, c);
    } else {
        br_vec2 a = br_affine_apply(r->view, br_v2(rc.x, rc.y));
        br_vec2 b = br_affine_apply(r->view, br_v2(rc.x + rc.w, rc.y));
        br_vec2 cc = br_affine_apply(r->view, br_v2(rc.x + rc.w, rc.y + rc.h));
        br_vec2 d = br_affine_apply(r->view, br_v2(rc.x, rc.y + rc.h));
        raster_triangle(r, a, b, cc, NULL, c);
        raster_triangle(r, a, cc, d, NULL, c);
    }
}

void br_draw_rect(br_renderer *r, br_rect rc, br_color c) {
    if (!r->target) return;
    if (!view_rotated(r)) {
        br_vec2 p0 = br_affine_apply(r->view, br_v2(rc.x, rc.y));
        br_vec2 p1 = br_affine_apply(r->view, br_v2(rc.x + rc.w, rc.y + rc.h));
        int x0 = pix_ceil(BR_MIN(p0.x, p1.x)), x1 = pix_ceil(BR_MAX(p0.x, p1.x));
        int y0 = pix_ceil(BR_MIN(p0.y, p1.y)), y1 = pix_ceil(BR_MAX(p0.y, p1.y));
        int y;
        if (x1 <= x0 || y1 <= y0) return;
        span(r, y0, x0, x1, c);
        if (y1 - 1 > y0) span(r, y1 - 1, x0, x1, c);
        for (y = y0 + 1; y < y1 - 1; ++y) {
            span(r, y, x0, x0 + 1, c);
            if (x1 - 1 > x0) span(r, y, x1 - 1, x1, c);
        }
    } else {
        br_vec2 a = br_affine_apply(r->view, br_v2(rc.x, rc.y));
        br_vec2 b = br_affine_apply(r->view, br_v2(rc.x + rc.w, rc.y));
        br_vec2 cc = br_affine_apply(r->view, br_v2(rc.x + rc.w, rc.y + rc.h));
        br_vec2 d = br_affine_apply(r->view, br_v2(rc.x, rc.y + rc.h));
        line_screen(r, a, b, c); line_screen(r, b, cc, c);
        line_screen(r, cc, d, c); line_screen(r, d, a, c);
    }
    r->stats.draw_calls++;
}

/* ---- circles --------------------------------------------------------------- */

/* Horizontal extent [x0, x1) of a disc on row y; false if the row misses. */
static bool disc_span(float cx, float cy, float rad, int y, int *x0, int *x1) {
    float dy = ((float)y + 0.5f) - cy, dx;
    if (rad <= 0 || dy * dy > rad * rad) return false;
    dx = sqrtf(rad * rad - dy * dy);
    *x0 = pix_ceil(cx - dx);
    *x1 = pix_ceil(cx + dx);
    return *x1 > *x0;
}

void br_fill_circle(br_renderer *r, br_vec2 center, float radius, br_color c) {
    br_vec2 p;
    float rad;
    int y, y0, y1, x0, x1;
    if (!r->target) return;
    p = br_affine_apply(r->view, center);
    rad = radius * view_scale(r);
    y0 = BR_MAX(pix_ceil(p.y - rad), r->clip.y);
    y1 = BR_MIN(pix_ceil(p.y + rad), r->clip.y + r->clip.h);
    for (y = y0; y < y1; ++y)
        if (disc_span(p.x, p.y, rad, y, &x0, &x1)) span(r, y, x0, x1, c);
    r->stats.draw_calls++;
}

void br_draw_circle(br_renderer *r, br_vec2 center, float radius, br_color c) {
    br_vec2 p;
    float rad;
    int y, y0, y1, ox0, ox1, ix0, ix1;
    if (!r->target) return;
    p = br_affine_apply(r->view, center);
    rad = radius * view_scale(r);
    y0 = BR_MAX(pix_ceil(p.y - rad), r->clip.y);
    y1 = BR_MIN(pix_ceil(p.y + rad), r->clip.y + r->clip.h);
    for (y = y0; y < y1; ++y) {
        if (!disc_span(p.x, p.y, rad, y, &ox0, &ox1)) continue;
        /* Ring = outer disc minus inner disc, so no pixel is drawn twice. */
        if (disc_span(p.x, p.y, rad - 1.0f, y, &ix0, &ix1)) {
            span(r, y, ox0, ix0, c);
            span(r, y, ix1, ox1, c);
        } else {
            span(r, y, ox0, ox1, c);
        }
    }
    r->stats.draw_calls++;
}

/* ---- images ---------------------------------------------------------------- */

void br_draw_image_affine(br_renderer *r, const br_image *img, const br_recti *src,
                          br_affine xf, br_color tint) {
    br_recti s;
    br_affine full, inv;
    br_vec2 corners[4];
    float minx, miny, maxx, maxy;
    int i, x, y, ix0, iy0, ix1, iy1, sx1, sy1;
    bool opaque_fast;
    if (!r->target || !img || !img->pixels) return;
    s = src ? br_recti_intersect(*src, br_recti_make(0, 0, img->width, img->height))
            : br_recti_make(0, 0, img->width, img->height);
    if (s.w <= 0 || s.h <= 0) return;
    full = br_affine_mul(r->view, xf);
    inv = br_affine_inverse(full);
    corners[0] = br_affine_apply(full, br_v2((float)s.x, (float)s.y));
    corners[1] = br_affine_apply(full, br_v2((float)(s.x + s.w), (float)s.y));
    corners[2] = br_affine_apply(full, br_v2((float)(s.x + s.w), (float)(s.y + s.h)));
    corners[3] = br_affine_apply(full, br_v2((float)s.x, (float)(s.y + s.h)));
    minx = maxx = corners[0].x;
    miny = maxy = corners[0].y;
    for (i = 1; i < 4; ++i) {
        minx = BR_MIN(minx, corners[i].x); maxx = BR_MAX(maxx, corners[i].x);
        miny = BR_MIN(miny, corners[i].y); maxy = BR_MAX(maxy, corners[i].y);
    }
    ix0 = BR_MAX(pix_ceil(minx), r->clip.x);
    iy0 = BR_MAX(pix_ceil(miny), r->clip.y);
    ix1 = BR_MIN(pix_ceil(maxx), r->clip.x + r->clip.w);
    iy1 = BR_MIN(pix_ceil(maxy), r->clip.y + r->clip.h);
    sx1 = s.x + s.w;
    sy1 = s.y + s.h;
    opaque_fast = r->blend == BR_BLEND_NONE;

    for (y = iy0; y < iy1; ++y) {
        br_color *row = r->target->pixels + (size_t)y * (size_t)r->target->width;
        float py = (float)y + 0.5f;
        float u = inv.a * ((float)ix0 + 0.5f) + inv.c * py + inv.tx;
        float v = inv.b * ((float)ix0 + 0.5f) + inv.d * py + inv.ty;
        for (x = ix0; x < ix1; ++x, u += inv.a, v += inv.b) {
            int iu = (int)floorf(u), iv = (int)floorf(v);
            br_color c;
            if (iu < s.x || iv < s.y || iu >= sx1 || iv >= sy1) continue;
            c = modulate(img->pixels[(size_t)iv * (size_t)img->width + (size_t)iu], tint);
            if (opaque_fast) row[x] = c;
            else blend_px(&row[x], c, r->blend);
            r->stats.pixels_written++;
        }
    }
    r->stats.draw_calls++;
}

void br_draw_image(br_renderer *r, const br_image *img, const br_recti *src,
                   br_rect dst, br_color tint) {
    br_recti s;
    br_affine xf;
    if (!img) return;
    s = src ? *src : br_recti_make(0, 0, img->width, img->height);
    if (s.w <= 0 || s.h <= 0) return;
    xf.a = dst.w / (float)s.w;
    xf.d = dst.h / (float)s.h;
    xf.b = xf.c = 0;
    xf.tx = dst.x - (float)s.x * xf.a;
    xf.ty = dst.y - (float)s.y * xf.d;
    br_draw_image_affine(r, img, &s, xf, tint);
}

void br_draw_sprite(br_renderer *r, const br_image *img, const br_recti *src,
                    br_vec2 pos, br_vec2 origin, br_vec2 scale, float rotation, br_color tint) {
    br_recti s;
    br_affine pivot = br_affine_identity();
    if (!img) return;
    s = src ? *src : br_recti_make(0, 0, img->width, img->height);
    pivot.tx = -((float)s.x + origin.x);
    pivot.ty = -((float)s.y + origin.y);
    br_draw_image_affine(r, img, &s, br_affine_mul(br_affine_trs(pos, rotation, scale), pivot), tint);
}

/* ---- text ------------------------------------------------------------------- */

#define LINE_HEIGHT 10.0f
#define ADVANCE      8.0f

void br_draw_text(br_renderer *r, const char *text, float x, float y, float scale, br_color c) {
    float cx = x, cy = y;
    const unsigned char *p = (const unsigned char *)text;
    uint32_t calls;
    if (!r->target || !text) return;
    if (scale <= 0) scale = 1;
    calls = r->stats.draw_calls;
    for (; *p; ++p) {
        const uint8_t *g;
        int row, col;
        if (*p == '\n') { cx = x; cy += LINE_HEIGHT * scale; continue; }
        g = br_font_glyph(*p);
        for (row = 0; row < 8; ++row) {
            uint8_t bits = g[row];
            if (!bits) continue;
            for (col = 0; col < 8; ++col)
                if (bits & (1u << col))
                    br_fill_rect(r, br_rect_make(cx + (float)col * scale, cy + (float)row * scale, scale, scale), c);
        }
        cx += ADVANCE * scale;
    }
    r->stats.draw_calls = calls + 1;
}

void br_draw_textf(br_renderer *r, float x, float y, float scale, br_color c, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    br_draw_text(r, buf, x, y, scale, c);
}

br_vec2 br_measure_text(const char *text, float scale) {
    float w = 0, line = 0, lines = 1;
    if (!text || !*text) return br_v2(0, 0);
    for (; *text; ++text) {
        if (*text == '\n') { lines += 1; line = 0; continue; }
        line += ADVANCE;
        if (line > w) w = line;
    }
    return br_v2(w * scale, ((lines - 1) * LINE_HEIGHT + 8.0f) * scale);
}
