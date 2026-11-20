"""Prepare a map's graph seed from measured edge outcomes (map preparation).

A graph built from the world offers candidate edges - pads, drops, jumps and
wall climbs - found from geometry alone (bot2_graph.c). On some maps many of
them can never be executed from the ways the runner reaches them, and each
costs two failures in play before the graph lets it go (Talravin, Talay:
docs/movement/records/2026-09-27-eight-maps.md). This tool reads the worlds of
earlier campaigns on one map - each world's graph cache and its
``[GRAPH] edge_result`` lines - and writes the built graph with the candidates
that failed somewhere and succeeded nowhere left out. Lift rides are kept: a
ride's failure depends on the lift's timing, not on the edge.

The seed holds only the built graph: cells, nodes and the built edges, none of
the edges a world learned, so a run seeded with it differs from an unseeded
run only by the candidates it leaves out.
Built edges are the leading block of every cache, identical across the worlds
of one map except for their ``validated`` flag, which is reset here; the tool
refuses worlds that disagree on anything else.

    python tools/movement_graph_prepare.py <campaign>... --map mp/ctf_talravin --out <seed.txt>
        [--filter REGEX] [--exclude REGEX] [--keep-all]

``--keep-all`` writes the same built graph with nothing left out (the control).
The output file name must stay the cache's own (<map>_<checksum>.txt) for
``movement_bench.py --graph``.
"""
import argparse
from pathlib import Path
import re
import sys

LEARNED = {'1', '5'}        # hop, strafe: edges a world learns in play
PRUNABLE = {'2': 'pad', '3': 'drop', '4': 'jump', '6': 'climb'}   # candidates from geometry
VALIDATED = 13              # field index of the validated flag in an edge line
RESULT = re.compile(r'\[GRAPH\] edge_result index=(\d+) type=(\w+) success=([01])')


def split_cache(text):
    """(header, reasons, cells, nodes, built edges) of one cache."""
    lines = [line for line in text.splitlines() if line.strip()]
    if not lines or not lines[0].startswith('graph '):
        raise ValueError('not a graph cache')
    body = lines[1:]
    reasons = [line for line in body if line[0] == 'r']
    cells = [line for line in body if line[0] == 'c']
    nodes = [line for line in body if line[0] == 'n']
    edges = [line for line in body if line[0] == 'e']
    built = []
    for line in edges:
        if line.split()[1] in LEARNED:
            break
        built.append(line)
    return lines[0], reasons, cells, nodes, built


def unvalidated(line):
    parts = line.split()
    parts[VALIDATED] = '0'
    return ' '.join(parts)


def outcomes(log_text):
    """{(edge index, type name): [successes, failures]} of the edges a world tried."""
    counts = {}
    for index, kind, success in RESULT.findall(log_text):
        entry = counts.setdefault((int(index), kind), [0, 0])
        entry[0 if success == '1' else 1] += 1
    return counts


def prepare(worlds, keep_all=False):
    """worlds: [(cache text, server.log text)]. Returns (seed text, report)."""
    reference = None
    flown, failed = set(), set()
    for cache, log in worlds:
        header, reasons, cells, nodes, built = split_cache(cache)
        shape = (header.split()[1:4], cells, nodes, [unvalidated(line) for line in built])
        if reference is None:
            reference = (header, reasons, cells, nodes, built, shape)
        elif shape != reference[5]:
            raise ValueError('worlds disagree on the built graph')
        for (index, kind), (ok, bad) in outcomes(log).items():
            if index >= len(built) or PRUNABLE.get(built[index].split()[1]) != kind:
                continue
            if ok:
                flown.add(index)
            if bad:
                failed.add(index)
    if reference is None:
        raise ValueError('no worlds')
    header, reasons, cells, nodes, built, _ = reference
    pruned = set() if keep_all else failed - flown
    kept = [unvalidated(line) for i, line in enumerate(built) if i not in pruned]
    header = re.sub(r' edges=\d+ ', ' edges=%d ' % len(kept), header)
    text = '\n'.join([header] + reasons + cells + nodes + kept) + '\n'
    report = dict(worlds=len(worlds), built=len(built), edges=len(kept))
    for code, name in PRUNABLE.items():
        members = {i for i, line in enumerate(built) if line.split()[1] == code}
        report[name] = '%d/flown %d/failed %d/pruned %d' % (len(members), len(flown & members),
                                                            len(failed & members), len(pruned & members))
    return text, report


def world_dirs(campaigns, map_name, pattern, exclude=None):
    for campaign in campaigns:
        for result in sorted(Path(campaign).glob('*/*/result.json')):
            directory = result.parent
            path = directory.as_posix()
            if '.miss' in directory.name or (pattern and not re.search(pattern, path)) or \
               (exclude and re.search(exclude, path)):
                continue
            caches = list((directory / 'base' / 'bot2-graph').glob('*.txt'))
            if len(caches) != 1:
                continue
            head = caches[0].read_text(encoding='utf-8', errors='replace').split('\n', 1)[0]
            if ' map=%s ' % map_name not in head:
                continue
            yield caches[0], directory / 'server.log'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('campaigns', nargs='+', type=Path)
    parser.add_argument('--map', required=True, help="the cache header's map name, e.g. mp/ctf_talravin")
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--filter', help='regular expression on world directories (forward slashes)')
    parser.add_argument('--exclude', help='regular expression on world directories to leave out')
    parser.add_argument('--keep-all', action='store_true', help='leave out nothing (the control seed)')
    args = parser.parse_args()
    found = list(world_dirs(args.campaigns, args.map, args.filter, args.exclude))
    if not found:
        sys.exit('no worlds of %s' % args.map)
    names = {cache.name for cache, _ in found}
    if len(names) != 1:
        sys.exit('worlds name different caches: %s' % sorted(names))
    worlds = [(cache.read_text(encoding='utf-8', errors='replace'), log.read_text(encoding='utf-8', errors='replace'))
              for cache, log in found]
    text, report = prepare(worlds, args.keep_all)
    if args.out.name != names.pop():
        sys.exit('--out must be named as the cache: %s' % found[0][0].name)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding='utf-8', newline='\n')
    print(' '.join('%s=%s' % item for item in report.items()))


if __name__ == '__main__':
    main()
