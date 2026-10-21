/* Tests: built-in components and the asset cache. */
#include "test.h"
#include <string.h>

static br_world W;
static br_builtin_ids ID;

static br_entity make(br_vec2 pos) {
    br_entity e = br_entity_create(&W);
    ((br_transform *)br_add(&W, e, ID.transform, NULL))->position = pos;
    return e;
}

static void setup(void) {
    br_world_init(&W);
    br_builtin_register(&W, &ID);
}

static void test_builtin_register(void) {
    int count;
    const br_component_desc *const *descs = br_builtin_descs(&count);
    setup();
    CHECK_EQ_INT(count, 9);
    CHECK(strcmp(descs[0]->name, "Name") == 0 && strcmp(descs[6]->name, "Bounds") == 0);
    CHECK(strcmp(descs[8]->name, "Tilemap") == 0 && ID.tilemap != BR_COMPONENT_INVALID);
    CHECK(strcmp(descs[7]->name, "Sound") == 0 && ID.sound != BR_COMPONENT_INVALID);
    CHECK(ID.transform != BR_COMPONENT_INVALID && ID.bounds != BR_COMPONENT_INVALID);
    CHECK(br_component_get_desc(&W, ID.shape) == descs[2]);
    {
        br_entity e = make(br_v2(1, 2));
        const br_transform *t = (const br_transform *)br_get(&W, e, ID.transform);
        const br_shape *s = (const br_shape *)br_add(&W, e, ID.shape, NULL);
        CHECK(t->scale.x == 1 && t->scale.y == 1 && t->position.y == 2);       /* defaults, then the edit */
        CHECK(s->size.x == 16 && s->color.a == 255 && s->kind == BR_SHAPE_RECT);
        CHECK(strcmp(((br_name *)br_add(&W, e, ID.name, NULL))->text, "Entity") == 0);
    }
    br_world_destroy(&W);
}

static void test_builtin_motion(void) {
    br_entity e, b;
    br_motion *m;
    setup();
    e = make(br_v2(0, 0));
    m = (br_motion *)br_add(&W, e, ID.motion, NULL);
    m->velocity = br_v2(10, -4);
    m->spin = 90;
    br_builtin_step(&W, 0.5f);
    {
        const br_transform *t = (const br_transform *)br_get(&W, e, ID.transform);
        CHECK_NEAR(t->position.x, 5, 1e-5);
        CHECK_NEAR(t->position.y, -2, 1e-5);
        CHECK_NEAR(t->rotation, 45, 1e-4);
    }
    /* bounce off the right edge */
    b = make(br_v2(318, 50));
    ((br_motion *)br_add(&W, b, ID.motion, NULL))->velocity = br_v2(100, 0);
    br_add(&W, b, ID.bounds, NULL);                            /* 0,0 .. 320,180 */
    br_builtin_step(&W, 0.1f);
    CHECK_NEAR(((br_transform *)br_get(&W, b, ID.transform))->position.x, 320, 1e-4);
    CHECK(((br_motion *)br_get(&W, b, ID.motion))->velocity.x < 0);
    /* wrap instead */
    ((br_bounds *)br_get(&W, b, ID.bounds))->wrap = true;
    ((br_motion *)br_get(&W, b, ID.motion))->velocity = br_v2(100, 0);
    br_builtin_step(&W, 0.1f);
    CHECK_NEAR(((br_transform *)br_get(&W, b, ID.transform))->position.x, 10, 1e-3);
    br_world_destroy(&W);
}

static void test_builtin_geometry(void) {
    br_entity e;
    br_vec2 c[4];
    br_transform *t;
    setup();
    e = make(br_v2(100, 100));
    ((br_shape *)br_add(&W, e, ID.shape, NULL))->size = br_v2(40, 10);
    CHECK(br_builtin_hit(&W, e, br_v2(118, 103)));
    CHECK(!br_builtin_hit(&W, e, br_v2(100, 110)));
    t = (br_transform *)br_get(&W, e, ID.transform);
    t->rotation = 90;                                          /* now tall */
    CHECK(br_builtin_hit(&W, e, br_v2(100, 118)));
    CHECK(!br_builtin_hit(&W, e, br_v2(118, 100)));
    t->rotation = 0;
    t->scale = br_v2(2, 2);
    CHECK(br_builtin_hit(&W, e, br_v2(138, 108)));
    CHECK(br_builtin_box(&W, e, c));
    CHECK_NEAR(c[0].x, 60, 1e-3);
    CHECK_NEAR(c[2].y, 110, 1e-3);
    CHECK(!br_builtin_box(&W, br_entity_create(&W), c));      /* no transform */
    br_world_destroy(&W);
}

static br_color px(const br_image *img, int x, int y) { return br_image_get(img, x, y); }

static void test_builtin_draw(void) {
    br_image img, tex;
    br_renderer r;
    br_entity a, b, l, s;
    setup();
    br_image_init(&img, 64, 64);
    br_renderer_init(&r, &img);
    br_clear(&r, BR_BLACK);
    a = make(br_v2(20, 20));
    b = make(br_v2(24, 20));
    { br_shape *sh = (br_shape *)br_add(&W, a, ID.shape, NULL); sh->color = BR_RED; sh->layer = 2; }
    { br_shape *sh = (br_shape *)br_add(&W, b, ID.shape, NULL); sh->color = BR_BLUE; sh->kind = BR_SHAPE_ELLIPSE; }
    l = make(br_v2(32, 50));
    br_add(&W, l, ID.label, NULL);
    br_builtin_draw(&W, &r);
    CHECK(br_color_eq(px(&img, 20, 20), BR_RED));               /* layer 2 over layer 0 */
    CHECK(br_color_eq(px(&img, 30, 20), BR_BLUE));              /* the ellipse sticks out */
    CHECK(br_color_eq(px(&img, 5, 5), BR_BLACK));
    {
        int x, lit = 0;
        for (x = 0; x < 64; ++x) lit += px(&img, x, 50).r > 200 || px(&img, x, 49).r > 200;
        CHECK(lit > 4);                                         /* "Text" was drawn */
    }
    /* sprites come through the asset cache, relative to its root */
    br_image_init(&tex, 4, 4);
    br_image_fill(&tex, BR_GREEN);
    CHECK(br_image_save_bmp(&tex, test_path("sprite_test.bmp")));
    br_image_destroy(&tex);
    br_assets_set_root(g_out_dir);
    s = make(br_v2(50, 10));
    strcpy(((br_sprite *)br_add(&W, s, ID.sprite, NULL))->image, "sprite_test.bmp");
    br_clear(&r, BR_BLACK);
    br_builtin_draw(&W, &r);
    CHECK(br_color_eq(px(&img, 50, 10), BR_GREEN));
    CHECK_EQ_INT(br_assets_count(), 1);
    CHECK(br_asset_image("sprite_test.bmp") == br_asset_image("sprite_test.bmp"));   /* loaded once */
    strcpy(((br_sprite *)br_get(&W, s, ID.sprite))->image, "missing.bmp");
    QUIET(br_builtin_draw(&W, &r));                             /* placeholder, no crash */
    CHECK(br_asset_image("missing.bmp") == NULL);
    br_assets_clear();
    br_assets_set_root("");
    br_builtin_release();
    br_image_destroy(&img);
    br_world_destroy(&W);
}

static br_engine g_fake_engine;    /* only its mixer is used */

static void test_sound_component(void) {
    br_sound tone;
    br_entity a, b, c;
    br_sound_src *src;
    br_mixer *m = &g_fake_engine.mixer;
    setup();
    CHECK(br_sound_generate(&tone, BR_WAVE_SINE, 440, 0.1f, 0.5f, 22050));
    CHECK(br_sound_save_wav(&tone, test_path("sound_test.wav")));
    br_sound_destroy(&tone);
    br_assets_set_root(g_out_dir);
    a = br_entity_create(&W);
    src = (br_sound_src *)br_add(&W, a, ID.sound, NULL);
    CHECK(src->play_on_start && !src->loop && src->volume == 1 && src->voice == 0);
    strcpy(src->file, "sound_test.wav");
    src->loop = true;
    br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60);   /* no engine: nothing, no crash */
    CHECK(!src->started);
    br_mixer_init(m, 44100);
    W.context = &g_fake_engine;
    br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60);
    src = (br_sound_src *)br_get(&W, a, ID.sound);
    CHECK(src->started && src->voice != 0 && br_voice_playing(m, src->voice));
    br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60);   /* plays once per start */
    CHECK_EQ_INT((int)br_mixer_active_voices(m), 1);
    b = br_entity_clone(&W, a);                                 /* the clone gets its own voice */
    CHECK(((br_sound_src *)br_get(&W, b, ID.sound))->voice == 0);
    br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60);
    CHECK_EQ_INT((int)br_mixer_active_voices(m), 2);
    c = br_entity_create(&W);                                   /* on request only */
    src = (br_sound_src *)br_add(&W, c, ID.sound, NULL);
    src->play_on_start = false;
    strcpy(src->file, "sound_test.wav");
    br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60);
    CHECK(((br_sound_src *)br_get(&W, c, ID.sound))->voice == 0);
    CHECK(br_builtin_sound_play(&W, c));
    br_builtin_sound_stop(&W, c);
    CHECK(((br_sound_src *)br_get(&W, c, ID.sound))->voice == 0);
    {
        float buf[2 * 2048];
        br_entity_destroy(&W, a);                               /* destroying stops its sound */
        br_mixer_mix_f32(m, buf, 2048);                         /* let the fade-out finish */
        CHECK_EQ_INT((int)br_mixer_active_voices(m), 1);
        br_world_destroy(&W);                                   /* ...and so does the world */
        br_mixer_mix_f32(m, buf, 2048);
        CHECK_EQ_INT((int)br_mixer_active_voices(m), 0);
    }
    QUIET(CHECK(br_asset_sound("missing.wav") == NULL));
    br_assets_clear();
    br_assets_set_root("");
}

/* Tilemap: drawing from a tileset, the cell under a point, solid tiles, scene files. */
static void test_tilemap(void) {
    br_image img, set;
    br_renderer r;
    br_entity root, e;
    br_tilemap *m;
    br_vec2 c;
    int col, row, i;
    char *text;
    br_entity made[4];
    setup();
    br_image_init(&set, 8, 8);                                   /* a 2x2 tileset of 4-pixel tiles */
    for (i = 0; i < 64; ++i) set.pixels[i] = (i % 8) < 4 ? ((i / 8) < 4 ? BR_RED : BR_BLUE) : ((i / 8) < 4 ? BR_GREEN : BR_WHITE);
    CHECK(br_image_save_bmp(&set, test_path("tiles_test.bmp")));
    br_image_destroy(&set);
    br_assets_set_root(g_out_dir);
    root = make(br_v2(100, 100));                                /* a map follows its parent */
    e = make(br_v2(-80, -80));
    br_set_parent(&W, e, root, false);
    m = (br_tilemap *)br_add(&W, e, ID.tilemap, NULL);
    CHECK(m->tile_size == 16 && m->columns == 16 && m->rows == 11 && m->tint.a == 255);
    strcpy(m->tileset, "tiles_test.bmp");
    m->tile_size = 4;
    m->columns = 3;
    m->rows = 2;
    strcpy(m->legend, "rgbw");                                   /* r: tile 0 (red), g: 1, b: 2, w: 3 */
    strcpy(m->solid, "bw");
    strcpy(m->tiles, "rgbw.r");                                  /* '.' isn't in the legend: nothing there */
    br_image_init(&img, 64, 64);
    br_renderer_init(&r, &img);
    br_clear(&r, BR_BLACK);
    br_builtin_draw(&W, &r);                                     /* the map spans x 14..26, y 16..24 */
    CHECK(br_color_eq(px(&img, 15, 17), BR_RED) && br_color_eq(px(&img, 19, 17), BR_GREEN));
    CHECK(br_color_eq(px(&img, 23, 17), BR_BLUE) && br_color_eq(px(&img, 15, 21), BR_WHITE));
    CHECK(br_color_eq(px(&img, 19, 21), BR_BLACK) && br_color_eq(px(&img, 23, 21), BR_RED));
    CHECK(br_color_eq(px(&img, 13, 17), BR_BLACK));
    CHECK(br_tilemap_get(&W, e, br_v2(19, 17), &col, &row) == 'g' && col == 1 && row == 0);
    CHECK(br_tilemap_get(&W, e, br_v2(13, 17), &col, &row) == 0 && col == -1);   /* outside */
    CHECK(br_tilemap_solid(&W, e, br_v2(23, 17)) && !br_tilemap_solid(&W, e, br_v2(15, 17)));
    CHECK(!br_tilemap_solid(&W, e, br_v2(40, 40)));
    CHECK(br_tilemap_cell_center(&W, e, 2, 1, &c) && c.x == 24 && c.y == 22);
    CHECK(br_tilemap_set(&W, e, br_v2(19, 21), 'b') && br_tilemap_solid(&W, e, br_v2(19, 21)));
    CHECK(!br_tilemap_set(&W, e, br_v2(1, 1), 'b'));
    {   /* bounds for the editor's picking */
        br_vec2 k[4];
        CHECK(br_builtin_box(&W, e, k) && k[0].x == 14 && k[0].y == 16 && k[2].x == 26 && k[2].y == 24);
    }
    /* a long map survives a scene file */
    m = (br_tilemap *)br_get(&W, e, ID.tilemap);
    m->columns = 40;
    m->rows = 40;
    for (i = 0; i < 1600; ++i) m->tiles[i] = "rgbw"[i % 4];
    m->tiles[1600] = 0;
    text = br_world_to_text(&W, &e, 1, NULL);
    CHECK(text != NULL);
    if (text) {
        br_world_clear(&W);
        CHECK(br_world_from_text(&W, text, made, 4, NULL, 0) == 1);
        m = (br_tilemap *)br_get(&W, made[0], ID.tilemap);
        CHECK(m && strlen(m->tiles) == 1600 && m->tiles[1599] == 'w' && strcmp(m->legend, "rgbw") == 0);
        br_free(text);
    }
    br_image_destroy(&img);
    br_builtin_release();
    br_assets_clear();
    br_assets_set_root("");
    br_world_destroy(&W);
}

void suite_components(void) {
    RUN(test_tilemap);
    RUN(test_builtin_register);
    RUN(test_sound_component);
    RUN(test_builtin_motion);
    RUN(test_builtin_geometry);
    RUN(test_builtin_draw);
}
