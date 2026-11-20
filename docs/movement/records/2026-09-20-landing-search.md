# Why the landing search did not run, 2026-09-20

All four distinct retained landings without a recorded straight search now
have a measured reason: **`off_path_index`**. The bot is grounded at the end
of its committed sequence, but live path lookup returns -1, so the next
straight search is not entered. The eight other targeted grounded commits
are a different cohort: their tap-now proposals were rejected by the selected
path, stopped on unknown trigger contact, or both. These findings do not
justify treating trigger stops as falls or relaxing safety checks.

## Evidence and correspondence

Start with the [corrected momentum audit](2026-09-19-what-the-kept-speed-buys.md):
58 pooled landings, 39 after retaining each distinct observed trace once.
The present targets are the four sequence-ending grounded landings with no
recorded straight search and eight grounded commits without an admitted
tap-now alternative, from `mom-on-r1` and `mom-on-r3`. The duplicated `r2`
trace is not counted again.

[The extraction and comparison report](../../../.codex-research/landing-retained-20260920-c/report.json)
binds the original audit and every input result, server log and play CSV by
SHA-256. It contains every target's position, speed, candidate outcome,
unsupported mask, native source line, and same-frame replay skip telemetry.
[The readable table](../../../.codex-research/landing-retained-20260920-c/report.md)
includes all nine fresh comparison worlds, including those that do not match.
Earlier derived reports remain retained.

Fresh diagnostics use the unchanged `review-arrival-build-3110d3de7b40460aaebae4e7591c8b0e`,
the seeded `red_out_L1_stand`, no corridor, `--edges --parity 256`, momentum
trace, no spending, and `bot_moveReroute=0`. The
[protocol](../../../.codex-research/landing-investigation-20260920T0651/protocol.md)
specifies three sequential alternating replay-off/on pairs. Three additional
replay-on worlds seek the missing fourth state only; they are not additional
paired performance measurements. The baseline source snapshot is verified
against its original build manifest.

The comparison changes only the `level` origin to fixture-relative time and
compares all remaining parsed MOMENTUM command/state fields over the common
safe outbound interval. It excludes fixture-level rows and ends at the last
noncarrying sample. This establishes equality of **logged fields at their
recorded precision**, not equality of every hidden player-state field.

| fresh world | matching retained trace | common safe window after fixture | matching momentum rows |
| --- | --- | ---: | ---: |
| replay-1-r1 | mom-on-r1 | 19,000 ms | 760 |
| replay-1-r5 | mom-on-r3 | 19,250 ms | 770 |
| replay-1-r6 | mom-on-r1 | 19,000 ms | 760 |

Replay-off r1 also reproduces retained r3; replay-off r2/r3 reproduce retained
r1. Replay-on r2/r3/r4 match neither retained trace. They remain in the
report and are not used to identify a retained frame's reason. Finding
matching traces supports this frame correspondence; it does not establish
that replay instrumentation is generally inert or that the nine worlds are
independent samples.

Reproduce the report with a fresh output directory:

```powershell
$landingCases = Get-ChildItem -LiteralPath .codex-research/landing-investigation-20260920T0651 -Directory -Filter 'replay-*' | ForEach-Object { Join-Path $_.FullName 'red_out_L1_stand' }
python tools/movement_landing_report.py .codex-research/momentum-review-20260920-a/audit-v2.json --out .codex-research/landing-retained-NEW --compare $landingCases
python -m unittest discover -s tools -p test_movement_landing_report.py -v
```

The six focused tests cover same-client/frame extraction, duplicate-trace
handling, relative-clock comparison, unknown trigger classification, absence
of candidate evidence, and all four engine admission labels. No native policy
change is needed to produce these diagnostic reports.

## Four missing searches: the measured gate

| retained world / level | relative ms | position | speed | matching replay world / level | skip reason |
| --- | ---: | --- | ---: | --- | --- |
| r1 / 7925 | 4600 | 562.9, 349.3, 1808.1 | 445.7 | replay-1-r1 / 8125 | off_path_index |
| r1 / 13675 | 10350 | 1334.8, 2240.4, 1808.1 | 440.5 | replay-1-r1 / 13875 | off_path_index |
| r1 / 21400 | 18075 | 2434.9, 4948.3, 1912.1 | 458.6 | replay-1-r1 / 21600 | off_path_index |
| r3 / 19375 | 16100 | 2287.2, 4282.1, 1808.1 | 452.7 | replay-1-r5 / 19375 | off_path_index |

Every matched skip has `path_index=-1`, `ground=1022`, `permitted=0`,
`stage=4`, `before_stage=3`, `walking=1`, and `owned=1`; retry, search and
cadence delays are zero. These rows identify the path-index guard, not a
timer throttle or an admission verdict. The old logs contain no tap-now
SPIKE candidate on these four frames because the straight search was not
entered. Ordinary parity-forecast refusal elsewhere on the same frame is
not evidence that the missing candidate search ran.

## Admission and live progress disagree about a later chord

The [retained baseline source](../../../.codex-research/landing-investigation-20260920T0651/baseline-source/codemp/game/bot2_move.c)
`NearPathCell` recognizes cells and strafe chords
through its forward admission window, including a chord whose takeoff is
ahead of the live path index. `PathProgress` intentionally recognizes a chord
only if its takeoff index has already been reached (`i <= index`); its
fallback accepts nearby cells within 128 units. The sequence executor did
not advance that walking index during the flight. Consequently an admitted
sequence can end inside a later chord, far from its endpoints, and fail live
progress lookup on touchdown.

All four matched states have reconstructed path geometry from native path
telemetry. Distances below are horizontal; admission also checks the vertical
tolerance of twice `STEPSIZE` (36 units). The chord radius is 48 units.

| replay landing | path generation / chord indices | chord endpoints | lateral distance | nearest endpoint distance |
| --- | --- | --- | ---: | ---: |
| 8125 | gen3 / 1 -> 2 | (448,64,1808.125) -> (576,480,1808.125) | 25.953 | 131.350 |
| 13875 | gen5 / 2 -> 3 | (1216,2048,1808.125) -> (1472,2464,1808.125) | 0.376 | 226.090 |
| 21600 | gen6 / 7 -> 8 | (2432,4736,1881.525) -> (2432,5152,1912.125) | 2.927 | 203.758 |
| 19375 (replay-1-r5) | gen3 / 13 -> 14 | (2176,4000,1808.125) -> (2368,4448,1808.125) | 8.879 | 184.553 |

The first two sequences started with progress indices 0 and 1. For the third,
index 5 is inferred from the reported remaining path; it is not a separately
logged full executor-state capture. The fourth root's progress index is not
claimed as separately observed here. All four landings fit the later chord's
48/36-unit envelope, but the endpoints are beyond the 128-unit fallback.
That is the concrete disagreement between accepted landing geometry and live
progress bookkeeping. All four final native stage rows also report measured
landing coordinates equal to the corresponding forecast landing coordinates
at the logged precision; this is not evidence of flight drift causing the skip.

The bounded repair target is the final touchdown of an already committed
sequence when ordinary lookup fails: reconcile progress with the same forward
chord geometry used for admission. This does not imply that an ordinary
walking bot may join future flight chords from the side, or that admission
and hurt-volume guards should be weakened. Implementation checks and native
outcomes are recorded separately below when available.

## Eight searched frames: tap-now refusals are not one class

The targeted eight frames contain **52 zero-run-up candidates**: 23 are
`off_path` with `unsupported=0`, and 29 are `refused` with `unsupported=256`.
The latter bit is `BOT2_FORECAST_TRIGGER`; it identifies a trigger contact,
not the trigger entity or whether a route would fall. These logs alone do
not distinguish item, flag and hurt-trigger contacts for those candidates.

| retained world / level | position | speed | tap-now outcomes |
| --- | --- | ---: | --- |
| r1 / 11925 | 1010.0,1688.8,1808.1 | 391.4 | 8 off_path, mask 0 |
| r1 / 15050 | 1584.7,2565.2,1808.0 | 371.0 | 6 refused, mask 256 |
| r1 / 16800 | 1991.1,3032.0,1808.1 | 426.7 | 6 off_path, mask 0 |
| r3 / 8050 | 579.5,392.4,1808.0 | 449.7 | 6 off_path, mask 0 |
| r3 / 10700 | 603.5,1344.8,1808.1 | 382.4 | 6 refused, mask 256 |
| r3 / 14100 | 1528.4,2546.8,1808.1 | 546.6 | 8 refused, mask 256 |
| r3 / 17575 | 2147.1,3623.2,1808.1 | 442.5 | 6 refused, mask 256 |
| r3 / 21025 | 2412.0,4772.6,1891.4 | 313.1 | 3 off_path mask 0; 3 refused mask 256 |

By frame: three are entirely selected-path rejection, four entirely unknown
trigger contact, and one mixed. There are no admitted tap-now alternatives
in this selected cohort. The tool recognizes `admitted`, `arrives`, `rerouted`
and `tap_onward` as admitted outcomes, so alternate success labels are not
miscounted as refusals. The selected live commits instead use a 24-unit run-up.
Candidate forecasts are not executed counterfactuals; these counts alone do
not prove how much time changing an admission decision would save.

Keep the path-progress mismatch, selected-path admission rejection and unknown
trigger stops separate. The first has a concrete bookkeeping repair target;
the other two do not acquire a safe bypass from that diagnosis.

## Implemented correction and regression

`LandingPathProgress` now reconciles a missing path index at the final
touchdown of a committed sequence. It first preserves every successful ordinary
lookup. Only on failure does it recognize a later strafe chord in the same
48-cell, 48-unit horizontal / 36-unit vertical admission window. The hook is
inside `HopStep`, so forecasts and live execution use the same rule. Ordinary
walkers, intermediate hop landings and airborne states retain their prior
behavior. No candidate safety gate, trigger rule or policy cvar changed.

The actual-C [landing regression](../../../.codex-research/landing-progress-test-20260920T070000Z-dccd3f7b/result.json)
passes all four retained geometries, ordinary-walking exclusion and window,
height, lateral-distance and lifecycle boundaries. The exact preserved baseline
fails the repaired-index assertion. The reroute-ownership regression remains
green. A fresh dedicated engine/game build succeeded:
`landing-progress-build-f3ca16de29064fef9bdaa11317a6d2b5`, manifest
`20260920T065800858899Z.sources.json`. Native sources were frozen throughout
build and collection; installed-game files were not changed.

The benchmark now accepts an explicit `--source-root` for a frozen source
snapshot. Every source hash must still match the build manifest, as must staged
binaries. The summary records the chosen root. Four binding tests cover the
default checkout, valid snapshot, changed snapshot and incomplete snapshot.
This permits alternating an old build and a changed build without weakening
their bindings. All 22 existing benchmark tests remain green, as do five
momentum-audit and six landing-report tests.

## Eight alternating pairs: correctness is not a speed win

The [declared protocol](../../../.codex-research/landing-investigation-20260920T0651/protocol.md)
was followed for eight sequential baseline/fixed pairs, with the same seeded
unguided `red_out_L1_stand`, no spending, replay off, momentum trace on,
`bot_moveReroute=0`, `--edges --parity 256`, and timescale 5. All sixteen runs
completed. Each arm contains six distinct fixture-relative logged traces;
the repeated processes are not sixteen independent observations.

| Pair | Baseline ms | Corrected ms | Corrected minus baseline |
| --- | ---: | ---: | ---: |
| 1 | 20,175 | 20,675 | +500 |
| 2 | 19,150 | 20,675 | +1,525 |
| 3 | 19,150 | 18,275 | -875 |
| 4 | 19,600 | 19,475 | -125 |
| 5 | 19,600 | 19,650 | +50 |
| 6 | 20,700 | 20,675 | -25 |
| 7 | 19,150 | 20,675 | +1,525 |
| 8 | 19,150 | 18,275 | -875 |

Baseline median **19,375 ms**, mean **19,584.4**, maximum **20,700**;
corrected median **20,162.5 ms**, mean **19,796.9**, maximum **20,675**.
The corrected median is 787.5 ms slower and the mean is 212.5 ms slower.
Four pairs improve and four worsen. The 18,275 ms best is not convergence
or an accepted performance improvement; the approximately 15-second target
remains unmet. Retain the bookkeeping correction as a shared-model consistency
repair, not as a speed optimization or production qualification. Do not
promote any policy on the strength of its best run.

In the conservative full-500-ms in-leg windows, grounded landings with no
recorded straight search fall from **20 to 10** pooled (14 to 6 after trace
deduplication). But airborne sequence endings barely change: **21/91** to
**22/95** pooled (15/69 to 18/69 distinct). More search access has not reliably
become an immediate tap or a shorter trip. These cohorts occupy different
trajectories; their counts are descriptive, not matched-landing effects.

[Hash-bound timing and landing summary](../../../.codex-research/landing-investigation-20260920T0651/timing-summary.json).
Reproduce into a fresh output file with
`.codex-research/landing-investigation-20260920T0651/summarize.py`.

Two separate corrected-build replay diagnostics complete in 18,275 and
20,675 ms. Neither emits `off_path_index`. One reaches the original first
landing exactly at `(562.939,349.301,1808.125)`, speed 445.672, now with
`path_index=1`; it reports **`straight_too_short`** instead. The index repair
exposes a proposal-generation gate, not a guarantee of a viable immediate hop.
Its two recorded outbound skips are both `straight_too_short`; the other run
has no landing skips. Do not generalize zero off-path skips beyond these runs.

- [Corrected replay run 1](../../../.codex-research/landing-investigation-20260920T0651/fixed-replay-r1/result.json).
- [Corrected replay run 2](../../../.codex-research/landing-investigation-20260920T0651/fixed-replay-r2/result.json).

### Standard outcome floor

The [fresh corrected floor](../../../.codex-research/landing-investigation-20260920T0651/fixed-floor/result.json)
passes **36/36**, with zero deaths and only `move` ownership. It is the
standard unseeded case set, `--edges --parity 256`, with at most four isolated
worlds. These parallel outcomes are not timing evidence. There are 5,313
forecast refusals, zero comparison horizons and zero compared frames; parity
is therefore unqualified. Post-collection verification confirms every native
source hash still matches the fresh build manifest.

## Next

Measure the remaining short-straight proposal gate at the reproduced landing,
including the turn ahead and any native-validated tap continuation. Separately
identify the trigger entities for the refusal cohort before deciding whether
the simulator lacks a useful continuation or is correctly stopping for a
hazard. Keep the pit and unsupported-contact gates. Judge any proposal change
on complete sequential controlled runs, not fewer skipped searches alone.

Runtime scheduling, CPU budgets and parity qualification remain separate:
these paired runs record zero parity comparisons, so zero mismatches cannot
establish agreement between the live and forecast trajectories.
