# Reroute admission: a landing off the path, validated as a route

The [landing probes](2026-09-17-human-landing-probes.md) isolated one
mechanism behind the momentum-continuation deficit: at the fastest human
run's sixth-jump state every immediate proposal was rejected for leaving the
selected path, although each passed the native physical and onward checks
when continued. This record is the implementation that record asked for: an
alternative onward route constructed and validated from the candidate
landing, carried into forecast and execution, and compared as a route.

It is default off (`bot_moveReroute`), it is not a qualified production
change, and it is not a sub-15-second stationary run.

## What the change does

In `codemp/game/bot2_move.c`:

- `straightPathFree` suspends the landing's path test for a single rollout.
  A candidate the judge marked `off_path` is rolled out again from the same
  root, with the same command function, the same hold and the same onward
  and fall checks; only the early stop at the first landing beside the path
  is suspended.
- `RerouteFrom` plans the route that landing opens: the planner's own state
  search from the cell the rollout landed on, in the state it measured there
  (speed, heading, a landing-frame tap, the Speed window left), to the cell
  the current path heads for, with the cell search as the same fallback a
  replan uses and the same avoid list. No cell, no objective cell or no route
  is outcome `no_route`: a landing the planner cannot continue from is not an
  alternative, whatever speed it retains.
- The pass has **its own walk reference**, rolled last, into its own buffers,
  to its own furthest done frame. The control's horizon and arm B's are
  untouched by candidates they never judged, which is the isolation the
  [independent-reference record](2026-09-17-independent-reference.md)
  established and this change had to preserve.
- Admission keeps the local margin test against that reference and adds a
  route test: the rollout's own time plus the walk model's remaining route
  from the landing must beat the remaining route it left by
  `MOVE_REROUTE_GAIN` (250 ms), or the candidate is `slower_route`. Both
  sides are priced by `PathRemainingMs`, so the comparison is one model
  against itself, not the graph's cost against the walk's.
- While the pass is on, every admitted candidate is priced that way and the
  ranking is by route, not by local margin: margins measured against
  different references cannot be compared, and a local margin never chose a
  route. `bot_moveStatePrefer` does not override a rerouted winner.
- `RerouteInstall` carries the route into execution at the commit: the route
  is planned again from the winning candidate's landing, installed as the
  path with a new generation and a fresh retry timer, the permit is revoked
  and the parity harness is reset - a declared discontinuity, exactly as a
  replan is. A winner whose route is gone at the commit is dropped
  (`no_route`) rather than flown.

New candidate outcomes: `rerouted`, `no_route`, `slower_route`. The
`[REROUTE]` telemetry line reports the pass itself (`pass_outcome`), which is
what the continuation did; the route gate and the margin are applied
afterwards, so the candidate line carries the outcome it ends on.

## What it does not do

- It does not widen `MOVE_PATH_REACH`, weaken a physical check, skip the
  onward or hold checks, or admit `off_path` candidates as a class. The
  measured rejected set contains both viable continuations and trajectories
  that fall or strand; the gate is replaced by a validated route.
- It does not spend Force, advance to level 2 or held jumps, or install into
  GameData.
- A rerouted candidate's `[SPIKE]`/`[REPLAY]` record reports the path-free
  continuation's measure, because that is the rollout it was judged on. The
  first, path-stopped rollout's measure is not retained for it.

## Verification in the small

`tools/test_movement_reference.py` compiles the production `StraightJudge`
against mocked forecast boundaries and runs nine scenarios: the four
reference-isolation scenarios that already existed, and five for this change
- a rerouted candidate admitted and ranked, one refused as `slower_route`,
one refused as `no_route`, and two that pin the ranking. It still rejects its
coupled-reference negative control. Retained:
`.codex-research/movement-reference-test-20260918T011028Z-e0d00e9e` (before
the bit split: `...-20260918T005104Z-fb0eb3ef`).

Every scenario asserts that the control candidate's own reference is
unchanged - same walk call, same 50-frame horizon, same margin - while a
rerouted candidate is judged at frame 99 of a 100-frame third reference.

48 movement replay tests, 22 benchmark tests and 14 route-report tests pass.
One unrelated suite, `test_strafe_map_transitions`, fails on a missing
archive directory; it fails identically without this change.

`tools/movement_replay.py` learns the third arm: `rerouted` candidates are
excluded from either arm's inferred horizon, are expected to carry the
`reroute` reference, and the new outcomes join its counts. Without that the
isolation audit would report every rerouted candidate as judged by the wrong
reference.

## The first campaign: build `move-build-reroute-20260918-b`

Retained under `.codex-research/reroute-campaign-20260918-a/`, serial
localhost worlds, timescale 1, one frozen build. The owner's human-route
recording server (port 29090) was idle on the machine throughout, in both
arms of every comparison.

**The default floor holds.** 36 of 36 pass with the cvar unset, zero parity
mismatches, no legacy owner, zero deaths. The refactor that moved the
arrival test into `StraightArrives` and added the third reference changes
nothing when the pass is off.

**The mechanism fires, at the state the probes isolated.** On the jump-6
human-derived root the search commits a rerouted two-hop landing at
735.0 units/sec, where the control commits the 24-unit run-up that sheds to
486. Four rerouted commits and three to five route installs per run:

| jump-6 root | control | reroute |
| --- | ---: | ---: |
| Force 100 | 12.500 s | 10.700 s |
| Force 0 | 11.775 s | 10.425 s |

Both arms pass with zero deaths and zero parity mismatches. These are
remaining-pickup times from a synthetic root, not flag-to-flag times, and
the earlier probe build gave the same direction (11.775/11.425 against
10.700/10.750).

**The gate does not improve.** The 12-case level-1 seeded arms, one
repetition each:

| | control | reroute |
| --- | ---: | ---: |
| total | 228,850 ms | 234,825 ms |
| median | 18,400 ms | 19,188 ms |

Every case passes with zero deaths and zero mismatches. Every per-case delta
is inside the recorded level-1 spread, so one repetition decides nothing by
itself - but nothing here supports the change either.

**The stands say it is worse on blue.** Three repetitions per arm:

| case | control | reroute |
| --- | --- | --- |
| red_out_L1_stand | 20,700 / 20,700 / 20,700 | 20,625 / 22,750 / 20,625 |
| blue_out_L1_stand | 19,300 / 20,650 / 20,650 | 22,675 / 22,675 / 21,975 |

Blue's two run sets do not overlap: about 2,025 ms of median cost that
quantisation does not explain. Red is a wash.

**The named regressions pass both ways**: `red_ret_L1_stand` 22,550 against
21,300, `red_ret_L1_respawn4` 20,400 against 18,600, and the falling-endpoint
regression `blue_out_L1_respawn5` 17,000 against 19,125. No deaths, no parity
mismatches.

One reported owners list, `red_ret_L1_respawn4` in the reroute arm, reads
`['legacy', 'move']`. It is not this change: both arms contain exactly one
`recover floor_missing / decline=corridor` frame at the same place
(x about 2,751.6, y about 2,975), and the play log's sampler landed on that
frame in one arm and one frame past it in the other. The seeded control arm
of the same build, with the pass off, reports the same owners for the same
case. The refusal there is pre-existing and unexplained; the ownership gate
is sensitive to which frame the sampler catches.

## What the first campaign establishes

1. The rejected candidates the probes found are executable: the search
   commits them, the executor flies them, and the route installed from the
   landing carries the run to the objective without a death or a mismatch.
2. Admitting them is not by itself faster over a route. On the family the
   owner times, it was slower.

The two are consistent, and the likely reason is in this change rather than
in the idea. With the pass on, the ranking of **every** admitted candidate
moved from the local margin to `PathRemainingMs`, which prices a route at a
flat 250 units/sec plus a flat 250 ms an edge. That is not what a fast
strafe corridor costs, and the stands are made of fast strafe corridors: a
route model that cannot see speed will prefer the shorter cell count. The
admission change and the ranking change were confounded in these arms.

They need not be coupled. Both walk references are the same walk from the
same root, rolled to different horizons, so a rerouted candidate's margin is
measured at the same frames as any other candidate's and the two compare.
`bot_moveReroute` is therefore now two bits - 1 admission with the route
gate, 2 the route-based ranking - measured apart in the campaign below.

## The second campaign: the two bits apart, build `move-build-reroute-20260918-c`

Retained under `.codex-research/reroute-bits-20260918-a/`. Three arms on one
build: control, bit 1 (admission, margin ranking), bits 1+2 (admission and
route ranking). Serial worlds, timescale 1.

Stands, three repetitions each, milliseconds:

| case | control | bit 1 | bits 1+2 |
| --- | --- | --- | --- |
| red_out_L1_stand | 19,150 / 19,050 / 20,700 | 17,600 / 20,425 / 20,500 | 20,625 / 20,125 / 20,075 |
| blue_out_L1_stand | 18,600 / 20,650 / 18,600 | 20,775 / 24,675 / 20,775 | 27,525 / 23,850 / 21,975 |

Medians: red 19,150 / 20,425 / 20,125; blue 18,600 / 20,775 / 23,850.

The 12-case level-1 seeded arms on this build, control against bit 1: total
229,650 ms against 233,300 ms, median 18,913 against 19,350, ten of twelve
cases slower or level. All pass, zero deaths, zero parity mismatches.

The jump-6 human-derived roots with bit 1 alone: **9.425 s in both Force
cases**, against 12.500 / 11.775 for the control and 10.700 / 10.425 for both
bits. That is the largest margin measured for this change anywhere.

So the two bits separate, and both conclusions are negative for the gate:

- Route ranking (bit 2) is the more expensive half, as expected of a model
  that prices every route at a flat walk speed. It should not be used again
  in this form.
- **Admission alone is still slower on the standard level-1 families**,
  while being much faster from a fast mid-route root. The idea is not
  refuted; this implementation of it does not pay on a standing start.

## Why a standing start pays for it: planned edges go stale

Counting `edge=strafe reason=stale` per stand world, which is the event that
declares a planned edge's entry envelope no longer matches the live state and
sends the path back to the search:

| arm | red_out_L1_stand | blue_out_L1_stand |
| --- | --- | --- |
| control | 5 / 5 / 4 | 1 / 2 / 1 |
| bit 1 | 5 / 6 / 12 | 11 / 14 / 10 |
| bits 1+2 | 4 / 6 / 5 | 15 / 9 / 5 |

In `stands-admit-r1/blue_out_L1_stand` the stale edges arrive in a run: seven
of them at 250 ms intervals from level 13,025 to 14,525, each a replan, each
followed by the path retry, ending in another reroute install. About a second
and a half of a twenty-second run spent re-planning.

The mechanism is visible in the change itself. A route is planned from the
landing **in the state the rollout measured there**, and its first planned
strafe edges are priced for that state. The bot then flies, lands, and walks
the new path - and by the time it reaches those edges' takeoffs it no longer
carries the state they were planned for, so the edge is stale, the path is
searched again, and the search pays `MOVE_PATH_RETRY`. A stand start, where
the bot has the least speed to spare, pays that most.

## What to do next, in order

1. **Keep the state the route was planned for, or do not plan for it.** The
   installed route's first edges are the ones that go stale. Either carry the
   landing state forward through the plan the executor follows (the state
   planner already threads a state; the install drops it at the boundary), or
   install the route without state-priced edges and let the straight search
   validate them as it does elsewhere. Measure the stale count first: it is
   the cheapest signal this change has.
2. **Do not let a rerouted winner bypass `StatePreferred`.** It does now, so
   a greedy rerouted hop can beat a planned chain that was learned and priced
   as a whole. On a stand the chain is most of what the bot has. This is one
   line and its own arm.
3. **Price the route gate by something that can see speed.** Admission itself
   still uses `PathRemainingMs`. The state planner's own `costMs` already
   prices links at the speed the state carries; use it on both sides of the
   comparison, or state plainly that the gate is a walk-speed gate.
4. Only then re-run the stands and the seeded arms, and keep the jump-6
   probe as the positive control - it is the one place this change is known
   to help, and any repair must not lose it.

Until one of those lands, `bot_moveReroute` stays default off and unqualified.
No run in this record is a sub-15-second stationary flag-to-flag pickup; the
best stationary stands here are the controls, at 18,600 ms (blue) and
19,050 ms (red).

## Limitations

- The jump-6 roots are synthetic continuation probes, not human replays, and
  their times are remaining-pickup times from a mid-route state.
- Parity is zero mismatches in every arm with zero compared boundaries, as in
  the earlier probe records: that is no positive parity qualification.
- The level-1 seeded arms are one repetition each. The stands are three, and
  only blue's separation there exceeds the recorded quantisation spread.
- The default 36-case floor was run on the first build. The bit split that
  followed changes nothing when the cvar is unset, but the floor has not been
  re-run on `move-build-reroute-20260918-c`; run it before any qualification.
- Owners lists are sampled: `red_ret_L1_respawn4` and `blue_out_L1_stand`
  each reported a legacy owner in a control arm, from a one-frame corridor
  recovery the sampler happened to catch.

## The third campaign: the two repairs, build `move-build-stale-20260918-d`

Retained under `.codex-research/reroute-stale-20260918-a/`. Two repairs from
the ordered list above were implemented, each default off:

- `bot_moveStaleAvoid`: a planned strafe edge that goes stale twice in a row
  for one objective is avoided for it, so the search stops re-picking a chord
  the bot does not arrive at in the state it needs. Any edge that does stand
  resets the run.
- `bot_moveReroute` bit 4: a rerouted winner is subject to
  `bot_moveStatePrefer` like any other candidate, instead of bypassing it.

Five arms on the stands, three repetitions each, milliseconds:

| arm | red_out_L1_stand | blue_out_L1_stand |
| --- | --- | --- |
| control | 20,700 / 20,700 / 19,600 | 18,600 / 19,925 / 20,650 |
| stale-avoid | 20,700 / 19,600 / 20,175 | 19,925 / 19,300 / 19,675 |
| admission | 19,350 / 21,325 / 21,325 | 20,600 / 20,600 / 18,575 |
| admission + stale-avoid | 21,325 / 17,600 / 19,025 | 18,575 / **181,850 stall** / 18,575 |
| admission + chain preference | 18,925 / 17,600 / 17,600 | 20,125 / 20,125 / 19,725 |

Medians, red: 20,700 / 20,175 / 21,325 / 19,025 / **17,600**. Blue: 19,925 /
19,675 / 20,600 / 18,575 / 20,125. The default 36-case floor on this build is
36/36 with both cvars off, zero mismatches, zero deaths, no legacy owner. The
jump-6 probe holds with the pass on (11.000 / 9.425 s).

Restoring the chain preference is the largest stand improvement measured in
this line of work: red's median falls 3,100 ms below the control and its
slowest run beats the control's fastest. Blue is a wash.

## The fourth campaign: the gate for bit 4, same build

Retained under `.codex-research/reroute-prefer-20260918-a/`. It fails, twice
over:

- `red_ret_L1_stand` **dies**. The walk brakes for `floor_missing` at level
  19,200, the plan completes at z=1,780 already descending, and the bot falls
  to the pit floor at z=32 where `trigger_hurt` kills it. One death, owners
  `['legacy', 'move', 'none']`. The named falling-endpoint regression
  (`blue_out_L1_respawn5`) passed in the same arm; a different case fell.
- `blue_out_L1_stand` **stalls** for 184,475 ms in the seeded arm.

## The stall is a pre-existing deadlock, and it is the important finding

The first stall appeared with `bot_moveStaleAvoid` on and was easy to blame
on it. The second appeared in an arm with **no stale-avoid at all**, at the
same place, with the same signature, which refutes that reading:

- `stands-admit-stale-r2/blue_out_L1_stand`: last landing at level 11,375 at
  (1901, 2611); no `edge=onward reason=complete` after it; no path event
  after 11,875; the bot inside ten units of (1873, 2600) until the case timed
  out at 181,850 ms. 664 straight searches ran in that time and refused every
  candidate.
- `seeded-prefer/blue_out_L1_stand`: last landing at 11,900 at (2018, 2911);
  no onward completion, no path event, the bot at (1901, 2623) until
  184,475 ms. Zero stale-avoids in the arm.

The mechanism is in the executor's own contract. A committed sequence's last
stage is cleared by travelling `MOVE_ONWARD_DISTANCE` (128 units) from the
landing mark (`MoveStep`), and the planner replans only when no sequence is
in progress:

    if(PlannerEnabled() && grounded && level.time>=m->path.retryTime &&
       !(m->plan.count && m->executor.stage<=Onward(&m->plan)) &&
       (!m->path.count || m->executor.pathIndex<0 || m->path.replan))Replan(client);

A bot that lands where it cannot cover 128 units therefore holds a plan that
will never finish and a path that will never be searched again. Nothing else
recovers it: the recovery never fires, the straight search is permitted at
that stage and refuses everything, and the walk shuffles. Reroute admission
did not create this; it lands the bot in such a pocket often enough to find
it. Any landing that cannot make the distance reaches it in a default build.

`bot_moveOnwardBound` (default off) releases the stage after
`MOVE_ONWARD_HELD` (40) grounded frames, exactly as the distance would, and
replans from where the bot stands. The hold counter runs in every arm, so a
control run now reports `edge=onward reason=held` with the frames held: the
deadlock is visible rather than inferred. Its campaign is
`.codex-research/onward-bound-20260918-a`.

## Where this leaves the change

Every reroute arm measured is disqualified on its own terms:

| arm | verdict |
| --- | --- |
| admission + route ranking | slowest of all; no safety failure |
| admission alone | slower than control on the standard families |
| admission + stale-avoid | best stand medians; one stall |
| admission + chain preference | best red stand median; one death, one stall |

`bot_moveReroute`, `bot_moveStaleAvoid` and `bot_moveOnwardBound` are all
default off and none is qualified or installed. What the work established
that stands on its own:

1. The candidates the landing probes isolated are executable and fast from a
   fast mid-route state (9.425 s against 12.500 s at the jump-6 root).
2. Ranking routes by `PathRemainingMs` is worse than ranking by the local
   margin, on a family made of fast corridors.
3. A rerouted winner must not bypass the planned-chain preference.
4. **A landed plan that cannot travel 128 units deadlocks the planner
   permanently**, in default builds, and nothing in the executor recovers it.
5. The planner's arrival model and the executor disagree often enough to
   produce runs of stale planned edges; avoiding the edge bounds the symptom
   and can strand the bot, so the disagreement itself is the thing to fix.

Items 4 and 5 are defects of the existing movement code that this change
surfaced. They should be fixed before any further work on admitting off-path
landings, because both of them punish exactly the state an admitted landing
leaves the bot in.

## The fifth campaign: the onward bound, build `move-build-onward-20260918-e`

`bot_moveOnwardBound` (default off) releases a landed plan's onward stage
after `MOVE_ONWARD_HELD` (40) grounded frames, exactly as the 128 units of
travel would, and searches the path again from where the bot stands. The hold
counter runs in every arm and reports `edge=onward reason=held` with the
frames held, so a control run shows the deadlock rather than hiding it.

**Safety**, `.codex-research/onward-bound-20260918-a`: the default 36-case
floor is 36/36 with the bound off and 36/36 with it on, zero parity
mismatches and zero deaths in both. The three named regressions pass with the
bound on (20,450 / 20,400 / 17,425 ms). The bound never fired in any of those
49 worlds, so it changed nothing that was not already held.

**The deadlock, at higher count**, `.codex-research/onward-bound-20260918-b`:
ten repetitions of `blue_out_L1_stand` per arm, admission (`bot_moveReroute
5`) against the same plus the bound.

| arm | times, sorted (ms) | median | max | stalls | holds | bound firings |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| admission | 19,700 19,725 20,125 20,125 20,125 21,675 21,675 22,500 22,625 **28,200** | 20,900 | 28,200 | 0 | 2 | 0 |
| admission + bound | 19,700 19,725 19,725 20,125 20,125 20,125 21,675 21,675 22,825 **23,400** | 20,125 | 23,400 | 0 | 0 | 2 |

No run stalled in these twenty worlds, so the stall is rarer than the two
occurrences earlier in the day suggested, and it is not reproduced here.
What is reproduced is the hold that precedes it, at the same place:

- `blue-prefer-r5`, the arm's slowest run at 28,200 ms, held the onward stage
  past the limit twice, at (1888.8, 2592.4) and (1882.9, 2597.1), escaping
  after about two seconds each time.
- `blue-bound-r8` fired the bound at **(1888.8, 2592.4)**, the same point to
  a tenth of a unit, and once more later; it finished at 23,400 ms.

That is one occurrence in each arm from different roots, not a controlled
pair, and it does not establish a time benefit. It does establish that the
hold is real, that it localises to one pocket on this route, that it costs
seconds when it resolves and the case when it does not, and that the bound
fires exactly there and the run continues.

## The state after this work

Default off and unqualified: `bot_moveReroute` (bits 1, 2, 4),
`bot_moveStaleAvoid`, `bot_moveOnwardBound`. Nothing is installed. The
stationary no-Force target is unmet, and the fastest stationary stands in
this record are control runs.

What a later agent should not re-derive:

1. Admitting a landing off the selected path is executable and fast from a
   fast mid-route state, and slower from a standing start. One number for
   both hides it.
2. `PathRemainingMs` cannot rank candidates that differ in the speed they
   keep.
3. A rerouted winner must not bypass the planned-chain preference; it is also
   not sufficient - that arm killed the bot on `red_ret_L1_stand`.
4. A landed plan that cannot travel 128 units deadlocks `Replan` for good.
   The bound is a symptom bound; the walk that cannot leave the landing, and
   the planner's arrival model that puts the bot there, are the defects.
5. Avoiding a planned edge because it went stale can strand the bot: the
   chord it removes may be the only way out of a pocket.

## The sixth campaign: the bound's own timing gate, and what it measured instead

`.codex-research/onward-bound-20260918-c`, no reroute in any arm: the bound
alone against the default, on the 12-case seeded families and on the stands
at three repetitions.

The stands are identical. Red medians 19,150 against 19,150, blue 19,925
against 19,925, the same three values in each arm, every case passing. The
bound fired in none of these worlds.

The seeded arms are not, and that is the finding. Over the ten cases both
arms completed, the bound arm is **7,100 ms faster in total**, median 20,263
against 18,875 - **while the bound never fired**. Zero `edge=onward
reason=bound` and zero `reason=held` events in either arm, so the code the
two arms ran was the same code on the same build. The whole 7,100 ms is
run-to-run variance.

That is a calibration, and it applies backwards to this record. The first
campaign's seeded arms differed by 5,975 ms and the second's by 3,650 ms, and
both were read here as evidence that admission costs time. They are smaller
than the difference a change that provably did nothing has just produced. A
12-case level-1 seeded arm at one repetition cannot resolve a few thousand
milliseconds, and those two readings must not be quoted as if it could.

What survives that correction, because it does not rest on an arm aggregate:

- The stand repetitions, where blue's run sets did not overlap (control
  18,600-20,650 against 20,775-24,675 for admission alone).
- The categorical failures: one death on `red_ret_L1_stand` with
  `bot_moveReroute 5`, and two stalls of about 175 seconds.
- The jump-6 probe, repeated across three builds and both Force cases, where
  admission is 2 to 3 seconds faster every time.

Two worlds in this campaign ended in `corner start refused` runner errors,
one per arm, with 8.7 GB free: fixture flakiness, excluded from the
comparison above, not bot failures.

**The bound's status.** It is safe (floor 36/36 with it on and off, named
regressions passing, stands identical) and inert unless the hold happens. It
fixes no failure that a default build was measured to have today, so by this
project's own rule it is not taken: it stays default off. The hold *report*
is not behind the cvar and now runs in every build, so the deadlock announces
itself as `edge=onward reason=held` with the frames held, wherever it happens
next.
