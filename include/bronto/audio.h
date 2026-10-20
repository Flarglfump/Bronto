/*
 * Bronto Engine - audio.
 *
 * A software mixer produces interleaved stereo int16 PCM; the platform
 * layer only has to hand that buffer to the OS (WASAPI/waveOut, CoreAudio,
 * ALSA, WebAudio). The engine pulls audio on the main thread once per
 * frame (the platform reports how many frames it wants), so the mixer
 * needs no locks or threads.
 *
 * Features: WAV loading (PCM 8/16/24/32-bit, float32; mono/stereo), any
 * source sample rate (resampled with linear interpolation), per-voice
 * volume / pan / pitch / looping, click-free volume ramps, 4 mix buses
 * with their own volume, and simple waveform generation for prototyping.
 */
#ifndef BRONTO_AUDIO_H
#define BRONTO_AUDIO_H

#include "types.h"

typedef struct br_sound {
    int16_t *samples;     /* interleaved */
    uint32_t frames;      /* samples per channel */
    int channels;         /* 1 or 2 */
    int sample_rate;
} br_sound;

bool br_sound_load_wav(br_sound *s, const char *path);
bool br_sound_load_wav_mem(br_sound *s, const void *data, size_t size);
bool br_sound_save_wav(const br_sound *s, const char *path);
void br_sound_destroy(br_sound *s);
float br_sound_duration(const br_sound *s);

typedef enum br_waveform {
    BR_WAVE_SINE = 0, BR_WAVE_SQUARE, BR_WAVE_TRIANGLE, BR_WAVE_SAW, BR_WAVE_NOISE
} br_waveform;
/* Generates a mono tone with a short attack/release envelope. */
bool br_sound_generate(br_sound *s, br_waveform wave, float frequency, float seconds,
                       float volume, int sample_rate);

/* ---- mixer -------------------------------------------------------------- */
#define BR_AUDIO_MAX_VOICES 32
#define BR_AUDIO_BUS_COUNT  4
enum { BR_BUS_SFX = 0, BR_BUS_MUSIC = 1, BR_BUS_UI = 2, BR_BUS_AMBIENT = 3 };

typedef uint32_t br_voice;  /* handle; 0 = invalid */

typedef struct br_play_params {
    float volume;   /* 0..1+   (default 1) */
    float pan;      /* -1 left .. +1 right (default 0) */
    float pitch;    /* playback rate multiplier (default 1) */
    bool loop;
    int bus;        /* BR_BUS_* (default SFX) */
} br_play_params;

typedef struct br_voice_state {
    const br_sound *sound;
    double position;          /* in source frames */
    br_play_params params;
    float gain_l, gain_r;     /* current (ramped) gains */
    uint32_t generation;
    uint64_t start_order;
    bool active, paused, stopping;
} br_voice_state;

typedef struct br_mixer {
    int sample_rate;
    br_voice_state voices[BR_AUDIO_MAX_VOICES];
    float bus_volume[BR_AUDIO_BUS_COUNT];
    float master_volume;
    uint32_t generation_counter;
    uint64_t play_counter;
    uint64_t frames_mixed;
} br_mixer;

br_play_params br_play_defaults(void);

void br_mixer_init(br_mixer *m, int sample_rate);
br_voice br_play(br_mixer *m, const br_sound *s, const br_play_params *params); /* params may be NULL */
void br_voice_stop(br_mixer *m, br_voice v);       /* fades out over a few ms */
void br_voice_set_paused(br_mixer *m, br_voice v, bool paused);
void br_voice_set_volume(br_mixer *m, br_voice v, float volume);
void br_voice_set_pan(br_mixer *m, br_voice v, float pan);
void br_voice_set_pitch(br_mixer *m, br_voice v, float pitch);
bool br_voice_playing(const br_mixer *m, br_voice v);
void br_mixer_stop_all(br_mixer *m);
/* Immediately silence voices using `s` - call before br_sound_destroy. */
void br_mixer_stop_sound(br_mixer *m, const br_sound *s);
void br_mixer_set_master(br_mixer *m, float volume);
void br_mixer_set_bus_volume(br_mixer *m, int bus, float volume);
uint32_t br_mixer_active_voices(const br_mixer *m);

/* Render `frames` stereo frames (2 samples each). */
void br_mixer_mix_f32(br_mixer *m, float *out, uint32_t frames);
void br_mixer_mix_s16(br_mixer *m, int16_t *out, uint32_t frames);

/* ---- lock-free ring buffer ------------------------------------------------
 * Single-producer / single-consumer queue of interleaved stereo int16
 * frames. The game thread writes, an OS audio thread reads (used by
 * callback-driven backends such as Core Audio). No locks, no allocation
 * after init, safe across exactly one writer thread and one reader thread. */
typedef struct br_audio_ring {
    int16_t *data;
    uint32_t capacity;          /* frames, power of two */
    volatile uint32_t read_pos; /* free-running frame counters */
    volatile uint32_t write_pos;
} br_audio_ring;

bool     br_audio_ring_init(br_audio_ring *r, uint32_t min_frames);
void     br_audio_ring_destroy(br_audio_ring *r);
uint32_t br_audio_ring_available(const br_audio_ring *r);   /* frames readable */
uint32_t br_audio_ring_space(const br_audio_ring *r);       /* frames writable */
uint32_t br_audio_ring_write(br_audio_ring *r, const int16_t *frames, uint32_t count);
/* Reads up to `count` frames; returns how many were read (the caller
 * fills the rest with silence on underrun). */
uint32_t br_audio_ring_read(br_audio_ring *r, int16_t *out, uint32_t count);

#endif /* BRONTO_AUDIO_H */
