"""A multilayer bake file holds the key of the code and sizes that made it (cp77mat.Materials.bake_code, RL-120)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent / 'levelport'))
import cp77mat  # noqa: E402


class BakeKey(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.m = cp77mat.Materials(self.d, bake_dir=self.d / 'bake')
        self.a = np.zeros((2, 2, 3), np.float32)

    def tearDown(self):
        self.tmp.cleanup()

    def bake(self, name, code=None):
        extra = {'code': np.array(code)} if code is not None else {}
        f = self.d / (name + '.npz')
        np.savez(f, col=self.a, nrm=self.a, orm=self.a, detail=np.array(json.dumps(None)), **extra)
        return f

    def test_a_bake_with_this_key_loads(self):
        self.assertIsNotNone(self.m._load_bake(self.bake('good', self.m.bake_code()[1]), self.m.bake_code()))

    def test_a_bake_with_another_key_is_baked_again(self):
        self.assertIsNone(self.m._load_bake(self.bake('stale', '0' * 16), self.m.bake_code()))

    def test_a_keyless_bake_loads_only_under_the_legacy_code(self):
        f = self.bake('legacy')
        legacy = (cp77mat.Materials.LEGACY_BAKE_CODE, self.m.bake_code()[1])
        self.assertIsNotNone(self.m._load_bake(f, legacy))
        self.assertIsNone(self.m._load_bake(f, ('x' * 16, self.m.bake_code()[1])))
        # (once the bake computation changes, every keyless bake is baked again: the code key moves off the legacy one)
        self.assertEqual(self.m._load_bake(f, self.m.bake_code()) is not None,
                         self.m.bake_code()[0] == cp77mat.Materials.LEGACY_BAKE_CODE)

    def test_the_sizes_are_in_the_key(self):
        other = cp77mat.Materials(self.d, layer_size=256, bake_dir=self.d / 'bake')
        self.assertEqual(other.bake_code()[0], self.m.bake_code()[0])
        self.assertNotEqual(other.bake_code()[1], self.m.bake_code()[1])


if __name__ == '__main__':
    unittest.main()
