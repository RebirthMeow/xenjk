# Bot2 movement rewrite: postmortem and redesign proposal (2026-11-25)

A fresh read of the rewrite's own records, written for the owner. Nothing was built or run for it. Sections 1-5
report what the records say; sections 6-7 are proposals. Where a number is mine and not a record's, it says so.

## 1. Summary

The rewrite replaced seven competing controllers with one movement owner, and that part worked: it plays 14 maps
without a navmesh, runs a balanced 4v4 on two maps, and beat the classic Bot2 26.8 captures to 0.

It did not meet its own speed goal. The stationary no-Force stand run never reached about 15 s by independent
search; it got there only by replaying a prepared decision library on one map. From 2026-10-04 on, nearly all effort
went into fitting live search inside a 25 ms single-thread server frame. That produced a stack of deferral, pool and
cache settings, each trading some behaviour for frame time, and a run of outcome comparisons that came back
"undecided".

The main cause is structural. Expensive search runs live, inside the game frame, on the game's own global physics
state. Everything else (the settings, the noise, the integration bugs) follows from that. The new local-play target
removes the constraint that shaped it, and adds a stricter one: the game thread is now also the player's render frame.

## 2. Timeline

| Date | Event |
| --- | --- |
| 2026-04-02 to 04-27 | Classic Bot2: navmesh (Recast/Detour), Pmove jump prediction, macro AI, phantom Pmove, wallrun tools. |
| up to 2026-09-13 | The strafe runner stack (commits in these paths from 09-07; its start was not traced): seven controllers, hand-recorded routes for one map. Frozen 09-14 ([archive](../archive/strafe-movement-2026-09-14/README.md)). |
| 2026-09-14 | Rewrite starts ([README](README.md)): one physics, one owner, graph from BSP traces. |
| 09-15 to 09-24 | Phases 0-3: harness, graph, state planner, level-1 and level-2 gates on Kejim. |
| 09-19 | Owner sets the target: independent movement first, about 15 s on the stand run ([refined method](records/2026-09-19-refined-method.md)). |
| 09-22 | Decision explorer and tape library: 150-175 worlds a start class to find one best run ([record](records/2026-09-22-four-class-library.md)). |
| 09-26 | Strafe skill lowered to 90% so the bot reads as human; level-1 stands slow by 2.3-3.9 s ([record](records/2026-09-26-strafe-skill.md)). |
| 09-27 | 14 maps, no navmesh, 828/828 trips on the eight new maps; prepared seeds per map. |
| 10-02 to 10-03 | Milestone A passed; milestone B 4v4 runs on Kejim and Dantooine. |
| 10-04 to 10-09 | Frame cost becomes the open item. Deferrals, pools, route tables, guidance. Legacy owners and classic Bot2 removed (10-07, 10-08). |
| 10-13 to 10-30 | Merged to master; renderer work takes over. Library re-explored 10-14; guidance default 10-30. |
| 11-09 to 11-10 | Full review (70 findings, no P0/P1), two fix batches. Stopped by the owner 11-10. |

About 340 commits touch the movement code and docs between 09-14 and 11-10; 291 carry an Opus 5.5 co-author line.
Bot2 is about 23,200 lines of C; `bot2_move.c` alone is 8,410.

## 3. What it achieved

| Result | Number | Source |
| --- | --- | --- |
| Map coverage without a navmesh | 14 maps; 828/828 trips, 0 deaths on the eight new ones | [eight maps](records/2026-09-27-eight-maps.md) |
| Against classic Bot2, 4v4 Kejim | 26.8 captures a world against 0.0, 16 of 16 worlds | [vs legacy](records/2026-10-03-vs-legacy.md) |
| Milestone A (lone runner, defended flag) | captures 4.31 to 6.44 a world with protect | [MILESTONE_A](MILESTONE_A.md) |
| Milestone B (4v4) | about 8.8 (Kejim) and 9.3 (Dantooine) captures a world, sides balanced | [HANDOFF](HANDOFF.md) |
| Level-1 stand with the re-explored library | 13,375-14,450 ms at four start classes | [overnight items](records/2026-10-14-overnight-items-6-7.md) |
| Level-2 stand | 8,200-11,000 ms against a 13,000 gate | [level-2 speed](records/2026-10-09-level2-speed-stale-library.md) |
| Frame cost, 4v4, two pinned cores | Kejim p99 18-20 ms; Dantooine 28.5-29.5 ms (target 25) | [timed pairs](records/2026-11-10-timed-pairs.md) |
| Exact replay of seeded worlds | same build, same seed: identical game log | [PLAN](PLAN.md), `tools/laps_identity.py` |

The method was also honest. Failed arms are kept, wrong claims are withdrawn in writing, and the handoff lists what
was built and left off.

## 4. What failed and why

### Symptoms

- **The 15 s independent stand run was never met.** The search baseline was a 21,150 ms median over 60 retained
  floors (best single run 19,150). The gate was set at 16,500 and met "only just" (16,212.5) on 2026-10-05. The
  13-14 s times come from the library, on Kejim only.
- **Frame cost was never closed.** Dantooine 4v4 stayed at 28.5-29.5 ms p99. Kejim's worst frames stayed above
  100 ms through every change.
- **Settings multiplied.** About 300 distinct `bot_*` names appear in the Bot2 sources (my token count, not
  audited). The review found 94 defender settings read by string at 133 sites, none registered.
- **Comparisons did not decide.** Of the 2026-11-09/10 outcome arms in the handoff, almost every captures result is
  "undecided". Several became defaults anyway on the owner's word.
- **Integration bugs between the parts.** An edge no flight was tried on was charged to the shared graph (BM-3,
  about two thirds of all 30 s edge time-outs). The arrival safety check did not simulate what a rerouted arrival
  flies (BX-1). A deferred search returned after the bot had walked off the lip, which then killed the stand's only
  strafe edge and cost the level-2 run 3.6 s.

### Root causes

**1. Search lives inside the game frame, on global state.** The forecast runs the real Pmove, which uses process
globals, writes the global `level.time` and the live entity's animation fields, and calls an engine trace that is
not thread-safe ([frame cost record](records/2026-10-06-frame-cost.md), reentrancy inventory). So search could only
run on the main thread, and the only way to cut a frame was to do the work later or not at all. Each deferral then
changed when a decision landed relative to the bot's motion, and each such shift was a behaviour change needing its
own outcome campaign. The threading spike was costed at "several sessions" with at most 1.4 times gain at two
cores, so it was never started. That estimate was right for the old target and is the wrong frame for the new one.

**2. The plan and the execution disagree, and local search fills the gap.** The graph planner prices routes on
approximate state. Live landings arrive slower than planned (378-420 u/s against 394-532), the next edge goes
"stale", and the bot walks ([where the stand time goes](records/2026-09-18-where-the-stand-time-goes.md): 107 of
227 landings broke the chain; at that time no search ran in flight). The planner's cost was later shown to predict
almost none of the hop flow ([hop room](records/2026-10-30-hop-room-offline.md)). 74% of candidate rollout work was
in searches that committed nothing. The speed gap is a chaining problem; adding candidates or library edges did not
close it, and "committing learned edges is worse than waiting" ([LESSONS](LESSONS.md)).

**3. The prepared library is a replay, not knowledge.** The decision explorer finds one good run per start class by
running whole game worlds at game speed (150-175 worlds a class; a chain of eight cells took about an hour on one
map). It is bound to the build's defaults: the walk deferral made the skill-90 library "buy almost nothing" until
it was re-explored. So every behaviour change quietly invalidates the bake, and the bake is long because it is done
through the live game.

**4. The outcome metric cannot see the effects being tested.** Team captures a ten-minute world has a paired
standard deviation of about 1.5 (my figure, from the records' ±0.22 at 48 pairs and ±0.17 at 80). Most changes move
captures by 0.1-0.3. Detecting 0.3 with ordinary power needs about 200 paired worlds; arms ran 16-48. Worlds also
diverge chaotically seconds after the first changed decision, so a pair attributes the split, not the outcome.
Wall-clock timing was worse: one world's tail swung 2.4 times between two identical runs.

**5. Moving targets.** Strafe skill 90 added 2.3-3.9 s to the level-1 stand a week after the 15 s goal was set.
Competitive Force defaults, review fixes and new defaults each changed the control, so controls had to be rebuilt
and earlier arms stopped being comparable.

**6. Method cost.** The rule "nothing is guarded in advance; every rule earns its place on a named case" was meant
to stop the old stack's exception pile. Applied to performance, it produced a new pile: each frame-cost idea became
a setting with a default, a record and an arm. Verification became the bulk of the work, which the owner flagged on
2026-09-25.

## 5. What is worth keeping

- **Physics facts** ([PHYSICS](PHYSICS.md), [LESSONS](LESSONS.md) "Engine facts"): constants, jump acceptance by
  `PMF_JUMP_HELD`, the sealed fall at about 1,100 u, flips on side input, Force regeneration rules.
- **The control kernel** (`bot2_movecontrol.c`, 187 lines) with the strafe-skill lever, and the corner compiler.
  Search and execution issuing commands from one function is the property that kept rollouts honest.
- **Graph discovery from collision** (flood fill, stacked floors, pads, lifts, drops) and the prepared-seed idea
  (drop candidates that failed and never succeeded).
- **Fixtures and harnesses:** `tools/fixtures/movement/<map>/cases.json` for 14 maps, `movement_bench.py`, the
  parity harness, seeded laps worlds (`defense_laps.py`, `laps_compare.py`, `laps_identity.py`), the work meter
  (`laps_work.py`), the hop-flow scoring.
- **Human reference data:** the 14.8 s owner run, the air-strafe efficiency study, the position pilot.
- **The team layer:** runner Force policy, camper, base and chase defenders. These sit on top of movement and are
  mostly independent of how routes are found.
- **Measurement lessons:** telemetry inside a search changes the bot; parallel batches are not independent; judge
  cost by work counts; a hash match across builds must be checked, never assumed.

The live straight search, the deferral and pool machinery, the avoid-list charging, the reroute searches and the
tape library are the parts I would not carry forward.

## 6. Redesign proposal for local play

### What the new target changes

On a local game the server frame runs inside the client's frame. A 25 ms bot frame was acceptable at 40 Hz on a
dedicated server; on a client at 120 fps it is three dropped frames. So the game-thread budget gets much tighter,
while total compute gets much larger. The old design is the wrong shape for both: all its work is on the one
thread that now matters most.

### Core idea

**Bake the map's movement once, offline, as flown trajectories. At runtime the game thread only follows and
checks; all search runs on worker threads against a private copy of the world.**

### Architecture

1. **A re-entrant movement kernel.** A bot-owned build of the player move code whose state is a context struct
   (or thread-local), plus a bot-owned copy of the static collision world. No engine calls, no `level` writes.
   Dynamic things (movers, other players) enter as a small snapshot. The existing parity harness is the acceptance
   test: the kernel must match native Pmove with zero mismatches on recorded rollouts.
2. **An offline baker** using that kernel, as a standalone tool with no game process. It explores from real
   successor states and stores *chains* (takeoff state, command policy, measured landing state, onward options),
   so plan and execution use the same numbers. It runs on every core at CPU speed, not game speed. Output is one
   file per map and physics binding, shippable with the map.
3. **Runtime follower** on the game thread: look up the baked option for the current state, issue the kernel's
   command, compare the live state with the baked one each frame. No rollouts on this thread.
4. **Async planner** on workers: when the follower's state leaves the baked envelope (a knock, a body in the way,
   a new objective), a worker searches from the predicted landing state while the bot is still in flight or
   walking. Flight is about 80% of travel time, so there is time.
5. **Fixed-latency commits.** A worker's answer is applied at a set game time after the request, not when it
   happens to finish; if it is late, the follower takes its safe fallback (walk the cell route). That keeps seeded
   worlds replayable, which the 2026-10-06 note rightly feared an AI thread would end.

### Budgets and threading I would assume

| Item | Assumption | To confirm by measurement |
| --- | --- | --- |
| Game-thread cost, all bots | median under 0.3 ms, worst frame under 1 ms | client frame-time histogram, bots off against 7 bots, in a heavy scene |
| Workers | 2 threads, below-normal priority, kept off the cores the render thread and its workers use | 1% low frame time with workers busy against idle; try 1, 2, 4 |
| Replan latency | answer applied 100 ms after the request | share of requests that miss; falls after a missed answer |
| Memory | about 20 MB a worker (the survey's per-thread figure) plus the collision copy and baked file | measured size per map; Night City-class maps may need a streamed or regional copy |
| Bake | minutes per map on the dev machine, run offline or at idle, cached by map checksum | rollouts a second per core from the kernel spike; edges needed per map from today's seeds |

The renderer already runs its own workers (mesh, pipeline), so core contention is real and has to be measured with
the game rendering, never dedicated-only.

### Offline against runtime

| Offline (bake) | Runtime, workers | Runtime, game thread |
| --- | --- | --- |
| Floor graph, pads, lifts, drops | Replan after knocks and objective changes | Follow, command, envelope check |
| Strafe chains with real successor states, per speed and Force class | Routes around bodies | Team and Force decisions |
| Goal tables for flags and bases | Validation against movers | Fallback walk |

No learning into a shared graph during play. A failed flight is a private, timed avoid for that bot; the baked file
changes only when re-baked. This removes the BM-3 class of bug and makes every world start from the same data.

### Verification: getting decisive answers

1. **Decide in the cheapest layer that can see the effect.** Kernel changes: parity, exact. Bake changes: route
   time and chain length from fixed start states in the offline tool, thousands of trials, no noise. Follower
   changes: replay identity or a per-event count. Only team behaviour needs laps worlds.
2. **Name the effect size and the seed count before running.** If the smallest effect worth having needs more
   worlds than are affordable, do not run the arm; pick a nearer metric (the event the change targets, per
   opportunity).
3. **No setting without an owner decision to keep it.** A change is in or out when its arm ends. Undecided means
   out, except a correctness fix, which is accepted by a test, not a campaign.
4. **Freeze the control per milestone.** Skill, Force profile and defaults do not move inside one.

### First milestone (cheap, no campaigns)

- **M0, one session, no code:** measure client frame time on a heavy scene with 0 and 7 current bots. This says
  how bad today's design is locally and sets the real budget.
- **M1, the kernel spike:** the re-entrant kernel and collision copy in a standalone tool for Kejim. Pass:
  zero-mismatch parity on recorded rollouts; a measured rollouts-a-second figure per core.
- **M2, the answer to the speed question:** an offline exhaustive stand-to-flag search on Kejim level 1 with that
  kernel, at strafe skill 90 and 100. It tells us, without a single game world, whether a 15 s chain exists in the
  current action vocabulary and what a full-map bake would cost.

If M1 cannot reach parity, or M2 finds no chain near 15 s, the direction is wrong and little has been spent.

## 7. Open questions for the owner

| Question | Recommendation |
| --- | --- |
| Is about 15 s on the level-1 stand still the goal, and at what strafe skill? | Keep it as a reference; set the gate after M2 says what skill 90 can reach. |
| May the bot module carry its own copy of the move code and collision? It departs from "small upstream calls". | Yes. It is the only route off the game thread; parity keeps it honest. |
| Must seeded worlds still replay exactly? | Yes, through fixed-latency commits. It is the best tool the project has. |
| Bake shipped with maps, or built on the player's machine at first load? | Ship it for the 14 maps; build at idle for others, with the walk-only fallback meanwhile. |
| Carry the current live search forward as the fallback? | No. Fallback is the cell route; keeping both is how the settings pile grew. |
| Which maps and modes first? | Kejim and Dantooine 4v4, as now; they have the controls and human data. |
| Night City-class worlds: are bots expected there? | Decide before sizing the collision copy; they change memory and bake by orders of magnitude. |

## Not verified

- The total bake time behind the owner's "long bake" remark. The records give worlds per class and one hour for one
  map's re-explore chain; I found no end-to-end figure for all maps.
- Whether search during flight was added after 2026-09-18 (later findings mention a landing pre-search).
- The setting count is a token count of the sources.
- Every budget in section 6 is an assumption until M0 and M1 are measured.
