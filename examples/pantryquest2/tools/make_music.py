#!/usr/bin/env python3
"""Composes and synthesises Pantry Quest II's music (all original), in the manner of the old
castle-crawling games: minor keys, a pulse lead with vibrato and an echo a sixteenth behind
it, a triangle bass running in octaves, and noise drums. Loops go to music/ (16000 Hz).

    python3 examples/pantryquest2/tools/make_music.py [track ...]

Each track is written below as notes: "D5/4" is D in octave 5 for 4 sixteenths, "r/2" a
rest, "-/4" holds the note before for 4 more; '|' marks bars (for reading only).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from synth import Track, tone, drum, freq, transpose   # noqa: E402

OUT = os.path.join(HERE, '..', 'music')
RATE = 16000


def parse(text):
    """'D5/4 F5/2 -/2 r/4' -> [(note or None, sixteenths)]"""
    out = []
    for tok in text.replace('|', ' ').split():
        name, _, n = tok.partition('/')
        n = float(n) if n else 1
        if name == '-':
            note, length = out[-1]
            out[-1] = (note, length + n)
        elif name == 'r':
            out.append((None, n))
        else:
            out.append((name, n))
    return out


def check_bars(text, what, bar=16):
    """Every '|'-marked bar must hold a whole bar (catches a mistyped length)."""
    for i, b in enumerate(text.split('|')):
        if not b.strip():
            continue
        n = sum(float(t.partition('/')[2] or 1) for t in b.split())
        if abs(n - bar) > 1e-6:
            raise SystemExit('%s: bar %d holds %g sixteenths, not %d' % (what, i + 1, n, bar))


def length16(notes):
    return sum(n for _, n in notes)


def voice(tr, notes, step, start=0.0, wave='square', duty=0.5, vol=0.2, vib=1.0, decay=None, sustain=0.6,
          legato=0.92, octave=0, wrap=True):
    t = start
    for note, n in notes:
        if note is not None:
            if octave:
                note = transpose(note, 12 * octave)
            secs = n * step * legato
            long_note = n >= 4
            samples = tone(RATE, note, secs, wave, duty=duty, vol=vol, vibrato=vib if long_note else 0.0,
                           vib_delay=min(0.15, secs * 0.3), decay=decay, sustain=sustain, release=0.02)
            if wrap:
                tr.add_wrapped(t, samples)
            else:
                tr.add(t, samples)
        t += n * step


def bass_line(chords, style, bars16=16):
    """A triangle bass from chord roots (one per bar, or 'D3 A2' for two halves)."""
    notes = []
    for ch in chords:
        roots = ch.split()
        per = bars16 // len(roots)
        for root in roots:
            if style == 'octaves':                     # root, octave up, eighths
                for k in range(per // 2):
                    notes.append((root if k % 2 == 0 else transpose(root, 12), 2))
            elif style == 'gallop':                    # da-da-dum
                for k in range(per // 4):
                    notes += [(root, 2), (root, 1), (transpose(root, 12), 1)]
            elif style == 'pulse':                     # driving eighths on the root
                for k in range(per // 2):
                    notes.append((root, 2))
            elif style == 'walk':                      # root, fifth, octave, fifth
                pat = [0, 7, 12, 7]
                for k in range(per // 4):
                    for p in pat:
                        notes.append((transpose(root, p), 1))
            elif style == 'long':
                notes.append((root, per))
            elif style == 'arp5':                      # open fifths up and down (fits any chord)
                pat = [0, 7, 12, 19, 24, 19, 12, 7]
                for k in range(per):
                    notes.append((transpose(root, pat[k % 8]), 1))
            elif style == 'arp':                       # minor-ish arpeggio up and down in sixteenths
                pat = [0, 7, 12, 15, 12, 7, 0, 7]
                for k in range(per):
                    notes.append((transpose(root, pat[k % 8]), 1))
    return notes


def drums(tr, pattern_bars, step, vol=0.25):
    """Each bar a string of 16: K kick, S snare, h hat, C crash, '.' nothing."""
    t = 0.0
    for bar in pattern_bars:
        for ch in bar:
            kind = {'K': 'kick', 'S': 'snare', 'h': 'hat', 'C': 'crash'}.get(ch)
            if kind:
                tr.add_wrapped(t, drum(RATE, kind, vol))
            t += step


def render(name, bpm, lead, chords, bass_style='octaves', drum_bars=None, harmony=None, counter=None,
           lead_duty=0.25, echo=True, swing=0.0, lead_vol=0.17, bass_vol=0.32, extra=None, intro=None):
    step = 60.0 / bpm / 4
    for what, text in (('lead', lead), ('harmony', harmony), ('counter', counter)):
        if text:
            check_bars(text, name + ' ' + what)
    L = parse(lead)
    total16 = length16(L)
    tr = Track(total16 * step, RATE)
    voice(tr, L, step, duty=lead_duty, vol=lead_vol, vib=1.2, decay=0.6, sustain=0.7)
    if echo:                                           # the echo: the lead again, a sixteenth late, thinner
        voice(tr, L, step, start=step * 1.0, duty=0.125, vol=lead_vol * 0.35, vib=1.0, decay=0.4, sustain=0.5)
    if harmony:
        H = parse(harmony)
        voice(tr, H, step, duty=0.5, vol=lead_vol * 0.55, vib=0.8, decay=0.5, sustain=0.6)
    if counter:
        C = parse(counter)
        voice(tr, C, step, duty=0.125, vol=lead_vol * 0.6, vib=0.5, decay=0.3, sustain=0.5)
    B = bass_line(chords, bass_style) if isinstance(chords, list) else parse(chords)
    voice(tr, B, step, wave='triangle', vol=bass_vol, vib=0.0, legato=0.85)
    if drum_bars:
        bars = drum_bars * (int(total16 // 16) // len(drum_bars) + 1)
        drums(tr, bars[:int(total16 // 16)], step)
    if extra:
        extra(tr, step)
    tr.write(os.path.join(OUT, name + '.wav'))
    print('  %s: %.1f s' % (name, total16 * step))


DR_ROCK = ['K.h.S.h.K.K.S.h.', 'K.h.S.h.K.hKS.hS']
DR_MARCH = ['K...S...K.K.S...', 'K...S...K.K.S.SS']
DR_DRIVE = ['K.hhS.hhK.hhS.hh', 'K.hhS.hhKKhhS.SS']
DR_SPARSE = ['K.......S.......', 'K.......S...h.h.']
DR_GALLOP = ['K.hKS.hKK.hKS.hS']
DR_BOSS = ['KhKhShKhKhKhShSh', 'KhKhShKhKKKhSSSS']


# =============================================================== the tracks

def village():
    """Crumb Village at midnight: D minor, the moon up, the castle on the hill."""
    lead = """
    D5/4 F5/2 A5/2 G5/4 F5/2 E5/2 | F5/4 D5/2 E5/2 C#5/8 | D5/4 F5/2 A5/2 A#5/4 A5/2 G5/2 | A5/12 r/4 |
    A#5/4 A5/2 G5/2 F5/4 G5/2 A5/2 | G5/4 F5/2 E5/2 F5/8 | E5/4 F5/2 G5/2 A5/4 C#5/4 | D5/12 r/4 |
    A5/4 D6/2 C6/2 A#5/4 A5/4 | G5/4 A#5/2 A5/2 G5/8 | F5/4 A5/2 G5/2 F5/4 E5/4 | F5/4 G5/4 A5/8 |
    A#5/2 A5/2 G5/2 F5/2 E5/2 F5/2 G5/2 A5/2 | A#5/4 C#6/4 D6/8 | C#6/2 A#5/2 A5/2 G5/2 F5/2 E5/2 D5/2 C#5/2 | D5/16
    """
    harmony = """
    A4/8 C5/8 | A4/8 A4/8 | A4/8 D5/8 | C#5/12 r/4 |
    D5/8 C5/8 | A#4/8 A4/8 | C#5/8 E5/8 | F5/12 r/4 |
    F5/8 F5/8 | D5/8 D5/8 | C5/8 C5/8 | C5/16 |
    D5/8 C#5/8 | E5/8 F5/8 | E5/16 | F5/16
    """
    chords = ['D3', 'A2', 'D3', 'A2', 'A#2', 'G2 A2', 'A2', 'D3',
              'D3', 'G2', 'F2', 'F2', 'A#2', 'A2', 'A2', 'D3']
    render('village', 132, lead, chords, 'octaves', DR_SPARSE + DR_ROCK, harmony=harmony)


def catacombs():
    """The Rye Catacombs: A minor, a galloping bass, the whip-cracking march down the bone halls."""
    lead = """
    A4/2 A4/1 A4/1 C5/2 A4/2 E5/4 D5/2 C5/2 | B4/2 G4/2 B4/2 D5/2 C5/4 B4/4 |
    A4/2 A4/1 A4/1 C5/2 A4/2 E5/4 F5/2 E5/2 | D5/4 E5/2 C5/2 B4/8 |
    A4/2 A4/1 A4/1 C5/2 A4/2 E5/4 D5/2 C5/2 | B4/2 G4/2 B4/2 D5/2 G5/4 F5/4 |
    E5/2 F5/2 E5/2 D5/2 C5/2 D5/2 B4/2 G#4/2 | A4/12 r/4 |
    F5/4 E5/2 D5/2 E5/4 C5/4 | D5/4 C5/2 B4/2 C5/4 A4/4 |
    B4/2 C5/2 D5/2 E5/2 F5/2 G5/2 A5/2 G5/2 | E5/12 D5/2 B4/2 |
    A5/4 G5/2 F5/2 G5/4 E5/4 | F5/4 E5/2 D5/2 E5/4 C5/4 |
    D5/2 C5/2 B4/2 C5/2 D5/2 E5/2 G#4/2 B4/2 | A4/16
    """
    harmony = """
    E4/8 G4/8 | D4/8 G4/8 | E4/8 A4/8 | G#4/16 |
    E4/8 G4/8 | D4/8 B4/8 | C5/8 G#4/8 | E4/16 |
    A4/8 A4/8 | F4/8 E4/8 | G4/8 B4/8 | G#4/16 |
    C5/8 C5/8 | A4/8 A4/8 | G#4/8 E4/8 | C5/16
    """
    chords = ['A2', 'G2', 'A2', 'E2', 'A2', 'G2', 'A2 E2', 'A2',
              'F2', 'D2', 'G2', 'E2', 'F2', 'D2', 'E2', 'A2']
    render('catacombs', 150, lead, chords, 'gallop', DR_GALLOP + DR_ROCK, harmony=harmony)


def frost():
    """The Frostbite Cellar: E minor, slow and glassy, open fifths rippling under a cold lead."""
    lead = """
    E5/6 F#5/2 G5/4 B5/4 | A5/6 G5/2 F#5/8 | D5/6 E5/2 F#5/4 A5/4 | G5/6 F#5/2 E5/8 |
    C6/6 B5/2 A5/4 G5/4 | F#5/6 G5/2 A5/4 B5/4 | G5/4 F#5/4 E5/4 D#5/4 | E5/16 |
    B5/4 E6/4 D6/4 B5/4 | C6/6 B5/2 A5/8 | A5/4 D6/4 C6/4 A5/4 | B5/6 A5/2 G5/8 |
    G5/4 C6/4 B5/4 G5/4 | F#5/4 A5/4 G5/4 F#5/4 | E5/4 G5/4 F#5/4 D#5/4 | E5/16
    """
    chords = ['E2', 'D2', 'B2', 'E2', 'C3', 'D3', 'A2 B2', 'E2',
              'E2', 'A2', 'D2', 'G2', 'C3', 'D3', 'A2 B2', 'E2']
    render('frost', 112, lead, chords, 'arp5', DR_SPARSE, lead_duty=0.5, lead_vol=0.15, bass_vol=0.26)


def molten():
    """The Molten Kitchens: G minor at a boil, driving eighths and runs like spitting fat."""
    lead = """
    G4/2 A#4/2 D5/2 G5/2 F5/2 D5/2 D#5/2 D5/2 | C5/2 D5/2 D#5/4 D5/4 A#4/4 |
    G4/2 A#4/2 D5/2 G5/2 A5/2 A#5/2 A5/2 G5/2 | F#5/8 D5/8 |
    D#5/4 D5/2 C5/2 D#5/4 G5/4 | F5/4 D#5/2 D5/2 F5/4 A#5/4 |
    A5/2 G5/2 F#5/2 G5/2 A5/2 F#5/2 D5/2 C5/2 | D5/12 r/4 |
    G5/2 G5/1 G5/1 A#5/2 G5/2 F5/2 D#5/2 D5/2 C5/2 | D#5/2 D#5/1 D#5/1 G5/2 D#5/2 D5/2 C5/2 A#4/2 A4/2 |
    C5/2 C5/1 C5/1 D#5/2 C5/2 A#4/2 A4/2 G4/2 A4/2 | A#4/4 C5/4 D5/8 |
    G5/2 A5/2 A#5/4 A5/2 G5/2 F#5/4 | D#5/2 F5/2 G5/4 F5/2 D#5/2 D5/4 |
    C5/2 D5/2 D#5/2 F5/2 F#5/2 G5/2 A5/2 F#5/2 | G5/16
    """
    chords = ['G2', 'C3', 'G2', 'D3', 'C3', 'A#2', 'D3', 'D3',
              'G2', 'D#2', 'C3', 'D3', 'D#2', 'C3', 'D3', 'G2']
    render('molten', 160, lead, chords, 'pulse', DR_DRIVE)


def brine():
    """The Brine Works: F minor, a lurching walk of pipes and pistons, off-beat stabs."""
    lead = """
    F4/3 G#4/3 C5/2 C#5/4 C5/4 | A#4/3 C5/3 A#4/2 G#4/4 G4/4 |
    F4/3 G#4/3 C5/2 D#5/4 C#5/2 C5/2 | C5/12 r/4 |
    C#5/3 C5/3 A#4/2 C#5/4 F5/4 | D#5/3 C#5/3 C5/2 D#5/4 G#5/4 |
    G5/3 F5/3 E5/2 G5/4 A#5/4 | C6/12 r/4 |
    F5/2 r/1 F5/1 G#5/2 F5/2 D#5/2 C#5/2 C5/4 | C#5/2 r/1 C#5/1 F5/2 C#5/2 C5/2 A#4/2 G#4/4 |
    A#4/2 r/1 A#4/1 C#5/2 A#4/2 G#4/2 G4/2 F4/4 | G4/4 G#4/4 A#4/4 C5/4 |
    F5/2 r/1 F5/1 G#5/2 F5/2 D#5/2 C#5/2 C5/4 | C#5/2 r/1 C#5/1 F5/2 C#5/2 C5/2 A#4/2 G#4/4 |
    G4/2 A#4/2 C5/2 E5/2 G5/2 E5/2 C5/2 E5/2 | F5/16
    """
    chords = ['F2', 'D#2', 'F2', 'C3', 'C#3', 'G#2', 'C3', 'C3',
              'F2', 'C#3', 'A#2', 'C3', 'F2', 'C#3', 'C3', 'F2']
    render('brine', 138, lead, chords, 'walk', DR_MARCH + DR_ROCK, lead_duty=0.125)


def castle():
    """Castle Fromage: D minor at full tilt, the lead and its echo racing up the halls."""
    lead = """
    D5/2 A4/2 D5/2 E5/2 F5/4 E5/2 D5/2 | C5/2 A4/2 C5/2 D5/2 E5/4 D5/2 C5/2 |
    A#4/2 F4/2 A#4/2 C5/2 D5/4 C5/2 A#4/2 | A4/2 C#5/2 E5/2 G5/2 A5/8 |
    D5/2 A4/2 D5/2 E5/2 F5/4 G5/2 A5/2 | A#5/4 A5/2 G5/2 A5/4 F5/4 |
    G5/2 F5/2 E5/2 D5/2 E5/4 C#5/4 | D5/12 r/4 |
    F5/4 F5/2 G5/2 A5/4 A5/2 A#5/2 | C6/4 A#5/2 A5/2 G5/8 |
    E5/4 E5/2 F5/2 G5/4 G5/2 A5/2 | A#5/4 A5/2 G5/2 F5/8 |
    D6/4 C#6/2 D6/2 A5/4 F5/4 | G5/4 F5/2 G5/2 E5/4 C#5/4 |
    D5/2 E5/2 F5/2 G5/2 A5/2 A#5/2 C#6/2 E6/2 | D6/12 r/4
    """
    harmony = """
    A4/8 A4/8 | G4/8 G4/8 | F4/8 F4/8 | E4/8 C#5/8 |
    A4/8 D5/8 | D5/8 C5/8 | A#4/8 A4/8 | A4/16 |
    C5/8 F5/8 | E5/8 E5/8 | C5/8 E5/8 | D5/8 D5/8 |
    A5/8 D5/8 | D5/8 C#5/8 | A4/16 | A5/16
    """
    chords = ['D3', 'C3', 'A#2', 'A2', 'D3', 'A#2', 'A2', 'D3',
              'F2', 'C3', 'C3', 'A#2', 'D3', 'A#2 A2', 'A2', 'D3']
    render('castle', 156, lead, chords, 'octaves', DR_ROCK + DR_DRIVE, harmony=harmony)


def boss():
    """A guardian: E minor, a chromatic grinding ostinato and a hammering beat."""
    lead = """
    E5/2 E5/2 F5/2 E5/2 D#5/2 E5/2 G5/2 F#5/2 | E5/2 E5/2 F5/2 E5/2 D#5/2 E5/2 B4/4 |
    C5/2 C5/2 C#5/2 C5/2 B4/2 C5/2 E5/2 D#5/2 | B4/8 D#5/8 |
    E5/2 E5/2 F5/2 E5/2 D#5/2 E5/2 G5/2 F#5/2 | G5/2 G5/2 G#5/2 G5/2 F#5/2 G5/2 B5/4 |
    A#5/2 A5/2 G#5/2 G5/2 F#5/2 F5/2 E5/2 D#5/2 | E5/8 B4/8
    """
    chords = ['E2', 'E2', 'C3', 'B2', 'E2', 'G2', 'A#2 B2', 'E2']
    render('boss', 168, lead, chords, 'pulse', DR_BOSS, lead_duty=0.125, lead_vol=0.16)


def count():
    """Count Fromage himself: B minor, a grand, gloomy waltz-march for the throne room."""
    lead = """
    B4/4 F#5/4 D5/2 C#5/2 B4/4 | A#4/4 F#5/4 C#5/2 B4/2 A#4/4 | G5/4 F#5/2 E5/2 D5/4 C#5/4 | F#5/8 A#4/8 |
    B4/4 F#5/4 D5/2 E5/2 F#5/4 | G5/4 A5/4 B5/4 A5/2 G5/2 |
    F#5/2 E5/2 D5/2 C#5/2 D5/2 C#5/2 B4/2 A#4/2 | B4/12 r/4 |
    D6/2 C#6/2 B5/2 A#5/2 B5/4 F#5/4 | G5/2 F#5/2 E5/2 D5/2 E5/4 C#5/4 |
    D5/2 C#5/2 B4/2 A#4/2 B4/4 G5/4 | F#5/16 |
    B5/4 A5/2 G5/2 F#5/4 E5/4 | D5/4 E5/2 F#5/2 G5/4 E5/4 |
    C#5/4 D5/2 E5/2 F#5/2 A#4/2 C#5/2 E5/2 | B4/16
    """
    harmony = """
    F#4/8 D4/8 | F#4/8 C#4/8 | B4/8 G4/8 | C#5/16 |
    F#4/8 B4/8 | E5/8 E5/8 | D5/8 C#5/8 | D5/16 |
    F#5/8 D5/8 | B4/8 A#4/8 | F#4/8 D5/8 | C#5/16 |
    D5/8 B4/8 | B4/8 B4/8 | A#4/16 | D4/16
    """
    chords = ['B2', 'F#2', 'E2', 'F#2', 'B2', 'E2', 'F#2', 'B2',
              'B2', 'E2', 'B2 G2', 'F#2', 'G2', 'E2', 'F#2', 'B2']
    render('count', 140, lead, chords, 'gallop', DR_MARCH, harmony=harmony, lead_duty=0.5)


def escape():
    """The escape: A minor at a sprint, the castle coming down behind."""
    lead = """
    A5/2 E5/2 A5/2 B5/2 C6/2 B5/2 A5/2 E5/2 | F5/2 C5/2 F5/2 G5/2 A5/2 G5/2 F5/2 C5/2 |
    G5/2 D5/2 G5/2 A5/2 B5/2 A5/2 G5/2 D5/2 | E5/2 B4/2 E5/2 F#5/2 G#5/2 B5/2 E6/4 |
    A5/2 E5/2 A5/2 B5/2 C6/2 B5/2 A5/2 E5/2 | F5/2 C5/2 F5/2 G5/2 A5/2 G5/2 F5/2 C5/2 |
    G5/2 A5/2 B5/2 C6/2 D6/2 C6/2 B5/2 G#5/2 | A5/12 r/4
    """
    chords = ['A2', 'F2', 'G2', 'E2', 'A2', 'F2', 'G2 E2', 'A2']
    render('escape', 184, lead, chords, 'pulse', DR_DRIVE)


def title():
    """The title: D minor, slow and grand, the castle rising out of the night."""
    lead = """
    D5/8 A4/8 | A#4/6 C5/2 D5/8 | F5/6 E5/2 D5/4 C5/4 | A4/16 |
    D5/8 F5/8 | A#5/6 A5/2 G5/8 | F5/4 G5/4 E5/4 C#5/4 | D5/16
    """
    harmony = """
    A4/8 F4/8 | F4/8 A#4/8 | A4/8 G4/8 | E4/16 |
    A4/8 D5/8 | D5/8 A#4/8 | A4/8 A4/8 | F4/16
    """
    chords = ['D3', 'A#2', 'F2 C3', 'A2', 'D3', 'G2', 'D3 A2', 'D3']
    render('title', 108, lead, chords, 'arp5', DR_SPARSE, harmony=harmony, lead_duty=0.5, bass_vol=0.24)


def ending():
    """The ending: D major at last, the sun coming up over Crumb Village."""
    lead = """
    F#5/4 A5/4 D6/6 C#6/2 | B5/4 A5/4 F#5/8 | G5/4 B5/4 E6/6 D6/2 | C#6/12 r/4 |
    D6/4 C#6/2 B5/2 A5/4 F#5/4 | G5/4 F#5/2 E5/2 D5/8 | E5/4 F#5/2 G5/2 A5/4 C#6/4 | D6/12 r/4
    """
    harmony = """
    D5/8 F#5/8 | F#5/8 D5/8 | D5/8 G5/8 | E5/12 r/4 |
    F#5/8 D5/8 | B4/8 B4/8 | C#5/8 E5/8 | F#5/12 r/4
    """
    chords = ['D3', 'B2', 'G2', 'A2', 'D3 F#2', 'G2', 'E2 A2', 'D3']
    render('ending', 116, lead, chords, 'walk', DR_SPARSE, harmony=harmony, lead_duty=0.5)


TRACKS = {'village': village, 'catacombs': catacombs, 'frost': frost, 'molten': molten, 'brine': brine,
          'castle': castle, 'boss': boss, 'count': count, 'escape': escape, 'title': title, 'ending': ending}


def main():
    os.makedirs(OUT, exist_ok=True)
    want = sys.argv[1:] or list(TRACKS)
    for t in want:
        TRACKS[t]()


if __name__ == '__main__':
    main()
