#!/usr/bin/env python3
"""Works out, for the map, which parts of each room need something Basil may not have yet:
a door or block that needs hot sauce, popcorn or garlic, the castle gate (four seals), a
hot room (oven mitts), and the places only some ability gets him to - a crawlspace (the
Dough Wrap), a high ledge (the Leaven Boots or the Whisk Spin), a frozen step (Frost
Sugar). The last kind it finds by playing each room (check_world.py's movement) with the
abilities in the order they're found: whatever first comes in reach with one needs it.

    python3 examples/pantryquest2/tools/make_locks.py

It writes tools/locks.json - [room, map x, map y, what's needed] - which make_rooms.py
puts in scripts/world_data.bs (locks()). It takes a minute or two: run it when the world
changes, before make_rooms.py.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if '--exact' not in sys.argv:
    sys.argv.append('--exact')                  # (the map shows what can be done, not what's comfortable)
import world                                            # noqa: E402
import check_world as cw                                # noqa: E402
from roomkit import SW, SH, DOOR_ROWS                    # noqa: E402

ABILITIES = ['dough', 'popcorn', 'sauce', 'frost', 'leaven', 'garlic', 'whisk']   # (the order they're found)
BLOCKS = {'x': 'popcorn', 'r': 'sauce'}


def cell_of(room, col, row):
    return room.x + min(col // SW, room.w - 1), room.y + min(row // SH, room.h - 1)


def door_cell(room, side, index):
    if side in 'WE':
        return (room.x if side == 'W' else room.x + room.w - 1), room.y + index
    return room.x + index, (room.y if side == 'N' else room.y + room.h - 1)


def spots(room, ab):
    """Every spot (and door) Basil can reach in a room with these abilities, from its doors."""
    g = cw.RoomGraph(room, set(ab))
    starts = set()
    for side, index, kind in room.doors:
        if side in 'WE':
            col = 1 if side == 'W' else room.W - 2
            row = index * SH + DOOR_ROWS[1] + 1
            for ball in (False, True):
                if g.stands(col, row, ball):
                    starts.add((col, row, ball))
        else:
            for a in g.arrive(side, index, (0, 0), -5.0, False):
                if a[0] == 'node':
                    starts.add((a[1], a[2], a[3]))
    seen = set(starts)
    exits = set()
    todo = list(starts)
    while todo:
        n = todo.pop()
        for m in g.moves(n):
            if m[0] == 'node':
                k = (m[1], m[2], m[3])
                if k not in seen:
                    seen.add(k)
                    todo.append(k)
            else:
                exits.add((m[1], cw.door_index(room, m[1], m[2], m[3])))
    return set((c, r) for c, r, b in seen), exits


def main():
    rooms = world.build()
    out = set()
    for room in rooms:
        if not hasattr(room, 'garlic_gates'):
            room.garlic_gates = set()
        # doors and blocks
        for side, index, kind in room.doors:
            if kind == 'wax':
                out.add((room.id,) + door_cell(room, side, index) + ('sauce',))
            if kind == 'gate':
                out.add((room.id,) + door_cell(room, side, index) + ('seals',))
        for y, row in enumerate(room.t):
            for x, ch in enumerate(row):
                need = BLOCKS.get(ch)
                if ch == 'g' and (x, y) in room.garlic_gates:
                    need = 'garlic'
                if need:
                    out.add((room.id,) + cell_of(room, x, y) + (need,))
        if room.heat:
            for (cx, cy) in room.cells():
                out.add((room.id, cx, cy, 'mitts'))
        # what each ability opens up, in the order they're found: a place first reached once
        # Basil has the Leaven Boots needs them (or something found after them)
        bosses = set('beaten_' + b for b in world.BOSS_NEEDS)
        have = set()
        before, before_exits = spots(room, have | bosses)
        for a in ABILITIES:
            have.add(a)
            now, now_exits = spots(room, have | bosses)
            for (c, r) in now - before:
                out.add((room.id,) + cell_of(room, c, r) + (a,))
            for (side, index) in now_exits - before_exits:
                out.add((room.id,) + door_cell(room, side, index) + (a,))
            before, before_exits = before | now, before_exits | now_exits
        print('  %s: %d' % (room.id, len([o for o in out if o[0] == room.id])), flush=True)
    data = sorted([list(o) for o in out])
    with open(os.path.join(HERE, 'locks.json'), 'w') as f:
        json.dump(data, f, indent=0)
    print('wrote %d locks to tools/locks.json' % len(data))


if __name__ == '__main__':
    main()
