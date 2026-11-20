# K07 integrated Kejim qualification (R5), 2026-09-07

September 8 review: corrected paired M2 counts and team median, separated
missed disturbances from failed movement, and narrowed causal claims below.
The [K07 review and R6 packets](STRAFE_MOVEMENT_K07_REVIEW_2026-09-08.md)
now govern next work. Timing remains deferred under the recorded owner
decision; the failed samples have not become measured passes.

One integrated revision, frozen once and measured end to end on one fresh
native build. This protocol records what was frozen, every arm that ran, what
passed, what failed, and what remains. Nothing was installed or promoted; the
qualified configuration stays opt-in.

## Frozen candidate

- Tree: the committed sources at `2ff38c6a` plus this session's one C change
  (the rendezvous fixture below, in `codemp/game/ai_bot2_flight.c`). No
  production controller, planner or supported-path code changed.
- Build: `.codex-research/strafe-build-k07-freeze-20260907` (1,102 sources,
  every hash matched the working tree before the first collection; binaries
  `openjkded.x86.exe` `5c59fa77…`, `jampgamex86.dll` `555b6264…`). Every arm
  below is bound to this build; the R4 builds were not reused.
- Configuration: `bot_supportedHop 2`, `bot_supportedCorners 2`,
  `bot_flightAdapter 1`, `bot_forecastGuard 1`, `bot_flightArtifact` = the
  `…-r3-e45e351d…` payload (9,193 bytes, 31 windows, 41 regions, checksum
  2993043243). Control arms differ only by `bot_flightAdapter 0`.
- Mocked and pure checks before the build, all passing:
  `strafe-flight-adapter-check-k07-20260907-v1` (82 scenarios, four new),
  `strafe-forecast-guard-check-k07-20260907-v1` (17),
  `strafe-movecontrol-check-k07-20260907-v1` (200,000 bitwise-identical cases),
  `strafe-flight-check-k07-20260907-v1`; Python suites for the cost runner,
  continuous runner, cost report, lifecycle report, native report and the
  telemetry parser's self-test.
- Host condition, stated because timing is judged from it: a ComfyUI
  `python_embeded` process (started 19:39 local, not this session's) was
  resident for every arm. Cost sample 1 additionally overlapped this
  session's own file scans and is retained as a contaminated sample.

## The rendezvous fixture (native same-frame contention)

R4 left native contention unobserved after nine constructions: holds and
clustered starts moved clients, which shifted their 8 ms Pmove phase against
the 25 ms frame and coupled every other timeline through flag state. A pause
of the whole game clock cannot help either: it delays every client equally and
changes nothing between two of them. The K07 fixture therefore moves nothing.
`bot_flightRendezvous N` (research only, default 0) makes the schedule withhold
the first client that asks for the preparation slot - it is queued with a
ticket, not admitted - until a second client holds a live ticket on the same
frame, when the unchanged production deferred-first rule decides between them,
or until N frames pass on one continuous approach, when it is admitted alone.
Withheld requests print `prepare_withheld` and are never counted as
deferrals; an approach whose holder stops asking for two frames prints
`rendezvous_lost`. `tools/strafe_cost_runner.py --rendezvous N` sets it;
`strafe_cost_report.py` reports a `contention.rendezvous` block beside the
production contention numbers, whose `contention_observed` still requires a
real `prepare_deferred`.

Result (both arms on the K07 build, 20 s, all clients alive on every frame):

| Arm | Withheld | Released | Production deferrals | Queue / waited | Fairness |
|---|---|---|---|---|---|
| `adapter-rendezvous-8/` (8 bots, N=8) | 46 requests, clients 4/5/6, longest 175 ms | 2 paired (frames 27425, 29350), 5 timeouts, 1 lost | **4** (`prepare_deferred … queue=2`) | max queue 2, max waited 50 ms | `starvation_pass` true, scope measured |
| `sixteen-rendezvous-8/` (16 bots, N=8) | 63 requests, clients 4/5/6/8 | 1 paired (frame 47825), 7 timeouts, 1 lost | **2** | max queue 2, max waited 50 ms | `starvation_pass` true |

This is the first native same-frame contention ever recorded on this adapter:
the second client of a pair is deferred by the production rule behind the
first's ticket, then admitted on a later frame with `waited=50`, exactly the
mocked contract. Movement cost in these arms is not budget evidence (a withheld
preparation is later than production would make it), but neither arm exceeded
the budget (8 bots max 3.73 ms; 16 bots max 4.08 ms).

## Complete eight-bot cost, several samples (`strafe-k07-cost-20260907/`)

Every arm: 800 frames at 40 Hz, complete accounting, every preparation charged
to its frame, one admission per frame, all eight clients alive and dispatched
on all 800 frames, timing claims valid. The same six-forecast preparation
(window 21, two prefixes and four candidates, `entry_refused`) sets the maximum
whenever it occurs.

| Sample | Movement p95 / max | Server frame p95 / max | Worst preparation | Budget (p95 <= 2.5, max <= 5 ms) |
|---|---|---|---|---|
| `adapter-s1/` (contaminated: this session's file scans overlapped) | 0.340 / **6.798 ms** | 1.058 / 7.344 | 6.545 ms, 6 forecasts | fail, retained |
| `adapter-s2/` | 0.230 / 4.994 ms | 0.831 / 5.441 | 4.782 ms, 2 forecasts (window 19) | pass by 6 us |
| `adapter-s3/` | 0.152 / 3.172 ms | 0.530 / 3.494 | 3.002 ms; no six-forecast preparation occurred (3 forecasts max) | pass |
| `adapter-s4/` (clean host, nothing else of ours running) | 0.221 / **7.240 ms** | 0.816 / 7.849 | 6.940 ms, 6 forecasts | **fail** |
| `control/` | 0.274 / 0.424 ms | 0.913 / 1.699 | none | not cost evidence: client 1 died falling at 14.4 s (225 dead-client frames) |

The six-forecast preparation has now been measured at 3.72 (R3), 5.11 (R4
`baseline`), 3.72 (R4 `baseline-b7`), 6.55 (s1), 3.71 (s2, on a 6-forecast
frame that was not the maximum), 6.94 (s4) and 5.67 / 6.46 ms (16-bot arm)
with identical rollout counts. Two of four K07 samples exceed the 5 ms maximum; p95 passes in every sample
by a wide margin. The variance is in forecast rollout time itself
(forecast_us is 98-99% of prepare_us on those frames), not in accounting or
in the guard's snapshot/compare.

**Attribution (owner's call, 2026-09-08): host contention, probably fine.**
A ComfyUI server was resident for every arm and a second ComfyUI batch
process started at 23:15 local and ran during the session; the owner has
said it must keep running. Preemption of a 4-7 ms rollout burst by a GPU
batch host is the simplest account of identical work costing 3.7 ms one run
and 6.9 ms the next, and nothing in the adapter changed between the passing
and failing samples. Treat these two failed samples as host noise, not an
adapter defect, and do not spend R6 effort on the rollout bound because of
them. They are retained as measured; the only thing that turns "probably
fine" into a pass is a clean-host recollection of the same arms, which is not
available while ComfyUI stays up.

## Sixteen-bot stress (`sixteen/`)

All sixteen clients alive and dispatched on all 800 frames, 8 preparations,
busiest frame 1, no deferral. Movement p95 0.549 ms. Maximum **71.2 ms** on
frame 38900: the artifact's first-use load (`artifact_loaded` is printed on
that frame, `library_loads` 1) cost 66.5 ms of the 70.9 ms preparation; the
rollouts on that frame took 4.3 ms. In every eight-bot arm the same first-use
load (also inside the recorded window, at frame 20900) cost under 0.1 ms, and
the R4 sixteen-bot arm's load did not stall either. A first-use file read
inside a live frame can therefore stall for tens of milliseconds on this host
(the freshly staged payload's first open). The full server frame reached
72.2 ms on that frame; excluding it, the sixteen-bot maximum is under 6.5 ms
(the six-forecast preparation again). **Attribution (owner's call,
2026-09-08): host contention, probably fine** - a one-off file-open stall on
a host running ComfyUI batches, not an adapter defect; the same load cost
under 0.1 ms in every other arm. Binding the payload at map load remains the
right delivery shape for K08, but it is a tidy-up, not a blocker.

## Lifecycle regression (`strafe-k07-lifecycle-20260907/`, phase 2)

18/18 inputs valid on the K07 build: 14 intervention cases pass, 2 boundary
cases pass on explanation (red 200 unit/s kick died in the pit at 15100 ms,
blue's did not), 2 controls pass with 15/15 entered windows; `k06_lifecycle_pass`
true. Exactly the R4 result, so the fixture change did not alter lifecycle
behaviour.

## Supported resource matrix with the adapter armed (`strafe-k07-supported-adapter-20260907/`)

`tools/strafe_supported_batch.py` gained the production-runtime arm
(`--flight-artifact`, `--flight-adapter`, `--forecast-guard`, recorded in
`mode.json`). Thirty accelerated cases (both teams, phases 0/3/6, force/expiry
0/0, 20/0, 50/0, 20/400, 20/8000), corners 2, adapter armed, guard on.
`strafe_supported_report.py`: **passed, every one of the 22 declared gates in
all 30 cases** (complete, setup, captures, alive, no stalls, cadence, clocks,
phase, native settings, loadout, no fire/power commands, no force spent, native
scores, no other powers, expiry, source freeze, build sources, module and engine
binding, capture hash, declared condition, interval and corner modes).
Sixty captures, every first trip complete, every entered window exited (15/15
per case, 13/13 in the 8 s Speed cases). Against the last supported-only matrix
(2026-09-06, older build, context only): first capture faster in 30 of 30
(red median 50,575 vs 57,100 ms; blue 51,000 vs 57,350 ms), idle lower in 30
of 30 (means 0.037 vs 0.056 red, 0.038 vs 0.049 blue), walking lower in 30 of
30 (0.465 vs 0.534, 0.474 vs 0.533). Matched-resource planned references
remain unbuilt; this is the plan's gate set, not a matched-reference speed
claim.

## Full-route runtime gate, phases 1/4/7 (`strafe-k07-fullroute-20260907-v2/`)

Twelve accelerated cases (both teams, phases 1/4/7, adapter and control arms
in the same world with the same payload staged), `--forecast-guard`, run once.
`strafe_flight_native_report.py`: **12/12 valid, `k05_runtime_pass` true**,
settings identical across cases, helper bindings current. Work: 87 prepared,
87 entered, 85 successful exits, 2 truncated by the capture end, 0 incomplete,
0 recoveries, 0 deferrals, 16 free entry refusals, 22 candidate refusals.

| Pair | First trip adapter / control (ms) | Faster | Idle adapter / control | Walking adapter / control | Harm |
|---|---|---|---|---|---|
| red p1 | 51,325 / 57,200 | 10.27% | 0.0360 / 0.0415 | 0.507 / 0.583 | pass |
| red p4 | 52,650 / 57,100 | 7.79% | 0.0375 / 0.0412 | 0.514 / 0.585 | pass |
| red p7 | 50,800 / 57,250 | 11.27% | 0.0359 / 0.0397 | 0.477 / 0.588 | pass |
| blue p1 | 50,350 / 57,325 | 12.17% | 0.0362 / 0.0414 | 0.476 / 0.583 | pass |
| blue p4 | 50,550 / 57,125 | 11.51% | 0.0361 / 0.0411 | 0.476 / 0.585 | pass |
| blue p7 | 50,075 / 56,900 | 11.99% | 0.0374 / 0.0409 | 0.473 / 0.586 | pass |

Team medians 10.27% red and 11.86% blue; idle and walking better than
control in every pair; no deaths, captures and pickups equal. These are the
K05 holdout's numbers reproduced on the K07 build (the same twelve cases, so
they are now a regression, not a fresh holdout). This campaign's first wave
overlapped the baseline continuity campaign's first wave for about a minute
(see below); accelerated outcome collection carries no timing claim, and every
case's clocks, cadence and settings predicates passed.

`…-v1/` is retained: three of its twelve runner processes failed to start with
Windows status 0xC0000142 (DLL initialisation failed, empty runner logs) while
the other nine completed; its report shows the eight valid pairs' harm and
speed predicates passing.

## Defended continuity, 12 candidate + 12 baseline (`strafe-k07-continuity-{candidate,baseline}-20260907/`)

`tools/strafe_play_smoke.py` gained the same three adapter flags, staging the
payload as `base/flight_artifact.txt` and recording it in provenance;
`tools/strafe_continuity_campaign.py` freezes the extra play arguments
(`--play-args`, file arguments hash-bound) and the port base. A 90-game-second
candidate smoke (`strafe-k07-play-candidate-smoke-20260907/`) confirmed the
adapter engages under `addrunner` with cached routes present: four windows
prepared, entered and exited, one capture, two enemies observed.

Both arms ran on the K07 build, twelve 600-game-second defended runs each
(two forced CHASE/BASE opponents, 1x and 5x, three repetitions per team and
rate), four worlds at a time. Every one of the 24 runs is valid: duration
complete, rate confirmed, roles verified, two enemies observed, all sampled
native frames 25 ms. The baseline arm's first wave overlapped the full-route
v2 campaign for about a minute and the aborted M2 launch for two; its runs
passed every validity predicate and are retained as the declared originals.
Frozen comparison (`aggregate.json` in each root, summed seconds, travel
denominator excludes classified goal waiting):

| Cell | Candidate travel idle / walking | Baseline travel idle / walking | Idle not worse | Walking not worse | Distant stalls cand. / base. |
|---|---|---|---|---|---|
| red 1x | 7.128% / 32.159% | 7.275% / 32.139% | yes | **no** (+0.02 points) | 2 / 1 |
| red 5x | 7.658% / 35.323% | 10.181% / 29.686% | yes | **no** (+5.64 points) | 2 / 3 |
| blue 1x | 6.508% / 34.939% | 8.648% / 38.395% | yes | yes | 0 / 1 |
| blue 5x | 5.801% / 37.088% | 8.377% / 37.072% | yes | **no** (+0.02 points) | 0 / 1 |
| all 12 | 6.763% / 34.889% | 8.623% / 34.343% | yes | no | 4 / 6 |

Context, not gates: captures 67 candidate vs 52 baseline; the candidate's
runner entered 285 baked windows and exited 269 (0-2 recoveries per run,
0-6 `owner_lost` releases per run where combat or a replaced plan took the
client back). Rule 1 passes. **Rule 3 fails**: the candidate walks more in
three of four cells, by hundredths of a point in two and by 5.6 points in red
5x, while its travel idle is lower in every cell and overall. **Rule 2 fails
as written**: every one of the baseline's six distant stalls and three of the
candidate's four are the cached-route follower parked at the enemy stand
entry while carrying (`release=stand_entry_resource`, Force pinned at 5-24
with Speed still active, no movement command, 5-10.5 s, no combat) - an
inherited controller wait, concretely identified but neither a goal arrival
nor a flag-state wait; the candidate's fourth (red 5x r2 at 432.8 s, not
carrying, `release=none`, movement owner alternating recovery/localnav with a
zero command for 5 s near 802,-1275) has no baseline counterpart and no
explanation in the retained telemetry. The continuity package therefore does
not pass on this collection. Idle improves in every cell and aggregate
stalls/captures improve, but red 1x has two stalls versus one baseline.
The walking regression is a measured whole-configuration difference: the
candidate changes corner mode as well as the flight adapter, so an ablation
is needed before attributing the cause. Repetition does not erase this result.

## Cached-route M2 disturbance regression (`strafe-k07-m2-evaluation-20260907/`)

The frozen 780-case evaluation partition (`strafe-m2-v2-frozen-suite-20260905`)
rerun on the K07 build with `tools/strafe_disturbance_runner.py` (four
workers, accelerated), audited with `strafe_m2_audit.py` against the retained
M0/M1 evidence. All 780 cases ran, no feasibility exclusions, suite and
references unchanged, the cached-route controller's hashed sources unchanged,
every successful trip within the 150% budget. **Pickups 739/780 (94.74%)
against the frozen reference's 758/780 (97.18%)**: 21 deaths (same count as
the reference), 12 timeouts (reference 1) and 8 the audit counts invalid.
Families: blocker 26/26, approach impulse 200/208, takeoff impulse 195/208,
mid-flight impulse 191/208 (reference 203), offset 103/104, skipped jump
24/26. Paired by case name, the 41 failed/invalid cases comprise **24 new
and 17 shared**, with five reference failures recovered. The eight invalid
cases all finish with a raw flag grab at 9,775 ms, before their scheduled
9,800 ms approach impulse: the disturbance never occurred. Thus there are
747 raw flag-grab endings but 739 qualified pickups. Do not count those
eight as valid disturbed passes or as failed pickups. The audit's
`overall_target`, `all_cases_valid`, `m0_retained` and
`regression_build_compatible` checks fail: the K07 build carries 56 changed
changed sources since the M1 build (the adapter, supported path, movement
dispatch and cost fixtures), so the cached-route benchmark is not the same
program even though `bot2_strafe.c` and its follower are byte-identical. The
retained M0-M3 result stays on its own build; this revision has not established
retention. The M0/build-compatibility predicates are provenance checks, not
newly measured M0 behavior failures. All M2 family floors pass, although
the overall 95% gate fails. Whether the outcome difference is changed dispatch around the follower or
run-to-run outcome variance of the same code is not established here
(`strafe-k07-m2-evaluation-20260907-aborted/` holds 36 cases of the first,
overlapped launch and is not evidence).

## Player-visible motion

Not re-collected this session. The retained evidence is the 2026-09-06 play
feedback record plus this session's candidate play smoke and continuity demos.

## Verdict and remaining limits

**K07 is not complete.** On the one frozen revision:

| Package item | Result |
|---|---|
| Integrated full-route quality (12 runs, phases 1/4/7) | pass |
| Integrated resource traversal (30 cases, adapter armed) | pass, all gates |
| Lifecycle regression (18 cases) | pass, unchanged from R4 |
| Native same-frame contention and fairness | obtained (rendezvous fixture), `starvation_pass` |
| Eight-bot complete cost, p95 | pass in every sample |
| Eight-bot complete cost, 5 ms maximum | fail on two of four samples, **attributed to ComfyUI host contention, probably fine** |
| Sixteen-bot stress | all alive; one 71 ms first-use payload load frame, **attributed to host contention, probably fine** |
| Defended continuity 12+12 | all valid; **rules 2 and 3 fail**, rule 1 passes |
| Cached-route M2 regression | **fail**: 739/780 pickups vs the frozen 758/780, twelve timeouts, mid-flight family below reference |
| Player-visible motion | not re-collected |

Remaining limits, in the order the next revision should take them:

1. The cached-route follower parks at the enemy stand entry with Speed still
   active and Force pinned in both arms; the candidate configuration walks more than
   the baseline in three cells; one five-second stop is unexplained. These
   are controller behaviour to decide, not collection noise, and they are the
   real open items.
2. The cached-route M2 regression is below its frozen reference on this
   build (739 vs 758). Whether that is the changed dispatch around the
   follower or outcome variance is not established.
3. Timing: the two failed cost samples and the 71 ms load frame are
   attributed to ComfyUI host contention (owner's call) and are not to be
   treated as adapter defects. The host cannot be quieted, so they stay
   "probably fine" rather than "pass". Binding the payload at map load is a
   K08 tidy-up; the rollout bound is not needed on this evidence.
4. Matched-resource planned references are still unbuilt; the twelve
   full-route cases are a regression now, so K07's next pass needs a fresh
   holdout phase set if a holdout claim is wanted.

Nothing here changes the qualified opt-in configuration, the installed
baseline or the frozen cached-route reference. Any C change for items 1-2
invalidates every arm above; the next pass repeats the whole collection on a
new freeze.

## Reproduction

```bash
python tools/strafe_fixture_build.py --out .codex-research/strafe-build-k07-freeze-20260907
python tools/strafe_cost_runner.py --build .codex-research/strafe-build-k07-freeze-20260907 --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --bots 8 --supported-hop 2 --supported-corners 2 --flight-artifact .codex-research/strafe-flight-artifact-r3-e45e351d2d1d4ec6be2d05360f17d160/flight.txt --out .codex-research/strafe-k07-cost-20260907/adapter-s2 --port 32102 --flight-adapter --forecast-guard
python tools/strafe_cost_runner.py … --bots 16 --rendezvous 8 --out .codex-research/strafe-k07-cost-20260907/sixteen-rendezvous-8 --port 32107 --flight-adapter --forecast-guard
python tools/strafe_cost_report.py --run <arm> --out <arm>-audit
python tools/strafe_flight_lifecycle_campaign.py --action freeze --build <build> --artifact <payload> --out .codex-research/strafe-k07-lifecycle-20260907 --first-port 32200 --phase 2 && python tools/strafe_flight_lifecycle_campaign.py --action run --out .codex-research/strafe-k07-lifecycle-20260907 && python tools/strafe_flight_lifecycle_report.py --campaign .codex-research/strafe-k07-lifecycle-20260907 --out .codex-research/strafe-k07-lifecycle-20260907-report
python tools/strafe_supported_batch.py --build <build> --out .codex-research/strafe-k07-supported-adapter-20260907 --accelerated --supported-corners 2 --flight-artifact <payload> --flight-adapter --forecast-guard --port 32300 && python tools/strafe_supported_report.py --runs .codex-research/strafe-k07-supported-adapter-20260907 --out .codex-research/strafe-k07-supported-adapter-20260907-report
python tools/strafe_flight_native_campaign.py --action freeze --build <build> --artifact <payload> --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --out .codex-research/strafe-k07-fullroute-20260907-v2 --teams red blue --phases 1 4 7 --first-port 32400 --workers 4 --runner-args=--forecast-guard && python tools/strafe_flight_native_campaign.py --action run --out .codex-research/strafe-k07-fullroute-20260907-v2 && python tools/strafe_flight_native_report.py --campaign .codex-research/strafe-k07-fullroute-20260907-v2 --out .codex-research/strafe-k07-fullroute-20260907-v2-report
python tools/strafe_continuity_campaign.py freeze --root .codex-research/strafe-k07-continuity-candidate-20260907 --build <build> --first-port 32520 --play-args "--supported-corners 2 --flight-artifact <payload> --flight-adapter --forecast-guard" && python tools/strafe_continuity_campaign.py run --root … --build <build> && python tools/strafe_continuity_campaign.py audit --root …
python tools/strafe_continuity_campaign.py freeze --root .codex-research/strafe-k07-continuity-baseline-20260907 --build <build> --first-port 32500   # baseline world, no play arguments
python tools/strafe_disturbance_runner.py --build <build> --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --suite .codex-research/strafe-m2-v2-frozen-suite-20260905/suite.json --partition evaluation --workers 4 --accelerated --out .codex-research/strafe-k07-m2-evaluation-20260907 --port 32600 && python tools/strafe_m2_audit.py --evaluation … --suite … --m0 .codex-research/strafe-m0-missed-launch-grace-regression-20260905 --m1 .codex-research/strafe-m1-missed-launch-grace-qualified-20260905 --m1-build .codex-research/strafe-build-m123-missed-launch-grace-20260905 --build <build> --out …-audit
```
