# The level-2 decision library

Recorded 2026-09-24. Owner direction the same day: build a level-2 library
as at level 1. Follows the [level-2 baseline](2026-09-24-level2-baseline.md).

| Item | Value |
| --- | --- |
| Build | `l2lib-build-20260924-a` (commit 44770bad) |
| Change | library keys carry the leg's spend policy (`t1e83s1` for the Speed leg; no-spend legs keep `t1e83`); `movement_decision_explore.py --case` |
| Graph | research seed `.codex-research/move-explore-20260916-b/seed.txt`, `g1-98eb1e97c24a4e21` |
| Explorer | `.codex-research/explorer-l2-chain-20260924-a.sh`: `--case red_out_L2_stand`, beam 3, patience 2, `bot_moveSuccessorAim=0`, classes 125, 100, 150, 75 (the build-`k` settings) |
| Library | `.codex-research/explorer-l2-library-20260924-a/library/mp_ctf_kejim_-1301924053`: the four build-`k` level-1 tapes and the four level-2 tapes (staged as `l2-c*-best.tape` in `.codex-research/explorer-l2-tapes-20260924-a/`, since both sets are named `c*-best.tape`) |
| Measurement | `.codex-research/explorer-l2-chain-20260924-b.sh`, production settings (`bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveReroute=1`, harness off) |

The first chain's library step failed on the duplicate tape names; the
second chain repeats only the library and the measurement.

## Results

| Class | 75 | 100 | 125 | 150 |
| --- | --- | --- | --- | --- |
| Level 2, baseline (no level-2 tape) | 13,575 | 15,600 | 15,200 | 13,575 |
| Explorer best, confirmed | 13,075 | 13,425 | 13,525 | 13,450 |
| Explorer worlds | 124 | 128 | 94 | 54 |
| **Level 2 with the library, two worlds a class** | **13,075** | **13,425** | **13,525** | **13,450** |
| Level 1 with the library | 13,625 | 13,975 | 12,300 | 14,075 |

- Every level-2 world selects its class's tape on the `exact` rung under
  `t1e83s1` and reproduces the explorer to the millisecond, both repetitions
  (one class-125 world missed its class before release and was retried).
- Level 1 is unchanged: the four classes equal the gate re-take on build `d`.
- Floor with the library: 36/36, zero deaths, owners `move` (and `jumppad`
  on the four respawn-5 cases, accepted 2026-09-22). Level totals 180.5 /
  122.7 / 130.1 s; level 2 was 124.7-130.6 s on earlier library floors.

## Against the level-2 gate

Row 2 is **not met**: every class is 75-525 ms over 13,000 ms (median 13,437.5,
tail 13,525). The explorer gained 500 to 2,175 ms a class and stopped on
patience at each; it branches only among candidates the search already
offers on the route the planner installed.

## Next

The explorer's `--route-branch` (one world per release-route edge avoided,
and per reference state edge required) is the unused lever: every forced
branch so far stays on the control's route, and the level-2 baseline showed
the release stretch is a hairpin run on the ground. After that, the search's
own offers at the ledge drops and the pillar.
