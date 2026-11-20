import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from movement_leg_budget import attribute, budget, combine


class LegBudgetTests(unittest.TestCase):
    def fixture(self, samples, events=(), candidates=()):
        lines = ['wall-clock [MOMENTUM] level=%d speed=%s ground=%d' % row
                 for row in samples]
        lines += list(events) + list(candidates)
        return '\n'.join(lines)

    def run_budget(self, text, end=1000, evaluation=None):
        csv = 'level_time,carrying\n100,0\n%d,0\n%d,1\n' % (end, end + 250)
        def read(path, *args, **kwargs):
            return json.dumps({'corner_level': 100, 'evaluation': evaluation or {}}) if path.name == 'result.json' else text
        with patch.object(Path, 'read_text', read), \
             patch.object(Path, 'read_bytes', return_value=b'bound evidence'), \
             patch.object(Path, 'glob', return_value=[Path('play.csv')]), \
             patch.object(Path, 'open', return_value=io.StringIO(csv)):
            return budget(Path('synthetic'))

    def straight(self, start, end, speed='400', ground=0):
        return [(t, speed, ground) for t in range(start, end + 1, 25)]

    def test_leg_excludes_the_fixture_level_and_ends_before_pickup(self):
        report = self.run_budget(self.fixture(self.straight(100, 1250)))
        # 100 is the fixture's own level and is excluded; 1000 is the last
        # noncarrying sample and is kept.
        self.assertEqual(125, report['start_exclusive'] + 25)
        self.assertEqual(1000, report['end_inclusive'])
        self.assertEqual(36, report['frames'])

    def test_incomplete_cadence_is_rejected_rather_than_interpolated(self):
        samples = [x for x in self.straight(100, 1250) if x[0] != 500]
        with self.assertRaises(ValueError):
            self.run_budget(self.fixture(samples))

    def test_missing_window_boundaries_are_rejected(self):
        for start, end in ((375, 1000), (125, 750), (500, 500)):
            with self.subTest(start=start, end=end), self.assertRaises(ValueError):
                self.run_budget(self.fixture(self.straight(start, end)))

    def test_off_cadence_end_is_rejected(self):
        with self.assertRaises(ValueError):
            self.run_budget(self.fixture(self.straight(125, 1000)), end=1010)

    def test_terminal_grounded_run_does_not_claim_a_failed_search(self):
        samples = [(t, '400', int(t >= 800))
                   for t, _, _ in self.straight(100, 1250)]
        report = self.run_budget(self.fixture(samples))
        self.assertEqual(9, report['owners']['uncommitted_grounded']['frames'])
        self.assertEqual({}, report['waiting_candidate_outcomes'])
        self.assertNotIn('precommit_grounded', report['owners'])

    def test_analysis_window_and_official_completion_are_distinct(self):
        report = self.run_budget(self.fixture(self.straight(100, 1250)),
                                 evaluation={'status': 'pass', 'time_ms': 1150})
        self.assertEqual(900, report['analysis_window_ms'])
        self.assertEqual(900, report['leg_ms'])
        self.assertEqual(1150, report['completion_ms'])
        combined = combine([report], 15000)
        self.assertEqual('LEG_BUDGET2', combined['format'])
        self.assertIn('compatibility alias', combined['semantics']['leg_ms'])

    def test_failed_evaluation_does_not_supply_completion_time(self):
        report = self.run_budget(self.fixture(self.straight(100, 1250)),
                                 evaluation={'status': 'fail', 'time_ms': 1150})
        self.assertIsNone(report['completion_ms'])

    def test_touchdown_speed_uses_grounded_boundary_and_never_lowers_samples(self):
        speeds = [490.1, 584.6, 600.0, 250.0, 400.0]
        levels = [125, 150, 175, 200, 225]
        leg = dict(levels=levels,
                   momentum={t: {'speed': str(v), 'ground': int(i in (1, 2, 3))}
                             for i, (t, v) in enumerate(zip(levels, speeds))},
                   events=[], candidates={})
        owners, _, _, observed, _, hypothetical = attribute(leg)
        self.assertEqual(584.6, owners[0]['entry_speed'])
        self.assertEqual([490.1, 584.6, 600.0, 584.6, 400.0], hypothetical)
        self.assertTrue(all(a >= b for a, b in zip(hypothetical, observed)))

    def test_stationary_window_has_finite_hypothetical_duration(self):
        report = self.run_budget(self.fixture(self.straight(100, 1250, speed='0')))
        self.assertEqual(900, report['hypothetical_analysis_window_ms'])
        self.assertEqual(0.0, combine([report], 15000)['pooled']['distance_units_spread'])

    def test_single_grounded_frame_does_not_infer_a_tap(self):
        samples = self.straight(100, 1250)
        samples = [(t, s, 1 if t == 500 else 0) for t, s, _ in samples]
        report = self.run_budget(self.fixture(samples))
        self.assertEqual(1, report['owners']['single_grounded_frame']['frames'])
        self.assertNotIn('precommit_grounded', report['owners'])

    def test_grounded_run_splits_at_the_commit_that_ends_it(self):
        # Grounded 500..700; the commit lands at 600, so 500..575 is the
        # precommit grounded interval and 600..700 is the committed run-up.
        samples = [(t, '400', 1 if 500 <= t <= 700 else 0)
                   for t, _, _ in self.straight(100, 1250)]
        events = ['wall-clock [MOVE] level=600 event=plan reason=sequence r0=24 hops=2']
        report = self.run_budget(self.fixture(samples, events))
        self.assertEqual(4, report['owners']['precommit_grounded']['frames'])
        self.assertEqual(5, report['owners']['runup_before_launch']['frames'])

    def test_a_zero_runup_commit_is_not_charged_as_a_runup(self):
        samples = [(t, '400', 1 if 500 <= t <= 700 else 0)
                   for t, _, _ in self.straight(100, 1250)]
        events = ['wall-clock [MOVE] level=600 event=plan reason=sequence r0=0 hops=2']
        report = self.run_budget(self.fixture(samples, events))
        self.assertNotIn('runup_before_launch', report['owners'])
        self.assertEqual(5, report['owners']['committed_grounded_without_runup']['frames'])

    def test_waiting_candidates_separate_trigger_contact_from_other_refusals(self):
        samples = [(t, '400', 1 if 500 <= t <= 700 else 0)
                   for t, _, _ in self.straight(100, 1250)]
        candidates = [
            'wall-clock [SPIKE] level=525 cand=0 r0=0 outcome=refused unsupported=256',
            'wall-clock [SPIKE] level=525 cand=1 r0=0 outcome=refused unsupported=0',
            'wall-clock [SPIKE] level=550 cand=2 r0=0 outcome=fell unsupported=0']
        events = ['wall-clock [MOVE] level=600 event=plan reason=sequence r0=24 hops=2']
        report = self.run_budget(self.fixture(samples, events, candidates))
        self.assertEqual({'refused/trigger_contact_unknown': 1, 'refused': 1, 'fell': 1},
                         report['waiting_candidate_outcomes'])

    def test_counterfactual_holds_entry_speed_and_never_slows_the_leg(self):
        samples = [(t, '250' if 525 <= t <= 700 else '500', 1 if 500 <= t <= 700 else 0)
                   for t, _, _ in self.straight(100, 1250)]
        events = ['wall-clock [MOVE] level=600 event=plan reason=sequence r0=24 hops=2']
        report = self.run_budget(self.fixture(samples, events))
        self.assertGreater(report['counterfactual_mean_speed'], report['mean_speed'])
        self.assertLess(report['counterfactual_leg_ms'], report['leg_ms'])

    def test_pooled_required_speed_uses_the_target_and_mean_distance(self):
        report = self.run_budget(self.fixture(self.straight(100, 1250)))
        pooled = combine([report], 15000)['pooled']
        self.assertAlmostEqual(pooled['distance_units_mean'] / 15.0,
                               pooled['mean_speed_required'])
        self.assertEqual(0.0, pooled['distance_units_spread'])


if __name__ == '__main__':
    unittest.main()
