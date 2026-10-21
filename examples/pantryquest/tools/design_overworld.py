#!/usr/bin/env python3
"""Designs Pantry Quest's whole overworld (16 x 8 screens): writes
tools/world/overworld.txt, caves.txt and cellars_unbuilt.txt.

    python3 examples/pantryquest/tools/design_overworld.py --force [seed]

Run it to lay the map out again (after changing the regions, the entrances or
the caves below), then tools/make_rooms.py. It replaces those three files, so
hand edits made to them since are lost: that is why it wants --force. For
small changes, edit tools/world/overworld.txt instead.

The seven hand-made screens around the start (HAND) are kept as they are in
overworld.txt, opened where the map needs. Every other screen is laid out from
its region's style, then checked: edges meet their neighbours', and every
opening, entrance, pickup and enemy can be reached. Same seed, same world.
Only Python's standard library is used."""
import os
import random
import re
import sys

GAME = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
W_, H_ = 16, 11
SOLID = set('TrW~bIKXYPLGSg')
WATER = set('~g')

REGION = ["GGGMMMMMMMMMBBCC",
          "GGGMMMMMMMMMBBCC",
          "GGGLLLLMMMMMBBBC",
          "WWLLLLLIIIDDDDDC",
          "WWLLLLIIIIDDDDDC",
          "WWWLLPPIPPDDDDKC",
          "WWWWPPPPPPDDKKKC",
          "WWWWWPPPPPPDKKKC"]
REGION_NAME = {'G': 'Fortune Cookie Field', 'M': 'Chocolate Mountains', 'B': 'Pickle Marsh', 'C': 'Juice Coast',
               'L': 'Blueberry Lake', 'I': 'Frosty Fields', 'W': 'Lettuce Woods', 'D': 'Sugar Desert',
               'K': 'Candy Meadows', 'P': 'Pantry Plains'}
HAND = ['ow_7_7', 'ow_6_7', 'ow_8_7', 'ow_6_6', 'ow_7_6', 'ow_8_6', 'ow_7_5']

def reg(x, y): return REGION[y][x]

# ---------------------------------------------------------------- edges
def ekey(a, b): return tuple(sorted([a, b]))

GATEWAYS = [((5, 7), (4, 7)), ((4, 6), (3, 6)), ((5, 5), (4, 5)), ((4, 6), (4, 5)),
            ((6, 5), (6, 4)), ((8, 5), (8, 4)),
            ((10, 7), (11, 7)), ((9, 6), (10, 6)), ((9, 5), (10, 5)),
            ((11, 7), (12, 7)), ((11, 6), (12, 6)), ((13, 5), (13, 6)), ((14, 5), (14, 6)),
            ((6, 4), (5, 4)), ((7, 3), (6, 3)), ((7, 3), (7, 2)), ((8, 3), (8, 2)),
            ((9, 3), (10, 3)), ((9, 4), (10, 4)), ((1, 4), (2, 4)), ((1, 3), (2, 3)),
            ((2, 2), (3, 2)), ((2, 2), (2, 3)), ((0, 2), (0, 3)), ((1, 2), (1, 3)),
            ((2, 1), (3, 1)), ((2, 0), (3, 0)), ((5, 2), (5, 1)), ((6, 2), (7, 2)),
            ((11, 1), (12, 1)), ((11, 2), (12, 2)), ((12, 3), (12, 2)), ((13, 3), (13, 2)),
            ((13, 0), (14, 0)), ((14, 2), (15, 2)), ((13, 1), (14, 1)), ((14, 4), (15, 4)),
            ((14, 6), (15, 6)), ((14, 7), (15, 7))]
# the hand-made screens: new openings (side, span) and the edges kept shut
HAND_OPEN = {('ow_6_7', 'w'): (4, 6), ('ow_8_7', 'e'): (4, 6), ('ow_6_6', 'n'): (7, 8),
             ('ow_8_6', 'e'): (4, 6), ('ow_7_5', 'w'): (4, 6), ('ow_7_5', 'e'): (4, 6), ('ow_7_5', 'n'): (3, 4)}
CLOSED = [((5, 6), (6, 6)), ((8, 6), (8, 5))]
SAME_REGION_CHANCE = 0.72

DIRS = {'n': (0, -1), 's': (0, 1), 'w': (-1, 0), 'e': (1, 0)}
OPP = {'n': 's', 's': 'n', 'e': 'w', 'w': 'e'}

def side_between(a, b):
    for s, (dx, dy) in DIRS.items():
        if (a[0] + dx, a[1] + dy) == b: return s
    raise ValueError

# ---------------------------------------------------------------- the hand-made screens
def parse_world(path):
    rooms, cur, mode = {}, None, None
    order = []
    for line in open(path).read().split('\n'):
        s = line.strip()
        if mode in ('map', 'inside') and s and not s.startswith('#') and not re.match(r'(put|var|end)\b', s):
            cur[mode].append(s); continue
        if not s or s.startswith('#'): continue
        w, _, rest = s.partition(' ')
        if w == 'room': cur = dict(name=rest.strip(), map=[], vars=[], puts=[], music=None); mode = None
        elif w == 'music': cur['music'] = rest.strip()
        elif w == 'map': mode = 'map'
        elif w == 'var': cur['vars'].append(rest.strip()); mode = None
        elif w == 'put': cur['puts'].append(rest.strip()); mode = None
        elif w == 'end': rooms[cur['name']] = cur; order.append(cur['name']); cur = None; mode = None
    return rooms, order

def openings_of(m):
    """The open runs along each edge of a map: {side: [(a, b), ...]}."""
    out = {}
    def runs(cells):
        r, start = [], None
        for i, ch in enumerate(cells + ['#']):
            open_ = ch not in SOLID and ch != '#'
            if open_ and start is None: start = i
            if not open_ and start is not None: r.append((start, i - 1)); start = None
        return r
    out['n'] = runs(list(m[0])); out['s'] = runs(list(m[-1]))
    out['w'] = runs([r[0] for r in m]); out['e'] = runs([r[-1] for r in m])
    return out

# ---------------------------------------------------------------- what goes where
# cellars: (x, y) -> (number, name, hidden)
CELLARS = {(7, 5): (1, 'THE COLD CELLAR', False), (13, 1): (2, 'THE PICKLE PANTRY', False),
           (1, 6): (3, 'THE SPICE RACK', False), (4, 3): (4, 'THE JAM JAR', False),
           (13, 6): (5, 'THE CANDY WORKS', False), (1, 1): (6, 'THE FORTUNE HALL', False),
           (11, 4): (7, 'THE SUGAR MILL', True), (9, 1): (8, 'THE SOUP CAULDRON', True),
           (5, 0): (9, "BARON GORGE'S BANQUET HALL", False)}
BUILT = {1, 2, 3, 4, 5}       # cellars with their own world file (cellarN.txt): no stand-in entrance room
SHOP_STOCK = {
    'cave_shop2': [('bombs_item', 20), ('key', 60), ('heart', 10)],
    'cave_shop3': [('soup', 30), ('bombs_item', 25), ('key', 50)],
    'cave_shop4': [('bombs_item', 20), ('soup', 25), ('heart', 8)],
    'cave_shop5': [('key', 45), ('soup', 30), ('bombs_item', 20)],
}
SHOP_LINES = {'cave_shop2': ("THE WOODS' FINEST GOODS!", "BUY SOMETHING, WILL YOU?"),
              'cave_shop3': ("SWEET DEALS IN THE DESERT!", "BEANS ONLY, PLEASE."),
              'cave_shop4': ("A SHOP ON A MOUNTAIN? WHY NOT!", "TAKE YOUR PICK."),
              'cave_shop5': ("FRESH FROM THE MARSH!", "PICKLED PRICES, TOO.")}
HINTS = {  # cave -> lines
    'cave_hint1': ("THE COLD CELLAR LIES NORTH,", "PAST THE FROSTY FIELDS."),
    'cave_hint2': ("THE DUNES HIDE A SUGAR MILL.", "BOMB THE CRACKED CLIFF!"),
    'cave_hint3': ("THE SPICE RACK SITS DEEP", "IN THE LETTUCE WOODS."),
    'cave_hint4': ("A JAM JAR ON THE LAKE...", "CROSS THE LONG BRIDGE."),
    'cave_hint5': ("THE BARON FEASTS UP HIGH,", "ATOP CHOCOLATE MOUNTAIN."),
    'cave_hint6': ("SOME CLIFFS SOUND HOLLOW.", "A POPCORN BOMB WILL TELL."),
    'cave_hint7': ("SWEETS AND TREATS LIE EAST,", "IN THE CANDY WORKS."),
    'cave_hint8': ("THE FORTUNE HALL? IT'S IN", "THE COOKIE FIELD, NORTHWEST."),
    'cave_hint9': ("PICKLES! THE PANTRY IS", "OUT IN THE MARSH, NORTHEAST."),
}
# (x, y) -> (cave, hidden): a cave mouth on the screen ('X' cliff or 'Y' bush when hidden)
CAVES = {
    (2, 4): ('cave_shop2', False), (12, 5): ('cave_shop3', False), (10, 1): ('cave_shop4', False),
    (14, 2): ('cave_shop5', False),
    (5, 7): ('cave_hint1', False), (10, 6): ('cave_hint2', False), (0, 4): ('cave_hint3', False),
    (6, 3): ('cave_hint4', False), (3, 1): ('cave_hint5', False), (8, 2): ('cave_hint6', False),
    (12, 7): ('cave_hint7', False), (2, 2): ('cave_hint8', False), (12, 3): ('cave_hint9', False),
    (0, 7): ('cave_pie1', True), (15, 0): ('cave_pie2', False),
    (2, 0): ('cave_stash1', True), (6, 1): ('cave_stash2', True), (11, 2): ('cave_stash3', True),
    (14, 4): ('cave_stash4', True), (9, 6): ('cave_stash5', True), (1, 3): ('cave_stash6', True),
    (5, 5): ('cave_stash7', True), (10, 7): ('cave_stash8', True),
}
STASH_BEANS = {'cave_stash1': 3, 'cave_stash2': 2, 'cave_stash3': 3, 'cave_stash4': 2, 'cave_stash5': 1,
               'cave_stash6': 2, 'cave_stash7': 1, 'cave_stash8': 2}
PIE_LINES = {'cave_pie1': ("YOU FOUND MY HIDEAWAY!", "HAVE A SLICE OF HEART PIE."),
             'cave_pie2': ("A LONG WAY OUT HERE, EH?", "THIS PIE WILL KEEP YOU GOING.")}
SPRINGS = [(3, 4), (12, 0)]                 # hearts to pick up, every visit
OW_PIES = {(0, 0): 'ow:pie1', (6, 2): 'ow:pie2', (15, 7): 'ow:pie3'}
ENEMIES = {'P': ['tomato', 'plum'], 'W': ['tomato', 'burger', 'plum'], 'L': ['plum', 'tomato', 'burger'],
           'M': ['burger', 'breadstick', 'bat'], 'I': ['plum', 'jelly', 'tomato'], 'D': ['meatball', 'burger', 'breadstick'],
           'G': ['bat', 'breadstick', 'burger'], 'B': ['jelly', 'plum', 'burger'], 'K': ['plum', 'tomato', 'burger', 'jelly'],
           'C': ['tomato', 'plum']}

# ---------------------------------------------------------------- building a screen
STYLE = {  # floor, sprinkle, border tiles
    'P': ('.', ',f', 'T'), 'W': ('.', ',', 'Tb'), 'L': ('.', ',f', 'T'), 'M': (',', '.', 'W'),
    'I': ('.', ',', 'I'), 'D': ('s', '.', 'rP'), 'G': ('.', ',f', 'T'), 'B': ('.', ',', 'Tg'),
    'K': ('.', 'f', 'L'), 'C': ('s', '.', 'T'),
}

class Room:
    def __init__(self, x, y, rnd):
        self.x, self.y, self.r = x, y, reg(x, y)
        self.rnd = rnd
        floor, _, _ = STYLE[self.r]
        self.g = [[floor] * W_ for _ in range(H_)]
        self.keep = set()      # cells paths must not change (entrances)
        self.puts = []
        self.vars = []
        self.targets = []      # cells that must be reachable

    def set(self, c, r, t, keep=False):
        if 0 <= c < W_ and 0 <= r < H_:
            if (c, r) in self.keep and not keep: return
            self.g[r][c] = t
            if keep: self.keep.add((c, r))

    def get(self, c, r): return self.g[r][c]

def border(room, opens, world_edges):
    rnd, R = room.rnd, room.r
    _, _, btiles = STYLE[R]
    def pick(c, r):
        if R == 'C' and (c >= 12 or (room.y == 0 and r <= 2)) : return '~'
        return rnd.choice(btiles)
    thick = {}
    for side in 'nswe':
        n = W_ if side in 'ns' else H_
        base = 2 if side in world_edges else 1
        t, i = [], 0                              # runs of 3 to 6 cells, one or two deep
        while len(t) < n:
            t += [base + (1 if rnd.random() < 0.3 else 0)] * rnd.randint(3, 6)
        thick[side] = t[:n]
    for c in range(W_):
        for r in range(thick['n'][c]): room.set(c, r, pick(c, r))
        for r in range(thick['s'][c]): room.set(c, H_ - 1 - r, pick(c, H_ - 1 - r))
    for r in range(H_):
        for c in range(thick['w'][r]): room.set(c, r, pick(c, r))
        for c in range(thick['e'][r]): room.set(W_ - 1 - c, r, pick(W_ - 1 - c, r))
    if R == 'C':                                   # the sea along the east
        for r in range(H_):
            shore = 11 + (1 if rnd.random() < 0.5 else 0) + (1 if r % 4 == 0 else 0)
            for c in range(shore, W_): room.set(c, r, '~')

def free_rect(room, c0, r0, w, h, margin=1):
    for r in range(r0 - margin, r0 + h + margin):
        for c in range(c0 - margin, c0 + w + margin):
            if not (2 <= c < W_ - 2 and 2 <= r < H_ - 2): return False
            if (c, r) in room.keep: return False
    return True

def scatter(room, tile, count, w=1, h=1, region=(2, 2, 13, 8)):
    rnd = room.rnd
    for _ in range(count):
        for _try in range(20):
            c = rnd.randint(region[0], region[2] - w + 1)
            r = rnd.randint(region[1], region[3] - h + 1)
            if free_rect(room, c, r, w, h, 0):
                for rr in range(r, r + h):
                    for cc in range(c, c + w):
                        room.set(cc, rr, tile if not callable(tile) else tile())
                break

def features(room):
    rnd, R = room.rnd, room.r
    floor, sprinkle, _ = STYLE[R]
    for _ in range(rnd.randint(4, 9)):            # crumbs and flowers on the floor
        c, r = rnd.randint(2, 13), rnd.randint(2, 8)
        if room.get(c, r) == floor: room.set(c, r, rnd.choice(sprinkle))
    if R == 'P':
        scatter(room, 'r', rnd.randint(2, 4)); scatter(room, 'T', rnd.randint(0, 2), 2, 2)
        scatter(room, 'f', rnd.randint(1, 2), 3, 1)
    elif R == 'W':
        for _ in range(rnd.randint(4, 7)): scatter(room, 'b', 1, rnd.choice([1, 2, 3]), rnd.choice([1, 2]))
        scatter(room, 'T', rnd.randint(1, 3), 2, 2)
    elif R == 'L' and getattr(room, 'island', False):
        pass
    elif R == 'L':
        c0, r0 = rnd.randint(2, 4), rnd.randint(2, 4)
        c1, r1 = rnd.randint(10, 13), rnd.randint(6, 8)
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                corner = (r in (r0, r1)) and (c in (c0, c1))
                if not corner and (c, r) not in room.keep: room.set(c, r, '~')
        scatter(room, 'T', 1, 1, 1, (2, 2, 13, 8))
    elif R == 'M':
        for _ in range(rnd.randint(1, 3)): scatter(room, 'W', 1, rnd.randint(3, 5), 2)
        scatter(room, 'r', rnd.randint(1, 3))
    elif R == 'I':
        for _ in range(rnd.randint(2, 4)): scatter(room, 'I', 1, 2, rnd.choice([1, 2]))
        scatter(room, 'b', rnd.randint(0, 2))
    elif R == 'D':
        scatter(room, 'P', rnd.randint(4, 7)); scatter(room, 'r', rnd.randint(1, 3))
        scatter(room, '.', rnd.randint(1, 2), 3, 2)
    elif R == 'G':                                 # fortune cookies in rows, like old stones
        rows = rnd.choice([(3, 5, 7), (3, 6), (4, 7), (3, 5, 7)])
        for r in rows:
            for c in range(3, 13, 2):
                if rnd.random() < 0.75 and (c, r) not in room.keep: room.set(c, r, 'G')
        scatter(room, 'T', rnd.randint(0, 1), 2, 2)
    elif R == 'B':
        for _ in range(rnd.randint(2, 4)): scatter(room, 'g', 1, rnd.randint(2, 4), rnd.randint(1, 2))
        scatter(room, 'P', rnd.randint(1, 3)); scatter(room, 'T', rnd.randint(0, 2))
    elif R == 'K':
        for _ in range(rnd.randint(2, 3)):
            r = rnd.randint(2, 8); c = rnd.randint(2, 6)
            for k in range(rnd.randint(2, 4)):
                if c + 2 * k < 14 and (c + 2 * k, r) not in room.keep: room.set(c + 2 * k, r, 'L')
        scatter(room, 'f', rnd.randint(2, 3), 2, 1)
    elif R == 'C':
        scatter(room, 'T', rnd.randint(1, 2)); scatter(room, 'r', rnd.randint(0, 2), 1, 1, (2, 2, 9, 8))

def entrance(room, kind, hidden, wall='W', opens=None):
    """A cave mouth or cellar door in an outcrop near the top. Returns the door cell."""
    rnd = room.rnd
    width = 5 if kind == 'cellar' else 3
    if kind == 'cellar' and room.r == 'L':          # the Jam Jar: on an island
        cx, r0 = 7, 3
    else:
        n = (opens or {}).get('n')
        spots = [c for c in range(3 + width // 2, 13 - width // 2)
                 if not n or c + width // 2 + 1 < n[0] or c - width // 2 - 1 > n[1]]   # clear of the way in from the north
        cx = rnd.choice(spots) if spots else rnd.randint(3 + width // 2, 12 - width // 2)
        r0 = rnd.randint(1, 2) if spots else 2
    if hidden and room.r == 'W' and kind == 'cave':   # a secret behind a bush
        for c in range(cx - 1, cx + 2): room.set(c, r0 + 1, 'b', keep=True)
        for c in range(cx - 1, cx + 2): room.set(c, r0, rnd.choice('bT'), keep=True)
        door = 'Y'
    else:
        for r in (r0, r0 + 1):
            for c in range(cx - width // 2, cx + width // 2 + 1): room.set(c, r, wall, keep=True)
        door = ('X' if hidden else ('F' if kind == 'cellar' else 'C'))
    room.set(cx, r0 + 1, door, keep=True)
    floor = STYLE[room.r][0]
    for c in range(cx - 1, cx + 2): room.set(c, r0 + 2, floor, keep=True)   # the step in front
    if kind == 'cellar':
        room.set(cx - 2, r0 + 2, 'S', keep=True); room.set(cx + 2, r0 + 2, 'S', keep=True)
        room.set(cx, r0 + 3, floor, keep=True)
    if room.r == 'L' and kind == 'cellar':        # the island around it
        room.island = True
        for r in range(r0 - 1, r0 + 5):
            for c in range(cx - 4, cx + 5):
                if (c, r) not in room.keep and room.g[r][c] not in 'W': room.set(c, r, '.')
        for r in range(r0 - 2, r0 + 6):
            for c in range(cx - 6, cx + 7):
                ring = r in (r0 - 2, r0 + 5) or c in (cx - 6, cx - 5, cx + 5, cx + 6)
                if ring and 1 <= r < H_ - 1 and 1 <= c < W_ - 1 and (c, r) not in room.keep: room.set(c, r, '~')
    room.targets.append((cx, r0 + 2))
    return (cx, r0 + 1)

def carve(room, a, b, floor):
    """A path two wide from cell a to cell b: the cheapest way round, through
    floor where it can, never through an entrance."""
    import heapq
    rnd = room.rnd
    noise = {(c, r): rnd.random() * 0.8 for c in range(W_) for r in range(H_)}
    def cost(c, r):
        t = room.g[r][c]
        return (1 if t not in SOLID else 7 if t not in WATER else 16) + noise[(c, r)]
    dist, prev = {a: 0}, {}
    todo = [(0, a)]
    while todo:
        d, p = heapq.heappop(todo)
        if p == b: break
        if d > dist.get(p, 1e9): continue
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (p[0] + dc, p[1] + dr)
            if not (0 <= q[0] < W_ and 0 <= q[1] < H_) or (q in room.keep and q != b): continue
            if q[0] in (0, W_ - 1) or q[1] in (0, H_ - 1): continue      # never along the screen's edge
            # the 2 x 2 brush must fit too (except at the edges, where the opening is)
            nd = d + cost(*q)
            if nd < dist.get(q, 1e9):
                dist[q] = nd; prev[q] = p; heapq.heappush(todo, (nd, q))
    if b not in prev and a != b: return False
    path, p = [b], b
    while p != a: p = prev[p]; path.append(p)
    for (c, r) in path:
        for cc, rr in ((c, r), (c + 1, r), (c, r + 1), (c + 1, r + 1)):
            if not (0 <= cc < W_ and 0 <= rr < H_) or (cc, rr) in room.keep: continue
            if (cc in (0, W_ - 1) or rr in (0, H_ - 1)) and (cc, rr) != (c, r): continue   # don't widen the edges
            t = room.g[rr][cc]
            if t in WATER: room.set(cc, rr, '=')
            elif t in SOLID: room.set(cc, rr, floor)
    return True

def opening_cells(side, span):
    a, b = span
    if side == 'n': return [(c, 0) for c in range(a, b + 1)]
    if side == 's': return [(c, H_ - 1) for c in range(a, b + 1)]
    if side == 'w': return [(0, r) for r in range(a, b + 1)]
    return [(W_ - 1, r) for r in range(a, b + 1)]

def inner_cell(side, span):
    a, b = span
    m = a
    if side == 'n': return (m, 1)
    if side == 's': return (m, H_ - 3)
    if side == 'w': return (1, m)
    return (W_ - 3, m)

def reachable(g, start):
    seen, todo = {start}, [start]
    while todo:
        c, r = todo.pop()
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            cc, rr = c + dc, r + dr
            if 0 <= cc < W_ and 0 <= rr < H_ and (cc, rr) not in seen and g[rr][cc] not in SOLID:
                seen.add((cc, rr)); todo.append((cc, rr))
    return seen

# ---------------------------------------------------------------- the whole map
def design(seed):
    rnd = random.Random(seed)
    hand, _ = parse_world(GAME + '/tools/world/overworld.txt')
    hand = {k: v for k, v in hand.items() if k in HAND}
    caves, _ = parse_world(GAME + '/tools/world/caves.txt')
    caves_old = {k: caves[k] for k in ('cave_chef', 'cave_shop', 'cave_secret')}   # the first three, made by hand
    # which edges are open, and where
    spans = {}                                   # (a, b) sorted -> span
    def name(p): return 'ow_%d_%d' % p
    hand_cells = {tuple(int(v) for v in n.split('_')[1:]): n for n in HAND}
    for p, n in hand_cells.items():
        op = openings_of(hand[n]['map'])
        for side, runs in op.items():
            dx, dy = DIRS[side]
            q = (p[0] + dx, p[1] + dy)
            if not (0 <= q[0] < 16 and 0 <= q[1] < 8): continue
            if runs: spans[ekey(p, q)] = runs[0]
        for (hn, side), sp in HAND_OPEN.items():
            if hn == n:
                dx, dy = DIRS[side]
                spans[ekey(p, (p[0] + dx, p[1] + dy))] = sp
    gate = {ekey(a, b) for a, b in GATEWAYS}
    closed = {ekey(a, b) for a, b in CLOSED}
    NS = [(7, 8)] * 3 + [(3, 4), (11, 12), (5, 6), (9, 10)]
    EW = [(4, 6)] * 3 + [(2, 3), (7, 8), (3, 5), (5, 7)]
    def choose_span(a, b):
        return rnd.choice(NS if a[0] == b[0] else EW)
    for y in range(8):
        for x in range(16):
            for q in ((x + 1, y), (x, y + 1)):
                if q[0] >= 16 or q[1] >= 8: continue
                k = ekey((x, y), q)
                if k in spans or k in closed: continue
                if (x, y) in hand_cells or q in hand_cells: continue      # hand screens: only what's listed
                same = reg(x, y) == reg(*q)
                if (same and rnd.random() < SAME_REGION_CHANCE) or k in gate:
                    spans[k] = choose_span((x, y), q)
    # connected? open more edges until every screen can be reached from the start
    def comp():
        seen, todo = {(7, 7)}, [(7, 7)]
        while todo:
            p = todo.pop()
            for s, (dx, dy) in DIRS.items():
                q = (p[0] + dx, p[1] + dy)
                if ekey(p, q) in spans and q not in seen: seen.add(q); todo.append(q)
        return seen
    while True:
        seen = comp()
        if len(seen) == 128: break
        cands = []
        for p in seen:
            for s, (dx, dy) in DIRS.items():
                q = (p[0] + dx, p[1] + dy)
                if 0 <= q[0] < 16 and 0 <= q[1] < 8 and q not in seen and q not in hand_cells and p not in hand_cells \
                        and ekey(p, q) not in closed:
                    cands.append((0 if reg(*p) == reg(*q) else 1, rnd.random(), p, q))
        cands.sort()
        _, _, p, q = cands[0]
        spans[ekey(p, q)] = choose_span(p, q)

    rooms = {}
    for y in range(8):
        for x in range(16):
            n = name((x, y))
            opens = {}
            for s, (dx, dy) in DIRS.items():
                k = ekey((x, y), (x + dx, y + dy))
                if k in spans: opens[s] = spans[k]
            world_edges = {s for s, (dx, dy) in DIRS.items() if not (0 <= x + dx < 16 and 0 <= y + dy < 8)}
            if n in hand:
                rooms[n] = build_hand(hand[n], opens)
            else:
                rooms[n] = build_room(x, y, opens, world_edges, seed)
    return rooms, caves_old, spans

def build_hand(h, opens):
    g = [list(r) for r in h['map']]
    for side, sp in opens.items():
        for (c, r) in opening_cells(side, sp):
            if g[r][c] in SOLID: g[r][c] = '.'
            ic = (c + (1 if side == 'w' else -1 if side == 'e' else 0), r + (1 if side == 'n' else -1 if side == 's' else 0))
            if g[ic[1]][ic[0]] in SOLID: g[ic[1]][ic[0]] = '.'
    return dict(map=[''.join(r) for r in g], puts=h['puts'], vars=h['vars'], music=h['music'] or 'overworld', opens=opens)

def build_room(x, y, opens, world_edges, seed):
    rnd = random.Random(seed * 1000 + x * 37 + y * 101)
    room = Room(x, y, rnd)
    floor = STYLE[room.r][0]
    # entrances first, so nothing covers them
    hub = None
    if (x, y) in CELLARS:
        num, title, hidden = CELLARS[(x, y)]
        door = entrance(room, 'cellar', hidden, opens=opens)
        to = 'd%d_2_4' % num
        room.puts.append(('warp', door[0], door[1], 'to = "%s", pos = vec2(128, 200)%s' % (to, ', hidden = true' if hidden else '')))
        hub = (door[0] - 1, door[1] + 2)
    if (x, y) in CAVES:
        cave, hidden = CAVES[(x, y)]
        door = entrance(room, 'cave', hidden, opens=opens)
        room.puts.append(('warp', door[0], door[1], 'to = "%s", pos = vec2(128, 216)%s' % (cave, ', hidden = true' if hidden else '')))
        hub = (door[0] - 1, door[1] + 1) if hub is None else hub
    border(room, opens, world_edges)
    features(room)
    for side, sp in opens.items():                   # the openings, cut through the border, two tiles in
        dc, dr = {'n': (0, 1), 's': (0, -1), 'w': (1, 0), 'e': (-1, 0)}[side]
        for (c, r) in opening_cells(side, sp):
            for k in range(2):
                cc, rr = c + dc * k, r + dr * k
                room.set(cc, rr, floor if room.get(cc, rr) not in WATER else '=')
    if hub is None:                                  # where the paths meet: dry land near the middle
        want = (rnd.randint(6, 8), rnd.randint(4, 5))
        land = [(c, r) for c in range(2, W_ - 3) for r in range(2, H_ - 3)
                if all(room.g[rr][cc] not in WATER for cc in (c, c + 1) for rr in (r, r + 1))]
        hub = min(land, key=lambda p: abs(p[0] - want[0]) + abs(p[1] - want[1])) if land else want
    for side, sp in opens.items():
        a = opening_cells(side, sp)[0]
        # two wide: start at the opening's first cell, go in
        carve(room, a, hub, floor)
    for t in room.targets:
        carve(room, (t[0] - 1, t[1]), hub, floor)
    # pickups
    if (x, y) in OW_PIES:
        room.puts.append(('pickup', None, None, 'kind = "heart_pie", flag = "%s"' % OW_PIES[(x, y)]))
    if (x, y) in SPRINGS:
        for k in range(3):
            room.puts.append(('pickup', None, None, 'kind = "heart"'))
    # enemies
    if (x, y) not in SPRINGS:
        dist = abs(x - 7) + abs(y - 7)
        count = 1 + min(3, dist // 3) + (1 if rnd.random() < 0.4 else 0)
        for _ in range(min(count, 4)):
            room.puts.append(('enemy', None, None, rnd.choice(ENEMIES[room.r])))
    room.opens = opens
    return room

def place_free(room, reach, avoid, far_from_edges=3):
    cands = [(c, r) for (c, r) in reach if far_from_edges <= c <= W_ - 1 - far_from_edges and 2 <= r <= H_ - 3
             and room.g[r][c] not in '=' and all(abs(c - a) + abs(r - b) >= 3 for (a, b) in avoid)]
    if not cands: return None
    p = room.rnd.choice(sorted(cands))
    avoid.append(p)
    return p

def finish(room):
    """Checks a generated screen and places its pickups and enemies on cells Pip can reach."""
    g = room.g
    starts = []
    for side, sp in room.opens.items():
        starts += opening_cells(side, sp)
    reach = reachable(g, starts[0]) if starts else set()
    problems = []
    for s in starts:
        if s not in reach: problems.append('opening %s unreachable' % (s,))
    for t in room.targets:
        if t not in reach: problems.append('entrance front %s unreachable' % (t,))
    avoid = list(starts) + list(room.targets)
    puts = []
    for kind, c, r, extra in room.puts:
        if kind == 'warp':
            puts.append('put warp %d %d %s' % (c, r, extra))
        elif kind == 'pickup':
            p = place_free(room, reach, avoid)
            if p is None: problems.append('no room for a pickup'); continue
            puts.append('put pickup %d %d %s' % (p[0], p[1], extra))
        else:
            p = place_free(room, reach, avoid)
            if p is None: continue
            puts.append('put %s %d %d' % (extra, p[0], p[1]))
    return [''.join(r) for r in g], puts, problems

def cave_room(name):
    m = ["KKKKKKKKKKKKKKKK", "KKKKKKKKKKKKKKKK"] + ["KK::::::::::::KK"] * 8 + ["KKKKKKK::KKKKKKK"]
    puts = ['put flame 4.5 4', 'put flame 10.5 4']
    if name in SHOP_STOCK:
        l1, l2 = SHOP_LINES[name]
        puts.append('put baker 7.5 4 line1 = "%s", line2 = "%s"' % (l1, l2))
        for (kind, price), c in zip(SHOP_STOCK[name], (5, 7.5, 10)):
            puts.append('put pickup %g 6.5 kind = "%s", price = %d' % (c, kind, price))
    elif name in HINTS:
        l1, l2 = HINTS[name]
        puts.append('put old_chef 7.5 4 line1 = "%s", line2 = "%s"' % (l1, l2))
    elif name in PIE_LINES:
        l1, l2 = PIE_LINES[name]
        puts.append('put old_chef 7.5 4 line1 = "%s", line2 = "%s"' % (l1, l2))
        puts.append('put pickup 7.5 6 kind = "heart_pie", flag = "%s"' % name.replace('cave_', ''))
    elif name in STASH_BEANS:
        n = STASH_BEANS[name]
        puts.append('put old_chef 7.5 4 line1 = "%s", line2 = "%s"' % (
            "SHH! YOU FOUND MY STASH.", "TAKE %s BEANS." % {1: 'FIVE', 2: 'TEN', 3: 'FIFTEEN'}[n]))
        for i, c in enumerate([(7.5,), (6.5, 8.5), (6, 7.5, 9)][n - 1]):
            puts.append('put pickup %g 6.5 kind = "bean5", flag = "%s%d"' % (c, name.replace('cave_', ''), i + 1))
    return m, puts

def write(rooms, caves_old, seed):
    out = ['# The Pantry Plains (the overworld): 16 x 8 screens, ow_<column>_<row>, 16 x 11 tiles each.',
           '# tools/make_rooms.py turns these into rooms/<name>.scene.',
           '#',
           '#   .  biscuit ground     ,  crumbs          f  flowers        s  sugar sand',
           '#   T  broccoli tree      r  cookie boulder  b  lettuce bush   W  chocolate cliff',
           '#   ~  blueberry juice    =  bridge          I  ice            C  cave mouth',
           '#   F  a cellar door      X  a cracked cliff (a bomb opens it)  Y  a bush hiding a cave (a bomb)',
           '#   P  pickle cactus      L  lollipop tree   G  fortune cookie S  pepper-mill statue',
           '#   g  pickle brine       p  path',
           '#',
           '# Edges: a screen\'s open edges meet its neighbours\' exactly (same columns or rows).',
           '#',
           '#   put <prefab> <column> <row> [script vars]   - a thing from prefabs/, at a tile (halves allowed)',
           '#   var <name> = <value>                         - a variable of the room\'s script (scripts/room.bs);',
           '#                                                  region is the name shown when Pip comes into it',
           '#',
           '# Regions:']
    for y in range(8):
        out.append('#   ' + ' '.join(REGION[y]))
    out.append('#   ' + ', '.join('%s %s' % (k, v) for k, v in REGION_NAME.items()))
    out.append('#')
    out.append('# Cellars: ' + ', '.join('%d %s (%d,%d)%s' % (v[0], v[1].title(), k[0], k[1], ' hidden' if v[2] else '')
                                        for k, v in sorted(CELLARS.items(), key=lambda kv: kv[1][0])))
    out.append('')
    problems = []
    for y in range(8):
        for x in range(16):
            n = 'ow_%d_%d' % (x, y)
            r = rooms[n]
            if isinstance(r, dict):
                m, puts = r['map'], ['put ' + p for p in r['puts']]
                reach = reachable([list(s) for s in m], opening_cells(*next(iter(r['opens'].items())))[0])
                for side, sp in r['opens'].items():
                    for cell in opening_cells(side, sp):
                        if cell not in reach: problems.append('%s: hand opening %s %s unreachable' % (n, side, cell))
            else:
                m, puts, pr = finish(r)
                problems += ['%s: %s' % (n, p) for p in pr]
            out.append('room %s' % n)
            out.append('music overworld')
            out.append('var region = "%s"' % REGION_NAME[reg(x, y)].upper())   # shown as Pip comes in
            out.append('map')
            out += m
            out += puts
            out.append('end')
            out.append('')
    open(GAME + '/tools/world/overworld.txt', 'w').write('\n'.join(out))
    # caves
    c = ['# Caves: one room each, 16 x 11 tiles of rock (K) and floor (:). Leaving by the bottom',
         '# goes back to where Pip came in (exit_s = "back").', '']
    names = sorted(set([v[0] for v in CAVES.values()]))
    for k, v in caves_old.items():
        c.append('room %s' % k)
        for var in v['vars']: c.append('var ' + var)
        c.append('map'); c += v['map']; c += ['put ' + p for p in v['puts']]; c += ['end', '']
    for nme in names:
        m, puts = cave_room(nme)
        c += ['room %s' % nme, 'var exit_s = "back"', 'map'] + m + puts + ['end', '']
    open(GAME + '/tools/world/caves.txt', 'w').write('\n'.join(c))
    # the cellars still to be dug: an entrance room each
    s = ['# The cellars still to come: for now, just an entrance room each, where a cook',
         '# says it is still being dug. Each will get its own file, like cellar1.txt.', '']
    for (x, y), (num, title, hidden) in sorted(CELLARS.items(), key=lambda kv: kv[1][0]):
        if num in BUILT: continue
        # where the door is on the overworld screen
        door = None
        for p in rooms['ow_%d_%d' % (x, y)].puts:
            if p[0] == 'warp': door = (p[1], p[2])
        ex = (door[0] * 16 + 8, 64 + (door[1] + 1) * 16 + 8)
        s += ['room d%d_2_4' % num, 'music cellar', 'doors s=open', 'var title = "%s"' % title,
              'var exit_s = "ow_%d_%d"' % (x, y), 'var exit_pos = vec2(%d, %d)' % ex, 'var exit_face = "down"',
              'inside', '............', '..P......P..', '............', '............', '............',
              '..P......P..', '............',
              'put flame 4.5 3', 'put flame 10.5 3',
              'put old_chef 7.5 3 line1 = "%s", line2 = "IS STILL BEING DUG. LATER!"' % title[:30],
              'end', '']
    open(GAME + '/tools/world/cellars_unbuilt.txt', 'w').write('\n'.join(s))
    return problems

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--force']
    if '--force' not in sys.argv:
        raise SystemExit('design_overworld: this replaces tools/world/overworld.txt, caves.txt and '
                         'cellars_unbuilt.txt (hand edits there are lost). Run it with --force.')
    seed = int(args[0]) if args else 7
    rooms, caves_old, spans = design(seed)
    probs = write(rooms, caves_old, seed)
    print('%d problems' % len(probs))
    for p in probs: print('  ', p)
