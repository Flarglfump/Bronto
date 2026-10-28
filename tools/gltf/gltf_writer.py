"""A small glTF 2.0 writer for Bronto's example tools: boxes skinned to bones,
with keyframed animations, written as .glb (or .gltf with the data inside).

    from gltf_writer import Model
    m = Model()
    hips = m.bone('hips', (0, 0, 0))
    leg = m.bone('leg', (0.1, -0.2, 0), parent=hips)
    m.box((-0.1, -0.2, -0.1), (0.1, 0.2, 0.1), bone=hips, color=(1, 0.5, 0.2))
    m.animation('walk', {leg: [(0, rot_x(30)), (0.4, rot_x(-30)), (0.8, rot_x(30))]})
    m.save_glb('hero.glb')

Bones are placed by where they are at rest, in the model's space; their
rotations in animations are quaternions (x, y, z, w) relative to that rest.
"""
import base64
import json
import math
import struct


def rot(axis, degrees):
    """A quaternion (x, y, z, w) turning `degrees` about an axis ('x', 'y' or 'z')."""
    h = math.radians(degrees) / 2
    s, c = math.sin(h), math.cos(h)
    return {'x': (s, 0, 0, c), 'y': (0, s, 0, c), 'z': (0, 0, s, c)}[axis]


class Model:
    def __init__(self):
        self.bones = []          # (name, rest position (model space), parent index)
        self.boxes = []          # (lo, hi, bone, colour (linear 0..1))
        self.anims = []          # (name, {bone: {'r': [(t, quat)], 't': [(t, xyz)]}})

    def bone(self, name, at, parent=None):
        self.bones.append((name, tuple(at), parent))
        return len(self.bones) - 1

    def box(self, lo, hi, bone, color):
        """A box from lo to hi (model space at rest), moved by `bone`; color as on screen (sRGB, 0..1)."""
        self.boxes.append((tuple(lo), tuple(hi), bone, tuple(color)))

    def animation(self, name, rotations=None, translations=None):
        tracks = {}
        for b, keys in (rotations or {}).items():
            tracks.setdefault(b, {})['r'] = keys
        for b, keys in (translations or {}).items():
            tracks.setdefault(b, {})['t'] = keys
        self.anims.append((name, tracks))

    # ---- writing ----
    def _build(self):
        buf = bytearray()
        views, accessors = [], []

        def add(data, ctype, count, kind, target=None, mn=None, mx=None):
            while len(buf) % 4:
                buf.append(0)
            view = {'buffer': 0, 'byteOffset': len(buf), 'byteLength': len(data)}
            if target:
                view['target'] = target
            buf.extend(data)
            views.append(view)
            acc = {'bufferView': len(views) - 1, 'componentType': ctype, 'count': count, 'type': kind}
            if mn is not None:
                acc['min'], acc['max'] = mn, mx
            accessors.append(acc)
            return len(accessors) - 1

        # materials: one per distinct colour
        colours = []
        for _, _, _, c in self.boxes:
            if c not in colours:
                colours.append(c)
        def linear(v):                    # (glTF colours are linear)
            return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
        materials = [{'pbrMetallicRoughness': {'baseColorFactor': [round(linear(c[0]), 5), round(linear(c[1]), 5), round(linear(c[2]), 5), 1.0],
                                               'metallicFactor': 0, 'roughnessFactor': 1}}
                     for c in colours]
        prims = []
        for colour in colours:            # a primitive per colour: every box of it
            pos, nor, joints, weights, idx = [], [], [], [], []
            for lo, hi, bone, c in self.boxes:
                if c != colour:
                    continue
                x0, y0, z0 = lo
                x1, y1, z1 = hi
                faces = [((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
                         ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
                         ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
                         ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
                         ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
                         ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)])]
                for n, quad in faces:
                    base = len(pos)
                    for p in quad:
                        pos.append(p)
                        nor.append(n)
                        joints.append((bone, 0, 0, 0))
                        weights.append((1.0, 0, 0, 0))
                    idx += [base, base + 1, base + 2, base, base + 2, base + 3]
            mn = [min(p[k] for p in pos) for k in range(3)]
            mx = [max(p[k] for p in pos) for k in range(3)]
            a_pos = add(b''.join(struct.pack('<3f', *p) for p in pos), 5126, len(pos), 'VEC3', 34962, mn, mx)
            a_nor = add(b''.join(struct.pack('<3f', *n) for n in nor), 5126, len(nor), 'VEC3', 34962)
            a_j = add(b''.join(struct.pack('<4B', *j) for j in joints), 5121, len(joints), 'VEC4', 34962)
            a_w = add(b''.join(struct.pack('<4f', *w) for w in weights), 5126, len(weights), 'VEC4', 34962)
            a_i = add(b''.join(struct.pack('<H', i) for i in idx), 5123, len(idx), 'SCALAR', 34963)
            prims.append({'attributes': {'POSITION': a_pos, 'NORMAL': a_nor, 'JOINTS_0': a_j, 'WEIGHTS_0': a_w},
                          'indices': a_i, 'material': colours.index(colour)})
        # nodes: node 0 holds the mesh; the bones follow (1 + i)
        nodes = [{'name': 'body', 'mesh': 0, 'skin': 0}]
        roots = [0]
        for i, (name, at, parent) in enumerate(self.bones):
            rel = at if parent is None else tuple(at[k] - self.bones[parent][1][k] for k in range(3))
            nodes.append({'name': name, 'translation': list(rel)})
            if parent is None:
                roots.append(1 + i)
            else:
                nodes[1 + parent].setdefault('children', []).append(1 + i)
        ibm = b''.join(struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -at[0], -at[1], -at[2], 1)
                       for _, at, _ in self.bones)
        skin = {'joints': [1 + i for i in range(len(self.bones))], 'inverseBindMatrices': add(ibm, 5126, len(self.bones), 'MAT4')}
        animations = []
        for name, tracks in self.anims:
            samplers, channels = [], []
            for b, t in tracks.items():
                for path, keys in t.items():
                    times = add(b''.join(struct.pack('<f', k[0]) for k in keys), 5126, len(keys), 'SCALAR',
                                mn=[min(k[0] for k in keys)], mx=[max(k[0] for k in keys)])
                    if path == 'r':
                        vals = add(b''.join(struct.pack('<4f', *k[1]) for k in keys), 5126, len(keys), 'VEC4')
                    else:
                        rest = self.bones[b][1] if self.bones[b][2] is None else tuple(
                            self.bones[b][1][k] - self.bones[self.bones[b][2]][1][k] for k in range(3))
                        vals = add(b''.join(struct.pack('<3f', *(rest[i] + k[1][i] for i in range(3))) for k in keys),
                                   5126, len(keys), 'VEC3')
                    samplers.append({'input': times, 'output': vals, 'interpolation': 'LINEAR'})
                    channels.append({'sampler': len(samplers) - 1, 'target': {'node': 1 + b, 'path': 'rotation' if path == 'r' else 'translation'}})
            animations.append({'name': name, 'samplers': samplers, 'channels': channels})
        doc = {'asset': {'version': '2.0', 'generator': 'Bronto gltf_writer.py'},
               'scene': 0, 'scenes': [{'nodes': roots}], 'nodes': nodes,
               'meshes': [{'name': 'body', 'primitives': prims}], 'materials': materials,
               'skins': [skin], 'animations': animations,
               'buffers': [{'byteLength': len(buf)}], 'bufferViews': views, 'accessors': accessors}
        return doc, bytes(buf)

    def save_glb(self, path):
        doc, data = self._build()
        js = json.dumps(doc, separators=(',', ':')).encode()
        js += b' ' * (-len(js) % 4)
        data += b'\0' * (-len(data) % 4)
        total = 12 + 8 + len(js) + 8 + len(data)
        with open(path, 'wb') as f:
            f.write(b'glTF' + struct.pack('<II', 2, total))
            f.write(struct.pack('<I', len(js)) + b'JSON' + js)
            f.write(struct.pack('<I', len(data)) + b'BIN\0' + data)

    def gltf_text(self):
        """The model as one .gltf text, its data inside as base64."""
        doc, data = self._build()
        doc['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()
        return json.dumps(doc, indent=1)
