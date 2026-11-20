"""The uptime classifier on hand-built samples."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_uptime as uptime  # noqa: E402


def samples(spec):
    """'a' airborne, 'g' grounded moving, 's' grounded still."""
    return [dict(speed=0.0 if c == 's' else 300.0, ground=c != 'a') for c in spec]


class ClassifyTest(unittest.TestCase):
    def test_a_short_landing_between_flights_is_strafing(self):
        self.assertEqual(uptime.classify(samples('aagaa')), ['strafe'] * 5)
        self.assertEqual(uptime.classify(samples('aaggaa')), ['strafe'] * 6)

    def test_a_long_ground_run_is_walking_or_standing(self):
        self.assertEqual(uptime.classify(samples('agggsa')),
                         ['strafe', 'walk', 'walk', 'walk', 'stand', 'strafe'])

    def test_ground_at_either_end_is_never_a_tap(self):
        self.assertEqual(uptime.classify(samples('gaag')), ['walk', 'strafe', 'strafe', 'walk'])
        self.assertEqual(uptime.classify(samples('saa')), ['stand', 'strafe', 'strafe'])

    def test_tally_reports_shares(self):
        spec = samples('aagggs')
        result = uptime.tally(spec, uptime.classify(spec))
        self.assertEqual(result['ms'], 150)
        self.assertEqual((result['strafe_ms'], result['walk_ms'], result['stand_ms']), (50, 75, 25))


if __name__ == '__main__':
    unittest.main()
