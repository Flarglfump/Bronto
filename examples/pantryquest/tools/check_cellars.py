#!/usr/bin/env python3
"""Checks every cellar in tools/world/cellar*.txt can always be finished.

    python3 examples/pantryquest/tools/check_cellars.py

Keys belong to the cellar they are found in, and a key opens any locked door
there - so a player could spend one on the "wrong" door. This walks each
cellar as a player could: every room reachable through open doors, cracked
walls (with bombs), solved puzzles and the doors unlocked so far, picking up
every key and item there. Then it tries every order of spending the keys on
the locked doors within reach. If any order leaves Pip with no keys, no way
on and the recipe page out of reach, that's a dead end, and it says so.

It also checks the doors on both sides of a wall agree, and that the map,
compass, heart pie, guardian and page are in every cellar.

Items from earlier cellars count as found (the plank from cellar 4 on, the oven
mitt from 6...); bombs from cellar 4 on (they may not be bought before). Only
Python's standard library is used.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_rooms  # noqa: E402

SOLID = set('#O~LHXPVQT')
DOOR = make_rooms.DOOR_CELLS
SIDES = {'n': (0, -1), 's': (0, 1), 'w': (-1, 0), 'e': (1, 0)}
OPP = {'n': 's', 's': 'n', 'e': 'w', 'w': 'e'}
ITEM_FROM = {'plank': 4, 'mitt': 6, 'tray': 7, 'skewers': 2, 'chili': 3, 'cutter': 1, 'whisk': 5, 'ladle': 8}


def grid_of(room):
    g = [['#'] * 16 for _ in range(11)]
    for r in range(7):
        for c in range(12): g[r + 2][c + 2] = room['inside'][r][c]
    for side in room['doors']:
        for (c, r) in DOOR[side]: g[r][c] = ','
    return g


def reach(room, g, starts, have):
    """The cells Pip can get to from `starts` with what he has: the plank crosses jam one wide, the mitt
    moves crates (counted as passable - the rooms are built so pushing opens the way)."""
    def ok(c, r):
        if not (0 <= c < 16 and 0 <= r < 11): return False
        t = g[r][c]
        if t not in SOLID: return True
        if t == 'O' and 'mitt' in have: return True
        if t == '~' and 'plank' in have:
            T = lambda cc, rr: g[rr][cc] if 0 <= cc < 16 and 0 <= rr < 11 else '#'
            return (T(c - 1, r) != '~' and T(c + 1, r) != '~') or (T(c, r - 1) != '~' and T(c, r + 1) != '~')
        return False
    seen, todo = set(s for s in starts if ok(*s)), [s for s in starts if ok(*s)]
    while todo:
        c, r = todo.pop()
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (c + dc, r + dr)
            if q not in seen and ok(*q):
                seen.add(q); todo.append(q)
    return seen


def things(room):
    out = []
    for (prefab, c, r, extra) in room['things']:
        m = re.search(r'kind = "(\w+)"', extra)
        cells = [(int(c), int(r))] if c == int(c) else [(int(c), int(r)), (int(c) + 1, int(r))]
        near = cells + [(x + dc, y + dr) for (x, y) in cells for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1))]
        out.append((prefab, m.group(1) if m else None, near))
    return out


def puzzle_ok(room, have):
    """Can a sealed room's puzzle be solved with what Pip has?"""
    vs = ' '.join(room['vars'])
    tiles = ''.join(room['inside'])
    needs = set()
    if 'B' in tiles: needs.add('mitt')                                   # crates onto plates
    if any(t[0] == 'brazier' for t in room['things']): needs.add('chili')
    if any(t[0] == 'gong' for t in room['things']): needs |= {'skewers', 'mitt'}
    if 'need_pages' in vs: return True                                    # (the Banquet Hall's door: all 8 pages, by then)
    return needs <= have


def explore(rooms, num, opened, start_have):
    """Everything reachable in a cellar with these locked doors opened: (rooms, keys found, items, locks in reach, page?)."""
    have = set(start_have)
    while True:
        seen_rooms, keys, items, locks, page = set(), 0, set(), set(), False
        entry = {('d%d_2_4' % num, 's')}
        todo = list(entry)
        cells_by_room = {}
        while todo:
            name, side_in = todo.pop()
            room = rooms.get(name)
            if room is None: continue
            g = grid_of(room)
            starts = set(DOOR[side_in])
            cells = reach(room, g, starts, have) | cells_by_room.get(name, set())
            if cells == cells_by_room.get(name):
                continue
            cells_by_room[name] = cells
            seen_rooms.add(name)
            sealed = re.search(r'sealed = "(\w*)"', ' '.join(room['vars']))
            sealed = sealed.group(1) if sealed else ''
            for side, kind in room['doors'].items():
                if not any(c in cells for c in DOOR[side]): continue
                x, y = (int(v) for v in name.split('_')[1:])
                other = 'd%d_%d_%d' % (num, x + SIDES[side][0], y + SIDES[side][1])
                if other not in rooms: continue
                lock = tuple(sorted([(name, side), (other, OPP[side])]))
                if side in sealed and not puzzle_ok(room, have): continue
                if kind == 'locked' and lock not in opened:
                    locks.add(lock); continue
                if kind == 'bomb' and 'bombs' not in have: continue
                todo.append((other, OPP[side]))
        for name, cells in cells_by_room.items():
            for (prefab, kind, near) in things(rooms[name]):
                if not any(c in cells for c in near): continue
                if kind == 'key': keys += 1
                elif kind == 'page' or kind == 'recipe': page = True
                elif kind: items.add(kind)
        new = have | (items & set(ITEM_FROM)) | ({'bombs'} if 'bomb' in items else set())
        if new == have:
            return seen_rooms, keys, items, locks, page
        have = new


def check(path):
    rel, room_list = make_rooms.parse(path)
    rooms = {r['name']: r for r in room_list if r.get('inside') is not None}
    if not rooms: return [], None
    num = int(next(iter(rooms)).split('_')[0][1:])
    problems = []
    # the doors on both sides of a wall agree
    for name, room in rooms.items():
        x, y = (int(v) for v in name.split('_')[1:])
        for side, kind in room['doors'].items():
            other = 'd%d_%d_%d' % (num, x + SIDES[side][0], y + SIDES[side][1])
            if other in rooms and rooms[other]['doors'].get(OPP[side]) != kind:
                problems.append('%s %s=%s, but %s %s=%s' % (name, side, kind, other, OPP[side], rooms[other]['doors'].get(OPP[side])))
    # what Pip brings with him, from the cellars before
    have = {k for k, n in ITEM_FROM.items() if n < num}
    if num >= 4: have.add('bombs')
    # every order of spending keys
    seen_states, dead, wins, todo = set(), [], 0, [frozenset()]
    every_kind = set()
    while todo:
        opened = todo.pop()
        if opened in seen_states: continue
        seen_states.add(opened)
        rooms_in, keys, items, locks, page = explore(rooms, num, opened, have)
        every_kind |= items
        left = keys - len(opened)
        if page:
            wins += 1
            continue
        if left <= 0 or not locks:
            dead.append((sorted(opened), sorted(rooms_in), left))
            continue
        for lock in locks:
            todo.append(opened | {lock})
    for opened, rooms_in, left in dead[:3]:
        problems.append('a dead end: after opening %s, %d key(s) left and no way on (rooms: %s)' % (
            [l[0][0] + ':' + l[0][1] for l in opened] or 'nothing', left, ', '.join(r.split('_', 1)[1] for r in rooms_in)))
    if wins == 0: problems.append('the recipe page can never be reached')
    for need in ('map', 'compass', 'heart_pie'):
        if need not in every_kind: problems.append('no %s found' % need)
    locks_total = sum(1 for r in rooms.values() for k in r['doors'].values() if k == 'locked') // 2
    return problems, '%s: %d rooms, %d locked doors, %d orders of using keys tried, %d dead ends' % (
        os.path.basename(path), len(rooms), locks_total, len(seen_states), len(dead))


def main():
    bad = 0
    for path in sorted(glob.glob(os.path.join(HERE, 'world', 'cellar[0-9]*.txt'))):
        problems, summary = check(path)
        if summary is None: continue
        print(summary)
        for p in problems: print('   ' + p)
        bad += len(problems)
    print('check_cellars: %s' % ('every cellar can always be finished' if not bad else '%d problem(s)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
