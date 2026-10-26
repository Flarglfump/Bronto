#!/usr/bin/env python3
"""Pictures of a stretch of a recording, for seeing what the bot did:

    python3 viewrec.py run.brec FROM TO [EVERY] [out.png] [--scene zz_test.scene]

plays the recording to frame FROM, then saves a frame every EVERY frames up to TO as one
contact sheet (a PNG, 4 across)."""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import util                    # noqa: E402
from runner import Runner, KEYS  # noqa: E402


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    bp = struct.unpack('<H', d[28:30])[0] // 8
    stride = (w * bp + 3) & ~3
    rows = []
    for y in range(abs(h)):
        r = d[off + y * stride: off + y * stride + w * bp]
        rows.append(bytes(c for i in range(w) for c in (r[i * bp + 2], r[i * bp + 1], r[i * bp])))
    if h > 0:
        rows.reverse()
    return w, abs(h), rows


def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)

    def ch(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    scene = None
    if '--scene' in sys.argv:
        scene = sys.argv[sys.argv.index('--scene') + 1]
        args = [a for a in args if a != scene]
    rec, a, b = args[0], int(args[1]), int(args[2])
    every = int(args[3]) if len(args) > 3 else 8
    out = args[4] if len(args) > 4 else os.path.join(os.path.dirname(rec), 'sheet.png')
    keys = {}
    for line in open(rec):
        p = line.split()
        if p and p[0] == 'k':
            keys.setdefault(int(p[1]), []).append((int(p[2][1:]), p[2][0] == '+'))
    codes = {v: k for k, v in KEYS.items()}
    r = Runner(util.RUNNER, util.PROJECT, scene)
    r.prefix(rec, a)
    tmp = os.path.join(os.path.dirname(os.path.abspath(out)), '_view.bmp')
    pics = []
    f = a
    while f < b:
        for code, down in keys.get(f, []):
            r.key(codes.get(code, 'enter'), down) if code in codes else None
        if (f - a) % every == 0:
            r.ask('draw')
            r.ask('save ' + tmp)
            pics.append(read_bmp(tmp))
        else:
            r.frame(1)
        f += 1
    r.close()
    w, h = pics[0][0], pics[0][1]
    cols = 4
    nrow = (len(pics) + cols - 1) // cols
    rows = []
    for j in range(nrow):
        for y in range(h):
            line = b''
            for i in range(cols):
                k = j * cols + i
                line += pics[k][2][y] if k < len(pics) else b'\0' * (w * 3)
            rows.append(line)
    write_png(out, w * cols, h * nrow, rows)
    print(out, len(pics), 'frames')


if __name__ == '__main__':
    main()
