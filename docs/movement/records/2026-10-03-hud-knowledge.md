# What the HUD and console tell a player: bot knowledge audit and the first uses (2026-10-03)

Owner (2026-10-03): "unseen pickups can be reacted to, HUD and console message communicates that. maybe look for
other 'ignored' things that should be known via similar methods."

The bots' knowledge boundary was perception: sight (`Visible`, `RunnerCanSee`: PVS and a trace), a reaction delay
(`Perceived`), and hearing as distance (`Known`, `bot_defenderHearRange` 1,500 u). A human also has the HUD flag
status, the console ("X got the RED flag", captures, returns), the kill feed, the scoreboard, the team overlay
(teammates' health, armor, location name, weapon, powerups; not Force) and sounds.

## Audit (bot2_defender.c, ai_bot2_objective.c, ai_bot2.c, bot2_move.c)

**Ignored, though a human has it from the HUD or console:**

1. Chase D's chase starts from sightings only (`chaseSeenTime`), and that field is set once and never cleared, so
   the gate really means "has seen anyone since release".
2. **The intercept was computed from the last sighting as if it were current.** Chase D runs for the first cell of
   the carrier's route home that it reaches first, from `lastSeen`; an unseen carrier never updates `lastSeen`, so a
   carrier already home was "intercepted" on a route it had finished. This is what held both chase Ds in the 171 s
   Dantooine standoff (seed 4): both were `chasing=1`, goals at intercept cells about 600 u from their own posts,
   the carriers on their stands 4,500 u away.
3. Base D's leash measures the same stale `lastSeen` from its stand.
4. Base D and the camper put the own-flag carrier first only when perceived; the carrier's identity is on the
   console. Unchanged: the owner chose this on 2026-10-02 (base D should fight the visible enemies around it).
5. **Deaths:** the runner's threat model clears an enemy only on a death it saw; the kill feed reports every death.
   An enemy killed unseen kept its regenerating Force estimate and its last-seen position (which the hand-off's
   "enemy nearer" test counted).
6. Sounds (Force throws, missiles, mine arming) and the damage-direction indicator are not modelled. Damage
   direction is moot while sight has no view cone (below).

**Correctly perception-only:** aim and shots (`Perceived`), carrier pulls, blocking, pursuit and the camper's goals
(`Known`), item respawn learning, the runner's exposure test and recent-damage source, flag status reads.

**Leaks, for the owner (information a human would not have):** no view cone (sight is all-round); hearing is exact
position within 1,500 u through walls whatever the target does; the reset and mine errands read the live runner
position; dropped-flag positions are read anywhere on the map (the HUD says "dropped", not where); teammate Force
(the overlay has none; a comment in ai_bot2_objective.c says it does) for the hand-off and base support; teammates'
exact positions (the overlay gives a location name); unseen items' presence and exact respawn times;
`RunnerRecentDamage` reads the attacker's identity (the indicator gives a direction); the legacy `Bot2_Think` reads
live state throughout. Owner (2026-10-04): these leaks are fine; keep them, no fixes planned.

## Changes (build `b3dan-build-20261003-g`)

- **`bot_defenderTheftKnown` (default 1):** when the own flag leaves its stand with an enemy carrying it, the
  defender knows the carrier was on the own stand at that moment (`[DEFENDER] THEFT`): `lastSeen` = the stand, the
  intercept recomputed, and the chase allowed. Positioning only; the aim still needs sight.
- **`bot_chaserInterceptAge` (default 1):** the intercept counts the time since the carrier's position was known
  (a sighting or the theft); points the carrier would already have passed are dropped, and with none left chase D
  runs for the enemy stand, where an unseen carrier waits for its own flag.
- **`bot_runnerKillFeed` (default 1):** the runner's threat model takes every enemy death, seen or not, and forgets
  that enemy's last-seen position.

All three at 0 replay the earlier build's world exactly (Dantooine seed 4: identical game log but the shutdown
time). With them on, the same seed's longest standoff is 42 s instead of 171 s and its longest hold 50 s instead of
176 s (one world).

## Measured (16 seeded worlds a map, against the fixed arm of [the B3 record](2026-10-03-b3-dantooine.md))

| | Kejim fixed | Kejim HUD | Dantooine fixed | Dantooine HUD |
|---|---|---|---|---|
| captures, both teams | 8.81 | 8.45 (-0.36 +- 0.78) | 9.31 | 10.30 (+0.99 +- 0.63) |
| falls, both runners | 11.40 | 8.82 (-2.6 +- 0.9) | 4.87 | 3.96 |
| defender enemy-flag pickups | 0.74 | 1.16 | 0.93 | 0.25 (-0.68 +- 0.20) |
| longest standoff (s) | 55 | 90 | 171 | 88 |
| longest hold (s) | 57 | **527** | 176 | 168 |

`[DEFENDER] THEFT` fired 3,167 times over the 32 worlds. Dantooine gains; Kejim is flat, and its 527 s hold is a
movement defect the arm exposed, not a knowledge effect.

## Stranded off the graph

Kejim HUD seed 6: the red camper, carrying, walked off a ledge after a hop sequence, fell about 440 u without dying
to (2341, 3465, 1368) and stood there 518 s with the flag: every replan `no_path from=-1` (no graph cell under it),
and `StepOffCell` found no cell below within its 96 u reach, so the step-off walk never started. `stranded.py`
counts stranded runs over 10 s: 0-1 a 16-world arm before, 4 in the Kejim HUD arm (518 s, 446 s, 34 s, 22 s; two at
that same pocket). A pocket the graph does not cover, reachable by falling.

**`bot_strandedKillMs` (default 10,000 in play mode):** a Bot2 bot grounded off the graph with no step-off cell for
10 s takes /kill (`[MOVE] STRANDED_KILL`), as a human would. `Move_Stranded` (bot2_move.c) requires no step-off
cell, so a carrier waiting on a flag pedestal (no graph floor, a cell just below) is not stranded.

## Kejim seeded worlds do not replay across runs

Replays today: Dantooine seeds 4 and 12 replayed their campaign worlds byte for byte across builds b, c and f. Kejim
seed 6 does not: the fixed arm's world replayed on its own build c diverges at 0:12, and so does the HUD arm's on
build g; the replays agree with each other (build g and build h with the new check off are identical to 120 s).
The [pit-kill record](2026-10-03-pit-kill-and-replays.md)'s proof was two Kejim worlds run at the same time, which
a dependence on load or on the order of worlds would pass. A clue: every Kejim replay run alone takes the first flag at 0:12-0:14, the same seeds run four at a time at 0:15,
so something early in a Kejim world depends on load (setup or graph loading paced by the wall clock is the first
suspect). Open: find what a Kejim world reads that a Dantooine world does not.
