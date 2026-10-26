#!/usr/bin/env python3
"""Checks that Pantry Quest II can be finished: it plays Basil's own movement (the speeds,
gravity and box sizes of scripts/basil.bs) through every room with the abilities he could
have by then, and follows the order of the game - the items each newly reachable place
holds, the guardians he can beat - until the Count's throne is reached, or nothing more
can be. It reports anything that can't be reached, and rooms whose doors don't lead on.

    python3 examples/pantryquest2/tools/check_world.py [--verbose] [--exact]

Jumps are played 8 pixels lower than they really reach, so nothing needs a perfect jump
(--exact plays them at full height).

The movement is tried from every place Basil can stand: walking, falling, jumps of a
few kinds (straight up, running, turning late, short hops), rolling up, popcorn bounces,
Frost Sugar steps (the spots marked in world.py) and the Whisk Spin. It is close to the
game but cautious, so anything it says can be reached, can, with room to spare.
"""
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import world                                             # noqa: E402
from roomkit import SW, SH                               # noqa: E402

RUN, GRAVITY, JUMP, HIGH_JUMP, CUT, MAX_FALL = 1.5, 0.25, 5.4, 7.0, 2.0, 5.0


def apex(v):
    """How high a jump at speed v rises, in pixels (the game moves first, then slows by GRAVITY)."""
    h, x = 0.0, v
    while x > 0:
        h += x
        x -= GRAVITY
    return h


def with_margin(v, px):
    """The jump speed whose apex is px lower: the checker plays with these, so that every jump
    it needs leaves a player some room to spare (a 3-tile ledge for a 3.6-tile jump, say)."""
    lo, hi = 0.0, v
    for _ in range(40):
        mid = (lo + hi) / 2
        if apex(mid) < apex(v) - px:
            lo = mid
        else:
            hi = mid
    return lo


MARGIN = 0 if '--exact' in sys.argv else 8                # pixels of height every needed jump must spare
JUMP_C, HIGH_JUMP_C = with_margin(JUMP, MARGIN), with_margin(HIGH_JUMP, MARGIN)
STAND = (12, 26)          # box size; its bottom is the feet
BALL = (12, 12)
SOLID = set('#cxruhgY')   # (the room's own letters before autotiling)
VERBOSE = '--verbose' in sys.argv


class Grid:
    """A room's cells for one set of abilities: solid or not, deadly or not, liquid or not."""

    def __init__(self, room, ab):
        self.room = room
        self.W, self.H = room.W, room.H
        solid = []
        for y in range(self.H):
            row = []
            for x in range(self.W):
                ch = room.t[y][x]
                s = ch in SOLID
                if ch == 'c':
                    s = False                                # pepper breaks crumbs
                if ch == 'x' and 'popcorn' in ab:
                    s = False
                if ch == 'r' and 'sauce' in ab:
                    s = False
                if ch == 'g' and 'garlic' in ab and (x, y) in room.garlic_gates:
                    s = False
                if ch == 'g' and room.boss and ('beaten_' + room.boss) in ab and (x, y) in getattr(room, 'boss_gates', ()):
                    s = False
                row.append(s)
            solid.append(row)
        if 'frost' in ab:
            for (x, y) in room.freeze_spots:
                solid[y][x] = True
        self.solid = solid
        self.deadly = [[room.t[y][x] in '^v~' for x in range(self.W)] for y in range(self.H)]
        self.memo_stand, self.memo_ball, self.memo_deadly = {}, {}, {}
        self.liquid = [[room.t[y][x] == '=' for x in range(self.W)] for y in range(self.H)]

    def box_hits(self, cx, cy, w, h):
        """Does a box (centre, size; whole-pixel edges) overlap a solid cell? Outside the room
        is open only through the doors' openings (off the edge is a way out)."""
        x0, x1 = cx - w / 2, cx + w / 2
        y0, y1 = cy - h / 2, cy + h / 2
        c0, c1 = int(x0 // 16), int((x1 - 1e-6) // 16)
        r0, r1 = int(y0 // 16), int((y1 - 1e-6) // 16)
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                if r < 0 or r >= self.H or c < 0 or c >= self.W:
                    continue                                 # beyond the edge: the door's way out
                if self.solid[r][c]:
                    return True
        return False

    def deadly_box(self, cx, cy, w, h):
        k = (cx, cy, w, h)
        v = self.memo_deadly.get(k)
        if v is None:
            v = self.memo_deadly[k] = self._deadly_box(cx, cy, w, h)
        return v

    def _deadly_box(self, cx, cy, w, h):
        x0, x1 = cx - w / 2 + 2, cx + w / 2 - 2             # (as basil.bs checks: just inside his box)
        y0, y1 = cy - h / 2 + 1, cy + h / 2 - 1
        for r in range(int(y0 // 16), int((y1 - 1e-6) // 16) + 1):
            for c in range(int(x0 // 16), int((x1 - 1e-6) // 16) + 1):
                if 0 <= r < self.H and 0 <= c < self.W and self.deadly[r][c]:
                    return True
        return False

    def in_liquid(self, cx, cy):
        c, r = int(cx // 16), int(cy // 16)
        return 0 <= r < self.H and 0 <= c < self.W and self.liquid[r][c]


class Body:
    """Basil, moving a whole pixel at a time as basil.bs does."""

    def __init__(self, g, x, y, ball, vx=0.0, vy=0.0):
        self.g, self.x, self.y, self.ball = g, x, y, ball       # x, y: his sprite's middle (feet at y + 16)
        self.vx, self.vy = vx, vy
        self.sx = self.sy = 0.0

    def centre(self, x=None, y=None):
        x = self.x if x is None else x
        y = self.y if y is None else y
        return (x, y + 10) if self.ball else (x, y + 3)

    def size(self):
        return BALL if self.ball else STAND

    def blocked(self, x, y):
        memo = self.g.memo_ball if self.ball else self.g.memo_stand
        k = (x, y)
        v = memo.get(k)
        if v is None:
            cx, cy = self.centre(x, y)
            w, h = self.size()
            v = memo[k] = self.g.box_hits(cx, cy, w, h)
        return v

    def move_x(self, dx):
        self.sx += dx
        steps = round_half(self.sx)
        self.sx -= steps
        s = 1 if steps > 0 else -1
        for _ in range(abs(steps)):
            if self.blocked(self.x + s, self.y):
                self.sx = 0
                return False
            self.x += s
        return True

    def move_y(self, dy):
        self.sy += dy
        steps = round_half(self.sy)
        self.sy -= steps
        s = 1 if steps > 0 else -1
        for _ in range(abs(steps)):
            if self.blocked(self.x, self.y + s):
                self.sy = 0
                return False
            self.y += s
        return True

    def on_ground(self):
        return self.blocked(self.x, self.y + 1)


def round_half(v):
    """BrontoScript's round(): floor(v + 0.5)."""
    import math
    return int(math.floor(v + 0.5))


def simulate(g, x, y, ball, plan, ab, vy0=None, frames=150):
    """Runs a plan: plan(frame, body) -> (dir, jump_pressed, jump_held). Returns how it ended:
    ('land', col, row, ball) / ('exit', side, x, y, vx, vy) / ('dead',) / None."""
    b = Body(g, x, y, ball)
    grounded = vy0 is None and b.on_ground()
    if vy0 is not None:
        b.vy = vy0
    jumped = False
    left_ground = not grounded
    spin = False
    for f in range(frames):
        d, press, held = plan(f, b)
        liquid = g.in_liquid(*b.centre())
        speed = RUN * (0.6 if liquid else 1)
        b.vx = d * speed
        b.move_x(b.vx)
        grounded = b.vy >= 0 and b.on_ground()
        if grounded:
            if left_ground:
                return end_land(b)
            b.vy = 0
            if press and not b.ball and not jumped:
                v = HIGH_JUMP_C if 'leaven' in ab else JUMP_C
                if liquid:
                    v *= 0.75
                b.vy = -v
                jumped = True
                left_ground = True
                spin = d != 0 and 'whisk' in ab
        else:
            left_ground = True
            gr = GRAVITY * (0.5 if liquid else 1)
            top = MAX_FALL * (0.5 if liquid else 1)
            b.vy = min(b.vy + gr, top)
            if b.vy < -CUT and not held and not (vy0 is not None and not jumped):
                b.vy = -CUT
        if not b.move_y(b.vy):
            b.vy = 0.5 if b.vy < 0 else 0
        cx, cy = b.centre()
        w, h = b.size()
        if g.deadly_box(cx, cy, w, h):
            return ('dead',)
        side = None
        if cx < 0:
            side = 'W'
        elif cx > g.W * 16:
            side = 'E'
        elif cy < 0:
            side = 'N'
        elif cy > g.H * 16:
            side = 'S'
        if side:
            return ('exit', side, cx, cy, b.vx, b.vy, b.ball)
        if not left_ground and f > 2 and d == 0 and not press:
            return end_land(b)
    return None


def end_land(b):
    return ('land', int(b.x // 16), int((b.y + 16) // 16) - 1, b.ball, b.x)


def plans(ab, ball):
    """The ways of moving tried from each spot."""
    out = []
    if ball:
        for d in (-1, 1):
            out.append(lambda f, b, d=d: (d if f < 11 else 0, False, False))   # roll about a tile
        return out
    for d in (-1, 1):
        out.append(lambda f, b, d=d: (d if f < 11 else 0, False, False))       # walk about a tile (or off an edge)
        for start in (0, 6, 12, 18, 26):
            out.append(lambda f, b, d=d, s=start: (d if f >= s else 0, f == 0, True))   # jump, steering from frame s
        for stop in (8, 16, 24):
            out.append(lambda f, b, d=d, s=stop: (d if f < s else 0, f == 0, True))     # jump, steering until frame s
        out.append(lambda f, b, d=d: (d, f == 0, f < 6))                     # a short hop
        for hold in (10, 14, 18):                                              # jumps let go of early
            out.append(lambda f, b, d=d, h=hold: (d, f == 0, f < h))
            out.append(lambda f, b, d=d, h=hold: (d if f >= 8 else 0, f == 0, f < h))
        out.append(lambda f, b, d=d: (d if f < 30 else -d, f == 0, True))     # jump, then double back
    out.append(lambda f, b: (0, f == 0, True))                                  # straight up
    return out


class RoomGraph:
    """Every spot Basil can stand (or roll) in a room with given abilities, and the moves between them."""

    def __init__(self, room, ab):
        self.room = room
        self.ab = ab
        self.g = Grid(room, ab)
        self.cache = {}

    def spot_xy(self, col, row, ball):
        return col * 16 + 8, (row + 1) * 16 - 16

    def stands(self, col, row, ball):
        x, y = self.spot_xy(col, row, ball)
        b = Body(self.g, x, y, ball)
        return not b.blocked(x, y) and b.on_ground()

    def moves(self, node):
        if node in self.cache:
            return self.cache[node]
        col, row, ball = node
        x, y = self.spot_xy(col, row, ball)
        out = set()
        for dx in (-6, 0, 6):                                  # from the middle of the tile, and its edges
            sx = x + dx
            b = Body(self.g, sx, y, ball)
            if b.blocked(sx, y) or not b.on_ground():
                continue
            for plan in plans(self.ab, ball):
                r = simulate(self.g, sx, y, ball, plan, self.ab)
                if r:
                    out.add(self.result(r))
        if ball and 'popcorn' in self.ab:                       # popcorn under him: a bounce
            for d in (-1, 0, 1):
                r = simulate(self.g, x, y, True, lambda f, b, d=d: (d if f > 4 else 0, False, False), self.ab, vy0=-4.6)
                if r:
                    out.add(self.result(r))
        if 'dough' in self.ab:
            b = Body(self.g, x, y, not ball)
            if not b.blocked(x, y):
                out.add(('node', col, row, not ball))
        if 'whisk' in self.ab and not ball:
            out |= self.whisk_from(col, row)
        out.discard(None)
        self.cache[node] = out
        return out

    def whisk_from(self, col, row):
        """The Whisk Spin climbs anywhere open: every standing spot joined through open space."""
        if hasattr(self, '_whisk'):
            return self._whisk.get((col, row), set())
        g = self.g
        open_cell = [[False] * g.W for _ in range(g.H)]
        for r in range(g.H):
            for c in range(g.W):
                x, y = c * 16 + 8, r * 16 + 0                   # standing with feet at the bottom of cell r
                b = Body(g, x, y - 16 + 16, False)
                open_cell[r][c] = not b.blocked(x, (r + 1) * 16 - 16) and not g.deadly_box(x, (r + 1) * 16 - 13, 12, 26)
        comp = {}
        n = 0
        for r in range(g.H):
            for c in range(g.W):
                if open_cell[r][c] and (c, r) not in comp:
                    stack = [(c, r)]
                    comp[(c, r)] = n
                    while stack:
                        a, bb = stack.pop()
                        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                            na, nb = a + dc, bb + dr
                            if 0 <= na < g.W and 0 <= nb < g.H and open_cell[nb][na] and (na, nb) not in comp:
                                comp[(na, nb)] = n
                                stack.append((na, nb))
                    n += 1
        groups = {}
        for (c, r), k in comp.items():
            if self.stands(c, r, False):
                groups.setdefault(k, set()).add(('node', c, r, False))
        exits = {}
        for (c, r), k in comp.items():                          # open cells on the edge: ways out
            if c == 0:
                exits.setdefault(k, set()).add(('exit', 'W', -1, r * 16 + 8, 0, 0, False))
            if c == g.W - 1:
                exits.setdefault(k, set()).add(('exit', 'E', g.W * 16 + 1, r * 16 + 8, 0, 0, False))
            if r == 0:
                exits.setdefault(k, set()).add(('exit', 'N', c * 16 + 8, -1, 0, -5, False))
        self._whisk = {}
        for (c, r), k in comp.items():
            self._whisk[(c, r)] = groups.get(k, set()) | exits.get(k, set())
        return self._whisk.get((col, row), set())

    def result(self, r):
        if r[0] == 'land':
            if not (0 <= r[1] < self.g.W and 0 <= r[2] < self.g.H):
                return None
            return ('node', r[1], r[2], r[3])
        if r[0] == 'exit':
            return r
        return None

    def arrive(self, side, index, pos, vel, ball):
        """Where Basil ends up coming in by a door: the spot he stands on first."""
        g = self.g
        if side in 'WE':
            col = 1 if side == 'W' else g.W - 2
            row = index * SH + 12
            return {('node', col, row, ball)} if self.stands(col, row, ball) else set()
        x = index * SW * 16 + 128
        if side == 'N':                                          # dropping in from above
            starts = [(x, 10, 0.0, 1.0)]
        else:                                                    # coming up a shaft, with a lift
            starts = [(x, g.H * 16 - 30, 0.0, min(vel, -4.5))]
        out = set()
        for (sx, sy, vx, vy) in starts:
            for d in (-1, 0, 1):
                for k in (0, 4, 8, 14):
                    r = simulate(g, sx, sy, ball, lambda f, b, d=d, k=k: (d if f >= k else 0, False, True), self.ab, vy0=vy)
                    if r and r[0] == 'land':
                        out.add(('node', r[1], r[2], r[3]))
                    elif r and r[0] == 'exit' and r[1] == side:
                        out.add(r)                               # (fell back out)
        return out


def door_index(room, side, cx, cy):
    if side in 'WE':
        return int(max(0, min(room.h - 1, cy // (SH * 16))))
    return int(max(0, min(room.w - 1, cx // (SW * 16))))


def check():
    rooms = world.build()
    by_id = {r.id: r for r in rooms}
    at = {}
    for r in rooms:
        for c in r.cells():
            at[c] = r
        if not hasattr(r, 'garlic_gates'):
            r.garlic_gates = set()

    def neighbour(room, side, index):
        if side == 'W':
            cell = (room.x - 1, room.y + index)
            o = at.get(cell)
            return (o, 'E', cell[1] - o.y) if o else None
        if side == 'E':
            cell = (room.x + room.w, room.y + index)
            o = at.get(cell)
            return (o, 'W', cell[1] - o.y) if o else None
        if side == 'N':
            cell = (room.x + index, room.y - 1)
            o = at.get(cell)
            return (o, 'S', cell[0] - o.x) if o else None
        cell = (room.x + index, room.y + room.h)
        o = at.get(cell)
        return (o, 'N', cell[0] - o.x) if o else None

    def door_kind(room, side, index):
        for s, i, k in room.doors:
            if s == side and i == index:
                return k
        return None

    start_room = by_id[world.START[0]]
    start_node = ('node',) + world.START[1:]
    ab = set()
    beaten = set()
    graphs = {}
    reached_items = set()
    history = []
    MOVING = {'dough', 'popcorn', 'sauce', 'frost', 'leaven', 'garlic', 'whisk'}   # (what changes how he moves)
    while True:
        key = frozenset(a for a in ab if a in MOVING or a.startswith('beaten_'))

        def graph(room, key=key):
            k = (room.id, key)
            if k not in graphs:
                graphs[k] = RoomGraph(room, set(key))
            return graphs[k]
        seen = set()
        todo = [(start_room.id,) + start_node[1:]]
        seen.add(todo[0])
        exits_tried = set()
        while todo:
            rid, col, row, ball = todo.pop()
            room = by_id[rid]
            rg = graph(room)
            for m in rg.moves((col, row, ball)):
                if m[0] == 'node':
                    n = (rid, m[1], m[2], m[3])
                    if n not in seen:
                        seen.add(n)
                        todo.append(n)
                else:
                    _, side, cx, cy, vx, vy, mb = m
                    idx = door_index(room, side, cx, cy)
                    k = (rid, side, idx, mb, round(vy))
                    if k in exits_tried:
                        continue
                    exits_tried.add(k)
                    kind = door_kind(room, side, idx)
                    if kind is None:
                        continue
                    if kind == 'wax' and 'sauce' not in ab:
                        continue
                    if kind == 'gate' and len([s for s in ab if s.startswith('seal_')]) < 4:
                        continue
                    if room.boss and room.boss not in beaten:
                        continue                                  # the guardian keeps the doors shut
                    nb = neighbour(room, side, idx)
                    if not nb:
                        continue
                    other, oside, oidx = nb
                    if door_kind(other, oside, oidx) is None:
                        continue
                    if other.heat and 'mitts' not in ab:
                        continue
                    if door_kind(other, oside, oidx) == 'wax' and 'sauce' not in ab:
                        continue
                    for a in graph(other).arrive(oside, oidx, (cx, cy), vy, mb):
                        if a[0] == 'node':
                            n = (other.id, a[1], a[2], a[3])
                            if n not in seen:
                                seen.add(n)
                                todo.append(n)
        rooms_seen = set(n[0] for n in seen)
        # items within reach: standing (or rolled up) near them
        new = False
        for r in rooms:
            if r.id not in rooms_seen:
                continue
            spots = [(n[1], n[2]) for n in seen if n[0] == r.id]
            for th in r.things:
                if th.kind != 'item':
                    continue
                iid = th.vars['iid']
                if iid in reached_items:
                    continue
                if any(abs(c - th.x) <= 1 and (row - th.y) in (0, 1, -1) for c, row in spots):
                    reached_items.add(iid)
                    kind = th.vars['kind']
                    if kind not in ab:
                        ab.add(kind)
                        history.append('%s in %s (%s)' % (kind, r.id, r.name))
                        new = True
            if r.boss and r.boss not in beaten:
                need = world.BOSS_NEEDS.get(r.boss, set())
                if need <= ab and any(True for n in seen if n[0] == r.id):
                    beaten.add(r.boss)
                    ab.add('beaten_' + r.boss)
                    for reward in world.BOSS_REWARDS.get(r.boss, []):
                        ab.add(reward)
                    history.append('beat %s in %s -> %s' % (r.boss, r.id, ', '.join(world.BOSS_REWARDS.get(r.boss, []))))
                    new = True
        if not new:
            break
    print('order of play:')
    for h in history:
        print('  ' + h)
    all_items = [(r.id, th.vars['iid'], th.vars['kind']) for r in rooms for th in r.things if th.kind == 'item']
    missing = [i for i in all_items if i[1] not in reached_items]
    unreached = [r.id for r in rooms if r.id not in rooms_seen]
    ok = True
    if missing:
        ok = False
        print('items out of reach:')
        for m in missing:
            print('  %s in %s' % (m[2], m[0]))
    if unreached:
        ok = False
        print('rooms never reached: ' + ', '.join(unreached))
    unbeaten = [r.boss for r in rooms if r.boss and r.boss not in beaten]
    if unbeaten:
        ok = False
        print('guardians never beaten: ' + ', '.join(unbeaten))
    print('reached %d of %d rooms, %d of %d items' % (len(rooms_seen), len(rooms), len(reached_items), len(all_items)))
    if VERBOSE:
        for r in rooms:
            spots = [(n[1], n[2], n[3]) for n in seen if n[0] == r.id]
            print('  %s: %d spots' % (r.id, len(spots)))
    print('OK' if ok else 'PROBLEMS')
    return ok


if __name__ == '__main__':
    sys.exit(0 if check() else 1)
