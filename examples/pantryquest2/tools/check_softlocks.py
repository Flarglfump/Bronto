#!/usr/bin/env python3
"""Looks for soft locks: places Basil can get to but not back from.

It plays the game in order as check_world.py does, and at each stage (each new set of
abilities) takes every spot Basil can reach. From each one he must be able to get back to
the start, perhaps after picking up what's there to be found. The only other way out that
counts is a guardian's room or a hazard, where a game over puts him back at a hearth. (The
pause menu's QUIT TO TITLE always works too, but it throws away whatever wasn't saved.)

    python3 examples/pantryquest2/tools/check_softlocks.py

Each spot found is printed with the stage, the room and the tile, so it can be fixed.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import world                                             # noqa: E402
import check_world as cw                                 # noqa: E402

MOVING = {'dough', 'popcorn', 'sauce', 'frost', 'leaven', 'garlic', 'whisk'}


class World:
    def __init__(self):
        self.rooms = world.build()
        self.by_id = {r.id: r for r in self.rooms}
        self.at = {}
        for r in self.rooms:
            for c in r.cells():
                self.at[c] = r
            if not hasattr(r, 'garlic_gates'):
                r.garlic_gates = set()
        self.graphs = {}

    def graph(self, room, ab):
        key = frozenset(a for a in ab if a in MOVING or a.startswith('beaten_'))
        k = (room.id, key)
        if k not in self.graphs:
            self.graphs[k] = cw.RoomGraph(room, set(key))
        return self.graphs[k]

    def neighbour(self, room, side, index):
        if side == 'W':
            cell = (room.x - 1, room.y + index)
            o = self.at.get(cell)
            return (o, 'E', cell[1] - o.y) if o else None
        if side == 'E':
            cell = (room.x + room.w, room.y + index)
            o = self.at.get(cell)
            return (o, 'W', cell[1] - o.y) if o else None
        if side == 'N':
            cell = (room.x + index, room.y - 1)
            o = self.at.get(cell)
            return (o, 'S', cell[0] - o.x) if o else None
        cell = (room.x + index, room.y + room.h)
        o = self.at.get(cell)
        return (o, 'N', cell[0] - o.x) if o else None

    @staticmethod
    def door_kind(room, side, index):
        for s, i, k in room.doors:
            if s == side and i == index:
                return k
        return None

    def step(self, node, ab, beaten):
        """Where Basil can go from a spot: (spots, died here?)."""
        rid, col, row, ball = node
        room = self.by_id[rid]
        rg = self.graph(room, ab)
        out, dies = set(), False
        for m in rg.moves((col, row, ball)):
            if m[0] == 'node':
                out.add((rid, m[1], m[2], m[3]))
                continue
            _, side, cx, cy, vx, vy, mb = m
            idx = cw.door_index(room, side, cx, cy)
            kind = self.door_kind(room, side, idx)
            if kind is None:
                continue
            if kind == 'wax' and 'sauce' not in ab:
                continue
            if kind == 'gate' and len([s for s in ab if s.startswith('seal_')]) < 4:
                continue
            if room.boss and room.boss not in beaten:
                continue
            nb = self.neighbour(room, side, idx)
            if not nb:
                continue
            other, oside, oidx = nb
            okind = self.door_kind(other, oside, oidx)
            if okind is None or (okind == 'wax' and 'sauce' not in ab):
                continue
            for a in self.graph(other, ab).arrive(oside, oidx, (cx, cy), vy, mb):
                if a[0] == 'node':
                    out.add((other.id, a[1], a[2], a[3]))
        # a game over gets him out: a guardian not yet beaten, a hot room without mitts, or a hazard he can reach
        if room.boss and room.boss not in beaten:
            dies = True
        if room.heat and 'mitts' not in ab:
            dies = True
        if self.reaches_hazard(rg, (col, row, ball)):
            dies = True
        return out, dies

    def reaches_hazard(self, rg, spot):
        col, row, ball = spot
        if not hasattr(rg, 'hazard_memo'):
            rg.hazard_memo = {}
        if spot in rg.hazard_memo:
            return rg.hazard_memo[spot]
        x, y = rg.spot_xy(col, row, ball)
        hit = False
        for dx in (-6, 0, 6):
            for plan in cw.plans(rg.ab, ball):
                r = cw.simulate(rg.g, x + dx, y, ball, plan, rg.ab)
                if r and r[0] == 'dead':
                    hit = True
                    break
            if hit:
                break
        rg.hazard_memo[spot] = hit
        return hit

    def explore(self, starts, ab, beaten):
        """Every spot reachable from starts: (spots, edges, spots where he can die)."""
        seen = set(starts)
        todo = list(starts)
        edges = {}
        deaths = set()
        while todo:
            n = todo.pop()
            nxt, dies = self.step(n, ab, beaten)
            edges[n] = nxt
            if dies:
                deaths.add(n)
            for m in nxt:
                if m not in seen:
                    seen.add(m)
                    todo.append(m)
        return seen, edges, deaths

    def items_in(self, spots, taken):
        """The items within reach of these spots (as check_world takes them): [(iid, kind)]."""
        by_room = {}
        for (rid, c, r, b) in spots:
            by_room.setdefault(rid, []).append((c, r))
        out = []
        for rid, cells in by_room.items():
            for th in self.by_id[rid].things:
                if th.kind != 'item' or th.vars['iid'] in taken:
                    continue
                if any(abs(c - th.x) <= 1 and (r - th.y) in (0, 1, -1) for c, r in cells):
                    out.append((th.vars['iid'], th.vars['kind']))
        return out

    def guardians_in(self, spots, ab, beaten):
        out = []
        for rid in set(s[0] for s in spots):
            room = self.by_id[rid]
            if room.boss and room.boss not in beaten and world.BOSS_NEEDS.get(room.boss, set()) <= ab:
                out.append(room.boss)
        return out


def back_to(edges, targets, nodes):
    """The spots in nodes from which some target can be reached."""
    rev = {}
    for a, bs in edges.items():
        for b in bs:
            rev.setdefault(b, set()).add(a)
    good = set(t for t in targets if t in nodes)
    todo = list(good)
    while todo:
        n = todo.pop()
        for p in rev.get(n, ()):
            if p not in good and p in nodes:
                good.add(p)
                todo.append(p)
    return good


def check():
    W = World()
    start = (world.START[0],) + tuple(world.START[1:])
    ab, beaten, taken = set(), set(), set()
    problems = {}
    stage = 0
    while True:
        stage += 1
        W.graphs.clear()                                  # (keeps memory down: rebuilt as needed)
        seen, edges, deaths = W.explore([start], ab, beaten)
        safe = back_to(edges, {start} | deaths, seen)
        stuck = seen - safe
        print('stage %d: %d spots, %d can\'t get back' % (stage, len(seen), len(stuck)), file=sys.stderr, flush=True)
        # a spot that can't get back may still be fine: what's to be found from it may get him out
        checked = set()
        for n in sorted(stuck):
            if n in checked:
                continue
            ab2, beaten2, taken2 = set(ab), set(beaten), set(taken)
            region, redges, rdeaths = W.explore([n], ab2, beaten2)
            while True:
                found = W.items_in(region, taken2)
                bosses = W.guardians_in(region, ab2, beaten2)
                if not found and not bosses:
                    break
                for iid, kind in found:
                    taken2.add(iid)
                    ab2.add(kind)
                for bname in bosses:
                    beaten2.add(bname)
                    ab2.add('beaten_' + bname)
                    ab2 |= set(world.BOSS_REWARDS.get(bname, []))
                region, redges, rdeaths = W.explore([n], ab2, beaten2)
            ok = start in region or bool(rdeaths)
            group = region & stuck
            checked |= group
            for k in [k for k in W.graphs if k[1] != frozenset(a for a in ab if a in MOVING or a.startswith('beaten_'))]:
                del W.graphs[k]
            if not ok:
                key = n[0]
                problems.setdefault(key, []).append((stage, sorted(ab), n, len(group)))
        # take everything now in reach, as check_world does
        new = False
        for iid, kind in W.items_in(seen, taken):
            taken.add(iid)
            if kind not in ab:
                ab.add(kind)
                new = True
            else:
                new = True
        for bname in W.guardians_in(seen, ab, beaten):
            beaten.add(bname)
            ab.add('beaten_' + bname)
            ab |= set(world.BOSS_REWARDS.get(bname, []))
            new = True
        if not new:
            break
    if not problems:
        print('no soft locks')
        return True
    for rid in sorted(problems):
        for stage, abl, n, size in problems[rid][:3]:
            print('%s (%s): stuck at tile %d,%d%s (stage %d, with %s; %d spots like it)' % (
                rid, W.by_id[rid].name, n[1], n[2], ' rolled up' if n[3] else '', stage, ', '.join(a for a in abl if not a.startswith('beaten_')) or 'nothing', size))
    print('SOFT LOCKS')
    return False


if __name__ == '__main__':
    sys.exit(0 if check() else 1)
