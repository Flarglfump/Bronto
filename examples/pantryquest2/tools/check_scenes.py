#!/usr/bin/env python3
"""Checks the hand-written scenes' references between entities.

A scene file refers to another entity by its place in the file (0 for the first), so
adding or moving an entity above one shifts what the numbers mean. The editor keeps
them right; this catches a scene edited by hand: the camera in main.scene must follow
Basil.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
EXPECT = {('main.scene', 'follow'): 'Basil'}


def entities(text):
    """[(name, body)] for each entity in a scene file, in order."""
    out = []
    for block in text.split('\nentity\n')[1:]:
        m = re.search(r'  Name\n    text = "([^"]*)"', block)
        out.append((m.group(1) if m else '?', block))
    return out


def main():
    ok = True
    for f in sorted(os.listdir(ROOT)):
        if not f.endswith('.scene'):
            continue
        ents = entities(open(os.path.join(ROOT, f)).read())
        for name, body in ents:
            for field, n in re.findall(r'\n    (follow) = (-?\d+)', body):
                n = int(n)
                target = ents[n][0] if 0 <= n < len(ents) else None
                want = EXPECT.get((f, field))
                if target is None or (want and target != want):
                    print('%s: %s.%s = %d points at %s (should be %s)' % (f, name, field, n, target, want or 'an entity'))
                    ok = False
    print('scenes OK' if ok else 'PROBLEMS')
    return ok


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
