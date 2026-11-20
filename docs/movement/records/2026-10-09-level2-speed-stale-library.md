# Level-2 speed: not the library, the window deferral (2026-10-09)

Branch `research/l2speed-20261009` (worktree `../xenjk-wt-l2`), off HEAD fc1187ba. Build
`.codex-research/l2speed-build-20261009-a` (fc1187ba + `bot_moveDeferGround`, default 0), campaign folder
`.codex-research/l2speed-20261009-a/` (`arm.py`, `cases.py`, `chain*.sh`, `arms.log`, worlds `*-c<class>-r1`). Retained
worlds read: `gate-20261009-a/l2-c*-lib-*`, `gate-20261009-a/l2arms/` (item 3 follow-up), `gate-20261005-e/l2-c*-lib-1`.
Problem: the level-2 speed row (13,000 ms or less at every class) fails on the 2026-10-06/07 defaults: 14,800 / 15,175 /
14,875 / 16,050 ms at classes 75 / 100 / 125 / 150, against 10,775 / 10,975 / 11,025 / 9,125 on 2026-10-05
([night record](2026-10-09-night-items-3-5.md), last section).

## Owner's hypothesis: a stale library or precomputed input. It does not hold.

What the level-2 stand worlds load: the production seed `move-explore-20260916-b/seed.txt` (2026-09-16), the decision
library `skill90-chain-20260926-a/library/mp_ctf_kejim_-1301924053` (2026-09-26 13:02, eight tapes keyed
`t1e83` / `t1e83s1` by class); all older than the deferral defaults (window defer
cdede706 2026-10-06, pool at frame end d1b05162 2026-10-07). The 2026-10-05 gate that passed used the same three files.

- **The library is selected and never consulted in the slow arm.** All 12 default worlds (`gate-20261009-a/l2-c*-lib-*`)
  print `[TAPE] select ... rung=exact file=l2-c<class>-best.tape` and then nothing: no `plan:tape` event, no census line.
  The first search of the run, the one that holds the tape's entry 0 (root = the stand, 152.1 -1159.2 1924.1), is the
  deferred one and never reaches the tape.
- **Removing the library changes nothing in the slow arm**: `ctl-nolib` reads 14,800 / 15,175 / 14,875 / 16,050, the same
  to the millisecond as the library arm (`arms.log`).
- **The library was not buying the 10-05 speed either.** The 10-05 gate worlds census: entry 0 hit at classes 125 and 150
  only (`hits=0 absent=1` at 75 and 100); classes 75 and 100 ran 10,775 and 10,975 ms without a single library hit.
- With the guard below and the library on, entry 0 hits at 125 and 150 and entry 1 too (`hits=2`), so the entries are
  still valid at the current build; without the library the guard arm reads 10,750 / 9,925 / 11,075 / 10,150.
  The library moves a class by 0.5-0.8 s either way; it neither explains nor fixes a 4-6 s loss.

## Mechanism: the deferred first search arrives after the stand's ledge

The stand's first planned edge (strafe edge 50732) takes off from the stand's lip, 44 u from the start. A level-2 run
buys Speed on 3125 -> 3150, and the first walking search falls due at the window entry.

- **Slow arm (defaults)**, class 100, 3150-3475 (momentum rows of `l2-c100-lib-1`): the search is deferred
  (`bot_moveWindowDefer` 1, K = 6 frames = 150 ms), the bot walks 215 -> 407 u/s, steps off the lip at ~3290 with
  `vz = 0`, `ground = 0`, `up = 0` (a fall, no jump), is airborne when the take falls due (the straight search needs a
  grounded root), lands 12 u lower, and `StateAvoid(..., "walked")` fires at 3450 (`cause=world`): edge 50732 is avoided,
  the replan has no strafe edge at all (`edges=0 cells=267 cost_ms=28138`, against `edges=16 cost_ms=14194`), and the
  first hop of the run launches at 6900 instead of 3300. Every default world has `edge:avoid` (edge 50732 in 9 of 12 worlds; in the other three
  edge 26722, `why=declined`, at 319.7 -768.0).
- **Arm E (window defer 0)**: the search runs whole at 3200, commits a `kind=state` hop (`index=50732`), jumps at 3275
  (`up = 127`, `vz = +301`) and lands 500 u on at 4150; edge 50732 completes (`edge_result success=1`).

**WindowDefer alone is the cause; the frame-end pool is not.** Arms on this build (one world a class; three repetitions
of each arm that was repeated read identically):

| arm (settings off default) | 75 | 100 | 125 | 150 |
| --- | --- | --- | --- | --- |
| default (`ctl`, = 2026-10-09 gate) | 14,800 | 15,175 | 14,875 | 16,050 |
| `bot_moveWindowDefer 0` | 9,825 | 11,950 | 10,550 | 10,900 |
| `bot_moveWalkPoolFrameEnd 0` | 14,800 | 15,175 | 14,875 | 16,050 |
| default, no library | 14,800 | 15,175 | 14,875 | 16,050 |
| `bot_moveWindowDefer 0`, no library | 9,825 | 11,950 | 10,000 | 11,425 |
| 2026-10-05 gate | 10,775 | 10,975 | 11,025 | 9,125 |

The pool at the frame's end changes when chunks run inside a server frame, not which frame a take falls due on, so it
leaves the decisions equal. The deferral is what costs: it keeps `bot_moveWindowDefer`'s frame-cost gain only if it
can still return in time.

## Fix: `bot_moveDeferGround`, `...Window`, `...Shorten` (all default 0 on the branch)

`WalkDefer` (bot2_move.c) predicts the bot's own walk K frames ahead before it defers. The stand's deferral is,
by inference (the guard arm searches whole on 3150; arm E searches on 3200), a cadence deferral begun on 3150 that the
takeoff window's opening on 3200 does not end while `bot_moveWindowDefer` is on, so it runs on to its take on 3300, after the lip. The guard reads the prediction: with
`bot_moveDeferGround N` (> 0) a deferral whose predicted walk leaves the ground before the take, or in the N - 1 frames
after it, is not started as asked. `bot_moveDeferGroundShorten 1` brings the take forward to N frames before the lip (when
that leaves two frames or more; the chunks still spread over them), else the search runs whole at once.
`bot_moveDeferGroundWindow R` limits the guard to bots with a planned strafe edge's takeoff within R u (1 means 128;
`TakeoffNear`). Tally: `[BUDGET] ... walk airborne_at_take= shortened=`.

Level-2 stand, one world a class (three repetitions of the first arm read identically), all other defaults on:

| arm | 75 | 100 | 125 | 150 |
| --- | --- | --- | --- | --- |
| default | 14,800 | 15,175 | 14,875 | 16,050 |
| Ground 3 (1 and 6 read the same to 25 ms) | 10,750 | 9,925 | 10,550 | 10,925 |
| Ground 3, no library | 10,750 | 9,925 | 11,075 | 10,150 |
| Ground 3 + Shorten | 10,600 | 11,000 | 9,675 | 8,200 |
| Ground 3 + Shorten + Window 128 / 96 / 112 / 80 | 10,600 | 11,000 | 9,675 | 8,200 |
| Ground 3 + Shorten + Window 64 | 14,800 | 15,175 | 14,875 | 16,050 |

Window 64 misses (the guard is not on at the first call); 80 and up reproduce the unlimited guard's stand exactly.
Window variants with Shorten off and 128 u: 10,725 / 9,925 / 10,550 / 10,925. The first search at the stand now chooses a
different first hop from arm E's (class 100, Ground 3: a two-hop straight launching at 3500 from y = -1023), and the
library's entry 0 is consulted again (hits at 125 and 150).

**Safety rows** (final variant, Ground 3 + Shorten + Window 80, build `l2speed-build-20261009-k`): the twelve level-2
cases, library on, all pass with 0 deaths (the four stand cases 8,200-12,600 ms against the default's 15,100-16,050; the eight
respawn cases 9,375-11,275 ms, within 0.6 s of default); parity 256 on the four stand classes: 0 mismatches (453-463 comparisons a
class). Earlier variants (Ground 3 alone, and Shorten): the same 12/12 and 0 mismatches. Level-1 stand with Ground 3
alone: unchanged to the millisecond (16,350 / 15,500 / 15,925 / 18,250); not re-run for the final variant.

## Frame cost: the guard everywhere is too dear; the takeoff-near guard costs a little

Work counts (`[FRAMEWORK]` est_over25 and est_p99_us, `kreplay-20261009-a/paired.py`), laps worlds with the
kreplay settings, 600 game s, unpinned, four at once, Kejim seeds 1-16 and Dantooine 1-8 paired against the default on the
same HEAD (the control replays K's `fix` arm to the frame: over25 551.9 and p99 31.76 both). Difference arm - control, +- se:

| arm | Kejim frames over 25 ms (control 551.9) | Kejim est p99 ms (31.76) | Dantooine over 25 ms (1,913.8) | Dantooine est p99 ms (58.39) | guard fires a world, Kejim / Dantooine |
| --- | --- | --- | --- | --- | --- |
| Ground 3 everywhere | +111 +- 25 (+20%) | +4.9 +- 0.9 | -4 +- 118 | -0.9 +- 1.3 | 377 / 140 |
| Ground 3 + Shorten, everywhere | +101 +- 21 | +4.8 +- 1.0 | -20 +- 72 | -1.4 +- 1.0 | 460 / 197 |
| + Window 128 | +20 +- 22 (+3.7%) | +1.36 +- 0.56 | +40 +- 41 | +0.33 +- 0.27 | 45 / 2 |
| + Window 80 | +16 +- 20 (+2.8%) | +0.95 +- 0.42 | +6 +- 6 | -0.04 +- 0.04 | 38 / 0.6 |

Every Kejim ledge a bot walks past in 4v4 is a place the unrestricted guard turns a spread search into a whole one;
limiting it to the bots that have a planned takeoff close by keeps the stand and removes most of that cost, **but not
all**: Window 80 still reads about +3% frames over 25 ms (undecided at 16 seeds) and +1 ms estimated p99 on Kejim (2.3 se).
Dantooine is unchanged. Whether the remainder is the guard's whole searches or the changed worlds' noise is not separated
(24 worlds each). Runner flow: Kejim hops -16 +- 15 (-2.7%), chained -13 +- 13, speed -7.5 +- 7.1, Dantooine hops +0.9 +- 0.7;
captures and falls (24 worlds): team captures -0.10 +- 0.38, runner falls +0.41 +- 0.60, everything undecided, as for any
change that replaces worlds (distinct 24 / 24).

## Reading and choices (owner)

- The library is not the cause; re-exploring it is not needed for this row (it moves a class 0.5-0.8 s). HANDOFF Open 2
  (re-explore the level-1 library at the current defaults) stands for the level-1 row, which this does not touch.
- `bot_moveWindowDefer 0` alone restores level 2 (9.8-11.9 s) and costs its frame gain back; the pool at the frame's end
  is innocent and stays.
- **Recommended:** `bot_moveDeferGround 3`, `bot_moveDeferGroundWindow 80`, `bot_moveDeferGroundShorten 1` as defaults:
  all four classes 8.2-11.0 s with every other default on, for about +1 ms Kejim est p99 / +3% frames over 25 ms
  (undecided) and nothing on Dantooine. If that remainder is too much, the next narrowing is to the first search after a
  Speed purchase or a respawn (the stand is the only measured loser), which would be nearly free; not built.
- The branch leaves the three defaults at 0 (nothing changes when merged); the gate rows read with
  `--set bot_moveDeferGround=3 --set bot_moveDeferGroundWindow=80 --set bot_moveDeferGroundShorten=1`.
- The row 2 tail and row 4 misses of the same gate run (night record) were not looked at here.
