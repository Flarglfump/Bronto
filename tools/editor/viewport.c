/*
 * BRONTO EDITOR - the scene view.
 *
 *   wheel                  zoom around the cursor
 *   right / middle drag    pan (or hold Space and drag)
 *   click                  select what's under the cursor
 *   drag                   move it (snaps to the grid when Snap is on)
 *   round handle           rotate (15 degree steps with Snap)
 *   square handle          scale
 *
 * The scene is drawn by the engine's own br_builtin_draw through a camera,
 * so it looks exactly as it will in a game. The grid, the game-screen
 * frame, outlines and handles are drawn on top in screen pixels.
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define HANDLE_REACH 7.0f            /* pixels: how close counts as on a handle */

void ed_view_init(ed_view *v) {
    memset(v, 0, sizeof(*v));
    v->zoom = 2.0f;
    v->grid = true;
    v->snap = false;
    v->grid_size = 8;
    v->game_size = br_v2(320, 180);
    v->center = br_v2(160, 90);
}

static br_vec2 view_mid(const ed_view *v) { return br_v2(v->rect.x + v->rect.w / 2, v->rect.y + v->rect.h / 2); }

br_vec2 ed_view_to_screen(const ed_view *v, br_vec2 w) {
    return br_v2_add(view_mid(v), br_v2_scale(br_v2_sub(w, v->center), v->zoom));
}

br_vec2 ed_view_to_world(const ed_view *v, br_vec2 s) {
    return br_v2_add(v->center, br_v2_scale(br_v2_sub(s, view_mid(v)), 1.0f / v->zoom));
}

static bool in_rect(br_rect r, br_vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }

static float snapped(float v, float step) { return roundf(v / step) * step; }

/* ---- the selected entity's handles ----------------------------------------- */

typedef struct handles { bool ok; br_vec2 corner[4], center, rotate, scale, top; } handles;

static handles selection_handles(const editor *ed) {
    handles h;
    br_vec2 c[4], up;
    int i;
    const br_transform *t = (const br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
    memset(&h, 0, sizeof(h));
    if (!t || !br_builtin_box(&ed->doc.world, ed->doc.selected, c)) return h;
    for (i = 0; i < 4; ++i) h.corner[i] = ed_view_to_screen(&ed->view, c[i]);
    h.center = ed_view_to_screen(&ed->view, t->position);
    h.top = br_v2_scale(br_v2_add(h.corner[0], h.corner[1]), 0.5f);
    up = br_v2_norm(br_v2_sub(h.top, h.center));
    if (up.x == 0 && up.y == 0) up = br_v2(0, -1);
    h.rotate = br_v2_add(h.top, br_v2_scale(up, 16));
    h.scale = h.corner[2];
    h.ok = true;
    return h;
}

static float angle_deg(br_vec2 from, br_vec2 to) { return atan2f(to.y - from.y, to.x - from.x) * BR_RAD2DEG; }

/* ---- input --------------------------------------------------------------------- */

static void start_drag(editor *ed, ed_drag kind, br_transform *t) {
    ed_view *v = &ed->view;
    v->drag = kind;
    v->drag_start = *t;
    v->drag_offset = br_v2_sub(t->position, v->mouse_world);
    v->drag_angle = angle_deg(t->position, v->mouse_world);
    v->drag_dist = br_v2_dist(t->position, v->mouse_world);
    if (v->drag_dist < 1e-3f) v->drag_dist = 1e-3f;
}

static void handle_input(editor *ed) {
    ed_view *v = &ed->view;
    const br_input *in = &ed->engine->input;
    br_vec2 m = br_v2(in->mouse_x, in->mouse_y);
    br_transform *t = (br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
    bool panning = br_mouse_down(in, BR_MOUSE_RIGHT) || br_mouse_down(in, BR_MOUSE_MIDDLE) ||
                   (br_mouse_down(in, BR_MOUSE_LEFT) && br_key_down(in, BR_KEY_SPACE));

    v->mouse_inside = in_rect(v->rect, m) && !br_ui_wants_mouse(&ed->ui, m) && ed->pending == ED_PENDING_NONE;
    v->mouse_world = ed_view_to_world(v, m);

    if (v->mouse_inside && in->wheel_y != 0) {               /* zoom, keeping the point under the cursor */
        br_vec2 before = v->mouse_world;
        v->zoom = br_clampf(v->zoom * powf(1.15f, in->wheel_y), 0.1f, 32.0f);
        v->center = br_v2_add(v->center, br_v2_sub(before, ed_view_to_world(v, m)));
        v->mouse_world = ed_view_to_world(v, m);
    }

    if (v->drag == ED_DRAG_NONE && v->mouse_inside) {
        if (br_mouse_pressed(in, BR_MOUSE_RIGHT) || br_mouse_pressed(in, BR_MOUSE_MIDDLE) ||
            (br_mouse_pressed(in, BR_MOUSE_LEFT) && br_key_down(in, BR_KEY_SPACE))) {
            v->drag = ED_DRAG_PAN;
            v->drag_mouse = m;
        } else if (br_mouse_pressed(in, BR_MOUSE_LEFT)) {
            handles h = selection_handles(ed);
            if (h.ok && br_v2_dist(m, h.rotate) <= HANDLE_REACH) start_drag(ed, ED_DRAG_ROTATE, t);
            else if (h.ok && br_v2_dist(m, h.scale) <= HANDLE_REACH) start_drag(ed, ED_DRAG_SCALE, t);
            else {
                ed->doc.selected = ed_doc_pick(&ed->doc, v->mouse_world);
                t = (br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
                if (t) start_drag(ed, ED_DRAG_MOVE, t);
            }
        }
    }

    if (v->drag == ED_DRAG_PAN) {
        if (panning) {
            v->center = br_v2_sub(v->center, br_v2_scale(br_v2_sub(m, v->drag_mouse), 1.0f / v->zoom));
            v->drag_mouse = m;
        } else {
            v->drag = ED_DRAG_NONE;
        }
    } else if (v->drag != ED_DRAG_NONE) {
        if (!t || !br_mouse_down(in, BR_MOUSE_LEFT)) {
            v->drag = ED_DRAG_NONE;
        } else if (v->drag == ED_DRAG_MOVE) {
            t->position = br_v2_add(v->mouse_world, v->drag_offset);
            if (v->snap) t->position = br_v2(snapped(t->position.x, v->grid_size), snapped(t->position.y, v->grid_size));
        } else if (v->drag == ED_DRAG_ROTATE) {
            float a = v->drag_start.rotation + angle_deg(t->position, v->mouse_world) - v->drag_angle;
            a = fmodf(a + 540.0f, 360.0f) - 180.0f;                 /* -180 .. 180 */
            t->rotation = v->snap ? snapped(a, 15) : roundf(a * 10) / 10;
        } else if (v->drag == ED_DRAG_SCALE) {
            float f = br_v2_dist(t->position, v->mouse_world) / v->drag_dist;
            br_vec2 s = br_v2_scale(v->drag_start.scale, f);
            if (v->snap) s = br_v2(snapped(s.x, 0.25f), snapped(s.y, 0.25f));
            if (br_absf(s.x) < 0.05f) s.x = 0.05f;
            if (br_absf(s.y) < 0.05f) s.y = 0.05f;
            t->scale = s;
        }
    }
    v->hover = v->drag == ED_DRAG_NONE && v->mouse_inside ? ed_doc_pick(&ed->doc, v->mouse_world) : BR_ENTITY_NULL;
}

/* ---- drawing ---------------------------------------------------------------------- */

static void draw_grid(editor *ed) {
    const ed_view *v = &ed->view;
    br_renderer *r = &ed->engine->renderer;
    br_vec2 tl = ed_view_to_world(v, br_v2(v->rect.x, v->rect.y));
    br_vec2 brc = ed_view_to_world(v, br_v2(v->rect.x + v->rect.w, v->rect.y + v->rect.h));
    float step = v->grid_size;
    int i, i0, i1;
    while (step * v->zoom < 10) step *= 2;                  /* never denser than 10 px */
    i0 = (int)floorf(tl.x / step); i1 = (int)ceilf(brc.x / step);
    for (i = i0; i <= i1; ++i) {
        float x = floorf(ed_view_to_screen(v, br_v2((float)i * step, 0)).x) + 0.5f;
        br_color c = i == 0 ? br_rgba(120, 140, 170, 140) : (i % 4 == 0) ? br_rgba(255, 255, 255, 34) : br_rgba(255, 255, 255, 14);
        br_draw_line(r, br_v2(x, v->rect.y), br_v2(x, v->rect.y + v->rect.h), c);
    }
    i0 = (int)floorf(tl.y / step); i1 = (int)ceilf(brc.y / step);
    for (i = i0; i <= i1; ++i) {
        float y = floorf(ed_view_to_screen(v, br_v2(0, (float)i * step)).y) + 0.5f;
        br_color c = i == 0 ? br_rgba(120, 140, 170, 140) : (i % 4 == 0) ? br_rgba(255, 255, 255, 34) : br_rgba(255, 255, 255, 14);
        br_draw_line(r, br_v2(v->rect.x, y), br_v2(v->rect.x + v->rect.w, y), c);
    }
}

static void outline(br_renderer *r, const br_vec2 *c, br_color color) {
    int i;
    for (i = 0; i < 4; ++i) br_draw_line(r, c[i], c[(i + 1) % 4], color);
}

static void draw_overlays(editor *ed) {
    const ed_view *v = &ed->view;
    br_renderer *r = &ed->engine->renderer;
    const ed_doc *d = &ed->doc;
    uint32_t i;
    br_vec2 a = ed_view_to_screen(v, br_v2(0, 0)), b = ed_view_to_screen(v, v->game_size);
    char text[64];

    /* the game's screen */
    br_draw_rect(r, br_rect_make(floorf(a.x), floorf(a.y), floorf(b.x - a.x), floorf(b.y - a.y)), br_rgba(255, 255, 255, 110));
    snprintf(text, sizeof(text), "game %dx%d", (int)v->game_size.x, (int)v->game_size.y);
    br_draw_text(r, text, floorf(a.x), floorf(a.y) - 10, 1, br_rgba(255, 255, 255, 110));

    /* entities with nothing to draw still get a marker you can grab */
    for (i = 0; i < d->count; ++i) {
        br_entity e = d->order[i];
        const br_transform *t = (const br_transform *)br_get(&d->world, e, d->ids.transform);
        br_vec2 p;
        if (!t || br_has(&d->world, e, d->ids.shape) || br_has(&d->world, e, d->ids.sprite) || br_has(&d->world, e, d->ids.label)) continue;
        p = ed_view_to_screen(v, t->position);
        br_draw_line(r, br_v2(p.x - 5, p.y), br_v2(p.x + 5, p.y), br_rgb(255, 200, 80));
        br_draw_line(r, br_v2(p.x, p.y - 5), br_v2(p.x, p.y + 5), br_rgb(255, 200, 80));
    }

    if (v->hover != BR_ENTITY_NULL && v->hover != d->selected) {
        br_vec2 c[4];
        int k;
        if (br_builtin_box(&d->world, v->hover, c)) {
            for (k = 0; k < 4; ++k) c[k] = ed_view_to_screen(v, c[k]);
            outline(r, c, br_rgba(255, 255, 255, 120));
        }
    }
    {
        handles h = selection_handles(ed);
        if (h.ok) {
            br_color accent = ed->ui.style.accent;
            outline(r, h.corner, accent);
            br_draw_line(r, h.top, h.rotate, accent);
            br_fill_circle(r, h.rotate, 4, v->drag == ED_DRAG_ROTATE ? BR_WHITE : accent);
            br_fill_rect(r, br_rect_make(h.scale.x - 3, h.scale.y - 3, 7, 7), v->drag == ED_DRAG_SCALE ? BR_WHITE : accent);
            br_fill_circle(r, h.center, 1.5f, accent);
        }
    }
    if (d->playing) {
        br_draw_rect(r, v->rect, br_rgb(90, 220, 110));
        br_draw_rect(r, br_rect_make(v->rect.x + 1, v->rect.y + 1, v->rect.w - 2, v->rect.h - 2), br_rgb(90, 220, 110));
        br_fill_rect(r, br_rect_make(v->rect.x + 6, v->rect.y + 6, 62, 14), br_rgb(90, 220, 110));
        br_draw_text(r, "PLAYING", v->rect.x + 9, v->rect.y + 9, 1, br_rgb(20, 40, 20));
    }
}

/* Assets dragged in from the browser: a preview under the mouse, then the drop. */
static void accept_drops(editor *ed) {
    ed_view *v = &ed->view;
    const char *rel = br_ui_dragging(&ed->ui, ED_ASSET_DRAG), *dropped;
    br_vec2 world;
    if (!rel || !in_rect(v->rect, ed->ui.mouse) || ed->doc.playing) return;
    world = ed_view_to_world(v, ed->ui.mouse);
    if (ed_asset_kind_of(rel, false) == ED_ASSET_IMAGE) {
        const br_image *img = br_asset_image(rel);
        if (img) br_draw_sprite(&ed->engine->renderer, img, NULL, ed->ui.mouse,
                                br_v2((float)img->width / 2, (float)img->height / 2),
                                br_v2(v->zoom, v->zoom), 0, br_rgba(255, 255, 255, 150));
    }
    if ((dropped = br_ui_drop_target(&ed->ui, v->rect, ED_ASSET_DRAG)) != NULL) {
        char path[ED_PATH_MAX];
        snprintf(path, sizeof(path), "%s", dropped);
        ed_assets_drop(ed, path, world);
    }
}

void ed_view_frame(editor *ed, br_rect rect) {
    ed_view *v = &ed->view;
    br_renderer *r = &ed->engine->renderer;
    br_camera2d cam;
    br_recti clip = br_recti_make((int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h);
    v->rect = rect;
    handle_input(ed);

    br_set_clip(r, clip);
    br_fill_rect(r, rect, br_rgb(22, 25, 30));
    if (v->grid) draw_grid(ed);
    cam = br_camera2d_make(v->center, view_mid(v));
    cam.zoom = v->zoom;
    br_begin_camera(r, &cam);
    br_builtin_draw(&ed->doc.world, r);                       /* exactly what a game draws */
    if (ed->show_colliders) br_physics_debug_draw(&ed->doc.world, r, br_rgba(90, 255, 140, 200), br_rgba(255, 220, 80, 200));
    br_end_camera(r);
    draw_overlays(ed);
    accept_drops(ed);
    br_reset_clip(r);
}
