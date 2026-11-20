# Parity mixed-refusal guard and trigger investigation handoff

The review found that `ParityBuild` accepted a trigger-stopped prefix when
`animationChanged` was also true. That field independently makes
`Bot2_ForecastRun` refuse a rollout; `animationRestored` does not cancel it.
The trigger exception therefore accepted more than its declared contract.

## Fix and focused verification

`ParityBuild` now refuses `animationChanged` explicitly and reports
`animation_changed` in refusal telemetry. Pure trigger-only prefixes remain
accepted; no candidate admission rule, physics or movement policy changed.

`tools/test_movement_parity_guard.py` compiles the actual `ParityBuild` and
`ParityDisarm` functions against deterministic forecast results using the
production forecast enums. All **41 cases pass**: pure trigger and complete
forecasts, mixed refusal flags, world and animation changes, restoration
failures, invalid counts/statuses, disarming and separate prediction storage.
The retained pre-fix source fails the trigger-plus-animation-change case.
This is a tested regression, not a duplicate implementation of the predicate.

Evidence: `.codex-research/movement-parity-guard-test-20260921T043636Z-275b278b/`.
`result.json` binds the source, forecast header, original source and compiler;
both passing and failing generated programs and logs are retained.

## Native verification

Fresh build and declared outcome validation live under
`.codex-research/parity-guard-review-c791388e/`. The build completed with frozen
source hashes, which remained unchanged through collection. Results:

| Group | Passed | Compared frames | Mismatches | Refusals |
| --- | ---: | ---: | ---: | ---: |
| Unpinned floor | 36/36 | 18,212 | 0 | 0 |
| Seeded named regressions | 2/2 | 1,686 | 0 | 0 |

Zero deaths and movement owner only in both groups. Reported comparisons total
**19,898**; periodic tally counts remain lower bounds. `verification-summary.json`
binds the source manifest and result hashes; `driver-result.json` retains exact
commands and exit codes. The floor used four outcome worlds, seeded cases ran
sequentially, on ports below the dynamic UDP range. No setup cases were missing.
These runs make no speed, CPU or production-installation claim.

Earlier 19,413-frame floor and 8,510-frame item-experiment results remain valid
historical observations bound to `parity-fix-build-20260920-a`; they must not be
silently attributed to the new guarded build.

## Corrected interpretation and next goal

The offer census counts first stopping reasons, not recoverable fast plans.
The origin-outside-trigger-box count is not a false-positive measurement.
The forecast uses a swept axis-aligned bounding-box overlap check, whereas
native non-item contact uses the actual hull and brush contact; item pickup
has separate semantics. Quantify disagreement before proposing replacement.

The [next-goal handoff](2026-09-21-trigger-shadow-handoff.md) specifies a
default-off shadow diagnostic, unchanged admission, native-contact fixtures,
bound evidence, and an explicit go/no-go decision. Preference tuning remains
parked; the current research configuration and installed game are unchanged.
