# What the archive taught, without the baggage

Each item says whether it is from code, from measurement, or from the
owner. Items from the archive's own claims that could not be tied to code or
a retained measurement were left out.

## Current owner direction and interpretation, 2026-09-19

The [refined method](records/2026-09-19-refined-method.md) governs new work.
Independent movement is the base capability, aiming for roughly 15-second
matched stationary no-Force stand runs; that target is unachieved. Prepared
plans are optional candidates under one movement owner. Additional recordings
and universal rejoining are not dependencies for improving independent search.

Graph paths carry approximate momentum but remain proposals. Connected search
must carry native terminal state and continuation context. Current exploration
harvests edges, not complete routes. Online discovery is still parity/fixture
gated; runtime scheduling and multi-bot cost need separate qualification.

The repaired checkpoint results show sub-unit reproduction on empty-world
tapes; they do not qualify the follower or dynamic recovery. A walking join
that cannot supply a fast entry state does not prove every moving join
impossible. Admit a join through its connection and critical continuation,
not geometric closeness, and keep making independent progress when it fails.

The [September 20 landing-search investigation](records/2026-09-20-landing-search.md)
adds a contract lesson: admission can accept a landing inside a later strafe
chord while live progress still holds the takeoff index. Reconcile that index
only at an observed final committed touchdown; do not remove the ordinary
walker's side-entry restriction. Correcting this disagreement halved recorded
unsearched grounded windows in the timed sample but did not shorten the trip.
The reproduced first landing next hit `straight_too_short`. A repaired earlier
gate does not prove the remaining proposal can launch or save time.

The [speed-win follow-up](records/2026-09-20-speed-win.md) measures that bend:
offer existing arc-aimed proposals on final landings without weakening their
native validation. This alone saves little. Validated rerouting plus exit-speed
ranking on the repaired build improves the eight-pair median 18.89 → 17.90 s
and mean by 4.41%, with two regressions and repeated trajectories retained.
Admission and ranking can interact; earlier isolated-arm results on another
binding do not settle the repaired combination. The result costs more search
work and does not qualify production or meet the 15-second target.

The [successor experiment](records/2026-09-20-successor-aim.md) corrects the
second heading's origin using a native first-flight landing, then validates
the full sequence again. Even a small heading correction changes the useful
continuation: a matched first landing is followed by 7.9 units/sec more speed
at the second landing. Repeated complete-run means improve, but some runs
regress and cost rises. The 8-unit run-up adds choices yet slows every pilot;
a larger candidate set is not automatically a better trip. Keep repeated
whole-run evidence and CPU cost beside local momentum observations.

Four measurement facts from the 2026-09-19 guided corridor work and what
followed it, all from measurement:

- **`unsupported` is a bitmask of why the forecast stopped, not a verdict on
  the maneuver.** `256` is `BOT2_FORECAST_TRIGGER`: the rollout ended on
  contact with a trigger entity, which on Kejim is usually an item, the enemy
  flag - the objective itself - or, when the rollout has fallen 1,100 units,
  the map's hurt volume. Only the last is a failed maneuver. Read the bit
  before calling a refusal a physical failure.
- **Telemetry inside the search measures a different bot.** Printing three
  landing-frame reasons where they occur cost 2,000 ms on the guided arm,
  reproducibly. Record the reason in state and print it after the search
  returns; then the arm reproduces its reference runs to the millisecond and
  to the commit count. The mechanism is **unresolved**: the forecast bounds a
  rollout by step counts, not by a clock, so the earlier "search's wall-clock
  budget" reading is withdrawn. The rule is empirical and holds either way.
- **Bind a landing window to the measured leg and the committed plan.**
  The corrected 40 Hz sample has 58 pooled landings, not 67: the old window
  included setup and return activity. Require every frame through t+500
  before the last noncarrying sample. Read `hops` from the preceding commit:
  hop0 is an ending when the plan has one hop. All 26 true middle-of-chain
  landings continue airborne; sequence endings do so 7/32. Grounded mean
  speed falls 445.4 -> 342.9 at +25 ms, consistent with grounded loss, but
  this is not proof of a universal friction-only mechanism. The three runs
  contain two distinct traces; r1/r2 match all 760 safe-window momentum rows.
  Distinct-trace counts and means are reported beside pooled observations.
  The earlier heading-exclusion and commit-delay ownership claims are
  withdrawn. [Corrected record](records/2026-09-19-what-the-kept-speed-buys.md).
- **Runs inside one parallel batch are not independent samples.** Three or
  five worlds started together land on the same outcome with identical commit
  counts - the shared load reaches the simulation by a mechanism as
  unresolved as the one above - while sequential runs of the same build and
  settings spread about
  1,550 ms. An arm measured as one parallel batch reports its own batch, not
  its distribution; alternate arms across sequential runs instead.

Historic reroute/tap measurements below retain their original conditions.
The candidate-result repair requires matched repaired controls before their
causal/ranking conclusions guide a new policy. Short-rollout throughput is
an estimate for planning an experiment, not unlimited compute or production
timing proof. Current ordering is in [HANDOFF.md](HANDOFF.md).

## Engine facts (from code)

- Ground accel 10, air accel 1, friction 6, stop speed 100, jump 225,
  gravity 800, base speed 250, Force Speed 1.7. See PHYSICS.
- Force Jump heights 96/192/384 above takeoff with strengths 420/590/840;
  ordinary tap is 225.
- A saber jump with side or back input flips. Launch forward, strafe after.
- Force Speed costs 50 and does not drain further. Level 3 lasts 20 s.
- Falling death is sealed by the engine about 1100 units above the floor.
  Past that boundary no command changes the outcome.
- Listen servers call bot AI at render cadence; one decision per game frame
  must be enforced or the same state is decided twice.
- A requested jump is not an observed jump. `PMF_JUMP_HELD` set on the next
  frame is the acceptance signal.
- A hold that owns a frame must zero only the motion fields. A fully zeroed
  `usercmd_t` sets the command angles to zero, which snaps the view to
  `delta_angles`. Those differ by world, so identical worlds diverged
  (2026-09-22).

## Measured facts (from retained worlds)

- **Learned edges transfer, and committing them is worse than waiting.**
  Offering every envelope-admitted strafe edge at a frame where the straight
  search admitted nothing gets 113 of 1,571 offers past every gate and 45
  committed, so the library does apply
  ([the rescue arm](records/2026-09-21-library-rescue.md)). It also loses
  1,988.7 ms at declared weight, because two committed library flights a
  world cost 3,425 ms at class 125 and 2,000 at class 150 and leave the bot
  waiting **more** afterwards. Waiting at those frames was the cheaper
  option, and an admitted single hop is most often refused later for
  `corridor` or `slower_route` - a flight with no validated continuation.
- **Rows 2 and 3 moved together in that screen**, class by class and in both
  directions. When a change moves them apart, suspect selection; when it
  moves them together, it is a mechanism change - which says nothing about
  the sign.
- **The library's envelope is not what keeps the bot standing.** At the 60
  waiting frames of the sixteen successor-aim confirmation worlds, the seeded
  library admits a strafe edge at 34 of them, no waiting state is unvisited or
  off the graph, and every uncovered one fails exactly one axis - the heading
  cone 18 times, fifteen of those by more than 110 degrees
  ([plan coverage](records/2026-09-21-plan-coverage.md)). The committed
  control is covered at 66 of 107, no less often, so **coverage does not
  distinguish a frame where the bot waited from one where it launched** and is
  not by itself a speed opportunity. Coverage is also entirely tap-dependent:
  clear the landing tap bit and it is 0 of 60.
- **The bot's heading is not retained at frame resolution.** `[MOMENTUM]`
  carries speed and ground, `[MOVE]` carries position only at events, and the
  play CSV samples at 250 ms. Any frame-level state analysis must reconstruct
  the heading and report the reconstruction's own error; two reconstructions
  of the same landings differ by a 7.7-degree median and 18.9-degree worst.
- **One cvar's effect ranges from -3,150 to +3,025 ms across four start
  classes.** `bot_moveStatePrefer=0` gains 3,150 ms at class 75, loses 3,025
  at class 125, 1,775 at class 100 and 150 at class 150, three repetitions an
  arm, one distinct completion per arm
  ([the full grid](records/2026-09-20-preference-removal-refuted.md)). Two
  things follow. A screen that does not control its start class returns
  whichever verdict its worlds draw, so pin with `--corner-class`. And **two
  classes are not a grid**: 75 and 150 are the extremes of the completion
  range and agreed on a direction that the two classes between them reverse,
  carrying 44 of the 93 weighted starts. Compute the declared-weight mean over
  all four before reading a direction from any subset.
- **Selection quality is class-specific, not a property of the ranking.**
  Forcing every admitted candidate at a commit shows the ranking taking the
  **slowest** of four at start class 75 and the **fastest** of three, four and
  two at class 150, where it is the score maximum every time
  ([frames](records/2026-09-20-selection-across-frames.md),
  [second class](records/2026-09-20-selection-second-start-class.md)). A
  scoring change justified on one class has to be shown not to regress a class
  where the incumbent is already optimal.
- **The score is not always what executes.** At class-75 commits 2, 4 and 5 a
  planned `kind=state` candidate committed although an admitted straight
  candidate scored higher and completed 1,150 to 1,725 ms sooner. Read `kind`,
  `state_candidates` and `state_admitted` on the commit line before attributing
  a decision to the score.

- Recorded strafe takeoffs on the Kejim lanes are mostly 850 to 1173 u/s.
  A tap from a 40 Hz cadence leaves at about 309 u/s. A rejoin near rest
  into a fast recorded launch lands short into the platform face.
- Riding excess speed airborne on a straight preserves it; grounded frames
  lose it. The archive's "six units per grounded frame" is not the engine's
  rule, which removes 15 percent of speed above stop speed per 25 ms frame
  (see PHYSICS); the original measurement's conditions were not retained.
  The conclusion stands, the number does not.
- Long level or uphill flights need a levitation reserve; a 12-Force flight
  exhausted early and missed where 20 reached.
- The exact-12 return cases seed the runner with 12 Force and a 13.3 s
  Speed window; regen resumes only after Speed expires and reaches 25 to 40
  by capture against the 50 a Speed costs, so the last 4.5 to 5.5 s run at
  the 250 u/s ground cap. That is a measured constraint on the tail. The
  frozen scorecard retracted the stronger claim: no phase-by-phase budget at
  achievable speeds was computed, so whether 16 s is reachable is open. The
  rewrite keeps the constraint and answers the question with the planner's
  own lower bound.
- Two of four chasm deaths in the defended campaign were opponent knockback
  above the seal height. Not recoverable by movement.
- Identical eight-bot fixtures diverge run to run. Classify events by
  instrumentation, not by replay.
- A single seeded arm cannot resolve small changes; only factor-sized
  effects survive repetition.
- Standing still while a path prepares turned 800 u/s into 303 u/s in
  600 ms. Preparation must never own a frame with a zero command while the
  bot has momentum.
- In the archived lane-owner experiment, refusing a join because a distant
  launch was unaffordable halved the frames the lane owned; the approach could
  build resources/momentum before that launch. Do not reject from a stale
  distant-state assumption. This does not waive checking a proposed entry's
  connection and critical continuation from the state it will actually reach.
- A branch guide with one airborne sample fell the runner 1776 units. Ground
  lines must stand on prepared floor at every sample.
- A start class is tied to the fixture's release, not only to the phase of
  the bot's own first move. Standing a released bot still until the
  class-125 phase gave 19.2 to 19.35 s from every class, where a class-125
  release gives 16,375 ms. It was deterministic, and the cause is not
  located ([plan review checks](records/2026-09-22-plan-review-checks.md),
  2026-09-22).
- Seeded explorer tapes do not carry to another graph cache. Remapped to v3
  by fold identity they lose a straight entry at the fourth to sixth commit,
  because the planner installs another path. A tape is bound to the routes
  its graph plans, not only to its edge indices (same record).

## Navmesh (from code and the owner)

- The mesh is a Detour tile set loaded from `maps/<map>.navmesh`; nothing in
  the repo generates it. The converter's cell size, agent radius and climb
  height are not recorded anywhere here.
- Every consumer re-traced its answers: the supported adapter every 32
  units, local nav with a four-sample corridor, the corner compiler because
  Detour renders a bend as a short bevel, the pass-through because
  string-pulled corners cut the hull.
- Its lasting value is topology and the authored off-mesh pairs for wallrun,
  drop and basic jump. Pads and lifts are found by entity scan already.
- Owner: it was useful for walking and for plotting pads and wallruns, was
  ignored more and more as physics took over, and its detail is not trusted.
  Treat it as a hint, not the root.

## Design lessons (from the archive's own failures)

- Every owner-chain release became a new reason string and a new retry
  timer. Seven owners produced dozens. The rewrite has one event schema.
- Gates evaluated at the launch frame (resource, momentum, alignment) were
  repeatedly wrong about resources the next command would buy. Decide at
  plan time with projected resources.
- Recorded guides encode the level they were recorded at. A level-3 lane
  cannot be followed at level 2. Generate per wishspeed instead.
- Hand-declared connectors needed a hash-bound sidecar, entry windows, exit
  envelopes and instance bookkeeping to stay safe. Generated edges need
  none of that.
- Proximity-triggered jumps with a look-back window created stale latches
  and rejoin special cases. Trigger by position along the reference.
- Telemetry that names the owner rather than the maneuver made audits pair
  events by nesting. Name the edge.
- Documentation accumulated a dated record per patch. Keep one design, one
  plan, one lessons file, and dated evidence only under research folders.
- A pass that changes the bot's state at the commit must forecast against
  the state it commits to. The reroute pass judged each candidate's onward
  travel against the path the flight *leaves* and then installed a different
  one, so it priced a continuation nobody would fly; and it planned the
  route for a winner that a later preference could still replace. Build the
  installable thing after the last decision that can change it, roll the
  candidate out against it, and refuse it when the two disagree. (Found in
  review, 2026-09-19, after the arm had already been measured.)

## Owner decisions carried forward

- Defender-free dependable movement first; defended and body-contact
  scenarios are v2. (2026-09-14)
- No map-specific C, no hand-authored rescue coordinates, no corner
  exception lists. Transfer to other maps through automatic preparation.
- Idle time, unnecessary walking and getting stuck are the priorities.
  Walking for a real turn, landing or arrival is legitimate.
- Captures and deaths must not conceal bad movement through resets.
- The pressed carrier may fight (`bot_carrierMelee`); that is retained.
- Wall moves (2026-09-27): the sideways wall run is never needed and is not
  built. The wall climb (run up a wall) is wanted: on ctf4's lower routes it
  beats standing on the lift and waiting for it. Climb edges are graph-native
  (identical counts with and without the navmesh); every ctf4 case at class
  150 climbs once and rides no lift ([record](records/2026-09-27-navmesh-off.md)).
- Order after both gates (2026-09-27, revised after an outside review): wide map
  compliance on a frozen 14-map list including survival after the objective, a
  small demo pilot before linking more demos, then end-to-end milestones (a
  defended pickup and escape first; chase and base after a combat decision),
  and level 3 last. Capabilities are built for a milestone's case, not in
  breadth. The bot's situation model separates known, last seen and unknown;
  policy never reads server state directly. Behaviour per position is
  specified by what labelled human demos observably show, kept apart from
  inferred causes. See [PLAN](PLAN.md#roadmap-owner-2026-09-27).
- Review 2026-09-14 (second reader): sampled maneuvers do not compose across
  a whole entry band; validate the committed maneuver from the live state,
  extend the forecast before trusting Speed, keep unknown gaps unknown,
  strengthen the executor contract, and do not retire the old stack before a
  second map passes. All adopted; see ARCHITECTURE and PLAN.
- A path installed mid-run does not carry the state its edges were priced
  for. The state planner plans a route from a landing in the state measured
  there; by the time the bot walks to the first planned strafe edge it no
  longer has that state, the edge is stale, and the path is searched again.
  Measured 2026-09-18: `edge=strafe reason=stale` goes from 1-2 per
  `blue_out_L1_stand` run to 10-14 once routes are installed at a commit.
- Admitting a landing off the selected path helps from a fast mid-route
  state and costs from a standing start; one number for both hides that.
- `PathRemainingMs` prices a route at a flat 250 units/sec plus 250 ms an
  edge. It cannot see speed, so it must not rank candidates that differ in
  the speed they keep.
- Never connect a test to a real server; localhost only.
- Row 5 (search cost, one bot) is judged on **p99 inside the 25 ms frame**,
  not on every frame. The rare over-frame searches (spawn, after a shove) are
  a recorded cost the rollout work keeps shrinking. (2026-09-22)
- `bot_moveSearchBudget` 4 is the engine default and the production
  configuration; 0 turns it off. It is decision-identical and trades about
  25-30 percent more total movement CPU for half the p99. (2026-09-22)
- `bot_forecastReplay` 1 (exact rollout replay) is the default, and the
  32-bit engines are large-address-aware. (2026-09-22)

## Tools worth keeping

- `bot2_movebench.c` and `tools/strafe_play_smoke.py` as the harness.
- The RCON fixture helpers in `tools/` (replies span packets; collect).
- The momentum trace at 40 Hz; 4 Hz play telemetry hides sub-250 ms events.
- The corner-start fixture for exact pose and velocity starts.
- `tools/movement_plan_coverage.py` to ask whether the graph library already
  admits a retained state, and to recompute the game's logged `envelope=`.

- A landed plan that cannot travel `MOVE_ONWARD_DISTANCE` (128 units) from
  its landing mark never clears its onward stage, and the replan gate waits
  on a sequence in progress: the planner then never replans. Measured
  2026-09-18 as two stalls of about 175 seconds at one pocket. Any change
  that makes landings more adventurous must assume a landing can be a dead
  end, and the executor must be able to leave one.
- Avoiding a planned edge because it went stale can strand the bot: the chord
  it removes may be the only way out of the pocket the bot is standing in.
- A 12-case level-1 seeded arm moved 7,100 ms between two arms that ran the
  same code (2026-09-18: `bot_moveOnwardBound` on, never firing). Aggregates
  of that size from one repetition per arm mean nothing; only repeated cases
  with non-overlapping run sets, or categorical failures, decide anything.
- A pass that rolls a continuation out and then measures it from frame 0 will
  measure the candidate it was continuing, not the continuation: the tap pass
  read the candidate's own hop as its successor for as long as it existed
  (2026-09-18), and reported admissions at 261 units/sec that were really
  500. Any scan over a shared rollout must start at the frame the new
  behaviour starts at, and a telemetry field must be captured before the
  field it is derived from is overwritten.
- The search never runs while the bot is airborne (measured 2026-09-18: 323
  searches over twelve stand runs, none strictly inside a flight). A chain
  therefore cannot be continued across a plan boundary without paying ground
  time, and ground time at speed is friction.
- Judging a candidate by progress inside a horizon prefers the candidate that
  walks up to a better launch point over the one that keeps its momentum,
  because the speed handed to the next plan is not in the metric. When most
  of a route is flight, that metric is backwards.
- The three retained human runs are executable under their own bindings.
  Their native Pmove substep is fixed at 8 ms; each captured command has its
  own accepted duration. Corrected playback reproduces the controls within
  0.29-0.35 units (2026-09-19), with the recorded server-time outcomes retained.
  This establishes execution of these inputs, not independent discovery or
  arbitrary-state robustness.
- In the retained 25 ms server-frame configuration, playback commonly submits
  several commands per frame. Commands keep their recorded durations; neither
  one command per frame nor one nominal 8 ms duration per command is equivalent.
- An integer read through a float loses the bottom bits: a map checksum parsed
  as a float turned -1301924053 into -1301924096 and refused a tape against its
  own map (2026-09-18).
- A ranking term cannot promote a candidate that admission refused. The
  exit-speed term corrected a metric that was demonstrably wrong and changed
  almost nothing (2026-09-18), because the candidate it would have chosen was
  not in the set it ranks. Fix the gate before weighting the choice.
- A hold that ends a rollout may not be the one the pass sets. The tap
  continuation's budget did nothing until `straightStopTime`, set by
  `StraightCommand` at the first onward frame, was extended too.
- A search keeping its candidates in `static` storage must empty each slot
  completely, not clear the fields it happens to write. The straight search
  cleared the plan, the outcome and the variant and left the extension flags,
  so a slot that had carried a rerouted or tap-continued candidate handed the
  next one the extension pass's walk reference: 372, 200 and 396 dirty slots
  in three cases (2026-09-19). Shared storage that survives a client reset
  needs an audit, not an assumption.
- A mark taken from a recording is a mark on a *command*, not on a clock. The
  fixture runs about 3.1 commands per server frame, so a replay reading
  checkpoints by elapsed time measured a third of them against a position it
  had not reached yet - and reported 23.8 to 37.8 units of drift on replays
  that were actually within 0.35 (2026-09-19). Any cross-check between a
  recording and a replay has to be indexed by what was executed.
- Pmove runs the whole difference between `ps->commandTime` and
  `cmd.serverTime`, in steps of at most `pmove_msec`, and does not discard the
  remainder. A command's duration is that difference, clamped to one second -
  never a nominal step, and never inferred from the previous command for the
  first one, which is exactly where the inference has nothing to work with.
- A module that takes a client's AI must validate before it takes it, and must
  track what it holds separately from what it is playing. Arming held the bot
  and then refused, leaving `armed` false and nothing able to release it
  (2026-09-19); `tape.client` could not be the release target, because a
  refused arm never sets it.
- A regression on a shared engine needs a defect-control build to be worth
  anything: the repaired tree with the defect put back, so the pair differs
  only in the defect. Both arms of the extension-case regression passed their
  cases; only the audit counter told them apart.
- The fixture's flag runner drives its bot from its own frame hook, not
  through `customThink`, so idling the think does not stop that bot. A probe
  that tests ownership has to read ownership, not movement (2026-09-19).
