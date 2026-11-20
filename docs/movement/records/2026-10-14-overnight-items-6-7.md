# 2026-10-14: overnight items 6 and 7 (level-1 gate rows 2 and 4, library re-explore)

Queue: [overnight-tests.md](../../overnight-tests.md) items 6-7.

## Build

The first start (`.codex-research/gate-rows-20261013-a`, on `master-build-20261013-a`) refused every world: the
runner's source check found client/renderer files changed since that build (b0440702 and later). Its directories are
kept with `REFUSED.txt`; so is the explorer start it triggered (`reexplore-chain-20261014-a`, `explorer-reex-*-20261014-a.missed*`).
Re-run on a fresh build of HEAD (`fcb5fd27`, branch `renderer/pbr-materials`), `.codex-research/night-build-20261014-a`;
`git diff master HEAD -- codemp/game codemp/server codemp/qcommon` is empty, so it is master's game code. Chain:
`.codex-research/night-20261014-a.sh`, log `night-20261014-a.log`.

## Item 6: gate rows 2 and 4 (`.codex-research/gate-rows-20261014-a`, `run.py`)

| Row | Bound | 2026-10-09 | 2026-10-14 |
| --- | --- | --- | --- |
| 2 tail, class 150 stand | 18,000 | 18,250 **NOT met** | 18,250 **NOT met**: all six library worlds, both controls and the diag world 18,250 |
| 4 red_out_L1_stand, library vs control | not slower | 19,325 vs 18,250 **slower** | library 15,350 / 16,350 / 15,925 (median 15,925), control 18,250 / 15,925 / 16,350 (median 16,350): **met** |

Row 4's miss is gone on the merged defaults. Row 2 is unchanged, and the library makes no difference at class 150
(library and control identical), which is what item 7 addresses.

## Item 7: level-1 library re-explore (`.codex-research/reexplore-chain-20261014-b.sh`)

Copy of `defer-chain-20261005-a.sh` on `night-build-20261014-a`, new names (`explorer-reex-l{1,2}-c{75..150}-20261014-b`,
results in `reexplore-chain-20261014-b/`, `progress.log`). 05:27-06:29; every cell confirmed on its first try.

| Stand | class 75 | class 100 | class 125 | class 150 |
| --- | --- | --- | --- | --- |
| level 1, explorer best = both stand worlds | 13,375 | 14,450 | 13,625 | 14,450 |
| level 2, explorer best = both stand worlds | 8,575 | 8,350 | 9,675 | 8,200 |

Library `reexplore-chain-20261014-b/library/mp_ctf_kejim_-1301924053/` (same graph key `g1-98eb1e97c24a4e21`). Kejim
floor with it (`floor-kejim-lib`): 36/36 green.

### Item 6 rows with the new library (`.codex-research/gate-rows-20261014-b`)

`gate-rows-20261014-a/run.py` with `LIBRARY` pointed at the new library, same build, otherwise unchanged.

| Row | Bound | old library (14-a) | new library (14-b) |
| --- | --- | --- | --- |
| 2 tail, class 150 stand | 18,000 | 18,250 NOT met | **14,450 met** (six library worlds and the diag world all 14,450; controls 18,250) |
| 4 red_out_L1_stand, library vs control | not slower | met | **met**: library 14,450 / 14,450 / 17,350 (median 14,450), control 16,950 / 18,250 / 19,325 (median 18,250) |

Diag world (parity 256): 0 mismatches.

Owner, 2026-10-14 (option A): the new library is production; `tools/play_vs_runner.py` `LIBRARY` points at it and
future gate scripts copy that path. Earlier gate scripts keep naming `skill90-chain-20260926-a`, as they ran. Rows 1, 3 and 5 and the level-2 gate were not
re-taken with it (owner 2026-10-06: no routine full gates).
