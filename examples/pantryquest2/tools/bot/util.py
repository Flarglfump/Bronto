"""Paths, and test scenes: main.scene with the Game told where to start."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, '..', '..'))
ROOT = os.path.normpath(os.path.join(PROJECT, '..', '..'))
RUNNER = os.path.join(ROOT, 'build', 'botrunner')


def test_scene(start_room='V0', start_pos=None, start_door='', start_items=(), name='zz_bot_test.scene'):
    """Writes a copy of main.scene whose Game starts in a given room with given items."""
    with open(os.path.join(PROJECT, 'main.scene')) as f:
        main = f.read()
    parts = ['start_room = \\"%s\\"' % start_room]
    if start_pos:
        parts.append('start_pos = vec2(%d, %d)' % start_pos)
    if start_door:
        parts.append('start_door = \\"%s\\"' % start_door)
    if start_items:
        parts.append('start_items = [%s]' % ', '.join('\\"%s\\"' % i for i in start_items))
    line = '    vars = "%s"\n' % ', '.join(parts)
    main = main.replace('    file = "scripts/game.bs"\n', '    file = "scripts/game.bs"\n' + line, 1)
    with open(os.path.join(PROJECT, name), 'w') as f:
        f.write(main)
    return name


def remove_test_scene(name):
    p = os.path.join(PROJECT, name)
    if os.path.exists(p):
        os.remove(p)
