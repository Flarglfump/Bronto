"""Breaking through: crumb blocks (any shot), wax (hot sauce) and cracked blocks (popcorn),
when the way somewhere goes through them."""
import nav
from fight import muzzle, aim_dir, stance_keys, AIMS


def first_cell(g, mx, my, dx, dy, kind):
    """The cell a shot stops in (or breaks): (col, row, char), or None if it flies out of range."""
    if kind == 'rocket':
        x, y, speed = mx, my, 2.0
        for k in range(60):
            speed = min(speed + 0.5, 6.5)
            for _ in range(3):
                x += dx * speed / 3
                y += dy * speed / 3
                t = g.tile_at(x, y)
                if t in ('Y', 'r', 'c') or t in g.solid_chars:
                    return int(x // 16), int(y // 16), t
        return None
    through = kind in ('garlic', 'garlic_frost')
    rng = 132.0 if through else 88.0
    x, y, gone = mx, my, 0.0
    while gone < rng:
        x += dx * 2.5
        y += dy * 2.5
        gone += 2.5
        t = g.tile_at(x, y)
        if t in ('Y', 'c'):
            return int(x // 16), int(y // 16), t
        if not through and t in g.solid_chars:
            return int(x // 16), int(y // 16), t
    return None


class Blocks:
    """Mixed into Bot."""

    def clear_block(self, c, r, ch):
        self.log('breaking', ch, 'at', (c, r))
        if ch == 'x':
            return self.pop_block(c, r)
        g = self.grid()
        rg = self.graph(g)
        here = nav.node_of(self.settle())
        kind = 'rocket' if ch == 'r' else self.beam()
        best = None
        for node, cost in rg.reachable(here).items():
            if node[0] == 'exit' or node[2]:
                continue
            for a in rg.anchor_states(node):
                for crouch in (False, True):
                    for facing in (1, -1):
                        for aim in AIMS:
                            if crouch and aim == 'down':
                                continue
                            mx, my = muzzle(a.x, a.y, facing, aim, crouch)
                            ddx, ddy = aim_dir(aim, facing)
                            hit = first_cell(g, mx, my, ddx, ddy, kind)
                            if hit and hit[:2] == (c, r):
                                score = cost + (8 if crouch else 0)
                                if best is None or score < best[0]:
                                    best = (score, node, a.x, aim, facing, crouch)
        if best is None:
            self.log('no way to shoot', (c, r))
            return False
        _, node, ax, aim, facing, crouch = best
        if not self.goto(lambda n, node=node: n == node, nav.dist_heuristic(node[0] * 16 + 8, node[1] * 16), desc='a spot to shoot from'):
            return False
        fire = 'rocket' if kind == 'rocket' else 'fire'
        for _ in range(240):
            g = self.grid()
            if g.cell(c, r) != ch:
                self.release_stance()
                return True
            b = self.basil()
            if b.ball:
                self.tick(('up',))
                self.tick(())
                continue
            if crouch and not b.crouch:
                self.tick(())
                self.tick(('down',))
                continue
            # still the right spot? (the shot is worked out from where he stands)
            mx, my = muzzle(b.x, b.y, facing, aim, crouch)
            ddx, ddy = aim_dir(aim, facing)
            hit = first_cell(g, mx, my, ddx, ddy, kind)
            if not hit or hit[:2] != (c, r):
                d = 1 if ax > b.x else -1
                if b.crouch:
                    self.tick(())
                    self.tick(('up',))
                    continue
                self.tick(nav.held_of(d))
                continue
            keys = stance_keys(aim, facing)
            if crouch:
                keys.discard('down')
            if b.shot_cd <= 0 and fire not in self.r.held:
                keys.add(fire)
            self.tick(keys)
        self.release_stance()
        return False

    def pop_block(self, c, r):
        """Rolls up next to a cracked block and drops popcorn by it."""
        g = self.grid()
        rg = self.graph(g)
        here = nav.node_of(self.settle())
        best = None
        for node, cost in rg.reachable(here).items():
            if node[0] == 'exit' or not node[2]:
                continue
            pc, pr = node[0], node[1]
            if abs(pc - c) + abs(pr - r) == 1:
                if best is None or cost < best[0]:
                    best = (cost, node)
        if best is None:
            self.log('no way to pop', (c, r))
            return False
        node = best[1]
        if not self.goto(lambda n, node=node: n == node, nav.dist_heuristic(node[0] * 16 + 8, node[1] * 16), desc='beside the cracked block'):
            return False
        for attempt in range(3):
            b = self.basil()
            if not b.ball:
                return False
            self.tick(())
            self.tick(('fire',))
            for _ in range(80):
                self.tick(())
                if self.grid().cell(c, r) != 'x':
                    self.settle()
                    return True
        return False
