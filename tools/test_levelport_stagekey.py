"""Stage keys and the files a module declares it runs without importing them (STAGEKEY_FILES, RL-13)."""
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'levelport'))
import stagekey  # noqa: E402


class DeclaredFiles(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        (self.d / 'tool.py').write_text(textwrap.dedent('''
            import helper
            STAGEKEY_FILES = {'stage_a': ['a.ps1']}

            def stage_a():
                return 1

            def stage_b():
                return helper.run()
            '''), encoding='utf-8')
        (self.d / 'helper.py').write_text(textwrap.dedent('''
            STAGEKEY_FILES = ['helper.c']

            def run():
                return 2
            '''), encoding='utf-8')
        (self.d / 'a.ps1').write_text('one\n', encoding='utf-8')
        (self.d / 'helper.c').write_text('int x;\n', encoding='utf-8')

    def tearDown(self):
        self.tmp.cleanup()

    def key(self, root):
        stagekey._CACHE.clear()
        return stagekey.code_key(self.d / 'tool.py', [root], entry=None, base=self.d)

    def test_a_declared_file_moves_only_the_definition_that_runs_it(self):
        a, b = self.key('stage_a'), self.key('stage_b')
        (self.d / 'a.ps1').write_text('two\n', encoding='utf-8')
        self.assertNotEqual(a, self.key('stage_a'))
        self.assertEqual(b, self.key('stage_b'))

    def test_a_list_moves_every_key_that_reaches_the_module(self):
        a, b = self.key('stage_a'), self.key('stage_b')
        (self.d / 'helper.c').write_text('int y;\n', encoding='utf-8')
        self.assertEqual(a, self.key('stage_a'))
        self.assertNotEqual(b, self.key('stage_b'))

    def test_line_ends_do_not_move_a_key(self):
        a = self.key('stage_a')
        (self.d / 'a.ps1').write_bytes(b'one\r\n')
        self.assertEqual(a, self.key('stage_a'))

    def test_the_report_names_the_declared_files(self):
        stagekey._CACHE.clear()
        rep, _ = stagekey.report(self.d / 'tool.py', ['stage_b'], entry=None, base=self.d)
        self.assertEqual(rep['(declared files)'], ['helper.c'])


if __name__ == '__main__':
    unittest.main()
