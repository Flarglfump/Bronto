"""Portraits for the dialogue box (32 x 32) and the people Basil meets (24 x 32 sprites)."""
import math
from pixels import Canvas, PALETTE, from_text, sheet, rgba, mix, noise

P = PALETTE


def outlined(c):
    c.outline()
    return c


def portrait_basil():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (40, 30, 60))
    c.ellipse(16, 21, 10, 10, P['s'])                       # face
    c.ellipse(16, 25, 7, 4, P['s'])
    c.rect(3, 9, 26, 6, P['o'])                             # the pot helmet
    c.ellipse(16, 10, 12, 6, P['o'])
    c.rect(3, 14, 26, 2, P['k'])                            # its rim
    c.rect(20, 7, 4, 2, P['y'])                             # shine
    c.rect(0, 11, 4, 2, P['G'])                             # handle
    c.rect(15, 0, 2, 5, P['L']); c.ellipse(13, 2, 3, 2, P['l']); c.ellipse(19, 3, 3, 2, P['L'])   # basil sprig
    c.rect(10, 18, 4, 4, P['w']); c.rect(18, 18, 4, 4, P['w'])                                   # eyes
    c.rect(12, 19, 2, 3, P['k']); c.rect(20, 19, 2, 3, P['k'])
    c.rect(13, 26, 6, 1, P['S'])                            # a determined mouth
    c.rect(7, 23, 3, 2, P['p']); c.rect(22, 23, 3, 2, P['p'])
    c.rect(6, 30, 20, 2, P['d'])                            # green scarf
    return c


def portrait_mozz():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (30, 40, 50))
    c.ellipse(16, 16, 12, 12, P['w'])                       # a ball of mozzarella
    c.ellipse(13, 12, 5, 4, P['a'])
    c.ellipse(16, 16, 12, 12, P['w'])
    c.ellipse(11, 10, 3, 2, (255, 255, 255))
    c.rect(9, 14, 4, 3, P['k']); c.rect(19, 14, 4, 3, P['k'])                                   # eyes
    c.ellipse(21, 15.5, 4, 4, (0, 0, 0, 0))
    for a in range(16):                                     # monocle
        x = 21 + math.cos(a * math.pi / 8) * 4
        y = 15.5 + math.sin(a * math.pi / 8) * 4
        c.set(int(x), int(y), P['Y'])
    c.rect(20, 14, 3, 3, P['k'])
    c.line(25, 17, 27, 27, P['Y'])
    c.rect(8, 20, 7, 2, P['W']); c.rect(17, 20, 7, 2, P['W'])                                   # moustache
    c.rect(9, 22, 4, 1, P['W']); c.rect(19, 22, 4, 1, P['W'])
    c.rect(10, 28, 12, 4, P['k'])                           # butler's coat
    c.rect(13, 27, 6, 3, P['R'])                            # bow tie
    c.rect(15, 27, 2, 3, P['q'])
    return c


def portrait_pip():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (60, 30, 30))
    c.ellipse(16, 21, 9, 9, P['s'])
    c.rect(7, 12, 18, 5, P['N'])                            # hair
    c.ellipse(16, 7, 10, 7, P['w'])                         # chef's hat
    c.rect(8, 8, 16, 6, P['w'])
    c.rect(8, 13, 16, 2, P['W'])
    c.rect(11, 19, 3, 3, P['k']); c.rect(18, 19, 3, 3, P['k'])
    c.rect(12, 26, 8, 2, P['R'])                            # smile
    c.rect(13, 27, 6, 1, P['p'])
    c.rect(8, 23, 3, 2, P['p']); c.rect(21, 23, 3, 2, P['p'])
    c.rect(6, 30, 20, 2, P['w'])
    return c


def portrait_count():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (20, 10, 30))
    for y in range(20, 32):                                  # the high collar
        for x in range(32):
            if abs(x - 16) > 18 - (y - 20) * 1.2:
                pass
    c.rect(0, 22, 32, 10, P['q'])
    c.rect(3, 18, 8, 14, P['u']); c.rect(21, 18, 8, 14, P['u'])
    c.ellipse(16, 16, 9, 11, (220, 230, 240))                # blue cheese face
    for x, y in ((10, 10), (20, 9), (13, 22), (21, 20), (17, 13), (9, 17)):
        c.set(x, y, P['B']); c.set(x + 1, y, P['b'])
    c.rect(8, 4, 16, 4, P['k'])                              # slick hair, a widow's peak
    c.rect(14, 8, 4, 2, P['k'])
    c.rect(10, 13, 4, 2, P['r']); c.rect(18, 13, 4, 2, P['r'])   # red eyes
    c.rect(11, 13, 1, 2, P['y']); c.rect(19, 13, 1, 2, P['y'])
    c.rect(12, 21, 8, 2, P['k'])                            # mouth and fangs
    c.set(13, 23, P['w']); c.set(18, 23, P['w'])
    c.rect(14, 26, 4, 3, P['Y'])                            # a gold brooch
    return c


def portrait_stale():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (40, 26, 20))
    c.ellipse(16, 15, 13, 12, P['n'])                       # a loaf for a helmet
    c.ellipse(16, 11, 12, 7, P['t'])
    for x in (8, 14, 20):
        c.line(x, 6, x + 4, 10, P['N'])                    # its scores
    for x, y in ((7, 18), (23, 12), (12, 23), (25, 20)):     # mould
        c.ellipse(x, y, 2, 2, P['m'])
    c.rect(7, 15, 18, 4, P['k'])                            # the visor slit
    c.rect(10, 16, 3, 2, P['r']); c.rect(19, 16, 3, 2, P['r'])
    c.rect(4, 27, 24, 5, P['G'])                            # armour
    c.rect(4, 27, 24, 1, P['W'])
    return c


def portrait_sundae():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (20, 40, 70))
    c.ellipse(16, 18, 14, 12, P['w'])                       # scoops
    c.ellipse(9, 12, 7, 6, P['p'])
    c.ellipse(23, 12, 7, 6, (140, 90, 60))
    c.ellipse(16, 4, 3, 3, P['r'])                          # the cherry
    c.line(16, 1, 19, 0, P['d'])
    c.rect(9, 18, 5, 3, P['b']); c.rect(18, 18, 5, 3, P['b'])   # angry eyes
    c.line(8, 16, 14, 18, P['k']); c.line(24, 16, 18, 18, P['k'])
    c.rect(10, 24, 12, 4, P['k'])                           # roaring mouth
    c.rect(11, 24, 2, 2, P['w']); c.rect(19, 24, 2, 2, P['w'])
    for x in (4, 9, 22, 27):
        c.rect(x, 26, 2, 4, (140, 90, 60))                  # drips of chocolate
    return c


def portrait_brine():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (20, 40, 30))
    c.ellipse(16, 15, 9, 13, P['L'])                        # a pickle
    for x, y in ((11, 8), (20, 11), (13, 20), (19, 22), (16, 5)):
        c.ellipse(x, y, 1.2, 1.2, P['l'])
    c.rect(7, 11, 18, 5, P['G'])                            # goggles
    c.ellipse(11, 13, 3, 3, P['c']); c.ellipse(21, 13, 3, 3, P['c'])
    c.rect(10, 12, 2, 2, P['k']); c.rect(20, 12, 2, 2, P['k'])
    c.rect(12, 21, 8, 2, P['k'])                            # a mad grin
    c.rect(13, 21, 6, 1, P['w'])
    c.rect(4, 27, 24, 5, P['w'])                            # lab coat
    c.rect(14, 27, 4, 5, P['L'])
    return c


def portrait_wyrm():
    c = Canvas(32, 32)
    c.rect(0, 0, 32, 32, (50, 10, 10))
    for y in range(32):
        for x in range(32):
            d = ((x - 15) / 13) ** 2 + ((y - 17) / 11) ** 2
            if d < 1:
                c.set(x, y, P['o'] if noise(x, y, 5) > 0.3 else P['f'])
    c.ellipse(24, 22, 7, 5, P['O'])                         # snout
    c.rect(26, 20, 2, 2, P['k'])
    for x, y in ((4, 4), (9, 2), (14, 3)):                  # flame crest
        c.ellipse(x, y + 3, 2, 4, P['H'])
    c.ellipse(13, 14, 3, 3, P['y'])
    c.rect(13, 13, 2, 3, P['k'])
    c.rect(18, 26, 12, 1, P['k'])
    for x in (20, 24, 28):
        c.set(x, 27, P['w'])
    return c


def portraits():
    frames = [portrait_basil(), portrait_mozz(), portrait_pip(), portrait_count(), portrait_stale(),
              portrait_sundae(), portrait_brine(), portrait_wyrm()]
    for f in frames:                                         # a frame round each
        for i in range(32):
            f.set(i, 0, P['k']); f.set(i, 31, P['k']); f.set(0, i, P['k']); f.set(31, i, P['k'])
    return sheet(frames, 32, 32)


MOZZ = [".........kkkkk..........",
        ".......kkwwwwwkk........",
        "......kwwwawwwwwk.......",
        ".....kwwwaawwwwwwk......",
        ".....kwwwwwwwwwwwk......",
        ".....kwwkkwwwkkwwk......",
        ".....kwwkkwwwYkYwk......",
        ".....kwwwwwwwkYkwk......",
        ".....kwWWWwwWWWwwk......",
        "......kwWwwwwWwwk.......",
        ".......kwwwwwwwk........",
        "........kkkkkkk.........",
        ".......kkkRRRkkk........",
        "......kkkkRqRkkkk.......",
        ".....kkkkkkwkkkkkk......",
        ".....kkkkkkwkkkkkk......",
        "....kkGkkkkwkkkkGkk.....",
        "....kwkkkkkwkkkkkwk.....",
        "....kwkkkkkwkkkkkwk.....",
        "....kwkkkkkYkkkkkwk.....",
        "....kkkkkkkwkkkkkkk.....",
        ".....kkkkkkwkkkkkk......",
        ".....kkkkkkkkkkkkk......",
        ".....kkkkkk.kkkkkk......",
        ".....kkkkk...kkkkk......",
        ".....kkkk.....kkkk......",
        ".....kkkk.....kkkk......",
        ".....kkkk.....kkkk......",
        ".....kkkk.....kkkk......",
        "....kGGGk.....kGGGk.....",
        "....kkkkk.....kkkkk.....",
        "........................"]

PIP = ["........kkkkkk..........",
       "......kkwwwwwwkk........",
       ".....kwwwwwwwwwwk.......",
       ".....kwwwwwwwwwwk.......",
       ".....kwwwwwwwwwwk.......",
       "......kWWWWWWWWk........",
       ".....kNNNNNNNNNNk.......",
       ".....kNssssssssNk.......",
       ".....kskkssskksk........",
       ".....kssssssssssk.......",
       "......kpssRRsspk........",
       ".......kssssssk.........",
       "......kkwwwwwwkk........",
       ".....kwwwwwwwwwwk.......",
       "....kswwwwrwwwwwsk......",
       "....kswwwwrwwwwwsk......",
       "....kswwwwrwwwwwsk......",
       ".....kwwwwwwwwwwk.......",
       ".....kNNNNNNNNNNk.......",
       ".....kwwwwwwwwwwk.......",
       ".....kwwwwwwwwwwk.......",
       "......kwwwkkwwwk........",
       "......knnnk.knnnk.......",
       "......knnnk.knnnk.......",
       "......knnnk.knnnk.......",
       "......knnnk.knnnk.......",
       "......knnnk.knnnk.......",
       ".....kkNNNk.kNNNkk......",
       ".....kNNNNk.kNNNNk......",
       ".....kkkkkk.kkkkkk......",
       "........................",
       "........................"]


def person(rows):
    a = from_text(rows)
    b = Canvas(24, 32)                                      # a breathing frame: one pixel lower at the top
    for y in range(32):
        for x in range(24):
            src = y - 1 if y < 14 else y
            if 0 <= src < 32:
                p = a.get(x, src)
                if p[3]:
                    b.set(x, y, p)
    return sheet([a, b], 24, 32)
