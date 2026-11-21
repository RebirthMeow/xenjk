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
"""cp77_collision.py -- a CP2077 area's collision for a kit: the chunk packs and the playtest hub, from a tiles tag's
collision parts (build_watson.py's collision stage; docs/levelport-collision.md section 36). It replaces tag 10's
uncommitted chain (tag10b's chunks step, tag10c.sh, kit10.py chunks with patch_kit.py, hub9/hub10.sh with
hub_inject.py and spawn_corpus.py).

  python -I tools/levelport/cp77_collision.py <area> <tiles tag> --map cp_watson --out <build>/cp_watson_collision-<tag>
                                              --q3map2 <hub q3map2.exe> --q3map2-chunks <chunk q3map2.exe>
                                              [--workers 4] [--jobs 4] [--text [--tile-parallel 2]] [--idle] [--tiles a,b]
                                              [--dry-run]

Two q3map2 builds of tools/levelport/q3map2's patches: the hub's with world_coords (xenjk8: the joined hub reaches
past +-65,536 u), the chunks' without it (xenjk7: every chunk is compiled about its own origin; world_coords' larger
base windings give other float bevels, so xenjk8 makes other chunk BSPs: the colkit check, section 36).

Steps, all into <out>.partial (renamed to <out> at the end; an existing <out> stops the run, an existing .partial is
resumed: every step and every tile's compile is kept once it is done):
  chunks   cp2gltf chunks: each tile's chunk glb in its own frame (tiles/<tag>/chunks), with cp77_tiles' collision
           arguments (the parts' own)
  hub      the hub's glb: cp2gltf's base.glb (the ring, lid and sky) with the area's pocket walled in
           (areas/<area>_hub.json) and a void kill (a trigger_hurt, dmg 9999) under the ring's box; then one gltf2map compile on its own
           worker, its BSP light grid under 65,536 distinct samples (once more with the vertical spacing doubled
           past that); the compile's spawns replaced by the area's (the hub data's, in its order); the check
           (classes, the trigger, model 0), the grid at the data's points and every spawn's trace corpus
  compile  every tile's chunk BSP (gltf2map --col-chunk --exact-planes --cook: the brushes as records through
           q3map2 -brushbin, no .map, tiles or scratch; --text: the .map path through --scratch) but a tile wholly
           outside the void kill's box (unreachable: kit10's rule), largest glb first, on --workers processes, each
           start waiting only for memory: its own peak (compile_peak_gb, from its glb's size) plus 2 GB, in
           available memory and in free commit, after what the running compiles may still take (no
           fixed interval, so empty and small tiles start at once); a tile stopped half way is moved aside and
           compiled again
  packs    zz_<map>_colchunks.pk3 (then 2, 3, ...: at most 3.5 GB each, the engine reads no zip64), written as the
           compiles finish: each chunk's BSP copied as compressed from its tile's pack into the open pack, and the
           tile's pack then removed (the tiles' packs are not kept; a run stopped with a pack
           open compiles that pack's tiles again). maps/<map>.chunks (the residency manifest: BSP, origin, placed
           bounds, in the manifest's tile order) and <map>_chunks.cfg (a cm_loadchunk line a chunk) go in the last
           pack (readers find them by name in any pack). zz_<map>.pk3 is the hub.
  record   collision.json: the inputs (sha256 of the tools, q3map2, the manifest and the hub data), the tiles (placed,
           empty, outside), the hub's check and the packs' sha256

Every pack is in <out> and nothing else ends in .pk3 there (the driver links every *.pk3 into the kit). --tiles
compiles only those (a check; the record says so). Normal priority; --idle (while the machine is in use) at Idle with one
worker. Stops under 25 GB free on C: or D:. Converted levels are for local play only."""
import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import traceback
import zipfile
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import colbsp  # noqa: E402
import memgate  # noqa: E402
import paths  # noqa: E402
import cp2gltf  # noqa: E402
import cp77_tiles  # noqa: E402
import gltfio  # noqa: E402
import levelport as lp  # noqa: E402

PY = sys.executable
FREE_GB = 25                    # on each drive the stage writes (its build, the work folder, the hub cache), before
                                # every step and every start; D: only when one of those is on it (a run once
                                # stopped 29 min for D:, which it never writes)
# a start waits for its own peak plus MARGIN_GB, in available memory and in free commit, after what the compiles
# already running may still take (each one's peak less its process tree's private bytes now), in place
# of a fixed 45 s between starts (198 of 213 gaps were the 45 s, the tile work 70 min of the
# 3.0 h). A cooked compile's peak, --jobs 4: about 3.3 GB + 0.022 GB a MB of glb (-1_3, 39 MB: 4.1 GB; -6_0, 197 MB:
# 7.5 GB, the converter's process); the text path's, unmeasured, taken higher; the hub's, 6 GB.
MARGIN_GB = 2.0
HUB_GB = 6.0
POLL_S = 2.0


def compile_peak_gb(glb_bytes, text=False):
    mb = glb_bytes / 1e6
    return 4.0 + 0.03 * mb if text else 3.3 + 0.022 * mb
HUB_CACHE = paths.LEVELPORT / 'cache' / 'hub'   # compiled hubs by their inputs' key
# compiled chunks by content: a key of the chunk glb's geometry (its JSON without the
# 'source' line, which names the tiles tag, and its binary buffer), the tile, the compile path's code (gltf2map.py and
# every tools/levelport module it imports), the chunks' q3map2 folder and the arguments that change the bytes. A hit
# raw-copies the member from the pack an earlier build wrote, checked against its recorded CRC and sizes; a missing or
# changed pack is a miss. Entries are added when a build finishes (the packs at their final path).
CHUNK_CACHE = paths.LEVELPORT / 'cache' / 'chunks' / 'index.json'
CHUNKS_PEAK_GB = 45.0         # cp2gltf chunks' reservation (Watson: over 43 GB of commit, more than the RAM)
PACK_MAX = 3_500_000_000        # bytes in one pack: under the engine's 4 GB (no zip64), with room for the directory
GRID_MAX = 65536                # distinct light grid samples: the light array's index is an unsigned short
READ = 4 << 20
STAND = ((-15, -15, -24), (15, 15, 40))     # the standing box, for the spawn corpus
PLAYERSOLID = 0x1111


def log(msg, f=None):
    line = '%s %s' % (time.strftime('%H:%M:%S'), msg)
    print(line, flush=True)
    if f is not None:
        with open(f, 'a', encoding='utf-8') as fp:
            fp.write(line + '\n')


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(READ), b''):
            h.update(b)
    return h.hexdigest()


WRITES = []                     # the folders this run writes (Run sets them)


def written_drives():
    out = set()
    for p in WRITES or [paths.REPO, paths.LEVELPORT]:      # (before Run sets them: the checkout's and the level port's)
        p = Path(p)
        while not p.exists() and p.parent != p:
            p = p.parent
        out.add(os.path.splitdrive(os.path.realpath(p))[0].upper() + '/')
    return sorted(out)


def free_ok(label):
    for d in written_drives():
        free = shutil.disk_usage(d).free / 2 ** 30
        if free < FREE_GB:
            sys.exit('cp77_collision: %s has %.0f GB free (under %d) before %s' % (d, free, FREE_GB, label))


def memory():
    """(available physical GB, free commit GB): memgate's, the commit limit capped at physical RAM plus
    memgate.commit_cap (the uncapped limit grew with the page file, 88.7 GB on C: once)"""
    import memgate
    return memgate.memory()


# ---- the hub's glb: the pocket and the void kill (hub9/hub_inject.py glb) ----

def read_glb(path):
    raw = Path(path).read_bytes()
    jlen = struct.unpack_from('<I', raw, 12)[0]
    doc = json.loads(raw[20:20 + jlen])
    off = 20 + jlen
    blen = struct.unpack_from('<I', raw, off)[0]
    return doc, bytearray(raw[off + 8:off + 8 + blen])


def write_glb(path, doc, bin_):
    while len(bin_) % 4:
        bin_.append(0)
    doc['buffers'] = [{'byteLength': len(bin_)}]
    js = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    js += b' ' * (-len(js) % 4)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(bin_)))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js)
        f.write(struct.pack('<I4s', len(bin_), b'BIN\0') + bytes(bin_))


def add_accessor(doc, bin_, arr, ctype, atype, target, minmax=False):
    while len(bin_) % 4:
        bin_.append(0)
    doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(bin_), 'byteLength': arr.nbytes, 'target': target})
    bin_ += arr.tobytes()
    acc = {'bufferView': len(doc['bufferViews']) - 1, 'componentType': ctype, 'count': int(arr.shape[0]), 'type': atype}
    if minmax:
        acc['min'], acc['max'] = arr.min(0).astype(float).tolist(), arr.max(0).astype(float).tolist()
    doc['accessors'].append(acc)
    return len(doc['accessors']) - 1


def box_like(q0, lo, hi):
    """A box from lo to hi in a ring box's own vertex pattern (each vertex at its box's min or max on each axis), so
    its winding is the ring's"""
    bmin, bmax = q0.min(0), q0.max(0)
    return np.array([[hi[k] if abs(v[k] - bmax[k]) < abs(v[k] - bmin[k]) else lo[k] for k in range(3)] for v in q0])


def add_box_node(doc, bin_, name, q, tris, mat, extras):
    pa = add_accessor(doc, bin_, lp.q3_to_gltf(q).astype('<f4'), 5126, 'VEC3', 34962, True)
    ia = add_accessor(doc, bin_, np.asarray(tris).reshape(-1).astype('<u4'), 5125, 'SCALAR', 34963)
    doc.setdefault('meshes', []).append({'name': name, 'primitives': [
        {'attributes': {'POSITION': pa}, 'indices': ia, 'material': mat}]})
    doc['nodes'].append({'name': name, 'mesh': len(doc['meshes']) - 1, 'extras': extras})
    return len(doc['nodes']) - 1


def hub_glb(src, out, pocket=None, depth=2.0, thick=80.0):
    """cp2gltf's base.glb with a void kill: a trigger_hurt (dmg 9999) over the ring's box, its top depth m under the
    ring's bottom + 20 m (cp2gltf puts the ring's bottom 20 m under the lowest collision), thick m deep, so a fall into
    the bay or the badlands dies instead of landing on the sky shell's floor. pocket (q3 units, a block south of the
    ring): the ring's south wall opened over the block's x span, the block walled in 1 m out and 1 m thick as the
    ring, a lid piece over it, and the kill box under the pocket too. Returns the report (the kill box among it)."""
    d = gltfio.Document(src)
    ring = []                                   # (q3 positions 8x3, triangles, node index, material) of each ring box
    lid = None
    for i, nd, m, chain in d.walk():
        ex = nd.get('extras') or {}
        if 'mesh' in nd and ex.get('lp') == 'collision' and 'playerclip' in (ex.get('contents') or []):
            for pos, _, _, tris, _ in d.primitives(nd['mesh']):
                w = lp.gltf_to_q3((np.c_[pos, np.ones(len(pos))] @ m.T)[:, :3])
                if re.fullmatch(r'clip\d+', nd.get('name', '')):
                    ring.append((w, tris, i, d.doc['meshes'][nd['mesh']]['primitives'][0].get('material')))
                elif nd.get('name') == 'ceiling':
                    lid = w
    if len(ring) < 4:
        sys.exit('cp77_collision: %s has %d ring boxes (clip<n>, playerclip): not a box ring' % (src, len(ring)))
    upm = lp.UNITS_PER_METRE
    doc, bin_ = read_glb(src)
    q0, tris0 = ring[0][0], ring[0][1]
    rep = {}
    if pocket:
        px0, py0, px1, py1 = pocket
        south = min(ring, key=lambda r: r[0][:, 1].max())
        s_lo, s_hi = south[0].min(0), south[0].max(0)
        if not (s_lo[0] < px0 and px1 < s_hi[0] and py0 < s_lo[1] and py1 >= s_hi[1] - 2 * upm):
            sys.exit('cp77_collision: the pocket %s does not hang off the south wall %s..%s' % (
                pocket, s_lo.tolist(), s_hi.tolist()))
        if lid is None:
            sys.exit('cp77_collision: no ceiling box for the pocket\'s lid')
        o, t = 1.0 * upm, 1.0 * upm            # the ring's: out 1 m, 1 m thick
        z0, z1 = s_lo[2], s_hi[2]
        mat = south[3]
        walls = {'clip_south_w': ((s_lo[0], s_lo[1]), (px0 - o, s_hi[1])),
                 'clip_south_e': ((px1 + o, s_lo[1]), (s_hi[0], s_hi[1])),
                 'clip_pocket_w': ((px0 - o - t, py0 - o - t), (px0 - o, s_hi[1])),
                 'clip_pocket_e': ((px1 + o, py0 - o - t), (px1 + o + t, s_hi[1])),
                 'clip_pocket_s': ((px0 - o - t, py0 - o - t), (px1 + o + t, py0 - o))}
        for parent in doc['nodes'] + [doc['scenes'][doc.get('scene', 0)]]:
            if south[2] in parent.get('children' if 'children' in parent else 'nodes', []):
                key = 'children' if 'children' in parent else 'nodes'
                parent[key] = [c for c in parent[key] if c != south[2]]
        root = doc['scenes'][doc.get('scene', 0)]['nodes']
        for name, ((x0, y0), (x1, y1)) in walls.items():
            root.append(add_box_node(doc, bin_, name, box_like(q0, (x0, y0, z0), (x1, y1, z1)), tris0, mat,
                                     {'lp': 'collision', 'contents': ['playerclip']}))
        l_lo, l_hi = lid.min(0), lid.max(0)
        root.append(add_box_node(doc, bin_, 'ceiling_pocket', box_like(
            q0, (px0 - o - t - 2 * upm, py0 - o - t - 2 * upm, l_lo[2]), (px1 + o + t + 2 * upm, l_lo[1] + upm, l_hi[2])),
            tris0, mat, {'lp': 'collision', 'contents': ['playerclip']}))
        rep['pocket_walls'] = {k: [list(map(float, a)), list(map(float, b))] for k, (a, b) in walls.items()}
        ring = [r for r in ring if r[2] != south[2]] + [(np.array([[px0 - o - t, py0 - o - t, z0]]), None, None, None)]
    allq = np.concatenate([r[0] for r in ring])
    lo, hi = allq.min(0), allq.max(0)
    top = lo[2] + (20.0 - depth) * upm
    bot = top - thick * upm
    kill = box_like(q0, (lo[0], lo[1], bot), (hi[0], hi[1], top))
    doc.setdefault('materials', []).append({'name': 'collision_trigger', 'extras': {'surfaceparms': []}})
    body = add_box_node(doc, bin_, 'void_kill_brush', kill, tris0, len(doc['materials']) - 1,
                        {'lp': 'collision', 'contents': ['trigger']})
    doc['nodes'].append({'name': 'void_kill', 'children': [body],
                         'extras': {'lp': 'entity', 'classname': 'trigger_hurt', 'dmg': '9999'}})
    doc['scenes'][doc.get('scene', 0)]['nodes'].append(body + 1)
    write_glb(out, doc, bin_)
    rep.update({'ring_boxes': len(ring), 'ring_q3_min': lo.round(2).tolist(), 'ring_q3_max': hi.round(2).tolist(),
                'kill_q3_min': kill.min(0).round(2).tolist(), 'kill_q3_max': kill.max(0).round(2).tolist()})
    return rep


# ---- the hub's BSP: spawns, check, light grid, spawn corpus (hub_inject.py spawns/check/grid, spawn_corpus.py) ----

def entities(text):
    return re.findall(r'\{[^{}]*\}', text)


def entity_text(b):
    o, n = b.lumps[0]
    return b.data[o:o + n].decode('latin1')


def put_spawns(new, out, spawns):
    """the compiled hub with its info_player_deathmatch entities replaced by spawns (the hub data's, in order);
    every other member and lump byte for byte"""
    b = colbsp.read(new)
    ents = entities(entity_text(b))
    kept = [e for e in ents if '"info_player_deathmatch"' not in e]
    sp = ['{\n"classname" "info_player_deathmatch"\n"angle" "%s"\n"origin" "%s"\n}' % (s['angle'], s['origin'])
          for s in spawns]
    lump = np.frombuffer(('\n'.join(kept + sp) + '\n').encode('latin1') + b'\0', np.uint8)
    tmp = out.with_name(out.name + '.bsp')
    with zipfile.ZipFile(new) as zi:
        bsp_name = next(x for x in zi.namelist() if x.endswith('.bsp'))
        colbsp.write(tmp, b, {0: lump})
        with zipfile.ZipFile(out.with_name(out.name + '.tmp'), 'w', zipfile.ZIP_DEFLATED) as zo:
            for info in zi.infolist():
                if info.filename == bsp_name:
                    zo.write(tmp, bsp_name)
                else:
                    zo.writestr(info, zi.read(info.filename))
    tmp.unlink()                                 # (this call's temporary BSP: the pk3 holds it)
    out.with_name(out.name + '.tmp').replace(out)
    return {'compiled_spawns_dropped': len(ents) - len(kept), 'spawns': len(sp), 'entities': len(kept) + len(sp)}


MODEL = np.dtype([('mins', '<f4', 3), ('maxs', '<f4', 3), ('rest', '<i4', 4)])


def hub_check(pk3):
    b = colbsp.read(pk3)
    ents = entities(entity_text(b))
    cls = {}
    for e in ents:
        c = re.search(r'"classname" "([^"]+)"', e)
        cls[c[1] if c else '?'] = cls.get(c[1] if c else '?', 0) + 1
    models = b.lump(colbsp.LUMP_NAMES.index('models'), MODEL)
    rep = {'classes': cls, 'model0': [models[0]['mins'].tolist(), models[0]['maxs'].tolist()],
           'spawns': [re.search(r'"origin" "([^"]+)"', e)[1] for e in ents if '"info_player_deathmatch"' in e],
           'trigger_hurt': [], 'grid_samples': grid_samples(b)}
    for e in ents:
        if '"trigger_hurt"' in e:
            mi = int(re.search(r'"model" "\*(\d+)"', e)[1])
            rep['trigger_hurt'].append({'model': mi, 'mins': models[mi]['mins'].tolist(),
                                        'maxs': models[mi]['maxs'].tolist()})
    return rep


def grid_samples(b):
    return b.lumps[colbsp.LUMP_NAMES.index('lightgrid')][1] // 30


def grid_at(pk3, p):
    """the BSP light grid's sample at a point (q3 units) as the renderer indexes it (model 0's bounds, worldspawn's
    gridsize), and its six neighbours: the sample index, style 0's ambient and directed light"""
    b = colbsp.read(pk3)
    m = re.search(r'"gridsize" "([^"]+)"', entities(entity_text(b))[0])
    gs = np.array([float(v) for v in m[1].split()]) if m else np.array([64.0, 64.0, 128.0])
    models = b.lump(colbsp.LUMP_NAMES.index('models'), MODEL)
    org = gs * np.ceil(models[0]['mins'] / gs)
    dims = ((gs * np.floor(models[0]['maxs'] / gs) - org) / gs).astype(int) + 1
    arr = b.lump(colbsp.LUMP_NAMES.index('lightarray'), np.dtype('<u2'))
    samples = b.lump(colbsp.LUMP_NAMES.index('lightgrid'), np.dtype([('amb', 'u1', (4, 3)), ('dir', 'u1', (4, 3)),
                                                                     ('styles', 'u1', 4), ('ll', 'u1', 2)]))
    rep = {'point': list(p), 'gridsize': gs.tolist(), 'dims': dims.tolist(), 'samples': len(samples), 'at': []}
    c = np.floor((np.array(p, float) - org) / gs).astype(int)
    for d in [(0, 0, 0), (1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]:
        q = c + d
        k = int(q[0] + q[1] * dims[0] + q[2] * dims[0] * dims[1])
        s = samples[arr[k]]
        rep['at'].append({'cell': q.tolist(), 'index': int(arr[k]), 'ambient': s['amb'][0].tolist(),
                          'directed': s['dir'][0].tolist(), 'style0': int(s['styles'][0])})
    return rep


def spawn_corpus(spawns, out):
    """levelcheck_trace's 13-number lines for every spawn: the standing box (a zero-length trace: in solid or not) and
    a 64 u drop from it (a floor within 64 u), MASK_PLAYERSOLID"""
    mins, maxs = STAND
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        for s in spawns:
            o = [float(v) for v in s['origin'].split()]
            f.write('%g %g %g %g %g %g %g %g %g %g %g %g %d\n' % (*o, *o, *mins, *maxs, PLAYERSOLID))
            f.write('%g %g %g %g %g %g %g %g %g %g %g %g %d\n' % (*o, o[0], o[1], o[2] - 64, *mins, *maxs, PLAYERSOLID))


# ---- the chunk packs (make_chunks_pk3.py's R2 raw copy) ----

def member(pk3, name):
    """(zip info, offset of its compressed bytes) of one member of a pk3"""
    with zipfile.ZipFile(pk3) as z:
        i = z.getinfo(name)
    with open(pk3, 'rb') as f:
        f.seek(i.header_offset)
        h = f.read(30)
    if h[:4] != b'PK\x03\x04':
        sys.exit('cp77_collision: %s: %s has no local header' % (pk3, name))
    nl, xl = struct.unpack_from('<HH', h, 26)
    return i, i.header_offset + 30 + nl + xl


def model0_bounds(pk3, name):
    """model 0's mins and maxs: the BSP's header and models lump out of a streaming inflate (nothing else kept); an
    XBSZ member (stored) decoded whole"""
    i, start = member(pk3, name)
    if i.compress_type == zipfile.ZIP_STORED:
        with open(pk3, 'rb') as f:
            f.seek(start)
            head = f.read(4)
        if head == b'XBSZ':
            with zipfile.ZipFile(pk3) as z:
                b = colbsp.Rbsp(colbsp.plain(z.read(name)))
            o = b.lumps[7][0]
            return (np.array(struct.unpack_from('<3f', b.data, o)), np.array(struct.unpack_from('<3f', b.data, o + 12)),
                    i)
    if i.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        sys.exit('cp77_collision: %s: %s compression %d' % (pk3, name, i.compress_type))
    d = zlib.decompressobj(-15) if i.compress_type == zipfile.ZIP_DEFLATED else None
    head, want, keep, pos, left = b'', None, b'', 0, i.compress_size
    with open(pk3, 'rb') as f:
        f.seek(start)
        while left > 0 or (d is not None and d.unconsumed_tail):
            raw = f.read(min(READ, left)) if left > 0 else b''
            left -= len(raw)
            buf = d.decompress(d.unconsumed_tail + raw, READ) if d is not None else raw
            if len(head) < 8 + 8 * 8:
                head += buf[:8 + 8 * 8 - len(head)]
                if len(head) >= 8 + 8 * 8:
                    want = struct.unpack_from('<i', head, 8 + 8 * 7)[0]          # the models lump's offset
            if want is not None and pos + len(buf) > want and len(keep) < 24:
                keep += buf[max(0, want - pos):max(0, want - pos) + 24 - len(keep)]
            pos += len(buf)
            if want is not None and len(keep) >= 24:
                break
    if len(keep) < 24:
        sys.exit('cp77_collision: %s: %s ended before its models lump' % (pk3, name))
    return np.array(struct.unpack_from('<3f', keep, 0)), np.array(struct.unpack_from('<3f', keep, 12)), i


def glb_geometry_digest(path):
    """sha256 of a chunk glb with its tag-naming 'source' line left out: the same geometry under another tiles tag
    digests the same (stage_chunks writes 'chunk <t> of <area>, tiles <tag>' into asset.extras.levelport.source)"""
    b = Path(path).read_bytes()
    n = struct.unpack_from('<I', b, 12)[0]
    doc = json.loads(b[20:20 + n])
    doc.get('asset', {}).get('extras', {}).get('levelport', {}).pop('source', None)
    h = hashlib.sha256(json.dumps(doc, sort_keys=True, separators=(',', ':')).encode())
    h.update(b[20 + n:])
    return h.hexdigest()


def module_closure(entry):
    """entry and every tools/levelport module it imports, at any depth (ast: imports anywhere in the file), and the
    files those modules declare they run without importing (STAGEKEY_FILES, every one: stagekey.declared_files)"""
    import ast
    seen, todo, extra = set(), [Path(entry).resolve()], set()
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        seen.add(f)
        tree = ast.parse(f.read_text(encoding='utf-8'))
        for node in ast.walk(tree):
            names = [x.name for x in node.names] if isinstance(node, ast.Import) else (
                [node.module] if isinstance(node, ast.ImportFrom) and node.module and not node.level else [])
            for nm in names:
                g = HERE / (nm.split('.')[0] + '.py')
                if g.exists():
                    todo.append(g.resolve())
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'STAGEKEY_FILES'
                                                    for t in node.targets):
                v = ast.literal_eval(node.value)
                for x in (sum(v.values(), []) if isinstance(v, dict) else v):
                    extra.add((f.parent / x).resolve())
    return sorted(seen | extra)


def compile_identity(q3map2, args):
    """what makes a chunk compile's bytes besides its glb: the compile path's code, the q3map2 folder (exe and DLLs),
    whether GameData is there for q3map2's -fs_basepath (a worktree has none), and the arguments"""
    h = hashlib.sha256()
    for f in module_closure(HERE / 'gltf2map.py'):
        h.update(('%s %s\n' % (f.name, sha256(f))).encode())
    for f in sorted(Path(q3map2).parent.glob('*')):
        if f.suffix.lower() in ('.exe', '.dll'):
            h.update(('%s %s\n' % (f.name, sha256(f))).encode())
    game = paths.GAMEDATA / 'base'
    for f in sorted(game.glob('assets*.pk3')) if game.is_dir() else []:
        st = f.stat()
        h.update(('%s %d %d\n' % (f.name, st.st_size, st.st_mtime_ns)).encode())
    h.update(json.dumps([str(a) for a in args]).encode())
    return h.hexdigest()


def game_shader_inputs(game):
    """{path: id} of what q3map2 reads from a GameData folder for a levelport map: every shader script (it loads them
    all) and the common and system textures, in the packs (pk3 name, member, CRC) and loose (size, time)"""
    out = {}
    if not game.is_dir():
        return {'none': 'no GameData'}         # (its path not keyed: a worktree's differs, and q3map2 reads nothing)
    want = lambda n: n.endswith('.shader') or n.startswith(('textures/common/', 'textures/system/'))
    for d in sorted(p for p in game.iterdir() if p.is_dir()):
        for pk in sorted(d.glob('*.pk3')):
            try:
                with zipfile.ZipFile(pk) as z:
                    for i in z.infolist():
                        n = i.filename.lower()
                        if want(n):
                            out['%s/%s:%s' % (d.name, pk.name, n)] = '%08x %d' % (i.CRC, i.file_size)
            except (OSError, zipfile.BadZipFile):
                out['%s/%s' % (d.name, pk.name)] = 'unreadable'
        for sub in ('scripts', 'shaders', 'textures/common', 'textures/system'):
            for f in sorted((d / sub).rglob('*')) if (d / sub).is_dir() else []:
                n = f.relative_to(d).as_posix().lower()
                if f.is_file() and want(n):
                    st = f.stat()
                    out['%s/%s' % (d.name, n)] = '%d %d' % (st.st_size, st.st_mtime_ns)
    return out


class ChunkCache:
    """CHUNK_CACHE's index: key -> {tile, member, crc, compress_size, file_size, packs: [the packs holding it, newest
    first]} or {tile, empty: true}"""

    def __init__(self, path=CHUNK_CACHE):
        self.path = path
        self.index = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
        self.lock = threading.Lock()

    @staticmethod
    def key(identity, tile, digest):
        return hashlib.sha256(('%s %s %s' % (identity, tile, digest)).encode()).hexdigest()

    def hit(self, key):
        """the entry when its pack still holds the member with the recorded CRC and sizes, else None"""
        e = self.index.get(key)
        if not e:
            return None
        if e.get('empty'):
            return e
        for pk in e['packs']:
            try:
                with zipfile.ZipFile(pk) as z:
                    i = z.getinfo(e['member'])
            except (OSError, KeyError, zipfile.BadZipFile):
                continue
            if (i.CRC, i.compress_size, i.file_size) == (e['crc'], e['compress_size'], e['file_size']):
                return dict(e, pack=pk)
        return None

    def miss_reason(self, key, tile, digest):
        """why a key missed: its entry's packs gone or changed, or no entry; then whether the tile has entries under
        another identity or another geometry"""
        if key in self.index:
            return 'its entry\'s packs are gone or changed'
        same_tile = [e for e in self.index.values() if e.get('tile') == tile]
        if not same_tile:
            return 'no entry for this tile'
        return 'no entry for this geometry and compile identity (%d entries for the tile)' % len(same_tile)

    def add(self, entries):
        """entries: {key: entry}; written through a temporary file (a reader never sees half an index)"""
        with self.lock:
            cur = json.loads(self.path.read_text(encoding='utf-8')) if self.path.exists() else {}
            for k, e in entries.items():
                old = cur.get(k)
                if 'pack' in e:
                    e = dict(e)
                    pk = e.pop('pack')
                    same = old and not old.get('empty') and (old['crc'], old['file_size']) == (e['crc'], e['file_size'])
                    e['packs'] = [pk] + [q for q in (old['packs'] if same else []) if q != pk]
                cur[k] = e
            self.path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self.path.with_suffix('.tmp')
            tmp.write_text(json.dumps(cur, indent=0, sort_keys=True), encoding='utf-8')
            tmp.replace(self.path)
            self.index = cur


def raw_copy(z, pk3, name):
    """one member's compressed bytes into the open (writing) ZipFile z as they are: its CRC and sizes kept"""
    i, start = member(pk3, name)
    zi = zipfile.ZipInfo(name, i.date_time)
    zi.compress_type, zi.CRC, zi.compress_size, zi.file_size = i.compress_type, i.CRC, i.compress_size, i.file_size
    zi.flag_bits = i.flag_bits & ~0x08                                       # sizes in the local header
    zi.external_attr, zi.create_system = i.external_attr, i.create_system
    zi.header_offset = z.fp.tell()
    z.fp.write(zi.FileHeader(zip64=False))
    with open(pk3, 'rb') as f:
        f.seek(start)
        left = i.compress_size
        while left > 0:
            b = f.read(min(READ, left))
            if not b:
                sys.exit('cp77_collision: %s: %s is short' % (pk3, name))
            z.fp.write(b)
            left -= len(b)
    z.filelist.append(zi)
    z.NameToInfo[name] = zi
    z.start_dir = z.fp.tell()
    z._didModify = True


def recover_members(src, out):
    """the whole members of a pack left open (no central directory) into a closed pack at out: local headers read in
    order, a member kept when its name is a chunk BSP and its data is all there with the CRC its header names
    (deflated ones checked by inflating, stored ones by the CRC). -> the tiles recovered"""
    tiles, data = [], Path(src).read_bytes()
    pos, entries = 0, []
    while pos + 30 <= len(data) and data[pos:pos + 4] == b'PK\x03\x04':
        (ver, flag, meth, mt, md, crc, csize, usize, nlen, xlen) = struct.unpack_from('<HHHHHIIIHH', data, pos + 4)
        name = data[pos + 30:pos + 30 + nlen].decode('utf-8', 'replace')
        start = pos + 30 + nlen + xlen
        if flag & 0x08 or start + csize > len(data):
            break                             # (sizes after the data, or the data cut short: the stop came here)
        blob = data[start:start + csize]
        raw = zlib.decompress(blob, -15) if meth == zipfile.ZIP_DEFLATED else blob
        m = re.fullmatch(r'maps/chunk_(.+)\.bsp', name)
        if m and len(raw) == usize and zlib.crc32(raw) & 0xffffffff == crc:
            entries.append((name, meth, (mt, md), crc, csize, usize, start))
            tiles.append(m[1])
        pos = start + csize
    if not entries:
        return []
    with zipfile.ZipFile(out, 'w') as z:
        for name, meth, (mt, md), crc, csize, usize, start in entries:
            zi = zipfile.ZipInfo(name, ((md >> 9) + 1980, (md >> 5) & 15, md & 31, mt >> 11, (mt >> 5) & 63,
                                        (mt & 31) * 2))
            zi.compress_type, zi.CRC, zi.compress_size, zi.file_size = meth, crc, csize, usize
            zi.header_offset = z.fp.tell()
            z.fp.write(zi.FileHeader(zip64=False))
            z.fp.write(data[start:start + csize])
            z.filelist.append(zi)
            z.NameToInfo[name] = zi
            z.start_dir = z.fp.tell()
            z._didModify = True
    return tiles


class PackWriter:
    """The chunk packs as the compiles finish (filled in the order the chunks come): a chunk's BSP
    raw-copied into the open pack, then its tile's pack removed. Full packs close as zz_<map>_colchunks2.pk3, 3, ...;
    the last, with the manifest and the cfg, as zz_<map>_colchunks.pk3 (the name tools read the manifest from).
    state/packs.json records each closed pack's tiles and every packed chunk's bounds; a pack still open when a run
    stops is moved aside on the resume and its tiles are compiled again (their tile packs are gone)."""

    def __init__(self, part, mapname, logf):
        self.part, self.mapname, self.logf = part, mapname, logf
        self.lock = threading.Lock()
        self.rec = part / 'state' / 'packs.json'
        st = json.loads(self.rec.read_text(encoding='utf-8')) if self.rec.exists() else {'closed': [], 'chunks': {}}
        self.closed = st['closed']            # [{'pack', 'tiles'}]
        self.chunks = {t: c for t, c in st['chunks'].items() if any(t in k['tiles'] for k in self.closed)}
        self.open_path = part / ('zz_%s_colchunks.open.tmp' % mapname)
        self.recovered = {}                   # tile -> a pack holding its chunk, recovered from a stopped run's open pack
        for q in part.glob('zz_%s_colchunks*.tmp' % mapname):     # open when the last run stopped
            dst = part / 'stopped' / ('%s-%s' % (q.name, time.strftime('%Y%m%dT%H%M%S')))
            q.rename(dst)
            # its whole members (each local header names its sizes: raw_copy writes them) into a closed pack, so
            # those chunks are packed again rather than compiled (one pack once closed only after 73 min)
            rec = recover_members(dst, dst.with_name(dst.name + '.recovered.pk3'))
            for t in rec:
                self.recovered[t] = dst.with_name(dst.name + '.recovered.pk3')
            log('packs: %s was open when the run stopped: moved to %s; %d whole chunks recovered (%s), the rest '
                'compile again' % (q.name, dst, len(rec), ', '.join(rec[:8]) + (' ...' if len(rec) > 8 else '')),
                logf)
        self.z, self.open_tiles = None, []

    def packed(self, t):
        return t in self.chunks

    def _save(self):
        tmp = self.rec.with_suffix('.tmp')
        tmp.write_text(json.dumps({'closed': self.closed, 'chunks': self.chunks}, indent=1), encoding='utf-8')
        tmp.replace(self.rec)

    def _close(self, last=False):
        name = 'zz_%s_colchunks%s.pk3' % (self.mapname, '' if last else str(len(self.closed) + 2))
        self.z.close()
        self.open_path.replace(self.part / name)
        self.closed.append({'pack': name, 'tiles': self.open_tiles})
        self._save()
        log('packs: %s closed, %d chunks' % (name, len(self.open_tiles)), self.logf)
        self.z, self.open_tiles = None, []

    def _open(self):
        self.z = zipfile.ZipFile(self.open_path, 'w', zipfile.ZIP_DEFLATED)

    def add(self, t, pk3, origin, keep=False):
        """the tile's chunk BSP into the open pack, its bounds kept, its tile pack removed (keep: a cached build's
        pack, copied from and left as it is)"""
        name = 'maps/chunk_%s.bsp' % t
        mins, maxs, i = model0_bounds(pk3, name)
        with self.lock:
            if self.z is None:
                self._open()
            if self.z.fp.tell() + i.compress_size + 4096 > PACK_MAX:
                self._close()
                self._open()
            raw_copy(self.z, pk3, name)
            self.open_tiles.append(t)
            self.chunks[t] = {'origin': [int(v) for v in origin], 'mins': mins.tolist(), 'maxs': maxs.tolist(),
                              'bytes': i.file_size}
        if not keep:
            pk3.unlink()                      # (copied: the tile's pack is not kept)

    def finish(self, placed, note):
        """placed: [(tile, origin)] in the manifest's order. The manifest and the cfg into the last pack, closed
        under the first name."""
        manifest = ['// %s collision chunks: %s: bsp, origin, placed bounds (mins, maxs)' % (self.mapname, note)]
        cfg = ['// %s collision chunks: %s' % (self.mapname, note)]
        for t, o in placed:
            c = self.chunks[t]
            name, o = 'maps/chunk_%s.bsp' % t, np.asarray(o)
            manifest.append('%s %d %d %d %s %s' % (name, *o, ' '.join('%.1f' % v for v in np.array(c['mins']) + o),
                                                 ' '.join('%.1f' % v for v in np.array(c['maxs']) + o)))
            cfg.append('cm_loadchunk %s %d %d %d' % (name, *o))
        cfg.append('echo %s: %d collision chunks placed' % (self.mapname, len(placed)))
        with self.lock:
            if self.z is None:
                self._open()
            self.z.writestr('%s_chunks.cfg' % self.mapname, '\n'.join(cfg) + '\n')
            self.z.writestr('maps/%s.chunks' % self.mapname, '\n'.join(manifest) + '\n')
            self._close(last=True)
        return [self.part / k['pack'] for k in self.closed], sum(self.chunks[t]['bytes'] for t, _ in placed)


# ---- the run ----

class Run:
    def __init__(self, a):
        self.a = a
        self.work = cp2gltf.work_dir(a.area)
        self.tdir = cp2gltf.tile_dir(self.work, a.tiles_tag)
        self.cdir = self.tdir / 'chunks'
        self.out = Path(a.out).resolve()
        self.part = self.out.with_name(self.out.name + '.partial')
        WRITES[:] = [self.out.parent, self.work, HUB_CACHE]
        self.state = self.part / 'state'
        self.logf = self.part / 'collision.log'
        hub = HERE / 'areas' / ('%s_hub.json' % a.area)
        if not hub.exists():
            sys.exit('cp77_collision: no hub data for %s (%s)' % (a.area, hub))
        self.hub_data_file = hub
        self.hub = json.loads(hub.read_text(encoding='utf-8'))
        self.q3map2 = Path(a.q3map2).resolve()
        self.q3map2_chunks = Path(a.q3map2_chunks).resolve()
        for q in (self.q3map2, self.q3map2_chunks):
            if not q.exists():
                sys.exit('cp77_collision: no q3map2 at %s' % q)
        self.started = threading.Lock()
        self.running = {}                  # start label -> [its peak (GB), its process id or None]
        self.ledger = {}                   # start label -> its memgate ledger file (the driver's lanes)
        self.running_lock = threading.Lock()
        self.stop = threading.Event()

    def done(self, step):
        return (self.state / (step + '.done')).exists()

    def chunk_args(self):
        return ['--col-chunk', '--exact-planes', '--cook', '--plain' if self.a.plain else '--xbsz']

    def chunk_key(self, t):
        with self.digest_lock:
            if t not in self.digests:
                self.digests[t] = glb_geometry_digest(self.cdir / ('%s.glb' % t))
            return ChunkCache.key(self.identity, t, self.digests[t])

    def index_packs(self, tiles):
        """every compiled or reused tile of this finished build into the chunk cache, at the packs' final paths"""
        entries = {}
        for k in self.packs.closed:
            pk = self.out / k['pack']
            with zipfile.ZipFile(pk) as z:
                for t in k['tiles']:
                    i = z.getinfo('maps/chunk_%s.bsp' % t)
                    entries[self.chunk_key(t)] = {'tile': t, 'pack': str(pk), 'member': i.filename, 'crc': i.CRC,
                                                  'compress_size': i.compress_size, 'file_size': i.file_size,
                                                  'build': self.out.name}
        for t, _ in tiles:
            if (self.out / 'chunks' / t / 'empty_chunk.txt').exists():
                entries[self.chunk_key(t)] = {'tile': t, 'empty': True, 'build': self.out.name}
        self.cache.add(entries)
        return len(entries)

    def seed(self, build):
        """the chunk cache seeded from a finished build (its collision.json names its tiles; its packs hold them):
        only after a cold compile of some of its tiles under this run's identity matched its bytes"""
        rec = json.loads((build / 'collision.json').read_text(encoding='utf-8'))
        if rec['tiles_tag'] != self.a.tiles_tag or rec.get('chunk_path') != 'cook':
            sys.exit('cp77_collision: %s is tiles tag %s, path %s: not a cooked build of tiles tag %s' % (
                build, rec['tiles_tag'], rec.get('chunk_path'), self.a.tiles_tag))
        self.digests, self.digest_lock = {}, threading.Lock()
        self.identity = compile_identity(self.q3map2_chunks, self.chunk_args())
        where = {}
        for pk in sorted(build.glob('zz_%s_colchunks*.pk3' % self.a.map)):
            with zipfile.ZipFile(pk) as z:
                for i in z.infolist():
                    m = re.fullmatch(r'maps/chunk_(.+)\.bsp', i.filename)
                    if m:
                        where[m[1]] = (pk, i)
        entries = {}
        for t in rec['tiles']['placed']:
            pk, i = where[t]
            entries[self.chunk_key(t)] = {'tile': t, 'pack': str(pk), 'member': i.filename, 'crc': i.CRC,
                                          'compress_size': i.compress_size, 'file_size': i.file_size,
                                          'build': build.name, 'seeded': True}
        for t in rec['tiles']['empty']:
            entries[self.chunk_key(t)] = {'tile': t, 'empty': True, 'build': build.name, 'seeded': True}
        ChunkCache().add(entries)
        print('cp77_collision: seeded %d chunks (%d placed, %d empty) from %s under compile identity %s (%s)' % (
            len(entries), len(rec['tiles']['placed']), len(rec['tiles']['empty']), build, self.identity[:16],
            ' '.join(self.chunk_args())))

    def mark(self, step, info=None):
        (self.state / (step + '.done')).write_text(json.dumps(info or {}, indent=1), encoding='utf-8')

    def sh(self, cmd, logf, env=None, start=None):
        """run cmd; start: wait_start's label, whose reservation follows the process until it ends"""
        cmd = [str(c) for c in cmd]
        log('$ %s' % ' '.join(cmd), self.logf)
        try:
            with open(logf, 'a', encoding='utf-8') as f:
                f.write('== %s %s\n' % (' '.join(cmd), time.strftime('%H:%M:%S')))
                f.flush()
                p = subprocess.Popen(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, **(env or {})))
                if start is not None:
                    with self.running_lock:
                        self.running[start][1] = p.pid
                        memgate.ledger_bind(self.ledger.get(start), p.pid)
                return p.wait()
        finally:
            if start is not None:
                with self.running_lock:
                    self.running.pop(start, None)
                    memgate.ledger_remove(self.ledger.pop(start, None))

    def outstanding(self):
        """what the running compiles may still take (GB): each one's peak less its process tree's private bytes
        (all of it before its process is up)"""
        import psutil
        with self.running_lock:
            runs = list(self.running.values())
        out = 0.0
        for peak, pid in runs:
            used = 0.0
            if pid is not None:
                try:
                    root = psutil.Process(pid)
                    for q in [root] + root.children(recursive=True):
                        try:
                            used += q.memory_info().private / 2 ** 30
                        except psutil.Error:
                            pass
                except psutil.Error:
                    pass
            out += max(0.0, peak - used)
        return out + memgate.ledger_others()      # (the driver's other lane)

    # chunks: cp2gltf chunks, once per tiles tag
    def chunks(self):
        if self.done('chunks'):
            return
        free_ok('chunks')
        if not (self.tdir / 'tiles.json').exists():
            sys.exit('cp77_collision: no tiles tag %s (%s)' % (self.a.tiles_tag, self.tdir))
        # one process holding the whole area's collision geometry: Watson's passes 43 GB of commit on 32 GB of RAM, and
        # beside the other lane it grew the page file until C: was nearly full (tags 14, 15): it runs with nothing else
        self.wait_start('chunks', CHUNKS_PEAK_GB)
        r = self.sh([PY, '-u', HERE / 'cp2gltf.py', 'chunks', '--area', self.a.area, '--tiles-tag', self.a.tiles_tag,
                     '--map', self.a.map, *cp77_tiles.BUILD_ARGS,
                     *(['--clutter-drops', self.a.clutter_drops] if self.a.clutter_drops else [])],
                    self.part / 'logs' / 'chunks.log', start='chunks')
        if r or not (self.cdir / 'manifest.json').exists() or not (self.cdir / 'base.glb').exists():
            sys.exit('cp77_collision: cp2gltf chunks failed (exit %d): %s' % (r, self.part / 'logs' / 'chunks.log'))
        self.mark('chunks', {'manifest_sha256': sha256(self.cdir / 'manifest.json'),
                             'clutter_drops': str(self.a.clutter_drops) if self.a.clutter_drops else None})

    # the hub's glb (before the compiles: its kill box decides which tiles are reachable)
    def hub_glb(self):
        if not self.done('hub_glb'):
            rep = hub_glb(self.cdir / 'base.glb', self.part / 'hub' / 'base_hub.glb', self.hub.get('pocket'))
            log('hub glb: %s' % json.dumps(rep), self.logf)
            self.mark('hub_glb', rep)
        return json.loads((self.state / 'hub_glb.done').read_text(encoding='utf-8'))

    def wait_start(self, label, peak):
        """one start at a time: the free space, then room for this start's peak after what the running compiles may
        still take; the start is reserved here (sh() binds it to its process). No fixed wait."""
        with self.started:
            free_ok(label)
            since, said = time.time(), 0.0
            while True:
                avail, commit = memory()
                out = self.outstanding()
                if avail - out >= peak + MARGIN_GB and commit - out >= peak + MARGIN_GB:
                    break
                others = memgate.ledger_others()
                with self.running_lock:
                    alone = not self.running and not memgate.ledger_others(live=True)   # (the driver's other lane counts)
                if not alone and time.time() - said >= 60:
                    log(memgate.waiting_line(label, peak, MARGIN_GB, avail, commit, out, others, since), self.logf)
                    said = time.time()
                if alone:                      # nothing of ours running: waiting would never end
                    log('%s starts alone: %.1f GB available, %.1f GB free commit, its peak taken as %.1f GB' % (
                        label, avail, commit, peak), self.logf)
                    break
                time.sleep(POLL_S)
            with self.running_lock:
                self.running[label] = [peak, None]
                self.ledger[label] = memgate.ledger_add(label, peak)
            return avail, commit, out

    def hub_key(self):
        """what the hub's compile reads (reuse it only on an exact match;
        no longer every tools file and every GameData file, which a patch_materials edit or a model install changed):
        the hub glb's bytes, gltf2map and every tools/levelport module it imports, the hub q3map2 build's files, the
        arguments, and of the GameData folder gltf2map hands q3map2 (-fs_basepath) what q3map2 opens there: every
        shader script ("No shaderlist.txt found: loading all shaders") and the common and system textures its shaders
        resolve, by CRC from the packs' directories and by size and time loose (a worktree has no GameData: "none")"""
        h = hashlib.sha256()

        def add(tag, data):
            h.update(tag.encode('utf-8') + b'\0' + (data if isinstance(data, bytes) else data.encode('utf-8')) + b'\0')
        add('glb', sha256(self.part / 'hub' / 'base_hub.glb'))
        for p in module_closure(HERE / 'gltf2map.py'):
            add('tool ' + p.name, sha256(p))
        for p in sorted(self.q3map2.parent.iterdir()):
            if p.is_file():
                add('q3map2 ' + p.name, sha256(p))
        add('args', json.dumps([self.a.map, self.hub['gltf2map'], '--exact-planes', GRID_MAX]))
        game = paths.GAMEDATA       # gltf2map.GAME
        for k, v in sorted(game_shader_inputs(game).items()):
            add('game ' + k, v)
        return h.hexdigest()[:32]

    def hub_compile(self):
        if self.done('hub_compile'):
            return
        hb = self.part / 'hub'
        g = self.hub['gltf2map']
        key = cached = None
        if not self.a.no_hub_cache:
            key = self.hub_key()
            cached = HUB_CACHE / key
            log('hub cache: key %s, %s' % (key[:16], 'a hit' if (cached / 'hub_compile.json').exists() else
                                           'a miss (%d hubs kept under other keys)' % len(list(HUB_CACHE.glob('*'))
                                                                                         if HUB_CACHE.is_dir() else [])),
                self.logf)
            if (cached / 'hub_compile.json').exists():
                info = json.loads((cached / 'hub_compile.json').read_text(encoding='utf-8'))
                dst = self.part / info['pk3']
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(cached / dst.name, dst)
                if sha256(dst) != info['sha256']:
                    sys.exit('cp77_collision: the cached hub %s is not its recorded bytes' % cached)
                info['reused'] = key
                log('hub compile: reused %s (its inputs\' key matches exactly)' % cached, self.logf)
                self.mark('hub_compile', info)
                return

        def compile_(o, extra=()):
            if o.exists():
                o.rename(o.with_name('%s.stopped-%s' % (o.name, time.strftime('%Y%m%dT%H%M%S'))))
            avail, commit, out = self.wait_start('the hub', HUB_GB)
            log('hub compile -> %s (available %.1f GB, free commit %.1f GB, %.1f GB reserved by running compiles)' % (
                o, avail, commit, out), self.logf)
            r = self.sh([PY, '-u', HERE / 'gltf2map.py', hb / 'base_hub.glb', self.a.map, '--out', o, *g, *extra,
                         '--exact-planes'], self.part / 'logs' / 'hub.log', env={'Q3MAP2': str(self.q3map2)},
                        start='the hub')
            pk = o / ('zz_%s.pk3' % self.a.map)
            if r or not pk.exists():
                sys.exit('cp77_collision: the hub compile failed (exit %d): %s' % (r, self.part / 'logs' / 'hub.log'))
            return pk
        pk = compile_(hb / 'gen')
        b = colbsp.read(pk)
        s = grid_samples(b)
        log('hub light grid: %d distinct samples' % s, self.logf)
        grid2 = None
        if s >= GRID_MAX:
            m = re.search(r'"gridsize" "([^"]+)"', entities(entity_text(b))[0])
            gx, gy, gz = (int(float(v)) for v in m[1].split())
            grid2 = '%d %d %d' % (gx, gy, gz * 2)
            log('hub light grid over %d: again with gridsize %s' % (GRID_MAX - 1, grid2), self.logf)
            pk = compile_(hb / 'gen2', ['--worldspawn', 'gridsize=' + grid2])
            s = grid_samples(colbsp.read(pk))
            if s >= GRID_MAX:
                sys.exit('cp77_collision: the hub\'s light grid is still %d samples' % s)
        info = {'pk3': str(pk.relative_to(self.part)), 'grid_samples': s, 'gridsize_again': grid2}
        if key is not None:                    # kept for the next run of the same inputs
            info.update({'key': key, 'sha256': sha256(pk)})
            tmp = cached.with_name(cached.name + '.tmp')
            if tmp.exists():
                shutil.rmtree(tmp)             # (an earlier attempt's half copy of this run's own files)
            tmp.mkdir(parents=True)
            shutil.copy2(pk, tmp / pk.name)
            (tmp / 'hub_compile.json').write_text(json.dumps(info, indent=1), encoding='utf-8')
            if not cached.exists():
                tmp.rename(cached)
            log('hub compile: kept as %s' % cached, self.logf)
        self.mark('hub_compile', info)

    def hub_finish(self):
        if self.done('hub'):
            return
        info = json.loads((self.state / 'hub_compile.done').read_text(encoding='utf-8'))
        out = self.part / ('zz_%s.pk3' % self.a.map)
        rep = put_spawns(self.part / info['pk3'], out, self.hub['spawns'])
        chk = hub_check(out)
        chk['spawn_injection'] = rep
        chk['grid'] = {k: grid_at(out, p) for k, p in self.hub.get('grid_points', {}).items()}
        for s in self.hub['spawns']:
            if s.get('name'):
                chk['grid'][s['name']] = grid_at(out, [float(v) for v in s['origin'].split()])
        spawn_corpus(self.hub['spawns'], self.part / 'spawns.txt')
        problems = []
        if chk['classes'].get('info_player_deathmatch') != len(self.hub['spawns']):
            problems.append('spawns %s, the data has %d' % (chk['classes'].get('info_player_deathmatch'),
                                                            len(self.hub['spawns'])))
        if len(chk['trigger_hurt']) != 1:
            problems.append('%d trigger_hurt' % len(chk['trigger_hurt']))
        if chk['grid_samples'] >= GRID_MAX:
            problems.append('light grid %d samples' % chk['grid_samples'])
        (self.part / 'hub' / 'check.json').write_text(json.dumps(chk, indent=1), encoding='utf-8')
        if problems:
            sys.exit('cp77_collision: the hub check failed: %s' % '; '.join(problems))
        log('hub: %s, %d spawns, trigger_hurt model %d, %d grid samples' % (
            out.name, len(chk['spawns']), chk['trigger_hurt'][0]['model'], chk['grid_samples']), self.logf)
        self.mark('hub', {'sha256': sha256(out), 'classes': chk['classes']})

    # the chunk compiles
    def tile_list(self, kill):
        man = json.loads((self.cdir / 'manifest.json').read_text(encoding='utf-8'))
        size = man['tile_size_m'] * lp.UNITS_PER_METRE
        lo, hi = kill['kill_q3_min'], kill['kill_q3_max']
        inside, outside = [], []
        for c in man['chunks']:
            o = lp.gltf_to_q3(np.array(c['offset_in_area_gltf_m'], np.float64))
            if np.any(np.abs(o - np.round(o)) > 1e-6):
                sys.exit('cp77_collision: chunk %s: origin %s is not whole units' % (c['tile'], o))
            o = np.round(o).astype(int)
            # the tile's square in the hub's frame: its origin is the tile's min corner (cm_load's 4,800 u grid)
            x0, y0, x1, y1 = o[0], o[1], o[0] + size, o[1] + size
            (outside if x1 <= lo[0] or x0 >= hi[0] or y1 <= lo[1] or y0 >= hi[1] else inside).append((c['tile'], o))
        if self.a.tiles:
            want = set(self.a.tiles.split(','))
            missing = want - {t for t, _ in inside}
            if missing:
                sys.exit('cp77_collision: --tiles not in the manifest or outside the ring: %s' % sorted(missing))
            inside = [(t, o) for t, o in inside if t in want]
        return man, inside, outside

    def tile_done(self, t):
        d = self.part / 'chunks' / t
        if self.a.slabs:
            return (d / ('chunk_%s.slabs.json' % t)).exists() or (d / 'empty_chunk.txt').exists()
        return (self.packs.packed(t) or (d / ('zz_chunk_%s.pk3' % t)).exists() or (d / 'empty_chunk.txt').exists())

    def compile_tile(self, t, w):
        if self.stop.is_set() or self.tile_done(t):
            return
        d = self.part / 'chunks' / t
        if d.exists():                         # stopped half way: moved aside, compiled again
            d.rename(self.part / 'stopped' / ('%s-%s' % (t, time.strftime('%Y%m%dT%H%M%S'))))
        if self.cache is not None:
            key = self.chunk_key(t)
            e = self.cache.hit(key)
            if e is None:
                why = self.cache.miss_reason(key, t, self.digests.get(t))
                self.misses[why] += 1
                log('tile %s: chunk cache miss: %s' % (t, why), self.logf)
            if e is not None:
                if e.get('empty'):
                    d.mkdir(parents=True, exist_ok=True)
                    (d / 'empty_chunk.txt').write_text('empty (the chunk cache: %s)\n' % key[:16], encoding='utf-8')
                else:
                    self.packs.add(t, Path(e['pack']), self.origin[t], keep=True)
                log('tile %s: reused from the chunk cache (%s, %s)' % (
                    t, key[:16], 'empty' if e.get('empty') else Path(e['pack']).parent.name), self.logf)
                self.reused += 1
                return
        peak = compile_peak_gb((self.cdir / ('%s.glb' % t)).stat().st_size, self.a.text)
        avail, commit, out = self.wait_start('tile %s' % t, peak)
        if self.a.text:
            scratch = self.part / 'scratch' / ('w%d' % w)
            scratch.mkdir(parents=True, exist_ok=True)
            how = ['--tile-parallel', self.a.tile_parallel, '--scratch', scratch]
        else:
            how = ['--cook'] + ([] if self.a.plain else ['--xbsz'])   # (no scratch: nothing but the tile's pack)
            if self.a.slabs:                   # the package, and the BSP cooked once for its hash (no pack)
                how += ['--package', d / ('chunk_%s.slabs' % t), '--no-pack']
        t0 = time.time()
        r = self.sh([PY, '-u', HERE / 'gltf2map.py', self.cdir / ('%s.glb' % t), 'chunk_%s' % t, '--out', d,
                     '--col-chunk', '--exact-planes', '--jobs', self.a.jobs, *how], self.part / 'logs' / ('%s.log' % t),
                    env={'Q3MAP2': str(self.q3map2_chunks)}, start='tile %s' % t)
        if r or not self.tile_done(t):
            self.stop.set()
            log('tile %s FAILED (exit %d): %s' % (t, r, self.part / 'logs' / ('%s.log' % t)), self.logf)
            return
        log('tile %s: %s in %.0f s (worker %d; at its start available %.1f GB, free commit %.1f GB, %.1f GB '
            'reserved by running compiles, its own peak taken as %.1f GB)' % (
                t, 'empty' if (d / 'empty_chunk.txt').exists() else 'compiled', time.time() - t0, w, avail, commit,
                out, peak), self.logf)
        if not (d / 'empty_chunk.txt').exists() and not self.a.slabs:
            self.packs.add(t, d / ('zz_chunk_%s.pk3' % t), self.origin[t])

    def compile_all(self, tiles):
        self.origin = dict(tiles)
        for t, o in tiles:                    # compiled by a stopped run, not yet packed
            pk = self.part / 'chunks' / t / ('zz_chunk_%s.pk3' % t)
            if not self.packs.packed(t) and pk.exists():
                self.packs.add(t, pk, o)
                log('tile %s: compiled before, packed now' % t, self.logf)
            elif not self.packs.packed(t) and t in self.packs.recovered:
                self.packs.add(t, self.packs.recovered[t], o, keep=True)
                log('tile %s: recovered from the open pack of the stopped run, packed now' % t, self.logf)
        left = [t for t, _ in tiles if not self.tile_done(t)]
        left.sort(key=lambda t: -(self.cdir / ('%s.glb' % t)).stat().st_size)
        log('compile: %d tiles, %d left, %d workers (--jobs %d --tile-parallel %d)' % (
            len(tiles), len(left), self.a.workers, self.a.jobs, self.a.tile_parallel), self.logf)
        queue, qlock = list(left), threading.Lock()
        errors = []

        def failed(what, e):
            # a thread drops a SystemExit without a word (threading's excepthook): free_ok's stop in a worker lost
            # its tile silently (a tile -4_-3 was never attempted, its worker gone). Every exit and
            # exception of a worker is logged, stops the starts and ends the run with its reason.
            msg = str(e).replace('cp77_collision: ', '', 1) if isinstance(e, SystemExit) else '%s: %s' % (
                type(e).__name__, e)
            log('%s stopped the run: %s' % (what, msg), self.logf)
            if not isinstance(e, SystemExit):
                with open(self.logf, 'a', encoding='utf-8') as f:
                    f.write(traceback.format_exc())
            errors.append('%s: %s' % (what, msg))
            self.stop.set()

        def worker(w):
            while not self.stop.is_set():
                with qlock:
                    if not queue:
                        return
                    t = queue.pop(0)
                try:
                    self.compile_tile(t, w)
                except BaseException as e:
                    failed('worker %d (tile %s)' % (w, t), e)
                    return

        def hub_worker():
            try:
                self.hub_compile()
            except BaseException as e:
                failed('the hub compile', e)
        ths = [threading.Thread(target=hub_worker)] + [threading.Thread(target=worker, args=(w,))
                                                       for w in range(self.a.workers)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        bad = [t for t, _ in tiles if not self.tile_done(t)]
        if errors or bad:
            sys.exit('cp77_collision: %s; a rerun resumes' % '; '.join(
                errors + (['%d tiles not compiled (%s)' % (len(bad), ', '.join(bad[:10]))] if bad else [])))

    def slab_pack(self, placed, note):
        """zz_<map>_colslabs.pk3: every placed chunk's package (stored: each is one zstd frame) and
        maps/<map>.colslabs.json (the placed tiles in the manifest's order with their origins, their brush counts, the
        sha256 and size of the BSP each expands to, the note the expansion's manifest carries); read back, then the
        tiles' package files removed"""
        import colpack
        pk = self.part / ('zz_%s_colslabs.pk3' % self.a.map)
        tmp = pk.with_suffix('.tmp')
        idx = {'map': self.a.map, 'note': note, 'placed': [[t, [int(v) for v in o]] for t, o in placed], 'brushes': {},
               'bsp': {}}
        files = []
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED) as z:
            for t, _ in placed:
                f = self.part / 'chunks' / t / ('chunk_%s.slabs' % t)
                blob = f.read_bytes()
                idx['brushes'][t] = colpack.brush_count(blob)
                idx['bsp'][t] = json.loads(f.with_name(f.name + '.json').read_text(encoding='utf-8'))
                z.writestr('maps/chunk_%s.slabs' % t, blob)
                files.append((f, len(blob)))
            z.writestr('maps/%s.colslabs.json' % self.a.map, json.dumps(idx, indent=1))
        with zipfile.ZipFile(tmp) as z:
            if z.testzip() is not None or any(z.getinfo('maps/chunk_%s.slabs' % t).file_size != n
                                              for (t, _), (_, n) in zip(placed, files)):
                sys.exit('cp77_collision: %s does not read back' % tmp)
        tmp.replace(pk)
        for f, _ in files:
            f.unlink()                         # (in the pack, read back)
            f.with_name(f.name + '.json').unlink()
        log('slab pack: %s, %d chunks, %.0f MB' % (pk.name, len(placed), pk.stat().st_size / 1e6), self.logf)
        return [pk]

    def main(self):
        a = self.a
        if self.out.exists():
            sys.exit('cp77_collision: %s exists (a collision build is made once)' % self.out)
        for sub in ('state', 'logs', 'hub', 'chunks', 'stopped') + (('scratch',) if a.text else ()):
            (self.part / sub).mkdir(parents=True, exist_ok=True)
        self.packs = PackWriter(self.part, a.map, self.logf)
        self.digests, self.digest_lock, self.reused = {}, threading.Lock(), 0
        self.cache = None if (a.no_chunk_cache or a.text or a.slabs) else ChunkCache()
        self.identity = compile_identity(self.q3map2_chunks, self.chunk_args())
        self.misses = collections.Counter()
        log('chunk cache: compile identity %s (gltf2map closure %d files, q3map2 %s, GameData %s, %s)' % (
            self.identity[:16], len(module_closure(HERE / 'gltf2map.py')), self.q3map2_chunks.parent.name,
            'present' if paths.GAMEDATA.is_dir() else 'none', ' '.join(self.chunk_args())), self.logf)
        t0 = time.time()
        log('== cp77_collision %s %s -> %s (q3map2: hub %s, chunks %s)' % (
            a.area, a.tiles_tag, self.out, self.q3map2, self.q3map2_chunks), self.logf)
        self.chunks()
        kill = self.hub_glb()
        man, tiles, outside = self.tile_list(kill)
        log('tiles: %d in the manifest, %d to compile, %d wholly outside the void kill\'s box (%s)' % (
            len(man['chunks']), len(tiles), len(outside), ', '.join(t for t, _ in outside) or 'none'), self.logf)
        if a.dry_run:
            return
        self.compile_all(tiles)
        self.hub_finish()
        placed = [(t, o) for t, o in tiles if not (self.part / 'chunks' / t / 'empty_chunk.txt').exists()]
        empty = [t for t, _ in tiles if (self.part / 'chunks' / t / 'empty_chunk.txt').exists()]
        note = '%s_collision-%s (cp77_collision.py, tiles tag %s%s)' % (
            a.map, a.tiles_tag, a.tiles_tag, ', a check: --tiles ' + a.tiles if a.tiles else '')
        if a.slabs:
            pks, size = self.slab_pack(placed, note), 0
        else:
            pks, size = self.packs.finish(placed, note)
            log('packs: %s, %d chunks, %.0f MB of BSP' % (', '.join(p.name for p in pks), len(placed), size / 1e6),
                self.logf)
        hub = json.loads((self.state / 'hub.done').read_text(encoding='utf-8'))
        rec = {
            'area': a.area, 'tiles_tag': a.tiles_tag, 'map': a.map, 'subset': a.tiles or None,
            'made': time.strftime('%Y-%m-%d %H:%M:%S'), 'seconds': round(time.time() - t0),
            'commit': subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=REPO, capture_output=True,
                                     text=True).stdout.strip(),
            'tools': {f: sha256(HERE / f) for f in ('cp77_collision.py', 'cp2gltf.py', 'gltf2map.py', 'cp77_tiles.py')},
            'q3map2': {'hub': {'path': str(self.q3map2), 'sha256': sha256(self.q3map2)},
                       'chunks': {'path': str(self.q3map2_chunks), 'sha256': sha256(self.q3map2_chunks)}},
            'hub_data': {'file': self.hub_data_file.name, 'sha256': sha256(self.hub_data_file)},
            'chunk_manifest': {'path': str(self.cdir / 'manifest.json'), 'sha256': sha256(self.cdir / 'manifest.json')},
            'tiles': {'placed': [t for t, _ in placed], 'empty': empty, 'outside': [t for t, _ in outside]},
            'chunk_path': 'slabs' if a.slabs else 'text' if a.text else 'cook',
            'chunk_cache': {'identity': getattr(self, 'identity', None), 'reused': getattr(self, 'reused', 0),
                            'misses': dict(getattr(self, 'misses', {}))},
            'hub': hub, 'kill_box': kill,
            'packs': {p.name: sha256(p) for p in pks + [self.part / ('zz_%s.pk3' % a.map)]},
        }
        (self.part / 'collision.json').write_text(json.dumps(rec, indent=1), encoding='utf-8')
        stray = [p for p in self.part.rglob('*.pk3') if p.parent != self.part]
        if stray:                              # the driver links <out>/*.pk3 only; the tiles' own packs stay below
            log('(%d tile and compile packs under chunks/ and hub/: not linked by the driver)' % len(stray), self.logf)
        self.part.rename(self.out)
        if not (a.text or a.slabs):     # (indexed whether or not this run read the cache: a cold build fills it)
            if self.cache is None:
                self.cache = ChunkCache()
            n = self.index_packs(tiles)
            log('chunk cache: %d tiles reused, %d indexed at %s' % (self.reused, n, self.out),
                self.out / 'collision.log')
        log('== done: %s in %.0f s' % (self.out, time.time() - t0), self.out / 'collision.log')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('area')
    ap.add_argument('tiles_tag')
    ap.add_argument('--map', default='cp_watson')
    ap.add_argument('--out', required=True, help='<build>/<map>_collision-<tag>: made once (via <out>.partial)')
    ap.add_argument('--q3map2', required=True, help='the hub\'s q3map2 (xenjk8: tools/levelport/q3map2\'s patches '
                                                    'with world_coords)')
    ap.add_argument('--q3map2-chunks', required=True,
                    help='the chunks\' q3map2 (xenjk9: the same without world_coords, with brush_bin.patch for the '
                         'cooked chunks; --text: xenjk7 does)')
    ap.add_argument('--workers', type=int, default=4,
                    help='chunk compiles at once (default 4, each start memory-gated; the hub runs beside)')
    ap.add_argument('--jobs', type=int, default=4, help='gltf2map --jobs per compile (default 4)')
    ap.add_argument('--tile-parallel', type=int, default=2,
                    help='--text: gltf2map --tile-parallel per compile (default 2)')
    ap.add_argument('--plain', action='store_true',
                    help='the cooked chunks as plain BSPs (deflated), not XBSZ (collision option C, the default)')
    ap.add_argument('--no-hub-cache', action='store_true',
                    help='compile the hub even when a hub of exactly these inputs is kept (.codex-research/levelport/'
                         'cache/hub)')
    ap.add_argument('--slabs', action='store_true',
                    help='collision option A: the chunks as slab packages (gltf2map --cook --package --no-pack) in '
                         'zz_<map>_colslabs.pk3, not BSPs; colexpand.py makes the chunk packs from it at install')
    ap.add_argument('--text', action='store_true',
                    help='the chunks by the .map text path (gltf2map without --cook, a scratch folder a worker), as '
                         'before cooker A')
    ap.add_argument('--idle', action='store_true', help='Idle priority, one worker, '
                                                        '--jobs 2 --tile-parallel 1')
    ap.add_argument('--tiles', default='', help='comma-separated: compile and pack only these (a check)')
    ap.add_argument('--clutter-drops', help="colclutter.py drops folder: those boxes left out of the chunk glbs (the "
                                            "road clutter rule, in the chunks step)")
    ap.add_argument('--no-chunk-cache', action='store_true',
                    help='compile every chunk even when the chunk cache holds its key (cache/chunks/index.json): '
                         'the cold check of a numbered kit; the build is still indexed')
    ap.add_argument('--seed-cache', metavar='BUILD',
                    help="index a finished collision build of this tiles tag under this run's compile identity "
                         '(only after a cold compile of some of its tiles matched its bytes), then stop')
    ap.add_argument('--dry-run', action='store_true', help='the chunk glbs and the hub glb, then the plan; no compile')
    a = ap.parse_args()
    if a.idle:
        import psutil
        psutil.Process().nice(psutil.IDLE_PRIORITY_CLASS if os.name == 'nt' else 19)
        a.workers, a.jobs, a.tile_parallel = 1, 2, 1
    r = Run(a)
    if a.seed_cache:
        r.seed(Path(a.seed_cache).resolve())
    else:
        r.main()


if __name__ == '__main__':
    import jobguard
    jobguard.kill_children_with_me()      # (its compiles and q3map2 end with it)
    main()
