import unittest

from movement_landing_report import candidate_class, extract, relative_trace, selected_runs


class LandingReportTests(unittest.TestCase):
    def run_fixture(self):
        return dict(case='world/case', start_exclusive=100, landings=[dict(
            level=500, grounded_next=True, sequence_position='end', action='commit',
            admitted_tap=False, hop=0, committed_hops=1, commit_runup='24', reissued_up='0')])

    def test_trigger_bit_never_becomes_a_fall(self):
        self.assertEqual('trigger_contact_unknown', candidate_class(dict(outcome='refused', unsupported='256')))
        self.assertEqual('trigger_contact_unknown', candidate_class(dict(outcome='refused', unsupported='257')))
        self.assertEqual('selected_path_rejection', candidate_class(dict(outcome='off_path', unsupported='0')))

    def test_same_level_different_client_and_nonzero_runup_not_included(self):
        text = ('[MOVE] client=0 level=500 event=edge reason=land x=1 y=2 z=3 speed=400\n'
                '[SPIKE] client=1 level=500 r0=0 outcome=admitted unsupported=0\n'
                '[SPIKE] client=0 level=500 r0=24 outcome=admitted unsupported=0\n'
                '[SPIKE] client=0 level=500 r0=0 outcome=off_path unsupported=0\n')
        frame = extract(self.run_fixture(), text)[0]
        self.assertEqual(1, len(frame['tap_candidates']))
        self.assertEqual(4, frame['tap_candidates'][0]['line'])
        self.assertEqual([1., 2., 3.], frame['position'])

    def test_no_candidate_does_not_invent_skip_reason(self):
        run = self.run_fixture()
        run['landings'][0]['action'] = 'no_recorded_straight_search'
        frame = extract(run, '[MOVE] client=0 level=500 event=edge reason=land x=1 y=2 z=3 speed=400')[0]
        self.assertEqual('no_candidate_evidence', frame['classification'])

    def test_conflicting_audit_is_refused_for_all_admitted_outcomes(self):
        for outcome in ('admitted', 'arrives', 'rerouted', 'tap_onward'):
            with self.subTest(outcome=outcome), self.assertRaises(ValueError):
                extract(self.run_fixture(),
                        '[MOVE] client=0 level=500 event=edge reason=land x=1 y=2 z=3 speed=400\n'
                        '[SPIKE] client=0 level=500 r0=0 outcome=' + outcome)

    def test_duplicate_observed_traces_are_not_recounted(self):
        runs = [dict(trace_sha256='a', case='r1'), dict(trace_sha256='a', case='r2'),
                dict(trace_sha256='b', case='r3')]
        self.assertEqual(['r1', 'r3'], [x['case'] for x in selected_runs(runs)])

    def test_relative_comparison_removes_clock_offset_only(self):
        a = relative_trace(dict(start_exclusive=100),
                           '[MOMENTUM] level=100 speed=5 up=0\n'
                           '[MOMENTUM] level=125 speed=400 up=1\n', 25)
        b = relative_trace(dict(start_exclusive=300),
                           '[MOMENTUM] level=325 speed=400 up=1\n'
                           '[MOMENTUM] level=350 speed=999 up=1\n', 25)
        self.assertEqual(a, b)
        c = relative_trace(dict(start_exclusive=300),
                           '[MOMENTUM] level=325 speed=400 up=0\n', 25)
        self.assertNotEqual(a, c)


if __name__ == '__main__':
    unittest.main()
