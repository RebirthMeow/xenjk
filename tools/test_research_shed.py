import contextlib, io, tempfile, unittest
from pathlib import Path

from tools import research_shed


def touch(path, text='x'):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')


class ResearchShedTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.research, self.trash = root / 'research', root / 'trash'
        w = self.research / 'camp-20200101-a' / 'w1'
        for rel in ('base/bot2-graph/m.txt', 'base/demos/d.dm_26', 'base/jampgamex86.dll',
                    'openjkded.x86.exe', 'console.log', 'base/games.log', 'result.json',
                    'sample-01.jsonl', 'compact-01.jsonl', 'sample-02.jsonl'):
            touch(w / rel)
        touch(self.research / 'camp-20200101-a' / '.staged-binaries' / 'h' / '0.dll')
        b = self.research / 'thing-build-20200101-a'
        touch(b / 'codemp' / 'x.obj')
        touch(b / 'ALL_BUILD.vcxproj')
        touch(b / '20200101T0.log')
        touch(b / '20200101T0.sources.json')
        touch(self.research / 'seeds-20200101-a' / 'w' / 'base' / 'bot2-graph' / 'm.txt')
        touch(self.research / 'KEEP.txt', 'seeds-20200101-a  # live seed\n')

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *extra):
        with contextlib.redirect_stdout(io.StringIO()):
            research_shed.main(['--research', str(self.research), '--trash', str(self.trash),
                                '--keep-days', '0', *extra])

    def test_dry_run_moves_nothing(self):
        self.run_tool()
        self.assertTrue((self.research / 'camp-20200101-a/w1/base/bot2-graph/m.txt').exists())
        self.assertEqual([p.name for p in self.trash.iterdir()][0][:7], 'dryrun-')

    def test_apply_moves_bulk_keeps_evidence_and_restores(self):
        self.run_tool('--apply')
        w = self.research / 'camp-20200101-a' / 'w1'
        for gone in ('base/bot2-graph', 'base/demos', 'base/jampgamex86.dll', 'openjkded.x86.exe', 'sample-01.jsonl'):
            self.assertFalse((w / gone).exists(), gone)
        for kept in ('console.log', 'base/games.log', 'result.json', 'compact-01.jsonl', 'sample-02.jsonl'):
            self.assertTrue((w / kept).exists(), kept)
        b = self.research / 'thing-build-20200101-a'
        self.assertEqual(sorted(p.name for p in b.iterdir()), ['20200101T0.log', '20200101T0.sources.json'])
        self.assertTrue((self.research / 'seeds-20200101-a/w/base/bot2-graph/m.txt').exists())
        stamp = next(self.trash.iterdir())
        self.assertTrue((stamp / 'camp-20200101-a/w1/base/bot2-graph/m.txt').exists())

        self.run_tool('--restore', stamp.name)
        self.assertTrue((w / 'base/bot2-graph/m.txt').exists())
        self.assertTrue((b / 'codemp/x.obj').exists())
        self.assertTrue((self.research / 'camp-20200101-a/.staged-binaries/h/0.dll').exists())


if __name__ == '__main__':
    unittest.main()
