# Failure attribution: bodies don't cost the shared graph, 2026-09-28

Owner, 2026-09-28, after a quick two-runner test: the movement graph is shared
by every bot on a server, so an edge that fails twice is switched off for
all of them until the map reloads. A player's body on a landing makes every
candidate for that edge fail, so a defender standing still for two runs
could switch off good routes for the rest of a match. Direction: a body,
rocket and so on is not a wall. Fixed geometry is prepared knowledge (map
preparation); what varies during a match is bodies; movers are in between.

## Two runners first (context)

Build `defender-build-20260927-f`, Kejim, localhost laps world
(`tools/play_vs_runner.py --no-client`, a second `addrunner`), 4 min, with a
single-runner control beside it (`.codex-research/play-vs-runner-20260928T061745Z`,
`...T061857Z`).

- Both runners are recognised and run independently; per-bot state is per
  client. Each took the other's flag and both parked on their own empty stands
  for the rest of the world: 0 captures against the control's 9. That is the
  CTF rule (no capture while your own flag is away); the runner is movement
  only and never retakes it.
- Legs: outbound 16.6 / 10.5 s and return 11.5 / 15.5 s against the control's
  10.8-14.8 / 10.7-15.3; one sample each.
- Closest approach 170 u; no body contact, no charged failures.

## What changed

**Failures are charged to their cause** (`Graph_EdgeFail`, `bot2_graph.c`):

| Cause | Shared graph | The failing bot |
|---|---|---|
| transient: a player, NPC or missile | nothing charged (`edge_fail_uncharged`) | avoids the edge for 3 s (`MOVE_TRANSIENT_AVOID_MS`) |
| mover: a lift, door or platform not at rest | edge off for 5 s (`GRAPH_MOVER_OFF_MS`) | - |
| world | as before: 2 failures disable an edge no bot has flown; a validated edge is off for 30 s (`GRAPH_PROVEN_OFF_MS`) and comes back (`edge_back`) | as before |

**How the cause is found:**

- *Search failures* (planned, drop and climb edge searches): the forecast
  counts what each refused contact hit (`Bot2_ForecastContacts`). When a
  player, NPC or missile refused a rollout, the same search runs again with
  every other player, NPC and missile unlinked (`Bot2_TransientHideBegin`,
  the threat module's pattern) and the frame's trace and corridor caches
  flushed on both sides. If it then finds a candidate, the body caused the
  failure. A search that saw no transient contact is never re-run.
- *Live failures* (a missed flight, a refused or declined crossing): transient
  when the live Pmove touched a player, NPC or missile, or the bot was pushed,
  gripped or knocked back, in the last 1.5 s; or when a player, NPC or missile
  stands within 96 u of the line from the bot through the edge's takeoff to
  its landing. Mover when it touched a mover not at rest.
- Lift ride failures are mover failures.

`bot_moveFailCause` (default 1; 0 is the behaviour before this change, on the
same binary). Map preparation reads only `edge_result success=0`, so an
uncharged failure is never counted there.

**Planning around bodies** (build c, after the first laps test below): while a
bot replans, cells whose hull would overlap another live player or NPC within
512 u are not admitted (`Graph_BodiesSet`; the goal always is). If that leaves
no route the replan runs again without bodies (`body_fallback=1`), so it is
never worse than before. A plan made with bodies near carries
`bodies=N body_fallback=0|1`; other plan lines are unchanged byte for byte.

**No thrash** (build c): a timed (body) avoid never displaces an entry from a
full avoid list, and replans at once only when it is new; otherwise the
replan waits for the normal cadence.

`bot_moveFailCause` 0 turns all of it off, body planning included.

## Measured

Builds `attrib-build-20260928-b` (attribution) and `-c` (plus body planning
and the thrash fix). Kejim, production configuration.

**Gate re-taken on build b** (`.codex-research/gate-20260928-a`, the
`gate-20260926-c` harness rebound): every pinned world identical to the last
gate. Level-1 stand 14,550 / 12,100 / 13,875 / 12,300 ms, level-2 stand
9,025 / 8,925 / 8,225 / 9,775 ms, both to the millisecond; rows 1-3 met;
floors ctf4 and Dantooine 36/36, Kejim 36/36; harness identity as before; cost
p99 18.2 / 20.9 ms (last gate 26.1 / 26.8). Solo worlds only ever see world
failures: 402 of them, charged as before, with no transient, mover or timed
offs. Sources verified unchanged before and after.

**Build c solo floor** (`attrib-defender-20260928-a/floor-kejim-c`): 36/36,
none of the new paths fire (no bodies, uncharged failures, lapsed avoids,
timed offs or mover causes).

**Laps worlds with a standing defender** (`play-vs-runner --no-client`, one
runner, a still `Defender_v2` posted by the new fixture command `defenderpost`
at (2112, 3840), a corridor the route crosses on 8 of 17 legs; 150 s posted,
then kicked for 150 s; `bot_carrierMelee 0`):

| World | Switch | Defender phase: failures charged / uncharged / disabled | Stalls |
|---|---|---|---|
| b on-1 | on | 7 / 15 / 0 | 0 |
| b on-2 | on | 1 / 4,209 / 0 | **pinned 105 s** |
| b off-1 | off | 10 / - / 1 (a real miss, 1,300 u from the post) | 0 |
| b off-2 | off | 3 / - / 1 (the same) | 0 |
| c on-1..4 | on | 3, 3, 2, 5 / 1, 0, 0, 11 / 0 | 0 |

- **b on-2 was a pin, and it was the known body stall**: the route ran straight
  into the defender, the runner stopped 31 u from it, and every route the
  planner offered was refused at the body. The old code holds quietly in that
  state (the defender pilot's 9 stalls: no avoids, no charged failures); build
  b thrashed instead, filling the 32-entry avoid list and then replanning
  every frame through its last slot. Build c's two fixes are the answer: in
  four worlds, no pins and at most 11 uncharged failures.
- The two disables in the "off" arm were unrelated world misses ("fell", an
  unproven learned drop far from the post). In these unpinned laps worlds the
  old code did not happen to charge a body failure near the post, so the
  laps test shows the new behaviour working, not the old one failing. The
  mechanism is shown directly by the failures the new code declined to charge:
  every one of them was a body refusal near the post (`unsupported=2` is
  `BOT2_FORECAST_BODY`) that the old code would have charged.
- **Proven edges come back**: every "timed off" (30 s) was followed by
  `edge_back`.

**Class-pinned A/B on build c** (`attrib-defender-20260928-a`, defender 0
posted at the same point, the 12 cases that pass within 50 u of it, classes
100 and 50, switch off against on): 48/48 pass, **identical times in every
pair**, zero failures near the post in either arm. The runner passes 48-80 u
from a standing body without any failure; failures need the route to run
through it.

**Also seen, not caused by this change:** falling deaths in laps worlds. Build
c's worlds had 1, 2, 2 and 0; two at the same place and time (569, 581) at
79 s, both before any new code path had fired in those worlds (c on-3 fired
none all run). Build b's old-behaviour arm had one, and the 2026-09-26 laps
sessions on earlier builds had 2, 4, 3 and 1. Single-leg bench cases stop at
the objective and never see them. Traced the same day:
[laps falls](2026-09-28-laps-falls.md) (Speed bought mid-sequence, and a pad
edge checked from its trigger's centre).

## Two runners, open

With this change, two bots no longer take learned routes out of each other's
graph when they get in each other's way. Two bots on one team, and runners
that retake their own flag, are still untested.
