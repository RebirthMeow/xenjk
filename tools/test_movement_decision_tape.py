"""Hand-built logs only: the extractor is read-only and must never guess."""
import json
import sys
import unittest
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_decision_tape as tape

MAP_LOAD = ('2026-09-22 10:00:00 [GRAPH] map_load map=mp/ctf_kejim checksum=-1301924053 '
            'cached=1 cells=14249 edges=94711\n')
# The same line as the engine prints it now, carrying the graph's own identity
# between the checksum and the rest.
MAP_LOAD_BOUND = ('2026-09-22 10:00:00 [GRAPH] map_load map=mp/ctf_kejim '
                  'checksum=-1301924053 graph=g1-0011223344556677 '
                  'cached=1 cells=14249 edges=94711\n')


def commit(level, x, y, z, kind='straight', hops=2, d0='1.25', r0=24,
           d1='0', r1=0, c0=4948, c1=-1, index=-1, index2=-1, nth=1, client=0):
    """One `[MOVE] ... reason=sequence` line, in the engine's own field order."""
    return ('2026-09-22 10:00:01 [MOVE] client=%d level=%d event=plan edge=hop0 '
            'reason=sequence speed=219.2 force=0 speed_ms=0 x=%.1f y=%.1f z=%.1f '
            'kind=%s id=2 hops=%d landing=0 reissued_up=0 arrives=0 candidates=19 '
            'admitted=19 frames=99 walk_frame=98 walk_stopped=0 walk_frames=99 '
            'd0=%s r0=%d d1=%s r1=%d c0=%d c1=%d chord=820.3 far=3 progress=853.7 walk=595.4 launch_speed=250.1 '
            'land_speed=364.7 launch1_speed=0.0 land1_speed=0.0 rollout_ms=35.4 '
            'traces=25564 index=%d index2=%d state_candidates=5 state_admitted=5 '
            'envelope=1 rerouted=0 route_cells=0 route_ms=34725 exit_ms=500 '
            'exit_credit=257.3 score=515.7 margin=258.3 stale_slots=0 commit_nth=%d '
            'select_forced=0\n'
            % (client, level, x, y, z, kind, hops, d0, r0, d1, r1, c0, c1,
               index, index2, nth))


def world(lines, corner_level=3125, name='decision-tape', time_ms=None):
    """A hand-built world: a log, and the runner record naming release and leg."""
    root = Path(__file__).resolve().parent.parent / '.codex-research/unit-artifacts' / name
    out = root / uuid.uuid4().hex
    out.mkdir(parents=True)
    (out / 'server.log').write_text(''.join(lines), encoding='utf-8')
    if corner_level is not None:
        case = dict(name='red_out_L1_stand', corner_level=corner_level)
        if time_ms is not None:
            case['time_ms'] = time_ms
        (out / 'result.json').write_text(json.dumps(dict(cases=[case])), encoding='utf-8')
    return out


class BindingTest(unittest.TestCase):
    def test_tape_is_bound_to_the_map_and_checksum_of_its_own_log(self):
        asset = tape.build(world([MAP_LOAD, commit(3200, 1, 2, 3)]))
        self.assertEqual(asset['map'], 'mp/ctf_kejim')
        self.assertEqual(asset['map_checksum'], -1301924053)

    def test_a_log_without_a_map_load_is_refused(self):
        with self.assertRaises(tape.TapeError):
            tape.build(world([commit(3200, 1, 2, 3)]))

    def test_a_world_with_no_release_level_is_refused(self):
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, commit(3200, 1, 2, 3)], corner_level=None))
        self.assertIn('release level', str(caught.exception))

    def test_a_declared_release_level_is_taken_over_the_record(self):
        target = world([MAP_LOAD, commit(3000, 1, 2, 3), commit(3200, 4, 5, 6)])
        self.assertEqual(len(tape.build(target)['entries']), 1)
        self.assertEqual(len(tape.build(target, 2000)['entries']), 2)

    def test_the_corner_line_is_read_when_it_did_reach_the_log(self):
        asset = tape.build(world([MAP_LOAD,
                                  '[CORNER] START map=mp/ctf_kejim level=3125 client=0\n',
                                  commit(3200, 1, 2, 3)], corner_level=None))
        self.assertEqual(asset['corner_level'], 3125)


class ReleaseFilterTest(unittest.TestCase):
    """A fixture world runs the bot's own AI before the release, and those
    commits belong to a different trip."""

    def test_commits_before_the_release_are_not_decisions_of_this_trip(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(2525, 1.3, -824.1, 1912.1),
                                  commit(3575, 164.2, -1071.1, 1912.1),
                                  commit(4100, 295.1, -201.8, 1808.1)]))
        self.assertEqual([e['level'] for e in asset['entries']], [3575, 4100])
        self.assertEqual([e['index'] for e in asset['entries']], [0, 1])

    def test_a_world_with_no_commit_after_the_release_is_refused(self):
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, commit(2525, 1, 2, 3)]))
        self.assertIn('no committed sequences', str(caught.exception))


class ReleaseFrameTest(unittest.TestCase):
    """The commit printed on the release level itself belongs to the old trip.

    `bot_corner_start` is executed in the same server frame the game already
    ran, so a world released at L can carry one commit at exactly L taken from
    wherever the bot's own AI had walked to. Seen in a
    verification run: a commit at
    level 3525 from (186.4, -496.6) in a world released at 3525 at the stand.
    `movement_bench_lib.runner_rows` has always been strict for the same
    reason; this was not.
    """

    def test_a_commit_on_the_release_level_is_not_a_decision_of_this_trip(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(3525, 186.4, -496.6, 1860.0),
                                  commit(3975, 164.2, -1071.1, 1912.1)],
                                 corner_level=3525))
        self.assertEqual([e['level'] for e in asset['entries']], [3975])
        self.assertEqual(asset['entries'][0]['root'], [164.2, -1071.1, 1912.1])

    def test_a_world_whose_only_commit_is_the_release_frame_is_refused(self):
        with self.assertRaises(tape.TapeError):
            tape.build(world([MAP_LOAD, commit(3525, 186.4, -496.6, 1860.0)],
                             corner_level=3525))


class DecisionIdentityTest(unittest.TestCase):
    """What makes two decisions different, and therefore what a match needs.

    The state search offers the same planned edge twice, tapping at once or
    after the short run-up, so a plan edge does not identify a state decision.
    Matching on edges alone picked the run-up-0 variant where the control had
    committed the run-up-24 one, reported it a hit, and diverged the trip:
    19,250 ms
    against the control's 15,925 with one hit and five absent misses.
    """

    def test_a_decision_is_both_hops(self):
        """Two candidates for one planned edge pair, agreeing on every
        first-hop field and differing in the second. Observed: the
        control and the tape world committed one of each at the same root and
        level, with second-hop landing speeds 514.7 against 506.8."""
        asset = tape.build(world([MAP_LOAD,
                                  commit(3600, 1, 2, 3, kind='state', d0='-0.105301', r0=24,
                                         d1='3.5', r1=0, index=50732, index2=26663),
                                  commit(3700, 400, 2, 3, kind='state', d0='-0.105301', r0=24,
                                         d1='-8.25', r1=24, index=50732, index2=26663)]))
        first, second = asset['entries']
        self.assertEqual((first['aim'], first['runup']), (second['aim'], second['runup']))
        self.assertEqual((first['edge'], first['edge2']), (second['edge'], second['edge2']))
        self.assertNotEqual((first['aim1'], first['runup1']),
                            (second['aim1'], second['runup1']))

    def test_a_commit_without_the_second_hop_is_refused(self):
        """A version-1 world's commit line has no d1. It is refused, not
        matched on half a decision."""
        line = commit(3600, 1, 2, 3).replace(' d1=0 r1=0', '')
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, line]))
        self.assertIn('d1', str(caught.exception))

    def test_one_planned_edge_pair_appears_at_two_runups(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(3600, 1, 2, 3, kind='state', d0='-0.105301',
                                         r0=24, index=50732, index2=26663),
                                  commit(3700, 400, 2, 3, kind='state', d0='-0.105301',
                                         r0=0, index=50732, index2=26663)]))
        first, second = asset['entries']
        self.assertEqual((first['edge'], first['edge2']), (second['edge'], second['edge2']))
        self.assertNotEqual(first['runup'], second['runup'])

    def test_every_entry_carries_the_runup_and_aim_a_match_needs(self):
        """Whatever the kind. A tape that dropped either for state entries
        could not tell the two apart, and the engine matches on what is here."""
        for kind, edge, edge2 in (('state', 50732, 26663), ('straight', -1, -1)):
            asset = tape.build(world([MAP_LOAD,
                                      commit(3600, 1, 2, 3, kind=kind, d0='2.20786',
                                             r0=24, index=edge, index2=edge2)]))
            entry = asset['entries'][0]
            self.assertEqual(entry['aim'], '2.20786', kind)
            self.assertEqual(entry['runup'], '24', kind)


class LegTest(unittest.TestCase):
    """A tape is one leg's decisions. The world keeps playing after the flag.

    Measured in a verification control run: the
    world is released at 3125, takes the flag at 19050 and commits again at
    19100 and 20375, on the way back. Counting those as entries would have
    muddied every class's hit count.
    """

    def test_commits_after_the_pickup_are_another_leg(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(3575, 164.2, -1071.1, 1912.1),
                                  commit(18325, 2432.1, 4909.1, 1912.1),
                                  commit(19100, 2489.6, 5315.8, 1924.1),
                                  commit(20375, 2486.1, 5008.8, 1912.1)],
                                 corner_level=3125, time_ms=15925))
        self.assertEqual(asset['leg_end_level'], 19050)
        self.assertEqual([e['level'] for e in asset['entries']], [3575, 18325])

    def test_a_world_record_keeps_its_time_under_evaluation(self):
        # The per-case result.json beside the log: no cases list, the finish
        # time under `evaluation`. A campaign record above it names a sibling
        # first and must not be read as this world's.
        out = world([MAP_LOAD,
                     commit(3575, 164.2, -1071.1, 1912.1),
                     commit(6500, 2432.1, 4909.1, 1912.1)], corner_level=None)
        case = out / 'red_out_L1_stand'
        case.mkdir()
        (out / 'server.log').rename(case / 'server.log')
        (case / 'result.json').write_text(json.dumps(dict(
            case=dict(name='red_out_L1_stand'), corner_level=3125,
            evaluation=dict(time_ms=2875))), encoding='utf-8')
        (out / 'result.json').write_text(json.dumps(dict(cases=[
            dict(name='blue_out_L1_stand', corner_level=3125, time_ms=19950),
            dict(name='red_out_L1_stand', corner_level=3125, time_ms=2875)])), encoding='utf-8')
        asset = tape.build(case)
        self.assertEqual(asset['leg_end_level'], 6000)
        self.assertEqual([e['level'] for e in asset['entries']], [3575])

    def test_a_campaign_record_is_read_for_its_own_case_only(self):
        out = world([MAP_LOAD, commit(3575, 1, 2, 3), commit(6500, 4, 5, 6)], corner_level=None)
        case = out / 'red_out_L1_stand'
        case.mkdir()
        (out / 'server.log').rename(case / 'server.log')
        (out / 'result.json').write_text(json.dumps(dict(cases=[
            dict(name='blue_out_L1_stand', corner_level=3125, time_ms=19950),
            dict(name='red_out_L1_stand', corner_level=3125, time_ms=2875)])), encoding='utf-8')
        asset = tape.build(case)
        self.assertEqual(asset['leg_end_level'], 6000)

    def test_the_objective_change_ends_the_leg_when_no_time_is_recorded(self):
        asset = tape.build(world([
            MAP_LOAD,
            commit(3575, 1, 2, 3),
            '2026-09-22 10:00:02 [MOVE] client=0 level=19050 event=plan edge=none '
            'reason=objective speed=345.5 force=92 speed_ms=0 x=1.0 y=2.0 z=3.0\n',
            commit(19100, 4, 5, 6)], corner_level=3125))
        self.assertEqual(asset['leg_end_level'], 19050)
        self.assertEqual(asset['leg_end_source'], 'objective change')
        self.assertEqual(len(asset['entries']), 1)

    def test_a_declared_leg_end_is_taken_over_both(self):
        target = world([MAP_LOAD, commit(3575, 1, 2, 3), commit(19100, 4, 5, 6)],
                       corner_level=3125, time_ms=15925)
        self.assertEqual(len(tape.build(target, leg=25000)['entries']), 2)

    def test_a_world_with_no_leg_end_is_carried_whole_and_says_so(self):
        asset = tape.build(world([MAP_LOAD, commit(3575, 1, 2, 3), commit(19100, 4, 5, 6)],
                                 corner_level=3125))
        self.assertIsNone(asset['leg_end_level'])
        self.assertIn('not found', asset['leg_end_source'])
        self.assertEqual(len(asset['entries']), 2)


class LandingCellTest(unittest.TestCase):
    """A straight plan has no edges, so it is named by where it goes.

    Measured in the class-125 world's own judged sets: of 166
    admitted straight pairs agreeing on hops and both run-ups, 7 agree on both
    aims as well, and the smallest nonzero aim separation is 0.030304 degrees
    while the start class perturbs the first entry's aim by up to 0.066. The
    aim never discriminated inside a set; the landing cell does.
    """

    def test_an_entry_carries_the_cell_each_hop_lands_in(self):
        asset = tape.build(world([MAP_LOAD, commit(3600, 1, 2, 3, c0=4948, c1=7217)]))
        entry = asset['entries'][0]
        self.assertEqual((entry['cell'], entry['cell1']), (4948, 7217))

    def test_two_hops_of_one_shape_landing_elsewhere_are_different_decisions(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(3600, 1, 2, 3, d0='1.25', r0=24, c0=4948),
                                  commit(3700, 400, 2, 3, d0='1.25', r0=24, c0=7217)]))
        first, second = asset['entries']
        self.assertEqual((first['aim'], first['runup'], first['hops']),
                         (second['aim'], second['runup'], second['hops']))
        self.assertNotEqual(first['cell'], second['cell'])

    def test_a_straight_commit_with_no_landing_cell_is_refused(self):
        """Off the graph, the decision has no name. It is refused, not carried
        with a -1 that would match any other unnamed hop."""
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, commit(3600, 1, 2, 3, c0=-1)]))
        self.assertIn('landing cell', str(caught.exception))

    def test_a_state_commit_needs_no_landing_cell(self):
        """A state plan is named by its graph edges, which are a plan field."""
        asset = tape.build(world([MAP_LOAD,
                                  commit(3600, 1, 2, 3, kind='state', c0=-1, c1=-1,
                                         index=50732, index2=26663)]))
        self.assertEqual(asset['entries'][0]['edge'], 50732)


class FieldTest(unittest.TestCase):
    def test_an_entry_carries_the_root_kind_aim_runup_and_edges(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(3575, 164.2, -1071.1, 1912.1, kind='state', hops=2,
                                         d0='-0.105301', r0=24, index=50732, index2=26663)]))
        entry = asset['entries'][0]
        self.assertEqual(entry['root'], [164.2, -1071.1, 1912.1])
        self.assertEqual(entry['kind'], 'state')
        self.assertEqual(entry['hops'], 2)
        self.assertEqual(entry['aim'], '-0.105301')
        self.assertEqual(entry['runup'], '24')
        self.assertEqual((entry['edge'], entry['edge2']), (50732, 26663))

    def test_the_aim_is_kept_as_printed_not_reformatted(self):
        """Both sides compare what they print. A tape that renormalised the
        aim would compare a number the engine never wrote."""
        for printed in ('-0.105301', '9.16367', '2.20786', '-19.6655', '0.154938'):
            asset = tape.build(world([MAP_LOAD, commit(3575, 1, 2, 3, d0=printed)]))
            self.assertEqual(asset['entries'][0]['aim'], printed)

    def test_an_unknown_kind_is_refused_rather_than_carried(self):
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, commit(3575, 1, 2, 3, kind='wallrun')]))
        self.assertIn('unknown kind', str(caught.exception))

    def test_a_climb_commit_is_refused_rather_than_skipped(self):
        """A climb prints no `hops=`; the extractor once failed to match it
        and returned the other decisions as if the climb had not happened."""
        climb = ('2026-09-23 22:34:01 [MOVE] client=0 level=%d event=plan edge=hop0 reason=sequence '
            'speed=39.4 force=50 speed_ms=10500 x=4999.8 y=-856.4 z=-39.9 kind=climb edge=113 '
            'id=8 candidates=66 admitted=57 refused=0 short=9 frames=72 jump=0 repress=6 '
            'rise=256.0 rollout_ms=33.9 traces=54269\n' % 3700)
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, commit(3575, 1, 2, 3), climb]))
        self.assertIn("unknown kind 'climb'", str(caught.exception))

    def test_a_commit_line_missing_a_match_field_is_refused(self):
        line = commit(3575, 1, 2, 3).replace(' index2=-1', '')
        with self.assertRaises(tape.TapeError) as caught:
            tape.build(world([MAP_LOAD, line]))
        self.assertIn('index2', str(caught.exception))

    def test_lines_that_are_not_commits_are_ignored(self):
        asset = tape.build(world([
            MAP_LOAD,
            '2026-09-22 10:00:01 [MOVE] client=0 level=3600 event=edge edge=hop0 '
            'reason=launch speed=255.0 force=100 speed_ms=0 x=17.5 y=-794.0 z=1919.5\n',
            '2026-09-22 10:00:01 [MOVE] client=0 level=3650 event=plan edge=none '
            'reason=none speed=1.0 force=0 speed_ms=0 x=1.0 y=2.0 z=3.0 kind=straight '
            'candidates=4 refused=4\n',
            commit(3700, 1, 2, 3)]))
        self.assertEqual(len(asset['entries']), 1)
        self.assertEqual(asset['entries'][0]['level'], 3700)


class GraphBinding(unittest.TestCase):
    """A tape is bound to the graph it was recorded on, when the log says one."""

    def test_reads_the_graph_from_the_map_load_line(self):
        self.assertEqual(
            tape.binding(MAP_LOAD_BOUND),
            ('mp/ctf_kejim', -1301924053, 'g1-0011223344556677'))

    def test_a_log_without_the_field_is_unbound(self):
        self.assertEqual(tape.binding(MAP_LOAD),
                         ('mp/ctf_kejim', -1301924053, 'none'))

    def test_the_header_carries_it(self):
        asset = tape.build(world([MAP_LOAD_BOUND, commit(3575, 1.0, 2.0, 3.0)]))
        self.assertEqual(asset['graph'], 'g1-0011223344556677')
        header = tape.tape_text(asset['map'], asset['map_checksum'],
                                asset['corner_level'], asset['entries'],
                                asset['source'], asset['graph']).splitlines()[0]
        self.assertTrue(header.endswith(' graph=g1-0011223344556677'), header)


class TextTest(unittest.TestCase):
    def test_the_file_the_engine_reads_is_a_bound_header_and_one_line_per_entry(self):
        asset = tape.build(world([MAP_LOAD,
                                  commit(3575, 164.2, -1071.1, 1912.1, kind='state',
                                         d0='-0.105301', r0=24, index=50732, index2=26663),
                                  commit(4100, 561.6, 349.3, 1808.1, kind='straight',
                                         d0='9.16367', r0=0)]))
        text = tape.tape_text(asset['map'], asset['map_checksum'], asset['corner_level'],
                              asset['entries'], asset['source'], asset['graph'])
        lines = text.splitlines()
        self.assertEqual(
            lines[0],
            'decisiontape 3 map=mp/ctf_kejim checksum=-1301924053 entries=2 graph=%s'
            % asset['graph'])
        body = [line for line in lines[1:] if not line.startswith('#')]
        self.assertEqual(len(body), 2)
        self.assertEqual(body[0].split(),
                         ['e', '0', '164.200000', '-1071.100000', '1912.100000',
                          'state', '2', '-0.105301', '24', '0', '0', '50732', '26663',
                          '4948', '-1'])
        self.assertEqual(body[1].split()[5:],
                         ['straight', '2', '9.16367', '0', '0', '0', '-1', '-1',
                          '4948', '-1'])

    def test_entry_indices_are_dense_and_ordered(self):
        asset = tape.build(world([MAP_LOAD] + [commit(3200 + 50 * i, i, i, i) for i in range(5)]))
        body = [line for line in tape.tape_text(asset['map'], asset['map_checksum'],
                                                asset['corner_level'], asset['entries'],
                                                asset['source']).splitlines()
                if line.startswith('e ')]
        self.assertEqual([int(line.split()[1]) for line in body], [0, 1, 2, 3, 4])


if __name__ == '__main__':
    unittest.main()
