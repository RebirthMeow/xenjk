"""codemp/ui/ui_roster.c's character.cfg parsing, compiled with gcc through tools/characters/roster_driver.c: the keys
after a `source` line still apply (the optional same-line source name once swallowed the next line's `order`, which
sorted the picker alphabetically), and `source <tag> "<name>"` on one line still sets the name."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [ROOT / 'tools/characters/roster_driver.c', ROOT / 'codemp/ui/ui_roster.c', ROOT / 'codemp/qcommon/q_shared.c',
           ROOT / 'shared/qcommon/q_string.c', ROOT / 'shared/qcommon/q_math.c', ROOT / 'shared/qcommon/q_color.c']


@unittest.skipUnless(shutil.which('gcc'), 'needs gcc')
class RosterCfg(unittest.TestCase):
    def run_driver(self, cfgs):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            exe = d / 'rd.exe'
            subprocess.run(['gcc', '-m32', '-O1', '-w', '-I%s' % (ROOT / 'codemp'), '-I%s' % (ROOT / 'shared'), '-o',
                            str(exe)] + [str(s) for s in SOURCES], check=True)
            (d / 'cfg').mkdir()
            for folder, text in cfgs.items():
                (d / 'cfg' / (folder + '.cfg')).write_bytes(text.replace('\n', '\r\n').encode())
            (d / 'entries.txt').write_text(''.join('%s/default\n' % f for f in cfgs))
            out = subprocess.run([str(exe), str(d / 'entries.txt'), str(d / 'cfg')], capture_output=True, text=True,
                                 check=True).stdout
        return [l.split() for l in out.splitlines() if l.startswith('C ')], out

    def test_order_after_source(self):
        chars, _ = self.run_driver({
            'tifa': 'character\ttifa\nname\t"Tifa"\nsource\tff7r\norder\t10\n',
            'aerith': 'character\taerith\nname\t"Aerith"\nsource\tff7r\norder\t20\n'})
        self.assertEqual([(c[2], c[4]) for c in chars], [('tifa', '10'), ('aerith', '20')])

    def test_source_name_on_line(self):
        chars, out = self.run_driver({'x': 'character\tx\nsource\tmygame "My Game"\norder\t5\n'})
        self.assertEqual(chars[0][4], '5')
        self.assertIn('"My Game"', out)


if __name__ == '__main__':
    unittest.main()
