# Milestone A defaults on ctf4 and Dantooine v3 (2026-10-02)

Owner request: test the latest runner and defenders on ctf4, then Dantooine. Build `maps-build-20261002-a` (e9458384,
which includes the unmeasured mine/rocket sight fix), `.codex-research/maps-laps-20261002-a/` (`run.sh`, `summ.py`,
`summary.txt`). Arms as `protect-20261002-a/run2.sh` "po" (runner defaults: protect on, absorb off, self-kill after a
capture; defenders rearm, reset, two mines, weapons `8a,11,10a`, `bot_defender 2`), 8 laps worlds of ten game minutes a
map, then 8 Kejim worlds on the same build. Every world distinct, no crashes.

## Setup for the new maps

- `tools/defense_laps.py --map`: other maps stage their prepared seed (no tape library); `--chaser-ambush` applies only
  where ambush posts exist (Kejim's chasm), elsewhere chase D waits on its own post.
- `tools/fixtures/movement/defender/posts.json` (`transfer_source`): Dantooine from the base-defender study's clusters
  (stand, forward post, side post = chase D's post); ctf4 has no base demos, so the stand and one graph cell 1,245 u
  toward the enemy stand. Mines 50 u either side of each stand.

## Results (per ten-minute world)

| | ctf4 | Dantooine v3 | Kejim (same build) | Kejim, `fix-20261002-a` po (10 worlds) |
|---|---|---|---|---|
| captures (se) | 7.00 (0.73) | 7.75 (0.65) | 5.38 (0.56) | 6.90 (0.69) |
| runner pickups | 13.1 | 20.9 | 23.5 | 22.4 |
| runner weapon deaths | 7.9 | 18.2 | 22.0 | 19.5 |
| runner falls | 0.75 | 1.38 | 3.75 | 3.10 |
| pulls hit / death / pit | 8.9 / 0.88 / 0.50 | 19.8 / 6.0 / 0.38 | 19.1 / 6.6 / 1.1 | 30.0 / 8.4 / 1.5 |
| protect presses (mine) | 9.5 (0.1) | 31.2 (3.4) | 32.4 (2.0) | 34.1 (0) |
| protect up / Speed carrying | 5% / 57% | 16% / 59% | 22% / 71% | 23% / 67% |

- **Transfer holds.** Both new maps capture at or above Kejim's rate with no map-specific change; falls are lower
  than Kejim's on both.
- **ctf4 is a light fight** (weapon deaths 7.9, a third of Kejim's); its defender posts are guessed, not demo-derived,
  so it may understate the defence.
- **Dantooine is Kejim-strength:** pull then repeater alt splash is the main killer (9.1 a world); pits are rare.
- **Mine sight fix (e9458384):** the mine reason now fires (2.0 a world on Kejim, 3.4 on Dantooine, 0 before).
- **Kejim same build 5.38 captures against 6.90 on the old build:** about 1.7 standard errors apart at 8 worlds; not
  resolved. Re-run 16 worlds before attributing it to the sight fix.
