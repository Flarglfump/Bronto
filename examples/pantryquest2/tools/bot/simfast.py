"""simc.c through ctypes: the sim's steps and moves, many times faster. It's built on first use
(any C compiler); without one the bot uses sim.py, which gives the same answers, slowly."""
import ctypes
import os
import subprocess

import sim

HERE = os.path.dirname(os.path.abspath(__file__))
SO = os.path.join(HERE, 'simc.so')
SRC = os.path.join(HERE, 'simc.c')

KEYBITS = {'left': 1, 'right': 2, 'up': 4, 'down': 8, 'jump': 16, 'aim': 32, 'ball': 64, 'fire': 128, 'rocket': 256}


class CGrid(ctypes.Structure):
    _fields_ = [('cols', ctypes.c_int), ('rows', ctypes.c_int),
                ('x0', ctypes.c_double), ('y0', ctypes.c_double), ('ts', ctypes.c_double),
                ('W', ctypes.c_double), ('H', ctypes.c_double),
                ('solid', ctypes.POINTER(ctypes.c_ubyte)), ('tile', ctypes.c_char_p),
                ('liquid', ctypes.POINTER(ctypes.c_ubyte)),
                ('fcols', ctypes.c_int), ('frows', ctypes.c_int), ('fx0', ctypes.c_double), ('fy0', ctypes.c_double),
                ('plat', ctypes.POINTER(ctypes.c_double)), ('nplat', ctypes.c_int),
                ('has_dough', ctypes.c_int), ('has_whisk', ctypes.c_int), ('has_leaven', ctypes.c_int)]


class CBasil(ctypes.Structure):
    _fields_ = [('x', ctypes.c_double), ('y', ctypes.c_double), ('vx', ctypes.c_double), ('vy', ctypes.c_double),
                ('sx', ctypes.c_double), ('sy', ctypes.c_double),
                ('hurt_t', ctypes.c_double), ('safe_t', ctypes.c_double), ('lava_t', ctypes.c_double), ('shot_cd', ctypes.c_double),
                ('facing', ctypes.c_int), ('grounded', ctypes.c_int), ('ball', ctypes.c_int), ('crouch', ctypes.c_int),
                ('spin', ctypes.c_int), ('lifted', ctypes.c_int), ('in_liquid', ctypes.c_int),
                ('hurt_count', ctypes.c_int), ('left_room', ctypes.c_int)]


lib = None


def load():
    global lib
    if lib is not None:
        return lib
    try:
        if not os.path.exists(SO) or os.path.getmtime(SO) < os.path.getmtime(SRC):
            subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-o', SO, SRC, '-lm'], check=True)
        lib = ctypes.CDLL(SO)
        lib.sim_play.restype = ctypes.c_int
        lib.sim_play.argtypes = [ctypes.POINTER(CGrid), ctypes.POINTER(CBasil), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
                                 ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                 ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_int,
                                 ctypes.POINTER(ctypes.c_double), ctypes.c_int, ctypes.c_int,
                                 ctypes.POINTER(ctypes.c_int), ctypes.POINTER(CBasil)]
        lib.sim_frame.argtypes = [ctypes.POINTER(CBasil), ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(CGrid)]
        lib.sim_blocked.restype = ctypes.c_int
        lib.sim_blocked.argtypes = [ctypes.POINTER(CGrid), ctypes.c_double, ctypes.c_double, ctypes.c_int]
    except Exception as e:                                     # (no compiler: the slow way)
        print('simfast: using sim.py (%s)' % e)
        lib = False
    return lib


def bits(keys):
    n = 0
    for k in keys:
        n |= KEYBITS.get(k, 0)
    return n


def cgrid(g, has):
    """The C side of a Grid (kept on it, rebuilt if what he has or the frozen steps change)."""
    key = (bool(has('dough')), bool(has('whisk')), bool(has('leaven')))
    c = getattr(g, '_c', None)                  # (a Grid's tiles and frozen steps don't change once made)
    if c is not None and c[0] == key:
        return c[1]
    cols, rows = g.cols, g.rows
    solid = (ctypes.c_ubyte * (cols * rows))()
    tiles = []
    for r in range(rows):
        row = g.t[r]
        for col in range(cols):
            ch = row[col]
            solid[r * cols + col] = 1 if ch in g.solid_chars else 0
            tiles.append(ch)
    tile = ''.join(tiles).encode('latin-1')
    cg = CGrid()
    cg.cols, cg.rows, cg.x0, cg.y0, cg.ts, cg.W, cg.H = cols, rows, g.x0, g.y0, g.ts, g.W, g.H
    cg.solid = solid
    cg.tile = tile
    keep = [solid, tile]
    if g.front:
        liq = (ctypes.c_ubyte * (g.fcols * g.frows))()
        for r in range(g.frows):
            for col in range(g.fcols):
                liq[r * g.fcols + col] = 1 if g.front[r][col] in ('=', '-') else 0
        cg.liquid = liq
        cg.fcols, cg.frows, cg.fx0, cg.fy0 = g.fcols, g.frows, g.fx0, g.fy0
        keep.append(liq)
    pl = (ctypes.c_double * max(1, 2 * len(g.platforms)))()
    for i, (x, y) in enumerate(g.platforms):
        pl[2 * i], pl[2 * i + 1] = x, y
    cg.plat, cg.nplat = pl, len(g.platforms)
    keep.append(pl)
    cg.has_dough, cg.has_whisk, cg.has_leaven = key[0], key[1], key[2]
    g._c = (key, cg, keep)
    return cg


FIELDS = ('x', 'y', 'vx', 'vy', 'sx', 'sy', 'hurt_t', 'safe_t', 'lava_t', 'shot_cd')
IFIELDS = ('facing', 'grounded', 'ball', 'crouch', 'spin', 'lifted', 'in_liquid', 'hurt_count')


def to_c(b):
    c = CBasil()
    for k in FIELDS:
        setattr(c, k, float(getattr(b, k) or 0))
    for k in IFIELDS:
        setattr(c, k, int(getattr(b, k) or 0))
    c.left_room = ord(b.left_room) if b.left_room else 0
    return c


def from_c(c):
    b = sim.Basil()
    for k in FIELDS:
        setattr(b, k, getattr(c, k))
    for k in IFIELDS:
        setattr(b, k, getattr(c, k))
    for k in ('grounded', 'ball', 'crouch', 'spin', 'lifted', 'in_liquid'):
        setattr(b, k, bool(getattr(b, k)))
    b.left_room = chr(c.left_room) if c.left_room else None
    return b


_tables = {}


def table(move):
    t = getattr(move, '_table', None)                  # (kept on the move: ids of dead moves get reused)
    if t is None:
        t = move._table = (ctypes.c_int * 120)(*[bits(move.fn(f, None)) for f in range(120)])
    return t


KINDS = {'jump': 0, 'walk': 1, 'toggle': 2}
HOW = {1: 'land', 2: 'exit', 3: 'hurt', 0: None}


def play(move, b, g, has, held0=(), schedule=None, max_frames=200, hazards=None, record=None, allow_hurt=False):
    cg = cgrid(g, has)
    cb = to_c(b)
    sched = None
    ns = 0
    if schedule is not None:
        ns = min(len(schedule), max_frames)
        sched = (ctypes.c_int * max(1, ns))(*schedule[:ns])
    hz = None
    nhz = 0
    if hazards:
        nhz = len(hazards)
        hz = (ctypes.c_double * (6 * nhz))()
        for i, m in enumerate(hazards):
            hz[6 * i:6 * i + 6] = [m.x, m.y, m.vx, m.vy, m.hx, m.hy]
    frames = ctypes.c_int(0)
    states = (CBasil * max_frames)() if record is not None else None
    how = lib.sim_play(ctypes.byref(cg), ctypes.byref(cb), table(move), 120, move.length, KINDS[move.kind],
                       1 if move.popcorn else 0, bits(held0), sched, ns, max_frames, hz, nhz, 1 if allow_hurt else 0,
                       ctypes.byref(frames), states)
    e = from_c(cb)
    keys = [move.fn(f, None) if f < 120 else set() for f in range(frames.value)]
    if record is not None:
        for f in range(frames.value):
            record.append(from_c(states[f]))
    return HOW[how], e, frames.value, keys


def frame(b, held, pressed, steps, g, has):
    cg = cgrid(g, has)
    cb = to_c(b)
    lib.sim_frame(ctypes.byref(cb), bits(held), bits([k for k in pressed if k in sim.EDGES]), steps, ctypes.byref(cg))
    nb = from_c(cb)
    for k in sim.Basil.__slots__:
        setattr(b, k, getattr(nb, k))
    return b
