/* Tests: particles (particles.h). */
#include "test.h"
#include <string.h>

static br_world W;
static br_builtin_ids ID;
static br_ids3d I3;
static br_component PART;
static br_engine g_engine;

static void setup(void) {
    memset(&g_engine, 0, sizeof(g_engine));
    br_input_init(&g_engine.input);
    br_mixer_init(&g_engine.mixer, 44100);
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    br_ids3d_of(&W, &I3);
    W.context = &g_engine;
    PART = br_component_find(&W, "Particles");
    br_assets_set_root(g_out_dir);
}

static void teardown(void) {
    br_world_destroy(&W);
    br_assets_clear();
    br_assets_set_root("");
    br_builtin_release();                    /* (the draw list, so it isn't counted as a leak here) */
}

static void steps(int n) { while (n-- > 0) br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60.0f); }

static br_entity emitter2d(br_vec2 at, br_particles **out) {
    br_entity e = br_entity_create(&W);
    ((br_transform *)br_add(&W, e, ID.transform, NULL))->position = at;
    *out = (br_particles *)br_add(&W, e, PART, NULL);
    return e;
}

static uint32_t hash_image(const br_image *img) {
    uint32_t h = 2166136261u;
    size_t i, n = (size_t)img->width * (size_t)img->height;
    for (i = 0; i < n; ++i) {
        h = (h ^ img->pixels[i].r) * 16777619u;
        h = (h ^ img->pixels[i].g) * 16777619u;
        h = (h ^ img->pixels[i].b) * 16777619u;
    }
    return h;
}

/* A stream and a burst: how many, where they go, the cap, and the same every run. */
static void test_particles_emit_and_move(void) {
    br_particles *p;
    br_entity e;
    br_image img;
    br_renderer r;
    uint32_t first = 0;
    int run;
    for (run = 0; run < 2; ++run) {
        setup();
        e = emitter2d(br_v2(80, 60), &p);
        CHECK(p->emitting && p->max > 0 && p->lifetime > 0);       /* (the defaults give off something) */
        p->rate = 60;
        p->burst = 10;
        p->speed = 60;
        p->speed_random = 0;
        p->spread = 0;
        p->angle = -90;                                            /* straight up */
        p->lifetime = 2;
        p->lifetime_random = 0;
        steps(1);
        CHECK(br_particles_count(&W, e) == 11);                    /* the burst, and one from the stream */
        steps(29);
        CHECK(br_particles_count(&W, e) == 40);                    /* half a second's more */
        br_image_init(&img, 160, 120);
        br_renderer_init(&r, &img);
        br_clear(&r, br_rgb(0, 0, 0));
        br_builtin_draw(&W, &r);
        {
            br_color above = br_image_get(&img, 80, 60 - 25), below = br_image_get(&img, 80, 60 + 10);
            CHECK(above.r > 0);                                    /* they rose */
            CHECK(below.r == 0 && below.g == 0);
        }
        if (run == 0) first = hash_image(&img);
        else CHECK(hash_image(&img) == first);                     /* the same particles, run after run */
        br_image_destroy(&img);
        p = (br_particles *)br_get(&W, e, PART);
        p->max = 25;
        steps(1);
        CHECK(br_particles_count(&W, e) == 40);                    /* (a lower max only stops new ones) */
        steps(200);
        CHECK(br_particles_count(&W, e) <= 25);                    /* never more than max */
        p->emitting = false;
        steps(150);
        CHECK(br_particles_count(&W, e) == 0);                     /* off: the last ones die */
        p->rate = 0;
        p->emitting = true;                                        /* on again: the burst again */
        steps(1);
        CHECK(br_particles_count(&W, e) == 10);
        teardown();
    }
}

static void test_particles_gravity_local_destroy(void) {
    br_particles *p;
    br_entity e, boom;
    br_image img;
    br_renderer r;
    setup();
    e = emitter2d(br_v2(50, 20), &p);
    p->rate = 0;
    p->burst = 1;
    p->speed = 0;
    p->speed_random = 0;
    p->gravity = 120;                                              /* falls (2D: +y) */
    p->lifetime = 5;
    p->lifetime_random = 0;
    p->size_start = p->size_end = 4;
    p->color_start = p->color_end = br_rgb(255, 255, 255);
    p->shape = BR_PARTICLE_SQUARE;
    steps(61);
    br_image_init(&img, 100, 120);
    br_renderer_init(&r, &img);
    br_clear(&r, br_rgb(0, 0, 0));
    br_builtin_draw(&W, &r);
    CHECK(br_image_get(&img, 50, 20 + 60).r == 255);               /* about 0.5 * 120 * 1^2 = 60 below */
    CHECK(br_image_get(&img, 50, 20).r == 0);
    /* local: it moves with the entity */
    p = (br_particles *)br_get(&W, e, PART);
    p->local = true;
    p->gravity = 0;
    br_particles_clear(&W, e);
    CHECK(br_particles_count(&W, e) == 0);
    CHECK(br_particles_emit(&W, e, 1));
    ((br_transform *)br_get(&W, e, ID.transform))->position = br_v2(20, 100);
    steps(1);
    br_clear(&r, br_rgb(0, 0, 0));
    br_builtin_draw(&W, &r);
    CHECK(br_image_get(&img, 20, 100).r == 255);
    br_image_destroy(&img);
    /* auto_destroy: a one-off explosion goes away once it's done */
    boom = emitter2d(br_v2(10, 10), &p);
    CHECK(br_particles_preset(p, "explosion", false));
    CHECK(p->rate == 0 && p->burst > 0);
    p->auto_destroy = true;
    steps(2);
    CHECK(br_particles_count(&W, boom) == p->burst);
    steps(120);
    br_world_flush(&W);
    CHECK(!br_entity_alive(&W, boom));
    CHECK(br_entity_alive(&W, e));
    /* without the component, its particles go */
    br_remove(&W, e, PART);
    steps(1);
    CHECK(br_particles_count(&W, e) == 0);
    CHECK(!br_particles_emit(&W, e, 3));
    teardown();
}

static void test_particles_presets_and_preview(void) {
    int count, k;
    const char *const *names = br_particles_presets(&count);
    br_particles p;
    br_entity e;
    br_particles *q;
    CHECK(count >= 7);
    for (k = 0; k < count; ++k) {
        CHECK(br_particles_preset(&p, names[k], false));
        CHECK(p.max > 0 && p.lifetime > 0 && (p.rate > 0 || p.burst > 0));
        CHECK(br_particles_preset(&p, names[k], true));
        CHECK(p.size_start > 0 && p.size_start < 2);                /* units, not pixels */
    }
    CHECK(!br_particles_preset(&p, "glitter", false));
    setup();                                                         /* the editor's preview bursts again */
    e = emitter2d(br_v2(10, 10), &q);
    br_particles_preset(q, "explosion", false);
    q->auto_destroy = true;
    for (k = 0; k < 200; ++k) br_particles_preview(&W, 1.0f / 60.0f);
    br_world_flush(&W);
    CHECK(br_entity_alive(&W, e));                                   /* (never destroyed while editing) */
    for (k = 0; k < 100 && br_particles_count(&W, e) == 0; ++k) br_particles_preview(&W, 1.0f / 60.0f);
    CHECK(br_particles_count(&W, e) > 0);
    teardown();
}

/* 3D: splatted after the meshes, hidden behind them, fogged. */
static void test_particles_3d(void) {
    br_image img;
    br_renderer r;
    br_r3d r3;
    br_entity cam, fx, wall;
    br_particles *p;
    br_vec3 sp;
    br_color c;
    setup();
    br_image_init(&img, 160, 90);
    br_renderer_init(&r, &img);
    br_r3d_init(&r3);
    cam = br_entity_create(&W);
    ((br_transform3d *)br_add(&W, cam, I3.transform3d, NULL))->position = br_v3(0, 1, 6);
    ((br_camera3d_comp *)br_add(&W, cam, I3.camera, NULL))->clear_color = br_rgb(0, 0, 0);
    fx = br_entity_create(&W);
    ((br_transform3d *)br_add(&W, fx, I3.transform3d, NULL))->position = br_v3(-1, 1, 0);
    p = (br_particles *)br_add(&W, fx, PART, NULL);
    p->rate = 0;
    p->burst = 1;
    p->speed = 0;
    p->speed_random = 0;
    p->lifetime = 10;
    p->size_start = p->size_end = 0.5f;
    p->color_start = p->color_end = br_rgb(255, 255, 255);
    p->shape = BR_PARTICLE_SQUARE;
    steps(1);
    CHECK(br_particles_count(&W, fx) == 1);
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 160, 90)));
    CHECK(br_r3d_project(&r3, br_v3(-1, 1, 0), &sp));
    c = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(c.r == 255 && c.g == 255);
    /* a wall in front hides it */
    wall = br_entity_create(&W);
    ((br_transform3d *)br_add(&W, wall, I3.transform3d, NULL))->position = br_v3(-1, 1, 2);
    ((br_mesh3d *)br_add(&W, wall, I3.mesh, NULL))->color = br_rgb(0, 0, 200);
    ((br_mesh3d *)br_get(&W, wall, I3.mesh))->lit = false;
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 160, 90)));
    c = br_image_get(&img, (int)sp.x, (int)sp.y);
    CHECK(c.b == 200 && c.r == 0);
    /* 3D directions: along the entity's up */
    br_entity_destroy(&W, wall);
    p = (br_particles *)br_get(&W, fx, PART);
    p->speed = 1;
    p->spread = 0;
    p->gravity = 0;
    br_particles_clear(&W, fx);
    CHECK(br_particles_emit(&W, fx, 1));
    steps(60);                                                       /* a unit up */
    CHECK(br_draw3d(&W, &r3, &r, NULL, br_recti_make(0, 0, 160, 90)));
    CHECK(br_r3d_project(&r3, br_v3(-1, 2, 0), &sp));
    CHECK(br_image_get(&img, (int)sp.x, (int)sp.y).r == 255);
    br_r3d_destroy(&r3);
    br_image_destroy(&img);
    teardown();
    br_components3d_release();
}

static void test_particles_scripts(void) {
    br_entity e;
    br_particles *p;
    br_script *s;
    double n = 0;
    const char *src = "var made: int = 0\nfunc start() -> void {\n    emit(self, 7)\n    made = particle_count(self)\n}\n";
    setup();
    CHECK(br_file_write_all(test_path("puff.bs"), src, strlen(src)));
    e = emitter2d(br_v2(10, 10), &p);
    p->rate = 0;
    p->burst = 0;
    s = (br_script *)br_add(&W, e, br_component_find(&W, "Script"), NULL);
    snprintf(s->file, sizeof(s->file), "puff.bs");
    steps(1);
    CHECK(br_script_get_number(&W, e, "made", &n) && n == 7);
    teardown();
}

void suite_particles(void) {
    RUN(test_particles_emit_and_move);
    RUN(test_particles_gravity_local_destroy);
    RUN(test_particles_presets_and_preview);
    RUN(test_particles_3d);
    RUN(test_particles_scripts);
}
