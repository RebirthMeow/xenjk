"""Read a prepared movement graph (bot2-graph/<map>_<checksum>.txt) and report it.

Coverage is measured against the benchmark fixtures: every declared start in
the given case files must stand on an admitted cell (a cell in one of the four
grid columns around the start whose height is within a step of the start's
hull origin). Node counts, widths and hazards are summarised so a build's
preparation is a set of numbers, not an impression.
"""
import argparse
import json
from collections import Counter
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
STEP = 18
HAZARDS = ['drop', 'liquid', 'hurt', 'ascent', 'blocked', 'steep']


def load_graph(path):
    lines = path.read_text().splitlines()
    header = lines[0].split()
    if len(header) < 2 or header[0] != 'graph':
        raise SystemExit('not a graph file: ' + str(path))
    meta = dict(version=int(header[1]))
    for token in header[2:]:
        key, _, value = token.partition('=')
        meta[key] = value
    grid = int(meta['grid'])
    cells, nodes, reasons, edges = [], [], {}, []
    for line in lines[1:]:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'r':
            reasons[parts[1]] = int(parts[2])
        elif parts[0] == 'c':
            ix, iy, z = int(parts[1]), int(parts[2]), float(parts[3])
            cells.append(dict(ix=ix, iy=iy, x=ix * grid, y=iy * grid, z=z,
                              link=[int(v) for v in parts[4:8]], refused=int(parts[8]),
                              flags=int(parts[9]), seed=int(parts[10]), node=[int(parts[11]), int(parts[12])]))
        elif parts[0] == 'e':
            edge = dict(type=int(parts[1]), src=int(parts[2]), dst=int(parts[3]),
                        takeoff=[float(v) for v in parts[4:7]], landing=[float(v) for v in parts[7:10]],
                        dir=[float(parts[10]), float(parts[11])], cost_ms=int(parts[12]), validated=int(parts[13]))
            if len(parts) >= 21:
                # Version 4: the state a strafe edge was learned with.
                edge.update(tap=int(parts[14]), entry_speed=float(parts[15]), entry_dir=[float(parts[16]), float(parts[17])],
                            exit_speed=float(parts[18]), exit_dir=[float(parts[19]), float(parts[20])])
            edges.append(edge)
        elif parts[0] == 'n':
            nodes.append(dict(axis=int(parts[1]), first=int(parts[2]), count=int(parts[3]),
                              z_min=float(parts[4]), z_max=float(parts[5]), width=float(parts[6]), flags=int(parts[7])))
    return meta, grid, cells, nodes, reasons, edges


def columns(cells):
    table = {}
    for index, c in enumerate(cells):
        table.setdefault((c['ix'], c['iy']), []).append(index)
    return table


def cell_under(cells, table, grid, origin):
    """The admitted cell a hull origin stands on, searching the four grid columns around it."""
    fx, fy = origin[0] / grid, origin[1] / grid
    best = None
    for ix in (int(fx // 1), int(fx // 1) + 1):
        for iy in (int(fy // 1), int(fy // 1) + 1):
            for index in table.get((ix, iy), []):
                dz = abs(cells[index]['z'] - origin[2])
                if dz <= STEP + .25:
                    distance = ((ix * grid - origin[0]) ** 2 + (iy * grid - origin[1]) ** 2) ** .5
                    if best is None or distance < best[0]:
                        best = (distance, index, dz)
    return best


WALK_MS = 128
DIAGONAL_MS = 181
HEURISTIC_SPEED = 450.0


def nearest_cell(cells, table, grid, point, radius=64, tolerance=36):
    """The nearest cell within a horizontal radius and height tolerance, as the native Graph_Nearest."""
    cx, cy = int(point[0] / grid + .5 // 1), int(point[1] / grid + .5 // 1)
    span = -(-radius // grid)
    best = None
    for ix in range(cx - span, cx + span + 1):
        for iy in range(cy - span, cy + span + 1):
            for index in table.get((ix, iy), []):
                c = cells[index]
                if abs(c['z'] - point[2]) > tolerance:
                    continue
                d = ((ix * grid - point[0]) ** 2 + (iy * grid - point[1]) ** 2) ** .5
                if d <= radius and (best is None or d < best[0]):
                    best = (d, index)
    return best[1] if best else None


def find_path(cells, edges, grid, start, goal, disabled=()):
    """A* by time over links and edges: the native Graph_Path. Returns (cells, edges_used, cost_ms) or None."""
    import heapq
    leaving = {}
    for index, e in enumerate(edges):
        if index not in disabled:
            leaving.setdefault(e['src'], []).append(index)

    def h(i):
        a, b = cells[i], cells[goal]
        return (((a['x'] - b['x']) ** 2 + (a['y'] - b['y']) ** 2) ** .5) / HEURISTIC_SPEED * 1000
    g = {start: 0.0}
    parent = {start: (None, -1)}
    heap = [(h(start), start)]
    closed = set()
    while heap:
        _, cell = heapq.heappop(heap)
        if cell in closed:
            continue
        closed.add(cell)
        if cell == goal:
            break
        steps = [(nxt, WALK_MS) for nxt in cells[cell]['link'] if nxt >= 0]
        links = cells[cell]['link']
        for d in range(4):
            a, b = links[d], links[(d + 1) % 4]
            if a >= 0 and b >= 0 and cells[a]['link'][(d + 1) % 4] >= 0 and cells[a]['link'][(d + 1) % 4] == cells[b]['link'][d]:
                steps.append((cells[a]['link'][(d + 1) % 4], DIAGONAL_MS))
        for nxt, cost in steps:
            if g[cell] + cost < g.get(nxt, 1e30):
                g[nxt] = g[cell] + cost
                parent[nxt] = (cell, -1)
                heapq.heappush(heap, (g[nxt] + h(nxt), nxt))
        for index in leaving.get(cell, []):
            e = edges[index]
            if g[cell] + e['cost_ms'] < g.get(e['dst'], 1e30):
                g[e['dst']] = g[cell] + e['cost_ms']
                parent[e['dst']] = (cell, index)
                heapq.heappush(heap, (g[e['dst']] + h(e['dst']), e['dst']))
    if goal not in closed:
        return None
    path, used = [], []
    cell = goal
    while cell is not None:
        path.append(cell)
        prev, edge = parent[cell]
        used.append(edge)
        cell = prev
    return path[::-1], used[::-1], g[goal]


def fixture_starts(paths):
    starts = {}
    for path in paths:
        doc = json.loads(Path(path).read_text())
        for case in doc['cases']:
            start = case['start']
            key = (start['name'], tuple(start['origin']))
            starts.setdefault(key, []).append(case['name'])
    return starts


def seed_lines(log):
    if not log:
        return []
    text = Path(log).read_text(errors='replace')
    return re.findall(r'\[GRAPH\] seed (.*)', text)


def report(graph_path, fixtures, log):
    meta, grid, cells, nodes, reasons, edges = load_graph(graph_path)
    table = columns(cells)
    out = dict(graph=str(graph_path), meta=meta, cells=len(cells), nodes=len(nodes), reasons=reasons)
    layered = sum(1 for column in table.values() if len(column) > 1)
    out['columns'] = len(table)
    out['stacked_columns'] = layered
    out['boundary_cells'] = sum(1 for c in cells if c['refused'])
    hazard_counts = Counter()
    for c in cells:
        for bit, name in enumerate(HAZARDS):
            if c['flags'] & (1 << bit):
                hazard_counts[name] += 1
    out['cells_with_hazard'] = dict(hazard_counts)
    lengths = sorted(n['count'] * grid for n in nodes)
    out['node_length_units'] = dict(min=lengths[0], median=lengths[len(lengths) // 2], max=lengths[-1]) if lengths else None
    out['nodes_at_least_256'] = sum(1 for n in nodes if n['count'] * grid >= 256)
    out['nodes_single_cell'] = sum(1 for n in nodes if n['count'] == 1)
    zs = sorted(c['z'] for c in cells)
    out['z_range'] = [zs[0], zs[-1]] if zs else None
    coverage = []
    for (name, origin), names in sorted(fixture_starts(fixtures).items()):
        hit = cell_under(cells, table, grid, origin)
        coverage.append(dict(start=name, origin=list(origin), cases=len(names), covered=hit is not None,
                             cell=hit[1] if hit else None, offset=round(hit[0], 1) if hit else None,
                             dz=round(hit[2], 3) if hit else None))
    out['starts'] = coverage
    out['starts_covered'] = sum(1 for s in coverage if s['covered'])
    out['starts_total'] = len(coverage)
    out['seeds'] = seed_lines(log)
    names = {1: 'hop', 2: 'pad', 3: 'drop', 4: 'jump', 5: 'strafe'}
    out['edges'] = [dict(type=names.get(e['type'], str(e['type'])), **{k: v for k, v in e.items() if k != 'type'}) for e in edges]
    out['edges_by_type'] = dict(Counter(e['type'] for e in out['edges']))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('graph', type=Path)
    parser.add_argument('--fixtures', type=Path, nargs='*',
                        default=[ROOT / 'tools/fixtures/movement/kejim/cases.json',
                                 ROOT / 'tools/fixtures/movement/kejim/spike_starts.json',
                                 ROOT / 'tools/fixtures/movement/kejim/handoff_probe.json'])
    parser.add_argument('--log', type=Path, help='server.log with the [GRAPH] seed lines')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--path', type=float, nargs=6, metavar='N',
                        help='search from x y z to x y z and print the path with its edges')
    parser.add_argument('--no-edges', action='store_true', help='search over walk links only')
    args = parser.parse_args()
    if args.path:
        meta, grid, cells, nodes, reasons, edges = load_graph(args.graph)
        table = columns(cells)
        start = nearest_cell(cells, table, grid, args.path[:3])
        goal = nearest_cell(cells, table, grid, args.path[3:])
        found = find_path(cells, edges, grid, start, goal, disabled=set(range(len(edges))) if args.no_edges else ()) \
            if start is not None and goal is not None else None
        if not found:
            print(f'no path start={start} goal={goal}')
            return 1
        path, used, cost = found
        walked = sum(1 for e in used if e < 0) * grid
        print(f'path start={start} goal={goal} cells={len(path)} cost_ms={cost:.0f} walk_units={walked} edges={sum(1 for e in used if e >= 0)}')
        for i, (cell, edge) in enumerate(zip(path, used)):
            c = cells[cell]
            if edge >= 0 or i == 0 or i == len(path) - 1 or i % 10 == 0:
                tag = f" via edge {edge} ({ {1: 'hop', 2: 'pad', 3: 'drop', 4: 'jump', 5: 'strafe'}.get(edges[edge]['type']) } cost_ms={edges[edge]['cost_ms']})" if edge >= 0 else ''
                print(f'  {i:4} cell={cell} at=({c["x"]},{c["y"]},{c["z"]:.1f}){tag}')
        return 0
    out = report(args.graph, args.fixtures, args.log)
    if args.json:
        print(json.dumps(out, indent=2))
    else:
        m = out['meta']
        print(f"graph {m.get('map')} checksum={m.get('checksum')} grid={m.get('grid')} cells={out['cells']} "
              f"columns={out['columns']} stacked={out['stacked_columns']} nodes={out['nodes']} "
              f"boundary={out['boundary_cells']} samples={m.get('samples')} refusals={m.get('refusals')} capped={m.get('capped')}")
        print('refusals ' + ' '.join(f'{k}={v}' for k, v in out['reasons'].items() if v))
        print('hazards ' + ' '.join(f'{k}={v}' for k, v in out['cells_with_hazard'].items()))
        print(f"nodes length_units={out['node_length_units']} at_least_256={out['nodes_at_least_256']} single_cell={out['nodes_single_cell']} z_range={out['z_range']}")
        for s in out['seeds']:
            print('seed ' + s)
        for e in out['edges']:
            print(f"edge {e['type']:<5} {e['src']}->{e['dst']} takeoff={e['takeoff']} landing={e['landing']} cost_ms={e['cost_ms']} validated={e['validated']}")
        for s in out['starts']:
            print(f"start {s['start']:<32} {'covered' if s['covered'] else 'UNCOVERED':<9} cases={s['cases']} "
                  f"cell={s['cell']} offset={s['offset']} dz={s['dz']} origin={s['origin']}")
        print(f"coverage {out['starts_covered']}/{out['starts_total']} starts")
    return 0 if out['starts_covered'] == out['starts_total'] else 1


if __name__ == '__main__':
    sys.exit(main())
