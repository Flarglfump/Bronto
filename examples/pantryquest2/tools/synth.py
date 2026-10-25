"""A small chiptune synthesiser: two pulse voices, a triangle, noise drums, as an old
console's sound chip would play them (with a little vibrato and echo to taste).

Only Python's standard library. Tracks are mixed as floats and written as 16-bit WAVs.
"""
import math
import random
import struct

NOTES = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7,
         'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}


def freq(note):
    """'A4' -> 440 Hz; numbers pass through."""
    if not isinstance(note, str):
        return note
    name, octave = note[:-1], int(note[-1])
    return 440.0 * 2 ** ((NOTES[name] + 12 * (octave + 1) - 69) / 12)


def midi(note):
    name, octave = note[:-1], int(note[-1])
    return NOTES[name] + 12 * (octave + 1)


def note_name(m):
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    return names[m % 12] + str(m // 12 - 1)


def transpose(note, semis):
    return note_name(midi(note) + semis)


class Track:
    def __init__(self, seconds, rate):
        self.rate = rate
        self.buf = [0.0] * int(seconds * rate)

    def add(self, start, samples, gain=1.0):
        i0 = int(start * self.rate)
        buf = self.buf
        n = len(buf)
        for i, v in enumerate(samples):
            j = i0 + i
            if 0 <= j < n:
                buf[j] += v * gain

    def add_wrapped(self, start, samples, gain=1.0):
        """For loops: what runs past the end comes in again at the start."""
        i0 = int(start * self.rate)
        buf = self.buf
        n = len(buf)
        for i, v in enumerate(samples):
            buf[(i0 + i) % n] += v * gain

    def echo(self, delay, amount, wrapped=True):
        d = int(delay * self.rate)
        out = list(self.buf)
        n = len(out)
        for i in range(n):
            j = i - d
            if j >= 0 or wrapped:
                out[i] += self.buf[j % n] * amount
        self.buf = out

    def write(self, path, gain=1.0):
        peak = max(1e-6, max(abs(v) for v in self.buf))
        scale = min(gain, 0.92 / peak)
        data = bytearray()
        for v in self.buf:
            data += struct.pack('<h', int(max(-1, min(1, v * scale)) * 32767))
        with open(path, 'wb') as f:
            f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
            f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, self.rate, self.rate * 2, 2, 16))
            f.write(b'data' + struct.pack('<I', len(data)) + bytes(data))


def tone(rate, f0, seconds, wave='square', duty=0.5, vol=0.3, f1=None, attack=0.004, release=0.04,
         vibrato=0.0, vib_delay=0.12, decay=None, sustain=1.0, seed=0):
    """One note: a frequency (sliding to f1), with an envelope."""
    n = max(1, int(seconds * rate))
    out = [0.0] * n
    phase = 0.0
    f0 = freq(f0)
    f1 = freq(f1) if f1 is not None else f0
    rng = random.Random(int(f0 * 13) + n + seed)
    noise_v, noise_t = 0.0, 0.0
    lfsr = 1
    for i in range(n):
        t = i / rate
        f = f0 + (f1 - f0) * (i / n)
        if vibrato and t > vib_delay:
            f *= 1 + 0.015 * vibrato * math.sin(2 * math.pi * 5.5 * (t - vib_delay))
        phase += f / rate
        phase -= int(phase)
        if wave == 'square':
            v = 1.0 if phase < duty else -1.0
        elif wave == 'triangle':                          # stepped, as the chip's was
            v = (4 * abs(phase - 0.5) - 1)
            v = round(v * 7.5) / 7.5
        elif wave == 'saw':
            v = 2 * phase - 1
        elif wave == 'sine':
            v = math.sin(2 * math.pi * phase)
        else:                                             # noise from a shift register, clocked at f
            noise_t += f / rate
            while noise_t >= 1:
                noise_t -= 1
                bit = (lfsr ^ (lfsr >> 1)) & 1
                lfsr = (lfsr >> 1) | (bit << 14)
                noise_v = 1.0 if lfsr & 1 else -1.0
            v = noise_v
        env = 1.0
        if t < attack:
            env = t / attack
        if decay:
            env *= sustain + (1 - sustain) * math.exp(-t / decay)
        if t > seconds - release:
            env *= max(0.0, (seconds - t) / release)
        out[i] = v * env * vol
    return out


def drum(rate, kind, vol=0.3):
    if kind == 'kick':
        return [a + b for a, b in zip(tone(rate, 140, 0.12, 'triangle', vol=vol * 1.6, f1=40, decay=0.05),
                                      tone(rate, 2000, 0.12, 'noise', vol=vol * 0.3, decay=0.01) + [0.0] * 0)]
    if kind == 'snare':
        return [a + b for a, b in zip(tone(rate, 4000, 0.14, 'noise', vol=vol, decay=0.05),
                                      tone(rate, 220, 0.14, 'triangle', vol=vol * 0.6, f1=160, decay=0.04))]
    if kind == 'hat':
        return tone(rate, 9000, 0.04, 'noise', vol=vol * 0.45, decay=0.012)
    if kind == 'crash':
        return tone(rate, 7000, 0.6, 'noise', vol=vol * 0.6, decay=0.2)
    return []
