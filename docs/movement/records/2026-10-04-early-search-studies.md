# Early search studies: why it misses in the 4v4, and player-body refusals (2026-10-04)

Follow-up to [the early search in the 4v4](2026-10-04-search-budget.md). Owner: study first, refine the proposal
after; the 20-minute version (short worlds, overlapped). Research directory `.codex-research/study-20261004-a/`
(`s1.sh`, `s2.sh`, `s2_bodies.py`). Build `study-build-20261004-a` adds the not-ready reasons to the `[BUDGET]` tally
(`notready idle= predicted= judging= ready_ahead= ready_passed=`); Study 2 ran on `rng-build-20261004-d`.

## Study 1: why early searches are not ready

Two 180 s seeded Dantooine 4v4 worlds (seeds 1, 2) and the solo Kejim bench (class 150, twins skipped, 24 cases,
24 passed), early search 4.

| | 4v4 | solo |
|---|---|---|
| straight searches | 2,433 | 816 |
| served by the early search | 454 (19%) | 636 (78%) |
| refused at the landing (state / candidates) | 377 / 560 | 0 / 0 |
| not ready | 991 (41%) | 178 (22%) |
| of which: no early search at all | 884 (89%) | 127 (71%) |
| of which: still judging (short of compute) | 21 (2%) | 0 |
| predictions made / finished (ready) / used | 4,651 / 1,713 / 454 | 990 / 702 / 636 |

In the 4v4 the early search finishes 1,713 searches and 454 are used: the rest are refused at the landing (the bot's
state or its candidates changed) or overtaken by a search nothing predicted. The not-ready searches are almost never
short of compute; nine in ten fire where no early search was aimed (a replan, goal change or knock, inferred: the
tally does not say which). Solo, the early search works as designed: 78% served, nothing refused.

## Study 2: are player-body refusals real?

One 180 s seeded 4v4 world (seed 1) with `bot_forecastBodyLog 1` and `bot_stateTrace`. For each rollout collision
with another player, the player's position at the rollout's predicted contact time (state trace) against the position
the rollout froze it at.

- 16% of candidate rollouts are refused on a body contact (7,366 of 46,564); 33% of searches lose at least one
  candidate that way (607 of 1,840).
- Of 1,748 player contacts, the player was still within 32 u of the frozen position at the contact time in 20%, within
  64 u in 30%, within 128 u in 47%. Median distance moved 144 u (p25 47, p75 319, p90 616).

Most player-body refusals are against a player who would have moved on by the time the bot got there. Rollouts freeze
every other player for the whole flight (up to ~3 s), with or without the early search.

## Not done

Study 3 (solo timing, early search 0 against 4 on the current build) needs worlds run alone, one at a time; not run in
the 20-minute pass.

## Study 2, full: eight 600 s worlds (Dantooine and Kejim seeds 1-4)

`.codex-research/study-20261004-b/` (`run.sh`, `bodies.py`, `bodies.txt`), study-build-20261004-a, four at a time.
The Dantooine worlds ran short of 600 game s under the logging (463-579 s; the wall-clock cap), Kejim 581-606 s.

13% of candidate rollouts are refused on a body contact on both maps (77,129 of 575,781 Dantooine, 88,049 of 667,328
Kejim). Of 38,894 player contacts, where the player really was at the contact time against where the rollout froze it:

| | contacts | within 32 u | within 64 u | within 128 u | median moved |
|---|---|---|---|---|---|
| Dantooine | 18,252 | 23% | 34% | 50% | 131 u |
| Kejim | 20,642 | 20% | 29% | 43% | 167 u |
| both | 38,894 | 21% | 31% | 46% | 148 u |
| searching: runner / camper / chase D / base D | 10,603 / 9,784 / 10,795 / 7,712 | 24 / 25 / 17 / 19% | | | 139 / 129 / 159 / 167 u |
| hit: runner / camper / chase D / base D | 12,389 / 9,842 / 9,769 / 6,894 | 20 / 17 / 23 / 28% | | | 170 / 190 / 136 / 93 u |

The short world's figures hold on both maps and for every role: about one contact in five is with a player still where
the rollout froze it; the median player has moved 148 u (four and a half hull widths) by the time the bot would get
there. Base D, who holds a post, is the most often still there (28%). The measure is a position check, not a
re-simulation: a player who moved on may have moved into the path elsewhere, which the frozen rollout cannot see either
(the live contact knock, `bot_moveContactKnock`, handles a real touch in flight).

## Proposals (2026-10-04, owner: both teed up for the next session, independent of each other)

**P1, players as obstacles only near in time (decision quality, not compute).** Rollouts keep colliding with other
players for the first T ms of a flight and ignore them after it: `ForecastTrace` (bot2_forecast.c) drops CONTENTS_BODY
from the mask once the rollout's step time is more than T past its root; standing on a player at the end still
refuses; a real touch in flight is still handled live (`bot_moveContactKnock`). New cvar, default off until measured.
- Compute: the check itself is free, but body-refused rollouts stop at the contact (the traced one at frame 39 of
  113), so up to an estimated 5-10% more simulated steps; report it.
- Effect on the early search: at best its 4v4 hit rate 19% -> ~42% (if every candidate-set refusal is body-driven);
  the 36% of searches with no early search at all are untouched. Not a reason to order P1 before P2.
- Measure: solo bench identical (no other players); T = 0 / 250 / 500 ms, 16 paired seeded worlds an arm on
  Dantooine and 16 on Kejim; captures, pickups, falls, weapon deaths, in-flight contacts, body-refusal share, mean
  frame cost. Risk: more flights through crowds, more real contacts and knocks; base D at its post is the most often
  really there (28%).

**P2, early search only away from other players (compute).** Skip the early search while another player is within R
of the bot; solo keeps it exactly as today. Removes most of the 4v4's discarded early work (no early search measured
15-45% lower mean). Cost: the ~19% of 4v4 searches it serves would run on their landing frame when players are near,
so judge it on frames over 25 ms and p99 as well as the mean. Measure: solo bench identity and timing; 4v4 timing pairs
and `[BUDGET]` tallies for two or three R; then laps outcomes.

Optional first check (~15 min): log why the candidate set differs when an early result is refused, to size how much
of the 23% depends on other players.
