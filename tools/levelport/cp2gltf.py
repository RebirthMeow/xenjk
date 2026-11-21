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
"""Cyberpunk 2077 world areas -> levelport glTF.

Stages, each writing into one work folder (default .codex-research/levelport/cp77/<area>):

  sectors   unbundle + serialise the streaming sectors (levels 0-3) that cover the area box, then `blocks`
  blocks    the streaming index (all.streamingblock, base and ep1): which quest/state variant ranges are on by default;
            scan keeps only those (without it every range, quest content included)
  scan      walk the sector JSON and write placements.json: one record per placed mesh instance
            (depot path, appearance, CP world matrix, node type, sector, debug name)
  fetch     uncook every mesh placed within --radius of the centre to glb (+ materials, PNG textures);
            --lods every LOD into meshes_lod/; --embedded the meshes embedded in the sectors (bended roads)
  decimate  Blender-collapse meshes to a render triangle budget (for the BSP-only build)
  build     <area>-<tag>.glb for gltf2map; --no-render makes the gameplay layer only (collision, entities)
  scene     the render layer: maps/<map>.xscn + images in zz_<map>_scene.pk3 (docs/levelport-scene.md)

CP space: metres, +Z up (right-handed). glTF out: metres, +Y up, (x, z, -y), as levelport expects.

Placement contracts follow the WolvenKit Blender add-on (importers/sector): nodeData Position/
Orientation/Scale place a node; worldTransformsBuffer entries are absolute world transforms;
cookedInstanceTransforms are local to each nodeData record. Shared transform buffers are found by
HandleId, also when another node points at them with HandleRefId.

Converted levels are for local play only (README).
"""
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
from contextlib import nullcontext
from pathlib import Path

import numpy as np

if __name__ == '__main__':      # (run as python -I, the script's own folder is not on the path)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltfio
import paths as lpaths
# the .xscn format and its writer live in xscn.py (game-independent); their names stay importable from here
from xscn import (SCENE_KIND, SCENE_VERSION, SCENE_VERT, SceneImages, dds_bytes, keep_coverage, mip_half,  # noqa: E402,F401
                  mip_half_cutout, pot_image, scene_box_section, scene_light_section, scene_shape_section,
                  snorm8, vertex_frame, xscn_blob, xscn_head_of, xscn_rebuild, xscn_sections)

HERE = Path(__file__).resolve().parent
# code the stages run without importing it, in their keys (stagekey.py): the lenient sector reader, the Blender
# decimation script
STAGEKEY_FILES = {'stage_sectors': ['wk_read_lenient.ps1'], 'stage_decimate': ['blender_decimate.py']}
REPO = HERE.parent.parent
# The game, WolvenKit and Blender: environment variables read when a stage needs them (paths.py); cp2gltf.GAME,
# .ARCHIVES, .WOLVENKIT, .DOTNET, .BLENDER and .PWSH resolve through __getattr__ below
def game():
    return lpaths.need('CP77_GAME')


def archives():
    g = game()
    return (g / 'archive' / 'pc' / 'content', g / 'archive' / 'pc' / 'ep1')


def wolvenkit_exe():
    return lpaths.need('WOLVENKIT_CLI')


def dotnet():
    return lpaths.need('WOLVENKIT_DOTNET')


def blender():
    return lpaths.need('BLENDER')


_LAZY_PATHS = {'GAME': game, 'ARCHIVES': archives, 'WOLVENKIT': wolvenkit_exe, 'DOTNET': dotnet, 'BLENDER': blender,
               'PWSH': lpaths.pwsh}     # (PowerShell 7.x on .NET 10 loads WolvenKit's DLLs)


def __getattr__(name):
    if name in _LAZY_PATHS:
        return _LAZY_PATHS[name]()
    raise AttributeError("module 'cp2gltf' has no attribute %r" % name)


# Named areas: centre (CP x, y) and half-size in metres.
AREAS = {
    # The market ring is ~40 m across, the roundabout road reaches ~80 m, its buildings ~130 m.
    'kabuki': {'centre': (-1185.0, 2052.0), 'half': 192.0, 'radius': 135.0},
    # Watson stress slice: the densest 640 m square of the district survey (x -1664..-1024,
    # y 1152..1792: 369 k placements, about 4x the Kabuki box), across the Kabuki / Little China border
    'watson_slice': {'centre': (-1344.0, 1472.0), 'half': 384.0, 'radius': 320.0},
    # Watson (docs/levelport-watson.md section 2): Kabuki, Little China and Northside, the near-empty cells, Arasaka
    # Waterfront and the rooms under -100 m left out. A 2,048 x 2,944 m box; what is in it is its 246 kept 128 m cells
    # (areas/watson_cells.json); the radius only reaches the box's corners
    'watson': {'centre': (-1280.0, 2240.0), 'half': (1024.0, 1472.0), 'radius': 1794.0,
               'cells': 'areas/watson_cells.json',
               # named spawns: the player's origin in CP2077 metres and a yaw in JKA degrees
               # (the game's frame: 72 faces Misty's door, 90 the Afterlife's sign; not CP2077's own yaw), on the
               # street outside each venue, checked clear with a floor under it
               'named_spawns': [('mistys_esoterica', (-1549.49, 1187.51, 17.07), 72),
                                ('afterlife', (-1474.0, 1045.01, 23.2), 90)]},
    # Watson and the next area as one map: Watson's cells and the 78 tiles south and
    # east (every 128 m cell of them). Watson's centre and frame are kept, so its
    # kits, spawns and patches stay valid; the far edge is 112.8 k u out (the engine's limit raised past 64 k). It
    # shares Watson's work folder (the fetched meshes, bakes and sectors): 'work'
    'night_city': {'centre': (-1280.0, 2240.0), 'half': (2048.0, 3008.0), 'radius': 3200.0,
                   'cells': 'areas/night_city_cells.json', 'work': 'watson',
                   # the scan's box (the cells' bounds) and the sectors' boxes: Watson's box and the new tiles in 10
                   # boxes (row runs; their bounding box is twice their area, the rest the bay and the badlands)
                   'box': (-2816.0, -768.0, 768.0, 3712.0),
                   'sector_boxes': [(-2304.0, 768.0, -256.0, 3712.0),
                                    (-2816, 0, 256, 512), (-2304, -768, -256, -256), (-2304, -256, 256, 0),
                                    (-2048, 512, -1792, 768), (-1536, 512, 512, 768), (-768, 768, 512, 1024),
                                    (-768, 1280, 512, 1536), (-768, 1024, 768, 1280), (-256, 1536, 256, 1792),
                                    (0, 1792, 256, 2048)],
                   'named_spawns': [('mistys_esoterica', (-1549.49, 1187.51, 17.07), 72),
                                    ('afterlife', (-1474.0, 1045.01, 23.2), 90)]},
    # night_city and the 28 cells its outline encloses (water strips Watson's density
    # survey left out, with the bridges over them) and El Coyote Cojo's block, x -1536..-1024, y -1152..-768 (whole
    # cells north of the world-128 limit, CP2077 y -1255 here). Two more sector boxes: the strip south of Kabuki
    # outside Watson's box, and the bar's block. 'half' (the water ring) is night_city's: the block's own water is left.
    # The radius reaches the block's far corner (768, -1152), 3,962 m out: night_city's 3,200 m cut the bar itself
    # (3,235 m) out of the first fetch and tiling
    'night_city_b': {'centre': (-1280.0, 2240.0), 'half': (2048.0, 3008.0), 'radius': 4000.0,
                     'cells': 'areas/night_city_b_cells.json', 'work': 'watson',
                     'box': (-2816.0, -1152.0, 768.0, 3712.0),
                     'sector_boxes': [(-2304.0, 768.0, -256.0, 3712.0),
                                      (-2816, 0, 256, 512), (-2304, -768, -256, -256), (-2304, -256, 256, 0),
                                      (-2048, 512, -1792, 768), (-1536, 512, 512, 768), (-768, 768, 512, 1024),
                                      (-768, 1280, 512, 1536), (-768, 1024, 768, 1280), (-256, 1536, 256, 1792),
                                      (0, 1792, 256, 2048), (-1792, 512, -1536, 768), (-1536, -1152, -1024, -768)],
                     'named_spawns': [('mistys_esoterica', (-1549.49, 1187.51, 17.07), 72),
                                      ('afterlife', (-1474.0, 1045.01, 23.2), 90)]},
}


def write_json(path, obj, **kw):
    """json.dump to a temporary name, renamed: a reader (or a run stopped mid-write) never sees half a file"""
    path = Path(path)
    tmp = path.with_name(path.name + '.tmp%d' % os.getpid())
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(obj, f, **kw)
    os.replace(tmp, path)


def work_dir(area):
    """An area's work folder (.codex-research/levelport/cp77/<area>, or the one it shares: AREAS 'work')."""
    return lpaths.LEVELPORT / 'cp77' / AREAS.get(area, {}).get('work', area)


# an area of cells (AREAS 'cells'), set by main(): (cell size, {(i, j)}, the floor height (m)); None: the radius alone
AREA_CELLS = None


def use_area(name):
    """Make in_area follow an area's own cells (or its radius alone)."""
    global AREA_CELLS
    f = AREAS[name].get('cells')
    if f is None:
        AREA_CELLS = None
        return
    d = json.load(open(HERE / f, encoding='utf-8'))
    size = float(d['cell_m'])
    AREA_CELLS = (size, {(int(np.floor(x / size)), int(np.floor(y / size))) for x, y in d['cells']},
                  float(d.get('z_min', -1e9)))


def in_area(x, y, centre, radius, z=None):
    """Whether a point (CP metres) belongs to the area: within radius of its centre and, for an area of cells, in one
    of them and not under its floor."""
    if math.hypot(x - centre[0], y - centre[1]) > radius:
        return False
    if AREA_CELLS is None:
        return True
    size, cells, z_min = AREA_CELLS
    if z is not None and z < z_min:
        return False
    return (int(np.floor(x / size)), int(np.floor(y / size))) in cells
SECTOR_M0 = 64.0              # exterior sector edge at level 0; level n is 64 * 2**n
# interior sectors are on a grid of half the exterior's: 32 * 2**n (measured from all.streamingblock's
# streaming boxes; tag 7 took the exterior grid and fetched interiors from the wrong places, 523 of Watson's missing)
SECTOR_M0_KIND = {'exterior': SECTOR_M0, 'interior': SECTOR_M0 / 2}

# Node types that place one render mesh per nodeData record.
NODE_MESH = ('worldStaticMeshNode', 'worldRotatingMeshNode', 'worldPhysicalDestructionNode',
             'worldBakedDestructionNode', 'worldDecorationMeshNode', 'worldMeshNode', 'worldClothMeshNode',
             'worldDynamicMeshNode', 'worldBendedMeshNode', 'worldTerrainMeshNode', 'worldAdvertisementNode')
SL = chr(92)                                                  # depot path separator
NODE_INSTANCED = 'worldInstancedMeshNode'                    # worldTransformsBuffer, absolute
NODE_DESTRUCTIBLE = 'worldInstancedDestructibleMeshNode'     # cookedInstanceTransforms, local to the node


def wolvenkit(*args, check=True):
    env = dict(os.environ, DOTNET_ROOT=str(dotnet()))
    r = subprocess.run([str(wolvenkit_exe()), *map(str, args)], env=env, capture_output=True, text=True, errors='replace')
    if r.returncode and check:
        sys.exit(f'WolvenKit {args[0]} failed ({r.returncode}):\n{r.stdout[-2000:]}{r.stderr[-2000:]}')
    if r.returncode:   # check=False: the caller handles it
        print(f'WolvenKit {args[0]} failed ({r.returncode}): {r.stderr.strip().splitlines()[0] if r.stderr.strip() else ""}'[:300],
              flush=True)
        return None
    return r.stdout


def area_box(name, half=None):
    a = AREAS[name]
    if half is None and a.get('box'):
        return tuple(a['box'])
    hx, hy = (half, half) if half else a['half'] if isinstance(a['half'], tuple) else (a['half'], a['half'])
    cx, cy = a['centre']
    return cx - hx, cy - hy, cx + hx, cy + hy


def _range_re(lo, hi):
    return '(' + '|'.join(str(i) for i in range(lo, hi + 1)) + ')'


def sector_regex(box, levels=(0, 1, 2, 3, 4, 5, 6, 7), margin=1, kind='exterior'):
    """One regex for the `kind` sectors (exterior, or interior: CP2077's enterable spaces, the same names on a grid
    half the size, SECTOR_M0_KIND) of the given levels overlapping box (plus a
    cell of margin at levels 0-3;
    above them a cell is 1-8 km). Levels 4-7 hold the big pieces too, not only far proxies: the crossroads decks
    (wlc_01_crossroad_02 at level 5), the tower cranes, terrain, overpass pillars (leaving them out left a
    hole in a junction and crane tops floating)."""
    parts = []
    for lv in levels:
        e = SECTOR_M0_KIND[kind] * 2 ** lv
        mg = margin if lv <= 3 else 0
        x0, y0 = math.floor(box[0] / e) - mg, math.floor(box[1] / e) - mg
        x1, y1 = math.floor(box[2] / e) + mg, math.floor(box[3] / e) + mg
        parts.append(f'{_range_re(x0, x1)}_{_range_re(y0, y1)}_-?\\d+_{lv}')
    # Night City only: other worlds reuse sector names (04_main_menu has exterior_-1_1_-1_3)
    return '03_night_city.*%s_(' % kind + '|'.join(parts) + ')\\.streamingsector$'


def stage_record_key(roots, extra, files=(), record=None):
    """A stage run's key (stage_kept): its code (stagekey over cp2gltf.py from roots), its arguments (extra, JSON) and
    the size and time of every file or folder it reads or writes (a folder's time moves when a file is added, removed
    or replaced into it: publish() and WolvenKit's exports replace)."""
    import stagekey
    h = hashlib.sha1(stagekey.code_key(Path(__file__).resolve(), roots, entry=None).encode())
    h.update(json.dumps(extra, sort_keys=True, default=str).encode())
    for f in files:
        try:
            st = os.stat(f)
            h.update(('%s %d %d\n' % (f, st.st_size, st.st_mtime_ns)).encode())
        except OSError:
            h.update(('%s -\n' % f).encode())
    return h.hexdigest()[:16]


def stage_kept(record, name, key):
    """Whether the stage's last finished run left this key in its record file (a no-change
    rerun of sectors, scan and fetch took about 12 min and rewrote outputs that downstream keys stat).
    CP2GLTF_RERUN=1 in the environment runs every stage whatever its record says."""
    if os.environ.get('CP2GLTF_RERUN'):
        return False
    try:
        return json.loads(Path(record).read_text(encoding='utf-8')).get(name) == key
    except (OSError, ValueError):
        return False


def stage_keep(record, name, key):
    """Record a finished stage run's key (written whole, then renamed)."""
    record = Path(record)
    try:
        d = json.loads(record.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        d = {}
    d[name] = key
    tmp = record.with_suffix('.tmp')
    tmp.write_text(json.dumps(d, indent=1, sort_keys=True), encoding='utf-8')
    os.replace(tmp, record)


def stage_sectors(work, box, kinds=('exterior',), boxes=None):
    """The sectors overlapping box, or each of boxes (AREAS 'sector_boxes': an area of cells whose bounding box is
    mostly outside them, the joined night_city's), unbundled and serialised."""
    raw, js = work / 'raw', work / 'json'
    rec, name = work / 'stage_records.json', 'sectors ' + ','.join(kinds)
    boxes = [tuple(b) for b in boxes] if boxes else [tuple(box)]

    def key():
        extra = {'box': list(box), 'kinds': list(kinds), 'regex': [sector_regex(box, kind=k) for k in kinds],
                 'identity': fetch_identity()}
        if boxes != [tuple(box)]:
            extra.update(boxes=[list(b) for b in boxes],
                         regex=[sector_regex(b, kind=k) for b in boxes for k in kinds])
        return stage_record_key(['stage_sectors'], extra,
                                [raw, work / 'raw_ep1', js, work / 'sectors_serialised.json', work / 'json_blocks'])
    if js.is_dir() and stage_kept(rec, name, key()):
        print('sectors: kept (the archives, WolvenKit, the box and the code unchanged since the last run)')
        return
    raw.mkdir(parents=True, exist_ok=True)
    js.mkdir(parents=True, exist_ok=True)
    # serialised only where new: an unbundle rewrites every matching file, and serialising all 3,489 again was most of
    # the stage while 498 were new. A sector keeps its json while its raw bytes are the
    # ones the json was made from (sectors_serialised.json; CP2077's archives never change) and WolvenKit is the same
    # build. Without a record, a json newer than its raw file counts as made from it: taken before the unbundle
    # rewrites the raw files.
    raw_ep1, js_ep1 = work / 'raw_ep1', work / 'json_ep1'
    man_f = work / 'sectors_serialised.json'
    # WolvenKit's build as the outer stage key sees it (its DLLs; the .exe is a launcher a DLL-only update leaves alone)
    wk = wolvenkit_exe()
    tool = ([[Path(f).name, Path(f).stat().st_size, int(Path(f).stat().st_mtime)]
             for f in sorted(Path(wk).parent.glob('WolvenKit*.dll'))] if wk.exists() else None)
    man = json.load(open(man_f, encoding='utf-8')) if man_f.exists() else {}
    # (an older manifest names the launcher alone: adopted once while it is unchanged)
    launcher = [wk.stat().st_size, int(wk.stat().st_mtime)] if wk.exists() else None
    made = man.get('files', {}) if man.get('tool') in (tool, launcher) else {}
    if not man:
        for f in sorted(list(raw.rglob('*.streamingsector')) + list(raw_ep1.glob('*.streamingsector'))):
            out = js / (('ep1-' if f.parent == raw_ep1 else '') + f.name + '.json')
            if out.exists() and out.stat().st_size > 0 and out.stat().st_mtime_ns >= f.stat().st_mtime_ns:
                made[out.name] = hashlib.sha1(f.read_bytes()).hexdigest()
    for b in boxes:
        for kind in kinds:
            wolvenkit('unbundle', *archives(), '-r', sector_regex(b, kind=kind), '-o', raw, '-v', 'Minimal')
    # Phantom Liberty adds sectors of the same names under default\ep1\ that stream beside the base ones (the Watson
    # slice has 7); serialising one folder tree flattens names, so they go apart and come back as ep1-<name>
    for f in list(raw.rglob('*.streamingsector')):
        if f.parent.name == 'ep1':
            raw_ep1.mkdir(exist_ok=True)
            os.replace(f, raw_ep1 / f.name)
    # (WolvenKit names TweakDB ids and physics presets only once its name table has loaded, so the same sector's json
    # holds either a name or a hash there: not a field the scan reads)
    every = sorted(list(raw.rglob('*.streamingsector')) + list(raw_ep1.glob('*.streamingsector')))
    todo, sha = [], {}
    for f in every:
        out = js / (('ep1-' if f.parent == raw_ep1 else '') + f.name + '.json')
        sha[out.name] = hashlib.sha1(f.read_bytes()).hexdigest()
        ok = out.exists() and out.stat().st_size > 0
        if not ok or made.get(out.name) != sha[out.name]:
            todo.append(f)
    print(f'sectors: {len(every)} unbundled, {len(todo)} to serialise (new or changed)', flush=True)
    # each one's json as it was: only a json this run rewrote counts as made from the new raw bytes (a
    # failed serialisation left the old, nonempty json, which was then recorded as current for good)
    js_of = lambda f: js / (('ep1-' if f.parent == raw_ep1 else '') + f.name + '.json')
    before = {f: (js_of(f).stat().st_mtime_ns if js_of(f).exists() else None) for f in todo}
    fresh = lambda f: js_of(f).exists() and js_of(f).stat().st_size > 0 and js_of(f).stat().st_mtime_ns != before[f]
    # the ones to serialise, hard-linked into a folder of their own (WolvenKit serialises a folder)
    stage = work / 'serialise_todo'
    for sub, fs in (('base', [f for f in todo if f.parent != raw_ep1]), ('ep1', [f for f in todo if f.parent == raw_ep1])):
        if not fs:
            continue
        d = stage / sub
        d.mkdir(parents=True, exist_ok=True)
        for f in fs:
            if not (d / f.name).exists():
                try:
                    os.link(f, d / f.name)
                except OSError:
                    shutil.copy2(f, d / f.name)
        if sub == 'base':
            wolvenkit('convert', 'serialize', d, '-o', js, '-v', 'Minimal', check=False)
        else:
            js_ep1.mkdir(exist_ok=True)
            wolvenkit('convert', 'serialize', d, '-o', js_ep1, '-v', 'Minimal', check=False)
            for f in js_ep1.glob('*.json'):
                os.replace(f, js / ('ep1-' + f.name))
        for f in fs:                      # (the links only: each file stays in raw/)
            q = d / f.name
            if q.exists() and q.stat().st_nlink > 1:
                os.unlink(q)
    # a sector the CLI refuses (WolvenKit 9.0.1: exterior_-17_21_0_0, an RTTI mismatch in AccessPointControllerPS:
    # it leaves an empty file) is read by WolvenKit's own reader with its parse errors handled (wk_read_lenient.ps1)
    for f in every:
        out = js / (('ep1-' if f.parent == raw_ep1 else '') + f.name + '.json')
        if not out.exists() or out.stat().st_size == 0 or (f in before and not fresh(f)):
            r = subprocess.run([lpaths.pwsh(), '-NoProfile', '-File', str(HERE / 'wk_read_lenient.ps1'), str(f), str(out),
                                str(wolvenkit_exe().parent)], capture_output=True, text=True, errors='replace')
            print(f'sectors: {out.name} read leniently ({"ok" if out.exists() and out.stat().st_size else "FAILED"}): '
                  + ' / '.join(l.strip() for l in r.stdout.splitlines() if l.strip().startswith(('read:', '1 x', '2 x', '3 x'))))
    stale = []
    for f in todo:
        out = js_of(f)
        if fresh(f):
            made[out.name] = sha[out.name]
        else:
            made.pop(out.name, None)       # retried next run; its old json, if any, is not certified
            stale.append(out.name)
    # CP2077 2.5's layout (WolvenKit 10: compiled records, cooked nodes) rewritten in the 2.31 one the readers know: the
    # sectors serialised now, and every earlier one rewritten by an older cp77_sector25 (convert_file adds the fixes
    # its version lacks; a 2.31 or current file is left as it is)
    import cp77_sector25
    conv = {}
    for f in every:
        if (fresh(f) if f in before else js_of(f).exists() and js_of(f).stat().st_size > 0):
            for k, v in cp77_sector25.convert_file(js_of(f)).items():
                conv[k] = conv.get(k, 0) + v
    if conv:
        print('sectors: rewritten from the 2.5 layout (cp77_sector25): ' + ', '.join(
            '%s %d' % kv for kv in sorted(conv.items())), flush=True)
    if stale:
        print('sectors: %d not serialised this run, left to retry: %s' % (len(stale), ', '.join(stale[:8])), flush=True)
    tmp = man_f.with_suffix('.tmp')
    tmp.write_text(json.dumps({'tool': tool, 'files': made}, indent=0, sort_keys=True), encoding='utf-8')
    os.replace(tmp, man_f)
    n = len(list(js.glob('*.streamingsector.json')))
    print(f'sectors: {n} serialised in {js} ({len(todo)} this run)')
    stage_blocks(work)
    if not stale:           # (a run that left sectors to retry is not recorded as done)
        stage_keep(rec, name, key())


# The streaming index, one per archive (ep1's sector copies override base's, so its descriptors win). Each descriptor
# of a sector with quest or state variants lists them: a name, the sector's record range it switches (rangeIndex into
# the sector's variantIndices; range 0 is always on) and enabledByDefault. Converting
# every range put content the game hides by default into the level (prologue and road blockades, cyberspace scenes,
# quest props, bodies: 787 placements and 177 entities in the Kabuki block).
BLOCK_REGEX = r'03_night_city\\_compiled\\default\\blocks\\all\.streamingblock$'   # the city's
# Phantom Liberty's own sectors (default\ep1\<name>, our ep1-<name>) are described in ep1's second block, of the same
# file name. Without it every range of theirs was drawn: 3,791 Watson records, the q307 ending's Zetatech front over
# Misty's, courier rewards, cutscene props
EP1_BLOCK_REGEX = r'03_night_city\\_compiled\\default\\ep1\\blocks\\all\.streamingblock$'
BLOCK_DIRS = ['content', 'ep1', 'ep1q']      # json_blocks/<dir>, in the order sector_variants reads


def stage_blocks(work):
    arcs = archives()
    for arc, d, rx in [(arc, arc.name, BLOCK_REGEX) for arc in arcs] + [(arcs[1], 'ep1q', EP1_BLOCK_REGEX)]:
        raw, js = work / 'raw_blocks' / d, work / 'json_blocks' / d
        raw.mkdir(parents=True, exist_ok=True)
        js.mkdir(parents=True, exist_ok=True)
        wolvenkit('unbundle', arc, '-r', rx, '-o', raw, '-v', 'Minimal')
        wolvenkit('convert', 'serialize', raw, '-o', js, '-v', 'Minimal')
    print('blocks: ' + ', '.join(f'{d.name} {len(list(d.rglob("*.json")))}' for d in (work / 'json_blocks').iterdir()))


def sector_variants(work):
    """{sector name: (set of enabled range indices, {range index: variant name})} for sectors with variants; None
    when the blocks were never fetched (stage_blocks), so every record is kept."""
    root = work / 'json_blocks'
    if not root.is_dir():
        return None
    table = {}
    for sub in BLOCK_DIRS:   # base, then ep1 over it, then ep1's own sectors
        for f in sorted((root / sub).rglob('*.json')):
            for d in json.load(open(f, encoding='utf-8'))['Data']['RootChunk']['descriptors']:
                if not d.get('variants'):
                    continue
                path = d['data']['DepotPath']['$value'].split(SL)
                name = ('ep1-' if path[-2] == 'ep1' else '') + path[-1].split('.')[0]   # as stage_sectors names them
                on, names = {0}, {}
                for v in d['variants']:
                    names[v['rangeIndex']] = cname(v.get('name'))
                    if v.get('enabledByDefault'):
                        on.add(v['rangeIndex'])
                table[name] = (on, names)
    return table


# ---------------------------------------------------------------------------------------------------- scan

def cname(v):
    if isinstance(v, dict):
        return v.get('$value') or ''
    return v or ''


def cname_hash(name):
    """a CName's hash, as CP2077 stores the names a resource leaves unnamed (FNV-1a 64 of the UTF-8 bytes)"""
    h = 0xcbf29ce484222325
    for b in name.encode('utf-8'):
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def depot(v):
    if isinstance(v, dict):
        return cname(v.get('DepotPath'))
    return ''


def quat_rot(q):
    """CP quaternion (i, j, k, r) -> 3x3 rotation."""
    x, y, z, w = (float(q.get(k, 0.0)) for k in ('i', 'j', 'k', 'r'))
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(pos, quat, scale):
    m = np.eye(4)
    s = [float(scale.get(k, 1.0)) for k in ('X', 'Y', 'Z')] if scale else [1.0, 1.0, 1.0]
    m[:3, :3] = quat_rot(quat or {'r': 1.0}) * np.array(s)[None, :]
    m[:3, 3] = [float(pos.get(k, 0.0)) for k in ('X', 'Y', 'Z')]
    return m


def shared_buffers(root):
    """HandleId -> Transforms list for every shared data buffer in the sector."""
    found = {}

    def walk(o):
        if isinstance(o, dict):
            hid = o.get('HandleId')
            if hid is not None and isinstance(o.get('Data'), dict):
                t = o['Data'].get('buffer', {})
                t = t.get('Data', {}) if isinstance(t, dict) else {}
                if isinstance(t, dict) and isinstance(t.get('Transforms'), list):
                    found[str(hid)] = t['Transforms']
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(root)
    return found


def buffer_slice(data, key, buffers):
    owner = data.get(key) or {}
    shared = owner.get('sharedDataBuffer') or {}
    if 'Data' in shared:
        t = shared.get('Data', {}).get('buffer', {}).get('Data', {}).get('Transforms', [])
    else:
        t = buffers.get(str(shared.get('HandleRefId')), [])
    start, count = int(owner.get('startIndex', 0) or 0), int(owner.get('numElements', 0) or 0)
    return t[start:start + count]


AD_VARIANTS = 8                     # stand-in ads: each placement shows one of this many per material
_AD_MAPS = {}


def ad_map(work):
    """cp77_ads.AdMap for the work folder, once per process"""
    if work not in _AD_MAPS:
        import cp77_ads
        _AD_MAPS[work] = cp77_ads.AdMap(work)
    return _AD_MAPS[work]


_ROT_MAPS = {}


def rot_map(work):
    """cp77_rotate.RotMap for the work folder, once per process"""
    if work not in _ROT_MAPS:
        import cp77_rotate
        _ROT_MAPS[work] = cp77_rotate.RotMap(work)
    return _ROT_MAPS[work]
AD_LIGHT_COLOUR = (220, 225, 235)   # ad lights take their colour from the ad's video at run time: a stand-in


def light_rec(src, m, sector, name, kind='static', colour=None):
    """A light from any of CP's light records (worldStaticLightNode, gameLightComponent, worldAdvertisementLightData),
    placed by the world matrix m."""
    c = src.get('color') or {}
    return {'pos': [round(float(v), 4) for v in m[:3, 3]], 'rot': [round(float(v), 5) for v in m[:3, :3].ravel()],
            'color': list(colour or (c.get('Red', 255), c.get('Green', 255), c.get('Blue', 255))),
            'type': src.get('type'), 'radius': float(src.get('radius') or 0),
            # None when the record gives none (scene_lights takes 1 then); an explicit 0 stays 0, a light switched off
            'intensity': None if src.get('intensity') is None else float(src['intensity']), 'EV': float(src.get('EV') or 0),
            'outer': float(src.get('outerAngle') or 0), 'inner': float(src.get('innerAngle') or 0),
            'diffuse': float(src.get('sceneDiffuse', 1)), 'specular': float(src.get('sceneSpecularScale', 100)),
            'channel': str(src.get('lightChannel')), 'unit': src.get('unit'),
            'source_radius': float(src.get('sourceRadius') or 0), 'capsule': float(src.get('capsuleLength') or 0),
            # light components (lamps, spotlights, signs) almost all carry useInFog 0: none of their light in the haze,
            # whatever scaleVolFog says; static light nodes have no such field
            'shadows': int(src.get('enableLocalShadows') or 0),
            'volfog': float(src.get('scaleVolFog', 100)) / 100.0 if src.get('useInFog', 1) else 0.0,
            # CP's own distances (metres; 0 = none given): the light off beyond autoHideDistance, its shadow faded out
            # over shadowFadeRange past shadowFadeDistance; allowDistantLight (a far stand-in in CP) kept as a flag
            'hide': float(src.get('autoHideDistance') or 0), 'shadow_fade': float(src.get('shadowFadeDistance') or 0),
            'shadow_range': float(src.get('shadowFadeRange') or 0), 'distant': int(src.get('allowDistantLight') or 0),
            'name': name, 'kind': kind, 'sector': sector}


def black(src):
    c = src.get('color') or {}
    return not (c.get('Red') or c.get('Green') or c.get('Blue'))


def fixed_point(p):
    """WorldPosition (FixedPoint Bits, 1/131072 m) -> {'X', 'Y', 'Z'}."""
    return {k.upper(): float((p.get(k) or {}).get('Bits', 0)) / 131072.0 for k in 'xyz'}


def component_space(chunks):
    """A function giving each of an entity's components (a template's, its appearance's or an instance's component
    list) its matrix in the entity's space, through its chain of hard transform bindings (by bind name)."""
    handles, by_name = {}, {}

    def index(o):
        if isinstance(o, dict):
            if 'HandleId' in o and isinstance(o.get('Data'), dict):
                handles[str(o['HandleId'])] = o['Data']
            for v in o.values():
                index(v)
        elif isinstance(o, list):
            for v in o:
                index(v)
    index(chunks)
    for c in chunks:
        if isinstance(c, dict) and cname(c.get('name')):
            by_name.setdefault(cname(c.get('name')), c)

    def local(c, depth=0):
        lt = c.get('localTransform') or {}
        m = trs(fixed_point(lt.get('Position') or {}), lt.get('Orientation'), None)
        par = c.get('parentTransform') or {}
        par = par.get('Data') or handles.get(str(par.get('HandleRefId'))) or {}
        up = by_name.get(cname(par.get('bindName')))
        if up is not None and up is not c and depth < 8:
            m = local(up, depth + 1) @ m
        return m
    return local


def component_lights(chunks, base, sector, name, kind, keep=None, local=None):
    """The enabled, coloured light components of one entity, placed onto base (the entity's world matrix).
    emissiveOnly lights only drive a mesh's glow (indicator LEDs, screens) and are left out. keep(component name):
    False leaves one out (the traffic lights' unlit lamps). local: component_space(chunks), when the caller has it."""
    local = local or component_space(chunks)
    out = []
    for c in chunks:
        if isinstance(c, dict) and c.get('$type') in ('gameLightComponent', 'entLightComponent'):
            if not c.get('isEnabled', 1) or black(c) or c.get('emissiveOnly'):
                continue
            if keep is not None and not keep(cname(c.get('name'))):
                continue
            out.append(light_rec(c, base @ local(c), sector, '%s/%s' % (name, cname(c.get('name'))), kind))
    return out


MESH_COMPONENTS = ('entMeshComponent', 'entPhysicalMeshComponent',
                   'entPhysicalDestructionComponent',	# destructible devices' intact mesh (the traffic lights' poles)
                   # skinned devices at their bind pose, the default state: the gates' roll-up doors (without them openings
                   # had no shutter)
                   'entSkinnedMeshComponent', 'entPhysicalSkinnedMeshComponent')
# run-time UI the game draws into a mesh (a gate's number plate: StreetSignWidgetComponent): no stand-in ad there
UI_ONLY_COMPONENTS = re.compile(r'^NumberPlateScreenMesh', re.I)
# people: heads, hair, garments, bodies and corpses an entity carries are the game's to spawn and animate; placed as
# scenery they stood as statues at their bind pose (619 in one scene)
CHARACTER_MESHES = 'base' + chr(92) + 'characters' + chr(92)
CHUNKS_ALL = (2 ** 63 - 1, 2 ** 64 - 1)
# doors, gates and shutters (interiors: decorative doors and lifts closed, the rest open if something is
# behind them, decorative shutters closed): their template names classify them. Decorative doors and gates, lifts and
# window shutters stay as CP2077 places them; every other one's leaf (the sliding panel, its handle, a roll-up door,
# a freight or gate leaf) is a candidate, opened by the collision build where both sides are clear (door_test) and
# left out of the scene only then (the build's door_open record)
DOOR_FAMILY = re.compile(r'(?i)(door|gate|shutter)[^\\]*\.ent$')
DOOR_CLOSED = re.compile(r'(?i)(decorative|lift|elevator|shutter_window|industrial_shutters)[^\\]*\.ent$')
DOOR_LEAF = re.compile(r'(?i)^(mesh|handle|door_mesh|door_sliding_[a-z0-9]+|\w*rollup_door_a|\w*shutter_a_door\w*)_?\d*$')
DOOR_CLEAR, DOOR_DROP = 0.6, 1.6        # m clear beyond the leaf on each side, and the floor within this below


def door_leaf(p):
    """A candidate door's leaf component: its key (sector and entity), else None."""
    t = p.get('ent_template')
    if not t or DOOR_CLOSED.search(t) or not DOOR_LEAF.match(p['name'].rsplit('/', 1)[-1]):
        return None
    return '%s|%s|%s' % (p['sector'], p['name'].rsplit('/', 1)[0], ','.join('%g' % v for v in p.get('ent_pos') or ()))


def door_open_keys(work, tag):
    """The doors the tag's collision build opened (every tile part's door_open), so the scene leaves out the same
    leaves; empty before any build with doors (the scene is then as before)."""
    out = set()
    for f in sorted((tile_dir(work, tag) / 'build').glob('*.npz')) if (tile_dir(work, tag) / 'build').is_dir() else ():
        with np.load(f, allow_pickle=True) as z:
            if 'door_open' in z.files:
                out.update(json.loads(str(z['door_open'])))
    return out


def _seg_hits(T, a, b):
    """Whether segment a->b crosses any triangle of T (k, 3, 3) (Moller-Trumbore, both windings)."""
    if not len(T):
        return False
    d = b - a
    e1, e2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
    pv = np.cross(d, e2)
    det = np.einsum('ij,ij->i', e1, pv)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    tv = a - T[:, 0]
    u = np.einsum('ij,ij->i', tv, pv) * inv
    qv = np.cross(tv, e1)
    v = (qv @ d) * inv
    t = np.einsum('ij,ij->i', e2, qv) * inv
    return bool(np.any(ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t >= 0) & (t <= 1)))


def door_test(T, tmin, tmax, boxes, leaves):
    """Whether a door opens onto space: both sides of its leaves (tile glTF frame, y up, metres) clear for DOOR_CLEAR
    beyond the leaf at a person's waist, with an up-facing floor (a triangle or a box top) within DOOR_DROP below.
    T: the tile's collision triangles without the candidate leaves, tmin/tmax their bounds; boxes: (depot, lo, hi, G).
    Returns (open, reason)."""
    C = []
    for p, ml, G in leaves:
        lo, hi = ml.lo, ml.hi
        c = np.array([[x, y, z, 1.0] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
        C.append((c @ G.T)[:, :3])
    C = np.concatenate(C)
    # the facing from the largest leaf piece (a handle's thinnest axis can be vertical)
    p0, ml0, G0 = max(leaves, key=lambda q: float(np.prod(np.maximum(q[1].hi - q[1].lo, 1e-3)) *
                                                 abs(np.linalg.det(q[2][:3, :3]))))
    e = ml0.hi - ml0.lo
    n = G0[:3, :3] @ np.eye(3)[int(np.argmin(e))]
    n[1] = 0.0                          # (the horizontal through the leaf's thinnest axis)
    if np.linalg.norm(n) < 1e-6:
        return False, 'no horizontal normal'
    n /= np.linalg.norm(n)
    c = C.mean(0)
    half = 0.5 * float(np.ptp(C @ n))
    y = float(C[:, 1].min()) + min(1.0, 0.5 * float(np.ptp(C[:, 1])))
    reach = half + DOOR_CLEAR + 0.5
    near = np.all((tmax >= c - reach - DOOR_DROP) & (tmin <= c + reach + DOOR_DROP), axis=1)
    Tn = T[near]
    up = Tn[np.abs(np.cross(Tn[:, 1] - Tn[:, 0], Tn[:, 2] - Tn[:, 0])[:, 1]) >
            0.7 * np.linalg.norm(np.cross(Tn[:, 1] - Tn[:, 0], Tn[:, 2] - Tn[:, 0]), axis=1)] if len(Tn) else Tn
    # (a placement scaled to nothing on an axis has no inverse and no volume: never a floor; tag 9's fringe tile
    # -10_1 has one)
    bx = [(lo, hi, np.linalg.inv(G)) for _, lo, hi, G in boxes
          if np.linalg.norm(G[:3, 3] - c) < reach + 30.0 and abs(np.linalg.det(G[:3, :3])) > 1e-9]

    def in_box(q):
        for lo, hi, Gi in bx:
            l = Gi[:3, :3] @ q + Gi[:3, 3]
            if np.all(l >= lo - 1e-3) and np.all(l <= hi + 1e-3):
                return True
        return False

    def seg_box(p, q):
        # the segment p->q meets a box (slabs in its own frame), a flat one too: a floor plate can be a box of no
        # thickness (maelstrom_office_ground_w300_l300_a; sampling every 0.1 m missed it and kept Vic's door shut)
        for lo, hi, Gi in bx:
            l0, l1 = Gi[:3, :3] @ p + Gi[:3, 3], Gi[:3, :3] @ q + Gi[:3, 3]
            d, t0, t1 = l1 - l0, 0.0, 1.0
            for ax in range(3):
                if abs(d[ax]) < 1e-12:
                    if not (lo[ax] - 1e-3 <= l0[ax] <= hi[ax] + 1e-3):
                        break
                    continue
                u0, u1 = (lo[ax] - 1e-3 - l0[ax]) / d[ax], (hi[ax] + 1e-3 - l0[ax]) / d[ax]
                t0, t1 = max(t0, min(u0, u1)), min(t1, max(u0, u1))
                if t0 > t1:
                    break
            else:
                return True
        return False
    for s in (1.0, -1.0):
        a = np.array([c[0], y, c[2]]) + n * s * (half + 0.1)
        b = np.array([c[0], y, c[2]]) + n * s * (half + DOOR_CLEAR)
        if _seg_hits(Tn, a, b) or in_box(b):
            return False, 'blocked'
        f = b - np.array([0.0, DOOR_DROP, 0.0])
        if not (_seg_hits(up, b, f) or seg_box(b, f)):
            return False, 'no floor'
    return True, 'open'


def component_meshes(chunks, base, sector, name):
    """The enabled mesh components of one entity (doors, lamp fixtures, vending machines, signs, traffic heads) as
    placements like the sectors' (type 'entity'): placed onto base with their visual scale; a chunk mask that hides
    some chunks rides along (chunk_mask), one that hides all leaves the component out."""
    local = component_space(chunks)
    out = []
    # a signal's lamps: each light component names the material zone of the face it lights (signal heads)
    zones = {str(ZONE_INDEX[str(c['materialZone'])]): cname(c.get('name')) for c in chunks if isinstance(c, dict)
             and c.get('$type') in ('gameLightComponent', 'entLightComponent') and str(c.get('materialZone')) in ZONE_INDEX
             and cname(c.get('name')) in TRAFFIC_LAMPS}
    for c in chunks:
        if not isinstance(c, dict) or c.get('$type') not in MESH_COMPONENTS or not c.get('isEnabled', 1):
            continue
        mesh = depot(c.get('mesh'))
        mask = int(c.get('chunkMask', CHUNKS_ALL[0]) or 0)
        if not mesh or not mask or UI_ONLY_COMPONENTS.search(cname(c.get('name')) or ''):
            continue
        if mesh.lower().startswith(CHARACTER_MESHES):
            continue
        vs = c.get('visualScale') or {}
        m = base @ local(c) @ np.diag([float(vs.get('X', 1)), float(vs.get('Y', 1)), float(vs.get('Z', 1)), 1.0])
        rec = {'mesh': mesh, 'app': cname(c.get('meshAppearance')) or 'default', 'type': 'entity', 'sector': sector,
               'name': '%s/%s' % (name, cname(c.get('name'))), 'm': [round(float(v), 5) for v in m[:3].ravel()]}
        if mask not in CHUNKS_ALL:
            rec['chunk_mask'] = mask
        if zones:
            rec['zones'] = zones
            rec['ent'] = [round(float(v), 4) for v in base[:3, 3]]
        out.append(rec)
    return out


def instance_mesh_overrides(inst):
    """A placed entity's per-instance changes to its template's mesh components, {template component id: fields}:
    the instance data's chunks matched to template components through its CruidDict (chunk index -> component id).
    Kept: visualScale, isEnabled, chunkMask, meshAppearance, mesh. A stall's food-ad monitor is a 4.6 m billboard
    template scaled to 0.16 here; read from the template alone it stood full size in the market; 47 of Kabuki's 63 instance mesh overrides change the scale."""
    cru = inst.get('CruidDict') or {}
    out = {}
    for i, c in enumerate(inst.get('Chunks') or []):
        if not isinstance(c, dict) or c.get('$type') not in MESH_COMPONENTS or str(i) not in cru:
            continue
        o = {}
        vs = c.get('visualScale') or {}
        if any(abs(float(vs.get(k, 1)) - 1.0) > 1e-4 for k in 'XYZ'):
            o['visualScale'] = {k: float(vs.get(k, 1)) for k in 'XYZ'}
        if not c.get('isEnabled', 1):
            o['isEnabled'] = 0
        if c.get('chunkMask') is not None and int(c['chunkMask']) not in CHUNKS_ALL:
            o['chunkMask'] = c['chunkMask']
        if cname(c.get('meshAppearance')) not in (None, '', 'default'):
            o['meshAppearance'] = c['meshAppearance']
        if depot(c.get('mesh')) not in ('', '0'):	# (an instance chunk's unset mesh is the uint64 0)
            o['mesh'] = c['mesh']
        if o:
            out[str(cru[str(i)])] = o
    return out


def template_chunks(root):
    """A serialised entity template's components: its compiled data package, else its component list."""
    cd = ((root.get('compiledData') or {}).get('Data') or {}).get('Chunks')
    return cd if cd else root.get('components') or []


def default_records(records, rc, variant, stats):
    """The records the game shows by default: range 0 and the enabled-by-default variant ranges (sector_variants)."""
    vi = list(rc.get('variantIndices') or [])
    if variant is None or not vi:
        return records
    on, names = variant
    bounds = vi + [len(records)]
    if max(names, default=0) >= len(bounds) - 1:   # the descriptor does not fit this sector's ranges: keep them all
        stats['variant_mismatch'] = stats.get('variant_mismatch', 0) + 1
        return records
    kept = list(records[:bounds[0]])   # before the first range (none in the sectors seen): always on
    for i in range(len(bounds) - 1):
        part = records[bounds[i]:bounds[i + 1]]
        if i in on:
            kept += part
        else:
            stats['variant_records_off'] = stats.get('variant_records_off', 0) + len(part)
    return kept


_SHOWN_VARIANTS = {}


def shown_records(work, sector, rc, stats=None):
    """A sector's records the game shows by default: default_records with the work folder's streaming blocks. Every
    reader of sector records goes through it, so variant-off content stays out of every output alike (the effects, foliage, rotation, ads and haze readers once
    took every range)."""
    key = str(work)
    if key not in _SHOWN_VARIANTS:
        _SHOWN_VARIANTS[key] = sector_variants(Path(work)) or {}
    return default_records(rc['nodeData']['Data'], rc, _SHOWN_VARIANTS[key].get(sector), {} if stats is None else stats)


def instanced_hides(n):
    """A worldInstancedMeshNode's hide distance per transform: its buffer holds, per group, a box (Unknown1), the
    group's forceAutoHideDistance (Unknown2) and its transform count (Unknown3), the groups in transform order
    (from the buffer layout); [] when the buffer does not say"""
    b = n.get('buffer') or {}
    d, c = b.get('Unknown2') or [], b.get('Unknown3') or []
    if len(d) != len(c):
        return []
    out = []
    for dist, cnt in zip(d, c):
        out += [float(dist or 0)] * int(cnt or 0)
    return out


# proxies the scene draws otherwise (proxy_placements leaves them out): CP2077's distant water patches
STANDIN_PROXY = re.compile(r'(?i)/global_ocean_patch_\d+x\d+_proxy\.mesh$')
# placeholder materials given a setup in this process (MeshLods: cp77mat.placeholder_setups' counts), for the run's logs
PLACEHOLDERS = {'meshes': 0, 'materials': 0}
PROXY_TYPES = ('worldGenericProxyMeshNode', 'worldBuildingProxyMeshNode', 'worldRoadProxyMeshNode',
               'worldEntityProxyMeshNode', 'worldDestructibleEntityProxyMeshNode')


def proxy_rec(n, t, m, sector):
    """A proxy (the far city's stand-in mesh) as a record: its mesh and placement, the distances it shows between
    (near: generic/building/road proxies' nearAutoHideDistance, entity proxies' entityAttachDistance; far:
    forceAutoHideDistance), what it stands for (nbNodesUnderProxy nodes of sourcePrefabHash, or the entity
    ownerGlobalId) -- metres, CP's frame"""
    own = n.get('ownerGlobalId') or {}
    rec = {'mesh': depot(n.get('mesh')), 'app': cname(n.get('meshAppearance')) or 'default', 'type': t,
           'sector': sector, 'name': cname(n.get('debugName')), 'm': [round(float(v), 5) for v in m[:3].ravel()],
           'near': round(float(n.get('nearAutoHideDistance') or n.get('entityAttachDistance') or 0), 3),
           'far': round(float(n.get('forceAutoHideDistance') or 0), 3),
           'count': int(n.get('nbNodesUnderProxy') or 0), 'prefab': str(n.get('sourcePrefabHash') or '0')}
    h = own.get('hash') if isinstance(own, dict) else None
    if h:
        rec['owner'] = str(h)
    return rec


def stream_of(r):
    """A node record's streaming distance (m; WolvenKit's UkFloat1): CP2077 unloads the record past it from its
    streaming reference point, whatever the node's forceAutoHideDistance (cp2077_city_tech.md, "Per-node streaming
    distances"; bounded by the level: 128, 256, 512, 1,024 m for levels 0-3, km-scale above)"""
    try:
        return max(0.0, float(r.get('UkFloat1') or 0))
    except (TypeError, ValueError):
        return 0.0


def draw_distance(p, with_proxies=True):
    """How far a placement is drawn (m; 0 = no limit): its hide distance, capped by its record's streaming distance.
    Detail kept to its hide alone outlived the proxy that replaces it (a building
    proxy's shell drawn over a poster at 119 m, inside the detail's 1.8 km hide but past its 171 m streaming
    distance). An entity's component with no hide of its own hides where its entity proxy takes over (proxy_hide,
    fetch --ents), but only in a scene that draws the proxies (otherwise it vanished unreplaced)."""
    h = float(p.get('hide') or 0) or (float(p.get('proxy_hide') or 0) if with_proxies else 0.0)
    s = float(p.get('stream') or 0)
    return min(h, s) if h > 0 and s > 0 else (h or s)


def scan_sector(path, box, variant=None, stats=None, water=None, proxies=None):
    whole = json.load(open(path, encoding='utf-8'))['Data']
    rc = whole['RootChunk']
    nodes, records = rc['nodes'], rc['nodeData']['Data']
    records = default_records(records, rc, variant, {} if stats is None else stats)
    embedded = {cname(e.get('FileName')): e.get('Content') for e in whole.get('EmbeddedFiles') or []}
    buffers = shared_buffers(rc)
    sector = path.name.split('.')[0]
    by_node = {}
    for r in records:
        by_node.setdefault(r['NodeIndex'], []).append(r)
    out, skipped = [], {}

    def inside(m):
        x, y = m[0, 3], m[1, 3]
        return box[0] <= x <= box[2] and box[1] <= y <= box[3]

    def emit(kind, n, m, extra=None, hide=None, stream=0.0):
        if not inside(m):
            return
        rec = {'mesh': depot(n.get('mesh')) or depot(n.get('meshRef')), 'app': cname(n.get('meshAppearance')) or 'default',
               'type': kind, 'sector': sector, 'name': cname(n.get('debugName')),
               'm': [round(float(v), 5) for v in m[:3].ravel()]}
        h = float(n.get('forceAutoHideDistance') or 0) if hide is None else hide
        if h > 0:
            rec['hide'] = round(h, 3)       # CP's distance (m) past which the placement is not drawn
        if stream > 0:
            rec['stream'] = round(stream, 3)    # and past which its record unloads (stream_of): see draw_distance
        pf = str(n.get('sourcePrefabHash') or '0')
        if pf != '0':
            rec['prefab'] = pf              # the prefab it came from: a proxy of the same prefab stands for it far off
        if extra:
            rec.update(extra)
        out.append(rec)

    lights, entities, volumes = [], [], []
    for ni, recs in by_node.items():
        n = nodes[ni]['Data']
        if proxies is not None and n.get('$type') in PROXY_TYPES and depot(n.get('mesh')):
            for r in recs:
                m = trs(r['Position'], r['Orientation'], r['Scale'])
                if inside(m):
                    proxies.append(proxy_rec(n, n['$type'], m, sector))
                    if stream_of(r) > 0:
                        proxies[-1]['stream'] = round(stream_of(r), 3)
            continue
        t = n['$type']
        if t in ('worldLightChannelVolumeNode', 'worldLightChannelShapeNode'):
            # interiors: lights of these channels (without LC_ChannelWorld) light only inside them; kept as the
            # box around the outline (extruded by height) or the convex shape, in the node's frame
            if t == 'worldLightChannelVolumeNode':
                o = (n.get('outline') or {}).get('Data') or {}
                pts = [(float(p.get('X', 0)), float(p.get('Y', 0))) for p in o.get('points') or []]
                lo = [min(p[0] for p in pts), min(p[1] for p in pts), 0.0] if pts else None
                hi = [max(p[0] for p in pts), max(p[1] for p in pts), float(o.get('height') or 0)] if pts else None
            else:
                vs = [(float(v.get('X', 0)), float(v.get('Y', 0)), float(v.get('Z', 0)))
                      for v in ((n.get('shape') or {}).get('Data') or {}).get('vertices') or []]
                lo = [min(v[k] for v in vs) for k in range(3)] if vs else None
                hi = [max(v[k] for v in vs) for k in range(3)] if vs else None
            for r in recs if lo else []:
                m = trs(r['Position'], r['Orientation'], r['Scale'])
                if inside(m):
                    volumes.append({'channels': str(n.get('channels')), 'm': [round(float(v), 5) for v in m[:3].ravel()],
                                    'lo': lo, 'hi': hi, 'sector': sector})
            continue
        if t == 'worldWaterPatchNode':
            # water (canals, the bay, pools): global_ocean_patch.mesh is a flat 10 m square (+-5 m) drawn by the
            # game's ocean shader; kept as its frame and appearance (ocean, swimming_pool, murky) into `water`, which
            # the scene draws as its own quads and the build fills with water contents (WATER_HALF, water_records)
            for r in recs if water is not None else []:
                m = trs(r['Position'], r['Orientation'], r['Scale'])
                if inside(m):
                    water.append({'app': cname(n.get('meshAppearance')) or 'default', 'sector': sector,
                                  'm': [round(float(v), 5) for v in m[:3].ravel()]})
            if water is None:
                skipped[t] = skipped.get(t, 0) + len(recs)
            continue
        if t == 'worldStaticLightNode':
            for r in recs:
                m = trs(r['Position'], r['Orientation'], r['Scale'])
                if inside(m):
                    lights.append(light_rec(n, m, sector, cname(n.get('debugName'))))
            continue
        if t in ('worldEntityNode', 'worldDeviceNode'):
            # devices (lamps, signs, ads; worldDeviceNode places the traffic lights' poles, street lamps, terminals):
            # their lights are components of the entity template (fetch --ents) or,
            # when the instance overrides them, of the node's instance data; a black instance light is a run-time
            # placeholder the device colours in, so only the template's counts
            tpl = depot(n.get('entityTemplate'))
            inst = (((n.get('instanceData') or {}).get('Data') or {}).get('buffer') or {}).get('Data') or {}
            for r in recs:
                m = trs(r['Position'], r['Orientation'], r['Scale'])
                if not inside(m):
                    continue
                entities.append({'template': tpl, 'app': cname(n.get('appearanceName')) or 'default',
                                 'm': [round(float(v), 5) for v in m[:3].ravel()], 'sector': sector,
                                 'name': cname(n.get('debugName')), 'embedded': tpl in embedded,
                                 'prefab': str(n.get('sourcePrefabHash') or '0')})
                if stream_of(r) > 0:
                    entities[-1]['stream'] = round(stream_of(r), 3)
                ov = instance_mesh_overrides(inst)
                if ov:
                    entities[-1]['overrides'] = ov
                lights += component_lights(inst.get('Chunks') or [], m, sector, cname(n.get('debugName')), 'instance')
                if tpl in embedded:
                    lights += component_lights(template_chunks(embedded[tpl] or {}), m, sector,
                                               cname(n.get('debugName')), 'entity')
            continue
        if t in NODE_MESH:
            if not depot(n.get('mesh')) and not depot(n.get('meshRef')):	# (worldTerrainMeshNode: meshRef)
                skipped[t + ' (no mesh)'] = skipped.get(t + ' (no mesh)', 0) + 1
                continue
            # worldBendedMeshNode: a straight template carried along these frames (bend())
            frames = [[float(f[a].get(k, 0.0)) for a in 'XYZW' for k in 'XYZ'] for f in n.get('deformationData') or []]
            for r in recs:
                m = trs(r['Position'], r['Orientation'], r['Scale'])
                emit(t, n, m, {'deform': frames} if frames else None, stream=stream_of(r))
                # ads light their surroundings in the colour of what they show (AD_LIGHT_COLOUR stands in)
                for L in n.get('lightsData') or [] if inside(m) else []:
                    if L.get('isEnabled', 1):
                        tr = L.get('transform') or {}
                        lights.append(light_rec(L, m @ trs(tr.get('position') or {}, tr.get('orientation'), None), sector,
                                                '%s/%s' % (cname(n.get('debugName')), cname(L.get('lightName'))), 'ad',
                                                AD_LIGHT_COLOUR if black(L) else None))
        elif t == NODE_INSTANCED:
            hides = instanced_hides(n)
            st = max([stream_of(r) for r in recs] or [0.0])     # (one record streams all the node's transforms)
            for k, tr in enumerate(buffer_slice(n, 'worldTransformsBuffer', buffers)):
                emit(t, n, trs(tr.get('translation', {}), tr.get('rotation'), tr.get('scale')),
                     hide=hides[k] if k < len(hides) else None, stream=st)
        elif t == NODE_DESTRUCTIBLE:
            local = buffer_slice(n, 'cookedInstanceTransforms', buffers)
            app = cname(n.get('appearanceName'))
            for r in recs:
                base = trs(r['Position'], r['Orientation'], r['Scale'])
                for tr in local:
                    m = base @ trs(tr.get('position', {}), tr.get('orientation'), None)
                    emit(t, n, m, {'app': app} if app else None, stream=stream_of(r))
        else:
            skipped[t] = skipped.get(t, 0) + len(recs)
    return out, lights, entities, volumes, skipped


def _scan_job(job):
    """One sector for stage_scan's pool: scan_sector with its own accumulators, returned for the merge in order."""
    f, box, variant = job
    vstats, water, proxies = {}, [], []
    return scan_sector(f, box, variant, vstats, water, proxies) + (vstats, water, proxies)


def stage_scan(work, box, workers=4):
    """The sectors' placements, lights, entities, volumes, water and proxies: one sector a job on `workers`
    processes (one process read 19 GB of json in 4-6 min), joined in the sectors' sorted order, so the
    store and placements.json are the same as a serial scan's."""
    from concurrent.futures import ProcessPoolExecutor
    files = sorted((work / 'json').glob('*.streamingsector.json'))
    rec = work / 'stage_records.json'

    def key():
        blocks = sorted((work / 'json_blocks').rglob('*.json')) if (work / 'json_blocks').is_dir() else []
        return stage_record_key(['stage_scan'], {'box': list(box)}, files + blocks + [
            work / 'placements.json', work / PlacementStore.DIR, work / PlacementStore.DIR / 'index.json'])
    if (work / 'placements.json').exists() and stage_kept(rec, 'scan', key()):
        print('scan: kept (the sector json, the streaming blocks, the box and the code unchanged since the last scan)')
        return
    lights, entities, volumes, water, skipped, proxies = [], [], [], [], {}, []
    variants, vstats = sector_variants(work), {}
    if variants is None:
        print('scan: no streaming blocks (cp2gltf.py blocks): every variant range kept, quest content included')
    store, meshes, n_placed = PlacementStore(work, 'w'), {}, 0
    jobs = [(f, box, (variants or {}).get(f.name.split('.')[0])) for f in files]
    ex = ProcessPoolExecutor(workers) if workers > 1 else None
    results = ex.map(_scan_job, jobs, chunksize=2) if ex else map(_scan_job, jobs)
    for k, (f, (p, li, en, vo, s, vs, wa, px)) in enumerate(zip(files, results)):
        for kk, v in vs.items():
            vstats[kk] = vstats.get(kk, 0) + v
        water += wa
        proxies += px
        store.add(p)
        n_placed += len(p)
        for q in p:
            meshes[q['mesh']] = meshes.get(q['mesh'], 0) + 1
        lights += li
        entities += en
        volumes += vo
        for kk, v in s.items():
            skipped[kk] = skipped.get(kk, 0) + v
        if k % 200 == 199:
            print(f'  scan: {k + 1}/{len(files)} sectors, {n_placed} placements', flush=True)
    if ex:
        ex.shutdown()
    store.close()
    write_json(work / 'placements.json', {
        'box': box, 'sectors': [f.name for f in files], 'placement_store': PlacementStore.DIR,
        'placement_count': n_placed, 'lights': lights,
        'entities': entities, 'channel_volumes': volumes, 'water': water, 'proxies': proxies,
        'skipped_types': dict(sorted(skipped.items(), key=lambda kv: -kv[1]))}, indent=0)
    kinds = {}
    for L in lights:
        kinds[L['kind']] = kinds.get(L['kind'], 0) + 1
    print(f'scan: {n_placed} placements of {len(meshes)} meshes, {len(lights)} lights '
          f'({", ".join(f"{v} {k}" for k, v in sorted(kinds.items()))}), {len(entities)} entities, '
          f'{len(volumes)} light channel volumes, {len(water)} water patches, from {len(files)} sectors')
    if vstats:
        print(f'scan: variant records left out (off by default) {vstats.get("variant_records_off", 0)}, sectors whose '
              f'variants did not fit their ranges (kept whole) {vstats.get("variant_mismatch", 0)}')
    for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]):
        print(f'  {v:7d} {k}')
    stage_keep(rec, 'scan', key())


# ---------------------------------------------------------------------------------------------------- fetch

# placed meshes: .mesh, and the older .w2mesh some Kabuki facades, balconies, roofs and cables still use (a building's
# wall bays were missing: 365 placements, 30 meshes)
MESH_EXTS = ('.mesh', '.w2mesh')


def mesh_stem(depot):
    """A mesh's depot path without its extension (.mesh or .w2mesh): the name of its glb and .Material.json."""
    return depot.rsplit('.', 1)[0]


class PlacementStore:
    """The scan's mesh placements by 128 m cell of their origin (placements/<i>_<j>.jsonl, a JSON record a line, and
    index.json naming this scan's cells): read a cell at a time. Watson's 1.6 M placements as one placements.json
    would be about 4 GB, some 13 GB once loaded; the slice's single file was 1.2 GB."""
    DIR, CELL = 'placements', 128.0

    def __init__(self, work, mode='r'):
        self.dir = Path(work) / self.DIR
        self.mode, self.buf, self.counts = mode, {}, {}
        if mode == 'w':
            self.dir.mkdir(parents=True, exist_ok=True)
        else:
            self.counts = json.load(open(self.dir / 'index.json', encoding='utf-8'))['cells']

    def add(self, placements):
        for p in placements:
            c = '%d_%d' % (math.floor(p['m'][3] / self.CELL), math.floor(p['m'][7] / self.CELL))
            self.buf.setdefault(c, []).append(json.dumps(p))
        if sum(len(v) for v in self.buf.values()) > 50000:
            self.flush()

    def flush(self):
        for c, lines in self.buf.items():     # a cell's first write this scan starts its file afresh
            with open(self.dir / (c + '.jsonl'), 'a' if c in self.counts else 'w', encoding='utf-8') as f:
                f.write('\n'.join(lines) + '\n')
            self.counts[c] = self.counts.get(c, 0) + len(lines)
        self.buf = {}

    def close(self):
        self.flush()
        write_json(self.dir / 'index.json', {'cell_m': self.CELL, 'cells': self.counts}, indent=0)

    def cells_near(self, centre, radius):
        for c in self.counts:
            i, j = map(int, c.split('_'))
            x0, y0 = i * self.CELL, j * self.CELL
            dx = max(x0 - centre[0], 0.0, centre[0] - x0 - self.CELL)
            dy = max(y0 - centre[1], 0.0, centre[1] - y0 - self.CELL)
            if math.hypot(dx, dy) <= radius:
                yield c

    def __iter__(self):
        for c in self.counts:
            yield from self.cell(c)

    def cell(self, c):
        with open(self.dir / (c + '.jsonl'), encoding='utf-8') as f:
            for line in f:
                if line.strip():
                    yield json.loads(line)


def read_records(path):
    """A list of records: a JSON array, or JSON lines (a tile's placements, stage_tiles)."""
    with open(path, encoding='utf-8') as f:
        head = f.read(1)
        f.seek(0)
        return json.load(f) if head == '[' else [json.loads(line) for line in f if line.strip()]


def proxy_placements(work):
    """The scan's proxies (placements.json 'proxies') as placements: drawn only between near and hide (metres), never
    collision (build skips 'proxy')"""
    src = json.load(open(work / 'placements.json', encoding='utf-8')).get('proxies', [])
    # a proxy without a near distance (652 of Watson's, building proxies of levels 2-4) drew over its detail at every
    # distance: it takes the median near of its kind's proxies at its level, from CP2077's own values
    by = {}
    for q in src:
        if q.get('near'):
            by.setdefault((q['type'], q['sector'].rsplit('_', 1)[-1]), []).append(q['near'])
    typical = {k: float(np.median(v)) for k, v in by.items()}
    skipped = {}
    for q in src:
        # CP2077's distant water patches (global_ocean_patch_{50x50,100x100,200x200}_proxy, global_water_patch.mt: 859
        # of Watson's proxies) are the ocean shader's far water; the scene's own water draws every patch at every
        # distance, so they only laid an opaque floor at the water's level, a false shallow band under it whose fade
        # in showed as dots
        if q.get('mesh') and STANDIN_PROXY.search(q['mesh'].replace('\\', '/')):
            nm = q['mesh'].replace('\\', '/').rsplit('/', 1)[-1]
            skipped[nm] = skipped.get(nm, 0) + 1
            continue
        if q.get('mesh'):
            near = q['near'] or typical.get((q['type'], q['sector'].rsplit('_', 1)[-1]), 0.0)
            yield {'mesh': q['mesh'], 'app': q.get('app') or 'default', 'type': q['type'], 'sector': q['sector'],
                   'name': q['name'], 'm': q['m'], 'near': near, 'hide': q['far'], 'stream': q.get('stream', 0),
                   'proxy': 1}
    print('proxy_placements: stand-ins left out: %s' % (', '.join('%s %d' % kv for kv in sorted(skipped.items()))
                                                         or 'none'), flush=True)


def coarse(p):
    """Whether a placement comes from a sector of level 4 or above (1 km cells and up): big pieces whose origin says
    little about where they are (a crossroads deck's origin sits 50 m off its deck, outside the area's cells)"""
    try:
        return int(str(p.get('sector', '')).rsplit('_', 1)[-1]) >= 4
    except ValueError:
        return False


def in_area_p(p, centre, radius):
    """in_area for a placement: by origin; a coarse one (coarse()) within the radius is kept here provisionally, and
    the tiles stage decides it by its placed bounds' centre instead (stage_tiles)"""
    x, y, z = p['m'][3], p['m'][7], p['m'][11]
    return in_area(x, y, centre, radius, z) or (coarse(p) and math.hypot(x - centre[0], y - centre[1]) <= radius
                                                 and (AREA_CELLS is None or z >= AREA_CELLS[2]))


def iter_placements(work, centre, radius, proxies=False):
    """The sectors' mesh placements in the area (in_area_p), and the placed entities' mesh components (fetch --ents),
    one at a time: from the placement store, or an older scan's placements.json list. proxies: the scan's proxies
    too (proxy_placements)."""
    ents = work / 'entity_meshes.json'
    extra = json.load(open(ents, encoding='utf-8')) if ents.exists() else []
    if (work / PlacementStore.DIR / 'index.json').exists():
        st = PlacementStore(work)
        src = (p for c in st.cells_near(centre, radius) for p in st.cell(c))
    else:
        src = json.load(open(work / 'placements.json', encoding='utf-8')).get('placements', [])
    for p in src:
        if p['mesh'].endswith(MESH_EXTS) and in_area_p(p, centre, radius):
            yield p
    for p in extra:
        if p['mesh'].endswith(MESH_EXTS) and in_area_p(p, centre, radius):
            yield p
    for p in (proxy_placements(work) if proxies else ()):
        if p['mesh'].endswith(MESH_EXTS) and in_area_p(p, centre, radius):
            yield p


def load_placements(work, centre, radius):
    """iter_placements as a list."""
    return list(iter_placements(work, centre, radius))


class lods_unfiltered:
    """WolvenKit's CLI drops LODs 2-4 and has no flag to keep them; its appsettings.json can
    ("MeshExportArgs": {"LodFilter": false}). Patched for the call, restored afterwards. Extra keys go into
    MeshExportArgs too (`export` needs MaterialRepo, its depot, to write materials). The unpatched file is kept
    beside it as appsettings.json.orig while patched: a run that was killed inside leaves it, and the next one takes
    the original from there (never a patched copy) and restores it at its end."""

    def __init__(self, **mesh_args):
        self.mesh_args = mesh_args

    def __enter__(self):
        self.path = wolvenkit_exe().parent / 'appsettings.json'
        self.orig = self.path.with_name('appsettings.json.orig')
        if self.orig.exists():
            self.raw = self.orig.read_bytes()
        else:
            self.raw = self.path.read_bytes()
            tmp = self.orig.with_name(self.orig.name + '.tmp')
            tmp.write_bytes(self.raw)
            os.replace(tmp, self.orig)
        cfg = json.loads(self.raw.decode('utf-8-sig'))
        cfg.setdefault('MeshExportArgs', {}).update(dict(LodFilter=False, **self.mesh_args))
        self.path.write_text(json.dumps(cfg, indent=2), encoding='utf-8')

    def __exit__(self, *exc):
        self.path.write_bytes(self.raw)
        try:
            self.orig.unlink()          # (the copy this class wrote; the original is back in place)
        except OSError:
            pass


def staging(dest):
    """A fresh folder for an exporter's run beside dest, on dest's volume (dest may be a junction to another drive),
    so that publish() moves files with renames."""
    import uuid
    top = Path(os.path.realpath(dest)).parent / ('_staging_' + Path(dest).name)
    d = top / uuid.uuid4().hex[:12]
    d.mkdir(parents=True)
    return d


def publish(stage, dest, stats=None):
    """Move each exported file from stage into dest where dest has none; never write over a file that exists.
    The exporter rewrites, in place, every texture and material a new mesh shares with one already there, and the
    caches are hard-linked between areas (seed_work.py): a reader once saw a half-written png. Each
    publication is a hard link to the staged file, which fails if the name exists: a reader sees a whole file or
    none, and two publishers racing for one name never overwrite each other (a replacing rename would). What is left in stage (copies of files dest already has) goes with the staging folder."""
    stats = stats if stats is not None else {}
    # a mesh's .glb last: the fetch counts a mesh done when its .glb exists, so an interrupted publication leaves
    # the mesh to fetch again, never a .glb without its materials and textures
    for f in sorted((f for f in Path(stage).rglob('*') if not f.is_dir()), key=lambda f: (f.suffix.lower() == '.glb', f)):
        t = Path(dest) / f.relative_to(stage)
        if t.exists():
            stats['kept'] = stats.get('kept', 0) + 1
            continue
        t.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(f, t)
        except FileExistsError:      # another writer published it first: theirs stays
            stats['kept'] = stats.get('kept', 0) + 1
            continue
        stats['published'] = stats.get('published', 0) + 1
    shutil.rmtree(stage)
    return stats


def publish_copy(src, dst):
    """publish() for one file from another volume: copied beside dst, then renamed in, only where dst is absent."""
    if dst.exists():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_name(dst.name + '.%d.tmp' % os.getpid())
    shutil.copy2(src, tmp)
    try:
        os.link(tmp, dst)            # never over a file another writer published meanwhile
    except FileExistsError:
        return False
    finally:
        os.unlink(tmp)
    return True


def stage_fetch_embedded(work, centre, radius):
    """Meshes embedded in the sectors (worldBendedMeshNode road templates, `"Flags": "Embedded"`): in no archive,
    so uncook misses them. Each goes back to a .mesh from the sector JSON (convert deserialize), then `export` writes
    its glb (every LOD) and .Material.json, its textures into meshes/ as the depot; the glb serves meshes_lod/ too."""
    check_cache_identity(work / 'meshes')
    needed = {p['mesh'] for p in iter_placements(work, centre, radius)}
    needed = {m for m in needed if not (work / 'meshes' / (mesh_stem(m).replace(SL, '/') + '.glb')).exists()}
    tmp = work / 'embedded'
    src, cooked, out = tmp / 'json', tmp / 'mesh', tmp / 'out'
    for d in (src, cooked, out):
        d.mkdir(parents=True, exist_ok=True)
    by_base = {}
    for f in sorted((work / 'json').glob('*.streamingsector.json')):
        d = json.load(open(f, encoding='utf-8'))
        for e in d['Data'].get('EmbeddedFiles') or []:
            name = cname(e.get('FileName'))
            # flat and unique: the intersections are all <folder>\s0_re.mesh
            base = hashlib.sha1(name.encode()).hexdigest()[:10] + '_' + name.split(SL)[-1]
            if name not in needed or base in by_base:
                continue
            doc = {'Header': dict(d['Header'], DataType='CR2W', ArchiveFileName=''),
                   'Data': {'Version': d['Data']['Version'], 'BuildVersion': d['Data']['BuildVersion'],
                            'RootChunk': e['Content'], 'EmbeddedFiles': []}}
            write_json(src / (base + '.json'), doc)
            by_base[base] = name
    print(f'fetch --embedded: {len(needed)} placed meshes missing, {len(by_base)} embedded in the sectors')
    if not by_base:
        return
    wolvenkit('convert', 'deserialize', src, '-o', cooked, '-v', 'Minimal')
    repo = staging(work / 'meshes')          # the textures it writes go through publish()
    with lods_unfiltered(MaterialRepo=str(repo), withMaterials=True, ExportType='WithMaterials'):
        # (a failed file fails the call: exit 3 over a few test-kit textures in no archive while 832
        # of 835 exported; what it wrote is published below and counted)
        wolvenkit('export', cooked, '-o', out, '-gp', game(), '--uext', 'png', '-v', 'Minimal', check=False)
    tex = publish(repo, work / 'meshes')
    done = 0
    for base, name in by_base.items():
        stem = mesh_stem(base)          # (.mesh or .w2mesh; WolvenKit names an old .w2mesh's export <name>.glb)
        if not (out / (stem + '.glb')).exists() and (out / (base + '.glb')).exists():
            stem = base
        rel = mesh_stem(name).replace(SL, '/')
        if not (out / (stem + '.glb')).exists():
            continue
        for sub in ('meshes', 'meshes_lod'):
            publish_copy(out / (stem + '.glb'), work / sub / (rel + '.glb'))
        if (out / (stem + '.Material.json')).exists():
            publish_copy(out / (stem + '.Material.json'), work / 'meshes' / (rel + '.Material.json'))
        done += 1
    print(f'  {done} exported; textures {tex.get("published", 0)} new, {tex.get("kept", 0)} already there')


def stage_fetch_embedded_ml(work):
    """Multilayer setups embedded in a mesh (`<mesh>.mesh_embedded\\<id>.mlsetup`): export writes the mesh's masks but
    leaves its setups inside the .mesh, so those materials baked to flat grey (40 on Kabuki: a shrine gate, kiosks,
    vending machines). Each such .mesh (already in meshes/) is serialised to JSON and its embedded setups written as
    `<setup>.json` beside the masks, where cp77mat reads a setup."""
    meshes = work / 'meshes'
    todo = {}
    for mj in meshes.rglob('*.Material.json'):
        for m in json.load(open(mj, encoding='utf-8')).get('Materials') or []:
            s = (m.get('Data') or {}).get('MultilayerSetup') or ''
            if '.mesh_embedded' in s and not (meshes / (s.replace(SL, '/') + '.json')).exists():
                todo.setdefault(s.split('.mesh_embedded')[0] + '.mesh', set()).add(s)
    print(f'fetch --embedded-ml: {sum(len(v) for v in todo.values())} embedded setups missing in {len(todo)} meshes')
    if not todo:
        return
    tmp = work / 'embedded_ml'
    src, out = tmp / 'mesh', tmp / 'json'
    for d in (src, out):
        d.mkdir(parents=True, exist_ok=True)
    names = {}
    for mesh in todo:
        f = meshes / mesh.replace(SL, '/')
        if f.exists():
            name = hashlib.sha1(mesh.encode()).hexdigest()[:10] + '_' + f.name    # flat and unique
            shutil.copy2(f, src / name)
            names[mesh] = name
    wolvenkit('convert', 'serialize', src, '-o', out, '-v', 'Minimal')
    done = 0
    for mesh, name in names.items():
        j = out / (name + '.json')
        if not j.exists():
            continue
        # export names an embedded file <mesh>.mesh_embedded\<id>: its id is the FNV-1a 64 hash of its own path
        want = {s.rsplit(SL, 1)[-1].split('.')[0]: s for s in todo[mesh]}
        for e in json.load(open(j, encoding='utf-8'))['Data'].get('EmbeddedFiles') or []:
            h = 0xcbf29ce484222325
            for b in cname(e.get('FileName')).encode():
                h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
            if str(h) in want:
                dst = meshes / (want[str(h)].replace(SL, '/') + '.json')
                dst.parent.mkdir(parents=True, exist_ok=True)
                write_json(dst, {'Data': {'RootChunk': e['Content']}})
                done += 1
    print(f'  {done} written')


def fetch_identity():
    """What every fetched file is a function of besides its depot path: the game archives and the WolvenKit build
    (sizes and times, a dozen stats a run; their bytes are never hashed). cp77_tiles.py keys the tile parts on it."""
    def stat(f):
        st = Path(f).stat()
        return [str(f), st.st_size, int(st.st_mtime)]
    exe, g = lpaths.env_path('WOLVENKIT_CLI'), lpaths.env_path('CP77_GAME')     # (unset: nothing of it to stat)
    wk = sorted(exe.parent.glob('WolvenKit*.dll')) if exe else []       # (the .exe is a 165 KB launcher)
    arcs = (g / 'archive' / 'pc' / 'content', g / 'archive' / 'pc' / 'ep1') if g else ()
    return [stat(f) for d in arcs for f in sorted(Path(d).glob('*.archive'))] + [stat(f) for f in wk]


def check_cache_identity(folder):
    """A fetch cache holds exports of one set of game archives: fetch skips a file the cache has, so after a game
    patch (or a WolvenKit change) new exports would sit beside stale ones unnoticed. The cache
    records the archives' identity (fetch_identity) in <folder>/.fetch_identity.json; a cache made before that adopts
    today's once. A different identity stops the fetch: move the cache aside (to the trash) and fetch again. (Per
    archive, refetching only what a changed archive holds, needs an archive -> file index: the city's, later.)"""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    f = folder / '.fetch_identity.json'
    now = fetch_identity()
    if not f.exists():
        f.write_text(json.dumps({'identity': now, 'adopted': any(folder.iterdir())}, indent=1), encoding='utf-8')
        return
    was = json.load(open(f, encoding='utf-8'))['identity']
    if was != now:
        changed = sorted({a[0] for a in was} ^ {b[0] for b in now} |
                         {a[0] for a in was for b in now if a[0] == b[0] and a != b})
        sys.exit('fetch: %s was exported from other game archives or another WolvenKit (changed: %s): move it aside '
                 '(to the trash) and fetch again' % (folder, ', '.join(Path(c).name for c in changed[:6])))


def stage_fetch(work, centre, radius, lods=False, batch_chars=24000, workers=3, proxies=False):
    """Uncook the placed meshes with their materials. WolvenKit takes one regex per call and Windows caps a
    command line at 32k characters, so the paths go in batches; each call scans the archives (~10 s).
    lods: every LOD, geometry only, into meshes_lod/ (glb meshes submesh_NN_LOD_m, m = LOD mask bit)."""
    from concurrent.futures import ThreadPoolExecutor
    out = work / ('meshes_lod' if lods else 'meshes')
    check_cache_identity(out)
    rec, name = work / 'stage_records.json', 'fetch' + (' lods' if lods else '') + (' proxies' if proxies else '')

    def key():
        store = work / PlacementStore.DIR
        ins = sorted(store.glob('*')) if store.is_dir() and not proxies else []
        return stage_record_key(['stage_fetch'], {'centre': list(centre), 'radius': radius, 'lods': lods,
                                                   'proxies': proxies, 'identity': fetch_identity()},
                                ins + [work / 'placements.json', work / 'entity_meshes.json', out])
    if stage_kept(rec, name, key()):
        print(f'fetch: kept (placements, entity meshes, {out.name}, the archives and the code unchanged since the last '
              f'run)')
        return
    if proxies:     # the far city's stand-ins (placements.json 'proxies', scan): their meshes alone
        src = json.load(open(work / 'placements.json', encoding='utf-8')).get('proxies', [])
        meshes = sorted({p['mesh'] for p in src if p.get('mesh')})
    else:
        meshes = sorted({p['mesh'] for p in iter_placements(work, centre, radius)})
    # a mesh is fetched when its glb and (with materials) its .Material.json are both there: an export cut off between
    # the two left a mesh without materials that was never fetched again
    have = lambda m: (out / (mesh_stem(m) + '.glb')).exists() and (lods or (out / (mesh_stem(m) + '.Material.json')).exists())
    todo = [m for m in meshes if not have(m)]
    batches, cur = [], []
    for m in todo:
        if cur and sum(len(x) + 4 for x in cur) + len(m) > batch_chars:
            batches.append(cur)
            cur = []
        cur.append(m)
    if cur:
        batches.append(cur)
    print(f'fetch: {len(meshes)} meshes within {radius:.0f} m, {len(todo)} to uncook in {len(batches)} calls')

    crashed, pub = [], {}

    def run(b):
        rx = '^(' + '|'.join(re.escape(m) for m in b) + ')$'
        extra = ('--mesh-export-type', 'MeshOnly') if lods else ('--uext', 'png')
        # each call exports into its own staging folder and publishes only what out lacks (publish())
        st = staging(out)
        # a crash (an access violation 0xC0000005 in UncookBuffers) loses only its batch,
        # which is retried below in small batches
        if wolvenkit('uncook', *archives(), '-r', rx, '-o', st, *extra, '-v', 'Minimal', check=False) is None:
            crashed.append(len(b))
        for m in b:     # WolvenKit names an old .w2mesh's export <stem>.w2mesh.glb
            f = st / (m + '.glb')
            if m.endswith('.w2mesh') and f.exists() and not (st / (mesh_stem(m) + '.glb')).exists():
                f.rename(st / (mesh_stem(m) + '.glb'))
        publish(st, out, pub)
        return len(b)
    done = 0
    with ThreadPoolExecutor(workers) as ex, (lods_unfiltered() if lods else nullcontext()):
        for n in ex.map(run, batches):
            done += n
            print(f'  {done}/{len(todo)}', flush=True)
        # what a batch left out, eight a call: a crashed batch's meshes (a mesh that crashes WolvenKit costs only its
        # own), and a batch that returned without exporting some (552 meshes in one run, no crash; one alone
        # exported fine)
        again = [m for m in todo if not have(m)]
        if again:
            print(f'fetch: {len(crashed)} calls crashed; retrying {len(again)} meshes eight a call', flush=True)
            list(ex.map(run, [again[i:i + 8] for i in range(0, len(again), 8)]))
    missing = [m for m in meshes if not have(m)]
    mf = work / 'fetch_missing.txt'
    # (this mode's own last missing set beside it: fetch, --lods and --proxies share fetch_missing.txt, and compared
    # with another mode's set none was recorded as done)
    mm = work / ('%s.missing.txt' % name.replace(' ', '_'))     # (fetch.missing.txt, fetch_lods.missing.txt, ...)
    last = mm if mm.exists() else mf
    before = set(last.read_text(encoding='utf-8').split('\n')) - {''} if last.exists() else None
    mf.write_text('\n'.join(missing), encoding='utf-8')
    mm.write_text('\n'.join(missing), encoding='utf-8')
    print(f'fetch: {len(meshes) - len(missing)} glb, {len(missing)} missing (fetch_missing.txt); files published '
          f'{pub.get("published", 0)}, already there {pub.get("kept", 0)}')
    # recorded as done only when nothing is missing, or when the missing set is the last run's (what the archives cannot
    # give): a run that newly failed some is retried next time, not kept
    if not missing or before == set(missing):
        stage_keep(rec, name, key())
    else:
        print('fetch: not recorded as done; the next run retries the %d missing' % len(missing), flush=True)


def cache_name(path):
    """A depot path's file in a flat cache: its name after a hash of the whole path (one name lives in several
    folders: hangout_1.ent under street signs and LCD screens, doors_single.app)."""
    return hashlib.sha1(path.lower().encode()).hexdigest()[:10] + '_' + path.split(SL)[-1]


def fetch_serialised(paths, raw, js):
    """Unbundle depot paths from the archives into raw/ and serialise them into js/<cache_name>.json. `convert
    serialize` writes flat file names, so each round takes files whose names differ (copies in a fresh staging folder
    a round, removed after it: a folder kept from an earlier run would serialise that run's files again and file
    them under this run's names)."""
    todo = [p for p in sorted(set(paths)) if not (js / (cache_name(p) + '.json')).exists()]
    if not todo:
        return
    raw.mkdir(parents=True, exist_ok=True)
    js.mkdir(parents=True, exist_ok=True)
    # the paths in batches: Windows caps a command line at 32k characters (Watson's templates)
    batch = []
    for p in todo + [None]:
        if batch and (p is None or sum(len(re.escape(x)) + 1 for x in batch) + len(re.escape(p)) > 24000):
            wolvenkit('unbundle', *archives(), '-r', '^(' + '|'.join(re.escape(x) for x in batch) + ')$', '-o', raw,
                      '-v', 'Minimal')
            batch = []
        if p is not None:
            batch.append(p)
    rounds = []
    for p in todo:
        name = p.split(SL)[-1].lower()
        for r in rounds:
            if name not in r:
                r[name] = p
                break
        else:
            rounds.append({name: p})
    for r in rounds:
        st = staging(js)
        src, out = st / 'src', st / 'out'
        src.mkdir()
        out.mkdir()
        for p in r.values():
            f = raw / p.replace(SL, '/')
            if f.exists():
                shutil.copy2(f, src / f.name)
        wolvenkit('convert', 'serialize', src, '-o', out, '-v', 'Minimal')
        for p in r.values():
            f = out / (p.split(SL)[-1] + '.json')
            if f.exists():
                shutil.copy2(f, js / (cache_name(p) + '.json'))
        shutil.rmtree(st)               # (copies made above and their json, all taken)


ENTITY_SKIP = tuple('base' + SL + f + SL for f in ('quest', 'cinematics', 'characters', 'gameplay' + SL + 'ai'))
TRAFFIC_LAMPS = ('red', 'yellow', 'green')
TRAFFIC_LIT = ('red',)      # nothing in the data says which lamp is lit (a controller switches them at run time)
ZONE_INDEX = {'Zero': 0, 'One': 1, 'Two': 2, 'Three': 3}   # gameLightComponent.materialZone
LAMP_INDEX = {'red': 0, 'yellow': 1, 'green': 2}            # the renderer's per-signal lamp states (x, y, z)


class Entities:
    """The archive entities placed within radius: per entity its template's components and its appearance's
    (.app, by the instance's appearance name, else the template's default), from fetch --ents."""

    def __init__(self, work, centre, radius, embedded=False):
        """embedded: the entities whose template the sector embeds too (fetch --ents's meshes; their lights come from
        the scan, so entity_lights leaves them out)"""
        self.work = work
        d = json.load(open(work / 'placements.json', encoding='utf-8'))
        self.placed = [e for e in d.get('entities', []) if (embedded or not e['embedded']) and e['template']
                       and in_area(e['m'][3], e['m'][7], centre, radius, e['m'][11])]
        self.tpl, self.app = {}, {}

    def template(self, path):
        if path not in self.tpl:
            f = self.work / 'ents' / (cache_name(path) + '.json')
            self.tpl[path] = json.load(open(f, encoding='utf-8'))['Data']['RootChunk'] if f.exists() else None
        return self.tpl[path]

    @staticmethod
    def appearance_ref(rc, name):
        """(the .app's depot path, the appearance's name in it) for an instance's appearance name, or None."""
        by_name = {cname(a.get('name')): a for a in rc.get('appearances') or []}
        a = by_name.get(cname(rc.get('defaultAppearance')) if name == 'default' else name)
        a = a or by_name.get(cname(rc.get('defaultAppearance')))
        if not a or not depot(a.get('appearanceResource')):
            return None
        return depot(a.get('appearanceResource')), cname(a.get('appearanceName'))

    def appearance(self, path, name):
        if path not in self.app:
            f = self.work / 'apps' / (cache_name(path) + '.json')
            self.app[path] = ({cname(x['Data'].get('name')): x['Data'] for x in
                               json.load(open(f, encoding='utf-8'))['Data']['RootChunk'].get('appearances') or []}
                              if f.exists() else None)
        return (self.app[path] or {}).get(name)

    def each(self):
        """(entity, its world matrix, its components: the appearance's then the template's)"""
        for e in self.placed:
            if e['template'].startswith(ENTITY_SKIP):
                continue
            rc = self.template(e['template'])
            if not rc:
                continue
            chunks = list(template_chunks(rc))
            ref = self.appearance_ref(rc, e['app'])
            A = self.appearance(*ref) if ref else None
            if A:
                chunks = list(template_chunks(A)) + chunks
            ov = e.get('overrides') or {}	# the instance's own mesh component changes (instance_mesh_overrides)
            if ov:
                chunks = [dict(c, **ov[str(c.get('id'))]) if isinstance(c, dict) and str(c.get('id')) in ov else c
                          for c in chunks]
            m = np.eye(4)
            m[:3] = np.array(e['m']).reshape(3, 4)
            # the node's scale is not the entity's: the game draws an entity at its components' own scales (CP2077's
            # proxy of street_lamp_cccpz_a, node scale 4, is 8 x 5.5 m, ours with it 32 x 22 m; the Japantown barrel
            # ad, node scale 16, was a ring some 300 m across). 1,500 of Watson's
            # entity nodes carry one (wall lamps 2.5, street lamps 4, pizza signs 4.33)
            s = np.linalg.norm(m[:3, :3], axis=0)
            m[:3, :3] /= np.where(s > 1e-9, s, 1.0)
            yield e, m, chunks


def write_embedded_templates(work, placed):
    """The templates these embedded entities name, out of their sectors' EmbeddedFiles, into ents/ beside the fetched
    ones (as fetch_serialised writes them: Data.RootChunk), so Entities reads both alike -> how many written"""
    want = {}
    for e in placed:
        want.setdefault(e['sector'], set()).add(e['template'])
    n = 0
    for sector, tpls in sorted(want.items()):
        todo = {t for t in tpls if not (work / 'ents' / (cache_name(t) + '.json')).exists()}
        if not todo:
            continue
        f = work / 'json' / (sector + '.streamingsector.json')
        if not f.exists():
            continue
        whole = json.load(open(f, encoding='utf-8'))
        for emb in whole['Data'].get('EmbeddedFiles') or []:
            name = cname(emb.get('FileName'))
            if name in todo and emb.get('Content'):
                (work / 'ents').mkdir(parents=True, exist_ok=True)
                json.dump({'Data': {'RootChunk': emb['Content']}},
                          open(work / 'ents' / (cache_name(name) + '.json'), 'w', encoding='utf-8'))
                todo.discard(name)
                n += 1
    return n


def stage_fetch_ents(work, centre, radius):
    """The entity templates placed within radius (archive ones; the sector embeds the rest) and their appearances
    (.app), serialised into ents/ and apps/, for their light and mesh components; the meshes go to
    entity_meshes.json, which fetch, fetch --lods and scene place like the sectors' own."""
    rec = work / 'stage_records.json'

    def key():
        return stage_record_key(['stage_fetch_ents'], {'centre': list(centre), 'radius': radius,
                                                        'identity': fetch_identity()},
                                [work / 'placements.json', work / 'entity_meshes.json', work / 'ents', work / 'apps'])
    if (work / 'entity_meshes.json').exists() and stage_kept(rec, 'fetch ents', key()):
        print('fetch: entity templates kept (placements.json, ents, apps, the archives and the code unchanged since '
              'the last run)')
        return
    E = Entities(work, centre, radius, embedded=True)
    paths = {e['template'] for e in E.placed if not e['embedded']}
    for d in ('ents', 'apps'):
        check_cache_identity(work / d)
    fetch_serialised(paths, work / 'ents_raw', work / 'ents')
    n_emb = write_embedded_templates(work, [e for e in E.placed if e['embedded']])
    apps = set()
    for e in E.placed:
        rc = E.template(e['template'])
        ref = E.appearance_ref(rc, e['app']) if rc else None
        if ref:
            apps.add(ref[0])
    fetch_serialised(apps, work / 'apps_raw', work / 'apps')
    # CP2077 2.5 through WolvenKit 10: the templates' and appearances' unset light colours back to the placeholder black
    import cp77_sector25
    n25 = sum(cp77_sector25.convert_resource_file(f) for d in ('ents', 'apps') for f in sorted((work / d).glob('*.json')))
    if n25:
        print(f'fetch: {n25} template and appearance lights left white by WolvenKit 10 put back as placeholders', flush=True)
    # an entity's meshes hide where its entity proxy takes over (the proxy's entityAttachDistance): the proxy whose
    # origin is within 1 m of the entity's (91% one-to-one on Watson); none: drawn as before
    prox = [q for q in json.load(open(work / 'placements.json', encoding='utf-8')).get('proxies', [])
            if 'Entity' in q['type'] and q.get('near')]
    PO = np.array([[q['m'][3], q['m'][7], q['m'][11]] for q in prox]) if prox else np.zeros((0, 3))
    order = np.argsort(PO[:, 0]) if len(PO) else np.zeros(0, np.int64)
    meshes, n_hide = [], 0
    for e, m, chunks in E.each():
        got = component_meshes(chunks, m, e['sector'], e['name'])
        for r in got if e.get('stream') else []:    # (the entity's record streams its components with it)
            r['stream'] = e['stream']
        if DOOR_FAMILY.search(e['template']):
            for r in got:                           # (the door pass classifies by template; names repeat in a
                r['ent_template'] = e['template']   # sector, so a door is its entity's position too)
                r['ent_pos'] = [round(float(v), 2) for v in m[:3, 3]]
        if len(PO):
            o = m[:3, 3]
            a0, a1 = np.searchsorted(PO[order, 0], o[0] - 1.0), np.searchsorted(PO[order, 0], o[0] + 1.0, 'right')
            near = order[a0:a1]
            near = near[np.linalg.norm(PO[near] - o, axis=1) <= 1.0]
            if len(near) >= 1:
                # the nearest (two proxies within 1 m used to leave the entity never hiding)
                k = near[int(np.argmin(np.linalg.norm(PO[near] - o, axis=1)))]
                h = float(prox[int(k)]['near'])
                for r in got:
                    if not r.get('hide'):
                        r['proxy_hide'] = round(h, 3)     # (draw_distance: only where the proxies are drawn)
                n_hide += 1
        meshes += got
    write_json(work / 'entity_meshes.json', meshes, indent=0)
    missing = [p for p in paths if not (work / 'ents' / (cache_name(p) + '.json')).exists()]
    missing_apps = [p for p in apps if not (work / 'apps' / (cache_name(p) + '.json')).exists()]
    print(f'fetch: {len(paths)} entity templates within {radius:.0f} m ({len(missing)} missing), {n_emb} embedded in '
          f'their sectors, {len(apps)} appearance files ({len(missing_apps)} missing), {len(meshes)} entity mesh '
          f'components (entity_meshes.json); {n_hide} entities hide where their proxy takes over')
    stage_keep(rec, 'fetch ents', key())


def entity_lights(work, centre, radius, signals=False):
    """Lights of the archive entities (fetch --ents) placed within radius: their templates' and appearances'. A
    traffic light shows one lamp (TRAFFIC_LIT); with signals (the scene's cycling signals) every lamp, tagged with its
    entity's position (signal_ent) and lamp name, which the renderer switches with the signal's state."""
    out = []
    # one component_space per template and appearance: it walks the whole template, and an entity without overrides
    # has the same components (the same objects) as every other placement of it (41 M calls
    # for Watson's 29,730 entities, 57 of a one-tile chunks stage's 67 s)
    spaces = {}
    # a placement whose instance data carries a light (the scan's kind 'instance', named <entity>/Component) shows
    # that light in place of its template's: both were drawn before (4,513 placements; Misty's wall lamp a warm 5000
    # spot on its pink 7000 point, where CP2077's facade is lit red). An A/B over 62 cameras:
    # instance-only matched CP2077, template-only did not. Traffic
    # signals keep their template lamps (the signal cycle switches them by name)
    # each instance light belongs to the one placement it is nearest of those with its sector and name (within 10 m):
    # same-named neighbours keep their own lamps (every same-named entity within 10 m once lost them)
    ents = Entities(work, centre, radius)
    named = {}
    for e in ents.placed:
        named.setdefault((e['sector'], e['name']), []).append(e)
    owned = set()
    for L in json.load(open(work / 'placements.json', encoding='utf-8')).get('lights', []):
        if L.get('kind') == 'instance':
            cands = named.get((L.get('sector'), L['name'].rpartition('/')[0]), [])
            near = min(cands, key=lambda e: math.dist(L['pos'][:3], (e['m'][3], e['m'][7], e['m'][11])), default=None)
            if near is not None and math.dist(L['pos'][:3], (near['m'][3], near['m'][7], near['m'][11])) < 10.0:
                owned.add(id(near))
    for e, m, chunks in ents.each():
        traffic = 'traffic' in e['template'] or 'crossing' in e['template']
        if id(e) in owned and not traffic:
            continue
        keep = (lambda n: n not in TRAFFIC_LAMPS or n in TRAFFIC_LIT) if traffic and not signals else None
        key = None if e.get('overrides') else (e['template'], e['app'])
        local = spaces.get(key) if key else None
        if local is None:
            local = component_space(chunks)
            if key:
                spaces[key] = local
        got = component_lights(chunks, m, e['sector'], e['name'], 'entity', keep, local)
        if traffic and signals:
            for L in got:
                lamp = L['name'].rsplit('/', 1)[-1]
                if lamp in TRAFFIC_LAMPS:
                    L['signal_ent'], L['lamp'] = [round(float(v), 4) for v in m[:3, 3]], lamp
        out += got
    return out


# ---------------------------------------------------------------------------------------------------- build

CP_TO_GLTF = np.array([[1.0, 0, 0, 0], [0, 0, 1.0, 0], [0, -1.0, 0, 0], [0, 0, 0, 1.0]])
SKIP_PREFIX = ('base' + '\\' + 'lighting' + '\\', 'engine' + '\\', 'base' + '\\' + 'fx' + '\\',
               'user' + '\\')    # developers' own folders (a debug cube): the shipped archives have none
# base\fx\ meshes the render layer keeps (never collision): neon letters (OPEN, arrows, Misty's XOXO), holograms
# (screens, the cherry tree) and the emissive cards (plane_emissive_*: 14k small glows on facades and streets, signages.mt).
# Every base\fx\ mesh used to be left out;
# their fx-only templates (fog lasers, light gradients, HUD markers, particles) are skipped by cp77mat
FX_KEEP = tuple('base\\fx\\' + p for p in ('environment\\neons\\', 'environment\\holograms\\',
                                           'meshes\\primitives\\plane_emissive'))
SUBMESH = re.compile(r'submesh_(\d+)_LOD_(\d+)')


def bend(pos, nrm, frames, y0, y1):
    """worldBendedMeshNode: the straight template (CP-local, along +Y from y0 to y1) carried along the node's
    deformation frames (axes X Y Z and origin W each), spaced evenly over [y0, y1]; a vertex blends its two
    neighbouring frames. Matches the nodes' deformedBox to 1-2 cm (Kabuki roads)."""
    F = np.asarray(frames, float).reshape(-1, 4, 3)
    n = len(F)
    if n == 1:
        A = F[0]
        return pos @ A[:3] + A[3], None if nrm is None else nrm @ A[:3]
    t = np.clip((pos[:, 1] - y0) / max(y1 - y0, 1e-9), 0, 1) * (n - 1)
    i = np.minimum(t.astype(int), n - 2)
    f = (t - i)[:, None]
    ys = y0 + (y1 - y0) * np.arange(n) / (n - 1)

    def at(k):
        A = F[k]
        return A[:, 3] + A[:, 0] * pos[:, 0:1] + A[:, 1] * (pos[:, 1] - ys[k])[:, None] + A[:, 2] * pos[:, 2:3]
    out = (1 - f) * at(i) + f * at(i + 1)
    if nrm is not None:
        X = (1 - f) * F[i, 0] + f * F[i + 1, 0]
        Y = (1 - f) * F[i, 1] + f * F[i + 1, 1]
        Z = (1 - f) * F[i, 2] + f * F[i + 1, 2]
        nrm = X * nrm[:, 0:1] + Y * nrm[:, 1:2] + Z * nrm[:, 2:3]
    return out, nrm


class MeshLods:
    """One uncooked mesh: chunks by LOD level (from the meshes_lod glb), and its appearances' per-chunk material
    names and material records (from the with-materials export's .Material.json)."""

    def __init__(self, work, depot):
        rel = self.rel = mesh_stem(depot).replace('\\', '/')
        self.work, self.ok, self.dec = work, False, {}
        g = self.src = work / 'meshes_lod' / (rel + '.glb')
        mj = work / 'meshes' / (rel + '.Material.json')
        if not g.exists():
            return
        self.chunks = self.read(g)           # chunk index -> (lod mask, [(pos, nrm, uv, tris)])
        if not self.chunks:
            return
        masks = 0
        for mask, _ in self.chunks.values():
            masks |= mask
        self.levels = [b for b in range(8) if masks >> b & 1]
        pts = np.concatenate([p[0] for mask, ps in self.chunks.values() if mask & (1 << self.levels[0]) for p in ps])
        self.lo, self.hi = pts.min(0), pts.max(0)
        self.size = float(np.linalg.norm(self.hi - self.lo))
        # the template's extent along CP-local +Y (bend()): glTF-local -Z
        self.ylim = (-float(self.hi[2]), -float(self.lo[2]))
        self.apps, self.materials = {}, {}
        if mj.exists():
            import cp77mat
            d = json.load(open(mj, encoding='utf-8'))
            # materials on CP2077's placeholder layer setup (the DEBUG texture) given their siblings'
            got = cp77mat.placeholder_setups(work / 'meshes', mj, d)
            if got:
                PLACEHOLDERS['meshes'] += 1
                PLACEHOLDERS['materials'] += got
            for i, (key, names) in enumerate((d.get('Appearances') or {}).items()):
                name = key[:-len(str(i))] if key.endswith(str(i)) else key
                self.apps.setdefault(name, names)
            self.materials = {m['Name']: m for m in d.get('Materials') or []}
        self.ok = True

    @staticmethod
    def read(path):
        doc = gltfio.Document(path)
        chunks = {}
        for i, nd, m, chain in doc.walk():
            k = SUBMESH.match(nd.get('name', '')) if 'mesh' in nd else None
            if k:
                chunks[int(k.group(1))] = (int(k.group(2)), [(pos @ m[:3, :3].T + m[:3, 3], nrm, uv, tris)
                                                            for pos, nrm, uv, tris, _ in doc.primitives(nd['mesh'])])
        return chunks

    def zones(self, ci):
        """Per primitive of chunk ci, each vertex's lights_interactive zone from COLOR_0 (round(5 r) - 1: a signal
        head's 0.4 hand is zone One, its 0.6 walking figure Two), or None without colours; read on first use."""
        if not hasattr(self, '_zones'):
            doc = gltfio.Document(self.src)
            self._zones = {}
            for i, nd, m, chain in doc.walk():
                k = SUBMESH.match(nd.get('name', '')) if 'mesh' in nd else None
                if k:
                    self._zones[int(k.group(1))] = [None if c is None else np.round(c[:, 0] * 5).astype(int) - 1
                                                    for c in doc.colors(nd['mesh'])]
        return self._zones.get(ci) or []

    def dec_path(self, level):
        return self.work / 'meshes_dec' / ('%s.L%d.glb' % (self.rel, level))

    def level_chunks(self, level):
        """[(chunk index, prims)] of one LOD level; the decimated copy when `decimate` made one."""
        if level not in self.dec:
            p = self.dec_path(level)
            self.dec[level] = self.read(p) if p.exists() else None
        src = self.dec[level] if self.dec[level] else self.chunks
        return sorted((ci, ps) for ci, (mask, ps) in src.items() if mask >> level & 1)

    def tris(self, level, original=False):
        chunks = [(ci, ps) for ci, (mask, ps) in self.chunks.items() if mask >> level & 1] if original else \
            self.level_chunks(level)
        return sum(len(t) for ci, ps in chunks for *_, t in ps)

    def level_for(self, size, near=12.0, mid=3.0):
        """Finest LOD from `near` metres, the second from `mid`, the coarsest below."""
        if size < mid:
            return self.levels[-1]
        want = 0 if size >= near else 1
        ok = [lv for lv in self.levels if lv <= want]
        return max(ok) if ok else self.levels[0]

    def app_name(self, app):
        """an appearance as this mesh names it: CP2077 2.5's cooked nodes give WolvenKit 10 some appearance names only
        as their CName hash (FNV-1a 64 of the name: 'cyan' 8119478377456132730), matched here against the mesh's own
        appearances; an unknown name stays as it is"""
        if app in self.apps or not (isinstance(app, str) and app.isdigit()):
            return app
        if not hasattr(self, '_app_hash'):
            self._app_hash = {cname_hash(n): n for n in self.apps}
        return self._app_hash.get(int(app), app)

    def material_names(self, app):
        names = self.apps.get(self.app_name(app)) or (next(iter(self.apps.values())) if self.apps else [])
        return names


def ring_boxes(centre_m, radius, z0, z1, segments=40, thick=1.0):
    """Closed ring of boxes (CP space) for the playable boundary."""
    out = []
    for i in range(segments):
        a0, a1 = 2 * math.pi * i / segments, 2 * math.pi * (i + 1) / segments
        p0 = np.array([math.cos(a0), math.sin(a0)]) * radius + centre_m
        p1 = np.array([math.cos(a1), math.sin(a1)]) * radius + centre_m
        d = (p1 - p0) / np.linalg.norm(p1 - p0)
        nrm = np.array([d[1], -d[0]]) * thick
        q = [p0 - d * 0.5, p1 + d * 0.5, p1 + d * 0.5 + nrm, p0 - d * 0.5 + nrm]
        out.append(np.array([[x, y, z] for z in (z0, z1) for x, y in q]))
    return out


def box_ring_boxes(half, z0, z1, out=1.0, thick=1.0):
    """Closed ring of four wall boxes (CP space, about the area's centre) around a box of half sizes (hx, hy), out
    metres outside it"""
    hx, hy = half[0] + out, half[1] + out
    walls = [(-hx - thick, -hy - thick, hx + thick, -hy), (-hx - thick, hy, hx + thick, hy + thick),
             (-hx - thick, -hy, -hx, hy), (hx, -hy, hx + thick, hy)]
    return [np.array([[x, y, z] for z in (z0, z1) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))])
            for x0, y0, x1, y1 in walls]


BOX_FACES = np.array([[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4],
                      [1, 2, 6], [1, 6, 5], [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]])

WATER_HALF = 5.0    # global_ocean_patch.mesh: a flat square, +-5 m about its node
WATER_DEPTH = 4.0   # swimmable depth under a patch; a floor below it (the canals have no bed in the sectors)
# a patch's look by its appearance: sRGB albedo and roughness, opaque; the scene's wet-ground mirror (r_sceneWet,
# its screen-space reflections) reflects the street over it
WATER_LOOK = {'default': ((14, 22, 26), 0.05), 'murky': ((22, 26, 18), 0.08), 'swimming_pool': ((30, 96, 112), 0.05)}
# the renderer's water mode (data contract v1): a water
# material is kind blend, flag 0x80, no colour image, a ripple normal map (water_ripple_normal), today's ORM, tint the
# appearance's ScatteringColor (global_ocean_patch.mesh's materials: ocean, murky_water, swimming_pool), no emissive.
# DLLs without the mode draw it as a lit blend in the tint colour
WATER_FLAG = 0x80
WATER_SCATTER = {'default': (36, 43, 33), 'murky': (2, 27, 15), 'swimming_pool': (127, 153, 165)}
# maps/<map>.xwat (the renderer's contract v1 and its foam intensity at offset 36), per
# appearance from CP2077's materials: visibility 3 / ScatteringDepth,
# shore EdgeBlend, tile 4 m (water_ripple_normal's), speed (no source: CP2077's waves are its wind's), normal strength
# min(1, Choppiness.Z / 5 + 0.3 NormalDetailIntensity), foam band ShoreThreshold, foam intensity FoamIntensity; 0 is
# the renderer's default (roughness from the ORM). Fields in xwat order
WATER_XWAT_FIELDS = ('visibility', 'shore', 'tile', 'speed', 'heading', 'roughness', 'normal_strength', 'foam',
                     'foam_intensity')
WATER_XWAT = {'default': (1.5, 0.5, 4.0, 0.0, 0.0, 0.0, 1.0, 0.5, 0.2),
              'murky': (0.42, 0.3, 4.0, 0.15, 0.0, 0.0, 1.0, 0.28, 0.2),
              'swimming_pool': (6.0, 0.1, 4.0, 0.05, 0.0, 0.0, 0.4, 0.0, 0.0)}
WATER_FOAM = ('engine', 'textures', 'global_ocean_patch', 'sea_water_foam.png')    # (exported with the meshes)


def water_ripple_normal(n=512, tile_m=4.0, wind=(1.0, 0.35), speed=2.0, slope=0.22, seed=2077):
    """A tileable ripple normal map (n x n over tile_m metres, OpenGL RGB like cp77mat normal_map): one frame of an
    FFT sea (a Phillips spectrum for a wind of `speed` m/s, heights to slopes, their spread scaled to `slope`), the
    renderer scrolling two layers of it. CP2077's water takes its normals from its run-time simulation
    (water_patch_fft.dtex); its WaterMap, nc_water.xbm, is a two-channel mask (values near 0 or 255, blue empty), not
    a normal map. Deterministic."""
    rng = np.random.default_rng(seed)
    k = 2.0 * np.pi * np.fft.fftfreq(n, d=tile_m / n)
    kx, ky = np.meshgrid(k, k)
    kk = np.hypot(kx, ky)
    kk[0, 0] = 1.0
    w = np.asarray(wind, float) / np.linalg.norm(wind)
    peak = speed * speed / 9.81
    spec = np.exp(-1.0 / (kk * peak) ** 2) / kk ** 4 * ((kx * w[0] + ky * w[1]) / kk) ** 2 * np.exp(-(kk * 0.01) ** 2)
    spec[0, 0] = 0.0
    H = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) * np.sqrt(spec / 2.0)
    gx, gy = np.fft.ifft2(1j * kx * H).real, np.fft.ifft2(1j * ky * H).real
    s = slope / max(float(np.sqrt((gx ** 2 + gy ** 2).mean())), 1e-12)
    nrm = np.stack([-gx * s, gy * s, np.ones((n, n))], -1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    return np.uint8(np.round(nrm * 127.5 + 127.5))


_WATER_XY = {}


def water_patch_points(work):
    """Every water patch the scan found (placements.json 'water'): their centres and half sizes (CP metres), kept a
    process: an open water mesh's test (open_water)"""
    k = str(work)
    if k not in _WATER_XY:
        recs = json.load(open(work / 'placements.json', encoding='utf-8')).get('water') or []
        c = np.array([[r['m'][3], r['m'][7], r['m'][11]] for r in recs], float).reshape(-1, 3)
        h = np.array([WATER_HALF * max(abs(r['m'][0]) + abs(r['m'][1]), abs(r['m'][4]) + abs(r['m'][5]))
                      for r in recs], float)
        _WATER_XY[k] = (c, h)
    return _WATER_XY[k]


def open_water(work, p, ml, m):
    """Whether a placement is open water drawn as a mesh: one of its materials is global_water_patch.mt (CP2077's ocean
    shader on a mesh of its own: water_mesh_arasaka_estate_a, a pond of the Arasaka estate's garden), it is no proxy,
    and no water patch of the scan lies over its footprint at or above its surface (a pool mesh under its own
    swimming_pool patches, water_mesh_arasaka_estate_b, stays as it is: water under water). The scene draws it in its
    own water material (scene_part: the patches' 'default'), not as an opaque sheet."""
    if p.get('proxy') or not any((r.get('MaterialTemplate') or '').replace('\\', '/').lower().endswith('global_water_patch.mt')
                                 for r in ml.materials.values()):
        return False
    corners = np.array([[x, y, z] for x in (ml.lo[0], ml.hi[0]) for y in (ml.lo[1], ml.hi[1])
                        for z in (ml.lo[2], ml.hi[2])]) @ G2C.T
    w = corners @ np.asarray(m, float)[:, :3].T + np.asarray(m, float)[:, 3]
    lo, hi = w.min(0), w.max(0)
    c, h = water_patch_points(work)
    over = ((c[:, 0] + h >= lo[0]) & (c[:, 0] - h <= hi[0]) & (c[:, 1] + h >= lo[1]) & (c[:, 1] - h <= hi[1])
            & (c[:, 2] >= hi[2] - 0.5))
    return not bool(over.any())


def water_records(work, centre, radius, half=None):
    """The scan's water patches (placements.json 'water'; none in an older scan): within radius and
    the area's cells, or with half (an area's AREAS 'half', the hub's ring box) every one inside that box: the open
    sea lies outside the kept cells, and taking the cells' patches only left the bay two-thirds empty (one scene,
    15,642 of 29,018)."""
    d = json.load(open(work / 'placements.json', encoding='utf-8'))
    if half:
        hx, hy = (half, half) if not isinstance(half, (tuple, list)) else half
        return [w for w in d.get('water', [])
                if abs(w['m'][3] - centre[0]) <= hx and abs(w['m'][7] - centre[1]) <= hy]
    return [w for w in d.get('water', []) if in_area(w['m'][3], w['m'][7], centre, radius, w['m'][11])]


def water_quads(recs, half=WATER_HALF, tol=0.05):
    """Water patches -> surface quads (4 x 3, CP space, counter-clockwise from above): the axis-aligned unit
    patches (all but 2 of the Watson slice's 3,048, laid on a 10 m grid) merged into rows, the rows into rectangles;
    the rest one quad each."""
    rects, out = [], []
    for w in recs:
        m = np.array(w['m']).reshape(3, 4)
        if np.allclose(m[:, :3], np.eye(3), atol=1e-4):
            x, y, z = m[:, 3]
            rects.append([x - half, y - half, x + half, y + half, z])
        else:
            c = np.array([[sx, sy, 0.0, 1.0] for sx, sy in ((-half, -half), (half, -half), (half, half), (-half, half))])
            out.append(c @ m.T)

    def merge(rs, along):   # along 0: join in x within a row (same y span); 1: join in y (same x span)
        a0, a1 = (0, 2) if along == 0 else (1, 3)
        b0, b1 = (1, 3) if along == 0 else (0, 2)
        groups = {}
        for r in rs:
            groups.setdefault((round(r[4], 1), round(r[b0] / tol), round(r[b1] / tol)), []).append(r)
        got = []
        for g in groups.values():
            g.sort(key=lambda r: r[a0])
            cur = list(g[0])
            for r in g[1:]:
                if r[a0] <= cur[a1] + tol:
                    cur[a1] = max(cur[a1], r[a1])
                else:
                    got.append(cur)
                    cur = list(r)
            got.append(cur)
        return got
    for x0, y0, x1, y1, z in merge(merge(rects, 0), 1):
        out.append(np.array([[x0, y0, z], [x1, y0, z], [x1, y1, z], [x0, y1, z]]))
    return out


# props that collide as their mesh whatever their fill (walked on or through: a box would block them)
COL_MESH_NAMES = re.compile(r'stair|step|ramp|rail|fence|bench|arch|barrier|bollard|pipe|ladder')
# walked up: these take their mesh even with --col-prop-tris 0 (the full list added 109 k triangles for Kabuki, past
# what q3map2 compiles in reasonable time; railings, benches and pipes stay boxes there)
COL_WALK_NAMES = re.compile(r'stair|step|ramp')
# grass, weeds and litter as boxes left invisible lumps to stand on (no
# collision now: the player walks through them); an awning's box hung to head height in an alley (awnings and the
# like collide as their own coarsest mesh, or not at all past --col-thin-tris); the pagoda's tiled roof pieces kept
# every tile ridge, steeper than a walkable slope (roof pieces collide as their convex hull: the smooth top)
COL_NONE_NAMES = re.compile(r'(^|_)(stipa|serriola|grass|weeds?|ferns?|ivy|moss|leaves|litter|bottles?|cigarettes?|'
                            r'newspapers?|papers?|soda_can|street_trash|cardboard_trash|paper_trash|fries_trash)(_|\.|$)')
COL_THIN_NAMES = re.compile(r'awning|canopy|parasol|umbrella|tarp')
# CP2077's lighting-only planes (seen by GI, not drawn, not solid), placed in doorways and windows: left out of the
# tiles altogether (1,967 in Watson's tag 7)
GI_ONLY_NAMES = re.compile(r'^(gi_visible_game_invisible|box_game_invisible)')
COL_HULL_NAMES = re.compile(r'^wat_kab_building_f_roof')	# the traditional (pagoda) set; other roofs have parapets


def hull_tris(T):
    """The convex hull of triangles (k, 3, 3) as outward-wound triangles: a tiled roof piece's smooth top. A flat or
    degenerate set comes back as it was."""
    from scipy.spatial import ConvexHull, QhullError
    pts = np.unique(T.reshape(-1, 3).round(4), axis=0)
    try:
        h = ConvexHull(pts)
    except (QhullError, ValueError):
        return T
    H = pts[h.simplices]
    n = np.cross(H[:, 1] - H[:, 0], H[:, 2] - H[:, 0])
    flip = (n * (H.mean(1) - pts.mean(0))).sum(1) < 0
    H[flip] = H[flip][:, ::-1]
    return H


def clearance(points, tris, half=0.75, low=0.15, high=1.9, cell=2.0):
    """True where no triangle's bounding box reaches into the upright box a player needs at the point (glTF metres, y
    up): +-half across (0.75: the player's 0.4 plus the slab depth gltf2map gives a wall; 0.5 left one of map-21's
    16 in a wall's slab), low..high above. Conservative (a slanted triangle's box covers more than it does), so it only
    ever drops a candidate. Spawns checked only overhead sat in thin walls (7 of 16)."""
    lo, hi = tris.min(1), tris.max(1)
    cells = {}
    c0 = np.floor(lo[:, [0, 2]] / cell).astype(np.int64)
    c1 = np.floor(hi[:, [0, 2]] / cell).astype(np.int64)
    for i in range(len(tris)):
        for cx in range(c0[i, 0], c1[i, 0] + 1):
            for cz in range(c0[i, 1], c1[i, 1] + 1):
                cells.setdefault((cx, cz), []).append(i)
    ok = np.ones(len(points), bool)
    for k, p in enumerate(points):
        a = np.array([p[0] - half, p[1] + low, p[2] - half])
        b = np.array([p[0] + half, p[1] + high, p[2] + half])
        near = set()
        for cx in range(int(np.floor(a[0] / cell)), int(np.floor(b[0] / cell)) + 1):
            for cz in range(int(np.floor(a[2] / cell)), int(np.floor(b[2] / cell)) + 1):
                near.update(cells.get((cx, cz), ()))
        if near:
            i = np.fromiter(near, np.int64)
            ok[k] = not np.any(np.all((lo[i] <= b) & (hi[i] >= a), axis=1))
    return ok


def cluster_tris(T, cell):
    """Vertex-clustering simplification of world triangles (k, 3, 3): vertices snap to a `cell` grid, collapsed and
    repeated triangles go. Collision only needs the shape to a few decimetres."""
    if cell <= 0 or not len(T):
        return T
    q = np.round(T.reshape(-1, 3) / cell).astype(np.int64)
    uq, inv = np.unique(q, axis=0, return_inverse=True)
    t = inv.reshape(-1, 3)
    t = t[(t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])]
    if not len(t):
        return T[:0]
    key = np.sort(t, axis=1)
    _, first = np.unique(key, axis=0, return_index=True)
    t = t[np.sort(first)]
    return uq[t].astype(np.float64) * cell


# --inst-split: mesh-collision placements whose prototype repeats go to per-tile instance lists, and every
# collision triangle carries a band. Bands: near (boxes, the size < --col-box branches), mid (mesh 6-12 m), structure
# (mesh 12 m and over, cutouts, bent pieces, water)
INST_BANDS = ('near', 'mid', 'structure')
INST_STRUCTURE_M = 12.0
INST_MIN_PLACED = 4         # a prototype is instanced when placed at least this often over all the tiling's tiles
INST_TOL = 1e-4


def inst_key16(key):
    """The prototype file name: the first 16 hex digits of the SHA-1 of the key's canonical JSON, the list
    [mesh depot path, appearance, collision LOD level, path] with json.dumps(separators=(',', ':')) (ASCII)."""
    return hashlib.sha1(json.dumps(list(key), separators=(',', ':')).encode('ascii')).hexdigest()[:16]


def inst_frame(L, tol=INST_TOL):
    """(frame, None) for an instanceable placed 3x3 (glTF frame, Y up), else (None, the reason): unit scale (every
    column's norm within tol of 1), then axis (every entry within tol of 0 or +-1) or yaw (|L[1][1]| within tol of 1,
    L[1][0], L[1][2], L[0][1], L[2][1] within tol of 0)."""
    L = np.asarray(L, np.float64)
    if np.any(np.abs(np.linalg.norm(L, axis=0) - 1.0) > tol):
        return None, 'scale'
    A = np.abs(L)
    if np.all((A <= tol) | (np.abs(A - 1.0) <= tol)):
        return 'axis', None
    if abs(A[1, 1] - 1.0) <= tol and max(A[1, 0], A[1, 2], A[0, 1], A[2, 1]) <= tol:
        return 'yaw', None
    return None, 'frame'


def inst_band(path, size, bent, col_box):
    """A collision placement's band (STEP0-FORMATS.md section 5) from its branch and the converter's own size. A hull
    has no size cap in the branch (only --col-min): one under --col-box is near like the other prop branches, a larger
    one is banded by size like a mesh (Watson's tag 5 has none; the rule is Kabuki's pagoda roofs)."""
    if bent or path == 'cutout':
        return 'structure'
    if path in ('thin', 'named', 'prop') or size < col_box:
        return 'near'
    return 'mid' if size < INST_STRUCTURE_M else 'structure'


def png_bytes(a):
    from io import BytesIO
    from PIL import Image
    b = BytesIO()
    Image.fromarray(a).save(b, 'PNG', optimize=False, compress_level=6)
    return b.getvalue()


def jpg_bytes(a, q=90):
    from io import BytesIO
    from PIL import Image
    b = BytesIO()
    Image.fromarray(a[..., :3]).save(b, 'JPEG', quality=q)
    return b.getvalue()


def placed_extent(ml, m):
    """A placement's world extent (x, y, z metres) from its mesh bounds and CP 3x4."""
    corners = np.array([[x, -z, y] for x in (ml.lo[0], ml.hi[0]) for y in (ml.lo[1], ml.hi[1])
                        for z in (ml.lo[2], ml.hi[2])]) @ np.asarray(m)[:, :3].T
    return corners.max(0) - corners.min(0)


def flat_placement(ml, m, height):
    return placed_extent(ml, m)[2] <= height


def placement_key(p, digits):
    """What makes two placements the same drawing: mesh, appearance, transform (to digits places), chunk mask,
    deformation and proxy near distance. The plan and the tiles drop only true duplicates (co-located
    placements of one mesh with complementary chunk masks or other appearances lost one)."""
    deform = p.get('deform')
    return (p['mesh'], p.get('app'), tuple(round(v, digits) for v in p['m']), str(p.get('chunk_mask')),
            json.dumps(deform, sort_keys=True) if deform else None, p.get('near'))


def make_plan(work, centre, radius, a, stats, placements=None, render=False):
    """[(placement, MeshLods, CP 3x4, world size m, LOD level)] after duplicate (placement_key), prefix and size culls; of the area's
    placements within radius, or of a given list (a tile's, stage_tiles). render: the scene's plan, which keeps the
    FX_KEEP meshes."""
    pl = load_placements(work, centre, radius) if placements is None else placements
    meshes, seen, plan = {}, set(), []
    for p in pl:
        if p['mesh'].startswith(SKIP_PREFIX) and not (render and p['mesh'].startswith(FX_KEEP)):
            stats['skipped_prefix'] += 1
            continue
        key = placement_key(p, 2)
        if key in seen:
            stats['dupes'] += 1
            continue
        seen.add(key)
        if p['mesh'] not in meshes:
            meshes[p['mesh']] = MeshLods(work, p['mesh'])
        ml = meshes[p['mesh']]
        if not ml.ok:
            stats['missing_mesh'] += 1
            if p.get('proxy'):      # (a proxy needs fetch --proxies and --proxies --lods)
                stats['missing_proxy_mesh'] = stats.get('missing_proxy_mesh', 0) + 1
            continue
        m = np.array(p['m']).reshape(3, 4)
        size = ml.size * float(np.linalg.norm(m[:, :3], axis=0).max())
        level = ml.level_for(size, a.near, a.mid)
        if size < a.min_size:
            # wide sparse slabs (3 m sidewalk tiles, floors, roofs) stay at any size: dropping them holes the
            # ground; the density cap leaves out clutter that happens to be flat (boxes, rags, fans)
            ext = placed_extent(ml, m)
            if not (ext[2] <= a.flat_height and min(ext[0], ext[1]) >= a.flat_min
                    and ml.tris(level, original=False) <= a.flat_density * ext[0] * ext[1]):
                stats['small'] += 1
                continue
            stats['flat_kept'] += 1
        plan.append((p, ml, m, size, level))
    return plan


def stage_decimate(work, centre, radius, a):
    """Fit the plan to --budget render triangles: each (mesh, LOD) gets min(its triangles, max(floor, k * size^2))
    for its median placed size, k solved by bisection, so small props lose the most; then Blender collapses the
    meshes over --keep-ratio of their target into meshes_dec/<depot>.L<level>.glb."""
    plan = make_plan(work, centre, radius, a, {k: 0 for k in ('dupes', 'skipped_prefix', 'missing_mesh', 'small', 'flat_kept')})
    groups = {}
    for p, ml, m, size, lv in plan:
        groups.setdefault((p['mesh'], lv), (ml, []))[1].append(size)
    items = [(depot, lv, ml, len(sizes), float(np.median(sizes)), ml.tris(lv, original=True))
             for (depot, lv), (ml, sizes) in groups.items()]
    total = sum(n * t for *_, n, s, t in items)

    def fitted(k):
        return sum(n * min(t, max(a.floor_tris, k * s * s)) for *_, n, s, t in items)
    lo, hi = 0.0, 1e6
    for _ in range(60):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if fitted(mid) < a.budget else (lo, mid)
    k = lo
    jobs = []
    for depot, lv, ml, n, s, t in items:
        target = min(t, max(a.floor_tris, k * s * s))
        if t and target / t < a.keep_ratio:
            dst = ml.dec_path(lv)
            dst.parent.mkdir(parents=True, exist_ok=True)
            keep = ['submesh_%02d_LOD_%d' % (ci, mask) for ci, (mask, ps) in ml.chunks.items() if mask >> lv & 1]
            # (exported under a temporary name, renamed below once Blender has finished the whole batch)
            jobs.append({'src': str(ml.src), 'dst': str(dst.with_suffix('.tmp.glb')), 'keep': keep,
                         'ratio': target / t, 'final': str(dst)})
    print(f'decimate: {total:,} -> {fitted(k):,.0f} triangles (k {k:.2f} per m^2), {len(jobs)} of {len(items)} '
          f'meshes to collapse')
    if a.dry_run or not jobs:
        return
    jf = work / 'decimate_jobs.json'
    jf.write_text(json.dumps(jobs, indent=0), encoding='utf-8')
    r = subprocess.run([str(blender()), '--background', '--factory-startup', '--python', str(HERE / 'blender_decimate.py'),
                        '--', str(jf)], capture_output=True, text=True, errors='replace')
    (work / 'decimate.log').write_text(r.stdout + r.stderr, encoding='utf-8')
    print('\n'.join([ln for ln in r.stdout.splitlines() if ln.startswith('decimate')][-3:]))
    if r.returncode or not any(ln.startswith('decimate done:') for ln in r.stdout.splitlines()):
        # (stopped part way: the export it was in may be cut short, so none of this run's is taken)
        sys.exit('decimate: Blender stopped before the end of the batch (exit %d, see %s); nothing published' % (
            r.returncode, work / 'decimate.log'))
    for j in jobs:
        if Path(j['dst']).exists():     # (a job that failed wrote none: the converter keeps the original)
            os.replace(j['dst'], j['final'])


def meshcache_dir(work):
    """Per-mesh collision results a work folder's builds keep: walk LODs and frame pieces."""
    return work / 'meshcache'


def kept_get(folder, key):
    """A kept result (JSON) by its key, or None."""
    try:
        return json.loads((folder / key[:2] / (key + '.json')).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def kept_put(folder, key, value):
    """Keep a result (JSON) under its key: written whole, then renamed (another process's same entry stands)."""
    f = folder / key[:2] / (key + '.json')
    f.parent.mkdir(parents=True, exist_ok=True)
    tmp = f.with_suffix('.tmp%d' % os.getpid())
    tmp.write_text(json.dumps(value), encoding='utf-8')
    try:
        os.replace(tmp, f)
    except OSError:
        tmp.unlink()


def stage_build(work, centre, radius, a):
    import cp77mat
    from nier2gltf import headroom
    tile, merge = getattr(a, 'tile', None), getattr(a, 'merge', False)
    # --base-only (with --merge): what a build of the tiles holds besides their collision chunks (cp2gltf.py chunks):
    # the boundary ring and lid, lights, spawns, in the area's frame -> tiles/<tag>/chunks/base.glb
    base_only = getattr(a, 'base_only', False)
    if base_only and not merge:
        sys.exit('build --base-only needs --merge')
    lights = []
    if not tile:                       # the map's lights: added after a tile's part has been written
        lights = [L for L in json.load(open(work / 'placements.json', encoding='utf-8')).get('lights', [])
                  if in_area(L['pos'][0], L['pos'][1], centre, radius, L['pos'][2])]
        lights += entity_lights(work, centre, radius)
    mats = cp77mat.Materials(work / 'meshes', size=a.texsize, bake_dir=work / 'bake')
    mats.collision = a.no_render        # (the gameplay layer's kinds as tag 11's: look fixes leave collision alone)
    use_tiles_ad_pool(work, a, mats)
    stats = {k: 0 for k in ('placements', 'dupes', 'skipped_prefix', 'missing_mesh', 'small', 'flat_kept', 'render_tris',
                            'col_tris', 'materials', 'skip_materials', 'lights')}
    if (tile or merge) and not a.no_render:
        sys.exit('build --tile / --merge: the gameplay layer only (--no-render)')
    # --inst-split DIR: the tile's part goes to DIR/parts/<tile>.npz with today's arrays unchanged (col in the same
    # order, the same boxes) plus each triangle's band and candidate, the candidates (rules 1-5 of STEP0-FORMATS.md
    # section 1) and their prototypes; the chunks stage applies rule 6 over every tile's candidates
    isplit = getattr(a, 'inst_split', None)
    if isplit and not tile:
        sys.exit('build --inst-split: a tile\'s part only (--tile)')
    inst_cand, inst_proto, col_band, col_cand = [], {}, [], []
    door_leaves = {}                    # candidate door -> its leaves (p, ml, G): door_test decides after the loop
    inst_rej = {}
    # a tile's placements only (its part: tiles/<tag>/build/<tile>.npz), or none: the parts joined (--merge)
    plan = ([] if merge else make_plan(work, centre, radius, a, stats, placements=[
        q for q in read_records(tile_dir(work, a.tiles_tag) / (tile + '.json')) if not q.get('proxy')] if tile else None))
    tri_total = sum(ml.tris(lv) for p, ml, m, size, lv in plan)
    print(f'build: {len(plan)} placements, {tri_total:,} render triangles before material skips')
    if a.dry_run:
        print('  ' + ', '.join(f'{k} {v}' for k, v in stats.items()))
        by = {}
        for p, ml, m, size, lv in plan:
            e = by.setdefault(p['mesh'], [0, 0, size, lv, ml.levels])
            e[0] += 1
            e[1] += ml.tris(lv)
        print('  top meshes by triangles (count, tris, size m, level, levels):')
        for k, v in sorted(by.items(), key=lambda kv: -kv[1][1])[:40]:
            print(f'  {v[0]:5d} {v[1]:9,d} {v[2]:6.1f} {v[3]} {v[4]} {k}')
        return

    origin = np.array([centre[0], centre[1], a.floor])
    w = gltfio.Writer()
    g_render, g_col, g_ent = w.node('render'), w.node('collision'), w.node('entities')
    mat_cache, mesh_cache = {}, {}
    kcache = cp77mat.ResultCache(matcache_dir(work), mats, 'build kind')

    asked = []                          # (a kept walk LOD or frame split: the materials its computation asks for)

    def material(ml, name):
        for q in asked:
            q.append(name)
        rec = ml.materials.get(name)
        if rec is None:
            return None
        key = material_key(ml, name, rec)
        if key not in mat_cache and a.no_render:
            # gameplay layer: only the kind decides what collides (kept on disk)
            kept = kcache.get(rec)
            if kept is None:
                with mats.track() as deps:
                    kept = {'kind': mats.convert(rec)['kind']}
                kcache.put(rec, kept, deps)
            if kept['kind'] == 'skip':
                stats['skip_materials'] += 1
                mat_cache[key] = None
                return None
            mat_cache[key] = (None, kept['kind'])
        if key not in mat_cache:
            got = mats.convert(rec)
            if got['kind'] == 'skip':
                stats['skip_materials'] += 1
                mat_cache[key] = None
                return None
            stem = re.sub(r'[^a-z0-9_]+', '_', (rec['BaseMaterial'] if rec['BaseMaterial'].endswith('.mi') else
                                                 rec['Name']).split('\\')[-1].lower().rsplit('.', 1)[0])
            nm = 'cp_%s_%d' % (stem[:40], len(mat_cache))
            img = got['image']
            kind = got['kind']
            if a.no_render:
                # gameplay layer: only the kind decides what collides
                mat_cache[key] = (None, kind)
                return mat_cache[key]
            ex = dict(got['extras'])
            alpha = None
            if kind == 'blend':
                ex['lp_blend'] = 'alpha'
                alpha = 'BLEND'
            elif kind == 'cutout':
                alpha = 'MASK'
            data = None
            if img is not None:
                data = png_bytes(img) if kind in ('blend', 'cutout') or img.shape[0] <= 8 else jpg_bytes(img)
            normal = w.texture(png_bytes(got['normal'])) if got.get('normal') is not None and kind == 'opaque' else None
            orm = w.texture(jpg_bytes(got['orm'])) if got.get('orm') is not None and kind == 'opaque' else None
            mi = w.material(nm, image=data, mime='image/png' if data and data[:4] == b'\x89PNG' else 'image/jpeg',
                            extras=ex, double_sided=kind in ('cutout', 'blend'), alpha=alpha, normal=normal, orm=orm)
            mat_cache[key] = (mi, kind)
            stats['materials'] += 1
        return mat_cache[key]

    def gltf_mesh(ml, app, level, depot):
        key = (depot, app, level)
        if key in mesh_cache:
            return mesh_cache[key]
        names = ml.material_names(app)
        prims = []
        for ci, ps in ml.level_chunks(level):
            got = material(ml, names[ci]) if ci < len(names) else None
            if got is None:
                continue
            mi, kind = got
            for pos, nrm, uv, tris in ps:
                prims.append({'position': pos, 'normal': nrm, 'uv': uv, 'indices': tris, 'material': mi})
        mesh_cache[key] = (w.mesh('%s_%s_%d' % (depot.split('\\')[-1][:-5], app, level), prims) if prims else None,
                           sum(len(p['indices']) for p in prims))
        return mesh_cache[key]

    import stagekey
    here = Path(__file__).resolve()
    # (a frame's pieces are frame_split's, through covered and vertical_hits: their code is in its key too)
    mesh_code = {k: stagekey.code_key(here, ['stage_build.' + f for f in fs] + ['MeshLods'], entry=None)
                 for k, fs in (('walk_level', ['walk_level']),
                               ('frame_pieces', ['frame_pieces', 'frame_split', 'covered', 'vertical_hits']))}
    mesh_dir = meshcache_dir(work)

    def kind_of(ml, name):
        """A material's kind for a key, counting nothing: the build's own, else the kept one (converted once)."""
        rec = ml.materials.get(name)
        if rec is None:
            return None
        key = material_key(ml, name, rec)
        if key in mat_cache:
            return mat_cache[key][1] if mat_cache[key] else 'skip'
        kept = kcache.get(rec)
        if kept is None:
            with mats.track() as deps:
                kept = {'kind': mats.convert(rec)['kind']}
            kcache.put(rec, kept, deps)
        return kept['kind']

    def mesh_key(what, ml, app, *args):
        """A per-mesh result's key on disk: its code (with MeshLods'), numpy's version, the mesh's two
        files, its chunks' material kinds and the result's own arguments as computed (the build's memo rounds the
        scale; its first placement's exact one is what the result is of)."""
        def stat(f):
            try:
                st = f.stat()
                return [st.st_size, st.st_mtime_ns]
            except OSError:
                return None
        names = ml.material_names(app)
        kinds = [kind_of(ml, names[ci]) if ci < len(names) else None for ci in sorted(ml.chunks)]
        files = [stat(ml.src), stat(ml.work / 'meshes' / (ml.rel + '.Material.json'))]
        return hashlib.sha1(json.dumps([mesh_code[what], np.__version__, what, ml.rel, app, files, kinds] + list(args))
                            .encode()).hexdigest()[:24]

    def collision_level(ml, cap):
        """The finest LOD of at most `cap` triangles (the shape the player sees up close where walking meets it, not
        the decimated copy), else the coarsest."""
        for lv in ml.levels:
            if ml.tris(lv, original=True) <= cap:
                return lv
        return ml.levels[-1]

    walk_cache = {}
    a_walk_tol, a_walk_share = getattr(a, 'col_walk_tol', 0.3), getattr(a, 'col_walk_share', 0.95)

    def walk_level(ml, app, cap, cutout=False, L=None):
        """The coarsest LOD of at most `cap` triangles that keeps the mesh's walkable surfaces where LOD 0 has them:
        points on LOD 0's up-facing triangles (area-weighted, at most 1,500), each within --col-walk-tol (m) of an
        up-facing triangle of the LOD over it, for --col-walk-share of them. A coarsest LOD that dropped a tank's top or
        a terrain mound's slope made most of Watson's falls; a LOD that keeps the floors keeps the brush
        count of the coarse one. None found: the finest LOD under the cap (collision_level), counted as walk_fallback.
        L: the placement's 3x3 (glTF frame), so the test is made as placed (a tilted or stretched
        placement was once judged in the mesh's own frame). A turn about the vertical with a uniform scale keeps the
        mesh-frame test (cached per mesh and scale, the tolerance scaled); any other placement is tested transformed."""
        L = np.eye(3) if L is None else np.asarray(L, np.float64)
        s = np.linalg.norm(L, axis=0)
        yaw_only = (np.allclose(s, s.mean(), rtol=0.01) and abs(L[1, 1] / max(s.mean(), 1e-9)) > 0.9998
                    and abs(L[0, 1]) + abs(L[2, 1]) + abs(L[1, 0]) + abs(L[1, 2]) < 0.03 * s.mean())
        if yaw_only:
            X, tol = None, a_walk_tol / max(float(s.mean()), 1e-6)
            key = (id(ml), app, cutout, round(float(s.mean()), 2))
        else:
            X, tol = L, a_walk_tol
            key = (id(ml), app, cutout, tuple(np.round(L, 3).ravel()))
            stats['walk_transformed'] = stats.get('walk_transformed', 0) + 1
        if key in walk_cache:
            return walk_cache[key]
        dk = mesh_key('walk_level', ml, app, cutout, cap, a_walk_tol, a_walk_share, tol,
                      None if X is None else X.tolist())
        kept = kept_get(mesh_dir, dk)
        if kept is not None:
            for nm in kept['asked']:
                material(ml, nm)
            if kept['fallback']:
                stats['walk_fallback'] = stats.get('walk_fallback', 0) + 1
            walk_cache[key] = kept['level']
            return kept['level']
        asked.append([])
        fell = stats.get('walk_fallback', 0)

        def up(lv):
            T = collision_tris(ml, app, None, lv, cutout=cutout)
            if T is None or not len(T):
                return np.zeros((0, 3, 3))
            if X is not None:
                T = T @ X.T                             # as placed (rotation and scale; translation doesn't matter)
            n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
            a = np.linalg.norm(n, axis=1)
            keep = (a > 1e-9) & (np.abs(n[:, 1]) > 0.5 * np.maximum(a, 1e-12))       # glTF: +Y up (either side)
            return T[keep].astype(np.float64)
        ref = up(ml.levels[0])
        choice = collision_level(ml, cap)
        if len(ref):
            area = 0.5 * np.linalg.norm(np.cross(ref[:, 1] - ref[:, 0], ref[:, 2] - ref[:, 0]), axis=1)
            rng = np.random.default_rng(0)
            k = rng.choice(len(ref), min(1500, len(ref)), p=area / area.sum())
            r1, r2 = rng.random(len(k)), rng.random(len(k))
            s1 = np.sqrt(r1)
            pts = (ref[k, 0] * (1 - s1)[:, None] + ref[k, 1] * (s1 * (1 - r2))[:, None] + ref[k, 2] * (s1 * r2)[:, None])
            for lv in reversed(ml.levels):              # coarsest first
                if ml.tris(lv, original=True) > cap:
                    continue
                T = up(lv)
                if not len(T):
                    continue
                a, b, c = T[:, 0], T[:, 1], T[:, 2]
                ok = np.zeros(len(pts), bool)
                for s0 in range(0, len(T), 4096):        # (samples x triangles in slices: memory)
                    A, B, C = a[s0:s0 + 4096], b[s0:s0 + 4096], c[s0:s0 + 4096]
                    v0, v1 = (B - A)[:, [0, 2]], (C - A)[:, [0, 2]]
                    v2 = pts[:, None, [0, 2]] - A[None, :, [0, 2]]
                    den = v0[:, 0] * v1[:, 1] - v1[:, 0] * v0[:, 1]
                    with np.errstate(divide='ignore', invalid='ignore'):
                        u = (v2[..., 0] * v1[:, 1] - v1[:, 0] * v2[..., 1]) / den
                        v = (v0[:, 0] * v2[..., 1] - v2[..., 0] * v0[:, 1]) / den
                    inside = (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6) & (np.abs(den) > 1e-12)
                    y = A[None, :, 1] + u * (B - A)[None, :, 1] + v * (C - A)[None, :, 1]
                    ok |= np.any(inside & (np.abs(y - pts[:, None, 1]) <= tol), 1)
                if ok.mean() >= a_walk_share:
                    choice = lv
                    break
            else:
                stats['walk_fallback'] = stats.get('walk_fallback', 0) + 1
        walk_cache[key] = choice
        kept_put(mesh_dir, dk, {'level': int(choice), 'fallback': stats.get('walk_fallback', 0) != fell,
                                'asked': asked.pop()})
        return choice

    def col_level(ml, app, cutout=False, L=None):
        """the LOD a large placement collides as: --col-walk-lod's walkable-preserving one (judged as placed: L),
        else collision_level"""
        if a.col_walk_lod:
            return walk_level(ml, app, a.col_tris, cutout, L)
        return collision_level(ml, a.col_tris)

    def collision_tris(ml, app, deform=None, level=None, cutout=False):
        """Opaque chunks of one LOD (the coarsest by default), with cutout: alpha-tested ones too, glTF-local
        triangles (k, 3, 3); bent first for a bended node."""
        names = ml.material_names(app)
        out = []
        level = ml.levels[-1] if level is None else level
        for ci, ps in sorted((ci, ps) for ci, (mask, ps) in ml.chunks.items() if mask >> level & 1):
            if ci >= len(names):
                continue
            got = material(ml, names[ci])
            if got is None or got[1] not in (('opaque', 'emissive', 'cutout') if cutout else ('opaque', 'emissive')):
                continue
            for pos, nrm, uv, tris in ps:
                if deform:
                    pos = bend(pos @ G2C.T, None, deform, *ml.ylim)[0] @ G2C
                out.append(pos[tris])
        return np.concatenate(out) if out else None

    to_g = CP_TO_GLTF
    from_g = np.linalg.inv(CP_TO_GLTF)
    col_world = []
    box_mi = w.material('collision_box', extras={'surfaceparms': []})
    box_cache, box_world, boxes = {}, [], []

    def add_box(depot, lo, hi, G, key=None):
        # a prop's one brush, its local bounding box (or a piece of it: key names the piece), placed by G (glTF space)
        c = np.array([[x, y, z] for y in (lo[1], hi[1]) for x, z in
                      ((lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2]))])
        key = key or depot
        if key not in box_cache:
            box_cache[key] = w.mesh('box_' + depot.split('\\')[-1][:-5] + ('' if key == depot else '_%d' % len(box_cache)),
                                    [{'position': c, 'indices': BOX_FACES, 'material': box_mi}])
        if not base_only:
            w.node('b%d' % len(boxes), mesh=box_cache[key], matrix=G, parent=g_col,
                   extras={'lp': 'collision', 'contents': ['solid', 'detail']})
        box_world.append((c @ G[:3, :3].T + G[:3, 3])[BOX_FACES])
        boxes.append((depot, lo, hi, G))
        stats['col_boxes'] = stats.get('col_boxes', 0) + 1

    frame_cache = {}

    def covered(T2, P):
        """Which points P (n, 2) lie in any of the triangles T2 (k, 3, 2) (either winding; edge-on ones skipped)."""
        a0, b0, c0 = T2[:, 0], T2[:, 1], T2[:, 2]
        area = (b0[:, 0] - a0[:, 0]) * (c0[:, 1] - a0[:, 1]) - (b0[:, 1] - a0[:, 1]) * (c0[:, 0] - a0[:, 0])
        keep = np.abs(area) > 1e-8
        a0, b0, c0 = a0[keep], b0[keep], c0[keep]
        out = np.zeros(len(P), bool)
        for i in range(0, len(a0), 4096):
            A, B, C = a0[i:i + 4096, None], b0[i:i + 4096, None], c0[i:i + 4096, None]
            Q = P[None]

            def side(U, V):
                return (V[..., 0] - U[..., 0]) * (Q[..., 1] - U[..., 1]) - (V[..., 1] - U[..., 1]) * (Q[..., 0] - U[..., 0])
            d1, d2, d3 = side(A, B), side(B, C), side(C, A)
            out |= (((d1 >= 0) & (d2 >= 0) & (d3 >= 0)) | ((d1 <= 0) & (d2 <= 0) & (d3 <= 0))).any(0)
        return out

    def vertical_hits(T, l, r, u, ws):
        """The heights (axis 1) where vertical lines at (u on axis l, each w of ws on axis r) cross the triangles T."""
        P2 = T[:, :, [l, r]]
        a0, b0, c0 = P2[:, 0], P2[:, 1], P2[:, 2]
        den = (b0[:, 1] - c0[:, 1]) * (a0[:, 0] - c0[:, 0]) + (c0[:, 0] - b0[:, 0]) * (a0[:, 1] - c0[:, 1])
        ok = np.abs(den) > 1e-10
        out = []
        for w in ws:
            w0 = ((b0[:, 1] - c0[:, 1]) * (u - c0[:, 0]) + (c0[:, 0] - b0[:, 0]) * (w - c0[:, 1])) / np.where(ok, den, 1.0)
            w1 = ((c0[:, 1] - a0[:, 1]) * (u - c0[:, 0]) + (a0[:, 0] - c0[:, 0]) * (w - c0[:, 1])) / np.where(ok, den, 1.0)
            w2 = 1.0 - w0 - w1
            k = ok & (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            out.append(w0[k] * T[k, 0, 1] + w1[k] * T[k, 1, 1] + w2[k] * T[k, 2, 1])
        return np.concatenate(out) if out else np.zeros(0)

    def frame_pieces(ml, depot, app, G):
        """A walk-through frame's box split around its opening: [(lo, hi), ...] in the mesh's frame, or None. Door
        frames, sliding-door casings and checkpoint frames fill most of their box, so as one box they were a slab
        across their own opening (Misty's door and a checkpoint were blocked; door_frame_single_a
        alone was 2,449 boxes in one tile set). Rays along a horizontal axis through LOD 0 at 0.3-1.75 m over the bottom find
        the widest run clear at every height; at least 0.85 m (a player) makes it an opening. The pieces: a post either
        side at full height and depth, and the lintel from the first hit over 1.75 m in the middle of the opening."""
        s = np.linalg.norm(G[:3, :3], axis=0)
        if s.min() < 1e-6 or G[1, 1] / s[1] < 0.95:
            return None                                 # upright only
        lo, hi = ml.lo, ml.hi
        if (hi[1] - lo[1]) * s[1] < 1.9:
            return None
        key = (depot, app, tuple(np.round(s, 3)))
        if key in frame_cache:
            return frame_cache[key]
        frame_cache[key] = None
        dk = mesh_key('frame_pieces', ml, app, s.tolist())
        kept = kept_get(mesh_dir, dk)
        if kept is not None:
            for nm in kept['asked']:
                material(ml, nm)
            if kept['pieces'] is not None:
                frame_cache[key] = [(np.array(plo, kept['dtype']), np.array(phi, kept['dtype']))
                                    for plo, phi in kept['pieces']]
            return frame_cache[key]
        asked.append([])
        try:
            got = frame_split(ml, app, s, lo, hi)
        finally:
            q = asked.pop()
        frame_cache[key] = got
        kept_put(mesh_dir, dk, {'asked': q, 'dtype': str(lo.dtype), 'pieces': None if got is None else
                                [[plo.tolist(), phi.tolist()] for plo, phi in got]})
        return got

    def frame_split(ml, app, s, lo, hi):
        """frame_pieces' rays through LOD 0: the pieces, or None."""
        T = collision_tris(ml, app, None, ml.levels[0])
        if T is None:
            return None
        hs = np.array([0.3, 0.7, 1.1, 1.5, 1.75])
        for r in (2, 0):
            l = 2 - r
            if (hi[l] - lo[l]) * s[l] < 0.85:
                continue
            # a ray along axis r at (u, v) hits a triangle when (u, v) lies in its projection on the l-y plane
            T2 = T[:, :, [l, 1]]
            du = 0.05 / s[l]
            us = np.arange(lo[l] + du / 2, hi[l], du)
            vs = lo[1] + hs / s[1]
            hit = covered(T2, np.stack(np.meshgrid(us, vs, indexing='ij'), -1).reshape(-1, 2)).reshape(len(us), len(vs))
            clear = ~hit.any(1)
            best, run, start = (0, -1), 0, 0
            for i, ok in enumerate(clear):
                if ok:
                    if run == 0:
                        start = i
                    run += 1
                    if run > best[1] - best[0] + 1:
                        best = (start, i)
                else:
                    run = 0
            if best[1] < 0 or (best[1] - best[0] + 1) * 0.05 < 0.85:
                continue
            u0, u1 = us[best[0]] - du / 2, us[best[1]] + du / 2
            um, top = (u0 + u1) / 2, None
            hh = np.arange(1.8, (hi[1] - lo[1]) * s[1], 0.05)
            if len(hh):
                up = covered(T2, np.stack([np.full(len(hh), um), lo[1] + hh / s[1]], -1))
                if up.any():
                    top = lo[1] + hh[np.argmax(up)] / s[1]
            # the surfaces a vertical line meets in the middle of the opening, across its depth: a lying pipe's walls
            # run along the rays, edge-on, so the rays saw its inside as an opening and no lintel (a player fell into sewer_pipe_a_straight_l300_ext: two side posts and nothing over or under). The
            # lowest over 1.75 m is the lintel when the rays found none; the highest under 1 m is a sill.
            ys = vertical_hits(T, l, r, um, lo[r] + (hi[r] - lo[r]) * np.array([0.1, 0.3, 0.5, 0.7, 0.9]))
            over = ys[ys > lo[1] + 1.75 / s[1]]
            if top is None and len(over):
                top = float(over.min())
            under = ys[ys < lo[1] + 1.0 / s[1]]
            sill = float(under.max()) if len(under) else None
            pieces = []
            for a0, a1 in ((lo[l], u0), (u1, hi[l])):
                if a1 - a0 > 1e-3:
                    plo, phi = lo.copy(), hi.copy()
                    plo[l], phi[l] = a0, a1
                    pieces.append((plo, phi))
            if top is not None:
                plo, phi = lo.copy(), hi.copy()
                plo[l], phi[l], plo[1] = u0, u1, top
                pieces.append((plo, phi))
            if sill is not None:
                plo, phi = lo.copy(), hi.copy()
                plo[l], phi[l], phi[1] = u0, u1, sill
                pieces.append((plo, phi))
            # no flat piece (a lintel found at the box's top came out 0 m high: a degenerate brush for q3map2)
            return [(plo, phi) for plo, phi in pieces if np.all((phi - plo) * s > 0.01)]
        return None

    def inst_place(p, G, size, path, level, T0, whole, n):
        """--inst-split: one mesh-collision placement's n triangles (already in col_world): their band, and the
        placement as a candidate when rules 1-5 hold (path, not bent, unit scale, axis or yaw, nothing clipped)."""
        band = inst_band(path, size, bool(p.get('deform')), a.col_box)
        why, frame, cand = None, None, -1
        if path == 'cutout':
            why = 'cutout'
        elif p.get('deform'):
            why = 'bent'
        else:
            frame, why = inst_frame(G[:3, :3])
        if why is None and not whole:
            why = 'clipped'
        if why is None:
            key = (p['mesh'], p['app'], int(level), path)
            k16 = inst_key16(key)
            if k16 not in inst_proto:
                # in the mesh's own frame on its own grid (the same cell as the chunks)
                inst_proto[k16] = (key, band, cluster_tris(np.asarray(T0, np.float64), a.col_cell))
            if inst_proto[k16][1] != band:
                sys.exit('inst-split: key %s placed in two bands' % k16)
            if len(inst_proto[k16][2]):
                cand = len(inst_cand)
                inst_cand.append((k16, G.copy(), frame, band, n, size))
            else:
                why = 'empty'
        if why is not None:
            inst_rej[why] = inst_rej.get(why, 0) + 1
            inst_rej[why + '_tris'] = inst_rej.get(why + '_tris', 0) + n
        col_band.append(np.full(n, INST_BANDS.index(band), np.uint8))
        col_cand.append(np.full(n, cand, np.int32))

    for n, (p, ml, m, size, level) in enumerate(plan):
        M = np.eye(4)
        M[:3, :4] = m
        M[:3, 3] -= origin
        G = to_g @ M @ from_g
        if not a.no_render:
            mesh, tris = gltf_mesh(ml, p['app'], level, p['mesh'])
            if mesh is None:
                continue
            w.node('p%d' % n, mesh=mesh, matrix=G, parent=g_render,
                   extras={'lp': 'render', 'cp_mesh': p['mesh'], 'cp_type': p['type'], 'cp_app': p['app']})
            stats['render_tris'] += tris
        stats['placements'] += 1
        dk = door_leaf(p) if tile and not isplit else None
        if dk:                          # (a candidate door's leaf: collides only if door_test keeps it closed)
            door_leaves.setdefault(dk, []).append((p, ml, G))
            continue
        prop_mesh = None
        ipath = ilevel = None           # (--inst-split: the branch that made T and its LOD)
        fname = p['mesh'].split('\\')[-1].lower()
        low = (G @ np.r_[(ml.lo + ml.hi) / 2, 1.0])[1] < a.col_height
        if a.col_names and COL_NONE_NAMES.search(fname):
            stats['col_none_named'] = stats.get('col_none_named', 0) + 1
            continue
        if a.col_names and size >= a.col_min and low and COL_HULL_NAMES.search(fname):
            ilevel = col_level(ml, p['app'], L=G[:3, :3])
            T = collision_tris(ml, p['app'], p.get('deform'), ilevel)
            if T is not None:
                prop_mesh = hull_tris(T)
                ipath = 'hull'
                stats['col_hulls'] = stats.get('col_hulls', 0) + 1
        elif a.col_names and a.col_min <= size < a.col_box and low and COL_THIN_NAMES.search(fname):
            # its own coarsest mesh; past --col-thin-tris nothing (a box hangs to the lowest edge: head height)
            if ml.tris(ml.levels[-1], original=True) <= a.col_thin_tris:
                prop_mesh = collision_tris(ml, p['app'], p.get('deform'), ml.levels[-1])
            if prop_mesh is None:
                stats['col_thin_dropped'] = stats.get('col_thin_dropped', 0) + 1
                continue
            ipath, ilevel = 'thin', ml.levels[-1]
            stats['col_thin_meshes'] = stats.get('col_thin_meshes', 0) + 1
        elif a.col_min <= size < a.col_box and low:
            # a prop collides as its mesh where its bounding box would be a wall the eye does not see: open, thin
            # things (railings, fences, stairs, benches, arches) whose surface fills little of their box. The name is
            # the file's own (the depot path's `\architecture\` matched "arch"); a named prop gets its mesh up to
            # --col-name-tris even when --col-prop-tris is 0 (stairs as boxes were blocks to climb)
            walked = COL_WALK_NAMES.search(fname) is not None
            named = walked or (a.col_prop_tris > 0 and COL_MESH_NAMES.search(fname) is not None)
            cap = max(a.col_prop_tris, a.col_name_tris if walked else 0)
            lv = collision_level(ml, cap)
            if cap and ml.tris(lv, original=True) <= cap:
                T = collision_tris(ml, p['app'], p.get('deform'), lv)
                if T is not None:
                    e = np.maximum(ml.hi - ml.lo, 1e-3)
                    box_area = 2.0 * (e[0] * e[1] + e[1] * e[2] + e[0] * e[2])
                    area = 0.5 * np.linalg.norm(np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]), axis=1).sum()
                    if named or (ml.tris(lv, original=True) <= a.col_prop_tris and area < a.col_prop_fill * box_area):
                        prop_mesh = T
                        ipath, ilevel = 'named' if named else 'prop', lv
                        if named:
                            stats['col_named_meshes'] = stats.get('col_named_meshes', 0) + 1
        if size >= a.col_box or prop_mesh is not None:
            if prop_mesh is not None:
                T = prop_mesh
            else:
                ipath, ilevel = 'mesh', col_level(ml, p['app'], L=G[:3, :3])
                T = collision_tris(ml, p['app'], p.get('deform'), ilevel)
            if T is None and prop_mesh is None and flat_placement(ml, m, a.flat_height):
                # a flat piece drawn only in alpha-tested materials is a floor all the same: lawns over the ground
                # (the Watson slice's lawn_squ_3m, scaled to 6.4 m, left a 107 m2 patch with nothing under it), grates
                ipath, ilevel = 'cutout', col_level(ml, p['app'], True, G[:3, :3])
                T = collision_tris(ml, p['app'], p.get('deform'), ilevel, cutout=True)
                stats['col_flat_cutout'] = stats.get('col_flat_cutout', 0) + (T is not None)
            elif T is None and prop_mesh is None and a.col_cutout_large:
                # a large piece drawn only in alpha-tested materials: terrain mounds (Watson's landfill_*_lp, 7,000 m2
                # a player fell through) and planters. A tree keeps its opaque trunk and so never comes here.
                ipath, ilevel = 'cutout', col_level(ml, p['app'], True, G[:3, :3])
                T = collision_tris(ml, p['app'], p.get('deform'), ilevel, cutout=True)
                stats['col_large_cutout'] = stats.get('col_large_cutout', 0) + (T is not None)
            if T is not None:
                T0 = T                          # (--inst-split: the prototype's own frame, before G)
                T = T @ G[:3, :3].T + G[:3, 3]
                clip = T[..., 1].min(1) < a.col_height
                T = cluster_tris(T[clip], a.col_cell)
                if len(T):
                    col_world.append(T)
                    if prop_mesh is not None:
                        stats['col_prop_meshes'] = stats.get('col_prop_meshes', 0) + 1
                    if isplit:
                        inst_place(p, G, size, ipath, ilevel, T0, bool(clip.all()), len(T))
        elif size >= a.col_min and (G @ np.r_[(ml.lo + ml.hi) / 2, 1.0])[1] < a.col_height:
            pieces = None if (isplit or p.get('deform')) else frame_pieces(ml, p['mesh'], p['app'], G)
            if pieces:
                for q, (plo, phi) in enumerate(pieces):
                    add_box(p['mesh'], plo, phi, G, key=(p['mesh'], p['app'], tuple(np.round(plo, 4)),
                                                         tuple(np.round(phi, 4))))
                stats['col_frames_split'] = stats.get('col_frames_split', 0) + 1
            else:
                add_box(p['mesh'], ml.lo, ml.hi, G)
    # candidate doors: open where both sides are clear with a floor (door_test) against the tile's own collision, the
    # rest closed as their leaves' boxes. A door within ~2 m of the tile's edge sees only this tile and may stay shut.
    door_open, door_closed = [], {}
    if door_leaves:
        Tc = np.concatenate(col_world).astype(np.float64) if col_world else np.zeros((0, 3, 3))
        tmin, tmax = (Tc.min(1), Tc.max(1)) if len(Tc) else (np.zeros((0, 3)), np.zeros((0, 3)))
        for k in sorted(door_leaves):
            ok, why = door_test(Tc, tmin, tmax, boxes, door_leaves[k])
            if ok:
                door_open.append(k)
            else:
                door_closed[k] = why
                for p, ml, G in door_leaves[k]:
                    add_box(p['mesh'], ml.lo, ml.hi, G)
        stats['doors_open'], stats['doors_closed'] = len(door_open), len(door_closed)
        for why in door_closed.values():
            stats['doors_closed_' + why.replace(' ', '_')] = stats.get('doors_closed_' + why.replace(' ', '_'), 0) + 1
    if tile:
        part = (Path(isplit) / 'parts' if isplit else tile_dir(work, a.tiles_tag) / 'build') / (tile + '.npz')
        part.parent.mkdir(parents=True, exist_ok=True)
        tmp = part.with_name(part.stem + '.tmp.npz')
        extra = {}
        if isplit:
            if sum(len(b) for b in col_band) != sum(len(t) for t in col_world):
                sys.exit('inst-split: a collision triangle without a band (a col_world branch inst_place missed)')
            pk = sorted(inst_proto)
            P = [inst_proto[k][2] for k in pk]
            extra = dict(col_band=np.concatenate(col_band) if col_band else np.zeros(0, np.uint8),
                         col_cand=np.concatenate(col_cand) if col_cand else np.zeros(0, np.int32),
                         cand_key=np.array([c[0] for c in inst_cand], 'U16'),
                         cand_G=np.array([c[1] for c in inst_cand]).reshape(-1, 4, 4),
                         cand_frame=np.array([c[2] for c in inst_cand], 'U4'),
                         cand_band=np.array([INST_BANDS.index(c[3]) for c in inst_cand], np.uint8),
                         cand_tris=np.array([c[4] for c in inst_cand], np.int64),
                         cand_size=np.array([c[5] for c in inst_cand], np.float64),
                         proto_key=np.array(pk, 'U16'),
                         proto_meta=np.array(json.dumps({k: [list(inst_proto[k][0]), inst_proto[k][1]] for k in pk})),
                         proto_off=np.cumsum([0] + [len(t) for t in P]).astype(np.int64),
                         proto_tris=np.concatenate(P) if P else np.zeros((0, 3, 3)),
                         inst_stats=np.array(json.dumps(inst_rej)))
        if door_leaves:                 # (the scene leaves the open doors' leaves out: door_open_keys)
            extra.update(door_open=np.array(json.dumps(door_open)), door_closed=np.array(json.dumps(door_closed)))
        savez_deflated(tmp, col=np.concatenate(col_world) if col_world else np.zeros((0, 3, 3), np.float32),
                 box_depot=np.array([d for d, *_ in boxes], object),
                 box_lohi=np.array([np.r_[lo, hi] for _, lo, hi, _ in boxes]).reshape(-1, 6),
                 box_G=np.array([G for *_, G in boxes]).reshape(-1, 4, 4),
                 stats=np.array(json.dumps(stats)), **extra)
        os.replace(tmp, part)
        print(f'build tile {tile} -> {part}: {sum(len(t) for t in col_world)} collision triangles, {len(boxes)} boxes')
        return
    if merge:
        man = json.load(open(tile_dir(work, a.tiles_tag) / 'tiles.json', encoding='utf-8'))
        for t in man['tiles']:
            f = tile_dir(work, a.tiles_tag) / 'build' / (t['id'] + '.npz')
            if not f.exists():
                sys.exit('build merge: no part for tile %s' % t['id'])
            with np.load(f, allow_pickle=True) as z:
                if len(z['col']):
                    col_world.append(z['col'])
                for d, lh, G in zip(z['box_depot'], z['box_lohi'], z['box_G']):
                    add_box(str(d), lh[:3], lh[3:], G)
                for k, v in json.loads(str(z['stats'])).items():
                    if k != 'col_boxes':
                        stats[k] = stats.get(k, 0) + v
    # collision in one node per 32 m cell: fewer, larger nodes for gltf2map
    if col_world:
        T = np.concatenate(col_world)
        stats['col_tris'] = len(T)
    if col_world and not base_only:
        cell = np.floor(T.mean(1)[:, [0, 2]] / 32.0).astype(int)
        mi = w.material('collision_solid', extras={'surfaceparms': []})
        for c in np.unique(cell, axis=0):
            sel = np.all(cell == c, axis=1)
            pos = T[sel].reshape(-1, 3)
            w.node('col_%d_%d' % tuple(c), mesh=w.mesh('col_%d_%d' % tuple(c), [{
                'position': pos, 'indices': np.arange(len(pos)).reshape(-1, 3), 'material': mi}]),
                parent=g_col, extras={'lp': 'collision', 'shape': 'mesh', 'contents': ['solid']})
    # playable boundary: playerclip ring just inside the kept radius; an area of a box (Watson) a ring along its box,
    # 1 m outside it: a circle through a 2 x 2.9 km box's corners reached +-66,900 u, past q3map2's and the engine's
    # +-64 k world, and q3map2 dropped it
    zs = T[..., 1] if col_world else np.zeros(1)
    clip_mi = w.material('collision_playerclip', extras={'surfaceparms': []})
    top = a.col_height + 2.0
    box_half = AREAS.get(a.area, {}).get('half')
    box_half = tuple(box_half) if isinstance(box_half, (tuple, list)) else None
    ring = (box_ring_boxes(box_half, float(zs.min()) - 20.0, top) if box_half else
            ring_boxes(np.zeros(2), radius - a.clip_inset, float(zs.min()) - 20.0, top))
    for i, box in enumerate(ring):
        pos = (np.c_[box, np.ones(len(box))] @ to_g.T)[:, :3]
        w.node('clip%d' % i, mesh=w.mesh('clip%d' % i, [{'position': pos, 'indices': BOX_FACES, 'material': clip_mi}]),
               parent=g_col, extras={'lp': 'collision', 'contents': ['playerclip']})
    # ceiling: collision stops at --col-height, so nobody may climb past it into the open upper city
    rx = ry = radius - a.clip_inset + 2.0
    if box_half:
        rx, ry = box_half[0] + 3.0, box_half[1] + 3.0          # over the ring (1 m out, 1 m thick)
    lid = np.array([[x, y, z] for z in (top, top + 1.0) for x, y in ((-rx, -ry), (rx, -ry), (rx, ry), (-rx, ry))])
    pos = (np.c_[lid, np.ones(len(lid))] @ to_g.T)[:, :3]
    w.node('ceiling', mesh=w.mesh('ceiling', [{'position': pos, 'indices': BOX_FACES, 'material': clip_mi}]),
           parent=g_col, extras={'lp': 'collision', 'contents': ['playerclip']})
    # water: a swimmable box under each merged patch and a solid floor below it (render: the scene's own quads)
    wq = [] if base_only else water_quads(water_records(work, centre, radius))     # (water: the chunks')
    if wq:
        wat_mi = w.material('collision_water', extras={'surfaceparms': []})
        bed_mi = w.material('collision_water_bed', extras={'surfaceparms': []})
        for i, q in enumerate(wq):
            for tag, z0, z1, contents, mi in (('water', -WATER_DEPTH, 0.0, ['water', 'trans', 'detail'], wat_mi),
                                              ('wbed', -WATER_DEPTH - 1.0, -WATER_DEPTH, ['solid', 'detail'], bed_mi)):
                box = np.array([[x, y, q[0][2] + z] for z in (z0, z1) for x, y, _ in q]) - origin
                pos = (np.c_[box, np.ones(len(box))] @ to_g.T)[:, :3]
                w.node('%s%d' % (tag, i), mesh=w.mesh('%s%d' % (tag, i), [{
                    'position': pos, 'indices': BOX_FACES, 'material': mi}]),
                    parent=g_col, extras={'lp': 'collision', 'contents': contents})
        stats['water_volumes'] = len(wq)
    # lights: CP radius is the reach; q3map2 linear falloff reaches about `light` units
    for i, L in enumerate(lights):
        if L['radius'] < a.light_min_radius:
            continue
        pos = (to_g @ np.r_[np.array(L['pos']) - origin, 1.0])[:3]
        reach = L['radius'] * gltf_upm() * a.light_scale
        w.node('light%d' % i, translation=pos, parent=g_ent, extras={
            'lp': 'entity', 'classname': 'light', 'light': '%d' % round(reach),
            '_color': '%.3f %.3f %.3f' % tuple(c / 255.0 for c in L['color']), 'spawnflags': '1'})
        stats['lights'] += 1
    # spawns: up-facing collision near the centre under open sky (--spawn-sky metres clear above), spread out
    if col_world:
        nrm = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
        area = np.linalg.norm(nrm, axis=1)
        up = (nrm[:, 1] / np.maximum(area, 1e-9) > 0.9) & (area > 0.5)
        cand = T[up].mean(1)
        cand = cand[np.hypot(cand[:, 0], cand[:, 2]) < a.spawn_radius]
        cand = cand[np.abs(cand[:, 1] - np.median(cand[:, 1])) < 4.0]
        # in a fixed order, so the pick does not depend on the order the triangles came in (whole or by tiles)
        cand = cand[np.lexsort(cand.T[::-1])]
        if len(cand) > 4000:
            cand = cand[np.random.default_rng(1).choice(len(cand), 4000, replace=False)]
        over = np.concatenate([T] + box_world) if box_world else T   # roofs are often prop boxes
        # only what reaches the spawn area (candidates within --spawn-radius of the centre, a player's half-width
        # more): the same answers, without sweeping a district's tens of millions of triangles per candidate (a base
        # build once sat an hour in headroom and clearance)
        r = a.spawn_radius + 2.0
        lo_xz, hi_xz = over[:, :, [0, 2]].min(1), over[:, :, [0, 2]].max(1)
        over = over[np.all(hi_xz >= -r, 1) & np.all(lo_xz <= r, 1)]
        cand = cand[headroom(cand, over[:, [0, 2, 1]], low=0.2, high=max(2.5, a.spawn_sky))]
        cand = cand[clearance(cand, over)]
        order = [int(np.argmin(np.hypot(cand[:, 0], cand[:, 2])))] if len(cand) else []
        while len(order) < min(a.spawns, len(cand)):
            d = np.min(np.linalg.norm(cand[:, None] - cand[order][None], axis=2), axis=1)
            order.append(int(np.argmax(d)))
        for k, i in enumerate(order):
            w.node('spawn%d' % k, translation=cand[i] + np.array([0, 1.0, 0]), parent=g_ent, extras={
                'lp': 'entity', 'classname': 'info_player_deathmatch', 'angle': str(90 * (k % 4))})
        stats['spawns'] = len(order)
        for name, cp, yaw in AREAS.get(a.area, {}).get('named_spawns', []):
            g = (CP_TO_GLTF[:3, :3] @ (np.asarray(cp, float) - origin))
            w.node('spawn_' + name, translation=g, parent=g_ent, extras={
                'lp': 'entity', 'classname': 'info_player_deathmatch', 'angle': str(yaw)})
            stats['spawns'] += 1
    out = work / ('%s-%s.glb' % (a.area, a.tag) if a.tag else '%s.glb' % a.area)
    if base_only:
        out = tile_dir(work, a.tiles_tag) / 'chunks' / 'base.glb'
        out.parent.mkdir(parents=True, exist_ok=True)
    w.save(out, extras={'levelport': {'version': 1, 'units_per_metre': gltf_upm(), 'source_game': 'cyberpunk_2077',
                                      'source': '%s r=%g m' % (a.area, radius),
                                      'worldspawn': {'message': 'Night City: %s' % a.area.title()}}})
    if mats.missing:
        (work / 'build_missing.txt').write_text('\n'.join(sorted(mats.missing)), encoding='utf-8')
    print(f'build -> {out} ({out.stat().st_size / 1e6:.0f} MB)')
    print('  ' + ', '.join(f'{k} {v}' for k, v in stats.items()) + f', missing inputs {len(mats.missing)}')


def gltf_upm():
    import levelport
    return levelport.UNITS_PER_METRE


# ---------------------------------------------------------------------------------------------------- scene

# a LOD draws while the bounding sphere's projected diameter is at least this fraction of the view height; the
# coarsest LOD always 0 (rd-vulkan's r_sceneCullPixels culls below a few pixels)
SCENE_LOD_SCREEN = (0.08, 0.03, 0.01, 0.004, 0.002, 0.001, 0.0005)
G2C = np.array([[1.0, 0, 0], [0, 0, -1.0], [0, 1.0, 0]])   # glTF-local axes -> CP-local (Z up)


def generated_uv(rec, pos):
    """The UVs CP2077 makes for a mesh that carries none (the terrain cells, multilayered_terrain.mt): its own x, y
    (CP-local metres, pos) times UVGenScaleOffset's X, Y plus Z, W, v counted down (uv rows). Measured on 389 cells'
    shared edges, their bakes meet within 3.6 of 255 this way and 26-49 any other (look/render15-20261103/
    terrain_uv.txt). Without them every vertex was (0, 0): one texel of the bake, a flat hangar floor. None where the material has no UVGenScaleOffset."""
    s = ((rec or {}).get('Data') or {}).get('UVGenScaleOffset')
    if not isinstance(s, dict) or not len(pos):
        return None
    u = pos[:, 0] * float(s.get('X', 1.0)) + float(s.get('Z', 0.0))
    v = pos[:, 1] * float(s.get('Y', 1.0)) + float(s.get('W', 0.0))
    return np.stack([u, 1.0 - v], -1)


def not_source(src, dst):
    """dst, or an exit when it is the source pack itself (writing it would truncate the pack being read)"""
    if Path(dst).resolve() == Path(src).resolve():
        sys.exit('%s: the output is the source pack; pass --out another folder (or another --tag)' % dst)
    return dst


def scene_repack(src, dst, name):
    """An older scene pk3 (JPEG/PNG per material) -> distinct DDS images, its scene's material paths rewritten."""
    import io
    import struct
    import zipfile
    from PIL import Image
    zin = zipfile.ZipFile(src)
    blob = bytearray(zin.read('maps/%s.xscn' % name))
    _, _, count, ofs = xscn_head_of(bytes(blob))     # (a version 6 file has ten sections)
    imgs, cache = SceneImages(name), {}
    for m in range(count[5]):
        at = ofs[5] + m * 240
        kind = struct.unpack_from('<I', blob, at + 192)[0]
        for k, role in enumerate('ano'):
            old = bytes(blob[at + 64 * k:at + 64 * (k + 1)]).rstrip(b'\0').decode()
            if not old:
                continue
            if old not in cache:
                im = Image.open(io.BytesIO(zin.read(old)))
                arr = np.asarray(im.convert('RGBA' if im.mode in ('RGBA', 'LA', 'P') else 'RGB'))
                cache[old] = imgs.add(arr, role, alpha=role == 'a' and kind in (1, 2))
            blob[at + 64 * k:at + 64 * (k + 1)] = cache[old].encode().ljust(64, b'\0')
    tmp = Path(str(dst) + '.tmp')       # (renamed when whole)
    with zipfile.ZipFile(tmp, 'w', allowZip64=False) as z:       # (no zip64: the engine cannot open one)
        z.writestr(zipfile.ZipInfo('maps/%s.xscn' % name), bytes(blob), compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
        imgs.write(z)
    zin.close()
    os.replace(tmp, dst)
    print(f'repack -> {dst} ({Path(dst).stat().st_size / 1e6:.0f} MB): {len(imgs.todo)} DDS images for {len(cache)} files')


VCACHE_SIZE = 32     # the post-transform cache the triangle order is tuned for (entries)


def tipsify(tris, nv, k=VCACHE_SIZE):
    """A triangle order for vertex reuse: the locality half of Sander, Nehab and Barczak, "Fast triangle reordering
    for vertex locality and reduced overdraw" (2007). It fans around a vertex, then moves to the one of the fan's
    vertices that stays in a k-entry cache longest, else back along the fans (a dead end) or to the next unused
    vertex. Returns the triangles' indices in their new order; each triangle keeps its own vertex order."""
    T = tris.tolist()
    flat = tris.ravel()
    live = np.bincount(flat, minlength=nv)
    start = np.zeros(nv + 1, np.int64)
    np.cumsum(live, out=start[1:])
    adj = (np.argsort(flat, kind='stable') // 3).tolist()
    st, live = start.tolist(), live.tolist()
    stamp, emitted, dead, out = [0] * nv, [False] * len(T), [], []
    s, cursor = k + 1, 0
    while cursor < nv and live[cursor] == 0:
        cursor += 1
    f = cursor if cursor < nv else -1
    while f >= 0:
        fan = []
        for j in range(st[f], st[f + 1]):
            t = adj[j]
            if emitted[t]:
                continue
            emitted[t] = True
            out.append(t)
            for v in T[t]:
                dead.append(v)
                fan.append(v)
                live[v] -= 1
                if s - stamp[v] > k:
                    stamp[v] = s
                    s += 1
        f, best = -1, -1
        for v in fan:
            if live[v] > 0:
                p = s - stamp[v] if s - stamp[v] + 2 * live[v] <= k else 0
                if p > best:
                    f, best = v, p
        while f < 0 and dead:
            d = dead.pop()
            if live[d] > 0:
                f = d
        if f < 0:
            while cursor < nv and live[cursor] == 0:
                cursor += 1
            f = cursor if cursor < nv else -1
    return np.array(out, np.int64)


def fifo_misses(flat, k=VCACHE_SIZE):
    """Vertex shader runs of an index list through a k-entry FIFO cache (the order's quality, not a GPU's exact one)."""
    from collections import deque
    q, inq, miss = deque(), set(), 0
    for v in flat.tolist():
        if v not in inq:
            miss += 1
            q.append(v)
            inq.add(v)
            if len(q) > k:
                inq.discard(q.popleft())
    return miss


def _vcache_one(job):
    """One geometry: its triangles in tipsify order, its vertices renumbered by first use (unused ones last).
    Returns the new local indices, the vertex permutation (new -> old) and the FIFO misses before and after."""
    loc, nv, k = job
    tris = loc.reshape(-1, 3)
    order = tipsify(tris, nv, k)
    new = tris[order].ravel()
    first = np.full(nv, -1, np.int64)
    seen = new[np.sort(np.unique(new, return_index=True)[1])]
    perm = np.concatenate([seen, np.setdiff1d(np.arange(nv), seen)])
    first[perm] = np.arange(nv)
    new = first[new]
    return new.astype('<u4'), perm, fifo_misses(loc, k), fifo_misses(new, k)


def scene_vcache_blob(blob, workers=12, k=VCACHE_SIZE):
    """An .xscn's geometry in vertex-cache order (tipsify, then the vertices by first use), the meshes' pixels
    unchanged: only geometry whose every part is opaque or cutout and not a masked glow is reordered (a blended or
    added surface's result depends on its triangles' order; with the depth prepass and the lit pass's LESS_OR_EQUAL
    an opaque one's does not, but for triangles of one mesh at the same depth). Each geometry keeps its vertex range
    and index range; the vertices' data is only permuted (their frames were computed before). Returns the new blob
    and stats."""
    import struct
    from collections import Counter
    from multiprocessing import Pool
    _, _, count, ofs = xscn_head_of(bytes(blob))     # (a version 6 file has ten sections)
    blob = bytearray(blob)
    V = np.frombuffer(blob, SCENE_VERT, count[0], ofs[0]).copy()
    I = np.frombuffer(blob, '<u4', count[1], ofs[1]).copy()
    parts = np.frombuffer(blob, np.dtype([('fi', '<u4'), ('ni', '<u4'), ('vo', '<i4'), ('mi', '<u4')]), count[4], ofs[4])
    kinds = [struct.unpack_from('<2I', blob, ofs[5] + m * 240 + 192) for m in range(count[5])]
    stats = Counter()
    groups = {}     # vertex offset -> its index ranges and whether every part using it may be reordered
    for fi, ni, vo, mi in parts.tolist():
        kind, flags = kinds[mi]
        g = groups.setdefault(vo, [set(), True])
        g[0].add((fi, ni))
        g[1] = g[1] and kind in (0, 1) and not flags & 4
    starts = sorted(groups) + [count[0]]
    jobs, where = [], []
    for j, vo in enumerate(starts[:-1]):
        ranges, ok = groups[vo]
        nv = starts[j + 1] - vo
        if not ok:
            stats['kept_order_blend_glow'] += 1
            continue
        if len(ranges) != 1:
            stats['kept_order_shared_vertices'] += 1
            continue
        (fi, ni), = ranges
        loc = I[fi:fi + ni].astype(np.int64)
        if ni % 3 or (len(loc) and loc.max() >= nv):
            stats['kept_order_odd'] += 1
            continue
        jobs.append((loc, nv, k))
        where.append((fi, ni, vo, nv))
    with Pool(workers) as pool:
        results = pool.map(_vcache_one, jobs, chunksize=8)
    for (fi, ni, vo, nv), (new, perm, before, after) in zip(where, results):
        I[fi:fi + ni] = new
        V[vo:vo + nv] = V[vo + perm]
        stats['reordered'] += 1
        stats['tris'] += ni // 3
        stats['misses_before'] += before
        stats['misses_after'] += after
    blob[ofs[0]:ofs[0] + V.nbytes] = V.tobytes()
    blob[ofs[1]:ofs[1] + I.nbytes] = I.tobytes()
    return bytes(blob), stats


def scene_vcache(src, dst, name, workers=12, k=VCACHE_SIZE):
    """A scene pk3 with its geometry in vertex-cache order (scene_vcache_blob); everything else copied."""
    import copy
    import zipfile
    zin = zipfile.ZipFile(src)
    blob, stats = scene_vcache_blob(zin.read('maps/%s.xscn' % name), workers, k)
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(dst, 'w', allowZip64=False) as z:       # (no zip64: the engine cannot open one)
        for info in zin.infolist():
            data = blob if info.filename == 'maps/%s.xscn' % name else zin.read(info.filename)
            z.writestr(copy.copy(info), data)
    t = max(stats['tris'], 1)
    print(f'vcache -> {dst}: {stats["reordered"]} geometries, {stats["tris"]} triangles, vertex runs per triangle '
          f'(FIFO {k}) {stats["misses_before"] / t:.3f} -> {stats["misses_after"] / t:.3f}; kept: '
          + ', '.join(f'{k[11:]} {v}' for k, v in sorted(stats.items()) if k.startswith('kept_order')))


GRADE_LUT = 'base\\weather\\24h_basic\\luts\\cp2077_gen_lut_nge_v017.xbm'   # the master environment's SDR LUT (2.x)


def scene_grade(work, depot):
    """The game's colour grade as maps/<map>.xlut bytes (docs/levelport-scene.md): a 32^3 float LUT from scene light
    in ARRI LogC (CP's CMF_ArriLogC input) to display light, its tone curve included. The master environment
    (cp2077_master_env_*.env, ColorGradingAreaSettings) names it; the weathers leave it alone. Fetched once into
    <work>/grade."""
    import base64
    import struct
    dst = work / 'grade'
    stem = depot.replace('\\', '/').rsplit('/', 1)[-1]
    js = dst / 'json' / (stem + '.json')
    if not js.exists():
        raw = dst / 'raw'
        raw.mkdir(parents=True, exist_ok=True)
        (dst / 'json').mkdir(parents=True, exist_ok=True)
        wolvenkit('unbundle', *archives(), '-r', '^' + re.escape(depot) + '$', '-o', raw, '-v', 'Minimal')
        wolvenkit('convert', 'serialize', raw / depot.replace('\\', '/'), '-o', dst / 'json', '-v', 'Minimal')
    root = json.load(open(js, encoding='utf-8'))['Data']['RootChunk']
    n = int(root['width'])
    blob = root['renderTextureResource']['renderResourceBlobPC']['Data']
    info = blob['header']['textureInfo']
    if (root['height'], root['depth'], root['setup']['rawFormat'], info['type']) != (n, n, 'TRF_HDRFloat', 'TEXTYPE_3D'):
        sys.exit(f'{depot}: not a float 3D LUT')
    lut = np.frombuffer(base64.b64decode(blob['textureData']['Bytes']), '<f4', n * n * n * 4)
    return struct.pack('<4s3I', b'XLUT', 1, n, 1) + lut.astype('<f2').tobytes()


def scene_regrade(work, src, dst, name, depot):
    """An existing scene pk3 with its colour grade written, replaced, or (an empty --grade) removed."""
    import copy
    import zipfile
    entry = 'maps/%s.xlut' % name
    lut = scene_grade(work, depot) if depot else None
    tmp = Path(str(dst) + '.tmp')       # (renamed when whole)
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(tmp, 'w', allowZip64=False) as z:
        for info in zin.infolist():
            if info.filename != entry:
                z.writestr(copy.copy(info), zin.read(info.filename))
        if lut:
            z.writestr(zipfile.ZipInfo(entry), lut, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
    os.replace(tmp, dst)
    print(f'regrade -> {dst}: ' + (f'{depot.rsplit(chr(92), 1)[-1]} ({len(lut)} bytes)' if lut else 'no grade'))


def channel_set(s):
    """A light channel string -> set of names; '0' (no restriction) -> None."""
    s = str(s or '0')
    return None if s == '0' else {c.strip() for c in s.split(',') if c.strip()}


class ChannelVolumes:
    """The light channel volumes as light_box takes them, each computed once: its channels, the inverse of its matrix,
    its padded box, and that box's bounds in CP space with a wide margin, which only rules out a volume that cannot
    hold the light (every light built and inverted every volume's matrix again, 5.9 M
    inverses for Watson's 42,738 lights, 590 of the tiled merge's 1,010 s)."""

    def __init__(self, volumes):
        self.v = []
        self.degenerate = 0         # volumes of a zero scale on an axis: they hold nothing, left out
        for v in volumes:
            M = np.eye(4)
            M[:3] = np.array(v['m']).reshape(3, 4)
            if abs(np.linalg.det(M[:3, :3])) < 1e-9:
                self.degenerate += 1
                continue
            inv = np.linalg.inv(M)
            lo, hi = np.array(v['lo']) - 0.25, np.array(v['hi']) + 0.25
            c = (np.array([[x, y, z, 1.0] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])])
                 @ M.T)[:, :3]
            pad = 0.01 * (1.0 + float(np.abs(c).max()))
            self.v.append((channel_set(v['channels']), inv, lo, hi, c.min(0) - pad, c.max(0) + pad))
        self.bmin = np.array([b[4] for b in self.v]).reshape(-1, 3)
        self.bmax = np.array([b[5] for b in self.v]).reshape(-1, 3)

    def near(self, p):
        """The volumes whose bounds hold p (CP metres), in their order."""
        k = np.nonzero(np.all((self.bmin <= p) & (p <= self.bmax), 1))[0]
        return [self.v[i] for i in k]


def light_box(L, volumes, origin, upm, stats):
    """A light whose channels leave out LC_ChannelWorld lights only inside a channel volume (an interior). The box
    of the smallest such volume holding the light, as the 3x4 map from scene space (Quake units) to its unit cube;
    None when the light is unrestricted, or when no volume of its channels holds it (it stays unconfined)."""
    ch = channel_set(L.get('channel'))
    if ch is None or 'LC_ChannelWorld' in ch:
        return None
    p = np.r_[np.array(L['pos'], float), 1.0]
    best = None
    if not isinstance(volumes, ChannelVolumes):
        volumes = ChannelVolumes(volumes)
    for vch, inv, lo, hi, _, _ in volumes.near(p[:3]):
        if vch is not None and not (ch & vch):
            continue
        q = (inv @ p)[:3]
        if np.all(q >= lo) and np.all(q <= hi):
            size = float(np.prod(np.maximum(hi - lo, 0.01)))
            if best is None or size < best[0]:
                best = (size, inv, lo, hi)
    if best is None:
        stats['channel_unconfined'] += 1
        return None
    _, inv, lo, hi = best
    centre, half = (lo + hi) / 2, np.maximum((hi - lo) / 2, 0.01)
    S = np.eye(4)                                   # scene (Quake) -> CP world
    S[:3, :3] /= upm
    S[:3, 3] = origin
    U = np.eye(4)                                   # volume local -> unit cube
    U[:3, :3] = np.diag(1.0 / half)
    U[:3, 3] = -centre / half
    stats['channel_confined'] += 1
    return (U @ inv @ S)[:3].ravel()


STEAM_DENSITY = (('super_subtle', 0.01), ('subtle', 0.03), ('stronger', 0.15), ('strong', 0.1), ('dense', 0.2))


def sector_fogs(f):
    """A sector's haze candidates (scene_fogs), anywhere in it: [(x, y, z, M as lists, density, mode, soft, kind)] in
    its node order, x y z the node's position (the area test is scene_fogs')."""
    rc = json.load(open(f, encoding='utf-8'))['Data']['RootChunk']
    nodes = rc['nodes']
    out = []
    for r in shown_records(f.parent.parent, f.name.split('.')[0], rc):
        n = nodes[r['NodeIndex']]['Data']
        t, name = n['$type'], cname(n.get('debugName'))
        p = r['Position']
        xyz = (float(p.get('X', 0)), float(p.get('Y', 0)), float(p.get('Z', 0)))
        if t == 'worldStaticFogVolumeNode':
            if not n.get('isVisibleInGame', 1):
                continue
            M = trs(p, r['Orientation'], r['Scale']) @ np.diag([0.5, 0.5, 0.5, 1.0])
            absolute = int(n.get('absolute') or 0)
            dens = float(n.get('densityFactor') or 0.0)
            if dens <= 0.0 or (not absolute and dens <= 1.0):
                continue
            soft = max(float(n.get('blendFalloff') or 0.0), float(n.get('densityFalloff') or 0.0), 0.1)
            out.append(xyz + (M.tolist(), dens, absolute, min(soft, 1.0), 0))
        elif t == 'worldEffectNode' and 'steam' in name.lower():
            m = re.search(r'(\d+(?:p\d+)?)x(\d+(?:p\d+)?)x(\d+)', name)
            w, d, h = ([float(v.replace('p', '.')) for v in m.groups()] if m else [2.0, 2.0, 6.0])
            dens = next((v for k, v in STEAM_DENSITY if k in name.lower()), 0.05)
            c = trs(p, None, None)
            M = c @ np.diag([w / 2, d / 2, h / 2, 1.0])
            M[:3, 3] += M[:3, 2]                # the column rises from the vent
            out.append(xyz + (M.tolist(), dens, 1, 0.5, 1))
    return out


def fogcache_file(work):
    """sector_fogs' results kept by each sector json's stat (kept_fogs)."""
    return work / 'fogcache.json'


def kept_fogs(work):
    """Every sector's haze candidates in the sectors' order, each sector's kept in work/fogcache.json by its json's
    size and time and sector_fogs' code: a scene merge read all 19 GB of sector json for them every time (218 of the tiled merge's 1,010 s)."""
    import stagekey
    code = stagekey.code_key(Path(__file__).resolve(), ['sector_fogs', 'shown_records', 'default_records'], entry=None)
    cf = fogcache_file(work)
    try:
        cache = json.loads(cf.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        cache = {}
    if cache.get('code') != code:
        cache = {'code': code, 'sectors': {}}
    kept, new, out = cache['sectors'], {}, []
    for f in sorted((work / 'json').glob('*.streamingsector.json')):
        st = f.stat()
        e = kept.get(f.name)
        if e is None or e[0] != [st.st_size, st.st_mtime_ns]:
            e = [[st.st_size, st.st_mtime_ns], sector_fogs(f)]
        new[f.name] = e
        out += [tuple(r) for r in e[1]]
    if new != kept:
        tmp = cf.with_suffix('.tmp')
        tmp.write_text(json.dumps({'code': code, 'sectors': new}), encoding='utf-8')
        os.replace(tmp, cf)
    return out


def scene_fogs(work, centre, radius, origin, stats):
    """The haze's local volumes (maps/<map>.xfog, scene_fog.comp): the sectors' worldStaticFogVolumeNode boxes (a unit
    cube scaled by the node; densityFactor is a density per metre when 'absolute', else a multiple of the haze's) and
    their steam effects as soft columns, sized by the effect's name (e_steam_column_3x3x20m: 3 x 3 m, 20 m tall),
    densest at the vent. Each record: the 3x4 map from scene space (Quake units) to the volume's [-1, 1] box, then
    density per metre, mode (0 a multiple, 1 absolute), soft edge share, kind (0 box, 1 column)."""
    import struct
    upm = gltf_upm()
    S = np.eye(4)                                   # scene (Quake) -> CP world
    S[:3, :3] /= upm
    S[:3, 3] = origin
    recs = []
    for x, y, z, M, dens, mode, soft, kind in kept_fogs(work):
        if not in_area(x, y, centre, radius, z):
            continue
        if abs(np.linalg.det(np.array(M)[:3, :3])) < 1e-9:    # (a zero scale on an axis: no volume)
            stats['fog_volumes_degenerate'] += 1
            continue
        recs.append((np.array(M), dens, mode, soft, kind))
        stats['steam_columns' if kind else 'fog_volumes'] += 1
    out = [struct.pack('<4s3I', b'XFOG', 1, len(recs), 0)]
    for M, dens, mode, soft, kind in recs:
        Q = (np.linalg.inv(M) @ S)[:3]              # scene -> the volume's [-1, 1] box
        out.append(struct.pack('<16f', *Q.ravel(), dens, mode, soft, kind))
    return b''.join(out)


def ad_lights_colour(lights, placed, mesh_mats, ad_colour, stats):
    """Ad lights whose colour the game takes from the ad's video at run time (AD_LIGHT_COLOUR stands in) take the
    mean colour of what their own billboard shows: the nearest placement of their node, within 30 m. Returns the
    colours by ad_light_key (ad_lights.json, for --relight)."""
    by_name, out = {}, {}
    for nm, pos, mi in placed:
        cols = [ad_colour[k] for k in mesh_mats.get(mi, ()) if k in ad_colour]
        if cols:
            by_name.setdefault(nm, []).append((pos, np.mean(cols, 0)))
    for L in lights:
        if L.get('kind') != 'ad' or tuple(L['color']) != AD_LIGHT_COLOUR:
            continue
        near = by_name.get(L['name'].split('/')[0], [])
        if not near:
            stats['ad_light_standin'] += 1
            continue
        d, c = min(((np.linalg.norm(pos - np.asarray(L['pos'])), c) for pos, c in near), key=lambda t: t[0])
        if d > 30.0:
            stats['ad_light_standin'] += 1
            continue
        c = c / max(float(c.max()), 1e-4)       # the hue; the light keeps its own power
        L['color'] = [int(round(v)) for v in 255.0 * c ** (1 / 2.2)]
        out[ad_light_key(L)] = L['color']
        stats['ad_light_coloured'] += 1
    return out


def ad_light_key(L):
    return '%s|%.1f|%.1f|%.1f' % (L['name'], *L['pos'])


class SceneSignals:
    """The traffic signals' cycles (cycling WALK / DON'T WALK like the game). From
    signal_groups.json in the work folder (cp77_signals.py builds it from the sectors' compiledTraffic_Intersection programs and each
    placed signal's worldTrafficLightListenerComponent override): per signal its group's program, (colour, seconds) from the intersection's t = 0. Every group of
    an intersection shares that clock; when an intersection's cycle starts is the engine's, so each gets a fixed
    offset from its name. Slot 0 is no signal (the renderer shows it green: a crossing's walking figure).
    maps/<map>.signals (text): `xsig 1 <count>`, then optional `sound <role> <path>` lines (the crossing sounds the
    cgame plays, SIGNAL_SOUND_ROLES; none without --signal-sounds: the crossings are silent), then per slot `<slot>
    <crossing|car> <cycle s> <offset s> <x y z> <n> <G|Y|R> <seconds> ...` (position in game units): the renderer
    lights lamps from it and the cgame plays the crossings' sounds from it, both on the game clock. Format:
    docs/levelport-scene.md, .signals."""

    def __init__(self, groups, centre, radius, sounds=None):
        self.sounds = dict(sounds or {})    # role: game path (SIGNAL_SOUND_ROLES)
        self.sigs = sorted((g for g in groups['signals']
                            if in_area(*g['pos_cp_m'][:2], centre, radius, g['pos_cp_m'][2])),
                           key=lambda g: (g['sector'], g['node_index']))
        self.pos = np.array([g['pos_cp_m'] for g in self.sigs]) if self.sigs else np.zeros((0, 3))

    def __bool__(self):
        return bool(self.sigs)

    def slot(self, ent):
        """A placed entity's slot (1-based) by its position, 0 when it is no signal of the table."""
        if ent is None or not len(self.pos):
            return 0
        d = np.linalg.norm(self.pos - np.asarray(ent, float), axis=1)
        i = int(np.argmin(d))
        return i + 1 if d[i] < 0.05 else 0

    def text(self, origin, upm):
        lines = ['xsig 1 %d' % len(self.sigs)]
        for role in SIGNAL_SOUND_ROLES:
            if self.sounds.get(role):
                assert not any(c.isspace() for c in self.sounds[role]), self.sounds[role]
                lines.append('sound %s %s' % (role, self.sounds[role]))
        for k, g in enumerate(self.sigs):
            prog = g.get('program') or [['GREEN', 60.0]]   # no group: the default colour (green), steady
            cycle = sum(float(d) for _, d in prog)
            name = g.get('intersection_node') or g['group_id'] if g.get('group_id') else '%s/%d' % (g['sector'], g['node_index'])
            offset = int(hashlib.sha1(str(name).encode()).hexdigest()[:8], 16) % max(int(cycle * 1000), 1) / 1000.0
            p = upm * (np.array(g['pos_cp_m']) - origin)
            lines.append('%d %s %.3f %.3f %.1f %.1f %.1f %d %s' % (
                k + 1, 'crossing' if g['kind'] == 'crossing' else 'car', cycle, offset, p[0], p[1], p[2], len(prog),
                ' '.join('%s %.3f' % (c[0], float(d)) for c, d in prog)))
        return '\n'.join(lines) + '\n'


# the crossing sounds a .signals file can name (cg_signals.c): walk's tick bed and chirp, don't walk's tone and chirp
SIGNAL_SOUND_ROLES = ('walk_base', 'walk_layer', 'dontwalk_base', 'dontwalk_layer')


def signal_sounds(folder):
    """--signal-sounds: a game folder holding <role>.wav for each role -> {role: path}; {} for none."""
    folder = (folder or '').strip().strip('/')
    return {r: '%s/%s.wav' % (folder, r) for r in SIGNAL_SOUND_ROLES} if folder else {}


def scene_signals(work, a, centre, radius):
    f = Path(a.signals) if getattr(a, 'signals', None) else work / 'signal_groups.json'
    if not f.exists():
        return None
    return SceneSignals(json.load(open(f, encoding='utf-8')), centre, radius,
                        signal_sounds(getattr(a, 'signal_sounds', None)))


def scene_lights(lights, origin, a, stats, volumes=(), sig=None):
    """The scene's light records. CP lights face their local +Y: spots and area lights are cones of CP's own inner
    and outer angles, spreads past 180 degrees included. Each light carries its share in the haze (scaleVolFog);
    lights that only light volumetric fog (sceneDiffuse 0 and sceneSpecularScale 0) are flagged so (type bit 1), and
    left out when their share is 0. Lights the source shadows (enableLocalShadows) are flagged so (type bit 2): rd-vulkan
    renders static shadow maps for them. Interior (channel) lights carry their volume's box."""
    upm = gltf_upm()
    lrecs = []
    volumes = volumes if isinstance(volumes, ChannelVolumes) else ChannelVolumes(volumes)
    for L in lights:
        if L['radius'] < a.light_min_radius / 4:
            continue
        if L.get('time') == 'never':        # (light_time.py: its timetable is off at every hour)
            stats['lights_never'] += 1
            continue
        fog_only = L.get('diffuse', 1) == 0 and L.get('specular', 100) == 0
        volfog = L.get('volfog', 1.0)
        if fog_only and volfog <= 0:
            stats['fog_only_dropped'] += 1
            continue
        stats['fog_only' if fog_only else 'in_haze' if volfog > 0 else 'not_in_haze'] += 1
        rot = np.array(L['rot']).reshape(3, 3)
        # spreads past 180 degrees are cones too (the ring's haze lights are 220-degree areas facing outward)
        cone = L['type'] in ('LT_Spot', 'LT_Area') and 0 < L['outer'] < 359
        half = math.radians(L['outer'] / 2) if cone else math.pi
        inner = math.radians(min(L['inner'], L['outer']) / 2) if cone and L.get('inner') else half * 0.6
        colour = (np.array(L['color']) / 255.0) ** 2.2
        # an explicit intensity 0 is a light switched off (432 static and 38 instance lights in Watson; they were
        # once lit at 1); an ad light's 0 keeps 1, as its power follows the ad's video at run time
        given = L.get('intensity')
        power = (1.0 if given is None or (not given and L.get('kind') == 'ad') else given) * 2.0 ** L['EV']
        if power <= 0.0:
            stats['lights_off'] += 1
            continue
        if L.get('unit') == 'LU_Nit':
            # luminance over the glowing capsule's surface -> the scene's power: L * area. Not the Lambertian pi * L *
            # area: with the pi, Misty's ground (a solved reference camera) took its hue from two nit tubes (cyan-grey,
            # 0.42 0.31 0.27 in its lower half where CP2077's is 0.51 0.30 0.20, and 69 against 49); without it the
            # hue matches (0.51 0.27 0.21, 58).
            r, length = max(L.get('source_radius', 0.0), 0.005), L.get('capsule', 0.0)
            power *= 2.0 * math.pi * r * length + 4.0 * math.pi * r * r
            stats['nit_lights'] += 1
        stats['lights_' + L.get('kind', 'static')] += 1
        # a signal's lamp: bit 3, its lamp in bits 4-5 and its slot from bit 6 (the renderer switches it with the cycle)
        slot = sig.slot(L.get('signal_ent')) if sig and L.get('lamp') in LAMP_INDEX else 0
        if slot >= 1 << 13:     # (bits 6-18: bit 19 and up are the renderer's and the day/night bits)
            sys.exit('scene: signal slot %d does not fit the light type word (13 bits)' % slot)
        bits = 8 | LAMP_INDEX[L['lamp']] << 4 | slot << 6 if slot else 0
        # night only (bit 20) or day only (bit 21): the renderer's xenv `time` drops them in the other (light_time.py)
        bits |= {'night': 1 << 20, 'day': 1 << 21}.get(L.get('time'), 0)
        stats['signal_lights'] += 1 if slot else 0
        shape = light_shape(L, upm)
        stats['tube_lights'] += 1 if shape[0].any() else 0
        # the reach is CP's from the light's centre; a tube's must cover its ends too
        lrecs.append((upm * (np.array(L['pos']) - origin), upm * L['radius'] + float(np.linalg.norm(shape[0])),
                      # the facing as a unit vector: the rotation's columns carry the placing entity's scale, and the
                      # cone's cosines are tested against a unit direction (712 cones scaled below
                      # their own cos outer lit nothing)
                      colour, power, rot[:, 1] / (float(np.linalg.norm(rot[:, 1])) or 1.0),
                      math.cos(half), math.cos(inner), (1 if cone else 0) | (2 if fog_only else 0) | (4 if L.get('shadows') else 0) | bits,
                      light_box(L, volumes, origin, upm, stats), volfog,
                      (upm * float(L.get('hide') or 0), upm * float(L.get('shadow_fade') or 0),
                       upm * float(L.get('shadow_range') or 0), 1 if L.get('distant') else 0), shape))
    return lrecs


def light_shape(L, upm):
    """A light's shape for version 6's shapes section: (half its tube as a vector in scene units, its source radius in
    scene units). CP2077's area lights with a capsule length are tubes along their local X (they face local +Y): the
    20 m strip over Kabuki's Red Street lies along the street, a portrait billboard's lies upright. rot's columns carry
    the placing entity's scale (billboards up to 20x), which a light's size does not take: a 50 m ad light would be
    1 km. Points, spots and capsule-less areas: no axis. The renderer spreads the record's power along the tube
    (drawn as points, an 8-20 m strip lit the walls beside it several times too bright, twice
    CP2077's brightness)."""
    x = np.array(L['rot'], float).reshape(3, 3)[:, 0]
    radius = upm * max(float(L.get('source_radius') or 0.0), 0.0)
    length = float(L.get('capsule') or 0.0)
    if L['type'] != 'LT_Area' or length <= 0.0:
        return np.zeros(3), radius
    return upm * x / (float(np.linalg.norm(x)) or 1.0) * (length / 2.0), radius


def light_times(work, lights, stats):
    """The lights CP2077 shows only at night (fuse-switched lamps) or only by day (light_time.py): L['time'], type
    bits 20 and 21; the full build and --relight alike (relit scenes once had them on at every hour)"""
    lt = work / 'light_time.json'
    if not lt.exists():
        return
    lt = json.load(open(lt, encoding='utf-8'))
    # 'never': a timetable that is off at every hour (scene_lights leaves those lights out)
    night, day, never = set(lt.get('night', ())), set(lt.get('day', ())), set(lt.get('never', ()))
    for L in lights:
        k = '%s|%s|%.1f|%.1f' % (L.get('sector'), L.get('name'), L['pos'][0], L['pos'][1])
        L['time'] = 'never' if k in never else 'night' if k in night else 'day' if k in day else None
        if L['time']:
            stats['lights_' + L['time'] + '_only'] += 1


def scene_relight(work, centre, radius, src, dst, a):
    """An existing scene pk3 with only its light table rebuilt from placements.json (minutes, not the material pass)."""
    import copy
    import struct
    import zipfile
    from collections import Counter
    name = a.map or 'cp_' + a.area
    origin = np.array([centre[0], centre[1], a.floor])
    lights = [L for L in json.load(open(work / 'placements.json', encoding='utf-8')).get('lights', [])
              if in_area(L['pos'][0], L['pos'][1], centre, radius, L['pos'][2])]
    # the signals as the full build has them (their lamps' cycle bits; the pack's .signals file rides along)
    sig = scene_signals(work, a, centre, radius)
    lights += entity_lights(work, centre, radius, signals=bool(sig))
    volumes = json.load(open(work / 'placements.json', encoding='utf-8')).get('channel_volumes', [])
    stats = Counter()
    light_times(work, lights, stats)
    # the ad lights' colours from the build's own ads (ad_lights.json beside the scene pk3)
    side = Path(src).parent / 'ad_lights.json'
    ad_colours = json.load(open(side, encoding='utf-8')) if side.exists() else {}
    for L in lights:
        c = ad_colours.get(ad_light_key(L))
        if c:
            L['color'] = c
            stats['ad_light_coloured'] += 1
    if side.exists() and Path(dst).parent != side.parent:
        Path(dst).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(side, Path(dst).parent / 'ad_lights.json')
    lrecs = scene_lights(lights, origin, a, stats, volumes, sig)
    zin = zipfile.ZipFile(src)
    blob = zin.read('maps/%s.xscn' % name)
    version = struct.unpack_from('<I', blob, 4)[0]
    n = 8 if version == 1 else 9 if version < 6 else 10
    count, ofs = list(struct.unpack_from('<%dI' % n, blob, 8)), list(struct.unpack_from('<%dI' % n, blob, 8 + 4 * n))
    assert ofs[7] == max(ofs[:8]) and all(ofs[k] > ofs[k - 1] for k in range(8, n)), 'the light sections are not last'
    lights_b, boxes_b, shapes_b = scene_light_section(lrecs), scene_box_section(lrecs), scene_shape_section(lrecs)
    # the sections before the lights as they are, the light sections new, laid out again as xscn_blob lays them
    # (16-byte aligned after the version's header; the old way shifted a version-5 file's sections by 8 B)
    ends = ofs[1:8] + [ofs[7]]
    body = [(count[k], blob[ofs[k]:ends[k]]) for k in range(7)]
    blob = xscn_rebuild(SCENE_VERSION, body + [(len(lrecs), lights_b), (len(lrecs), boxes_b), (len(lrecs), shapes_b)])
    fog = scene_fogs(work, centre, radius, origin, stats)     # the haze's local volumes ride along
    tmp = Path(str(dst) + '.tmp')       # (renamed when whole)
    with zipfile.ZipFile(tmp, 'w', allowZip64=False) as z:
        for info in zin.infolist():
            if info.filename == 'maps/%s.xfog' % name:
                continue
            data = blob if info.filename == 'maps/%s.xscn' % name else zin.read(info.filename)
            z.writestr(copy.copy(info), data)
        z.writestr(zipfile.ZipInfo('maps/%s.xfog' % name), fog)
    zin.close()
    os.replace(tmp, dst)
    print(f'relight -> {dst}: {len(lrecs)} lights (' + ', '.join(f'{k} {v}' for k, v in sorted(stats.items())) + ')')


_baker = None


def _prebake_init(args):
    global _baker
    import cp77mat
    _baker = cp77mat.Materials(*args[:1], **args[1])


def _prebake_one(job):
    setup, mask, n = job
    return _baker.multilayer(setup, mask, n) is not None


def prebake(work, plan, a, workers):
    """Bake the plan's multilayered materials into work/bake in parallel (one at a time inside the scene stage took
    about 8 s each: hours cold); the scene stage then reads the cache."""
    from concurrent.futures import ProcessPoolExecutor
    import cp77mat
    kw = dict(size=a.scene_texsize, bake_dir=work / 'bake', ml_size=a.texsize, detail_size=a.detail_size,
              detail_slots=getattr(a, 'detail_slots', 1))
    probe = cp77mat.Materials(work / 'meshes', **kw)
    jobs = set()
    for p, ml, m, size, lv in plan:
        for nm in ml.material_names(p['app']):
            rec = ml.materials.get(nm)
            if rec and rec['MaterialTemplate'].split(SL)[-1].lower().startswith('multilayered'):
                d = rec.get('Data') or {}
                jobs.add((d.get('MultilayerSetup', ''), d.get('MultilayerMask', ''), a.texsize))
    todo = sorted(j for j in jobs if not probe.bake_path(*j).exists())
    print(f'prebake: {len(jobs)} multilayer bakes, {len(todo)} to make on {workers} processes', flush=True)
    done = 0
    with ProcessPoolExecutor(workers, initializer=_prebake_init, initargs=((work / 'meshes', kw),)) as ex:
        for ok in ex.map(_prebake_one, todo, chunksize=4):
            done += 1
            if done % 200 == 0:
                print(f'  prebake {done}/{len(todo)}', flush=True)


def material_key(ml, mname, rec):
    """One converted material per distinct material record: its instance (.mi, else the mesh's own) and name, and a
    hash of the whole record. Meshes override a shared .mi's values (a window's room size, an ad screen's glow, a
    decal's image): keyed by the .mi alone, whichever mesh came first set them for all (Kabuki: 13 materials, 1,023
    instances changed with the order the placements came in)."""
    base = rec['BaseMaterial'] if rec['BaseMaterial'].endswith('.mi') else ml.rel
    return base, rec['Name'], hashlib.sha1(json.dumps(rec, sort_keys=True).encode()).hexdigest()[:12]


def placement_uid(p, m):
    """A placement's identity for keys that must not depend on where in a list it comes (a bent placement's own
    geometry, across tiles): its name and frame."""
    return '%s|%s' % (p.get('name'), ','.join('%.3f' % v for v in np.asarray(m, float).ravel()))


def materials_json(sc):
    """materials.json's records: each material's name record with its key (repr of scene_part's material key: mesh,
    name, appearance variant...) and its record's kind and flags, so two builds' material ids map exactly, old to new"""
    out = []
    for i, m in enumerate(sc['mnames']):
        r = dict(m)
        if i < len(sc['mkeys']):
            r['mkey'] = sc['mkeys'][i]
        if i < len(sc['mrecs']):
            r['flags'] = int(sc['mrecs'][i][2])
        out.append(r)
    return out


def matcache_dir(work):
    """The converted materials a work folder's scene parts and collision builds keep (cp77mat.ResultCache)."""
    return work / 'matcache'


def empty_drec(mats):
    """A material record's tiled detail when it has none: .xdet v1's (image, tile, normal, roughness, colour), or v2's
    two share maps and eight empty slots."""
    return ('', 0.0, 0.0, 0.0, 0.0) if mats.detail_slots <= 1 else ('', '') + ('', 0.0, 0.0, 0.0, 0.0) * 8


def scene_material_salt():
    """What a scene part's kept materials are a function of in this file: scene_material's code and SceneImages'
    (its image names), so an edit to either misses the kept ones."""
    import stagekey
    return 'scene ' + stagekey.code_key(Path(__file__).resolve(), ['scene_material', 'SceneImages'], entry=None)


def scene_material(rec, mats, images, cache=None):
    """What a scene part takes from a converted material (cp77mat convert): its kind, its images as SceneImages paths
    (added to images), and the values its records hold, as JSON, so that it can be kept (convert
    was 65% of a scene part). A kept one is used while the image cache holds every image it names; a new one
    is the same values through JSON, as a kept one gives them."""
    import cp77mat
    got = cache.get(rec) if cache is not None else None
    if got is not None:
        names = [q for q in got.get('paths', []) + [(got.get('decal') or {}).get(k, '') for k in ('normal', 'height')]
                 + [q for q in (got.get('detail') or []) if isinstance(q, str)] if q]
        if images.cache is not None and all(images.cached(q).exists() for q in names):
            for q in names:
                images.keep(q)
            return got
    with mats.track() as deps:
        c = mats.convert(rec)
        kind, ex = c['kind'], c['extras']
        sm = {'kind': kind}
        if kind != 'skip':
            glow_mask = bool(ex.get('glow_mask'))         # a masked glow in the colour image's alpha
            ga = ex.get('glow_albedo') if kind in ('opaque', 'cutout') else None
            nrm = c.get('normal')
            if ga is not None:
                # a glow in the colour image's own colours, masked by the normal image's alpha (flags 0x400; flat where
                # the material has no normal map)
                if nrm is None:
                    nrm = np.full(ga.shape + (3,), (128, 128, 255), np.uint8)
                if ga.shape != nrm.shape[:2]:
                    from PIL import Image
                    ga = np.asarray(Image.fromarray(ga).resize(nrm.shape[1::-1], Image.BILINEAR))
                nrm = np.concatenate([nrm[..., :3], ga[..., None]], -1)
            paths = []
            for img, suffix in ((c['image'], 'a'), (nrm, 'n'), (c.get('orm'), 'o')):
                if img is None or (suffix != 'a' and kind not in ('opaque', 'cutout')):
                    paths.append('')
                    continue
                paths.append(images.add(img, suffix, alpha=suffix == 'a' and (kind in ('cutout', 'blend') or glow_mask),
                                        coverage=suffix == 'a' and kind == 'cutout'))
            dec = ex.get('decal')
            if dec:
                dec = {'normal': images.add(dec['normal'], 'n') if dec.get('normal') is not None else '',
                       'height': images.add(dec['height'], 'h') if dec.get('height') is not None else '',
                       'alpha': dec['alpha'], 'depth': dec['depth'], 'normal_alpha': dec['normal_alpha']}
            det = c.get('detail')
            detail = None
            if det and kind in ('opaque', 'cutout') and 'slots' in det:
                # .xdet v2: [share map 0, share map 1, then per slot image, tile, normal, roughness range, colour
                # strength], 8 slots (empty: '', 0, 0, 0, 0); the share maps raw (linear) RGBA, the second '' for 4
                # slots or fewer
                sh = det['share']
                detail = [images.add(sh[..., :4], 's', alpha=True),
                          images.add(sh[..., 4:], 's', alpha=True) if len(det['slots']) > 4 else '']
                for s in range(8):
                    e = det['slots'][s] if s < len(det['slots']) else None
                    detail += ([images.add(mats.detail_image(e['template']), 'd', alpha=True), e['tile'], e['normal'],
                                e['rough'], 1.0] if e else ['', 0.0, 0.0, 0.0, 0.0])
            elif det and kind in ('opaque', 'cutout'):
                detail = [images.add(mats.detail_image(det['template']), 'd', alpha=True), det['tile'], det['normal'],
                          det['rough']]
            ad = None
            if kind in ('emissive', 'blend') and (ex.get('standin_ad') or ex.get('cp_template') == 'television_ad.mt'):
                ad = [float(x) for x in cp77mat.lin(c['image'][..., :3].reshape(-1, 3) / 255.0).mean(0)]
            sm.update(paths=paths, emissive=ex.get('lp_emissive'), window=ex.get('window'), decal=dec,
                      glow_mask=glow_mask, glow_albedo=ga is not None, leaves=bool(ex.get('foliage_leaves')),
                      vboard=bool(ex.get('vector_board')), lamp=ex.get('signal_lamp'),
                      terrain=ex.get('cp_template') == 'multilayered_terrain.mt',
                      standin_ad=bool(ex.get('standin_ad')),
                      detail=detail, ad_colour=ad, screen=ex.get('screen'))
    try:
        sm = json.loads(json.dumps(sm))
    except (TypeError, ValueError):     # (a value JSON does not hold: used as it is, never kept)
        return sm
    if cache is not None:
        cache.put(rec, sm, deps)
    return sm


def scene_part(work, a, plan, origin, sig, mats, images, stats, water_from=None):
    """The render scene's geometry, materials and instances for a plan's placements (one tile, or a whole area), each
    unique mesh once with all of its LODs; with water_from (centre, radius), the water patches instead (water_records).
    Returns the part merge_scene_parts joins: arrays, plus keys that say when two parts' meshes or materials are the
    same one."""
    import cp77mat
    upm = gltf_upm()
    with_proxies = any(q[0].get('proxy') for q in plan)  # (an entity hides at its proxy only where proxies draw)
    verts, idx, geo, chunks = [], [], {}, []
    count = {'v': 0, 'i': 0}
    meshes, lods, parts, mrecs, mat_index, mesh_of = [], [], [], [], {}, {}
    mkeys, meshkeys = [], []
    mnames = []     # per material record: where it came from (materials.json beside the pk3; r_sceneDebug 8 names the index)
    drecs = []      # per material record: its tiled detail (maps/<map>.xdet): image path, tiles, normal, roughness, colour
    standin = set()     # material keys of stand-in ads (each placement shows one of AD_VARIANTS of them)
    ad_colour = {}      # material index -> the mean linear colour an ad shows (its lights take it)
    mcache = cp77mat.ResultCache(matcache_dir(work), mats, scene_material_salt())
    dcut = set()        # material keys whose metal_base_det detail became a cutout (the merge's count)

    def material(ml, mname, variant=None, zone=None):
        rec = ml.materials.get(mname)
        if rec is None:
            return None
        key = material_key(ml, mname, rec)
        if variant is not None and key not in mat_index:
            # whether a material is a stand-in ad is learnt by converting its own record: first, so it never depends
            # on which mesh came before (a screen's second stand-in face kept its variant in one tile and
            # lost it in another, scene_mesh's any() having stopped at the first)
            material(ml, mname)
        if variant is not None and key in standin:
            rec, key = dict(rec, _variant='|%s' % variant), key + (variant,)
        if zone is not None:	# (zone, lamp, crossing): a signal head's lamp zone (scene_mesh), cp77mat's branch
            rec, key = dict(rec, _zone=zone[0], _lamp=zone[1], _crossing=zone[2]), key + ('zone',) + tuple(zone)
        if key in mat_index:
            return mat_index[key]
        sm = scene_material(rec, mats, images, mcache)
        if sm['kind'] == 'skip':
            stats['skip_materials'] += 1
            mat_index[key] = None
            return None
        i, kind = len(mrecs), sm['kind']
        paths = list(sm['paths'])
        em = sm['emissive'] or {}
        ecol = list(em.get('color', (1, 1, 1))) if em.get('tint', False) else [1.0, 1.0, 1.0]
        win = sm['window']
        dec = sm['decal']
        if dec:
            # a projected mesh decal (no version bump): kind blend as
            # before, flags bit 6 (0x40) | two-sided; tint.a its DiffuseAlpha (the lit pass multiplies albedo alpha
            # by it, so an older renderer draws the same 0.7 blend); window = (DepthThreshold in CP2077 metres,
            # NormalAlpha); images: the colour with its alpha, its normal, its height map
            stats['mesh_decals'] = stats.get('mesh_decals', 0) + 1
            paths[1], paths[2] = dec['normal'], dec['height']
            mrecs.append((paths, SCENE_KIND[kind], 1 | 0x40, ecol + [0.0], [1.0, 1.0, 1.0, float(dec['alpha'])],
                          [float(dec['depth']), float(dec['normal_alpha'])]))
        elif win:
            # a window: flags bit 1; tint holds its room (width, height in metres, the dark share, the colour
            # temperature spread), the last two floats its depth and the atlas's rooms
            stats['windows'] += 1
            mrecs.append((paths, SCENE_KIND[kind], 2, ecol + [float(em.get('intensity', 0.0))],
                          [win['room'][0], win['room'][1], win['off'], win['temp']], [win['room'][2], float(win['tiles'])]))
        else:
            lamp = sm['lamp']   # a signal lamp: flags 8 | lamp << 4 (the renderer switches it)
            mrecs.append((paths, SCENE_KIND[kind], (1 if kind in ('cutout', 'blend') else 0) |
                          (4 if sm['glow_mask'] else 0) | (0x400 if sm.get('glow_albedo') else 0) |
                          (0x800 if sm.get('leaves') else 0) |
                          # terrain: the renderer draws it a little behind its depth (r_sceneTerrainPush), so what is
                          # laid in its plane wins
                          (0x1000 if sm.get('terrain') and kind == 'opaque' else 0) |
                          # a vector billboard is a screen to the renderer (its screen gain: patch_vboard22)
                          (0x200 if sm.get('vboard') else 0) |
                          (8 | LAMP_INDEX[lamp] << 4 if lamp else 0),
                          ecol + [float(em.get('intensity', 0.0))], [1.0, 1.0, 1.0, 1.0], [0.0, 0.0]))
        mkeys.append(repr(key))
        stats['materials'] += 1
        if (kind == 'cutout' and getattr(mats, 'detail_cutout', False)
                and (rec.get('MaterialTemplate') or '').split('\\')[-1].lower() == 'metal_base_det.mt'
                and float((rec.get('Data') or {}).get('AlphaThreshold') or 0) > 0):
            stats['part_det_cutouts'] += 1  # (metal_base_det's tiled detail baked in as a cutout; the merge counts
            dcut.add(repr(key))             # the distinct ones: Watson 432)
        if sm['detail']:
            drecs.append(tuple(sm['detail']) + (() if len(sm['detail']) > 4 else (1.0,)))
            stats['detail_materials'] += 1
        else:
            drecs.append(empty_drec(mats))
        if sm['standin_ad']:
            standin.add(key[:3])
        if sm['ad_colour'] is not None:
            ad_colour[i] = sm['ad_colour']
        mnames.append({'index': i, 'mesh': ml.rel, 'name': mname, 'base': rec['BaseMaterial'], 'kind': kind,
                       'images': paths})
        if sm.get('screen'):            # (a screen's CP2077 shader values: maps/<map>.xscr, scene_map_files)
            mnames[-1]['screen'] = sm['screen']
        mat_index[key] = i
        return i

    def geometry(pos, nrm, uv, tris, deform=None, ylim=None, box=None, rec=None):
        pos = pos @ G2C.T
        nrm = None if nrm is None else nrm @ G2C.T
        if uv is None:
            uv = generated_uv(rec, pos)
        if deform:
            pos, nrm = bend(pos, nrm, deform, *ylim)
        if box is not None and len(pos):
            box[0] = np.minimum(box[0], pos.min(0))
            box[1] = np.maximum(box[1], pos.max(0))
        n, t = vertex_frame(pos, nrm, uv, tris)
        v = np.zeros(len(pos), SCENE_VERT)
        v['pos'] = pos
        v['nrm'][:, :3] = snorm8(n)
        v['tan'] = snorm8(t)
        if uv is not None:
            v['uv'] = uv
        verts.append(v)
        idx.append(tris.astype('<u4').ravel())
        g = (count['i'], tris.size, count['v'])
        chunks.append(g + (len(pos),))
        count['v'] += len(pos)
        count['i'] += tris.size
        return g

    water_mat, water_nrm = {}, []

    def water_material(app):
        """The scene's water material for an appearance (WATER_LOOK), its index in this part: the patches' quads and an
        open water mesh's chunks (open_water) take the same record and key, so the merge keeps one"""
        if app in water_mat:
            return water_mat[app]
        col, rough = WATER_LOOK[app]
        mi = len(mrecs)
        if not water_nrm:
            water_nrm.append(images.add(water_ripple_normal(), 'n'))
            # the foam (its red channel, tiled with the ripples): 1024 square, as the renderer asked
            from PIL import Image
            f = work / 'meshes' / Path(*WATER_FOAM)
            water_nrm.append(images.add(np.asarray(Image.open(f).convert('RGB').resize((1024, 1024),
                                                                                       Image.LANCZOS)), 'a')
                             if f.exists() else '')
        paths = ['', water_nrm[0],
                 images.add(np.full((4, 4, 3), (255, round(255 * rough), 0), np.uint8), 'o')]
        tint = [c / 255.0 for c in WATER_SCATTER[app]] + [1.0]
        mrecs.append((paths, SCENE_KIND['blend'], WATER_FLAG, [1.0, 1.0, 1.0, 0.0], tint, [0.0, 0.0]))
        mkeys.append(repr(('water', app)))
        drecs.append(empty_drec(mats))
        mnames.append({'index': mi, 'mesh': 'water', 'name': app, 'base': 'global_ocean_patch', 'kind': 'water',
                       'images': paths, 'foam': water_nrm[1] if WATER_XWAT[app][7] > 0 else ''})
        stats['materials'] += 1
        water_mat[app] = mi
        return mi

    def scene_mesh(ml, app, depot, deform=None, uid=None, variant=None, chunk_mask=None, zones=None, crossing=False,
                   water=False):
        # a bent placement is geometry of its own; a mesh showing stand-in ads is one per ad variant; an entity's mesh
        # component may hide some of its chunks; a signal head's face splits into its lamps' zones (zones: zone -> lamp);
        # open water (open_water) draws its global_water_patch chunks in the scene's water material
        app = ml.app_name(app)
        names = ml.material_names(app)
        if not any(material(ml, nm) is not None and (mats_key(ml, nm) in standin) for nm in names):
            variant = None
        zones = {int(z): lamp for z, lamp in (zones or {}).items()}
        key = (depot, app, uid if deform else None, variant, chunk_mask, tuple(sorted(zones.items())), crossing)
        if water:
            key = key + ('open_water',)
        if key in mesh_of:
            return mesh_of[key]
        first_lod = len(lods)
        box = [np.full(3, np.inf), np.full(3, -np.inf)] if deform else None
        for k, level in enumerate(ml.levels):
            first_part = len(parts)
            for ci, (mask, ps) in sorted(ml.chunks.items()):
                if not mask >> level & 1 or (chunk_mask is not None and not chunk_mask >> ci & 1):
                    continue
                rec = (ml.materials.get(names[ci]) or {}) if ci < len(names) else {}
                if water and (rec.get('MaterialTemplate') or '').replace('\\', '/').lower().endswith('global_water_patch.mt'):
                    mi = water_material('default')     # (its own record not converted: no unused opaque sheet)
                else:
                    mi = material(ml, names[ci], variant) if ci < len(names) else None
                if mi is None:
                    continue
                signal = bool(zones) and rec.get('MaterialTemplate', '').lower().endswith('lights_interactive.mt')
                for j, (pos, nrm, uv, tris) in enumerate(ps):
                    zv = ml.zones(ci)[j] if signal and j < len(ml.zones(ci)) else None
                    if zv is not None:
                        # a signal head's face: each lamp's zone its own glowing material, the rest the dark face
                        tz = zv[tris[:, 0]]
                        lit = [int(z) for z in np.unique(tz) if int(z) in zones]
                        groups = [(~np.isin(tz, lit), mi, -1)]
                        groups += [(tz == z, material(ml, names[ci], zone=(z, zones[z], crossing)), z) for z in lit]
                        for sel, m_, z in groups:
                            if m_ is None or not sel.any():
                                continue
                            # (the dark face depends on which zones are lit: two placements' lamp sets may differ)
                            gk = (depot, ci, j, uid if deform else None, z, tuple(lit) if z == -1 else None)
                            if gk not in geo:
                                geo[gk] = geometry(pos, nrm, uv, tris[sel], deform, ml.ylim, box if k == 0 else None)
                            parts.append(geo[gk] + (m_,))
                        continue
                    gk = (depot, ci, j, uid if deform else None)
                    if gk not in geo:
                        geo[gk] = geometry(pos, nrm, uv, tris, deform, ml.ylim, box if k == 0 else None, rec)
                    parts.append(geo[gk] + (mi,))
            if len(parts) > first_part:
                lods.append([first_part, len(parts) - first_part, SCENE_LOD_SCREEN[min(k, len(SCENE_LOD_SCREEN) - 1)]])
        if len(lods) == first_lod:
            mesh_of[key] = None
            return None
        lods[-1][2] = 0.0
        lo, hi = (box[0], box[1]) if box is not None and np.all(np.isfinite(box[0])) else (G2C @ ml.lo, G2C @ ml.hi)
        meshes.append(((lo + hi) / 2, float(np.linalg.norm(hi - lo)) / 2, first_lod, len(lods) - first_lod))
        meshkeys.append(repr(key))
        mesh_of[key] = len(meshes) - 1
        return mesh_of[key]

    def mats_key(ml, mname):
        rec = ml.materials.get(mname)
        return rec and material_key(ml, mname, rec)

    inst, placed = [], []
    rot = []        # per rotating instance: its index here, axis xyz, rad/s, pivot xyz, phase (maps/<map>.xrot)
    admap, rotmap = ad_map(work), rot_map(work)
    for n, (p, ml, m, size, lv) in enumerate(plan):
        # which ad a screen shows: CP2077's own (its pinned ad or one of its district group's, in its format:
        # cp77_ads.AdMap); else a stand-in by its name and place, the same every build
        art = admap.image(p)
        variant = ('ad:' + art) if art else int(hashlib.sha1(('%s|%.0f|%.0f' % (p.get('name'), m[0][3], m[1][3])).encode()
                                                             ).hexdigest()[:8], 16) % AD_VARIANTS
        if art:
            stats['ad_screens'] += 1
        water = open_water(work, p, ml, m)
        stats['open_water'] += 1 if water else 0
        mi = scene_mesh(ml, p['app'], p['mesh'], p.get('deform'), placement_uid(p, m), variant, p.get('chunk_mask'),
                        p.get('zones'), 'crossing' in p.get('name', '').lower(), water)
        if mi is None:
            stats['no_material'] += 1
            continue
        placed.append((p.get('name'), np.asarray(m, float)[:, 3], mi))
        W = np.zeros((3, 4))
        W[:, :3] = upm * np.asarray(m)[:, :3]
        W[:, 3] = upm * (np.asarray(m)[:, 3] - origin)
        slot = sig.slot(p.get('ent')) if sig and p.get('zones') else 0
        stats['signal_instances'] += 1 if slot else 0
        inst.append((W, mi, slot, upm * draw_distance(p, with_proxies), upm * float(p.get('near') or 0)))
        r = rotmap.rec(p, m)
        if r:
            rot.append([len(inst) - 1] + r)
            stats['rotating'] += 1
        if n % 5000 == 0:
            print(f'  scene: {n}/{len(plan)} placements, {len(meshes)} meshes, {len(mrecs)} materials', flush=True)

    # water (water_records): one 10 m quad per appearance (WATER_LOOK), an instance per patch
    water_mesh = {}
    for wr in water_records(work, *water_from, half=AREAS.get(a.area, {}).get('half')) if water_from else []:
        app = wr['app'] if wr['app'] in WATER_LOOK else 'default'
        if app not in water_mesh:
            mi = water_material(app)
            h = WATER_HALF
            quad = np.array([[-h, -h, 0.0], [h, -h, 0.0], [h, h, 0.0], [-h, h, 0.0]])
            parts.append(geometry(quad @ G2C, np.tile([0.0, 0.0, 1.0], (4, 1)) @ G2C,
                                  np.array([[0.0, 1.0], [1.0, 1.0], [1.0, 0.0], [0.0, 0.0]]),
                                  np.array([[0, 1, 2], [0, 2, 3]])) + (mi,))
            lods.append([len(parts) - 1, 1, 0.0])
            meshes.append((np.zeros(3), h * math.sqrt(2.0), len(lods) - 1, 1))
            meshkeys.append(repr(('water', app)))
            water_mesh[app] = len(meshes) - 1
        m = np.asarray(wr['m'], float).reshape(3, 4)
        W = np.zeros((3, 4))
        W[:, :3] = upm * m[:, :3]
        W[:, 3] = upm * (m[:, 3] - origin)
        inst.append((W, water_mesh[app], 0, 0.0, 0.0))
        stats['water_patches'] += 1

    return {
        'V': np.concatenate(verts) if verts else np.zeros(0, SCENE_VERT),
        'I': np.concatenate(idx) if idx else np.zeros(0, '<u4'),
        'chunks': np.array(chunks, np.int64).reshape(-1, 4),       # fi, ni, vo, vertex count
        'meshes': np.array([list(c) + [r, fl, nl] for c, r, fl, nl in meshes], np.float64).reshape(-1, 6),
        'lods': np.array(lods, np.float64).reshape(-1, 3),
        'parts': np.array(parts, np.int64).reshape(-1, 4),
        'inst_W': np.array([W for W, *_ in inst], np.float64).reshape(-1, 3, 4),
        'inst_mesh': np.array([mi for _, mi, *_ in inst], np.int64),
        'inst_slot': np.array([sl for _, _, sl, _, _ in inst], np.int64),
        'inst_hide': np.array([h for _, _, _, h, _ in inst], np.float32),
        'inst_near': np.array([n for *_, n in inst], np.float32),
        'inst_rot': np.array(rot, np.float64).reshape(-1, 9),
        'placed_pos': np.array([pos for _, pos, _ in placed], np.float64).reshape(-1, 3),
        'placed_mesh': np.array([mi for _, _, mi in placed], np.int64),
        'placed_name': [nm for nm, _, _ in placed],
        'mrecs': [list(r) for r in mrecs], 'mkeys': mkeys, 'mnames': mnames, 'drecs': [list(d) for d in drecs],
        'meshkeys': meshkeys, 'ad_colour': {int(k): [float(x) for x in v] for k, v in ad_colour.items()},
        'images': sorted(images.todo), 'matcache': [mcache.hits, mcache.misses], 'dcut': sorted(dcut),
    }


PART_ARRAYS = ('V', 'I', 'chunks', 'meshes', 'lods', 'parts', 'inst_W', 'inst_mesh', 'inst_slot', 'placed_pos',
               'placed_mesh')
PART_OPTIONAL = ('inst_hide', 'inst_near',      # older parts have none (scene-hide writes inst_hide.npy beside)
                 'inst_rot')                    # (older parts: no rotating instances)


def savez_deflated(path, **arrays):
    """np.savez's file (members <name>.npy, read by the same np.load) deflated at level 1: the tile parts were stored,
    35 GB of a tag's 45 (scene parts to 0.39 of that, collision parts 0.15, about 4 s more on the
    largest scene part)"""
    import zipfile
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as z:
        for k, v in arrays.items():
            with z.open(k + '.npy', 'w', force_zip64=True) as f:
                np.lib.format.write_array(f, np.asanyarray(v), allow_pickle=True)


def save_part(part, stats, folder):
    """A part on disk (arrays.npz + part.json), written into a temporary folder and renamed: an interrupted tile leaves
    no part, and a finished one is never half written."""
    folder = Path(folder)
    tmp = folder.with_name(folder.name + '.tmp')
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    savez_deflated(tmp / 'arrays.npz', **{k: part[k] for k in PART_ARRAYS + PART_OPTIONAL if k in part})
    rest = {k: v for k, v in part.items() if k not in PART_ARRAYS + PART_OPTIONAL}
    rest['stats'] = dict(stats)
    (tmp / 'part.json').write_text(json.dumps(rest), encoding='utf-8')
    if folder.exists():
        shutil.rmtree(folder)
    os.replace(tmp, folder)


def load_part(folder):
    folder = Path(folder)
    part = json.loads((folder / 'part.json').read_text(encoding='utf-8'))
    with np.load(folder / 'arrays.npz') as z:
        for k in PART_ARRAYS:
            part[k] = z[k]
        if 'inst_hide' in z.files:
            part['inst_hide'] = z['inst_hide']
        part['inst_near'] = z['inst_near'] if 'inst_near' in z.files else np.zeros(len(part['inst_mesh']), np.float32)
        part['inst_rot'] = z['inst_rot'] if 'inst_rot' in z.files else np.zeros((0, 9))
    if 'inst_hide' not in part:
        side = folder / 'inst_hide.npy'
        part['inst_hide'] = (np.load(side) if side.exists()
                             else np.zeros(len(part['inst_mesh']), np.float32))
    if len(part['inst_hide']) != len(part['inst_mesh']):
        sys.exit('part %s: %d hide distances for %d instances' % (folder, len(part['inst_hide']), len(part['inst_mesh'])))
    part['ad_colour'] = {int(k): v for k, v in part['ad_colour'].items()}
    return part


class SceneMerger:
    """Scene parts joined one at a time: a mesh or material several parts hold (the same key) is kept once, the first
    time it comes, with the geometry its LODs use (identical chunks once, whichever part brought them); instances
    and placements point at the joined meshes. add() returns a part's new vertices and indices and its instances, so
    a tiled scene can write each tile's share and let it go (stage_scene_tiled); merge_scene_parts keeps them all.
    Parts that built one mesh key differently (LOD screens, index and vertex counts, material keys) stop the merge:
    keeping the first would make the scene depend on tile order (a stand-in ad face)."""

    def __init__(self):
        self.meshes, self.lods, self.parts = [], [], []
        self.sig_g, self.conflicts = {}, []
        self.mrecs, self.mnames, self.drecs, self.mkeys, self.meshkeys = [], [], [], [], []
        self.dcut = set()   # the parts' detail-cutout material keys (scene_part 'dcut')
        self.placed_pos, self.placed_mesh, self.placed_name = [], [], []
        self.rots, self.n_inst = [], 0     # the rotating instances, their indices over the parts' instances end to end
        self.mat_g, self.mesh_g, self.geo_g, self.ad_colour, self.images = {}, {}, {}, {}, set()
        self.nv = self.ni = 0

    def add(self, P):
        self.dcut.update(P.get('dcut') or [])
        V, I = [], []
        lods, sparts = self.lods, self.parts
        mmap = np.zeros(len(P['mkeys']), np.int64)
        for i, key in enumerate(P['mkeys']):
            if key not in self.mat_g:
                self.mat_g[key] = len(self.mrecs)
                self.mrecs.append(P['mrecs'][i])
                self.mnames.append(dict(P['mnames'][i], index=len(self.mnames)))
                self.drecs.append(P['drecs'][i])
                self.mkeys.append(key)
                if i in P['ad_colour']:
                    self.ad_colour[self.mat_g[key]] = P['ad_colour'][i]
            mmap[i] = self.mat_g[key]
        chunk_of = {(int(c[0]), int(c[2])): c for c in P['chunks']}
        copied = {}     # a chunk of this part -> where it went; geo_g: a chunk's content -> where it went, any part
        meshmap = np.zeros(len(P['meshkeys']), np.int64)
        for mi, key in enumerate(P['meshkeys']):
            fl, nl = P['meshes'][mi][4:6]
            sig = tuple((round(float(sc), 6), tuple((int(nidx), P['mkeys'][int(m)], int(chunk_of[(int(fi), int(vo))][3]))
                                                    for fi, nidx, vo, m in P['parts'][int(fp):int(fp) + int(npart)]))
                        for fp, npart, sc in P['lods'][int(fl):int(fl) + int(nl)])
            if key in self.mesh_g:
                if self.sig_g[key] != sig:
                    self.conflicts.append(key)
                meshmap[mi] = self.mesh_g[key]
                continue
            self.sig_g[key] = sig
            cx, cy, cz, r, fl, nl = P['meshes'][mi]
            first_lod = len(lods)
            for fp, npart, sc in P['lods'][int(fl):int(fl) + int(nl)]:
                first_part = len(sparts)
                for fi, nidx, vo, m in P['parts'][int(fp):int(fp) + int(npart)]:
                    g = copied.get((int(fi), int(vo)))
                    if g is None:
                        c = chunk_of[(int(fi), int(vo))]
                        cv, ci = P['V'][int(vo):int(vo) + int(c[3])], P['I'][int(fi):int(fi) + int(c[1])]
                        h = hashlib.sha1(cv.tobytes() + ci.tobytes()).digest()
                        g = self.geo_g.get(h)
                        if g is None:
                            V.append(cv)
                            I.append(ci)
                            g = self.geo_g[h] = (self.ni, self.nv)
                            self.ni += int(c[1])
                            self.nv += int(c[3])
                        copied[(int(fi), int(vo))] = g
                    sparts.append((g[0], int(nidx), g[1], int(mmap[int(m)])))
                lods.append([first_part, len(sparts) - first_part, float(sc)])
            self.meshes.append(((cx, cy, cz), float(r), first_lod, len(lods) - first_lod))
            self.meshkeys.append(key)
            self.mesh_g[key] = meshmap[mi] = len(self.meshes) - 1
        self.placed_pos.append(P['placed_pos'])
        self.placed_mesh.append(meshmap[P['placed_mesh']])
        self.placed_name += P['placed_name']
        self.images |= set(P['images'])
        R = np.array(P.get('inst_rot', np.zeros((0, 9))), np.float64).reshape(-1, 9)
        if len(R):
            R[:, 0] += self.n_inst
            self.rots.append(R)
        self.n_inst += len(P['inst_mesh'])
        return V, I, (P['inst_W'], meshmap[P['inst_mesh']], P['inst_slot'],
                      P.get('inst_hide', np.zeros(len(P['inst_mesh']), np.float32)),
                      P.get('inst_near', np.zeros(len(P['inst_mesh']), np.float32)))

    def result(self, V, I, inst):
        """The joined scene (scene_finish's input); V, I: the parts' geometry blocks; inst: their instances"""
        if self.conflicts:
            sys.exit('scene merge: %d mesh keys built differently by different parts, e.g. %s' % (
                len(self.conflicts), '; '.join(self.conflicts[:3])))
        cat = lambda xs, shape, dt: np.concatenate(xs) if xs else np.zeros(shape, dt)
        return {'V': cat(V, 0, SCENE_VERT), 'I': cat(I, 0, '<u4'), 'meshes': self.meshes, 'lods': self.lods,
                'parts': self.parts, 'mrecs': self.mrecs, 'mnames': self.mnames, 'drecs': self.drecs,
                'mkeys': self.mkeys, 'meshkeys': self.meshkeys, 'dcut': sorted(self.dcut),
                'inst_W': cat([w for w, *_ in inst], (0, 3, 4), np.float64),
                'inst_mesh': cat([m for _, m, *_ in inst], 0, np.int64),
                'inst_slot': cat([sl for _, _, sl, *_ in inst], 0, np.int64),
                'inst_hide': cat([h for _, _, _, h, *_ in inst], 0, np.float32),
                'inst_near': cat([x[4] if len(x) > 4 else np.zeros(len(x[1]), np.float32) for x in inst], 0, np.float32),
                'placed_pos': cat(self.placed_pos, (0, 3), np.float64),
                'placed_mesh': cat(self.placed_mesh, 0, np.int64), 'placed_name': self.placed_name,
                'inst_rot': cat(self.rots, (0, 9), np.float64), 'ad_colour': self.ad_colour, 'images': sorted(self.images)}


def merge_scene_parts(parts):
    """Join scene parts into one (SceneMerger, every part's geometry kept)."""
    m = SceneMerger()
    V, I, inst = [], [], []
    for P in parts:
        v, i, ins = m.add(P)
        V += v
        I += i
        inst.append(ins)
    return m.result(V, I, inst)


def scene_light_records(work, centre, radius, a, sc, stats, sig, origin):
    """The sectors' lights of a joined scene (ad lights coloured by their ads) as light records, and the ad colours"""
    lights = [L for L in json.load(open(work / 'placements.json', encoding='utf-8')).get('lights', [])
              if in_area(L['pos'][0], L['pos'][1], centre, radius, L['pos'][2])]
    lights += entity_lights(work, centre, radius, signals=bool(sig))
    light_times(work, lights, stats)
    meshes, lods, parts = sc['meshes'], sc['lods'], sc['parts']
    mesh_mats = {mi: {parts[p][3] for f, c, _ in lods[fl:fl + nl] for p in range(f, f + c)}
                 for mi, (_, _, fl, nl) in enumerate(meshes)}
    placed = list(zip(sc['placed_name'], sc['placed_pos'], sc['placed_mesh']))
    ad_colours = ad_lights_colour(lights, placed, mesh_mats, {k: np.asarray(v) for k, v in sc['ad_colour'].items()}, stats)
    lrecs = scene_lights(lights, origin, a, stats,
                         json.load(open(work / 'placements.json', encoding='utf-8')).get('channel_volumes', []), sig)
    return lrecs, ad_colours


def ocean_mask_block(path, origin, upm):
    """.xwat v3's sea mask after the ocean record: u32 width, height; f32 cell, origin x, origin
    y (game units, the min corner); width x height u8, rows +y, columns +x, nonzero = sea. From an .npz in CP2077
    metres (mask[i, j] with i along x; x0, y0 its min corner; cell its size)"""
    import struct
    m = np.load(path)
    mask, x0, y0, cell = m['mask'], float(m['x0']), float(m['y0']), float(m['cell'])
    w, h = mask.shape
    rows = np.ascontiguousarray(mask.T.astype(np.uint8) * 255)          # [row = y][column = x]
    return struct.pack('<II3f', w, h, cell * upm, (x0 - origin[0]) * upm, (y0 - origin[1]) * upm) + rows.tobytes()


def scene_map_files(z, work, centre, radius, a, sig, origin, drecs, stats, mnames=(), rots=None, ocean_mesh=None):
    """What a scene pack holds besides the scene itself, once per map: the colour grade, the haze's local volumes,
    the signals, the tiled details (in the material table's order), the water materials' settings and the ocean plane,
    the screens' shader values, the rotating meshes"""
    import struct
    import zipfile
    name = a.map or 'cp_' + a.area
    if a.grade:
        z.writestr(zipfile.ZipInfo('maps/%s.xlut' % name), scene_grade(work, a.grade),
                   compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
    z.writestr(zipfile.ZipInfo('maps/%s.xfog' % name), scene_fogs(work, centre, radius, origin, stats))
    if sig:
        z.writestr(zipfile.ZipInfo('maps/%s.signals' % name), sig.text(origin, gltf_upm()))
    if drecs and len(drecs[0]) > 5:
        # v2: 'XDET', 2, materials, slot images; the slot images' 64 B
        # paths; per material 8 slots of (u32 image index or 0xFFFFFFFF, f32 tile, normal strength, roughness range,
        # colour strength) and its two share maps' 64 B paths (raw shares, slot s in channel s % 4 of map s // 4)
        table, recs = {}, []
        for d in drecs:
            r = b''
            for s in range(8):
                path, tile, nk, rr, cs = d[2 + 5 * s:7 + 5 * s]
                k = table.setdefault(path, len(table)) if path else 0xFFFFFFFF
                r += struct.pack('<I4f', k, tile, nk, rr, cs)
            recs.append(r + d[0].encode('ascii').ljust(64, b'\0') + d[1].encode('ascii').ljust(64, b'\0'))
        if table:
            xdet = [struct.pack('<4s3I', b'XDET', 2, len(drecs), len(table))]
            xdet += [p.encode('ascii').ljust(64, b'\0') for p in sorted(table, key=table.get)] + recs
            z.writestr(zipfile.ZipInfo('maps/%s.xdet' % name), b''.join(xdet), compress_type=zipfile.ZIP_DEFLATED,
                       compresslevel=1)
    elif any(d[0] for d in drecs):
        # the multilayered materials' main layers, tiled by the renderer (docs/levelport-scene.md, "Detail")
        xdet = [struct.pack('<4s3I', b'XDET', 1, len(drecs), 0)]
        xdet += [path.encode('ascii').ljust(64, b'\0') + struct.pack('<4f', tile, nk, rr, cs)
                 for path, tile, nk, rr, cs in drecs]
        z.writestr(zipfile.ZipInfo('maps/%s.xdet' % name), b''.join(xdet), compress_type=zipfile.ZIP_DEFLATED,
                   compresslevel=1)
    screens = [m for m in mnames if m.get('screen')]
    if screens:
        # the screens' CP2077 shader values: 'XSCR', 1,
        # records, 0; per record u32 material index, 16 f32 (template, flipbook tiles w h and frames per second, grid
        # rows, darkness, lines or dots, fade distance, layer separation, layers 2-4, blink, hue cycle, broken,
        # additive). Optional for the renderer (cvar defaults without it).
        xscr = [struct.pack('<4s3I', b'XSCR', 1, len(screens), 0)]
        xscr += [struct.pack('<I16f', int(m['index']), *[float(v) for v in m['screen']]) for m in screens]
        z.writestr(zipfile.ZipInfo('maps/%s.xscr' % name), b''.join(xscr), compress_type=zipfile.ZIP_DEFLATED,
                   compresslevel=1)
        stats['screen_materials'] = len(screens)
    if rots is not None and len(rots):
        # CP2077's rotating meshes (cp77_rotate.py): 'XROT', 1, records, 0;
        # per record u32 instance index (over the scene's instances end to end), f32 axis xyz in the mesh's frame,
        # signed rad/s, pivot xyz in the mesh's frame before its scale, start phase (rad)
        xrot = [struct.pack('<4s3I', b'XROT', 1, len(rots), 0)]
        xrot += [struct.pack('<I8f', int(r[0]), *[float(v) for v in r[1:9]]) for r in rots]
        z.writestr(zipfile.ZipInfo('maps/%s.xrot' % name), b''.join(xrot), compress_type=zipfile.ZIP_DEFLATED,
                   compresslevel=1)
    water = [m for m in mnames if m.get('kind') == 'water' and m.get('name') in WATER_XWAT]
    if water:
        # the water mode's settings, a record per water material (WATER_XWAT; the renderer's xwat.py checks one)
        # with the ocean (the renderer's ocean plane; until the baseline ocean_xwat.py on
        # the built scene): v2 adds one 32 B record after them, u32 the default water patch's mesh, f32 sea level
        # (CP2077 z 0 m), min x, min y, max x, max y (the area's box, game units), u32 pad[2]; v3 the sea mask after it
        mask = getattr(a, 'ocean_mask', None)
        ver = 1 if ocean_mesh is None else 3 if mask else 2
        xwat = [struct.pack('<4sIII', b'XWAT', ver, len(water), 0)]
        xwat += [struct.pack('<I9f', m['index'], *WATER_XWAT[m['name']]) + (m.get('foam') or '').encode('ascii').ljust(
            64, b'\0')[:64] for m in water]
        if ocean_mesh is not None:
            upm = gltf_upm()
            half = AREAS.get(a.area, {}).get('half', radius)
            hx, hy = half if isinstance(half, (tuple, list)) else (half, half)     # (kabuki's is one number)
            xwat.append(struct.pack('<I5f2I', ocean_mesh, (0.0 - origin[2]) * upm, -hx * upm, -hy * upm, hx * upm,
                                    hy * upm, 0, 0))
            if mask:
                xwat.append(ocean_mask_block(mask, origin, upm))
            stats['ocean_xwat_version'] = ver
        z.writestr(zipfile.ZipInfo('maps/%s.xwat' % name), b''.join(xwat))
        stats['water_materials'] = len(water)


def scene_finish(work, centre, radius, a, sc, images, stats, sig, missing=0):
    """What a whole scene adds to its joined parts: the sectors' lights (ad lights coloured by their ads), the haze's
    local volumes, the signals, the colour grade, the tiled details; written to maps/<map>.xscn and packed with the
    images into zz_<map>_scene.pk3."""
    import zipfile
    name = a.map or 'cp_' + a.area
    origin = np.array([centre[0], centre[1], a.floor])
    lrecs, ad_colours = scene_light_records(work, centre, radius, a, sc, stats, sig, origin)
    meshes, lods, parts, mrecs = sc['meshes'], sc['lods'], sc['parts'], sc['mrecs']
    V, I = sc['V'], sc['I']
    inst = list(zip(sc['inst_W'], sc['inst_mesh'], sc['inst_slot'],
                    sc.get('inst_hide', np.zeros(len(sc['inst_mesh']), np.float32)),
                    sc.get('inst_near', np.zeros(len(sc['inst_mesh']), np.float32))))
    blob = xscn_blob(V, I, meshes, lods, parts, mrecs, inst, lrecs)

    out = a.out or lpaths.LEVELPORT / 'build' / ('%s_scene-%s' % (name, a.tag or '1'))
    out.mkdir(parents=True, exist_ok=True)
    pk3 = out / ('zz_%s_scene.pk3' % name)
    with zipfile.ZipFile(pk3, 'w', allowZip64=False) as z:      # (past 4 GB this fails: the tiled scene splits)
        z.writestr(zipfile.ZipInfo('maps/%s.xscn' % name), blob, compress_type=zipfile.ZIP_DEFLATED, compresslevel=1)
        images.write(z, sc['images'])
        scene_map_files(z, work, centre, radius, a, sig, origin, sc['drecs'], stats, sc['mnames'], sc.get('inst_rot'))
    write_json(out / 'materials.json', materials_json(sc), indent=0)
    write_json(out / 'ad_lights.json', ad_colours, indent=0)   # for --relight
    tris0 = sum(parts[lods[fl][0] + k][1] // 3 for c, r, fl, nl in meshes for k in range(lods[fl][1]))
    print(f'scene -> {pk3} ({pk3.stat().st_size / 1e6:.0f} MB): {len(inst)} instances of {len(meshes)} meshes, '
          f'{len(I) // 3:,} triangles over all LODs ({tris0:,} at LOD 0), {len(mrecs)} materials, '
          f'{len(sc["images"])} images, {len(lrecs)} lights')
    print('  ' + ', '.join(f'{k} {v}' for k, v in sorted(stats.items())) + f', missing inputs {missing}')
    print(f'scene merge counts: detail cutouts {len(sc.get("dcut") or [])}', flush=True)


def scene_inputs(work, centre, radius, a):
    """What every part of an area's scene shares: the map origin, its signal table, the material converter and the
    image cache."""
    import cp77mat
    name = a.map or 'cp_' + a.area
    origin = np.array([centre[0], centre[1], a.floor])
    sig = scene_signals(work, a, centre, radius)
    mats = cp77mat.Materials(work / 'meshes', size=a.scene_texsize, bake_dir=work / 'bake', ml_size=a.texsize,
                             detail_size=a.detail_size, detail_slots=getattr(a, 'detail_slots', 1))
    mats.screen_sheets = bool(getattr(a, 'screen_sheets', False))
    # metal_base_det.mt's tiled detail baked in as a cutout (chain-link fences, grates, vents: 432 of Watson's
    # materials, the default, shipped by patch_materials --detail-cutout). The scene's
    # converter only: the build stage's (collision) keeps them as they were, so no collision changes
    mats.detail_cutout = True
    use_tiles_ad_pool(work, a, mats)
    return name, origin, sig, mats, SceneImages(name, work / 'scene_dds')


def use_tiles_ad_pool(work, a, mats):
    """A tile's part or a merge takes the ad stand-in pool the tiles stage found (tiles/<tag>/ad_pool.json), so every
    part draws from the same pool without reading the whole folder's materials again."""
    f = tile_dir(work, a.tiles_tag) / 'ad_pool.json'
    if (getattr(a, 'tile', None) or getattr(a, 'merge', False)) and f.exists():
        mats.ad_pool = [tuple(p) for p in json.load(open(f, encoding='utf-8'))]


def stage_scene(work, centre, radius, a):
    """Render layer (docs/levelport-scene.md): every placement within --radius over --scene-min metres, each unique
    mesh once with all of its LODs, its baked materials and the sectors' lights, written to maps/<map>.xscn and packed
    with the images into zz_<map>_scene.pk3. One part for the placements and one for the water, joined as tiles are
    (merge_scene_parts), then scene_finish."""
    from collections import Counter
    name, origin, sig, mats, images = scene_inputs(work, centre, radius, a)
    stats = Counter()
    plan = make_plan(work, centre, radius, argparse.Namespace(**dict(vars(a), min_size=a.scene_min)), stats,
                     render=True)
    if a.bake_workers > 1:
        prebake(work, plan, a, a.bake_workers)
    parts = [scene_part(work, a, plan, origin, sig, mats, images, stats),
             scene_part(work, a, [], origin, sig, mats, images, stats, water_from=(centre, radius))]
    images.flush()
    scene_finish(work, centre, radius, a, merge_scene_parts(parts), images, stats, sig, len(mats.missing))


# ---------------------------------------------------------------------------------------------------- tiles

def tile_dir(work, tag):
    return work / 'tiles' / tag


def stage_tiles(work, centre, radius, a):
    """Split an area's placements into square tiles of --tile-size metres on the city's grid, a corner at CP2077's
    world origin (the chunked city: one grid for every district, tile i_j is the same square in
    any area's build; 256 m = 4 level-0 sector cells) (tiles/<tag>/tiles.json, one <i>_<j>.json of placements each,
    a JSON record a line): a placement belongs to the
    tile its placed bounds' centre lies in, so a building across an edge is built once, by one tile. The area keeps its
    own rule for what is in it (in_area by origin), so the tiles together hold the same placements as one build of the
    area. Streams the placements: a tile's file is written as they come."""
    from collections import Counter
    t0 = time.time()
    size = a.tile_size
    # each mesh's local bounds centre (glTF axes), cached in the work folder: loading every mesh took 168 s on Kabuki
    cache_f = work / 'mesh_bounds.json'
    bounds = json.load(open(cache_f, encoding='utf-8')) if cache_f.exists() else {}
    out = tile_dir(work, a.tiles_tag)
    out.mkdir(parents=True, exist_ok=True)
    files, counts, stats, seen, new = {}, Counter(), Counter(), set(), 0
    placed = set()
    try:
        for p in iter_placements(work, centre, radius, proxies=getattr(a, 'proxies', False)):
            if GI_ONLY_NAMES.search(p['mesh'].split(SL)[-1].lower()):
                stats['gi_only'] += 1
                continue
            # the same mesh, appearance and transform twice (6,815 in Watson's tag 7, most within one sector): drawn
            # once. Two copies of a billboard took different stand-in ads and fought
            k = hash(placement_key(p, 3))
            if k in placed:
                stats['duplicate'] += 1
                continue
            placed.add(k)
            # (a mesh cached without bounds is looked at again once a run: it may have been fetched since; one run
            # found 2,492 such, fetched with the interiors, tiled by their origin)
            if p['mesh'] not in bounds or (bounds[p['mesh']] is None and p['mesh'] not in seen):
                ml = MeshLods(work, p['mesh'])
                if ml.ok or p['mesh'] not in bounds:
                    bounds[p['mesh']] = ((ml.lo + ml.hi) / 2).tolist() if ml.ok else None
                    new += 1
            seen.add(p['mesh'])
            mid = bounds[p['mesh']]
            m = np.array(p['m']).reshape(3, 4)
            if mid is not None:
                c = m[:, :3] @ (G2C @ np.asarray(mid)) + m[:, 3]
            else:
                c = m[:, 3]                # the plan drops it anyway (missing_mesh); keep it where it stands
                stats['missing_mesh'] += 1
            if coarse(p) and not in_area(m[0, 3], m[1, 3], centre, radius, m[2, 3]):
                # a level 4+ piece kept provisionally (in_area_p): in the area when its placed bounds' centre is
                if not in_area(c[0], c[1], centre, radius, c[2]):
                    stats['coarse_out'] += 1
                    continue
                stats['coarse_by_bounds'] += 1
            key = (int(np.floor(c[0] / size)), int(np.floor(c[1] / size)))
            if key not in files:
                # (to a temporary name, renamed when complete: a record linked from another tag's tiling,
                # cp77_tiles --from-tag, keeps its bytes)
                files[key] = open(out / ('%d_%d.json.tmp' % key), 'w', encoding='utf-8')
            files[key].write(json.dumps(p) + '\n')
            counts[key] += 1
    finally:
        for f in files.values():
            f.close()
    for key in files:
        os.replace(out / ('%d_%d.json.tmp' % key), out / ('%d_%d.json' % key))
    if new:
        tmp = cache_f.with_suffix('.tmp')
        tmp.write_text(json.dumps(bounds), encoding='utf-8')
        os.replace(tmp, cache_f)
    # the ad stand-in pool, once for every part (use_tiles_ad_pool)
    import cp77mat
    tmp = out / 'ad_pool.tmp'
    tmp.write_text(json.dumps(cp77mat.Materials(work / 'meshes').find_ad_pool()), encoding='utf-8')
    os.replace(tmp, out / 'ad_pool.json')
    tiles = [{'id': '%d_%d' % (i, j), 'i': i, 'j': j, 'placements': counts[(i, j)],
              'box': [i * size, j * size, (i + 1) * size, (j + 1) * size]}
             for i, j in sorted(counts)]
    tmp = out / 'tiles.tmp'
    with open(tmp, 'w') as f:
        json.dump({'area': a.area, 'centre': list(centre), 'radius': radius, 'tile_size': size, 'tiles': tiles},
                  f, indent=1)
    os.replace(tmp, out / 'tiles.json')
    print(f'tiles: {sum(counts.values())} placements of {len(seen)} meshes ({new} bounds new) into {len(tiles)} '
          f'tiles of {size:.0f} m ({", ".join("%s %d" % (t["id"], t["placements"]) for t in tiles)}) '
          f'in {time.time() - t0:.0f} s; ' + ', '.join(f'{k} {v}' for k, v in stats.items()))


def clutter_drops(a, tile, nboxes):
    """the box indices of a tile that lose collision (--clutter-drops: colclutter.py drops' <tile>.json, its 'drop'
    list in the part's box order): dropped where the chunk glb is written, not patched out of
    the compiled BSP afterwards"""
    d = getattr(a, 'clutter_drops', None)
    if not d:
        return set()
    f = Path(d) / ('%s.json' % tile)
    if not f.exists():
        return set()
    drop = set(int(i) for i in json.loads(f.read_text(encoding='utf-8'))['drop'])
    if drop and max(drop) >= nboxes:
        sys.exit('chunks: %s: a clutter drop past its %d boxes (%s): drops made from another part?' % (tile, nboxes, f))
    return drop


def stage_chunks(work, centre, radius, a):
    """Each tile's collision as a glb in the tile's own frame (tiles/<tag>/chunks/<i>_<j>.glb, manifest.json): the
    collision chunks of the chunked city (docs/levelport-watson.md sections 14-15). Collision far from the origin
    is not the collision at the origin (the city's corners: q3map2 builds other bevels, the engine's side tests lose
    two float steps; docs/levelport-collision.md section 8), so a chunk is compiled about its own origin and queried
    in its frame. The origin is the tile's corner on the city grid at --floor (whole metres: 256 m a tile, so the
    shift from any area's frame is exact). From the tile's collision part (build --tile): its triangles and prop
    boxes, and the water volumes whose surface's centre lies in the tile. No boundary ring, lid, sky or spawns: those
    belong to a build of several tiles, not to a tile."""
    if getattr(a, 'inst_split', None):
        return stage_inst_chunks(work, centre, radius, a)
    tdir = tile_dir(work, a.tiles_tag)
    man = json.load(open(tdir / 'tiles.json', encoding='utf-8'))
    size = man['tile_size']
    out = tdir / 'chunks'
    out.mkdir(parents=True, exist_ok=True)
    origin_area = np.array([centre[0], centre[1], a.floor], np.float64)
    to_g = CP_TO_GLTF
    quads = water_quads(water_records(work, centre, radius))
    rows = []
    for t in man['tiles']:
        part = tdir / 'build' / (t['id'] + '.npz')
        if not part.exists():
            sys.exit('chunks: no collision part for tile %s (build --tile first)' % t['id'])
        origin = np.array([t['i'] * size, t['j'] * size, a.floor], np.float64)
        d = (to_g[:3, :3] @ (origin_area - origin))           # area frame -> tile frame, glTF metres
        w = gltfio.Writer()
        g_col = w.node('collision')
        with np.load(part, allow_pickle=True) as z:
            T = z['col'].astype(np.float64) + d
            box_depot, box_lohi, box_G = list(z['box_depot']), z['box_lohi'], z['box_G'].astype(np.float64)
        if len(T):
            cell = np.floor(T.mean(1)[:, [0, 2]] / 32.0).astype(int)
            mi = w.material('collision_solid', extras={'surfaceparms': []})
            for c in np.unique(cell, axis=0):
                sel = np.all(cell == c, axis=1)
                pos = T[sel].reshape(-1, 3)
                w.node('col_%d_%d' % tuple(c), mesh=w.mesh('col_%d_%d' % tuple(c), [{
                    'position': pos, 'indices': np.arange(len(pos)).reshape(-1, 3), 'material': mi}]),
                    parent=g_col, extras={'lp': 'collision', 'shape': 'mesh', 'contents': ['solid']})
        box_mi, box_cache, depots = w.material('collision_box', extras={'surfaceparms': []}), {}, set()
        dropped = clutter_drops(a, t['id'], len(box_depot))
        for k, (depot, lh, G) in enumerate(zip(box_depot, box_lohi, box_G)):
            if k in dropped:                # (no collision: road clutter, colclutter.py's rule; the node numbers keep
                continue                    # the part's box order, b<k>)
            depot, lo, hi = str(depot), lh[:3], lh[3:]
            # one mesh per shape, not per mesh path: a split frame's posts and lintel are boxes of one mesh with
            # other bounds (frame_pieces; every piece used to take the first box's shape)
            key = (depot, tuple(lh.tolist()))
            if key not in box_cache:
                c = np.array([[x, y, zz] for y in (lo[1], hi[1]) for x, zz in
                              ((lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2]))])
                box_cache[key] = w.mesh('box_' + depot.split(SL)[-1][:-5] + ('_%d' % len(box_cache) if depot in depots
                                                                            else ''), [{
                    'position': c, 'indices': BOX_FACES, 'material': box_mi}])
                depots.add(depot)
            G = G.copy()
            G[:3, 3] += d
            w.node('b%d' % k, mesh=box_cache[key], matrix=G, parent=g_col,
                   extras={'lp': 'collision', 'contents': ['solid', 'detail']})
        nwater = 0
        mine = [q for q in quads if t['box'][0] <= np.mean([v[0] for v in q]) < t['box'][2]
                and t['box'][1] <= np.mean([v[1] for v in q]) < t['box'][3]]
        if mine:
            wat_mi = w.material('collision_water', extras={'surfaceparms': []})
            bed_mi = w.material('collision_water_bed', extras={'surfaceparms': []})
            for i, q in enumerate(mine):
                for tag, z0, z1, contents, mi in (('water', -WATER_DEPTH, 0.0, ['water', 'trans', 'detail'], wat_mi),
                                                  ('wbed', -WATER_DEPTH - 1.0, -WATER_DEPTH, ['solid', 'detail'], bed_mi)):
                    box = np.array([[x, y, q[0][2] + zz] for zz in (z0, z1) for x, y, _ in q]) - origin
                    pos = (np.c_[box, np.ones(len(box))] @ to_g.T)[:, :3]
                    w.node('%s%d' % (tag, i), mesh=w.mesh('%s%d' % (tag, i), [{
                        'position': pos, 'indices': BOX_FACES, 'material': mi}]),
                        parent=g_col, extras={'lp': 'collision', 'contents': contents})
                nwater += 1
        f = out / (t['id'] + '.glb')
        tmp = f.with_name(f.stem + '.tmp.glb')
        w.save(tmp, extras={'levelport': {'version': 1, 'units_per_metre': gltf_upm(), 'source_game': 'cyberpunk_2077',
                                          'source': 'chunk %s of %s, tiles %s' % (t['id'], a.area, a.tiles_tag),
                                          'chunk': {'tile': t['id'], 'origin_cp_m': origin.tolist()},
                                          'worldspawn': {'message': 'Night City: chunk %s' % t['id']}}})
        os.replace(tmp, f)
        rows.append({'tile': t['id'], 'i': t['i'], 'j': t['j'], 'box_cp_m': t['box'],
                     'origin_cp_m': origin.tolist(),
                     # where the chunk's frame sits in this area's frame (glTF metres): the area's build is the
                     # chunks moved by this, a whole multiple of the tile size
                     'offset_in_area_gltf_m': (-d).tolist(),
                     'col_tris': int(len(T)), 'boxes': len(box_depot), 'water': nwater, 'glb': f.name,
                     **({'clutter_dropped': len(dropped)} if dropped else {})})
    # the rest of a build of these tiles, in the area's frame: ring, lid, lights, spawns (build --merge --base-only),
    # kept while what it is made of holds: the tiles' collision parts, placements.json (its lights and entities), the
    # entity templates and appearances, the code and the arguments (it was rebuilt every run)
    b = argparse.Namespace(**vars(a))
    b.merge, b.tile, b.base_only, b.no_render = True, None, True, True
    b.clutter_drops = None             # (the base build has no boxes: not in its key)

    def base_key():
        return stage_record_key(['stage_build'], {k: v for k, v in sorted(vars(b).items())}, [
            tdir / 'tiles.json', work / 'placements.json', work / 'ents', work / 'apps', out / 'base.glb'] + [
            tdir / 'build' / (t['id'] + '.npz') for t in man['tiles']])
    rec = out / 'base.key.json'
    if (out / 'base.glb').exists() and stage_kept(rec, 'base', base_key()):
        print('chunks: base.glb kept (its parts, placements, entity templates, code and arguments unchanged)')
    else:
        stage_build(work, centre, radius, b)
        stage_keep(rec, 'base', base_key())
    tmp = out / 'manifest.tmp'
    tmp.write_text(json.dumps({'area': a.area, 'tiles_tag': a.tiles_tag, 'tile_size_m': size, 'floor_cp_m': a.floor,
                               'units_per_metre': gltf_upm(), 'area_origin_cp_m': origin_area.tolist(),
                               # base.glb: the area build's ring, lid, lights and spawns, in the area's glTF frame
                               # (the frame of <area>-<tag>.glb); each chunk sits in it at offset_in_area_gltf_m
                               'base': {'glb': 'base.glb', 'frame': 'area'},
                               'chunks': rows}, indent=1), encoding='utf-8')
    os.replace(tmp, out / 'manifest.json')
    print('chunks: %d tiles -> %s (%d collision triangles, %d boxes, %d water volumes)' % (
        len(rows), out, sum(r['col_tris'] for r in rows), sum(r['boxes'] for r in rows), sum(r['water'] for r in rows)))


def stage_inst_chunks(work, centre, radius, a):
    """chunks --inst-split DIR (STEP0-FORMATS.md): from the parts build --inst-split wrote for every tile of the tiling
    (DIR/parts/<tile>.npz), for --inst-tiles (default all): DIR/chunks/<tile>.glb (today's chunk without the instanced
    placements' triangles, its cell nodes split by band: col_<i>_<j>_<band>), DIR/inst/<tile>.json (the instances),
    DIR/protos/<key16>.glb and index.json (the prototypes they place), DIR/col_counts.json. Rule 6 (a key placed at
    least INST_MIN_PLACED times) is counted exactly, over every tile's candidates (placements passing rules 1-5). Each
    part's triangles, boxes and stats are checked against the tiling's own part (tiles/<tag>/build/<tile>.npz): the
    split starts from today's collision, not from a rebuild that drifted."""
    from collections import Counter
    D = Path(a.inst_split)
    tdir = tile_dir(work, a.tiles_tag)
    man = json.load(open(tdir / 'tiles.json', encoding='utf-8'))
    size = man['tile_size']
    ids = [t['id'] for t in man['tiles']]
    byid = {t['id']: t for t in man['tiles']}
    missing = [i for i in ids if not (D / 'parts' / (i + '.npz')).exists()]
    if missing:
        sys.exit('chunks --inst-split: no part for %d tiles (build --inst-split --tile first): %s' % (
            len(missing), ' '.join(missing[:12])))
    want = [s for s in (a.inst_tiles or '').split(',') if s] or ids
    if set(want) - set(ids):
        sys.exit('chunks --inst-tiles: not tiles of %s: %s' % (a.tiles_tag, ' '.join(sorted(set(want) - set(ids)))))
    # rule 6 over every tile; a key's prototype must be the same triangles in every part that holds it
    city, phash, mismatch, rej_city = Counter(), {}, set(), Counter()
    for tid in ids:
        with np.load(D / 'parts' / (tid + '.npz'), allow_pickle=True) as z:
            city.update(z['cand_key'].tolist())
            rej_city.update(json.loads(str(z['inst_stats'])))
            off, PT = z['proto_off'], z['proto_tris']
            for i, k in enumerate(z['proto_key'].tolist()):
                h = hashlib.sha1(np.ascontiguousarray(PT[off[i]:off[i + 1]]).tobytes()).hexdigest()
                if phash.setdefault(k, h) != h:
                    mismatch.add(k)
    if mismatch:
        sys.exit('chunks --inst-split: %d keys with different prototypes in different parts' % len(mismatch))
    owngrid = getattr(a, 'inst_owngrid', False)
    for sub in ('chunks_base_owngrid',) if owngrid else ('chunks', 'inst', 'protos'):
        (D / sub).mkdir(parents=True, exist_ok=True)
    origin_area = np.array([centre[0], centre[1], a.floor], np.float64)
    to_g = CP_TO_GLTF
    quads = water_quads(water_records(work, centre, radius))
    protos, placed_here, rows, counts = {}, Counter(), [], []
    for tid in want:
        t = byid[tid]
        origin = np.array([t['i'] * size, t['j'] * size, a.floor], np.float64)
        d = (to_g[:3, :3] @ (origin_area - origin))           # area frame -> tile frame, glTF metres
        with np.load(D / 'parts' / (tid + '.npz'), allow_pickle=True) as z, \
                np.load(tdir / 'build' / (tid + '.npz'), allow_pickle=True) as r:
            same = (np.array_equal(z['col'], r['col']) and np.array_equal(z['box_lohi'], r['box_lohi'])
                    and np.array_equal(z['box_G'], r['box_G']) and list(z['box_depot']) == list(r['box_depot'])
                    and json.loads(str(z['stats'])) == json.loads(str(r['stats'])))
            ref_tris = len(r['col'])
            col = z['col'].astype(np.float64)
            band, cc = z['col_band'], z['col_cand']
            ck, cG, cf, cb, ct, cs = (z['cand_key'].tolist(), z['cand_G'].astype(np.float64), z['cand_frame'].tolist(),
                                      z['cand_band'], z['cand_tris'], z['cand_size'])
            box_depot, box_lohi, box_G = list(z['box_depot']), z['box_lohi'], z['box_G'].astype(np.float64)
            off, PT = z['proto_off'], z['proto_tris']
            pmeta = json.loads(str(z['proto_meta']))
            pk = {k: i for i, k in enumerate(z['proto_key'].tolist())}
            rej = json.loads(str(z['inst_stats']))
            on = np.array([city[k] >= INST_MIN_PLACED for k in ck], bool)
            for k in set(k for k, o in zip(ck, on) if o) - set(protos):
                i = pk[k]
                (mesh, app, level, path), pband = pmeta[k]
                protos[k] = {'T': PT[off[i]:off[i + 1]].astype(np.float64), 'mesh': mesh, 'app': app, 'level': level,
                             'path': path, 'band': pband}
        if len(col) != len(band) or len(col) != len(cc):
            sys.exit('chunks --inst-split: %s: bands or candidates do not cover the triangles' % tid)
        moved = (cc >= 0) & (on[np.maximum(cc, 0)] if len(on) else np.zeros(len(cc), bool))
        # (--inst-base: chunks_base/, the same tiles banded the same way with nothing moved: today's rule at this
        # converter, for a same-build baseline. --inst-owngrid alone: chunks_base_owngrid/, the instanced split with
        # each instance's prototype placed back as world triangles, the geometry the instances give on their own grid,
        # for the engine check; nothing else is rewritten)
        if owngrid:
            subs = [('chunks_base_owngrid', moved)]
        else:
            subs = [('chunks', moved)] + ([('chunks_base', np.zeros_like(moved))] if a.inst_base else [])
        for sub, mv in subs:
            T, B = col[~mv] + d, band[~mv]
            if sub == 'chunks_base_owngrid':
                cis = np.flatnonzero(on)
                own = [protos[ck[ci]]['T'] @ cG[ci][:3, :3].T + (cG[ci][:3, 3] + d) for ci in cis]
                if own:
                    T = np.concatenate([T] + own)
                    B = np.concatenate([B] + [np.full(len(o), cb[ci], B.dtype) for o, ci in zip(own, cis)])
            w = gltfio.Writer()
            g_col = w.node('collision')
            if len(T):
                cell = np.floor(T.mean(1)[:, [0, 2]] / 32.0).astype(int)
                mi = w.material('collision_solid', extras={'surfaceparms': []})
                for c in np.unique(cell, axis=0):
                    inc = np.all(cell == c, axis=1)
                    for bi, bn in enumerate(INST_BANDS):
                        sel = inc & (B == bi)
                        if not sel.any():
                            continue
                        pos = T[sel].reshape(-1, 3)
                        nm = 'col_%d_%d_%s' % (c[0], c[1], bn)
                        w.node(nm, mesh=w.mesh(nm, [{'position': pos, 'indices': np.arange(len(pos)).reshape(-1, 3),
                                                     'material': mi}]),
                               parent=g_col, extras={'lp': 'collision', 'shape': 'mesh', 'contents': ['solid'], 'band': bn})
            box_mi, box_cache, depots = w.material('collision_box', extras={'surfaceparms': []}), {}, set()
            for k, (depot, lh, G) in enumerate(zip(box_depot, box_lohi, box_G)):
                depot, lo, hi = str(depot), lh[:3], lh[3:]
                key = (depot, tuple(lh.tolist()))     # one mesh per shape (stage_chunks)
                if key not in box_cache:
                    c = np.array([[x, y, zz] for y in (lo[1], hi[1]) for x, zz in
                                  ((lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2]))])
                    box_cache[key] = w.mesh('box_' + depot.split(SL)[-1][:-5] + ('_%d' % len(box_cache) if depot in
                                                                                depots else ''), [{
                        'position': c, 'indices': BOX_FACES, 'material': box_mi}])
                    depots.add(depot)
                G = G.copy()
                G[:3, 3] += d
                w.node('b%d' % k, mesh=box_cache[key], matrix=G, parent=g_col,
                       extras={'lp': 'collision', 'contents': ['solid', 'detail'], 'band': 'near'})
            nwater = 0
            mine = [q for q in quads if t['box'][0] <= np.mean([v[0] for v in q]) < t['box'][2]
                    and t['box'][1] <= np.mean([v[1] for v in q]) < t['box'][3]]
            if mine:
                wat_mi = w.material('collision_water', extras={'surfaceparms': []})
                bed_mi = w.material('collision_water_bed', extras={'surfaceparms': []})
                for i, q in enumerate(mine):
                    for tag, z0, z1, contents, mi in (('water', -WATER_DEPTH, 0.0, ['water', 'trans', 'detail'], wat_mi),
                                                      ('wbed', -WATER_DEPTH - 1.0, -WATER_DEPTH, ['solid', 'detail'], bed_mi)):
                        box = np.array([[x, y, q[0][2] + zz] for zz in (z0, z1) for x, y, _ in q]) - origin
                        pos = (np.c_[box, np.ones(len(box))] @ to_g.T)[:, :3]
                        w.node('%s%d' % (tag, i), mesh=w.mesh('%s%d' % (tag, i), [{
                            'position': pos, 'indices': BOX_FACES, 'material': mi}]),
                            parent=g_col, extras={'lp': 'collision', 'contents': contents, 'band': 'structure'})
                    nwater += 1
            (D / sub).mkdir(parents=True, exist_ok=True)
            f = D / sub / (tid + '.glb')
            tmp = f.with_name(f.stem + '.tmp.glb')
            w.save(tmp, extras={'levelport': {'version': 1, 'units_per_metre': gltf_upm(),
                                              'source_game': 'cyberpunk_2077',
                                              'source': 'chunk %s of %s, tiles %s, %s' % (
                                                  tid, a.area, a.tiles_tag,
                                                  {'chunks': 'instanced split',
                                                   'chunks_base_owngrid': 'instanced split, the instances placed back '
                                                                          'as world triangles'}.get(
                                                      sub, 'banded, nothing moved')),
                                              'chunk': {'tile': tid, 'origin_cp_m': origin.tolist()},
                                              'inst': {'instances': 'inst/%s.json' % tid if sub == 'chunks' else None,
                                                       'bands': list(INST_BANDS)},
                                              'worldspawn': {'message': 'Night City: chunk %s' % tid}}})
            os.replace(tmp, f)
        if owngrid:
            print('chunks --inst-owngrid %s: %d instances placed back, %d triangles left' % (
                tid, int(on.sum()), int((~moved).sum())))
            continue
        # the instances, in the chunk's frame (G as today's code applies it, then the tile's shift)
        inst = []
        for ci in np.flatnonzero(on):
            k = ck[ci]
            G = cG[ci].copy()
            G[:3, 3] += d
            W = protos[k]['T'] @ G[:3, :3].T + G[:3, 3]
            inst.append({'proto': k, 'G': [float(v) for v in G[:3, :4].reshape(-1)], 'frame': cf[ci],
                         'tris': int(len(protos[k]['T'])), 'tris_chunk': int(ct[ci]), 'band': INST_BANDS[cb[ci]],
                         'size': float(cs[ci]),
                         'bounds': [W.reshape(-1, 3).min(0).tolist(), W.reshape(-1, 3).max(0).tolist()]})
            placed_here[k] += 1
        f = D / 'inst' / (tid + '.json')
        tmp = f.with_suffix('.tmp')
        tmp.write_text(json.dumps({'tile': tid, 'origin_cp_m': origin.tolist(), 'instances': inst}), encoding='utf-8')
        os.replace(tmp, f)
        mv_band = np.bincount(band[moved], minlength=3) if moved.any() else np.zeros(3, int)
        left_band = np.bincount(band[~moved], minlength=3) if (~moved).any() else np.zeros(3, int)
        counts.append({'tile': tid, 'same_as_tag': bool(same), 'tris_tag': int(ref_tris), 'tris_today': int(len(col)),
                       'instanced': len(inst), 'tris_moved': int(moved.sum()), 'tris_left': int((~moved).sum()),
                       'proto_tris_placed': int(sum(i['tris'] for i in inst)),
                       'candidates': len(ck), 'rare': int(len(ck) - on.sum()),
                       'moved_by_band': dict(zip(INST_BANDS, map(int, mv_band))),
                       'left_by_band': dict(zip(INST_BANDS, map(int, left_band))),
                       'boxes': len(box_depot), 'water': nwater, 'not_candidates': rej})
        rows.append({'tile': tid, 'i': t['i'], 'j': t['j'], 'box_cp_m': t['box'], 'origin_cp_m': origin.tolist(),
                     'offset_in_area_gltf_m': (-d).tolist(), 'col_tris': int((~moved).sum()), 'boxes': len(box_depot),
                     'water': nwater, 'instances': len(inst), 'glb': tid + '.glb', 'inst': '../inst/%s.json' % tid})
        print('chunks --inst-split %s: %d instances, %d of %d triangles moved%s' % (
            tid, len(inst), moved.sum(), len(col), '' if same else ' (DIFFERS from tag %s)' % a.tiles_tag))
    if owngrid:
        return
    # the prototypes these tiles place
    index = {}
    for k in sorted(protos):
        P = protos[k]
        w = gltfio.Writer()
        g_col = w.node('collision')
        mi = w.material('collision_solid', extras={'surfaceparms': []})
        pos = P['T'].reshape(-1, 3)
        w.node('proto_%s' % k, mesh=w.mesh('proto_%s' % k, [{
            'position': pos, 'indices': np.arange(len(pos)).reshape(-1, 3), 'material': mi}]),
            parent=g_col, extras={'lp': 'collision', 'contents': ['solid'], 'shape': 'mesh', 'band': P['band']})
        f = D / 'protos' / (k + '.glb')
        tmp = f.with_name(f.stem + '.tmp.glb')
        w.save(tmp, extras={'levelport': {'version': 1, 'units_per_metre': gltf_upm(), 'source_game': 'cyberpunk_2077',
                                          'source': 'collision prototype %s of %s, tiles %s' % (k, a.area, a.tiles_tag),
                                          'prototype': {'key16': k, 'mesh': P['mesh'], 'app': P['app'],
                                                        'level': P['level'], 'path': P['path']},
                                          'worldspawn': {'message': 'Night City: prototype %s' % k}}})
        os.replace(tmp, f)
        index[k] = {'mesh': P['mesh'], 'app': P['app'], 'level': P['level'], 'path': P['path'], 'band': P['band'],
                    'tris': int(len(P['T'])), 'bounds': [pos.min(0).tolist(), pos.max(0).tolist()],
                    'placed_city': int(city[k]), 'placed_dense4': int(placed_here[k])}
    for name, obj in (('protos/index.json', index),
                      ('chunks/manifest.json', {'area': a.area, 'tiles_tag': a.tiles_tag, 'tile_size_m': size,
                                                'floor_cp_m': a.floor, 'units_per_metre': gltf_upm(),
                                                'area_origin_cp_m': origin_area.tolist(),
                                                'base': {'glb': '../../%s/chunks/base.glb' % a.tiles_tag,
                                                         'frame': 'area'}, 'chunks': rows})):
        f = D / name
        tmp = f.with_suffix('.tmp')
        tmp.write_text(json.dumps(obj, indent=1), encoding='utf-8')
        os.replace(tmp, f)
    tot = {k: sum(c[k] for c in counts) for k in ('instanced', 'tris_tag', 'tris_today', 'tris_moved', 'tris_left',
                                                  'proto_tris_placed', 'candidates', 'rare', 'boxes', 'water')}
    for k in ('moved_by_band', 'left_by_band'):
        tot[k] = {b: sum(c[k][b] for c in counts) for b in INST_BANDS}
    rej_here = Counter()
    for c in counts:
        rej_here.update(c['not_candidates'])
    tot['not_candidates'] = dict(rej_here)
    tot['same_as_tag'] = sum(c['same_as_tag'] for c in counts)
    tot['protos'] = len(index)
    tot['proto_tris'] = sum(v['tris'] for v in index.values())
    out = {'tiles_tag': a.tiles_tag, 'tiles': len(want), 'tiles_counted': len(ids),
           'rule6': {'min_placed': INST_MIN_PLACED, 'method': 'exact: every candidate (rules 1-5) of every one of the '
                     '%d tiles counted by its key, from the parts of build --inst-split' % len(ids),
                     'keys_city': len(city), 'keys_placed_enough': sum(1 for v in city.values()
                                                                       if v >= INST_MIN_PLACED),
                     'candidates_city': int(sum(city.values())),
                     'candidates_city_placed_enough': int(sum(v for v in city.values() if v >= INST_MIN_PLACED)),
                     'not_candidates_city': dict(rej_city)},
           'total': tot, 'per_tile': counts}
    f = D / 'col_counts.json'
    tmp = f.with_suffix('.tmp')
    tmp.write_text(json.dumps(out, indent=1), encoding='utf-8')
    os.replace(tmp, f)
    print('chunks --inst-split: %d tiles, %d instances of %d prototypes (%d triangles), %d of %d triangles moved, '
          '%d of %d tiles the same as tag %s' % (len(want), tot['instanced'], tot['protos'], tot['proto_tris'],
                                                 tot['tris_moved'], tot['tris_tag'], tot['same_as_tag'], len(want),
                                                 a.tiles_tag))


def stage_scene_tile(work, centre, radius, a, tid):
    """One tile's scene part (tiles/<tag>/scene/<tile>/): its own placements only; images into the shared cache."""
    from collections import Counter
    name, origin, sig, mats, images = scene_inputs(work, centre, radius, a)
    stats = Counter()
    pl = read_records(tile_dir(work, a.tiles_tag) / (tid + '.json'))
    opened = door_open_keys(work, a.tiles_tag)
    if opened:                          # (doors the collision build opened: their leaves are not drawn either)
        n0 = len(pl)
        pl = [q for q in pl if door_leaf(q) not in opened]
        stats['door_leaves_open'] = n0 - len(pl)
    fol = tile_dir(work, a.tiles_tag) / 'foliage' / (tid + '.jsonl')
    if fol.exists():                    # (CP2077's foliage, cp77_foliage.py: the render layer's alone)
        fl = read_records(fol)
        stats['foliage'] = len(fl)
        pl = pl + fl
    plan = make_plan(work, centre, radius, argparse.Namespace(**dict(vars(a), min_size=a.scene_min)), stats,
                     placements=pl, render=True)
    if a.bake_workers > 1:
        prebake(work, plan, a, a.bake_workers)
    part = scene_part(work, a, plan, origin, sig, mats, images, stats)
    new = images.flush()
    save_part(part, stats, tile_dir(work, a.tiles_tag) / 'scene' / tid)
    print(f'scene tile {tid}: {len(plan)} placements, {len(part["inst_mesh"])} instances of {len(part["meshkeys"])} '
          f'meshes, {len(part["mkeys"])} materials, {len(part["images"])} images ({new} new)')
    import cp77mat
    print(f'scene tile {tid} counts: matcache {part["matcache"][0]} hits {part["matcache"][1]} misses; metal_base_det '
          f'cutouts {len(part["dcut"])}; placeholders {PLACEHOLDERS["materials"]} materials in '
          f'{PLACEHOLDERS["meshes"]} meshes (setups index: {cp77mat.SETUPS_STATS["kept"]} kept, '
          f'{cp77mat.SETUPS_STATS["scans"]} scans)', flush=True)


def stage_scene_hide(work, centre, radius, a):
    """Scene parts built before the scan kept CP's hide distances: each part's instances get theirs from
    this scan's placements (the PlacementStore), matched by name and origin (a part's instances are its placements in
    order, then its water), as inst_hide.npy beside the part (game units, 0 = none). A part already holding
    inst_hide is skipped. --tile: one tile; otherwise every tile of --tiles-tag."""
    from collections import Counter
    upm = gltf_upm()
    store = PlacementStore(work)
    tdir = tile_dir(work, a.tiles_tag)
    man = json.load(open(tdir / 'tiles.json', encoding='utf-8'))
    ids = [a.tile] if a.tile else sorted(t['id'] for t in man['tiles'])
    total = Counter()
    for tid in ids:
        folder = tdir / 'scene' / tid
        if not (folder / 'arrays.npz').exists():
            print(f'scene-hide {tid}: no part')
            continue
        with np.load(folder / 'arrays.npz') as z:
            if 'inst_hide' in z.files:
                print(f'scene-hide {tid}: built with hide distances, kept')
                continue
            pos, n_inst = z['placed_pos'], len(z['inst_mesh'])
        names = json.loads((folder / 'part.json').read_text(encoding='utf-8'))['placed_name']
        cells = {'%d_%d' % (math.floor(x / store.CELL), math.floor(y / store.CELL)) for x, y, _ in pos}
        look, clash = {}, 0
        for c in cells:
            if c not in store.counts:
                continue
            for q in store.cell(c):
                k = (q.get('name'), round(q['m'][3], 3), round(q['m'][7], 3), round(q['m'][11], 3))
                h = float(q.get('hide') or 0)
                if k in look and look[k] != h:
                    clash += 1
                    # the farther: never hide sooner than the source; 0 is "never hides", so it wins
                    h = 0.0 if 0.0 in (h, look[k]) else max(h, look[k])
                look[k] = h
        hide = np.zeros(n_inst, np.float32)
        st = Counter()
        for i, (nm, (x, y, zz)) in enumerate(zip(names, pos)):
            h = look.get((nm, round(float(x), 3), round(float(y), 3), round(float(zz), 3)))
            st['matched' if h is not None else 'unmatched'] += 1
            if h:
                hide[i] = upm * h
                st['with_hide'] += 1
        st['water'] = n_inst - len(names)
        st['clashes'] = clash
        np.save(folder / 'inst_hide.npy', hide)
        total.update(st)
        print(f'scene-hide {tid}: ' + ', '.join(f'{k} {v}' for k, v in sorted(st.items())), flush=True)
    print('scene-hide: ' + ', '.join(f'{k} {v}' for k, v in sorted(total.items())))
    if total['unmatched']:
        print(f'scene-hide: {total["unmatched"]} instances found no placement (no hide distance; drawn as before)')


SCENE_FILE_MAX = 1 << 30        # a tiled scene's files: the renderer reads each whole into an int length
SCENE_IMAGE_PACK = 1500 << 20   # images per pack: no pk3 near 4 GB (zip64)
SCENE_SHARD_PACK = 1_900_000_000  # stored scene shards per pack: zipfile writes no offset past 2 GB without zip64
#                                   (the first pack's manifest and map files ride under it)


def stage_scene_tiled(work, centre, radius, a):
    """The area's scene as a tiled scene (docs/levelport-scene.md, "Tiled scenes"), one part at a time, so a district
    never sits in memory whole: per tile a geometry shard (the meshes it is first to place) and an instance file (its
    instances and the lights whose origin is in its square), one material table, maps/<map>.xman; the images over
    packs of at most SCENE_IMAGE_PACK. The parts are taken in the merge's order (tiles.json, then the water), so the
    shards end to end are the sections stage_scene_merge would write."""
    from collections import Counter
    import zipfile
    name, origin, sig, mats, images = scene_inputs(work, centre, radius, a)
    tdir = tile_dir(work, a.tiles_tag)
    man = json.load(open(tdir / 'tiles.json', encoding='utf-8'))
    tiles = man['tiles']
    if getattr(a, 'scene_tiles', None):
        # a part of the area only (the joined scene, 3.59 GB, passes rd-vulkan's 2 GB scene buffer)
        keep = {l.strip() for l in open(a.scene_tiles, encoding='utf-8') if l.strip()}
        tiles = [t for t in tiles if t['id'] in keep]
        print('scene tiled: %d tiles of %d (%s)' % (len(tiles), len(man['tiles']), a.scene_tiles), flush=True)
    lacking = [t['id'] for t in tiles if not (tdir / 'scene' / t['id'] / 'part.json').exists()]
    if lacking:
        sys.exit('scene tiled: no part for tiles %s' % ', '.join(lacking))
    upm, size = gltf_upm(), float(man['tile_size'])
    final = a.out or lpaths.LEVELPORT / 'build' / ('%s_scene-%s' % (name, a.tag or '1'))
    if (final / ('zz_%s_scene.pk3' % name)).exists():
        sys.exit('scene tiled: %s exists (a numbered build is never overwritten)' % final)
    # written into <build>.partial and renamed into place only when every pack is complete: a failure part way left
    # the final name present, so the retry refused it, and a main pack without its image packs
    out = final.with_name(final.name + '.partial')
    if out.exists():
        # a failed attempt's folder is set aside, never reused: its extra shard packs (zz_<map>_scene2/3.pk3) would
        # survive into this build and override its geometry
        aside = out.with_name('%s.failed-%s' % (out.name, time.strftime('%Y%m%d-%H%M%S')))
        out.rename(aside)
        print('scene tiled: an earlier attempt left %s; moved to %s' % (out.name, aside.name), flush=True)
    out.mkdir(parents=True)
    pk3 = out / ('zz_%s_scene.pk3' % name)
    stats = Counter()
    m = SceneMerger()
    lines, rows = ['xman 1', 'tile_size %.6g' % (size * upm),
                   'grid_origin %.6g %.6g' % tuple(-origin[:2] * upm)], []
    tile_inst = {}

    # the shards stored, not deflated: the renderer's scene streaming reads them by range in the background
    # (FS_RangeReadAsync takes stored entries only), so a pack holds about their raw
    # size, and past SCENE_SHARD_PACK they go on in zz_<map>_scene2.pk3, 3, ... (the first keeps the manifest)
    more = []

    def zput(z, path, blob):
        if len(blob) > SCENE_FILE_MAX:
            sys.exit('scene tiled: %s is %.0f MB, over the %d MB a file may be' % (path, len(blob) / 2 ** 20,
                                                                                  SCENE_FILE_MAX >> 20))
        w = more[-1] if more else z
        if w.fp.tell() + len(blob) + 4096 > SCENE_SHARD_PACK:
            more.append(zipfile.ZipFile(out / ('zz_%s_scene%d.pk3' % (name, len(more) + 2)), 'w', allowZip64=False))
            w = more[-1]
        w.writestr(zipfile.ZipInfo(path), blob, compress_type=zipfile.ZIP_STORED)
    with zipfile.ZipFile(pk3, 'w', allowZip64=False) as z:
        order = [(t['id'], tdir / 'scene' / t['id']) for t in tiles] + [('water', None)]
        for tid, folder in order:
            if folder is None:
                P = scene_part(work, a, [], origin, sig, mats, images, stats, water_from=(centre, radius))
            else:
                P = load_part(folder)
                stats.update(P.pop('stats'))
            b = (len(m.meshes), len(m.lods), len(m.parts), m.nv, m.ni)
            v, i, ins = m.add(P)
            V = np.concatenate(v) if v else np.zeros(0, SCENE_VERT)
            I = np.concatenate(i) if i else np.zeros(0, '<u4')
            path = 'maps/%s/g_%s.xscn' % (name, tid)
            zput(z, path, xscn_blob(V, I, m.meshes[b[0]:], m.lods[b[1]:], m.parts[b[2]:], [], [], []))
            lines.append('lib %s %d %d %d %d %d' % (path, len(m.meshes) - b[0], len(m.lods) - b[1],
                                                   len(m.parts) - b[2], len(V), len(I)))
            tile_inst[tid] = ins
            print('scene tiled: %s: %d instances, %d new meshes, %.0f MB of geometry' % (
                tid, len(ins[1]), len(m.meshes) - b[0], (V.nbytes + I.nbytes) / 2 ** 20), flush=True)
            del P, v, i, V, I
        images.flush()
        sc = m.result([], [], [])
        lrecs, ad_colours = scene_light_records(work, centre, radius, a, sc, stats, sig, origin)
        path = 'maps/%s/materials.xscn' % name
        zput(z, path, xscn_blob(np.zeros(0, SCENE_VERT), np.zeros(0, '<u4'), [], [], [], sc['mrecs'], [], []))
        lines.append('mat %s %d' % (path, len(sc['mrecs'])))
        # each light to the tile its origin is in (the nearest tile when it is in none: the water's are kept by its
        # surfaces' tiles)
        ij = {t['id']: (t['i'], t['j']) for t in tiles}
        centres = np.array([[(i + 0.5) * size, (j + 0.5) * size] for i, j in ij.values()])
        ids = list(ij)
        by_tile = {tid: [] for tid, _ in order}
        for L in lrecs:
            cp = np.asarray(L[0][:2]) / upm + origin[:2]
            key = '%d_%d' % (int(np.floor(cp[0] / size)), int(np.floor(cp[1] / size)))
            if key not in ij:
                key = ids[int(np.argmin(((centres - cp) ** 2).sum(1)))]
            by_tile[key].append(L)
        mc = np.array([c + (r,) for c, r, _, _ in sc['meshes']], np.float64).reshape(-1, 4)
        for tid, _ in order:
            W, mi, slot, hide, near = tile_inst[tid]
            inst = list(zip(W, mi, slot, hide, near))
            Ls = by_tile[tid]
            path = 'maps/%s/t_%s.xscn' % (name, tid)
            zput(z, path, xscn_blob(np.zeros(0, SCENE_VERT), np.zeros(0, '<u4'), [], [], [], [], inst, Ls))
            if len(mi):
                R = np.asarray(W, np.float64)
                c = np.einsum('kij,kj->ki', R[:, :, :3], mc[mi, :3]) + R[:, :, 3]
                r = mc[mi, 3] * np.sqrt((R[:, :, :3] ** 2).sum((1, 2)))
                lo, hi = (c - r[:, None]).min(0), (c + r[:, None]).max(0)
            else:
                lo = hi = np.zeros(3)
            # the last number: the tile's light shapes (version 6; a line without it has none)
            lines.append('tile %s %s %d %d %d %.1f %.1f %.1f %.1f %.1f %.1f %d' % (tid, path, len(inst), len(Ls),
                                                                                len(Ls), *lo, *hi, len(Ls)))
            rows.append((tid, len(inst), len(Ls)))
        z.writestr(zipfile.ZipInfo('maps/%s.xman' % name), '\n'.join(lines) + '\n')
        scene_map_files(z, work, centre, radius, a, sig, origin, sc['drecs'], stats, sc['mnames'], sc['inst_rot'],
                        ocean_mesh=m.mesh_g.get(repr(('water', 'default'))))
    for q in more:
        q.close()
    # the images over packs of at most SCENE_IMAGE_PACK
    packs, cur, size_now = [], [], 0
    for path in sc['images']:
        n = images.cached(path).stat().st_size
        if cur and size_now + n > SCENE_IMAGE_PACK:
            packs.append(cur)
            cur, size_now = [], 0
        cur.append(path)
        size_now += n
    if cur:
        packs.append(cur)
    for k, paths in enumerate(packs):
        with zipfile.ZipFile(out / ('zz_%s_scene_img%d.pk3' % (name, k)), 'w', allowZip64=False) as z:
            images.write(z, paths)
    write_json(out / 'materials.json', materials_json(sc), indent=0)
    write_json(out / 'ad_lights.json', ad_colours, indent=0)
    if final.exists():
        final.rmdir()       # (an empty folder left by an older writer; one holding files stops the rename)
    os.replace(out, final)
    out = final
    print(f'scene tiled -> {out}: {len(order)} tiles, {sum(r[1] for r in rows)} instances of {len(sc["meshes"])} meshes, '
          f'{m.nv:,} vertices, {m.ni // 3:,} triangles over all LODs, {len(sc["mrecs"])} materials, '
          f'{len(sc["images"])} images in {len(packs)} packs, {len(lrecs)} lights')
    print('  ' + ', '.join(f'{k} {v}' for k, v in sorted(stats.items())))
    print(f'scene merge counts: detail cutouts {len(m.dcut)}', flush=True)


def stage_scene_merge(work, centre, radius, a):
    """The area's scene from its tiles' parts (every tile of tiles.json must have one) and the water, then
    scene_finish (--tiled: stage_scene_tiled)."""
    from collections import Counter
    if getattr(a, 'tiled', False):
        return stage_scene_tiled(work, centre, radius, a)
    name, origin, sig, mats, images = scene_inputs(work, centre, radius, a)
    man = json.load(open(tile_dir(work, a.tiles_tag) / 'tiles.json', encoding='utf-8'))
    folders = [tile_dir(work, a.tiles_tag) / 'scene' / t['id'] for t in man['tiles']]
    lacking = [f.name for f in folders if not (f / 'part.json').exists()]
    if lacking:
        sys.exit('scene merge: no part for tiles %s' % ', '.join(lacking))
    stats = Counter()
    parts = []
    for f in folders:
        P = load_part(f)
        stats.update(P.pop('stats'))
        parts.append(P)
    parts.append(scene_part(work, a, [], origin, sig, mats, images, stats, water_from=(centre, radius)))
    images.flush()
    images.todo.update({p: None for P in parts for p in P['images']})
    scene_finish(work, centre, radius, a, merge_scene_parts(parts), images, stats, sig)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('stage', choices=('sectors', 'blocks', 'scan', 'fetch', 'decimate', 'tiles', 'build', 'scene',
                                      'scene-hide',
                                      'chunks'))
    ap.add_argument('--sector-kinds', default='exterior',
                    help='sectors: the kinds to unbundle, comma-separated (exterior, interior); into the same raw/ and '
                         'json/, so a later scan sees them all')
    ap.add_argument('--scan-workers', type=int, default=4,
                    help='scan: sector processes (each holds one sector\'s json: under 1 GB for Watson\'s largest)')
    ap.add_argument('--budget', type=float, default=1.5e6, help='decimate: render triangle budget')
    ap.add_argument('--floor-tris', type=float, default=12, help='decimate: never below this many triangles')
    ap.add_argument('--keep-ratio', type=float, default=0.8, help='decimate: leave meshes needing less cut')
    ap.add_argument('--min-size', type=float, default=0.6, help='build: drop placements smaller than this (m)')
    ap.add_argument('--flat-height', type=float, default=0.5,
                    help='build: placements under --min-size kept when no taller than this (m)...')
    ap.add_argument('--flat-min', type=float, default=1.5, help='build: ...at least this wide both ways (m)...')
    ap.add_argument('--flat-density', type=float, default=20.0, help='build: ...and at most this many triangles/m2')
    ap.add_argument('--near', type=float, default=12.0, help='build: LOD 0 from this size (m)')
    ap.add_argument('--mid', type=float, default=3.0, help='build: LOD 1 from this size (m), LOD 2 below')
    ap.add_argument('--col-min', type=float, default=1.0, help='build: collision for placements from this size (m)')
    ap.add_argument('--col-walk-lod', action='store_true',
                    help='build: a large placement collides as the coarsest LOD (of at most --col-tris) that keeps LOD '
                         '0\'s walkable surfaces (--col-walk-tol, --col-walk-share), not simply the finest under the cap')
    ap.add_argument('--col-walk-tol', type=float, default=0.3, help='build --col-walk-lod: height tolerance (m)')
    ap.add_argument('--col-walk-share', type=float, default=0.95,
                    help='build --col-walk-lod: share of LOD 0\'s walkable points the LOD must keep')
    ap.add_argument('--col-cutout-large', action='store_true',
                    help='build: a large placement with no opaque material collides as its alpha-tested ones (terrain '
                         'mounds drawn only in cutout materials)')
    ap.add_argument('--col-box', type=float, default=6.0,
                    help='build: below this size (m) a placement collides as its bounding box, above as its mesh')
    ap.add_argument('--col-cell', type=float, default=0.1,
                    help='build: collision mesh vertex grid (m; 0.6 moved floors up to 11 units)')
    ap.add_argument('--col-tris', type=int, default=4000,
                    help='build: a large placement collides as its finest LOD of at most this many triangles')
    ap.add_argument('--col-prop-tris', type=int, default=600,
                    help='build: a prop may collide as its mesh (LOD of at most this many triangles), else its box')
    ap.add_argument('--col-name-tris', type=int, default=400,
                    help='build: a prop named stair/step/ramp collides as its mesh up to this many triangles '
                         '(finest LOD under it), whatever --col-prop-tris says; 0 turns it off')
    ap.add_argument('--col-names', type=int, default=1,
                    help='build: the file-name collision rules (COL_NONE/THIN/HULL_NAMES: litter none, awnings as '
                         'their mesh, pagoda roofs as hulls); 0 = off')
    ap.add_argument('--col-thin-tris', type=int, default=300,
                    help='build: an awning-like prop collides as its coarsest mesh up to this many triangles, else not at all')
    ap.add_argument('--col-prop-fill', type=float, default=0.6,
                    help='build: a prop collides as its mesh when its surface is under this share of its box\'s')
    ap.add_argument('--col-height', type=float, default=16.0,
                    help='build: collision only below this height over --floor (m); a playerclip ceiling caps it')
    ap.add_argument('--texsize', type=int, default=512, help='build: texture and multilayer bake size')
    ap.add_argument('--floor', type=float, default=12.0, help='build: CP height that becomes glTF 0 (m)')
    ap.add_argument('--clip-inset', type=float, default=12.0, help='build: playerclip ring this far inside --radius')
    ap.add_argument('--light-min-radius', type=float, default=2.0)
    ap.add_argument('--light-scale', type=float, default=1.0)
    ap.add_argument('--spawns', type=int, default=16)
    ap.add_argument('--spawn-radius', type=float, default=70.0)
    ap.add_argument('--spawn-sky', type=float, default=30.0,
                    help='metres of open space a spawn needs above it (keeps spawns out of buildings and under roofs)')
    ap.add_argument('--dry-run', action='store_true', help='build: count only')
    ap.add_argument('--no-render', action='store_true',
                    help='build: gameplay layer only (collision, entities, boundary); the scene stage draws the level')
    ap.add_argument('--tag', default='', help='build: output <area>-<tag>.glb (number builds; never overwrite)')
    ap.add_argument('--area', default='kabuki', choices=sorted(AREAS))
    ap.add_argument('--half', type=float, help='box half-size in metres (default: the area preset)')
    ap.add_argument('--radius', type=float, help='kept radius in metres (default: the area preset)')
    ap.add_argument('--work', type=Path, help='work folder (default .codex-research/levelport/cp77/<area>)')
    ap.add_argument('--lods', action='store_true', help='fetch: every LOD, geometry only, into meshes_lod/')
    ap.add_argument('--ents', action='store_true', help='fetch: the placed entity templates (their lights) into ents/')
    ap.add_argument('--proxies', action='store_true',
                    help='fetch: the proxy meshes (placements.json proxies); tiles: the proxies into the tiles too')
    ap.add_argument('--embedded', action='store_true',
                    help='fetch: meshes embedded in the sectors (bended roads), which uncook cannot find')
    ap.add_argument('--embedded-ml', action='store_true',
                    help='fetch: multilayer setups embedded in the placed meshes, which export leaves inside the .mesh')
    ap.add_argument('--map', help='scene: map name (default cp_<area>), as the gameplay BSP is named')
    ap.add_argument('--tile-size', type=float, default=256.0, help='tiles: square tiles of this side (m)')
    ap.add_argument('--tiles-tag', default='t1', help='tiles, scene --tile/--merge: the tiling (work/tiles/<tag>/)')
    ap.add_argument('--tile', help='scene: build this tile\'s part only (an id from tiles.json, e.g. 0_-1)')
    ap.add_argument('--clutter-drops', type=Path,
                    help='chunks: colclutter.py drops folder (<tile>.json): those boxes get no collision (road clutter)')
    ap.add_argument('--inst-split', type=Path,
                    help='build --tile, chunks: the instanced split (look/collision-20261030/inst/STEP0-FORMATS.md) '
                         'into this folder: parts/, then chunks/, inst/, protos/, col_counts.json')
    ap.add_argument('--inst-tiles', default='',
                    help='chunks --inst-split: these tiles (comma-separated ids; default all); rule 6 counts all')
    ap.add_argument('--inst-base', action='store_true',
                    help='chunks --inst-split: also chunks_base/, the same tiles banded with nothing instanced')
    ap.add_argument('--inst-owngrid', action='store_true',
                    help='chunks --inst-split: only chunks_base_owngrid/, the split with each instance\'s prototype '
                         'placed back as world triangles (M-2\'s engine check); chunks/, inst/ and protos/ untouched')
    ap.add_argument('--base-only', action='store_true',
                    help='build --merge: only what a build holds besides its collision chunks (ring, lid, lights, '
                         'spawns) -> tiles/<tag>/chunks/base.glb')
    ap.add_argument('--merge', action='store_true', help='scene: the area from its tiles\' parts')
    ap.add_argument('--scene-tiles', help='scene --merge --tiled: a file of tile ids, one a line: these tiles\' parts only')
    ap.add_argument('--tiled', action='store_true',
                    help='scene --merge: a tiled scene (docs/levelport-scene.md: shards, tile files, a manifest) for an '
                         'area too large for one .xscn')
    ap.add_argument('--scene-min', type=float, default=0.0, help='scene: drop placements smaller than this (m)')
    ap.add_argument('--bake-workers', type=int, default=12,
                    help='scene: processes baking the multilayered materials before the build (1: in the build)')
    ap.add_argument('--detail-size', type=int, default=512,
                    help='scene: multilayered materials leave their main layer to the renderer, tiled at the source\'s '
                         'rate (its detail image at most this size; 0: bake every layer, as before)')
    ap.add_argument('--detail-slots', type=int, default=8,
                    help='scene: layer groups the renderer tiles per multilayered material (1: the main layer, .xdet '
                         'v1; 2-8: .xdet v2 with share maps). 8 is the default: with 1 the other layers bake into one 512 px image and alias into waves')
    ap.add_argument('--ocean-mask', type=Path,
                    help='scene --tiled: the sea mask (.npz in CP2077 metres: mask, x0, y0, cell) for .xwat v3; '
                         'without it v2 (the ocean plane, no mask)')
    ap.add_argument('--screen-sheets', action='store_true',
                    help='scene: television_ad screens carry their whole flipbook sheet (.xscr gives its tiles and '
                         'frames per second) for a renderer that plays them; off: the first frame, as before')
    ap.add_argument('--scene-texsize', type=int, default=1024,
                    help='scene: single-texture cap; multilayer bakes stay at --texsize')
    ap.add_argument('--out', type=Path, help='scene: output folder (default build/<map>_scene-<tag>)')
    ap.add_argument('--repack', type=Path, help='scene: rewrite this older scene pk3 with distinct DDS images')
    ap.add_argument('--relight', type=Path, help='scene: rewrite only the light table of this scene pk3 (after a scan)')
    ap.add_argument('--signals', default=None,
                    help='scene: the traffic signals\' groups and programs (default <work>/signal_groups.json; none: '
                         'signal heads stay dark)')
    ap.add_argument('--signal-sounds', default=None,
                    help='scene: the game folder of the crossing sounds the .signals file names (walk_base, '
                         'walk_layer, dontwalk_base, dontwalk_layer .wav, e.g. sound/<map>/signals; default none: '
                         'the crossings are silent)')
    ap.add_argument('--grade', default=GRADE_LUT,
                    help='scene: the game\'s colour-grade LUT (a depot path) as maps/<map>.xlut; empty for none')
    ap.add_argument('--regrade', type=Path, help='scene: rewrite only the colour grade of this scene pk3')
    ap.add_argument('--vcache', type=Path, help='scene: rewrite only the triangle and vertex order of this scene pk3 '
                                                '(tipsify; on Kabuki the source order is better: README "Vertex order")')
    a = ap.parse_args(argv)
    work = a.work or work_dir(a.area)
    box = area_box(a.area, a.half)
    centre, radius = AREAS[a.area]['centre'], a.radius or AREAS[a.area]['radius']
    use_area(a.area)
    if a.stage == 'fetch' and a.ents:
        stage_fetch_ents(work, centre, radius)
    elif a.stage == 'fetch' and a.embedded_ml:
        stage_fetch_embedded_ml(work)
    elif a.stage == 'fetch' and a.embedded:
        stage_fetch_embedded(work, centre, radius)
    elif a.stage == 'fetch':
        stage_fetch(work, centre, radius, a.lods, proxies=a.proxies)
    elif a.stage == 'decimate':
        stage_decimate(work, centre, radius, a)
    elif a.stage == 'build':
        stage_build(work, centre, radius, a)
    elif a.stage == 'scene' and a.repack:
        out = a.out or lpaths.LEVELPORT / 'build' / ('%s_scene-%s' % (a.map or 'cp_' + a.area, a.tag))
        out.mkdir(parents=True, exist_ok=True)
        scene_repack(a.repack, not_source(a.repack, out / Path(a.repack).name), a.map or 'cp_' + a.area)
    elif a.stage == 'scene' and a.relight:
        out = a.out or lpaths.LEVELPORT / 'build' / ('%s_scene-%s' % (a.map or 'cp_' + a.area, a.tag))
        out.mkdir(parents=True, exist_ok=True)
        scene_relight(work, centre, radius, a.relight, not_source(a.relight, out / Path(a.relight).name), a)
    elif a.stage == 'scene' and a.vcache:
        out = a.out or Path(a.vcache).parent.with_name(Path(a.vcache).parent.name + '-vc')
        scene_vcache(a.vcache, out / Path(a.vcache).name, a.map or 'cp_' + a.area)
    elif a.stage == 'scene' and a.regrade:
        out = a.out or lpaths.LEVELPORT / 'build' / ('%s_scene-%s' % (a.map or 'cp_' + a.area, a.tag))
        out.mkdir(parents=True, exist_ok=True)
        scene_regrade(work, a.regrade, not_source(a.regrade, out / Path(a.regrade).name), a.map or 'cp_' + a.area, a.grade)
    elif a.stage == 'chunks':
        stage_chunks(work, centre, radius, a)
    elif a.stage == 'tiles':
        stage_tiles(work, centre, radius, a)
    elif a.stage == 'scene-hide':
        stage_scene_hide(work, centre, radius, a)
    elif a.stage == 'scene' and a.tile:
        stage_scene_tile(work, centre, radius, a, a.tile)
    elif a.stage == 'scene' and a.merge:
        stage_scene_merge(work, centre, radius, a)
    elif a.stage == 'scene':
        stage_scene(work, centre, radius, a)
    else:
        {'sectors': lambda w, b: stage_sectors(w, b, tuple(a.sector_kinds.split(',')),
                                               None if a.half else AREAS[a.area].get('sector_boxes')),
         'scan': lambda w, b: stage_scan(w, b, a.scan_workers),
         'blocks': lambda w, b: stage_blocks(w)}[a.stage](work, box)


if __name__ == '__main__':
    import jobguard
    jobguard.kill_children_with_me()      # (its pools, WolvenKit and Blender end with it)
    main()
