"""What can be placed in a room, as scene entities: doors, items, hearths, enemies...

Each kind is a function that adds an entity (a child of the room) to the scene.
"""
from roomkit import SW, SH, TILE, DOOR_ROWS, SHAFT_COLS


def q(s):
    return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"') + '"'


def vtext(d):
    out = []
    for k, v in d.items():
        if isinstance(v, bool):
            out.append('%s = %s' % (k, 'true' if v else 'false'))
        elif isinstance(v, (int, float)):
            out.append('%s = %s' % (k, v))
        elif isinstance(v, tuple):
            out.append('%s = vec2(%s, %s)' % (k, v[0], v[1]))
        elif isinstance(v, list):
            out.append('%s = [%s]' % (k, ', '.join(q(i) if isinstance(i, str) else str(i) for i in v)))
        else:
            out.append('%s = %s' % (k, q(v)))
    return ', '.join(out)


def sprite(image, layer, frame_size=None, frame=0, flip=False):
    f = [('image', q(image)), ('layer', layer)]
    if frame_size:
        f.append(('frame_size', '%d %d' % frame_size))
        if frame:
            f.append(('frame', frame))
    if flip:
        f.append(('flip_x', 'true'))
    return ('Sprite', f)


def script(file, vars=None):
    f = [('file', q(file))]
    if vars:
        f.append(('vars', q(vtext(vars))))
    return ('Script', f)


def tags(t):
    return ('Tags', [('list', q(t))])


def anim(frames, fps, loop=True):
    return ('Animation', [('frames', q(frames)), ('fps', fps), ('loop', 'true' if loop else 'false'), ('playing', 'true')])


def name(n):
    return ('Name', [('text', q(n))])


def pos(x, y):
    return ('Transform', [('position', '%g %g' % (x, y))])


# ---- doors -----------------------------------------------------------------------------------
DOOR_FRAME = {'blue': 0, 'wax': 1, 'boss': 2, 'gate': 3}


def door(sc, room, side, index, kind, to, arrive, style):
    if side in 'WE':
        x = 0 if side == 'W' else room.W - 1
        cx, cy = x * TILE + TILE // 2, (index * SH + DOOR_ROWS[1]) * TILE + TILE // 2
    else:
        x = index * SW + SHAFT_COLS[0]
        cx, cy = (x + 1) * TILE, (0 if side == 'N' else room.H - 1) * TILE + TILE // 2
    v = dict(side=side, index=index, kind=kind, to=to, arrive=arrive)
    comps = [name('Door'), pos(cx, cy)]
    if side in 'WE' and kind != 'open':
        comps.append(sprite('art/door.bmp', 4, (16, 48), DOOR_FRAME.get(kind, 0), side == 'E'))
    comps += [script('scripts/door.bs', v), tags('door')]
    sc.entity(comps)


# ---- things --------------------------------------------------------------------------------------
def centre(th):
    return th.x * TILE + TILE // 2, th.y * TILE + TILE // 2


def feet(th, height):
    """A thing standing on the floor under tile (x, y): its middle, for its height."""
    return th.x * TILE + TILE // 2, (th.y + 1) * TILE - height // 2


ITEM_FRAMES = {   # art/items.bmp, 16 x 16 frames
    'dough': 0, 'popcorn': 1, 'sauce': 2, 'frost': 3, 'leaven': 4, 'mitts': 5, 'garlic': 6, 'whisk': 7,
    'cake': 8, 'packet': 9, 'page': 10, 'seal_salt': 11, 'seal_sugar': 12, 'seal_vinegar': 13, 'seal_pepper': 14,
}


def item(sc, room, th):
    x, y = centre(th)
    kind = th.vars['kind']
    sc.entity([name('Item'), pos(x, y), sprite('art/items.bmp', 3, (16, 16), ITEM_FRAMES[kind]),
               script('scripts/item.bs', dict(kind=kind, iid=th.vars['iid'])), tags('item')])


def hearth(sc, room, th):
    x, y = feet(th, 32)
    sc.entity([name('Hearth'), pos(x, y), sprite('art/hearth.bmp', -5, (32, 32)), anim('0-3', 8),
               script('scripts/hearth.bs'), tags('hearth')])


def maptable(sc, room, th):
    x, y = feet(th, 32)
    sc.entity([name('MapTable'), pos(x, y), sprite('art/maptable.bmp', -5, (32, 32)), anim('0-1', 3),
               script('scripts/maptable.bs', dict(area=room.area)), tags('maptable')])


def npc(sc, room, th):
    x, y = feet(th, 32)
    v = dict(th.vars)
    who = v.pop('who', 'mozz')
    sc.entity([name('NPC'), pos(x, y), sprite('art/%s.bmp' % who, 3, (24, 32)), anim('0-1', 2),
               script('scripts/npc.bs', dict(v, who=who)), tags('npc')])


def sign(sc, room, th):
    x, y = feet(th, 16)
    sc.entity([name('Sign'), pos(x, y), sprite('art/sign.bmp', -4), script('scripts/sign.bs', dict(th.vars)), tags('sign')])


def switch(sc, room, th):
    x, y = centre(th)
    sc.entity([name('Switch'), pos(x, y), sprite('art/switch.bmp', 2, (16, 16)),
               script('scripts/switch.bs', dict(gate=th.vars['gate'], sid=room.id)), tags('enemy, switch')])


BOSSES = {   # name: (sheet, frame size, script)
    'stale': ('art/boss_stale.bmp', (40, 48)),
    'sundae': ('art/boss_sundae.bmp', (56, 56)),
    'brine': ('art/boss_frank.bmp', (40, 56)),
    'wyrm': ('art/boss_wyrm.bmp', (32, 32)),
    'count': ('art/boss_count.bmp', (32, 48)),
}


def boss(sc, room, th):
    n = th.vars['name']
    sheet, fs = BOSSES[n]
    x, y = feet(th, fs[1])
    v = dict(home=(x, y))
    if getattr(room, 'boss_gates', None):
        g = sorted(room.boss_gates)
        v['gate'] = [g[0][0], g[0][1], g[-1][0] - g[0][0] + 1, g[-1][1] - g[0][1] + 1]
    sc.entity([name('Boss'), pos(x, y), sprite(sheet, 3, fs), script('scripts/bosses/%s.bs' % n, v), tags('enemy, boss')])


# Enemies: kind -> (sheet, frame size, animation, brain, hp, damage, extra vars, tags)
ENEMIES = {
    'crawler': ('art/crawler.bmp', (16, 16), '0-1', 'crawler', 2, 8, {}, 'enemy, freezable'),
    'gnat': ('art/gnat.bmp', (16, 16), '0-1', 'flyer', 1, 6, {}, 'enemy, freezable'),
    'batter': ('art/batter.bmp', (16, 16), '0', 'swooper', 2, 10, {}, 'enemy, freezable'),
    'hopper': ('art/hopper.bmp', (16, 16), '0', 'hopper', 3, 10, {}, 'enemy, freezable'),
    'walker': ('art/walker.bmp', (16, 24), '0-3', 'walker', 3, 10, {}, 'enemy, freezable'),
    'slider': ('art/slider.bmp', (16, 16), '0-1', 'slider', 4, 12, {}, 'enemy, freezable'),
    'leaper': ('art/leaper.bmp', (16, 16), '0-1', 'leaper', 3, 14, {}, 'enemy, freezable'),
    'ghost': ('art/ghost.bmp', (16, 16), '0-1', 'ghost', 4, 12, {}, 'enemy'),
    'turret': ('art/turret.bmp', (16, 16), '0-1', 'turret', 4, 10, {}, 'enemy'),
    'knight': ('art/knight.bmp', (16, 24), '0-3', 'knight', 8, 16, {}, 'enemy, freezable'),
}


def enemy(sc, room, th, kind):
    sheet, fs, frames, brain, hp, dmg, extra, tg = ENEMIES[kind]
    x, y = centre(th)
    if brain in ('walker', 'knight', 'hopper'):
        x, y = feet(th, fs[1])                           # standing on the floor under its tile
    v = dict(brain=brain, hp=hp, damage=dmg)
    v.update(extra)
    v.update(th.vars)
    sc.entity([name(kind.capitalize()), pos(x, y), sprite(sheet, 2, fs), anim(frames, 6),
               script('scripts/enemy.bs', v), tags(tg)])


PLACE = {'item': item, 'hearth': hearth, 'maptable': maptable, 'npc': npc, 'sign': sign, 'switch': switch, 'boss': boss}


def place(sc, room, th, style):
    if th.kind.startswith('enemy:'):
        return enemy(sc, room, th, th.kind[6:])
    if th.kind not in PLACE:
        raise SystemExit('room %s: there is no such thing as a %s' % (room.id, th.kind))
    PLACE[th.kind](sc, room, th)
