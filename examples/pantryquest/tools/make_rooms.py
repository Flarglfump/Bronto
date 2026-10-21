#!/usr/bin/env python3
"""Builds Pantry Quest's rooms (rooms/*.scene) from the maps in tools/world/*.txt.

    python3 examples/pantryquest/tools/make_rooms.py

Each room becomes an ordinary scene file, the kind the editor makes and
opens: a root entity "Room" (scripts/room.bs) with a Tilemap child "Map"
and a child for every enemy, item and doorway, copied from prefabs/. The
game spawns a room under the HUD as Pip walks into it. Edit the maps here
(rather than the generated scenes) and run this again; it uses only
Python's standard library.
"""
import glob
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, '..'))

TILESETS = {
    'overworld': dict(image='art/tiles_overworld.bmp', legend='.,TrW~bCs=fIFKX:YpPLGSg', solid='TrW~bIKXYPLGSg'),
    'dungeon': dict(image='art/tiles_dungeon.bmp', legend='.#O~SLHXPV,', solid='#O~LHXPV'),
}
COLS, ROWS = 16, 11
DOOR_CELLS = {'n': [(7, 0), (8, 0), (7, 1), (8, 1)], 's': [(7, 9), (8, 9), (7, 10), (8, 10)],
              'w': [(0, 5), (1, 5)], 'e': [(14, 5), (15, 5)]}
DOOR_TILE = {'open': ',', 'locked': 'L', 'bomb': 'X'}


def prefab_body(name):
    """A prefab's entity block, without the file's header."""
    path = os.path.join(GAME, 'prefabs', name + '.scene')
    with open(path) as f:
        lines = f.read().split('\n')
    body = [l for l in lines if l.strip() and not l.startswith('#') and not l.startswith('bronto_scene')]
    if sum(1 for l in body if l.strip() == 'entity') != 1:
        raise SystemExit('prefabs/%s.scene: a room thing must be one entity' % name)
    return body


def quote(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')


def thing(prefab, col, row, extra):
    """A prefab placed at a tile, its script vars extended with `extra`."""
    x, y = col * 16 + 8, row * 16 + 8
    out, has_vars, in_script = [], False, False
    for l in prefab_body(prefab):
        s = l.strip()
        if s.startswith('position ='):
            l = l[:len(l) - len(l.lstrip())] + 'position = %g %g' % (x, y)
        if s == 'Script':
            in_script = True
        elif not l.startswith('    ') and s != 'entity':
            in_script = False
        if in_script and s.startswith('vars =') and extra:
            m = re.match(r'(\s*vars = ")(.*)"$', l)
            old = m.group(2)
            l = m.group(1) + (old + ', ' if old else '') + quote(extra) + '"'
            has_vars = True
        out.append(l)
        if in_script and s.startswith('file =') and extra and not any(x.strip().startswith('vars =') for x in prefab_body(prefab)):
            out.append('    vars = "%s"' % quote(extra))
            has_vars = True
    out += ['  Parent', '    entity = 0']
    return out


def room_scene(room, source):
    name = room['name']
    kind = 'dungeon' if re.match(r'd\d+_', name) else 'overworld'
    ts = TILESETS[kind]
    if room.get('inside') is not None:                  # a cellar room: walls and doorways around the inside
        grid = [['#'] * COLS for _ in range(ROWS)]
        inside = room['inside']
        if len(inside) != 7 or any(len(r) != 12 for r in inside):
            raise SystemExit('%s: %s: the inside must be 7 rows of 12' % (source, name))
        for r in range(7):
            for c in range(12):
                grid[r + 2][c + 2] = inside[r][c]
        for side, what in room['doors'].items():
            for (c, r) in DOOR_CELLS[side]:
                grid[r][c] = DOOR_TILE[what]
        rows = [''.join(r) for r in grid]
    else:
        rows = room['map']
        if len(rows) != ROWS or any(len(r) != COLS for r in rows):
            raise SystemExit('%s: %s: the map must be %d rows of %d' % (source, name, ROWS, COLS))
    for r in rows:
        for ch in r:
            if ch not in ts['legend']:
                raise SystemExit('%s: %s: %r isn\'t a %s tile' % (source, name, ch, kind))
    room_vars = ['id = "%s"' % name, 'music = "%s"' % room.get('music', '')]
    if kind == 'dungeon':
        room_vars.append('stays_clear = true')
    room_vars += room['vars']
    out = ['# Pantry Quest - room %s (made by tools/make_rooms.py from tools/%s: edit it there)' % (name, source), '#']
    out += ['#   ' + r for r in rows]
    out += ['bronto_scene 1', '',
            'entity', '  Name', '    text = "Room"', '  Transform', '    position = 0 64',
            '  Script', '    file = "scripts/room.bs"', '    vars = "%s"' % quote(', '.join(room_vars)), '',
            'entity', '  Name', '    text = "Map"', '  Transform', '    position = 128 88',
            '  Tilemap', '    tileset = "%s"' % ts['image'], '    tile_size = 16', '    columns = %d' % COLS,
            '    rows = %d' % ROWS, '    legend = "%s"' % ts['legend'], '    solid = "%s"' % ts['solid'],
            '    tiles = "%s"' % ''.join(rows), '    layer = -10', '  Parent', '    entity = 0']
    for (prefab, col, row, extra) in room['things']:
        out += [''] + thing(prefab, col, row, extra)
    return '\n'.join(out) + '\n'


def parse(path):
    rel = os.path.relpath(path, HERE)
    rooms, cur, mode = [], None, None
    for n, line in enumerate(open(path).read().split('\n'), 1):
        s = line.strip()
        if mode in ('map', 'inside') and s and not s.startswith('#') and not re.match(r'(put|var|end)\b', s):
            cur[mode].append(s)
            continue
        if not s or s.startswith('#'):
            continue
        word, _, rest = s.partition(' ')
        if word == 'room':
            cur = dict(name=rest.strip(), map=[], inside=None, doors={}, vars=[], things=[])
            mode = None
        elif cur is None:
            raise SystemExit('%s:%d: %r outside a room' % (rel, n, s))
        elif word == 'music':
            cur['music'] = rest.strip()
        elif word == 'map':
            mode = 'map'
        elif word == 'inside':
            cur['inside'] = []
            mode = 'inside'
        elif word == 'doors':
            for part in rest.split():
                side, _, what = part.partition('=')
                if side not in DOOR_CELLS or what not in DOOR_TILE:
                    raise SystemExit('%s:%d: bad door %r' % (rel, n, part))
                cur['doors'][side] = what
        elif word == 'var':
            cur['vars'].append(rest.strip())
            mode = None
        elif word == 'put':
            m = re.match(r'(\w+)\s+([\d.]+)\s+([\d.]+)\s*(.*)$', rest)
            if not m:
                raise SystemExit('%s:%d: put <prefab> <column> <row> [vars]' % (rel, n))
            cur['things'].append((m.group(1), float(m.group(2)), float(m.group(3)), m.group(4).strip()))
            mode = None
        elif word == 'end':
            rooms.append(cur)
            cur, mode = None, None
        else:
            raise SystemExit('%s:%d: what is %r?' % (rel, n, word))
    return rel, rooms


def main():
    out_dir = os.path.join(GAME, 'rooms')
    os.makedirs(out_dir, exist_ok=True)
    made = 0
    for path in sorted(glob.glob(os.path.join(HERE, 'world', '*.txt'))):
        rel, rooms = parse(path)
        for room in rooms:
            with open(os.path.join(out_dir, room['name'] + '.scene'), 'w') as f:
                f.write(room_scene(room, rel))
            made += 1
    print('make_rooms: wrote %d rooms to %s' % (made, os.path.relpath(out_dir)))


if __name__ == '__main__':
    main()
