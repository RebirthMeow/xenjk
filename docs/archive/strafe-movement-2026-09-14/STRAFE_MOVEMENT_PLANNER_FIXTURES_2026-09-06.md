# General planning: native fixture development

This implements the first experimental steps in the
[general planning design](STRAFE_MOVEMENT_GENERAL_PLANNING_2026-09-06.md), within
M3a-B/C. Production movement and the installed game are unchanged. These are
static-world development results, not completion of the planning program.

## Geometry and contract

The [planner contract](STRAFE_MOVEMENT_PLANNER_CONTRACT_2026-09-06.md) describes
complete movement roots, executable feedback maneuvers and useful onward exits.
`tools/strafe_planner_geometry.py` generates parameterized flat, ramp-to-flat
and two-gap/90-degree-turn collision maps. It emits support/exit descriptions,
alternate waypoint subdivisions, compiler and artifact hashes. Support in the
description is analytic; native execution must validate actual geometry.

All three default maps compile sealed in
`.codex-research/planner-geometry-smoke-v2-20260906/`. The v1 outputs remain.
Native controls on the v2 ramp and gap maps are in
`.codex-research/planner-native-geometry-controls-20260906/`: `ramp-v2`,
`gap-local-v2`, `gap-continuation-v2`. All three have 160 matching forecast/live
steps. The ramp walks to X686 on its upper floor. The old gap experiment still
selects local candidate0/failure versus continuation candidate5/exit at3600ms.
This is a regression of the old endpoint contract, not a new planner result.
Three initial launches used obsolete map filenames and failed before starting
a native world; their partially created output directories are retained.

## Waypoint representation experiment

The movebench extension accepts immutable corridor points. Both arms call the
unchanged `Bot2_HopCommand`; one handles every vertex, the other removes only
forward collinear vertices before execution. Slope changes and turns remain.
This is a primitive movement reference in a controlled corridor, not the full
production navigation adapter or a general corridor-safety algorithm.

The first adapter incorrectly advanced at16 units, while the hop controller
stopped roughly17–19 units away. Its sequential stalls invalidate its speed
contrast. The source/captures and `INTERPRETATION_CORRECTION.json` remain in
`.codex-research/strafe-corridor-matrix-20260906/`. The initial auditor also
compared decoder tuples directly with JSON arrays; the corrected v2 audit
canonicalizes serialization. Neither failure was erased or counted as a win.

The corrected adapter uses production supported traversal's actual handoff:
distance<24 XY, |height difference|<32, horizontal speed<80, grounded. Fresh
build: `.codex-research/strafe-build-corridor-handoff-20260906`. Its frozen matrix
is `.codex-research/strafe-corridor-handoff-matrix-20260906/`; final independent
audit is `.codex-research/strafe-corridor-handoff-matrix-audit-20260906.json`.

There are 24 positive native episodes: sequential/coalesced, 1/2/4/8 segments,
and initial velocities (0,0), (0,-50), (0,+50). All 3,840 native steps pass the
ordinary-movement forecast, rolling-feedback and recorded-command contract.
All three additional forecast-bias controls are rejected. Build sources,
staged binary/map bytes, actual initial velocities and raw capture decoding
are checked. Full player-state parity still excludes the two previously
documented server bookkeeping fields and is reported separately as failing.

Within each entry state, all four coalesced variants have identical decoded
commands, movement fields and policy states after clock normalization. This
is not full-player-state equality between independent processes. All twelve
coalesced runs pass grounded entry and onward traversal at useful velocity.

| Nominal representation | Useful onward passage | Full4s idle | Full4s walking |
|---|---:|---:|---:|
| One segment, either arm | 1.850s | 1.400s | 1.250s |
| Two sequential segments | 2.600s | 0.675s | 2.025s |
| Four sequential segments | 3.050s | 0.250s | 3.750s |
| Eight sequential segments | No qualifying passage within4s | 0s | 4.000s |
| Two/four/eight coalesced segments | 1.850s | 1.400s | 1.250s |

The eight-segment reference reaches X593 but fails the declared useful-velocity
passage contract; do not call that failure an inability to reach X576. The
faster controller spends more of the remaining fixed window idle near its
destination. Both raw window and travel-prefix metrics are retained. Nominal
coalesced travel to onward passage has zero idle and0.500s walking. This
isolates the cost of unnecessary intermediate arrivals without interpreting
every second of legitimate arrival waiting as a movement defect.

## First geometry-derived maneuver proposal

`bot2_maneuver.c/.h` supplies a pure experimental policy with explicit geometry,
candidate parameters and caller-owned20-byte movement state. It derives
takeoff planes from support bounds, applies ordinary jump pulses, steers in
air using projected landing error, and drives through an onward boundary
before braking. Eight predeclared proposals vary launch margin12/24/36/48
and first-flight turn anticipation0.35/0.60. There are no map-name checks,
embedded fixture coordinates or hand-selected jump/turn timestamps in this
policy. The initial experiment restricts support tops to equal height.

The native fixture evaluates all eight from the same captured full root, with
the same four-second horizon/1,280-call allowance for both scorers. A shared
neutral25ms initial tick is explicit: selection occurs at the first Pmove
boundary, after command receipt, so the first command cannot depend on which
candidate wins. Candidate failures continue through a recorded failure/braking
phase rather than becoming successful 'unsupported' exclusions. Forecast and
live execution regenerate the same commands. This first experiment uses one
initial decision; the later [actual-state reselection probe](STRAFE_MOVEMENT_REPLAN_PROBE_2026-09-06.md)
adds a bounded second decision. Affordable online planning and reuse remain open.

Fresh build: `.codex-research/strafe-build-maneuver-geometry-20260906`.
Initial nominal pair: `.codex-research/strafe-maneuver-geometry-pilot-20260906/`.
All eight candidates complete the prescribed onward traversal; local and
continuation both choose candidate7. Native first-platform acceptance occurs
at1.250s, useful onward passage at2.825s, with safe retained second-platform
support through the4s horizon. Both selected live episodes match all160
forecast steps. This does not distinguish the scorers; the simpler rank works
on this nominal case too.

The first Python audit mistakenly demanded4000ms between the first and last
post-step endpoints, which span3975ms. Duration is4000ms from the capture root.
The original reports remain; `audit-v3.json` verifies all native candidate
scores and selection with the corrected clock test and retained auditor source.
The correction changes no native trajectory or traversal criterion.

Search was instrumented, including world hashing/copies and candidate capture;
the observed86.7/97.0ms is not a clean production timing measurement and does
not meet deployment cost. No affordable live-planning claim is made.

## Unchanged-policy geometry and entry-state matrix

Frozen declaration and native captures:
`.codex-research/strafe-maneuver-matrix-20260906/`.
Independent audit: `.codex-research/strafe-maneuver-matrix-audit-20260906.json`.
Post-collection behavior analysis:
`.codex-research/strafe-maneuver-matrix-behavior-20260906.json`.

The unchanged maneuver kernel was tested on four geometry combinations, each
with initial horizontal velocity (0,0), (0,-50) and (0,50). Both scorers receive
the same eight candidates. All 24 positive runs pass 160 native steps in all
four movement parity contracts, totaling 3,840 steps; the extra biased forecast
is rejected. Paired candidate outcomes match between scorers.

| Geometry | Local first-landing useful exits | Continuation useful exits |
|---|---:|---:|
| Nominal | 2/3 | 3/3 |
| Wider gaps | 0/3 | 3/3 |
| Shorter, narrower platforms | 3/3 | 3/3 |
| Mixed gaps, narrow platforms | 3/3 | 3/3 |
| Total | 8/12 | 12/12 |

The three wider-gap local choices land short of the second platform and enter
the failure phase. The nominal negative-Y local choice reaches the onward end
but violates the second-platform hull inset, so passage alone does not pass.
All candidate failures remain in the captures and audit. These are held-out
parameter combinations for this kernel, not arbitrary geometry or a second
game map.

Across the twelve continuation runs, the fixed four-second windows contain
27.40% idle, 38.80% walking, 1.30% fast grounded travel and 32.50% airborne time.
The local arm has 31.09% idle and 34.95% walking. These raw windows include
post-arrival waiting and failed local outcomes. Within the continuation travel
prefixes ending at useful passage, idle is 0.29%, walking 52.09%, airborne
45.78%. Horizontal speed below 20 defines idle; 20 through 299 defines walking
while grounded. The two arms' attained-only prefix sets differ (12 versus 8),
so their aggregate prefix percentages are not a matched speed comparison.
Facing error during movement averages about 33 degrees in both arms; it is
retained as a diagnostic, without an acceptance claim. This establishes useful
traversal choice before high-speed strafe optimization.

Actual-state reselection now passes the separate probe linked above. Next:
affordable reuse and its retained-policy comparison. Automatic extraction from a game navmesh,
unequal-height maneuver composition, moving bodies, resources and second-map
transfer remain unimplemented/unqualified.
