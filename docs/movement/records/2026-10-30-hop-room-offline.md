# Hop room: does route geometry predict the runner's hop flow beyond the planner's cost? (2026-10-30, offline, then a native guide pair)

Owner question (2026-10-30): could a small learned model help route choice? The candidate use was a hop-room term
in the planner's cost, because guided routes were cheaper by that cost yet hopped less
([frame-cost record](2026-10-06-frame-cost.md), "preloaded goal tables": Kejim -7% hops, -9% chained, -15 u/s).
This is the offline check that comes first: no native runs, no source change. Evidence and scripts:
`.codex-research/hoproom-20261030-a/` (`run.sh`, `extract.py`, `analyze.py`, `results.txt`, `samples.csv`,
`calibration.csv`, `inputs/MANIFEST.txt`).

## Data

The guide on/off arms (`guide-outcome-20261006-a`) are gone. They were in the 2026-10-26 runner-era move to
`.codex-trash`, which has since been emptied. What remains are twelve stopped laps worlds that kept their C: staging
folders: Kejim 2026-10-05 seed 1 (two worlds) and 2026-10-08 seeds 9-12; Dantooine 2026-10-05 seed 1 (two) and
2026-10-09 seeds 1-4. All are 4v4, two-sided, about 4 minutes of game time each, on the planner's defaults (guidance
off), across three builds. The inputs these worlds provided were copied into `inputs/` and sha256-checked (306 MB):
console log, play CSV, game log, settings and build manifest. The graph seeds are hard links to the kept copies
(`move-explore-20260916-b/seed.txt`; `mapprep-20260927-a` Dantooine). Extraction from the D: copies reproduces the
staging run row for row.

**Unit.** One sample per runner (Runner_v2, Runner2_v2) per second. The route installed at that moment (the latest
`[ROUTE]` line; the CSV's route columns are 0 for these runners) is rebuilt as a polyline:

- Listed edges: takeoff and landing from the seed, or `[MOVE] ... reason=learn` lines for learned edges.
- Walk gaps between edges: the cell search's walk graph (links, diagonals, clearance penalty).

The next 1,200 u from the runner's nearest vertex (within 160 u) are priced with the state search's own walk law, in
Python. That is `StateWalkOf`: friction 6, stop speed 100, acceleration 10, wish 250 or 425 with Speed. Edges are
charged their costMs and carry their exit speed. Outcomes are measured over the next 2.5 s:

- Mean horizontal speed.
- Airborne share, from hop launch and land events.
- Hops.
- Chained hops: under 100 ms on the ground before the hop, as in the 2026-10-05 flow measure.

**Pricing check** (`calibration.csv`, routes over 500 ms). The ratio of rebuilt to printed route cost has a median
of 1.00-1.03, except Kejim routes priced without Speed, where it is 1.21. Those are probably routes that plan a Speed
purchase; the per-route ratio (`route_scale`) is a planner feature, so the baseline keeps it. Rebuilt and printed cell
counts agree (median 40 against 34 in one Kejim world).

**Kept.** 2,432 samples: Kejim 1,336, Dantooine 1,096. Skipped:

- 886 off the route.
- 628 within 200 u of the route's end.
- 646 on a route that could not be rebuilt. These are almost all learned edges whose index was never printed:
  3-35% of a world's routes, worst in the two 2026-10-05 Dantooine worlds (`unbuilt.txt`).
- 648 dead, or dying within the window.

## Feature sets and test

- **cost**: the planner's pace on the stretch (length / priced time) and `route_scale`.
- **planner**: cost plus what the planner or the runner's state already holds: the edge mix ahead (share of length on
  edges, strafe and drop/jump counts), current speed, Speed active, carrying, Force, airborne now.
- **base**: planner plus threat and goal controls: nearest alive enemy, enemies within 600 u, distance to the route's
  goal.
- **geometry**, added to base:
  - mean and minimum node width;
  - mean and minimum clearance (the planner's own wall distance);
  - floor room (cells within 128 u at the same height);
  - turning per 1,000 u;
  - longest run within 20 degrees;
  - sinuosity;
  - climb and drop per 1,000 u.
- **location**: base plus x, y, z, a stricter control. Geometry is a property of place, so this asks whether the
  geometry of the route chosen adds anything beyond where the runner is.

Leave one world out, five folds per map: the two worlds of one 2026-10-05 seed replay their first seconds, so they
are one fold. Ridge and a small gradient-boosted model.

## Results

Out-of-sample R². Δ is what geometry adds; (k/5) is the number of folds where it helped.

| | | cost | planner | base | + geometry (Δ, folds) | location | + geometry (Δ, folds) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Kejim gbm | speed | 0.15 | 0.53 | 0.63 | 0.64 (+0.01, 4/5) | 0.63 | 0.65 (+0.02, 4/5) |
| | airborne | 0.06 | 0.14 | 0.19 | 0.22 (+0.03, 3/5) | 0.18 | 0.20 (+0.03, 4/5) |
| | hops | 0.05 | 0.17 | 0.22 | 0.29 (+0.07, 5/5) | 0.27 | 0.30 (+0.03, 4/5) |
| | chained | 0.07 | 0.19 | 0.22 | 0.34 (+0.12, 5/5) | 0.31 | 0.34 (+0.03, 4/5) |
| Kejim ridge | hops | 0.00 | 0.11 | 0.13 | 0.19 (+0.06, 5/5) | 0.21 | 0.21 (-0.01, 2/5) |
| | chained | 0.00 | 0.14 | 0.14 | 0.22 (+0.07, 5/5) | 0.22 | 0.22 (-0.01, 3/5) |
| Dantooine gbm | speed | 0.30 | 0.57 | 0.67 | 0.69 (+0.02, 5/5) | 0.68 | 0.69 (+0.01, 5/5) |
| | airborne | 0.00 | 0.05 | 0.11 | 0.15 (+0.04, 5/5) | 0.15 | 0.15 (+0.01, 3/5) |
| | hops | 0.02 | 0.05 | 0.14 | 0.20 (+0.06, 4/5) | 0.22 | 0.24 (+0.02, 5/5) |
| | chained | -0.02 | 0.12 | 0.21 | 0.27 (+0.06, 4/5) | 0.24 | 0.27 (+0.03, 4/5) |

The full ridge rows are in `results.txt`. Its Kejim speed and airborne rows give geometry +0.06 even with location.

- **The planner's cost says almost nothing about hopping.** The cost set alone explains 0-7% of hops, chained hops
  and airborne share on both maps. On Kejim, realized speed is flat across planned-pace fifths (median 441, 471, 441,
  419, 495 u/s). What the planner knows apart from cost (mostly the runner's current speed and Speed) carries most of
  the explained speed.
- **Geometry adds, consistently, with threat and goal held:** hops +0.06-0.07 and chained +0.07-0.12 on Kejim in 5 of
  5 folds; +0.02-0.06 on Dantooine in 4-5 of 5. Speed gains are small for the boosted model (+0.01-0.02).
- **Most of that is place.** With x, y, z held too, the boosted model keeps +0.02-0.03 for hops and chained in 4-5 of
  5 folds on both maps, and ridge keeps nothing for hops or chained. The geometry signal is real but largely tells
  apart the parts of the map; the route-direction part on top of that is small.
- **Size at equal planned pace.** Samples are split into fifths by the out-of-sample score (the boosted speed forecast
  with geometry minus without). The fifths have the same planned pace (Kejim 349-361 u/s, Dantooine 299-308) and
  similar current speed and nearest-enemy distance:

  | least room → most room | speed u/s | hops / 2.5 s | chained / 2.5 s | airborne |
  | --- | --- | --- | --- | --- |
  | Kejim | 442 → 488 (+45) | 1.49 → 1.81 (+21%) | 0.67 → 1.00 (+49%) | 0.49 → 0.54 |
  | Dantooine | 375 → 411 (+36) | 1.21 → 1.53 (+26%) | 0.35 → 0.64 (+80%) | 0.51 → 0.55 |

  The spread is several times the guided routes' loss (-15 u/s, -7% hops, -9% chained). A planner that drifts one
  fifth toward less room at equal cost would reproduce that loss. This is consistent with the 2026-10-06 reading, not
  a demonstration of it: the guided arms that would show it are gone.
- **Which geometry** (ridge, standardized, base held; boosted-model permutation ranks agree):
  - **Kejim.** Node width: hops +0.30 and chained +0.25 per sd, speed +31 u/s. Longest straight run: chained +0.20
    per sd, speed +25 u/s. Drops ahead cut chains: hops -0.33, chained -0.34 per sd. Climb costs speed: -36 u/s per
    sd.
  - **Dantooine.** Straight run: hops +0.21 per sd. Minimum floor room: hops +0.40 per sd. Mean floor room: speed +38
    u/s per sd. Climb's sign differs from Kejim: Dantooine's climbs are ramps that the hops take.
  - **Clearance.** The planner's own wall-distance term comes out negative once width and room are in. It overlaps
    them and is already in the cost.

## Reading

Yes: on these worlds, route geometry predicts the runner's hop flow better than the planner's cost does. The cost on
its own predicts essentially none of it. Most of the geometry signal is "which part of the map", and only a small
consistent part is the direction the route takes there. The directions that hold on both maps are a straight run
ahead and floor room or corridor width. Drops ahead hurting chains is clear on Kejim only. Climb is map-specific, so a
learned or hand term would need per-map care or should leave climb out.

Limits:

- Observational: these are the default planner's own routes, not a route-choice experiment.
- Twelve short worlds on three builds.
- An approximate rebuild of the walk portions.
- Excluded routes with unprinted learned edges, which would bias against learned-edge-heavy Dantooine routes if their
  geometry differs.
- Small per-window counts for hops and chained hops.

## Native follow-up: guide off/on on paired Kejim seeds (2026-10-30, owner: "run the guided on/off Kejim pair")

Build `hoproom-build-20261030-a` (`tools/strafe_fixture_build.py`, HEAD 97c25de3; game sources unchanged since
2763e8ce). `runarms.py` ran one 60 s warm-up world first, against the fresh-build first-launch stall: it started
normally and is kept, not analysed. Then Kejim seeds 3-10 in each arm, the same seeds as 2026-10-06 (seeds 1-2 built
the guide's goal list). Settings: `laps_campaign.COMMON` plus `--two-sided` (the milestone-B 4v4), 600 game s at rate
25, `--seed N`, four worlds at once, unpinned. The on arm adds `bot_movePlanGuide 1` and `bot_movePlanGuideMax 1`; its
worlds log `guide_preload goals=76 built=76`.

Checks: all 16 worlds ran 605-613 game s, with Runner2_v2 named correctly and one or two ready echoes. Large "Hitch
warning" lines (up to 133 s) appear in every world, including the older ones; game levels keep stepping through them,
so they come from how the x25 rate counts wall time, not from a stall.

**Extraction trap, fixed.** A finished `defense_laps` world saves its graph over the staged seed: learned edges
appended, and seed edges with their end-of-game costs (dearer, switched off). The first pass priced routes with
those costs: rebuilt/printed median 2.5-3.0, up to 58×. `extract.py` now prices seed edges from the pristine seed
(`PRISTINE`). Learned edges take their cost and exit speed from their learn lines, and only their takeoff and landing
from the saved file. After the fix:

- Calibration matches the older Kejim worlds: median 1.04 with Speed, 1.19-1.20 without, in both arms.
- Unbuilt samples fall to 8-9 an arm.
- The older worlds' samples are unchanged row for row.

Results (`arms.txt`, `compare.txt`; on minus off, paired by seed, mean ± se over 8 seeds):

| | off | on | on - off | 2026-10-06 (on - off) |
| --- | --- | --- | --- | --- |
| hops a world, both runners | 716.5 | 709.6 | -6.9 ± 15.9 | -43.5 ± 12.2 |
| chained hops | 353.3 | 350.5 | -2.8 ± 9.6 | -34.4 ± 9.9 |
| mean speed alive, u/s | 416.0 | 412.2 | -3.7 ± 8.9 | -14.9 ± 6.3 |
| airborne, share alive | 0.530 | 0.532 | +0.002 ± 0.012 | -0.015 ± 0.007 |
| team captures (laps_compare) | 3.20 | 3.21 | +0.01 ± 0.42 | -1.36 ± 0.91 (captures, both teams) |
| runner falls / weapon deaths, both | 8.49 / 31.26 | 8.77 / 31.48 | +0.28 ± 1.58 / +0.22 ± 2.13 | |

- **The guided routes' hop loss does not reproduce on this build.** Every hop measure is within about half a standard
  error, and so are captures and runner deaths.
- **The routes flown have the same hop room.** Samples on the installed route ahead (5,445 off, 5,417 on) were scored
  by the hop-room model trained only on the older worlds. The score differs by -0.06 ± 0.66, and so does every
  geometry measure ahead: width -20 ± 23 u, straight run -2 ± 12, room, drop, climb and turning all inside one se.
  The share in each room fifth is the same (least-room fifth 25.8% off, 24.9% on). Planned pace on the stretch ahead
  is 4.7 ± 3.2 u/s lower with the guide.
- **The guide still does its work.** State-search expansions are 3.46 M → 2.13 M a world (1.63× fewer), and searches
  over 5,000 expansions 125 → 56. The planner's own elapsed time is 10.2 → 8.6 s a world, and plans of 50 ms or more
  39.6 → 17.4. These worlds ran four at once, unpinned, with a levelport compile running in another session, so the
  times are indicative only. The 2026-10-06 pinned timing is the measured gain: Kejim frames over 100 ms 55 → 10.
- **The offline finding replicates on a new build.** The model trained on the older worlds predicts the new off arm
  better with geometry than without: hops R² 0.21 → 0.29, chained 0.21 → 0.31, speed 0.58 → 0.60.

**Reading.** On the current defaults, planner guidance keeps its planner saving and no longer measurably costs hops,
speed or captures on Kejim, at 8 paired seeds. Its routes have the same hop room as the straight-line heuristic's.
What changed since 2026-10-06 is not shown. Candidates are the defaults and fixes that landed after it: route tables,
reroute joins, the frame-end pool, the review fixes and competitive Force. So a hop-room cost term is not needed to
make guidance safe. Geometry still predicts hop flow that the cost ignores, but no route change here shows that cost
being acted on.

**Not done, needs the owner:**

- Whether guidance comes back on: `bot_movePlanGuide 1`, `bot_movePlanGuideMax 1`.
- If so, the checks the rules call for first: a pinned timing pair on this build, run alone, and a Dantooine outcome
  pair. 2026-10-06 Dantooine showed no frame gain.
- Eight seeds make a loss the size of 2026-10-06's unlikely (-43.5 hops sits 2.3 se below this pair's -6.9 ± 15.9),
  but they cannot rule out a small one.

Files: `runarms.py`, `arms.py`, `arms.txt`, `compare.txt`, `arms.csv`, `calibration-arms.csv`, and `worlds/` (16 arm
worlds plus the warm-up, 1.4 GB).

## Re-enable checks: pinned Kejim timing and a Dantooine outcome pair (2026-10-30, owner: "run the pinned timing pair alone on this build and a Dantooine guide off/on outcome pair")

`runphase2.py`, same build. Both parts used `laps_campaign.COMMON` plus `--two-sided`, 600 game s at rate 25.

- **Timing.** Kejim seeds 1-2 (the 2026-10-06 timing seeds), the frame meter on in both arms
  (`bot_moveFrameMeter 1`, `bot_moveFrameMeterSlow 25000`), one world at a time with no other world of mine
  running. Order ABBA, two repetitions per seed and arm.
  - Pinning: the spec pair (CPUs 2, 4) was 61% and 26% busy with other sessions' work at the start, so the worlds
    were pinned to CPUs 12 and 14 instead: two threads on two P-cores, the same core type.
  - Outside load on their sibling threads 13 and 15 (`typeperf`, every 5 s) averaged 0.1-3.5% in every world.
  - Repetitions replay: work meter and route decisions are identical. The only difference is seed 1 on, where one
    repetition logged three more route lines at the shutdown cutoff.
- **Dantooine.** Seeds 3-10, four worlds at once. All 16 started cleanly and ran 606-610 game s; the guide preloaded
  in exactly the on worlds.

**Kejim timing** (`timing.txt`; seed 1 / seed 2, mean of the two repetitions; frame meter totals over all eight bots):

| | off | on |
| --- | --- | --- |
| frames over 100 ms | 24.5 / 25.5 | 3.0 / 1.5 |
| frames over 50 ms | 41.0 / 43.5 | 8.5 / 19.0 |
| frames over 25 ms | 156 / 159 | 104 / 107 |
| worst frame, ms | 223 / 161 | 142 / 124 |
| p99, ms | 21.5 / 21.75 | 20.4 / 20.5 |
| work meter est. frames over 25 ms (load-independent) | 669 / 629 | 483 / 490 |
| state-search expansions a frame, mean | 251 / 247 | 166 / 171 |
| planner s a world / plans of 50 ms or more | 8.6 / 32, 8.4 / 35 | 6.3 / 6, 6.6 / 7 |

The repetitions agree to within 1 ms of p99 and a few frames over 50 or 100 ms. The gain holds in every
repetition. It matches 2026-10-06 (frames over 100 ms 55 → 10 over two worlds), now on the current defaults.

**Dantooine outcomes** (`dan-compare.txt`, `dan-arms.txt`; on minus off, paired se, 8 seeds):

| | off | on | on - off |
| --- | --- | --- | --- |
| team captures | 4.44 | 5.11 | +0.68 ± 0.58 |
| all captures | 9.24 | 10.47 | +1.23 ± 1.07 |
| runner falls, both | 3.33 | 2.34 | -0.99 ± 1.00 |
| runner weapon deaths, both | 34.38 | 33.13 | -1.25 ± 2.14 |
| hops a world, both runners | 603.1 | 591.8 | -11.4 ± 22.7 |
| chained hops | 223.1 | 216.0 | -7.1 ± 11.2 |
| mean speed alive, u/s | 377.1 | 379.4 | +2.3 ± 9.7 |
| airborne, share alive | 0.502 | 0.501 | -0.001 ± 0.016 |
| hop-room score of the routes flown | 1.79 | 4.58 | +2.79 ± 1.43 |

Nothing passes the noise cut. Guided routes have, if anything, more hop room on Dantooine: 32.8% of samples are in the
older worlds' most-room fifth, against 28.3%. The search work falls (expansions 6.87 M → 4.04 M a world, 1.70×), but
Dantooine's searches are cheap: planner time is 2.63 → 2.51 s a world, and no plan reaches 50 ms in either arm. So, as
on 2026-10-06, Dantooine gets no frame-time gain. The older-trained hop-room model again predicts the new Dantooine
off arm better with geometry: hops R² 0.20 → 0.24, chained 0.21 → 0.25.

## Verdict: can guidance come back on?

**On the measured maps, yes.**

- **Kejim** keeps the frame-tail gain on the current build. Frames over 100 ms fall about tenfold, over 50 ms by two
  thirds, the worst frame by a third, and p99 by about 1 ms. Hops, speed, captures, falls and deaths show no measurable
  cost at 8 paired seeds.
- **Dantooine** shows no cost in outcomes or hop flow, and no frame gain.
- The 2026-10-06 reason for leaving it off, the hop loss, does not reproduce on Kejim or Dantooine.

What this does not cover:

- **Maps without a preloaded goal list.** Only Kejim and Dantooine have `.guide` files. Elsewhere, tables are built in
  play once a goal is asked for three times, and that upkeep is what cancelled the first version's gain on 2026-10-06.
- **Dantooine frame time with the guide**, pinned. It was not timed here; on 2026-10-06 its tail was flat to slightly
  worse.
- **Losses smaller than about two standard errors** of these pairs (about 30 hops a world on Kejim, about 45 on
  Dantooine).

**Owner, 2026-10-30: make it the default.** `bot_movePlanGuide` and `bot_movePlanGuideMax` now default to 1 in
`bot2_graph.c`; 0 turns each off. The guide also preloads at map load when the cvar is unset. Build
`guidedefault-build-20261030-a`.

Files: `runphase2.py`, `timing.py`, `timing.txt`, `timing/`, `dan-compare.txt`, `dan-arms.txt`, `dan-arms.csv`,
`calibration-dan.csv`, `dantooine/`.
