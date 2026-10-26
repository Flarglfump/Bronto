"""Getting about a room: the spots Basil can stand (or roll) on, and the moves between them -
walks, jumps of many kinds, rolling up - each one played through sim.py's exact physics.

A move is a policy: given the frame number and Basil's state it says which keys are held.
The same policy drives the planning (in the sim) and the playing (in the game), so a move
the sim says lands on a spot lands there."""
import heapq
import math

import sim
import simfast

FAST = True

TILE = 16


# ---- moves --------------------------------------------------------------------------------------
class Move:
    def __init__(self, name, fn, length, kind='jump', popcorn=False):
        self.name, self.fn, self.length, self.kind = name, fn, length, kind
        self.popcorn = popcorn

    def keys(self, f, b):
        return self.fn(f, b)


def dkey(d):
    return 'right' if d > 0 else 'left' if d < 0 else None


def held_of(d, jump=False, *extra):
    out = set(extra)
    k = dkey(d)
    if k:
        out.add(k)
    if jump:
        out.add('jump')
    return out


def make_moves():
    moves = []
    for d in (-1, 1):
        moves.append(Move('walk%+d' % d, lambda f, b, d=d: held_of(d if f < 11 else 0), 11, 'walk'))
        for s in (0, 6, 12, 18, 26):
            moves.append(Move('jump%+d_from%d' % (d, s), lambda f, b, d=d, s=s: held_of(d if f >= s else 0, True), 1))
        for s in (8, 16, 24):
            moves.append(Move('jump%+d_until%d' % (d, s), lambda f, b, d=d, s=s: held_of(d if f < s else 0, True), 1))
        moves.append(Move('hop%+d' % d, lambda f, b, d=d: held_of(d, f < 6), 1))
        for h in (10, 14, 18):
            moves.append(Move('jump%+d_let%d' % (d, h), lambda f, b, d=d, h=h: held_of(d, f < h), 1))
            moves.append(Move('jump%+d_let%d_late' % (d, h), lambda f, b, d=d, h=h: held_of(d if f >= 8 else 0, f < h), 1))
        moves.append(Move('jump%+d_back' % d, lambda f, b, d=d: held_of(d if f < 30 else -d, True), 1))
        moves.append(Move('jump%+d_back20' % d, lambda f, b, d=d: held_of(d if f < 20 else -d, True), 1))
    moves.append(Move('jump_up', lambda f, b: held_of(0, True), 1))
    return moves


def make_ball_moves():
    moves = []
    for d in (-1, 1):
        moves.append(Move('roll%+d' % d, lambda f, b, d=d: held_of(d if f < 11 else 0), 11, 'walk'))
    return moves


POP_STEPS = 49                     # popcorn pops on the 49th step from the one it was dropped in (that one is 1)


def make_bounces():
    """Rolled up: drop popcorn, wait for it to pop and throw him up, steering after k frames
    (or rolling off just before it pops, to be thrown up from beside it)."""
    out = []
    for d in (-1, 0, 1):
        for k in ((0, 6, 14) if d else (0,)):
            out.append(Move('bounce%+d_%d' % (d, k),
                            lambda f, b, d=d, k=k: {'fire'} if f == 0 else (held_of(d) if f >= 50 + k else set()),
                            1, 'jump', popcorn=True))
        if d:
            for s in (42, 44, 45, 46, 47):
                out.append(Move('bounce%+d_early%d' % (d, s),
                                lambda f, b, d=d, s=s: {'fire'} if f == 0 else (held_of(d) if f >= s else set()),
                                1, 'jump', popcorn=True))
    return out


MOVES = make_moves()
BALL_MOVES = make_ball_moves()
BOUNCES = make_bounces()
WAIT = Move('wait', lambda f, b: set(), 1, 'walk')
ROLL_UP = Move('roll_up', lambda f, b: {'ball'} if f == 0 else set(), 1, 'toggle')
STAND_UP = Move('stand_up', lambda f, b: {'up'} if f == 0 else set(), 1, 'toggle')
ALL_MOVES = MOVES + BALL_MOVES + [ROLL_UP, STAND_UP]


def node_of(b):
    return (int(b.x // TILE), int((b.y + 15) // TILE), bool(b.ball))


# ---- playing a move through the sim ------------------------------------------------------------
def touches_any(b, hazards, k, margin=2):
    mx, my = b.middle()
    w, h = sim.SIZES[b.pose()]
    for hz in hazards:
        p = hz.at(k)
        if p is None:
            continue
        if abs(p[0] - mx) < hz.hx + w / 2 + margin and abs(p[1] - my) < hz.hy + h / 2 + margin:
            return True
    return False


class Mover:
    """An enemy moving on in a straight line (for keeping out of its way)."""
    __slots__ = ('x', 'y', 'vx', 'vy', 'hx', 'hy')

    def __init__(self, x, y, vx, vy, hx, hy):
        self.x, self.y, self.vx, self.vy, self.hx, self.hy = x, y, vx, vy, hx, hy

    def at(self, k):
        return self.x + self.vx * k, self.y + self.vy * k


def play(move, b, g, has, held0=(), schedule=None, max_frames=200, hazards=None, record=None, allow_hurt=False):
    if FAST and simfast.load():
        return simfast.play(move, b, g, has, held0, schedule, max_frames, hazards, record, allow_hurt)
    return play_py(move, b, g, has, held0, schedule, max_frames, hazards, record, allow_hurt)


def play_py(move, b, g, has, held0=(), schedule=None, max_frames=200, hazards=None, record=None, allow_hurt=False):
    """Plays a move from state b (not changed). Returns (how, state, frames, keys) where how is
    'land' (standing / rolled up again), 'exit' (off the room's edge), 'hurt' or None, and keys
    is the list of held-key sets frame by frame."""
    b = b.copy()
    prev = set(held0)
    airborne = not b.grounded
    keys = []
    pending = []
    pop = None                                       # [steps since dropped, x, y]
    for f in range(max_frames):
        held = move.keys(f, b) if f < 120 else set()
        pressed = [k for k in held if k not in prev]
        steps = schedule[f] if schedule is not None and f < len(schedule) else 1
        if pending:
            pressed = pressed + [k for k in pending if k not in pressed]
        pending = pressed if steps == 0 else []
        hurt_before = b.hurt_count
        hurt_any = hurt_before
        if move.popcorn:
            for i in range(steps):
                sim.step(b, sim.Inputs(held, pressed if i == 0 else ()), g, has)
                if pop is None and i == 0 and 'fire' in pressed and b.ball:
                    pop = [1, b.x, b.y + 10]
                elif pop is not None and pop[0] > 0:
                    pop[0] += 1
                    if pop[0] == POP_STEPS:
                        pop[0] = -1
                        if b.ball and abs(b.x - pop[1]) < 14 and -20 < b.y + 10 - pop[2] < 4:
                            b.vy, b.grounded, b.lifted = -4.6, False, True
        else:
            sim.frame(b, held, pressed, steps, g, has)
        if record is not None:
            record.append(b.copy())
        keys.append(held)
        prev = held
        if b.hurt_count > hurt_before and not allow_hurt:
            return 'hurt', b, f + 1, keys
        if hazards and touches_any(b, hazards, f + 1):
            return 'hurt', b, f + 1, keys
        if b.left_room:
            return 'exit', b, f + 1, keys
        if not b.grounded:
            airborne = True
        elif move.popcorn and (pop is None or pop[0] > 0):
            continue
        elif airborne:
            return 'land', b, f + 1, keys
        elif move.popcorn:
            return None, b, f + 1, keys                      # (popped, but no bounce)
        elif move.kind == 'toggle' and f >= 1:
            return 'land', b, f + 1, keys
        elif f + 1 >= move.length and f >= 1 and not held:
            return 'land', b, f + 1, keys
    return None, b, max_frames, keys


# ---- the room as a graph ----------------------------------------------------------------------
class RoomGraph:
    """Standing / rolled-up spots and the moves between them, worked out as they're needed."""

    def __init__(self, g, has, allow_hurt=False):
        self.g, self.has = g, has
        self.cache = {}
        self.allow_hurt = allow_hurt                  # (only to get out of spikes or lava he's already in)

    def stand_y(self, x, row, pose):
        """The y he'd stand at in a row (feet on whatever is under him there), or None."""
        for y in range((row + 1) * TILE - 16, row * TILE - 16, -1):
            if not self.g.blocked(x, y, pose) and self.g.blocked(x, y + 1, pose):
                return y
        return None

    def stands(self, col, row, ball):
        pose = sim.BALL if ball else sim.STAND
        return self.stand_y(col * TILE + 8, row, pose) is not None

    def anchor_states(self, node):
        col, row, ball = node
        pose = sim.BALL if ball else sim.STAND
        out = []
        for dx in (0, -5, 5):
            x = col * TILE + 8 + dx
            y = self.stand_y(x, row, pose)
            if y is None:
                continue
            out.append(sim.Basil(x=x, y=y, vx=0.0, vy=0.0, sx=0.0, sy=0.0, facing=1, grounded=True, ball=ball,
                                 crouch=False, spin=False, hurt_t=0, safe_t=0, lifted=False, in_liquid=False,
                                 lava_t=0, shot_cd=0))
        return out

    def moves(self, node):
        """[(cost, result, move, dx)]: result is a node or ('exit', side, index)."""
        if node in self.cache:
            return self.cache[node]
        out = {}
        col, row, ball = node
        candidates = BALL_MOVES if ball else MOVES
        if ball and self.has('popcorn'):
            candidates = candidates + BOUNCES
        for b in self.anchor_states(node):
            dx = b.x - (col * TILE + 8)
            for mv in candidates:
                how, e, frames, _ = play(mv, b, self.g, self.has, allow_hurt=self.allow_hurt)
                self._add(out, how, e, frames, mv, dx)
            if self.has('dough'):
                how, e, frames, _ = play(STAND_UP if ball else ROLL_UP, b, self.g, self.has, allow_hurt=self.allow_hurt)
                self._add(out, how, e, frames, STAND_UP if ball else ROLL_UP, dx)
        res = [(c, r, mv, dx) for r, (c, mv, dx) in out.items()]
        self.cache[node] = res
        return res

    def _add(self, out, how, e, frames, mv, dx):
        if e.hurt_count:
            frames += 900 * e.hurt_count                     # (through spikes or lava: only if there's no other way)
        if how == 'land':
            r = node_of(e)
        elif how == 'exit':
            r = ('exit', e.left_room, exit_index(self.g, e))
        else:
            return
        cost = frames + (0 if mv.kind == 'walk' else 12)         # (walking looks better than hopping)
        if r not in out or cost < out[r][0]:
            out[r] = (cost, mv, dx)

    def moves_from_state(self, b):
        """The moves from exactly where he is (not a tile's anchor): dx is None for these."""
        out = {}
        cands = (BALL_MOVES + (BOUNCES if self.has('popcorn') else []) if b.ball else MOVES) + [WAIT]
        if self.has('dough'):
            cands = cands + [STAND_UP if b.ball else ROLL_UP]
        for mv in cands:
            how, e, frames, _ = play(mv, b, self.g, self.has, allow_hurt=self.allow_hurt)
            self._add(out, how, e, frames, mv, None)
        return [(c, r, mv, dx) for r, (c, mv, dx) in out.items()]

    def path(self, start, goal, heuristic=None, limit=4000, start_state=None):
        """Cheapest way (in frames) from a node to one goal(node) says yes to: [(node, move, dx, next)].
        With start_state, the first moves are played from exactly there."""
        if goal(start):
            return []
        first = self.moves_from_state(start_state) if start_state is not None else None
        h = heuristic or (lambda n: 0)
        best = {start: 0}
        came = {}
        q = [(h(start), 0, 0, start)]
        tie = 0
        n_exp = 0
        while q:
            _, cost, _, n = heapq.heappop(q)
            if cost > best.get(n, 1e18):
                continue
            if goal(n):
                out = []
                while n in came:
                    p, mv, dx = came[n]
                    out.append((p, mv, dx, n))
                    n = p
                return out[::-1]
            if n[0] == 'exit':
                continue
            n_exp += 1
            if n_exp > limit:
                break
            for c, r, mv, dx in (first if (n == start and first is not None) else self.moves(n)):
                nc = cost + c
                if nc < best.get(r, 1e18):
                    best[r] = nc
                    came[r] = (n, mv, dx)
                    tie += 1
                    heapq.heappush(q, (nc + h(r), nc, tie, r))
        return None


def _reachable(self, start, limit=3000):
    """Every node reachable from start, with its cost (frames)."""
    best = {start: 0}
    q = [(0, 0, start)]
    tie = 0
    while q:
        cost, _, n = heapq.heappop(q)
        if cost > best.get(n, 1e18) or n[0] == 'exit':
            continue
        if len(best) > limit:
            break
        for c, r, mv, dx in self.moves(n):
            nc = cost + c
            if nc < best.get(r, 1e18):
                best[r] = nc
                tie += 1
                heapq.heappush(q, (nc, tie, r))
    return best


RoomGraph.reachable = _reachable


def exit_index(g, b):
    mx, my = b.middle()
    if b.left_room in ('W', 'E'):
        return int(max(0, my) // 240)
    return int(max(0, mx) // 256)


def dist_heuristic(tx, ty):
    def h(n):
        if n[0] == 'exit':
            return 0
        x, y = n[0] * TILE + 8, n[1] * TILE
        return math.hypot(x - tx, y - ty) / 2.5
    h.target = (tx, ty)
    return h


def trace(move, b, g, has, max_frames=200):
    """The states a move goes through (for seeing what it passes)."""
    out = [b.copy()]
    play(move, b, g, has, (), None, max_frames, record=out)
    return out


BREAKABLE = {'c': 'fire', 'x': 'popcorn', 'r': 'rocket'}


def opened(g, has):
    """The room with every block he can break taken out (for planning a way through)."""
    g2 = g.copy_with(g.platforms)
    g2.t = list(g.t)
    for r, row in enumerate(g2.t):
        out = []
        for ch in row:
            if ch == 'c' or (ch == 'x' and has('popcorn')) or (ch == 'r' and has('sauce')):
                out.append('.')
            else:
                out.append(ch)
        g2.t[r] = ''.join(out)
    g2.memo = {}
    return g2


def blocks_on(path, g_real, g_open, rg_open, has, start_state=None):
    """The breakable cells (col, row, char) a path through the opened room goes through, in order."""
    out = []
    for (node, mv, dx, nxt) in path:
        if node[0] == 'exit':
            continue
        states = None
        if dx is None and start_state is not None:
            states = trace(mv, start_state, g_open, has)
        for a in ([] if states else rg_open.anchor_states(node)):
            if dx is None or a.x - (node[0] * 16 + 8) == dx:
                states = trace(mv, a, g_open, has)
                break
        if not states:
            continue
        for s in states:
            w, h = sim.SIZES[s.pose()]
            mx, my = s.middle()
            for r in range(int((my - h / 2) // 16), int((my + h / 2 - 0.001) // 16) + 1):
                for c in range(int((mx - w / 2) // 16), int((mx + w / 2 - 0.001) // 16) + 1):
                    ch = g_real.cell(c, r)
                    if ch in BREAKABLE and (c, r, ch) not in out:
                        out.append((c, r, ch))
            # (standing on a block: the floor under him)
            if s.grounded:
                for c in range(int((s.x - 6) // 16), int((s.x + 5.999) // 16) + 1):
                    r = int((s.y + 16) // 16)
                    ch = g_real.cell(c, r)
                    if ch in BREAKABLE and g_open.cell(c, r) == '.' and (c, r, ch) not in out:
                        pass
    return out


def in_hazard(b, g):
    """Is he in spikes or lava now?"""
    s = b.copy()
    s.safe_t = 1
    return sim.hazards(s, g)
