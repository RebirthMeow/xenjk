# 2026-11-09: one dropped-flag reach test, and an edge test on the defenders' last few units (review BA-2 with BA-9)

Branch `claude/bot2-review-next`, 5f4bc099. Review items BA-2 and BA-9 in
[docs/review/2026-11-09-bot2.md](../../review/2026-11-09-bot2.md) (lane report [ai.md](../../review/2026-11-09/bot2/ai.md)).

## What was wrong

- **BA-2.** Chase D's flag return (`ReturnFlag`) took any dropped own flag as its goal and it outranks every other
  errand. The runner takes a dropped flag only through `ReachableDrop` (no hazard on it, a graph route to it), and
  the camper only when the flag has a cell. A flag at the bottom of Kejim's chasm was a no-path goal for chase D
  until its native return.
- **BA-9.** Five straight walks onto a point inside 72 u (the flag return, `Rearm`, `RearmPlan`, the mine spot, the
  camper's take and retake) replaced the movement's command with forward 127 and no edge test, as the saber walk
  did before `bot_defenderMeleeEdge` (base D falls 3.38 -> 1.31 a Kejim world on 2026-10-06). The mine-spot walk
  also skipped the keep-out test.

## The change

- `bot_defenderDropReach` (default 0). With 1, chase D and the camper take the runner's test: `ReachableDrop` is now
  `Bot2_ReachableDrop` (ai_bot2.h), cached half a second a client, one path query a tick shared by every caller.
  Chase D leaves an unreachable dropped flag and goes on to its other errands (`[DEFENDER] RETURN_SKIP`); the camper
  treats it as no dropped flag (its cell test stays in front).
- `bot_defenderReachEdge` (default 0). The five walks share one helper, `ReachWalk`, which with the cvar off is the
  old code at each site (the same tests in the same order; the mine spot keeps its missing keep-out test and its
  pitch). With 1 the walk takes the movement's test for a guest's straight walk onto a point,
  `Move_GuestReachAhead` (bot2_move.c, beside `Move_GuestEdgeAhead`): a chord to the point that is a hazard refusal
  of the ground corridor or the edge probe is not walked straight (the defender moves by `DefenderMove`, the
  planner's walk with its own brake), and floor ending within the stopping distance along the velocity brakes
  against it, as `Melee`'s brake does. Unlike `Move_GuestEdgeAhead` it does not look past the point, so a flag or an
  item near a lip stays reachable. The mine-spot walk then takes the keep-out test too. `[DEFENDER] REACH_EDGE`
  logs a change of the answer.

## Expected effect (written before measuring)

One arm with both on (`reach`). Chase D after a carrier death over a pit: fewer idle holds, returns to errands.
Defender falls near items and dropped flags on ledges: down, if any happen at these sites (the 2026-10-06 walkway
falls were the saber walk's, already fixed). Team captures: no decidable change expected at 16-24 worlds; the pair
reports `RETURN_SKIP` and `REACH_EDGE` counts and defender falls.

## Measurement

Build `.codex-research/review-next-build-20261109-b` (5f4bc099). Evidence `.codex-research/baarms-20261109-a/`, arm
`reach` (`bot_defenderDropReach=1`, `bot_defenderReachEdge=1`), seeds and control as in
[2026-11-09-carrier-sk-guard.md](2026-11-09-carrier-sk-guard.md).

## Measured (2026-11-09 and 10): three arms, the first two wrong

Each arm: Kejim seeds 35-50 and Dantooine 35-42 against the guide campaign's control (16 + 8 pairs). Event counts
from `events.py` (beside the evidence), cut at level 600,000.

| arm | build | what chase D left | RETURN_SKIP a world (Kejim / Dantooine) | team captures Kejim | Dantooine |
| --- | --- | --- | --- | --- | --- |
| 1 `baarms-20261109-a` | -b, 5f4bc099 | any flag the runner's test did not answer yes | 49 / 45 | +0.90 +- 0.38 | +0.06 +- 0.52 |
| 2 `reach2-20261109-a` | -e, 80ad192b | a flag at rest the runner's test answered no | 20 / 19 | +0.16 +- 0.39 | -0.24 +- 0.54 |
| 3 `reach3-20261109-a` | -f, bc2e1b8c | a flag at rest with no cell within 64 u or a hazard on it | 1.75 / 0.5 | **+0.68 +- 0.32** | +0.25 +- 0.48 |

The first arm counted a pending shared query (another bot had that tick's one query) and a flag still bouncing as
unreachable; the second still judged the flag by a graph route from the asking bot's own cell, which fails whenever
chase D is in the air or off the graph, so it left flags on ordinary floor (a flag at 2233,4421,1800 read reachable
and unreachable a tick apart). The third tests the flag itself, as the review's fix reads, and every flag it leaves
lies at z = 24 on Kejim, the chasm floor. Only the third is the fix; the first two are kept as evidence.

Third arm, the rest (`reach3-20261109-a/compare-*.txt`, `events.txt`): Kejim runner captures +0.37 +- 0.30, camper
captures +0.31 +- 0.29, runner falls -1.05 +- 0.71, runners' falls -0.99 +- 0.80, team returns -0.86 +- 0.91,
defender deaths -0.83 +- 0.77, all captures +1.23 +- 0.64. Dantooine runners' falls +1.36 +- 0.96, everything else
within its noise. The edge test fired 86 brakes and 35 refusals over 16 Kejim worlds (10 and 9 over 8 Dantooine).

**Undecided** on both maps at 16 + 8 pairs (Kejim at 2.1 se, the cut-off is 2.5). It reads as a gain on Kejim,
where the chasm takes dropped flags, and as nothing on Dantooine. The two parts (the reach test and the edge test)
were measured together. Next, if the owner wants it decided: more Kejim seeds for arm 3 (about 16 more pairs, half
an hour), or the two parts apart.

## Default (owner, 2026-11-10)

`bot_defenderDropReach` and `bot_defenderReachEdge` are 1 by default since 1fb110d0 (owner: "Default it on now", on
the third arm's reading, undecided). 0 turns either off. Watch Dantooine runners' falls (+1.36 +- 0.96 in the arm)
and Kejim captures in the next outcome runs; controls for later arms must be on a build with these defaults.
