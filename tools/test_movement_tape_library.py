"""The decision-tape library builder: keys, bindings and what it refuses."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import movement_tape_library as lib

TAPE = ('decisiontape 3 map=mp/ctf_kejim checksum=1234567890 entries=1\n'
        'e 0 1.0 2.0 3.0 state 2 -0.105301 24 0.5 0 50732 26663 -1 -1\n')
# The same tape as the engine writes it now: bound to the graph it was
# recorded on as well as to the map and the checksum.
BOUND = ('decisiontape 3 map=mp/ctf_kejim checksum=1234567890 entries=1 graph=g1-0011223344556677\n'
         'e 0 1.0 2.0 3.0 state 2 -0.105301 24 0.5 0 50732 26663 -1 -1\n')


def write_tape(path, text=TAPE):
    path.write_text(text, newline='\n')
    return path


class ParseKey(unittest.TestCase):
    def test_parses_a_row(self):
        row = lib.parse_key('t1e34:886:75=c75.tape')
        self.assertEqual((row['leg'], row['cell'], row['phase']), ('t1e34', 886, 75))
        self.assertEqual(row['tape'].name, 'c75.tape')

    def test_refuses_a_missing_tape_field(self):
        with self.assertRaises(ValueError):
            lib.parse_key('t1e34:886:75')

    def test_refuses_a_short_spec(self):
        with self.assertRaises(ValueError):
            lib.parse_key('t1e34:886=c75.tape')

    def test_refuses_a_phase_off_the_ring(self):
        with self.assertRaises(ValueError):
            lib.parse_key('t1e34:886:200=c75.tape')
        with self.assertRaises(ValueError):
            lib.parse_key('t1e34:886:-25=c75.tape')

    def test_refuses_a_negative_cell(self):
        with self.assertRaises(ValueError):
            lib.parse_key('t1e34:-1:75=c75.tape')

    def test_refuses_a_leg_id_with_a_space(self):
        with self.assertRaises(ValueError):
            lib.parse_key('t1 e34:886:75=c75.tape')

    def test_refuses_an_overlong_leg_id(self):
        with self.assertRaises(ValueError):
            lib.parse_key('t' * 32 + ':886:75=c75.tape')


class Binding(unittest.TestCase):
    def test_reads_the_tape_header(self):
        self.assertEqual(lib.tape_binding(TAPE), ('mp/ctf_kejim', '1234567890', 'none'))

    def test_reads_the_graph_a_bound_tape_declares(self):
        self.assertEqual(lib.tape_binding(BOUND),
                         ('mp/ctf_kejim', '1234567890', 'g1-0011223344556677'))

    def test_refuses_another_file(self):
        self.assertIsNone(lib.tape_binding('graph 7 map=mp/ctf_kejim checksum=1 cells=3\n'))

    def test_refuses_a_header_without_a_checksum(self):
        self.assertIsNone(lib.tape_binding('decisiontape 3 map=mp/ctf_kejim entries=1\n'))

    def test_library_name_flattens_the_map_path(self):
        self.assertEqual(lib.library_name('mp/ctf_kejim', '99'), 'mp_ctf_kejim_99')


class Build(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.tape = write_tape(self.tmp / 'c75.tape')
        self.other = write_tape(self.tmp / 'c125.tape')

    def rows(self, *specs):
        return [lib.parse_key(spec) for spec in specs]

    def test_writes_the_index_and_copies_the_tapes(self):
        directory, manifest = lib.build(
            self.rows('t1e34:886:75=' + str(self.tape), 't1e34:886:125=' + str(self.other)),
            self.tmp / 'out')
        self.assertEqual(directory.name, 'mp_ctf_kejim_1234567890')
        self.assertEqual((directory / 'c75.tape').read_bytes(), self.tape.read_bytes())
        lines = (directory / 'index.txt').read_text().splitlines()
        self.assertEqual(lines[0],
                         'decisionindex 1 map=mp/ctf_kejim checksum=1234567890 keys=2 graph=none')
        self.assertEqual(lines[1], 'key t1e34 886 75 c75.tape')
        self.assertEqual(lines[2], 'key t1e34 886 125 c125.tape')
        self.assertEqual(manifest['map'], 'mp/ctf_kejim')
        self.assertEqual(len(manifest['keys']), 2)

    def test_the_index_carries_the_graph_the_tapes_declare(self):
        bound = write_tape(self.tmp / 'c75g.tape', BOUND)
        directory, manifest = lib.build(self.rows('t1e34:886:75=' + str(bound)),
                                        self.tmp / 'out')
        self.assertEqual(
            (directory / 'index.txt').read_text().splitlines()[0],
            'decisionindex 1 map=mp/ctf_kejim checksum=1234567890 keys=1 '
            'graph=g1-0011223344556677')
        self.assertEqual(manifest['graph'], 'g1-0011223344556677')

    def test_refuses_tapes_from_two_different_graphs(self):
        other = write_tape(self.tmp / 'c125g.tape',
                           BOUND.replace('g1-0011223344556677', 'g1-8899aabbccddeeff'))
        bound = write_tape(self.tmp / 'c75g.tape', BOUND)
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(bound),
                                't1e34:886:125=' + str(other)), self.tmp / 'out')

    def test_graph_may_be_declared_for_tapes_that_carry_none(self):
        directory, manifest = lib.build(self.rows('t1e34:886:75=' + str(self.tape)),
                                        self.tmp / 'out', graph='g1-8899aabbccddeeff')
        self.assertEqual(manifest['graph'], 'g1-8899aabbccddeeff')
        self.assertIn('graph=g1-8899aabbccddeeff',
                      (directory / 'index.txt').read_text().splitlines()[0])

    def test_refuses_a_declared_graph_that_contradicts_the_tape(self):
        bound = write_tape(self.tmp / 'c75g.tape', BOUND)
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(bound)), self.tmp / 'out',
                      graph='g1-8899aabbccddeeff')

    def test_index_is_written_with_lf(self):
        directory, _ = lib.build(self.rows('t1e34:886:75=' + str(self.tape)), self.tmp / 'out')
        self.assertNotIn(b'\r', (directory / 'index.txt').read_bytes())

    def test_refuses_two_rows_with_one_key(self):
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(self.tape),
                                't1e34:886:75=' + str(self.other)), self.tmp / 'out')

    def test_accepts_one_tape_under_two_keys(self):
        directory, _ = lib.build(self.rows('t1e34:886:75=' + str(self.tape),
                                           't1e34:886:125=' + str(self.tape)), self.tmp / 'out')
        self.assertEqual(len((directory / 'index.txt').read_text().splitlines()), 3)
        self.assertTrue((directory / 'c75.tape').is_file())

    def test_refuses_two_different_tapes_of_one_name(self):
        nested = self.tmp / 'nested'
        nested.mkdir()
        write_tape(nested / 'c75.tape', TAPE.replace('entries=1', 'entries=1 '))
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(self.tape),
                                't1e34:886:125=' + str(nested / 'c75.tape')), self.tmp / 'out')

    def test_refuses_tapes_of_different_bindings(self):
        write_tape(self.other, TAPE.replace('checksum=1234567890', 'checksum=99'))
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(self.tape),
                                't1e34:886:125=' + str(self.other)), self.tmp / 'out')

    def test_refuses_a_missing_tape(self):
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(self.tmp / 'absent.tape')),
                      self.tmp / 'out')

    def test_refuses_a_file_that_is_not_a_tape(self):
        (self.tmp / 'junk.tape').write_text('hello\n', newline='\n')
        with self.assertRaises(ValueError):
            lib.build(self.rows('t1e34:886:75=' + str(self.tmp / 'junk.tape')),
                      self.tmp / 'out')

    def test_refuses_an_empty_library(self):
        with self.assertRaises(ValueError):
            lib.build([], self.tmp / 'out')

    def test_refuses_more_keys_than_the_engine_reads(self):
        rows = self.rows(*['t1e34:%d:75=%s' % (n, self.tape) for n in range(65)])
        with self.assertRaises(ValueError):
            lib.build(rows, self.tmp / 'out')

    def test_main_writes_a_manifest(self):
        out = self.tmp / 'out'
        lib.main(['--out', str(out), '--key', 't1e34:886:75=' + str(self.tape)])
        directory = out / 'mp_ctf_kejim_1234567890'
        manifest = json.loads((directory / 'manifest.json').read_text())
        self.assertEqual(manifest['keys'][0]['file'], 'c75.tape')
        self.assertEqual(manifest['directory'], str(directory))

    def test_runs_as_a_script(self):
        out = self.tmp / 'script'
        result = subprocess.run(
            [sys.executable, str(Path(lib.__file__)), '--out', str(out),
             '--key', 't1e34:886:75=' + str(self.tape)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('mp_ctf_kejim_1234567890', result.stdout)


if __name__ == '__main__':
    unittest.main()
