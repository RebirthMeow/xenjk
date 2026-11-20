# Known-map maneuver library probe — 2026-09-06

## Question

This experiment tests a first persistent proposal library for the generated nominal two-gap map. A library lookup selects the nearest baked grounded root and proposes two candidate IDs. The existing native forecast then evaluates both proposals from the actual player state. The library does not replace native validation, prove a final 32-step connector, or establish affordability in a live world.

The protocol separates training, baking, and held-out evaluation. All case declarations are frozen before native outcomes. The library is baked from all nine retained and independently audited training cases, including failed cases; only successful policies may be retrieved. The library JSON and its hash are frozen before any held-out fixture starts. No held-out artifact can be read by the bake stage.

## Cases

Training uses nine fresh four-second, 40 Hz native worlds. Every root has `x=0`, `z=24.125`, `vx=0`; `y` is `-16`, `0`, or `16`, and `vy` is `-50`, `0`, or `50`. Each training world evaluates all eight candidates with the continuation scorer. These are on-platform entries with contact acquisition pending: the placement fixture preserves its exact `groundEntityNum` value (`ENTITYNUM_NONE`), and the first native pmove must acquire `ENTITYNUM_WORLD`. The library stores that exact entry field and validates the resulting native contact; it does not treat the entry as an arbitrary airborne state.

Held-out roots combine `x ∈ {-16,16}`, `y ∈ {-8,8}`, and `vy ∈ {-25,25}`, with `z=24.125` and `vx=0`. Each of the eight roots runs twice: a full-eight reference arm and a baked-two proposal arm. This produces sixteen held-out worlds. Training and held-out worlds use unique ports and output directories, with at most four active worlds.

## Stages and isolation

1. `freeze` snapshots the build manifest and all declared source bytes, binaries, runner, evaluators, baker, server helper, geometry JSON, BSP, exact 25-case declarations, and stage commands.
2. `train` runs only the nine training worlds and retains every result or launcher failure.
3. `bake` independently audits all nine raw training captures, then invokes the frozen baker. It writes the immutable library JSON and records its hash. It refuses to run if any held-out directory exists.
4. `evaluate` verifies the library hash and runs the sixteen held-out worlds. It cannot rewrite the library.
5. `audit` independently decodes all raw captures and checks the complete provenance and outcome contract.

The execution stages use fresh private worlds and ordinary native lifecycle. Timing benchmarks are excluded and cannot run concurrently under this protocol.

The bake record reports the nine native training-world wall durations, the total serialized/concurrent training-stage elapsed time, baker preparation duration, JSON storage bytes, and encoded native-payload bytes. These are precomputation and storage observations. They are kept separate from later runtime timing and do not establish production cost.

## Audit contract

Every native world must bind to the frozen executable, DLL, BSP, source manifest, settings, loaded module path, actual initial root and velocity, exact stored ground field, first-step `ENTITYNUM_WORLD` contact, and nominal geometry configuration. Captures must contain 160 ordered movement steps at 25 ms and match their saved JSON canonically. All four existing movement parity gates must pass and unsupported records must be zero.

Training and full-eight reference cases must independently reproduce the native continuation selection across eight candidates and at most 1,280 candidate pmove calls. The baked arm must verify the exact frozen library bytes and hash, nearest-root lookup, both proposed IDs, and full native forecasts from the actual held-out root. It may use at most 320 candidate pmove calls. A useful exit is an outcome field, not a prerequisite for retaining a case.

Held-out comparisons pair only arms with the exact same declared and observed root. They report useful exit, fixed-window idle (`<20` horizontal units/s while grounded), walking (`20` through `<300`), airborne time, horizontal travel, and actual useful-exit finish time. Failures stay visible. The audit does not require the two-candidate arm to choose the same trajectory as the full search; it requires native validation and reports outcome differences.

This probe covers nearest-root retrieval on one known static map. It does not qualify interpolation, approximate state reuse, dynamic obstacles, changing resources, map-general retrieval, runtime cost, or production integration.

The bad-shortlist control also has an independent raw-capture audit at
`.codex-research/strafe-library-bad-shortlist-control-20260906/audit-independent.json`.
Both exported candidates match the logged actual-root full-state hash and the
declared origin/velocity; independent scoring finds both unusable. The v6
airborne +100 impulse/reuse regression passes another 160 native steps in
`.codex-research/strafe-library-v6-regression-20260906`. Together with the nine
training and sixteen held-out worlds, the final build has 4,160 matched live
steps. Neither the refused controls nor their offline rollouts add live steps.

The retained v2 training capture passed its native movement gates, but its bake failed because the first library helper assumed the stored placement state was already grounded on `ENTITYNUM_WORLD`. That assumption contradicted the fixture's preserved `ENTITYNUM_NONE` entry state. The corrected contract records both the exact stored field and required first-native-step world contact; the v2 bake failure remains evidence rather than being reclassified.

## Measured evidence

The campaign history retains two setup failures before the completed measurement. The first campaign used `+` in generated case directory names; the engine command parser could not execute the resulting configuration path, so those launch failures remain recorded. The v2 campaign completed all nine training worlds but then failed the bake on the incorrect stored-`ENTITYNUM_WORLD` assumption described above. Neither failure was removed from the evidence chain.

The corrected v3 campaign is retained at `.codex-research/strafe-library-campaign-v3-20260906`, with its independent audit at `.codex-research/strafe-library-campaign-v3-audit-20260906.json`. All sixteen held-out worlds passed provenance, raw decoding, actual-root/contact, 160 native movement steps, four movement parity gates, native candidate validation, and library binding. This accounts for 2,560 audited held-out movement steps.

The nine training worlds took 25.639 seconds as a stage with up to four concurrent worlds. Baking took 0.974 seconds. The resulting library contains nine entries derived from 65 successful training outcomes; its library JSON is 153,833 bytes and its encoded native payload is 445 bytes. These values measure experiment preparation and storage. Concurrent training duration is not a per-world or production cost measurement.

Both arms reached a useful exit from all eight held-out roots. Five library/reference pairs had the same useful-exit finish time. The other three library proposals finished 25, 25, and 50 ms later. Across the fixed four-second windows, the library arm averaged 0.390625 percentage points more walking and 0.3125 percentage points less idle time than full search. Those small differences describe the measured trajectories; they do not establish a speed improvement.

Full search used 1,280 candidate pmove calls per held-out root. The two-proposal library used 320, a 75% reduction in candidate-search work. Both arms retained the same 160 selected diagnostic-forecast calls, so total guarded forecast work fell from 1,440 to 480 calls, a 66.7% reduction. These are deterministic native forecast-call counts. The outcome campaign does not measure runtime CPU cost, and every accepted proposal still receives a full four-second native lookahead.

Two retained negative-control suites exercise fail-closed binding. `.codex-research/strafe-library-binding-controls-v2-20260906/audit.json` passes both malformed-header cases with zero candidate pmove calls. `.codex-research/strafe-library-bad-shortlist-control-20260906/audit.json` passes after 320 candidate steps but before any live movement and does not expand the invalid shortlist into a full search. These controls show rejection at the declared boundaries; they do not qualify recovery policy after a rejected library proposal.

The stored library is a proposal index over existing feedback-policy candidates. It is not a full-map trajectory graph, a final short-range connector, or a faster movement controller. Dynamic obstacles, resources, approximate retrieval, other maps, live affordability, and production dispatch remain outside this result.
