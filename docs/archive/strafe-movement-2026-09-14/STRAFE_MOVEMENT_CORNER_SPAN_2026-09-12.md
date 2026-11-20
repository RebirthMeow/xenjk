# Spanned-bevel corner continuity — 2026-09-12

Reattribution of the remaining supported ground travel the
[continuation plan](STRAFE_MOVEMENT_NEXT_EXECUTION_2026-09-12.md) puts third,
and repair of the largest cause it found. Baseline for new work is the
[ramp-entry arm](STRAFE_MOVEMENT_RAMP_ENTRY_2026-09-12.md). Nothing here is
installed and no acceptance rule changes.

Every artefact this record cites is indexed in
`.codex-research/corner-span-20260912/README.md`, including the three retained
intermediate builds and their preflights, the counterfactual sources and the
attribution the diagnosis was made from.

## What the retained traces attribute the ground travel to

`tools/strafe_hop_attribution.py` rebuilds each published plan from its retained
`[SUPPORTEDTRACE] node` lines and replays `Bot2_PathCommand`'s hop-room
accumulation against the forty-hertz trace, so every grounded supported frame
carries the reason the hop gate refused it. Over the ramp arm's twelve exact-12
worlds, split by whether Force Speed was still running:

| Force Speed active | Frames | Seconds | Mean u/s |
|---|---|---|---|
| `room_cut_by_goal` | 596 | 14.900 | 260.4 |
| `sample_not_hopping` | 306 | 7.650 | 420.6 |
| `room_cut_by_sample` | 134 | 3.350 | 423.3 |
| `room_cut_by_pass` | 60 | 1.500 | 420.9 |
| `hop` | 50 | 1.250 | 484.8 |
| `pass_vertex` | 19 | 0.475 | 390.4 |

| Force Speed expired | Frames | Seconds | Mean u/s |
|---|---|---|---|
| `sample_not_hopping` | 996 | 24.900 | 237.9 |
| `room_cut_by_pass` | 436 | 10.900 | 254.1 |
| `room_cut_by_sample` | 302 | 7.550 | 251.7 |
| `pass_vertex` | 167 | 4.175 | 254.2 |
| `hop` | 12 | 0.300 | 269.8 |

Placement is the play track's own interpolation and the plan index is a
nearest-edge placement, not the executor's `state->index`; residual medians are
0.05–0.13 units and maxima 9.2–12.3, and no sample went unplaced. A frame
labelled `hop` is one the gate would have admitted at that position.

Three things follow, and only the first is repaired here.

**The largest Speed-active cause is one vertex pair.** 592 of the 596
`room_cut_by_goal` frames sit on a single 315.6-unit edge -- 299 on red's copy
of the route corner, 293 on blue's -- and the other four on the 8.4-unit stub
past it. It is not merely walked: the runner brakes to a near stop there.

**The largest Speed-expired cause is the base ramp, and repairing it would buy
very little.** 714 of the 996 Speed-expired `sample_not_hopping` frames are the
two base ramps, which preparation refuses to hop because `Prepare` clears the sample's
hop flag whenever a floor sample differs from the previous one by more than one
unit -- a flatness test, not a jumpability test, on ground the same preparation
accepts for walking at up to 18 units per 32-unit sample. But the measured value
of a hop there is small: post-expiry, the same worlds hop at 250–290 u/s against
237–250 u/s walking, because Force Speed is what makes a hop fast on this
geometry. The ceiling is about 0.13 s per ramp. **Corrected 2026-09-12: that
ceiling is not established.** Both speeds above are means over *grounded*
supported frames -- `strafe_hop_attribution.py` skips every frame whose trace row
is airborne, and a frame it labels `hop` is one where the gate would have
admitted a hop at that position, not one where the runner left the floor. So the
comparison is between two sets of ground frames, and the speed a hop actually
reaches in flight, which is what a hop would buy, is the quantity the instrument
excludes. The flatness gate and the missing clearance evidence stand; the 0.13 s
is an unmeasured estimate and not a measured ceiling. It is also the case that the
100-unit tall-envelope sweep is skipped on exactly those samples, because the
elevation clear turns it off before it runs, so there is no native clearance
evidence for a ramp hop yet. Recorded, not attempted.

**T5 is not reachable on these cases by hop room at all.** The exact-12 cases
seed the runner with 12 Force and one 13.275-second Speed window; Force only
regenerates after Speed expires and reaches 25–40 by capture, against the 50 a
level-3 Speed costs. The final 4.5–5.5 seconds at the 250 u/s ground cap are the
declared resource state of the case, not a controller defect. What movement
work can still buy is distance covered *before* expiry -- which is what the
repair below does.

## The reproducible interval

On each team's first seeded return the runner crosses the mid-map corridor at
the 425 u/s Force Speed ground cap, brakes to a near stop at the corner where
the corridor turns north, and re-accelerates. In 46 of the ramp arm's 60 worlds
the runner drops below 150 u/s there. Measured in a fixed 400-unit
neighbourhood of the vertex over the four exact-12 cases and three repetitions
(`.codex-research/corner-span-20260912/v2/baseline-r*-bevel.json`):

| Case | Entry u/s | Minimum u/s | Exit u/s | Time below 150 | Shortfall against the 425 u/s corridor |
|---|---|---|---|---|---|
| R2-red | 428.4–429.6 | 48.7–49.5 | 554.8–561.3 | 0.425–0.450 s | 0.477–0.483 s |
| R2-blue | 429.2–433.6 | 47.8–48.7 | 541.3–559.0 | 0.450 s | 0.478–0.489 s |
| R7f12-red | 428.4–428.6 | 45.6–46.1 | 555.1–555.4 | 0.450 s | 0.492–0.500 s |
| R7f12-blue | 429.0–429.7 | 47.0–48.7 | 551.2–553.1 | 0.425–0.450 s | 0.475–0.477 s |

The shortfall is a kinematic diagnostic, not recoverable time: a turn may
legitimately require lower speed. It is reported so the repair can be judged
against something measured rather than against the absence of a stop.

The vertex is a route asset mirrored exactly between the teams by the track
join: red's `(634.5,1430.5)` is blue's `(1989.5,2729.5)` half-turned about
`(1312,2080)`. It is followed 8.4 units later by `(627,1426.75)`, and the two
together turn the corridor through about 62 degrees -- 5 degrees at the first
vertex and 57 at the second. Neither is near either base, and neither lies in
the seam or ramp neighbourhoods the two previous cycles measured, which is why
neither of them saw it.

## The responsible contract

`ProposeCorner` in `ai_bot2_supported.c` already knows that a bend can arrive as
a short bevel. Before testing anything it spans one:

```c
while(e<sourceCount-2 && e<i+3 &&
      Distance(path->plan.points[e].point,path->plan.points[e+1].point)<64 &&
      fabsf(path->plan.points[e+1].point[2]-b[2])<=1)e++;
c=path->plan.points[e+1].point;
```

`sourceCount` is the prepared frontier, not the path. `PrepareCorners` compiles
a vertex as soon as one source point beyond it exists (`cornerIndex<sourceCount-1`),
but this span may reach three vertices further and then reads the point after
that -- five source points in all. On a streamed return the vertex is therefore
judged against the end of the prefix that happens to be prepared, and the
retained telemetry shows exactly that: the red vertex was compiled at level 9725
while raw point 6 was still being prepared at 9950.

With the span cut there, the 8.4-unit bevel becomes the exit leg, and both
proposals refuse on it and only on it:

* `trim=min(128,min(la,lb)*.4f)` is `min(128, 8.4*0.4)` = 3.36, below the
  16-unit floor: `corner_reason=trim`.
* `ProposePass`'s `speed=min(speed,PASS_YAW_RATE/16*min(la,lb)*min(la,lb))` is
  `0.375*8.4²` = 26.5, below the 100 u/s walk floor:
  `pass_reason=speed_below_walk_floor`.

Both are correct answers about an 8.4-unit exit leg. Neither is an answer about
the vertex, whose real exit leg is 1533.8 units long. And the refusal is
permanent: `cornerIndex` only advances, so nothing reconsiders the vertex when
the rest of the path arrives. With the tail present the same span reaches the
bevel end and offers a 128-unit trim.

The executor is behaving correctly downstream of that. `Crossing` is false at a
vertex with no `crossing` flag, no `passSpeed` and a real heading change, so
`ContinuousCommand` brakes to the arrival law `(travel-12)*4`.

## Native evidence, and what it refused

With the span deferred until the tail exists, the first intermediate build
(`strafe-build-ramp…span2`, `preflight2/`) proposes the arc and
`ValidateCornerSample` **refuses it**, at sample 8 of 16 on red and 9 of 17 on
blue, with `tracking_body_floor`: one of the four 12-unit tracking offsets under
the arc's deepest point finds no flat floor within a unit of the centre's. A
128-unit trim cuts further inside the corner than the corridor has room for.
That is the native geometry's answer and it is kept.

The same build also showed what the legs alone cannot: asked for one narrower
arc on the same span, the corridor accepts it. So the trim rule
`min(128, min(la,lb)*.4f)` bounds the arc by the legs and nothing bounds it by
the corridor, and only the native samples know the difference.

The second intermediate build (`span3`, `preflight3/`) is the evidence for the
fallback order and for one defect the first ordering hid, both recorded under
the corrections below.

## The corrections

Six bounded changes, five in `ai_bot2_supported.c` and one in the shared
executor. None touches the crossing gates, the branch guard, resource admission
or any acceptance rule. The last three exist because the first three produced
costs the intermediate builds measured; each is recorded with the build that
found it.

1. **A vertex is judged once, on its final span.** `ProposeCorner` reports
   whether its span stopped at the prepared frontier while the geometry would
   have continued (`cornerTruncated`), and `PrepareCorners` yields the frame
   instead of answering the vertex when it did and the path is still streaming.
   Nothing is published earlier by compiling early: `curved` replaces the plan
   only when preparation completes, so the deferral costs only the order the
   compiler does its own work in.
2. **A natively refused arc still gets the bounded pass.** Before this cycle a
   bevel vertex reached `ProposePass` because `ProposeCorner` had refused it on
   the truncated span. Once the span is correct the arc is proposed instead, and
   the native-refusal path kept the vertex outright -- which lost the seam
   repair's own pass at `(462,-107)` and dropped that neighbourhood's minimum
   from 250 to 161 u/s in `preflight2/`. Both refusals now reach the same
   fallback, and the pass is asked first: a pass is a bounded excursion at
   corridor speed, a narrowed arc is a slower rounded corner.
3. **A vertex with no pass gets one narrower arc.** When the pass has nothing to
   offer, the same span is re-proposed with the trim halved, once. Only refused
   corners pay for it, and `ProposeCorner`'s own gates -- radius floor, angle,
   elevation, the 16-unit trim floor -- still decide whether the narrower arc
   exists at all, and `ValidateCornerSample` still decides whether it clears.
4. **The executor names its refusal.** `Bot2_PathCommandReported` fills a
   caller-owned `bot2_pathRefusal_t` with the clause that refused, the index,
   the speed and the height above the vertex, and the supported owner prints it
   on the release line. A release read the same before for a stale index, a
   missed pass envelope, a broken turn envelope and a stalled follower. The
   report is out of the follower state deliberately: `bot2_pathState_t` is the
   connection bench's recorded `extra` blob and a static check binds its size to
   `bot2_transitionContinuation_t`; growing it fails the build, which is how
   this was found.

5. **A refusal is settled, so a starved frame resumes at the fallback.** The
   fallback can run out of the frame's trace budget, and the loop then rebuilt
   the arc from sample zero and refused it again at the same sample, for good:
   both red worlds of `preflight3/` never finished preparation and stopped dead,
   2.3–2.4 s below 150 u/s and 3–4 s slower to capture. A native refusal is an
   answer about the vertex, so it is recorded (`cornerKeeping`) and the loop
   resumes inside the fallback rather than at the proposal.
6. **An airborne prefix refusal declines the frame instead of the path.** Every
   measured `preparing_interval_contract` release is a seeded runner airborne at
   345–757 u/s crossing a published vertex it is 3–9 units below. The prefix's
   contract is a ground contract, so a runner still in the air cannot meet it
   however good the path is, and discarding the path throws away preparation
   that has not finished. A grounded refusal still releases: that one is about
   the path. The measure is in `preflight5/` -- the blue localnav windows halve,
   19 frames to 9.
7. **The acquisition coast lasts while the compiler is deliberately waiting.**
   The deferral holds the compiler for as long as the following edge takes to
   sample -- 650 ms on R1-red, whose path begins at the bevel -- and publishes
   nothing for the prefix to follow. The 250 ms coast covered part of that and
   the runner stood still for the rest: 12–17 stationary samples per world in R1
   and R3, both teams, every repetition of `v1/`. The coast's own gates -- ground
   contact, the 80 u/s floor, agreement with the runner's velocity, the forward
   floor probes and the walk cap -- are re-checked every frame it covers, and
   preparation's 3 s timeout still bounds the wait. In `preflight6/` the
   stationary count is 1 and R1-red's capture is 16.875 s.

The numbering above runs past four because the fifth, sixth and seventh
corrections are answers to what the intermediate builds measured, not part of
the original design.

## Checks

`tools/strafe_corner_span_check.py` compiles the production `ProposeCorner` and
asks it about the retained geometry twice: with the tail truncated where the
traces truncated it, and with the tail present. It covers the mirrored scene,
the truncation report firing only when the frontier cut the span and not when
the geometry did, the bounded span, and the unchanged straight, angle,
elevation, trim and ordinary-corner gates. It also models the caller's rule both
ways, so the streamed vertex is shown to be skipped without the correction and
to compile an arc above 400 u/s with it. No trace is mocked: the proposal makes
no world queries, so this covers span and gate execution, not native clearance.

`tools/strafe_supported_wait_check.py` drives the real compiler and the real
owner against mocked proposals, native samples and executor results, and now
covers the refused-arc fallback and the airborne refusal: that a refused arc
with a pass available takes the pass and never narrows, that one without takes
exactly one narrower arc on the same span, that a frame which runs out of traces
inside the fallback resumes inside the fallback rather than re-validating the
arc, and that an airborne prefix refusal keeps the path while a grounded one
releases it.

Each correction is shown to be necessary by reverting it alone
(`.codex-research/corner-span-20260912/counterfactual.py`, which refuses if the
text it reverts has moved):

| Reverted correction | Check that fails | Assertion |
|---|---|---|
| The truncation report (`counterfactual-truncation.c`) | `strafe_corner_span_check` | `p.cornerTruncated` on the streamed bevel |
| The refused-arc fallback (`counterfactual-fallback.c`) | `strafe_supported_wait_check` | `p->cornerRejected==1 && p->passAccepted==1` |
| The airborne prefix refusal (`counterfactual-airborne.c`) | `strafe_supported_wait_check` | `Tick(100,objective)` with the runner in the air |

`tools/strafe_seam_check.py`, `tools/strafe_fast_return_check.py` and
`tools/strafe_corner_track_check.py` pass unchanged against the final source,
protecting the pass proposal and validation, fast-return admission, branch and
tap contracts, and the executor's tracking and pass/stop split. The full Python
suite passes: 468 tests.

## Frozen comparison

- Build: `.codex-research/strafe-build-span7-e81ada23d60441559bc0af6b0c6720f8`.
- Declaration, drivers, worlds and audits: `.codex-research/corner-span-20260912/v2`.
- Baseline: `.codex-research/ramp-entry-20260912/v1/candidate-r{1,2,3}`.
- Unchanged thirty-route assets: `.codex-research/pkgD-step3-20260911/assets`.
- Same twenty cases, three repetitions, timescale 5, 55-second observation, at
  most four isolated native worlds. All 1104 measured sources and both binaries
  match the build manifest (`v2/verification.json`); nothing changed during
  compilation or collection. Corrected 2026-09-12: this read `v1`, which binds
  the `span4` build of the retained intermediate arm, not the `span7` build this
  section is about. Both files exist and both bind 1104 sources, so the wrong
  one read as a valid binding for an arm it does not cover.
- A four-case preflight ran first on both teams (`preflight6/`, preceded by
  `preflight4/` on the same corrections without the last two): all captured, no
  deaths, health floor 90, and the bevel, seam and ramp neighbourhoods all
  clear. It is separate from the three complete repetitions.
- `v1/` is the same twenty-case declaration on the `span4` build, retained
  because it is the evidence the sixth and seventh corrections were made from.
  It is not the scored arm.
- Bevel diagnostic: supported-owned first-return samples within 400 XY units of
  the vertex, in the track join's own mirrored frame. A fixed local diagnostic,
  not an acceptance threshold.

## Results

| Twenty-world totals | Ramp r1/r2/r3 | Span r1/r2/r3 |
|---|---|---|
| Captured worlds | 20/20/20 | 20/20/20 |
| Deaths / falls | 0/0/0 | 0/0/0 |
| Minimum sampled z | 1808.003/1808.004/1808.005 | 1808.002/1808.004/1808.002 |
| Health floor | 90 | 90 |
| Localnav frames | 60/63/62 | 72/132/74 |
| Stationary samples | 41/53/44 | 34/117/42 |
| Zero-input samples | 124/134/128 | 106/156/101 |
| All-reason rapid churn | 0/0/0 | 0/0/0 |
| Branch entries | 42/47/44 | 37/48/41 |
| Entries open at world end | 1/2/2 | 0/6/2 |
| Gradable resource releases on a low jump | 0/0/0 | 0/0/0 |
| Unpaired resource releases | 0/0/0 | 0/0/0 |
| T5 met / declared denominator | 0/8 each | 0/8 each |

**The diagnosed stop is gone.** In the bevel neighbourhood the twelve exact-12
first returns hold a minimum of 237.3–319.4 u/s with no sample below 150,
against 45.6–49.5 u/s and 0.425–0.450 s below 150 before. The vertex is an
accepted arc in every world, reached by the fallback's one narrower proposal
after the widest arc the legs allow is refused natively.

| Exact-12 first return | Ramp seconds r1/r2/r3 | Span seconds r1/r2/r3 |
|---|---|---|
| R2-red | 18.500/18.425/18.550 | 17.775/17.775/17.775 |
| R2-blue | 18.750/18.575/18.750 | 17.850/17.925/17.925 |
| R7f12-red | 17.800/18.475/18.475 | 17.075/17.075/17.075 |
| R7f12-blue | 18.000/17.750/17.750 | 17.200/17.150/17.150 |

All twelve exact-12 pairings are faster, by 0.60–1.40 s, and on the
three-repetition mean 15 of the twenty declared cases are faster, for a total of
−9.325 s across the twenty. The four slower cases are R4-blue (+0.99), R5-red
(+0.98), R4-red (+0.86) and R6b-blue (+0.11), all of them 20–22 second returns;
R1-blue is unchanged. **Every return still exceeds 16 seconds and T5 remains 0/8
in every repetition.** This is a resolved stop and a consistent travel gain, not
a travel-time qualification. Corrected 2026-09-12: the last clause read "the
attribution above says why T5 cannot be reached on these cases by movement
alone". The attribution establishes the resource state -- 12 Force, one Speed
window, regeneration short of a second cast, and therefore a 250 u/s tail over
the final 4.5–5.5 s. It does not establish a movement-time lower bound for the
whole return: no budget was computed over the remaining phases at their
achievable speeds, so "cannot be reached by movement alone" was an inference and
not a measurement. T5 stays failed and its feasibility stays open.

The two previous repairs are retained. Seam minima are 244.6–426.1 u/s with no
below-150 sample, against 250.0–255.9 before: eleven of the twelve are at or
above the baseline and R2-red sits 5.4 u/s under it in r1 and r3, because that
vertex pair is now an accepted arc rather than two passes. Ramp minima are
237.2–241.5 u/s with no below-150 sample, identical to the baseline. No
lower-floor departure, no branch abandonment, no low-jump resource refusal among
the 30–32 gradable releases, and no unmatched release. Open final branch
instances remain censored, not successful transitions.

Regressions: 26/26 temporary-body cases passed and 30/30 resource cases
completed without refusal; these subsets retain `milestone_pass=false` for their
broader gates.

## The regressions, attributed

**Localnav and stationary frames rise, and one world accounts for most of it.**
Releases into localnav are 72/132/74 against 60/63/62. R6b-blue in r2 is 41 of
the 132 and 54 of the r2 stationary count on its own: its published plan refused
a `turn_envelope` at 425.4 u/s -- the runner overran a narrowed arc's own speed
limit -- and localnav then stood still for about a second. That is the only
`turn_envelope` refusal in 60 worlds, and the only published-plan refusal of any
kind besides three `pass_not_crossed`. The narrowed arc is what makes it
possible: a half-trim arc carries a lower speed limit than the runner's
approach, and the braking law has to fit inside the interval that precedes it.
It is a real cost of correction 3 and it has no repair here.

The rest is the seeded outbound descent, unchanged in kind from the ramp arm and
still never on a measured return: the blue cases go from 3 to 9 frames because a
newly published arc at the top of their descent refuses while the runner is
still on the ground, and R4-red improves from 20–22 to 7 in two repetitions of
three. Grounded `preparing_interval_contract` releases are 10/10/10 against
12/11/10, so the count did not grow; the windows they open did.

**What the intermediate arm cost before the last two corrections.** `v1/`, the
same declaration on the `span4` build, ran at 223/185/140 localnav frames and
196/132/110 stationary. Correction 6 halved the airborne windows and correction 7
removed the acquisition stall; both are measured in `preflight5/` and
`preflight6/` and confirmed by the arm above. Reporting only the final arm would
hide that the first three corrections, on their own, made the regression the
continuation plan asked to fold in about three times worse.

## Status

Implemented, checked and measured against the ramp-entry arm. Nothing is
installed; the installed baseline remains `reviewready-b` / `525f1b50`. T5, T2/T4
and the defended T6 obligations are unchanged and open.

Two named defects remain from this cycle's own evidence:

* A published narrowed arc can carry a speed limit the runner overruns, and the
  executor's refusal releases the whole plan. Either the arc's limit has to be
  reachable from the interval that precedes it, or the refusal has to degrade
  rather than release. One occurrence in 60 worlds, about one second.
* The seeded outbound descent still meets published crossings on the ground at
  345–757 u/s and cannot satisfy them. The executor now names which clause
  refused, at what speed and at what height above the vertex, on every release
  line -- that is the evidence the repair needs and did not have.

Recorded and not attempted: the per-sample `|dz|>1` hop flatness gate on the base
ramps, whose measured ceiling is about 0.13 s per ramp, and which has no native
clearance evidence because the tall-envelope sweep is skipped on exactly the
samples the elevation clear turns off.

## Correction, 2026-09-12

**The `turn_envelope` refusal above is not an overrun, and the first bullet
misreads it.** This record's own trace settles it: plan node 32 carries
`limit=425.000` and the clause tolerates 40 more, so the runner's 425.362 u/s was
inside it. The momentum tap has `ground=0` from level 14350 to 14850 with `vz`
falling from +84 to −319, and nodes 30 and 31 are `hop=1` against node 32's
`hop=0`: the runner was airborne on the plan's own hop when it reached a
non-hopping limited interval, 0.3 units above the vertex and inside the corridor,
and the only condition it missed was ground contact. Every other number in this
record stands. The repair, the clause-level evidence and the arm that measures it
are in the [degraded release record](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md).
