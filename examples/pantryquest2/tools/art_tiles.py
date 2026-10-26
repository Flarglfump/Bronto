"""The tilesets: one for each part of the world, drawn in code.

Each tileset is 16 tiles wide (16 x 16 pixels each):
  row 0   the solid tiles, by which neighbours are solid too (N=1 E=2 S=4 W=8): 'A'..'P'
  row 1   interior variants 'Q' 'R' 'S', then the special tiles (see SPECIAL)
  row 2   the background tiles (BG)
  row 3   more background tiles
The characters for each tile are in LEGEND (make_rooms.py uses the same lists).
"""
from pixels import Canvas, PALETTE, noise, mix

SOLID_CHARS = 'ABCDEFGHIJKLMNOP'          # by neighbour mask
FILL_VARIANTS = 'QRS'                      # interior (all four neighbours solid), with details
SPECIAL = [                                 # row 1 after the variants
    ('c', 'crumb'),      # crumb block: a shot breaks it (it grows back)
    ('x', 'bomb'),       # cracked block: popcorn breaks it
    ('r', 'wax'),        # wax seal block: hot sauce breaks it
    ('^', 'spikes'),     # spikes on the floor
    ('v', 'spikes_down'),
    ('~', 'lava'),       # molten (non-solid; hurts)
    ('=', 'liquid'),     # brine / slush (non-solid; slows) - drawn in the front map, see-through
    ('-', 'liquid_top'), # its surface
    ('Y', 'door_block'), # invisible solid: a closed door's cells (not 'D': that's a wall tile)
    ('u', 'pillar'),     # a solid pillar
    ('h', 'ledge'),      # a thin ledge (solid)
    ('g', 'grate'),      # a grate (solid, see-through)
    ('z', 'moss'),       # foreground moss / icicles / drips (front map)
]
BG = [                                       # rows 2 and 3: the background (none of it is solid)
    ('#', 'wall'), ('$', 'wall2'), ('%', 'wall3'), ('&', 'dark'), ('*', 'window'), ('+', 'torch'),
    ('(', 'arch_l'), (')', 'arch_r'), ('[', 'pillar_bg'), (']', 'banner'), ('{', 'shelf'), ('}', 'chain'),
    ('<', 'web'), ('>', 'skull'), ('|', 'column'), ('!', 'candle'),
    ('0', 'sky'), ('1', 'stars'), ('2', 'moon_tl'), ('3', 'moon_tr'), ('4', 'moon_bl'), ('5', 'moon_br'),
    ('6', 'roof'), ('7', 'house'), ('8', 'house_win'), ('9', 'hill'), ('@', 'hill_top'), ('`', 'fence'),
    (':', 'crate'), (';', 'barrel'), ('T', 'pipe_h'), ('U', 'pipe_v'),
]
LEGEND = SOLID_CHARS + FILL_VARIANTS + ''.join(c for c, _ in SPECIAL)
LEGEND = LEGEND + '.' * (32 - len(LEGEND)) + ''.join(c for c, _ in BG)
SOLID = SOLID_CHARS + FILL_VARIANTS + 'cxrYuhg'

P = PALETTE


# ---- the parts of the world: their colours and their textures -----------------------------------
STYLES = {
    'village': dict(base=(84, 70, 96), light=(124, 108, 136), dark=(46, 38, 58), top=(70, 150, 80), top_light=(130, 200, 110),
                    bg=(26, 26, 54), bg_light=(40, 40, 76), accent=(250, 210, 110), pattern='cobble'),
    'catacombs': dict(base=(148, 96, 52), light=(206, 150, 90), dark=(84, 50, 26), top=(222, 176, 112), top_light=(246, 214, 160),
                      bg=(46, 26, 30), bg_light=(70, 42, 44), accent=(150, 200, 90), pattern='brick'),
    'frost': dict(base=(110, 170, 220), light=(190, 232, 250), dark=(50, 90, 150), top=(236, 248, 252), top_light=(255, 255, 255),
                  bg=(18, 34, 70), bg_light=(30, 56, 100), accent=(170, 240, 255), pattern='ice'),
    'molten': dict(base=(110, 70, 70), light=(160, 110, 100), dark=(54, 30, 34), top=(220, 110, 40), top_light=(255, 180, 70),
                   bg=(50, 18, 16), bg_light=(84, 30, 22), accent=(255, 150, 40), pattern='iron'),
    'brine': dict(base=(70, 120, 90), light=(120, 180, 130), dark=(30, 66, 50), top=(150, 210, 120), top_light=(200, 240, 160),
                  bg=(14, 40, 36), bg_light=(24, 64, 56), accent=(190, 240, 120), pattern='glaze'),
    'castle': dict(base=(212, 170, 70), light=(246, 214, 120), dark=(150, 106, 34), top=(250, 230, 160), top_light=(255, 248, 210),
                   bg=(40, 22, 50), bg_light=(66, 38, 80), accent=(190, 120, 240), pattern='cheese'),
}


def texture(style, x, y, variant=0):
    """The fill of a solid tile at pixel (x, y) (0..15), as a colour."""
    s = STYLES[style]
    base, light, dark = s['base'], s['light'], s['dark']
    n = noise(x, y, variant * 7 + sum(ord(ch) for ch in style) % 97)   # (not hash(): Python changes it every run)
    pat = s['pattern']
    if pat == 'brick':                                  # bread-crust bricks, offset rows
        row = y // 8
        bx = (x + (4 if row % 2 else 0)) % 16
        if y % 8 == 7 or bx in (0, 15):
            return dark
        c = base if n > 0.25 else mix(base, dark, 0.35)
        if y % 8 == 0:
            c = mix(c, light, 0.5)
        if n > 0.93:
            c = light
        return c
    if pat == 'cobble':                                 # round stones
        cx, cy = (x // 8) * 8 + 4 + ((y // 8) % 2) * 2, (y // 8) * 8 + 4
        d = abs(x - cx) + abs(y - cy)
        if d >= 5:
            return dark
        return light if d <= 1 and n > 0.5 else (base if n > 0.2 else mix(base, dark, 0.3))
    if pat == 'ice':                                    # big ice blocks with frost streaks
        if x == 0 or y == 0:
            return dark
        if (x + y) % 11 == 0 and n > 0.4:
            return light
        return base if n > 0.15 else mix(base, light, 0.4)
    if pat == 'iron':                                   # riveted iron plates
        if x in (0, 15) or y in (0, 15):
            return dark
        if (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):
            return light
        return base if n > 0.2 else mix(base, dark, 0.25)
    if pat == 'glaze':                                  # glazed tiles in a grid
        if x % 8 == 0 or y % 8 == 0:
            return dark
        if (x % 8, y % 8) in ((1, 1), (2, 1), (1, 2)):
            return light
        return base if n > 0.1 else mix(base, light, 0.3)
    if pat == 'cheese':                                 # cheese stone: blocks with holes
        if y % 8 == 0 or (x + (0 if (y // 8) % 2 else 8)) % 16 == 0:
            return dark
        for hx, hy, r in ((5, 4, 2.2), (11, 12, 1.6), (12, 3, 1.0)) if variant != 1 else ((8, 9, 2.6), (3, 3, 1.2)):
            d = ((x - hx) ** 2 + (y - hy) ** 2) ** 0.5
            if d < r:
                return dark if y < hy else mix(base, dark, 0.5)
        return base if n > 0.15 else mix(base, light, 0.5)
    return base


def solid_tile(style, mask, variant=0):
    """A solid tile; the open sides (neighbours not solid) get a surface and an edge."""
    s = STYLES[style]
    c = Canvas(16, 16)
    open_n, open_e, open_s, open_w = not (mask & 1), not (mask & 2), not (mask & 4), not (mask & 8)
    for y in range(16):
        for x in range(16):
            col = texture(style, x, y, variant)
            if open_n and y < 4:                          # the top surface: crust, grass, snow, rind
                col = s['top_light'] if y == 0 else (s['top'] if y < 3 else mix(s['top'], s['dark'], 0.5))
                if y == 3 and noise(x, 3, 5) > 0.6:
                    col = s['top']
            if open_s and y >= 14:
                col = s['dark'] if y == 14 else P['k']
            if open_w and x <= 1:
                col = mix(col, s['dark'], 0.6) if x == 1 else P['k']
            if open_e and x >= 14:
                col = mix(col, s['dark'], 0.6) if x == 14 else P['k']
            if open_n and y == 0 and (open_w and x == 0 or open_e and x == 15):
                col = None                                # rounded corners
            if open_s and y == 15 and (open_w and x == 0 or open_e and x == 15):
                col = None
            if col is not None:
                c.set(x, y, col)
    if open_n and style == 'frost':                       # icicles hang from snowy tops? (no: from bottoms)
        pass
    if open_s and style == 'frost':
        for x in (3, 9, 12):
            for k in range(3):
                pass
    return c


def special_tile(style, name):
    s = STYLES[style]
    c = Canvas(16, 16)
    if name == 'crumb':                                   # a block of packed crumbs, cracked into pieces
        for y in range(16):
            for x in range(16):
                n = noise(x, y, 11)
                col = mix(s['light'], s['base'], 0.4) if n > 0.3 else s['dark']
                if x in (0, 15) or y in (0, 15):
                    col = s['dark']
                c.set(x, y, col)
        for x, y in ((3, 3), (8, 6), (12, 10), (5, 11), (10, 2)):
            c.set(x, y, s['top_light'])
    elif name == 'bomb':                                   # a solid block with a crack down it
        for y in range(16):
            for x in range(16):
                c.set(x, y, texture(style, x, y, 2))
        for y in range(16):
            x = 7 + (1 if (y // 3) % 2 else -1) + (y % 2)
            c.set(x, y, P['k'])
            c.set(x + 1, y, s['dark'])
        for k in range(4):
            c.set(4 + k, 9 + k // 2, P['k'])
    elif name == 'wax':                                    # a red wax seal
        c.rect(0, 0, 16, 16, (130, 20, 30))
        c.ellipse(8, 8, 6.5, 6.5, (200, 40, 50))
        c.ellipse(8, 8, 4, 4, (150, 24, 36))
        c.rect(7, 4, 2, 8, (230, 90, 90))
        c.rect(4, 7, 8, 2, (230, 90, 90))
        for x in range(16):
            c.set(x, 0, P['k']); c.set(x, 15, P['k']); c.set(0, x, P['k']); c.set(15, x, P['k'])
    elif name in ('spikes', 'spikes_down'):                # breadstick / icicle / cheese-grater spikes
        col, light = s['light'], P['w']
        for i in range(4):
            for y in range(10):
                w = (10 - y) // 3
                for x in range(i * 4 + 2 - w, i * 4 + 2 + w + 1):
                    yy = 15 - y if name == 'spikes' else y
                    c.set(x, yy, light if x == i * 4 + 1 else col)
        for x in range(16):
            c.set(x, 15 if name == 'spikes' else 0, s['dark'])
    elif name == 'lava':                                   # molten cheese, bubbling
        for y in range(16):
            for x in range(16):
                n = noise(x, y, 23)
                col = P['H'] if n > 0.7 else (P['f'] if n > 0.25 else P['O'])
                if y < 2:
                    col = P['y']
                c.set(x, y, col)
    elif name in ('liquid', 'liquid_top'):                  # brine (see-through)
        tint = s['accent']
        for y in range(16):
            for x in range(16):
                a = 110
                col = mix(tint, s['dark'], 0.5)
                if name == 'liquid_top' and y < 3:
                    col, a = (mix(tint, P['w'], 0.4) if y == 0 or (x + y) % 5 == 0 else tint), 200
                elif noise(x, y, 31) > 0.95:
                    col, a = P['w'], 160
                c.set(x, y, (col[0], col[1], col[2], a))
    elif name == 'door_block':
        pass                                               # drawn by the door itself
    elif name == 'pillar':
        for y in range(16):
            for x in range(3, 13):
                col = s['light'] if x in (4, 5) else (s['dark'] if x in (11, 12) else s['base'])
                c.set(x, y, col)
            c.set(3, y, P['k']); c.set(12, y, P['k'])
    elif name == 'ledge':
        for x in range(16):
            for y in range(6):
                c.set(x, y, s['top_light'] if y == 0 else (s['top'] if y < 3 else s['dark']))
            c.set(x, 6, P['k'])
    elif name == 'grate':
        for y in range(16):
            for x in range(16):
                if x % 4 == 0 or y % 4 == 0:
                    c.set(x, y, s['light'] if (x + y) % 8 == 0 else s['dark'])
    elif name == 'moss':                                    # front decoration: drips / icicles / mold
        for x in (2, 7, 12):
            for y in range(4 + (x % 5)):
                c.set(x, y, s['accent'])
                c.set(x + 1, y, mix(s['accent'], P['k'], 0.4))
    return c


def bg_tile(style, name):
    s = STYLES[style]
    bg, bl = s['bg'], s['bg_light']
    c = Canvas(16, 16)
    def fill(variant=0):
        for y in range(16):
            for x in range(16):
                col = texture(style, x, y, variant)
                c.set(x, y, mix(col, bg, 0.72))
    if name in ('wall', 'wall2', 'wall3'):
        fill({'wall': 0, 'wall2': 1, 'wall3': 2}[name])
        if name == 'wall3':
            for x, y in ((4, 5), (5, 5), (5, 6), (10, 11), (11, 10)):
                c.set(x, y, mix(s['accent'], bg, 0.5))
    elif name == 'dark':
        c.rect(0, 0, 16, 16, mix(bg, P['k'], 0.5))
    elif name == 'window':                                  # a pointed window with moonlight
        fill()
        for y in range(2, 15):
            for x in range(4, 12):
                if y < 5 and abs(x - 7.5) > (y - 1) * 1.4:
                    continue
                c.set(x, y, (40, 50, 110) if (x + y) % 2 else (60, 70, 140))
        c.rect(7, 3, 2, 12, mix(bg, P['k'], 0.4))
        c.rect(4, 8, 8, 1, mix(bg, P['k'], 0.4))
    elif name == 'torch':                                   # a wall torch, flame and glow
        fill()
        for y in range(16):
            for x in range(16):
                d = ((x - 8) ** 2 + (y - 5) ** 2) ** 0.5
                if d < 7:
                    c.blend(x, y, P['H'], 0.18 * (1 - d / 7))
        c.rect(7, 8, 2, 6, P['N'])
        c.rect(6, 7, 4, 2, P['G'])
        c.ellipse(8, 5, 2, 3, P['f'])
        c.ellipse(8, 5.5, 1, 1.5, P['y'])
    elif name in ('arch_l', 'arch_r'):
        fill()
        for y in range(16):
            for x in range(16):
                xx = x if name == 'arch_l' else 15 - x
                d = ((xx - 16) ** 2 + (y - 16) ** 2) ** 0.5
                if d < 12:
                    c.set(x, y, mix(bg, P['k'], 0.55))
                elif d < 14:
                    c.set(x, y, bl)
    elif name in ('pillar_bg', 'column'):
        fill()
        for y in range(16):
            for x in range(4, 12):
                c.set(x, y, bl if x in (5, 6) else mix(bl, bg, 0.5))
    elif name == 'banner':                                  # a hanging banner in the Count's purple
        fill()
        c.rect(3, 0, 10, 13, (90, 30, 110))
        for x in range(3, 13):
            if abs(x - 7.5) < (16 - 13) + (x % 2):
                c.set(x, 13, (90, 30, 110))
        c.ellipse(8, 6, 2.5, 2.5, P['Y'])
    elif name == 'shelf':
        fill()
        c.rect(0, 10, 16, 2, P['N'])
        c.rect(2, 5, 3, 5, (120, 150, 190))
        c.rect(7, 6, 4, 4, (180, 120, 70))
        c.rect(12, 4, 2, 6, (150, 190, 120))
    elif name == 'chain':
        fill()
        for y in range(0, 16, 4):
            c.rect(7, y, 2, 3, P['G'])
            c.set(7, y + 3, P['D'])
    elif name == 'web':
        fill()
        for i in range(16):
            c.set(i, i, P['W'])
            c.set(i, 0, P['W'])
            c.set(0, i, P['W'])
            if i < 12:
                c.set(i, 11, mix(P['W'], bg, 0.5))
                c.set(11, i, mix(P['W'], bg, 0.5))
    elif name == 'skull':                                   # a skull-shaped bread roll
        fill()
        c.ellipse(8, 8, 5, 4.5, mix(P['t'], bg, 0.3))
        c.rect(5, 7, 2, 2, P['k'])
        c.rect(9, 7, 2, 2, P['k'])
        c.rect(6, 11, 4, 1, P['k'])
    elif name == 'candle':
        fill()
        c.rect(7, 8, 3, 6, P['e'])
        c.rect(6, 14, 5, 1, P['Y'])
        c.ellipse(8.5, 6, 1.2, 2, P['y'])
        for y in range(16):
            for x in range(16):
                d = ((x - 8.5) ** 2 + (y - 6) ** 2) ** 0.5
                if d < 6:
                    c.blend(x, y, P['H'], 0.12 * (1 - d / 6))
    elif name in ('sky', 'stars'):
        for y in range(16):
            for x in range(16):
                c.set(x, y, (14, 14, 40))
        if name == 'stars':
            for x, y in ((3, 4), (11, 9), (7, 13)):
                c.set(x, y, P['w'])
            c.set(12, 2, P['W'])
    elif name.startswith('moon'):
        for y in range(16):
            for x in range(16):
                c.set(x, y, (14, 14, 40))
        ox = 16 if name.endswith('l') else 0
        oy = 16 if name[5] == 't' else 0
        for y in range(16):
            for x in range(16):
                d = ((x - ox) ** 2 + (y - oy) ** 2) ** 0.5
                if d < 13:
                    c.set(x, y, (250, 240, 200) if noise(x, y, 41) > 0.15 else (220, 210, 170))
                elif d < 15:
                    c.blend(x, y, (250, 240, 200), 0.3)
    elif name == 'roof':
        for y in range(16):
            for x in range(16):
                c.set(x, y, (110, 40, 40) if (y % 4) else (70, 24, 30))
    elif name in ('house', 'house_win'):
        for y in range(16):
            for x in range(16):
                c.set(x, y, (150, 130, 110) if not (x in (0, 15) or y in (0, 7)) else (80, 56, 40))
        if name == 'house_win':
            c.rect(4, 3, 8, 8, (250, 200, 90))
            c.rect(7, 3, 2, 8, (80, 56, 40))
            c.rect(4, 6, 8, 2, (80, 56, 40))
    elif name in ('hill', 'hill_top'):
        for y in range(16):
            for x in range(16):
                if name == 'hill_top' and y < 6 + int(3 * noise(x, 0, 3)):
                    c.set(x, y, (14, 14, 40))
                else:
                    c.set(x, y, (24, 40, 50))
    elif name == 'fence':
        for x in (2, 8, 14):
            c.rect(x - 1, 4, 2, 12, P['N'])
        c.rect(0, 7, 16, 2, P['n'])
        c.rect(0, 12, 16, 2, P['n'])
    elif name in ('crate', 'barrel'):
        fill()
        if name == 'crate':
            c.rect(1, 3, 14, 13, P['N'])
            c.rect(2, 4, 12, 11, P['n'])
            c.line(2, 4, 13, 14, P['N'])
        else:
            c.ellipse(8, 9, 6, 7, P['n'])
            c.rect(2, 6, 12, 1, P['G'])
            c.rect(2, 12, 12, 1, P['G'])
    elif name in ('pipe_h', 'pipe_v'):
        fill()
        for i in range(16):
            for k in range(5, 11):
                col = s['light'] if k == 6 else (s['dark'] if k == 10 else s['base'])
                if name == 'pipe_h':
                    c.set(i, k, col)
                else:
                    c.set(k, i, col)
    else:
        fill()
    return c


def tileset(style):
    out = Canvas(256, 64)
    for m in range(16):
        out.paste(solid_tile(style, m), m * 16, 0)
    for i, v in enumerate(FILL_VARIANTS):
        out.paste(solid_tile(style, 15, i + 1), i * 16, 16)
    for i, (ch, name) in enumerate(SPECIAL):
        out.paste(special_tile(style, name), (3 + i) * 16, 16)
    for i, (ch, name) in enumerate(BG):
        out.paste(bg_tile(style, name), (i % 16) * 16, 32 + (i // 16) * 16)
    return out


def lava_frame(style, frame):
    """The tileset again with the molten tiles shifted: swapped back and forth, they bubble."""
    t = tileset(style)
    idx = 3 + [n for _, n in SPECIAL].index('lava')
    ox, oy = idx * 16, 16
    tile = Canvas(16, 16)
    for y in range(16):
        for x in range(16):
            tile.set(x, y, t.get(ox + (x + frame * 5) % 16, oy + y))
            if y < 2:
                tile.set(x, y, (252, 220, 72) if (x + frame * 3) % 6 else (255, 255, 200))
    t.paste(tile, ox, oy)
    return t
