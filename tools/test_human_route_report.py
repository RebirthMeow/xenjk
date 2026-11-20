"""In-memory tests: never delete files or manufacture engine substeps."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import human_route_report as report


def recording():
    metadata = dict(format='HUMAN_ROUTE1', human=True, auto_enemy_pickup=True, team=1, map='test', clock='native level')
    summary = dict(format='HUMAN_ROUTE1', complete=True, started=True, reason='enemy_flag_pickup',
                   rows=3, start_level=1000, end_level=1050, elapsed_ms=50, stationary_start=True)
    rows = []
    for i in range(3):
        row = dict(row=i, level=1000+25*i, elapsed_ms=25*i, cmd_time=1000+25*i, forward=127, right=0, up=127 if i==0 else 0)
        for prefix in ('pre', 'post', 'final'):
            row.update({prefix+'_'+key: 0 for key in ('x', 'y', 'z', 'vx', 'vy', 'vz', 'redflag', 'blueflag', 'active_powers')})
            row.update({prefix+'_x': i+(prefix!='pre'), prefix+'_ground': 1022, prefix+'_force': 100, prefix+'_cmd_time': 1000+25*i})
        rows.append(row)
    rows[-1]['final_blueflag'] = 1
    return rows, metadata, summary


class ObservationTests(unittest.TestCase):
    def test_nonfinite_and_missing_are_unknown(self):
        for value in ('nan', 'inf', '-inf', '', None, 'unknown'):
            self.assertIsNone(report.number(value))
        self.assertEqual(report.number('0'), 0)

    def test_path_does_not_bridge_missing_position(self):
        path = report.observed_path([[0, 0, 0], [3, 4, 12], None, [100, 100, 100]])
        self.assertEqual(path['horizontal_units'], 5)
        self.assertEqual(path['spatial_units'], 13)
        self.assertEqual(path['connected_segments'], 1)

    def test_sample_gap_is_observed_not_fabricated_airtime(self):
        cadence = report.cadence_facts([{'t': 0}, {'t': 25}, {'t': 100}, {'t': 100}, {'t': 90}], 't')
        self.assertEqual(cadence['observed_delta_ms_counts']['75.0'], 1)
        self.assertEqual(cadence['duplicate_timestamps'], 1)
        self.assertEqual(cadence['backwards_timestamps'], 1)
        self.assertNotIn('airtime_ms', cadence)

    def test_positive_up_runs_are_facts_not_force_claims(self):
        facts = report.input_facts([{'up': v} for v in [127, 127, 0, 127, 0]], 't', 'up')
        self.assertEqual(facts['positive_up_rows'], 3)
        self.assertEqual(facts['positive_up_runs'], 2)
        self.assertEqual(facts['consecutive_positive_up_runs'], 1)
        self.assertNotIn('force_spent', facts)

    def test_finish_time_requires_valid_complete_explicit_clock(self):
        summary = dict(valid=True, complete=True, first_input_time=100, pickup_time=15100, timing_clock='command_time')
        self.assertEqual(report.completion_time(summary)['time_ms'], 15000)
        for changes in ({'valid': False}, {'complete': False}, {'pickup_time': 99}, {'timing_clock': None}, {'first_input_time': None}):
            self.assertIsNone(report.completion_time(dict(summary, **changes))['time_ms'])

    def test_regeneration_does_not_prove_zero_spend(self):
        facts = report.resource_facts([{'f': '100', 'a': '0'}, {'f': '100', 'a': '0'}], ['f'], ['a'])
        self.assertEqual(facts['observed_force_min'], 100)
        self.assertIsNone(facts['force_spent'])

    def test_complete_recording_and_native_polyline(self):
        result, route = report.analyze(*recording())
        self.assertTrue(result['timing']['qualified'], result['timing']['invalid_reasons'])
        self.assertEqual(result['timing']['time_ms'], 50)
        self.assertEqual([p['elapsed_ms'] for p in route['points']], [0, 0, 25, 50])
        self.assertEqual(result['observed_path']['horizontal_units'], 3)

    def test_duplicate_level_is_valid_but_backwards_command_is_not(self):
        rows, meta, summary = recording()
        rows[1]['level'] = rows[0]['level']
        rows[1]['elapsed_ms'] = 0
        self.assertTrue(report.analyze(rows, meta, summary)[0]['timing']['qualified'])
        rows[1]['cmd_time'] = 900
        self.assertFalse(report.analyze(rows, meta, summary)[0]['timing']['qualified'])

    def test_no_pickup_no_time_even_when_summary_claims_complete(self):
        rows, meta, summary = recording()
        rows[-1]['final_blueflag'] = 0
        self.assertIsNone(report.analyze(rows, meta, summary)[0]['timing']['time_ms'])

    def test_truncated_rows_and_moving_start_are_invalid(self):
        rows, meta, summary = recording()
        self.assertIsNone(report.analyze(rows[:-1], meta, summary)[0]['timing']['time_ms'])
        rows[0]['pre_vx'] = 20
        self.assertIsNone(report.analyze(rows, meta, summary)[0]['timing']['time_ms'])

    def test_observed_transition_has_no_invented_airtime(self):
        rows, meta, summary = recording()
        rows[0]['post_ground'] = rows[0]['final_ground'] = 1023
        rows[1]['pre_ground'] = 1023
        events = report.analyze(rows, meta, summary)[0]['transitions']
        self.assertEqual(events['counts'], {'departure_with_jump_input': 1, 'landing': 1})
        self.assertNotIn('airtime_ms', events)

    def test_missing_summary_still_reports_geometry_without_finish_time(self):
        rows, meta, _ = recording()
        result, route = report.analyze(rows, meta, {}, ['summary not closed'])
        self.assertIsNone(result['timing']['time_ms'])
        self.assertEqual(len(route['points']), 4)

    def test_teleport_and_carried_start_do_not_qualify(self):
        rows, meta, summary = recording()
        for field in ('teleport_seen', 'start_carrying'):
            result, _ = report.analyze(rows, meta, dict(summary, **{field: True}))
            self.assertIsNone(result['timing']['time_ms'])

    def test_multisubstep_command_is_reported_without_invented_frames(self):
        rows, meta, summary = recording()
        rows[0]['pre_cmd_time'] = 925
        rows[0]['post_cmd_time'] = 1000
        rows[0]['pmove_msec'] = 8
        result, _ = report.analyze(rows, meta, summary)
        self.assertEqual(result['command_processing']['observed_pmove_command_delta_ms_counts']['75.0'], 1)
        self.assertEqual(result['command_processing']['recorded_pmove_msec_counts']['8.0'], 1)
        self.assertNotIn('airtime_ms', result['command_processing'])


class SubjectTests(unittest.TestCase):
    """A bot recording and a human demonstration are different kinds of
    evidence, and the report must never present one as the other."""

    def bot_recording(self):
        rows, meta, summary = recording()
        meta.update(human=False, subject='bot', corner_level=1000,
                    settings={'bot_moveEdges': '1', 'bot_moveStraight': '1'},
                    clock='level.time: corner release frame to post-touch enemy flag possession')
        return rows, meta, summary

    def test_a_recording_without_a_subject_is_a_human_demonstration(self):
        """Every recording made before the bot subject existed says nothing,
        and every one of them is a human's."""
        result, route = report.analyze(*recording())
        self.assertEqual(result['subject'], 'human')
        self.assertEqual(route['subject'], 'human')
        self.assertIn('Human route report', report.readable(result))

    def test_a_bot_recording_is_labelled_as_its_own_kind(self):
        result, route = report.analyze(*self.bot_recording())
        self.assertEqual(result['subject'], 'bot')
        self.assertEqual(route['subject'], 'bot')
        self.assertTrue(result['timing']['qualified'], result['timing']['invalid_reasons'])
        readable = report.readable(result)
        self.assertIn('Bot route report', readable)
        self.assertNotIn('Human route report', readable)
        self.assertIn('not a human demonstration', readable)
        self.assertIn('Release -> enemy pickup', readable)
        self.assertIn('bot_moveEdges', readable)

    def test_a_bot_recording_claiming_to_be_human_does_not_qualify(self):
        rows, meta, summary = self.bot_recording()
        meta['human'] = True
        self.assertIsNone(report.analyze(rows, meta, summary)[0]['timing']['time_ms'])

    def test_a_human_recording_still_has_to_declare_itself_human(self):
        rows, meta, summary = recording()
        meta['human'] = False
        self.assertIsNone(report.analyze(rows, meta, summary)[0]['timing']['time_ms'])

    def test_a_bot_recording_without_its_release_level_does_not_qualify(self):
        rows, meta, summary = self.bot_recording()
        del meta['corner_level']
        self.assertIsNone(report.analyze(rows, meta, summary)[0]['timing']['time_ms'])

    def test_an_unknown_subject_does_not_qualify(self):
        rows, meta, summary = self.bot_recording()
        meta['subject'] = 'demo'
        self.assertIsNone(report.analyze(rows, meta, summary)[0]['timing']['time_ms'])


if __name__ == '__main__':
    unittest.main()
