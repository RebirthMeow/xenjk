# Depleted-resource returns: attribution and corner momentum (2026-09-10)

Packages A and C of the approved depleted-return plan. **Nothing is installed.**
The installed build remains `strafe-build-reviewready-20260910-b` (`525f1b50`).
Packages B, D and E have not started. K07 remains open.

Scope: this record establishes *where* the route-5 return loses momentum, on
native evidence, and delivers the corner-execution half of the repair. It does
not establish depleted-Force competence, which needs Package B's continuation
choice and Package D's branch lanes.

## Evidence index

| What | Where |
|---|---|
| Package A attribution, eight findings | `.codex-research/depleted-return-20260910-a/attribution.md` |
| Package C record, tuning rounds, honest gaps | `.codex-research/corner-momentum-20260910/record.md` |
| Declared cases (20, both directions) | `.codex-research/depleted-return-20260910-a/cases.json` |
| Before worlds + audit (build `a7fa5e58…`) | `.codex-research/depleted-return-20260910-a/worlds`, `momentum-audit.json` |
| After worlds + audit (build `131d3e82…`) | `.codex-research/depleted-return-20260910-d/worlds`, `momentum-audit.json` |
| Intermediate pass-through tuning rounds (retained) | `.codex-research/depleted-return-20260910-b`, `-c` |
| Recorded start states (212) from the owner's play | `.codex-research/depleted-return-20260910-a/seeds.json` |
| Corner-track law selection | `.codex-research/pkgC3-corner-track/result.json` |
| Refused poses and the source-freeze refusal, retained | `.codex-research/depleted-return-20260910-a/worlds/R7*-red-z1922.9`, `.codex-research/corner-cases-20260910/before` |

World configuration for every case, identical to the owner's installed runner:
`bot_supportedSpans 1`, `bot_supportedCorners 2`, `bot_supportedTrace 1`,
staged flight adapter (`flight.txt` sha256 `0cf03c14…`, byte-identical to the
installed `runner_flight.txt`), `bot_forecastGuard 1`, `bot_carrierMelee 1`,
`bot_fastReturn 1`, `bot_momentumTrace 1`; catalog
`tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt` (sha256 `5be10703…`,
byte-identical to the installed `kejim_routes.txt`).

## How to rerun this

A fresh build is required after any C/C++/header/CMake change, and the smoke
runner refuses a build whose sources no longer match the checkout.

```powershell
$b = '.codex-research/strafe-build-' + [guid]::NewGuid().ToString('N')
python tools/strafe_fixture_build.py --out $b
```

```bash
python .codex-research/depleted-return-20260910-a/run_cases.py --build <build> --out <out>/worlds --cases .codex-research/depleted-return-20260910-a/cases.json --flight-artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --game-seconds 55 --seconds 170 --workers 4 --port 48000
```

```bash
PYTHONPATH=tools python tools/strafe_momentum_audit.py <out>/worlds/*/ --out <out>/momentum-audit.json
```

The driver runs at most four isolated worlds with distinct ports and
homepaths, merges its run record rather than overwriting it, and retains every
world. A full twenty-case pass takes about eight minutes of wall time.

## New instruments

- `[FASTRETURN] admission client level route index group takeoff landing dist dz
  class speed force speed_remaining result`, one line per (join, group).
- `[FASTRETURN] release` extended with `speed force speed_remaining group
  landing_xy landing_dz`.
- `bot_momentumTrace`: 40 Hz `[MOMENTUM]` line while a movement-only runner
  carries. Four-hertz rows cannot show a one-frame stop and every braking rule
  under study lasts under 250 ms.
- `bot_cornerVelocity "vx vy vz"`: declared native start velocity for
  `bot_corner_start`, confirmed in `[CORNER] START ... velocity= vx= vy= vz=`.
- `tools/strafe_play_state_seeds.py` (+ unit tests): start states from recorded
  play, bound to the telemetry file that was open when each console record was
  written, because level time restarts with every world.
- `tools/strafe_momentum_audit.py`: owner spans, refusal windows, churn pairs
  and resource buckets, split by Force bucket and Speed state, never pooled.
- `tools/strafe_corner_track_check.py`: selects the ground-turn lateral bound
  instead of assuming it, and asserts the old law reproduces the reported crawl.

## Attribution (Package A, on unchanged movement rules)

1. **The receiver reserve refused a flight the runner then completed.** Route 5
   group 2 (the 660-unit gap at sample 29) was refused twice at 16 Force for
   `takeoff_receiver_reserve`, then admitted 500 ms later and landed safely:
   launch 425.2 u/s, air-strafe to 663.6, grounded on the far side.
2. **The launch class depends on where the runner stands, not on the launch.**
   The same group reported `dist=643.0 class=20`, `dist=544.6 class=20`,
   `dist=455.6 class=10`. The distance is measured from the live origin, so the
   churn itself walks the runner close enough to lift the reserve.
3. **A refused takeoff costs about 360 u/s within 100 ms** (425.0 u/s at the
   refusal frame, 65.6 u/s two frames later under `fwd=-127 right=-127`).
4. **The sub-80 arrival rule is a commanded ramp to a stop:** commands
   78/52/33/22/14 with speed 286.5/187.2/122.1/81.3/49.4/47.5.
5. **The 250 cap dominates every stretch without Force Speed** (798-sample
   bucket, median 250.4 u/s).
6. **A seeded run-up is spent before the lane can accept it:** armed at level
   1625, first join 4.9 s later at 310.9 u/s, because the supported owner held
   the frame and commanded `fwd=-127` for fifteen consecutive frames.
7. Churn reproduced in both directions at every resource level, including the
   owner's own pattern (refusal at 27, rejoin at 29, refusal, rejoin at 36 at
   **53.3 u/s**).
8. All 20 worlds captured, and that is not evidence of health.

## Corner momentum (Package C)

| Item | Change |
|---|---|
| C1 | `min(250, sqrt(600 r))` -> `min(425, sqrt(1000 r))`; radius published per point; pursuit lookahead bounded to `sqrt(16 r)` on a curve |
| C2 | Pass-through vertices: a bounded ground turn where no arc fits, swept at compile time, crossed rather than stopped |
| C3 | Turn admission to 120 degrees, trim floor 24 -> 16, every refusal named |
| C4 | Bounded 250 ms acquisition coast along the queried first leg over floor probed that frame |
| C5 | Hop room ends at a pass vertex; the ramp includes pass speed; no flight window is planned into an interval ending at one |

The lateral constant was **selected**: the binding constraint is the executor's
own 360 deg/s yaw limit (`v <= omega r`), which admits at most 1200 at the
32-unit radius floor. 1000 is compiled and the check asserts it does not exceed
what the arcs actually held.

Measured across the same 20 worlds, before -> after:

| Measure | Before | After |
|---|---|---|
| Stationary carrier samples (< 20 u/s) | 366 | **64** |
| Worlds containing a full stop (< 1 u/s) | 16 | **0** |
| Zero-input samples | 436 | **208** |
| Join-then-resource-release within 300 ms | 61 | **34** |
| Frames owned by the fast-return lane | 6076 | **8540** |
| Frames owned by the supported fallback | 22119 | 20875 |

Resource buckets with Speed active (samples / median of per-world medians /
samples below 250 u/s): 1-12 Force `3582/411.6/952` -> `3889/425.4/516`;
13-20 Force `2066/440.1/488` -> `2185/672.5/229`; 21+ Force
`10227/570.9/1225` -> `9808/613.8/896`. Inactive-Speed medians stay at 250
because 250 *is* the ground maximum without Force Speed.

## Which cases now work, and which remain weak

Every case still reaches a capture; the table below is about the maneuvers, not
the outcome. "Refused groups" are the launches the lane declined in that world.

| Case | Seed | Admitted | Refused groups |
|---|---|---|---|
| R1 red/blue | route sample 8, 26 F, Speed 13.3 s | 9 / 12 | 4, 5 (+7 blue) |
| R2 red/blue | sample 8, 12 F | 5 / 13 | 3, 4, 5 |
| R3 red/blue | sample 8, 20 F | 11 / 6 | 4, 5 |
| R4 red/blue | sample 8, 0 F, no Speed | 5 / 5 | 3, 4 (`takeoff_flight_resources`) |
| R5 red/blue | sample 8, Speed expiring at the landing | 10 / 5 | 3, 4 (+5 red) |
| R6a red/blue | recorded S1 pickup, 26 F | 12 / 12 | 2, 3, 4, 5 |
| R6b red/blue | recorded S3 depleted post-overshoot, 6 F | 5 / 8 | 0, 3, 4, 5, 7 |
| R7f26 red/blue | sample 1 run-up, 26 F | 12 / 13 | 2, 3, 4, 5 |
| R7f20 red/blue | sample 1 run-up, 20 F | 14 / 12 | 2, 3, 4, 5 |
| R7f12 red/blue | sample 1 run-up, 12 F | 6 / 6 | 1, 3, 4, 5 |

**Working now.** Corner execution and the acquisition seam: no world contains a
full stop, stationary samples fell by 83 percent, and the lane owns 40 percent
more frames than before. Group 1 (the held jump at takeoff 10) executes from a
seeded run-up at 20 and 26 Force. Group 2 (the 660-unit gap at takeoff 29) is
reachable and, once admitted, is completed at 16 Force.

**Still weak.** Every flight group in the second half of route 5 is refused in
the depleted cases and falls through to the generic supported fallback, because
**no branch exists yet**:

- Group 2 (takeoff 29) — `takeoff_receiver_reserve` below 20 Force.
- Group 3 (takeoff 36), group 4 (43), group 5 (50), group 7 (57) — refused as
  `takeoff_receiver_reserve`, `takeoff_reserve` or `takeoff_flight_resources`
  depending on Force and remaining Speed. All four are unbranched.
- Group 1 (takeoff 10) at 12 Force — refused for the receiver reserve even at
  807 u/s of real run-up, which is the plan's BR-A case.
- Group 0 (the tap at sample 2) at 6 Force with no Speed (`R6b`).

## Honest gaps in this record

- **T4 is not measured.** The audit reports per-owner and per-refusal windows,
  not per-published-corner windows. Correlating `[SUPPORTEDTRACE] node` limits
  with trace positions is the remaining work before T4 can be claimed.
- The six declared corner cases (upper/ramp/stall, both directions) have no
  matched before/after: the pre-Package-C build was run against a changed
  checkout and the source-freeze guard refused all six. Only the treatment arm
  exists and no improvement is claimed for them.
- The 26 temporary-body cases and the 30 resource cases with fast return off
  have **not** been rerun on this build.
- `R7f12-red` regressed on one measure: longest grounded slow window
  2350 -> 3275 ms, while its stationary samples fell from 31 to 0.
- Pass-through reach is real but narrow: 210 acceptances at 5 distinct map
  vertices across 20 worlds, with 96 refused by their own native sweeps. Legs
  under about 16 units still keep the stop.
- The churn reduction (61 -> 34) is a side effect of fewer stalls, not a fix.
  Package B's churn guard has not been written.

## Regression status on the changed sources

`strafe_corner_track_check`, `strafe_supported_wait_check`,
`strafe_flightplan_check`, `strafe_fast_return_check`,
`strafe_route_resume_check`, `strafe_catalog_check`, `strafe_hop_arrival_check`
all pass; Python suites 384 tests OK. The sweep is retained in
`.codex-research/handoff-checks-20260910`, and every `codemp/` source hash
recorded in those `result.json` files matches the working tree, so the pass
is bound to the sources this handoff describes and not to an earlier revision. `strafe_route_resume_check` and
`strafe_catalog_check` had been stale since the fast-return lane landed (last
passing evidence 2026-09-04) and were repaired here: a missing `byte` typedef,
the `LoadRoutes(void)` / `SelectRoute(ent, routeTeam)` signatures, and the
load/prepare split.
