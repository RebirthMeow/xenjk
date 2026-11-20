# Milestone B1: the four agent changes merged, and cheaper testing (2026-10-02)

Owner: address the four open B1 items with agents; make testing cheaper than eight full worlds; up to eight native
worlds at once.

## The four changes (each measured by its agent against `camper-20261002-b/ce`, 8 worlds)

| change | cvar (default) | verdict | record |
|---|---|---|---|
| defenders never take the runner team's flag | `bot_defenderFlagKeepOut` (1) | chase D's pickups 1.0 → 0 a world, carrier kills and returns unchanged | [chase-enemy-flag](2026-10-02-chase-enemy-flag.md) |
| camper displacement throw | `bot_camperDisplace` (0) | 10.5 throws a world, 80% hit, targets moved ~170 u; no capture gain | [camper-displace](2026-10-02-camper-displace.md) |
| camper reactive protect (the runner's decision, shared) | `bot_camperProtect` (0) | camper deaths 18.4 → 14.4, captures flat | [camper-force](2026-10-02-camper-force.md) |
| runner hand-off self-kill | `bot_runnerHandoff` (0) | a self-kill returns the flag to its stand (g_combat.c, standard JKA), so the camper takes it there: 15 of 15 taken, 6 captured; team captures unchanged | [handoff](2026-10-02-handoff.md) |

## Cheaper testing

- `botplayrate 12.5|25` (sv_fps 500/1000, still a 25 ms game step) and `botleveltime`; `defense_laps.py --rate` waits
  on the game clock. Alone, 25x runs a ten-minute world in 60-83 s (CPU-bound); with eight at once about 95 s each,
  about five worlds a minute against 1.8 before. Four 25x worlds matched the 5x `ce` arm on every metric. The old
  fixed wall-clock wait had cut some 5x worlds short (`camper-20261002-a/ca-2`, `ca-3`: 525 and 478 game s).
- `tools/laps_compare.py`: per-600-s metrics, arm minus baseline with a Welch standard error, `effect` (2.5 se),
  `null` (95% interval inside +-15%) or `undecided`.
- `tools/laps_campaign.py`: arms in rounds of four worlds up to eight at once, each arm stopping once decided (min 8,
  max 32).
- `tools/laps_scenarios.py`: four-bot moments extracted from retained worlds' CSVs by a condition, replayed for a few
  game seconds under every arm and compared per moment. Worlds are not deterministic: two identical arms over 8
  carrying moments differed on 3-4 of them and once by 2.5 se, so situation tests need repeats and a strict reading.

## First campaign on the merged build (`merge-build-20261002-a`, `.codex-research/merge-20261002-a/`)

Defaults (keep-out on, the rest off) against camper protect on and displacement on, 24 worlds an arm at 25x, seven at
once, 22 minutes; no crashes, all worlds distinct. Neither arm reached a capture verdict at 24 worlds:

| per 600 game s | base | protect on (`cp`) | displace on (`dp`) |
|---|---|---|---|
| runner-team captures (se) | 11.02 (0.47) | 10.52 (-0.50 +- 0.78) | 11.55 (+0.53 +- 0.82) |
| camper deaths | 18.76 | **15.50 (-3.26 +- 1.03)** | 16.35 (-2.41 +- 1.20) |
| runner weapon deaths | 13.72 | 12.64 | 13.06 |
| defender deaths | 19.99 | 21.10 | 17.98 |

- Camper protect cuts camper deaths by about a sixth, a real effect, with captures inside +-1.5; displacement changes
  nothing measurable. Both stay off unless the owner wants survival over captures.
- **Keep-out is not complete:** chase D still took the runner team's flag 9 times in 24 base worlds (0.37 a world,
  against 1.0 before); the agent's own 8 worlds had none. Its record names the gap: a goal outside the pickup box whose
  route crosses it.

## Owner decisions (same day)

- Camper protect on by default (`bot_camperProtect 1`).
- Self-kill rules on the community servers: `/kill` (console or bind) returns the flag, as here; blowing yourself up
  drops it. A dropped-flag hand-off would need the carrier to kill itself with its own splash, which the runner (saber
  only) cannot do today.
- Keep-out gap located: in all three checked pickups (base-1, -3, -12) chase D was standing at the keep-out spot
  (2464, 5216) when the runner captured, and walked north over the blue stand on its way back to its post: a route
  across the pickup box, not a goal in it.

## Keep-out for routes (same day)

`bot_defenderFlagKeepOutRoute` (default 1): a keeping-out defender's replans route around the enemy flag's pickup box,
entered as nine bodies on a 48 u lattice (`Bot2_DefenderKeepOut`, `PlanBodies` in bot2_move.c). Build
`keepout-build-20261002-a`, `.codex-research/keepout-20261002-a/`. A situation test (24 moments with chase D within
700 u of the runner's stand and the runner carrying, 30 s, both arms) showed the route bodies in use and nothing else
changed, but only one pickup in 24 off-arm windows: too rare for short tests. Laps campaign, 24 worlds an arm at 25x,
four at once (20 minutes): defenders' pickups of the runner team's flag **7 → 1** (0.29 → 0.04 a world, -0.24 +- 0.10);
team captures 9.75 → 10.25 (+0.50 +- 0.83), every other metric inside its noise.
