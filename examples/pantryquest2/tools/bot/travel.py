"""Going places: through doors (shooting their caps open), from room to room (worldnav.py),
and to things in a room (items, hearths, map tables)."""
import nav
import worldnav as wn

WN = wn.WorldNav()


class Travel:
    """Mixed into Bot."""

    # ---- what he has, as the world planner counts it ----
    def abilities(self):
        ab = set(self.inv()['items'])
        fl = self.flags()
        for boss in ('stale', 'sundae', 'brine', 'wyrm', 'count'):
            if 'boss:' + boss in fl:
                ab.add('beaten_' + boss)
        return ab

    # ---- doors ----
    def doors(self):
        out = []
        for (s, i, nm, x, y) in self.r.tagged('door'):
            v = self.r.vars(s, i, ['side', 'index', 'kind', 'opened', 'sealed'])
            out.append({'id': (s, i), 'x': x, 'y': y, 'side': v[0], 'index': int(float(v[1])), 'kind': v[2],
                        'opened': v[3] in ('1', 'true'), 'sealed': v[4] in ('1', 'true')})
        return out

    def door(self, side, index):
        for d in self.doors():
            if d['side'] == side and d['index'] == index:
                return d
        return None

    def open_door(self, side, index):
        """Walks up to a side door and shoots its cap open (hot sauce for wax)."""
        for attempt in range(6):
            d = self.door(side, index)
            if d is None or d['opened'] or side in 'NS':
                return True
            if d['sealed']:
                self.log('door', side, index, 'is sealed')
                return False
            g = self.grid()
            row = index * 15 + 12
            col = 2 if side == 'W' else g.cols - 3
            spot = (col, row, False)
            if not self.goto(lambda n, spot=spot: n[:2] == spot[:2] and not n[2], nav.dist_heuristic(col * 16 + 8, row * 16), desc='door %s%d' % (side, index)):
                if not self.goto(lambda n, spot=spot: n[0] != 'exit' and abs(n[0] - spot[0]) <= 2 and n[1] == spot[1], nav.dist_heuristic(col * 16 + 8, row * 16), desc='near door %s%d' % (side, index)):
                    continue
            b = self.basil()
            if b.ball:
                self.tick(('up',))
                self.tick(())
            facing = -1 if side == 'W' else 1
            key = 'left' if side == 'W' else 'right'
            fire = 'rocket' if d['kind'] == 'wax' else 'fire'
            for _ in range(90):
                if self.door(side, index)['opened']:
                    self.tick(())
                    return True
                b = self.basil()
                if b.shot_cd <= 0 and fire not in self.r.held:
                    self.tick(('aim', key, fire))
                else:
                    self.tick(('aim', key))
            self.tick(())
        return False

    def leave_by(self, side, index):
        """Out of this room through a door (shaft or side door)."""
        here = self.room()
        if side in 'WE' and not self.open_door(side, index):
            return False
        goal = ('exit', side, index)
        for attempt in range(4):
            if self.room() != here:
                return True
            g = self.grid()
            ex = {'W': (0, index * 240 + 180), 'E': (g.W, index * 240 + 180), 'N': (index * 256 + 128, 0),
                  'S': (index * 256 + 128, g.H)}[side]
            ok = self.goto(lambda n: n == goal, nav.dist_heuristic(*ex), desc='exit %s%d' % (side, index))
            self.wait_play()
            if self.room() != here:
                return True
            if side in 'WE' and not self.door(side, index)['opened']:
                self.open_door(side, index)
        return self.room() != here

    # ---- the world ----
    def travel(self, target_room, offline_goal=None):
        """To a room (and, with offline_goal(spots), to a part of it), room by room."""
        tried_hearth = False
        for attempt in range(60):
            self.wait_play()
            room = self.room()
            b = self.settle()
            node = nav.node_of(b)
            def goal(rid, spots):
                return rid == target_room and (offline_goal is None or offline_goal(rid, spots))
            plan = WN.route(room, node, goal, self.abilities(), self.flags())
            if plan is None:                          # (his exact spot may not be one of the planner's: try those by it)
                for dc, dr in ((0, 1), (0, -1), (-1, 0), (1, 0), (-1, 1), (1, 1), (0, 2), (-1, -1), (1, -1)):
                    near = (node[0] + dc, node[1] + dr, node[2])
                    if WN.graph(room, self.abilities()).stands(*near):
                        plan = WN.route(room, near, goal, self.abilities(), self.flags())
                        if plan is not None:
                            break
            if plan is None:
                self.log('no route from', room, node, 'to', target_room)
                if not tried_hearth and self.return_to_hearth():
                    tried_hearth = True
                    continue
                return False
            if not plan:
                return True
            r, side, idx = plan[0]
            self.log('travel', room, '->', target_room, ':', ' '.join('%s%s%d' % p for p in plan))
            self.leave_by(side, idx)
        return False

    # ---- things in a room ----
    def thing_entities(self, tag=None, name=None):
        out = []
        if tag:
            for (s, i, nm, x, y) in self.r.tagged(tag):
                out.append((s, i, nm, x, y))
        return out

    def touch_point(self, px, py, half=(7, 7), desc='a thing'):
        """Goes to stand (or roll) where his box touches a point's box."""
        def ok(n):
            c, r, ball = n if n[0] != 'exit' else (None, None, None)
            if c is None:
                return False
            x = c * 16 + 8
            h = 12 if ball else 26
            my = (r + 1) * 16 - h / 2
            return abs(px - x) < half[0] + 6 - 3 and abs(py - my) < half[1] + h / 2 - 2
        if not self.goto(ok, nav.dist_heuristic(px, py), desc=desc):
            return False
        # step into it exactly
        for _ in range(60):
            b = self.basil()
            mx, my = b.middle()
            w, h = (12, 12) if b.ball else (12, 26)
            if abs(px - mx) < half[0] + w / 2 - 1 and abs(py - my) < half[1] + h / 2 - 1:
                return True
            if self.state() != 'play':
                return True
            d = 1 if px > b.x else -1
            self.tick(nav.held_of(d))
        return False

    def take_item(self, iid):
        """Picks up an item in this room by its id."""
        for (s, i, nm, x, y) in self.r.tagged('item'):
            v = self.r.vars(s, i, ['iid', 'home'])
            if v[0] == iid:
                hx, hy = parse_vec(v[1])
                self.log('item', iid, 'at', hx, hy)
                self.touch_point(hx, hy, (7, 9), desc='item ' + iid)
                self.wait_play()
                return ('item:' + iid) in self.flags()
        return ('item:' + iid) in self.flags()

    def use_here(self, px, py, desc):
        """Stands at something (a hearth, a map table, someone) and presses UP."""
        if not self.touch_point(px, py, (6, 6), desc=desc):
            return False
        self.settle()
        self.tick(())
        self.tick(('up',))
        self.tick(())
        return True


def parse_vec(t):
    t = t.strip()
    if t.startswith('('):
        a, b = t[1:-1].split(',')
        return float(a), float(b)
    return 0.0, 0.0
