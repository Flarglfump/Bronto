#!/usr/bin/env python3
"""Checks sim.py against the game: random keys in a room, and every step the sim's guess
(from the game's own state the step before) must match what the game did.

    python3 testsim.py ROOM X Y [items,...] [frames] [seed]
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from runner import Runner                     # noqa: E402
from game import Game                         # noqa: E402
import sim                                    # noqa: E402
import util                                   # noqa: E402


def main():
    room, x, y = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    items = [i for i in (sys.argv[4].split(',') if len(sys.argv) > 4 else []) if i]
    frames = int(sys.argv[5]) if len(sys.argv) > 5 else 2000
    rng = random.Random(int(sys.argv[6]) if len(sys.argv) > 6 else 1)
    scene = util.test_scene(start_room=room, start_pos=(x, y), start_items=items)
    r = Runner(util.RUNNER, util.PROJECT, scene)
    g = Game(r)
    try:
        while g.state() != 'play':
            r.frame(1)
        has = lambda it: it in items
        held = set()
        bad = 0
        hold_for = 0
        pending = []
        hurts = 0
        for f in range(frames):
            if hold_for <= 0:
                held = set()
                for k, p in (('left', .3), ('right', .35), ('up', .08), ('down', .1), ('jump', .3), ('aim', .05)):
                    if rng.random() < p:
                        held.add(k)
                if rng.random() < 0.05:
                    held.add('ball')
                hold_for = rng.randint(1, 25)
            hold_for -= 1
            before = set(r.held)
            b = g.basil()
            grid = g.grid()
            r.hold(held)
            pressed = [k for k in ('up', 'down', 'jump', 'ball') if k in held and k not in before]
            k = r.steps_ahead(1)[0]
            pressed = pressed + [p for p in pending if p in held and p not in pressed]
            pending = pressed if k == 0 else []
            guess = sim.frame(b.copy(), held, pressed, k, grid, has)
            if g.state() != 'play':
                r.frame(1)
                continue
            r.frame(1)
            if g.state() != 'play':
                continue
            now = g.basil()
            if now.safe_t > guess.safe_t + 0.5:
                hurts += 1
                continue
            if (guess.x, guess.y, guess.vx, guess.vy, guess.sx, guess.sy, guess.grounded, guess.ball, guess.crouch) != \
               (now.x, now.y, now.vx, now.vy, now.sx, now.sy, now.grounded, now.ball, now.crouch):
                bad += 1
                if bad < 10:
                    print('frame', r.frames, 'held', sorted(held), 'pressed', pressed)
                    print('  was  ', b)
                    print('  guess', guess)
                    print('  game ', now)
        print('%d frames, %d differed (%d hurt by enemies, not counted)' % (frames, bad, hurts))
    finally:
        r.close()
        util.remove_test_scene(scene)


if __name__ == '__main__':
    main()
