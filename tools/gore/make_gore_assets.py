"""Generate the blood art for cgame/cg_gore.c and pack it as zz_xenjk_gore.pk3.

Everything is procedural (numpy noise and shapes), so the pack is ours to use
and can be rebuilt at any time. World marks and flying drops are alpha blended:
their textures are white with coverage in alpha, and cg_gore.c colours them
with the blood's albedo times the light grid at the spot. The body mark is lit
like the model it sits on (rgbGen lightingDiffuse), so its albedo is baked in.

    python tools/gore/make_gore_assets.py [--out build/gore/zz_xenjk_gore.pk3] [--seed 7] [--preview]

--preview also writes a contact sheet PNG beside the pack.
"""

import argparse
import io
import math
import os
import zipfile

import numpy as np
from PIL import Image

# fresh blood albedo (cg_gore.c goreFresh), baked into the body mark
FRESH = np.array([0.40, 0.025, 0.02])


def value_noise(shape, cells, rng):
    """Smooth value noise in 0..1 at roughly `cells` cells across."""
    h, w = shape
    gy, gx = cells + 2, int(cells * w / h) + 2
    grid = rng.random((gy, gx))
    y = np.linspace(0, cells, h, endpoint=False)
    x = np.linspace(0, cells * w / h, w, endpoint=False)
    yi, xi = np.floor(y).astype(int), np.floor(x).astype(int)
    fy, fx = y - yi, x - xi
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = grid[yi][:, xi]
    b = grid[yi][:, xi + 1]
    c = grid[yi + 1][:, xi]
    d = grid[yi + 1][:, xi + 1]
    top = a + (b - a) * fx[None, :]
    bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def fbm(shape, rng, base=4, octaves=4):
    out = np.zeros(shape)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        out += amp * value_noise(shape, base * 2 ** o, rng)
        total += amp
        amp *= 0.5
    return out / total


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def coords(h, w):
    y, x = np.mgrid[0:h, 0:w].astype(float)
    return (x + 0.5) / w * 2 - 1, (y + 0.5) / h * 2 - 1   # -1..1 both ways


def blob(xx, yy, cx, cy, r, rng, wobble=0.18, harmonics=6, stretch=1.0, angle=0.0):
    """Coverage of one irregular blob: a circle whose radius wobbles with angle."""
    dx, dy = xx - cx, yy - cy
    ca, sa = math.cos(angle), math.sin(angle)
    u = (dx * ca + dy * sa) / stretch
    v = -dx * sa + dy * ca
    theta = np.arctan2(v, u)
    rr = np.ones_like(theta)
    for k in range(2, 2 + harmonics):
        rr += wobble / k * rng.uniform(0.4, 1.0) * np.sin(k * theta + rng.uniform(0, 2 * math.pi))
    d = np.sqrt(u * u + v * v) / (r * rr)
    return 1 - smoothstep(0.85, 1.0, d)


def finish(cov, rng, rim=0.25):
    """Interior variation plus a darker drying rim, and a clean zero border."""
    h, w = cov.shape
    inner = 0.82 + 0.18 * fbm(cov.shape, rng, base=6)
    edge = cov * (1 - cov) * 4          # peaks where coverage crosses 0.5
    out = np.clip(cov * inner + rim * edge, 0, 1)
    b = max(2, h // 64)
    out[:b, :] = out[-b:, :] = 0
    out[:, :b] = out[:, -b:] = 0
    return out


def splat(size, rng):
    xx, yy = coords(size, size)
    cov = blob(xx, yy, 0, 0, rng.uniform(0.32, 0.42), rng, wobble=0.35)
    # a spatter tail off one side: drops get smaller and longer further out
    tail = rng.uniform(0, 2 * math.pi)
    for _ in range(rng.integers(14, 26)):
        a = tail + rng.normal(0, 0.7)
        dist = rng.uniform(0.38, 0.9)
        r = 0.09 * (1 - dist) + rng.uniform(0.008, 0.03)
        cov = np.maximum(cov, blob(xx, yy, dist * math.cos(a), dist * math.sin(a), r, rng,
                                   wobble=0.2, stretch=1 + dist * 1.5, angle=a))
    # a few fine specks all round
    for _ in range(rng.integers(10, 20)):
        a = rng.uniform(0, 2 * math.pi)
        dist = rng.uniform(0.45, 0.92)
        cov = np.maximum(cov, blob(xx, yy, dist * math.cos(a), dist * math.sin(a),
                                   rng.uniform(0.006, 0.018), rng, wobble=0.1))
    return finish(cov, rng)


def streak(w, h, rng):
    """A run down a wall: impact blob near the top (t = 0.2, cg_gore.c), rivulets below."""
    xx, yy = coords(h, w)                 # yy -1 top .. 1 bottom
    top = -0.6
    cov = blob(xx, yy * 2, 0, top * 2, rng.uniform(0.42, 0.55), rng, wobble=0.3)
    for _ in range(rng.integers(2, 5)):
        x0 = rng.uniform(-0.45, 0.45)
        length = rng.uniform(0.6, 1.6)
        width = rng.uniform(0.05, 0.12)
        y_end = top + length
        along = np.clip((yy - top) / length, 0, 1)
        wander = 0.06 * np.sin(yy * rng.uniform(3, 7) + rng.uniform(0, 6))
        half = width * (1 - 0.6 * along)
        run = (1 - smoothstep(0.7, 1.0, np.abs(xx - x0 - wander) / half)) * (yy > top) * (yy < y_end)
        cov = np.maximum(cov, run)
        # the bead the run ends in
        cov = np.maximum(cov, blob(xx, yy * 2, x0 + 0.06 * math.sin(y_end * 5), y_end * 2,
                                   width * 1.7, rng, wobble=0.1))
    return finish(cov, rng, rim=0.15)


def body(size, rng):
    xx, yy = coords(size, size)
    cov = blob(xx, yy, 0, 0, 0.45, rng, wobble=0.4)
    for _ in range(8):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.5, 0.85)
        cov = np.maximum(cov, blob(xx, yy, d * math.cos(a), d * math.sin(a), rng.uniform(0.03, 0.07), rng))
    return finish(cov, rng, rim=0.1)


def drop(w, h):
    """A soft rounded line for flying drops (RT_LINE stretches it along the path)."""
    xx, yy = coords(h, w)
    d = np.sqrt((xx * 1.0) ** 2 + (yy * 0.9) ** 2)
    return 1 - smoothstep(0.5, 1.0, d)


def alpha_png(cov, rgb=(1.0, 1.0, 1.0)):
    c = np.empty(cov.shape + (4,), np.uint8)
    for i in range(3):
        c[..., i] = int(min(1.0, rgb[i]) * 255 + 0.5)
    c[..., 3] = (np.clip(cov, 0, 1) * 255 + 0.5).astype(np.uint8)
    return png_bytes(c)


def png_bytes(arr):
    buf = io.BytesIO()
    Image.fromarray(arr).save(buf, "PNG", optimize=True)
    return buf.getvalue()


def shader(name, rgb, alpha, extra=""):
    return f"""{name}
{{
	polygonOffset
	surfaceparm nomarks
	surfaceparm trans
	q3map_nolightmap{extra}
	{{
		clampmap {name}
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen {rgb}
		alphaGen {alpha}
	}}
}}
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join("build", "gore", "zz_xenjk_gore.pk3"))
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()

    import gore_v2 as v2      # beside this file; imports the helpers above
    import gore_chunks

    rng = np.random.default_rng(args.seed)
    images = {}     # name -> (kind, rgb or None, alpha)
    for i in range(4):
        images[f"gfx/xen_gore/splat{i + 1}"] = ("grey", None, splat(256, rng))
    for i in range(3):
        images[f"gfx/xen_gore/streak{i + 1}"] = ("grey", None, streak(128, 256, rng))
    images["gfx/xen_gore/body"] = ("body", FRESH * 1.6, body(128, rng))
    images["gfx/xen_gore/drop"] = ("drop", None, drop(16, 32))
    # v2 (cg_goreLook 2) from its own seed stream, so the v1 images stay as they were
    rng2 = np.random.default_rng(args.seed + 1000)
    for i in range(4):
        images[f"gfx/xen_gore/v2_splat{i + 1}"] = ("v2", *v2.mist(512, rng2))
    for i in range(3):
        images[f"gfx/xen_gore/v2_streak{i + 1}"] = ("v2", *v2.drips(256, 512, rng2))
    for i in range(3):
        images[f"gfx/xen_gore/v2_slant{i + 1}"] = ("v2", *v2.spray(256, 512, rng2))
    rgb, a = v2.body(128, rng2)
    images["gfx/xen_gore/v2_body"] = ("v2body", rgb * FRESH[None, None, :] * 1.6, a)
    # the severed-limb cap (cg_gore.c remaps the stock charred cap shader to it)
    capc = v2.cap(256, rng2)
    images["gfx/xen_gore/cap"] = ("cap", capc, np.ones(capc.shape[:2]))
    pool = v2.pool_tex(256, rng2)
    images["gfx/xen_gore/pool"] = ("pool", *pool)
    images["gfx/xen_gore/pool_env"] = ("poolenv", *v2.pool_env(256, rng2))
    chk = v2.chunk_tex(256, rng2)
    images["gfx/xen_gore/chunk"] = ("chunk", chk, np.ones(chk.shape[:2]))
    # bloody shoe prints, three loads per foot (1 fresh from the pool .. 3 nearly worn off)
    for i, load in enumerate((1.0, 0.6, 0.3)):
        for side in ("r", "l"):
            images[f"gfx/xen_gore/print_{side}{i + 1}"] = ("print", *v2.footprint(64, 128, rng2, load, side == "l"))

    text = "// xenjk blood (cgame/cg_gore.c); generated by tools/gore/make_gore_assets.py\n\n"
    for name, (kind, _, _) in images.items():
        if kind in ("grey", "v2", "print"):
            text += shader(name, "vertex", "vertex") + "\n"
        elif kind == "drop":
            text += shader(name, "entity", "entity", "\n\tcull none") + "\n"
        elif kind == "pool":
            # cg_gore_pool.c: a tiling surface (map, not clampmap). The edge is cut as geometry
            # (crisp at any MSAA, no alpha test to dither). The body multiplies the floor, so it
            # takes the floor's own lighting on any map. The wet pass adds the vertex shine through
            # a reflected room, over the same area; cgame gives it the reflection's texture
            # coordinates from the pool's smooth surface (tcGen environment takes each cell's own
            # flat normal, and the floor's slight tilts made the reflection jump from cell to cell).
            head = "\n{\n\tpolygonOffset\n\tsurfaceparm nomarks\n\tq3map_nolightmap\n"
            text += (name + head + "\t{\n\t\tmap " + name + "\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n"
                     "\t\trgbGen vertex\n\t}\n}\n\n")
            text += (name + "_wet" + head + "\t{\n\t\tclampmap " + name + "_env\n"
                     "\t\tblendFunc GL_ONE GL_ONE\n\t\trgbGen vertex\n\t}\n}\n\n")
        elif kind == "poolenv":
            pass
        elif kind == "chunk":
            # lit flesh, and a faint wet gloss that slides with the view
            text += (f"{name}\n{{\n\tq3map_nolightmap\n\t{{\n\t\tmap {name}\n\t\trgbGen lightingDiffuse\n\t}}\n"
                     f"\t{{\n\t\tmap {name}\n\t\ttcGen environment\n\t\tblendFunc GL_ONE GL_ONE\n"
                     f"\t\trgbGen const ( 0.22 0.16 0.16 )\n\t}}\n}}\n\n")
        elif kind == "cap":
            text += f"""{name}\n{{\n\tq3map_nolightmap\n\t{{\n\t\tmap {name}\n\t\trgbGen lightingDiffuse\n\t}}\n}}\n\n"""
        else:
            text += shader(name, "lightingDiffuse", "texture") + "\n"
        if kind == "v2":
            text += v2.wet_shader(name + "_wet", name) + "\n"

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("shaders/xen_gore.shader", text.replace("\n", "\r\n"))
        # meat chunks: small to fist-sized, from their own seed stream
        rng3 = np.random.default_rng(args.seed + 2000)
        for i, size in enumerate((2.2, 2.8, 3.4, 4.2, 5.0, 6.0)):
            z.writestr(f"models/xen_gore/chunk{i + 1}.md3", gore_chunks.chunk(f"models/xen_gore/chunk{i + 1}.md3", rng3, size))
        for name, (kind, rgb, a) in images.items():
            if kind in ("v2", "v2body", "cap", "print", "chunk", "pool", "poolenv"):
                z.writestr(name + ".png", v2.rgba_png(rgb, a))
            else:
                z.writestr(name + ".png", alpha_png(a, rgb if rgb is not None else (1, 1, 1)))
    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes, {len(images)} images)")

    if args.preview:
        # world marks in fresh blood's colour over a light grey floor (top) and a dark brown one
        rows = []
        for floor in ((0.72, 0.72, 0.72), (0.25, 0.17, 0.12)):
            row = []
            for name, (kind, rgb, a) in images.items():
                if kind not in ("grey", "v2", "print"):
                    continue
                size = (192, 192) if a.shape[0] == a.shape[1] else (96, 192) if kind == "print" else (192, 384)
                al = np.asarray(Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).resize(size)).astype(float) / 255
                if rgb is None:
                    sh = np.ones(al.shape + (3,))
                else:
                    sh = np.asarray(Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).resize(size)).astype(float) / 255 * 1.25
                col = FRESH[None, None, :] * 1.4 * sh
                bg = np.ones(al.shape + (3,)) * np.array(floor)
                row.append(((bg * (1 - al[..., None]) + col * al[..., None]) * 255).clip(0, 255).astype(np.uint8))
            rows.append(row)
        width = sum(t.shape[1] + 6 for t in rows[0])
        sheet = np.full((2 * 390, width, 3), 40, np.uint8)
        for r, row in enumerate(rows):
            x = 0
            for t in row:
                sheet[r * 390:r * 390 + t.shape[0], x:x + t.shape[1]] = t
                x += t.shape[1] + 6
        path = os.path.splitext(args.out)[0] + "_preview.png"
        Image.fromarray(sheet).save(path)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
