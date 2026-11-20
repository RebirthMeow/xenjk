# Level-1 to 3 floors on ctf4 and Dantooine, 2026-09-23

Owner order: test fully on ctf4 and Dantooine. Build `maps-build-20260923-l`
(build `k` plus one change: `addrunner` accepts any CTF map; the only other
Kejim input, the strafe route catalog, is checksum-bound and does not load
elsewhere). Production settings less the Kejim seed and library (both are
Kejim-bound): graph built at map load, exit speed 500, state prefer, reroute.

## Cases

`tools/movement_cases_from_bsp.py` builds Kejim's declared set for any CTF map:
flag stand plus each team's two `team_CTF_<team>spawn` points farthest from its
own flag (Kejim's `respawn_4` and `respawn_5` are exactly these), 36 cases. It
reproduces Kejim's `cases.json` field for field
(`tools/test_movement_cases_from_bsp.py`). Heights come from the engine's seed
cells (`--graph-world`): an offline brush trace put Dantooine's respawns over a
pit 256 units down, because the floor there is not a brush, and every respawn
case died of `MOD_FALLING` at 3,075 ms (retained as
`.codex-research/maps-full-20260923-a-badpose/`, not a movement result). The
two methods agree on every brush floor. Fixtures:
`tools/fixtures/movement/ctf4/cases.json`, `tools/fixtures/movement/dantooine_v3/cases.json`
(map `ctf_dantooine_v3`, which has a navmesh; `mp/ctf_dantooine` is not tested).

## Dantooine v3: 108 of 108

Three floors (`.codex-research/maps-full-20260923-b/dant-{1,2,3}`), 36/36 each,
zero deaths, owners `move` and `jumppad` only. Median per level over all
cases and repetitions: L1 **20,325 ms**, L2 14,688, L3 14,888. Level-1 cases
run 17.6-24.1 s. One outlier: `blue_ret_L2_stand` 37,800 ms once (14,225 and
14,675 in the other floors), not yet examined.

## ctf4: 0 of 36

Every case stalls past its bound with owners `legacy` and `move`. The map joins
its levels with six triggered lifts (`func_door` platforms with
`trigger_multiple` triggers). The graph is static geometry: it has drop edges
down but nothing up, so from the red base the planner expands 6,574 of 14,514
cells and returns `no_path`; the rewrite then yields (`offmesh`, then
`legacy_state`) and the legacy owner wanders. Two findings:

- **Missing capability: movers.** No lift edge, no platform wait or ride.
- **Level-1 hand-off still open.** The no-legacy rule of 2026-09-22 covers
  corridor, no-path-at-commit and miss declines; a leg whose first plan finds no
  path still reaches the legacy owner through the off-mesh yield.
