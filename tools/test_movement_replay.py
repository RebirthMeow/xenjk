"""Tests for the movement replay analyzer (tools/movement_replay.py).

The segment partition is the analyzer's load-bearing claim: it is offered as
a reconciliation of fixture-to-pickup time, so the intervals must be
mutually exclusive and must sum to the elapsed time exactly. A partition
that quietly drops or double-counts an interval would make every percentage
in an evidence record wrong, so that invariant is tested directly rather
than by eye.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_replay import (arm_isolation, chain_breaks, command_boundary_events,  # noqa: E402
                             movement_coverage, paired_arms, parse_replay_events, pickup_level,
                             runup_pairs, segment_timeline)


def move(level, event, edge, reason, **fields):
    row = dict(level=str(level), event=event, edge=edge, reason=reason)
    row.update({k: str(v) for k, v in fields.items()})
    return row


def cand(level, search, variant, runup, outcome, **fields):
    row = dict(kind='cand', level=str(level), search=str(search), variant=variant,
               plan_kind='state', hops='2', runup0=str(runup), edge='10', edge2='11',
               hop0_yaw='90.0', outcome=outcome, margin='0', progress='0',
               a0_speed='0', a0_cell='-1', committed='0', l0_speed='250', gen='1')
    row.update(role='observe_only' if variant == 'b_measured_policy' else 'proposal', offer='0')
    row.update({k: str(v) for k, v in fields.items()})
    return row


class SegmentPartition(unittest.TestCase):
    """One plan of two hops, then a gap, then a second plan."""

    def setUp(self):
        self.events = [
            move(1000, 'plan', 'none', 'path'),
            move(1100, 'plan', 'hop0', 'sequence', id=1, kind='state', index=10, index2=11),
            move(1200, 'edge', 'hop0', 'launch', speed=250),
            move(2000, 'edge', 'hop0', 'land', speed=370),
            move(2025, 'edge', 'hop1', 'launch', speed=370),
            move(2800, 'edge', 'hop1', 'land', speed=500),
            move(3300, 'plan', 'hop0', 'sequence', id=2, kind='straight', index=-1, index2=-1),
            move(3400, 'edge', 'hop0', 'launch', speed=250),
            move(4000, 'edge', 'hop0', 'land', speed=360),
        ]
        self.events = command_boundary_events(self.events, 25)

    def test_durations_sum_to_elapsed(self):
        segments = segment_timeline(self.events, 1000, 4500)
        self.assertEqual(sum(s['ms'] for s in segments), 3500)

    def test_intervals_do_not_overlap_and_leave_no_hole(self):
        segments = segment_timeline(self.events, 1000, 4500)
        cursor = 1000
        for segment in segments:
            self.assertEqual(segment['start'], cursor, 'gap or overlap before %r' % (segment,))
            self.assertGreater(segment['end'], segment['start'])
            cursor = segment['end']
        self.assertEqual(cursor, 4500)

    def test_flight_is_launch_to_landing_only(self):
        segments = segment_timeline(self.events, 1000, 4500)
        flight = [s for s in segments if s['label'] == 'flight']
        self.assertEqual([s['ms'] for s in flight], [825, 800, 625])

    def test_the_run_up_after_a_commit_is_approach_not_flight(self):
        segments = segment_timeline(self.events, 1000, 4500)
        approach = [s for s in segments if s['label'] == 'approach']
        self.assertEqual([s['ms'] for s in approach], [75, 75])

    def test_ground_between_a_plan_and_the_next_commit_is_search_walk(self):
        segments = segment_timeline(self.events, 1000, 4500)
        labels = {s['label']: s['ms'] for s in segments if s['label'] != 'flight'}
        self.assertNotIn('interhop', labels)
        self.assertEqual(labels['search_walk'], 500)
        self.assertEqual(labels['tail_walk'], 500)

    def test_a_launch_with_no_landing_still_closes_the_partition(self):
        events = self.events[:3]       # commit and launch, no landing recorded
        segments = segment_timeline(events, 1000, 4500)
        self.assertEqual(sum(s['ms'] for s in segments), 3500)
        self.assertIn('unlanded', segments[-1]['detail'])

    def test_missing_landing_followed_by_launch_does_not_double_count(self):
        events = [e for e in self.events if not (e.get('reason') == 'land' and e['level'] == '2000')]
        segments = segment_timeline(events, 1000, 4500)
        self.assertEqual(sum(s['ms'] for s in segments), 3500)
        self.assertTrue(all(a['end'] == b['start'] for a, b in zip(segments, segments[1:])))


class CommandBoundary(unittest.TestCase):
    def test_legacy_logs_require_an_explicit_binding(self):
        with self.assertRaises(ValueError):
            command_boundary_events([move(3900, 'edge', 'hop0', 'launch')])

    def test_confirmation_is_preserved_and_input_is_not_modified(self):
        events = [move(3900, 'edge', 'hop0', 'launch', speed=300)]
        corrected = command_boundary_events(events, 25)
        self.assertEqual(corrected[0]['level'], '3875')
        self.assertEqual(corrected[0]['confirmation_level'], '3900')
        self.assertEqual(events[0]['level'], '3900')

    def test_explicit_native_launch_timestamp_takes_precedence(self):
        corrected = command_boundary_events([move(3900, 'edge', 'hop0', 'launch', launch_level=3850)], 25)
        self.assertEqual(corrected[0]['level'], '3850')

    def test_invalid_delay_is_rejected(self):
        with self.assertRaises(ValueError):
            command_boundary_events([], 0)


class ChainBreaks(unittest.TestCase):
    def test_a_next_frame_launch_is_chained_and_a_later_one_is_a_break(self):
        events = [
            move(2000, 'edge', 'hop0', 'land', speed=370),
            move(2025, 'edge', 'hop1', 'launch', speed=370),
            move(2800, 'edge', 'hop1', 'land', speed=500),
            move(3400, 'edge', 'hop0', 'launch', speed=250),
        ]
        rows, verdict = chain_breaks(command_boundary_events(events, 25), 1000, 4500)
        self.assertEqual(verdict, dict(landings=2, chained=1, breaks=1,
                                       ground_ms_in_breaks=575, speed_lost_total=250.0))
        self.assertTrue(rows[0]['chained'])
        self.assertEqual(rows[1]['speed_lost'], 250.0)

    def test_a_landing_with_no_later_launch_is_not_counted(self):
        events = [move(2000, 'edge', 'hop0', 'land', speed=370)]
        rows, verdict = chain_breaks(events, 1000, 4500)
        self.assertEqual(rows, [])
        self.assertEqual(verdict['landings'], 0)


class PairedArms(unittest.TestCase):
    def test_unjudged_is_censored_not_a_rejection(self):
        for control in ('admitted', 'refused'):
            pairs, _, verdict = paired_arms([
                cand(3000, 4, 'a_control', 0, control),
                cand(3000, 4, 'b_measured_policy', 0, 'unjudged')], 1000, 4500)
            self.assertIsNone(pairs[0]['b_admitted'])
            self.assertEqual(verdict['censored'], 1)
            self.assertEqual(sum(verdict[k] for k in ('both', 'a_only', 'b_only', 'neither')), 0)

    def test_arms_of_one_search_pair_by_hops_and_run_up(self):
        replay = [cand(3000, 4, 'a_control', 0, 'refused'),
                  cand(3000, 4, 'b_measured_policy', 0, 'admitted', margin='12.5'),
                  cand(3000, 4, 'a_control', 24, 'admitted', margin='40'),
                  cand(3000, 4, 'b_measured_policy', 24, 'admitted', margin='30')]
        pairs, unpaired, verdict = paired_arms(replay, 1000, 4500)
        self.assertEqual(unpaired, [])
        self.assertEqual(verdict['pairs'], 2)
        self.assertEqual(verdict['b_only'], 1)
        self.assertEqual(verdict['both'], 1)
        self.assertEqual(verdict['a_better_margin'], 1)
        self.assertEqual(verdict['b_better_margin'], 0)

    def test_a_missing_arm_is_reported_rather_than_matched_up(self):
        pairs, unpaired, verdict = paired_arms([cand(3000, 4, 'a_control', 0, 'admitted')], 1000, 4500)
        self.assertEqual(pairs, [])
        self.assertEqual(verdict['unpaired'], 1)
        self.assertEqual(unpaired[0]['have'], ['a_control'])

    def test_candidates_outside_the_window_are_ignored(self):
        replay = [cand(9000, 4, 'a_control', 0, 'admitted'),
                  cand(9000, 4, 'b_measured_policy', 0, 'admitted')]
        pairs, unpaired, verdict = paired_arms(replay, 1000, 4500)
        self.assertEqual((pairs, unpaired, verdict['pairs']), ([], [], 0))

    def test_the_second_hop_heading_is_its_own_delta(self):
        """A pair can be identical at hop 0 and eight degrees apart at hop 1."""
        pairs, _, _ = paired_arms([
            cand(3000, 4, 'a_control', 0, 'refused', hop0_yaw='83.6526', hop1_yaw='84.8722'),
            cand(3000, 4, 'b_measured_policy', 0, 'arrives', hop0_yaw='83.6973', hop1_yaw='77.0054'),
        ], 1000, 4500)
        self.assertAlmostEqual(pairs[0]['heading_delta'], 0.0447, places=4)
        self.assertAlmostEqual(pairs[0]['hop1_delta'], -7.8668, places=4)

    def test_hop_one_heading_wraps_rather_than_reading_as_a_turn(self):
        pairs, _, _ = paired_arms([
            cand(3000, 4, 'a_control', 0, 'refused', hop1_yaw='179.0'),
            cand(3000, 4, 'b_measured_policy', 0, 'refused', hop1_yaw='-179.0'),
        ], 1000, 4500)
        self.assertAlmostEqual(pairs[0]['hop1_delta'], 2.0, places=4)

    def test_a_log_without_the_new_fields_still_pairs(self):
        """v1/v2 logs have no hop1_yaw, walk_ref or onward diagnostics."""
        replay = [cand(3000, 4, 'a_control', 0, 'admitted'),
                  cand(3000, 4, 'b_measured_policy', 0, 'admitted')]
        for row in replay:
            row.pop('hop0_yaw')
            row['hop0_yaw'] = '90.0'
        pairs, _, verdict = paired_arms(replay, 1000, 4500)
        self.assertEqual(verdict['both'], 1)
        for key in ('hop1_delta', 'a_hop1_yaw', 'a_walk_ref', 'b_walk_ref',
                    'a_end_stage', 'a_onward_max', 'a_brake_frames', 'a_held_frames'):
            self.assertIsNone(pairs[0][key], key)

    def test_the_new_fields_are_carried_when_the_log_has_them(self):
        pairs, _, _ = paired_arms([
            cand(3000, 4, 'a_control', 0, 'no_onward', walk_ref='control',
                 end_stage='2', onward_max='61.5', brake_frames='19', held_frames='0'),
            cand(3000, 4, 'b_measured_policy', 0, 'admitted', walk_ref='replay',
                 end_stage='3', onward_max='214.7', brake_frames='0', held_frames='0'),
        ], 1000, 4500)
        pair = pairs[0]
        self.assertEqual((pair['a_walk_ref'], pair['b_walk_ref']), ('control', 'replay'))
        self.assertEqual((pair['a_end_stage'], pair['b_end_stage']), (2, 3))
        self.assertEqual((pair['a_brake_frames'], pair['b_brake_frames']), (19, 0))
        self.assertAlmostEqual(pair['a_onward_max'], 61.5)
        # no_onward is a refusal, not a censored comparison.
        self.assertIs(pair['a_admitted'], False)
        self.assertIs(pair['b_admitted'], True)


class SuccessorAimPairing(unittest.TestCase):
    def ordinary(self, explicit_source):
        rows = [cand(3000, 4, 'a_control', 0, 'off_path', cand=0, margin=0),
                cand(3000, 4, 'b_measured_policy', 0, 'admitted', cand=1, margin=40),
                cand(3000, 4, 'a_control', 24, 'admitted', cand=2, margin=20),
                cand(3000, 4, 'b_measured_policy', 24, 'admitted', cand=3, margin=30)]
        if explicit_source:
            for row in rows:
                row['aim_source'] = '-1'
        return rows

    def clones(self):
        return [cand(3000, 4, 'a_control', 0, 'admitted', cand=4, aim_source=0,
                     hop0_yaw=120, margin=100, committed=1),
                cand(3000, 4, 'a_control', 24, 'refused', cand=5, aim_source=2,
                     hop0_yaw=130, margin=-10)]

    def test_appended_clones_do_not_replace_historical_heading_pairs(self):
        for explicit_source in (False, True):
            with self.subTest(explicit_source=explicit_source):
                ordinary = self.ordinary(explicit_source)
                self.assertEqual(paired_arms(ordinary, 1000, 4500),
                                 paired_arms(ordinary + self.clones(), 1000, 4500))

    def test_appended_clones_do_not_replace_zero_vs_twenty_four_pairs(self):
        for explicit_source in (False, True):
            with self.subTest(explicit_source=explicit_source):
                ordinary = self.ordinary(explicit_source)
                self.assertEqual(runup_pairs(ordinary, 1000, 4500),
                                 runup_pairs(ordinary + self.clones(), 1000, 4500))

    def test_clones_stay_in_raw_events_and_overall_candidate_commit_counts(self):
        expected = self.ordinary(True) + self.clones()
        text = '\n'.join('[REPLAY] ' + ' '.join('%s=%s' % item for item in row.items())
                         for row in expected)
        replay = parse_replay_events(text)
        paired_arms(replay, 1000, 4500)
        runup_pairs(replay, 1000, 4500)
        self.assertEqual(expected, replay)
        self.assertEqual(4, sum(row['outcome'] == 'admitted' for row in replay))
        stats, _ = arm_isolation(replay, 1000, 4500)
        self.assertEqual((6, 1), (stats['candidates'], stats['commits']))


class ArmIsolation(unittest.TestCase):
    def test_an_observe_only_run_is_clean(self):
        replay = [
            cand(3000, 4, 'a_control', 24, 'admitted', walk_ref='control', done='60', walk_frame='59'),
            cand(3000, 4, 'a_control', 0, 'no_onward', walk_ref='control', done='-1',
                 walk_frame='-1', committed='0'),
            cand(3000, 4, 'b_measured_policy', 24, 'admitted', walk_ref='replay', done='91', walk_frame='90'),
        ]
        replay[0]['committed'] = '1'
        stats, problems = arm_isolation(replay, 1000, 4500)
        self.assertEqual(problems, [])
        self.assertTrue(stats['checked'])
        self.assertEqual((stats['searches'], stats['candidates'], stats['commits'], stats['unjudged']),
                         (1, 3, 1, 0))

    def test_a_replay_commit_is_a_fault(self):
        replay = [cand(3000, 4, 'a_control', 0, 'refused', walk_ref='control'),
                  cand(3000, 4, 'b_measured_policy', 0, 'admitted', walk_ref='replay',
                       done='40', walk_frame='39', committed='1')]
        _, problems = arm_isolation(replay, 1000, 4500)
        self.assertEqual([p['fault'] for p in problems], ['the observe-only arm committed'])

    def test_a_control_judged_by_the_replay_reference_is_a_fault(self):
        replay = [cand(3000, 4, 'a_control', 0, 'admitted', walk_ref='replay',
                       done='40', walk_frame='39'),
                  cand(3000, 4, 'b_measured_policy', 0, 'refused', walk_ref='replay')]
        _, problems = arm_isolation(replay, 1000, 4500)
        self.assertIn('judged by the replay reference', [p['fault'] for p in problems])

    def test_a_control_past_its_own_horizon_is_a_fault(self):
        replay = [cand(3000, 4, 'a_control', 0, 'admitted', walk_ref='control',
                       done='40', walk_frame='60'),
                  cand(3000, 4, 'b_measured_policy', 0, 'refused', walk_ref='replay')]
        _, problems = arm_isolation(replay, 1000, 4500)
        self.assertTrue(any('past its own horizon' in p['fault'] for p in problems))

    def test_an_unjudged_candidate_is_a_fault(self):
        replay = [cand(3000, 4, 'a_control', 0, 'admitted', walk_ref='control',
                       done='40', walk_frame='39'),
                  cand(3000, 4, 'b_measured_policy', 0, 'unjudged', walk_ref='replay')]
        stats, problems = arm_isolation(replay, 1000, 4500)
        self.assertEqual(stats['unjudged'], 1)
        self.assertIn('no reference judged this candidate', [p['fault'] for p in problems])

    def test_telemetry_without_walk_ref_is_reported_unchecked(self):
        replay = [cand(3000, 4, 'a_control', 0, 'admitted', done='40', walk_frame='39'),
                  cand(3000, 4, 'b_measured_policy', 0, 'admitted', done='40', walk_frame='39')]
        stats, problems = arm_isolation(replay, 1000, 4500)
        self.assertFalse(stats['checked'])
        self.assertEqual(problems, [])


class RoleAwareIsolation(unittest.TestCase):
    def pair(self, offer=0):
        return [cand(3000, 4, 'a_control', 0, 'admitted', role='proposal', offer=offer,
                     walk_ref='control', walk_ref_frames=40, done=40, walk_frame=39),
                cand(3000, 4, 'b_measured_policy', 0, 'admitted',
                     role='proposal' if offer else 'observe_only', offer=offer,
                     walk_ref='replay', walk_ref_frames=90, done=90, walk_frame=89)]

    def test_proposal_b_can_commit_using_its_longer_independent_reference(self):
        rows = self.pair(offer=1)
        rows[1]['committed'] = '1'
        stats, problems = arm_isolation(rows, 1000, 4500)
        self.assertEqual(problems, [])
        self.assertTrue(stats['fully_observed'])
        self.assertEqual(stats['proposal_candidates'], 2)

    def test_observe_only_b_commit_is_rejected(self):
        rows = self.pair()
        rows[1]['committed'] = '1'
        _, problems = arm_isolation(rows, 1000, 4500)
        self.assertEqual([p['fault'] for p in problems], ['the observe-only arm committed'])

    def test_offered_b_cannot_use_control_reference(self):
        rows = self.pair(offer=1)
        rows[1]['walk_ref'] = 'control'
        _, problems = arm_isolation(rows, 1000, 4500)
        self.assertIn('judged by the control reference', [p['fault'] for p in problems])

    def test_bounds_use_actual_reference_count_for_both_arms(self):
        for arm in (0, 1):
            with self.subTest(arm=arm):
                rows = self.pair(offer=1)
                rows[arm]['walk_ref_frames'] = '20'
                _, problems = arm_isolation(rows, 1000, 4500)
                self.assertTrue(any('past its own horizon' in p['fault'] for p in problems))

    def test_zero_reference_frames_cannot_support_a_judgement(self):
        rows = self.pair()
        rows[1]['walk_ref_frames'] = '0'
        _, problems = arm_isolation(rows, 1000, 4500)
        self.assertTrue(any('past its own horizon' in p['fault'] for p in problems))

    def test_mixed_b_roles_in_one_search_are_invalid(self):
        rows = self.pair()
        other = dict(rows[1], role='proposal', offer='1', runup0='24')
        rows.append(other)
        _, problems = arm_isolation(rows, 1000, 4500)
        self.assertTrue(any('mixed roles' in p['fault'] for p in problems))

    def test_role_must_agree_with_explicit_offer(self):
        rows = self.pair(offer=1)
        rows[1]['role'] = 'observe_only'
        _, problems = arm_isolation(rows, 1000, 4500)
        self.assertIn('candidate role contradicts offer mode', [p['fault'] for p in problems])

    def test_unknown_role_is_not_a_pass(self):
        rows = self.pair()
        rows[1]['role'] = 'mystery'
        stats, problems = arm_isolation(rows, 1000, 4500)
        self.assertFalse(stats['checked'])
        self.assertIn('unknown candidate role', [p['fault'] for p in problems])

    def legacy(self):
        rows = self.pair()
        for row in rows:
            for key in ('role', 'offer', 'walk_ref_frames'):
                row.pop(key)
        return rows

    def test_legacy_unknown_mode_does_not_assume_b_is_observe_only(self):
        rows = self.legacy()
        rows[1]['committed'] = '1'
        stats, problems = arm_isolation(rows, 1000, 4500)
        self.assertFalse(stats['checked'])
        self.assertFalse(stats['fully_observed'])
        self.assertEqual(problems, [])
        self.assertIn('unknown', stats['mode_sources'])

    def test_legacy_offer_mode_does_not_trust_old_reference_labels(self):
        rows = self.legacy()
        rows[1]['committed'] = '1'
        stats, problems = arm_isolation(rows, 1000, 4500, heading_offer=1)
        self.assertEqual(problems, [])
        self.assertFalse(stats['checked'])
        self.assertIn('legacy offer reference provenance unavailable', stats['limitations'])

    def test_legacy_observe_mode_is_explicitly_inferred_not_fully_observed(self):
        stats, problems = arm_isolation(self.legacy(), 1000, 4500, heading_offer=0)
        self.assertEqual(problems, [])
        self.assertTrue(stats['checked'])
        self.assertFalse(stats['fully_observed'])
        self.assertEqual(stats['mode_sources'], ['settings_inferred'])
        self.assertEqual(stats['horizon_sources'], ['inferred_done'])

    def test_new_pair_fields_are_preserved(self):
        rows = self.pair(offer=1)
        rows[1].update(onward_travel='160.5', onward_progress='-20.0', onward_frames='48', committed='1')
        pairs, _, _ = paired_arms(rows, 1000, 4500)
        pair = pairs[0]
        self.assertEqual((pair['b_role'], pair['b_offer'], pair['b_walk_ref_frames']), ('proposal', 1, 90))
        self.assertTrue(pair['b_committed'])
        self.assertEqual((pair['b_onward_travel'], pair['b_onward_progress'], pair['b_onward_frames']),
                         (160.5, -20.0, 48))


class RunupPairs(unittest.TestCase):
    def test_the_speed_the_run_up_sheds_is_the_launch_difference(self):
        replay = [cand(3000, 4, 'a_control', 0, 'off_path', l0_speed='560', a0_speed='520'),
                  cand(3000, 4, 'a_control', 24, 'admitted', l0_speed='258', a0_speed='384')]
        rows, verdict = runup_pairs(replay, 1000, 4500)
        self.assertEqual(verdict['tap_off_path_runup_admitted'], 1)
        self.assertEqual(verdict['tap_rejected_runup_admitted'], 1)
        self.assertEqual(rows[0]['speed_shed'], 302.0)
        self.assertEqual(verdict['median_speed_shed'], 302.0)

    def test_the_replay_arm_is_not_mixed_into_the_run_up_pairing(self):
        replay = [cand(3000, 4, 'b_measured_policy', 0, 'admitted'),
                  cand(3000, 4, 'b_measured_policy', 24, 'admitted')]
        rows, verdict = runup_pairs(replay, 1000, 4500)
        self.assertEqual((rows, verdict['pairs']), ([], 0))


class MovementCoverage(unittest.TestCase):
    def test_jump_pad_samples_warn_without_assigning_exact_durations(self):
        rows = [dict(level_time='1000', movement_owner='move'),
                dict(level_time='1250', movement_owner='jumppad'),
                dict(level_time='1500', movement_owner='move')]
        coverage = movement_coverage(rows, 1000, 1500)
        self.assertEqual(coverage['non_hop_owners'], ['jumppad'])
        self.assertEqual(coverage['samples'], 3)
        self.assertTrue(any('jump-pad' in w and 'not ground' in w for w in coverage['warnings']))
        self.assertNotIn('durations', coverage)
        self.assertNotIn('intervals', coverage)
        self.assertIn('No exact ownership intervals', coverage['sampling_note'])

    def test_owners_before_fixture_or_after_objective_do_not_leak(self):
        rows = [dict(level_time='999', movement_owner='legacy'),
                dict(level_time='1000', movement_owner='move'),
                dict(level_time='2000', movement_owner='move'),
                dict(level_time='2001', movement_owner='jumppad')]
        coverage = movement_coverage(rows, 1000, 2000)
        self.assertEqual(coverage['sampled_owners'], ['move'])
        self.assertEqual(coverage['non_hop_owners'], [])
        self.assertEqual(coverage['samples'], 2)
        self.assertIn('not verified ground', coverage['interpretation'])

    def test_missing_ownership_is_reported_not_assumed_move(self):
        coverage = movement_coverage([dict(level_time='1200')], 1000, 2000)
        self.assertEqual(coverage['sampled_owners'], [])
        self.assertEqual(coverage['missing_owner_samples'], 1)
        self.assertTrue(coverage['warnings'])

    def test_no_play_rows_cannot_establish_ownership_coverage(self):
        coverage = movement_coverage([], 1000, 2000)
        self.assertEqual(coverage['samples'], 0)
        self.assertTrue(coverage['warnings'])


class PickupFrame(unittest.TestCase):
    """The play log writes a row on the possession transition itself."""

    ROWS = [dict(level_time='3300', carrying='0', captures='0'),
            dict(level_time='3550', carrying='0', captures='0'),
            dict(level_time='22475', carrying='1', captures='0'),
            dict(level_time='22725', carrying='1', captures='0')]

    def test_the_transition_row_is_the_pickup_frame(self):
        self.assertEqual(pickup_level(self.ROWS, 3325, 'pickup'), 22475)

    def test_a_row_at_or_before_the_fixture_is_not_the_pickup(self):
        rows = [dict(level_time='3300', carrying='1', captures='0')] + self.ROWS
        self.assertEqual(pickup_level(rows, 3325, 'pickup'), 22475)

    def test_an_objective_never_reached_is_none_rather_than_a_guess(self):
        rows = [dict(level_time='3550', carrying='0', captures='0')]
        self.assertIsNone(pickup_level(rows, 3325, 'pickup'))


if __name__ == '__main__':
    unittest.main()
