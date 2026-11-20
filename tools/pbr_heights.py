#!/usr/bin/env python3
"""Structure-aware height/normal maps for rend2 parallax materials (siege_urban_pbr v2).

The first urban build (build_urban.py, in the separate rend2 map-builder tools) gave every
lightmapped texture a height made from band-passed luminance, compressed to alpha
0.72..1.0, and ran parallax on 52 materials. Stains and painted shading became
geometry, the useful relief (mortar, joints, gaps) moved by only 0.003..0.008 of a
repeat, and the asphalt ground paid full parallax cost for nothing visible.

This tool rebuilds those 52 materials as an override pack:
  * materials with real discrete relief keep parallax, with a new height that is flat
    at 1.0 on the faces and carves recesses (mortar, joints, plank gaps, grating
    slots, crevices) down the full 0..1 range;
  * the rest switch to a plain `normalMap` (RGB `_n.png`, same normals as before).

Height methods (all filters wrap, so every output tiles):
  groove   thin elongated recesses: morphological top-hat of contrast-normalised
           luminance (dark or bright polarity), kept only where it survives a
           horizontal/vertical (and for cracks diagonal) line opening, soft
           thresholded, cleaned of small blobs, bevelled. Faces get a small,
           top-clamped pore term.
  plateau  two-level relief (grating bars over slots): smoothed luminance, opened
           and closed to drop scratches, soft thresholded and bevelled.
  rubble   piles (garbage, grass): band-passed luminance over the full range with a
           soft top clamp, so crevices are deep and tops do not swim.

The normal map comes from the final height scaled by the material's parallax depth
(physically consistent with what the parallax shows) plus a fine luminance-detail term
so faces are not plastic. RMO keeps the first build's roughness/metal convention and
takes its cavity/AO from the new height.

Usage:
  python tools/pbr_heights.py build-urban [--out DIR] [--content DIR]
Deterministic; reads the bench content copy only (never writes there).

Map pipeline: the rend2 map builders (a separate tools folder, not in this repository) import
this module (build_dantooine.load_pbr_heights, XENJK_TOOLS overrides the path) and call
v2_maps() / height_maps() per material, so a rebuild produces these materials directly:
  build_urban.py      URBAN_V2       (same files as the build-urban override pack)
  build_dantooine.py  DANTOONINE_V2
  build_ctf_pbr.py    ILLIMIRAN_V2   (--heights illimiran)
  build_cyber.py      CYBER_V2       (height traced on the CP albedo, kept in CP's normal alpha)

Alignment check: does the trace follow the albedo's own joints? Build with
PBR_CAPTURE_DIR=DIR set (each traced material's working albedo is saved to DIR/<table>/<leaf>.png), then
  python tools/pbr_heights.py check DIR --out OUT [--only urban/wall3,...] [--before OLD_OUT]
writes per material OUT/<table>/<leaf>_overlay.png (the whole texture: traced joint edge red, centreline
yellow, strong albedo joints the trace misses cyan), <leaf>_crop.png (1:1 top-left 512 px, albedo | overlay),
OUT/scores.json, and with --before changed_sheet.jpg (albedo | before | after for every changed trace).
Score (alignment()): an independent ridge detector on the albedo gives the strong joint pixels per direction;
cov = share of them the trace covers, sup = share of the traced centreline that has ridge evidence, w_alb/w_tr =
albedo vs traced joint width in px, score = (F1(cov, sup) - F1 of the same trace shifted by chance) / (1 - that).
Plateau and rubble methods have no thin joints and are judged on the overlay. Table rules that came out of it:
  heads: "albedo"  head joints per course from the albedo's own vertical edges (any spacing), not a brick period
  heads: False     courses only
  mortar           the albedo's measured joint width (w_alb), not a guess
  depth            0.35 x course, but at most 2 x the drawn joint width (thin joints must not become black slits)
  no table params  a material that cannot be traced reliably (wall15) is a plain normal map
"""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
CONTENT = ROOT / ".codex-research" / "renderer-bench" / "content" / "base"
OUT = ROOT / ".codex-research" / "renderer-bench" / "variants" / "urban_v2"
URBAN_PK3 = "zz_xen_rend2_urban.pk3"
SOURCE_PK3S = ("siege_urban_v_10.pk3", "assets8_pbr2.pk3", "assets8_pbr1.pk3")
PACK_NAME = "zzzz_urban_v2.pk3"
MTR = "shaders/xen_urban.mtr"
NS = "textures/xen_urb"

# --- first build's classification, copied from build_urban.py (report only) ---------
V1_MATERIALS = {  # normal strength, parallax depth, roughness, metal, ao strength
    "concrete": (1.5, 0.020, 0.88, 0.00, 0.50), "brick": (2.5, 0.030, 0.88, 0.00, 0.60),
    "asphalt": (1.2, 0.010, 0.92, 0.00, 0.35), "plaster": (2.5, 0.015, 0.90, 0.00, 0.45),
    "metal": (3.0, 0.000, 0.50, 0.55, 0.40), "painted": (2.5, 0.000, 0.45, 0.10, 0.40),
    "wood": (4.0, 0.020, 0.75, 0.00, 0.50), "cardboard": (2.0, 0.010, 0.92, 0.00, 0.40),
    "fabric": (2.0, 0.010, 0.95, 0.00, 0.50), "window": (1.5, 0.000, 0.22, 0.00, 0.30),
    "rough": (3.5, 0.020, 0.92, 0.00, 0.60), "car": (1.2, 0.000, 0.30, 0.00, 0.25),
    "prop": (2.0, 0.000, 0.60, 0.00, 0.35),
}
V1_CLASSES = [
    ("window", "window"), ("glass", "window"),
    ("roadline", "asphalt"), ("road", "asphalt"), ("tracks", "asphalt"),
    ("brick", "brick"), ("wall13", "brick"), ("wall3", "brick"),
    ("sidewalk", "concrete"), ("cement", "concrete"), ("tile", "concrete"), ("wall", "concrete"),
    ("white", "plaster"), ("light", "plaster"),
    ("box", "cardboard"),
    ("wood", "wood"), ("crate1", "wood"), ("crate2", "wood"), ("crate4", "wood"),
    ("carpet", "fabric"), ("couch", "fabric"), ("clothes", "fabric"),
    ("grass", "rough"), ("garbage", "rough"),
    ("crate3", "painted"), ("door", "painted"), ("garage", "painted"), ("fridge", "painted"),
    ("stove", "painted"), ("colors/", "painted"), ("employees", "painted"), ("firstaid", "painted"),
    ("caprice", "car"), ("police", "car"), ("luxury", "car"), ("truck_flatbed", "car"),
    ("forklift/body", "car"),
    ("models/", "prop"),
]


def classify_v1(name):
    low = name.lower()
    for frag, kind in V1_CLASSES:
        if frag in low:
            return kind
    return "metal"


# --- v2 decisions ----------------------------------------------------------------------
# Every v1 parallax material falls in exactly one v2 class. Parallax classes name a
# height method; depth is parallaxDepth for full-range (0..1) heights.
V2_CLASSES = {
    "masonry":   dict(parallax=True, why="brick / block courses with mortar: discrete recess, the "
                                         "classic parallax win"),
    "joint":     dict(parallax=True, why="slabs or tiles with cut joints and edge cracks: few, real "
                                         "recesses on otherwise flat faces"),
    "grating":   dict(parallax=True, why="bars over deep slots: strongest two-level relief on the map"),
    "container": dict(parallax=True, why="shipping-container panels, posts and plank seams: real "
                                         "recessed seams between flat panels"),
    "planks":    dict(parallax=True, why="boards with gaps (parquet) or deep weathering cracks"),
    "panel":     dict(parallax=True, why="painted panels with cut seam grooves (white): thin real grooves"),
    "rubble":    dict(parallax=True, why="heaped trash / grass blades: depth between pieces is the look"),
    "asphalt":   dict(parallax=False, why="fine aggregate only, largest screen area; no discrete relief "
                                          "a ray march can show, normal map carries the grain"),
    "cardboard": dict(parallax=False, why="flat sheets; creases and tape are sub-millimetre, labels "
                                          "would become geometry"),
    "fabric":    dict(parallax=False, why="weave is micro-relief; normal map only"),
    "plaster":   dict(parallax=False, why="smooth or stuccoed walls: stains/scratches are paint, not "
                                          "relief; normal map only"),
    "concrete":  dict(parallax=False, why="poured concrete: form-tie pits and stains only, no joints"),
    "metal":     dict(parallax=False, why="painted metal sheet (rust is colour, seams are rivets): normal"),
    "smooth_wood": dict(parallax=False, why="veneer / board face without gaps: grain is colour"),
    "print":     dict(parallax=False, why="printed box art; any height is a lie"),
    # added for ctf_dantooine_pbr
    "carving":   dict(parallax=True, why="relief cut into stone (emblem outlines): recessed lines on flat faces"),
    "marble":    dict(parallax=False, why="polished stone: veins are colour, not relief"),
    "irregular": dict(parallax=False, why="masonry whose joints the tracer cannot separate from grime: a false "
                                          "groove pattern is worse than none"),
    "frame":     dict(parallax=False, why="one large panel per repeat with a moulded frame: a normal map carries "
                                          "the bevel; the face itself is flat"),
}

# leaf -> (v2 class, method params). Method params only for parallax classes.
# polarity: which way the recess differs in luminance ("dark" mortar, "bright" mortar).
# width: top-hat structuring size in px (at the working resolution), ~2x recess width.
# line: minimum straight run in px for a recess to count (rejects stains/blobs).
# cover: expected recess area fraction, sets the soft threshold.
# masonry: course = bed-joint spacing px, brick = typical unit length px, mortar = drawn
#   joint width px (course/brick measured by autocorrelation, checked on 1:1 crops);
#   vertical = courses run vertically (plank columns, container posts); straight = machined
#   seams, not traced; openings = colour distance that marks windows/doors as set-back reveals.
# floor: height at the bottom of a recess.
# depth: parallaxDepth in UV units for the 0..1 height. Rule: the deepest recess shows about
#   a third of the feature it separates (0.35 x course for masonry, x slot/board width
#   elsewhere), in texels of the axis the courses stack along; capped at 0.045 so grazing
#   views (offset = depth x tan(angle)) stay inside one unit. Flat tops sit at 1.0 = depth 0,
#   so faces never swim; only recesses move.
MASONRY = dict(method="masonry", floor=0.06, pores=0.10, bevel=1.0)
# heads="albedo": head joints are read from the albedo's own vertical edges per course (any spacing), not
# from a regular brick period. mortar = the albedo's measured joint width (check: w_alb), not a guess.
WALL13 = dict(MASONRY, channel="chroma", polarity="dark", width=7, course=17, brick=43, mortar=3, heads="albedo",
              depth=0.006)                                  # 0.35 x 17 / 1024
# facade variants: windows/doors are reveals at the full depth, mortar uses half of it
FACADE = dict(openings=0.25, opening_height=0.0, floor=0.5)
CONTAINER = dict(MASONRY, vertical=True, straight=True, polarity="dark", width=11, course=25, brick=4096,
                 mortar=5, floor=0.15, pores=0.05, bed_z=2.0, head_z=99.0, depth=0.015)
URBAN_V2 = {
    # depth rule: the deepest recess shows 0.35 x course, but at most 2 x the drawn joint width
    "brickwall":   ("masonry", dict(MASONRY, polarity="dark", width=13, course=80, brick=150, mortar=3, heads="albedo",
                                    depth=0.016)),          # 2 x 4 / 512 (joint 3 px in the albedo)
    "wall3":       ("masonry", dict(MASONRY, polarity="bright", width=9, course=48, brick=100, mortar=2, heads="albedo", floor=0.2,
                                    depth=0.006)),          # 2 x 3 / 1024; was mortar 4, periodic heads, 0.016
    "wall13":      ("masonry", WALL13),
    "wall13door":  ("masonry", dict(WALL13, **FACADE, depth=0.012)),
    # wall15: 10 px courses of small, uneven bricks: the tracer finds no consistent head joints (alignment
    # 0.3-0.4 whatever the setting), a wrong grid is worse than none -> plain normal map
    "wall15":      ("irregular", None),
    "wall15door":  ("irregular", None),
    "tile":        ("joint", dict(method="groove", polarity="dark", width=9, line=61, cover=0.02,
                                  diagonal=False, floor=0.10, pores=0.12, bevel=1.0, depth=0.015)),
    "sidewalktrim": ("joint", dict(method="groove", polarity="dark", width=9, line=41, cover=0.02,
                                   diagonal=False, floor=0.10, pores=0.22, bevel=1.2, depth=0.015)),
    "tracks":      ("grating", dict(method="plateau", polarity="bright", smooth=3.0, open=11, slot_line=121,
                                    cover=0.45, floor=0.0, pores=0.06, bevel=1.5,
                                    depth=0.035)),          # slots ~25 px wide, 18 texels deep
    # (slot_line 41 -> 121: dirt blotches in the slots were bridged into the bars)
    "crate1":      ("container", CONTAINER),
    "crate1side":  ("container", CONTAINER),
    "crate2":      ("container", CONTAINER),
    "crate2side":  ("container", CONTAINER),
    "crate4":      ("container", dict(MASONRY, vertical=True, straight=True, polarity="dark", width=7,
                                      course=23, brick=60, mortar=2, floor=0.25, bed_z=1.0, heads="albedo",
                                      head_gap=12, depth=0.010)),  # plank columns + the cross seams (alignment 0.26 -> 0.74)
    "wood1":       ("planks", dict(MASONRY, vertical=True, straight=True, polarity="dark", width=5, course=51,
                                   brick=100, mortar=2, floor=0.35, pores=0.04, bed_z=1.0, head_z=2.5,
                                   depth=0.015)),
    "wood4":       ("planks", dict(method="groove", polarity="dark", width=9, line=21, cover=0.10,
                                   diagonal=True, floor=0.05, pores=0.12, bevel=1.0, depth=0.025)),
    "white":       ("panel", dict(method="groove", polarity="dark", width=7, line=41, cover=0.02,
                                  diagonal=False, floor=0.30, pores=0.0, bevel=1.0, depth=0.015)),
    "garbage":     ("rubble", dict(method="rubble", low=10.0, gamma=1.5, top=1.2, depth=0.030)),
    "trashcangarbage": ("rubble", dict(method="rubble", low=6.0, gamma=1.5, top=1.2, depth=0.030)),
    "grass":       ("rubble", dict(method="rubble", low=8.0, gamma=1.3, top=1.15, depth=0.012)),
    # plain normal maps
    "road": ("asphalt", None), "roadline": ("asphalt", None),
    "box1a": ("cardboard", None), "box1b": ("cardboard", None), "box1c": ("cardboard", None),
    "box1d": ("cardboard", None), "box1e": ("cardboard", None), "box2a": ("cardboard", None),
    "box2b": ("cardboard", None), "box3a": ("cardboard", None), "box3b": ("cardboard", None),
    "box3d": ("cardboard", None), "gamebox": ("print", None),
    "carpet": ("fabric", None), "couch": ("fabric", None), "couchwide": ("fabric", None),
    "cement": ("concrete", None), "sidewalk": ("concrete", None),
    "wall1": ("plaster", None), "wall2": ("plaster", None), "wall4": ("plaster", None),
    "wall5": ("plaster", None), "wall6": ("plaster", None), "wall7": ("plaster", None),
    "wall8": ("plaster", None), "wall10": ("plaster", None), "wall11": ("plaster", None),
    "wall12": ("plaster", None), "wall14": ("plaster", None),
    "wall9": ("metal", None),
    "wood2": ("smooth_wood", None), "wood3": ("smooth_wood", None),
}

# ctf_dantooine_pbr (build_dantooine.py): the 9 generated materials the first build gave parallax
# (stone 0.035, plaster 0.015). Leaves are build_dantooine's GENERATED leaf names.
DANTOONINE_V2 = {
    # irregular ashlar: near-black joints and diagonal cracks, not a regular grid -> groove with diagonals
    # on raw luminance (contrast normalisation turned the vertical drip stains into joints)
    "temple_basicwall": ("masonry", dict(method="groove", normalise="raw", polarity="dark", width=9, line=61,
                                         cover=0.04, diagonal=True, floor=0.10, pores=0.10, bevel=1.2,
                                         depth=0.030)),
    # joints no darker than its grime band: neither groove mode traces it (hatching), so no parallax
    "templewall_1": ("irregular", None),
    # rubble-stone wall: dark stones in pale mortar (~100 px stones at 1024 -> 0.35 x 100 / 1024)
    "rock_mayan4": ("rubble", dict(method="rubble", polarity="dark", low=24.0, gamma=1.5, top=1.2, depth=0.035)),
    # square tiles with pale bevelled joints (~85 px tiles at 512)
    "floor2": ("joint", dict(method="groove", polarity="bright", width=9, line=41, cover=0.10, diagonal=False,
                             floor=0.15, pores=0.12, bevel=1.2, depth=0.020)),
    # metal floor plates: four plates per repeat with thin seams
    "warroom_floor": ("panel", dict(method="groove", polarity="dark", width=7, line=61, cover=0.015,
                                    diagonal=False, floor=0.30, pores=0.04, bevel=1.0, depth=0.010)),
    # carved emblem: dark cut outlines
    "stone_pull": ("carving", dict(method="groove", normalise="raw", polarity="dark", width=9, line=15,
                                   cover=0.06, diagonal=True, floor=0.20, pores=0.10, bevel=1.0, depth=0.020)),
    "pillerwall2": ("marble", None),
    "plainwall": ("plaster", None),
    "stonepanel04": ("frame", None),
}
# ctf_illimiran_pbr (build_ctf_pbr.py --heights illimiran). The first build gave no material parallax
# (it judged the stock textures' pack heights too noisy); under the v2 rule only generated materials
# with discrete relief get it. Every leaf not listed keeps its plain normal map.
ILLIMIRAN_V2 = {
    # sandstone blocks with dark joints (512 px, ~7 courses)
    "wall1": ("masonry", dict(method="groove", normalise="raw", polarity="dark", width=7, line=41, cover=0.06,
                              diagonal=False, floor=0.10, pores=0.10, bevel=1.0, depth=0.020)),
    # square grating: 6x6 dark cells between bright bars (85 px pitch at 512)
    "new7": ("grating", dict(method="plateau", polarity="bright", smooth=1.5, open=7, grid=6, cover=0.45,
                             floor=0.0, pores=0.04, bevel=1.2, depth=0.030)),
    # floor and wall panels with thin cut seams
    "pod_floor": ("panel", dict(method="groove", polarity="dark", width=7, line=61, cover=0.02, diagonal=False,
                                floor=0.30, pores=0.04, bevel=1.0, depth=0.010)),
    "floor1": ("panel", dict(method="groove", polarity="dark", width=7, line=61, cover=0.02, diagonal=False,
                             floor=0.30, pores=0.04, bevel=1.0, depth=0.010)),
    "kgrille1": ("panel", dict(method="groove", polarity="dark", width=9, line=81, cover=0.03, diagonal=False,
                               floor=0.25, pores=0.04, bevel=1.0, depth=0.012)),
}
# siege_urban_cyber (build_cyber.py): CP2077 world materials. Heights are traced on the CP albedo
# (the leaf's material set, 1024 px); the builder keeps CP's authored normals and stores the traced
# height in their alpha. Only the masonry sets have discrete relief; every other WORLD leaf stays a
# plain normal map. 14 brick courses / 12 block courses per repeat, pale mortar.
CP_BRICK = dict(MASONRY, straight=True, polarity="bright", width=9, course=73, brick=128, mortar=4, floor=0.10,
                heads="albedo", depth=0.010)                 # 2 x 5 / 1024; was mortar 6, periodic heads, 0.025
CYBER_V2 = {
    "wall3": ("masonry", CP_BRICK),                          # bricks_old_rough
    "brickwall": ("masonry", CP_BRICK),                      # bricks_old_rough
    "wall13": ("masonry", CP_BRICK),                         # bricks_old_damaged (same bond)
    "wall6": ("masonry", dict(MASONRY, straight=True, polarity="bright", width=7, course=85, brick=171, mortar=3,
                              floor=0.15, heads="albedo", depth=0.008)),  # cinderblock, 2 x 4 / 1024
}
HEIGHT_TABLES = {"urban": URBAN_V2, "dantooine": DANTOONINE_V2, "illimiran": ILLIMIRAN_V2, "cyber": CYBER_V2}

PREVIEW = ["brickwall", "wall3", "wall13", "wall15door", "tile", "white", "tracks", "crate1",
           "crate4", "wood1", "wood4", "garbage"]


# --- image helpers -----------------------------------------------------------------------
def blur(a, sigma):
    return ndimage.gaussian_filter(np.asarray(a, np.float32), sigma, mode="wrap")


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / max(e1 - e0, 1e-6), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def wrap_gradient(h):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5
    return dx, dy


def working_image(diffuse):
    img = diffuse.convert("RGB")
    if max(img.size) > 1024:
        img.thumbnail((1024, 1024), Image.Resampling.LANCZOS)
    return img


def luminance(img):
    return np.asarray(img.convert("L"), np.float32) / 255.0


def contrast_normalised(lum, sigma):
    """Remove painted-in lighting and equalise contrast so stained and clean areas
    produce comparable recess responses."""
    d = lum - blur(lum, sigma)
    return d / (np.sqrt(blur(d * d, sigma)) + 0.02)


def line_footprints(n, diagonal):
    n = max(3, int(n) | 1)
    fps = [np.ones((1, n), bool), np.ones((n, 1), bool)]
    if diagonal:
        fps += [np.eye(n, dtype=bool), np.fliplr(np.eye(n, dtype=bool))]
    return fps


def drop_small(mask, min_area):
    """Remove connected blobs below min_area; labels on a 2x2 tiled copy so components
    crossing the wrap seam are measured whole."""
    h, w = mask.shape
    tiled = np.tile(mask, (2, 2))
    lab, n = ndimage.label(tiled, structure=np.ones((3, 3)))
    if n == 0:
        return mask
    area = ndimage.sum(np.ones_like(tiled, np.float32), lab, index=np.arange(1, n + 1))
    keep = np.concatenate([[False], area >= min_area])
    window = keep[lab][h // 2: h // 2 + h, w // 2: w // 2 + w]
    return np.roll(window, (h // 2, w // 2), axis=(0, 1))


def boxes(mask):
    """Replace each component by its bounding rectangle (window and door openings are
    rectangles; the detected shapes are ragged). Done on a 3x3 tiling so components
    crossing the wrap seam stay whole; the centre tile is returned."""
    h, w = mask.shape
    tiled = np.tile(mask, (3, 3))
    lab, _n = ndimage.label(tiled)
    out = np.zeros_like(tiled)
    for sl in ndimage.find_objects(lab):
        if sl is not None:
            out[sl] = True
    return out[h:2 * h, w:2 * w]


# --- height methods -------------------------------------------------------------------------
def groove_height(lum, p):
    w = max(lum.shape)
    if p.get("normalise", "contrast") == "raw":
        # raw luminance: only joints that are really dark (or bright) count; contrast normalisation
        # would lift faint stains and drips to joint strength on heavily stained stone
        n = (lum - blur(lum, max(6.0, w / 32))) / (np.std(lum) + 1e-4)
    else:
        n = contrast_normalised(lum, max(6.0, w / 32))
    s = int(p["width"]) | 1
    if p["polarity"] == "dark":
        top = ndimage.grey_closing(n, size=(s, s), mode="wrap") - n
    else:
        top = n - ndimage.grey_opening(n, size=(s, s), mode="wrap")
    top = blur(top, 0.6)
    lines = np.zeros_like(top)
    for fp in line_footprints(p["line"], p.get("diagonal")):
        lines = np.maximum(lines, ndimage.grey_opening(top, footprint=fp, mode="wrap"))
    cover = p["cover"]
    t1 = np.percentile(lines, 100 * (1 - cover * 0.55))
    t0 = np.percentile(lines, 100 * (1 - cover * 1.6))
    soft = smoothstep(t0, t1, lines)
    core = drop_small(soft > 0.5, p["line"] * 1.5)
    core = ndimage.binary_dilation(core, iterations=2)
    recess = soft * core
    # close pin-gaps along mortar lines so joints do not dot-dash
    recess = np.maximum(recess, ndimage.grey_closing(recess, size=(3, 3), mode="wrap") * 0.9)
    recess = np.clip(blur(recess, p["bevel"]) * 1.15, 0, 1)
    h = 1.0 - (1.0 - p["floor"]) * recess
    # faces: only dark pores/chips sink a little, nothing rises above the face (top clamp)
    fine = lum - blur(lum, 3.0)
    fine /= np.std(fine) + 1e-4
    pores = np.clip(-fine / 2.5, 0, 1) * (1 - recess)
    h = h - p["pores"] * blur(pores, 0.7)
    return np.clip(h, 0, 1), recess


def feature_channel(img, p):
    """Signal in which the recess is high: luminance (dark or bright mortar) or, for
    orange brick on grey mortar, chroma (mortar = low saturation)."""
    rgb = np.asarray(img, np.float32) / 255.0
    if p.get("channel") == "chroma":
        f = rgb.max(2) - rgb.min(2)
        return -f if p["polarity"] == "dark" else f
    f = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    return -f if p["polarity"] == "dark" else f


def periodic_peaks(profile, spacing, z):
    """Wrap-aware local maxima at least `spacing` apart and z robust sigmas above median."""
    win = max(3, int(spacing) | 1)
    local = ndimage.maximum_filter1d(profile, win, mode="wrap")
    med = np.median(profile)
    sigma = 1.4826 * np.median(np.abs(profile - med)) + 1e-6
    return np.flatnonzero((profile >= local) & (profile > med + z * sigma))


def longest_runs(mask):
    """Per column, the longest vertical run of True (rows apart in the course interior)."""
    best = np.zeros(mask.shape[1], np.int32)
    cur = np.zeros(mask.shape[1], np.int32)
    for row in mask:
        cur = np.where(row, cur + 1, 0)
        best = np.maximum(best, cur)
    return best


def albedo_heads(zv, p):
    """Head joints of one course read from the albedo's own vertical edges: columns where a vertical
    ridge (second derivative across x, z-scored above `head_zt`) runs unbroken (gaps up to 3 rows)
    through at least `head_cov` of the course interior, at any spacing (stones are not on a regular
    brick period). zv: ridge z of the interior rows."""
    ww = zv.shape[1]
    near = ndimage.maximum_filter1d(zv, 3, axis=1, mode="wrap") > p.get("head_zt", 2.0)
    near = ndimage.binary_closing(np.pad(near, ((4, 4), (0, 0))), structure=np.ones((4, 1), bool))[4:-4]
    cover = longest_runs(near) / max(len(zv), 1)
    gap = max(5, int(p.get("head_gap", p["brick"] * 0.25)))
    local = ndimage.maximum_filter1d(cover, gap | 1, mode="wrap")
    strength = blur(zv.mean(0), 1.0)
    out = []
    for x in np.flatnonzero((cover >= local) & (cover >= p.get("head_cov", 0.6))):
        lo = np.arange(x - 2, x + 3) % ww
        out.append(int(lo[np.argmax(strength[lo])]))
    return sorted(set(out))


def masonry_height(img, lum, p):
    """Masonry is a grid: trace it instead of thresholding blobs.
    1. recess top-hat T of the feature channel (mortar high);
    2. bed joints: peaks of the row profile of T, about one course apart; each one is
       traced per column inside a +-band (follows wavy courses) and drawn continuous;
    3. head joints: per course, peaks of the column profile of T averaged over the
       course interior (a real joint is consistent over the whole course height, a
       stain or scratch is not), drawn from bed to bed;
    4. regions that are not brick (windows, doors) are found by colour distance from
       the wall's median colour and set back as a recess (window reveals)."""
    if p.get("vertical"):  # courses run vertically (plank columns, container posts)
        flipped = dict(p, vertical=False)
        h, recess = masonry_height(img.transpose(Image.Transpose.TRANSPOSE), lum.T, flipped)
        return h.T, recess.T
    course, brick, mortar = p["course"], p["brick"], p["mortar"]
    hh, ww = lum.shape
    f = contrast_normalised(feature_channel(img, p), max(8.0, course * 1.5))
    s = int(p["width"]) | 1
    t = ndimage.grey_closing(-f, size=(s, s), mode="wrap") + f
    t = blur(t, 0.6)
    k = max(2, int(round(course * 0.15)))
    mask = np.zeros((hh, ww), np.float32)

    rows = periodic_peaks(blur(t.mean(1), 0.8), course * 0.6, p.get("bed_z", 0.5))
    cols = np.arange(ww)
    offs = np.arange(-k, k + 1)
    traced = []
    for y0 in rows:
        band = t[(y0 + offs[:, None]) % hh, cols[None, :]]  # (2k+1, W)
        off = ndimage.median_filter(offs[np.argmax(band, 0)].astype(np.float32), size=15, mode="wrap")
        off = np.round(blur(off, 3.0)).astype(int)
        if p.get("straight"):  # machined seams: one offset for the whole line
            off = np.full_like(off, int(np.round(np.median(off))))
        traced.append(y0 + off)
        for d in range(-(mortar // 2), mortar - mortar // 2):
            mask[(y0 + off + d) % hh, cols] = 1.0
    heads = 0
    mode = p.get("heads", "periodic")
    zv = ridge_responses(img, p)[2] if mode == "albedo" else None
    for i, y0 in enumerate(rows):
        if mode in (False, "none"):
            break
        y1 = rows[(i + 1) % len(rows)] + (hh if i + 1 == len(rows) else 0)
        inner = np.arange(y0 + k + 1, y1 - k) % hh
        if len(inner) < 2:
            continue
        if mode == "albedo":
            peaks = albedo_heads(zv[inner], p)
        else:
            prof = blur(t[inner].mean(0), 0.8)
            peaks = periodic_peaks(prof, brick * 0.5, p.get("head_z", 2.5))
        for x0 in peaks:
            span = np.arange(y0, y1 + 1) % hh
            for d in range(-(mortar // 2), mortar - mortar // 2):
                mask[span, (x0 + d) % ww] = 1.0
            heads += 1
    recess = np.clip(blur(mask, p["bevel"]) * 1.2, 0, 1)
    h = 1.0 - (1.0 - p["floor"]) * recess
    if p.get("openings"):
        rgb = np.asarray(img, np.float32) / 255.0
        local = np.dstack([blur(rgb[..., c], 1.5) for c in range(3)])
        dist = np.linalg.norm(local - np.median(rgb.reshape(-1, 3), 0), axis=2)
        other = dist > p["openings"]
        # frames, glass and shards join into one opening; brick-sized specks drop out
        other = ndimage.binary_closing(np.pad(other, course * 2, mode="wrap"), iterations=max(2, course // 2))
        other = ndimage.binary_fill_holes(other)[course * 2:-course * 2, course * 2:-course * 2]
        other = ndimage.binary_opening(other, iterations=max(2, course // 2))
        other = boxes(drop_small(other, course * brick * 3))
        soft = np.clip(blur(other.astype(np.float32), 1.2) * 1.2, 0, 1)
        h = h * (1 - soft) + p.get("opening_height", 0.45) * soft
        recess = np.maximum(recess, soft)
    fine = lum - blur(lum, 3.0)
    fine /= np.std(fine) + 1e-4
    h = h - p["pores"] * blur(np.clip(-fine / 2.5, 0, 1) * (1 - recess), 0.7)
    return np.clip(h, 0, 1), recess


def plateau_height(lum, p):
    w = max(lum.shape)
    v = lum - blur(lum, w / 6)
    v = blur(v, p["smooth"])
    if p["polarity"] == "dark":
        v = -v
    o = int(p["open"]) | 1
    v = ndimage.grey_closing(ndimage.grey_opening(v, size=(o, o), mode="wrap"), size=(o, o), mode="wrap")
    if p.get("slot_line"):  # slots run vertically: fill dirt patches on the bars shorter than a slot
        v = ndimage.grey_closing(v, size=(int(p["slot_line"]) | 1, 1), mode="wrap")
    if p.get("grid"):  # exact n x n cell grid: average over the cell shifts, so per-cell dirt cancels out
        n = int(p["grid"])
        hh, ww = v.shape
        v = np.mean([np.roll(v, (round(i * hh / n), round(j * ww / n)), axis=(0, 1))
                     for i in range(n) for j in range(n)], axis=0)
    cover = p["cover"]  # fraction that is low
    lo, hi = np.percentile(v, [100 * cover * 0.6, 100 * min(cover * 1.4, 0.95)])
    top = smoothstep(lo, hi, v)
    top = blur(top, p["bevel"])
    h = p["floor"] + (1 - p["floor"]) * top
    fine = lum - blur(lum, 3.0)
    fine /= np.std(fine) + 1e-4
    h = h - p["pores"] * np.clip(-fine / 2.5, 0, 1)
    return np.clip(h, 0, 1), 1 - top


def rubble_height(lum, p):
    if p.get("polarity") == "dark":  # dark pieces in bright mortar (rubble-stone walls)
        lum = 1.0 - lum
    b = blur(lum, 1.0) - blur(lum, p["low"])
    lo, hi = np.percentile(b, [2, 98])
    h = np.clip((b - lo) / max(hi - lo, 1e-4), 0, 1)
    h = 1 - (1 - h) ** p["gamma"]  # widen the tops, keep crevices narrow
    x = h * p["top"]
    h = np.where(x < 0.8, x, 0.8 + 0.2 * np.tanh((x - 0.8) / 0.2))  # soft top clamp at 1.0
    return np.clip(h, 0, 1), 1 - h


METHODS = {"groove": groove_height, "plateau": plateau_height, "rubble": rubble_height}


def normal_from_height(h, lum, depth, detail):
    """Normal consistent with the parallax: height in texels is h * depth * size per axis.
    A fine luminance term (the first build's detail band) keeps faces from looking plastic."""
    hh, ww = h.shape
    dx, dy = wrap_gradient(blur(h, 0.5))
    sx, sy = depth * ww * 0.8, depth * hh * 0.8
    fine = blur(lum - blur(lum, max(4, ww / 24)), 0.8)
    fx, fy = wrap_gradient(fine)
    k = detail * (ww / 256.0) ** 0.5
    n = np.dstack([-(dx * sx + fx * k), dy * sy + fy * k, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n


def rmo_from_height(h, lum, material):
    """material: the builder's (normal strength, depth, roughness, metal, ao strength) tuple."""
    _s, _d, rough, metal, ao_k = material
    cavity = np.clip(blur(h, 3) - h, 0, 1)
    recess = 1 - h
    ao = np.clip(1.0 - ao_k * (cavity * 2.0 + recess * 0.6), 0.3, 1.0)
    r = np.clip(rough + recess * 0.06 + cavity * 0.2, 0.05, 1.0)
    m = np.full_like(r, metal)
    if metal:
        m = np.clip(metal * (0.4 + 1.2 * blur(lum, 2)), 0, 1)
    return np.dstack([r, m, ao])


def to_img(a, mode):
    img = Image.fromarray(np.uint8(np.clip(a, 0, 1) * 255 + 0.5))
    assert img.mode == mode, (img.mode, mode)
    return img


# --- builder API (the rend2 map builders' build_*.py) ----------------------------------------------
def capture_albedo(name, img):
    """PBR_CAPTURE_DIR=DIR saves each traced material's working albedo as DIR/<table>/<leaf>.png while a
    builder runs, so `check` can re-trace and overlay it without the builder's sources."""
    d = os.environ.get("PBR_CAPTURE_DIR")
    if d and name:
        path = Path(d) / name[0] / f"{name[1]}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        img.save(path)


def height_maps(diffuse, params, material, name=None):
    """Structure-aware maps for one parallax material.
    diffuse: PIL image; params: a table entry's method params (with "depth");
    material: the builder's (normal strength, depth, roughness, metal, ao strength) tuple.
    Returns (normal+height RGBA image, RMO RGB image, parallaxDepth, detail dict)."""
    img = working_image(diffuse)
    capture_albedo(name, img)
    lum = luminance(img)
    if params["method"] == "masonry":
        h, recess = masonry_height(img, lum, params)
    else:
        h, recess = METHODS[params["method"]](lum, params)
    depth = params["depth"]
    n = normal_from_height(h, lum, depth, material[0] * 0.6)
    nh_img = to_img(np.dstack([n * 0.5 + 0.5, h]), "RGBA")
    rmo_img = to_img(rmo_from_height(h, lum, material), "RGB")
    return nh_img, rmo_img, depth, {"img": img, "lum": lum, "h": h, "n": n, "recess": recess}


def v2_maps(table, leaf, diffuse, material):
    """A builder's hook for a material it would give parallax (or one the table adds parallax to).
    table: leaf -> (v2 class, params or None), e.g. URBAN_V2.
    Returns None when the material should be a plain normal map (the builder keeps its own
    normals and writes them as an RGB `_n` file), else (nh RGBA, rmo RGB, parallaxDepth).
    A leaf missing from the table is an error: every parallax material is classified."""
    if leaf not in table:
        raise KeyError(f"pbr_heights: unclassified parallax material {leaf!r}")
    _v2, params = table[leaf]
    if params is None:
        return None
    label = next((k for k, t in HEIGHT_TABLES.items() if t is table), "table")
    nh, rmo, depth, _detail = height_maps(diffuse, params, material, name=(label, leaf))
    return nh, rmo, depth


# --- alignment check (overlay + score) --------------------------------------------------------
# Does the traced recess follow the albedo's own joints? The evidence below is a plain ridge
# detector on the albedo (second derivative across the line), independent of every tracer above.
RIDGE_Z = 2.2      # a "strong albedo joint" pixel: ridge response this many robust sigmas over the median
SUPPORT_Z = 2.0    # a traced centreline pixel is "supported" if the ridge response nearby reaches this


def recess_polarity(p):
    """Which way the recess differs in luminance (plateau's polarity names its tops, so it flips)."""
    if p["method"] == "plateau":
        return "dark" if p["polarity"] == "bright" else "bright"
    return p.get("polarity", "dark")


def ridge_responses(img, p):
    """Robust z-scored ridge strength of the recess feature (recess high), per line direction:
    (f, z_h, z_v) = lines running along x (second derivative across y) and along y. Two fixed scales
    (1 and 2 px sigma, scale-normalised, best of both) so the evidence never depends on the tracer's
    own width settings."""
    q = dict(p, polarity=recess_polarity(p))
    f = feature_channel(img, q)
    out = []
    for order in ((2, 0), (0, 2)):
        best = None
        for sigma in (1.0, 2.0):
            r = -ndimage.gaussian_filter(f, sigma, order=order, mode="wrap") * sigma * sigma
            r = r - np.median(r)
            z = r / (1.4826 * np.median(np.abs(r)) + 1e-6)
            best = z if best is None else np.maximum(best, z)
        out.append(best)
    return f, out[0], out[1]


def line_open(mask, n, vertical):
    n = max(3, int(n) | 1)
    fp = np.ones((n, 1), bool) if vertical else np.ones((1, n), bool)
    return ndimage.binary_opening(np.pad(mask, n, mode="wrap"), structure=fp)[n:-n, n:-n]


def centreline(mask):
    """Medial pixels of a mask (local maxima of the distance transform inside it)."""
    pad = 16
    m = np.pad(mask, pad, mode="wrap")
    d = ndimage.distance_transform_edt(m)
    c = (d >= ndimage.maximum_filter(d, size=3)) & m
    return c[pad:-pad, pad:-pad]


def joint_width(f, centres, vertical):
    """FWHM in px of the mean feature profile across the given centreline pixels."""
    ys, xs = np.nonzero(centres)
    if len(ys) < 20:
        return None
    hh, ww = f.shape
    offs = np.arange(-10, 11)
    if vertical:
        prof = np.mean([f[ys, (xs + o) % ww] for o in offs], axis=1)
    else:
        prof = np.mean([f[(ys + o) % hh, xs] for o in offs], axis=1)
    base = np.median(np.concatenate([prof[:3], prof[-3:]]))
    peak = prof[8:13].max() - base
    if peak <= 1e-6:
        return None
    half = (prof - base) >= peak * 0.5
    c = 10 + int(np.argmax(prof[8:13])) - 2
    lo = hi = c
    while lo > 0 and half[lo - 1]:
        lo -= 1
    while hi < len(prof) - 1 and half[hi + 1]:
        hi += 1
    return float(hi - lo + 1)


def evidence(img, p):
    """(f, {tag: (z, strong mask, vertical)}): the albedo's own joint evidence per direction. A strong
    joint pixel has ridge z > RIDGE_Z and lies on a straight run long enough to be a joint (masonry: a
    third of a brick or a course; elsewhere half the groove line length)."""
    f, zh, zv = ridge_responses(img, p)
    out = {}
    for tag, z, vertical in (("h", zh, False), ("v", zv, True)):
        if p["method"] == "masonry":
            span = p["course"] * 0.35 if vertical else p["brick"] * 0.35
        else:
            span = int(p.get("line") or max(9, p.get("course", 20) * 0.35)) * 0.5
        span = max(5, min(span, 0.25 * img.size[1 if vertical else 0]))
        out[tag] = (z, line_open(z > RIDGE_Z, span, vertical), vertical)
    return f, out


def _score(joint, ev):
    res, tp_c, tp_s, n_c, n_s = {}, 0.0, 0.0, 0, 0
    for tag, (z, strong, vertical) in ev.items():
        mine = line_open(joint, 7, vertical)
        near = ndimage.binary_dilation(mine, iterations=2)
        cen = centreline(mine) & mine
        zmax = ndimage.maximum_filter(z, size=(1, 5) if vertical else (5, 1))
        cov = float((strong & near).sum() / max(strong.sum(), 1))
        sup = float((cen & (zmax > SUPPORT_Z)).sum() / max(cen.sum(), 1))
        res[tag] = dict(cov=cov, sup=sup, z=float(z[cen].mean()) if cen.any() else None,
                        n_alb=int(strong.sum()), n_tr=int(cen.sum()), cen=cen, strong=strong)
        tp_c += cov * strong.sum()
        n_c += strong.sum()
        tp_s += sup * cen.sum()
        n_s += cen.sum()
    cov, sup = tp_c / max(n_c, 1), tp_s / max(n_s, 1)
    return res, cov, sup, 2 * cov * sup / max(cov + sup, 1e-6)


def alignment(img, p, recess):
    """Score a trace against its albedo. recess: 0..1 map (1 = joint). Per direction (h = joints running
    along x, v = along y):
      cov    fraction of strong albedo joint pixels within 2 px of the traced joint (recall)
      sup    fraction of traced centreline pixels with albedo ridge evidence nearby (precision)
      z      mean ridge z under the traced centreline (0 = no better than any pixel, >3 = clear joint)
      w_alb  albedo joint width (FWHM of the mean profile over the strong joints); w_tr traced width
      n_alb  strong albedo joint pixels (how much evidence there is); n_tr traced centreline pixels
    Overall: cov and sup over both directions, f1 their harmonic mean, null_f1 the same for the trace
    shifted by a few unrelated offsets (what a joint grid of this density scores by chance), and
    score = (f1 - null_f1) / (1 - null_f1). None when the method has no joint lines (rubble)."""
    if p["method"] in ("rubble", "plateau"):  # no thin joint lines: judged on the overlay
        return None
    f, ev = evidence(img, p)
    joint = recess >= 0.5
    if not any(e[1].any() for e in ev.values()):
        return None  # the albedo shows no joint lines at this width
    res, cov, sup, f1 = _score(joint, ev)
    nulls = [_score(np.roll(joint, sh, axis=(0, 1)), ev)[3] for sh in ((11, 23), (-17, 31), (29, -13), (-23, -19))]
    null = float(np.mean(nulls))
    out = {}
    for tag, r in res.items():
        out[tag] = dict(cov=round(r["cov"], 3), sup=round(r["sup"], 3),
                        z=round(r["z"], 2) if r["z"] is not None else None,
                        w_alb=joint_width(f, centreline(ev[tag][1]) & ev[tag][1], ev[tag][2]),
                        w_tr=joint_width(joint.astype(np.float32), r["cen"], ev[tag][2]),
                        n_alb=r["n_alb"], n_tr=r["n_tr"])
    out.update(cov=round(float(cov), 3), sup=round(float(sup), 3), f1=round(float(f1), 3),
               null_f1=round(null, 3), score=round(float((f1 - null) / max(1 - null, 1e-6)), 3))
    return out


def overlay_image(img, recess, strong=None):
    """Albedo with the traced joints outlined in red (mask edge) and their centrelines in yellow; the
    albedo's own strong joint pixels not covered by the trace are marked cyan (missed)."""
    rgb = np.asarray(img.convert("RGB"), np.uint8).copy()
    joint = recess >= 0.5
    edge = joint & ~ndimage.binary_erosion(joint, iterations=1)
    rgb[edge] = (255, 0, 0)
    rgb[centreline(joint) & joint] = (255, 230, 0)
    if strong is not None:
        miss = strong & ~ndimage.binary_dilation(joint, iterations=2)
        rgb[miss] = (0, 255, 255)
    return Image.fromarray(rgb)


def check_material(img, params, material=(2.0, 0.0, 0.8, 0.0, 0.5)):
    """Trace one albedo and score it: returns (score dict or None, overlay image, traced detail)."""
    _nh, _rmo, _depth, d = height_maps(img, params, material)
    work = d["img"]
    sc = alignment(work, params, d["recess"])
    strong = None
    if sc is not None:
        strong = np.zeros(work.size[::-1], bool)
        for _z, s_, _v in evidence(work, params)[1].values():
            strong |= s_
    return sc, overlay_image(work, d["recess"], strong), d


# --- archive access ---------------------------------------------------------------------------
class Sources:
    def __init__(self, content):
        self.zips = []
        for n in SOURCE_PK3S:
            if (content / n).exists():
                z = zipfile.ZipFile(content / n)
                self.zips.append((z, {k.lower(): k for k in z.namelist()}))
        self.urban = zipfile.ZipFile(content / URBAN_PK3)

    def image(self, logical):
        for z, index in self.zips:
            for ext in (".png", ".tga", ".jpg", ".jpeg"):
                hit = index.get(logical.lower() + ext)
                if hit:
                    return Image.open(io.BytesIO(z.read(hit))), f"{Path(z.filename).name}:{hit}"
        raise FileNotFoundError(logical)


def parse_mtr(text):
    """name -> (start, end, body) for each top-level shader block (CRLF tolerant)."""
    out = {}
    for m in re.finditer(r"^(textures/xen_urb/\S+)\r?\n\{.*?\r?\n\}\r?\n", text, re.M | re.S):
        out[m.group(1)] = (m.start(), m.end(), m.group(0))
    return out


def rewrite_block(body, leaf, depth):
    nh = f"{NS}/{leaf}_nh.png"
    pat = re.compile(r"normalHeightMap " + re.escape(nh) + r"(\r?\n)(\s*)parallaxDepth \S+")
    if not pat.search(body):
        raise ValueError(f"unexpected parallax lines in {leaf}")
    if depth:
        return pat.sub(lambda m: f"normalHeightMap {nh}{m.group(1)}{m.group(2)}parallaxDepth {depth:g}", body)
    return pat.sub(f"normalMap {NS}/{leaf}_n.png", body)  # the regex consumed the line break


# --- preview ----------------------------------------------------------------------------------
def lit(diffuse_arr, n):
    light = np.array([-0.55, 0.55, 0.63])
    light /= np.linalg.norm(light)
    shade = np.clip((n * light).sum(2), 0, 1)
    return np.clip(diffuse_arr * (0.25 + 1.0 * shade[..., None]), 0, 1)


def preview_sheet(rows, path, tile=320):
    cols = ["diffuse", "old height (alpha)", "new height", "new normal", "diffuse x new normal"]
    head = 18
    sheet = Image.new("RGB", (tile * len(cols), (tile + head) * len(rows) + head), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for i, c in enumerate(cols):
        d.text((i * tile + 4, 3), c, fill=(255, 255, 255))
    for r, (label, images) in enumerate(rows):
        y = head + r * (tile + head)
        d.text((4, y + 3), label, fill=(255, 220, 80))
        for i, im in enumerate(images):
            im = im.convert("RGB")
            # show a centre crop at native resolution where possible (detail matters)
            w, h = im.size
            side = min(w, h, 512)
            im = im.crop(((w - side) // 2, (h - side) // 2, (w - side) // 2 + side, (h - side) // 2 + side))
            sheet.paste(im.resize((tile, tile), Image.Resampling.LANCZOS), (i * tile, y + head))
    sheet.save(path, quality=90)


# --- build ------------------------------------------------------------------------------------
def build_urban(out, content):
    out.mkdir(parents=True, exist_ok=True)
    src = Sources(content)
    raw = src.urban.read(MTR)
    text = raw.decode("ascii")
    blocks = parse_mtr(text)
    parallax = {name: b for name, b in blocks.items() if "parallaxDepth" in b[2]}
    unknown = sorted(n.rsplit("/", 1)[-1] for n in parallax if n.rsplit("/", 1)[-1] not in URBAN_V2)
    if unknown:
        raise SystemExit(f"unclassified parallax materials: {unknown}")

    files, report, previews = {}, [], {}
    new_text = text
    for name in sorted(parallax, key=lambda n: blocks[n][0], reverse=True):
        start, end, body = blocks[name]
        leaf = name.rsplit("/", 1)[-1]
        v2, params = URBAN_V2[leaf]
        diffuse_name = re.search(r"^\s*map (\S+)", body, re.M).group(1)
        old_depth = float(re.search(r"parallaxDepth (\S+)", body).group(1))
        v1 = classify_v1(diffuse_name)
        old_nh = Image.open(io.BytesIO(src.urban.read(f"{NS}/{leaf}_nh.png"))).convert("RGBA")
        entry = {"shader": name, "diffuse": diffuse_name, "v1_class": v1, "v2_class": v2,
                 "old_depth": old_depth, "why": V2_CLASSES[v2]["why"]}
        old_a = np.asarray(old_nh, np.float32)[..., 3] / 255
        entry["old_height"] = {"min": round(float(old_a.min()), 3), "max": round(float(old_a.max()), 3),
                               "mean": round(float(old_a.mean()), 3),
                               "effective_max_disp_uv": round(old_depth * float(old_a.max() - old_a.min()), 4)}
        if params is None:
            files[f"{NS}/{leaf}_n.png"] = old_nh.convert("RGB")  # identical normals, alpha dropped
            entry.update(new_depth=None, parallax=False, files=[f"{NS}/{leaf}_n.png"])
        else:
            diffuse, origin = src.image(diffuse_name)
            nh_img, rmo_img, depth, detail = height_maps(diffuse, params, V1_MATERIALS[v1])
            img, h, n = detail["img"], detail["h"], detail["n"]
            files[f"{NS}/{leaf}_nh.png"] = nh_img
            files[f"{NS}/{leaf}_rmo.png"] = rmo_img
            entry.update(new_depth=depth, parallax=True, method=params["method"], source=origin,
                         size=list(img.size), files=[f"{NS}/{leaf}_nh.png", f"{NS}/{leaf}_rmo.png"],
                         params={k: v for k, v in params.items() if k != "depth"},
                         new_height={"min": round(float(h.min()), 3), "max": round(float(h.max()), 3),
                                     "mean": round(float(h.mean()), 3),
                                     "flat_top_fraction": round(float((h > 0.95).mean()), 3),
                                     "recess_fraction": round(float((h < 0.6).mean()), 3),
                                     "effective_max_disp_uv": round(depth * float(h.max() - h.min()), 4)})
            if leaf in PREVIEW:
                dif = np.asarray(img, np.float32) / 255
                old_res = old_nh.resize(img.size, Image.Resampling.BILINEAR)
                previews[leaf] = (f"{leaf}  [{v2}] depth {old_depth:g} -> {depth:g}",
                                  [img, old_res.getchannel("A"), to_img(h, "L"), to_img(n * 0.5 + 0.5, "RGB"),
                                   to_img(lit(dif, n), "RGB")])
        new_text = new_text[:start] + rewrite_block(body, leaf, entry["new_depth"]) + new_text[end:]
        report.append(entry)

    report.sort(key=lambda e: e["shader"])
    pack = out / PACK_NAME
    stamp = (2026, 10, 8, 0, 0, 0)  # fixed zip timestamps: deterministic pack bytes
    with zipfile.ZipFile(pack, "w", zipfile.ZIP_DEFLATED) as z:
        info = zipfile.ZipInfo(MTR, stamp)
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, new_text.encode("ascii"))
        for path in sorted(files):
            buf = io.BytesIO()
            files[path].save(buf, "PNG", optimize=True)
            info = zipfile.ZipInfo(path, stamp)
            info.compress_type = zipfile.ZIP_STORED  # PNG is already deflated
            z.writestr(info, buf.getvalue())
    sheets = []
    order = [k for k in PREVIEW if k in previews]
    for i in range(0, len(order), 6):
        p = out / f"preview_{i // 6 + 1}.jpg"
        preview_sheet([previews[k] for k in order[i:i + 6]], p)
        sheets.append(p.name)
    summary = {}
    for e in report:
        s = summary.setdefault(e["v2_class"], {"count": 0, "parallax": e["parallax"], "old_depths": set(),
                                               "new_depths": set()})
        s["count"] += 1
        s["old_depths"].add(e["old_depth"])
        s["new_depths"].add(e["new_depth"])
    summary = {k: dict(v, old_depths=sorted(v["old_depths"]),
                       new_depths=sorted(d for d in v["new_depths"] if d is not None))
               for k, v in summary.items()}
    (out / "report.json").write_text(json.dumps({"pack": PACK_NAME, "materials": report, "classes": summary,
                                                 "previews": sheets}, indent=1), encoding="utf-8")
    return pack, summary


def run_check(albedo_dir, out, only=(), before=None):
    """Overlay + alignment score for every captured albedo whose table entry has a trace."""
    out.mkdir(parents=True, exist_ok=True)
    rows = {}
    for table_name, table in HEIGHT_TABLES.items():
        for leaf, (_v2, params) in sorted(table.items()):
            key = f"{table_name}/{leaf}"
            src = albedo_dir / table_name / f"{leaf}.png"
            if params is None or not src.exists() or (only and key not in only):
                continue
            img = Image.open(src).convert("RGB")
            sc, ov, d = check_material(img, params)
            rows[key] = sc
            dest = out / table_name
            dest.mkdir(exist_ok=True)
            ov.save(dest / f"{leaf}_overlay.png")
            side = min(512, *img.size)
            sheet = Image.new("RGB", (side * 2, side))
            sheet.paste(img.crop((0, 0, side, side)), (0, 0))
            sheet.paste(ov.crop((0, 0, side, side)), (side, 0))
            sheet.save(dest / f"{leaf}_crop.png")
            if sc is None:
                print(f"{key:28s} method {params['method']}: no joint lines to score")
            else:
                print(f"{key:26s} score {sc['score']:5.2f} (f1 {sc['f1']:.2f} null {sc['null_f1']:.2f}) cov {sc['cov']:.2f} sup {sc['sup']:.2f} | "
                      f"h cov {sc['h']['cov']:.2f} sup {sc['h']['sup']:.2f} z {sc['h']['z']} w {sc['h']['w_alb']}/{sc['h']['w_tr']}"
                      f" | v cov {sc['v']['cov']:.2f} sup {sc['v']['sup']:.2f} z {sc['v']['z']} w {sc['v']['w_alb']}/{sc['v']['w_tr']}")
    (out / "scores.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    if before:
        changed_sheet(albedo_dir, out, before, rows)
    return rows


def changed_sheet(albedo_dir, out, before, rows, side=512):
    """albedo | before-trace overlay | after-trace overlay for every material whose trace changed; a material
    that became a plain normal map shows a grey tile. Scores in the label."""
    old_rows = json.loads((before / "scores.json").read_text(encoding="utf-8")) if (before / "scores.json").exists() else {}
    items = []
    for key in sorted(set(old_rows) | set(rows)):
        t, leaf = key.split("/")
        a, b = before / t / f"{leaf}_overlay.png", out / t / f"{leaf}_overlay.png"
        if not a.exists() or (b.exists() and a.read_bytes() == b.read_bytes()):
            continue
        items.append((key, albedo_dir / t / f"{leaf}.png", a, b if b.exists() else None))
    head = 18
    sheet = Image.new("RGB", (side * 3, (side + head) * max(len(items), 1)), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    fmt = lambda sc: "n/a" if not sc else f"score {sc['score']:.2f} cov {sc['cov']:.2f} sup {sc['sup']:.2f} " \
        f"w {sc['h']['w_alb']}/{sc['h']['w_tr']} {sc['v']['w_alb']}/{sc['v']['w_tr']}"
    for r, (key, alb, a, b) in enumerate(items):
        y = r * (side + head)
        d.text((4, y + 3), f"{key}   before: {fmt(old_rows.get(key))}   after: " +
               (fmt(rows.get(key)) if b else "plain normal map (no trace)"), fill=(255, 220, 80))
        for i, path in enumerate((alb, a, b)):
            if path is None:
                d.rectangle((i * side, y + head, (i + 1) * side - 1, y + head + side - 1), fill=(90, 90, 90))
                continue
            im = Image.open(path).convert("RGB")
            sheet.paste(im.crop((0, 0, min(side, im.width), min(side, im.height))), (i * side, y + head))
    sheet.save(out / "changed_sheet.jpg", quality=92)
    print(f"changed_sheet.jpg: {len(items)} materials")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build-urban", help="write the siege_urban_pbr v2 override pack")
    b.add_argument("--out", type=Path, default=OUT)
    b.add_argument("--content", type=Path, default=CONTENT, help="folder holding the source pk3s (read only)")
    c = sub.add_parser("check", help="overlay the traced joints on each albedo and score the alignment")
    c.add_argument("albedo", type=Path, help="DIR/<table>/<leaf>.png as written by PBR_CAPTURE_DIR during a build")
    c.add_argument("--out", type=Path, required=True)
    c.add_argument("--only", default="", help="comma-separated table/leaf names, e.g. urban/wall3")
    c.add_argument("--before", type=Path, help="an earlier check output dir: also write changed_sheet.jpg "
                   "(albedo | before | after, one row per material whose trace changed or was dropped)")
    args = ap.parse_args(argv)
    if args.cmd == "check":
        return run_check(args.albedo, args.out, [x for x in args.only.split(",") if x], args.before)
    if args.cmd == "build-urban":
        pack, summary = build_urban(args.out, args.content)
        print(json.dumps({"pack": str(pack), "classes": summary}, indent=1))


if __name__ == "__main__":
    main()
