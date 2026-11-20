"""Tests for the guided corridor builder."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import movement_corridor as mc  # noqa: E402


ASSET = {
    'format': 'BOT_ROUTE2',
    'binding': {'map': 'mp/ctf_kejim', 'map_checksum': -1301924053, 'recording': 'r.csv'},
    'reference': {'complete': True, 'elapsed_ms': 14800},
    'start': {'origin': [0.0, 0.0, 10.0]},
    'contract': [{'origin': [100.0, 0.0, 10.0]}, {'origin': [110.0, 0.0, 10.0]},
                 {'origin': [400.0, 0.0, 10.0]}],
}


class Corridor(unittest.TestCase):
    def test_marks_lead_with_the_start(self):
        self.assertEqual(mc.marks(ASSET)[0], [0.0, 0.0, 10.0])
        self.assertEqual(len(mc.marks(ASSET)), 4)

    def test_thinning_keeps_both_ends(self):
        kept = mc.thin(mc.marks(ASSET), 50)
        self.assertEqual(kept[0], [0.0, 0.0, 10.0])
        self.assertEqual(kept[-1], [400.0, 0.0, 10.0])
        self.assertNotIn([110.0, 0.0, 10.0], kept)

    def test_thinning_off_keeps_every_mark(self):
        self.assertEqual(mc.thin(mc.marks(ASSET), 0), mc.marks(ASSET))

    def test_header_binds_map_and_source(self):
        text = mc.render(ASSET, 'r.route.json', 'ab12', 0)
        first = text.splitlines()[0]
        self.assertEqual(first, 'corridor 1 map=mp/ctf_kejim checksum=-1301924053 points=4')
        self.assertIn('source_sha256=ab12', text)

    def test_body_is_positions_only(self):
        body = [line for line in mc.render(ASSET, 'r.route.json', 'ab12', 0).splitlines()
                if line and not line.startswith('#') and not line.startswith('corridor')]
        self.assertEqual(body, ['p 0.000 0.000 10.000', 'p 100.000 0.000 10.000',
                                'p 110.000 0.000 10.000', 'p 400.000 0.000 10.000'])

    def test_point_count_matches_the_header(self):
        text = mc.render(ASSET, 'r.route.json', 'ab12', 50)
        declared = int(text.splitlines()[0].split('points=')[1])
        self.assertEqual(declared, sum(1 for line in text.splitlines() if line.startswith('p ')))


if __name__ == '__main__':
    unittest.main()
