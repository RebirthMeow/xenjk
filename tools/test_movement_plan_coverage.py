import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from movement_plan_coverage import (classify, cover, cross_check, load_cache,
                                    nearest_cell, trace)

# A hand-built cache: one column of cells, one strafe edge and one hop edge,
# written in the game's own record order so the strafe edge keeps index 1.
CACHE = '\n'.join([
    'graph 4 map=mp/test checksum=1 grid=32 gravity=800 speed=250 step=18 '
    'cells=2 nodes=0 edges=2 seeds=0 seed_refused=0 samples=0 refusals=0 '
    'links=0 refined=0 boundary=0 capped=0 hints=0 hints_bound=0',
    'c 3 0 0.000 -1 -1 -1 -1 0 0 0 -1 -1',
    'c 9 0 0.000 -1 -1 -1 -1 0 0 0 -1 -1',
    'e 1 0 1 96.000 0.000 0.000 300.000 0.000 0.000 1.000000 0.000000 500 1 '
    '0 400.0 1.000000 0.000000 500.0 1.000000 0.000000',
    'e 5 0 1 96.000 0.000 0.000 300.000 0.000 0.000 1.000000 0.000000 500 1 '
    '1 400.0 1.000000 0.000000 500.0 1.000000 0.000000',
    ''])


def state(speed=400.0, direction=(1.0, 0.0), tap=1, origin=(100.0, 0.0, 0.0)):
    return dict(origin=origin, speed=speed, dir=direction, tap=tap,
                heading_source='finite_difference')


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        path = Path(self.directory.name) / 'cache.txt'
        path.write_text(CACHE, encoding='utf-8')
        self.cache = load_cache(path)

    def test_only_strafe_edges_are_kept_under_their_game_index(self):
        self.assertEqual(2, self.cache['edge_records'])
        self.assertEqual([1], [e['edge'] for e in self.cache['edges']])
        self.assertEqual(1, self.cache['by_record'][1]['tap'])

    def test_the_root_cell_is_the_nearest_column_inside_the_radius(self):
        self.assertEqual(0, nearest_cell(self.cache, (100.0, 0.0, 0.0)))
        self.assertEqual(1, nearest_cell(self.cache, (290.0, 0.0, 0.0)))
        # Beyond Graph_Nearest's 64-unit radius, and beyond its height
        # tolerance, there is no root cell rather than a distant one.
        self.assertEqual(-1, nearest_cell(self.cache, (190.0, 0.0, 0.0)))
        self.assertEqual(-1, nearest_cell(self.cache, (100.0, 0.0, 100.0)))

    def test_a_state_inside_the_envelope_is_covered(self):
        result = classify(self.cache, 0, state())
        self.assertEqual('covered', result['verdict'])
        self.assertEqual(1, result['admitting'])
        self.assertEqual(1, result['attached'])

    def test_one_axis_over_names_that_axis_and_its_margin(self):
        result = classify(self.cache, 0, state(speed=500.0))
        self.assertEqual('one_axis', result['verdict'])
        self.assertEqual('speed', result['axis'])
        self.assertAlmostEqual(52.0, result['margin'])
        cone = classify(self.cache, 0, state(direction=(0.0, 1.0)))
        self.assertEqual('cone', cone['axis'])
        self.assertAlmostEqual(60.0, cone['margin'])

    def test_a_tap_edge_refuses_a_tapless_state_and_is_not_a_near_miss(self):
        result = classify(self.cache, 0, state(tap=0))
        self.assertEqual('one_axis', result['verdict'])
        self.assertEqual('tap', result['axis'])
        far = classify(self.cache, 0, state(tap=0, speed=900.0))
        self.assertEqual('no_edge_near', far['verdict'])

    def test_edges_beyond_twice_the_reach_are_not_considered(self):
        result = classify(self.cache, 1, state(origin=(290.0, 0.0, 0.0)))
        self.assertEqual('no_edge_near', result['verdict'])
        self.assertEqual(0, result['nearby'])

    def test_an_off_graph_state_is_reported_rather_than_snapped(self):
        self.assertEqual('off_graph', classify(self.cache, -1, state())['verdict'])

    def test_the_cross_check_names_the_axis_it_disagrees_on(self):
        logged = [dict(level=500, state=state(), edge=1, envelope=1),
                  dict(level=525, state=state(speed=600.0), edge=1, envelope=1),
                  dict(level=550, state=None, reason='no_position_record')]
        result = cross_check(lambda row: self.cache, logged)
        self.assertEqual(2, result['compared'])
        self.assertEqual(1, result['tallies']['agree'])
        self.assertEqual({'speed': 1}, result['disagreement_axes'])

    def test_cover_counts_a_missing_state_without_scoring_it(self):
        rows = [dict(case='c', level=500, owner='precommit_grounded', state=None),
                dict(case='c', level=600, owner='precommit_grounded', state=state())]
        result = cover(lambda row: self.cache, rows, ('reach_xy', 'reach_z', 'speed', 'cone', 'tap'))
        self.assertEqual({'no_state': 1, 'covered': 1}, result['verdicts'])
        self.assertEqual(0.5, result['covered_fraction'])


class TraceTests(unittest.TestCase):
    def fixture(self):
        momentum = []
        for level in range(125, 1001, 25):
            momentum.append('wall [MOMENTUM] level=%d speed=400.0 ground=%d'
                            % (level, int(level >= 500)))
        events = [
            'wall [MOVE] client=0 level=200 event=edge edge=hop0 reason=launch '
            'speed=400.0 x=0.0 y=0.0 z=0.0',
            'wall [MOVE] client=0 level=500 event=edge edge=hop0 reason=land '
            'speed=400.0 x=100.0 y=0.0 z=0.0',
            'wall [MOVE] client=0 level=525 event=edge edge=strafe reason=refused '
            'speed=400.0 x=110.0 y=0.0 z=0.0 index=1 tap=1 envelope=1 landing=0 '
            'live_speed=400.0 entry_speed=400.0',
            'wall [MOVE] client=0 level=600 event=plan edge=hop0 reason=sequence '
            'speed=400.0 x=140.0 y=0.0 z=0.0',
            'wall [MOVE] client=0 level=625 event=edge edge=hop0 reason=launch '
            'speed=400.0 x=150.0 y=0.0 z=0.0']
        return '\n'.join(momentum + events)

    def run_trace(self):
        csv = 'level_time,carrying\n100,0\n1000,0\n1250,1\n'
        def read(path, *args, **kwargs):
            return (json.dumps({'corner_level': 100, 'evaluation': {}})
                    if path.name == 'result.json' else self.fixture())
        with patch.object(Path, 'read_text', read), \
             patch.object(Path, 'read_bytes', return_value=b'bound evidence'), \
             patch.object(Path, 'glob', return_value=[Path('play.csv')]), \
             patch.object(Path, 'open', return_value=io.StringIO(csv)):
            return trace(Path('synthetic'))

    def test_the_waiting_state_is_the_runs_first_frame(self):
        rows, _, _ = self.run_trace()
        waiting = [r for r in rows if r['role'] == 'waiting']
        self.assertEqual(1, len(waiting))
        self.assertEqual(500, waiting[0]['level'])
        self.assertEqual((100.0, 0.0, 0.0), waiting[0]['state']['origin'])
        self.assertEqual(400.0, waiting[0]['state']['speed'])

    def test_the_heading_prefers_the_next_frame_over_the_launch_chord(self):
        rows, _, _ = self.run_trace()
        waiting = next(r for r in rows if r['role'] == 'waiting')
        self.assertEqual('finite_difference', waiting['state']['heading_source'])
        self.assertAlmostEqual(1.0, waiting['state']['dir'][0])
        # Both reconstructions exist here and agree, so the error is zero
        # rather than absent.
        self.assertAlmostEqual(0.0, waiting['heading_error_degrees'])

    def test_the_tap_bit_is_the_landing_frame(self):
        rows, _, _ = self.run_trace()
        waiting = next(r for r in rows if r['role'] == 'waiting')
        committed = next(r for r in rows if r['role'] == 'committed')
        self.assertEqual(1, waiting['state']['tap'])
        self.assertEqual(600, committed['level'])
        self.assertEqual(0, committed['state']['tap'])

    def test_logged_envelope_records_are_collected_with_their_own_state(self):
        _, logged, _ = self.run_trace()
        self.assertEqual(1, len(logged))
        self.assertEqual((1, 1), (logged[0]['edge'], logged[0]['envelope']))
        self.assertEqual(400.0, logged[0]['state']['speed'])


if __name__ == '__main__':
    unittest.main()
