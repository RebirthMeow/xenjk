"""Build xenjk_showcase: an original model-showcase map made only from stock JKA assets and our own.

Two areas, joined by teleporter pads:
- deck: a floating Cloud City viewing deck under the stock Bespin sunset sky. Low warm sun (key) from yaw SUN_YAW,
  sky fill, amber pylon strips behind the stage (kicker), light neutral stage floor.
- studio: a grey cyclorama with three-point lights (key, fill, two rims), sealed below the deck and out of its view.
- ruins: the hero stage and spawn, a pale overgrown temple terrace above the Yavin canopy under its own sky; the
  subject faces away from the sun (rim) with the bright sky and pale stone in front (key). Pads to the others.
- cornell: the Cornell box (red and green side walls, white elsewhere, a ceiling light panel, two blocks), below the
  studio; reached from the deck's second pad.

Writes the .map, shader, textures, arena and cfg into a build folder, compiles with q3map2 (bsp, vis, light) and packs
zz_xenjk_showcase.pk3. Nothing from third-party maps is used (rgoer_duel2's licence forbids derivatives).

    python tools/showcase_map/build_showcase_map.py [--out DIR] [--fast]

Environment: Q3MAP2 (required: the q3map2.exe of netradiant-custom 20260114).
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
GAME = ROOT / 'game_directory' / 'Jedi Academy' / 'GameData'
Q3MAP2 = Path(os.environ['Q3MAP2']) if os.environ.get('Q3MAP2') else None   # q3map2.exe (netradiant-custom 20260114)
NAME = 'xenjk_showcase'
TEX = 'xenjk_showcase'

# Lighting: one place to tune. Yaw 0 = +x; the stage spawn faces +x, so the sun is the model's front-left key.
SUN_YAW, SUN_ELEV = 25, 30   # 26 (was 16): front and back of the model both read; a 16 degree sun left the back black
SUN_RGB = (1.0, 0.72, 0.48)
SUN_BAKE = 480           # q3map_sunExt intensity (lightmaps, light grid)
SUN_LIVE = 240           # q3gl2_sun intensity (rd-vulkan/rend2 live sun with cascaded shadows)
MAP_LIGHT_SCALE = 0.75   # q3gl2_sun: scales the baked light so the live sun is not counted twice in full
SUN_SHADOW_SCALE = 0.5   # q3gl2_sun: light kept in the live sun's shadow
SKY_LIGHT = 500          # q3map_skylight fill (was 160)
DECK_FILL = 2600         # soft cool fill on the stage's shadow side (behind-right of the model), 0 = none
AMBER = 5000             # pylon strip surface light (kicker/rim from behind; was 2600)
AMBIENT = 28             # worldspawn _ambient: flat fill in lightmaps and the light grid, so the model's shadow side reads

DECK_R, STAGE_R, RAIL_R = 640, 192, 624
STAGE_TOP = 8
STUDIO_Z = -1600         # studio floor, below the deck; its outside faces are sky, so the deck never sees it
STUDIO_H = 640
STUDIO_HALF = 640
CORNELL_Z = -2900        # Cornell box floor, below the studio (hidden from the deck behind it)
CORNELL_HALF = 288       # inner cube: 576 on a side
CORNELL_LIGHT = 12000    # ceiling panel q3map_surfacelight (48-unit pieces: with -fast, 16-unit pieces under ~20000
                         # each fell below the light cutoff before reaching the walls)
RUINS_Z = -4500          # the ruins terrace top, below the Cornell box, under its own Yavin sky
RUINS_R = 600            # terrace radius
RUINS_SKYLIGHT = 900     # q3map_skylight: a bright soft sky, so a black outfit reads from the camera side
RUINS_SUN_BAKE = 260     # q3map_sunExt in the ruins (same direction as the live sun)
RUINS_FACING = SUN_YAW + 180   # the subject faces away from the sun: the sun is her rim, the sky her key
SKY_BOTTOM = -5400       # the sky shell's floor, below the ruins


# ---------------------------------------------------------------- brushes

def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(a):
    l = math.sqrt(sum(c * c for c in a))
    return tuple(c / l for c in a)


def _plane_points(n, p, s=128.0):
    """Three points whose q3map2 plane (normal = (p2 - p0) x (p1 - p0)) faces outward along n."""
    a = (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)
    u = _norm(_cross(a, n))
    v = _cross(n, u)
    p1 = tuple(p[i] + v[i] * s for i in range(3))
    p2 = tuple(p[i] + u[i] * s for i in range(3))
    return p, p1, p2


def _fmt(p):
    return '( %s )' % ' '.join(('%.6f' % c).rstrip('0').rstrip('.') or '0' for c in p)


def brush(planes):
    """planes: [(normal, point_on_plane, texture, scale)]"""
    out = ['{']
    for n, p, tex, scale in planes:
        p0, p1, p2 = _plane_points(_norm(n), p)
        out.append('%s %s %s %s 0 0 0 %g %g 0 0 0' % (_fmt(p0), _fmt(p1), _fmt(p2), tex, scale, scale))
    out.append('}')
    return '\n'.join(out)


def prism(poly, lo, hi, tex, axis='z'):
    """Convex prism: poly is a CCW 2D polygon in the plane across `axis`, extruded from lo to hi along it.
    tex: a texture name, or a dict with 'top', 'bottom', 'side' (and an optional 'scale')."""
    if isinstance(tex, str):
        tex = {'top': tex, 'bottom': tex, 'side': tex}
    scale = tex.get('scale', 0.5)
    to3 = {'z': lambda a, b, c: (a, b, c), 'y': lambda a, b, c: (a, c, b), 'x': lambda a, b, c: (c, a, b)}[axis]
    planes = [(to3(0, 0, 1), to3(0, 0, hi), tex['top'], scale), (to3(0, 0, -1), to3(0, 0, lo), tex['bottom'], scale)]
    m = len(poly)
    sides = tex.get('sides') or [tex['side']] * m   # 'sides': one texture per edge, in polygon order
    for i in range(m):
        a, b = poly[i], poly[(i + 1) % m]
        da, db = b[0] - a[0], b[1] - a[1]
        planes.append((to3(db, -da, 0), to3(a[0], a[1], lo), sides[i], scale))
    return brush(planes)


def box(mins, maxs, tex):
    return prism([(mins[0], mins[1]), (maxs[0], mins[1]), (maxs[0], maxs[1]), (mins[0], maxs[1])], mins[2], maxs[2], tex)


def ngon(r, sides, cx=0.0, cy=0.0, phase=None):
    phase = math.pi / sides if phase is None else phase
    return [(cx + r * math.cos(phase + 2 * math.pi * i / sides), cy + r * math.sin(phase + 2 * math.pi * i / sides))
            for i in range(sides)]


def ring(r0, r1, sides, lo, hi, tex):
    """An annulus as `sides` trapezoid brushes."""
    out = []
    for i in range(sides):
        t0, t1 = 2 * math.pi * i / sides, 2 * math.pi * (i + 1) / sides
        poly = [(r0 * math.cos(t0), r0 * math.sin(t0)), (r1 * math.cos(t0), r1 * math.sin(t0)),
                (r1 * math.cos(t1), r1 * math.sin(t1)), (r0 * math.cos(t1), r0 * math.sin(t1))]
        out.append(prism(poly, lo, hi, tex))
    return out


def hollow_box(mins, maxs, t, tex):
    """Six slabs of thickness t enclosing mins..maxs (inside faces at mins/maxs). tex: str or per-face dict keyed
    floor/ceiling/xneg/xpos/yneg/ypos."""
    f = (lambda k: tex) if isinstance(tex, str) else (lambda k: tex[k])
    x0, y0, z0 = mins
    x1, y1, z1 = maxs
    return [
        box((x0 - t, y0 - t, z0 - t), (x1 + t, y1 + t, z0), f('floor')),
        box((x0 - t, y0 - t, z1), (x1 + t, y1 + t, z1 + t), f('ceiling')),
        box((x0 - t, y0, z0), (x0, y1, z1), f('xneg')),
        box((x1, y0, z0), (x1 + t, y1, z1), f('xpos')),
        box((x0, y0 - t, z0), (x1, y0, z1), f('yneg')),
        box((x0, y1, z0), (x1, y1 + t, z1), f('ypos')),
    ]


def entity(cls, brushes=(), **kv):
    lines = ['{', '"classname" "%s"' % cls]
    lines += ['"%s" "%s"' % (k, v) for k, v in kv.items()]
    lines += list(brushes)
    lines.append('}')
    return '\n'.join(lines)


def v3(*c):
    return ' '.join('%g' % x for x in c)


# ---------------------------------------------------------------- the map

CAULK = 'system/caulk'
CLIP = 'system/clip'


def deck():
    world, ents = [], []
    floor_dark = 'bespin/u_floor04'
    # deck slab (dark) with the raised stage (light tile) at the centre
    world += ring(STAGE_R, DECK_R, 32, -32, 0, {'top': floor_dark, 'bottom': CAULK, 'side': 'bespin/hydraulic'})
    world.append(prism(ngon(STAGE_R, 32, phase=0), -32, STAGE_TOP,
                       {'top': 'bespin/floor3', 'bottom': CAULK, 'side': 'bespin/hydraulic', 'scale': 0.25}))
    # a deep underside skirt so the deck reads as a structure from the rail
    world.append(prism(ngon(DECK_R - 32, 32, phase=0), -160, -32, {'top': CAULK, 'bottom': 'bespin/u_floor03',
                                                                  'side': 'bespin/mp_b_wall04a'}))
    # parapet
    world += ring(RAIL_R, DECK_R, 32, 0, 36, {'top': 'bespin/u_beam01', 'bottom': CAULK, 'side': 'bespin/hydraulic'})
    # invisible wall to the sky: nobody walks or force-jumps off the deck
    world += ring(DECK_R, DECK_R + 16, 32, 0, 1536, CLIP)
    # pylons behind the stage (the side the front camera looks toward), amber strips facing the stage
    for yaw in (150, 180, 210):
        a = math.radians(yaw)
        cx, cy = 520 * math.cos(a), 520 * math.sin(a)
        world.append(prism(ngon(36, 8, cx, cy), 0, 384, {'top': 'bespin/u_beam01', 'bottom': CAULK,
                                                         'side': 'bespin/u_beam01'}))
        # the strip: a thin slab on the stage-facing side, lit by its shader
        ux, uy = -math.cos(a), -math.sin(a)       # toward the stage
        px, py = -uy, ux                          # across
        c = (cx + ux * 36, cy + uy * 36)
        poly = [(c[0] - px * 8, c[1] - py * 8), (c[0] - px * 8 + ux * 4, c[1] - py * 8 + uy * 4),
                (c[0] + px * 8 + ux * 4, c[1] + py * 8 + uy * 4), (c[0] + px * 8, c[1] + py * 8)]
        if (poly[1][0] - poly[0][0]) * (poly[2][1] - poly[0][1]) - (poly[1][1] - poly[0][1]) * (poly[2][0] - poly[0][0]) < 0:
            poly.reverse()
        world.append(prism(poly, 48, 336, '%s/amber_strip' % TEX))
    # teleporter pad to the studio, off to the sun side so it never shadows the stage
    pad = (0, -460)
    world.append(prism(ngon(40, 16, *pad), 0, 4, {'top': '%s/pad_glow' % TEX, 'bottom': CAULK,
                                                  'side': 'bespin/hydraulic'}))
    ents.append(entity('trigger_teleport', [box((pad[0] - 28, pad[1] - 28, 4), (pad[0] + 28, pad[1] + 28, 72),
                                                'system/trigger')], target='to_studio'))
    ents.append(entity('misc_teleporter_dest', origin=v3(0, -300, STAGE_TOP + 32), angle='90', targetname='to_deck'))
    # a second pad, opposite, to the Cornell box
    pad = (0, 460)
    world.append(prism(ngon(40, 16, *pad), 0, 4, {'top': '%s/pad_glow' % TEX, 'bottom': CAULK,
                                                  'side': 'bespin/hydraulic'}))
    ents.append(entity('trigger_teleport', [box((pad[0] - 28, pad[1] - 28, 4), (pad[0] + 28, pad[1] + 28, 72),
                                                'system/trigger')], target='to_cornell'))
    # spawns: the stage centre facing +x (sun front-left), and two on the deck for duels
    ents.append(entity('info_player_deathmatch', origin=v3(0, 0, STAGE_TOP + 32), angle='0'))
    # a pad to the ruins, behind-left of the stage
    pad = (-330, -330)
    world.append(prism(ngon(40, 16, *pad), 0, 4, {'top': '%s/pad_glow' % TEX, 'bottom': CAULK,
                                                  'side': 'bespin/hydraulic'}))
    ents.append(entity('trigger_teleport', [box((pad[0] - 28, pad[1] - 28, 4), (pad[0] + 28, pad[1] + 28, 72),
                                                'system/trigger')], target='to_ruins'))
    ents.append(entity('info_player_deathmatch', origin=v3(-320, 300, 40), angle='-45'))
    ents.append(entity('info_player_deathmatch', origin=v3(320, -300, 40), angle='135'))
    if DECK_FILL:
        # the sun is front-left (+x, +y): fill from behind-right, high and wide, sky-blue so it reads as bounce
        ents.append(entity('light', origin=v3(-300, -260, 260), light=str(DECK_FILL), _color=v3(0.74, 0.82, 1.0),
                           _deviance='96', _samples='16'))
    ents.append(entity('misc_cubemap', origin=v3(0, 0, STAGE_TOP + 64), name='stage', radius='640'))
    ents.append(entity('misc_cubemap', origin=v3(-420, 0, 96), name='pylons', radius='400'))
    return world, ents


# Sealed rooms use two shells. Inner: inside faces are the room, outside faces an opaque shadow caster
# (studio_dark), because the live sun's cascades skip sky surfaces. Outer, GAP away so q3map2 keeps both face sets:
# sky outside so the box vanishes from the deck, caulk inside.
SHELL_T, SHELL_GAP = 16, 8


def slab(mins, maxs, out, top=None, bottom=None, sides=(None, None, None, None)):
    """A box with `out` everywhere except the named faces. box() edge order: -y, +x, +y, -x."""
    return box(mins, maxs, {'top': top or out, 'bottom': bottom or out, 'side': out,
                            'sides': [s or out for s in sides]})


def shell(r, zlo, zhi, inner, out):
    """Six slabs around -r..r x -r..r x zlo..zhi. inner: one texture, or a dict keyed floor/ceiling/xneg/xpos/yneg/ypos."""
    f = (lambda k: inner) if isinstance(inner, str) else (lambda k: inner[k])
    t = SHELL_T
    return [
        slab((-r - t, -r - t, zlo - t), (r + t, r + t, zlo), out, top=f('floor')),
        slab((-r - t, -r - t, zhi), (r + t, r + t, zhi + t), out, bottom=f('ceiling')),
        slab((-r - t, -r, zlo), (-r, r, zhi), out, sides=(None, f('xneg'), None, None)),
        slab((r, -r, zlo), (r + t, r, zhi), out, sides=(None, None, None, f('xpos'))),
        slab((-r, -r - t, zlo), (r, -r, zhi), out, sides=(None, None, f('yneg'), None)),
        slab((-r, r, zlo), (r, r + t, zhi), out, sides=(f('ypos'), None, None, None)),
    ]


def sky_shell(r, zlo, zhi):
    t, g = SHELL_T, SHELL_GAP
    return shell(r + t + g, zlo - t - g, zhi + t + g, CAULK, '%s/sunset_sky' % TEX)


def studio():
    world, ents = [], []
    h, z0, z1 = STUDIO_HALF, STUDIO_Z, STUDIO_Z + STUDIO_H
    grey, dark = '%s/cyc_grey' % TEX, '%s/studio_dark' % TEX
    t = SHELL_T
    # with sky outside faces nothing blocked the sun and it lit the sealed room (the inner shell's dark outside fixes
    # that once the cascades cover the whole layer; see docs/renderer-vulkan-sun-shadow-investigation.md)
    inner = shell(h, z0, z1, grey, dark)
    inner[1] = slab((-h - t, -h - t, z1), (h + t, h + t, z1 + t), dark)          # ceiling: dark both sides
    world += inner
    world += sky_shell(h, z0, z1)
    # cyclorama: the floor sweeps up into the +x wall (radius CR), as segments smoothed by the shader's shade angle
    CR, SEG = 320, 10
    cx, cz = h - CR, z0 + CR   # the sweep's centre
    for i in range(SEG):
        t0, t1 = -math.pi / 2 + (math.pi / 2) * i / SEG, -math.pi / 2 + (math.pi / 2) * (i + 1) / SEG
        a0 = (cx + CR * math.cos(t0), cz + CR * math.sin(t0))
        a1 = (cx + CR * math.cos(t1), cz + CR * math.sin(t1))
        # (x, z) section: the arc chord and the room corner behind it
        poly = [a0, (h, z0), a1]
        world.append(prism(poly, -h, h, {'top': CAULK, 'bottom': CAULK, 'side': grey}, axis='y'))
    # subject mark and teleporter back up
    world.append(prism(ngon(56, 24, 0, 0), z0, z0 + 4, {'top': grey, 'bottom': CAULK, 'side': grey}))
    pad = (-h + 72, h - 72)
    world.append(prism(ngon(40, 16, *pad), z0, z0 + 4, {'top': '%s/pad_glow' % TEX, 'bottom': CAULK,
                                                         'side': dark}))
    ents.append(entity('trigger_teleport', [box((pad[0] - 28, pad[1] - 28, z0 + 4), (pad[0] + 28, pad[1] + 28, z0 + 72),
                                                'system/trigger')], target='to_deck'))
    ents.append(entity('misc_teleporter_dest', origin=v3(-200, 0, z0 + 40), angle='0', targetname='to_studio'))
    # three-point lighting for a subject at the centre facing -x (toward the camera side)
    lights = [
        ('key', (-330, 270, z0 + 340), 6500, (1.0, 0.95, 0.88), 40),
        ('fill', (-360, -300, z0 + 170), 2000, (0.84, 0.9, 1.0), 56),
        ('rim_l', (150, 280, z0 + 300), 2200, (1.0, 0.94, 0.86), 20),
        ('rim_r', (150, -280, z0 + 300), 2200, (0.9, 0.94, 1.0), 20),
        ('backdrop', (420, 0, z0 + 120), 1400, (1.0, 0.97, 0.94), 64),
    ]
    for _, o, val, col, dev in lights:
        ents.append(entity('light', origin=v3(*o), light=str(val), _color=v3(*col), _deviance=str(dev),
                           _samples='16'))
    ents.append(entity('misc_cubemap', origin=v3(0, 0, z0 + 64), name='studio', radius='640'))
    return world, ents


def cornell():
    """The Cornell box: white floor, ceiling and back wall, red left and green right wall (seen from the camera side,
    -x), a square light in the ceiling, the tall and short blocks behind the subject. The camera side is charcoal
    instead of open. All light is the panel's and its bounces, so the walls' colours bleed onto the model."""
    world, ents = [], []
    h, z0, z1 = CORNELL_HALF, CORNELL_Z, CORNELL_Z + 2 * CORNELL_HALF
    white, red, green = ('%s/cornell_%s' % (TEX, c) for c in ('white', 'red', 'green'))
    dark = '%s/studio_dark' % TEX
    # the camera looks +x: its left is +y (red), its right -y (green)
    world += shell(h, z0, z1, {'floor': white, 'ceiling': white, 'xpos': white, 'xneg': dark, 'ypos': red,
                               'yneg': green}, dark)
    world += sky_shell(h, z0, z1)
    # the light: a thin square panel under the ceiling
    L = 72
    world.append(box((-L, -L, z1 - 2), (L, L, z1), {'top': CAULK, 'bottom': '%s/cornell_light' % TEX,
                                                   'side': '%s/cornell_light' % TEX}))
    # the blocks, turned as in the original: tall back-left, short back-right of the subject
    def block(cx, cy, half, height, yaw):
        a = math.radians(yaw)
        poly = [(cx + half * (math.cos(a) * sx - math.sin(a) * sy), cy + half * (math.sin(a) * sx + math.cos(a) * sy))
                for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        return prism(poly, z0, z0 + height, {'top': white, 'bottom': CAULK, 'side': white})
    world.append(block(150, 110, 52, 220, 17))
    world.append(block(120, -130, 52, 104, -18))
    # back to the deck: a pad in the camera-side corner
    pad = (-h + 64, h - 64)
    world.append(prism(ngon(40, 16, *pad), z0, z0 + 4, {'top': '%s/pad_glow' % TEX, 'bottom': CAULK, 'side': dark}))
    ents.append(entity('trigger_teleport', [box((pad[0] - 28, pad[1] - 28, z0 + 4), (pad[0] + 28, pad[1] + 28, z0 + 72),
                                                'system/trigger')], target='to_deck'))
    ents.append(entity('misc_teleporter_dest', origin=v3(-150, 0, z0 + 40), angle='0', targetname='to_cornell'))
    ents.append(entity('misc_cubemap', origin=v3(0, 0, z0 + 64), name='cornell', radius='400'))
    return world, ents


def _polar(r, yaw):
    a = math.radians(yaw)
    return r * math.cos(a), r * math.sin(a)


def _beam(a, b, width, lo, hi, tex):
    """A straight beam from point a to point b (2D), `width` wide, lo..hi high."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    l = math.hypot(dx, dy)
    px, py = -dy / l * width / 2, dx / l * width / 2
    return prism([(a[0] - px, a[1] - py), (b[0] - px, b[1] - py), (b[0] + px, b[1] + py), (a[0] + px, a[1] + py)],
                 lo, hi, tex)


def ruins():
    """The hero stage: a pale, overgrown temple terrace above the Yavin canopy. The subject faces away from the low
    sun, so the sun rims her hair and shoulders while the bright sky and the pale stone light her front; the camera
    looks at her front with the colonnade and the sky behind her. Columns stand off the sun line so their shadows
    fall beside her, not on her."""
    world, ents = [], []
    z = RUINS_Z
    stone, paved, pale = 'yavin/stone', 'yavin/paved', 'yavin/temple_basicwall4'
    vines, rock, wall = 'yavin/temple_vines2', 'yavin/rockwall', 'yavin/stonewall2'
    # the terrace, a rock base under it, the dais she stands on
    world.append(prism(ngon(RUINS_R, 32, phase=0), z - 48, z, {'top': paved, 'bottom': CAULK, 'side': wall,
                                                              'scale': 0.5}))
    world.append(prism(ngon(RUINS_R - 40, 32, phase=0), z - 420, z - 48, {'top': CAULK, 'bottom': rock,
                                                                          'side': rock}))
    world.append(prism(ngon(110, 24, phase=0), z, z + 12, {'top': stone, 'bottom': CAULK, 'side': stone}))
    # a broken parapet: low wall pieces with gaps
    pieces = ring(RUINS_R - 24, RUINS_R, 32, z, z + 36, {'top': stone, 'bottom': CAULK, 'side': wall})
    for i, b in enumerate(pieces):
        if i % 5 not in (2, 3):
            world.append(b)
    world += ring(RUINS_R, RUINS_R + 16, 32, z, z + 1024, CLIP)
    # the colonnade behind her (around the sun's yaw), skipping the sun line itself
    tops = {}
    for d, height, intact in ((-80, 300, True), (-60, 140, False), (-40, 300, True), (-22, 300, True),
                              (22, 300, True), (40, 96, False), (60, 300, True), (80, 210, False)):
        cx, cy = _polar(400, SUN_YAW + d)
        side = vines if d in (-60, 40, 80) else pale
        world.append(prism(ngon(26, 8, cx, cy), z, z + height, {'top': stone, 'bottom': CAULK, 'side': side,
                                                               'scale': 0.25}))
        world.append(prism(ngon(40, 8, cx, cy), z, z + 20, {'top': stone, 'bottom': CAULK, 'side': stone}))
        if intact:
            world.append(prism(ngon(38, 8, cx, cy), z + height, z + height + 20,
                               {'top': stone, 'bottom': stone, 'side': stone}))
            tops[d] = (cx, cy, z + height + 20)
    # lintels across intact neighbours
    for a, b in ((-80, -40), (-40, -22), (22, 60)):
        world.append(_beam(tops[a][:2], tops[b][:2], 36, tops[a][2], tops[a][2] + 30,
                           {'top': stone, 'bottom': stone, 'side': pale, 'scale': 0.25}))
    # a fallen drum on the terrace, front-left of the camera's view
    fx, fy = _polar(300, RUINS_FACING + 55)
    world.append(prism(ngon(26, 8, fy, z + 26, phase=math.pi / 8), fx - 90, fx + 90,
                       {'top': pale, 'bottom': pale, 'side': pale, 'scale': 0.25}, axis='x'))
    # pads to the other stages, on the camera's half of the terrace (the colonnade holds the sun's half)
    for yaw, target in ((RUINS_FACING - 70, 'to_deck'), (RUINS_FACING + 40, 'to_studio'),
                        (RUINS_FACING + 70, 'to_cornell')):
        px, py = _polar(470, yaw)
        world.append(prism(ngon(40, 16, px, py), z, z + 4, {'top': '%s/pad_glow' % TEX, 'bottom': CAULK,
                                                            'side': stone}))
        ents.append(entity('trigger_teleport', [box((px - 28, py - 28, z + 4), (px + 28, py + 28, z + 72),
                                                    'system/trigger')], target=target))
    ents.append(entity('misc_teleporter_dest', origin=v3(*_polar(160, RUINS_FACING), z + 40),
                       angle=str(RUINS_FACING), targetname='to_ruins'))
    ents.append(entity('info_player_start', origin=v3(0, 0, z + 12 + 32), angle=str(RUINS_FACING)))
    # its own sky: Yavin inside, the deck's sky outside (both skipped by the sun cascades, so the live sun lights it)
    h, lo, hi = 1100, z - 700, z + 1300
    world += shell(h, lo, hi, '%s/ruins_sky' % TEX, CAULK)
    world += sky_shell(h, lo, hi)
    ents.append(entity('misc_cubemap', origin=v3(0, 0, z + 76), name='ruins', radius='700'))
    return world, ents


def build_map():
    world, ents = [], []
    for part in (deck, studio, cornell, ruins):
        w, e = part()
        world += w
        ents += e
    # sky shell around the deck (the studio is sealed on its own, below the deck)
    world += hollow_box((-3072, -3072, SKY_BOTTOM), (3072, 3072, 3072), 16, '%s/sunset_sky' % TEX)
    # light grid bounds: the stages only (RBSP indexes grid points with 16 bits: keep under 65k; 22 x 22 x 81 now)
    world.append(box((-704, -704, RUINS_Z - 16), (704, 704, 640), 'system/lightgrid'))
    ws = entity('worldspawn', world, message='xenjk showcase', _ambient=str(AMBIENT), gridsize='64 64 64', _lightmapscale='0.5',
                _blocksize='0', music='')
    return '\n'.join([ws] + ents) + '\n'


# ---------------------------------------------------------------- shader, textures, scripts

def shader_text():
    s = '%g %g %g' % SUN_RGB
    return f'''// xenjk_showcase: generated by tools/showcase_map/build_showcase_map.py

textures/{TEX}/sunset_sky
{{
	qer_editorimage textures/skies/bespin_ft
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nomarks
	notc
	q3map_nolightmap
	q3map_lightImage textures/skies/bespin_up
	q3map_sunExt {s} {SUN_BAKE} {SUN_YAW} {SUN_ELEV} 2 16
	q3map_skylight {SKY_LIGHT} 3
	skyParms textures/skies/bespin 512 -
	q3gl2_sun {s} {SUN_LIVE} {SUN_YAW} {SUN_ELEV} {MAP_LIGHT_SCALE} {SUN_SHADOW_SCALE}
}}

textures/{TEX}/ruins_sky
{{
	qer_editorimage textures/skies/yavin_ft
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nomarks
	notc
	q3map_nolightmap
	q3map_lightImage textures/skies/yavin_up
	q3map_sunExt 1 0.86 0.66 {RUINS_SUN_BAKE} {SUN_YAW} {SUN_ELEV} 2 16
	q3map_skylight {RUINS_SKYLIGHT} 3
	skyParms textures/skies/yavin 512 -
	// the renderer keeps one live sun, from the last sky shader it parsed: repeat the deck's exactly
	q3gl2_sun {s} {SUN_LIVE} {SUN_YAW} {SUN_ELEV} {MAP_LIGHT_SCALE} {SUN_SHADOW_SCALE}
}}

textures/{TEX}/amber_strip
{{
	qer_editorimage textures/bespin/u_light04
	q3map_surfacelight {AMBER}
	q3map_lightRGB 1 0.62 0.3
	q3map_lightSubdivide 32
	q3map_nonplanar
	{{
		map $lightmap
	}}
	{{
		map textures/bespin/u_light04
		blendFunc GL_DST_COLOR GL_ZERO
	}}
	{{
		map textures/bespin/u_light04glow
		blendFunc GL_ONE GL_ONE
		glow
	}}
}}

textures/{TEX}/pad_glow
{{
	qer_editorimage textures/bespin/mp_b_light04
	q3map_surfacelight 300
	q3map_lightRGB 0.45 0.7 1
	{{
		map $lightmap
	}}
	{{
		map textures/bespin/mp_b_light04
		blendFunc GL_DST_COLOR GL_ZERO
	}}
	{{
		map textures/bespin/mp_b_light04_glow
		blendFunc GL_ONE GL_ONE
		glow
	}}
}}

textures/{TEX}/cyc_grey
{{
	qer_editorimage textures/{TEX}/cyc_grey
	q3map_shadeAngle 80
	q3map_nonplanar
	{{
		map $lightmap
	}}
	{{
		map textures/{TEX}/cyc_grey
		blendFunc GL_DST_COLOR GL_ZERO
	}}
}}

textures/{TEX}/cornell_light
{{
	qer_editorimage textures/{TEX}/cornell_white
	q3map_surfacelight {CORNELL_LIGHT}
	q3map_lightRGB 1 0.86 0.68
	q3map_lightSubdivide 48
	{{
		map textures/{TEX}/cornell_white
		rgbGen identity
	}}
	{{
		map textures/{TEX}/cornell_white
		blendFunc GL_ONE GL_ONE
		glow
	}}
}}

textures/{TEX}/cornell_white
{{
	qer_editorimage textures/{TEX}/cornell_white
	{{
		map $lightmap
	}}
	{{
		map textures/{TEX}/cornell_white
		blendFunc GL_DST_COLOR GL_ZERO
	}}
}}

textures/{TEX}/cornell_red
{{
	qer_editorimage textures/{TEX}/cornell_red
	{{
		map $lightmap
	}}
	{{
		map textures/{TEX}/cornell_red
		blendFunc GL_DST_COLOR GL_ZERO
	}}
}}

textures/{TEX}/cornell_green
{{
	qer_editorimage textures/{TEX}/cornell_green
	{{
		map $lightmap
	}}
	{{
		map textures/{TEX}/cornell_green
		blendFunc GL_DST_COLOR GL_ZERO
	}}
}}

textures/{TEX}/studio_dark
{{
	qer_editorimage textures/{TEX}/studio_dark
	{{
		map $lightmap
	}}
	{{
		map textures/{TEX}/studio_dark
		blendFunc GL_DST_COLOR GL_ZERO
	}}
}}
'''


def write_textures(base):
    """Our own flat studio materials: a soft, faintly noisy grey (no banding under the lightmap) and a charcoal."""
    from PIL import Image
    import random
    d = base / 'textures' / TEX
    d.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(20261013)
    # Cornell box reflectances (0.725 white, 0.63/0.065/0.05 red, 0.14/0.45/0.091 green) as 8-bit colours
    for name, rgb, amp in (('cyc_grey', (150, 150, 150), 3), ('studio_dark', (34, 34, 34), 3),
                           ('cornell_white', (185, 185, 185), 2), ('cornell_red', (161, 17, 13), 2),
                           ('cornell_green', (36, 115, 23), 2)):
        im = Image.new('RGB', (256, 256))
        im.putdata([(lambda n: tuple(max(0, c + n) for c in rgb))(rnd.randint(-amp, amp)) for _ in range(256 * 256)])
        im.save(d / (name + '.tga'))


ARENA = '{\n\tmap\t\t"%s"\n\tlongname\t"xenjk Showcase"\n\ttype\t\t"ffa duel powerduel"\n}\n' % NAME

# exec xenjk_showcase: third-person camera in front of the model, HUD off
CFG = '''// xenjk_showcase camera: the model faces +x into the sun; this camera looks at its front
seta cg_thirdPerson 1
seta cg_thirdPersonRange 110
seta cg_thirdPersonAngle 160
seta cg_thirdPersonVertOffset 10
seta cg_thirdPersonPitchOffset 0
seta cg_draw2D 0
seta cg_drawGun 0
echo "showcase camera on; cg_thirdPersonAngle rotates around the model, cg_draw2D 1 brings the HUD back"
'''


# ---------------------------------------------------------------- compile and pack

def q3map2(stage_args, mapfile, base_root, log):
    if Q3MAP2 is None:
        sys.exit('set Q3MAP2 to q3map2.exe (netradiant-custom 20260114)')
    args = [str(Q3MAP2), *stage_args[:1], '-game', 'ja', '-fs_basepath', str(GAME), '-fs_basepath', str(base_root),
            *stage_args[1:], str(mapfile)]
    t0 = time.time()
    with open(log, 'a', encoding='utf-8') as f:
        f.write('\n$ %s\n' % ' '.join(args))
        f.flush()
        r = subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, cwd=str(Q3MAP2.parent))
    print('  %-8s %5.1f s  exit %d' % (stage_args[0] if stage_args[0].startswith('-') else 'bsp', time.time() - t0,
                                       r.returncode))
    if r.returncode:
        sys.exit('q3map2 failed, see %s' % log)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--out', default=str(ROOT / '.codex-research' / 'showcase_map' / 'build' / time.strftime('%Y%m%d-%H%M%S')))
    ap.add_argument('--fast', action='store_true', help='quick light pass for layout checks')
    a = ap.parse_args(argv)
    out = Path(a.out).resolve()
    root = out / 'stage'                  # an fs_basepath for q3map2: stage/base/{maps,shaders,textures}
    base = root / 'base'
    for sub in ('maps', 'shaders', 'scripts'):
        (base / sub).mkdir(parents=True, exist_ok=True)
    mapfile = base / 'maps' / (NAME + '.map')
    mapfile.write_text(build_map(), encoding='utf-8', newline='\n')
    (base / 'shaders' / (NAME + '.shader')).write_text(shader_text(), encoding='utf-8', newline='\r\n')
    (base / 'scripts' / (NAME + '.arena')).write_text(ARENA, encoding='utf-8', newline='\r\n')
    (base / (NAME + '.cfg')).write_text(CFG, encoding='utf-8', newline='\r\n')
    write_textures(base)
    log = out / 'q3map2.log'
    print('building %s in %s' % (NAME, out))
    q3map2(['-bsp', '-meta', '-patchmeta'], mapfile, root, log)
    q3map2(['-vis', '-saveprt'], mapfile, root, log)
    light = ['-light', '-fast', '-filter', '-patchshadows', '-deluxe']
    light += ['-bounce', '1', '-samples', '2'] if a.fast else ['-bounce', '4', '-samples', '3', '-bouncegrid']
    q3map2(light, mapfile, root, log)
    pk3 = out / ('zz_%s.pk3' % NAME)
    with zipfile.ZipFile(pk3, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(base.rglob('*')):
            rel = p.relative_to(base).as_posix()
            if p.is_file() and not rel.endswith(('.prt', '.srf', '.map', '.bak')) and not rel.startswith('maps/' + NAME + '/'):
                z.write(p, rel)
            elif p.is_file() and rel.startswith('maps/' + NAME + '/'):   # external lightmaps, if any
                z.write(p, rel)
    shutil.copy2(mapfile, out / mapfile.name)
    print('pk3: %s (%.1f MB)' % (pk3, pk3.stat().st_size / 1e6))
    return pk3


if __name__ == '__main__':
    main()
