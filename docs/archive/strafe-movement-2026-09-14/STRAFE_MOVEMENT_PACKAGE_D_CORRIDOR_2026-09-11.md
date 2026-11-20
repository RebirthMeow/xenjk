# Package D groundwork: the depleted corridor, and why BR-A moves (2026-09-11)

Evidence for the branch lanes, extracted from worlds already collected. **No
route is authored here and no improvement is claimed.** Nothing is installed;
the installed build remains `strafe-build-reviewready-20260910-b` (`525f1b50`).

This record supplies shape evidence. The owner-approved
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md)
owns the revised execution order, state contracts and acceptance checks, and the
[control record](STRAFE_MOVEMENT_PACKAGE_D_CONTROL_2026-09-11.md) owns the
frozen case set, the measured control bands and the windows declared from the
geometry below. The family split used here — the band 3600 < y < 4300 with the
x separator at 2350 — is frozen in that manifest and implemented in
`tools/strafe_momentum_audit.py`, so the classification is no longer local to
`extract_corridor.py`.
The corrections below clarify evidence scope; no collection was rerun.

## Where the evidence comes from

`.codex-research/pkgD-corridor-20260911/extract_corridor.py` reads the carried
episodes of the twenty arm G worlds
(`.codex-research/depleted-return-20260911-g`, build
`b5d38e71…`, sources matching the working tree) and separates the episodes
that flew route 5's group 1 from those that did not. In the band
`3600 < y < 4300` the group-1 flight runs at x 2538..2663 and the depleted
corridor at x 2089..2187, so x separates them cleanly where a height
threshold does not — an earlier height test misclassified a flown episode and
produced the wrong answer.

- Red, route 5: **22** depleted episodes, 5 that flew group 1.
  `corridor-red.json`
- Blue, route 4 half-turned about (1312, 2080) into route 5 coordinates:
  **23** depleted episodes, 6 that flew. `corridor-blue-mirrored.json`

The mirror is exact: route 4 half-turned about (1312, 2080) matches route 5 on
all 70 samples to within 0.01 units, so the two directions are measured in one
frame.

## Finding 1: the measured corridor does not reach samples 24..27

The plan specifies BR-A as a "tap-only lane from the stand run-up (samples
4..9) to the landing area at sample 21 and through the 51 degree turn as a hop
line, rejoining the lane at ~24..27". **The measured depleted corridor does not reach that landing area.**
This does not prove every other ordinary-jump approach impossible.

Nearest approach of the depleted corridor to each lane sample, red:

| Lane sample | 21 | 22 | 24 | 26 | 27 | 29 | 33 | 36 | 42 | **44** | 49 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Nearest XY (units) | 597 | 591 | 512 | 445 | 412 | 358 | 216 | 96 | 2.2 | **0.9** | 1.7 |

In twenty-two depleted episodes the runner never came within 350 units of the
landing platform. Every one of them descends instead at about (2440, 4570) and
runs a corridor at x 2090..2190. The platform at (2585, 3435, 1808) is reached
only by episodes that fly group 1 in this collection — that is what group 1 is for.

Blue agrees: its closest convergence at or after sample 30 is 3.7 units at
sample 47, 4.2 at 40, 4.4 at 42 and 5.4 at 50.

**So BR-A targets a candidate rejoin near sample ~43/44, not 24..27**.
Full-state transition feasibility remains unmeasured. For early BR-A entries
this would replace lane groups 1 (takeoff 10), 2 (takeoff 29) *and* 3 (takeoff 36) with
a single branch. Three of the unbranched groups in the 2026-09-10 table are
potentially covered by one lane for early entries, in both directions.
A runner already on the group-1 landing platform needs separate entry coverage.

## Finding 2: the corridor is already fast, except at one choke

Per-station medians across the 22 red episodes, stationed on y:

| y | x | z | median u/s | min u/s | grounded |
|---|---|---|---|---|---|
| 4900 | 2437.0 | 1912.1 | 479.0 | 250.4 | 100 % |
| 4700 | 2418.3 | 1883.9 | 404.0 | 88.5 | 67 % |
| 4600 | 2404.6 | 1841.0 | 403.9 | 79.4 | 100 % |
| **4500** | **2386.8** | **1819.2** | **101.3** | **45.7** | 100 % |
| 4400 | 2315.8 | 1808.1 | 425.0 | 250.0 | 100 % |
| 4300 | 2177.6 | 1808.1 | 431.6 | 252.0 | 95 % |
| 4000 | 2135.5 | 1869.7 | 535.4 | 356.5 | 0 % |
| 3600 | 2089.1 | 1862.1 | 687.2 | 250.0 | 5 % |
| 3200 | 2046.3 | 1823.0 | 689.0 | 417.4 | 0 % |
| 2800 | 1996.7 | 1808.1 | 429.4 | 145.8 | 95 % |
| 2400 | 1779.2 | 1919.1 | 653.9 | 260.9 | 5 % |
| 2200 | 1512.0 | 1810.8 | 750.3 | 250.2 | 50 % |

Only one station falls below 300 u/s: **y = 4500, median 101.3, min 45.7** —
the descent step from the upper corridor (z 1912) to the lower floor
(z 1808). Blue has the same choke (116.5) and a second at y = 4300 (89.8).

These are pooled station medians, with Force/Speed and entry momentum mixed;
they do not establish zero-Force/no-Speed competence. The table also retains
red medians near 403..404 u/s and blue has the second choke described below. The descent is the first measured bottleneck to target. Whether the branch
also improves the rest of the corridor must be measured separately by resource
state; production ownership alone does not establish faster movement.

## Finding 3: the choke has a named cause, and it is not tuning

`[SUPPORTEDTRACE]` over the choke window (level 2350..2550 in `R2-red`):

```
corner_skip source=6 corner_reason=trim pass_reason=speed_below_walk_floor
corner_skip source=7 corner_reason=trim pass_reason=speed_below_walk_floor
corner_skip source=8 corner_reason=trim pass_reason=speed_below_walk_floor
corner_skip source=9 corner_reason=trim pass_reason=speed_below_walk_floor
```

`speed_below_walk_floor` is **not** about the runner's live speed. It is the
geometrically admissible pass speed at `ai_bot2_supported.c:390`:
`v_pass = PASS_YAW_RATE/16 * min(la,lb)^2`, refused below 100 u/s, so legs
shorter than about 16 units keep the strict stop. The navmesh legs at the
descent are shorter than that, so every vertex there brakes to under 80 u/s.
This is the "pass-through reach is real but narrow" gap the 2026-09-10 record
already recorded, met head-on.

It cannot be tuned away inside the supported executor without lengthening the
legs, which is not that executor's to do. An authored strafe line through the
descent is the remedy the plan already prescribes: *generic supported walking
stays the last resort*.

## What to author, in route-5 coordinates

BR-A, as a `STRAFEROUTE1` member of a new branch fixture:

- **Leave** the lane on the run-up at sample 8/9, (2472, 4904..4852, ~1914),
  within a declared and natively checked entry envelope (the run-up can reach
  800 u/s); arbitrary arrival speed is not an admission guarantee.
- **Descend** through (2418, 4700, 1884) -> (2405, 4600, 1841) ->
  (2387, 4500, 1819) -> (2316, 4400, 1808). This is the stretch that must not
  stop; the rest of the corridor does not need help.
- **Run** the corridor (2178, 4300, 1808) -> (2135, 4000, 1870) ->
  (2089, 3600, 1862) -> (2046, 3200, 1823) -> (1997, 2800, 1808) ->
  (1779, 2400, 1919) -> (1512, 2200, 1811) as a hop line.
- **Candidate rejoin** near lane sample 43/44, (1575.7, 2234.1, 1819.2) /
  (1497.6, 2154.1, 1834.8). Measured convergence 0.9..2.2 units red,
  3.7..5.4 blue. Check height, heading, velocity, contact, latch, the sample-43
  cue and onward affordability before committing; XY convergence alone cannot
  authorize this exit.
- **Mirror** to route 4 with `tools/strafe_route_halfturn.py --center 1312 2080`,
  whose identity on this pair is exact.

Physics budget for the authoring, unchanged from the plan: tap vz 225
(0.5625 s of air, about 240 units at 425 u/s and 340 at 600), ground cap 425
with Force Speed and 250 without, air-strafe gain about +40 u/s per hop, held
jumps cost 10 to start and drain further, so a tap-only lane is the zero and
low-Force class.

## What this does not settle

- **BR-B is not addressed.** Its group (takeoff 29) is inside the stretch BR-A
  now spans only for early BR-A entries. Runners that successfully fly group 1
  and then cannot fly group 2 require BR-B or a checked connector from their
  landing-platform state. Retain that obligation until measured; spatial
  overlap cannot close it.
- Groups 5 (takeoff 50) and 7 (57) remain unbranched, and this record says
  nothing about them.
- Nothing here is authored, converted, bundled or run. The corridor is what
  native play already did; whether an authored line holds it faster is
  unmeasured.
- The blue-only choke at y = 4300 (89.8 u/s) is unexplained. It may be the
  same short-leg refusal at a different vertex, or a direction-specific
  approach; its trace has not been read.
- The runtime cannot execute a branch yet. B3 (the sidecar branch table,
  `fastReturn_t.branchRoute/rejoinIndex/branchGroup` and the projection switch
  in `Bot2_StrafeProject`) and B6 are still unstarted, and a branch lane
  without them is an asset nothing loads.
