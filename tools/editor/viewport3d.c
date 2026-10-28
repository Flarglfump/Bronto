/*
 * BRONTO EDITOR - the scene view in 3D.
 *
 * The same view as viewport.c, switched to 3D for scenes with 3D entities
 * (the toolbar's 3D button switches by hand):
 *
 *   right drag (or Alt + left drag)   orbit around the focus point
 *   middle drag (or Shift + right)    pan
 *   wheel                             dolly in and out
 *   right click                       the view's menu (New 3D: cube, sphere, camera, light...)
 *   click                             select what's under the cursor (meshes; cameras, lights
 *                                     and empties by their markers)
 *   W / E / R                         the gizmo: move, rotate, scale
 *   drag an arrow / ring / box        along that axis; the centre moves across the ground
 *                                     (or scales evenly)
 *   C                                 look through the scene's Camera3D (again: the orbit camera)
 *   F                                 frame the selection
 *
 * Snap (toolbar) rounds moves to half units, turns to 15 degrees, scales to tenths.
 * The scene is drawn by the engine's br_draw3d, so it looks as it will in a game;
 * the grid, markers, outlines and gizmos are drawn on top.
 */
#include "editor.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define GIZMO_PX 70.0f              /* how long the gizmo's arrows are on screen */
#define REACH 7.0f                  /* pixels: how close counts as on a handle */
#define MARKER_REACH 9.0f
#define MOVE_SNAP 0.5f
#define TURN_SNAP 15.0f
#define SCALE_SNAP 0.1f

static const br_vec3 k_axis[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
static const br_color k_axis_color[3] = { { 230, 80, 80, 255 }, { 110, 210, 90, 255 }, { 80, 140, 240, 255 } };

static bool in_rect(br_rect r, br_vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h; }
static float snapped(float v, float step) { return roundf(v / step) * step; }
static br_recti irect(br_rect r) { return br_recti_make((int)r.x, (int)r.y, (int)r.w, (int)r.h); }

void ed_view3d_reset(ed_view *v) {
    v->orbit_target = br_v3(0, 0, 0);
    v->orbit_yaw = 35;
    v->orbit_pitch = 28;
    v->orbit_dist = 12;
}

const char *ed_new3d_name(ed_new3d kind) {
    static const char *names[ED_NEW_COUNT] = { "Cube", "Sphere", "Plane", "Cylinder", "Cone", "Capsule", "Character", "Particles", "Camera", "Light", "Empty" };
    return kind < ED_NEW_COUNT ? names[kind] : "Empty";
}

/* ---- the orbit camera ------------------------------------------------------------- */

br_camera3d ed_view3d_camera(const ed_view *v) {
    float yaw = v->orbit_yaw * BR_DEG2RAD, pitch = v->orbit_pitch * BR_DEG2RAD;
    br_vec3 back = br_v3(sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch));
    br_camera3d c = br_camera3d_make(br_v3_add(v->orbit_target, br_v3_scale(back, v->orbit_dist)), v->orbit_target);
    c.near_clip = fmaxf(0.02f, v->orbit_dist * 0.005f);
    c.far_clip = fmaxf(1000.0f, v->orbit_dist * 20);
    return c;
}

/* The camera the view draws with: the orbit one, or the scene's own. */
static br_camera3d view_camera(const editor *ed, bool *through) {
    br_camera3d c;
    if (ed->view.through_camera && br_scene_camera3d(&ed->doc.world, &c, NULL)) { if (through) *through = true; return c; }
    if (through) *through = false;
    return ed_view3d_camera(&ed->view);
}

/* Set up r3 for this frame's camera (for projecting and rays, even before drawing). */
static void begin(editor *ed) {
    br_camera3d c = view_camera(ed, NULL);
    br_r3d_begin(&ed->view.r3, &ed->engine->renderer, &c, irect(ed->view.rect));
}

bool ed_view3d_project(const editor *ed, br_vec3 p, br_vec2 *screen) {
    br_vec3 s;
    if (!br_r3d_project(&ed->view.r3, p, &s)) return false;
    *screen = br_v2(s.x, s.y);
    return true;
}

static void mouse_ray(const editor *ed, br_vec2 m, br_vec3 *o, br_vec3 *d) { br_r3d_ray(&ed->view.r3, m, o, d); }

/* Where a ray meets the plane y = h (false: it doesn't, or behind). */
static bool ray_plane_y(br_vec3 o, br_vec3 d, float h, br_vec3 *hit) {
    float t;
    if (fabsf(d.y) < 1e-6f) return false;
    t = (h - o.y) / d.y;
    if (t < 0) return false;
    *hit = br_v3_add_scaled(o, d, t);
    return true;
}

/* World units that look `px` pixels long at point p. */
static float world_per_px(const editor *ed, br_vec3 p, float px) {
    const br_camera3d *c = &ed->view.r3.camera;
    float h = (float)ed->view.r3.viewport.h;
    float d = br_v3_dist(c->position, p);
    if (h < 1) h = 1;
    if (c->orthographic) return px * 2 * c->size / h;
    return px * d * 2 * tanf(c->fov * 0.5f * BR_DEG2RAD) / h;
}

static br_vec3 selected_pos(const editor *ed) {
    br_vec3 p = br_v3(0, 0, 0);
    br_world_transform3d(&ed->doc.world, ed->doc.selected, &p, NULL, NULL);
    return p;
}
static br_transform3d *selected_t3(editor *ed) {
    return (br_transform3d *)br_get(&ed->doc.world, ed->doc.selected, ed->doc.ids3.transform3d);
}

/* The gizmo's axes: the world's for moving and turning, the entity's own for scaling. */
static void gizmo_axes(const editor *ed, br_vec3 out[3]) {
    int i;
    if (ed->view.gizmo == ED_GIZMO_SCALE) {
        br_quat q;
        br_world_transform3d(&ed->doc.world, ed->doc.selected, NULL, &q, NULL);
        for (i = 0; i < 3; ++i) out[i] = br_quat_rotate(q, k_axis[i]);
    } else {
        for (i = 0; i < 3; ++i) out[i] = k_axis[i];
    }
}

/* ---- markers and picking ------------------------------------------------------------ */

static bool has_mesh(const ed_doc *d, br_entity e) {
    const br_mesh3d *m = (const br_mesh3d *)br_get(&d->world, e, d->ids3.mesh);
    return m && m->visible && br_mesh3d_mesh(m);
}

br_entity ed_view3d_pick(const editor *ed, br_vec2 screen) {
    const ed_doc *d = &ed->doc;
    br_vec3 o, dir, n;
    br_entity hit, best = BR_ENTITY_NULL;
    float t, best_px = MARKER_REACH;
    uint32_t i;
    for (i = 0; i < d->count; ++i) {                 /* cameras, lights and empties: by their marker */
        br_entity e = d->order[i];
        br_vec3 p;
        br_vec2 s;
        if (!br_has(&d->world, e, d->ids3.transform3d) || has_mesh(d, e)) continue;
        br_world_transform3d(&d->world, e, &p, NULL, NULL);
        if (ed_view3d_project(ed, p, &s) && br_v2_dist(s, screen) < best_px) { best_px = br_v2_dist(s, screen); best = e; }
    }
    if (best != BR_ENTITY_NULL) return best;
    mouse_ray(ed, screen, &o, &dir);
    hit = br_pick3d(&d->world, o, dir, &t, &n);
    return hit;
}

static void box_lines(br_r3d *r3, br_mat4 m, br_vec3 lo, br_vec3 hi, br_color c, bool depth) {
    br_vec3 p[8];
    int i;
    static const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
    for (i = 0; i < 8; ++i) p[i] = br_m4_mul_point(m, br_v3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
    for (i = 0; i < 12; ++i) br_r3d_line(r3, p[edges[i][0]], p[edges[i][1]], c, depth);
}

/* An outline around an entity: its mesh's box, or a small cube for a marker. */
static void outline(editor *ed, br_entity e, br_color c) {
    const ed_doc *d = &ed->doc;
    br_mat4 m = br_world_matrix3d(&d->world, e);
    if (has_mesh(d, e)) {
        const br_mesh *mesh = br_mesh3d_mesh((const br_mesh3d *)br_get(&d->world, e, d->ids3.mesh));
        box_lines(&ed->view.r3, m, mesh->min, mesh->max, c, false);
    } else {
        br_vec3 p;
        float s;
        br_world_transform3d(&d->world, e, &p, NULL, NULL);
        s = world_per_px(ed, p, 8);
        box_lines(&ed->view.r3, br_m4_trs(p, br_quat_identity(), br_v3(1, 1, 1)), br_v3(-s, -s, -s), br_v3(s, s, s), c, false);
    }
}

static void draw_markers(editor *ed) {
    const ed_doc *d = &ed->doc;
    br_r3d *r3 = &ed->view.r3;
    uint32_t i;
    for (i = 0; i < d->count; ++i) {
        br_entity e = d->order[i];
        br_vec3 p, f, u, rt;
        const br_camera3d_comp *cam;
        const br_light *light;
        if (!br_has(&d->world, e, d->ids3.transform3d)) continue;
        br_world_transform3d(&d->world, e, &p, NULL, NULL);
        f = br_forward3d(&d->world, e);
        u = br_up3d(&d->world, e);
        rt = br_right3d(&d->world, e);
        if ((cam = (const br_camera3d_comp *)br_get(&d->world, e, d->ids3.camera)) != NULL) {   /* a little frustum */
            br_color c = cam->active ? br_rgb(235, 235, 245) : br_rgb(130, 130, 140);
            float s = world_per_px(ed, p, 14), h = s * 0.55f, w = h * 1.6f;
            br_vec3 tip = br_v3_add_scaled(p, f, s * 1.6f), k[4];
            int j;
            for (j = 0; j < 4; ++j) k[j] = br_v3_add(tip, br_v3_add(br_v3_scale(rt, (j & 1) ? w : -w), br_v3_scale(u, (j & 2) ? h : -h)));
            for (j = 0; j < 4; ++j) br_r3d_line(r3, p, k[j], c, true);
            br_r3d_line(r3, k[0], k[1], c, true); br_r3d_line(r3, k[1], k[3], c, true);
            br_r3d_line(r3, k[3], k[2], c, true); br_r3d_line(r3, k[2], k[0], c, true);
            br_r3d_line(r3, br_v3_add_scaled(tip, u, h * 1.3f), br_v3_add_scaled(br_v3_add_scaled(tip, u, h * 1.9f), rt, w * 0.4f), c, true);   /* "up" */
        } else if ((light = (const br_light *)br_get(&d->world, e, d->ids3.light)) != NULL) {
            br_color c = br_rgb(255, 215, 90);
            float s = world_per_px(ed, p, 7);
            int j;
            for (j = 0; j < 3; ++j) br_r3d_line(r3, br_v3_add_scaled(p, k_axis[j], -s), br_v3_add_scaled(p, k_axis[j], s), c, true);
            if (light->kind == BR_LIGHT_DIRECTIONAL) {                   /* which way it shines */
                br_vec3 end = br_v3_add_scaled(p, f, s * 5);
                br_r3d_line(r3, p, end, c, true);
                br_r3d_line(r3, end, br_v3_add(br_v3_add_scaled(end, f, -s * 1.2f), br_v3_scale(rt, s * 0.7f)), c, true);
                br_r3d_line(r3, end, br_v3_add(br_v3_add_scaled(end, f, -s * 1.2f), br_v3_scale(rt, -s * 0.7f)), c, true);
            } else if (light->kind == BR_LIGHT_POINT && d->selected == e) {   /* its reach, as three rings */
                int a, k;
                for (a = 0; a < 3; ++a)
                    for (k = 0; k < 48; ++k) {
                        float t0 = (float)k / 48 * 2 * BR_PI, t1 = (float)(k + 1) / 48 * 2 * BR_PI;
                        br_vec3 x = k_axis[(a + 1) % 3], y = k_axis[(a + 2) % 3];
                        br_r3d_line(r3, br_v3_add(p, br_v3_scale(br_v3_add(br_v3_scale(x, cosf(t0)), br_v3_scale(y, sinf(t0))), light->range)),
                                        br_v3_add(p, br_v3_scale(br_v3_add(br_v3_scale(x, cosf(t1)), br_v3_scale(y, sinf(t1))), light->range)),
                                    br_rgba(255, 215, 90, 70), true);
                    }
            }
        } else if (!has_mesh(d, e)) {                                     /* an empty: its axes */
            float s = world_per_px(ed, p, 8);
            br_r3d_line(r3, p, br_v3_add_scaled(p, rt, s), k_axis_color[0], true);
            br_r3d_line(r3, p, br_v3_add_scaled(p, u, s), k_axis_color[1], true);
            br_r3d_line(r3, p, br_v3_add_scaled(p, f, -s), k_axis_color[2], true);
        }
    }
}

/* The ground: lines every unit (every ten, far away), the x axis red and z blue. */
static void draw_grid(editor *ed) {
    br_r3d *r3 = &ed->view.r3;
    const br_camera3d *c = &r3->camera;
    float step = 1, reach;
    int n, i;
    float cx, cz, h = fabsf(c->position.y);
    while (h / step > 25) step *= 10;
    reach = step * 30;
    cx = snapped(ed->view.orbit_target.x, step * 10);
    cz = snapped(ed->view.orbit_target.z, step * 10);
    n = 30;
    for (i = -n; i <= n; ++i) {
        float x = cx + (float)i * step, z = cz + (float)i * step;
        bool major = fmodf(fabsf(roundf(x / step)), 10) == 0, majz = fmodf(fabsf(roundf(z / step)), 10) == 0;
        br_color a = fabsf(x) < 1e-4f ? br_rgba(80, 140, 240, 200) : major ? br_rgba(255, 255, 255, 60) : br_rgba(255, 255, 255, 26);
        br_color b = fabsf(z) < 1e-4f ? br_rgba(230, 80, 80, 200) : majz ? br_rgba(255, 255, 255, 60) : br_rgba(255, 255, 255, 26);
        br_r3d_line(r3, br_v3(x, 0, cz - reach), br_v3(x, 0, cz + reach), a, true);     /* (x = 0 is the z axis: blue) */
        br_r3d_line(r3, br_v3(cx - reach, 0, z), br_v3(cx + reach, 0, z), b, true);
    }
}

/* ---- the gizmo ----------------------------------------------------------------------- */

typedef struct gizmo { bool ok; br_vec3 at, axis[3]; float len; br_vec2 c, tip[3]; bool tip_ok[3]; } gizmo;

static gizmo make_gizmo(const editor *ed) {
    gizmo g;
    int i;
    memset(&g, 0, sizeof(g));
    if (!br_has(&ed->doc.world, ed->doc.selected, ed->doc.ids3.transform3d)) return g;
    g.at = selected_pos(ed);
    if (!ed_view3d_project(ed, g.at, &g.c)) return g;
    g.len = world_per_px(ed, g.at, GIZMO_PX);
    gizmo_axes(ed, g.axis);
    for (i = 0; i < 3; ++i) g.tip_ok[i] = ed_view3d_project(ed, br_v3_add_scaled(g.at, g.axis[i], g.len), &g.tip[i]);
    g.ok = true;
    return g;
}

static float seg_dist(br_vec2 p, br_vec2 a, br_vec2 b) {
    br_vec2 ab = br_v2_sub(b, a);
    float l2 = ab.x * ab.x + ab.y * ab.y, t = l2 > 0 ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2 : 0;
    t = br_clampf(t, 0, 1);
    return br_v2_dist(p, br_v2_add(a, br_v2_scale(ab, t)));
}

/* A ring around axis i (for turning), as points on screen. */
static int ring_points(const editor *ed, const gizmo *g, int i, br_vec2 *out, bool *front, int n) {
    br_vec3 x = g->axis[(i + 1) % 3], y = g->axis[(i + 2) % 3], to_cam = br_v3_norm(br_v3_sub(ed->view.r3.camera.position, g->at));
    int k, m = 0;
    for (k = 0; k < n; ++k) {
        float t = (float)k / (float)n * 2 * BR_PI;
        br_vec3 dir = br_v3_add(br_v3_scale(x, cosf(t)), br_v3_scale(y, sinf(t)));
        br_vec2 s;
        if (!ed_view3d_project(ed, br_v3_add_scaled(g->at, dir, g->len * 0.9f), &s)) continue;
        out[m] = s;
        front[m] = br_v3_dot(dir, to_cam) > -0.15f;
        ++m;
    }
    return m;
}

/* Which handle is under the mouse: 0..2 an axis, 3 the centre, -1 none. */
static int hit_gizmo(const editor *ed, const gizmo *g, br_vec2 m) {
    int i, best = -1;
    float best_d = REACH;
    if (!g->ok) return -1;
    if (ed->view.gizmo != ED_GIZMO_ROTATE && br_v2_dist(m, g->c) < REACH + 2) return 3;
    for (i = 0; i < 3; ++i) {
        float dd;
        if (ed->view.gizmo == ED_GIZMO_ROTATE) {
            br_vec2 pts[64];
            bool front[64];
            int n = ring_points(ed, g, i, pts, front, 64), k;
            for (k = 0; k < n; ++k)
                if (front[k] && front[(k + 1) % n] && (dd = seg_dist(m, pts[k], pts[(k + 1) % n])) < best_d) { best_d = dd; best = i; }
        } else if (g->tip_ok[i] && (dd = seg_dist(m, g->c, g->tip[i])) < best_d && br_v2_dist(g->c, g->tip[i]) > 6) {
            best_d = dd;
            best = i;
        }
    }
    return best;
}

static void draw_gizmo(editor *ed, const gizmo *g) {
    br_renderer *r = &ed->engine->renderer;
    int i;
    if (!g->ok) return;
    for (i = 0; i < 3; ++i) {
        bool hot = ed->view.hot_axis == i || (ed->view.drag != ED_DRAG_NONE && ed->view.drag_axis == i);
        br_color c = hot ? br_rgb(255, 240, 120) : k_axis_color[i];
        if (ed->view.gizmo == ED_GIZMO_ROTATE) {
            br_vec2 pts[64];
            bool front[64];
            int n = ring_points(ed, g, i, pts, front, 64), k;
            for (k = 0; k < n; ++k)
                if (front[k] || front[(k + 1) % n]) br_draw_line(r, pts[k], pts[(k + 1) % n], front[k] && front[(k + 1) % n] ? c : br_rgba(c.r, c.g, c.b, 70));
        } else if (g->tip_ok[i]) {
            br_vec2 d = br_v2_sub(g->tip[i], g->c), side;
            float l = br_v2_len(d);
            if (l < 2) continue;
            d = br_v2_scale(d, 1 / l);
            side = br_v2(-d.y, d.x);
            br_draw_line(r, g->c, g->tip[i], c);
            br_draw_line(r, br_v2_add(g->c, side), br_v2_add(g->tip[i], side), c);
            if (ed->view.gizmo == ED_GIZMO_MOVE)
                br_fill_triangle(r, br_v2_add(g->tip[i], br_v2_scale(d, 9)), br_v2_add(g->tip[i], br_v2_scale(side, 4)), br_v2_sub(g->tip[i], br_v2_scale(side, 4)), c);
            else
                br_fill_rect(r, br_rect_make(floorf(g->tip[i].x) - 3, floorf(g->tip[i].y) - 3, 7, 7), c);
        }
    }
    if (ed->view.gizmo != ED_GIZMO_ROTATE) {
        bool hot = ed->view.hot_axis == 3 || (ed->view.drag != ED_DRAG_NONE && ed->view.drag_axis == 3);
        br_draw_rect(r, br_rect_make(floorf(g->c.x) - 4, floorf(g->c.y) - 4, 9, 9), hot ? br_rgb(255, 240, 120) : BR_WHITE);
    }
}

/* Where along the line at + axis * s the mouse ray passes closest (false: they're parallel). */
static bool axis_param(const editor *ed, br_vec3 at, br_vec3 axis, br_vec2 m, float *s) {
    br_vec3 o, d, w0;
    float b, dd, e, denom;
    mouse_ray(ed, m, &o, &d);
    w0 = br_v3_sub(at, o);
    b = br_v3_dot(axis, d);
    dd = br_v3_dot(axis, w0);
    e = br_v3_dot(d, w0);
    denom = 1 - b * b;
    if (denom < 1e-4f) return false;
    *s = (b * e - dd) / denom;
    return true;
}

static float screen_angle(br_vec2 c, br_vec2 m) { return atan2f(m.y - c.y, m.x - c.x) * BR_RAD2DEG; }

static void start_drag(editor *ed, const gizmo *g, int axis, br_vec2 m) {
    ed_view *v = &ed->view;
    br_transform3d *t = selected_t3(ed);
    if (!t) return;
    v->drag_axis = axis;
    v->drag_start3 = *t;
    v->drag_from3 = g->at;
    br_world_transform3d(&ed->doc.world, ed->doc.selected, NULL, &v->drag_rot3, NULL);
    ed_view_grab_group(ed);
    v->press_at = m;
    if (v->gizmo == ED_GIZMO_MOVE) {
        v->drag = ED_DRAG_MOVE3;
        if (axis == 3) {
            br_vec3 o, d;
            mouse_ray(ed, m, &o, &d);
            if (!ray_plane_y(o, d, g->at.y, &v->drag_hit3)) v->drag_hit3 = g->at;
        } else if (!axis_param(ed, g->at, g->axis[axis], m, &v->drag_param)) {
            v->drag = ED_DRAG_NONE;
        }
    } else if (v->gizmo == ED_GIZMO_ROTATE) {
        v->drag = ED_DRAG_ROTATE3;
        v->drag_param = screen_angle(g->c, m);
    } else {
        v->drag = ED_DRAG_SCALE3;
    }
}

static void update_drag(editor *ed, br_vec2 m) {
    ed_view *v = &ed->view;
    br_world *w = &ed->doc.world;
    br_entity e = ed->doc.selected;
    br_transform3d *t = selected_t3(ed);
    gizmo g = make_gizmo(ed);
    if (!t || !g.ok) { v->drag = ED_DRAG_NONE; return; }
    if (v->drag == ED_DRAG_MOVE3) {
        br_vec3 to = v->drag_from3;
        if (v->drag_axis == 3) {
            br_vec3 o, d, hit;
            mouse_ray(ed, m, &o, &d);
            if (!ray_plane_y(o, d, v->drag_from3.y, &hit)) return;
            to = br_v3_add(v->drag_from3, br_v3_sub(hit, v->drag_hit3));
            if (v->snap) { to.x = snapped(to.x, MOVE_SNAP); to.z = snapped(to.z, MOVE_SNAP); }
        } else {
            float s;
            if (!axis_param(ed, v->drag_from3, g.axis[v->drag_axis], m, &s)) return;
            to = br_v3_add_scaled(v->drag_from3, g.axis[v->drag_axis], s - v->drag_param);
            if (v->snap) {
                float *c = v->drag_axis == 0 ? &to.x : v->drag_axis == 1 ? &to.y : &to.z;
                *c = snapped(*c, MOVE_SNAP);
            }
        }
        {   /* every selected root by as much */
            br_vec3 delta = br_v3_sub(to, v->drag_from3);
            uint32_t i;
            for (i = 0; i < v->group_count; ++i)
                if (br_has(w, v->group[i], ed->doc.ids3.transform3d)) br_set_world_position3d(w, v->group[i], br_v3_add(v->group_from3[i], delta));
        }
    } else if (v->drag == ED_DRAG_ROTATE3) {
        br_vec3 axis = g.axis[v->drag_axis], to_cam = br_v3_sub(v->r3.camera.position, g.at);
        float delta = screen_angle(g.c, m) - v->drag_param;
        /* on screen (y down) a clockwise drag; around an axis pointing at you, that's a negative turn */
        if (br_v3_dot(axis, to_cam) > 0) delta = -delta;
        delta = fmodf(delta + 540.0f, 360.0f) - 180.0f;
        if (v->snap) delta = snapped(delta, TURN_SNAP);
        {   /* each selected root turns, and swings round the primary one */
            br_quat q = br_quat_axis_angle(axis, delta * BR_DEG2RAD);
            uint32_t i;
            for (i = 0; i < v->group_count; ++i) {
                br_entity o = v->group[i];
                br_transform3d *ot;
                if (!br_has(w, o, ed->doc.ids3.transform3d)) continue;
                br_set_world_rotation3d(w, o, br_quat_mul(q, v->group_rot3[i]));
                if (o != e) br_set_world_position3d(w, o, br_v3_add(v->drag_from3, br_quat_rotate(q, br_v3_sub(v->group_from3[i], v->drag_from3))));
                ot = (br_transform3d *)br_get(w, o, ed->doc.ids3.transform3d);
                ot->rotation = br_v3(roundf(ot->rotation.x * 100) / 100, roundf(ot->rotation.y * 100) / 100, roundf(ot->rotation.z * 100) / 100);
            }
            if (!ed_doc_is_selected(&ed->doc, e) || v->group_count == 0) br_set_world_rotation3d(w, e, br_quat_mul(q, v->drag_rot3));
        }
    } else if (v->drag == ED_DRAG_SCALE3) {
        br_vec3 s = v->drag_start3.scale;
        float f;
        if (v->drag_axis == 3) {
            f = 1 + ((m.x - v->press_at.x) - (m.y - v->press_at.y)) / 100.0f;
            if (f < 0.01f) f = 0.01f;
            s = br_v3_scale(s, f);
            if (v->snap) s = br_v3(snapped(s.x, SCALE_SNAP), snapped(s.y, SCALE_SNAP), snapped(s.z, SCALE_SNAP));
        } else if (g.tip_ok[v->drag_axis]) {
            br_vec2 dir = br_v2_sub(g.tip[v->drag_axis], g.c);
            float l = br_v2_len(dir), *c;
            if (l < 2) return;
            f = 1 + br_v2_dot(br_v2_sub(m, v->press_at), br_v2_scale(dir, 1 / l)) / l;
            c = v->drag_axis == 0 ? &s.x : v->drag_axis == 1 ? &s.y : &s.z;
            *c *= f;
            if (v->snap) *c = snapped(*c, SCALE_SNAP);
        }
        if (fabsf(s.x) < 0.01f) s.x = 0.01f;
        if (fabsf(s.y) < 0.01f) s.y = 0.01f;
        if (fabsf(s.z) < 0.01f) s.z = 0.01f;
        {   /* the others by the same factors (each its own scale) */
            br_vec3 st = v->drag_start3.scale, k = br_v3(st.x != 0 ? s.x / st.x : 1, st.y != 0 ? s.y / st.y : 1, st.z != 0 ? s.z / st.z : 1);
            uint32_t i;
            for (i = 0; i < v->group_count; ++i) {
                br_transform3d *ot = (br_transform3d *)br_get(w, v->group[i], ed->doc.ids3.transform3d);
                if (ot && v->group[i] != e) ot->scale = br_v3_mul(v->group_scale3[i], k);
            }
        }
        t->scale = s;
    }
}

/* ---- the view's own buttons (top left) ----------------------------------------------- */

typedef struct vbutton { const char *label; br_rect r; bool on; } vbutton;

static int view_buttons(const editor *ed, vbutton *b) {
    float x = ed->view.rect.x + 6, y = ed->view.rect.y + 6;
    int i;
    static const char *labels[4] = { "Move W", "Rotate E", "Scale R", "Camera C" };
    for (i = 0; i < 4; ++i) {
        float w = br_measure_text(labels[i], 1).x + 10;
        if (i == 3) x += 6;
        b[i].label = labels[i];
        b[i].r = br_rect_make(x, y, w, 16);
        b[i].on = i < 3 ? (int)ed->view.gizmo == i : ed->view.through_camera;
        x += w + 2;
    }
    return 4;
}

static void press_button(editor *ed, int i) {
    if (i < 3) { ed->view.gizmo = (ed_gizmo)i; return; }
    if (!ed->view.through_camera && !br_scene_camera3d(&ed->doc.world, NULL, NULL)) {
        ed_status(ed, "There's no active Camera3D in this scene to look through (right click > New 3D > Camera)");
        return;
    }
    ed->view.through_camera = !ed->view.through_camera;
    ed_status(ed, ed->view.through_camera ? "Looking through the scene's camera (C again: the editor's camera)" : "The editor's camera");
}

/* ---- input --------------------------------------------------------------------------- */

static void handle_input(editor *ed) {
    ed_view *v = &ed->view;
    const br_input *in = &ed->engine->input;
    br_vec2 m = br_v2(in->mouse_x, in->mouse_y);
    bool alt = (in->mods & BR_MOD_ALT) != 0, shift = (in->mods & BR_MOD_SHIFT) != 0;
    bool keys = !br_ui_wants_keyboard(&ed->ui) && !ed_code_has_keyboard(ed) && !br_ui_menu_active(&ed->ui) && ed->pending == ED_PENDING_NONE &&
                !(in->mods & (BR_MOD_CONTROL | BR_MOD_SUPER)) && !ed->doc.playing && !ed->settings_visible && !ed->project_visible && !ed->picker.open;
    gizmo g;
    vbutton buttons[4];
    int nb = view_buttons(ed, buttons), i;
    bool on_button = false;

    v->mouse_inside = in_rect(v->rect, m) && !br_ui_wants_mouse(&ed->ui, m) && ed->pending == ED_PENDING_NONE;
    {
        br_vec3 o, d;
        mouse_ray(ed, m, &o, &d);
        v->mouse_on_ground = ray_plane_y(o, d, 0, &v->mouse_ground);
        if (!v->mouse_on_ground) v->mouse_ground = br_v3_add_scaled(o, d, v->orbit_dist);
    }
    if (keys) {
        if (br_key_pressed(in, BR_KEY_W)) v->gizmo = ED_GIZMO_MOVE;
        if (br_key_pressed(in, BR_KEY_E)) v->gizmo = ED_GIZMO_ROTATE;
        if (br_key_pressed(in, BR_KEY_R)) v->gizmo = ED_GIZMO_SCALE;
        if (br_key_pressed(in, BR_KEY_C)) press_button(ed, 3);
    }
    for (i = 0; i < nb; ++i) if (in_rect(buttons[i].r, m)) on_button = true;

    if (v->mouse_inside && in->wheel_y != 0 && v->drag == ED_DRAG_NONE)
        v->orbit_dist = br_clampf(v->orbit_dist * powf(0.87f, in->wheel_y), 0.2f, 5000.0f);

    g = make_gizmo(ed);
    if (v->drag == ED_DRAG_NONE && v->mouse_inside) {
        bool right = br_mouse_pressed(in, BR_MOUSE_RIGHT), middle = br_mouse_pressed(in, BR_MOUSE_MIDDLE), left = br_mouse_pressed(in, BR_MOUSE_LEFT);
        if (middle || (right && shift) || (left && br_key_down(in, BR_KEY_SPACE))) {
            v->drag = ED_DRAG_PAN;
            v->drag_mouse = v->press_at = m;
            v->pan_moved = false;
            v->pan_button = right ? BR_MOUSE_RIGHT : left ? BR_MOUSE_LEFT : BR_MOUSE_MIDDLE;
        } else if (right || (left && alt)) {
            v->drag = ED_DRAG_ORBIT;
            v->drag_mouse = v->press_at = m;
            v->pan_moved = false;
            v->pan_button = right ? BR_MOUSE_RIGHT : BR_MOUSE_LEFT;
        } else if (left && on_button) {
            for (i = 0; i < nb; ++i) if (in_rect(buttons[i].r, m)) press_button(ed, i);
        } else if (left) {
            int h = ed->doc.playing ? -1 : hit_gizmo(ed, &g, m);
            bool add = shift || (in->mods & (BR_MOD_CONTROL | BR_MOD_SUPER)) != 0;
            if (h >= 0 && !add) start_drag(ed, &g, h, m);
            else {
                br_entity hit = ed_view3d_pick(ed, m);
                if (hit == BR_ENTITY_NULL) {                     /* empty space: a box to select with */
                    if (!add) ed_doc_select(&ed->doc, BR_ENTITY_NULL);
                    v->drag = ED_DRAG_BOX;
                    v->press_at = m;
                    v->box_add = add;
                } else if (add) ed_doc_select_toggle(&ed->doc, hit);
                else if (ed_doc_is_selected(&ed->doc, hit)) ed_doc_select_add(&ed->doc, hit);   /* (keeps the others) */
                else ed_doc_select(&ed->doc, hit);
            }
        }
    }

    if (v->drag == ED_DRAG_PAN || v->drag == ED_DRAG_ORBIT) {
        if (br_mouse_down(in, v->pan_button)) {
            br_vec2 dm = br_v2_sub(m, v->drag_mouse);
            if (br_v2_dist(m, v->press_at) > 3) v->pan_moved = true;
            if (v->drag == ED_DRAG_ORBIT) {
                v->orbit_yaw = fmodf(v->orbit_yaw - dm.x * 0.4f, 360.0f);
                v->orbit_pitch = br_clampf(v->orbit_pitch + dm.y * 0.4f, -89.0f, 89.0f);
            } else {
                br_camera3d c = ed_view3d_camera(v);
                br_vec3 right = br_quat_rotate(c.rotation, br_v3(1, 0, 0)), up = br_quat_rotate(c.rotation, br_v3(0, 1, 0));
                float k = world_per_px(ed, v->orbit_target, 1);
                v->orbit_target = br_v3_add(v->orbit_target, br_v3_add(br_v3_scale(right, -dm.x * k), br_v3_scale(up, dm.y * k)));
            }
            v->drag_mouse = m;
        } else {
            if (v->pan_button == BR_MOUSE_RIGHT && !v->pan_moved) {   /* a click, not a drag: the menu */
                ed_menu_view(ed, br_v2(v->mouse_ground.x, v->mouse_ground.z));
                ed->menu.world3 = v->mouse_ground;
            }
            v->drag = ED_DRAG_NONE;
        }
    } else if (v->drag == ED_DRAG_BOX) {
        if (!br_mouse_down(in, BR_MOUSE_LEFT)) {                /* what's inside the box (by where it is) */
            br_rect box = br_rect_make(fminf(v->press_at.x, m.x), fminf(v->press_at.y, m.y), fabsf(m.x - v->press_at.x), fabsf(m.y - v->press_at.y));
            uint32_t k;
            v->drag = ED_DRAG_NONE;
            if (box.w > 2 || box.h > 2) {
                for (k = 0; k < ed->doc.count; ++k) {
                    br_entity e = ed->doc.order[k];
                    br_vec3 p;
                    br_vec2 sp;
                    if (!br_has(&ed->doc.world, e, ed->doc.ids3.transform3d)) continue;
                    br_world_transform3d(&ed->doc.world, e, &p, NULL, NULL);
                    if (ed_view3d_project(ed, p, &sp) && in_rect(box, sp)) ed_doc_select_add(&ed->doc, e);
                }
                if (ed_doc_selection_count(&ed->doc) > 1) ed_status(ed, "%u selected", (unsigned)ed_doc_selection_count(&ed->doc));
            }
        }
    } else if (v->drag != ED_DRAG_NONE) {
        if (!br_mouse_down(in, BR_MOUSE_LEFT) || ed->doc.playing) v->drag = ED_DRAG_NONE;
        else update_drag(ed, m);
    }
    v->hot_axis = v->drag == ED_DRAG_NONE && v->mouse_inside && !ed->doc.playing ? hit_gizmo(ed, &g, m) : -1;
    v->hover = v->drag == ED_DRAG_NONE && v->mouse_inside && v->hot_axis < 0 && !on_button ? ed_view3d_pick(ed, m) : BR_ENTITY_NULL;
}

/* ---- new things ----------------------------------------------------------------------- */

br_vec3 ed_view3d_place(const editor *ed) {
    br_vec3 p = ed->view.orbit_target;
    if (ed->view.snap) p = br_v3(snapped(p.x, MOVE_SNAP), snapped(p.y, MOVE_SNAP), snapped(p.z, MOVE_SNAP));
    return p;
}

br_entity ed_new3d_at(editor *ed, ed_new3d kind, br_vec3 at) {
    ed_doc *d = &ed->doc;
    br_entity e;
    if (ed->view.snap) at = br_v3(snapped(at.x, MOVE_SNAP), snapped(at.y, MOVE_SNAP), snapped(at.z, MOVE_SNAP));
    if (kind <= ED_NEW_CONE && kind != ED_NEW_PLANE && at.y < 0.5f) at.y = 0.5f;     /* standing on the ground */
    if ((kind == ED_NEW_CAPSULE || kind == ED_NEW_CHARACTER) && at.y < 1) at.y = 1;
    if ((e = ed_doc_create3d(d, ed_new3d_name(kind), at)) == BR_ENTITY_NULL) return e;
    if (kind <= ED_NEW_CONE) {
        static const int shapes[5] = { BR_MESH_CUBE, BR_MESH_SPHERE, BR_MESH_PLANE, BR_MESH_CYLINDER, BR_MESH_CONE };
        ((br_mesh3d *)br_add(&d->world, e, d->ids3.mesh, NULL))->shape = shapes[kind];
        if (kind == ED_NEW_PLANE) ((br_transform3d *)br_get(&d->world, e, d->ids3.transform3d))->scale = br_v3(10, 1, 10);
    } else if (kind == ED_NEW_CAPSULE || kind == ED_NEW_CHARACTER) {
        ((br_mesh3d *)br_add(&d->world, e, d->ids3.mesh, NULL))->shape = BR_MESH_CAPSULE;
        if (kind == ED_NEW_CHARACTER) {                   /* solid, the same shape: moved with move_and_slide() */
            br_collider3d *c = (br_collider3d *)br_add(&d->world, e, br_component_find(&d->world, "Collider3D"), NULL);
            br_mesh3d *m = (br_mesh3d *)br_get(&d->world, e, d->ids3.mesh);
            c->shape = BR_COLLIDER3D_CAPSULE;
            c->size = br_v3(1, 2, 1);
            c->radius = 0.5f;
            ((br_rigidbody3d *)br_add(&d->world, e, br_component_find(&d->world, "RigidBody3D"), NULL))->type = BR_BODY_KINEMATIC;   /* (sets off triggers) */
            m->shadow = true;
            m->color = br_rgb(240, 170, 90);
            ed_status(ed, "Added a character: move it from a script with move_and_slide(self, velocity)");
            return e;
        }
    } else if (kind == ED_NEW_PARTICLES) {                /* a fire, to start from (particles.h) */
        br_particles *p = (br_particles *)br_add(&d->world, e, br_component_find(&d->world, "Particles"), NULL);
        if (p) br_particles_preset(p, "fire", true);
        ed_status(ed, "Added particles: a fire. Its Particles fields change the look (or right click in 2D for other presets)");
        return e;
    } else if (kind == ED_NEW_CAMERA) {
        br_transform3d *t = (br_transform3d *)br_get(&d->world, e, d->ids3.transform3d);
        br_add(&d->world, e, d->ids3.camera, NULL);
        t->position = br_v3_add(at, br_v3(0, 3, 8));
        br_look_at3d(&d->world, e, at);
    } else if (kind == ED_NEW_LIGHT) {
        br_transform3d *t = (br_transform3d *)br_get(&d->world, e, d->ids3.transform3d);
        br_add(&d->world, e, d->ids3.light, NULL);
        t->position = br_v3_add(at, br_v3(0, 4, 0));
        t->rotation = br_v3(-50, 30, 0);                  /* a sun, shining down at an angle */
    }
    ed_status(ed, "Added %s", ed_new3d_name(kind));
    return e;
}

/* ---- drawing ---------------------------------------------------------------------------- */

static void draw_axes_widget(editor *ed) {
    br_renderer *r = &ed->engine->renderer;
    br_vec2 c = br_v2(ed->view.rect.x + 34, ed->view.rect.y + ed->view.rect.h - 34);
    br_mat4 view = ed->view.r3.view;
    static const char *names[3] = { "X", "Y", "Z" };
    int i;
    br_fill_circle(r, c, 26, br_rgba(0, 0, 0, 70));
    for (i = 0; i < 3; ++i) {
        br_vec3 d = br_m4_mul_dir(view, k_axis[i]);
        br_vec2 tip = br_v2_add(c, br_v2(d.x * 20, -d.y * 20));
        br_draw_line(r, c, tip, k_axis_color[i]);
        br_draw_text(r, names[i], tip.x - 3 + d.x * 5, tip.y - 4 - d.y * 5, 1, k_axis_color[i]);
    }
}

static void accept_drops(editor *ed) {
    ed_view *v = &ed->view;
    const char *rel = br_ui_dragging(&ed->ui, ED_ASSET_DRAG), *dropped;
    if (!rel || !in_rect(v->rect, ed->ui.mouse) || ed->doc.playing) return;
    if ((dropped = br_ui_drop_target(&ed->ui, v->rect, ED_ASSET_DRAG)) != NULL) {
        char path[ED_PATH_MAX];
        snprintf(path, sizeof(path), "%s", dropped);
        ed_assets_drop3d(ed, path, v->mouse_ground, ed_view3d_pick(ed, ed->ui.mouse));
    } else {                                                   /* where it would land */
        br_vec3 p = v->mouse_ground;
        float s = world_per_px(ed, p, 10);
        br_r3d_line(&v->r3, br_v3(p.x - s, p.y, p.z), br_v3(p.x + s, p.y, p.z), ed->ui.style.accent, false);
        br_r3d_line(&v->r3, br_v3(p.x, p.y, p.z - s), br_v3(p.x, p.y, p.z + s), ed->ui.style.accent, false);
        br_r3d_line(&v->r3, p, br_v3(p.x, p.y + s * 2, p.z), ed->ui.style.accent, false);
    }
}

void ed_view3d_frame(editor *ed, br_rect rect) {
    ed_view *v = &ed->view;
    br_renderer *r = &ed->engine->renderer;
    br_recti clip = irect(rect);
    bool through = false;
    br_camera3d cam;
    char text[96];
    gizmo g;
    v->rect = rect;
    if (v->orbit_dist <= 0) ed_view3d_reset(v);
    begin(ed);                                    /* (rays for the input, with this frame's view) */
    if (!ed->dbg.stopped) handle_input(ed);

    br_set_clip(r, clip);
    cam = view_camera(ed, &through);
    br_fill_rect(r, rect, br_rgb(36, 40, 48));
    if (through) {                                /* as the game sees it: the camera's sky too */
        br_draw3d(&ed->doc.world, &v->r3, r, NULL, clip);
    } else {
        br_r3d_begin(&v->r3, r, &cam, clip);
        br_draw3d(&ed->doc.world, &v->r3, r, &cam, clip);
    }
    if (v->grid && !through) draw_grid(ed);
    if (!through || !ed->doc.playing) draw_markers(ed);
    if (ed->show_colliders) br_physics3d_debug_draw(&ed->doc.world, &v->r3, br_rgba(90, 255, 140, 200), br_rgba(255, 220, 80, 200));
    if (v->hover != BR_ENTITY_NULL && !ed_doc_is_selected(&ed->doc, v->hover)) outline(ed, v->hover, br_rgba(255, 255, 255, 110));
    if (br_has(&ed->doc.world, ed->doc.selected, ed->doc.ids3.transform3d)) outline(ed, ed->doc.selected, ed->ui.style.accent);
    if (ed->doc.multi_count > 1 && ed->doc.selected == ed->doc.multi_primary) {   /* the rest of a multiple selection */
        uint32_t k;
        br_color c = ed->ui.style.accent;
        c.a = 170;
        for (k = 0; k < ed->doc.multi_count; ++k)
            if (ed->doc.multi[k] != ed->doc.selected && br_has(&ed->doc.world, ed->doc.multi[k], ed->doc.ids3.transform3d)) outline(ed, ed->doc.multi[k], c);
    }
    if (!ed->dbg.stopped) accept_drops(ed);
    g = make_gizmo(ed);
    if (!ed->doc.playing) draw_gizmo(ed, &g);
    draw_axes_widget(ed);
    if (v->drag == ED_DRAG_BOX) {                                  /* the box being drawn */
        br_vec2 mm = br_v2(ed->engine->input.mouse_x, ed->engine->input.mouse_y);
        br_rect box = br_rect_make(fminf(v->press_at.x, mm.x), fminf(v->press_at.y, mm.y), fabsf(mm.x - v->press_at.x), fabsf(mm.y - v->press_at.y));
        br_color c = ed->ui.style.accent;
        c.a = 40;
        br_fill_rect(r, box, c);
        br_draw_rect(r, box, ed->ui.style.accent);
    }
    {
        vbutton b[4];
        int n = view_buttons(ed, b), i;
        for (i = 0; i < n; ++i) {
            bool hot = in_rect(b[i].r, br_v2(ed->engine->input.mouse_x, ed->engine->input.mouse_y));
            br_fill_rect(r, b[i].r, b[i].on ? ed->ui.style.accent : hot ? br_rgba(80, 86, 100, 230) : br_rgba(20, 22, 28, 200));
            br_draw_text(r, b[i].label, b[i].r.x + 5, b[i].r.y + 4, 1, b[i].on ? BR_WHITE : br_rgb(210, 214, 224));
        }
    }
    snprintf(text, sizeof(text), "%s  %u triangles", through ? "scene camera" : "3D", v->r3.stats.triangles);
    br_draw_text(r, text, rect.x + rect.w - br_measure_text(text, 1).x - 8, rect.y + 8, 1, br_rgba(255, 255, 255, 120));
    if (ed->doc.playing) {
        br_draw_rect(r, rect, br_rgb(90, 220, 110));
        br_draw_rect(r, br_rect_make(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2), br_rgb(90, 220, 110));
        br_fill_rect(r, br_rect_make(rect.x + 6, rect.y + 26, 62, 14), br_rgb(90, 220, 110));
        br_draw_text(r, "PLAYING", rect.x + 9, rect.y + 29, 1, br_rgb(20, 40, 20));
    }
    br_reset_clip(r);
}

/* ---- switching, framing ------------------------------------------------------------------- */

void ed_view_set_3d(editor *ed, bool on) {
    ed->view.three_d = on;
    ed->view.drag = ED_DRAG_NONE;
    if (on && ed->view.orbit_dist <= 0) ed_view3d_reset(&ed->view);
}

void ed_view3d_focus(editor *ed) {
    ed_view *v = &ed->view;
    const ed_doc *d = &ed->doc;
    if (br_has(&d->world, d->selected, d->ids3.transform3d)) {
        float size = 1;
        v->orbit_target = selected_pos(ed);
        if (has_mesh(d, d->selected)) {
            const br_mesh *m = br_mesh3d_mesh((const br_mesh3d *)br_get(&d->world, d->selected, d->ids3.mesh));
            br_vec3 s;
            br_world_transform3d(&d->world, d->selected, NULL, NULL, &s);
            size = br_v3_len(br_v3_mul(br_v3_sub(m->max, m->min), br_v3(fabsf(s.x), fabsf(s.y), fabsf(s.z))));
        }
        v->orbit_dist = br_clampf(size * 2.2f, 2, 4000);
    } else {
        ed_view3d_reset(v);
    }
}
