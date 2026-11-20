# Adverse entry sampling and native runtime experiment — September 6, 2026

This implements the first two packages of the owner's approved execution
sequence in the [active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md). The adjacent
[real-map preparation experiment](STRAFE_MOVEMENT_MAP_PREPARATION_2026-09-06.md)
adds native support probes independently. These are M3a-B/C research gates.
They do not qualify the installed runner, continuous recovery, or M3 cost.

## Frozen hypothesis and case selection

The previous clearance-ranked midpoint bake resolved the sampled clearance
failure but refused eight previously declared interior states. All sixteen
rejected candidate prefixes matched the full reference, whose suffixes were
useful. Terminal Y velocity -31 lay outside the measured [-33,-32] envelope.
This experiment changes how the envelope is measured; it adds no hand-picked
field padding or map-specific exception.

`tools/strafe_adverse_regions.py` declares all roots before native collection:

- 27 original tensor corners: X [-24,0,24], Y [-16,0,16], VY [-50,0,50].
- 64 first-wave strictly interior integer roots, eight per original cell,
  deterministic Halton bases 2,3,5 starting at index 1.
- 64 reserved second-wave roots, eight per original cell, starting at index 97.
- 32 unseen roots, four per cell, starting at index 193.
- All 48 previously evaluated roots, as a separate regression group.

The sets are disjoint; the generator skips collisions in fixed order. The
complete sampling declaration SHA-256 is
`5300baeeda910c9854b258964f4a0e990e08557bad9f9ad493c0e1c87b0cfdcc`.
Standing Z is 24.125, VX and VZ zero; VY is the declared integer. Native
physics is 40 Hz, 160 live frames, continuation scorer, the same nominal
generated two-platform BSP and eight feedback candidates. This contract is
static, force-disabled and WORLD-supported.

Each training root executes all eight full guarded forecasts, followed by the
selected 160-frame live continuation with existing parity checks. Retain all
failed candidate suffixes. Bake the corners, then compare first-wave measured
outcomes against each original cell. Schedule its entire reserved second wave
if any declared rule fires: a proposed candidate's terminal state leaves the
corner envelope; preferred clearance falls below the corner minimum; a
unanimous corner usefulness label changes; or no corner proposal is eligible.
Freeze the machine-derived reasons and exact scheduled subset before wave two.
This is **adaptive sampling within the original eight cells**, not spatial
subdivision and not a continuous reachable-set certificate.

Bake the final candidates from every collected root in that cell, using the
existing ranking: maximize minimum second-platform hull clearance, then
minimize worst and mean completion time, then candidate ID. Eligibility still
requires every measured suffix to be useful with nonnegative clearance and a
supported 12-frame prefix. Envelopes come only from measured terminal states.
Freeze all JSON, native payload, build, helper and source hashes before the
80 paired evaluation roots are launched. No evaluation labels feed this bake.

## Gates and retained measurements

Require 80/80 full references and 80/80 native coverage choices to produce
useful, clearance-safe continuations, with the unchanged actual-state gates,
no more than two 12-frame proposal predictions, and all four movement parity
checks. Report unseen and regression groups separately. Retain refusal and
failure captures, candidate identities, finish time, walking/idle fractions
and longest spans. Four-second fixtures cannot pass the five-second
confinement gate.

Retain the seven existing native negative controls: X outside either edge,
Y or VY outside, incorrect map/candidate binding, and incompatible terminal
envelopes. Controls must refuse before live movement; only the envelope
control may predict two prefixes. Five guarded selection timing episodes run
alone after outcomes and compilation finish.

## Separately invoked runtime predictor

`bot_movebench_runtimeTiming=1` invokes the bounded native predictor again
from the same actual root and compares it to the guarded oracle. Its measured
span includes scratch setup, policy, native Pmove, narrow restoration and
retained per-step trace rows. Full-world audit hashes, full command/policy/PS
and contact comparisons, logging and serialization are outside that span.
The measurement is a new invocation, never a subtraction of profiler medians.

Run five coverage episodes (one or two 12-frame predictions plus the retained
selected 160-frame diagnostic) and one full-search episode (eight 160-frame
predictions plus selected diagnostic), serialized with no other native world
or build active. Require exact oracle equality, restored world/global state,
the declared inventory and positive finite elapsed costs. Preserve live
parity and useful-outcome checks. Report candidate prediction and selected
diagnostic costs separately; neither equals complete runtime selection,
tracking, collision adaptation or repair cost.

A seventh isolated episode sets `bot_movebench_runtimeTestMismatch=1`, adding
1 unit/s only to the clone's X velocity. It must detect a PS mismatch and stop
before live movement, while restoring world/global state. This negative
control does not alter the actual player. Retain its expected failed capture.

## Execution and provenance

`tools/strafe_adverse_campaign.py` actions are `freeze`, `train`, `evaluate`,
`control`, `timing`, `runtime`, `audit`, and `audit-runtime`. Use a fresh build
from `tools/strafe_fixture_build.py`, unique output directory, and base port
33400. Outcome runs use at most four isolated worlds; all timing is serialized.
The frozen campaign records every reserved case and all final payload hashes.
Training/reference/coverage are independently re-audited from raw captures.

The first compilation exposed undefined MIN/MAX macros in the map probe's
swept bounds. That failed build is retained; explicit portable comparisons
replace them in the fresh v2 build. No result from the failed build counts.

If empirical region failures remain, retain the counterexamples and revise
the representation/refinement policy explicitly. A pass authorizes the next
research step—compatible real-map transitions and bounded tracking/repair—
without establishing Kejim completion or eight-active-bot affordability.

## Result — gate remains open

The frozen declaration is retained with
`.codex-research/strafe-adverse-regions-20260906/`. All eight cells trigger
their reserved second wave, producing 155 training roots. Independent raw
reconstruction reproduces both refinement decisions and the final bake.
All 48 previous evaluation roots now pass, but four of 32 unseen roots refuse
before movement: X=-20 or 4, Y=-7 or 9, VY=-31. All 80 full references pass;
seven rejection controls and five guarded timing episodes pass. The payload
is 2,002 bytes. This is a failed 80/80 coverage gate, with no unsafe executed
suffix in this evaluation.

All eight attempted prefixes in the four refusals exactly match their paired
full-reference prefixes after clock normalization. Their only envelope miss
is terminal VY=-34 against measured [-33,-31], and every corresponding full
suffix is useful. Preserve these as counterexamples; no envelope padding was
applied. Four-second idle includes stopping after arrival, so it cannot show
an active-travel improvement. Among 76 executed pairs, mean pre-finish walking
increases about 0.99 percentage points and active idle is essentially unchanged.

The independent audit is
`.codex-research/strafe-adverse-independent-audit-20260906.json`; detailed
refusal and travel analysis is
`.codex-research/strafe-adverse-failures-20260906.json`. The separate auditor
normalizes integer candidate keys through JSON before semantic comparison;
the frozen campaign helper's integer/string mismatch is retained. It compares
substantive bake contents and raw artifact hashes without treating rebuilt
audit paths as a changed policy.

The first native runtime-core negative control detected the injected mismatch
but executed one live step before stopping. That failure is retained. The
subsequent fix, fresh build, exact-root calibration, positive regressions and
two zero-live-step controls are documented in
[native core qualification](STRAFE_MOVEMENT_NATIVE_CORE_2026-09-06.md).

The next entry experiment must systematically discover discrete native
execution outcomes and validate their spatial continuation slack. Existing
80-root evaluation data become regressions; freeze a new unseen set before
collecting replacement qualification. Prefix similarity alone cannot prove
that full continuations from different positions are interchangeable.
