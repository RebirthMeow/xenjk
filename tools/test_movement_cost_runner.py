"""The row-5 cost runner's reporting: percentiles, the budget and what counts.

Nothing here runs a server. The native part is evidence a campaign records;
what is testable is the arithmetic that turns a capture into the numbers a
record quotes, and the rules about which frames are in it.
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import movement_cost_runner as runner


def row(level_time, movement_us, dispatches=4, clients=4, dispatch_us=None,
        prepare_us=0.0, alive=4, moving=4, bad_clocks=0, elapsed_ms=None):
    return dict(level_time=str(level_time),
                elapsed_ms=str(level_time if elapsed_ms is None else elapsed_ms),
                clients=str(clients), dispatches=str(dispatches), alive=str(alive),
                moving=str(moving),
                movement_us='%.3f' % movement_us, bad_clocks=str(bad_clocks),
                dispatch_us='%.3f' % (movement_us if dispatch_us is None else dispatch_us),
                prepare_us='%.3f' % prepare_us)


class Percentile(unittest.TestCase):
    def test_nearest_rank_picks_a_frame_that_happened(self):
        values = [float(v) for v in range(1, 101)]
        self.assertEqual(runner.percentile(values, .50), 50.0)
        self.assertEqual(runner.percentile(values, .95), 95.0)
        self.assertEqual(runner.percentile(values, .99), 99.0)

    def test_a_single_frame_is_every_percentile(self):
        self.assertEqual(runner.percentile([7.0], .99), 7.0)

    def test_no_frames_has_no_percentile(self):
        self.assertIsNone(runner.percentile([], .5))


class Summarize(unittest.TestCase):
    def test_reports_the_distribution_and_the_worst_frame(self):
        rows = [row(1000 + 25 * i, 100.0 * (i + 1)) for i in range(100)]
        result = runner.summarize(rows)
        self.assertEqual(result['frames'], 100)
        self.assertEqual(result['active_frames'], 100)
        self.assertEqual(result['p50_us'], 5000.0)
        self.assertEqual(result['p95_us'], 9500.0)
        self.assertEqual(result['p99_us'], 9900.0)
        self.assertEqual(result['max_us'], 10000.0)
        self.assertEqual(result['worst_frame']['level_time'], 1000 + 25 * 99)

    def test_the_declared_budget_is_counted_not_fitted(self):
        rows = [row(1000, 4999.0), row(1025, 5000.0), row(1050, 5001.0)]
        result = runner.summarize(rows)
        # Strictly over: a frame exactly at the budget met it.
        self.assertEqual(result['budget_us'], runner.BUDGET_US)
        self.assertEqual(result['over_budget_frames'], 1)
        self.assertEqual(result['over_frame_frames'], 0)

    def test_counts_frames_over_the_server_frame_separately(self):
        result = runner.summarize([row(1000, 26000.0), row(1025, 100.0)])
        self.assertEqual(result['over_frame_frames'], 1)
        self.assertEqual(result['over_budget_frames'], 1)
        self.assertEqual(result['max_share_of_frame'], round(26000.0 / 25000, 6))

    def test_a_frame_with_no_dispatch_measures_nothing(self):
        rows = [row(1000, 0.0, dispatches=0), row(1025, 400.0)]
        result = runner.summarize(rows)
        self.assertEqual(result['frames'], 2)
        self.assertEqual(result['active_frames'], 1)
        self.assertEqual(result['p50_us'], 400.0)

    def test_dispatch_and_preparation_are_reported_apart(self):
        rows = [row(1000, 900.0, dispatch_us=600.0, prepare_us=300.0)]
        result = runner.summarize(rows)
        self.assertEqual(result['dispatch_us_total'], 600.0)
        self.assertEqual(result['prepare_us_total'], 300.0)

    def test_an_empty_capture_reports_nothing_rather_than_zero(self):
        result = runner.summarize([])
        self.assertIsNone(result['p50_us'])
        self.assertIsNone(result['max_us'])
        self.assertIsNone(result['worst_frame'])
        self.assertEqual(result['over_budget_frames'], 0)

    def test_a_custom_budget_only_changes_the_reporting(self):
        rows = [row(1000, 3000.0), row(1025, 7000.0)]
        self.assertEqual(runner.summarize(rows, budget_us=2000)['over_budget_frames'], 2)
        self.assertEqual(runner.summarize(rows, budget_us=8000)['over_budget_frames'], 0)


class Configuration(unittest.TestCase):
    """The production configuration the row asks for, as the settings state it."""

    class Args:
        bots = 4
        parity = 0
        library = None
        telemetry = False
        search_budget = 0
        graph_save_defer = 1
        assets = Path('assets')
        out = Path('out')

    def test_the_parity_harness_is_off_and_the_search_is_on(self):
        settings = runner.settings_for(self.Args(), 30600, 'pw')
        self.assertEqual(settings['bot_moveParity'], 0)
        for name in ('bot_moveEdges', 'bot_movePlanner', 'bot_moveState', 'bot_moveStraight'):
            self.assertEqual(settings[name], 1, name)

    def test_four_bots_on_one_dedicated_localhost_server(self):
        settings = runner.settings_for(self.Args(), 30600, 'pw')
        self.assertEqual(settings['dedicated'], 1)
        self.assertEqual(settings['net_ip'], '127.0.0.1')
        self.assertEqual(settings['sv_maxclients'], 4)
        self.assertEqual(settings['bot_moveCost'], 4)

    def test_the_per_frame_trace_is_off_unless_asked_for(self):
        args = self.Args()
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_moveCostTrace'], 0)
        args.trace = True
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_moveCostTrace'], 1)

    def test_extra_cvars_are_set_and_malformed_ones_refused(self):
        args = self.Args()
        args.set = ['bot_moveCorridorMemo=1', 'bot_forecastScopeReuse=1']
        settings = runner.settings_for(args, 30600, 'pw')
        self.assertEqual(settings['bot_moveCorridorMemo'], '1')
        self.assertEqual(settings['bot_forecastScopeReuse'], '1')
        args.set = ['bot_moveCorridorMemo']
        with self.assertRaises(SystemExit):
            runner.settings_for(args, 30600, 'pw')

    def test_one_bot_is_the_rescoped_gate_row(self):
        args = self.Args()
        args.bots = 1
        settings = runner.settings_for(args, 30600, 'pw')
        self.assertEqual(settings['sv_maxclients'], 1)
        self.assertEqual(settings['bot_moveCost'], 1)

    def test_the_frame_is_twenty_five_milliseconds(self):
        self.assertEqual(runner.settings_for(self.Args(), 30600, 'pw')['sv_fps'], 40)
        self.assertEqual(runner.FRAME_US, 25000)

    def test_a_timing_world_does_not_print_inside_the_search(self):
        args = self.Args()
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_telemetry'], 0)
        args.telemetry = True
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_telemetry'], 1)

    def test_the_library_is_off_when_none_is_staged(self):
        self.assertEqual(runner.settings_for(self.Args(), 30600, 'pw')['bot_moveTapeLibrary'], 0)

    def test_the_search_budget_is_passed_through(self):
        args = self.Args()
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_moveSearchBudget'], 0)
        args.search_budget = 2
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_moveSearchBudget'], 2)

    def test_the_graph_save_is_deferred_unless_a_world_asks_for_immediate(self):
        args = self.Args()
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_moveGraphSaveDefer'], 1)
        args.graph_save_defer = 0
        self.assertEqual(runner.settings_for(args, 30600, 'pw')['bot_moveGraphSaveDefer'], 0)


if __name__ == '__main__':
    unittest.main()
