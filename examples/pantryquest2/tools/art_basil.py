"""Basil, Pip's apprentice: a pot helmet with a basil sprig, a chef's jacket, and the
pepper mill he fires peppercorns from. His frames are built from parts, so every frame
has the same helmet and jacket.

Frames are 24 x 32, facing right, his feet on the bottom row and his middle at x = 12.
"""
from pixels import Canvas, PALETTE, from_text, sheet, rgba

HELMET = [            # 17 x 9: a copper saucepan worn upside down (its handle behind), a basil sprig on top
    "..........lL.....",
    ".........lLLd....",
    "..........dL.....",
    "......kkkkkkkkk..",
    ".....kOoooooyyok.",
    "kkkkkOooooooyyook",
    "kGgWkOoooooooooOk",
    "kkkkkkkkkkkkkkkkk",
    "....kWkkkkkkkkkkk",
]
FACE = [              # under the rim: the visor (dark glass with a glint) and his cheek
    "..kssssbbcck",
    "..kSsssbbbck",
    "...kSsssssk.",
    "....kkkkkk..",
]
TORSO = [             # 12 x 9: the white jacket, a green scarf, a belt
    "...kkkkkk...",
    "..kddLLLdk..",
    ".kwwwwwwwwk.",
    ".kwWwwkwwwk.",
    ".kwWwwkwwwk.",
    ".kwWwwwwwWk.",
    ".kNNNyNNNNk.",
    ".kwWwwwwwWk.",
    "..kkkkkkkk..",
]
ARM_FWD = [           # 13 x 6: his arm and the pepper mill (a crank on top, a steel grinder at the end)
    ".kkk..kk.....",
    "kwwWkkgkkkk..",
    "kwwkNnnnnnWkk",
    "kWkkNtnnnnWgk",
    ".kk.kkkkkkkk.",
    ".............",
]
ARM_UP = [            # 6 x 13: the mill pointing up
    "..kk..",
    ".kgWk.",
    ".kWWk.",
    "kNnnNk",
    "kNnnNk",
    "kNnnNk",
    "kNnnNk",
    "kNnnNk",
    ".kkkk.",
    ".kwwk.",
    "kwwWk.",
    "kwWk..",
    ".kk...",
]
LEGS = {              # 12 x 8
    'stand': ["..kkk.kkk...",
              "..kNk.kNk...",
              "..kNk.kNk...",
              "..kNk.kNk...",
              "..kNk.kNk...",
              ".kkGk.kGkk..",
              ".kGGk.kGGk..",
              ".kkkk.kkkk.."],
    'run0': ["..kkk.kkk...",
             "..kNk.kNkk..",
             ".kNk...kNk..",
             ".kNk....kNk.",
             "kNk.....kGk.",
             "kGkk...kGGk.",
             "kGGk...kkkk.",
             "kkkk........"],
    'run1': ["...kkkkk....",
             "...kNNNk....",
             "...kNkNk....",
             "...kNkNk....",
             "..kNk.kGkk..",
             "..kGk.kGGk..",
             "..kGGkkkkk..",
             "..kkkk......"],
    'run2': ["..kkk.kkk...",
             "..kNkkkNk...",
             "...kNkNk....",
             "....kNk.....",
             "...kNkNk....",
             "..kGkkkGk...",
             "..kGGkGGk...",
             "..kkkkkkk..."],
    'run3': ["...kkkkk....",
             "...kNNNk....",
             "....kNNk....",
             "...kNkkNk...",
             "..kNk..kNk..",
             ".kGGk..kGk..",
             ".kkkk..kGGk.",
             ".......kkkk."],
    'jump': ["..kkk.kkk...",
             "..kNk.kNk...",
             "..kNkkNk....",
             "...kNNk.....",
             "..kNk.kNk...",
             ".kGGk.kGk...",
             ".kkkk.kGGk..",
             "......kkkk.."],
    'fall': ["..kkk.kkk...",
             "..kNk.kNk...",
             ".kNk...kNk..",
             ".kNk...kNk..",
             ".kGk...kGk..",
             "kGGk...kGGk.",
             "kkkk...kkkk.",
             "............"],
}

DUMPLING = [          # the Dough Wrap: Basil rolled up in dough, 16 x 16 (two frames of pleats)
    [".....kkkkkk.....",
     "...kkeeeeeekk...",
     "..keeeEeeEeeek..",
     ".keeeEeeeeEeeek.",
     ".keeEeeeeeeEeek.",
     "keeeEeeeeeeeEeek",
     "keeEeeeeeeeeEeek",
     "keeEeeeeeeeeEeek",
     "keeeeeeeeeeeeeek",
     "keeeeeeeeeeeeEek",
     "kEeeeeeeeeeeeEek",
     ".kEeeeeeeeeeEEk.",
     ".kEEeeeeeeeEEEk.",
     "..kEEEEeeEEEEk..",
     "...kkEEEEEEkk...",
     ".....kkkkkk....."],
    [".....kkkkkk.....",
     "...kkeeeeeekk...",
     "..keeeeeeeeeeek.",
     ".keeeeeeeeeeeeek",
     ".kEeeeeeeeeeeeek",
     "kEEeeeeeeeeeeeEk",
     "kEeeEEEeeeEEEeEk",
     "keeEeeeEeEeeeEek",
     "keeeeeeeeeeeeeek",
     "keeeeeeeeeeeeeek",
     "kEeeeeeeeeeeeEek",
     ".kEeeeeeeeeeEEk.",
     ".kEEeeeeeeeEEEk.",
     "..kEEEEeeEEEEk..",
     "...kkEEEEEEkk...",
     ".....kkkkkk....."],
]


def frame(legs='stand', bob=0, arm='fwd', tint=None):
    c = Canvas(24, 32)
    top = 3 + bob
    c.text(LEGS[legs], 6, 24)
    c.text(TORSO, 6, top + 12)
    c.text(HELMET, 2, top)
    c.text(FACE, 7, top + 8)
    if arm == 'fwd':
        c.text(ARM_FWD, 11, top + 14)
    elif arm == 'up':
        c.text(ARM_UP, 13, top + 1)
    if tint:
        c.recolor(tint)
    return c


def spin_frame(i):
    """The Whisk Spin: Basil curled up, turning (a quarter turn a frame)."""
    base = Canvas(24, 24)
    base.ellipse(12, 12, 8, 8, rgba(PALETTE['w']))
    base.ellipse(12, 12, 8, 3, rgba(PALETTE['W']))
    base.ellipse(12, 7, 6, 3, rgba(PALETTE['o']))
    base.rect(10, 3, 2, 3, rgba(PALETTE['L']))
    base.rect(5, 13, 14, 2, rgba(PALETTE['d']))
    base.outline()
    out = Canvas(24, 32)
    # rotate by quarter turns
    for y in range(24):
        for x in range(24):
            p = base.get(x, y)
            if not p[3]:
                continue
            xx, yy = x, y
            for _ in range(i % 4):
                xx, yy = 23 - yy, xx
            out.set(xx, yy + 4, p)
    # motion streaks
    for k in range(3):
        out.set(2 + k, 8 + (i * 5 + k * 7) % 18, rgba(PALETTE['c']))
        out.set(21 - k, 22 - (i * 5 + k * 7) % 18, rgba(PALETTE['c']))
    return out


def ball_frame(i):
    """Rolling: the dumpling, turned a quarter each frame."""
    src = from_text(DUMPLING[i % 2])
    out = Canvas(24, 32)
    for y in range(16):
        for x in range(16):
            p = src.get(x, y)
            if not p[3]:
                continue
            xx, yy = x, y
            for _ in range(i // 2):
                xx, yy = 15 - yy, xx
            out.set(xx + 4, yy + 16, p)
    return out


OVEN_MITTS = {PALETTE['w']: (240, 120, 120), PALETTE['W']: (196, 70, 80)}     # the oven-mitt jacket: red quilting


def frames(tint=None):
    f = [frame('stand', 0, tint=tint)]                                             # 0 stand
    f += [frame('run%d' % i, (0, 1, 0, 1)[i], tint=tint) for i in range(4)]       # 1-4 run
    f += [frame('jump', 0, tint=tint), frame('fall', 0, tint=tint)]               # 5 rise, 6 fall
    f += [frame('stand', 0, 'up', tint=tint)]                                      # 7 aim up
    f += [frame('jump', 0, 'up', tint=tint)]                                       # 8 aim up in the air
    hurt = frame('fall', 1, 'none', tint=tint)
    hurt.recolor({PALETTE['w']: (255, 190, 190)})
    f += [hurt]                                                                     # 9 hurt
    f += [spin_frame(i) for i in range(4)]                                          # 10-13 spin
    f += [ball_frame(i) for i in range(4)]                                          # 14-17 roll
    f += [frame('run%d' % i, (0, 1, 0, 1)[i], 'up', tint=tint) for i in range(4)] # 18-21 run, aiming up
    kneel = frame('jump', 2, 'none', tint=tint)
    f += [kneel]                                                                    # 22 kneel (saving, item fanfare)
    f += [frame('stand', 0, 'none', tint=tint)]                                     # 23 stand, arm down (talking)
    return f


def basil_sheet(tint=None):
    return sheet(frames(tint), 24, 32, 8)
