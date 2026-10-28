#!/usr/bin/env python3
"""Makes tests/gltf_fixture.h: a small skinned glTF (a bar of two bones) for test_3d.c.
python3 tests/make_gltf_fixture.py"""
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools', 'gltf'))
from gltf_writer import Model, rot  # noqa: E402

m = Model()
root = m.bone('root', (0, 0, 0))
tip = m.bone('tip', (0, 1, 0), root)
m.box((-0.1, 0, -0.1), (0.1, 1, 0.1), root, (1, 0, 0))
m.box((-0.1, 1, -0.1), (0.1, 2, 0.1), tip, (0, 0, 1))
m.animation('bend', rotations={tip: [(0, rot('z', 0)), (1, rot('z', 90))]})
m.animation('lift', translations={root: [(0, (0, 0, 0)), (2, (0, 4, 0))]})
text = m.gltf_text()
with open(os.path.join(HERE, 'gltf_fixture.h'), 'w') as f:
    f.write('/* A skinned bar of two bones (root at 0, tip at 1 up y), two units tall, with the animations\n')
    f.write(' * "bend" (the tip turns 90 degrees about z over a second) and "lift" (all of it up 4 in 2 s).\n')
    f.write(' * Made by tests/make_gltf_fixture.py. (Bytes: a string this long is past what C99 promises.) */\n')
    f.write('static const char k_gltf_bar[] = {\n')
    data = text.encode() + b'\0'
    for i in range(0, len(data), 24):
        f.write('    ' + ', '.join('%d' % b for b in data[i:i + 24]) + ',\n')
    f.write('};\n')
print('wrote tests/gltf_fixture.h (%d bytes of glTF)' % len(text))
