"""Both bike wheels on screen (a cgame bone override once folded the front swingarm into the body and the front
tyre was not drawn). A visual gate for any change to the bike's model, bones or pose.

A run of tools/bike/wheel_check.py (or any localhost ride with cg_bikeSeatDebug > 0) logs, per screenshot, an
'echo SHOT <label>' and the cgame's BIKESCREEN lines: each wheel's centre and tyre radius on the 640x480 virtual
screen (cg_bike.c CG_BikePose). Against a reference run (the parked bike, lit, from the side):
  parked shots   each wheel's patch (PATCH_R tyre radii about its centre, resized to SIZE) against the same wheel's
                 patch in the reference shot of the same label: normalised cross-correlation of the grey levels
                 >= MIN_NCC. The bike stands where it spawned, so the shots match nearly pixel for pixel
  ride shots     the rim spins and the floor behind moves, so each wheel's radial profile (mean grey by distance from
                 the centre, PROFILE_BINS rings inside PROFILE_FRAC of the tyre radius: rotation does not change it)
                 against the reference parked wheel's: correlation >= MIN_PROFILE_RIDE
A wheel missing, buried in the body or drawn elsewhere leaves fairing and floor where the tyre should be.
Calibration (against a build with the folded arm): parked patch, fixed 0.998,
folded front 0.676; ride profile, fixed 0.33..0.65 front and 0.91..0.99 back, folded front -0.45.
The reference must be a run with the fork corrected: an earlier reference drew
the fork's hub pieces half a turn round (wheel_front's override is Euler (180, 0, spin) and its child
wheel_front_spring carries the fork). Against that old reference the corrected fork scored 0.895 parked (ride 0.717
front); against the corrected run the folded-arm build fails (front 0.591, --coords-from-reference).

  python -I wheel_gate.py REFERENCE_RUN TEST_RUN [--coords-from-reference]
Exit status 1 if any wheel in any shared shot is under its gate. --coords-from-reference uses the reference's wheel
positions (a build without the BIKESCREEN print: parked shots only).
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image

PATCH_R = 1.0
SIZE = 64
MIN_NCC = 0.9
PROFILE_BINS, PROFILE_FRAC = 16, 0.95
MIN_PROFILE_RIDE = 0.1
WHEELS = ('wheel_front', 'wheel_back')
REF_PARKED = 'parked_arm1'


def shots(run: str, unpaired: list | None = None) -> dict:
    """label -> (screenshot path, {wheel: (x, y, r)}) from the run's console log and screenshots; labels whose second
    has no screenshot go to unpaired."""
    log = glob.glob(os.path.join(run, '*.qconsole.log'))[0]
    files = {os.path.basename(p)[4:23]: p for p in glob.glob(os.path.join(run, 'home_*', 'base', 'screenshots', '*.jpg'))}
    out, last = {}, {}
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.search(r'BIKESCREEN t=\d+ (\w+) x=([-\d.]+) y=([-\d.]+) r=([-\d.]+)', line)
        if m:
            last[m.group(1)] = tuple(float(v) for v in m.group(2, 3, 4))
            continue
        m = re.match(r'(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d) SHOT (\S+)', line)
        if m:
            stamp = '%s_%s-%s-%s' % m.group(1, 2, 3, 4)
            if stamp in files:
                out[m.group(5)] = (files[stamp], dict(last))
            elif unpaired is not None:
                unpaired.append(m.group(5))
    return out


def grey(path: str) -> np.ndarray:
    return np.asarray(Image.open(path).convert('L'), np.float64)


def patch(img: np.ndarray, x: float, y: float, r: float) -> np.ndarray | None:
    """the wheel's patch, or None when its box is not wholly on screen"""
    h, w = img.shape
    cx, cy, rx, ry = x * w / 640.0, y * h / 480.0, r * PATCH_R * w / 640.0, r * PATCH_R * h / 480.0
    y0, y1, x0, x1 = int(cy - ry), int(cy + ry), int(cx - rx), int(cx + rx)
    if y0 < 0 or x0 < 0 or y1 > h or x1 > w or y1 <= y0 or x1 <= x0:
        return None
    a = np.asarray(Image.fromarray(img[y0:y1, x0:x1]).resize((SIZE, SIZE)))
    return a - a.mean()


def profile(img: np.ndarray, x: float, y: float, r: float) -> np.ndarray:
    h, w = img.shape
    cx, cy, rx, ry = x * w / 640.0, y * h / 480.0, r * w / 640.0, r * h / 480.0
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot((xx - cx) / rx, (yy - cy) / ry)
    p = np.array([img[(d >= i * PROFILE_FRAC / PROFILE_BINS) & (d < (i + 1) * PROFILE_FRAC / PROFILE_BINS)].mean()
                  for i in range(PROFILE_BINS)])
    return p - p.mean()


def ncc(a: np.ndarray, b: np.ndarray) -> float:
    return float((a * b).sum() / max(1e-9, np.sqrt((a * a).sum() * (b * b).sum())))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('reference')
    ap.add_argument('test')
    ap.add_argument('--coords-from-reference', action='store_true')
    a = ap.parse_args()
    unpaired = []
    ref, test = shots(a.reference), shots(a.test, unpaired)
    if REF_PARKED not in ref:
        print('the reference has no %s shot: FAIL' % REF_PARKED)
        return 1
    ref_img, ref_pos = grey(ref[REF_PARKED][0]), ref[REF_PARKED][1]
    fails = checked = 0
    for label in unpaired:   # an unchecked shot is not a passed one
        print('%-12s no screenshot in the SHOT line\'s second: FAIL' % label)
        fails += 1
    for label in sorted(test):
        tpath, tpos = test[label]
        parked = label.startswith('parked')
        if parked and label not in ref:
            print('%-12s not in the reference: FAIL' % label)
            fails += 1
            continue
        if a.coords_from_reference:
            if not parked:
                continue
            tpos = ref[label][1]
        timg = grey(tpath)
        for wname in WHEELS:
            if wname not in tpos or wname not in ref_pos:
                print('%-12s %-11s no screen position: FAIL' % (label, wname))
                fails += 1
                continue
            if parked:
                rimg, rpos = grey(ref[label][0]), ref[label][1]
                pr, pt = patch(rimg, *rpos[wname]), patch(timg, *tpos[wname])
                if pr is None or pt is None:
                    print('%-12s %-11s partly off screen: FAIL' % (label, wname))
                    fails += 1
                    continue
                v, gate, what = ncc(pr, pt), MIN_NCC, 'patch'
            else:
                v, gate, what = ncc(profile(ref_img, *ref_pos[wname]), profile(timg, *tpos[wname])), MIN_PROFILE_RIDE, \
                    'profile'
            ok = v >= gate
            fails += not ok
            checked += 1
            print('%-12s %-11s %-7s %.3f (gate %.2f) %s' % (label, wname, what, v, gate, 'ok' if ok else 'FAIL'))
    if not checked:
        print('no labelled shots to check: FAIL')
        fails += 1
    print('%s: %d wheel checks, %d failed' % ('FAIL' if fails else 'PASS', checked, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
