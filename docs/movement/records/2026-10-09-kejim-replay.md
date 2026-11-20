# Kejim replay and review leftovers (2026-10-09)

Workstream K of the 2026-10-09 orchestration (owner away). Branch `review/kejim-replay-20261009` (worktree
`../xenjk-wt-k`), not merged. Evidence: `.codex-research/kreplay-20261009-a/`; builds
`kreplay-build-20261009-a` (387e69e9, the server fix alone), `-b` (6d7fe6f7, + the inert review fixes and M9's cvar),
`-c` (c3930176, + M10, M13, D8). Control: N's `nighttests-20261009-a/default` arm on `nighttests-build-20261009-a`
(HEAD 49f77220), the same laps settings (be-combat C/D strings).

## Why Kejim laps worlds did not replay (HANDOFF Open 1)

**Cause: setup commands ran late after a slow map load.** `defense_laps.py --seed` queues the adds, the release and
the rate change with `botat <level> <command>`; `Bot2_AtFrame` sends each one to the command buffer (`EXEC_APPEND`) in
the game frame that reaches its level. The command buffer runs between engine frames, but `SV_Frame` runs every game
frame a wall-clock gap owes in one loop. The first engine frame after `devmap` owes the whole map load (the `Hitch
warning`), so the game clock runs from about 0.5 s to 0.5 s + the load in one batch. Kejim's load (graph cache 14 MB)
is 1.1-1.3 s alone and up to 1.9 s with four worlds starting at once; Dantooine's is 0.5-0.6 s. A load over about
1.5 s carries the batch past 2,000 ms, the first add's due level, and `addrunner` runs at the batch's end instead:

- `poolend-outcome-20261007-a/kej/off-3` against `offrep-3` (the HANDOFF's "seed 3 off twice"): hitch 1,862 against
  1,226 ms; the runner's telemetry file is named for level 2350 against 2000 and its first plan is at 2375 against
  2025 (`consolediff.py`: the first differing console line). Everything after differs; the game logs part at 0:15-0:16.
- `join-replay-20261007-a/kej/on-3` (the "usual cross-build divergence" of 2026-10-07): hitch 1,788 ms, addrunner at 2275.
- The 2026-10-03 clue (records/2026-10-03-hud-knowledge.md: a Kejim world alone takes its first flag at 0:12-0:14,
  four at a time at 0:15) is the same dependence on load.

**Retained worlds agree** (`survey.py`, `survey.txt`: every seeded laps world of 2026-10-03..08 grouped by build, map,
seed and settings, game logs compared over the first 300 s; on time = addrunner ran at 2000). Since 2026-10-05 every
pair of worlds whose setup ran on time replays (92 Dantooine pairs, 75 Kejim) and every pair with a late addrunner
differs (16 Dantooine, 5 Kejim; the Dantooine ones are fresh-build first-launch stalls, the same mechanism). The 13
on-time pairs that differ are all 2026-10-03/04, before the Ghoul2 clock fix and RNG bisect of those days. No second
cause is visible. Review area 8 already counted late commands (`setup_late_commands`) and its worlds had none, which
is why they replayed.

**Fix (387e69e9, server):** `SV_Frame`'s catch-up loop stops after a game frame that left text in the command buffer
(`Cbuf_Pending`, cmd.cpp); the rest of the catch-up runs in the next engine frame, after the buffer. A command a game
frame queues then runs before the next game frame, as it does at one frame a loop. Nothing changes when no command
is queued (every frame of an accelerated laps world after setup), so it should replay worlds whose setup was on time;
it also takes the burst out of the fresh-build first-launch stall (memory note: every add in one burst at one level).
Game code is unchanged.

**Verified** (`run1.sh`, `identity-verify.txt`; Kejim seeds 3-6, 120 game s, the four worlds of a batch pinned to
one CPU to slow the load; the batch's last world, started while the other three ran, hitched 3-4 s):

| World | Build | Load hitch | Setup commands late | Against the unstressed world (game log, decisions over ~130 s) |
| --- | --- | --- | --- | --- |
| `ctl-stress/kej-3..5` | control | 1.10-1.40 s | 0 | identical to `nighttests default/kej-3..5` |
| `ctl-stress/kej-6` | control | 3.06 s | 4 (max 1,575 ms) | differs at the second line (the runner joins at 0:03) |
| `fix-stress/kej-3..6` | fix (a) | 1.09-3.82 s | 0 | identical to `fix-normal/kej-3..6` (31,500-33,200 decision lines each) |
| `fix-normal/kej-3..6` | fix (a) | 1.13-1.15 s | 0 | identical to `nighttests default/kej-3..6` (the control's 600 s worlds) |

So the late setup reproduces on the control on demand, the fix holds a 3.8 s load to the same world, and the fix is
inert when the load is short (its worlds replay the control build's). The 2026-10-06 memory note that each build
gives different seeded worlds "even when the change is an inert diagnostic" is, at least for Kejim, mostly this
(`join-replay-20261007-a`'s Kejim pair had a late add); review area 8 replayed all on-time worlds across builds.
`laps_identity.py` here is the worktree's, which covers every decision stream (T13 below).

## Review leftovers (docs/review/FINDINGS.md)

### Inert fixes (build b, 6d7fe6f7): identity

Build `-b` (the server fix, D7, D9, D10, D11 and M9's cvar at its default 0) replays the control: Dantooine 1-4 over
600 s, game log and every decision line (140,000-147,000 a world), `identity-inert-dan.txt`. Kejim 1-4 on build b
(run3, 2026-10-09 05:09, `kreplay-20261009-a/inert/kej-1..4`, all with 0 late setup commands) also replays
`nighttests-20261009-a/default/kej-1..4`: game log and every decision line identical in all four (149,000-158,000
lines a world). Decided: build b is inert for Dantooine and Kejim.

- **D7** `bot_defenderChargeMs` held every alt-fire: the hold now applies only to alt-fires that charge in
  `PM_Weapon` (Bryar pistol and old pistol, DEMP2, thermal, the rocket lock); any other alt fires through the normal
  gates. Default 0 and the pistol keeps its own `bot_defenderPistolChargeMs`, so laps worlds are unchanged.
- **D9** perception history 64 -> 256 frames: `Perceived(react + 250)` needs 1,250 ms, 50 frames at 25 ms (laps
  worlds, sv_fps 40) but 156 at 8 ms. Newest-first lookups return the same frame whenever 64 covered it.
- **D10** `Mate` skips a slot whose client number now names another bot (`DefenderSlot(d->client) != s`);
  `Bot2_DefenderHops` reads a slot's chase/block/return flags only for the client armed in it. Left open: a defender
  dead at a release keeps the previous release's state. Clearing it would leave it idle (unarmed) until the next
  release and arming it unplaced is untested; that is a fixture choice, no laps world reaches it (one release, all
  alive).
- **D11** a missile slot freed after its missile was first seen (`freetime > firstSeen`) holds a new missile; G_Spawn
  reuses slots at once in a level's first 2 s, or when slots run out. Laps worlds fire nothing in the first 2 s.

### M9 (owner: measure): a climb ahead takes the approach's frames

Confirmed in the code: within `MOVE_EDGE_APPROACH` (400 u) of a climb edge's takeoff, `PlannedEligible` hands every
grounded frame to `ClimbSearch`, which returns at once outside `MOVE_CLIMB_REACH` (96 u) without setting the planned
retry, so the gap and straight searches never run on the last 300 u before a climb. `bot_moveClimbApproach 1`
(default 0) returns the frame to them until the bot is within 96 u. Kejim has no climb edges (no climb plan in
the control's Kejim worlds), so only Dantooine can show it. **Measured** (run3, 2026-10-09 05:11-05:19, build b with `bot_moveClimbApproach=1`, against
`nighttests-20261009-a/default`, 600 game s a world):

- Kejim 1-4: identical to the control in the game log and every decision line, as expected (no climb edges).
  Decided: no effect.
- Dantooine 1-8 (`laps_compare.py`, per 600 s, arm minus control, +-95% half-width): team caps 5.74 -> 4.56
  (-1.18 +- 0.43), runner caps 4.32 -> 2.96 (-1.36 +- 0.45, past the t cut-off), all caps 11.59 -> 9.12 (-2.48 +- 0.84,
  past the cut-off); runner falls 1.48 -> 1.11 (-0.37 +- 0.68), runners' weapon deaths +2.31 +- 1.85. `laps_compare`
  verdict: **undecided** (primary team caps).
- `paired.py` (8 seeds): climb commits 49.3 -> 44.5 (-4.8 +- 1.6), failed climb searches -140 +- 78, hops -0.9 +- 12.5
  (no change), chained hops -2.1 +- 8.2, frames over 25 ms -147 +- 99, p99 work -1.75 +- 1.0 ms.

Reading: the approach change does not give the runner more hops; fewer climbs are committed (-10%) and the
secondary metrics point to fewer runner captures, with the primary metric undecided at n=8. No evidence for turning
`bot_moveClimbApproach` on; if anything the sign is against it. Left default 0. Not extended (no worlds added).

### Decision-changing fixes (build c, c3930176): M10, M13, D8

- **M10** `WalkStep`: a refused corner chord now leaves the path's own stop (the edge takeoff's braking distance) for
  the cell targets it falls back to, instead of the corner's (usually none).
- **M13** a drop deferral still pending at or after its take frame (the bot left the edge, so `DropDefer` was not
  called to take it) is ended in `SearchBudgetStep`, as `DropDefer` or `WalkDefer` would end it when next called; it
  had blocked the early pre-search until then.
- **D8** the armed top-up's existence check returns at once for an unclaimed item (as before) and keeps its verdict on
  the teammate-claimed item, the one that needs a route query, for `RP_EVAL_MS` (500 ms), as the full evaluation is.
  `Graph_Generation` is still called at the same point.

**Measured** (run3, 2026-10-09 05:19-05:34, build `-c`, 8 Dantooine + 16 Kejim against
`nighttests-20261009-a/default`, `fix/`; no world had late setup commands):

- Kejim 1-16: game log identical to the control in all 16; decision lines identical in 8 of 16 (kej-1, 3, 4, 5, 9, 10,
  12, 15) and diverging late in the other 8 (lines 12,334 to 137,727 of ~150,000; kej-11 earliest) with no change in
  any game-log event. `laps_compare`: team caps 3.73 -> 3.79 (+0.06 +- 0.37), runner falls 3.63 -> 3.63 (0.00 +- 0.58),
  runners' weapon deaths +0.04 +- 1.12, runners falls +0.00 +- 0.97; `paired.py`: hops 597.4 -> 597.8 (+0.4 +- 0.3),
  frames over 25 ms +0.25 +- 0.48. Decided: no measurable effect on Kejim (captures, falls, weapon deaths, hops and
  work all within noise; at most 8 worlds changed any decision).
- Dantooine 1-8: no world replays the control's decisions (first difference at lines 8,178-98,405); game log
  differs in dan-1..4. Build b inert replays the control on Dantooine, so these differences are the three fixes
  (Dantooine's climbs and crowds exercise them), not noise. `laps_compare`: team caps 5.74 -> 5.30 (-0.43 +- 0.41),
  runner caps -0.37 +- 0.50, runner falls 1.48 -> 1.48 (0.00 +- 0.67), runners' weapon deaths -0.24 +- 1.37,
  verdict **undecided** (team caps). `paired.py`: hops 503.0 -> 494.1 (-8.9 +- 5.1), chained -2.9 +- 5.0, frames
  over 25 ms -48 +- 38, p99 -0.6 +- 0.8 ms, climb commits -2.6 +- 1.2.

Reading: M10/M13/D8 are safe on Kejim and show no effect that survives n=8 on Dantooine (undecided; captures
point slightly down, hops slightly down, work slightly down, all inside or near the noise). The arm cannot separate
the three fixes from one another; they stay in the branch as correctness fixes, not as measured gains. Not extended.

### M11 (owner decision): the keep-out lattice in crowds

`PlanBodies` adds the defender's flag keep-out as nine bodies after the live bodies, only if they fit in
`GRAPH_MAX_BODIES` (16), and not at all with `bot_moveFailCause 0`. Measured fact: a laps world has eight players and
no NPCs, so a replan sees at most seven other bodies and the lattice always fits (7 + 9 = 16); the drop needs nine or
more players within 512 u (a real server, 10+ players). The defender's own goal clamp (`FlagKeepOut`) holds either
way; what is lost in a crowd is the route around the flag. Options (none measured; laps worlds cannot show them):

1. **Leave it** (document as latent). No cost; on a large server a crowded defender may route over the enemy flag.
2. **Lattice first:** reserve the nine lattice bodies, live bodies take the remaining seven nearest (sorted by
   distance). Exact in laps worlds; in a crowd the farthest live bodies are dropped instead of the keep-out.
3. **Raise `GRAPH_MAX_BODIES` to 25** (16 live + 9): exact in laps worlds; a crowded replan's body pass grows with
   the bodies (per-cell checks), unmeasured.
4. **Independent of `bot_moveFailCause`** (alone or with 2/3): the keep-out routing stays when failure attribution
   is turned off. Only matters if that cvar is ever set to 0.

### Tools (T11-T15)

- **T11** was already fixed (area 10).
- **T12** `laps_compare.py`: new `runners_self_kills` (MOD_SUICIDE of both runners: CAPKILL and stranded kills);
  `defender_deaths` from the game log over the base and chase D of both teams (two-sided: a team, the mean), self-kills
  out (the summary held only the red ones); `defender_enemy_flag` is the red base and chase D's blue-flag touches
  only (it counted Runner2/Camper2 pickups and the blue defenders' returns). `laps_scenarios.py` counts as
  `laps_compare` does. Retained summaries need no rewrite (read from the game log). Test added.
- **T13** `laps_identity.py` also compares `[REROUTE]`, `[TAPE] select`, `[PLANGUIDE]`, `[PATHTABLE]`, `[SHADOW]`,
  `[FORECAST]`, and keeps `hits=`/`misses=` on `[DEFENDER]` lines (game counts there). Its docstring no longer says
  Kejim does not replay.
- **T14** `route_asset_build`, `movement_tape_library`, `movement_graph_merge`, `movement_graph_enrich` refuse an
  existing output without `--force` (`movement_explore` writes its merge into a new folder, unaffected).
- **T15** no rcon password in `movement_cost_runner`'s manifest; `test_strafe_fixture_build` removes its stub build
  folders (one was left per test and run; the 756 already in `unit-artifacts` are left for the shed);
  `install_kejim_runner.ps1` refuses a DLL whose SHA-256 is not the build manifest's. Kept: `game_seconds` includes
  the 8-9 s setup in both arms alike; redefining it would bias comparisons against retained summaries by ~1.4%.
- Suite: 491 tests from the worktree root, all pass but `test_strafe_map_preparation`'s, which needs the game assets
  the worktree does not have (`game_directory`).

### Observed on the way: failed climb searches

The control's Dantooine worlds run about 1,000 climb searches a world that admit nothing (`dan-1`: 1,006, 772 with
every candidate short, 999 charged to the world, 13.3 s of rollouts), mostly the campers' and defenders' 256 u wall
climbs (939 of 1,006), at Force 50 or less (force=50: 420, 30: 165, under 10: about 300). Not a review finding; a candidate for the frame-cost and
graph-charge work (a climb that every candidate falls short of is searched and charged every 250 ms; whether Force decides it is not checked).

## Left

Later (2026-10-09, [flag-avoid record](2026-10-09-flag-avoid.md)): M9 removed (owner), M11 built as a forbidden volume, D10's dead-at-release case fixed.

Nothing pending from this record. Owner decisions: M11 (above); whether M9's cvar is worth keeping at all (undecided,
sign against); merge of branch review/kejim-replay-20261009 into movement/level1-gate (not done, not pushed).
