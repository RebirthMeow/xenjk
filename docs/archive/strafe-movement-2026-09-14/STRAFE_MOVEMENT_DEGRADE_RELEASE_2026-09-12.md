# Released plans that nothing had found fault with — 2026-09-12

Continues the [continuation plan](STRAFE_MOVEMENT_NEXT_EXECUTION_2026-09-12.md)
past its third step, on the two defects the
[corner-span record](STRAFE_MOVEMENT_CORNER_SPAN_2026-09-12.md#status) left named:
a published turn limit the runner arrives over, and the seeded descent's
grounded crossing refusals. Both were executor release decisions, and both are
repaired here. Nothing is installed; the installed baseline remains
`reviewready-b` / `525f1b50`.

Scope: the executor's release rule and the report that names it. No compiler,
native-check, resource-admission or route change. Contracts, thresholds and
denominators are unchanged.

## What the retained traces attribute the releases to

Every release in the corner-span arm that named a contract — 11, 13 and 10 across
its three twenty-world repetitions — falls into three families, and the executor's
new clause naming says which condition each one actually missed. The first
finding is that one of them was attributed wrongly:

**The `turn_envelope` release was not an overrun.** The corner-span record reads
its one occurrence (R6b-blue, r2) as "the runner overran a narrowed arc's own
speed limit at 425.4 u/s". The arc's published limit at that vertex is 425.0 and
the clause tolerates 40 more, so 425.362 was inside it. The runner was *airborne*:
plan node 32 is `hop=0 limit=425`, the two nodes before it are `hop=1`, and the
momentum tap has `ground=0` from level 14350 to 14850 with `vz` falling from +84
to −319. The only condition it missed was ground contact, on a ground contract it
could not meet in mid-flight, 0.3 units above the vertex and inside the corridor.
Releasing cost about a second: localnav then braked it from 425 to 130 u/s.

**The blue seeded descent overflew a vertex and landed under it.** Five worlds
per repetition release `crossing_not_passed` at prefix index 2, at 486–693 u/s,
65–75 units *below* the vertex whose plane they had crossed while falling. The
height window is what refuses, and no later state can satisfy it: the vertex is
behind the runner for good. The path below it was still the path it wanted.

**The red seeded descent lands beside the line.** Five worlds per repetition
release at prefix index 3 at 345–438 u/s. The clause is lateral and the vertex
carries no limit at all, so the bound is the ordinary 24-unit arrival tolerance;
the offsets themselves are 26.0–70.6 units, measured by this cycle's own report,
because the baseline binary had no field for them. That is a runner genuinely off
the corridor, not a contract too strict, and it is the one family this cycle does
not repair.

## The responsible contract

`Bot2_PathCommandReported` in `codemp/game/bot2_path.c` owns all three. Its
advance loop refuses as soon as a crossing's plane is behind the runner and its
contract is unmet, and its head check refuses when the interval the follower is
on is a limited one and the state is outside the envelope. Every refusal reaches
`ai_bot2_supported.c` as one result, and the owner's `Reject` destroys the
published path and bans re-acquisition for a second; the runner falls to
localnav, which is where the standing frames come from.

Two of the three families are states that say nothing about the path — a runner in
the air, and a vertex the runner has gone past — and the third is a speed limit
judged as if the runner were already at the vertex when it was still approaching
it.

## The corrections

All four are in the executor. None changes a threshold, an envelope or a
validated speed.

1. **A ground contract is not judged in the air.** On a limited interval, when
   the runner is airborne and the interval does not itself admit flight, the
   height window and the contact requirement are deferred and the plan keeps
   commanding the interval. The lateral envelope still decides every frame, and
   the judgement happens on landing. This is the prefix's existing airborne rule
   (correction 6 of the corner-span cycle) applied where the plan is published.
2. **A vertex the runner is past is left behind, not released on.** The advance
   loop walks past a crossing whose plane is behind the runner and whose contract
   it missed, and then requires the interval the follower resumed on to contain
   the runner, in the ordinary arrival tolerances — 24 units of lateral offset,
   128 of height, between the two vertices. Outside them the plan is released as
   before, with the vertex, the clause and the measured offset named. A vertex
   *ahead* is never skipped, so nothing cuts a corner.
3. **A vertex's speed limit is judged against the braking room left to reach
   it.** The head check compares the runner's speed against
   `sqrt(limit² + 1600·room)`, the same braking law `ContinuousCommand` already
   schedules the approach with, where `room` is the distance still left on the
   interval. At the vertex this is the old `limit + 40` exactly, so nothing
   traverses an arc faster than the arc was validated for.
4. **A speed the geometry was not swept for is never left behind.** Correction 2
   forgives where the runner was — in the air, low, or wide — but not a crossing
   missed on speed: that is the contract the sweep covered, and the vertex is
   where it was measured, so it releases there as it always did.
   **Amended 2026-09-12: as written here the gate did not hold.** It asked the
   refusal's reported clause, which names the *first* condition to fail in a
   precedence order, so a crossing that was wide *and* faster than its sweep
   reported `lateral` and was left behind with the speed unjudged. The rule above
   is unchanged and is now asked of the state, with the one boundary this record
   did not state: the ceiling belongs to a turn taken on the vertex's own floor,
   so a plane crossed in flight or outside the height window carries no speed to
   judge — which is what makes correction 2's overflown vertices compatible with
   this one. See the
   [speed-guard record](STRAFE_MOVEMENT_SPEED_GUARD_2026-09-12.md).

The report gained the clause, the measured lateral offset, the vertex's own limit
and the braking room, and the owner prints all four on the release line and on
the airborne prefix line. Nothing reads them; `Reachable` is the only new gate.

## Checks

`tools/strafe_path_release_check.py` compiles the production executor and decides
single frames from the states above: the airborne arc with its measured lateral
offset and height, the overflown vertex at its measured drop, the narrowed arc at
425.1 u/s against a 372 limit, a pass vertex crossed wide and crossed fast, and an
ordinary metadata-free path. Each case is paired with the counterexample that must
still be refused — airborne but wide, landed 300 units down, three times the arc's
limit, outside a pass vertex's rejoin excursion — and every case runs at four
rotations, mirrored. It asserts the reported clause, limit, room and offset, and
that the resumed command points down the interval rather than back at the vertex
it left. No native geometry is involved: these are contract decisions over a given
player state, so this covers admission and release, not clearance.

Each correction is shown to be necessary by reverting it alone
(`.codex-research/degrade-release-20260912/counterfactual.py`, which refuses if
the text it reverts has moved):

| Reverted correction | Assertion that fires |
|---|---|
| The airborne deferral | the airborne frame inside the corridor continues |
| The stale-vertex resume | the overflown vertex's interval continues |
| The braking-room allowance | 425.1 u/s twenty units before a 372 arc continues |
| The speed-miss exclusion | a pass vertex crossed at 438 against a 362 sweep refuses |

`tools/strafe_corner_track_check.py`, `tools/strafe_seam_check.py`,
`tools/strafe_corner_span_check.py`, `tools/strafe_supported_wait_check.py` and
`tools/strafe_fast_return_check.py` pass unchanged against the final source,
protecting the ground-turn speed law and arc tracking, the pass proposal and
validation, the bevel span, supported dispatch with its airborne prefix rule, and
fast-return admission, branches and taps. The full Python suite passes: 468 tests.

## Frozen comparison

- Build: `.codex-research/strafe-build-degrade2-20260913` (stamped UTC).
- Declaration, drivers, worlds and audits: `.codex-research/degrade-release-20260912/v2`.
- Baseline: `.codex-research/corner-span-20260912/v2/candidate-r{1,2,3}`, re-read
  from its retained worlds by `v2/compare.py`, not quoted from its stored audits.
- Unchanged thirty-route assets: `.codex-research/pkgD-step3-20260911/assets`.
- Same twenty cases, three repetitions, timescale 5, 55-second observation, at
  most four isolated native worlds. All 1104 measured sources and both binaries
  match the build manifest (`v2/verification.json`); nothing changed during
  compilation or collection.
- A four-case preflight ran first on both teams and on the exact-12 resource
  state (`preflight2/`): all captured twice, no deaths, health floor 90, clean
  shutdown, and the only contract release is the retained lateral miss.
- `v1/` is the same twenty-case declaration on the two-correction build
  `.codex-research/strafe-build-degrade-20260912`, retained because it is the
  evidence corrections 3 and 4 were made from. It is not the scored arm.

## Results

| Twenty-world totals | Span r1/r2/r3 | Degrade r1/r2/r3 |
|---|---|---|
| Captured worlds | 20/20/20 | 20/20/20 |
| Deaths / falls | 0/0/0 | 0/0/0 |
| Minimum sampled z | 1808.002/1808.004/1808.002 | 1808.005 each |
| Health floor | 90 | 90 |
| Contract releases | 11/13/10 | **6/6/6** |
| Localnav frames | 72/132/74 | **38/36/31** |
| Stationary samples | 34/117/42 | 52/48/43 |
| Zero-input samples | 106/156/101 | 149/158/119 |
| Zero-input while stationary | 0/24/0 | **0/0/0** |
| All-reason rapid churn | 0/0/0 | 0/0/0 |
| Branch entries | 37/48/41 | 43/42/42 |
| Entries open at world end | 0/6/2 | 4/4/0 |
| Gradable resource releases on a low jump | 0/0/0 | 0/0/0 |
| Unpaired resource releases | 0/0/0 | 0/0/0 |
| T5 met / declared denominator | 0/8 each | 0/8 each |

**The releases that remain are one family.** All eighteen are
`crossing_not_passed`, clause lateral, at prefix index 3, in the same six worlds
every repetition — R1/R2/R3/R4/R5-red and R4-blue — at 26.0–70.6 units off a
vertex whose tolerance is 24. Both airborne families are gone: the five blue
seeded worlds go from nine localnav frames each to none, and the one airborne
`turn_envelope` release does not recur.

**The zero-input rise is the plan braking, not a runner idling.** Zero-input
frames while stationary fall to none in all three repetitions, against 24 in the
baseline's r2; of the zero-input frames that remain, 102–136 are above 250 u/s,
and their owner is the published plan (137/146/107 frames) rather than localnav
(12 each, against 23/59/20). Those are frames the plan used to lose to a release.

**Travel is unchanged to slightly better, with one case that is not about this
build.** Thirteen of twenty cases are faster on the three-repetition mean. The
mean delta over all twenty is +0.058 s, and −0.070 s without R5-blue, which is
+2.492 s on its own and is attributed below. Exact-12 returns: R2-red 17.775 in
both arms, R2-blue 17.900 to 17.758, R7f12-red 17.075 to 17.058, R7f12-blue
17.167 to 17.050. The bevel and ramp neighbourhoods hold: bevel minima
237.2–319.7 u/s and ramp minima 237.2–241.4 u/s, with no sample below 150 in
either arm. Regressions: 26/26 temporary-body cases passed their subset
thresholds and 30/30 resource cases completed with no contract release at all;
both subsets retain `milestone_pass=false` for their broader gates.

## The regressions, attributed

**R5-blue is a start-phase outcome, not a build outcome.** *Corrected
2026-09-13: it is neither. What decides it is the runner's distance to route 4's
sample 50 at the moment its Force reaches the 70 the join's launch costs — 110 to
127 units in every world that joins, 131 to 132 in every world that misses,
against a 128-unit scan radius, over eighteen retained worlds and six builds. The
acquire-level rule below holds on the worlds it was drawn from and fails on a
later one at the same phase, and the 600 ms window below is not the mechanism: the
scan is retried every grounded frame. See the
[join race record](STRAFE_MOVEMENT_JOIN_RACE_2026-09-13.md). The paragraph is kept
as written because the worlds it cites are the evidence that analysis re-reads.*
Its capture is 17.875
in every baseline repetition and 17.875 in candidate r1, but 21.700 and 21.525 in
r2 and r3. The case turns on one window: at about level 13300 the fast-return
layer joins route 4 at index 50, casts Force Speed with the Force that has
regenerated by then, and launches at 807–827 u/s. When that join does not happen,
the published plan walks the rest of the return at 302 down to 117 u/s and the
capture lands where R5-red's does. What decides it is when the world's own fixture
setup let the runner acquire a path: every world that acquired by level 1700
joined, and the two that acquired at 1875 and 2350 did not, because preparation
takes about 9.5 s from acquisition and the runner reaches the join scan out of
phase. Three further R5-blue worlds on the measured build
(`.codex-research/degrade-release-20260912/phase`) acquired at 1075–1225, joined
at 12700–13325 and captured in 17.850–17.875 — the baseline outcome, from the
candidate binary. The two corrections that could plausibly have caused it are also
ruled out by `v1/`, whose R5-blue captures in 17.850 in all three repetitions on
the two-correction build. The fragility is real and is recorded as such: a
one-shot join window about 600 ms wide, reachable only from a narrow band of
preparation phase, with nothing to re-open it for the rest of the return.

**Nothing casts Force Speed while the published plan owns the command.** The
ground follower's cast (`ai_bot2_route.c`) is the one that matters on a return,
and the executor zeroes `generic_cmd` on every frame it commands. That is why a
long plan-owned stretch and a missed lane compound: the Force regenerates, 66 to
96 in R5-blue r2, and is never spent. This cycle does not change it, because
issuing Speed from the executor is a resource decision and belongs to the layer
that owns admission.

**Stationary samples are up against the baseline's best repetition and down
against its worst** (52/48/43 against 34/117/42), and entries open at world end
are 4/4/0 against 0/6/2. Both measures move within the band the retained arms
already show for this declaration, and neither is resolvable at three
repetitions.

## Status

Implemented, checked and measured against the corner-span arm. Nothing is
installed. T5, T2/T4 and the defended T6 obligations are unchanged and open.

One defect was found in this cycle's own executor after the fact: correction 4's
gate read the reported clause instead of the state, so an overspeed crossing that
also missed another condition was left behind. Repaired and measured in the
[speed-guard record](STRAFE_MOVEMENT_SPEED_GUARD_2026-09-12.md); this arm's sixty
worlds do not contain the state, so its numbers below are unaffected.

Two named defects remain from this cycle's own evidence:

* The seeded descent's red family lands 26.0–70.6 units off the prefix vertex it
  crosses at index 3, at 274–444 u/s, and is released for it. Twenty-four units is
  the ordinary arrival tolerance and the vertex carries no limit, so this is not a
  contract to relax: either the landing is off the corridor and the plan should
  be re-acquired from where the runner is without the second-long retry ban, or
  the prefix published for a runner in a ballistic descent is the wrong prefix.
  Six worlds per repetition, four localnav frames each.
* The fast-return join that R5-blue's time depends on is missed whenever the
  runner crosses the 128-unit scan radius around its lane sample before its Force
  reaches the 70 that sample's launch costs, and a plan-owned stretch then carries
  it away with no second chance and no Force Speed cast for the rest of the return.
  That is a lane handoff defect, not an executor one, and it is worth more than the
  releases repaired here: 3.8 s on one case when it misfires. Attributed in the
  [join race record](STRAFE_MOVEMENT_JOIN_RACE_2026-09-13.md) — which also corrects
  the "one window, about 600 ms wide" mechanism this bullet first gave it.
