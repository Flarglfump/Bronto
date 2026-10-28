/* Tests: 3D - math, meshes and the software rasterizer (render3d), the 3D
 * components (components3d), 3D physics (physics3d) and 3D in scripts. */
#include "test.h"
#include <stdlib.h>
#include "gltf_fixture.h"
#include <string.h>

/* ---- math ------------------------------------------------------------------------- */

static void test_euler_and_quaternions(void) {
    static const float angles[][3] = { { 0, 0, 0 }, { 30, 45, 60 }, { -20, 170, 5 }, { 80, -60, -120 }, { 10, 0, 0 }, { 0, -90, 0 } };
    int i;
    for (i = 0; i < (int)(sizeof(angles) / sizeof(angles[0])); ++i) {
        br_vec3 e = br_v3(angles[i][0], angles[i][1], angles[i][2]);
        br_quat q = br_quat_from_euler(e);
        br_vec3 back = br_quat_to_euler(q);
        br_quat q2 = br_quat_from_euler(back);
        br_vec3 v = br_v3(0.3f, -1.2f, 2.0f), a = br_quat_rotate(q, v), b = br_quat_rotate(q2, v);
        CHECK(br_v3_dist(a, b) < 1e-4f);                        /* the same turn (angles may differ by 360) */
        CHECK(br_v3_dist(a, br_m4_mul_dir(br_quat_to_m4(q), v)) < 1e-4f);
    }
    {   /* yaw 90: forward (-Z) turns to -X; pitch 90: forward turns up */
        br_vec3 f = br_quat_rotate(br_quat_from_euler(br_v3(0, 90, 0)), br_v3(0, 0, -1));
        CHECK(br_v3_dist(f, br_v3(-1, 0, 0)) < 1e-5f);
        f = br_quat_rotate(br_quat_from_euler(br_v3(90, 0, 0)), br_v3(0, 0, -1));
        CHECK(br_v3_dist(f, br_v3(0, 1, 0)) < 1e-5f);
    }
    {   /* looking at a point; TRS and back */
        br_quat q = br_quat_look(br_v3(1, 0, -1), br_v3(0, 1, 0));
        br_vec3 t, s;
        br_quat r;
        br_mat4 m;
        CHECK(br_v3_dist(br_quat_rotate(q, br_v3(0, 0, -1)), br_v3_norm(br_v3(1, 0, -1))) < 1e-5f);
        CHECK(br_v3_dist(br_quat_rotate(q, br_v3(0, 1, 0)), br_v3(0, 1, 0)) < 1e-5f);
        m = br_m4_trs(br_v3(1, 2, 3), q, br_v3(2, 3, 4));
        br_m4_decompose(m, &t, &r, &s);
        CHECK(br_v3_dist(t, br_v3(1, 2, 3)) < 1e-5f && br_v3_dist(s, br_v3(2, 3, 4)) < 1e-4f);
        CHECK(br_v3_dist(br_quat_rotate(r, br_v3(0, 0, -1)), br_quat_rotate(q, br_v3(0, 0, -1))) < 1e-4f);
        CHECK(br_v3_dist(br_m4_mul_point(m, br_v3(0, 0, -1)), br_v3_add(br_v3(1, 2, 3), br_quat_rotate(q, br_v3(0, 0, -4)))) < 1e-4f);
    }
}

/* ---- meshes ---------------------------------------------------------------------- */

static void test_meshes(void) {
    br_mesh m;
    float t;
    br_vec3 n;
    char err[128];
    uint32_t i;
    CHECK(br_mesh_cube(&m));
    CHECK(m.vert_count == 24 && m.index_count == 36);
    CHECK(br_v3_dist(m.min, br_v3(-0.5f, -0.5f, -0.5f)) < 1e-6f && br_v3_dist(m.max, br_v3(0.5f, 0.5f, 0.5f)) < 1e-6f);
    for (i = 0; i < m.index_count; i += 3) {               /* every face wound counter-clockwise, outward */
        br_vec3 a = m.verts[m.indices[i]].pos, b = m.verts[m.indices[i + 1]].pos, c = m.verts[m.indices[i + 2]].pos;
        CHECK(br_v3_dot(br_v3_cross(br_v3_sub(b, a), br_v3_sub(c, a)), m.verts[m.indices[i]].normal) > 0);
    }
    CHECK(br_mesh_raycast(&m, br_v3(0, 0, 5), br_v3(0, 0, -1), &t, &n));
    CHECK_NEAR(t, 4.5, 1e-5);
    CHECK(br_v3_dist(n, br_v3(0, 0, 1)) < 1e-5f);
    CHECK(!br_mesh_raycast(&m, br_v3(2, 0, 5), br_v3(0, 0, -1), &t, &n));
    br_mesh_destroy(&m);
    CHECK(br_mesh_sphere(&m, 16, 8));
    CHECK_NEAR(m.max.y, 0.5, 1e-5);
    CHECK_NEAR(m.min.y, -0.5, 1e-5);
    CHECK(br_mesh_raycast(&m, br_v3(0.01f, 3, 0.02f), br_v3(0, -1, 0), &t, &n) && fabsf(t - 2.5f) < 0.02f);
    br_mesh_destroy(&m);
    CHECK(br_mesh_plane(&m) && m.index_count == 6 && m.max.y == 0);
    br_mesh_destroy(&m);
    CHECK(br_mesh_cylinder(&m, 12));
    CHECK_NEAR(m.max.y, 0.5, 1e-6);
    br_mesh_destroy(&m);
    CHECK(br_mesh_cone(&m, 12));
    CHECK_NEAR(m.min.y, -0.5, 1e-6);
    br_mesh_destroy(&m);
    {   /* .obj: a square from two triangles' worth, with a negative index and no normals; then a broken one */
        const char *obj = "# a square\nv -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nvt 0 0\nvt 1 1\nf 4/1 3 2 -4/2\ng ignored\nusemtl x\n";
        CHECK(br_mesh_load_obj_mem(&m, obj, strlen(obj), err, sizeof(err)));
        CHECK(m.index_count == 6 && m.vert_count == 4);
        CHECK(br_v3_dist(m.verts[0].normal, br_v3(0, 1, 0)) < 1e-5f);    /* (its own flat normal: facing up) */
        CHECK(m.verts[3].uv.x == 1 && m.verts[0].uv.x == 0);
        br_mesh_destroy(&m);
        CHECK(!br_mesh_load_obj_mem(&m, "v 0 0 0\nf 1 2 3\n", 16, err, sizeof(err)) && strstr(err, "line 2"));
        CHECK(!br_mesh_load_obj_mem(&m, "v 0 0 0\n", 8, err, sizeof(err)) && strstr(err, "no faces"));
    }
}

/* ---- the rasterizer ----------------------------------------------------------------- */

static br_color px(const br_image *img, int x, int y) { return br_image_get(img, x, y); }

static void test_render3d(void) {
    br_image img;
    br_renderer r;
    br_r3d r3;
    br_mesh cube;
    br_camera3d cam = br_camera3d_make(br_v3(0, 0, 5), br_v3(0, 0, 0));
    br_material red = br_material_make(br_rgb(255, 0, 0)), blue = br_material_make(br_rgb(0, 0, 255));
    br_light3d sun;
    uint32_t i, hash1 = 0, hash2 = 0;
    br_image_init(&img, 160, 90);
    br_renderer_init(&r, &img);
    br_r3d_init(&r3);
    br_mesh_cube(&cube);
    red.lit = blue.lit = false;
    /* a near red cube in front of a far blue one: red in the middle, blue around it, sky elsewhere */
    br_image_fill(&img, br_rgb(10, 10, 10));
    CHECK(br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90)));
    br_r3d_draw_mesh(&r3, &cube, br_m4_trs(br_v3(0, 0, -3), br_quat_identity(), br_v3(4, 4, 1)), &blue);
    br_r3d_draw_mesh(&r3, &cube, br_m4_identity(), &red);
    CHECK(br_color_eq(px(&img, 80, 45), br_rgb(255, 0, 0)));
    CHECK(br_color_eq(px(&img, 80 + 18, 45), br_rgb(0, 0, 255)));
    CHECK(br_color_eq(px(&img, 2, 2), br_rgb(10, 10, 10)));
    CHECK(r3.stats.culled > 0 && r3.stats.triangles > 0);
    /* the other order: depth still puts red in front */
    br_image_fill(&img, br_rgb(10, 10, 10));
    br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
    br_r3d_draw_mesh(&r3, &cube, br_m4_identity(), &red);
    br_r3d_draw_mesh(&r3, &cube, br_m4_trs(br_v3(0, 0, -3), br_quat_identity(), br_v3(4, 4, 1)), &blue);
    CHECK(br_color_eq(px(&img, 80, 45), br_rgb(255, 0, 0)));
    /* the cube's width on screen: 1 unit at distance 4.5 with a 60 degree view, 90 pixels high */
    {
        int x0 = -1, x1 = -1, x;
        for (x = 0; x < 160; ++x) if (px(&img, x, 45).r == 255) { if (x0 < 0) x0 = x; x1 = x; }
        CHECK(abs((x1 - x0 + 1) - (int)(90.0 / (2 * 4.5 * tan(30 * BR_DEG2RAD)) + 0.5)) <= 1);
    }
    /* lighting: a sun straight down the view lights the front face fully, one from behind leaves only ambient */
    memset(&sun, 0, sizeof(sun));
    sun.kind = BR_LIGHT_DIRECTIONAL;
    sun.color = BR_WHITE;
    sun.intensity = 1;
    red.lit = true;
    r3.ambient = br_rgb(20, 20, 20);
    sun.direction = br_v3(0, 0, -1);
    br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
    br_r3d_add_light(&r3, &sun);
    br_r3d_draw_mesh(&r3, &cube, br_m4_identity(), &red);
    CHECK(px(&img, 80, 45).r == 255);
    sun.direction = br_v3(0, 0, 1);
    br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
    br_r3d_add_light(&r3, &sun);
    br_r3d_draw_mesh(&r3, &cube, br_m4_identity(), &red);
    CHECK(px(&img, 80, 45).r == 20);
    /* inside a cube with culling off: the walls are drawn (near-plane clipping keeps it whole) */
    red.lit = false;
    red.cull = false;
    br_image_fill(&img, br_rgb(10, 10, 10));
    br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
    br_r3d_draw_mesh(&r3, &cube, br_m4_trs(br_v3(0, 0, 5), br_quat_identity(), br_v3(3, 3, 3)), &red);
    for (i = 0; i < 160 * 90; ++i) if (img.pixels[i].r != 255) break;
    CHECK(i == 160 * 90);
    /* project and ray agree */
    {
        br_vec3 sp, o, d, p = br_v3(0.7f, -0.4f, -1.5f);
        br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
        CHECK(br_r3d_project(&r3, p, &sp) && sp.z > 0 && sp.z < 1);
        br_r3d_ray(&r3, br_v2(sp.x, sp.y), &o, &d);
        CHECK(br_v3_len(br_v3_cross(br_v3_sub(p, o), d)) < 1e-3f);    /* p is on the ray */
        CHECK(!br_r3d_project(&r3, br_v3(0, 0, 10), &sp));             /* behind the camera */
    }
    /* textures: a 2x2 checker on a facing square, sampled by uv */
    {
        br_image tex;
        br_material m = br_material_make(BR_WHITE);
        br_mesh plane;
        br_image_init(&tex, 2, 2);
        br_image_set(&tex, 0, 0, br_rgb(0, 255, 0)); br_image_set(&tex, 1, 0, br_rgb(255, 255, 0));
        br_image_set(&tex, 0, 1, br_rgb(255, 0, 255)); br_image_set(&tex, 1, 1, br_rgb(0, 255, 255));
        m.texture = &tex;
        m.lit = false;
        br_mesh_plane(&plane);
        br_image_fill(&img, br_rgb(10, 10, 10));
        br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
        br_r3d_draw_mesh(&r3, &plane, br_m4_trs(br_v3(0, 0, 0), br_quat_from_euler(br_v3(90, 0, 0)), br_v3(3, 1, 3)), &m);
        CHECK(br_color_eq(px(&img, 70, 35), br_rgb(0, 255, 0)));      /* top left: the image's top left */
        CHECK(br_color_eq(px(&img, 90, 55), br_rgb(0, 255, 255)));    /* bottom right */
        br_mesh_destroy(&plane);
        br_image_destroy(&tex);
    }
    /* deterministic */
    for (i = 0; i < 2; ++i) {
        uint32_t k, *h = i ? &hash2 : &hash1;
        br_material lit = br_material_make(br_rgb(200, 120, 40));
        br_image_fill(&img, br_rgb(10, 10, 10));
        br_r3d_begin(&r3, &r, &cam, br_recti_make(0, 0, 160, 90));
        br_r3d_add_light(&r3, &sun);
        br_r3d_draw_mesh(&r3, &cube, br_m4_trs(br_v3(0.2f, 0.1f, 0), br_quat_from_euler(br_v3(20, 35, 10)), br_v3(1.5f, 1, 1)), &lit);
        for (k = 0; k < 160 * 90; ++k) *h = *h * 31u + img.pixels[k].r + img.pixels[k].g * 7u + img.pixels[k].b * 13u;
    }
    CHECK(hash1 == hash2);
    br_mesh_destroy(&cube);
    br_r3d_destroy(&r3);
    br_image_destroy(&img);
}

/* ---- components --------------------------------------------------------------------- */

static br_world W;
static br_builtin_ids ID;
static br_ids3d I3;

static void setup3(void) {
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    CHECK(br_ids3d_of(&W, &I3));
}
static br_entity make3(br_vec3 p) {
    br_entity e = br_entity_create(&W);
    br_transform3d *t = (br_transform3d *)br_add(&W, e, I3.transform3d, NULL);
    t->position = p;
    return e;
}
static br_transform3d *t3(br_entity e) { return (br_transform3d *)br_get(&W, e, I3.transform3d); }

static void test_components3d(void) {
    br_entity parent, child, cam, cam2, box;
    br_vec3 p;
    br_camera3d c;
    setup3();
    CHECK(t3(make3(br_v3(0, 0, 0)))->scale.x == 1);               /* defaults */
    parent = make3(br_v3(10, 0, 0));
    t3(parent)->rotation = br_v3(0, 90, 0);
    t3(parent)->scale = br_v3(2, 2, 2);
    child = make3(br_v3(0, 0, -1));
    CHECK(br_set_parent(&W, child, parent, false));
    br_world_transform3d(&W, child, &p, NULL, NULL);
    CHECK(br_v3_dist(p, br_v3(8, 0, 0)) < 1e-4f);                 /* -Z turned to -X, doubled */
    CHECK(br_v3_dist(br_forward3d(&W, child), br_v3(-1, 0, 0)) < 1e-5f);
    CHECK(br_set_parent(&W, child, BR_ENTITY_NULL, true));        /* unparented, it stays put */
    CHECK(br_v3_dist(t3(child)->position, br_v3(8, 0, 0)) < 1e-4f && fabsf(t3(child)->scale.x - 2) < 1e-4f);
    CHECK(br_set_parent(&W, child, parent, true));
    CHECK(br_v3_dist(t3(child)->position, br_v3(0, 0, -1)) < 1e-4f);
    br_set_world_position3d(&W, child, br_v3(10, 5, 0));
    br_world_transform3d(&W, child, &p, NULL, NULL);
    CHECK(br_v3_dist(p, br_v3(10, 5, 0)) < 1e-4f);
    br_look_at3d(&W, child, br_v3(10, 5, -7));
    CHECK(br_v3_dist(br_forward3d(&W, child), br_v3(0, 0, -1)) < 1e-4f);
    /* cameras: the active one with the highest priority */
    CHECK(!br_scene_camera3d(&W, &c, NULL));
    cam = make3(br_v3(0, 2, 10));
    br_add(&W, cam, I3.camera, NULL);
    cam2 = make3(br_v3(0, 50, 0));
    ((br_camera3d_comp *)br_add(&W, cam2, I3.camera, NULL))->priority = 5;
    {
        br_entity which;
        CHECK(br_scene_camera3d(&W, &c, &which) && which == cam2);
        ((br_camera3d_comp *)br_get(&W, cam2, I3.camera))->active = false;
        CHECK(br_scene_camera3d(&W, &c, &which) && which == cam && c.fov == 60);
    }
    /* drawing a Mesh entity through it, and picking it */
    box = make3(br_v3(0, 2, 0));
    ((br_mesh3d *)br_add(&W, box, I3.mesh, NULL))->color = br_rgb(250, 30, 30);
    {
        br_image img;
        br_renderer r;
        br_r3d r3;
        float t;
        br_vec3 n;
        br_image_init(&img, 120, 80);
        br_renderer_init(&r, &img);
        br_r3d_init(&r3);
        CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 120, 80)));
        CHECK(px(&img, 60, 40).r > 100 && px(&img, 60, 40).g < 60);     /* the box, lit by the default sun */
        CHECK(br_color_eq(px(&img, 2, 2), ((br_camera3d_comp *)br_get(&W, cam, I3.camera))->clear_color));   /* the sky */
        CHECK(br_pick3d(&W, br_v3(0, 2, 10), br_v3(0, 0, -1), &t, &n) == box && fabsf(t - 9.5f) < 1e-4f);
        CHECK(br_v3_dist(n, br_v3(0, 0, 1)) < 1e-4f);
        CHECK(br_pick3d(&W, br_v3(5, 2, 10), br_v3(0, 0, -1), &t, &n) == BR_ENTITY_NULL);
        ((br_mesh3d *)br_get(&W, box, I3.mesh))->visible = false;
        CHECK(br_pick3d(&W, br_v3(0, 2, 10), br_v3(0, 0, -1), &t, &n) == BR_ENTITY_NULL);
        br_r3d_destroy(&r3);
        br_image_destroy(&img);
    }
    /* a scene file keeps it all */
    {
        br_world w2;
        char err[200];
        br_entity loaded[16];
        int k, n;
        CHECK(br_world_save_file(&W, NULL, 0, test_path("scene3d.scene")));
        br_world_init(&w2);
        br_builtin_register(&w2, NULL);
        n = br_world_load_file(&w2, test_path("scene3d.scene"), loaded, 16, err, sizeof(err));
        CHECK(n == (int)br_world_entity_count(&W));
        for (k = 0; k < n; ++k) {
            const br_transform3d *a = (const br_transform3d *)br_get(&w2, loaded[k], br_component_find(&w2, "Transform3D"));
            if (a && fabsf(a->position.y - 50) < 1e-6f) CHECK(((const br_camera3d_comp *)br_get(&w2, loaded[k], br_component_find(&w2, "Camera3D")))->priority == 5);
        }
        br_world_destroy(&w2);
    }
    br_world_destroy(&W);
    br_components3d_release();
}

/* ---- physics ------------------------------------------------------------------------ */

static br_entity body3(br_vec3 p, int shape, br_vec3 size, int type) {
    br_entity e = make3(p);
    br_collider3d *c = (br_collider3d *)br_add(&W, e, br_component_find(&W, "Collider3D"), NULL);
    c->shape = shape;
    c->size = size;
    c->radius = size.x * 0.5f;
    if (type >= 0) ((br_rigidbody3d *)br_add(&W, e, br_component_find(&W, "RigidBody3D"), NULL))->type = type;
    return e;
}
static void steps3(int n) { while (n-- > 0) br_physics3d_step(&W, 1.0f / 60.0f); }

static void test_physics3d_rest_and_stack(void) {
    br_entity ball, stack[5], tilted, ground;
    int i;
    setup3();
    ground = body3(br_v3(0, -0.5f, 0), BR_COLLIDER3D_BOX, br_v3(40, 1, 40), -1);
    ball = body3(br_v3(3, 4, 0), BR_COLLIDER3D_SPHERE, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    for (i = 0; i < 5; ++i) {
        stack[i] = body3(br_v3(0, 0.5f + (float)i, 0), BR_COLLIDER3D_BOX, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
        t3(stack[i])->rotation = br_v3(0, 12.0f * (float)i, 0);
    }
    tilted = body3(br_v3(-3, 2, 0), BR_COLLIDER3D_BOX, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    t3(tilted)->rotation = br_v3(30, 20, 40);
    steps3(60 * 6);
    CHECK_NEAR(t3(ball)->position.y, 0.5, 0.02);
    CHECK_NEAR(t3(stack[4])->position.y, 4.5, 0.03);              /* five high, still standing */
    CHECK(fabsf(t3(stack[4])->position.x) < 0.05f && fabsf(t3(stack[4])->position.z) < 0.05f);
    CHECK_NEAR(t3(tilted)->position.y, 0.5, 0.03);                /* tumbled onto a face */
    {
        br_vec3 up = br_up3d(&W, tilted);                          /* some axis of it points straight up */
        br_vec3 rt = br_right3d(&W, tilted), fw = br_forward3d(&W, tilted);
        float m = fmaxf(fabsf(up.y), fmaxf(fabsf(rt.y), fabsf(fw.y)));
        CHECK(m > 0.999f);
    }
    {   /* resting means resting: nothing moves over one more second */
        br_vec3 before = t3(stack[4])->position;
        steps3(60);
        CHECK(br_v3_dist(before, t3(stack[4])->position) < 0.005f);
    }
    BR_UNUSED(ground);
    br_world_destroy(&W);
}

static void test_physics3d_contacts(void) {
    br_entity floor_, bouncer, trig, slider, sticky, ramp, a, b, kin, pushed;
    uint32_t n, k;
    const br_contact3d_event *ev;
    bool began = false, entered = false, left = false;
    setup3();
    floor_ = body3(br_v3(0, -0.5f, 0), BR_COLLIDER3D_BOX, br_v3(100, 1, 100), -1);
    bouncer = body3(br_v3(0, 3, 0), BR_COLLIDER3D_SPHERE, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    ((br_collider3d *)br_get(&W, bouncer, br_component_find(&W, "Collider3D")))->bounce = 0.8f;
    trig = body3(br_v3(0, 1.5f, 0), BR_COLLIDER3D_BOX, br_v3(2, 0.2f, 2), -1);
    ((br_collider3d *)br_get(&W, trig, br_component_find(&W, "Collider3D")))->trigger = true;
    {
        float highest_after_bounce = 0;
        bool bounced = false;
        for (k = 0; k < 120; ++k) {
            uint32_t j;
            steps3(1);
            ev = br_physics3d_events(&W, &n);
            for (j = 0; j < n; ++j) {
                if (ev[j].kind == BR_CONTACT_BEGIN && (ev[j].a == bouncer || ev[j].b == bouncer)) {
                    began = true;
                    CHECK(ev[j].speed > 3);
                    CHECK(fabsf(ev[j].normal.y) > 0.99f);
                }
                if (ev[j].kind == BR_TRIGGER_ENTER && (ev[j].a == trig || ev[j].b == trig)) entered = true;
                if (ev[j].kind == BR_TRIGGER_EXIT && (ev[j].a == trig || ev[j].b == trig)) left = true;
            }
            if (began) bounced = true;
            if (bounced && t3(bouncer)->position.y > highest_after_bounce) highest_after_bounce = t3(bouncer)->position.y;
        }
        CHECK(began && entered && left);
        CHECK(highest_after_bounce > 1.0f);                          /* it came back up */
    }
    /* friction on a slope: rubber stays, ice slides */
    ramp = body3(br_v3(20, 0, 0), BR_COLLIDER3D_BOX, br_v3(10, 1, 4), -1);
    t3(ramp)->rotation = br_v3(0, 0, 15);
    sticky = body3(br_v3(20, 1.2f, 0), BR_COLLIDER3D_BOX, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    t3(sticky)->rotation = br_v3(0, 0, 15);
    ((br_collider3d *)br_get(&W, sticky, br_component_find(&W, "Collider3D")))->friction = 1;
    slider = body3(br_v3(20, 1.2f, 1.5f), BR_COLLIDER3D_BOX, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    t3(slider)->rotation = br_v3(0, 0, 15);
    ((br_collider3d *)br_get(&W, slider, br_component_find(&W, "Collider3D")))->friction = 0;
    ((br_collider3d *)br_get(&W, ramp, br_component_find(&W, "Collider3D")))->friction = 1;
    {
        float sx = t3(sticky)->position.x, lx = t3(slider)->position.x;
        steps3(60);
        CHECK(fabsf(t3(sticky)->position.x - sx) < 0.05f);
        CHECK(t3(slider)->position.x < lx - 0.5f);                    /* down the slope (toward -x) */
    }
    /* layers: a and b pass through each other */
    a = body3(br_v3(-20, 3, 0), BR_COLLIDER3D_SPHERE, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    b = body3(br_v3(-20, 0.25f, 0), BR_COLLIDER3D_BOX, br_v3(1, 0.5f, 1), -1);
    ((br_collider3d *)br_get(&W, a, br_component_find(&W, "Collider3D")))->layer = 3;
    ((br_collider3d *)br_get(&W, b, br_component_find(&W, "Collider3D")))->mask = ~(1 << 3);
    steps3(90);
    CHECK_NEAR(t3(a)->position.y, 0.5, 0.05);                      /* on the floor, inside b */
    /* a kinematic paddle pushes a dynamic box */
    kin = body3(br_v3(-30, 0.5f, 0), BR_COLLIDER3D_BOX, br_v3(1, 1, 3), BR_BODY_KINEMATIC);
    ((br_rigidbody3d *)br_get(&W, kin, br_component_find(&W, "RigidBody3D")))->velocity = br_v3(2, 0, 0);
    pushed = body3(br_v3(-28.5f, 0.5f, 0), BR_COLLIDER3D_BOX, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    steps3(60);
    CHECK_NEAR(t3(kin)->position.x, -28, 0.01);
    CHECK(t3(pushed)->position.x > t3(kin)->position.x + 0.9f);
    /* queries */
    {
        br_raycast3d_hit hit;
        br_entity found[8];
        CHECK(br_physics3d_raycast(&W, br_v3(-20, 10, 0), br_v3(-20, -10, 0), -1, BR_ENTITY_NULL, &hit));
        CHECK(hit.entity == a && fabsf(hit.point.y - 1.0f) < 0.06f && hit.normal.y > 0.99f);
        CHECK(br_physics3d_raycast(&W, br_v3(-20, 10, 0), br_v3(-20, -10, 0), -1, a, &hit) && hit.entity == b);
        CHECK(!br_physics3d_raycast(&W, br_v3(-50, 10, 0), br_v3(-50, 5, 0), -1, BR_ENTITY_NULL, &hit));
        CHECK(br_physics3d_overlap_sphere(&W, br_v3(0, 1.5f, 0), 0.3f, -1, found, 8) == 1 && found[0] == trig);
    }
    BR_UNUSED(floor_);
    br_world_destroy(&W);
}

static void test_physics3d_deterministic(void) {
    uint32_t pass, hashes[2] = { 0, 0 };
    for (pass = 0; pass < 2; ++pass) {
        br_entity bodies[12];
        int i;
        setup3();
        body3(br_v3(0, -0.5f, 0), BR_COLLIDER3D_BOX, br_v3(30, 1, 30), -1);
        for (i = 0; i < 12; ++i) {
            bodies[i] = body3(br_v3((float)(i % 3) * 0.6f, 1 + (float)i * 1.1f, (float)(i % 2) * 0.4f), i % 2 ? BR_COLLIDER3D_SPHERE : BR_COLLIDER3D_BOX,
                              br_v3(1, 1, 1), BR_BODY_DYNAMIC);
            t3(bodies[i])->rotation = br_v3((float)i * 7, (float)i * 13, (float)i * 3);
        }
        steps3(240);
        for (i = 0; i < 12; ++i) {
            br_vec3 p = t3(bodies[i])->position;
            uint32_t bits[3];
            memcpy(bits, &p, sizeof(bits));
            hashes[pass] = hashes[pass] * 31u + bits[0] + bits[1] * 7u + bits[2] * 13u;
        }
        br_world_destroy(&W);
    }
    CHECK(hashes[0] == hashes[1]);
}

/* Capsules: standing on a box, lying on a box, against each other; mesh colliders:
 * a ramp made of triangles that a ball rolls down and a box rests on. */
static br_entity mesh_body(br_vec3 p, int shape, br_vec3 scale, br_vec3 rot) {
    br_entity e = make3(p);
    br_mesh3d *m = (br_mesh3d *)br_add(&W, e, I3.mesh, NULL);
    br_collider3d *c = (br_collider3d *)br_add(&W, e, br_component_find(&W, "Collider3D"), NULL);
    m->shape = shape;
    c->shape = BR_COLLIDER3D_MESH;
    t3(e)->scale = scale;
    t3(e)->rotation = rot;
    return e;
}

static void test_physics3d_capsules_and_meshes(void) {
    br_entity stand, lie, cap2, floor_mesh, ball, box, ramp, roller;
    br_raycast3d_hit hit;
    setup3();
    body3(br_v3(0, -0.5f, 0), BR_COLLIDER3D_BOX, br_v3(20, 1, 20), -1);
    stand = body3(br_v3(0, 1.5f, 0), BR_COLLIDER3D_CAPSULE, br_v3(1, 2, 1), BR_BODY_DYNAMIC);   /* radius 0.5, 2 tall */
    ((br_rigidbody3d *)br_get(&W, stand, br_component_find(&W, "RigidBody3D")))->lock_rotation = true;
    lie = body3(br_v3(4, 1, 0), BR_COLLIDER3D_CAPSULE, br_v3(1, 2, 1), BR_BODY_DYNAMIC);
    t3(lie)->rotation = br_v3(0, 0, 90);                       /* on its side */
    cap2 = body3(br_v3(0, 4, 0.2f), BR_COLLIDER3D_CAPSULE, br_v3(1, 2, 1), BR_BODY_DYNAMIC);   /* falls onto the standing one */
    t3(cap2)->rotation = br_v3(90, 0, 0);
    steps3(60 * 4);
    CHECK_NEAR(t3(stand)->position.y, 1.0, 0.03);              /* its bottom on the floor */
    CHECK_NEAR(t3(lie)->position.y, 0.5, 0.03);                /* lying: as high as it is round */
    CHECK(t3(cap2)->position.y > 0.45f);                       /* on the other, or rolled off onto the floor: not through it */
    CHECK(br_physics3d_raycast(&W, br_v3(4, 5, 0.1f), br_v3(4, -5, 0.1f), -1, BR_ENTITY_NULL, &hit) && hit.entity == lie);
    CHECK(fabsf(hit.point.y - 1.0f) < 0.05f && hit.normal.y > 0.9f);
    CHECK(br_physics3d_raycast(&W, br_v3(-5, 1.5f, 0), br_v3(5, 1.5f, 0), -1, BR_ENTITY_NULL, &hit) && hit.entity == stand);
    CHECK_NEAR(hit.point.x, -0.5, 0.02);
    br_world_destroy(&W);
    br_components3d_release();

    setup3();
    floor_mesh = mesh_body(br_v3(0, 0, 0), BR_MESH_PLANE, br_v3(30, 1, 30), br_v3(0, 0, 0));   /* a floor of two triangles */
    ramp = mesh_body(br_v3(20, 2, 0), BR_MESH_PLANE, br_v3(10, 1, 6), br_v3(0, 0, 20));    /* tipped 20 degrees: down toward -x */
    ball = body3(br_v3(0, 3, 0), BR_COLLIDER3D_SPHERE, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    box = body3(br_v3(5, 3, 3), BR_COLLIDER3D_BOX, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    t3(box)->rotation = br_v3(10, 20, 5);
    roller = body3(br_v3(22, 4.5f, 0), BR_COLLIDER3D_SPHERE, br_v3(1, 1, 1), BR_BODY_DYNAMIC);
    steps3(60 * 3);
    CHECK_NEAR(t3(ball)->position.y, 0.5, 0.03);
    CHECK_NEAR(t3(box)->position.y, 0.5, 0.06);                /* settled on a face */
    CHECK(t3(roller)->position.x < 20);                        /* rolled down the ramp */
    CHECK(br_physics3d_raycast(&W, br_v3(-3, 5, -3), br_v3(-3, -5, -3), -1, BR_ENTITY_NULL, &hit) && hit.entity == floor_mesh);
    CHECK(fabsf(hit.point.y) < 1e-3f && hit.normal.y > 0.99f);
    {
        br_entity found[4];
        CHECK(br_physics3d_overlap_sphere(&W, br_v3(-3, 0.2f, -3), 0.3f, -1, found, 4) == 1 && found[0] == floor_mesh);
    }
    BR_UNUSED(ramp);
    br_world_destroy(&W);
    br_components3d_release();
}

/* br_physics3d_move: walking into a wall slides along it, gravity keeps it on the floor,
 * it walks up a ramp, and a fast move doesn't go through a thin wall. */
static void test_physics3d_character(void) {
    br_entity hero, wall, thin;
    br_physics3d_move_info info;
    br_vec3 v;
    int i;
    setup3();
    body3(br_v3(0, -0.5f, 0), BR_COLLIDER3D_BOX, br_v3(40, 1, 40), -1);
    wall = body3(br_v3(3, 1, 0), BR_COLLIDER3D_BOX, br_v3(1, 2, 20), -1);
    thin = body3(br_v3(-6, 1, 0), BR_COLLIDER3D_BOX, br_v3(0.1f, 2, 20), -1);
    mesh_body(br_v3(0, 0, -10), BR_MESH_PLANE, br_v3(6, 1, 8), br_v3(15, 0, 0));           /* a ramp rising toward -z */
    hero = body3(br_v3(0, 1, 0), BR_COLLIDER3D_CAPSULE, br_v3(1, 2, 1), -1);
    v = br_v3(0, 0, 0);
    for (i = 0; i < 30; ++i) {                                  /* stand: gravity, then the floor stops it */
        v.y -= 9.81f / 60;
        br_physics3d_move(&W, hero, br_v3_scale(v, 1.0f / 60), &v, &info);
    }
    CHECK(info.on_floor && fabsf(t3(hero)->position.y - 1) < 0.01f && fabsf(v.y) < 0.2f);
    for (i = 0; i < 90; ++i) {                                  /* into the wall at an angle: slides along it */
        v = br_v3(3, v.y - 9.81f / 60, 3);
        br_physics3d_move(&W, hero, br_v3_scale(v, 1.0f / 60), &v, &info);
    }
    CHECK(t3(hero)->position.x < 2.01f && t3(hero)->position.x > 1.9f);   /* against the wall (2.5 - 0.5) */
    CHECK(t3(hero)->position.z > 3);                            /* and slid along it */
    CHECK(br_physics3d_last_move(&W, hero, &info) && info.on_wall && info.on_floor);
    CHECK(fabsf(v.x) < 1e-3f);                                  /* the velocity into the wall is gone */
    t3(hero)->position = br_v3(-4, 1, 0);                       /* a fast move at the thin wall: stopped, not through */
    br_physics3d_move(&W, hero, br_v3(-5, 0, 0), NULL, &info);
    CHECK(t3(hero)->position.x > -6 && info.on_wall);
    t3(hero)->position = br_v3(0, 1, -5);                       /* up the ramp */
    for (i = 0; i < 240; ++i) {
        v = br_v3(0, v.y - 9.81f / 60, -2);
        br_physics3d_move(&W, hero, br_v3_scale(v, 1.0f / 60), &v, &info);
    }
    CHECK(t3(hero)->position.z < -11.5f && t3(hero)->position.y > 1.4f && info.on_floor);
    if (!(t3(hero)->position.z < -11.5f && t3(hero)->position.y > 1.4f)) printf("      ramp: %.2f %.2f\n", (double)t3(hero)->position.y, (double)t3(hero)->position.z);
    BR_UNUSED(wall); BR_UNUSED(thin);
    br_world_destroy(&W);
    br_components3d_release();
}

/* Fog fades far things into its colour; a Mesh with `shadow` darkens the ground below it. */
static void test_fog_and_shadows(void) {
    br_image img;
    br_renderer r;
    br_r3d r3;
    br_entity cam, ground, near_box, far_box, floater;
    br_camera3d_comp *c;
    br_color sky;
    setup3();
    br_image_init(&img, 160, 90);
    br_renderer_init(&r, &img);
    br_r3d_init(&r3);
    cam = make3(br_v3(0, 6, 6));
    c = (br_camera3d_comp *)br_add(&W, cam, I3.camera, NULL);
    sky = c->clear_color;
    c->fog = true;
    c->fog_color = br_rgb(250, 0, 250);
    c->fog_start = 10;
    c->fog_end = 30;
    br_look_at3d(&W, cam, br_v3(0, 0, -4));
    ground = make3(br_v3(0, 0, 0));
    ((br_mesh3d *)br_add(&W, ground, I3.mesh, NULL))->shape = BR_MESH_PLANE;
    t3(ground)->scale = br_v3(8, 1, 8);
    ((br_mesh3d *)br_get(&W, ground, I3.mesh))->color = br_rgb(200, 200, 200);
    ((br_mesh3d *)br_get(&W, ground, I3.mesh))->lit = false;
    near_box = make3(br_v3(-2, 1, 0));
    ((br_mesh3d *)br_add(&W, near_box, I3.mesh, NULL))->lit = false;
    far_box = make3(br_v3(2, 1, -60));                          /* beyond the fog's end */
    t3(far_box)->scale = br_v3(8, 8, 8);
    ((br_mesh3d *)br_add(&W, far_box, I3.mesh, NULL))->lit = false;
    floater = make3(br_v3(1.5f, 1.2f, 0));                      /* a ball above the ground, with a shadow */
    ((br_mesh3d *)br_add(&W, floater, I3.mesh, NULL))->shape = BR_MESH_SPHERE;
    ((br_mesh3d *)br_get(&W, floater, I3.mesh))->shadow = true;
    t3(floater)->scale = br_v3(0.5f, 0.5f, 0.5f);
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 160, 90)));
    {
        br_vec3 sp;
        br_color near_c, far_c, under, beside;
        CHECK(br_r3d_project(&r3, br_v3(-2, 1, 0.5f), &sp));
        near_c = br_image_get(&img, (int)sp.x, (int)sp.y);
        CHECK(near_c.r > 190 && near_c.g > 190 && near_c.b > 190);   /* near: its own colour */
        CHECK(br_r3d_project(&r3, br_v3(2, 1, -56), &sp));
        far_c = br_image_get(&img, (int)sp.x, (int)sp.y);
        CHECK(far_c.r == 250 && far_c.g == 0 && far_c.b == 250);    /* far: all fog */
        CHECK(br_r3d_project(&r3, br_v3(1.5f, 0, 0), &sp));
        under = br_image_get(&img, (int)sp.x, (int)sp.y);
        CHECK(br_r3d_project(&r3, br_v3(3.5f, 0, 0.5f), &sp));
        beside = br_image_get(&img, (int)sp.x, (int)sp.y);
        CHECK(under.r + 25 < beside.r);                              /* the shadow */
        if (!(under.r + 25 < beside.r)) { printf("      under %d beside %d\n", under.r, beside.r); br_image_save_bmp(&img, test_path("shadow.bmp")); }
    }
    BR_UNUSED(sky);
    br_r3d_destroy(&r3);
    br_image_destroy(&img);
    br_world_destroy(&W);
    br_components3d_release();
}

/* The BVH finds exactly what looking at every triangle finds. */
static bool brute_ray(const br_mesh *m, br_vec3 o, br_vec3 d, float *best) {
    uint32_t i;
    bool hit = false;
    *best = 1e30f;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        br_vec3 a = m->verts[m->indices[i]].pos, b = m->verts[m->indices[i + 1]].pos, c = m->verts[m->indices[i + 2]].pos;
        br_vec3 e1 = br_v3_sub(b, a), e2 = br_v3_sub(c, a), pv = br_v3_cross(d, e2), tv, qv;
        float det = br_v3_dot(e1, pv), inv, u, v, t;
        if (fabsf(det) < 1e-12f) continue;
        inv = 1.0f / det;
        tv = br_v3_sub(o, a);
        u = br_v3_dot(tv, pv) * inv;
        if (u < 0 || u > 1) continue;
        qv = br_v3_cross(tv, e1);
        v = br_v3_dot(d, qv) * inv;
        if (v < 0 || u + v > 1) continue;
        t = br_v3_dot(e2, qv) * inv;
        if (t >= 0 && t < *best) { *best = t; hit = true; }
    }
    return hit;
}

static void test_mesh_bvh(void) {
    br_mesh m;
    br_rng rng;
    int k, agree = 0, hits = 0;
    uint32_t tris[4096], i;
    br_rng_seed(&rng, 5);
    CHECK(br_mesh_sphere(&m, 64, 32));
    CHECK(m.bvh == NULL);
    for (k = 0; k < 400; ++k) {
        br_vec3 o = br_v3(br_rng_range(&rng, -2, 2), br_rng_range(&rng, -2, 2), br_rng_range(&rng, -2, 2));
        br_vec3 d = br_v3_sub(br_v3(br_rng_range(&rng, -0.4f, 0.4f), br_rng_range(&rng, -0.4f, 0.4f), br_rng_range(&rng, -0.4f, 0.4f)), o);
        float t1 = 0, t2 = 0;
        br_vec3 n;
        bool a = br_mesh_raycast(&m, o, d, &t1, &n), b = brute_ray(&m, o, d, &t2);
        if (a == b && (!a || fabsf(t1 - t2) < 1e-5f)) ++agree;
        if (a) ++hits;
    }
    CHECK(m.bvh != NULL);                                        /* (made by the first raycast) */
    CHECK_EQ_INT(agree, 400);
    CHECK(hits > 300);
    {   /* boxes: every triangle that touches it, and no others */
        br_vec3 lo = br_v3(0.1f, -0.2f, -0.5f), hi = br_v3(0.6f, 0.3f, 0.5f);
        uint32_t n = br_mesh_triangles_in(&m, lo, hi, tris, 4096), want = 0;
        bool all = true;
        for (i = 0; i + 2 < m.index_count; i += 3) {
            br_vec3 a = m.verts[m.indices[i]].pos, b = m.verts[m.indices[i + 1]].pos, c = m.verts[m.indices[i + 2]].pos;
            br_vec3 tl = br_v3_min(a, br_v3_min(b, c)), th = br_v3_max(a, br_v3_max(b, c));
            if (tl.x > hi.x || th.x < lo.x || tl.y > hi.y || th.y < lo.y || tl.z > hi.z || th.z < lo.z) continue;
            ++want;
            {
                uint32_t j;
                bool found = false;
                for (j = 0; j < n && j < 4096; ++j) if (tris[j] == i / 3) found = true;
                if (!found) all = false;
            }
        }
        CHECK(n == want && n > 0 && all);
        CHECK(br_mesh_triangles_in(&m, lo, hi, tris, 3) == want);        /* (counts past max) */
    }
    br_mesh_bounds(&m);                                          /* vertices changed: it goes, and comes back */
    CHECK(m.bvh == NULL);
    br_mesh_destroy(&m);
}

/* Real shadows: a box over the ground darkens the ground where the sun can't reach. */
static void test_shadow_maps(void) {
    br_image img;
    br_renderer r;
    br_r3d r3;
    br_entity cam, ground, box, sun;
    br_light *L;
    br_vec3 sp;
    br_color under_on, under_off, open_on, open_off, top_on, top_off;
    setup3();
    br_image_init(&img, 200, 120);
    br_renderer_init(&r, &img);
    br_r3d_init(&r3);
    cam = make3(br_v3(0, 7, 9));
    br_add(&W, cam, I3.camera, NULL);
    br_look_at3d(&W, cam, br_v3(0, 0, 0));
    ground = make3(br_v3(0, 0, 0));
    ((br_mesh3d *)br_add(&W, ground, I3.mesh, NULL))->shape = BR_MESH_PLANE;
    t3(ground)->scale = br_v3(14, 1, 14);
    box = make3(br_v3(0, 2, 0));                                  /* up in the air: its shadow falls straight down */
    sun = make3(br_v3(0, 10, 0));
    t3(sun)->rotation = br_v3(-90, 0, 0);                          /* (facing down) */
    L = (br_light *)br_add(&W, sun, I3.light, NULL);
    L->kind = BR_LIGHT_DIRECTIONAL;
    ((br_mesh3d *)br_add(&W, box, I3.mesh, NULL))->color = br_rgb(200, 60, 60);
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 200, 120)));
    CHECK(br_r3d_project(&r3, br_v3(0, 0, 0.0f), &sp));            /* (the box hides (0, 0, 0)? no: it's above, the camera looks down past it) */
    CHECK(!r3.shadows);
    CHECK(br_r3d_project(&r3, br_v3(0.2f, 0, 0.2f), &sp));
    under_off = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(br_r3d_project(&r3, br_v3(4, 0, 2), &sp));
    open_off = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(br_r3d_project(&r3, br_v3(0, 2.5f, 0), &sp));
    top_off = br_image_get(&img, (int)sp.x, (int)sp.y);
    L = (br_light *)br_get(&W, sun, I3.light);
    L->shadows = true;
    L->shadow_range = 10;
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 200, 120)));
    CHECK(r3.shadows);
    CHECK(br_r3d_project(&r3, br_v3(0.2f, 0, 0.2f), &sp));
    under_on = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(br_r3d_project(&r3, br_v3(4, 0, 2), &sp));
    open_on = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(br_r3d_project(&r3, br_v3(0, 2.5f, 0), &sp));
    top_on = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(under_on.r + 60 < under_off.r);                          /* in the shadow: darker */
    CHECK(abs(open_on.r - open_off.r) <= 2 && abs(open_on.g - open_off.g) <= 2);   /* in the sun: just the same (no speckles) */
    CHECK(abs(top_on.r - top_off.r) <= 2);                         /* the box's own top: lit */
    if (!(under_on.r + 60 < under_off.r) || abs(open_on.r - open_off.r) > 2) {
        printf("      under %d/%d open %d/%d top %d/%d\n", under_on.r, under_off.r, open_on.r, open_off.r, top_on.r, top_off.r);
        br_image_save_bmp(&img, test_path("shadow_map.bmp"));
    }
    ((br_mesh3d *)br_get(&W, box, I3.mesh))->cast_shadow = false;  /* not a caster: no shadow */
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 200, 120)));
    CHECK(br_r3d_project(&r3, br_v3(0.2f, 0, 0.2f), &sp));
    CHECK(abs(br_image_get(&img, (int)sp.x, (int)sp.y).r - under_off.r) <= 2);
    br_r3d_destroy(&r3);
    br_image_destroy(&img);
    br_world_destroy(&W);
    br_components3d_release();
}

/* glTF: a skinned bar of two bones; its animations; Animator; and the explorer's model. */
static br_vec3 top_corner(const br_model *m, const br_mat4 *nodes) {      /* the bar's top (+x, +y, +z) corner, posed */
    br_mesh posed;
    br_vec3 best = br_v3(-1e9f, -1e9f, -1e9f);
    int i;
    uint32_t v;
    memset(&posed, 0, sizeof(posed));
    for (i = 0; i < m->part_count; ++i) {
        const br_model_part *p = &m->parts[i];
        if (!br_model_part_posed(m, i, nodes, &posed)) continue;
        for (v = 0; v < p->mesh.vert_count; ++v)
            if (p->mesh.verts[v].pos.y > 1.99f && p->mesh.verts[v].pos.x > 0.09f && p->mesh.verts[v].pos.z > 0.09f) best = posed.verts[v].pos;
    }
    br_mesh_destroy(&posed);
    return best;
}

static void test_gltf_models(void) {
    br_model m;
    br_model_pose pose;
    br_mat4 nodes[8];
    char err[160];
    br_vec3 c;
    CHECK(br_model_load_mem(&m, k_gltf_bar, strlen(k_gltf_bar), NULL, err, sizeof(err)));
    CHECK(m.part_count == 2 && m.node_count == 3 && m.skin_count == 1 && m.clip_count == 2);
    CHECK(br_model_find_clip(&m, "bend") == 0 && br_model_find_clip(&m, "lift") == 1 && br_model_find_clip(&m, "nope") < 0);
    CHECK(fabsf(m.clips[0].duration - 1) < 1e-5f && fabsf(m.clips[1].duration - 2) < 1e-5f);
    CHECK(m.parts[0].skin == 0 && m.parts[0].color.r == 255 && m.parts[0].color.b == 0);
    CHECK(fabsf(m.merged.max.y - 2) < 1e-4f && m.merged.vert_count == 48);
    CHECK(br_model_pose_init(&pose, &m));
    br_model_node_matrices(&m, &pose, nodes);
    c = top_corner(&m, nodes);
    CHECK(fabsf(c.x - 0.1f) < 1e-4f && fabsf(c.y - 2) < 1e-4f);                  /* at rest */
    br_model_pose_sample(&pose, &m, 0, 1, false);                                 /* bent a right angle at the tip joint */
    br_model_node_matrices(&m, &pose, nodes);
    c = top_corner(&m, nodes);
    CHECK(fabsf(c.x - (-1.0f)) < 1e-3f && fabsf(c.y - 1.1f) < 1e-3f);             /* (0.1, 2) -> (-1, 1.1) */
    br_model_pose_sample(&pose, &m, 0, 0.5f, false);                              /* half way: 45 degrees */
    br_model_node_matrices(&m, &pose, nodes);
    c = top_corner(&m, nodes);
    CHECK(fabsf(c.x - (0.1f * 0.70711f - 0.70711f)) < 1e-3f);
    br_model_pose_sample(&pose, &m, 1, 3, true);                                  /* lift, looping: 3 s is 1 s in: 2 up */
    br_model_node_matrices(&m, &pose, nodes);
    CHECK(fabsf(top_corner(&m, nodes).y - 4) < 1e-3f);
    br_model_pose_sample(&pose, &m, 1, 3, false);                                 /* not looping: held at the end */
    br_model_node_matrices(&m, &pose, nodes);
    CHECK(fabsf(top_corner(&m, nodes).y - 6) < 1e-3f);
    br_model_pose_destroy(&pose);
    br_model_destroy(&m);
    CHECK(!br_model_load_mem(&m, "{\"asset\":{\"version\":\"1.0\"}}", 28, NULL, err, sizeof(err)) && strstr(err, "2.0"));
    CHECK(!br_model_load_mem(&m, "{\"asset\":", 9, NULL, err, sizeof(err)) && err[0]);
    {   /* a damaged copy never crashes it */
        size_t n = strlen(k_gltf_bar), k;
        char *bad = (char *)br_alloc(n + 1);
        for (k = 0; k < 40; ++k) {
            memcpy(bad, k_gltf_bar, n + 1);
            bad[(k * 7919) % n] = (char)('0' + k % 10);
            if (br_model_load_mem(&m, bad, n, NULL, err, sizeof(err))) br_model_destroy(&m);
        }
        br_free(bad);
    }
    {   /* a .glb (the Explorer's figure), and an Animator playing it in a world */
        br_entity e;
        br_mesh3d *mesh;
        br_animator *an;
        br_component ac;
        const br_model *md;
        CHECK(br_model_load(&m, "examples/explorer/models/explorer.glb", err, sizeof(err)));
        CHECK(m.clip_count == 4 && br_model_find_clip(&m, "walk") >= 0 && m.skin_count == 1);
        br_model_destroy(&m);
        setup3();
        br_assets_set_root("examples/explorer/");
        e = make3(br_v3(0, 0, 0));
        mesh = (br_mesh3d *)br_add(&W, e, I3.mesh, NULL);
        mesh->shape = BR_MESH_MODEL;
        snprintf(mesh->file, sizeof(mesh->file), "models/explorer.glb");
        md = br_asset_model(mesh->file);
        CHECK(md != NULL && br_mesh3d_mesh(mesh) == &md->merged);
        ac = br_component_find(&W, "Animator");
        an = (br_animator *)br_add(&W, e, ac, NULL);
        snprintf(an->clip, sizeof(an->clip), "walk");
        CHECK(br_animator_clip_count(&W, e) == 4 && strcmp(br_animator_clip_name(&W, e, 1), "walk") == 0);
        CHECK(fabsf(br_animator_clip_length(&W, e, "walk") - 0.8f) < 1e-4f);
        br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 0.25f);
        an = (br_animator *)br_get(&W, e, ac);
        CHECK(strcmp(an->started, "walk") == 0 && fabsf(an->time - 0.25f) < 1e-5f && !an->from[0]);
        snprintf(an->clip, sizeof(an->clip), "jump");                              /* another: a crossfade */
        an->loop = false;
        br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 0.05f);
        an = (br_animator *)br_get(&W, e, ac);
        CHECK(strcmp(an->from, "walk") == 0 && an->fade > 0 && an->fade < an->fade_len);
        br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 0.5f);
        an = (br_animator *)br_get(&W, e, ac);
        CHECK(!an->from[0] && !an->playing && fabsf(an->time - br_animator_clip_length(&W, e, "jump")) < 1e-5f);   /* done, held */
        {
            br_image img;
            br_renderer r;
            br_r3d r3;
            br_entity cam = make3(br_v3(0, 0.3f, -4));
            br_add(&W, cam, I3.camera, NULL);
            br_look_at3d(&W, cam, br_v3(0, 0, 0));
            br_image_init(&img, 64, 64);
            br_renderer_init(&r, &img);
            br_r3d_init(&r3);
            CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 64, 64)));
            CHECK(r3.stats.triangles > 50);                                            /* (the figure was drawn) */
            br_r3d_destroy(&r3);
            br_image_destroy(&img);
        }
        br_world_destroy(&W);
        br_assets_clear();
        br_assets_set_root("");
        br_components3d_release();
    }
}

void suite_3d(void) {
    RUN(test_gltf_models);
    RUN(test_shadow_maps);
    RUN(test_mesh_bvh);
    RUN(test_euler_and_quaternions);
    RUN(test_meshes);
    RUN(test_render3d);
    RUN(test_components3d);
    RUN(test_physics3d_rest_and_stack);
    RUN(test_physics3d_contacts);
    RUN(test_physics3d_deterministic);
    RUN(test_physics3d_capsules_and_meshes);
    RUN(test_physics3d_character);
    RUN(test_fog_and_shadows);
}
