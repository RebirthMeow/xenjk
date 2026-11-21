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
"""cp77_seamask.py -- the ocean plane's sea mask (the scene stage's --ocean-mask, .xwat v3) for a tiled CP2077 area.

  python -I tools/levelport/cp77_seamask.py <out.npz> --tiles-tag <tag> --map <reference map>
         [--area night_city_b] [--floor 12] [--grid <grid.npz>] [--preview <mask.jpg>] [--save-map <city.jpg>]

Reads, under cp2gltf.work_dir(area):
  tiles/<tag>/build/*.npz   the tiles' collision parts ('col': triangles in the area's glTF frame, metres about the
                            area's centre and --floor), made by the parts stage (cp77_tiles.py)
  placements.json           the scan's water patches ('water')
and --map, the Realistic Map 32K reference (a top-down render of the whole city, a CP2077 mod; not CP2077's own data):
either the folder of its 16 uncooked tiles (finalimage-<col>_<row>.png, 8,192 px each, from the mod's
base/fx/textures/3dmap/static/image/32k) or the 8,192 px stitch of them this tool writes with --save-map. From the
tiles the stitch is made as it always was (each tile to 2,048 px, Lanczos) and read back through a JPEG at quality 82,
so a mask from the tiles and one from a saved stitch are the same.

Writes <out.npz>: mask (bool, [i along x, j along y], True = sea), x0, y0 (its min corner, CP metres), cell (20 m): the
form cp2gltf.ocean_mask_block reads. --grid writes the 10 m working grids (hf, seen, mapw, seed, cand, x0, y0);
--preview a picture of the mask (north up, 3 px a cell). An existing output is never written over.

The mask, by connected water: the sea spreads from CP2077's own water patches (at sea level) and from the open sea
(water on the reference map outside the collision) through every 10 m cell of the area's water box (AREAS 'half')
whose highest walkable surface up to QUAY m over sea level lies under it (a bed); a cell with a surface between BED
and QUAY m (a quay, a bank, the street) stops it. Surfaces higher than QUAY are ignored, so the water passes under
decks, piers and the roads over the channels, and a sunken plaza or ramp behind a quay never connects to the sea. A
20 m cell of the mask is sea when any of its 10 m cells is wet; separate pieces under SPECK cells of 20 m are dropped.
Converted levels are for local play only."""
import argparse
import io
import json
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import binary_dilation, label

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402

CELL = 10.0         # the working grid (m)
QUAY = 2.5          # m over sea level: a surface at or under it holds the sea back; above it, the water passes under
BED = -0.3          # a walkable surface under this is a bed the sea can cover
DEEP = -35.0        # a bed under this is the sea at any distance (basements and undercrofts stay above it)
NEAR_BED = 4        # a bed joins when within this many cells of known water (40 m)
NEAR_DECK = 16      # a cell with only higher surfaces (a deck, a road, a pier) within this many cells (160 m)
SPECK = 25          # a separate piece of the 20 m mask under this many cells (1 ha) is dropped
CHUNK = 200000      # triangles at a time (the result is the same for any size; it bounds memory)
# the reference map: its 4 x 4 tiles cover CP2077 x from REF_X0 and y down from REF_Y1, REF_SIZE m square, north up
REF_X0, REF_Y1, REF_SIZE = -6297.5, 4427.2, 12112.28
REF_TILES, REF_TILE_PX, REF_JPEG_QUALITY = 4, 2048, 82


def reference_map(src, save=None):
    """The reference map as an RGB uint8 array: a stitched image, or the stitch of a folder of tiles made the way the
    stored stitch was (each tile to REF_TILE_PX, Lanczos, then a JPEG at REF_JPEG_QUALITY)"""
    Image.MAX_IMAGE_PIXELS = None
    src = Path(src)
    if src.is_dir():
        out = Image.new('RGB', (REF_TILES * REF_TILE_PX, REF_TILES * REF_TILE_PX))
        for i in range(REF_TILES):
            for j in range(REF_TILES):
                f = src / ('finalimage-%d_%d.png' % (i, j))
                if not f.exists():
                    sys.exit('cp77_seamask: no reference tile %s' % f)
                with Image.open(f) as im:
                    out.paste(im.convert('RGB').resize((REF_TILE_PX, REF_TILE_PX), Image.Resampling.LANCZOS),
                              (i * REF_TILE_PX, j * REF_TILE_PX))
        buf = io.BytesIO()
        out.save(buf, format='JPEG', quality=REF_JPEG_QUALITY)
        del out
        if save:
            Path(save).parent.mkdir(parents=True, exist_ok=True)
            Path(save).write_bytes(buf.getvalue())
        buf.seek(0)
        with Image.open(buf) as im:
            return np.asarray(im.convert('RGB'))
    if not src.exists():
        sys.exit('cp77_seamask: no reference map %s' % src)
    with Image.open(src) as im:
        return np.asarray(im.convert('RGB'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', type=Path, help='the mask (.npz)')
    ap.add_argument('--tiles-tag', required=True, help="the tiling whose collision parts are read (work/tiles/<tag>)")
    ap.add_argument('--map', required=True, type=Path,
                    help='the Realistic Map 32K reference: its uncooked tiles folder, or a stitch saved by --save-map')
    ap.add_argument('--area', default='night_city_b', choices=sorted(cp2gltf.AREAS))
    ap.add_argument('--floor', type=float, default=12.0, help="the parts' floor: CP height of glTF 0 (m)")
    ap.add_argument('--grid', type=Path, help="also write the 10 m working grids (.npz)")
    ap.add_argument('--preview', type=Path, help='also write a picture of the mask (.jpg)')
    ap.add_argument('--save-map', type=Path, help='with --map a tiles folder: also write the stitch (.jpg)')
    a = ap.parse_args()
    for p in (a.out, a.grid, a.preview, a.save_map):
        if p is not None and p.exists():
            sys.exit('cp77_seamask: %s exists' % p)
    t0 = time.time()
    work = cp2gltf.work_dir(a.area)
    ar = cp2gltf.AREAS[a.area]
    cx, cy = ar['centre']
    half = ar.get('half', ar['radius'])
    hx, hy = half if isinstance(half, (tuple, list)) else (half, half)
    ring = (cx - hx, cy - hy, cx + hx, cy + hy)
    nx, ny = int((ring[2] - ring[0]) // CELL), int((ring[3] - ring[1]) // CELL)
    parts = sorted((cp2gltf.tile_dir(work, a.tiles_tag) / 'build').glob('*.npz'))
    if not parts:
        sys.exit('cp77_seamask: no collision parts in %s' % (cp2gltf.tile_dir(work, a.tiles_tag) / 'build'))
    # the reference map's water at 10 m (outside the collision: the open sea); only the sampled pixels are tested
    im = reference_map(a.map, a.save_map)
    s = REF_SIZE / im.shape[1]
    xs = ring[0] + (np.arange(nx) + 0.5) * CELL
    ys = ring[1] + (np.arange(ny) + 0.5) * CELL
    cols = ((xs - REF_X0) / s).astype(int)
    rows = ((REF_Y1 - ys) / s).astype(int)
    okc = (cols >= 0) & (cols < im.shape[1])
    okr = (rows >= 0) & (rows < im.shape[0])
    px = im[np.ix_(rows[okr], cols[okc])].astype(np.int32)
    del im
    r, g, b = px[..., 0], px[..., 1], px[..., 2]
    mapw = np.zeros((nx, ny), bool)
    mapw[np.ix_(okc, okr)] = ((b >= r + 6) & (g >= r - 4) & (r + g + b < 360)).T
    hf = np.full((nx, ny), -np.inf)          # the highest walkable surface up to QUAY (CP z)
    seen = np.zeros((nx, ny), bool)          # any collision in the cell (inside the converted area)

    def put(px, py, pz, surf):
        i = ((px - ring[0]) // CELL).astype(int)
        j = ((py - ring[1]) // CELL).astype(int)
        k = (i >= 0) & (i < nx) & (j >= 0) & (j < ny)
        seen[i[k], j[k]] = True
        if surf:
            np.maximum.at(hf, (i[k], j[k]), pz[k])

    tris = 0
    for f in parts:
        with np.load(f, allow_pickle=True) as z:
            col = z['col']
        tris += len(col)
        for c0 in range(0, len(col), CHUNK):
            T = col[c0:c0 + CHUNK]
            # glTF (x, y up, z = -CP y) about the area's centre and floor -> CP metres
            X, Y, Z = T[:, :, 0] + cx, cy - T[:, :, 2], T[:, :, 1] + a.floor
            n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
            up = np.abs(n[:, 1]) > 0.9 * np.linalg.norm(n, axis=1)
            low = up & (Z.max(1) <= QUAY)
            # sample points: vertices, centroid, edge midpoints, and every ~5 m over a triangle wider than a cell
            for sel, surf in ((low, True), (~low, False)):
                if not sel.any():
                    continue
                P = np.stack([X[sel], Y[sel], Z[sel]], 2)
                S = np.concatenate([P, P.mean(1, keepdims=True), (P + np.roll(P, 1, axis=1)) / 2], 1)
                put(S[..., 0].ravel(), S[..., 1].ravel(), S[..., 2].ravel(), surf)
                edge = np.max(np.linalg.norm(P[:, :, :2] - np.roll(P[:, :, :2], 1, axis=1), axis=2), 1)
                for t in P[edge > CELL]:
                    m = int(np.ceil(np.linalg.norm(t[:, :2] - np.roll(t[:, :2], 1, axis=0), axis=1).max() / 5.0))
                    u, v = np.meshgrid(np.arange(m + 1), np.arange(m + 1))
                    w = u + v <= m
                    ua, vb = u[w] / m, v[w] / m
                    q = t[0] + ua[:, None] * (t[1] - t[0]) + vb[:, None] * (t[2] - t[0])
                    put(q[:, 0], q[:, 1], q[:, 2], surf)
        del col
    # the water we know: CP2077's patches at sea level, and the open sea (map water outside the collision)
    seed = np.zeros((nx, ny), bool)
    for p in json.load(open(work / 'placements.json', encoding='utf-8')).get('water', []):
        if abs(p['m'][11]) < 0.5:
            i, j = int((p['m'][3] - ring[0]) // CELL), int((p['m'][7] - ring[1]) // CELL)
            if 0 <= i < nx and 0 <= j < ny:
                seed[i, j] = True
    seed |= mapw & ~seen
    # candidates. Inside the collision the map's colour test also takes grey-blue roofs for water, so there only
    # CP2077's own patches count (seed). A bed only near known water: the city's undercrofts, basements and tunnels
    # are beds under sea level too, and chained together they reach the whole city. A deck (nothing up to QUAY: the
    # roads and piers over the channels, open water beneath) further out: a pier strip can run 100 m from a patch.
    deck = seen & ~np.isfinite(hf)
    bed = np.isfinite(hf) & (hf < BED)
    cand = (seed | (bed & binary_dilation(seed, iterations=NEAR_BED))
            | (deck & binary_dilation(seed, iterations=NEAR_DECK)))
    # a deep bed is the sea at any distance (the seafloor west of the bay, about -50 m with nothing over it)
    cand |= np.isfinite(hf) & (hf < DEEP)
    # outside the collision: the open sea (foam fails the colour test, so the map's water alone ended short of the
    # horizon), and what was never converted (the view shows nothing there either way): the sea to the box's edge
    # makes a horizon instead of a void
    cand |= ~seen
    if a.grid:
        a.grid.parent.mkdir(parents=True, exist_ok=True)
        np.savez(a.grid, hf=hf, seen=seen, mapw=mapw, seed=seed, cand=cand, x0=ring[0], y0=ring[1])
    lab, _ = label(cand)
    keep = np.unique(lab[seed & (lab > 0)])
    wet = np.isin(lab, keep) & (lab > 0)
    # the 20 m mask: any wet 10 m cell
    m20 = wet[:nx // 2 * 2, :ny // 2 * 2].reshape(nx // 2, 2, ny // 2, 2).any(axis=(1, 3))
    # specks: the colour test takes dark containers and roofs outside the collision for water
    lab20, _ = label(m20)
    sizes = np.bincount(lab20.ravel())
    small = np.isin(lab20, np.nonzero(sizes < SPECK)[0]) & (lab20 > 0)
    print('cp77_seamask: specks dropped: %d cells in %d pieces' % (int(small.sum()), len(set(lab20[small].tolist()))))
    m20 &= ~small
    a.out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(a.out, mask=m20, x0=ring[0], y0=ring[1], cell=2 * CELL)
    print('cp77_seamask: %d parts, %d triangles; 10 m cells: beds %d, deck/open %d, candidates %d, wet (connected to '
          'the sea) %d; mask %d cells of 20 m (%.1f km2); %.0f s'
          % (len(parts), tris, int(bed.sum()), int((~np.isfinite(hf) & (seen | mapw)).sum()), int(cand.sum()),
             int(wet.sum()), int(m20.sum()), m20.sum() * 400 / 1e6, time.time() - t0))
    if a.preview:
        ov = np.zeros((ny // 2, nx // 2, 3), np.uint8)
        ov[...] = (60, 50, 40)
        ov[np.flipud(m20.T)] = (30, 110, 200)
        a.preview.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(ov).resize((nx // 2 * 3, ny // 2 * 3), Image.NEAREST).save(a.preview, quality=85)


if __name__ == '__main__':
    main()
