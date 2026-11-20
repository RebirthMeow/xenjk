# Moving the strafe controller toward live matches

Later update: the adapter now supports automatic selection from an inline
route catalog. See `STRAFE_MOVEMENT_ROUTE_CATALOG_2026-09-04.md` for the
two-team native evidence. The measurements below preserve the original
single-route experiment; full multi-bot movement cost remains unproven.

The owner redirected the investigation after the speed milestone: retain the
current speed and focus on cheap execution during a live match. The measured
spawn-to-flag result remains 6.70 s versus 6.60 s for frozen planned commands;
see `STRAFE_MOVEMENT_FLAG_RUN_2026-09-04.md` for that completed benchmark.

## Implementation

`bot2_strafe.c/.h` now contains the shared movement kernel. It reads the
current player state and an immutable guide, updates a caller-owned small
controller state, and emits legal commands. It has no file I/O, allocations,
entity mutation, global per-bot state, trajectory simulation or diagnostic
printing. Guide projection is a short local search, not a scan of the map.

The expensive steering work is reduced from 576 candidate evaluations to at
most 48 at the validated 40 Hz cadence. Key-angle offsets are calculated
once per command. View directions far beyond the turn limit duplicate the
same clamped view with a greater yaw penalty, so the fast mode retains the
nearest outside sample and the samples inside the turn limit. The exhaustive
mode remains available for differential checking.

`ai_bot2_route.c` connects this kernel to the ordinary `Bot2_ExecuteMovement`
dispatch. A guide is loaded and floor-probed once, cached and shared across
bots. Each client owns its own follower state. Activation uses a filename in
`bot_strafeRoute`, empty by default. An empty, missing, malformed, wrong-map
or wrong-team guide leaves normal movement in charge. Map checksum and team
are part of the asset header. Loading never overwrites game files.

The initial integration deliberately accepts the validated movement settings:
40 Hz, fixed 8 ms integration, floating movement, base speed 250, gravity 800,
rank-3 Speed and Levitation. It requires a slow entry near the guide root and
enough existing force. It aligns the view with legal commands before departure.
It does not teleport, refill force, grant equipment or alter player physics.
The broader Bot2 orchestrator already has equipment/force grants; this change
neither introduces nor validates those existing behaviors as human parity.

The adapter yields to combat aim/attack ownership, changed objectives,
specialized traversal, knockback/grip, lost Speed, stale updates, excessive
route error, lack of progress and detected body blockers. It keeps the existing
view smoother's history current for the handoff and clears follower state on
death/reset. Normal landing timers are allowed: initially treating them as
interruptions caused an unnecessary handoff after the first landing.

## Measurements and final integration check

- Extraction, fast kernel and normal movement-dispatch runs all reproduce the
  original 268-command nominal movement trace exactly and pick up at 6.700 s.
- The corrected dispatch run averages 29.5 microseconds per decision, p95
  57.2 microseconds. Its 1.258 ms maximum includes one-time route loading and
  floor preprocessing. These wall-clock samples are not a hard deadline.
- A 32-controller interleaved batch takes 0.531 ms on average and 2.353 ms at
  its measured maximum, excluding the exhaustive reference computation.
  It checks 8,576 controller decisions with **zero fast/full command
  mismatches**, using distinct initial view and target offsets. No decision
  evaluates more than 48 expensive steering candidates.

That batch exercises 32 independent controller states against recorded live
positions; it is **not a 32-player match**, and excludes native gameplay,
combat/tactics, per-player obstacle probes, network and logging costs. The
dispatch benchmark still uses the fixture for initial setup and capture; it
calls normal movement dispatch, not the complete `Bot2_Think` match loop.

Artifacts: `.codex-research/strafe-shared-{slow,fast}-20260904`,
`strafe-runtime-landtimer-20260904`, and `strafe-runtime-budget32-20260904`.

The final 12-case dispatch matrix passes: all actual pickups, identical
movement/commands/ground contacts and integration clocks to the frozen
step-3 feedback traces, and continuous route ownership throughout each run.
That is **3,248 matching native command checkpoints**. Phase-6 exposed a
guard that incorrectly measured the final approach against the guide endpoint;
it now measures against the actual flag and retains a bounded braking radius
based on approach speed (256–512 units). This changes when the adapter yields,
not the pickup criterion or the controller's commands. The corrected case
again matches the 7.825 s reference exactly.

Final matrix: `.codex-research/strafe-runtime-accepted-matrix-20260904`.
Independent raw-CSV/hash audit and frozen source snapshot:
`.codex-research/strafe-runtime-accepted-audit-20260904`.
The largest per-episode p95 dispatch cost is 0.133 ms and largest individual
sample is 2.332 ms, including load/preprocessing on the first command.
A fresh native build, subsequent guard-correction build, 36 Python tests,
and a 122-checkpoint movement-fixture regression pass. No test server remains
running. The source snapshot retains the compiled version; the final working
tree also restores the original no-final-newline convention in `ai_bot2.c`
and `.h`, a whitespace-only difference.

## What the interruption test revealed

At 2.5 s, a separate test gives view ownership to the existing combat-aim
path. The route adapter releases once and does not fight the persistent aim
lock. Existing movement eventually picks up the flag in 15.25 s. This confirms
the handoff, not acceptable combat-route performance.

That fallback path has a **65.1 ms movement-decision spike** in the test.
The incumbent running-jump path can synchronously explore four-jump native
prediction chains. The new route kernel avoids that work while it owns
movement, but handing control back can expose it. Therefore this is an
experimental live-dispatch component, not a claim that the whole bot already
meets a live-match frame budget. Artifact:
`.codex-research/strafe-runtime-aim-handoff-20260904`.

The next live-match performance work should budget recovery/proposal work
across frames or preprocess/cache alternatives, with a cheap current-command
policy while planning. Then profile several actual bots with combat and
body-blocking. That is more useful now than shaving another tenth of a second
off the clean nominal route. The current body-block and knockback guards are
conservative exits; recovery from those events has not yet been established.

Follow-up completed: `STRAFE_MOVEMENT_RECOVERY_BUDGET_2026-09-04.md` records a
bounded-search prototype and seven paired recovery cases. It reduces prediction
cost but introduces two new deaths, so it remains opt-in and disabled by
default. The next experiment should retain route guidance when choosing a
recovery maneuver instead of relying on a truncated strategic search.

Subsequent correction: `STRAFE_MOVEMENT_RECOVERY_FORENSICS_2026-09-04.md`
identifies uncontrolled random ties and guide-command leakage in those
fallback trials. Their causal regression claim is withdrawn. The clean route
comparison remains valid; corrected fallback trials and a surviving
single-choice alternative are recorded separately.

## Trying the component

The portable asset is `tools/fixtures/strafe_flag/kejim_red_i2_runtime.txt`.
Place a copy under the chosen runtime's `base/botroutes/` directory and set
`bot_strafeRoute "botroutes/kejim_red_i2_runtime.txt"`. The updated game DLL
must be loaded, Bot2 must be enabled for the bot, and the above physics and
entry conditions must hold. Only the Kejim red-i2 route is supplied; other
spawns and teams use incumbent movement. No installed game DLL or server
configuration was changed by this work.

For a fresh isolated dispatch run:

```powershell
python -B tools/strafe_flag_runner.py --build .codex-research/strafe-build-live-final-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/runtime-NEW --tape tools/fixtures/strafe_flag/kejim_red_i2_guide.csv --mode runtime --fps 40 --port 29712
```

Add `--budget-bots 32` for the differential kernel batch, or
`--interrupt-ms 2500` for the separate persistent-aim handoff test. Diagnostic
I/O and exhaustive differential checks belong to this fixture, not the
production controller's per-frame implementation.

To repeat the adapter comparison against the preserved acceptance traces:

```powershell
python -B tools/strafe_runtime_matrix.py --build .codex-research/strafe-build-live-final-20260904 --assets 'game_directory/Jedi Academy/GameData' --reference .codex-research/strafe-flag-final-matrix-20260904 --out .codex-research/runtime-matrix-NEW --port 29750
```
