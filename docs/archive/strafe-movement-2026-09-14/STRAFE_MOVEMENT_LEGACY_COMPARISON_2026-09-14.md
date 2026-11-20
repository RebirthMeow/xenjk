# Legacy Bot2 comparison against residual walking and idle starts

2026-09-14. User-requested diagnostic, not a controller merge or qualification.

## Finding

Legacy Bot2 locomotion requested a grounded jump 25 ms after fixture setup in
all ten walking starts, including Force 1 and Force 2. Runner first requested
one 0.900–4.150 s after setup. In two near-stand idle starts, legacy requested
one at 0.175 s, versus Runner at 1.925 and 0.775 s. This establishes available
jumping opportunities under reconstructed physical conditions; it does not
prove every historical delay was unnecessary under the original private state.

Legacy shortened all four selected near-home return legs by 1.600–2.150 s in
the first cohort. However, six of twelve legacy continuations ended in fatal
falls, versus zero Runner deaths. One legacy death followed a successful
capture at 4.025 s and occurred at 14.100 s. Five occurred before 12 s from
setup. All retained failures count; increased airtime includes drops and is
not a success metric. These short observations do not establish full-trip
completion for outbound cases or population-level reliability.

## Method and reproducibility

Twelve selected residual episodes come from the latest eight Speed-candidate
recordings. The local evidence root is
`.codex-research/legacy-comparison-20260914-a/`:

- `episode-pool.json`: all extracted walking/idle episodes, with source rows.
- `cases.json`: twelve case declarations, original episode IDs, exact CSV paths,
  timestamps, positions, velocity, yaw, Force and possession.
- `collect.py`, `native-a/declaration.json`, `native-a/runs.json`: 24 native worlds.
- `collect-repeat.py`, `native-b/`: eight repeat worlds for C01/C07/C09/C12.
- `analyze.py`, `analysis.json`: per-world command latency, local observation
  bins, observed capture/death time and retained kill messages.
- `verification.json`: sixteen paired settings/binary/fixture/physical setup
  checks and all 1,107 current native inputs matching the measured manifest.
- Each world retains summary, provenance, native fixture acknowledgement,
  server log, CSV, demo and staged binary hashes.

Fresh build: `.codex-research/strafe-build-legacy-compare-20260914-a`, manifest
`20260914T084058017031Z.sources.json`. Dedicated localhost, 40 Hz, 1x,
no enemies, at most four worlds concurrently, decision seed 2026091401.
Each arm requests 12 game seconds after the harness observation anchor;
setup precedes that anchor and shutdown can retain extra time. Table latencies
are relative to the native fixture acknowledgement. Captures are first observed
CSV samples, so retain sampling uncertainty; they are not exact event timestamps.

The diagnostic mode keeps the same movement-only objective and postfilter.
Mode 1 runs current Runner adapters; mode 2 bypasses them and executes the real
regular Bot2 movement state machine, including its native jump predictor.
Common jumppad handling remains. Mode defaults to zero and only takes effect
on movement-only clients with dedicated server and cheats enabled. The runner
CLI requires an isolated dedicated corner fixture. This is locomotion isolation,
not a complete regular Bot2 combat/role/Speed/Absorb-policy replay.

The fixture restores recorded pose, velocity, yaw, Force and possession, and
resets controllers and jump/knockback state. It does not reconstruct historical
private timers/caches, animation, health or RNG history. Both arms can execute
differently before setup; equal decision seed does not guarantee equal subsequent
RNG phase. At the same level timestamp as setup, a pre-intervention sample may
exist: analysis excludes timestamps at or before setup. Optional eight-second
XY-speed bins use [setup+25, setup+8025), with grounded <20 idle, 20–<300 walk,
otherwise fast. They differ from the historical inventory's 3D-speed definition
and include any pit waiting/death; do not present them as safe walking reduction.

## Exact selected situations and first-cohort results

Jump columns are seconds to the first positive grounded up-command, not a
count of successful jumps. Capture/death columns are seconds from fixture
setup across the entire retained continuation. Dash means not observed.

| Case / retained episode | Force | First jump Runner / legacy | Capture Runner / legacy | Legacy death |
|---|---:|---:|---:|---:|
| C01 `spawn-red-r2-walking-5` | 27 | 4.025 / 0.025 | 5.650 / 4.025 | 14.100 |
| C02 `spawn-blue-r2-walking-6` | 27 | 4.150 / 0.025 | 5.775 / 3.625 | — |
| C03 `zero-red-r2-walking-7` | 80 | 3.525 / 0.025 | 5.175 / 3.575 | — |
| C04 `zero-blue-r1-walking-7` | 80 | 3.650 / 0.025 | 5.275 / 3.675 | — |
| C05 `spawn-red-r1-walking-10` | 63 | 3.725 / 0.025 | — / — | 11.600 |
| C06 `spawn-blue-r1-walking-7` | 56 | 3.425 / 0.025 | — / — | — |
| C07 `spawn-blue-r2-walking-4` | 1 | 1.900 / 0.025 | 11.150 / 8.100 | — |
| C08 `zero-red-r1-walking-12` | 2 | 1.375 / 0.025 | 10.525 / — | — |
| C09 `zero-blue-r1-walking-4` | 40 | 0.900 / 0.025 | 13.275 / — | 7.200 |
| C10 `zero-red-r1-walking-6` | 49 | 2.250 / 0.025 | 11.350 / — | 8.575 |
| C11 `spawn-blue-r1-idle-1` | 42 | 1.925 / 0.175 | — / — | 9.825 |
| C12 `spawn-red-r1-idle-1` | 100 | 0.775 / 0.175 | — / — | 11.400 |

C01–C04 cover red/blue near-home supported walking with both low and high Force.
C05–C06 cover outbound ramp/ground sections. C07–C08 cover near-empty Force.
C09 covers localnav handoff; C10 covers a short supported hop opportunity.
C11–C12 cover near-stand idle starts. C08 legacy continued onto the lower route
without capturing during observation; this is not proof of permanent failure.

## Repeat evidence

C01: legacy capture 4.025/4.050 s versus Runner 5.650/5.650 s. The first
legacy observation includes the later fatal fall; none was observed in the
shorter repeat. Do not call that failure disproved.

C07: legacy capture 8.100 then 13.025 s, versus Runner 11.150 then 10.750 s.
Immediate low-Force jumping repeats; a reliable return-time win does not.

C09: legacy fatal fall at 7.200 s in both runs; Runner remains alive and
captures at 13.275/13.150 s. C12: legacy fatal falls at 11.400/10.825 s;
Runner remains alive in both observations. Across all 32 worlds there are
eight legacy worlds with observed death and no Runner deaths. These are
selected starts with unequal retained end times, not a general failure-rate estimate.

## Why legacy jumps, and where it fails

Regular Bot2 tries both strafe directions from current native state, forecasts
landing with Pmove and scores up to four jumps ahead. It permits passing a
waypoint when the landing continues toward the goal, except for final-stand
overshoot or an opposing turn. A valid first landing can still be accepted
when deeper continuation fails. It does not demand the same prepared interval
hop flags/room that constrain Runner. Movement aim can snap immediately;
its first-step behavior is not equivalent to Runner's constrained executor.

This is not just a simpler algorithm: the default jump planning mode permits
uncapped recursive prediction. No isolated CPU timing was collected here, so
this comparison does not establish a processing-budget benefit.

The failed native jumps show forecast/execution disagreement. Examples in the
first cohort's legacy server logs:

- C09: predicted zero vertical drop and 517.8 units of horizontal displacement;
  recorded landing drop 1776.0 and displacement 1239.8, followed by death.
- C05: predicted drop 440.0, actual 1776.0 before death.
- C11/C12: predicted drop zero, actual 1336.0 before death.
- C10: one landing differs by about 470 XY units from prediction before the
  subsequent pit fall. This alone does not prove which command caused the fall.

The logs label the deaths `MOD_FALLING`; native CSV confirms descent to the
pit bottom. This is evidence against trusting candidate admission alone.
The precise first divergent simulation/live command has not been isolated.
`last_reason` in MOVECOMPARE can be stale and is not used as proof of a current
rejection. The selected chain angle-fraction telemetry describes downstream
search; the first candidate is simulated at boundary fraction zero. Do not
mistake that histogram alone for proof of a first-jump angle mismatch.

## Updated implementation direction

1. Use C01/C02 as the near-home opportunity cases, C07/C08 as low-resource
   tests and C09/C12 as mandatory failure controls. Preserve this baseline.
2. Expose legacy's current-state jump candidates through a shared native
   maneuver representation: exact first command, subsequent steering/hold
   schedule, receiving pose/velocity and continuation goal.
3. First establish prediction/live command agreement on these candidates.
   Trace the first divergence in C09 rather than tuning coordinates or relaxing
   clearance. Re-run identical commands through the same motion kernel and
   clock/resource/body rules used by live movement.
4. Admit only candidates with verified safe landing and receiving continuation
   through Runner's ownership/body/lifecycle contract. Replan from actual landing
   state; do not treat four-hop search score as an executed four-hop plan.
5. Repeat both teams, low/full resources and failure controls, then score complete
   objective trips including post-capture departure, death, stalls and CPU cost.
   Retain walking fallback where no safe improvement is demonstrated.

This refines the connected-hop next step; it does not authorize merging legacy
movement wholesale or mark movement acceptance complete. No game installation.

## Affected verification

Fresh dedicated engine/DLL build succeeded. All 32 fixture runs completed and
physical acknowledgements were checked; process exit zero is not an outcome
pass. Paired runtime settings differ only in comparison mode and isolated port;
fixture and staged executable/DLL hashes match. All 1,107 native source hashes
still match the build. The extracted compiled comparison-mode scope check passed;
three decision-seed and nine state-seed Python tests pass. No production movement
policy was changed by this comparison beyond the opt-in diagnostic dispatch.
