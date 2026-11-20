"""Hand-built campaigns only: the tree reader must be read before it is read from.

A campaign's progress file and its worlds are written here by hand, so what the
reader does with them - which branch is a measurement, where a score comes
from, how a rank is formed, what a tie means - is exercised without a game.
"""
import json
from pathlib import Path
import sys
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_decision_tree as tree  # noqa: E402

ARTIFACTS = Path(__file__).resolve().parent.parent / '.codex-research/unit-artifacts'
MAP_LOAD = ('2026-09-22 10:00:00 [GRAPH] map_load map=mp/ctf_kejim checksum=-1301924053 '
            'cached=1 cells=14249 edges=94711\n')


def commit(level, nth=1, forced=0, score=500.0, kind='straight', hops=2, r0=24, r1=0,
           c0=4948, c1=-1, speed=219.2, d0='1.25', d1='0', client=0):
    return ('2026-09-22 10:00:01 [MOVE] client=%d level=%d event=plan edge=hop0 '
            'reason=sequence speed=%.1f force=0 speed_ms=0 x=1.0 y=2.0 z=3.0 '
            'kind=%s id=2 hops=%d landing=0 reissued_up=0 arrives=0 candidates=4 '
            'admitted=3 frames=99 walk_frame=98 walk_stopped=0 walk_frames=99 '
            'd0=%s r0=%d d1=%s r1=%d c0=%d c1=%d chord=820.3 far=3 progress=853.7 '
            'walk=595.4 launch_speed=250.1 land_speed=364.7 launch1_speed=0.0 '
            'land1_speed=0.0 rollout_ms=35.4 traces=25564 index=-1 index2=-1 '
            'state_candidates=5 state_admitted=5 envelope=1 rerouted=0 route_cells=0 '
            'route_ms=34725 exit_ms=500 exit_credit=257.3 score=%.1f margin=258.3 '
            'stale_slots=0 commit_nth=%d select_forced=%d\n'
            % (client, level, speed, kind, hops, d0, r0, d1, r1, c0, c1, score, nth, forced))


def spike(level, cand, outcome='admitted', kind='straight', hops=2, r0=24, r1=0,
          cell=4948, cell1=-1, d0='1.25', client=0):
    return ('2026-09-22 10:00:01 [SPIKE] client=%d level=%d seq=469 kind=%s plan=2 cand=%d '
            'hops=%d d0=%s r0=%d d1=0 r1=%d outcome=%s frames=99 unsupported=0 '
            'launch0=1 l0_speed=250.1 l0_err=0.1 land0=40 a0_speed=364.7 a0_drop=1.0 '
            'a0_err=0.2 a0_cell=%d a0_flags=0 launch1=-1 l1_speed=0.0 l1_err=0.0 '
            'land1=-1 a1_speed=0.0 a1_drop=0.0 a1_err=0.0 a1_cell=%d a1_flags=0 '
            'done=99 progress=853.7 start_speed=219.2 start_err=0.3\n'
            % (client, level, kind, cand, hops, d0, r0, r1, outcome, cell, cell1))


def temp():
    return ARTIFACTS / 'decision-tree' / uuid.uuid4().hex


def world(directory, lines, time_ms=20000, corner_level=3075, case='red_out_L1_stand'):
    directory = Path(directory)
    (directory / case).mkdir(parents=True, exist_ok=True)
    (directory / case / 'server.log').write_text(''.join(lines), encoding='utf-8')
    record = dict(name=case, corner_level=corner_level, time_ms=time_ms, status='pass',
                  reason='complete', deaths=0, owners=['move'], parity=dict(mismatches=0),
                  straight=dict(commits=13), state=dict(commits=5))
    (directory / 'result.json').write_text(json.dumps(dict(cases=[record])), encoding='utf-8')
    return directory


def campaign(rows):
    """A progress file and the worlds it names, laid out as a campaign is.

    Each row is (name, parent, cand, time_ms, forced, score, cells, decision_level).
    The root carries the decision and its candidate set; each branch carries its
    own forced commit at the same ordinal.
    """
    root = temp()
    root.mkdir(parents=True, exist_ok=True)
    lines = []
    for name, parent, cand, time_ms, forced, score, cells, level in rows:
        out = root / name
        if parent is None:
            log = ([MAP_LOAD, commit(level, nth=1, score=score, c0=cells[0], c1=cells[1])] +
                   [spike(level, other[2], cell=other[6][0], cell1=other[6][1],
                          d0='9.%04d' % other[2])
                    for other in rows if other[1] == name])
            branch = None
        else:
            log = [MAP_LOAD, commit(level, nth=1, forced=forced, score=score,
                                    c0=cells[0], c1=cells[1])]
            branch = dict(nth=1, cand=cand)
        world(out, log, time_ms=time_ms)
        lines.append(dict(world=name, parent=parent, branch=branch, out=str(out),
                          status='pass', time_ms=time_ms, corner_level=3075,
                          forced=forced if parent else None,
                          decisions=[dict(index=0, level=level, client=0, kind='straight',
                                          hops=2, aim='1.25', runup='24', aim1='0',
                                          runup1='0', cell=cells[0], cell1=cells[1],
                                          edge=-1, edge2=-1, commit_nth=1)]))
    progress = root / 'progress.jsonl'
    progress.write_text(''.join(json.dumps(x) + '\n' for x in lines), encoding='utf-8')
    return progress


SIMPLE = [
    # name, parent, cand, time_ms, forced, score, (c0, c1), level
    ('control', None, None, 21000, 0, 400.0, (4948, -1), 5325),
    ('control-d1c0', 'control', 0, 19000, 1, 300.0, (4948, -1), 5325),
    ('control-d1c1', 'control', 1, 20000, 1, 500.0, (4949, -1), 5325),
    ('control-d1c2', 'control', 2, 21000, 1, 400.0, (4950, -1), 5325),
]


class RankTest(unittest.TestCase):
    def test_ranks_start_at_one_and_average_over_ties(self):
        self.assertEqual(tree.ranks([10, 20, 30]), [1.0, 2.0, 3.0])
        self.assertEqual(tree.ranks([10, 10, 30]), [1.5, 1.5, 3.0])
        self.assertEqual(tree.ranks([10, 20, 30], reverse=True), [3.0, 2.0, 1.0])

    def test_spearman_is_one_on_a_matching_order(self):
        self.assertAlmostEqual(tree.spearman([1, 2, 3], [10, 20, 30]), 1.0)

    def test_spearman_is_minus_one_on_a_reversed_order(self):
        self.assertAlmostEqual(tree.spearman([1, 2, 3], [30, 20, 10]), -1.0)

    def test_spearman_is_none_when_it_is_undefined(self):
        self.assertIsNone(tree.spearman([1], [2]))
        self.assertIsNone(tree.spearman([1, 1, 1], [10, 20, 30]))

    def test_spearman_refuses_unequal_lengths(self):
        with self.assertRaises(ValueError):
            tree.spearman([1, 2], [1, 2, 3])


class ProgressTest(unittest.TestCase):
    def test_the_last_record_of_a_world_wins(self):
        path = campaign(SIMPLE)
        with path.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(dict(world='control', parent=None, time_ms=1)) + '\n')
        self.assertEqual(tree.read_progress(path)['control']['time_ms'], 1)

    def test_a_lineage_runs_from_the_root_down(self):
        records = tree.read_progress(campaign(SIMPLE))
        self.assertEqual(tree.lineage(records, 'control-d1c1'),
                         ['control', 'control-d1c1'])

    def test_a_lineage_refuses_a_world_it_has_no_record_of(self):
        records = tree.read_progress(campaign(SIMPLE))
        with self.assertRaises(KeyError):
            tree.lineage(records, 'control-d9c9')

    def test_branches_are_taken_by_parent_and_depth(self):
        records = tree.read_progress(campaign(SIMPLE))
        self.assertEqual(sorted(tree.branches(records, 'control', 1)), [0, 1, 2])
        self.assertEqual(tree.branches(records, 'control', 2), {})


class DecisionTableTest(unittest.TestCase):
    def test_every_branch_is_a_row_with_its_own_score_and_trip(self):
        table = tree.decision_table(tree.read_progress(campaign(SIMPLE)), 'control', 1)
        self.assertEqual(table['admitted'], 3)
        self.assertEqual(table['measured'], 3)
        self.assertEqual([r['cand'] for r in table['rows']], [0, 1, 2])
        self.assertEqual([r['score'] for r in table['rows']], [300.0, 500.0, 400.0])
        self.assertEqual([r['time_ms'] for r in table['rows']], [19000, 20000, 21000])

    def test_the_judge_rank_is_the_score_order_and_one_is_first(self):
        table = tree.decision_table(tree.read_progress(campaign(SIMPLE)), 'control', 1)
        self.assertEqual([r['judge_rank'] for r in table['rows']], [3.0, 1.0, 2.0])
        self.assertEqual([r['time_rank'] for r in table['rows']], [1.0, 2.0, 3.0])

    def test_a_branch_whose_index_was_not_admitted_is_dropped_not_estimated(self):
        rows = list(SIMPLE)
        rows[2] = ('control-d1c1', 'control', 1, 20000, -1, 500.0, (4949, -1), 5325)
        table = tree.decision_table(tree.read_progress(campaign(rows)), 'control', 1)
        self.assertEqual(table['measured'], 2)
        self.assertEqual([r['cand'] for r in table['rows']], [0, 2])
        self.assertEqual(table['dropped'][0]['cand'], 1)
        self.assertEqual(table['dropped'][0]['forced'], -1)

    def test_the_score_is_read_from_the_branchs_own_commit_and_not_the_parents(self):
        table = tree.decision_table(tree.read_progress(campaign(SIMPLE)), 'control', 1)
        self.assertNotIn(400.0, [r['score'] for r in table['rows']][:1])
        self.assertEqual(table['rows'][0]['judged']['score'], '300.0')

    def test_the_root_speed_spread_is_reported_so_branches_can_be_compared(self):
        table = tree.decision_table(tree.read_progress(campaign(SIMPLE)), 'control', 1)
        self.assertEqual(table['root_speed_spread'], 0.0)
        self.assertEqual(table['parent_root_speed'], 219.2)

    def test_a_decision_the_parent_never_took_is_refused(self):
        records = tree.read_progress(campaign(SIMPLE))
        with self.assertRaises(ValueError):
            tree.decision_table(records, 'control', 2)


class TieTest(unittest.TestCase):
    def test_a_repeated_time_from_one_identity_is_a_duplicate(self):
        rows = list(SIMPLE)
        rows[2] = ('control-d1c1', 'control', 1, 19000, 1, 500.0, (4948, -1), 5325)
        table = tree.decision_table(tree.read_progress(campaign(rows)), 'control', 1)
        repeated = table['ties']['repeated_times']
        self.assertEqual(repeated[0]['time_ms'], 19000)
        self.assertTrue(repeated[0]['duplicates_of_one_decision'])

    def test_a_repeated_time_from_two_identities_is_not_a_duplicate(self):
        rows = list(SIMPLE)
        rows[2] = ('control-d1c1', 'control', 1, 19000, 1, 500.0, (4949, -1), 5325)
        table = tree.decision_table(tree.read_progress(campaign(rows)), 'control', 1)
        repeated = table['ties']['repeated_times']
        self.assertEqual(repeated[0]['distinct_decisions'], 2)
        self.assertFalse(repeated[0]['duplicates_of_one_decision'])

    def test_one_identity_with_two_times_is_reported(self):
        rows = list(SIMPLE)
        rows[2] = ('control-d1c1', 'control', 1, 20000, 1, 500.0, (4948, -1), 5325)
        table = tree.decision_table(tree.read_progress(campaign(rows)), 'control', 1)
        split = table['ties']['one_identity_many_times']
        self.assertEqual(split[0]['candidates'], [0, 1])
        self.assertEqual(split[0]['times'], [19000, 20000])

    def test_distinct_times_counts_the_outcomes_the_branches_produced(self):
        table = tree.decision_table(tree.read_progress(campaign(SIMPLE)), 'control', 1)
        self.assertEqual(table['distinct_times'], 3)
        self.assertEqual(table['time_histogram'], {19000: 1, 20000: 1, 21000: 1})


class ReportTest(unittest.TestCase):
    def test_the_report_names_the_winner_and_the_rank_it_held(self):
        result = tree.report(campaign(SIMPLE), 'control-d1c2', [1])
        table = result['decisions'][0]
        self.assertEqual(result['lineage'], ['control', 'control-d1c2'])
        self.assertEqual(table['winning_child'], 'control-d1c2')
        self.assertEqual(table['winner']['cand'], 2)
        self.assertEqual(table['winner']['judge_rank'], 2.0)
        self.assertEqual(table['winner']['time_ms'], 21000)

    def test_a_depth_the_lineage_does_not_reach_is_not_tabled(self):
        result = tree.report(campaign(SIMPLE), 'control-d1c0', [1, 2, 3])
        self.assertEqual(len(result['decisions']), 1)

    def test_a_route_root_link_advances_no_decision(self):
        # control -> control-rq40 (a route root: a parent, no branch) -> a
        # movement branch at decision 1 of the route root.
        rows = [(name.replace('control', 'control-rq40'), parent and 'control-rq40',
                 cand, time_ms, forced, score, cells, level)
                for name, parent, cand, time_ms, forced, score, cells, level in SIMPLE]
        progress = campaign(rows)
        records = tree.read_progress(progress)
        route_root = dict(records['control-rq40'], parent='control', branch=None,
                          route_branch=dict(kind='rq', edge=40))
        control = dict(records['control-rq40'], world='control', parent=None)
        with progress.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(control) + '\n' + json.dumps(route_root) + '\n')
        result = tree.report(progress, 'control-rq40-d1c1', [1, 2, 3])
        self.assertEqual(result['lineage'], ['control', 'control-rq40', 'control-rq40-d1c1'])
        self.assertEqual(len(result['decisions']), 1)
        table = result['decisions'][0]
        self.assertEqual(table['parent'], 'control-rq40')
        self.assertEqual(table['measured'], 3)
        self.assertEqual(table['winning_child'], 'control-rq40-d1c1')
        self.assertEqual(table['winner']['cand'], 1)

    def test_the_spearman_of_the_hand_built_campaign_is_its_own_number(self):
        result = tree.report(campaign(SIMPLE), 'control-d1c1', [1])
        # judge ranks 3, 1, 2 against times 19000, 20000, 21000.
        self.assertAlmostEqual(result['decisions'][0]['spearman'], -0.5)


if __name__ == '__main__':
    unittest.main()
