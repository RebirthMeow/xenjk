# Red-stand library re-explored; both gates re-taken on the slow-drop build

Recorded 2026-09-25 (collection 2026-09-24 21:53 to 2026-09-25 00:19). Owner
scope: red stand only; the blue side, respawns and Dantooine libraries wait
until the gates pass.

| Item | Value |
| --- | --- |
| Build | `dropslow-build-20260924-b` (c126d485: own flag, circle run-up, slow drops 150 by default, the review repairs) |
| Graph | research seed `g1-98eb1e97c24a4e21` |
| Library | `.codex-research/slowdrop-chain-20260924-a/library/mp_ctf_kejim_-1301924053` (level 1 `t1e83`, level 2 `t1e83s1`) |
| Settings | `bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveReroute=1`, everything else default |
| Worlds | explore and library check: `.codex-research/slowdrop-chain-20260924-a/` (`slowdrop-chain-20260924-a.sh`); gates: `.codex-research/gate-20260924-f/` (the `gate-20260924-e` harness rebound to this build and library; `analyse.out.txt`, `analysis.json`) |
| Sources | hashes and both binaries matched before the first gate world and after the last (`verify-before.json`, `verify-after.json`) |

## The library

Explorer as before (beam 3, patience 2, `bot_moveSuccessorAim=0`), level 1
and level 2 side by side, classes 125/100/150/75; every cell confirmed a tape
on its first try, 21:53 to 23:35. Measured with the library, two worlds a
cell, identical to the explore in all 16:

| Stand | 75 | 100 | 125 | 150 |
| --- | --- | --- | --- | --- |
| Level 2, new library | **8,150** | **9,100** | 9,875 | **9,575** |
| Level 2, library off (circle record) | 8,350 | 10,125 | 9,875 | 10,125 |
| Level 2, stale library (circle record) | 9,900 | 10,350 | 10,100 | 11,500 |
| Level 1, new library | 11,650 | **14,925** | 14,325 | **14,125** |
| Level 1, library off (circle record) | 11,650 | 15,925 | 14,325 | 16,525 |
| Level 1, stale library (circle record) | 11,650 | 16,100 | 14,325 | 13,950 |

At level-1 class 125 the explorer's best branch (15,000) did not beat its own
control (14,325), so that tape is the control's route. Kejim floor with the
library 36/36.

## Level-2 gate

| Row | Result |
| --- | --- |
| 0. Pre-gate floors | Kejim 36/36 (phase B, library on), ctf4 36/36, Dantooine v3 36/36 on this build (`floor-ctf4`, `floor-dantooine_v3`); zero deaths. Level-2 cases 12,200-20,750 ms on ctf4, 11,525-14,700 on Dantooine. **Met** |
| 1. Safety | Kejim floor as above; harness-on arm 366-425 comparisons per world, **zero mismatches**, times equal to the harness-off worlds |
| 2. Speed | `red_out_L2_stand` **8,150 / 9,100 / 9,875 / 9,575 ms**, three worlds a class, identical. Every class under 13,000 (reference 9,000; two classes under it). **Met** |
| 3. Coverage | Twelve level-2 cases, three repetitions: **36 of 36, no death**. Medians 7,625 (blue return respawn4) to 11,825 (blue out respawn4); tail 12,425 (red out stand). Previous gate: medians 8,225-13,200 |
| 4. Cost | One bot, budget 4, the cost runner's Speed spend: p99 **12.7 / 12.8 ms**, 2 frames over 25 ms per 800 in each world; worst 87.5 / 86.0 ms, 83 / 81 ms of it in `prepare`. **Met** |

**Every level-2 row met.**

## Level-1 gate

| Row | Result |
| --- | --- |
| 1. Safety | Floor **36/36**, zero deaths, owners `move` and `jumppad`; seeded regressions at class 125 pass (17,000 and 17,000), respawn4 at class 50 pass (15,525 and 15,525). Harness-on arm 526-710 comparisons per world, **zero mismatches**; decisions identical to the harness-off worlds at all six cells in every field but `traces` (three pairs differ there only, as in the previous gate). **Met** |
| 2. Speed | Library **11,650 / 14,925 / 14,325 / 14,125 ms**, six worlds a class, identical. Median **14,225** (bound 16,500), tail **14,925** (bound 18,000). **Met.** Controls 11,650 / 15,925 / 14,325 / 16,525 |
| 3. Mechanism | Waiting share, library worst against control worst: **8.3 / 0.0** at class 75, 32.1 / 48.1, 51.0 / 51.0, 28.1 / 56.6. **Not met at class 75**, strictly: five of six library worlds wait 0.0, one (`c75-lib-3`) stands two grounded frames at touchdown 9 (level 15,600, 664 u/s, 131 u/s lost) where the others take a single grounded frame; same tape, 8 of 8 entries hit, same 11,650 ms. Classes 100 and 150 now wait less than their controls |
| 4. Coverage | Twelve level-1 cases, three repetitions, library on and off: **72 of 72, no death**. The library selects only on `red_out_L1_stand` (`exact`) |
| 5. Cost | As level 2: p99 12.7 / 12.8 ms. **Met** |

Level 1: rows 1, 2, 4 and 5 met; row 3 fails its strict reading at class 75
on one world of six, two frames, no time cost. **Owner ruling (2026-09-25):
row 3 met; both gates passed** (as the previous gate's row 3 at class 100).

## Notes

- Row 4 unpinned, `red_out_L1_stand` library 14,925 against control 14,125:
  the library's tapes are per class, and the unpinned starts land on
  whichever class the wall clock gives.
- ctf4 `red_out_L3_respawn4` lists owner `none` in both slow-drop arms
  (`dropslow-20260924-a`, this gate) and not with slow drops off. No such
  frame falls inside the evaluated leg (release 2,775, completion at +12,200
  ms, 350 ms faster than slow drops off); they are after completion, where
  the bot, still running at 780 u/s with no path, falls into the base pit.
  Outside every gate row.
