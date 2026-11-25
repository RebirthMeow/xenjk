"""w3par.py -- dump Witcher 3 layer lists with several headless Blenders at once (Beauclair extraction).

One Blender at a time was the rule because imports that share the add-on's uncook folder race on the files they
materialize. Here each worker has its own uncook folder (<root>/p<k>/uncook, created on first use); when every part is
dumped, each worker's files are copied into the shared uncook folder (a file already there is kept) and the parts'
JSON is rewritten from the worker's folder to the shared one, so later steps read one folder as before.

    python tools/levelport/w3par.py --root <par root> --uncook <shared uncook> --workers 3
        --part <name>=<layer list> [--part ...] --work-prefix <dir>/work-d- [--tool tools/levelport/w3gltf.py]

A part is dumped to <work-prefix><name> with <tool> (w3gltf.py's arguments); a part whose summary.json exists is
skipped. Parts go to the first free worker, largest layer list first. Logs: <work-prefix><name>.log. Blender is run
at normal priority; W3_GAME must be set. Exit 1 if any part failed (its log names why).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BLENDER = os.environ.get('BLENDER', r'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe')
RUNNER = '''import bpy, runpy, sys
argv = sys.argv[sys.argv.index('--') + 1:]
unc = argv[argv.index('--uncook') + 1]
tool = argv[argv.index('--tool') + 1]
for k in ('--uncook', '--tool'):
    i = sys.argv.index(k)
    del sys.argv[i:i + 2]
bpy.context.preferences.addons['bl_ext.user_default.witcher3_tools'].preferences.uncook_path = unc
runpy.run_path(tool, run_name='__main__')
'''


def layers_in(path):
    return sum(1 for ln in open(path, encoding='utf-8') if ln.strip() and not ln.lstrip().startswith('#'))


def norm(p):
    return str(p).replace('\\', '/').rstrip('/')


def rewrite(work, old, new):
    """Every .json in a work folder: the worker's uncook folder replaced by the shared one (both slash styles, as JSON
    writes a backslash path); returns the files changed"""
    changed = 0
    pairs = [(old, new), (old.replace('/', '\\\\'), new.replace('/', '\\\\'))]
    for f in Path(work).rglob('*.json'):
        s = f.read_text(encoding='utf-8')
        t = s
        for a, b in pairs:
            t = t.replace(a, b)
            t = t.replace(a.lower(), b)
        if t != s:
            tmp = f.with_suffix('.json.tmp')
            tmp.write_text(t, encoding='utf-8')
            tmp.replace(f)
            changed += 1
    return changed


def merge_uncook(src, dst):
    """src's files into dst where dst has none (the same depot file decodes to the same bytes); returns (copied, kept)"""
    copied = kept = 0
    for d, _dirs, files in os.walk(src):
        rel = os.path.relpath(d, src)
        out = os.path.join(dst, rel)
        os.makedirs(out, exist_ok=True)
        for fn in files:
            t = os.path.join(out, fn)
            if os.path.exists(t):
                kept += 1
                continue
            shutil.copy2(os.path.join(d, fn), t)
            copied += 1
    return copied, kept


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', required=True, help="the workers' folders (p1, p2, ...)")
    ap.add_argument('--uncook', required=True, help='the shared uncook folder later steps read')
    ap.add_argument('--workers', type=int, default=3)
    ap.add_argument('--part', action='append', required=True, help='name=layer list')
    ap.add_argument('--work-prefix', required=True)
    ap.add_argument('--tool', default=str(HERE / 'w3gltf.py'))
    ap.add_argument('--no-merge', action='store_true', help="leave the workers' uncook folders and paths as they are")
    a = ap.parse_args(argv)
    if not os.environ.get('W3_GAME'):
        sys.exit('w3par: set W3_GAME')
    root = Path(a.root)
    parts = []
    for p in a.part:
        name, lst = p.split('=', 1)
        parts.append((layers_in(lst), name, lst))
    parts.sort(reverse=True)
    runner = root / 'w3par_runner.py'
    root.mkdir(parents=True, exist_ok=True)
    runner.write_text(RUNNER, encoding='utf-8')
    queue = list(parts)
    lock = threading.Lock()
    done, failed = [], []
    used = set()

    def worker(k):
        unc = root / ('p%d' % k) / 'uncook'
        unc.mkdir(parents=True, exist_ok=True)
        while True:
            with lock:
                if not queue:
                    return
                n, name, lst = queue.pop(0)
            work = a.work_prefix + name
            if (Path(work) / 'summary.json').exists():
                print('%s %s: already dumped' % (time.strftime('%H:%M:%S'), name), flush=True)
                continue
            used.add(k)
            t = time.time()
            with open(work + '.log', 'w', encoding='utf-8') as log:
                r = subprocess.run([BLENDER, '-b', '-noaudio', '--python', str(runner), '--', '--uncook', norm(unc) + '/',
                                    '--tool', a.tool, '--layers', lst, '--work', work], cwd=str(REPO), stdout=log,
                                   stderr=subprocess.STDOUT)
            tail = ''
            for ln in open(work + '.log', encoding='utf-8', errors='replace'):
                if ln.startswith('W3GLTF_DONE'):
                    tail = ln.strip()[:200]
            ok = r.returncode == 0 and (Path(work) / 'summary.json').exists()
            with lock:
                (done if ok else failed).append((name, k))
            print('%s %s (p%d, %d layers): exit %d %.0f min %s' % (time.strftime('%H:%M:%S'), name, k, n, r.returncode,
                                                                  (time.time() - t) / 60, tail), flush=True)

    threads = [threading.Thread(target=worker, args=(k,)) for k in range(1, a.workers + 1)]
    for th in threads:
        th.start()
    for th in threads:
        th.join()
    if not a.no_merge:
        shared = norm(a.uncook)
        for k in sorted(used):
            unc = norm(root / ('p%d' % k) / 'uncook')
            c, kept = merge_uncook(unc, shared)
            nfix = sum(rewrite(a.work_prefix + name, unc, shared) for name, kk in done if kk == k)
            print('%s merged p%d: %d files copied, %d already there; %d json files rewritten' % (
                time.strftime('%H:%M:%S'), k, c, kept, nfix), flush=True)
    print('W3PAR_DONE %s' % json.dumps({'done': [n for n, _ in done], 'failed': [n for n, _ in failed]}), flush=True)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
