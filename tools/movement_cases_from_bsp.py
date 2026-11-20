"""Build a MOVEBENCH1 case file for a CTF map from its BSP alone.

The Kejim declared set (`tools/fixtures/movement/kejim/README.md`) made
portable: two teams, both legs, three levels, three starts each, 36 cases.

  stand      the team's flag origin, dropped to the floor.
  respawns   the team's two `team_CTF_<team>spawn` points farthest from its own
             flag (Kejim's `respawn_4` and `respawn_5` are exactly these),
             dropped to the floor, farthest first as `respawn_5`.
  yaw        the entity's `angle`.
  return     starts at the enemy team's stand and respawns, as on Kejim.

Dropping to the floor is the engine's own box trace done offline: the player
box (-15,-15,-24)..(15,15,40) swept straight down through the world model's
brushes whose contents block a player (solid, player clip, terrain), stopping
SURFACE_CLIP_EPSILON (0.125) above the first plane it meets - which is how
Kejim's poses sit at floor + 24.125. Curved patches and terrain are not brushes
and are not traced, so on a map whose floors are patches the trace falls through
to whatever brush lies below (Dantooine's spawns: a pit 270 units down).

`--graph-world <dir>` takes the heights from the engine instead: a world that
loaded the map prints one `[GRAPH] seed` line per flag and spawn with its cell,
and its saved graph cache holds each cell's height, which is the standing
origin plus 0.125 (Kejim: cell 1924.25, stand 1924.125). Every start is then the
entity's x and y at its seed cell's height less 0.125.

Usage:
    python tools/movement_cases_from_bsp.py --pk3 <file.pk3> --bsp-member maps/mp/x.bsp
        --map mp/x --out tools/fixtures/movement/<name>/cases.json
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from strafe_map_preparation import bsp_entities  # noqa: E402

MINS = (-15.0, -15.0, -24.0)
MAXS = (15.0, 15.0, 40.0)
EPSILON = 0.125
MASK_PLAYERSOLID = 0x1 | 0x10 | 0x1000
LEVELS = {1: (0, ['walk', 'corner', 'hop', 'tap'], 180000),
          2: (50, ['walk', 'corner', 'hop', 'tap', 'speed'], 120000),
          3: (100, ['walk', 'corner', 'hop', 'tap', 'speed', 'hold'], 120000)}


def lump(data, index):
    offset, length = struct.unpack_from('<ii', data, 8 + index * 8)
    return data[offset:offset + length]


def world_brushes(data):
    """The world model's player-blocking brushes as lists of (normal, dist)."""
    shaders = lump(data, 1)
    contents = [struct.unpack_from('<i', shaders, i * 72 + 68)[0] for i in range(len(shaders) // 72)]
    planes_raw = lump(data, 2)
    planes = [struct.unpack_from('<4f', planes_raw, i * 16) for i in range(len(planes_raw) // 16)]
    _, _, _, _, _, _, _, _, first, count = struct.unpack_from('<6f4i', lump(data, 7), 0)
    brushes_raw, sides_raw = lump(data, 8), lump(data, 9)
    out = []
    for b in range(first, first + count):
        side, sides, shader = struct.unpack_from('<3i', brushes_raw, b * 12)
        if not contents[shader] & MASK_PLAYERSOLID:
            continue
        faces = []
        for s in range(side, side + sides):
            plane = planes[struct.unpack_from('<i', sides_raw, s * 12)[0]]
            faces.append((plane[:3], plane[3]))
        out.append(faces)
    return out


def sweep(brushes, start, end):
    """Fraction along start->end the player box travels before a brush blocks
    it (the engine's CM_TraceThroughBrush with the box offsets), and whether
    it started inside one."""
    best, start_solid = 1.0, False
    for faces in brushes:
        enter, leave, starts_out, gets_out = -1.0, 1.0, False, False
        for normal, dist in faces:
            offset = [MAXS[i] if normal[i] < 0 else MINS[i] for i in range(3)]
            d = dist - sum(offset[i] * normal[i] for i in range(3))
            d1 = sum(start[i] * normal[i] for i in range(3)) - d
            d2 = sum(end[i] * normal[i] for i in range(3)) - d
            if d2 > 0:
                gets_out = True
            if d1 > 0:
                starts_out = True
            if d1 > 0 and (d2 >= EPSILON or d2 >= d1):
                break
            if d1 <= 0 and d2 <= 0:
                continue
            if d1 > d2:
                f = (d1 - EPSILON) / (d1 - d2)
                enter = max(enter, max(f, 0.0))
            else:
                f = (d1 + EPSILON) / (d1 - d2)
                leave = min(leave, f)
        else:
            if not starts_out:
                start_solid = True
                continue
            if enter < leave and enter > -1 and enter < best:
                best = max(enter, 0.0)
    return best, start_solid


def drop_to_floor(brushes, origin, rise=64.0, depth=1024.0):
    """The standing origin under `origin`: the box swept down from `rise` above
    it. Tries lower starts when the first is inside solid (a low ceiling)."""
    for lift in (rise, 32.0, 16.0, 4.0):
        start = [origin[0], origin[1], origin[2] + lift]
        end = [origin[0], origin[1], start[2] - depth]
        fraction, solid = sweep(brushes, start, end)
        if solid or fraction >= 1:
            continue
        z = start[2] + (end[2] - start[2]) * fraction
        return [origin[0], origin[1], round(z * 8) / 8]
    raise ValueError('no floor under %r' % (origin,))


def engine_floors(world):
    """{(classname, x, y, z): standing z} from a world's seed lines and cache."""
    world = Path(world)
    cache = next((world / 'base' / 'bot2-graph').glob('*.txt'))
    heights = [float(line.split()[3]) for line in cache.read_text(encoding='utf-8').splitlines()
               if line.startswith('c ')]
    floors = {}
    for line in (world / 'server.log').read_text(encoding='utf-8', errors='replace').splitlines():
        if '[GRAPH] seed class=' not in line or 'status=admitted' not in line:
            continue
        fields = dict(f.split('=', 1) for f in line.split('[GRAPH] seed ')[1].split() if '=' in f)
        key = (fields['class'], float(fields['x']), float(fields['y']), float(fields['z']))
        floors[key] = heights[int(fields['cell'])] - 0.125
    return floors


def starts(entities, brushes, floors=None):
    def drop(e):
        if floors is None:
            return drop_to_floor(brushes, e['origin'])
        key = (e['classname'],) + tuple(e['origin'])
        if key not in floors:
            raise ValueError('no admitted seed cell for %r' % (key,))
        return [e['origin'][0], e['origin'][1], floors[key]]
    by_team = {}
    for team in ('red', 'blue'):
        flag = next(e for e in entities if e['classname'] == 'team_CTF_%sflag' % team)
        spawns = [e for e in entities if e['classname'] == 'team_CTF_%sspawn' % team]
        spawns.sort(key=lambda e: -math.dist(e['origin'][:2], flag['origin'][:2]))
        if len(spawns) < 2:
            raise ValueError('%s has fewer than two team spawns' % team)
        by_team[team] = [
            ('%s_flagstand' % team, drop(flag), flag['angle']),
            ('%s_respawn_5' % team, drop(spawns[0]), spawns[0]['angle']),
            ('%s_respawn_4' % team, drop(spawns[1]), spawns[1]['angle'])]
    return by_team


def build(pk3, member, map_name, declared, graph_world=None):
    data = zipfile.ZipFile(pk3).read(member)
    brushes = world_brushes(data)
    poses = starts(bsp_entities(data), brushes, engine_floors(graph_world) if graph_world else None)
    cases = []
    for team, enemy in (('red', 'blue'), ('blue', 'red')):
        for leg, side in (('outbound', team), ('return', enemy)):
            for level, (force, templates, bound) in LEVELS.items():
                for name, origin, yaw in poses[side]:
                    tag = 'stand' if name.endswith('flagstand') else name.split('_', 1)[1].replace('_', '')
                    cases.append({
                        'name': '%s_%s_L%d_%s' % (team, 'out' if leg == 'outbound' else 'ret', level, tag),
                        'team': team, 'leg': leg, 'level': level,
                        'start': {'name': name, 'origin': origin, 'yaw': yaw,
                                  'velocity': [0.0, 0.0, 0.0], 'carrying': 1 if leg == 'return' else 0},
                        'resources': {'force': force, 'speed_ms': 0},
                        'spend': {'templates': templates},
                        'goal': 'enemy_flag' if leg == 'outbound' else 'home_flag',
                        'pass_condition': {'condition': 'pickup' if leg == 'outbound' else 'capture',
                                           'bound_ms': bound, 'no_death': True}})
    return {'format': 'MOVEBENCH1', 'map': map_name, 'bsp_sha256': hashlib.sha256(data).hexdigest(),
            'declared': declared, 'cases': cases}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pk3', type=Path, required=True)
    parser.add_argument('--bsp-member', required=True)
    parser.add_argument('--map', required=True)
    parser.add_argument('--declared', default='generated by tools/movement_cases_from_bsp.py')
    parser.add_argument('--graph-world', type=Path, help='a bench world of this map: heights from its seed cells')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    doc = build(args.pk3, args.bsp_member, args.map, args.declared, args.graph_world)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(doc, indent=2) + '\n', encoding='utf-8', newline='\n')
    for case in doc['cases'][:3] + doc['cases'][9:12]:
        print(case['name'], case['start']['name'], case['start']['origin'], case['start']['yaw'])


if __name__ == '__main__':
    main()
