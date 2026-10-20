/*
 * Native audio output integration test (developer tool).
 *
 * Runs the engine on the native platform, plays a 440 Hz tone and lets the
 * real audio backend deliver it. On Linux the ALSA "file" plugin captures
 * exactly what reached the device, which is then analysed: format,
 * channel layout, pitch, level and continuity (no dropouts).
 *
 *   make audio-test        (Linux: needs an X server or xvfb-run)
 */
#define _POSIX_C_SOURCE 200809L
#include "bronto/bronto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int g_checks, g_fail;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static br_sound g_tone;

static bool on_init(br_engine *e) {
    br_play_params p = br_play_defaults();
    br_sound_generate(&g_tone, BR_WAVE_SINE, 440.0f, 10.0f, 0.5f, 44100);
    p.loop = true;
    br_play(&e->mixer, &g_tone, &p);
    return true;
}

static bool idle(br_scene *s) { BR_UNUSED(s); return true; }
static const br_scene_desc k_scene = { "audio", 0, false, false, idle, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    const char *out_dir = argc > 1 ? argv[1] : ".";
    char raw_path[512], device[600];
    br_engine *e = BR_NEW(br_engine);
    br_engine_config c = br_engine_default_config();
    size_t size = 0, frames, i, longest_silence = 0, run = 0;
    int16_t *pcm;
    double t0, elapsed, sum = 0;
    int crossings = 0, mismatched = 0;

    snprintf(raw_path, sizeof(raw_path), "%s/audio_capture.raw", out_dir);
    remove(raw_path);
    snprintf(device, sizeof(device), "file:'%s',raw", raw_path);
    setenv("BRONTO_AUDIO_DEVICE", device, 1);

    c.title = "Bronto audio test";
    c.initial_scene = &k_scene;
    c.on_init = on_init;
    CHECK(br_engine_init(e, &c));
    t0 = now();
    while (now() - t0 < 1.0) br_engine_frame(e);
    elapsed = now() - t0;
    br_engine_shutdown(e);
    br_free(e);
    br_sound_destroy(&g_tone);

    pcm = (int16_t *)br_file_read_all(raw_path, &size);
    CHECK(pcm != NULL);
    if (!pcm) return 1;
    frames = size / 4;
    printf("captured %zu frames (%.2f s of audio) during %.2f s of running\n",
           frames, (double)frames / 44100.0, elapsed);
    CHECK(frames >= 44100 / 2);

    /* skip the 5 ms fade-in, analyse up to 1 s */
    if (frames > 44100) frames = 44100;
    for (i = 441; i < frames; ++i) {
        int16_t l = pcm[i * 2], r = pcm[i * 2 + 1];
        if (l != r) mismatched++;
        sum += (double)l * l;
        if ((pcm[(i - 1) * 2] < 0) != (l < 0)) crossings++;
        if (l == 0) { if (++run > longest_silence) longest_silence = run; } else run = 0;
    }
    {
        double secs = (double)(frames - 441) / 44100.0;
        double rms = sqrt(sum / (double)(frames - 441)) / 32767.0;
        double hz = (double)crossings / 2.0 / secs;
        printf("pitch %.1f Hz, rms %.3f (expect 0.354), L/R mismatches %d, longest silence %zu frames\n",
               hz, rms, mismatched, longest_silence);
        CHECK(fabs(hz - 440.0) < 3.0);       /* right sample rate, frames in order */
        CHECK(fabs(rms - 0.3536) < 0.02);    /* right format and level */
        CHECK(mismatched == 0);              /* centred mono -> identical channels */
        CHECK(longest_silence < 4);          /* no gaps / dropouts */
    }
    br_free(pcm);
    printf("audio integration: %d checks, %d failed -> %s\n", g_checks, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
