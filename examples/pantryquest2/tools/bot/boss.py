"""Fighting the guardians: every frame, try a few dozen ways of moving over the next
moment (in the sim), with the guardian and everything it has thrown moved on as they will
move (a small model of each guardian's script), and take the first frame of the safest.
Shoot whenever the guardian can be hurt and a shot from here reaches it."""
import math

import nav
import sim
from fight import muzzle, aim_dir, stance_keys, AIMS, Foe

H = 45                     # frames looked ahead
DT = 1.0 / 60.0


class Hz:
    """Something that hurts to touch: its box (half sizes) frame by frame from now."""
    __slots__ = ('path', 'hx', 'hy', 'name')

    def __init__(self, path, hx, hy, name=''):
        self.path, self.hx, self.hy, self.name = path, hx, hy, name


def make_plans():
    plans = [('idle', [set()] * H)]
    for d in (-1, 1):
        for k in (4, 10, 20, H):
            plans.append(('walk%+d_%d' % (d, k), [nav.held_of(d)] * k + [set()] * (H - k)))
    for d in (-1, 0, 1):
        for j in (5, 12, 22, H):
            plans.append(('jump%+d_%d' % (d, j), [nav.held_of(d, f < j) for f in range(H)]))
        if d:
            plans.append(('jumpwait%+d' % d, [nav.held_of(d if f >= 10 else 0, True) for f in range(H)]))
            plans.append(('jumpback%+d' % d, [nav.held_of(d if f < 12 else -d, True) for f in range(H)]))
    return plans


PLANS = make_plans()


class BossFight:
    """Mixed into Bot. A guardian's model is a function (vars, basil, k) -> [(x, y, hx, hy)] of
    the boxes that hurt k frames from now."""

    def projectiles(self, g):
        """Spit (seeds, bones, scoops, fire...): straight flights with gravity, stopping at walls."""
        out = []
        for (s, i, nm, x, y) in self.r.tagged('spit'):
            v = self.r.vars(s, i, ['vel', 'grav', 'life'])
            try:
                vx, vy = parse_vec(v[0])
                grav = float(v[1])
            except Exception:
                continue
            path = []
            px, py, pvy = x, y, vy
            for k in range(H + 1):
                path.append((px, py))
                pvy += grav
                px += vx
                py += pvy
                if g.tile_at(px, py) in g.solid_chars:
                    path.append(None)
                    break
            out.append(Hz(path, 3, 3, 'spit'))
        return out

    def minion_hazards(self):
        """Ordinary enemies about (the Count's spores, say): moving on in a straight line."""
        out = []
        self.threats(400)
        for f in self.foes(within=200):
            if f.frozen or not f.visible:
                continue
            out.append(Hz([(f.x + f.vx * k, f.y + f.vy * k) for k in range(H + 1)], f.half[0], f.half[1], 'enemy'))
        return out

    def basil_hit(self, b, hz, k, margin=3):
        """Does Basil's box (as b is, k frames on) touch the hazard then, or a frame either side?"""
        mx, my = b.middle()
        w, h = sim.SIZES[b.pose()]
        for kk in (k - 1, k, k + 1):
            if kk < 0 or kk >= len(hz.path) or hz.path[kk] is None:
                continue
            x, y = hz.path[kk]
            if abs(x - mx) < hz.hx + w / 2 + margin and abs(y - my) < hz.hy + h / 2 + margin:
                return True
        return False

    def choose(self, b, g, hazards, prefer, has):
        """The best plan: (score, name, keys)."""
        sched = self.r.steps_ahead(H)
        best = None
        prev0 = set(k for k in self.r.held if k in ('left', 'right', 'jump', 'up', 'down'))
        for name, keys in PLANS:
            s = b.copy()
            prev = prev0
            score = 0.0
            for k in range(H):
                held = keys[k]
                pressed = [x for x in held if x not in prev]
                hc = s.hurt_count
                sim.frame(s, held, pressed, sched[k], g, has)
                prev = held
                if s.hurt_count > hc:
                    score += 600 * (H - k + 5)
                    break
                hit = False
                for hz in hazards:
                    if self.basil_hit(s, hz, k + 1):
                        hit = True
                        break
                if hit:
                    score += 1000 * (H - k + 5)
                    break
            score += prefer(s)
            if name.startswith('jump') and not b.grounded:
                score += 1.0
            if best is None or score < best[0]:
                best = (score, name, keys[0])
        return best

    def fight_loop(self, boss_model, prefer, can_hurt, boss_box, done, limit=45000, kinds=None):
        """The guardian fight, frame by frame, until done()."""
        has = self.has_fn()
        for f in range(limit):
            if done():
                self.tick(())
                return True
            if self.state() != 'play' or len(self.r.scenes()) > 1:
                self.wait_play()
                continue
            b = self.basil()
            g = self.grid()
            hazards = boss_model(b, g) + self.projectiles(g) + self.minion_hazards()
            if f % 60 == 0:
                bv = self.g.vars('Game', ['boss_hp', 'room_id'])
                self.log('  boss fight: basil', (b.x, b.y), 'energy', self.inv()['energy'], 'boss hp', bv['boss_hp'],
                         'room', bv['room_id'], 'hazards', [(h.name, h.path[0]) for h in hazards][:4])
            score, name, keys = self.choose(b, g, hazards, prefer, has)
            keys = set(keys)
            # a shot, if one reaches the guardian from here
            box = boss_box()
            if box and can_hurt() and b.shot_cd <= 0 and b.hurt_t <= 0:
                shot = self.shot_at_box(b, g, box, keys, has)
                if shot is not None:
                    keys = shot
            self.tick(keys)
        return False

    def shot_at_box(self, b, g, box, keys, has):
        """Keys that fire a shot at a box (x, y, hx, hy, vx, vy) this frame, keeping to the plan's
        movement; or None."""
        foe = Foe()
        foe.x, foe.y, foe.half = box[0], box[1], (box[2], box[3])
        foe.vx, foe.vy = box[4], box[5]
        kind = self.beam()
        fire = 'fire'
        weapon = getattr(self, 'boss_weapon', None)
        want = weapon() if weapon else None
        if want is None and self.inv()['rockets'] > 3 and 'rocket' not in self.r.held:
            want = 'rocket'                              # (hot sauce hurts far more; keep a few for doors)
        if want == 'rocket':
            if self.inv()['rockets'] <= 0 or 'rocket' in self.r.held:
                return None
            fire, kind = 'rocket', 'rocket'
        elif len(self.r.tagged('pepper')) >= 3 or 'fire' in self.r.held:
            if 'rocket' in getattr(self, 'boss_rockets', ()) and self.inv()['rockets'] > 0 and 'rocket' not in self.r.held:
                fire, kind = 'rocket', 'rocket'
            else:
                return None
        moving = 'left' in keys or 'right' in keys
        if b.grounded and not moving and 'jump' not in keys:
            # planted: any stance
            for aim in AIMS:
                for facing in (b.facing, -b.facing):
                    mx, my = muzzle(b.x, b.y, facing, aim, b.crouch)
                    dx, dy = aim_dir(aim, facing)
                    if self.shot_hits(g, mx, my, dx, dy, foe, kind) is not None:
                        k = stance_keys(aim, facing)
                        k.add(fire)
                        return k
            return None
        # moving or in the air: forward, or up / down with the keys that don't change the movement
        facing = 1 if 'right' in keys else -1 if 'left' in keys else b.facing
        options = [('fwd', set())]
        if not b.grounded:
            options.append(('down', {'down'}) if not moving else ('diag_down', {'down'}))
        options.append(('up' if not moving else 'diag_up', {'up'}))
        for aim, extra in options:
            mx, my = muzzle(b.x, b.y, facing, aim, False)
            dx, dy = aim_dir(aim, facing)
            if self.shot_hits(g, mx, my, dx, dy, foe, kind) is not None:
                return set(keys) | extra | {fire}
        return None

    def boss_entity(self, name):
        for (s, i, nm, x, y) in self.r.tagged('boss'):
            if nm == name:
                return (s, i, x, y)
        return None


def parse_vec(t):
    t = t.strip()
    a, b = t[1:-1].split(',')
    return float(a), float(b)


# ---- Sir Stale --------------------------------------------------------------------------------------
def stale_model(bot):
    last = {}

    def model(b, g):
        e = bot.boss_entity('Boss')
        if not e:
            return []
        s, i, x, y = e
        v = bot.r.vars(s, i, ['state', 't', 'facing', 'vel', 'next_move'])
        state, t, facing = v[0], float(v[1]), int(float(v[2]))
        vx, vy = parse_vec(v[3])
        nm = int(float(v[4]))
        path = []
        px, py = x, y
        bx = b.x
        for k in range(H + 1):
            path.append((px, py))
            t += DT
            if state == 'walk':
                facing = 1 if bx > px else -1
                px = min(max(px + facing * 0.7, 32), 480)
                if t > 1.4:
                    nm = (nm + 1) % 3
                    state = ['windup', 'leap', 'throw'][nm]
                    t = 0
            elif state == 'windup':
                if t > 0.6:
                    state, t = 'charge', 0
            elif state == 'charge':
                nx = px + facing * 4
                if nx < 32 or nx > 480:
                    px = min(max(nx, 32), 480)
                    state, t = 'dizzy', 0
                else:
                    px = nx
            elif state == 'leap':
                if t < 0.3:
                    facing = 1 if bx > px else -1
                    vx, vy = max(-3, min(3, (bx - px) / 50)), -5
                else:
                    vy += 0.25
                    px = min(max(px + vx, 32), 480)
                    py += vy
                    if vy > 0 and py + 24 >= 208:
                        py = 184
                        state, t = 'walk', 0
            elif state in ('dizzy', 'throw'):
                if (state == 'dizzy' and t > 1.8) or (state == 'throw' and t > 1.0):
                    state, t = 'walk', 0
        return [Hz(path, 12, 20, 'stale')]
    return model


def fight_stale(bot):
    """Sir Stale: his crust turns pepper except while he's dizzy (after charging into a wall)
    or throwing. Jump his charges and leaps, keep away from the walls, shoot him when he's open."""
    model = stale_model(bot)
    info = {}

    def look():
        e = bot.boss_entity('Boss')
        if not e:
            info.clear()
            return None
        v = bot.r.vars(e[0], e[1], ['state'])
        info.update(state=v[0], x=e[2], y=e[3])
        return info

    def can_hurt():
        i = look()
        return bool(i) and i['state'] in ('dizzy', 'throw')

    def box():
        i = look()
        return (i['x'], i['y'], 12, 20, 0, 0) if i else None

    def prefer(s):
        i = info or look()
        if not i:
            return 0
        dx = abs(s.x - i['x'])
        p = 0.0
        if i.get('state') in ('dizzy', 'throw'):
            p += abs(dx - 64) * 0.3
        else:
            p += max(0, 110 - dx) * 0.4
        # don't get cornered: the wall behind him (away from the knight) should be far off
        room_left, room_right = 24, 488
        behind = (s.x - room_left) if s.x < i['x'] else (room_right - s.x)
        p += max(0, 120 - behind) * 0.5
        return p

    def done():
        return 'boss:stale' in bot.flags()

    bot.log('fighting Sir Stale')
    ok = bot.fight_loop(model, prefer, can_hurt, box, done)
    bot.wait_play()
    return ok


# ---- the Abominable Sundae ---------------------------------------------------------------------------
def sundae_model(bot):
    def model(b, g):
        e = bot.boss_entity('Boss')
        if not e:
            return []
        s, i, x, y = e
        v = bot.r.vars(s, i, ['state', 't', 'facing', 'vel', 'moves', 'bounces', 'ground', 'right_wall', 'chill_t'])
        state, t, facing = v[0], float(v[1]), int(float(v[2]))
        vy = parse_vec(v[3])[1]
        moves, bounces = int(float(v[4])), int(float(v[5]))
        ground, right = float(v[6]), float(v[7])
        chill = float(v[8])
        path = []
        px, py = x, y
        for k in range(H + 1):
            path.append((px, py))
            dt = DT * (0.3 if chill > 0 else 1)
            chill -= DT
            t += dt
            if state == 'idle':
                facing = 1 if b.x > px else -1
                if t > 0.9:
                    moves += 1
                    state = ['roll', 'throw', 'stomp'][moves % 3]
                    t = 0
            elif state == 'throw':
                if t > 1.5:
                    state, t = 'idle', 0
            elif state == 'stomp':
                if t >= 0.25:
                    if t - dt < 0.25:
                        vy = -4.5
                    vy += 0.25
                    py += vy
                    if py >= ground:
                        py = ground
                        state, t = 'idle', 0
            elif state == 'roll':
                if t >= 0.4:
                    px += facing * 3.5
                    if px < 40 or px > right:
                        px = min(max(px, 40), right)
                        facing = -facing
                        bounces += 1
                        if bounces >= 3:
                            state, t = 'idle', 0
        return [Hz(path, 20, 22, 'sundae')]
    return model


def fight_sundae(bot):
    """The Abominable Sundae: it can be hurt except while rolling. Jump its rolls, dodge the
    scoops and icicles, and keep shooting (Frost Sugar slows it)."""
    model = sundae_model(bot)
    info = {}

    def look():
        e = bot.boss_entity('Boss')
        if not e:
            info.clear()
            return None
        info.update(state=bot.r.vars(e[0], e[1], ['state'])[0], x=e[2], y=e[3])
        return info

    def can_hurt():
        i = look()
        return bool(i) and i['state'] not in ('roll', 'intro', 'sleep', 'dying')

    def box():
        i = look()
        return (i['x'], i['y'], 20, 22, 0, 0) if i else None

    def prefer(s):
        i = info or look()
        if not i:
            return 0
        dx = abs(s.x - i['x'])
        p = abs(dx - 90) * 0.3
        behind = (s.x - 24) if s.x < i['x'] else (488 - s.x)
        p += max(0, 100 - behind) * 0.4
        return p

    bot.set_frost(True)
    bot.log('fighting the Abominable Sundae')
    ok = bot.fight_loop(model, prefer, can_hurt, box, lambda: 'boss:sundae' in bot.flags())
    bot.wait_play()
    bot.set_frost(False)
    return ok


# ---- Doctor Brine and Frankenfurter ------------------------------------------------------------------
def brine_model(bot):
    def model(b, g):
        out = []
        for (s, i, nm, x, y) in bot.r.tagged('boss'):
            if nm == 'Boss':                                     # Frankenfurter
                v = bot.r.vars(s, i, ['state', 't', 'facing', 'right_wall'])
                state, t, facing, right = v[0], float(v[1]), int(float(v[2])), float(v[3])
                if state in ('sleep', 'down', 'done', 'dying'):
                    continue
                path = []
                px = x
                for k in range(H + 1):
                    path.append((px, y))
                    t += DT
                    if state == 'walk':
                        facing = 1 if b.x > px else -1
                        px = min(max(px + facing * 0.6, 32), right)
                        if t > 1.6:
                            state = 'lunge' if abs(b.x - px) < 70 else 'lob'
                            t = 0
                    elif state == 'lunge':
                        if 0.35 < t < 0.75:
                            px = min(max(px + facing * 3.2, 32), right)
                        if t > 1.2:
                            state, t = 'walk', 0
                    elif state == 'lob' and t > 1.0:
                        state, t = 'walk', 0
                out.append(Hz(path, 14, 24, 'monster'))
            elif nm == 'Doctor':
                v = bot.r.vars(s, i, ['state', 't', 'zap_x'])
                state, t, zx = v[0], float(v[1]), float(v[2])
                if state == 'zap':
                    k_hit = max(0, int((0.8 - t) * 60))
                    path = [None] * (H + 1)
                    for k in range(max(0, k_hit - 2), min(H + 1, k_hit + 3)):
                        path[k] = (zx, b.y)
                    out.append(Hz(path, 8, 400, 'bolt'))
                elif state == 'fight':
                    mx, my = b.middle()
                    path = []
                    px, py = x, y
                    for k in range(H + 1):
                        path.append((px, py))
                        dx, dy = mx - px, my - py
                        d = math.hypot(dx, dy) or 1
                        px += dx / d * 0.7
                        py = min(max(py + dy / d * 0.7, 80), 180)
                    out.append(Hz(path, 10, 14, 'doctor'))
        return out
    return model


def fight_brine(bot):
    """Frankenfurter first (any shot hurts it; keep away from its lunges, dodge the mustard and
    the doctor's lightning), then the doctor: three rockets to crack his jar, then pepper."""
    model = brine_model(bot)
    st = {}

    def look():
        st.clear()
        for (s, i, nm, x, y) in bot.r.tagged('boss'):
            if nm == 'Boss':
                st['monster'] = (bot.r.vars(s, i, ['state'])[0], x, y)
            elif nm == 'Doctor':
                v = bot.r.vars(s, i, ['state', 'jar'])
                st['doctor'] = (v[0], x, y, int(float(v[1])))
        return st

    def target():
        look()
        m = st.get('monster')
        if m and m[0] in ('walk', 'lunge', 'lob'):
            return ('monster', m[1], m[2], 14, 24)
        d = st.get('doctor')
        if d and d[0] == 'fight':
            return ('doctor', d[1], d[2], 10, 14)
        return None

    def can_hurt():
        return target() is not None

    def box():
        t = target()
        return (t[1], t[2], t[3], t[4], 0, 0) if t else None

    def weapon():
        d = st.get('doctor')
        if d and d[0] == 'fight' and d[3] > 0:
            return 'rocket'
        return None

    def prefer(s):
        t = target() if not st else None
        m = st.get('monster')
        p = 0.0
        if m and m[0] in ('walk', 'lunge', 'lob'):
            dx = abs(s.x - m[1])
            p += abs(dx - 100) * 0.3
            behind = (s.x - 24) if s.x < m[1] else (488 - s.x)
            p += max(0, 90 - behind) * 0.4
        d = st.get('doctor')
        if d and d[0] == 'fight':
            p += abs(math.hypot(s.x - d[1], s.y - d[2]) - 70) * 0.2
        return p

    bot.boss_weapon = weapon
    bot.set_frost(False)
    bot.log('fighting Doctor Brine')
    try:
        ok = bot.fight_loop(model, prefer, can_hurt, box, lambda: 'boss:brine' in bot.flags())
    finally:
        bot.boss_weapon = None
    bot.wait_play()
    return ok


# ---- the Flambé Wyrm --------------------------------------------------------------------------------
def wyrm_model(bot):
    hist = []                                       # the head's positions, frame by frame (while leaping)

    def model(b, g):
        e = bot.boss_entity('Boss')
        if not e:
            return []
        s, i, x, y = e
        v = bot.r.vars(s, i, ['state', 'vel'])
        state = v[0]
        vx, vy = parse_vec(v[1])
        if state != 'leap':
            hist.clear()
            return []
        if not hist or hist[-1] != (x, y):
            hist.append((x, y))
        head = []
        px, py = x, y
        for k in range(H + 1):
            head.append((px, py))
            vy += 0.13
            px += vx
            py += vy
        out = [Hz(head, 12, 12, 'head')]
        for n in range(1, 7):
            path = []
            for k in range(H + 1):
                j = k - 5 * n
                if j >= 0:
                    path.append(head[j])
                elif len(hist) + j - 1 >= 0:
                    path.append(hist[len(hist) + j - 1])
                else:
                    path.append(None)
            out.append(Hz(path, 7, 7, 'body'))
        return out
    return model


def fight_wyrm(bot):
    """The Flambé Wyrm: only its head, while it leaps, can be hurt. Stay on the dry landing by
    the door, dodge the head, its body and its fire, and shoot the head."""
    model = wyrm_model(bot)
    st = {}

    def look():
        e = bot.boss_entity('Boss')
        st.clear()
        if e:
            v = bot.r.vars(e[0], e[1], ['state', 'vel'])
            st.update(state=v[0], x=e[2], y=e[3], vel=parse_vec(v[1]))
        return st

    def can_hurt():
        return look().get('state') == 'leap'

    def box():
        i = look()
        if not i:
            return None
        return (i['x'], i['y'], 12, 12, i['vel'][0], i['vel'][1])

    def prefer(s):
        p = 0.0
        if s.in_liquid or s.hurt_count:
            p += 200
        p += abs(s.x - 428) * 0.05                      # (on the dry landing; it wakes once he's past x 440)
        return p

    bot.set_frost(False)
    bot.log('fighting the Flambe Wyrm')
    ok = bot.fight_loop(model, prefer, can_hurt, box, lambda: 'boss:wyrm' in bot.flags())
    bot.wait_play()
    return ok


# ---- Count Fromage, and the Big Cheese -------------------------------------------------------------
def count_model(bot):
    def model(b, g):
        e = bot.boss_entity('Boss')
        if not e:
            return []
        s, i, x, y = e
        v = bot.r.vars(s, i, ['state', 't', 'vel', 'right_wall'])
        state, t = v[0], float(v[1])
        vx, vy = parse_vec(v[2])
        right = float(v[3])
        if state in ('appear', 'cast'):
            k_end = int(max(0, (0.4 - t if state == 'appear' else 0) + (1.3 if state == 'appear' else 1.3 - t)) * 60)
            path = [(x, y) if k <= k_end else None for k in range(H + 1)]
            return [Hz(path, 10, 22, 'count')]
        if state == 'big':
            path = []
            px, py = x, y
            for k in range(H + 1):
                path.append((px, py))
                px += vx
                py += vy
                if px < 48 or px > right - 8:
                    vx = -vx
                if py < 60 or py > 176:
                    vy = -vy
            return [Hz(path, 26, 26, 'cheese')]
        return []
    return model


def fight_count(bot):
    """Count Fromage: hurt him while he's there (appearing or casting), dodge his fire; then the
    Big Cheese, bouncing about the room: only garlic hurts it (Frost Sugar off: garlic alone)."""
    model = count_model(bot)
    st = {}

    def look():
        e = bot.boss_entity('Boss')
        st.clear()
        if e:
            v = bot.r.vars(e[0], e[1], ['state', 'vel'])
            st.update(state=v[0], x=e[2], y=e[3], vel=parse_vec(v[1]))
        return st

    def can_hurt():
        return look().get('state') in ('appear', 'cast', 'big')

    def box():
        i = look()
        if not i:
            return None
        if i['state'] == 'big':
            return (i['x'], i['y'], 26, 26, i['vel'][0], i['vel'][1])
        return (i['x'], i['y'], 10, 22, 0, 0)

    def prefer(s):
        i = st or look()
        p = 0.0
        if i and i.get('state') == 'big':
            p += max(0, 90 - math.hypot(s.x - i['x'], s.y - i['y'])) * 0.5
        p += abs(s.x - 256) * 0.02
        return p

    def weapon():                                      # (hot sauce for the Count; only garlic hurts the Big Cheese)
        return 'beam' if (st or look()).get('state') == 'big' or bot.inv()['rockets'] <= 0 else 'rocket'

    bot.boss_weapon = weapon
    bot.set_frost(False)
    bot.log('fighting Count Fromage')
    try:
        ok = bot.fight_loop(model, prefer, can_hurt, box, lambda: 'boss:count' in bot.flags())
    finally:
        bot.boss_weapon = None
    bot.wait_play()
    return ok
