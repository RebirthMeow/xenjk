"""movement_graph_prepare.py: candidates failed and never flown leave the built graph; nothing learned stays."""
import unittest

import movement_graph_prepare as prepare

HEADER = ('graph 7 map=mp/ctf_x checksum=-1 grid=32 gravity=800 speed=250 step=18 cells=2 nodes=1 '
          'edges={n} seeds=0 seed_refused=0 samples=0 refusals=0 links=0 refined=0 boundary=0 capped=0 hints=0 hints_bound=0')
CELLS = ['c 0 0 0.000 1 -1 -1 -1 0 0 0 0 -1', 'c 1 0 0.000 -1 0 -1 -1 0 0 0 0 -1']
NODES = ['n 0 0 2 0.000 0.000 32.0 0']


def edge(kind, src, dst, validated=0, cost=500):
    return (f'e {kind} {src} {dst} 0.000 0.000 0.000 0.000 0.000 0.000 1.000000 0.000000 {cost} {validated} '
            f'0 0.0 0.000000 0.000000 0.0 0.000000 0.000000')


def cache(edges):
    return '\n'.join([HEADER.format(n=len(edges)), 'r fill 0'] + CELLS + NODES + edges) + '\n'


def results(*items):
    return ''.join(f'[GRAPH] edge_result index={i} type={t} success={s} traversals=0 failures=0 disabled=0\n'
                   for i, t, s in items)


BUILT = [edge(2, 0, 1), edge(3, 0, 1), edge(3, 1, 0), edge(3, 0, 1, cost=700), edge(4, 1, 0)]


class Prepare(unittest.TestCase):
    def test_prunes_only_drops_failed_everywhere(self):
        worlds = [(cache(BUILT + [edge(5, 0, 1)]), results((1, 'drop', 0), (2, 'drop', 0), (4, 'jump', 0))),
                  (cache([BUILT[0], BUILT[1].replace(' 500 0 ', ' 500 1 ')] + BUILT[2:]), results((1, 'drop', 1), (2, 'drop', 0)))]
        text, report = prepare.prepare(worlds)
        edges = [line for line in text.splitlines() if line.startswith('e ')]
        self.assertIn('pruned 1', report['drop'])              # drop 2: failed twice, never flown
        self.assertEqual(len(edges), 3)                         # and the jump that failed, flown nowhere
        self.assertNotIn(BUILT[2], edges)
        self.assertTrue(all(line.split()[1] != '5' for line in edges))   # nothing learned
        self.assertTrue(all(line.split()[13] == '0' for line in edges))  # validated reset
        self.assertIn(' edges=3 ', text.splitlines()[0])

    def test_prunes_every_candidate_type_but_lifts(self):
        built = [edge(2, 0, 1), edge(4, 1, 0), edge(6, 0, 1), edge(7, 1, 0)]
        worlds = [(cache(built), results((0, 'pad', 0), (1, 'jump', 0), (2, 'climb', 1), (2, 'climb', 0), (3, 'lift', 0)))]
        text, report = prepare.prepare(worlds)
        edges = [line.split()[1] for line in text.splitlines() if line.startswith('e ')]
        self.assertEqual(edges, ['6', '7'])                     # pad and jump pruned; the flown climb and the lift stay

    def test_outcome_type_must_match_the_edge(self):
        worlds = [(cache(BUILT), results((1, 'jump', 0)))]      # an index read against another world's numbering
        _, report = prepare.prepare(worlds)
        self.assertEqual(report['edges'], len(BUILT))

    def test_keep_all_is_the_built_graph(self):
        worlds = [(cache(BUILT + [edge(5, 0, 1)]), results((2, 'drop', 0)))]
        text, report = prepare.prepare(worlds, keep_all=True)
        self.assertTrue(all('pruned 0' in report[k] for k in ('pad', 'drop', 'jump', 'climb')))
        self.assertEqual(sum(1 for line in text.splitlines() if line.startswith('e ')), len(BUILT))

    def test_refuses_worlds_that_disagree(self):
        worlds = [(cache(BUILT), ''), (cache(BUILT[:-1]), '')]
        with self.assertRaises(ValueError):
            prepare.prepare(worlds)


if __name__ == '__main__':
    unittest.main()
