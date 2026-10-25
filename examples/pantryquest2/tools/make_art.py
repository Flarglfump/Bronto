#!/usr/bin/env python3
"""Draws Pantry Quest II's art (all original), as 32-bit BMPs in art/.

    python3 examples/pantryquest2/tools/make_art.py [--preview folder]

--preview also writes 4x PNGs of everything, for looking at.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pixels import write_bmp, write_png          # noqa: E402
import art_basil                                  # noqa: E402
import art_tiles                                  # noqa: E402
import art_things                                 # noqa: E402
import art_enemies                                # noqa: E402
import art_people                                 # noqa: E402
import art_bosses                                 # noqa: E402
import art_title                                  # noqa: E402

ART = os.path.join(HERE, '..', 'art')
OUT = {}


def out(name, canvas):
    OUT[name] = canvas
    write_bmp(os.path.join(ART, name + '.bmp'), canvas)


def main():
    out('basil', art_basil.basil_sheet())
    out('basil_mitts', art_basil.basil_sheet(art_basil.OVEN_MITTS))
    for style in art_tiles.STYLES:
        out('tiles_' + style, art_tiles.tileset(style))
    for f in range(2):
        out('tiles_molten_%d' % f, art_tiles.lava_frame('molten', f))
    out('door', art_things.door_sheet())
    out('shots', art_things.shots_sheet())
    out('rocket', art_things.rocket())
    out('popcorn', art_things.popcorn_sheet())
    for k in ('puff', 'burst', 'crumbs', 'blast', 'pop'):
        out('fx_' + k, art_things.fx_sheet(k))
    out('items', art_things.items_sheet())
    out('drops', art_things.drops_sheet())
    out('hearth', art_things.hearth_sheet())
    out('maptable', art_things.maptable_sheet())
    out('hud_icons', art_things.hud_icons())
    out('spit', art_things.spit_sheet())
    out('sign', art_things.signpost())
    out('portraits', art_people.portraits())
    out('mozz', art_people.person(art_people.MOZZ))
    out('pip', art_people.person(art_people.PIP))
    out('boss_stale', art_bosses.stale_sheet())
    out('boss_sundae', art_bosses.sundae_sheet())
    out('boss_frank', art_bosses.frank_sheet())
    out('boss_doctor', art_bosses.doctor_sheet())
    out('boss_wyrm', art_bosses.wyrm_sheet())
    out('boss_count', art_bosses.count_sheet())
    out('boss_bigcheese', art_bosses.bigcheese_sheet())
    out('spore', art_bosses.spore_sheet())
    out('bolt', art_bosses.bolt_sheet())
    out('switch', art_bosses.switch_sheet())
    out('title_night', art_title.night())
    out('title_empty', art_title.night(False))
    out('title_dawn', art_title.dawn())
    out('logo', art_title.logo())
    for name in art_enemies.E:
        out(name, art_enemies.enemy_sheet(name))
    if '--preview' in sys.argv:
        folder = sys.argv[sys.argv.index('--preview') + 1]
        for name, c in OUT.items():
            write_png(os.path.join(folder, name + '.png'), c.scaled(3))
    print('wrote %d images to art/' % len(OUT))


if __name__ == '__main__':
    main()
