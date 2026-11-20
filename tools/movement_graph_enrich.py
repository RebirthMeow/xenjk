"""Enrich a production graph cache with the edges real runs committed.

Every retained world logs its commits as ``[MOVE] ... event=plan edge=hop0
reason=sequence ... index=N index2=M`` where ``N`` and ``M`` name graph edges
(-1 is none). An index is local to the graph that world loaded, so each one is
resolved against **that world's own saved cache** (``<world>/base/bot2-graph/``),
never against another graph. ``Graph_Save`` writes ``edge[0..edgeCount)`` in
index order, disabled edges included (the flag is not saved), and nothing
removes an edge except ``Graph_ResetLearned`` at a fixture release, which
truncates the appended edges. So a saved record at index N is the edge the
commit named, with one exception: a commit *before* the release naming an
appended index, in a world whose reset dropped edges, named an edge that no
longer exists and whose index may have been reused. Those are refused, and so
is any index whose ``[GRAPH] edge_added`` line disagrees with the saved record.

The same resolution serves **planned routes** (``--routes``): every installed
route prints a ``[ROUTE]`` line of its edge indices (``read_routes``), and a
fast trip flies straight candidates along route edges it never names on a
commit, so the commit lines alone cannot fold them. Each route index is
resolved against the world's own saved cache exactly as a commit's is, at the
route's own level, and a route whose ``graph=`` is not the identity the world
loaded is refused whole (``other_graph``). With ``--routes-leg`` only the
routes installed during the case's leg are folded - from the release to the
completion the runner recorded - not the bot's pre-release free play or its
play after the objective.

The resolved records - and, optionally, the learned edges of worlds that loaded
the base cache itself (their saved edges past the loaded count) - are folded
into the base cache with ``movement_graph_merge.py``'s fold identity: one edge
per type, cells, tap and entry band (strafe) or rounded takeoff (others), the
lowest cost kept, the base's own order first. Cells and nodes must match the
base in every world, since an edge names cells by index.

    python tools/movement_graph_enrich.py BASE.txt --out OUT.txt --manifest OUT.json \\
        --committed .codex-research/<campaign> [...] --learned .codex-research/<campaign> [...] \\
        --routes .codex-research/<campaign> [...]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_graph_merge import edge_key, fold_edges, write_cache  # noqa: E402

SEQUENCE = 'reason=sequence'
FIELD = re.compile(r'(\w+)=([\w./-]+)')
KINDS = {'1': 'hop', '2': 'pad', '3': 'drop', '4': 'jump', '5': 'strafe'}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def fields(line):
    return dict(FIELD.findall(line))


def read_sections(path):
    """Header, reasons, cells, nodes and edge lines of a cache, plus the cells-and-nodes digest."""
    text = Path(path).read_text(encoding='utf-8')
    lines = text.splitlines()
    if not lines or not lines[0].startswith('graph '):
        raise ValueError(f'not a graph cache: {path}')
    reasons, cells, nodes, edges = [], [], [], []
    for line in lines[1:]:
        if not line.strip():
            continue
        {'r': reasons, 'c': cells, 'n': nodes, 'e': edges}[line[0]].append(line)
    floor = hashlib.sha256('\n'.join(cells + ['--'] + nodes).encode('utf-8')).hexdigest()
    return lines[0], reasons, cells, nodes, edges, floor


def binding(header):
    values = dict(token.partition('=')[::2] for token in header.split()[2:])
    return {k: values.get(k) for k in ('map', 'checksum', 'grid', 'gravity', 'speed', 'step')}


def find_worlds(campaign):
    """Every directory under a campaign holding a server.log and one saved cache."""
    worlds = []
    for root, dirs, files in os.walk(campaign):
        dirs.sort()
        if 'server.log' not in files:
            continue
        caches = sorted((Path(root) / 'base' / 'bot2-graph').glob('*.txt'))
        worlds.append((Path(root), caches))
        dirs[:] = [d for d in dirs if d != 'base']
    return worlds


def read_world(directory):
    """What the log and result say: loaded edges, identity, release, reset, commits, appends."""
    world = dict(map_loads=0, loaded=None, identity=None, release=None, dropped=0,
                 commits=[], added={}, route_lines=[], leg_ms=None)
    with open(directory / 'server.log', encoding='utf-8', errors='replace') as stream:
        for line in stream:
            if '[GRAPH] map_load' in line:
                world['map_loads'] += 1
                if world['map_loads'] == 1:
                    f = fields(line)
                    world['loaded'] = int(f['edges'])
                    world['identity'] = f.get('graph')
            elif '[GRAPH] edge_added' in line:
                f = fields(line)
                world['added'].setdefault(int(f['index']), []).append(
                    (int(f['from']), int(f['to'])))
            elif ROUTE in line:
                world['route_lines'].append(line)
            elif SEQUENCE in line and 'event=plan' in line and 'edge=hop0' in line:
                f = fields(line)
                world['commits'].append(dict(level=int(f['level']), kind=f.get('kind'),
                                             index=int(f.get('index', -1)),
                                             index2=int(f.get('index2', -1)),
                                             c0=int(f.get('c0', -1)), c1=int(f.get('c1', -1))))
    result = directory / 'result.json'
    if result.is_file():
        try:
            data = json.loads(result.read_text(encoding='utf-8'))
        except ValueError:
            data = {}
        world['release'] = data.get('corner_level')
        world['leg_ms'] = data.get('time_ms')
        response = data.get('corner_response') or ''
        match = re.search(r'reset_learned dropped=(\d+)', response)
        if match:
            world['dropped'] = int(match.group(1))
    # The bench writes the completion time on the run's summary, one level up.
    summary = directory.parent / 'result.json'
    if world['leg_ms'] is None and summary.is_file():
        try:
            cases = json.loads(summary.read_text(encoding='utf-8')).get('cases') or []
        except ValueError:
            cases = []
        for case in cases:
            if isinstance(case, dict) and case.get('name') == directory.name:
                world['leg_ms'] = case.get('time_ms')
    return world


ROUTE = '[ROUTE]'
ROUTE_EDGES = re.compile(r'(?:^|\s)edges=(-|\d+(?:,\d+)*)(?:\s|$)')


def read_routes(lines):
    """Every installed route a log's ``[ROUTE]`` lines name, in order of installation.

    The engine prints one line per installed route - ``source=replan`` where a
    replan installs it, ``source=reroute`` for a route a commit installed,
    printed after that search returned - split into parts of at most 64 edge
    indices. ``total`` is the route's edge count, ``first`` the ordinal of the
    part's first index and ``count`` the indices in the part (``edges=-`` when
    the route has none). A route is keyed by client and path generation and is
    returned only when its parts are contiguous and add up to ``total``; a
    route missing a part is returned under ``incomplete`` instead, never
    guessed at. Parsing only: nothing here folds a route into a cache.
    """
    parts, order = {}, []
    for line in lines:
        at = line.find(ROUTE)
        if at < 0:
            continue
        f = fields(line[at:])
        try:
            key = (int(f['client']), int(f['gen']))
            first, count, total = int(f['first']), int(f['count']), int(f['total'])
        except (KeyError, ValueError):
            continue
        match = ROUTE_EDGES.search(line[at:])
        listed = match.group(1) if match else '-'
        edges = [] if listed == '-' else [int(x) for x in listed.split(',') if x]
        if len(edges) != count:
            continue
        if key not in parts:
            parts[key] = dict(client=key[0], generation=key[1], level=int(f.get('level', -1)),
                              source=f.get('source'), graph=f.get('graph'),
                              origin=int(f.get('from', -1)), to=int(f.get('to', -1)),
                              cost_ms=int(f.get('cost_ms', -1)), total=total, pieces={})
            order.append(key)
        parts[key]['pieces'][first] = edges
    routes, incomplete = [], []
    for key in order:
        route = parts[key]
        pieces = route.pop('pieces')
        edges, at = [], 0
        while at in pieces and (pieces[at] or at == 0):
            edges.extend(pieces[at])
            if not pieces[at]:
                break
            at += len(pieces[at])
        if len(edges) == route['total'] and sum(len(p) for p in pieces.values()) == route['total']:
            route['edges'] = edges
            routes.append(route)
        else:
            route['edges'] = edges
            incomplete.append(route)
    return routes, incomplete


def refusal(world, edges, index, level):
    """Why an index named at `level` cannot be read from the world's own save, or None."""
    release = world['release']
    if index >= len(edges):
        return 'beyond_cache'
    if (world['dropped'] and release is not None and level < release
            and world['loaded'] is not None and index >= world['loaded']):
        return 'dropped_by_reset'
    parts = edges[index].split()
    added = world['added'].get(index)
    if added and added[-1] != (int(parts[2]), int(parts[3])):
        return 'added_mismatch'
    return None


def resolve(world, edges):
    """(index, record) for every named index, and the refusals by reason."""
    resolved, refused = [], {}
    for commit in world['commits']:
        for slot in ('index', 'index2'):
            index = commit[slot]
            if index < 0:
                continue
            why = refusal(world, edges, index, commit['level'])
            if why:
                refused[why] = refused.get(why, 0) + 1
            else:
                resolved.append((index, edges[index], commit, slot))
    return resolved, refused


def resolve_routes(world, edges, leg_only=False):
    """(index, record, route) for every edge of every complete installed route.

    A route is refused whole when its line names another graph than the one the
    world loaded (``other_graph``) or when a part is missing (``incomplete``);
    otherwise each index is resolved as a commit's is, at the route's level.
    """
    routes, incomplete = read_routes(world['route_lines'])
    resolved, refused = [], {}
    if leg_only:
        release, leg = world['release'], world['leg_ms']
        if release is None or leg is None:
            refused['no_leg'] = len(routes) + len(incomplete)
            return [], [], refused
        inside = lambda r: release <= r['level'] <= release + leg
        refused_outside = sum(1 for r in routes if not inside(r))
        if refused_outside:
            refused['outside_leg'] = refused_outside
        routes = [r for r in routes if inside(r)]
        incomplete = [r for r in incomplete if inside(r)]
    if incomplete:
        refused['incomplete'] = len(incomplete)
    for route in routes:
        if world['identity'] and route['graph'] and route['graph'] != world['identity']:
            refused['other_graph'] = refused.get('other_graph', 0) + 1
            continue
        for index in route['edges']:
            if index < 0:
                continue
            why = refusal(world, edges, index, route['level'])
            if why:
                refused[why] = refused.get(why, 0) + 1
            else:
                resolved.append((index, edges[index], route))
    return routes, resolved, refused


def enrich(base_path, committed, learned, learned_identity=None, log=print, routes=(), routes_leg=False):
    header, reasons, cells, nodes, base_edges, floor = read_sections(base_path)
    base_binding = binding(header)
    base_keys = {edge_key(line.split()) for line in base_edges}
    inputs, extra, campaigns = [], [], []
    for group, dirs in (('committed', committed), ('learned', learned), ('routes', routes)):
        for campaign in dirs:
            stats = dict(campaign=str(campaign), group=group, worlds=0, no_cache=0, floor_mismatch=0,
                         sequence_lines=0, named=0, resolved=0, refused={}, keys=set(),
                         identities={}, landing_agrees=0, types={}, learned_edges=0,
                         loaded_but_mutated=0, routes=0, route_indices=0)
            for directory, caches in find_worlds(campaign):
                stats['worlds'] += 1
                world = read_world(directory)
                entry = dict(world=str(directory), group=group,
                             server_log=sha256(directory / 'server.log'))
                if (directory / 'result.json').is_file():
                    entry['result_json'] = sha256(directory / 'result.json')
                if len(caches) != 1:
                    stats['no_cache'] += 1
                    entry['cache'] = None
                    inputs.append(entry)
                    continue
                entry['cache'] = str(caches[0])
                entry['cache_sha256'] = sha256(caches[0])
                inputs.append(entry)
                w_header, _, _, _, edges, w_floor = read_sections(caches[0])
                if w_floor != floor or binding(w_header) != base_binding:
                    stats['floor_mismatch'] += 1
                    continue
                identity = world['identity'] or 'unreported'
                stats['identities'][identity] = stats['identities'].get(identity, 0) + 1
                if group == 'committed':
                    stats['sequence_lines'] += len(world['commits'])
                    stats['named'] += sum((c['index'] >= 0) + (c['index2'] >= 0) for c in world['commits'])
                    resolved, refused = resolve(world, edges)
                    for why, count in refused.items():
                        stats['refused'][why] = stats['refused'].get(why, 0) + count
                    stats['resolved'] += len(resolved)
                    for index, line, commit, slot in resolved:
                        parts = line.split()
                        key = edge_key(parts)
                        stats['keys'].add(key)
                        kind = KINDS.get(parts[1], parts[1])
                        stats['types'][kind] = stats['types'].get(kind, 0) + 1
                        landing = commit['c0'] if slot == 'index' else commit['c1']
                        stats['landing_agrees'] += int(parts[3]) == landing
                        if world['loaded'] is not None and index < world['loaded'] and key not in base_keys \
                                and identity == learned_identity:
                            stats['loaded_but_mutated'] += 1
                        extra.append(line)
                elif group == 'routes':
                    listed, resolved, refused = resolve_routes(world, edges, routes_leg)
                    stats['routes'] += len(listed)
                    stats['route_indices'] += sum(len(r['edges']) for r in listed)
                    for why, count in refused.items():
                        stats['refused'][why] = stats['refused'].get(why, 0) + count
                    stats['resolved'] += len(resolved)
                    for index, line, route in resolved:
                        parts = line.split()
                        stats['keys'].add(edge_key(parts))
                        kind = KINDS.get(parts[1], parts[1])
                        stats['types'][kind] = stats['types'].get(kind, 0) + 1
                        extra.append(line)
                else:
                    if learned_identity and world['identity'] != learned_identity:
                        stats['refused']['other_graph'] = stats['refused'].get('other_graph', 0) + 1
                        continue
                    appended = edges[world['loaded']:] if world['loaded'] is not None else []
                    stats['learned_edges'] += len(appended)
                    for line in appended:
                        stats['keys'].add(edge_key(line.split()))
                    extra.extend(appended)
            stats['distinct'] = len(stats['keys'])
            stats['in_base'] = len(stats['keys'] & base_keys)
            stats['new'] = stats['distinct'] - stats['in_base']
            campaigns.append(stats)
            log(f"{group} {campaign}: worlds={stats['worlds']} distinct={stats['distinct']} "
                f"in_base={stats['in_base']} new={stats['new']} refused={stats['refused']}")
    folded = fold_edges(base_edges + extra)
    replaced = sum(1 for a, b in zip(base_edges, folded[:len(base_edges)]) if a != b)
    all_keys = set().union(*(s['keys'] for s in campaigns)) if campaigns else set()
    committed_keys = set().union(*(s['keys'] for s in campaigns if s['group'] == 'committed')) \
        if any(s['group'] == 'committed' for s in campaigns) else set()
    learned_keys = set().union(*(s['keys'] for s in campaigns if s['group'] == 'learned')) \
        if any(s['group'] == 'learned' for s in campaigns) else set()
    route_keys = set().union(*(s['keys'] for s in campaigns if s['group'] == 'routes')) \
        if any(s['group'] == 'routes' for s in campaigns) else set()
    new_header = re.sub(r' edges=\d+', f' edges={len(folded)}', header, count=1)
    summary = dict(base_edges=len(base_edges), out_edges=len(folded), base_records_replaced=replaced,
                   distinct_all=len(all_keys), new_all=len(all_keys - base_keys),
                   distinct_committed=len(committed_keys), new_committed=len(committed_keys - base_keys),
                   distinct_learned=len(learned_keys), new_learned=len(learned_keys - base_keys),
                   new_learned_only=len(learned_keys - base_keys - committed_keys),
                   distinct_routes=len(route_keys), new_routes=len(route_keys - base_keys),
                   new_routes_only=len(route_keys - base_keys - committed_keys - learned_keys))
    by_type = {}
    for line in folded:
        kind = KINDS.get(line.split()[1], line.split()[1])
        by_type[kind] = by_type.get(kind, 0) + 1
    summary['out_types'] = by_type
    for stats in campaigns:
        stats['keys'] = len(stats['keys'])
    return (new_header, reasons, cells, nodes, folded), summary, campaigns, inputs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('base', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--committed', type=Path, nargs='*', default=[],
                        help='campaigns whose committed edges are resolved and folded')
    parser.add_argument('--learned', type=Path, nargs='*', default=[],
                        help='campaigns whose saved caches past the loaded count are folded')
    parser.add_argument('--routes', type=Path, nargs='*', default=[],
                        help="campaigns whose installed routes' edges ([ROUTE] lines) are resolved and folded")
    parser.add_argument('--routes-leg', action='store_true',
                        help="fold only routes installed between the case's release and its completion")
    parser.add_argument('--learned-identity', help='only learned worlds that loaded this graph identity')
    parser.add_argument('--force', action='store_true', help='replace an existing --out or --manifest')
    args = parser.parse_args(argv)
    existing = [p for p in (args.out, args.manifest) if p.exists()]
    if existing and not args.force:   # --out can name a live seed
        raise SystemExit('refusing to replace %s; pass --force to replace it' % ', '.join(map(str, existing)))
    cache, summary, campaigns, inputs = enrich(args.base, args.committed, args.learned, args.learned_identity,
                                               routes=args.routes, routes_leg=args.routes_leg)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_cache(args.out, *cache)
    manifest = dict(tool='tools/movement_graph_enrich.py', tool_sha256=sha256(__file__),
                    merge_sha256=sha256(Path(__file__).with_name('movement_graph_merge.py')),
                    base=str(args.base), base_sha256=sha256(args.base),
                    out=str(args.out), out_sha256=sha256(args.out),
                    learned_identity=args.learned_identity, routes_leg=args.routes_leg,
                    summary=summary, campaigns=campaigns, inputs=inputs)
    args.manifest.write_text(json.dumps(manifest, indent=1), encoding='utf-8', newline='\n')
    print(json.dumps(summary))
    print(f'wrote {args.out} and {args.manifest}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
