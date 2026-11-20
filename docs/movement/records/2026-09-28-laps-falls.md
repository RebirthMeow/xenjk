# Runner falls in laps worlds, 2026-09-28

The movement runner falls into Kejim's pit (the fall-to-blackness
`trigger_hurt` below z 704) in long laps worlds (`tools/play_vs_runner.py
--no-client`: one runner capturing again and again), which single-leg bench
cases never show. Found while testing failure attribution
([record](2026-09-28-failure-attribution.md)). Analysis only: no source
change, no installed-game change; retained worlds were read in place.

## The falls

17 `MOD_FALLING` deaths in the eight laps sessions named for this study: four
on 2026-09-28 (builds `attrib-build-20260928-b` and `-c`, 6 falls) and four on
2026-09-26 (`strafeskill-`, `probe-`, `threat-build-20260926-a`, `-d`; 11).
One of the 11 was a human's kill ("Padawan", `miss reason=knocked`), which
leaves 16 falls of the runner's own movement. Each was traced from the play
log (4 Hz) and the `[MOVE]` events: the pit sample, the last launch above
z 1000 before it, the commit that launch belonged to, and the Speed events
between (`tools/movement_laps_falls.py falls`).

| World | Died (s) | Flight | Cause |
|---|---|---|---|
| 0928 T074130Z | 79.4 | pad at (160, 192), native pad edge to (462, 493, 1810) | **B** pad from the trigger's edge, no Speed |
| 0928 T074150Z | 79.4 | the same flight, frame for frame | **B** |
| 0928 T074110Z | 86.8 | straight id 56, commit Force 49, Speed bought 50 ms later | **A** Speed bought mid-sequence |
| 0928 T074130Z | 195.3 | state id 127, commit Force 49, Speed bought on the launch frame | **A** |
| 0928 T074150Z | 106.7 | straight id 62, Speed bought 50 ms after commit, fatal hop 1 | **A** |
| 0928 T065717Z | 266.5 | straight id 145, Speed bought 325 ms after commit | **A** |
| 0926 T102637Z | 83.8 | state id 32, Speed bought 25 ms after commit, fatal hop 1 | **A** |
| 0926 T102637Z | 144.1 | knocked by the human player | pushed (not counted) |
| 0926 T105031Z | 32.2 | straight id 13, Speed expiring 675 ms after the fatal launch | C |
| 0926 T111504Z | 27.4 | straight id 9, Speed expiring 50 ms after the fatal launch | C |
| 0926 T105031Z | 54.2, 194.1, 237.0 | straight, no Speed change in flight | C |
| 0926 T111504Z | 139.7, 161.8 | straight, no Speed change in flight | C |
| 0926 T113626Z | 69.7 | straight, no Speed change in flight | C |

**Every fall on the current builds is A or B.** No fall had an objective or
owner change in flight: the executor (or the jumppad state, for B) owned every
fatal flight from launch to pit.

## A: Speed bought in the middle of a committed sequence (5 falls)

`SpeedEdge` (`bot2_move.c`) buys Force Speed on any grounded frame the executor
issues (`walked`) when the pool affords it and the path left is long enough.
Nothing stops it while a committed hop sequence is still ahead: a run-up, a
launch frame, or a landing between hops. The sequence's hops were rolled out
and admitted at the old wishspeed, and are then flown at Speed's (air strafe
1.7 times as strong), so they carry on past their landing floor. The purchase
revokes the corridor permit and resets the parity harness, which rebuilds its
horizon *with* the purchase, so parity never compares the flight against the
rollout that admitted it.

- 190 s in T074130Z: state plan 127 committed at 190,050 ms, Force 49, predicted
  hop 0 to land on cell 4228 at 398 u/s after 37 frames. Force reached 50 and
  Speed was bought on the launch frame (190,150); the live hop reached 536 u/s,
  was still at z 1829 over the ledge, and landed in the pit 1,080 u from the
  predicted cell.
- Measured over the 09-28 laps worlds: committed landings after a mid-sequence
  purchase miss their predicted cell by a median 126 u, 9 of 18 by more than
  128 u (including the pit landings); landings without one miss by a median
  13 u, none of 2,406 by more than 128 u.
- Why only laps: in bench legs the Force is set at the start (0 or 100), so
  purchases fall at the leg start or on a sequence's last landing: the
  2026-09-28 gate's Kejim worlds have 2,893 committed landings and none after
  a mid-sequence purchase. In laps the pool regenerates across the cost while
  sequences are running: 8-11 of every 9-13 purchases in a laps world are
  mid-sequence. They kill only when the hops left end near a ledge over the pit.

## B: a pad edge checked from the trigger's centre (2 falls)

Pad discovery (`Pads`, `bot2_graph.c`) flies each candidate arc from the cell
nearest the trigger's centre, (160, 192) for this pad, and keeps a landing that
the steered arc reaches at run speed and at Speed. The runner walks onto the
pad from its west edge, 60-100 u farther from the landing, carrying the flag
with Force 6 and no Speed. The jumppad state locks the native pad edge
(462, 493, 1810) and steers at it with the discovery's law, but at the 250 u/s
air cap the arc is over the landing only at z 1528, below the ledge. It then
turns back toward the landing (yaw +40 to -140 degrees with forward held),
which only brakes it, and drops into the pit.

**Reproduced deterministically** by a pinned bench case,
`tools/fixtures/movement/kejim_falls/cases.json` (`fallB_pad_carrier_force6`:
blue carrier from rest at (-208, 228, 1368), Force 6): death at 7,400 ms at
classes 50, 100 and 150, the same locked target and pit spot (572, 583) as the
laps worlds (`.codex-research/laps-falls-20260928-a`). The same start with
Force 50, 75 or 100 buys Speed and passes at 11,325 ms; with Force 25 it dies
at 7,400 ms. The edge is safe only with Speed or from the centre.

## C: 2026-09-26 divergence, not attributable (8 falls)

On the superseded 09-26 builds, straight plans' fatal hops flew 400-2,300 u
off their predicted landing with no Speed purchase (two had Speed expiring
50 and 675 ms after the launch, which the forecast models). Those sessions ran
server-wide `pmove_fixed 1` (before the per-bot step), a lowered strafe skill
(81) on some, and two had a human playing; they ran without the parity
harness, so where the flight left its rollout cannot be recovered from the
retained telemetry. None of the 09-28 falls is of this kind.

## Proposed fixes

**A.** `SpeedEdge` does not buy while a committed sequence is ahead: return
when `m->plan.count && m->executor.stage < Onward(&m->plan)`, the same
condition that already keeps `Replan` from running mid-sequence. The purchase
then comes on the next frame with no sequence ahead, and the next search rolls
out with Speed active (the forecast already applies an active Speed). The
delay is at most the rest of one sequence (about 1 s). The alternative, a
purchase that drops the committed remainder and re-searches, keeps the timing
but needs care on a launch frame. Measure on the level-2 gate rows, since
Speed timing is what level 2 buys.

**B.** Either check a pad edge from the trigger's standing entries rather than
its centre (discovery), or choose the pad edge at the throw from the live
entry state: `PadFlightEdge` already picks among the edges leaving the trigger
and could keep only those a `PadArc` from the live position and velocity, at
the live wishspeed, brings down on the landing. The first changes the graph's
identity and the prepared seeds; the second does not. With no landing
reachable, the route should not step onto the pad without Speed.

## Fixes implemented (owner: "do those fixes", same day)

Build `falls-build-20260928-j`, on top of the uncommitted defender and
failure-attribution work.

**A.** `SpeedEdge` returns while `m->plan.count && m->executor.stage <
Onward(&m->plan)`. A purchase on a sequence's last landing is still allowed
(the stage is then `Onward`), so the bench's leg-start and last-landing
purchases are unchanged.

**B**, three parts:

1. `Graph_PadReachable` (`bot2_graph.c`): the throw from a given entry point,
   steered at a pad edge's landing at a given wishspeed, with discovery's own
   landing checks. It finds the push target with `G_Find`, because
   `G_PickTarget` draws on `rand()` and would shift pinned worlds.
2. `PadFlightEdge` (`bot2_move.c`) picks the landing at the throw: the path's
   pad edge only if it is reachable from where the bot entered, at the
   wishspeed it has (run speed, or Speed's); otherwise the reachable edges
   leaving the trigger, ranked as before; with none reachable, the old choice,
   logged `pad unreachable`.
3. `PadAhead`: as the bot comes within 192 u of the path's next pad takeoff,
   it predicts the entry point. A landing counts as reachable only from that
   entry and from 12 and 24 u further in. With none reachable, this bot avoids
   the pad edge and replans (`pad avoid`). Each judgement is logged once per
   route (`pad ahead`). `bot_movePadEntry 0` turns this part off.

**A bug found on the way:** `PadArc` traced with `ENTITYNUM_NONE` as the
entity to ignore, which was right at map load (discovery, no players). From a
live entry the arc starts inside the bot's own hull, so every trace was
`startsolid` and every landing looked unreachable. The live calls now pass
the bot's number; discovery still passes `ENTITYNUM_NONE`, so the graph is
unchanged.

### Measured on build j

- **B**: `fallB_pad_carrier_force6` **captures at classes 50, 100 and 150**
  (14,850 / 14,475 / 14,575 ms; control died at 7,400 ms in all three). At the
  throw the planned (462, 493) landing is judged unreachable from the west
  entry, and the pad's reachable north-west landing (-142, 493) is flown
  instead. Force 25 now captures (14,500 ms); Force 50, 75 and 100 unchanged at
  11,325 ms.
- **A**: four five-minute laps worlds (`play-vs-runner-20260928T082919Z`,
  `T082934Z`, `T082949Z`, `T083005Z`): **no falls**, and **no committed
  landing after a mid-sequence purchase**. The ordinary landing error is
  unchanged (median 12-13 u, none over 128 u). The build-b/c controls, by
  contrast, had 12 such landings, 7 of them over 128 u. Lap pace is unchanged:
  11 captures in each 301 s world, median lap 24.7-26.4 s against the solo
  control's 26.0 s.
- Kejim floor 36/36 (`laps-falls-20260928-j/floor-kejim`).
- **Gate re-taken on build j** (`.codex-research/gate-20260928-b`, sources
  verified before and after): every world passes with no deaths, 274 of 274.
  Level-1 stand 14,550 / 12,100 / 13,875 / 12,300 ms and level-2 stand
  9,025 / 8,925 / 8,225 / 9,775 ms, both identical to the millisecond with the
  build-b gate. Rows 1-3 met; floors ctf4 and Dantooine 36/36; cost p99
  16.8 / 17.1 ms (build b 18.2 / 20.9). Two row-1 identity lines read
  `identical: False` (diag-c75, diag-c125). The only differing field is the
  search's `traces=` count, which depends on trace-cache hits with the harness
  on and is missing from `analyse.py`'s `VOLATILE` list. With it ignored,
  decisions are identical at all four classes (10 / 9 / 10 / 9 commits).

## Checks for the fixes

- B: `fallB_pad_carrier_force6` at classes 50, 100 and 150 must capture with
  no death; the Force 50-100 variants unchanged.
- A: `tools/movement_laps_falls.py landings` over new laps worlds: no committed
  landing after a mid-sequence purchase (the count goes to zero), and the
  "not bought" distribution unchanged (median 13 u, none over 128 u). A laps
  world is 5 minutes; four run in parallel.
- Both: the gates, level-2 stand times in particular.
