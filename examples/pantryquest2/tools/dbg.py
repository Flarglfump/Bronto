import sys, check_world as cw, world
rooms = {r.id: r for r in world.build()}
def reach(rid, ab, start):
    g = cw.RoomGraph(rooms[rid], set(ab))
    seen = {start}; todo = [start]; exits = set()
    while todo:
        n = todo.pop()
        for m in g.moves(n):
            if m[0] == 'node':
                k = (m[1], m[2], m[3])
                if k not in seen: seen.add(k); todo.append(k)
            else:
                exits.add((m[1], cw.door_index(rooms[rid], m[1], m[2], m[3])))
    return seen, exits
if __name__ == '__main__':
    rid = sys.argv[1]; ab = sys.argv[2].split(',') if sys.argv[2] != '-' else []
    c, r = int(sys.argv[3]), int(sys.argv[4])
    seen, exits = reach(rid, ab, (c, r, False))
    room = rooms[rid]
    grid = [list(row) for row in room.t]
    for (x, y, b) in seen: grid[y][x] = 'o' if not b else 'b'
    for y, row in enumerate(grid): print('%3d %s' % (y, ''.join(row)))
    print('exits', exits)
