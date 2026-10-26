/* Tests: what the engine could do that scripts and the editor now can too - sprite
 * sheets, blending, Animation, Camera (following, limits, HUD layers, shaking),
 * tilemap boxes and cells, draw() from scripts, voices, music and tones, broadcast,
 * the scene stack and globals, actions, the window, and the editor's play stack. */
#include "test.h"
#include "editor.h"
#include <string.h>

static br_world W;
static br_builtin_ids ID;
static br_component SCRIPT;
static br_engine g_engine;          /* scripts need world->context: input, mixer, framebuffer, time */

static void setup(void) {
    memset(&g_engine, 0, sizeof(g_engine));
    br_input_init(&g_engine.input);
    br_mixer_init(&g_engine.mixer, 22050);
    br_image_init(&g_engine.framebuffer, 64, 48);
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    W.context = &g_engine;
    SCRIPT = br_component_find(&W, "Script");
    br_assets_set_root(g_out_dir);
}

static void teardown(void) {
    br_world_destroy(&W);
    br_script_reset_session();
    br_image_destroy(&g_engine.framebuffer);
    br_builtin_release();
    br_assets_clear();
    br_assets_set_root("");
}

static br_entity make(br_vec2 pos) {
    br_entity e = br_entity_create(&W);
    ((br_transform *)br_add(&W, e, ID.transform, NULL))->position = pos;
    return e;
}

static br_entity scripted(const char *name, const char *file, const char *source) {
    br_entity e = make(br_v2(0, 0));
    br_script *s;
    if (source[0]) CHECK(br_file_write_all(test_path(file), source, strlen(source)));   /* "": the file as it is */
    snprintf(((br_name *)br_add(&W, e, ID.name, NULL))->text, 32, "%s", name);
    s = (br_script *)br_add(&W, e, SCRIPT, NULL);
    snprintf(s->file, sizeof(s->file), "%s", file);
    return e;
}

static void steps(int n) { while (n-- > 0) br_world_run_phase(&W, BR_PHASE_FIXED_UPDATE, 1.0f / 60.0f); }

static double num(br_entity e, const char *var) {
    double v = -12345;
    CHECK(br_script_get_number(&W, e, var, &v));
    return v;
}

static br_color px(const br_image *img, int x, int y) { return br_image_get(img, x, y); }

/* A 4x2-frame sheet of 4x4 frames, each frame one colour (frame i: red = 10 + i * 20), with a
 * marker pixel at its top-left corner for flipping. */
static void write_sheet(void) {
    br_image img;
    int x, y;
    br_image_init(&img, 16, 8);
    for (y = 0; y < 8; ++y)
        for (x = 0; x < 16; ++x) {
            int frame = (y / 4) * 4 + x / 4;
            br_color c = br_rgb((uint8_t)(10 + frame * 20), 50, 50);
            if (x % 4 == 0 && y % 4 == 0) c = br_rgb(255, 255, 255);   /* the marker */
            br_image_set(&img, x, y, c);
        }
    CHECK(br_image_save_bmp(&img, test_path("sheet.bmp")));
    br_image_destroy(&img);
}

static void test_sprite_frames_and_blend(void) {
    br_image img;
    br_renderer r;
    br_entity e;
    br_sprite *sp;
    br_shape *sh;
    br_vec2 k[4];
    setup();
    write_sheet();
    e = make(br_v2(10, 10));
    sp = (br_sprite *)br_add(&W, e, ID.sprite, NULL);
    CHECK(sp->frame_size.x == 0 && sp->frame == 0 && !sp->flip_x && sp->blend == BR_BLEND_ALPHA);
    strcpy(sp->image, "sheet.bmp");
    sp->frame_size = br_v2(4, 4);
    sp->frame = 5;                                   /* second row, second column */
    br_image_init(&img, 32, 32);
    br_renderer_init(&r, &img);
    br_clear(&r, BR_BLACK);
    br_builtin_draw(&W, &r);                         /* covers 8..12 x 8..12 */
    CHECK(px(&img, 9, 9).r == 10 + 5 * 20 && px(&img, 8, 8).r == 255 && px(&img, 7, 7).r == 0 && px(&img, 12, 12).r == 0);
    CHECK(br_builtin_box(&W, e, k) && k[0].x == 8 && k[2].x == 12);   /* the frame's size, not the image's */
    sp->flip_x = true;                               /* the marker moves to the right */
    br_clear(&r, BR_BLACK);
    br_builtin_draw(&W, &r);
    CHECK(px(&img, 11, 8).r == 255 && px(&img, 8, 8).r == 10 + 5 * 20);
    sp->frame = 99;                                  /* past the end: the last frame */
    sp->flip_x = false;
    br_clear(&r, BR_BLACK);
    br_builtin_draw(&W, &r);
    CHECK(px(&img, 9, 9).r == 10 + 7 * 20);
    /* additive blending adds; multiply darkens */
    br_remove(&W, e, ID.sprite);
    sh = (br_shape *)br_add(&W, e, ID.shape, NULL);
    sh->size = br_v2(4, 4);
    sh->color = br_rgb(100, 0, 0);
    sh->blend = BR_BLEND_ADD;
    br_clear(&r, br_rgb(50, 20, 0));
    br_builtin_draw(&W, &r);
    CHECK(px(&img, 9, 9).r == 150 && px(&img, 9, 9).g == 20);
    sh->blend = BR_BLEND_MULTIPLY;
    sh->color = br_rgb(128, 255, 0);
    br_clear(&r, br_rgb(200, 100, 100));
    br_builtin_draw(&W, &r);
    CHECK(px(&img, 9, 9).r > 95 && px(&img, 9, 9).r < 105 && px(&img, 9, 9).g == 100 && px(&img, 9, 9).b == 0);
    CHECK(r.blend == BR_BLEND_ALPHA);                /* put back afterwards */
    br_image_destroy(&img);
    teardown();
}

static void test_animation(void) {
    int out[16];
    br_entity e;
    br_sprite *sp;
    br_animation *an;
    CHECK(br_animation_parse("0-3", out, 16) == 4 && out[3] == 3);
    CHECK(br_animation_parse("4,5, 6,5", out, 16) == 4 && out[2] == 6 && out[3] == 5);
    CHECK(br_animation_parse("3-1, 7", out, 16) == 4 && out[0] == 3 && out[2] == 1 && out[3] == 7);
    CHECK(br_animation_parse("", out, 16) == 0 && br_animation_parse("a", out, 16) < 0 && br_animation_parse("1-", out, 16) < 0);
    setup();
    e = make(br_v2(0, 0));
    sp = (br_sprite *)br_add(&W, e, ID.sprite, NULL);
    an = (br_animation *)br_add(&W, e, ID.animation, NULL);
    strcpy(an->frames, "2-4");
    an->fps = 10;                                    /* 6 steps a frame at 60 steps a second */
    an->loop = true;
    steps(1);
    CHECK(sp->frame == 2);
    steps(6);
    CHECK(sp->frame == 3);
    steps(12);
    CHECK(sp->frame == 2);                           /* round again */
    an->loop = false;
    strcpy(an->frames, "7,8");                       /* a new list starts from its first */
    steps(1);
    CHECK(sp->frame == 7 && an->playing);
    steps(30);
    CHECK(sp->frame == 8 && !an->playing);           /* once, holding the last */
    teardown();

    setup();                                         /* from a script */
    {
        br_entity s = scripted("hero", "anim.bs",
            "var f: int = -1\n"
            "func start() -> void { play_animation(self, \"5-6\", 20, true) }\n"
            "func update(dt: number) -> void { f = self.Sprite.frame }\n");
        br_add(&W, s, ID.sprite, NULL);
        steps(1);
        CHECK(num(s, "f") == 5 && br_has(&W, s, ID.animation));
        steps(3);
        CHECK(num(s, "f") == 6);
    }
    teardown();
}

static void test_camera(void) {
    br_entity cam_e, hero;
    br_camera *cam;
    br_camera2d view;
    int ui;
    setup();
    CHECK(!br_camera_view(&W, 64, 48, &view, &ui) && view.target.x == 32 && ui > 1000);   /* none: as before */
    hero = make(br_v2(100, 30));
    cam_e = make(br_v2(0, 0));
    cam = (br_camera *)br_add(&W, cam_e, ID.camera, NULL);
    CHECK(cam->current && cam->zoom == 1 && cam->ui_layer == 50 && cam->pixel_snap);
    cam->follow = hero;
    CHECK(br_camera_current(&W) == cam_e);
    steps(1);                                        /* the first step jumps there */
    CHECK(br_world_transform(&W, cam_e).position.x == 100);
    cam->limit = true;                               /* a room 0..200 x 0..48 */
    cam->limit_min = br_v2(0, 0);
    cam->limit_max = br_v2(200, 48);
    ((br_transform *)br_get(&W, hero, ID.transform))->position = br_v2(190, 10);
    steps(1);
    CHECK(br_world_transform(&W, cam_e).position.x == 168 && br_world_transform(&W, cam_e).position.y == 24);   /* stops at the edges */
    cam->deadzone = br_v2(20, 0);                    /* moving within the deadzone doesn't move it */
    ((br_transform *)br_get(&W, hero, ID.transform))->position = br_v2(150, 24);
    steps(1);
    CHECK(br_world_transform(&W, cam_e).position.x == 168);
    ((br_transform *)br_get(&W, hero, ID.transform))->position = br_v2(120, 24);
    steps(1);
    CHECK(br_world_transform(&W, cam_e).position.x == 140);
    cam->deadzone = br_v2(0, 0);
    cam->smoothing = 0.5f;                           /* smoothing: part of the way each step */
    ((br_transform *)br_get(&W, hero, ID.transform))->position = br_v2(60, 24);
    steps(1);
    CHECK(br_world_transform(&W, cam_e).position.x < 140 && br_world_transform(&W, cam_e).position.x > 60);
    br_camera_snap(&W);
    CHECK(br_world_transform(&W, cam_e).position.x == 60);
    CHECK(br_camera_view(&W, 64, 48, &view, &ui) && view.target.x == 60 && ui == 50);
    br_camera_shake(&W, 3, 0.5f);
    steps(1);
    CHECK(br_camera_view(&W, 64, 48, &view, &ui) && (view.target.x != 60 || view.target.y != 24));
    CHECK(br_absf(view.target.x - 60) <= 3 && br_absf(view.target.y - 24) <= 3);
    steps(40);
    CHECK(br_camera_view(&W, 64, 48, &view, &ui) && view.target.x == 60 && view.target.y == 24);   /* it settles */
    cam->current = false;
    CHECK(br_camera_current(&W) == BR_ENTITY_NULL);
    teardown();
}

/* The world through the camera, the HUD layers on the screen. */
static void test_camera_draw(void) {
    br_entity cam_e, block, hud;
    br_shape *s;
    br_camera *cam;
    br_camera2d view;
    int ui;
    br_renderer *r = &g_engine.renderer;
    setup();
    br_renderer_init(r, &g_engine.framebuffer);
    block = make(br_v2(100, 20));                    /* in the world */
    s = (br_shape *)br_add(&W, block, ID.shape, NULL);
    s->size = br_v2(4, 4);
    s->color = BR_RED;
    hud = make(br_v2(4, 4));                         /* on the screen */
    s = (br_shape *)br_add(&W, hud, ID.shape, NULL);
    s->size = br_v2(4, 4);
    s->color = BR_GREEN;
    s->layer = 60;
    cam_e = make(br_v2(100, 20));
    cam = (br_camera *)br_add(&W, cam_e, ID.camera, NULL);
    CHECK(br_camera_view(&W, 64, 48, &view, &ui));
    br_clear(r, BR_BLACK);
    br_builtin_draw_layers(&W, r, &view, NULL, ui);
    CHECK(br_color_eq(px(&g_engine.framebuffer, 32, 24), BR_RED));     /* the world point at the middle */
    CHECK(br_color_eq(px(&g_engine.framebuffer, 4, 4), BR_GREEN));     /* the HUD where it was put */
    CHECK(!r->camera_active);                                          /* the renderer is left as it was */
    cam->zoom = 2;
    CHECK(br_camera_view(&W, 64, 48, &view, &ui));
    br_clear(r, BR_BLACK);
    br_builtin_draw_layers(&W, r, &view, NULL, ui);
    CHECK(br_color_eq(px(&g_engine.framebuffer, 29, 21), BR_RED) && br_color_eq(px(&g_engine.framebuffer, 27, 19), BR_BLACK));
    br_world_run_phase(&W, BR_PHASE_RENDER, 0);                        /* the draw system does the same */
    CHECK(br_color_eq(px(&g_engine.framebuffer, 29, 21), BR_RED) && br_color_eq(px(&g_engine.framebuffer, 4, 4), BR_GREEN));
    teardown();
}

static void test_tile_boxes_and_cells(void) {
    br_entity m;
    br_tilemap *t;
    setup();
    m = make(br_v2(32, 16));                         /* 4x2 cells of 16: x 0..64, y 0..32 */
    t = (br_tilemap *)br_add(&W, m, ID.tilemap, NULL);
    t->tile_size = 16;
    t->columns = 4;
    t->rows = 2;
    strcpy(t->solid, "#");
    strcpy(t->tiles, "#...\n..#.");
    strcpy(t->tiles, "#.....#.");
    CHECK(br_tilemap_solid_box(&W, m, br_v2(19, 8), br_v2(8, 8)));     /* 15..23: reaches into cell (0, 0) */
    CHECK(!br_tilemap_solid_box(&W, m, br_v2(24, 8), br_v2(16, 8)));   /* its left edge on the cell's edge: not inside */
    CHECK(br_tilemap_solid_box(&W, m, br_v2(40, 24), br_v2(2, 2)));    /* cell (2, 1) */
    CHECK(!br_tilemap_solid_box(&W, m, br_v2(200, 200), br_v2(4, 4)));
    CHECK(br_tilemap_cell(&W, m, 2, 1) == '#' && br_tilemap_cell(&W, m, 9, 9) == 0);
    CHECK(br_tilemap_set_cell(&W, m, 3, 1, '#') && br_tilemap_cell(&W, m, 3, 1) == '#' && !br_tilemap_set_cell(&W, m, 4, 0, '#'));
    {
        br_entity s = scripted("probe", "tiles2.bs",
            "var hit: bool\nvar cell: vec2\nvar ch: string\nvar mid: vec2\n"
            "func start() -> void {\n"
            "    var m := find(\"map\")\n"
            "    hit = tile_box_solid(m, vec2(40, 24), vec2(2, 2))\n"
            "    cell = tile_cell(m, vec2(40, 24))\n"
            "    tile_put(m, 1, 0, \"#\")\n"
            "    ch = tile_get(m, 1, 0)\n"
            "    mid = tile_cell_center(m, 1, 0)\n"
            "}\n");
        char buf[32];
        snprintf(((br_name *)br_add(&W, m, ID.name, NULL))->text, 32, "map");
        steps(1);
        CHECK(num(s, "hit") == 1);
        CHECK(br_script_get_string(&W, s, "cell", buf, sizeof(buf)) && strcmp(buf, "(2, 1)") == 0);
        CHECK(br_script_get_string(&W, s, "ch", buf, sizeof(buf)) && strcmp(buf, "#") == 0);
        CHECK(br_script_get_string(&W, s, "mid", buf, sizeof(buf)) && strcmp(buf, "(24, 8)") == 0);
    }
    teardown();
}

static void test_script_draw(void) {
    br_renderer *r = &g_engine.renderer;
    br_entity s, block;
    br_shape *sh;
    char err[256];
    setup();
    br_renderer_init(r, &g_engine.framebuffer);
    block = make(br_v2(10, 10));                     /* layer 5: between the two draw() layers */
    sh = (br_shape *)br_add(&W, block, ID.shape, NULL);
    sh->size = br_v2(8, 8);
    sh->color = BR_BLUE;
    sh->layer = 5;
    s = scripted("painter", "paint.bs",
        "var draw_layer: int = 10\n"
        "var drawn: int = 0\n"
        "func draw() -> void {\n"
        "    drawn += 1\n"
        "    draw_rect(vec2(6, 6), vec2(4, 4), \"#ff0000\")\n"
        "    draw_rect(vec2(20, 20), vec2(6, 6), [0, 255, 0], false)\n"
        "    draw_line(vec2(40, 2), vec2(40, 10), rgb(255, 255, 0))\n"
        "    draw_circle(vec2(50, 30), 3, \"#ffffff\")\n"
        "    draw_text(\"A\", vec2(30, 36), \"#ffffff\")\n"
        "    var size := text_size(\"AB\")\n"
        "    if size.x != 16 { draw_rect(vec2(0, 0), vec2(64, 48), \"#000000\") }\n"
        "}\n");
    steps(1);                                        /* (draw() runs once the script has started) */
    br_clear(r, BR_BLACK);
    br_builtin_draw(&W, r);
    CHECK(num(s, "drawn") == 1);
    CHECK(br_color_eq(px(&g_engine.framebuffer, 7, 7), br_rgb(255, 0, 0)));          /* over the blue block */
    CHECK(br_color_eq(px(&g_engine.framebuffer, 12, 12), BR_BLUE));
    CHECK(br_color_eq(px(&g_engine.framebuffer, 20, 23), br_rgb(0, 255, 0)) && br_color_eq(px(&g_engine.framebuffer, 23, 23), BR_BLACK));
    CHECK(br_color_eq(px(&g_engine.framebuffer, 40, 5), br_rgb(255, 255, 0)) && br_color_eq(px(&g_engine.framebuffer, 50, 30), BR_WHITE));
    br_script_set_number(&W, s, "draw_layer", 0);    /* now under the block */
    br_clear(r, BR_BLACK);
    br_builtin_draw(&W, r);
    CHECK(br_color_eq(px(&g_engine.framebuffer, 7, 7), BR_BLUE));
    teardown();

    setup();                                         /* drawing outside draw() is a mistake */
    s = scripted("bad", "bad.bs", "func update(dt) { draw_rect(vec2(0, 0), vec2(1, 1), \"#fff\") }\n");
    QUIET(steps(1));
    CHECK(strstr(br_script_last_error(&W), "draw_rect() only works inside draw()") != NULL);
    teardown();
    CHECK(!br_script_check("func draw(x: number) -> void { }\n", "d.bs", err, sizeof(err)) && strstr(err, "the engine passes"));
    CHECK(!br_script_check("func on_resume() -> int { return 1 }\n", "d.bs", err, sizeof(err)) && strstr(err, "ignores what on_resume() returns"));
}

/* A short WAV to play. */
static void write_beep(const char *name, float seconds) {
    br_sound snd;
    CHECK(br_sound_generate(&snd, BR_WAVE_SQUARE, 440, seconds, 0.5f, 22050));
    CHECK(br_sound_save_wav(&snd, test_path(name)));
    br_sound_destroy(&snd);
}

static void test_voices_music_tones(void) {
    br_entity s;
    float buf[2048];
    setup();
    write_beep("beep.wav", 0.5f);
    write_beep("song_a.wav", 2);
    write_beep("song_b.wav", 2);
    s = scripted("dj", "dj.bs",
        "var v: number = 0\nvar playing: bool\nvar after: bool = true\nvar len: number\nvar tone: number\nvar track: string\n"
        "func start() -> void {\n"
        "    v = play_sound(\"beep.wav\", 0.8, 2, -0.5)\n"
        "    playing = sound_playing(v)\n"
        "    set_sound_volume(v, 0.3)\n"
        "    set_sound_pan(v, 1)\n"
        "    set_sound_pitch(v, 0.5)\n"
        "    len = sound_length(\"beep.wav\")\n"
        "    tone = play_tone(\"triangle\", 880, 0.1)\n"
        "    play_music(\"song_a.wav\", 0.5)\n"
        "    track = music()\n"
        "}\n"
        "func stop_it() -> void { stop_sound(v) }\n"
        "func check() -> void { after = sound_playing(v) }\n"
        "func next_song() -> void { play_music(\"song_b.wav\", 1, 1) }\n"
        "func same_song() -> void { play_music(\"song_b.wav\", 0.25) }\n");
    steps(1);
    CHECK(num(s, "v") > 0 && num(s, "playing") == 1 && num(s, "tone") > 0);
    CHECK_NEAR(num(s, "len"), 0.5, 0.01);
    {
        br_voice v = (br_voice)num(s, "v");
        const br_voice_state *vs = &g_engine.mixer.voices[v & 0xFF];
        CHECK_NEAR(vs->params.volume, 0.3, 1e-6);
        CHECK_NEAR(vs->params.pan, 1, 1e-6);
        CHECK_NEAR(vs->params.pitch, 0.5, 1e-6);
    }
    CHECK(strcmp(br_script_music(), "song_a.wav") == 0);
    br_script_call(&W, s, "stop_it", NULL, 0);
    br_mixer_mix_f32(&g_engine.mixer, buf, 1024);    /* (stopping fades out over a few ms) */
    br_script_call(&W, s, "check", NULL, 0);
    CHECK(num(s, "after") == 0);
    br_script_call(&W, s, "next_song", NULL, 0);     /* a crossfade over a second */
    CHECK(strcmp(br_script_music(), "song_b.wav") == 0);
    {
        int i, music = 0;
        for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) music += g_engine.mixer.voices[i].active && g_engine.mixer.voices[i].params.bus == BR_BUS_MUSIC;
        CHECK(music == 2);                           /* the old one still fading out */
        g_engine.time = 2;                           /* ... a second and more later */
        steps(1);
        br_mixer_mix_f32(&g_engine.mixer, buf, 1024);
        music = 0;
        for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) music += g_engine.mixer.voices[i].active && g_engine.mixer.voices[i].params.bus == BR_BUS_MUSIC;
        CHECK(music == 1);
        br_script_call(&W, s, "same_song", NULL, 0); /* the same track: only its volume */
        steps(1);
        for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i)
            if (g_engine.mixer.voices[i].active && g_engine.mixer.voices[i].params.bus == BR_BUS_MUSIC) CHECK_NEAR(g_engine.mixer.voices[i].params.volume, 0.25, 1e-6);
    }
    br_world_destroy(&W);                            /* the music outlives the scene ... */
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    W.context = &g_engine;
    CHECK(strcmp(br_script_music(), "song_b.wav") == 0);
    br_script_reset_session();                       /* ... until the session ends */
    CHECK(br_script_music()[0] == 0);
    br_mixer_stop_all(&g_engine.mixer);
    teardown();
}

static void test_broadcast_and_globals(void) {
    br_entity a, b, c;
    char buf[64];
    setup();
    a = scripted("a", "listen.bs", "var got: number = 0\nfunc boss_beaten(n: number) -> void { got += n }\n");
    b = scripted("b", "listen.bs", "");
    c = scripted("c", "shout.bs",
        "var heard: int = 0\n"
        "func start() -> void {\n"
        "    heard = broadcast(\"boss_beaten\", 3)\n"
        "    broadcast(\"nobody_has_this\")\n"
        "    set_global(\"coins\", 12)\n"
        "    set_global(\"spot\", [vec2(1, 2), \"room_3\", true])\n"
        "}\n");
    steps(1);
    CHECK(num(c, "heard") == 2 && num(a, "got") == 3 && num(b, "got") == 3);
    teardown();

    /* A new scene: the globals are still there (teardown ended the session, so set them again
     * without one). */
    setup();
    scripted("w", "write.bs", "func start() -> void {\n set_global(\"coins\", 12)\n set_global(\"spot\", [vec2(1, 2), \"room_3\"])\n}\n");
    steps(1);
    br_world_destroy(&W);
    br_world_init(&W);
    br_builtin_register(&W, &ID);
    W.context = &g_engine;
    SCRIPT = br_component_find(&W, "Script");
    c = scripted("c", "read.bs",
        "var coins: number = 0\nvar room: string\nvar missing: number\nvar had: bool\nvar gone: bool\n"
        "func start() -> void {\n"
        "    coins = global(\"coins\", 0)\n"
        "    var spot: list = global(\"spot\")\n"
        "    room = spot[1]\n"
        "    missing = global(\"nope\", 7)\n"
        "    had = has_global(\"coins\")\n"
        "    delete_global(\"coins\")\n"
        "    gone = not has_global(\"coins\")\n"
        "}\n");
    steps(1);
    CHECK(num(c, "coins") == 12 && num(c, "missing") == 7 && num(c, "had") == 1 && num(c, "gone") == 1);
    CHECK(br_script_get_string(&W, c, "room", buf, sizeof(buf)) && strcmp(buf, "room_3") == 0);
    teardown();
}

/* A key pressed and let go within one frame is still a press of its action (a quick tap,
 * or a slow frame). */
static void test_action_tap(void) {
    br_entity s;
    br_event ev;
    setup();
    s = scripted("p", "tap.bs",
        "var presses: int = 0\nvar downs: int = 0\n"
        "func start() -> void { bind(\"pause\", \"enter\") }\n"
        "func update(dt: number) -> void {\n"
        "    if action_pressed(\"pause\") { presses += 1 }\n"
        "    if action_down(\"pause\") { downs += 1 }\n"
        "}\n");
    steps(1);
    br_input_begin_frame(&g_engine.input);
    ev = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_ENTER, 0, false);
    br_input_handle_event(&g_engine.input, &ev);
    ev = br_event_key(BR_EVENT_KEY_UP, BR_KEY_ENTER, 0, false);
    br_input_handle_event(&g_engine.input, &ev);
    br_input_end_events(&g_engine.input);
    g_engine.frame = 1;
    steps(1);
    CHECK(num(s, "presses") == 1 && num(s, "downs") == 1);
    br_input_begin_frame(&g_engine.input);
    br_input_end_events(&g_engine.input);
    g_engine.frame = 2;
    steps(1);
    CHECK(num(s, "presses") == 1 && num(s, "downs") == 1);
    teardown();
}

/* "shift", "ctrl" and "alt" mean either side in bind(), as in key_down(); "leftshift" one side. */
static void test_bind_either_side(void) {
    br_entity s;
    br_event ev;
    br_action c_act;
    setup();
    s = scripted("p", "sides.bs",
        "var aim: bool\nvar dash: bool\nvar edge: int = 0\n"
        "func start() -> void {\n"
        "    bind(\"aim\", \"shift\", \"q\")\n"
        "    bind(\"dash\", \"leftshift\")\n"
        "}\n"
        "func update(dt: number) -> void {\n"
        "    aim = action_down(\"aim\")\n"
        "    dash = action_down(\"dash\")\n"
        "    if action_pressed(\"aim\") { edge += 1 }\n"
        "}\n");
    steps(1);
    br_input_begin_frame(&g_engine.input);
    ev = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_RIGHT_SHIFT, BR_MOD_SHIFT, false);
    br_input_handle_event(&g_engine.input, &ev);
    br_input_end_events(&g_engine.input);
    steps(1);
    CHECK(num(s, "aim") == 1 && num(s, "dash") == 0 && num(s, "edge") == 1);
    c_act = br_input_action(&g_engine.input, "aim");                 /* the C side agrees */
    CHECK(br_action_down(&g_engine.input, c_act));
    br_input_begin_frame(&g_engine.input);
    ev = br_event_key(BR_EVENT_KEY_UP, BR_KEY_RIGHT_SHIFT, 0, false);
    br_input_handle_event(&g_engine.input, &ev);
    ev = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_LEFT_SHIFT, BR_MOD_SHIFT, false);
    br_input_handle_event(&g_engine.input, &ev);
    br_input_end_events(&g_engine.input);
    steps(1);
    CHECK(num(s, "aim") == 1 && num(s, "dash") == 1);
    teardown();
    CHECK(br_key_right_partner(BR_KEY_LEFT_ALT) == BR_KEY_RIGHT_ALT && br_key_right_partner(BR_KEY_A) == BR_KEY_UNKNOWN);
}

/* Every script's variables are set before any start() runs, so a start() can read another
 * entity's variables whichever was made first. */
static void test_start_sees_others(void) {
    br_entity a;
    setup();
    a = scripted("a", "reader.bs", "var seen: number = 0\nfunc start() -> void { seen = find(\"b\").speed }\n");
    scripted("b", "speedy.bs", "var speed: number = 7\n");
    steps(1);
    CHECK(num(a, "seen") == 7);
    teardown();
}

/* A folder isn't a file: reading one fails cleanly (some systems open it, with a huge size). */
static void test_read_folder(void) {
    size_t size = 1;
    CHECK(br_file_read_all(g_out_dir, &size) == NULL && size == 0);
}

static void test_scene_requests(void) {
    br_entity s;
    char path[64];
    bool transparent = false, below = true;
    setup();
    s = scripted("menu", "stack.bs",
        "var paused: int = 0\nvar resumed: int = 0\n"
        "func open() -> void { push_scene(\"pause.scene\", false, true) }\n"
        "func open_default() -> void { push_scene(\"map.scene\") }\n"
        "func close() -> void { pop_scene() }\n"
        "func on_pause() -> void { paused += 1 }\n"
        "func on_resume() -> void { resumed += 1 }\n");
    steps(1);
    CHECK(!br_script_push_request(&W, path, sizeof(path), &transparent, &below));
    br_script_call(&W, s, "open", NULL, 0);
    CHECK(br_script_push_request(&W, path, sizeof(path), &transparent, &below) && strcmp(path, "pause.scene") == 0 && !transparent && below);
    CHECK(!br_script_push_request(&W, path, sizeof(path), &transparent, &below));    /* taken */
    br_script_call(&W, s, "open_default", NULL, 0);
    CHECK(br_script_push_request(&W, path, sizeof(path), &transparent, &below) && transparent && !below);
    br_script_call(&W, s, "close", NULL, 0);
    CHECK(br_script_pop_request(&W) && !br_script_pop_request(&W));
    CHECK(br_script_notify(&W, "on_pause") == 1 && br_script_notify(&W, "on_resume") == 1 && br_script_notify(&W, "nothing") == 0);
    CHECK(num(s, "paused") == 1 && num(s, "resumed") == 1);
    teardown();
}

static void test_actions_window_input(void) {
    br_entity s;
    br_event ev;
    setup();
    s = scripted("p", "act.bs",
        "var down: bool\nvar pressed: int = 0\nvar axis: int = 0\nvar typed: string\nvar size: vec2\nvar g: vec2\n"
        "func start() -> void {\n"
        "    bind(\"jump\", \"space\", \"z\", \"pad:a\")\n"
        "    bind(\"left\", \"left\", \"pad:left_x-\")\n"
        "    bind(\"right\", \"right\")\n"
        "    size = screen_size()\n"
        "    set_gravity(vec2(0, 900))\n"
        "    g = gravity()\n"
        "}\n"
        "func update(dt: number) -> void {\n"
        "    down = action_down(\"jump\")\n"
        "    if action_pressed(\"jump\") { pressed += 1 }\n"
        "    axis = action_axis(\"left\", \"right\")\n"
        "    var t := text_typed()\n"
        "    if t != \"\" { typed = t }\n"
        "}\n");
    steps(1);
    CHECK(num(s, "pressed") == 0);
    br_input_begin_frame(&g_engine.input);
    ev = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_Z, 0, false);
    br_input_handle_event(&g_engine.input, &ev);
    ev = br_event_key(BR_EVENT_KEY_DOWN, BR_KEY_LEFT, 0, false);
    br_input_handle_event(&g_engine.input, &ev);
    ev = br_event_make(BR_EVENT_TEXT_INPUT);
    ev.u.text.codepoint = 'h';
    br_input_handle_event(&g_engine.input, &ev);
    ev.u.text.codepoint = 0xE9;                      /* é, as UTF-8 */
    br_input_handle_event(&g_engine.input, &ev);
    br_input_end_events(&g_engine.input);
    g_engine.frame = 1;
    steps(1);
    CHECK(num(s, "down") == 1 && num(s, "pressed") == 1 && num(s, "axis") == -1);
    {
        char buf[16];
        CHECK(br_script_get_string(&W, s, "typed", buf, sizeof(buf)) && strcmp(buf, "h\xC3\xA9") == 0);
        CHECK(br_script_get_string(&W, s, "size", buf, sizeof(buf)) && strcmp(buf, "(64, 48)") == 0);
        CHECK(br_script_get_string(&W, s, "g", buf, sizeof(buf)) && strcmp(buf, "(0, 900)") == 0);
    }
    br_input_begin_frame(&g_engine.input);           /* held: no second press */
    br_input_end_events(&g_engine.input);
    steps(1);
    CHECK(num(s, "down") == 1 && num(s, "pressed") == 1);
    teardown();
    {
        char err[256];
        CHECK(br_script_check("func f() -> void { bind(\"x\", 3) }\n", "b.bs", err, sizeof(err)) == false);
    }
}

/* The editor's Play: push_scene puts a scene of its own on top; pop and Stop take it away. */
static void test_editor_play_stack(void) {
    ed_doc d;
    const char *overlay = "bronto_scene 1\n\nentity\n  Name\n    text = \"Menu\"\n  Transform\n    position = 1 2\n";
    CHECK(br_file_write_all(test_path("overlay.scene"), overlay, strlen(overlay)));
    br_assets_set_root(g_out_dir);
    ed_doc_init(&d);
    ed_doc_create(&d, "Hero", br_v2(0, 0));
    CHECK(!ed_doc_push_scene(&d, "overlay.scene", true, false));       /* only while playing */
    ed_doc_play(&d);
    CHECK(ed_doc_stack_depth(&d) == 1 && ed_doc_top_world(&d) == &d.world);
    CHECK(ed_doc_push_scene(&d, "overlay.scene", true, false));
    CHECK(ed_doc_stack_depth(&d) == 2 && ed_doc_top_world(&d) != &d.world);
    CHECK(br_world_entity_count(ed_doc_top_world(&d)) == 1);
    QUIET(CHECK(!ed_doc_push_scene(&d, "missing.scene", true, false) && ed_doc_stack_depth(&d) == 2));
    ed_doc_step(&d, 1.0f / 60);
    CHECK(ed_doc_pop_scene(&d) && ed_doc_stack_depth(&d) == 1 && !ed_doc_pop_scene(&d));
    CHECK(ed_doc_push_scene(&d, "overlay.scene", false, true));
    ed_doc_stop(&d);                                                   /* Stop takes it away */
    CHECK(ed_doc_stack_depth(&d) == 1 && d.count == 1);
    ed_doc_destroy(&d);
    br_assets_clear();
    br_assets_set_root("");
}

void suite_features(void) {
    RUN(test_sprite_frames_and_blend);
    RUN(test_animation);
    RUN(test_camera);
    RUN(test_camera_draw);
    RUN(test_tile_boxes_and_cells);
    RUN(test_script_draw);
    RUN(test_voices_music_tones);
    RUN(test_broadcast_and_globals);
    RUN(test_action_tap);
    RUN(test_bind_either_side);
    RUN(test_start_sees_others);
    RUN(test_read_folder);
    RUN(test_scene_requests);
    RUN(test_actions_window_input);
    RUN(test_editor_play_stack);
}
