# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""Level-port checks that find what a player would trip over, so a playtest does not have to (holes, bad spawns, collision issues).

  spawns  <map.pk3>                         every info_player_* box (JKA's +-15, -24..+40) against the player-solid
                                            brushes (the engine's own plane test: exact, brushes carry their bevels),
                                            and whether there is a floor within 64 u below it
  aerial  <map.pk3> <scene.pk3> <out>       the 64-bit client (hidden, muted, private home) photographs the level
          --map NAME --radius U             straight down in tiles from 40,000 u at 15 degrees (near orthographic; a
                                            copy of the map sets distanceCull so the far plane reaches under the
                                            ground), the sky cleared to magenta (r_fastsky 7), scene materials flat
                                            (r_sceneDebug 1, no haze), finest LODs; then `holes`
  holes   <out> [--radius U]                magenta pixels inside the level's radius = places the sky shows through
                                            from above: holes in the ground or roofs. Projected to the ground plane,
                                            clustered on a 0.5 m grid, ranked by area
  compare <map.pk3> <scene.pk3> <out>       the collision compare: per 16 u column, the player-solid intervals
                                            against the render scene's walkable surfaces (LOD 0, up-facing; blend,
                                            window and water materials left out). A visible surface is matched (a
                                            collision top within -10..+6 u), buried (inside a solid) or missing; a
                                            collision top with 64 u free above it and nothing drawn within -6..+10 u
                                            is invisible (unless water lies up to 4.5 m over it: the water's bed).
                                            look/kabuki_collide.py vectorised (its rules and outputs: cats.npz,
                                            samples.npz, solid.npz, stats.json, overview.png); minutes, not an hour
          [--tiles T --tile ID]             one tile's cells (tiles.json: the area's tiles; tile_owner partitions
                                            the plane), with all geometry that reaches them, neighbours' included:
                                            each rule reads one cell's column, so the tile's cells come out as a whole
                                            compare's. The slice's densest tile: 1.7 GB, against 16.6 GB whole
  compare-tiles <map> <scene> <out>         compare per tile (--workers, each started into --free-gb), then stitch
          --tiles T
  stitch  <out> <tile dirs>                 tiles' compares onto one grid, as one compare's outputs (collide, drop)
  collide <cats.npz> <out>                  the collision compare's categories clustered:
                                            visible floors with no collision under them (fall through) and collision
                                            tops with nothing visible on them (invisible lumps, walls)
  drop    <map.pk3> <collide dir> <out>     every flagged surface (per 16 u cell) tested: a JKA player box let fall
          [--engine N --scene PK3           onto each fall-through surface through the map's brushes (the engine's
           --map NAME]                      box trace, offline) holds (within a step: a gap narrower than the player),
                                            falls or finds nothing (void); with no free start within 14 u, the solid
                                            over it says why (covered, low, blocked aside). Each invisible top: its
                                            height over the visible floor under it (bump, lump, wall or roof).
                                            --engine N traces N of the drops (seed 0) with the engine's own trace in
                                            the client (game module command levelcheck_trace), tolerances fixed:
                                            0.25 u, no hit, start in solid
          [--tiles T --tile ID]             one tile's compare folder: its surfaces against the brushes its columns
                                            reach, drop_rows.npz only
  drop-tiles <map> <collide dir> <out>      drop per tile on compare-tiles' folders, merged in a whole drop's order,
          --tiles T                         then its clusters, engine sample and outputs (the map never loaded whole)

The map's collision lumps and the scene's tables and needed ranges are streamed out of the pk3s (BspLumps, Ranges):
a tile's memory is its own content, not the district's.

Each writes <out>/<check>.json (every cluster: area, centre in game units, a setviewpos line) and a top-down PNG with
the clusters ringed. Game units throughout (37.5 a metre). Converted levels are local only."""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import time
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / 'tools'))
import paths  # noqa: E402
import q3bsp  # noqa: E402

SOLID, PLAYERCLIP = 0x1, 0x10     # JKA's (codemp/game/surfaceflags.h); 0x10000 is Quake 3's playerclip (wrong here)
# the checks' client: the 64-bit build (so it does not wait for other users of the 32-bit client), staged
# once into BIN64 under its own exe name, so other checks for a running xenjk.x86*.exe neither wait on it
# nor stop it; --bits 32 runs the installed exe instead. $XENJK_X64: the x64 build's Release folder (needed by the
# client checks only)
X64 = paths.env_path('XENJK_X64')
BIN64 = paths.LEVELPORT / 'look' / 'levelcheck-bin64'
# the game module with levelcheck_trace (codemp/game/g_levelcheck.c): $XENJK_X64_GAME, a jampgame-only x64 build of
# the working tree's jampgamex86_64.dll (optional: without it the client's own game module is used)
X64_GAME = paths.env_path('XENJK_X64_GAME')
BOX_MIN, BOX_MAX = np.array([-15.0, -15.0, -24.0]), np.array([15.0, 15.0, 40.0])
U = 37.5


def merged_ranges(first, count):
    """(first, count) element ranges, sorted, overlapping and touching ones merged"""
    first, end = np.asarray(first, np.int64), np.asarray(first, np.int64) + np.asarray(count, np.int64)
    keep = end > first
    first, end = first[keep], end[keep]
    o = np.argsort(first, kind='stable')
    first, end = first[o], end[o]
    if not len(first):
        return []
    run = np.maximum.accumulate(end)
    start = np.r_[True, first[1:] > run[:-1]]
    i = np.nonzero(start)[0]
    return list(zip(first[i].tolist(), (np.maximum.reduceat(end, i) - first[i]).tolist()))


class Ranges:
    """Some element ranges of one array in a zip member, read by streaming it (forward seeks, never the whole member
    in memory); rows[ids] by the array's own indices."""

    def __init__(self, z, name, base, dtype, ranges):
        dtype = np.dtype(dtype)
        self.first = np.array([a for a, _ in ranges], np.int64)
        self.local = np.r_[0, np.cumsum([n for _, n in ranges])][:-1].astype(np.int64)
        parts = []
        with z.open(name) as f:
            for a, n in ranges:
                f.seek(base + a * dtype.itemsize)
                parts.append(np.frombuffer(f.read(n * dtype.itemsize), dtype))
        self.data = np.concatenate(parts) if parts else np.zeros(0, dtype)

    def __getitem__(self, ids):
        ids = np.asarray(ids, np.int64)
        k = np.searchsorted(self.first, ids, 'right') - 1
        return self.data[self.local[k] + ids - self.first[k]]

    def span(self, a, n):
        return self[np.arange(a, a + n)]


class MultiRanges(Ranges):
    """Ranges of one array laid end to end over several zip members (a tiled scene's shards), by global index:
    members [(name, section offset, first global index, count)]"""

    def __init__(self, z, members, dtype, ranges):
        dtype = np.dtype(dtype)
        firsts, lens, datas = [], [], []
        for name, ofs, base, count in members:
            local = [(lo - base, hi - lo) for lo, hi in ((max(a, base), min(a + n, base + count)) for a, n in ranges)
                     if hi > lo]
            if local:
                datas.append(Ranges(z, name, ofs, dtype, local).data)
                firsts += [base + x for x, _ in local]
                lens += [n for _, n in local]
        self.first = np.array(firsts, np.int64)          # (members and ranges ascend, so these do)
        self.local = np.r_[0, np.cumsum(lens)][:-1].astype(np.int64)
        self.data = np.concatenate(datas) if datas else np.zeros(0, dtype)


XSCN_DT = {2: np.dtype([('c', '<f4', 4), ('lod', '<u4'), ('nlod', '<u4'), ('pad', '<u4', 2)]),
           3: np.dtype([('part', '<u4'), ('npart', '<u4'), ('screen', '<f4'), ('pad', '<u4')]),
           4: np.dtype([('fi', '<u4'), ('ni', '<u4'), ('vo', '<i4'), ('mi', '<u4')]),
           5: np.dtype([('pad', 'u1', 192), ('kind', '<u4'), ('flags', '<u4'), ('rest', 'u1', 40)]),
           6: np.dtype([('m', '<f4', 12), ('mesh', '<u4'), ('flags', '<u4'), ('pad', '<u4', 2)])}


def xscn_head(z, name):
    """an .xscn member's counts and section offsets (10 each; version 1's 8 and versions 2-5's 9, padded)"""
    import struct
    with z.open(name) as f:
        head = f.read(8 + 8 * 10)
    version = struct.unpack_from('<I', head, 4)[0]
    n = 8 if version == 1 else 9 if version < 6 else 10
    count = list(struct.unpack_from('<%dI' % n, head, 8)) + [0] * (10 - n)
    ofs = list(struct.unpack_from('<%dI' % n, head, 8 + 4 * n)) + [0] * (10 - n)
    return count, ofs


def xscn_tables(z, name, which):
    """some of a member's small sections, in one streamed pass"""
    count, ofs = xscn_head(z, name)
    tab = {}
    with z.open(name) as f:
        for i in sorted(which, key=lambda i: ofs[i]):
            f.seek(ofs[i])
            tab[i] = np.frombuffer(f.read(count[i] * XSCN_DT[i].itemsize), XSCN_DT[i])
    return tab, count, ofs


def scene_packs(pk3):
    """The packs a scene is served from, in the engine's load order (later ones win): pk3 may be several,
    comma-separated; a single zz_<map>_scene.pk3 brings its siblings zz_<map>_scene*.pk3 (the tiled writer's shards
    zz_<map>_scene2.pk3, 3, ..., and overlays such as patch_chunks' zz_<map>_scene_zz_zzzdoors.pk3), ordered as the
    engine orders them (by uppercased name: '_' after the letters, as patch_chunks.scene_patch sorts)."""
    paths = [Path(p) for p in str(pk3).split(',') if p]
    if len(paths) == 1 and paths[0].suffix.lower() == '.pk3':
        paths = list(paths[0].parent.glob(paths[0].stem + '*.pk3')) or paths
    return sorted(paths, key=lambda q: q.name.upper())


class PackSet:
    """Several zips read as one: each member from the last pack in load order that holds it (names compared without
    case, as the engine's filesystem does). namelist/open/read, all Ranges needs."""

    def __init__(self, paths):
        self.zips, self.serve = [], {}
        for p in paths:
            z = zipfile.ZipFile(p)
            self.zips.append(z)
            for n in z.namelist():
                self.serve[n.lower()] = (z, n)

    def namelist(self):
        return [n for _, n in self.serve.values()]

    def where(self, name):
        try:
            return self.serve[name.lower()]
        except KeyError:
            raise KeyError('%s: in none of %s' % (name, [z.filename for z in self.zips])) from None

    def open(self, name):
        z, n = self.where(name)
        return z.open(n)

    def read(self, name):
        z, n = self.where(name)
        return z.read(n)


def scene_source(pk3, window=None):
    """A scene pack's tables and geometry readers, one .xscn or a tiled scene (maps/<map>.xman,
    docs/levelport-scene.md): (meshes, lods, parts, materials, instances, I(ranges), V(ranges)), instances only
    from the tiles whose bounds reach window (x0, y0, x1, y1). Streamed, never a file whole. Members resolve over
    the whole pack set (scene_packs: the shards and overlays, the last pack holding a name serving it)."""
    z = PackSet(scene_packs(pk3))
    man = next((n for n in z.namelist() if n.endswith('.xman')), None)
    if man is None:
        name = next(n for n in z.namelist() if n.endswith('.xscn'))
        tab, count, ofs = xscn_tables(z, name, (2, 3, 4, 5, 6))
        return (tab[2], tab[3], tab[4], tab[5], tab[6],
                lambda r: Ranges(z, name, ofs[1], '<u4', r), lambda r: Ranges(z, name, ofs[0], SCENE_VERT, r))
    libs, matf, tiles = [], None, []
    for ln in z.read(man).decode('ascii').splitlines():
        f = ln.split()
        if f and f[0] == 'lib':
            libs.append(f[1])
        elif f and f[0] == 'mat':
            matf = f[1]
        elif f and f[0] == 'tile':
            tiles.append((f[2], [float(v) for v in f[6:12]]))
    M, L, P, vm, im = [], [], [], [], []
    nv = ni = 0
    for name in libs:
        tab, count, ofs = xscn_tables(z, name, (2, 3, 4))
        M.append(tab[2])
        L.append(tab[3])
        P.append(tab[4])
        vm.append((name, ofs[0], nv, count[0]))
        im.append((name, ofs[1], ni, count[1]))
        nv += count[0]
        ni += count[1]
    mats = xscn_tables(z, matf, (5,))[0][5]
    inst = []
    for name, (x0, y0, z0, x1, y1, z1) in tiles:
        if window is not None and (x1 < window[0] or x0 > window[2] or y1 < window[1] or y0 > window[3]):
            continue
        inst.append(xscn_tables(z, name, (6,))[0][6])
    cat = lambda xs, dt: np.concatenate(xs) if xs else np.zeros(0, dt)
    return (cat(M, XSCN_DT[2]), cat(L, XSCN_DT[3]), cat(P, XSCN_DT[4]), mats, cat(inst, XSCN_DT[6]),
            lambda r: MultiRanges(z, im, '<u4', r), lambda r: MultiRanges(z, vm, SCENE_VERT, r))


PLANE64 = np.dtype([('normal', '<f4', 3), ('dist', '<f8')])


class BspLumps:
    """The lumps the collision checks read (shaders, planes, brushes, brush sides), streamed out of the pk3: a
    district's map is never in memory whole (q3bsp.Bsp reads the file whole and copies every lump).
    pk3 may be several, comma-separated, each holding one BSP or more (a district's collision chunks, cm_loadchunk's
    maps, with the hub's map for the sky shell): their lumps laid end to end, every index moved by the lumps before
    it, as one map. A chunk is in its own frame: a pk3's .cfg lines `cm_loadchunk <bsp> x y z` place it, each plane's
    distance moved by n.T in float64 (planes' dist is float64 here; the engine's own-frame reference, collision doc
    section 12). window (x0, y0, x1, y1): only the BSPs whose world model's bounds, placed, reach it (a tile's check
    reads its own and its neighbours' chunks). self.maps: the BSPs read."""

    def __init__(self, pk3, window=None):
        import struct
        sides_dt = None
        parts = {k: [] for k in ('shaders', 'planes', 'brushes', 'brushsides')}
        n = dict(shaders=0, planes=0, brushsides=0)
        self.maps = []
        # every pack's cm_loadchunk placements first, so a cfg in one pack places BSPs in another (scoped per archive, a hub's cfg left the chunk pack's BSPs at the origin); a BSP placed twice differently is
        # an error, not a silent pick
        at = {}
        for path in str(pk3).split(','):
            with zipfile.ZipFile(path) as z:
                for c in (m for m in z.namelist() if m.endswith('.cfg')):
                    for line in z.read(c).decode('utf-8', 'replace').splitlines():
                        w = line.split()
                        if len(w) == 5 and w[0] == 'cm_loadchunk':
                            T = np.array([float(v) for v in w[2:]])
                            if w[1] in at and not np.array_equal(at[w[1]], T):
                                raise ValueError('%s placed at %s and at %s' % (w[1], at[w[1]], T))
                            at[w[1]] = T
        for path in str(pk3).split(','):
            z = zipfile.ZipFile(path)
            for name in sorted(m for m in z.namelist() if m.endswith('.bsp')):
                T = at.get(name, np.zeros(3))
                with z.open(name) as f:
                    head = f.read(8 + 8 * 17)
                ident, version = struct.unpack_from('<4si', head, 0)
                raven = ident == b'RBSP'
                lumps = [struct.unpack_from('<ii', head, 8 + 8 * i) for i in range(17)]
                if window is not None:
                    w = Ranges(z, name, lumps[q3bsp.L_MODELS][0], q3bsp.MODEL, [(0, 1)]).data[0]
                    lo, hi = w['mins'] + T, w['maxs'] + T
                    if hi[0] < window[0] or lo[0] > window[2] or hi[1] < window[1] or lo[1] > window[3]:
                        continue
                dt = q3bsp.RBRUSHSIDE if raven else q3bsp.BRUSHSIDE
                if sides_dt is not None and dt != sides_dt:
                    sys.exit('BspLumps: %s is not the format of the maps before it' % name)
                sides_dt, self.raven = dt, raven
                got = {}
                for attr, li, d in (('shaders', q3bsp.L_SHADERS, q3bsp.SHADER), ('planes', q3bsp.L_PLANES, q3bsp.PLANE),
                                    ('brushes', q3bsp.L_BRUSHES, q3bsp.BRUSH), ('brushsides', q3bsp.L_BRUSHSIDES, dt)):
                    ofs, ln = lumps[li]
                    got[attr] = Ranges(z, name, ofs, d, [(0, ln // d.itemsize)]).data.copy()
                pl = np.zeros(len(got['planes']), PLANE64)
                pl['normal'] = got['planes']['normal']
                pl['dist'] = got['planes']['dist'] + got['planes']['normal'].astype(np.float64) @ T
                got['planes'] = pl
                got['brushes']['first_side'] += n['brushsides']
                got['brushes']['shader'] += n['shaders']
                got['brushsides']['plane'] += n['planes']
                bs = got['brushsides']['shader']
                bs[bs >= 0] += n['shaders']
                for k in n:
                    n[k] += len(got[k])
                for k in parts:
                    parts[k].append(got[k])
                self.maps.append('%s:%s' % (path, name))
        if not self.maps:
            sys.exit('BspLumps: no map in %s reaches %s' % (pk3, window))
        for k, v in parts.items():
            setattr(self, k, np.concatenate(v))


def load_bsp(pk3, solids=False, window=None):
    """The map in a pk3: with solids, only the collision checks' lumps (BspLumps; several pk3s, and window)"""
    if solids:
        return BspLumps(pk3, window)
    z = zipfile.ZipFile(pk3)
    name = next(n for n in z.namelist() if n.endswith('.bsp'))
    f = tempfile.NamedTemporaryFile(suffix='.bsp', delete=False)
    f.write(z.read(name))
    f.close()
    try:
        return q3bsp.Bsp(f.name)
    finally:
        os.unlink(f.name)


def solid_brushes(b, sky=False):
    """Player-solid brushes (the sky shell only with sky: its floor, far under the level, is where a fall through
    everything ends) as (bounds min (n,3), max (n,3), first side, side count): bounds from the first six sides, axial
    -X +X -Y +Y -Z +Z, as the engine's CM_BoundBrush reads them."""
    flags = b.shaders['content_flags'].astype(np.int64)
    is_sky = np.array([b'sky' in bytes(n).lower() for n in b.shaders['name']])
    br = b.brushes
    keep = ((flags[br['shader']] & (SOLID | PLAYERCLIP)) != 0) & (sky | ~is_sky[br['shader']]) & (br['num_sides'] >= 6)
    br = br[keep]
    fs = br['first_side']
    d = np.stack([b.planes['dist'][b.brushsides['plane'][fs + k]] for k in range(6)], 1).astype(np.float64)
    mn = np.stack([-d[:, 0], -d[:, 2], -d[:, 4]], 1)
    mx = np.stack([d[:, 1], d[:, 3], d[:, 5]], 1)
    return mn, mx, fs, br['num_sides']


def client(base, bits, home):
    """(command line start, working folder) for a check's client with its private home; base: home/base, where
    the 64-bit build's game modules go."""
    import renderer_bench as rb
    if bits == 32:
        exe, cwd = rb.ASSETS / 'xenjk.x86.exe', rb.ASSETS
    else:
        x64 = paths.need('XENJK_X64')
        BIN64.mkdir(parents=True, exist_ok=True)
        for src, dst in ((x64 / 'xenjk.x86_64.exe', BIN64 / 'levelcheck64.exe'),
                         (x64 / 'rd-vulkan_x86_64.dll', BIN64 / 'rd-vulkan_x86_64.dll'),
                         (REPO / 'lib' / 'SDL2' / 'bin' / 'x64' / 'SDL2.dll', BIN64 / 'SDL2.dll')):
            if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
                shutil.copy2(src, dst)
        for m in ('cgame', 'ui', 'jampgame'):
            shutil.copy2(x64 / ('%sx86_64.dll' % m), base / ('%sx86_64.dll' % m))
        if X64_GAME is not None and X64_GAME.exists():
            shutil.copy2(X64_GAME, base / X64_GAME.name)
        exe, cwd = BIN64 / 'levelcheck64.exe', BIN64
    return [str(exe), '+set', 'fs_basepath', str(rb.ASSETS), '+set', 'fs_homepath', str(Path(home).resolve()), '+set', 'fs_game',
            'base', '+set', 'logfile', '2', '+set', 'sv_cheats', '1'], str(cwd)


# the shared game lock ($GAME_LOCK_DIR, default <research>/anime-batch-20261030): slot files, and
# GAME_LOCK_EXCLUSIVE=1 takes all of them for a timing run. Its test for other clients counts xenjk.x86.exe only, so
# the 64-bit check client (levelcheck64.exe) needs no slot and holds nobody up; it only keeps out of a timing run's way, which a client beside it would skew
LOCK_DIR = Path(os.environ.get('GAME_LOCK_DIR') or paths.RESEARCH / 'anime-batch-20261030')
LOCK_SLOTS = int(os.environ.get('GAME_LOCK_SLOTS', '3'))


def timing_run_holds():
    """Someone holds every slot of the game lock (an exclusive timing run): the pid in all of them, else None"""
    pids = set()
    for i in range(LOCK_SLOTS):
        f = LOCK_DIR / ('game.lock' if i == 0 else 'game.lock.%d' % i)
        try:
            if time.time() - f.stat().st_mtime > 1800:      # (the lock's own stale rule)
                return None
            pids.add(f.read_text(encoding='utf-8', errors='replace').split(' ', 1)[0])
        except OSError:
            return None
    return pids.pop() if len(pids) == 1 else None


def client_popen(args, cwd, home):
    """Start a check's client with no console, its output in home/stdout.txt: started from a shell with no console
    of its own (an automated one's), renderer_bench's hidden new console hung before the client wrote its log.
    The 64-bit client waits while a timing run holds the game lock, and takes no slot."""
    import subprocess
    import renderer_bench as rb
    if Path(args[0]).name == 'levelcheck64.exe':
        t0 = time.time()
        while timing_run_holds():
            if time.time() - t0 > 3 * 3600:
                sys.exit('levelcheck: a timing run has held the game lock for 3 hours')
            time.sleep(15)
    out = open(Path(home) / 'stdout.txt', 'wb')
    # never the user's mouse or focus: mouse input off from the
    # command line (an autoexec runs after the window first grabs), and the window starts minimised, not activated
    args = [args[0], '+set', 'in_mouse', '0', '+set', 'in_nograb', '1'] + list(args[1:])
    si = None
    if hasattr(subprocess, 'STARTUPINFO'):
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 7          # SW_SHOWMINNOACTIVE
    return subprocess.Popen(args, cwd=cwd, env=rb.client_env(), stdin=subprocess.DEVNULL, stdout=out,
                            stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0),
                            startupinfo=si)


def box_hits(b, sb, lo, hi, eps=0.1):
    """Indices of the brushes in sb that a box [lo, hi] overlaps by more than eps (the plane test: for every side,
    the box's deepest corner is behind it)."""
    mn, mx, fs, ns = sb
    cand = np.nonzero(np.all(mn < hi - eps, 1) & np.all(mx > lo + eps, 1))[0]
    out = []
    for i in cand:
        pl = b.planes[b.brushsides['plane'][fs[i]:fs[i] + ns[i]]]
        n = pl['normal'].astype(np.float64)
        deepest = np.where(n > 0, lo, hi)          # the box corner farthest behind each plane
        if np.all((n * deepest).sum(1) - pl['dist'] < -eps):
            out.append(int(i))
    return out


def cmd_spawns(a):
    """Each spawn: stuck in a solid brush, and a floor within 64 u under it. a.map may be a hub and its chunk packs
    (comma-separated): the spawns come from the first (the hub), the brushes from every pack, each spawn with the
    chunks placed around it (one pack only would leave the hub alone with no floors)"""
    t0 = time.time()
    packs = str(a.map).split(',')
    hub = load_bsp(packs[0])
    sp = [e for e in hub.entities if e.get('classname', '').startswith('info_player')]
    if len(packs) == 1:
        b = hub
        sb = solid_brushes(b)
    res = []
    for e in sp:
        o = np.array([float(c) for c in e['origin'].split()])
        if len(packs) > 1:
            b = load_bsp(a.map, solids=True, window=(o[0] - 256, o[1] - 256, o[0] + 256, o[1] + 256))
            sb = solid_brushes(b)
        stuck = box_hits(b, sb, o + BOX_MIN, o + BOX_MAX)
        floor = box_hits(b, sb, o + BOX_MIN + [0, 0, -64], o + [15, 15, -23])
        res.append({'classname': e['classname'], 'origin': o.round(1).tolist(), 'stuck_in': len(stuck),
                    'floor_within_64': bool(floor), 'setviewpos': 'setviewpos %.0f %.0f %.0f 0' % tuple(o)})
    bad = [r for r in res if r['stuck_in']]
    fall = [r for r in res if not r['stuck_in'] and not r['floor_within_64']]
    out = Path(a.out) if a.out else None
    nb = len(sb[0]) if sp or len(packs) == 1 else 0     # (several packs: the last spawn's window; no spawn: none)
    summary = {'spawns': len(res), 'stuck': len(bad), 'no_floor_within_64': len(fall), 'brushes': nb,
               'seconds': round(time.time() - t0, 1), 'points': res}
    if out:
        out.mkdir(parents=True, exist_ok=True)
        json.dump(summary, open(out / 'spawns.json', 'w'), indent=1)
    for r in bad:
        print('STUCK %s %s in %d brushes  %s' % (r['classname'], r['origin'], r['stuck_in'], r['setviewpos']))
    for r in fall:
        print('NO FLOOR within 64 u: %s %s  %s' % (r['classname'], r['origin'], r['setviewpos']))
    print('spawns: %d of %d stuck, %d with no floor within 64 u (%d brushes, %.1f s)'
          % (len(bad), len(res), len(fall), nb, time.time() - t0))


# ---------------------------------------------------------------------------------------------------- aerial

def ground_half_width(meta_or_alt, ground=None, fov=None):
    """Half the ground a straight-down shot spans across the screen's width: (alt - ground) tan(fov / 2). Takes an
    aerial.json dict (older shots have no fov: 90) or alt, ground, fov."""
    if isinstance(meta_or_alt, dict):
        meta_or_alt, ground, fov = meta_or_alt['alt'], meta_or_alt['ground'], meta_or_alt.get('fov', 90.0)
    return (meta_or_alt - ground) * float(np.tan(np.radians(fov) / 2))


def map_with_far_clip(src, dst, distance_cull):
    """A copy of a map pk3 whose worldspawn sets distanceCull (the renderer's far plane: distanceCull x 1.732, 10,392 u
    without it): the BSP's entity lump rewritten at the end of the file, every other lump untouched."""
    import struct
    zin = zipfile.ZipFile(src)
    with zipfile.ZipFile(dst, 'w') as zout:
        for info in zin.infolist():
            data = zin.read(info)
            if info.filename.lower().endswith('.bsp'):
                eo, el = struct.unpack_from('<2i', data, 8)
                text = data[eo:eo + el].rstrip(b'\0').decode('latin-1')
                text = text.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"distanceCull" "%d"'
                                    % distance_cull, 1)
                ent = text.encode('latin-1') + b'\0'
                at = (len(data) + 3) & ~3
                data = data.ljust(at, b'\0') + ent
                data = data[:8] + struct.pack('<2i', at, len(ent)) + data[16:]
            zout.writestr(info, data, compress_type=zipfile.ZIP_STORED)


def aerial_tiles(radius, half_w, w, h):
    """Camera centres covering a disc of radius around the origin: a shot spans 2 half_w across the screen's width
    (ground_half_width) and h/w of that along its height; screen up is +x (yaw 0, pitch 90), screen right is -y."""
    half_w = half_w * 0.92                  # overlap
    half_h = half_w * h / w
    xs = np.arange(-radius + half_h, radius + half_h, 2 * half_h)
    ys = np.arange(-radius + half_w, radius + half_w, 2 * half_w)
    return [(float(x), float(y)) for x in xs for y in ys if np.hypot(max(abs(x) - half_h, 0), max(abs(y) - half_w, 0)) < radius]


def cmd_aerial(a):
    import renderer_bench as rb
    out = Path(a.out).resolve()
    run = out / 'run'
    if run.exists():
        shutil.rmtree(run)
    base = run / 'home' / 'base'
    base.mkdir(parents=True)
    ground = a.ground_z if a.ground_z is not None else spawn_ground(a.map)
    if a.alt is None:
        # high and narrow, near orthographic like the reference map (look/realistic-map-20261030): from 8,000 u at
        # 90 degrees a 100 m tower near a shot's edge leaned 87 m across the streets; from 40,000 u at 15, 12 m
        a.alt = ground + 40000.0
    # the far plane is distanceCull x 1.732 (tr_main.cpp R_SetFarClip; 10,392 u when the map sets none, which from
    # 10,000 u cut the canals' water out of the shots): the check's copy of the map sets it 4,000 u under
    # the ground
    far = int(np.ceil((a.alt - ground + 4000.0) / 1.732))
    map_with_far_clip(a.map, base / Path(a.map).name, far)
    for pk in scene_packs(a.scene):         # (the shards and overlays too: a tiled scene spans several packs)
        try:
            os.link(pk, base / pk.name)
        except OSError:
            shutil.copy2(pk, base / pk.name)
    w, h = a.size.split('x')
    w, h = int(w), int(h)
    tiles = aerial_tiles(a.radius, ground_half_width(a.alt, ground, a.fov), w, h)
    sets = {'r_mode': '-1', 'r_customwidth': str(w), 'r_customheight': str(h), 'r_fullscreen': '0', 'r_noborder': '1',
            's_volume': '0', 's_musicvolume': '0', 'cl_motd': '0', 'net_enabled': '0', 'cl_autoRecordDemo': '0',
            'con_notifytime': '0', 'cg_draw2D': '0', 'cg_drawGun': '0', 'cg_fov': '%g' % a.fov, 'cg_fovAspectAdjust': '0',
            'in_nograb': '1', 'in_mouse': '0', 'cg_thirdPerson': '0', 'cl_renderer': 'rd-vulkan', 'bot_enable': '0',
            'sv_pure': '0', 'g_gametype': '0', 'r_fastsky': '7', 'r_sceneDebug': str(a.debug), 'r_sceneLodBias': '64',
            'com_maxfps': '0', 'r_swapInterval': '0'}
    (base / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % kv for kv in sets.items()), encoding='utf-8')
    lines = ['devmap %s' % a.mapname, 'wait 600', 'togglemenu', 'wait 100', 'team free', 'wait 200', 'notarget', 'god',
             'set com_unfocused 0']
    for k, (x, y) in enumerate(tiles):
        lines += ['benchcam %.0f %.0f %.0f 0 90' % (x, y, a.alt), 'wait 120', 'screenshot_png aerial_%03d' % k, 'wait 20']
    lines += ['benchcam off', 'echo LEVELCHECK done', 'wait 30', 'quit']
    (base / 'bench.cfg').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    args, cwd = client(base, a.bits, run / 'home')
    log = base / 'qconsole.log'
    fg = rb.foreground_window()
    t0 = time.time()
    proc = client_popen(args + ['+exec', 'bench.cfg'], cwd, run / 'home')
    while time.time() - t0 < a.timeout and proc.poll() is None:
        rb.hide_client_windows(proc.pid, fg)
        time.sleep(1)
        if log.exists() and 'LEVELCHECK done' in log.read_text(encoding='utf-8', errors='replace'):
            time.sleep(2)
            break
    rb.stop_client(proc)
    shots = sorted((base / 'screenshots').glob('aerial_*.png'))
    # a blank frame is unseen ground too, and its pixels would count as seen and not sky (once the last
    # two shots were black, both at the level's edge)
    blank = [s.name for s in shots if np.asarray(Image.open(s).convert('L')).max() < 8]
    if blank:
        for s in blank:
            os.replace(base / 'screenshots' / s, base / 'screenshots' / ('blank_' + s))
        shots = [s for s in shots if s.name not in blank]
    json.dump({'map': a.mapname, 'alt': a.alt, 'ground': ground, 'fov': a.fov, 'far_clip': far, 'size': [w, h],
               'radius': a.radius,
               'tiles': tiles, 'shots': [s.name for s in shots], 'blank': blank, 'debug': a.debug,
               'wall_s': round(time.time() - t0)},
              open(out / 'aerial.json', 'w'), indent=1)
    print('aerial: %d of %d tiles shot in %.0f s (ground %.0f, camera %.0f)' % (len(shots), len(tiles), time.time() - t0,
                                                                                ground, a.alt))
    if len(shots) < len(tiles):
        # a missing shot is unseen ground, not ground without holes (0 of 14 shot read as 0 holes)
        tail = log.read_text(encoding='utf-8', errors='replace').splitlines()[-15:] if log.exists() else ['(no log)']
        sys.exit('aerial: %d of %d tiles not shot (%d of them blank); console tail:\n  %s'
                 % (len(tiles) - len(shots), len(tiles), len(blank), '\n  '.join(tail)))
    holes(out, a.radius, edge=a.edge)


def cmd_holes_kind(a):
    """What the aerial's sky pixels are under: the same tiles shot with the material index (aerial --debug 8, the
    shots pixel for pixel the same cameras) say per sky pixel whether nothing was drawn there (open: a hole) or a
    surface was, glass and other blend materials drawn through (r_sceneDebug 1 draws them translucent over the
    magenta sky). The index view holds 12 bits (4 a channel): an index stands for every material m + 4096 k, so a
    pixel is blend only when all of them are blend, solid when none is, mixed otherwise. The holes are the open
    pixels, clustered as holes() does."""
    out1, out8 = Path(a.flat), Path(a.index)
    m1, m8 = json.load(open(out1 / 'aerial.json')), json.load(open(out8 / 'aerial.json'))
    if m1['tiles'] != m8['tiles'] or m1['size'] != m8['size'] or len(m1['shots']) != len(m8['shots']):
        sys.exit('holes-kind: the two aerials are not the same cameras')
    kinds = [r.get('kind') for r in json.load(open(a.materials, encoding='utf-8'))]
    n = len(kinds)
    blend = np.array([k == 'blend' for k in kinds] + [False] * (4096 - n % 4096 if n % 4096 else 0))
    blend = blend.reshape(-1, 4096)                     # [k, index]: material index + 4096 k
    real = np.zeros_like(blend)
    real.reshape(-1)[:n] = True
    all_blend = np.all(blend | ~real, 0) & np.any(real, 0)
    any_blend = np.any(blend & real, 0)
    w, h = m1['size']
    d = ground_half_width(m1)
    radius, cell, edge = m1['radius'], 19.0, a.edge
    g = int(np.ceil(2 * radius / cell))
    cnt = {k: np.zeros((g, g), np.int32) for k in ('seen', 'open', 'blend', 'solid', 'mixed')}
    uu, vv = np.meshgrid(np.arange(w) + 0.5, np.arange(h) + 0.5)
    for (cx, cy), s1, s8 in zip(m1['tiles'], m1['shots'], m8['shots']):
        im1 = np.asarray(Image.open(out1 / 'run/home/base/screenshots' / s1).convert('RGB')).astype(np.int16)
        im8 = np.asarray(Image.open(out8 / 'run/home/base/screenshots' / s8).convert('RGB')).astype(np.int16)
        sky = (im1[..., 0] > 180) & (im1[..., 1] < 90) & (im1[..., 2] > 180)
        empty = (im8[..., 0] == 255) & (im8[..., 1] == 0) & (im8[..., 2] == 255)
        idx = (im8[..., 0] // 16) | (im8[..., 1] // 16) << 4 | (im8[..., 2] // 16) << 8
        X = cx + (h / 2 - vv) / (w / 2) * d
        Y = cy - (uu - w / 2) / (w / 2) * d
        i = ((X + radius) / cell).astype(np.int64)
        j = ((Y + radius) / cell).astype(np.int64)
        ok = (i >= 0) & (i < g) & (j >= 0) & (j < g) & (np.hypot(X, Y) < radius - edge)
        np.add.at(cnt['seen'], (i[ok], j[ok]), 1)
        for k, msk in (('open', sky & empty), ('blend', sky & ~empty & all_blend[idx]),
                       ('solid', sky & ~empty & ~any_blend[idx]), ('mixed', sky & ~empty & any_blend[idx] & ~all_blend[idx])):
            np.add.at(cnt[k], (i[ok & msk], j[ok & msk]), 1)
    seen = np.maximum(cnt['seen'], 1)
    sky_frac = (cnt['open'] + cnt['blend'] + cnt['solid'] + cnt['mixed']) / seen
    hole_cells = sky_frac > 0.5                          # holes()'s cells
    open_cells = cnt['open'] / seen > 0.5
    cm2 = (cell / U) ** 2
    rep = {'cell_u': cell, 'hole_cells_m2': round(float(hole_cells.sum()) * cm2, 1),
           'open_cells_m2': round(float(open_cells.sum()) * cm2, 1),
           'sky_pixels_by_kind': {k: int(cnt[k].sum()) for k in ('open', 'blend', 'solid', 'mixed')}}
    # holes()'s clusters, each by what its sky pixels are under
    lab, k = ndimage.label(hole_cells)
    rows = []
    for idx_, sl in enumerate(ndimage.find_objects(lab), 1):
        cells = lab[sl] == idx_
        area = float(cells.sum()) * cm2
        if area < 0.5:
            continue
        by = {kk: int(cnt[kk][sl][cells].sum()) for kk in ('open', 'blend', 'solid', 'mixed')}
        tot = max(sum(by.values()), 1)
        c = (np.argwhere(cells).mean(0) + [sl[0].start, sl[1].start] + 0.5) * cell - radius
        rows.append({'area_m2': round(area, 1), 'x': round(float(c[0])), 'y': round(float(c[1])),
                     'share': {kk: round(v / tot, 3) for kk, v in by.items()},
                     'verdict': 'hole' if by['open'] / tot > 0.5 else 'behind glass' if by['blend'] / tot > 0.5 else
                     'unclear'})
    rows.sort(key=lambda r: -r['area_m2'])
    rep['clusters'] = rows
    rep['by_verdict_m2'] = {v: round(sum(r['area_m2'] for r in rows if r['verdict'] == v), 1)
                            for v in ('hole', 'behind glass', 'unclear')}
    Path(a.out).mkdir(parents=True, exist_ok=True)
    json.dump(rep, open(Path(a.out) / 'holes_kind.json', 'w'), indent=1)
    print(json.dumps({k: v for k, v in rep.items() if k != 'clusters'}, indent=1))
    for r in rows[:20]:
        print('  %6.1f m2 at %6d %6d  %-13s %s' % (r['area_m2'], r['x'], r['y'], r['verdict'], r['share']))


def spawn_ground(map_pk3):
    b = load_bsp(str(map_pk3).split(',')[0])        # (the hub holds the spawns)
    z = [float(e['origin'].split()[2]) - 24 for e in b.entities if e.get('classname', '').startswith('info_player')]
    return float(np.median(z)) if z else 0.0


def hole_grid(out, radius, cell=19.0, edge=1500.0):
    """An aerial run's shots projected to the ground plane: per cell (x, y from -radius), pixels seen and pixels of
    sky, inside radius - edge. Reads only."""
    out = Path(out)
    meta = json.load(open(out / 'aerial.json'))
    w, h = meta['size']
    d = ground_half_width(meta)
    n = int(np.ceil(2 * radius / cell))
    hole = np.zeros((n, n), np.int32)
    seen = np.zeros((n, n), np.int32)
    shots = out / 'run' / 'home' / 'base' / 'screenshots'
    uu, vv = np.meshgrid(np.arange(w) + 0.5, np.arange(h) + 0.5)
    for (cx, cy), name in zip(meta['tiles'], meta['shots']):
        im = np.asarray(Image.open(shots / name).convert('RGB')).astype(np.int16)
        if im.shape[:2] != (h, w):      # (the window did not come up at --size: unseen ground is not hole-free)
            sys.exit('holes: %s is %dx%d, not the size the run asked for, %dx%d' % (name, im.shape[1], im.shape[0], w, h))
        if meta.get('debug', 1) == 8:
            # the material index view: exactly the sky's clear colour where no surface was drawn at all (glass and
            # cutouts are drawn whole there; an index colour never reaches 255 or 0)
            sky = (im[..., 0] == 255) & (im[..., 1] == 0) & (im[..., 2] == 255)
        else:
            sky = (im[..., 0] > 180) & (im[..., 1] < 90) & (im[..., 2] > 180)
        X = cx + (h / 2 - vv) / (w / 2) * d
        Y = cy - (uu - w / 2) / (w / 2) * d
        i = ((X + radius) / cell).astype(np.int64)
        j = ((Y + radius) / cell).astype(np.int64)
        ok = (i >= 0) & (i < n) & (j >= 0) & (j < n) & (np.hypot(X, Y) < radius - edge)
        np.add.at(seen, (i[ok], j[ok]), 1)
        np.add.at(hole, (i[ok & sky], j[ok & sky]), 1)
    return hole, seen


def holes(out, radius, cell=19.0, min_m2=0.5, edge=1500.0):
    """Sky pixels (magenta) inside radius - edge, projected to the ground plane and clustered. The edge band is left
    out: a level port keeps a placement by its origin, so pieces straddling the boundary are missing there by design."""
    out = Path(out)
    meta = json.load(open(out / 'aerial.json'))
    hole, seen = hole_grid(out, radius, cell, edge)
    n = len(hole)
    c = (np.arange(n) + 0.5) * cell - radius
    inner = np.hypot(c[:, None], c[None, :]) < radius - edge - cell
    if (inner & (seen == 0)).any():
        print('holes: %d cells inside radius - edge were never seen: not judged' % int((inner & (seen == 0)).sum()),
              flush=True)
    frac = np.where(seen > 0, hole / np.maximum(seen, 1), 0)
    mask = frac > 0.5
    # the counts per cell (x index, y index from -radius): runs compare cell for cell (the noise floor)
    np.savez_compressed(out / 'holes.npz', hole=hole, seen=seen, cell=cell, radius=radius, edge=edge)
    lab, k = ndimage.label(mask)
    clusters = []
    for idx, sl in enumerate(ndimage.find_objects(lab), 1):
        cells = np.argwhere(lab[sl] == idx) + [sl[0].start, sl[1].start]
        area = len(cells) * (cell / U) ** 2
        if area < min_m2:
            continue
        c = (cells.mean(0) + 0.5) * cell - radius
        clusters.append({'area_m2': round(area, 1), 'x': round(float(c[0])), 'y': round(float(c[1])),
                         'size_m': [round(float(np.ptp(cells[:, 0]) + 1) * cell / U, 1),
                                    round(float(np.ptp(cells[:, 1]) + 1) * cell / U, 1)],
                         'setviewpos': 'setviewpos %.0f %.0f %.0f 0' % (c[0], c[1], meta['ground'] + 600)})
    clusters.sort(key=lambda c: -c['area_m2'])
    # the map, north (+y) up: covered ground grey, holes red, the 20 largest ringed and numbered
    img = np.zeros((n, n, 3), np.uint8)
    img[seen > 0] = (70, 70, 70)
    img[mask] = (255, 40, 40)
    im = Image.fromarray(np.transpose(img, (1, 0, 2))[::-1])
    s = max(1, 1600 // n)
    im = im.resize((n * s, n * s), Image.NEAREST)
    dr = ImageDraw.Draw(im)
    for r, c in enumerate(clusters[:20], 1):
        px, py = (c['x'] + radius) / cell * s, (n - (c['y'] + radius) / cell) * s
        rr = max(8, max(c['size_m']) * U / cell * s / 2 + 4)
        dr.ellipse([px - rr, py - rr, px + rr, py + rr], outline=(255, 255, 0), width=2)
        dr.text((px + rr + 2, py - 6), str(r), fill=(255, 255, 0))
    im.save(out / 'holes.png')
    total = sum(c['area_m2'] for c in clusters)
    json.dump({'clusters': clusters, 'total_m2': round(total, 1), 'cell_u': cell, 'radius': radius, 'edge_excluded': edge},
              open(out / 'holes.json', 'w'), indent=1)
    print('holes: %d clusters over %.0f m2 (sky seen from above, %.0f u edge band excluded); largest:' % (len(clusters), total, edge))
    for r, c in enumerate(clusters[:15], 1):
        print('  %2d  %7.1f m2  %5.1f x %5.1f m  at %6d %6d   %s' % (r, c['area_m2'], *c['size_m'], c['x'], c['y'],
                                                                   c['setviewpos']))


def cmd_holes(a):
    holes(a.out, a.radius, edge=a.edge)


# ---------------------------------------------------------------------------------------------------- compare

CELL = 16.0
ZMAX = 400.0 * U    # the district builds' cp2gltf --col-height 400 m (cp77_tiles.py): nothing above it has collision
WATER_UNDER = 4.5 * U   # a water bed's top lies this far under its surface at most (cp2gltf WATER_DEPTH 4 m)
# (FF7 Rebirth, ff7rscene.py: the Massive tree, grass and small-nature streams)
NOT_FLOOR = re.compile(r'/vegetation/(palms|trees)/|/umbrella/|parasol|'
                       r'_\d\dTree_|Tree_Terrain_Strip/|Grass_Terrain_Strip/|NatureMini|_Grass_|_Leaf|_Leaves', re.I)   # scene meshes (materials.json), see load_scene
SCENE_VERT = np.dtype([('pos', '<f4', 3), ('nrm', 'i1', 4), ('tan', 'i1', 4), ('uv', '<f4', 2)])


def load_scene(pk3, window=None):
    """World-space triangles (n, 3, 3) of LOD 0 of every instance that may be walked on (facing up by its vertex
    normals, nz > 0.5, or two-sided and steep either way; not blend, a window or water), the water surfaces'
    triangles, and how many triangles there are in all. window (x0, y0, x1, y1): only the instances whose bounding
    sphere reaches it."""
    # streamed, never whole (scene_source: one .xscn or a tiled scene): the tables, then the index ranges of the LOD 0
    # parts the kept instances draw, then the vertex ranges those indices name
    meshes, lods, parts, mats, inst, read_I, read_V = scene_source(pk3, window)
    kinds = list(zip(mats['kind'].tolist(), mats['flags'].tolist()))
    # scene v5: the flags word is an instance's near distance (f32; 0 before v5); one above 0 is CP2077's far proxy,
    # drawn only from there out, never a floor anyone stands on (proxies counted as floors otherwise)
    inst = inst[~(inst['flags'].view('<f4') > 0)]
    if window is not None:
        R = inst['m'].reshape(-1, 3, 4).astype(np.float64)
        sc = meshes['c'][inst['mesh']].astype(np.float64)
        c = np.einsum('kij,kj->ki', R[:, :, :3], sc[:, :3]) + R[:, :, 3]
        r = sc[:, 3] * np.sqrt((R[:, :, :3] ** 2).sum((1, 2)))        # (the Frobenius norm bounds the largest scale)
        x0, y0, x1, y1 = window
        dx = np.maximum(np.maximum(x0 - c[:, 0], c[:, 0] - x1), 0)
        dy = np.maximum(np.maximum(y0 - c[:, 1], c[:, 1] - y1), 0)
        inst = inst[dx * dx + dy * dy <= r * r]
    lod0 = lods[meshes['lod'][np.unique(inst['mesh'])]]
    used = np.concatenate([np.arange(a, a + k) for a, k in zip(lod0['part'], lod0['npart'])]) if len(lod0) else \
        np.zeros(0, np.int64)
    I = read_I(merged_ranges(parts['fi'][used], parts['ni'][used]))
    vr = [(int(p['vo']) + int(ix.min()), int(ix.max()) - int(ix.min()) + 1)
          for p, ix in ((parts[k], I.span(parts[k]['fi'], parts[k]['ni'])) for k in used) if len(ix)]
    V = read_V(merged_ranges([a for a, _ in vr], [k for _, k in vr]))
    # (beside the scene pack; a comma-separated list: beside its first pack)
    mj = Path(str(pk3).split(',')[0]).with_name('materials.json')
    recs = json.loads(mj.read_text(encoding='utf-8')) if mj.exists() else []
    if not recs:
        print('load_scene: no %s: water and canopies are judged as floors' % mj, flush=True)
    water = {m['index'] for m in recs if m.get('mesh') == 'water'}
    import cp2gltf
    # not floors: tree and palm canopies, umbrellas (CP2077 has no collision on them either), and what the
    # converter leaves walk-through by name (grass tufts, litter: cp2gltf COL_NONE_NAMES); on the Watson slice these
    # were most of the confirmed falls
    nofloor = {m['index'] for m in recs if NOT_FLOOR.search(m.get('mesh', '')) or
               cp2gltf.COL_NONE_NAMES.search(m.get('mesh', '').rsplit('/', 1)[-1].lower())}
    T, Wt, total = [], [], 0
    order = np.argsort(inst['mesh'], kind='stable')
    ms, starts = np.unique(inst['mesh'][order], return_index=True)
    for mi, s0, s1 in zip(ms, starts, list(starts[1:]) + [len(order)]):
        lod = lods[meshes[mi]['lod']]
        tris, nvs, walk, two, wet = [], [], [], [], []
        for p in parts[lod['part']:lod['part'] + lod['npart']]:
            kind, flags = kinds[p['mi']]
            idx = I.span(p['fi'], p['ni']).astype(np.int64) + p['vo']
            v = V[idx]
            tris.append(v['pos'].reshape(-1, 3, 3).astype(np.float32))
            nvs.append(v['nrm'][:, :3].reshape(-1, 3, 3).astype(np.float32).sum(1))
            k = len(tris[-1])
            walk.append(np.full(k, kind != 2 and not flags & 2 and int(p['mi']) not in water and
                                int(p['mi']) not in nofloor))
            two.append(np.full(k, bool(flags & 1)))
            wet.append(np.full(k, int(p['mi']) in water))
        if not tris:
            continue
        t, nv = np.concatenate(tris), np.concatenate(nvs)
        walk, two, wet = np.concatenate(walk), np.concatenate(two), np.concatenate(wet)
        for c0 in range(s0, s1, max(1, 2000000 // max(len(t), 1))):     # instances in chunks: memory
            M = inst['m'][order[c0:min(s1, c0 + max(1, 2000000 // max(len(t), 1)))]].reshape(-1, 3, 4).astype(np.float32)
            P = np.einsum('kij,ntj->knti', M[:, :, :3], t) + M[:, None, None, :, 3]
            g = np.cross(P[:, :, 1] - P[:, :, 0], P[:, :, 2] - P[:, :, 0])
            wn = np.einsum('kij,nj->kni', M[:, :, :3], nv)
            g = np.where(((g * wn).sum(-1) < 0)[..., None], -g, g)
            nz = g[..., 2] / (np.linalg.norm(g, axis=-1) + 1e-12)
            near = np.ones(P.shape[:2], bool)
            if window is not None:          # (a large mesh reaches every window: only its triangles that do)
                x0, y0, x1, y1 = window
                near = ((P[..., 0].max(-1) >= x0) & (P[..., 0].min(-1) <= x1) & (P[..., 1].max(-1) >= y0) &
                        (P[..., 1].min(-1) <= y1))
            T.append(P[walk[None] & ((nz > 0.5) | (two[None] & (np.abs(nz) > 0.5))) & near])
            total += P.shape[0] * P.shape[1]
            if wet.any():
                Wt.append(P[wet[None] & near])
    return (np.concatenate(T) if T else np.zeros((0, 3, 3), np.float32),
            np.concatenate(Wt) if Wt else np.zeros((0, 3, 3), np.float32), total)


def walk_samples(T, lo, nx, ny, step=2000000):
    """(cell index, z) at every cell centre a triangle covers (its plane's height there); in batches of triangles"""
    if len(T) > step:
        out = [walk_samples(T[k:k + step], lo, nx, ny, step) for k in range(0, len(T), step)]
        return np.concatenate([o[0] for o in out]), np.concatenate([o[1] for o in out])
    A, B, C = T[:, 0].astype(np.float64), T[:, 1].astype(np.float64), T[:, 2].astype(np.float64)
    mn = np.minimum(np.minimum(A, B), C)
    mx = np.maximum(np.maximum(A, B), C)
    i0 = np.ceil((mn[:, 0] - lo[0]) / CELL - 0.5).astype(np.int64)
    i1 = np.floor((mx[:, 0] - lo[0]) / CELL - 0.5).astype(np.int64)
    j0 = np.ceil((mn[:, 1] - lo[1]) / CELL - 0.5).astype(np.int64)
    j1 = np.floor((mx[:, 1] - lo[1]) / CELL - 0.5).astype(np.int64)
    # the cell range clamped to the grid before expanding: a district-sized terrain triangle touching one tile
    # window was expanded over its whole box, then trimmed (memory)
    i0, j0 = np.maximum(i0, 0), np.maximum(j0, 0)
    i1, j1 = np.minimum(i1, nx - 1), np.minimum(j1, ny - 1)
    w = np.maximum(i1 - i0 + 1, 0)
    h = np.maximum(j1 - j0 + 1, 0)
    cnt = w * h
    keep = cnt > 0
    tri = np.repeat(np.nonzero(keep)[0], cnt[keep])
    k = np.arange(len(tri)) - np.repeat(np.cumsum(cnt[keep]) - cnt[keep], cnt[keep])
    ii = i0[tri] + k % w[tri]
    jj = j0[tri] + k // w[tri]
    px = lo[0] + (ii + 0.5) * CELL
    py = lo[1] + (jj + 0.5) * CELL
    a, b, c = A[tri], B[tri], C[tri]
    v0 = b[:, :2] - a[:, :2]
    v1 = c[:, :2] - a[:, :2]
    v2 = np.stack([px, py], 1) - a[:, :2]
    den = v0[:, 0] * v1[:, 1] - v1[:, 0] * v0[:, 1]
    with np.errstate(divide='ignore', invalid='ignore'):
        u = (v2[:, 0] * v1[:, 1] - v1[:, 0] * v2[:, 1]) / den
        v = (v0[:, 0] * v2[:, 1] - v2[:, 0] * v0[:, 1]) / den
    inside = ((u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6) & (np.abs(den) > 1e-9) & (ii >= 0) & (ii < nx) &
              (jj >= 0) & (jj < ny))
    zz = a[:, 2] + u * (b[:, 2] - a[:, 2]) + v * (c[:, 2] - a[:, 2])
    return (jj * nx + ii)[inside], zz[inside]


def solid_intervals(b, sb, lo, nx, ny):
    """The player-solid intervals over every cell centre, overlapping or touching ones (0.05 u) merged (a top inside
    another brush is no floor): (cell, zb, zt), sorted by cell then height. Each brush over the cells its bounds
    cover, a vertical line through it between its down sides' highest and its up sides' lowest crossing."""
    mn, mx, fs, ns = sb
    i0 = np.maximum(np.ceil((mn[:, 0] - lo[0]) / CELL - 0.5), 0).astype(np.int64)
    i1 = np.minimum(np.floor((mx[:, 0] - lo[0]) / CELL - 0.5) + 1, nx).astype(np.int64)
    j0 = np.maximum(np.ceil((mn[:, 1] - lo[1]) / CELL - 0.5), 0).astype(np.int64)
    j1 = np.minimum(np.floor((mx[:, 1] - lo[1]) / CELL - 0.5) + 1, ny).astype(np.int64)
    w, h = np.maximum(i1 - i0, 0), np.maximum(j1 - j0, 0)
    C, ZB, ZT = [], [], []
    for k in np.unique(ns):
        sel = np.nonzero((ns == k) & (w > 0) & (h > 0))[0]
        if not len(sel):
            continue
        pl = b.planes[b.brushsides['plane'][fs[sel][:, None] + np.arange(k)]]
        N, D = pl['normal'].astype(np.float64), pl['dist'].astype(np.float64)
        cnt = (w * h)[sel]
        step = max(1, int(4e6 // k))
        ends = np.searchsorted(np.cumsum(cnt), np.arange(step, cnt.sum() + step, step))
        a0 = 0
        for a1 in np.unique(np.minimum(ends + 1, len(sel))):
            if a1 <= a0:
                continue
            br = np.arange(a0, a1)
            c = cnt[br]
            rep = np.repeat(br, c)
            off = np.arange(len(rep)) - np.repeat(np.cumsum(c) - c, c)
            g = sel[rep]
            ii, jj = i0[g] + off % w[g], j0[g] + off // w[g]
            px, py = lo[0] + (ii + 0.5) * CELL, lo[1] + (jj + 0.5) * CELL
            n = N[rep]
            r = D[rep] - n[..., 0] * px[:, None] - n[..., 1] * py[:, None]
            nz = n[..., 2]
            with np.errstate(divide='ignore', invalid='ignore'):
                q = r / nz
            zt = np.where(nz > 1e-6, q, np.inf).min(1)
            zb = np.where(nz < -1e-6, q, -np.inf).max(1)
            ok = np.all((np.abs(nz) > 1e-6) | (r >= -1e-3), 1) & (zb < zt)
            C.append(jj[ok] * nx + ii[ok])
            ZB.append(zb[ok])
            ZT.append(zt[ok])
            a0 = a1
    if not C:
        return np.zeros(0, np.int64), np.zeros(0), np.zeros(0)
    cell = np.concatenate(C)
    zb, zt = np.clip(np.concatenate(ZB), -1e5, 1e5), np.clip(np.concatenate(ZT), -1e5, 1e5)
    o = np.lexsort((zb, cell))
    cell, zb, zt = cell[o], zb[o], zt[o]
    (kt,), allz, n = lexkeys((cell, zt))
    run = allz[np.maximum.accumulate(kt) % n]           # the highest top so far in this cell (cells ascending)
    start = np.ones(len(cell), bool)
    start[1:] = (cell[1:] != cell[:-1]) | (zb[1:] > run[:-1] + 0.05)
    first = np.nonzero(start)[0]
    return cell[first], zb[first], np.maximum.reduceat(zt, first)


def lexkeys(*pairs):
    """(cell, height) pairs as exact int64 keys in one order: cell, then the height's rank among all the heights
    given. (The key used to be cell * 1e6 + z in float64, which rounds by 1/4,000 u at a few million cells:
    a rule's limit hit exactly went either way, and a district-sized grid would round coarser still.) ->
    (keys per pair, the sorted heights, the rank base n: key = cell * n + rank)"""
    allz = np.unique(np.concatenate([np.asarray(z, np.float64) for _, z in pairs]))
    n = len(allz) + 1
    return [np.asarray(c, np.int64) * n + np.searchsorted(allz, np.asarray(z, np.float64)) for c, z in pairs], allz, n


def keyed(cell, z):
    """(cell, z) sorted by cell, then height"""
    o = np.lexsort((z, cell))
    return np.asarray(cell)[o], np.asarray(z)[o]


def any_within(kc, kz, cell, z0, z1):
    """per query (cell, z0, z1): a sorted (kc, kz) of that cell lies in [z0, z1]"""
    (k, q0, q1), _, _ = lexkeys((kc, kz), (cell, z0), (cell, z1))
    if not len(k):          # (no solid at all: nothing is within; k[-1] would fail)
        return np.zeros(len(q0), bool)
    at = np.searchsorted(k, q0, 'left')
    return (at < len(k)) & (k[np.minimum(at, len(k) - 1)] <= q1)


def tile_owner(man, tid):
    """A partition of the plane among an area's tiles (cp2gltf.py tiles: tiles.json, CP2077 metres; game units from
    the area's centre): a cell belongs to the tile whose square holds its centre; past the tiles' bounding box it is
    taken back to the box's edge, and a square of the box with no tile goes to the nearest tile (squares apart, then
    the lower id). -> (the tile's reach (x0, y0, x1, y1), one cell wider, infinite on a side at the box's edge;
    owns(x, y) -> bool array)"""
    size, (cx, cy) = man['tile_size'], man['centre']
    tiles = sorted(man['tiles'], key=lambda t: t['id'])
    I, J = np.array([t['i'] for t in tiles]), np.array([t['j'] for t in tiles])
    i0, i1, j0, j1 = I.min(), I.max(), J.min(), J.max()
    gi, gj = np.meshgrid(np.arange(i0, i1 + 1), np.arange(j0, j1 + 1))
    d = (gi[..., None] - I) ** 2 + (gj[..., None] - J) ** 2
    table = d.argmin(-1)                    # (first minimum: the lower id)
    me = next(k for k, t in enumerate(tiles) if t['id'] == tid)

    def owns(x, y):
        i = np.clip(np.floor((np.asarray(x) / U + cx) / size).astype(np.int64), i0, i1)
        j = np.clip(np.floor((np.asarray(y) / U + cy) / size).astype(np.int64), j0, j1)
        return table[j - j0, i - i0] == me
    sj, si = np.nonzero(table == me)
    a0, a1, b0, b1 = si.min() + i0, si.max() + i0, sj.min() + j0, sj.max() + j0
    x0 = -np.inf if a0 == i0 else (a0 * size - cx) * U - CELL
    x1 = np.inf if a1 == i1 else ((a1 + 1) * size - cx) * U + CELL
    y0 = -np.inf if b0 == j0 else (b0 * size - cy) * U - CELL
    y1 = np.inf if b1 == j1 else ((b1 + 1) * size - cy) * U + CELL
    return (x0, y0, x1, y1), owns


def cmd_stitch(a):
    """Tiles' compares (compare --tiles --tile) into one: their own cells on one grid, as one compare's outputs
    (cats.npz, samples.npz, solid.npz, stats.json, overview.png) for collide and drop."""
    t0 = time.time()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    dirs = [Path(d) for d in a.tiles]
    heads = []
    for d in dirs:
        with np.load(d / 'cats.npz') as z:
            heads.append((np.array(z['lo']), z['cat'].shape[1], z['cat'].shape[0]))
    lo = np.min([h[0] for h in heads], 0)
    hi = np.max([h[0] + np.array([h[1], h[2]]) * CELL for h in heads], 0)
    nx, ny = int(round((hi[0] - lo[0]) / CELL)), int(round((hi[1] - lo[1]) / CELL))
    cat = np.zeros((ny, nx, 4), np.int32)
    taken = np.zeros((ny, nx), bool)
    S = {k: [] for k in ('miss_cell', 'miss_z', 'inv_cell', 'inv_z', 'vis_cell', 'vis_z')}
    O = {k: [] for k in ('cell', 'zb', 'zt')}
    st = {'walk_samples': 0, 'matched': 0, 'buried': 0, 'missing': 0, 'invisible_collision_tops': 0,
          'water_bed_tops': 0, 'owned_cells': 0}
    for d, (tlo, tnx, tny) in zip(dirs, heads):
        oi, oj = int(round((tlo[0] - lo[0]) / CELL)), int(round((tlo[1] - lo[1]) / CELL))

        def remap(c):
            j, i = np.divmod(np.asarray(c, np.int64), tnx)
            return (j + oj) * nx + i + oi
        with np.load(d / 'cats.npz') as z:
            own = z['owned']
            win = taken[oj:oj + tny, oi:oi + tnx]
            if (win & own).any():
                sys.exit('stitch: %s owns %d cells another tile owns' % (d, int((win & own).sum())))
            win |= own
            cat[oj:oj + tny, oi:oi + tnx][own] = z['cat'][own]
        with np.load(d / 'samples.npz') as z:
            for k in S:
                S[k].append(remap(z[k]) if k.endswith('_cell') else z[k])
        with np.load(d / 'solid.npz') as z:
            O['cell'].append(remap(z['cell']))
            O['zb'].append(z['zb'])
            O['zt'].append(z['zt'])
        t = json.load(open(d / 'stats.json'))
        st['walk_samples'] += t['walk_samples']
        for k in ('matched', 'buried', 'missing'):
            st[k] += t['visible_surfaces'][k]
        for k in ('invisible_collision_tops', 'water_bed_tops', 'owned_cells'):
            st[k] += t[k]
    S = {k: np.concatenate(v) for k, v in S.items()}
    oc, ozb, ozt = (np.concatenate(O[k]) for k in ('cell', 'zb', 'zt'))
    o = np.lexsort((ozb, oc))
    np.savez_compressed(out / 'solid.npz', cell=oc[o].astype(np.int32), zb=ozb[o], zt=ozt[o], lo=lo,
                        grid=np.array([nx, ny]))
    np.savez_compressed(out / 'cats.npz', cat=cat, lo=lo, cell=CELL, owned=taken)
    np.savez_compressed(out / 'samples.npz', lo=lo, grid=np.array([nx, ny]), cell=CELL, **S)
    tot = cat.reshape(-1, 4).sum(0)
    stats = dict(cell=CELL, lo=lo.tolist(), grid=[nx, ny], tiles=len(dirs), owned_cells=st['owned_cells'],
                 walk_samples=st['walk_samples'],
                 visible_surfaces=dict(matched=st['matched'], buried=st['buried'], missing=st['missing']),
                 invisible_collision_tops=st['invisible_collision_tops'], water_bed_tops=st['water_bed_tops'],
                 seconds=round(time.time() - t0, 1))
    if [int(v) for v in tot] != [st['matched'], st['buried'], st['missing'], st['invisible_collision_tops']]:
        sys.exit('stitch: the cells hold %s, the tiles counted %s' % (tot.tolist(), stats))
    json.dump(stats, open(out / 'stats.json', 'w'), indent=1)
    img = np.zeros((ny, nx, 3), np.uint8)
    img[cat[..., 0] > 0] = (110, 110, 110)
    img[cat[..., 3] > 0] = (60, 110, 255)
    img[cat[..., 1] > 0] = (255, 150, 0)
    img[cat[..., 2] > 0] = (255, 40, 40)
    Image.fromarray(img[::-1]).resize((nx * 2, ny * 2), Image.NEAREST).save(out / 'overview.png')
    print('stitch: ' + json.dumps(stats))


def cmd_compare_tiles(a):
    """compare per tile (--workers processes, each started once --free-gb of memory is free), then stitch: a
    district's compare in a tile's memory. A tile whose folder holds a finished compare under the same --key is
    kept."""
    import subprocess
    import threading
    from concurrent.futures import ThreadPoolExecutor
    try:
        import psutil
    except ImportError:
        psutil = None
        print('compare-tiles: no psutil: the tiles start with no memory test', flush=True)
    man = json.load(open(a.tiles))
    ids = sorted(t['id'] for t in man['tiles'] if not a.only or t['id'] in a.only.split(','))
    out = Path(a.out)
    tdir = out / 'tiles'
    tdir.mkdir(parents=True, exist_ok=True)
    gate = threading.Lock()

    def one(tid):
        d, keyf = tdir / tid, tdir / (tid + '.key')
        if a.key and keyf.exists() and keyf.read_text() == a.key and (d / 'overview.png').exists():
            return 0
        keyf.unlink(missing_ok=True)
        with gate:      # one start at a time, and only into free memory; then a moment for it to grow
            while psutil and psutil.virtual_memory().available < a.free_gb * 2 ** 30:
                time.sleep(15)
            with open(tdir / (tid + '.log'), 'w') as f:
                pr = subprocess.Popen([sys.executable, '-I', str(Path(__file__).resolve()), 'compare', a.map, a.scene,
                                       str(d), '--tiles', a.tiles, '--tile=' + tid,
                                       *(['--zmax', repr(a.zmax)] if a.zmax is not None else [])],
                                      stdout=f, stderr=subprocess.STDOUT)
            time.sleep(20)
        rc = pr.wait()
        if rc == 0 and a.key:
            keyf.write_text(a.key)
        print('compare-tiles: %s %s' % (tid, 'ok' if rc == 0 else 'FAILED (%d): %s' % (rc, tdir / (tid + '.log'))),
              flush=True)
        return rc
    with ThreadPoolExecutor(max(1, a.workers)) as ex:
        failed = sum(1 for rc in ex.map(one, ids) if rc)
    if failed:
        sys.exit('compare-tiles: %d tiles failed' % failed)
    cmd_stitch(argparse.Namespace(out=str(out), tiles=[str(tdir / t) for t in ids]))


def cmd_compare(a):
    t0 = time.time()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    # --tiles/--tile: one tile's cells (tile_owner), judged with everything that reaches them, its neighbours'
    # geometry too: every rule below reads one cell's column, so a tile's cells come out as the whole level's (stitch
    # puts the tiles back together)
    reach, owns = (None, None) if not getattr(a, 'tile', None) else tile_owner(json.load(open(a.tiles)), a.tile)
    b = load_bsp(a.map, solids=True, window=reach)
    sb = solid_brushes(b)
    if reach is not None:
        mn, mx = sb[0], sb[1]
        k = (mx[:, 0] >= reach[0]) & (mn[:, 0] <= reach[2]) & (mx[:, 1] >= reach[1]) & (mn[:, 1] <= reach[3])
        sb = tuple(v[k] for v in sb)
    Tw, Wt, total = load_scene(a.scene, reach)
    print('compare: %d brushes, %d scene triangles (%d walkable, %d water) in %.0f s'
          % (len(sb[0]), total, len(Tw), len(Wt), time.time() - t0), flush=True)
    # the grid: the walkable surfaces' and the collision's extent together on the level's 16 u lattice (cell edges on
    # multiples of 16 u), with a tile only as far as it reaches. Collision beyond every visible floor (an invisible
    # slab) had no cell to be judged in
    ext = [Tw[:, :, :2].reshape(-1, 2)] if len(Tw) else []
    if len(sb[0]):
        ext += [sb[0][:, :2], sb[1][:, :2]]
    ext = np.concatenate(ext) if ext else np.zeros((1, 2))
    lo, hi = np.floor(ext.min(0) / CELL) * CELL, np.ceil(ext.max(0) / CELL) * CELL
    if reach is not None:
        lo = np.maximum(lo, np.floor(np.array(reach[:2]) / CELL) * CELL)
        hi = np.maximum(np.minimum(hi, np.ceil(np.array(reach[2:]) / CELL) * CELL), lo)
    nx, ny = int(round((hi[0] - lo[0]) / CELL)), int(round((hi[1] - lo[1]) / CELL))
    vc, vz = walk_samples(Tw, lo, nx, ny)
    del Tw
    mc, mzb, mzt = solid_intervals(b, sb, lo, nx, ny)
    owned = np.ones(nx * ny, bool)
    if owns is not None:
        jj, ii = np.divmod(np.arange(nx * ny), nx)
        owned = owns(lo[0] + (ii + 0.5) * CELL, lo[1] + (jj + 0.5) * CELL)
        k = owned[vc]
        vc, vz = vc[k], vz[k]
        k = owned[mc]
        mc, mzb, mzt = mc[k], mzb[k], mzt[k]
    np.savez_compressed(out / 'solid.npz', cell=mc.astype(np.int32), zb=mzb.astype(np.float32),
                        zt=mzt.astype(np.float32), lo=lo, grid=np.array([nx, ny]))
    print('compare: %d walk samples, %d solid intervals on a %d x %d grid in %.0f s'
          % (len(vc), len(mc), nx, ny, time.time() - t0), flush=True)
    cat = np.zeros((nx * ny, 4), np.int32)          # matched, buried, missing, invisible
    # each visible surface (a cell's samples rounded to 1 u, one each), against the merged tops and solids there
    u = np.unique(np.stack([vc.astype(np.float64), np.round(vz)], 1), axis=0)
    zmax = getattr(a, 'zmax', None) or ZMAX
    u = u[u[:, 1] <= zmax]
    uc, uz = u[:, 0].astype(np.int64), u[:, 1]
    # (merged intervals are disjoint, sorted by cell then height: their tops and bottoms both ascend)
    matched = any_within(mc, mzt, uc, uz - 10, uz + 6)
    (bk, q), _, _ = lexkeys((mc, mzb), (uc, uz - 6))
    at = np.searchsorted(bk, q, 'left') - 1     # the last solid starting under z - 6
    atc = np.maximum(at, 0)
    if len(mc):
        buried = ~matched & (at >= 0) & (mc[atc] == uc) & (mzt[atc] > uz + 6)
    else:                   # (a tile with visible floors and no collision: every floor is missing, not a crash)
        buried = np.zeros(len(uc), bool)
    missing = ~matched & ~buried
    for k, m in ((0, matched), (1, buried), (2, missing)):
        np.add.at(cat[:, k], uc[m], 1)
    # each merged top with 64 u free above it, under the converter's height, against what is drawn there
    nxt_free = np.ones(len(mc), bool)
    nxt_free[:-1] = (mc[1:] != mc[:-1]) | (mzb[1:] >= mzt[:-1] + 64)
    cand = nxt_free & (mzt <= zmax) & (mzt < 1e5)
    seen = any_within(*keyed(vc.astype(np.int64), vz.astype(np.float64)), mc, mzt - 6, mzt + 10)
    if len(Wt):
        wc, wz = walk_samples(Wt, lo, nx, ny)
        bed = any_within(*keyed(wc.astype(np.int64), wz.astype(np.float64)), mc, mzt - 6, mzt + WATER_UNDER)
    else:
        bed = np.zeros(len(mc), bool)
    inv = cand & ~seen & ~bed
    np.add.at(cat[:, 3], mc[inv], 1)
    tot = cat.sum(0)
    stats = dict(cell=CELL, lo=lo.tolist(), grid=[nx, ny], tile=getattr(a, 'tile', None),
                 owned_cells=int(owned.sum()),
                 brushes=len(sb[0]), walk_samples=int(len(vc)),
                 visible_surfaces=dict(matched=int(tot[0]), buried=int(tot[1]), missing=int(tot[2])),
                 invisible_collision_tops=int(tot[3]), water_bed_tops=int((cand & ~seen & bed).sum()),
                 seconds=round(time.time() - t0, 1))
    json.dump(stats, open(out / 'stats.json', 'w'), indent=1)
    np.savez_compressed(out / 'cats.npz', cat=cat.reshape(ny, nx, 4), lo=lo, cell=CELL, owned=owned.reshape(ny, nx))
    np.savez_compressed(out / 'samples.npz', miss_cell=uc[missing], miss_z=uz[missing].astype(np.float32),
                        inv_cell=mc[inv], inv_z=mzt[inv].astype(np.float32), vis_cell=vc.astype(np.int64),
                        vis_z=vz.astype(np.float32), lo=lo, grid=np.array([nx, ny]), cell=CELL)
    img = np.zeros((ny, nx, 3), np.uint8)
    cc = cat.reshape(ny, nx, 4)
    img[cc[..., 0] > 0] = (110, 110, 110)
    img[cc[..., 3] > 0] = (60, 110, 255)
    img[cc[..., 1] > 0] = (255, 150, 0)
    img[cc[..., 2] > 0] = (255, 40, 40)
    Image.fromarray(img[::-1]).resize((nx * 2, ny * 2), Image.NEAREST).save(out / 'overview.png')
    print('compare: ' + json.dumps(stats))


COLLIDE_KINDS = ((2, 'fall_through', (255, 40, 40)), (3, 'invisible_solid', (60, 140, 255)))


def collide_clusters(cat, lo, cell, k, min_m2):
    """Clusters of one compare category (8-connected cells), largest first: (area m2, centre x, y, cells (j, i))."""
    lab, n = ndimage.label(cat[..., k] > 0, structure=np.ones((3, 3)))
    cl = []
    for idx, sl in enumerate(ndimage.find_objects(lab), 1):
        cells = np.argwhere(lab[sl] == idx) + [sl[0].start, sl[1].start]
        area = len(cells) * (cell / U) ** 2
        if area >= min_m2:
            cy, cx = (cells.mean(0) + 0.5) * cell + lo[::-1]
            cl.append((area, float(cx), float(cy), cells))
    cl.sort(key=lambda c: -c[0])
    return cl


def cmd_collide(a):
    d = np.load(a.cats)
    cat, lo, cell = d['cat'], d['lo'], float(d['cell'])
    ny, nx, _ = cat.shape
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    img = np.zeros((ny, nx, 3), np.uint8)
    img[cat[..., 0] > 0] = (70, 70, 70)
    for k, name, col in COLLIDE_KINDS:
        img[cat[..., k] > 0] = col
        cl = [{'area_m2': round(area, 1), 'x': round(cx), 'y': round(cy), 'setviewpos': 'setviewpos %.0f %.0f 2000 0' % (cx, cy)}
              for area, cx, cy, _ in collide_clusters(cat, lo, cell, k, a.min_m2)]
        report[name] = {'clusters': cl, 'total_m2': round(sum(c['area_m2'] for c in cl), 1)}
        print('%s: %d clusters over %.0f m2; largest:' % (name, len(cl), report[name]['total_m2']))
        for r, c in enumerate(cl[:10], 1):
            print('  %2d  %7.1f m2 at %6d %6d' % (r, c['area_m2'], c['x'], c['y']))
    im = Image.fromarray(img[::-1])
    im.save(out / 'collide.png')
    json.dump(report, open(out / 'collide.json', 'w'), indent=1)


# ---------------------------------------------------------------------------------------------------- drop

STEP = 18.0     # pmove STEPSIZE: a box resting this far under the visible floor still stands on it
VOID = 1000.0   # a fall farther than this leaves the level


GRID = 256.0
_grids = {}


def brush_grid(sb):
    """{(i, j): brush indices} on a GRID-u xy grid, each brush in every cell its bounds grown by the player box touch
    (a query point's own cell holds every brush its box can reach)."""
    key = id(sb[0])
    if key not in _grids:
        mn, mx = sb[0], sb[1]
        i0 = np.floor((mn[:, 0] + BOX_MIN[0]) / GRID).astype(np.int64)
        i1 = np.floor((mx[:, 0] + BOX_MAX[0]) / GRID).astype(np.int64)
        j0 = np.floor((mn[:, 1] + BOX_MIN[1]) / GRID).astype(np.int64)
        j1 = np.floor((mx[:, 1] + BOX_MAX[1]) / GRID).astype(np.int64)
        w, h = i1 - i0 + 1, j1 - j0 + 1
        cnt = w * h
        br = np.repeat(np.arange(len(mn)), cnt)
        k = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
        ci, cj = i0[br] + k % w[br], j0[br] + k // w[br]
        o = np.lexsort((cj, ci))
        ci, cj, br = ci[o], cj[o], br[o]
        starts = np.nonzero(np.r_[True, (ci[1:] != ci[:-1]) | (cj[1:] != cj[:-1])])[0]
        ends = np.r_[starts[1:], len(br)]
        _grids[key] = {(int(ci[s]), int(cj[s])): br[s:e] for s, e in zip(starts, ends)}
    return _grids[key]


def drop_z(b, sb, x, y, z, floor=-65536.0, hit=None):
    """Where a player box let fall from origin (x, y, z) comes to rest: its origin z; None when nothing stops it above
    floor; 'stuck' when it starts inside a brush. Each side of a brush pushed out by the box's deepest corner (the
    engine's box trace) leaves a vertical line through the brush between its lowest and highest crossing. Inside as
    the engine counts it (cm_trace.cpp CM_TestBoxInBrush): touching bounds overlap, a box on a side's plane is
    inside. hit (a list) gets the normal z of the side it lands on: the engine stops SURFACE_CLIP_EPSILON (0.125 u)
    off that plane, 0.125 / nz above the rest straight down."""
    mn, mx, fs, ns = sb
    lo, hi = np.array([x, y, floor]) + BOX_MIN, np.array([x, y, z]) + BOX_MAX
    near = brush_grid(sb).get((int(np.floor(x / GRID)), int(np.floor(y / GRID))))
    if near is None:
        return None
    cand = near[np.all(mn[near] <= hi, 1) & np.all(mx[near] >= lo, 1)]
    if not len(cand):
        return None
    # every candidate's sides at once: first side of each brush, side -> brush
    k = ns[cand]
    first = np.cumsum(k) - k
    side = np.arange(k.sum()) - np.repeat(first, k) + np.repeat(fs[cand], k)
    pl = b.planes[b.brushsides['plane'][side]]
    n = pl['normal'].astype(np.float64)
    r = pl['dist'] - (n * np.where(n > 0, BOX_MIN, BOX_MAX)).sum(1) - n[:, 0] * x - n[:, 1] * y
    nz = n[:, 2]
    with np.errstate(divide='ignore', invalid='ignore'):
        q = r / nz
    qu = np.where(nz > 1e-6, q, np.inf)
    top = np.minimum.reduceat(qu, first)
    bot = np.maximum.reduceat(np.where(nz < -1e-6, q, -np.inf), first)
    out = np.logical_or.reduceat((np.abs(nz) <= 1e-6) & (r < 0.0), first)     # a vertical side the box is outside of
    ok = ~out & (top >= bot)
    if np.any(ok & (bot <= z) & (z <= top)):
        return 'stuck'
    below = ok & (top < z)
    if not below.any():
        return None
    i = int(np.nonzero(below)[0][np.argmax(top[below])])
    if hit is not None:
        hit.append(float(nz[first[i] + int(np.argmin(qu[first[i]:first[i] + k[i]]))]))
    return float(top[i])


# starts aside the flagged point, nearest first, all within 14 u on each axis: the player box (+-15) still covers the
# point (24 u aside it fell beside narrow ledges, 278 of the slice's 338 first "falls")
DROP_OFFSETS = sorted(((dx, dy) for dx in (-14.0, -7.0, 0.0, 7.0, 14.0) for dy in (-14.0, -7.0, 0.0, 7.0, 14.0)),
                      key=lambda d: d[0] * d[0] + d[1] * d[1])


def drop_from(b, sb, x, y, zs, hit=None):
    """A player dropped onto a surface at height zs: (its box's bottom where it rests, None when nothing stops it,
    'stuck' when no start is free; the start origin used, for stuck the first). From 8 u above zs, else just above it
    (an overhang); a player box (30 u) at a 16 u cell often overlaps a wall beside the surface, so then from up to
    14 u aside. hit: as drop_z's, for the start used."""
    first = (x, y, zs - BOX_MIN[2] + 8.0)
    for dx, dy in DROP_OFFSETS:
        for lift in (8.0, 1.0):
            start = (x + dx, y + dy, zs - BOX_MIN[2] + lift)
            h = []
            got = drop_z(b, sb, *start, hit=h)
            if got != 'stuck':
                if hit is not None:
                    hit += h
                return (None if got is None else got + BOX_MIN[2]), start
    return 'stuck', first


ROOM = (('covered', 16.0), ('low', 64.0))   # no player fits: a solid this close over the surface (else blocked aside)
FALL_RESULTS = ('falls', 'void', 'holds', 'covered', 'low', 'blocked')
INVISIBLE_BANDS = (('bump', 18.0), ('lump', 64.0), ('wall_or_roof', np.inf))


DROP_REACH = 32.0    # how far aside a drop's box reaches: DROP_OFFSETS' 14 u and the box's 15 u


def drop_surfaces(b, sb, sm, so, lo, nx, cell):
    """The offline drops of one compare's flagged surfaces (whole or one tile's, its samples and solids) as arrays:
    fall-through surfaces u (cell, z; a cell's surfaces once per whole unit of height), each one's result, rest,
    start, what the engine should find (kind 0 lands at want, 1 void, 2 stuck) and the landing side's normal z;
    invisible tops (ic, iz) and their band."""
    s_cell, s_zb, s_zt = so['cell'].astype(np.int64), so['zb'].astype(np.float64), so['zt'].astype(np.float64)

    def solids(c):
        i0, i1 = np.searchsorted(s_cell, c), np.searchsorted(s_cell, c, 'right')
        return s_zb[i0:i1], s_zt[i0:i1]

    mc, mz = sm['miss_cell'].astype(np.int64), np.round(sm['miss_z'].astype(np.float64))
    u = np.unique(np.stack([mc, mz], 1), axis=0).reshape(-1, 2)
    n = len(u)
    result = np.empty(n, object)
    rest_z, want, nz = np.full(n, np.nan), np.full(n, np.nan), np.full(n, np.nan)
    start, kind = np.zeros((n, 3)), np.zeros(n, np.int8)
    for k, (c, zs) in enumerate(u):
        c = int(c)
        x, y = lo[0] + (c % nx + 0.5) * cell, lo[1] + (c // nx + 0.5) * cell
        hit = []
        rest, start[k] = drop_from(b, sb, x, y, zs, hit)
        if rest == 'stuck':
            zb, zt = solids(c)
            over = zb[(zt > zs + 6.0) & (zb > zs - 6.0)]
            gap = over.min() - zs if len(over) else np.inf
            result[k] = next((name for name, lim in ROOM if gap < lim), 'blocked')
            kind[k] = 2
        else:
            # void: off the level, more than 1,000 u (27 m) down (the sky shell's floor, the quest boxes 230 m under it)
            result[k] = ('void' if rest is None or zs - rest > VOID else 'holds' if zs - rest <= STEP else 'falls')
            if rest is None:
                kind[k] = 1
            else:
                rest_z[k], want[k], nz[k] = rest, rest - BOX_MIN[2], hit[0]
    # invisible tops: height over the highest visible surface under them in their cell
    vc, vz = sm['vis_cell'].astype(np.int64), sm['vis_z'].astype(np.float64)
    vc, vz = keyed(vc, vz)
    ic, iz = sm['inv_cell'].astype(np.int64), sm['inv_z'].astype(np.float64)
    (vk, q), _, _ = lexkeys((vc, vz), (ic, iz - 6.0))
    at = np.searchsorted(vk, q, 'left') - 1      # the highest visible surface 6 u or more under it
    has = (at >= 0) & (vc[np.maximum(at, 0)] == ic)     # in the same cell
    under = np.where(has, iz - vz[np.maximum(at, 0)], np.nan)
    band = np.array(['nothing_visible_under' if np.isnan(h) else next(nm for nm, lim in INVISIBLE_BANDS if h <= lim)
                     for h in under], object)
    return dict(u=u, result=result.astype(str), rest_z=rest_z, start=start, kind=kind, want=want, nz=nz, ic=ic, iz=iz,
                band=band.astype(str))


def drop_report(a, out, cat, lo, cell, R, t0):
    """drop.json (clusters by result, the engine's check), drop.png and drop_rows.npz from drop_surfaces' arrays on
    the compare's grid"""
    ny, nx, _ = cat.shape
    cell_m2 = (cell / U) ** 2
    u, result, ic, band = R['u'], R['result'], R['ic'], R['band']
    cell_of = u[:, 0].astype(np.int64)
    np.savez_compressed(out / 'drop_rows.npz', lo=lo, grid=np.array([nx, ny]), cell=cell, **R)
    # per cluster: area by result (a cell with surfaces of several results counts each)
    report = {'cell_m2': round(cell_m2, 4)}
    for k, name, _ in COLLIDE_KINDS:
        cells_res = {}
        src = zip(cell_of, result) if k == 2 else zip(ic, band)
        for c, r in src:
            cells_res.setdefault(int(c), set()).add(str(r))
        names = FALL_RESULTS if k == 2 else [n for n, _ in INVISIBLE_BANDS] + ['nothing_visible_under']
        totals, rows = dict.fromkeys(names, 0.0), []
        for area, cx, cy, cells in collide_clusters(cat, lo, cell, k, a.min_m2):
            by = dict.fromkeys(names, 0.0)
            for j, i in cells:
                for r in cells_res.get(int(j * nx + i), ()):
                    by[r] += cell_m2
            for r in names:
                totals[r] += by[r]
            rows.append({'area_m2': round(area, 1), 'x': round(cx), 'y': round(cy),
                         'm2': {r: round(v, 1) for r, v in by.items() if v},
                         'setviewpos': 'setviewpos %.0f %.0f 2000 0' % (cx, cy)})
        key = ('falls', 'void') if k == 2 else ('bump', 'lump')
        rows.sort(key=lambda r: -sum(r['m2'].get(n, 0) for n in key))
        report[name] = {'m2': {r: round(v, 1) for r, v in totals.items()}, 'clusters': rows}
        print('%s, m2 by result: %s' % (name, ', '.join('%s %.0f' % kv for kv in totals.items())))
    if a.engine:
        # (sample index, start origin, offline rest origin z / None / 'stuck', the landing side's normal z)
        starts = [(k, tuple(R['start'][k]), 'stuck' if R['kind'][k] == 2 else None if R['kind'][k] == 1 else
                   float(R['want'][k]), float(R['nz'][k])) for k in range(len(u))]
        report["engine"] = drop_engine(a, out, starts)
    report['seconds'] = round(time.time() - t0, 1)
    json.dump(report, open(out / 'drop.json', 'w'), indent=1, default=lambda o: o.item())   # numpy scalars
    # top-down, north up: falls red, voids magenta, holds dull red, no room yellow (covered), orange (low, blocked);
    # invisible tops: bumps light blue, lumps cyan, walls and roofs blue, with nothing visible under them dark blue
    img = np.zeros((ny, nx, 3), np.uint8)
    img[cat[..., 0] > 0] = (60, 60, 60)
    colours = {'falls': (255, 40, 40), 'void': (255, 0, 255), 'holds': (150, 110, 110), 'covered': (255, 220, 0),
               'low': (255, 150, 0), 'blocked': (255, 150, 0), 'bump': (150, 200, 255), 'lump': (0, 230, 255),
               'wall_or_roof': (60, 110, 255), 'nothing_visible_under': (30, 40, 140)}
    for c, r in list(zip(ic, band)) + list(zip(cell_of, result)):      # fall-through drawn last, over the tops
        img[int(c) // nx, int(c) % nx] = colours[str(r)]
    Image.fromarray(img[::-1]).save(out / 'drop.png')
    print('drop: %d surfaces dropped, %d invisible tops banded in %.1f s -> %s'
          % (len(u), len(ic), time.time() - t0, out / 'drop.json'))


def cmd_drop(a):
    """Every flagged surface of the collision compare, per 16 u cell (not one point a cluster): a fall-through
    surface gets a player dropped on it (falls, void, holds within a step) or, when no start within 14 u is free, the
    reason from the solids over it (covered, low, blocked aside); an invisible top gets its height over the visible
    floor under it. --engine N runs N of the drops' starts (seed 0) through the engine's own trace (the game module's
    levelcheck_trace) with tolerances fixed beforehand: a landing within 0.25 u, nothing hit, a start in solid.
    --tiles/--tile: one tile's compare folder (compare --tile), only the brushes its columns reach, drop_rows.npz
    alone (drop-tiles puts the tiles together)."""
    t0 = time.time()
    cdir, out = Path(a.collide), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    d, sm, so = np.load(cdir / 'cats.npz'), np.load(cdir / 'samples.npz'), np.load(cdir / 'solid.npz')
    cat, lo, cell = d['cat'], d['lo'], float(d['cell'])
    ny, nx, _ = cat.shape
    r = None
    if getattr(a, 'tile', None):
        reach, _ = tile_owner(json.load(open(a.tiles)), a.tile)
        r = (reach[0] - DROP_REACH, reach[1] - DROP_REACH, reach[2] + DROP_REACH, reach[3] + DROP_REACH)
    b = load_bsp(a.map, solids=True, window=r)
    sb = solid_brushes(b, sky=True)
    if r is not None:
        mn, mx = sb[0], sb[1]
        k = (mx[:, 0] >= r[0]) & (mn[:, 0] <= r[2]) & (mx[:, 1] >= r[1]) & (mn[:, 1] <= r[3])
        sb = tuple(v[k] for v in sb)
        R = drop_surfaces(b, sb, sm, so, lo, nx, cell)
        np.savez_compressed(out / 'drop_rows.npz', lo=lo, grid=np.array([nx, ny]), cell=cell, **R)
        print('drop: tile %s, %d brushes, %d surfaces dropped, %d invisible tops banded in %.1f s'
              % (a.tile, len(sb[0]), len(R['u']), len(R['ic']), time.time() - t0))
        return
    drop_report(a, out, cat, lo, cell, drop_surfaces(b, sb, sm, so, lo, nx, cell), t0)


def cmd_drop_tiles(a):
    """drop per tile (--workers processes into --free-gb, a tile kept under the same --key) on compare-tiles'
    folders, the tiles' rows put on the stitched grid in a whole drop's order, then the clusters, the engine's check
    and the outputs as drop's: the map is never loaded whole here."""
    import subprocess
    import threading
    from concurrent.futures import ThreadPoolExecutor
    try:
        import psutil
    except ImportError:
        psutil = None
        print('drop-tiles: no psutil: the tiles start with no memory test', flush=True)
    t0 = time.time()
    cdir, out = Path(a.collide), Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ids = sorted(t['id'] for t in json.load(open(a.tiles))['tiles'] if not a.only or t['id'] in a.only.split(','))
    gate = threading.Lock()

    def one(tid):
        d = cdir / 'tiles' / tid
        keyf = d / 'drop.key'
        if a.key and keyf.exists() and keyf.read_text() == a.key and (d / 'drop_rows.npz').exists():
            return 0
        keyf.unlink(missing_ok=True)
        with gate:
            while psutil and psutil.virtual_memory().available < a.free_gb * 2 ** 30:
                time.sleep(15)
            with open(d / 'drop.log', 'w') as f:
                pr = subprocess.Popen([sys.executable, '-I', str(Path(__file__).resolve()), 'drop', a.map, str(d), str(d),
                                       '--tiles', a.tiles, '--tile=' + tid], stdout=f, stderr=subprocess.STDOUT)
            time.sleep(20)
        rc = pr.wait()
        if rc == 0 and a.key:
            keyf.write_text(a.key)
        print('drop-tiles: %s %s' % (tid, 'ok' if rc == 0 else 'FAILED (%d): %s' % (rc, d / 'drop.log')), flush=True)
        return rc
    with ThreadPoolExecutor(max(1, a.workers)) as ex:
        failed = sum(1 for rc in ex.map(one, ids) if rc)
    if failed:
        sys.exit('drop-tiles: %d tiles failed' % failed)
    with np.load(cdir / 'cats.npz') as z:
        cat, lo, cell = z['cat'], np.array(z['lo']), float(z['cell'])
    nx = cat.shape[1]
    parts = {k: [] for k in ('u', 'result', 'rest_z', 'start', 'kind', 'want', 'nz', 'ic', 'iz', 'band')}
    for tid in ids:
        with np.load(cdir / 'tiles' / tid / 'drop_rows.npz') as z:
            tnx, oi, oj = int(z['grid'][0]), *np.round((np.array(z['lo']) - lo) / cell).astype(np.int64)

            def remap(c):
                j, i = np.divmod(np.asarray(c, np.int64), tnx)
                return (j + oj) * nx + i + oi
            for k in parts:
                v = z[k]
                if k == 'u':
                    v = np.stack([remap(v[:, 0]), v[:, 1]], 1)
                elif k == 'ic':
                    v = remap(v)
                parts[k].append(v)
    R = {k: np.concatenate(v) for k, v in parts.items()}
    o = np.lexsort((R['u'][:, 1], R['u'][:, 0]))        # a whole drop's order (np.unique: cell, then height)
    for k in ('u', 'result', 'rest_z', 'start', 'kind', 'want', 'nz'):
        R[k] = R[k][o]
    drop_report(a, out, cat, lo, cell, R, t0)


def drop_engine(a, out, starts):
    """N of the drops' starts (seed 0) traced by the engine (game module command levelcheck_trace, the trap->Trace
    pmove calls, MASK_PLAYERSOLID) straight down to -60,000 u. Tolerances, fixed before any run: the engine's end
    within 0.25 u of the offline rest (it stops 0.125 u short of a surface), no hit for a void, startsolid for a
    stuck start. Every disagreement is listed as it is."""
    import renderer_bench as rb
    rng = np.random.default_rng(0)
    pick = [starts[i] for i in sorted(rng.choice(len(starts), min(a.engine, len(starts)), replace=False))]
    run = out / 'run_engine'    # not aerial's run/: its shots are the holes evidence
    if run.exists():
        shutil.rmtree(run)
    base = run / 'home' / 'base'
    base.mkdir(parents=True)
    # the map only: the traces never read the scene, and with the 1.6 GB scene pack beside it the client stalled
    # loading images on the slice, 0 of 5,000 traces back. A hub and its chunk
    # packs (comma-separated) go in together, every chunk placed by hand with residency off
    # (a bare cm_chunkManifest runs the command, so "set")
    packs = [Path(p) for p in str(a.map).split(',')]
    for p in packs:
        try:
            os.link(p, base / p.name)
        except OSError:
            shutil.copy2(p, base / p.name)
    chunked = len(packs) > 1
    (base / 'levelcheck_in.txt').write_text(''.join('%.3f %.3f %.3f -60000\n' % st for _, st, _, _ in pick),
                                            encoding='ascii')
    sets = {'r_mode': '-1', 'r_customwidth': '640', 'r_customheight': '360', 'r_fullscreen': '0', 'r_noborder': '1',
            's_volume': '0', 's_musicvolume': '0', 'cl_motd': '0', 'net_enabled': '0', 'cl_autoRecordDemo': '0',
            'in_nograb': '1', 'in_mouse': '0', 'cl_renderer': 'rd-vulkan', 'bot_enable': '0', 'sv_pure': '0',
            'g_gametype': '0', 'com_maxfps': '60'}
    (base / 'autoexec.cfg').write_text(''.join('seta %s "%s"\n' % kv for kv in sets.items()), encoding='utf-8')
    pre = 'set cm_chunkManifest 0\n' if chunked else ''
    place = 'exec %s_chunks\nwait 600\n' % a.mapname if chunked else ''
    (base / 'bench.cfg').write_text(pre + 'devmap %s\nwait 600\n' % a.mapname + place +
                                    'levelcheck_trace levelcheck_in.txt levelcheck_out.txt\n'
                                    'wait 30\necho LEVELCHECK done\nwait 30\nquit\n', encoding='utf-8')
    args, cwd = client(base, 64, run / 'home')
    log = base / 'qconsole.log'
    fg = rb.foreground_window()
    t0 = time.time()
    proc = client_popen(args + ['+exec', 'bench.cfg'], cwd, run / 'home')
    while time.time() - t0 < a.timeout and proc.poll() is None:
        rb.hide_client_windows(proc.pid, fg)
        time.sleep(1)
        if log.exists() and 'LEVELCHECK done' in log.read_text(encoding='utf-8', errors='replace'):
            time.sleep(2)
            break
    rb.stop_client(proc)
    got = (base / 'levelcheck_out.txt').read_text(encoding='ascii').split('\n') if (base / 'levelcheck_out.txt').exists() else []
    rows = [list(map(float, ln.split())) for ln in got if ln.strip()]
    tally, bad = {}, []
    for (k, st, want, nz), r in zip(pick, rows):
        endz, frac, startsolid = r[4], r[5], bool(r[6])
        if want == 'stuck':
            kind, ok = 'stuck', startsolid
        elif want is None:
            kind, ok = 'void', not startsolid and frac >= 1.0
        else:                   # the engine stops 0.125 u off the plane it hits (cm_trace.cpp): 0.125 / nz straight down
            want = want + 0.125 / nz
            kind, ok = 'lands', not startsolid and frac < 1.0 and abs(endz - want) <= 0.25
        t = tally.setdefault(kind, [0, 0])
        t[0] += 1
        t[1] += ok
        if not ok:
            bad.append({'start': [round(v, 3) for v in st], 'offline': want, 'engine_end_z': endz, 'fraction': frac,
                        'startsolid': startsolid})
    agree = sum(t[1] for t in tally.values())
    print('engine: %d of %d traces back, %d agree (%s) in %.0f s' % (len(rows), len(pick), agree, ', '.join(
        '%s %d/%d' % (k, t[1], t[0]) for k, t in sorted(tally.items())), time.time() - t0))
    for r in bad[:10]:
        print('  disagrees:', r)
    if len(rows) < len(pick):     # traces that never came back are unchecked, not agreeing (as aerial's shots)
        sys.exit('engine: %d of %d traces never came back' % (len(pick) - len(rows), len(pick)))
    return {'traced': len(rows), 'asked': len(pick), 'agree': agree,
            'by_kind': {k: {'agree': t[1], 'of': t[0]} for k, t in tally.items()}, 'disagreements': bad}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('spawns')
    p.add_argument('map')
    p.add_argument('--out')
    p = sub.add_parser('aerial')
    p.add_argument('--bits', type=int, default=64, choices=(32, 64), help='the client build (64: levelcheck-bin64)')
    p.add_argument('map')
    p.add_argument('scene')
    p.add_argument('out')
    p.add_argument('--map', dest='mapname', required=True, help='the map name for devmap')
    p.add_argument('--radius', type=float, required=True, help='the level radius in game units (cp2gltf --radius x 37.5)')
    p.add_argument('--alt', type=float, help='camera height (game units; default ground + 40000: with --fov 15 near '
                                              'orthographic, the map copy\'s far plane set to reach under the ground)')
    p.add_argument('--fov', type=float, default=15.0, help='horizontal field of view (degrees)')
    p.add_argument('--ground-z', type=float, help='ground plane for the projection (default: the spawns\' floor)')
    p.add_argument('--size', default='3840x2160')
    p.add_argument('--timeout', type=int, default=900)
    p.add_argument('--edge', type=float, default=1500.0, help='band inside the radius left out (game units)')
    p.add_argument('--debug', type=int, default=8, help='r_sceneDebug for the shots: 8 (default) the material '
                   'index, a hole where nothing is drawn; 1 flat albedo, a hole by colour (surfaces '
                   'of a magenta-like albedo then read as holes)')
    p = sub.add_parser('holes-kind')
    p.add_argument('flat', help='an aerial folder (--debug 1)')
    p.add_argument('index', help='the same aerial with --debug 8')
    p.add_argument('materials', help='the scene build\'s materials.json')
    p.add_argument('out')
    p.add_argument('--edge', type=float, default=1500.0)
    p = sub.add_parser('holes')
    p.add_argument('out')
    p.add_argument('--radius', type=float, required=True)
    p.add_argument('--edge', type=float, default=1500.0, help='band inside the radius left out (game units)')
    p = sub.add_parser('collide')
    p.add_argument('cats')
    p.add_argument('out')
    p.add_argument('--min-m2', type=float, default=1.0)
    p = sub.add_parser('compare')
    p.add_argument('map')
    p.add_argument('scene')
    p.add_argument('out')
    p.add_argument('--tiles', help='an area\'s tiles.json (cp2gltf.py tiles): with --tile, only that tile\'s cells')
    p.add_argument('--tile', help='the tile id (tiles.json)')
    p.add_argument('--zmax', type=float, default=None,
                   help='the highest surface judged (default %g: the collision height of cp2gltf; a level built above '
                        'its origin, such as the Beauclair castle of w3build at 2,000-10,000 u, passes a larger one)'
                        % ZMAX)
    p = sub.add_parser('compare-tiles')
    p.add_argument('map')
    p.add_argument('scene')
    p.add_argument('out')
    p.add_argument('--tiles', required=True, help='the area\'s tiles.json')
    p.add_argument('--workers', type=int, default=2)
    p.add_argument('--free-gb', type=float, default=3.0,
                   help='start a tile only with this much memory free (the Watson slice\'s densest tile peaks at 1.7 GB)')
    p.add_argument('--zmax', type=float, default=None, help='compare --zmax for every tile (default %g)' % ZMAX)
    p.add_argument('--key', help='the inputs\' key: a tile finished under it is kept')
    p.add_argument('--only', help='these tiles only (ids, comma-separated); ownership still from every tile of --tiles')
    p = sub.add_parser('stitch')
    p.add_argument('out')
    p.add_argument('tiles', nargs='+', help='the tiles\' compare folders (compare --tiles --tile)')
    p = sub.add_parser('drop')
    p.add_argument('--bits', type=int, default=64, choices=(32, 64),
                   help='(unused: the engine check runs the 64-bit client, levelcheck-bin64)')
    p.add_argument('map')
    p.add_argument('collide', help='the collision compare\'s folder (cats.npz, samples.npz)')
    p.add_argument('out')
    p.add_argument('--min-m2', type=float, default=1.0)
    p.add_argument('--engine', type=int, default=0, help='trace this many drops in the engine too (seed 0)')
    p.add_argument('--scene', help='(unused: the engine traces load the map only)')
    p.add_argument('--map', dest='mapname', help='--engine: the map name for devmap')
    p.add_argument('--timeout', type=int, default=1800)
    p.add_argument('--tiles', help='with --tile: one tile\'s compare folder (compare --tile), its rows only')
    p.add_argument('--tile', help='the tile id (tiles.json)')
    p = sub.add_parser('drop-tiles')
    p.add_argument('--bits', type=int, default=64, choices=(32, 64),
                   help='(unused: the engine check runs the 64-bit client, levelcheck-bin64)')
    p.add_argument('map')
    p.add_argument('collide', help='compare-tiles\' folder (stitched, tiles/<id>/)')
    p.add_argument('out')
    p.add_argument('--tiles', required=True, help='the area\'s tiles.json')
    p.add_argument('--workers', type=int, default=2)
    p.add_argument('--free-gb', type=float, default=2.0,
                   help='start a tile only with this much memory free (a dense slice tile peaks at 0.44 GB)')
    p.add_argument('--key', help='the inputs\' key: a tile finished under it is kept')
    p.add_argument('--only', help='these tiles only (ids, comma-separated); ownership still from every tile of --tiles')
    p.add_argument('--min-m2', type=float, default=1.0)
    p.add_argument('--engine', type=int, default=0, help='trace this many drops in the engine too (seed 0)')
    p.add_argument('--scene', help='(unused: the engine traces load the map only)')
    p.add_argument('--map', dest='mapname', help='--engine: the map name for devmap')
    p.add_argument('--timeout', type=int, default=1800)
    a = ap.parse_args()
    {'spawns': cmd_spawns, 'aerial': cmd_aerial, 'holes': cmd_holes, 'holes-kind': cmd_holes_kind, 'compare': cmd_compare,
     'compare-tiles': cmd_compare_tiles, 'stitch': cmd_stitch,
     'collide': cmd_collide, 'drop': cmd_drop, 'drop-tiles': cmd_drop_tiles}[a.cmd](a)


if __name__ == '__main__':
    main()
