"""Merge the graph caches of a movement campaign's worlds into one seed file.

Every world of a campaign prepares the same graph at map load (the fill is
deterministic for one map, checksum and physics binding) and learns its own
edges on the way. This tool reads each world's cache under
``<campaign>/<case>/base/bot2-graph/`` and writes one cache with the same
cells and nodes and the union of the edges, so a later campaign can be
seeded with everything the earlier one learned (``movement_bench.py
--graph``). Strafe edges are folded as the game folds them: one edge per
launch cell, landing cell, tap flag and entry-speed band, the lowest cost
kept. Other learned edges are folded by type, cells and rounded takeoff.

    python tools/movement_graph_merge.py .codex-research/<campaign> --out <seed.txt> [--level N] [--filter REGEX]

Refuses when the worlds disagree on the header binding, the cells or the
nodes, since a seed must describe exactly the floor every world admits.
"""
import argparse
from pathlib import Path
import re
import sys

STATE_BAND = 48.0


def read_cache(path):
    text = path.read_text()
    lines = text.splitlines()
    header = lines[0]
    if not header.startswith('graph '):
        raise SystemExit(f'not a graph cache: {path}')
    cells, nodes, reasons, edges = [], [], [], []
    for line in lines[1:]:
        if not line.strip():
            continue
        kind = line[0]
        if kind == 'c':
            cells.append(line)
        elif kind == 'n':
            nodes.append(line)
        elif kind == 'r':
            reasons.append(line)
        elif kind == 'e':
            edges.append(line)
        else:
            raise SystemExit(f'unknown line in {path}: {line[:40]}')
    return header, reasons, cells, nodes, edges


# The game's edge cap (bot2_graph.c GRAPH_MAX_EDGES): a cache over it is refused at load.
GRAPH_MAX_EDGES = 131072


def header_fields(header):
    fields = {'version': header.split()[1] if len(header.split()) > 1 else ''}
    for token in header.split()[2:]:
        key, _, value = token.partition('=')
        fields[key] = value
    return fields


def edge_key(parts):
    """The identity an edge is folded on: type, cells, tap and entry band."""
    kind, src, dst = int(parts[1]), int(parts[2]), int(parts[3])
    if kind == 5 and len(parts) >= 21:
        tap, entry = int(parts[14]), float(parts[15])
        return (kind, src, dst, tap, int(entry // STATE_BAND))
    takeoff = tuple(round(float(v)) for v in parts[4:7])
    return (kind, src, dst, takeoff)


def fold_edges(edge_lines):
    """Union of edge lines, one per key, the lowest cost kept; in first-seen order."""
    folded, order = {}, []
    for line in edge_lines:
        parts = line.split()
        key = edge_key(parts)
        cost = int(parts[12])
        if key not in folded:
            folded[key] = (cost, line)
            order.append(key)
        elif cost < folded[key][0]:
            folded[key] = (cost, line)
    return [folded[k][1] for k in order]


def world_caches(campaign, level=None, pattern=None):
    caches = []
    for case_dir in sorted(p for p in Path(campaign).iterdir() if p.is_dir()):
        name = case_dir.name
        if level is not None and f'_L{level}_' not in name:
            continue
        if pattern and not re.search(pattern, name):
            continue
        for cache in sorted((case_dir / 'base' / 'bot2-graph').glob('*.txt')):
            caches.append((name, cache))
    return caches


def merge(caches):
    """Merge parsed caches: (header, reasons, cells, nodes, edges) tuples with names."""
    base = None
    edge_lines = []
    per_world = {}
    for name, (header, reasons, cells, nodes, edges) in caches:
        fields = header_fields(header)
        binding = {k: fields[k] for k in ('version', 'map', 'checksum', 'grid', 'gravity', 'speed', 'step')}
        if base is None:
            base = dict(header=header, reasons=reasons, cells=cells, nodes=nodes, binding=binding)
        else:
            if binding != base['binding']:
                raise SystemExit(f'{name}: binding differs from the first world: {binding} vs {base["binding"]}')
            if cells != base['cells']:
                raise SystemExit(f'{name}: cells differ from the first world ({len(cells)} vs {len(base["cells"])})')
            if nodes != base['nodes']:
                raise SystemExit(f'{name}: nodes differ from the first world')
        edge_lines.extend(edges)
        per_world[name] = len(edges)
    if base is None:
        raise SystemExit('no caches to merge')
    edges = fold_edges(edge_lines)
    if len(edges) > GRAPH_MAX_EDGES:
        raise SystemExit(f'{len(edges)} folded edges: over the game\'s cap of {GRAPH_MAX_EDGES}, the game would refuse the cache')
    header = re.sub(r' edges=\d+', f' edges={len(edges)}', base['header'], count=1)
    return header, base['reasons'], base['cells'], base['nodes'], edges, per_world


def write_cache(path, header, reasons, cells, nodes, edges):
    Path(path).write_text('\n'.join([header] + reasons + cells + nodes + edges) + '\n', encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('campaign', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--level', type=int, choices=(1, 2, 3), help='only the worlds of this level')
    parser.add_argument('--filter', help='regular expression on case names')
    parser.add_argument('--force', action='store_true', help='replace an existing --out')
    args = parser.parse_args()
    if args.out.exists() and not args.force:   # --out can name a live seed
        print(f'refusing to replace {args.out}; pass --force to replace it', file=sys.stderr)
        return 2
    found = world_caches(args.campaign, args.level, args.filter)
    if not found:
        print('no world caches found', file=sys.stderr)
        return 1
    header, reasons, cells, nodes, edges, per_world = merge([(name, read_cache(path)) for name, path in found])
    write_cache(args.out, header, reasons, cells, nodes, edges)
    by_type = {}
    for line in edges:
        kind = {'1': 'hop', '2': 'pad', '3': 'drop', '4': 'jump', '5': 'strafe'}.get(line.split()[1], line.split()[1])
        by_type[kind] = by_type.get(kind, 0) + 1
    print(f'merged {len(found)} worlds: cells={len(cells)} nodes={len(nodes)} edges={len(edges)} '
          + ' '.join(f'{k}={v}' for k, v in sorted(by_type.items())))
    print('edges per world: ' + ', '.join(f'{k} {v}' for k, v in per_world.items()))
    print(f'wrote {args.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
