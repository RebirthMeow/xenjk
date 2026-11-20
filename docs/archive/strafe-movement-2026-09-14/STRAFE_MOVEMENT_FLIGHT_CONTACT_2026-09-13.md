# Flight contact reproduction and outbound walking diagnosis

## Initial diagnostic experiment

Work order: [immediate implementer](STRAFE_MOVEMENT_IMMEDIATE_IMPLEMENTER_2026-09-13.md).
No installation, commit, combat changes or qualification claim is in scope.

The retained 525425–525975 failure lacks native collision identity and cached
pre-hop state. Its three fresh-path solo approximations are not replays. The
current experiment adds opt-in evidence capture, not a gameplay correction.

Build: `.codex-research/strafe-build-flightcontact-980a6a09ef764899978dd1d2d8817614`.
Evidence: `.codex-research/flight-contact-c9e102ce84ea41baaf1a34be628833a8`.
Production movement remains the background-recovery baseline. Diagnostics wrap
only actual native Pmove traces, after forecast preparation, forwarding each
trace exactly once and restoring the callback afterward. The capture stores
cached flight owner/path/follower, Pmove and player/client state, selected
forecast frames, linked entity hull/state snapshots and native results. Raw
records are tied to this Win32 build ABI; pointers are evidence only and must
never be loaded into a new process. This is not a whole-server savegame.

`bot_flightCapture` is dedicated/cheats-only and defaults off. The runner uses
devmap when capture is requested; no pose/resource intervention is made in the
defended observation. Capture overhead is excluded from runtime-cost claims.
All-window capture is used. The optional window filter needs lifecycle review
before use across multiple window entries; it is not used in these runs.

The first 30-second preflight used normal map startup and correctly produced no
capture with cheats disabled. The corrected preflight enables diagnostic devmap,
completes alive and writes two flight captures. All 82 existing mocked adapter
lifecycle scenarios pass with capture disabled. Native preflight forecast/live
position and velocity agree exactly over the matching captured frames. This
checks the capture boundary, not multiplayer safety.

Two fresh 600-second normal-speed blue/one-opponent worlds are being collected,
seeds 2026090807 and 2026090808, isolated ports 58411/58412. Commands and scope
are in `capture-declaration.json`; final commands/results go to `capture-runs.json`.
These use the current source baseline, not the old landing-trial build, and
must not be relabeled as deterministic replays of the historical match.

The original retained summary confirms two opponents. Two additional blue
600-second worlds with two opponents and the same two seed values use ports
58413/58414, declared in `two-enemy-declaration.json`. The one-opponent worlds
remain lower-contention controls. No more than four worlds run concurrently.

## Walking diagnosis so far

The movement-only objective command bypasses the legacy ability policy and
leaves resource actions to movement. The supported controller never requests
Force Speed. In the retained red interval 182225–191475, Force remains 100.
The preparation trace at 182450 identifies the long first edge as `hop=0`,
`first_clear=elevation` at sample 1; the next edge allows hopping. Thus missing
Speed and a terrain-based no-hop decision coexist. The slope restriction must
remain unless separate native evidence validates another envelope. Investigate
useful Speed on supported ground-only travel, with real braking/corridor/turn
constraints; do not force hops or activate Speed everywhere to change counts.

## Contact evidence, native control and first repair

The original demo's parsed launch window adds a collision identity: opponent 2
reports `groundEntityNum=0` in snapshot 525750, the same snapshot in which runner
0 loses upward velocity. Snapshot labels are 25 ms earlier than the pre-command
momentum log labels for these states; do not conflate them. The opponent stood
on the runner briefly. The external decoder ends later with parse errors in all
three recordings; the retained launch window is inside the successfully decoded
prefix, not a claim of complete or independently validated demo decoding.
`original-demo/actor-{0,2}-contact-window.json` and decoder bindings retain this.

A fresh two-opponent world captured window 11/candidate 2 at 133325 and its
pre-hop state at 133575. The terminal dedicated movement replay rehydrates the
cached path, controller and native PS/Pmove values, replacing every serialized
pointer with local storage/callbacks. It calls native Pmove and production flight
execution/recovery for at most 1600 ms, then terminates the private world. It
omits the game loop, combat AI and regeneration; it is not whole-match replay.

Build `strafe-build-flightreplay-7488dc165fc54284a5b1ed2dd3304592`:
clear replay reproduces all 64 native position/velocity pairs exactly (also from
the later 133600 seed). A declared gravity/body proxy uses the original
opponent's relative pre-hop offset and velocity. At 133650 native trace hits
entity 132's underside, normal (0,0,-1), and upward velocity collapses. Flight
refuses at 133775; recovery fails at 133875, below the receiver's floor. This
reproduces the interference mechanism with cached native state, not the full
original match. The earlier replay build's embedded forecast chunk still came
from startup; comparisons use the original seed's native frames instead. The
later build loads the seed forecast explicitly.

The first repair retains the selected forecast's positions per client (including
correct first-candidate retention during exhaustive diagnostics). At a proposed
grounded flight jump, five 25 ms segments are checked against nearby linked BODY
hulls using their current velocity/gravity. Relative swept boxes have a 0.25-unit
curvature allowance. A conflict restores the pre-command policy/continuation and
incoming command and releases on world ground as `takeoff_body_motion`, naming
the body. Static world/floor/resource/receiver checks remain in force. This is a
short takeoff check, not a guarantee against arbitrary later opponent motion.

Build `strafe-build-bodyguard-37045bc8f80847618b7f4d860297c923`:
`bodyguard-moving-body` refuses at 133600, ground 1022, z=1808.125, before jumping.
Clear, moving-away and higher-clearance controls retain all 64 native position/
velocity pairs exactly. `bodyguard-declaration.json`, `bodyguard-replay-runs.json`
and `replay-comparison.json` bind the declared comparisons. All 82 mocked adapter
lifecycle scenarios still pass, with added moving-body/clearance/reset checks.

Broader unchanged twenty-case/three-repetition and 26-body/30-resource checks
are collecting under `guard-verification/`; full-game continuation and defended
verification are not yet established for the repair. Walking correction is next
after affected flight verification. Nothing is installed or committed.
T5/T6/K07 remain open.

## Review checkpoint after collection

The collection statements above describe the implementer's earlier checkpoint.
`guard-verification/verification-runs.json` now records successful completion of
all three repetitions and the affected regressions. Review confirms 60/60 solo
captures, all alive, health floor 90 and zero churn; 26/26 temporary-body cases
meet subset thresholds; 30/30 resource cases complete without refusal. Current
1104-source/two-binary bindings match. Whole-trip time/idle/walking scoring has
not yet been completed for this arm.

None of the sixty solo worlds logged a takeoff-body guard event. They support
regression retention, not safety of the newly guarded interaction. The moving-body
replay breaks immediately when the adapter returns false. Ordinary supported
movement instead invokes the path follower on that same frame. Thus the replay
establishes a grounded adapter refusal but not the ensuing fallback command,
avoidance of a later unsafe jump, useful progress or eventual safe continuation.

Next verification must run the moving-body case through ordinary dispatch and
the native game loop, retaining same-frame commands, contacts, retries and
completion, then affected normal-speed defended checks. Preserve the contact
diagnosis and clear controls; do not repeat completed reproduction or integration
subsets without a new reason. Outbound walking has a useful diagnosis (no
supported Speed request plus an elevation-based no-hop edge), but no implemented
correction or before/after evidence yet.
