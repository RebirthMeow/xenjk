# B3 on Dantooine, the 4v4 standoff, frame cost, and two chase D defects (2026-10-03)

Owner: "do 1-4" from the handoff's next list: (1) the 4v4 on Dantooine v3, (2) the 4v4 frame cost, (3) the one
frozen world in 16 (`runcamp/off-4`), (4) score the standoff (hold times, both flags held, how it breaks).

Evidence: `.codex-research/b3dan-20261003-a/` (`run.sh`, `run_fixed.sh`, `timing.sh`, `framecost.py`, `riders.py`,
`replay/`, `compare*.txt`, `standoff*.txt`, `framecost.txt`). Builds `b3dan-build-20261003-b` (HEAD 47e682ba plus a
`CARRY_SAMPLE melee=` diagnostic) and `-c` (plus the brace fix and a `RESPAWN` diagnostic, below). All worlds seeded
(`--seed` = world number), 600 game s, the B3 arguments of `runcamp`, four at once except timing.

## The frozen world (item 3): two deadlocks, one per carrier

`whyfrozen.py` on `runcamp/off-4` (build j, 247 s frozen), then the game log, the play CSV and the console:

- **Red camper carrying, blue chase D on its head (444 s).** Camper2_v2 took the blue flag at 2:03 and stood at
  (2143, 3177) for 350 s; Chaser2_v2 stood 64 u above it (play CSV `ground` = the camper). The carrier's mover
  declined its first strafe edge every replan (`decline=corridor`, `cause=transient`: the body overhead), avoided it
  3 s, replanned the same edge, and held. Chase D's shot was held for its own splash (`pre=self`, flechette alt at
  68 u), so `Brace` leaned it into the burst point, which was straight below it: horizontal distance under 1 u,
  speed near 0, so Brace zeroed the move every frame and the movement's `step_off` (311 events) never got it off.
  Armed, it never meleed. The stall ended when chase D finally fired and killed the camper (9:33).
- **Blue runner carrying on its own stand, red chase D beside it (about 270 s).** Red chase D, 81 u away, unarmed
  (`weap=noweap`), the runner's saber up so no pistol (owner rule), and no `MELEE` line in 270 s.

**Chase D never melees.** Across every retained 4v4 world (`stalls`, `runcamp`) base D and the camper melee
thousands of times and chase D never once. The new `melee=` field shows chase D refused for `nosaber` while alive
(2,473 samples), and the `REARM` lines' weapon masks show every chase D with fists (`WP_MELEE`) and no saber. The
`RESPAWN` diagnostic: every chase D spawns with saber offense 0 (40 of 40 in one world), base and camper with 1.

Cause: `tools/fixtures/movement/defender/kejim_defender.jkb` had LF line endings (since 27a27e79, 2026-09-28; the
repo's `* text=auto` normalises). The game's `GetValueGroup` (ai_util.c) needs the `{` two characters after the group
name, which only CRLF gives, so the file "contains no GeneralBotInfo group" (printed at every add) and the bot takes
`DEFAULT_FORCEPOWERS` 5-1-000…: rank 5, saber offense 0 (so a spawn with fists, no saber), Force Jump 1. The
engine's bot cheat (w_force.c) still sets push and pull 3 and, light side, absorb 3, which is why the throws worked
and nothing looked wrong. Every chase D since 2026-09-28 ran this way, and base D too until it got its own
`kejim_base.jkb` (CRLF) on 2026-10-02.

## Fixes

- **`.gitattributes` `*.jkb text eol=crlf`**, and the file converted. Chase D now spawns with the declared
  `7-1-030330000000000110`: saber, Force Jump 3, push and pull 3 (and absorb 3 from the engine's light-side cheat).
- **`bot_defenderBraceOnBody` (default 0):** `Brace` does nothing while the defender stands on a player; the
  movement's step-off owns the move. 1 restores the old brace.

Replay check (Dantooine seed 12, which has a 7.8 s chase-D-on-carrier episode with `pre=self` and `BRACE_KNOCK`):
build c with `bot_defenderBraceOnBody 1` replays build b's world byte for byte (same game-log hash); with the fix
the world is identical up to that episode (2:23) and every later rider episode is 0.2 s or less.

## Dantooine 4v4 and the standoff (items 1 and 4), current defaults (build b)

`laps_compare.py`, 16 worlds a map, per 600 game s (standard error):

| | Kejim | Dantooine |
|---|---|---|
| captures, both teams | 7.62 (0.44) | 9.12 (0.50) |
| red / blue | 3.91 / 3.71 | 4.33 / 4.79 |
| enemy-flag pickups | 30.8 | 28.3 |
| falls, both runners | 10.78 | 4.32 |
| camper falls | 5.58 | 2.30 |
| runner weapon deaths, both | 30.4 | 33.2 |
| defender deaths | 23.9 | 20.8 |

Dantooine plays like Kejim with a third of the falls: +1.5 +- 0.7 captures (undecided at 16 worlds), red and blue
balanced on both maps. The Dantooine camper posts are placeholders (posts.json `camper_source`: the enemy base's
forward guard post, about 690 u from the stand).

`tools/laps_standoff.py` (new): holds per carrier, standoffs (both flags off their stands, taken or dropped) and how
each ends, frozen stretches.

| per world | Kejim | Dantooine |
|---|---|---|
| holds | 61.3 | 57.4 |
| hold length, median / p90 / max (s) | 8.5 / 22.1 / 56.9 | 9.6 / 22.5 / 58.0 |
| holds ending in a capture | 12% | 16% |
| holds ending in the carrier's weapon death / fall | 66% / 19% | 75% / 8% |
| time both flags off their stands | 47% | 40% |
| time both flags carried | 34% | 30% |
| standoffs | 30.2 | 24.4 |
| standoff length, median / p90 / max (s) | 7.0 / 19.0 / 60.2 | 6.8 / 19.8 / 75.0 |
| standoff broken by the losing carrier's weapon death / fall | 68% / 24% | 80% / 10% |
| freed team captures within 20 s | 29% | 36% |
| worlds frozen over 60 s | 0 of 16 | 0 of 16 |

A standoff is short and is broken by killing a carrier, almost always with a weapon; about a third of the breaks
convert into a capture. Kejim's pit breaks a quarter of them.

## The fixed arm (build c, both fixes), same seeds

| | Kejim | Kejim fixed | Dantooine | Dantooine fixed |
|---|---|---|---|---|
| captures, both teams | 7.62 | 8.81 (+1.19 +- 0.58) | 9.12 | 9.31 (+0.19 +- 0.65) |
| runner weapon deaths, both | 30.4 | 27.2 (-3.2 +- 1.1) | 33.2 | 29.2 (-4.0 +- 1.1) |
| falls, both runners | 10.8 | 11.4 | 4.3 | 4.9 |
| defender deaths | 23.9 | 23.8 | 20.8 | 20.9 |
| standoffs broken into a capture within 20 s | 29% | 37% | 36% | 41% |
| chase D melee entries (16 worlds) | 0 | 371 | 0 | 545 |
| rider episodes over 5 s | 0 | 0 | 1 | 0 |

Chase D now draws the saber and swings when unarmed beside a sabering target; its gun kills of runners fall
(Kejim flechette-alt splash 183 -> 152, Dantooine repeater alt 93 -> 55), and runners die about 3.5 fewer times a
world by weapons on both maps. Captures do not drop. One Dantooine world (seed 4) had a 171 s standoff: both
carriers on their own stands, the red runner attacking the blue carrier and dying, the red carrier guarded by its
base D, blue chase D at its post (chase D chases only a carrier it has seen; it never saw this pickup). No bot was
still; it is how a stand-off plays, not a stall.

## Frame cost (item 2)

`bot_moveFrameMeter 1` (each bot's command dispatch and pre-Pmove boundary, wall-clock), build b, one world at a
time, seeds 1-4 a map, 25x. Microseconds per game frame (frame = 25 ms):

| | Kejim | Dantooine |
|---|---|---|
| all eight bots, mean | 6,500 | 7,986 |
| all eight bots, median | 3,000 | 3,500 |
| all eight bots, world p99 | 41,000 to over 100,000 | 56,250 to 57,750 |
| frames over 25 ms | 4.8% | 7.8% |
| one bot, mean | 656 to 1,106 | 858 to 1,256 |
| one bot, world p99 | 3,750 to 34,500 | 16,750 to 24,250 |

The cost is spread evenly over the eight bots (no role dominates). The median frame fits easily; the tail does
not: one frame in 13 to 21 is over budget, where several bots' searches coincide (the multi-bot cost the 2026-09-22
row 5 record deferred and the 2026-09-28 two-bot measurement saw at one in 800). A 1x world (Kejim seed 1, 300 game
s, build c) checks the 25x figures: see the next section.

## Frame cost at real time

One Kejim world at 1x (seed 1, 300 game s, build c, alone; `timing-1x/`): all eight bots mean 8,491 us, median
3,750, p99 66,000, frames over 25 ms 6.9% (844 of 12,290), 15 frames over 200 ms (`[FRAME] slow`), the worst
716 ms. One bot: mean 765 to 1,984 us, p99 15,250 to 26,250. So the 25x figures are not an artifact of the rate: a
4v4 of these bots on one server misses the 25 ms frame about once in 15 frames, and a human on it would feel those
frames (a 200 ms frame is a visible hitch). This is the open cost item for B3; a per-frame search budget shared by
the bots (stagger, or a cap with deferral) is the obvious lever, for the owner to order.

## Open

- The frame-cost tail above (owner's call on a shared search budget).
- Chase D chases only a carrier it has seen; a carrier that took the flag unseen and holds on its own stand is
  chased by nobody (Dantooine seed 4, 171 s). Whether chase D should act on the HUD's flag status is the owner's
  call (knowledge-boundary rule).
- Dantooine camper posts are placeholders; captures there do not yet need better ones.

## Where the slow frames go (profile, build d)

`bot_moveFrameMeterSlow 25000` prints `[FRAMESLOW]` for every frame whose bots took 25 ms or more, with each bot's
microseconds, forecast Pmove steps and forecast traces (`bot2_meterSteps`/`bot2_meterTraces`); `slowframes.py`
joins them by level to the `[MOVE]` lines. One world a map, alone, 25x (`profile/`):

| | Kejim | Dantooine |
|---|---|---|
| slow frames | 1,922 | 2,669 |
| slow frame, median / p90 / max | 35.8 / 79.6 / 333 ms | 37.2 / 67.5 / 204 ms |
| one bot over 25 ms by itself | 69% | 69% |
| the top bot's share of a slow frame (median) | 85% | 86% |
| bots over 5 ms in the frame: 1 / 2 / 3+ | 1,065 / 681 / 176 | 1,467 / 948 / 254 |

**The hitch is one bot's search, not several bots at once.** The top bot is replanning (`plan:path`,
`plan:objective`, `plan:sequence`) or running a landing search (`edge:land`); a search runs about 45 forecast
steps a millisecond at about 11 traces a step, and the large ones are 2,000 to 3,500 steps (50 to 200 ms). A few
(56 on Kejim) are the graph state planner alone: about 200 ms with no rollout at all, long replans of 80 to 160
cells. Splitting the bots over threads would help only the third of slow frames where two or more bots coincide;
a search has to leave the frame it starts in.

## Proposal: searches off the game frame (owner question, "is it time to get an AI thread?")

Owner (2026-10-03): no loss of strafe or other bot quality, but the hitching must go.

The profile rules out the cheap answers: a per-frame search cap would cut or delay the very searches that make the
movement good, and one thread per bot fixes only the third of slow frames where searches coincide. The fix that
keeps every search whole is to let a search finish outside the frame it starts in:

1. **Search ahead, hand off on a fixed frame.** At a decision point the bot forecasts its own state K frames ahead
   (the forecast already reproduces its own commands exactly: parity mismatches are zero against a static world),
   starts the search from that state, and keeps executing its current plan for those K frames. On frame start+K the
   result is installed; if it is not done the frame waits for it. The search is the same search at the same depth;
   what changes is that it starts from where the bot will be, and interference in those K frames invalidates it the
   way it invalidates a commit today (a replan). Results land on a fixed game frame, so seeded replays stay exact.
2. **Run it on a worker thread.** The machine has 12 cores; a 200 ms search spread over K = 8 frames costs the game
   frame nothing.

What a worker needs that does not exist yet: the forecast's state per worker (it simulates through one static
client, `forecastClient`, with one active request and trace cache); traces that are safe off the main thread (the
engine's collision stamps every brush it tests with a shared `checkcount`, and entity traces use one global box
model, so a worker needs its own stamp and its own snapshot of bodies and movers); the graph planner's scratch
state per worker; and a snapshot of the game state a search reads. The engine part (collision) is a change to
OpenJK's `cm_trace.cpp`, which costs some portability to other derivatives.

Suggested order: (a) step 1 alone, on the main thread, to measure whether starting K frames ahead costs anything on
the movement benchmark and the laps worlds; (b) the worker, behind a cvar, with seeded replays equal between thread
and no thread; (c) more workers. The graph planner's own 200 ms replans (no rollouts) go through the same hand-off.
