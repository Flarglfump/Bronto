/* Tests: PNG reading and the DEFLATE / zlib decompressor (png.c, inflate.h). */
#include "test.h"
#include <string.h>
#include "png_fixtures.h"

static void test_inflate(void) {
    /* zlib.compress(b'Bronto! ' * 40 + b'the end', 9): a fixed-Huffman block with long matches */
    static const unsigned char z[] = { 120,218,115,42,202,207,43,201,87,84,112,26,165,201,162,75,50,82,21,82,243,82,0,26,212,110,225 };
    unsigned char bad[sizeof(z)];
    size_t n = 0, i;
    char *out = (char *)br_zlib_decompress(z, sizeof(z), &n, 0);
    CHECK(out && n == 8 * 40 + 7);
    if (out) {
        for (i = 0; i < 40; ++i) CHECK(memcmp(out + i * 8, "Bronto! ", 8) == 0);
        CHECK(memcmp(out + 320, "the end", 7) == 0);
    }
    br_free(out);
    CHECK(br_zlib_decompress(z, sizeof(z), &n, 100) == NULL);           /* bigger than allowed */
    memcpy(bad, z, sizeof(z));
    bad[sizeof(z) - 1] ^= 1;                                             /* the checksum */
    CHECK(br_zlib_decompress(bad, sizeof(bad), &n, 0) == NULL);
    CHECK(br_zlib_decompress(z, 10, &n, 0) == NULL);                     /* cut short */
    CHECK(br_zlib_decompress("\x78\x9c", 2, &n, 0) == NULL);
    {   /* a stored block, raw */
        static const unsigned char stored[] = { 1, 5, 0, 250, 255, 'h', 'e', 'l', 'l', 'o' };
        out = (char *)br_inflate(stored, sizeof(stored), &n, 0);
        CHECK(out && n == 5 && memcmp(out, "hello", 5) == 0);
        br_free(out);
    }
}

static void test_png_kinds(void) {
    size_t k;
    for (k = 0; k < sizeof(png_fixtures) / sizeof(png_fixtures[0]); ++k) {
        const png_fixture *f = &png_fixtures[k];
        br_image img;
        bool same;
        CHECK(br_image_load_png_mem(&img, f->data, f->size));
        CHECK(img.width == PNG_FIX_W && img.height == PNG_FIX_H);
        same = img.pixels && memcmp(img.pixels, f->rgba, (size_t)PNG_FIX_W * PNG_FIX_H * 4) == 0;
        CHECK(same);
        if (!same) printf("      %s decoded differently\n", f->name);
        br_image_destroy(&img);
    }
}

static void test_png_roundtrip_and_files(void) {
    br_image a, b;
    int x, y;
    void *bytes;
    size_t size;
    br_image_init(&a, 37, 21);
    for (y = 0; y < 21; ++y)
        for (x = 0; x < 37; ++x) br_image_set(&a, x, y, br_rgba((uint8_t)(x * 7), (uint8_t)(y * 12), (uint8_t)(x ^ y), (uint8_t)(255 - x)));
    bytes = br_image_encode_png(&a, &size);
    CHECK(bytes && br_image_load_png_mem(&b, bytes, size));
    CHECK(b.width == 37 && memcmp(a.pixels, b.pixels, 37 * 21 * 4) == 0);
    br_image_destroy(&b);
    CHECK(br_image_save_png(&a, test_path("roundtrip.png")));
    CHECK(br_image_load(&b, test_path("roundtrip.png")));             /* by extension */
    CHECK(b.height == 21 && memcmp(a.pixels, b.pixels, 37 * 21 * 4) == 0);
    br_image_destroy(&b);
    br_free(bytes);
    br_image_destroy(&a);
}

/* Damaged files are refused, never read out of bounds (try under the sanitizers). */
static uint32_t damage_runs(const png_fixture *f, unsigned char *copy) {
    uint32_t seed = 12345, i, loaded = 0;
    br_image img;
    size_t cut;
    for (cut = 0; cut < f->size - 12; cut += 7) CHECK(!br_image_load_png_mem(&img, f->data, cut));   /* (only IEND may be missing) */
    for (i = 0; i < 3000; ++i) {
        int flips = 1 + (int)(i % 4), k;
        memcpy(copy, f->data, f->size);
        for (k = 0; k < flips; ++k) {
            seed = seed * 1103515245u + 12345u;
            copy[8 + (seed >> 8) % (f->size - 8)] ^= (unsigned char)(1u << ((seed >> 4) & 7));
        }
        if (br_image_load_png_mem(&img, copy, f->size)) { ++loaded; br_image_destroy(&img); }
    }
    CHECK(!br_image_load_png_mem(&img, "not a png at all", 16));
    return loaded;
}

static void test_png_damage(void) {
    size_t k;
    for (k = 0; k < sizeof(png_fixtures) / sizeof(png_fixtures[0]); k += 4) {
        const png_fixture *f = &png_fixtures[k];
        unsigned char *copy = (unsigned char *)br_alloc(f->size);
        uint32_t loaded = 0;
        QUIET(loaded = damage_runs(f, copy));
        CHECK(loaded < 3000);
        br_free(copy);
    }
}

/* Compression: what br_deflate makes, br_inflate (and zlib) read back exactly. */
static void test_deflate_round_trips(void) {
    static const int sizes[] = { 0, 1, 2, 3, 7, 100, 4000, 70000, 300000 };
    size_t k;
    br_rng rng;
    br_rng_seed(&rng, 7);
    for (k = 0; k < BR_ARRAY_COUNT(sizes); ++k) {
        int kind;
        for (kind = 0; kind < 5; ++kind) {
            size_t n = (size_t)sizes[k], i, packed = 0, back = 0;
            unsigned char *src = (unsigned char *)br_alloc(n + 1), *z, *out;
            int level = kind == 4 ? 1 : 6;
            for (i = 0; i < n; ++i) {
                switch (kind) {
                case 0: src[i] = 0; break;                                            /* runs */
                case 1: src[i] = (unsigned char)br_rng_u32(&rng); break;              /* noise: stored */
                case 2: src[i] = (unsigned char)"the quick brown fox jumps over the lazy dog. "[(i * 7 + i / 13) % 45]; break;
                case 3: src[i] = (unsigned char)((i % 300) < 150 ? br_rng_u32(&rng) & 7 : 'a' + (i % 26)); break;
                default: src[i] = (unsigned char)(i * i >> 7); break;
                }
            }
            z = (unsigned char *)br_deflate(src, n, &packed, level);
            CHECK(z != NULL);
            out = z ? (unsigned char *)br_inflate(z, packed, &back, 0) : NULL;
            CHECK(out != NULL && back == n && (n == 0 || memcmp(out, src, n) == 0));
            if (kind == 0 && n >= 4000) CHECK(packed < n / 50);                      /* zeros: next to nothing */
            if (kind == 1 && n >= 100) CHECK(packed <= n + n / 1000 + 16);            /* noise: hardly bigger */
            if (kind == 2 && n >= 4000) CHECK(packed < n / 8);
            br_free(z);
            br_free(out);
            z = (unsigned char *)br_zlib_compress(src, n, &packed, 9);
            out = z ? (unsigned char *)br_zlib_decompress(z, packed, &back, 0) : NULL;
            CHECK(out != NULL && back == n && (n == 0 || memcmp(out, src, n) == 0));
            br_free(z);
            br_free(out);
            br_free(src);
        }
    }
}

void suite_png(void) {
    RUN(test_inflate);
    RUN(test_png_kinds);
    RUN(test_png_roundtrip_and_files);
    RUN(test_png_damage);
    RUN(test_deflate_round_trips);
}
