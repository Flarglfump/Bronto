"""The world of Pantry Quest II: every room, where it sits on the grid, and what is in it.

Each room is a rectangle of screens (x, y = its top-left cell; w, h in screens) drawn with
the commands in roomkit.Room, in tiles: (0, 0) is a room's top-left tile, a screen is
16 x 15 tiles, and a one-screen room's floor is row 13. Doors join rooms that touch: a W
door on a room's left wall leads into the room on its left, which needs an E door there.

The way through, in brief (check_world.py plays it to be sure):
  Crumb Village -> the well -> Rye Catacombs: the Dough Wrap (C2), Sir Stale (C9: the Seal
  of Salt and Popcorn) -> a popcorn floor (C7) -> Frostbite Cellar: Hot Sauce (F2), Frost
  Sugar behind wax (F6), the Abominable Sundae (F9: the Seal of Sugar) -> the wax door in
  the Sunken Chapel (C11) -> Molten Kitchens: frozen fire beans over the lava (M1) to the
  Leaven Boots (M2) -> the high climb in the East Stair (C10) -> Brine Works: the Oven Mitts
  (B4), the Garlic Wave (B6), Doctor Brine (B8: the Seal of Vinegar) -> down into the hot
  kitchens: the Whisk Spin (M6), the garlic switch (M7), the Flambe Wyrm (M8: the Seal of
  Pepper) -> the castle gate (V4) -> up the clock tower (X6) -> Count Fromage (X7).
"""
from roomkit import Room

START = ('V0', 4, 12, False)            # room, column, row (Basil's feet on row 13), rolled up

# What each guardian needs to be beaten, and what beating it gives.
BOSS_NEEDS = {'stale': set(), 'sundae': set(), 'brine': set(), 'wyrm': {'mitts'}, 'count': {'garlic'}}
BOSS_REWARDS = {'stale': ['seal_salt', 'popcorn'], 'sundae': ['seal_sugar'], 'brine': ['seal_vinegar'],
                'wyrm': ['seal_pepper'], 'count': ['count']}


def zigzag(r, rows, left=(1, 6), right=(9, 14), first='left', xoff=0):
    """Ledges up a shaft, alternating sides, one on each listed row."""
    side = first
    for row in rows:
        a, b = left if side == 'left' else right
        r.plat(xoff + a, row, b - a + 1)
        side = 'right' if side == 'left' else 'left'


def tunnel(r, x0, x1, row):
    """A tunnel one tile tall (only a rolled-up Basil fits): solid above and below."""
    r.solid(x0, row - 1, x1 - x0 + 1, 1)
    r.clear(x0, row, x1 - x0 + 1, 1)
    r.solid(x0, row + 1, x1 - x0 + 1, 1)


def outdoor(r):
    """No stone ceiling outside: an invisible one at the very top (row 0) instead."""
    for x in range(1, r.W - 1):
        r.t[0][x] = 'Y'
        r.t[1][x] = '.'
    for y in range(0, 10):                    # and no stone walls rising into the sky at the ends
        for x in (0, r.W - 1):
            r.t[y][x] = 'Y'


def build():
    rooms = []

    def R(*a, **k):
        r = Room(*a, **k)
        r.garlic_gates = set()
        rooms.append(r)
        return r

    # ================================================================== CRUMB VILLAGE (surface)
    r = R('V0', 'village', 2, 3, 2, 1, name='Outskirts')
    r.notes.append('moon')
    outdoor(r)
    r.door('E', 0)
    r.solid(12, 11, 2, 2)                     # a crate stack: a first jump
    r.solid(19, 10, 3, 3)                     # a wall to jump over
    r.plat(25, 9, 3)
    r.add('sign', 7, 12, text="{jump}: JUMP. {fire}: PEPPER.|{up}: AIM UP. {up} WHILE RUNNING AIMS AT AN ANGLE, AND SO DOES HOLDING {aim}.")
    r.add('sign', 23, 12, text="CRUMB VILLAGE, THIS WAY.|THE CASTLE ON THE HILL WAS NOT HERE YESTERDAY.")
    r.enemy('gnat', 27, 6)

    r = R('V1', 'village', 4, 3, 2, 1, name='Bakery Ruins')
    outdoor(r)
    r.door('W', 0).door('E', 0)
    r.solid(5, 11, 2, 2)                      # the bakery's broken walls
    r.solid(25, 11, 2, 2)
    r.plat(8, 8, 6).plat(17, 8, 7)            # its upper floor, fallen in the middle
    r.add('npc', 13, 12, who='mozz', say='mozz_meet', again='mozz_idle', until='seals4')
    r.add('hearth', 20, 12)

    r = R('V2', 'village', 6, 3, 3, 1, name='Market Square')
    outdoor(r)
    r.door('W', 0).door('E', 0).door('S', 1).door('N', 2)
    r.solid(22, 12, 1, 1).solid(25, 12, 1, 1)  # the well's stone rim (its shaft is the floor's opening)
    r.plat(5, 10, 4).plat(11, 7, 3)            # market stalls
    r.plat(29, 10, 4)
    r.plat(35, 9, 5)                           # a rooftop ...
    r.solid(41, 4, 3, 1)                       # ... and a chimney ledge, high above it (the Leaven Boots)
    r.enemy('gnat', 15, 5).enemy('gnat', 33, 6)
    r.add('sign', 21, 12, text="THE OLD WELL. IT GOES DOWN A LONG WAY.")

    r = R('V2r', 'village', 8, 2, 1, 1, name='Rooftops')
    r.notes.append('moon')
    outdoor(r)
    r.door('S', 0)
    r.solid(2, 10, 3, 3)                       # chimneys
    r.solid(9, 10, 2, 3)
    r.solid(12, 7, 3, 6)
    r.item('cake', 13, 6, 'cake_v2r')

    r = R('V3', 'village', 9, 3, 3, 1, name='Chapel Road')
    outdoor(r)
    r.door('W', 0).door('E', 0)
    r.solid(8, 12, 4, 1).solid(12, 11, 24, 2)  # steps up to the chapel's plaza
    r.solid(36, 12, 4, 1)
    r.plat(16, 6, 12)                          # the chapel's roof beam (the chapel is behind)
    r.plat(40, 9, 4)
    for x in range(16, 28):
        for y in range(7, 11):
            r.bg('|' if x % 4 == 0 else '#', x, y)
    r.bg('*', 19, 8).bg('*', 24, 8).bg('!', 21, 10).bg('!', 23, 10)
    r.item('page', 22, 10, 'page1')
    r.enemy('batter', 18, 7).enemy('batter', 26, 7)
    r.enemy('gnat', 42, 5)

    r = R('V4', 'village', 12, 3, 2, 1, name='Castle Gate')
    r.notes.append('moon')
    outdoor(r)
    r.door('W', 0).door('E', 0, 'gate')
    r.solid(26, 3, 5, 7)                        # the gatehouse, the gate at the end of its arch
    for y in range(3, 10):
        r.bg('|', 25, y)
    r.add('npc', 22, 12, who='mozz', say='mozz_gate', needs='seals4')
    r.add('sign', 8, 12, text="CASTLE FROMAGE. ITS GATE HAS FOUR HOLLOWS:|SALT, SUGAR, VINEGAR, PEPPER.")

    r = R('V5', 'village', 14, 3, 4, 1, name='The Bridge')
    r.notes.append('moon')
    outdoor(r)
    r.door('W', 0).door('E', 0)
    r.fill('h', 3, 13, 58, 1)                   # the bridge's planks
    r.fill('Y', 3, 14, 58, 1)
    for x in range(8, 58, 10):
        r.solid(x, 11, 1, 2)                    # its posts
    r.enemy('batter', 20, 3).enemy('batter', 40, 3)
    r.enemy('ghost', 50, 7)

    # ================================================================== RYE CATACOMBS
    r = R('C0', 'catacombs', 7, 4, 1, 3, name='Well Shaft')
    r.door('N', 0).door('W', 1).door('E', 2)
    r.plat(6, 4, 4)                              # under the well: where Basil lands
    zigzag(r, range(7, 41, 3), left=(1, 6), right=(9, 14), first='left')
    r.enemy('crawler', 3, 12).enemy('crawler', 12, 21).enemy('batter', 8, 30)

    r = R('C1', 'catacombs', 4, 5, 3, 1, name='Ossuary')
    r.door('W', 0).door('E', 0).door('S', 1)
    r.solid(19, 10, 10, 2)                       # a bone pile, with a crawlspace under it ...
    r.clear(19, 12, 7, 1)                        # ... down to the floor's opening (a shortcut)
    r.solid(26, 12, 3, 1)
    r.plat(8, 10, 5).plat(14, 7, 3).plat(32, 9, 4).plat(39, 6, 3)
    r.solid(40, 10, 5, 3)                        # a tomb with a cracked side: a packet inside
    r.clear(41, 12, 3, 1)
    r.block('x', 40, 12, 1, 1)
    r.item('packet', 42, 12, 'packet_c1')
    r.enemy('crawler', 10, 9).enemy('batter', 30, 3).enemy('crawler', 36, 12)

    r = R('C2', 'catacombs', 3, 5, 1, 1, name='Dough Alcove')
    r.door('E', 0)
    r.solid(6, 11, 4, 2)                         # the altar
    r.item('dough', 8, 10, 'dough')
    r.bg('!', 5, 9).bg('!', 10, 9)

    r = R('C3', 'catacombs', 8, 6, 1, 1, name='Map Room')
    r.door('W', 0).door('E', 0).door('S', 0)
    r.solid(10, 2, 3, 10)                        # the east wall, with a crawlspace under it
    r.clear(10, 12, 3, 1)
    r.add('maptable', 3, 12, area='catacombs')
    r.enemy('crawler', 14, 4)

    r = R('C8', 'catacombs', 9, 6, 4, 1, name='Bone Corridor')
    r.door('W', 0).door('E', 0)
    r.block('^', 10, 12, 2, 1)                   # spike pits (two tiles: a running jump clears them)
    r.block('^', 26, 12, 4, 1)                   # (this one has a stepping stone over it)
    r.block('^', 44, 12, 2, 1)
    r.plat(27, 10, 3)
    r.plat(17, 8, 3).plat(37, 8, 3).plat(52, 8, 3)
    r.solid(30, 4, 4, 1)                         # a high shelf: a cake slice (the Leaven Boots)
    r.item('cake', 31, 3, 'cake_c8')
    r.enemy('batter', 12, 3).enemy('batter', 46, 3)
    r.enemy('walker', 21, 12, shoots='bone', hp=4, damage=10)
    r.enemy('crawler', 38, 7).enemy('walker', 57, 12, shoots='bone', hp=4, damage=10)

    r = R('C9', 'catacombs', 13, 6, 2, 1, name="Sir Stale's Hall", boss='stale', music='music/boss.wav')
    r.door('W', 0, 'boss').door('E', 0)
    r.solid(1, 13, 30, 1)
    r.add('boss', 22, 12, name='stale')
    r.bg(']', 8, 6).bg(']', 23, 6)

    r = R('C10', 'catacombs', 15, 5, 1, 3, name='East Stair')
    r.door('W', 1).door('N', 0)
    r.solid(1, 28, 14, 1)
    r.clear(7, 28, 2, 1)                          # a gap down to the lower stair
    zigzag(r, (25, 22, 19), first='right')
    r.plat(10, 14, 5)                             # five tiles up: only the Leaven Boots reach it
    r.plat(1, 10, 6)                              # (above that, the steps are made for them)
    r.plat(7, 5, 4)                               # under the opening up into the Brine Works
    r.plat(1, 40, 4).plat(6, 37, 4).plat(11, 34, 4).plat(6, 31, 4)   # the lower stair: three tiles a step, back up to the gap
    r.item('packet', 7, 42, 'packet_c10')
    r.enemy('gnat', 8, 17).enemy('crawler', 3, 35)

    r = R('C4', 'catacombs', 4, 6, 3, 2, name='Crumbling Crypt')
    r.door('N', 1).door('E', 1).door('W', 1, 'open')  # (no cap: at the end of a crawlspace, a shot can't reach it)
    r.plat(20, 4, 8)                             # under the opening from the Ossuary
    r.plat(10, 8, 6).plat(30, 8, 6)
    r.solid(2, 12, 8, 1).solid(38, 12, 8, 1)
    r.block('c', 18, 12, 12, 1)                  # a crumbling floor
    r.plat(14, 16, 5).plat(30, 16, 5)
    r.plat(22, 19, 4)
    r.plat(6, 21, 5).plat(37, 21, 5)
    r.solid(1, 24, 6, 4)                         # the west door's crawlspace
    tunnel(r, 1, 6, 27)
    r.solid(7, 25, 2, 3)
    r.clear(7, 27, 2, 1)
    r.item('page', 24, 18, 'page2')
    r.enemy('crawler', 12, 7).enemy('crawler', 32, 7).enemy('batter', 23, 13)
    r.enemy('walker', 20, 27, hp=3).enemy('gnat', 40, 17)

    r = R('C5', 'catacombs', 7, 7, 1, 1, name='Rye Hearth')
    r.door('W', 0).door('E', 0)
    r.add('hearth', 8, 12)
    r.add('npc', 4, 12, who='mozz', say='mozz_catacombs', needs='item:dough')

    r = R('C11', 'catacombs', 8, 7, 3, 1, name='Sunken Chapel')
    r.door('W', 0).door('N', 0).door('E', 0, 'wax')
    r.plat(4, 4, 6)                              # under the opening up to the Map Room
    r.plat(2, 8, 3).plat(12, 7, 5)
    r.solid(20, 10, 8, 3)                        # the sunken altar
    r.plat(39, 10, 4).plat(33, 8, 4).plat(28, 7, 3)   # steps back up over the altar from the pool's side
    r.liquid(28, 11, 9, 2)
    r.enemy('batter', 16, 3).enemy('crawler', 23, 9).enemy('gnat', 38, 5)

    r = R('C7', 'catacombs', 3, 7, 1, 3, name='Deep Stair')
    r.door('E', 0).door('S', 0)
    r.solid(1, 13, 10, 1)
    zigzag(r, range(16, 41, 3), first='right')
    r.block('x', 6, 43, 4, 2)                    # a cracked floor over the way down: popcorn
    r.solid(1, 43, 5, 1).solid(10, 43, 5, 1)
    r.enemy('crawler', 12, 15).enemy('gnat', 6, 25).enemy('crawler', 3, 33)

    # ================================================================== FROSTBITE CELLAR
    r = R('F0', 'frost', 3, 10, 1, 3, name='Ice Shaft')
    r.door('N', 0).door('W', 1).door('W', 2).door('E', 1)
    r.plat(6, 4, 4)
    zigzag(r, range(7, 41, 3), first='right')    # one climb, top to bottom (no floor half way to be caught under)
    r.clear(1, 40, 1, 1)                         # (headroom at the bottom west door)
    r.enemy('crawler', 12, 12).enemy('hopper', 8, 27).enemy('gnat', 6, 36)

    r = R('F7', 'frost', 2, 11, 1, 1, name='Frost Map')
    r.door('E', 0)
    r.add('maptable', 6, 12, area='frost')
    r.add('npc', 10, 12, who='mozz', say='mozz_frost')

    r = R('F2', 'frost', 2, 12, 1, 1, name='Sauce Shrine')
    r.door('E', 0)
    r.solid(5, 11, 4, 2)
    r.item('sauce', 7, 10, 'sauce')

    r = R('F1', 'frost', 4, 11, 4, 1, name='Icebox Hall')
    r.door('W', 0).door('E', 0)
    r.plat(8, 10, 4).plat(15, 7, 4).plat(24, 10, 6)
    r.solid(32, 8, 2, 5)
    r.plat(38, 9, 5).plat(48, 10, 4)
    r.solid(16, 2, 3, 2)                         # a shelf up high: a page (the Leaven Boots)
    r.plat(20, 4, 3)
    r.item('page', 21, 3, 'page3')
    r.enemy('hopper', 12, 12).enemy('slider', 28, 6).enemy('hopper', 44, 12).enemy('crawler', 33, 7)

    r = R('F3', 'frost', 8, 11, 1, 1, name='Frost Hearth')
    r.door('W', 0).door('S', 0)
    r.add('hearth', 4, 12)
    r.solid(10, 13, 2, 1)

    r = R('F4', 'frost', 7, 12, 2, 2, name='Glacier Steps')
    r.door('N', 1).door('W', 1).door('E', 1, 'wax')
    r.plat(21, 4, 6)                             # under the opening from the hearth
    zigzag(r, range(7, 26, 3), left=(14, 19), right=(23, 28), first='left')
    r.plat(4, 16, 6)
    r.block('r', 2, 12, 2, 3)                    # a wax-sealed cubby: a sauce packet
    r.solid(1, 11, 5, 1).solid(4, 12, 2, 3)
    r.item('packet', 1, 13, 'packet_f4')
    r.solid(1, 15, 1, 1)
    r.enemy('crawler', 16, 9).enemy('hopper', 6, 27).enemy('slider', 20, 20)

    r = R('F6', 'frost', 9, 13, 1, 1, name='Sugar Vault')
    r.door('W', 0, 'wax')
    r.solid(7, 11, 4, 2)
    r.item('frost', 9, 10, 'frost')

    r = R('F5', 'frost', 4, 13, 3, 1, name='Frozen Pipes')
    r.door('E', 0).door('S', 1)
    r.block('^', 26, 12, 13, 1)                  # a long pit of icicles, that only frozen frost flies can bridge
    for x in (29, 33, 37):
        r.freeze(x, 10)
    r.solid(1, 2, 6, 6)
    r.solid(8, 11, 3, 2)
    r.plat(3, 9, 3)
    r.enemy('slider', 30, 10).enemy('slider', 36, 10)
    r.enemy('crawler', 12, 12).enemy('crawler', 44, 12)
    for x in range(5, 45, 6):
        r.bg('T', x, 4)

    r = R('F8', 'frost', 5, 14, 3, 1, name='Slush Pit')
    r.door('N', 0).door('E', 0)
    r.plat(5, 4, 6)
    r.plat(2, 8, 3)
    r.solid(12, 10, 4, 3).solid(26, 9, 4, 4)
    r.liquid(16, 9, 10, 4)
    r.liquid(30, 10, 10, 3)
    r.enemy('hopper', 14, 9).enemy('crawler', 28, 8).enemy('gnat', 36, 5)

    r = R('F9', 'frost', 8, 14, 2, 1, name="Sundae's Den", boss='sundae', music='music/boss.wav')
    r.door('W', 0, 'boss')
    r.add('boss', 22, 12, name='sundae')

    # ================================================================== MOLTEN KITCHENS
    r = R('M0', 'molten', 11, 7, 1, 3, name='Scalding Stair')
    r.door('W', 0).door('S', 0)
    zigzag(r, range(16, 41, 3), first='right')
    r.solid(1, 13, 8, 1)
    r.enemy('crawler', 12, 18).enemy('gnat', 6, 30)

    r = R('M1', 'molten', 9, 10, 4, 1, name='Simmer Hall')
    r.door('N', 2).door('W', 0).door('E', 0)
    r.plat(36, 4, 6)                             # under the opening from the stair
    r.plat(30, 8, 4)
    r.block('~', 20, 12, 10, 1)                  # molten cheese, west ...
    r.block('~', 44, 12, 14, 1)                  # ... and a wide pool east: frozen fire beans to cross
    r.plat(23, 9, 3)
    r.solid(42, 8, 2, 5)
    for x in (47, 51, 55):
        r.freeze(x, 9)
    r.enemy('leaper', 47, 12).enemy('leaper', 51, 12).enemy('leaper', 55, 12)
    r.solid(5, 4, 3, 1)                          # a page on a high hook (the Whisk Spin)
    r.item('page', 6, 3, 'page4')
    r.enemy('crawler', 10, 12).enemy('gnat', 15, 5)

    r = R('M2', 'molten', 13, 10, 1, 1, name='Leaven Shrine')
    r.door('W', 0).door('S', 0)
    r.solid(3, 11, 4, 2)
    r.item('leaven', 5, 10, 'leaven')
    r.solid(10, 13, 2, 1)

    r = R('M3', 'molten', 8, 10, 1, 1, name='Molten Hearth')
    r.door('E', 0)
    r.add('hearth', 4, 12)
    r.add('maptable', 10, 12, area='molten')
    r.add('npc', 7, 12, who='mozz', say='mozz_molten')

    r = R('M4', 'molten', 13, 11, 1, 3, name='Furnace Drop', heat=True)
    r.door('N', 0).door('E', 2).door('S', 0)
    r.plat(6, 4, 4)
    zigzag(r, range(7, 41, 3), first='left')
    r.clear(7, 43, 2, 2)
    r.enemy('crawler', 3, 15).enemy('gnat', 8, 30)

    r = R('M5', 'molten', 14, 13, 3, 1, name='Boiler Room', heat=True)
    r.door('W', 0).door('E', 0)
    r.block('~', 8, 12, 30, 1)
    r.plat(9, 9, 3).plat(15, 7, 3).plat(21, 9, 3).plat(27, 7, 3).plat(33, 9, 3)
    r.solid(39, 10, 3, 3)
    r.plat(25, 4, 3)                             # a cake slice up on a pipe (the Leaven Boots)
    r.item('cake', 26, 3, 'cake_m5')
    r.enemy('leaper', 14, 12).enemy('leaper', 28, 12).enemy('crawler', 40, 9)

    r = R('M6', 'molten', 17, 13, 2, 1, name='Whisk Pantry', heat=True)
    r.door('W', 0).door('N', 1)
    r.plat(21, 4, 6)
    r.plat(15, 8, 4).plat(26, 9, 4)
    r.solid(5, 11, 4, 2)
    r.item('whisk', 7, 10, 'whisk')
    r.enemy('gnat', 18, 5).enemy('crawler', 27, 8)

    r = R('M10', 'molten', 18, 10, 1, 3, name='Steam Pipe', heat=True)
    r.door('N', 0).door('S', 0)
    r.plat(6, 4, 4)
    zigzag(r, range(7, 41, 3), first='right')
    r.clear(7, 43, 2, 2)
    r.enemy('crawler', 12, 20)

    r = R('M7', 'molten', 13, 14, 1, 2, name='Cheese Falls', heat=True)
    r.door('N', 0).door('W', 1)
    r.plat(6, 4, 4)
    zigzag(r, range(7, 26, 3), first='right')
    r.fill('g', 1, 25, 1, 3)                     # a grate over the west door ...
    for y in range(25, 28):
        r.garlic_gates.add((1, y))
    r.solid(8, 18, 7, 1)
    r.add('switch', 12, 15, gate=[1, 25, 1, 3])  # ... opened by a switch behind the wall (garlic goes through walls)
    r.solid(9, 14, 1, 4)
    r.enemy('crawler', 4, 22)

    r = R('M8', 'molten', 11, 15, 2, 1, name="Wyrm's Furnace", boss='wyrm', music='music/boss.wav')
    r.door('E', 0, 'boss')
    r.block('~', 1, 12, 25, 1)                   # a moat of molten cheese; a dry landing by the door
    r.plat(4, 10, 4).plat(14, 8, 4).plat(22, 10, 3)
    r.add('boss', 16, 12, name='wyrm')

    # ================================================================== BRINE WORKS
    r = R('B0', 'brine', 15, 4, 2, 1, name='Drain Gate')
    r.door('S', 0).door('E', 0)
    r.solid(10, 13, 2, 1)
    r.plat(14, 10, 4).plat(22, 8, 4)
    r.enemy('turret', 27, 3, shoots='brine').enemy('crawler', 18, 12)

    r = R('B1', 'brine', 17, 4, 3, 1, name='Pickling Vats')
    r.door('W', 0).door('E', 0)
    r.solid(8, 9, 2, 4).solid(20, 9, 2, 4).solid(32, 9, 2, 4)
    r.liquid(10, 7, 10, 6)                       # vats of brine
    r.liquid(22, 7, 10, 6)
    r.plat(13, 5, 4).plat(25, 5, 4)
    r.item('page', 26, 12, 'page6')
    r.solid(36, 3, 3, 1)
    r.block('r', 40, 2, 1, 3)                    # a wax-sealed shelf: a sauce packet
    r.solid(40, 5, 4, 1)
    r.item('packet', 42, 3, 'packet_b1')
    r.enemy('turret', 15, 3, shoots='brine').enemy('walker', 38, 12, shoots='brine', hp=4)

    r = R('B2', 'brine', 20, 4, 1, 1, name='Brine Hearth')
    r.door('W', 0).door('S', 0)
    r.add('hearth', 4, 12)
    r.add('maptable', 11, 12, area='brine')
    r.add('npc', 13, 12, who='mozz', say='mozz_brine')

    r = R('B3', 'brine', 20, 5, 1, 4, name='Brine Shaft')
    r.door('N', 0).door('W', 1).door('E', 2).door('S', 0)
    r.plat(6, 4, 4)
    zigzag(r, range(7, 56, 3), first='left')
    r.clear(7, 58, 2, 2)
    r.enemy('crawler', 3, 15).enemy('gnat', 8, 33).enemy('turret', 12, 48, shoots='brine')

    r = R('B4', 'brine', 19, 6, 1, 1, name='Mitt Locker')
    r.door('E', 0)
    r.solid(3, 11, 4, 2)
    r.item('mitts', 5, 10, 'mitts')

    r = R('B6', 'brine', 21, 7, 2, 1, name='Garlic Garden')
    r.door('W', 0)
    r.solid(6, 2, 22, 9)                         # a garden behind a wall: a crawlspace and cracked blocks
    tunnel(r, 6, 20, 12)
    r.block('x', 21, 11, 2, 2)
    r.clear(23, 11, 7, 2)
    r.clear(6, 12, 22, 1)
    r.solid(21, 13, 2, 1)
    r.solid(24, 13, 1, 1)
    r.item('garlic', 27, 12, 'garlic')
    r.item('page', 25, 12, 'page5')
    r.enemy('crawler', 3, 12)

    r = R('B7', 'brine', 20, 9, 1, 1, name='Lab Door')
    r.door('N', 0).door('W', 0)
    r.plat(5, 4, 6)
    r.plat(10, 9, 4)

    r = R('B8', 'brine', 18, 9, 2, 1, name="Dr. Brine's Lab", boss='brine', music='music/boss.wav')
    r.door('E', 0, 'boss').door('S', 0)
    r.fill('g', 7, 13, 2, 2)                      # a grate over the drain, opened when the doctor is beaten
    r.boss_gates = {(7, 13), (8, 13), (7, 14), (8, 14)}
    r.add('boss', 16, 12, name='brine')

    # ================================================================== CASTLE FROMAGE
    r = R('X0', 'castle', 18, 2, 2, 2, name='Grand Foyer')
    r.door('W', 1).door('E', 0).door('E', 1)
    r.solid(1, 13, 8, 1)
    r.plat(10, 24, 5).plat(18, 21, 5).plat(25, 18, 5).plat(18, 15, 4)
    r.plat(25, 13, 6)
    r.add('hearth', 5, 27)
    r.enemy('ghost', 15, 8).enemy('knight', 22, 27)

    r = R('X1', 'castle', 20, 2, 3, 1, name='Portrait Gallery')
    r.door('W', 0).door('E', 0)
    for x in (8, 18, 28, 38):
        r.bg('*', x, 5)
    r.plat(12, 9, 4).plat(24, 8, 4).plat(34, 9, 4)
    r.enemy('knight', 20, 12).enemy('ghost', 30, 6).enemy('knight', 40, 12)

    r = R('X2', 'castle', 20, 3, 3, 1, name='Wine Cellar')
    r.door('W', 0).door('E', 0)
    r.solid(10, 10, 6, 3).solid(26, 9, 6, 4)
    r.plat(18, 7, 4)
    r.item('page', 19, 6, 'page7')
    r.enemy('gnat', 22, 4).enemy('knight', 36, 12).enemy('crawler', 12, 9)

    r = R('X3', 'castle', 23, 1, 2, 2, name='Library')
    r.door('W', 1).door('E', 0)
    for y in range(4, 26, 4):
        for x in (2, 29):
            r.bg('{', x, y)
    zigzag(r, range(25, 9, -4), left=(4, 12), right=(18, 27), first='left')
    r.plat(10, 12, 12)
    r.add('maptable', 15, 11, area='castle')
    r.item('page', 22, 11, 'page8')
    r.plat(1, 7, 4)
    r.item('packet', 2, 6, 'packet_x3')
    r.enemy('ghost', 8, 18).enemy('ghost', 24, 6).enemy('knight', 20, 27)

    r = R('X4', 'castle', 23, 3, 2, 1, name='Fondue Kitchen')
    r.door('W', 0).door('E', 0)
    r.solid(12, 10, 8, 3)
    r.plat(5, 8, 4)
    r.item('cake', 6, 7, 'cake_x4')
    r.add('hearth', 26, 12)
    r.enemy('knight', 22, 12).enemy('gnat', 15, 5)

    r = R('X6', 'castle', 25, 0, 1, 4, name='Clock Tower')
    r.door('W', 0).door('W', 1).door('W', 3)
    r.plat(1, 13, 5)
    r.plat(1, 28, 5)
    r.plat(10, 50, 4).plat(3, 40, 3).plat(11, 31, 3).plat(4, 20, 3).plat(11, 9, 3)
    r.enemy('ghost', 8, 35).enemy('ghost', 6, 15).enemy('batter', 12, 3)

    r = R('X7', 'castle', 23, 0, 2, 1, name='Throne Room', boss='count', music='music/count.wav')
    r.door('E', 0, 'boss').door('W', 0, 'boss')
    r.plat(5, 9, 4).plat(23, 9, 4)
    r.add('boss', 16, 12, name='count')

    r = R('X8', 'castle', 22, 0, 1, 1, name="Pip's Cell")
    r.door('E', 0)
    r.add('npc', 6, 12, who='pip', say='pip_rescue', needs='boss:count')
    return rooms
