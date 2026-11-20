import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from movement_momentum_audit import audit, combine


class MomentumAuditTests(unittest.TestCase):
    def fixture(self, prefix='wall-clock A '):
        # Fixture at100, last noncarrying sample1000, first carrying1250.
        # Include a same-level prefixture landing and a window crossing1000.
        lines = [prefix + '[MOVE] level=75 event=plan reason=sequence hops=1',
                 prefix + '[MOVE] level=100 event=edge reason=land edge=hop0']
        for t in range(100, 1276, 25):
            lines.append(prefix + '[MOMENTUM] level=%d speed=400 ground=1' % t)
        lines += [prefix + '[MOVE] level=125 event=plan reason=sequence hops=1',
                  prefix + '[MOVE] level=400 event=edge reason=land edge=hop0',
                  prefix + '[MOVE] level=425 event=plan reason=sequence hops=2',
                  prefix + '[MOVE] level=500 event=edge reason=land edge=hop0',
                  prefix + '[MOVE] level=525 event=edge reason=land edge=hop1']
        return '\n'.join(lines)

    def run_audit(self, text):
        csv = 'level_time,carrying\n100,0\n750,0\n1000,0\n1250,1\n'
        def read(path, *args, **kwargs):
            return json.dumps({'corner_level': 100}) if path.name == 'result.json' else text
        with patch.object(Path, 'read_text', read), \
             patch.object(Path, 'read_bytes', return_value=b'bound evidence'), \
             patch.object(Path, 'glob', return_value=[Path('play.csv')]), \
             patch.object(Path, 'open', return_value=io.StringIO(csv)):
            return audit(Path('synthetic'))

    def test_full_window_and_fixture_boundaries(self):
        report = self.run_audit(self.fixture())
        self.assertEqual([400, 500], [x['level'] for x in report['landings']])
        self.assertEqual(1000, report['end_inclusive'])
        self.assertEqual({'before_or_at_fixture': 1,
                          'window_not_proven_before_pickup': 1}, report['excluded'])

    def test_hop_zero_is_not_necessarily_mid_chain(self):
        report = self.run_audit(self.fixture())
        self.assertEqual(['end', 'middle'], [x['sequence_position'] for x in report['landings']])

    def test_missing_intermediate_frame_excludes_window(self):
        text = '\n'.join(x for x in self.fixture().splitlines() if '[MOMENTUM] level=475 ' not in x)
        report = self.run_audit(text)
        self.assertEqual([500], [x['level'] for x in report['landings']])
        self.assertEqual(1, report['excluded']['missing_frame'])

    def test_duplicates_ignore_wall_clock_but_not_telemetry(self):
        a, b = self.run_audit(self.fixture()), self.run_audit(self.fixture('different date '))
        report = combine([a, b])
        self.assertEqual(4, report['pooled']['all']['count'])
        self.assertEqual(2, report['unique_traces']['all']['count'])
        c = self.run_audit(self.fixture().replace('speed=400', 'speed=401'))
        self.assertNotEqual(a['trace_sha256'], c['trace_sha256'])

    def test_all_engine_admission_outcomes_count_as_tap_alternatives(self):
        for outcome in ('admitted', 'arrives', 'rerouted', 'tap_onward'):
            with self.subTest(outcome=outcome):
                text = self.fixture() + '\n[SPIKE] level=400 r0=0 outcome=' + outcome
                self.assertTrue(self.run_audit(text)['landings'][0]['admitted_tap'])


if __name__ == '__main__':
    unittest.main()
