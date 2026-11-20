# Strafe recovery: bounded prediction experiment

**Correction from subsequent forensics:** see
`STRAFE_MOVEMENT_RECOVERY_FORENSICS_2026-09-04.md`. The original comparison
used uncontrolled direction ties and fed guide commands into fallback where
normal Bot2 carries its previous command. The observed costs/failures below
are preserved historical measurements, but the claim of two cap-induced
live recovery regressions is withdrawn. Corrected, seeded rechecks and a
native surviving alternative are recorded in the follow-up. Default-off
status remains appropriate while corrected evaluation is incomplete.

This follows the live-dispatch experiment in
`STRAFE_MOVEMENT_LIVE_RUNTIME_2026-09-04.md`. The clean cached route already
meets the step-3 timing criterion. This experiment addresses the expensive
legacy movement path exposed when combat takes view ownership away from it.

## Change under test

`ai_bot2_plan.c` places a game-thread work scope around ordinary movement
dispatch, including all its early returns. The optional bounded mode permits
1,024 phantom Pmove calls per running-jump candidate, 2,048 per dispatch, and
4,096 across all dispatches at the same `level.time`. Escape scans share the
dispatch/frame limits. Offline diagnostic calls outside this scope retain
their existing search behavior.

Every phantom physics call, including the two landing-slide calls, checks
the budget before running. An interrupted trajectory returns failure without
publishing its player state or landing. Running-jump scoring tries complete
two-, three-, then four-jump horizons; a partial deeper pass is discarded.
The previous complete pass, or the existing validated first-jump fallback,
remains available. Predicted states are not cached across live frames.

Bots denied their first physics step by the shared budget enter a FIFO queue.
They keep ordinary movement and retry from current state on the next eligible
frame instead of incurring the ordinary failed-jump cooldown. Death, map
restart, a dispatch that no longer requests prediction, or 100 ms without a
renewed request removes stale waiters. Shared Bot2 state layout is unchanged.

These are **operation limits, not a hard wall-clock deadline**. Native physics
and collision costs depend on the scene. Navigation, floor probes, combat,
and the rest of the server also consume time outside the capped physics work.
The cap does not establish that 32 actual bots fit in a server frame.

## Measurement and acceptance

The experiment uses the same isolated XenJK Kejim red-i2 fixture: 40 Hz,
fixed 8 ms floating movement, speed 250, gravity 800, explicit force regen
200. Success requires native enemy-flag pickup while alive within 20 seconds.
The fixture grants setup state once, then uses ordinary movement dispatch and
native gameplay. It does not run a complete multiplayer Bot2 think loop.

At a declared time, combat permanently owns view direction toward world +X.
The cached route must release once. Both bounded and exhaustive arms receive
the same requested timing phase and optional declared velocity impulse.
Seven paired cases cover handoffs at 1.5, 2.5, and 3.5 seconds, phase offsets
3 and 6 at the 2.5-second handoff, and +50 X / -50 Y velocity disturbances.
Each native process runs separately to avoid deliberate benchmark contention.
Legacy movement contains random direction tie-breaking, so matching fixture
conditions do not imply a paired random seed or bitwise reproducibility.

`tools/strafe_recovery_matrix.py` preserves all cases, reports deaths/timeouts,
and freezes source bytes before the batch. A successful nominal example is
insufficient: any new failure relative to exhaustive recovery blocks enabling
bounded mode by default.

## Early trials retained

- `.codex-research/strafe-recovery-bounded-20260904`: half-sized budgets
  (512 / 1,024 / 2,048). Worst movement decision 3.701 ms, but the bot died
  at 19.725 s. Rejected.
- `.codex-research/strafe-recovery-exhaustive-20260904`: instrumented full
  search reached 25,723 phantom physics calls in one dispatch. Native pickup
  at 15.150 s; worst movement decision 66.487 ms. This supports attributing
  the earlier approximately 65 ms spike to the prediction search.
- `.codex-research/strafe-recovery-bounded2-20260904`: doubled budgets restored
  pickup in the initial 2.5-second handoff trial at 17.275 s, worst decision
  9.020 ms. Broader validation is required before accepting this tradeoff.

The first two trial reports also contain a `planning.max_us` field that was
incorrectly labeled: OpenJK's `PrecisionTimerEnd` returns raw CPU timestamp
counter ticks on Windows, not microseconds. That field is invalid as a time
measurement and was removed. All timing numbers above and in the comparison
use the independent `decision_us` measurements from QueryPerformanceCounter
in the existing native fixture. Failed trial artifacts remain unchanged.

## Interpretation

The paired matrix is preserved at
`.codex-research/strafe-recovery-matrix-20260904`, including frozen source,
native module hashes, complete CSV traces, and individual server logs.

| Handoff case | Exhaustive result | Bounded result | Worst decision, exhaustive / bounded |
|---|---|---|---|
| 1.5 s | Pickup 16.525 s | Death 16.275 s | 47.772 / 5.646 ms |
| 2.5 s | Pickup 15.250 s | Pickup 17.125 s | 85.413 / 9.140 ms |
| 3.5 s | Pickup 14.750 s | Pickup 14.750 s | 55.381 / 7.246 ms |
| Phase 3, 2.5 s | Pickup 13.800 s | Death 13.675 s | 40.582 / 6.888 ms |
| Phase 6, 2.5 s | Pickup 17.950 s | Pickup 17.875 s | 63.909 / 5.826 ms |
| +50 X impulse, 2.5 s | Timeout 20.000 s | Pickup 13.525 s | 49.484 / 6.428 ms |
| -50 Y impulse, 2.5 s | Death 16.375 s | Pickup 13.950 s | 52.702 / 5.227 ms |

Both methods finish five of seven cases; those are different five cases.
Bounded mode introduces two deaths and recovers two exhaustive failures.
All seven bounded cases obey the measured operation limits, and all fourteen
arms release the route exactly once for aim ownership. The matrix acceptance
test correctly **fails**. This is a small unseeded comparison, not a statistical
estimate of relative success rates or a full-match performance claim.

`bot_jumpPlanMode` therefore defaults to **0**. Value 1 explicitly enables
the experimental cap; the research runner sets this explicitly for bounded
arms. No installed game DLL or live server configuration was changed. The
matrix source snapshot contains the prototype's earlier default of 1; both
arms explicitly set the cvar, so their behavior does not depend on that
default. Final builds restore default 0.

Bounding the search reduces its cost substantially, but a shorter search
changes which route continuation wins. A landing that passes the legacy
phantom simulator is not proof that the altered native trajectory will stay
alive or reach the objective. The simulator already has approximation limits;
the budget does not resolve them.

The next useful experiment is to preserve route guidance during recovery:
use an offline continuation/rejoin hint for strategic direction and spend a
small live budget validating the immediate maneuver against the current
state. Compare it with both existing exhaustive recovery and the rejected
truncated-search arm. Keeping the exact same failed cases is essential.
Increasing the cap until a nominal example works is not sufficient evidence.

## Reproduction

```powershell
python -B tools/strafe_plan_budget_check.py --out .codex-research/plancheck-NEW
python -B tools/strafe_recovery_matrix.py --build .codex-research/strafe-build-recovery-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/recovery-NEW --port 29810
```

The standalone C check compiles the actual budget source with only its engine
headers/API stubbed. It checks candidate/dispatch/frame limits, 32-client FIFO
service, map reset, inactive waiters, and unrestricted diagnostic mode. It
does not simulate 32 live players or validate landing safety.

## Final verification

- Clean native engine/game build:
  `.codex-research/strafe-build-recovery-final-20260904`.
- 36 existing Python tests pass. The final production scheduler source also
  passes the standalone C check at
  `.codex-research/strafe-recovery-scheduler-final-20260904`.
- All 12 clean-route cases still pick up with exactly the frozen commands,
  positions, velocities, and clocks across 3,248 checkpoints. No case releases
  the route. Nominal remains 6.700 s against the 6.600 s native planned sequence.
  Artifact: `.codex-research/strafe-recovery-clean-route-matrix-20260904`.
- The native simulator regression passes 122 movement checkpoints at 30 Hz,
  fixed 8 ms floating movement:
  `.codex-research/strafe-recovery-native-regression-20260904`. Its existing
  full-state forecast/feedback exclusions (`fd.forcePowerRegenDebounceTime`
  and `entityEventSequence`) remain; no new full-state parity claim is made.
- `tools/strafe_recovery_report.py` independently verifies raw outcomes,
  timing maxima, observed budget mode, operation counts, route release,
  identical pre-handoff paired prefixes, and recorded artifact hashes.
  `strafe-recovery-matrix-20260904/audit-final.json` passes data integrity while
  retaining the failed experiment acceptance. The first audit caught two
  omitted source snapshots; their bytes were recovered only after matching
  every recorded per-arm hash and preserved under `supplemental_sources`.
  The original failed audit remains available. Future matrices freeze those
  files at creation.

No test server remains running. No installed assets were modified, and no
research results or unrelated working changes were removed.

## UnrealJKA precedent checked after the owner's reminder

The suggested cached-continuation experiment overlaps an existing UnrealJKA
approach. `<UnrealJKA>/JKA/Source/JKACore/AI/JKABotMove.cpp` already has an
atlas of goal-specific depth-four values, distinct from its transition cache.
Its key contains position, horizontal speed/heading, effective run speed, and
goal; it does not encode the full view, vertical velocity, force pool, or
movement timers. `<UnrealJKA>/JKA/Docs/STRAFE_ATLAS.md` describes checking the
proposed immediate jump from live state and substituting the cached future
value only on a matching successor. This is precedent, not a new architecture
invented by this XenJK experiment. Atlas-specific effectiveness was not
established by the limited source/report inspection below.

The historical reports distinguish three outcomes:

- `LINES_SCORECARD_ANALYSIS.md`: precomputed outbound routes reportedly
  reduced spawn-to-first-grab from about 23.2 to 12.3–12.6 seconds and total
  bot compute by 13% at 4v4. This supports known-route execution, not arbitrary
  atlas coverage or disturbed-state recovery.
- `LINES_FIRST_PHASE3_CONTINUATION_ANALYSIS.md`: continuing the line after
  disruption was rejected: disturbed completion fell 9.1 percentage points
  at 4v4 and deaths increased. Sustained fire and unsafe merging matter.
  Read its appended correction: earlier claims that most deaths came from
  ordinary post-landing navigation were confounded by pit-fade timing and
  false STABLE reports. Generic stabilize-before-classify advice was withdrawn.
- `RECOVERY_FIXES_ANALYSIS.md`: retry/survival fallback mechanisms ran, but
  improved match outcomes were not demonstrated; they remained default-off.
  Later `FRUGAL_TAPES_ANALYSIS.md` records successful Kejim outbound/return
  route work, including a reported +2.19 captures per boot for return routes.
  The old scorecard's untaped-return bottleneck is therefore historical.

These are findings reported by the earlier campaigns, not raw-log analyses
or Unreal benchmark reruns performed in this XenJK task. Earlier documents'
approval language and owner rulings are historical records, not new task
instructions.

Before another bake, use the two failed native cases to capture the complete
state at the first consequential divergence and search offline for an actual
surviving continuation. Replay that continuation natively through flag pickup
or a demonstrably usable recovery endpoint. Separate a missing usable option,
a wrong ranking, a simulator mismatch, and a lethal handoff. Only then test a
small cached hint against the existing baselines. Include view/force/timers
and the swept path of any rejoin; proximity to a certified line is insufficient.
