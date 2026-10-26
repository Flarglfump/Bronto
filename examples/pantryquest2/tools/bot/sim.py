"""Basil's movement, step for step as scripts/basil.bs does it, on the room as it is now
(its tile map read from the game, and any frozen enemies to stand on). The bot plans with
it; given the same keys it lands on the same pixel as the game."""
import math

RUN, GRAVITY, JUMP, HIGH_JUMP, CUT, MAX_FALL = 1.5, 0.25, 5.4, 7.0, 2.0, 5.0
STAND, CROUCH, BALL = 0, 1, 2
SIZES = {STAND: (12, 26), CROUCH: (12, 18), BALL: (12, 12)}
DT = 1.0 / 60.0


def rnd(v):
    return math.floor(v + 0.5)


def sgn(v):
    return (v > 0) - (v < 0)


class Grid:
    """The room's solid map (and front map, for liquid), from the game's tile maps."""

    def __init__(self, tm, front=None, platforms=()):
        self.cols, self.rows, self.ts = tm['cols'], tm['rows'], tm['ts']
        self.x0, self.y0 = tm['x0'], tm['y0']
        self.solid_chars = set(tm['solid'])
        self.t = [row.ljust(self.cols)[:self.cols] for row in tm['rows_text']]
        self.front = None
        if front:
            self.front = [row.ljust(front['cols'])[:front['cols']] for row in front['rows_text']]
            self.fx0, self.fy0, self.fcols, self.frows = front['x0'], front['y0'], front['cols'], front['rows']
        self.platforms = list(platforms)            # frozen enemies: their middles
        self.memo = {}
        self.W = self.cols * self.ts                 # the room's size in pixels
        self.H = self.rows * self.ts

    def copy_with(self, platforms):
        g = Grid.__new__(Grid)
        g.__dict__.update(self.__dict__)
        g.platforms = list(platforms)
        g.memo = {}
        return g

    def set_tile(self, c, r, ch):
        row = self.t[r]
        self.t[r] = row[:c] + ch + row[c + 1:]
        self.memo = {}

    def cell(self, c, r):
        if 0 <= r < self.rows and 0 <= c < self.cols:
            return self.t[r][c]
        return ' '

    def solid_cell(self, c, r):
        return self.cell(c, r) in self.solid_chars

    def tile_at(self, x, y):
        c = math.floor((x - self.x0) / self.ts)
        r = math.floor((y - self.y0) / self.ts)
        return self.cell(c, r)

    def front_at(self, x, y):
        if not self.front:
            return ' '
        c = math.floor((x - self.fx0) / self.ts)
        r = math.floor((y - self.fy0) / self.ts)
        if 0 <= r < self.frows and 0 <= c < self.fcols:
            return self.front[r][c]
        return ' '

    def box_solid(self, cx, cy, w, h):
        """tile_box_solid: does a box overlap a solid cell (edges touching don't count)?"""
        x0, x1 = cx - w / 2 - self.x0, cx + w / 2 - self.x0
        y0, y1 = cy - h / 2 - self.y0, cy + h / 2 - self.y0
        c0 = max(0, math.floor(x0 / self.ts))
        c1 = min(self.cols - 1, math.ceil(x1 / self.ts) - 1)
        r0 = max(0, math.floor(y0 / self.ts))
        r1 = min(self.rows - 1, math.ceil(y1 / self.ts) - 1)
        sc = self.solid_chars
        for r in range(r0, r1 + 1):
            row = self.t[r]
            for c in range(c0, c1 + 1):
                if row[c] in sc:
                    return True
        return False

    def blocked(self, x, y, pose):
        k = (x, y, pose)
        v = self.memo.get(k)
        if v is None:
            w, h = SIZES[pose]
            cx, cy = x, y + 16 - h / 2
            v = self.box_solid(cx, cy, w, h)
            if not v:
                for (px, py) in self.platforms:
                    if abs(cx - px) < w / 2 + 8 and abs(cy - py) < h / 2 + 8:
                        v = True
                        break
            self.memo[k] = v
        return v


EDGES = ('up', 'down', 'jump', 'ball', 'fire', 'rocket')


class Inputs:
    """The keys for one frame: what's held, and what went down this frame."""
    __slots__ = ('left', 'right', 'up', 'down', 'jump', 'aim', 'ball', 'fire', 'rocket', 'p_up', 'p_down', 'p_jump', 'p_ball', 'p_fire', 'p_rocket')

    def __init__(self, held=(), pressed=()):
        for k in self.__slots__:
            setattr(self, k, False)
        for k in held:
            setattr(self, k, True)
        for k in pressed:
            if k in EDGES:
                setattr(self, 'p_' + k, True)


class Basil:
    """His state: what basil.bs keeps from one step to the next."""
    __slots__ = ('x', 'y', 'vx', 'vy', 'sx', 'sy', 'facing', 'grounded', 'ball', 'crouch', 'spin', 'hurt_t', 'safe_t',
                 'lifted', 'in_liquid', 'lava_t', 'shot_cd', 'dead', 'hurt_count', 'left_room')

    def __init__(self, **kw):
        for k in self.__slots__:
            setattr(self, k, 0)
        self.dead = False
        self.left_room = None
        for k, v in kw.items():
            setattr(self, k, v)

    def copy(self):
        b = Basil.__new__(Basil)
        for k in self.__slots__:
            setattr(b, k, getattr(self, k))
        return b

    def pose(self):
        return BALL if self.ball else CROUCH if self.crouch else STAND

    def middle(self):
        h = SIZES[self.pose()][1]
        return self.x, self.y + 16 - h / 2

    def key(self):
        return (self.x, self.y, self.vx, self.vy, self.sx, self.sy, self.grounded, self.ball, self.crouch, self.spin,
                self.lifted, self.hurt_t > 0)

    def __repr__(self):
        return 'Basil(%s)' % ', '.join('%s=%r' % (k, getattr(self, k)) for k in ('x', 'y', 'vx', 'vy', 'sx', 'sy', 'grounded', 'ball', 'crouch', 'spin', 'facing'))


def step(b, inp, g, has):
    """One step of basil.bs's update() (in play). `has(item)`: what he has. Changes b."""
    if b.safe_t > 0:
        b.safe_t -= DT
    if b.hurt_t > 0:
        b.hurt_t -= DT
    if b.shot_cd > 0:
        b.shot_cd -= 1
    mx, my = b.middle()
    b.in_liquid = g.front_at(mx, my) in ('=', '-')
    control = b.hurt_t <= 0
    left, right = inp.left, inp.right
    d = 0
    if control and left and not right:
        d = -1
    if control and right and not left:
        d = 1
    aiming = control and inp.aim and not b.ball
    sideways = 0
    if left and not right:
        sideways = -1
    if right and not left:
        sideways = 1
    if sideways != 0 and control:
        b.facing = sideways

    if control and not b.ball and not aiming:
        roll = inp.p_ball and b.grounded
        if b.crouch and inp.p_down:
            roll = True
        if roll and has('dough'):
            b.ball = True
            b.crouch = False
            b.spin = False
        elif not b.crouch and b.grounded and inp.p_down:
            b.crouch = True
        elif b.crouch and (sideways != 0 or inp.p_up or inp.p_jump):
            if not g.blocked(b.x, b.y, STAND):
                b.crouch = False
    elif control and b.ball and (inp.p_up or inp.p_jump or inp.p_ball):
        if not g.blocked(b.x, b.y, STAND):
            b.ball = False
        elif not g.blocked(b.x, b.y, CROUCH):
            b.ball = False
            b.crouch = True
    if b.crouch and not b.grounded and not g.blocked(b.x, b.y, STAND):
        b.crouch = False
    if aiming or b.crouch:
        d = 0

    speed = RUN * 0.6 if b.in_liquid else RUN
    if control:
        b.vx = d * speed
    elif b.grounded:
        b.vx = b.vx * 0.8
    if not move_x(b, b.vx, g):
        b.vx = 0

    was = b.grounded
    b.grounded = b.vy >= 0 and g.blocked(b.x, b.y + 1, b.pose())
    if b.grounded:
        if not was:
            b.spin = False
        b.vy = 0
        if control and not b.ball and not b.crouch and inp.p_jump:
            jump(b, d, has)
    else:
        gr, top = GRAVITY, MAX_FALL
        if b.in_liquid:
            gr, top = GRAVITY * 0.5, MAX_FALL * 0.5
        b.vy = min(b.vy + gr, top)
        if control and not b.ball and b.spin and has('whisk') and inp.p_jump and b.vy > -2:
            jump(b, d, has)
        if b.vy >= 0:
            b.lifted = False
        if b.vy < -CUT and not inp.jump and b.hurt_t <= 0 and not b.lifted:
            b.vy = -CUT
    if not move_y(b, b.vy, g):
        b.vy = 0.5 if b.vy < 0 else 0

    if control and (inp.p_fire or inp.p_rocket) and b.shot_cd <= 0:
        b.shot_cd = 9                                   # (near enough: a shot or a rocket)
    hazards(b, g)
    mx, my = b.middle()
    if mx < 0:
        b.left_room = 'W'
    elif mx > g.W:
        b.left_room = 'E'
    elif my < 0:
        b.left_room = 'N'
    elif my > g.H:
        b.left_room = 'S'
    return b


def jump(b, d, has):
    v = HIGH_JUMP if has('leaven') else JUMP
    if b.in_liquid:
        v = v * 0.75
    b.vy = -v
    b.grounded = False
    b.spin = d != 0 and has('whisk')


def move_x(b, dx, g):
    b.sx += dx
    steps = rnd(b.sx)
    b.sx -= steps
    s = sgn(steps)
    pose = b.pose()
    for _ in range(abs(steps)):
        if g.blocked(b.x + s, b.y, pose):
            b.sx = 0
            return False
        b.x += s
    return True


def move_y(b, dy, g):
    b.sy += dy
    steps = rnd(b.sy)
    b.sy -= steps
    s = sgn(steps)
    pose = b.pose()
    for _ in range(abs(steps)):
        if g.blocked(b.x, b.y + s, pose):
            b.sy = 0
            return False
        b.y += s
    return True


def hazards(b, g):
    mx, my = b.middle()
    w, h = SIZES[b.pose()]
    sx, sy = w / 2, h / 2
    lava = False
    for (px, py) in ((mx - sx + 2, my + sy - 2), (mx + sx - 2, my + sy - 2), (mx, my)):
        if g.tile_at(px, py) == '~':
            lava = True
    if lava:
        b.lava_t = (b.lava_t or 0) - DT
        if b.lava_t <= 0:
            b.lava_t = 0.4
            b.hurt_count += 1
        if b.vy > 1:
            b.vy = 1
    spikes = False
    for (px, py) in ((mx - sx + 2, my + sy - 2), (mx + sx - 2, my + sy - 2), (mx - sx + 2, my - sy + 1), (mx + sx - 2, my - sy + 1)):
        if g.tile_at(px, py) in ('^', 'v'):
            spikes = True
    if spikes:
        b.hurt_count += 1
        if b.safe_t <= 0:                                # hurt(): knocked back
            b.safe_t = 1.1
            b.hurt_t = 0.25
            away = sgn(b.x - (b.x - b.facing))
            if away == 0:
                away = -b.facing
            b.vx = away * 1.6
            b.vy = -2 if b.ball else -2.5
            b.spin = False
    return lava or spikes


def frame(b, held, pressed, steps, g, has):
    """A frame of the game: `steps` fixed steps (0, 1 or 2). Keys that went down are seen by
    the first step only; a frame with no steps leaves them for the next (the caller keeps
    them in `pressed` then)."""
    for i in range(steps):
        step(b, Inputs(held, pressed if i == 0 else ()), g, has)
    return b
