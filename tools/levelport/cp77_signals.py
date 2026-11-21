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
"""cp77_signals.py -- the traffic signals' groups and light programs: signal_groups.json, which the scene stage
(cp2gltf SceneSignals) and cp77_tiles read from the work folder.

  python -I cp77_signals.py --area kabuki [--out <file>] [--raw <folder>]

Reads, in the area's work folder (cp2gltf.work_dir):
  json/*.streamingsector.json   the fetched sectors (WolvenKit's serialised form, as the fetch stage writes them)
  placements.json               the scan's entities (only to mark which signals the port places: in_port_placements)
Writes <work>/signal_groups.json (or --out); with --raw, also the two joins' inputs, signals_raw.json and
intersections_raw.json, into that folder.

How the data joins:
  - every placed crossing light / car traffic light (a worldEntityNode of crossing_light_1, traffic_light_1 or
    traffic_light_2) carries a worldTrafficLightListenerComponent override in its instance data: intersectionRef (a
    node path) and groupIdx (its light group);
  - the intersectionRef path, FNV-1a64 hashed, is worldTrafficCompiledNode_App1.unk3 of every lane of one
    compiledTraffic_Intersection node;
  - a lane's App2.unk4 is the light group index and App2.unk5 that group's program: (worldTrafficLightColor, seconds),
    GREEN=0, RED=1, YELLOW=2 (WolvenKit's enum). Every group of one intersection has the same program total (its cycle)
    and all share t=0; when the cycle starts against world time is the engine's (not in the data).
  - crossings inside a worldAmbientAreaNode {or_crossing_glitch_heavy} play the glitched variant of the crossing
    sounds (the area's audio override); the sound names are the crossing light template's
    CrossingLightControllerPS.crossingLightSFXSetup (greenLightSFX, redLightSFX), which no sector overrides.
"""
import argparse
import base64
import glob
import json
import math
import os
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import cp2gltf  # noqa: E402

SIGNAL_TEMPLATES = ('crossing_light_1.ent', 'traffic_light_1.ent', 'traffic_light_2.ent')
COL = {0: 'GREEN', 1: 'RED', 2: 'YELLOW'}              # worldTrafficLightColor
GLITCH_AREA = '{or_crossing_glitch_heavy}'             # worldAmbientAreaNode: the crossings' glitched sounds
# CrossingLightControllerPS.crossingLightSFXSetup of crossing_light_1.ent (the template; no instance overrides)
SFX_GREEN = 'dev_city_crossing_light_watson_01_walk_f_01'
SFX_RED = 'dev_city_crossing_light_watson_01_dont_walk_f_01'
# text a sector must hold to have anything this tool reads (the rest are skipped before parsing)
MARKERS = tuple(t.split('.')[0] for t in SIGNAL_TEMPLATES) + ('compiledTraffic_Intersection', GLITCH_AREA)


def fnv1a64(s):
    h = 0xcbf29ce484222325
    for b in s.encode('utf-8'):
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def read_sectors(js):
    """One pass over the sectors -> (signals_raw, intersections_raw, glitch areas)."""
    signals, ints, glitch = [], [], []
    for f in sorted(glob.glob(os.path.join(js, '*.json'))):
        text = Path(f).read_text(encoding='utf-8')
        if not any(m in text for m in MARKERS):
            continue
        rc = json.loads(text)['Data']['RootChunk']
        sector = os.path.basename(f).split('.')[0]
        recs = {}
        for r in rc['nodeData']['Data']:
            recs.setdefault(r['NodeIndex'], []).append(r)
        for i, nd in enumerate(rc['nodes']):
            d = nd.get('Data') or nd
            name = d.get('debugName', {}).get('$value', '')
            if d.get('$type') == 'worldEntityNode':
                signals += sector_signal(sector, i, d, recs.get(i, []))
            if name.startswith('compiledTraffic_Intersection'):
                ints.append(sector_intersection(sector, i, name, d, recs.get(i, [])))
            if d.get('$type') == 'worldAmbientAreaNode' and name == GLITCH_AREA:
                glitch += [glitch_box(r, d) for r in recs.get(i, [])]
    return signals, ints, glitch


def sector_signal(sector, i, d, recs):
    """A placed signal's records: its listener override (intersectionRef, groupIdx) and each nodeData transform."""
    base = d['entityTemplate']['DepotPath']['$value'].split(chr(92))[-1]
    if base not in SIGNAL_TEMPLATES:
        return []
    ref, grp, sfx = None, None, None
    idata = d.get('instanceData') or {}
    try:
        chunks = idata['Data']['buffer']['Data']['Chunks']
    except (KeyError, TypeError):
        chunks = []
    for c in chunks:
        if c.get('$type') == 'worldTrafficLightListenerComponent':
            ref, grp = c['intersectionRef']['$value'], c['groupIdx']
        s = json.dumps(c)
        if 'crossingLightSFXSetup' in s:
            sfx = s[:200]
    out = []
    for r in recs:
        p, q = r['Position'], r['Orientation']
        out.append({'sector': sector, 'node_index': i,
                    'debug_name': d.get('debugName', {}).get('$value'), 'template': base,
                    'appearance': d.get('appearanceName', {}).get('$value'),
                    'kind': 'crossing' if base.startswith('crossing') else 'car',
                    'pos_cp_m': [round(p['X'], 3), round(p['Y'], 3), round(p['Z'], 3)],
                    'quat_ijkr': [q['i'], q['j'], q['k'], q['r']],
                    'intersection_ref': ref, 'group_idx': grp,
                    'intersection_hash': str(fnv1a64(ref)) if ref else None, 'sfx_override': sfx})
    return out


def sector_intersection(sector, i, name, d, recs):
    """A compiledTraffic_Intersection node's lanes, each with its light programs (App1/App2/App3) in compact form:
    progs are (App2.unk1, unk2, unk3, unk4 = group, unk5 = [(colour, seconds)])."""
    pos = [[round(r['Position']['X'], 2), round(r['Position']['Y'], 2), round(r['Position']['Z'], 2)] for r in recs]
    lanes = []
    for a in d.get('unk1') or []:
        progs = [[p['unk1'], p['unk2'], p['unk3'], p['unk4'], [[q['unk1'], round(q['unk2'], 3)] for q in p['unk5']]]
                 for p in a['unk7']]
        lanes.append({'hash': a['unk3'], 'id': list(base64.b64decode(a['unk4'])), 'w': a['unk5'],
                      'len': round(a['unk6'], 2), 'progs': progs, 'workspots': len(a['unk8'])})
    return {'sector': sector, 'node': i, 'name': name, 'pos': pos, 'lanes': lanes}


def glitch_box(r, d):
    """An ambient area's box from its nodeData record: the outline is the unit square (+-1) scaled by Scale X, Y,
    rotated by the record's yaw, Scale.Z * the outline's height tall from Position.Z."""
    p, q, s = r['Position'], r['Orientation'], r['Scale']
    height = ((d.get('outline') or {}).get('Data') or {}).get('height', 2)
    return {'centre': (p['X'], p['Y'], p['Z']), 'yaw': 2 * math.atan2(q['k'], q['r']),
            'half': (s['X'], s['Y']), 'zmax': p['Z'] + height * s['Z']}


def in_glitch(p, g):
    dx, dy = p[0] - g['centre'][0], p[1] - g['centre'][1]
    c, s_ = math.cos(-g['yaw']), math.sin(-g['yaw'])
    lx, ly = dx * c - dy * s_, dx * s_ + dy * c
    return abs(lx) <= g['half'][0] and abs(ly) <= g['half'][1] and g['centre'][2] - 1 <= p[2] <= g['zmax']


def timeline(prog):
    t, out = 0.0, []
    for c, d in prog:
        out.append({'color': c, 'start_s': round(t, 3), 'end_s': round(t + d, 3)})
        t += d
    return out


def build_groups(area, sig, ints, glitch, entities):
    """signals_raw + intersections_raw -> the signal_groups.json document."""
    ported = [(e['m'][3], e['m'][7], e['m'][11]) for e in entities
              if 'crossing_light' in e['template'] or 'traffic_light_' in e['template']]
    by_hash = {}
    for o in ints:
        h = o['lanes'][0]['hash'] if o['lanes'] else None
        groups = {}
        for lane in o['lanes']:
            for p in lane['progs']:
                g = p[3]
                prog = [[COL[c], d] for c, d in p[4]]
                groups.setdefault(g, {'program': prog, 'lanes': 0, 'pedestrian_lanes': 0})
                if groups[g]['program'] != prog:
                    groups[g]['conflict'] = True
                groups[g]['lanes'] += 1
                if p[1] > 0.5:                    # App2.unk2 == 1.0 only on crosswalk (crowd) lanes
                    groups[g]['pedestrian_lanes'] += 1
        cyc = sorted({round(sum(d for _, d in g['program']), 2) for g in groups.values()})
        by_hash[h] = {'name': o['name'], 'sector': o['sector'], 'node_index': o['node'], 'pos_cp_m': o['pos'][0],
                      'cycle_s': cyc, 'groups': {str(k): v for k, v in sorted(groups.items())}}

    seen, signals = set(), []
    for s in sig:
        key = (s['template'], tuple(round(v, 2) for v in s['pos_cp_m']))
        if key in seen:
            continue
        seen.add(key)
        I = by_hash.get(s['intersection_hash'])
        g = I['groups'].get(str(s['group_idx'])) if I else None
        p = s['pos_cp_m']
        in_port = any(abs(p[0] - x) < 0.02 and abs(p[1] - y) < 0.02 and abs(p[2] - z) < 0.02 for x, y, z in ported)
        signals.append({
            'sector': s['sector'], 'node_index': s['node_index'], 'debug_name': s['debug_name'],
            'template': s['template'], 'kind': s['kind'], 'pos_cp_m': p, 'quat_ijkr': s['quat_ijkr'],
            'in_port_placements': in_port,
            'intersection_ref': s['intersection_ref'],
            'intersection_node': I['name'] if I else None,
            'group_id': ('%s#%d' % (I['name'], s['group_idx'])) if I else None,
            'group_idx': s['group_idx'],
            'cycle_s': I['cycle_s'][0] if I and len(I['cycle_s']) == 1 else (I['cycle_s'] if I else None),
            'program': g['program'] if g else None,
            'timeline': timeline(g['program']) if g else None,
            'phase_offset_s': 0.0 if g else None,
            'note': None if g else ('no listener override: never receives TrafficLightChangeEvent' if not I
                                    else 'group index not present in intersection lanes'),
        })

    # the crossings play greenLightSFX on GREEN and redLightSFX on RED; the glitched set inside a glitch area
    for s in signals:
        if s['kind'] != 'crossing':
            s['audio'] = None
            continue
        g = any(in_glitch(s['pos_cp_m'], b) for b in glitch)
        sfx = '_glitch_heavy' if g else ''
        s['audio'] = {'on_green': SFX_GREEN + sfx, 'on_red': SFX_RED + sfx, 'glitch_heavy_area': g}

    return {
        'about': '%s placed crossing (pedestrian) and car traffic signals with their intersection light group '
                 'and the group program from the compiled traffic data. Colors are worldTrafficLightColor sent to '
                 'the entity: car GREEN/YELLOW/RED; crossing GREEN=WALK, YELLOW=flashing DON\'T WALK (blink_01, '
                 '1 s), RED=DON\'T WALK. All groups of one intersection share t=0 of its cycle; the cycle start '
                 'against world time is native (not in data). Positions are CP world metres (nodeData Position).'
                 % area.capitalize(),
        'sources': ['json/*.streamingsector.json', 'placements.json'],
        'intersections': {v['name']: v for h, v in by_hash.items()
                          if any(s['intersection_hash'] == h for s in sig)},
        'signals': signals,
    }


def report(doc):
    signals = doc['signals']
    print(len(signals), Counter(s['kind'] for s in signals), 'in port', sum(s['in_port_placements'] for s in signals))
    print('crossings in a glitch-heavy area',
          [(s['sector'], s['node_index']) for s in signals if s.get('audio') and s['audio']['glitch_heavy_area']])
    print('no program', [(s['sector'], s['node_index'], s['kind'], s['note']) for s in signals if not s['program']])
    for n, I in doc['intersections'].items():
        used = Counter((s['kind'], s['group_idx']) for s in signals if s['intersection_node'] == n)
        print(n, I['cycle_s'], dict(used))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--area', required=True, choices=sorted(cp2gltf.AREAS))
    ap.add_argument('--out', help='the output file (default <work>/signal_groups.json)')
    ap.add_argument('--raw', help='a folder for signals_raw.json and intersections_raw.json (the joins\' inputs)')
    a = ap.parse_args()
    work = cp2gltf.work_dir(a.area)
    js = work / 'json'
    if not js.is_dir():
        sys.exit('%s: no sectors (the fetch stage writes them)' % js)
    if not (work / 'placements.json').exists():
        sys.exit('%s: no placements.json (the scan stage writes it)' % work)
    sig, ints, glitch = read_sectors(str(js))
    with open(work / 'placements.json', encoding='utf-8') as f:
        entities = json.load(f)['entities']
    doc = build_groups(a.area, sig, ints, glitch, entities)
    if a.raw:
        raw = Path(a.raw)
        raw.mkdir(parents=True, exist_ok=True)
        cp2gltf.write_json(raw / 'signals_raw.json', sig, indent=1)
        cp2gltf.write_json(raw / 'intersections_raw.json', ints, indent=1)
    out = Path(a.out) if a.out else work / 'signal_groups.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    cp2gltf.write_json(out, doc, indent=1)
    report(doc)
    print('wrote', out)


if __name__ == '__main__':
    main()
