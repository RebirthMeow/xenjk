# HANDOFF archive: session blocks to 2026-09-30

History, not instructions. These are the session blocks moved out of [HANDOFF.md](../HANDOFF.md) on 2026-10-02:
every block dated 2026-09-30 or earlier, newest first, verbatim except that relative link targets were rewritten for
this file's location. Nothing here states current defaults or the next action; for those read the head of
[HANDOFF.md](../HANDOFF.md) and [PLAN.md](../PLAN.md).

# Update (2026-09-30, end of day): targeted fall fixture built, baseline not run

Committed: `410124e0` (the fall work). Uncommitted: the scenario fixture — `tools/defense_laps.py --scenario
FILE:NAME` (runner pose/velocity/carrying/Force through `bot_corner_start`, both defenders at posts with
`bot_defenderPostVelocity` / `bot_chaserPostVelocity`, new in `bot2_defender.c`), and
`tools/fixtures/movement/defender/scenarios.json` (49 moments 1 s before recorded falls: 33 base D holds, 16 chase D
own splash; generator `.codex-research/falls3-20260930-a/make_scenarios.py`). Build `falls3-build-20260930-t`.
Runner: `.codex-research/scen-20260930-a/runscen.py <build> <tag> [reps] [game_s]`. A smoke run placed all three
correctly. The first baseline died at 62 of 98 worlds when D: filled up (worlds were also ~4 min each); D: was then
cleaned of world graph snapshots, demos, binary copies and intermediate builds (~15 GB free). Before the next
baseline: make the harness skip copying `bot2-graph`, `demos` and binaries, and check free space.

Disk clean-up the same day (owner): D: had filled up. Removed per-world graph snapshots, demos and binary copies from
the falls2/falls3/scen campaigns, intermediate builds `falls3-build-20260930-a..r`, the archived compiler
intermediates (`.codex-archive/20260930T173729Z-research-intermediates`, `.obj` only), all generated videos, and the
whole `.codex-research/video-kejim-20260923-a` folder (cited by records/2026-09-23-flight-mode-2.md; gone). Logs,
play samples and game logs are kept. D: 58.8 GB free after.

# Next session: defender falls, after the guest flight trace (2026-09-30)

Read [the flight trace section](2026-09-30-defender-falls-and-route.md#chase-ds-flights-the-guest-flight-trace-2026-09-30)
and the [owner decisions round](2026-09-30-defender-falls-and-route.md#owner-decisions-round-2026-09-30).
Uncommitted, nothing pushed; source = `falls3-build-20260930-s`. Runner floor identical to HEAD; `bot2_move.c`
differs from HEAD only in guest-only paths (lip guard, walk brake, trace, `Move_GroundedThisFrame`,
`Move_GuestRawCommand`, `GUEST_HOP`).

**Rates (full configuration, ambush on):** chase D about 0.7 falls a world, base D about 0.55-0.6 (target 0.5 each),
from 1.1 / 0.8 at the start of the day's full-configuration work. Captures 3.7-4.1, route stops 3-3.6 (target 2 met).

**Next.**
1. Chase D's own splash falls (7 of 11 on build s): the knock safety check; measure its misses first.
2. Base D's holds that do not stop (6 of 10 on build s, 17 of 43 pooled): read them with `falltax.py`; the guest
   walk brake applies to walks, not to `DeclineHeld`.
3. The trace (`bot_moveGuestTrace 1`) for what is left of the flights: body contacts in the air and executor
   decisions that differ from their rollout.
Judge by pooled worlds (16+) and `falltax.py` classes; single 8-world arms swing 3-11 falls.

# Earlier: base D's own-splash falls, then the route (2026-09-30, after the second attempt)

The second attempt is the "Second attempt" section of
[defender falls and route](2026-09-30-defender-falls-and-route.md#second-attempt-2026-09-30-later-session).
Read it before the older sections below; it supersedes their problem-1 plan.

**State at stop.** Uncommitted on `movement/level1-gate`, nothing pushed. On top of the first attempt's changes:
`bot2_defender.c` has the brace (`bot_defenderSelfSplash`, default 1), the backpedal clamp
(`bot_defenderBackpedal`, default 0 = clamp on), `bot_baseRearmChase` (default 0) and the diagnostics
(`CARRY_SAMPLE weap=`, `THROW replaced= self_air=`, `FIRE self_air=`). `bot2_move.c` has the guest-only
`GUEST_HOP` print. The source equals `falls3-build-20260930-c`; build d's knock-safety hold was reverted.
Runner floor on build c identical (Kejim classes 150 and 50); the level-1 gate was not run.

**Measured.**
- Holding close splash shots tripled captures (3.3 to 8.6 per ten minutes). Owner: keep the damage,
  counter-steer the push, no repeater primary (the saber reflects bolts). The brace keeps captures unchanged.
- The backpedal clamp fixed chase D's slow launches (41% to 7% under 0.9 of the rollout).
- Falls over 16 worlds each: controls chase D 18 / base D 13; brace + clamp chase D 9 / base D 10.
  Chase D is near the 0.5-a-world target, base D is not.
- Throws are not a fall cause (closed). Base D's route blocker is `noweap`; a rearm during the chase did not help.

**Next.**
1. Base D's own-splash falls: at 50-75 u the knock lifts it before the brace can act. Read those falls from the
   press (`fallcause.py`, `BRACE`, `knock dv=`): velocity and floor at the press, the knock's direction and size.
   Calibrate a knock model on the measured `knock dv=` lines before gating anything (build d's guess failed).
   Candidates: steer against the knock in the air, or no close splash shot while running at a DROP-cell lip.
   Judge on captures and kill% as well as falls.
2. Route: base D's `noweap` during carries. Why it has no weapon then (deaths, the rearm radius, chase D taking
   the items) comes before another rearm lever.

**Owner decisions pending:** whether L4 (`bot_chaserRearmChase`) becomes a default; the ambush posts; whether
the airborne hold and the goal latch stay. Commit when the owner says so.

Comparing an arm: `.codex-research/falls3-20260930-a/run3.sh` is the template (the `lip` configuration, eight
worlds, four a wave); `compare.py <tags>`, `fallcause.py <globs>`, `guesthop.py <globs>`. Eight-world arms do not
resolve falls: `def` met the target and its replicate did not. Use 16.

# Earlier: the same three defender problems, second attempt plan (owner, 2026-09-30)

The owner's three problems stand: (1) chase D falls, (2) kills only at the
stand, (3) the runner and the dropped flag. The first attempt is in
[defender falls and route](2026-09-30-defender-falls-and-route.md)
(the diagnosis, then its "Results" section: read both, the results correct
the diagnosis). Problem 3 is closed as an analysis artifact. Problems 1 and 2
are open with a changed picture, below. Resume from here; do not re-run the
first attempt's arms.

**State at stop.** Uncommitted on `movement/level1-gate`, nothing pushed:
`codemp/game/bot2_defender.c` (post snapping, goal latch, carry diagnostic,
L1/L3/L4 levers), `codemp/game/bot2_move.c` (airborne decline hold, guest lip
guard), `tools/fixtures/movement/defender/posts.json` (ambush posts dropped
from z 1887 to the walkway floor 1808), the gaps record's two correction
notes, the fixed `flags.py` in `ret-20260930-a` and `goal-20260930-a`. Last
builds `falls2ctl-build-20260930-a` (HEAD control), `falls2-build-20260930-b`
(everything); campaign `.codex-research/falls2-20260930-a/`. Runner floor
identical on both builds (Kejim classes 150 and 50, parity 0); the level-1
gate was not run.

**The corrected picture (all measured, 8 worlds an arm):**
- **Control.** The stand goal's recorded 1.9 captures per ten minutes was a
  lucky eight; the same build re-run gives 4.0 and 70% stops, HEAD 4.5 and
  67% (`gctl`, `octl`). Compare against these 16 worlds.
- **Falls.** Chase D's are mostly committed hops launched from the walkway
  beside its post whose rollout landed on DROP-flagged lip cells; the live
  flight misses the lip (`fallflight.py`). Base D's begin in a `floor_missing`
  walk brake or a hold taken already past the lip. Not causes: the aim, the
  chase post (it has a cell), base D's throws (push and pull skip teammates).
  The off-graph goals were the two ambush posts recorded 79 u above the floor;
  corrected, chase D walks 1,700 u forward to hold them and its falls double,
  so the ambush is off in the best arms.
- **What helped and what did not.** The guest lip guard (`bot_moveGuestLip`,
  on) halves chase D's falls with the ambush on (25 → 13), inside the spread
  without it (11 → 9). The airborne hold (`bot_moveHoldAir`) and the goal
  latch (`bot_defenderGoalLatchMs`) changed nothing. Best arm: about 1.1
  falls a world for chase D and 1.0 for base D, against the 0.5 target.
- **The route.** Escaped carriers were in chase D's sight 87% and range 72%
  of the time, closest approach 34 u, and it fired in 6 of 41 carries: it
  had no weapon. A respawn keeps the spawn loadout and rearming waited for
  the chase to end. `bot_chaserRearmChase 1` (L4, off): captures 4.2 → 2.5,
  stops 66% → 79%. Route stops still 1.5 per ten minutes against 2; the gain
  is at the stand. L1 (route-ahead goal) no effect; L3 (pull then close)
  fired 9 times in 4 worlds, unjudged.

**Owner decisions pending** (ask before building on them):
- `bot_chaserRearmChase` as a default (the one measured win).
- Ambush posts in the laps configuration: now reachable, they cost falls.
- Keep or withdraw `bot_moveHoldAir` and the goal latch: no benefit, and the
  hold touches the runner (the level-1 gate is owed if it stays).

**Problem 1, falls: the next attempt.** Target 0.5 a world per defender
(`lip` arm: chase 9, base 8 in 8 worlds). Three sources remain, in order of
size; take them one at a time and measure each alone.
1. **Chase D's hop landings near lips** (3-5 an arm the guard misses). The
   guest's hops are admitted by rollout with no parity check. First move:
   log, for every guest hop, the rollout's predicted landing and the live
   landing (`GUEST_LAND` with the miss distance and the landing cell's
   flags), over the `lip` configuration; read the misses before the falls.
   Then either widen `LethalLip` to one-hop landings and DROP cells with a
   refused side within a hop length, or refuse any guest hop whose landing
   cell is DROP-flagged when the miss distribution says the flight cannot
   be trusted there. The runner's floor must stay identical (guests only).
2. **Base D's walk off the walkway steps** (`floor_missing` brake, then a
   hold past the lip). Read its falls from the brake's first frame
   (`falls2_long.txt` style, `.codex-research/falls-20260930-a/falls2.py`):
   where the walk was heading and why the brake came late. The candidates
   are the walk's brake distance at its speed (a brake that starts 30 u
   from the lip at 385 u/s is late), and the block goal's cell choice on the
   walkway. Fix in `bot2_move.c` only if the runner's floor and gate stay
   identical; otherwise in base D's goal choice (`BlockGoal`, posts).
3. **Chase D's own throw or shot inside a committed flight** (unverified,
   4-5 falls in some arms). Check whether `Throw` overwriting `generic_cmd`
   drops a Speed purchase that frame, and whether a throw's hand extend
   changes Pmove for the thrower; log `THROW` with `in_flight=1` and count
   the falls within 2 s. If it holds, no lethal throw or shot while a
   sequence is between launch and landing (`DefenderMove` knows).
Acceptance per step: falls for the defender concerned, 8 worlds, against
`lip`; captures not above the control range.

**Problem 2, the route: the next attempt.** Target at least 2 route stops
per ten minutes with captures at or under the control. With the rearm
decision taken, the diagnostic (`bot_defenderCarryLog 1`, `carry.py`) is
the tool: re-read the escaped carries under L4 and rank the `PRESHOT`
reasons again. Expected next blockers, in order: `range` for base D and
`weapon` still (the rearm walk itself), then the engage delay at 500-700 u/s.
Levers not yet judged: L3 needs a configuration where the pull fires (the
pull's line of sight and 300-700 u band; count `THROW reason=carrier`
first), and a base D rearm during a chase mirroring L4. Do not retry L1.

**Problem 3, closed.** `flags.py` requires a respawn before a re-take; 0
pit re-takes over 16 worlds. No runner change; the runner's dropped-flag
reachability (`ReachableDrop`) stands. Reopen only with a re-take at z under
100 that follows a non-carrying sample.

## Five resolution suggestions per problem (2026-09-30)

These are proposed next steps, not implemented fixes or new measured results.
They use the corrected Results in the linked record; the owner decisions and
acceptance criteria above still apply. Try one causal change at a time rather
than combining all five suggestions into an arm.

### Problem 1: defender falls

1. **Measure and repair guest landing divergence first.** Pair every predicted
   hop landing with its actual touchdown or fall, including launch state,
   commands, cell flags and miss distance. Find the first differing frame in
   failed flights; repair a demonstrated forecast/execution mismatch before
   compensating for it with a larger safety margin. Include successful flights
   so the diagnostic does not mistake ordinary error for the cause of a fall.
2. **Cover one-hop lip landings with the guest guard.** If the measured misses
   remain large enough to leave the available floor, extend `LethalLip` to the
   uncovered one-hop cases. Judge clearance against the player footprint and
   observed error near refused sides; use blanket DROP-cell refusal only as a
   bounded diagnostic, since DROP alone does not prove an unsafe landing.
   Require fewer chase falls without stranding the defender or changing runners.
3. **Brake before base D exhausts its stopping distance.** Reconstruct speed,
   floor support and commands before each `floor_missing` event. Use native
   movement rollouts to establish where braking must begin at those states,
   then test earlier braking on the named failures. A shared movement fix must
   preserve the runner floor and level-1 gate; a hold after leaving the lip
   cannot establish that the approach was safe.
4. **Choose base block goals with a safe approach and stopping area.** For
   `BlockGoal`, test reachable standing cells with enough support to arrive and
   stop from the live state, instead of treating the nearest valid cell as
   sufficient. Prefer this defender-local fix if the falls follow goal choice.
   For ambush posts, separately recommend keeping them out of the laps setup
   until their approaches meet the fall target, subject to the owner decision.
5. **Isolate the defender's own attack/movement conflict.** Record the movement
   command before and after `Throw`, Speed activation, hand extend and flight
   phase. If a throw replaces a required Speed command, preserve the movement
   purchase and defer that attack; if another attack effect causes divergence,
   test a narrowly scoped flight restriction. Require a reproduced causal
   difference before suppressing attacks; nearby throws alone are not proof.

### Problem 2: kills at the stand but too few route stops

1. **Use the measured chase-rearm win as the candidate baseline.** Recommend
   enabling L4 after the pending owner decision, then analyse its retained
   escaped carries before making another change. Report stand stops separately
   from route stops: 2.5 captures and 79% stops support rearming, but 1.5 route
   stops still misses the route target of 2 per ten minutes.
2. **Give base D a reachable rearm choice during pursuit.** Test a base-specific
   equivalent of L4 when it lacks usable ammunition or a weapon. Validate an
   item's reachable pickup approach, recheck availability when chase D takes it,
   and choose another reachable item if necessary. Track `to=-1`, time unarmed
   and pursuit time lost; the recorded rise to 105 off-graph plans per world
   makes blindly copying the chase policy insufficient.
3. **Separate the remaining weapon blockers before fixing them.** Split L4's
   `PRESHOT weapon` samples into no weapon/ammo, raising/dropping and hand extend.
   For each escaped carry, measure how much visible, in-range time each consumes.
   Fix the dominant cause with an appropriate change, such as a shorter reachable
   rearm detour or avoiding repeated weapon switches, rather than treating every
   weapon refusal as a missing pickup.
4. **Test engagement delay only on armed, visible opportunities.** Measure how
   often the 1,200 ms continuous-sight delay outlasts an otherwise usable firing
   window. If it does, trial a shorter delay or bounded retention of target
   recognition across brief occlusion, requiring current visibility to fire.
   Use the defender's observed target state; judge additional route stops and
   captures, not just increased shot count. Do not retry the ineffective L1 arm.
5. **Qualify pull-then-close where it actually activates.** Use a named case
   with visible carriers in the 300-700 u pull band and compare L3 on/off.
   Count eligible opportunities, pulls, hits, safe closes and subsequent shots;
   nine activations in four worlds did not establish an effect. Only a useful
   causal result should advance to eight-world laps, with at least 2 route
   stops per ten minutes, captures at or below control and no fall regression.

### Problem 3: apparent dropped-flag re-takes in the pit (closed)

These suggestions protect the corrected analysis; they do not reopen a runner
defect or justify changing `ReachableDrop` without new evidence.

1. **Make a re-take an explicit event transition.** Keep the corrected rule:
   carrier death, then a non-carrying sample, then carrying again. Associate the
   sequence with the same player and flag, and use respawn events where available
   to corroborate it. A dying carrier retaining its flag bit must remain part of
   the original carry, not become a new pickup.
2. **Add focused analyser regression cases.** Cover the original false positive,
   a genuine re-take after respawn, a legitimate ledge pickup, a flag return and
   a truncated log. Assert event counts and pickup classification so future
   script changes cannot silently restore the pit-re-take claim.
3. **Retain an evidence trail for every claimed re-take.** Report death time,
   intervening non-carrying sample, new carrying time, origin and source log
   offsets. Cross-check pickup/return events where available; classify gaps as
   unknown instead of inferring a pickup from later pit-floor coordinates.
4. **Keep the corrected analyser reproducible.** Consider moving the reusable
   logic from the two campaign-local `flags.py` copies into a versioned analysis
   tool, retaining both originals and their evidence. Check it against the
   retained 16 worlds and the corrected per-arm counts; this needs no new native
   campaign and avoids the copies drifting apart.
5. **Use a strict evidence trigger for any future runner fix.** Reopen only on
   a new carrying transition below z 100 after a non-carrying sample, as required
   above. Then inspect the dropped entity, graph path and live pickup sequence
   in a named reproduction before changing `ReachableDrop`. Preserve legitimate
   ledge retrievals as controls; until that evidence exists, keep the closure.

**Comparing an arm.**
- `tools/defense_laps.py`, the gaps ctl configuration exactly as
  `.codex-research/falls2-20260930-a/run.sh` runs it (`bot_chaserGoal 1`
  passed; `--chaser-ambush` only if the owner keeps it), four worlds a wave,
  **eight worlds an arm**: four-world arms did not resolve anything here.
- Hash each world's `play-game.log` and check `live/jampgamex86_crash.txt`.
- `defense_laps.py` reads `posts.json` from the working tree; the release's
  `POST_SNAP` prints go to a discarded rcon reply, so read the `to=-1` counts.
- Scripts in `.codex-research/falls2-20260930-a/`: `compare.py <tags>`,
  `carry.py`, `fallflight.py`, `armed_at_carry.py`; fall context in
  `.codex-research/falls-20260930-a/`.
- Fresh unique build per arm; no source edits, comments included, while a
  campaign collects.

**Owner-deferred:** a video of the defaults (recipe in
`.codex-research/human-20260930-a/video/`).

# Previous: roadmap steps 1 and 2 (owner, 2026-09-27)

The owner set a new order, revised the same day after an outside review; it
is written in [PLAN, Roadmap](../PLAN.md#roadmap-owner-2026-09-27): (1) wide map
compliance on a frozen 14-map list, with survival after the objective and
the navmesh question, (2) a demo position pilot, (3) milestone A - take a
defended flag and escape usefully, built end to end, (4) chase and base
milestones after the combat decision, (5) level 3 last.

**State at stop:** everything below is committed in `5fa681f2` on
`movement/level1-gate` (strafe skill 90, bots-only fixed step, threat
reaction, the play fixes, `tools/play_vs_runner.py`). Both gates met on
`gate-20260926-c`. Not yet exercised in play: the tightened `StepOffCell`
(standing, reachable cells only).
Review fixes on top (uncommitted, 2026-09-27, [record](2026-09-26-play-env-and-threats.md#review-follow-up-2026-09-27)):
fixed step synced in every bot mode, human route metadata, `human_rush_study.py` cache ids,
per-label bot rows and empty reports; build `reviewfix-build-20260927-a`, Kejim class 150
identical to the committed build.

**Navmesh (done first, owner 2026-09-27, [record](2026-09-27-navmesh-off.md)):**
the gates used it - it is the graph's only source of pad, drop and jump
edges. `bot_navmeshOff 1` (build `navoff-build-20260927-a`):
Kejim identical, ctf4 18/18 (+22.1 s), Dantooine v3 and Metalorn v2 12/18
each (every stand case stalls on an island). **Native edges
([record](2026-09-27-navmesh-off.md#native-pads-drops-and-jumps-same-day)):**
`bot_moveNativeEdges 1` (test, off) with the mesh off: 78/78, -32.2 s net
over the four maps (Metalorn +2.6 s). Native pads help Dantooine (-14.4 s
vs mesh) and hurt Metalorn (+13.5 s) because the planner prices walking at
wishspeed while the runner strafes at 550-700 u/s. Pricing open straights
by the ideal strafe law was tried and refuted (Kejim +17 to +23 s; reverted);
the walk is priced at about double its measured time, partly the clearance
penalty.
**Re-scoped (owner, 2026-09-27) and done
([record](2026-09-27-navmesh-off.md#native-edges-by-default-pads-at-the-meshs-price-same-day-owner)):**
native drops and jumps already met the record's acceptance; the pad loss was
a currency mismatch the native pad created, not a reason to rebuild the cost
model. Link-price calibration is **parked until milestone A**. Build
`native-build-20260927-k` (uncommitted): `bot_moveNativeEdges` **default on**
(no mesh hints imported), pads priced by the mesh's formula
(`CandidateCostMs`: length at 250 u/s plus 2,000 ms), dropped-flag
reachability by graph path, and an unplanned pad (a walk thrown by a trigger
it did not plan) flown to a native pad landing (`PadThrownLanding`). The mesh
load stays the DLL default for older Bot2 states; `play_vs_runner.py` sets
`bot_navmeshOff 1`. Mesh not loaded, `--linger 4`: **78/78, 0 deaths after
the objective**, -31.4 s net against the mesh arm (Metalorn +2.6 s). Pads at
this price are never planned in these cases, as with the mesh. Without the
unplanned-pad landing Dantooine lost 2 carriers into the pit after the
pickup. Loaded graph caches keep their edges, so the Kejim seed and library
still bind.
**Eight new maps (2026-09-27, [record](2026-09-27-eight-maps.md)):**
fixtures for talay, illimiran_v2, telti, nelvaan, talravin, manaan,
kharzet_v3, kothis_v5 (`tools/fixtures/movement/<map>/cases.json`, engine
heights equal the offline trace). Build `k`, classes 150 and 50 without a
mesh and 150 with one: **828 of 828 trips complete, zero deaths on the trip
or after the objective**, parity 0 on seven maps. Six maps clean. **Talravin
and Talay are slow**: native drop discovery offers dozens of parallel drops
the drop search cannot fly (72-85 and 31 failed drops a case, 0-1 elsewhere);
the mesh's drops on the same build are 9.3 and 2.1 s a case faster.
Talravin's parity mismatches all fall after the trip, at 10 Force
(`fd.forcePower`, a predicted 5-point spend the game does not make).
A discovery-time arc check on drops was tried and changed nothing (the
failing drops fly as arcs; the executor's drop search refuses them),
reverted ([record](2026-09-27-eight-maps.md#drops-checked-by-a-flown-arc-no-effect-same-day-reverted)).
**Prepared drop set works** ([record](2026-09-27-eight-maps.md#a-prepared-drop-set-from-measured-outcomes-same-day-owner)):
`tools/movement_graph_prepare.py` writes a seed of the built graph minus drops
that failed and were never flown in earlier worlds; out of sample (class 100)
Talravin -6.7 s a case, Talay -3.2 s, none slower, failed drops 81 to 9 and
29 to 1. **Materials and water no longer refused** (owner: the landing effect is
visual; pits in water are refused by their `trigger_hurt`;
[record](2026-09-27-eight-maps.md#materials-and-water-no-longer-refused-same-day-owner)):
build `native-build-20260927-m`. Talravin -3.9 s a case unseeded and every
`fd.forcePower` mismatch gone; with the pruned seed Talravin is about -9.5 s
a case against where it started (failed drops 81 to 4); the other eleven maps
are identical to build `k`, parity 0 everywhere. Water is not exercised by any
fixture. Sources = build `m` (uncommitted, with `tools/movement_graph_prepare.py`).
**Map preparation (owner):** prepared seeds for every map, pruning every
candidate type (pads, drops, jumps, climbs) that failed and never succeeded
([record](2026-09-27-eight-maps.md#map-preparation-prepared-seeds-for-every-map-same-day-owner),
recipe in [PLAN](../PLAN.md#roadmap-owner-2026-09-27)); class 100 out of sample: Talravin -5.7 s,
Talay -2.3 s a case, every other map identical, 22 arms clean.
`play_vs_runner.py` stages them. Water case dropped (owner).
**Next:** roadmap step 2 (demo position pilot) and milestone A, or commit
this work first (owner). Also open from the navmesh step: the planner walks across pad triggers it
does not mean to take (safe now, not avoided); `WalkStep` off the planned
path steers at a mesh waypoint when one is loaded and declines `no_path`
without one (Kejim play: 2 `no_path` in one session; measure off-path frames
under knocks before replacing it); dropped-flag reachability not yet
exercised natively.

**Start with, in parallel:**

- **Step 1.** Moving fixture worlds' writable files to the C: SSD (as
  `play_vs_runner.py` did) makes the campaigns cheaper; do it first. Then
  author the case shape for the eight new maps (talay, illimiran_v2, telti,
  nelvaan, talravin, manaan, kharzet_v3, kothis_v5) as for
  Taloraan/Carratos/Metalorn ([record](2026-09-25-new-maps.md)) and
  run level-1 and level-2 floors with and without each map's navmesh, all
  with `--linger` for survival after the objective; the six maps with
  fixtures get the `--linger` and no-navmesh arms too. Record where the
  graph alone fails; that list decides whether a navmesh generator is
  rebuilt. The list is frozen; additions need the owner.
- **Step 2.** Offline, no native runs: pick about 20 matches per position
  (offense, chase, base) on 2-3 maps from the list, extract them with a
  sibling of `tools/human_rush_study.py`, and report what the snapshots can
  and cannot answer (zones, transitions, Force use with its visible
  context). Keep observed facts apart from inferred causes; latency only
  where the trigger is in the POV's snapshots. Link more of the 2,726
  unlinked demos only where the pilot shows a question is answerable and
  short of data. Also locate the owner's written position descriptions
  (earlier session transcripts) and record them next to the numbers.

**Fixed now for milestone A:** the bot's situation model separates known,
last seen (with age) and unknown; policy never reads server state directly.
Scoring joins survival and flag progress. Force priorities come from named
fixture situations, not a general allocator first.

**Open owner decisions:** a minimal combat layer before the chase and base
milestones.

# Previous: in-game test findings (2026-09-26; lag fixed, see "State at stop")

Owner played CTF against the runner on Kejim with `tools/play_vs_runner.py` (new: private
localhost server, production settings, runner on the other team; sessions
`.codex-research/play-vs-runner-20260926T0502*`, `...T0509*`, `...T0514*`). Uncommitted fixes on
build `playfix-build-20260926-b` (Kejim class-150 fixture 24/24 identical to the pre-fix arm):

- **Stranded off the graph (fixed):** fast post-pickup/capture flights land on props 56 units above
  both Kejim stands; no cell, `no_path from=-1`, the decline hold braked forever (5+ minutes).
  `DeclineHeld` now walks off to the nearest safe cell below (`StepOffCell`); seen working in play
  (off in 0.4 s). Open: the arrival check could avoid off-graph landings in the first place.
  Audit follow-up (after `playfix-build-20260926-b`, not yet run natively): `StepOffCell` now takes
  only cells that stand now (`Graph_CellStands`, no departed lift) and that the player box can walk
  level to and fall onto (two box traces), so the floor under the prop (distance 0, a zero wish
  velocity) is no longer chosen. The launcher's `--stop` waits on a process handle instead of
  `os.kill(pid, 0)` (which can `TerminateProcess` on Windows mid graph save), and
  `--quiet-console` without `--no-client` is refused before launch.
- **Graph cache flushed at every leg boundary (fixed):** in play every pickup and capture rewrote the
  14 MB cache on the game thread, 0.5-2.8 s stalls (owner saw ping 999). Now only at shutdown unless
  `bot_moveGraphSaveLeg` is set.
- **Remaining hitches (fixed, 2026-09-26):** the server's small writes (the runner's synced play-log
  CSV, the server demo) blocked the game thread 0.8-1.5 s at a time on D:, a spinning HDD at ~75%
  busy (measured with temporary engine write timers: `FS_Write` of 56-195 bytes taking 821-1,548 ms;
  CPU idle during stalls). `tools/play_vs_runner.py` now puts the server and client writable files
  on the SSD (`--home`, default `%LOCALAPPDATA%/xenjk-play`) and copies them to
  `.codex-research/play-vs-runner-<stamp>/live` on `--stop`; server-only repro: 0 stalls in 3 min
  against 4-7 per 2-3 min on D:. `--no-client` runs the server and runner alone. `g_main.c` keeps a
  `[FRAME] slow` line (frame over 200 ms, bot dispatch split out). Build `playfix-build-20260926-b`;
  Kejim class-150 24/24 identical. Note: fixture worlds also write to D: (wall time only).
- Otherwise the runner played normally: captures, pickups, deaths to weapons and the pit, flag
  returns, respawns.
- **Performance in play** (sessions `...T0509*`, `...T0514*`; 9 captures in ~7 min): spawn-to-pickup
  median 11.6-12.7 s, pickup-to-capture 11.5-12.1 s; median 590-600 u/s, p90 ~910; under 50 u/s
  1.5-2.5% of alive time; no stationary stretch of 1 s in the fixed build; owner `move` throughout.
  Force: no resets; each life is ~40 s of Speed (100 Force, two 50-point buys, no regeneration while
  a power is active), so short lives look like permanent Speed. Weakness: 10 deaths in session 3,
  8 while carrying, several within 1-2 s of the pickup into a guarded stand - no threat awareness,
  same fastest line every time. The perch step-off fired 4 times in 4 minutes.

**State at stop (2026-09-26; since committed in `5fa681f2`):** uncommitted - step-off, leg-flush opt-in (`bot_moveGraphSaveLeg`),
`[FRAME] slow` in `g_main.c`, `tools/play_vs_runner.py` (new, untracked); build
`playfix-build-20260926-b`, Kejim class-150 24/24 identical. Owner has not yet said whether to commit.
**Strafe skill (2026-09-26, [record](2026-09-26-strafe-skill.md)):** `bot_moveStrafeSkill`
(percent, default 100) aims the held-side air strafe wide of perfect; 81 measured 74.7% against
89.7%, parity 0 mismatches. `play_vs_runner.py` now plays at 81; gates and fixtures stay at 100.
Level 1 slows 2-4 s on stands; with Force Speed lap times did not move.
**Play environment and threats (2026-09-26, [record](2026-09-26-play-env-and-threats.md)):**
`addrunner` no longer forces the server's `pmove_fixed` (it discarded every other command of a
250 fps client, force presses with it); Bot2 bots step fixed alone (`bot_pmoveFixed`,
`Bot2_PmoveFixed()`), runner times identical. Young missiles are unseen while a bot decides
(`bot_threatReactMs`, 850, owner). Strafe skill default 90 (DLL): level-2 gate met,
library re-explored at 90 (`.codex-research/skill90-chain-20260926-a`); gates re-taken
(`gate-20260926-c`): every row met (L1 median 13,088, L2 stand 8,225-9,775; cost p99 16.5-17.9 ms on
re-measurement, after an unexplained 21-30 ms window). sv_fps 30 / gravity 760: owner, not worth chasing.
**Next, owner's pick:** (1) carrier survival / threat awareness (design decision: the runner is
movement-only by design); (2) refuse off-graph landings in the post-pickup flight instead of
recovering; (3) tune strength toward an intermediate human (Speed usage); also move fixture worlds'
writable files to the C: SSD for faster campaigns.

# Previous: make verification cheap

**Done (2026-09-25, [record](2026-09-25-deferred-pin.md)): pin misses are gone.**
`bot_cornerDefer` lets the game start a pinned class at the first frame of it, and
`movement_bench.py --corner-class` uses it by default (`--corner-class-rcon` for the old path).
Build `deferpin-build-20260925-b` (uncommitted; sources = `arrivesafe-build-20260925-g` plus this
change): 95 of 95 shared pairs identical to `arrivesafe-20260925-f`, 108/108 passed (the rcon arm
lost 13 cases to four misses), world-seconds 1,800 to 861, 80-100 s an arm. Use this build for the
next campaigns. **Mirror twins** (same record): 1,913 of 1,916 retained pairs identical outside
Kejim's stands; `--skip-twins` runs 118 of 216 cases a class, smoke 18/18 identical in 41 s.
**Control reuse and path scoping** are written as the default comparison recipe in
[PLAN](../PLAN.md#cheap-comparisons-owner-order-2026-09-25). A two-arm, six-map, two-class comparison
is now about 10 min an arm (was 35-45). **The verification order is done.**

**Drop score item closed (2026-09-26, [record](2026-09-25-drop-plan.md#follow-up-2026-09-26-the-walk-priced-route-left-is-not-the-loss)):**
the Metalorn L1 stand loss is route divergence from the first drop on, not the walk-priced score;
pricing the route left by the state planner (`bot_moveDropRoute`, test) was a wash over 176 pairs
(+500 ms) at 45 ms a search, withdrawn.

**Gates re-taken (2026-09-26, [record](2026-09-26-gates-current-defaults.md)): every row of
both gates met** on `deferpin-build-20260925-b` with items, planned drops and arrival safety on;
level-1 row 3 met strictly; the library needed no re-explore. 32 minutes. Sources still
uncommitted. **Next: jump edges onto the planned search** (the last open candidate), or the owner's
new pick; open observation: Kejim level-1 respawn4 at class 50 is 925 ms slower than the previous
gate (route divergence at a reroute).

**First order of business (owner, 2026-09-25):** gates and checks take too long; find ways to make
review not the lion's share of the time. Starting facts, from the day's campaigns:

- **Half the world-time is thrown away on pin misses.** `dropplan-20260925-a`: 618 worlds kept
  (median 7.6 s wall each), 735 retired for missing `--corner-class`, 4,883 against 5,230
  world-seconds; `arrivesafe-20260925-e`: 782 kept, 846 missed, 6,010 against 5,938. A pinned
  start that landed first time would roughly halve every campaign.
- **An arm is sequential runs of 24-36 cases at four worlds**, about 3-5 min a map-class, so a
  two-arm, six-map, two-class comparison is 70-95 min and a single fix took three of them today.
- **Mirror twins:** blue_out/red_ret (and so on) run near-identical worlds, so about half the
  pairs repeat information.
- **Arms that did not change the path under test** were re-run anyway; reusing a control arm with a
  one-run identity check (done twice today, 27/27 and 32/32 identical) saved about 40 min each.
- Gates not re-taken since items, planned drops and arrival safety went default on (owner waived).

Candidates after that, owner's pick: the gates on the new defaults; the drop score's walk-priced
route; jump edges onto the planned search.

# Previous: level 2

**Start here (2026-09-25, night).** Branch `movement/level1-gate`; sources match build
`arrivesafe-build-20260925-g` (uncommitted). **Surviving the pickup**
([record](2026-09-25-arrive-safe.md)): the "runs on after completion" item was the carrier
dying after a pickup at speed. `bot_moveArriveSafe` (arrival rolled on past the flag, unsafe ones
refused; objective change held through a committed flight) and `bot_moveWalkKeepPath` (only an
on-path walk keeps speed), both **on by default (owner)**. Old-settings Dantooine: 4 of 7
carriers died, 0 of 8 with both; floors 328 pairs -8.0 s, no deaths either arm (Metalorn +3.3 s).
Bench `--linger` scores survival. **Next:** the drop score's walk-priced route (Metalorn L1 stand
-1.3 s), jump edges onto the planned search, or the gates on the new defaults - owner's pick.

**Earlier (2026-09-25, evening).** Sources then matched build
`dropplan-build-20260925-d` (b with `bot_moveDropPlan` defaulting on, owner). **Item pickups pass by default** (`bot_forecastItems`,
owner; gates not re-taken). **Planned drop search** `bot_moveDropPlan` (**default on**, owner;
[record](2026-09-25-drop-plan.md)): crossing point, heading and approach speed chosen from
the ledge and the floor below it, run-up steered onto the line, scored by landing plus route
left. 284 pinned pairs against items-on: 63 faster, 20 slower, **-69.5 s**, zero deaths, Kejim
unchanged, drop searches cheaper (34.6 against 51.8 ms). `bot_moveDropAim` withdrawn.
**Next (done above):** the "runs on after completion" item; open: the drop score prices the route left at walk speed (Metalorn L1 stand -1.3 s).

**Superseded the same day (drop aim).**
**Drop searches** ([record](2026-09-25-drop-aim.md)): full-speed drops failed because
the run-up ignores where the takeoff is (searches begin up to 169 units beside short ledges)
and because item pickups refuse rollouts (Carratos's stand drop: one `weapon_demp2`). New test
cvar `bot_moveDropAim` (off) adds a heading aimed at the takeoff; with the existing
`bot_forecastItems=1`, 279 pinned pairs on build `dropaim-build-20260925-a`: 114 faster, 47
slower, **-136.0 s**, zero deaths, Kejim identical, parity zero mismatches (Carratos, Taloraan).
Aim alone -11.0 s. **Next: owner decision on making both defaults**; if yes, re-run the gate
stand cells with the library (Kejim's search changes under items), then the "runs on after
completion" item.

**Earlier (2026-09-25).** Sources clean at that point.
Defaults: own flag at its stand does not refuse (`bot_forecastOwnFlag`), circle run-up on
(`bot_moveCircleRunup`; tuning `bot_moveCirclePush` 150 / `bot_moveCircleScale` 1), slow drops
on (`bot_moveDropSlow` 150, braked late `bot_moveDropBrake` 96), knock recovery on.
Current build `dropbrake-build-20260925-b` (11687b69, late-braked slow drops; Kejim identical to the
gate build). Gates were taken on `dropslow-build-20260924-b` (c126d485); production library is now
`.codex-research/slowdrop-chain-20260924-a/library/mp_ctf_kejim_-1301924053` (red stand,
levels 1 and 2, re-explored under the circle run-up and slow drops). **Gates re-taken**
([record](2026-09-25-gates-slowdrop.md)): level 2 every row met (stand 8,150 / 9,100 /
9,875 / 9,575 ms); level 1 rows 1, 2, 4, 5 met, row 3 fails strictly at class 75 on one world
of six (two grounded frames, no time cost); **ruled met (owner, 2026-09-25): both gates passed**.
**Next:** per the owner's scope, widen the libraries (blue side, respawns, Dantooine) - owner
asked what that buys before committing to it.

**New maps, 2026-09-25** ([record](2026-09-25-new-maps.md)): fixtures for Taloraan,
Carratos and Metalorn v2; 216/216, zero deaths. Late-braked slow drops adopted (-28.9 s over 264
pinned pairs; Kejim and Dantooine unchanged, so the gates stand). Open there: full-speed drop
candidates fail often on these maps; the bot runs on after completing with no path.

**Slow drops, 2026-09-24:** `bot_moveDropSlow` default 150 ([record](2026-09-24-drop-slow.md));
the review's six findings repaired ([record](2026-09-24-review-findings.md)).

**Circle run-up, 2026-09-24** ([record](2026-09-24-circle-runup.md)): `bot_moveCircleRunup`
now default on (owner) - level 2 -4.6 s summed over four classes, level 1 -1.9 s, library off
both arms; floors 36/36. The library re-explore is deferred (owner): the production library's
tapes predate the circle, and the gates have not been re-taken with it on.

**Own flag, 2026-09-24** ([record](2026-09-24-own-flag.md)): on by default (owner;
7049d7ae, with the ctf4 lift down-ride fix). Production library is now
`.codex-research/ownflag-chain-20260924-a/library/mp_ctf_kejim_-1301924053` (level 1 and
level 2). Level-2 stand 9,900 / 11,850 / 9,725 / 11,550 ms, level-1 11,650 / 14,750 /
14,475 / 13,950; floors 36/36 on Kejim, ctf4, Dantooine v3. **Gates re-taken**
([record](2026-09-24-gates-retake.md)): level 2 every row met; level 1 rows 1, 2, 4, 5
met, row 3 at class 100 ruled met in substance (owner): **both gates passed**. Follow-up
(same record): the slow cells are landings past the next chord's takeoff; two `bot_moveLandPast`
repairs tested and withdrawn. Was next (owner): the level-1 regression at
classes 100/125 against the build-d library, and the `walked` avoid in the level-2 stand.

**Level-2 library, 2026-09-24** ([record](2026-09-24-level2-library.md)):
library keys carry the spend (44770bad); combined library
`.codex-research/explorer-l2-library-20260924-a/library/mp_ctf_kejim_-1301924053`.
Level-2 stand 13,075 / 13,425 / 13,525 / 13,450 ms (gate 13,000 at every class,
not met); level 1 unchanged; floor 36/36. Next: the explorer's `--route-branch`
on the level-2 stand, then the search's offers at the ledge drops and the pillar.

Start on branch `movement/level1-gate` (18df38d4; `master` is still at
ca421809). The level-2 gate is declared ([PLAN](../../archive/movement-2026-10-05/PLAN.md#level-2-exit-gate-2026-09-24)):
every pinned stand class at or under 13,000 ms; floors on Kejim, Dantooine v3
and ctf4 first (met once). Next ([baseline record](2026-09-24-level2-baseline.md)):
under Speed the first hop comes 3.5 s after release (candidates `fell` on the
upper ledges); classes 100/125 take a hop forecast into a pillar at 139 u/s. The
Force-pool divergence is closed ([record](2026-09-24-force-pool-divergence.md):
start phase, not Force). Compare arms with the pinned four-at-a-time method
below.

**Update, 2026-09-24, the level-1 gate is passed** (owner ruling: row 3 met in
substance; [record](2026-09-24-level1-gate.md); branch
`movement/level1-gate`, committed). **Next: level 2.** Level 3 is set aside
until level 2 is good (owner). The "level 2 slower than level 1" item is
closed ([record](2026-09-24-level2-vs-level1.md)): on build `d`, level
2's median beats level 1 on 12 of 12 Kejim cases over three repetitions, and
dash2 runs 19.6-20.8 s against 29.0-29.3. Open for level 2: the Force-pool
divergence, `red_out_stand`'s wide level-2 tail (13,575 / 15,200 ms), and a
level-2 gate to declare. Kejim sequence joins stay open. Red against blue:
level 1 respawn4 is 2.5 s slower blue→red because the 32-unit graph grid falls
8 units differently on the two sides of the map, putting blue's route on the
lip ([record](2026-09-24-respawn4-mirror.md)); two walk-brake
variants were tested and withdrawn. `bot_moveDropPush` 16 (steering points
pushed off drops) is now the default: no death over 324 pinned floor worlds,
12 pairs faster, 2 slower, -12.1 s; the 2 s red/blue gap at class 150 remains. [Map symmetry](2026-09-24-map-symmetry.md):
the grid effect is not directional; v3 Dantooine (the map in play) is asymmetric,
so no global grid refinement (owner). Parked below level 2: a Kejim arm with the
grid offset 12 units, local refinement near drops, v3 judged per side.
**Fast comparison method:** pin every case at classes 50/100/150 and run
four worlds at a time (about 5 minutes per arm for the whole floor).
- **Repairs:** the bot refused the blue flag (`ArrivesAt` now uses the game's
  item pickup rule, exact on the rollout's last frame, margin on projections);
  a taken pre-search never disarmed the parity harness (`SearchCacheTake`).
- **Build `d`:** floor 36/36, zero mismatches, identity on six cells; stand
  13,625 / 13,975 / 12,300 / 14,075 ms (median 13,800); 72/72; p99 11.4/10.4 ms.
- **Row 3:** classes 100 and 150 wait a larger share than their controls. At
  150 the frames are equal (13 and 13); at 100 the library walks the last
  220 units to the flag at walk speed. The owner ruled it met in substance:
  the full-speed flag refusal is repaired, and walking onto the flag is fine.

**Update, 2026-09-24, latest: lifts built, measured and committed
(ca421809, on `master`); the level-1 gate is next, on branch
`movement/level1-gate`.**
- **Committed since build `k`:** wall climbs, the other maps and the build-`k`
  library (bd15d286, review fixes c02210ee); the knockback fixture
  (6f13a2cd, [record](2026-09-24-knockback-fixture.md)); knock
  recovery, on by default, and the forecast temp-entity leak fix (7e1df32f,
  [record](2026-09-24-knock-recovery.md)). Floors 36/36 on Kejim,
  ctf4 and Dantooine at defaults. Nothing is installed in the game.
- **Knockback scope (owner):** a push straight into a pit is not a case. With
  recovery on, no arm died after an off-course landing. The in-scope failures
  left are all the lift gap: ctf4's stalls on or beside a mover floor.
- **Lifts (owner, 2026-09-24), committed as ca421809**
  ([record](2026-09-24-lifts.md); build `lift-build-20260924-d`).
  - A resting lift is floor in the graph, walk and forecast until it can next
    move.
  - Lift edges ride up (board on the platform) and down (stand in the top
    call, board the arrived lift), run by one ride state in the executor.
  - The forecast refuses a lethal fall (owner: drops are fine unless they
    kill) and honours a call's re-arm.
  - Measured: ctf4 108/108 over two floors and the kicked arm (control
    103/108); climbs-off floor 36/36 on 36 rides; Dantooine and Kejim 36/36
    with Kejim's identity unchanged; times unchanged.
  - Open: the lethal-fall refusal is unmeasured in play; trains, plats and
    button lifts are not handled.
- **Open after lifts, in order:** the level-1 gate on one build; Kejim
  sequence joins (26 of 39 tap-now candidates touch the chasm's
  `trigger_hurt`); level 2 slower than level 1 on 10 of 12 floor cases and
  the Force-pool divergence.

**Update, 2026-09-23: flight mode 2 and build `k` defaults, committed
as a8bcd82a** ([record](2026-09-23-flight-mode-2.md)).
- **Landed, default on:** one strafe side a hop (`bot_moveFlightMode 2`),
  hop lines pinned to the search's aim points, run-ups and the walk keep
  speed, 6-cell wall clearance in both planners, the route check and an
  8-unit arrival margin.
- **Measured:** dash2 28.6 s (from 33.75), Kejim L1 stand 15.5-16.9 s over
  four classes (from 19.6-20.1), two floors 36/36. Key changes in flight
  under 1 a second, air efficiency 90-93%. The owner approved build `k`'s
  videos.
- **Same day, owner order "1 then 2"**
  ([record](2026-09-23-heading-sweep-k.md)): the heading sweep is
  safe on build `k` (four floors 36/36) but not faster, so it stays opt-in.
  Successor aim was dropped by accident from the build-`k` settings; it is
  mixed with it on. The seed library's first entry misses at three of four
  classes, so the tapes are being re-explored on build `k`
  (`.codex-research/explorer-k-chain-20260923-a.sh`, classes 125, 100, 150,
  75; the explorer resumes from each class's `progress.jsonl`).
- **Explorer result:** the build-`k` library
  (`.codex-research/explorer-k-library-20260923-a/library/`) reproduces
  13,625 / 14,125 / 13,400 / 14,075 ms at classes 75 / 100 / 125 / 150,
  selected by the bot, every entry hit; two library floors 36/36.
- **Production configuration (owner, 2026-09-23), superseding the seed-gate
  one below:** build `k` sources (a8bcd82a) and defaults; research seed
  `g1-98eb1e97c24a4e21`; library
  `.codex-research/explorer-k-library-20260923-a/library/mp_ctf_kejim_-1301924053`
  (index sha256 `b9ea9f32...`); harness off; `bot_moveExitSpeed=500
  bot_moveStatePrefer=1 bot_moveReroute=1`, successor aim and heading sweep
  off. The seed-gate tapes are retired. Nothing is installed in the game.
  Stand-run launcher: `.codex-research/production-arm-20260923.sh <build>
  <outdir> <class>`.
- **Other maps** ([record](2026-09-23-ctf4-dantooine.md), build
  `maps-build-20260923-l`, uncommitted): Dantooine v3 108/108 over three
  floors, L1 median 20.3 s; ctf4 0/36, its levels are joined by lifts the graph
  cannot model, and a no-path leg still reaches the legacy owner.
- **Wall climbs, 2026-09-24** ([record](2026-09-24-wall-moves.md),
  build `climb-build-20260924-f`, uncommitted): the run up a wall cannot be
  sustained at 25 ms commands, so bots send 8 ms commands only while climbing
  (owner decision; `bot2_finecmd.c`). Climb edges are discovered on built
  graphs and validated by rollout. ctf4 71/72 over two floors (was 0/36),
  Dantooine 36/36 with climbs in 34 cases, Kejim production floor and library
  stand times identical. The no-path off-mesh yield is now held at every level
  (no legacy hand-off).
- **Open, in order:**
  1. Lifts: stationary movers as floor in the graph and forecast (ctf4's one
     failure is a drop landing beside a resting lift), then lift edges and
     riding, priced against climbs.
  2. Level-1 gate on one build (the gate is not passed).
  3. Kejim sequence joins: at 26 of 39 joins every tap-now candidate touched
     the chasm's `trigger_hurt`. Air strafing turns about 12 degrees in a
     hop at 655 u/s, so the line has to be chosen before the bend.
  4. Level 2 is slower than level 1 on 10 of 12 floor cases, and identical
     level-1 worlds diverge on the Force pool (both from the entry below).

**Update, 2026-09-23: owner's level-1 sign-off tests, goal chains**
([record](2026-09-23-goal-chain-uptime.md); nothing committed).
- **Kejim item route:** 60.9 s, with 68% strafing, 32% walking and 0.1%
  standing. The bot brakes into each stop, arriving at 120-150 u/s.
- **dash2:** 46.7 s, with 97% strafing. Speed plateaus near 700 u/s because
  every landing frame pays about 57 u/s of friction before the next jump.
  Only dash2 is installed; there is no dash1.
- **Later the same day (see the record's follow-up):**
  - the landing jump is measured faster: dash2 takes 32.7 s, and the
    level-1 floor is 8.4 s faster in total;
  - it exposes the known lip-walk death near the red base: two round trips
    at start class 175 fall into the pit, and both pass with it off;
  - follow-through does not pay on round trips;
  - the new flight mode wins on dash2 (29.1 s) but is slower on Kejim, so
    it stays opt-in.
- **Lip walk fixed:** `bot_moveLipGuard` defaults to on.
  - The momentum chord is now at least 32 units long above 20 u/s.
  - The class-175 deaths are gone, and 60 round trips at eight classes
    ran without a death. Both floors pass 36/36.
  - With the guard on, the landing jump is 0.97 s faster on round trips
    (19 of 29 pairs) and neutral on the level-1 floor.
- **Owner ruling, 2026-09-23:** `bot_moveLandingJump` defaults to on (unset
  reads as 1, and 0 turns it off). `bot_moveFlightMode` and
  `bot_moveFollowThrough` stay opt-in.
- **Open:**
  - why level 2 runs slower on 10 of 12 floor cases under the guard and
    under follow-through;
  - why identical level-1 worlds diverge on the Force pool (the search reads
    Force somewhere).

**Update, 2026-09-22, review fixes (after build t; nothing committed).**
- **Replay:** a step with a spinning saber animation before or after it is
  never replayed (Pmove's one random draw a `PM_NORMAL` client reaches). The
  exactness comment and [record](2026-09-22-rollout-replay.md) now
  say the per-rollout check is slot-and-level, with the whole world compared
  at scope end. The hint frame pointer is formed only inside its bound.
- **Search cache:** a cached candidate chosen after the take (StatePreferred,
  forced override, tape) is validated before commit. A refusal logs
  `cache_stale`, restores the decision tape to its pre-selection state, and
  clears both the retry timer and the lookahead cadence, so a live search
  runs on the next frame.
- **Tools:** `movement_decision_tape.py` reads the world's own
  `evaluation.time_ms` and matches its case by name in a campaign record.
  `movement_decision_tree.py` indexes decisions by branch `nth`, so a route
  root no longer shifts the tables. Regression tests were added, and the suite
  is green (893).
- **Checks** (`.codex-research/review-fix-20260922-b`, build
  `review-fix-build-a477ce26`): the six cells pass at build t's times with
  identical decisions (red_ret differs only in one `traces` counter). There
  were zero `cache_stale` events, and the floor passes 36/36. Run `-a` was
  the first round's fixes alone.

**Update, 2026-09-22, final: replay on by default, LAA engine; ready for review.**
- **Tree:** the sources equal `.codex-research/row5-build-20260922-t`.
- **Owner rulings:** `bot_forecastReplay` defaults to 1, and the 32-bit
  engines are linked `/LARGEADDRESSAWARE`.
- **Checks on t with defaults:** the six cells are identical to build p and
  the floor passes 36/36.
- **Cost pair:** rollout time falls from 525 to 469 ms, and Pmove from 317 to
  269 ms.
- **Stopping state:** nothing is committed or installed, no openjkded is
  running, and the suite is green.
- **Review focus:** the replay exactness argument in `bot2_forecast.c`, the
  ring in `RunCandidate`, and the `strafeNearGeneration` bump sites in
  `bot2_graph.c`.
- **Next:** the steering policy's per-step cost (about 40 percent of a
  step).

**Update, 2026-09-22, earlier: rollout replay measured**
([record](2026-09-22-rollout-replay.md)).
- **Tree:** the sources equal `.codex-research/row5-build-20260922-s`.
  `bot_forecastReplay` is default off (1 replays, 2 verifies), and
  `RunCandidate` takes a 12-slot frame ring (about 12 MB).
- **Exactness:** 0 mismatches in 24,819 verified steps. The six cells are
  decision-identical in both modes.
- **Gain:** on same-decision worlds, rollout time falls about 7 percent and
  Pmove about 13 percent, with 17 percent of steps replayed. p99 is
  unchanged.
- **Owner question, since ruled:** replay is on by default (see above).
- **Next rollout lever:** the steering policy (about 40 percent of a step),
  then Pmove's traces. A stopped campaign (`replay-20260922-b`) is explained
  in its `NOTE.md`.
- **Checks:** no openjkded is running, nothing is installed, and the suite is
  green.

**Update, 2026-09-22, earlier: owner rulings and budget 4 by default.**
- **Rulings:** row 5 is judged on p99 inside the frame, and it is met.
  `bot_moveSearchBudget` 4 is the engine default (unset reads as 4, 0 turns
  it off).
- **Build and checks:** build `.codex-research/row5-build-20260922-p`
  (`.codex-research/budget-default-20260922-a/`). With the cvar unset, the six
  cells are decision-identical to budget 0 and the floor passes 36/36. Of 16
  searches at class 75, 11 are served in flight.
- **Cost runner:** `--search-budget` defaults to 4.
- **Suite:** green (890).
- **Next (owner): the rollouts.** Per step, Pmove is about 51 percent and the
  steering policy about 40 percent. Repeated work is the larger lever: 29
  percent of candidate steps are shared prefixes, and 56 percent of reroute
  steps re-fly the root flight
  (`.codex-research/search-speed-20260922-a/prefix_share.py`).

**Update, later 2026-09-22: per-cell strafe-edge lists landed**
([record](2026-09-22-planstate-lists.md)).
- **Tree:** the sources now equal build `.codex-research/row5-build-20260922-o`
  (worlds in `.codex-research/planstate-20260922-a/`).
- **Checks:** identity holds on all six cells and the floor passes 36/36.
- **Planner cost:** 1.17-1.44 us per expansion, down from 1.75, and
  PlanState time falls 20-27%.
- **Frame tail:** p99 does not measurably move. Rollouts are 58-71% of the
  heavy frames.
- **Next candidate:** the rest of each expansion (heap, hash table, the
  walk's law over eight links). Take the cross-search memo only if repeated
  identical queries are measured first. Otherwise return to the rollouts,
  which dominate the tail.

**Where the tree was (build n).**
- The sources equal build `.codex-research/row5-build-20260922-n`.
- It passed identity on the six reference cells:
  - `red_out_L1_stand` at classes 75, 100, 125 and 150;
  - `red_ret_L1_stand` and `blue_out_L1_stand` at class 125;
  - all with the seed, the library under
    `.codex-research/seed-gate-20260922-a/library/`, and the harness off.
- The floor passes 36/36, and the suite is green (890 tests).
- No openjkded is running, and nothing is installed.

**What landed this session** (records
[row 5](2026-09-22-row5-planner.md) and [search speed](2026-09-22-search-speed.md)):
- **Owner decisions:**
  - the seed is production;
  - `jumppad` frames are accepted;
  - the link rung is removed;
  - row 5 is one bot;
  - threads are a backup.
- **Planner and search:**
  - exact planner speedups;
  - the `RerouteFrom` memo;
  - the resumable finish phase;
  - deadline-sized pre-search chunks.
- **Cost fixture:** it accepts `--bots 1`, has `--trace` and `--set`, and
  writes accounting v3 with profiling columns.
- **Rollout speed:**
  - the steering-search rewrite (`Mod360`, checked by
    `tools/strafe_movecontrol_check.py`);
  - world copy and guard once per scope, and lone rollouts scoped;
  - a corridor memo and a trace cache.

  All are unconditional.
- **Numbers:** a rollout costs 11.6-13.6 us a step, against 23-24 before.
  With one bot, p99 is 22.3-22.8 ms at budget 0 and 11.4-11.7 ms at budget 4.

**Next action: the state planner.**
- **Why:** in the worst remaining frames (the search after the fixture's
  2.5 s shove) it is 7-14 `Graph_PlanState` calls at about 5 ms each,
  27,500-49,700 expansions, as much as the rollouts. On the seed each
  expansion scans nine cells' edge lists.
- **Exact candidates:**
  - per-cell neighbourhood strafe-edge lists, kept in the current scan order
    so the heap sees the same pushes (done: build o);
  - a planner memo across searches keyed by `Graph_Generation()`.
- **Verification:** each needs identity on the six cells, then the one-bot
  cost pair (`tools/movement_cost_runner.py --bots 1 --search-budget 0|4`,
  keeping only worlds without `MOD_FALLING`), read with
  `.codex-research/search-speed-20260922-e/analyse_cost.py`.

**Owner items, since ruled** (see the rulings above): row 5 is judged on
p99 inside the frame, and `bot_moveSearchBudget` 4 is the default.

**Then:** the gate report, the spawn-fall defect (an unsettled fixture spawn
pose, about one world in five, falls at 7.25 s with the budget on or off),
and level 2.

---

# Next: the owner rules on row 5; then the gate, the spawn fall, level 2

**One-bot row 5, 2026-09-22** ([record](2026-09-22-row5-planner.md), section 5).
Build `.codex-research/row5-build-20260922-f`; the sources equal it. It
carries, all decision-identical on the six cells:
- the link-rung removal;
- the v3 cost columns and budget counters;
- the planner speedups and the memo;
- the resumable finish phase;
- the deadline-sized chunks (`SearchChunkBudget`).

The fixture and the runner accept `--bots 1`, and the runner has `--trace`.

Results with one bot:
- budget 4: p99 12.5 to 13.9 ms, three frames over 25 ms per 800, and the
  floor 36/36;
- budget 0: p99 36 to 40 ms.

Planner-call weighting was tried and reverted (build g).

**Owner questions:**
1. Row 5's criterion: p99 or every frame.
2. `bot_moveSearchBudget` 4 as the default.

**Found:** an unsettled fixture spawn pose (y -704.000, z 1531.271, about one
world in five) makes the bot fall to death at 7.25 s. It happens with the
budget off too (`row5-20260922-f/XB0-3`, `XB2-5`, identical traces). It is a
strafe-safety item.

**Stopping state:** no openjkded running, nothing installed, suite green.

---


**Row 5 on the seed, 2026-09-22** ([record](2026-09-22-row5-planner.md)).
Final build: `.codex-research/row5-build-20260922-d`. It carries, all
decision-identical on the six reference cells:
- the link-rung removal;
- accounting-v3 cost columns and budget counters;
- exact planner speedups;
- the unconditional `RerouteFrom` memo;
- the resumable finish phase.

The floor passes 36/36 on it.

Row 5 is not met. With four bots, 37 to 42 frames are over 25 ms at budgets
0, 2 and 4; only 5 to 14 of 46 landing searches are served from a pre-search.

**Stopping state:**
- no openjkded running;
- nothing installed;
- the sources are build d's (1,117 hashes; verify with
  `.codex-research/row5-20260922-d/verify_sources.py` after pointing it at
  build d).

**Next action.** In order:
1. Split the budget's "refused, other" counter into its causes: context
   fields, originals, executor, candidates, validation.
2. Replace the per-bot rollout budget with one server-wide pool per frame,
   charging a reroute call as rollout-equivalents.
3. A deadline-based chunk size.
4. The gap/edge search under the pool.

After each step, check identity on the six cells with
`.codex-research/row5-20260922-d/identity.py`: `red_out_L1_stand` at classes
75, 100, 125 and 150; `red_ret_L1_stand` and `blue_out_L1_stand` at class 125.
All use the seed, the library under `.codex-research/seed-gate-20260922-a/library/`,
and the harness off. Then run the four-bot cost pair and
`.codex-research/row5-20260922-a/analyse_cost.py`.

**The gate on the seed, 2026-09-22** ([record](2026-09-22-seed-gate.md)).
Production configuration:
- research seed `g1-98eb1e97c24a4e21`;
- the four seeded tapes, library under `.codex-research/seed-gate-20260922-a/library/`;
- harness off;
- build `planner-directives-build-20260922-a`.

**Rows 1 to 4 met together:**
- floor 36/36, seeded regressions 2/2, respawn4 at class 50 pass;
- harness on/off decisions identical on six cells;
- row 2 median 15,962.5, tail 16,725;
- row 3 met at every class;
- row 4: 72/72.

**Not passed:**
- row 5: p99 98 to 100 ms, worst 216 to 219 ms with four bots; v3 gives 60 to 65 and 106 to 108;
- the respawn5 cases report owner `jumppad` beside `move`, for the owner to rule on.

**Stopping state:** no openjkded running, sources verified after the last
world, nothing installed.

**Next action: row 5 on the seed.**
1. Per-call cost of `Graph_PlanState` and `RerouteFrom` on the seed.
2. Memoise `RerouteFrom` within a search.
3. Spread the finish phase.
4. Repeat `cost.py` (seed against v3).

Every step must be decision-identical on the six reference cells:
- four stand classes;
- the two seeded regressions at class 125, using `identity.sh`.

Ask the owner:
- whether the respawn5 `jumppad` frames violate row 1;
- whether the `link` rung goes.

**Plan review checks, 2026-09-22**
([record](2026-09-22-plan-review-checks.md); decisions in
[PLAN.md](../PLAN.md), "Plan review, 2026-09-22").

**(1) v3 cannot carry the seeded trips.** The four seeded explorer tapes
were remapped to cache v3 by `.codex-research/v3-feasibility-20260922-a/remap.py`.
Results by class:

| Class | Time on v3 | Entries hit |
| ---: | ---: | ---: |
| 75 | 17,250 ms | 6 of 11 |
| 100 | 16,950 ms | 7 of 11 |
| 125 | 16,375 ms | 5 of 11 |
| 150 | 17,000 ms | 4 of 10 |

The median is 16,975 ms. Requiring 3313 or 3314 changes nothing.

**(2) The start-phase hold is refuted.** Held to phase 125, every class
takes 19,175 to 19,350 ms, deterministic on build
`start-phase-build-20260922-b`. The code is removed, and the tree again
matches the planner-directives manifest (`mismatched: []`).

**Stopping state:**
- no openjkded running;
- nothing installed;
- the eight-class v3 chain (`...-chain-20260922-a.sh`) is superseded and was
  never run.

**Next action: ask the owner which graph is production.** The seed was set
aside only because two respawn4 cases died on it, and both now pass on the
seed.

- **If the seed:** run the floor, both seeded regressions and row 4 on the
  seed with the current build, library on. Then bind the four seeded tapes
  to `g1-98eb1e97c24a4e21` and re-take rows 2 and 3, then row 5.
- **If v3:** launch `.codex-research/decision-explorer-v3-chain-20260922-b.sh`
  (four declared classes only). Use the pre-launch checks and the detached
  launch below, with `-b` in place of `-a` for the chain's own files; the
  `manifest-before.json` to verify against is still the `-a` one. Read the
  four classes before running 0, 25, 50 and 175.

Either way, any row-5 change must be decision-identical on the reference cells.


**Deferred save by default, planner directives, cache v3, route branching,
2026-09-22** ([record](2026-09-22-planner-directives-and-v3.md)). Fresh
build `.codex-research/planner-directives-build-20260922-a` (1,117 hashes and
both binaries matched before the first world and after the last world).
**(1)** `bot_moveGraphSaveDefer` now defaults on (`0` restores immediate
saves), with a flush at every objective change after the census; the six
reference cells are decision-identical to the safety-save deferred arm and the
four production caches byte-identical. **(2)** `bot_movePlanAvoid`,
`bot_movePlanRequire` (a required-edge bit in `Graph_PlanState`'s key, the
ordinary search when unmet, `require_met=` on the plan line) and
`bot_movePlanGen`, default off and inert on every cell. **(3)** The four
seeded tapes replay on the seed at 15,950 / 16,725 / 15,975 / 15,925 ms, every
entry hit; their leg routes fold into **cache v3,
`.codex-research/production-graph-20260922-c/production-graph.txt`, 3,332
edges, `g1-9a85d509075ecbc1`**, carrying seed 51453/48185/41945/59480 as
3312-3315. On v3 (harness and library off): floor **36 of 36**, zero deaths;
seeded regressions 17,500 / 20,700; respawn4 at class 50 16,625 / 16,625;
class 125 **16,375 ms** and class 75 **20,650**, four of four each. v3's
class-75 release plan takes 3312 and the seeded route's first eight edges
unforced, not the whole seeded route. **(4)** `tools/movement_decision_explore.py
--route-branch [--reference-tape]` runs one world per release-route edge avoided
and per reference state edge required, each a confirmed root; the class-75
smoke drove the bench (16 worlds, best confirmed 17,925 ms). **Stopping state**:
no openjkded running, sources verified after the last world, suite 887 OK; the
v3 chain was not launched, by owner decision.

**Superseded by the plan review above - the former next action: launch the eight-class explorer chain on v3 with route
branching.** The script is written and has never run:
`.codex-research/decision-explorer-v3-chain-20260922-a.sh` (classes 75 150 100
125 0 25 50 175, beam 3, patience 2, max-depth 0, port 37800, build
`planner-directives-build-20260922-a`, `--graph` v3, `--route-branch`, and for
classes 75/100/150/125 `--reference-tape` the seeded explorer tape of that
class; outputs `.codex-research/decision-explorer-v3-c<class>-20260922-a/`,
log `.codex-research/decision-explorer-v3-chain-20260922-a.log`, done marker
`.codex-research/decision-explorer-v3-chain-20260922-a.done`; it re-verifies
the sources into `...-chain-20260922-a.manifest-after.json` at the end).
Before launching, in order: (a) `python
.codex-research/planner-directives-20260922-a/verify_sources.py
.codex-research/decision-explorer-v3-chain-20260922-a.manifest-before.json`
must report `mismatched: []`, `missing: []` and both binaries matching; (b) no
`openjkded.x86.exe` process may be running (`tasklist | findstr openjk`); (c)
`python -m unittest discover -s tools -p "test_*.py"` from the repo root must
pass. Then launch detached, never inline: `Start-Process -WindowStyle Hidden
-FilePath "C:\Program Files\Git\bin\bash.exe" -ArgumentList
"-l",".codex-research/decision-explorer-v3-chain-20260922-a.sh"
-WorkingDirectory "<repo root>"`, and confirm a python process whose
command line names `movement_decision_explore` within 20 s. Nothing under
`tools/` or `codemp/` may change until the `.done` marker exists. A route
root's result is a tape plus a directive, which the tape library cannot carry
yet; that is the follow-up once the chain reports.

**Deferred save, no legacy hand-off, route telemetry, 2026-09-22**
([record](2026-09-22-deferred-save-and-floor-safety.md)). The first
three items of the seed-deaths and row-5 work order landed on fresh build
`.codex-research/safety-save-build-20260922-a` (1,117 hashes matched before and
after; worlds under `.codex-research/safety-save-20260922-a/`). **(1)**
`bot_moveGraphSaveDefer` (default 0) snapshots the cache at each autosave and
writes it at shutdown, from `Graph_Autosave` and at map prepare: all six
reference cells decision-identical between arms and to their reference worlds
(seed 15,925 / 21,125, v1 19,050 / 19,150, v2 19,275 / 19,450 ms), four of six
caches byte-identical and the seeded two identical but for a post-leg learn; on
v2 cost p99 fell from about 119 ms to 64 to 74 ms and dispatch work about nine
times, but 103 to 117 frames of 800 still exceed 5 ms and every worst frame is
a boundary search, so row 5 is still open. **(2)** A walk decline the movement
owner can resolve (corridor, no path, miss) is held under that owner - the
route's next crossing avoided, a replan, the walk's brake - and never handed
to legacy; no cvar, inert on every reference cell. The eleven-death trip now
**passes at 19,400 ms** on the seed at class 50 (both respawn4 cases, zero
deaths, no legacy), and v2 at class 50 passes at **16,625 ms**; floor on v2
with the save deferred **36 of 36**. **(3)** Every installed route prints a
`[ROUTE]` line of its edge indices outside any search, and
`tools/movement_graph_enrich.py` parses it (`read_routes`); nothing folds
routes yet. **Next**: item (4) of the work order, planner `avoid`/`require`
directives, then fold planned routes into cache v3; the row-5 planner memo and
finish-phase spreading follow the attribution record. The deferral stays
default off until the owner decides.

**Cache v2 from the edges real runs committed; the explorer re-runs on it,
2026-09-22** ([record](2026-09-22-production-graph-v2.md)). On cache v1
the explorer's four declared classes converged at 17,650 / 17,125 / 17,900 /
16,275 ms, all above row 2's 16,500, and the seed's fast trips used state-plan
edges v1 lacked. `tools/movement_graph_enrich.py` resolves every committed edge
index in a retained world against that world's own saved cache and folds the
records into v1: **12,079 indices across 2,806 worlds, all resolved**, 470
committed edges new from the seven seeded campaigns plus 2,194 learned edges
from the v1 explorer worlds' saves. **Cache v2 is
`.codex-research/production-graph-20260922-b/production-graph.txt`, 3,312
edges, identity `g1-9631cbeaef2e5ba8`.** On build
`search-budget-build-20260922-c` (1,117 hashes matched before and after), library
and harness off: **floor 36 of 36**, zero deaths, owner `move`, no forbidden
spend, both seed-killed respawn cases passing (16,125 and 16,150 ms) and both
seeded regressions; **one cell per class** - class 125 six of six at **19,275
ms**, class 75 four of four at **19,450** - which is 225 and 300 ms slower than
on v1, the ranking taking enriched edges from the first decision. **Running
now**: the eight-class explorer chain on v2, classes 75 150 100 125 0 25 50
175, beam 3, patience 2, from
`.codex-research/decision-explorer-v2-chain-20260922-a.sh`, output
`.codex-research/decision-explorer-v2-c<class>-20260922-a/`, exit codes in
`decision-explorer-v2-chain-20260922-a.log`, done marker
`decision-explorer-v2-chain-20260922-a.done`. **Sources and `tools/` stay
frozen until that marker exists.** Next: read each class's best against row 2
and against its v1 best; tapes explored there bind to `g1-9631cbeaef2e5ba8`.

**A search budget that keeps every decision and does not pay for itself,
2026-09-22** ([record](2026-09-22-search-budget.md)). The Pmove-boundary
search now has a budget, `bot_moveSearchBudget`, **default off** and staying
off. It is a **per-bot per-frame candidate rollout count**, not a microsecond
budget, because a wall-clock split would divide the same world differently from
run to run and decision identity has to be checkable. On fresh build
`search-budget-build-20260922-c` (1,117 source hashes matched before the first
world and again after the last), the engine predicts the boundary the next
straight search will run at - the landing of the committed sequence, or the
retry a failed search scheduled - installs that predicted state on the bot for a
bounded chunk of the ordinary search on each frame before it, and at the
boundary takes the judged set only when the path, the objective, the player
state on the fields the parity harness compares, the executor pair (where the
landing tap bit lives), every constructed candidate and a validating rollout of
the winner **from the actual state** all agree; anything else is today's whole
search on that frame. **All four declared cells are decision-identical**:
seeded class 125 **15,925 ms / 13 / 5** and class 75 **21,125 / 16 / 8** with
the harness armed, production class 125 **19,050** and class 75 **19,150** with
it off, each matching its budget-off twin on the same build decision for
decision (13 of 13, 16 of 16, 14 of 14, 14 of 14). Over the floor and both
seeded regressions, **328 of 492 searches (66.7 percent) were served from a set
judged in flight**, one refused for a replanned path and **zero** refused on
state, executor, candidates or validation, and the floor is **36 of 36** with
zero deaths, the movement owner throughout and no forbidden spend. **But it
does not meet row 5 and does not move the tail**: two paired cost worlds with
the arms alternated give p50 **117 / 112 us** to **3,028 / 1,414** (the work
being spread, which is the point), p95 **71,657 / 90,012** to **104,478 /
80,701**, p99 **226,991 / 182,785** to **244,111 / 242,457**, worst frame
**2.41 / 0.56 s** to **3.42 / 1.72 s**, frames over the declared 5,000 us
budget **126 / 123** to **364 / 337** of 800, and **1.6 to 2.1 times** the
total work at the boundary. Two things row 5 inherits. The budget-off control
**on the production cache** is already about twice the 2026-09-22 smoke at p95
and p99 - the smoke ran on the 10-edge map-load graph, not the 676-edge cache -
so the row's starting point is worse than the number it has been carrying. And
the candidate rollouts are **not** the tail: the budget moves them rather than
multiplying them (`[SPIKE]` 339 to 349 and 350 to 371 a leg, `[REROUTE]` 37 to
35 and 34 to 33) and the frames that break the server frame break it in both
arms, so whatever costs 100 ms to 2 s at that boundary is something a rollout
counter cannot see. Naming it - the reroute pass's per-candidate
`Graph_PlanState` over 14,249 cells and 676 edges is the obvious suspect - is
the next row-5 measurement, before any further budget work.

**A production graph cache from the bot's own runs, and a release that puts
the learned set back, 2026-09-22**
([record](2026-09-22-production-graph.md)). The graph-binding record's
open item - *an unseeded stand run is not a cell* - is closed, and the cause was
entirely the fixture's own pre-release free play. **A fixture world does not
begin at the release**: the bench brings a world up, the bot's AI plays from its
spawn, and only when the world reaches the pinned level does `bot_corner_start`
place it at the case's pose; every strafe edge the search commits in between is
learned into the graph and joins the state planner's routes. On the ten retained
unseeded worlds pinned at level 3,125 the **pre-release learned set predicts the
trip exactly** - five worlds that learned cell 47 to 4949 all returned **19,700
ms** with 12 of 12 decisions identical, four that learned nothing or cell 61 to
6167 all returned **16,725**, one that learned cell 50 to 4948 returned
**18,475** - with both parity arms inside the groups, so the harness's arming is
not the variable. The mechanism is the first post-release route: 206 cells with
one strafe edge on it against 215 cells and none. **The release now restores the
learned set to the graph as loaded.** `Graph_ResetLearned` drops appended edges
and restores, from a copy taken before the first mutation, the loaded edges a
merge, a cost or a result changed; it is called from `bot_corner_start` and from
nowhere else, so **production is unchanged - a real spawn has no release**.
Suspending learning during the hold was considered and is impossible without a
cvar, because the engine is not told a release is coming; no cvar was added.
**The production graph is now a cache prepared from real runs**: the union of
the gate campaign's unseeded arms through `tools/movement_graph_merge.py` - 144
world caches, 3,736 edge records folded to **676** (650 strafe, 16 hop, 10 pad)
over 14,249 cells - identity **`g1-0e5af7aba9b5e908`**, against the map-load
graph's 10 edges and the 2026-09-16 seed's 94,711. On fresh build
`production-graph-build-20260922-a` (1,117 source hashes matched before the
first world and after the last), with that cache, the library off and the
harness off, **a start class is a cell again**: ten worlds at class 125 return
**19,050 ms** and **one** 12-decision leg sequence, six at class 75 **19,150 ms**
and **one** 13-decision sequence, each across three different release levels of
its class. **The floor is 36 of 36** on that configuration - zero deaths,
movement owner on every frame, no forbidden spend - and **both respawn cases the
research seed killed now pass**, `red_ret_L1_respawn4` at 16,300 ms and
`blue_out_L1_respawn4` at 16,150, as do both seeded regression cases. Both
seeded control cells still reproduce at 15,925 / 13 / 5 and 21,125 / 15 / 7. Two
things the explorer campaigns inherit: the cache is a **third** graph identity,
so a tape must be explored on the graph it will execute on; and gate row 1's
non-zero parity count cannot be met by a configuration that turns the harness
off, which the gate has to reconcile.

**Tapes bind to their graph, the search leaves the harness, and row 5 has an
instrument, 2026-09-22**
([record](2026-09-22-graph-binding-and-decoupling.md)). The gate's two
open defects are addressed and the row it could not measure now has a path. On
fresh build `graph-binding-build-20260922-b`, 1,117 source hashes matched
before the first world and after the last, twenty-six bench worlds (seventeen
completed) and two cost worlds, zero deaths and the movement owner throughout. **Hop discovery no
longer runs only while the parity harness is armed**: `ParityHorizon`'s four
non-harness conditions - dedicated, `sv_cheats`, `bot_moveRewrite`,
`Bot2_IsMovementOnly` - move to `MoveSearchGates` and are all kept, and only
`bot_moveParity > 0` leaves the search's path. With the harness on the two
seeded control cells reproduce to the commit (**15,925 ms / 13 / 5** and
**21,125 / 15 / 7**); with it off they return the same times, the **same leg
decisions in the same order** (13 of 13 and 15 of 15) and **zero**
comparisons - zero because nothing was armed, and no world reports
`no_search_gates`. An unseeded pair matched on the release level returns
**19,700 ms** either way, 12 of 12 decisions identical. **A tape is now bound to the graph it names cells and
edges in**: `Graph_Identity()` is `g1-` and a 64-bit digest of the cells and
edges as loaded, frozen at map load, written by the Python tools as the
optional trailing header field of a tape and of a library index, so every
earlier campaign replays unchanged. The map checksum could not do this - the
2026-09-16 seed is `g1-98eb1e97c24a4e21` with 94,711 edges and the unseeded
build of the same map `g1-301dd39cbead57f6` with 10. A seeded library staged
unseeded is now refused **at load** (`keys=0`, `library=graph_mismatch`), and a
tape refused on its own is refused **on the exact rung** with the census line
reading `rung=graph_mismatch entries=0`, so the leg falls back at its first
think instead of running eleven entries into zero hits; the same library in a
seeded world still hits 11 of 11 at 15,925 ms. **Row 5's first reading is far
outside its budget**: `tools/movement_cost_runner.py`, four bots on one
dedicated localhost server with the production configuration, 800 frames -
**p50 67.5 us, p95 36,800, p99 96,870, worst 272,692** against the 25,000 us
frame, **114 of 800 frames over the declared 5,000 us budget and 64 over the
whole frame**, 96 percent of it the Pmove-boundary search. Nothing was tuned;
one run measures the instrument, not the row. **The production configuration is
the unseeded graph**, and an unseeded stand run is shown **not** to be a cell:
ten worlds pinned to one release level return 16,725, 18,475 or 19,700 ms and
both arms share the outcomes, because the graph is built at load and edges are
learned before the release. An unseeded cell needs repetitions - the open item
the explorer campaigns inherit. The paragraphs below record the gate.

**Gate rows 1 to 4 measured together, 2026-09-22**
([record](2026-09-22-gate-l1.md)). On `tie-break-build-20260922-a`
with the four-key library bound by hash, no rebuild and no C source change,
1,117 source hashes matched before the first world and after the last:
**rows 1, 2 and 3 are met, row 4 is met in the floor's configuration and not
in row 2's, and row 5 is not measured - so the gate is not reported passed.**
Row 2 at the **six** repetitions per class the row asks, two controls per
class: equal-weight median **15,962.5 ms** against 16,500 and tail **16,725
ms** against 18,000, control 18,475 and 21,125, declared-weight mean 16,054.6
against 17,427.7; all 24 library worlds on the **exact** rung with **every
entry hit** at aim gap 0.0000 and the six worlds of each class agreeing to the
millisecond. Row 3 on those worlds: **30.4, 55.1, 37.9 and 35.0 percent**
against controls of 71.2, 59.5, 37.9 and 50.5, so no class waits more than its
control and **rows 2 and 3 move together at every class** - the attribution the
row exists for - while no candidate is `admitted` at any waiting frame in any
of the 32 worlds. Row 1: the 36-case floor is **36/36 with the library off and
36/36 with it on**, both seeded regressions pass with it on, zero deaths,
movement owner only, zero mismatches, **every** world reporting a non-zero
comparison count (419 to 825) and no forbidden spend; the ladder fires in 11 of
the 36 floor cases (3 on `phase`, 8 on `link`) and harms none. Row 4 unseeded
is twelve cases, three repetitions per arm, **72 of 72**, with one case 200 ms
slower in the library arm. **What failed is the research seed, not the
library**: eleven seeded row-4 worlds died, all `red_ret_L1_respawn4` or
`blue_out_L1_respawn4`, six library and five control, and a four-world probe
with the 2026-09-16 seed removed returns all four passing with zero deaths. And
**a tape does not survive its graph seed** - unseeded, an exact-rung selection
hits zero entries - which is new and unrecorded in the library's design. The
open work the gate now points at is row 5, the runtime-qualification row, and
the two seed defects above. The paragraphs below record the tie-break repair
that made these numbers possible.

**The tie-break is the defect, and all four tapes now reproduce, 2026-09-22**
([record](2026-09-22-tie-break-and-tally.md)). The four-class result's
open question is answered without re-extracting a tape or touching the
library: when several admitted candidates match a tape entry by the
landing-cell identity - 5 or 6 entries of every leg do - the tie was broken by
the judge's **score**, which is computed from the live root and so is not
stable between the explorer's branch world and a fresh tape-only one. It is now
broken by the **printed-aim gap**, the quantity the census already reports and
a property of the candidate rather than of the ranking, with the rank deciding
only an exact tie. On a fresh build with the same four-key library, the
class-100 tape hits **11 of 11** entries where it hit 4 and the class-150
**10 of 10** where it hit 2, and both return their explorer campaigns' own
times - **16,725** and **15,975 ms** against 16,875 and 18,675 - while classes
75 and 125 are unchanged at 15,950 and 15,925 with every entry hit and every
aim gap 0.0000. **Row 2's two numbers are met on these worlds and the row is
not reported passed**: equal-weight median **15,962.5 ms** against 16,500 and
tail **16,725** against 18,000, over three repetitions per class where the row
asks six and one control per class; the declared-weight mean is 16,054.6
against 17,427.7. **Row 3 is not measured on these worlds**, and the plan's own
rule says a row-2 gain whose mechanism the waiting share does not confirm is a
selection artifact, so that is the next measurement. The **parity tally** is
repaired in the same build: one `reason=tally` event per leg carrying the
count the leg really made, `parity_summary` reading it and falling back to the
old `horizon` count for retained worlds, `row1_ok` stating the row's
requirement, and all 18 worlds reporting 667 to 905 comparisons with zero
mismatches - and the two zero-count worlds explained from their own logs. The
**decision explorer** now runs a confirmation world, the branch's whole
sequence as a tape with no prefix and no forced index, before a branch's time
can be the incumbent; it is unit-tested and no campaign has run under it.
Both control cells reproduce on the new build with the library off. The
paragraphs below record the sixth revision and the result that posed the
question.

**Owner direction, 2026-09-22, sixth revision.** The transfer question is
answered ([record](2026-09-22-decision-tape.md)): entry 0 transfers
from every class, the sequence stops at entry 1 or 2 for a class-specific
reason, and no class approaches 15,925 ms. The class is the phase between the
25 ms frame and the 8 ms Pmove step, so this is physics, and forcing the
reachable prefix still beats the ranking by 450 to 550 ms at two classes. The
next work order is the
[decision explorer](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-22-the-decision-explorer):
a per-class beam search over the live pipeline's admitted candidates, using
the decision-tape prefix plus `bot_moveSelectNth`/`bot_moveSelectCand` to
branch, every world run to the objective, the fastest chain per class retained
as a decision tape. Stage 1 first: index-forcing reproducibility at class 75,
wall-clock per world, which of the eight phases the harness can reach, and the
build script's exit code. Explorer times count toward the gate only once the
bot selects the tape itself by map, leg, start cell and phase, and the
fallback is measured. The paragraphs below record the fifth revision and its
result.

**Four-class library measured, 2026-09-22**
([record](2026-09-22-four-class-library.md)). The four explorer tapes
are indexed under one key each (leg `t1e83`, cell 0, phases 75, 100, 150, 125)
and rows 2 and 3 are measured with the bot selecting, 24 completed worlds on
build `tape-library-build-20260922-b`, three library worlds against three
tape-off controls per class, alternated. **Row 2's median is met and its tail
is not**: equal-weight median **16,412 ms** against 16,500, tail **18,675 ms**
against 18,000, control 18,475 and 21,125 on the same worlds. The
declared-weight mean did not move at all - **17,438 ms against 17,428** -
because class 150 carries weight 47 and the library made it **1,100 ms
slower**. **Every library world selected on the exact rung**, so selection is
not the defect; **two of the four tapes do not reproduce**. `c100-best` hits 4
of 11 entries and `c150-best` 2 of 10, and they are exactly the two that had
never been run on a fresh world - an explorer's stopping rule reads a branch
world's time, and a branch world ran under a prefix plus a forced index, so an
explorer result is not a tape result until the tape has run alone. Per class,
library against control: **15,950 / 21,125**, **16,875 / 19,375**, **15,925 /
15,925**, **18,675 / 17,575**. **Row 3 is not met anywhere**: waiting shares
30.4, 58.3, 37.9 and 62.5 percent against a threshold of 35, with an
`uncommitted_grounded` run at classes 100 and 150, and no candidate `admitted`
at any waiting frame in any world. Row 1's signal only: 22 of 24 worlds carry a
non-zero parity comparison count, the two zeroes are the class-75 control cell
again, zero mismatches, and nothing was repaired. Rows 4 and 5 are not measured
and this is one case on one map. The next question the result poses is whether
the class-100 and class-150 tapes can be made to reproduce - re-extraction, the
tie rule, or an exit-state term in the decision identity - before any further
class is indexed.


**Stage 3 is done, 2026-09-22**
([record](2026-09-22-tape-library.md)). **The bot now selects its own
decision tape, and the fallback is measured**, which are the two conditions
stage 3 set before an explorer time can count toward a gate row. A library of
tapes and one index sits under `bot2-decisions/<map>_<checksum>/`, bound the
way the graph cache is, behind the default-off `bot_moveTapeLibrary`;
`bot_moveDecisionTape` still wins over it, so every earlier campaign replays
unchanged. At a leg start the bot forms its own key - the leg it runs to
(**team and objective entity**, `t1e83` here), the start cell the planner plans
from (**cell 0**) and **`level.time` modulo 200** - and walks a four-rung
ladder whose every rung's count is printed on the census line. **Three fresh
class-75 worlds and one class-125 world select their own tape on the exact rung
and return 15,950 and 15,925 ms with 11 of 11 entries hit, no refused and no
absent entry**, zero deaths and the movement owner throughout; with both cvars
unset the same build returns the 15,925 / 13 / 5 and 21,125 / 15 / 7 control
cells. **The fallback completes**: classes 100 and 150 fall to the nearest
phase, hit 2 and 1 of 11, and finish in 19,150 and 17,025 ms against 19,375 and
17,575 ms controls, the class-150 figure agreeing with the transfer
collection's own. One thing the design had wrong and the probe caught: the
fixture's release holds **no objective entity**, so the key is completed at the
leg's first think with the leg start's own cell and phase. The
**zero-comparison parity world reproduced and has a cause** - the tally's
`rollouts % 10` print condition, not the sampler's coverage; both such worlds
carry `parity_index` up to 52 - and the repair is left to its own step because
it changes every world's pass/fail semantics. No gate row is reported moved
here. The paragraphs below record the sixth revision and the stages before it.

**Stage 1 is done, 2026-09-22**
([record](2026-09-22-decision-explorer-stage1.md)). On the same build,
with no rebuild and no source change: **index forcing is reproducible to the
frame within a class** - two admitted candidates forced at the commit after the
class-125 prefix's first hit at class 75, two worlds each, each pair identical
in completed time, in every commit of the leg and in the census, one pair from
two release levels 400 ms apart (21,125 ms unforced against 20,275 and 19,025
forced, which is a diagnostic and moves no row). **A world costs 16.9 s median**
at the default timescale, 9.5 to 35.3 s over 27 worlds, so a beam of 3 is 12 to
21 minutes per depth. **All eight classes are reachable**: the play log's 4 Hz
ladder is `L0 + 250k` and `gcd(250, 200) = 50`, so a world offers the four
classes congruent to `L0` modulo 50 and the other four need another world -
classes 0, 25, 50 and 175 were each landed here for the first time.
**`strafe_fixture_build.py` now exits non-zero on a failed build**, checked
against MSBuild's own verdict and the staged binaries, with six regressions that
fail before the repair. The explorer itself is built
(`tools/movement_decision_explore.py`, 34 unit cases, resumable, one world per
bench call) and its smoke run at class 75 to depth 1 ran four worlds and wrote
the fastest as a tape: 21,125 ms control against 19,725, **18,950** and 21,000
for the first three admitted candidates of the first decision. Two things it had
to be built around: the override is offered on pre-release commits too, since
the commit counter restarts at the release, and a leg ending on an arriving hop
has a last decision no tape can name. Stage 2, the search campaigns, has not
run.

**Stage 2 at class 75 is done, 2026-09-22**
([record](2026-09-22-decision-explorer-c75.md)). The beam search ran on
the same build with no rebuild and no source change: 175 worlds, 163 usable, 49
minutes, beam 3, stopping after depths 4 and 5 did not improve on depth 3.
**The class's fastest chain is 15,950 ms against its 21,125 ms control**, and
**it reproduces exactly**: three fresh worlds executing `c75-best.tape`
complete in 15,950 ms with **11 of 11 entries hit, no refused and no absent
entry, every aim gap 0.0000**, zero deaths, the movement owner throughout, and
agree decision for decision - one of them released 600 ms from the other two.
Three alternated tape-off controls return 21,125 ms three times, and the
class-125 cross-check returns 15,925 ms with and without its tape, so nothing
drifted. **Row 3 has a fast trip's number for the first time**: waiting is 30.4
percent of grounded frames at class 75 and 37.9 at 125 against the control's
71.2, over a threshold of 15 - but in frames the tape trip waits 21 where the
control waits 141, and the fast trips carry no `uncommitted_grounded` run at
all; the threshold was declared before any trip under 16,000 ms existed and the
owner reads it. **The judge's ranking does not predict the completed trip**:
Spearman 0.178, 0.036 and 0.335 over the winning lineage's first three
decisions, the ranking's own first choice completing in 21,125, 19,575 and
19,575 ms, and the eventual winner holding rank 7 of 21, 6 of 11 and 5 of 17.
The tape's landing-cell identity also turns out **not** to be an identity for
the trip: five candidates it calls one decision complete in five times spanning
1,625 ms. No time here moves a gate row - stage 3's two conditions are open. No
other class's campaign has run.


**Owner direction, 2026-09-21, fifth revision.** Step 1 stands and its build
is the control. The open-loop tape replay of steps 2 and 3 is withdrawn: the
self-control fails at a landing decided by 0.28 units, and a knife-edge trip
replayed open-loop from a different start cannot separate "the world differs"
from "a sub-unit difference tipped a landing", repaired or not. Steps 2 and 3
are re-posed as the
[decision tape](../../archive/movement-2026-10-05/PLAN.md#steps-2-and-3-revised-2026-09-21-the-decision-tape):
build the class-125 world's committed decisions from its retained telemetry,
load them through one default-off cvar into the existing forced-candidate
hook, which selects only admitted candidates after the last ranking step, and
run classes 75, 100 and 150 with a tape-off control per class. Hits, refused
misses and absent misses are the result; the reading table is in the plan.
Self-control first: the tape from its own world must reproduce 15,925 ms,
13 commits (5 of them state), every entry a hit. The raw-angle hypothesis
is parked; player and converter stay untouched. No explorer before the
report. The paragraphs below record the fourth revision and step 1.


**Owner direction, 2026-09-21, fourth revision.** The next work order is the
[transfer question](../../archive/movement-2026-10-05/PLAN.md#the-transfer-question-2026-09-21-does-the-fast-chain-survive-a-different-start-class):
record the bot's own class-125 trip (15,925 ms on the rescue screen's control)
as a `BOTROUTE2` tape, prove the player reproduces it from class 125, then
play it from releases pinned at classes 75, 100 and 150, three worlds each.
If all three complete within 250 ms of the recording, the start class is a
decision-phase artifact and the chain explorer is built with that tape as its
first plan. If a class diverges at a repeatable tape index, the world differs
there and that difference is located before any explorer. The reading table
is declared in the plan. Tape times move no gate row. The rejected rescue
switch and its code are removed under this order. No frame-local candidate or
selection work at level 1. The paragraphs below record the rescue result that
led here.

**Step 1 is done, 2026-09-21**
([record](2026-09-21-bot-route-recorder.md)). Recording a bot did need
a source change - `Arm()` refused `SVF_BOT` by construction - and it was made
under the owner's direction: the console command still refuses a bot, and a bot
is armed only by the corner fixture on its own release frame under
`bot_routeRecord 2`, a new value of the existing cvar rather than a new switch.
The recording starts at the release, so row 0 is the release frame's own
command; the metadata declares `subject: bot`, the release level and the nine
binding settings, and `human_route_report.py` now labels a bot recording as its
own kind and refuses to read one as a demonstration. The rejected rescue switch
and all of its code went out in the same rebuild.

On that build, `red_out_L1_stand` at class 125 completes in **15,925 ms with 13
straight and 5 state commits with the recorder off, and the same 15,925 ms with
the same 13 and 5 with it armed** - the rescue screen's own control cell, to the
millisecond and to the decision, so the build is the control from here on and
the recorder is inert on the trip. The tape is 636 rows, complete at the enemy
flag, retained beside its world. The recorder's own clock reads 15,900 ms
against the harness's 15,925: one server frame, the same offset every retained
route asset shows.

**The decision tape is built and its self-control passes, 2026-09-22**
([record](2026-09-22-decision-tape.md)). The fifth revision's
instrument: `tools/movement_decision_tape.py` builds a world's committed
decisions read-only from its commit lines; `bot_moveDecisionTape` loads one the
way the corridor loads its polyline; the preference runs where the
forced-candidate override runs and keeps its rule, selecting only a candidate
the judge already admitted, and reporting every entry it could not take as a
refused or an absent miss. Admission, physics, cadence and weights are
unchanged, and unset the world is the control.

On the repaired build a class-125 world with the tape unset completes in
**15,925 ms with 13 commits, 5 of them state**, and the tape built from that
world's own telemetry completes in **15,925 ms with the same 13 and 5 and every
one of the leg's 11 entries hit, no miss, every aim gap zero**. That took six
attempts; the five failures are retained and each was a real defect, listed in
the record - including the rule change the first collection forced, from the
printed aim to the landing cell. **Collection answered the transfer question on 2026-09-22, and the answer
is not one row of the declared table.** 18 worlds, all complete, no pin misses,
every triple identical. Entry 0 - the stand's state chain on edges
50732/26663 - transfers from every start. The sequence then stops at the second
or third decision for a class-specific reason: class 75 offers entry 1 and the
judge **refuses** it twice at the same level in all three worlds (row 2), while
classes 100 and 150 find it **absent** (row 3) and their roots return near the
late entries without matching anything. No class comes near 15,925 ms or hits
more than 2 of 11 entries, so **row 1 is refuted: the class-125 sequence is not
available whole from any other start** and is not the chain explorer's first
plan.

| Start class | Control | Tape | Change | Hits | First miss |
| ---: | ---: | ---: | ---: | ---: | --- |
| 75 | 21,125 ms | 21,125 ms | 0 | 1 of 11 | entry 1, refused |
| 100 | 19,375 ms | **18,925 ms** | **-450** | 2 of 11 | entry 2, absent |
| 150 | 17,575 ms | **17,025 ms** | **-550** | 1 of 11 | entry 1, absent |

Forcing the reachable prefix is 450 ms faster at class 100 and 550 at class
150, and nothing at class 75. That is a diagnostic under forced selection and
moves no gate row, but it says the ranking's own choice at those first
decisions is not the best available from those starts.

**The next work is the owner's to name**, and the table names two branches at
once: row 2 for class 75, locate the frame where entry 1 is refused and compare
the live root, speed and heading against the tape's; row 3 for classes 100 and
150, the explorer must search per start class and the plan library be keyed by
start state rather than map position alone.

The paragraphs below are the open-loop replay this order replaced.

**Step 2 of the replay order ran, and its self-control did not reproduce.** Three worlds pinned
to class 125 (corner levels 5325, 4925, 4725) all return `rejected` /
`tape_end`: **15,925 ms, max drift 31.839, 69 of 69 marks, no flag**. The
replay is not wandering off the line - through command 580 it tracks the
recording to **0.343 units and 0.017 units/sec**, the repaired player's own
documented fidelity and inside the 0.29 to 0.35 the order required. It breaks
at a single landing, command 583, where it arrives 0.283 units lower and leaves
with 20.689 units/sec less than the recording kept; one grounded 8 ms step at
426 units/sec costs about 20.4, so a sub-unit arrival difference deciding a
ground contact by one step fits what is measured. Everything after 583 follows.

Two things fell out on the way. The replay is **deterministic across absolute
level**: three class-125 levels spanning 600 ms gave identical times and
identical drift, so within the class the absolute level changes nothing. And it
is **not tape angle rounding**, which is worth about 5e-4 units over the whole
trip; but the bot's command angles carry a constant `delta_angles` offset of
234.997559 degrees, so unlike a human's its raw 16-bit command angles are
exactly recoverable, and reissuing them is the untested candidate for closing
the 0.34 units. The player and the converter's format were not touched.

**Next: the order's third reading.** Repair the drift with a regression shown
to fail first, then rerun the self-control; steps 3 and 4 do not run and
neither reading of the class difference is drawn until it passes. Replaying at
the tape's own level 3125 is not available through `route_approve.py` - its
startup consumes past it, the first readable play-log row being 4425 to 4675 -
so reaching an exact early level would need that startup changed.


**Arm A ran on 2026-09-21 and is not accepted**
([record](2026-09-21-library-rescue.md)). The library-rescue arm of
the third-revision work order is implemented behind `bot_moveLibraryRescue`,
default off, and screened over 48 worlds: four start classes, six matched
pairs each, arms alternated in sequence, the control the same build with the
switch off.

It works as designed and costs time. Over the 24 rescue worlds the search
offered **1,571 envelope-admitted strafe edges, the judge admitted 113 (7.2
percent) and committed 45**, at frames where it had admitted nothing - so
learned edges do transfer across the envelope's width. But the declared-weight
mean is **19,416.4 ms against the control's 17,427.7, a 1,988.7 ms loss**,
and the waiting share falls only **60.3 to 58.3 percent** against the gate's
15. Class by class the two rows move together: both improve at class 75
(-2,725 ms, -9.5 points) and class 100 (-75 ms, -6.1), and both worsen at
class 125 (+3,425 ms, +18.0) and class 150 (+2,000 ms, +9.8), which carry 77
of the 93 declared weight. Two committed library flights per world are enough
to do that, and they leave the bot waiting more afterwards, not less: waiting
was cheaper than the flight the library offered.

Safety on the same worlds: 44 of 48 complete, four given up on the class pin
(a pin miss, not a crash), zero deaths, movement owner on every completed
world, **30,406 parity comparisons with zero mismatches and zero refusals**.

**Arm B does not run.** Its precondition was objective-flag contact leading
the rescue refusal census; the leader is `corridor` at 726 of 1,458, so the
item-branch repair is not indicated by this evidence.

The order's failure branch names **whole chains validated as chains** as the
next explorer, and the census reads that way - an admitted single hop is most
often refused later for `corridor` or `slower_route`, which is what a flight
with no validated continuation looks like. That is a proposal, not a
decision: **no explorer is built before the owner picks one**, and
`bot_moveLibraryRescue` stays default off until the owner says whether to
promote it (it cannot be promoted on this result), delete it with its code,
or keep it for a variant. The arm's exact sources are retained beside the
screen in `arm-sources/`, hash-matching the build manifest.

Keep successor aiming and rerouting enabled, exit-speed weight 500, state
preference enabled, `bot_moveSuccessorFan=0`, and no further candidate-fan or
selection screens. The paragraphs below record how the coverage diagnostic
reached this point.


Latest completed experiment: [plan-coverage diagnostic](2026-09-21-plan-coverage.md),
the read-only work item the level-1 exit gate asked for first. No native run,
no source change, no rebuild. Over 60 waiting runs in the sixteen
successor-aim confirmation worlds, the seeded library admits an edge at **34
(56.7 %)**; the committed control is **66 of 107 (61.7 %)**; **no waiting
state is unvisited** and none is off the graph; the 26 uncovered runs each
fail exactly one axis - heading cone 18, takeoff reach 4, speed band 4 - and
fifteen of the cone margins exceed 110 degrees, so they are not near misses
and widening the cone is not indicated. Coverage is entirely tap-dependent
(0 of 60 with the tap bit cleared). The heading is the one state component
not retained at frame resolution: it is reconstructed, and the two
reconstructions differ by a 7.7-degree median and 18.9-degree worst, enough
to move three cone cases and no more. The tool reproduces the game's own
logged `envelope=` verdict on 130 of 150 records, and 18 of the 20
disagreements resolve once the unlogged cell choice is relaxed. Each
confirmation world's own cache is the seeded library to within four edges, so
that column is not independent evidence.

That is the decision table's **first branch** - the library applies and the
live pipeline refuses it - whose next work is native re-validation of library
plans with a trigger-aware admission. Read the qualification before acting on
it: the committed control is covered no less often than the waiting frames,
so coverage does not by itself demonstrate a speed opportunity, and the
owner's 2026-09-21 deprioritization of the trigger admission repair is
untouched by this result. **Do not build an explorer before the owner picks
one.** Keep successor aiming and rerouting enabled, exit-speed weight 500 and
state preference enabled, `bot_moveSuccessorFan=0`, and no further
candidate-fan or selection screens.

Gate row 3 was regenerated on the same worlds under the corrected accounting:
**993 of 1,834 grounded frames waiting, 54.1 percent**, against the gate's 15
percent, with 1,374 of 1,885 candidate refusals there carrying the
trigger-contact bit. Tool: [movement_plan_coverage.py](../../../tools/movement_plan_coverage.py);
reports in `.codex-research/plan-coverage-20260921-a/`.

The [second-hop fan screen](2026-09-21-successor-fan-screen.md)
before it stands: the +/-3-degree landing-only offer regressed class 150 by
575 ms and class 125 by 1,450 ms in both screening pairs, no confirmation was
launched, and `bot_moveSuccessorFan=0` is the baseline.

The [trigger shadow measurement](2026-09-21-trigger-shadow.md) and
[rescue follow-up](2026-09-21-trigger-shadow-rescue.md) are corrected after
review: `swept` tests an enclosing axis-aligned box against brushes, not the actual
swept hull. Its 107/60 `swept_not_endpoint` cases remain unresolved; they are not
proven protective crossings. The 39 straight-search blue-flag verdicts are initial
`arrives` conversions, not final admissions or coverage of all 51 census
disagreements. Later chain/path gates and other rollout paths remain unmeasured.
Keep trigger admission conservative and the default-off diagnostic off for speed
runs. Deprioritize an admission repair because no speed payoff is demonstrated;
this is a priority decision, not proof that only 30 refusals are recoverable.
The diagnostic changes throughput, so its absolute counts describe instrumented
searches and stable observed rates are not guaranteed for uninstrumented runs.

The review requires a default-off diagnostic with admission unchanged; the
first-stop census is not a measured speed opportunity. The parity mixed-refusal
guard is fixed; [review-fix validation](2026-09-21-parity-review-fix.md)
records the focused regression and fresh native checks. Fresh guarded build: 36/36
floor plus both seeded cases pass, zero deaths, movement owner only, **19,898
reported comparisons with zero mismatches and refusals**. Sources stayed frozen.

**Owner direction, 2026-09-20, after reviewing the preference localisation and
ablation below.** It takes precedence over every next-action paragraph in this
file.

**Parity compares again: 19,413 frames, zero mismatches, and a control that
proves the zero.** [The diagnosis](2026-09-20-parity-zero-comparisons.md)
found why the harness had been inert: every parity build was refused with
`unsupported=256`, and matching each refusal to its trigger by frame index,
**119 of 119 were the objective flag** - the harness forecasts the walk, the
walk runs at the flag, the flag's pickup box is a trigger. A second defect sat
behind it in the sources: `RolloutRequest` points every rollout at
`parity.frames`, so the searches overwrite the armed prediction.
[The repair](2026-09-21-parity-repair.md) gives the build its own
frames and arms over the prefix of a rollout stopped only by a trigger,
tallying `truncated=1`. Floors on the repaired build: **36 of 36 and both
seeded regressions, zero deaths, movement owner only, 19,413 frames compared,
zero mismatches, zero refusals**, 41 of 63 tallies covering a committed
sequence. Restoring the shared buffer alone returns **935 mismatches, every
one `after_state`** - the exact comparison that reads the buffer after the
searches - so the harness reports disagreement and the zero is a measurement.
`compared` comes from the periodic tally and is a lower bound; nothing is
installed, and the harness still runs under `bot_moveParity`, which only the
bench sets.

1. **Park the preference-tuning branch**, keeping `bot_moveStatePrefer=1`.
   There is enough evidence to refuse both global removal and a speculative
   bounded-rank implementation; what the work established is that a decision's
   value depends on the policy that follows it, so a plain speed threshold has
   weak justification and its other firings are unmeasured.
2. **Restore meaningful forecast/execution parity comparisons.** Zero
   comparisons is a concrete qualification hole, and it has to close before
   any deeper forecast is added. **Done 2026-09-21**, see below.
3. **Aim the next speed work at classes 150 and 125**, which carry 77 of the
   93 declared weight. At the class-150 decisions sampled, selection already
   commits the fastest candidate offered, so the work there is **offering
   better continuations**, not choosing better among the same ones.
   **Opened 2026-09-21**, see below.

**The trigger is the most frequent first refusal; speed upside is unmeasured.**
[The offer census](2026-09-21-offer-limit-is-the-trigger-test.md) reports
5,995 trigger refusals, but stops there and cannot classify the remainder of
those trajectories. The item-toggle experiment completes 12 cells over 25
worlds at classes 150 and 125 with unchanged 17,575 / 15,925 ms outcomes;
removing medpak contacts exposes additional hurt contacts. The origin-versus-box
comparison does not establish false refusals, because the hull has volume and
the check spans motion. The shadow follow-up is measured with the limitations stated above.
Next is the bounded candidate-generation experiment; trigger admission stays unchanged.

Neither the localisation nor the ablation is a speed win, and neither shows
selection work as a whole is exhausted; what they bought is attribution and a
reason to stop a low-return branch.

For Opus: [focused experiment handoff](../../archive/movement-2026-10-05/OPUS_HANDOFF.md). The
[review corrections](2026-09-20-opus-review-fixes.md) supersede the
metric and causal claims in the earlier September 20 experiment records.

Storage tooling: [campaign-local staged binaries](2026-09-20-hardlink-staged-binaries.md)
is implemented for future worlds. Keep `.staged-binaries` masters immutable;
the helper shares copied, hash-verified snapshots rather than build outputs.
Existing worlds are not converted. This does not change the movement experiment
next steps below.

**Exit-speed 500 is confirmed against 2000 at matched verified starts.**
[The pinned confirmation](2026-09-20-exit-speed-pinned-confirmation.md):
four declared start strata, six matched pairs each plus a three-pair anchor,
53 timed worlds out of 141 native worlds, one cell incomplete and reported as
such. Every cell produced one distinct completion time. 500 wins 1,475 ms at
class 150 and 525 ms at class 125, ties at class 100, and **loses 1,025 ms at
the slow class 75, six pairs of six**; its worst cell is 21,125 ms against the
incumbent's 20,100. Weighted by the earlier cohort's declared start
frequencies the improvement is **807.8 ms** (17,394.4 against 18,202.2 ms);
weighted equally over the four strata it is 243.75 ms. Search cost is
unchanged - pooled logged rollout median inside the completed trip 36.85
against 38.70 ms. Floors at 500,
unpinned: 36/36, zero deaths, movement owner throughout, both seeded named
regressions pass. 500 remains a **research configuration value**, not
installed; 15 seconds, runtime and parity qualification remain open, and
parity still makes zero comparisons.

**The start level is a class, not a level.** The pre-start play log steps
250 ms, so a world can only start on its own ladder `L0 + 250k` and `L0`
varies; exact pins at 3325, 3300 and 3275 each missed 4 worlds of 4 today.
`corner_level mod 200` explains all 100 worlds of the retained sweep, and the
grid confirmed it prospectively: 53 worlds on 14 distinct absolute levels, each
landing on its class's time. `tools/movement_bench.py` gained `--corner-class`
for this; `--corner-level` is unchanged and floors stay unpinned.

**The selection failure is found, and a forced-candidate diagnostic isolates
it.** [The slow class's selection failure](2026-09-20-slow-class-selection-failure.md):
at start class 75, relative level 4,775 ms, a straight commit offers 18
candidates and admits 12, the rollout list is byte-identical across arms, and
at weight 1000 the top three score 660.1 / 660.1 / 660.0.

`bot_moveSelectNth` / `bot_moveSelectCand` (default off, build
`.codex-research/select-override-build-20260920-a`) commit a named admitted
candidate at a named sequence commit and leave every other decision alone.
With the policy fixed at `bot_moveExitSpeed=2000` in every arm, three
repetitions each: control 20,100 ms, forcing the ranking's own winner 20,100,
forcing cand 5 20,925, **forcing cand 1 18,775**. One frame's choice spans
**2,150 ms** under one policy and the ranking takes the middle; it leaves
1,325 ms against its own admitted set. The control reproduces the reference
build to the millisecond, the floor is 36/36 with the override off, and both
seeded named regressions pass.

Two earlier weight-derived claims are withdrawn by that experiment: later
decisions are **not** worth "at most 250 ms" - the same candidate completes
19,250 under a weight-1000 policy and 18,775 under weight 2000, 475 ms from
later ranking alone - and nothing here says a bounded continuation would fail.
What is measured is the horizon: on this frame a one- or two-decision
lookahead over the realized continuations ranks the worst trip first and the
winner only leads from the fourth decision, about six seconds out. So
**measure the horizon a bounded rank needs before building one**, on this
frame and on others; experiment 6 is repriced, not refused.

**Repeated under 500, the same frame is worth 2,300 ms and the ranking takes
the worst.** [The exit-speed-500 repeat](2026-09-20-select-override-exit-speed-500.md): 12 cells of 12, 34 native
worlds, three repetitions an arm, `bot_moveExitSpeed=500` fixed in every arm.
Control 21,125 ms - the reference build's class-75 value, so the diagnostic is
inert when off here too - forcing the ranking's own winner 21,125, forcing
cand 13 19,950, **forcing cand 1 18,825**. The 1,025 ms that 500 loses at
class 75 is mostly this frame: the choice between the candidates the two
weights take is worth 825 ms under the 2000 policy and 1,175 ms under 500,
the rest being the later decisions. **cand 1 is admitted under both
policies**. On the same committed candidate the two policies' later decisions
differ by at most 200 ms, against the frame's 2,300; adding the weight-1000
band, cand 1 spans 475 ms across the three weights, not monotonically.

**The horizon is priced, and it is about half the remaining trip.** Ranking
the three realized continuations by distance still to run at common
elapsed-time endpoints from the frame, the order is stable only from
**frame + 7,000 ms** under both policies, and at **frame + 5,000 ms the true
winner ranks last** under both; the leg after the frame runs 14,000 to
16,350 ms. Correct endpoints do occur earlier, +3,000 at 500 and +2,000 at
2000, but the order flips back, so a cut there is an accident of where the
flight happens to be. Cumulative progress is worse: the winner never leads
inside six decisions at 500. This is an **oracle** over realized continuations
under the full live policy - it bounds a bounded forecast rank from above and
says nothing about its cost. Next: build the bounded rank, measure its
accuracy and CPU against this oracle, and check other frames and start classes
before choosing a scoring rule.

**Every frame tested at the slow class leaves seconds, and the score does not
order the trips.** [The frame survey](2026-09-20-selection-across-frames.md): 33 cells of 33, 94 native worlds,
the same override at commits 2, 4 and 5. Commit 5 admits exactly four
candidates, **all four were forced, and the ranking took the slowest**; the
frames carry 1,525 to 2,300 ms each and the ranking's own choice is worst or
near-worst at 4 of 4. At commit 5 the best trip has the **lowest** score, two
candidates are the **same point** in progress and carried speed yet complete
600 ms apart, and the executed candidate is not the score maximum at any of
the three frames. The spans are **not additive**: each forced commit changes
everything after it.

**At the fast start class the ranking is already optimal, so this is not a
general failure.** [The second start class](2026-09-20-selection-second-start-class.md): at class 150, three commits
admit 3, 4 and 2 candidates, **all forced exhaustively**, and the ranking
commits the fastest at 3 of 3 - the score maximum every time, ordering four
completions exactly including a 2,475 ms cliff, and separating two candidates
with identical progress by score alone. The frames carry 1,000 to 2,575 ms and
the incumbent captures all of it. A bounded rank has nothing to win here and
must be shown not to regress it.

**The difference between the classes is the planned-candidate preference, and
removing it is worth 3,150 ms at the slow class.**
[The preference arms](2026-09-20-state-preference-costs-the-slow-class.md): at class 75 the candidate that executes at commits
4 and 5 is a **state** candidate scoring below an admitted straight candidate
that is faster; at class 150 the executed candidate is the score maximum every
time. With `bot_moveStatePrefer=0`, three repetitions an arm, 12 cells of 12:
class 75 **21,125 to 17,975 ms**, class 150 17,575 to 17,725. **Floors pass
unpinned at that setting: 36 of 36, zero deaths, movement owner only, both
seeded named regressions.** 17,975 beats every frame-forced arm of that leg,
because the preference overrides at more than one commit.

**Collected, and removal is refuted: it loses 1,162.9 ms weighted.**
[The full grid](2026-09-20-preference-removal-refuted.md) adds the two missing classes, 12 cells of 12 over 38
worlds, and the direction reverses: class 125 **+3,025 ms** and class 100
**+1,775** with the preference off, against -3,150 at class 75 and +150 at
class 150. Declared weighting 17,394.4 ms on against **18,557.3 off**; equal
weighting 18,500 against 18,950, so the direction does not depend on the
borrowed weights. The regressing classes are **93.5 percent of starts** and
would have to fall to 68.3 to break even. All four `StatePrefer=1` cells
reproduce the confirmation's 500 column to the millisecond, so the control
here is the confirmation's own. Search cost is unchanged: pooled in-trip
`rollout_ms` median 36.50 ms on against 35.05 off.

**`bot_moveStatePrefer=1` is confirmed, not replaced.** The preference is
right at three classes of four and wrong at one, so the target is a preference
that is **conditional at the decision**, not a global retuning or removal.
That is the same shape as the frame evidence - at class 75 the executed
planned candidate scores below a faster admitted straight candidate; at class
150 the executed candidate is the score maximum at every frame measured. It
**re-elevates bounded continuation ranking**. The 7,000 ms horizon often
quoted against it belongs to **a different decision and one ranking rule**:
it was measured at commit 3 of the class-75 leg, ranking realized
continuations by distance still to run. It prices that rule at that frame and
is not a measured requirement for a rule at commit 2. That price is now weighed against a 1,162.9 ms weighted loss
for the cheap alternative, not the 3,150 ms gain two classes suggested.

**That commit is worth 1,900 to 5,300 ms, and its right answer is not the
same at two classes.** [The ablation](2026-09-20-preference-decisive-commit-priced.md)
forces that one frame in both directions with the existing override: 36 cells
of 36, 93 native worlds, **all six controls reproducing the preference
collections to the millisecond**, zero deaths, movement owner only. At class
75 the state plan is wrong from both sides - holding the judge's winner saves
**1,900 ms**, forcing the state plan into the off trip costs 1,425. At class
125 it is right from both sides - holding the judge's winner costs **5,300
ms**, more than the whole class delta. **At class 100 the two sides
contradict**: holding the winner is 150 ms faster, yet forcing the state plan
into the off trip saves 3,050 and is the fastest class-100 trip in the grid.
So the frame is worth seconds and its value is **policy-dependent and not
additive**. The root-speed reading survives on the two clean labels - wrong
below the ground walk speed, right at it - and class 100 neither confirms nor
refutes it. A rule that only declined the preference at the class-75 decision
would be worth **122.6 ms** weighted (17,271.8 against 17,394.4) and 475 ms
equally weighted. Best of the four policies measured here, chosen per class,
is 340.3 ms weighted but **1,106.25 ms equally weighted** (17,393.75 against
18,500): the same combination priced two ways, so that figure is mostly the
borrowed start distribution, and **best of what was tested is not a ceiling**
on selection - the other admitted candidates, the later firings and their
interactions are untested. The rule would also not be a no-op elsewhere,
because firings at 62.5 to 211.2 units/sec exist before the timed start and
class 150's first in-trip firing is at 242.1.

**The preference acts at one commit per leg, and the classes differ there
by root speed.** [The localisation](2026-09-20-preference-localisation.md)
reconstructs the judge's own ranking from the retained `[SPIKE]` rollouts of
both preference collections: 378 commits over 24 worlds, 141 of them exactly
reconstructible, 120 reproducing the logged score, **21 preference firings
and none unexplained**, and no firing at all in a preference-off arm. The
earliest in-trip firing is the largest at every class that has one, and at
classes 75, 100 and 125 it is the **same decision** - commit 2, about
2,950 ms in, on edge 36420. What separates the losing class there is the
**speed the bot arrives with: 99.1 units/sec against 250.0, 250.1 and
242.1**. The score gap does not separate them - 22.1 at the loser, between
14.9 and 30.8 at two winners - and neither does domination: the preferred
plan is worse on both score terms at class 100, where it wins 1,775 ms. It
localizes and does not price; the ablation above prices that commit and
supplies the positive and negative pairs a conditional rule is fitted to.

Two classes are not a grid: 75 and 150 are the extremes of the completion
range and agreed on a direction that the two between them reverse, carrying 44
of the 93 weighted starts.

No weight near 1000 is a proposal: only 500 and 2000 have floors, and 1000
loses at other start classes. Corrected budget accounting separates
uncommitted ground from observed search refusal; terminal walking is not
evidence of failed planning, and the prior 54.2% waiting attribution and
speed-preservation lower bound remain withdrawn. No installed configuration
changes.

Updated 2026-09-20 after the [successor-aim experiments](2026-09-20-successor-aim.md). The owner's
[refined method](2026-09-19-refined-method.md) takes precedence over
every earlier next-action paragraph below. Current design:
[ARCHITECTURE.md](../ARCHITECTURE.md); gates: [PLAN.md](../PLAN.md).

**Goal:** one movement system capable of useful independent strafing from the
actual live state, with prepared routes as optional faster plans. Aim for
roughly 15-second stationary no-Force stand runs under matched conditions;
this is unachieved. The latest eight fresh counterbalanced pairs test
`bot_moveSuccessorAim=1` on the earlier reroute1/exit-credit2000 configuration:
**mean 17,943.75 versus 18,840.625 ms**, median **18,125 versus 18,512.5**,
maximum 19,375 versus 20,350. Six pairs improve, two regress. The 16,450-ms
outcome repeats three times, not a 16.45-second median. The earlier 17,900-ms
median is a different cohort, not the current matched control. Experimental
settings remain default off. Both 36-case floors and two seeded regressions pass; search cost rises from
median 22.2 to 38.9 ms and parity still makes zero comparisons. This is not
production qualification. The 8-unit run-up regressed and was reverted;
component separation did not establish a better replacement configuration.
Do not wait for new respawn
recordings or general route rejoining.

## What the diagnostic settled

[The guided corridor](2026-09-19-guided-corridor.md) closes steps 1
and 2 below. Steps 1 and 2 are done; read that record before starting step 3,
which it rewrites.

- The repaired control binding is verified and matched: build
  `review-fix-build-d` against the checkout, `red_out_L1_stand`,
  `--edges --parity 256` with the `move-explore-20260916-b` seed, observed
  search activity, `force 0`, and the bench world's own `pmove_fixed 1 /
  pmove_msec 8 / pmove_float 1 / sv_fps 40`. **The bench and the recorder
  already share that physics binding**; the earlier mismatch note was wrong.
- Guided onto a human route's own coarse line, generating its own commands and
  carrying native successor states, the search runs **20,500-20,675 ms**
  against the human's **14,800** on that line, and **19,150-19,600** unguided.
  The corridor is not the limit, and the human's line is not the fastest line
  this vocabulary has.
- The bot gives up 180-190 units/sec at about ten joins a run, roughly 1,650
  units/sec in total, where the human pays nothing (93.2% airborne to the
  bot's 81%). **Those refusals are three different things, not one.** Over
  262 candidates in an instrumented run: 91 admitted, 92 clean rollouts
  rejected only for leaving the planned path (`unsupported=0`), 39 stopped on
  an item or flag contact - unknown rather than bad - 18 genuine falls into
  the map's hurt volume, 22 on mixed frames. `unsupported=256` is
  `BOT2_FORECAST_TRIGGER`, a rollout stopped on trigger contact; **it does
  not mean the landing failed**, and an earlier version of this handoff said
  it did.
- By class, the speed given up divides as: about 1,028 units/sec over four
  joins where the fast flight really does fall into the pit (**that refusal is
  correct and stays**), 629 over four joins that are pure selected-path
  rejection, 106 at one join where the rollout is refused for touching the
  enemy flag - the objective - and 126 at one join that is never searched at
  all. 226 of the run's 312 trigger contacts are that enemy flag.
- **Landing-frame coverage is now complete.** It was not: the `kind=skip`
  diagnostic was gated on the onward stage and 2 of 13 last-hop landings
  reached nothing and recorded no reason. Every branch that can take a landing
  frame now names itself, including the three silent exits inside
  `StraightSearch`. Over three guided runs on `move-cover-build-c`: **40
  last-hop landings, 33 searched, 4 `straight_too_short`, 3 `off_path_index`,
  0 silent** - and all seven named skips are at 352-446 units/sec, the fast
  landings that matter. Both reasons are path causes, so with the 92 `off_path`
  candidate rejections, **all three ways a fast landing loses its continuation
  trace back to the planned path, not to the maneuver.**
- **The selected-path rejection is measured and it is not the win.**
  `bot_moveReroute=1` re-plans from each candidate's measured landing and
  re-judges it. Over eight sequential repetitions per arm it keeps **61% of
  joins on the landing frame against the control's 53%** and gives up 1,527
  units/sec against 1,591 - and the completed stand time does not improve
  (median 20,362 against 20,150, mean 20,356 against 20,031). `=3` is worse on
  every measure. **Speed kept at a join is not time saved.**
- **A sampling rule, learned the hard way.** The first read of that experiment
  was three parallel repetitions per arm and showed `=1` 925 ms faster,
  reproducibly. It was an artifact: worlds in one parallel batch land on the
  same outcome with identical counts, while sequential runs of the same build
  spread about 1,550 ms. Measure a timed arm as sequential, alternating runs;
  parallel batches stay fine for the 36-case floor.

## Next actions, in order

1. Done - the matched repaired binding above. `--edges --parity 256` is the
   search-enabled fixture; `bot_moveRewrite` alone does not invoke hop
   discovery. The parity gate is an implementation boundary to remove during
   runtime integration, not a desired dependency. Do not count a case with
   zero search activity as a test of independent hop search.
2. Done - the guided corridor diagnostic. `tools/movement_corridor.py` builds
   the polyline, `movement_bench.py --corridor` stages it, `bot_moveCorridor`
   loads it; unset, every search is unguided and unchanged (measured: run for
   run identical to the repaired control).
3. Done - the landing frame is auditable and the instrumentation is inert
   (`move-cover-build-c`, floor 36/36 green, the guided arm reproducing its
   reference runs to the millisecond and to the commit counts). One rule came
   out of it: **never print inside the search.** Doing so cost 2,000 ms
   reproducibly; the reason is now recorded by the search and printed by the
   caller after it returns. The mechanism is unresolved - the rollouts are
   bounded by step counts, not by a clock - so the rule is empirical.
4. Done - the selected-path rejection is measured and this pass does not
   shorten the trip. Review then found two defects in the pass: the installed
   route could belong to a different candidate, and the continuation it
   forecast was not the one it executed. Both are fixed and neither is
   measured, so the arm does not rule the rejection out as a target. See above.
5. Done - the corrected pass was re-measured against the control, nine
   sequential alternating repetitions (one dropped from both arms for an RCON
   fixture refusal): median 19,550 against 19,600, mean 19,669 against 19,569,
   **indistinguishable**. It installs 11 routes against the defective pass's
   18 and both new guards stayed silent. The joins-kept metric is unqualified
   and should not be quoted until it is defined in code.
6. Corrected - [what the kept speed buys](2026-09-19-what-the-kept-speed-buys.md).
   The earlier coarse distance-bin and heading conclusions are withdrawn
   pending a reproducible outbound-only analysis. The reviewed 40 Hz analysis
   requires every sample through landing+500 ms before the last observed
   noncarrying sample and excludes fixture-level telemetry. It retains 58
   landings, not 67. See [review resolution](2026-09-20-review-resolution.md)
   for the documentation correction and separate code regression repair.
7. Corrected - two distinct outbound traces, not three independent trajectories:
   r1/r2 have identical 760-row safe-window momentum records. Pooled airborne
   landings average 403.7 -> 403.9 units/sec after 25 ms; grounded landings
   average 445.4 -> 342.9, reaching 257.3 by 100 ms. Distinct-trace counts are
   22 airborne / 17 grounded. This supports a grounded-loss association,
   not a universal causal statement about every landing or heading.
8. Corrected - the pooled 25 grounded landings split into 13 commits,
   5 searches admitting nothing and 7 without a recorded straight search; keeping
   each distinct trace once gives 10/3/4. Two grounded commits have an admitted
   tap-now proposal; all grounded commits select the 24-unit run-up.
   Classify by committed `hops`, not `hop0`: within-chain continuation is
   26/26 pooled (18/18 distinct), while sequence endings continue airborne
   7/32 pooled (4/21 distinct), including endings of one-hop plans.
9. Done - [the landing-search investigation](2026-09-20-landing-search.md)
   matches all four retained missing-search states to `off_path_index`.
   Admission accepted a later strafe chord, but the live index stayed at the
   takeoff. Final touchdown now reconciles a failed lookup inside the existing
   admission bounds; ordinary walking and safety gates are unchanged.
   Actual-C regressions pass and the preserved baseline fails. All 36 standard
   cases pass. Eight alternating pairs show no speed win (numbers above):
   unsearched grounded windows halve from 20 to 10, but airborne sequence
   endings remain 21/91 versus 22/95. Two separate corrected replay runs have
   no off-path skip; the reproduced first state instead says `straight_too_short`.

   The short-straight follow-up is now complete in step 10. Separately identify the trigger
   entities in the tap-refusal cohort: its 52 proposals split into 23 selected-
   path rejections and 29 trigger-contact unknowns, not 52 failed landings.
10. Done - [short-landing proposals and the speed win](2026-09-20-speed-win.md).
    At final flight landings, offer the existing arc-aimed candidates even
    before a nearby bend; preserve the goal gate and every native admission
    check. The fix alone has only a marginal timing effect. Combined validated
    rerouting and exit-speed ranking on that build produce the eight-pair win
    above. Preserve the frozen build/configuration as a research reference.
    The first actual-native-successor follow-up is completed in step 11;
    positive parity and CPU scheduling remain separate qualification work.
    No installed-game change or production-default enablement is implied.
11. Done - [actual-landing second-hop proposals](2026-09-20-successor-aim.md).
    A guarded native first-flight measurement supplies the second heading's
    origin; full original-root rerolls retain native continuation and all
    admission checks. Eight pairs show the improvement above. The intermediate
    run-up and reroute/credit component screens are completed and retained.
    **Next:** explain the slower successor outcomes, improve useful connected
    continuations toward 15 seconds, and avoid repeated first-flight work
    before increasing search breadth. Qualify CPU budget and positive parity
    separately; no new recordings or universal rejoining are prerequisites.
   Preserve pit and unsupported-contact gates. Measure physical next-frame
   airborne state, run-up, plan length and complete sequential controlled runs.
10. Separate production search scheduling from parity diagnostics and qualify
   an explicit live CPU budget, including multiple bots. Keep native validation
   and the shared execution step. No production or installation claim follows
   from a successful offline diagnostic.
11. Integrate prepared plans as candidates under that same lifecycle, starting
   with known standing entries and counting entry cost. General rejoining and
   contention follow separately; joins need a validated connection plus route
   continuation, and may simply be refused while independent travel continues.

## Boundaries and deliverables

Separate graph paths (approximate proposals), executable maneuver sequences
(validated continuations), and prepared plans/tapes (retained sequences).
The existing explorer harvests edges; it is not yet a full-route optimizer.
The tape player remains a research fixture, not the runtime owner to extend
with another controller chain. Extract explicit search inputs/results as work
requires; do not start a broad rewrite or add a new scoring switch by default.

Keep original recordings, assets, failed worlds and measured sources intact.
Use fresh source/binary/settings/case bindings for changed native experiments,
freeze sources during collection, at most four isolated outcome worlds, and
timing runs alone. Follow the existing build procedure below. No installed-game
changes are in this milestone. Record the diagnostic result even if it fails,
then update the next action from evidence. Closure requires unguided movement
quality and runtime-cost/lifecycle checks separately; prepared-route times
cannot satisfy the independent-search target.

## Completed repair baseline

The [follow-up review](2026-09-19-repair-follow-up.md) verified retained
checkpoint and stale-slot evidence and found two additional P2 items: loading
a replacement tape can leave the prior AI held, and the ownership probe can
pass a phase without observing its requested arm. Fix these before claiming
full tape lifecycle coverage; they do not block the independent-search
diagnostic. The original four repairs remain closed. One standard repaired
floor attempt is 35/36 due to setup failure, and the ordinary valid-arm probe
phase was a no-op; keep those qualification limits visible.

The four review findings are repaired and measured:
[the repair record](2026-09-19-review-repairs.md) closes
[the work order](2026-09-19-review-fixes.md). Each repair has a
regression that failed against the defect before it passed, on fresh paired
builds and regenerated assets:

1. Candidate results are fully reset between searches; the judge audits what
   it is handed and reports `stale_slots=`. 0 on the repaired build, 372/200/396
   on a paired defect build.
2. The first tape command carries its own recorded physics duration. Latent in
   the three retained assets - their first command is 8 ms, which is what
   playback was substituting - so no retained route time was affected.
3. Checkpoints are read after the command that produced them. This was
   inflating drift: the three routes reproduce their recordings to
   **0.29-0.35 units** where the retained runs reported **23.8-37.8**, with
   every route time identical.
4. A refused arm releases the AI. Arming validates before it holds, rolls the
   placement back, and `stop` works after a refusal.

The tape/asset format is versioned `BOTROUTE2`/`BOT_ROUTE2`; `BOTROUTE1` is
refused by name and the conversion is a rebuild from the retained recording
CSV. Retained assets, worlds and approvals are untouched.

**One number the repairs sharpen:** the follower's harm on the line was
measured at 78.3 drift against open loop's 37.8, a ratio of two. With
checkpoints read correctly it is **114.2 against 0.294** on the same route and
build. The cause is unchanged - cross-track against the chord between marks -
and `bot_routeFollow` stays default off and unqualified, but the case against
that error signal is much stronger than the retained figures made it look.

**One thing to carry forward before quoting the search experiments:** finding 1
was a live correctness defect in the reroute and tap passes' own arms. Whether
any retained reroute/tap result was changed by it is not established; compare
those experiments against repaired controls before carrying their causal or
ranking conclusions forward. The older chain-entry and onward-held defects are
untouched and still open.

Additional demonstrations from `(-640,1696,1808.125)` and
`(-320,2624,1368.125)` remain useful optional evidence. They are not the next
dependency; independent connected search proceeds first. Contention remains
unqualified because the route measurements use empty worlds.

# Retained status: a recorded route runs from the benchmark stand

Note the correction the diagnostic made to the paragraph below: the search
figures and the route runs share `pmove_fixed 1`, `pmove_msec 8`,
`pmove_float 1` and `sv_fps 40`, which `Svcmd_AddRunner_f` sets in every bench
world. The two halves of that comparison do not span different physics
settings. The other limits it lists stand.

**Latest:** [the route prefix](2026-09-19-route-prefix.md). The bot
walks from the benchmark stand into a recorded route's start and then plays the
tape unchanged, with nothing steering it. **All three human routes reach the
enemy flag from `(152,-1160,1924.125)`** - `red_out_L1_stand`'s own coordinates
- in **14,875, 15,350 and 19,000 ms including the walk**, three repetitions
each, every route time equal to its recorded time and every hand-off inside 0.7
units at a standstill. The bot's own search from that stand is a **21,150 ms
median over 60 retained floors** (best run 19,150), so the fastest route is
**6,275 ms under the median**, and this is the first stand-to-flag number in
this work under 15 seconds.

Read the comparison's limits before quoting it. The route world runs
`pmove_fixed 1` and the search figures were taken **without** it, so the two
span different physics settings; `red_out_L1_stand` declares `force 0` where a
prefix run carries the recording's pool of 100, which nothing in either half of
the run spends; and everything after the hand-off is open loop on an empty
world. `bot_routePrefix` is default off and playback with it unset is
unchanged - the control reproduces 14,825 ms and 37.832 max drift.

Two findings worth carrying. **The planner arrives at a cell, not at a point**:
the first prefix run walked 12.6 units and then stood 23.1 units from the
tape's start for 29.5 seconds with no hold event, because the walk's hold
detector only fires when the objective is more than 64 units away. The prefix
closes the last 96 units itself, which keeps the movement floor untouched. And
**closing on a point is a braking problem**: a full-throttle waypoint intent
parks 13.3 units short on command quantisation, a wish-limited proportional law
creeps and then parks at its dead radius, and only `sqrt(2*600*d)` - the speed
whose coasting distance is what remains - arrives.

**Selection is measured, and it is narrow**
([where a route is worth taking](2026-09-19-route-selection.md)).
The same route entered from the two red outbound respawns loses to the search
by **6,825 and 17,125 ms**, because its start is 2,593 and 3,543 units
*behind* them; the walk alone costs 12,275 and 20,625 ms. Joining the tape
part-way does not rescue that: their nearest point on its line is 1,358 and
1,598 units off it, in a **727-794 units/sec flying state** the walk's 250
units/sec ceiling cannot deliver. This excludes those walking entries; it
does not establish that every moving entry is impossible. Only the standing
entries are currently demonstrated. Independent movement must cover the
other cases without requiring a new recording for each.

That measurement also corrects a baseline this work has been quoting: the
stand's search time is a **21,150 ms median over 60 retained floors**
(19,150 to 36,200), and 19,150 is the best run of one 18-repetition arm.

**Earlier recording request, now optional evidence:**
demonstrations started at `(-640,1696,1808.125)` and `(-320,2624,1368.125)`,
the two red outbound respawns, which would make the route line cover three
fixture cases instead of one. Then contention, since every route measurement
so far is on an empty world. A suffix index can represent a playback offset,
but does not establish a physical entry. The refined method requires a native
connection and ensuing route continuation from the actual live state. The
follower stays unqualified. The two older defects below are still open and
still block the search's own line of work.

**Previously:** [the follower, and what playback already tolerates](2026-09-18-route-follower.md).
The follower does not yet make a route survive a foreign start, and the
measurement taken on the way matters more than it does. With the follower armed
at **gain 0** - off-start placement and the divergence abort, no steering -
open-loop playback completes `1789686390-1-c0` in the recorded 14,825 ms from
**28 units of along-track error either way**, and **fails at 16 units
cross-track**. Starting ahead or behind on the line is a phase offset the tape
absorbs; starting on a parallel line puts the recorded takeoffs at the wrong
ledges.

The follower itself hurts on the line (78.3 drift against playback's 37.8),
helps off it (y+16: 326 to 95, and it survives the whole tape instead of two
seconds), is non-monotonic, and never reaches the flag. The cause is the error
signal: cross-track measured against the chord between contract marks reads the
recording's own curve as error. Interpolating along that chord by time **cannot**
fix it - the interpolated point is on the chord, so only the along-track
component changes - which is why the retry reproduced 78.283 exactly. A correct
signal needs the recording's true position per instant; **the contract stores a
mark every 500 ms and was designed to check a replay, not steer one**, and its
`k` line drops the velocity vector too.

**The measurement argues for the prefix over the follower**: the walk only has to
land inside ~16 units in the sensitive direction, which the existing planner can
hit, and then plain playback runs the route. `bot_routeFollow` and
`bot_routeFollowGain` are default off and not qualified.

**Previously:** [the walk-through repair is refuted](2026-09-18-walk-through-refuted.md).
An attempt to fix both blocking defects as one root cause - the walk's
lookahead ending on a strafe chord's takeoff, so the bot arrives at rest -
is **safe and ineffective**: four seeded arms 12/12 with zero mismatches,
but stale edges 45 to 44, onward-held 138 to 138, and +4,412 ms inside a
documented 7,100 ms same-code swing. `bot_moveWalkThrough` is default off and
**not qualified**.

The diagnosis generalised from one atypical log line. The measured mechanism:
the typical stale edge has `live_speed` **250** (the walk's wishspeed ceiling)
against `entry_speed` **400** - those are downstream links of a chain, and a
walk cannot reach 400 by any steering change. This is chain breakage, and the
repair must put the bot at a chain's takeoff *flying*. The dead-end landing is
untouched and frequent: **138 onward-held events in a 12-case arm that passed
every case**.

Two corrections that affect how evidence here is read. **The floor is
stochastic**: of 60 retained standard-configuration 36-case floors, 6 (10 %)
have a death, and all 20 deaths fall in those 6 runs, so a single 36-of-36 is
about a 90 % outcome and weak evidence of safety either way. And **four
measurement errors are retained in that record** - a wrong seed, and
off-configuration runs (`--parity 0`, no `--edges`) that produced two stated
conclusions which were false. The standard arm configuration is
`--edges --parity 256` with seed `move-explore-20260916-b`. The later repair
record narrows the retained search-active population to 44 floors, 40 clean;
do not reuse the older 60-floor safety denominator as a configuration-matched
population. The historical stand-time population is separately identified.

**Previously:** [the search budget](2026-09-18-search-budget.md) measures
what a rollout costs, as step 0 of generalising the route assets into an
offline route compiler. The offline explorer is **CPU-bound, not frame-bound**:
an eightfold batch increase moves its rate by one part in six hundred. One core
runs about 62,000 simulated command frames a second - at the fixture's 25 ms
frame, about **1,550x real time for those short rollouts**. The record's
103 full-route rollouts/sec/core and overnight 1.2e7 estimate extrapolate
constant cost per frame; complete-route search and multi-bot runtime cost were
not measured. Use the rate to size the next experiment, not to declare compute
unconstrained. Counters only were added in that record; 94 tool tests passed
at that date.

**Previously:** [route assets](2026-09-18-route-assets.md). On the owner's
direction the approach changed: adapt a human recording into an approved bot
route offline, and execute it, rather than extending the online search to find
the same line. **The bot completed the fastest demonstration in 12,900 ms
against the human's 12,875**, on the recorded line, reproducibly; all three
demonstrations are approved. Read the limits first - each route starts where
its recording started, and the benchmark stand is 392.8, 35.7 and 9.0 units
from those starts, so 12,900 ms is not a stand time. The comparable one is the
route starting 35.7 units out: 14,825 ms against the search's 19,150. Playback
is open loop and reacts to nothing. The follower, and a prefix from the stand
into a route, are what make this usable from the fixture.

**Previously:** [the exit-speed term](2026-09-18-exit-speed-term.md) adds
the term the record below found missing: `bot_moveExitSpeed` (default off)
credits a candidate's measured exit speed as distance carried over that many
milliseconds and ranks on the margin plus that credit. Admission is untouched.
It is **safe** - floor 36/36 unset, at 1000 and at 2000, regressions pass, no
death in 105 stand runs, the first of this series of which that is true - and
it **does not improve the stand times**: medians move at most 250 ms while
means and maxima get worse. The telemetry says why: the committed run-up mix
barely changes (87/104 to 90/96), because at 66 of the 81 breaks there was no
admitted immediate candidate for a ranking term to promote. The metric is now
right; the binding constraint is upstream of it. The untested pairing is
admission at the break plus this ranking.

**Previously:** [where the stand time goes](2026-09-18-where-the-stand-time-goes.md)
locates the deficit, from the retained worlds alone. The runs are **80.6 %
flight**, so the deficit is a launch-speed deficit. Across twelve control
runs: 227 landings, 107 broken chains, 18,429 units/sec lost in them. No
search ever runs while the bot is airborne, so a successor cannot be
committed before touchdown; and at **81 of the 107 breaks** the plan
committed carries a 24-unit run-up, which `HopStep` executes by commanding
`Wishspeed` - about 250 - while the bot is moving at 415. Four frames, 165
units/sec. Banning the run-up is not the repair: at 66 of those 81 there was
no admitted immediate candidate at all, the refusals dominated by `off_path`.
Where both were admitted the run-up candidate wins 11 of 15 on judged
progress, honestly - **the judge has no term for the speed a plan hands
forward**, and that speed is what buys the 80.6 %.

**Previously:** [the tap continuation, measured correctly](2026-09-18-tap-onward-measurement.md)
repairs two defects in the tap pass's own measurement - it read the
candidate's own hop as the successor, and its telemetry lost the walk's
refusal - and then measures the repaired pass. With honest measurement the
pass admits real continuations at 476-500 units/sec and **no stand median
moves outside the control's spread**; `bot_moveTapBudget`, which grants the
successor's flight extra frames beyond the walk's hold, does not move them
either and takes the floor to 34/36. Three deaths across both campaigns, all
in arms with the tap pass on, one on `red_ret_L1_stand`. Admitting the
continuation is therefore not what separates the bot from the human runs;
the deaths and the two older defects below are the work.

**Previously:** [reroute admission](2026-09-18-reroute-admission.md)
answers the landing probes' work order and measures it. `bot_moveReroute`
(default off, bit 1 admission, bit 2 route ranking) continues a candidate
rejected for leaving the path, admits it only when its landing opens a
planner route that beats the route it left, judges it against a third walk
reference of its own, and installs that route at the commit.

The candidates the probes isolated are now **committed and flown**: at the
jump-6 root the search takes a 735.0 units/sec landing instead of the run-up
that sheds to 486, and remaining pickup is 9.425 s against the control's
12.500 and 11.775 s, with no death and no parity mismatch. The default
36-case floor is unchanged at 36/36 with the cvar unset.

It does not pass the gate, and four arms were measured to find out why:
route-based ranking is the worst, admission alone is slower than the control
on the standard families, `bot_moveStaleAvoid` gave the best stand medians
and one 181-second stall, and restoring the planned-chain preference
(`bot_moveReroute 5`) gave the best red stand median in this work - 17,600
against the control's 20,700 - and then **killed the bot** on
`red_ret_L1_stand` and stalled `blue_out_L1_stand`.

**Two older defects are what block this, and both are reachable in default
builds.** First, a landed plan whose walk cannot cover `MOVE_ONWARD_DISTANCE`
(128 units) never clears its onward stage, and the replan gate waits on a
sequence in progress: `Replan` then never runs again. Measured twice today at
one pocket on blue, 174 and 177 seconds of a bot shuffling in place with no
path event, and measured again as a two-second hold in the arm's slowest run.
`bot_moveOnwardBound` (default off) bounds the symptom - floor 36/36 with it
on, and it fires at exactly the pocket where the holds and stalls happen -
but the walk that cannot leave the landing is the defect. Second, the
planner's arrival model and the executor disagree often enough to produce
runs of stale planned edges; `bot_moveStaleAvoid` (default off) bounds that
symptom and can strand the bot, because the chord it removes may be the only
way out.

**Earlier next action, retained for context:** fix the walk or the arrival model so a landing is never a
dead end, rather than bounding the deadlock; then re-measure admission with
the chain preference, because that arm was the fastest before it fell; keep
the jump-6 probe (9.425 s against 12.500 s) as the positive control, and
`red_ret_L1_stand` as the safety case that caught the fall.

**Read the seeded-arm numbers with care.** A later campaign
(`onward-bound-20260918-c`) moved a 12-case seeded arm by 7,100 ms between
two arms running the same code, so this record's 3,650 and 5,975 ms seeded
differences are withdrawn as evidence. The stand repetitions with
non-overlapping run sets, the death and the two stalls are what stand.

Arms: `.codex-research/reroute-campaign-20260918-a`,
`reroute-bits-20260918-a`, `reroute-stale-20260918-a`,
`reroute-prefer-20260918-a`, `onward-bound-20260918-{a,b}`; builds
`move-build-reroute-20260918-{b,c}`, `move-build-stale-20260918-d`,
`move-build-onward-20260918-e`. No sub-15-second stationary pickup exists in
any arm.

**Previously:** [landing probes](2026-09-17-human-landing-probes.md)
show six immediate proposals at a 749.8-units/sec human-derived root pass
path-free native physical/onward checks but fail selected-path admission.
The chosen run-up reduces launch speed to 486.1. A wider heading sweep did
not fix it. Both new cvars are default-off diagnostics; nothing is installed
or qualified as a faster bot. Next validate an alternative onward path from
the landing, carry it into execution, and compare the complete continuation.
Do not simply remove the path gate. See the record for exact fixtures,
builds, limits and verification, including zero compared parity boundaries.

**New evidence:** [three human demonstrations](2026-09-17-human-demonstrations.md)
complete in 12.875, 14.800 and 18.550 seconds. The two latter starts are close
to the benchmark stand. The fastest run demonstrates repeated immediate
jump continuations that retain and build momentum; retained stand bot runs
repeatedly lose it. The probes above now isolate one admission constraint;
full continuation ranking and cadence/resource equivalence remain open.
The respawn pad-cost investigation below remains valid but is not sufficient
to explain the stand deficit. Keep demonstration coordinates out of production
navigation. See the record for start differences and measurement limitations.

The owner has offered to demonstrate faster routes. Use
[HUMAN_ROUTES.md](../HUMAN_ROUTES.md) to capture authoritative human inputs and
motion, then compare route geometry, pit crossings, speed and landings with
the bot evidence. This is diagnostic evidence, not hand-authored production
navigation. The stationary flag-to-flag target remains unmet.
Recorder implementation and fresh verification are documented in the
[human recorder record](2026-09-17-human-route-recorder.md).

The current implementation fixes offered B changing A's walking horizon and
makes reference telemetry explicit about candidate role. Read
[the independent-reference record](2026-09-17-independent-reference.md)
for fresh results and the next decision. This supersedes the work order below.
The offer remains default off; no admission threshold was relaxed.

Do not treat local margin as a route-time guarantee or assume every
`no_onward` refusal is caused by a turn. Distance travelled and objective
progress are now recorded. Historical route extrema below are specific to
those campaigns, not global bests across all retained evidence.

Fresh results: 36/36 default floor and 12/12 seeded safety checks in each
arm, zero parity mismatches. Ownership/performance gates still fail. Eight
blue trials per arm show no offer benefit (median 20.525 s offer vs 19.800 s
control); leave the offer off. All counters and caveats are in the record.

Next concrete implementation: measure realizable pad entry/exit and route
costs. A diagnostic seed penalty on unvalidated pad proposals changes red
outbound respawn5 from median 18.075 s to 15.400 s over three runs per arm,
with native continuity and no deaths. The artificial cost is diagnostic
only: do not ship it or remove pads globally. Correct the model and test
both route alternatives, then rerun the affected gate. This is a respawn
result, not the owner's stationary flag-to-flag target, which remains unmet.

---

# Execute next: the deficit against greedy, then the onward test

Updated 2026-09-17 (second revision). This former work order is retained as
context; the opening 2026-09-19 review repair order now takes precedence.
Earlier requirements being retained here does not mean they were completed.

This is a historical work order, superseded by the independent-reference
correction above. Read these prior records for context:

1. [The rescue audit](2026-09-17-rescue-audit.md) - every retained
   B-only admission and censored pair, inspected.
2. [The arm-B reference record](2026-09-17-arm-b-reference.md) - the
   censoring resolved, the `no_onward` mislabel, the onward diagnostics, and
   the decided A/B comparison.
3. [The heading offer record](2026-09-17-heading-offer.md) - the
   successor change, its matched campaigns, and its failure against the gate.
4. [The correction record](2026-09-17-replay-correction.md) and
   [the original replay record](2026-09-17-replay.md) for background.

## What is settled, and must not be re-litigated

- **Replacing the aimed heading with the stored heading is refuted.** Over 138
  decided pairs on three routes: 28 both admitted, 7 A-only, 8 B-only.
- **Censoring is resolved.** Arm B has its own walk reference; censored pairs
  are zero on all three routes and `arm_isolation` is clean over 1,092 judged
  candidates. Do not re-derive B's reference from the control's horizon.
- **`no_second_landing` was a mislabel.** Every instance in the retained
  corpus was a one-hop candidate. The judge now reports `no_onward`.
- **The onward failure is not a brake and not a hold.** Measured
  `brake_frames=0`, `held_frames=0`, `end_stage=2`, about 56 units of the
  required 128 in the 48-frame window.
- **A route-level diagnostics-off/on identity check is not available** on this
  harness: the fixture start is an rcon round trip at wall-clock time, so two
  runs of one case do not share roots. Use the in-run isolation check,
  `StraightJudge`'s ordering and parity instead. Do not spend runs on it.
- `bot_moveHeadingOffer` is safe and faster than its own control, and still
  **fails the level-1 gate** at +1,240 ms per case against greedy. It stays
  default off.

## Current ordered work

1. **Resolve blue.** Repeat `blue_out_L1_stand` in both arms
   (`bot_moveHeadingOffer` 0 and 1) enough times to say whether the offer's
   600 ms median cost is real or route quantisation. Three repetitions cannot;
   the recorded level-1 spread reaches 2,050 ms. Report the observed spread,
   not a mean alone. If blue is genuinely worse, find which root loses and
   why before either defaulting or discarding the offer.
2. **Follow the deficit where it is.** The seeded arm's stand family is
   19,944 ms against greedy's 20,381; the deficit against greedy is in the
   respawn families (18,000 against 15,356 at respawn5, 19,019 against 17,506
   at respawn4). The stand cases are the owner's timing experiment, but the
   *gate* is failing on the respawn families, and nothing in this milestone
   examined them. Segment a respawn case with the analyzer before changing
   anything.
3. **Then the onward test, as its own experiment.** 121 `no_onward` refusals
   in one 12-case arm, with a measured mechanism: `onwardMax` is displacement
   from the landing mark; turning is one possible explanation for low
   displacement, not an established cause. New travel/progress diagnostics
   must separate useful turns from movement away from the objective. Any change here is a
   loosening of an admission rule: it needs its own control, must keep the
   falling-endpoint regression (`move-explore-20260916-bench-d`
   `blue_out_L1_respawn5`), and must not be adopted because it is faster.
4. **Re-measure large-blue search 19** with the onward fields. It is the one
   audited rescue still without a mechanism, and it is the case that first
   exposed the mislabel.
5. **Merge capture stays open and stays fourth.** Old seed files hold retained
   representatives, not entrants and losers; capture prospectively with
   immutable identities and full same-root context before any merge/no-merge
   comparison. Do not reconstruct an exit distribution from a final cache.

## Standing constraints

Do not widen `GRAPH_STATE_REACH`, `GRAPH_STATE_BAND` or `MOVE_STATE_TAKEOFF`,
weaken a safety check, spend Force in the level-1 arm, advance to level 2 or
held jumps, or install into GameData for this milestone. Use fresh unique
builds and retain all evidence under the existing archive policy. Keep
measured C/C++/header/CMake sources frozen while a campaign collects, comments
included. Keep diagnostic collection apart from final timing runs, and run
timing serially with no other native world competing.

Existing opt-in cvars, all default off: `bot_moveReplay` (bit 1 telemetry,
bit 2 the observe-only measured-policy arm), `bot_moveHeadingOffer`,
`bot_moveChainAdmit`, `bot_moveLandReplan`. None is promoted.

Keep three conclusions separate: **experiment completed**, **implementation
qualified**, and **sub-15-second target achieved**. As of this revision the
first holds for the replay investigation, the second does not hold for the
offer, and the third has not been achieved in any run: the best complete
stationary no-Force outbound pickups are 19,075 ms (red, offer arm) and
19,800 ms (blue, offer arm).

## Trusted baseline for the next agent

| Purpose | Path |
|---|---|
| Current build (offer, `launch_level`, onward diagnostics) | `.codex-research/move-build-offer-20260917T070254469Z` |
| Default 36-case floor on it | `.codex-research/move-offer-20260917T070254469Z-floor-control` |
| 12-case seeded arms | `.codex-research/move-offer-20260917T070254469Z-seeded-{control,offer}` |
| Stand repetitions | `.codex-research/move-offer-20260917T070254469Z-{control,offer}-r1..r3` |
| Named regressions | `.codex-research/move-offer-20260917T070254469Z-regressions-{control,offer}` |
| Analyzer v3 over the arm-on runs | `.codex-research/replay-v3-armb-20260917T070026774Z` |
| Analyzer v3 over the retained corpus | `.codex-research/replay-v3-20260917T065058266Z` |
| Retained greedy reference | `.codex-research/move-phase3-20260916-speed-f` |
| Large seed | `.codex-research/move-explore-20260916-b/seed.txt` |
| Small clean seed | `.codex-research/move-review-20260916-explore-a/seed.txt` |

A log from the offer build carries `launch_level` on every launch event, so
`tools/movement_replay.py` needs no `--launch-confirmation-delay-ms` for it.
Older logs still require the verified `25`.

---

# Superseded work order, 2026-09-17 (first revision)

Updated 2026-09-17. This was the active work order at that date; the original
ordered plan below is retained as context, not a claim that every requirement
was completed. Read [the correction record](2026-09-17-replay-correction.md)
before [the original replay record](2026-09-17-replay.md).

The replay investigation remains open. The existing default passes the
reported safety/parity campaigns but misses ownership/performance gates;
no stationary no-Force pickup below 15 seconds has been demonstrated.
The original analyzer-v1 flight/ground tables are superseded: a launch-stage
confirmation is one command frame later than the jump command. Use the
correction record's analyzer-v2 results, not the old 3,013/2,800 ms split.

## Corrected retained baseline

Analyzer-v2 output is retained at
`.codex-research/move-replay-correction-20260917T061951996008Z`.
Historical native launch confirmations require the explicit analyzer option
`--launch-confirmation-delay-ms 25`; do not apply it blindly to a future log
that already records the command boundary. No new native collection was
needed to correct these retained logs.

| Retained arm | elapsed | corrected flight | corrected ground | B-only | censored pairs |
|---|---:|---:|---:|---:|---:|
| large-seed red outbound | 19,150 ms | 16,225 ms | 2,925 ms | 0 | 1 |
| large-seed blue outbound | 19,925 ms | 16,525 ms | 3,400 ms | 2 | 2 |
| small-seed red outbound | 19,775 ms | 15,475 ms | 4,300 ms | 1 | 2 |

The large red gap's corrected arithmetic is +2,513 ms ground against 412 ms
modeled walk, plus +3,300 ms flight against 12,925 ms modeled flight. The
sum is 5,813 ms; it does not establish which intervention changes that gap.

Begin rescue inspection at these retained searches (not synthetic roots):

- Large blue: search 19, level 18950, edge 78278, one hop/run-up 24;
  A `no_second_landing`, B `admitted`.
- Large blue: search 23, level 20400, edge 93195, two hops/run-up 0;
  A `refused`, B `admitted`.
- Small red: search 19, level 20400, edge 3999, two hops/run-up 24;
  A `refused`, B `arrives`. `arrives` is forecast admission, not proof of a
  native pickup by B, which was observe-only.

## Current ordered work

1. **Audit all retained heading rescues before changing selection.** The
   large-seed red route has zero B-only admissions and one censored B result;
   large-seed blue has two B-only admissions and two censored B results;
   small-seed red has one B-only admission. Inspect each pair's root, edge
   snapshot, construction, rollout exit, refusal reason and next transition.
   The stored-heading hypothesis is open. A local rescue is not yet a faster
   native route. Do not omit either direction or the small-seed result.
2. **Resolve censored B candidates without changing A.** Freeze the control
   candidates, walk reference and selected result first. In a separate
   observe-only forecast, use the same original pre-command root and extend
   B's walk reference through its own completion. Preserve first-command,
   hold, safety and resource rules. Emit judged/censored/refused distinctly;
   never count unavailable reference evidence as a rejected policy. Confirm
   A's selected candidate and issued command are identical with diagnostics
   off/on, including roots where B needs a longer reference horizon.
3. **Test the smallest successor change supported by those pairs.** Offer
   a rescued stored-heading policy through the existing admission rules,
   retaining the control alternatives. Attribute actual commands and route
   segments to immutable policy versions. Compare matched complete native
   runs before promoting a change. Keep failed continuations and regressions.
4. **Capture merge inputs prospectively before testing merging.** Old seed
   files contain retained representatives, not discarded entrants/losers.
   Do not attempt to reconstruct their exit distribution from that seed.
   Add opt-in capture of every entrant, selected representative, loser and
   cost update, with immutable transition/capture IDs, source/seed/map hashes,
   exact entry/exit state, stored policy and full same-root forecast context.
   `bot_moveReplay` root text is a selected-field diagnostic, not a complete
   persisted player state. Autosave-off does not freeze in-memory records.
5. **Run a controlled merge/no-merge comparison on that one captured corpus.**
   Hold roots, templates, acceptance, search budget, ordering, resources and
   case/seed bindings fixed; disclose capacity/overflow differences. Compare
   predicted composition and native continuation, not just exit spread.
   Large spread motivates this experiment but does not prove merging caused
   the route error. State-key merging and changed entry states are separate
   alternatives to distinguish. Do not promote no-merge solely on correlation.

For each retry, update the correction record or a new dated record with exact
commands, immutable input/output bindings, both paired outcomes, censored
counts, actual ownership and pickup times, and the next decision. Preserve
native continuity between hops; synthetic root restoration is a separate
single-edge probe. Keep diagnostic collection apart from final timing runs.
Use repeated matched trials and report observed spread; there is no universal
2-second detectability threshold and repetition spread alone is not causation.

Existing controls: `bot_moveReplay` bit 1 telemetry / bit 2 observe-only B,
`bot_moveChainAdmit` and `bot_moveLandReplan` default off. The two treatment
experiments did not establish a default improvement. Do not widen
`GRAPH_STATE_REACH`, `GRAPH_STATE_BAND` or `MOVE_STATE_TAKEOFF`, weaken safety,
advance to level 2/held jumps, or install into GameData for this milestone.
Use fresh unique builds and retain all evidence under the existing archive
policy. Source freezes and native process limits below still apply.

## Original work order, 2026-09-16 (historical scope and procedure)

Prepared 2026-09-16. This was the active work order for the previous agent.
It supersedes the next-action paragraphs in older movement evidence, not
the safety/physics contracts in this folder. Read this file, the current
working diff, and [the review](2026-09-16-search-review.md) first.
Do not restart completed experiments just to recover context.

## 1. Objective and scope

The owner confirms a **stationary home-stand start to enemy flag pickup in
under 15 seconds**, with ordinary tap jumps, no active Force Speed and no
held Force jumps. Match those conditions. Red and blue outbound stand
cases are the primary performance cases; a return/capture case is not the
same timing experiment. Regenerated Force is allowed by native lifecycle;
spending it is not allowed in the level-1 arm.

The next task is to explain and reduce the gap between the planner's
roughly 13-second estimates and the bot's roughly 19-second execution.
First compare replay of the **stored control heading** with today's
**heading aimed at the stored landing**, from identical actual states.
Measure where planned chains stop composing and where time is lost, then
implement the smallest evidence-supported correction and qualify it.
Do this before increasing exploration volume or moving to level 2.

Do not conclude that pits must be avoided, or that the search is complete.
Safe pit crossings are allowed when the native rollout proves the entire
continuation. Do not remove a safety check merely to obtain a faster time.

This task authorizes local implementation, analysis, builds and isolated
localhost experiments. It does not authorize installing into GameData,
connecting to a real server, permanent deletion, or map-specific movement
coordinates. Keep native lifecycle and resources intact outside declared
fixture interventions. No Force/physics changes, held-jump work, second-map
qualification, or retirement of the old movement stack in this milestone.

## 2. Starting state: preserve this working tree

Workspace: the repository root, PowerShell. At preparation time HEAD is
`1c8867e6` (`Movement phase 3: the state-space planner at level 1`).
**The current implementation is ahead of HEAD in uncommitted files.**
Never reset, checkout, clean, or rebuild the old HEAD as the new baseline.

Already modified: `bot2_move.c`, `bot2_forecast.c`, `bot2_graph.c/.h`,
`movement_bench.py`, `movement_campaign_compare.py`,
`test_movement_bench.py`, and movement docs. `tools/movement_explore.py`
and `docs/movement/records/` are untracked but required. Other untracked
files and `.codex-research`, `.codex-archive`, `.claude` are not cleanup
targets. Inspect status/diffs again because another task may have advanced.

Already fixed and tested; preserve these fixes:

- Exploration is armed at `bot_moveExplore=-1` (idle), the fixture is
  acknowledged, then the runner enables `bot_moveExplore=1`. Movement reset
  clears an active exploration queue. Synthetic exploration restores view
  angles as well as player state.
- Candidate comparisons use walking at the candidate's own completion
  frame. Changing the preferred candidate copies its matching walk metrics.
- Graph loading supports its 131,072-edge capacity and terminates each
  input line before scanning. The old 16 MiB limit and long suffix scans
  are gone. An isolated >16 MiB capacity round trip passes.
- **All** strafe edges check entry speed and heading, not only landing taps.
  Flight-only edge cost cannot silently include free acceleration/run-up.
- Gap and replayed-hop candidates use `HopHoldCommand` to observe 48
  post-landing frames and reject horizon exhaustion. A 128-unit onward
  mark while falling is not sufficient admission.
- Failed/incomplete times are excluded from completion-time comparisons;
  case tables label failures rather than showing time-to-death as speed.

Read [the verification procedure](../../archive/strafe-movement-2026-09-14/STRAFE_MOVEMENT_VERIFICATION.md).
Use fresh unique build/output directories. Never delete old evidence.
Keep measured C/C++/headers/CMake files frozen during builds and collection,
including comments. Documentation/analysis can proceed while a frozen run
is collecting. Follow AGENTS.md for archive-only cleanup.

## 3. Trusted baseline and retained evidence

Paths in this table are relative to the workspace. Each campaign's
`result.json` links its source/binary manifest and records case/settings/
seed bindings. Do not merge evidence across builds without naming them.

| Purpose | Path | Established result |
|---|---|---|
| Frozen corrected build | `.codex-research/move-build-review-20260916-a` | Dedicated engine + game DLL; no installed-game changes |
| Unseeded floor | `.codex-research/move-review-20260916-floor-a` | 36/36, zero mismatches, only `move` ownership |
| Large-seed level 1 | `.codex-research/move-review-20260916-seeded-b` | 12/12 survive, zero mismatches; two cases include legacy; mean 2.085 s slower than retained greedy arm |
| Large diagnostic seed | `.codex-research/move-explore-20260916-b/seed.txt` | 94,711 edges; inherited exploration began before its fixture, so not a clean preparation baseline |
| Correctly bound exploration smoke | `.codex-research/move-review-20260916-explore-a` | Red outbound, radius 32, one round, 549 cells, 5,426 total edges, no overflow; abstract cost 15.006 s |
| Small clean seed | `.codex-research/move-review-20260916-explore-a/seed.txt` | Use for the first controlled replay experiment |
| Small-seed native replay | `.codex-research/move-review-20260916-explore-replay-a` | Red outbound 19.775 s, only `move`, zero mismatches |
| Offline A*/Dijkstra comparison | `.codex-research/move-review-20260916-search-compare-a` | `results.json`, `manifest.json`, and both paths retained |
| Parser/admission regression | `.codex-research/graph-fixes-20260916-unit/README.md` | Exact extracted functions, bounded lines, capacity and entry checks |
| Retained greedy reference | `.codex-research/move-phase3-20260916-speed-f` | Comparison reference; do not rebuild/recollect its old sources |

Large-seed stationary outbound native times: **red 19.150 s, blue 19.925 s**.
Unseeded: **red 21.450 s, blue 21.275 s**. Both large-seed outbound cases
fail the under-15-second target despite passing survival/resource checks.

Offline stationary graph queries on the large seed:

| Direction | Existing A* | Same state search with heuristic zero |
|---|---:|---:|
| Red to blue | 13.145 s, 228 expanded | 12.790 s, 22,636 expanded |
| Blue to red | 13.001 s, 168 expanded | 12.745 s, 22,263 expanded |

Neither overflowed. The zero-heuristic searches cost 115--118 ms versus
1--2 ms in the isolated harness. This is **not** native execution, and
Dijkstra still uses the lossy state representation. Do not put a 118 ms
search on the live landing frame or claim a sub-13-second bot run.

Named failure evidence to keep:

- `.codex-research/move-explore-20260916-bench-d/blue_out_L1_respawn5/server.log`:
  gap plan 4 lands at level 14000, z1368.1; completes at 14450 already at
  z1287.8; then falls and dies. The former short horizon admitted the fall.
- Corrected large-seed `red_ret_L1_stand` and `red_ret_L1_respawn4` include
  legacy ownership. They survive but do not pass the ownership gate.
- Review `-regression-a` and `-seeded-a` accidentally omitted `--edges`.
  The latter is 9/12 with three deaths. Retain and label these as
  off-configuration runs, not matched qualification or improvements.

## 4. Code map and distinctions that matter

Search these function names; line numbers may have moved.

| File | Functions / purpose |
|---|---|
| `codemp/game/bot2_move.c` | `Replan`, `LiveState`, `PlannedStrafe`: choose and approach the graph path |
| Same | `StraightSearch`: constructs planned candidates using `AimAt(... e->landing ...)`; this is the first experiment site |
| Same | `RunCandidate`, `StraightJudge`, `StraightWalk`, `SpikeMeasure`: native forecast and admission; parity frame storage is reused on every rollout |
| Same | `StraightCommit`, `Reissue`, `MoveStep`, `HopStep`: committed/live commands; preserve the same command source as the forecast |
| Same | `LearnStrafe`, `ExploreLearn`, `PlanCompleted`: record edges, merge them, and update measured edge costs |
| Same | `ExploreState`, `ExploreBegin`, `ExploreReset`: declared synthetic preparation roots and restoration |
| `codemp/game/bot2_graph.c/.h` | `Graph_AddStrafeEdge`, `Graph_StrafeAdmits`, `StateKey`, `StateWalk`, `StateLink`, `StateStrafeEdges`, `Graph_PlanState`, `Heuristic` |
| `codemp/game/bot2_forecast.c/.h` | Forecast request/guard, full root and policy continuation, exclusions, result/completion distinction |
| `tools/movement_bench*.py` | Fixture runner, verdicts, telemetry parsing |
| `tools/movement_explore.py` | Preparation runner, not a fastest-route certificate |

`graphEdge_t.dir` is the stored hop policy heading. `entryDir` and
`exitDir` are velocity headings. They are different quantities.
`takeoff` is a measured launch position, not the beginning of a run-up.
`costMs` on learned strafe edges is measured **launch-to-landing** time.
Approach, acceleration, braking, planning waits and pickup approach must
not disappear from the end-to-end accounting.

`PlanCompleted` updates edge costs from `plan.measured[]`, which was
produced by the committed **forecast**. This is not an independent native
flight measurement. Collect actual launch/landing events separately.

The graph record is **not a full reproducible player state**. It omits
view yaw/latches, detailed Force timers and other native fields. An
artificial root reconstructed from only position/speed is a synthetic
probe, not exact replay. The path currently exposes per-cell expected
speeds, not the entire state-search entry history.

`RunCandidate(... fresh=1 ...)` generates this boundary's first command;
`Reissue` makes the live commit use it. Do not compare a fresh candidate
against a replay that already consumed a walk frame. Every hypothetical
arm must start from the same pre-command player/executor state and time.

## 5. Ordered execution plan

### Step A — instrument without changing selection

1. Confirm the checkout and read the current diff. Reuse retained evidence
   for baseline facts; do not recollect it before every edit.
2. Add opt-in diagnostic telemetry at path creation, candidate creation/
   result, commit, launch, landing, replan and objective pickup. Default
   behavior must remain unchanged. Name any new flags in the experiment
   record. This was the original instrumentation request; `bot_moveReplay`
   now exists, but complete reproducible root capture still needs verification.
3. Record a path generation ID and snapshot its ordered transitions and
   expected entry/exit states/costs. Include source/seed hashes in the run
   manifest. Edge index alone is not identity: learning/merging/cost updates
   can change a record. Snapshot the complete relevant edge fields when
   first considered and when committed; relate versions explicitly.
   Disabling graph autosave freezes disk writes only. Use an immutable
   experiment snapshot or log every in-memory merge/cost mutation and
   invalidate comparisons whose assumed edge records changed.
4. Capture full in-memory live player state, executor-before state, policy,
   root command, timing and context at the candidate boundary. Use the
   existing forecast guard; do not mutate live physics to construct an arm.
   Copy each result/measurement before running the next candidate because
   the shared parity frame buffer will be overwritten.
   Include vertical velocity, grounded entity, jump latch, command time,
   view/controller yaw, Force timers, animation state, Pmove settings,
   executor, plan, path and permit. Record actual issued commands and
   second-hop boundaries; edge IDs alone do not establish policy replay.
5. First collect red outbound with the **small clean seed**, then blue
   outbound with the large seed to locate the first divergent transition.
   Separate pre-fixture, actual route, and post-pickup events. The runner
   can keep logging briefly after pickup; do not charge the return trip to
   the outbound route. Existing play CSV is 4 Hz: use native event/frame
   times for segment accounting, not interpolation of sparse rows.

Required per-transition output (CSV/JSON plus a readable summary):

```text
case, build_hash, seed_hash, path_generation, segment, edge_snapshot_id,
policy_variant, root_time, root_position, root_velocity, root_view_yaw,
expected_entry_position/speed/heading/tap, actual_entry_error,
stored_control_heading, aimed_control_heading, runup,
predicted_launch/landing_time, actual_launch/landing_time,
predicted_exit_position/velocity, actual_exit_position/velocity,
graph_flight_cost_ms, approach_ms, flight_ms, interhop_ground_ms,
brake_or_wait_ms, remaining_pickup_ms,
forecast_status, unsupported_bits, admission_reason,
committed, replaced_by_candidate, replan_reason, owner, parity_result
```

Use explicit null/not-observed values. Never invent an actual landing for
a refused or replaced plan. Durations need mutually exclusive boundaries;
their sum must reconcile to fixture-to-pickup elapsed time, with any
unclassified interval shown separately. Distinguish whole-path estimate,
one-hop flight cost, validation horizon, and actual route elapsed time.
Report search CPU time separately from engine elapsed time. Stop calling
a segment exact policy replay when a different policy takes over, and
record that boundary rather than attributing its outcome to the old edge.

### Step B — paired policy replay from the same root

Run two hypothetical candidate families inside the same native guard:

- **A, control:** current `AimAt(actual root, stored landing)` construction.
- **B, measured policy:** use the stored `e->dir` for hop 0 and `e2->dir`
  for hop 1, with the same hop counts and 0/24 run-up alternatives as A.

Only heading construction differs. Keep the same root, first-frame
semantics, inputs, spend policy, time step, walk reference, admission,
post-landing hold and search ordering for both arms. Initially B is
observe-only; it must not change which candidate is committed. Label the
variant in telemetry. If implementing extra candidates, size buffers for
the new count; do not overrun `MOVE_STATE_CANDIDATES` or result arrays.

For exact-entry composition evidence, newly capture complete roots during
preparation or native execution; do not claim the old graph file contains
them. If persisted, serialize/version named fields and bind the capture to
the engine/source build, physics, map and timing. Do not persist pointers
or replay raw struct bytes across builds. Keep captures under unique local
research directories and reusable capture/replay code under `tools/` or
Bot2-owned source.

At a chain's next hop use the **actual preceding native terminal state**,
not the edge's ideal recorded exit. A teleport, speed reset, yaw reset,
Force refill or fixed prerecorded coordinate between hops invalidates an
end-to-end speed result. A declared synthetic root can test one edge only.

### Step C — classify the first failure before changing the algorithm

For each primary route, identify the earliest planned transition that
is not executed as priced, and assign an evidenced reason:

| Observation | Next action |
|---|---|
| A fails, B succeeds from identical root | Try the stored-control candidate as a normal validated proposal; retain A if displaced roots need it; measure route effect |
| Both fail; live root is outside entry requirements | Find the preceding state/cost error; do not widen the envelope to conceal it |
| B succeeds only after restoring ideal speed/position | Demonstrates non-composition; fix transition modeling/replay, not native player state |
| Both pass, but greedy candidate replaces the planned one | Attribute choice and its subsequent state; test a controlled selection change separately from heading construction |
| Local hop is fine; approach/braking consumes the missing time | Charge/model that transition or improve approach policy, then revalidate; flight time alone is insufficient |
| Forecast refuses at trigger/item | Preserve unsupported status and distinguish unknown from unsafe; do not globally turn exclusions off as a speed fix |
| Required safe next jump is rejected by walking-only continuation | Construct and validate the actual next-jump continuation; keep the falling-endpoint regression test |
| Edge was merged into an incompatible state | Preserve distinct entry/exit transitions or establish a valid dominance rule; a cheaper flight is not automatically the better route |

If no B candidate helps, report that negative result and follow the first
measured state/cost mismatch. Do not keep tuning heading offsets blindly.
Promote each new actual failure into a portable fixture or reproducible
case/seed binding before writing a special recovery rule.

### Step D — implement and qualify the demonstrated correction

Make one causal change at a time; controls and treatment must differ only
in that change. Keep one executor and shared live/forecast command law.
Native-validate every committed sequence from its real state. Test the
primary route first, then the two named legacy cases and former fatal
blue outbound respawn-5 case. Run the full suites once the targeted change
passes. Fix regressions rather than deleting them or weakening the gate.

If policy replay/state costs do not explain the gap, proceed to distinct
state transitions, a valid online heuristic, and wider corner/pit proposals
in that order. Existing Dijkstra evidence is already collected: use it as
an offline diagnostic, not a synchronous production replacement. Preserve
the best **fully executed** route and report exploration budget/coverage;
abstract low costs and exhausted cell queues do not establish convergence.

## 6. Commands that work now

Run from the workspace root. Any additional diagnostic flags must be implemented and
documented before adding `--set` arguments for them. These commands use
existing flags only. `--edges` and `--parity 256` are **mandatory** here;
both default off in the benchmark runner. Timing runs are serial, with no
other native worlds/builds competing. Outcome-only work may use at most
four distinct localhost ports/homepaths under AGENTS.md.

```powershell
Set-Location -LiteralPath '<repo root>'
$ErrorActionPreference = 'Stop'
$env:PYTHONDONTWRITEBYTECODE = '1'
git status --short
git diff --stat
netsh int ipv4 show dynamicport udp

# Define once for this iteration. Every new build/arm needs a fresh output.
$replayTag = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$replayBuild = '.codex-research/move-build-replay-' + $replayTag
$replayRoot = '.codex-research/move-replay-' + $replayTag
$replaySmallSeed = '.codex-research/move-review-20260916-explore-a/seed.txt'
$replayLargeSeed = '.codex-research/move-explore-20260916-b/seed.txt'

# After source edits, freeze measured sources until collection completes.
python tools/strafe_fixture_build.py --out $replayBuild
if ($LASTEXITCODE -ne 0) { throw 'Build failed; retain its output and fix before collecting.' }

# First focused native run. --momentum-trace is diagnostic overhead;
# controls must match it, and final timings should be collected without it.
python tools/movement_bench.py --build $replayBuild --out ($replayRoot + '-red-probe') --level 1 --filter '^red_out_L1_stand$' --graph $replaySmallSeed --edges --parity 256 --momentum-trace --parallel 1 --port 29900
if ($LASTEXITCODE -ne 0) { throw 'Probe failed; inspect retained result before expanding.' }

# Both primary timing cases with the same seed and settings.
python tools/movement_bench.py --build $replayBuild --out ($replayRoot + '-stands') --level 1 --filter '^(red|blue)_out_L1_stand$' --graph $replayLargeSeed --edges --parity 256 --parallel 1 --port 29904

# Targeted safety / ownership cases, after the change passes its probe.
python tools/movement_bench.py --build $replayBuild --out ($replayRoot + '-regressions') --level 1 --filter '^(blue_out_L1_respawn5|red_ret_L1_stand|red_ret_L1_respawn4)$' --graph $replayLargeSeed --edges --parity 256 --parallel 1 --port 29908

# Final qualification: do not run these repeatedly without a new reason.
python tools/movement_bench.py --build $replayBuild --out ($replayRoot + '-seeded-L1') --level 1 --graph $replayLargeSeed --edges --parity 256 --parallel 1 --port 29912
python tools/movement_bench.py --build $replayBuild --out ($replayRoot + '-floor') --edges --parity 256 --parallel 1 --port 29928

python tools/movement_campaign_compare.py ($replayRoot + '-seeded-L1') --against review=.codex-research/move-review-20260916-seeded-b --against greedy=.codex-research/move-phase3-20260916-speed-f --no-baseline --cases
```

The runner returns nonzero for a red campaign, but a green exit code alone
does **not** enforce no legacy owner or the under-15-second target. Inspect
every `result.json`, event/owner audit and actual pickup time. Check each
command's exit status before choosing the next action; the command list is
a workflow reference, not permission to ignore failures and continue.
Confirm ports are free/outside the machine's dynamic range. Do not kill
unrelated processes to free one. If build/source hashes no longer match,
build anew; never bypass `stage_build` verification.

For fresh small preparation after changing a policy (not needed merely to
read old evidence), run this serially in a separate slot:

```powershell
python tools/movement_explore.py --build $replayBuild --out ($replayRoot + '-explore-smoke') --level 1 --filter '^red_out_L1_stand$' --rounds 1 --radius 32 --batch 8 --parity 256 --port 29972 --parallel 1 --timeout 240
```

Verify fixture -> enable -> exploration-begin order in its server log,
completion/no overflow, and cache loading in a native replay. This small
budget is a workflow check, not a speed-search stopping criterion.

For Python tests, run affected tests. Some existing tests use
`TemporaryDirectory`, whose default cleanup permanently deletes files.
The following wrapper retains this affected suite's temporary artifacts:

```powershell
@'
import contextlib
import datetime
import pathlib
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
root = pathlib.Path('.codex-research') / ('replay-tests-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
root.mkdir(parents=True, exist_ok=False)

@contextlib.contextmanager
def retained_directory(*args, **kwargs):
    path = tempfile.mkdtemp(prefix='case-', dir=str(root.resolve()))
    yield path

tempfile.TemporaryDirectory = retained_directory
sys.path.insert(0, str(pathlib.Path('tools').resolve()))
suite = unittest.defaultTestLoader.discover('tools', pattern='test_movement_bench.py')
result = unittest.TextTestRunner(verbosity=2).run(suite)
print('Retained test artifacts:', root.resolve())
sys.exit(0 if result.wasSuccessful() else 1)
'@ | python -
if ($LASTEXITCODE -ne 0) { throw 'Affected Python tests failed; retain artifacts.' }
```

Inspect any additional suite for explicit cleanup before running it; this
wrapper only handles these tests' context-manager use. C changes require
the fresh native checks above; documentation changes require
link/consistency checks only.

## 7. Required deliverables and completion criteria

Before calling this replay milestone complete, provide:

1. A runnable opt-in paired replay/capture mechanism and versioned analyzer,
   with default gameplay behavior preserved until a measured fix is chosen.
2. Both primary cases' segment reports. Reconcile actual route elapsed
   time, identify the first failed/mispriced transition, and show A/B
   outcomes from identical roots. Include negative results and unknowns.
3. A causal implementation fix if the experiment supports one, its named
   regression and matched controls. Do not stop at another general essay
   when the measured failure has a concrete local correction.
4. Final 36-case unseeded floor and 12-case seeded level-1 results: no
   deaths, forbidden spend or parity mismatches; separately assess legacy
   ownership and speed. The seeded arm must meet the existing no-legacy
   and no-slower-than-greedy gate before being declared qualified.
5. Best executed red/blue stationary no-Force pickup times. Claim the
   speed target only for complete native runs **strictly below 15,000 ms**,
   with matched repetition evidence. If timing is near the boundary, add a
   native pickup timestamp rather than relying on 4 Hz sampling. Do not
   substitute graph costs, time to death, warm-start speed, or averages
   that hide a failed case. A best run is not proof of an optimum.
6. A dated evidence record with build/source/binary/seed/settings/case
   bindings, analysis commands, time-cost distribution and all failures;
   update PLAN, EVIDENCE and this handoff's next action.

Keep three conclusions separate: **replay experiment completed**,
**implementation qualified**, and **sub-15-second target achieved**.
If the investigation finishes but the target/gate does not, state that
plainly, record the precise remaining failure and next executable step,
and do not advance to level 2. Missing input should only stop work when it
actually blocks the experiment; the user's start/timing/Force conditions
are already confirmed and should not be asked again.
