# Recovery forensics: command context and one consequential choice

Follow-up: `STRAFE_MOVEMENT_AIM_FRAME_2026-09-04.md` implements and tests the
airborne live/simulator steering discrepancy identified below. The observations
in this document describe the preceding implementation and remain historical
controls for that experiment.

## Result

The two previous apparent regressions were not a sound basis for choosing a
precomputation architecture. The investigation found uncontrolled random
tie-breaking and a command-context bug in the native test fixture. Both are
now controlled or corrected. The original artifacts remain intact.

With the corrected fixture, one previously failing case succeeds with the
capped planner. The other has an explicitly demonstrated surviving alternative:
from **byte-identical full player state and Bot2 state**, changing the initial
strafe direction alone leads to native flag pickup instead of death.
This proves existence for that particular state and subsequent fixture inputs;
it does not provide a general selector or certify disturbance recovery.

## Why the earlier comparison was misleading

The old phase-3 pair first differed at 2.500 s, the exact aim handoff frame,
before either method performed a jump-chain search. The WALK-to-AIRBORNE
transition calls `EvaluateStrafeDir`, whose equal-floor-score case uses
`Q_irand`. The full and bounded arms chose different directions. With paired
random inputs they produce identical commands and both die. The earlier
statement that this pair demonstrated a cap-induced regression is withdrawn.

The fixture also populated `usercmd_t` from the guide before calling ordinary
movement. A running-jump launch can set only `upmove`, leaving the other
command fields unchanged. In actual `Bot2_Think`, those fields come from the
previous bot command. In the old fixture they instead came from a guide row,
with yaw initialized to zero packed command angle. The old 7.900 s launch
therefore jumped with guide axes and an effective 67.5-degree view despite the
persistent +X aim intent. Those results do not establish live fallback quality.

Runtime fixture dispatch now carries its previous command forward and clears
buttons, matching the movement call's ordinary command-inheritance contract.
It still supplies the declared saber/force setup and scripted objective/aim;
it is not the complete Bot2 combat/force-management loop. The old context is
available only through `--legacy-command-context` for forensic reproduction.

## Instrumentation

`--capture-state` writes `flag_states.bin` plus `flag_state_layout.txt`:
compiler-sized full pre/post `playerState_t`, full pre/post `bot2_state_t`,
the submitted `usercmd_t`, and both elapsed/absolute clocks. Header version 1
records all structure sizes; player-field offsets come from the existing
compiler-derived field list. Selected Bot2 offsets aid inspection; the entire
Bot2 struct is captured regardless. This is a capture format, not a world-state
restore format. It does not claim to serialize movers, the navmesh, or all
entities. Runs are native executions from the ordinary isolated spawn fixture.

Positive `--decision-seed` selects separate deterministic per-frame random
streams for decision work and live ClientThink. Both `Rand_Init` and libc
`srand` are seeded. This prevents different amounts of phantom work from
consuming the next live step's random inputs. Zero preserves normal RNG.
The streams and any intervention are recorded in metadata; no such RNG
changes occur outside the explicitly active research fixture.

`--probe-time T` uses exhaustive planning only during a declared window
(default one 25 ms frame). Adding `--probe-seed S` instead changes only that
window's decision seed, leaving the native stream and later streams fixed.
It permits a controlled alternative to a random strafe tie, without
teleporting the player or forcing a landing to count as successful.

## Corrected native results

All trials use Kejim red-i2, sv_fps 40, fixed 8 ms floating movement,
g_speed 250, gravity 800, explicit XenJK force regen 200, decision seed 1,
and persistent world +X aim after the declared handoff. The success criterion
is actual enemy-flag pickup while alive within 20 seconds.

| Case | Exhaustive | Capped | Capped with one different initial tie |
|---|---|---|---|
| Aim handoff 1.500 s | Timeout 20.000 s | Pickup 18.275 s | Not needed for the existence check |
| Phase 3, aim handoff 2.500 s | Death 14.800 s | Death 14.800 s | Pickup 17.000 s |

Paired cases: `.codex-research/strafe-forensics-corrected-pairs-20260904`.
The source snapshot and `audit.json` verify native outcomes, measured planning
modes, operation counts, source/module hashes, and identical paired prefixes.
The matrix's acceptance still fails because ordinary capped phase-3 recovery
dies. There are no new capped failures relative to exhaustive in this two-case
recheck; it is far too small to estimate relative success rates.

Single-choice alternative:
`.codex-research/strafe-forensics-corrected-phase3-choice-20260904`.
It changes the decision seed to 12345 at 2.500 s for one frame, selecting
strafeDir -1 instead of +1. Both **complete pre-decision structs are byte-equal**
at that frame. The only listed Bot2 field that changes immediately is the
chosen strafe direction. Native movement then evolves normally; there is no
later seed override or exhaustive planning interval. Capped prediction stays
within 2,048 calls per dispatch; worst recorded decision is 8.794 ms.

The common root is position (725.312, 1680.486, 1888.462), velocity
(488.242, 520.124, 97.840), view yaw 169.997 degrees, airborne, pm_time 0,
40 force with Speed active. Commands first differ at 2.500 s. Both land at
3.100 s, but nearby initial landings are not equivalent future states.
`phase3-choice-comparison.json` records the exact fields, byte offsets,
commands and capture hashes. The alternate run finishes with health 100.

Earlier, before fixing command inheritance, a one-frame exhaustive decision
at 7.900 s also changed the 1.5-second handoff's outcome from death to pickup
at 15.275 s. That is preserved at
`.codex-research/strafe-forensics-aim1500-one-plan-20260904`, along with the
seeded full/bounded trials. It was a useful diagnostic, not accepted corrected
runtime evidence: the command-context bug was still present.

## Next discriminating experiment

First make the live and simulated input generation agree under combat aim.
Source inspection identifies another concrete mismatch to test: AIRBORNE
movement constructs its axes relative to the desired aim yaw, then applies
view smoothing; the phantom simulator constructs them relative to the
smoothed yaw. During a turn, these can produce different world movement
directions. This remains an identified implementation discrepancy, not a
measured fix or an explanation for all deaths.

Then test a small deterministic choice between the immediate left/right
continuations, using the actual airborne state and a usable next state as the
criterion. Compare against the existing tie and the known surviving witness.
The witness is a test case, not a direction to hardcode. Keep complete
native outcomes and sustained-contact limitations explicit. This work does
not commit to importing the Unreal atlas or to baking another route library.

## Reproduction

```powershell
python -B tools/strafe_recovery_matrix.py --build .codex-research/strafe-build-forensics-final-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/forensics-NEW --port 29880 --cases aim1500 phase3 --decision-seed 1 --capture-state
python -B tools/strafe_flag_runner.py --build .codex-research/strafe-build-forensics-final-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/choice-NEW --tape tools/fixtures/strafe_flag/kejim_red_i2_guide.csv --mode runtime --fps 40 --port 29885 --interrupt-ms 2500 --phase 3 --jump-plan-mode 1 --decision-seed 1 --capture-state --probe-time 2500 --probe-seed 12345
```

`tools/strafe_state_compare.py` compares captured command/state histories and
rejects truncated, duplicate-frame, or invalid-layout captures. No production
planner or movement policy was changed in this investigation. The experimental
planner remains default-off, pending broader corrected evaluation.
