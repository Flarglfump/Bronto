"""Small things: doors, shots, effects, items, pickups, hearths, map tables, HUD icons."""
import math
from pixels import Canvas, PALETTE, from_text, sheet, rgba, mix, noise

P = PALETTE


def door_sheet():
    """16 x 48 frames: blue, wax (red), guardian (purple), gate (gold), half open, open (just the frame)."""
    caps = [((80, 140, 250), (40, 70, 170), (170, 210, 255)), ((220, 50, 60), (130, 20, 30), (255, 150, 140)),
            ((150, 70, 200), (80, 30, 120), (220, 160, 255)), ((240, 200, 70), (160, 120, 30), (255, 240, 170))]
    frames = []

    def frame_only():
        c = Canvas(16, 48)
        for y in range(48):
            for x in (0, 1, 14, 15):
                c.set(x, y, P['G'] if x in (1, 14) else P['k'])
        for x in range(16):
            for y in (0, 1, 46, 47):
                c.set(x, y, P['g'] if y in (1, 46) else P['k'])
        for y in (8, 24, 40):
            c.set(1, y, P['W']); c.set(14, y, P['W'])
        return c
    for main, dark, light in caps:
        c = frame_only()
        for y in range(2, 46):
            for x in range(3, 13):
                col = main
                if x in (3, 4):
                    col = light
                if x in (11, 12):
                    col = dark
                if y in (2, 45):
                    col = dark
                c.set(x, y, col)
        for y in range(6, 42, 6):                       # rivets / bubble ridges
            c.set(7, y, light); c.set(8, y, light)
        c.ellipse(8, 24, 3, 5, light)
        c.ellipse(8, 24, 2, 3.5, main)
        frames.append(c)
    half = frame_only()
    for y in range(2, 46):
        for x in range(3, 13):
            if abs(y - 24) > 14:
                half.set(x, y, (120, 140, 180))
    frames.append(half)
    frames.append(frame_only())
    return sheet(frames, 16, 48)


def shots_sheet():
    """8 x 8: pepper, frost, garlic, garlic+frost (sideways), then the same going up. Each has a
    dark outline round a ring that contrasts with its middle, so it shows up on dark rooms and pale ones alike."""
    cols = [  # rim, middle, glint
        ((255, 222, 120), (84, 46, 26), (255, 255, 255)),     # pepper: a dark peppercorn, glowing hot round the edge
        ((30, 70, 170), (170, 235, 255), (255, 255, 255)),     # frost: ice with a deep blue edge
        ((70, 110, 30), (240, 240, 200), (200, 240, 120)),     # garlic
        ((30, 110, 120), (200, 240, 255), (160, 255, 200)),    # garlic + frost
    ]
    frames = []
    for up in (False, True):
        for i, (rim, main, light) in enumerate(cols):
            c = Canvas(8, 8)
            c.ellipse(4, 4, 3.9, 3.9, (20, 12, 16))        # a dark outline, then a bright ring, then the middle
            c.ellipse(4, 4, 2.9, 2.9, rim)
            c.ellipse(4, 4, 1.8, 1.8, main)
            c.set(3, 3, light)
            if i >= 2:                                    # the garlic wave: rings
                for k in range(8):
                    a = k * math.pi / 4
                    c.set(int(4 + math.cos(a) * 3.5), int(4 + math.sin(a) * 3.5), light)
            frames.append(c)
    return sheet(frames, 8, 8)


def rocket():
    c = from_text([
        "...............",
        "..kk...........",
        ".kyokkkkkkkk...",
        "kyorrrrrrrrRkk.",
        "kyorrrwrrrrRRrk",
        ".kyokkkkkkkk...",
        "..kk...........",
        "...............",
    ])
    return c


def popcorn_sheet():
    a = from_text([
        "..kkkk..",
        ".kyyYYk.",
        "kyyyyYYk",
        "kyyyYYYk",
        "kYyyYYYk",
        ".kYYYYk.",
        "..kkkk..",
        "........"])
    b = a.copy()
    b.recolor({P['y']: (255, 255, 255), P['Y']: (252, 200, 120)})
    return sheet([a, b], 8, 8)


def fx_sheet(kind):
    """16 x 16 animations: puff (4), burst (5), crumbs (4), blast (5), pop (4)."""
    frames = []
    if kind == 'puff':
        for i in range(4):
            c = Canvas(16, 16)
            r = 2 + i * 1.5
            for k in range(6):
                a = k * math.pi / 3 + i
                c.set(int(8 + math.cos(a) * r), int(8 + math.sin(a) * r), P['W'] if i < 2 else P['g'])
            frames.append(c)
    elif kind == 'burst':
        for i in range(5):
            c = Canvas(16, 16)
            r = 2 + i * 1.6
            c.ellipse(8, 8, r, r, P['y'] if i < 2 else (P['o'] if i < 4 else P['G']))
            if i > 1:
                c.ellipse(8, 8, r - 2, r - 2, (0, 0, 0, 0))
            for k in range(8):
                a = k * math.pi / 4
                c.set(int(8 + math.cos(a) * (r + 1)), int(8 + math.sin(a) * (r + 1)), P['w'])
            frames.append(c)
    elif kind == 'crumbs':
        for i in range(4):
            c = Canvas(16, 16)
            for k, (dx, dy) in enumerate(((-1, -1), (1, -1), (-1, 0.5), (1, 0.7), (0.2, -1.3))):
                x = 8 + dx * (2 + i * 2)
                y = 8 + dy * (2 + i * 2) + i * i * 0.5
                c.rect(int(x), int(y), 2, 2, P['t'] if k % 2 else P['N'])
            frames.append(c)
    elif kind == 'blast':
        for i in range(5):
            c = Canvas(16, 16)
            r = 3 + i * 1.5
            c.ellipse(8, 8, r, r, P['f'] if i < 3 else P['O'])
            c.ellipse(8, 8, r * 0.6, r * 0.6, P['H'] if i < 3 else P['f'])
            if i >= 3:
                c.ellipse(8, 8, r - 2.5, r - 2.5, (0, 0, 0, 0))
            frames.append(c)
    elif kind == 'pop':
        for i in range(4):
            c = Canvas(16, 16)
            for k in range(7):
                a = k * 2 * math.pi / 7
                r = 2 + i * 2
                c.ellipse(8 + math.cos(a) * r, 8 + math.sin(a) * r, 1.6, 1.6, P['w'] if i < 3 else P['e'])
            frames.append(c)
    return sheet(frames, 16, 16)


def items_sheet():
    """16 x 16 items: dough, popcorn, sauce, frost, leaven, mitts, garlic, whisk, cake, packet, page, 4 seals."""
    f = []
    # dough wrap: a dumpling
    c = Canvas(16, 16); c.ellipse(8, 9, 6.5, 5.5, P['e']); c.ellipse(8, 7, 4, 2.5, P['E'])
    for x in (5, 8, 11): c.set(x, 5, P['F'])
    c.outline(); f.append(c)
    # popcorn
    c = Canvas(16, 16)
    for (x, y) in ((6, 6), (10, 6), (8, 9), (5, 10), (11, 10)): c.ellipse(x, y, 2.6, 2.6, P['w'])
    c.rect(5, 11, 7, 4, P['r']); c.rect(7, 11, 1, 4, P['w']); c.rect(10, 11, 1, 4, P['w'])
    c.outline(); f.append(c)
    # hot sauce bottle
    c = Canvas(16, 16); c.rect(6, 2, 4, 3, P['d']); c.rect(5, 5, 6, 10, P['r']); c.rect(6, 8, 4, 3, P['e'])
    c.set(6, 6, P['p']); c.outline(); f.append(c)
    # frost sugar: a sugar cube with frost
    c = Canvas(16, 16); c.rect(3, 4, 10, 9, P['C']); c.rect(3, 4, 10, 2, P['w']); c.rect(11, 4, 2, 9, P['c'])
    for (x, y) in ((5, 8), (8, 10), (9, 7)): c.set(x, y, P['b'])
    c.outline(); f.append(c)
    # leaven boots
    c = Canvas(16, 16); c.rect(4, 3, 5, 9, P['n']); c.rect(4, 10, 9, 3, P['N']); c.rect(4, 13, 10, 1, P['M'])
    c.ellipse(6, 4, 3, 2, P['e']); c.outline(); f.append(c)
    # oven mitts
    c = Canvas(16, 16); c.ellipse(8, 8, 5, 6, P['r']); c.ellipse(4, 7, 2, 3, P['r']); c.rect(5, 12, 7, 3, P['w'])
    c.line(6, 5, 10, 9, P['R']); c.line(10, 5, 6, 9, P['R']); c.outline(); f.append(c)
    # garlic
    c = Canvas(16, 16); c.ellipse(8, 9, 5.5, 5, P['w']); c.ellipse(8, 9, 1.5, 5, P['W']); c.rect(7, 2, 2, 4, P['l'])
    c.line(4, 13, 12, 13, P['W']); c.outline(); f.append(c)
    # whisk
    c = Canvas(16, 16); c.rect(7, 9, 2, 6, P['n'])
    for dx in (-3, -1, 1, 3): c.line(8, 9, 8 + dx, 2, P['W'])
    c.ellipse(8, 4, 3.5, 3, (0, 0, 0, 0)); c.outline(); f.append(c)
    # cake slice (energy tank)
    c = Canvas(16, 16)
    for y in range(5, 14):
        for x in range(2 + (13 - y) // 2, 14):
            c.set(x, y, P['e'] if y not in (8, 11) else P['p'])
    c.rect(9, 3, 4, 2, P['w']); c.set(11, 2, P['r']); c.outline(); f.append(c)
    # sauce packet (more rockets)
    c = Canvas(16, 16); c.rect(3, 3, 10, 10, P['o']); c.rect(3, 3, 10, 2, P['O']); c.rect(5, 7, 6, 3, P['r'])
    c.outline(); f.append(c)
    # a diary page
    c = Canvas(16, 16); c.rect(3, 2, 10, 12, P['e'])
    for y in (5, 7, 9, 11): c.rect(5, y, 6, 1, P['G'])
    c.rect(10, 2, 3, 3, P['F']); c.outline(); f.append(c)
    # the four seals
    for col in ((240, 240, 250), (250, 180, 220), (200, 230, 140), (120, 80, 60)):
        c = Canvas(16, 16); c.ellipse(8, 8, 6.5, 6.5, P['Y']); c.ellipse(8, 8, 4.5, 4.5, col)
        c.rect(7, 5, 2, 6, P['Y']); c.outline(); f.append(c)
    return sheet(f, 16, 16)


def drops_sheet():
    """8 x 8: energy (2), big energy (2), sauce (2)."""
    f = []
    for col, light in ((P['p'], P['w']), (P['r'], P['p'])):
        for k in range(2):
            c = Canvas(8, 8)
            c.ellipse(4, 4, 3 - k * 0.5, 3 - k * 0.5, col)
            c.set(3, 3, light)
            f.append(c)
    for k in range(2):
        c = Canvas(8, 8); c.rect(2, 1, 4, 6, P['r']); c.rect(3, 0, 2, 1, P['d']); c.set(3, 2, P['p'] if k else P['w'])
        f.append(c)
    return sheet(f, 8, 8)


def hearth_sheet():
    """32 x 32 x 4: an old brick oven with a fire (save here)."""
    f = []
    for i in range(4):
        c = Canvas(32, 32)
        c.ellipse(16, 18, 14, 12, (150, 70, 50))
        c.rect(2, 18, 28, 14, (150, 70, 50))
        for y in range(8, 32, 4):
            for x in range(2, 30):
                if c.get(x, y)[3] and (x + (y // 4) * 3) % 8:
                    pass
                if c.get(x, y)[3]:
                    c.set(x, y, (110, 46, 34))
        c.ellipse(16, 22, 8, 7, P['k'])
        c.rect(8, 22, 16, 9, P['k'])
        for k in range(5):
            fx = 10 + k * 3
            h = 4 + ((k * 7 + i * 3) % 5)
            c.ellipse(fx, 29 - h / 2, 2, h / 2 + 1, P['f'])
            c.ellipse(fx, 30 - h / 3, 1, h / 3, P['y'])
        c.rect(0, 30, 32, 2, (90, 40, 30))
        c.outline()
        f.append(c)
    return sheet(f, 32, 32)


def maptable_sheet():
    """32 x 32 x 2: a table with a map spread on it (its glow pulses)."""
    f = []
    for i in range(2):
        c = Canvas(32, 32)
        c.rect(3, 26, 3, 6, P['N']); c.rect(26, 26, 3, 6, P['N'])
        c.rect(1, 20, 30, 6, P['n']); c.rect(1, 20, 30, 1, P['t'])
        c.rect(4, 14, 24, 7, P['e'] if i else P['w'])
        for x, y in ((7, 16), (12, 18), (18, 15), (23, 17)): c.rect(x, y, 3, 2, P['b'])
        c.line(8, 17, 24, 16, P['r'])
        c.outline()
        f.append(c)
    return sheet(f, 32, 32)


def hud_icons():
    """8 x 8: the rocket (sauce) icon, a seal, a page."""
    f = []
    c = Canvas(8, 8); c.rect(2, 1, 4, 6, P['r']); c.rect(3, 0, 2, 1, P['d']); c.set(3, 2, P['p']); f.append(c)
    c = Canvas(8, 8); c.ellipse(4, 4, 3.5, 3.5, P['Y']); c.ellipse(4, 4, 2, 2, P['e']); f.append(c)
    c = Canvas(8, 8); c.rect(1, 1, 6, 6, P['e']); c.rect(2, 3, 4, 1, P['G']); f.append(c)
    return sheet(f, 8, 8)


def spit_sheet():
    """8 x 8, two frames each: seed, brine, fire, bone (breadstick), snow."""
    f = []
    for main, light in ((P['N'], P['n']), (P['J'], P['j']), (P['f'], P['y']), (P['t'], P['e']), (P['C'], P['w'])):
        for k in range(2):
            c = Canvas(8, 8)
            c.ellipse(4, 4, 2.5 + k * 0.4, 2.5 - k * 0.3, main)
            c.set(3, 3, light)
            f.append(c)
    return sheet(f, 8, 8)


def signpost():
    """16 x 16: a wooden sign on a post."""
    c = Canvas(16, 16)
    c.rect(7, 8, 2, 8, P['N'])
    c.rect(1, 2, 14, 7, P['n'])
    c.rect(1, 2, 14, 1, P['t'])
    for y in (4, 6):
        c.rect(3, y, 10, 1, P['N'])
    c.outline()
    return c
