#!/usr/bin/env python3
"""Asset converter for DELVE (Amiga AGA).

Palette (256 entries, AGA 24-bit):
    0..127   base colours
               - sprite/decor/UI colours
               - ENV range: colours of the structural tiles (walls, floors) kept on
                 their own indices so each biome can re-tint them via the palette
               - flash colours (white hit flash, red tint ramp)
               - 3 flame colours that are cycled at runtime (torch flicker)
    128..255 shadow bank = darkened copies of 0..127, computed at runtime.
             Setting bitplane 7 on any pixel shades it: used for baked wall shadows
             and real-time drop shadows under every character.

Tiles: 16x16, stored as interleaved 8-bitplane data (+ white/red flash variants and
cookie-cut masks). Extra tiles are generated here: mirrored creatures and weapons,
4-way weapon rotations, wall-cap/corner pieces for autotiling, torches, effects,
damage digits. Shadow masks are single-plane.
"""
import sys, os, colorsys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = lambda *p: os.path.join(ROOT, 'assets', *p)
G = lambda *p: os.path.join(ROOT, 'gen', *p)
TS, PLANES = 16, 8

sheet = Image.open(A('tiny_dungeon.png')).convert('RGBA')
px = sheet.load()
ntx, nty = sheet.width // TS, sheet.height // TS
NBASE = ntx * nty

# tiles whose colours follow the biome tint (walls, rock, floors, doorframes)
STRUCT = set([0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 24, 25, 26, 27, 9, 10, 11,
              21, 22, 23, 33, 34, 35, 36, 37, 38, 39, 40, 42, 48, 49, 50, 51, 52, 53,
              57, 58, 59, 6, 18, 30, 31, 43, 54, 55])

def rgb_tile(t):
    tx, ty = t % ntx, t // ntx
    return [[(px[tx * TS + x, ty * TS + y][:3] if px[tx * TS + x, ty * TS + y][3] >= 128 else None)
             for x in range(TS)] for y in range(TS)]

# ------------------------------------------------------------------ palette
pal = [(16, 12, 22)]                       # 0: void
def alloc(c):
    pal.append(c)
    return len(pal) - 1

sprite_idx, env_idx = {}, {}
for t in range(NBASE):
    if t in STRUCT: continue
    for row in rgb_tile(t):
        for c in row:
            if c is not None and c not in sprite_idx:
                sprite_idx[c] = alloc(c)
UI = {
    'white': (255, 255, 255), 'yellow': (255, 214, 64), 'red': (226, 52, 52),
    'green': (80, 210, 90), 'blue': (80, 150, 255), 'grey': (120, 120, 140),
    'darkred': (90, 20, 24), 'darkgreen': (20, 70, 30), 'hudbg': (34, 30, 46),
    'hudline': (110, 100, 140), 'purple': (170, 90, 230), 'orange': (255, 140, 40),
    'black': (0, 0, 0), 'smoke': (150, 146, 160), 'smoke2': (96, 92, 108),
}
ui_index = {k: alloc(c) for k, c in UI.items()}
FLASH_W = alloc((255, 255, 255))
FLASH_W2 = alloc((210, 214, 235))
RED_RAMP = [alloc(c) for c in ((90, 10, 16), (160, 24, 30), (225, 60, 50), (255, 140, 120))]
FLAME = [alloc(c) for c in ((255, 236, 120), (255, 150, 40), (220, 64, 24))]
ENV_START = len(pal)
for t in sorted(STRUCT):
    for row in rgb_tile(t):
        for c in row:
            if c is not None and c not in env_idx:
                env_idx[c] = alloc(c)
ENV_END = len(pal)
assert len(pal) <= 128, len(pal)
while len(pal) < 128: pal.append((0, 0, 0))

def lum(c): return (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]) / 255.0

# ------------------------------------------------------------------ biomes
# each biome re-tints the ENV colours (walls/floors); 0 = original sandstone
def tint(c, h_shift=0.0, s_mul=1.0, v_mul=1.0, blend=None):
    r, g, b = [v / 255 for v in c]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    h = (h + h_shift) % 1.0
    s = min(1, s * s_mul); v = min(1, v * v_mul)
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    if blend:
        bc, f = blend
        r = r * (1 - f) + bc[0] / 255 * f; g = g * (1 - f) + bc[1] / 255 * f; b = b * (1 - f) + bc[2] / 255 * f
    return (int(r * 255), int(g * 255), int(b * 255))

def material(c):
    h, sat, v = colorsys.rgb_to_hsv(*[x / 255 for x in c])
    if c[2] >= c[0]: return 'stone'          # blue-grey brick / caps
    if v < 0.55: return 'rock'                # dark brown rock mass
    return 'floor'                            # sand

def recolor(c, h=None, s_mul=1.0, v_mul=1.0, blend=None):
    r, g, b = [x / 255 for x in c]
    hh, ss, vv = colorsys.rgb_to_hsv(r, g, b)
    if h is not None: hh = h
    r, g, b = colorsys.hsv_to_rgb(hh, min(1, ss * s_mul), min(1, vv * v_mul))
    if blend:
        bc, f = blend
        r, g, b = [r * (1 - f) + bc[0] / 255 * f, g * (1 - f) + bc[1] / 255 * f, b * (1 - f) + bc[2] / 255 * f]
    return (int(r * 255), int(g * 255), int(b * 255))

# per biome: (floor, rock, stone) recolour parameters
BIOMES = [
    ('SANDSTONE HALLS', {}, {}, {}),
    ('GREY CRYPTS',  dict(h=0.62, s_mul=0.18, v_mul=0.62), dict(h=0.65, s_mul=0.35, v_mul=0.55), dict(v_mul=0.92)),
    ('MOSSY CAVERNS', dict(h=0.24, s_mul=0.55, v_mul=0.62), dict(h=0.20, s_mul=0.7, v_mul=0.62), dict(h=0.30, s_mul=0.8, v_mul=0.85)),
    ('FROZEN DEPTHS', dict(h=0.56, s_mul=0.38, v_mul=1.0, blend=((230, 240, 255), 0.2)),
                      dict(h=0.62, s_mul=0.9, v_mul=0.75), dict(h=0.58, s_mul=1.2, v_mul=1.08)),
    ('INFERNAL FORGE', dict(h=0.015, s_mul=0.85, v_mul=0.52), dict(h=0.0, s_mul=1.1, v_mul=0.42),
                       dict(h=0.03, s_mul=0.6, v_mul=0.62)),
    ('VOID SANCTUM', dict(h=0.76, s_mul=0.35, v_mul=0.5), dict(h=0.78, s_mul=0.8, v_mul=0.45), dict(h=0.74, s_mul=1.4, v_mul=0.85)),
]
env_list = pal[ENV_START:ENV_END]
biome_pal = [[recolor(c, **{'floor': fl, 'rock': rk, 'stone': st}[material(c)]) for c in env_list]
             for _, fl, rk, st in BIOMES]

# ------------------------------------------------------------------ tiles (index space)
def idx_tile(t):
    m = env_idx if t in STRUCT else sprite_idx
    return [[None if c is None else m[c] for c in row] for row in rgb_tile(t)]

tiles = [idx_tile(t) for t in range(NBASE)]
is_struct = [t in STRUCT for t in range(NBASE)]
def add(p, struct=False):
    tiles.append(p); is_struct.append(struct)
    return len(tiles) - 1

hflip = lambda p: [list(reversed(r)) for r in p]
vflip = lambda p: [list(r) for r in reversed(p)]
rot_cw = lambda p: [[p[TS - 1 - x][y] for x in range(TS)] for y in range(TS)]

extra = {}
CREATURES = [84, 85, 86, 87, 88, 92, 96, 97, 98, 99, 100,
             108, 109, 110, 111, 112, 120, 121, 122, 123, 124]
WEAPONS = [103, 104, 105, 106, 107, 117, 118, 119, 129, 130, 131]
tile_flip = [0] * NBASE
weapon_dir = [[0, 0, 0, 0] for _ in range(NBASE)]
for t in CREATURES + WEAPONS:
    tile_flip[t] = add(hflip(tiles[t]))
for t in WEAPONS:
    up = tiles[t]; r = rot_cw(up); d = rot_cw(r); l = rot_cw(d)
    weapon_dir[t] = [t, add(r), add(d), add(l)]

# wall autotile pieces (derived from Tiny Dungeon's own cap tiles)
extra['CAP_N'] = add(vflip(tiles[2]), True)
extra['CAP_NW'] = add(vflip(tiles[16]), True)
extra['CAP_NE'] = add(vflip(tiles[17]), True)
extra['IN_SW'] = add(hflip(tiles[1]), True)
extra['IN_NE'] = add(vflip(tiles[1]), True)
extra['IN_NW'] = add(vflip(hflip(tiles[1])), True)
# cap on both top and bottom (1-tall rock)
ns = [list(r) for r in vflip(tiles[2])[:8]] + [list(r) for r in tiles[2][8:]]
extra['CAP_NS'] = add(ns, True)

def blank(): return [[None] * TS for _ in range(TS)]
def U(n): return ui_index[n]

# wall torch on brick: sconce + flame using the cycling FLAME colours
torch = [list(r) for r in tiles[40]]
for y in range(9, 13):
    for x in (6, 7, 8, 9):
        torch[y][x] = U('black') if y == 12 or x in (6, 9) else sprite_idx.get((90, 64, 50), U('grey'))
for y in range(7, 10):
    torch[y][7] = torch[y][8] = U('grey')
flame = {(7, 1): 0, (8, 2): 0, (6, 3): 1, (7, 3): 0, (8, 3): 0, (9, 3): 1,
         (6, 4): 1, (7, 4): 0, (8, 4): 1, (9, 4): 2, (6, 5): 2, (7, 5): 1, (8, 5): 1, (9, 5): 2,
         (7, 6): 2, (8, 6): 2}
for (x, y), k in flame.items():
    torch[y][x] = FLAME[k]
extra['TORCH'] = add(torch, True)

def disc(outer, inner, ro, ri, cx=7.5, cy=7.5):
    p = blank()
    for y in range(TS):
        for x in range(TS):
            d = (x - cx) ** 2 + (y - cy) ** 2
            if d <= ri * ri: p[y][x] = U(inner)
            elif d <= ro * ro: p[y][x] = U(outer)
    return p

extra['ORB'] = add(disc('purple', 'white', 4.2, 2.0))
extra['FIREBALL'] = add(disc('orange', 'yellow', 4.2, 2.2))
extra['BOLT'] = add(disc('blue', 'white', 3.5, 1.5))
ar = blank()
for y in range(3, 14): ar[y][7] = U('orange')
for y, xs in ((2, (7,)), (3, (6, 8)), (4, (5, 9))):
    for x in xs: ar[y][x] = U('grey')
ar[2][7] = U('white')
for y, xs in ((13, (6, 8)), (14, (5, 9))):
    for x in xs: ar[y][x] = U('white')
extra['ARROW_U'] = add(ar)
extra['ARROW_R'] = add(rot_cw(ar))
extra['ARROW_D'] = add(rot_cw(rot_cw(ar)))
extra['ARROW_L'] = add(rot_cw(rot_cw(rot_cw(ar))))
hl = blank()
for i in range(4, 12):
    hl[i][7] = hl[i][8] = hl[7][i] = hl[8][i] = U('green')
hl[7][7] = hl[7][8] = hl[8][7] = hl[8][8] = U('white')
extra['HEAL'] = add(hl)
HEART = ["................", "................", "................",
         "...RRR...RRR....", "..RRWRR.RRRRR...", "..RWRRRRRRRRR...",
         "..RRRRRRRRRRR...", "..RRRRRRRRRRR...", "...RRRRRRRRR....",
         "....RRRRRRR.....", ".....RRRRR......", "......RRR.......",
         ".......R........", "................", "................", "................"]
extra['HEART'] = add([[None if ch == '.' else U('white' if ch == 'W' else 'red') for ch in row] for row in HEART])
KO = ["................", "................", "................",
      ".....WWWWWW.....", "....WWWWWWWW....", "....WW..WW.W....",
      "....W...W...W...", "....WWWWWWWWW...", ".....WWW.WWW....",
      "......WWWWW.....", "......W.W.W.....", "................",
      "................", "................", "................", "................"]
extra['KO'] = add([[None if ch == '.' else U('grey') for ch in row] for row in KO])

# hit spark (2 frames)
for f, (r1, r2) in enumerate(((2, 5), (4, 7))):
    p = blank()
    for i in range(r1, r2 + 1):
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            p[8 + dy * i][8 + dx * i] = U('white') if i < r2 else U('yellow')
        if i < r2 - 1:
            for dx, dy in ((1, 1), (-1, -1), (1, -1), (-1, 1)):
                p[8 + dy * (i - 1)][8 + dx * (i - 1)] = U('yellow')
    p[8][8] = U('white')
    extra['SPARK%d' % f] = add(p)
# smoke puff (3 frames)
import math
for f, rad in enumerate((3.0, 5.0, 6.5)):
    p = blank()
    for k in range(5):
        a = k * 2 * math.pi / 5 + f
        cx, cy = 7.5 + math.cos(a) * rad * 0.6, 8.5 + math.sin(a) * rad * 0.5 - f
        rr = rad * 0.55
        for y in range(TS):
            for x in range(TS):
                if (x - cx) ** 2 + (y - cy) ** 2 <= rr * rr:
                    edge = (x - cx) ** 2 + (y - cy) ** 2 > (rr - 1.2) ** 2
                    if f == 2 and (x + y) % 2: continue
                    p[y][x] = U('smoke2') if edge else U('smoke')
    extra['SMOKE%d' % f] = add(p)
# damage digits 0-9 (3x5, white with dark outline), centred in the tile
DIG = ["111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
       "111100111001111", "111100111101111", "111001010010010", "111101111101111", "111101111001111"]
for d, bits in enumerate(DIG):
    p = blank()
    for y in range(5):
        for x in range(3):
            if bits[y * 3 + x] == '1':
                for oy in (-1, 0, 1):
                    for ox in (-1, 0, 1):
                        X, Y = 6 + x + ox, 5 + y + oy
                        if p[Y][X] is None: p[Y][X] = U('black')
    for y in range(5):
        for x in range(3):
            if bits[y * 3 + x] == '1': p[5 + y][6 + x] = U('white')
    t = add(p)
    if d == 0: extra['DIGIT0'] = t
# sparkle (item twinkle)
tw = blank()
for i in range(-3, 4):
    tw[8][8 + i] = tw[8 + i][8] = U('white') if abs(i) < 2 else U('yellow')
extra['TWINKLE'] = add(tw)

# ------------------------------------------------------------------ animation frames
# Tiny Dungeon sprites are single frames; build animation frames from them.
def bbox(p):
    ys = [y for y in range(TS) if any(c is not None for c in p[y])]
    xs = [x for x in range(TS) if any(p[y][x] is not None for y in range(TS))]
    return (min(xs), min(ys), max(xs), max(ys)) if ys else (0, 0, 15, 15)

def copyp(p): return [list(r) for r in p]

def feet(p, left):
    """lift one foot (left or right half of the bottom rows) by a pixel"""
    x0, y0, x1, y1 = bbox(p)
    cx = (x0 + x1 + 1) // 2
    o = copyp(p)
    cols = range(x0, cx) if left else range(cx, x1 + 1)
    for y in range(y1 - 3, y1 + 1):
        for x in cols:
            o[y - 1][x] = p[y][x] if p[y][x] is not None else o[y - 1][x] if y - 1 < y1 - 3 else None
    for x in cols: o[y1][x] = None
    return o

def breathe(p):
    """head and shoulders dip one pixel"""
    x0, y0, x1, y1 = bbox(p)
    mid = (y0 + y1) // 2
    o = copyp(p)
    for y in range(mid, y0, -1): o[y] = list(p[y - 1])
    o[y0] = [None] * TS
    return o

def lean(p):
    """lean forward (to the right)"""
    x0, y0, x1, y1 = bbox(p)
    mid = (y0 + y1) // 2
    o = [[None] * TS for _ in range(TS)]
    for y in range(TS):
        sh = 2 if y < y0 + 3 else 1 if y <= mid else 0
        for x in range(TS):
            if p[y][x] is not None and x + sh < TS: o[y][x + sh] = p[y][x]
    return o

def scale(p, sx, sy):
    """nearest-neighbour scale anchored bottom-centre"""
    x0, y0, x1, y1 = bbox(p)
    cx = (x0 + x1) / 2.0
    o = [[None] * TS for _ in range(TS)]
    for y in range(TS):
        for x in range(TS):
            srcx = int(round(cx + (x - cx) / sx))
            srcy = int(round(y1 - (y1 - y) / sy))
            if 0 <= srcx < TS and 0 <= srcy < TS and srcy >= y0 - 1: o[y][x] = p[srcy][srcx]
    return o

def wave(p, ph):
    o = [[None] * TS for _ in range(TS)]
    for y in range(TS):
        sh = int(round(math.sin(y / 2.5 + ph)))
        for x in range(TS):
            if p[y][x] is not None and 0 <= x + sh < TS: o[y][x + sh] = p[y][x]
    return o

def skitter(p, d):
    x0, y0, x1, y1 = bbox(p)
    mid = (y0 + y1) // 2
    o = copyp(p)
    for y in range(mid, y1 + 1):
        o[y] = [None] * TS
        for x in range(TS):
            if p[y][x] is not None and 0 <= x + d < TS: o[y][x + d] = p[y][x]
    return o

# frame order: WALK_A, WALK_B, BREATHE, ATTACK, SQUASH1, SQUASH2
def frames_for(t, p):
    if t == 120:                                        # bat: flap
        a, b = scale(p, 1.15, 0.7), scale(p, 0.9, 1.0)
    elif t in (121,):                                   # ghost: waver
        a, b = wave(p, 0), wave(p, math.pi)
    elif t == 108:                                      # slime: squish
        a, b = scale(p, 1.15, 0.8), scale(p, 0.92, 1.0)
    elif t == 122:                                      # spider: skitter
        a, b = skitter(p, -1), skitter(p, 1)
    else:
        a, b = feet(p, True), feet(p, False)
    return [a, b, breathe(p), lean(p), scale(p, 1.18, 0.72), scale(p, 1.35, 0.42)]

NFRAMES = 6
anim_slot = [255] * NBASE
anim_tab = []
for t in CREATURES:
    anim_slot[t] = len(anim_tab)
    right = [add(f) for f in frames_for(t, tiles[t])]
    left = [add(hflip(tiles[f])) for f in right]
    anim_tab.append((right, left))

# 45-degree weapon rotations for slashing arcs (8 directions: U UR R DR D DL L UL)
inv_sprite = {v: k for k, v in sprite_idx.items()}
def rot_rgb(t, ang):
    tx, ty = t % ntx, t // ntx
    im = sheet.crop((tx * TS, ty * TS, tx * TS + TS, ty * TS + TS)).rotate(ang, resample=Image.NEAREST)
    out = []
    for y in range(TS):
        row = []
        for x in range(TS):
            r, g, b, a = im.getpixel((x, y))
            row.append(sprite_idx.get((r, g, b)) if a >= 128 else None)
        out.append(row)
    return out
weapon8 = [[0] * 8 for _ in range(NBASE)]
for t in WEAPONS:
    u, r, d, l = weapon_dir[t]
    weapon8[t] = [u, add(rot_rgb(t, -45)), r, add(rot_rgb(t, -135)), d, add(rot_rgb(t, 135)), l, add(rot_rgb(t, 45))]

# pulsing projectile frames
extra['ORB2'] = add(disc('purple', 'white', 3.3, 1.6))
extra['FIREBALL2'] = add(disc('red', 'orange', 4.8, 2.6))
extra['BOLT2'] = add(disc('white', 'blue', 2.8, 1.2))

NT = len(tiles)
assert NT <= 1024, NT

# ------------------------------------------------------------------ flash variants
def white_var(c):
    return FLASH_W if lum(pal[c]) > 0.35 else FLASH_W2
def red_var(c):
    l = lum(pal[c])
    return RED_RAMP[min(3, int(l * 4.2))]

def planar(pix, remap=None):
    words = []
    for y in range(TS):
        for p in range(PLANES):
            w = 0
            for x in range(TS):
                c = pix[y][x]
                if c is None: continue
                if remap: c = remap(c)
                if (c >> p) & 1: w |= 0x8000 >> x
            words.append(w)
    return words

def mask(pix):
    words = []
    for y in range(TS):
        w = 0
        for x in range(TS):
            if pix[y][x] is not None: w |= 0x8000 >> x
        words += [w] * PLANES
    return words

gfx, gfxw, gfxr, msk = [], [], [], []
for t in range(NT):
    p = tiles[t]
    gfx += planar([[0 if c is None else c for c in row] for row in p])
    gfxw += planar(p, white_var)
    gfxr += planar(p, red_var)
    msk += mask(p)

# ------------------------------------------------------------------ shadow masks (1 plane)
def smask(fn):
    out = []
    for y in range(TS):
        w = 0
        for x in range(TS):
            if fn(x, y): w |= 0x8000 >> x
        out.append(w)
    return out
SHADOWS = {
    'TOP':  smask(lambda x, y: y < 3 or (y < 6 and (x + y) % 2 == 0)),
    'LEFT': smask(lambda x, y: x < 2 or (x < 4 and (x + y) % 2 == 0)),
    'TL':   smask(lambda x, y: y < 3 or x < 2 or ((y < 6 or x < 4) and (x + y) % 2 == 0)),
    'DROP': smask(lambda x, y: 12 <= y <= 15 and ((x - 7.5) / 6.5) ** 2 + ((y - 13.5) / 2.2) ** 2 <= 1.0
                  and not (y == 15 and (x + y) % 2)),
    'DOT':  smask(lambda x, y: (x + y) % 2 == 0),
}

# ------------------------------------------------------------------ font
font = ImageFont.truetype(A('font.ttf'), 8)
glyphs = []
for ch in range(32, 128):
    im = Image.new('L', (8, 8), 0)
    ImageDraw.Draw(im).text((1, -1), chr(ch), font=font, fill=255)
    for y in range(8):
        b = 0
        for x in range(8):
            if im.getpixel((x, y)) > 110: b |= 0x80 >> x
        glyphs.append(b)

# ------------------------------------------------------------------ sounds
SFX = ['swing', 'hit', 'hurt', 'coin', 'potion', 'door', 'chest', 'die',
       'magic', 'select', 'floor', 'levelup', 'gameover',
       'v_prepare', 'v_fight', 'v_gameover', 'v_levelup', 'v_powerup']

# ------------------------------------------------------------------ music
# Two-voice songs (lead + bass). Tokens: note name (A4, C#5, Bb2), '.' = hold, '-' = rest.
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
def parse(txt):
    out = []
    for tok in txt.split():
        if tok == '.': out.append(0)
        elif tok == '-': out.append(1)
        else:
            n = NOTE[tok[0]]; i = 1
            if tok[i] == '#': n += 1; i += 1
            elif tok[i] == 'b': n -= 1; i += 1
            out.append(12 * (int(tok[i:]) + 1) + n)
    return out

def bars(*chords):   # bass helper: chord root notes -> 8-step root/fifth patterns
    out = []
    for root, pat in chords:
        out.append(pat.replace('R', root[0]).replace('r', root[1]))
    return ' '.join(out)

DUNGEON_LEAD = '''
A4 . C5 . E5 . D5 C5   B4 . . . A4 . G4 .   F4 . A4 . C5 . B4 A4   G4 . . . E4 . - .
G4 . B4 . D5 . C5 B4   A4 . . . B4 . C5 .   B4 . G#4 . E4 . F4 G#4  A4 . . . - . - .
E5 . . D5 C5 . B4 .   C5 . A4 . . . - .   F5 . . E5 D5 . C5 .   B4 . G4 . . . - .
C5 . D5 . E5 . F5 .   E5 . D5 . C5 . B4 .   A4 . B4 . C5 . G#4 .   A4 . . . - . - .'''
B_AM = 'A2 . A2 E3 A2 . E3 A2'; B_F = 'F2 . F2 C3 F2 . C3 F2'; B_G = 'G2 . G2 D3 G2 . D3 G2'; B_E = 'E2 . E2 B2 E2 . G#2 B2'
DUNGEON_BASS = ' '.join([B_AM, B_AM, B_F, B_F, B_G, B_G, B_E, B_E] * 2)

BOSS_LEAD = '''
D5 - D5 - F5 . E5 D5   C5 . A4 . . . - .   D5 - D5 - F5 . G5 F5   E5 . C#5 . . . - .
Bb4 . D5 . F5 . Bb5 .   A5 . . . G5 . F5 .   E5 . F5 . G5 . E5 .   C#5 . . . A4 . . .'''
BOSS_BASS = ' '.join(['D2 D3 D2 D3 D2 D3 C3 D3'] * 2 + ['Bb1 Bb2 Bb1 Bb2 C2 C3 C2 C3', 'A1 A2 A1 A2 A1 A2 C#3 E3'] +
                     ['Bb1 Bb2 Bb1 Bb2 Bb1 Bb2 Bb1 Bb2', 'F2 F3 F2 F3 F2 F3 F2 F3', 'G2 G3 G2 G3 C2 C3 C2 C3', 'A1 A2 A1 A2 A1 A2 A1 A2'])

CAMP_LEAD = '''
E5 . . . D5 . C5 .   D5 . . . . . - .   C5 . . . B4 . A4 .   G4 . . . . . - .
A4 . . . B4 . C5 .   D5 . . . E5 . D5 .   C5 . . . B4 . G4 .   C5 . . . . . - .'''
CAMP_BASS = ' '.join(['C3 . G3 . E3 . G3 .', 'G2 . D3 . B2 . D3 .', 'A2 . E3 . C3 . E3 .', 'E2 . B2 . G2 . B2 .',
                      'F2 . C3 . A2 . C3 .', 'G2 . D3 . B2 . D3 .', 'A2 . E3 . F2 . C3 .', 'C3 . G3 . C3 . - .'])

# (name, lead, bass, ticks per step, lead wave, bass wave)
SONGS = [('TITLE', CAMP_LEAD, CAMP_BASS, 9, 3, 1),
         ('DUNGEON', DUNGEON_LEAD, DUNGEON_BASS, 7, 0, 1),
         ('BOSS', BOSS_LEAD, BOSS_BASS, 5, 2, 2),
         ('CAMP', CAMP_LEAD, CAMP_BASS, 11, 3, 1)]
song_data = []
for name, lead, bass, tempo, lw, bw in SONGS:
    L, B = parse(lead), parse(bass)
    assert len(L) == len(B), (name, len(L), len(B))
    song_data.append((name, L, B, tempo, lw, bw))

# single-cycle waveforms, 32 samples (and 16-sample copies for high notes)
def wavegen(kind, n):
    out = []
    for i in range(n):
        ph = i / n
        if kind == 0: v = 70 if ph < 0.25 else -70                       # 25% pulse
        elif kind == 1: v = int(110 * (4 * ph - 1 if ph < 0.5 else 3 - 4 * ph))  # triangle
        elif kind == 2: v = int(90 * (2 * ph - 1))                       # saw
        else: v = int(90 * math.sin(2 * math.pi * ph) + 20 * math.sin(6 * math.pi * ph))  # soft
        out.append(max(-127, min(127, v)))
    return out
WAVES = [wavegen(k, 32) + wavegen(k, 16) for k in range(4)]
PAL_CLOCK = 3546895
note_period, note_short = [], []
for n in range(128):
    f = 440.0 * 2 ** ((n - 69) / 12.0)
    per = PAL_CLOCK / (f * 32)
    short = 0
    if per < 124: per = PAL_CLOCK / (f * 16); short = 1
    note_period.append(max(124, min(65535, int(round(per))))); note_short.append(short)
sfx = []
for n in SFX:
    data = bytearray(open(A('sfx', n + '.raw'), 'rb').read())
    if len(data) & 1: data.append(0)
    data[0:2] = b'\0\0'
    sfx.append(data)

# ------------------------------------------------------------------ emit
def warr(name, words, chip=True, per=12):
    s = ('__chip ' if chip else '') + 'const UWORD %s[%d] = {\n' % (name, len(words))
    for i in range(0, len(words), per):
        s += '  ' + ','.join('0x%04x' % w for w in words[i:i + per]) + ',\n'
    return s + '};\n'
hexc = lambda c: '0x%06x' % ((c[0] << 16) | (c[1] << 8) | c[2])

with open(G('assets.c'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#include "assets.h"\n\n')
    f.write('const ULONG pal_rgb[128] = {\n')
    for i in range(0, 128, 8):
        f.write('  ' + ','.join(hexc(c) for c in pal[i:i + 8]) + ',\n')
    f.write('};\n')
    f.write('const ULONG biome_pal[NUM_BIOMES][ENV_COLS] = {\n')
    for b in biome_pal:
        f.write('  {' + ','.join(hexc(c) for c in b) + '},\n')
    f.write('};\n')
    f.write('const char *const biome_name[NUM_BIOMES] = {%s};\n' % ','.join('"%s"' % b[0] for b in BIOMES))
    f.write(warr('tile_gfx', gfx))
    f.write(warr('tile_gfx_white', gfxw))
    f.write(warr('tile_gfx_red', gfxr))
    f.write(warr('tile_mask', msk))
    f.write(warr('shadow_masks', sum(SHADOWS.values(), [])))
    f.write('const UBYTE font8[%d] = {\n' % len(glyphs))
    for i in range(0, len(glyphs), 16):
        f.write('  ' + ','.join('0x%02x' % b for b in glyphs[i:i + 16]) + ',\n')
    f.write('};\n')
    for n, d in zip(SFX, sfx):
        f.write('__chip const BYTE sfx_%s[%d] = {\n' % (n, len(d)))
        sd = [b - 256 if b > 127 else b for b in d]
        for i in range(0, len(sd), 24):
            f.write('  ' + ','.join(str(v) for v in sd[i:i + 24]) + ',\n')
        f.write('};\n')
    f.write('const UWORD tile_flip[%d] = {%s};\n' % (NBASE, ','.join(map(str, tile_flip))))
    f.write('const UWORD weapon_dir[%d][4] = {%s};\n' % (
        NBASE, ','.join('{%s}' % ','.join(map(str, w)) for w in weapon_dir)))
    f.write('const UWORD weapon8[%d][8] = {%s};\n' % (
        NBASE, ','.join('{%s}' % ','.join(map(str, w)) for w in weapon8)))
    f.write('const UBYTE anim_slot[%d] = {%s};\n' % (NBASE, ','.join(map(str, anim_slot))))
    f.write('const UWORD anim_tab[%d][2][%d] = {%s};\n' % (len(anim_tab), NFRAMES,
        ','.join('{{%s},{%s}}' % (','.join(map(str, r)), ','.join(map(str, l))) for r, l in anim_tab)))
    for i, w in enumerate(WAVES):
        f.write('__chip const BYTE wave%d[48] = {%s};\n' % (i, ','.join(map(str, w))))
    f.write('const BYTE *const waves[4] = {wave0, wave1, wave2, wave3};\n')
    f.write('const UWORD note_period[128] = {%s};\n' % ','.join(map(str, note_period)))
    f.write('const UBYTE note_short[128] = {%s};\n' % ','.join(map(str, note_short)))
    for name, L, B, tempo, lw, bw in song_data:
        f.write('static const UBYTE song_%s_lead[%d] = {%s};\n' % (name.lower(), len(L), ','.join(map(str, L))))
        f.write('static const UBYTE song_%s_bass[%d] = {%s};\n' % (name.lower(), len(B), ','.join(map(str, B))))
    f.write('const SongDef songs[NUM_SONGS] = {\n')
    for name, L, B, tempo, lw, bw in song_data:
        f.write('  { song_%s_lead, song_%s_bass, %d, %d, %d, %d },\n' % (name.lower(), name.lower(), len(L), tempo, lw, bw))
    f.write('};\n')
    f.write('const SfxDef sfx_table[SFX_COUNT] = {\n')
    for n, d in zip(SFX, sfx):
        f.write('  { sfx_%s, %d },\n' % (n, len(d) // 2))
    f.write('};\n')

with open(G('assets.h'), 'w') as f:
    f.write('/* generated by tools/convert.py - do not edit */\n#ifndef ASSETS_H\n#define ASSETS_H\n')
    f.write('#include <exec/types.h>\n\n')
    f.write('#define NUM_TILES %d\n#define NUM_BASE_TILES %d\n#define TILE_WORDS %d\n' % (NT, NBASE, TS * PLANES))
    for k, v in extra.items():
        f.write('#define T_%s %d\n' % (k, v))
    for k, v in ui_index.items():
        f.write('#define COL_%s %d\n' % (k.upper(), v))
    f.write('#define COL_VOID 0\n#define COL_FLAME0 %d\n' % FLAME[0])
    f.write('#define ENV_START %d\n#define ENV_COLS %d\n#define NUM_BIOMES %d\n' % (ENV_START, ENV_END - ENV_START, len(BIOMES)))
    for i, k in enumerate(SHADOWS):
        f.write('#define SH_%s %d\n' % (k, i))
    f.write('\nextern const ULONG pal_rgb[128];\n')
    f.write('extern const ULONG biome_pal[NUM_BIOMES][ENV_COLS];\n')
    f.write('extern const char *const biome_name[NUM_BIOMES];\n')
    f.write('extern __chip const UWORD tile_gfx[], tile_gfx_white[], tile_gfx_red[], tile_mask[], shadow_masks[];\n')
    f.write('extern const UBYTE font8[];\n')
    f.write('extern const UWORD tile_flip[%d];\nextern const UWORD weapon_dir[%d][4];\n' % (NBASE, NBASE))
    f.write('extern const UWORD weapon8[%d][8];\nextern const UBYTE anim_slot[%d];\n' % (NBASE, NBASE))
    f.write('extern const UWORD anim_tab[%d][2][%d];\n' % (len(anim_tab), NFRAMES))
    f.write('enum { AN_WALK_A, AN_WALK_B, AN_BREATHE, AN_ATTACK, AN_SQUASH1, AN_SQUASH2 };\n\n')
    f.write('typedef struct { const BYTE *data; UWORD words; } SfxDef;\n')
    f.write('enum {\n' + ''.join('  SFX_%s,\n' % n.upper() for n in SFX) + '  SFX_COUNT\n};\n')
    f.write('extern const SfxDef sfx_table[SFX_COUNT];\n')
    f.write('typedef struct { const UBYTE *lead, *bass; UWORD len, tempo; UBYTE lead_wave, bass_wave; } SongDef;\n')
    f.write('enum {' + ''.join(' SONG_%s,' % n for n, *_ in song_data) + ' NUM_SONGS };\n')
    f.write('extern const SongDef songs[NUM_SONGS];\nextern const BYTE *const waves[4];\n')
    f.write('extern const UWORD note_period[128];\nextern const UBYTE note_short[128];\n#endif\n')

print('palette used: %d (env %d..%d), tiles: %d' % (ENV_END, ENV_START, ENV_END - 1, NT))
