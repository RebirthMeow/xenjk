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
"""cp77_ads.py -- which CP2077 ad each screen shows (the ads the game shows are CP2077's own set,
not a stand-in pool).

CP2077 does not draw one city-wide pool. Every ad screen names a TweakDB group (the district's: Adverts.Kabuki,
Adverts.LittleChina...) and may pin one ad (adOverrideTDBID: Adverts.Mr_Whitey_Animated...); the game shows the pinned
ad, else one of the group's, in the definition of the screen's format (Format_16x9, Format_3x4, Format_3_3x1...): an
item of the ad's .inkwidget library. Two kinds of screens carry these:
  - worldAdvertisementNode: a placed screen mesh with adGroupTDBID, adOverrideTDBID and format on the node;
  - ad devices (worldEntityNode / worldDeviceNode): an AdvertisementWidgetComponent in the node's instance data or its
    template, the format left to the screen mesh (here: read from the template's and the mesh's names).

  python -I cp77_ads.py index [--area night_city_b]        work/ad_index.json: every ad screen of the scanned sectors
  python -I cp77_ads.py stats [--area night_city_b]

The TweakDB side (groups -> ads, ad -> library items per format) is an ads.json, the rendered items its
rendered/manifest.json, both made by cp77_ads_fetch.py (its dump step runs cp77_ads_dump.ps1; WolvenKit
10.0.0-beta.1 or later on the installed game's tweakdb.bin, CP2077 2.5 or later). Converted levels are for local play only."""
import argparse
import collections
import hashlib
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import paths  # noqa: E402

# the compositor's folder (ads.json, rendered/): CP77_ADS_DIR, else beside the work folders under the level port
# (cp2gltf.work_dir's root), resolved so a junction gives the same path strings as before (the material cache keys
# hold them)
ADS_DIR = Path(os.environ.get('CP77_ADS_DIR') or (paths.LEVELPORT / 'look' / 'ads-20261105')).resolve()
FORMAT_RE = [(re.compile(p, re.I), f) for p, f in (
    # (templates write the shape with x or _: digital_billboard_21_9, _10_33, _33_10, _4_3)
    (r'3[._]3x1|33[x_]10', 'Format_3_3x1'), (r'1x3[._]3|10[x_]33', 'Format_1x3_3'), (r'16[x_]9', 'Format_16x9'),
    (r'9[x_]16', 'Format_9x16'), (r'21[x_]9', 'Format_21x9'), (r'9[x_]21', 'Format_9x21'), (r'4[x_]3', 'Format_4x3'),
    (r'3[x_]4', 'Format_3x4'), (r'1[x_]1(?!\d)', 'Format_1x1'), (r'3x1', 'Format_3x1'), (r'1x3', 'Format_1x3'))]


def cv(v):
    return v.get('$value') if isinstance(v, dict) and '$value' in v else v


def tdb(v):
    """a TweakDBID field: its name, or None for 0 / empty"""
    v = cv(v)
    return None if v in (None, '', '0', 0) else str(v)


def format_of(*names):
    for n in names:
        for rx, f in FORMAT_RE:
            if n and rx.search(n):
                return f
    return None


def template_ad(work, tpl, cache):
    """(group, override) of a template's AdvertisementWidgetComponent, or None"""
    if tpl in cache:
        return cache[tpl]
    import cp2gltf          # (here, not at the top: cp2gltf imports this module)
    got = None
    # the template's own cache file (by its whole depot path: one file name lives in several folders), its
    # components as the converter reads them (the compiled data first), then the component list
    f = work / 'ents' / (cp2gltf.cache_name(tpl.replace('/', cp2gltf.SL)) + '.json')
    try:
        rc = json.load(open(f, encoding='utf-8'))['Data']['RootChunk']
    except (OSError, ValueError, KeyError):
        rc = None
    for c in (list(cp2gltf.template_chunks(rc)) + list(rc.get('components') or [])) if rc else []:
        if c.get('$type') == 'AdvertisementWidgetComponent':
            got = (tdb(c.get('adGroupTDBID')), tdb(c.get('adOverrideTDBID')))
            break
    cache[tpl] = got
    return got


def index(work):
    """every ad screen of the scanned sectors: 'sector|name' -> [entry, ...], one per shown node of that name (a
    sector's screens often share one debug name: keyed by it alone, the last node's group, pinned ad and format stood
    for every one), each entry its group, override, format, kind, template, node (its index in the
    sector's nodes) and pos (the node's shown records' positions, CP metres): AdMap.entry's ways to match a placement
    to its node"""
    import cp2gltf          # (here, not at the top: cp2gltf imports this module)
    out, tcache = {}, {}

    def put(key, e, i):
        e['node'], e['pos'] = i, shown[i]
        out.setdefault(key, []).append(e)
    for f in sorted((work / 'json').glob('*.streamingsector.json')):
        s = f.read_text(encoding='utf-8')
        if 'worldAdvertisementNode' not in s and 'AdvertisementWidgetComponent' not in s and 'advertising' not in s:
            continue
        sector = f.name[:-len('.streamingsector.json')]
        rc = json.loads(s)['Data']['RootChunk']
        shown = {}                      # variant-off ranges stay out
        for r in cp2gltf.shown_records(work, sector, rc):
            q = r.get('Position') or {}
            shown.setdefault(r['NodeIndex'], []).append([round(float(q.get(k, 0.0)), 2) for k in 'XYZ'])
        for i, n in enumerate(rc['nodes']):
            if i not in shown:
                continue
            nd = n['Data']
            t = nd.get('$type')
            name = cv(nd.get('debugName'))
            if t == 'worldAdvertisementNode':
                mesh = cv((nd.get('mesh') or {}).get('DepotPath')) or ''
                put('%s|%s' % (sector, name), {
                    'kind': 'node', 'group': tdb(nd.get('adGroupTDBID')), 'override': tdb(nd.get('adOverrideTDBID')),
                    'format': cv(nd.get('format')) or format_of(mesh, name), 'mesh': mesh}, i)
            elif t in ('worldEntityNode', 'worldDeviceNode'):
                tpl = cv((nd.get('entityTemplate') or {}).get('DepotPath')) or ''
                inst = (((nd.get('instanceData') or {}).get('Data') or {}).get('buffer') or {}).get('Data') or {}
                got = None
                for c in inst.get('Chunks') or []:
                    if c.get('$type') == 'AdvertisementWidgetComponent':
                        got = (tdb(c.get('adGroupTDBID')), tdb(c.get('adOverrideTDBID')))
                tg = template_ad(work, tpl, tcache) if 'advertis' in tpl or got else None
                if got is None and tg is None:
                    continue
                g = (got or (None, None))
                put('%s|%s' % (sector, name), {
                    'kind': 'device', 'group': g[0] or (tg or (None, None))[0], 'override': g[1] or (tg or (None, None))[1],
                    'format': format_of(tpl, name), 'template': tpl}, i)
    return out


def meta(e):
    """what of an index entry decides its ad (not where its node is)"""
    return (e.get('group'), e.get('override'), e.get('format'))


def choose(entry, ads, groups, key):
    """the ad a screen shows: its pinned ad, else one of its group's (by key, the same every build; the game picks at
    run time), following IncludedGroups"""
    o = entry.get('override')
    if o and o in ads:
        return o
    seen, pool, todo = set(), [], [entry.get('group') or 'Adverts.AllAdvertisements']
    while todo:
        g = todo.pop()
        if g in seen or g not in groups:
            continue
        seen.add(g)
        pool += [a for a in groups[g].get('Advertisements') or [] if a in ads]
        todo += groups[g].get('IncludedGroups') or []
    if not pool:
        return None
    return pool[int(hashlib.sha1(key.encode()).hexdigest()[:8], 16) % len(pool)]


FORMAT_NEAREST = {'Format_1x3': 'Format_1x3_3', 'Format_3x1': 'Format_3_3x1'}   # (no ad has these: the nearest shape)


class AdMap:
    """The scene build's side: a placement -> the rendered image of the ad its screen shows, or None (the stand-in pool
    then, as before). Screens are found by the scan's names: a worldAdvertisementNode by its own (sector|name), an ad
    device's mesh component ('<entity>/<component>', type entity) by its entity's. The ad is the screen's pinned one,
    else one of its group's that has its format, chosen by the placement's sector|name (the same every build). An
    animated ad's static render is taken where the compositor made one (its _static_equivalents)."""

    def __init__(self, work, ads_dir=ADS_DIR):
        self.ok = False
        f = work / 'ad_index.json'
        man_f = ads_dir / 'rendered' / 'manifest.json'
        if not f.exists() or not man_f.exists():
            return
        self.idx = json.load(open(f, encoding='utf-8'))
        tdb_ = json.load(open(ads_dir / 'ads.json', encoding='utf-8-sig'))
        self.groups = tdb_['groups']
        man = json.load(open(man_f, encoding='utf-8'))
        # each ad record's own render: an animated layout can carry other art than its static twin (the compositor's
        # check against the game: it shows naranjita_animated's three-part title)
        self.art = {}
        for ad, fmts in man.items():
            if ad.startswith('_'):
                continue
            self.art[ad] = {fm: str(ads_dir / p) for fm, p in fmts.items()}
        self.dir = ads_dir
        self.ok = True

    def entry(self, p):
        if p.get('type') == 'worldAdvertisementNode':
            es = self.idx.get('%s|%s' % (p.get('sector'), p.get('name')))
        elif p.get('type') == 'entity' and '/' in (p.get('name') or ''):
            es = self.idx.get('%s|%s' % (p.get('sector'), p['name'].rsplit('/', 1)[0]))
        else:
            return None
        if isinstance(es, dict) or not es:
            return es or None           # (an older index: one entry per name)
        if len(es) == 1 or all(meta(e) == meta(es[0]) for e in es[1:]):
            return es[0]
        if p.get('node') is not None:   # (a placement that names its node: exact)
            got = [e for e in es if e.get('node') == p['node']]
            if got:
                return got[0]
        # same-named nodes that differ: the one whose shown record is nearest the placement. A screen node's record is
        # the placement's own place (Watson: every one within 5 cm); a device's mesh component sits off its entity's
        # origin, so for close same-named devices the nearest can be a neighbour (Watson tag 10h: about half of the
        # 3,916 such component placements have no clear nearest)
        m = p.get('m') or [0.0] * 12
        at = (float(m[3]), float(m[7]), float(m[11]))
        return min(es, key=lambda e: min((sum((u - v) ** 2 for u, v in zip(q, at)) for q in e.get('pos') or ()),
                                         default=float('inf')))

    def image(self, p):
        """the png path a placement's screen shows, or None"""
        e = self.entry(p) if self.ok else None
        if not e:
            return None
        fm = FORMAT_NEAREST.get(e.get('format'), e.get('format'))
        if not fm:
            return None
        have = {a: v for a, v in self.art.items() if fm in v}
        # chosen per screen: many screens share one debug name (Tom's three stacked billboard_frame_3_3x1_e), so the
        # key holds its place too (the renderer's night-17 compare: three panels, one ad)
        m = p.get('m') or [0.0] * 12
        ad = choose(e, have, self.groups, '%s|%s|%.0f|%.0f|%.0f' % (p.get('sector'), p.get('name'), m[3], m[7], m[11]))
        if ad and not ad.endswith('_Animated') and ad + '_Animated' in have:
            # the game shows an ad's animated record where one exists (pinned Adverts.Naranjita, drawn as
            # Naranjita_Animated's layout); one reference so far, so the rest are unchecked
            ad = ad + '_Animated'
        return have[ad][fm] if ad else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=('index', 'stats'))
    ap.add_argument('--area', default='night_city_b')
    a = ap.parse_args()
    import cp2gltf          # (here, not at the top: cp2gltf imports this module)
    work = cp2gltf.work_dir(a.area)
    f = work / 'ad_index.json'
    if a.cmd == 'index':
        idx = index(work)
        text = json.dumps(idx, indent=0)
        if f.exists() and f.read_text(encoding='utf-8') == text:
            print('%s: unchanged, not rewritten (the scene parts\' keys stat it)' % f.name)
        else:
            tmp = f.with_name(f.name + '.tmp')      # (renamed when whole: the scene parts' keys stat it)
            tmp.write_text(text, encoding='utf-8')
            tmp.replace(f)
        print('ad_index: %d screens -> %s' % (len(idx), f))
    idx = json.load(open(f, encoding='utf-8'))
    idx = {(k, j): e for k, v in idx.items() for j, e in enumerate([v] if isinstance(v, dict) else v)}
    c = collections.Counter((e['kind'], bool(e.get('override')), e.get('format') is not None) for e in idx.values())
    print('kind / pinned / format known:', sorted(c.items()))
    print('formats', collections.Counter(e.get('format') for e in idx.values()).most_common(12))
    print('groups', collections.Counter(e.get('group') for e in idx.values()).most_common(12))


if __name__ == '__main__':
    main()
