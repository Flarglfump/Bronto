/*
 * SNAKE - sound.
 *
 * Every sound is synthesised at start-up, so the game ships with no audio
 * files. Sounds are triggered by subscribing to the rule events on the
 * engine's message bus: nothing in the rules or the drawing code calls
 * into this file.
 */
#include "view.h"
#include <math.h>
#include <string.h>

static struct {
    br_sound eat, bonus, die, start, music;
    bool music_muted;
} A;

/* ---- synthesis ---------------------------------------------------------------- */

/* A frequency sweep with an exponential decay envelope. */
static void synth_sweep(br_sound *s, br_waveform wave, float f0, float f1, float seconds, float volume) {
    uint32_t i, n = (uint32_t)(seconds * 44100.0f);
    br_rng noise;
    float phase = 0;
    br_rng_seed(&noise, 99);
    memset(s, 0, sizeof(*s));
    s->samples = (int16_t *)br_alloc(n * sizeof(int16_t));
    s->frames = n;
    s->channels = 1;
    s->sample_rate = 44100;
    for (i = 0; i < n; ++i) {
        float t = (float)i / (float)n, f = f0 + (f1 - f0) * t, v;
        float env = (1.0f - t) * (1.0f - t) * (i < 200 ? (float)i / 200.0f : 1.0f);
        switch (wave) {
        case BR_WAVE_SQUARE:   v = phase < 0.5f ? 1.0f : -1.0f; break;
        case BR_WAVE_TRIANGLE: v = 4.0f * br_absf(phase - 0.5f) - 1.0f; break;
        case BR_WAVE_NOISE:    v = br_rng_range(&noise, -1, 1); break;
        default:               v = sinf(phase * BR_TAU); break;
        }
        s->samples[i] = (int16_t)(v * env * volume * 32767.0f);
        phase += f / 44100.0f;
        phase -= (float)(int)phase;
    }
}

/* A little looping tune: bass + lead, A minor pentatonic, 120 BPM. */
static void synth_music(br_sound *s) {
    static const int lead[32] = { 57, 0, 60, 62, 64, 0, 62, 60, 57, 0, 55, 57, 60, 0, 0, 0,
                                  64, 0, 67, 64, 62, 0, 60, 62, 64, 0, 62, 60, 57, 0, 0, 0 };
    static const int bass[8] = { 33, 33, 36, 31, 33, 33, 28, 31 };
    const float step = 0.125f;   /* 8th notes */
    uint32_t i, n = (uint32_t)(32 * step * 44100.0f);
    memset(s, 0, sizeof(*s));
    s->samples = (int16_t *)br_alloc(n * sizeof(int16_t));
    s->frames = n;
    s->channels = 1;
    s->sample_rate = 44100;
    for (i = 0; i < n; ++i) {
        float t = (float)i / 44100.0f;
        int idx = (int)(t / step), bidx = (int)(t / (step * 4));
        float in_note = t - (float)idx * step, in_bass = t - (float)bidx * step * 4;
        float v = 0;
        if (lead[idx]) {
            float f = 440.0f * powf(2.0f, (float)(lead[idx] - 69) / 12.0f);
            float ph = in_note * f;
            float env = expf(-in_note * 9.0f);
            v += ((ph - floorf(ph)) < 0.25f ? 1.0f : -1.0f) * 0.16f * env;   /* 25% pulse lead */
        }
        {
            float f = 440.0f * powf(2.0f, (float)(bass[bidx % 8] - 69) / 12.0f);
            float ph = in_bass * f;
            float env = expf(-in_bass * 2.0f);
            v += (4.0f * br_absf(ph - floorf(ph) - 0.5f) - 1.0f) * 0.30f * env;  /* triangle bass */
        }
        s->samples[i] = (int16_t)(br_clampf(v, -1, 1) * 32767.0f);
    }
}

/* ---- playing ------------------------------------------------------------------- */

/* Shake every connected controller (no-op where rumble is unsupported). */
static void rumble_all(br_engine *e, float heavy, float light, float seconds) {
    int i;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i)
        if (br_pad_connected(&e->input, i)) br_gamepad_rumble(e, i, heavy, light, seconds);
}

/* Rule events arrive as one message type carrying a snake_event. */
static bool on_snake_event(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    const snake_event *s = BR_EVENT_DATA(ev, snake_event);
    br_play_params p = br_play_defaults();
    switch (s->type) {
    case SNAKE_EV_ATE:
    case SNAKE_EV_ATE_BONUS:
        p.pitch = 1.0f + br_clampf((float)s->score / 2000.0f, 0, 0.6f);         /* rises as you score */
        p.pan = br_clampf(((float)s->cell.x / (SNAKE_COLS - 1)) * 2.0f - 1.0f, -1, 1) * 0.6f;
        br_play(&e->mixer, s->type == SNAKE_EV_ATE_BONUS ? &A.bonus : &A.eat, &p);
        if (s->type == SNAKE_EV_ATE_BONUS) rumble_all(e, 0.2f, 0.6f, 0.15f);
        break;
    case SNAKE_EV_DIED:
    case SNAKE_EV_WON:
        br_play(&e->mixer, &A.die, NULL);
        rumble_all(e, 0.9f, 0.5f, 0.4f);
        break;
    }
    return false;
}

void sfx_init(br_engine *e, uint32_t event_type) {
    br_play_params mp = br_play_defaults();
    synth_sweep(&A.eat, BR_WAVE_SQUARE, 520, 1040, 0.08f, 0.35f);
    synth_sweep(&A.bonus, BR_WAVE_TRIANGLE, 660, 1760, 0.25f, 0.5f);
    synth_sweep(&A.die, BR_WAVE_NOISE, 0, 0, 0.6f, 0.5f);
    synth_sweep(&A.start, BR_WAVE_SQUARE, 330, 660, 0.15f, 0.25f);
    synth_music(&A.music);
    mp.loop = true;
    mp.bus = BR_BUS_MUSIC;
    mp.volume = 0.6f;
    br_play(&e->mixer, &A.music, &mp);
    br_events_subscribe(&e->events, event_type, on_snake_event, e, 0);
}

void sfx_shutdown(br_engine *e) {
    br_mixer_stop_all(&e->mixer);
    br_sound_destroy(&A.eat);
    br_sound_destroy(&A.bonus);
    br_sound_destroy(&A.die);
    br_sound_destroy(&A.start);
    br_sound_destroy(&A.music);
}

void sfx_start(br_engine *e) { br_play(&e->mixer, &A.start, NULL); }

void sfx_toggle_music(br_engine *e) {
    A.music_muted = !A.music_muted;
    br_mixer_set_bus_volume(&e->mixer, BR_BUS_MUSIC, A.music_muted ? 0.0f : 1.0f);
}
