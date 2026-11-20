# Strafe movement: first investigation and experiment contract

Date: 2026-09-04. Continues `STRAFE_MOVEMENT_RESEARCH_HANDOFF_2026-09-04.md`.

## Product target

Build an intermediate human offense player: pursue the objective, strafe jump,
avoid needless deaths, use force powers at useful moments, and shoot with human
accuracy. Strafe jumping is the current capability milestone. Its purpose is
reliable offensive progress, with enough control left to turn, recover, and fight.
Maximum demo speed and flawless expert route reproduction are not the target.

Owner clarification: build around XenJK's current gameplay and physics. Matching
historical base_enhanced servers is not a product requirement. Their settings
still matter when interpreting demo observations, but historical compatibility
must not become a prerequisite for developing the XenJK bot.

Two community players are the owner's preferred references for the intended skill level.
The owner's own demos are also acceptable, and most of the corpus is expected to be the owner's. Use those available examples rather than restricting collection to
the preferred names. These preferences do not establish that a particular
segment is free from rockets, body blocks, force effects, or other interference;
the same per-segment evidence checks apply to every player.

Preserve movement/aim coupling: the player has one view and legal user commands.
Later combat cannot silently receive an independent perfect movement camera.
Measure speed, completion, falls, view changes, and corrective actions separately;
do not manufacture human behavior by adding random deaths. The preferred player
references are now identified, but individual demo segments have not been rated
or validated as a target skill distribution. They provide observations and route
proposals pending that analysis.

## What was done

- Read the handoff, complete Bot2 README, simulator, live airborne controller,
  view smoothing, relevant native Pmove/server setup, and the full existing runner.
- Added `tools/strafe_demo_audit.py`: read-only SHA-256 inventory, sidecar
  provenance, exact duplicate groups, and bounded observation-only extraction.
  Output directories must be new. No input inference or learning is performed.
- Audited three median-sized demos from the ctf4, Kejim, and Kothis folders.
- Ran one short dedicated-server smoke episode on `mp/ctf4`, preserving log,
  result CSV, and crash dump under `.codex-research/strafe-20260904-smoke`.
- Fixed the runner's classification of an unlogged nonzero process exit. Future
  such exits are `ERROR_EXIT`; cleanup termination cannot masquerade as a crash.
- Seven in-memory/framing and runner-result tests pass. No gameplay C/C++ changed;
  no source build or simulator-parity claim is made by this investigation.

## Simulator findings: source evidence, not measured root causes

| Finding | Evidence | Consequence for the experiment |
| --- | --- | --- |
| Mixed-time exit state | `ai_bot2_pmove.c`, landing branch: two extra `Pmove` calls, then only `dummy_ps.origin` is restored | Position, velocity, commandTime, and timers do not describe the same checkpoint. Cannot compose this as a full-state exit. |
| Different view policy | Simulator applies a 1200 deg/s yaw limit on every tick; `Bot2_ApplySmoothing` snaps when `lockAim` is false | A candidate can simulate different inputs from live strafing, even with identical Pmove physics. |
| Search-only action | `EvaluateJumpChain` tries fractions 0 and 1; live `BOT_STATE_AIRBORNE` computes only the boundary angle. `tele_lastChainFrac` is telemetry | An apparently useful future maneuver may never be executed. |
| Setup omissions | Live `ClientThink_real` sets `pmove_float`, `stepSlideFix`, actual animation table, and bot `CONTENTS_MONSTERCLIP`; phantom leaves options zero, uses zero animations and `MASK_PLAYERSOLID` | Physics/contact/animation differences need individual parity cases. |
| Hull margin is not preserved | Simulator inflates XY by 12; ordinary-player `PM_CheckDuck` sets XY back to +/-15 | Comments do not establish a 12-unit clearance guarantee. Measure real swept-body clearance separately. |
| Cadence is conflated | Phantom uses `pmove_msec` as policy-update interval if fixed, otherwise `1000/sv_fps`; native Pmove subdivides a received command internally | Input update rate and physics integration steps must be recorded separately. |
| World behavior is incomplete | Trigger processing occurs in `g_active.c` outside Pmove; phantom uses hazard tests, live `g_entities`, and does not advance movers | Start with a checked static scene; unsupported interactions must be explicit. |
| Native Pmove is not generally pure | `_GAME` branches access entities, effects, RNG, and `solidHack`; Pmove also uses shared `pm`/`pml` state | Copying playerState alone does not establish safe, deterministic rollouts. Audit/isolate the supported subset. |

`start_yaw` is also unused by the current simulator. Do not assume varying that
argument explores different approaches. The chain already looks ahead four
jumps; the hypothesis is not simply that adding a second jump helps. A fair
comparison must give the incumbent its existing lookahead and account for cost.

The mixed-time exit should eventually be replaced by a coherent complete
checkpoint, with a separate stabilization test on a copy. Which checkpoint the
controller actually commits to (first contact or after stabilization) must be
part of the maneuver contract; restoring every field to first contact alone
does not establish that the live controller will immediately execute the next jump.

## Demo evidence and limits

Verified collection path: `<demo folder>\a copy of a all demos`.
The filesystem contains 4,426 demos totaling 51,971,650,026 bytes (48.40 GiB).
SHA-256 identifies 4,409 unique contents: 17 redundant copies. All 4,426 use
the `.dm_26` extension; only the sampled headers were decoded to verify protocol.
There are 4,229 readable metadata sidecars and 197 demos without sidecars.
The full read-only inventory took 587.9 seconds on this machine.
See `.codex-research/strafe-20260904-audit/summary.json` and `inventory.json`
for content deduplication and sidecar coverage; independent session count remains
unknown. Byte-distinct files can still be overlapping recordings of one match.

The external decoder is `<demo folder>\JkDemoExtract`. Its README is stale: snapshot
decoding exists. Its `pmove_sim.py` explicitly omits map collision. Its normal
message walker can silently stop at an unsupported opcode or parsing error.
The audit uses a stricter driver, retains errors, and emits decoded observations
without the extractor's inferred controls or confidence labels.

The first strict attempt rejected all three gamestate messages at opcode zero.
Inspection of XenJK `CL_ParseGamestate` and `CL_Record_f` identified the missing
trailing RMG short. The local adapter consumes that field, accepting only zero
(static map). The external checkout is unchanged. Also reject stale snapshots
whose message number differs from the message currently being parsed.

Authoritative corrected prefix results are in
`.codex-research/strafe-20260904-prefixes-rmg/summary.json` and its three JSONL
files. Earlier rejected results remain preserved in `strafe-20260904-prefixes`.
The original long-running inventory process began before the adapter fix;
its sample errors are superseded by the corrected prefix run, not corpus defects.

| Sample | Date in filename | Advertised game | Advertised gravity | Advertised sv_fps | Decoded observations |
| --- | --- | --- | --- | --- | --- |
| `mp/ctf4` | 2015-10-11 | base_enhanced | 800 | 30 | 1,199 |
| `mp/ctf_kejim` | 2021-11-21 | base_enhanced | 760 | 30 | 1,199 |
| `ctf_kothis_v5` | 2025-07-26 | base_enhanced | 760 | 30 | 1,199 |

Each audit stopped intentionally at 1,200 message blocks, about 40 seconds of
observations. All three normally have 33 ms snapshot spacing with gaps. The
differences between successive observed `ps.commandTime` values vary markedly.
Those values still do not reveal every intervening input or command timestamp.
The files contain server messages, not original usercmd streams (see XenJK
`CL_WriteDemoMessage` and snapshot parsing). Decoded PS dictionaries include
network observations/defaults; they are not complete simulator root states.
Server-command configstring updates are not applied by this adapter. Initial
advertised settings are therefore provenance clues, not guarantees for a whole
recording. No sample is native-replay validated.

`tools/strafe_demo_candidates.py` found 4/5/2 paired airborne-span proposals in
the ctf4/Kejim/Kothis prefixes respectively. It filters deaths, POV changes,
non-increasing command times, and large snapshot gaps. These are observations
requiring geometry and physics inspection, not inferred jump commands. Candidate
JSONs retain four observed checkpoints per pair. A useful first inspection
target is Kejim serverTime **240149-242195 ms**: same-height landings, first exit
speed approximately 413 units/s, and approximately 34 degrees between that exit
velocity and displacement toward the second landing. The first landing is near
`(2124.32, 3320.06, 1808.12)`, with observed velocity `(39.13, 411.29, 0)`.
Whether this contains the desired gap/platform constraint is unverified. Do not
confuse absolute serverTime with elapsed demo playback time. A more severe ctf4
candidate at 111647-113198 ms turns approximately 133 degrees and needs explicit
contact/force inspection before it could fit the fixture's supported subset.

## Interaction exclusion requirement (owner clarification)

The movement examples used to study player control must not be explained by
other players: rocket/splash knockback, other weapon hits, saber kicks, force
push/pull/grip, body blocking, standing on players, and similar interactions.
Apply this gate to the complete maneuver and its approach/exit context, before
input fitting, controller tuning, or scoring human movement accuracy.

Candidates have three conceptual dispositions: clean for the validated scope,
suspect, or unknown. The current automated screen can only return **suspect** or
**unknown**, and sets `eligible_for_control_fitting=false` for both. It must not
turn missing events, absent visible opponents, or a successful kinematic fit
into a clean verdict. Existing candidate nominations remain unapproved.

Implemented screening in `tools/strafe_demo_candidates.py` checks health/armor
decreases, changes to `damageEvent`, active `PMF_TIME_KNOCKBACK` with `pm_time`,
and special force/hand states. These are conservative exclusion signals, not
proof that an opponent caused the change (fall damage or a self-initiated force
state can also flag a segment). Repeated `damageCount` alone is not treated as a
new hit. Native `ClientTimerActions` also decays over-max health and armor:
one-point decreases above the observed `STAT_MAX_HEALTH`, without a damage-event
change, are marked possible passive decay (unknown), not automatically hits.
Review includes 500 ms before and 250 ms after the maneuver and reports
missing padding or discontinuities. This padding is an initial review window,
not a guarantee: earlier impulses can persist after timers and events expire.
Extend the approach review or reject the sample when that history is unclear.

The audit now retains each snapshot's entity list and area mask, including
visible players, missiles, and event entities, using only the valid snapshot's
members rather than stale ring entries. This supports subsequent dynamic-contact
review. It does not provide complete world visibility. A nearby player alone
does not prove collision; no nearby player in the snapshot does not exclude it.
Body-contact review needs swept hulls with timing/position uncertainty, player
solidity and trajectory semantics. Projectile disappearance alone is not an
explosion; resolve event type, position, ownership and affected-player evidence
where available. Missing evidence remains unknown.

Before accepting a control example, require adequate approach history, verified
physics/BSP identity, and a native collision-aware replay that explains position
AND full velocity through takeoff, contacts and exit using legal controls, with
no fitted external impulses. Investigate unexplained residuals as possible
interactions, omitted state or decoder/physics errors; do not tune movement to
absorb them. Replay agreement alone is insufficient when multiple control and
interaction explanations remain possible. Use event/entity review and, for the
first selected segment, visual review as supporting evidence. If ordinary demos
cannot resolve the ambiguity, use a deliberately recorded solo session with
exact commands/full state, or the seed-free fixture, for the first experiment.

Preserve excluded segments with reason/provenance for a later disturbance and
recovery evaluation. They must remain separate from clean strafe-control data.
The new bounded re-extraction and screen are stored under
`.codex-research/strafe-20260904-interactions`; earlier artifacts remain intact.
The authoritative `sample-*-screen-v2.json` files account for possible passive
decay: **5 suspect and 6 unknown** across the 11 proposals, with **zero approved
control-fitting examples**. The initial screen/summary in that directory
predates this refinement and conservatively flags all 11. Thirteen tooling
tests pass, including armor-only hits, pre-departure knockback, missing history,
empty visible-entity lists and passive overhealth decay. No body-contact or
native replay validation has been completed yet.

Local BSP candidates and hashes are in the smoke directory's
`bsp-candidates.json`. `assets0.pk3` and `assets3.pk3` contain different
`maps/mp/ctf4.bsp` bytes. Kothis has multiple versions and directory conventions.
Match the actually resolved BSP and physics profile before using a demo seed.
Neither a folder name nor a metadata `checksumFeed` establishes BSP identity.
Reserve an additional, as-yet-uninspected map for a later transfer claim; all
three audited maps are development evidence, not untouched test maps.

## What the live smoke episode established

The existing engine loaded the installed game DLL, not a freshly built module:

- Engine: `build/Release/openjkded.x86.exe`, SHA-256
  `85f68b589916f6d22380bc2b8af2fb0f90b514f0e1694194d545d6f96c9a97ef`.
- Loaded game: `game_directory/Jedi Academy/GameData/base/jampgamex86.dll`, SHA-256
  `ff891aa30f28937d9d78edf0a5822b8cca7003f24bf6f57b773733e9c3174899`.

The dump module list established the loaded paths. Server log advertises
`sv_fps=40` and `g_stepSlideFix=1`. An Alora bot on red ran with Bot2 forced.
The process exited after 43.97 wall seconds with `0xC0000005`. The dump's
exception address lies in `ntdll.dll`; that is not enough to identify the root
cause. Do not attribute it to a specific Bot2 line without a matching-symbol
stack. The original CSV says `EXIT` because it predates the runner fix.

There were 21 LAND telemetry records: mean XY error 36.78 units, maximum 304.78.
These are selected landing observations during an uncontrolled run. They are
not an error bound, independent trials, or identical-state replay comparisons.
No capture was observed. A local port/homepath launch works, but the existing
binary episode is unsuitable as a completed benchmark. The runner's map
discovery also warns about `mp/ctf4` because it only discovers `ctf_*` names;
explicitly requesting the stock map works.

## Smallest experiment worth implementing next

Implement a gated Bot2-owned movement fixture, before another movement scorer.
Use a new source-build directory and an isolated runtime containing explicitly
staged engine/game binaries; record their hashes and verify the loaded DLL path.
Do not use the install batch's cleanup/install flow for this research run.
The existing Visual Studio 2022 Win32 CMake setup is available; build viability
and the crash's source-build reproducibility have not yet been tested.

1. **Recorded-command parity gate.** One ordinary humanoid, static BSP geometry,
   no opponents, vehicles, active force powers, pickups or trigger intersections.
   Exclude unsupported scenes explicitly. Use a fixed command schedule at the
   actual bot cadence (start at 25 ms, matching this smoke setup). Test both
   `pmove_fixed=0` and fixed 8 ms integration while retaining that same command
   schedule. Include still, run, takeoff, airborne steering, landing, and braking.
   Capture root PS in full, incoming usercmds including angles/delta angles and
   timestamps, relevant pmove setup, hull/contact outputs, animation/control
   state, and pre/post server checkpoints. Re-run exactly those commands through
   an isolated native predictor against the same geometry. A deterministic
   fixture preamble/fresh process avoids pretending that resetting origin and
   velocity resets the world. Hash settings, assets, binaries, and the trace.
   Report every-step position/velocity/view/timer/contact differences and
   supported world side effects. For identical deterministic movement math,
   require exact movement/timer/contact agreement; any numeric allowance must
   be justified before the policy experiment. Then compare whole-server
   checkpoints separately to identify work outside Pmove. Do not widen landing
   tolerances to pass this gate.

2. **One feedback maneuver.** On broad, safe floor, run one legal-input
   strafe-jump policy through the very same input generator in prediction and
   execution. Include last view and jump-button/contact phase in controller
   state. Retain a seed-free policy. Demonstrate a coherent entry/exit checkpoint
   and reproducible command trace before optimizing speed. Explicitly measure
   any proposed view-rate cap; no intermediate-human rating is claimed yet.

3. **Two-maneuver discrimination.** Choose one short gap -> platform -> turn ->
   second landing in development geometry, plus the broad-floor control. Place
   two candidate first maneuvers on the same platform: a faster arrival pointed
   toward the outside edge, and an arrival with a usable heading/speed for the
   next jump. Candidate routes, target regions, timeout, and failure criteria
   are fixed before evaluation. Route discovery is outside this first test.
   Success means completing both maneuvers and remaining controllable in the
   second exit region; first-platform contact alone is not success.

Compare (a) incumbent Bot2, (b) fixed nominal commands, (c) the same feedback
controller with local landing/time scoring, and (d) that feedback controller
with continuation feasibility/time scoring. The clean scientific contrast is
(c) vs (d): identical inputs, proposals, physical limits, and rollout budget,
changing only the value assigned to the complete exit state. The incumbent is
an engineering reference; do not claim it lacks lookahead. Give the local scorer
the same candidate rollout allowance. Count failed/no-candidate searches.

Start with the nominal case and eight signed, one-variable probes (lateral
position +/-16 units, longitudinal velocity +/-50 units/s, lateral velocity
+/-50 units/s, and launch timing +/-one command interval). Then test both
grounded/jump-released and landing/jump-held phases only where the full captured
state is physically valid. These probes are debugging cases, not independent
probability estimates. After locking the controller, evaluate the same 100
seeded, sampled initial disturbances for all controllers, including vertical
velocity/contact state where valid. Publish the distribution, every outcome,
paired completion differences and uncertainty; do not count deterministic
reruns of one seed as additional independent evidence.

Provisional compute experiment: at most 32 trajectory candidates and 1,280
native Pmove calls per decision (40 command intervals/candidate); measure a
2 ms target on this machine, checking elapsed time between candidates and
reporting overruns. These are proposed caps, not measured throughput or a
guaranteed deadline. If the horizon cannot span the second maneuver, report
insufficient coverage and choose an appropriate bounded horizon for all arms
before locking the experiment. Include p50/p95/max decision latency, amortized
per-bot cost, offline preprocessing, and separately timed setup/logging.

The discriminating result is improved *complete traversal* and usable exits
under matched budgets, not just higher speed or lower path error. If local
feedback performs as well, keep the simpler controller. If parity fails,
diagnose it before interpreting any composition result. Only then attempt
automatic placement on held-out geometry or expand demo processing.

## Reproduction

```powershell
python -B tools/strafe_demo_audit.py --root '<demo folder>\a copy of a all demos' --out '.codex-research/strafe-audit-NEW' --extractor '<demo folder>\JkDemoExtract' --sample-folder ctf4 --sample-folder kejim --sample-folder kothis --blocks 1200
python -B -m unittest discover -s tools -p 'test_*.py' -v
```

The inventory reads roughly 48 GiB to hash every demo. Use a new output name;
there is no deletion or cleanup step. Existing source changes are unrelated and
were preserved. The initial investigation implements the evidence tooling and
defines the experiment. The native fixture was subsequently implemented and
tested; see [native fixture results](STRAFE_MOVEMENT_NATIVE_FIXTURE_2026-09-04.md).
The proposed controller comparison remains unimplemented. The earlier
"next experiment" section records the plan before that fixture work.
