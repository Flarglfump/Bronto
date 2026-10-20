/*
 * POP DEFENSE - sound.
 *
 * Every sound is synthesised at start-up from a few oscillators, so the
 * game ships with no audio files. Sounds are triggered by subscribing to
 * the rule events on the engine's message bus: nothing in the rules or
 * the drawing code calls into this file.
 */
#include "view.h"
#include <math.h>
#include <string.h>

#define SR 44100

typedef enum {
    S_POP0, S_POP1, S_POP2, S_HIT, S_BLOCKED, S_EXPLOSION, S_FREEZE, S_FIRE, S_SNIPE, S_LAUNCH,
    S_PLACE, S_UPGRADE, S_SELL, S_ROUND_START, S_ROUND_END, S_LEAK, S_DEFEAT, S_VICTORY,
    S_CLICK, S_ERROR, S_MUSIC, S_COUNT
} sound_id;

static struct {
    br_sound sounds[S_COUNT];
    br_voice music;
    bool music_muted;
    uint64_t frame;             /* per-frame limits on busy sounds */
    int pops_this_frame, hits_this_frame, booms_this_frame;
    br_rng rng;
} A;

/* ---- a tiny synthesiser -------------------------------------------------------- */

typedef struct synth { float *buf; uint32_t frames; br_rng noise; } synth;

static void synth_begin(synth *s, float seconds) {
    s->frames = (uint32_t)(seconds * SR);
    s->buf = (float *)br_calloc(s->frames, sizeof(float));
    br_rng_seed(&s->noise, 1234);
}

/* One note: pitch glides from f0 to f1 over `len` seconds starting at
 * `start`, with an exponential decay envelope. */
static void voice(synth *s, float start, br_waveform wave, float f0, float f1, float len, float vol, float decay) {
    uint32_t i, first = (uint32_t)(start * SR), n = (uint32_t)(len * SR);
    float phase = 0;
    for (i = 0; i < n && first + i < s->frames; ++i) {
        float t = (float)i / SR, k = (float)i / (float)n;
        float f = (f0 > 0 && f1 > 0) ? f0 * powf(f1 / f0, k) : f0 + (f1 - f0) * k;
        float env = expf(-t * decay) * br_clampf(t / 0.003f, 0, 1) * br_clampf((1.0f - k) * 40.0f, 0, 1);
        float v;
        switch (wave) {
        case BR_WAVE_SQUARE:   v = phase < 0.5f ? 1.0f : -1.0f; break;
        case BR_WAVE_TRIANGLE: v = 4.0f * br_absf(phase - 0.5f) - 1.0f; break;
        case BR_WAVE_SAW:      v = 2.0f * phase - 1.0f; break;
        case BR_WAVE_NOISE:    v = br_rng_range(&s->noise, -1, 1); break;
        default:               v = sinf(phase * BR_TAU); break;
        }
        s->buf[first + i] += v * env * vol;
        phase += f / SR;
        phase -= floorf(phase);
    }
}

static void notes(synth *s, float start, br_waveform wave, const float *freqs, int count, float step, float len, float vol, float decay) {
    int i;
    for (i = 0; i < count; ++i) voice(s, start + (float)i * step, wave, freqs[i], freqs[i], len, vol, decay);
}

static void synth_end(synth *s, br_sound *out) {
    uint32_t i;
    memset(out, 0, sizeof(*out));
    out->samples = (int16_t *)br_alloc(s->frames * sizeof(int16_t));
    out->frames = s->frames;
    out->channels = 1;
    out->sample_rate = SR;
    for (i = 0; i < s->frames; ++i) out->samples[i] = (int16_t)(br_clampf(s->buf[i], -1, 1) * 32000.0f);
    br_free(s->buf);
}

static float midi(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }

/* A breezy loop: C - Am - F - G, bass on the beat, plucked arpeggios. */
static void make_music(br_sound *out) {
    static const int roots[4] = { 48, 45, 41, 43 };
    static const int chord[4][3] = { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };
    static const int melody[32] = { 72, 0, 76, 0, 79, 76, 74, 0,  72, 0, 69, 72, 76, 0, 0, 0,
                                    77, 0, 76, 74, 72, 0, 69, 0,  71, 0, 74, 0, 79, 0, 0, 0 };
    const float beat = 0.5f, eighth = 0.25f;
    synth s;
    int bar, i;
    synth_begin(&s, 16 * beat);
    for (bar = 0; bar < 4; ++bar) {
        float t0 = (float)bar * 4 * beat;
        for (i = 0; i < 4; ++i) {      /* root and fifth */
            float f = midi(roots[bar] + ((i & 1) ? 7 : 0));
            voice(&s, t0 + (float)i * beat, BR_WAVE_TRIANGLE, f, f, 0.45f, 0.30f, 5);
        }
        for (i = 0; i < 8; ++i) voice(&s, t0 + (float)i * eighth, BR_WAVE_SQUARE, midi(chord[bar][i % 3]), midi(chord[bar][i % 3]), 0.2f, 0.045f, 14);
    }
    for (i = 0; i < 32; ++i)
        if (melody[i]) voice(&s, (float)i * eighth, BR_WAVE_TRIANGLE, midi(melody[i]), midi(melody[i]), 0.24f, 0.16f, 7);
    synth_end(&s, out);
}

static void make_sounds(void) {
    synth s;
    br_sound *o = A.sounds;
    int i;
    for (i = 0; i < 3; ++i) {                  /* three slightly different pops */
        synth_begin(&s, 0.08f);
        voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.014f, 0.55f, 250);
        voice(&s, 0, BR_WAVE_SINE, 1500.0f + 250.0f * (float)i, 420, 0.06f, 0.5f, 55);
        synth_end(&s, &o[S_POP0 + i]);
    }
    synth_begin(&s, 0.07f);  voice(&s, 0, BR_WAVE_TRIANGLE, 300, 180, 0.06f, 0.45f, 60); voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.01f, 0.3f, 300); synth_end(&s, &o[S_HIT]);
    synth_begin(&s, 0.08f);  voice(&s, 0, BR_WAVE_SQUARE, 2600, 2400, 0.04f, 0.12f, 90); voice(&s, 0, BR_WAVE_SINE, 3900, 3900, 0.07f, 0.15f, 50); synth_end(&s, &o[S_BLOCKED]);
    synth_begin(&s, 0.5f);   voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.45f, 0.55f, 9); voice(&s, 0, BR_WAVE_SINE, 140, 40, 0.32f, 0.7f, 9); synth_end(&s, &o[S_EXPLOSION]);
    synth_begin(&s, 0.35f);  voice(&s, 0, BR_WAVE_TRIANGLE, 900, 2200, 0.28f, 0.22f, 7); voice(&s, 0.04f, BR_WAVE_SINE, 2600, 3300, 0.28f, 0.10f, 8); synth_end(&s, &o[S_FREEZE]);
    synth_begin(&s, 0.32f);  voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.3f, 0.32f, 9); voice(&s, 0, BR_WAVE_SAW, 180, 80, 0.25f, 0.14f, 8); synth_end(&s, &o[S_FIRE]);
    synth_begin(&s, 0.12f);  voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.07f, 0.6f, 45); voice(&s, 0, BR_WAVE_SQUARE, 320, 110, 0.09f, 0.25f, 35); synth_end(&s, &o[S_SNIPE]);
    synth_begin(&s, 0.1f);   voice(&s, 0, BR_WAVE_SINE, 190, 90, 0.09f, 0.45f, 25); voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.03f, 0.18f, 80); synth_end(&s, &o[S_LAUNCH]);
    synth_begin(&s, 0.12f);  voice(&s, 0, BR_WAVE_SQUARE, 320, 140, 0.09f, 0.22f, 25); voice(&s, 0, BR_WAVE_NOISE, 0, 0, 0.05f, 0.3f, 60); synth_end(&s, &o[S_PLACE]);
    { static const float f[4] = { 523.3f, 659.3f, 784.0f, 1046.5f };
      synth_begin(&s, 0.4f);  notes(&s, 0, BR_WAVE_SQUARE, f, 4, 0.06f, 0.14f, 0.16f, 18); synth_end(&s, &o[S_UPGRADE]); }
    synth_begin(&s, 0.35f);  voice(&s, 0, BR_WAVE_SQUARE, 988, 988, 0.07f, 0.16f, 10); voice(&s, 0.07f, BR_WAVE_SQUARE, 1319, 1319, 0.26f, 0.16f, 10); synth_end(&s, &o[S_SELL]);
    synth_begin(&s, 0.35f);  voice(&s, 0, BR_WAVE_SAW, 220, 440, 0.28f, 0.16f, 3); voice(&s, 0, BR_WAVE_SQUARE, 440, 880, 0.28f, 0.07f, 3); synth_end(&s, &o[S_ROUND_START]);
    { static const float f[4] = { 784.0f, 987.8f, 1174.7f, 1568.0f };
      synth_begin(&s, 0.9f);  notes(&s, 0, BR_WAVE_TRIANGLE, f, 4, 0.08f, 0.6f, 0.26f, 5); synth_end(&s, &o[S_ROUND_END]); }
    synth_begin(&s, 0.32f);  voice(&s, 0, BR_WAVE_SAW, 120, 75, 0.3f, 0.32f, 5); synth_end(&s, &o[S_LEAK]);
    { static const float f[4] = { 392.0f, 329.6f, 261.6f, 196.0f };
      synth_begin(&s, 1.6f);  notes(&s, 0, BR_WAVE_SQUARE, f, 4, 0.3f, 0.5f, 0.2f, 3); synth_end(&s, &o[S_DEFEAT]); }
    { static const float f[6] = { 523.3f, 659.3f, 784.0f, 659.3f, 784.0f, 1046.5f };
      synth_begin(&s, 1.6f);  notes(&s, 0, BR_WAVE_SQUARE, f, 6, 0.12f, 0.35f, 0.16f, 4);
      voice(&s, 0.72f, BR_WAVE_TRIANGLE, 1046.5f, 1046.5f, 0.8f, 0.3f, 2.5f); synth_end(&s, &o[S_VICTORY]); }
    synth_begin(&s, 0.03f);  voice(&s, 0, BR_WAVE_SQUARE, 1800, 1800, 0.02f, 0.12f, 150); synth_end(&s, &o[S_CLICK]);
    synth_begin(&s, 0.15f);  voice(&s, 0, BR_WAVE_SQUARE, 140, 120, 0.14f, 0.22f, 12); synth_end(&s, &o[S_ERROR]);
    make_music(&o[S_MUSIC]);
}

/* ---- playing --------------------------------------------------------------------------- */

static void play(br_engine *e, sound_id id, float volume, float pitch, float x) {
    br_play_params p = br_play_defaults();
    p.volume = volume;
    p.pitch = pitch;
    p.pan = x < 0 ? 0.0f : br_clampf(x / TD_MAP_W * 2.0f - 1.0f, -1, 1) * 0.6f;
    br_play(&e->mixer, &A.sounds[id], &p);
}

static void rumble_all(br_engine *e, float heavy, float light, float seconds) {
    int i;
    for (i = 0; i < BR_MAX_GAMEPADS; ++i)
        if (br_pad_connected(&e->input, i)) br_gamepad_rumble(e, i, heavy, light, seconds);
}

/* Rule events arrive as one message type carrying a td_event. */
static bool on_td_event(const br_event *ev, void *user) {
    br_engine *e = (br_engine *)user;
    const td_event *t = BR_EVENT_DATA(ev, td_event);
    float jitter = br_rng_range(&A.rng, 0.9f, 1.12f);
    if (e->frame != A.frame) {
        A.frame = e->frame;
        A.pops_this_frame = A.hits_this_frame = A.booms_this_frame = 0;
    }
    switch (t->type) {
    case TD_EV_POP:          /* a few per frame is plenty when hundreds pop at once */
        if (t->kind == TD_BLIMP) { play(e, S_EXPLOSION, 1.0f, 0.6f, t->pos.x); rumble_all(e, 0.6f, 0.4f, 0.3f); break; }
        if (A.pops_this_frame++ < 3)
            play(e, (sound_id)(S_POP0 + br_rng_int(&A.rng, 0, 2)), 0.45f, jitter * (1.0f + 0.03f * (float)(TD_CERAMIC - t->kind)), t->pos.x);
        break;
    case TD_EV_HIT:      if (A.hits_this_frame++ < 2) play(e, S_HIT, 0.4f, jitter, t->pos.x); break;
    case TD_EV_BLOCKED:  if (A.hits_this_frame++ < 2) play(e, S_BLOCKED, 0.5f, jitter, t->pos.x); break;
    case TD_EV_EXPLOSION: if (A.booms_this_frame++ < 2) play(e, S_EXPLOSION, t->kind == TD_PROJ_BOMBLET ? 0.25f : 0.5f, jitter, t->pos.x); break;
    case TD_EV_PULSE:    play(e, t->kind == TD_ICE ? S_FREEZE : S_FIRE, 0.5f, jitter, t->pos.x); break;
    case TD_EV_SNIPE:    play(e, S_SNIPE, 0.5f, jitter, t->pos.x); break;
    case TD_EV_ATTACK:   if (t->kind == TD_BOMB) play(e, S_LAUNCH, 0.4f, jitter, t->pos.x); break;
    case TD_EV_TOWER_PLACED:   play(e, S_PLACE, 0.8f, 1.0f, t->pos.x); break;
    case TD_EV_TOWER_UPGRADED: play(e, S_UPGRADE, 0.8f, 1.0f, t->pos.x); break;
    case TD_EV_TOWER_SOLD:     play(e, S_SELL, 0.8f, 1.0f, t->pos.x); break;
    case TD_EV_ROUND_START:    play(e, S_ROUND_START, 0.7f, 1.0f, -1); break;
    case TD_EV_ROUND_END:      play(e, S_ROUND_END, 0.7f, 1.0f, -1); break;
    case TD_EV_LEAK:           play(e, S_LEAK, 0.6f, 1.0f, -1); rumble_all(e, 0.4f, 0.2f, 0.12f); break;
    case TD_EV_DEFEAT:         play(e, S_DEFEAT, 0.9f, 1.0f, -1); rumble_all(e, 0.9f, 0.6f, 0.5f); break;
    case TD_EV_VICTORY:        play(e, S_VICTORY, 0.9f, 1.0f, -1); rumble_all(e, 0.3f, 0.6f, 0.3f); break;
    default: break;
    }
    return false;
}

void sfx_init(br_engine *e, uint32_t event_type) {
    br_play_params p = br_play_defaults();
    memset(&A, 0, sizeof(A));
    br_rng_seed(&A.rng, 5);
    make_sounds();
    br_events_subscribe(&e->events, event_type, on_td_event, e, 0);
    p.loop = true;
    p.bus = BR_BUS_MUSIC;
    p.volume = 0.5f;
    A.music = br_play(&e->mixer, &A.sounds[S_MUSIC], &p);
}

void sfx_shutdown(br_engine *e) {
    int i;
    br_mixer_stop_all(&e->mixer);
    for (i = 0; i < S_COUNT; ++i) br_sound_destroy(&A.sounds[i]);
}

void sfx_ui(br_engine *e, sfx_ui_sound s) { play(e, s == SFX_CLICK ? S_CLICK : S_ERROR, 0.6f, 1.0f, -1); }

void sfx_toggle_music(br_engine *e) {
    A.music_muted = !A.music_muted;
    br_mixer_set_bus_volume(&e->mixer, BR_BUS_MUSIC, A.music_muted ? 0.0f : 1.0f);
}
