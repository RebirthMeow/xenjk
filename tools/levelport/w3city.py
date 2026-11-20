"""w3city.py -- several Witcher 3 work folders as one (Beauclair's districts into the city).

Plain Python. Each source is a work folder made by w3gltf.py (and w3trees.py / w3merge.py after it): placements.json
(placements, lights, fx, skipped), materials.json, meshes/index.json with meshes/*.glb, collision.json (shapes). The
city folder gets them all, in source order:

- placements, lights and fx appended; a placement whose layer an earlier source already gave is left out (the same
  layer dumped twice), so the order decides;
- materials and mesh index records by name / depot path, the first source's kept (the Blender material names carry
  the add-on's ids and are the same across dumps of the same files);
- mesh glbs hard-linked into <city>/meshes (one copy on disk; a copy where the drive refuses a link): file names are
  w3gltf's mesh keys, the same for the same depot path in every source;
- collision shapes appended, layers given twice left out as for the placements.

Terrain is not merged: run w3terrain.py on the city folder with the city's box.

    python tools/levelport/w3city.py --out <city work> <work> [<work> ...]
"""
import argparse
import filecmp
import json
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3work  # noqa: E402


def link(src, dst):
    if dst.exists():
        # the same file (a hard link), or equal in size and time: kept; else a re-export replaced the source, so the
        # staged copy is stale (it kept the old geometry beside new metadata)
        if os.path.samefile(src, dst) or filecmp.cmp(src, dst, shallow=True):
            return 'kept'
        tmp = dst.with_name(dst.name + '.new')
        shutil.copy2(src, tmp)
        os.replace(tmp, dst)
        return 'replaced'
    try:
        os.link(src, dst)
        return 'linked'
    except OSError:
        shutil.copy2(src, dst)
        return 'copied'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('works', nargs='+')
    a = ap.parse_args()
    out = Path(a.out)
    (out / 'meshes').mkdir(parents=True, exist_ok=True)
    pl = {'space': None, 'placements': [], 'lights': [], 'fx': [], 'skipped': {}}
    col = {'space': None, 'shapes': []}
    mats, index = {}, {}
    layers_seen, col_layers_seen = set(), set()
    stats = {'sources': [], 'glbs': {'linked': 0, 'copied': 0, 'kept': 0}}
    for w in map(Path, a.works):
        p = json.load(open(w / 'placements.json', encoding='utf-8'))
        c = json.load(open(w / 'collision.json', encoding='utf-8'))
        pl['space'] = pl['space'] or p.get('space')
        col['space'] = col['space'] or c.get('space')
        mine = {x.get('layer') for x in p['placements']} | {x.get('layer') for x in p.get('lights', [])}
        dup = mine & layers_seen
        n = {'work': str(w), 'placements': 0, 'lights': 0, 'fx': 0, 'shapes': 0, 'duplicate_layers': sorted(
            l for l in dup if l)}
        for key in ('placements', 'lights', 'fx'):
            for x in p.get(key, []):
                if x.get('layer') in dup:
                    continue
                pl[key].append(x)
                n[key] += 1
        for k, v in (p.get('skipped') or {}).items():
            pl['skipped'][k] = pl['skipped'].get(k, 0) + v
        layers_seen |= mine
        cmine = {s.get('layer') for s in c['shapes']}
        cdup = cmine & col_layers_seen
        for s in c['shapes']:
            if s.get('layer') in cdup:
                continue
            col['shapes'].append(s)
            n['shapes'] += 1
        col_layers_seen |= cmine
        for name, rec in json.load(open(w / 'materials.json', encoding='utf-8')).items():
            mats.setdefault(name, rec)
        for key, rec in json.load(open(w / 'meshes' / 'index.json', encoding='utf-8')).items():
            if key not in index:
                index[key] = rec
                stats['glbs'][link(w / 'meshes' / rec['file'], out / 'meshes' / rec['file'])] += 1
        stats['sources'].append(n)
        print('w3city', w.name, {k: v for k, v in n.items() if k != 'work'}, flush=True)
    w3work.new_export(str(out))
    for name, data in (('placements.json', pl), ('collision.json', col), ('materials.json', mats),
                       ('meshes/index.json', index)):
        f = out / name
        tmp = f.with_name(f.name + '.partial')
        with open(tmp, 'w', encoding='utf-8') as fp:
            json.dump(data, fp)
        os.replace(tmp, f)
    stats.update(placements=len(pl['placements']), lights=len(pl['lights']), fx=len(pl['fx']),
                 shapes=len(col['shapes']), materials=len(mats), meshes=len(index))
    with open(out / 'city.json', 'w') as fp:
        json.dump(stats, fp, indent=1)
    print('w3city ->', out, {k: v for k, v in stats.items() if k != 'sources'})


if __name__ == '__main__':
    main()
