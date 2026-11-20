# Strafe movement research: restart in XenJK

Date: 2026-09-04. Destination: the repository root.

**Historical research origin, not the current restart procedure.** For the
current AI handoff, read the [implementation guide](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md),
[active plan](STRAFE_MOVEMENT_PROJECT_PLAN.md) and
[scorecard](STRAFE_MOVEMENT_PROGRESS.md). Do not rerun the early reconnaissance
or follow the suggested opening message below as today's work order.
Aiming experiments remain paused.

Architecture completion at that historical checkpoint: [two layers of movement control](STRAFE_MOVEMENT_TWO_LAYERS_2026-09-04.md).
Route/navmesh guidance now feeds one shared local controller. Its native
ordinary-movement forecasts agree with execution, and the tested airborne
handoff survives. Coverage, general recovery and multi-bot cost remain open.

Continuation results: [spawn-to-enemy-flag benchmark](STRAFE_MOVEMENT_FLAG_RUN_2026-09-04.md)
records the user's later step-3 acceptance test, after the native fixture,
feedback, forecast and two-maneuver investigations. The original proposal
below is retained as historical research context.

The subsequent [live-runtime work](STRAFE_MOVEMENT_LIVE_RUNTIME_2026-09-04.md)
extracts the cheap shared controller, connects it to normal Bot2 movement,
and records the remaining cost of legacy recovery planning after interruption.

Earlier continuation (aiming now paused): [airborne steering during aim turns](STRAFE_MOVEMENT_AIM_FRAME_2026-09-04.md)
tests a shared command-frame correction after the fixture/RNG forensics. It
addresses live recovery without importing the previous Unreal atlas.

## Owner intent and scope

The owner wants to investigate a general solution to strafe jumping through different 3D spaces, levels, and maps. UnrealJKA has accumulated approaches and remedies; the question is whether a better organizing principle can replace that accretion. The owner selected XenJK as the iteration environment to avoid Unreal overhead, and reports approximately 4,000 player strafe-jumping demos that could support the work.

This document preserves the preceding investigation and proposal. It is a research starting point, not a proven algorithm or an instruction to port the Unreal stack. The owner has requested this handoff; no implementation campaign, bulk demo processing, training, or architectural replacement has yet been performed under this request. No new Codex task was created.

## Proposed organizing principle

Navigate between controllable movement states. Choose a maneuver by the useful options available after it, including momentum, rather than by landing position or speed alone.

Example: a central landing at 900 units/s toward a platform's outside edge may have no safe continuation. A less direct landing at 550 units/s aligned with the next jump can be better. The objective is to arrive in a region of states from which the next maneuver remains achievable.

A maneuver has a common contract:

- Entry region: positions, velocities, movement modes, and resources from which execution works.
- Feedback policy: legal commands generated from the current state, including internal controller state.
- Traversal envelope: body occupancy and contact behavior during execution, with a stated disturbance model.
- Exit region: resulting states compatible with subsequent maneuvers.
- Cost and evidence: duration, failure measurements, resource consumption, validation provenance.

Connections require compatible full states, not merely nearby endpoints. A robust composition requires the relevant possible exits to lie inside the next controller's admissible entry region; nominal overlap alone is insufficient. An empirically tested region is not a formally guaranteed funnel.

The working architecture is a sparse graph of these connections plus bounded live rollout from the actual state. Offline or incremental search discovers alternatives and estimates cost-to-go. Online execution simulates the same feedback policy it will execute, commits a short portion, and reassesses. Walking, braking, jumps, waiting, and recovery share this interface while retaining their different physical modes.

Conceptual objective: expected remaining completion time subject to an explicit failure-risk limit. Cost-to-go includes momentum and resources; speed earns value through enabled future progress. Safety and survival take priority when disturbances invalidate ordinary traversal. A finite search returning no candidate means 'not found within this search,' never proof of physical impossibility.

This is a hypothesis to test. It must earn its complexity against simpler controllers and the incumbent. Do not equate 'add MPC' with solving topology, horizon choice, or compute cost.

## What was actually learned from UnrealJKA

Read source: <UnrealJKA>\JKA\Source\JKACore\AI\JKABotMove.cpp, JKABot.cpp, JKABotLineTrack.h. No live Unreal benchmark was run during this conversation.

1. The chain scorer combines nav distance, straight-line distance, speed, and altitude penalties. These are geometry-dependent proxies for continuation value. More lookahead alone does not fix an inadequate action alphabet or state representation.
2. The offline TAS search already expands legal commands through rich Pmove states, including airborne/contact/animation phases. Landing-only primitives cannot express all successful routes; the ctf4 example requires midair control changes and wall interaction.
3. Lines-first is a valuable foundation: the 2026-08-25 scorecard reports outbound time falling from roughly 23.2 s to 12.3–12.6 s, including 4v4, and reduced bot computation. These are historical results on the measured setup, not current XenJK results or proof of cross-map generalization. Faster outbound trips did not automatically improve captures: untaped return behavior mattered.
4. Different controllers and safety gates use different notions of useful continuation. Ownership and handoff seams are a substantive source of complexity.
5. Simulator/executor agreement includes input cadence, yaw policy, collision hull, trigger handling, timers, and world state. Sharing Pmove is necessary but not sufficient.
6. Recovery audits distinguish survival from mission usability. Their sparse constant-control searches do not prove unrecoverability. An audit that changes omitted state cannot establish a clean counterfactual for the original full state.

Read these selectively, not as implementation requirements:

- <UnrealJKA>\JKA\Docs\BOT_STRAFE_SYSTEM_MAP.md — accumulated architecture and seams; historical snapshot.
- <UnrealJKA>\JKA\Docs\STRAFE_ATLAS.md — atlas, full-state search, discovery/refinement, and replay contracts.
- <UnrealJKA>\JKA\Docs\LINES_FIRST_DESIGN.md and LINES_FIRST_IMPL_SPEC.md — tracking and join-region ideas.
- <UnrealJKA>\JKA\Docs\LINES_SCORECARD_ANALYSIS.md — measured value and task-level bottleneck.
- <UnrealJKA>\JKA\Docs\RECOVERY_VERDICT_AUDIT_2026-08-25.md — verdict limitations and omitted-state hazards.
- <UnrealJKA>\JKA\Docs\RESIM_PARITY_HANDOFF.md — demo input inversion and bounded-window replay; engine-specific assumptions need checking.

The core transferable asset is the reasoning and experimental machinery, not Unreal-specific code or every historical remedy. Comments and older design documents can be stale; verify live implementations before relying on them.

## XenJK reconnaissance already completed

Repository: the repository root. HEAD at inspection: d5344a5dc940bd6053078977ba4d64004d13350e.

Read root AGENTS.md and the opening architecture/working sections of codemp/game/ai_bot2_README.md. Follow both; re-read the complete relevant sections before code edits. Minimize upstream footprint, keep new logic Bot2-owned, inspect all consumers before changing shared state, and build changes to cross-file interfaces.

Useful entry points, relative to XenJK:

| File | Starting point |
| --- | --- |
| codemp/game/ai_bot2_pmove.c | Bot2_PMTrace, Bot2_PMPointContents, SimulatePmoveTrajectory (~93), ScoreJumpChain (~415), EvaluateJumpChain (~479), IsSafeToJump (~567) |
| codemp/game/ai_bot2_movement.c | Bot2_ExecuteMovement, movement ownership, floor/hazard probes |
| codemp/game/ai_bot2_wallrun.c | Wallrun simulation and geometry scanner |
| codemp/game/ai_bot2.h, ai_bot2_internal.h | State and simulator/controller interfaces |
| codemp/game/bg_pmove.c, bg_slidemove.c | Native movement and collision semantics to inspect |
| tools/bot_test_runner.py | Existing dedicated-server CTF completion runner; inspect fully before reuse |
| codemp/game/bot2_svcmds.c | Existing diagnostic command integration |

The runner header describes per-map capture testing and separate ports/homepaths for concurrent server instances. That is useful infrastructure, but a flag-capture timeout alone does not measure maneuver robustness or controller parity.

Working tree is already dirty: tracked changes in cgame and game CMake/hooks, plus untracked aimfeedback, conctrace, hitcoverage, saberbench, lesson scripts/docs, and other files. Treat them as unrelated owner work; do not reset, clean, overwrite, or absorb them. Root .codex-archive already exists. Global owner policy is archive instead of delete, with manifests; permanent deletion requires explicit exact-target approval.

Read-only Git queries needed a command-local safe.directory override under the sandbox identity. No global Git configuration was changed.

## Use the ~4,000 demos as evidence, proposals, and tests

The corpus count is owner-reported; its location, unique count, protocol mix, map coverage, and input observability have not been audited. An older Unreal handoff mentions <demo folder>\a copy of a all demos\ as a candidate location. That path was not verified in this conversation.

Start with a read-only inventory and a small representative sample. Record content hashes, source paths, map/BSP identity, game/mod/protocol, timing, POV identity, relevant physics settings where observable, duration, and extraction confidence. Deduplicate before treating files as independent evidence.

Do not assume raw user commands are present. The Unreal resim documentation describes standard demos as server snapshots, with richer recorder playerState and sparser/PVS-limited other-player entityState. Verify the actual XenJK/demo formats. Snapshot cadence is not command cadence; interpolated display motion is not ground truth; input inversion can have multiple solutions.

Use the data in ascending order of required trust:

1. Route and topology proposals: where skilled players go, useful approaches, alternate paths, takeoff/landing/contact events.
2. Reachable-state sampling: authentic position/velocity pairs, speed/heading distributions, difficult transitions, and observed mistakes.
3. Maneuver seeds: infer plausible controls for short windows, then validate by native simulation. Store residuals and ambiguity rather than calling inferred controls exact labels.
4. Feedback development: perturb replay-valid starts and optimize a controller's completion region. An observed successful line alone says nothing about its robustness nearby.
5. Optional learning: use verified examples to propose actions or approximate value-to-go. Keep held-out evaluation and live rollout checks. Do not begin with a giant imitation-learning job.

Prefer richer POV observations for input reconstruction. Track missing state, knockback, interactions, force state, command timing, and world changes. Unexplained replay errors may be extraction, state, physics-version, or world-model errors; do not tune a controller to compensate for them.

Split evaluation by map and recording/session, not random adjacent frames. Keep test-map demos out of training, seed selection, and tuning when claiming unseen-map transfer. If automatic preprocessing of test geometry is allowed, state that separately. Human time is an observed reference, not a global-optimum certificate.

## Bounded first experiment

The next conversation should first inspect the native simulator and demo tools, then specify the smallest useful experiment with measured compute limits. Recommended sequence:

1. Establish a resettable movement fixture: exact root state, legal command stream, real collision geometry, reproducible outcome. Compare phantom prediction with actual dedicated-server execution. Include view, command timing, triggers, ground transitions, and relevant timers. Keep sampling and setup outside the timed controller budget.
2. Select a small gap-to-platform-to-turn scenario, plus an easier open-space control. Use one replay-valid human segment as an optional seed; also retain a seed-free baseline.
3. Construct ONE feedback maneuver. Sweep start position, full velocity, timing/contact phase, and a stated disturbance range. Report successes and failure classes, not only average path error.
4. Compose TWO maneuvers. Demonstrate that choosing an exit state for the following maneuver improves complete traversal over choosing only a locally fast/safe landing.
5. Transfer the same machinery to held-out geometry without hand-authored routes or map-specific code. Allow and measure automatic geometry preprocessing. Expand map/corpus coverage only after this discriminating experiment works.

Baselines: incumbent Bot2; nominal sequence/replay where applicable; feedback tracking alone; proposed continuation-aware composition under matched budgets. Separate known-route tracking, automatic route discovery, and disturbance recovery so one cannot mask another.

Metrics: completion probability with counts/uncertainty; successful travel time plus failures/timeouts reported separately; end-to-end expected cost; falls/stalls; usable exit-state rate; prediction error; per-decision and per-bot compute including tail latency; preprocessing/storage; degradation as disturbance grows. Report independent seeds/episodes and physics/settings hashes. Repeated deterministic trials are not independent evidence.

Stop and diagnose if the simulator cannot match execution, inferred demos cannot be replayed adequately, entry regions are too narrow to compose, or a simpler baseline performs as well. Do not widen geometric tolerances until a nominal success becomes a claimed robustness result.

## Design constraints and open questions

- Preserve full simulator state initially; only compress indexing after measuring which omissions cause aliases. Position, full velocity, view/control state, ground/contact mode, timers, resources, and mover phase can matter.
- Use native legal inputs; preserve player/bot mechanical parity. Controllers and simulation must share input generation, including view constraints.
- Use event-aware or adaptive action durations; whole-jump fixed-direction primitives are an initial restriction, not the problem definition.
- Retain alternative topology and heading representatives. A cheap state arriving sooner is not necessarily dominant if future options differ.
- Geometry-specific regions cannot be translated across walls, floors, or maps without validation. Same-physics skills may transfer; their placement and admissible regions do not automatically transfer.
- Unknown coverage is not unsafe-by-proof and not safe-by-default. Keep feasibility, empirical robustness, and formal claims distinct.
- Start with static geometry, then explicitly add movers and interactions; record this limitation rather than claiming arbitrary live-world coverage.
- Corpus location/formats, available binary/build loop, intended skill/humanization constraints, preprocessing budget, and target runtime budget remain to be established in XenJK.

## Research precedent

These were consulted to situate the proposal, not to claim it is solved for JKA:

- Majumdar and Tedrake, Funnel Libraries for Real-Time Robust Feedback Motion Planning: https://arxiv.org/abs/1601.04037 . Feedback regions and composition under bounded uncertainty; its formal machinery/guarantees do not directly transfer to this hybrid collision kernel.
- LaValle, Planning Algorithms, kinodynamic planning: https://lavalle.pl/planning/node719.html . Planning with state that includes velocity and dynamics.

## Suggested opening message in XenJK

Latest correction: `STRAFE_MOVEMENT_RECOVERY_FORENSICS_2026-09-04.md` fixes
fixture command inheritance and controls random ties. It withdraws the
earlier two-regression causal claim, preserves all old results, and records
a native surviving alternative from byte-identical complete player/Bot2
state. Next work should test live/predictor command agreement during aim
turns, then immediate recovery choice, without presuming an Unreal solution.

Continuation results now exist: see `STRAFE_MOVEMENT_FLAG_RUN_2026-09-04.md`
for the completed Kejim spawn-to-flag comparison,
`STRAFE_MOVEMENT_LIVE_RUNTIME_2026-09-04.md` for the inexpensive live adapter,
and `STRAFE_MOVEMENT_RECOVERY_BUDGET_2026-09-04.md` for the subsequent capped
recovery experiment. The latter reduces compute but introduces two new deaths
in seven paired cases and is disabled by default. Preserve those failures for
the next experiment using route guidance during recovery.

Read docs/STRAFE_MOVEMENT_RESEARCH_HANDOFF_2026-09-04.md and the repo's AGENTS.md/Bot2 README. Continue the general strafe-movement investigation here using native XenJK for fast iteration. Our hypothesis is that reusable feedback maneuvers with compatible entry/exit movement states can replace accumulating route and recovery exceptions. We have roughly 4,000 player demos that may supply topology, state samples, and replay-validated maneuver seeds. First audit the existing simulator, demo access, and test loop; then identify the smallest experiment that can validate or falsify continuation-aware maneuver composition. Preserve unrelated working changes. Treat the handoff's proposal as a hypothesis, keep demo observability and simulator parity explicit, and do not port the entire Unreal architecture.
