# R9: prepare during waits and continue bounded recovery

The owner authorized recovery work after discussing useful exploratory motion,
preparing alternatives during waits, and distinguishing unavoidable falls from
controller errors. Continue from local `master` at `b2fe7aa1`. The active
[plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) and unchanged acceptance gates govern.
Timing optimization and independent resource references are separate remaining
packages; this work does not install a game build.

## Native fall investigation

The R8 timing sample `adapter-s4` lost client 1 to `MOD_FALLING` at level
28400, elapsed 14400 ms. Timing-only telemetry cannot classify avoidability.
The fixture has idle brains, Saber and cleared attack buttons, but explicitly
injects a +150 Y velocity impulse into client 1 at elapsed 2525 ms. Rocket
damage is not supported by this workload. No active-flight preparation or
entry for client 1 appears in the original buffered log.

Four diagnostic eight-client worlds on frozen R8 are retained at
`.codex-research/strafe-r9-fall-traces-20260909/`. Each contains 6400 pre/post
rows, no death and no between-frame discontinuity. They reproduce the same
physical trajectory, differing only in native short-angle encoding; these are
not four diverse successes. Client 1 jumps at elapsed 8775 from approximately
(1546,2203,1808), lands at 10375 on z=1368, takes 13 fall damage and survives
with 87 health to reach the opposite raised stand by 20000. The original fall
remains unreproduced and unclassified. No further identical probes were run.

An unavoidable fall can be an explained movement outcome, but losing a living
client still invalidates a timing sample claiming eight active clients.
`MOD_FALLING` also labels delayed pit-trigger death, so a future capture must
examine the departure/pit entry before death, not just its last grounded row.

## Production recovery changes

All behavior changes are private to `ai_bot2_localnav.c`:

- Begin the existing bounded candidate scan after 250 ms without progress,
  while retaining the 1500 ms passing-obstruction grace before taking recovery
  ownership. Scan one candidate per global game frame; ordinary movement keeps
  its command destination during preparation. Progress invalidates pending work.
- Revalidate a prepared step from actual state before admitting it. A moving
  blocker can invalidate an earlier proposal; cached clearance is not executable
  authority.
- On successful arrival, first query current ordinary ground guidance. If it
  is usable, hand back. If it remains blocked, a fresh floor/clearance check can
  admit another 64-unit segment in the successful direction, including movement
  that does not shorten straight-line objective distance. Require an onward
  topology path. No speculative air maneuver or resource change is added.
- Bound each episode to three steps and six seconds. Timeouts never count as
  successful arrivals. Store four recent recovery origins and reject candidates
  within 24 units of them to discourage short backtracking loops.
- Continuation uses at most two ground corridors and one path query, once
  globally in that frame; ordinary scans and execution validation remain bounded.
  Complete runtime cost will require qualification after behavior stabilizes.

This is incremental local preparation plus bounded continuation, not a general
multi-step optimizer. A safe step with unknown onward topology is still refused;
the policy does not blindly move when no physically valid proposal is available.

## Retained review failure and correction

The first behavior build still wrote current origin into a caller's destination
before returning false from preparatory scanning. The ordinary-navigation caller
uses its selected destination after that false return, so the write could brake
movement and suppress a valid sidestep. Independent review caught this contract
violation. The original collection in
`.codex-research/strafe-r9-outcomes-20260909/` was stopped, and all partial/completed
recordings retained with `aborted.json`; they are not qualification passes.
The scheduler was terminated and its native worlds shut down before correction.
The fix removes that false-return write. The harness now asserts an unchanged
sentinel destination on every tested false path, including failed admission.

## Frozen verification

Final behavior build:
`.codex-research/strafe-build-r9-recovery-691d9d112f0440e7bc89313574862f62`.
Manifest SHA256:
`5545eb190aa1c1ea8abdd774b937440aa3eb65b8faf206fa4a279d9aa06afa70`;
game DLL SHA256:
`4acf0e3b5d21fe2cac19656b5c7ae521a25a36f04c2cac7dcd927f8b8183775a`.
Earlier unique builds remain retained, including the unmeasured telemetry-only
iteration and the build containing the reviewed false-return defect.

`tools/strafe_recovery_episode_check.py` extracts the actual ground-corridor and
recovery code into a Win32 harness with mocked collision/nav boundaries. The
final check passes at `.codex-research/strafe-recovery-episode-check-20260908-c/`:
warm preparation ownership, unchanged false-return destinations, moving-blocker
revalidation, safe continuation, clear-route release, step/time limits, timeout
priority, visited history, floor/topology refusals and the shared frame budget.
This establishes policy contracts, not native pressure escape or timing.

The fixed native declaration is
`.codex-research/strafe-r9-outcomes-fixed-20260909/plan.json`: red 1x repetitions
2/3, red 5x repetition 3 and blue 5x repetition 2, all former R7 stalled cases
at their original seeds/rates and 600 game seconds. Also retain all ten previously
admitted corner poses with unchanged resources/body setup. These are targeted
development cases, not the complete candidate/baseline continuity cohort.
At most four isolated outcome worlds run at once; sources remain frozen.

## Completed outcomes

All four pressure cases complete, seeds confirmed, clean shutdown and zero
audited distant stalls. Red 1x repetition 3 and red 5x repetition 3 retain long
classified goal waits with unavailable home flags; those remain visible in the
raw idle totals. The former R7 stalls are not reproduced. Same seeds do not
give identical combat timelines, so these four development observations are
not a controlled causal estimate or the full 12+12 qualification.

There are 14 initial recovery steps across the four pressure cases, but **zero
`corner_continue` events**. The two completed 5x cases resume travel after their
arrived steps; their sampled defenders are already at least about 780 units
away at arrival, so those steps do not prove escape from sustained nearby
pressure. The blue fifth step ends on objective change. Input-bound review is
`.codex-research/strafe-r9-analysis-20260909/defended-continuation-review.json`.

All ten retained admissible corner cases finish without distant confinement or
death, and carrying cases capture. The sampled first displacement beyond 64
units improves from 3775 to 3025 ms in both body-blocked stand cases A4/C2b.
Six other cases preserve that sampled crossing time, B1 improves 50 ms and D1
is 25 ms later. This is sampled crossing evidence, not continuous timing proof.
The comparison is retained in `corner-comparison.json` in the R9 analysis root.

All 30 adapter-armed resource cases pass the existing audit with exactly the
same pickup/capture event sequences as R8. All 26 frozen temporary-body cases
are valid and pass. Evidence is `.codex-research/strafe-r9-regression-20260909/`.
Those regressions use two workers while the two long pressure cases finish;
the total never exceeds four native worlds. This does not requalify full M2.

Ten additional static pressure/control cases are declared and retained in
`.codex-research/strafe-r9-static-pressure-20260909/`: red (2550,5582,1913),
yaw -110; blue (53,-1450,1913), yaw 70; each with four cardinal 36-unit body
offsets and a no-body control. Force 0, carrying, no Speed, 20 game seconds.
All ten admit, survive, and cross 64 units within 1275..3275 ms with no sampled
confinement. None executes `corner_continue`; a single static body usually
clears during the first step. No placements were replaced to force branch
coverage. These probes do not represent a defender following a sidestep.

## Status and next decision

Preparing during waits has native benefit in the retained blocked-corner cases;
the false-return handoff contract is corrected. Bounded continuation is
implemented and checked with mocked native boundaries, but its actual execution
under sustained moving pressure remains unobserved. Do not claim that branch
qualified or the original fall fixed. Further pressure work should use an
observed moving-body episode, not another arbitrary static-placement sweep.

The next integrated evaluation still requires the complete defended cohort and
retained M0/M1/full-M2, full-route and lifecycle gates on the final revision.
Independent planned resource references and cost optimization remain separate
work. The R8 timing failure remains retained; no new timing claim is made here.
K07, normal MP installation and map transfer remain open.
