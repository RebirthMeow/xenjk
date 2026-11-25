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
"""w3lighttime.py -- which of a Witcher 3 city's lights the game lights only at night: maps/<map>.xlt.

    python -I w3lighttime.py --work <city> --build-json <city>/city-N.build.json --out DIR --map w3_city

The game switches its street lamps, lanterns, braziers, torches, window lights and fireplaces by the clock: a light
entity's CGameplayLightComponent with isCityLight set belongs to the city light manager (CCityLightManager, native),
which lights it at dusk and puts it out at dawn. Candles, chandeliers and other gameplay lights without the flag keep
their isLightOn state at every hour. The flag is read from each light's entity template (placements.json's
witcher_entity_path); a template named *_daylight (window_light_01_one_light_interior_daylight: a room's daylight
through a window) is taken as lit by day only.

Writes DIR/maps/<map>.xlt (vk_scene.cpp Scene_LoadLightTimes: `xlt 1`, then `night x y z` or `day x y z` per light in
the scene's units; the renderer marks the scene light at that point, within a unit, as night or day only) and
DIR/light_time.json (the counts and templates). Reads the bundles ($W3_GAME); the bundle index is cached at --index-cache.
Converted levels are for local play only.
"""
import argparse
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levelport  # noqa: E402
import w3bundle  # noqa: E402
import w3emitter  # noqa: E402


def chunk_props(f, d, off, size):
    """{name: (type name, value bytes)} of a chunk's properties (w3census.entities' walk)"""
    out = {}
    p = off + 1
    while p + 8 <= off + size:
        ni = struct.unpack_from('<H', d, p)[0]
        if ni == 0:
            break
        ti, sz = struct.unpack_from('<HI', d, p + 2)
        out[f.names[ni]] = (f.names[ti], d[p + 8:p + 4 + sz])
        p += 4 + sz
    return out


def template_class(idx, low, path):
    """'night' (a city light), 'day', 'always' (a gameplay light that stays as it is), 'off' (isLightOn false) or
    'plain' (no gameplay light component: an ordinary light), from an entity template"""
    if path.lower().split('\\')[-1].endswith('_daylight.w2ent'):
        return 'day'
    k = low.get(path.lower())
    if not k:
        return 'plain'
    d = w3bundle.read(idx, k)
    f = w3emitter.CR2W(d)
    g = [c for c in f.chunks if c[0] == 'CGameplayLightComponent']
    if not g:
        return 'plain'
    pr = chunk_props(f, d, g[0][2], g[0][3])
    flag = lambda n, default: (pr[n][1][0] != 0) if n in pr and pr[n][0] == 'Bool' and pr[n][1] else default  # noqa: E731
    if flag('isCityLight', False):
        return 'night'
    return 'always' if flag('isLightOn', True) else 'off'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--work', required=True)
    ap.add_argument('--build-json', required=True, help="w3build.py's record (its box and origin)")
    ap.add_argument('--out', required=True)
    ap.add_argument('--map', default='w3_city')
    ap.add_argument('--index-cache', default='<levelport root>/levelport/w3/bundle_index.pkl')
    a = ap.parse_args()

    build = json.load(open(a.build_json, encoding='utf-8'))
    origin = build['origin']
    x0, y0, x1, y1 = build['box']
    upm = levelport.UNITS_PER_METRE
    pl = json.load(open(os.path.join(a.work, 'placements.json'), encoding='utf-8'))
    idx = w3bundle.index(cache=a.index_cache)
    low = {k.lower(): k for k in idx}
    classes, per_template, lines = {}, collections.Counter(), []
    counts = collections.Counter()
    for L in pl['lights']:
        M = L['matrix']
        p = [M[0][3], M[1][3], M[2][3]]
        if not (x0 <= p[0] <= x1 and y0 <= p[1] <= y1) or not L['props'].get('witcher_light_enabled', True):
            continue   # (w3scene.py's lights: in the box and enabled)
        path = L['props'].get('witcher_entity_path') or ''
        if path not in classes:
            classes[path] = template_class(idx, low, path) if path else 'plain'
        c = classes[path]
        counts[c] += 1
        if c in ('night', 'day'):
            per_template[(c, path.split('\\')[-1])] += 1
            q = [upm * (p[k] - origin[k]) for k in range(3)]
            lines.append('%s %.2f %.2f %.2f' % (c, q[0], q[1], q[2]))
    os.makedirs(os.path.join(a.out, 'maps'), exist_ok=True)
    xlt = os.path.join(a.out, 'maps', a.map + '.xlt')
    with open(xlt, 'w', encoding='ascii', newline='\n') as f:
        f.write('xlt 1\n# the city light manager\'s lamps (isCityLight): lit at night only; *_daylight window lights by '
                'day only (tools/levelport/w3lighttime.py)\n')
        f.write('\n'.join(lines) + '\n')
    rec = {'counts': dict(counts), 'templates': {'%s %s' % k: n for k, n in per_template.most_common()}}
    with open(os.path.join(a.out, 'light_time.json'), 'w') as f:
        json.dump(rec, f, indent=1)
    print('W3LIGHTTIME', xlt, dict(counts))


if __name__ == '__main__':
    main()
