# Independent strafe feedback check

This continues the [native fixture](STRAFE_MOVEMENT_NATIVE_FIXTURE_2026-09-04.md).
The immediate question is whether prediction and execution generate the same
legal commands from independently carried state through a strafe jump and
landing. This is a foundation for the two-maneuver experiment, not an evaluation
of objective play or intermediate-human skill.

Owner decisions after this check: XenJK is the target gameplay environment;
Two community players are preferred skill references, and the owner's demos are acceptable as
the expected majority of the corpus. See the first-experiment product target.

The subsequent [offline forecast check](STRAFE_MOVEMENT_FORECAST_CHECK_2026-09-04.md)
adds a complete prediction generated before the first live movement step. The
results below retain the earlier rolling-feedback scope and evidence.

## What changed

The live script now uses an explicit controller state: previous yaw and command
time, jump start time, previous upmove, and waiting/jump/airborne/landed phase.
One pure input function is called independently for live and predicted state.
It retains the broad-floor run/strafe/brake schedule, uses velocity feedback for
strafe steering, requires ground contact and a released jump button before
launch, and sends a single command with positive upmove before releasing it.
Commands are still issued once per
server tick, not once per physics integration step.

The configured yaw cap is 360 degrees/second. This is an explicit engineering
constraint, not a measured intermediate-player target. Controller yaw remains
continuous; only output commands use the game's short-angle representation.
The initial implementation used `AngleNormalize180` for controller history,
which itself quantizes angles and produced 360.1318 degrees/second at 40 Hz.
Replacing that normalization with a continuous wrap removed that extra
quantization. Float arithmetic and command-angle quantization are measured
separately rather than mistaken for a different command cadence.

A third Pmove probe captures its player state, self client, entity array and
movement setup once at the first measured call. It keeps its own controller and
player state thereafter. It does not receive later live state, commands, entity
copies, or movement-setup refreshes. Its self position is advanced from its own
prediction. Command timestamp rounding reproduces the supported
`ClientThink_real` fixed-step preprocessing.

The existing cloned-state and recorded-command probes remain available beside
it, so a failure can be attributed to individual physics, replay, or feedback.
Version 3 binary records add both generated commands, both controllers'
before/after states, and the independent probe's player-state entry/exit.
The decoder still reads older version 2 captures.

## Exact acceptance contract

Successful feedback checks require complete matching records, verified loaded
DLL and settings, equal submitted commands and controller states, and equal
touch lists, hulls, and water classification. A positive vertical takeoff with
jump input and a completed airborne-to-landed controller phase are required;
mere settling onto the floor does not satisfy jump coverage.

The player-state comparison is byte-exact except for the two already diagnosed
server bookkeeping fields: `fd.forcePowerRegenDebounceTime` and
`entityEventSequence`. Those differences are still recorded, and full-state
parity remains a separate result. No fields are synchronized back into the
prediction. All other bytes, including flags, animation timers and unclassified
bytes, must match; equal position alone cannot pass this check.

This exclusion applies only to the current ordinary, force-disabled experiment.
Force-dependent maneuvers need XenJK's own resource updates modeled. Historical
base_enhanced settings inform demo interpretation; reproducing those servers is
not required for the chosen product target. A 30 Hz/gravity-760 XenJK case does
not establish a historical-server replay.

The predictor still uses the real static collision world, animation assets,
callbacks, and the server clock as it advances. It is a rolling independent
feedback check, **not yet an offline future rollout or independent server-world
simulation**. The earlier exclusions for other players/NPCs, missiles, triggers,
non-world contacts, liquids and effect-spawning surfaces continue to apply.

## Prespecified cases and evidence

`tools/strafe_fixture_matrix.py` records its plan before launching cases and
runs each in a fresh process and directory. The 15 positive cases cover:

- Four combinations of fixed/float movement at 40 Hz.
- 30 Hz with ordinary and fixed integration, plus a gravity-760 case.
- Lateral starting position offsets of -16 and +16 units.
- Initial world X/Y velocity offsets of -50 and +50 units/second.
- Jump/strafe onset shifted by -25 and +25 ms.

Initial velocity is applied at fixture setup, before the neutral preamble and
run-up, and can decay before takeoff. These are agreement checks, not a sampled
disturbance distribution or evidence of an entry region that reliably recovers
from launch-time errors. The map is still the single Kejim floor fixture.

Two negative controls are also prescribed: invert only the individual shadow's
`pmove_float`, or bias only the independent predictor's strafe steering by five
degrees. The latter must demonstrate a command mismatch while the individual
cloned-state physics comparison still passes. Expected mismatches return an
ordinary failed-fixture result, not a native application crash.

The authoritative final rerun uses `.codex-research/strafe-build-feedback-verified-20260904`
and `.codex-research/strafe-feedback-matrix-tap-20260904`. Its `plan.json`
records source hashes; `summary.json` includes every outcome. Each episode
retains binary hashes, captures, decoded states, native metadata and logs.

Final audited results are in `.codex-research/strafe-feedback-final-20260904/report.json`:

| Check | Result |
|---|---|
| Positive cases | 15 completed, each with one takeoff and landing |
| Independent feedback | 2,286/2,286 commands and controller/movement checkpoints agree |
| Cloned-state physics | 2,286/2,286 full player states agree |
| Full-state independent feedback | Fails only the two reported server bookkeeping fields |
| Predictor-only physics inversion | Rejected on sample 40 with one full-state mismatch |
| Predictor-only steering bias | Rejected on sample 66; command, controller and movement diverge while individual physics still matches |
| Native crashes after console fix | None in either 17-launch matrix or the gravity correction run |
| Python checks | 27 tests pass, including inherited error mode, gravity mismatch rejection and unsupported capture evidence |

The audited set uses 12 cases of 160 commands at 40 Hz and three cases of 122
commands at 30 Hz. Controller yaw rate peaked at 360 degrees/second at 40 Hz
and 360.000148 at 30 Hz (float arithmetic). Quantized command yaw changes peaked
at 360.131836 degrees/second; rounding to 360/65536-degree angle units explains
the small difference. Peak horizontal speeds ranged from 376.70 to 392.22 units/s.
These are diagnostic observations, not a human-skill score or throughput test.

The gravity-760 case required a configuration correction. Kejim's worldspawn
sets gravity to 800 after startup cvars are supplied. Inspection of the raw
player-state capture caught that the original requested-760 case actually ran
at 800. It is preserved and explicitly excluded from gravity-760 coverage.
`strafe-feedback-gravity760-verified` is the replacement: gravity is set through
the owned server's RCON after map load, allowed to update before arming, and
verified as 760 in every captured movement root. The decoder now reads actual
gravity and base speed, and all settings gates require the requested gravity
and speed 250 to match captured state. The other 14 cases were rechecked at
gravity 800 from their preserved captures. The consolidated report identifies
the original excluded case and its replacement without overwriting either.

The preceding `strafe-feedback-matrix-verified-20260904` completed all 17
launches without an access violation after the console fix, but rejected its
30 Hz/fixed case for activating force jump. The earlier controller held upmove
for a nominal 100 ms: commands at 0, 33, 66 and 99 ms could remain held through
more than 100 ms of rounded physics advances. Its final captured state had
`forcePowersActive=2` and levitation level 1. `WP_ForcePowersUpdate` in
`w_force.c` enforces minimum levitation level outside Siege, restoring it after
the fixture's initial zeroing. The force-disable cvar alone is therefore not a
guarantee that held jump input cannot invoke force-jump behavior.

The single-command tap fixes the input contract without changing production
force mechanics or resource regeneration. Supported-state checks now reject
active force powers both before and immediately after live Pmove. New tagged
unsupported-boundary records preserve the full actual state without inventing
a predicted result, so the decoder can retain valid preceding evidence. The
older rejection remains preserved, including its original parser failure on
the terminal server checkpoint; it is not counted as a completed movement run.

## Windows crash found during the experiment

The first matrix included startup access violations, and the user saw a Windows
application-error dialog. These are preserved as failed launches, not movement
observations. The screenshot's address `0x77C49623` matches
`strafe-feedback-matrix-20260904/negative_feedback/jampgame_crash.{txt,dmp}`.
The dump identifies `ntdll.dll!memmove+0x33`. An x86 frame-pointer walk,
symbolized with the matching local PDBs, includes:

```text
KERNELBASE
CON_Show -> CON_Hide -> Sys_Print -> Com_Printf
NavMesh_Log -> NavMesh_InitForMap -> G_InitGame
GVM_InitGame -> SV_InitGame -> SV_InitGameProgs -> SV_Map_f
```

The failure is in console redraw during startup logging. With stdout redirected
to the runner's pipe, `GetConsoleScreenBufferInfo` fails. `CON_Show` used the
uninitialized window bounds to form a pointer into a stack line buffer for
`WriteConsoleOutput`, leading to an invalid read. `CON_Init` also consumed an
uninitialized result from the same API. Both calls now check success before
using that data in `shared/sys/con_win32.cpp`; redirected text output continues
through the existing stderr path.

Runner changes also keep startup arguments below the legacy parser's limits by
writing a local config, use `+safe` to skip personal autoexec/server configs,
and suppress Windows error dialogs only for launched test children. The parent
error mode is restored immediately after spawning. A subprocess test verifies
the documented child inheritance and parent restoration; it does not provoke
an actual crash. See Microsoft's
[SetErrorMode documentation](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode).

The suppression is separate from the console fix: it keeps an unexpected failure
from interrupting the desktop while the runner still records the nonzero exit.
Skipping personal configs alone did not fix the crash, as preserved by
`strafe-feedback-safe-early25`. The first long command-line failure is not
retrospectively assigned a root cause without its own dump.

## Reproduce and next step

```powershell
python -B tools/strafe_fixture_build.py --out .codex-research/strafe-feedback-build-NEW
python -B tools/strafe_fixture_matrix.py --build .codex-research/strafe-feedback-build-NEW --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/strafe-feedback-matrix-NEW --port 29350
python -B -m unittest discover -s tools -p 'test_*.py' -v
```

Single episodes use `strafe_fixture_runner.py` with the same build/assets/out
arguments. Its options include `--fps`, `--fixed`, `--float`, `--gravity`,
`--regen`, `--yaw-rate`, `--velocity`, `--jump-delay`, and the two negative-control
flags. Use new output paths so prior evidence remains available.

The next physics gate is a bounded future rollout of this same policy from a
captured state, using an explicit future clock and a supported world contract.
Then fix the two-maneuver geometry and compare local landing scoring with
continuation-aware scoring under identical candidate and compute limits. No
production Bot2 scorer has been replaced, and no human demo has been approved
for control fitting by this experiment.
