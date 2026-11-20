# Spreading guidance-table builds over frames (2026-11-10)

Owner, 2026-11-10: "Do 1 then 2". Item 2 of HANDOFF "Next action (2026-11-10)", the last open frame-cost item of
the 2026-11-09 review that is not a timed run (design in
[2026-11-09-frame-cost-lossless.md](2026-11-09-frame-cost-lossless.md)).

## Change (22bd8a5b, `bot_movePlanGuideSpread`, default 0)

`codemp/game/bot2_graph.c`. With the cvar at N > 0, a guidance table that `GuideFor` would build whole is started
instead (`GuideStart`) and settles at most N cells a frame (`GuideSpreadRun`, from `Graph_PathTableStep`, and once
in the asking call). One table builds at a time, since the settle heap (`guideHeap`) is shared:

- A search asked while its table builds, or wanting a new table while another builds, runs unguided
  (`guideStats.waits`); asked again later, it gets its table.
- Folds wait while a build holds the heap. The building slot's `seen` is the log position at its start, so its
  next fold takes in the edges learned while it built, as a whole build's does.
- A rebuild of the arcs, a preload, or a clearance change drops the pending build (`GuidePendCancel`).
- Preloaded goals are still built whole at the load. The cap of 64 × cells + 1024 settles applies to the whole
  build.

At 0 nothing changes: build `-h` with its defaults replays the new-defaults control exactly on Kejim 35 and
Dantooine 35 (`identity-branch-*.txt`, the branch's `laps_identity.py`). The chain's own identity step had run the
main checkout's older copy, which does not mask `guide_preload`'s wall-clock `ms=` field. It reported line 2
differing, so the arms stage was started by hand (`run-arms.sh`).

## Arm

- Build `review-next-build-20261110-h` (22bd8a5b), arm `sp` = `bot_movePlanGuideSpread 8000`, about 2 ms of
  settling a frame (Kejim's graph is about 14,000 cells, so a table takes two frames).
- Control: the new-defaults worlds `ndctl-20261110-a/worlds/nd-*` (build -g,
  [2026-11-10-new-defaults-control.md](2026-11-10-new-defaults-control.md)), paired by seed.
- The 4v4 laps setup, rounds of 16 Kejim / 8 Dantooine seeds until decided or out of seeds.
- Evidence: `.codex-research/spread-20261110-a/` (`runarms.log`, `compare-*.txt`, `work.txt`, `worlds/sp-*`).
  64 worlds, none failed.

## Results (arm minus control, per 600 game s)

| | Kejim (48 pairs) | Dantooine (16 pairs) |
|---|---|---|
| team captures | 4.08 → 3.79, -0.29 ± 0.21, undecided | 4.38 → 4.75, +0.37 ± 0.35, undecided |
| all captures | -0.66 ± 0.43 | +0.74 ± 0.67 |
| runners' falls | -0.18 ± 0.49 (inside ±15%) | -0.93 ± 0.57 |
| camper falls | +0.54 ± 0.38 | +1.23 ± 0.52 |
| guide cells a world | 11.95M → 10.97M (-8%, ± 0.13M) | 13.50M → 12.60M (-7%, ± 0.35M) |
| largest frame's guide cells | 52,350 → 41,462 (-21%, ± 1,076) | 64,486 → 60,796 (-6%, ± 3,178) |
| est frames over 25 ms | 584 → 545 (-6.5%, ± 9) | 1,708 → 1,657 (-3%, ± 68) |

- Outcomes: nothing decided. Kejim reads down and Dantooine up, each by about 1 se, with no reason in the change
  for the maps to differ.
- Frame cost: real but small. The largest frame keeps about 41,000 guide cells on Kejim and 61,000 on Dantooine,
  five to eight times the 8,000 budget. Every build after the load goes through the spread path, so the rest of the
  peak should be folds (`GuideFold`, still whole; a frame can fold several slots). The meter does not count folds
  apart, so that is inferred, not measured. The work-meter tail moves 3-6%.
- About 8% fewer guide cells a world: searches asked during a build run unguided and do not ask for a table, and
  some asks are never repeated.

## Reading

`bot_movePlanGuideSpread` stays 0. Spreading builds alone moves the frame peak by a fifth on Kejim and barely on
Dantooine, apparently because the peak frames are mostly folds. To go further:
1. Spread or cap folds too: one fold per frame, or the fold heap on its own budget. Same outcome-arm shape, the same
   control.
2. Leave guidance as it is and look elsewhere for Dantooine's tail: the replans themselves, which BP-2's profile
   (a timed run, owner) would place.

Either way the control is `ndctl-20261110-a/worlds/nd-*` for builds that replay it.
