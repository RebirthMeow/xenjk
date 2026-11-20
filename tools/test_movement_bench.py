"""Synthetic tests for the movement benchmark's pure helpers."""
import json
from pathlib import Path
import tempfile
import unittest

from movement_bench_lib import (campaign_verdict, composition_verdict, corner_pin_state, event_summary,
                                evaluate, latest_play_level, load_cases, parity_summary, parse_move_events,
                                select_cases, state_audit, straight_audit, validate_corner_level,
                                corner_class_state, validate_corner_class, CORNER_CLASS_MODULUS)
from movement_campaign_compare import case_time, deltas, family_means, parse_spike_lines, summarize
from movement_graph_report import cell_under, columns, load_graph

FIXTURE = Path(__file__).resolve().parent / 'fixtures' / 'movement' / 'kejim' / 'cases.json'


def case(level=1, leg='outbound', force=0, bound=180000):
    templates = {1: ['walk', 'corner', 'hop', 'tap'],
                 2: ['walk', 'corner', 'hop', 'tap', 'speed'],
                 3: ['walk', 'corner', 'hop', 'tap', 'speed', 'hold']}[level]
    return dict(name='t', team='red', leg=leg, level=level,
                start=dict(name='s', origin=[0, 0, 0], yaw=0, velocity=[0, 0, 0],
                           carrying=1 if leg == 'return' else 0),
                resources=dict(force=force, speed_ms=0), spend=dict(templates=templates),
                goal='home_flag' if leg == 'return' else 'enemy_flag',
                pass_condition=dict(condition='capture' if leg == 'return' else 'pickup',
                                    bound_ms=bound, no_death=True))


def row(t, **changes):
    values = dict(level_time=t, health=100, force=0, powers_active=0, up=0, carrying=0,
                  captures=0, movement_only=1, movement_owner='move')
    values.update(changes)
    return {k: str(v) for k, v in values.items()}


class Fixture(unittest.TestCase):
    def test_declared_set_is_36_valid_cases(self):
        doc, digest = load_cases(FIXTURE)
        self.assertEqual(len(doc['cases']), 36)
        self.assertEqual(len(digest), 64)
        self.assertEqual(len(select_cases(doc['cases'], level=1)), 12)
        self.assertEqual(len(select_cases(doc['cases'], pattern=r'^red_ret_')), 9)
        teams = {(c['team'], c['leg'], c['level']) for c in doc['cases']}
        self.assertEqual(len(teams), 12)

    def test_invalid_case_is_refused(self):
        bad = dict(format='MOVEBENCH1', map='m', bsp_sha256='x', cases=[case()])
        bad['cases'][0]['spend']['templates'].append('speed')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'cases.json'
            path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):
                load_cases(path)


class Evaluate(unittest.TestCase):
    def test_pickup_inside_bound_passes(self):
        rows = [row(1000), row(2000), row(3000, carrying=1), row(4000, carrying=1)]
        result = evaluate(case(), rows, 500)
        self.assertEqual((result['status'], result['reason'], result['time_ms']), ('pass', 'complete', 2500))

    def test_rows_before_start_are_ignored(self):
        rows = [row(100, health=0), row(1000), row(2000, carrying=1)]
        self.assertEqual(evaluate(case(), rows, 500)['status'], 'pass')

    def test_death_is_red(self):
        rows = [row(1000), row(2000, health=0), row(3000, carrying=1)]
        result = evaluate(case(), rows, 500)
        self.assertEqual((result['status'], result['reason'], result['deaths']), ('fail', 'death', 1))

    def test_stall_past_bound_is_red(self):
        rows = [row(1000), row(200000)]
        self.assertEqual(evaluate(case(), rows, 500)['reason'], 'stall')
        rows = [row(1000), row(200000, carrying=1)]
        self.assertEqual(evaluate(case(), rows, 500)['reason'], 'stall')

    def test_forbidden_speed_at_level_one(self):
        rows = [row(1000), row(2000, powers_active=4), row(3000, carrying=1)]
        self.assertEqual(evaluate(case(), rows, 500)['reason'], 'forbidden_speed')
        rows = [row(1000, force=50), row(2000, force=0, powers_active=4), row(3000, force=0, carrying=1)]
        self.assertEqual(evaluate(case(level=2, force=50), rows, 500)['status'], 'pass')

    def test_forbidden_hold_below_the_pool(self):
        rows = [row(1000, force=50), row(2000, force=40), row(3000, force=40, carrying=1)]
        self.assertEqual(evaluate(case(level=2, force=50), rows, 500)['reason'], 'forbidden_hold')
        rows = [row(1000, force=50), row(2000, force=0, powers_active=4), row(3000, force=0, carrying=1)]
        self.assertEqual(evaluate(case(level=2, force=50), rows, 500)['status'], 'pass')
        rows = [row(1000, force=50), row(2000, force=0), row(3000, force=0, carrying=1)]
        self.assertEqual(evaluate(case(level=2, force=50), rows, 500)['reason'], 'forbidden_hold')
        self.assertEqual(evaluate(case(level=3, force=100), rows, 500)['status'], 'pass')

    def test_capture_condition_for_return_leg(self):
        rows = [row(1000, carrying=1), row(2000, carrying=0, captures=1)]
        result = evaluate(case(leg='return'), rows, 500)
        self.assertEqual((result['status'], result['time_ms']), ('pass', 1500))

    def test_world_ending_early_is_incomplete(self):
        result = evaluate(case(), [row(1000), row(2000)], 500)
        self.assertEqual((result['status'], result['reason']), ('incomplete', 'world_ended'))
        self.assertEqual(evaluate(case(), [], 500)['reason'], 'no_rows')


class ChainCapture(unittest.TestCase):
    @staticmethod
    def chain_case(points):
        c = case(level=1)
        c.update(leg='chain', chain=points)
        c['pass_condition']['condition'] = 'capture'
        return c

    def test_a_capture_before_the_chain_completes_does_not_pass(self):
        rows = [row(750, captures=1), row(1000, captures=1)]
        result = evaluate(self.chain_case([[0, 0, 0], [1, 1, 1]]), rows, 500, None, None)
        self.assertEqual(result['status'], 'incomplete')

    def test_only_a_capture_after_the_chain_completes_passes(self):
        rows = [row(750, captures=1), row(1500, captures=1), row(2500, captures=2)]
        result = evaluate(self.chain_case([[0, 0, 0]]), rows, 500, None, 1200)
        self.assertEqual((result['status'], result['time_ms']), ('pass', 2000))

    def test_a_capture_shown_on_the_completion_frame_is_the_baseline(self):
        rows = [row(1000), row(1250, captures=1), row(1500, captures=1)]
        result = evaluate(self.chain_case([[0, 0, 0]]), rows, 500, None, 1250)
        self.assertEqual(result['status'], 'incomplete')
        rows.append(row(1750, captures=2))
        result = evaluate(self.chain_case([[0, 0, 0]]), rows, 500, None, 1250)
        self.assertEqual((result['status'], result['time_ms']), ('pass', 1250))

    def test_an_empty_chain_is_still_the_round_trip(self):
        rows = [row(750), row(1000, captures=1)]
        result = evaluate(self.chain_case([]), rows, 500, None, None)
        self.assertEqual((result['status'], result['time_ms']), ('pass', 500))


def move(level, event, edge, reason, **extra):
    fields = dict(client=0, level=level, event=event, edge=edge, reason=reason)
    fields.update(extra)
    return {k: str(v) for k, v in fields.items()}


class Composition(unittest.TestCase):
    SEQUENCE = [move(1100, 'plan', 'hop0', 'sequence', id=2, admitted=36),
                move(1200, 'edge', 'hop0', 'launch'), move(1700, 'edge', 'hop0', 'land'),
                move(1725, 'edge', 'hop1', 'launch'), move(2300, 'edge', 'hop1', 'land'),
                move(2900, 'edge', 'onward', 'complete', parity_index=95, mismatches=0)]

    def spike_case(self):
        c = case(leg='return')
        c['pass_condition'] = dict(condition='composition', bound_ms=20000, no_death=True)
        return c

    def test_complete_sequence_passes_at_its_time(self):
        v = composition_verdict(self.SEQUENCE, 1000)
        self.assertEqual((v['status'], v['reason'], v['time_ms']), ('pass', 'complete', 1900))
        self.assertEqual(v['edges'][0], 'hop0:launch')
        result = evaluate(self.spike_case(), [row(1050, carrying=1), row(3000, carrying=1)], 1000, self.SEQUENCE)
        self.assertEqual((result['status'], result['time_ms']), ('pass', 1900))

    def test_refusal_miss_and_parity_are_red(self):
        for events, reason in ((self.SEQUENCE[:1] + [move(1200, 'plan', 'none', 'none', candidates=36)], 'no_sequence'),
                               (self.SEQUENCE[:3] + [move(1750, 'miss', 'hop1', 'jump_refused')], 'miss:jump_refused'),
                               (self.SEQUENCE[:2] + [move(1300, 'parity', 'walk', 'command', frame=4)], 'parity:command')):
            result = evaluate(self.spike_case(), [row(1050, carrying=1)], 1000, events)
            self.assertEqual((result['status'], result['reason']), ('fail', reason))

    def test_pending_sequence_stays_incomplete_until_the_bound(self):
        result = evaluate(self.spike_case(), [row(1050, carrying=1)], 1000, self.SEQUENCE[:4])
        self.assertEqual(result['status'], 'incomplete')
        result = evaluate(self.spike_case(), [row(1050, carrying=1), row(22000, carrying=1)], 1000, self.SEQUENCE[:4])
        self.assertEqual((result['status'], result['reason']), ('fail', 'stall'))

    def test_death_beats_the_sequence(self):
        result = evaluate(self.spike_case(), [row(1050, carrying=1), row(1500, health=0)], 1000, self.SEQUENCE)
        self.assertEqual((result['status'], result['reason']), ('fail', 'death'))


class ParityTally(unittest.TestCase):
    """The comparison count a world is read by.

    The engine's periodic ``horizon`` event prints only when the horizon was
    exhausted and an arbitrary rollout counter came up, so four class-75
    control worlds on three builds compared frames and reported none
    (`docs/movement/records/2026-09-22-tape-library.md`). ``Move_ParityTally``
    emits one ``tally`` per leg with the count the leg actually made.
    """

    def tally(self, frames, **extra):
        return move(2900, 'parity', 'walk', 'tally', frames=frames, mismatches=0,
                    refusals=0, rollouts=1100, horizon=256, planned=0, truncated=0,
                    index=52, **extra)

    def test_the_tally_is_the_count_not_the_largest_horizon_event(self):
        events = [move(1425, 'parity', 'walk', 'horizon', frames=70, mismatches=0),
                  self.tally(718)]
        summary = parity_summary(events)
        self.assertEqual(summary['compared'], 718)
        self.assertEqual(summary['tallies'], 1)
        self.assertEqual(summary['horizons'], 1)
        self.assertTrue(summary['row1_ok'])

    def test_a_world_that_printed_no_horizon_event_still_reports_its_count(self):
        # `c75-ctl-1` and `c75-ctl-3` of the four-class library campaign: a
        # completed trip, zero mismatches, and no horizon event at all.
        summary = parity_summary([self.tally(698)])
        self.assertEqual((summary['compared'], summary['horizons']), (698, 0))
        self.assertTrue(summary['row1_ok'])

    def test_a_log_from_before_the_tally_is_read_as_it_always_was(self):
        summary = parity_summary([move(1425, 'parity', 'walk', 'horizon', frames=905)])
        self.assertEqual((summary['compared'], summary['tallies']), (905, 0))
        self.assertTrue(summary['row1_ok'])

    def test_a_world_that_really_compared_nothing_still_fails_row_one(self):
        summary = parity_summary([move(1475, 'parity', 'walk', 'forecast_refused')])
        self.assertEqual(summary['compared'], 0)
        self.assertFalse(summary['row1_ok'], 'a zero comparison count must fail row 1')

    def test_a_mismatch_fails_row_one_however_much_was_compared(self):
        summary = parity_summary([self.tally(718),
                                  move(1450, 'parity', 'walk', 'after_state', frame=3)])
        self.assertEqual((summary['compared'], summary['mismatches']), (718, 1))
        self.assertFalse(summary['row1_ok'])

    def test_the_tally_is_not_a_mismatch_and_does_not_fail_a_world(self):
        events = Composition.SEQUENCE[:2] + [self.tally(718)] + Composition.SEQUENCE[2:]
        verdict = composition_verdict(events, 1000)
        self.assertEqual((verdict['status'], verdict['reason']), ('pass', 'complete'))

    def test_the_last_tally_of_a_world_is_the_count(self):
        summary = parity_summary([self.tally(120), self.tally(718)])
        self.assertEqual((summary['compared'], summary['tallies']), (718, 2))


class Events(unittest.TestCase):
    TEXT = '\n'.join([
        'noise',
        '2026-09-15 00:56:50 [MOVE] client=1 level=900 event=walk edge=walk reason=start speed=0.0 force=0 speed_ms=0 x=1 y=2 z=3',
        '2026-09-15 00:56:50 [MOVE] client=1 level=1000 event=plan edge=none reason=walk speed=0.0 force=0 speed_ms=0 x=1 y=2 z=3',
        '[MOVE] client=1 level=1025 event=walk edge=walk reason=start speed=0.0 force=0 speed_ms=0 x=1 y=2 z=3',
        '[MOVE] client=1 level=1425 event=parity edge=walk reason=horizon speed=1.0 force=0 speed_ms=0 x=1 y=2 z=3 frames=16 mismatches=0 refusals=0 rollouts=10',
        '[MOVE] client=1 level=1450 event=parity edge=walk reason=after_state speed=1.0 force=0 speed_ms=0 x=1 y=2 z=3 frame=3 field=origin offset=12',
        '[MOVE] client=1 level=1475 event=parity edge=walk reason=forecast_refused speed=1.0 force=0 speed_ms=0 x=1 y=2 z=3 status=0 unsupported=256',
        '[MOVE] client=1 level=1500 event=recover edge=none reason=offmesh speed=1.0 force=0 speed_ms=0 x=1 y=2 z=3 decline=offmesh offmesh=3',
    ])

    def test_parse_and_summaries(self):
        self.assertEqual(len(parse_move_events(self.TEXT)), 7)
        events = parse_move_events(self.TEXT, after_level=950)
        self.assertEqual(len(events), 6)
        self.assertEqual(events[3]['field'], 'origin')
        parity = parity_summary(events)
        self.assertEqual(parity, dict(mismatches=1, by_reason={'after_state': 1}, refusals=1,
                                      horizons=1, compared=16, tallies=0, tallied=0,
                                      horizon_frames=16, row1_ok=False))
        self.assertEqual(event_summary(events), {'plan:walk': 1, 'walk:start': 1, 'recover:offmesh': 1})

    def test_straight_audit_flags_a_commit_judged_against_a_frozen_walk(self):
        text = ('[MOVE] client=0 level=100 event=plan edge=hop0 reason=sequence kind=straight id=1 hops=2 frames=90 walk_frame=89 walk_stopped=0\n'
                '[MOVE] client=0 level=200 event=plan edge=hop0 reason=sequence kind=straight id=2 hops=2 frames=90 walk_frame=63 walk_stopped=0\n'
                '[MOVE] client=0 level=300 event=plan edge=hop0 reason=sequence kind=straight id=3 hops=1 frames=70 walk_frame=40 walk_stopped=1\n'
                '[MOVE] client=0 level=400 event=plan edge=hop0 reason=sequence kind=gap id=4 frames=50\n')
        audit = straight_audit(parse_move_events(text))
        self.assertEqual(audit['commits'], 3)
        self.assertEqual([s['id'] for s in audit['short']], ['2'])
        self.assertEqual(audit['short'][0]['walk_frame'], 63)
        self.assertEqual(audit['state_commits'], 0)

    def test_state_audit_counts_paths_commits_and_learned_edges(self):
        text = ('[MOVE] client=0 level=100 event=plan edge=none reason=path from=1 to=2 cells=40 edges=2 state=1 strafe=2 expanded=900 overflow=0\n'
                '[MOVE] client=0 level=150 event=plan edge=hop0 reason=sequence kind=state id=1 hops=2 edge=7 edge2=8 envelope=1 frames=80 walk_frame=79 walk_stopped=0\n'
                '[MOVE] client=0 level=200 event=edge edge=strafe reason=refused index=9 tap=1 envelope=0 candidates=4 disabled=0\n'
                '[MOVE] client=0 level=200 event=edge edge=strafe reason=avoid index=9 why=jump_refused avoided=1\n'
                '[MOVE] client=0 level=210 event=plan edge=none reason=path from=3 to=2 cells=30 edges=0 state=0 strafe=0 expanded=0 overflow=1\n'
                '[MOVE] client=0 level=300 event=plan edge=hop0 reason=sequence kind=straight id=2 hops=2 frames=90 walk_frame=89 walk_stopped=0\n'
                '[MOVE] client=0 level=400 event=edge edge=strafe reason=learn kind=strafe hop=0 index=10 merged=0\n'
                '[MOVE] client=0 level=400 event=edge edge=strafe reason=learn kind=strafe hop=1 index=5 merged=1\n')
        events = parse_move_events(text)
        audit = state_audit(events)
        self.assertEqual(audit, dict(paths_state=1, paths_cell=1, overflow=1, strafe_planned=2, commits=1, chains=1,
                                     avoided=1, learned=2, merged=1, envelope=1, refused=1))
        straight = straight_audit(events)
        self.assertEqual((straight['commits'], straight['state_commits'], straight['short']), (2, 1, []))

    def test_graph_merge_folds_strafe_edges_by_band_and_keeps_the_lowest_cost(self):
        import movement_graph_merge as gm
        header = 'graph 4 map=mp_ctf_kejim checksum=-1 grid=32 gravity=800 speed=250 step=18 cells=2 nodes=1 edges=0 seeds=1 seed_refused=0 samples=1 refusals=0 links=1 refined=0 boundary=0 capped=0 hints=0 hints_bound=0'
        cells = ['c 0 0 1.0 1 -1 -1 -1 0 0 1 0 -1', 'c 1 0 1.0 -1 -1 0 -1 0 0 0 0 -1']
        nodes = ['n 0 0 2 1.0 1.0 32.0 0']
        strafe = 'e 5 0 1 0 0 1 300 0 1 1 0 %d 1 1 %s 1 0 400 1 0'
        world_a = (header, [], cells, nodes, [strafe % (600, '300.0'), 'e 1 0 1 0 0 1 100 0 1 1 0 500 1 0 0 0 0 0 0 0'])
        world_b = (header, [], cells, nodes, [strafe % (550, '320.0'), strafe % (700, '380.0'), 'e 1 0 1 0 0 1 100 0 1 1 0 450 1 0 0 0 0 0 0 0'])
        merged_header, _, merged_cells, _, edges, per_world = gm.merge([('a', world_a), ('b', world_b)])
        self.assertEqual(merged_cells, cells)
        self.assertIn(' edges=3 ', merged_header + ' ')
        self.assertEqual(per_world, dict(a=2, b=3))
        costs = sorted(int(line.split()[12]) for line in edges)
        self.assertEqual(costs, [450, 550, 700])
        other = (header.replace('checksum=-1', 'checksum=-2'), [], cells, nodes, [])
        with self.assertRaises(SystemExit):
            gm.merge([('a', world_a), ('c', other)])

    def test_campaign_compare_helpers(self):
        text = ('[MOVE] client=0 level=100 event=plan edge=hop0 reason=sequence kind=straight id=1 hops=2 landing=1 reissued_up=1 arrives=0 rollout_ms=12.5\n'
                '[MOVE] client=0 level=150 event=edge edge=hop0 reason=launch speed=300.0\n'
                '[MOVE] client=0 level=200 event=edge edge=hop0 reason=land speed=400.0\n'
                '[MOVE] client=0 level=210 event=edge edge=hop1 reason=launch speed=390.0\n'
                '[MOVE] client=0 level=300 event=plan edge=none reason=none kind=straight candidates=12 refused=1 rollout_ms=7.5\n'
                '[MOVE] client=0 level=400 event=plan edge=hop0 reason=sequence kind=gap id=2 frames=50\n'
                '[MOVE] client=0 level=500 event=miss edge=hop0 reason=jump_refused\n'
                '[MOVE] client=0 level=600 event=edge edge=speed reason=request kind=speed remaining_ms=9000 force=50\n'
                '[MOVE] client=0 level=625 event=edge edge=speed reason=complete kind=speed purchases=1 window_ms=19975\n'
                '[MOVE] client=0 level=700 event=walk edge=walk reason=brake speed=412.0 why=drop\n'
                '[SPIKE] client=0 level=100 kind=straight plan=1 cand=0 hops=2 outcome=admitted frames=120\n'
                '[SPIKE] client=0 level=100 kind=straight plan=1 cand=1 hops=2 outcome=off_path frames=60\n'
                '[SPIKE] client=0 level=300 kind=straight plan=2 cand=0 hops=1 outcome=refused frames=10\n'
                '[SPIKE] client=0 level=400 kind=gap plan=3 cand=0 hops=1 outcome=admitted frames=40\n')
        s = summarize(parse_move_events(text), parse_spike_lines(text))
        self.assertEqual(s['plans'], {'straight:2': 1, 'gap': 1})
        self.assertEqual((s['straight_commits'], s['at_landing'], s['reissued'], s['arrives'], s['misses']), (1, 1, 1, 0, 1))
        self.assertEqual(s['searches'], 2)
        self.assertEqual((s['speed_purchases'], s['brakes']), (1, 1))
        self.assertEqual(s['search_ms'], [12.5, 7.5])
        self.assertEqual(s['outcomes'], {'admitted': 1, 'off_path': 1, 'refused': 1})
        self.assertEqual((s['launch'], s['launch1'], s['land']), ([300.0], [390.0], [400.0]))
        run = {'a_stand': dict(status='pass', time_ms=1000), 'a_respawn5': dict(status='pass', time_ms=2000),
               'b_respawn4': dict(status='incomplete', time_ms=None)}
        other = {'a_stand': dict(status='pass', time_ms=1100), 'a_respawn5': dict(status='pass', time_ms=2000)}
        self.assertEqual(family_means(run), {'stand': 1000, 'respawn5': 2000, 'respawn4': None})
        d = deltas(run, other)
        self.assertEqual((d['cases'], d['mean'], d['min'], d['max'], d['faster'], d['identical']), (2, -50, -100, 0, 1, 1))

    def test_time_to_death_is_not_a_route_improvement(self):
        run = {'a_stand': dict(status='fail', reason='death', time_ms=6000),
               'b_stand': dict(status='pass', time_ms=19000)}
        other = {'a_stand': dict(status='pass', time_ms=20000),
                 'b_stand': dict(status='pass', time_ms=21000)}
        self.assertEqual(family_means(run)['stand'], 19000)
        self.assertEqual(deltas(run, other)['mean'], -2000)
        self.assertEqual(deltas(run, other)['cases'], 1)
        self.assertEqual(deltas({'a_stand': run['a_stand']}, other), dict(cases=0))
        self.assertEqual(case_time(run['a_stand']), 'fail:death')

    def test_campaign_verdict(self):
        results = [dict(evaluation=dict(status='pass')), dict(evaluation=dict(status='fail'))]
        self.assertEqual(campaign_verdict(results)['green'], False)
        self.assertEqual(campaign_verdict(results[:1])['green'], True)
        self.assertEqual(campaign_verdict([])['green'], False)


class Graph(unittest.TestCase):
    """The preparation's cache file and the report's coverage rule."""
    TEXT = ("graph 3 map=mp/test checksum=7 grid=32 gravity=800 speed=250 step=18 cells=4 nodes=3 edges=1 "
            "seeds=1 seed_refused=0 samples=9 refusals=5 links=2 refined=0 boundary=3 capped=0 hints=1 hints_bound=1\n"
            "e 2 0 2 0 0 24.25 64 0 24.25 1 0 500 0\n"
            "r floor_missing 5\n"
            "c 0 0 24.25 1 -1 -1 -1 14 1 1 0 1\n"
            "c 1 0 24.25 2 -1 0 -1 10 1 0 0 2\n"
            "c 2 0 24.25 -1 -1 1 -1 11 1 0 0 2\n"
            "c 1 0 152.25 -1 -1 -1 -1 15 1 0 1 2\n"
            "n 0 0 3 24.25 24.25 32.0 1\n"
            "n 1 0 1 24.25 24.25 96.0 1\n"
            "n 1 3 1 152.25 152.25 32.0 1\n")

    def test_load_and_coverage(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'mp_test_7.txt'
            path.write_text(self.TEXT)
            meta, grid, cells, nodes, reasons, edges = load_graph(path)
        self.assertEqual((meta['map'], meta['checksum'], grid), ('mp/test', '7', 32))
        self.assertEqual((len(cells), len(nodes), reasons['floor_missing']), (4, 3, 5))
        self.assertEqual(cells[1]['link'], [2, -1, 0, -1])
        self.assertEqual((len(edges), edges[0]['src'], edges[0]['dst'], edges[0]['cost_ms']), (1, 0, 2, 500))
        table = columns(cells)
        self.assertEqual(len(table[(1, 0)]), 2, 'stacked floors in one column are distinct cells')
        # A start between two admitted columns at the floor's height is covered by the nearer one.
        hit = cell_under(cells, table, grid, [44.0, 4.0, 24.125])
        self.assertEqual((hit[1], round(hit[2], 3)), (1, .125))
        # The upper floor of the stacked column covers a start at its own height only.
        self.assertEqual(cell_under(cells, table, grid, [32.0, 0.0, 152.125])[1], 3)
        self.assertIsNone(cell_under(cells, table, grid, [32.0, 0.0, 90.0]))
        self.assertIsNone(cell_under(cells, table, grid, [200.0, 0.0, 24.125]))
class CornerPin(unittest.TestCase):
    """The fixture's start level decides the leg, so a pinned A/B must hit it."""

    HEADER = 'level_time,health,movement_only'

    def log(self, *rows):
        return '\n'.join([self.HEADER] + list(rows))

    def test_latest_level_is_the_last_complete_row(self):
        self.assertEqual(latest_play_level(self.log('3300,100,1', '3325,100,1')), 3325)

    def test_torn_final_line_is_skipped_not_guessed(self):
        # The server is still writing; a short row must not be read as a level.
        self.assertEqual(latest_play_level(self.log('3300,100,1', '3325,10')), 3300)

    def test_unparsable_level_is_skipped(self):
        self.assertEqual(latest_play_level(self.log('3300,100,1', 'x,100,1')), 3300)

    def test_header_only_and_empty_have_no_level(self):
        self.assertIsNone(latest_play_level(self.HEADER))
        self.assertIsNone(latest_play_level(''))

    def test_log_without_a_level_column_has_no_level(self):
        self.assertIsNone(latest_play_level('health,movement_only\n100,1'))

    def test_default_lead_sends_on_the_target_frame_itself(self):
        # Measured: the start lands on the level of the last row seen.
        self.assertEqual(corner_pin_state(None, 3375, 0), 'wait')
        self.assertEqual(corner_pin_state(3350, 3375, 0), 'wait')
        self.assertEqual(corner_pin_state(3375, 3375, 0), 'send')

    def test_pin_gives_up_once_the_target_has_passed(self):
        # Only a successful corner start consumes the world, but a late one is
        # spent on the wrong stratum, so the world has to be replaced.
        self.assertEqual(corner_pin_state(3400, 3375, 0), 'overshot')
        self.assertEqual(corner_pin_state(3400, 3375, 50), 'overshot')

    def test_a_lead_widens_the_send_window_without_moving_the_target(self):
        self.assertEqual(corner_pin_state(3325, 3375, 50), 'send')
        self.assertEqual(corner_pin_state(3375, 3375, 50), 'send')
        self.assertEqual(corner_pin_state(3300, 3375, 50), 'wait')

    def test_unset_pin_validates_and_stays_unset(self):
        self.assertIsNone(validate_corner_level(None))

    def test_pin_must_be_a_positive_multiple_of_the_play_cadence(self):
        self.assertEqual(validate_corner_level(3375), 3375)
        for bad in (0, -25, 3380, 13):
            with self.assertRaises(ValueError):
                validate_corner_level(bad)


class CornerClassPin(unittest.TestCase):
    """A world can only start on its own 250 ms ladder, so pin the class."""

    def test_a_class_member_inside_the_window_starts_the_world(self):
        self.assertEqual(corner_class_state(3350, 150, 3000, 7000), 'send')
        self.assertEqual(corner_class_state(4350, 150, 3000, 7000), 'send')

    def test_a_level_off_the_class_keeps_waiting(self):
        # 3325 is one frame away and a different stratum; it is not a start.
        self.assertEqual(corner_class_state(3325, 150, 3000, 7000), 'wait')
        self.assertEqual(corner_class_state(None, 150, 3000, 7000), 'wait')

    def test_a_class_member_before_the_window_keeps_waiting(self):
        # The runner cannot see the opening levels, so an early member is not
        # a start the harness can actually catch.
        self.assertEqual(corner_class_state(2350, 150, 3000, 7000), 'wait')

    def test_a_world_whose_ladder_never_meets_the_class_is_given_up(self):
        self.assertEqual(corner_class_state(7025, 150, 3000, 7000), 'overshot')

    def test_unset_class_validates_and_stays_unset(self):
        self.assertIsNone(validate_corner_class(None, 3000, 7000))

    def test_class_must_be_a_residue_on_the_play_cadence(self):
        self.assertEqual(validate_corner_class(150, 3000, 7000), 150)
        self.assertEqual(validate_corner_class(0, 3000, 7000), 0)
        for bad in (-25, 13, CORNER_CLASS_MODULUS, CORNER_CLASS_MODULUS + 50):
            with self.assertRaises(ValueError):
                validate_corner_class(bad, 3000, 7000)

    def test_window_must_be_sane_and_contain_a_member_of_the_class(self):
        for low, high in ((0, 7000), (3010, 7000), (5000, 5000), (7000, 3000)):
            with self.assertRaises(ValueError):
                validate_corner_class(150, low, high)
        # A window narrower than the modulus can miss the class entirely.
        with self.assertRaises(ValueError):
            validate_corner_class(150, 3200, 3325)
        self.assertEqual(validate_corner_class(150, 3200, 3350), 150)


class DeferredClassPin(unittest.TestCase):
    """bot_cornerDefer: the game, not the runner's rcon timing, picks the level."""

    def args(self, **values):
        from types import SimpleNamespace
        base = dict(corner_class=150, corner_class_min=3000, corner_class_max=7000, corner_class_rcon=False)
        base.update(values)
        return SimpleNamespace(**base)

    def test_a_class_pin_is_deferred_unless_rcon_is_asked_for(self):
        from movement_bench import corner_deferred
        self.assertTrue(corner_deferred(self.args()))
        self.assertFalse(corner_deferred(self.args(corner_class_rcon=True)))
        self.assertFalse(corner_deferred(self.args(corner_class=None)))

    def test_tools_without_the_option_keep_the_immediate_start(self):
        from types import SimpleNamespace
        from movement_bench import corner_deferred
        self.assertFalse(corner_deferred(SimpleNamespace()))
        self.assertTrue(corner_deferred(SimpleNamespace(corner_class=50)))

    def test_the_deferred_start_is_read_from_the_server_log(self):
        from movement_bench import await_deferred_start, CORNER_START
        import time
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'server.log'
            log.write_text('x\n2026 [CORNER] START map=mp/ctf4 level=3150 client=0\n')
            found = await_deferred_start(log, time.monotonic() + 1)
            self.assertEqual(CORNER_START.search(found).group(1), '3150')
            log.write_text('[CORNER] REFUSED deferred window passed level=7025 class=150 max=7000\n')
            found = await_deferred_start(log, time.monotonic() + 1)
            self.assertIsNone(CORNER_START.search(found))
            self.assertIn('window passed', found)


class DefenderLearning(unittest.TestCase):
    """The behaviours from the human 1v1 study set cvars only when switched on."""
    @staticmethod
    def args(**on):
        from types import SimpleNamespace
        base = dict(map='mp/ctf_kejim', defender_rearm=False, defender_reset=False, defender_mines=0, chaser_ambush=False)
        base.update(on)
        return SimpleNamespace(**base)

    def test_off_sets_nothing(self):
        from movement_bench import defender_learning_settings
        self.assertEqual(defender_learning_settings(self.args(), 'red'), {})

    def test_mines_and_ambush_carry_their_points(self):
        from movement_bench import defender_learning_settings
        got = defender_learning_settings(self.args(defender_mines=2, chaser_ambush=True, defender_rearm=True), 'red')
        self.assertEqual(got['bot_defenderMines'], 2)
        self.assertEqual(len(got['bot_defenderMineSpots'].split(';')), 2)
        self.assertEqual(len(got['bot_chaserAmbushPosts'].split(';')), 3)
        self.assertEqual(got['bot_defenderRearm'], 1)
        self.assertNotIn('bot_defenderReset', got)
        self.assertNotIn('bot_defenderMineSpots', defender_learning_settings(self.args(defender_mines=3), None))


class MirrorTwins(unittest.TestCase):
    """--skip-twins: a return leg repeats its outbound twin on symmetric maps."""

    def names(self, *names):
        return [dict(name=n) for n in names]

    def test_a_return_leg_is_dropped_when_its_twin_runs(self):
        from movement_bench_lib import skip_twins
        kept, skipped = skip_twins(self.names('blue_out_L1_stand', 'red_ret_L1_stand',
                                              'red_out_L2_respawn4', 'blue_ret_L2_respawn4',
                                              'blue_ret_L3_stand'), 'mp/ctf4')
        self.assertEqual([c['name'] for c in kept], ['blue_out_L1_stand', 'red_out_L2_respawn4',
                                                     'blue_ret_L3_stand'])
        self.assertEqual(skipped, ['red_ret_L1_stand', 'blue_ret_L2_respawn4'])

    def test_a_map_keeps_the_twins_it_was_measured_to_split(self):
        from movement_bench_lib import skip_twins
        kept, skipped = skip_twins(self.names('red_out_L1_stand', 'blue_ret_L1_stand',
                                              'red_out_L1_respawn4', 'blue_ret_L1_respawn4'), 'mp/ctf_kejim')
        self.assertEqual(skipped, ['blue_ret_L1_respawn4'])
        self.assertIn('blue_ret_L1_stand', [c['name'] for c in kept])

    def test_an_unmeasured_map_runs_every_case(self):
        from movement_bench_lib import skip_twins
        cases = self.names('blue_out_L1_stand', 'red_ret_L1_stand')
        self.assertEqual(skip_twins(cases, 'mp/ctf_new'), (cases, []))

    def test_every_fixture_map_is_measured(self):
        from movement_bench_lib import TWIN_SKIPPABLE
        for path in (Path(__file__).resolve().parent / 'fixtures' / 'movement').glob('*/cases.json'):
            self.assertIn(json.loads(path.read_text())['map'], TWIN_SKIPPABLE, path)


class AfterSummary(unittest.TestCase):
    """--linger: what the play log shows after a passed trip's objective."""
    @staticmethod
    def rows(spec):
        return [dict(level_time=str(t), z=str(z), health=str(h), carrying=str(c)) for t, z, h, c in spec]

    def test_death_after_pickup(self):
        from movement_bench import after_summary
        got = after_summary(self.rows([(900, 100, 100, 0), (1000, 100, 100, 1), (1100, 50, 100, 1),
                                       (1200, -300, 0, 1)]), 0, 1000)
        self.assertTrue(got['died'])
        self.assertEqual(got['max_drop'], 400.0)
        self.assertFalse(got['dropped_flag'])

    def test_safe_carry_and_no_time(self):
        from movement_bench import after_summary
        got = after_summary(self.rows([(1000, 100, 100, 1), (1500, -220, 100, 1)]), 0, 1000)
        self.assertFalse(got['died'])
        self.assertEqual(got['seconds'], 0.5)
        self.assertIsNone(after_summary([], 0, 1000))
        self.assertIsNone(after_summary(self.rows([(1000, 0, 100, 1)]), None, 1000))

    def test_capture_is_not_a_drop(self):
        from movement_bench import after_summary
        rows = self.rows([(1000, 0, 100, 1), (2000, 0, 100, 0)])
        rows[0]['captures'], rows[1]['captures'] = '0', '1'
        got = after_summary(rows, 0, 1000)
        self.assertTrue(got['captured'])
        self.assertFalse(got['dropped_flag'])



if __name__ == '__main__':
    unittest.main()
