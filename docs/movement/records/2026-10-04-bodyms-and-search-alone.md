# Players near in time (P1) and early search away from players (P2) (2026-10-04)

The two proposals of [the early search studies](2026-10-04-early-search-studies.md#proposals-2026-10-04-owner-both-teed-up-for-the-next-session-independent-of-each-other),
owner: build both, P1 then P2. Build `bodyms-build-20261004-a` (3296088b + both, each default off). With both off it
replays the previous build exactly: Dantooine seed 1 against `theft-20261004-a/on/w-1`, game log and all 258,503
decision lines identical (`tools/laps_identity.py`). The solo bench was not re-run: with no other player no trace
can stop on one, so P1 cannot act there, and P2 acts only with another live player in range.

## P1: `bot_forecastBodyMs` (other players block a rollout only for the first T ms)

`ForecastTrace` (bot2_forecast.c): once a rollout step is T ms or more past the frame the rollout was asked at, a trace
that stops on another player is traced again with every player's body contents cleared (restored at once; the trace
cache keys it apart). It still stops on the world, movers and any other body. Unset or negative: off.

Research directory `.codex-research/bodyms-20261004-a/` (`run.sh`, `bodyms.py`, `compare.txt`): off / 0 / 250 /
500 ms, Dantooine and Kejim seeds 1-16 paired, 128 worlds of 600 game s, four at a time, frame meter on.

| map, arm | rollouts a world | refused on a body | in-flight body contacts | simulated steps | team caps | runners' falls |
|---|---|---|---|---|---|---|
| Dantooine off | 157,340 | 14.1% | 34.4 | 11.13 M | 8.32 | 4.00 |
| T = 0 | 149,042 | 0.1% | 61.0 | 11.48 M | 8.00 (-0.31 +- 0.80) | 3.51 (-0.49 +- 0.66) |
| T = 250 | 151,396 | 3.7% | 53.8 | 11.67 M | 7.70 (-0.61 +- 0.79) | 3.95 (-0.06 +- 0.65) |
| T = 500 | 152,317 | 7.0% | 50.8 | 11.28 M | 6.78 (-1.54 +- 0.81) | 4.68 (+0.68 +- 0.55) |
| Kejim off | 168,543 | 13.7% | 39.4 | 9.63 M | 6.65 | 8.55 |
| T = 0 | 158,901 | 0.2% | 68.1 | 10.19 M | 8.01 (+1.36 +- 0.80) | **13.25 (+4.69 +- 1.01)** |
| T = 250 | 160,294 | 3.1% | 61.6 | 9.97 M | 6.65 (-0.00 +- 0.90) | 9.59 (+1.04 +- 0.95) |
| T = 500 | 162,190 | 6.0% | 53.1 | 9.84 M | 6.66 (+0.01 +- 0.85) | 10.35 (+1.80 +- 0.95) |

- **Mechanism works as built:** body refusals fall from 14% of candidate rollouts to 0.1-0.2% (T = 0), 3-4% (250),
  6-7% (500).
- **Contacts:** in-flight body contacts (`edge=knock reason=contact`) rise with it: 34 -> 51-61 a world on
  Dantooine, 39 -> 53-68 on Kejim. Flights the rollout let through crowds really do meet players more often.
- **Compute:** simulated steps rise 1-5% (no arm is cheaper). Frame cost in these four-at-a-time worlds moves the same
  way (mean +0.1-0.4 ms); not a timing run.
- **Outcomes:** no arm moves team captures beyond its spread on either map (largest: Dantooine T = 500 -1.54 +- 0.81,
  Kejim T = 0 +1.36 +- 0.80, neither decided). Kejim's falls rise: both runners +4.69 +- 1.01 a world at T = 0
  (4.6 se), +1.0 to +1.8 at 250/500; Kejim T = 0 also shifts captures from red to blue (red minus blue -2.77 +- 0.85),
  Kejim's map is not symmetric. Pickups are equivalent in every arm (inside +-15%).

**Reading:** letting flights through players does not buy captures, costs slightly more search, and on Kejim's chasm
it costs falls: a contact in flight is a knock over the pit. The frozen player is usually gone by the contact time,
but the one that is not is the one that matters there. P1 stays off (default unset).

## P2: `bot_moveSearchAlone` (no early search near another player)

`SearchBudgetStep` (bot2_move.c): while another live, non-spectator player in PM_NORMAL is within R units, the early
search stands down (a pre-search in progress is abandoned) and the search runs whole at the boundary. `[BUDGET]`
`notready` lines gain `skipped_near` (frames stood down). Unset or 0: off.

Timing, `.codex-research/alone-20261004-a/` (`timing.sh`, `timing.py`, `timing.txt`): Dantooine seeds 1 and 2, one
world at a time.

| arm | mean ms (s1 / s2) | p50 | p99 | over 25 ms | steps (M) | early-search hits |
|---|---|---|---|---|---|---|
| off (early search 4) | 8.80* / 6.81 | 4.25 / 3.00 | 56.5 / 52.5 | 2,102* / 1,518 | 13.02 / 9.55 | 775 / 715 |
| R = 512 | 6.60 / 6.80 | 2.50 / 2.25 | 52.2 / 53.5 | 1,523 / 1,713 | 9.33 / 10.08 | 556 / 459 |
| R = 1024 | 6.40 / 6.29 | 1.75 / 1.50 | 51.5 / 53.0 | 1,570 / 1,652 | 9.26 / 9.44 | 260 / 295 |
| R = 2048 | 5.86 / 5.72 | 1.00 / 1.00 | 52.5 / 52.5 | 1,650 / 1,646 | 8.52 / 8.49 | 71 / 52 |
| no early search (budget 0) | 5.80 / 5.53 | 0.75 / 0.75 | 53.2 / 52.2 | 1,715 / 1,608 | 8.41 / 8.40 | - |

\* overlapped a disk-heavy analysis of the P1 worlds; re-run alone (`off/w-1b`): mean 8.50 ms, p50 4.25, p99 55.0,
1,955 over 25 ms.

- The mean falls with R toward the no-early-search figure; R = 2048 is effectively budget 0 (52-71 hits a world).
- p99 (51.5-56.5 ms) and frames over 25 ms do not improve with any R or with no early search: the tail is whole
  searches, as before.
- Each arm is a different game (the early search judges against old positions, so standing it down changes
  decisions); the steps also differ by world, not only by R.

Laps outcomes (`laps.sh`, `laps-compare.txt`): R = 1024 and budget 0, Dantooine and Kejim seeds 1-16 paired, against
P1's off worlds (same build and arguments).

| map, arm | team caps | pickups | runners' falls | runners' weapon deaths | wall a world (four at a time) |
|---|---|---|---|---|---|
| Dantooine off | 8.32 | 27.54 | 4.00 | 31.18 | 197 s |
| R = 1024 | 7.77 (-0.55 +- 1.16) | 26.95 = | 4.69 (+0.68 +- 0.59) | 30.10 = | 166 s |
| budget 0 | 8.26 (-0.06 +- 1.05) | 28.41 = | 4.01 (+0.00 +- 0.60) | 30.94 = | 147 s |
| Kejim off | 6.65 | 29.05 | 8.55 | 28.50 | 173 s |
| R = 1024 | 7.08 (+0.44 +- 0.76) | 29.99 = | 8.31 (-0.24 +- 0.85) | 30.67 (+2.18 +- 1.12) | 138 s |
| budget 0 | 7.64 (+0.99 +- 0.90) | 29.52 = | 8.13 (-0.42 +- 0.89) | 30.75 (+2.26 +- 1.08) | 125 s |

(= : 95% interval inside +-15% of the control.) Nothing moves beyond its spread but Kejim's camper captures
(+1.36 +- 0.51, +1.85 +- 0.48). Every changed arm on Kejim, P1's included, shows camper captures 2.2-3.5 against the
control's 1.60, and Kejim worlds do not replay once any decision changes: the control looks low by chance, not a
P2 effect. The wall time a world falls 16-28%, but the control ran beside P1's arms and these beside each other, so it
is only indicative; the timing table is the cost measure.

In-flight body contacts (`contacts.py`, `contacts.txt`) do not move: Dantooine 34.4 off, 32.9 (R = 1024), 36.1
(budget 0); Kejim 39.4, 43.3, 42.4; standard errors 1.3-2.4 a world. Body-refused candidates stay at 12-14%.

**Reading:** standing the early search down near players (or turning it off) changes no outcome measurable at 16
worlds a map and lowers the mean bot cost toward the no-early-search figure; the p99 and frames over 25 ms are
unchanged by every setting. P2 at a large R keeps the early search exactly as today for a lone bot (the bench, where
it was qualified) while giving the 4v4 nearly all of budget 0's mean gain; R = 2048 has budget 0's step count and was
not itself laps-tested. Defaults unchanged pending the owner (below).

## Options for the owner

1. **P2 on at R = 2048** (`bot_moveSearchAlone 2048`): the 4v4 mean cost of budget 0, a lone bot unchanged. Measure
   first: one 16-seed laps arm a map at 2048 (about 30 min), or accept budget 0's outcomes as standing for it.
2. **No early search** (`bot_moveSearchBudget 0`): the same mean in the 4v4 and simpler, but it changes the solo
   bench's decisions (78% of its searches are served early there); needs a bench re-take.
3. **Keep both off:** today's behaviour. P1 stays off in every option (falls on Kejim, no capture gain).

The tail (p99 about 52-56 ms) needs whole searches finished outside their frame in every option.

## Owner decision and confirmation (2026-10-04)

Owner: P1 makes things worse, use P2. `bot_moveSearchAlone` now defaults to 2048 (0 turns it off); build
`alone-build-20261004-b`, `.codex-research/alone-20261004-b/` (`run.sh`, `compare.txt`). With it set to 0 the build
replays `bodyms-20261004-a/off/dan-1` exactly (game log and 258,503 decision lines). The default against the off worlds,
16 paired seeds a map:

| map | team caps | pickups | runners' falls | runners' weapon deaths |
|---|---|---|---|---|
| Dantooine | 8.32 -> 7.76 (-0.55 +- 1.05) | 27.54 -> 28.47 = | 4.00 -> 4.07 | 31.18 -> 30.94 = |
| Kejim | 6.65 -> 7.76 (+1.12 +- 0.88) | 29.05 -> 29.51 = | 8.55 -> 8.07 | 28.50 -> 30.99 (+2.50 +- 1.11) |

As for R = 1024 and budget 0: nothing beyond its spread but Kejim's camper captures (1.60 -> 3.51), the control's low
draw seen in every changed arm. The solo bench is unchanged by construction (no other player, so P2 never acts).
