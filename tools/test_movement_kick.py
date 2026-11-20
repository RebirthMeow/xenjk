"""Hand-built logs and campaigns for the knockback fixture's readers."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movement_bench_lib import kick_audit  # noqa: E402
from movement_kick_report import compare, death_class  # noqa: E402


def armed(level, index, trigger='hop'):
    return ('[KICK] ARMED level=%d client=0 index=%d trigger=%s knockback=100.0 angle=90.0 pitch=20.0 '
            'delay_ms=0 into_ms=0\n' % (level, index, trigger))


def applied(level, rel, index, trigger='hop'):
    return ('[KICK] APPLIED level=%d rel_ms=%d client=0 index=%d trigger=%s knockback=100.0 angle=90.0 '
            'pitch=20.0 x=1.0 y=-2.0 z=3.5 ground=0 plan=4 kind=3 stage=1 vx=400.0 vy=0.0 vz=120.0 '
            'speed=400.0 after_vx=400.0 after_vy=376.0 after_vz=376.8 after_speed=548.9 pm_time=200\n'
            % (level, rel, index, trigger))


class Audit(unittest.TestCase):
    def test_applied_kicks_are_read_in_order(self):
        text = armed(1000, 0) + armed(1000, 1, 'walk') + applied(2500, 1500, 0) + applied(4000, 3000, 1, 'walk')
        audit = kick_audit(text, 1000)
        self.assertEqual(audit['armed'], 2)
        self.assertEqual([k['index'] for k in audit['applied']], [0, 1])
        self.assertEqual(audit['applied'][0]['after_speed'], 548.9)
        self.assertEqual(audit['applied'][0]['origin'], [1.0, -2.0, 3.5])
        self.assertEqual(audit['unfired'], [])

    def test_an_unfired_kick_is_listed(self):
        audit = kick_audit(armed(1000, 0) + armed(1000, 1), 1000)
        self.assertEqual(audit['applied'], [])
        self.assertEqual(audit['unfired'], [0, 1])

    def test_a_retired_attempt_does_not_count(self):
        """A corner start retried prints a second ARMED; only the release's own counts."""
        text = armed(400, 0) + applied(600, 200, 0) + armed(1000, 0)
        audit = kick_audit(text, 1000)
        self.assertEqual((audit['armed'], audit['applied'], audit['unfired']), (1, [], [0]))

    def test_a_kick_after_the_endpoint_is_not_applied(self):
        """kejim-walk/red_ret_L1_respawn5: passed at 12,700 ms, its only kick at 12,750."""
        text = armed(1000, 0, 'walk') + applied(13750, 12750, 0, 'walk')
        audit = kick_audit(text, 1000, 1000 + 12700)
        self.assertEqual((audit['applied'], audit['unfired']), ([], [0]))
        self.assertEqual([k['rel_ms'] for k in audit['late']], [12750])
        self.assertEqual(kick_audit(text, 1000, 1000 + 12775)['unfired'], [])

    def test_a_kick_on_the_verdict_frame_is_late(self):
        """The verdict's sample reports the previous Pmove, and the kick is applied
        before it: a pass and a kick both at 12,700 ms completed before the kick."""
        text = armed(1000, 0, 'walk') + applied(13700, 12700, 0, 'walk')
        audit = kick_audit(text, 1000, 1000 + 12700)
        self.assertEqual((audit['applied'], audit['unfired']), ([], [0]))
        self.assertEqual([k['rel_ms'] for k in audit['late']], [12700])
        rows, totals = compare({'a': case('a', 'pass', 12000)},
                               {'a': case('a', 'pass', 12700, kicks=audit)})
        self.assertEqual((totals['fired'], totals['passed']), (0, 0))

    def test_no_fixture_is_empty(self):
        self.assertEqual(kick_audit('', 1000), dict(armed=0, applied=[], unfired=[]))


def case(name, status, time_ms, reason='complete', kicks=None):
    return dict(name=name, status=status, reason=reason, time_ms=time_ms, deaths=int(reason == 'death'), kicks=kicks)


FIRED = dict(armed=1, unfired=[], applied=[dict(index=0, trigger='hop', rel_ms=1500, speed=400.0,
                                                after_speed=548.9)])
UNFIRED = dict(armed=1, unfired=[0], applied=[])


class Report(unittest.TestCase):
    def test_recovery_is_paired_against_the_control(self):
        control = {'a': case('a', 'pass', 20000), 'b': case('b', 'pass', 18000), 'c': case('c', 'pass', 19000)}
        kicked = {'a': case('a', 'pass', 21500, kicks=FIRED),
                  'b': case('b', 'fail', 2100, 'death', kicks=FIRED),
                  'c': case('c', 'pass', 19000, kicks=UNFIRED)}
        rows, totals = compare(control, kicked)
        by = {r['name']: r for r in rows}
        self.assertEqual(by['a']['delta_ms'], 1500)
        self.assertIsNone(by['b']['delta_ms'])
        self.assertEqual((totals['fired'], totals['unfired'], totals['passed'], totals['deaths']), (2, 1, 1, 1))
        self.assertEqual(totals['delta_median_ms'], 1500)

    def test_an_unfired_kick_is_never_a_recovery(self):
        rows, totals = compare({'c': case('c', 'pass', 19000)}, {'c': case('c', 'pass', 19000, kicks=UNFIRED)})
        self.assertEqual((totals['fired'], totals['passed'], totals['paired']), (0, 0, 0))


def momentum(level, ground):
    return ('[MOMENTUM] level=%d client=0 speed=300.0 vz=0.0 ground=%d fwd=127 right=0 up=0 owner=move\n'
            % (level, ground))


def position(level, z, reason='land'):
    return ('[MOVE] client=0 level=%d event=edge edge=hop0 reason=%s speed=300.0 force=0 speed_ms=0 '
            'x=1.0 y=2.0 z=%.1f\n' % (level, reason, z))


class DeathClass(unittest.TestCase):
    def test_a_push_straight_into_the_pit_is_direct(self):
        text = (applied(2000, 1000, 0) + ''.join(momentum(lv, 0) for lv in range(2025, 4000, 25)) +
                momentum(4000, 1) + momentum(4025, 1) + position(4000, -2023.9) + position(4025, -2023.9, 'hurt'))
        self.assertEqual(death_class(text, 1000, 3025), 'direct')

    def test_standing_first_and_falling_after_is_the_recovery(self):
        text = (applied(2000, 1000, 0) + momentum(2600, 1) + momentum(2625, 1) + position(2600, 1368.1) +
                ''.join(momentum(lv, 0) for lv in range(2650, 4000, 25)) + position(4000, 32.1, 'hurt'))
        self.assertEqual(death_class(text, 1000, 3000), 'landed_then_fell')

    def test_the_respawn_after_the_death_is_not_read(self):
        text = (applied(2000, 1000, 0) + momentum(4000, 1) + momentum(4025, 1) + position(4000, -2023.9) +
                position(9000, 500.0, 'respawn'))
        self.assertEqual(death_class(text, 1000, 3000), 'direct')

    def test_a_death_with_no_landing_is_not_classified_from_the_respawn(self):
        text = (applied(2000, 1000, 0) + ''.join(momentum(lv, 0) for lv in range(2025, 4025, 25)) +
                position(4000, -2023.9, 'hurt') + momentum(9000, 1) + momentum(9025, 1) +
                position(9000, 500.0, 'respawn'))
        self.assertIsNone(death_class(text, 1000, 3000))

    def test_no_kick_says_nothing(self):
        self.assertIsNone(death_class(position(4000, 0.0), 1000, 3000))


if __name__ == '__main__':
    unittest.main()
