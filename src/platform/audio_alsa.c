/*
 * Linux audio output: ALSA, loaded at run time with dlopen (no ALSA headers
 * or -lasound needed to build). Works with PulseAudio / PipeWire too, which
 * provide the ALSA "default" device.
 *
 * Model: non-blocking, pushed from the game thread. The device buffer is
 * sized to the target latency; each frame the engine tops it up with
 * exactly the free space (snd_pcm_avail_update). Underruns (e.g. a long
 * hitch) are recovered transparently.
 *
 * Device selection: $BRONTO_AUDIO_DEVICE, default "default". Handy values:
 * "null" (discard), "file:'/tmp/out.raw',raw" (capture to a file).
 *
 * Included by the Linux window backend.
 */
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>

typedef struct _snd_pcm snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;

#define ALSA_STREAM_PLAYBACK 0
#define ALSA_NONBLOCK 1
#define ALSA_FORMAT_S16_LE 2
#define ALSA_ACCESS_RW_INTERLEAVED 3

#define ALSA_FUNCS(F) \
    F(int, snd_pcm_open, (snd_pcm_t **, const char *, int, int)) \
    F(int, snd_pcm_set_params, (snd_pcm_t *, int, int, unsigned int, unsigned int, int, unsigned int)) \
    F(int, snd_pcm_get_params, (snd_pcm_t *, snd_pcm_uframes_t *, snd_pcm_uframes_t *)) \
    F(snd_pcm_sframes_t, snd_pcm_avail_update, (snd_pcm_t *)) \
    F(snd_pcm_sframes_t, snd_pcm_writei, (snd_pcm_t *, const void *, snd_pcm_uframes_t)) \
    F(int, snd_pcm_recover, (snd_pcm_t *, int, int)) \
    F(int, snd_pcm_drop, (snd_pcm_t *)) \
    F(int, snd_pcm_close, (snd_pcm_t *)) \
    F(const char *, snd_strerror, (int))

#define ALSA_DECLARE(ret, name, args) typedef ret (*name##_fn) args; static name##_fn p_##name;
ALSA_FUNCS(ALSA_DECLARE)

/* alsa-lib prints its own diagnostics to stderr (e.g. on machines with no
 * sound card); route them away - we report problems through BR_WARN. */
typedef void (*alsa_error_handler)(const char *, int, const char *, int, const char *, ...);
typedef int (*snd_lib_error_set_handler_fn)(alsa_error_handler);
static void alsa_quiet(const char *file, int line, const char *fn, int err, const char *fmt, ...) {
    BR_UNUSED(file); BR_UNUSED(line); BR_UNUSED(fn); BR_UNUSED(err); BR_UNUSED(fmt);
}

static struct {
    void *lib;
    snd_pcm_t *pcm;
    snd_pcm_uframes_t buffer_frames;
    uint64_t underruns;
} A;

static bool alsa_load(void) {
    const char *names[] = { "libasound.so.2", "libasound.so" };
    size_t i;
    if (A.lib) return true;
    for (i = 0; i < BR_ARRAY_COUNT(names) && !A.lib; ++i) A.lib = dlopen(names[i], RTLD_LAZY | RTLD_LOCAL);
    if (!A.lib) { BR_WARN("audio: libasound not found - running silent"); return false; }
#define ALSA_LOAD(ret, name, args) \
    { union { void *o; name##_fn f; } u_; u_.o = dlsym(A.lib, #name); p_##name = u_.f; \
      if (!p_##name) { BR_WARN("audio: libasound lacks %s - running silent", #name); return false; } }
    ALSA_FUNCS(ALSA_LOAD)
#undef ALSA_LOAD
    {
        union { void *o; snd_lib_error_set_handler_fn f; } u;
        u.o = dlsym(A.lib, "snd_lib_error_set_handler");
        if (u.f) u.f(alsa_quiet);
    }
    return true;
}

static bool alsa_open(int rate, int latency_ms) {
    const char *device = getenv("BRONTO_AUDIO_DEVICE");
    snd_pcm_uframes_t period = 0;
    int err;
    if (!device || !*device) device = "default";
    if (rate <= 0 || !alsa_load()) return false;
    err = p_snd_pcm_open(&A.pcm, device, ALSA_STREAM_PLAYBACK, ALSA_NONBLOCK);
    if (err < 0) {
        BR_WARN("audio: cannot open ALSA device '%s': %s - running silent", device, p_snd_strerror(err));
        A.pcm = NULL;
        return false;
    }
    err = p_snd_pcm_set_params(A.pcm, ALSA_FORMAT_S16_LE, ALSA_ACCESS_RW_INTERLEAVED, 2, (unsigned)rate,
                               1 /* allow resampling */, (unsigned)(latency_ms > 0 ? latency_ms : 75) * 1000u);
    if (err < 0) {
        BR_WARN("audio: ALSA device '%s' rejected 16-bit stereo @ %d Hz: %s", device, rate, p_snd_strerror(err));
        p_snd_pcm_close(A.pcm);
        A.pcm = NULL;
        return false;
    }
    p_snd_pcm_get_params(A.pcm, &A.buffer_frames, &period);
    BR_INFO("audio: ALSA '%s', %d Hz, buffer %lu frames (%.0f ms)", device, rate,
            (unsigned long)A.buffer_frames, 1000.0 * (double)A.buffer_frames / (double)rate);
    return true;
}

static void alsa_close(void) {
    if (!A.pcm) return;
    p_snd_pcm_drop(A.pcm);
    p_snd_pcm_close(A.pcm);
    A.pcm = NULL;
}

static uint32_t alsa_wanted(void) {
    snd_pcm_sframes_t avail;
    if (!A.pcm) return 0;
    avail = p_snd_pcm_avail_update(A.pcm);
    if (avail < 0) {
        /* -EPIPE (underrun) / -ESTRPIPE (suspend): recover, refill next frame */
        if (avail == -EPIPE) A.underruns++;
        if (p_snd_pcm_recover(A.pcm, (int)avail, 1) < 0) return 0;
        avail = p_snd_pcm_avail_update(A.pcm);
        if (avail < 0) return 0;
    }
    if ((snd_pcm_uframes_t)avail > A.buffer_frames && A.buffer_frames) avail = (snd_pcm_sframes_t)A.buffer_frames;
    return (uint32_t)avail;
}

static void alsa_submit(const int16_t *frames, uint32_t count) {
    int retries = 2;
    if (!A.pcm) return;
    while (count > 0 && retries > 0) {
        snd_pcm_sframes_t n = p_snd_pcm_writei(A.pcm, frames, count);
        if (n == -EAGAIN) return;                  /* device full: drop the excess */
        if (n < 0) {
            if (n == -EPIPE) A.underruns++;
            if (p_snd_pcm_recover(A.pcm, (int)n, 1) < 0) return;
            --retries;
            continue;
        }
        frames += (size_t)n * 2u;
        count -= (uint32_t)n;
    }
}
