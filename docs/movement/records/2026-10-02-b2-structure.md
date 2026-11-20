# Milestone B2, structure: two runners and a base each side (2026-10-02)

Plan: [MILESTONE_B.md](../MILESTONE_B.md) step B2. Build `b2-build-20261002-a`, `.codex-research/b2-20261002-a/`.

## Built

- **Two runners.** `Bot2_IsMovementOnly` accepts `Runner2_v2`; a second `addrunner` adds it (the same bot profile,
  `kejim_runner.bot`). An audit of every single-runner assumption found the movement core per client; the decision
  tape (one global tape, never matching in production: its only key is a red out-leg with spend policy s1) is turned
  off in two-sided worlds (`bot_moveTapeLibrary 0`); the parity harness, explorer and fixture directives are single
  client but off in production; the reach cache holds one goal (performance only).
- **Two of each declared role** (bot2_defender.c `slotNames`): `Defender_v2`/`Defender2_v2`, `Chaser_v2`/`Chaser2_v2`,
  `Camper_v2`/`Camper2_v2`; `adddefender <team> [chase|camper]` takes the role's first free name. Position cvars carry
  the slot's prefix (`bot_defender2Post`, `bot_defender2MineSpots`, ...); teammates are found by team (`Mate`).
- **`defense_laps.py --two-sided`**: Runner2_v2 on red and Defender2_v2 on blue at the blue posts and mine spots;
  `sv_maxclients 8`; captures per bot and team from the play CSV (`captures_by_team`); `laps_compare.py` reports
  `blue_caps`, `red_caps`, `red_minus_blue`.

## First two-sided worlds (runner + base each side, Kejim, 16 worlds at 25x, ~30 s wall each)

All four bots run (each runner covers ~44,000 u in five game minutes). Captures are under one a team per ten minutes
(blue 0.30, red 0.12): **both flags are taken 95% of the time.** Each runner takes the other flag and waits at its own
stand; neither base goes after the enemy carrier, because base D's leash holds it within 1,500 u of its stand whenever
the other team has more than one player (bot2_defender.c, `bot_defenderLeash`). Breaking the standoff is the owner's
call (who goes for the enemy carrier in a 2v2).

## Owner: a true 2v2 is offense + chase each side (same day)

"In a true 2v2 it's 1 chase 1 offense vs 1 chase 1 offense"; the base-goes-hunting logic for a fake 2-offense lineup is
not worth building. Base support therefore moves to the 4v4. `defense_laps.py --two-sided` now mirrors `--roles` onto
blue (`Chaser2_v2` at the blue chase and ambush posts).

Offense + chase each side, Kejim, 16 worlds at 25x (two identical arms, `.codex-research/b2-20261002-a/oc/`, ~87 s
wall a world), every world distinct, no crashes:

- **Captures:** blue 4.4, red 6.1 a ten-minute world; red minus blue +1.8 +- 1.2 (inside the spread; per world from -6
  to +12). The identical arms agree on every metric.
- **Standoff:** both flags taken 46% of the time (95% with a base each side); both home 21%.
- Runner deaths: weapons 8.8, falls 3.6 a world each.

## The 4v4 (B3 lineup, same day)

`defense_laps.py --two-sided --camper --roles base,chase`: runner, camper, base D, chase D a team. Build
`b2-build-20261002-a`, `.codex-research/b3-20261002-a/base4/`, 12 worlds at 25x (two identical arms, three at once,
~3 min wall a world; alone eight bots run at about 4x real time). No crashes, all distinct.

- Captures blue 4.6, red 4.35 a ten-minute world (runners 2.8 / 2.75, campers 1.8 / 1.5); red minus blue -0.25 +- 0.9.
- Both flags taken 24% of the time, both home 24%.
- Red chase D captured twice in 12 worlds: the keep-out still lets a chase D carry the enemy flag (unexplained).
- The identical arms differed on red captures by 2.5 se (5.4 against 3.3 at 6 worlds): with ~14 metrics at n=6 a
  false flag is expected; trust only the campaign's primary metric on small arms.

Base support (agent, [record](2026-10-02-base-support.md)) is merged with `bot_baseSupport 0`: it delivers Force
(20.6 team energizes a world, carrier Force near its stand 14 -> 56) but no capture gain, and more standoff (33 -> 42%).
