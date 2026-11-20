# Ramp-entry continuity — 2026-09-12

Diagnosis and repair of the stopped ramp-entry transitions the
[T5 phase ledger](STRAFE_MOVEMENT_T5_PHASES_2026-09-12.md) ranks second, after
the [seam repair](STRAFE_MOVEMENT_SEAM_REPAIR_2026-09-12.md). Baseline for new
work is seam v3. Nothing here is installed and no acceptance rule changes.

Every artefact this record cites is indexed in
`.codex-research/ramp-entry-20260912/README.md`, including the retained
pre-change source, the three single-correction counterfactuals and the offline
reproductions.

## The reproducible interval

On the first seeded return the runner walks the flat approach at the ground cap
of 250 u/s, then brakes twice inside 96 units of the base ramp before
accelerating up it. The forty-hertz trace of `candidate-r1/R2-red` shows both:

| Level (ms) | Speed (u/s) | What the executor is doing |
|---|---|---|
| 17300 | 250.1 | approaching on the flat |
| 17700 | 48.2 | arrival at the turn vertex `(248.25,-305.75)` |
| 17825 | 118.8 | partial recovery over the next 30 units |
| 18025 | 47.4 | arrival at the ramp vertex `(244.5,-335.75)` |
| 18175 | 238.1 | climbing the ramp at the slope-limited walk |

Measured in a fixed 96-unit neighbourhood of each team's own turn vertex over
the four exact-12 cases and three repetitions
(`.codex-research/ramp-entry-20260912/baseline-ramp.json`):

| Case | Minimum u/s | Time below 150 u/s | Shortfall against the 250 u/s ground cap |
|---|---|---|---|
| R2-red | 47.4 | 0.625 s each repetition | 0.457–0.465 s |
| R7f12-red | 45.2–46.5 | 0.650 s each repetition | 0.475–0.477 s |
| R2-blue | 45.2–48.4 | 0.325 s each repetition | 0.252–0.261 s |
| R7f12-blue | 45.8–50.6 | 0.300–0.325 s | 0.241–0.258 s |

The shortfall is a kinematic diagnostic, not recoverable time: a turn may
legitimately require lower speed. It is reported so the repair can be judged
against something measured rather than against the absence of a stop.

## The responsible contract

`ProposePass` in `ai_bot2_supported.c` builds a bounded outside excursion
through a vertex the arc compiler refused, and `ValidatePass` checks it with
standing floor probes and inflated-hull sweeps. Three properties of that pair
produce the stops. All three are generic, not Kejim-specific, and all three
share one mistake: they treat a body on sloped ground as if the ground were
level.

**The swept corridor counts ground as obstruction.** The sweep uses the body
hull inflated 16 units to each side but keeps its bottom at the chord. The
inflation exists to cover a follower tracking off the line, and on level ground
it is a sound conservative envelope. On a slope it is not conservative, it is
wrong: the ground 31 units to the side of the line is higher than the chord, so
the box is solid before the body has moved. Both base ramps refuse at the very
first chord, `b -> pass[0]`, with `startsolid` and `allsolid` set at the vertex
itself - a prepared standing point the runner already occupies and walks over in
the same world. This is the refusal that actually fires at every ramp entry.

**Heights are inherited, so a rising exit drives later sweeps into the ground.**
`ValidatePass` probes only `pass[0]`, `pass[1]`, `pass[2]` and `pass[6]`. The
rejoin `pass[3]` then takes `pass[2]`'s probed height and the inside-cut chord
ends `pass[4]`/`pass[5]` take `pass[6]`'s. On a level corner those are the same
floor. On a ramp or a step they are not: the endpoint is placed below its own
floor, by up to 25.8 units at the measured vertices, and its sweep is blocked by
the ground it was meant to slide along.

**The elevation gate compares endpoint heights, so a gentle ramp reads as a
cliff.** `fabsf(c[2]-b[2])>18` refuses any leg long enough to climb 18 units
however shallow it is. The base ramp rises at 0.269 (about 15 degrees), well
inside what preparation already accepted for the same interval - preparation's
own rule is at most 18 units of height per sample and samples at most 32 units
apart, a ratio of 0.5625.

Together these make every ramp entry a stopped arrival. The executor is behaving
correctly: `Crossing` is false at a vertex with no `crossing` flag, no
`passSpeed` and a real heading change, so `ContinuousCommand` brakes to the
arrival law `(travel-12)*4` - which is exactly the 48 u/s measured 24 units out.

A refusal could not be attributed before this cycle, because `ValidatePass`
reported only its stage: `pass_clearance_or_hurt` reads the same for a wall, a
hurt volume and its own swept box sitting in the floor. It now reports which
probe or chord refused and what the trace answered.

Two further observations from the same traces, recorded because they explain
which vertices reach the proposal at all and neither is repaired here:

* `ProposeCorner` spans a short level bevel before testing elevation, so at the
  turn vertex it reports `elevation` while `ProposePass`, which always looks one
  point ahead, reports `straight` for the same vertex. The two reasons in one
  `corner_skip` line are consistent, not contradictory.
* The straightness test `dot > 1-1e-7` sits exactly one unit in the last place
  below one in this float width. At Kejim's blue-base coordinates the prepared
  subdivision `(2357.833,4452.167)` computes `dot` at precisely that value, so a
  geometrically straight vertex is classed as a turn and proposes a pass whose
  rejoin lands on the 1.833-unit step beyond it. Reproduced offline against the
  engine's own `VectorNormalize`; at red-base coordinates the same construction
  computes exactly one and is classed straight.

## Native evidence that the maneuver is feasible

Two bounded probe worlds on the frozen seam-v3 build, through the existing
`bot_mapbench` fixture (`tools/strafe_ramp_probe.py` derives the points from the
production proposal; `strafe_map_probe.py prepare-explicit` binds them to the
BSP; `tools/strafe_ramp_probe_report.py` joins the answers back):

* `.codex-research/ramp-entry-20260912/{declaration,request,derivation,report}.json`
  and `native/` — the turn vertex, both directions, plus the floor under every
  preparation sample of the five raw edges either side of both base ramps.
* `.codex-research/ramp-entry-20260912/*-slope.json` and `native-slope/` — the
  ramp vertex one point further, both directions and both bases, derived under
  the proposed admission rule.

The probed floors reproduce the retained supported node heights exactly
(1808.250, 1808.458, 1816.583, 1810.083), which binds the probe to the traces.
Each chord was swept at the centre line and at four 16-unit tracking offsets,
standing on its own floor; the offsets stand in for the production sweep's
inflated hull, which exists to cover a follower tracking off the line.

| Vertex | Worst inherited deficit | Chords clear on their own floors |
|---|---|---|
| Red turn `(248.25,-305.75)` | 6.470 units | 25/25 |
| Red turn, descending | 6.470 units | 24/25 — outward chord at one offset, fraction 0.061 |
| Red ramp `(244.5,-335.75)` | 25.837 units | 25/25 |
| Red ramp, descending | 8.131 units | 25/25 |
| Blue ramp `(2382,4471.75)` | 21.021 units | 24/25 — cut chord at one offset, fraction 0.406 |
| Blue ramp, descending | 6.844 units | 24/25 — cut chord at one offset, fraction 0.145 |

So a real body, standing on its own floor and tracking up to 16 units off the
line, crosses both red vertices in the direction the return uses. The blue ramp
vertex and the red turn taken downhill each have one genuinely obstructed
offset. Offset player hulls are not the production sweep; they bound the
question, they do not answer it, and the corrected build's own validation is
what decides below. A refusal it keeps is reported as a refusal.

## The correction

Three bounded changes in `ai_bot2_supported.c`, all geometric and none touching
the executor, the crossing gates, the branch guard or resource admission. Each
has a check case that fails without it; the three counterfactual sources and
their failing assertions are retained beside the check.

1. **Every swept endpoint stands on its own floor.** `ProposePass` carries the
   interpolated height of the leg each point sits on - the exit leg for
   `pass[2]`, `pass[3]` and `pass[5]`, the entry leg for `pass[4]`, the vertex
   height for the two off-path excursion points - and `ValidatePass` probes all
   seven points against their own declared height with the unchanged 18-unit
   window, then sweeps between the probed centres. The inherited assignments are
   removed. This costs three more traces per proposal, 12 of the 32-trace frame
   budget rather than 9.
2. **A leg is admissible as a step or as a walkable ramp.** The elevation gate
   becomes `rise <= 18 || rise <= 0.5625 * length` per leg, the ratio
   preparation already applies to the same interval. Drops are bounded the same
   way as climbs, and the per-point probes above are what keep the swept corridor
   on real ground.
3. **The swept corridor asks about obstruction, not about ground.** The sweep
   box, inflated 16 units to each side, kept its bottom at the chord, so ground
   rising beside the line entered it and the corridor reported solid before the
   body had moved. Its bottom is now offset by `STEPSIZE`, the same idiom the
   engine's own path-clearance check uses. Walls, pillars, low ceilings and hurt
   volumes still refuse; the hazard inventory keeps the full body box, because a
   hurt volume flush with the floor is exactly what a raised box would miss.

The third correction is what the ramp entries were actually refused on. Both
red vertices refused at the very first chord, `b -> pass[0]`, with `startsolid`
and `allsolid` set at the vertex itself - a prepared standing point the runner
already occupies - and both blue vertices did the same. That is visible now
because `ValidatePass` reports which probe or chord refused and what the trace
answered; before, every refusal read as `pass_clearance_or_hurt` and was
indistinguishable from a wall. The first two corrections are still required:
each has a case that fails without it, and the inherited heights would have
refused the rejoin chord on the very next sweep.

Everything else is unchanged: the straight, angle, speed-floor, excursion, cut,
exit and entry span gates, the outward degeneracy cutoff the seam repair set,
the local run-up publication that preserves upstream hop eligibility, and the
native traces that must all pass before a vertex becomes a crossing.

## Checks

`tools/strafe_seam_check.py` extracts the production proposal and validation and
compiles them against a declared floor plane: a downward probe answers with that
plane's height under its own position, and a sweep is blocked when the swept box
overlaps ground or an obstruction beside the chord. It covers mirrored shallow
seams, all twelve floor and hull refusal points, hurt volumes, short legs,
stepped exits, gentle ramps up and down, grades and cliffs that stay refused,
ledges, steppable and unsteppable ground beside the chord, exact straight legs,
and real and excessive turns. Mocked traces establish gate execution and height
bookkeeping, not native clearance.

Each correction is shown to be necessary by reverting it alone. The variants are
rebuilt from the current source by
`.codex-research/ramp-entry-20260912/sources/counterfactuals.py`, which refuses
if a correction it reverts has moved:

| Reverted correction | Assertion that fails | Check output |
|---|---|---|
| Per-point heights (`counterfactual-assignment.c`) | `ValidatePass` on the stepped exit | `cf-assignment/` |
| Step-or-slope admission (`counterfactual-absolute.c`) | `ProposePass` on the gentle ramp | `cf-absolute/` |
| Raised sweep box (`counterfactual-flatbox.c`) | `ValidatePass` with steppable ground beside the chord | `cf-flatbox/` |

`tools/strafe_supported_wait_check.py` and `tools/strafe_fast_return_check.py`
pass unchanged against the final source, protecting the local run-up
publication, preserved hop metadata and radius, previous-pass rejoin protection,
and the prior corridor, branch and tap contracts. The full Python suite passes:
468 tests. All 1104 measured sources and both binaries still match the scored
build, so the arm above remains bound to the source it was collected from.

## Retained intermediate builds

Two builds precede the measured one and are retained, not scored.

`.codex-research/strafe-build-ramp-13fcc593b82f4f43b70a3daa98d21b19` carried the
per-point heights and the step-or-slope admission only. Its four-case preflight
(`preflight/`) captured on both teams, but both ramp vertices still refused with
`pass_clearance_or_hurt`, which is what showed the first two corrections were
not the whole cause.

`.codex-research/strafe-build-ramp-diag-ce76e78b7efb4a18ad15a1247692c7cb` added
the refusal telemetry to that same policy. Its preflight (`preflight-diag/`) is
the evidence that every ramp refusal is sweep index 0, `b -> pass[0]`, with
`startsolid` and `allsolid` set at the vertex. The telemetry is in the measured
build too; only the swept-box correction separates them.

`preflight2/` is the four-case preflight of the measured build: all captured, no
deaths, health floor 90, both ramp vertices accepted in both directions on both
bases, and no pass refusal of any kind in the four worlds.

## Frozen comparison

- Build: `.codex-research/strafe-build-ramp2-9d65038df4f042d88231215dec09be82`.
- Declaration, drivers, worlds and audits: `.codex-research/ramp-entry-20260912/v1`.
- Baseline: `.codex-research/seam-fix-20260912/v3/candidate-r{1,2,3}`.
- Unchanged thirty-route assets: `.codex-research/pkgD-step3-20260911/assets`.
- Same twenty cases, three repetitions, timescale 5, 55-second observation, at
  most four isolated native worlds. All 1104 measured sources and both binaries
  match the build manifest (`v1/verification.json`); nothing changed during
  compilation or collection.
- A four-case preflight ran first on both teams; all captured, no deaths, health
  floor 90. It is separate from the three complete repetitions.
- Ramp diagnostic: supported-owned first-return samples within 96 XY units of
  each team's own turn vertex. A fixed local diagnostic, not an acceptance
  threshold, and each team keeps its own vertex because the two base ramps are
  near-mirrored rather than identical.

## Results

| Twenty-world totals | Seam v3 r1/r2/r3 | Ramp r1/r2/r3 |
|---|---|---|
| Captured worlds | 20/20/20 | 20/20/20 |
| Deaths / falls | 0/0/0 | 0/0/0 |
| Minimum sampled z | 1808.004/1808.004/1808.005 | 1808.003/1808.004/1808.005 |
| Health floor | 90 | 90 |
| Localnav frames | 6/4/4 | **60/63/62** |
| Stationary samples | 52/66/65 | 41/53/44 |
| Zero-input samples | 138/119/123 | 124/134/128 |
| All-reason rapid churn | 0/0/0 | 0/0/0 |
| Branch entries | 42/44/50 | 42/47/44 |
| Entries open at world end | 3/7/10 | 1/2/2 |
| Gradable resource releases on a low jump | 0 | 0/0/0 |
| Unpaired resource releases | 0 | 0/0/0 |
| T5 met / declared denominator | 0/8 each | 0/8 each |

The diagnosed stops are gone. In the ramp neighbourhood the twelve exact-12
first returns now hold a minimum of 237.2–241.5 u/s with no sample below 150,
against 45.2–50.6 u/s and 0.300–0.650 s below 150 before. Both ramp vertices are
accepted passes in both directions and on both bases, and no pass proposal is
refused anywhere in the preflight worlds.

| Exact-12 first return | Baseline seconds r1/r2/r3 | Ramp seconds r1/r2/r3 |
|---|---|---|
| R2 red | 19.050/19.000/19.000 | 18.500/18.425/18.550 |
| R2 blue | 18.900/18.875/18.900 | 18.750/18.575/18.750 |
| R7f12 red | 18.250/18.250/18.975 | 17.800/18.475/18.475 |
| R7f12 blue | 18.000/18.150/18.225 | 18.000/17.750/17.750 |

Ten of the twelve pairings are faster, one is unchanged and one is slower; the
two R7f12 ranges overlap. Every return remains above 16 seconds and **T5 remains
0/8 in every repetition**. This is a resolved local stop and a consistent small
travel gain, not a travel-time qualification.

The seam repair is retained: seam minima 250.0–255.9 u/s with no below-150
sample in any repetition, and the one 185.9 u/s baseline outlier does not recur.
The corridor repair and authored-tap admission hold: no lower-floor departure,
no branch abandonment, no low-jump resource refusal among 30/33/31 gradable
releases, and no unmatched release. Open final branch instances remain censored,
not successful transitions. Regressions: 26/26 temporary-body cases passed and
30/30 resource cases completed without refusal; these subsets retain
`milestone_pass=false` for their broader gates.

## The one regression, attributed

Localnav frames rise from 4-6 to 60-63 per twenty worlds. Every one is the same
event, and none is on a measured return. The runner is seeded at a base corner
and descends the bank at 430-440 u/s; a newly published outbound pass vertex is
crossed while it is still well above the vertex height, so the strict pass
contract in `Bot2_PathCommand` refuses - a pass must be crossed on the ground,
near the vertex height, inside its swept excursion - and supported ownership is
released with `preparing_interval_contract`. Localnav holds the frame for 0.075
to 0.5 seconds and supported re-acquires. The largest case is R4-red at 20
frames; the rest are 2-6.

The gate is behaving correctly and is not relaxed here. What the trace shows is
that a vertex published while preparation is still streaming can be reached
before the braking law has room to meet its cap: closing from 400 to 330 u/s
needs about 32 units of warning. The bounded next correction is to stop
publishing a pass the runner cannot be given that room for, or to degrade a
refused pass to the vertex's ordinary arrival rather than releasing ownership.
Neither is attempted in this cycle, because neither has its own evidence yet.

## Status

Implemented, checked and measured against seam v3. Nothing is installed; the
installed baseline remains `reviewready-b` / `525f1b50`. T5, T2/T4 and the
defended T6 obligations are unchanged and open. The remaining T5 loss is the
long ground and ramp travel the phase ledger ranks third: the runner climbs the
ramp at 237 u/s and crosses the upper flat at 250 because the continuous
controller walks whenever checked hop room is below 320. That is the next
supported cause, and the localnav attribution above is the bounded repair to
fold in with it.
