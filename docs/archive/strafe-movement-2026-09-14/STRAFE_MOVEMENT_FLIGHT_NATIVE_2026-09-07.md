# Held-out native validation of the baked flight artifact

**Later review correction:** this is a historical development log, including
cases reused during tuning. Follow the
[runtime review/work order](STRAFE_MOVEMENT_RUNTIME_REVIEW_2026-09-07.md) for
current direction. Independent raw-data review confirms the harm improvement
but finds runtime10% quality still unmet and idle worse in five of six cases.
Preparation is outside dispatch timing; its cost belongs in the5 ms maximum.
All224 missing-dispatch frames coincide with fewer living clients, not a flight
counter bypass. Endpoint-only guard batching does not preserve immediate
mutation detection and is not approved by the current plan. Earlier statements
below that K05/cost is complete or that batching necessarily closes it are
superseded. No historical captures or failed reports have been overwritten.

Dated protocol and result, 2026-09-07 UTC. This is the K05 item the offline work
could not answer: at real production entries, does the baked ranked list still
contain a candidate the native oracle accepts, does the chosen one behave, and
does execution cost what the arithmetic predicted?

**Answer, after three iterations: the artifact loads and executes natively, the
search reduction is better than predicted, and the adapter now passes its harm
gate — zero deaths, every capture the control scored, and a faster trip in all
twelve comparisons.** The first iteration completed 24 of 45 executions and
killed the bot twelve times; diagnosis traced every death to one cause, and two
further iterations removed it. Nothing was installed and no setting was promoted;
both cvars still default to off, and the outstanding cost item is unchanged.

Read this document in order: the failing measurement, the diagnosis, then each
iteration and what it did and did not establish.

## What was run

| Input | Path |
|---|---|
| Build, iteration 1 | `.codex-research/strafe-build-adapter-v2-cd03b1a9d0d2483097fda500cbeb73a8` |
| Build, iteration 2 | `.codex-research/strafe-build-flight-skew-v2-20260907` (superseding `…-skew-20260907`) |
| Build, iteration 3 | `.codex-research/strafe-build-flight-recover-20260907`, then `…-skew2-20260907` |
| Payload, both | `.codex-research/strafe-flight-artifact-4a724cbaf7da4b9b92f790a672313738/flight.txt` (9,207 bytes, sha256 `0b4c9b03…8703`) |
| Outcome collection, iteration 1 | `.codex-research/strafe-flight-native-20260907` |
| Outcome collection, iteration 2 | `.codex-research/strafe-flight-native-skew-v2-20260907` (and `…-skew-20260907`) |
| Outcome collections, iteration 3 | `.codex-research/strafe-flight-native-recover-20260907`, `…-native-skew2-20260907` |
| Cost collections | `.codex-research/strafe-flight-cost-20260907`, `…-cost-skew-v2-20260907`, `…-cost-skew2-20260907` |
| Adapter checks | `.codex-research/strafe-flight-adapter-check-lf-20260907`, `…-check-skew2-20260907` |
| Setup smoke check, not evidence | `.codex-research/strafe-flight-native-smoke-20260907` |

At iteration 1 the tree still hashed identically to that build's 1,100-entry
source manifest, so those runs measure the code as compiled; iteration 2 was
built fresh from the changed tree. All twenty-four outcome cases report
`unchanged_sources: true`.

### The case set, declared before collection

`tools/strafe_flight_native_campaign.py --action freeze` wrote
`plan.json` + `plan.sha256` before any world started; `--action run` refuses to
launch unless the frozen plan still hashes and re-declares to the identical
command inventory. The plan carries the gate text itself, so the failure
definitions are frozen with the commands.

Twelve cases: two teams × phases 0/3/6 × two arms, ports 31600–31611, four
workers, 120 game seconds each, `--accelerated`. Both arms are the ordinary
production world used by the K03 gate's baseline runs — `--hop-probe 6
--supported-hop 2 --supported-corners 2 --supported-trace --initial-force 0
--speed-expiry 0` — with the payload staged in **both**. The only difference is
`bot_flightAdapter`. The control is therefore not a different world: it is the
same world with execution disabled, so a difference is attributable to the
adapter and not to staging, build or seed.

Declared failures: any `artifact_refused`; zero prepared entries; an entered
window that does not reach `exit` with `resumed=1`; more than four rollouts per
entry; and any loss of captures, pickups, trip time or lives against the matched
control.

## Result 1 — it loads and runs in normal dispatch

All six adapter cases printed `[FLIGHT] artifact_loaded … windows=31 regions=41
checksum=2993043243` and produced entries. No `artifact_refused`, no
`bind_refused`, no `build_refused`, no `forecast_world_refused` in any case.
This is the first time a bot has executed a cold-loaded prepared window in a
live match rather than under the discovery fixture.

## Result 2 — the search reduction is real, and better than predicted

**Every one of the 45 entries cost exactly one 256-step rollout.** Forty-four
took the rank-0 candidate; one (blue window 0, which has four baked candidates)
took rank 3 after the three ahead of it had no valid prepared takeoff and were
skipped without a rollout.

Discovery spends 17 rollouts per root. The offline arithmetic predicted 12 per
red round trip and 9 per blue. Measured is 1 per entry, so a red round trip of
five entries costs five rollouts rather than 12. The ranked list is doing its
job: the first entry that has a prepared takeoff is almost always accepted.

Nine window admissions produced no entry at all: seven spent zero rollouts
because ground preparation yielded no valid takeoff, exactly as designed, and
two spent one rollout that the native oracle then refused (`status=3
unsupported=512`). Those two are the held-out check working — a baked candidate
declined at a live entry it was never observed at.

## Result 3 — 24 of 45 executions complete; 21 refuse early

| Outcome | Count | Live duration |
|---|---|---|
| `release reason=complete`, all with `resumed=1` | 24 | 2300–2900 ms |
| `release reason=refused` | 21 | 350–625 ms (one at 2075 ms) |

The distribution is bimodal with nothing in between. A completion runs for
exactly the number of frames its rollout predicted (predicted 92–117 frames,
observed 2300–2925 ms) — when the plan holds, it holds to the frame. A refusal
happens 14–25 frames in, during the ground run-up, before the first jump.

Per window, the split is not random: windows 17, 21, 2, 20, 29 completed every
time (14 entries), while window 26 refused all four times and windows 1, 10, 11,
27 refused their single entry. Windows 0, 4, 7, 9, 19, 25 did both — the same
baked candidate at the same window succeeded from one live entry and failed from
another.

## Result 4 — the adapter is a net regression, and the deaths have one cause

| Case | First pickup | First capture | Second pickup | Second capture | Deaths |
|---|---|---|---|---|---|
| red-control-p0 | 28625 | 57225 | 85750 | 114150 | 0 |
| red-adapter-p0 | 47575 | 76125 | — | — | 3 |
| red-control-p3 | 28675 | 57375 | 86075 | 114700 | 0 |
| red-adapter-p3 | **24475** | **51950** | 76350 | — | 1 |
| red-control-p6 | 28675 | 57275 | 85875 | 114425 | 0 |
| red-adapter-p6 | **24500** | — | 63475 | — | 2 |
| blue-control-p0 | 28575 | 57050 | 85700 | 114250 | 0 |
| blue-adapter-p0 | 28425 | — | — | — | 2 |
| blue-control-p3 | 28575 | 57200 | 85675 | 114325 | 0 |
| blue-adapter-p3 | 28450 | — | 66125 | — | 2 |
| blue-control-p6 | 28450 | 56975 | 85425 | 113725 | 0 |
| blue-adapter-p6 | 55750 | — | — | — | 2 |

Times are milliseconds from trial start. The six controls are identical to each
other and reproduce the K03 baseline exactly: two pickups, two captures, no
deaths, first capture at 56975–57375 ms.

The upside is real where flights complete: red at phases 3 and 6 reached the flag
at 24475/24500 ms against a control of 28675, and red-adapter-p3 captured at
51950 ms against 57375 — a 9.5% first-trip improvement, the same size K03
measured offline. The artifact can deliver what it was built to deliver.

The downside is larger. **All twelve deaths follow a `release reason=refused`
by 4.6–5.9 seconds, with no other flight event in between.** The trace through
`continuous.csv` shows the mechanism (blue-adapter-p6, window 4, entered 22250):

```
 22775  ground=1022  z=1808.1  vz=0      speed=310   <- run-up, still on floor
 22800  ground=1023  z=1808.1  vz=-6.4   speed=312   <- walked off the ledge, never jumped
 22825  ground=1023  z=1807.5  vz=-32.0  speed=306   <- RELEASED reason=refused
 …                                                      free fall for ~4.6 s
 27475                                                  death
```

The run-up accelerates the bot from 250 to 310 units/s toward the launch point,
fails to launch, walks off the edge, and the kernel refuses on the next frame
because it is airborne in a ground phase. The adapter then hands the follower
back to `Bot2_PathCommand`, which has no answer for a client in free fall over a
pit. The refusal is a correct refusal; releasing there is not survivable.

Of the 21 refusals, 13 left the client airborne and 8 left it grounded. **Twelve
of the thirteen airborne refusals ended in death; none of the eight grounded ones
cost anything.** So the adapter has two distinct defects, and the second is the
one that costs lives:

1. **The run-up does not reach a launch the kernel will accept**, in 21 of 45
   executions.
2. **Release has no floor.** The ordinary fallback chain is safe for a grounded
   client and fatal for an airborne one.

### The mechanism behind defect 1, from the code

Preparation runs at the Pmove boundary of frame *T*, after that frame's command
already exists, so the adapter cannot own frame *T* — every entry logs
`latency=25`, one game frame. But the rollout that validated the candidate ran
with `request->firstTime = level.time` (`ai_bot2_flight.c:211`), i.e. it
simulated the plan starting at *T*.

Two things follow, and both are visible in the source rather than inferred:

- The prepared policy carries `policy.lastTime = level.time - Bot2_GameStepMsec()`
  (`ai_bot2_flight.c:339`). In the rollout the first command is emitted at *T*,
  so `EmitCommand` computes `dt = 25 ms`. Live, the first command is emitted at
  *T+25* against the same stored `lastTime`, so **`dt = 50 ms`** — the first
  live command turns up to twice as far as the validated one, because
  `Bot2_MoveControl` bounds yaw by `turnRate * dt`.
- The live state at *T+25* is the result of one ordinary path-following frame,
  not of the rollout's own step 0. At 250–310 units/s that is 6–8 units of
  position, against the takeoff-boundary tolerance of 12 units and 40 units/s
  (`bot2_flight.c:200-202`).

The discovery bench has the same one-frame offset and compensates for it: at
selection it adopts `connection.policy = selected[0].afterPolicy`
(`bot2_connectionbench.c:861`), advancing the executed policy past the step the
frame consumed. The adapter stores the root policy unadvanced. That difference
is the most likely reason discovery's candidates behaved and the adapter's
replays of the same candidates do not.

This is a hypothesis with code evidence, not a measured cause. It predicts that
the refusals concentrate at the takeoff-boundary check in `bot2_flight.c:199-203`
and would be confirmed by instrumenting the refusal with its phase and the
takeoff deltas.

## Result 5 — cost: execution is free, preparation is a 17 ms spike

Two eight-bot runs on the same build and flags, run alone and sequentially, one
with the adapter and one without, telemetry off so timing claims stand
(`tools/strafe_cost_runner.py`, ports 31700/31701, audited by
`strafe_cost_report.py`).

| Metric | Control | Adapter | Plan gate |
|---|---|---|---|
| Movement dispatch p95 | 0.876 ms | 0.884 ms | ≤2.5 ms |
| Movement dispatch max | 1.446 ms | 1.692 ms | ≤5 ms |
| Server frame p95 | 1.561 ms | 1.494 ms | — |
| Server frame max | 3.001 ms | **16.967 ms** | — |
| `eight_bot_budget_pass` | true | **false** | — |

Judged on the metric the plan's budget names — movement dispatch — the adapter
passes with room. Steady-state execution is not measurable above the control.

Judged on the whole server frame, preparation is not cheap and must be reported
separately, as the guide required. There were five prepared entries, at level
20900, 22300, 26950, 27475 and 29800. **The five most expensive server frames in
the run are exactly those five level times, at 15.3–17.0 ms.** Every other frame
is ≤2.1 ms, indistinguishable from the control. Two 64-step ground prefixes plus
a 256-step rollout cost about two thirds of a 25 ms frame at 40 Hz.

That is survivable for one client and unbudgeted for several: nothing serialises
preparation across clients. The per-client retry backoff is one second, which
does not prevent eight clients from preparing in the same frame — eight
preparations would be roughly 120 ms in a 25 ms frame. The guide's item 4 asked
for this work to sit "under the shared per-frame scheduler"; it does not.

`eight_bot_budget_pass` is false for a separate reason worth stating plainly:
`all_clients_dispatched` is false because 267 frames dispatched seven of eight
clients. Those are the frames where the flight adapter owned a client's command,
so the movement dispatch counter did not see it. That is an ownership/counter
interaction rather than a dropped client, but the audit is reported as measured
and not explained away.

## What this establishes and what it does not

Established: the payload loads under the live engine checksum and physics; a
production bot executes baked windows; the ranked list is accepted by the native
oracle at held-out entries at one rollout each; completions match their rollout
frame for frame; and the trip improvement K03 measured offline appears in
production when a flight completes.

Not established, and now measured against: that the adapter is safe or useful as
it stands. It is neither. It loses captures in five of six cases, adds one to
three deaths where the control has none, and its preparation is an unscheduled
17 ms frame spike.

Retained gaps unchanged by this work: the native pickup-skip lifecycle branch is
still uncovered, and the two map-checksum sign conventions still coexist.

## The mechanism, isolated: which takeoff boundary the candidate uses

`candidate & 1` selects the prepared takeoff boundary
(`bot2_flight.c:193`): bit clear takes `takeoffs[speed][0]`, the **last grounded
state** of the ground prefix — the lip — and bit set takes `takeoffs[speed][1]`,
one frame earlier. Splitting the 45 entries on that single bit separates the
fatal cases completely:

| Boundary | Complete | Refused, grounded | Refused, rising | **Refused, falling** |
|---|---|---|---|---|
| `[0]` lip | 11 | 6 | 1 | **12** |
| `[1]` one frame of margin | 13 | 2 | 0 | **0** |

Every fatal refusal used the lip boundary, and the margin boundary produced none
at all while completing 13 of 15. A refusal with the client grounded costs
nothing: the ordinary follower simply resumes.

That is the one-frame execution skew made visible. A boundary taken from the last
grounded frame has exactly zero margin for an adapter that executes one frame
after the rollout that validated it: the run-up reaches the lip before `along`
reaches `targetAlong`, the launch check never fires, and the client walks off.

## Second iteration: the skew compensated

Three changes to `ai_bot2_flight.c`, built as
`.codex-research/strafe-build-flight-skew-20260907`:

1. `FLIGHT_BOUNDARY_SKEW 1` — both prepared boundaries move one frame back from
   the last grounded prefix state, handing the adapter back the frame it loses to
   the Pmove ordering. The rollout still validates whatever that produces, so the
   failure mode of being too conservative is fewer entries, not more falls.
2. `policy.lastTime = level.time` when the plan is committed, so the first
   executed command is integrated over one game frame instead of two.
3. A distinct `refused_falling` release reason, so the unrecoverable case can
   never hide inside a refusal count again.

`tools/strafe_flight_adapter_check.py` covers both behaviours and now passes
**37** mocked scenarios: a two-state prefix no longer yields a boundary while a
three-state one does, and the first executed command is asserted to carry
`dt = 25 ms`.

The identical frozen case set was re-run on that build
(`.codex-research/strafe-flight-native-skew-20260907`, ports 31620–31631). Same
payload, same controls, same twelve cases.

It was then collected a **second** time. The first skew build was compiled from
sources a patching script had rewritten with CRLF line endings, against the LF
the rest of the tree uses; normalising them changed every touched file's hash and
so broke the freeze the collection was bound to. Rather than argue that the
content was equivalent, the build and all twelve cases were redone from the
normalised sources as `.codex-research/strafe-build-flight-skew-v2-20260907` and
`.codex-research/strafe-flight-native-skew-v2-20260907` (ports 31640–31651).
**Every case reproduced exactly** — the same pickup, capture and death times, the
same 71 entries, 43 completions, 26 refusals and 2 falling refusals — which is
also a useful determinism check on the collection itself. The numbers below are
that second collection; the first is retained.

| Measure | First iteration | After the fix | Control |
|---|---|---|---|
| Entries prepared and entered | 45 | 71 | — |
| Completions with `resumed=1` | 24 (53%) | 43 (61%) | — |
| Refusals with the client falling | 12 | **2** | — |
| Deaths | 12 | **3** | 0 |
| Cases whose first capture beats its control | 1 of 6 | **6 of 6** | — |
| Captures scored (of 12) | 1 | 9 | 12 |
| Rollouts per entry | 1.00 | 1.04 (68 at one, 3 at two) | — |

First capture, adapter against its matched control:

| Team | Phase | Control | Adapter | Improvement |
|---|---|---|---|---|
| red | 0 | 57225 | 51775 | 9.52% |
| red | 3 | 57375 | 53925 | 6.01% |
| red | 6 | 57275 | 56025 | 2.18% |
| blue | 0 | 57050 | 52000 | 8.85% |
| blue | 3 | 57200 | 55000 | 3.85% |
| blue | 6 | 56975 | 53700 | 5.75% |

Where the second trip also completed it improved too: red p0 114150→105450
(7.62%), red p3 114700→105900 (7.67%), blue p3 114325→108275 (5.29%).

**The harm gate is still not met at this iteration.** Three deaths against a
control's zero, and three cases lost their second capture to one. All three
follow a refusal 4.7–5.5 s earlier that left the client airborne, and the
adapter released it there. An earlier reading of this collection called those
post-launch refusals, inferred from a `vz` sample in the capture; the engine's
own telemetry in the next iteration reports `phase=0` for all three — the same
unlaunched lip problem, absorbed for ten of thirteen entries by one frame of
margin but not for these.

Cost was re-measured on the fixed build as a matched pair, each run alone:

| Metric | Control | Adapter |
|---|---|---|
| Movement dispatch p95 | 0.883 ms | 0.787 ms |
| Movement dispatch max | 1.631 ms | 1.047 ms |
| Server frame max | 2.497 ms | **16.834 ms** |
| `eight_bot_budget_pass` | true | true |

The four most expensive server frames in the adapter run are again exactly its
four preparation frames (15.4–16.8 ms), against a control whose worst frame is
2.5 ms. One earlier adapter cost run is retained and **not** used: two of its
frames advanced 250 ms and 50 ms instead of 25, a host stall, so
`frame_samples_verified` is false
(`.codex-research/strafe-flight-cost-skew-20260907/adapter`).

## Third iteration: a release floor, and the margin the residual actually needed

Two changes were made and measured **separately**, because they address different
things and only one of them turned out to matter.

### The release floor, which engages correctly and saves nothing

`Bot2_FlightRecoverCommand` is exported from `bot2_flight.c` in the same shape as
`Bot2_FlightRunupCommand`: it steers an airborne client toward a finite landing
range using the same descending projection, preferred speed and lateral
correction the refused candidate flew with, never requests a jump, advances no
phase, and refuses atomically when the range cannot be reached. The adapter keeps
a client that refuses in the air (new `FLIGHT_RECOVER` state, bounded to 3000 ms)
instead of handing it to `Bot2_PathCommand`, and releases with `recovered`,
`recover_refused` or `recover_expired`. Those replace iteration 2's
`refused_falling`, which named a case that can no longer occur: a refusal is now
either grounded and harmless, or it enters recovery.

Measured on the same frozen case set
(`.codex-research/strafe-flight-native-recover-20260907`): **every case is
identical to iteration 2 — same times, same three deaths.** Recovery engaged
exactly three times, on exactly the three fatal entries, and refused immediately
each time.

That is a useful negative result, and it corrected a mistake in the analysis
above. The three `[FLIGHT] recover` lines all carry **`phase=0`**: the client had
never launched. An earlier reading of this document said the residual deaths
followed a refusal *after* the launch, inferred from a `vz` sample in the capture;
the engine's own phase says otherwise. Without a jump there is no arc, so
`AirIntent` correctly declines — no steering recovers a bot that walked off a
ledge at 310 units/s. The floor is retained anyway: it is the right invariant for
the post-launch case, which will eventually happen, and it declines honestly
rather than pretending.

### The margin, which is what the residual needed

The residual is therefore the same lip problem as before, just rarer: one frame
of margin absorbed ten of thirteen, not all of them. `FLIGHT_BOUNDARY_SKEW` was
raised to **2** and the frozen case set re-run
(`.codex-research/strafe-flight-native-skew2-20260907`, ports 31680–31691). This
is a declared two-point comparison of a constant whose meaning is known — how
much execution drift the prepared boundary absorbs — and both points are reported.

| Measure | Iteration 1 | Skew 1 | Skew 2 | Control |
|---|---|---|---|---|
| Entries prepared and entered | 45 | 71 | 79 | — |
| Completions with `resumed=1` | 24 | 43 | 53 | — |
| Refusals, client grounded (harmless) | 8 | 26 | 25 | — |
| Refusals, client airborne | 13 | 3 | 1 | — |
| **Deaths** | 12 | 3 | **0** | 0 |
| **Captures scored, of 12** | 1 | 9 | **12** | 12 |
| Rollouts per entry | 1.00 | 1.04 | 1.09 | — |

**The harm gate passes.** No case lost a capture, a pickup or a life, and every
one of the twelve capture comparisons is faster than its matched control:

| Team | Phase | First capture | | | Second capture | | |
|---|---|---|---|---|---|---|---|
| | | control | adapter | gain | control | adapter | gain |
| red | 0 | 57225 | 54600 | 4.59% | 114150 | 106175 | 6.99% |
| red | 3 | 57375 | 54500 | 5.01% | 114700 | 105225 | 8.26% |
| red | 6 | 57275 | 54650 | 4.58% | 114425 | 105775 | 7.56% |
| blue | 0 | 57050 | 56350 | 1.23% | 114250 | 111950 | 2.01% |
| blue | 3 | 57200 | 54000 | 5.59% | 114325 | 109925 | 3.85% |
| blue | 6 | 56975 | 49975 | 12.29% | 113725 | 105000 | 7.67% |

Median gain: first capture 4.59% red and 5.59% blue; second capture 7.56% red and
3.85% blue. That is production dispatch on a cold-loaded artifact, not the
discovery oracle, and it is smaller than K03's offline 10.74%/12.71% — as it
should be, since those measured a different quantity under the 17-candidate
search.

Cost on the final build, matched pair, each run alone with telemetry off:

| Metric | Control | Adapter | Plan gate |
|---|---|---|---|
| Movement dispatch p95 | 0.896 ms | 0.833 ms | ≤2.5 ms |
| Movement dispatch max | 1.032 ms | 0.970 ms | ≤5 ms |
| Server frame max | 2.453 ms | **16.804 ms** | — |
| `eight_bot_budget_pass` | true | true | — |

Unchanged from the earlier measurement, and unchanged in its one problem: the
four most expensive server frames in the adapter run are exactly its four
preparation frames, 15.9–16.8 ms of a 25 ms frame, and nothing serialises
preparation across clients.

`tools/strafe_flight_adapter_check.py` covers the recovery lifecycle and the
boundary arithmetic and passes **41** mocked scenarios; the pure kernel check
(576 range-feedback, 216 run-up and 576 prepared-takeoff cases) still passes with
the new export.

## Fourth iteration: preparation is scheduled, and its cost is measured

The last open K05 item was that preparation cost 15-17 ms of a 25 ms server
frame and nothing serialised it across clients. Two changes to
`ai_bot2_flight.c` close it and, more usefully, replace the inference with a
measurement.

`FLIGHT_PREPARATIONS_PER_FRAME` is 1. `ClaimPreparation` keys a shared counter
on `level.time` and admits one preparation per server frame across all clients,
the same shape as the supported adapter's own navigation-query budget
(`ai_bot2_supported.c:373-379`). The claim is taken after the window lookup
succeeds and before anything is spent, so a deferred client spends nothing: no
prefix, no rollout, no attempt, and **its retry clock is not touched**. Deferral
therefore costs one frame, not `FLIGHT_RETRY_MSEC`, and no client starves --
the client that did prepare is the one backed off for a second.

Every forecast run in a preparation now goes through `RunForecast`, which sets
`request->measureTime` and accumulates `timing.totalUs`, `count` and a run
counter. Each preparation that spent anything emits one unconditional
`[FLIGHT] prepare_cost` line with its outcome, forecast-run count, step count
and microseconds. Unconditional matters: a timing collection has telemetry off,
and that is exactly the collection whose cost this describes.

### What the schedule is, and is not, evidence for

`tools/strafe_flight_adapter_check.py` passes **42** mocked scenarios, one more
than before: two clients admitted on the same frame, exactly one preparing,
the other logging `prepare_deferred`, spending no forecast run, keeping its
retry clock and preparing on the very next frame.

That scenario is load-bearing rather than vacuous. With
`FLIGHT_PREPARATIONS_PER_FRAME` widened to 8 and nothing else changed, the check
fails at exactly that assertion; the failing run is retained in
`.codex-research/strafe-flight-schedule-counterexample-20260907/` with its
`check.log` and a note.

**Natively, the schedule has never engaged.** Across the eight-bot cost run and
all six adapter cases of the frozen twelve, `deferrals` is 0 and
`busiest_frame` is 1. That is not a surprise and it is not a pass: no retained
collection has ever recorded two preparations on one frame -- the eight-bot run
produces 8 preparations in 800 frames, and the twelve cases fly one bot each.
The schedule bounds a projected stall, not an observed one, and the mocked
scenario is what establishes the mechanism.

### Cost, now measured directly rather than inferred from frame maxima

Eight-bot timing pair on build
`.codex-research/strafe-build-flight-schedule-20260907`, run alone and
sequentially with telemetry off, ports 31721/31741
(`.codex-research/strafe-flight-cost-schedule-20260907`).

| Metric | Control | Adapter | Plan gate |
|---|---|---|---|
| Movement dispatch p95 | 0.875 ms | 0.864 ms | <=2.5 ms |
| Movement dispatch max | 1.246 ms | 1.054 ms | <=5 ms |
| Server frame max | 2.684 ms | **18.687 ms** | -- |
| `eight_bot_budget_pass` | true | true | -- |

Eight preparations, every one of them three forecast runs, costing **12.9 to
18.0 ms** each; the run's worst server frame, 18.687 ms, is the frame carrying
the 18.047 ms preparation. The earlier 15.3-17.3 ms figure was read off server
frame maxima; this is the preparation reporting itself, and it agrees.

### A first control run failed the budget gate, for an unrelated reason

The first control arm (port 31731, retained as `control/`) reports
`eight_bot_budget_pass` **false**: movement dispatch max 47.621 ms and server
frame max 49.520 ms. It is one frame, at `level_time` 23325, between neighbours
of 244 and 599 microseconds, and the whole of it is `localnav_us` 47419.6 on
client 1 -- `ai_bot2_localnav.c`, with `bot_flightAdapter` 0 and the report
recording `artifact_loaded: false`. No flight code can run in that arm.

Across the twelve retained eight-bot collections, 9,600 sampled frames, this is
the only local-navigation sample above 5 ms; every other collection maxes
between 0.55 and 1.6 ms. The arm was re-run unchanged (port 31741) and came back
at 1.246 ms movement max and 2.684 ms frame max. Both control runs are retained.
The failure is reported, not excluded, and it is not attributed to the adapter;
what it is remains open, and it is 2.6 times larger than the preparation spike
this iteration was about.

### The frozen twelve cases, re-run: outcomes unchanged

`.codex-research/strafe-flight-native-schedule-20260907`, ports 31760-31771,
same frozen plan shape, same payload, same twelve cases.

| Measure | Skew 2 | Schedule | Control |
|---|---|---|---|
| Entries prepared and entered | 79 | 79 | -- |
| Completions with `resumed=1` | 53 | 53 | -- |
| Airborne refusals entering recovery | 1 | 1 | -- |
| **Deaths** | 0 | **0** | 0 |
| **Captures scored, of 12** | 12 | **12** | 12 |

The harm gate still passes and every one of the twelve capture comparisons is
faster than its matched control:

| Team | Phase | First capture | | | Second capture | | |
|---|---|---|---|---|---|---|---|
| | | control | adapter | gain | control | adapter | gain |
| red | 0 | 65425 | 62800 | 4.01% | 122350 | 114375 | 6.52% |
| red | 3 | 65650 | 62775 | 4.38% | 122975 | 113500 | 7.70% |
| red | 6 | 61825 | 59200 | 4.25% | 118975 | 110325 | 7.27% |
| blue | 0 | 61450 | 60750 | 1.14% | 118650 | 116350 | 1.94% |
| blue | 3 | 61675 | 58675 | 4.86% | 118800 | 114600 | 3.54% |
| blue | 6 | 61525 | 54525 | 11.38% | 118275 | 109550 | 7.38% |

These are raw `level_time` values from `continuous.csv`, which include the
pre-match interval; the earlier tables in this document subtract it, so **do not
compare these numbers to the skew-2 table above directly**. The same script over
the retained skew-2 collection gives 4.25%/4.63%/4.55% red and
1.46%/4.86%/11.66% blue on first capture, so the two collections are directly
comparable to each other and the schedule changed no outcome. That is the
expected result: `deferrals` is 0, so the schedule never engaged in these runs.

### The new cost finding: preparation scales with forecast runs, and the bound is over a frame

The `prepare_cost` lines make the shape of the cost visible for the first time.
Across the six adapter cases, 100 preparations:

| Forecast runs | n | Median | Max | Median steps |
|---|---|---|---|---|
| 2 (two ground prefixes, no candidate) | 9 | 8.4 ms | 11.1 ms | 45 |
| 3 (prefixes + one candidate rollout) | 84 | 16.0 ms | 21.7 ms | 141 |
| 4 (prefixes + two candidate rollouts) | 7 | 24.3 ms | 28.5 ms | 251 |

**These twelve cases are `--accelerated` with telemetry on, so the microseconds
are indicative and not timing evidence**; the counts are not affected by either.
The scaling is nevertheless unambiguous and matches the mechanism: cost is about
8 ms per forecast run here and about 5 ms per run in the valid timing collection,
and step count explains little of the variance -- an 83-step preparation cost
14.2 ms and a 156-step one 15.3 ms. `Bot2_ForecastRun` copies every entity and
hashes the world twice per call, and that per-run fixed cost dominates.

Two consequences, both newly measured and both open:

1. **A single client can exceed the server frame.** The baked list holds at most
   four candidates, so a preparation is bounded at six forecast runs. Four runs
   were observed seven times and already median 24.3 ms. At the valid
   collection's ~5 ms per run, six runs is roughly 32 ms of a 25 ms frame -- from
   one client, which a one-per-frame schedule does not prevent. Splitting a
   preparation across frames is not sound: the rollouts are rooted at a live
   `pmove_t` and re-rooting revalidates nothing.
2. **One of the two ground prefixes may be dead weight.** `PrepareTakeoffs`
   always runs both speed variants, and the prefixes are 2 of every
   preparation's forecast runs -- the whole cost of the 9 preparations that
   yielded no takeoff at all. In all 16 windows entered across these runs,
   **every chosen candidate used speed variant 1; not one used variant 0.**
   That is what was chosen, not what was offered, so it does not yet prove
   variant 0 is unused. Preparing only the speed variants the baked candidate
   list actually references would cut a typical preparation from three forecast
   runs to two -- roughly 18 ms to 12 ms -- and is the cheapest available lever.

## Fifth iteration: prepare only the ground prefixes the baked list can use

The fourth iteration's instrumentation showed preparation cost scaling with the
number of forecast runs, and named the cheapest lever: `PrepareTakeoffs` ran both
speed variants unconditionally, whatever the baked ranked list actually
referenced. This iteration takes it.

`PrefixMask` reads the mask straight from the baked window. Bit 1 of a candidate
id chooses the run-up speed and bit 0 chooses which of that prefix's two
boundaries to launch from, so **one prefix serves both boundaries of one speed**.
`SelectCandidate` returns at the first point id, so candidates at or after that
rank can never be examined and their speed is not needed; a list whose first
entry is a point id needs no prefix at all and is now refused for free instead of
after two ground rollouts.

The equivalence is structural rather than statistical. `window->takeoffs` is
zeroed before preparation, so a skipped speed is indistinguishable from one whose
prefix ran and found no boundary — a state `SelectCandidate` already handles by
skipping the candidate. Every read of `takeoffs` in the tree is indexed by a
selected candidate (`ai_bot2_flight.c:292` and `:375`, `bot2_flight.c:193`), and
the mask loop stops on exactly the condition `SelectCandidate` stops on. No reachable
candidate loses its takeoff.

### What the baked payload actually needs

Reading the retained 9,207-byte payload's 31 windows:

| Ground prefixes a window needs | Windows |
|---|---|
| One | **28** (27 use speed variant 1, one uses variant 0) |
| Both | 1 |
| None (a point id is ranked first) | 2 |

Prefix forecast runs across the window set fall from 62 to 30. Per-window
worst-case forecast runs, counting both prefixes and every reachable candidate:

| Worst case | Before | After |
|---|---|---|
| 0 runs | — | 2 windows |
| 2 runs | 2 windows | 26 windows |
| 3 runs | 26 windows | — |
| 4 runs | — | 2 windows |
| 5 runs | 2 windows | — |
| 6 runs | 1 window | 1 window |

**The bound does not move.** One window — blue, outbound leg, ranked candidates
`[2, 3, 6, 1]` — references both speeds and has four reachable candidates, so it
can still require six forecast runs. Everything else improves.

### Measured: the typical preparation is a third cheaper

Eight-bot timing pair on `.codex-research/strafe-build-flight-prefix-20260907`,
run alone and sequentially with telemetry off, ports 31781/31801
(`.codex-research/strafe-flight-cost-prefix-20260907`).

| Metric | Schedule build | Prefix build |
|---|---|---|
| Preparations | 8 | 8 |
| Forecast runs each | 3 | **2** |
| Median preparation | 15.45 ms | **10.29 ms** |
| Worst preparation | 18.05 ms | **12.08 ms** |
| Total preparation | 124.0 ms | **81.6 ms** |
| Server frame max | 18.687 ms | **12.868 ms** |
| Movement dispatch p95 / max | 0.864 / 1.054 ms | 0.863 / 1.102 ms |
| `eight_bot_budget_pass` | true | true |

Both runs produced the same eight preparations, on the same windows, in the same
order, with the same four `prepared` and four `entry_refused` outcomes. The
matched control passes (movement p95 0.493 ms, max 0.910 ms; frame max 1.741 ms).

### The frozen twelve, again: identical outcomes, a third less work

`.codex-research/strafe-flight-native-prefix-20260907`, ports 31820-31831.

| Measure | Skew 2 | Schedule | Prefix | Control |
|---|---|---|---|---|
| Entries prepared and entered | 79 | 79 | **79** | — |
| Completions with `resumed=1` | 53 | 53 | **53** | — |
| Airborne refusals entering recovery | 1 | 1 | **1** | — |
| **Deaths** | 0 | 0 | **0** | 0 |
| **Captures scored, of 12** | 12 | 12 | **12** | 12 |

Every one of the twelve capture comparisons is again faster than its matched
control, 1.06% to 11.38%. The lifecycle numbers are identical across three
builds, which is what an equivalence claim should look like.

The work behind them is not identical:

| | Schedule build | Prefix build |
|---|---|---|
| Preparations that spent anything | 100 | **91** |
| Total forecast runs | 298 | **198** |
| Runs per preparation | 2 x9, 3 x84, 4 x7 | 2 x76, 3 x14, 4 x1 |

**34% fewer forecast runs**, matching the timing collection's 34% drop in total
preparation microseconds. The nine preparations that vanished were all window 15
— `team=1 leg=0`, ranked candidates `[11]`, a point id — which previously spent
two ground rollouts on every attempt before refusing, and now refuses for free.
No other window disappeared.

`tools/strafe_flight_adapter_check.py` passes **43** mocked scenarios: the
synthetic payload gained a third window whose only candidate is speed variant 0,
and the check asserts the exact prefix mask in both directions — variant 1 only
for window 0, variant 0 only for window 2, and neither for the point-first
window, which also takes no preparation slot from another client.

### A second control arm failed its audit, again for a host reason

The first prefix-build control (port 31791, retained as `control/`) reports
`eight_bot_budget_pass` false through `frame_samples_verified`, not through any
cost gate: movement dispatch max 1.099 ms and server frame max 2.280 ms, both
well inside the gates, but one server frame at `level_time` 21575 advanced
`simulation_advance_ms` 50 instead of 25. That frame itself cost 1.19 ms, so it
is a tick the host did not deliver on time rather than work the game did. The
audit is right to refuse it: a dropped tick invalidates the sampling assumption.
Re-run unchanged (port 31801), the arm verifies and passes.

That is the second control arm in this campaign to fail for a reason unrelated
to flight — the previous one was a single 47.6 ms local-navigation frame. Both
are retained, both are in the arm where the adapter does nothing, and neither is
attributed to the adapter. Two host artifacts in four control runs is worth
noting for anyone reading later collections from this machine.

## Sixth: the two changes audited, and what that corrected

The schedule and prefix changes were reviewed adversarially before being relied
on further: six independent dimensions over the changed C, the mocked check, the
Python tooling and the documented numbers, each finding then attacked by three
verifiers with distinct lenses (does the code path really do this / is the quoted
evidence accurate / is it caused by these changes at all), majority to survive.
31 findings were raised and 5 survived.

**The audit was not complete.** Nineteen verifier agents failed on a session
limit, including all three verifiers for the docs-versus-evidence dimension and
the completeness critic. Those findings were dropped as unverified rather than
refuted, so the fact-check of this document's numbers has no independent
confirmation; its findings were triaged by hand instead, and two of them - a
wrong source citation and an overstated reduction - are corrected below.

### Three test gaps, closed and proven load-bearing

The mocked check passed 43 scenarios while three separate halves of the two
changes were untested. Each is now covered, and each cover was verified by
mutating the source, re-running, and confirming the check fails:

| Mutation | Before | Now |
|---|---|---|
| Delete `request->measureTime=qtrue` | passed | **KILLED** |
| Delete the per-preparation `memset(&spent,...)` | passed | **KILLED** |
| `PrefixMask` reads only `candidates[0]` | passed | **KILLED** |

Each mutation is a real production defect that the check would have blessed.
Without `measureTime` the real `Bot2_ForecastRun` leaves `timing.totalUs` at
zero, so **every `prepare_cost` line would have reported `us=0`** - the mock had
been filling the field unconditionally, so the flag that makes the whole
measurement work was decorative. Without the reset, the accumulator is never
scoped to one preparation, so the second preparation of a session reports the
running total. And a mask reading only the first ranked candidate silently drops
a reachable candidate's takeoff.

That last one had no fixture at all: the synthetic payload contained no window
whose reachable candidates span both speeds, which is the only configuration in
which prefix selection can go wrong. Window 2 now ranks `[1, 2]` - variant 0
then variant 1 - so the mask must return both bits, and the check asserts
`prefixCalls==2 && prefixSpeeds==3` in one scenario and `prefixSpeeds==2` for
window 0 in another, pinning it from both sides. **44 scenarios.** The source was
restored to its exact pre-mutation sha256 after the run.

### A parser defect found by writing the test for it

`preparation_costs` matched `us=(\d+)`, which **prefix-matches**: a malformed
`us=1.62e4` parsed as `us=1` rather than failing, recording a one-microsecond
preparation instead of a drift. The numeric fields are now anchored, and the
parser reports `parsed_all` / `unparsed` so a C format change that outruns the
regex is visible instead of silently producing quiet zeros. Re-parsing every
retained log reproduces its report's numbers exactly.

### Where preparation cost actually goes: measured, and it is not the search

The cost instrumentation established that preparation scales with forecast runs
rather than steps, but not what a run spends its time on. A throwaway diagnostic
build set `request->profile` and accumulated the per-part breakdown
`Bot2_ForecastRun` already produces; the eight-bot run below is that build, and
the source was reverted to its exact sha256 afterwards. Nothing here is promoted.

Eight preparations, two forecast runs each:

| Part | Range | Share |
|---|---|---|
| **Integrity guard (pre + post)** | 6364-7414 us | **62-76%** |
| Policy callback (the flight controller) | 1552-5075 us | 15-39% |
| **Actual Pmove** | 264-676 us | **3-5%** |
| World copy | 131-448 us | 1-4% |
| Restore | 0 us | 0% |

Profiling adds per-step clock reads, which inflate the policy and Pmove parts,
so the guard's share is if anything understated.

The mechanism is in `bot2_forecast.c`: every run calls `WorldHash()` plus
`HashBytes(&level, sizeof(level))` **twice** - once before the rollout, once
after - and `WorldHash` is a byte-at-a-time FNV-1a over the whole entity array
plus each client. Measured sizes in this build: `sizeof(gentity_t)` 1520,
`MAX_GENTITIES` 1024, so 1,556,480 bytes of entities, plus 8 x 7,368 bytes of
`gclient_t`, plus 322,328 bytes of `level_locals_t` - **1.85 MB per guard,
3.7 MB per forecast run.** At this machine's measured FNV throughput
(~1.17 GB/s, best of five over 0.5-4 MB) that is ~3.2 ms, which is what the
profile measured.

**So the preparation spike is about two thirds integrity guard and 3-5% actual
simulation.** The artifact already did its job - the search is down to one or
two rollouts from discovery's seventeen - and what dominates the remaining cost
is a debug guard, re-hashing an unchanged world once per rollout.

That reframes the last open cost item. The guard is per-run, but within one
preparation every rollout starts from the same root and the world is restored
between them, so hashing once before the first and once after the last still
detects any mutation; the adapter's `WorldIntact` already discards the whole
preparation rather than attributing a mutation to a particular rollout.
Amortising the guard across a preparation would take the six-run worst case from
about 30 ms to roughly **13 ms - inside a 25 ms frame** - and a typical two-run
preparation from about 10 ms to 6.8 ms. That is a change to a shared kernel bound
to the campaign's measured evidence, so it needs its own package with an opt-in
scope, an equivalence proof and re-validation; it is not done here.

### Correction: `eight_bot_budget_pass` is not a stable adapter indicator

Three adapter timing arms, one per build:

| Build | Movement p95 | Movement max | Frame max | Owned frames in sample | `eight_bot_budget_pass` |
|---|---|---|---|---|---|
| schedule | 0.864 ms | 1.054 ms | 18.687 ms | 0 | true |
| prefix | 0.863 ms | 1.102 ms | 12.868 ms | 0 | true |
| audit | 0.881 ms | 0.972 ms | 12.634 ms | **224** | **false** |

The audit arm is a comment-only change from the prefix arm, so the difference is
not behavioural. `eight_bot_budget_pass` folds in `all_clients_dispatched`, which
is false on exactly the frames where the flight adapter owns a client's command -
the movement dispatch counter does not see an owned client. This run had five
entries whose flights fell inside the 800-frame sample; the earlier two had four
whose flights did not.

**The gate the plan names - movement dispatch p95 and max - passes on all three,
at 0.863-0.881 ms p95 against 2.5 ms and 0.972-1.102 ms max against 5 ms.** The
earlier statements that the adapter "passes the eight-bot budget" were true as
written but partly luck: had a flight overlapped those samples, they would have
read false for the same counter reason, already recorded under Result 5. Report
the dispatch distribution, not the composite flag, when the adapter is armed.

The prefix build's frame-max improvement reproduces independently here:
18.687 ms on the schedule build against 12.868 and 12.634 on the two builds with
prefix selection.

### Two documentation errors the audit caught

The equivalence argument cited `ai_bot2_flight.c:270,349` as the places
`takeoffs` is read. Those were pre-patch line numbers, and 349 was inside
`PrepareTakeoffs`, which is the one site *not* indexed by a selected candidate.
The reads are `ai_bot2_flight.c:292` (the rollout callback) and `375`
(`SelectCandidate`), plus `bot2_flight.c:193`; the writes are 336 and 355. The
claim is unchanged, its citation was wrong.

`PrefixMask`'s own comment claimed the change "halves" a preparation in the same
sentence that gave the numbers refuting it - three forecast runs to two is a
third, and the measured aggregate was 34%. Corrected in the source, along with
the adapter header's contract block, which still promised "the two ground
prefixes" unconditionally.

### The lazy-prefix option, quantified and declined

Prefixes are prepared eagerly for every speed the reachable list references. They
could instead be run only when `SelectCandidate` first needs one. Costed against
the retained collection rather than assumed: it would save **9 forecast runs of
198, 4.5%, all of them on window 0**, and leave the worst case unchanged for
every window (window 0 eager 6, lazy 6). Window 0's variant-0 candidate - id 1,
ranked last, the sole reason it needs a second prefix - was never selected or
even reached across 100 attempts; candidates 2 and 3 always won. The change would
interleave prefix extraction with candidate rollouts through the shared
`rollout[]` buffer for a gain that does not move the bound, so it is recorded
here rather than made. Guard amortisation is the better target by an order of
magnitude.

## Seventh: complete accounting (R1), and what the corrected number is

Executed after the runtime review, on a fresh build
(`.codex-research/strafe-build-r1-accounting-65095a074e25442da4aefde6f39f6627`,
1,100 native sources bound). No control policy, candidate order, physics or
threshold changed; only measurement, reporting and tests.

**Change.** `Bot2_MoveBenchPmove` now clocks the whole
`Bot2_FlightAdapterBeforePmove` call and charges it through
`Bot2_MoveCostPreparation` to the cost fixture's current frame and client, so
`movement_us` is `dispatch_us+prepare_us`. Every early refusal, the first-use
artifact parse, receiver binding, window build, both prefixes, every candidate
rollout and the publication are inside that interval; flight command execution
and airborne recovery were already inside dispatch and are not counted twice.
The adapter adds the oracle's own rollout time as `forecast_us`, counts
admissions, deferrals, library loads and free refusals per frame, and routes
its unconditional prints through a buffer that is flushed after recording. The
metadata declares `accounting=complete_movement_v2` and the measured clock
read cost (0.028 us). A capture without the declaration is dispatch-only and
cannot qualify anything.

**Evidence.** `.codex-research/strafe-flight-cost-r1-20260907-v2/`, eight bots,
timing alone, adapter and control arms on the same build and payload.

| Arm | Complete movement p95 / max | Dispatch alone p95 / max | Preparation max | Server frame max | Budget |
|---|---:|---:|---:|---:|---|
| adapter | 0.727 / **12.650 ms** | 0.713 / 1.007 ms | 12.360 ms (rollouts 12.334 ms) | 12.985 ms | fails on the 5 ms maximum only |
| control | 0.742 / 1.523 ms | 0.737 / 1.520 ms | 0.152 ms | 2.707 ms | passes |

Eight preparations ran in the adapter arm (8.8–12.4 ms each); each one's
`prepare_cost` line matches its frame row's `forecast_us` and sits inside that
row's `prepare_us`, and no frame admitted more than one. Preparation work that
is not rollout time is at most 0.06 ms per preparation, so the whole cost item
is the rollouts and the per-rollout world guard - R2's target - not the
adapter's bookkeeping. Both arms kept all eight clients alive and dispatched on
all 800 frames; the review's 224 lost-client frames did not recur here and are
not explained by this work.

The first collection on this change, `.codex-research/strafe-flight-cost-r1-20260907/`
(build `…-d7eb8507…`, adapter max 13.776 ms), is retained; its build differs
from the final one only by a header comment.

**Audit.** `tools/strafe_flight_native_report.py` over the review's twelve
frozen cases (`.codex-research/strafe-flight-native-report-20260907-v1/`)
recomputes from raw CSV and logs: 79 prepared and entered, 53 completed, 25
released `refused` after entry, one airborne recovery refused; 21 entries
refused before preparation and 19 candidate rollouts refused; 13 entries into
thinly observed windows. Load, reachability, work-count and harm predicates
pass; execution fails; first-trip medians improve 4.67% red and 5.35% blue
against the 10% gate; idle is worse in five pairs and walking better in six.
This matches the review and is reported predicate by predicate rather than as
one flag.

## Eighth: the guard replaced and the controller pruned (R2)

Executed after R1 on fresh builds; harnesses first, native cost last.

**Where the cost was.** The R1 arm's preparations were 8.8–12.4 ms for two
rollouts of 50–130 steps. The retained diagnostic profile attributed 6.7 ms
of a 10.9 ms preparation to the two whole-world FNV hashes per rollout and
3.2 ms to the policy callback - about 26 us per step against 2 us for Pmove.
The callback's cost was the local controller: 72 grid angles times 9 keys
per step, each calling `Wrap` twice, some 1,300 `fmodf` calls to keep about
six angles per key that the `fast` filter admits.

**Guard.** `Bot2_ForecastGuardBegin/End` take one immutable baseline of the
same bytes the hash covered and compare exactly after every rollout; a
contaminated scope executes nothing further, and End reports it. The legacy
path is unchanged for every other caller. `tools/strafe_forecast_guard_check.py`
compiles the real oracle against a mocked Pmove that mutates an entity, a
client, a non-clock level field, a clock or the live animation on a chosen
call: 17 scenarios including mutation in the first/middle/last rollout, an
undo that never executes, nested and reused scopes, a foreign root, every
early return, restoration, and byte-identical legacy equivalence.

**Controller.** The fast scan now finds each key's admissible arc once per
step by scanning outward from the nearest grid angle with the same `Wrap`
expression, then evaluates exactly the same candidates in the same order.
`tools/strafe_movecontrol_check.py` compares it against the verbatim unpruned
scan over 200,000 cases (large drifted yaws, zero to 2,000 deg/s turn budgets,
1–500 ms steps, limited and unlimited throttle, fast and exhaustive): bitwise
identical commands, yaw and work.

**Native cost, four arms on one build** (`strafe-build-r2-guard-b72e8685…`,
`.codex-research/strafe-flight-cost-r2-20260907-v2/`), eight bots, timing alone,
complete accounting, eight preparations per adapter arm, the same windows in
each, all eight clients alive on all 800 frames:

| Arm | Complete movement p95 / max | Preparation max | Server frame max | Budget |
|---|---:|---:|---:|---|
| scoped guard | 0.282 / **3.379 ms** | 3.075 ms | 4.004 ms | **pass** |
| scoped guard, exhaustive | 0.291 / 3.296 ms | 3.144 ms | 3.900 ms | pass |
| legacy guard | 0.303 / 8.718 ms | 8.546 ms | 9.139 ms | fails maximum |
| control | 0.276 / 0.673 ms | 0.143 ms | 2.083 ms | pass |

Per rollout the scoped guard costs a 1.94 MB snapshot once per preparation
(0.13–0.66 ms) plus a 0.2–0.7 ms compare; the per-step cost inside a rollout
is now 12–18 us. The earlier guard-only collection without the controller
pruning (`…-r2-20260907/`) measured 6.04 ms max: the guard alone halved
preparation, the controller pruning took the rest.

**What exhaustive did and did not reach.** Every window met in the collection
offered one candidate with a valid takeoff after its prefixes, so exhaustive
added only the second prefix (three forecasts, not six). The baked
two-prefix/four-candidate maximum was not reachable on these roots. At the
measured per-step cost a full 256-step rollout is about 4 ms, so a window
that needed several long rollouts would exceed the maximum; that case is
bounded by `bot_flightRolloutCap`, which reports every candidate it skips, and
is not measured here.

## Ninth: the latency modelled, the list re-baked from runtime evidence (R3)

Executed after R2 on fresh builds; diagnosis from the reviewed captures first,
one change per collection, the same twelve development cases until the last
step, then a frozen holdout run once.

**Attribution.** In the reviewed captures the forecast root was the previous
frame's post-state and the client moved about six units under the already
issued command before the first flight command ran. Every refused flight was
followed on the same frame by `[SUPPORTEDTRACE] release reason=interval_contract`:
the follower handed back had not been advanced during flight ownership, the
supported adapter rejected its own plan, waited its 1 s acquire backoff and
took 0.4–1.1 s more to be ready. Adapter idle exceeded control by 193 frames
across the six first trips; 280 adapter idle frames fell within 3 s after a
refused release and its remaining idle was lower than control's. The one
adapter trip with no refusal was 12.3% faster than its control.

**Latency model** (`.codex-research/strafe-flight-native-r3-20260907/`).
Rollouts start from the issued command; refusals report phase and geometry;
grounded refusals after the first landing try the receiver resume. Result on
the twelve development cases: 78 prepared, 72 complete, 6 still in flight when
the capture ended, **no refusal after entry at all**, idle and walking better
than control in every pair, medians 8.0% red / 9.1% blue. The remaining lost
time was 28 windows refused at preparation, concentrated on windows 1, 21
and 26.

**Oracle at actual roots** (`…-r3-oracle-20260907/`, `bot_flightExhaustive 2`).
Every rollout refusal was reported with its terminal phase: almost all were
phase 5, grounded, inside the second landing's along range but 70–97 units
lateral off its line. Rolling out every flight id at the same 96 preparation
roots (537 complete, 91 refused) showed the listed 1.25× ids failing where
base-speed ids completed at every root: window 1's only listed id completed
at 1 of 9 roots against ids 1, 4, 5 at 9 of 9; window 26's listed ids at 2 or
fewer of 6 against 0, 4, 5 at 6 of 6. That is the list deficiency the review
named as the condition for a re-bake.

**Re-bake** (`.codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/`).
`strafe_flight_artifact.py --runtime-oracle` reproduces the measured payload
byte-for-byte, then re-ranks the 13 windows met at three or more roots by
runtime completion with the original anchor rule; only `red:f06:v1` (window
21) has no id completing at every root and is flagged. Development cases with
it (`…-r3-rebake-20260907/`): 90 of 90 entered windows complete, 11.70% red /
10.60% blue.

**Holdout** (`…-r3-holdout-20260907/`, phases 1/4/7 frozen with
`--forecast-guard`, run once):

| Pair | Adapter first trip | Control | Gain | Idle | Walking |
|---|---:|---:|---:|---|---|
| red p1 | 51,325 ms | 57,200 ms | 10.3% | 3.60% vs 4.15% | 50.7% vs 58.3% |
| red p4 | 52,650 ms | 57,100 ms | 7.8% | 3.75% vs 4.12% | 51.4% vs 58.5% |
| red p7 | 50,800 ms | 57,250 ms | 11.3% | 3.59% vs 3.97% | 47.7% vs 58.8% |
| blue p1 | 50,350 ms | 57,325 ms | 12.2% | 3.62% vs 4.14% | 47.6% vs 58.3% |
| blue p4 | 50,550 ms | 57,125 ms | 11.5% | 3.61% vs 4.11% | 47.6% vs 58.5% |
| blue p7 | 50,075 ms | 56,900 ms | 12.0% | 3.74% vs 4.09% | 47.3% vs 58.6% |

Medians 10.27% red / 11.86% blue; 85 of 87 entered windows complete and two
cut off by the capture end; no deaths; captures and pickups equal to control
in every pair; every frozen predicate and the full-route gate pass. The
receiver-resume path was never exercised because no refusal after entry
occurred in any R3 collection.

**Cost with the re-baked artifact** (`.codex-research/strafe-flight-cost-r3-20260907/`,
build `strafe-build-r3-oracle-7851ea4040a949fe90b10b3dd3465464`): complete movement p95 0.314 / max 3.889 ms, server frame max
4.193 ms, all eight clients alive on every frame, one preparation spending six
forecasts (both prefixes and four candidates) in 3.72 ms - the maximal case
R2 could only bound - and the control at max 0.402 ms.

## Superseded proposed next steps (see current runtime review)

K05's outcome question is answered, its harm gate is met on three consecutive
builds, preparation is scheduled, measured, a third cheaper, and the two changes
have been adversarially audited. What remains:

1. **Amortise the forecast integrity guard.** Measured: 62-76% of preparation
   cost is two full-world FNV hashes per rollout, against 3-5% actual Pmove.
   Within one preparation the root is fixed and the world is restored between
   rollouts, so an opt-in guard scope hashing once per preparation would take the
   six-run worst case from ~30 ms to ~13 ms - inside the frame - and close the
   last cost item at its root. This is a `bot2_forecast.c` change bound to the
   campaign's measured evidence: it needs an opt-in flag leaving every existing
   caller byte-identical, an equivalence proof, and re-validation.
2. **Until then, one baked window can still overrun a frame.** Exactly one of the
   31 - blue, outbound leg, ranked candidates `[2, 3, 6, 1]` - references both
   speeds and has four reachable candidates, so it can require six forecast runs,
   about 30 ms. Every other window is now at most four and 26 are at two. No
   attempt in any collection has ever spent more than two rollouts.
3. **Finish the audit.** The docs-versus-evidence dimension's three verifiers and
   the completeness critic all failed on a session limit, so this document's
   numeric claims have had no independent fact-check. Re-run that dimension.
4. **Report the dispatch distribution, not `eight_bot_budget_pass`, when the
   adapter is armed.** The composite flag folds in `all_clients_dispatched`,
   which is false exactly when a flight overlaps the sampled window.
5. **The schedule has never engaged natively.** `deferrals` is 0 and
   `busiest_frame` is 1 in every collection. The mocked contention scenario and
   its retained counterexample establish the mechanism; no native run has
   contended for the frame.
6. **Two control arms failed their audits for host reasons.** A 47.6 ms
   `localnav_us` frame and a dropped server tick, both with flight disabled,
   neither reproducing. Re-run a failing control before reading anything into it,
   and retain both arms.
7. **Measure the release floor.** It exists, is bounded and declines correctly,
   but no post-launch airborne refusal has been observed natively. **K06**.
8. **`FLIGHT_BOUNDARY_SKEW` is measured at two points, not swept**, and **thin
   windows are still thin** - 19 of 31 have fewer than four observations.

Do not weaken the K03 gate or drop losing cases to obtain a pass.
