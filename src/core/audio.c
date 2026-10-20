#include "bronto/audio.h"
#include "bronto/memory.h"
#include "bronto/log.h"
#include "bronto/bmath.h"

#include <string.h>

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

/* ---- WAV ---------------------------------------------------------------- */

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

bool br_sound_load_wav_mem(br_sound *s, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    size_t pos = 12;
    uint32_t format = 0, channels = 0, rate = 0, bits = 0, block_align = 0;
    const uint8_t *pcm = NULL;
    uint32_t pcm_size = 0, frames, f, out_ch;
    memset(s, 0, sizeof(*s));
    if (size < 12 || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) {
        BR_ERROR("WAV: not a RIFF/WAVE file");
        return false;
    }
    while (pos + 8 <= size) {
        uint32_t id_len = le32(p + pos + 4);
        const uint8_t *body = p + pos + 8;
        size_t avail = size - (pos + 8);
        if (id_len > avail) id_len = (uint32_t)avail;  /* tolerate truncated last chunk */
        if (memcmp(p + pos, "fmt ", 4) == 0 && id_len >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            rate = le32(body + 4);
            block_align = le16(body + 12);
            bits = le16(body + 14);
            if (format == 0xFFFEu && id_len >= 26) format = le16(body + 24); /* EXTENSIBLE */
        } else if (memcmp(p + pos, "data", 4) == 0) {
            pcm = body;
            pcm_size = id_len;
        }
        pos += 8 + (size_t)id_len + (id_len & 1u);  /* chunks are word aligned */
    }
    if (!pcm || !channels || !rate || !block_align) { BR_ERROR("WAV: missing fmt or data chunk"); return false; }
    if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
          (format == 3 && bits == 32))) {
        BR_ERROR("WAV: unsupported encoding (format %u, %u bits)", format, bits);
        return false;
    }
    frames = pcm_size / block_align;
    out_ch = channels >= 2 ? 2 : 1;   /* extra channels are dropped */
    s->samples = (int16_t *)br_alloc((size_t)frames * out_ch * sizeof(int16_t) + 2);
    if (!s->samples) return false;
    s->frames = frames;
    s->channels = (int)out_ch;
    s->sample_rate = (int)rate;
    for (f = 0; f < frames; ++f) {
        uint32_t c;
        for (c = 0; c < out_ch; ++c) {
            const uint8_t *q = pcm + (size_t)f * block_align + (size_t)c * (bits / 8);
            int32_t v;
            if (format == 3) {
                float fv;
                uint32_t raw = le32(q);
                memcpy(&fv, &raw, sizeof(fv));
                fv = br_clampf(fv, -1.0f, 1.0f);
                v = (int32_t)(fv * 32767.0f);
            } else if (bits == 8) {
                v = ((int32_t)q[0] - 128) * 256;   /* multiply: shifting a negative is UB */
            } else if (bits == 16) {
                v = (int16_t)le16(q);
            } else if (bits == 24) {
                v = (int32_t)(((uint32_t)q[0] << 8) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 24)) >> 16;
            } else {
                v = (int32_t)le32(q) >> 16;
            }
            s->samples[(size_t)f * out_ch + c] = (int16_t)v;
        }
    }
    return true;
}

bool br_sound_load_wav(br_sound *s, const char *path) {
    size_t size;
    void *data = br_file_read_all(path, &size);
    bool ok;
    if (!data) { BR_ERROR("could not read sound '%s'", path); memset(s, 0, sizeof(*s)); return false; }
    ok = br_sound_load_wav_mem(s, data, size);
    br_free(data);
    return ok;
}

static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v & 0xFFFFu); w16(p + 2, v >> 16); }

bool br_sound_save_wav(const br_sound *s, const char *path) {
    uint32_t data_bytes = s->frames * (uint32_t)s->channels * 2u;
    size_t total = 44 + (size_t)data_bytes;
    uint8_t *buf = (uint8_t *)br_calloc(1, total);
    uint32_t i;
    bool ok;
    if (!buf) return false;
    memcpy(buf, "RIFF", 4); w32(buf + 4, (uint32_t)total - 8); memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4); w32(buf + 16, 16);
    w16(buf + 20, 1); w16(buf + 22, (uint32_t)s->channels);
    w32(buf + 24, (uint32_t)s->sample_rate);
    w32(buf + 28, (uint32_t)s->sample_rate * (uint32_t)s->channels * 2u);
    w16(buf + 32, (uint32_t)s->channels * 2u); w16(buf + 34, 16);
    memcpy(buf + 36, "data", 4); w32(buf + 40, data_bytes);
    for (i = 0; i < s->frames * (uint32_t)s->channels; ++i) w16(buf + 44 + i * 2, (uint16_t)s->samples[i]);
    ok = br_file_write_all(path, buf, total);
    br_free(buf);
    return ok;
}

void br_sound_destroy(br_sound *s) { br_free(s->samples); memset(s, 0, sizeof(*s)); }

float br_sound_duration(const br_sound *s) {
    return s->sample_rate ? (float)s->frames / (float)s->sample_rate : 0.0f;
}

bool br_sound_generate(br_sound *s, br_waveform wave, float freq, float seconds, float volume, int rate) {
    uint32_t i, n, env;
    uint32_t noise = 0x12345678u;
    float phase = 0, step;
    memset(s, 0, sizeof(*s));
    if (rate <= 0 || seconds <= 0) return false;
    n = (uint32_t)(seconds * (float)rate);
    if (!n) return false;
    s->samples = (int16_t *)br_alloc(n * sizeof(int16_t));
    if (!s->samples) return false;
    s->frames = n;
    s->channels = 1;
    s->sample_rate = rate;
    step = freq / (float)rate;
    env = BR_MIN((uint32_t)rate / 200u, n / 4u);  /* 5 ms attack & release */
    for (i = 0; i < n; ++i) {
        float v, e = 1.0f;
        switch (wave) {
        case BR_WAVE_SQUARE:   v = phase < 0.5f ? 1.0f : -1.0f; break;
        case BR_WAVE_TRIANGLE: v = 4.0f * br_absf(phase - 0.5f) - 1.0f; break;
        case BR_WAVE_SAW:      v = 2.0f * phase - 1.0f; break;
        case BR_WAVE_NOISE:
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            v = (float)(noise & 0xFFFFu) / 32767.5f - 1.0f;
            break;
        case BR_WAVE_SINE:
        default:               v = sinf(phase * BR_TAU); break;
        }
        if (env) {
            if (i < env) e = (float)i / (float)env;
            else if (i >= n - env) e = (float)(n - 1 - i) / (float)env;
        }
        s->samples[i] = (int16_t)(br_clampf(v * volume * e, -1.0f, 1.0f) * 32767.0f);
        phase += step;
        phase -= (float)(int)phase;
    }
    return true;
}

/* ---- mixer ------------------------------------------------------------------ */

#define VOICE_INDEX_BITS 8u
#define RAMP_FRAMES 128u   /* ~3 ms at 44.1 kHz */

br_play_params br_play_defaults(void) {
    br_play_params p;
    p.volume = 1.0f;
    p.pan = 0.0f;
    p.pitch = 1.0f;
    p.loop = false;
    p.bus = BR_BUS_SFX;
    return p;
}

void br_mixer_init(br_mixer *m, int sample_rate) {
    int i;
    memset(m, 0, sizeof(*m));
    m->sample_rate = sample_rate > 0 ? sample_rate : 44100;
    m->master_volume = 1.0f;
    for (i = 0; i < BR_AUDIO_BUS_COUNT; ++i) m->bus_volume[i] = 1.0f;
}

static br_voice_state *resolve(const br_mixer *m, br_voice v) {
    uint32_t idx = v & ((1u << VOICE_INDEX_BITS) - 1u);
    const br_voice_state *vs;
    if (v == 0 || idx >= BR_AUDIO_MAX_VOICES) return NULL;
    vs = &m->voices[idx];
    if (!vs->active || vs->generation != (v >> VOICE_INDEX_BITS)) return NULL;
    return (br_voice_state *)vs;
}

static void target_gains(const br_mixer *m, const br_voice_state *v, float *l, float *r) {
    int bus = BR_CLAMP(v->params.bus, 0, BR_AUDIO_BUS_COUNT - 1);
    float g = v->params.volume * m->bus_volume[bus] * m->master_volume;
    float pan = br_clampf(v->params.pan, -1.0f, 1.0f);
    if (v->stopping || v->paused) g = 0.0f;
    /* Linear balance: centre = full volume on both channels. */
    *l = g * (pan > 0 ? 1.0f - pan : 1.0f);
    *r = g * (pan < 0 ? 1.0f + pan : 1.0f);
}

br_voice br_play(br_mixer *m, const br_sound *s, const br_play_params *params) {
    int i, slot = -1;
    br_voice_state *v;
    if (!s || !s->samples || !s->frames) return 0;
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i)
        if (!m->voices[i].active) { slot = i; break; }
    if (slot < 0) {
        /* Steal the oldest non-looping voice (or the oldest overall). */
        uint64_t best = (uint64_t)-1;
        int k;
        for (k = 0; k < 2 && slot < 0; ++k)
            for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) {
                if (k == 0 && m->voices[i].params.loop) continue;
                if (m->voices[i].start_order < best) { best = m->voices[i].start_order; slot = i; }
            }
    }
    v = &m->voices[slot];
    memset(v, 0, sizeof(*v));
    v->sound = s;
    v->params = params ? *params : br_play_defaults();
    if (v->params.pitch <= 0) v->params.pitch = 1.0f;
    v->active = true;
    m->generation_counter = (m->generation_counter + 1) & 0x00FFFFFFu;
    if (!m->generation_counter) m->generation_counter = 1;
    v->generation = m->generation_counter;
    v->start_order = ++m->play_counter;
    /* Start at full gain: sounds normally begin at a zero crossing. */
    target_gains(m, v, &v->gain_l, &v->gain_r);
    return (v->generation << VOICE_INDEX_BITS) | (uint32_t)slot;
}

void br_voice_stop(br_mixer *m, br_voice v) {
    br_voice_state *vs = resolve(m, v);
    if (vs) vs->stopping = true;
}
void br_voice_set_paused(br_mixer *m, br_voice v, bool paused) {
    br_voice_state *vs = resolve(m, v);
    if (vs) vs->paused = paused;
}
void br_voice_set_volume(br_mixer *m, br_voice v, float volume) {
    br_voice_state *vs = resolve(m, v);
    if (vs) vs->params.volume = volume < 0 ? 0 : volume;
}
void br_voice_set_pan(br_mixer *m, br_voice v, float pan) {
    br_voice_state *vs = resolve(m, v);
    if (vs) vs->params.pan = br_clampf(pan, -1, 1);
}
void br_voice_set_pitch(br_mixer *m, br_voice v, float pitch) {
    br_voice_state *vs = resolve(m, v);
    if (vs && pitch > 0) vs->params.pitch = pitch;
}
bool br_voice_playing(const br_mixer *m, br_voice v) {
    const br_voice_state *vs = resolve(m, v);
    return vs && !vs->stopping;
}

void br_mixer_stop_all(br_mixer *m) {
    int i;
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i)
        if (m->voices[i].active) m->voices[i].stopping = true;
}

void br_mixer_stop_sound(br_mixer *m, const br_sound *s) {
    int i;
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i)
        if (m->voices[i].active && m->voices[i].sound == s) m->voices[i].active = false;
}

void br_mixer_set_master(br_mixer *m, float volume) { m->master_volume = volume < 0 ? 0 : volume; }
void br_mixer_set_bus_volume(br_mixer *m, int bus, float volume) {
    if (bus >= 0 && bus < BR_AUDIO_BUS_COUNT) m->bus_volume[bus] = volume < 0 ? 0 : volume;
}

uint32_t br_mixer_active_voices(const br_mixer *m) {
    uint32_t n = 0;
    int i;
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) n += m->voices[i].active ? 1u : 0u;
    return n;
}

static float sample_at(const br_sound *s, uint32_t frame, int ch) {
    return (float)s->samples[(size_t)frame * (size_t)s->channels + (size_t)(s->channels == 2 ? ch : 0)] *
           (1.0f / 32768.0f);
}

static void mix_voice(br_mixer *m, br_voice_state *v, float *out, uint32_t frames) {
    const br_sound *s = v->sound;
    double step = (double)v->params.pitch * (double)s->sample_rate / (double)m->sample_rate;
    float tl, tr, dl, dr;
    uint32_t i, ramp = BR_MIN(frames, RAMP_FRAMES);
    target_gains(m, v, &tl, &tr);
    dl = (tl - v->gain_l) / (float)ramp;
    dr = (tr - v->gain_r) / (float)ramp;

    for (i = 0; i < frames; ++i) {
        uint32_t f0 = (uint32_t)v->position, f1 = f0 + 1;
        float t = (float)(v->position - (double)f0), l, r;
        if (f1 >= s->frames) f1 = v->params.loop ? 0 : f0;
        l = sample_at(s, f0, 0) + (sample_at(s, f1, 0) - sample_at(s, f0, 0)) * t;
        r = sample_at(s, f0, 1) + (sample_at(s, f1, 1) - sample_at(s, f0, 1)) * t;
        if (i < ramp) { v->gain_l += dl; v->gain_r += dr; }
        else { v->gain_l = tl; v->gain_r = tr; }
        out[i * 2 + 0] += l * v->gain_l;
        out[i * 2 + 1] += r * v->gain_r;

        v->position += step;
        if (v->position >= (double)s->frames) {
            if (v->params.loop) {
                while (v->position >= (double)s->frames) v->position -= (double)s->frames;
            } else {
                v->active = false;
                return;
            }
        }
    }
    /* A stopping voice that has faded to silence is released. */
    if (v->stopping && v->gain_l <= 1e-4f && v->gain_r <= 1e-4f) v->active = false;
}

void br_mixer_mix_f32(br_mixer *m, float *out, uint32_t frames) {
    int i;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    for (i = 0; i < BR_AUDIO_MAX_VOICES; ++i) {
        br_voice_state *v = &m->voices[i];
        if (!v->active) continue;
        if (v->paused && v->gain_l <= 1e-4f && v->gain_r <= 1e-4f) continue; /* hold position */
        mix_voice(m, v, out, frames);
    }
    m->frames_mixed += frames;
}

void br_mixer_mix_s16(br_mixer *m, int16_t *out, uint32_t frames) {
    float tmp[512 * 2];
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = BR_MIN(frames - done, 512u), i;
        br_mixer_mix_f32(m, tmp, n);
        for (i = 0; i < n * 2; ++i) {
            float v = br_clampf(tmp[i], -1.0f, 1.0f);
            out[done * 2 + i] = (int16_t)(v * 32767.0f);
        }
        done += n;
    }
}

/* ---- lock-free SPSC ring ----------------------------------------------------- */

/* Acquire loads / release stores on the shared counters: the reader must
 * see the samples before it sees the advanced write position, and vice
 * versa. */
static uint32_t ring_load(const volatile uint32_t *p) {
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#elif defined(_MSC_VER)
    return (uint32_t)_InterlockedOr((volatile long *)p, 0);   /* full barrier */
#else
    return *p;
#endif
}

static void ring_store(volatile uint32_t *p, uint32_t v) {
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
#elif defined(_MSC_VER)
    _InterlockedExchange((volatile long *)p, (long)v);
#else
    *p = v;
#endif
}

bool br_audio_ring_init(br_audio_ring *r, uint32_t min_frames) {
    uint32_t cap = 64;
    memset(r, 0, sizeof(*r));
    while (cap < min_frames) cap <<= 1;
    r->data = (int16_t *)br_calloc((size_t)cap * 2u, sizeof(int16_t));
    if (!r->data) return false;
    r->capacity = cap;
    return true;
}

void br_audio_ring_destroy(br_audio_ring *r) {
    br_free(r->data);
    memset(r, 0, sizeof(*r));
}

uint32_t br_audio_ring_available(const br_audio_ring *r) {
    return ring_load(&r->write_pos) - ring_load(&r->read_pos);
}

uint32_t br_audio_ring_space(const br_audio_ring *r) {
    return r->capacity - br_audio_ring_available(r);
}

uint32_t br_audio_ring_write(br_audio_ring *r, const int16_t *frames, uint32_t count) {
    uint32_t w = r->write_pos;                 /* only this thread writes it */
    uint32_t space = r->capacity - (w - ring_load(&r->read_pos));
    uint32_t n = count < space ? count : space, first, idx = w & (r->capacity - 1);
    first = r->capacity - idx;
    if (first > n) first = n;
    memcpy(r->data + (size_t)idx * 2u, frames, (size_t)first * 2u * sizeof(int16_t));
    if (n > first) memcpy(r->data, frames + (size_t)first * 2u, (size_t)(n - first) * 2u * sizeof(int16_t));
    ring_store(&r->write_pos, w + n);
    return n;
}

uint32_t br_audio_ring_read(br_audio_ring *r, int16_t *out, uint32_t count) {
    uint32_t rd = r->read_pos;                 /* only this thread writes it */
    uint32_t avail = ring_load(&r->write_pos) - rd;
    uint32_t n = count < avail ? count : avail, first, idx = rd & (r->capacity - 1);
    first = r->capacity - idx;
    if (first > n) first = n;
    memcpy(out, r->data + (size_t)idx * 2u, (size_t)first * 2u * sizeof(int16_t));
    if (n > first) memcpy(out + (size_t)first * 2u, r->data, (size_t)(n - first) * 2u * sizeof(int16_t));
    ring_store(&r->read_pos, rd + n);
    return n;
}
