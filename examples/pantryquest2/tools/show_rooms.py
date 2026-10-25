#!/usr/bin/env python3
"""Prints rooms from world.py as text (for checking layouts): show_rooms.py V1 C0 ..."""
import sys, world
rooms = {r.id: r for r in world.build()}
for rid in sys.argv[1:] or rooms:
    r = rooms[rid]
    grid = [list(row) for row in r.t]
    for th in r.things:
        ch = {'item': 'I', 'hearth': 'H', 'maptable': 'M', 'npc': 'N', 'sign': 'S', 'boss': 'B', 'switch': 'W'}.get(th.kind, 'e')
        if 0 <= th.y < r.H and 0 <= th.x < r.W:
            grid[th.y][th.x] = ch
    for (x, y) in r.freeze_spots:
        grid[y][x] = 'f'
    print('%s %s (%d,%d %dx%d) doors %s' % (r.id, r.name, r.x, r.y, r.w, r.h, r.doors))
    print('    ' + ''.join(str(i % 10) for i in range(r.W)))
    for y, row in enumerate(grid):
        print('%3d %s' % (y, ''.join(row)))
