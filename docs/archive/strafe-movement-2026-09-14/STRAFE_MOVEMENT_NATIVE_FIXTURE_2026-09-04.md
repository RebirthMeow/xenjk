# Native movement fixture: first measured result

Date: 2026-09-04. Continues the research handoff and first-experiment notes.
Subsequent work: [independent feedback check and Windows console fix](STRAFE_MOVEMENT_FEEDBACK_CHECK_2026-09-04.md).
The product target remains an intermediate-human offense player: objective
progress, survival, appropriate force use, human accuracy, and usable strafing.
This experiment establishes a narrow movement measurement foundation; it does
not rate the bot's human likeness or establish the proposed continuation scorer.

## What now works

The new solo fixture completed a run, strafe takeoff, airborne steering, landing,
and braking on Kejim in four movement configurations. All **640 individual
Pmove calls matched the full player state exactly**, together with touch lists,
hull, water classification, output command, and trace-call count. The second
probe carried its own player state across the recorded command stream. Its
position, velocity, view, ground contact ID, and command time matched every step.
No numeric tolerance was used.

Full-state agreement across the entire replay **fails**: force-regeneration
timing and event-replication sequence differ outside Pmove. These fields are
recorded and reported, never omitted to turn a failure into a pass.

An intentional predictor-only `pmove_float` inversion failed on sample 40,
the first running command, with one full-state mismatch. The runner returned
exit code 1 and all three parity gates false. The first 39 stationary samples
alone would have missed this error.

## Implementation and scope

- `codemp/game/bot2_movebench.c`: native fixture, single-episode command,
  scripted command generation, Pmove interception and diagnostics.
- `bot2_movebench.h` and `bot2_movebench_fields.inc`: small hook interface and
  compiler-derived offsets/sizes for full player-state field inspection.
- `g_active.c`: a command-driver call in `G_RunClient` and one normal-player
  Pmove wrapper. Inactive calls retain the usual execution path.
- `g_svcmds.c` / game CMake: command registration and build entries.
- `tools/strafe_fixture_build.py`: fresh Win32 dedicated/game build, explicit
  optimization and symbols, no installation or cleanup operation.
- `tools/strafe_fixture_runner.py`: fresh isolated process/homepath, staged
  matching executable/DLL/PDBs, localhost RCON, capture decoder and parity gates.

The command requires a fresh dedicated cheats-enabled process and one live bot,
with `bot_forcebot2=0`. `bot_movebench_hold=1` keeps the bot idle before arming
and after completion. Commands go through `ClientThink` and `ClientThink_real`,
including command-receipt timing; they are issued once per server tick. The
fixture explicitly equips a blaster and disables force abilities before the
measurement. Actual pre-Pmove state is then cloned without sanitizing fields.

Other players/NPCs and missiles abort the run. Trigger swept-box overlap,
non-world trace contacts, start/all-solid traces, liquids, and landing-effect
materials are excluded. The accepted scene is ordinary humanoid movement on
static world geometry, without attacks, active force powers, or vehicles.
This directly avoids rockets and body blocks in the baseline experiment.

The shadow has a copied entity array and self client/player state, uses real
collision callbacks and animation data, and restores known Pmove globals.
Entity/client hashes before and after prediction detect mutations in that
memory. This is a restricted diagnostic, **not a general side-effect sandbox**:
hashes are not a proof of purity, arbitrary callbacks/global RNG/effects are
not isolated, and future scenarios need fresh audits. No claim is made that
production `SimulatePmoveTrajectory` has been fixed or validated by this probe.

Binary captures retain full input pmove, root/live/shadow player states,
submitted commands, and before/after `ClientThink_real` player states. Tagged
chain records retain the second probe's entry/exit states. Pointer values in
the binary are provenance only; this is a versioned native ABI capture, not a
portable savegame. The decoder rejects malformed/truncated/duplicate records.

## Measured matrix

All runs: `mp/ctf_kejim`, origin `(2124.32,3320.06,1808.2)`, yaw 0,
40 Hz server, speed 250, gravity 800, step-slide fix enabled, integration msec 8.
This deliberately differs from the historical Kejim demo's gravity 760; no
historical trajectory fit is being claimed. The command schedule lasts four
seconds and contains one jump, with velocity-based yaw during its strafe phase.

| Preserved run | fixed | float | command advance (ms) | exact steps | chain full-state mismatches | peak horizontal speed |
|---|---:|---:|---|---:|---:|---:|
| `strafe-fixture-07` | 0 | 0 | 25 x 160 | 160/160 | 157 | 398.00 |
| `strafe-fixture-08` | 1 | 0 | 24 x 140; 32 x 20 | 160/160 | 156 | 407.54 |
| `strafe-fixture-09` | 0 | 1 | 25 x 160 | 160/160 | 156 | 397.18 |
| `strafe-fixture-10` | 1 | 1 | 24 x 140; 32 x 20 | 160/160 | 155 | 400.78 |
| `strafe-fixture-11-negative` | 0 | 0, inverted in shadow | 25 x 40 | 39/40, aborted | not a completed replay | 63.00 |

The 24/32 ms advances result from rounding commands onto the fixed 8 ms grid;
they are not individual internal substeps. Command cadence and integration
cadence must remain separate in the simulator.

Every valid run had zero chain movement mismatches and zero movement differences
between the Pmove output and the immediate `ClientThink_real` return. That
server checkpoint is not the end of the whole server frame. It changed
`entityEventSequence` on two frames. Across the chain, `entityEventSequence`
differed on 96 frames; the remaining differences were
`fd.forcePowerRegenDebounceTime`. Source explains both: `bg_misc.c` advances
the event sequence during player-to-entity conversion; `w_force.c` advances
regeneration timing during force updates outside Pmove even with no powers in
use. Eventual resource use will require modeling the corresponding server work.

### Historical server regeneration is a separate compatibility requirement

Subsequent owner decision: target XenJK's current gameplay. The historical
compatibility considerations below apply to interpreting or explicitly replaying
old demos, not to a requirement to emulate base_enhanced in the finished bot.

The owner reports that these servers used base_enhanced with frame-dependent
regeneration, tuning a custom regeneration value together with server frame
timing to obtain the desired rate. Treat that as a compatibility requirement
to verify for each server version, not as permission to replace its behavior
with XenJK's timing model or to assume one setting for the whole corpus.

The three preserved initial gamestates provide the following evidence:

| Sample | Advertised game | Advertised sv_fps | Advertised g_forceRegenTime |
|---|---|---:|---|
| ctf4, 2015 | base_enhanced | 30 | 200 |
| Kejim, 2021 | base_enhanced | 30 | absent / unknown |
| Kothis, 2025 | base_enhanced | 30 | absent / unknown |

Source: `.codex-research/strafe-20260904-interactions/summary.json`, serverinfo
only; its older interaction verdict counts are superseded by the v2 screens.
These are initial advertised settings. The current decoder does not apply
subsequent server-command configstring updates. An absent regeneration value
must not be filled with a default, and the game name does not identify the exact
server implementation. No base_enhanced regeneration implementation has yet
been verified in this investigation.

XenJK currently catches up overdue regeneration with a `while` loop and adds
the interval to the previous deadline in `w_force.c`. That is the local source
behavior; it is not evidence that the historical base_enhanced builds do the
same. Replay compatibility must capture implementation/version, configured
interval, server update timing, and relevant resource state together. Copying
only `g_forceRegenTime` is insufficient if the timer update rules differ.

The native fixture mismatch reported above occurs entirely within XenJK:
the rolling Pmove-only probe does not perform the server's regeneration or
event-conversion updates. It is not evidence of a base_enhanced bug or a
demonstration that historical regeneration values are wrong. Event sequence
bookkeeping is a separate mismatch from force regeneration. The force-disabled
ordinary-jump experiment can continue with an explicit movement-only contract
while full-state parity remains false. Matching historical regeneration becomes
necessary before evaluating force-dependent maneuvers or resource decisions.

The chain replays commands generated from the **live** state. It does not yet
generate future commands from its own predicted state or advance a complete
independent server world. It therefore establishes recorded-command movement
agreement in this scene, not feedback-controller horizon agreement.

## Provenance, cost, and verification

Artifacts are under `.codex-research/`. The consolidated reanalysis is
`strafe-fixture-matrix-20260904.json`; it leaves each original capture/report
untouched and includes capture, binary and movement-source hashes. Each run
contains `manifest.json`, `server.log`, `report.json`, `decoded.json`, and
`home/base/bot2_movebench.{bin,meta}`. Native code reports its actual loaded DLL
path, which matched the runner's staged DLL in every matrix run.

The map's native checksum was `-1301924053`. The sole inventoried Kejim BSP,
`base/ctf_kejim.pk3/maps/mp/ctf_kejim.bsp`, has SHA256
`76537a48e7d7273e2be6c033f281f8e0dfa49fcc8d550c334e4a8b0a42682e37`.

The authoritative build is `strafe-build-verified-20260904`, a fresh VS 2022
Win32 RelWithDebInfo build with `/O2 /Ob1 /Zi` and matching symbols. Subsequent
fixture edits rebuilt successfully. Initial build/run attempts are preserved;
their unoptimized timing, incomplete provenance, and rejected geometry are
not mixed into this matrix. No game installation was replaced.

Across the four valid runs, shadow-Pmove p50 was 3.6–6.8 microseconds, p95
12.6–16.9 microseconds, and maximum 254.5 microseconds. Diagnostic probe p50 was
2.89–3.10 ms due to full entity copying and hashing. That probe interval includes
live movement and one shadow run, but excludes the second rolling probe and
file/logging overhead. These are instrumented single-call timings, not a
controller-budget benchmark. The proposed 1,280 calls / 2 ms decision budget
is unproven and should not be assumed feasible from these results.

Python checks: 20 passing tests, including strict capture parsing, incomplete
evidence/provenance rejection, negative process exits, and conservative demo
interaction screening. Native checks: four positive episodes and the expected
failure above, using the same source-built binaries. Earlier runtime failures
remain preserved; this does not diagnose the legacy smoke-test crash.

## Next discriminating step

1. Define the complete state/update contract for ordinary movement over a
   horizon, including outside-Pmove bookkeeping; keep resource limitations
   explicit. Do not silently copy live fields into future predicted states.
2. Generate the same legal feedback policy from predicted state and live state,
   with explicit last-view and jump-button phase. Check commands and states
   through a full run including landing, then controlled initial disturbances.
3. Only after that, run the predeclared two-maneuver comparison: local landing
   scoring versus continuation-aware scoring with identical candidates/budgets.
   Score usable second exits and traversal completion, not peak speed.

Human demos remain separate evidence. The sampled 11 paired airborne candidates
are five suspect and six unknown after conservative interaction screening;
**zero are approved for control fitting**. Empty visible-entity lists or absence
of damage do not establish freedom from body blocks, force effects, or earlier
knockback. A candidate needs sufficient surrounding history and collision/
impulse evidence; its demo category alone is not an isolation guarantee.

## Reproduce

From the repository root, using new output directory names:

```powershell
python -B tools/strafe_fixture_build.py --out .codex-research/strafe-build-NEW
python -B tools/strafe_fixture_runner.py --build .codex-research/strafe-build-NEW --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/strafe-run-NEW --map mp/ctf_kejim --origin 2124.32 3320.06 1808.2 --yaw 0 --fixed 0 --float 0 --port 29285
python -B -m unittest discover -s tools -p 'test_*.py' -v
```

Use separate outputs for the four fixed/float combinations. Add
`--negative-control` for the expected failure. `status=complete` denotes fixture
completion; read the three explicit parity gate booleans for the scientific
result. Older reports 07–09 predate those booleans; the consolidated reanalysis
provides them for all matrix runs.
