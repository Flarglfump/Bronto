#!/usr/bin/env python3
"""Draws Pantry Quest's maps of the land from tools/world/overworld.txt.

    python3 examples/pantryquest/tools/make_map.py

Run it after design_overworld.py or make_art.py (it reads the land's screens and
the tiles' colours). It writes:

  art/world_map.bmp   the whole land, a pixel for each tile (256 x 88): the pause menu's map
  art/map_names.bmp   the areas' names, in a tiny pixel font, to lay over it
  art/map_<n>_<red|gold>.bmp   each cellar's number on it (gold: cleared)
  art/minimap.bmp     the same land, 4 x 4 pixels a screen (64 x 32): the HUD's map
  scripts/map_data.bs where each cellar's door is

Each tile is drawn in its own colour (the average of its picture), and the open
ground of each region takes on that region's colour - golden cookie fields,
cocoa mountains, icy fields, pink candy meadows - so the regions read at a
glance. Only Python's standard library is used.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import make_art            # noqa: E402  (the tiles' pictures and palette, and write_bmp)
import design_overworld    # noqa: E402  (the regions, the Sugar Road and the cellars)

LEGEND = '.,TrW~bCs=fIFKX:YpPLGSg'          # the overworld tileset, in order (tools/make_rooms.py)
FLOOR = set('.,fsp')                        # open ground, tinted by region
REGION_TINT = {
    'P': (231, 200, 144),   # Pantry Plains: biscuit
    'I': (196, 226, 242),   # Frosty Fields: ice
    'L': (190, 186, 236),   # Blueberry Lake: lilac
    'W': (156, 206, 112),   # Lettuce Woods: leaf
    'G': (240, 202, 110),   # Fortune Cookie Field: golden
    'M': (164, 112, 76),    # Chocolate Mountains: cocoa
    'B': (150, 196, 120),   # Pickle Marsh: brine green
    'D': (248, 232, 168),   # Sugar Desert: sugar
    'K': (248, 178, 212),   # Candy Meadows: pink
    'C': (238, 226, 178),   # Juice Coast: sand
}
ROAD_TINT = (236, 214, 150)                 # the Sugar Road: packed sugar
TINT = 0.5

# The names on the big map, by hand where they read best: (text, screen column, screen row) -
# the middle of the text, in screens (fractions allowed).
LABELS = [
    ('COOKIE FIELD', 1.7, 1.6), ('CHOCOLATE MOUNTAINS', 7.4, 1.0), ('PICKLE MARSH', 13.0, 0.5),
    ('BLUEBERRY LAKE', 4.0, 2.4), ('FROSTY FIELDS', 8.0, 3.55), ('LETTUCE WOODS', 1.95, 4.85),
    ('SUGAR ROAD', 11.6, 3.3), ('SUGAR DESERT', 12.0, 4.75), ('PANTRY PLAINS', 7.4, 6.5),
    ('CANDY MEADOWS', 13.6, 6.7), ('COAST', 15.55, 4.0),       # (the coast: one screen wide, so down it)
]


# A tiny pixel font for the names on the map, 5 high and mostly 3 wide (M, N and W wider, to tell
# them apart) - the game's own 8 x 8 one would cover the map.
FONT = {
    'A': ['.#.', '#.#', '###', '#.#', '#.#'], 'B': ['##.', '#.#', '##.', '#.#', '##.'], 'C': ['.##', '#..', '#..', '#..', '.##'],
    'D': ['##.', '#.#', '#.#', '#.#', '##.'], 'E': ['###', '#..', '##.', '#..', '###'], 'F': ['###', '#..', '##.', '#..', '#..'],
    'G': ['.##', '#..', '#.#', '#.#', '.##'], 'H': ['#.#', '#.#', '###', '#.#', '#.#'], 'I': ['###', '.#.', '.#.', '.#.', '###'],
    'J': ['..#', '..#', '..#', '#.#', '.#.'], 'K': ['#.#', '#.#', '##.', '#.#', '#.#'], 'L': ['#..', '#..', '#..', '#..', '###'],
    'M': ['#...#', '##.##', '#.#.#', '#...#', '#...#'], 'N': ['#..#', '##.#', '#.##', '#..#', '#..#'], 'O': ['.#.', '#.#', '#.#', '#.#', '.#.'],
    'P': ['##.', '#.#', '##.', '#..', '#..'], 'Q': ['.#.', '#.#', '#.#', '##.', '.##'], 'R': ['##.', '#.#', '##.', '#.#', '#.#'],
    'S': ['.##', '#..', '.#.', '..#', '##.'], 'T': ['###', '.#.', '.#.', '.#.', '.#.'], 'U': ['#.#', '#.#', '#.#', '#.#', '###'],
    'V': ['#.#', '#.#', '#.#', '#.#', '.#.'], 'W': ['#...#', '#...#', '#.#.#', '##.##', '#...#'], 'X': ['#.#', '#.#', '.#.', '#.#', '#.#'],
    'Y': ['#.#', '#.#', '.#.', '.#.', '.#.'], 'Z': ['###', '..#', '.#.', '#..', '###'], ' ': ['...'] * 5,
    '0': ['###', '#.#', '#.#', '#.#', '###'], '1': ['.#.', '##.', '.#.', '.#.', '###'], '2': ['##.', '..#', '.#.', '#..', '###'],
    '3': ['##.', '..#', '.#.', '..#', '##.'], '4': ['#.#', '#.#', '###', '..#', '..#'], '5': ['###', '#..', '##.', '..#', '##.'],
    '6': ['.##', '#..', '###', '#.#', '###'], '7': ['###', '..#', '.#.', '.#.', '.#.'], '8': ['###', '#.#', '###', '#.#', '###'],
    '9': ['###', '#.#', '###', '..#', '##.'],
}
INK, EDGE = (255, 240, 208), (42, 26, 16)


def text_pixels(text, vertical=False):
    """The pixels of `text` in FONT (a gap of one between letters; `vertical`: one under another)."""
    on = set()
    x0 = 0
    for i, ch in enumerate(text):
        ox, oy = (0, i * 6) if vertical else (x0, 0)
        x0 += len(FONT[ch][0]) + 1
        for y, row in enumerate(FONT[ch]):
            for x, c in enumerate(row):
                if c == '#': on.add((ox + x, oy + y))
    return on


def stamp(img, w, h, on, cx, cy, ink, edge):
    """Draws pixels centred on (cx, cy), each with a one-pixel dark edge round it."""
    xs = [p[0] for p in on]; ys = [p[1] for p in on]
    ox, oy = int(round(cx - (max(xs) + 1) / 2)), int(round(cy - (max(ys) + 1) / 2))
    for (x, y) in on:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                X, Y = ox + x + dx, oy + y + dy
                if 0 <= X < w and 0 <= Y < h and (x + dx, y + dy) not in on and img[Y * w + X][3] == 0:
                    img[Y * w + X] = edge + (255,)
    for (x, y) in on:
        X, Y = ox + x, oy + y
        if 0 <= X < w and 0 <= Y < h: img[Y * w + X] = ink + (255,)


def tile_colours():
    ground = make_art.pixels_of(make_art.S['tile_ground'])
    out = {}
    for i, ch in enumerate(LEGEND):
        name = make_art.OVERWORLD_TILES[i]
        px = make_art.pixels_of(make_art.S['tile_' + name], ground)
        n = len(px)
        out[ch] = tuple(sum(p[k] for p in px) // n for k in range(3))
    return out


def screens():
    """name -> 11 rows of 16 tiles, and the warps on each screen."""
    rooms = {}
    for part in open(os.path.join(GAME, 'tools', 'world', 'overworld.txt')).read().split('\nroom ')[1:]:
        lines = part.split('\n')
        name = lines[0].strip()
        i = lines.index('map')
        rooms[name] = (lines[i + 1:i + 12], [l for l in lines if l.startswith('put warp')])
    return rooms


def mix(a, b, k):
    return tuple(int(a[i] * (1 - k) + b[i] * k) for i in range(3))


def main():
    colours = tile_colours()
    rooms = screens()
    W, H = 16 * 16, 8 * 11
    big = [(0, 0, 0, 255)] * (W * H)
    for name, (rows, _) in rooms.items():
        x, y = (int(v) for v in name.split('_')[1:])
        region = design_overworld.reg(x, y)
        tint = ROAD_TINT if (x, y) in design_overworld.ROAD else REGION_TINT[region]
        for r in range(11):
            for c in range(16):
                ch = rows[r][c]
                col = colours[ch]
                if ch in FLOOR:
                    col = mix(col, tint, TINT)
                big[(y * 11 + r) * W + x * 16 + c] = col + (255,)
    make_art.write_bmp(os.path.join(GAME, 'art', 'world_map.bmp'), W, H, big)
    # the minimap: 4 x 4 a screen, each pixel the average of its block of tiles, leaning
    # further to the region's colour (so it reads that small)
    mini = []
    for py in range(32):
        for px in range(64):
            sx, sy = px // 4, py // 4
            c0, c1 = (px % 4) * 4, (px % 4) * 4 + 4
            r0, r1 = int((py % 4) * 11 / 4), int(((py % 4) + 1) * 11 / 4)
            acc, n = [0, 0, 0], 0
            for r in range(r0, max(r1, r0 + 1)):
                for c in range(c0, c1):
                    p = big[(sy * 11 + r) * W + sx * 16 + c]
                    for k in range(3): acc[k] += p[k]
                    n += 1
            tint = ROAD_TINT if (sx, sy) in design_overworld.ROAD else REGION_TINT[design_overworld.reg(sx, sy)]
            avg = tuple(v // n for v in acc)
            water = avg[2] > avg[0] + 40                       # (water stays blue)
            mini.append((avg if water else mix(avg, tint, 0.45)) + (255,))
    make_art.write_bmp(os.path.join(GAME, 'art', 'minimap.bmp'), 64, 32, mini)
    # the names: a see-through picture over the map (and over its fog)
    names = [(0, 0, 0, 0)] * (W * H)
    for (text, sx, sy) in LABELS:
        stamp(names, W, H, text_pixels(text, vertical=(text == 'COAST')), sx * 16, sy * 11, INK, EDGE)
    make_art.write_bmp(os.path.join(GAME, 'art', 'map_names.bmp'), W, H, names)
    # each cellar's number, red (not done yet) and gold (cleared): 5 x 7, in the same tiny font
    for n in range(1, 10):
        for kind, ink in (('red', (255, 112, 96)), ('gold', (255, 216, 74))):
            pic = [(0, 0, 0, 0)] * (5 * 7)
            stamp(pic, 5, 7, text_pixels(str(n)), 2.5, 3.5, ink, EDGE)
            make_art.write_bmp(os.path.join(GAME, 'art', 'map_%d_%s.bmp' % (n, kind)), 5, 7, pic)
    # the cellar doors, in big-map pixels
    doors = []
    for name, (_, warps) in rooms.items():
        x, y = (int(v) for v in name.split('_')[1:])
        for w in warps:
            m = re.match(r'put warp ([\d.]+) ([\d.]+) to = "d(\d)_2_4"', w)
            if m:
                doors.append((int(m.group(3)), x * 16 + float(m.group(1)) + 0.5, y * 11 + float(m.group(2)) + 0.5, x, y))
    doors.sort()
    out = ['# Where things go on the map of the land (art/world_map.bmp: a pixel a tile, 256 x 88).',
           '# Made by tools/make_map.py - edit it there.', '',
           '# Each cellar\'s door: [number, x, y, "x_y" of its screen] (x and y in map pixels).', 'func doors() {', '    return [']
    for i, (n, mx, my, x, y) in enumerate(doors):
        out.append('        [%d, %g, %g, "%d_%d"]%s' % (n, mx, my, x, y, ',' if i < len(doors) - 1 else ''))
    out += ['    ]', '}', '']
    open(os.path.join(GAME, 'scripts', 'map_data.bs'), 'w').write('\n'.join(out))
    print('make_map: wrote art/world_map.bmp, map_names.bmp, minimap.bmp, map_<n>_<red|gold>.bmp and scripts/map_data.bs')


if __name__ == '__main__':
    main()
