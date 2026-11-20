# The early search (search budget) in the 4v4: no tail gain, and not exact with other players (2026-10-04)

**Naming.** `bot_moveSearchBudget` is not a cap on thinking. It sets the early search (pre-search): on the frames
before a landing, the bot runs the coming search's candidate rollouts from the predicted landing state, at least N a
frame (raised so the work fits the frames left). 0 means no early search: the bot searches once, at the landing.
This record says "early search" for it.

Follow-up to the [perf counters](2026-10-04-perf-counters.md): simulated forecast steps are ~90% of bot time and
the slow bot-frames are mostly whole straight searches. The search budget (`bot_moveSearchBudget`, default 4 since
2026-09-22, bot2_move.c) runs a coming search early from the predicted boundary and is meant to be decision-identical
by construction. Owner: try it first ("ok"). Research directories: `.codex-research/budget-20261004-a/` (sweep,
`sweep.py`, `sweep.txt`), `.codex-research/rng-20261004-a/` (identity worlds, state trace, `bisect/`).

## The budget's own tallies

`[BUDGET]` tallies of one 4v4 world (perf-build-20261004-b, seed 1): 4,201 straight searches, 775 served by a
pre-search (18%); 1,698 not ready at the boundary, 972 refused on candidates, 687 refused on state. 82% of searches
still run whole on their boundary frame, and the slow bot-frames are spread over every role (runner 235, camper 296,
chase D 282, base D 230 over 25 ms).

## Sweep (one world at a time, 25x, Dantooine seeds 1 and 2)

| budget | seed 1: mean / p99 / over 25 ms | seed 2: mean / p99 / over 25 ms | hits / not ready |
|---|---|---|---|
| 0 | 5.33 ms / 49.2 / 1,505 | 5.39 / 50.5 / 1,564 | - |
| 4 (default) | 7.68-7.87 / 48.5-50.0 / 1,645-1,704 | 6.23 / 47.5 / 1,305 | 715-775 / 1,698-1,743 |
| 8 | 6.85 / 48.8 / 1,442 | 6.44 / 49.0 / 1,435 | 687-793 / 1,788-1,874 |
| 16 | 6.79 / 52.8 / 1,708 | 6.51 / 49.0 / 1,593 | 702-776 / 1,679-1,766 |

A larger budget raises neither the hit rate nor the tail: "not ready" is not short of rollouts. The p99 is the same
with no early search at all, and the mean is 15-45% lower without it: 82% of early searches are discarded or
unfinished, so the search is then run a second time on the landing frame. These worlds are not identical (below), so the
frame figures compare different games; the direction is consistent on both seeds.

## Not exact: the pre-search judges against other players' old positions

Every budget gives a different game from the same seed (budget 4 is deterministic: two runs identical). Seed 1's
first difference is client 0's straight search at level 7525: candidates 0-1 refused on a body contact with budget 4,
none with budget 0. Bisected in 12-second seeded worlds (~10 s wall each, `bisect/run.sh`):

- not rollout replay (identical split with `bot_forecastReplay 0`);
- not the game's random stream: rollouts restore it now (`Rand_State`, q_math.c; below), and nothing changed;
- not the world: `bot_stateTrace` shows every client and missile identical every frame to 8,000 ms;
- `bot_forecastBodyLog 1` (new, diagnostic): client 0's rollout hits player 4 at (4654, -979, -25), where player 4's
  state and link really were, while at 7,525 player 4 is at (4212, -1172). The rollout lines say `level=7525` because
  the pre-search sets the clock to its target; they print among other bots' pre-search lines, in an earlier real frame.

The pre-search runs on frames before the boundary against the world of those frames. At the boundary it compares the
bot's own state and re-validates the chosen candidate with one rollout, but every candidate it refused or ranked
against another player's body (or missile) where that player stood frames earlier stays as judged. A full search at
the boundary sees the bodies where they are. With one bot (where it was designed and verified, 2026-09-22) nothing
else moves; with seven other players something nearly always has. The same holds for anything the world changes
between the chunk and the boundary.

## The random stream (kept, latent)

Pmove's spinning-saber stir draws from the game's one random stream (`holdrand`, q_math.c) and rollouts did not put it
back, so a different number of rollouts would move every later draw (aim, spread, spawn choice). It changed nothing in
these worlds (no rollout reached that draw), but it is a real leak: `Bot2_ForecastRun` now saves it with the clocks and
restores it (`Rand_State` beside `Rand_Init` in shared q_math.c). Worlds on the build with it are identical to before.

## Options for the owner

1. **No early search in play** (`bot_moveSearchBudget 0`): each bot searches once, at the landing; the decisions become the full search's at every boundary (the
   reference the budget was meant to equal), mean frame cost 15-45% lower in these worlds, p99 unchanged. Changes
   decisions relative to today's default, so it wants a laps comparison before it is a default.
2. **Make it exact:** at the boundary, also require every other player and missile to be where the chunk saw them (a
   compare of those entities, cheap), else fall back to the whole search. Exact by construction again, but in a 4v4
   it would almost never hit, so it would be pure overhead; worth it only for lone-bot fixtures.
3. Keep it as it is: an approximation that judges against slightly old body positions, at a mean cost and no tail gain.

The tail is unchanged by any of these: it is whole searches, and a search of ~2,000 steps is ~27 ms.
