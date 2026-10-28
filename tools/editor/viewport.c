/*
 * BRONTO EDITOR - the scene view.
 *
 *   wheel                  zoom around the cursor
 *   right / middle drag    pan (or hold Space and drag)
 *   right click            the menu for what's under the cursor (menus.c)
 *   click                  select what's under the cursor
 *   Shift / Ctrl + click   add it to the selection, or take it out
 *   drag on empty space    a box: selects what's inside (with Shift / Ctrl: adds it)
 *   drag                   move it, and everything selected with it (snaps to the grid when Snap is on)
 *   round handle           rotate (15 degree steps with Snap)
 *   square handle          scale
 *
 * Scenes with 3D entities are shown in 3D instead (viewport3d.c).
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
    v->hot_axis = -1;
    ed_view3d_reset(v);
    br_r3d_init(&v->r3);
}

void ed_view_destroy(ed_view *v) { br_r3d_destroy(&v->r3); }

static br_vec2 view_mid(const ed_view *v) { return br_v2(v->rect.x + v->rect.w / 2, v->rect.y + v->rect.h / 2); }

/* The editor's own view: game-screen coordinates <-> the editor's pixels. */
static br_vec2 frame_to_screen(const ed_view *v, br_vec2 w) {
    return br_v2_add(view_mid(v), br_v2_scale(br_v2_sub(w, v->center), v->zoom));
}
static br_vec2 screen_to_frame(const ed_view *v, br_vec2 s) {
    return br_v2_add(v->center, br_v2_scale(br_v2_sub(s, view_mid(v)), 1.0f / v->zoom));
}

/* The game's camera: world <-> game-screen coordinates. */
static br_vec2 game_to_frame(const ed_view *v, br_vec2 w) {
    br_renderer r;
    memset(&r, 0, sizeof(r));
    br_begin_camera(&r, &v->game_camera);
    return br_world_to_screen(&r, w);
}
static br_vec2 frame_to_game(const ed_view *v, br_vec2 s) {
    br_renderer r;
    memset(&r, 0, sizeof(r));
    br_begin_camera(&r, &v->game_camera);
    return br_screen_to_world(&r, s);
}

br_vec2 ed_view_to_screen(const ed_view *v, br_vec2 w) {
    return frame_to_screen(v, v->use_game_camera ? game_to_frame(v, w) : w);
}

br_vec2 ed_view_to_world(const ed_view *v, br_vec2 s) {
    br_vec2 f = screen_to_frame(v, s);
    return v->use_game_camera ? frame_to_game(v, f) : f;
}

static bool in_rect(br_rect r, br_vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }

static float snapped(float v, float step) { return roundf(v / step) * step; }

/* ---- the selected entity's handles ----------------------------------------- */

typedef struct handles { bool ok, multi; br_vec2 corner[4], center, rotate, scale, top; } handles;

static handles selection_handles(const editor *ed) {
    handles h;
    br_vec2 c[4], up;
    int i;
    const br_transform *t = (const br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
    memset(&h, 0, sizeof(h));
    if (!t || !br_builtin_box(&ed->doc.world, ed->doc.selected, c)) return h;
    h.multi = ed->doc.multi_count > 1 && ed->doc.selected == ed->doc.multi_primary;   /* several: no rotate / scale handles */
    for (i = 0; i < 4; ++i) h.corner[i] = ed_view_to_screen(&ed->view, c[i]);
    h.center = ed_view_to_screen(&ed->view, br_world_transform(&ed->doc.world, ed->doc.selected).position);
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

/* Remembers where every selected root is, for a drag that moves them together. */
void ed_view_grab_group(editor *ed) {
    ed_view *v = &ed->view;
    uint32_t i;
    v->group_count = ed_doc_selection(&ed->doc, v->group, ED_SELECT_MAX, true);
    for (i = 0; i < v->group_count; ++i) {
        br_entity e = v->group[i];
        if (br_has(&ed->doc.world, e, ed->doc.ids.transform)) v->group_from[i] = br_world_transform(&ed->doc.world, e).position;
        if (br_has(&ed->doc.world, e, ed->doc.ids3.transform3d)) {   /* (3D: world place and turn; its own, local, scale) */
            br_world_transform3d(&ed->doc.world, e, &v->group_from3[i], &v->group_rot3[i], NULL);
            v->group_scale3[i] = ((const br_transform3d *)br_get(&ed->doc.world, e, ed->doc.ids3.transform3d))->scale;
        }
    }
}

static void start_drag(editor *ed, ed_drag kind, br_transform *t) {
    ed_view *v = &ed->view;
    br_vec2 wp = br_world_transform(&ed->doc.world, ed->doc.selected).position;   /* gizmos work on screen */
    ed_view_grab_group(ed);
    v->drag_from2 = wp;
    v->drag = kind;
    v->drag_start = *t;
    v->drag_offset = br_v2_sub(wp, v->mouse_world);
    v->drag_angle = angle_deg(wp, v->mouse_world);
    v->drag_dist = br_v2_dist(wp, v->mouse_world);
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
            v->press_at = m;
            v->pan_moved = false;
            v->pan_button = br_mouse_pressed(in, BR_MOUSE_RIGHT) ? BR_MOUSE_RIGHT : BR_MOUSE_MIDDLE;
        } else if (br_mouse_pressed(in, BR_MOUSE_LEFT)) {
            handles h = selection_handles(ed);
            bool add = (in->mods & (BR_MOD_SHIFT | BR_MOD_CONTROL | BR_MOD_SUPER)) != 0;
            if (h.ok && !h.multi && !add && br_v2_dist(m, h.rotate) <= HANDLE_REACH) start_drag(ed, ED_DRAG_ROTATE, t);
            else if (h.ok && !h.multi && !add && br_v2_dist(m, h.scale) <= HANDLE_REACH) start_drag(ed, ED_DRAG_SCALE, t);
            else {
                br_entity hit = ed_doc_pick(&ed->doc, v->mouse_world);
                if (hit == BR_ENTITY_NULL) {                     /* empty space: a box to select with */
                    if (!add) ed_doc_select(&ed->doc, BR_ENTITY_NULL);
                    v->drag = ED_DRAG_BOX;
                    v->press_at = m;
                    v->box_add = add;
                } else if (add) {
                    ed_doc_select_toggle(&ed->doc, hit);
                } else {
                    if (ed_doc_is_selected(&ed->doc, hit)) ed_doc_select_add(&ed->doc, hit);   /* (keeps the others) */
                    else ed_doc_select(&ed->doc, hit);
                    t = (br_transform *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids.transform);
                    if (t) start_drag(ed, ED_DRAG_MOVE, t);
                }
            }
        }
    }

    if (v->drag == ED_DRAG_PAN) {
        if (panning) {
            v->center = br_v2_sub(v->center, br_v2_scale(br_v2_sub(m, v->drag_mouse), 1.0f / v->zoom));
            v->drag_mouse = m;
            if (br_v2_dist(m, v->press_at) > 3) v->pan_moved = true;
        } else {
            v->drag = ED_DRAG_NONE;
            if (v->pan_button == BR_MOUSE_RIGHT && !v->pan_moved)   /* a click, not a drag */
                ed_menu_view(ed, ed_view_to_world(v, m));
        }
    } else if (v->drag == ED_DRAG_BOX) {
        if (!br_mouse_down(in, BR_MOUSE_LEFT)) {                /* select what's inside */
            br_rect box = br_rect_make(BR_MIN(v->press_at.x, m.x), BR_MIN(v->press_at.y, m.y), br_absf(m.x - v->press_at.x), br_absf(m.y - v->press_at.y));
            uint32_t i;
            v->drag = ED_DRAG_NONE;
            if (box.w > 2 || box.h > 2) {
                for (i = 0; i < ed->doc.count; ++i) {
                    br_entity e = ed->doc.order[i];
                    if (!br_has(&ed->doc.world, e, ed->doc.ids.transform)) continue;
                    if (in_rect(box, ed_view_to_screen(v, br_world_transform(&ed->doc.world, e).position)))
                        ed_doc_select_add(&ed->doc, e);
                }
                if (ed_doc_selection_count(&ed->doc) > 1) ed_status(ed, "%u selected", (unsigned)ed_doc_selection_count(&ed->doc));
            }
        }
    } else if (v->drag != ED_DRAG_NONE) {
        if (!t || !br_mouse_down(in, BR_MOUSE_LEFT)) {
            v->drag = ED_DRAG_NONE;
        } else if (v->drag == ED_DRAG_MOVE) {
            br_vec2 to = br_v2_add(v->mouse_world, v->drag_offset), delta;
            uint32_t i;
            if (v->snap) to = br_v2(snapped(to.x, v->grid_size), snapped(to.y, v->grid_size));
            delta = br_v2_sub(to, v->drag_from2);
            for (i = 0; i < v->group_count; ++i)           /* every selected root by as much (children follow) */
                if (br_has(&ed->doc.world, v->group[i], ed->doc.ids.transform))
                    br_set_world_position(&ed->doc.world, v->group[i], br_v2_add(v->group_from[i], delta));   /* a child: into its parent's space */
        } else if (v->drag == ED_DRAG_ROTATE) {
            br_vec2 wp = br_world_transform(&ed->doc.world, ed->doc.selected).position;
            float a = v->drag_start.rotation + angle_deg(wp, v->mouse_world) - v->drag_angle;
            a = fmodf(a + 540.0f, 360.0f) - 180.0f;                 /* -180 .. 180 */
            t->rotation = v->snap ? snapped(a, 15) : roundf(a * 10) / 10;
        } else if (v->drag == ED_DRAG_SCALE) {
            float f = br_v2_dist(br_world_transform(&ed->doc.world, ed->doc.selected).position, v->mouse_world) / v->drag_dist;
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
    br_vec2 a = frame_to_screen(v, br_v2(0, 0)), b = frame_to_screen(v, v->game_size);
    char text[64];

    /* the game's screen */
    br_draw_rect(r, br_rect_make(floorf(a.x), floorf(a.y), floorf(b.x - a.x), floorf(b.y - a.y)), br_rgba(255, 255, 255, 110));
    snprintf(text, sizeof(text), v->use_game_camera ? "game %dx%d (through its camera)" : "game %dx%d", (int)v->game_size.x, (int)v->game_size.y);
    br_draw_text(r, text, floorf(a.x), floorf(a.y) - 10, 1, br_rgba(255, 255, 255, 110));

    /* cameras: what each one shows (blue; the current one brighter) and its limits (orange) */
    if (!v->use_game_camera) {
        for (i = 0; i < d->count; ++i) {
            br_entity e = d->order[i];
            const br_camera *cam = (const br_camera *)br_get(&d->world, e, d->ids.camera);
            br_vec2 c[4], p;
            float hw, hh, z, ang, cs, sn;
            int k;
            bool current;
            if (!cam || !br_has(&d->world, e, d->ids.transform)) continue;
            current = br_camera_current(&d->world) == e;
            z = cam->zoom > 0.01f ? cam->zoom : 1;
            hw = v->game_size.x / 2 / z;
            hh = v->game_size.y / 2 / z;
            p = br_world_transform(&d->world, e).position;
            ang = br_world_transform(&d->world, e).rotation * (BR_PI / 180.0f);
            cs = cosf(ang); sn = sinf(ang);
            c[0] = br_v2(-hw, -hh); c[1] = br_v2(hw, -hh); c[2] = br_v2(hw, hh); c[3] = br_v2(-hw, hh);
            for (k = 0; k < 4; ++k) c[k] = ed_view_to_screen(v, br_v2(p.x + c[k].x * cs + c[k].y * sn, p.y - c[k].x * sn + c[k].y * cs));
            outline(r, c, current ? br_rgba(110, 190, 255, 220) : br_rgba(110, 190, 255, 90));
            {
                char label[64];
                ed_doc_name(d, e, label, sizeof(label));
                br_draw_text(r, label, c[0].x + 2, c[0].y + 2, 1, current ? br_rgba(110, 190, 255, 220) : br_rgba(110, 190, 255, 90));
            }
            if (cam->limit) {
                br_vec2 lo = ed_view_to_screen(v, cam->limit_min), hi = ed_view_to_screen(v, cam->limit_max);
                br_draw_rect(r, br_rect_make(BR_MIN(lo.x, hi.x), BR_MIN(lo.y, hi.y), br_absf(hi.x - lo.x), br_absf(hi.y - lo.y)), br_rgba(255, 170, 60, 160));
            }
        }
    }

    /* entities with nothing to draw still get a marker you can grab */
    for (i = 0; i < d->count; ++i) {
        br_entity e = d->order[i];
        const br_transform *t = (const br_transform *)br_get(&d->world, e, d->ids.transform);
        br_vec2 p;
        if (!t || br_has(&d->world, e, d->ids.shape) || br_has(&d->world, e, d->ids.sprite) || br_has(&d->world, e, d->ids.label)) continue;
        p = ed_view_to_screen(v, br_world_transform(&d->world, e).position);
        br_draw_line(r, br_v2(p.x - 5, p.y), br_v2(p.x + 5, p.y), br_rgb(255, 200, 80));
        br_draw_line(r, br_v2(p.x, p.y - 5), br_v2(p.x, p.y + 5), br_rgb(255, 200, 80));
    }

    if (d->multi_count > 1 && d->selected == d->multi_primary) {   /* the rest of a multiple selection */
        uint32_t k;
        for (k = 0; k < d->multi_count; ++k) {
            br_vec2 c[4];
            int j;
            if (d->multi[k] == d->selected || !br_builtin_box(&d->world, d->multi[k], c)) continue;
            for (j = 0; j < 4; ++j) c[j] = ed_view_to_screen(v, c[j]);
            outline(r, c, br_rgba(ed->ui.style.accent.r, ed->ui.style.accent.g, ed->ui.style.accent.b, 170));
        }
    }
    if (v->drag == ED_DRAG_BOX) {                                  /* the box being drawn */
        br_vec2 m = br_v2(ed->engine->input.mouse_x, ed->engine->input.mouse_y);
        br_rect box = br_rect_make(BR_MIN(v->press_at.x, m.x), BR_MIN(v->press_at.y, m.y), br_absf(m.x - v->press_at.x), br_absf(m.y - v->press_at.y));
        br_fill_rect(r, box, br_rgba(ed->ui.style.accent.r, ed->ui.style.accent.g, ed->ui.style.accent.b, 40));
        br_draw_rect(r, box, ed->ui.style.accent);
    }
    if (v->hover != BR_ENTITY_NULL && !ed_doc_is_selected(d, v->hover)) {
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
            if (!h.multi) {
                br_draw_line(r, h.top, h.rotate, accent);
                br_fill_circle(r, h.rotate, 4, v->drag == ED_DRAG_ROTATE ? BR_WHITE : accent);
                br_fill_rect(r, br_rect_make(h.scale.x - 3, h.scale.y - 3, 7, 7), v->drag == ED_DRAG_SCALE ? BR_WHITE : accent);
            }
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
    if (v->three_d) { ed_view3d_frame(ed, rect); return; }
    v->rect = rect;
    if (!ed->dbg.stopped) handle_input(ed);          /* (a script held by the debugger: look only) */

    br_set_clip(r, clip);
    br_fill_rect(r, rect, br_rgb(22, 25, 30));
    cam = br_camera2d_make(v->center, view_mid(v));
    cam.zoom = v->zoom;
    /* Playing with a Camera: the world as the game's camera shows it, its HUD on the screen frame. */
    v->use_game_camera = ed->doc.playing &&
        br_camera_view(&ed->doc.world, (int)v->game_size.x, (int)v->game_size.y, &v->game_camera, &v->game_ui_layer);
    if (v->grid) draw_grid(ed);
    {
        int depth = ed_doc_stack_depth(&ed->doc), first = depth - 1, i;
        while (first > 0 && ed->doc.overlays[first - 1]->transparent) --first;     /* the lowest scene still showing */
        for (i = first; i < depth; ++i) {
            br_world *w = ed_doc_stack_world(&ed->doc, i);
            br_camera2d game, world_cam;
            int ui_layer;
            if (ed->doc.playing && br_camera_view(w, (int)v->game_size.x, (int)v->game_size.y, &game, &ui_layer)) {
                /* editor(game(p)): the game's camera, then the editor's view of the game's screen */
                world_cam = cam;
                world_cam.zoom = cam.zoom * game.zoom;
                world_cam.rotation = game.rotation;
                {
                    br_vec2 d0 = br_v2_scale(br_v2_sub(game.offset, cam.target), 1.0f / game.zoom);
                    float cs = cosf(game.rotation), sn = sinf(game.rotation);
                    world_cam.target = br_v2_sub(game.target, br_v2(d0.x * cs + d0.y * sn, -d0.x * sn + d0.y * cs));
                }
                br_builtin_draw_layers(w, r, &world_cam, &cam, ui_layer);
                if (ed->show_colliders && i == 0) {
                    br_begin_camera(r, &world_cam);
                    br_physics_debug_draw(w, r, br_rgba(90, 255, 140, 200), br_rgba(255, 220, 80, 200));
                    br_end_camera(r);
                }
            } else {
                br_begin_camera(r, &cam);
                br_builtin_draw(w, r);                         /* exactly what a game draws */
                if (ed->show_colliders && i == 0) br_physics_debug_draw(w, r, br_rgba(90, 255, 140, 200), br_rgba(255, 220, 80, 200));
                br_end_camera(r);
            }
        }
    }
    draw_overlays(ed);
    if (!ed->dbg.stopped) accept_drops(ed);
    br_reset_clip(r);
}
