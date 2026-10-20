/*
 * Bronto Engine - rendering.
 *
 * Bronto renders in software into an RGBA8 framebuffer (br_image). The
 * platform layer only has to put those pixels on screen (GDI on Windows,
 * CoreGraphics on macOS, X11 on Linux, a <canvas> on the web), which is
 * what keeps the engine free of GPU API dependencies.
 *
 * The drawing API is deliberately backend-agnostic (positions, rects,
 * images, a 2D camera expressed as a transform) so a GPU backend or a 3D
 * rasterizer path can be added behind the same calls later.
 *
 * Coordinates are floats; pixel centres are at +0.5. While a camera is
 * active, all positions are in world space.
 */
#ifndef BRONTO_RENDER_H
#define BRONTO_RENDER_H

#include "types.h"
#include "bmath.h"

/* ---- colour -------------------------------------------------------------- */
typedef struct br_color { uint8_t r, g, b, a; } br_color;

BR_INLINE br_color br_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    br_color c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}
BR_INLINE br_color br_rgb(uint8_t r, uint8_t g, uint8_t b) { return br_rgba(r, g, b, 255); }
/* 0xRRGGBBAA */
BR_INLINE br_color br_hex(uint32_t v) {
    return br_rgba((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}
BR_INLINE bool br_color_eq(br_color a, br_color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
br_color br_color_lerp(br_color a, br_color b, float t);

#define BR_WHITE   br_rgba(255, 255, 255, 255)
#define BR_BLACK   br_rgba(0, 0, 0, 255)
#define BR_RED     br_rgba(230, 41, 55, 255)
#define BR_GREEN   br_rgba(0, 200, 80, 255)
#define BR_BLUE    br_rgba(0, 121, 241, 255)
#define BR_YELLOW  br_rgba(253, 220, 0, 255)
#define BR_ORANGE  br_rgba(255, 161, 0, 255)
#define BR_PURPLE  br_rgba(160, 80, 220, 255)
#define BR_GRAY    br_rgba(130, 130, 130, 255)
#define BR_DARKGRAY br_rgba(50, 50, 55, 255)
#define BR_BLANK   br_rgba(0, 0, 0, 0)

/* ---- images ------------------------------------------------------------- */
/* Pixels are stored row-major, top row first, bytes in R,G,B,A order. */
typedef struct br_image {
    int width, height;
    br_color *pixels;
} br_image;

bool br_image_init(br_image *img, int width, int height);  /* cleared to transparent */
void br_image_destroy(br_image *img);
/* A smaller (or equal) copy of `src`: each pixel averages the area it
 * covers, with alpha weighting (smooth icons and thumbnails). */
bool br_image_downscale(const br_image *src, br_image *dst, int width, int height);
bool br_image_copy(br_image *dst, const br_image *src);
void br_image_fill(br_image *img, br_color c);
BR_INLINE br_color br_image_get(const br_image *img, int x, int y) {
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return br_rgba(0, 0, 0, 0);
    return img->pixels[(size_t)y * (size_t)img->width + (size_t)x];
}
BR_INLINE void br_image_set(br_image *img, int x, int y, br_color c) {
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return;
    img->pixels[(size_t)y * (size_t)img->width + (size_t)x] = c;
}

/* Image IO - dependency-free codecs.
 * BMP: 8-bit paletted, 24-bit, 32-bit (BI_RGB / BI_BITFIELDS), top-down or bottom-up.
 * TGA: true-colour 24/32-bit, raw or RLE.                                    */
bool br_image_load(br_image *img, const char *path);   /* by extension */
bool br_image_load_bmp_mem(br_image *img, const void *data, size_t size);
bool br_image_load_tga_mem(br_image *img, const void *data, size_t size);
bool br_image_save_bmp(const br_image *img, const char *path);  /* 32-bit, keeps alpha */
bool br_image_save_tga(const br_image *img, const char *path);  /* 32-bit, keeps alpha */
/* RGBA PNG, for tools that want one (icons, screenshots to share). Written
 * without compression (stored deflate blocks): valid everywhere, larger. */
bool br_image_save_png(const br_image *img, const char *path);
void *br_image_encode_png(const br_image *img, size_t *out_size);   /* the file's bytes, br_alloc'd */

/* ---- renderer -------------------------------------------------------------- */
typedef enum br_blend_mode {
    BR_BLEND_ALPHA = 0,  /* standard "over" compositing (default) */
    BR_BLEND_NONE,       /* overwrite                             */
    BR_BLEND_ADD,        /* additive (glows, particles)           */
    BR_BLEND_MULTIPLY    /* darken / tint                         */
} br_blend_mode;

/* screen = offset + zoom * rotate(rotation) * (world - target) */
typedef struct br_camera2d {
    br_vec2 target;   /* world point that appears at `offset` */
    br_vec2 offset;   /* screen position (usually screen centre) */
    float zoom;       /* 1 = 1 world unit per pixel */
    float rotation;   /* radians */
} br_camera2d;

/* 2D affine transform: x' = a*x + c*y + tx ; y' = b*x + d*y + ty */
typedef struct br_affine { float a, b, c, d, tx, ty; } br_affine;

typedef struct br_render_stats {
    uint32_t draw_calls;
    uint64_t pixels_written;
} br_render_stats;

typedef struct br_renderer {
    br_image *target;
    br_recti clip;
    br_blend_mode blend;
    bool camera_active;
    br_camera2d camera;
    br_affine view;      /* world -> screen (identity when no camera) */
    br_affine inv_view;
    br_render_stats stats;
} br_renderer;

void br_renderer_init(br_renderer *r, br_image *target);
void br_renderer_set_target(br_renderer *r, br_image *target); /* resets clip */
void br_renderer_reset_stats(br_renderer *r);

void br_set_clip(br_renderer *r, br_recti clip);   /* screen space */
void br_reset_clip(br_renderer *r);
void br_set_blend(br_renderer *r, br_blend_mode mode);

br_camera2d br_camera2d_make(br_vec2 target, br_vec2 offset);
void br_begin_camera(br_renderer *r, const br_camera2d *cam);
void br_end_camera(br_renderer *r);
br_vec2 br_world_to_screen(const br_renderer *r, br_vec2 p);
br_vec2 br_screen_to_world(const br_renderer *r, br_vec2 p);

/* Primitives */
void br_clear(br_renderer *r, br_color c);           /* ignores clip and camera */
void br_draw_pixel(br_renderer *r, float x, float y, br_color c);
void br_draw_line(br_renderer *r, br_vec2 a, br_vec2 b, br_color c);
void br_draw_rect(br_renderer *r, br_rect rect, br_color c);   /* 1px outline */
void br_fill_rect(br_renderer *r, br_rect rect, br_color c);
void br_draw_circle(br_renderer *r, br_vec2 center, float radius, br_color c);
void br_fill_circle(br_renderer *r, br_vec2 center, float radius, br_color c);
void br_fill_triangle(br_renderer *r, br_vec2 a, br_vec2 b, br_vec2 c, br_color color);
/* Per-vertex colours (Gouraud) - the building block for 3D shading later. */
void br_fill_triangle_colors(br_renderer *r, br_vec2 a, br_vec2 b, br_vec2 c,
                             br_color ca, br_color cb, br_color cc);

/* Images. src may be NULL for the whole image. tint multiplies colour. */
void br_draw_image(br_renderer *r, const br_image *img, const br_recti *src,
                   br_rect dst, br_color tint);
/* Full sprite transform: `origin` is the pivot in source pixels, rotation in
 * radians around the pivot, placed at `pos`. */
void br_draw_sprite(br_renderer *r, const br_image *img, const br_recti *src,
                    br_vec2 pos, br_vec2 origin, br_vec2 scale, float rotation,
                    br_color tint);
/* Arbitrary affine: maps source pixel space to current space. */
void br_draw_image_affine(br_renderer *r, const br_image *img, const br_recti *src,
                          br_affine xf, br_color tint);

/* Text (built-in 8x8 bitmap font, ASCII 32..126). Supports '\n'. */
#define BR_FONT_GLYPH_SIZE 8
void br_draw_text(br_renderer *r, const char *text, float x, float y, float scale, br_color c);
void br_draw_textf(br_renderer *r, float x, float y, float scale, br_color c, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;
br_vec2 br_measure_text(const char *text, float scale);
const uint8_t *br_font_glyph(int ch);   /* 8 rows, LSB = leftmost pixel */

/* Affine helpers */
br_affine br_affine_identity(void);
br_affine br_affine_mul(br_affine outer, br_affine inner);   /* outer(inner(p)) */
br_affine br_affine_inverse(br_affine m);
br_vec2   br_affine_apply(br_affine m, br_vec2 p);
br_affine br_affine_trs(br_vec2 translate, float rotation, br_vec2 scale);

#endif /* BRONTO_RENDER_H */
