"""Talks to build/botrunner (tools/botrunner/main.c): it runs the game headless and records
the run, and this side presses keys, lets frames happen and reads the game."""
import os
import subprocess

KEYS = {
    'right': 262, 'left': 263, 'down': 264, 'up': 265,
    'jump': 90, 'fire': 88, 'rocket': 67, 'aim': 81, 'ball': 69,    # Z X C Q E (the default keys)
    'enter': 257, 'escape': 256, 'map': 77, 'pause': 80,
}


class GameEnded(Exception):
    pass


class ScriptError(Exception):
    pass


class Runner:
    def __init__(self, exe, game, scene=None):
        args = [exe, game]
        if scene:
            args += ['--test-scene', scene]
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.frames = 0
        self.held = set()
        self.ended = False
        self._sched = []                 # fixed steps in each of the coming frames
        first = self.read()
        if not first or not first[0].startswith('ready'):
            raise RuntimeError('botrunner did not start: %r' % first)

    def read(self):
        out = []
        while True:
            line = self.p.stdout.readline()
            if line == '':
                raise RuntimeError('botrunner stopped')
            line = line.rstrip('\n')
            if line == '.':
                return out
            out.append(line)

    def ask(self, line):
        self.p.stdin.write(line + '\n')
        return self.read()

    # ---- keys and frames ----
    def key(self, name, down):
        down = bool(down)
        if (name in self.held) == down:
            return
        if down:
            self.held.add(name)
        else:
            self.held.discard(name)
        self.p.stdin.write('key %d %d\n' % (KEYS[name], 1 if down else 0))
        self.read()

    def hold(self, names):
        """Exactly these keys down (the rest up)."""
        names = set(names)
        for n in list(self.held):
            if n not in names:
                self.key(n, False)
        for n in names:
            self.key(n, True)

    def steps_ahead(self, n):
        """How many fixed steps (script updates) each of the next n frames runs: 0, 1 or 2."""
        while len(self._sched) < n:
            have = len(self._sched)
            got = self.ask('steps %d' % (have + 2000))[0]
            self._sched = [int(c) for c in got]
        return self._sched[:n]

    def frame(self, n=1):
        del self._sched[:n]
        out = self.ask('frame %d' % n)
        parts = out[0].split()
        self.frames = int(parts[1])
        if int(parts[2]):
            raise ScriptError('\n'.join(out[1:]))
        if 'ended' in parts:
            self.ended = True
            raise GameEnded()

    def prefix(self, path, upto):
        """Plays an earlier recording's keys up to a frame (recording them again)."""
        out = self.ask('prefix %s %d' % (os.path.abspath(path), upto))
        if out[0].startswith('error'):
            raise RuntimeError(out[0])
        parts = out[0].split()
        self.frames = int(parts[1])
        self._sched = []
        codes = {v: k for k, v in KEYS.items()}
        self.held = set(codes[int(c)] for c in out[1].split()[1:] if int(c) in codes)
        return self.frames

    def segment(self, name):
        if name == getattr(self, '_last_seg', None):      # (a step tried again: the same segment still)
            return
        self._last_seg = name
        self.ask('seg ' + name)

    # ---- reading ----
    def scenes(self):
        return [l.split(' ', 1)[1] for l in self.ask('scenes')]

    def find(self, name):
        return [tuple(int(x) for x in l.split()) for l in self.ask('find ' + name)]

    def tagged(self, tag):
        out = []
        for l in self.ask('tagged ' + tag):
            s, i, nm, x, y = l.split()
            out.append((int(s), int(i), nm, float(x), float(y)))
        return out

    def vars(self, s, e, names):
        return self.ask('var %d %d %s' % (s, e, ' '.join(names)))

    def pos(self, s, e):
        l = self.ask('pos %d %d' % (s, e))[0]
        if l == 'none':
            return None
        x, y = l.split()
        return float(x), float(y)

    def child(self, s, e, name):
        return int(self.ask('child %d %d %s' % (s, e, name))[0])

    def tilemap(self, s, e):
        out = self.ask('map %d %d' % (s, e))
        if not out or out[0] == 'none':
            return None
        cols, rows, ts, x0, y0, solid = out[0].split(' ', 5)
        return {'cols': int(cols), 'rows': int(rows), 'ts': int(ts), 'x0': float(x0), 'y0': float(y0),
                'solid': '' if solid == '-' else solid, 'rows_text': out[1:1 + int(rows)]}

    def screenshot(self, path):
        self.ask('draw')
        self.frames += 1
        self.ask('save ' + path)

    def finish(self, path):
        return self.ask('finish ' + os.path.abspath(path))

    def close(self):
        try:
            self.ask('quit')
        except Exception:
            pass
        self.p.wait(timeout=10)


def parse_value(text):
    """A script value as the runner prints it: number, bool, vec2 tuple, string, list."""
    t = text.strip()
    if t == '?':
        return None
    if t in ('true', 'false'):
        return t == 'true'
    if t.startswith('(') and t.endswith(')') and ',' in t:
        try:
            a, b = t[1:-1].split(',')
            return float(a), float(b)
        except ValueError:
            return t
    try:
        return float(t)
    except ValueError:
        return t


def parse_list(text):
    """A script list of strings / numbers as printed: ["a", "b"] or [1, [2, 3]]."""
    import ast
    t = text.replace('true', 'True').replace('false', 'False').replace('nil', 'None')
    try:
        return ast.literal_eval(t)
    except Exception:
        return []
