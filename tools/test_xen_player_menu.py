# The tracked player screen menu is what its generator writes (tools/ui/make_xen_player_menu.py).
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


class XenPlayerMenuTest(unittest.TestCase):
    def test_tracked_menu_matches_generator(self):
        script = os.path.join(HERE, 'ui', 'make_xen_player_menu.py')
        r = subprocess.run([sys.executable, '-I', script, '--check'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == '__main__':
    unittest.main()
