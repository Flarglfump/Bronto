#!/usr/bin/env python3
"""Synthesises Pantry Quest II's sound effects (all original) into sfx/, at 22050 Hz.

    python3 examples/pantryquest2/tools/make_sound.py

The music is made by make_music.py.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from synth import Track, tone, drum    # noqa: E402

OUT = os.path.join(HERE, '..', 'sfx')
R = 22050


def sfx(name, seconds, parts):
    tr = Track(seconds, R)
    for start, samples in parts:
        tr.add(start, samples)
    tr.write(os.path.join(OUT, name + '.wav'))


def seq(notes, step, start=0.0, **kw):
    parts, t = [], start
    for note, length in notes:
        if note is not None:
            parts.append((t, tone(R, note, length * step * 0.95, **kw)))
        t += length * step
    return parts


def main():
    os.makedirs(OUT, exist_ok=True)
    sfx('pepper', 0.09, [(0, tone(R, 1200, 0.08, duty=0.25, vol=0.25, f1=500, release=0.02)),
                         (0, tone(R, 3000, 0.04, 'noise', vol=0.12, decay=0.01))])
    sfx('frost', 0.14, [(0, tone(R, 2200, 0.13, duty=0.125, vol=0.18, f1=3400, vibrato=3, vib_delay=0)),
                        (0, tone(R, 6000, 0.1, 'noise', vol=0.08, decay=0.04))])
    sfx('garlic', 0.16, [(0, tone(R, 600, 0.15, 'triangle', vol=0.4, f1=900, vibrato=4, vib_delay=0)),
                         (0, tone(R, 1200, 0.15, duty=0.5, vol=0.1, f1=1800))])
    sfx('rocket', 0.3, [(0, tone(R, 300, 0.28, 'noise', vol=0.3, f1=1200, decay=0.15)),
                        (0, tone(R, 180, 0.2, duty=0.25, vol=0.18, f1=420))])
    sfx('blast', 0.45, [(0, tone(R, 900, 0.44, 'noise', vol=0.45, f1=100, decay=0.12)),
                        (0, tone(R, 110, 0.3, 'triangle', vol=0.5, f1=40, decay=0.1))])
    sfx('pop', 0.3, [(0, tone(R, 1800, 0.06, 'noise', vol=0.4, decay=0.02)),
                     (0.02, tone(R, 400, 0.2, duty=0.5, vol=0.25, f1=1400, decay=0.05)),
                     (0, tone(R, 90, 0.25, 'triangle', vol=0.4, f1=40, decay=0.08))])
    sfx('drop', 0.08, [(0, tone(R, 700, 0.07, duty=0.25, vol=0.2, f1=300))])
    sfx('click', 0.06, [(0, tone(R, 300, 0.05, duty=0.5, vol=0.2))])
    sfx('hurt', 0.32, [(0, tone(R, 'A4', 0.3, duty=0.5, vol=0.3, f1='C3')), (0, tone(R, 900, 0.2, 'noise', vol=0.15, decay=0.08))])
    sfx('jump', 0.14, [(0, tone(R, 280, 0.13, duty=0.25, vol=0.2, f1=700))])
    sfx('roll', 0.12, [(0, tone(R, 600, 0.11, duty=0.25, vol=0.18, f1=200))])
    sfx('unroll', 0.12, [(0, tone(R, 200, 0.11, duty=0.25, vol=0.18, f1=600))])
    sfx('sizzle', 0.3, [(0, tone(R, 5000, 0.28, 'noise', vol=0.3, decay=0.1))])
    sfx('crumble', 0.2, [(0, tone(R, 800, 0.18, 'noise', vol=0.3, f1=200, decay=0.06))])
    sfx('hit', 0.1, [(0, tone(R, 'C3', 0.09, duty=0.25, vol=0.3, f1='G2')), (0, tone(R, 3000, 0.05, 'noise', vol=0.15, decay=0.02))])
    sfx('clank', 0.12, [(0, tone(R, 1800, 0.11, duty=0.125, vol=0.18, decay=0.03)), (0, tone(R, 2400, 0.1, duty=0.125, vol=0.1, decay=0.03))])
    sfx('door', 0.3, [(0, tone(R, 140, 0.28, duty=0.5, vol=0.2, f1=320)), (0, tone(R, 2000, 0.25, 'noise', vol=0.08, decay=0.1))])
    sfx('enemy_die', 0.32, seq([('G5', 1), ('D5', 1), ('A4', 1), ('E4', 2)], 0.05, duty=0.25, vol=0.22) +
        [(0, tone(R, 1500, 0.25, 'noise', vol=0.15, decay=0.08))])
    sfx('freeze', 0.4, seq([('E6', 1), ('B6', 1), ('E7', 2)], 0.06, duty=0.125, vol=0.15) +
        [(0, tone(R, 8000, 0.35, 'noise', vol=0.08, decay=0.15))])
    sfx('screech', 0.3, [(0, tone(R, 1500, 0.28, duty=0.125, vol=0.15, f1=900, vibrato=8, vib_delay=0))])
    sfx('pickup', 0.16, seq([('C6', 1), ('G6', 2)], 0.045, duty=0.25, vol=0.2))
    sfx('die', 1.4, seq([('B4', 1), ('A#4', 1), ('A4', 1), ('G#4', 1), ('G4', 2), ('F#4', 2), ('F4', 4)], 0.09, duty=0.5, vol=0.25, vibrato=2) +
        [(0, tone(R, 400, 1.2, 'noise', vol=0.15, f1=60, decay=0.5))])
    sfx('save', 1.2, seq([('C5', 2), ('E5', 2), ('G5', 2), ('C6', 4), ('G5', 2), ('C6', 6)], 0.06, duty=0.25, vol=0.2) +
        seq([('C4', 8), ('G3', 10)], 0.06, wave='triangle', vol=0.4))
    sfx('map', 0.8, seq([('G5', 1), ('C6', 1), ('E6', 1), ('G6', 1), ('C7', 4)], 0.07, duty=0.125, vol=0.18))
    sfx('item', 2.4, seq([('D5', 2), ('F5', 2), ('A5', 2), ('D6', 6), ('C6', 2), ('A#5', 2), ('A5', 2), ('D6', 10)], 0.08, duty=0.5, vol=0.22, vibrato=2) +
        seq([('D4', 6), ('A#3', 6), ('A3', 6), ('D3', 10)], 0.08, wave='triangle', vol=0.45) +
        [(0, drum(R, 'crash', 0.3))])
    sfx('small_item', 1.0, seq([('A5', 1), ('C#6', 1), ('E6', 1), ('A6', 4)], 0.07, duty=0.25, vol=0.2) +
        seq([('A3', 7)], 0.07, wave='triangle', vol=0.4))
    sfx('roar', 1.2, [(0, tone(R, 140, 1.15, duty=0.5, vol=0.3, f1=70, vibrato=10, vib_delay=0, decay=0.6)),
                      (0, tone(R, 600, 1.1, 'noise', vol=0.25, f1=150, decay=0.5))])
    sfx('boss_hit', 0.15, [(0, tone(R, 'A2', 0.14, duty=0.25, vol=0.35, f1='E2')), (0, tone(R, 4000, 0.1, 'noise', vol=0.2, decay=0.03))])
    sfx('boss_die', 2.5, [(i * 0.18, tone(R, 900, 0.3, 'noise', vol=0.4, f1=80, decay=0.1)) for i in range(12)] +
        [(0, tone(R, 300, 2.4, 'triangle', vol=0.4, f1=40, decay=1.0))])
    sfx('blip', 0.04, [(0, tone(R, 1400, 0.035, duty=0.25, vol=0.15))])
    sfx('text', 0.03, [(0, tone(R, 900, 0.025, duty=0.25, vol=0.08))])
    sfx('select', 0.12, seq([('E6', 1), ('A6', 2)], 0.035, duty=0.25, vol=0.18))
    sfx('back', 0.12, seq([('A5', 1), ('E5', 2)], 0.035, duty=0.25, vol=0.18))
    sfx('quake', 1.5, [(0, tone(R, 60, 1.45, 'noise', vol=0.4, decay=0.8)), (0, tone(R, 50, 1.4, 'triangle', vol=0.4, f1=30, decay=0.7))])
    sfx('alarm', 0.6, seq([('A5', 2), ('E5', 2), ('A5', 2), ('E5', 2)], 0.07, duty=0.5, vol=0.2))
    sfx('heartbeat', 0.5, [(0, tone(R, 70, 0.1, 'triangle', vol=0.4, decay=0.04)), (0.15, tone(R, 60, 0.1, 'triangle', vol=0.3, decay=0.04))])
    print('wrote the sound effects to sfx/')


if __name__ == '__main__':
    main()
