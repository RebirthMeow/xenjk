# Opus handoff: independent movement toward 15 seconds

Latest completed experiment: [second-hop fan screen](../../movement/records/2026-09-21-successor-fan-screen.md). The +/-3-degree landing-only offer regressed class 150 by 575 ms and class 125 by 1,450 ms in both screening pairs; no confirmation was launched. Keep `bot_moveSuccessorFan=0` and the existing research baseline. The record contains the result, CPU/parity checks and next-work boundary; it supersedes the candidate-experiment instructions below.

Current next goal (2026-09-21 owner direction): identify one recurring costly
transition in retained class-150/125 traces, then test one bounded candidate-generation
variation with all admission checks preserved. Screen complete trips against the
current control; a promising arm needs fresh four-class confirmation, the 36-case
floor, both seeded regressions, meaningful parity comparisons and CPU measurement.
The milestone is a measured completed-trip improvement. Keep successor aiming and
rerouting enabled, exit-speed weight 500 and state preference enabled. This work
order supersedes older next-action suggestions below.

The [trigger shadow measurement](../../movement/records/2026-09-21-trigger-shadow.md) and
[rescue follow-up](../../movement/records/2026-09-21-trigger-shadow-rescue.md) are corrected after
review: `swept` tests an enclosing axis-aligned box against brushes, not the actual
swept hull. Its 107/60 `swept_not_endpoint` cases remain unresolved; they are not
proven protective crossings. The 39 straight-search blue-flag verdicts are initial
`arrives` conversions, not final admissions or coverage of all 51 census
disagreements. Later chain/path gates and other rollout paths remain unmeasured.
Keep trigger admission conservative and the default-off diagnostic off for speed
runs. Deprioritize an admission repair because no speed payoff is demonstrated;
this is a priority decision, not proof that only 30 refusals are recoverable.
The diagnostic changes throughput, so its absolute counts describe instrumented
searches and stable observed rates are not guaranteed for uninstrumented runs.

Prepared 2026-09-20. Workspace: the repository root (PowerShell).
This is an experiment plan and handoff, not new performance evidence.

## Objective and starting point

Improve stationary, no-Force-spend independent movement toward roughly
15 seconds on `red_out_L1_stand`. Preserve native physics, resource lifecycle,
one movement owner and every admission/safety check. Prepared plans are optional;
new human recordings and universal route rejoining are not prerequisites.
The immediate task is a useful, measured continuation improvement, not installation.

Read [AGENTS.md](../../../AGENTS.md), the owner's
[refined method](../../movement/records/2026-09-19-refined-method.md), and the latest
[successor-aim record](../../movement/records/2026-09-20-successor-aim.md). Use
[ARCHITECTURE.md](../../movement/ARCHITECTURE.md) and [PHYSICS.md](../../movement/PHYSICS.md) for specific
design questions. The long [HANDOFF.md](../../movement/HANDOFF.md) contains historical work;
its current introduction and the latest dated records take precedence.

The checkout contains substantial uncommitted, unrelated work. Inspect diffs
before editing; do not reset or clean the tree. Archive rather than delete,
including generated evidence. No installed-game changes are authorized by this
handoff. Read-only analysis, local fixes and isolated native tests are in scope.

## Historical starting point, prepared 2026-09-20

The settings and table below describe the original successor campaign. The current
control uses exit-speed weight 500. Parity now makes meaningful comparisons: the
[guarded repair](../../movement/records/2026-09-21-parity-review-fix.md) passed the 36-case floor
and both seeded cases with 19,898 reported comparisons and zero
mismatches/refusals/deaths. Those results retain their own build binding.

Current measured research configuration:

```text
bot_moveSuccessorAim=1
bot_moveReroute=1
bot_moveExitSpeed=2000
bot_moveStatePrefer=1
bot_moveHeadingSweep=0
bot_moveReplay=0
```

Use the existing graph seed, no corridor and no human playback. The three
experimental settings on the first three lines remain default off.

| Latest eight counterbalanced pairs | Aim off | Aim on |
| --- | ---: | ---: |
| Mean completed time | 18,840.625 ms | 17,943.75 ms |
| Median | 18,512.5 ms | 18,125 ms |
| Worst | 20,350 ms | 19,375 ms |
| Best | 17,900 ms | 16,450 ms |
| Distinct common-prefix logged traces | 6 | 5 |
| Recorded median search rollout cost | 22.2 ms | 38.9 ms |
| Recorded maximum search rollout cost | 58.5 ms | 99.2 ms |

Six pairs improve, two regress. The 16,450-ms trajectory repeats three times
in confirmation and once in the pilot. **It is not typical 16.45-second
performance, attainment of 15 seconds, or four independent discoveries.**
The prior campaign's 17,900-ms median is from another cohort, not this matched
control. Compare fresh matched arms, not best times across campaigns.

Both 36-case floors and two seeded named regressions pass: **74/74, zero
deaths, movement owner only**. Parity still makes **zero comparisons**; zero
mismatches therefore does not qualify forecast/execution agreement. The search
remains fixture/parity gated. CPU scheduling, dynamic interference, interruption
recovery and production enablement remain separate open work.

## Executed 2026-09-20: experiments 1 to 3

All three were read-only. No native run, no source change, no rebuild.

**Experiment 1 is a clean negative.** All 24 `va` sites in the movement
sources were enumerated. Twenty-two are argument-position, created and
consumed in one call expression. One pointer is held, `bot2_move.c:3128`,
and the only intervening `va` on any path to its use is the nested
`va("%s stale_count=%d",detail,...)` that reads it: one call against a limit
of three. `Q_strcat(detail,...,va(...))` at 3209-3218 and `bot2_graph.c:955`
write into real local arrays, not `va` buffers. Decisively, `Event` does not
call `va`, and `Bot2_PrintTelemetry` formats into its own `char string[1024]`,
so telemetry added the way this project adds it advances the `va` index not at
all. **The static-buffer hypothesis in experiment 4 is therefore not supported
for any ordinary patch**; carry frame overrun as the leading mechanism. Two
secondary observations, neither a finding: `StageEdge` returns a single
`static char name[16]` and all three callers use one result per call; and the
longest emitted `[MOVE]` line in the retained worlds is 657 characters against
a 640-character detail buffer and a 1,024-character print buffer, with
`aim_source=` present on every sequence line, so nothing needed downstream is
being truncated today.

**Review update, 2026-09-20:** the [correction record](../../movement/records/2026-09-20-opus-review-fixes.md)
supersedes the budget-derived conclusions previously summarized here.
Experiments 2 and 3 supplied useful localization, but did not settle the only
remaining cause or lever. The speed integral is not proof of a fixed route;
54.2% waiting included terminal walking, and the hypothetical speed-preservation
calculation is not a lower bound on achievable savings.

The [trigger classification](../../movement/records/2026-09-20-trigger-classification.md)
identifies real hurt-volume contacts in the sampled diagnostic. Keep those
admission refusals. It does not prove that every possible continuation fails.
The live state planner already uses carried speed. The
[route-pricing](../../movement/records/2026-09-20-route-pricing.md) and
[search-rate](../../movement/records/2026-09-20-search-rate.md) pilots did not establish useful
improvements and were reverted; they do not globally refute those approaches.

The [exit-speed sweep](../../movement/records/2026-09-20-exit-speed-finer-sweep.md) favors 500
provisionally. Correct official completion means weighted over common start
strata are 17,084.4 versus 18,040.6 ms at 2000; the earlier quoted values used a
shorter analysis window. The fast common start is 15,925 ms, and the observed
maximum at 500 is 21,125 ms. Neither reliable 15-second performance nor runtime
or parity qualification is established. No installed changes.

## Recommended experiments

They are ordered to execute in this sequence. Experiments 1 to 3 need no
native run and no source change: they are read from the retained evidence and
the sources as they stand, and each one can refuse or reprice the work after
it. Experiment 4 measures the cost of the search itself, which every later
change has to pay. Experiments 5 to 7 change behaviour and need the full
pilot, counterbalanced pairs and floors described at the end.

### 1. Audit the held `va` pointers in the movement sources

Free, read-only, and first because a defect here would sit under every other
result. `va` rotates four static buffers (`q_shared.c:697`), so a pointer it
returns is clobbered by the fourth further call from anywhere, including
nested calls. `StraightSearch` holds `va` results across loops and builds
event details by concatenating further `va` results into a fixed buffer.

List every site in the movement sources where a `va` result is stored in a
variable, passed onward, or read after any further call that can itself reach
`va`, and count the intervening calls on each path. Report the sites that can
exceed three, with the path that does it. A site that cannot is not a finding.

This is also the cheapest of the mechanism hypotheses in experiment 4: if held
pointers can be clobbered, then adding telemetry inside the search changes what
the bot does rather than only what it costs, and the retained 2,000 ms belongs
to behaviour. Repair any finding on its own, against the floors, before it is
entangled with a timing experiment.

**Keep only if:** a finding is real on the exact call path shown. Do not
rewrite working formatting for tidiness while a campaign could be running.

### 2. Account for the whole leg before buying another local decision

Experiments 3, 5 and 6 improve one decision at a time. None of them states what
the leg's 18 seconds are made of, or whether 15 seconds is reachable at all
at level 1 with no Force spend. Compute it before them; it prices all three.

The physics gives a one-number model to test, not to assume. In flight
`MoveControl` admits only wish directions whose component along the current
velocity is below wishspeed, so the added acceleration is at most
`wishspeed * dt` and its along-velocity part at most `wishspeed * dt *
(wishspeed / speed)`. That predicts `speed^2` rising about linearly in air
frames from 250, while ground friction removes roughly 15 percent of the
excess every grounded frame. Speed is bought in the air and spent on the
ground, so leg time is close to a function of the airborne frame fraction.

Measure it from retained evidence. The `[MOMENTUM]` line already carries
`level`, `speed`, `ground` and `up` at the 40 Hz sample; use the momentum
audit's existing outbound-leg window rules. Report airborne against grounded
frames, the speed integral, the route's own length, and mean speed. Check the
`speed^2` prediction against the measured flights rather than assuming it.

Then attribute every grounded frame to an owner the telemetry already names:
the landing-frame search exits (`no_straight_ahead`, `straight_too_short`,
`goal_too_near`, `gap_corridor_refusal`), the onward stage's hold
(`MOVE_ONWARD_HELD` permits 40 grounded frames, one second at the 25 ms
decision step, before `bot_moveOnwardBound` releases it, and
`bot_moveWalkThrough` is not in the reference configuration), each refusal
class with its count, and plain walking where no search ran.

Deliver a table of grounded-frame owners with frames and modelled speed cost,
and the leg time that remains if every named owner is removed on the same
route. That is an upper bound on what experiments 3, 5 and 6 can buy, computed
without a native run.

**Decision:** a bound well under 15,000 ms makes the proposal and selection
experiments the right work. A bound near the current time means the route,
not the hop choice, is the limit: note that `PathRemainingMs` and
`baseRouteMs` price a strafe route at the walk model's speed, so reroute
ranking compares flights by how long walking them would take. Distance from a
speed integral is not the route's length; report both. A 40 Hz sample under
timescale 5 is not a per-command record. Do not add telemetry inside the
search to sharpen this measurement, for the reason in experiment 4.

### 3. Explain the first useful divergence in fast and slow outcomes

Do this before another parameter sweep. Use the retained 16.45-second trace
and the slower enabled traces to distinguish three causes:

- A useful continuation is never proposed or is refused.
- Several continuations are valid, but selection chooses a worse one.
- Runs already differ in fixture phase or native state before the decision.

Start from `fast-trace-match-1.json` and `fast_trace.py` in the evidence root
below. Four 17.9-second control traces match the fast trace's logged momentum
through relative 1,675 ms. A clone selected at relative 450 ms changes only the
second heading. The actual first landing at 1,375 ms matches; second-landing
speed at 2,425 ms is 506.8 versus 514.7 units/sec. Later decisions diverge, so
this does not assign the entire 1.45-second saving to the first clone.

Deliver a short table of the earliest material divergence per distinct slow
trace: matched fields/window, offered candidates, refusal reasons, selected
`aim_source`, next-frame airborne state, and later landing. Logged equality is
not equality of every hidden player/animation/world field. Collect a narrowly
bound diagnostic only if retained data cannot resolve the cause; instrumentation
can change trajectories. Do not manufacture an exact-match claim.

**Decision:** missing proposals lead to experiment 5; poor choice among valid
ones leads to experiment 6. Unknown trigger contacts need classification, not
an admission exception. A local speed gain must still shorten complete runs.

### 4. Price the coupling between in-search work and route time

[LESSONS.md](../../movement/LESSONS.md) records that printing three landing-frame reasons
where they occur cost 2,000 ms on the guided arm, reproducibly, and that the
mechanism is **unresolved**: the forecast bounds a rollout by step counts and
not by a clock, so the wall-clock reading was withdrawn while the empirical
rule was kept. That figure is larger than every gain the experiments above
are chasing, and it is a property of how much work the search does rather
than of what it decides.

This is not a filed curiosity. Successor aim raised recorded median rollout
cost from 22.2 to 38.9 ms and maximum from 58.5 to 99.2 ms while improving
the mean by about 900 ms. If route time responds to in-search work at all,
those arms were not matched on it, and the 900 ms is a policy gain net of a
cost of unknown size. Experiment 7 is filed on the premise that CPU savings
alone are not game-time savings; under a real coupling that premise inverts
and experiment 7 becomes a game-time experiment with the clearest attribution
of the set.

Hold policy fixed and vary only work. Inject a decision-neutral load inside
the search at several levels spanning the roughly 17 ms per search that
successor aim added: a counted arithmetic loop, or a fixed number of extra
rollouts of an already-judged candidate whose result is discarded. Candidates,
judging, commits, ordering and telemetry stay identical; only the work differs.
Report complete matched runs as route time against injected microseconds.

Two mechanisms are worth separating before concluding anything, both from the
same runs:

- **Frame overrun.** At timescale 5 with fps 40 and msec 8, a server frame
  that overruns changes the command cadence the bot issues. Count commands
  per second of game time and Pmove steps per second per arm; this mechanism
  predicts cadence moving with load.
- **Static buffer reuse.** Added prints change how many `va` calls fall
  between a held pointer's creation and its use, which would make the 2,000 ms
  a behaviour change rather than a timing one. Experiment 1 settles this
  without a native run; run it first and carry its answer in here.

**Keep only if:** nothing. This experiment installs nothing; the injection is
removed before any retained build and its output is a single number, route
milliseconds per millisecond of search work. Route times are quantised and one
arm cannot resolve a difference this size, so use the same eight counterbalanced
pairs and report the spread.

**Decision:** a coupling indistinguishable from zero leaves the policy
experiments as written and retires a standing doubt. A nonzero one reprices
all of them: re-examine the successor-aim comparison under it before building
on the 900 ms, promote experiment 7 ahead of 5 and 6, and give any expansion
of the proposal set a budget in route milliseconds rather than in candidates.

### 5. Choose the second target from measured landing speed and direction

This is the strongest next movement hypothesis. `LandingAimOffers` currently
corrects the origin of the second heading but keeps the precomputed target.
`StraightSearch` computes second-hop distance as
`0.695 * (estimated initial speed + 100) + 76`; any positive run-up uses
`Wishspeed` for that initial estimate. The first hop can land somewhere else
and at a substantially different speed.

For a bounded set of original straight candidates, use the simulated first
landing's position, speed and **velocity direction** to propose a new target
along a connected path segment. Initially keep a small number of alternatives
and retain the existing candidate as fallback. Stored `graphEdge.dir` is a
control heading, not measured velocity. Avoid snapping across gaps or treating
an off-path point as a valid connection merely because a nearby cell exists.

Start with independent straight proposals. A planned-state edge carries entry
and endpoint semantics; moving its target while retaining its edge identity
can silently misrepresent the maneuver. Handle that explicitly before extending
the experiment to planned-state candidates.

Use the full original-root native reroll and existing judge for admission.
Do not seed execution with an estimated speed, graph nominal exit, reset jump
latch or synthetic player state. Test one proposal change at a time, without
simultaneously changing ranking or safety gates.

**Keep only if:** complete matched runs improve consistently enough to justify
cost, the tail and failures remain visible, and affected regression floors pass.
Reject a change that merely offers more candidates or increases landing speed.

### 6. Rank a few valid candidates by a native continuation

> **Repriced 2026-09-20** by
> [the slow class's selection failure](../../movement/records/2026-09-20-slow-class-selection-failure.md).
> The concrete failure this section waited for exists, and a forced-candidate
> diagnostic isolates it: under one fixed policy, committing a different
> admitted candidate at one frame moves the trip from 20,100 to 18,775 or
> 20,925 ms, so that frame is worth 2,150 ms and the ranking takes the middle.
> On that frame a one- or two-decision lookahead over the realized
> continuations ranks the worst trip first and the winner leads only from the
> fourth. Measure the horizon a bounded rank would need - here and on other
> frames - before building one. `bot_moveSelectNth` / `bot_moveSelectCand`
> are the tool for asking that question frame by frame.
>
> **Horizon priced 2026-09-20** by
> [the exit-speed-500 repeat](../../movement/records/2026-09-20-select-override-exit-speed-500.md).
> Under 500, the current research setting, the same frame is worth **2,300 ms**
> and the ranking takes the **worst** of the three, not the middle: control
> 21,125 ms, forced cand 13 19,950, forced cand 1 18,825, 12 cells of 12.
> cand 1 is admitted and best under both policies, and the same committed
> candidate moves at most 200 ms between them, so this frame dominates its
> downstream. Ranking the realized continuations by distance still to run at
> common elapsed-time endpoints is **stable only from frame + 7,000 ms** under
> both policies - about half the 14,000 to 16,350 ms leg that follows - and at
> **frame + 5,000 ms the true winner ranks last** under both. That is an
> oracle over the realized continuations under the full live policy: it bounds
> a bounded forecast rank from above and prices no CPU. What remains is the
> bounded rank itself, measured for accuracy against that oracle and for cost,
> and the same question on other frames and start classes before a scoring
> rule is chosen.
>
> **Generalized 2026-09-20, and it does not generalize.**
> [The frame survey](../../movement/records/2026-09-20-selection-across-frames.md) forced candidates at three more commits of the slow
> class: every frame carries 1,525 to 2,300 ms and the ranking is worst or
> near-worst at 4 of 4, with commit 5 forced exhaustively and taken slowest.
> But [the second start class](../../movement/records/2026-09-20-selection-second-start-class.md) forced three commits of class 150
> exhaustively and there the ranking commits the **fastest** candidate at 3 of
> 3, the score maximum every time. The failure is **class-specific**, so a
> bounded rank must be shown not to regress a class where the incumbent is
> already optimal. The `kind` field names the difference, and
> [removing the planned-candidate preference](../../movement/records/2026-09-20-state-preference-costs-the-slow-class.md) is worth **3,150 ms at
> class 75** against **+150 at class 150**, floors passing unpinned. Price
> that at classes 100 and 125 before returning to this experiment.
>
> **Refuted 2026-09-20 by the full grid.**
> [Removing the preference loses 1,162.9 ms weighted](../../movement/records/2026-09-20-preference-removal-refuted.md): class 125
> **+3,025 ms** and class 100 **+1,775** with it off, against -3,150 at class
> 75. Declared weighting 17,394.4 on against 18,557.3 off, equal weighting
> 18,500 against 18,950, search cost unchanged. `bot_moveStatePrefer=1` is
> confirmed. The preference is right at three classes of four, so the target
> is a **conditional** preference at the decision - which is this experiment,
> against its priced 7,000 ms horizon. Two classes are not a grid: 75 and 150
> are the extremes and the two between them reverse the direction.

Use only after finding a concrete selection failure in experiment 3. Current
progress relative to walking and the exit-speed bonus are short-horizon
heuristics, not full-trip value. `StatePreferred` can also replace a winner
with a planned candidate selected by margin; inspect that final decision before
assuming the highest score is what executed. Blanket preference removal and
simple bonus changes have already been screened without establishing a better
replacement.

Test bounded follow-up evaluation of the best two or three already-admitted
candidates. Compare at a declared common simulated endpoint, handle real early
goal arrival separately, and carry all required native state and continuation
context. Unsupported contacts, horizon exhaustion and an unavailable follow-up
are unknown, not free progress or successful arrival. Preserve the original
admitted fallback when the extra evaluation cannot establish a better choice.

Begin with diagnostic ranking alongside the current choice. First demonstrate
a repeatable wrong-choice example; then permit commits and measure full runs.
Do not turn approximate remaining graph distance into a claimed native trip
time, or add an unbounded third/fourth-hop search to every frame.

### 7. Reduce duplicate first-flight measurements as a separate CPU experiment

The new search is expensive. Within a single immutable live root, several
proposals may share exactly the same first-flight inputs. Investigate caching
the measured first-landing data once for those identical prefixes. Include all
relevant executor, origin, first-hop, physics, clock, resource, path and permit
context; start with a per-search cache, not reuse across frames or maps.

**Important trap:** `FirstLandingCommand` stops on the callback after the step
that observes touchdown. That touchdown step has already emitted the original
second-hop command and run its Pmove. `result.terminalPS` is therefore not an
untouched first-flight landing state and can depend on hop 1. The recorded
landing frame's **beforePS** is the relevant boundary. Cache proposal measurements,
not the whole prepass terminal result or a later success/refusal verdict.
A second-command trigger can differ even when the first landing is identical.

If implementing true forecast continuation instead of full-root rerolls, qualify
the exact boundary first: player state alone omits executor policy, command/world
timing, jump bookkeeping, projected animation and path/permit context. The API's
terminal-animation support does not automatically provide animation for an
earlier landing frame. Keep full rerolls until equivalence is demonstrated.

**Keep only if:** candidate construction and decisions are preserved under the
declared binding and measured forecast work decreases. CPU savings alone are
not game-time savings. Do not bundle this optimization into the first policy
trial; it needs its own attribution and regression checks.

## Avoid repeating these without a new hypothesis

- **8-unit run-up:** baseline 16,450 / 19,050 / 16,450 versus 19,650 in all
  three trials. It was selected five times per run and raised CPU cost. Reverted;
  original 0/24 vocabulary restored byte for byte, experiment retained.
- **Reroute/credit separation with successor aim on:** three-block means were
  none 19,150, reroute only 18,366.67, credit only 20,075, both 18,183.33 ms.
  Reroute only had a better median but worse mean/tail and similar search cost.
  Two distinct logged traces per arm are not a broad parameter qualification.
- **Heading sweep:** rotating both headings together did not establish a better
  full-route configuration. A single 17.475-second result was not confirmation.
- **Fewer path skips alone:** bookkeeping repairs and more immediate taps did
  not always shorten the trip. Never equate a commit with an actual jump.
- **Trigger bit 256:** means unsupported trigger contact, not necessarily a pit,
  harmless pickup or successful goal touch. Preserve existing contact gates.
- **Parallel timing:** simultaneous worlds can repeat the same trajectory and
  create misleading speed claims. Parallelize outcome floors only.

## Concrete starting commands and verification

Run from the workspace root. This rechecks the current reference without
editing or rebuilding it; use an unused port. Output paths are always unique.

```powershell
$ErrorActionPreference = 'Stop'
$taskOut = '.codex-research/opus-reference-' + [guid]::NewGuid().ToString('N')
python tools/movement_bench.py `
  --build .codex-research/successor-aim-build-20260920-a `
  --out $taskOut --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 32310 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=2000 --set bot_moveSuccessorAim=1
if ($LASTEXITCODE -ne 0) { throw 'Retain and inspect the failed reference world' }
```

The runner verifies source/binary bindings. If native sources have changed,
do not bypass that check. A verified exact copy of this reference's native
sources is at `.codex-research/short-runup-20260920-a/baseline-source`; pass it
as `--source-root` when intentionally testing the old reference. The similarly
named `successor-aim-20260920-a/baseline-source` is the **older pre-aim build**.

Before editing, freeze a hash-verified native source snapshot for each retained
control. Build changed sources into a fresh path:

```powershell
$taskBuild = '.codex-research/opus-build-' + [guid]::NewGuid().ToString('N')
python tools/strafe_fixture_build.py --out $taskBuild
if ($LASTEXITCODE -ne 0) { throw 'Retain and inspect the failed build' }
```

Do not edit measured sources during compilation or collection. Follow the
[native verification procedure](../strafe-movement-2026-09-14/STRAFE_MOVEMENT_VERIFICATION.md).
Keep stationary start, no-spend resources, seed, map, fixed 1 / msec 8 / float 1 /
fps 40, timescale 5 and command cadence matched. Regeneration stays native.

Declare a small pilot before running it. For a promising change, collect eight
fresh counterbalanced pairs, one native world at a time; exclude screening from
confirmation. Report all times, failures, mean, median, maximum, distinct traces
and search cost. Preserve failed setup/evidence and identify replacements.
Use at most four isolated worlds with distinct ports/homepaths for untimed
36-case floors; also run seeded `blue_out_L1_stand` and `red_ret_L1_stand`.
Test enabled and default behavior for retained changes. Use hidden launchers.

Run affected extracted-C and Python regressions. Relevant tools include
`test_movement_successor_aim.py`, `test_movement_reroute.py`,
`test_movement_reference.py`, and `test_movement_replay.py`. Read their CLI before
invoking. Replay analyzer v5 keeps `aim_source` clones out of historical A/B
and run-up pair tables; all raw candidates and total counts remain included.
Momentum analysis must stay inside the outbound leg, exclude fixture setup,
and require complete windows; no coarse interpolation or post-pickup mixing.

## Code and evidence map

Primary code: [bot2_move.c](../../../codemp/game/bot2_move.c), especially
`StraightSearch`, `LandingAimOffers`, `FirstLandingCommand`, `StraightJudge`,
`StatePreferred`, `StraightCommit`, `HopStep` and `PathPointAt`.
Forecast contract: [bot2_forecast.h](../../../codemp/game/bot2_forecast.h).
Tool entry: [movement_bench.py](../../../tools/movement_bench.py).

- Frozen reference build: `.codex-research/successor-aim-build-20260920-a`;
  manifest `20260920T082745909836Z.sources.json` (1,117 native inputs).
- Main evidence: `.codex-research/successor-aim-20260920-a/`:
  `summary-confirm-final.json`, `confirmation-binding-check-1.json`,
  `fast-trace-match-1.json`, `summary-ablation-final.json`,
  `ablation-binding-check-1.json`, `qualification-summary-2.json`.
- Reproduction scripts in that directory: `summarize.py --phase confirm`,
  `summarize.py --phase ablation`, `fast_trace.py`; provide new output files.
- Failed run-up evidence: `.codex-research/short-runup-20260920-a/`, including
  `summary-pilot-1.json` and both native source snapshots.
- Earlier speed-win evidence: `.codex-research/speed-search-20260920-a/` and
  [its record](../../movement/records/2026-09-20-speed-win.md).

`.ignore` excludes research evidence from broad searches: use exact paths or
`rg --no-ignore --hidden` on the relevant directory. Keep analysis scripts and
their input hashes. Update [HANDOFF.md](../../movement/HANDOFF.md), [PLAN.md](../../movement/PLAN.md) and
[EVIDENCE.md](../../movement/EVIDENCE.md) when the next action or measured result changes.

## Suggested first session

> **Done 2026-09-20.** The exit-speed confirmation below is complete:
> [the pinned confirmation](../../movement/records/2026-09-20-exit-speed-pinned-confirmation.md).
> 500 wins 1,475 ms at start class 150 and 525 ms at class 125, ties at class
> 100 and loses 1,025 ms at the slow class 75, six matched pairs of six in each
> cell; declared-weight improvement 807.8 ms, equal-weight 243.75 ms; search
> cost unchanged; floors 36/36 unpinned plus both seeded regressions. The
> strata are start-level classes because a world can only start on its own
> 250 ms play-log ladder, which is why 3275 and 3300 were unreachable as exact
> pins; `--corner-class` pins the class. Next is the reassessment named at the
> end of this section: a specific native selection failure, then experiment 6.

Read the [review corrections](../../movement/records/2026-09-20-opus-review-fixes.md) and
current [HANDOFF.md](../../movement/HANDOFF.md) before the historical experiment menu above.

Confirm the provisional `bot_moveExitSpeed=500` against 2000 using fresh repeated
matched runs at predefined pinned start levels, including 3275, where the
retained sweep shows a regression. Include the common 3300/3325/3350 strata;
declare weighting and repetition counts before collection. Verify achieved
`corner_level`, retain all misses, and report unavailable cells as incomplete.
Do not replace them with easier starts or condition on a subsequent touchdown.
Acceptance floors should continue sampling starts with pinning unset.

Use official `evaluation.time_ms` for completion; report a leg budget's
`analysis_window_ms` separately. The observed start-level association improves
comparison design but does not eliminate independent confirmation or prove
that fixture timing is unaffected by the treatment.

Experiment 6 has since been repriced twice against a specific native
selection failure, and its horizon is priced; read the blockquote at the
head of that section first.

After confirmation, reassess experiment 6, bounded continuation ranking,
against a specific native selection failure. Preserve every safety gate and
measure CPU as well as complete trips. The prior pilots justify keeping their
changes reverted; they do not establish that continuation ranking is the only
remaining route to improvement.

Freeze measured sources, declare the pilot, keep screening out of confirmation,
and verify source/binary/settings/case bindings. Report implementation, measured
timing, CPU cost, parity qualification and installation as separate facts.
