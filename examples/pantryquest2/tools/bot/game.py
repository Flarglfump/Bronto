"""Reading Pantry Quest II through the runner: Basil, the Game's state, the room."""
from runner import parse_value, parse_list
import sim

BASIL_VARS = ['vel', 'sub', 'facing', 'grounded', 'ball', 'crouch', 'spin', 'hurt_t', 'safe_t', 'lifted',
              'in_liquid', 'lava_t', 'shot_cd']


class Game:
    def __init__(self, runner):
        self.r = runner
        self._ids = {}
        self._scene_list = None
        self._grid = None
        self._grid_key = None

    # ---- entities in the main scene ----
    def scenes(self):
        return self.r.scenes()

    def main_index(self):
        """The scene index of main.scene (it's at the bottom while playing), or -1."""
        sc = self.r.scenes()
        for i, s in enumerate(sc):
            if s == 'main.scene' or s.startswith('zz_'):
                return i
        return -1

    def ent(self, name):
        k = name
        e = self._ids.get(k)
        if e is not None:
            if self.r.pos(*e) is not None:
                return e
        found = self.r.find(name)
        if not found:
            self._ids.pop(k, None)
            return None
        e = found[0]
        self._ids[k] = e
        return e

    def var(self, name, var):
        e = self.ent(name)
        if not e:
            return None
        return parse_value(self.r.vars(e[0], e[1], [var])[0])

    def vars(self, name, names):
        e = self.ent(name)
        if not e:
            return None
        return dict(zip(names, (parse_value(v) for v in self.r.vars(e[0], e[1], names))))

    def raw(self, name, var):
        e = self.ent(name)
        if not e:
            return ''
        return self.r.vars(e[0], e[1], [var])[0]

    def glist(self, var):
        return parse_list(self.raw('Game', var))

    # ---- the Game ----
    def state(self):
        return self.var('Game', 'state')

    def room(self):
        return self.var('Game', 'room_id')

    def flags(self):
        return self.strings('flags')

    def strings(self, var):
        """A Game list of strings (printed without quotes: [a, b, c])."""
        t = self.raw('Game', var).strip()
        if not t.startswith('[') or t == '[]':
            return []
        return [x.strip() for x in t[1:-1].split(',') if x.strip()]

    def inv(self):
        """[items, energy, tanks, rockets, max_rockets, pages, frost_on]"""
        e = self.ent('Game')
        if not e:
            return None
        return None

    def items(self):
        txt = self.raw('Game', 'inv')
        return txt

    # ---- Basil ----
    def basil(self):
        e = self.ent('Basil')
        if not e:
            return None
        p = self.r.pos(*e)
        vals = self.r.vars(e[0], e[1], BASIL_VARS)
        v = dict(zip(BASIL_VARS, (parse_value(x) for x in vals)))
        b = sim.Basil()
        b.x, b.y = p
        b.vx, b.vy = v['vel']
        b.sx, b.sy = v['sub']
        b.facing = int(v['facing'])
        for k in ('grounded', 'ball', 'crouch', 'spin', 'lifted', 'in_liquid'):
            setattr(b, k, bool(v[k]))
        for k in ('hurt_t', 'safe_t', 'lava_t', 'shot_cd'):
            setattr(b, k, v[k])
        return b

    def grid(self, fresh=False):
        """The room as it is now (tiles change: doors, broken blocks, wax)."""
        game = self.ent('Game')
        if not game:
            return None
        room = self.var('Game', 'room_id')
        bas = self.ent('Basil')
        s = bas[0]
        mp = self.r.vars(bas[0], bas[1], ['map'])[0]
        fr = self.r.vars(bas[0], bas[1], ['front'])[0]
        mid = self.ent_id(mp)
        fid = self.ent_id(fr)
        tm = self.r.tilemap(s, mid)
        front = self.r.tilemap(s, fid) if fid else None
        plats = [(x, y) for (sc, i, nm, x, y) in self.r.tagged('platform')]
        g = sim.Grid(tm, front, plats)
        g.room = room
        return g

    @staticmethod
    def ent_id(text):
        """An entity variable as printed ("entity 12" or similar) -> its id."""
        import re
        m = re.search(r'(\d+)', text or '')
        return int(m.group(1)) if m else 0
