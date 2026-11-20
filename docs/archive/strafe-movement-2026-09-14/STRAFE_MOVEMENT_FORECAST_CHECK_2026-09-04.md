# Offline strafe forecast check

This extends the [independent feedback check](STRAFE_MOVEMENT_FEEDBACK_CHECK_2026-09-04.md).
XenJK remains the gameplay target. Two community players are preferred human references;
The owner's recordings are acceptable as the main demo corpus. The immediate test is
whether a complete future strafe-jump forecast agrees with later live movement.
It does not yet compare movement scorers or measure human skill.

The subsequent [two-maneuver comparison](STRAFE_MOVEMENT_COMPOSITION_CHECK_2026-09-04.md)
implements the first local-versus-continuation scoring test. This document
retains the preceding one-jump simulator evidence.

## Implementation and acceptance

`bot2_movebench.c` now computes the entire four-second fixture episode at the
first measured Pmove boundary, before executing that live move. It captures the
client, entity array, movement setup and controller once. Each future command
comes from its own predicted player state and controller history. Neither later
live commands nor live movement states are fed into this forecast.

The forecast advances `level.time`, `level.previousTime` and `level.framenum`
temporarily at the selected server cadence, then restores those fields and the
known Pmove globals before live execution. It reproduces fixed-step command-time
rounding. The collision world remains the real, static Kejim geometry.

Version 4 binary records store every future command, controller entry/exit and
player-state entry/exit before any live-boundary record. The decoder rejects a
forecast written after live movement. Failed episodes retain the forecast's
unobserved tail, explicitly marked as uncompared. Older version 2 and 3 evidence
remains readable.

Acceptance requires a complete forecast through the four-second episode end,
matching captured cadence and timestamps, verified native module and physics
settings, and a real takeoff and landing. Every submitted command, controller
state, movement-state byte and contact result must agree. The only comparison
exclusions are the previously identified server bookkeeping fields
`fd.forcePowerRegenDebounceTime` and `entityEventSequence`. Their differences
remain in reports, and full-state parity is reported separately. Prediction
never synchronizes those fields from live state.

Entity/client and level hashes check for mutations; explicit checks verify
restoration of the clock and known Pmove globals. This is a restricted diagnostic,
not a complete world sandbox: it does not clone engine collision, animation
assets, arbitrary global state or RNG state. Ordinary humanoid blaster movement
on supported surfaces is the current contract. Other players/NPCs, missiles,
triggers, non-world contacts, liquids, active force powers and surfaces that
spawn material effects remain excluded. These checks do not justify running
arbitrary combat or force actions speculatively.

One additional negative control biases only the offline forecast's strafe
steering by five degrees. The live policy, rolling feedback probe and individual
physics probe retain their original settings. This must produce an ordinary
forecast mismatch while the individual physics and rolling command comparisons
remain equal on the observed prefix.

## Evidence and reproduction

The fresh native build is `.codex-research/strafe-build-forecast-20260904`.
The prespecified matrix is `.codex-research/strafe-forecast-matrix-20260904`;
its `plan.json` records cases and source hashes before launching the episodes.
Each case stages its own engine, game DLL and matching symbols, verifies the
loaded module path, and preserves raw captures, decoded records and logs.
The 15 positive cases are the same settings and starting-condition probes as
the feedback check, with three negative controls: physics, rolling feedback,
and offline forecast. Initial velocity probes still occur before the neutral
preamble and may decay; they do not establish robustness to takeoff disturbances.

Forecast timing separates the sum of native Pmove calls from total forecast
construction, including copying, world hashing and restoration checks. Binary
capture writing is outside those timings. These diagnostic timings do not
establish the proposed production search budget or a multi-bot throughput bound.

Measured results from the completed matrix:

| Check | Result |
|---|---|
| Positive episodes | 15/15 completed with takeoff and landing |
| Offline forecast | 2,286/2,286 submitted commands, controller states, movement checkpoints and contact checks match |
| Other probes | Cloned-state physics and rolling feedback also pass all 15 episodes |
| Full player-state forecast | Differences only in the two reported server bookkeeping fields |
| World and clock checks | No detected entity/client/level mutation; known globals restored in all forecasts |
| Negative physics control | Rejected on sample 40 |
| Negative rolling-feedback control | Rejected on sample 66 |
| Negative offline-forecast control | Rejected on sample 66; one command/controller/movement mismatch, with cloned physics and rolling feedback still equal on the observed prefix |
| Native crashes | None in the 18-case matrix or additional 20 Hz episode |
| Python checks | 30 tests pass |

The positive matrix's forecast construction times have minimum/median/maximum
3.956/4.551/8.467 ms. The corresponding aggregate Pmove timings are
0.164/0.257/1.717 ms per four-second forecast. These are 15 single construction
measurements across different cases, not repeated latency samples of one search.
Construction currently exceeds the provisional 2 ms decision target; neither
32-candidate search throughput nor multi-bot cost has been established.

A separate nominal 20 Hz smoke check, recorded after the matrix in
`.codex-research/strafe-forecast-fps20-20260904`, passes all 80 commands and
movement checkpoints. It is additional cadence coverage, outside the original
matrix. The earlier `strafe-forecast-nominal-20260904` smoke check predates the
final timing boundary and bias-provenance logging; it is preserved separately
and is not included in the matrix statistics.

```powershell
python -B tools/strafe_fixture_build.py --out .codex-research/strafe-forecast-build-NEW
python -B tools/strafe_fixture_matrix.py --build .codex-research/strafe-forecast-build-NEW --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/strafe-forecast-matrix-NEW --port 29450
python -B -m unittest discover -s tools -p 'test_*.py' -v
```

Use new output directories. The runner keeps the prior Windows console fix and
child-only error-dialog suppression. Game installation assets remain read-only.

## Next discriminating experiment

The next step is to fix a short two-maneuver geometry and candidate set before
comparing local landing/time scoring with continuation feasibility/time scoring.
Both scorers must receive identical candidates, legal-input controllers, future
horizons and compute limits. The success criterion is reaching a usable second
exit, not merely touching the first platform. The broad-floor forecast check
supports that experiment's simulator; it is not evidence that the proposed
continuation scorer improves traversal. No production Bot2 scorer has changed.

Demo collection retains the prior interaction screening: a lack of visible
damage does not establish a clean segment, and missing nearby-player observations
leave body-block or projectile influence unresolved. No additional human segment
is approved for fitting by this simulator check.
