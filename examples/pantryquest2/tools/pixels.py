"""Pixel helpers shared by the art tools: a canvas, pixel text, sheets, BMP and PNG files.

Only Python's standard library. Pictures are lists of (r, g, b, a) tuples, row by row.
"""
import math
import os
import struct
import zlib

# The palette: one character per colour, close to an old console's ('.' is transparent).
PALETTE = {
    '.': None,
    'k': (18, 12, 20),       # outline, near black
    'K': (40, 30, 44),       # very dark purple
    'w': (252, 248, 240),    # white
    'W': (206, 202, 214),    # light grey
    'g': (150, 146, 162),    # grey
    'G': (96, 92, 110),      # dark grey
    'D': (60, 56, 74),       # darker grey
    's': (252, 200, 160),    # skin
    'S': (220, 150, 110),    # skin shade
    'r': (228, 52, 52),      # red
    'R': (150, 20, 40),      # dark red
    'q': (96, 10, 30),       # blood red
    'o': (248, 140, 40),     # orange
    'O': (186, 84, 20),      # dark orange
    'y': (252, 220, 72),     # yellow
    'Y': (204, 156, 28),     # gold
    'e': (252, 236, 180),    # cream / cheese light
    'E': (232, 196, 100),    # cheese
    'F': (176, 132, 44),     # cheese shade
    'n': (170, 106, 54),     # bread / wood
    'N': (110, 62, 30),      # dark bread
    'M': (70, 38, 22),       # very dark brown
    't': (222, 176, 112),    # crust light
    'l': (150, 224, 84),     # light green
    'L': (60, 168, 60),      # green
    'd': (24, 96, 50),       # dark green
    'm': (110, 150, 60),     # mold green
    'z': (66, 92, 40),       # dark mold
    'c': (140, 220, 248),    # ice light
    'C': (210, 244, 252),    # ice white
    'b': (60, 120, 228),     # blue
    'B': (32, 54, 140),      # dark blue
    'i': (20, 28, 80),       # night blue
    'p': (248, 160, 204),    # pink
    'P': (150, 70, 196),     # purple
    'u': (96, 40, 140),      # dark purple
    'v': (200, 130, 240),    # lilac
    'a': (255, 255, 255),    # pure white shine
    'x': (0, 0, 0),          # black
    'j': (190, 236, 120),    # brine light
    'J': (90, 160, 70),      # brine
    'h': (60, 110, 50),      # brine dark
    'f': (255, 110, 40),     # flame
    'H': (255, 196, 60),     # hot glow
    'A': (120, 60, 30),      # rust
    'T': (60, 160, 160),     # teal
    'U': (24, 80, 90),       # dark teal
}


def rgba(c, a=255):
    return (c[0], c[1], c[2], a)


class Canvas:
    """A picture to draw into."""

    def __init__(self, w, h, fill=None):
        self.w, self.h = w, h
        self.px = [fill if fill else (0, 0, 0, 0)] * (w * h)

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return (0, 0, 0, 0)

    def set(self, x, y, c):
        if c is None:
            return
        if len(c) == 3:
            c = (c[0], c[1], c[2], 255)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def blend(self, x, y, c, alpha):
        if not (0 <= x < self.w and 0 <= y < self.h):
            return
        r, g, b, a = self.get(x, y)
        if a == 0:
            self.set(x, y, (c[0], c[1], c[2], int(alpha * 255)))
            return
        self.set(x, y, (int(r + (c[0] - r) * alpha), int(g + (c[1] - g) * alpha), int(b + (c[2] - b) * alpha), a))

    def rect(self, x0, y0, w, h, c):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                self.set(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                dx, dy = (x + 0.5 - cx) / max(rx, 0.01), (y + 0.5 - cy) / max(ry, 0.01)
                if dx * dx + dy * dy <= 1.0:
                    self.set(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(n, 1)
            self.set(int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t)), c)

    def paste(self, other, ox, oy, flip=False):
        for y in range(other.h):
            for x in range(other.w):
                p = other.get(other.w - 1 - x if flip else x, y)
                if p[3]:
                    self.set(ox + x, oy + y, p)

    def text(self, rows, ox=0, oy=0, pal=None, flip=False):
        """Pixel text: one character per pixel, from the palette."""
        pal = pal or PALETTE
        for y, row in enumerate(rows):
            w = len(row)
            for x, ch in enumerate(row):
                c = pal.get(ch) if ch in pal else PALETTE.get(ch)
                if c is not None:
                    self.set(ox + (w - 1 - x if flip else x), oy + y, rgba(c))

    def outline(self, color=PALETTE['k']):
        """Puts a 1-pixel outline round everything drawn (in the transparent pixels)."""
        add = []
        for y in range(self.h):
            for x in range(self.w):
                if self.get(x, y)[3]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if self.get(x + dx, y + dy)[3] and self.get(x + dx, y + dy)[:3] != tuple(color):
                        add.append((x, y))
                        break
        for x, y in add:
            self.set(x, y, rgba(color))

    def shade(self, light=(-1, -1), amount=0.25):
        """Darkens pixels away from the light and brightens those facing it (inside the shape)."""
        out = list(self.px)
        for y in range(self.h):
            for x in range(self.w):
                p = self.get(x, y)
                if not p[3]:
                    continue
                toward = self.get(x + light[0], y + light[1])[3]
                away = self.get(x - light[0], y - light[1])[3]
                f = 0
                if not toward:
                    f = amount
                elif not away:
                    f = -amount
                if f:
                    out[y * self.w + x] = tuple(max(0, min(255, int(v + (255 - v) * f if f > 0 else v * (1 + f)))) for v in p[:3]) + (p[3],)
        self.px = out

    def recolor(self, swap):
        """Replaces colours: {(r,g,b): (r,g,b)}."""
        for i, p in enumerate(self.px):
            if p[3] and p[:3] in swap:
                c = swap[p[:3]]
                self.px[i] = (c[0], c[1], c[2], p[3])

    def copy(self):
        c = Canvas(self.w, self.h)
        c.px = list(self.px)
        return c

    def flipped(self):
        c = Canvas(self.w, self.h)
        for y in range(self.h):
            for x in range(self.w):
                c.px[y * self.w + x] = self.px[y * self.w + self.w - 1 - x]
        return c

    def scaled(self, k):
        c = Canvas(self.w * k, self.h * k)
        for y in range(c.h):
            for x in range(c.w):
                c.px[y * c.w + x] = self.px[(y // k) * self.w + x // k]
        return c


def from_text(rows, pal=None):
    w = max(len(r) for r in rows)
    c = Canvas(w, len(rows))
    c.text(rows, pal=pal)
    return c


def sheet(frames, fw, fh, per_row=None):
    """Frames of the same size side by side (per_row a row), as a sprite sheet."""
    per_row = per_row or len(frames)
    rows = (len(frames) + per_row - 1) // per_row
    out = Canvas(fw * per_row, fh * rows)
    for i, f in enumerate(frames):
        ox, oy = (i % per_row) * fw, (i // per_row) * fh
        out.paste(f, ox + (fw - f.w) // 2, oy + (fh - f.h) // 2)
    return out


def write_bmp(path, c):
    """32-bit top-down BMP with an alpha mask (what br_image_save_bmp writes)."""
    header = 14 + 108
    data = bytearray()
    for (r, g, b, a) in c.px:
        data += bytes((b, g, r, a))
    info = struct.pack('<IiiHHIIiiII', 108, c.w, -c.h, 1, 32, 3, len(data), 2835, 2835, 0, 0)
    info += struct.pack('<IIIII', 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0x73524742)
    info += bytes(108 - len(info))
    head = b'BM' + struct.pack('<IHHI', header + len(data), 0, 0, header)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, 'wb') as f:
        f.write(head + info + data)


def write_png(path, c):
    raw = b''.join(b'\x00' + bytes(sum((list(p) for p in c.px[y * c.w:(y + 1) * c.w]), [])) for y in range(c.h))

    def chunk(t, d):
        out = struct.pack('>I', len(d)) + t + d
        return out + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', c.w, c.h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def noise(x, y, seed=0):
    """A repeatable 0..1 value for a pixel."""
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
