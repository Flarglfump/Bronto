"""Frozen steps: with Frost Sugar, a frozen enemy is something to stand on for a few seconds.
Where the way on is over spikes or molten cheese (the Frozen Pipes, the Simmer Hall), freeze
an enemy where it makes a step towards the goal, hop on, keep it frozen (a shot down at it),
freeze the next, and so on."""
import math

import nav
import sim
from fight import muzzle, aim_dir, stance_keys, AIMS


class Freezing:
    """Mixed into Bot."""

    def frozen_steps(self):
        """[(x, y, seconds left, foe id)] for every frozen enemy."""
        out = []
        for (s, i, nm, x, y) in self.r.tagged('platform'):
            v = self.r.vars(s, i, ['frozen_t'])[0]
            try:
                t = float(v)
            except ValueError:
                t = 0
            out.append((x, y, t, i))
        return out

    def predict(self, foe, k):
        """Where a freezable enemy will be k frames on (sliders bounce off walls; leapers fall)."""
        if foe.brain == 'slider':
            x, f = foe.x, (1 if foe.vx >= 0 else -1)
            g = self._fz_grid
            for _ in range(int(k)):
                if g.tile_at(x + f * 9, foe.y) in g.solid_chars:
                    f = -f
                x += f * 0.8
            return x, foe.y
        if foe.brain == 'leaper':
            x, y, vy = foe.x, foe.y, foe.vy
            for _ in range(int(k)):
                vy += 0.15
                y += vy
            return x, y
        return foe.x + foe.vx * k, foe.y + foe.vy * k

    def freeze_cross(self, goal, heur, goal_xy, limit=5400, desc=''):
        """Gets to goal(node) over a gap that needs frozen enemies as steps."""
        self.log('crossing on frozen steps to', desc)
        self.set_frost(True)
        try:
            return self._freeze_cross(goal, heur, goal_xy, limit, desc)
        finally:
            self.set_frost(False)

    def _freeze_cross(self, goal, heur, goal_xy, limit, desc):
        useful_cache = {}
        start_frame = self.r.frames
        while self.r.frames - start_frame < limit:
            if self.state() != 'play' or len(self.r.scenes()) > 1:
                self.wait_play()
                continue
            b = self.basil()
            if b.hurt_t > 0 or not b.grounded:
                self.tick(())
                continue
            here = nav.node_of(b)
            if here[0] != 'exit' and goal(here):
                return True
            g = self.grid()
            self._fz_grid = g
            has = self.has_fn()
            steps = self.frozen_steps()
            rg = nav.RoomGraph(g, has)
            # standing on a frozen step that's thawing: freeze it again (a shot straight down)
            under = [p for p in steps if abs(p[0] - b.x) < 14 and 0 < p[1] - (b.y + 16) < 12]
            if under and under[0][2] < 3.0 and b.shot_cd <= 0 and 'fire' not in self.r.held:
                self.tick(('aim', 'down', 'fire'))
                self.tick(())
                continue
            # the whole way, on the steps there are now?
            path = rg.path(here, goal, heur, start_state=b, limit=1500)
            if path and self.steps_last(path, steps):
                self.follow(path, g, rg)
                continue
            # on towards it, onto a step nearer the goal?
            d_here = math.hypot(b.x - goal_xy[0], b.y - goal_xy[1])

            def progress(n, steps=steps):
                if n[0] == 'exit' or n[2]:
                    return False
                x, y = n[0] * 16 + 8, (n[1] + 1) * 16 - 16
                if math.hypot(x - goal_xy[0], y - goal_xy[1]) > d_here - 24:
                    return False
                return any(abs(p[0] - x) < 16 and int((p[1] - 24 + 15) // 16) == n[1] and p[2] > 2.5 for p in steps)
            path = rg.path(here, progress, heur, start_state=b, limit=1500)
            if path:
                self.follow(path, g, rg)
                continue
            # freeze something where it makes a step on
            if self.freeze_useful(b, g, has, here, goal, progress, heur, goal_xy, steps, useful_cache):
                continue
            # (or first get as near the goal as he can on his own: the edge of the gap)
            if not getattr(self, '_fz_edge_tried', None) == (g.room, here):
                reach = rg.reachable(here, limit=600)
                best = min((n for n in reach if n[0] != 'exit' and not n[2]),
                           key=lambda n: math.hypot(n[0] * 16 + 8 - goal_xy[0], (n[1] + 1) * 16 - 16 - goal_xy[1]),
                           default=None)
                self._fz_edge_tried = (g.room, here)
                if best is not None and best != here:
                    self.log('  to the edge', best)
                    path = rg.path(here, lambda n, best=best: n == best, heur, start_state=b)
                    if path:
                        self.follow(path, g, rg)
                        continue
            self.tick(())
        return False

    def steps_last(self, path, steps):
        """Do the frozen steps a path stands on last until it's past them?"""
        t = 0
        for (node, mv, dx, nxt) in path:
            t += 40
            if nxt[0] == 'exit':
                continue
            x = nxt[0] * 16 + 8
            for p in steps:
                if abs(p[0] - x) < 16 and int((p[1] - 24 + 15) // 16) == nxt[1] and p[2] * 60 < t + 30:
                    return False
        return True

    def freeze_useful(self, b, g, has, here, goal, progress, heur, goal_xy, steps, cache):
        """Fires a frost shot now if it freezes an enemy where it would make a step on."""
        if b.shot_cd > 0 or 'fire' in self.r.held or len(self.r.tagged('pepper')) >= 3:
            return False
        kind = self.beam()
        if kind not in ('frost', 'garlic_frost'):
            return False
        foes = [f for f in self.foes(within=170) if not f.frozen and f.visible]
        for foe in foes:
            for facing in (b.facing, -b.facing):
                for aim in AIMS:
                    mx, my = muzzle(b.x, b.y, facing, aim, False)
                    dx, dy = aim_dir(aim, facing)
                    k = self.shot_hits(g, mx, my, dx, dy, foe, kind)
                    if k is None:
                        continue
                    px, py = self.predict(foe, k + 1)
                    key = (round(px / 4), round(py / 4), tuple(sorted((round(p[0]), round(p[1])) for p in steps)))
                    ok = cache.get(key)
                    if ok is None:
                        g2 = g.copy_with(list(g.platforms) + [(px, py)])
                        rg2 = nav.RoomGraph(g2, has)
                        newsteps = steps + [(px, py, 6.0, -1)]
                        d_here = math.hypot(b.x - goal_xy[0], b.y - goal_xy[1])

                        def prog2(n):
                            if n[0] == 'exit':
                                return goal(n)
                            if goal(n):
                                return True
                            if n[2]:
                                return False
                            x, y = n[0] * 16 + 8, (n[1] + 1) * 16 - 16
                            if math.hypot(x - goal_xy[0], y - goal_xy[1]) > d_here - 40:
                                return False
                            return any(abs(p[0] - x) < 16 and int((p[1] - 24 + 15) // 16) == n[1] for p in newsteps)
                        ok = rg2.path(here, prog2, heur, start_state=b, limit=800) is not None
                        cache[key] = ok
                    if ok:
                        self.log('  freezing', foe, 'at', (round(px), round(py)))
                        keys = stance_keys(aim, facing)
                        keys.add('fire')
                        self.tick(keys)
                        self.tick(())
                        return True
        return False
