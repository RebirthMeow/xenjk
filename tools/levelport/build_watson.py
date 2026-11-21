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
"""build_watson.py -- the whole Watson kit from the CP2077 extraction, every stage a committed tool (the baseline's
driver, docs/BASELINE.md, phase 2). Nothing is built in place: a tiles
tag, scene, decals, fx, env and collision build of their own per run, and a new kit folder.

  python -I tools/levelport/build_watson.py <tag> --data <inputs.json> [--from STAGE] [--until STAGE] [--only STAGE]
                                            [--bin <x64 Release folder>] [--dry-run]

Stages, in order (each logs to <work>/tiles/<tag>/driver/<stage>.log and leaves <stage>.done; a rerun skips what is
done, so an interrupted run resumes; --from reruns from a stage on):
  extract    cp2gltf sectors (exterior and interior), scan, the fetches (the entities first, then meshes, LODs, proxies
             and their LODs, embedded meshes and multilayer setups), the device graph and light_time.py, the moon;
             only with --extract. CP2077 2.5 or later is read through WolvenKit 10 (cp77_sector25 rewrites its
             sectors in the layout the readers know); a fresh extraction goes in a research root of its own
             (XENJK_RESEARCH), as the reference kits' 2.31 extraction cannot be re-made from a patched game.
             Without --extract the stage keeps the work folder's.
  tiles      cp2gltf tiles: the area's placements in 256 m squares (tiles/<tag>)
  indexes    cp77_ads index, cp77_rotate index, cp77_foliage (tiles/<tag>/foliage)
  parts      cp77_tiles --parts-only: every tile's collision part, then its scene part (keyed: a part is reused
             only when the code, arguments and placements that made it are unchanged). The collision parts come
             first because they decide which doors open, and a scene part leaves those doors' leaves out (the
             door rule); --scene-only would draw every door shut
  scene      the tiled scene merge, 8 detail slots, the ocean plane and sea mask (build/<map>_scene-<tag>)
  interiors  tag_interiors: CP2077's interior streaming boxes (build/<map>_scene-<tag>-int)
  collision  the collision chunks and the hub (the collision entry point named in the inputs)
  clutter    colclutter.py scan, drops, patch and verify: the road clutter CP2077 pushes aside or treats as debris
             loses collision -> build/<map>_collision-<tag>c; "clutter": false skips it
  spawns     the inputs' spawns (default areas/<area>_spawns.json) put into the compiled hub (cp77_collision.
             put_spawns; every other lump and member as compiled) -> build/<map>_spawns-<tag>
  decals     cp77decals fetch and build (build/<map>_decals-<n>)
  fx         cp77fx fetch and build (build/<map>_fx-<tag>)
  env        the env, dawn, day and moon packs from the inputs' files (build/<map>_env-<tag>)
  kit        build/<map>_baseline-<tag>: launchers, settings, the x64 binaries, every pack hard-linked: the
             chunk packs from the clutter build when there is one, the hub from the spawns build when there is one,
             the inputs' extra packs (the report packs a kit carries until their converters are in), the inputs'
             renderer over --bin's, the inputs' cfg lines after the template's
  checks     scene_parity against the inputs' reference kit ("parity_tiles": a list or "all"); the chunk packs'
             members against the inputs' reference collision build by CRC and size (logs/chunk_crc.json)

The inputs file (local data, never committed: CP2077-derived and hand-made inputs, each with its producer named;
areas/night_city_b/inputs.example.json is a template with the required keys):
  {"area": "night_city_b", "map": "cp_watson",
   "ocean_mask": ".../look/ocean-tag10-20261104/sea_plane_mask20_final.npz",
   "ads_dir": ".../look/ads-20261105", "decals_root": ".../look/decals-<tag>", "fx_root": ".../look/fx-<tag>",
   "env": {"maps/cp_watson.xenv": "...", "maps/cp_watson_dawn.xenv": "...", "maps/cp_watson_day.xenv": "..."},
   "moon": ".../moon.png", "game_data": ".../GameData", "cfg": "<the kit's openjk.cfg template>",
   "sdl2": "<SDL2.dll for bin64>", "wolvenkit_cli": "<WolvenKit.CLI.exe: the build the decal and fx fetches used>",
   "collision": ["python", "-I", "tools/levelport/<collision entry point>", "{work}", "{tag}", "{out}"],
   "reference_kit": ".../build/cp_watson_dev-night-29/home/base",
   "reference_materials": ".../build/cp_watson_scene-10j/materials.json",
   "parity_tiles": ["-7_4", "-5_8", "-3_3", "-6_0", "-6_11"] or "all",
   optional: "clutter": {"max_height": 1.0, "keep": "", "jobs": 6} or false, "spawns": "<spawns json>",
   "extra_packs": ["<pk3>", ...], "replace_packs": {"<pack name>": "<pk3>"}, "decals_build": "<folder>",
   "renderer": "<rd-vulkan_x86_64.dll>", "cfg_set": ["seta <cvar> <value>", ...],
   "reference_collision": "<a collision build folder>",
   "from_tag": "<an earlier tiles tag: its tiling and every part whose key holds are linked, not made again>",
   "parts_reference": "<a tiles tag>": right after the parts stage its collision parts are compared with this tag's,
     array by array, and the run stops on a difference ("parts_gate": "report" only reports),
   "reference_hub": "<hub pk3>", "reference_spawns_hub": "<hub pk3>", "compare_packs": ["<pack name>", ...],
   "engine": {"bin": "<folder with openjkded.x86_64.exe>", "game": "<jampgamex86_64.dll>", "corpora": "<folder of
     corpus folders, each corpus.txt, traced/levelcheck_out.txt and traced/run/home/base/bench.cfg>"},
   "parity_materials_b_only": true (parity's materials: only the keys the tag holds, for a tag of a few tiles),
   "scene_stats": "<scene_stats.py>", "log_counts": {"<name>": ["<stage>", "<regex with one number group>"]},
   "expect": "<expectations json for tag_verdict.py judge>"}
"env", "cfg" and "moon" may be left out: the env files and the settings template are the repository's
(tools/levelport/areas/<area>/env/*.xenv, kit-openjk.cfg), the moon the extraction's (--extract fetches it into the
work folder). A relative path in the inputs is read from the repository's root. A file an input names is keyed by its
contents.
Converted levels are for local play only."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import paths  # noqa: E402

LP = paths.LEVELPORT
PY = sys.executable
STAGES = ['extract', 'tiles', 'indexes', 'parts', 'clutter', 'collision', 'clutter_patch', 'spawns', 'scene',
          'interiors', 'decals', 'fx', 'env', 'kit', 'checks']
# after the parts, two lanes at once: the collision side and the scene side, on one memgate ledger
PREFIX = ['extract', 'tiles', 'indexes', 'parts', 'clutter']
LANES = (['collision', 'clutter_patch', 'spawns'], ['scene', 'interiors', 'decals', 'fx', 'env'])
SUFFIX = ['kit', 'checks']
# what a stage's .done key holds besides its own run's arguments: the stages it reads (their run ids), the tools it
# runs (each one's module closure), the inputs-file keys it reads
DEPS = {'extract': [], 'tiles': ['extract'], 'indexes': ['tiles'], 'parts': ['tiles', 'indexes'], 'clutter': ['parts'],
        'collision': ['parts', 'clutter'], 'clutter_patch': ['collision', 'clutter'], 'spawns': ['collision'],
        'scene': ['parts'], 'interiors': ['scene'], 'decals': ['extract'], 'fx': ['extract'], 'env': [],
        'kit': ['interiors', 'collision', 'clutter_patch', 'spawns', 'decals', 'fx', 'env'], 'checks': ['kit']}
TOOLS = {'extract': ['cp2gltf.py'], 'tiles': ['cp2gltf.py'], 'indexes': ['cp77_ads.py', 'cp77_rotate.py',
         'cp77_foliage.py'], 'parts': ['cp77_tiles.py'], 'clutter': ['colclutter.py'],
         'collision': ['cp77_collision.py'], 'clutter_patch': ['colclutter.py'], 'spawns': ['build_watson.py'],
         'scene': ['cp2gltf.py'], 'interiors': ['tag_interiors.py'], 'decals': ['cp77decals.py'], 'fx': ['cp77fx.py'],
         'env': [], 'kit': ['build_watson.py'], 'checks': ['scene_parity.py', 'build_watson.py', 'tag_verdict.py',
         'engine_trace_ded.py']}
# stages keyed on Run methods of their own (each method's syntax tree, docstring dropped) in place of a tool's closure
METHODS = {'env': ['env']}
# inputs that name a file: keyed by its contents (Run.input_value); and those read from the repository's root when
# relative (Run.defaults)
FILE_INPUTS = ('env', 'moon', 'cfg', 'spawns', 'ocean_mask', 'sdl2')
REPO_PATHS = ('cfg', 'moon', 'spawns', 'ocean_mask', 'ads_dir', 'expect', 'scene_stats', 'sdl2')
INPUTS = {'tiles': ['from_tag'], 'indexes': ['ads_dir'], 'parts': ['ads_dir', 'from_tag', 'parts_reference', 'parts_gate'], 'clutter': ['clutter'], 'collision': ['collision', 'clutter'],
          'clutter_patch': ['clutter'], 'spawns': ['spawns'], 'scene': ['ocean_mask', 'ads_dir'],
          'decals': ['decals_root'], 'fx': ['fx_root'], 'env': ['env', 'moon'],
          'kit': ['sdl2', 'cfg', 'game_data', 'extra_packs', 'replace_packs', 'renderer', 'cfg_set', 'decals_build'],
          'checks': ['reference_kit', 'reference_materials', 'reference_collision', 'parity_tiles', 'reference_hub',
                     'reference_spawns_hub', 'compare_packs', 'engine', 'expect', 'log_counts', 'scene_stats',
                     'parity_materials_b_only']}
# a lane-B command's memory reservation while the collision lane runs beside it (GB; the scene merge read 24 GB of
# parts in tag 11, its peak not logged: taken high)
RESERVE = {'scene': 12.0, 'interiors': 6.0, 'decals': 4.0, 'fx': 3.0, 'env': 0.5}
# a stage starts only with its own size plus FLOOR_GB free on each drive it writes (a fixed 25 GB of both drives once stopped a run for
# D:, which the stage never writes); the preflight sums the
# run's stages per drive before the first one starts, so a run that cannot fit stops in its first minute
FLOOR_GB = 15
# what each stage writes, in GB: the tile folders, the parts deflated, the
# collision as the cooked XBSZ build (5.5 GB of packs beside 7.5 GB of chunk glbs)
STAGE_GB = {'extract': 45, 'tiles': 2.5, 'indexes': 0.5, 'parts': 12, 'scene': 7.6, 'interiors': 4, 'collision': 14,
            'clutter': 0.5, 'clutter_patch': 6, 'spawns': 0.05, 'decals': 0.5, 'fx': 0.1, 'env': 0.05, 'kit': 0.05,
            'checks': 0.1}
BIN_FILES = ('xenjk.x86_64.exe', 'rd-vulkan_x86_64.dll', 'cgamex86_64.dll', 'jampgamex86_64.dll', 'uix86_64.dll')
LAUNCHERS = {       # name -> extra arguments (the kit's three ways in)
    'Play_Watson_Chunks.bat': '',
    'Play_Watson_Dawn.bat': ' +set r_sceneEnv maps/cp_watson_dawn.xenv',
    'Play_Watson_Day.bat': ' +set r_sceneEnv maps/cp_watson_day.xenv',
}


def tool(name):
    return str(HERE / name)


def method_sha(name):
    """a Run method's syntax tree (no positions, its docstring dropped): what a stage keyed in METHODS runs"""
    import ast
    import hashlib
    tree = ast.parse((HERE / 'build_watson.py').read_text(encoding='utf-8'))
    run = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Run')
    fn = next(n for n in run.body if isinstance(n, ast.FunctionDef) and n.name == name)
    if fn.body and isinstance(fn.body[0], ast.Expr) and isinstance(fn.body[0].value, ast.Constant):
        fn.body = fn.body[1:] or [ast.Pass()]
    return hashlib.sha256(ast.dump(fn).encode()).hexdigest()


class Run:
    def __init__(self, a, inp):
        import cp2gltf
        self.a, self.inp = a, inp
        self.area, self.map, self.tag = inp.get('area', 'night_city_b'), inp.get('map', 'cp_watson'), a.tag
        self.work = cp2gltf.work_dir(self.area)
        self.tiles = cp2gltf.tile_dir(self.work, self.tag)
        self.logs = self.tiles / 'driver'
        self.build = LP / 'build'
        self.defaults()

    def defaults(self):
        """the inputs a run may leave out, from the repository (tools/levelport/areas/<area>/: env/*.xenv, the
        night, dawn and day files; kit-openjk.cfg, the kit's settings template) and the extraction (the moon, which
        --extract fetches); a relative path in the inputs is read from the repository's root"""
        inp, own = self.inp, HERE / 'areas' / self.area
        for k in REPO_PATHS:
            if isinstance(inp.get(k), str) and not Path(inp[k]).is_absolute():
                inp[k] = str(REPO / inp[k])
        if isinstance(inp.get('env'), dict):
            inp['env'] = {arc: str(REPO / src) if not Path(src).is_absolute() else src
                          for arc, src in inp['env'].items()}
        elif not inp.get('env') and (own / 'env').is_dir():
            inp['env'] = {'maps/' + f.name: str(f) for f in sorted((own / 'env').glob('*.xenv'))}
        if not inp.get('cfg') and (own / 'kit-openjk.cfg').exists():
            inp['cfg'] = str(own / 'kit-openjk.cfg')
        if not inp.get('moon') and self.moon_file().exists():
            inp['moon'] = str(self.moon_file())

    def moon_file(self):
        """the moon image as the extraction's fetch leaves it (CP2077's engine/textures/sky/moon.xbm, uncooked)"""
        return self.work / 'moon' / 'engine' / 'textures' / 'sky' / 'moon.png'

    def input_value(self, k):
        """an input as a stage key holds it: a file named by the inputs by its contents, not its path (the same
        file in another checkout keys the same; an edit to it moves the key)"""
        v = self.inp.get(k)
        if k not in FILE_INPUTS or not v:
            return v
        if isinstance(v, dict):
            return {a: file_sha(f) if Path(f).is_file() else f for a, f in sorted(v.items())}
        return file_sha(v) if Path(v).is_file() else v

    def writes(self, stage):
        """the folders a stage writes (its log folder too)"""
        w = [self.logs]
        if stage in ('extract', 'tiles', 'indexes', 'parts', 'collision', 'clutter'):
            w.append(self.work)
        if stage in ('scene', 'interiors', 'collision', 'clutter_patch', 'spawns', 'decals', 'fx', 'env', 'kit',
                     'checks'):
            w.append(self.build)
        if stage == 'decals' and self.inp.get('decals_root'):
            w.append(Path(self.inp['decals_root']))
        if stage == 'fx' and self.inp.get('fx_root'):
            w.append(Path(self.inp['fx_root']))
        return w

    def drives(self, stage):
        """the drives a stage writes, junctions resolved (.codex-research/levelport is C:'s level port)"""
        out = set()
        for p in self.writes(stage):
            while not p.exists() and p.parent != p:     # (a folder still to be made: the drive of its nearest parent)
                p = p.parent
            out.add(os.path.splitdrive(os.path.realpath(p))[0].upper() + '/')
        return sorted(out)

    def free_ok(self, label):
        """the stage's size plus FLOOR_GB on each drive it writes, else a stop now with the shortfall"""
        need = STAGE_GB.get(label, 1) + FLOOR_GB
        for d in self.drives(label):
            free = shutil.disk_usage(d).free / 2 ** 30
            if free < need:
                sys.exit('build_watson: %s has %.1f GB free before %s, which needs %.1f GB there (%.1f to write, %d '
                         'kept free): %.1f GB short' % (d, free, label, need, STAGE_GB.get(label, 1), FLOOR_GB,
                                                         need - free))

    def marker(self, stage):
        """a stage's .done marker: {made, minutes, key, run} (an old plain-text one: {'legacy'})"""
        f = self.logs / ('%s.done' % stage)
        if not f.exists():
            return None
        t = f.read_text(encoding='utf-8', errors='replace')
        try:
            return json.loads(t)
        except ValueError:      # (a JSON marker cut short is no marker: the stage runs again)
            return None if t.lstrip().startswith('{') else {'legacy': t.strip()}

    def stage_key(self, stage, runs):
        """the stage's key: its tools' code (each one's module closure), the inputs-file values it reads, the run's
        arguments that change its output, and the run ids of the stages it reads (a .done once held only a date)"""
        import hashlib
        h = hashlib.sha256(stage.encode())
        for t in TOOLS.get(stage, []):
            for f in self.closure(t):
                h.update(('%s %s\n' % (f.name, file_sha(f))).encode())
        for m in METHODS.get(stage, []):         # (a stage keyed on Run methods: an edit to one reruns it)
            h.update(('code Run.%s %s\n' % (m, method_sha(m))).encode())
        h.update(json.dumps({k: self.input_value(k) for k in INPUTS.get(stage, [])}, sort_keys=True, default=str).encode())
        h.update(json.dumps({'tag': self.tag, 'area': self.area, 'map': self.map, 'extract': self.a.extract,
                             'kit_name': self.a.kit_name if stage in ('kit', 'checks') else None,
                             'bin': self.a.bin if stage == 'kit' else None}).encode())
        for d in DEPS.get(stage, []):
            h.update(('%s %s\n' % (d, runs.get(d))).encode())
        return h.hexdigest()[:24]

    def key_parts(self, stage, runs):
        """stage_key's parts by name (for the marker, and to say which one moved)"""
        parts = {'tool ' + f.name: file_sha(f) for t in TOOLS.get(stage, []) for f in self.closure(t)}
        parts.update({'code Run.' + m: method_sha(m) for m in METHODS.get(stage, [])})
        parts.update({'input ' + k: json.dumps(self.input_value(k), sort_keys=True, default=str)
                      for k in INPUTS.get(stage, [])})
        parts.update({'stage ' + d: str(runs.get(d)) for d in DEPS.get(stage, [])})
        return parts

    def moved(self, stage):
        """what of a stage's key moved since its marker: its tools' files, its inputs, the stages it reads"""
        m = self.marker(stage) or {}
        old = m.get('parts')
        if not old:
            return 'its marker holds no parts (an older marker)'
        runs = {x: (self.marker(x) or {}).get('run') or (self.marker(x) or {}).get('legacy') for x in STAGES}
        new = self.key_parts(stage, runs)
        diff = sorted(k for k in set(old) | set(new) if old.get(k) != new.get(k))
        return ', '.join(diff[:6]) + (' ...' if len(diff) > 6 else '') if diff else 'nothing it names'

    def closure(self, name):
        if not hasattr(self, '_closures'):
            self._closures = {}
        if name not in self._closures:
            import cp77_collision
            self._closures[name] = cp77_collision.module_closure(HERE / name)
        return self._closures[name]

    def plan(self, stages):
        """the stages this run starts: a marker missing, its key changed (a tool, an input or a stage it reads), a
        stage it reads in this run, or forced by --only / --from (whose stage then gets a new run id, so every stage
        after it that reads it runs too: --from parts once skipped every later stage)"""
        runs, todo = {}, []
        for st in STAGES:
            m = self.marker(st)
            runs[st] = (m or {}).get('run') or (m or {}).get('legacy')
        for st in stages:
            m = self.marker(st)
            forced = self.a.only == st or self.a.start == st
            upstream = any(d in todo for d in DEPS.get(st, []))
            if forced or upstream or m is None:
                todo.append(st)
            elif 'legacy' not in m and m.get('key') != self.stage_key(st, runs):
                todo.append(st)
                print('build_watson: %s planned again: %s moved' % (st, self.moved(st)), flush=True)
        return todo

    def runs(self, stage):
        """whether main() runs this stage (see plan)"""
        if not hasattr(self, '_plan'):
            self._plan = self.plan(self.stages)
        return stage in self._plan

    def preflight(self, stages):
        """every problem the run would meet that can be seen now, as one list (a kit
        stage once failed on --bin nine hours in)"""
        self.stages = stages
        todo = [st for st in stages if self.runs(st) and not (st == 'extract' and not self.a.extract)]
        bad, inp = [], self.inp

        def path(key, what, need=True):
            v = inp.get(key)
            if not v:
                if need:
                    bad.append('inputs: no "%s" (%s)' % (key, what))
            elif not Path(v).exists():
                bad.append('inputs: "%s" %s does not exist (%s)' % (key, v, what))
        if any(st in todo for st in ('indexes', 'parts', 'scene')):
            path('ads_dir', 'the ads folder', need=False)
        if 'scene' in todo:
            path('ocean_mask', 'the sea mask', need=False)
        if 'decals' in todo:
            path('decals_root', 'the decal fetch root')
        if 'fx' in todo:
            path('fx_root', 'the fx fetch root')
        if any(st in todo for st in ('decals', 'fx', 'extract')):
            path('wolvenkit_cli', 'the decal and fx fetches run WolvenKit')
            import psutil
            if any((p.info['name'] or '').lower() == 'cyberpunk2077.exe' for p in psutil.process_iter(['name'])):
                bad.append('CP2077 is running: the WolvenKit fetches (decals, fx, extract) read its archives')
        if 'env' in todo:
            for arc, src in sorted((inp.get('env') or {}).items()):
                if not Path(src).exists():
                    bad.append('inputs: env %s: %s does not exist' % (arc, src))
            if not inp.get('env'):
                bad.append('inputs: no "env" (the night, dawn and day .xenv files)')
            path('moon', 'the moon image', need=False)
        if 'kit' in todo:
            path('sdl2', 'SDL2.dll for bin64')
            path('cfg', "the kit's openjk.cfg")
            path('game_data', 'the launchers\' fs_basepath')
            if not self.a.bin:
                bad.append('--bin: not given (the kit stage takes the x64 engine, renderer and game modules from it)')
            else:
                miss = [f for f in BIN_FILES if not (Path(self.a.bin) / f).exists()]
                if miss:
                    bad.append('--bin %s: no %s' % (self.a.bin, ', '.join(miss)))
        if 'checks' in todo:
            path('reference_kit', 'scene_parity\'s reference')
            path('reference_materials', 'scene_parity\'s reference materials')
            path('reference_collision', 'the chunk CRC check\'s reference build', need=False)
        if 'spawns' in todo and not self.spawns_file().exists():
            bad.append('spawns: %s does not exist' % self.spawns_file())
        if 'kit' in todo:
            for pk in inp.get('extra_packs', []) + list(inp.get('replace_packs', {}).values()):
                if not Path(pk).exists():
                    bad.append('inputs: extra_packs/replace_packs: %s does not exist' % pk)
            if 'decals' not in todo and self.decals_dir() is None:
                bad.append('kit: no decals build recorded for this tag and no "decals_build" in the inputs')
            path('renderer', 'the renderer over --bin\'s', need=False)
        if 'collision' in todo:
            cmd = inp.get('collision') or []
            if not cmd:
                bad.append('inputs: no "collision" entry point')
            for i, c in enumerate(cmd):
                if c.lower().endswith(('.exe', '.py')) and '{' not in c and not (
                        Path(c).exists() or (REPO / c).exists()):
                    bad.append('inputs: collision: %s does not exist' % c)
                if c == '--q3map2-chunks' and i + 1 < len(cmd) and Path(cmd[i + 1]).exists() and not (
                        {'--plain', '--text'} & set(cmd)):
                    if b'brushbin' not in Path(cmd[i + 1]).read_bytes():
                        bad.append('inputs: collision: --q3map2-chunks %s has no -brushbin, which the cooked chunks '
                                   '(cp77_collision\'s default) need: the xenjk9 build' % cmd[i + 1])
            import toolchain        # (the q3map2 builds the inputs name against the reference build's)
            for flag, name in (('--q3map2', 'Q3MAP2_HUB'), ('--q3map2-chunks', 'Q3MAP2_CHUNKS')):
                if flag in cmd and cmd.index(flag) + 1 < len(cmd) and Path(cmd[cmd.index(flag) + 1]).exists():
                    q, pin = cmd[cmd.index(flag) + 1], toolchain.pin_of(name)
                    if pin and toolchain.sha256(q) != pin:
                        print('preflight: collision %s %s is not the reference build (toolchain.json): the chunks '
                              'may differ from the reference collision' % (flag, q), flush=True)
        # the outside tools every run uses (toolchain.py): one missing stops the run now; another build is reported
        import toolchain
        problems, diffs = toolchain.check(['python', 'numpy', 'Pillow', 'psutil'] + (
            ['XENJK_LIBZSTD'] if 'collision' in todo else []), quiet=True)
        bad += ['toolchain: ' + p for p in problems]
        for d in diffs:
            print('preflight: toolchain: %s' % d, flush=True)
        # outputs that a stage to run would find made already (each exits on an existing folder, hours in)
        b = self.build
        outs = {'scene': b / ('%s_scene-%s' % (self.map, self.tag)), 'interiors': b / ('%s_scene-%s-int' % (
            self.map, self.tag)), 'collision': b / ('%s_collision-%s' % (self.map, self.tag)),
            'fx': b / ('%s_fx-%s' % (self.map, self.tag)), 'env': b / ('%s_env-%s' % (self.map, self.tag)),
            'clutter_patch': self.clutter_dir(), 'spawns': self.spawns_dir(), 'kit': self.kit_dir()}
        for st, o in outs.items():
            if st in todo and o.exists():
                nxt = [x for x in STAGES[STAGES.index(st) + 1:] if x in self.stages]
                bad.append('%s: %s exists already%s. Resume with --from %s (it keeps this output), or set it aside '
                           'to build it again' % (st, o, (' (planned again: %s)' % self.moved(st)) if self.marker(st)
                                                  else '', nxt[0] if nxt else st))
        # a stopped attempt's leftovers of this tag (a stage would meet them hours in)
        for p in sorted(self.build.glob('%s_*-%s*' % (self.map, self.tag))):
            if p.suffix in ('.partial', '.tmp') or p.name.endswith(('.partial', '.tmp')):
                bad.append('%s is left from a stopped attempt: look at it, then set it aside' % p)
        if 'kit' in todo and self.a.bin:
            import subprocess as sp
            newest = sp.run(['git', 'log', '-1', '--format=%ct', '--', 'codemp'], cwd=REPO, capture_output=True,
                            text=True).stdout.strip()
            old = [f for f in BIN_FILES if (Path(self.a.bin) / f).exists() and newest and
                   (Path(self.a.bin) / f).stat().st_mtime < int(newest) - 60]
            if old:
                print('preflight: --bin\'s %s older than this tree\'s newest codemp commit (%s): is it this tree\'s '
                      'build?' % (', '.join(old), time.strftime('%Y-%m-%d %H:%M', time.localtime(int(newest)))),
                      flush=True)
        eng = inp.get('engine')
        if 'checks' in todo and eng:
            for k, want in (('bin', 'openjkded.x86_64.exe'), ('game', None), ('corpora', None)):
                q = Path(eng.get(k, '')) / want if want else Path(eng.get(k, ''))
                if not eng.get(k) or not q.exists():
                    bad.append('inputs: engine.%s: %s does not exist (the engine checks)' % (k, q))
        if paths.GAMEDATA.is_dir() and 'collision' in todo:
            bad.append('collision: run from a worktree (this checkout has game_directory/, which the chunk cache\'s '
                       'compile identity and the hub key would hash: every chunk misses and the hub is rebuilt)')
        # the disk the run will write, per drive
        need = {}
        for st in todo:
            for d in self.drives(st):
                need[d] = need.get(d, 0) + STAGE_GB.get(st, 1)
        for d, gb in sorted(need.items()):
            free = shutil.disk_usage(d).free / 2 ** 30
            print('preflight: %s: the run writes about %.0f GB, %.0f GB free (%d kept free)' % (d, gb, free, FLOOR_GB),
                  flush=True)
            if free < gb + FLOOR_GB:
                bad.append('%s: about %.0f GB to write and %d to keep free, %.0f GB free: %.0f GB short' % (
                    d, gb, FLOOR_GB, free, gb + FLOOR_GB - free))
        import psutil
        ram = psutil.virtual_memory()
        print('preflight: RAM %.0f GB, %.0f GB available (the largest scene parts peak at 7-11 GB each)' % (
            ram.total / 2 ** 30, ram.available / 2 ** 30), flush=True)
        if 'parts' in todo:
            print('preflight: parts: every tile\'s collision part first, then its scene part on the collision\'s '
                  'doors (cp77_tiles --parts-only, %d workers); no --scene-only' % self.a.workers, flush=True)
        print('preflight: stages to run: %s' % (', '.join(todo) or 'none'), flush=True)
        if bad:
            sys.exit('build_watson: preflight: %d problem%s:\n  %s' % (len(bad), 's' if len(bad) > 1 else '',
                                                                      '\n  '.join(bad)))
        print('preflight: ok', flush=True)

    def sh(self, stage, cmd, env=None):
        """one command of a stage, its output appended to the stage's log; a failure stops the run. Its start, end,
        exit, peak private bytes (its process tree) and the free space before and after go to logs/timings.json;
        while the lanes run, a scene-side command waits for its memory reservation (RESERVE) on the shared gate"""
        import memgate
        cmd = [str(c) for c in cmd]
        log = self.logs / ('%s.log' % stage)
        line = '== %s %s' % (' '.join(cmd), time.strftime('%H:%M:%S'))
        print(line, flush=True)
        if self.a.dry_run:
            return
        self.logs.mkdir(parents=True, exist_ok=True)
        self.free_ok(stage)
        label = '%s %d' % (stage, id(cmd))
        gate = getattr(self, 'gate', None) if stage in RESERVE else None
        if gate is not None:
            gate.wait_start(label, RESERVE[stage])
        free0 = {d: round(shutil.disk_usage(d).free / 2 ** 30, 1) for d in self.drives(stage)}
        t0, peak = time.time(), 0.0
        try:
            with open(log, 'a', encoding='utf-8') as f:
                f.write(line + '\n')
                f.flush()
                p = subprocess.Popen(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, **(env or {})))
                if gate is not None:
                    gate.bind(label, p.pid)
                while p.poll() is None:
                    peak = max(peak, memgate.tree_private_gb(p.pid))
                    time.sleep(1.0)
                f.write('exit=%d %s\n' % (p.returncode, time.strftime('%H:%M:%S')))
        finally:
            if gate is not None:
                gate.release(label)
        self.timing({'stage': stage, 'cmd': ' '.join(cmd)[:300], 'start': time.strftime('%Y-%m-%d %H:%M:%S',
                     time.localtime(t0)), 'seconds': round(time.time() - t0, 1), 'exit': p.returncode,
                     'peak_private_gb': round(peak, 2), 'free_gb_before': free0,
                     'free_gb_after': {d: round(shutil.disk_usage(d).free / 2 ** 30, 1) for d in free0}})
        if p.returncode:
            sys.exit('build_watson: %s failed (exit %d): %s; see %s' % (stage, p.returncode, ' '.join(cmd), log))

    def timing(self, row):
        import threading
        if not hasattr(self, '_tlock'):
            self._tlock = threading.Lock()
        with self._tlock:
            f = self.logs / 'timings.json'
            rows = json.loads(f.read_text(encoding='utf-8')) if f.exists() else []
            rows.append(row)
            tmp = f.with_suffix('.tmp')
            tmp.write_text(json.dumps(rows, indent=1), encoding='utf-8')
            tmp.replace(f)

    def kept(self, name, key):
        """a driver-side record (work/driver_records.json): whether name's last run had this key"""
        f = self.work / 'driver_records.json'
        rec = json.loads(f.read_text(encoding='utf-8')) if f.exists() else {}
        return rec.get(name) == key and not os.environ.get('CP2GLTF_RERUN')

    def keep(self, name, key):
        f = self.work / 'driver_records.json'
        rec = json.loads(f.read_text(encoding='utf-8')) if f.exists() else {}
        rec[name] = key
        tmp = f.with_suffix('.tmp')
        tmp.write_text(json.dumps(rec, indent=1, sort_keys=True), encoding='utf-8')
        tmp.replace(f)

    def side_key(self, tool_name, files=(), trees=(), extra=None):
        """T5: a side stage's inputs as a key: its tool's module closure, files by size and time, folders by every
        file's size and time below them, and its arguments"""
        import hashlib
        h = hashlib.sha256(tool_name.encode())
        for f in self.closure(tool_name):
            h.update(('%s %s\n' % (f.name, file_sha(f))).encode())
        for f in files:
            f = Path(f)
            if f.exists():
                st = f.stat()
                h.update(('%s %d %d\n' % (f, st.st_size, st.st_mtime_ns)).encode())
        for t in trees:
            for f in sorted(Path(t).rglob('*')) if Path(t).is_dir() else []:
                if f.is_file():
                    st = f.stat()
                    h.update(('%s %d %d\n' % (f.relative_to(t), st.st_size, st.st_mtime_ns)).encode())
        h.update(json.dumps(extra, sort_keys=True, default=str).encode())
        return h.hexdigest()[:24]

    def extraction_files(self):
        """what an extraction leaves that the side stages read: the fetch and scan records, the placements"""
        w = self.work
        return [w / 'stage_records.json', w / 'placements.json', w / 'fetch_missing.txt', w / 'json',
                w / 'entity_meshes.json', w / 'light_time.json']

    def cp2gltf(self, stage, *args, env=None):
        self.sh(stage, [PY, '-u', tool('cp2gltf.py'), *args, '--area', self.area], env)

    # ---- the stages ----

    def extract(self):
        s = 'extract'
        if not self.a.extract:
            print('== extract: kept (the extraction in the work folder; --extract re-makes it)', flush=True)
            return 'kept'
        self.cp2gltf(s, 'sectors')
        self.cp2gltf(s, 'sectors', '--sector-kinds', 'interior')
        self.cp2gltf(s, 'scan')
        # the entities first: their mesh components (street crossings, sign panels, ladders, turrets) are fetched by
        # the mesh fetches after them; fetched last, a fresh extraction had none of them (tag 13, 2026-11-20)
        self.cp2gltf(s, 'fetch', '--ents')
        # the entity fetch reports its mesh components only on a pass that found its entities' templates fetched
        # (tag 10a); a second pass with CP2GLTF_RERUN picks them up
        if not self.a.dry_run and 'entity mesh components' not in (self.logs / 'extract.log').read_text(
                encoding='utf-8', errors='replace'):
            self.cp2gltf(s, 'fetch', '--ents', env={'CP2GLTF_RERUN': '1'})
        for extra in ([], ['--lods'], ['--proxies'], ['--proxies', '--lods'], ['--embedded'], ['--embedded-ml']):
            self.cp2gltf(s, 'fetch', *extra)
        self.devices()
        self.sh(s, [PY, '-I', tool('light_time.py'), self.area])
        self.moon()

    def devices(self):
        """the city's device graph (light_time.py: the lamps fuses switch by the clock): CP2077's
        base/worlds/03_night_city/_compiled/default/03_night_city.devices unbundled and serialised into work/devices"""
        out = self.work / 'devices'
        print('== extract: devices -> %s' % out, flush=True)
        if self.a.dry_run:
            return
        import cp2gltf
        import re
        raw = self.work / 'devices_raw'
        rx = '^%s$' % re.escape(cp2gltf.SL.join(('base', 'worlds', '03_night_city', '_compiled', 'default',
                                                  '03_night_city.devices')))
        cp2gltf.wolvenkit('unbundle', *cp2gltf.archives(), '-r', rx, '-o', raw, '-v', 'Minimal')
        src = sorted(raw.rglob('03_night_city.devices'))
        if not src:
            sys.exit('build_watson: extract: the device graph was not unbundled into %s' % raw)
        out.mkdir(parents=True, exist_ok=True)
        cp2gltf.wolvenkit('convert', 'serialize', src[0].parent, '-o', out, '-v', 'Minimal')
        if not (out / '03_night_city.devices.json').exists():
            sys.exit('build_watson: extract: no %s' % (out / '03_night_city.devices.json'))

    def moon(self):
        """the moon's image for the env stage: CP2077's engine/textures/sky/moon.xbm uncooked to PNG
        (work/moon; AtmosphereAreaSettings.moonTexture, a grey disc in its alpha)"""
        out = self.work / 'moon'
        print('== extract: moon -> %s' % self.moon_file(), flush=True)
        if self.a.dry_run:
            return
        import cp2gltf
        import re
        rx = '^%s$' % re.escape(cp2gltf.SL.join(('engine', 'textures', 'sky', 'moon.xbm')))
        cp2gltf.wolvenkit('uncook', *cp2gltf.archives(), '-r', rx, '-o', out,
                          '--uext', 'png', '-v', 'Minimal')
        if not self.moon_file().exists():
            sys.exit('build_watson: extract: the moon fetch left no %s' % self.moon_file())
        self.inp.setdefault('moon', str(self.moon_file()))

    def tiles_(self):
        """cp2gltf tiles in cp77_tiles' own words, then its tiles.key (without the key, the parts stage
        tiled the area again, 4.7 min)"""
        tcmd = [PY, '-u', tool('cp2gltf.py'), 'tiles', '--area', self.area, '--tiles-tag', self.tag, '--tile-size',
                str(256.0), '--proxies']
        prev = self.inp.get('from_tag')
        if prev and not self.a.dry_run:       # (the earlier tag's tiling, linked while its key holds)
            import cp2gltf
            import cp77_tiles
            pcmd = [c if c != self.tag else prev for c in tcmd]
            if cp77_tiles.link_tiling(self.work, cp2gltf.tile_dir(self.work, prev), self.tiles, tcmd, pcmd):
                print('== tiles: linked from tag %s (its tiles.key holds)' % prev, flush=True)
                return
        if not self.a.dry_run:
            # (the tiling kept while its tiles.key holds: a change elsewhere in cp2gltf.py moves this stage's key, not
            # what stage_tiles reads; a hand tiling of 8 tiles was once tiled over)
            import cp77_tiles
            kf = self.tiles / 'tiles.key'
            if kf.exists() and kf.read_text().strip() == cp77_tiles.tiles_key(self.work, tcmd) and \
                    cp77_tiles.tiles_done(self.tiles):
                print('== tiles: kept (its tiles.key holds)', flush=True)
                return 'kept'
        self.sh('tiles', tcmd)
        if not self.a.dry_run:
            import cp77_tiles
            (self.tiles / 'tiles.key').write_text(cp77_tiles.tiles_key(self.work, tcmd))

    def indexes(self):
        """the ad and rotation indexes (work folder: kept while their inputs hold; a rewrite of
        ad_index.json alone rebuilds every scene part, whose keys stat it) and this tag's foliage"""
        s = 'indexes'
        ads = self.inp.get('ads_dir')
        k = self.side_key('cp77_ads.py', self.extraction_files() + ([Path(ads) / 'ads.json', Path(ads) / 'rendered' /
                          'manifest.json'] if ads else []), extra={'area': self.area, 'ads_dir': ads})
        if (self.work / 'ad_index.json').exists() and self.kept('ads index', k):
            print('== indexes: ad_index.json kept (its inputs unchanged)', flush=True)
        else:
            self.sh(s, [PY, '-I', tool('cp77_ads.py'), 'index', '--area', self.area],
                    env={'CP77_ADS_DIR': ads} if ads else None)
            if not self.a.dry_run:
                self.keep('ads index', k)
        k = self.side_key('cp77_rotate.py', self.extraction_files(), extra={'area': self.area})
        if (self.work / 'rotate_index.json').exists() and self.kept('rotate index', k):
            print('== indexes: rotate_index.json kept (its inputs unchanged)', flush=True)
        else:
            self.sh(s, [PY, '-I', tool('cp77_rotate.py'), 'index', '--area', self.area])
            if not self.a.dry_run:
                self.keep('rotate index', k)
        self.sh(s, [PY, '-I', tool('cp77_foliage.py'), self.area, self.tag, '--tile-size', '256'])

    def parts(self):
        self.sh('parts', [PY, '-u', tool('cp77_tiles.py'), self.area, self.tag, '--map', self.map, '--parts-only',
                          '--workers', str(self.a.workers), '--bake-workers', str(self.a.bake_workers),
                          *(['--from-tag', self.inp['from_tag']] if self.inp.get('from_tag') else [])],
                env={'CP77_ADS_DIR': self.inp['ads_dir']} if self.inp.get('ads_dir') else None)
        if self.inp.get('parts_reference') and not self.a.dry_run:
            self.parts_gate()

    def verdict_dir(self):
        d = self.logs / 'verdict'
        d.mkdir(parents=True, exist_ok=True)
        return d

    def parts_gate(self):
        """the early gate: this tag's collision parts against parts_reference's, array by array,
        right after the parts stage; a difference stops the run here, 80 minutes before the collision compile ends
        (equal parts mean any chunk difference against the reference collision is the chunk stage's)"""
        import cp2gltf
        import tag_verdict
        ref = cp2gltf.tile_dir(self.work, self.inp['parts_reference'])
        print('== parts gate: %s against %s' % (self.tiles, ref), flush=True)
        r = tag_verdict.parts(ref, self.tiles, self.verdict_dir() / 'parts.json')
        if (r['differing'] or r['only_b']) and self.inp.get('parts_gate') != 'report':
            sys.exit('build_watson: parts gate: %d tiles' % len(r['differing']) + "' collision parts differ from tag %s's "
                     '(%s), %d only in this tag; see %s' % (self.inp['parts_reference'], ', '.join(
                         list(r['differing'])[:8]), len(r['only_b']), self.verdict_dir() / 'parts.json'))

    def scene(self):
        args = ['scene', '--merge', '--tiled', '--tiles-tag', self.tag, '--tag', self.tag, '--map', self.map,
                '--detail-slots', '8']
        if self.inp.get('ocean_mask'):
            args += ['--ocean-mask', self.inp['ocean_mask']]
        self.cp2gltf('scene', *args, env={'CP77_ADS_DIR': self.inp['ads_dir']} if self.inp.get('ads_dir') else None)

    def interiors(self):
        self.sh('interiors', [PY, '-I', tool('tag_interiors.py'), self.build / ('%s_scene-%s' % (self.map, self.tag)),
                              self.build / ('%s_scene-%s-int' % (self.map, self.tag)), '--area', self.area,
                              '--tag', self.tag, '--map', self.map])

    def collision(self):
        cmd = self.inp.get('collision')
        if not cmd:
            sys.exit('build_watson: no collision entry point in the inputs')
        out = self.build / ('%s_collision-%s' % (self.map, self.tag))
        cmd = [c.format(work=self.work, tag=self.tag, out=out, map=self.map, area=self.area) for c in cmd]
        if self.clutter_mode() == 'in_stage':
            cmd += ['--clutter-drops', self.drops_dir()]
        if self.a.idle:
            cmd += ['--idle']
        self.sh('collision', cmd)

    def collision_dir(self):
        return self.build / ('%s_collision-%s' % (self.map, self.tag))

    def clutter_dir(self):
        return self.build / ('%s_collision-%sc' % (self.map, self.tag))

    def spawns_dir(self):
        return self.build / ('%s_spawns-%s' % (self.map, self.tag))

    def spawns_file(self):
        return Path(self.inp.get('spawns') or HERE / 'areas' / ('%s_spawns.json' % self.area))

    def clutter_mode(self):
        """"in_stage" (default: the boxes left out where the chunk glbs are written), "patch" (colclutter
        patching the compiled packs) or "off" ("clutter": false)"""
        c = self.inp.get('clutter', {})
        return 'off' if c is False else c.get('mode', 'in_stage')

    def drops_dir(self):
        return self.tiles / 'clutter' / 'drops'

    def clutter(self):
        """colclutter scan and drops over this tag's collision parts: which boxes lose collision"""
        c = self.inp.get('clutter', {})
        if self.clutter_mode() == 'off':
            print('== clutter: off in the inputs', flush=True)
            return
        s, tool_ = 'clutter', tool('colclutter.py')
        rule = ['--max-height', str(c.get('max_height', 1.0))] + (['--keep', c['keep']] if c.get('keep') else [])
        self.sh(s, [PY, '-I', tool_, 'scan', self.tiles, self.tiles / 'clutter', '--jobs', str(c.get('jobs', 6))])
        self.sh(s, [PY, '-I', tool_, 'drops', self.tiles / 'clutter', *rule])

    def clutter_patch(self):
        """mode "patch" only: colclutter patch and verify on the compiled packs -> <map>_collision-<tag>c"""
        c = self.inp.get('clutter', {})
        if self.clutter_mode() != 'patch':
            print('== clutter_patch: not in mode "patch" (%s)' % self.clutter_mode(), flush=True)
            return
        s, tool_ = 'clutter_patch', tool('colclutter.py')
        drops, jobs = self.drops_dir(), ['--jobs', str(c.get('jobs', 6))]
        self.sh(s, [PY, '-I', tool_, 'patch', self.collision_dir(), drops, self.tiles, self.clutter_dir(),
                    '--map', self.map, *jobs, '--max-height', str(c.get('max_height', 1.0))])
        self.sh(s, [PY, '-I', tool_, 'verify', self.collision_dir(), drops, self.tiles, self.clutter_dir(),
                    '--map', self.map])
        if self.a.dry_run:
            return
        v = json.loads((self.clutter_dir() / 'clutter-verify.json').read_text(encoding='utf-8'))
        if not v.get('ok'):
            sys.exit('build_watson: clutter: the verify failed (%s)' % (self.clutter_dir() / 'clutter-verify.json'))

    def spawns(self):
        """the spawn list into the compiled hub, the rest of it as compiled (cp77_collision.put_spawns), with the
        hub check and the spawn corpus for an engine gate"""
        out = self.spawns_dir()
        src = self.collision_dir() / ('zz_%s.pk3' % self.map)
        print('== spawns: %s into %s -> %s' % (self.spawns_file(), src, out), flush=True)
        if self.a.dry_run:
            return
        if out.exists():
            sys.exit('build_watson: %s exists' % out)
        import colbsp
        import cp77_collision as cc
        spawns = json.loads(self.spawns_file().read_text(encoding='utf-8'))['spawns']
        if len(spawns) > 128:
            sys.exit('build_watson: spawns: %d, the engine takes 128' % len(spawns))
        part = out.with_name(out.name + '.partial')
        part.mkdir(parents=True)
        pk = part / ('zz_%s.pk3' % self.map)
        r = cc.put_spawns(src, pk, spawns)
        chk = cc.hub_check(pk)
        a, b = colbsp.read(src), colbsp.read(pk)
        same_lumps = all(bytes(a.data[a.lumps[i][0]:a.lumps[i][0] + a.lumps[i][1]]) ==
                         bytes(b.data[b.lumps[i][0]:b.lumps[i][0] + b.lumps[i][1]]) for i in range(1, colbsp.HEADER_LUMPS))
        with zipfile.ZipFile(src) as za, zipfile.ZipFile(pk) as zb:
            bsp = next(n for n in za.namelist() if n.endswith('.bsp'))
            same_members = sorted(za.namelist()) == sorted(zb.namelist()) and all(
                za.read(n) == zb.read(n) for n in za.namelist() if n != bsp)
        cc.spawn_corpus(spawns, part / 'spawns.txt')
        info = {'spawns_file': str(self.spawns_file()), 'source_hub': str(src), 'put_spawns': r,
                'classes': chk['classes'], 'spawns_in_bsp': len(chk['spawns']), 'other_lumps_identical': same_lumps,
                'other_members_identical': same_members}
        (part / 'hub-spawns.json').write_text(json.dumps(info, indent=1), encoding='utf-8')
        if len(chk['spawns']) != len(spawns) or not same_lumps or not same_members:
            sys.exit('build_watson: spawns: the hub check failed: %s' % json.dumps(info))
        part.rename(out)

    def wk_env(self):
        return {'WOLVENKIT_CLI': self.inp['wolvenkit_cli']} if self.inp.get('wolvenkit_cli') else None

    def decals_builds(self):
        return {p for p in self.build.glob('%s_decals-*' % self.map) if p.is_dir()}

    def records(self, kind):
        f = self.build / ('%s_%s_records.json' % (self.map, kind))
        return f, (json.loads(f.read_text(encoding='utf-8')) if f.exists() else {})

    def decals(self):
        """WolvenKit's decal fetch under --extract only (it once ran in every rebuild); the build kept while
        its inputs hold (build/<map>_decals_records.json: key -> the decals build that has them)"""
        root = self.inp['decals_root']
        if self.a.extract:
            self.sh('decals', [PY, '-I', tool('cp77decals.py'), 'fetch', self.work, root], env=self.wk_env())
        key = self.side_key('cp77decals.py', self.extraction_files(), [root], {'map': self.map, 'area': self.area})
        rf, rec = self.records('decals')
        if rec.get(key) and Path(rec[key]).is_dir() and not os.environ.get('CP2GLTF_RERUN'):
            print('== decals: kept %s (its inputs unchanged)' % rec[key], flush=True)
            if not self.a.dry_run:
                (self.logs / 'decals_build.txt').write_text('%s\n' % rec[key], encoding='utf-8')
            return
        before = self.decals_builds()
        self.sh('decals', [PY, '-I', tool('cp77decals.py'), self.work, root, self.build, '--map', self.map,
                           '--area', self.area])
        if self.a.dry_run:
            return
        new = sorted(self.decals_builds() - before)
        if len(new) != 1:
            sys.exit('build_watson: decals: %d new decals builds (%s); the kit takes the one this stage made' % (
                len(new), ', '.join(p.name for p in new)))
        (self.logs / 'decals_build.txt').write_text('%s\n' % new[0], encoding='utf-8')
        rec[key] = str(new[0])
        rf.write_text(json.dumps(rec, indent=1), encoding='utf-8')

    def decals_dir(self):
        """this run's decals build: the inputs' decals_build, else the one its decals stage recorded"""
        if self.inp.get('decals_build'):
            return Path(self.inp['decals_build'])
        rec = self.logs / 'decals_build.txt'
        return Path(rec.read_text(encoding='utf-8').strip()) if rec.exists() else None

    def fx(self):
        """WolvenKit's fx fetch under --extract only; the build kept while its inputs hold: an earlier tag's fx build
        with the same key is hard-linked as this tag's (build/<map>_fx_records.json)"""
        root = Path(self.inp['fx_root'])
        if self.a.extract:
            self.sh('fx', [PY, '-I', tool('cp77fx.py'), 'fetch', self.work, root], env=self.wk_env())
        out = self.build / ('%s_fx-%s' % (self.map, self.tag))
        key = self.side_key('cp77fx.py', self.extraction_files(), [root / 'json', root / 'png'], {'map': self.map})
        rf, rec = self.records('fx')
        src = Path(rec[key]) if rec.get(key) else None
        if src is not None and src.is_dir() and src != out and not out.exists() and not os.environ.get(
                'CP2GLTF_RERUN'):
            print('== fx: kept %s as %s (hard links; its inputs unchanged)' % (src, out), flush=True)
            if not self.a.dry_run:
                part = out.with_name(out.name + '.partial')
                part.mkdir(parents=True)
                for f in src.iterdir():
                    if f.is_file():
                        os.link(f, part / f.name)
                part.rename(out)
            return
        self.sh('fx', [PY, '-I', tool('cp77fx.py'), self.work, root / 'json', out, '--png', root / 'png',
                       '--map', self.map])
        if not self.a.dry_run:
            rec[key] = str(out)
            rf.write_text(json.dumps(rec, indent=1), encoding='utf-8')

    def env(self):
        """the env packs: the night, dawn and day .xenv files (one pack, env_em as the kits had it) and the moon"""
        out = self.build / ('%s_env-%s' % (self.map, self.tag))
        print('== env -> %s' % out, flush=True)
        if self.a.dry_run:
            return
        if out.exists():
            sys.exit('build_watson: %s exists' % out)
        part = out.with_name(out.name + '.partial')
        part.mkdir(parents=True)
        with zipfile.ZipFile(part / ('zz_%s_env_em.pk3' % self.map), 'w') as z:
            for arc, src in sorted(self.inp['env'].items()):
                z.write(src, arc)
        if self.inp.get('moon'):
            with zipfile.ZipFile(part / ('zz_%s_zz_moon.pk3' % self.map), 'w') as z:
                z.write(self.inp['moon'], 'textures/skies/moon.png')
        (part / 'inputs.json').write_text(json.dumps({'env': self.inp['env'], 'moon': self.inp.get('moon')}, indent=1),
                                          encoding='utf-8')
        part.rename(out)

    def kit(self):
        """build/<map>_baseline-<tag>: bin64 (the x64 engine, renderer and SDL2), home/base (the game modules, the
        settings and every pack, hard links), the launchers"""
        kit = self.kit_dir()
        print('== kit -> %s' % kit, flush=True)
        if self.a.dry_run:
            return
        if kit.exists():
            sys.exit('build_watson: %s exists' % kit)
        if not self.a.bin or not (Path(self.a.bin) / 'xenjk.x86_64.exe').exists():
            sys.exit('build_watson: the kit stage needs --bin, a folder with xenjk.x86_64.exe (got %s)' % self.a.bin)
        bin_dir = Path(self.a.bin)
        b = self.build
        scene = b / ('%s_scene-%s-int' % (self.map, self.tag))
        packs = sorted(scene.glob('*.pk3'))
        # (the patched build only in mode "patch": a leftover one is not linked silently)
        col = self.clutter_dir() if self.clutter_mode() == 'patch' else self.collision_dir()
        packs += sorted(col.glob('zz_%s_colchunks*.pk3' % self.map))
        hub = self.spawns_dir() if self.spawns_dir().exists() else self.collision_dir()
        packs.append(hub / ('zz_%s.pk3' % self.map))
        dec = self.decals_dir()
        if dec is None or not dec.is_dir():
            sys.exit('build_watson: the kit stage has no decals build (the decals stage records its own; or set '
                     '"decals_build" in the inputs)')
        packs += sorted(dec.glob('*.pk3'))
        packs += sorted((b / ('%s_fx-%s' % (self.map, self.tag))).glob('*.pk3'))
        packs += sorted((b / ('%s_env-%s' % (self.map, self.tag))).glob('*.pk3'))
        packs += [Path(pk) for pk in self.inp.get('extra_packs', [])]
        for name, src in sorted(self.inp.get('replace_packs', {}).items()):   # (by name: a report round's pack)
            if not any(pk.name.upper() == name.upper() for pk in packs):
                sys.exit('build_watson: replace_packs: no %s among the kit\'s packs' % name)
            packs = [pk for pk in packs if pk.name.upper() != name.upper()] + [Path(src)]
        names = [p.name.upper() for p in packs]
        if len(set(names)) != len(names):
            sys.exit('build_watson: two packs share a name: %s' % sorted(n for n in names if names.count(n) > 1))
        part = kit.with_name(kit.name + '.partial')
        if part.exists():
            # an earlier attempt stopped part way: set aside, as cp2gltf does, never written into
            part.rename(kit.with_name('%s.failed-%s' % (kit.name, time.strftime('%Y%m%dT%H%M%S'))))
        (part / 'bin64').mkdir(parents=True)
        (part / 'home' / 'base').mkdir(parents=True)
        for f in ('xenjk.x86_64.exe', 'rd-vulkan_x86_64.dll'):
            shutil.copy2(bin_dir / f, part / 'bin64' / f)
        if self.inp.get('renderer'):          # (a renderer build of its own: the report rounds' DLLs)
            shutil.copy2(self.inp['renderer'], part / 'bin64' / 'rd-vulkan_x86_64.dll')
        shutil.copy2(self.inp['sdl2'], part / 'bin64' / 'SDL2.dll')
        for f in ('cgamex86_64.dll', 'jampgamex86_64.dll', 'uix86_64.dll'):
            shutil.copy2(bin_dir / f, part / 'home' / 'base' / f)
        shutil.copy2(self.inp['cfg'], part / 'home' / 'base' / 'openjk.cfg')
        if self.inp.get('cfg_set'):           # (after the template's lines: the later seta wins)
            with open(part / 'home' / 'base' / 'openjk.cfg', 'a', encoding='utf-8', newline='\r\n') as f:
                f.write('\n// build_watson: the inputs\' cfg_set\n' + '\n'.join(self.inp['cfg_set']) + '\n')
        for p in packs:
            os.link(p, part / 'home' / 'base' / p.name)
        game = self.inp['game_data']
        for name, extra in LAUNCHERS.items():
            (part / name).write_text(
                '@echo off\r\nrem Watson, baseline tag %s (build_watson.py, %s): converted from game data for local '
                'play only.\r\ncd /d "%%~dp0bin64"\r\nstart "" "%%~dp0bin64\\xenjk.x86_64.exe" +set fs_basepath "%s" '
                '+set fs_homepath "%%~dp0home" +set cl_renderer rd-vulkan +set net_ip 127.0.0.1 +set sv_pure 0 '
                '+devmap %s%s\r\n' % (self.tag, time.strftime('%Y-%m-%d'), game, self.map, extra), encoding='utf-8',
                newline='')        # (the text's own CRLF as it is: no CR CR LF)
        (part / 'KIT.txt').write_text(
            'Watson baseline kit, tag %s (build_watson.py %s). Binaries: %s%s. Packs:\n%s\n' % (
                self.tag, time.strftime('%Y-%m-%d %H:%M'), bin_dir,
                ', renderer %s' % self.inp['renderer'] if self.inp.get('renderer') else '',
                '\n'.join(str(p) for p in packs)),
            encoding='utf-8')
        import kit_derive
        kit_derive.write_kit_json(part, None, kit.name)       # (every file's size and sha256: kit_verify.py)
        shutil.copy2(self.a.data, self.logs / ('inputs-%s.json' % self.tag))
        part.rename(kit)

    def kit_dir(self):
        return self.build / (self.a.kit_name or '%s_baseline-%s' % (self.map, self.tag))

    def checks(self):
        """every check of the tag into logs/verdict/, then tag_verdict.py judge against the inputs' expectations: a
        miss fails the stage (and the run). parity against the reference kit; the served materials table's counts;
        the scene summary and the inputs' log counts; the chunks against the reference collision (byte-equal, else
        brush-equal); the hub (before spawns) and the spawns hub against theirs, lump by lump; the named packs and map
        files against the reference kit; the engine corpora on the dedicated server under the tie rule; scene_stats"""
        import re
        import tag_verdict
        kit, s, v = self.kit_dir(), 'checks', self.verdict_dir() if not self.a.dry_run else self.logs / 'verdict'
        hb = kit / 'home' / 'base'
        tiles = self.inp.get('parity_tiles', [])
        if tiles == 'all':                    # (every tile against the reference kit)
            tj = self.tiles / 'tiles.json'
            tiles = [t['id'] for t in json.load(open(tj, encoding='utf-8'))['tiles']] if tj.exists() else ['(every tile)']
        mats = self.build / ('%s_scene-%s' % (self.map, self.tag)) / 'materials.json'
        self.sh(s, [PY, '-I', tool('scene_parity.py'), self.inp['reference_kit'], hb,
                    '--materials-a', self.inp['reference_materials'], '--materials-b', mats,
                    *['--tile=%s' % t for t in tiles], '--max-details', '200000', '--out', v / 'parity.json',
                    *(['--materials-b-only'] if self.inp.get('parity_materials_b_only') else [])])
        if self.a.dry_run:
            return
        tag_verdict.scene(hb, mats, v / 'scene.json')
        if self.inp.get('expect') and 'materials' in json.loads(Path(self.inp['expect']).read_text(encoding='utf-8')):
            tag_verdict.materials(v / 'parity.json', mats, self.inp['expect'], v / 'materials.json',
                                  self.inp.get('reference_materials'))
        counts = {}
        log = (self.logs / 'scene.log').read_text(encoding='utf-8', errors='replace') if (
            self.logs / 'scene.log').exists() else ''
        m = re.search(r'scene tiled -> .*?: (\d+) tiles, (\d+) instances of (\d+) meshes', log)
        if m:
            counts.update(tiles=int(m[1]), instances=int(m[2]), meshes=int(m[3]))
        m = re.search(r'(\d+) materials, (\d+) images', log[log.rfind('scene tiled ->'):]) if m else None
        if m:
            counts.update(materials=int(m[1]), images=int(m[2]))
        for name, (st, rx) in (self.inp.get('log_counts') or {}).items():
            t = (self.logs / ('%s.log' % st)).read_text(encoding='utf-8', errors='replace') if (
                self.logs / ('%s.log' % st)).exists() else ''
            found = [int(x) for x in re.findall(rx, t)]
            counts[name] = found[-1] if found else None
        (v / 'scene_log.json').write_text(json.dumps(counts, indent=1), encoding='utf-8')
        if (self.tiles / 'scene').is_dir():
            tag_verdict.part_stats(self.tiles / 'scene', v / 'part_stats.json')
        if self.inp.get('reference_collision'):
            tag_verdict.chunks(self.inp['reference_collision'], self.collision_dir() if self.clutter_mode() != 'patch'
                               else self.clutter_dir(), v / 'chunks.json')
        if self.inp.get('reference_hub'):
            tag_verdict.hub(self.inp['reference_hub'], self.collision_dir() / ('zz_%s.pk3' % self.map), v / 'hub.json')
        if self.inp.get('reference_spawns_hub') and (self.spawns_dir() / ('zz_%s.pk3' % self.map)).exists():
            tag_verdict.hub(self.inp['reference_spawns_hub'], self.spawns_dir() / ('zz_%s.pk3' % self.map),
                            v / 'spawns.json')
        if self.inp.get('compare_packs'):
            tag_verdict.packs(self.inp['reference_kit'], hb, v / 'packs.json', self.inp['compare_packs'])
        eng = self.inp.get('engine')
        if eng:
            self.engine_checks(eng, hb, v)
        if self.inp.get('scene_stats'):
            self.sh(s, [PY, '-I', self.inp['scene_stats'], hb])
        if self.inp.get('expect'):
            import io
            import contextlib
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                misses = tag_verdict.judge(self.inp['expect'], v)
            print(buf.getvalue(), end='', flush=True)
            (self.logs / 'verdict.txt').write_text(buf.getvalue(), encoding='utf-8')
            if misses:
                sys.exit('build_watson: verdict: %d checks missed (%s)' % (misses, self.logs / 'verdict.txt'))

    def engine_checks(self, eng, hb, v):
        """each corpus traced on the dedicated server against this kit's collision (its chunk packs and hub), next to
        its reference outputs (<corpus>/traced/levelcheck_out.txt), scored under the tie rule (tag_verdict engine)"""
        base = Path(eng['corpora'])
        mine = v / 'engine'
        ref = v / 'engine_ref'
        for d in sorted(p for p in base.iterdir() if (p / 'traced' / 'levelcheck_out.txt').exists()):
            pre = [ln.strip() for ln in (d / 'traced' / 'run' / 'home' / 'base' / 'bench.cfg').read_text(
                encoding='utf-8', errors='replace').splitlines() if ln.startswith('cm_loadchunk')]
            (ref / d.name).mkdir(parents=True, exist_ok=True)
            shutil.copy2(d / 'traced' / 'levelcheck_out.txt', ref / d.name / 'levelcheck_out.txt')
            packs = sorted(hb.glob('zz_%s_colchunks*.pk3' % self.map))
            cmd = [PY, '-I', tool('engine_trace_ded.py'), hb / ('zz_%s.pk3' % self.map), self.map, d / 'corpus.txt',
                   mine / d.name, '--bin', eng['bin'], '--game', eng['game']]
            for pk in packs:
                cmd += ['--pk3', pk]
            for c in pre:
                cmd += ['--pre', c]
            self.sh('checks', cmd)
        import tag_verdict
        tag_verdict.engine(ref, mine, v / 'engine.json')

def file_sha(p):
    """a tool file's hash for the stage keys, CRLF read as LF (worktrees check files out with other line ends)"""
    import hashlib
    return hashlib.sha256(Path(p).read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tag')
    ap.add_argument('--data', type=Path, required=True, help='the inputs file (see above)')
    ap.add_argument('--bin', help='the x64 Release folder the kit takes its engine, renderer and game modules from '
                                  '(required by the kit stage)')
    ap.add_argument('--from', dest='start', choices=STAGES)
    ap.add_argument('--until', choices=STAGES)
    ap.add_argument('--only', choices=STAGES)
    ap.add_argument('--workers', type=int, default=max(2, min(12, (os.cpu_count() or 4) // 2)))
    ap.add_argument('--bake-workers', type=int, default=12)
    ap.add_argument('--kit-name', help='the kit folder under build/ (default <map>_baseline-<tag>): a kit rebuilt with new '
                                       'binaries beside one in use')
    ap.add_argument('--extract', action='store_true',
                    help='run the extract stage (sectors, scan, fetches) instead of keeping the extraction')
    ap.add_argument('--dry-run', action='store_true', help='the preflight, then print the commands only')
    ap.add_argument('--no-preflight', action='store_true', help='start without the preflight\'s checks')
    ap.add_argument('--idle', action='store_true', help='Idle priority for every stage (the '
                    'children inherit it), half the workers, cp77_collision --idle')
    ap.add_argument('--one-lane', action='store_true', help='the stages one after another (no collision lane beside '
                    'the scene side)')
    a = ap.parse_args()
    sys.path.insert(0, str(HERE))
    import jobguard
    jobguard.kill_children_with_me()          # (a stopped driver ends its stage's tool and workers)
    if a.idle:
        import psutil
        psutil.Process().nice(psutil.IDLE_PRIORITY_CLASS if os.name == 'nt' else 19)
        a.workers = max(1, a.workers // 2)
    inp = json.load(open(a.data, encoding='utf-8'))
    r = Run(a, inp)
    stages = [a.only] if a.only else STAGES[STAGES.index(a.start or STAGES[0]):
                                             STAGES.index(a.until) + 1 if a.until else None]
    r.stages = stages
    if not a.no_preflight:
        r.preflight(stages)
    todo = r.plan(stages)
    import memgate
    import threading
    import uuid
    os.environ[memgate.LEDGER_ENV] = str(r.work / 'memgate-ledger')    # (the lanes' tools see each other)
    mlock = threading.Lock()

    def stage(st):
        if st not in todo:
            m = r.marker(st) or {}
            print('build_watson: %s done (%s)' % (st, m.get('made') or m.get('legacy')), flush=True)
            return
        t0 = time.time()
        old = r.marker(st) or {}
        kept = getattr(r, st + '_' if st == 'tiles' else st)() == 'kept'
        if not a.dry_run:
            with mlock:
                runs = {s: (r.marker(s) or {}).get('run') or (r.marker(s) or {}).get('legacy') for s in STAGES}
                r.logs.mkdir(parents=True, exist_ok=True)
                # (a stage that kept its output keeps its run id: the stages reading it read the same thing)
                run = old.get('run') if kept and old.get('run') else uuid.uuid4().hex[:12]
                done = r.logs / ('%s.done' % st)
                tmp = done.with_name(done.name + '.tmp')       # (renamed when whole)
                tmp.write_text(json.dumps({
                    'made': time.strftime('%Y-%m-%d %H:%M'), 'minutes': round((time.time() - t0) / 60, 1),
                    'key': r.stage_key(st, runs), 'run': run, 'parts': r.key_parts(st, runs)}),
                    encoding='utf-8')
                os.replace(tmp, done)
                f = r.logs / 'timings.json'
                rows = json.loads(f.read_text(encoding='utf-8')) if f.exists() else []
                mine = [x for x in rows if x.get('stage') == st and 'cmd' in x and x['start'] >= time.strftime(
                    '%Y-%m-%d %H:%M:%S', time.localtime(t0))]
                r.timing({'stage': st, 'stage_seconds': round(time.time() - t0, 1), 'commands': len(mine),
                          'stage_peak_private_gb': max([x['peak_private_gb'] for x in mine] or [0.0])})

    def lane(sts, errors, stop):
        for st in sts:
            if stop.is_set():
                return
            try:
                stage(st)
            except BaseException as e:            # (a SystemExit in a thread is otherwise dropped)
                errors.append('%s: %s' % (st, e))
                stop.set()
                return
    order = [st for st in STAGES if st in stages]
    pre = [st for st in order if st in PREFIX]
    la, lb = ([st for st in order if st in LANES[0]], [st for st in order if st in LANES[1]])
    post = [st for st in order if st in SUFFIX]
    for st in pre:
        stage(st)
    errors, stop = [], threading.Event()
    both = not a.one_lane and any(st in todo for st in la) and any(st in todo for st in lb)
    if both:
        r.gate = memgate.Gate(2.0, log=lambda m: print('build_watson: %s' % m, flush=True))
        print('build_watson: two lanes: %s beside %s' % (', '.join(la), ', '.join(lb)), flush=True)
        ths = [threading.Thread(target=lane, args=(x, errors, stop)) for x in (la, lb)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        r.gate = None
    else:
        lane(la + lb, errors, stop)
    if errors:
        sys.exit('build_watson: %s' % '; '.join(errors))
    for st in post:
        stage(st)
    print('build_watson: %s: %s done' % (a.tag, ', '.join(stages)), flush=True)


if __name__ == '__main__':
    main()
