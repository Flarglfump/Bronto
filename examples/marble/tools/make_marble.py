#!/usr/bin/env python3
"""Makes the Marble example's course: main.scene, the textures in art/ and the gem
model in models/. Run it from anywhere: python3 examples/marble/tools/make_marble.py

The scene is ordinary editor material - after making it once, open the folder in the
editor and change it there. This script is only how the first version was laid out.

Coordinates: y is up and the course runs away from the camera, along -z.
"""
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


# ---- textures (PNG, written by hand with the standard library's zlib) ----------------

def write_png(path, w, h, pixel):
    raw = bytearray()
    for y in range(h):
        raw.append(0)                                   # no filter
        for x in range(w):
            raw += bytes(pixel(x, y))
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def tiles(x, y):                                        # a stone tile, one per unit
    edge = x == 0 or y == 0
    n = ((x * 7 + y * 13) ^ (x * y)) % 5
    if edge:
        return (70, 78, 92)
    return (150 + n * 4, 158 + n * 4, 172 + n * 3)


def grass(x, y):
    n = ((x * 31 + y * 17) ^ (x * 3)) % 7
    if x == 0 or y == 0:
        return (66, 120, 62)
    return (86 + n * 3, 150 + n * 4, 80 + n * 2)


def marble(x, y):                                       # stripes, so you can see it roll
    band = (x // 4 + y // 4) % 2
    swirl = int(20 * math.sin((x + y * 0.5) * 0.6))
    if band:
        return (235, 90 + swirl, 60)
    return (250, 230, 200)


def wood(x, y):
    n = int(10 * math.sin(y * 0.9 + x * 0.15))
    return (150 + n, 100 + n, 60 + n // 2)


# ---- the gem: an octahedron, as a Wavefront .obj ------------------------------------

GEM_OBJ = """# a gem: an octahedron 0.5 across (Marble example)
v 0 0.3 0
v 0 -0.3 0
v 0.22 0 0
v -0.22 0 0
v 0 0 0.22
v 0 0 -0.22
f 1 5 3
f 1 3 6
f 1 6 4
f 1 4 5
f 2 3 5
f 2 6 3
f 2 4 6
f 2 5 4
"""


# ---- the scene ------------------------------------------------------------------------

out = ['# Marble - a 3D example made of scenes and scripts. Open this folder in the editor.',
       '# Made by tools/make_marble.py; edit it in the editor from here on.',
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


def box_collider(size=(1, 1, 1), friction=0.8, trigger=False, bounce=0):
    f = [('shape', 'box'), ('size', size), ('friction', friction), ('bounce', bounce)]
    if trigger:
        f.append(('trigger', True))
    return ('Collider3D', f)


def script(file, vars=''):
    f = [('file', '"%s"' % file)]
    if vars:
        f.append(('vars', '"%s"' % vars))
    return ('Script', f)


def tags(words):
    return ('Tags', [('list', '"%s"' % words)])


def block(name, centre, size, texture='art/tiles.png', color='#ffffffff', rot=(0, 0, 0), extra=()):
    """A solid box: a scaled cube with a matching collider (the collider is 1 x 1 x 1,
    scaled with the entity)."""
    entity(name, t3(centre, rot, size), mesh('cube', color, texture, (size[0], size[2])),
           box_collider(), *extra)


# Light, sky and the camera first.
entity('Sun', t3((0, 10, 0), (-55, 35, 0)), ('Light', [('kind', 'directional'), ('color', '#fff4e0ff'), ('intensity', 1)]))
entity('Sky Light', t3((0, 10, 0)), ('Light', [('kind', 'ambient'), ('color', '#8090b0ff'), ('intensity', 0.45)]))
entity('Camera', t3((0, 4.5, 7), (-28, 0, 0)),
       ('Camera3D', [('fov', 60), ('near_clip', 0.1), ('far_clip', 200), ('clear_color', '#7cb4e8ff'),
                     ('fog', True), ('fog_color', '#7cb4e8ff'), ('fog_start', 18), ('fog_end', 60)]),
       script('scripts/camera.bs'))

# The course, start to finish (away from the camera, along -z).
block('Start', (0, -0.5, 0), (7, 1, 7), 'art/grass.png')
block('Walkway', (0, -0.5, -8.5), (2.5, 1, 10))
for side in (-1, 1):                                  # low walls along the walkway
    block('Rail', (side * 1.5, 0.15, -8.5), (0.5, 0.3, 10), 'art/wood.png')
# the ramp: a slab rising 2 units over 6 (its top from y 0 at z -13.5 to y 2 at z -19.5)
pitch = math.degrees(math.atan2(2, 6))
length = math.hypot(2, 6)
th = 0.5
cx, cy, cz = 0, 1 - th / 2 * math.cos(math.radians(pitch)), -16.5 - th / 2 * math.sin(math.radians(pitch))
block('Ramp', (cx, cy, cz), (2.5, th, length), rot=(pitch, 0, 0))
block('Upper Deck', (0, 1.5, -23.5), (7, 1, 8))
block('Crate', (-1.8, 2.5, -24), (1, 1, 1), 'art/wood.png')
block('Crate', (1.6, 2.5, -21.5), (1, 1, 1), 'art/wood.png', rot=(0, 30, 0))
# the lift: a kinematic platform going up and down between the upper deck and the high deck
entity('Lift', t3((0, 1.75, -29), (0, 0, 0), (3, 0.5, 3)),
       mesh('cube', '#ffffffff', 'art/wood.png', (3, 3)), box_collider(),
       ('RigidBody3D', [('type', 'kinematic')]), script('scripts/lift.bs', 'travel = vec3(0, 3, 0)'))
block('High Deck', (0, 2.5, -35.5), (7, 5, 10))       # its top at y 5, its near wall stops you when the lift is down
# the flag at the end
entity('Pole', t3((0, 6.5, -39), (0, 0, 0), (0.15, 3, 0.15)), mesh('cylinder', '#e8e8f0ff'), box_collider())
entity('Flag', t3((0.45, 7.6, -39), (0, 0, -90), (0.6, 0.8, 0.12)), mesh('cone', '#ff5a4aff'))
entity('Goal', t3((0, 5.75, -38.5)), box_collider((3, 1.5, 3), trigger=True), script('scripts/goal.bs'))
# checkpoints: where a fall brings you back
entity('Checkpoint', t3((0, 2.5, -21)), box_collider((7, 1, 2), trigger=True), tags('checkpoint'))
entity('Checkpoint', t3((0, 5.5, -33)), box_collider((7, 1, 2), trigger=True), tags('checkpoint'))
# trees on the start pad
for i, (x, z) in enumerate(((-2.6, 2.2), (2.7, 1.6), (-2.4, -2.5))):
    entity('Trunk', t3((x, 0.6, z), (0, 0, 0), (0.35, 1.2, 0.35)), mesh('cylinder', '#7a5236ff'), box_collider())
    entity('Leaves', t3((x, 1.9, z), (0, i * 40, 0), (1.4, 1.8, 1.4)), mesh('cone', '#3f9a4cff'))
# a bouncy bumper
entity('Bumper', t3((2.2, 0.4, -2.0), (0, 0, 0), (0.8, 0.8, 0.8)), mesh('sphere', '#4cc3ffff'),
       ('Collider3D', [('shape', 'sphere'), ('radius', 0.5), ('bounce', 1), ('friction', 0.2)]))

# gems (the model), each a trigger
gems = [(1.5, 0.6, -1.5), (0, 0.6, -6), (0, 0.6, -10.5), (0, 1.6, -16.5), (-2.2, 2.6, -21.5),
        (2.2, 2.6, -25.5), (0, 4.2, -29), (-2.2, 5.6, -36)]
for p in gems:
    entity('Gem', t3(p, (0, 0, 0), (1.4, 1.4, 1.4)), mesh('model', '#5ff2ffd0', file='models/gem.obj', shadow=True),
           ('Collider3D', [('shape', 'sphere'), ('radius', 0.35), ('trigger', True)]),
           tags('gem'), script('scripts/gem.bs'))

# the marble last but one: a textured sphere that rolls
entity('Marble', t3((0, 0.8, 1.5)), mesh('sphere', '#ffffffff', 'art/marble.png', shadow=True),
       ('RigidBody3D', [('mass', 1), ('linear_damping', 0.15), ('angular_damping', 0.4)]),
       ('Collider3D', [('shape', 'sphere'), ('radius', 0.5), ('friction', 0.9)]),
       script('scripts/marble.bs'))

# the HUD: 2D labels, drawn over the 3D view
entity('HUD', ('Transform', [('position', (160, 10))]),
       ('Label', [('text', '"Gems 0/8"'), ('size', 1), ('color', '#ffffffff'), ('layer', 5)]),
       script('scripts/hud.bs'))
entity('Message', ('Transform', [('position', (160, 34))]),
       ('Label', [('text', '"Roll to the flag!"'), ('size', 1), ('color', '#ffe88aff'), ('layer', 5)]))
entity('Help', ('Transform', [('position', (160, 190))]),
       ('Label', [('text', '"arrows / stick: roll   space / A: jump"'), ('size', 1), ('color', '#e0e8f4c0'), ('layer', 5)]))

os.makedirs(os.path.join(ROOT, 'art'), exist_ok=True)
os.makedirs(os.path.join(ROOT, 'models'), exist_ok=True)
write_png(os.path.join(ROOT, 'art', 'tiles.png'), 16, 16, tiles)
write_png(os.path.join(ROOT, 'art', 'grass.png'), 16, 16, grass)
write_png(os.path.join(ROOT, 'art', 'marble.png'), 32, 16, marble)
write_png(os.path.join(ROOT, 'art', 'wood.png'), 16, 16, wood)
with open(os.path.join(ROOT, 'models', 'gem.obj'), 'w') as f:
    f.write(GEM_OBJ)
with open(os.path.join(ROOT, 'main.scene'), 'w') as f:
    f.write('\n'.join(out))
print('wrote main.scene (%d entities), art/*.png, models/gem.obj' % out.count('entity'))
