"""The title, story and ending pictures: Crumb Village under the moon with Castle Fromage on
its hill (and the same view at dawn, the castle gone), and the PANTRY QUEST II logo.
"""
import math
from pixels import Canvas, PALETTE, rgba, noise, mix

P = PALETTE

# A 5 x 7 font for the logo's letters.
GLYPHS = {
    'P': ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    'A': [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    'N': ["#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"],
    'T': ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    'R': ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    'Y': ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    'Q': [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    'U': ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    'I': ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"],
    ' ': ["...", "...", "...", "...", "...", "...", "..."],
}


def sky(c, top, bottom, h, bands=8, seed=1):
    """A banded sky with dithered steps between the bands, as old consoles drew them."""
    for y in range(h):
        t = y / max(h - 1, 1)
        band = t * (bands - 1)
        lo = int(band)
        frac = band - lo
        for x in range(c.w):
            k = lo + (1 if (frac > 0.5 and (x + y) % 2 == 0) or frac > 0.85 else 0)
            c.set(x, y, rgba(mix(top, bottom, min(k, bands - 1) / (bands - 1))))


def hills(c, base, amp, freq, phase, colour, top_colour=None):
    for x in range(c.w):
        y0 = int(base - amp * (math.sin(x * freq + phase) * 0.6 + math.sin(x * freq * 2.3 + phase * 1.7) * 0.4))
        for y in range(y0, c.h):
            c.set(x, y, rgba(colour))
        if top_colour:
            c.set(x, y0, rgba(top_colour))


def castle(c, cx, base, dark, lit, rng_seed=3):
    """Castle Fromage: a keep, towers with pointed roofs, and lit windows."""
    def tower(x, w, h, roof=True):
        c.rect(x, base - h, w, h, rgba(dark))
        for k in range(0, w, 3):                     # battlements
            c.rect(x + k, base - h - 2, 2, 2, rgba(dark))
        if roof:
            for k in range(w // 2 + 3):
                c.rect(x - 1 + k, base - h - k - 2, w + 2 - 2 * k, 1, rgba(dark))
        for wy in range(base - h + 6, base - 6, 9):  # windows
            for wx in range(x + 2, x + w - 2, 4):
                if noise(wx, wy, rng_seed) > 0.45:
                    c.rect(wx, wy, 1, 2, rgba(lit))
    tower(cx - 30, 60, 34, roof=False)               # the curtain wall and keep
    tower(cx - 12, 24, 58, roof=False)
    tower(cx - 38, 10, 52)
    tower(cx + 28, 10, 50)
    tower(cx - 4, 8, 78)                             # the Count's tower
    tower(cx + 12, 8, 64)
    c.rect(cx - 4, base - 12, 8, 12, rgba(P['k']))   # the gate
    c.ellipse(cx, base - 12, 4, 3, rgba(P['k']))


def village(c, base, dark, lit):
    x = 0
    k = 0
    while x < c.w:
        w = 18 + int(noise(k, 1, 9) * 16)
        h = 14 + int(noise(k, 2, 9) * 14)
        c.rect(x, base - h, w - 2, h + 40, rgba(dark))
        for r in range(w // 2):                      # a pointed roof
            c.rect(x - 1 + r, base - h - r, w - 2 * r, 1, rgba(dark))
        if noise(k, 3, 9) > 0.3:
            c.rect(x + w // 2 - 3, base - h + 5, 3, 4, rgba(lit))
        if noise(k, 4, 9) > 0.6:                     # a chimney
            c.rect(x + w - 6, base - h - w // 2 + 1, 3, 6, rgba(dark))
        x += w
        k += 1


def stars(c, h, seed, colour):
    for y in range(h):
        for x in range(c.w):
            n = noise(x, y, seed)
            if n > 0.993:
                c.set(x, y, rgba(colour))
            elif n > 0.987:
                c.set(x, y, rgba(mix(colour, (40, 40, 80), 0.5)))


def night(with_castle=True):
    c = Canvas(256, 240)
    sky(c, (10, 8, 30), (70, 30, 90), 170, bands=7)
    stars(c, 120, 5, P['w'])
    mx, my = 218, 96
    c.ellipse(mx, my, 14, 14, rgba(P['e']))          # the moon - a wheel of cheese, holes and all
    c.ellipse(mx - 4, my - 3, 3, 3, rgba(P['E']))
    c.ellipse(mx + 5, my + 5, 3, 2, rgba(P['E']))
    c.ellipse(mx + 4, my - 6, 2, 2, rgba(P['E']))
    for a in range(0, 360, 3):                       # a faint halo
        x, y = mx + math.cos(math.radians(a)) * 19, my + math.sin(math.radians(a)) * 19
        if int(a / 3) % 2 == 0:
            c.set(int(x), int(y), rgba((120, 90, 130)))
    hills(c, 150, 10, 0.03, 1.0, (40, 22, 60))
    hills(c, 162, 26, 0.012, 2.4, (28, 16, 44))      # the castle's hill
    if with_castle:
        castle(c, 150, 142, (8, 4, 16), P['y'])
    for y in range(40, 143):                         # moonlight on the castle's right-hand edges
        for x in range(100, 200):
            if c.get(x, y)[:3] == (8, 4, 16) and c.get(x + 1, y)[:3] != (8, 4, 16) and c.get(x + 1, y)[:3] != tuple(P['y']):
                c.set(x, y, rgba((70, 50, 96)))
    hills(c, 186, 6, 0.05, 0.3, (20, 12, 30))
    village(c, 206, (12, 8, 18), P['o'])
    c.rect(0, 222, 256, 18, rgba(P['k']))
    return c


def dawn():
    """The same hills at sunrise; where the castle was, only smoke."""
    c = Canvas(256, 240)
    sky(c, (60, 80, 170), (252, 180, 110), 170, bands=7)
    c.ellipse(70, 150, 26, 26, rgba(P['y']))         # the sun coming up
    c.ellipse(70, 150, 20, 20, rgba(P['H']))
    hills(c, 150, 10, 0.03, 1.0, (150, 110, 140))
    hills(c, 162, 26, 0.012, 2.4, (110, 80, 120))
    for i in range(40):                              # smoke where the castle stood
        x = 150 + int((noise(i, 1, 7) - 0.5) * 24) + i // 3
        y = 140 - i * 2
        r = 4 + i // 6
        c.ellipse(x, y, r, r * 0.8, rgba(mix((150, 130, 150), (230, 200, 200), i / 40)))
    hills(c, 186, 6, 0.05, 0.3, (80, 60, 90))
    village(c, 206, (54, 36, 54), P['y'])
    c.rect(0, 222, 256, 18, rgba(P['k']))
    return c


def word(text, scale, fill_top, fill_bottom, outline=P['k'], shadow=P['q']):
    """Big letters: the font scaled up, a two-colour fill, an outline and a drop shadow."""
    gw = sum(len(GLYPHS[ch][0]) + 1 for ch in text) - 1
    c = Canvas(gw * scale + 4, 7 * scale + 5)
    x = 2
    for ch in text:
        g = GLYPHS[ch]
        for gy, row in enumerate(g):
            for gx, v in enumerate(row):
                if v == '#':
                    col = mix(fill_top, fill_bottom, gy / 6)
                    c.rect(x + gx * scale + 1, 2 + gy * scale + 2, scale, scale, rgba(shadow))
                    c.rect(x + gx * scale, 2 + gy * scale, scale, scale, rgba(col))
        x += (len(g[0]) + 1) * scale
    # holes in the cheese
    for y in range(c.h):
        for xx in range(c.w):
            p = c.get(xx, y)
            if p[3] and p[:3] != tuple(shadow) and noise(xx // 2, y // 2, 11) > 0.93:
                c.set(xx, y, rgba(mix(p[:3], fill_bottom, 0.6)))
    c.outline(outline)
    return c


def logo():
    top = word('PANTRY', 4, P['e'], P['F'])
    mid = word('QUEST', 4, P['e'], P['F'])
    two = word('II', 5, P['r'], P['R'], shadow=P['k'])
    w = max(top.w, mid.w + two.w + 6)
    c = Canvas(w, top.h + mid.h + 2)
    c.paste(top, (w - top.w) // 2, 0)
    c.paste(mid, (w - mid.w - two.w - 6) // 2, top.h + 2)
    c.paste(two, (w - mid.w - two.w - 6) // 2 + mid.w + 6, top.h + 2 - 4)
    return c
