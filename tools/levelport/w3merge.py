"""w3merge.py -- merge a staging folder into a Witcher 3 work folder (Beauclair).

Plain Python. A staging folder holds what a side tool made in the work folder's shape: meshes/*.glb with
meshes/index.json (depot path -> record with 'file'), materials.json and placements.json (w3flags.py's flags, for one).
This copies the glbs into <work>/meshes/, adds the index records and materials, and appends the placements (their kind
set to 'w2mesh', the kind w3scene.py draws; the original kind kept as 'source_kind'). Placements whose mesh's file name
matches a --drop pattern are removed from the work folder (the castle's flag layer placed eight blockout boxes,
box_cloth.w2mesh, that the game hides).

The work folder's three JSON files are copied once to <name>.pre-<tag>.json before the first merge with that tag, and a
re-run starts from those copies, so merging twice does not add twice. A re-run whose copies are older than another
step's last run is refused (w3work.py); --rebase sets them aside and starts from the current files.

    python tools/levelport/w3merge.py --work <work> --stage <stage> --tag flags [--drop box_cloth.w2mesh ...] [--rebase]
"""
import argparse
import fnmatch
import filecmp
import json
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w3work  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--work', required=True)
    ap.add_argument('--stage', required=True)
    ap.add_argument('--tag', required=True)
    ap.add_argument('--drop', nargs='*', default=[], help='mesh file-name patterns removed from the work placements')
    ap.add_argument('--rebase', action='store_true', help="start from the current files when this tag's copies are "
                                                          "stale (w3work.py)")
    a = ap.parse_args()
    work, stage = Path(a.work), Path(a.stage)
    files = {k: work / rel for k, rel in w3work.FILES.items()}
    data = {}
    for k, pre in w3work.start(str(work), a.tag, rebase=a.rebase).items():
        with open(pre, encoding='utf-8') as f:
            data[k] = json.load(f)
    s_index = json.load(open(stage / 'meshes' / 'index.json', encoding='utf-8'))
    s_mats = json.load(open(stage / 'materials.json', encoding='utf-8'))
    s_pl = json.load(open(stage / 'placements.json', encoding='utf-8'))
    s_pl = s_pl['placements'] if isinstance(s_pl, dict) else s_pl

    stats = {'meshes': 0, 'materials': 0, 'placements': 0, 'dropped': 0}
    for key, rec in s_index.items():
        src = stage / 'meshes' / rec['file']
        dst = work / 'meshes' / rec['file']
        # (size and time: a re-export of the same size kept the old geometry; copy2 keeps the time, so an unchanged
        # source matches)
        if not dst.exists() or not filecmp.cmp(src, dst, shallow=True):
            shutil.copy2(src, dst)
        data['index'][key] = rec
        stats['meshes'] += 1
    for name, rec in s_mats.items():
        data['materials'][name] = rec
        stats['materials'] += 1
    pl = data['placements']
    keep = []
    for p in pl['placements']:
        base = os.path.basename(p['mesh'].replace('\\', '/'))
        if any(fnmatch.fnmatch(base.lower(), d.lower()) for d in a.drop):
            stats['dropped'] += 1
            continue
        keep.append(p)
    for p in s_pl:
        p = dict(p)
        if p.get('kind') != 'w2mesh':
            p['source_kind'] = p.get('kind')
            p['kind'] = 'w2mesh'
        p['merged_from'] = a.tag
        keep.append(p)
        stats['placements'] += 1
    pl['placements'] = keep
    for k, f in files.items():
        tmp = f.with_name(f.name + '.partial')
        with open(tmp, 'w', encoding='utf-8') as fp:
            json.dump(data[k], fp, indent=None if k != 'index' else 1)
        os.replace(tmp, f)
    w3work.wrote(str(work), a.tag)
    print('w3merge', a.tag, stats)


if __name__ == '__main__':
    main()
