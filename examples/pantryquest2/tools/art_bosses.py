"""The guardians, drawn from shapes (they're too big to write out a pixel at a time), and a few
odd pieces: the Big Cheese, spores, lightning, the garlic switch. All face left."""
import math
from pixels import Canvas, PALETTE, sheet, mix, noise

P = PALETTE


def finish(c, shade=True):
    if shade:
        c.shade((-1, -1), 0.18)
    c.outline()
    return c


# ---- SIR STALE: a knight whose helmet is a loaf gone hard as stone, with a baguette lance ---------------
def stale_frame(pose):
    c = Canvas(40, 48)
    bob = 1 if pose in (2,) else 0
    crouch = 4 if pose in (3, 7) else 0
    top = 2 + crouch + bob
    # legs
    if pose == 7:
        c.rect(14, 36, 5, 8, P['G']); c.rect(22, 34, 5, 8, P['G'])
    elif pose in (1, 2):
        a = 3 if pose == 1 else -3
        c.rect(15 + a, 36, 5, 11, P['G']); c.rect(22 - a, 36, 5, 11, P['G'])
        c.rect(14 + a, 45, 7, 3, P['D']); c.rect(21 - a, 45, 7, 3, P['D'])
    else:
        c.rect(14, 36 + crouch // 2, 5, 11 - crouch // 2, P['G']); c.rect(22, 36 + crouch // 2, 5, 11 - crouch // 2, P['G'])
        c.rect(13, 45, 7, 3, P['D']); c.rect(21, 45, 7, 3, P['D'])
    # body armour
    c.rect(11, top + 17, 19, 18, P['g'])
    c.rect(11, top + 17, 19, 3, P['W'])
    c.rect(19, top + 20, 3, 15, P['G'])
    c.rect(11, top + 30, 19, 2, P['A'])                     # a rusty belt
    # mould on the armour
    for (x, y) in ((13, top + 24), (26, top + 27), (16, top + 32)):
        c.ellipse(x, y, 1.6, 1.4, P['m'])
    # the loaf helmet
    c.ellipse(20, top + 9, 12, 9, P['n'])
    c.ellipse(20, top + 6, 11, 6, P['t'])
    for x in (12, 18, 24):
        c.line(x, top + 2, x + 4, top + 7, P['N'])
    for (x, y) in ((10, top + 12), (29, top + 9), (23, top + 3)):
        c.ellipse(x, y, 2, 1.6, P['m'])
    visor_open = pose in (5, 6)
    if visor_open:                                          # the visor up: his face, crumbly and furious
        c.rect(10, top + 10, 14, 6, P['t'])
        c.rect(12, top + 11, 3, 2, P['r']); c.rect(19, top + 11, 3, 2, P['r'])
        c.rect(13, top + 14, 7, 1, P['N'])
        c.rect(9, top + 7, 16, 3, P['G'])                   # the raised visor
    else:
        c.rect(9, top + 10, 16, 5, P['G'])
        c.rect(10, top + 12, 14, 1, P['k'])
        c.rect(12, top + 11, 2, 2, P['r']); c.rect(19, top + 11, 2, 2, P['r'])
    # arm and lance
    if pose == 4:                                           # charging: lance levelled low
        c.rect(0, top + 27, 22, 4, P['t']); c.rect(0, top + 27, 22, 1, P['e'])
        c.rect(18, top + 24, 6, 7, P['g'])
    elif pose == 6:                                         # throwing: arm raised
        c.rect(4, top + 4, 5, 14, P['g']); c.ellipse(6, top + 3, 3, 3, P['t'])
    elif pose == 5:                                         # dizzy: lance dropped, stars
        c.rect(26, top + 28, 14, 3, P['t'])
        for k in range(3):
            a = k * 2.1
            x = int(20 + math.cos(a) * 9); y = int(top - 1 + math.sin(a) * 2)
            c.set(x, y, P['y']); c.set(x + 1, y, P['y']); c.set(x, y + 1, P['y'])
    else:
        c.rect(3, top + 15, 4, 24, P['t']); c.rect(3, top + 15, 1, 24, P['e'])     # lance upright
        c.ellipse(5, top + 14, 3, 3, P['t'])
        c.rect(5, top + 24, 8, 5, P['g'])
    c.rect(28, top + 18, 5, 10, P['g'])                     # back arm / shield edge
    return finish(c)


def stale_sheet():
    return sheet([stale_frame(i) for i in range(8)], 40, 48)


# ---- THE ABOMINABLE SUNDAE --------------------------------------------------------------------------------------------
def sundae_frame(pose):
    c = Canvas(56, 56)
    if pose == 3:                                           # curled into a rolling ball
        c.ellipse(28, 28, 24, 24, P['w'])
        c.ellipse(20, 20, 9, 8, P['p'])
        c.ellipse(35, 34, 9, 8, (130, 80, 50))
        c.ellipse(28, 8, 3, 3, P['r'])
        for k in range(8):
            a = k * math.pi / 4
            c.line(28 + math.cos(a) * 10, 28 + math.sin(a) * 10, 28 + math.cos(a) * 22, 28 + math.sin(a) * 22, P['W'])
        return finish(c)
    squash = 4 if pose == 1 else 0
    melt = 8 if pose == 4 else 0
    base = 54
    c.ellipse(28, base - 16 + squash // 2, 20, 16 - squash // 2, P['w'])       # the body: a great scoop of vanilla
    c.ellipse(28, base - 34 + squash + melt, 17, 13, P['w'])                   # head
    c.ellipse(19, base - 44 + squash + melt, 10, 8, P['p'])                    # strawberry scoop
    c.ellipse(37, base - 44 + squash + melt, 10, 8, (130, 80, 50))             # chocolate scoop
    c.ellipse(28, base - 53 + squash + melt, 3.5, 3.5, P['r'])                 # the cherry
    c.line(28, base - 56 + squash + melt, 31, base - 58 + squash + melt, P['d'])
    for x in (12, 20, 38, 45):                                                 # chocolate drips
        c.rect(x, base - 26 + (x % 5), 2, 6 + (x % 4), (130, 80, 50))
    fy = base - 34 + squash + melt
    c.rect(17, fy - 3, 6, 4, P['b']); c.rect(31, fy - 3, 6, 4, P['b'])         # angry eyes
    c.line(16, fy - 6, 23, fy - 3, P['k']); c.line(38, fy - 6, 31, fy - 3, P['k'])
    if pose == 2:
        c.rect(19, fy + 4, 14, 6, P['k']); c.rect(21, fy + 4, 3, 3, P['w']); c.rect(29, fy + 4, 3, 3, P['w'])
        c.ellipse(5, base - 30, 6, 5, P['w'])                                  # an arm, flung forward
        c.ellipse(4, base - 40, 4, 4, P['C'])                                  # with a scoop of ice
    else:
        c.rect(20, fy + 5, 12, 3, P['k'])
        c.ellipse(8, base - 18, 6, 9, P['w'])                                  # arms down
        c.ellipse(48, base - 18, 6, 9, P['w'])
    c.ellipse(16, base - 3, 7, 3, P['W']); c.ellipse(40, base - 3, 7, 3, P['W'])   # feet
    if pose == 4:
        c.ellipse(28, base - 2, 26, 3, P['w'])                                 # melting into a puddle
    return finish(c)


def sundae_sheet():
    return sheet([sundae_frame(i) for i in range(5)], 56, 56)


# ---- FRANKENFURTER: a hot dog brought to life in the doctor's vats ---------------------------------------
def frank_frame(pose):
    c = Canvas(40, 56)
    if pose == 5:                                           # down
        c.ellipse(20, 48, 18, 7, P['t'])
        c.ellipse(20, 46, 15, 4, (190, 70, 60))
        c.line(8, 46, 32, 46, P['y'])
        c.rect(30, 42, 3, 2, P['G'])
        return finish(c)
    step = 2 if pose == 1 else 0
    c.rect(12 - step, 44, 6, 10, (120, 60, 50)); c.rect(23 + step, 44, 6, 10, (120, 60, 50))   # legs
    c.rect(11 - step, 52, 8, 4, P['k']); c.rect(22 + step, 52, 8, 4, P['k'])
    c.ellipse(20, 30, 14, 17, P['t'])                       # the bun
    c.ellipse(20, 26, 9, 21, (196, 76, 64))                 # the sausage
    for y in range(10, 44, 4):                              # mustard zigzag
        c.set(17 + (y // 4) % 2 * 4, y, P['y']); c.set(18 + (y // 4) % 2 * 4, y + 1, P['y'])
    for y in (18, 30):                                      # stitches
        c.line(13, y, 27, y, P['k'])
        for x in range(14, 27, 3):
            c.set(x, y - 1, P['k']); c.set(x, y + 1, P['k'])
    c.rect(9, 14, 3, 3, P['G']); c.rect(28, 14, 3, 3, P['G'])     # neck bolts
    c.rect(14, 8, 4, 3, P['y']); c.rect(22, 8, 4, 3, P['y'])       # glowing eyes
    c.rect(15, 9, 2, 1, P['k']); c.rect(23, 9, 2, 1, P['k'])
    c.rect(15, 13, 10, 2, P['k'])
    if pose == 3:                                           # punch: arm thrust out
        c.rect(0, 24, 14, 6, P['t']); c.ellipse(2, 27, 4, 4, (196, 76, 64))
    elif pose == 4:                                         # lob: arm up
        c.rect(4, 6, 6, 18, P['t']); c.ellipse(7, 5, 4, 4, P['y'])
    elif pose == 2:                                         # wind-up: arm back
        c.rect(30, 22, 10, 6, P['t'])
    else:
        c.rect(4, 22, 6, 16, P['t']); c.rect(31, 22, 6, 16, P['t'])
    return finish(c)


def frank_sheet():
    return sheet([frank_frame(i) for i in range(6)], 40, 56)


def doctor_frame(cracks):
    c = Canvas(24, 32)
    c.ellipse(12, 18, 11, 13, (150, 220, 210))             # the jar
    c.rect(4, 2, 16, 4, P['G']); c.rect(4, 2, 16, 1, P['W'])
    c.ellipse(12, 20, 5, 8, P['L'])                         # the doctor: a pickle in goggles
    for (x, y) in ((10, 15), (14, 19), (11, 24)):
        c.set(x, y, P['l'])
    c.rect(7, 15, 10, 3, P['G']); c.rect(8, 15, 3, 3, P['c']); c.rect(13, 15, 3, 3, P['c'])
    c.rect(9, 22, 6, 1, P['k'])
    c.rect(7, 26, 10, 4, P['w'])                            # lab coat
    for k in range(cracks):                                 # cracks in the glass
        x0 = 4 + k * 6
        c.line(x0, 8 + k * 3, x0 + 4, 14 + k * 4, P['w'])
        c.line(x0 + 4, 14 + k * 4, x0 + 1, 20 + k * 2, P['w'])
    c.ellipse(7, 10, 2, 4, (220, 255, 250))                 # shine
    return finish(c, shade=False)


def doctor_sheet():
    return sheet([doctor_frame(i) for i in range(4)], 24, 32)


# ---- THE FLAMBE WYRM ----------------------------------------------------------------------------------------------------
def wyrm_frame(kind):
    c = Canvas(32, 32)
    if kind == 2:                                           # a piece of body: a ball of burnt caramel, flames on top
        c.ellipse(16, 17, 11, 11, (170, 90, 30))
        c.ellipse(13, 13, 5, 4, (220, 140, 60))
        for k in range(3):
            c.ellipse(10 + k * 6, 6, 2.5, 4.5, P['f'])
            c.ellipse(10 + k * 6, 7, 1.2, 2.5, P['y'])
        return finish(c)
    c.ellipse(16, 18, 12, 11, P['o'])                       # head, facing up (it turns as it flies)
    c.ellipse(16, 8, 8, 8, P['O'])                          # snout
    c.ellipse(10, 16, 3, 3, P['y']); c.ellipse(22, 16, 3, 3, P['y'])
    c.rect(10, 15, 1, 3, P['k']); c.rect(22, 15, 1, 3, P['k'])
    for x in (6, 26):                                       # horns of flame
        c.ellipse(x, 26, 3, 5, P['f']); c.ellipse(x, 27, 1.5, 3, P['H'])
    if kind == 1:                                           # mouth open
        c.ellipse(16, 4, 5, 4, P['k'])
        c.ellipse(16, 3, 3, 2, P['f'])
        c.set(12, 6, P['w']); c.set(20, 6, P['w'])
    else:
        c.line(11, 4, 21, 4, P['k'])
    return finish(c)


def wyrm_sheet():
    return sheet([wyrm_frame(i) for i in range(3)], 32, 32)


# ---- COUNT FROMAGE ------------------------------------------------------------------------------------------------------
def count_frame(pose):
    c = Canvas(32, 48)
    cape = (90, 20, 40)
    lining = (170, 30, 50)
    if pose == 1:                                           # cape swept open, arms out
        for y in range(14, 46):
            w = 6 + (y - 14) * 0.45
            c.rect(int(16 - w - 6), y, int(w * 2 + 12), 1, cape)
            c.rect(int(16 - w - 3), y, int(w * 2 + 6), 1, lining)
    else:
        for y in range(14, 46):
            w = 5 + (y - 14) * 0.25
            c.rect(int(16 - w), y, int(w * 2), 1, cape)
    c.rect(12, 18, 8, 22, P['k'])                           # his suit
    c.rect(14, 18, 4, 10, P['w'])                           # shirt front
    c.rect(15, 20, 2, 2, P['Y'])                            # brooch
    c.rect(12, 40, 3, 6, P['k']); c.rect(17, 40, 3, 6, P['k'])
    for y in range(8, 18):                                  # the high collar
        c.set(7 + (y - 8) // 2, y, lining); c.set(24 - (y - 8) // 2, y, lining)
    c.ellipse(16, 10, 6, 7, (225, 232, 240))                # blue cheese head
    for (x, y) in ((13, 7), (18, 12), (15, 13), (19, 6)):
        c.set(x, y, P['b'])
    c.rect(10, 2, 12, 3, P['k']); c.rect(15, 5, 2, 2, P['k'])  # hair, widow's peak
    c.rect(12, 9, 3, 2, P['r']); c.rect(17, 9, 3, 2, P['r'])
    c.rect(14, 14, 4, 1, P['k']); c.set(14, 15, P['w']); c.set(17, 15, P['w'])
    if pose == 1:
        c.ellipse(3, 22, 3, 3, (225, 232, 240)); c.ellipse(29, 22, 3, 3, (225, 232, 240))   # hands
    if pose == 3:
        c.recolor({(225, 232, 240): (255, 180, 180)})
    return finish(c)


def count_sheet():
    return sheet([count_frame(i) for i in range(4)], 32, 48)


def bigcheese_frame(pose):
    c = Canvas(64, 64)
    c.ellipse(32, 32, 30, 30, P['E'])                       # a wheel of cheese
    c.ellipse(32, 32, 26, 26, P['e'])
    for (x, y, r) in ((18, 18, 4), (44, 20, 3), (14, 40, 3), (48, 44, 5), (30, 50, 2.5)):
        c.ellipse(x, y, r, r, P['F'])
    for (x, y, r) in ((22, 44, 6), (46, 30, 5), (36, 12, 4)):   # mould
        c.ellipse(x, y, r, r * 0.8, P['m'] if (x + y) % 2 else (90, 120, 170))
    c.rect(20, 24, 8, 5, P['r']); c.rect(36, 24, 8, 5, P['r'])   # furious eyes
    c.line(18, 21, 28, 25, P['k']); c.line(46, 21, 36, 25, P['k'])
    if pose == 2:
        c.ellipse(32, 40, 12, 8, P['k'])
        for x in (24, 30, 36, 40):
            c.rect(x, 33, 2, 4, P['w'])
    else:
        c.rect(22, 38 + pose, 20, 3, P['k'])
        c.rect(24, 41 + pose, 2, 3, P['w']); c.rect(38, 41 + pose, 2, 3, P['w'])
    return finish(c)


def bigcheese_sheet():
    return sheet([bigcheese_frame(i) for i in range(3)], 64, 64)


def spore_sheet():
    f = []
    for k in range(2):
        c = Canvas(16, 16)
        c.ellipse(8, 8, 5 + k * 0.5, 5 + k * 0.5, P['m'])
        for i in range(8):
            a = i * math.pi / 4 + k * 0.4
            c.set(int(8 + math.cos(a) * 7), int(8 + math.sin(a) * 7), P['z'])
        c.set(6, 6, P['l'])
        c.rect(6, 8, 1, 2, P['k']); c.rect(9, 8, 1, 2, P['k'])
        f.append(finish(c, shade=False))
    return sheet(f, 16, 16)


def bolt_sheet():
    f = []
    for k in range(4):
        c = Canvas(16, 192)
        x = 8
        for y in range(0, 192, 6):
            nx = 8 + int((noise(y, k, 3) - 0.5) * 10)
            c.line(x, y, nx, y + 6, P['w'] if k < 3 else P['c'])
            c.line(x + 1, y, nx + 1, y + 6, P['y'])
            x = nx
        f.append(c)
    return sheet(f, 16, 192)


def switch_sheet():
    f = []
    for on in (False, True):
        c = Canvas(16, 16)
        c.rect(1, 1, 14, 14, P['G'])
        c.ellipse(8, 9, 5, 5, P['w'] if on else P['W'])
        c.rect(7, 2, 2, 4, P['l'] if on else P['G'])
        c.ellipse(8, 9, 1.5, 4, P['W'] if on else P['g'])
        if on:
            c.ellipse(8, 9, 2, 2, P['y'])
        f.append(finish(c, shade=False))
    return sheet(f, 16, 16)
