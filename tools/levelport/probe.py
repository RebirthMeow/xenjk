"""probe.py -- what collides where a playtest report points, offline: no compile, no client.

  python -I probe.py <report> [--tag 8] [--kit DIR | --pack PK3 ...] [--area watson] [--json OUT]

<report>: a /screenpos report's .txt (codemp/cgame/cg_screenpos.c: eye and view angles, player origin, the
engine's own crosshair and floor traces), its folder's index.txt (every report in it; a stamp without its .txt
takes the index line's view position, level, at its yaw), or a position: "x y z [yaw [pitch]]" in game units.

For each report, in the hub map's frame (game units = the area's glTF frame x 37.5, levelport.py):
  - the tile under the player (CP2077 metres on the city's 256 m grid) and its eight neighbours;
  - against their collision parts (tiles/<tag>/build/<tile>.npz: the mesh triangles and prop boxes the chunks are
    compiled from): the crosshair ray (8192 u), the floor straight below the player (1024 u), the player's box
    (15 x 15, -24 to +40 u about the origin) and the boxes within 2 m (each by its mesh's file name);
  - with --kit or --pack: the same ray and floor against the compiled chunk BSPs (maps/<map>.chunks: each chunk's
    origin; brush traces with the chunks' own planes, solid and playerclip brushes), and the box test, so a report
    is answered by what the engine loads; a kit's packs are read in the engine's order (a later name wins: a dev
    kit's patch pack over the base pack).
The report's own traces (the engine's) are printed beside, so the offline answers can be checked against them. The
parts hold no water: a chunk's water bed (cp2gltf.py chunks adds it) is a floor only the chunk answer sees. Options
go before a position: probe.py --kit DIR -- -33600 -40800 300 0 20.
Reads only. Converted levels are for local play only."""
import argparse
import json
import math
import re
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import colzbsp  # noqa: E402
import levelport as lp  # noqa: E402

UPM = lp.UNITS_PER_METRE
PLAYER_MINS, PLAYER_MAXS = np.array([-15.0, -15.0, -24.0]), np.array([15.0, 15.0, 40.0])     # JKA's standing box
CONTENTS_SOLID, CONTENTS_PLAYERCLIP, CONTENTS_TERRAIN = 0x1, 0x10, 0x1000
SL = chr(92)


# ------------------------------------------------------------------------------------------------- reports

def parse_report(path):
    """A /screenpos .txt -> dict (eye, pitch, yaw, origin, the engine's traces as text)."""
    t = Path(path).read_text(encoding='utf-8', errors='replace')
    r = {'source': str(path), 'name': Path(path).stem}

    def vec(pat):
        m = re.search(pat + r'\s*\(([-\d.e]+) ([-\d.e]+) ([-\d.e]+)\)', t, re.M)
        return [float(v) for v in m.groups()] if m else None
    r['issue'] = (re.search(r'^issue: (.*)$', t, re.M) or [None, ''])[1]
    r['map'] = (re.search(r'^map: maps/(\S+)\.bsp', t, re.M) or [None, ''])[1]
    r['eye'] = vec(r'^eye:')
    m = re.search(r'angles pitch ([-\d.]+) yaw ([-\d.]+)', t)
    r['pitch'], r['yaw'] = (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)
    r['origin'] = vec(r'origin')
    r['engine'] = [line.strip() for line in t.splitlines()
                   if line.startswith(('crosshair, player collision', 'crosshair, shots', 'below the player'))]
    if r['eye'] is None:
        r['eye'] = r['origin']
    if r['origin'] is None:
        r['origin'] = r['eye']
    return r


def parse_index(path):
    """index.txt -> its reports (the .txt beside, else the index line's view position and yaw)."""
    out, d = [], Path(path).parent
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = re.match(r'^(\S+)\s+(\S+)\s+\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)\s*:\s*([-\d.]+)\s*(.*)$', line)
        if not m:
            continue
        f = d / (m.group(1) + '.txt')
        if f.exists():
            out.append(parse_report(f))
        else:
            p = [float(v) for v in m.group(3, 4, 5)]
            out.append({'source': str(path), 'name': m.group(1), 'issue': m.group(7), 'map': m.group(2), 'eye': p,
                        'origin': p, 'pitch': 0.0, 'yaw': float(m.group(6)), 'engine': []})
    return out


def forward(pitch, yaw):
    """Quake's view direction (degrees; pitch down positive) in game units."""
    p, y = math.radians(pitch), math.radians(yaw)
    return np.array([math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), -math.sin(p)])


# ------------------------------------------------------------------------------------------------- frames

class Area:
    """The area's frames: game units <-> its glTF frame (metres, +Y up) <-> CP2077 metres; tiles on the grid."""

    def __init__(self, work, tag, area):
        import cp2gltf
        a = cp2gltf.AREAS[area]
        self.centre, self.floor = a['centre'], 12.0           # (cp2gltf main's --floor default; the tiled builds')
        self.tdir = cp2gltf.tile_dir(work, tag)
        man = json.loads((self.tdir / 'tiles.json').read_text(encoding='utf-8'))
        self.size, self.tiles = man['tile_size'], {t['id'] for t in man['tiles']}

    def cp(self, q3):
        g = lp.q3_to_gltf(q3)
        return np.array([g[0] + self.centre[0], -g[2] + self.centre[1], g[1] + self.floor])

    def tile(self, q3):
        c = self.cp(q3)
        return '%d_%d' % (math.floor(c[0] / self.size), math.floor(c[1] / self.size))

    def around(self, q3):
        i, j = map(int, self.tile(q3).split('_'))
        return [t for t in ('%d_%d' % (i + di, j + dj) for di in (-1, 0, 1) for dj in (-1, 0, 1)) if t in self.tiles]


# ------------------------------------------------------------------------------------------------- parts

def box_tris(lo, hi, G):
    """A prop box's 12 triangles, placed by G (glTF metres)."""
    c = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
    c = c @ G[:3, :3].T + G[:3, 3]
    F = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4),
         (1, 5, 7), (1, 7, 3)]
    return c[np.array(F)]


class Parts:
    """Tiles' collision parts joined: triangles (n, 3, 3) and boxes, in game units (each triangle's tile kept)."""

    def __init__(self, area, tiles):
        T, tt, self.box_name, B, bt = [], [], [], [], []
        for t in tiles:
            f = area.tdir / 'build' / (t + '.npz')
            if not f.exists():
                continue
            with np.load(f, allow_pickle=True) as z:
                T.append(lp.gltf_to_q3(z['col'].astype(np.float64)))
                tt += [t] * len(z['col'])
                for d, lh, G in zip(z['box_depot'], z['box_lohi'], z['box_G']):
                    B.append(lp.gltf_to_q3(box_tris(lh[:3], lh[3:], G.astype(np.float64))))
                    self.box_name.append(str(d).split(SL)[-1])
                    bt.append(t)
        self.T = np.concatenate(T) if T else np.zeros((0, 3, 3))
        self.tri_tile = np.array(tt)
        self.B = np.array(B).reshape(-1, 12, 3, 3)
        self.box_tile = np.array(bt)

    def ray(self, s, e):
        """Nearest hit of the segment s->e: (distance in u, 'triangle'/'box', name, normal) or None."""
        best = None
        d = e - s
        for kind, T, names in (('triangle', self.T, None), ('box', self.B.reshape(-1, 3, 3), self.box_name)):
            if not len(T):
                continue
            lo, hi = np.minimum(s, e) - 1e-3, np.maximum(s, e) + 1e-3
            near = np.nonzero(np.all(T.max(1) >= lo, 1) & np.all(T.min(1) <= hi, 1))[0]
            if not len(near):
                continue
            t, n = _ray_tris(s, d, T[near])
            if t is None:
                continue
            k = near[n]
            hit = (t * np.linalg.norm(d), kind, names[k // 12] if names else self.tri_tile[k], _normal(T[k]))
            if best is None or hit[0] < best[0]:
                best = hit
        return best

    def overlaps(self, mins, maxs):
        """Triangles and boxes the box mins..maxs (game units) overlaps: (triangle count, box names)."""
        tri = _tris_box(self.T, mins, maxs)
        bx = []
        if len(self.B):
            hit = _tris_box(self.B.reshape(-1, 3, 3), mins, maxs).reshape(-1, 12).any(1)
            hit |= _inside_boxes(self.B, (mins + maxs) / 2)   # (a player wholly inside a box crosses none of its faces)
            bx = sorted({self.box_name[k] for k in np.nonzero(hit)[0]})
        return int(tri.sum()), bx

    def boxes_near(self, p, r):
        if not len(self.B):
            return []
        c = self.B.reshape(len(self.B), -1, 3).mean(1)
        k = np.nonzero(np.linalg.norm(c - p, axis=1) < r)[0]
        return sorted({self.box_name[i] for i in k})


def _normal(t):
    n = np.cross(t[1] - t[0], t[2] - t[0])
    return n / max(np.linalg.norm(n), 1e-12)


def _ray_tris(s, d, T):
    """Moller-Trumbore over triangles (either side): the nearest t in [0, 1] and its index, or (None, None)."""
    e1, e2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
    p = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, p)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    tv = s - T[:, 0]
    u = np.einsum('ij,ij->i', tv, p) * inv
    q = np.cross(tv, e1)
    v = (q @ d) * inv
    t = np.einsum('ij,ij->i', e2, q) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t >= 0) & (t <= 1)
    if not hit.any():
        return None, None
    k = np.nonzero(hit)[0]
    i = k[np.argmin(t[k])]
    return float(t[i]), int(i)


def _tris_box(T, mins, maxs):
    """Which triangles overlap the axis-aligned box (separating axes: the box's 3, the triangle's normal, 9 edge
    crosses)."""
    if not len(T):
        return np.zeros(0, bool)
    c, h = (mins + maxs) / 2, (maxs - mins) / 2
    v = T - c
    lo, hi = T.min(1), T.max(1)
    ok = np.all(hi >= mins, 1) & np.all(lo <= maxs, 1)
    k = np.nonzero(ok)[0]
    if not len(k):
        return ok
    v = v[k]
    e = [v[:, 1] - v[:, 0], v[:, 2] - v[:, 1], v[:, 0] - v[:, 2]]
    n = np.cross(e[0], e[1])
    r = np.abs(n) @ h
    d = np.einsum('ij,ij->i', n, v[:, 0])
    sep = np.abs(d) > r
    for ei in e:
        for ax in np.eye(3):
            a = np.cross(np.broadcast_to(ax, ei.shape), ei)
            pr = np.einsum('kij,kj->ki', v, a)
            rad = np.abs(a) @ h
            sep |= (pr.min(1) > rad) | (pr.max(1) < -rad)
    ok[k] = ~sep
    return ok


def _inside_boxes(B, p):
    """Which boxes (their 12 triangles each, box_tris' corner order) hold p: p - corner 0 in the box's own edges (x, y,
    z: corners 4, 2, 1) within 0..1. A flat box (a decal's, no volume) holds nothing."""
    # box_tris' corners: 4 x + 2 y + z (each 0 at lo, 1 at hi); its triangles 0 (0, 1, 3), 1 (0, 3, 2), 2 (4, 6, 7)
    c0 = B[:, 0, 0]
    M = np.stack([B[:, 2, 0] - c0, B[:, 1, 2] - c0, B[:, 0, 1] - c0], -1)   # columns: the x, y, z edges
    vol = np.abs(np.linalg.det(M))
    ok = vol > 1e-6
    out = np.zeros(len(B), bool)
    if ok.any():
        uvw = np.linalg.solve(M[ok], (p - c0[ok])[..., None])[..., 0]
        out[ok] = np.all((uvw >= 0) & (uvw <= 1), 1)
    return out


# ------------------------------------------------------------------------------------------------- compiled chunks

class Chunks:
    """The chunk BSPs a kit or packs load (maps/<map>.chunks: name, origin), read on demand."""

    def __init__(self, packs, mapname):
        self.files = {}                                  # name in the packs -> the pack that serves it (the last wins)
        for pk in sorted(packs, key=lambda p: p.name.upper()):      # (the engine's order: FS_PathCmp, later first)
            with zipfile.ZipFile(pk) as z:
                for n in z.namelist():
                    self.files[n.lower()] = pk
        self.origin, self.loaded = {}, {}
        m = 'maps/%s.chunks' % mapname
        if m not in self.files:
            sys.exit('probe: no %s in %s' % (m, ', '.join(str(p) for p in packs)))
        for line in self.read(m).decode().splitlines():
            if line.startswith('//') or not line.strip():
                continue
            f = line.split()
            self.origin[f[0].lower()] = np.array([float(v) for v in f[1:4]])

    def read(self, name):
        with zipfile.ZipFile(self.files[name.lower()]) as z:
            return z.read(name)

    def source(self, tile):
        n = 'maps/chunk_%s.bsp' % tile
        return str(self.files.get(n)) if n in self.files else None

    def brushes(self, tile):
        """A chunk's model-0 brushes for the player (solid, playerclip, terrain): (planes per side, side ranges,
        contents), in game units at the chunk's origin; None when the kit has no such chunk."""
        name = 'maps/chunk_%s.bsp' % tile
        if name not in self.origin:
            return None
        if tile not in self.loaded:
            data = colzbsp.plain(self.read(name))           # (a kit's chunks are XBSZ unless built --plain)
            lumps = [struct.unpack_from('<ii', data, 8 + 8 * i) for i in range(18)]

            def L(i, dt):
                o, n = lumps[i]
                return np.frombuffer(data, dt, n // np.dtype(dt).itemsize, o)
            sh = L(1, np.dtype([('name', 'S64'), ('sf', '<i4'), ('cf', '<i4')]))
            pl = L(2, np.dtype([('n', '<f4', 3), ('d', '<f4')]))
            model = L(7, np.dtype([('mins', '<f4', 3), ('maxs', '<f4', 3), ('fsurf', '<i4'), ('nsurf', '<i4'),
                                   ('fbrush', '<i4'), ('nbrush', '<i4')]))[0]
            br = L(8, np.dtype([('fs', '<i4'), ('ns', '<i4'), ('shader', '<i4')]))
            sd = L(9, np.dtype([('plane', '<i4'), ('shader', '<i4'), ('surf', '<i4')]))
            br = br[model['fbrush']:model['fbrush'] + model['nbrush']]
            cf = sh['cf'][br['shader']].astype(np.int64)
            br = br[(cf & (CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_TERRAIN)) != 0]
            cf = sh['cf'][br['shader']].astype(np.int64)
            ns = br['ns'].astype(np.int64)
            starts = np.r_[0, np.cumsum(ns)[:-1]].astype(np.int64)
            idx = np.repeat(br['fs'].astype(np.int64) - starts, ns) + np.arange(int(ns.sum()))   # each brush's sides
            p = pl[sd['plane'][idx]]
            o = self.origin[name]
            n = p['n'].astype(np.float64)
            dist = p['d'].astype(np.float64) + n @ o                # the chunk placed at its origin
            self.loaded[tile] = (n, dist, starts, br['ns'].astype(np.int64), cf)
        return self.loaded[tile]

    def layers(self, tiles):
        """The tiles and any more BSPs placed for them (patch_chunks.py: <tile>_p), as names for brushes()."""
        out = []
        for t in tiles:
            out.append(t)
            out += sorted(n[len('maps/chunk_'):-4] for n in self.origin if n.startswith('maps/chunk_%s_' % t.lower()))
        return out

    def ray(self, tiles, s, e, eps=0.03125):
        """The nearest brush the segment s->e enters (Quake's point trace): (distance in u, tile, contents) or None."""
        best = None
        for t in self.layers(tiles):
            got = self.brushes(t)
            if got is None or not len(got[2]):
                continue
            n, dist, starts, counts, cf = got
            d1, d2 = n @ s - dist, n @ e - dist
            out_all = (d1 > 0) & (d2 > 0)                   # a side both points are outside of: the brush is missed
            enter = np.where((d1 > d2) & (d1 > 0), (d1 - eps) / np.where(d1 > d2, d1 - d2, 1.0), -1.0)
            leave = np.where((d1 < d2) & (d2 > 0), (d1 + eps) / np.where(d1 < d2, d1 - d2, -1.0), 1.0)
            miss = np.maximum.reduceat(out_all.astype(np.int8), starts) > 0
            fin = np.maximum.reduceat(enter, starts)
            fout = np.minimum.reduceat(leave, starts)
            start_out = np.maximum.reduceat((d1 > 0).astype(np.int8), starts) > 0
            hit = ~miss & start_out & (fin < fout) & (fin > -1.0)
            if not hit.any():
                continue
            k = np.nonzero(hit)[0]
            i = k[np.argmin(fin[k])]
            r = (max(float(fin[i]), 0.0) * float(np.linalg.norm(e - s)), t, int(cf[i]))
            if best is None or r[0] < best[0]:
                best = r
        return best

    def overlaps(self, tiles, mins, maxs):
        """Brushes the box overlaps (each side's plane pushed out by the box's extent along its normal), per tile."""
        out = {}
        c, h = (mins + maxs) / 2, (maxs - mins) / 2
        for t in self.layers(tiles):
            got = self.brushes(t)
            if got is None or not len(got[2]):
                continue
            n, dist, starts, counts, cf = got
            inside = (n @ c - dist - np.abs(n) @ h) < 0
            k = np.minimum.reduceat(inside.astype(np.int8), starts) > 0
            if k.any():
                out[t] = int(k.sum())
        return out


# ------------------------------------------------------------------------------------------------- the probe

def probe(r, area, chunks=None):
    eye, org = np.array(r['eye']), np.array(r['origin'])
    tiles = area.around(org)
    parts = Parts(area, tiles)
    f = forward(r['pitch'], r['yaw'])
    out = {'report': r['name'], 'issue': r.get('issue', ''), 'tile': area.tile(org), 'cp': area.cp(org).round(2).tolist(),
           'engine': r.get('engine', []), 'parts': {}, 'chunks': {}}
    hit = parts.ray(eye, eye + f * 8192)
    out['parts']['crosshair'] = None if hit is None else {
        'u': round(hit[0], 1), 'what': hit[1], 'name': str(hit[2]), 'normal': lp.dir_q3_to_gltf(hit[3]).round(3).tolist()}
    fl = parts.ray(org, org - np.array([0, 0, 1024.0]))
    out['parts']['floor'] = None if fl is None else {'u': round(fl[0], 1), 'what': fl[1], 'name': str(fl[2])}
    ntri, bx = parts.overlaps(org + PLAYER_MINS, org + PLAYER_MAXS)
    out['parts']['player_box'] = {'triangles': ntri, 'boxes': bx}
    out['parts']['boxes_within_2m'] = parts.boxes_near(org, 2 * UPM)
    if chunks is not None:
        h = chunks.ray(tiles, eye, eye + f * 8192)
        out['chunks']['crosshair'] = None if h is None else {'u': round(h[0], 1), 'tile': h[1], 'contents': hex(h[2])}
        h = chunks.ray(tiles, org, org - np.array([0, 0, 1024.0]))
        out['chunks']['floor'] = None if h is None else {'u': round(h[0], 1), 'tile': h[1], 'contents': hex(h[2])}
        out['chunks']['player_box'] = chunks.overlaps(tiles, org + PLAYER_MINS, org + PLAYER_MAXS)
        out['chunks']['sources'] = {t: chunks.source(t) for t in tiles}
    return out


def show(o):
    print('== %s: %s' % (o['report'], o['issue']))
    print('   tile %s (CP2077 %s)' % (o['tile'], ' '.join('%.1f' % v for v in o['cp'])))
    for line in o['engine']:
        print('   engine: ' + line)
    p = o['parts']
    c = p['crosshair']
    print('   parts:  crosshair %s' % ('nothing within 8192 u' if c is None else
                                       '%(u).1f u, %(what)s %(name)s, normal (glTF) %(normal)s' % c))
    print('           floor %s' % ('nothing within 1024 u' if p['floor'] is None else '%(u).1f u below, %(what)s %(name)s'
                                   % p['floor']))
    pb = p['player_box']
    print('           player box: %d triangles, %s' % (pb['triangles'], ', '.join(pb['boxes']) or 'no boxes'))
    print('           boxes within 2 m: %s' % (', '.join(p['boxes_within_2m']) or 'none'))
    if o['chunks']:
        c = o['chunks']
        print('   chunks: crosshair %s' % ('nothing within 8192 u' if c['crosshair'] is None else
                                         '%(u).1f u, chunk %(tile)s, contents %(contents)s' % c['crosshair']))
        print('           floor %s' % ('nothing within 1024 u' if c['floor'] is None else
                                       '%(u).1f u below, chunk %(tile)s, contents %(contents)s' % c['floor']))
        print('           player box inside brushes: %s' % (', '.join('%s %d' % kv for kv in c['player_box'].items())
                                                           or 'none'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('report', nargs='+', help='a report .txt, an index.txt, or x y z [yaw [pitch]] in game units')
    ap.add_argument('--tag', default='8', help='the tiling whose collision parts to read (tiles/<tag>/build)')
    ap.add_argument('--area', default='watson')
    ap.add_argument('--work', type=Path)
    ap.add_argument('--kit', type=Path, help='a kit folder: its home/base packs (the chunk BSPs it loads)')
    ap.add_argument('--pack', type=Path, action='append', default=[], help='chunk packs (instead of --kit)')
    ap.add_argument('--map', default=None, help='map name (default cp_<area>)')
    ap.add_argument('--json', type=Path, help='write the answers here too')
    a = ap.parse_args()
    import cp2gltf
    work = a.work or cp2gltf.work_dir(a.area)
    area = Area(work, a.tag, a.area)
    if re.fullmatch(r'[-\d. ]+', ' '.join(a.report)):
        v = [float(x) for x in ' '.join(a.report).split()]
        reports = [{'name': 'at', 'issue': '', 'eye': v[:3], 'origin': v[:3], 'yaw': v[3] if len(v) > 3 else 0.0,
                    'pitch': v[4] if len(v) > 4 else 0.0, 'engine': []}]
    else:
        reports = []
        for f in a.report:
            reports += parse_index(f) if Path(f).name.lower() == 'index.txt' else [parse_report(f)]
    packs = list(a.pack)
    if a.kit:
        packs += sorted((a.kit / 'home' / 'base').glob('*.pk3'))
    chunks = None
    if packs:
        chunk_packs = [p for p in packs if 'colchunks' in p.name.lower()] or packs
        chunks = Chunks(chunk_packs, a.map or 'cp_' + a.area)
    outs = []
    for r in reports:
        o = probe(r, area, chunks)
        show(o)
        outs.append(o)
    if a.json:
        a.json.write_text(json.dumps(outs, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()
