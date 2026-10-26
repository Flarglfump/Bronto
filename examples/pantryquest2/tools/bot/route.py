#!/usr/bin/env python3
"""Pantry Quest II, played from the title to the ending by the bot, and recorded.

    python3 examples/pantryquest2/tools/bot/route.py [--out run.brec] [--quiet] [--shots dir]
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import util                                   # noqa: E402
from runner import Runner, GameEnded          # noqa: E402
from bot import Bot, Died                     # noqa: E402
import worldnav as wn                         # noqa: E402
import nav                                    # noqa: E402
import boss                                   # noqa: E402


def title(bot):
    bot.r.segment('Title')
    bot.wait(70)
    bot.tick(('jump',))                       # press start
    bot.wait(40)
    bot.tick(('jump',))                       # START
    bot.wait(30)
    bot.tick(('jump',))                       # the first journey
    bot.wait_until(lambda: bot.r.scenes() == ['intro.scene'], 200)
    # the story so far: a page at a time, at reading pace
    while bot.r.scenes() == ['intro.scene']:
        bot.read_page('Intro', 30)
    bot.wait_until(lambda: bot.r.scenes() == ['main.scene'] and bot.state() == 'play', 600)


def get(bot, room, iid):
    t = [t for t in wn.things(room, 'item') if t.vars['iid'] == iid][0]
    if not bot.travel(room, wn.near_tile(t.x, t.y)):
        raise RuntimeError('could not get to %s in %s' % (iid, room))
    if not bot.take_item(iid):
        raise RuntimeError('could not take %s in %s' % (iid, room))
    bot.log('took', iid)


STEPS = []


def step(fn):
    STEPS.append(fn)
    return fn


def rest_at(bot, room):
    if not bot.travel(room):
        raise RuntimeError('could not get to the hearth in ' + room)
    bot.rest()


@step
def dough(bot):
    bot.r.segment('Crumb Village and the Rye Catacombs')
    rest_at(bot, 'V1')
    get(bot, 'V3', 'page1')
    get(bot, 'C2', 'dough')


@step
def to_stale(bot):
    if not bot.travel('C9'):
        raise RuntimeError('could not get to Sir Stale')


@step
def stale(bot):
    bot.r.segment("Sir Stale")
    # into the hall, to wake him
    bot.goto(lambda n: n[0] != 'exit' and n[0] >= 6 and n[1] == 12, nav.dist_heuristic(120, 192), desc='into the hall')
    if not boss.fight_stale(bot):
        raise RuntimeError('Sir Stale won')
    bot.take_item('seal_salt')
    bot.take_item('popcorn')


@step
def packet_c1(bot):
    bot.r.segment('Popcorn')
    get(bot, 'C1', 'packet_c1')


@step
def page2(bot):
    get(bot, 'C4', 'page2')


@step
def rye_hearth(bot):
    rest_at(bot, 'C5')


@step
def sauce(bot):
    bot.r.segment('Down to the Frostbite Cellar')
    get(bot, 'F2', 'sauce')


def read_map(bot, room):
    if not bot.travel(room):
        raise RuntimeError('could not get to the map table in ' + room)
    for (sc, i, nm, x, y) in bot.r.tagged('maptable'):
        bot.use_here(x, y + 4, 'the map table')
        bot.wait_play()


@step
def frost_map(bot):
    read_map(bot, 'F7')


@step
def frost_hearth(bot):
    rest_at(bot, 'F3')


@step
def packet_f4(bot):
    get(bot, 'F4', 'packet_f4')


@step
def frost(bot):
    get(bot, 'F6', 'frost')
    bot.set_frost(False)                      # (on only when he needs frozen steps: frost doesn't kill)


@step
def to_sundae(bot):
    if not bot.travel('F9'):
        raise RuntimeError('could not get to the Sundae')


@step
def sundae(bot):
    bot.r.segment('The Abominable Sundae')
    bot.goto(lambda n: n[0] != 'exit' and n[0] >= 8 and n[1] == 12, nav.dist_heuristic(140, 192), desc='into the den')
    if not boss.fight_sundae(bot):
        raise RuntimeError('the Sundae won')
    bot.take_item('seal_sugar')


@step
def molten_hearth(bot):
    bot.r.segment('The Molten Kitchens')
    rest_at(bot, 'M3')
    read_map(bot, 'M3')


@step
def leaven(bot):
    get(bot, 'M2', 'leaven')


@step
def high_places(bot):
    bot.r.segment('The Leaven Boots')
    get(bot, 'C8', 'cake_c8')
    get(bot, 'V2r', 'cake_v2r')


@step
def page3(bot):
    get(bot, 'F1', 'page3')


@step
def packet_c10(bot):
    rest_at(bot, 'C5')
    get(bot, 'C10', 'packet_c10')


@step
def brine_works(bot):
    bot.r.segment('The Brine Works')
    rest_at(bot, 'B2')
    read_map(bot, 'B2')
    get(bot, 'B1', 'page6')
    get(bot, 'B1', 'packet_b1')


@step
def mitts(bot):
    get(bot, 'B4', 'mitts')


@step
def garlic(bot):
    get(bot, 'B6', 'page5')
    get(bot, 'B6', 'garlic')


@step
def to_brine(bot):
    rest_at(bot, 'B2')
    if not bot.travel('B8'):
        raise RuntimeError('could not get to Doctor Brine')


@step
def brine(bot):
    bot.r.segment('Doctor Brine')
    bot.goto(lambda n: n[0] != 'exit' and n[0] <= 22 and n[1] == 12, nav.dist_heuristic(300, 192), desc='into the lab')
    if not boss.fight_brine(bot):
        raise RuntimeError('Doctor Brine won')
    bot.take_item('seal_vinegar')


@step
def whisk(bot):
    bot.r.segment('The hot kitchens')
    get(bot, 'M6', 'whisk')


@step
def hot_kitchens(bot):
    get(bot, 'M5', 'cake_m5')
    get(bot, 'M1', 'page4')


@step
def to_wyrm(bot):
    rest_at(bot, 'M3')
    if not bot.travel('M7'):
        raise RuntimeError('could not get to Cheese Falls')
    hit_switch(bot)
    if not bot.travel('M8'):
        raise RuntimeError('could not get to the Wyrm')


def hit_switch(bot):
    """Cheese Falls: the grate over the west door opens from a switch behind a wall; garlic
    pepper goes through walls."""
    for _ in range(3):
        if any(f.startswith('switch:') for f in bot.flags()):
            return
        sw = [e for e in bot.r.find('Switch')]
        if not sw:
            return
        sx, sy = bot.r.pos(*sw[0])
        bot.set_frost(False)
        bot.shoot_point(sx, sy, through_walls=True, done=lambda: any(f.startswith('switch:') for f in bot.flags()))


@step
def wyrm(bot):
    bot.r.segment('The Flambe Wyrm')
    if not boss.fight_wyrm(bot):
        raise RuntimeError('the Wyrm won')
    bot.take_item('seal_pepper')


@step
def castle(bot):
    bot.r.segment('Castle Fromage')
    rest_at(bot, 'V1')
    if not bot.travel('X0'):
        raise RuntimeError('could not get into the castle')
    rest_at(bot, 'X0')
    get(bot, 'X2', 'page7')
    read_map(bot, 'X3')
    get(bot, 'X3', 'page8')
    get(bot, 'X3', 'packet_x3')
    get(bot, 'X4', 'cake_x4')


@step
def to_count(bot):
    rest_at(bot, 'X4')
    if not bot.travel('X7'):
        raise RuntimeError('could not get to the throne room')


@step
def count(bot):
    bot.r.segment('Count Fromage')
    bot.goto(lambda n: n[0] != 'exit' and 6 <= n[0] <= 26 and n[1] == 12, nav.dist_heuristic(256, 192), desc='into the throne room')
    if not boss.fight_count(bot):
        raise RuntimeError('the Count won')


@step
def pip(bot):
    bot.r.segment('The escape')
    if not bot.travel('X8'):
        raise RuntimeError('could not get to Pip')
    for (sc, i, nm, x, y) in bot.r.tagged('npc'):
        bot.touch_point(x, y, (30, 30), desc='Pip')
    bot.wait_play()


@step
def escape(bot):
    bot.travel('V4')
    bot.wait_until(lambda: bot.r.scenes()[:1] == ['ending.scene'], 600)


@step
def ending(bot):
    bot.r.segment('The ending')
    bot.watch_death = False
    for _ in range(40):
        if bot.r.scenes()[:1] == ['title.scene']:
            break
        bot.wait(240)
        bot.tick(('jump',))
        bot.tick(())
    bot.wait(120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'playthrough.brec'))
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--shots')
    ap.add_argument('--resume', help='an earlier run (.brec, with its .steps.json): play it up to --at')
    ap.add_argument('--at', help='the step to carry on from (default: the one it stopped in)')
    ap.add_argument('--stop-after', help='stop after this step')
    a = ap.parse_args()
    r = Runner(util.RUNNER, util.PROJECT)
    bot = Bot(r, verbose=not a.quiet, shots=a.shots)
    steps_log = []
    names = [fn.__name__ for fn in STEPS]
    first = 0
    try:
        if a.resume:
            with open(a.resume + '.steps.json') as f:
                old = json.load(f)
            at = a.at or old[-1][0]
            starts = dict((n, fr) for n, fr in old)
            if at in starts:
                frame = starts[at]
            else:                                     # (a step after the last one: from the end of the run)
                with open(a.resume) as f:
                    frame = [int(l.split()[1]) for l in f if l.startswith('end ')][0]
            steps_log = [x for x in old if x[1] < frame]
            r.prefix(a.resume, frame)
            first = names.index(at)
            bot.log('resumed at', at)
        else:
            title(bot)
        for fn in STEPS[first:]:
            steps_log.append((fn.__name__, r.frames))
            with open(a.out + '.steps.json', 'w') as f:
                json.dump(steps_log, f)
            for tries in range(4):
                bot.log('==', fn.__name__, '(again)' if tries else '')
                try:
                    fn(bot)
                    break
                except Died:
                    continue
            if a.stop_after == fn.__name__:
                break
    except GameEnded:
        pass
    finally:
        out = r.finish(a.out)
        print('recorded', out)
        r.close()


if __name__ == '__main__':
    main()
