# Defender falls, route stops and the pit re-take (2026-09-30)

Owner: "review code and plan how to tackle these: (1) chase D still falls
about 1.5 times per world, (2) almost all kills are at the stand, (3) the
runner follows a dropped flag into the lethal chasm pit". This record is the
review of the retained worlds and the plan. It follows
[defender gaps](2026-09-29-defender-gaps.md). Everything below was read from
retained evidence; no native run was made for it.

Worlds read: `.codex-research/goal-20260930-a/` (`stand-1..8`, `ctl-5..8`),
`.codex-research/def-20260930-a/` (`def-1..4`) and
`.codex-research/ret-20260930-a/` (`ret-1..4`). Scripts:
`.codex-research/falls-20260930-a/` (`falls2.py`, `falls3.py`, `retake.py`,
copied from the session scratchpad).

## 3. The pit re-take is a measurement artifact, not a runner fault

`ret-20260930-a/flags.py` calls a carrier kill "re-taken by the runner"
when the runner's first carrying play-log sample after the kill is over
250 u from the stand. The kill's time in `play-game.log` is a whole second,
so that "first sample after the kill" is the runner's own death sample:
still carrying, at the pit floor (z 32) or the ledge, up to a second before
the kill line.

- All 9 "re-takes" in the ret arm are sample index 0 after the kill second,
  with no non-carrying sample before them and the runner's health at 1-90:
  its landing. (`retake.py`, second pass.)
- Requiring a respawn first (a non-carrying sample, then a carrying one):
  over 16 worlds and 94 carrier kills, 77 flags were returned by a defender,
  13 went home by the timer, 3 were re-taken on the chasm ledge (z 1368,
  1394, 1829) and **0 in the pit**.
- The runner's own `[OBJECTIVE] dropped_flag` lines never name an origin
  below z 100. The print is suppressed when the entity number and verdict
  repeat, so this is weak on its own; the play-log count above is the
  evidence.

So `ReachableDrop` (`ai_bot2_objective.c`) is not shown wrong, and the runner
never carried at the pit floor after a respawn. The fix is in the analysis:
`flags.py` (and the gaps record's "Most re-takes are in the pit" bullet and
the handoff's item 3) must require a respawn before a re-take. No runner
change.

## 1. Where the defender falls come from

43 defender deaths by `MOD_FALLING` in 20 worlds (chase D 24, base D 19),
each read back from its last `[MOVE] client=N` line on the upper floor
(z above 1,700) (`falls3.py`):

| Mechanism | Chase D | Base D |
| --- | --- | --- |
| A no-path decline **hold** taken while moving 100-640 u/s on the upper floor, then over the lip | 15 | 15 |
| A hop in flight, then a goal change (`plan reason=objective`) reset it airborne | 3 | 3 |
| A hop in flight while base D's throw at the runner hit (the chaser 100-200 u from the runner) | 3 | 0 |
| A knock recovery search on the lip refusing all candidates | 2 | 0 |
| Other (`objective_held` mid-air) | 1 | 1 |

**The hold.** `DeclineHeld` (`bot2_move.c`) answers a `no_path` decline with
the walk's brake. On the ground that stops the bot. A strafing defender is
airborne most frames, and airborne the brake is no input at all: momentum
carries it over the chasm lip. `KnockCommand` already shows the right air
behaviour (face a heading, hold forward: PM_AirMove's air control); the hold
never uses it. The trigger of the fast holds, both defenders, 218 of 475:
`plan reason=objective`, that is a goal change (`Move_Reset`, then a replan
that fails). Others: `floor_startsolid` 47, `strafe avoid` 40, `walk start`
37, `miss hop0 knocked` 11.

**Why the replan fails: the posts are off the graph.** Chase D logged 1,393
`no_path` plans in the eight stand worlds, every one `to=-1`: the goal has
no cell (`Graph_Nearest(objective, 64, 64)`). 1,276 of them were under the
`AMBUSH post=-1` goal, the chaser's own post `194 -143 1797` from
`tools/fixtures/movement/defender/posts.json` (Kejim red `chase_posts`),
the rest `POST_MOVE` (74) and `REARM` (43). Base D: 83 `to=-1`, 49
`from=-1`. So every return home is a hold, and a hold taken at speed beside
the chasm is a fall. Whether that post is over the void or on a lip cell the
graph did not admit is to be checked against the graph cache before it is
moved.

**Goal churn.** Chase D's movement goal flips between the own stand
(`PURSUE`), the enemy stand (`CHASE`), the post (`AMBUSH`/`CHASE_END`), the
block, a dropped flag (`RETURN`) and items (`REARM`) within hundreds of
milliseconds (`CHASE_END` 129850, `PURSUE`/`CHASE` 130125, `CHASE_END`
130350 in `stand-1`). Each flip is an objective reset. `ArriveSafe` holds a
reset only between the run-up and the last landing of a committed sequence;
resets at other airborne moments go through.

**Team throws.** `ForceThrow` (`w_force.c`) excludes teammates only from the
weapon pull; the push and pull move them. Base D throws at a runner that
chase D is chasing at 100-200 u; three chase D falls have base D's `THROW`
and `HIT` inside the chaser's flight.

**Not the cause:** the aim (the record's earlier finding stands), and the
pit floor: a defender that reaches z 32 stands there 2-7 s before the
`fallingToDeath` kill, so every fall is decided on the upper floor.

## 2. Carriers that clear the stand are never engaged again

Eight stand worlds (`compare.py` world rule, carries from the game log,
shots from `FIRE` lines):

| Carries | Count | Duration (median) | Chase D shots (median) | Carries with zero chase D shots | Chase D throws (median) | Base D shots (median) |
| --- | --- | --- | --- | --- | --- | --- |
| Captured | 15 | 15 s (11-18) | 0 | 10 of 15 | 1 | 0 |
| Stopped | 72 | 0 s | 1 | 16 of 72 | 0 | 1 |

A stop is decided in the grab second. A carrier that leaves the stand runs
11-18 s home with, in two thirds of the carries, not one shot from chase D
and about one pull. Causes to establish, in order: chase D is in a hold or
a fall when the carrier breaks out (section 1: the post goal); with the
stand goal it runs the carrier's own route behind it, out of sight or range
for the 1,200 ms continuous-sight engage delay at 500-700 u/s; the pull
(up to 1,000 u) is not followed by a close-range exact shot.

## Plan (for the implementing session)

Order matters: the fall fixes may also free chase D for the route, so the
route work is measured after them.

**Step 0, analysis fix (no build).** `flags.py`: a re-take needs a
non-carrying sample after the kill before the carrying one. Re-run it over
the ret and stand worlds and correct the gaps record with a dated note (keep
the old text; do not delete evidence). Copy the three scripts into
`.codex-research/falls-20260930-a/`.

**Step 1, posts on the graph** (`bot2_defender.c`, `posts.json`). At the
release (and whenever a post list cvar changes) map every post, ambush post,
guard post and mine spot to a cell: `Graph_Nearest` with a wider radius
(128 u, 64 u tolerance), snapping the goal to the cell's centre and printing
a `POST` line with the snap distance, or refusing the post with a print when
none is found. Check the Kejim red chase post against the graph cache first
and move it in `posts.json` if it is over the void. Acceptance: chase D
`no_path to=-1` per world from about 170 to near 0.

**Step 2, no input is never the airborne answer** (`bot2_move.c`,
`DeclineHeld`). While `ps->groundEntityNum==ENTITYNUM_NONE`, the hold steers
with `KnockCommand`'s air control toward the last grounded origin (record it
every grounded frame per client), so a hold taken mid-strafe pulls the bot
back over the floor it left. Grounded, unchanged. Runner and guest alike;
the runner's floor (36/36) and the level-1 gate must not change. Log the
airborne hold frames (`recover reason=hold_air`, count and heading) so the
effect is countable.

**Step 3, goal latch for guests** (`bot2_defender.c`). One `DefenderGoal`
wrapper in front of every `Bot2_ExecuteMovement` call: while the defender is
airborne, or within 250 ms of the last goal change, the previous goal is
kept. Print `GOAL_HELD` with the reason. This removes the airborne objective
resets and the sub-second churn without touching the runner.

**Step 4, no throw through a teammate** (`bot2_defender.c`). Before a
lethal throw or a carrier pull, replay `ForceThrow`'s cone and radius on the
other defender; if it is inside, and airborne or within 200 u of a lethal
drop, hold the throw (`THROW_HELD reason=teammate`). Shots are unaffected.

**Step 5, measure.** Fresh unique build. `tools/defense_laps.py`, gaps ctl
configuration, four worlds a wave, eight worlds; control is the eight
`goal-20260930-a/stand-*` worlds (same sources but for the defaults made
since, so re-pass `bot_chaserGoal 1` as the control did). Hash
`play-game.log`, check `live/jampgamex86_crash.txt`. Extend
`goal-20260930-a/compare.py` `ARMS`. Acceptance: chase D and base D falls
each at or under 0.5 per world; captures per ten minutes not above the
control's 1.9 (per-world spread 0-4); stops per grab at or above 80%.
Then the runner floor (`movement_bench.py`, Kejim class 150, 36/36) for the
`DeclineHeld` change.

**Step 6, the route (after step 5).** Add a per-carry route diagnostic:
every 250 ms of a carry, chase D's distance to the carrier, visible,
in range, the `PRESHOT` reason and its movement event; summarise per carry
at its end (`CARRY` line). Read the escaped carries. Then, one lever at a
time, four worlds each, eight for the winner:

- L1, route-ahead goal: instead of the far enemy stand, chase D's goal is
  the cell about 500-700 ms ahead of the carrier on the carrier's graph
  route to its stand (always on the graph), falling back to the stand when
  the carrier is unseen for over a second.
- L2, base D's chase: it has the leash off against a lone attacker, but
  logged zero shots on most escaped carries; find where it is at the
  break-out (block goal, mines, rearm) and whether it chases at all.
- L3, pull then close: after a carrier pull hits, the chaser runs at the
  carrier's landing cell for `bot_defenderFollowMs` so the exact aim
  inside 200 u gets its shot.

Acceptance for the route: route stops per ten minutes from 0.5 to at least
2 with captures at or under the control and no fall regression.

**Docs.** Update this record's sections with results, the handoff's top
section (state, open list) and PLAN's status log line for 2026-09-30.
Do not push. Leave the work uncommitted for the owner unless told otherwise.

## Results (2026-09-30, implementing session)

Builds: `falls2ctl-build-20260930-a` (HEAD `2f08678d`, built before any edit), `falls2-build-20260930-a`
(steps 1-3 and the carry log) and `falls2-build-20260930-b` (+ the guest lip guard and L4). Campaign
`.codex-research/falls2-20260930-a/` (`run.sh` to `run7.sh`, `run.log`); scripts `compare.py` (extended from
goal-20260930-a), `carry.py`, `fallflight.py`, `armed_at_carry.py`. Every arm uses the gaps ctl configuration
exactly as the goal-20260930-a stand arm ran it (`bot_chaserGoal 1` passed, `--chaser-ambush` unless the arm
says "no ambush"), 10 game minutes a world, four worlds a wave. Every world's `play-game.log` is distinct, and
there is no `jampgamex86_crash.txt` in any of them.

### Step 0: the re-take rule (done)

`flags.py` (both copies, `ret-20260930-a` and `goal-20260930-a`) now needs a respawn (a non-carrying sample)
before a carrying sample counts as a re-take; the old scripts are kept as `flags.pre-fix.py.txt`. Re-run:
no-return arm 2 re-takes (was 10), return arm 2 (was 9), goal ctl 0, stand 1. The five re-takes are at
z 1901, 1940, 1394, 1368 and 1829: none in the pit. Dated correction in the
[gaps record](2026-09-29-defender-gaps.md#chase-d-returns-the-dropped-flag-2026-09-30). No runner change.

### Corrections to the diagnosis above

- **The off-graph goal was not the chase post.** `194 -143 1797` has a cell 15 u away (10317). The 1,393
  `to=-1` plans belong to ambush posts 1 (1,089) and 2 (304), `923 1486 1887` and `571 1276 1887`: recorded
  79 u above the walkway floor (the owner airborne), outside `Graph_Nearest`'s 64 u tolerance. The blue
  mirrors had the same fault. `posts.json` now has them at 1808; every other post and mine spot is within 18 u
  of a cell.
- **Base D's throws cannot move chase D.** `ForceThrow` skips `OnSameTeam` clients before any push or pull, and
  in CTF that is every teammate. The three "team throw" falls are chase D hops already in flight 0.7-2.2 s
  before base D's throw, and the HIT lines are the runner's velocity. Step 4 was not built: it would have held
  throws for nothing.
- **Most chase D falls are hop landings, not holds.** Read from the flight's launch rather than the last
  upper-floor line (`fallflight.py`), chase D's falls in the control worlds are committed hops launched from the
  walkway beside its post (x 200-470, y -110 to -440) that fly 2.5 s into the chasm. In the one read in full
  (`gctl-1`, 176075) the admitted two-hop rollout landed both hops on DROP-flagged lip cells (`a0_flags=1`,
  `a1_flags=1`) at 410-490 u/s; the live second hop missed the lip. The holds are mostly base D's.
- **The stand-goal control does not reproduce.** `goal-build-20260930-a` re-run now with the same settings
  (`gctl`, 8 worlds) gives 4.0 captures per ten minutes and 70% stops, not 1.9 and 83%; HEAD with the committed
  posts (`octl`) gives 4.5 and 67%. So the valid control is these 16 worlds, and the 1.9 of the gaps record was
  a lucky set of eight.

### What was built

- **Step 1** (`bot2_defender.c` `SnapPoints`): at the release every post, ambush post and mine spot is snapped
  to its cell's centre (`Graph_Nearest` 128 u, 64 u tolerance; a point off the floor is first dropped onto
  it) or refused; `POST_SNAP` lines. The release runs from an rcon command, so those lines go to the rcon
  reply that `defense_laps.py` discards; the effect is read from the `to=-1` counts.
- **Step 2** (`bot2_move.c` `DeclineHeld`, `bot_moveHoldAir`, default on): an airborne hold steers with the
  knock recovery's air control towards the last grounded origin (per client, kept across `Move_Reset`; within
  3 s and 768 u, else against the velocity). `recover reason=hold_air` lines. Grounded unchanged.
- **Step 3** (`DefenderMove`, `bot_defenderGoalLatchMs`, default 250): every defender movement goal goes
  through one latch; a goal more than 64 u from the held one waits while airborne or within 250 ms of the last
  change. `GOAL_HELD` lines.
- **Step 4**: not built (see above).
- **Step 6 diagnostic** (`bot_defenderCarryLog 1`, default 0): `CARRY_SAMPLE` every 250 ms of a carry and a
  `CARRY` summary per defender.
- **L1** (`bot_chaserGoal 2`, `bot_chaserAheadMs` 600) and **L3** (`bot_chaserPullClose`), default off.
- **Guest lip guard** (`bot2_move.c` `LethalLip`, `bot_moveGuestLip`, default on, guests only): a guest's
  candidate whose hop lands on a DROP/HURT/LIQUID cell with no floor within 512 u past a refused side is refused
  (outcome `lip`). Added after the reading above.
- **L4** (`bot_chaserRearmChase`, default 0): an unarmed chase D (no loadout weapon stocked) rearms during a
  chase. Added after the carry diagnostic.

### Measured (8 worlds an arm unless noted; rates per ten game minutes)

| Arm | Build | Captures (per world) | Stops per grab | Route stops | Chase D / base D falls | Chase D `to=-1` per world |
| --- | --- | --- | --- | --- | --- | --- |
| goal stand (recorded) | goal | 1.9 (4, 2, 4, 1, 0, 1, 2, 1) | 83% | 1.4 | 12 / 10 | 174 |
| `gctl`: goal build, re-run | goal | 4.0 (6, 4, 2, 2, 6, 6, 1, 5) | 70% | 1.7 | 11 / 10 | 175 |
| `octl`: HEAD, committed posts | ctl | 4.5 (6, 4, 4, 4, 4, 5, 4, 5) | 67% | 1.4 | 11 / 6 | 148 |
| `hctl`: HEAD, corrected posts | ctl | 4.0 (7, 2, 6, 6, 7, 3, 0, 1) | 67% | 1.9 | 26 / 10 | 0.5 |
| `fix`: steps 1-3, corrected posts | a | 5.1 (7, 7, 4, 6, 6, 5, 3, 3) | 64% | 2.3 | 25 / 7 | 0.8 |
| `snap`: posts only (4 worlds) | a | 4.6 | 66% | 1.0 | 8 / 6 | 16 |
| `base`: no ambush, latch and air hold off | a | 4.1 (4, 7, 6, 5, 4, 3, 3, 1) | 64% | 0.9 | 11 / 8 | 0.2 |
| `noamb`: no ambush, steps 1-3 | a | 3.7 (4, 4, 4, 7, 2, 3, 2, 4) | 73% | 1.9 | 11 / 6 | 0.1 |
| `lip`: as noamb + lip guard | b | 4.2 (2, 2, 5, 5, 5, 5, 5, 5) | 66% | 0.4 | 9 / 8 | 8 |
| `lipamb`: as fix + lip guard | b | 4.8 (5, 6, 5, 6, 2, 6, 5, 4) | 64% | 2.4 | 13 / 10 | 8 |
| `rearm`: as lip + L4 | b | **2.5** (4, 5, 2, 0, 2, 3, 2, 2) | **79%** | 1.5 | 10 / 8 | 10 |
| `ahead`: as lip + L1 (4 worlds) | b | 4.0 (3, 4, 5, 4) | 71% | 0.2 | 2 / 4 | 1 |
| `close`: as lip + L3 (4 worlds) | b | 3.2 (5, 3, 2, 2) | 75% | 2.4 | 3 / 3 | 2.5 |

- **Step 1 works as a fix and hurts as a change of position.** Chase D's `to=-1` plans fall from about 175 to
  under 1 per world. But with the ambush posts reachable, chase D walks about 1,700 u forward along the chasm
  walkway to hold them, and its falls double (`octl` 11 → `hctl` 26, `fix` 25). In the committed state it stood
  braked on its own post.
- **Steps 2 and 3 change nothing measurable.** Without ambush, `noamb` against `base`: chase D 11 against 11,
  base D 6 against 8. The airborne hold fires about 100 times a world and the latch holds a goal about 160 times,
  but base D's falls begin in a hold taken already past the lip (one read: 13 u out, falling at 277 u/s), which
  air control cannot recover.
- **The lip guard halves chase D's falls where it matters.** With the ambush posts reachable, 25 → 13
  (`fix` → `lipamb`). Without ambush, 11 → 9 (inside the spread).
- **Acceptance fails.** No arm reaches 0.5 falls a world for either defender. The best is `lip`, about 1.1
  (chase) and 1.0 (base). Captures in every step 1-3 arm stay inside the valid control's range.
- **Runner floor** (Kejim, `--skip-twins`, classes 150 and 50, 24 cases each, `floor.sh`): builds a and b are
  identical to the control build in every case (344.07 s and 339.90 s, parity 0), with 74 airborne holds taken
  in the class 150 and 50 worlds. The level-1 gate was not run.

### Step 6: the route

**The carry diagnostic** (`fix` worlds, 41 escaped carries, `carry.py`):
- Chase D had the carrier in sight 87% of the samples and in range 72%, with a median closest approach of
  34 u, yet fired in only 6 of the 41 carries.
- 63% of its shot decisions in those carries were `weapon`: no loadout weapon stocked, raising or dropping,
  or a hand extend.
- Base D chased in 98% of the samples (the stale `blocking` flag is set while chasing; `carry.py` reads
  chasing first). It was mostly blocked by `weapon` (37%) and `range` (28%).

**The cause** (`armed_at_carry.py`: whether chase D died since its last weapon or ammo pickup when the grab
came):

| Arm | Armed at the grab: captured | Unarmed at the grab: captured |
| --- | --- | --- |
| goal stand (recorded) | 8 of 73 (11%) | 7 of 14 (50%) |
| `fix` | 19 of 81 (23%) | 22 of 38 (58%) |
| `lip` | 19 of 83 (23%) | 15 of 21 (71%) |
| `rearm` (L4) | 13 of 88 (15%) | 7 of 12 (58%) |

A respawn keeps only the spawn loadout, and rearming waited for the chase to end. So chase D's falls cost
captures twice: once when it dies, and again at the next grab, which it meets unarmed.

**Levers.**
- **L2 (base D's chase)** was answered by the diagnostic: base D chases. The limit is its weapon and range.
- **L4** (8 worlds): captures 4.2 → 2.5 per ten minutes and stops 66% → 79% against `lip`, below every control
  world set. The winner.
- **L1** (4 worlds): no effect.
- **L3** (4 worlds): fired only 9 times, so its 3.2 captures are not its effect.
- **Route acceptance is not met.** L4's route stops are 1.5 per ten minutes against the target of 2; its gain is
  in carries that never leave the stand.

### Open

- **Falls.** They stay near 1 a world for each defender. What remains:
  - chase D's hops over the lip that the guard does not cover (one-hop landings near lips, 3-5 an arm);
  - base D's walks off the walkway steps under a `floor_missing` brake, which then become holds;
  - chase D's own lethal throw or shot inside a committed flight (4-5 of the falls in some arms). Pmove does not
    restrict the thrower's movement, and `Throw` overwrites `generic_cmd` (a Speed purchase that frame would be
    dropped). This is unverified.
- **Defaults to decide (owner).**
  - `bot_chaserRearmChase` (measured win, off).
  - Whether the corrected ambush posts should stay in the laps configuration (they cost falls).
  - Whether to keep the airborne hold and the latch at all: no measured benefit, and the hold touches the runner.
- **Base D's `to=-1` plans** rise with L4 (105 a world): item goals off the graph. Chase D now takes the items
  base D targeted.

## Second attempt (2026-09-30, later session)

Builds: `falls3-build-20260930-a` (falls2 b + inert diagnostics), `-b` (+ a self-splash hold and the backpedal
clamp), `-c` (the hold replaced by a brace; + `bot_baseRearmChase`), `-d` (+ a knock-safety hold, measured and
reverted). Campaign `.codex-research/falls3-20260930-a/` (`run.sh` to `run5.sh`, `run.log`); scripts
`fallcause.py` (per fall: knocks, own splash shots, throws, the last guest hop, the last `floor_missing`),
`guesthop.py` (guest hops against their rollout), and copies of `compare.py` and `carry.py`. Every arm is the `lip`
configuration (no ambush, `bot_chaserGoal 1`, `bot_defenderCarryLog 1`), 10 game minutes, eight worlds, four a wave.

### Diagnostics (build a, inert prints)

- `GUEST_HOP` (`bot2_move.c`, guests only): at each hop's launch and landing, the committed rollout's
  position and speed against the live ones, with the predicted cell's flags and `LethalLip` verdict.
- `CARRY_SAMPLE weap=`: the `weapon` refusal split into `noweap` (no loadout weapon owned), `hand`, `switch`
  and `ammo`. `THROW replaced= self_air= self_speed=` and `FIRE self_air=`.

### What the falls were

- **Base D knocks itself off.** Its own rocket or repeater alt at a runner 45-120 u away, then a knock of
  dv 180-480 u/s within about 75 ms. It led 22 of 77 defender falls in the saved falls2 arms and 9 of base D's
  12 in the diagnostic arms.
- **Chase D's hops launched slow.** 510 of chase D's 1,184 launches (43%) left at under 0.9 of the rollout's
  launch speed, and 33% under 0.8; base D's 1% (9 of 717). Its falling hops were predicted to land on safe floor
  (no DROP flag in most), and the live flight went into the pit 450-1,700 u away. The cause is the aim, not the
  planner: `SetView` turns the move with the view, a runner behind makes `forwardmove` negative, and
  `BG_AdjustClientSpeed` runs a grounded player backwards at 0.75 speed. The rollouts never see it.
- **Throws are not a cause.** No `THROW` replaced a Speed purchase (`replaced=0` in all 546). One of 100
  airborne throws was followed by a fall. Source 3 of the first attempt is closed.

### Route (problem 2)

The weapon split over escaped carries (`dlip`, `drearm` = L4): base D's `weapon` refusals are mostly `noweap`
(474 of 732 without L4, 810 of 1,179 with it), then `ammo`. It chases with no loadout weapon at all. Under L4,
chase D's are mostly `ammo` (120 of 253).

### Fixes and levers measured (rates per ten game minutes)

| Arm | Build | Settings | Captures | Stops per grab | Route stops | Kill% | Chase D / base D falls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `c` | b | splash guard off, clamp off | 3.3 | 73% | 0.9 | 69% | 9 / 5 |
| `c3` | c | as `c` (7 distinct logs) | 3.5 | 74% | 1.6 | 63% | 9 / 8 |
| `sp` | b | hold close splash shots | **8.6** | 59% | 2.6 | **32%** | 11 / 3 |
| `bp` | b | backpedal clamp only | 3.7 | 72% | 1.6 | 75% | 6 / 11 |
| `br` | c | brace only | 3.3 | 72% | 1.0 | 66% | 5 / 6 |
| `def` | c | brace + clamp (defaults) | 3.3 | 73% | 1.7 | 72% | 3 / 1 |
| `defr` | c | as `def`, replicate | 3.6 | 73% | 1.1 | 64% | 6 / 9 |
| `bre` | c | defaults + `bot_baseRearmChase 1` | 2.7 | 77% | 1.1 | 75% | 4 / 6 |
| `ks`, `ksb` | d | defaults + knock-safety hold (16 worlds) | 3.1, 5.1 | 75%, 60% | 1.2, 0.9 | 64%, 60% | 7 / 14 |

- **Holding close splash shots is withdrawn.** It cut base D's falls, but captures rose from 3.3 to 8.6 and
  the kill rate fell from 69% to 32%: the close shots are the kills. Owner (2026-09-30): keep the damage and
  counter-steer against the push; the repeater's bolts are no fallback, since the runner's saber reflects them.
- **The brace** (`bot_defenderSelfSplash`, default 1, `Brace`): the shot is fired as before. For its flight time
  plus 400 ms the defender, on the ground, leans into the burst where the floor goes on (else brakes), and once
  knocked it runs against its velocity. About 80 braces a world, 45% with a detected knock. Captures are
  unchanged.
- **The backpedal clamp** (`bot_defenderBackpedal`, default 0, `SetView`): on the ground the aim may not turn a
  moving defender past 89 degrees from its travel, and no shot is fired along a held-back view. Chase D's slow
  launches went from 41% to 7% (under 0.9) and from 32% to 1% (under 0.8); the landing miss p90 from 65 to 40 u.
  Captures are unchanged.
- **Falls, pooled.** Controls (`c`, `c3`, 16 worlds): chase D 18, base D 13. Brace + clamp (`def`, `defr`, 16):
  chase D 9 (0.56 a world), base D 10 (0.63). `def` alone met the 0.5 target (3 and 1); its replicate did not
  (6 and 9). Chase D's halving holds across every arm with either fix (5-6 in eight worlds, against 9 and 9).
  Base D's change is inside the spread. Acceptance is not met for base D; it is nearly met for chase D.
- **The knock-safety hold** (build d, reverted): a close splash shot waited while its predicted knock
  (5 u/s per splash damage point, carried 200 and 400 ms) would leave the shooter over no floor within 128 u.
  Base D still fell 14 times in 16 worlds, 7 of them after its own splash. The knock model or the floor test is
  wrong for the walkway steps; not pursued further.
- **Base D rearming while chasing** does not help the route (1.1 route stops) and its falls rise to 6. Off.
- **Runner floor** (Kejim, classes 150 and 50, `--skip-twins`): build c is identical to falls2 b in all 48
  cases (times and parity 0). The level-1 gate was not run; `bot2_move.c` changed only by the guest-only
  `GUEST_HOP` print.

### Open

- Base D's splash falls under the brace: at 50-75 u the rocket arrives in one or two frames and the knock lifts
  the shooter (vz about +50), so neither the lean nor the ground braking gets time. Read the falls from the
  press: the velocity and floor at the press, and where the knock carried it. Candidate next steps: a better knock
  model checked against measured `knock dv=` lines, then air control against the knock (the movement's knock
  recovery is the owner of that frame), or no close splash shot while running at a lip, found from the graph's
  DROP cells rather than a trace.
- Route: still about 1.1-1.7 route stops per ten minutes against 2. Base D's blocker is `noweap`. A rearm walk
  during the chase did not help.

## Owner decisions round (2026-09-30)

Owner: L4 on by default, ambush posts on in the laps configuration ("max falls, now we can't hide"), the airborne
hold and the goal latch removed; fall fixes must keep the defender's abilities (no holding close splash shots, no
"stop aiming in the air"; those serve as diagnostics only). Builds `falls3-build-20260930-e` to `-i`, campaign
`.codex-research/falls3-20260930-a/` (`run6.sh` to `run9.sh`); `worldfalls.py` gives per-world falls, the median
world, loop worlds and respawn deaths. Every arm: the full configuration (ambush on), 16 worlds.

| Arm | Build | Chase D / base D falls | Median world | Captures | Route stops |
| --- | --- | --- | --- | --- | --- |
| `ful` control (brace + clamp, L4, no hold/latch) | e | 18 / 13 | 1.0-2.5 | 3.2-3.5 | 4.2-6.1 |
| `h` defaults | h | **13 / 9** | 1.0-1.5 | 3.8-4.1 | 2.8-3.5 |
| `hk` knock safety off | h | 21 / 26 | 2.0-4.0 | 2.2-3.7 | 3.8-4.2 |
| `hf` guest finish off | h | 14 / 9 | 1.0-1.5 | 3.2-3.8 | 2.0-3.4 |

- **The respawn loop.** A committed sequence from before a death held the new objective through the guest finish
  hold (build g), and the dead plan's run-up walked the respawned defender into the pit exactly 8,925 ms after
  every respawn at the lower spawns (26 falls in `aab-5`, 23 in `lpb-1`). A defender's death does not pass
  through `Bot2_Think`'s movement reset. Fix: the defender resets its movement at its respawn. Earlier builds
  showed the same signature a few times (the arrival hold across a death). No loop world on build h.
- **Knock safety v2** (`bot_defenderKnockSafe`, default 1): a close splash shot waits, leaning into the burst,
  only while its knock (the 90th percentile of measured self-knocks by weapon and burst distance, lift 0.6, push
  0.8, on top of the shooter's velocity) would land or slide it off same-level graph floor. Off: base D 26 falls,
  on: 9; captures are not above the control.
- **Removed, no measured benefit:** the guest slow-hop decline, the guest finish hold, base D's rearm while
  chasing, the route-ahead goal (L1), the air-aim hold (a diagnostic), the any-lip refusal (harmful: 46 / 19).
  The clamp and the brace are plain behaviour now. New settings against HEAD: `bot_chaserRearmChase` (1),
  `bot_chaserPullClose` (0), `bot_defenderCarryLog` (0), `bot_defenderKnockSafe` (1), `bot_moveGuestLip` (1).
- **Runner floor** identical to HEAD (Kejim classes 150 and 50) on builds e, g and h; `bot2_move.c` now differs
  from HEAD only by the guest lip guard and the guest-only `GUEST_HOP` print.
- **Status against the target** (0.5 falls a world per defender, ambush on): chase D 0.81, base D 0.56.
  Chase D's remaining falls are mostly hops whose live flight leaves the rollout mid-air after a matched launch
  (the aim turning the move in the air is the suspect; the fix must keep aiming, e.g. a rollout that models the
  turned command, or a view turn that keeps the air-strafe angle).

## Chase D's flights: the guest flight trace (2026-09-30)

Builds `falls3-build-20260930-i` to `-s`, campaign `run9.sh` to `run17.sh`; scripts `worldfalls.py`, `falltax.py`
(fall class by the state when the defender left the upper floor). Full configuration (ambush on).

- **Where the falls are** (80 default-equivalent worlds, builds h-k): chase D 0.70 a world (hop 21, own splash 10,
  hold 9, walk at speed 8, creep 6), base D 0.54 (hold 17, walk at speed 10, own splash 7, hop 6).
- **Guest walk brake** (`bot_moveGuestBrake`, default 1, guests only): a guest's momentum chord covers its stopping
  distance, and after a hazard brake it backs away from the lip for 300 ms. The creep class went to 0 in its arms;
  total falls unchanged (22 against 22, 16 worlds each). Kept: a measured class effect, guest-only.
- **Body check** (build k, removed): refusing guest hops through another player's extrapolated path fired 2,223
  times and tightened landings (p90 miss 20 to 13 u); fatal hops 5 against 5. Removed.
- **The trace** (`bot_moveGuestTrace 1`, default 0): at a guest's straight commit the committed plan is rolled out
  once more and its frames kept; the first live frame off the rollout (4 u or 25 u/s) prints `GUEST_DIVERGE` with
  the live and predicted state, the executor state, the movement's own command (`Move_GuestRawCommand`) and the
  final one. Findings, in order:
  1. The executor agreed with its rollout at most divergences; the defender's command differed.
  2. The idle aim (`Aim`, used while moving to or holding an ambush post, where guardMoving is not set) turned the
     run backwards unclamped. Fixed: it clamps; only the throw turns freely.
  3. The movement itself brakes and steers with the back key on the ground, and its rollout ran at 0.75 speed;
     the clamp turned that forward, so the live run was 1/0.75 faster than the rollout (220 against 176 u/s).
     Fixed: a backwards movement command keeps the movement's view that frame, and no shot goes along it.
     Early divergences 71 to 24 in four worlds.
  4. Airborne at think time but touching down within the command, the aim's backwards turn paid the backwards
     speed in the substeps after touchdown. Fixed: the clamp applies when `Move_GroundedThisFrame` (standing or
     `LandsWithin`). Aim-turned-backwards landing frames 58 to 4.
  5. What remains: body contacts in the air (the nearest player 38-55 u, 350-400 u/s lost in a frame) and executor
     decisions that differ from the rollout (not yet explained).
- **Outcome on build s** (16 worlds): chase D 0.69 a world (own splash 7, hop 3), base D 0.62; hop falls 0.19 a world
  against 0.26 before; totals inside the spread. Captures 3.7-3.8, route stops 3.1-3.6, kill rate 58-60%.
  Runner floor identical to HEAD.
