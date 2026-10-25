"""The room builder: rooms are rectangles of screens on the world grid, and each is built
from a few drawing commands (walls, platforms, blocks, enemies, items). make_rooms.py
turns them into scenes; check_world.py checks the game can be finished.

Coordinates in a room are in tiles: (0, 0) is its top-left tile. A screen (a cell of the
world grid) is 16 x 15 tiles of 16 pixels.
"""
import art_tiles

SW, SH = 16, 15              # tiles per screen
TILE = 16

# Doors: where their openings are in a screen.
DOOR_ROWS = (10, 11, 12)    # the opening in a side wall (the floor under it is row 13)
DOOR_FLOOR = 13
SHAFT_COLS = (7, 8)          # the opening in a floor or a ceiling

AREAS = {   # name: (title, tileset style, music, map colour)
    'village': ('CRUMB VILLAGE', 'village', 'music/village.wav', (110, 100, 170)),
    'catacombs': ('RYE CATACOMBS', 'catacombs', 'music/catacombs.wav', (170, 110, 60)),
    'frost': ('FROSTBITE CELLAR', 'frost', 'music/frost.wav', (90, 160, 220)),
    'molten': ('MOLTEN KITCHENS', 'molten', 'music/molten.wav', (220, 90, 50)),
    'brine': ('BRINE WORKS', 'brine', 'music/brine.wav', (90, 170, 100)),
    'castle': ('CASTLE FROMAGE', 'castle', 'music/castle.wav', (200, 170, 70)),
}


class Thing:
    """Something placed in a room: an enemy, an item, a hearth... (a prefab and its vars)."""

    def __init__(self, what, x, y, **vars):
        self.kind, self.x, self.y, self.vars = what, x, y, vars


class Room:
    def __init__(self, rid, area, x, y, w, h, name='', heat=False, dark=False, music=None, boss=None):
        self.id, self.area, self.x, self.y, self.w, self.h = rid, area, x, y, w, h
        self.name = name
        self.heat = heat
        self.dark = dark
        self.music = music
        self.boss = boss
        self.W, self.H = w * SW, h * SH
        self.t = [['#' for _ in range(self.W)] for _ in range(self.H)]   # '#' = solid for now (autotiled later)
        self.front = [[' ' for _ in range(self.W)] for _ in range(self.H)]
        self.bg_extra = {}
        self.doors = []          # (side, index, kind)
        self.things = []
        self.freeze_spots = []   # cells a frozen enemy can be stood on (for the checker)
        self.notes = []
        # an open interior inside a one-tile frame (two rows of floor and ceiling)
        self.clear(1, 2, self.W - 2, self.H - 4)

    # ---- the shape ---------------------------------------------------------------------
    def inside(self, x, y):
        return 0 <= x < self.W and 0 <= y < self.H

    def fill(self, ch, x, y, w=1, h=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.inside(xx, yy):
                    self.t[yy][xx] = ch
        return self

    def solid(self, x, y, w=1, h=1):
        return self.fill('#', x, y, w, h)

    def clear(self, x, y, w=1, h=1):
        return self.fill('.', x, y, w, h)

    def plat(self, x, y, w):
        return self.fill('#', x, y, w, 1)

    def ledge(self, x, y, w):
        return self.fill('h', x, y, w, 1)

    def block(self, ch, x, y, w=1, h=1):
        """Special tiles: 'c' crumb, 'x' bomb, 'r' wax, '^' spikes, 'v' spikes down, '~' lava, 'u' pillar, 'g' grate."""
        return self.fill(ch, x, y, w, h)

    def liquid(self, x, y, w, h):
        """Brine or slush: drawn in front, see-through; the top row is its surface."""
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if self.inside(xx, yy) and self.t[yy][xx] == '.':
                    self.t[yy][xx] = '='
                    self.front[yy][xx] = '-' if yy == y else '='
        return self

    def bg(self, ch, x, y):
        self.bg_extra[(x, y)] = ch
        return self

    def deco_front(self, ch, x, y):
        self.front[y][x] = ch
        return self

    # ---- doors -------------------------------------------------------------------------
    def door(self, side, index=0, kind='blue'):
        """A door in a side wall (W / E, index = which screen down) or a shaft in the floor or
        ceiling (N / S, index = which screen across). Kinds: blue, wax (hot sauce), boss
        (shut while its guardian lives), gate (the castle gate), open (no cap)."""
        self.doors.append((side, index, kind))
        if side in 'WE':
            x = 0 if side == 'W' else self.W - 1
            for r in DOOR_ROWS:
                self.t[index * SH + r][x] = '.'
            for dx in range(3):                     # a floor to stand on in the doorway
                xx = x + dx if side == 'W' else x - dx
                self.t[index * SH + DOOR_FLOOR][xx] = '#' if self.t[index * SH + DOOR_FLOOR][xx] in '.#' else self.t[index * SH + DOOR_FLOOR][xx]
            for dx in range(1, 3):                  # and room to walk through it
                xx = x + dx if side == 'W' else x - dx
                for r in DOOR_ROWS:
                    if self.t[index * SH + r][xx] == '#':
                        self.t[index * SH + r][xx] = '.'
        else:
            y0 = 0 if side == 'N' else self.H - 2
            for c in SHAFT_COLS:
                for yy in range(y0, y0 + 2):
                    self.t[yy][index * SW + c] = '.'
        return self

    # ---- things ------------------------------------------------------------------------
    def add(self, what, x, y, **vars):
        """A thing at tile (x, y): its feet on the bottom of that tile for walkers, centred otherwise."""
        self.things.append(Thing(what, x, y, **vars))
        return self

    def enemy(self, kind, x, y, **vars):
        return self.add('enemy:' + kind, x, y, **vars)

    def item(self, kind, x, y, iid):
        return self.add('item', x, y, kind=kind, iid=iid)

    def freeze(self, x, y):
        """A spot a frozen flyer makes a step at (for the checker: with Frost Sugar, it's solid)."""
        self.freeze_spots.append((x, y))
        return self

    # ---- for the tools --------------------------------------------------------------------
    def solid_at(self, x, y):
        if not self.inside(x, y):
            return True
        return self.t[y][x] in '#cxruhgY'

    def cells(self):
        return [(self.x + i, self.y + j) for j in range(self.h) for i in range(self.w)]

    def autotiled(self):
        """The solid map with each wall tile chosen by which neighbours are solid too."""
        out = []
        for y in range(self.H):
            row = []
            for x in range(self.W):
                ch = self.t[y][x]
                if ch == '#':
                    m = 0
                    for bit, (dx, dy) in ((1, (0, -1)), (2, (1, 0)), (4, (0, 1)), (8, (-1, 0))):
                        nx, ny = x + dx, y + dy
                        if not self.inside(nx, ny) or self.t[ny][nx] in '#xr':
                            m |= bit
                    if m == 15:
                        v = (x * 7 + y * 13 + (x // 3) * 5) % 23
                        row.append('Q' if v == 0 else ('R' if v == 1 else ('S' if v == 2 else art_tiles.SOLID_CHARS[15])))
                    else:
                        row.append(art_tiles.SOLID_CHARS[m])
                elif ch in ('=',):
                    row.append('.')                 # liquid is drawn in front
                else:
                    row.append(ch)
            out.append(''.join(row))
        return out
