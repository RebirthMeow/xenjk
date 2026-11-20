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
"""light_time.py -- which of an area's lights CP2077 shows only at night or only by day (read from CP2077's device
network and environment).

  python -I light_time.py <area>          -> <work>/light_time.json

CP2077 switches street and wall lamps by the clock through fuses, not per light: a lamp entity's device
(ElectricLightControllerPS) hangs off a FuseControllerPS in the city's device graph
(base\\worlds\\03_night_city\\_compiled\\default\\03_night_city.devices, serialised into <work>/devices/), and the fuse's
timeTableSetup lists ON/OFF game-clock times (the fuse_box template's own: ON 21:00, OFF 04:20; a sector's instance
data may replace it). A device's nodePosition is its entity's placement (within 0.3 m). A lamp whose fuse is off at
13:00 and on at 01:00 is night only. Static light nodes do not switch, except by their environment colour group: only
ECG_Group0 fades (off from 19:43 to 04:50: day only) and ECG_Sky follows the sky light (x1.3 by day, x0.04 at night:
the portal fills, day only here). Everything else is lit at all hours: static ECG_Default nodes, ads, entities without
a fuse, traffic and crossing signals (they cycle). A fuse whose table never turns off (wlc_07: ON 19:00 only) leaves
its lamps lit at all hours, as the data says; one that is never on (every entry OFF) leaves them dark at all hours:
class 'never' (these once fell to 'always' and were lit day and night).

The output lists the night-only, day-only and never-lit lights by (sector, name, x, y) to 0.1 m (keys 'night', 'day'
and 'never'), the key scene_light_records matches on (placements.json's lights and entity_lights' as the merge reads
them); the scene's light type then carries bit 20 (night only) or bit 21 (day only), which the renderer's xenv drops
in the other time, and a 'never' light is left out of the scene (cp2gltf.light_times). Reads only the work folder;
writes <work>/light_time.json, or --out. Converted levels are for local play only."""
import argparse
import collections
import json
import math
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import cp2gltf  # noqa: E402

TEMPLATE_TT = [('ON', 21, 0), ('OFF', 4, 20)]       # the fuse_box template's timetable
TT_RE = re.compile(r'"timeTableSetup": \{"HandleId": "\d+", "Data": \{"\$type": "DeviceTimeTableManager", '
                   r'"timeTable": (\[.*?\])\}')


def lit_at(tt, h):
    ev = sorted((hh + mm / 60.0, st) for st, hh, mm in tt)
    st = ev[-1][1]                  # (before the first entry of the day: the last one, from the day before)
    for t, s in ev:
        if t <= h:
            st = s
    return st == 'ON'


def time_class(tt):
    """'night', 'day', 'never' (off at every hour) or 'always' (also for a table that is never off, or a mixed one)."""
    on = [lit_at(tt, h + 0.5) for h in range(24)]
    if not any(on):
        return 'never'
    if on[1] and not on[13]:
        return 'night'
    if on[13] and not on[1]:
        return 'day'
    return 'always'


def key(L):
    return '%s|%s|%.1f|%.1f' % (L.get('sector'), L.get('name'), L['pos'][0], L['pos'][1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('area', choices=sorted(cp2gltf.AREAS))
    ap.add_argument('--out', type=Path, help='the table (default <work>/light_time.json)')
    a = ap.parse_args()
    A = cp2gltf.AREAS[a.area]
    cp2gltf.use_area(a.area)
    centre, radius = A['centre'], A['radius']
    work = cp2gltf.work_dir(a.area)
    dev = json.load(open(work / 'devices' / '03_night_city.devices.json', encoding='utf-8'))
    dev = dev['Data']['RootChunk']['data']['Data']['unk1']
    by_hash = {n['hash']: n for n in dev}
    cell = {}
    for n in dev:
        p = n['nodePosition']
        cell.setdefault((round(p['X'] * 4), round(p['Y'] * 4), round(p['Z'] * 4)), []).append(n)

    def device_at(x, y, z):
        k = (round(x * 4), round(y * 4), round(z * 4))
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for n in cell.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                        p = n['nodePosition']
                        if math.dist((x, y, z), (p['X'], p['Y'], p['Z'])) < 0.3:
                            return n
        return None

    # the sectors' fuse nodes (their timetables) and static lights of a non-default environment colour group
    fuses, ecg = [], {}
    files = sorted((work / 'json').glob('*.streamingsector.json'))
    for f in files:
        text = f.read_text(encoding='utf-8')
        has_fuse, has_ecg = 'fuse_box' in text, ('"ECG_Sky"' in text or '"ECG_Group' in text)
        if not (has_fuse or has_ecg):
            continue
        rc = json.loads(text)['Data']['RootChunk']
        sector = f.name.split('.')[0]
        pos = collections.defaultdict(list)
        for e in rc['nodeData']['Data']:
            pos[e['NodeIndex']].append(e['Position'])
        for i, nd in enumerate(rc['nodes']):
            x = nd['Data']
            if has_fuse and 'fuse_box' in str(((x.get('entityTemplate') or {}).get('DepotPath') or {}).get('$value')):
                tts = [json.loads(g) for g in TT_RE.findall(json.dumps(x.get('instanceData')))]
                tts = [g for g in tts if g]
                tt = [(e['state'], e['time']['hours'], e['time']['minutes']) for e in tts[-1]] if tts else None
                for p in pos[i]:
                    fuses.append(((p['X'], p['Y'], p['Z']), cp2gltf.cname(x.get('debugName')), tt))
            elif (has_ecg and x.get('$type') == 'worldStaticLightNode'
                  and x.get('envColorGroup', 'ECG_Default') in ('ECG_Sky', 'ECG_Group0')):
                for p in pos[i]:
                    ecg['%s|%s|%.1f|%.1f' % (sector, cp2gltf.cname(x.get('debugName')), p['X'], p['Y'])] = \
                        x['envColorGroup']
    fuse_class = {}

    def fuse_of(n):
        """A device's fuse's class, or None (no fuse among its parents)."""
        fus = [h for h in n['parents'] if h in by_hash and by_hash[h]['className']['$value'] == 'FuseControllerPS']
        if not fus:
            return None
        h = fus[0]
        if h not in fuse_class:
            p = by_hash[h]['nodePosition']
            near = [(math.dist((p['X'], p['Y'], p['Z']), q), name, tt) for q, name, tt in fuses]
            near = min(near, key=lambda t: (t[0], t[1]), default=None)     # (never compares two timetables)
            tt = near[2] if near is not None and near[0] < 1.0 and near[2] else TEMPLATE_TT
            fuse_class[h] = (near[1] if near is not None and near[0] < 1.0 else '?', time_class(tt), tt)
        return fuse_class[h]

    P = json.load(open(work / 'placements.json', encoding='utf-8'))
    ents = collections.defaultdict(list)
    for e in P.get('entities', []):
        ents[(e['sector'], e['name'])].append(e)
    lights = [L for L in P.get('lights', []) if cp2gltf.in_area(L['pos'][0], L['pos'][1], centre, radius, L['pos'][2])]
    lights += cp2gltf.entity_lights(work, centre, radius, signals=True)
    out = {'night': [], 'day': [], 'never': []}
    stats = collections.Counter()
    for L in lights:
        k = key(L)
        kind = L.get('kind', 'static')
        if kind == 'static':
            g = ecg.get(k)
            if g:
                out['day'].append(k)
                stats['day: static ' + g] += 1
            else:
                stats['always: static'] += 1
            continue
        if kind == 'ad':
            stats['always: ad'] += 1
            continue
        cands = ents.get((L.get('sector'), str(L.get('name', '')).rsplit('/', 1)[0]), [])
        e = min(cands, key=lambda e: math.dist((e['m'][3], e['m'][7], e['m'][11]), L['pos']), default=None)
        n = device_at(e['m'][3], e['m'][7], e['m'][11]) if e is not None else None
        if n is None or n['className']['$value'] != 'ElectricLightControllerPS':
            stats['always: %s, no lamp device' % kind] += 1
            continue
        f = fuse_of(n)
        if f is None:
            stats['always: lamp without a fuse'] += 1
            continue
        if f[1] in ('night', 'day', 'never'):
            out[f[1]].append(k)
        stats['%s: fuse %s' % (f[1], f[0])] += 1
    out['stats'] = dict(stats)
    out['fuses'] = {str(h): [v[0], v[1], v[2]] for h, v in fuse_class.items()}
    dst = a.out or work / 'light_time.json'
    dst.write_text(json.dumps(out, indent=0), encoding='utf-8')
    print('light_time: %d lights, %d night only, %d day only, %d never lit -> %s' % (
        len(lights), len(out['night']), len(out['day']), len(out['never']), dst))
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1]):
        print('  %7d  %s' % (v, k))


if __name__ == '__main__':
    main()
