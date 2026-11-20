"""tag_interiors.py -- CP2077's interior streaming boxes into a built tiled scene.

  python -I tag_interiors.py <src scene build> <dst scene build> [--area night_city] [--tag 9]

CP2077 loads an interior sector only while the camera is inside its streaming box (all.streamingblock's descriptors,
category Interior: a 3D box in CP2077 metres; Arasaka Tower's upper floors start at z 128 m). We drew every interior
from everywhere: at street level by Arasaka Tower 90% of the interior placements within 1.5 km are outside their box,
98% at Misty's. The renderer's contract (v1):
  - an interior instance's slot: bit 24 set, bits 0-23 its group (an index into maps/<map>.xint); interiors carry no
    signal slot (asserted);
  - bit 25 on a loose prop's instance (NO_DECAL_TYPES, and foliage: the .cfoliage litter and plants): it
    receives no decals; bit 26 on foliage: it lies on another surface (drawn pulled toward the eye, depth only);
    beside a signal slot or a group;
  - maps/<map>.xint: b'XINT', version 1, count, pad (u32), then per group f32 min xyz, max xyz in game units (the hub
    frame: (cp - (centre x, centre y, floor)) * 37.5), u32 sector level, u32 pad; groups sorted by sector name.
Each tile's t_<tile>.xscn instances are matched to their placements by name and position (the part's placed_name
and placed_pos, in the same order); an interior placement with no descriptor stays untagged. The shards are written
stored (the renderer streams them by range), the rest copied as it is; the image packs are hard links. Every member
is taken once, from the last pack in the engine's load order that holds it (the overlays beside the shards: a
patch_chunks doors pack, the ads, foliage or holograms packs carry tiles of the same names), so a tile patched by
patch_chunks (its door leaves hidden in place, the counts unchanged) is tagged as patched; a tile whose instances no
longer match its part (an older patch that removed them) is refused. A new build, never over an existing one.
Converted levels are for local play only."""
import argparse
import json
import os
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402
import pk3write  # noqa: E402

INST = np.dtype([('W', '<f4', 12), ('mi', '<u4'), ('near', '<f4'), ('slot', '<u4'), ('hide', '<f4')])
INTERIOR = 1 << 24
NO_DECALS = 1 << 25             # (renderer contract: the instance receives no projected or mesh decals)
ON_SURFACE = 1 << 26            # (it lies on another surface; scene.vert pulls it toward the eye along its
                                # ray, depth only: CP2077's foliage litter is authored flush with the street)
# CP2077's world decals mark the static street and walls; the loose props on them (destructible and physics meshes,
# moving ones, entities' components: boxes, cans, devices) take none. Graffiti used to slide over the debris
# boxes as the eye moved (those were foliage: the .cfoliage litter takes none either)
NO_DECAL_TYPES = ('worldInstancedDestructibleMeshNode', 'worldPhysicalDestructionNode', 'worldDynamicMeshNode',
                  'worldRotatingMeshNode', 'worldClothMeshNode', 'entity')


def interior_boxes(work):
    """{sector name (ep1- for Phantom Liberty's own): (min xyz, max xyz, level)} from the streaming blocks, read
    as cp2gltf.sector_variants reads them: json_blocks/<BLOCK_DIRS> in order, a later block's descriptor over an
    earlier one's (ep1's copies of base sectors override base's), the name from the descriptor's DepotPath (ep1- only
    for a sector under default/ep1/, ep1q's). Before, ep1's block was read as if every sector in it were PL's own
    (prefixed ep1-, so its base-sector overrides were lost) and ep1q not at all."""
    out = {}
    root = work / 'json_blocks'
    for sub in cp2gltf.BLOCK_DIRS:
        for f in sorted((root / sub).rglob('*.json')) if (root / sub).is_dir() else ():
            for d in json.load(open(f, encoding='utf-8'))['Data']['RootChunk'].get('descriptors') or []:
                if d.get('category') != 'Interior':
                    continue
                dp = cp2gltf.depot(d.get('data') or {})
                if '.streamingsector' not in dp:
                    continue
                path = dp.split(chr(92))
                name = ('ep1-' if len(path) > 1 and path[-2] == 'ep1' else '') + path[-1].split('.streamingsector')[0]
                b = d['streamingBox']
                out[name] = ([b['Min'][k] for k in 'XYZ'], [b['Max'][k] for k in 'XYZ'], int(d.get('level', 0)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', type=Path)
    ap.add_argument('dst', type=Path)
    ap.add_argument('--area', default='night_city')
    ap.add_argument('--tag', default='9')
    ap.add_argument('--map', default='cp_watson')
    a = ap.parse_args()
    if a.dst.exists():
        sys.exit('tag_interiors: %s exists' % a.dst)
    work = cp2gltf.work_dir(a.area)
    tdir = cp2gltf.tile_dir(work, a.tag)
    cx, cy = cp2gltf.AREAS[a.area]['centre']
    floor, upm = 12.0, cp2gltf.gltf_upm()
    boxes = interior_boxes(work)
    names = sorted(boxes)
    gid = {n: i for i, n in enumerate(names)}
    xint = [struct.pack('<4sIII', b'XINT', 1, len(names), 0)]
    for n in names:
        lo, hi, lv = boxes[n]
        lo = [(lo[0] - cx) * upm, (lo[1] - cy) * upm, (lo[2] - floor) * upm]
        hi = [(hi[0] - cx) * upm, (hi[1] - cy) * upm, (hi[2] - floor) * upm]
        xint.append(struct.pack('<6f2I', *lo, *hi, lv, 0))

    # the engine's load order (uppercased names: '_' after letters and digits, as patch_chunks.scene_patch sorts)
    packs = sorted(a.src.glob('zz_%s_scene*.pk3' % a.map), key=lambda q: q.name.upper())
    shard_packs = [p for p in packs if '_img' not in p.name]
    serve = {}                      # member (no case) -> (zip, info) of the last pack holding it, in first-seen order
    for pk in shard_packs:
        zin = zipfile.ZipFile(pk)
        for info in zin.infolist():
            serve[info.filename.lower()] = (zin, info)
    part = a.dst.with_name(a.dst.name + '.partial')
    part.mkdir(parents=True)
    out = pk3write.ShardWriter(part, a.map)
    stats = {'tiles': 0, 'instances': 0, 'interior': 0, 'no_box': 0, 'no_decals': 0}
    small = [(info, zin.read(info)) for zin, info in serve.values() if not info.filename.endswith('.xscn')]
    for info, blob in small:
        out.first.writestr(zipfile.ZipInfo(info.filename, info.date_time), blob, compress_type=zipfile.ZIP_DEFLATED,
                           compresslevel=1)
    out.first.writestr(zipfile.ZipInfo('maps/%s.xint' % a.map), b''.join(xint))
    for zin, info in serve.values():
        if not info.filename.endswith('.xscn'):
            continue
        blob = zin.read(info)
        base = info.filename.rsplit('/', 1)[-1]
        if base.startswith('t_') and base != 't_water.xscn':
            tid = base[2:-5]
            _, _, counts, offs = cp2gltf.xscn_head_of(blob)
            rec = np.frombuffer(blob, INST, counts[6], offs[6]).copy()
            with np.load(tdir / 'scene' / tid / 'arrays.npz', allow_pickle=True) as ar:
                placed = np.round(ar['placed_pos'], 2).tolist()      # (once: an npz reads an array per access)
            pn = json.loads((tdir / 'scene' / tid / 'part.json').read_text(encoding='utf-8'))['placed_name']
            if counts[6] != len(placed) or len(pn) != counts[6]:
                sys.exit('tag_interiors: %s (%s): %d instances, the part %d: not this tag\'s part, or a patch that '
                         'removed instances' % (tid, Path(zin.filename).name, counts[6], len(placed)))
            sector, prop = {}, set()
            for r in cp2gltf.read_records(tdir / (tid + '.json')):
                key = (r.get('name'), round(r['m'][3], 2), round(r['m'][7], 2), round(r['m'][11], 2))
                if 'interior' in r.get('sector', ''):
                    sector[key] = r['sector']
                if r.get('type') in NO_DECAL_TYPES:
                    prop.add(key)
            slots = rec['slot']
            for k in range(counts[6]):
                pos = placed[k]
                # (foliage: CP2077's scattered litter and plants, from the .cfoliage brushes, placed_name 'foliage<node>_...';
                # no tile record: the report 2 debris was this)
                if (pn[k], pos[0], pos[1], pos[2]) in prop or str(pn[k]).startswith('foliage'):
                    slots[k] |= NO_DECALS
                    stats['no_decals'] += 1
                if str(pn[k]).startswith('foliage'):
                    slots[k] |= ON_SURFACE
                s = sector.get((pn[k], pos[0], pos[1], pos[2]), '')
                if 'interior' not in s:
                    continue
                g = gid.get(s)
                if g is None:
                    stats['no_box'] += 1
                    continue
                if slots[k] & ~(NO_DECALS | ON_SURFACE):
                    sys.exit('tag_interiors: %s: an interior instance with a signal slot (%d)' % (tid, slots[k]))
                slots[k] |= INTERIOR | g
                stats['interior'] += 1
            stats['tiles'] += 1
            stats['instances'] += counts[6]
            blob = blob[:offs[6]] + rec.tobytes() + blob[offs[6] + rec.nbytes:]
        out.stored(info, blob)
    out.close()
    pk3write.link_unchanged(a.src, packs, part)
    os.replace(part, a.dst)
    print('tag_interiors: %s: %d groups; %d tiles, %d instances, %d interior tagged, %d interior without a box, %d '
          'loose props (no decals)' % (a.dst.name, len(names), stats['tiles'], stats['instances'], stats['interior'],
                                       stats['no_box'], stats['no_decals']))


if __name__ == '__main__':
    main()
