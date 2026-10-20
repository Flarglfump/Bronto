/* Tests: renderer, image codecs, audio. */
#include "test.h"
#include <string.h>

/* ======================= render ========================================= */

static int count_color(const br_image *img, br_color c) {
    int i, n = 0;
    for (i = 0; i < img->width * img->height; ++i) n += br_color_eq(img->pixels[i], c);
    return n;
}

static int count_not(const br_image *img, br_color c) {
    return img->width * img->height - count_color(img, c);
}

static void test_rect_coverage_and_clip(void) {
    br_image img;
    br_renderer r;
    br_image_init(&img, 64, 48);
    br_renderer_init(&r, &img);
    br_clear(&r, BR_BLACK);
    br_fill_rect(&r, br_rect_make(2, 3, 10, 10), BR_RED);
    CHECK_EQ_INT(count_color(&img, BR_RED), 100);
    CHECK(br_color_eq(br_image_get(&img, 2, 3), BR_RED));
    CHECK(br_color_eq(br_image_get(&img, 11, 12), BR_RED));
    CHECK(br_color_eq(br_image_get(&img, 12, 12), BR_BLACK));
    /* Sub-pixel rect [0.6, 3.4): only pixel centres 1.5 and 2.5 are inside. */
    br_clear(&r, BR_BLACK);
    br_fill_rect(&r, br_rect_make(0.6f, 0.6f, 2.8f, 2.8f), BR_RED);
    CHECK_EQ_INT(count_color(&img, BR_RED), 4);
    CHECK(br_color_eq(br_image_get(&img, 1, 1), BR_RED) && br_color_eq(br_image_get(&img, 2, 2), BR_RED));
    /* clipping, including fully off-screen and negative coordinates */
    br_clear(&r, BR_BLACK);
    br_set_clip(&r, br_recti_make(10, 10, 5, 5));
    br_fill_rect(&r, br_rect_make(-100, -100, 1000, 1000), BR_GREEN);
    CHECK_EQ_INT(count_color(&img, BR_GREEN), 25);
    br_reset_clip(&r);
    br_fill_rect(&r, br_rect_make(-50, -50, 10, 10), BR_BLUE);
    br_fill_rect(&r, br_rect_make(500, 500, 10, 10), BR_BLUE);
    CHECK_EQ_INT(count_color(&img, BR_BLUE), 0);
    /* outline: perimeter of a 10x6 rect = 2*10 + 2*4 */
    br_clear(&r, BR_BLACK);
    br_draw_rect(&r, br_rect_make(5, 5, 10, 6), BR_WHITE);
    CHECK_EQ_INT(count_color(&img, BR_WHITE), 28);
    br_image_destroy(&img);
}

static void test_blending(void) {
    br_image img;
    br_renderer r;
    br_color c;
    br_image_init(&img, 4, 4);
    br_renderer_init(&r, &img);
    br_clear(&r, br_rgb(0, 0, 200));
    br_fill_rect(&r, br_rect_make(0, 0, 4, 4), br_rgba(255, 0, 0, 128));
    c = br_image_get(&img, 1, 1);
    CHECK(c.r == 128 && c.g == 0 && c.b == 100 && c.a == 255);
    br_set_blend(&r, BR_BLEND_ADD);
    br_fill_rect(&r, br_rect_make(0, 0, 4, 4), br_rgba(200, 10, 0, 255));
    c = br_image_get(&img, 1, 1);
    CHECK(c.r == 255 && c.g == 10 && c.b == 100);
    br_set_blend(&r, BR_BLEND_MULTIPLY);
    br_fill_rect(&r, br_rect_make(0, 0, 4, 4), br_rgba(128, 255, 0, 255));
    c = br_image_get(&img, 1, 1);
    CHECK(c.r == 128 && c.g == 10 && c.b == 0);
    br_set_blend(&r, BR_BLEND_NONE);
    br_fill_rect(&r, br_rect_make(0, 0, 4, 4), br_rgba(1, 2, 3, 4));
    CHECK(br_color_eq(br_image_get(&img, 3, 3), br_rgba(1, 2, 3, 4)));
    br_image_destroy(&img);
}

/* A quad split into two triangles, at awkward angles and sizes: no pixel
 * may be missed (a crack) or drawn twice along the shared diagonal. */
static void test_triangles_watertight(void) {
    br_image img;
    br_renderer r;
    int k, x, y, doubled = 0, cracks = 0;
    br_image_init(&img, 96, 96);
    br_renderer_init(&r, &img);
    for (k = 0; k < 40; ++k) {
        float a = 0.13f + (float)k * 0.377f, s = 9.0f + (float)(k % 7) * 4.3f, c = cosf(a), sn = sinf(a);
        br_vec2 m = br_v2(48.3f + (float)(k % 3) * 0.17f, 47.9f), p[4];
        float lx[4] = { -1, 1, 1, -1 }, ly[4] = { -1, -1, 1, 1 };
        int i;
        for (i = 0; i < 4; ++i)
            p[i] = br_v2(m.x + (lx[i] * c - ly[i] * sn) * s, m.y + (lx[i] * sn + ly[i] * c) * s * 0.7f);
        br_image_fill(&img, BR_BLACK);
        br_set_blend(&r, BR_BLEND_ADD);
        br_fill_triangle(&r, p[0], p[1], p[2], br_rgb(100, 0, 0));
        br_fill_triangle(&r, p[0], p[2], p[3], br_rgb(100, 0, 0));
        for (y = 1; y < 95; ++y)
            for (x = 1; x < 95; ++x) {
                int v = br_image_get(&img, x, y).r;
                if (v > 100) doubled++;
                if (v == 0 && br_image_get(&img, x - 1, y).r && br_image_get(&img, x + 1, y).r &&
                    br_image_get(&img, x, y - 1).r && br_image_get(&img, x, y + 1).r) cracks++;
            }
    }
    CHECK_EQ_INT(doubled, 0);
    CHECK_EQ_INT(cracks, 0);
    br_image_destroy(&img);
}

static void test_triangles_shared_edge(void) {
    /* Two alpha triangles forming a quad must blend each pixel exactly once:
     * the result must equal a single rect fill. */
    br_image a, b;
    br_renderer r;
    br_color half = br_rgba(255, 255, 255, 128);
    int i, same = 1;
    br_vec2 p0 = br_v2(3.3f, 2.7f), p1 = br_v2(40.1f, 5.2f), p2 = br_v2(37.6f, 30.9f), p3 = br_v2(6.2f, 28.4f);
    br_image_init(&a, 48, 36);
    br_image_init(&b, 48, 36);
    br_renderer_init(&r, &a);
    br_clear(&r, BR_BLACK);
    br_fill_triangle(&r, p0, p1, p2, half);
    br_fill_triangle(&r, p0, p2, p3, half);
    /* No pixel may have been blended twice (that would give 191, not 128). */
    for (i = 0; i < 48 * 36; ++i) same &= a.pixels[i].r == 0 || a.pixels[i].r == 128;
    CHECK(same);
    /* Winding must not matter. */
    br_renderer_set_target(&r, &b);
    br_clear(&r, BR_BLACK);
    br_fill_triangle(&r, p2, p1, p0, half);
    br_fill_triangle(&r, p3, p2, p0, half);
    CHECK(memcmp(a.pixels, b.pixels, sizeof(br_color) * 48 * 36) == 0);
    /* Gouraud interpolation hits the vertex colours near the corners. */
    br_clear(&r, BR_BLACK);
    br_fill_triangle_colors(&r, br_v2(0, 0), br_v2(48, 0), br_v2(0, 36), BR_RED, BR_GREEN, BR_BLUE);
    CHECK(br_image_get(&b, 0, 0).r > 220);
    CHECK(br_image_get(&b, 46, 0).g > 180);
    CHECK(br_image_get(&b, 0, 34).b > 200);
    br_image_destroy(&a);
    br_image_destroy(&b);
}

static void test_circles_lines(void) {
    br_image img;
    br_renderer r;
    int n;
    br_image_init(&img, 100, 100);
    br_renderer_init(&r, &img);
    br_clear(&r, BR_BLACK);
    br_fill_circle(&r, br_v2(50, 50), 20, BR_WHITE);
    n = count_color(&img, BR_WHITE);
    CHECK(n > 1200 && n < 1314);     /* pi*r^2 = 1256.6 */
    CHECK(br_color_eq(br_image_get(&img, 50, 50), BR_WHITE));
    CHECK(br_color_eq(br_image_get(&img, 50, 29), BR_BLACK));
    br_clear(&r, BR_BLACK);
    br_draw_circle(&r, br_v2(50, 50), 20, BR_WHITE);
    n = count_color(&img, BR_WHITE);
    CHECK(n > 115 && n < 150);       /* ~ pi*(r^2-(r-1)^2) = 122.5 plus raster overlap */
    CHECK(br_color_eq(br_image_get(&img, 50, 50), BR_BLACK));
    br_clear(&r, BR_BLACK);
    br_draw_line(&r, br_v2(10.5f, 10.5f), br_v2(89.5f, 10.5f), BR_WHITE);
    CHECK_EQ_INT(count_color(&img, BR_WHITE), 80);
    br_clear(&r, BR_BLACK);
    br_draw_line(&r, br_v2(0.5f, 0.5f), br_v2(99.5f, 99.5f), BR_WHITE);
    CHECK_EQ_INT(count_color(&img, BR_WHITE), 100);
    br_clear(&r, BR_BLACK);
    br_draw_line(&r, br_v2(-1000, 50.5f), br_v2(1000, 50.5f), BR_WHITE);  /* clipped */
    CHECK_EQ_INT(count_color(&img, BR_WHITE), 100);
    br_image_destroy(&img);
}

static void make_test_sprite(br_image *s) {
    /* 4x2: top row R G B W, bottom row transparent except one yellow */
    br_image_init(s, 4, 2);
    br_image_set(s, 0, 0, BR_RED); br_image_set(s, 1, 0, BR_GREEN);
    br_image_set(s, 2, 0, BR_BLUE); br_image_set(s, 3, 0, BR_WHITE);
    br_image_set(s, 3, 1, BR_YELLOW);
}

static void test_images_and_sprites(void) {
    br_image img, spr;
    br_renderer r;
    br_recti src = { 1, 0, 2, 1 };
    br_image_init(&img, 32, 32);
    make_test_sprite(&spr);
    br_renderer_init(&r, &img);

    br_clear(&r, BR_BLACK);
    br_draw_image(&r, &spr, NULL, br_rect_make(0, 0, 8, 4), BR_WHITE);  /* 2x scale */
    CHECK(br_color_eq(br_image_get(&img, 0, 0), BR_RED));
    CHECK(br_color_eq(br_image_get(&img, 1, 1), BR_RED));
    CHECK(br_color_eq(br_image_get(&img, 2, 0), BR_GREEN));
    CHECK(br_color_eq(br_image_get(&img, 7, 3), BR_YELLOW));
    CHECK(br_color_eq(br_image_get(&img, 0, 3), BR_BLACK));   /* transparent texel */
    CHECK_EQ_INT(count_not(&img, BR_BLACK), 20);

    br_clear(&r, BR_BLACK);   /* sub-rect + tint */
    br_draw_image(&r, &spr, &src, br_rect_make(10, 10, 2, 1), br_rgba(255, 255, 255, 255));
    CHECK(br_color_eq(br_image_get(&img, 10, 10), BR_GREEN));
    CHECK(br_color_eq(br_image_get(&img, 11, 10), BR_BLUE));
    CHECK_EQ_INT(count_not(&img, BR_BLACK), 2);

    /* Rotate 90 degrees clockwise (y down) around the top-left texel corner:
     * texel (u,v) -> screen (pos.x - v, pos.y + u). */
    br_clear(&r, BR_BLACK);
    br_draw_sprite(&r, &spr, NULL, br_v2(16, 8), br_v2(0, 0), br_v2(1, 1), BR_PI / 2, BR_WHITE);
    CHECK(br_color_eq(br_image_get(&img, 15, 8), BR_RED));
    CHECK(br_color_eq(br_image_get(&img, 15, 11), BR_WHITE));
    CHECK(br_color_eq(br_image_get(&img, 14, 11), BR_YELLOW));
    CHECK_EQ_INT(count_not(&img, BR_BLACK), 5);
    br_image_destroy(&spr);
    br_image_destroy(&img);
}

static void test_camera(void) {
    br_image img;
    br_renderer r;
    br_camera2d cam = br_camera2d_make(br_v2(100, 100), br_v2(16, 16));
    br_vec2 s, w;
    br_image_init(&img, 32, 32);
    br_renderer_init(&r, &img);
    cam.zoom = 2;
    br_begin_camera(&r, &cam);
    s = br_world_to_screen(&r, br_v2(101, 100));
    CHECK_NEAR(s.x, 18, 1e-5); CHECK_NEAR(s.y, 16, 1e-5);
    br_clear(&r, BR_BLACK);
    br_fill_rect(&r, br_rect_make(100, 100, 2, 2), BR_RED);   /* -> screen 16..20 */
    CHECK_EQ_INT(count_color(&img, BR_RED), 16);
    CHECK(br_color_eq(br_image_get(&img, 16, 16), BR_RED));
    cam.rotation = 0.6f;
    br_begin_camera(&r, &cam);
    w = br_screen_to_world(&r, br_world_to_screen(&r, br_v2(123.5f, -7.25f)));
    CHECK_NEAR(w.x, 123.5, 1e-3); CHECK_NEAR(w.y, -7.25, 1e-3);
    br_clear(&r, BR_BLACK);
    br_fill_rect(&r, br_rect_make(98, 98, 4, 4), BR_RED);   /* rotated: area preserved */
    CHECK(count_color(&img, BR_RED) > 56 && count_color(&img, BR_RED) < 72);
    br_end_camera(&r);
    s = br_world_to_screen(&r, br_v2(5, 6));
    CHECK_NEAR(s.x, 5, 0); CHECK_NEAR(s.y, 6, 0);
    br_image_destroy(&img);
}

static void test_text(void) {
    br_image img;
    br_renderer r;
    br_vec2 sz = br_measure_text("Hi\nthere", 2);
    br_image_init(&img, 64, 16);
    br_renderer_init(&r, &img);
    CHECK_NEAR(sz.x, 80, 0); CHECK_NEAR(sz.y, 36, 0);
    br_clear(&r, BR_BLACK);
    br_draw_text(&r, "I", 0, 0, 1, BR_WHITE);
    /* 'I' = rows 1E,0C,0C,0C,0C,0C,1E -> 4+2*5+4 = 18 pixels */
    CHECK_EQ_INT(count_color(&img, BR_WHITE), 18);
    br_clear(&r, BR_BLACK);
    br_draw_textf(&r, 0, 0, 1, BR_WHITE, "%d", 1);
    CHECK(count_color(&img, BR_WHITE) > 0);
    CHECK(br_font_glyph(200) != NULL);
    br_image_destroy(&img);
}

static void test_image_codecs(void) {
    br_image img, back;
    int x, y;
    br_image_init(&img, 7, 5);   /* odd width exercises row padding */
    for (y = 0; y < 5; ++y)
        for (x = 0; x < 7; ++x)
            br_image_set(&img, x, y, br_rgba((uint8_t)(x * 30), (uint8_t)(y * 50), (uint8_t)(x + y), (uint8_t)(255 - x * 10)));
    CHECK(br_image_save_bmp(&img, test_path("roundtrip.bmp")));
    CHECK(br_image_load(&back, test_path("roundtrip.bmp")));
    CHECK(back.width == 7 && back.height == 5);
    CHECK(memcmp(img.pixels, back.pixels, 7 * 5 * sizeof(br_color)) == 0);
    br_image_destroy(&back);
    CHECK(br_image_save_tga(&img, test_path("roundtrip.tga")));
    CHECK(br_image_load(&back, test_path("roundtrip.tga")));
    CHECK(memcmp(img.pixels, back.pixels, 7 * 5 * sizeof(br_color)) == 0);
    br_image_destroy(&back);
    br_image_destroy(&img);

    {   /* hand-made RLE TGA, bottom-up 3x2, 24-bit */
        const uint8_t tga[] = {
            0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 2, 0, 24, 0,
            0x82, 0, 0, 255,                 /* 3 x red (BGR)   - bottom row */
            0x00, 255, 0, 0, 0x01, 0, 255, 0, 0, 255, 0  /* blue, then 2 raw greens */
        };
        CHECK(br_image_load_tga_mem(&back, tga, sizeof(tga)));
        CHECK(br_color_eq(br_image_get(&back, 0, 1), br_rgb(255, 0, 0)));   /* flipped to top-first */
        CHECK(br_color_eq(br_image_get(&back, 0, 0), br_rgb(0, 0, 255)));
        CHECK(br_color_eq(br_image_get(&back, 2, 0), br_rgb(0, 255, 0)));
        br_image_destroy(&back);
    }
    {   /* hand-made 8-bit paletted BMP, 2x2 bottom-up */
        uint8_t bmp[14 + 40 + 8 + 8];
        memset(bmp, 0, sizeof(bmp));
        bmp[0] = 'B'; bmp[1] = 'M';
        bmp[2] = sizeof(bmp);
        bmp[10] = 14 + 40 + 8;
        bmp[14] = 40; bmp[18] = 2; bmp[22] = 2; bmp[26] = 1; bmp[28] = 8;
        bmp[46] = 2;                                /* colours used */
        bmp[54] = 0; bmp[55] = 0; bmp[56] = 255;    /* palette 0 = red   */
        bmp[58] = 255; bmp[59] = 0; bmp[60] = 0;    /* palette 1 = blue  */
        bmp[62] = 0; bmp[63] = 1;                   /* bottom row: red blue (padded to 4) */
        bmp[66] = 1; bmp[67] = 1;                   /* top row: blue blue */
        CHECK(br_image_load_bmp_mem(&back, bmp, sizeof(bmp)));
        CHECK(br_color_eq(br_image_get(&back, 0, 1), br_rgb(255, 0, 0)));
        CHECK(br_color_eq(br_image_get(&back, 1, 1), br_rgb(0, 0, 255)));
        CHECK(br_color_eq(br_image_get(&back, 0, 0), br_rgb(0, 0, 255)));
        br_image_destroy(&back);
    }
    {   const uint8_t junk[] = { 'B', 'M', 1, 2, 3 };
        CHECK(!br_image_load_bmp_mem(&back, junk, sizeof(junk)));
        CHECK(!br_image_load_tga_mem(&back, junk, sizeof(junk))); }
}

/* Draws a showcase frame of every primitive and saves it for visual review. */
static void test_showcase_image(void) {
    br_image img, spr;
    br_renderer r;
    br_camera2d cam;
    int i;
    br_image_init(&img, 320, 180);
    br_image_init(&spr, 16, 16);
    for (i = 0; i < 256; ++i) {
        int x = i % 16, y = i / 16;
        br_image_set(&spr, x, y, ((x / 4 + y / 4) & 1) ? br_rgb(250, 200, 40) : br_rgba(90, 50, 160, 255));
    }
    br_renderer_init(&r, &img);
    br_clear(&r, br_rgb(24, 26, 36));
    for (i = 0; i < 320; i += 16) br_draw_line(&r, br_v2((float)i + 0.5f, 0), br_v2((float)i + 0.5f, 180), br_rgb(36, 40, 54));
    br_fill_triangle_colors(&r, br_v2(20, 160), br_v2(80, 40), br_v2(140, 160), BR_RED, BR_GREEN, BR_BLUE);
    br_fill_circle(&r, br_v2(190, 70), 30, br_rgba(0, 200, 255, 160));
    br_draw_circle(&r, br_v2(190, 70), 34, BR_WHITE);
    br_fill_rect(&r, br_rect_make(170, 90, 60, 40), br_rgba(255, 80, 120, 140));
    br_draw_rect(&r, br_rect_make(168, 88, 64, 44), BR_YELLOW);
    br_draw_image(&r, &spr, NULL, br_rect_make(250, 20, 48, 48), BR_WHITE);
    cam = br_camera2d_make(br_v2(0, 0), br_v2(274, 120));
    cam.rotation = 0.4f;
    cam.zoom = 1.5f;
    br_begin_camera(&r, &cam);
    br_draw_sprite(&r, &spr, NULL, br_v2(0, 0), br_v2(8, 8), br_v2(1.5f, 1.5f), 0.3f, BR_WHITE);
    br_end_camera(&r);
    br_set_blend(&r, BR_BLEND_ADD);
    br_fill_circle(&r, br_v2(60, 40), 18, br_rgba(255, 120, 0, 200));
    br_set_blend(&r, BR_BLEND_ALPHA);
    br_draw_text(&r, "BRONTO ENGINE 0.1", 8, 6, 1, BR_WHITE);
    br_draw_text(&r, "The quick brown fox jumps\nover the lazy dog! 0123456789", 8, 158, 1, br_rgb(180, 220, 255));
    br_draw_text(&r, "{}[]()<>@#$%&*+\n-=/\\|~^_`'\";:,.?", 196, 136, 1, br_rgb(150, 255, 150));
    CHECK(br_image_save_bmp(&img, test_path("showcase.bmp")));
    br_image_destroy(&spr);
    br_image_destroy(&img);
}

static void test_image_downscale(void) {
    br_image a, b;
    CHECK(br_image_init(&a, 4, 4));
    a.pixels[0] = BR_WHITE;                                  /* top-left 2x2: one white, three clear */
    CHECK(br_image_downscale(&a, &b, 2, 2));
    CHECK(b.width == 2 && b.pixels[0].a == 64);              /* coverage averages */
    CHECK(b.pixels[0].r == 255 && b.pixels[0].g == 255);    /* clear pixels don't darken it */
    CHECK(b.pixels[3].a == 0);
    br_image_destroy(&b);
    CHECK(!br_image_downscale(&a, &b, 0, 2));
    br_image_destroy(&a);
}

void suite_render(void) {
    RUN(test_image_downscale);
    RUN(test_rect_coverage_and_clip);
    RUN(test_blending);
    RUN(test_triangles_shared_edge);
    RUN(test_triangles_watertight);
    RUN(test_circles_lines);
    RUN(test_images_and_sprites);
    RUN(test_camera);
    RUN(test_text);
    RUN(test_image_codecs);
    RUN(test_showcase_image);
}

/* ======================= audio ========================================== */

static double rms(const float *buf, uint32_t frames, int ch) {
    double s = 0;
    uint32_t i;
    for (i = 0; i < frames; ++i) s += (double)buf[i * 2 + ch] * buf[i * 2 + ch];
    return sqrt(s / frames);
}

static void test_generate_and_mix(void) {
    br_sound tone;
    br_mixer m;
    static float out[4410 * 2];
    br_voice v;
    br_play_params p = br_play_defaults();
    CHECK(br_sound_generate(&tone, BR_WAVE_SINE, 441, 1.0f, 0.5f, 44100));
    CHECK_EQ_INT(tone.frames, 44100);
    CHECK_NEAR(br_sound_duration(&tone), 1.0, 1e-6);
    br_mixer_init(&m, 44100);
    v = br_play(&m, &tone, NULL);
    CHECK(v != 0 && br_voice_playing(&m, v));
    br_mixer_mix_f32(&m, out, 4410);
    CHECK_NEAR(rms(out + 441 * 2, 3969, 0), 0.5 / sqrt(2.0), 0.01);  /* skip attack */
    CHECK_NEAR(rms(out + 441 * 2, 3969, 1), 0.5 / sqrt(2.0), 0.01);
    br_voice_set_pan(&m, v, -1);   /* hard left */
    br_mixer_mix_f32(&m, out, 4410);
    CHECK_NEAR(rms(out + 1000 * 2, 3000, 1), 0, 1e-6);
    CHECK(rms(out + 1000 * 2, 3000, 0) > 0.3);
    br_mixer_set_bus_volume(&m, BR_BUS_SFX, 0);
    br_mixer_mix_f32(&m, out, 4410);
    CHECK_NEAR(rms(out + 1000 * 2, 3000, 0), 0, 1e-6);
    br_mixer_set_bus_volume(&m, BR_BUS_SFX, 1);
    p.bus = BR_BUS_MUSIC;
    br_mixer_stop_all(&m);
    br_mixer_mix_f32(&m, out, 512);        /* fade completes */
    CHECK_EQ_INT(br_mixer_active_voices(&m), 0);
    CHECK(!br_voice_playing(&m, v));
    v = br_play(&m, &tone, &p);
    br_mixer_set_master(&m, 0.5f);
    br_mixer_mix_f32(&m, out, 4410);
    CHECK_NEAR(rms(out + 441 * 2, 3969, 0), 0.25 / sqrt(2.0), 0.01);
    br_sound_destroy(&tone);
}

static void test_voice_end_loop_pitch(void) {
    br_sound s;
    br_mixer m;
    static float out[2000 * 2];
    br_play_params p = br_play_defaults();
    br_voice a, b, c;
    br_sound_generate(&s, BR_WAVE_SQUARE, 100, 0.01f, 0.5f, 44100);   /* 441 frames */
    br_mixer_init(&m, 44100);
    a = br_play(&m, &s, NULL);
    p.pitch = 2.0f;
    b = br_play(&m, &s, &p);                /* finishes in ~221 frames */
    p.pitch = 1.0f; p.loop = true;
    c = br_play(&m, &s, &p);
    br_mixer_mix_f32(&m, out, 300);
    CHECK(br_voice_playing(&m, a) && !br_voice_playing(&m, b) && br_voice_playing(&m, c));
    br_mixer_mix_f32(&m, out, 300);
    CHECK(!br_voice_playing(&m, a) && br_voice_playing(&m, c));
    br_mixer_mix_f32(&m, out, 2000);
    CHECK(br_voice_playing(&m, c));          /* looping keeps going */
    CHECK(rms(out, 2000, 0) > 0.1);
    br_voice_set_paused(&m, c, true);
    br_mixer_mix_f32(&m, out, 1000);
    CHECK(rms(out + 500 * 2, 500, 0) < 1e-6);
    CHECK(br_voice_playing(&m, c));
    br_voice_stop(&m, c);
    br_mixer_mix_f32(&m, out, 1000);
    CHECK(!br_voice_playing(&m, c));
    br_mixer_stop_sound(&m, &s);
    br_sound_destroy(&s);
}

static void test_voice_stealing_and_handles(void) {
    br_sound s;
    br_mixer m;
    br_voice first, v = 0;
    br_play_params loop = br_play_defaults();
    int i;
    loop.loop = true;
    br_sound_generate(&s, BR_WAVE_NOISE, 0, 0.1f, 0.1f, 22050);
    br_mixer_init(&m, 44100);
    first = br_play(&m, &s, &loop);          /* oldest, but looping: protected */
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) v = br_play(&m, &s, NULL);
    CHECK_EQ_INT(br_mixer_active_voices(&m), BR_AUDIO_MAX_VOICES);
    CHECK(br_voice_playing(&m, first));
    CHECK(br_voice_playing(&m, v));
    CHECK(!br_voice_playing(&m, 0));
    CHECK(br_play(&m, NULL, NULL) == 0);
    br_mixer_stop_sound(&m, &s);
    CHECK_EQ_INT(br_mixer_active_voices(&m), 0);
    br_voice_stop(&m, v);                    /* stale handle: harmless */
    br_sound_destroy(&s);
}

static void test_resampling(void) {
    br_sound s;
    br_mixer m;
    static float out[3000 * 2];
    br_voice v;
    br_sound_generate(&s, BR_WAVE_SINE, 220, 0.05f, 0.5f, 22050);  /* 1102 frames @22k */
    br_mixer_init(&m, 44100);
    v = br_play(&m, &s, NULL);
    br_mixer_mix_f32(&m, out, 2150);
    CHECK(br_voice_playing(&m, v));          /* lasts ~2204 output frames */
    br_mixer_mix_f32(&m, out, 100);
    CHECK(!br_voice_playing(&m, v));
    br_sound_destroy(&s);
}

static void test_wav_io(void) {
    br_sound s, back;
    int16_t pcm[] = { 0, 1000, -1000, 32767, -32768, 5 };
    s.samples = pcm; s.frames = 3; s.channels = 2; s.sample_rate = 48000;
    CHECK(br_sound_save_wav(&s, test_path("roundtrip.wav")));
    CHECK(br_sound_load_wav(&back, test_path("roundtrip.wav")));
    CHECK(back.frames == 3 && back.channels == 2 && back.sample_rate == 48000);
    CHECK(memcmp(back.samples, pcm, sizeof(pcm)) == 0);
    br_sound_destroy(&back);
    {   /* 8-bit mono with an extra chunk before 'data' */
        uint8_t w[] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E',
                        'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x11,0x2B,0,0, 0x11,0x2B,0,0, 1,0, 8,0,
                        'L','I','S','T', 3,0,0,0, 'a','b','c', 0,
                        'd','a','t','a', 3,0,0,0, 128, 255, 0 };
        CHECK(br_sound_load_wav_mem(&back, w, sizeof(w)));
        CHECK(back.frames == 3 && back.channels == 1 && back.sample_rate == 11025);
        CHECK(back.samples[0] == 0 && back.samples[1] == 127 * 256 && back.samples[2] == -32768);
        br_sound_destroy(&back);
    }
    {   /* float32 stereo */
        float fv[2] = { 0.5f, -1.5f };   /* second sample clamps */
        uint8_t w[44 + 8];
        memset(w, 0, sizeof(w));
        memcpy(w, "RIFF", 4); memcpy(w + 8, "WAVEfmt ", 8); w[16] = 16;
        w[20] = 3; w[22] = 2; w[24] = 0x44; w[25] = 0xAC; w[32] = 8; w[34] = 32;
        memcpy(w + 36, "data", 4); w[40] = 8;
        memcpy(w + 44, fv, 8);
        CHECK(br_sound_load_wav_mem(&back, w, sizeof(w)));
        CHECK(back.frames == 1 && back.channels == 2);
        CHECK(back.samples[0] == 16383 && back.samples[1] == -32767);
        br_sound_destroy(&back);
    }
    QUIET(CHECK(!br_sound_load_wav_mem(&back, "nope", 4)));
}

static void test_s16_output_clips(void) {
    br_sound s;
    br_mixer m;
    int16_t out[600 * 2];
    int i, clipped = 1;
    br_play_params p = br_play_defaults();
    br_sound_generate(&s, BR_WAVE_SQUARE, 50, 0.1f, 1.0f, 44100);
    br_mixer_init(&m, 44100);
    p.volume = 3.0f;                          /* way over full scale */
    br_play(&m, &s, &p);
    br_mixer_mix_s16(&m, out, 600);
    for (i = 300; i < 400; ++i) clipped &= out[i * 2] == 32767 || out[i * 2] == -32767;
    CHECK(clipped);
    br_sound_destroy(&s);
}

void suite_audio(void) {
    RUN(test_generate_and_mix);
    RUN(test_voice_end_loop_pitch);
    RUN(test_voice_stealing_and_handles);
    RUN(test_resampling);
    RUN(test_wav_io);
    RUN(test_s16_output_clips);
}
