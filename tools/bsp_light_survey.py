"""Read-only survey of map light data for character shadows (JKA RBSP).

For each map found in the given pk3s (later pk3s override earlier ones, as the
game loads them) it reports:
  - light entities kept in the BSP entity lump (tier-2 shadow lights), by kind;
  - whether the sky shader declares q3map_sun / q3gl2_sun;
  - light-grid quality where characters stand: grid samples 40 u above
    upward-facing vertices, trilerped like R_SetupEntityLightingGrid, giving
    the directed share of light, the floor share a character could block,
    the light elevation, and how far the direction swings over 64 u of walking.

Usage: python tools/bsp_light_survey.py --gamedata <GameData/base> --out <dir>
"""
import argparse
import json
import math
import random
import re
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_order import pack_sort_key  # noqa: E402  (the engine's pk3 order)

LS_NONE = 0xFF
SURF_SKY = 0x00002000
LUM = (0.2126, 0.7152, 0.0722)


def lum(c):
    return c[0] * LUM[0] + c[1] * LUM[1] + c[2] * LUM[2]


def lumps(data):
    ident, version = struct.unpack_from("<4si", data, 0)
    if ident != b"RBSP":
        raise ValueError("not RBSP: %r v%d" % (ident, version))
    return [struct.unpack_from("<ii", data, 8 + 8 * i) for i in range(18)]


def parse_entities(text):
    ents = []
    for block in re.findall(r"\{([^{}]*)\}", text):
        ents.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)))
    return ents


def latlong_dir(lng_b, lat_b):
    # tr_light.cpp: lat = latLong[1], lng = latLong[0], both *4 into a 1024 table
    lat = lat_b * (2 * math.pi / 256)
    lng = lng_b * (2 * math.pi / 256)
    return (math.cos(lat) * math.sin(lng), math.sin(lat) * math.sin(lng), math.cos(lng))


class Grid:
    def __init__(self, data, L, gridsize, wmins, wmaxs):
        self.size = gridsize
        self.origin = [gridsize[i] * math.ceil(wmins[i] / gridsize[i]) for i in range(3)]
        maxs = [gridsize[i] * math.floor(wmaxs[i] / gridsize[i]) for i in range(3)]
        self.bounds = [int((maxs[i] - self.origin[i]) / gridsize[i]) + 1 for i in range(3)]
        ofs, ln = L[15]
        n = ln // 30
        self.cells = []
        for k in range(n):
            b = data[ofs + 30 * k: ofs + 30 * k + 30]
            amb = [0.0, 0.0, 0.0]
            dirl = [0.0, 0.0, 0.0]
            styles = b[24:28]
            for j in range(4):
                if styles[j] == LS_NONE:
                    break
                # styles other than 0 animate; take them at full strength
                for c in range(3):
                    amb[c] += b[j * 3 + c]
                    dirl[c] += b[12 + j * 3 + c]
            self.cells.append((styles[0] != LS_NONE, amb, dirl, latlong_dir(b[28], b[29]),
                               sum(1 for s in styles if s != LS_NONE)))
        ofs, ln = L[17]
        cnt = self.bounds[0] * self.bounds[1] * self.bounds[2]
        self.array = struct.unpack_from("<%dH" % cnt, data, ofs) if ln == cnt * 2 else None

    def sample(self, p):
        if self.array is None:
            return None
        pos, frac = [], []
        for i in range(3):
            v = (p[i] - self.origin[i]) / self.size[i]
            f = math.floor(v)
            fr = v - f
            f = max(0, min(int(f), self.bounds[i] - 1))
            pos.append(f)
            frac.append(fr)
        step = (1, self.bounds[0], self.bounds[0] * self.bounds[1])
        start = pos[0] + pos[1] * step[1] + pos[2] * step[2]
        amb = [0.0] * 3
        dirl = [0.0] * 3
        d = [0.0] * 3
        total = 0.0
        for i in range(8):
            fac = 1.0
            gp = start
            for j in range(3):
                if i & (1 << j):
                    fac *= frac[j]
                    gp += step[j]
                else:
                    fac *= 1 - frac[j]
            if gp >= len(self.array):
                continue
            ci = self.array[gp]
            if ci >= len(self.cells):
                continue
            ok, a, dl, nd, _ = self.cells[ci]
            if not ok:
                continue
            total += fac
            for c in range(3):
                amb[c] += fac * a[c]
                dirl[c] += fac * dl[c]
                d[c] += fac * nd[c]
        if total <= 0:
            return None
        amb = [x / total for x in amb]
        dirl = [x / total for x in dirl]
        m = math.sqrt(sum(x * x for x in d)) or 1.0
        return amb, dirl, [x / m for x in d]


def pct(xs, q):
    if not xs:
        return None
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * (len(xs) - 1) + 0.5))]


def survey(name, data, shader_text, rng):
    L = lumps(data)
    ents = parse_entities(data[L[0][0]:L[0][0] + L[0][1]].decode("latin-1"))
    world = ents[0] if ents else {}
    gridsize = [64.0, 64.0, 128.0]
    if "gridsize" in world:
        try:
            gridsize = [float(x) for x in world["gridsize"].split()]
        except ValueError:
            pass
    classes = {}
    for e in ents:
        cn = e.get("classname", "")
        classes[cn] = classes.get(cn, 0) + 1
    lights = [e for e in ents if e.get("classname", "").lower().startswith("light")]
    spots = sum(1 for e in lights if "target" in e or "_spot" in "".join(e))
    light_kinds = {k: v for k, v in classes.items() if k.lower().startswith("light")}

    # shaders lump: 64-byte name + surfaceFlags + contentFlags
    sofs, sln = L[1]
    skies = []
    for k in range(sln // 72):
        nm, sf, _cf = struct.unpack_from("<64sii", data, sofs + 72 * k)
        if sf & SURF_SKY:
            skies.append(nm.split(b"\0")[0].decode("latin-1"))
    sky_sun = {}
    for s in skies:
        body = shader_text.get(s.lower())
        if body is None:
            sky_sun[s] = "no shader text"
        else:
            # <keyword> r g b intensity yaw elevation: a zero intensity declares no sun
            suns = [[m.group(1).lower()] + [float(x) for x in m.groups()[1:]] for m in re.finditer(
                r"^\s*(q3gl2_sun|q3map_sunext|q3map_sun|sun)\s+\S+\s+\S+\s+\S+\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)",
                body, re.I | re.M)]
            sky_sun[s] = suns or "none"
    lit_suns = [v for vs in sky_sun.values() if isinstance(vs, list) for v in vs if v[1] > 0]

    # emitters that survive compilation: q3map_surfacelight surfaces and flares
    names = [struct.unpack_from("<64s", data, sofs + 72 * k)[0].split(b"\0")[0].decode("latin-1").lower()
             for k in range(sln // 72)]
    surf_light = {}
    for k, nm in enumerate(names):
        body = shader_text.get(nm)
        m = body and re.search(r"q3map_surfacelight\s+([-\d.]+)", body, re.I)
        if m and float(m.group(1)) > 0 and not re.search(r"surfaceparm\s+sky", body, re.I):
            surf_light[k] = float(m.group(1))
    vofs_, _ = L[10]
    fofs, fln = L[13]
    emit_pts, flares = [], 0
    for k in range(fln // 148):
        shnum, _fog, stype, fv, nvv = struct.unpack_from("<5i", data, fofs + 148 * k)
        if stype == 4:
            flares += 1
        if shnum not in surf_light or nvv == 0:
            continue
        cx = cy = cz = 0.0
        for v in range(fv, fv + nvv):
            x, y, z = struct.unpack_from("<3f", data, vofs_ + 80 * v)
            cx += x
            cy += y
            cz += z
        emit_pts.append((cx / nvv, cy / nvv, cz / nvv, surf_light[shnum]))
    # emitter surfaces within 128 u of each other count as one fixture: [x, y, z, summed value]
    fixtures = []
    for p in emit_pts:
        for c in fixtures:
            if sum((c[i] - p[i]) ** 2 for i in range(3)) < 128 * 128:
                c[3] += p[3]
                break
        else:
            fixtures.append(list(p))
    bsp = Bsp(data, L)

    mofs, _ = L[7]
    wmins = struct.unpack_from("<3f", data, mofs)
    wmaxs = struct.unpack_from("<3f", data, mofs + 12)
    grid = Grid(data, L, gridsize, wmins, wmaxs)

    # count over the grid array (cells are deduplicated in the data lump)
    open_cells = [grid.cells[i] for i in (grid.array or ()) if i < len(grid.cells) and grid.cells[i][0]]
    multi_style = sum(1 for c in open_cells if c[4] > 1)

    # floor points: upward-facing vertices (RBSP drawvert is 80 bytes, normal at 52)
    vofs, vln = L[10]
    nv = vln // 80
    idx = list(range(nv))
    rng.shuffle(idx)
    floor = []
    for k in idx:
        x, y, z = struct.unpack_from("<3f", data, vofs + 80 * k)
        nx, ny, nz = struct.unpack_from("<3f", data, vofs + 80 * k + 52)
        if nz > 0.7:
            floor.append((x, y, z))
            if len(floor) >= 6000:
                break

    share, floor_share, elev, swing, strong_swing = [], [], [], [], []
    fix_angle, fix_none, sun_angle = [], [], []
    for (x, y, z) in floor:
        if bsp.solid((x, y, z + 40)):
            continue  # a ledge under a wall or ceiling: no one stands here
        s = grid.sample((x, y, z + 40))
        if s is None:
            continue
        amb, dirl, d = s
        a, dl = lum(amb), lum(dirl)
        if a + dl < 1:
            continue
        share.append(dl / (a + dl))
        fz = dl * max(0.0, d[2])
        floor_share.append(fz / (a + fz) if a + fz > 0 else 0.0)
        elev.append(math.degrees(math.asin(max(-1.0, min(1.0, d[2])))))
        ang = rng.random() * 2 * math.pi
        s2 = grid.sample((x + 64 * math.cos(ang), y + 64 * math.sin(ang), z + 40))
        if s2 is not None:
            dot = max(-1.0, min(1.0, sum(d[i] * s2[2][i] for i in range(3))))
            sw = math.degrees(math.acos(dot))
            swing.append(sw)
            if dl / (a + dl) >= 0.4:
                strong_swing.append(sw)
        # does a visible fixture (or the declared sun) explain the grid direction here?
        if dl / (a + dl) < 0.4 or len(fix_angle) + len(fix_none) >= 1500:
            continue
        eye = (x, y, z + 40)
        if lit_suns:
            sy, se = math.radians(lit_suns[0][2]), math.radians(lit_suns[0][3])
            sd = (math.cos(sy) * math.cos(se), math.sin(sy) * math.cos(se), math.sin(se))
            sun_angle.append(math.degrees(math.acos(max(-1.0, min(1.0, sum(d[i] * sd[i] for i in range(3)))))))
        cands = []
        for f in fixtures:
            v = [f[i] - eye[i] for i in range(3)]
            dd = sum(c * c for c in v)
            if 32 * 32 < dd < 1536 * 1536:
                cands.append((f[3] / dd, v, dd))
        cands.sort(key=lambda c: -c[0])
        for _w, v, dd in cands[:12]:
            if bsp.clear(eye, v, math.sqrt(dd) - 24):
                n = math.sqrt(dd)
                fix_angle.append(math.degrees(math.acos(max(-1.0, min(1.0, sum(d[i] * v[i] / n for i in range(3)))))))
                break
        else:
            fix_none.append(1)

    return {
        "map": name,
        "gridsize": gridsize,
        "grid_bounds": grid.bounds,
        "grid_cells": len(grid.array or ()),
        "grid_open": len(open_cells),
        "grid_multistyle_frac": round(multi_style / len(open_cells), 3) if open_cells else None,
        "light_entities": len(lights),
        "light_kinds": light_kinds,
        "light_spots": spots,
        # q3map strips light entities but leaves the info_null spot targets behind
        "info_null": classes.get("info_null", 0),
        "sky_shaders": sky_sun,
        "lit_sun": bool(lit_suns),
        "surfacelight_shaders": len(surf_light),
        "surfacelight_surfaces": len(emit_pts),
        "surfacelight_fixtures": len(fixtures),
        "flares": flares,
        "floor_points": len(share),
        "directed_share_p25_p50_p75": [round(pct(share, q), 3) for q in (0.25, 0.5, 0.75)] if share else None,
        "strong_frac": round(sum(1 for s in share if s >= 0.4) / len(share), 3) if share else None,
        "floor_block_share_p50_p75": [round(pct(floor_share, q), 3) for q in (0.5, 0.75)] if floor_share else None,
        "elevation_deg_p10_p50_p90": [round(pct(elev, q), 1) for q in (0.1, 0.5, 0.9)] if elev else None,
        "low_sun_frac": round(sum(1 for e in elev if e < 30) / len(elev), 3) if elev else None,
        "swing64_deg_p50_p90": [round(pct(swing, q), 1) for q in (0.5, 0.9)] if swing else None,
        "swing64_strong_deg_p50_p90": [round(pct(strong_swing, q), 1) for q in (0.5, 0.9)] if strong_swing else None,
        # strong-light points: share with a visible fixture within 1536 u, and the angle from the
        # grid direction to the brightest visible one (value / d^2); the same angle to the declared sun
        "fixture_seen_frac": round(len(fix_angle) / (len(fix_angle) + len(fix_none)), 3) if fix_angle or fix_none else None,
        "fixture_angle_p50": round(pct(fix_angle, 0.5), 1) if fix_angle else None,
        "fixture_within30_frac": round(sum(1 for t in fix_angle if t < 30) / len(fix_angle), 3) if fix_angle else None,
        "sun_angle_p50": round(pct(sun_angle, 0.5), 1) if sun_angle else None,
        "sun_within30_frac": round(sum(1 for t in sun_angle if t < 30) / len(sun_angle), 3) if sun_angle else None,
    }


class Bsp:
    """Point-in-leaf walks; a leaf with cluster -1 is solid or outside the world."""

    def __init__(self, data, L):
        pofs, pln = L[2]
        self.planes = [struct.unpack_from("<4f", data, pofs + 16 * k) for k in range(pln // 16)]
        nofs, nln = L[3]
        self.nodes = [struct.unpack_from("<3i", data, nofs + 36 * k) for k in range(nln // 36)]
        lofs, lln = L[4]
        self.cluster = [struct.unpack_from("<i", data, lofs + 48 * k)[0] for k in range(lln // 48)]

    def solid(self, p):
        n = 0
        while n >= 0:
            pl, front, back = self.nodes[n]
            a, b, c, dist = self.planes[pl]
            n = front if a * p[0] + b * p[1] + c * p[2] - dist >= 0 else back
        return self.cluster[-1 - n] < 0

    def clear(self, start, v, length):
        n = math.sqrt(sum(c * c for c in v)) or 1.0
        t = 8.0
        while t < length:
            if self.solid([start[i] + v[i] / n * t for i in range(3)]):
                return False
            t += 16.0
        return True


def shader_index(pk3s):
    text = {}
    for pk in pk3s:
        try:
            z = zipfile.ZipFile(pk)
        except zipfile.BadZipFile:
            continue
        for n in z.namelist():
            if n.lower().startswith("shaders/") and n.lower().endswith(".shader"):
                src = z.read(n).decode("latin-1")
                src = re.sub(r"//[^\n]*", "", src)
                # top-level "name { ... }" with one level of nested stages
                for m in re.finditer(r"([^\s{}]+)\s*\{((?:[^{}]|\{[^{}]*\})*)\}", src):
                    text.setdefault(m.group(1).lower(), m.group(2))
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gamedata", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--maps", nargs="*", help="only these map names (maps/<name>.bsp)")
    args = ap.parse_args()
    base = Path(args.gamedata)
    pk3s = sorted(base.glob("*.pk3"), key=pack_sort_key)
    # game search order: later pk3s win
    shaders = shader_index(list(reversed(pk3s)))
    bsps = {}
    for pk in pk3s:
        try:
            z = zipfile.ZipFile(pk)
        except zipfile.BadZipFile:
            continue
        for n in z.namelist():
            if n.lower().startswith("maps/") and n.lower().endswith(".bsp"):
                bsps[n.lower()[5:-4]] = (pk, n)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for name in sorted(bsps):
        if args.maps and name not in args.maps:
            continue
        pk, n = bsps[name]
        try:
            data = zipfile.ZipFile(pk).read(n)
            r = survey(name, data, shaders, random.Random(20261014))
            r["pk3"] = pk.name
        except Exception as ex:  # report and keep going
            r = {"map": name, "pk3": pk.name, "error": str(ex)}
        rows.append(r)
        print(json.dumps(r), flush=True)
    with open(out / "survey.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(rows, f, indent=1)


if __name__ == "__main__":
    sys.exit(main())
