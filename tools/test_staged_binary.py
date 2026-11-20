"""Exercise staging on real files; retain fixtures under .codex-research."""
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone
import errno
import json
import multiprocessing
from pathlib import Path
import stat
import unittest
from unittest import mock
import uuid

import staged_binary


def stage_worker(source, destination, campaign_root):
    """Module-level entry point supports Windows multiprocessing spawn."""
    return staged_binary.stage_binary(source, destination, campaign_root=campaign_root)


class StagedBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        cls.root = (Path(__file__).resolve().parents[1] / '.codex-research' /
                    ('test-staged-binary-' + stamp + '-' + uuid.uuid4().hex[:8]))
        cls.root.mkdir(parents=True)
        print('\nRetained staging test fixtures: ' + str(cls.root), flush=True)

    def setUp(self):
        self.folder = self.root / self._testMethodName
        self.folder.mkdir()
        self.source = self.folder / 'build' / 'jampgamex86.dll'
        self.source.parent.mkdir()
        self.source.write_bytes(b'original binary fixture\x00' * 128)
        self.expected = staged_binary.digest(self.source)

    def destination(self, index):
        path = self.folder / ('world-' + str(index)) / 'home' / 'base' / self.source.name
        path.parent.mkdir(parents=True)
        return path

    def stage(self, destination):
        return staged_binary.stage_binary(self.source, destination, campaign_root=self.folder)

    def journal(self):
        return [json.loads(line) for line in
                (self.folder / '.staged-binaries' / 'staging.jsonl').read_text().splitlines()]

    def test_actual_hardlinks_hashes_and_source_isolation(self):
        first, second = self.destination(1), self.destination(2)
        one, two = self.stage(first), self.stage(second)
        master = Path(one['master'])
        self.assertEqual(one['mode'], 'hardlink')
        self.assertEqual(two['mode'], 'hardlink')
        self.assertTrue(first.samefile(second))
        self.assertTrue(first.samefile(master))
        self.assertFalse(self.source.samefile(master))
        self.assertEqual(staged_binary.digest(first), self.expected)
        self.source.write_bytes(b'subsequent build output')
        self.assertEqual(staged_binary.digest(master), self.expected)
        self.assertEqual(staged_binary.digest(second), self.expected)
        third = self.destination(3)
        newer = self.stage(third)
        self.assertNotEqual(newer['master'], one['master'])
        self.assertEqual(staged_binary.digest(third), staged_binary.digest(self.source))
        self.assertEqual(len(self.journal()), 3)

    def test_rollover_caps_world_links(self):
        destinations = [self.destination(i) for i in range(5)]
        with mock.patch.object(staged_binary, 'MAX_WORLD_LINKS', 2):
            records = [self.stage(path) for path in destinations]
        self.assertEqual(records[0]['master'], records[1]['master'])
        self.assertEqual(records[2]['master'], records[3]['master'])
        self.assertEqual(len({row['master'] for row in records}), 3)
        self.assertEqual(Path(records[0]['master']).stat().st_nlink, 3)
        self.assertEqual(Path(records[2]['master']).stat().st_nlink, 3)
        self.assertEqual(Path(records[4]['master']).stat().st_nlink, 2)
        self.assertTrue(all(staged_binary.digest(path) == self.expected for path in destinations))

    def test_concurrent_processes_share_verified_master(self):
        destinations = [self.destination(i) for i in range(12)]
        context = multiprocessing.get_context('spawn')
        with ProcessPoolExecutor(max_workers=4, mp_context=context) as pool:
            futures = [pool.submit(stage_worker, str(self.source), str(path), str(self.folder))
                       for path in destinations]
            records = [future.result(timeout=60) for future in futures]
        self.assertEqual(len({row['master'] for row in records}), 1)
        master = Path(records[0]['master'])
        self.assertEqual(master.stat().st_nlink, len(destinations) + 1)
        self.assertTrue(all(path.samefile(master) for path in destinations))
        self.assertTrue(all(staged_binary.digest(path) == self.expected for path in destinations))
        journal = self.journal()
        self.assertEqual(len(journal), len(destinations))
        self.assertEqual({row['destination'] for row in journal}, {str(p) for p in destinations})

    def test_cross_volume_fallback_warns_and_journals(self):
        destination = self.destination(1)
        with mock.patch.object(staged_binary.os, 'link', side_effect=OSError(errno.EXDEV, 'cross-volume')):
            with self.assertWarnsRegex(UserWarning, 'staged private copy'):
                record = self.stage(destination)
        self.assertEqual(record['mode'], 'copy')
        self.assertIn('cross-volume', record['fallback_reason'])
        self.assertFalse(destination.samefile(record['master']))
        self.assertEqual(staged_binary.digest(destination), self.expected)
        self.assertEqual(self.journal(), [record])

    def test_existing_destination_is_never_overwritten(self):
        destination = self.destination(1)
        destination.write_bytes(b'preexisting world')
        with self.assertRaises(FileExistsError):
            self.stage(destination)
        self.assertEqual(destination.read_bytes(), b'preexisting world')

    def test_destination_collision_during_link_is_not_copy_fallback(self):
        destination = self.destination(1)

        def collision(master, target):
            Path(target).write_bytes(b'concurrent owner')
            raise FileExistsError(errno.EEXIST, 'destination won by another process')

        with mock.patch.object(staged_binary.os, 'link', side_effect=collision):
            with self.assertRaises(FileExistsError):
                self.stage(destination)
        self.assertEqual(destination.read_bytes(), b'concurrent owner')
        self.assertFalse((self.folder / '.staged-binaries' / 'staging.jsonl').exists())

    def test_corrupt_master_is_rejected(self):
        record = self.stage(self.destination(1))
        master = Path(record['master'])
        master.chmod(stat.S_IREAD | stat.S_IWRITE)
        master.write_bytes(b'deliberately corrupted test master')
        destination = self.destination(2)
        with self.assertRaisesRegex(RuntimeError, 'Immutable staging master changed'):
            self.stage(destination)
        self.assertFalse(destination.exists())
        self.assertEqual(len(self.journal()), 1)


if __name__ == '__main__':
    unittest.main()
