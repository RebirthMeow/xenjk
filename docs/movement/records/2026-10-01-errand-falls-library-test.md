# Would a defender tape library fix the hopped-errand falls? (2026-10-01)

Question (owner): errands have been walked since 2026-09-29 (`bot_defenderWalkErrands`) because hopping them put the
defenders in Kejim's chasm. Would a tape library (offline-explored, pre-verified legs) fix those falls?

**Setup.** Build `errtape-build-20261001-a` (sources = HEAD c3b93398), `.codex-research/errtape-20261001-a/`
(`run.sh`): 16 laps worlds an arm, 600 game s, rearm, reset, 2 pedestal mines, ambush, the owner's loadout,
`bot_chaserGoal 1`, `g_fixGolanDamage 1`. Arm `walk` adds `--defender-walk`; arm `hop` hops errands. Falls are
classified by `falls_mode.py` (mode at the commit of the sequence: errand, or chase / block / return; mechanism from
the last upper-floor event, after falls3.py), flight fidelity by `divergence.py` (GUEST_HOP prediction against live).
Outputs `falls_mode.txt`, `divergence.txt`.

| arm (16 worlds) | base D falls | of which errand | chase D falls | of which errand | captures | runner deaths |
|---|---|---|---|---|---|---|
| walk | 0.44 | 0.06 | 0.81 | 0.12 | 3.88 | 35.2 |
| hop | 0.75 | 0.50 | 1.44 | 0.50 | 3.69 | 37.6 |

The problem is still real on HEAD: hopped errands add about 0.8 falls a world (errand falls 16 against 3).

**Flights follow their rollouts.** Over 4,145 errand hops the landing differs from the guest's own prediction by
p50 0.0 / p90 0.0 u (launch, where the run-up ends: p90 17-20 u). A tape replays rollout decisions through the same
kernel, so it adds no flight fidelity.

**What the 16 errand falls in `hop` are.**
- 8: **a goal change drops a committed sequence in its run-up** (6 by the stricter tag of the run-up hold test below,
  which requires the sequence to be unfinished at the reset). The errand goal moves (item taken, the next rearm
  item chosen, a mine spot) while the defender runs up to a hop near the walkway lip. `Move_Plan` holds a goal change
  only after the launch (`stage>MOVE_STAGE_RUNUP0`); in the run-up it resets, the defender is often already off the
  graph (`no_path`, `from=-1`), recovery holds, and its run-up speed carries it over. The same mechanism is behind 11
  of the 20 falls in `walk` (block, return and chase legs, which hop in both arms).
- 4: hop flights that miss a lip from mid-fight momentum (hop-4: chase ends mid-flight, a new hop is committed at
  737 u/s on landing, launch miss 13 u, the landing misses the walkway and ends 1,170 u away on the pit floor). These
  start from fight states, not from a spawn a library leg would be keyed on.
- 4: knock-related (knock nearby, knock recovery).

**Answer.** A library would not resolve most of it. Its fidelity is already there, and the main cause is the
executor reset on a goal change, which a library avoids only on legs whose goal never changes mid-leg (rearm goals
do). Cheaper targeted test: extend the objective hold to the run-up for guests (or brake instead of holding at
speed when a reset leaves the defender off the graph), then re-run `hop` against `walk`.

## Run-up hold test (2026-10-01, owner: "test the run-up hold for defenders")

`bot_moveGuestRunupHold` (guests only, default 0, uncommitted): `Move_Plan`'s objective hold also covers the run-up
(stage `MOVE_STAGE_RUNUP0`), for at most 1 s. Build `runuphold-build-20261001-a`, `.codex-research/runuphold-20261001-a/`
(`run.sh`, `table.py`, `table.txt`, `falls_mode.txt`), the setup above, 16 worlds an arm, all four arms on one build.
`falls_mode.py` there carries the stricter drop tag and a `no_path hold` tag (recovery holding after the defender lost
the graph).

| arm | base D | chase D | errand falls | dropped seq | no_path hold | hop flight | knock | captures | runner deaths |
|---|---|---|---|---|---|---|---|---|---|
| walk | 0.19 | 0.94 | 0.44 | 0.38 | 0.00 | 0.31 | 0.19 | 3.38 | 38.1 |
| walk + hold | 0.38 | 1.06 | 0.31 | 0.31 | 0.19 | 0.38 | 0.25 | 3.00 | 38.5 |
| hop | 0.25 | 1.38 | 0.50 | 0.75 | 0.06 | 0.69 | 0.12 | 3.19 | 35.8 |
| hop + hold | 0.62 | 1.81 | 0.69 | 0.38 | 0.31 | 1.00 | 0.62 | 3.94 | 36.7 |

(falls per world; class columns cover all falls, both roles.)

- **The hold removes what it targets and nothing improves.** Errand sequence drops go from 6 to 0 with errands
  hopped, but errand falls go from 8 to 11: 5 hop flights and 5 `no_path` holds. All falls 26 to 39 (about
  1.6 standard errors, so not better and possibly worse). The dropped sequence was where those falls ended, not why
  they happened.
- **The hop flights that replace them are a second-hop fault.** hopH-10: chase D's hop0 lands 108 u short of its
  rollout (526 against 636 u/s), and hop1 launches at once, 98 u from its planned launch point, into the pit. Over all
  hops the landing matches its rollout (p90 0 u), but the executor does not re-check a later hop after a landing miss.
- **Arm-to-arm variance is large.** Hop against walk errand falls were 1.00 against 0.19 in the first campaign and 0.50
  against 0.44 here. Pooled over 32 worlds each: errand falls 0.75 against 0.31, all falls 1.91 against 1.19.

Owner decision needed: drop the hold or keep it off. The levers left, by this evidence: re-check (or abort) the next
hop when a landing misses its rollout by tens of units near a lip, and brake instead of holding when a defender loses
the graph at speed (`no_path` hold). A tape library is not indicated.

## Why defender hops land off their rollout (2026-10-01, owner: "chase down WHY these hops are landing off")

8 laps worlds, errands hopped, `bot_moveGuestTrace 1` (first frame a live hop leaves its rollout by 4 u or 25 u/s,
`GUEST_DIVERGE`), build `runuphold-build-20261001-a` with the hold off. `.codex-research/hopmiss-20261001-a/`
(`run.sh`, `classes.py` → `classes.txt`, `followup.py`, `misses.py`). The trace's `last_cmd` is compared with the
rollout's previous frame (`prev_pred_cmd`) by world wish direction; the defender's view turn rotates the keys, so a
different command can push the same way.

| first divergence before the landing | landings | missed > 50 u | rate |
|---|---|---|---|
| none | 3,561 | 5 | 0.1% |
| A: the defender layer rewrote or replaced the movement's command | 100 | 40 | 40% |
| B: same command, another player within 72 u | 65 | 42 | 65% |
| C: the movement's own command differs from its rollout | 75 | 30 | 40% |
| D: same command, state only | 36 | 5 | 14% |

A hop nothing disturbs lands where its rollout says: 5 misses in 3,561. 117 of the 122 misses follow a disturbance.
- **B, collisions (42):** 36 are the two defenders hopping into each other (chase D into base D 20, base D into chase D
  16); 6 are the runner. Neither defender's rollout contains the other.
- **A, the defender layer overrides a committed sequence (40):** within `DEFENDER_ITEM_REACHED` (72 u) of an item,
  the dropped flag or a mine spot the defender stops calling the movement and walks straight (`forwardmove=127`),
  brace does the same, and `SetView`'s clamp paths change the direction on ground frames. Errand goals are items,
  so hops aimed at them meet this at the start; most are frame 1 on the ground.
- **C, the movement's own command (30):** 19 are run-up frames 1-2 after the commit on the ground, where the live
  executor's yaw differs from the rollout's by one turn step (18-27°) with equal executor times. Unexplained; a
  commit-frame offset between the live executor and the rollout is the suspect.

So a library would not help here either: its tapes would meet the same three disturbances. Fixes, by size:
keep the defender layer off a committed sequence (A), make each defender's hops account for the other (B), then find
the commit-frame yaw offset (C).

## Three fixes and a rearm planner (2026-10-01, owner: "build it, only timers they saw themselves")

Built by four agents in worktrees, merged on `movement/defender-hops-integ` (af35aea5; not on `movement/level1-gate`),
build `integ-build-20261001-a`. All default 0:
- `bot_defenderRearmPlan 1`: the item with the soonest usable time, route time from `Graph_Path` cost, a respawn
  timer only for an item the defender took or saw taken (the item's own `nextthink`, read at that moment), teammate
  claims, 1,500 ms hysteresis, waiting at an item due back sooner than another can be reached (REARM_PLAN,
  ITEM_SEEN_TAKEN lines).
- `bot_defenderKeepSequence 1` (2: airborne frames may still aim): while `Move_SequenceActive`, the 72 u straight
  walks (item, flag, mine spot, mine stop), brace and `SetView` leave the movement's command and view alone; no shot.
- `bot_moveGuestTeammate 1`: a guest candidate whose rollout overlaps the other guest's predicted path (its committed
  rollout, else 0.5 s of its velocity) is refused (`teammate` outcome in `RunCandidate`).
- Class C (run-up yaw offset) was investigated without a source change; the agent attributes it to the known
  `MoveControl` near-tie ([record](2026-10-01-guest-divergence-movecontrol-tie.md)). The table below
  argues against that being all of it: C falls from 187 to 36 with all fixes on.

`.codex-research/integ-20261001-a/` (`run.sh`, `table.txt`, `rearm.txt`, `classes.txt`), 16 laps worlds an arm,
the setup above with `bot_moveGuestTrace 1` in every arm.

| arm | falls base / chase | missed landings (> 50 u) | captures (se) | runner deaths | races | give-ups | fires |
|---|---|---|---|---|---|---|---|
| walk (today) | 0.62 / 0.75 | 144 | 3.31 (0.44) | 36.2 | 1.19 | 6.9 | 125 |
| hop | 0.62 / 1.38 | 197 | 2.94 (0.31) | 38.1 | 1.56 | 5.2 | 134 |
| hop + keep | 0.75 / 1.44 | 184 | 4.06 (0.36) | 35.6 | 1.69 | 5.4 | 124 |
| hop + teammate | 0.56 / 1.56 | 188 | 3.50 (0.32) | 36.6 | 1.50 | 4.8 | 129 |
| hop + planner | 0.62 / 1.12 | 190 | 3.25 (0.35) | 36.9 | 0.06 | 9.6 | 130 |
| hop + all three | 0.44 / 0.56 | 77 | 4.50 (0.32) | 34.5 | 0.19 | 8.4 | 119 |
| walk + all three | 0.31 / 0.38 | 37 | 4.25 (0.28) | 35.2 | 0.06 | 11.0 | 118 |

- **Each fix removes its own class and alone changes nothing.** Keep: class A 155 → 1 landings, but B and C rise and
  misses stay 184. Teammate: B 112 → 55. Planner: races 1.56 → 0.06. Falls move only with all three: hopped
  errands 2.00 → 1.00 falls a world (below today's walked 1.37), walked errands 1.37 → 0.69. Missed landings fall
  61% (hopped) and 74% (walked).
- **The cost is captures:** every arm with keep runs 1.0-1.5 captures a world above its control (about three
  standard errors), with only 5-10% fewer shots: holding the view for a whole sequence costs more than the shots.
  `bot_defenderKeepSequence 2` (aim allowed in the air) is unmeasured.
- **Planner give-ups rise** (5-7 → 8-11): base D's only mine source, the trip mine (item 69), is 7-9 s away by
  route, sometimes plus a seen respawn wait, and `DEFENDER_ITEM_GIVEUP_MS` stays a flat 8 s.

## Keep 2 and the give-up deadline (2026-10-01, owner: "run keep 2 with the give-up scaling")

Build `integ-build-20261001-b` (665cc719: the planner's give-up limit max(8 s, 2·eta + wait)), keep at 2 (aim allowed
in the air), `.codex-research/keep2-20261001-a/` (`run.sh`, `table.txt`, `rearm.txt`), 16 laps worlds an arm:

| arm | falls base / chase | missed landings | captures (se) | runner deaths | fires | races | give-ups |
|---|---|---|---|---|---|---|---|
| walk (today) | 0.00 / 1.00 | 142 | 3.62 (0.36) | 36.6 | 125 | 1.62 | 7.4 |
| walk + all, keep 2 | 0.19 / 0.56 | 55 | 3.25 (0.42) | 37.2 | 127 | 0.06 | 11.6 |
| hop + all, keep 2 | 0.31 / 0.69 | 70 | 3.56 (0.32) | 36.9 | 130 | 0.00 | 8.9 |
| walk + teammate + planner (no keep) | 0.38 / 0.81 | 75 | 4.00 (0.47) | 35.8 | 123 | 0.06 | 10.9 |

- **Keep 2 removes keep 1's capture cost** (captures at or under the control, shots unchanged) and keeps most of the
  landing gain (missed landings 142 → 55 walked).
- **Falls:** walked + all 0.75 a world against the control's 1.00 here (1.37 in integ-20261001-a; keep 1 gave 0.69
  there); hopped + all 1.00, level with walking today. Without keep, teammate + planner do not help (1.19).
- **The scaled give-up did nothing** (11.6): the planner refreshes `rpEtaMs`/`rpWaitMs` every 500 ms evaluation, so the
  limit shrank toward 8 s while the clock ran from the choice. Build `integ-build-20261001-c` (d8a6d0a9) fixes the
  deadline at the choice and restarts it when a waited-for item comes back. 8 worlds, walked + all, keep 2: give-ups
  2.6 a world (trip mine 84 → 3 over the arm), death-to-pickup median 3.2 → 4.6 s (it waits instead of giving up),
  captures 2.38, falls 1.00 (8 worlds; inside the spread).

**Owner decision (2026-10-01):** the three are the defaults (`bot_defenderRearmPlan 1`, `bot_defenderKeepSequence 2`,
`bot_moveGuestTeammate 1`; 93e2e1b5, merged into `movement/level1-gate`, build `integ-build-20261001-d`) and errands
hop (`bot_defenderWalkErrands` stays 0; tools pass `--defender-walk` only when asked). All three are defender-only:
the runner never takes these paths (`MoveGuest`, `Bot2_DefenderIs`).

## Self-kill re-test on the new defaults (2026-10-02, owner: "sk isn't as punished, faster routing after")

Build `integ-build-20261001-d` (the merged defaults), errands hopped, `bot_defender 2`, 16 laps worlds an arm,
`.codex-research/sk-20261002-a/` (`run.sh`, analysis in the run's output):

| arm | captures (se) | deaths base / chase | self-kills base / chase | runner deaths | falls base / chase | death-to-pickup |
|---|---|---|---|---|---|---|
| spent reset off | 2.81 (0.28) | 9.2 / 9.9 | 2.4 / 5.3 | 39.8 | 0.44 / 0.38 | 3.2 s |
| spent reset on | 5.69 (0.44) | 21.2 / 24.8 | 20.5 / 23.6 | 32.6 | 0.38 / 1.06 | 3.0 s |

The faster routing did not change the verdict: the human-derived rule still doubles captures (3.81 → 6.94 on
2026-10-01, 2.81 → 5.69 now). Cost per self-kill was never the main term; frequency is: the rule fires 20-24 times a
defender in ten minutes (one every 25-30 s), against the owner's 24 over whole defense sessions. The off arm is also
the first measurement of the new defaults with errands hopped: falls 0.82 a world, captures 2.81, both the lowest of
any control this series.
