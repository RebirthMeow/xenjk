# The guided corridor diagnostic, 2026-09-19

The [refined method](2026-09-19-refined-method.md)'s second acceptance step:
give the search a human route's coarse corridor, have it generate its own
commands from the matched start, and compare connected native execution with
the human reference. This record is that diagnostic and the unguided arm taken
beside it. It is **not** an independent-movement qualification and **not** a
runtime-cost result; those verdicts are separate and both remain open.

**What it found.** The corridor is not what limits the bot. Guided onto the
human's own line the search completes the route in **20,500-20,725 ms** where
the human took **14,800 ms** on that line and where the same search unguided
takes **19,150-19,600 ms**. The vocabulary reaches the human's speeds in
short bursts - a chained hop leaves 506-527 units/sec - but it cannot hold
them: about half of the hop-to-hop joins miss the landing frame, and a
delayed join costs a median **182-187 units/sec** to ground friction, around
**1,650 units/sec over a single run**. The human pays that toll zero times:
it is airborne **93.2%** of the run against the bot's **81%**.

**Corrected 2026-09-19, after review.** The first version of this record read
`unsupported=256` on a refused candidate as "no supported landing". That is
wrong: `256` is `BOT2_FORECAST_TRIGGER` (`1<<8`), the rollout stopping on
contact with a trigger entity. The refusals at the delayed joins are three
different things - a genuine physical failure, a selected-path rejection and
a simulation the forecast declines to model - and the classification below
replaces the earlier single claim. The coverage claim that "no landing frame
skipped its search" is also withdrawn; it rested on instrumentation that
fired once in an entire run. Both corrections are worked through below and
neither changes the guided-versus-human or guided-versus-unguided times.

## Binding

| | |
| --- | --- |
| Repaired control build | `.codex-research/review-fix-build-d`, 1,117 sources and both binaries verified against the checkout at the start of this work |
| Diagnostic build | `.codex-research/move-corridor-build-a`, binding the checkout with the corridor change |
| Case | `red_out_L1_stand`, `tools/fixtures/movement/kejim/cases.json`, start `(152,-1160,1924.125)` yaw 90, at rest, **force 0**, spend `walk corner hop tap`, goal the enemy flag |
| Search fixture | `--edges --parity 256`, planner/straight/state default on, graph seed `move-explore-20260916-b/seed.txt`, timescale 5 |
| Physics | `sv_fps 40`, `pmove_fixed 1`, `pmove_msec 8`, `pmove_float 1`, `g_speed 250`, `g_gravity 800`, `g_stepSlideFix 1`, enforced by `addrunner` itself |
| Human reference | `1789686390-1-c0`, 14,800 ms, complete, stationary start `(120.8,-1142.6,1924.125)`, 35.4 units from the case's stand |
| Corridor | `.codex-research/corridors/red_out_1789686390.txt`: 62 points, radius 192, admitting 2,190 of the graph's 14,249 cells |

**The physics already matched.** [HANDOFF.md](../HANDOFF.md) recorded that the
route runs used `pmove_fixed 1` and the search figures did not. That is wrong
for the benchmark arm: `Svcmd_AddRunner_f` sets `sv_fps 40`, `pmove_fixed 1`,
`pmove_msec 8` and `pmove_float 1` in every bench world, which is exactly what
`human_route_record.py` sets for a recording. The retained floor logs show both
changes before the corner start. No physics correction is owed to the
comparison below.

## What the guidance is, and what it is not

`tools/movement_corridor.py` reads a `BOT_ROUTE2` asset and writes a polyline
of its contract marks: **positions only**. No commands, timing, velocities,
jump latch or states cross into the bot. `Graph_CorridorLoad` marks, once per
load, which graph cells lie within the radius of that polyline; while a
corridor is loaded both `Graph_Path` and `Graph_PlanState` refuse to expand
any other cell, the goal cell excepted. Everything else is the unchanged
search: its own state-space plan, its own straight-hop candidates, its own
native rollouts from the live state, its own committed sequences carrying
actual native successor states. Nothing is reset between maneuvers.

With `bot_moveCorridor` unset nothing is loaded and `Graph_CorridorAdmits`
returns 1 for every cell, so the unguided search is the same code running
unrestricted. Measured: the diagnostic build with no corridor reproduces the
repaired control run for run - 19,150 / 19,600 / 19,150 ms with identical
commit, planned-edge and learned-edge counts.

## The arms

Three repetitions each, one dedicated localhost world per run, all retained.

| Arm | Build | Corridor | Times (ms) | Parity | Status |
| --- | --- | --- | --- | --- | --- |
| Repaired control | build-d | none | 20,700 / 19,150 / 19,150 / 19,600 / 19,600 / 19,150 | 0 mismatches | 6/6 pass |
| Control | build-a | none | 19,150 / 19,600 / 19,150 | 0 mismatches | 3/3 pass |
| Guided | build-a | 62 points, r=192 | 20,675 / 20,500 / 20,500 | 0 mismatches | 3/3 pass |
| Human reference | - | - | 14,800 | - | complete |

No deaths, no forbidden spend, `force 0` throughout, one owner (`move`).

## The guidance took effect

Distance from the human polyline, over the completed run:

| Arm | median | p90 | max |
| --- | --- | --- | --- |
| Guided | 70.0 / 77.6 / 77.9 | 101 / 111 / 112 | 124 / 148 / 147 |
| Control | 96.6 / 106.7 / 96.6 | 362 / 415 / 362 | 660 / 646 / 660 |

The guided runs stay inside the corridor the whole way; the unguided search
leaves the human's line by up to 660 units. So the guided arm really did run
the human's route, and it is 5.7 seconds slower on it than the human was.

**The corridor costs the bot time rather than saving it.** Guided is
1,350 ms slower than unguided on the same build. The human's line is not the
fastest line available to this vocabulary, which is a fact about the
vocabulary, not a reason to widen the corridor.

**Nor is the radius the limit.** The same corridor at half and double the
width changes the admitted cells by a factor of three and the time by almost
nothing:

| radius | admitted cells | time |
| --- | --- | --- |
| 96 | 1,100 of 14,249 | 20,175 ms |
| 192 | 2,190 | 20,675 / 20,500 / 20,500 ms |
| 384 | 3,426 | 20,725 ms |

A wider corridor does not buy back the 5.7 seconds, so the guided arm is not
slow because the guidance fenced off the cells it needed.

## Where the 5.7 seconds go

Speed over the completed run, from the play logs:

| | median | p90 | max | airborne |
| --- | --- | --- | --- | --- |
| Human | 541 | 693 | 725 | 93.2% |
| Guided | 361-373 | 458-475 | 541-557 | 81% |
| Control | 371-380 | 481-499 | 545-567 | 83% |

The bot is airborne for most of the run and its chained hops do accelerate:
62 -> 265, 254 -> 364 -> 506, 344 -> 418 -> 475 -> 520, 432 -> 491 -> 527.
When the next tap happens on the landing frame it pays no ground friction and
the chain keeps everything: the join at `land 5700 (364.7)` to
`launch 5725 (364.7)` is one command frame and loses nothing.

Every other join is a catastrophe for speed. A grounded frame runs at the
walk's 250 wishspeed, and `PM_Friction` removes roughly `speed * 6 * dt` per
frame, so 125 ms on the ground takes 527 units/sec down to 255:

| Arm | joins | on the landing frame | mean kept | delayed | median gap | median loss | total lost |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Guided r1 | 20 | 10 | 10.2 | 10 | 150 ms | 182.5 | 1,652 |
| Guided r2/r3 | 21 | 12 | 0.4 | 9 | 125 ms | 186.6 | 1,654 |
| Control r1/r3 | 19 | 12 | 8.8 | 7 | 325 ms | 190.3 | 1,334 |
| Control r2 | 18 | 10 | 0.0 | 8 | 175 ms | 184.5 | 1,525 |

## Why the chain breaks: the candidates at a fast landing

Read with `bot_moveReplay=1` and `bot_forecastTriggerLog=1`
(`corridor-a-guided-trig-r1`, 20,725 ms, same arm, 262 candidates over the
completed run).

### What a refusal means

`unsupported` is a bitmask of the reasons the forecast stopped
([bot2_forecast.h:48](../../../codemp/game/bot2_forecast.h)). `256` is
`BOT2_FORECAST_TRIGGER`: the swept hull came within 15 units horizontally,
24 below or 40 above a trigger entity's bounds, and the rollout ended there.
It says the forecast stopped, not that the maneuver failed. **Every refused
candidate in the run carried exactly `256` and no other bit** - there was not
one `SOLID`, `BODY` or policy refusal - so the earlier reading of these as
failed landings was wrong in both directions: they are trigger stops, and 13
of the 77 had already completed their first landing before the rollout ended.

The 312 trigger contacts name four entities:

| Entity | contacts | what the contact is |
| --- | --- | --- |
| `team_CTF_blueflag` (83), z ~1924 | 226 | the objective. The rollout is refused for reaching the goal |
| `team_CTF_redflag` (82), z ~1924 | 39 | the home stand, at the start of the run |
| `trigger_hurt` (48), z 701-723 | 38 | the map's pit volume, 1,100 units below the route: **the rollout fell** |
| `item_medpak_instant` (53), z ~1370 | 9 | a pickup on the route the forecast will not model |

### The three kinds of non-admission, kept apart

Over all 262 candidates of the completed run:

| Class | count | what it means |
| --- | --- | --- |
| admitted | 91 | committable |
| **selected-path rejection** | 92 | `off_path`/`no_path`/walk-corridor with `unsupported=0`: the flight simulated cleanly and the judge rejected it against the planned path |
| **unsupported simulation** | 39 | stopped on an item or flag contact: the candidate is *unknown*, not bad |
| **physical failure** | 18 | fell into the hurt volume. The refusal is correct and must stay |
| mixed trigger frame | 22 | both a fall and an item/flag contact at that search frame |

Trigger attribution is per search frame, not per candidate: the candidate
lines are printed in a batch after all rollouts, so a `[FORECAST] trigger`
line cannot be tied to one candidate. The class counts above inherit that
limit; the per-join table below is the primary evidence.

### The delayed joins, one line each

| landing | speed | next launch | gap | tap-now candidates | triggers at that frame | class |
| --- | --- | --- | --- | --- | --- | --- |
| 6275 | 521.7 | 254.4 | 650 ms | 6 refused | hurt x6, medpak x8 | fall + unsupported |
| 8550 | 435.4 | 250.7 | 125 ms | 6 `off_path`, `unsupported=0` | - | selected-path |
| 11125 | 500.1 | 235.2 | 125 ms | 6 refused | hurt x6 | physical failure |
| 14525 | 538.6 | 245.1 | 100 ms | 8 refused | hurt x8 | physical failure |
| 16250 | 429.4 | 253.8 | 125 ms | 6 `off_path`, `unsupported=0` | - | selected-path |
| 18000 | 447.2 | 259.0 | 375 ms | 6 `off_path`, `unsupported=0` | - | selected-path |
| 19175 | 381.2 | 254.9 | 350 ms | **none searched** | - | coverage gap |
| 21150 | 445.1 | 242.4 | 125 ms | 6 refused | hurt x8, blueflag x2 | fall + unsupported |
| 22025 | 323.3 | 243.6 | 150 ms | 8 `off_path`, `unsupported=0` | blueflag x2 | selected-path |
| 23450 | 360.2 | 254.3 | 125 ms | 7 refused | blueflag x14 | unsupported (the goal) |

Speed given up, by class: physical falls about 1,028 units/sec over four
joins, selected-path rejection 629 over four, the objective-contact refusal
106, the coverage gap 126.

**So the earlier single explanation is replaced by three.** At four joins the
fast continuation falls into the pit and the refusal is right - the bot has
no *other* fast option there, which is a vocabulary question and not an
admission one. Two of those four, 6275 and 21150, are mixed trigger frames
carrying an item or flag contact as well as the fall, and trigger attribution
is per search frame rather than per candidate: there the fall is the likely
cause and not a measured one, so the 1,028 units/sec charged to physical
falls is an upper bound. At four more the flight is physically fine and is rejected
only for leaving the planned path. At one the rollout is stopped for touching
the enemy flag, which is the objective. The admitted candidates are
systematically the slow ones: entry speed median 250 and maximum 447, against
262 and 539 for the refused.

The heading fan is about 9-13 degrees wide at every entry speed above 200
units/sec (tap-now candidates: 11.4 degrees in the 200-299 band, 12.8 in
400-499, 10.9 in 500-599), and a flight rides its fixed hop heading to the
ground. The committed sequence also holds at most two hops
(`bot2_movePlan_t.hop[2]`), so a third jump always needs a fresh commit on the
landing frame.

## Landing-to-search coverage, from the events

The earlier claim - "not one landing frame skipped its search" - is withdrawn.
It rested on the absence of `[REPLAY] kind=skip` lines, and that diagnostic is
gated on `StraightLanding()`, which requires the executor to have reached the
*onward* stage. **That fired once in the whole run**: 27 `crossing=land`
records against a single `crossing=complete`. Worse, the one completion is not
a landing: it is at level 6775 at 250.4 units/sec, 500 ms after the physical
landing at 6275 at 521.7, because the onward stage begins only once the walk
has covered `MOVE_ONWARD_DISTANCE` from the mark. The skip instrumentation
cannot see the frame this diagnostic is about.

Counting the actual events instead - every `crossing=land` whose hop is the
plan's last:

| | |
| --- | --- |
| physical landings of a plan's last hop | 13 |
| with a search on that frame | 11 |
| with a recorded skip reason | 0 |
| **silent: no search and no reason** | **2** (level 19175 at 381.2, level 24375 at 352.9) |
| intermediate landings, a hop still to fly, correctly not searched | 9 |

At 19175 the bot lands at 381.2 units/sec on plan 10 and the next movement
event is 200 ms later at 253.6, with nothing in between.

### The coverage hole, found and closed

`move-cover-build-c` names every branch that can take a landing frame without
searching: the four early returns in the parity hook before the discovery
dispatch (`no_parity_horizon`, `explore`, `frame_not_owned`, `speed_request`),
the two searches that take the frame instead of the straight search
(`spike_took_frame`, `planned_took_frame`), the two unlogged gates in
`StraightEligible` (`planner_off`, `straight_off`), and **the three silent
exits inside `StraightSearch` itself** (`no_straight_ahead`,
`straight_too_short`/`goal_too_near`, `gap_corridor_refusal`), which return
after the search has claimed the frame but before it generates a candidate.
Those last three were the ones dropping the silent frames.

Over three guided runs on that build, **40 last-hop landings and not one
silent frame**:

| | |
| --- | --- |
| searched | 33 |
| declined, `straight_too_short` | 4 |
| declined, `off_path_index` | 3 |
| **silent** | **0** |

Both named reasons are path causes. `off_path_index` is the executor's
progress index at -1 - the bot landed off the planned path, so the search is
declined outright - and `straight_too_short` is the planned path offering no
straight long enough to hop along. Together with the 92 candidates rejected as
`off_path`, **all three ways a fast landing loses its continuation trace back
to the planned path rather than to the maneuver.**

### The instrumentation had to be moved before it could be believed

Printing those three reasons where they occur costs **2,000 ms**: the guided
arm went to 22,700 ms, reproducibly, against its 20,500-20,725. The effect
is reproducible and its mechanism is **unresolved**. The first reading of it
- that the search's rollouts run against a wall-clock budget, so telemetry
inside that window changes the result it reports on - is not supported by
the implementation: the forecast bounds a rollout by step counts, and the
clock readings inside it measure cost rather than cap it. The measurement
stands; the explanation is withdrawn. The reasons are now recorded by the search
and printed by the caller after it returns, and the guided arm reproduces the
reference runs exactly - 20,725 with 15/7/41/12 and 20,675 with 17/5/46/16,
the same times and the same commit, planned-edge and learned-edge counts as
the uninstrumented arm. The 36-case floor on that build is 36/36 green.

**Carry this forward:** a diagnostic that prints inside the search measures a
different bot, by a mechanism not yet established. Record and print outside it.

## Verdicts, kept apart

- **Diagnostic (expressiveness):** answered for the corridor question - the
  corridor is not the limit - and the losses at the joins are now classified
  rather than explained by one cause. The largest single class whose refusal
  is *not* correct behaviour is the selected-path rejection of clean fast
  flights, on a classification whose two mixed-trigger joins are not
  attributed candidate by candidate. No conclusion here licenses ignoring triggers or relaxing any
  admission test: the pit refusals are a safety gate working.
- **Independent movement quality:** unguided, matched, repaired, `force 0`
  from the stand, the search completes in **19,150-19,600 ms** over three
  repetitions on this build and 19,150-20,700 over six on the repaired
  control. The approximately 15-second target is **not met**. These are a
  single case and a single start family; the 36-case floor is the safety
  gate, not a performance claim.
- **Runtime qualification:** untouched. Every run here needs the
  `--parity 256` fixture to dispatch discovery, which the method names as an
  implementation boundary to remove. No CPU budget, multi-bot cost or
  production scheduling result is claimed.
- **Prepared plans:** untouched. Nothing here selects, enters or departs a
  prepared plan.

## Regression

`.codex-research/corridor-a-floor-r1`: the standard 36-case floor on the
diagnostic build with no corridor loaded - **36 of 36 pass, 0 failed,
0 incomplete, green**, and no deaths. No green case turned red.

The floor qualifies behaviour and not parity. With `--parity 256` set, the
run recorded **0 comparisons** over all 36 cases against 6,475 refusals, so
the zero mismatches it also reports say nothing: no frame was compared.

The corridor is inert unless `bot_moveCorridor` names a file that binds to the
running map and checksum; a rebuilt graph clears the admission marks and the
next plan reloads it. `bot_moveCorridor` is unset everywhere but this
diagnostic, and it is guidance for a diagnostic, never a movement policy.

## Retained

- `.codex-research/guided-control-d-r1..r6` - repaired-control arm, before any
  source change
- `.codex-research/corridor-a-control-r1..r3` - unguided arm on the diagnostic
  build
- `.codex-research/corridor-a-guided-r1..r3` - guided arm
- `.codex-research/corridor-a-guided-r96`, `-r384` - the radius arms
- `.codex-research/corridor-a-guided-replay-r1` - the guided arm with
  `bot_moveReplay=1`
- `.codex-research/corridor-a-guided-trig-r1` - the same with
  `bot_forecastTriggerLog=1`, which carries the refusal classification,
  the trigger identities and the coverage audit above
- `.codex-research/corridor-a-floor-r1` - the floor regression
- `.codex-research/corridors/red_out_1789686390.txt` - the corridor, naming its
  source asset by hash
- `.codex-research/move-corridor-build-a` - the build and its source manifest
- `.codex-research/move-reroute-fix-build-a` - the build carrying the two
  reroute corrections, and its source manifest
- `.codex-research/reroute-fix-floor-r1` - the 36-case floor on that build,
  36/36 green

## The directed experiment: the selected-path rejection, measured

Run on `move-cover-build-c`, `red_out_L1_stand`, unguided, **eight sequential
repetitions per arm alternating between them** - parallel batches were tried
first and are not independent samples (below). `bot_moveReroute=1` re-plans
from each candidate's measured landing state to the same goal and prices it as
rollout time plus the remaining path from there, so a fast flight that leaves
the pre-landing path is judged against a path that matches where it actually
goes. No admission is loosened and no trigger is ignored: every candidate
still passes the same native rollout and the same test.

| Arm | n | times (ms) | median | mean | joins kept on the landing frame | units/sec lost |
| --- | --- | --- | --- | --- | --- | --- |
| control | 8 | 19,150 / 19,350 / 19,350 / 19,600 / 20,700 x4 | 20,150 | 20,031 | 53% | 1,591 |
| `bot_moveReroute=1` | 8 | 18,675 / 20,325 x3 / 20,400 x3 / 22,000 | 20,362 | 20,356 | **61%** | **1,527** |

**It does what it was built to do and it does not make the bot faster.** The
reroute pass keeps more joins on the landing frame and gives up less speed,
and the completed stand time is unchanged to slightly worse - median +212 ms,
mean +325 ms, inside a spread the control itself covers. This is a repaired
control, which the stale-slot repair left owing for this pass.

`bot_moveReroute=3` (adding route ranking) was measured over three runs at
20,800-21,600 with 47% of joins kept, worse than the control on every measure;
it was not carried to eight.

The result that matters beyond this case: **speed kept at a join is not the
same as time saved.** What the experiment does *not* establish is that the
selected-path rejection has been addressed. The pass as measured forecast
each candidate's continuation against the path the flight leaves and then
installed a different one (see the correction below), so it could refuse a
useful replacement and could admit a continuation it never judged. The times
stand as times; the mechanism they were produced by is not the one this
section described. Two questions stay open together - what the kept speed is
pointing *at*, the refined method's "speed pointing into a dead end is not
valuable", and whether a reroute that judges the continuation it will fly
moves the route time at all.

### A sampling correction that invalidated an earlier read of this arm

The first measurement of these arms was three parallel repetitions each and
read `bot_moveReroute=1` as **925 ms faster** (18,675 x3 against 19,600 x3),
perfectly reproducible. It was an artifact. Worlds started in one parallel
batch land on the same outcome with identical commit, planned-edge and
learned-edge counts: every world in the batch sees the same load, and by what
mechanism that load reaches the simulation is unresolved, as above. The
dependence is measured; its cause is not. Sequential runs of the
same build and settings spread about 1,550 ms (19,150 / 20,175 / 20,700).
A second parallel batch of the same arms reversed the sign. Only the
sequential, alternating samples above are evidence; parallel batches remain
fine for the 36-case floor, whose verdict is pass/fail rather than time.

### A correction to the reroute pass, found in review

A review of the implementation after these runs found two defects in it.
Both are fixed in the tree. **Neither fix is measured: every number in this
section was produced by the build that carries both defects.**

- **The installed route could belong to a different candidate.** The route
  was planned from the winner's landing *before* `StatePreferred` could
  change the winner, and installed afterwards. A search in which the
  preference moved the winner installed the previous winner's route - or,
  where the build had not run that search at all, whatever an earlier search
  had left in the shared buffer. The build now runs after the last thing
  that can change the winner, and the installer refuses a route that was not
  built for the candidate being committed.
- **The pass forecast one continuation and executed another.** A rerouted
  candidate was rolled out with the landing's path test suspended but with
  the *old* path still steering everything after the last landing, and the
  replacement route was constructed and installed only afterwards, never
  forecast. The onward stage and the hold that decided the candidate were
  therefore not the ones it would fly. The pass now rolls the candidate out
  a second time with the replacement route in place and no permit - the
  state `RerouteInstall` leaves behind - and judges that rollout; a landing
  that does not repeat refuses the candidate rather than backing it with a
  route planned from a landing it no longer makes.

Whether either changes the arm's time is open, in both directions: the first
defect admitted routes that were never validated, the second refused
continuations that may have been fine. Both fixes are inert with
`bot_moveReroute` unset, which is the default; the 36-case floor on the
corrected build is **36 of 36 green**
(`.codex-research/reroute-fix-floor-r1`, `move-reroute-fix-build-a`), and it
records 0 parity comparisons against 6,614 refusals, exactly as the
diagnostic build's floor did. The zero-comparison floor is a harness question
of its own, now tracked in the plan.

### The re-measurement on the corrected pass

Run on `move-reroute-fix-build-a`, `red_out_L1_stand`, unguided, `force 0`
from the stand, **nine sequential repetitions alternating between the arms**,
same settings as the arms above (`--edges --parity 256`, the
`move-explore-20260916-b` seed, timescale 5). Repetition 6's control run lost
its fixture start to an RCON refusal (`corner start refused`, an environment
failure with no movement in it); that repetition is dropped from **both** arms
so every reported pair is a pair, and repetition 9 was run to replace it. The
dropped reroute run was 19,050 ms, the fastest value the arm produces, so
dropping it does not flatter the pass.

| Arm | n | times (ms) | median | mean | joins kept | units/sec lost |
| --- | --- | --- | --- | --- | --- | --- |
| control | 8 | 19,150 x3 / 19,600 x4 / 20,700 | 19,600 | 19,569 | 55% | 1,923 |
| `bot_moveReroute=1` | 8 | 19,050 x2 / 19,550 x3 / 19,800 / 20,325 / 20,475 | **19,550** | 19,669 | 57% | 2,004 |

Paired by repetition, control against reroute:

| rep | control | reroute | delta |
| --- | --- | --- | --- |
| 1 | 19,150 | 19,050 | -100 |
| 2 | 20,700 | 19,800 | -900 |
| 3 | 19,150 | 19,550 | +400 |
| 4 | 19,600 | 20,475 | +875 |
| 5 | 19,600 | 19,050 | -550 |
| 7 | 19,150 | 20,325 | +1,175 |
| 8 | 19,600 | 19,550 | -50 |
| 9 | 19,600 | 19,550 | -50 |

**The corrected pass is indistinguishable from the control.** Mean delta
+100 ms, median -50 ms, five of eight repetitions faster, and the per-pair
deltas run from -900 to +1,175 - inside the 1,550 ms spread a single arm
produces on this case. The defective pass measured +325 ms on its mean; the
corrected one measures +100 on its mean and -50 on its median. **Neither
number resolves anything at this sample size.** What can be said is narrower
and firmer than before: correcting the two defects did not turn the pass into
a win, and it did not make it worse either.

The pass is doing less work than it was. Over eight runs it installs **11**
alternative routes against the defective pass's 18 over its own eight, which
is what the revalidation predicts: continuations the old pass admitted on a
rollout it never flew are now judged against the route they would actually
fly, and some of them do not survive. Both new guards stayed silent - **0**
`revalidate=drift` refusals and **0** `reroute mismatch` refusals over every
run - so the installed route was always the winner's own and the landing
always repeated on the second pass. Every run passed, no deaths, and the
parity harness recorded no mismatches (and, as everywhere, no comparisons).

One reporting correction. The joins figures above use the definition this
record's own tables imply - a landing followed by a launch, kept when that
launch is issued on the landing's own frame - and on the **original** arms
that definition gives 52% control against 56% reroute, not the 53% against
61% stated earlier. Counting instead the commits whose re-issued frame is a
landing gives 50% against 41%, which reverses the sign. The 61% is not
reproducible from the retained logs by either reading, so the joins-kept
claim should be treated as unqualified until the metric is defined in code.
The route times are exactly reproducible: `sq-off-r1..r8` and `sq-rr1-r1..r8`
are the original arms' worlds and they give the published medians and means.

Retained: `.codex-research/rrfix-off-r1..r9`, `.codex-research/rrfix-1-r1..r9`
(`-r6` is the dropped repetition, kept as it fell).

## Next

The three items this record directed - make the landing frame auditable,
measure the selected-path rejection, then re-measure it once the pass was
corrected - **are all done above**.

The re-measurement settled which of the two open hypotheses to carry. A
reroute that judges the continuation it will fly is *still* not faster than
the control, so the continuation's validity was not what stood between the
kept speed and the saved time.

**The later momentum interpretation was corrected after review.**
[What the kept speed buys](2026-09-19-what-the-kept-speed-buys.md) now binds
complete landing windows to the measured outbound leg and reports duplicate
traces. Its earlier 4 Hz correlation and heading-exclusion claims are
withdrawn pending a reproducible bounded analysis. The corrected trace
supports a grounded-speed-loss association; it does not rule out heading
or establish that a particular admission change saves time.

Leave the pit refusals alone - they are a safety gate doing its job. The
objective-contact refusal (the rollout stopped for reaching the enemy flag)
and the item stop are real but small, about 106 units/sec at one join; note
them, do not spend a campaign on them.

The two [repair follow-ups](2026-09-19-repair-follow-up.md) stay tracked
separately and are not prerequisites for either.
