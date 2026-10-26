"""The bot: plays Pantry Quest II through the runner a frame at a time. This file has the
frame loop and getting about (rooms, paths, moves); fight.py, things.py and route.py
build on it."""
import os
import sys

import nav
import sim
from game import Game
from runner import Runner, GameEnded
from fight import Fighting
from travel import Travel, HearthTravel
from boss import BossFight
from blocks import Blocks
from freeze import Freezing


class Stuck(Exception):
    pass


class Died(Exception):
    pass


class Bot(Fighting, Travel, HearthTravel, BossFight, Blocks, Freezing):
    def __init__(self, runner, verbose=True, shots=None):
        self.r = runner
        self.g = Game(runner)
        self.verbose = verbose
        self.shots = shots
        self._grid = None
        self._grid_room = None
        self._graphs = {}
        self.inv_cache = None
        self.on_frame = []
        self.debug = False
        self.talk_pause = 40
        self.watch_death = True
        self.deaths = 0                    # hooks run before each frame (fighting, dialogue...)

    # ---- logging ----
    def log(self, *a):
        if self.verbose:
            f = self.r.frames
            s = f // 60
            print('[%6d %d:%02d:%02d] %s' % (f, s // 3600, s // 60 % 60, s % 60, ' '.join(str(x) for x in a)), flush=True)

    def shot(self, name):
        if self.shots:
            os.makedirs(self.shots, exist_ok=True)
            self.r.screenshot(os.path.join(self.shots, '%06d_%s.bmp' % (self.r.frames, name)))

    # ---- frames ----
    def tick(self, held=(), n=1):
        """Holds exactly these keys for n frames. A game over raises Died (once he's back)."""
        for _ in range(n):
            self.r.hold(held)
            self.r.frame(1)
            self.inv_cache = None
            if self.watch_death and self.g.var('Game', 'state') == 'dead':
                self.deaths += 1
                self.log('GAME OVER (%d)' % self.deaths)
                self.r.hold(())
                while self.g.var('Game', 'state') != 'play':
                    self.r.frame(1)
                raise Died()

    def wait(self, n):
        self.tick((), n)

    def wait_until(self, cond, limit=3000, held=()):
        for _ in range(limit):
            if cond():
                return True
            self.tick(held)
        return cond()

    # ---- the game ----
    def state(self):
        return self.g.state()

    def room(self):
        return self.g.room()

    def inv(self):
        """[items, energy, tanks, rockets, max_rockets, pages, frost_on]"""
        if self.inv_cache is None:
            e = self.g.ent('Game')
            # Game.inv is an object: print its fields through to_list() isn't possible; read the parts
            txt = self.g.raw('Game', 'inv')
            self.inv_cache = parse_inventory(txt)
        return self.inv_cache

    def has(self, item):
        return item in self.inv()['items']

    def has_fn(self):
        items = set(self.inv()['items'])
        return lambda it: it in items

    def flags(self):
        return set(self.g.flags())

    def basil(self):
        return self.g.basil()

    def grid(self):
        return self.g.grid()

    # ---- getting about a room ----
    def graph(self, g=None, allow_hurt=False):
        g = g or self.grid()
        has = self.has_fn()
        key = (g.room, tuple(g.t), tuple(sorted(self.inv()['items'])), tuple(g.platforms), allow_hurt)
        rg = self._graphs.get(key)
        if rg is None:
            if len(self._graphs) > 40:
                self._graphs.clear()
            rg = self._graphs[key] = nav.RoomGraph(g, has, allow_hurt)
        return rg

    def settle(self, limit=120):
        """Lets go of everything until he's standing (or rolled up) still on the ground."""
        for _ in range(limit):
            b = self.basil()
            if self.state() == 'play' and b.grounded and b.vx == 0 and b.hurt_t <= 0:
                return b
            self.tick(())
        return self.basil()

    def goto(self, goal, heuristic=None, tries=12, desc='', g=None):
        """Plans and walks/jumps to a node goal(node) accepts (or an exit). True when there
        (or once he's left the room: the goal was this room's)."""
        start_room = self.room()
        for attempt in range(tries):
            self.wait_play()
            if self.room() != start_room:
                return True
            b = self.settle()
            here = nav.node_of(b)
            if goal(here):
                return True
            g = self.grid()
            rg = self.graph(g, allow_hurt=nav.in_hazard(b, g))
            if not b.grounded:
                self.tick(())
                continue
            self._escaping = rg.allow_hurt
            path = rg.path(here, goal, heuristic, start_state=b)
            if path is None:
                # through blocks he can break?
                has = self.has_fn()
                g2 = nav.opened(g, has)
                rg2 = nav.RoomGraph(g2, has)
                path2 = rg2.path(here, goal, heuristic, start_state=b)
                if path2:
                    blocks = nav.blocks_on(path2, g, g2, rg2, has, start_state=b)
                    if blocks and self.clear_block(*blocks[0]):
                        continue
                # over frozen enemies?
                if has('frost') and any(f.brain in ('slider', 'leaper', 'flyer', 'crawler', 'hopper', 'walker', 'swooper')
                                        for f in self.foes()):
                    gxy = getattr(heuristic, 'target', None)
                    if gxy and self.freeze_cross(goal, heuristic, gxy, desc=desc):
                        continue
                self.log('no way from', here, 'to', desc or 'the goal', 'in', g.room)
                return False
            if not path:
                return True
            self.log('path', here, '->', ' '.join('%s:%s' % (mv.name, nx) for (_, mv, _, nx) in path))
            ok = self.follow(path, g, rg)
            self.log('follow:', ok, 'at', nav.node_of(self.basil()), self.room(), self.state())
            if ok == 'done':
                return True
            if ok == 'exit':
                return True
        return False

    def follow(self, path, g, rg):
        """Plays a path's moves. Returns 'done', 'exit' (went through a door) or 'off' (off course)."""
        has = self.has_fn()
        i = 0
        while i < len(path):
            node, mv, dx, nxt = path[i]
            if mv.kind == 'walk' and nxt[0] != 'exit' and nxt[1] == node[1] and nxt[2] == node[2] and abs(nxt[0] - node[0]) == 1:
                # a walk along the floor: run on through every walk the same way
                d = 1 if nxt[0] > node[0] else -1
                j = i
                while j + 1 < len(path):
                    n2, m2, _, x2 = path[j + 1]
                    if m2.kind == 'walk' and x2[0] != 'exit' and x2[1] == n2[1] and x2[2] == n2[2] and x2[0] - n2[0] == d:
                        j += 1
                    else:
                        break
                target = path[j][3]
                # stop walking where the next move can start from (its anchor), or the tile's middle
                if j + 1 < len(path):
                    _, m3, dx3, _ = path[j + 1]
                    tx = target[0] * 16 + 8 + (dx3 or 0)
                else:
                    tx = target[0] * 16 + 8
                # (somewhere he can stand: on a frozen step the tile's middle may be over nothing)
                pose = sim.BALL if target[2] else sim.STAND
                for ddx in sorted(range(-7, 8), key=abs):
                    if rg.stand_y(tx + ddx, target[1], pose) is not None:
                        tx += ddx
                        break
                if not self.walk_to_x(tx, target, g):
                    return 'off'
                i = j + 1
                continue
            r = self.do_move(node, mv, dx, nxt, path[i + 1:], g, has)
            if r == 'exit':
                return 'exit'
            if r is None:
                return 'off'
            i += r                                    # (it may have landed further along the path)
        return 'done'

    def walk_to_x(self, tx, target, g, limit=600):
        has = self.has_fn()
        for _ in range(limit):
            b = self.basil()
            if self.state() != 'play':
                self.wait_play()
                return False
            if not b.grounded and b.hurt_t <= 0:
                self.tick(())
                continue
            if b.hurt_t > 0:
                self.tick(())
                continue
            if nav.node_of(b)[1:] != target[1:] and b.grounded and abs(b.y - ((target[1] + 1) * 16 - 16)) > 2:
                return False                                   # fell or got knocked off
            if self.threats() and self.engage():
                continue
            if b.x == tx and b.vx == 0:
                return True
            if abs(b.x - tx) <= 1 and nav.node_of(b) == target:
                # close enough: stop here (an exact pixel isn't needed; moves are checked before they start)
                self.tick(())
                return True
            d = 1 if tx > b.x else -1
            # don't overshoot: if one more step goes past, tap instead (one step at a time)
            nb = b.copy()
            sim.frame(nb, nav.held_of(d), [], 1, g, has)
            if not nb.grounded:                        # (and never walk off an edge the plan didn't mean)
                self.tick(())
                return nav.node_of(self.basil()) == target
            if (nb.x - tx) * d > 0 and abs(nb.x - tx) > abs(b.x - tx):
                self.tick(())
                return nav.node_of(self.basil()) == target
            self.tick(nav.held_of(d))
        return False

    def do_move(self, node, mv, dx, nxt, rest, g, has):
        """Starts a move from where the sim says it lands on the path's next spot (or further on),
        after a few steps to get to that start if need be, and plays it. Returns how many
        steps along the path it got (1 = the next spot), 'exit', or None."""
        targets = [nxt] + [r[3] for r in rest]
        anchor = node[0] * 16 + 8 + dx if dx is not None else self.basil().x
        for attempt in range(40):
            if self.state() != 'play':
                self.wait_play()
                return None
            b = self.basil()
            if b.hurt_t > 0 or not b.grounded:
                self.tick(())
                continue
            if self.threats() and self.engage():
                continue
            found = self.find_start(b, g, has, mv, targets, anchor)
            if found is None:
                if self.find_start(b, g, has, mv, targets, anchor, safe=False) is None:
                    return None
                if not (self.threats(160) and self.engage(radius=160)):  # (in the way: shoot it, or wait for it to pass)
                    self.tick(())
                    self.tick(())
                continue
            pre, cand, k, res, expect = found
            ok = True
            for i, held in enumerate(pre):
                self.tick(held)
            if pre:
                now = self.basil()
                if (now.x, now.y, now.sx, now.grounded) != (expect.x, expect.y, expect.sx, expect.grounded):
                    continue                                   # (something got in the way: look again)
            return self.play_move(cand, res, k + 1, len(targets))
        return None

    _escaping = False

    def find_start(self, b, g, has, mv, targets, anchor, safe=True):
        """A few frames of walking (or none) and then a move that lands on one of the targets:
        (pre-walk keys, move, which target, landing node, state after the walk), or None."""
        sched = self.r.steps_ahead(260)
        hazards = self.movers() if safe else []
        pres = [[]]
        for n in range(1, 28):
            for d in (-1, 1):
                pres.append([nav.held_of(d)] * n)
                pres.append([nav.held_of(d)] * n + [set()])
        others = [m for m in (nav.BALL_MOVES + (nav.BOUNCES if has('popcorn') else []) if b.ball else
                              nav.MOVES + (nav.WHISK if has('whisk') else [])) if m is not mv] + \
                 ([nav.STAND_UP if b.ball else nav.ROLL_UP] if has('dough') else [])
        best = None
        for pass_moves in ([mv], others):
            for pre in pres:
                if best is not None and len(pre) > best[0][2] + 6:
                    continue
                s = b.copy()
                prev = set(self.r.held)
                bad = False
                for f, held in enumerate(pre):
                    pressed = [k for k in held if k not in prev]
                    sim.frame(s, held, pressed, sched[f], g, has)
                    prev = held
                    if not s.grounded or (s.hurt_count > b.hurt_count) or (hazards and nav.touches_any(s, hazards, f + 1)):
                        bad = True
                        break
                if bad:
                    continue
                for cand in pass_moves:
                    shifted = [nav.Mover(m.x + m.vx * len(pre), m.y + m.vy * len(pre), m.vx, m.vy, m.hx, m.hy) for m in hazards]
                    how, e, frames, keys = nav.play(cand, s, g, has, prev, sched[len(pre):], hazards=shifted, allow_hurt=self._escaping)
                    if how == 'land':
                        res = nav.node_of(e)
                    elif how == 'exit':
                        res = ('exit', e.left_room, nav.exit_index(g, e))
                    else:
                        continue
                    if res not in targets:
                        continue
                    k = targets.index(res)
                    score = (e.hurt_count, -k, len(pre) + frames)
                    if best is None or score < best[0]:
                        best = (score, (list(pre), cand, k, res, s))
            if best is not None:
                break
        return best[1] if best else None

    def play_move(self, mv, expect, index_after, n_targets):
        """Plays a move in the game, a frame at a time (its keys from the game's own state)."""
        b = self.basil()
        if self.debug:
            self.log('  move', mv.name, 'from', (b.x, b.y, b.sx), 'expect', expect)
        airborne = not b.grounded
        for f in range(220):
            held = mv.keys(f, b) if f < 120 else set()
            self.tick(held)
            if self.state() != 'play':
                self.wait_play()
                return 'exit' if isinstance(expect, tuple) and expect[0] == 'exit' else None
            b = self.basil()
            if b.hurt_t > 0:
                return None
            if not b.grounded:
                airborne = True
            elif mv.popcorn and not airborne and f < 70:
                continue
            elif airborne or (mv.kind == 'toggle' and f >= 1) or (f + 1 >= mv.length and f >= 1 and not held):
                break
        got = nav.node_of(b)
        if self.debug and got != expect:
            self.log('  landed', got, 'not', expect, b)
        if expect[0] == 'exit':
            return None
        if got == expect:
            return index_after
        return None

    def movers(self, radius=200):
        """Enemies and things thrown nearby, moving on as they're moving now."""
        out = []
        b = self.basil()
        for f in self.foes(within=radius):
            if f.frozen or not f.visible and f.brain != 'ghost':
                continue
            out.append(nav.Mover(f.x, f.y, f.vx, f.vy, f.half[0], f.half[1]))
        for (s, i, nm, x, y) in self.r.tagged('spit'):
            if abs(x - b.x) < radius and abs(y - b.y) < radius:
                v = self.r.vars(s, i, ['vel'])[0]
                try:
                    vx, vy = [float(t) for t in v.strip()[1:-1].split(',')]
                except ValueError:
                    vx = vy = 0.0
                out.append(nav.Mover(x, y, vx, vy, 3, 3))
        return out

    # ---- Frost Sugar on or off (the pause menu's SYSTEM page) ----
    def set_frost(self, on):
        inv = self.inv()
        if 'frost' not in inv['items'] or inv['frost_on'] == on:
            return
        self.log('Frost Sugar', 'on' if on else 'off')
        self.wait_play()
        self.settle()
        self.tick(())
        self.tick(('pause',))
        self.wait_until(lambda: self.r.scenes()[-1:] == ['pause.scene'], 60)
        self.wait(20)
        self.tick(('left',))                       # MAP -> SYSTEM
        self.wait(20)
        self.tick(('jump',))                       # the first option: FROST SUGAR
        self.wait(30)
        self.tick(('pause',))
        self.tick(())
        self.wait_play()
        self.inv_cache = None

    def return_to_hearth(self):
        """RETURN TO LAST HEARTH, from the pause menu's SYSTEM page."""
        self.log('returning to the last hearth')
        self.wait_play()
        self.settle()
        before = self.room()
        self.tick(())
        self.tick(('pause',))
        self.wait_until(lambda: self.r.scenes()[-1:] == ['pause.scene'], 60)
        self.wait(20)
        self.tick(('left',))                       # MAP -> SYSTEM
        self.wait(12)
        opts = self.g.raw('Pause', 'options')
        names = [x.strip() for x in opts.strip('[]').split(',')]
        if 'hearth' not in names:
            self.log('  (not allowed here)')
            self.tick(('pause',))
            self.tick(())
            self.wait_play()
            return False
        for _ in range(names.index('hearth')):
            self.tick(('down',))
            self.tick(())
        self.wait(10)
        self.tick(('jump',))
        self.tick(())
        self.wait_until(lambda: self.room() != before and self.state() == 'play', 400)
        self.wait_play()
        return True

    # ---- hearths ----
    def rest(self):
        """Rests at the hearth in this room (saving, and filling energy and hot sauce)."""
        for (s, i, nm, x, y) in self.r.tagged('hearth'):
            self.log('resting at the hearth in', self.room())
            self.use_here(x, y + 4, 'the hearth')
            self.wait_play()
            return True
        return False

    def threats(self, radius=96):
        """Enemies close enough to bother with (a cheap look: positions only)."""
        b = self.basil()
        n = 0
        seen = getattr(self, '_foe_last', {})
        prev = getattr(self, '_foe_prev', {})
        skip = set(i for (s, i, nm, x, y) in self.r.tagged('platform')) | set(i for (s, i, nm, x, y) in self.r.tagged('boss'))
        for (s, i, nm, x, y) in self.r.tagged('enemy'):
            if i in skip:
                continue
            if i in seen and seen[i][0] < self.r.frames:
                prev[i] = seen[i]
            seen[i] = (self.r.frames, x, y)
            if abs(x - b.x) < radius and abs(y - b.y) < radius:
                n += 1
        self._foe_last, self._foe_prev = seen, prev
        return n

    def wait_play(self, limit=6000):
        """Waits until the game is playing, reading through any talk on the way."""
        for _ in range(limit):
            sc = self.r.scenes()
            st = self.state()
            if st == 'play' and len(sc) == 1:
                return True
            if sc and sc[-1] == 'dialogue.scene':
                self.read_talk()
                continue
            if sc and sc[-1] == 'hearth.scene':
                self.tick(('rocket',))                  # (the hearth's menu: stay)
                self.tick(())
                continue
            self.tick(())
        return False

    def read_page(self, name, pause=40):
        """Waits for a page of words to be shown in full, gives a moment to read it, then JUMP."""
        last = None
        same = 0
        for _ in range(1200):
            if not self.g.ent(name):
                return
            v = self.g.var(name, 'shown')
            if v == last:
                same += 1
            else:
                same = 0
            last = v
            if same >= pause:
                break
            self.tick(())
        self.tick(('jump',))
        self.tick(())

    def read_talk(self):
        """Pages through the dialogue box on top (each page: wait for the words, then JUMP)."""
        for _ in range(4000):
            sc = self.r.scenes()
            if not sc or sc[-1] != 'dialogue.scene':
                return
            self.read_page('Dialogue', self.talk_pause)


def parse_inventory(txt):
    """Inventory(items: [dough, popcorn], energy: 99, tanks: 0, rockets: 0, max_rockets: 0, pages: [], frost_on: true)"""
    out = {'items': [], 'energy': 0, 'tanks': 0, 'rockets': 0, 'max_rockets': 0, 'pages': [], 'frost_on': True}
    import re
    m = re.search(r'items: \[([^\]]*)\]', txt)
    if m:
        out['items'] = [s.strip() for s in m.group(1).split(',') if s.strip()]
    m = re.search(r'pages: \[([^\]]*)\]', txt)
    if m:
        out['pages'] = [s.strip() for s in m.group(1).split(',') if s.strip()]
    for k in ('energy', 'tanks', 'rockets', 'max_rockets'):
        m = re.search(k + r': (-?[\d.]+)', txt)
        if m:
            out[k] = int(float(m.group(1)))
    out['frost_on'] = 'frost_on: false' not in txt
    return out
