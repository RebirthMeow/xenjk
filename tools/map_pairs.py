#!/usr/bin/env python3
"""Old/new screenshot pairs from two renderer_scenes.py runs (map pipeline).

    python tools/map_pairs.py <old scenes dir> <new scenes dir> --map siege_urban_pbr --out DIR

For each candidate run present in both dirs and each view screenshot: `<cand>_<view>_pair.jpg` (old left, new
right, each half size) and `<cand>_<view>_crop.jpg` (the same 1280x720 region of both at full resolution, old
above new, chosen where the two images differ most), plus `<cand>_sheet.jpg` (old row over new row, all views).
Views are not pixel-identical between runs (pitch comes from a timed +lookdown), so small shifts are expected.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

CROP = (1280, 720)


def shots(run_root, mapname):
    out = {}
    for rd in sorted(Path(run_root).glob('*__%s__0' % mapname)):
        cand = rd.name.split('__')[0]
        out[cand] = {p.stem: p for p in sorted(rd.glob('home/*/screenshots/view_*.jpg'))}
    return out


def label(img, text):
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 12 + 8 * len(text), 22), fill=(0, 0, 0))
    d.text((6, 5), text, fill=(255, 220, 80))
    return img


def best_crop(a, b):
    """Top-left of the CROP window where old and new differ most (coarse grid, centre-weighted)."""
    da = np.asarray(a.convert('L').resize((a.width // 16, a.height // 16)), np.float32)
    db = np.asarray(b.convert('L').resize((a.width // 16, a.height // 16)), np.float32)
    diff = np.abs(da - db)
    cw, ch = CROP[0] // 16, CROP[1] // 16
    best, pos = -1.0, (0, 0)
    for y in range(0, diff.shape[0] - ch + 1, 4):
        for x in range(0, diff.shape[1] - cw + 1, 4):
            s = diff[y:y + ch, x:x + cw].mean()
            # prefer the middle of the frame a little: the edges hold sky and HUD-free borders
            s *= 1.0 - 0.15 * abs((x + cw / 2) / diff.shape[1] - 0.5)
            if s > best:
                best, pos = s, (x * 16, y * 16)
    return pos


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('--map', required=True)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--crop', default='', help='X,Y: fixed top-left for every crop (e.g. when rain or fog '
                    'makes the largest difference something other than the materials)')
    ap.add_argument('--views', default='', help='only these views (comma-separated, e.g. view_start)')
    ap.add_argument('--suffix', default='', help='added to the crop file names')
    ap.add_argument('--zoom', default='', help='X,Y,W,H: also write <cand>_<view>_zoom.jpg, that region of old (left) and '
                    'new (right) at full resolution, enlarged 2x (a close-up of one wall; with --views for one view)')
    args = ap.parse_args(argv)
    old, new = shots(args.old, args.map), shots(args.new, args.map)
    args.out.mkdir(parents=True, exist_ok=True)
    written = []
    for cand in sorted(set(old) & set(new)):
        views = [v for v in old[cand] if v in new[cand] and (not args.views or v in args.views.split(','))]
        tw, th = 960, 540
        sheet = Image.new('RGB', (tw * len(views), th * 2))
        for i, v in enumerate(views):
            a, b = Image.open(old[cand][v]).convert('RGB'), Image.open(new[cand][v]).convert('RGB')
            w, h = a.width // 2, a.height // 2
            pair = Image.new('RGB', (w * 2, h))
            pair.paste(label(a.resize((w, h), Image.Resampling.LANCZOS), 'OLD %s %s' % (cand, v)), (0, 0))
            pair.paste(label(b.resize((w, h), Image.Resampling.LANCZOS), 'NEW %s %s' % (cand, v)), (w, 0))
            p = args.out / ('%s_%s_pair.jpg' % (cand, v))
            pair.save(p, quality=92)
            x, y = map(int, args.crop.split(',')) if args.crop else best_crop(a, b)
            crop = Image.new('RGB', (CROP[0], CROP[1] * 2))
            box = (x, y, x + CROP[0], y + CROP[1])
            crop.paste(label(a.crop(box), 'OLD %s %s crop @%d,%d' % (cand, v, x, y)), (0, 0))
            crop.paste(label(b.crop(box), 'NEW'), (0, CROP[1]))
            c = args.out / ('%s_%s_crop%s.jpg' % (cand, v, args.suffix))
            crop.save(c, quality=92)
            sheet.paste(a.resize((tw, th), Image.Resampling.LANCZOS), (i * tw, 0))
            sheet.paste(b.resize((tw, th), Image.Resampling.LANCZOS), (i * tw, th))
            written += [p.name, c.name]
            if args.zoom:
                zx, zy, zw, zh = map(int, args.zoom.split(','))
                zbox = (zx, zy, zx + zw, zy + zh)
                z = Image.new('RGB', (zw * 4, zh * 2))
                z.paste(label(a.crop(zbox).resize((zw * 2, zh * 2), Image.Resampling.LANCZOS), 'OLD %s %s zoom' % (cand, v)), (0, 0))
                z.paste(label(b.crop(zbox).resize((zw * 2, zh * 2), Image.Resampling.LANCZOS), 'NEW'), (zw * 2, 0))
                zp = args.out / ('%s_%s_zoom%s.jpg' % (cand, v, args.suffix))
                z.save(zp, quality=92)
                written.append(zp.name)
        if not args.views:  # a view subset would overwrite the full sheet
            s = args.out / ('%s_sheet.jpg' % cand)
            label(sheet, 'top: OLD  bottom: NEW  views: %s' % ', '.join(views)).save(s, quality=88)
            written.append(s.name)
    print('\n'.join(written) if written else 'no matching screenshots')


if __name__ == '__main__':
    main()
