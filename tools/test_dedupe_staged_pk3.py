"""dedupe_staged_pk3: copies become links, unmatched copies go to the trash in research_shed's manifest format."""
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dedupe_staged_pk3 as dd  # noqa: E402
import research_shed  # noqa: E402


class DedupeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.research, self.trash = self.tmp / '.codex-research', self.tmp / '.codex-trash'
        lp = self.research / 'levelport'
        self.build, self.look = lp / 'build', lp / 'look'
        (self.build / 'b1').mkdir(parents=True)
        (self.look / 'run1' / 'home').mkdir(parents=True)
        self.orig = self.build / 'b1' / 'zz_map.pk3'
        self.orig.write_bytes(b'x' * 5000)
        self.copy = self.look / 'run1' / 'home' / 'zz_map.pk3'
        shutil.copy2(self.orig, self.copy)
        self.lone = self.look / 'run1' / 'home' / 'zz_gone.pk3'
        self.lone.write_bytes(b'y' * 100)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_tool(self, *args):
        with mock.patch.object(dd, 'RESEARCH', self.research), mock.patch.object(dd, 'TRASH', self.trash), \
                mock.patch.object(sys, 'argv', ['dedupe', '--look', str(self.look), '--builds', str(self.build),
                                                *args]):
            dd.main()

    def test_dry_run_changes_nothing(self):
        self.run_tool()
        self.assertEqual(os.stat(self.copy).st_nlink, 1)
        self.assertTrue(self.lone.exists())
        self.assertFalse(self.trash.exists())

    def test_apply_links_moves_and_restores(self):
        self.run_tool('--apply')
        self.assertTrue(os.path.samefile(self.copy, self.orig))
        self.assertFalse(self.lone.exists())
        stamp = next(self.trash.iterdir()).name
        self.assertTrue(stamp.endswith('-staged-pk3'))
        rows = (self.trash / stamp / 'MANIFEST.tsv').read_text(encoding='utf-8').splitlines()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].split('\t')[0], 'staged-pk3')
        research_shed.restore(self.trash, self.research, stamp)
        self.assertEqual(self.lone.read_bytes(), b'y' * 100)

    def test_an_original_under_look_is_left_alone(self):
        with mock.patch.object(dd, 'RESEARCH', self.research), mock.patch.object(dd, 'TRASH', self.trash), \
                mock.patch.object(sys, 'argv', ['dedupe', '--look', str(self.build), '--builds', str(self.build),
                                                '--apply']):
            dd.main()
        self.assertTrue(self.orig.exists())

    def test_a_leftover_link_file_does_not_stop_the_run(self):
        os.link(self.orig, self.copy.with_name(self.copy.name + '.link'))
        self.run_tool('--apply')
        self.assertTrue(os.path.samefile(self.copy, self.orig))
        self.assertFalse(self.copy.with_name(self.copy.name + '.link').exists())


if __name__ == '__main__':
    unittest.main()
