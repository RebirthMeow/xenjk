"""A frozen control must satisfy exactly the same source checks as a checkout."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import movement_bench


class SourceBindingTests(unittest.TestCase):
    def check_root(self, supplied=None, missing=False, actual='expected'):
        build = Path('build')
        manifest = build / 'bound.sources.json'
        expected_root = movement_bench.ROOT if supplied is None else supplied
        with patch.object(Path, 'glob', return_value=[manifest]), \
             patch.object(Path, 'read_text', return_value=json.dumps({'sources': {'game.c': 'expected'}})), \
             patch.object(Path, 'is_file', return_value=not missing), \
             patch.object(movement_bench, 'digest', return_value=actual) as digest:
            if missing or actual != 'expected':
                with self.assertRaises(SystemExit):
                    movement_bench.stage_build(build, supplied)
            else:
                self.assertEqual(manifest, movement_bench.stage_build(build, supplied)[0])
                digest.assert_called_once_with(expected_root / 'game.c')

    def test_default_checkout(self):
        self.check_root()

    def test_frozen_snapshot(self):
        self.check_root(Path('snapshot'))

    def test_modified_snapshot_is_refused(self):
        self.check_root(Path('snapshot'), actual='modified')

    def test_incomplete_snapshot_is_refused(self):
        self.check_root(Path('snapshot'), missing=True)


if __name__ == '__main__':
    unittest.main()
