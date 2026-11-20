# Strafing chaser, 2026-09-28

Owner, 2026-09-28: go ahead with the strafing chaser proposed after round 3 of
the [one-defender pilot](2026-09-27-one-defender.md). The walking chaser
(250 u/s against a carrier at 430-570) was a weak probe. The work order was
to let the declared defender run the movement searches, keep it from writing
into the shared graph, measure its per-frame cost, and rerun the return and
pickup cases against it.

Build `chaser-build-20260928-c`. Worlds and the analysis are in
`.codex-research/chaser-20260928-a/` (`campaign.sh`, `analyse.py`).

## What changed

- **The guest (`MoveGuest`, `bot2_move.c`).** With `bot_defenderStrafe 1`
  the declared defender passes `MoveSearchGates` and runs every search the
  runner runs: straight hops, gap, drop, climb and planned edges, pads.
  The graph is shared, so the guest writes nothing into it:
  - no learned or merged strafe edge and no gap hop;
  - no validation and no measured cost;
  - no failure charge. A failure it sees goes only on its own avoid list:
    3 s when transient, for its objective otherwise.

  The parity harness, the decision tape and library, exploration, the
  forced-candidate override and the fixture's plan directives stay the
  runner's:
  - the guest never builds a horizon;
  - it never reads or ends the runner's horizon, including the disarm each
    search did at its start (`SearchDisarm`);
  - it never compares frames against it.
- **The chaser's goal (`bot2_defender.c`).** Every goal change more than
  64 u is a movement reset that drops a committed hop, and a carrier moves
  64 u in about three frames. So the strafing chaser holds its goal, the
  carrier as last seen, until a new sighting is `bot_defenderRetarget`
  (default 256) from it or the goal is within 96 u. The walking chaser still
  takes every sighting. The loadout is unchanged: levitation 3, push and
  pull 3, no Speed.
- **The frame meter (`bot_moveFrameMeter`, `bot2_movecost.c`).** It times
  each bot's command dispatch and its whole pre-Pmove boundary, per client
  per game frame, in ordinary bench worlds. The results go into a 0.25 ms
  histogram per client and one for each frame's total over every bot. A
  cumulative `[FRAMECOST]` line prints once a second from the fixture's
  release. The meter is wall-clock, so timing worlds run alone.
- **Bench options:** `movement_bench.py --defender-strafe` and
  `--frame-meter` (`frame_cost` in each `result.json`).

Fixed before the campaign, found in smoke worlds on builds `a` and `b`:

- The guest's frames reached the harness's comparison blocks. Its unowned
  frames logged `live_declined` and `boundary_time` against the runner's
  armed horizon. The bench's runner-only event filter hid this, which is why
  the guest's `[MOVE]` lines were read directly.
- The disarm at the start of each search ended the runner's horizon on the
  guest's searches too.

Suspected and not a defect: a non-gated defender's boundary disarms the
harness (`if(parity.armed)ParityDisarm()`), but in practice it does not cut
the runner's coverage. The retained round-2/3 arms and this campaign's
walking arms compare as many frames (median 1,080-1,230) and horizons as
their controls. The change made for it was reverted.

## Arms

Kejim, classes 50 and 150. Every arm has `--edges --linger 4
--set bot_navmeshOff=1`, the prepared seed, and melee range 96 in the
defended arms.

- **Returns:** `--filter _ret_`, 18 cases a class.
- **Pickups:** `--skip-twins`, 24 cases a class: 18 outbound legs and
  Kejim's 6 return stands.

The arms are a control, the walking chaser (modes 1 and 2) and the strafing
chaser (modes 1 and 2); there is no mode-2 walking arm on pickups. The
outcome arms have `--parity 256` and run four worlds at a time. The timing
arms have `--parity 0` (production search) and run one world at a time, on
the return cases at class 50. The controls were rerun on this build rather
than reused: the retained round-3 arms (`defender-build-20260927-e`) predate
failure attribution and the laps-falls fixes.

## Results

**Every world of every arm passed:** 372 outcome worlds and 54 timing
worlds. Deaths are read from the defender's throw outcomes, not from pass
counts, because a pickup leg passes at the pickup.

| Arm | Worlds | Carrier killed | Throws at a carrier | Deaths after the objective, not carrying | Paired time vs control |
|---|---|---|---|---|---|
| returns, walk 1 | 36 | 0 | 3 | 4 | identical 36/36 |
| returns, walk 2 | 36 | 0 | 0 | 2 | identical 36/36 |
| returns, strafe 1 | 36 | 0 | 4 | 1 | 35 identical, 1 slower (+1.1 s) |
| returns, strafe 2 | 36 | 0 | 0 | 6 | identical 36/36 |
| pickups, walk 1 | 48 | 2 | 2 | 0 | 12 slower, +12.6 s |
| pickups, strafe 1 | 48 | 2 | 8 | 1 | 13 slower, +13.7 s |
| pickups, strafe 2 | 48 | 2 | 2 | 3 | 12 slower, +3.5 s |

- **No carrier died on any return.**
- **The pickup kills are one situation in all three arms.**
  - Class 150, `red_out_L2_respawn5` and `L3_respawn5`: a pull at
    470-610 u, 0.9-1.1 s after the pickup, as the fast carrier leaves the
    blue stand. It lands in the pit 1,750 ms later.
  - It comes before the chaser has moved from its post, and round 2's
    holding defender (`md1`, `md2`) killed the same carrier in the same
    worlds. It is the stand exit, not the chaser.
- **Deaths after the objective, not carrying:** after a capture, the linger
  sends the runner out again. It passes the defender's base and is pushed
  at 70-140 u (mode 2: zero escapes). These are not carrier kills.
- **Pickup slowdowns** are the defender's body on the approach to the flag,
  as in round 2: melee through at about +1-3 s a world.

**The chaser strafes but does not close.**

| Chaser | Speed while the runner carries, median / p90 | Airborne | Hop launches | Carry time within 1,000 u, median | Closest approach, median |
|---|---|---|---|---|---|
| walking (returns) | 250 / 252 u/s | 2-3% | 0 | 0-15% | 248-1,382 u |
| strafing (returns) | 320 / 523 u/s | 64-66% | 426-427 an arm | 0% | 1,303-1,352 u |
| strafing (pickups) | 286-287 / 503-507 u/s | 54-56% | 498-518 an arm | 28% | 82-86 u |

It flies about 30% faster than the walk. Without Speed it is still slower
than the carrier (430-570 u/s median), and its goal is where the carrier was.
On the returns it falls out of range as the walker did. On pickups it starts
beside the flag, so proximity is the post's, not the chase's.

**Isolation held in every world.**
- Chaser learn events: 0. Chaser parity events: 0.
- Every `[GRAPH] edge_added` after the start is the runner's. Of 14,047,
  13,994 match a runner learn event by index. The other 53 are all `hop`
  edges printed right after the runner's own `onward complete`, its gap
  plans. The walking arms, where the defender cannot search, have them too.
- Runner parity mismatches in the strafing arms: 15 of 48 (strafe 1) and 6
  of 20 (strafe 2) fall on a defender HIT frame. One checked mismatch
  (`red_ret_L3_respawn4`, after_state, 10.5 u) had the chaser about 50 u
  away, a body contact.
- The walking arms' 33-40 are forecast refusals, not mismatches.

**Cost.** Return cases, class 50, 18 worlds an arm, about 24,500 frames each.
Microseconds per game frame from the frame meter:

| Arm | Client | Mean | World p99 median (max) | Worst frame | Frames over 25 ms (per 800) |
|---|---|---|---|---|---|
| control | runner | 551 | 7,750 (10,250) | 29,851 | 4 (0.13) |
| walking chaser | runner | 519 | 7,625 (9,000) | 29,125 | 6 (0.20) |
| | chaser | 75 | 750 (3,500) | 13,781 | 0 |
| | frame total | 595 | 8,000 (9,000) | 29,141 | 6 (0.20) |
| strafing chaser | runner | 555 | 8,375 (10,000) | 33,045 | 9 (0.29) |
| | chaser | 325 | 8,250 (11,500) | 37,655 | 16 (0.52) |
| | frame total | 880 | 12,375 (14,750) | 60,641 | 32 (1.04) |

- A searching chaser costs about 0.33 ms a frame on average.
- The two-bot frame total keeps its p99 inside the 25 ms frame
  (12.4 ms against the runner alone's 7.75).
- Frames over 25 ms rise eightfold, to about one in 800. These are frames
  where both bots' searches coincide: the multi-bot cost the 2026-09-22
  [row 5 record](2026-09-22-row5-planner.md) measured and deferred, now for
  two bots.

## Reading

- The return route stays safe against a chaser that strafes: no carrier
  died, as against the walker.
- The one lethal situation is still the carrier leaving the stand after a
  fast pickup, and the chaser has nothing to do with it.
- A chaser that threatens the return would need Speed (a Force bidder of
  its own) or an intercept goal (the carrier's own base, which a human
  chaser knows) rather than the last-seen point. Neither is in scope here.
- The guest seam and the frame meter are reusable: any second searching bot
  (a teammate, the chase and base milestones) runs through `MoveGuest`'s
  rules until the graph gets per-bot writers.

## With Speed (owner, same day)

Owner: give the chaser Speed. Build `chaser-build-20260928-e`; worlds and
the analysis are in `.codex-research/chaser-20260928-b/`.

**What changed.**

- `bot_defenderSpeed 1` (`--defender-speed`) gives the defender Speed 3 at
  its release, in the same declared fixture setup that fills its Force, as
  the corner start gives the runner its Speed. A personality `forceinfo`
  with Speed staged as the bot's file was tried first and did not take:
  the defender still spawned with Speed 0, cause not traced.
- The movement code needed nothing new: the spend policy buys Speed for any
  bot that knows it. The guest buys it as the runner does, from the pool
  its throws use (Speed 50, a push or pull 20).
- A lethal throw the chaser finds but cannot afford now prints
  `[DEFENDER] STARVED`, at most every 500 ms.

**Identity.** The return control and the no-Speed strafing chaser at class
50 on build `e` are identical to build `c`'s in all 18 worlds each, so
build `c`'s controls are reused.

**Results.** All 168 outcome worlds passed.

| Arm | Worlds | Carrier killed | Throws at a carrier | Chaser Speed purchases / chases | STARVED |
|---|---|---|---|---|---|
| returns, Speed 1 | 36 | 0 | 6 | 47 / 64 | 0 |
| returns, Speed 2 | 36 | 0 | 1 | 48 / 64 | 0 |
| pickups, Speed 1 | 48 | 2 | 12 | 39 / 59 | 0 |
| pickups, Speed 2 | 48 | 2 | 2 | 40 / 59 | 0 |

| Chaser, runner carrying | Speed median / p90 | Carry time within 1,000 u, median | Closest approach, median |
|---|---|---|---|
| strafing, no Speed (returns) | 320 / 523 u/s | 0% | 1,303-1,352 u |
| strafing with Speed (returns) | 431-436 / 701-702 u/s | 0-1% | 1,145-1,170 u |
| strafing with Speed (pickups) | 393 / 638-644 u/s | 39-40% | 84 u |

- Speed makes the chaser a third faster. It is about as fast as the
  carrier's median (430-570 u/s) and faster at p90. Then on one pickup
  world it followed the carrier all the way to the runner's own stand.
- On the returns it still starts too far behind to catch up. Its median
  closest approach falls only from about 1,300 to 1,150 u, and no carrier
  died.
- The pickup kills are the same class-150 respawn-5 stand-exit pull, in the
  same worlds, as without Speed.
- The pool never starved a lethal throw. The chaser bought Speed in about
  three chases of four, and throws are rare enough that 50 left over
  always covered them.
- Isolation held: no chaser learn or parity events. Runner parity events rose
  on pickups (166 and 225), but they are almost all forecast refusals
  (`unsupported=2`, a live player overlapping the rollout) in three
  worlds. They come after the capture, with the chaser standing at the
  runner's stand. They are harness coverage, not wrong predictions.
  Mismatches proper (command, state, declined) are as without Speed.

**Cost.** Return cases, class 50. The three arms were rerun back to back,
one world at a time:

| Arm | Chaser mean | Frame total mean | Frame total world p99 median (max) | Frames over 25 ms (per 800) |
|---|---|---|---|---|
| control | - | 533 us | 7,750 (8,750) us | 3 (0.10) |
| strafing, no Speed | 329 us | 866 us | 11,375 (15,250) us | 23 (0.75) |
| strafing with Speed | 340 us | 901 us | 11,625 (14,250) us | 25 (0.81) |

Speed does not change the chaser's cost. One runner frame of 425 ms
(`red_ret_L3_respawn5`, level 24,050-25,050) is a single outlier. The
frame's events are an ordinary runner search, and the runner prints
`[SPIKE]` lines inside its timed boundary to a log on the D: spinning disk.
That fits the known D: write stall, but it is not proven.

**Reading.** A faster chaser from the same post changes nothing for the
carrier: on returns it starts too far behind, and on pickups the only lethal
moment is the stand exit, which it does not affect. What would change it is
where it starts or where it goes: a chaser posted along the return route, or
one that runs to intercept at the carrier's base. Both are owner choices.
