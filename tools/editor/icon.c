/*
 * BRONTO EDITOR - the app icon: a little brontosaurus, drawn with the
 * engine's own renderer (no image file to ship).
 *
 * Clip-art style: flat colours with a thick dark outline. Every part is
 * drawn twice - first grown by the outline width in the outline colour,
 * then in its own colour - so the outline wraps the whole silhouette. It is
 * drawn 4x larger than needed and averaged down, which smooths the edges.
 *
 * Coordinates are on a 128 x 128 grid; the dinosaur faces right.
 */
#include "editor.h"
#include <math.h>

#define SUPER 4                                  /* supersampling factor */
#define OUTLINE 3.2f                             /* outline width, grid units */

typedef struct pen { br_renderer *r; float k; float grow; bool outline; } pen;

static const br_color k_line  = { 34, 62, 48, 255 };
static const br_color k_body  = { 108, 196, 120, 255 };
static const br_color k_far   = { 84, 160, 98, 255 };    /* the legs on the far side */
static const br_color k_belly = { 196, 230, 150, 255 };
static const br_color k_spot  = { 76, 150, 92, 255 };

static br_vec2 at(const pen *p, float x, float y) { return br_v2(x * p->k, y * p->k); }

static void ellipse(const pen *p, float cx, float cy, float rx, float ry, br_color c) {
    int i, n = 48;
    br_vec2 mid = at(p, cx, cy), prev;
    rx += p->grow;
    ry += p->grow;
    prev = at(p, cx + rx, cy);
    for (i = 1; i <= n; ++i) {
        float a = (float)i * BR_TAU / (float)n;
        br_vec2 next = at(p, cx + cosf(a) * rx, cy + sinf(a) * ry);
        br_fill_triangle(p->r, mid, prev, next, p->outline ? k_line : c);
        prev = next;
    }
}

static void circle(const pen *p, float x, float y, float radius, br_color c) {
    br_fill_circle(p->r, at(p, x, y), (radius + p->grow) * p->k, p->outline ? k_line : c);
}

/* A tube along a quadratic curve, its radius going from r0 to r1. */
static void tube(const pen *p, br_vec2 a, br_vec2 ctl, br_vec2 b, float r0, float r1, br_color c) {
    int i, n = 40;
    for (i = 0; i <= n; ++i) {
        float t = (float)i / (float)n, u = 1 - t;
        float x = u * u * a.x + 2 * u * t * ctl.x + t * t * b.x;
        float y = u * u * a.y + 2 * u * t * ctl.y + t * t * b.y;
        circle(p, x, y, r0 + (r1 - r0) * t, c);
    }
}

/* A stubby leg: a column with a rounded foot. */
static void leg(const pen *p, float x, float top, float bottom, float w, br_color c) {
    float g = p->grow;
    br_fill_rect(p->r, br_rect_make((x - w / 2 - g) * p->k, (top - g) * p->k, (w + 2 * g) * p->k, (bottom - top) * p->k),
                 p->outline ? k_line : c);
    ellipse(p, x, bottom, w / 2, 3.2f, c);
}

/* The silhouette: everything that gets the outline. */
static void silhouette(const pen *p) {
    leg(p, 48, 80, 104, 11, k_far);                                         /* far legs */
    leg(p, 80, 80, 104, 11, k_far);
    tube(p, br_v2(34, 80), br_v2(10, 92), br_v2(5, 70), 13, 2.5f, k_body);  /* tail, curling up */
    tube(p, br_v2(80, 70), br_v2(100, 60), br_v2(99, 26), 12, 8.5f, k_body);/* neck */
    ellipse(p, 60, 77, 33, 22, k_body);                                     /* body */
    ellipse(p, 106, 22, 13, 9.5f, k_body);                                  /* head */
    leg(p, 40, 84, 108, 12, k_body);                                        /* near legs */
    leg(p, 72, 84, 108, 12, k_body);
}

void ed_make_icon(br_image *out, int size) {
    br_image big;
    br_renderer r;
    pen p;
    if (!br_image_init(&big, size * SUPER, size * SUPER)) { br_image_init(out, size, size); return; }
    br_renderer_init(&r, &big);
    br_clear(&r, br_rgba(0, 0, 0, 0));
    p.r = &r;
    p.k = (float)(size * SUPER) / 128.0f;

    p.grow = OUTLINE; p.outline = true;  silhouette(&p);   /* the outline */
    p.grow = 0;       p.outline = false; silhouette(&p);   /* the colours */

    ellipse(&p, 62, 87, 22, 7.5f, k_belly);                /* belly */
    circle(&p, 49, 61, 4.2f, k_spot);                      /* spots along the back */
    circle(&p, 61, 57.5f, 3.6f, k_spot);
    circle(&p, 72, 61, 3.0f, k_spot);
    circle(&p, 39, 67, 3.0f, k_spot);
    circle(&p, 92, 46, 2.4f, k_spot);
    circle(&p, 107, 18, 4.0f, k_line);                      /* eye */
    circle(&p, 107.6f, 18, 3.0f, br_rgb(255, 255, 255));
    circle(&p, 108.6f, 18.6f, 1.9f, k_line);
    circle(&p, 109.2f, 17.6f, 0.7f, br_rgb(255, 255, 255));
    circle(&p, 113, 25, 2.4f, br_rgb(236, 150, 150));       /* cheek */
    {                                                       /* smile */
        int i;
        for (i = 0; i <= 10; ++i) {
            float a = 0.35f + (float)i * 0.11f;
            circle(&p, 109 + cosf(a) * 7, 21 + sinf(a) * 5.5f, 0.75f, k_line);
        }
    }
    circle(&p, 117.5f, 20, 0.9f, k_line);                   /* nostril */

    if (!br_image_downscale(&big, out, size, size)) br_image_init(out, size, size);
    br_image_destroy(&big);
}
