# 2026-11-09: strafe edges never flown are not charged (`bot_moveChargeFlown`, review BM-3)

Branch `claude/bot2-exec-deep-bab45d` (base 011e4501). Review item BM-3
(`.codex-research/review-bot2-20261109/move.md`, verified in `review-20261109-SUMMARY.md`); the second read of the
executor that went with it is `.codex-research/review-bot2-20261109/exec-deep.md` (BX-1..10).

## What was wrong

`StateAvoid` (bot2_move.c) puts a strafe edge on the bot's avoid list and, for a runner (not a guest), charges
`Graph_EdgeFail` on the edge every bot plans over. Two of its callers charge an edge no flight was tried on:

- **walked**: the bot is more than 64 u along a strafe chord with no sequence in progress (`Move_Command`). Mostly a
  straight hop that came down on a later chord, or a takeoff whose deferred window search was refused or found nothing.
- **declined**: the corridor refused the walk and a crossing of any type (strafe, drop, jump, pad, lift) lies within
  four path entries; that crossing is charged, whatever the corridor refused.

Two world failures turn a validated edge off for 30 s for everyone (an unvalidated one is disabled for the map), and
every charge bumps the graph's generation and reach topology. HANDOFF, "Level-2 speed row", records one case: the
stand's only strafe edge killed by "walked".

## Before: retained worlds

`.codex-research/hoproom-20261030-a` (build 97c25de3, the 4v4, 16 worlds a map), counted with
`tools/laps_edge_charges.py` (an avoid line is paired with the graph line before it). A world, runners only (the
six guests of the 4v4 never charge):

| | Kejim | Dantooine v3 |
| --- | --- | --- |
| "walked" avoids / charged / turned an edge off 30 s | 268.6 / 41.2 / 4.8 | 111.2 / 22.9 / 6.7 |
| "declined" avoids / charged / off | 101.0 / 0.9 / 0.1 | 21.2 / 0 / 0 |
| "refused" avoids / charged / off (judged; unchanged by this work) | 206.5 / 21.8 / 2.9 | 50.5 / 10.8 / 3.2 |
| `disabled=1` after any of them | 0 | 0 |

No strafe edge was disabled for the map in 32 worlds: the seeds' strafe edges are validated, so the second failure
is a 30 s off. "walked" is 61% of all strafe-edge offs on Kejim (76 of 125) and 67% on Dantooine (107 of 159). Most
avoids charge nothing because their cause reads transient (a player or missile touched within 1.5 s or near the
line) or the bot is a guest. What came before a charged "walked" avoid (the 20 Kejim control worlds below, 834 of
them): a straight hop committed under 3 s earlier 51%, a planned chain that flew another edge 19%, a rerouted hop
10%, nothing within 3 s 14%. 6% (53) name the edge the bot's own last chain was committed for: 45 of those were hit
in the flight and came down on the chord, 5 completed the chain and landed short, 3 followed an objective change
(exec-deep BX-5). A bot whose route holds an edge someone else's failure turned off walked it 12 times in the 32
retained worlds.

## The change

`bot_moveChargeFlown` (default 0; 8e0614ae, 1d2e7dea). With 1, a "walked" or "declined" avoid of an edge the bot's
last sequence did not fly goes on that bot's avoid list for the objective, as a guest's does, and writes nothing to
the shared edge. Unchanged: "refused" (the edge's candidates were judged from inside its entry envelope), a missed
flight, a transient cause (a 3 s avoid, never charged), and a walked or declined edge that the bot's last chain flew
to a landing (`EdgeFlown`: it came down short, which is that flight's result). With the cvar on the avoid line
carries `charged=0|1`; off, the line is byte for byte today's. Nothing else needed the signal: the planner reads the
avoid list, and map preparation counts `edge_result` lines, which an unflown edge no longer prints.

Also in the build, for exec-deep BX-2: a per-leg `[BUDGET] ... walk orphan_landings= orphan_alone=` line (a landing
that found a walking deferral still in the cache). A counter and a print; no decision reads it.

## Measurement

Build `.codex-research/chargeflown-build-20261109-b` (`tools/strafe_fixture_build.py` from the branch at 1d2e7dea;
manifest in the build folder). Campaign `.codex-research/chargeflown-20261109-b/` (`runarms.py`, `run.sh`,
`work.py`, `compare-*.txt`, `work.txt`, `charges-*.txt`): the milestone-B 4v4 (`laps_campaign.COMMON`,
`--two-sided`), 600 game s at rate 25, frame meter on, seeds 3-22, Kejim and Dantooine v3, arms `ctl` (defaults)
and `cf` (`bot_moveChargeFlown=1`), interleaved by seed, at most four servers on the machine (shared with the
replan-budget session; its launcher and this one count every server). Counts are taken up to 600,000 ms of level
time, since worlds overrun their 600 s by different amounts. A first build (`-a`, 8e0614ae: every walked or
declined avoid uncharged, the flown-short case included) was stopped after two worlds and is not analysed.

**The default is inert.** Five control worlds here (Kejim 3-5, Dantooine 3-4) against the replan-budget build's
control worlds of the same seeds (`replan-budget-20261109-a/worlds/ctl-*`, another branch off the same base):
game logs identical, and every decision count to 600,000 ms identical (hops by kind, replans, avoids and charges by
reason, the search budget's tallies).

## Result (20 paired seeds a map)

All 81 worlds ran to the end: no crash report, no late setup command, every game log distinct.

**Charges** (`charges-*.txt`, whole worlds, a world):

| | Kejim ctl | Kejim cf | Dantooine ctl | Dantooine cf |
| --- | --- | --- | --- | --- |
| "walked" avoids | 275.0 | 273.4 | 110.7 | 114.0 |
| ... charged to the shared edge | 41.7 | 0.4 | 20.7 | 0.05 |
| ... turned an edge off 30 s | 4.8 | 0 | 6.5 | 0 |
| "declined" avoids | 94.2 | 92.8 | 26.9 | 22.4 |
| ... charged / off | 1.5 / 0.15 | 0 / 0 | 0.1 / 0.05 | 0 / 0 |
| "refused" charged / off | 22.2 / 2.9 | 21.0 / 2.1 | 11.0 / 3.0 | 11.7 / 2.9 |
| `disabled=1` after walked or declined | 0 | 0 | 0 | 0 |

The nine charges left in the arm are the flown case (Kejim's eight are completed chains that landed short). Every
failure line of any edge type: 71.0 -> 27.8 a Kejim world, 97.2 -> 66.5 on Dantooine; timed offs of any edge
11.2 -> 5.3 and 26.6 -> 20.9. "walked" avoids did not rise without the shared charge.

**Work and movement** (`work.txt`; to 600,000 ms; cf minus ctl paired by seed, +- one standard error):

| | Kejim ctl | cf - ctl | Dantooine ctl | cf - ctl |
| --- | --- | --- | --- | --- |
| est. frames over 25 ms | 483.9 | +3.6 +- 5.5 | 1,542 | +95 +- 61 |
| est. p99 ms | 30.7 | +0.1 +- 0.2 | 54.0 | +1.3 +- 0.9 |
| forecast steps | 6.72 M | +0.9% +- 0.9 | 7.77 M | +3.2% +- 1.8 |
| planner expansions | 4.53 M | +1.2% +- 2.0 | 7.70 M | -2.6% +- 2.1 |
| replans | 3,321 | -5 +- 28 | 3,718 | +128 +- 78 |
| replans that found no path | 223.9 | -19 +- 26 | 70.8 | +22 +- 12 |
| hops committed | 1,635 | -6 +- 12 | 1,437 | +16 +- 13 |
| planned-chain hops | 215.3 | -0.3 +- 2.6 | 58.8 | -0.1 +- 3.6 |

Kejim: no measurable change. Dantooine: steps, replans and estimated slow frames each read up by 1.6-1.8 standard
errors (undecided; the estimate overstates Dantooine's p99, read the counts).

**Outcomes** (`tools/laps_compare.py`, `compare-kej.txt`, `compare-dan.txt`; a world, cf minus ctl, Welch se):

| | Kejim ctl | cf - ctl | Dantooine ctl | cf - ctl |
| --- | --- | --- | --- | --- |
| team captures (primary) | 3.69 | -0.25 +- 0.32 | 4.90 | -0.12 +- 0.40 |
| all captures | 7.39 | -0.49 +- 0.64 | 10.00 | -0.25 +- 0.77 |
| pickups | 29.24 | -0.62 +- 0.81 | 26.46 | +0.53 +- 0.73 |
| falls of both runners | 8.92 | -0.29 +- 1.01 | 2.91 | +0.49 +- 0.53 |
| weapon deaths of both runners | 31.68 | +0.05 +- 1.03 | 32.37 | +0.82 +- 0.99 |
| camper falls | 3.80 | -0.84 +- 0.46 | 1.92 | -0.05 +- 0.35 |

Verdict on team captures: **undecided on both maps**; no metric passes the cut-off on either. Dantooine's runner
falls read +2.1 +- 0.7 at nine seeds and +0.5 +- 0.5 at twenty; by what the runner last did (`falls.py`) the falls
after a knock are 2.20 against 2.25 a world and the rest is spread over a few events each.

## Reading

- The charge the review flagged is real and large: 41.7 never-flown charges a Kejim world and 20.7 on Dantooine, and
  with "walked" about two thirds of all timed strafe-edge offs. It never disabled an edge for the map in 72 worlds, because
  the seeds' strafe edges are validated; on an unprepared map (no seed) the second such charge would.
- Removing it changes nothing this arm size can see in captures, falls, hops or Kejim's frame work. Captures read a
  little down on both maps (-7% and -2%, each under one standard error) and Dantooine's work a little up; neither is
  decided, and at this noise a capture effect under about 0.8 a world on Kejim or 1.0 on Dantooine would not be.
- A possible reason there is so little either way: a 30 s off of a chord a bot keeps arriving on badly also keeps
  the other bots off it for a while, which may be worth about what the lost edge costs. Not tested.

Default stays 0 (the brief: off until measured; the measurement does not decide it). Not installed.

## Tools

- `tools/laps_edge_charges.py <world>...`: avoids by reason and what each charged.
- `.codex-research/chargeflown-20261109-b/work.py` (work meter and event counts, paired), `falls.py` (runner falls by
  the runner's last movement event), `analyse.sh`.
