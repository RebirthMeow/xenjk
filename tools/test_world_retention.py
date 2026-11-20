import os, tempfile, unittest
from pathlib import Path

from tools import world_retention


def touch(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


class WorldRetentionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.campaign = Path(self.tmp.name) / '.codex-research' / 'camp-a'

    def tearDown(self):
        self.tmp.cleanup()

    def graph(self, world, text):
        path = self.campaign / world / 'base' / 'bot2-graph' / 'mp_ctf_kejim_-1.txt'
        touch(path, text)
        return path

    def test_identical_snapshots_share_one_copy_and_keep_content(self):
        a, b, c = self.graph('w1', 'same'), self.graph('w2', 'same'), self.graph('w3', 'learned')
        self.assertEqual(world_retention.share_graphs(a.parent.parent), (1, 1))
        self.assertEqual(world_retention.share_graphs(b.parent.parent), (1, 0))
        self.assertEqual(world_retention.share_graphs(c.parent.parent), (1, 1))
        self.assertTrue(os.path.samefile(a, b))
        self.assertFalse(os.path.samefile(a, c))
        self.assertEqual((a.read_text(), b.read_text(), c.read_text()), ('same', 'same', 'learned'))
        self.assertEqual(len(list((self.campaign / '.graphs').iterdir())), 2)
        self.assertEqual(sorted(p.name for p in b.parent.iterdir()), ['mp_ctf_kejim_-1.txt'])

    def test_rerun_is_idempotent_and_missing_graph_is_fine(self):
        a = self.graph('w1', 'x')
        world_retention.share_graphs(a.parent.parent)
        self.assertEqual(world_retention.share_graphs(a.parent.parent), (1, 0))
        self.assertEqual(world_retention.share_graphs(self.campaign / 'none' / 'base'), (0, 0))

    def test_copy_session_leaves_out_binaries(self):
        src = Path(self.tmp.name) / 'live'
        for rel in ('openjkded.x86.exe', 'server/base/jampgamex86.dll', 'server/base/games.log',
                    'server/base/bot2-play/p.csv', '.staged-binaries/h/0.dll'):
            touch(src / rel, 'x')
        dst = self.campaign / 'w1' / 'live'
        world_retention.copy_session(src, dst)
        self.assertTrue((dst / 'server/base/games.log').exists())
        self.assertTrue((dst / 'server/base/bot2-play/p.csv').exists())
        for rel in ('openjkded.x86.exe', 'server/base/jampgamex86.dll', '.staged-binaries'):
            self.assertFalse((dst / rel).exists(), rel)


class DiskGuardTest(unittest.TestCase):
    def test_require_free_refuses_below_the_floor(self):
        with tempfile.TemporaryDirectory() as d:
            world_retention.require_free(Path(d) / 'not' / 'yet', min_gb=0)   # nearest existing parent is used
            with self.assertRaises(SystemExit) as stop:
                world_retention.require_free(d, min_gb=1e12)
            self.assertIn('refusing to start', str(stop.exception))

    def test_environment_overrides_the_floor(self):
        with tempfile.TemporaryDirectory() as d:
            os.environ['XENJK_MIN_FREE_GB'] = '0'
            try:
                world_retention.require_free(d, min_gb=1e12)
            finally:
                del os.environ['XENJK_MIN_FREE_GB']

    def test_drop_staging_needs_the_copy_and_removes_read_only_binaries(self):
        import stat
        with tempfile.TemporaryDirectory() as d:
            session, copy = Path(d) / 'session', Path(d) / 'out' / 'live'
            binary = session / '.staged-binaries' / 'openjkded.x86.exe-x' / '0.exe'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'MZ')
            os.chmod(binary, stat.S_IREAD)
            (session / 'server-console.log').write_text('log')
            self.assertFalse(world_retention.drop_staging(session, copy))   # no retained copy: kept
            self.assertTrue(binary.exists())
            copy.mkdir(parents=True)
            (copy / 'server-console.log').write_text('log')
            self.assertTrue(world_retention.drop_staging(session, copy))
            self.assertFalse(session.exists())


if __name__ == '__main__':
    unittest.main()
