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
"""Brightness statistics of look screenshots, for comparing a converted level against reference shots.

    python tools/levelport/look_stats.py <image> [<image> ...] [--crop-bottom 0.06]

Per image, on luminance (Rec. 709, 0-255) of a 480-wide copy:
  p01..p99, mean       percentiles and mean
  <20, >200            share of dark and bright pixels
  large                std of a 17 px box blur: contrast between areas
  fine                 mean |L - blur|: contrast within surfaces
  top35 / mid / bot40  mean of the top 35%, the middle and the bottom 40% of the frame
--crop-bottom drops a caption strip (ArtStation portfolio shots carry one, about 6%).
"""
import argparse

import numpy as np
from PIL import Image
from scipy.ndimage import uniform_filter


def stats(path, crop_bottom=0.0):
    im = Image.open(path).convert('RGB')
    if crop_bottom > 0:
        im = im.crop((0, 0, im.width, round(im.height * (1 - crop_bottom))))
    w = 480
    h = round(im.height * w / im.width)
    a = np.asarray(im.resize((w, h), Image.BOX), np.float32)
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    blur = uniform_filter(lum, 17, mode='nearest')
    pct = np.percentile(lum, [1, 10, 25, 50, 75, 90, 99])
    return {'p01': pct[0], 'p10': pct[1], 'p25': pct[2], 'p50': pct[3], 'p75': pct[4], 'p90': pct[5], 'p99': pct[6],
            'mean': lum.mean(), '<20': (lum < 20).mean(), '>200': (lum > 200).mean(),
            'large': blur.std(), 'fine': np.abs(lum - blur).mean(),
            'top35': lum[:int(0.35 * h)].mean(), 'mid': lum[int(0.35 * h):int(0.6 * h)].mean(),
            'bot40': lum[int(0.6 * h):].mean()}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('images', nargs='+')
    ap.add_argument('--crop-bottom', type=float, default=0.0)
    a = ap.parse_args(argv)
    keys = ('p01', 'p10', 'p25', 'p50', 'p75', 'p90', 'mean', '<20', '>200', 'large', 'fine', 'top35', 'mid', 'bot40')
    print('%-40s ' % 'image' + ' '.join('%6s' % k for k in keys))
    for p in a.images:
        s = stats(p, a.crop_bottom)
        print('%-40s ' % p[-40:] + ' '.join(('%6.3f' if k in ('<20', '>200') else '%6.1f') % s[k] for k in keys))


if __name__ == '__main__':
    main()
