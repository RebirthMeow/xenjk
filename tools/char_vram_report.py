"""char_vram_report.py -- tables and the pop check from char_vram_check.py runs.

    python -I tools/char_vram_report.py table e_before e_after          # samples.json as markdown, one table per tag
    python -I tools/char_vram_report.py pops e_before e_after --sheet pops.jpg

pops: for each tag's screenshots of the still far view (far_a, far_b, then far_d, far_e after the close-up), the
sharpness of the figures (mean absolute Laplacian of the luma inside the figures' band) and the mean absolute difference
between shots. A mip pop while still shows as a sharpness step between far_a and far_b (or far_d and far_e); the
frozen NPCs' idle motion sets the noise floor, which the tag run on the unchanged renderer measures. --sheet writes
the crops side by side.
"""
import argparse
import json
import os
import sys
from pathlib import Path

WORK = (Path(os.environ.get('XENJK_MAIN', Path(__file__).resolve().parents[1])) / '.codex-research'
        / 'char-vram-20261109')   # XENJK_MAIN: the checkout holding .codex-research (default this repository)
COLS = [('label', 'step'), ('client_dedicated_MB', 'client VRAM MB'), ('client_shared_MB', 'shared MB'),
        ('adapter_dedicated_MB', 'adapter MB'), ('above_MB', 'pool held MB'), ('pool_MB', 'pool MB'), ('sharp', 'sharp'),
        ('at_tail', 'at tail'), ('building', 'building'), ('whole_MB', 'whole-loaded MB'), ('squeeze', 'squeeze'),
        ('trimmed', 'trimmed'), ('moved', 'moved'), ('reloads', 'reloads'), ('budget_MB', 'budget MB'),
        ('process_MB', 'process MB'), ('chunks', 'image chunks MB')]


def table(work, tags):
    for tag in tags:
        rows = json.loads((work / 'runs' / tag / 'samples.json').read_text(encoding='utf-8'))
        for r in rows:
            k = r.get('kinds_MB') or {}
            r['chunks'] = k.get('image chunks')
        cols = [c for c in COLS if any(r.get(c[0]) is not None for r in rows)]
        info = json.loads((work / 'runs' / tag / 'run.json').read_text(encoding='utf-8'))
        print('\n**%s** (%s, renderer %s, %s)\n' % (tag, info.get('scene'), info.get('dll_sha256', '')[:8],
                                                    ' '.join('%s=%s' % kv for kv in info.get('sets', {}).items()
                                                             if kv[0].startswith(('r_vram', 'r_stream', 'r_charShadows')))))
        print('| ' + ' | '.join(c[1] for c in cols) + ' |')
        print('|' + '---|' * len(cols))
        for r in rows:
            def f(v):
                if v is None:
                    return ''
                return ('%.0f' % v) if isinstance(v, float) else str(v)
            print('| ' + ' | '.join(f(r.get(c[0])) for c in cols) + ' |')


def pops(work, tags, sheet, band, names):
    import numpy as np
    from PIL import Image
    x0, y0, x1, y1 = band
    crops = {}
    print('| tag | shot | sharpness | diff to the first | diff to previous |')
    print('|---|---|---|---|---|')
    for tag in tags:
        prev = first = None
        for n in names:
            p = work / 'runs' / tag / (n + '.png')
            if not p.exists():
                continue
            im = Image.open(p).convert('RGB')
            w, h = im.size
            box = (int(x0 * w), int(y0 * h), int(x1 * w), int(y1 * h))
            c = im.crop(box)
            crops[(tag, n)] = c
            g = np.asarray(c.convert('L'), dtype=np.float32)
            lap = np.abs(4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1] - g[1:-1, :-2] - g[1:-1, 2:]).mean()
            a = np.asarray(c, dtype=np.float32)
            d0 = '' if first is None else '%.2f' % np.abs(a - first).mean()
            d1 = '' if prev is None else '%.2f' % np.abs(a - prev).mean()
            print('| %s | %s | %.3f | %s | %s |' % (tag, n, lap, d0, d1))
            prev = a
            if first is None:
                first = a
    if sheet and crops:
        cw, ch = next(iter(crops.values())).size
        out = Image.new('RGB', (cw * len(names), ch * len(tags)), (0, 0, 0))
        for r, tag in enumerate(tags):
            for k, n in enumerate(names):
                if (tag, n) in crops:
                    out.paste(crops[(tag, n)], (k * cw, r * ch))
        out.save(sheet, quality=92)
        print('\nsheet: %s (rows %s; columns %s)' % (sheet, ', '.join(tags), ', '.join(names)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', choices=['table', 'pops'])
    ap.add_argument('tags', nargs='+')
    ap.add_argument('--work', default=str(WORK))
    ap.add_argument('--sheet')
    ap.add_argument('--band', type=float, nargs=4, default=[0.36, 0.49, 0.64, 0.60], help='x0 y0 x1 y1 of the figures, 0..1')
    ap.add_argument('--shots', nargs='+', default=['far_a', 'far_b', 'far_c', 'far_d', 'far_e'])
    a = ap.parse_args()
    if a.what == 'table':
        table(Path(a.work), a.tags)
    else:
        pops(Path(a.work), a.tags, a.sheet, a.band, a.shots)


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    main()
