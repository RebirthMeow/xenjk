# Return acquisition and tap timing

Owner execution, 2026-09-13, Astra without subagents. This continues the
[live handoff](STRAFE_MOVEMENT_LIVE_HANDOFF_2026-09-13.md). No installation.

## Corrected cause

The retained `walking-partial-basegoal/on-r1` does take the first short hop:
supported movement takes it before fast return owns the runner, without native
Speed. Fast return then joins at index 5 instead of 2. The next gap reaches only
492.3 u/s, is refused, and uses branch 26. The control takes its short hop under
fast return with Speed and reaches the next launch at 689.2 u/s. This corrects
the earlier shorthand that the hop itself was missing.

Fresh acquisition diagnostics establish that lane preparation is already ready.
The direct hull chord to a grounded sample 17 units away contacts world floor
at fraction zero, with neither startsolid nor allsolid and normal `(0,0,1)`.
The old `join_body` release imposes 500 ms backoff. Fallback can take the short
hop before the retry. Acquisition depends on native pose/command phase, not
only pickup Force and speed.

## Experimental changes

- `bot_returnStepJoin`: after this walkable world-floor contact, check vertical
  18-unit headroom and the raised hull corridor, then all four original floor
  samples. Bodies, walls, solid starts, obstructed headroom and missing floor
  still refuse. Grounding must be on the world. No position mutation.
- `bot_returnTapAtSample`: the shared kernel's optional `tapAtSample` policy
  preserves ground run-up for a future authored ordinary tap until its sampled
  takeoff is within one command interval along the guide. Other Force-jump hints
  and legacy callers retain their behavior. Fast return supplies this policy;
  the kernel has no cvar or map dependency.
- Existing `bot_supportedPartialSpans` preserves validated per-sample prefix
  decisions. No additional prefix expansion or resource purchase was introduced.

All experimental switches default off. Acquisition/contact diagnostics are
behind the existing momentum trace. Launch resource/momentum gates are unchanged.
The runner spaces its non-idempotent `addrunner` command after status and gathers
its reply; it never retries that mutation after a missing acknowledgement.

## Native results

Evidence root: `.codex-research/return-acquire-cfe0dcbe32de4e6c9065b28180f5da5a`.
All successful directories retain declarations, exact commands, settings,
source/binary provenance, native logs and outcomes. Comparisons use time from
the declared corner intervention, not delayed CSV collection start. These are
native reconstructions, not exact private-controller replays.

| Experiment | Control full trip | Candidate full trip | Result |
| --- | --- | --- | --- |
| Step join, walking off | 36.800 s | 40.200 s | Regresses |
| Step join, partial spans on | 38.525 s | 34.425 s | Improves |
| Tap timing, step join on, walking off | 36.625 s | 36.475 s | Improves 0.150 s |
| Tap timing, step join on, partial spans on | 34.275 s | 35.125 s | Regresses 0.850 s |
| Combined, base goal, repetition 1 | 37.025 s | 34.150 s | Improves 2.875 s |
| Combined, base goal, repetition 2 | 37.025 s | 35.125 s | Improves 1.900 s |
| Combined, dropped flag, both repetitions | 45.000 s | 43.100 s | Improves 1.900 s |

The first step-join campaign has two retained setup failures (missing seed
acknowledgement) and two completed worlds; `step-join-repeat` contains the fresh
four-way comparison. Do not treat the failed setups as movement completions.
Earlier `diagnostic` worlds retain the geometry/backoff reproduction.

Earlier acquisition alone can fire the short hop too early: the walking-off
step-join case refuses the next gap at 577.0 u/s. The timing experiment addresses
that run-up boundary, but its mixed ablation results prevent claiming it is
uniformly faster. The combined base-goal and dropped-flag pairs all capture alive
with health floor 90. First-edge traversal falls from about 9.2 s to 6.5–6.6 s.
Sampled idle/walking measurements retain 25–250 ms of explicit uncovered time.
The dropped-flag candidate still reaches pickup 0.950 s later despite its faster
first edge; its better return produces the net gain. Preserve this limitation.

## Checks and evidence boundaries

Fresh build:
`.codex-research/strafe-build-tap-timing-cfe0dcbe32de4e6c9065b28180f5da5a`.
Earlier diagnostic and step-join builds share the suffix with prefixes
`strafe-build-acquire-diag-` and `strafe-build-step-join-`.

Compiled fast-return checks cover admitted floor contact and refusal for bodies,
walls, solid starts, steep floor, blocked headroom/corridor and absent/invalid
support, including unchanged player state. Twenty rotated tap-timing cases and
existing terminal-semantics checks pass. The owner admission/lifecycle suite and
12 runner seed/pose tests pass. These mocked checks do not establish native
collision or broad movement qualification.

`verification.json` confirms 1105 native sources, both build binaries and both
staged binaries in sixteen completed worlds (timing, combined base goal,
combined dropped flag, normal defended). Integration has its own final binding
check after collection. The new combination has not completed full movement
qualification.

## Normal-speed defended discovery

`normal-defended` has four completed 180 s worlds, red/blue, two native enemies,
combined policy off/on, no pose/resource/body-proxy setup. Captures are 2/3 in
controls and 4/3 in candidates. Idle changes from 5.0/4.0% to 2.4/4.8%; walking
from 30.1/36.7% to 33.2/27.1%. Thus red walking and blue idle still regress.
Control red has one audited confinement episode and one falling death; neither
candidate has either. Native runner deaths are 5/3 in controls and 1/3 in
candidates. Divergent combat trajectories prevent a causal paired estimate.
Use `audit.json` and `outcomes.json`, retaining complete per-world histories.

The broader integration runs use the unchanged twenty return cases three times
with all three candidate switches enabled. Body checks cover legacy shared-kernel
compatibility. The resource matrix enables partial spans in its existing
supported/flight runtime; it does not run fast return. Exact scope and historical
control limitations are declared in `integration/declaration.json`.

## First combination: integration and rejected timing scope

`integration` completes 60/60 solo captures alive, health floor 90, 26/26 body
checks and 30/30 resource completions (zero resource-matrix deaths). All 1105
source hashes, settings and staged binaries in 116 worlds verify in
`integration/verification.json`. The body/resource results retain their limited
runtime scopes above; neither awards broad qualification.

Compared with retained guard-only controls, first-return mean delta is +0.0792 s,
32/60 pairs are slower, 25 idle fractions and 39 walking fractions worsen. T5
remains 0/8 in all three repetitions. `totals.json`, `movement-diagnostics.json`
and the per-repetition window/fall/loss/seam/ramp/bevel audits retain the results.
This combination does not pass the performance gate.

`depleted-matched` checks two counterexamples on the same build. R6b-blue (`r1`)
is equal at 20.350 s corner-to-capture, so its historical +1.125 s regression is
not reproduced in that matched run. R2-blue (`r2`) worsens 17.675→18.600 s.
The candidate delays branch 27's first tap from index 11 to 12, changes the later
tap sequence and releases at index 33 with `ground_exit_floor`. The control
finishes the validated tap chain. All four matched worlds capture alive.

The first timing policy was too broad: a validated tap chain already has a
prepared ground/air corridor and uses opportunistic taps to retain momentum.
The follow-up restricts `tapAtSample` to launches without that existing chain
contract. This uses prepared capability, not a route ID, map coordinate, team or
Force threshold. A compiled owner check pins both sides. Its fresh build is
`strafe-build-tap-chain-cfe0dcbe32de4e6c9065b28180f5da5a`; native follow-up is
separate from the completed first-combination integration evidence.

## Latest revision: validated-chain timing preserved

`depleted-chain`, `basegoal-chain` and `dropped-chain` are twelve completed
same-build native worlds on the tap-chain build, all capturing alive with health
floor 90. `verification-chain.json` verifies all 1105 current sources, the two
build binaries and both staged binaries in all twelve worlds.

- R2-blue is equal at 17.825 s, removing the previous 0.925 s regression.
- R6b-blue is 20.350→20.425 s, a remaining 0.075 s regression in this pair.
- Base-goal trips improve 37.050→34.000 s in both repetitions. The first edge
  improves 9.175/9.200→6.700/6.725 s. Sampled walking falls substantially, but
  sampled idle rises from 1.500 s to 2.250/2.000 s; no all-metric pass is claimed.
- Dropped-flag repetition 1 improves 45.000→43.100 s. Repetition 2 regresses
  40.700→42.275 s. Its pickup regresses 31.575→33.175 s; the return is essentially
  equal at 9.125/9.100 s. The loss is outbound, not the repaired return join.

`dropped-chain/late-pickup-window.json` isolates that loss. The candidate retains
its roughly 2.5 s gain through y=5100. Both still have the dropped-flag objective
`(1691,3696,1360)` and follow the supported path around the back platform. The
faster candidate is already descending toward `(1955,5453,1790)` when the flag
naturally returns to base near 30 s; the slower control is near `(2237,5301,1956)`.
Both then retarget the base, but the candidate has farther to return. This is
an observed dynamic-objective timing cost, not evidence that the short hop is
still lost. Do not pause for the timer, edit the flag lifecycle, omit the pair,
or declare that speed must monotonically improve every changing-objective trip.

The latest revision remains experimental. The first combination's completed
60/26/30 checks are not relabeled as qualification of the changed source. Its
specific branch regression is repaired and affected native verification is
complete; the remaining idle and dropped-objective full-trip gates are open.
Before another broad campaign, trace the added base-goal idle and assess ordinary
full-game objective changes against these retained outcomes. Keep the existing
acceptance gates and preserve all measured tradeoffs for the owner.
