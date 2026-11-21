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
"""cp77_sector25.py -- a CP2077 2.5 streaming sector, as WolvenKit 10 serialises it, rewritten in the 2.31 layout the
level port's readers were written against (the scan, decals, clutter, signals, lights). cp2gltf's sectors stage runs it
on each sector it serialises; a sector already in the 2.31 layout is left as it is.

What 2.5 changed, and how each part comes back:
  - nodeData records are worldCompiledNodeInstanceSetupInfo: nodeIdx (a string), instanceIndex, transform
    {position, orientation}, scale, streamingDistance, streamingRefPoint, secondaryRefPointDistance. They become
    {NodeIndex, Position, Orientation, Scale, UkFloat1 (the streaming distance), Pivot, MaxStreamingDistance}, in the
    same order (variantIndices still index them).
  - Static meshes, proxies, decals, bended roads, cables and acoustic sectors are "cooked": one node holds a list of
    inner nodes in its buffer, and a record's instanceIndex names its own. Each (node, inner node) a record uses
    becomes a node of the 2.31 type with the inner node's fields over the node's own (debugName, sourcePrefabHash, ...):
    forcedAutoHideDistance is also forceAutoHideDistance (the 2.31 mesh and proxy name); a decal's flags list becomes
    its booleans and diffuseColorScaleIndex its colour; a bended mesh's deformation frames and box come back from the
    node's shared arrays.
  - An instanced mesh node holds several meshes (inner nodes), each a range of one transform buffer of fixed-point
    positions (bits / 2^17 m). Each (node, inner node) becomes a 2.31 worldInstancedMeshNode with its own transforms
    inline ({rotation, translation, scale}) and its hide distance in the 2.31 buffer's per-group lists.
  - An instanced destructible mesh's local transforms are an inline instanceTransforms list (2.31: the shared
    cookedInstanceTransforms buffer, which 2.5 drops): put back as cookedInstanceTransforms.
  - An entity's or device's instance light whose colour the instance leaves unset (the device colours it at run time)
    reads white (255, 255, 255, 255) from WolvenKit 10 where WolvenKit 9 read black (0, 0, 0, 0), which the readers
    take as the placeholder: put back as black. Kabuki: 526 placeholders; 11 lights that are white in 2.31 as well
    turn black too (13 of 831 instance lights white in 2.5 are real; no field tells them apart).
  - A mesh node's advertisement lights (lightsData, worldAdvertisementLightData), whose colour the ad's video sets at
    run time: white in 2.5 where 2.31 reads black, the placeholder cp2gltf colours from the billboard (AD_LIGHT_COLOUR,
    ad_lights_colour): put back as black. Watson, 40 sectors sampled: 197 black in 2.31 and white in 2.5, the one real
    colour unchanged.
  - The same white for an unset light colour in the entity templates and appearances the entity fetch serialises
    (convert_resource_file, run by cp2gltf's fetch --ents).
  - Cooked collision and foliage destruction keep their 2.5 types (no reader uses them: collision is made from the
    render meshes).

The rewritten RootChunk carries xenjkLegacyFrom: "2.5" (a second run leaves it)."""
import json
import re
import sys
from pathlib import Path

VERSION = 4                     # of the rewrite: a file rewritten by an older one gets the node fixes it lacks
FIXED = 1.0 / 131072.0          # WorldPosition FixedPoint bits per metre
NONE = 4294967295               # a record's instanceIndex with no inner node

# cooked node type -> the 2.31 type its inner nodes are
COOKED = {
    'worldCookedStaticMeshNode': 'worldStaticMeshNode',
    'worldCookedGenericProxyMeshNode': 'worldGenericProxyMeshNode',
    'worldCookedStaticDecalNode': 'worldStaticDecalNode',
    'worldCookedBendedMeshNode': 'worldBendedMeshNode',
    'worldCookedCableMeshNode': 'worldCableMeshNode',
    'worldCookedAcousticSectorNode': 'worldAcousticSectorNode',
}
MESH_TYPES = ('worldStaticMeshNode', 'worldGenericProxyMeshNode', 'worldBendedMeshNode', 'worldCableMeshNode',
              'worldInstancedMeshNode')
DECAL_FLAGS = {'HorizontalFlip': 'horizontalFlip', 'VerticalFlip': 'verticalFlip',
               'EnableNormalTreshold': 'enableNormalTreshold', 'IsStretchingEnabled': 'isStretchingEnabled',
               'ShouldCollectWithRayTracing': 'shouldCollectWithRayTracing'}


def is_25(rc):
    recs = (rc.get('nodeData') or {}).get('Data') or []
    return bool(recs) and 'nodeIdx' in recs[0] and 'NodeIndex' not in recs[0]


def _vec(v, w=None):
    v = v or {}
    out = {k: float(v.get(k, 0.0)) for k in 'XYZ'}
    if w is not None:
        out['W'] = w
    return out


def _record(r, ni):
    t = r.get('transform') or {}
    return {'NodeIndex': ni, 'Position': _vec(t.get('position'), 0), 'Orientation': dict(t.get('orientation') or {'r': 1}),
            'Scale': _vec(r.get('scale') or {'X': 1, 'Y': 1, 'Z': 1}), 'Pivot': _vec(r.get('streamingRefPoint')),
            'UkFloat1': float(r.get('streamingDistance') or 0.0),
            'MaxStreamingDistance': float(r.get('secondaryRefPointDistance') or 0.0),
            'CookedPrefabData': r.get('externalInplaceResourceHash'),
            'Id': str(((r.get('globalNodeId') or {}).get('hash')) or '0'),
            'QuestPrefabRefHash': r.get('patchedNode'), 'xenjk25': {'nodeIdx': r.get('nodeIdx'),
                                                                     'instanceIndex': r.get('instanceIndex')}}


def _outer(d, drop):
    return {k: v for k, v in d.items() if k not in drop and k != '$type'}


def _cooked(d, inner, buf):
    t = COOKED[d['$type']]
    n = _outer(d, ('buffer',))
    n.update({k: v for k, v in inner.items() if k != '$type'})
    n['$type'] = t
    if 'forcedAutoHideDistance' in inner and t in MESH_TYPES:
        n['forceAutoHideDistance'] = inner['forcedAutoHideDistance']
    if t == 'worldStaticDecalNode':
        on = {s.strip() for s in str(inner.get('flags') or '').split(',') if s.strip()}
        for flag, field in DECAL_FLAGS.items():
            n[field] = 1 if flag in on else 0
        cs = buf.get('DiffuseColorScales') or []
        k = int(inner.get('diffuseColorScaleIndex', -1) if inner.get('diffuseColorScaleIndex') is not None else -1)
        if 0 <= k < len(cs):
            n['diffuseColorScale'] = cs[k]
    if t in ('worldBendedMeshNode', 'worldCableMeshNode'):
        s, c = int(inner.get('deformationDataStartIndex') or 0), int(inner.get('deformationDataCount') or 0)
        n['deformationData'] = (buf.get('DeformationData') or [])[s:s + c]
        boxes = buf.get('DeformedBoxes') or []
        bi = int(inner.get('index') or 0)
        if 0 <= bi < len(boxes):
            n['deformedBox'] = boxes[bi]
    return n


def _fixed(p, k):
    v = (p or {}).get(k) or {}
    return float(v.get('Bits', 0)) * FIXED if isinstance(v, dict) else float(v or 0.0)


def _instanced(d, inner, rng, transforms):
    n = _outer(d, ('buffer', 'cookedWorldTransformsBuffer'))
    n.update({k: v for k, v in inner.items() if k != '$type'})
    n['$type'] = 'worldInstancedMeshNode'
    s, c = int(rng.get('transformIndex') or 0), int(rng.get('transformCount') or 0)
    out = []
    for t in transforms[s:s + c]:
        wt = t.get('transform') or {}
        pos = wt.get('Position') or {}
        out.append({'$type': 'worldNodeTransform', 'rotation': dict(wt.get('Orientation') or {'r': 1}),
                    'translation': {k.upper(): _fixed(pos, k) for k in 'xyz'}, 'scale': _vec(t.get('scale'))})
    n['worldTransformsBuffer'] = {'$type': 'worldRenderProxyTransformBuffer', 'startIndex': 0, 'numElements': len(out),
                                  'sharedDataBuffer': {'Data': {'buffer': {'Data': {'Transforms': out}}}}}
    hide = inner.get('forcedAutoHideDistance')
    n['forceAutoHideDistance'] = hide
    n['buffer'] = {'Unknown2': [hide or 0], 'Unknown3': [len(out)]}
    return n


def _destructible(d):
    """a worldInstancedDestructibleMeshNode with its 2.5 inline transforms as the 2.31 cookedInstanceTransforms"""
    T = d.get('instanceTransforms') or []
    if d.get('cookedInstanceTransforms') or not T:
        return d
    d = dict(d)
    d['cookedInstanceTransforms'] = {'$type': 'worldTransformBuffer', 'startIndex': 0, 'numElements': len(T),
                                     'sharedDataBuffer': {'Data': {'buffer': {'Data': {'Transforms': T}}}}}
    return d


WHITE = {'Red': 255, 'Green': 255, 'Blue': 255, 'Alpha': 255}


def _instance_lights(d):
    """an entity or device node's instance light components left white by WolvenKit 10 put back black; -> how many"""
    inst = (((d.get('instanceData') or {}).get('Data') or {}).get('buffer') or {}).get('Data') or {}
    n = 0
    for c in inst.get('Chunks') or []:
        if isinstance(c, dict) and c.get('$type') in ('gameLightComponent', 'entLightComponent'):
            col = c.get('color') or {}
            if all(col.get(k) == v for k, v in WHITE.items()):
                c['color'] = dict(col, Red=0, Green=0, Blue=0, Alpha=0)
                n += 1
    return n


def _ad_lights(d):
    """a mesh node's advertisement lights left white by WolvenKit 10 put back black; -> how many"""
    n = 0
    for L in d.get('lightsData') or []:
        col = L.get('color') if isinstance(L, dict) else None
        if isinstance(col, dict) and all(col.get(k) == v for k, v in WHITE.items()):
            L['color'] = dict(col, Red=0, Green=0, Blue=0, Alpha=0)
            n += 1
    return n


def fix_nodes(rc, since=1):
    """the node-level rewrites a file rewritten by VERSION `since` lacks (2: destructible transforms, 3: placeholder
    instance lights, 4: placeholder ad lights), in place; -> counts"""
    st = {}
    for w in rc.get('nodes') or []:
        d = w.get('Data') or {}
        t = d.get('$type')
        if since < 2 and t == 'worldInstancedDestructibleMeshNode':
            e = _destructible(d)
            if e is not d:
                w['Data'] = e
                st['worldInstancedDestructibleMeshNode'] = st.get('worldInstancedDestructibleMeshNode', 0) + 1
        if since < 3 and t in ('worldEntityNode', 'worldDeviceNode'):
            k = _instance_lights(d)
            if k:
                st['placeholder instance lights'] = st.get('placeholder instance lights', 0) + k
        if since < 4:
            k = _ad_lights(d)
            if k:
                st['placeholder ad lights'] = st.get('placeholder ad lights', 0) + k
    return st


def legacy_root(rc):
    """rc (a sector's RootChunk) rewritten in place in the 2.31 layout; -> counts by what was rewritten"""
    stats = {}
    if not is_25(rc):
        return stats
    nodes = rc.get('nodes') or []
    new_nodes, made, out = [], {}, []

    def add(key, node):
        if key not in made:
            made[key] = len(new_nodes)
            new_nodes.append(node)
        return made[key]
    for r in rc['nodeData']['Data']:
        ni = int(r.get('nodeIdx') or 0)
        wrap = nodes[ni]
        d = wrap.get('Data') or {}
        t = d.get('$type')
        ii = r.get('instanceIndex', NONE)
        buf = (d.get('buffer') or {}).get('Data') if isinstance(d.get('buffer'), dict) else None
        inner = (buf or {}).get('InnerNodes') or []
        if t in COOKED and 0 <= int(ii) < len(inner):
            k = add((ni, ii), dict(wrap, Data=_cooked(d, inner[int(ii)], buf)))
            stats[t] = stats.get(t, 0) + 1
        elif t == 'worldInstancedMeshNode' and inner and 0 <= int(ii) < len(inner):
            rngs = buf.get('TransformRanges') or []
            T = (((d.get('cookedWorldTransformsBuffer') or {}).get('Data') or {}).get('Transforms')) or []
            k = add((ni, ii), dict(wrap, Data=_instanced(d, inner[int(ii)], rngs[int(ii)] if int(ii) < len(rngs) else {}, T)))
            stats[t] = stats.get(t, 0) + 1
        else:
            k = add((ni, None), wrap)
        out.append(_record(r, k))
    rc['nodes'] = new_nodes
    rc['nodeData']['Data'] = out
    stats.update(fix_nodes(rc))
    rc['xenjkLegacyFrom'] = '2.5'
    rc['xenjkLegacyVersion'] = VERSION
    stats['records'] = len(out)
    stats['nodes'] = len(new_nodes)
    return stats


def _walk_lights(x):
    if isinstance(x, dict):
        if x.get('$type') in ('gameLightComponent', 'entLightComponent'):
            yield x
        for v in x.values():
            yield from _walk_lights(v)
    elif isinstance(x, list):
        for v in x:
            yield from _walk_lights(v)


def convert_resource_file(path):
    """an entity template or appearance (.ent / .app JSON) serialised by WolvenKit 10: its light components left white
    put back black, the placeholder the readers skip (Watson: 502 template and 3,300 appearance lights black in 2.31
    and white in 2.5, none white in both); a WolvenKit 9 file, or one done already, is left as it is. -> lights put back"""
    raw = Path(path).read_bytes()
    if b'xenjkPlaceholderLights' in raw or b'"WolvenKitVersion": "10' not in raw[:400]:
        return 0
    doc = json.loads(raw)
    n = 0
    for c in _walk_lights(doc.get('Data')):
        col = c.get('color') or {}
        if all(col.get(k) == v for k, v in WHITE.items()):
            c['color'] = dict(col, Red=0, Green=0, Blue=0, Alpha=0)
            n += 1
    doc.setdefault('Header', {})['xenjkPlaceholderLights'] = n
    tmp = Path(str(path) + '.tmp25')
    tmp.write_text(json.dumps(doc, indent=2), encoding='utf-8')
    tmp.replace(path)
    return n


def convert_file(path):
    """a sector JSON file rewritten in place when it is in the 2.5 layout; -> its counts ({} when left as it was)"""
    raw = Path(path).read_bytes()
    if b'xenjkLegacyFrom' in raw:
        m = re.search(rb'"xenjkLegacyVersion": *(\d+)', raw)      # (a current file is left without parsing it)
        if m and int(m.group(1)) >= VERSION:
            return {}
        doc = json.loads(raw)
        rc = doc['Data']['RootChunk']
        since = int(rc.get('xenjkLegacyVersion') or 1)
        if since >= VERSION:
            return {}
        stats = dict(fix_nodes(rc, since), upgraded=1)
        rc['xenjkLegacyVersion'] = VERSION
    elif b'"nodeIdx"' not in raw:
        return {}
    else:
        doc = json.loads(raw)
        stats = legacy_root(doc['Data']['RootChunk'])
    if stats:
        tmp = Path(str(path) + '.tmp25')
        tmp.write_text(json.dumps(doc), encoding='utf-8')
        tmp.replace(path)
    return stats


if __name__ == '__main__':
    tot = {}
    for f in sys.argv[1:]:
        for k, v in convert_file(f).items():
            tot[k] = tot.get(k, 0) + v
    print(json.dumps(tot, indent=1))
