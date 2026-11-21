"""memgate's ledger: a rename over a file another process is reading waits for it (Windows refuses it meanwhile)."""
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'levelport'))
import memgate  # noqa: E402


class LedgerReplace(unittest.TestCase):
    def test_a_rename_over_a_file_held_open_goes_through_once_it_is_closed(self):
        with tempfile.TemporaryDirectory() as d:
            f, tmp = Path(d) / 'a.json', Path(d) / 'a.tmp'
            f.write_text('old', encoding='utf-8')
            tmp.write_text('new', encoding='utf-8')
            held = open(f, encoding='utf-8')            # (a reader: on Windows the rename is refused meanwhile)
            t = threading.Timer(0.2, held.close)
            t.start()
            t0 = time.time()
            memgate._replace(tmp, f)
            t.join()
            self.assertEqual(f.read_text(encoding='utf-8'), 'new')
            self.assertLess(time.time() - t0, 2.0)



class CommitCap(unittest.TestCase):
    def test_the_page_file_as_it_stands_less_the_margin(self):
        self.assertAlmostEqual(memgate.commit_cap(63.2, 31.8), 63.2 - memgate.PAGE_MARGIN_GB)

    def test_never_under_ram_plus_the_allowance_nor_past_the_limit(self):
        self.assertAlmostEqual(memgate.commit_cap(50.0, 31.8), 31.8 + memgate.PAGE_ALLOW_GB)
        self.assertAlmostEqual(memgate.commit_cap(40.0, 31.8), 40.0)



class LimitSeen(unittest.TestCase):
    def test_a_grown_page_file_does_not_lift_the_cap(self):
        import os
        with tempfile.TemporaryDirectory() as d:
            os.environ[memgate.LEDGER_ENV] = d
            memgate._LIMIT0[0] = None
            try:
                self.assertEqual(memgate.limit_seen(63.0), 63.0)
                self.assertEqual(memgate.limit_seen(87.0), 63.0)      # grown since: the run keeps the first
                memgate._LIMIT0[0] = None                            # (another process of the run reads the record)
                self.assertEqual(memgate.limit_seen(87.0), 63.0)
                self.assertEqual(memgate.limit_seen(60.0), 60.0)      # smaller now: that one, and the cap may not pass it
                (Path(d) / 'stray.json').write_text('63.0', encoding='utf-8')
                self.assertEqual(memgate.ledger_others(), 0.0)       # (the record and stray files are not reservations)
            finally:
                del os.environ[memgate.LEDGER_ENV]
                memgate._LIMIT0[0] = None


class LedgerLive(unittest.TestCase):
    def test_an_outgrown_reservation_still_counts_as_running(self):
        import json
        import os
        import psutil
        with tempfile.TemporaryDirectory() as d:
            parent = psutil.Process(os.getppid())
            # another live process's reservation of 0.001 GB, which its tree has long outgrown
            (Path(d) / 'x.json').write_text(json.dumps({'owner': parent.pid, 'born': parent.create_time(),
                                                        'pid': parent.pid, 'peak': 0.001, 'label': 'x'}),
                                            encoding='utf-8')
            os.environ[memgate.LEDGER_ENV] = d
            try:
                self.assertEqual(memgate.ledger_others(), 0.0)
                self.assertEqual(memgate.ledger_others(live=True), 1)
            finally:
                del os.environ[memgate.LEDGER_ENV]


class PartPeakScale(unittest.TestCase):
    def test_estimates_follow_the_measured_peaks_after_a_few_parts(self):
        import cp77_tiles
        sc = cp77_tiles.PeakScale()
        for _ in range(4):
            sc.add('scene', 2.5, 8.5)
        self.assertEqual(sc.scale('scene'), 1.0)
        sc.add('scene', 2.8, 8.5)
        self.assertAlmostEqual(sc.scale('scene'), 2.8 / 8.5 * sc.SCALE_ROOM)
        self.assertEqual(sc.scale('build'), 1.0)

    def test_never_above_the_table_nor_under_the_floor(self):
        import cp77_tiles
        sc = cp77_tiles.PeakScale()
        for _ in range(5):
            sc.add('scene', 0.1, 10.0)
        self.assertEqual(sc.scale('scene'), sc.SCALE_FLOOR)
        sc.add('scene', 12.0, 10.0)
        self.assertEqual(sc.scale('scene'), 1.0)


if __name__ == '__main__':
    unittest.main()
