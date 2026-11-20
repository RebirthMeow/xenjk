# Actual-state maneuver reselection probe

Status: implemented and measured static subset, 2026-09-06. This is an offline
static-world experiment under the [planner contract](STRAFE_MOVEMENT_PLANNER_CONTRACT_2026-09-06.md).
It does not qualify an online planner or dynamic collision simulation.

## Question and fixed cases

Can the same maneuver proposals be evaluated from the actual moving player
checkpoint, without restoring a nominal trajectory, and then generate commands
that agree with live native movement?

Use the unchanged nominal geometry from
`.codex-research/strafe-maneuver-matrix-20260906/geometry-builds/nominal`.
All cases start once at (0,0,24.125) with zero velocity. Four seconds of native
movement run at 40 Hz, ordinary movement, gravity 800 and no active powers.
The initial build is `.codex-research/strafe-build-maneuver-replan-20260906`.
The maneuver kernel and its eight proposals are unchanged from the geometry
matrix; only the opt-in native fixture and its capture/audit path are extended.

| Case | Reselection boundary | Declared change |
|---|---|---|
| Baseline | None | None |
| Negative | None | Forecast-only yaw bias; must be rejected |
| Ground | 250 ms, grounded approach | None |
| Air | 600 ms, airborne after first takeoff | None |
| Air-plus | 600 ms, airborne after first takeoff | Add 100 to actual Y velocity |
| Air-minus | 600 ms, airborne after first takeoff | Subtract 100 from actual Y velocity |

Each positive case starts with the same continuation scorer and eight-candidate
search. The four replan cases perform exactly one additional selection. The
observed airborne policy may still be in launch phase 1 before the command
changes it to air-steering phase 2; the fixture preserves that state.

## Native boundary and evidence

At the declared server boundary, compare the actual state with independently
carried recorded-command and feedback simulations. Reject any prior movement
or policy mismatch. Apply the declared impulse once to the actual velocity
and to each independently carried simulation velocity. Never replace their
states with a nominal or newly copied player state. Save full before/after
player states and the unchanged policy in a transition record.

Generate and submit the current command using the incumbent candidate. At its
native Pmove boundary, copy the complete actual root and evaluate the same
eight candidates. Every candidate must retain that already-submitted command
and its policy transition for this tick; candidate differences begin on the
next command. This explicit execution delay avoids changing a command after
the game has received it. The selected future is compared against subsequent
live commands, policy, movement, contacts and clocks.

Capture version 5 records two forecast epochs. The initial four-second forecast
remains in the file. Its unexecuted future is marked superseded only when the
new forecast is written before those live frames. The decoder rejects attempts
to replace already executed predictions. Candidate epoch-1 files have unique
names and are never written over epoch-0 evidence. Disabled replanning retains
the version-4 fixture behavior.

The horizon remains the original episode's four-second endpoint. Therefore
the second search uses 151 steps per candidate at 250 ms or 137 at 600 ms,
for total candidate calls of 2,488 or 2,376 respectively. The hard ceiling is
2,560. These are instrumented offline calls, not an online budget. Selected
forecast validation, clone parity and capture overhead are additional work.

## Gates and interpretation

Freeze all six cases and hashes before native execution. Preserve every outcome.
Require 160 matched native steps in all four movement parity contracts for each
positive case, and detection of the biased negative. Independently decode the
binary captures and reconstruct candidate scoring/selection for each epoch.
Verify complete candidate-root identity, unchanged current command, preserved
policy, exact impulse/contact declaration, native world/clock restoration,
staged binary/map hashes, loaded module path and call ceilings.

Useful onward support is a separate outcome. A valid exhausted search is not
successful travel. The current test replans before the first landing; it does
not establish arbitrary-phase connectors, recovery from a wrong support,
position displacement, automatic geometry extraction, retained-plan reuse,
moving obstacles, resource-aware movement or production performance.

The original pre-outcome freeze is retained even if a declaration/auditor issue
requires a new freeze before collection. Do not rewrite failed evidence or
weaken a gate after observing native outcomes.

## First collection: animation-context mismatch retained

The six-case collection at
`.codex-research/strafe-maneuver-replan-matrix-v2-20260906/` and its
`strafe-maneuver-replan-matrix-audit-v2-20260906.json` audit fail all four replan
cases. The unchanged baseline and deliberate negative behave as expected.
Reselection root, current command and policy checks pass, but forecast
`legsFlip` diverges at ground frame 48 and air frames 67/73/65. Position,
velocity, commands, policy and contacts still match at the first divergence.
No animation field is excluded from the movement contract.

Source diagnosis: the GAME animation restart helpers in `bg_panimate.c` read
`g_entities[client].s.legsAnim/torsoAnim` directly, bypassing the forecast's
`pmove.baseEnt`. Native `ClientThink_real` refreshes those entity animation
fields from post-Pmove player state between commands. The forecast previously
left them fixed at the real replan root, so a later animation change could
incorrectly trigger the same-animation restart bit. A complete player-state
copy alone did not cover this entity-state dependency.

The new fixture adapter exposes the captured forecast entity's two animation
IDs during each forecast Pmove, verifies they were only read, and immediately
restores the real entity's IDs. It advances the clone's animation/flip quartet
from the forecast player state after the step, matching native conversion
cadence. The initial entity prefix comes from the actual root. World hashing
and explicit per-rollout animation-restoration checks remain active. This is
a restricted simulator correction, with no production movement or shared
animation-code change.

Correction build:
`.codex-research/strafe-build-maneuver-replan-animation-20260906`.
The same six cases must be frozen again and collected; the first failure set
remains evidence of why mid-trajectory roots need more than player state.

## Corrected collection

Frozen cases/captures:
`.codex-research/strafe-maneuver-replan-matrix-v3-20260906/`.
Independent audit:
`.codex-research/strafe-maneuver-replan-matrix-audit-v3-20260906.json`.
All five positive cases pass all four movement parity contracts (800 native
steps). All four reselections preserve actual roots/current commands, validate
their declared transitions, and reach useful onward support. The forecast-bias
negative is rejected. Every rollout passes explicit animation-context
read-only/restoration checks; no additional player-state field is excluded.

| Case | Useful onward passage | Candidate calls |
|---|---:|---:|
| Baseline | 2.825 s | 1,280 |
| Ground replan | 2.825 s | 2,488 |
| Air replan | 2.800 s | 2,376 |
| Air +100 Y impulse | 2.975 s | 2,376 |
| Air -100 Y impulse | 2.775 s | 2,376 |

This does not isolate the benefit of reselection over simply retaining the
incumbent feedback policy after an impulse; that paired comparison remains
necessary. It establishes actual-root candidate evaluation and exact native
continuation for these four declared boundaries.

The original 25-case geometry matrix was repeated on the corrected build:
`.codex-research/strafe-maneuver-animation-regression-20260906/` and
`strafe-maneuver-animation-regression-audit-20260906.json`. Its 24 positives
retain all 3,840 matched steps and the extra biased forecast is rejected.
`strafe-maneuver-animation-regression-comparison-20260906.json` verifies
identical candidate outcomes, selected IDs, actual outcomes and decoded
command/movement trajectories against the earlier matrix. This repeat adds
regression confidence, not additional held-out cases.

Auditor corruption tests in
`.codex-research/strafe-replan-audit-mutations-20260906.json` reject four
in-memory corruptions: uniformly wrong current commands, wrong epoch labels,
a hidden position reset and a missing replacement epoch. Recorded native
files remain unchanged. These are audit tests, not additional native trials.

## Next unresolved gate

Retain a validated continuation when its actual checkpoint still matches,
and invalidate it on a declared disturbance using the same root contract.
Compare retained-policy, forced-reselection and reuse-with-invalidation arms
with the same decision schedule and geometry. Bind the complete movement
contract, policy, preceding entity animation state, command cadence, static
collision geometry/settings and remaining horizon. A whole-world hash that
includes the moving bot cannot serve as an unchanged static-world key; define
the static fingerprint separately and validate the bot's state explicitly.

Report searches/candidate calls avoided and outcomes before claiming cost.
Measure lookup/validation separately with capture disabled, then require the
existing representative eight-bot budget before any live-planner claim.
Exact same-episode reuse does not establish reusable entry regions or
arbitrary-state connectors. Higher-speed maneuver proposals and automatic
game-map extraction remain separate necessary steps toward the general goal.
