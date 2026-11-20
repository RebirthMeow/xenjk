"""tag_probesky.py -- the sky CP2077's reflection probe captures see, into a kit's scene instances, as a patch pack
(a pagoda room seen through a paneless round window was lit
like the street).

    python -I tag_probesky.py <kit> <probe_survey.json> <probe_skyshare.json> --out <pack.pk3> [--area night_city]
                              [--map cp_watson] [--modes eALLOW_AMBIENT_CONTRIBUTION,eOVERRIDE_GI_AMBIENT]

CP2077 lights a surface's ambient from the reflection probes around it. Each probe (worldReflectionProbeNode: a box,
its node's position, orientation and scale, the unit cube's half extents, a priority) holds a G-buffer capture
(.envprobe, CReflectionProbeDataResource) that the engine relights; the sky's light comes from the capture's sky texels,
so an enclosed room's capture (none) gives none. probe_skyshare.py measured each capture's sky texels (vis: against
open ground's three faces' worth; the node's hideSkyColor blacks them out: vis 0). haveSkyData does not decide it
(absent it reads true, and rooms whose captures hold no sky carry it). Our renderer gave every surface one sky and
ground hemisphere.

The renderer's contract (beside tag_interiors.py's bits 24-26): slot bits 28-31, the sky the instance's probe lacks,
round(15 x (1 - vis)); scene.frag takes r_sceneProbeSky (0 off) of that share from its hemisphere. The instance's
probe: of the probes whose box holds its mesh's bounding-sphere centre and whose ambientMode lets the capture give the
ambient (--modes; an eNO_AMBIENT_CONTRIBUTION probe's surfaces take CP2077's GI, which we do not have), the highest
priority (at equal priority the smaller box); none: 0, as before. Global probes are skipped. Each t_<tile>.xscn the
kit serves (the last pack in load order holding it) whose slots change is written, stored, to --out, whose name must
sort after the kit's other scene packs. Reads the kit; writes only --out. Converted levels are for local play only."""
import argparse
import collections
import glob
import json
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cp2gltf  # noqa: E402

INST = np.dtype([('W', '<f4', 12), ('mi', '<u4'), ('near', '<f4'), ('slot', '<u4'), ('hide', '<f4')])
MESH = struct.Struct('<4f2I2I')     # xscnMesh_t: centre, radius, first LOD, LODs, pad
SHIFT, MASK = 28, np.uint32(0xf0000000)
FLOOR = 12.0                        # (tag_interiors.py: the hub frame's floor, CP2077 metres)


def quat_rows(q):
    i, j, k, r = q
    n = (i * i + j * j + k * k + r * r) ** 0.5 or 1.0
    i, j, k, r = i / n, j / n, k / n, r / n
    return np.array([[1 - 2 * (j * j + k * k), 2 * (i * j - k * r), 2 * (i * k + j * r)],
                     [2 * (i * j + k * r), 1 - 2 * (i * i + k * k), 2 * (j * k - i * r)],
                     [2 * (i * k - j * r), 2 * (j * k + i * r), 1 - 2 * (i * i + j * j)]])


def probes(survey, share, cx, cy, upm, modes):
    """[(priority, volume, lost (0..15), R (scene -> probe axes), centre (scene units), half extents (units))]: the
    probes whose capture gives the ambient"""
    out, st = [], collections.Counter()
    for r in survey:
        if r['t'] != 'worldReflectionProbeNode':
            continue
        if r.get('globalProbe') in (1, '1', True):
            st['global (skipped)'] += 1
            continue
        mode = str(r.get('ambientMode'))
        if mode not in modes:
            st['%s (skipped)' % mode] += 1
            continue
        s = share.get((r.get('data') or '').replace('/', chr(92)).lower())
        if s is None:
            st['no capture'] += 1
            continue
        vis = 0.0 if r.get('hideSkyColor') in (1, '1', True) else s['vis']
        lost = int(round(15 * (1.0 - vis)))
        h = np.abs(np.array(r['scale'], float)) * upm
        c = np.array([(r['pos'][0] - cx) * upm, (r['pos'][1] - cy) * upm, (r['pos'][2] - FLOOR) * upm])
        out.append((float(r.get('priority') or 0), float(np.prod(h)), lost, quat_rows(r['quat']).T, c, h))
        st['%s, lost %s' % (mode, 'all' if lost == 15 else 'none' if lost == 0 else 'some')] += 1
    return out, st


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('kit', type=Path)
    ap.add_argument('survey', type=Path)
    ap.add_argument('share', type=Path)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--area', default='night_city')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--modes', default='eALLOW_AMBIENT_CONTRIBUTION,eOVERRIDE_GI_AMBIENT',
                    help='the ambientMode values whose capture gives the ambient (comma-separated)')
    a = ap.parse_args()
    if a.out.exists():
        sys.exit('tag_probesky: %s exists' % a.out)
    cx, cy = cp2gltf.AREAS[a.area]['centre']
    upm = cp2gltf.gltf_upm()
    pr, st = probes(json.load(open(a.survey, encoding='utf-8'))['records'],
                    json.load(open(a.share, encoding='utf-8'))['probes'], cx, cy, upm, set(a.modes.split(',')))
    print('tag_probesky: probes %s' % json.dumps(dict(st)), flush=True)
    CELL = 64.0 * upm               # a coarse grid over the probes' bounding boxes for the lookups
    grid = collections.defaultdict(list)
    for n, (_, _, _, R, c, h) in enumerate(pr):
        ext = np.abs(R.T) @ h       # the box's axis-aligned half extents
        lo, hi = np.floor((c - ext) / CELL).astype(int), np.floor((c + ext) / CELL).astype(int)
        for gx in range(lo[0], hi[0] + 1):
            for gy in range(lo[1], hi[1] + 1):
                grid[(gx, gy)].append(n)
    hb = a.kit / 'home' / 'base'
    packs = sorted(glob.glob(str(hb / ('zz_%s_scene*.pk3' % a.map))), key=lambda p: Path(p).name.upper())
    if packs and Path(packs[-1]).name.upper() > a.out.name.upper():
        sys.exit('tag_probesky: %s sorts before %s: it would not win' % (a.out.name, Path(packs[-1]).name))
    serve, zin = {}, {}
    for pk in packs:
        if '_img' in Path(pk).name:
            continue
        zin[pk] = zipfile.ZipFile(pk)                  # (each pack's directory read once)
        for n in zin[pk].namelist():
            serve[n.lower()] = (pk, n)
    man = zin[serve['maps/%s.xman' % a.map][0]].read('maps/%s.xman' % a.map).decode()
    centres = []
    for l in (l.split() for l in man.splitlines() if l.startswith('lib ')):
        pk, n = serve[l[1].lower()]
        b = zin[pk].read(n)
        _, _, counts, offs = cp2gltf.xscn_head_of(b)
        centres += [MESH.unpack_from(b, offs[2] + MESH.size * i)[:3] for i in range(counts[2])]
    centres = np.array(centres, np.float64)
    print('tag_probesky: %d meshes' % len(centres), flush=True)
    stats, hist = collections.Counter(), collections.Counter()
    part = a.out.with_name(a.out.name + '.partial')
    zout = zipfile.ZipFile(part, 'w', zipfile.ZIP_STORED, allowZip64=False)
    tiles = 0
    for key, (pk, n) in sorted(serve.items()):
        base = key.rsplit('/', 1)[-1]
        if not (base.startswith('t_') and base.endswith('.xscn')) or base == 't_water.xscn':
            continue
        blob = zin[pk].read(n)
        _, _, counts, offs = cp2gltf.xscn_head_of(blob)
        rec = np.frombuffer(blob, INST, counts[6], offs[6]).copy()
        if len(rec) and rec['mi'].max() >= len(centres):
            sys.exit('tag_probesky: %s names mesh %d of %d' % (n, rec['mi'].max(), len(centres)))
        if len(rec) and (rec['slot'] & MASK).any():
            sys.exit('tag_probesky: %s: slot bits 28-31 already used' % n)
        W = rec['W'].reshape(-1, 3, 4).astype(np.float64)
        c = np.einsum('nij,nj->ni', W[:, :, :3], centres[rec['mi']]) + W[:, :, 3]
        lost = np.zeros(len(rec), np.uint32)
        for k in range(len(rec)):
            best = None
            for pi in grid.get((int(np.floor(c[k, 0] / CELL)), int(np.floor(c[k, 1] / CELL))), ()):
                prio, vol, ls, R, pc, h = pr[pi]
                if np.all(np.abs(R @ (c[k] - pc)) <= h) and (best is None or (prio, -vol) > (best[0], -best[1])):
                    best = (prio, vol, ls)
            if best is None:
                stats['in no ambient-giving probe'] += 1
                continue
            lost[k] = best[2]
            hist[int(best[2])] += 1
            if best[2] and rec['slot'][k] & (1 << 24):
                stats['losing sky, interior-tagged'] += 1
        stats['instances'] += len(rec)
        stats['losing sky'] += int((lost > 0).sum())
        if not lost.any():
            continue
        rec['slot'] |= lost << np.uint32(SHIFT)
        tiles += 1
        blob = blob[:offs[6]] + rec.tobytes() + blob[offs[6] + rec.nbytes:]
        zout.writestr(zipfile.ZipInfo(n, (2026, 11, 9, 0, 0, 0)), blob)
    zout.close()
    for z in zin.values():
        z.close()
    part.replace(a.out)
    stats['tiles written'] = tiles
    print('tag_probesky: %s' % json.dumps(dict(stats)))
    print('tag_probesky: instances by lost sky (0..15, in an ambient-giving probe): %s' % json.dumps(dict(sorted(hist.items()))))
    print('tag_probesky: %s' % a.out)


if __name__ == '__main__':
    main()
