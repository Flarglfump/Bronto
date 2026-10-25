#!/usr/bin/env python3
"""Writes the prefabs (prefabs/*.scene): shots, effects, pickups - small scenes the scripts
spawn. They are ordinary scenes: open them in the editor."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'prefabs')


def q(s):
    return '"' + s.replace('"', '\\"') + '"'


def prefab(name, title, comps):
    """Every prefab is tagged 'transient': it belongs to the room it was spawned in, and the
    Game clears them all away when Basil changes rooms (or starts the room again)."""
    tagged = [c for c in comps if c[0] == 'Tags']
    if tagged:
        k, v = tagged[0][1][0]
        comps = [c for c in comps if c[0] != 'Tags'] + [('Tags', [(k, v[:-1] + ', transient"')])]
    else:
        comps = comps + [('Tags', [('list', '"transient"')])]
    lines = ['# Pantry Quest II - ' + title + ' (made by tools/make_prefabs.py)', 'bronto_scene 1', '', 'entity']
    for cname, fields in comps:
        lines.append('  ' + cname)
        for k, v in fields:
            lines.append('    %s = %s' % (k, v))
    with open(os.path.join(OUT, name + '.scene'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def sprite(image, layer, fs=None, frame=0, blend=None):
    f = [('image', q(image)), ('layer', layer)]
    if fs:
        f.append(('frame_size', '%d %d' % fs))
    if frame:
        f.append(('frame', frame))
    if blend:
        f.append(('blend', blend))
    return ('Sprite', f)


def anim(frames, fps, loop):
    return ('Animation', [('frames', q(frames)), ('fps', fps), ('loop', 'true' if loop else 'false'), ('playing', 'true')])


def base(n):
    return [('Name', [('text', q(n))]), ('Transform', [('position', '0 0')])]


def script(f, vars=None):
    fl = [('file', q(f))]
    if vars:
        fl.append(('vars', q(vars)))
    return ('Script', fl)


def tags(t):
    return ('Tags', [('list', q(t))])


def main():
    os.makedirs(OUT, exist_ok=True)
    prefab('shot', "a peppercorn from Basil's mill", base('Shot') + [sprite('art/shots.bmp', 6, (8, 8)), script('scripts/shot.bs'), tags('pepper')])
    prefab('rocket', 'a hot sauce rocket', base('Rocket') + [sprite('art/rocket.bmp', 6), script('scripts/rocket.bs'), tags('rocket')])
    prefab('popcorn', 'a popcorn kernel, about to pop', base('Popcorn') + [sprite('art/popcorn.bmp', 4, (8, 8)), script('scripts/popcorn.bs'), tags('popcorn')])
    prefab('spit', "something an enemy throws", base('Spit') + [sprite('art/spit.bmp', 6, (8, 8)), anim('0-1', 8, True), script('scripts/spit.bs'), tags('spit')])
    for k, n, fps, blend in (('puff', 4, 16, None), ('burst', 5, 14, 'add'), ('crumbs', 4, 10, None), ('blast', 5, 16, 'add'), ('pop', 4, 14, None)):
        prefab(k, 'an effect: ' + k, base(k.capitalize()) + [sprite('art/fx_%s.bmp' % k, 7, (16, 16), 0, blend), anim('0-%d' % (n - 1), fps, False), script('scripts/fx.bs')])
    prefab('drop', 'a little energy or hot sauce left by an enemy', base('Drop') + [sprite('art/drops.bmp', 3, (8, 8)), anim('0-1', 6, True), script('scripts/drop.bs'), tags('drop')])
    prefab('item', 'an item a guardian leaves (its kind and id are set when it is spawned)',
           base('Item') + [sprite('art/items.bmp', 3, (16, 16)), script('scripts/item.bs'), tags('item')])
    prefab('doctor', 'Doctor Brine in his jar (bosses/brine.bs spawns him)',
           base('Doctor') + [sprite('art/boss_doctor.bmp', 4, (24, 32)), script('scripts/bosses/doctor.bs'), tags('enemy, boss')])
    prefab('wyrm_seg', "a piece of the Flambe Wyrm's body",
           base('WyrmBody') + [sprite('art/boss_wyrm.bmp', 2, (32, 32), 2), script('scripts/bosses/segment.bs'), tags('enemy, boss')])
    prefab('spore', "a mould spore the Big Cheese breathes out",
           base('Spore') + [sprite('art/spore.bmp', 4, (16, 16)), anim('0-1', 6, True),
                            script('scripts/enemy.bs', 'brain = \"flyer\", hp = 1, damage = 8, drops = false'), tags('enemy, spore')])
    prefab('bolt', 'lightning from Doctor Brine', base('Bolt') + [sprite('art/bolt.bmp', 7, (16, 192), 0, 'add'), anim('0-3', 16, False), script('scripts/fx.bs')])
    print('wrote the prefabs')


if __name__ == '__main__':
    main()
