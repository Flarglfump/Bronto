#!/usr/bin/env python3
"""Makes the Explorer example: the rolling hills (a model, also its collider), its
textures, the crystal model and main.scene. Run it from anywhere:
python3 examples/explorer/tools/make_explorer.py

As with Marble, the scene is ordinary editor material afterwards: open the folder in
the editor and change it there.
"""
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIZE = 48                      # the land is SIZE x SIZE units, centred on 0, 0
CELLS = 24


def height(x, z):
    """The land's height at x, z: gentle hills, flat near the middle where you start."""
    h = 1.6 * math.sin(x * 0.23) * math.cos(z * 0.19) + 0.9 * math.sin(x * 0.11 + 1.3) + 0.7 * math.cos(z * 0.13 - 0.4)
    h += 2.5 * max(0.0, (abs(x) + abs(z) - 30) / 10)      # rising at the edges, a bowl to keep you in
    flat = min(1.0, math.hypot(x, z) / 8)
    return h * flat


# ---- files ------------------------------------------------------------------------------

def write_png(path, w, h, pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(pixel(x, y))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def grass(x, y):
    n = ((x * 31 + y * 17) ^ (x * 7 + y)) % 9
    tuft = (x * 13 + y * 29) % 23 == 0
    return (70 + n * 3, 140 + n * 5 + (25 if tuft else 0), 70 + n * 2)


def bark(x, y):
    n = int(12 * math.sin(x * 1.3 + y * 0.2))
    return (110 + n, 76 + n, 50 + n // 2)


def stone(x, y):
    n = ((x * 7 + y * 11) ^ (x * y)) % 6
    return (128 + n * 6, 128 + n * 6, 136 + n * 5)


def terrain_obj():
    lines = ['# the hills of the Explorer example (made by tools/make_explorer.py)']
    step = SIZE / CELLS
    for j in range(CELLS + 1):
        for i in range(CELLS + 1):
            x, z = -SIZE / 2 + i * step, -SIZE / 2 + j * step
            lines.append('v %.4f %.4f %.4f' % (x, height(x, z), z))
    for j in range(CELLS + 1):
        for i in range(CELLS + 1):
            x, z = -SIZE / 2 + i * step, -SIZE / 2 + j * step
            lines.append('vt %.4f %.4f' % (x / 4, -z / 4))   # the grass repeats every 4 units
    for j in range(CELLS + 1):
        for i in range(CELLS + 1):
            x, z = -SIZE / 2 + i * step, -SIZE / 2 + j * step
            e = 0.05
            nx, nz = height(x - e, z) - height(x + e, z), height(x, z - e) - height(x, z + e)
            l = math.sqrt(nx * nx + (2 * e) ** 2 + nz * nz)
            lines.append('vn %.4f %.4f %.4f' % (nx / l, 2 * e / l, nz / l))
    for j in range(CELLS):
        for i in range(CELLS):
            a = j * (CELLS + 1) + i + 1
            b, c, d = a + 1, a + CELLS + 1, a + CELLS + 2
            lines.append('f %d/%d/%d %d/%d/%d %d/%d/%d' % (a, a, a, c, c, c, d, d, d))   # counter-clockwise from above
            lines.append('f %d/%d/%d %d/%d/%d %d/%d/%d' % (a, a, a, d, d, d, b, b, b))
    return '\n'.join(lines) + '\n'


CRYSTAL_OBJ = """# a crystal: a long octahedron
v 0 0.6 0
v 0 -0.6 0
v 0.25 0 0
v -0.25 0 0
v 0 0 0.25
v 0 0 -0.25
f 1 5 3
f 1 3 6
f 1 6 4
f 1 4 5
f 2 3 5
f 2 6 3
f 2 4 6
f 2 5 4
"""

# ---- the explorer: a blocky figure with a skeleton, and its animations ---------------------

def explorer_model():
    sys.path.insert(0, os.path.join(ROOT, '..', '..', 'tools', 'gltf'))
    from gltf_writer import Model, rot
    shirt, skin, trousers, boots, pack, hair = (0.93, 0.5, 0.18), (0.96, 0.78, 0.62), (0.26, 0.33, 0.52), (0.36, 0.22, 0.12), (0.56, 0.36, 0.18), (0.33, 0.2, 0.1)
    m = Model()
    hips = m.bone('hips', (0, -0.12, 0))
    spine = m.bone('spine', (0, 0.12, 0), hips)
    head = m.bone('head', (0, 0.62, 0), spine)
    bones = {}
    for side, x in (('l', 1), ('r', -1)):                # (it faces -z: its left is +x)
        bones['arm_' + side] = m.bone('arm_' + side, (0.31 * x, 0.58, 0), spine)
        bones['hand_' + side] = m.bone('hand_' + side, (0.31 * x, 0.24, 0), bones['arm_' + side])
        bones['leg_' + side] = m.bone('leg_' + side, (0.12 * x, -0.16, 0), hips)
        bones['shin_' + side] = m.bone('shin_' + side, (0.12 * x, -0.58, 0), bones['leg_' + side])
    m.box((-0.23, -0.22, -0.13), (0.23, -0.02, 0.13), hips, trousers)
    m.box((-0.24, -0.04, -0.14), (0.24, 0.62, 0.14), spine, shirt)
    m.box((-0.18, 0.04, 0.14), (0.18, 0.5, 0.3), spine, pack)                      # a backpack (on its back: +z)
    m.box((-0.16, 0.62, -0.16), (0.16, 0.96, 0.16), head, skin)
    m.box((-0.17, 0.9, -0.17), (0.17, 1.0, 0.17), head, hair)
    m.box((-0.05, 0.76, -0.175), (-0.02, 0.81, -0.16), head, hair)                # eyes, in front (-z)
    m.box((0.02, 0.76, -0.175), (0.05, 0.81, -0.16), head, hair)
    for side, x in (('l', 1), ('r', -1)):
        cx = 0.31 * x
        m.box((cx - 0.06, 0.22, -0.06), (cx + 0.06, 0.6, 0.06), bones['arm_' + side], shirt)
        m.box((cx - 0.05, -0.08, -0.05), (cx + 0.05, 0.24, 0.05), bones['hand_' + side], skin)
        lx = 0.12 * x
        m.box((lx - 0.08, -0.6, -0.08), (lx + 0.08, -0.14, 0.08), bones['leg_' + side], trousers)
        m.box((lx - 0.075, -1.0, -0.075), (lx + 0.075, -0.56, 0.075), bones['shin_' + side], trousers)
        m.box((lx - 0.08, -1.0, -0.2), (lx + 0.08, -0.88, 0.09), bones['shin_' + side], boots)
    # idle: breathing, arms hanging and swaying a little
    m.animation('idle', rotations={
        bones['arm_l']: [(0, rot('z', 4)), (1, rot('z', 6)), (2, rot('z', 4))],
        bones['arm_r']: [(0, rot('z', -4)), (1, rot('z', -6)), (2, rot('z', -4))],
        head: [(0, rot('x', 0)), (1, rot('x', -3)), (2, rot('x', 0))],
    }, translations={hips: [(0, (0, 0, 0)), (1, (0, 0.015, 0)), (2, (0, 0, 0))]})
    # walk: one stride each way in 0.8 seconds
    def swing(amount, phase, n=8, length=0.8):
        return [(length * i / n, rot('x', amount * math.sin(2 * math.pi * i / n + phase))) for i in range(n + 1)]
    def bend(amount, phase, n=8, length=0.8):
        return [(length * i / n, rot('x', -amount * max(0.0, math.sin(2 * math.pi * i / n + phase)))) for i in range(n + 1)]
    m.animation('walk', rotations={
        bones['leg_l']: swing(32, 0), bones['leg_r']: swing(32, math.pi),
        bones['shin_l']: bend(40, -math.pi / 2), bones['shin_r']: bend(40, math.pi / 2),
        bones['arm_l']: swing(-28, 0), bones['arm_r']: swing(-28, math.pi),
        bones['hand_l']: [(t, rot('x', 15)) for t in (0, 0.8)], bones['hand_r']: [(t, rot('x', 15)) for t in (0, 0.8)],
    }, translations={hips: [(0.8 * i / 8, (0, 0.04 * abs(math.cos(2 * math.pi * i / 8)) - 0.02, 0)) for i in range(9)]})
    # jump: knees up, arms up
    m.animation('jump', rotations={
        bones['leg_l']: [(0, rot('x', 0)), (0.15, rot('x', 50))], bones['leg_r']: [(0, rot('x', 0)), (0.15, rot('x', 20))],
        bones['shin_l']: [(0, rot('x', 0)), (0.15, rot('x', -70))], bones['shin_r']: [(0, rot('x', 0)), (0.15, rot('x', -40))],
        bones['arm_l']: [(0, rot('x', 0)), (0.15, rot('x', 150))], bones['arm_r']: [(0, rot('x', 0)), (0.15, rot('x', 150))],
    })
    # cheer: when the last crystal is found
    m.animation('cheer', rotations={
        bones['arm_l']: [(0, rot('z', 150)), (0.3, rot('z', 170)), (0.6, rot('z', 150))],
        bones['arm_r']: [(0, rot('z', -150)), (0.3, rot('z', -170)), (0.6, rot('z', -150))],
    }, translations={hips: [(0, (0, 0, 0)), (0.3, (0, 0.18, 0)), (0.6, (0, 0, 0))]})
    return m


# ---- the scene ----------------------------------------------------------------------------

out = ['# Explorer - a 3D example: walk the hills, find the crystals. Open this folder in the editor.',
       '# Made by tools/make_explorer.py; edit it in the editor from here on.',
       'bronto_scene 1', '']


def fmt(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, (int, float)):
        s = ('%.4f' % v).rstrip('0').rstrip('.')
        return '0' if s == '-0' else s
    if isinstance(v, tuple):
        return ' '.join(fmt(c) for c in v)
    return v


def entity(name, *components):
    out.append('entity')
    out.append('  Name')
    out.append('    text = "%s"' % name)
    for comp, fields in components:
        out.append('  ' + comp)
        for k, v in fields:
            out.append('    %s = %s' % (k, fmt(v)))
    out.append('')


def t3(pos, rot=(0, 0, 0), scale=(1, 1, 1)):
    return ('Transform3D', [('position', pos), ('rotation', rot), ('scale', scale)])


def mesh(shape, color, texture='', uv=(1, 1), file='', shadow=False):
    f = [('shape', shape)]
    if shadow:
        f.append(('shadow', True))
    if file:
        f.append(('file', '"%s"' % file))
    if texture:
        f.append(('texture', '"%s"' % texture))
    f += [('color', color), ('uv_scale', uv)]
    return ('Mesh', f)


def collider(shape, size=(1, 1, 1), radius=0.5, trigger=False):
    f = [('shape', shape), ('size', size), ('radius', radius), ('friction', 0.8)]
    if trigger:
        f.append(('trigger', True))
    return ('Collider3D', f)


def particles(fields):
    return ('Particles', fields)


# the crystals' glow: a few motes drifting up around each, moving with it
GLOW = [('rate', 6), ('max', 30), ('lifetime', 1.4), ('lifetime_random', 0.4), ('speed', 0.35), ('speed_random', 0.5),
        ('spread', 60), ('area', (0.8, 0.8)), ('gravity', -0.3), ('size_start', 0.12), ('size_end', 0),
        ('color_start', '#e0d0ffff'), ('color_end', '#a0f0ff00'), ('blend', 'add'), ('local', True)]


def script(file, vars=''):
    f = [('file', '"%s"' % file)]
    if vars:
        f.append(('vars', '"%s"' % vars))
    return ('Script', f)


entity('Sun', t3((0, 20, 0), (-50, -30, 0)), ('Light', [('kind', 'directional'), ('color', '#fff2dcff'), ('intensity', 1),
                                                          ('shadows', True), ('shadow_range', 16)]))
entity('Sky Light', t3((0, 20, 0)), ('Light', [('kind', 'ambient'), ('color', '#8898c0ff'), ('intensity', 0.45)]))
entity('Camera', t3((0, 5, 9)),
       ('Camera3D', [('fov', 60), ('near_clip', 0.1), ('far_clip', 120), ('clear_color', '#9cc4ecff'),
                     ('fog', True), ('fog_color', '#9cc4ecff'), ('fog_start', 14), ('fog_end', 40)]),
       script('scripts/camera.bs'))
entity('Hills', t3((0, 0, 0)), mesh('model', '#ffffffff', 'art/grass.png', file='models/hills.obj'),
       collider('mesh'))

# trees: a trunk (a solid capsule) and leaves (cones)
trees = [(-6, -9), (7, -6), (-12, 3), (11, 9), (-4, 14), (15, -14), (-15, -15), (3, -17), (-18, 10), (18, 2)]
for i, (x, z) in enumerate(trees):
    y = height(x, z)
    entity('Trunk', t3((x, y + 1.2, z), (0, 0, 0), (0.5, 2.4, 0.5)), mesh('cylinder', '#ffffffff', 'art/bark.png'),
           collider('capsule', (1, 1, 1), 0.5))
    entity('Leaves', t3((x, y + 3.2, z), (0, i * 37, 0), (2.4, 2.8, 2.4)), mesh('cone', '#3d9450ff', shadow=False))
# rocks to walk round (or jump on)
rocks = [(4, 4, 1.2), (-8, -3, 1.6), (9, -13, 2.0), (-13, -8, 1.1), (13, 14, 1.5)]
for x, z, s in rocks:
    entity('Rock', t3((x, height(x, z) + s * 0.25, z), (0, x * 10, 0), (s, s * 0.7, s)), mesh('sphere', '#ffffffff', 'art/stone.png'),
           collider('sphere', radius=0.5))
# stepping stones up to a high crystal
steps = [(-2, 8, 0.8), (0.5, 10.5, 1.6), (3, 13, 2.4)]
for x, z, top in steps:
    base = height(x, z)
    h = top + 0.5
    entity('Step', t3((x, base + top - h / 2 + 0.5, z), (0, 15, 0), (1.6, h, 1.6)), mesh('cube', '#ffffffff', 'art/stone.png', (1, 1)),
           collider('box'))

# the crystals: triggers
crystals = [(0, -6), (8, -2), (-9, 7), (12, -9), (-14, -12), (6, 16), (-17, 0)]
for x, z in crystals:
    entity('Crystal', t3((x, height(x, z) + 1.0, z)), mesh('model', '#9a7dffd8', file='models/crystal.obj', shadow=True),
           collider('sphere', radius=0.6, trigger=True), ('Tags', [('list', '"crystal"')]), particles(GLOW), script('scripts/crystal.bs'))
x, z, top = steps[-1]
entity('Crystal', t3((x, height(x, z) + top + 1.6, z)), mesh('model', '#ffd36ad8', file='models/crystal.obj', shadow=True),
       collider('sphere', radius=0.6, trigger=True), ('Tags', [('list', '"crystal"')]), particles(GLOW), script('scripts/crystal.bs'))

# the explorer: a capsule, moved by its script with move_and_slide() (kinematic: the
# physics doesn't move it, but it sets off triggers)
entity('Explorer', t3((0, height(0, 0) + 1.05, 0)), mesh('model', '#ffffffff', file='models/explorer.glb', shadow=True),
       ('Animator', [('clip', '"idle"'), ('speed', 1), ('loop', True), ('playing', True), ('blend', 0.15)]),
       collider('capsule', (1, 2, 1), 0.5), ('RigidBody3D', [('type', 'kinematic')]), script('scripts/explorer.bs'))

entity('HUD', ('Transform', [('position', (160, 10))]),
       ('Label', [('text', '"Crystals 0/8"'), ('size', 1), ('color', '#ffffffff'), ('layer', 5)]), script('scripts/hud.bs'))
entity('Message', ('Transform', [('position', (160, 34))]),
       ('Label', [('text', '"Find the eight crystals!"'), ('size', 1), ('color', '#fff0a0ff'), ('layer', 5)]))
entity('Help', ('Transform', [('position', (160, 190))]),
       ('Label', [('text', '"WASD walk   Q E look   space jump"'), ('size', 1), ('color', '#e8eef8c0'), ('layer', 5)]))

os.makedirs(os.path.join(ROOT, 'art'), exist_ok=True)
os.makedirs(os.path.join(ROOT, 'models'), exist_ok=True)
write_png(os.path.join(ROOT, 'art', 'grass.png'), 32, 32, grass)
write_png(os.path.join(ROOT, 'art', 'bark.png'), 16, 16, bark)
write_png(os.path.join(ROOT, 'art', 'stone.png'), 16, 16, stone)
with open(os.path.join(ROOT, 'models', 'hills.obj'), 'w') as f:
    f.write(terrain_obj())
with open(os.path.join(ROOT, 'models', 'crystal.obj'), 'w') as f:
    f.write(CRYSTAL_OBJ)
explorer_model().save_glb(os.path.join(ROOT, 'models', 'explorer.glb'))
n_main = out.count('entity')
with open(os.path.join(ROOT, 'main.scene'), 'w') as f:
    f.write('\n'.join(out))

# the burst when one is picked up (crystal.bs spawns it; auto_destroy takes it away after)
out = ['# A burst of light where a crystal was picked up. Made by tools/make_explorer.py.', 'bronto_scene 1', '']
entity('Sparkle', t3((0, 0, 0)),
       particles([('rate', 0), ('burst', 40), ('max', 60), ('lifetime', 0.8), ('lifetime_random', 0.5), ('speed', 4),
                  ('speed_random', 0.6), ('spread', 180), ('gravity', 4), ('drag', 1.5), ('size_start', 0.18), ('size_end', 0.02),
                  ('color_start', '#fff4c0ff'), ('color_end', '#a080ff00'), ('blend', 'add'), ('auto_destroy', True)]))
os.makedirs(os.path.join(ROOT, 'prefabs'), exist_ok=True)
with open(os.path.join(ROOT, 'prefabs', 'sparkle.scene'), 'w') as f:
    f.write('\n'.join(out))
print('wrote main.scene (%d entities), prefabs/sparkle.scene, art/*.png, models/*.obj, models/explorer.glb' % n_main)
