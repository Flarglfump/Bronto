#!/usr/bin/env python3
"""Synthesises Pantry Quest's sound effects and music (all original), as WAV files.

    python3 examples/pantryquest/tools/make_sound.py

Square, triangle and noise voices, like an old console's sound chip. Only
Python's standard library is used. Effects go to sfx/ (22050 Hz), music to
music/ (16000 Hz loops).
"""
import math
import os
import random
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..')
NOTES = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}


def freq(note):
    """'A4' -> 440 Hz; numbers pass through."""
    if not isinstance(note, str):
        return note
    name, octave = note[:-1], int(note[-1])
    return 440.0 * 2 ** ((NOTES[name] + 12 * (octave + 1) - 69) / 12)


class Track:
    def __init__(self, seconds, rate):
        self.rate = rate
        self.buf = [0.0] * int(seconds * rate)

    def add(self, start, samples):
        i0 = int(start * self.rate)
        for i, v in enumerate(samples):
            if 0 <= i0 + i < len(self.buf):
                self.buf[i0 + i] += v

    def write(self, path, gain=1.0):
        peak = max(1e-6, max(abs(v) for v in self.buf))
        scale = min(gain, 0.95 / peak) if peak * gain > 0.95 else gain
        data = b''.join(struct.pack('<h', int(max(-1, min(1, v * scale)) * 32767)) for v in self.buf)
        with open(path, 'wb') as f:
            f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
            f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, self.rate, self.rate * 2, 2, 16))
            f.write(b'data' + struct.pack('<I', len(data)) + data)


def tone(rate, f0, seconds, wave='square', duty=0.5, vol=0.3, f1=None, attack=0.005, release=0.05,
         vibrato=0.0, decay=None):
    """One note: a frequency (sliding to f1 if given), shaped by a simple envelope."""
    n = int(seconds * rate)
    out = []
    phase = 0.0
    f0 = freq(f0)
    f1 = freq(f1) if f1 is not None else f0
    rng = random.Random(int(f0 * 7) + n)
    noise_v, noise_t = 0.0, 0.0
    for i in range(n):
        t = i / rate
        f = f0 + (f1 - f0) * (i / max(1, n - 1))
        if vibrato:
            f *= 1 + 0.012 * math.sin(2 * math.pi * 6 * t) * vibrato
        phase = (phase + f / rate) % 1.0
        if wave == 'square':
            v = 1.0 if phase < duty else -1.0
        elif wave == 'triangle':
            v = 4 * abs(phase - 0.5) - 1
        elif wave == 'saw':
            v = 2 * phase - 1
        else:                                   # noise, re-sampled at the "frequency"
            noise_t += f / rate
            if noise_t >= 1:
                noise_t -= 1
                noise_v = rng.uniform(-1, 1)
            v = noise_v
        env = 1.0
        if t < attack:
            env = t / attack
        if decay:
            env *= math.exp(-t / decay)
        if t > seconds - release:
            env *= max(0.0, (seconds - t) / release)
        out.append(v * env * vol)
    return out


def sfx(name, seconds, parts):
    tr = Track(seconds, 22050)
    for start, samples in parts:
        tr.add(start, samples)
    tr.write(os.path.join(OUT, 'sfx', name + '.wav'))


def seq(rate, notes, step, start=0.0, **kw):
    """Notes one after another: [(note or None, length in steps), ...]."""
    parts, t = [], start
    for note, length in notes:
        if note is not None:
            parts.append((t, tone(rate, note, length * step * 0.95, **kw)))
        t += length * step
    return parts


def make_sfx():
    R = 22050
    sfx('swing', 0.16, [(0, tone(R, 1800, 0.14, 'noise', vol=0.25, f1=600, decay=0.05)),
                        (0, tone(R, 'C6', 0.1, duty=0.125, vol=0.12, f1='C5'))])
    sfx('hit', 0.12, [(0, tone(R, 'C3', 0.1, duty=0.25, vol=0.35, f1='G2')),
                      (0, tone(R, 3000, 0.06, 'noise', vol=0.2, decay=0.02))])
    sfx('enemy_die', 0.3, seq(R, [('G5', 1), ('D5', 1), ('A4', 1), ('E4', 2)], 0.05, duty=0.25, vol=0.25))
    sfx('hurt', 0.3, [(0, tone(R, 'A4', 0.28, duty=0.5, vol=0.3, f1='A3')),
                      (0, tone(R, 900, 0.2, 'noise', vol=0.15, decay=0.08))])
    sfx('bean', 0.2, seq(R, [('E6', 1), ('B6', 3)], 0.045, duty=0.25, vol=0.22))
    sfx('heart', 0.22, seq(R, [('C6', 1), ('E6', 1), ('G6', 1), ('C7', 2)], 0.035, duty=0.25, vol=0.2))
    sfx('key', 0.4, seq(R, [('A6', 1), ('E6', 1), ('A6', 3)], 0.07, wave='triangle', vol=0.4))
    sfx('item', 1.5, seq(R, [('C5', 2), ('E5', 2), ('G5', 2), ('C6', 2), ('G5', 1), ('C6', 7)], 0.08, duty=0.25, vol=0.25) +
        seq(R, [('C4', 8), ('G3', 8)], 0.08, wave='triangle', vol=0.35))
    sfx('secret', 1.0, seq(R, [('G5', 1), ('A5', 1), ('C6', 1), ('E6', 1), ('D6', 1), ('G6', 4)], 0.09,
                           wave='triangle', vol=0.4) +
        seq(R, [('G4', 1), ('A4', 1), ('C5', 1), ('E5', 1), ('D5', 1), ('G5', 4)], 0.09, duty=0.125, vol=0.12))
    sfx('door', 0.3, [(0, tone(R, 300, 0.25, 'noise', vol=0.3, decay=0.08)),
                      (0, tone(R, 'C3', 0.15, vol=0.2, f1='A2'))])
    sfx('shutter', 0.35, [(0, tone(R, 2000, 0.3, 'noise', vol=0.2, f1=400, decay=0.15)),
                          (0.05, tone(R, 'E3', 0.2, duty=0.25, vol=0.2))])
    sfx('bomb_place', 0.12, [(0, tone(R, 'A3', 0.1, duty=0.5, vol=0.25, f1='E3'))])
    sfx('explode', 0.8, [(0, tone(R, 1200, 0.75, 'noise', vol=0.5, f1=150, decay=0.25)),
                         (0, tone(R, 'C2', 0.4, 'triangle', vol=0.5, f1='C1'))])
    sfx('pop', 0.6, [(0.0, tone(R, 2500, 0.08, 'noise', vol=0.4, decay=0.03)),
                     (0.07, tone(R, 3200, 0.07, 'noise', vol=0.35, decay=0.03)),
                     (0.15, tone(R, 2200, 0.08, 'noise', vol=0.35, decay=0.03)),
                     (0.22, tone(R, 900, 0.35, 'noise', vol=0.4, f1=200, decay=0.12))])
    sfx('cutter', 0.18, [(0, tone(R, 'E5', 0.16, duty=0.125, vol=0.12, f1='B5', vibrato=4))])
    sfx('stairs', 0.6, seq(R, [('G4', 1), ('E4', 1), ('C4', 1), ('G3', 1), ('E3', 2)], 0.09, wave='triangle', vol=0.4))
    sfx('low_health', 0.3, [(0, tone(R, 'A5', 0.08, duty=0.25, vol=0.15)), (0.13, tone(R, 'A5', 0.08, duty=0.25, vol=0.15))])
    sfx('text', 0.04, [(0, tone(R, 'C6', 0.03, duty=0.125, vol=0.12))])
    sfx('tink', 0.15, [(0, tone(R, 'E7', 0.12, 'triangle', vol=0.4, decay=0.04))])
    sfx('roar', 0.9, [(0, tone(R, 'C3', 0.85, 'saw', vol=0.3, f1='F2', vibrato=6)),
                      (0, tone(R, 400, 0.85, 'noise', vol=0.12, decay=0.4))])
    sfx('boss_die', 1.6, [(i * 0.12, tone(R, 900 - i * 60, 0.2, 'noise', vol=0.4, decay=0.08)) for i in range(10)] +
        seq(R, [('C5', 1), ('G4', 1), ('E4', 1), ('C4', 1), ('G3', 4)], 0.1, duty=0.25, vol=0.2, start=0.4))
    sfx('page', 2.8, seq(R, [('G4', 2), ('C5', 2), ('E5', 2), ('G5', 2), ('E5', 1), ('G5', 1), ('C6', 6),
                             ('A5', 2), ('B5', 2), ('C6', 8)], 0.09, duty=0.25, vol=0.25) +
        seq(R, [('C4', 8), ('C4', 8), ('F3', 4), ('G3', 4), ('C4', 8)], 0.09, wave='triangle', vol=0.35))
    sfx('game_over', 2.6, seq(R, [('E5', 3), ('D5', 3), ('C5', 3), ('B4', 3), ('A4', 3), ('G#4', 3), ('A4', 8)], 0.09,
                              duty=0.5, vol=0.2) +
        seq(R, [('A3', 9), ('E3', 9), ('A2', 8)], 0.09, wave='triangle', vol=0.35))
    sfx('fall', 0.7, [(0, tone(R, 'C6', 0.65, duty=0.25, vol=0.2, f1='C3'))])
    sfx('menu', 0.08, [(0, tone(R, 'G6', 0.06, duty=0.25, vol=0.18))])
    sfx('buy', 0.3, seq(R, [('C6', 1), ('G6', 1), ('C7', 2)], 0.06, duty=0.25, vol=0.2))
    # the Pickle Pantry and the Spice Rack
    sfx('skewer', 0.2, [(0, tone(R, 2600, 0.18, 'noise', vol=0.22, f1=1200, decay=0.06)),
                        (0, tone(R, 'A6', 0.12, 'triangle', vol=0.15, f1='E6'))])
    sfx('fire', 0.45, [(0, tone(R, 700, 0.42, 'noise', vol=0.35, f1=250, decay=0.2)),
                       (0.02, tone(R, 'D3', 0.25, 'saw', vol=0.12, f1='A2'))])
    sfx('charge', 0.6, [(0, tone(R, 'E2', 0.55, 'saw', vol=0.3, f1='E3', vibrato=8)),
                        (0, tone(R, 300, 0.5, 'noise', vol=0.2, decay=0.3))])
    sfx('teleport', 0.45, [(0, tone(R, 'C5', 0.4, duty=0.125, vol=0.15, f1='C7', vibrato=10)),
                           (0.05, tone(R, 'G5', 0.35, 'triangle', vol=0.2, f1='G3'))])
    sfx('splash', 0.4, [(0, tone(R, 900, 0.35, 'noise', vol=0.3, f1=200, decay=0.12)),
                        (0, tone(R, 'G3', 0.2, 'triangle', vol=0.3, f1='C3'))])
    sfx('whisk', 1.0, [(0, tone(R, 400, 0.95, 'noise', vol=0.25, f1=2400, decay=0.6)),
                       (0, tone(R, 'C4', 0.9, duty=0.125, vol=0.12, f1='C6', vibrato=14))])


# ---------------------------------------------------------------- music
def song(name, bpm, rate, voices, bars, gain=1.0):
    """voices: (notes, step in beats, voice settings); every voice spans `bars` bars of 4 beats."""
    beat = 60.0 / bpm
    length = bars * 4 * beat
    tr = Track(length, rate)
    for notes, step, kw in voices:
        t = 0.0
        for note, n in notes:
            dur = n * step * beat
            if note is not None:
                if kw.get('wave') == 'noise':
                    tr.add(t, tone(rate, note, min(dur, 0.12), release=0.02, **kw))
                else:
                    tr.add(t, tone(rate, note, dur * 0.92, release=min(0.06, dur * 0.3), **kw))
            t += dur
    tr.write(os.path.join(OUT, 'music', name + '.wav'), gain)


def chord_bass(roots, per, octave):
    out = []
    for r in roots:
        out += [(r + str(octave), 1)] * per
    return out


def drums(bars, pattern):
    out = []
    for _ in range(bars):
        for hit in pattern:
            out.append((None if hit == '.' else (110 if hit == 'k' else 3500 if hit == 's' else 9000), 1))
    return out


def make_music():
    # Overworld: an upbeat march in C (2 x 8 bars)
    lead = [
        ('C5', 2), ('E5', 1), ('G5', 1), ('C6', 3), ('B5', 1), ('A5', 2), ('G5', 2), ('E5', 3), (None, 1),
        ('F5', 2), ('A5', 1), ('C6', 1), ('D6', 3), ('C6', 1), ('B5', 2), ('G5', 2), ('D5', 3), (None, 1),
        ('C5', 2), ('E5', 1), ('G5', 1), ('C6', 3), ('D6', 1), ('E6', 2), ('D6', 1), ('C6', 1), ('A5', 3), ('G5', 1),
        ('F5', 2), ('E5', 1), ('D5', 1), ('G5', 2), ('B4', 2), ('C5', 6), (None, 2),
        ('A5', 2), ('A5', 1), ('B5', 1), ('C6', 2), ('A5', 2), ('G5', 2), ('E5', 2), ('C5', 4),
        ('F5', 2), ('F5', 1), ('G5', 1), ('A5', 2), ('F5', 2), ('E5', 2), ('G5', 2), ('C6', 4),
        ('D6', 2), ('C6', 1), ('B5', 1), ('A5', 2), ('B5', 2), ('C6', 2), ('G5', 2), ('E5', 4),
        ('F5', 2), ('A5', 2), ('G5', 2), ('B4', 2), ('C5', 8),
    ]
    bass = chord_bass(['C', 'C', 'G', 'G', 'F', 'F', 'C', 'C', 'F', 'F', 'D', 'D', 'G', 'G', 'G', 'G',
                       'C', 'C', 'G', 'G', 'A', 'A', 'F', 'F', 'F', 'F', 'G', 'G', 'C', 'C', 'C', 'C',
                       'F', 'F', 'F', 'F', 'C', 'C', 'C', 'C', 'F', 'F', 'F', 'F', 'C', 'C', 'C', 'C',
                       'D', 'D', 'F', 'F', 'C', 'C', 'C', 'C', 'F', 'F', 'G', 'G', 'C', 'C', 'C', 'C'], 1, 3)
    harmony = []
    for root in ['C', 'G', 'F', 'C', 'F', 'D', 'G', 'G', 'C', 'G', 'A', 'F', 'F', 'G', 'C', 'C',
                 'F', 'F', 'C', 'C', 'F', 'F', 'C', 'C', 'D', 'F', 'C', 'C', 'F', 'G', 'C', 'C']:
        third = {'C': 'E', 'G': 'B', 'F': 'A', 'D': 'F#', 'A': 'C#'}[root]
        harmony += [(root + '4', 1), (third + '4', 1), (root + '4', 1), (third + '4', 1)]
    song('overworld', 138, 16000, [
        (lead, 0.5, dict(duty=0.25, vol=0.22, vibrato=1)),
        (harmony, 0.5, dict(duty=0.125, vol=0.07)),
        (bass, 1.0, dict(wave='triangle', vol=0.4)),
        (drums(16, 'k.s.k.s.k.s.kks.'), 0.25, dict(wave='noise', vol=0.12, decay=0.03)),
    ], 16)

    # The cellar: slow and mysterious, A minor (8 bars, arpeggios under a long melody)
    arps = []
    for chord in (['A3', 'C4', 'E4', 'C4'], ['F3', 'A3', 'C4', 'A3'], ['G3', 'B3', 'D4', 'B3'], ['E3', 'G#3', 'B3', 'G#3']) * 2:
        arps += [(n, 1) for n in chord * 4]
    melody = [('A4', 4), ('G4', 2), ('E4', 2), ('F4', 6), (None, 2), ('D4', 4), ('E4', 2), ('F4', 2), ('E4', 8),
              ('C5', 4), ('B4', 2), ('A4', 2), ('A4', 6), (None, 2), ('B4', 4), ('A4', 2), ('G#4', 2), ('A4', 8)]
    song('cellar', 96, 16000, [
        (arps, 0.25, dict(duty=0.5, vol=0.08)),
        (melody, 0.5, dict(wave='triangle', vol=0.35, vibrato=2)),
        (chord_bass(['A', 'F', 'G', 'E'] * 2, 1, 2), 4.0, dict(wave='triangle', vol=0.3)),
        (drums(8, 'k.......k...s...'), 0.25, dict(wave='noise', vol=0.08, decay=0.05)),
    ], 8)

    # Title: broad and hopeful, in C
    title = [('G4', 4), ('C5', 4), ('E5', 4), ('D5', 2), ('C5', 2), ('D5', 4), ('G4', 4), ('C5', 8),
             ('E5', 4), ('G5', 4), ('A5', 4), ('G5', 2), ('E5', 2), ('F5', 2), ('E5', 2), ('D5', 2), ('B4', 2), ('C5', 8)]
    song('title', 92, 16000, [
        (title, 0.5, dict(duty=0.25, vol=0.22, vibrato=2)),
        (chord_bass(['C', 'C', 'G', 'C', 'C', 'F', 'G', 'C'], 1, 3), 4.0, dict(wave='triangle', vol=0.35)),
        ([(n, 1) for c in (['C4', 'E4', 'G4'], ['C4', 'E4', 'G4'], ['B3', 'D4', 'G4'], ['C4', 'E4', 'G4'],
                          ['C4', 'E4', 'G4'], ['C4', 'F4', 'A4'], ['B3', 'D4', 'G4'], ['C4', 'E4', 'G4'])
          for n in (c * 3 + [c[1], c[2], c[1], c[0], c[1], c[2], c[1], c[0]])[:16]], 0.25, dict(duty=0.125, vol=0.05)),
    ], 8)

    # The boss: urgent, low and fast
    boss = []
    for root in ['E3', 'E3', 'F3', 'E3', 'E3', 'E3', 'G3', 'F#3'] * 2:
        boss += [(root, 1), (None, 1), (root, 1), (freq(root) * 2, 1)]
    song('boss', 152, 16000, [
        (boss, 0.5, dict(duty=0.5, vol=0.13)),
        ([('B4', 2), ('C5', 2), ('B4', 2), ('E5', 2), ('D5', 4), ('C5', 2), ('B4', 2)] * 2, 1.0, dict(duty=0.25, vol=0.12)),
        (chord_bass(['E', 'F', 'E', 'G'] * 2, 4, 2), 0.5, dict(wave='triangle', vol=0.35)),
        (drums(8, 'k.k.s.k.k.k.s.ss'), 0.25, dict(wave='noise', vol=0.12, decay=0.03)),
    ], 8)


if __name__ == '__main__':
    os.makedirs(os.path.join(OUT, 'sfx'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'music'), exist_ok=True)
    make_sfx()
    make_music()
    print('make_sound: wrote sfx/*.wav and music/*.wav')
