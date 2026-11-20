"""Summarize complete sweep outcomes, separately from trace analysis windows.

The caller declares the complete arm/round grid and weighting strata. Selecting
strata after seeing outcomes makes the weighted result exploratory, not an
independent confirmation. Unconditioned outcomes and excluded-stratum counts
are always reported alongside the weighted comparison.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics


def stats(values):
    return {"n": len(values), "mean_ms": statistics.mean(values),
            "min_ms": min(values), "max_ms": max(values)}


def read_json(path, bindings):
    data = path.read_bytes()
    bindings.append({"path": str(path.resolve()),
                     "sha256": hashlib.sha256(data).hexdigest()})
    return json.loads(data)


def load_world(world, arm, case, bindings):
    campaign = read_json(world / "result.json", bindings)
    detail = read_json(world / case / "result.json", bindings)
    cases = campaign.get("cases", [])
    if len(cases) != 1 or cases[0].get("name") != case:
        raise ValueError(f"Expected exactly case {case}: {world}")
    outcome = cases[0]
    evaluation = detail.get("evaluation", {})
    if (not campaign.get("verdict", {}).get("green")
            or outcome.get("status") != "pass"
            or evaluation.get("status") != "pass"
            or detail.get("case", {}).get("name") != case):
        raise ValueError(f"Incomplete or failed world: {world}")
    duration = outcome.get("time_ms")
    corner = detail.get("corner_level")
    if (type(duration) is not int or duration <= 0
            or duration != evaluation.get("time_ms")
            or type(corner) is not int
            or str(detail.get("settings", {}).get("bot_moveExitSpeed")) != str(arm)):
        raise ValueError(f"Invalid or inconsistent outcome/settings: {world}")
    return {"world": world.name, "arm": arm, "corner_level": corner,
            "time_ms": duration}


def summarize(records, arms, strata):
    if not records or not strata or len(set(strata)) != len(strata):
        raise ValueError("Nonempty records and unique explicit strata required")
    counts = Counter(row["corner_level"] for row in records
                     if row["corner_level"] in strata)
    by_arm = {}
    for arm in arms:
        rows = [row for row in records if row["arm"] == arm]
        if not rows:
            raise ValueError(f"Missing arm {arm}")
        cells = {level: stats([row["time_ms"] for row in rows
                              if row["corner_level"] == level])
                 for level in sorted({row["corner_level"] for row in rows})}
        if any(level not in cells for level in strata):
            raise ValueError(f"Arm {arm} missing a requested stratum")
        included = sum(row["corner_level"] in strata for row in rows)
        by_arm[str(arm)] = {
            "overall": stats([row["time_ms"] for row in rows]),
            "by_corner_level": {str(k): v for k, v in cells.items()},
            "weighted_mean_ms": sum(cells[level]["mean_ms"] * counts[level]
                                    for level in strata) / sum(counts.values()),
            "weighted_coverage": {"included": included,
                                  "excluded": len(rows) - included},
        }
    return {"metric": "official completed case time_ms",
            "weighting": {"strata": strata, "pooled_counts": dict(counts),
                          "included": sum(counts.values()),
                          "excluded": len(records) - sum(counts.values()),
                          "caveat": "If strata were selected post hoc, this is exploratory; it does not replace independent confirmation."},
            "arms": by_arm, "worlds": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--arms", type=int, nargs="+", required=True)
    parser.add_argument("--rounds", type=int, required=True)
    parser.add_argument("--strata", type=int, nargs="+", required=True)
    parser.add_argument("--case", default="red_out_L1_stand")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.rounds < 1 or len(set(args.arms)) != len(args.arms):
        parser.error("Positive round count and unique arms required")
    expected = {f"x{arm}-r{round_}" for arm in args.arms
                for round_ in range(1, args.rounds + 1)}
    actual = {path.name for path in args.root.glob("x*-r*") if path.is_dir()}
    if actual != expected:
        parser.error(f"World grid mismatch: missing={sorted(expected - actual)}, unexpected={sorted(actual - expected)}")
    bindings = []
    records = [load_world(args.root / f"x{arm}-r{round_}", arm, args.case, bindings)
               for arm in args.arms for round_ in range(1, args.rounds + 1)]
    report = summarize(records, args.arms, args.strata)
    report.update({"case": args.case, "expected_rounds": args.rounds,
                   "input_bindings": bindings,
                   "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()})
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
