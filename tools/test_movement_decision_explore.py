"""Hand-built worlds only: the explorer drives a game and must be read first.

No bench runs here. `Campaign` takes the thing that runs a world as a
parameter, so the search - which candidates a decision offers, what prefix a
branch is given, which children carry the beam, when it stops and what it does
on a resume - is exercised against logs written by hand.
"""
import json
import sys
import types
import unittest
import uuid
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_decision_explore as explore
import movement_decision_tape as tape

ARTIFACTS = Path(__file__).resolve().parent.parent / '.codex-research/unit-artifacts'
MAP_LOAD = ('2026-09-22 10:00:00 [GRAPH] map_load map=mp/ctf_kejim checksum=-1301924053 '
            'cached=1 cells=14249 edges=94711\n')


def commit(level, x=100.0, y=200.0, z=300.0, kind='straight', hops=2, d0='1.25', r0=24,
           d1='0', r1=0, c0=4948, c1=-1, index=-1, index2=-1, nth=1, forced=0, client=0):
    return ('2026-09-22 10:00:01 [MOVE] client=%d level=%d event=plan edge=hop0 '
            'reason=sequence speed=219.2 force=0 speed_ms=0 x=%.1f y=%.1f z=%.1f '
            'kind=%s id=2 hops=%d landing=0 reissued_up=0 arrives=0 candidates=4 '
            'admitted=3 frames=99 walk_frame=98 walk_stopped=0 walk_frames=99 '
            'd0=%s r0=%d d1=%s r1=%d c0=%d c1=%d chord=820.3 far=3 progress=853.7 '
            'walk=595.4 launch_speed=250.1 land_speed=364.7 launch1_speed=0.0 '
            'land1_speed=0.0 rollout_ms=35.4 traces=25564 index=%d index2=%d '
            'state_candidates=5 state_admitted=5 envelope=1 rerouted=0 route_cells=0 '
            'route_ms=34725 exit_ms=500 exit_credit=257.3 score=515.7 margin=258.3 '
            'stale_slots=0 commit_nth=%d select_forced=%d\n'
            % (client, level, x, y, z, kind, hops, d0, r0, d1, r1, c0, c1,
               index, index2, nth, forced))


def spike(level, cand, outcome='admitted', kind='straight', hops=2, d0='1.25', r0=24,
          d1='0', r1=0, cell=4948, cell1=-1, client=0, plan=2):
    return ('2026-09-22 10:00:01 [SPIKE] client=%d level=%d seq=469 kind=%s plan=%d cand=%d '
            'hops=%d d0=%s r0=%d d1=%s r1=%d outcome=%s frames=99 unsupported=0 '
            'launch0=1 l0_speed=250.1 l0_err=0.1 land0=40 a0_speed=364.7 a0_drop=1.0 '
            'a0_err=0.2 a0_cell=%d a0_flags=0 launch1=-1 l1_speed=0.0 l1_err=0.0 '
            'land1=-1 a1_speed=0.0 a1_drop=0.0 a1_err=0.0 a1_cell=%d a1_flags=0 '
            'done=99 progress=853.7 start_speed=219.2 start_err=0.3\n'
            % (client, level, kind, plan, cand, hops, d0, r0, d1, r1, outcome, cell, cell1))


def census(hits, refused=0, absent=0, deferred=0, entries=11):
    return ('2026-09-22 10:00:02 [TAPE] census entries=%d hits=%d refused=%d absent=%d '
            'deferred=%d cursor=%d\n' % (entries, hits, refused, absent, deferred, hits))


def world(lines, name, corner_level=3075, time_ms=20000, status='pass', case='red_out_L1_stand'):
    """A world laid out the way the bench lays one out: summary, case, log."""
    out = ARTIFACTS / 'decision-explore' / uuid.uuid4().hex if name is None else name
    out = Path(out)
    (out / case).mkdir(parents=True, exist_ok=True)
    (out / case / 'server.log').write_text(''.join(lines), encoding='utf-8')
    record = dict(name=case, corner_level=corner_level, time_ms=time_ms, status=status,
                  reason='complete', deaths=0, owners=1,
                  parity=dict(mismatches=0), straight=dict(commits=13), state=dict(commits=5))
    (out / case / 'result.json').write_text(json.dumps(record), encoding='utf-8')
    (out / 'result.json').write_text(json.dumps(dict(cases=[record])), encoding='utf-8')
    return out


def leg_lines(levels, kinds=None, arrival=False):
    """One log: the map binding, then one commit per level, numbered from 1."""
    lines = [MAP_LOAD]
    for i, level in enumerate(levels):
        kind = (kinds or {}).get(i, 'straight')
        last = arrival and i == len(levels) - 1
        lines.append(commit(level, kind=kind, nth=i + 1, c0=-1 if last else 4948 + i,
                            d0='%.4f' % (1.0 + i)))
    return lines


def temp(name=''):
    return ARTIFACTS / 'decision-explore' / (uuid.uuid4().hex + name)


class CandidateTest(unittest.TestCase):
    def test_the_candidates_of_a_decision_are_that_searchs_own_spike_lines(self):
        out = world(leg_lines([3200, 3400]) +
                    [spike(3200, 0, 'refused'), spike(3200, 1), spike(3200, 2, 'fell'),
                     spike(3400, 0)], temp())
        entries = explore.leg(out)['entries']
        rows = explore.candidates(explore.world_text(out), 0, entries[0]['level'])
        self.assertEqual([r['cand'] for r in rows], [0, 1, 2])
        self.assertEqual([r['admitted'] for r in rows], [False, True, False])

    def test_only_admitted_candidates_are_branches(self):
        out = world(leg_lines([3200]) +
                    [spike(3200, 0, 'refused'), spike(3200, 1, 'arrives'),
                     spike(3200, 2, 'tap_onward'), spike(3200, 3, 'slower')], temp())
        entry = explore.leg(out)['entries'][0]
        rows = explore.admitted_candidates(explore.world_text(out), entry)
        self.assertEqual([r['cand'] for r in rows], [1, 2])

    def test_the_candidate_the_world_committed_is_marked(self):
        out = world(leg_lines([3200]) +
                    [spike(3200, 0, d0='9.0000'), spike(3200, 1, d0='1.0000')], temp())
        entry = explore.leg(out)['entries'][0]
        rows = explore.admitted_candidates(explore.world_text(out), entry)
        self.assertEqual([r['committed'] for r in rows], [False, True])

    def test_candidates_of_another_frame_are_not_offered(self):
        out = world(leg_lines([3200, 3400]) + [spike(3200, 7), spike(3400, 9)], temp())
        entries = explore.leg(out)['entries']
        rows = explore.admitted_candidates(explore.world_text(out), entries[1])
        self.assertEqual([r['cand'] for r in rows], [9])


class SettingsTest(unittest.TestCase):
    def test_an_override_replaces_the_base_value(self):
        out = explore.base_settings(['bot_moveReroute=0'])
        self.assertIn('bot_moveReroute=0', out)
        self.assertNotIn('bot_moveReroute=1', out)

    def test_a_cvar_given_twice_keeps_only_the_last(self):
        for overrides in (['bot_moveClimbs=0', 'bot_moveClimbs=1'],
                          ['bot_moveReroute=0', 'bot_moveReroute=2']):
            out = explore.base_settings(overrides)
            name = overrides[0].split('=')[0]
            values = [v for v in out if v.split('=', 1)[0] == name]
            self.assertEqual(values, [overrides[-1]])


class LegTest(unittest.TestCase):
    def test_an_arriving_hop_ends_the_nameable_leg_and_is_recorded(self):
        out = world(leg_lines([3200, 3400, 3600], arrival=True), temp())
        found = explore.leg(out)
        self.assertEqual(len(found['entries']), 2)
        self.assertEqual(found['unnameable_level'], 3600)

    def test_a_climb_ends_the_nameable_leg_and_is_recorded(self):
        climb = ('2026-09-23 22:34:01 [MOVE] client=0 level=%d event=plan edge=hop0 reason=sequence '
            'speed=39.4 force=50 speed_ms=10500 x=4999.8 y=-856.4 z=-39.9 kind=climb edge=113 '
            'id=8 candidates=66 admitted=57 refused=0 short=9 frames=72 jump=0 repress=6 '
            'rise=256.0 rollout_ms=33.9 traces=54269\n' % 3500)
        out = world(leg_lines([3200, 3400]) + [climb] + leg_lines([3600])[1:], temp())
        found = explore.leg(out)
        self.assertEqual(len(found['entries']), 2)
        self.assertEqual(found['unnameable_level'], 3500)

    def test_a_leg_with_no_arrival_is_whole(self):
        out = world(leg_lines([3200, 3400, 3600]), temp())
        found = explore.leg(out)
        self.assertEqual(len(found['entries']), 3)
        self.assertIsNone(found['unnameable_level'])

    def test_the_trip_is_read_from_the_case_and_not_from_a_retired_world(self):
        out = world(leg_lines([3200, 3400]), temp())
        (out / 'red_out_L1_stand.miss-1').mkdir()
        (out / 'red_out_L1_stand.miss-1' / 'server.log').write_text(
            ''.join([MAP_LOAD, commit(1000, kind='state', nth=1)]), encoding='utf-8')
        self.assertEqual(explore.case_directory(out).name, 'red_out_L1_stand')
        self.assertEqual(len(explore.leg(out)['entries']), 2)


class PrefixTest(unittest.TestCase):
    def test_a_prefix_is_the_first_k_decisions_of_the_world_branched_from(self):
        out = world(leg_lines([3200, 3400, 3600]), temp())
        text = explore.prefix_text(explore.leg(out), 2)
        rows = [line for line in text.splitlines() if line.startswith('e ')]
        self.assertEqual(len(rows), 2)
        self.assertIn('entries=2', text.splitlines()[0])
        self.assertIn('map=mp/ctf_kejim checksum=-1301924053', text.splitlines()[0])

    def test_a_prefix_longer_than_the_leg_is_refused(self):
        out = world(leg_lines([3200]), temp())
        with self.assertRaises(explore.ExploreError):
            explore.prefix_text(explore.leg(out), 4)

    def test_the_first_decision_is_branched_with_no_tape_at_all(self):
        campaign, execute = campaign_over([leg_lines([3200, 3400]) + [spike(3200, 1)]] * 2)
        campaign.options.max_depth = 1
        campaign.run()
        self.assertEqual([call['tape'] for call in execute.calls[:2]], [None, None])
        self.assertEqual(execute.calls[1]['branch'],
                         dict(nth=1, cand=1, level=3200, prefix=0, kind='straight', hops=2,
                              committed=False, parent='control', nth_is_depth=True))

    def test_a_deeper_branch_carries_its_parents_decisions_as_the_tape(self):
        log = (leg_lines([3200, 3400, 3600]) +
               [spike(3200, 1), spike(3400, 2), spike(3600, 3)])
        campaign, execute = campaign_over([log] * 8)
        campaign.options.max_depth = 2
        campaign.run()
        self.assertEqual(execute.calls[1]['tape'], None)
        # calls[2] is the confirmation of the first branch; the second branch
        # is the call after it, and it is the one that carries a prefix.
        self.assertEqual(execute.calls[2]['name'], 'control-d1c1-cf')
        staged = Path(execute.calls[3]['tape']).read_text(encoding='utf-8')
        self.assertIn('entries=1', staged.splitlines()[0])
        self.assertEqual(execute.calls[3]['branch']['nth'], 2)
        self.assertEqual(execute.calls[3]['branch']['prefix'], 1)


class Execute(object):
    """A stub that lays down a prepared world for every call, in order."""

    def __init__(self, logs, times=None, corner=3075, confirm_misses=()):
        self.logs = list(logs)
        self.times = list(times or [])
        self.corner = corner
        # Confirmation worlds that are to come back with an entry unhit, named
        # by the branch they confirm.
        self.confirm_misses = set(confirm_misses)
        self.calls = []

    def __call__(self, name, directory, tape_path, branch, directive=None):
        index = len(self.calls)
        self.calls.append(dict(name=name, out=str(directory),
                               tape=str(tape_path) if tape_path else None, branch=branch,
                               directive=directive))
        log = list(self.logs[min(index, len(self.logs) - 1)])
        time_ms = self.times[index] if index < len(self.times) else 20000
        if branch:
            # What the engine writes when the forced index fires, and what its
            # census says about a prefix every entry of which was taken.
            log = [line.replace('select_forced=0', 'select_forced=1')
                   if 'commit_nth=%d ' % branch['nth'] in line else line for line in log]
            if branch['prefix']:
                log.append(census(branch['prefix'], entries=branch['prefix']))
        elif tape_path:
            # A confirmation world: the whole sequence and nothing forced, so
            # its census answers for every entry of the tape it was given.
            header = Path(tape_path).read_text(encoding='utf-8').splitlines()[0]
            entries = int(header.partition('entries=')[2].split()[0])
            if name[:-3] in self.confirm_misses:
                log.append(census(entries - 1, absent=1, entries=entries))
            else:
                log.append(census(entries, entries=entries))
        world(log, directory, corner_level=self.corner, time_ms=time_ms)
        return 0


def options(out, **extra):
    values = dict(out=out, corner_class=75, build='build', beam=3, patience=2,
                  max_branches=0, max_depth=0, root_name='control', world_attempts=3)
    values.update(extra)
    return types.SimpleNamespace(**values)


def campaign_over(logs, times=None, **extra):
    out = temp()
    out.mkdir(parents=True)
    execute = Execute(logs, times)
    return explore.Campaign(options(out, **extra), execute), execute


class BeamTest(unittest.TestCase):
    def records(self, times, **flags):
        rows = []
        for i, time_ms in enumerate(times):
            row = dict(world='w%d' % i, status='pass', time_ms=time_ms, decisions=[1],
                       prefix_broken=False, forced=1)
            row.update(flags)
            rows.append(row)
        return rows

    def test_the_fastest_worlds_carry_the_beam(self):
        kept = explore.beam(self.records([20000, 18000, 19000, 17000]), 2)
        self.assertEqual([r['time_ms'] for r in kept], [17000, 18000])

    def test_a_world_whose_prefix_stopped_hitting_is_not_in_the_beam(self):
        rows = self.records([17000, 19000])
        rows[0]['prefix_broken'] = True
        self.assertEqual([r['time_ms'] for r in explore.beam(rows, 3)], [19000])

    def test_a_world_whose_force_never_fired_is_not_in_the_beam(self):
        rows = self.records([17000, 19000])
        rows[0]['forced'] = -1
        self.assertEqual([r['time_ms'] for r in explore.beam(rows, 3)], [19000])

    def test_an_incomplete_world_is_not_in_the_beam(self):
        rows = self.records([17000, 19000])
        rows[0]['status'] = 'incomplete'
        self.assertEqual([r['time_ms'] for r in explore.beam(rows, 3)], [19000])

    def test_a_prefix_is_broken_when_the_census_misses_one_of_its_entries(self):
        record = dict(branch=dict(prefix=2), census=dict(totals=dict(hits=1, refused=1, absent=0)))
        self.assertTrue(explore.prefix_broken(record))
        record = dict(branch=dict(prefix=2), census=dict(totals=dict(hits=2, refused=0, absent=0)))
        self.assertFalse(explore.prefix_broken(record))

    def test_a_branch_with_no_prefix_has_no_prefix_to_break(self):
        self.assertFalse(explore.prefix_broken(dict(branch=dict(prefix=0), census=dict(totals=None))))


class StopRuleTest(unittest.TestCase):
    def test_two_depths_without_an_improvement_stop_the_search(self):
        self.assertFalse(explore.stalled([True, False], 2))
        self.assertTrue(explore.stalled([True, False, False], 2))

    def test_an_improvement_resets_the_patience(self):
        self.assertFalse(explore.stalled([False, False, True], 2))

    def test_a_search_stops_when_the_prefix_covers_the_leg(self):
        campaign, execute = campaign_over([leg_lines([3200]) + [spike(3200, 1)]] * 4)
        summary = campaign.run()
        self.assertIn('covers its leg', summary['stop'])

    def test_a_search_stops_when_no_branch_improves_for_two_depths(self):
        log = (leg_lines([3200, 3400, 3600, 3800]) +
               [spike(3200, 1), spike(3400, 1), spike(3600, 1), spike(3800, 1)])
        campaign, execute = campaign_over([log] * 12, times=[20000] * 12)
        summary = campaign.run()
        self.assertIn('improved', summary['stop'])
        self.assertEqual(summary['depths'], 2)

    def test_the_best_sequence_is_written_as_a_tape_the_bench_can_load(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        campaign, execute = campaign_over([log] * 4, times=[20000, 18000, 18000, 18000],
                                          max_depth=1)
        summary = campaign.run()
        self.assertEqual(summary['best']['time_ms'], 18000)
        written = Path(summary['tape']).read_text(encoding='utf-8')
        self.assertIn('decisiontape %d map=mp/ctf_kejim' % tape.VERSION, written)
        self.assertEqual(len([r for r in written.splitlines() if r.startswith('e ')]), 2)
        self.assertEqual([e['index'] for e in summary['sequence']], [0, 1])


class ConfirmationTest(unittest.TestCase):
    """A branch's time is a branch world's until one world runs its tape alone.

    The four-class library campaign is the failure this answers: two explorer
    bests whose branch worlds completed in 16,725 and 15,975 ms returned 16,875
    and 18,675 ms the first time the whole sequence ran as a tape, because a
    branch ran under a prefix plus a forced index and a tape does not.
    """

    def campaign(self, **extra):
        log = leg_lines([3200, 3400]) + [spike(3200, 1), spike(3200, 2)]
        return campaign_over([log] * 12, max_depth=1, **extra)

    def test_the_confirmation_runs_the_whole_sequence_with_no_force_and_no_prefix(self):
        campaign, execute = self.campaign()
        campaign.run()
        call = [c for c in execute.calls if c['name'] == 'control-d1c1-cf'][0]
        self.assertIsNone(call['branch'], 'a confirmation forces no index')
        staged = Path(call['tape']).read_text(encoding='utf-8')
        # The whole leg, not the prefix the branch ran under.
        self.assertIn('entries=2', staged.splitlines()[0])
        self.assertEqual(len([r for r in staged.splitlines() if r.startswith('e ')]), 2)

    def test_the_confirmed_time_is_what_the_beam_and_the_incumbent_read(self):
        # Both branches run first, then the beam confirms them in branch-time
        # order: control 20,000; branches 16,000 and 17,000; their
        # confirmations 18,500 and 19,500. The beam and the report must carry
        # 18,500, which is the only time a tape can be asked to reproduce.
        campaign, execute = self.campaign()
        execute.times = [20000, 16000, 17000, 18500, 19500]
        summary = campaign.run()
        self.assertEqual(summary['best']['world'], 'control-d1c1')
        self.assertEqual(summary['confirmed_time_ms'], 18500)
        self.assertEqual(summary['confirmation'], 'control-d1c1-cf')

    def test_a_branch_whose_confirmation_misses_an_entry_is_excluded_and_recorded(self):
        campaign, execute = self.campaign()
        execute.confirm_misses = {'control-d1c1'}
        execute.times = [20000, 16000, 17000, 16000, 19000]
        summary = campaign.run()
        self.assertEqual(summary['best']['world'], 'control-d1c2')
        self.assertEqual([row['world'] for row in summary['unconfirmed']], ['control-d1c1'])
        self.assertEqual(summary['unconfirmed'][0]['branch_time_ms'], 16000)
        self.assertEqual(summary['unconfirmed'][0]['census']['absent'], 1)

    def test_a_confirmation_world_is_never_itself_a_node(self):
        record = dict(world='w-cf', confirm_of='w', status='pass', time_ms=15000,
                      decisions=[1], prefix_broken=False, forced=None)
        self.assertFalse(explore.confirmed(record))
        self.assertEqual(explore.beam([record], 3), [])

    def test_the_control_confirms_nothing(self):
        root = dict(world='control', status='pass', time_ms=20000, decisions=[1],
                    prefix_broken=False, forced=None, branch=None)
        self.assertTrue(explore.confirmed(root))
        self.assertEqual(explore.score_time(root), 20000)

    def test_a_branch_with_no_confirmation_is_not_confirmed(self):
        row = dict(world='w', branch=dict(prefix=0), status='pass', time_ms=16000,
                   decisions=[1], prefix_broken=False, forced=1)
        self.assertFalse(explore.confirmed(row))
        row['confirmation'] = dict(status='pass', time_ms=16500,
                                   census=dict(totals=dict(entries=2, hits=2, refused=0, absent=0)))
        self.assertTrue(explore.confirmed(row))
        self.assertEqual(explore.score_time(row), 16500)

    def test_a_confirmation_that_refused_an_entry_does_not_confirm(self):
        totals = dict(entries=2, hits=1, refused=1, absent=0)
        self.assertFalse(explore.confirmed_hit_all(dict(census=dict(totals=totals))))
        totals = dict(entries=2, hits=2, refused=0, absent=0)
        self.assertTrue(explore.confirmed_hit_all(dict(census=dict(totals=totals))))
        self.assertFalse(explore.confirmed_hit_all(dict(census=dict(totals=None))))

    def test_only_the_beams_own_width_is_confirmed(self):
        # Three branches, a beam of one: the confirmation costs one world per
        # beam slot, not one per branch.
        log = leg_lines([3200, 3400]) + [spike(3200, 1), spike(3200, 2), spike(3200, 3)]
        campaign, execute = campaign_over([log] * 12, max_depth=1, beam=1)
        campaign.run()
        self.assertEqual([c['name'] for c in execute.calls if c['name'].endswith('-cf')],
                         ['control-d1c1-cf'])

    def test_a_resumed_campaign_does_not_re_run_a_confirmation(self):
        campaign, execute = self.campaign()
        first = campaign.run()
        calls = len(execute.calls)
        again = explore.Campaign(campaign.options, execute).run()
        self.assertEqual(len(execute.calls), calls, 'a confirmation was run twice')
        self.assertEqual(first['confirmed_time_ms'], again['confirmed_time_ms'])
        self.assertEqual(first['best']['world'], again['best']['world'])


class ResumeTest(unittest.TestCase):
    def test_a_world_already_in_the_progress_file_is_never_run_again(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        campaign, execute = campaign_over([log] * 4, max_depth=1)
        campaign.run()
        first = len(execute.calls)
        again = explore.Campaign(campaign.options, execute)
        again.run()
        self.assertEqual(len(execute.calls), first, 'a resumed campaign ran a world twice')
        self.assertEqual(len(again.records), first)

    def test_a_resumed_campaign_reaches_the_same_result(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        campaign, execute = campaign_over([log] * 4, max_depth=1)
        first = campaign.run()
        again = explore.Campaign(campaign.options, execute).run()
        self.assertEqual(first['best']['world'], again['best']['world'])
        self.assertEqual(first['sequence'], again['sequence'])

    def test_a_world_retained_without_a_progress_line_is_read_not_rerun(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        campaign, execute = campaign_over([log] * 4, max_depth=1)
        world(log, campaign.out / 'control', time_ms=21000)
        campaign.run()
        self.assertEqual([call['name'] for call in execute.calls[:1]], ['control-d1c1'])

    def test_the_progress_file_carries_one_line_per_world(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1), spike(3200, 2)]
        campaign, execute = campaign_over([log] * 4, max_depth=1)
        campaign.run()
        lines = [json.loads(line) for line in
                 (campaign.out / 'progress.jsonl').read_text(encoding='utf-8').splitlines()]
        self.assertEqual([row['world'] for row in lines],
                         ['control', 'control-d1c1', 'control-d1c2',
                          'control-d1c1-cf', 'control-d1c2-cf'])


class RetryTest(unittest.TestCase):
    def test_a_world_that_never_landed_is_retried_under_its_own_name(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        out = temp()
        out.mkdir(parents=True)
        execute = Execute([log] * 6)
        first = [True]

        def run(name, directory, tape_path, branch):
            code = execute(name, directory, tape_path, branch)
            if first[0]:
                first[0] = False
                # What the bench leaves behind when the pinned class never came.
                world(log, directory, status='incomplete', time_ms=None)
            return code

        campaign = explore.Campaign(options(out, max_depth=1), run)
        campaign.run()
        self.assertEqual([call['name'] for call in execute.calls[:2]], ['control', 'control-a2'])
        self.assertTrue((out / 'control').is_dir(), 'the world that missed must be retained')

    def test_a_world_that_completed_is_not_retried(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        campaign, execute = campaign_over([log] * 4, max_depth=1)
        campaign.run()
        self.assertEqual([call['name'] for call in execute.calls],
                         ['control', 'control-d1c1', 'control-d1c1-cf'])


class CommandTest(unittest.TestCase):
    def test_the_bench_is_told_the_class_the_tape_and_the_forced_index(self):
        opts = options(Path('out'), graph=Path('seed.txt'), source_root=Path('.'),
                       corner_class_min=3000, corner_class_max=5500, attempts=4)
        command = explore.bench_command(opts, Path('out/w'), Path('t.tape'),
                                        dict(nth=2, cand=15), 37400)
        self.assertIn('--corner-class', command)
        self.assertEqual(command[command.index('--corner-class') + 1], '75')
        self.assertIn('--decision-tape', command)
        self.assertIn('bot_moveSelectNth=2', command)
        self.assertIn('bot_moveSelectCand=15', command)
        self.assertIn('--parallel', command)
        self.assertEqual(command[command.index('--parallel') + 1], '1')

    def test_the_case_defaults_to_the_level_one_stand_and_can_be_named(self):
        opts = options(Path('out'), graph=None, source_root=Path('.'),
                       corner_class_min=3000, corner_class_max=5500, attempts=4)
        command = explore.bench_command(opts, Path('out/w'), None, None, 37400)
        self.assertEqual(command[command.index('--filter') + 1], '^red_out_L1_stand$')
        opts.case = 'red_out_L2_stand'
        command = explore.bench_command(opts, Path('out/w'), None, None, 37400)
        self.assertEqual(command[command.index('--filter') + 1], '^red_out_L2_stand$')
        self.assertEqual(command.count('--filter'), 1)

    def test_a_world_with_no_branch_sets_no_override(self):
        opts = options(Path('out'), graph=None, source_root=Path('.'),
                       corner_class_min=3000, corner_class_max=5500, attempts=4)
        command = explore.bench_command(opts, Path('out/w'), None, None, 37400)
        self.assertNotIn('--decision-tape', command)
        self.assertFalse([token for token in command if token.startswith('bot_moveSelect')])

    def test_a_set_override_replaces_the_base_value_once(self):
        opts = options(Path('out'), graph=None, source_root=Path('.'),
                       corner_class_min=3000, corner_class_max=5500, attempts=4,
                       set=['bot_moveExitSpeed=0', 'bot_moveFlightMode=2'])
        command = explore.bench_command(opts, Path('out/w'), None, None, 37400)
        exits = [t for t in command if t.startswith('bot_moveExitSpeed=')]
        self.assertEqual(exits, ['bot_moveExitSpeed=0'])
        self.assertIn('bot_moveFlightMode=2', command)
        self.assertEqual(command[command.index('bot_moveFlightMode=2') - 1], '--set')


class CensusTest(unittest.TestCase):
    def test_the_census_totals_are_read_from_the_engines_own_line(self):
        found = explore.census(census(11, 0, 0, 0))
        self.assertEqual(found['totals'], dict(entries=11, hits=11, refused=0, absent=0,
                                               deferred=0, cursor=11))

    def test_the_librarys_selection_on_the_totals_line_is_kept_as_printed(self):
        # The engine's totals line carries the key the leg formed, the rung
        # that answered it and the file chosen, beside the tallies. A field
        # that is not a count must survive rather than fail the read.
        line = ('2026-09-22 10:00:02 [TAPE] census entries=11 hits=11 refused=0 absent=0 '
                'deferred=1 cursor=11 leg=t1e83 cell=0 phase=75 rung=exact '
                'file=mp_ctf_kejim_-1301924053/c75-best.tape exact=1 phase_keys=1 '
                'link_keys=0 keys=2\n')
        totals = explore.census(line)['totals']
        self.assertEqual(totals['hits'], 11)
        self.assertEqual(totals['leg'], 't1e83')
        self.assertEqual(totals['rung'], 'exact')
        self.assertEqual(totals['cell'], 0)
        self.assertEqual(totals['file'], 'mp_ctf_kejim_-1301924053/c75-best.tape')

    def test_a_forced_commit_reports_whether_the_index_fired(self):
        text = ''.join([commit(3200, nth=1, forced=0), commit(3400, nth=2, forced=1)])
        self.assertEqual(explore.forced_commit(text, 2), 1)
        self.assertEqual(explore.forced_commit(text, 1), 0)

    def test_a_named_candidate_that_was_not_admitted_is_reported_as_not_forced(self):
        text = commit(3400, nth=2, forced=-1)
        self.assertEqual(explore.forced_commit(text, 2), -1)

    def test_the_override_refusing_before_the_release_is_not_the_legs_branch(self):
        # The counter restarts at the release, so the bot's own pre-release
        # walk offers the same ordinal and the override refuses there.
        text = ''.join([commit(2525, nth=1, forced=-1), commit(4325, nth=1, forced=1)])
        self.assertEqual(explore.forced_commit(text, 1, 3875), 1)
        self.assertEqual(explore.forced_commit(text, 1), -1)


# ---------------------------------------------------------------------------
# Route branching


def plan_path(level, gen, replans=1, cost_ms=14054, directive=None, client=0):
    """The engine's replan line; the directive fields only when one applied."""
    line = ('2026-09-22 10:00:01 [MOVE] client=%d level=%d event=plan edge=none reason=path '
            'speed=0.0 force=0 speed_ms=0 x=152.0 y=-1160.0 z=1924.1 from=0 to=1 cells=22 '
            'edges=17 cost_ms=%d replans=%d elapsed_ms=0 state=1 strafe=17 expanded=171 '
            'overflow=0 avoid=0 start_speed=0.0 start_tap=0 gen=%d corridor=0 corridor_cells=0 '
            'corridor_from=-1.0' % (client, level, cost_ms, replans, gen))
    if directive:
        line += (' directive=%d plan_avoid=%d require=%d require_met=%d'
                 % (directive.get('flags', 0), directive.get('plan_avoid', 0),
                    directive.get('require', -1), directive.get('require_met', 0)))
    return line + '\n'


def route_line(level, gen, edges, client=0):
    return ('2026-09-22 10:00:01 [ROUTE] client=%d level=%d gen=%d source=replan from=0 to=1 '
            'cells=22 cost_ms=14054 state=1 graph=g1-a total=%d first=0 count=%d edges=%s\n'
            % (client, level, gen, len(edges), len(edges), ','.join(str(e) for e in edges)))


def routed_log(route, directive=None, levels=(3200, 3400), spikes=(1,)):
    """A leg whose release plan installed `route`, with a pre-release plan before it."""
    lines = [MAP_LOAD, plan_path(1825, 1), route_line(1825, 1, [99, 98])]
    lines += [plan_path(3075, 2, directive=directive), route_line(3075, 2, route)]
    lines += leg_lines(list(levels))[1:]
    lines += [spike(levels[0], cand) for cand in spikes]
    return lines


class RouteReadTest(unittest.TestCase):
    def test_the_release_route_is_the_first_plan_of_the_leg_and_its_route_line(self):
        text = ''.join(routed_log([10, 11, 12]))
        route = explore.release_route(text, 3075)
        self.assertEqual(route['edges'], [10, 11, 12])
        self.assertEqual((route['level'], route['generation'], route['cost_ms']), (3075, 2, 14054))
        self.assertEqual(route['directive'], 0)
        # The pre-release plan is the bot's own free play, never the leg's.
        self.assertEqual(explore.release_route(text, 1000)['edges'], [99, 98])

    def test_a_later_replan_is_not_the_release_route(self):
        text = ''.join([MAP_LOAD, plan_path(3100, 3, replans=2), route_line(3100, 3, [5])])
        self.assertIsNone(explore.release_route(text, 3075))

    def test_an_avoid_takes_only_when_the_edge_is_gone_and_the_line_says_it_applied(self):
        route = dict(edges=[10, 12], directive=1, plan_avoid=1, require=-1, require_met=0)
        self.assertTrue(explore.route_taken(route, dict(avoid=[11], gen=1)))
        self.assertFalse(explore.route_taken(dict(route, edges=[10, 11]), dict(avoid=[11], gen=1)))
        self.assertFalse(explore.route_taken(dict(route, directive=0), dict(avoid=[11], gen=1)))

    def test_a_require_takes_only_when_the_planner_met_it(self):
        route = dict(edges=[10, 40, 12], directive=2, plan_avoid=0, require=40, require_met=1)
        self.assertTrue(explore.route_taken(route, dict(require=40, gen=1)))
        # No route took the edge: the ordinary search ran and the line says so.
        self.assertFalse(explore.route_taken(dict(route, edges=[10, 11], require_met=0),
                                             dict(require=40, gen=1)))
        self.assertFalse(explore.route_taken(None, dict(require=40, gen=1)))

    def test_the_bench_is_told_the_directive_for_the_first_plan(self):
        opts = options(Path('out'), graph=None, source_root=Path('.'),
                       corner_class_min=3000, corner_class_max=5500, attempts=4)
        command = explore.bench_command(opts, Path('out/w'), None, None, 37400,
                                        dict(avoid=[2374], gen=1))
        self.assertIn('bot_movePlanAvoid=2374', command)
        self.assertIn('bot_movePlanGen=1', command)
        self.assertFalse([t for t in command if t.startswith('bot_movePlanRequire')])
        command = explore.bench_command(opts, Path('out/w'), None, None, 37400,
                                        dict(require=815, gen=1))
        self.assertIn('bot_movePlanRequire=815', command)
        self.assertFalse([t for t in explore.bench_command(opts, Path('out/w'), None, None, 37400)
                          if t.startswith('bot_movePlan')])


CACHE_HEADER = ('graph 7 map=mp/ctf_kejim checksum=-1 grid=32 gravity=800 speed=250 step=18 cells=2 '
                'nodes=1 edges={n} seeds=0 seed_refused=0 samples=0 refusals=0 links=0 refined=0 '
                'boundary=0 capped=0 hints=0 hints_bound=0')


def cache_edge(src, dst, cost=600, tap=1, entry=500.0):
    return (f'e 5 {src} {dst} 0.000 0.000 0.000 0.000 0.000 0.000 1.000000 0.000000 {cost} 1 '
            f'{tap} {entry:.1f} 1.000000 0.000000 540.0 1.000000 0.000000')


def cache_file(path, edges):
    path.write_text('\n'.join([CACHE_HEADER.format(n=len(edges)), 'r fill 0',
                               'c 0 0 0.000 1 -1 -1 -1 0 0 0 0 -1', 'c 1 0 0.000 -1 0 -1 -1 0 0 0 0 -1',
                               'n 0 0 2 0.000 0.000 32.0 0'] + edges) + '\n', encoding='utf-8')
    return path


class ReferenceTest(unittest.TestCase):
    def test_the_reference_tapes_state_edges_resolve_by_fold_identity(self):
        root = temp()
        root.mkdir(parents=True)
        # The reference graph: index 2 is 5->6, index 3 is 6->7 (the state pair);
        # the explored graph holds 6->7 at index 0 and 5->6 nowhere.
        reference = cache_file(root / 'seed.txt', [cache_edge(1, 2), cache_edge(3, 4),
                                                   cache_edge(5, 6), cache_edge(6, 7, entry=520.0)])
        explored = cache_file(root / 'v3.txt', [cache_edge(6, 7, cost=550, entry=525.0), cache_edge(1, 2)])
        tape_path = root / 'ref.tape'
        tape_path.write_text('decisiontape 3 map=mp/ctf_kejim checksum=-1 entries=3\n'
                             '# source=%s\n# corner_level=3875\n'
                             'e 0 1.0 2.0 3.0 straight 2 1.2 24 0 0 -1 -1 4948 4949\n'
                             'e 1 1.0 2.0 3.0 state 2 0.5 24 1.1 0 2 3 6 7\n'
                             'e 2 1.0 2.0 3.0 state 1 0.5 0 0 0 3 -1 7 -1\n'
                             % (root / 'w' / 'server.log'), encoding='utf-8')
        resolved, unresolved = explore.reference_edges(tape_path, reference, explored)
        self.assertEqual(resolved, [dict(reference=3, edge=0, entry=1)])
        self.assertEqual(unresolved, [dict(reference=2, why='absent_from_explored_graph')])
        # Without --reference-graph, the tape's own world's saved cache is read.
        (root / 'w' / 'base' / 'bot2-graph').mkdir(parents=True)
        cache_file(root / 'w' / 'base' / 'bot2-graph' / 'mp_ctf_kejim_-1.txt',
                   [cache_edge(9, 9), cache_edge(9, 8), cache_edge(1, 2), cache_edge(6, 7, entry=520.0)])
        resolved, unresolved = explore.reference_edges(tape_path, None, explored)
        self.assertEqual([r['edge'] for r in resolved], [1, 0])
        self.assertEqual(unresolved, [])


class RouteExecute(Execute):
    """Worlds whose release route follows the directive they were given.

    The control's route is 10, 11, 12. Avoiding 10 or 11 gives the same new
    route (10 and 11 are one corridor), avoiding 12 gives another; requiring
    40 takes it, requiring 41 finds no route and the ordinary search runs.
    """
    ROUTES = {None: [10, 11, 12], ('avoid', 10): [20, 21, 12], ('avoid', 11): [20, 21, 12],
              ('avoid', 12): [10, 11, 30], ('require', 40): [10, 40, 12], ('require', 41): [10, 11, 12]}

    def __init__(self, times):
        Execute.__init__(self, [], times)

    def __call__(self, name, directory, tape_path, branch, directive=None):
        key, flags = None, None
        if directive and directive.get('avoid'):
            key = ('avoid', directive['avoid'][0])
            flags = dict(flags=1, plan_avoid=1)
        elif directive and directive.get('require') is not None:
            key = ('require', directive['require'])
            flags = dict(flags=2, require=directive['require'], require_met=int(key[1] == 40))
        self.logs = [routed_log(self.ROUTES[key], flags, spikes=(1, 2))]
        return Execute.__call__(self, name, directory, tape_path, branch, directive)


class RouteBranchTest(unittest.TestCase):
    def campaign(self, times, reference=(), **extra):
        out = temp()
        out.mkdir(parents=True)
        execute = RouteExecute(times)
        campaign = explore.Campaign(options(out, route_branch=True, **extra), execute)
        campaign.reference = [dict(reference=100 + e, edge=e, entry=1) for e in reference]
        return campaign, execute

    def test_each_route_edge_is_avoided_and_each_reference_edge_required_on_the_first_plan(self):
        # control, rq40, rq41, av10, av11, av12, then the confirmations of the
        # roots that took and are new: rq40, av10 (av11 is its duplicate), av12.
        campaign, execute = self.campaign([20000, 17000, 16000, 19000, 19000, 18000,
                                           17100, 19100, 18100], reference=(40, 41, 11),
                                          max_depth=0, patience=1)
        roots = campaign.route_roots(campaign.world('control', 0, None, None))
        names = [c['name'] for c in execute.calls]
        self.assertEqual(names[:6], ['control', 'control-rq40', 'control-rq41', 'control-av10',
                                     'control-av11', 'control-av12'])
        self.assertEqual(execute.calls[3]['directive'], dict(avoid=[10], gen=1))
        self.assertEqual(execute.calls[1]['directive'], dict(require=40, gen=1))
        self.assertIsNone(execute.calls[0]['directive'])
        by = {r['world']: r for r in roots}
        self.assertEqual(by['control-av11']['route_duplicate'], 'control-av10')
        self.assertFalse(by['control-rq41']['route_taken'], 'a require no route met is a route miss')
        self.assertTrue(by['control-rq40']['route_taken'])
        # 11 is on the control's route already: required, it would branch nothing.
        self.assertNotIn('control-rq11', names)
        # The confirmations: the root's whole leg as a tape, under its directive.
        confirms = [c for c in execute.calls if c['name'].endswith('-cf')]
        self.assertEqual([c['name'] for c in confirms],
                         ['control-rq40-cf', 'control-av10-cf', 'control-av12-cf'])
        for call in confirms:
            self.assertIsNone(call['branch'])
            self.assertIsNotNone(call['tape'])
            self.assertEqual(call['directive'], by[call['name'][:-3]]['directive'])
        self.assertTrue(explore.confirmed(by['control-rq40']))
        self.assertEqual(explore.score_time(by['control-rq40']), 17100)
        self.assertFalse(explore.confirmed(by['control-av11']))
        self.assertFalse(explore.confirmed(by['control-rq41']))

    def test_route_roots_compete_for_the_beam_and_their_children_inherit_the_directive(self):
        campaign, execute = self.campaign([20000, 17000, 19000, 19000, 18000, 17100, 19100, 18100] + [15000] * 40,
                                          reference=(40,), max_depth=1, beam=2)
        summary = campaign.run()
        # The depth-0 beam is the two fastest confirmed roots: rq40 (17,100) and
        # av12 (18,100); the control (20,000) and av10 (19,100) are out.
        children = [c for c in execute.calls if '-d1c' in c['name'] and not c['name'].endswith('-cf')]
        parents = sorted({c['name'].split('-d1c')[0] for c in children})
        self.assertEqual(parents, ['control-av12', 'control-rq40'])
        for call in children:
            parent = call['name'].split('-d1c')[0]
            expected = dict(require=40, gen=1) if parent == 'control-rq40' else dict(avoid=[12], gen=1)
            self.assertEqual(call['directive'], expected)
        self.assertTrue(summary['route_branch'])
        self.assertEqual(len(summary['route_worlds']), 4)
        self.assertIsNotNone(summary['directive'])

    def test_a_capped_run_keeps_the_named_inclusions(self):
        campaign, execute = self.campaign([20000] * 12, reference=(40,), max_depth=0,
                                          route_branches=2)
        campaign.route_roots(campaign.world('control', 0, None, None))
        self.assertEqual([c['name'] for c in execute.calls if not c['name'].endswith('-cf')],
                         ['control', 'control-rq40', 'control-av10'])

    def test_without_route_branching_nothing_changes(self):
        log = leg_lines([3200, 3400]) + [spike(3200, 1)]
        campaign, execute = campaign_over([log] * 4, max_depth=1)
        campaign.run()
        self.assertEqual([c['name'] for c in execute.calls],
                         ['control', 'control-d1c1', 'control-d1c1-cf'])
        self.assertTrue(all(c['directive'] is None for c in execute.calls))


if __name__ == '__main__':
    unittest.main()
