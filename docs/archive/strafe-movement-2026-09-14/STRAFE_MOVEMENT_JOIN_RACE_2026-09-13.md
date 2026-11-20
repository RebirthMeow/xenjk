# The missed fast-return join is a resource race, not a start phase — 2026-09-13

**Review correction and implemented follow-up:** the resource/radius mechanism
motivated the [bounded join repair](STRAFE_MOVEMENT_JOIN_APPROACH_2026-09-13.md).
It does not exclude build/startup effects on the state reaching that mechanism.
The older categorical exclusions below are superseded; no owner algorithm
choice is pending. Raw evidence and the original interpretation are retained.


Attributes R5-blue's 3.8-second loss, the first open item in the
[handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md). The
[degrade-release record](STRAFE_MOVEMENT_DEGRADE_RELEASE_2026-09-12.md#the-regressions-attributed)
attributed it to the phase the fixture started the world in and described the
mechanism as a 600 ms window; review called that attribution provisional because
every baseline world it rested on had acquired early. Both the attribution and the
mechanism are wrong, and the evidence to replace them was already retained.

Scope: reanalysis, plus one telemetry-only source change and the arm that
confirms it. Nothing installed and no contract, threshold or decision altered.
Twenty-six R5-blue worlds across seven builds — eighteen retained by five earlier
arms, eight collected here — the frozen catalog, and each world's own 40 Hz
momentum trace.

## What decides it

One number separates all twenty-six worlds: **the runner's distance to route 4's
sample 50 at the moment its Force reaches 70.**

| | Worlds | Distance at 70 Force | Capture |
|---|---|---|---|
| Joined | 21 | 110.0–129.5 units | 17.850–17.900 s |
| Missed | 5 | 130.6–132 units | 21.525–21.750 s |

The interpolated groups do not overlap. The 128-unit scan radius does NOT lie
between their extrema (129.5 and 130.6). Position estimates are interpolated from
250 ms play-track rows with several units of uncertainty; their approximately
1.2-unit separation cannot establish an actual geometric or five-millisecond
margin. Direct telemetry establishes resource refusal and joins at 70 Force,
not that claimed precision. The earlier margin interpretation is withdrawn.

Why 70: sample 50's group launches at a receiver 959.7 units away and level
(`dz=0.0`), so `FastReturnTakeoffAdmission` imposes its 20-Force receiver reserve
on top of the 50 a level-3 Speed cast. Every joined world joins holding exactly 70
and is left holding exactly 20. The reserve is measured and is not a tolerance to
relax — the record that added it measured a 12-Force level flight exhausting early
and missing.

## Why it is structural, not unlucky

The runner is never in a good position when it can afford the launch. At its
closest approach to sample 50 — 16 to 31 units, dead on the lane, on the ground,
at the ground cap — it holds **66 or 68 Force**, in every one of the eighteen
retained worlds this was measured over. It needs 70. So the join is never decided
while the runner is approaching the sample: in **0 of 18** was it still approaching
when the launch became affordable. Every join happens on the way out, 110 to 129
units past, on radius the runner has not yet spent. Five of the twenty-six worlds
run out of radius first.

Two consequences follow, and neither is a near miss:

* **Nothing re-opens the join.** The scan keeps running, but the published plan
  carries the runner away from the lane, and the next join in a missed world is at
  level 51025–53300 — a later return entirely. The loss is the whole first return:
  the plan walks it at 250 u/s down to 117.
* **The Force the runner was short of then regenerates and is never spent.** It
  climbs to 100 under a plan-owned command while the runner walks, because the
  executor zeroes `generic_cmd` on every frame it commands. The runner misses the
  launch by 2 Force and then banks 30 it has no use for.

## The kernel's own account of it

The attribution above is a reading of `FastReturnTakeoffAdmission` set against
retained traces. To check it against what the kernel says rather than what the
source implies, `FastReturnJoinSample` now reports the nearest sample each scan
weighed and the gate that refused it, and `FastReturnJoinScanRecord` prints one
line per distinct (sample, refusal) — the scan runs on every grounded frame, so a
refusal that stands for five seconds of walking would otherwise be two hundred
identical lines. It is reporting only: the candidate is weighed, refused and
recorded in the order it always was, and `tools/strafe_fast_return_check.py` passes
unchanged on the instrumented source.

The prediction was written before the arm ran. `FastReturnProjectedResources` models
a level-3 cast only at 60 Force or more, so the approach should refuse
`takeoff_flight_resources` below 60, `takeoff_receiver_reserve` from 60 to 69, and
admit at 70.

Eight worlds were collected, seven joined and one missed. **Every refusal on the
approach to sample 50 names `takeoff_receiver_reserve`, at 65 Force with 15
projected — in the world that missed exactly as in the seven that joined — and every
join fires at exactly 70 Force.** No other gate ever refused. A ninth world is
retained with no evidence: its fixture setup failed with an unconfirmed rate command
before the world ran, which is an environment failure and not a movement outcome.

So the refusal is the resource gate, it is the *same* refusal whether the runner
goes on to join or not, and what separates the two is only where the runner is
standing when the Force finally arrives.

## Correcting the previous attribution

**Start phase does not decide it.** `speed-guard v1-r3` intervened at corner level
1600 and acquired at 1625 — the same phase as `span4 v1-r2` and `v1-r3`, which
both acquired at 1625 and both captured 17.850. It missed the join and captured
21.725. The degrade record's rule — acquired by level 1700 and it joins — holds on
the worlds it was drawn from and fails here. Acquire level is a loose proxy for
when the runner reaches the corridor; the distance at 70 Force is the thing
itself, and it needs no reference to the build or the fixture's start.

**The mechanism is not a 600 ms window.** The scan is retried on every grounded
frame while the lane is inactive, subject only to the refusal backoff. There is no
window in time. There is a corridor in space, about a second wide at the ground
cap, and a resource threshold the runner crosses somewhere inside or just outside
it.

**There is no build effect left to explain.** The misses fall on the three newest
builds, which would look like a regression — but `degrade2` and
`speedguard` decide this declaration identically (the
[speed-guard record](STRAFE_MOVEMENT_SPEED_GUARD_2026-09-12.md) establishes that
its guard fires in none of the sixty worlds), and the distance-at-70-Force rule
accounts for all twenty-six worlds across all seven builds without reference to
which binary ran. R5-blue's aggregate difference between any two of these arms is
which side of the radius its three worlds happened to land on.

## What this does not settle

The repair is a resource-ownership decision and is deliberately not taken here.
The runner arrives 2 to 4 Force short — 400 to 800 ms — at the one position where
the join is geometrically free, and the candidate responses trade different things:

1. **Hold at the sample until the launch is affordable.** Costs up to about 800 ms
   of waiting to save 3.8 s. Needs a bounded wait and a rule for abandoning it, and
   it makes the lane layer hold a runner still, which it does not do today.
2. **Commit while approaching, on projected regeneration.** Matches the admission
   ladder's existing habit of judging projected rather than instantaneous
   resources. But the remaining approach is only about 100 ms, so on its own it
   closes none of the 400–800 ms gap unless it is paired with (1).
3. **Re-open the join.** Give the missed return a second chance at a later lane
   sample rather than walking the rest of it unguided. Needs the plan not to have
   carried the runner out of reach by then, which it currently does.
4. **Spend the Force under the plan-owned command.** Addresses the 30 banked Force
   rather than the missed launch, and is the `generic_cmd` ownership question the
   degrade record already named and declined to answer.

None of these is a tolerance change and none should be taken by widening the
128-unit radius, which would buy these four worlds and say nothing about the next
ones.

## Evidence

`.codex-research/join-phase-20260913`, indexed by its own `README.md`:
`phase_pool.py` enumerates every retained R5-blue and R5-red world with its build,
phase, join and capture; `join_reach.py` measures the approach to the lane samples;
`join_race.py` measures the race over the eighteen retained worlds and asserts the
same-frame join and the 0-of-18 approach result; `instrumented.py` collects the
eight worlds on `.codex-research/strafe-build-joinscan-20260913` and checks the
written-down prediction against the scan's own refusals; `pooled.py` reports the
separation over all twenty-six. The telemetry is in `ai_bot2_route.c`
(`FastReturnJoinScanRecord`), and `strafe_fast_return_check/` is that file's own
admission check passing on it.

The arm this cycle compares against, `.codex-research/speed-guard-20260912/v1`, was
collected before the telemetry landed. Its `verification.json` is the
collection-time binding and names `ai_bot2_route.c` as the one source the checkout
has drifted in since; its driver now versions a differing re-run beside that record
instead of replacing it.
