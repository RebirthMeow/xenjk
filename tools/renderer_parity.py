#!/usr/bin/env python3
"""Parity pairs between two candidates of renderer_scenes.py runs.

    python tools/renderer_parity.py --map siege_urban_pbr --out DIR \
        --ref <scenes dir>:somaz-rend2-perf --test <scenes dir>:jksunny-vk-s8-sun-par

`--ref`/`--test` are `<run dir>:<candidate>` (the run may be a different dir for each). Per view of the map: a
side-by-side pair (`<view>_pair.jpg`, ref left, test right), a full-resolution crop pair of the region where the two
differ most (`<view>_crop.jpg`, ref above test), and per map a contact sheet (`sheet.jpg`, ref row over test row)
plus `stats.txt`: mean luma of each, the mean absolute luma difference and the signed mean difference (test - ref)
per view and on a 4x4 grid (the worst cell is named, which is where the visible difference is).
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

CROP = (1280, 720)


def shots(spec, mapname):
    root, cand = spec.rsplit(':', 1)
    # a Windows drive letter leaves 'D' + ':' in front; rsplit keeps it in root
    for rd in sorted(Path(root).glob('%s__%s__0' % (cand, mapname))):
        return {p.stem: p for p in sorted(rd.glob('home/*/screenshots/view_*.jpg'))}
    return {}


def label(img, text):
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 12 + 8 * len(text), 22), fill=(0, 0, 0))
    d.text((6, 5), text, fill=(255, 220, 80))
    return img


def luma(img):
    return np.asarray(img.convert('L'), np.float32)


def best_crop(a, b):
    da, db = luma(a), luma(b)
    s = 16
    da = da[:da.shape[0] // s * s, :da.shape[1] // s * s].reshape(da.shape[0] // s, s, da.shape[1] // s, s).mean((1, 3))
    db = db[:db.shape[0] // s * s, :db.shape[1] // s * s].reshape(db.shape[0] // s, s, db.shape[1] // s, s).mean((1, 3))
    diff = np.abs(da - db)
    cw, ch = CROP[0] // s, CROP[1] // s
    best, pos = -1.0, (0, 0)
    for y in range(0, diff.shape[0] - ch + 1, 4):
        for x in range(0, diff.shape[1] - cw + 1, 4):
            v = diff[y:y + ch, x:x + cw].mean() * (1.0 - 0.15 * abs((x + cw / 2) / diff.shape[1] - 0.5))
            if v > best:
                best, pos = v, (x * s, y * s)
    return pos


def grid_stats(a, b, n=4):
    la, lb = luma(a), luma(b)
    h, w = la.shape
    worst, wc = -1.0, None
    for gy in range(n):
        for gx in range(n):
            sl = (slice(gy * h // n, (gy + 1) * h // n), slice(gx * w // n, (gx + 1) * w // n))
            d = float(np.abs(la[sl] - lb[sl]).mean())
            if d > worst:
                worst, wc = d, (gx, gy, float((lb[sl] - la[sl]).mean()))
    return worst, wc


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--map', required=True)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--ref', required=True)
    ap.add_argument('--test', required=True)
    ap.add_argument('--views', default='')
    args = ap.parse_args(argv)
    ref, test = shots(args.ref, args.map), shots(args.test, args.map)
    views = [v for v in ref if v in test and (not args.views or v.replace('view_', '') in args.views.split(','))]
    if not views:
        print('no matching screenshots')
        return
    args.out.mkdir(parents=True, exist_ok=True)
    rn, tn = args.ref.rsplit(':', 1)[1], args.test.rsplit(':', 1)[1]
    tw, th = 960, 540
    sheet = Image.new('RGB', (tw * len(views), th * 2))
    lines = ['%s: ref %s, test %s' % (args.map, rn, tn),
             'view: luma ref / test, mean |diff|, signed diff (test-ref), worst 4x4 cell (col,row,signed)']
    for i, v in enumerate(views):
        a, b = Image.open(ref[v]).convert('RGB'), Image.open(test[v]).convert('RGB')
        if a.size != b.size:
            b = b.resize(a.size)
        w, h = a.width // 2, a.height // 2
        pair = Image.new('RGB', (w * 2, h))
        pair.paste(label(a.resize((w, h), Image.Resampling.LANCZOS), 'REF %s' % rn), (0, 0))
        pair.paste(label(b.resize((w, h), Image.Resampling.LANCZOS), 'TEST %s' % tn), (w, 0))
        pair.save(args.out / ('%s_pair.jpg' % v), quality=92)
        x, y = best_crop(a, b)
        crop = Image.new('RGB', (CROP[0], CROP[1] * 2))
        box = (x, y, x + CROP[0], y + CROP[1])
        crop.paste(label(a.crop(box), 'REF %s %s crop @%d,%d' % (rn, v, x, y)), (0, 0))
        crop.paste(label(b.crop(box), 'TEST %s' % tn), (0, CROP[1]))
        crop.save(args.out / ('%s_crop.jpg' % v), quality=92)
        sheet.paste(a.resize((tw, th), Image.Resampling.LANCZOS), (i * tw, 0))
        sheet.paste(b.resize((tw, th), Image.Resampling.LANCZOS), (i * tw, th))
        la, lb = luma(a), luma(b)
        worst, wc = grid_stats(a, b)
        lines.append('%s: %.1f / %.1f, %.2f, %+.2f, worst %.2f at %s' % (
            v, la.mean(), lb.mean(), np.abs(la - lb).mean(), (lb - la).mean(), worst, wc))
    label(sheet, 'top: REF %s  bottom: TEST %s  views: %s' % (rn, tn, ', '.join(views))).save(
        args.out / 'sheet.jpg', quality=88)
    (args.out / 'stats.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
