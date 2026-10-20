/* Tests: lock-free audio ring buffer (single-threaded semantics and a
 * real two-thread stress test where threads are available). */
#if !defined(_WIN32) && !defined(__wasm__) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "test.h"
#include <string.h>

#if !defined(_WIN32) && !defined(__wasm__)
#  include <pthread.h>
#  define HAVE_THREADS 1
#endif

static void test_ring_basics(void) {
    br_audio_ring r;
    int16_t in[200 * 2], out[200 * 2];
    int i, ok = 1;
    for (i = 0; i < 400; ++i) in[i] = (int16_t)(i * 3 - 500);
    CHECK(br_audio_ring_init(&r, 100));
    CHECK_EQ_INT(r.capacity, 128);                 /* rounded to a power of two */
    CHECK_EQ_INT(br_audio_ring_available(&r), 0);
    CHECK_EQ_INT(br_audio_ring_read(&r, out, 10), 0);   /* underrun: nothing */
    CHECK_EQ_INT(br_audio_ring_write(&r, in, 200), 128);   /* clipped to capacity */
    CHECK_EQ_INT(br_audio_ring_space(&r), 0);
    CHECK_EQ_INT(br_audio_ring_write(&r, in, 1), 0);
    CHECK_EQ_INT(br_audio_ring_read(&r, out, 100), 100);
    CHECK(memcmp(out, in, 100 * 2 * sizeof(int16_t)) == 0);
    /* wrap-around: write past the end of the storage */
    CHECK_EQ_INT(br_audio_ring_write(&r, in + 128 * 2, 72), 72);
    CHECK_EQ_INT(br_audio_ring_available(&r), 100);
    CHECK_EQ_INT(br_audio_ring_read(&r, out, 200), 100);
    for (i = 0; i < 28 * 2; ++i) ok &= out[i] == in[100 * 2 + i];
    for (i = 0; i < 72 * 2; ++i) ok &= out[28 * 2 + i] == in[128 * 2 + i];
    CHECK(ok);
    br_audio_ring_destroy(&r);
}

#if defined(HAVE_THREADS)
#define STRESS_FRAMES 400000u
static br_audio_ring g_ring;
static volatile int g_bad;

static void *consumer(void *arg) {
    int16_t buf[97 * 2];
    uint32_t expect = 0;
    BR_UNUSED(arg);
    while (expect < STRESS_FRAMES) {
        uint32_t n = br_audio_ring_read(&g_ring, buf, 97), i;
        for (i = 0; i < n; ++i, ++expect) {
            if (buf[i * 2] != (int16_t)expect || buf[i * 2 + 1] != (int16_t)~expect) g_bad = 1;
        }
    }
    return NULL;
}

static void test_ring_two_threads(void) {
    pthread_t t;
    int16_t buf[61 * 2];
    uint32_t next = 0;
    CHECK(br_audio_ring_init(&g_ring, 256));
    g_bad = 0;
    pthread_create(&t, NULL, consumer, NULL);
    while (next < STRESS_FRAMES) {
        uint32_t n = 61, i, w;
        if (STRESS_FRAMES - next < n) n = STRESS_FRAMES - next;
        for (i = 0; i < n; ++i) { buf[i * 2] = (int16_t)(next + i); buf[i * 2 + 1] = (int16_t)~(next + i); }
        w = br_audio_ring_write(&g_ring, buf, n);
        next += w;            /* partial writes: retry the rest next loop */
    }
    pthread_join(t, NULL);
    CHECK(!g_bad);            /* every frame arrived intact and in order */
    CHECK_EQ_INT(br_audio_ring_available(&g_ring), 0);
    br_audio_ring_destroy(&g_ring);
}
#endif

void suite_audio_ring(void) {
    RUN(test_ring_basics);
#if defined(HAVE_THREADS)
    RUN(test_ring_two_threads);
#endif
}
