"""movement_graph_enrich.py on hand-built worlds: indices resolve against each world's own cache."""
import json
from pathlib import Path
import tempfile
import unittest

import movement_graph_enrich as enrich

HEADER = ('graph 7 map=mp/ctf_kejim checksum=-1 grid=32 gravity=800 speed=250 step=18 cells=4 nodes=1 '
          'edges={n} seeds=0 seed_refused=0 samples=0 refusals=0 links=0 refined=0 boundary=0 capped=0 hints=0 hints_bound=0')
CELLS = ['c 0 0 0.000 1 -1 -1 -1 0 0 0 0 -1', 'c 1 0 0.000 2 0 -1 -1 0 0 0 0 -1',
         'c 2 0 0.000 3 1 -1 -1 0 0 0 0 -1', 'c 3 0 0.000 -1 2 -1 -1 0 0 0 0 -1']
NODES = ['n 0 0 4 0.000 0.000 32.0 0']


def edge(kind, src, dst, cost, tap=0, entry=250.0, x=0.0):
    return (f'e {kind} {src} {dst} {x:.3f} 0.000 0.000 0.000 0.000 0.000 1.000000 0.000000 {cost} 1 '
            f'{tap} {entry:.1f} 1.000000 0.000000 400.0 1.000000 0.000000')


def cache_text(edges, cells=CELLS):
    return '\n'.join([HEADER.format(n=len(edges)), 'r fill 0'] + cells + NODES + edges) + '\n'


def commit(level, index, index2=-1, c0=-1, c1=-1):
    return (f'[MOVE] client=0 level={level} event=plan edge=hop0 reason=sequence speed=250.0 kind=state '
            f'hops=2 c0={c0} c1={c1} index={index} index2={index2} state_candidates=1\n')


BASE = [edge(2, 0, 1, 500), edge(5, 1, 2, 900, entry=260.0)]


def world(root, name, edges, log, result=None, cells=CELLS):
    directory = Path(root) / name / 'case'
    (directory / 'base' / 'bot2-graph').mkdir(parents=True)
    (directory / 'base' / 'bot2-graph' / 'mp_ctf_kejim_-1.txt').write_text(cache_text(edges, cells), encoding='utf-8')
    (directory / 'server.log').write_text(log, encoding='utf-8')
    if result is not None:
        (directory / 'result.json').write_text(json.dumps(result), encoding='utf-8')
    return directory


def map_load(edges, identity='g1-a'):
    return f'[GRAPH] map_load map=mp/ctf_kejim checksum=-1 graph={identity} cached=1 cells=4 edges={edges}\n'


class Worlds(object):
    """Hand-built worlds and a base cache in a temporary directory."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.base = self.root / 'v1.txt'
        self.base.write_text(cache_text(BASE), encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def run_enrich(self, committed=(), learned=(), identity=None, routes=(), routes_leg=False):
        return enrich.enrich(self.base, [self.root / c for c in committed],
                             [self.root / c for c in learned], identity, log=lambda *_: None,
                             routes=[self.root / c for c in routes], routes_leg=routes_leg)


class EnrichTest(Worlds, unittest.TestCase):

    def test_index_resolves_against_the_worlds_own_cache(self):
        # A seeded world: a different graph, where index 2 is a strafe edge 2->3.
        seeded = [edge(1, 3, 0, 700), edge(1, 2, 0, 800), edge(5, 2, 3, 600, entry=300.0)]
        world(self.root / 'camp', 'w1', seeded,
              map_load(3, 'g1-seed') + commit(100, 2, -1, c0=3) + commit(200, -1, -1))
        cache, summary, campaigns, _ = self.run_enrich(committed=['camp'])
        header, _, _, _, edges = cache
        self.assertEqual(edges[:2], BASE)
        self.assertEqual(edges[2], seeded[2])
        self.assertIn(' edges=3 ', header)
        stats = campaigns[0]
        self.assertEqual((stats['named'], stats['resolved'], stats['distinct'], stats['in_base'], stats['new']),
                         (1, 1, 1, 0, 1))
        self.assertEqual(stats['landing_agrees'], 1)

    def test_already_in_base_and_lower_cost_folds(self):
        # The world loaded the base; it commits base edge 1 with a cheaper measurement in its save.
        saved = [BASE[0], edge(5, 1, 2, 700, entry=270.0)]
        world(self.root / 'camp', 'w1', saved, map_load(2) + commit(100, 1, c0=2))
        cache, summary, campaigns, _ = self.run_enrich(committed=['camp'])
        edges = cache[4]
        self.assertEqual(len(edges), 2)
        self.assertEqual(edges[1], saved[1])
        self.assertEqual(summary['base_records_replaced'], 1)
        self.assertEqual((campaigns[0]['in_base'], campaigns[0]['new']), (1, 0))

    def test_pre_release_appended_index_is_refused_after_a_dropping_reset(self):
        # Pre-release the world appended index 2 (1->3); the release dropped it and
        # a post-release append reused index 2 for 3->2.
        saved = BASE + [edge(5, 3, 2, 650, entry=400.0)]
        log = (map_load(2) + '[GRAPH] edge_added index=2 type=strafe from=1 to=3 cost_ms=500\n'
               + commit(1000, 2) + '[GRAPH] edge_added index=2 type=strafe from=3 to=2 cost_ms=650\n'
               + commit(4000, 2, c0=2))
        world(self.root / 'camp', 'w1', saved, log,
              result=dict(corner_level=3000, corner_response='[GRAPH] reset_learned dropped=1 restored=0'))
        _, _, campaigns, _ = self.run_enrich(committed=['camp'])
        stats = campaigns[0]
        self.assertEqual(stats['refused'], {'dropped_by_reset': 1})
        self.assertEqual((stats['resolved'], stats['new']), (1, 1))

    def test_added_line_disagreeing_with_save_and_beyond_cache_are_refused(self):
        log = (map_load(2) + '[GRAPH] edge_added index=1 type=strafe from=0 to=3 cost_ms=1\n'
               + commit(100, 1) + commit(200, 9))
        world(self.root / 'camp', 'w1', BASE, log)
        _, _, campaigns, _ = self.run_enrich(committed=['camp'])
        self.assertEqual(campaigns[0]['refused'], {'added_mismatch': 1, 'beyond_cache': 1})
        self.assertEqual(campaigns[0]['resolved'], 0)

    def test_learned_edges_are_the_save_past_the_loaded_count(self):
        saved = BASE + [edge(5, 2, 3, 400, entry=500.0)]
        world(self.root / 'prod', 'w1', saved, map_load(2, 'g1-v1'))
        world(self.root / 'prod', 'w2', saved + [edge(1, 3, 1, 300)], map_load(2, 'g1-other'))
        cache, summary, campaigns, _ = self.run_enrich(learned=['prod'], identity='g1-v1')
        self.assertEqual(cache[4], saved)
        self.assertEqual(campaigns[0]['learned_edges'], 1)
        self.assertEqual(campaigns[0]['refused'], {'other_graph': 1})
        self.assertEqual(summary['new_learned'], 1)

    def test_a_world_on_other_cells_is_not_folded(self):
        cells = CELLS[:3] + ['c 9 9 0.000 -1 2 -1 -1 0 0 0 0 -1']
        world(self.root / 'camp', 'w1', BASE + [edge(5, 2, 3, 1, entry=100.0)],
              map_load(3) + commit(100, 2), cells=cells)
        cache, _, campaigns, _ = self.run_enrich(committed=['camp'])
        self.assertEqual(campaigns[0]['floor_mismatch'], 1)
        self.assertEqual(cache[4], BASE)

    def test_main_writes_cache_and_manifest(self):
        world(self.root / 'camp', 'w1', BASE + [edge(5, 2, 3, 1, entry=100.0)], map_load(3) + commit(100, 2))
        out, manifest = self.root / 'out' / 'v2.txt', self.root / 'out' / 'v2.json'
        enrich.main([str(self.base), '--out', str(out), '--manifest', str(manifest),
                     '--committed', str(self.root / 'camp')])
        self.assertEqual(len([l for l in out.read_text(encoding='utf-8').splitlines() if l.startswith('e ')]), 3)
        data = json.loads(manifest.read_text(encoding='utf-8'))
        self.assertEqual(data['summary']['out_edges'], 3)
        self.assertEqual(len(data['inputs']), 1)
        self.assertEqual(len(data['inputs'][0]['cache_sha256']), 64)


def route(gen, total, first, edges, source='replan', client=0, level=2525):
    listed = ','.join(str(e) for e in edges) if edges else '-'
    return (f'2026-09-22 09:31:39 [ROUTE] client={client} level={level} gen={gen} source={source} from=7 to=1 '
            f'cells=20 cost_ms=13306 state=1 graph=g1-9631cbeaef2e5ba8 total={total} first={first} '
            f'count={len(edges)} edges={listed}\n')


class RouteFoldTest(Worlds, unittest.TestCase):
    """Planned routes fold like commits: each index against the world's own save."""

    def test_route_edges_resolve_against_the_worlds_own_cache(self):
        # A seeded world whose route flies 2 (a strafe edge 2->3 only it has) and 1;
        # nothing names 2 on a commit, which is the case the route fold exists for.
        seeded = [edge(1, 3, 0, 700), edge(5, 1, 2, 900, entry=260.0), edge(5, 2, 3, 600, entry=300.0)]
        log = (map_load(3, 'g1-seed') + commit(100, -1, c0=3)
               + route(1, 2, 0, [1, 2], level=90).replace('g1-9631cbeaef2e5ba8', 'g1-seed'))
        world(self.root / 'camp', 'w1', seeded, log)
        cache, summary, campaigns, _ = self.run_enrich(routes=['camp'])
        edges = cache[4]
        self.assertEqual(edges, BASE + [seeded[2]])
        stats = campaigns[0]
        self.assertEqual((stats['group'], stats['routes'], stats['route_indices'], stats['resolved']),
                         ('routes', 1, 2, 2))
        self.assertEqual((stats['distinct'], stats['in_base'], stats['new']), (2, 1, 1))
        self.assertEqual((summary['new_routes'], summary['new_routes_only']), (1, 1))

    def test_a_route_on_another_graph_or_missing_a_part_is_refused(self):
        log = (map_load(2, 'g1-v1') + route(1, 1, 0, [1], level=90)
               + route(2, 70, 0, [1], level=95).replace('g1-9631cbeaef2e5ba8', 'g1-v1'))
        world(self.root / 'camp', 'w1', BASE, log)
        _, _, campaigns, _ = self.run_enrich(routes=['camp'])
        self.assertEqual(campaigns[0]['refused'], {'other_graph': 1, 'incomplete': 1})
        self.assertEqual(campaigns[0]['resolved'], 0)

    def test_a_pre_release_route_index_dropped_by_the_reset_is_refused(self):
        saved = BASE + [edge(5, 3, 2, 650, entry=400.0)]
        ident = lambda line: line.replace('g1-9631cbeaef2e5ba8', 'g1-a')
        log = (map_load(2) + ident(route(1, 1, 0, [2], level=1000))
               + ident(route(2, 2, 0, [1, 2], level=4000)) + route(3, 1, 0, [9], level=4100).replace(
                   'g1-9631cbeaef2e5ba8', 'g1-a'))
        world(self.root / 'camp', 'w1', saved, log,
              result=dict(corner_level=3000, corner_response='[GRAPH] reset_learned dropped=1 restored=0'))
        cache, _, campaigns, _ = self.run_enrich(routes=['camp'])
        self.assertEqual(campaigns[0]['refused'], {'dropped_by_reset': 1, 'beyond_cache': 1})
        self.assertEqual(campaigns[0]['resolved'], 2)
        self.assertEqual(cache[4], saved)

    def test_leg_only_folds_the_routes_between_release_and_completion(self):
        # Pre-release free play (level 1000), the leg (3000 to 3000+16000) and
        # play after the flag (20000); the completion is on the run's summary.
        saved = BASE + [edge(5, 2, 3, 600, entry=300.0), edge(5, 3, 2, 650, entry=400.0),
                        edge(5, 0, 3, 700, entry=500.0)]
        ident = lambda line: line.replace('g1-9631cbeaef2e5ba8', 'g1-a')
        log = (map_load(5) + ident(route(1, 1, 0, [2], level=1000)) + ident(route(2, 1, 0, [3], level=3000))
               + ident(route(3, 1, 0, [4], level=20000)))
        case = world(self.root / 'camp', 'w1', saved, log, result=dict(corner_level=3000))
        (case.parent / 'result.json').write_text(json.dumps(dict(cases=[dict(name='case', time_ms=16000)])),
                                                 encoding='utf-8')
        cache, _, campaigns, _ = self.run_enrich(routes=['camp'], routes_leg=True)
        self.assertEqual(cache[4], BASE + [saved[3]])
        self.assertEqual(campaigns[0]['refused'], {'outside_leg': 2})
        cache, _, campaigns, _ = self.run_enrich(routes=['camp'])
        self.assertEqual(len(cache[4]), 5)

    def test_leg_only_without_a_completion_folds_nothing(self):
        log = map_load(2) + route(1, 1, 0, [1], level=3500).replace('g1-9631cbeaef2e5ba8', 'g1-a')
        world(self.root / 'camp', 'w1', BASE, log, result=dict(corner_level=3000))
        _, _, campaigns, _ = self.run_enrich(routes=['camp'], routes_leg=True)
        self.assertEqual(campaigns[0]['refused'], {'no_leg': 1})

    def test_main_takes_routes(self):
        seeded = BASE + [edge(5, 2, 3, 1, entry=100.0)]
        world(self.root / 'camp', 'w1', seeded,
              map_load(3) + route(1, 1, 0, [2], level=90).replace('g1-9631cbeaef2e5ba8', 'g1-a'))
        out, manifest = self.root / 'out' / 'v3.txt', self.root / 'out' / 'v3.json'
        enrich.main([str(self.base), '--out', str(out), '--manifest', str(manifest),
                     '--routes', str(self.root / 'camp')])
        data = json.loads(manifest.read_text(encoding='utf-8'))
        self.assertEqual((data['summary']['out_edges'], data['summary']['new_routes']), (3, 1))
        self.assertEqual(data['inputs'][0]['group'], 'routes')


class RouteLineTest(unittest.TestCase):
    """The [ROUTE] line's parser."""

    def test_one_line_route(self):
        routes, incomplete = enrich.read_routes([route(1, 3, 0, [680, 681, 2374])])
        self.assertEqual(incomplete, [])
        self.assertEqual(len(routes), 1)
        self.assertEqual(routes[0]['edges'], [680, 681, 2374])
        self.assertEqual((routes[0]['client'], routes[0]['generation'], routes[0]['source']), (0, 1, 'replan'))
        self.assertEqual(routes[0]['graph'], 'g1-9631cbeaef2e5ba8')
        self.assertEqual((routes[0]['origin'], routes[0]['to'], routes[0]['cost_ms'], routes[0]['level']),
                         (7, 1, 13306, 2525))

    def test_parts_join_in_order_and_routes_keep_installation_order(self):
        lines = ['[MOVE] client=0 level=2525 event=plan edge=none reason=path cells=20 edges=16\n',
                 route(4, 5, 3, [9, 10]), route(4, 5, 0, [1, 2, 3]),
                 route(2, 2, 0, [7, 8], source='reroute', level=2400)]
        routes, incomplete = enrich.read_routes(lines)
        self.assertEqual(incomplete, [])
        self.assertEqual([r['generation'] for r in routes], [4, 2])
        self.assertEqual(routes[0]['edges'], [1, 2, 3, 9, 10])
        self.assertEqual(routes[1]['source'], 'reroute')

    def test_a_route_with_no_edges(self):
        routes, _ = enrich.read_routes([route(3, 0, 0, [])])
        self.assertEqual(routes[0]['edges'], [])
        self.assertEqual(routes[0]['total'], 0)

    def test_a_missing_part_is_incomplete_not_guessed(self):
        routes, incomplete = enrich.read_routes([route(5, 70, 0, list(range(64)))])
        self.assertEqual(routes, [])
        self.assertEqual(len(incomplete), 1)
        self.assertEqual(len(incomplete[0]['edges']), 64)

    def test_a_count_that_disagrees_with_the_list_is_dropped(self):
        line = route(6, 3, 0, [1, 2, 3]).replace('count=3', 'count=4')
        self.assertEqual(enrich.read_routes([line]), ([], []))

    def test_clients_are_kept_apart(self):
        routes, _ = enrich.read_routes([route(1, 1, 0, [5], client=0), route(1, 1, 0, [6], client=1)])
        self.assertEqual([(r['client'], r['edges']) for r in routes], [(0, [5]), (1, [6])])


if __name__ == '__main__':
    unittest.main()
