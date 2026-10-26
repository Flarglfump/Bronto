"""Getting about the world: which rooms lead to which, through which doors, with what Basil
has. It plans with check_world.py's model of each room (played at full jump height), and
the bot then plays each room's part with its own exact planner (nav.py)."""
import os
import sys
from collections import deque

TOOLS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, TOOLS)
_argv = sys.argv
sys.argv = [_argv[0], '--exact']
import world                     # noqa: E402
import check_world as cw         # noqa: E402
sys.argv = _argv

ROOMS = {r.id: r for r in world.build()}
for _r in ROOMS.values():
    if not hasattr(_r, 'garlic_gates'):
        _r.garlic_gates = set()
AT = {}
for _r in ROOMS.values():
    for _c in _r.cells():
        AT[_c] = _r
MOVING = {'dough', 'popcorn', 'sauce', 'frost', 'leaven', 'garlic', 'whisk'}


def neighbour(room, side, index):
    if side == 'W':
        cell = (room.x - 1, room.y + index)
        o = AT.get(cell)
        return (o, 'E', cell[1] - o.y) if o else None
    if side == 'E':
        cell = (room.x + room.w, room.y + index)
        o = AT.get(cell)
        return (o, 'W', cell[1] - o.y) if o else None
    if side == 'N':
        cell = (room.x + index, room.y - 1)
        o = AT.get(cell)
        return (o, 'S', cell[0] - o.x) if o else None
    cell = (room.x + index, room.y + room.h)
    o = AT.get(cell)
    return (o, 'N', cell[0] - o.x) if o else None


def door_kind(room, side, index):
    for s, i, k in room.doors:
        if s == side and i == index:
            return k
    return None


def things(room_id, kind=None):
    r = ROOMS[room_id]
    return [t for t in r.things if kind is None or t.kind == kind]


class WorldNav:
    def __init__(self):
        self.graphs = {}

    def graph(self, room_id, ab):
        key = (room_id, frozenset(a for a in ab if a in MOVING or a.startswith('beaten_')))
        g = self.graphs.get(key)
        if g is None:
            g = self.graphs[key] = cw.RoomGraph(ROOMS[room_id], set(key[1]))
        return g

    def reach(self, room_id, starts, ab):
        """Every spot reachable in a room from some starting spots, and the exits reached."""
        g = self.graph(room_id, ab)
        seen = set(starts)
        todo = list(starts)
        exits = {}
        while todo:
            n = todo.pop()
            for m in g.moves(n):
                if m[0] == 'node':
                    k = (m[1], m[2], m[3])
                    if k not in seen:
                        seen.add(k)
                        todo.append(k)
                else:
                    _, side, cx, cy, vx, vy, mb = m
                    idx = cw.door_index(ROOMS[room_id], side, cx, cy)
                    exits.setdefault((side, idx), (cx, cy, vy, mb))
        return seen, exits

    def passable(self, room, side, idx, ab, flags):
        kind = door_kind(room, side, idx)
        if kind is None:
            return None
        if kind == 'wax' and 'sauce' not in ab and ('wax:%s%s%d' % (room.id, side, idx)) not in flags:
            return None
        if kind == 'gate' and len([s for s in ab if s.startswith('seal_')]) < 4:
            return None
        if room.boss and ('beaten_' + room.boss) not in ab:
            return None
        nb = neighbour(room, side, idx)
        if not nb:
            return None
        other, oside, oidx = nb
        ok = door_kind(other, oside, oidx)
        if ok is None:
            return None
        if ok == 'wax' and 'sauce' not in ab and ('wax:%s%s%d' % (other.id, oside, oidx)) not in flags:
            return None
        if other.heat and 'mitts' not in ab:
            return None
        return nb

    def route(self, room_id, start_node, goal, ab, flags=()):
        """Rooms and doors from here to where goal(room_id, spots) says yes: a list of
        (room, side, index) exits to take (empty if the goal is in this room). None if there's no way."""
        ab = set(ab)
        start = (room_id, ('at', start_node))
        prev = {start: None}
        q = deque([start])
        while q:
            st = q.popleft()
            rid, how = st
            if how[0] == 'at':
                starts = {how[1]}
            else:
                _, side, idx, vy, mb = how
                starts = set()
                for a in self.graph(rid, ab).arrive(side, idx, None, vy, mb):
                    if a[0] == 'node':
                        starts.add((a[1], a[2], a[3]))
            if not starts:
                continue
            spots, exits = self.reach(rid, starts, ab)
            if goal(rid, spots):
                out = []
                while prev[st] is not None:
                    p, ex = prev[st]
                    out.append((p[0],) + ex)
                    st = p
                return out[::-1]
            room = ROOMS[rid]
            for (side, idx), (cx, cy, vy, mb) in exits.items():
                nb = self.passable(room, side, idx, ab, flags)
                if not nb:
                    continue
                other, oside, oidx = nb
                nst = (other.id, ('door', oside, oidx, round(vy), mb))
                if nst not in prev:
                    prev[nst] = (st, (side, idx))
                    q.append(nst)
        return None


def near_tile(tx, ty):
    """A goal: a spot within reach of a tile (as check_world counts picking an item up)."""
    def ok(rid, spots, tx=tx, ty=ty):
        return any(abs(c - tx) <= 1 and (r - ty) in (0, 1, -1) for (c, r, b) in spots)
    return ok
