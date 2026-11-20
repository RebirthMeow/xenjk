import contextlib
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from movement_sweep_summary import load_world, main, summarize


class SweepSummaryTests(unittest.TestCase):
    def test_pooled_weights_and_excluded_tail_preserve_overall(self):
        records = [{"arm": arm, "corner_level": corner, "time_ms": duration}
                   for arm, corner, duration in [(500, 3300, 10), (500, 3300, 10),
                                                (500, 3350, 20), (500, 3275, 90),
                                                (2000, 3300, 30), (2000, 3350, 40)]]
        report = summarize(records, [500, 2000], [3300, 3350])
        self.assertEqual(report["arms"]["500"]["weighted_mean_ms"], 14)
        self.assertEqual(report["arms"]["2000"]["weighted_mean_ms"], 34)
        self.assertEqual(report["arms"]["500"]["overall"]["max_ms"], 90)
        self.assertEqual(report["weighting"]["excluded"], 1)

    def test_missing_cell_rejected(self):
        with self.assertRaises(ValueError):
            summarize([{"arm": 500, "corner_level": 3300, "time_ms": 10}],
                      [500], [3300, 3350])

    def world(self, status="pass", detail_time=15000):
        outcome = {"name": "case", "status": status, "time_ms": 15000}
        campaign = {"verdict": {"green": True}, "cases": [outcome]}
        detail = {"evaluation": dict(outcome, time_ms=detail_time),
                  "case": {"name": "case"}, "corner_level": 3350,
                  "settings": {"bot_moveExitSpeed": "500"}}
        return [json.dumps(doc).encode() for doc in (campaign, detail)]

    def test_official_duration_and_hash_binding(self):
        bindings = []
        with patch.object(Path, "read_bytes", side_effect=self.world()):
            row = load_world(Path("world"), 500, "case", bindings)
        self.assertEqual(row["time_ms"], 15000)
        self.assertEqual(len(bindings), 2)
        self.assertEqual(len(bindings[0]["sha256"]), 64)

    def test_failed_world_rejected(self):
        with patch.object(Path, "read_bytes", side_effect=self.world(status="incomplete")):
            with self.assertRaises(ValueError):
                load_world(Path("world"), 500, "case", [])

    def test_inconsistent_duration_rejected(self):
        with patch.object(Path, "read_bytes", side_effect=self.world(detail_time=14900)):
            with self.assertRaises(ValueError):
                load_world(Path("world"), 500, "case", [])

    def test_missing_result_is_not_omitted(self):
        with patch.object(Path, "read_bytes", side_effect=FileNotFoundError):
            with self.assertRaises(FileNotFoundError):
                load_world(Path("world"), 500, "case", [])

    def test_incomplete_grid_rejected(self):
        argv = ["summary", "root", "--arms", "500", "--rounds", "2",
                "--strata", "3350", "--out", "report.json"]
        with patch("sys.argv", argv), patch.object(Path, "glob", return_value=[]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                main()

    def test_existing_output_is_not_overwritten(self):
        argv = ["summary", "root", "--arms", "500", "--rounds", "1",
                "--strata", "3350", "--out", "report.json"]
        row = {"arm": 500, "corner_level": 3350, "time_ms": 15000}
        with (patch("sys.argv", argv),
              patch.object(Path, "glob", return_value=[Path("x500-r1")]),
              patch.object(Path, "is_dir", return_value=True),
              patch("movement_sweep_summary.load_world", return_value=row),
              patch.object(Path, "read_bytes", return_value=b"tool"),
              patch.object(Path, "mkdir"),
              patch.object(Path, "open", side_effect=FileExistsError) as output):
            with self.assertRaises(FileExistsError):
                main()
            output.assert_called_once_with("x", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
