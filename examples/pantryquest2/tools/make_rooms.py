#!/usr/bin/env python3
"""Turns the rooms in world.py into scenes (rooms/<id>.scene), and writes the world's
facts for the scripts (scripts/world_data.bs): every room's place on the grid, its doors,
its area and name, for the map, the minimap and the doors.

    python3 examples/pantryquest2/tools/make_rooms.py

Open any room in the editor to look at it; but change it in world.py, which is where it
is made (this writes over the scenes).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import art_tiles                                    # noqa: E402
import roomkit                                      # noqa: E402
from roomkit import SW, SH, TILE, DOOR_ROWS, SHAFT_COLS, AREAS   # noqa: E402
import world                                        # noqa: E402
import things                                       # noqa: E402

ROOT = os.path.join(HERE, '..')


def q(s):
    return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"') + '"'


def vars_text(d):
    parts = []
    for k, v in d.items():
        if isinstance(v, bool):
            parts.append('%s = %s' % (k, 'true' if v else 'false'))
        elif isinstance(v, (int, float)):
            parts.append('%s = %s' % (k, v))
        elif isinstance(v, (list, tuple)) and len(v) == 2 and all(isinstance(n, (int, float)) for n in v) and k.endswith('_at'):
            parts.append('%s = vec2(%s, %s)' % (k, v[0], v[1]))
        elif isinstance(v, (list, tuple)):
            parts.append('%s = [%s]' % (k, ', '.join(q(i) if isinstance(i, str) else str(i) for i in v)))
        else:
            parts.append('%s = %s' % (k, q(v)))
    return ', '.join(parts)


class Scene:
    def __init__(self, title):
        self.lines = ['# ' + title, 'bronto_scene 1', '']
        self.count = 0

    def entity(self, comps, parent=0):
        self.lines.append('entity')
        for name, fields in comps:
            self.lines.append('  ' + name)
            for k, v in fields:
                self.lines.append('    %s = %s' % (k, v))
        if parent is not None and self.count > 0:
            self.lines.append('  Parent')
            self.lines.append('    entity = %d' % parent)
        self.lines.append('')
        self.count += 1
        return self.count - 1

    def text(self):
        return '\n'.join(self.lines)


def background(room):
    """The background map: the area's wall, with windows, torches and the like."""
    style = AREAS[room.area][1]
    W, H = room.W, room.H
    bg = [['#' for _ in range(W)] for _ in range(H)]
    seed = sum(ord(c) for c in room.id)
    for y in range(H):
        for x in range(W):
            n = (x * 73 + y * 151 + seed * 17) % 97
            bg[y][x] = '$' if n < 8 else ('%' if n < 12 else '#')
    if room.area == 'village':                            # the night sky over the rooftops
        sky_rows = H - 6 if room.h == 1 else H * 2 // 3
        for y in range(H):
            for x in range(W):
                if y < sky_rows - 3:
                    bg[y][x] = '1' if (x * 31 + y * 17 + seed) % 11 == 0 else '0'
                elif y < sky_rows - 2:
                    bg[y][x] = '@'
                elif y < sky_rows:
                    bg[y][x] = '9'
                else:
                    bg[y][x] = '7' if (x // 4) % 3 else '8'
                    if y == sky_rows:
                        bg[y][x] = '6'
        if room.id.endswith('0') or 'moon' in room.notes:
            for (dx, dy), ch in zip(((0, 0), (1, 0), (0, 1), (1, 1)), '2345'):
                bg[2 + dy][W - 5 + dx] = ch
    else:
        # windows and torches on the walls, where there is room in front of them
        for y in range(3, H - 3, 4):
            for x in range(3 + (y // 4) % 3, W - 3, 7):
                if room.t[y][x] == '.' and room.t[y + 1][x] == '.':
                    k = (x + y * 3 + seed) % 6
                    if k == 0:
                        bg[y][x] = '*'
                        bg[y + 1][x] = '*' if False else bg[y + 1][x]
                    elif k == 1:
                        bg[y][x] = '+'
                    elif k == 2 and room.area in ('catacombs', 'castle'):
                        bg[y][x] = '>' if room.area == 'catacombs' else ']'
                    elif k == 3 and room.area in ('castle', 'catacombs'):
                        bg[y][x] = '!'
                    elif k == 4 and room.area in ('frost', 'brine', 'molten'):
                        bg[y][x] = '{' if room.area != 'molten' else '}'
    for (x, y), ch in room.bg_extra.items():
        if 0 <= x < W and 0 <= y < H:
            bg[y][x] = ch
    return [''.join(r) for r in bg]


def tilemap_fields(tileset, room, tiles, solid, layer):
    return [('tileset', q(tileset)), ('tile_size', TILE), ('columns', room.W), ('rows', room.H),
            ('legend', q(art_tiles.LEGEND)), ('solid', q(solid)), ('tiles', q(''.join(tiles))), ('layer', layer)]


def door_link(room, side, index, rooms_at):
    """The room through a door, and which of its doors you come in by."""
    if side == 'W':
        cell = (room.x - 1, room.y + index)
        other = rooms_at.get(cell)
        return other, ('E', cell[1] - other.y) if other else None
    if side == 'E':
        cell = (room.x + room.w, room.y + index)
        other = rooms_at.get(cell)
        return other, ('W', cell[1] - other.y) if other else None
    if side == 'N':
        cell = (room.x + index, room.y - 1)
        other = rooms_at.get(cell)
        return other, ('S', cell[0] - other.x) if other else None
    cell = (room.x + index, room.y + room.h)
    other = rooms_at.get(cell)
    return other, ('N', cell[0] - other.x) if other else None


def write_room(room, rooms_at):
    style = AREAS[room.area][1]
    tileset = 'art/tiles_%s.bmp' % style
    sc = Scene('Pantry Quest II - room %s, %s (made by tools/make_rooms.py from tools/world.py: change it there)' % (room.id, room.name))
    music = room.music or AREAS[room.area][2]
    rv = dict(id=room.id, area=room.area, w=room.w, h=room.h, music=music)
    if room.heat:
        rv['heat'] = True
    if room.boss:
        rv['boss'] = room.boss
    sc.entity([('Name', [('text', q('Room'))]), ('Transform', [('position', '0 0')]),
               ('Script', [('file', q('scripts/room.bs')), ('vars', q(vars_text(rv)))]), ('Tags', [('list', q('room'))])], None)
    centre = '%d %d' % (room.W * TILE // 2, room.H * TILE // 2)
    sc.entity([('Name', [('text', q('Back'))]), ('Transform', [('position', centre)]),
               ('Tilemap', tilemap_fields(tileset, room, background(room), '', -30))])
    tiles = room.autotiled()
    sc.entity([('Name', [('text', q('Map'))]), ('Transform', [('position', centre)]),
               ('Tilemap', tilemap_fields(tileset, room, tiles, art_tiles.SOLID, -10))])
    front = [''.join(c if c != ' ' else '.' for c in row) for row in room.front]
    if any(ch != '.' for row in front for ch in row):
        sc.entity([('Name', [('text', q('Front'))]), ('Transform', [('position', centre)]),
                   ('Tilemap', tilemap_fields(tileset, room, front, '', 20))])
    for side, index, kind in room.doors:
        other, arrive = door_link(room, side, index, rooms_at)
        if other is None:
            raise SystemExit('room %s: its %s door %d leads nowhere' % (room.id, side, index))
        if not any(d[0] == arrive[0] and d[1] == arrive[1] for d in other.doors):
            raise SystemExit('room %s: its %s door %d leads to %s, which has no %s door %d' % (room.id, side, index, other.id, arrive[0], arrive[1]))
        things.door(sc, room, side, index, kind, other.id, '%s%d' % arrive, style)
    for th in room.things:
        things.place(sc, room, th, style)
    path = os.path.join(ROOT, 'rooms', room.id + '.scene')
    with open(path, 'w') as f:
        f.write(sc.text())


def bs_str(s):
    return '"' + s + '"'


def write_world_data(rooms):
    """scripts/world_data.bs: the rooms for the map and the doors."""
    lines = ['# Pantry Quest II - the world\'s rooms: where each sits on the map, its size, area and name.',
             '# Made by tools/make_rooms.py from tools/world.py: change it there.', '@strict', '',
             'func rooms() -> list {',
             '    # [id, x, y, w, h, area, name, doors ("W0", "E1", ...), boss room]',
             '    return [']
    for r in rooms:
        doors = ['%s%d%s' % (s, i, ':' + k if k != 'blue' else '') for s, i, k in r.doors]
        lines.append('        [%s, %d, %d, %d, %d, %s, %s, [%s], %s],' % (
            bs_str(r.id), r.x, r.y, r.w, r.h, bs_str(r.area), bs_str(r.name),
            ', '.join(bs_str(d) for d in doors), 'true' if r.boss else 'false'))
    lines[-1] = lines[-1].rstrip(',')
    lines += ['    ]', '}', '',
              'func areas() -> list {',
              '    # [area, title, map colour]',
              '    return [']
    for k, (title, style, music, col) in AREAS.items():
        lines.append('        [%s, %s, rgb(%d, %d, %d)],' % (bs_str(k), bs_str(title), col[0], col[1], col[2]))
    lines[-1] = lines[-1].rstrip(',')
    lines += ['    ]', '}', '']
    lines += ['func things() -> list {',
              '    # what the map marks: [room, kind, x, y] (kind: item, hearth, map, boss) - x, y in tiles',
              '    return [']
    marks = []
    for r in rooms:
        for th in r.things:
            k = th.kind
            if k in ('item', 'hearth', 'maptable'):
                marks.append('        [%s, %s, %d, %d, %s],' % (bs_str(r.id), bs_str(k), th.x, th.y, bs_str(th.vars.get('iid', ''))))
    if marks:
        marks[-1] = marks[-1].rstrip(',')
    lines += marks + ['    ]', '}', '']
    with open(os.path.join(ROOT, 'scripts', 'world_data.bs'), 'w') as f:
        f.write('\n'.join(lines))


def main():
    rooms = world.build()
    rooms_at = {}
    for r in rooms:
        for c in r.cells():
            if c in rooms_at:
                raise SystemExit('rooms %s and %s overlap at %s' % (rooms_at[c].id, r.id, c))
            rooms_at[c] = r
    for f in os.listdir(os.path.join(ROOT, 'rooms')):
        if f.endswith('.scene'):
            os.remove(os.path.join(ROOT, 'rooms', f))
    for r in rooms:
        write_room(r, rooms_at)
    write_world_data(rooms)
    print('wrote %d rooms (%d screens)' % (len(rooms), sum(r.w * r.h for r in rooms)))


if __name__ == '__main__':
    main()
