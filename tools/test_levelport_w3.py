# ===========================================================================
# Copyright (C) 2026, xenjk contributors
#
# This file is part of xenjk, an OpenJK derivative.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
# ===========================================================================
"""Witcher 3 and collision level-port tools: the pieces that run without game data."""
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np

LP = Path(__file__).resolve().parent / 'levelport'
sys.path.insert(0, str(LP))


class W3FxRecord(unittest.TestCase):
    def test_every_kind_packs(self):
        """Each hand-set W3 record carries every field cp77fx's emitter record packs."""
        import cp77fx
        import w3fx
        textures, emitters = {}, []
        for kind in w3fx.KINDS:
            em = {'texture': r'fx\textures\water\splash.xbm', 'atlas': [2, 2], 'name': kind, 'max_particles': 10}
            emitters.append(w3fx.emitter_record('a\\b.w2p', em, kind, textures))
        places = [(np.hstack([np.eye(3), np.zeros((3, 1))]), 0, 1.0, 0.0)]
        blob = cp77fx.xfx_blob(sorted(textures, key=textures.get), emitters, [(0, len(emitters), 60.0)], places, {},
                               np.zeros(3))
        magic, ver, nt, ne, ns, npl = struct.unpack_from('<4sI4I', blob)
        self.assertEqual((magic, nt, ne, ns, npl), (b'XFX\0', len(textures), len(emitters), 1, 1))

    def test_dropped_counts(self):
        import w3fx
        self.assertEqual(w3fx.decoded_dropped({'velocity_life': [1.0], 'velocity_life_mode': 'absolute',
                                               'life': [1, 2], 'life_random': False, 'stacked': {'x': 1}}),
                         ['velocity_life_absolute', 'life_start_end', 'stacked'])
        self.assertEqual(w3fx.decoded_dropped({'life': [1, 2], 'life_random': True}), [])


class ChunkMembers(unittest.TestCase):
    def test_plain_and_xbsz_read_alike(self):
        """colzbsp.member_bsp gives the plain BSP of a member whether it was packed plain or XBSZ."""
        import colzbsp
        try:
            import colpack
            colpack._lib()
        except (OSError, SystemExit):
            self.skipTest('no libzstd')
        lumps = 18
        body = b'\x11' * 400
        head = struct.pack('<4si', b'RBSP', 1) + b''.join(struct.pack('<ii', 8 + 8 * lumps, len(body) if i == 0 else 0)
                                                          for i in range(lumps))
        bsp = head + body
        with tempfile.TemporaryDirectory() as d:
            pk = os.path.join(d, 'a.pk3')
            with zipfile.ZipFile(pk, 'w') as z:
                z.writestr('maps/plain.bsp', bsp)
                z.writestr('maps/packed.bsp', colzbsp.encode(bsp))
            with zipfile.ZipFile(pk) as z:
                self.assertEqual(colzbsp.member_bsp(z, 'maps/plain.bsp'), bsp)
                self.assertEqual(colzbsp.member_bsp(z, 'maps/packed.bsp'), bsp)


class WorkBaselines(unittest.TestCase):
    """w3work: a step's copy made before an earlier step's last write is refused, not silently used."""

    def setUp(self):
        import contextlib
        import io
        self.td = tempfile.TemporaryDirectory()
        self.work = self.td.name
        os.makedirs(os.path.join(self.work, 'meshes'))
        for rel in ('placements.json', 'materials.json', os.path.join('meshes', 'index.json')):
            Path(self.work, rel).write_text('{"placements": []}' if rel == 'placements.json' else '{}')
        self.quiet = contextlib.redirect_stdout(io.StringIO())
        self.quiet.__enter__()

    def tearDown(self):
        self.quiet.__exit__(None, None, None)
        self.td.cleanup()

    def test_rerun_upstream_makes_later_copy_stale(self):
        import w3work
        w3work.start(self.work, 'foliage', keys=('placements',))
        w3work.wrote(self.work, 'foliage')
        w3work.start(self.work, 'trees')
        w3work.wrote(self.work, 'trees')
        w3work.start(self.work, 'trees')                        # (a re-run of trees alone: its copies still hold)
        w3work.wrote(self.work, 'trees')
        w3work.start(self.work, 'foliage', keys=('placements',))
        w3work.wrote(self.work, 'foliage')                      # (foliage again: trees' copies hold its old output)
        with self.assertRaises(SystemExit):
            w3work.start(self.work, 'trees')
        pre = w3work.start(self.work, 'trees', rebase=True)
        self.assertTrue(all(os.path.exists(p) for p in pre.values()))
        self.assertTrue(os.listdir(os.path.join(self.work, 'stale-baselines')))

    def test_new_export_sets_copies_aside(self):
        import w3work
        pre = w3work.start(self.work, 'flags')
        w3work.new_export(self.work)
        self.assertFalse(any(os.path.exists(p) for p in pre.values()))
        self.assertFalse(os.path.exists(os.path.join(self.work, w3work.STATE)))


class VerdictPaths(unittest.TestCase):
    def test_missing_class_is_a_miss(self):
        import tag_verdict
        with tempfile.TemporaryDirectory() as d:
            Path(d, 'parity.json').write_text('{"summary": {"instances": {"matched": 3}, "tiles": {"missing": 2}}}')
            self.assertEqual(tag_verdict.get(d, 'parity.summary.instances.only_a'), 0)
            self.assertEqual(tag_verdict.get(d, 'parity.summary.tiles.missing'), 2)
            self.assertIsInstance(tag_verdict.get(d, 'parity.summary.lights.only_a'), KeyError)
            self.assertIsInstance(tag_verdict.get(d, 'parity.summary.lights'), KeyError)


class PackWriting(unittest.TestCase):
    def test_renamed_only_when_complete(self):
        import pk3write
        with tempfile.TemporaryDirectory() as d:
            pk = os.path.join(d, 'zz_a.pk3')
            with pk3write.writing(pk) as z:
                z.writestr('maps/a.txt', b'a')
            with zipfile.ZipFile(pk) as z:
                self.assertEqual(z.read('maps/a.txt'), b'a')
            with self.assertRaises(RuntimeError):
                with pk3write.writing(os.path.join(d, 'zz_b.pk3')) as z:
                    z.writestr('maps/b.txt', b'b')
                    raise RuntimeError('stopped')
            self.assertFalse(os.path.exists(os.path.join(d, 'zz_b.pk3')))


class TerrainTiles(unittest.TestCase):
    def test_rect_fills_between_samples(self):
        import w3terrain
        self.assertEqual(w3terrain.tile_rect({(0, 0), (2, 1)}), [(0, 0), (0, 1), (1, 0), (1, 1), (2, 0), (2, 1)])


if __name__ == '__main__':
    unittest.main()
