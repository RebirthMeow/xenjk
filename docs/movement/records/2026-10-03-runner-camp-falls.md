# B3: the runner's falls while camping (2026-10-03)

Owner (2026-10-03): keep runner camping ("a bit of denying the enemy from capping more so than capping yourself") and
fix its falls. The [stalls record](2026-10-02-b3-stalls.md) measured runner falls 3.5 -> 5.8 a world with camping on.
Offline reanalysis only, no new worlds: `.codex-research/b3-20261002-a/runcamp/` (`off-1..16`, `on-1..16`, build
`b3-build-20261002-j`, 4v4 `--two-sided --camper --roles base,chase`, Kejim). Scripts in the same directory:
`falls.py` (every runner `MOD_FALLING` death, classified; output `falls.txt`) and `exposure.py` (travel seconds by
objective).

## The +2.3 was one runner

`laps_compare.py`'s `runner_falls` reads `summary.json`'s `runner_deaths`, which `defense_laps.py` fills for
`Runner_v2` (blue) alone; the red runner's deaths are in `runner2_deaths` and were never compared. Per 600 game s:

| | off | on | difference |
|---|---|---|---|
| `Runner_v2` (the old metric) | 3.51 | 5.79 | +2.28 +- 0.84 |
| `Runner2_v2` | 5.17 | 4.07 | -1.10 |
| both runners (`runners_falls`, new) | 8.67 | 9.86 | **+1.19 +- 1.19** |
| both, falls with the camp objective | 0.18 | 0.99 | **+0.81 +- 0.24** |
| both, every other fall | 8.49 | 8.87 | +0.38 |

`laps_compare.py` now also prints `runners_falls` and `runners_weapon_deaths` (both runners); `runner_falls` keeps its
meaning. Camping adds about 0.8 falls a world; the other falls do not change.

## Where the camping falls happen

`falls.py` takes the fall's start as the last grounded sample at least 150 u above where the runner died (Kejim's pit
kills seconds after the drop), the objective there from the play CSV (3 is `MACRO_CAMP_REGEN`), the last
`RUNNER_CAMP` goal, defender `THROW`/`HIT` lines matched to the runner's position in the 3 s before, and the killer
credited by the game log.

16 runner falls in the on arm started while camping (3 in the off arm, where the idle runner just stands):

- **14 on the way to the camp spot**, 1,100-6,400 u from it, mostly around the central walkway (x 1,300-2,600,
  y 2,300-3,400, z 1,808): 6 thrown (pull or push matched), 2 credited to an enemy (shot or knocked), 6 the runner's
  own hop sequences.
- **2 at the spot itself** (goal within 48 u), both on the red side's lower ledge (spot about 608, -1312, z 1,826;
  the stand is at z 1,908) beside the pit: one pulled (a matched `THROW`), one credited to base D.
- **None chasing the enemy carrier** (`fc`): the re-snapped goal never preceded a fall.

Travel exposure, both runners, horizontal speed over 100 u/s:

| objective | arm | travel s | falls | per travel minute | own (no enemy) per minute |
|---|---|---|---|---|---|
| fetch | off | 7,903 | 59 | 0.45 | 0.26 |
| fetch | on | 6,314 | 56 | 0.53 | 0.28 |
| return | on | 5,608 | 86 | 0.92 | 0.18 |
| camp | on | 2,516 | 16 | **0.38** | **0.14** |
| camp | off | 338 | 3 | 0.53 | 0.36 |

Camping turns about 2,200 s of standing (off arm) into travel into the enemy base. That travel falls **less** often
per minute than fetching does. The extra falls are exposure on the runner's ordinary route, not a camping defect.

## The handoff's suspects

- Camp spot beside a lethal drop: real but small. Lower-ledge spots (z 1,826, 82 u under the stand, within
  `CAMP_LEVEL_Z` 160) held runners about 200 of the 1,670 s spent at a spot and produced both at-spot falls
  (0.12 a world). A fix (spots at the stand's level, or a pit clearance) cannot be measured at 16 worlds an arm.
- `fc` goal re-snapping: no falls; dropped.
- Standing still as a pull target: the two at-spot falls are this, on the ledge spot only.
- Route into the enemy base: the bulk (14 of 16), at a lower rate than the same route while fetching.

## Next (owner's call)

Camping costs about 0.8 falls a world against +3.2 pickups, at a lower fall rate per travel minute than fetching.
Options: accept it; keep camp spots to the stand's level (removes the lower ledge, expected -0.1 a world,
unmeasurable); or treat it as the general runner travel-fall problem (fetch 0.53, return 0.92 a travel minute,
about 40% thrown), which is not camping work.
