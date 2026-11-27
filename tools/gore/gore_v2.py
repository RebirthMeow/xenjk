"""v2 blood art (cg_goreLook 2) for make_gore_assets.py.

Real stains are thick, dark and opaque in the middle and thin, lighter and see-through at
the edge. Spatter has many small drops, elongated ones with tails pointing away from the
wound, and specks. v2 textures carry opacity in alpha (from a thickness field) and a shade
in rgb (thicker is darker, mottled), which multiplies cg_gore.c's lit blood colour. The wet
shine is cg_gore.c's job, through the *_wet shaders written here.
"""
import math

import numpy as np

from make_gore_assets import coords, fbm, smoothstep, png_bytes


DARKEN = 0.8     # v2 blood a little darker than goreFresh: real spatter reads maroon


def warp_field(xx, yy, rng, amount=0.35, base=3):
    h, w = xx.shape
    wx = (fbm((h, w), rng, base=base) - 0.5) * 2 * amount
    wy = (fbm((h, w), rng, base=base) - 0.5) * 2 * amount
    return xx + wx, yy + wy


def thick_blob(xx, yy, cx, cy, r, warp=None):
    """Thickness 0..1 of a pool: domain-warped distance, dome-shaped profile."""
    x, y = (xx, yy) if warp is None else warp
    d2 = ((x - cx) ** 2 + (y - cy) ** 2) / (r * r)
    return np.clip(1 - d2, 0, 1) ** 0.6


def drop_with_tail(xx, yy, cx, cy, r, angle, tail):
    """A drop that hit at a slant: round head, a tail narrowing along `angle`."""
    ca, sa = math.cos(angle), math.sin(angle)
    u = (xx - cx) * ca + (yy - cy) * sa        # along travel
    v = -(xx - cx) * sa + (yy - cy) * ca
    head = np.clip(1 - (u * u + v * v) / (r * r), 0, 1) ** 0.6
    along = np.clip(u / (tail * r + 1e-6), 0, 1)
    half = r * (1 - along) ** 1.4 * 0.75
    body = np.clip(1 - np.abs(v) / (half + 1e-6), 0, 1) * (u > 0) * (u < tail * r)
    return np.maximum(head, body * 0.8 * (1 - along * 0.5))


def finish(thick, rng, edge_soft=0.08):
    """thickness -> (shade rgb multiplier, alpha)."""
    h, w = thick.shape
    grain = fbm((h, w), rng, base=10, octaves=3)
    mottle = fbm((h, w), rng, base=3, octaves=3)
    t = np.clip(np.clip(thick, 0, 1) * (0.85 + 0.3 * grain), 0, 1) * (thick > 0.002)
    # thin film translucent, body nearly opaque, a soft fringe a pixel or two wide
    alpha = smoothstep(0.0, edge_soft, t) * (0.38 + 0.6 * smoothstep(0.02, 0.45, t))
    # a faint darker ring where the edge dries first
    ring = smoothstep(0.0, 0.06, t) * (1 - smoothstep(0.06, 0.2, t))
    shade = (1.15 - 0.45 * smoothstep(0.1, 0.9, t) - 0.12 * ring) * (0.85 + 0.3 * mottle)
    rgb = np.stack([shade, shade * 0.92, shade * 0.9], -1)
    b = max(2, h // 64)
    alpha[:b, :] = alpha[-b:, :] = 0
    alpha[:, :b] = alpha[:, -b:] = 0
    return np.clip(rgb * DARKEN / 1.25, 0, 1), np.clip(alpha, 0, 1)


# ---- large decals: one per spot, painted from many small drops (cg_goreLook 2) ----
#
# Real spatter is hundreds of specks, clumps and thin drips, not a few solid shapes, and
# it covers a metre or so. Each texture below is one whole spot, stamped speck by speck in
# pixel space (a small window per speck, so hundreds stay cheap).

def stamp(thick, cx, cy, r, val, angle=0.0, tail=0.0):
    """A speck of radius r px at (cx, cy); with tail > 0 it is a drop stretched along angle."""
    h, w = thick.shape
    reach = r * (1 + max(tail, 0)) + 2
    x0, x1 = int(max(0, cx - reach)), int(min(w, cx + reach + 1))
    y0, y1 = int(max(0, cy - reach)), int(min(h, cy + reach + 1))
    if x0 >= x1 or y0 >= y1:
        return
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(float)
    xx += 0.5 - cx
    yy += 0.5 - cy
    if tail > 0:
        piece = drop_with_tail(xx, yy, 0, 0, r, angle, tail)
    else:
        d = np.sqrt(xx * xx + yy * yy) / max(r, 0.35)
        piece = np.clip(1.25 - d, 0, 1) ** 0.6       # a little soft so sub-pixel specks stay faint
    thick[y0:y1, x0:x1] = np.maximum(thick[y0:y1, x0:x1], piece * val)


def haze(thick, rng, n, cx, cy, sx, sy):
    """The finest mist: sub-pixel specks that only tint the surface."""
    for _ in range(n):
        stamp(thick, rng.normal(cx, sx), rng.normal(cy, sy), rng.uniform(0.3, 0.7), rng.uniform(0.3, 0.6))


def speck_radius(rng, big=1.0):
    """Mostly tiny, a few bigger: the size spread of spatter."""
    return min(9.0 * big, 0.55 * rng.lognormal(0.35, 0.65) * big)


def mist(size, rng):
    """Floor spot: a mist of specks in clumps, denser at the middle, slanted drops on the rim."""
    thick = np.zeros((size, size))
    c = size / 2
    clumps = [(c + rng.normal(0, size * 0.12), c + rng.normal(0, size * 0.12), rng.uniform(0.04, 0.1) * size)
              for _ in range(rng.integers(4, 9))]
    haze(thick, rng, 2500, c, c, size * 0.2, size * 0.2)
    for _ in range(rng.integers(1200, 1800)):
        if rng.random() < 0.6:
            cx, cy, spread = clumps[rng.integers(len(clumps))]
            x, y = rng.normal(cx, spread), rng.normal(cy, spread)
        else:
            a = rng.uniform(0, 2 * math.pi)
            d = size * 0.44 * rng.random() ** 0.8
            x, y = c + d * math.cos(a), c + d * math.sin(a)
        dist = math.hypot(x - c, y - c) / (size * 0.5)
        r = speck_radius(rng, 1.2 - 0.6 * dist)
        val = rng.uniform(0.55, 0.95)
        if dist > 0.35 and rng.random() < 0.5:     # outer drops landed at a slant: tails point away
            stamp(thick, x, y, r, val, math.atan2(y - c, x - c), rng.uniform(1.5, 3 + 3 * dist))
        else:
            stamp(thick, x, y, r, val)
    for _ in range(rng.integers(1, 4)):          # a couple of heavier drops, irregular, not round pools
        x, y = rng.normal(c, size * 0.08), rng.normal(c, size * 0.08)
        for _ in range(rng.integers(3, 7)):
            stamp(thick, x + rng.normal(0, 4), y + rng.normal(0, 4), rng.uniform(2.5, 6), rng.uniform(0.7, 0.95))
    return finish(thick, rng, edge_soft=0.05)


def spray(w, h, rng):
    """Directional floor spray: the hit at t = 0.1, drops thrown down the texture in a fan."""
    thick = np.zeros((h, w))
    x0, y0 = w / 2, h * 0.1
    fan = rng.uniform(0.25, 0.45)
    haze(thick, rng, 2000, x0, y0 + h * 0.25, w * 0.15, h * 0.22)
    for _ in range(rng.integers(1000, 1500)):
        a = math.pi / 2 + rng.normal(0, fan)
        d = h * 0.85 * rng.random() ** 1.3
        x, y = x0 + d * math.cos(a), y0 + d * math.sin(a)
        far = d / (h * 0.85)
        r = speck_radius(rng, 1.15 - 0.6 * far)
        val = rng.uniform(0.55, 0.95)
        if rng.random() < 0.3 + 0.6 * far:
            stamp(thick, x, y, r, val, a, rng.uniform(1.5, 2 + 6 * far))
        else:
            stamp(thick, x, y, r, val)
    for _ in range(rng.integers(25, 50)):         # dense specks at the hit
        stamp(thick, rng.normal(x0, w * 0.06), rng.normal(y0 + 6, h * 0.02), rng.uniform(1.5, 4), rng.uniform(0.7, 0.95))
    return finish(thick, rng, edge_soft=0.05)


def drips(w, h, rng):
    """Wall spot: a ragged smear of specks at t = 0.2 and many thin drips running down from it."""
    thick = np.zeros((h, w))
    x0, y0 = w / 2, h * 0.2
    sw, sh = w * rng.uniform(0.18, 0.3), h * rng.uniform(0.035, 0.06)
    heavy = []
    haze(thick, rng, 1500, x0, y0, sw * 1.4, sh * 1.8)
    for _ in range(rng.integers(500, 800)):
        x, y = rng.normal(x0, sw), rng.normal(y0, sh)
        if abs(x - x0) > w * 0.46:
            continue
        core = math.exp(-((x - x0) / sw) ** 2 - ((y - y0) / sh) ** 2)
        r = speck_radius(rng, 0.6 + 0.9 * core)
        stamp(thick, x, y, r, rng.uniform(0.55, 0.95))
        if r > 1.6 and core > 0.25:
            heavy.append((x, y, r))
    for _ in range(rng.integers(150, 300)):        # spatter thrown wider, a little elongated outward
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(1.2, 3.2)
        x, y = x0 + d * sw * math.cos(a), y0 + d * sh * 2.2 * math.sin(a)
        stamp(thick, x, y, speck_radius(rng, 0.6), rng.uniform(0.5, 0.9), a, rng.uniform(0, 2.5))
    rng.shuffle(heavy)
    for (x, y, r) in heavy[:rng.integers(6, 14)]:  # drips start where the blood is thick
        length = h * rng.uniform(0.08, 0.7) * (0.5 + 0.5 * min(r, 4) / 4)
        width = rng.uniform(0.7, 1.8) * (0.7 + 0.3 * min(r, 4) / 4)
        phase, freq, amp = rng.uniform(0, 6), rng.uniform(0.01, 0.04), rng.uniform(0.3, 1.5)
        steps = int(length)
        for k in range(steps):
            t = k / max(steps, 1)
            wob = 1 + 0.25 * math.sin(k * 0.21 + phase)     # drips thicken and thin as they run
            stamp(thick, x + amp * math.sin(phase + freq * k) + 0.6 * math.sin(k * 0.07 + 2 * phase), y + k,
                  width * wob * (1 - 0.55 * t) + 0.2, 0.8 - 0.3 * t)
        if rng.random() < 0.35:                      # a small bead where it stopped
            stamp(thick, x + amp * math.sin(phase + freq * steps), y + steps, width * 1.5, 0.85)
    return finish(thick, rng, edge_soft=0.05)


def body(size, rng):
    xx, yy = coords(size, size)
    warp = warp_field(xx, yy, rng, 0.25, 3)
    thick = thick_blob(xx, yy, 0, 0, 0.4, warp)
    for _ in range(14):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.45, 0.85)
        thick = np.maximum(thick, thick_blob(xx, yy, d * math.cos(a), d * math.sin(a), rng.uniform(0.015, 0.05)) * 0.7)
    return finish(thick, rng)


def periodic_noise(size, rng, cutoff, aniso=1.0):
    """Smooth noise that tiles: random spectrum, low-passed, in 0..1."""
    f = np.fft.fftfreq(size)[:, None] * aniso, np.fft.fftfreq(size)[None, :]
    k = np.sqrt(f[0] ** 2 + f[1] ** 2)
    spec = (rng.normal(size=(size, size)) + 1j * rng.normal(size=(size, size))) * np.exp(-(k / cutoff) ** 2)
    n = np.real(np.fft.ifft2(spec))
    return (n - n.min()) / (n.max() - n.min() + 1e-9)


def cap(size, rng):
    """The cut face of a severed limb (replaces the stock charred, glowing cap): raw red
    muscle with fibres, darker clotted patches and a few pale specks of fat. It tiles, as
    the cap surfaces wrap it at any scale. Albedo, lit like the model (rgbGen lightingDiffuse)."""
    fibre = periodic_noise(size, rng, 0.08, aniso=4.0)          # muscle grain, one way
    mottle = periodic_noise(size, rng, 0.03)
    clot = np.clip((periodic_noise(size, rng, 0.02) - 0.55) * 4, 0, 1)
    fat = np.clip((periodic_noise(size, rng, 0.12) - 0.8) * 6, 0, 1)
    raw = np.array([0.50, 0.07, 0.06])
    dark = np.array([0.20, 0.025, 0.02])
    pale = np.array([0.62, 0.30, 0.26])
    shade = 0.75 + 0.35 * fibre[..., None] + 0.2 * (mottle[..., None] - 0.5)
    col = raw * shade
    col = col * (1 - clot[..., None]) + dark * clot[..., None]
    col = col * (1 - 0.6 * fat[..., None]) + pale * 0.6 * fat[..., None]
    return np.clip(col, 0, 1)


def chunk_tex(size, rng):
    """Torn flesh for the meat chunks: higher contrast than the cap, so a lump a few units
    across still reads as meat in game light. Raw red fibres over dark maroon, near-black
    clots in the hollows, pale patches of fat. It tiles."""
    fibre = periodic_noise(size, rng, 0.1, aniso=3.0)
    mottle = periodic_noise(size, rng, 0.04)
    clot = np.clip((periodic_noise(size, rng, 0.035) - 0.5) * 3.5, 0, 1)
    fat = np.clip((periodic_noise(size, rng, 0.06, aniso=2.0) - 0.62) * 5, 0, 1)
    raw = np.array([0.62, 0.09, 0.08])
    maroon = np.array([0.26, 0.03, 0.03])
    black = np.array([0.07, 0.01, 0.01])
    pale = np.array([0.80, 0.62, 0.52])
    t = np.clip(fibre * 0.8 + 0.4 * (mottle - 0.5), 0, 1)[..., None]
    col = maroon * (1 - t) + raw * t
    col = col * (1 - clot[..., None] * 0.85) + black * clot[..., None] * 0.85
    col = col * (1 - 0.75 * fat[..., None]) + pale * 0.75 * fat[..., None]
    return np.clip(col, 0, 1)


def pool_tex(size, rng):
    """The surface of a blood pool, tiled under cg_gore_pool.c's vertex colours: nearly
    even, as liquid is, with a faint mottle (thicker patches, skin forming as it sits).
    The pool's edge is cut as geometry in cg_gore_pool.c, so alpha is opaque."""
    mottle = periodic_noise(size, rng, 0.03)
    fine = periodic_noise(size, rng, 0.12)
    # darker, thicker streaks where it ran and began to clot, drawn out one way and the other
    streak = np.maximum(periodic_noise(size, rng, 0.06, aniso=3.0), periodic_noise(size, rng, 0.06, aniso=1 / 3.0))
    clot = smoothstep(0.62, 0.85, streak)
    shade = 0.8 + 0.2 * mottle + 0.05 * (fine - 0.5) - 0.22 * clot
    return np.stack([shade, shade * 0.93, shade * 0.92], -1).clip(0, 1), np.ones((size, size))


def pool_env(size, rng):
    """What a pool reflects, for the wet pass: no real room, just the shape of one, mostly
    dark with a few bright lights, so gloss shows as highlights that slide over the pool as
    the view moves rather than as a milky wash. A dome seen from below: cg_gore_pool.c maps
    the reflected ray's horizontal part to the texture (straight up is the centre, the
    horizon the rim), so the ceiling's lights are in the middle and dim walls round the edge.
    The vertex colour (Fresnel and the grid's light) sets its strength."""
    xx, yy = coords(size, size)
    rad = np.sqrt(xx ** 2 + yy ** 2)
    walls = smoothstep(0.55, 1.0, rad) * 0.08 * (0.7 + 0.6 * periodic_noise(size, rng, 0.06))
    img = 0.005 + 0.015 * periodic_noise(size, rng, 0.04) + walls
    for i in range(14):                             # lights: small and bright, long panels and round lamps
        a, d = rng.uniform(0, 2 * math.pi), math.sqrt(rng.uniform(0.0, 0.6))
        cx, cy = d * math.cos(a), d * math.sin(a)
        r = rng.uniform(0.007, 0.018)
        long = rng.uniform(1.0, 5.0)
        q = ((xx - cx) / (r * long)) ** 2 + ((yy - cy) / r) ** 2
        # a soft core and a faint glow: solid white shapes, cut by the pool's edge, read as scraps of paper
        img += 0.65 * np.exp(-q ** 1.5) + 0.1 * np.exp(-q / 8)
    img = img.clip(0, 1)
    return np.stack([img, img * 0.97, img * 0.95], -1), np.ones((size, size))


def footprint(w, h, rng, load, left=False):
    """A bloody shoe print, heel at the top row and toe at the bottom (cg_gore.c lays it
    with t along the walk). load 1 is a sole fresh from a pool; lower loads print thinner
    and patchier, the tread showing through as the blood wears off."""
    xx, yy = coords(h, w)              # x across -1..1, y heel -1 .. toe 1
    if left:
        xx = -xx
    xx, yy = warp_field(xx, yy, rng, amount=0.04, base=4)
    v = (yy + 1) / 2                   # 0 heel .. 1 toe
    # the sole's outline: half width and centre along its length (right foot: the big toe
    # and the arch on the -x side, so the waist sits toward +x)
    vs = [0.02, 0.06, 0.15, 0.27, 0.36, 0.48, 0.62, 0.75, 0.87, 0.94, 0.985]
    ws = [0.0, 0.40, 0.52, 0.50, 0.36, 0.42, 0.68, 0.72, 0.60, 0.40, 0.0]
    cs = [0.06, 0.06, 0.06, 0.08, 0.2, 0.12, -0.02, -0.04, -0.08, -0.12, -0.12]
    half = np.interp(v, vs, ws) * 0.95
    centre = np.interp(v, vs, cs)
    sole = smoothstep(0.0, 0.08, half - np.abs(xx - centre))
    # the waist barely touches the ground on the inner side
    waist = smoothstep(0.27, 0.36, v) * (1 - smoothstep(0.5, 0.6, v))
    sole *= 1 - waist * (1 - smoothstep(centre - 0.05, centre + 0.25, xx)) * 0.85
    # tread: blocky lugs on the forefoot, bars on the heel; blood sits on the lugs
    lugs = smoothstep(-0.5, -0.1, np.sin(xx * math.pi * 3.2 + 0.4)) * smoothstep(-0.4, 0.0, np.sin(v * 2 * math.pi * 13))
    bars = smoothstep(-0.3, 0.1, np.sin(v * 2 * math.pi * 16))
    tread = np.where(v < 0.32, bars, lugs)
    rim = sole * (1 - smoothstep(0.03, 0.12, half - np.abs(xx - centre)))
    film = sole * (0.3 + 0.7 * np.maximum(tread, rim * 0.8))
    keep = smoothstep((1 - load) * 0.75 - 0.08, (1 - load) * 0.75 + 0.08, fbm((h, w), rng, base=6, octaves=3))
    thick = film * keep * (0.4 + 0.5 * load)
    return finish(thick, rng, edge_soft=0.05)


def rgba_png(rgb, alpha):
    c = np.empty(alpha.shape + (4,), np.uint8)
    c[..., :3] = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
    c[..., 3] = (np.clip(alpha, 0, 1) * 255 + 0.5).astype(np.uint8)
    return png_bytes(c)


def wet_shader(name, image):
    """Additive shine masked by the mark's own alpha; cg_gore.c writes view-dependent vertex colours."""
    return f"""{name}
{{
	polygonOffset
	surfaceparm nomarks
	surfaceparm trans
	q3map_nolightmap
	{{
		clampmap {image}
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}}
}}
"""
