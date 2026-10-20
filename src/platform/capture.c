/*
 * Bronto Engine - capturing stdout / stderr (see capture.h): the shared part,
 * plus the OS backend for this build, included below.
 *
 * Reader threads move what the streams receive into one ring buffer of
 * records ([stream][length, 2 bytes][bytes]); br_capture_read() takes them
 * out on the program's own thread. Each backend provides:
 *
 *   static bool cap_os_start(bool echo);       redirect both streams to pipes, start the readers
 *   static void cap_os_stop(void);             put them back, end the readers
 *   static void cap_os_write_original(int stream, const char *text, size_t n);
 *   static void cap_lock(void), cap_unlock(void);
 *
 * and calls cap_push() from its readers.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "bronto/capture.h"

#include <stdio.h>
#include <string.h>

#define CAP_RING (256u * 1024u)
#define CAP_CHUNK 4096u                    /* the most one record holds */

static unsigned char cap_ring[CAP_RING];
static size_t cap_head, cap_tail;          /* running byte counts: written, read */
static size_t cap_lost;
static bool cap_on;

static void cap_lock(void);
static void cap_unlock(void);

static void cap_put(size_t at, unsigned char b) { cap_ring[at % CAP_RING] = b; }
static unsigned char cap_get(size_t at) { return cap_ring[at % CAP_RING]; }

/* Called by the reader threads. */
static void cap_push(int stream, const char *data, size_t n) {
    cap_lock();
    while (n > 0) {
        size_t part = n < CAP_CHUNK ? n : CAP_CHUNK, i;
        if (cap_head - cap_tail + 3 + part > CAP_RING) { cap_lost += n; break; }   /* full: drop the rest */
        cap_put(cap_head, (unsigned char)stream);
        cap_put(cap_head + 1, (unsigned char)(part & 0xff));
        cap_put(cap_head + 2, (unsigned char)(part >> 8));
        for (i = 0; i < part; ++i) cap_put(cap_head + 3 + i, (unsigned char)data[i]);
        cap_head += 3 + part;
        data += part;
        n -= part;
    }
    cap_unlock();
}

#if defined(__wasm__) || defined(__EMSCRIPTEN__)   /* the web: no pipes or threads */
static void cap_lock(void) {}
static void cap_unlock(void) {}
static bool cap_os_start(bool echo) { BR_UNUSED(echo); (void)cap_push; return false; }
static void cap_os_stop(void) {}
static void cap_os_write_original(int stream, const char *text, size_t n) {
    fwrite(text, 1, n, stream == BR_STREAM_STDERR ? stderr : stdout);
}
#elif defined(_WIN32)
#  include "capture_win32.c"
#else
#  include "capture_posix.c"
#endif

bool br_capture_start(bool echo) {
    if (cap_on) return true;
    fflush(stdout);
    fflush(stderr);
    cap_head = cap_tail = cap_lost = 0;
    cap_on = cap_os_start(echo);
    return cap_on;
}

void br_capture_stop(void) {
    if (!cap_on) return;
    fflush(stdout);
    fflush(stderr);
    cap_os_stop();
    cap_on = false;
}

bool br_capture_active(void) { return cap_on; }

size_t br_capture_read(br_stream *stream, char *buffer, size_t size) {
    size_t len, n, i;
    if (!cap_on || size == 0) return 0;
    fflush(stdout);
    fflush(stderr);
    cap_lock();
    if (cap_head == cap_tail) { cap_unlock(); return 0; }
    *stream = (br_stream)cap_get(cap_tail);
    len = (size_t)cap_get(cap_tail + 1) | ((size_t)cap_get(cap_tail + 2) << 8);
    n = len < size ? len : size;
    for (i = 0; i < n; ++i) buffer[i] = (char)cap_get(cap_tail + 3 + i);
    if (n == len) {
        cap_tail += 3 + len;
    } else {                               /* the rest stays: a new header just before it */
        size_t rest = len - n;
        cap_tail += n;
        cap_put(cap_tail, (unsigned char)*stream);
        cap_put(cap_tail + 1, (unsigned char)(rest & 0xff));
        cap_put(cap_tail + 2, (unsigned char)(rest >> 8));
    }
    cap_unlock();
    return n;
}

size_t br_capture_dropped(void) {
    size_t n;
    cap_lock();
    n = cap_lost;
    cap_unlock();
    return n;
}

void br_capture_write_original(br_stream stream, const char *text, size_t length) {
    if (!cap_on) {
        FILE *f = stream == BR_STREAM_STDERR ? stderr : stdout;
        fwrite(text, 1, length, f);
        fflush(f);
        return;
    }
    cap_os_write_original((int)stream, text, length);
}
