# Ordinary-route preparation fallback trials

September 13, 2026. Owner work on Astra, no subagents. Dropped-flag retrieval is
deferred to objective/combat work. This study uses ordinary enemy-base travel.

## Finding and decision

Do not advance `bot_supportedPrepareFallback`. Two variants are implemented and
measured; neither meets the complete-trip performance requirement. The switch
remains off. Preserve existing coast and prepared-prefix behavior in normal use.
No installation or broad movement qualification is claimed.

Retained ordinary-route evidence shows a jumppad landing followed by braking
while supported preparation is incomplete. In the reference example the owner
acquires at level 13150, its initial coast ends at 13400, and the first executable
prefix appears at 13600. A sampled speed falls to 111 u/s in that interval.
Other startup trajectories publish a short prefix and then brake near its end.
Those are distinct contracts: lack of an executable interval, and arrival at
a currently published temporary endpoint.

## Broad trial

The first trial restores the incoming command and returns false when no prepared
command or coast owns the frame. Preparation remains live. Ordinary dispatch
then tries its existing route/local navigation/fallback owners; this does not
create a new validated corridor or waive an existing native check.

Build: `.codex-research/strafe-build-prepare-fallback-0fbf7555d0e34c57bfed374045c00444`.
Evidence: `.codex-research/prepare-fallback-0fbf7555d0e34c57bfed374045c00444`.

The four complete ordinary-goal fixture trips capture alive, health floor 90:

| Pair | Control / candidate start | Capture seconds | Sampled idle ms | Sampled walking ms |
|---|---|---|---|---|
| 1 | 2150 / 2325 | 32.150 / 32.175 | 250 / 250 | 11950 / 12325 |
| 2 | 2325 / 2325 | 34.500 / 32.175 | 1250 / 500 | 13150 / 12000 |

Pair 1 has different movement-clock phase. Matching fixture starts in pair 2
do not imply identical private state; the fixture resets its declared state.
All rows, uncovered time and commands are in `basegoal/comparison.json` and
`basegoal/declaration.json`. The old scoring definition is unchanged.

Four 60-game-second ordinary-spawn solo worlds at 1x also capture alive, health
floor 90, with zero audited confinement. The broad candidate worsens walking:
red 33.8→48.1%, blue 32.3→36.1%. First capture is later by approximately three
seconds on blue and four on red (different initial observation times retained
in `spawn/outcomes.json`). This is discovery, not phase-matched qualification,
but it does not support accepting the broad rule. Both candidates emit the new
fallback event. `spawn/audit.json` uses the existing grounded 3D speed bins,
distinct from the fixture's XY sampled metric.

## Restricted coast-expiry trial

The current source narrows the same opt-in switch to an expired coast timer,
world-ground contact and at least 80 u/s XY speed. Earlier coast refusals,
stationary startup and airborne preparation keep existing behavior. This
avoids the broad trial's unconditional preparation handoff. The helper exposes
`--supported-prepare-fallback`; default is zero.

Build: `.codex-research/strafe-build-coast-expiry-0fbf7555d0e34c57bfed374045c00444`.
Evidence: the `expired/` subdirectory of the evidence root.

All four ordinary-goal worlds start at level 3350 and capture alive, health
floor 90. Both controls take 32.150 s; both candidates take 32.200 s. Sampled
idle remains 250 ms; walking decreases 12250→12125 ms. The first outbound edge
is 250 ms later with the candidate. **Both full trips regress by 50 ms**, so this
variant is not accepted either. There is no threshold adjustment to excuse it.

The restricted variant's four normal-spawn worlds also capture alive with zero
audited confinement: red first capture 39.200→39.100 level seconds, blue
28.700→28.800, with first observation at 1.700 seconds in every world. Red has
one capture per arm and blue two. Walking changes 41.1→40.7% red and
30.7→31.9% blue; idle is 1.3% in all four. All eight restricted-variant worlds
verify against the current 1105-source manifest, staged binaries, fixtures,
runner and experimental settings. This remains mixed discovery evidence.

The candidate changes an earlier preparation interval on the route, while
short-prefix braking can still occur after landing. A global coast handoff
does not directly solve that endpoint contract.

## Checks and retained limits

The extracted production-owner check verifies command restoration, preparation
retention, stationary wait expiry, and preserved handling before coast expiry
and while airborne. Existing lifecycle, partial-prefix/no-rewind and 56 rotated
Speed admission/deactivation checks pass. Twelve existing play-seed tests pass.
The first check attempt had an artificial three-second gap between calls and
correctly triggered lifecycle reset; the corrected check drives every 25 ms.
Both check outputs are retained.

Each native build is unique; measured sources are frozen through its collection.
The broad build's eight-world source/settings/binary/fixture verification passed
before narrowing the code. Its manifest does not bind the later changed source.
At most four isolated hidden worlds run together, on ports below the dynamic
UDP range. No dropped flag, body, damage or mid-run pose/resource intervention
is introduced. Normal-spawn discovery is not a defended or body-compatibility
qualification.

## Next bounded work

Keep `bot_supportedPrepareFallback=0`. Inspect the temporary-prefix endpoint
on the ordinary jumppad landing: how much checked path is published, how close
the moving player is to its stopped endpoint, and when a usable continuation
becomes available. Existing background landing recovery already addresses a
different refused-prefix case; do not blindly repeat that repair. Collect a
local prefix/publication trace if the existing evidence cannot distinguish
the states. A future rule must retain native geometry/resource checks and
improve complete ordinary trips, not just remove a local speed dip.
