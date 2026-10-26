"""Fighting: which enemies are about, and shooting them - standing or crouched, aimed any of
eight ways, leading them a little - using the shots' own flight (scripts/shot.bs)."""
import math
import re

import sim

AIMS = ('fwd', 'up', 'diag_up', 'diag_down', 'down')


def muzzle(x, y, facing, aim, crouch, grounded=True):
    low = 4 if crouch else 0
    if aim == 'up':
        return x + facing * 3, y - 18 + low
    if aim == 'diag_up':
        return x + facing * 10, y - 6 + low
    if aim == 'diag_down':
        return x + facing * 10, y + 9 + low
    if aim == 'down':
        return x + facing * 3, y + 12 + low
    return x + facing * 14, y - 2 + low


def aim_dir(aim, facing):
    r = math.sqrt(0.5)
    if aim == 'up':
        return 0.0, -1.0
    if aim == 'down':
        return 0.0, 1.0
    if aim == 'diag_up':
        return facing * r, -r
    if aim == 'diag_down':
        return facing * r, r
    return float(facing), 0.0


def stance_keys(aim, facing):
    k = {'aim'}
    side = 'right' if facing > 0 else 'left'
    if aim == 'fwd':
        k.add(side)
    elif aim == 'up':
        k.add('up')
    elif aim == 'diag_up':
        k |= {side, 'up'}
    elif aim == 'diag_down':
        k |= {side, 'down'}
    elif aim == 'down':
        k.add('down')
    return k


class Foe:
    __slots__ = ('id', 'scene', 'name', 'x', 'y', 'vx', 'vy', 'hp', 'half', 'visible', 'armored', 'shield', 'facing',
                 'frozen', 'brain', 'boss')

    def __repr__(self):
        return '%s#%d(%.0f,%.0f hp%s)' % (self.name, self.id, self.x, self.y, self.hp)


BODY_RE = re.compile(r'(\w+): (\([^)]*\)|[^,()]+)')


def parse_body(txt):
    out = {}
    for k, v in BODY_RE.findall(txt):
        v = v.strip()
        if v.startswith('('):
            a, b = v[1:-1].split(',')
            out[k] = (float(a), float(b))
        elif v in ('true', 'false'):
            out[k] = v == 'true'
        else:
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out


class Fighting:
    """Mixed into Bot."""

    def foes(self, within=None):
        """The enemies in the room now (with a speed worked out from where they were)."""
        seen = getattr(self, '_foe_last', {})
        if not hasattr(self, '_foe_prev'):
            self._foe_prev = {}
        out = []
        b = None
        if within is not None:
            b = self.basil()
        bosses = set(i for (s, i, nm, x, y) in self.r.tagged('boss'))
        for (s, i, nm, x, y) in self.r.tagged('enemy'):
            if i in bosses:
                continue
            if within is not None and math.hypot(x - b.x, y - b.y) > within:
                continue
            vals = self.r.vars(s, i, ['b', 'frozen_t', 'brain'])
            body = parse_body(vals[0]) if vals[0] != '?' else {}
            f = Foe()
            f.id, f.scene, f.name, f.x, f.y = i, s, nm, x, y
            last = seen.get(i)
            if last and last[0] == self.r.frames:
                last = getattr(self, '_foe_prev', {}).get(i)
            if last and self.r.frames - last[0] <= 2:
                n = self.r.frames - last[0]
                f.vx, f.vy = (x - last[1]) / n, (y - last[2]) / n
            else:
                f.vx = f.vy = 0.0
            f.hp = body.get('hp', 1)
            f.half = body.get('half', (7.0, 7.0))
            f.visible = body.get('visible', True)
            f.armored = body.get('armored', False)
            f.shield = body.get('shield', False)
            f.facing = body.get('facing', 1)
            try:
                f.frozen = float(vals[1]) > 0
            except ValueError:
                f.frozen = False
            f.brain = vals[2]
            f.boss = False
            if i in seen and seen[i][0] < self.r.frames:
                getattr(self, '_foe_prev', {})[i] = seen[i]
            seen[i] = (self.r.frames, x, y)
            out.append(f)
        self._foe_last = seen
        return out

    def shot_hits(self, g, mx, my, dx, dy, foe, kind='pepper', delay=1):
        """Does a shot fired now from (mx, my) going (dx, dy) hit the foe (moving on as it does)?
        Returns the steps it takes, or None."""
        speed, rng = 5.0, 88.0
        through = kind in ('garlic', 'garlic_frost')
        if through:
            rng = 132.0
        if kind == 'rocket':
            return self.rocket_hits(g, mx, my, dx, dy, foe)
        x, y, gone = mx, my, 0.0
        for k in range(1, 40):
            fx = foe.x + foe.vx * (k + delay)
            fy = foe.y + foe.vy * (k + delay)
            for _ in range(2):
                x += dx * speed / 2
                y += dy * speed / 2
                gone += speed / 2
                t = g.tile_at(x, y)
                if t == 'Y' or t == 'c':
                    return None
                if abs(x - fx) < foe.half[0] + 2 and abs(y - fy) < foe.half[1] + 2:
                    return k
                if not through and t in g.solid_chars:
                    return None
            if gone >= rng:
                return None
        return None

    def rocket_hits(self, g, mx, my, dx, dy, foe):
        x, y, speed = mx, my, 2.0
        for k in range(1, 60):
            speed = min(speed + 0.5, 6.5)
            fx = foe.x + foe.vx * (k + 1)
            fy = foe.y + foe.vy * (k + 1)
            for _ in range(3):
                x += dx * speed / 3
                y += dy * speed / 3
                t = g.tile_at(x, y)
                if t in ('Y', 'r', 'c'):
                    return None
                if abs(x - fx) < foe.half[0] + 2 and abs(y - fy) < foe.half[1] + 2:
                    return k
                if t in g.solid_chars:
                    return None
            if x < -16 or y < -16 or x > g.W + 16 or y > g.H + 16:
                return None
        return None

    def can_hurt(self, foe, kind):
        if foe.frozen:
            return kind in ('rocket',)
        if foe.armored and kind not in ('rocket', 'frost', 'garlic_frost'):
            return False
        return True

    def best_shot(self, b, g, foes, kind, allow_crouch=True):
        """(foe, aim, facing, crouch, steps) for a shot that hits from where he is, or None."""
        best = None
        stances = [(False,)] + ([(True,)] if allow_crouch else [])
        tries = getattr(self, '_shots_at', {})
        for foe in foes:
            if not foe.visible or not self.can_hurt(foe, kind):
                continue
            t = tries.get(foe.id)
            if t and t[0] >= 8 and t[1] == foe.hp:
                continue                                  # (eight shots and no harm done: leave it be)
            for (crouch,) in stances:
                for facing in (b.facing, -b.facing):
                    for aim in AIMS:
                        if crouch and aim == 'down':
                            continue
                        if foe.shield and kind not in ('rocket', 'frost', 'garlic_frost') and \
                                aim not in ('up', 'down') and facing == -foe.facing:
                            continue                      # (pepper at its face bounces off a shield; from above or behind, not)
                        mx, my = muzzle(b.x, b.y, facing, aim, crouch)
                        dx, dy = aim_dir(aim, facing)
                        k = self.shot_hits(g, mx, my, dx, dy, foe, kind)
                        if k is None:
                            continue
                        cost = k + (0 if crouch == b.crouch else 6) + (0 if facing == b.facing else 1)
                        if best is None or cost < best[-1]:
                            best = (foe, aim, facing, crouch, cost)
        return best

    def beam(self):
        inv = self.inv()
        it = inv['items']
        if 'garlic' in it and 'frost' in it and inv['frost_on']:
            return 'garlic_frost'
        if 'garlic' in it:
            return 'garlic'
        if 'frost' in it and inv['frost_on']:
            return 'frost'
        return 'pepper'

    def engage(self, radius=120, max_frames=400, only=None, use_rockets=False, quiet=6):
        """Shoots enemies near him until none are left that a shot from here can reach.
        Returns whether it shot anything."""
        shot_any = False
        idle = 0
        for _ in range(max_frames):
            if self.state() != 'play' or len(self.r.scenes()) > 1:
                return shot_any
            b = self.basil()
            if b.hurt_t > 0 or not b.grounded or b.ball:
                self.release_stance()
                self.tick(())
                continue
            foes = [f for f in self.foes(within=radius) if (only is None or only(f))]
            if not foes:
                break
            g = self.grid()
            kind = self.beam()
            shot = self.best_shot(b, g, foes, kind)
            if shot is None and (use_rockets or any(f.shield or f.armored for f in foes)) and self.inv()['rockets'] > 0:
                shot = self.best_shot(b, g, foes, 'rocket')
                if shot:
                    kind = 'rocket'
            if shot is None:
                idle += 1
                if idle > quiet:
                    break
                self.release_stance()
                self.tick(())
                continue
            idle = 0
            foe, aim, facing, crouch, _ = shot
            if crouch and not b.crouch:
                self.tick(())
                self.tick(('down',))
                continue
            if not crouch and b.crouch:
                self.tick(())
                self.tick(('up',))
                continue
            keys = stance_keys(aim, facing)
            n_shots = len(self.r.tagged('pepper'))
            fire = 'rocket' if kind == 'rocket' else 'fire'
            if b.shot_cd <= 0 and (kind == 'rocket' or n_shots < 3) and fire not in self.r.held:
                keys = keys | {fire}
                shot_any = True
                tries = self.__dict__.setdefault('_shots_at', {})
                t = tries.get(foe.id)
                tries[foe.id] = (t[0] + 1, t[1]) if t and t[1] == foe.hp else (1, foe.hp)
            self.tick(keys)
        self.release_stance()
        return shot_any

    def release_stance(self):
        b = self.basil()
        if not self.r.held and not b.crouch:
            return
        self.tick(())
        if b.crouch:
            self.tick(('up',))
            self.tick(())
