# The search budget: the same decisions, spread over the flight, and no cheaper

Recorded 2026-09-22. The [level-1 exit gate](../../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21)
row 5 measurement of the
[graph-binding record](2026-09-22-graph-binding-and-decoupling.md) left the
straight search running to completion on the frame that triggers it, with no
budget of any kind. This work gives it one - `bot_moveSearchBudget`, default
off - by moving the rollouts onto the frames before the boundary instead of
deferring them past it, and measures what that buys.

> **The decisions are identical on all four declared cells, and that is the
> result worth keeping.** On fresh build `search-budget-build-20260922-c`
> (1,117 source hashes matched before the first world and again after the
> last), with the budget on: the two **seeded** control cells with the parity
> harness armed return **15,925 ms / 13 straight / 5 state** and **21,125 ms /
> 16 / 8**, and the two **production** cells with the harness off return
> **19,050 ms** and **19,150 ms** - each the same completion time, the same
> commit counts and the **same leg decision sequence in the same order** as the
> same cell on the same build with the budget off (13 of 13, 16 of 16, 14 of 14
> and 14 of 14 decisions, compared on kind, hop count, both landing cells, both
> graph edge indices and the candidate and admitted counts of the search that
> took them).
>
> **The mechanism is taken most of the time and refused on nothing.** Over the
> 36-case floor and the two seeded regressions - 38 legs, budget on - **492
> straight searches, 328 of them served from a candidate set judged in flight
> (66.7 percent)**, one refused because the path had been replanned, and
> **zero** refused on the state compare, the executor compare, the candidate
> compare or the validating rollout. 163 searches ran at a frame no prediction
> had been built for and paid for themselves, exactly as they do today.
>
> **The floor holds.** 36 of 36 with the budget on, production cache, library
> off, harness off: zero deaths, the movement owner on every frame of every
> case, no forbidden spend. Both seeded regression cases pass.
>
> **It does not meet row 5, and it does not move the tail.** Two paired cost
> worlds, four bots, the production configuration, the arms alternated across
> sequential runs: p50 rises from **117 / 112 us** to **3,028 / 1,414 us** -
> the work being spread, which is what it is for - while p95 goes **71,657 /
> 90,012** to **104,478 / 80,701**, p99 **226,991 / 182,785** to **244,111 /
> 242,457**, the worst frame **2.41 / 0.56 s** to **3.42 / 1.72 s**, frames
> over the declared 5,000 us budget **126 / 123** to **364 / 337** of 800, and
> frames over the whole 25,000 us server frame **70 / 81** to **100 / 95**.
> Total Pmove-boundary work rises **1.6 to 2.1 times**. The budget is left
> **default off**.
>
> **Why it cannot work in this form, measured rather than argued.** The
> candidate rollouts are not the tail. In the paired bench worlds the budget
> raises the per-leg `[SPIKE]` count only from **339 to 349** at class 125 and
> **350 to 371** at class 75, and leaves the `[REROUTE]` count at **37 against
> 35** and **34 against 33**, so it moves that work rather than multiplying it -
> and the tail does not move with it. The frames that break the server frame
> break it in both arms, and the budget-off control has a 2.41 s frame of its
> own, so what costs 100 ms to 2 s at that boundary is not the candidate
> rollout loop, which is the only thing a rollout budget can count.
>
> **And row 5's starting point is worse than the smoke said.** The smoke ran
> with no `--graph`, on the 10-edge graph the map builds at load. On the
> **production cache** the same measurement with the budget off is p95
> **71,657-90,012 us** against the smoke's 36,800 and p99 **182,785-226,991**
> against 96,870. The configuration the plan declares is about twice as
> expensive as the number row 5 has been carrying.

## What was asked

Give the Pmove-boundary search a budget that cannot change decisions: search
during the flight from the forecast's predicted landing state, spread the
rollouts across the flight's frames, and at the actual landing validate the
chosen candidate from the actual state with one rollout and commit it, falling
back to the whole search whenever the actual state is not the predicted one.
Grounded retry searches get the same treatment. All of it behind one
default-off cvar. Then: decision identity on four named cells, the cost pair,
the floor, and this record.

## The cvar, and why a rollout count

`bot_moveSearchBudget`. **0 is today's behaviour exactly** - every path the
budget adds returns before it does anything, so every earlier cell replays
unchanged, which the V and W phases below demonstrate on this very build. A
positive value is **the number of candidate rollouts one bot may issue on one
server frame while it pre-searches the boundary ahead of it**. The campaigns
ran it at **2**.

A rollout count rather than a microsecond budget, for two reasons:

- **A microsecond budget is not deterministic.** It would split a search at a
  point that depends on the machine and the moment, so the same world would
  divide its work differently from run to run and the fall-back would fire
  non-deterministically. Decision identity is the whole requirement here, and
  it has to be checkable rather than hoped for; a rollout count makes the split
  a property of the search and not of the host.
- **This project has already withdrawn a wall-clock reading of the search.**
  The forecast bounds a rollout by step counts and not by a clock
  ([LESSONS](../LESSONS.md)), and reading a clock where the search runs is the
  same class of intrusion as printing there, which cost 2,000 ms once.

## How it works

Five pieces, all in `codemp/game/bot2_move.c`.

**1. The boundary is predicted, once.** `SearchNextBoundary` says where the
next straight search will run: the landing of a committed sequence (the frame
`StraightLanding` will be true at), or the retry a failed search scheduled with
`m->straight.searchTime` and the lookahead cadence. `SearchPredict` builds the
horizon to it with the harness's own `RolloutRequest` into its own buffer, and
keeps that boundary's frame - the player state before its Pmove, the executor
before and after the command, the command itself with its native time, and the
preceding animation. **One prediction per boundary**: a prediction is a horizon
rollout, and the first version of this work re-took it on every frame for a
boundary that had not moved, which alone was most of its overhead.

**2. The pre-search runs from that state.** `SearchChunk` installs the
predicted boundary on the bot - player state, executor pair, submitted command,
animation, `level.time`, `level.previousTime`, `level.framenum` - runs a chunk
of the ordinary search, and puts the live state back on every path out,
together with everything the search writes outside its own result storage
(`movement[client]` and the parity harness's arming). Nothing else in the
search had to learn about this: it is the same function, reading the same
fields, from a state that has been made the bot's.

**3. The judge suspends between candidates and nowhere else.** `StraightJudge`
now carries a `judgeState_t` with its resume point and the locals that must
survive it, and returns whether it finished. With the budget off the caller
hands it a zeroed stack local, `searchRolloutBudget` is zero, every loop runs
to its end and it returns 1 on its first call. Under the budget the candidate
loop may stop between two candidates - the only place a suspension is safe,
because the walk references are written in the finish phase and read at the end
of it, and a second client's search would overwrite them across a frame
boundary.

**4. The boundary takes it, or does not.** `SearchCacheTake` runs after the
frame has built its own candidates, so the construction is the check. It
refuses unless: the budget is on; a pre-search is ready **for this exact
frame**; the path generation, cell count, objective, commit count, permit and
tape cursor are unchanged; the candidate count matches; the live player state
matches the predicted one on **the fields the parity harness compares**
(position, velocity, ground contact, the view and animation fields, the Force
pool and its clocks); the executor and the executor it started from match byte
for byte, which is where the landing-frame **tap bit** lives; every constructed
candidate plan matches the one construction built in flight; and the winner's
own last rollout, re-run **from the actual state**, returns the same outcome
and the same measurement. Any one of those refuses the cache and the frame does
its own search, which is decision-identical because it is today's code.

**5. A leg tally, printed at the leg boundary.** Counted in state and printed
once where the parity tally prints, never where it happens.

## The build

`.codex-research/search-budget-build-20260922-c`, manifest
`20260922T132900987439Z.sources.json` sha256
`433802327bd373505f5d033f78994dc55ffcd4238212c00bf3c7635c38982c63`,
`openjkded.x86.exe` sha256
`5d850df20702774d5b406d1183494ecddf024aee4b6b18840c5078218ad465bd`,
`jampgamex86.dll` sha256
`a69df557f65fdc5fdcda49e94c43262c9d4adc2409d97501c6f7555d08e96328`. **All
1,117 source hashes matched the working tree before the first world and again
after the last** (`verify_sources.py`, `manifest-before.json`,
`manifest-after.json`).

Worlds under `.codex-research/search-budget-20260922-c/`, every one retained,
one world per bench call, sequential, launched detached from a script
(`run.sh` with `run.py`, `progress.jsonl`, `campaign.out.txt`,
`campaign.done`; `cost.sh` and `cost2.sh` for the four cost worlds). The floor
is the one call at `--parallel 4`, as every earlier floor record ran it. Every
bench world runs the decision-tape record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs) and
`--edges`; the only thing that varies inside a pair is
`bot_moveSearchBudget`.

`analyse.py` reads every retained world and writes `analysis.txt` and
`analysis.json` beside them; the identity verdict in it is computed decision by
decision, not eyeballed.

Two earlier builds of this same work are retained and are **not** read as
results: `search-budget-build-20260922-a` with its campaign `-a`, whose cache
compared the live path index against the index the prediction was stored at -
the executor advances it through the flight, so almost every take was refused
and the mechanism was measured at 1 to 2 hits a leg; and
`search-budget-build-20260922-b` with campaign `-b`, which is this engine
without the prediction rate limit. Both reproduce the identity result; `-b`'s
cost pair is the one that says the rate limit was worth taking and did not
change the verdict.

## What changed

`codemp/game/bot2_move.c` alone on the engine side: the budget helpers, the
per-client pre-search cache, `SearchNextBoundary`, `SearchPredict`,
`SearchChunk`, `SearchCacheTake`, `SearchCacheValidate`, `SearchBudgetStep` and
`SearchBudgetReset`; `StraightJudge` gaining its `judgeState_t` and a return
value; `StraightSearch` becoming a wrapper over `StraightSearchCore`; one call
at the Pmove boundary and one at `Move_Reset`. On the tooling side
`tools/movement_cost_runner.py` gains `--search-budget` (default 0, so its own
default is the configuration every earlier cost world ran), covered by
`tools/test_movement_cost_runner.py`, and
`tools/test_movement_search_budget.py` is a new regression on the default-off
contract read out of the shipped C source.

## 1. Identity, cell by cell

Phases V and W are the budget-off arm and the build verification; X and Y are
the same cells with `bot_moveSearchBudget=2`. `red_out_L1_stand`, pinned with
`--corner-class`, one world per bench call.

| Cell | Graph | Harness | Budget off | Budget on | Decisions |
| --- | --- | --- | ---: | ---: | --- |
| seeded class 125 | 2026-09-16 seed | `--parity 256` | **15,925 ms** / 13 / 5 | **15,925 ms** / 13 / 5 | **13 of 13 identical** |
| seeded class 75 | 2026-09-16 seed | `--parity 256` | **21,125 ms** / 16 / 8 | **21,125 ms** / 16 / 8 | **16 of 16 identical** |
| production class 125 | production cache | off | **19,050 ms** / 14 / 4 | **19,050 ms** / 14 / 4 | **14 of 14 identical** |
| production class 75 | production cache | off | **19,150 ms** / 14 / 5 | **19,150 ms** / 14 / 5 | **14 of 14 identical** |

A decision is compared as the commit line records it, measured from the
release rather than from the map clock, because two worlds of one start class
release at different absolute levels: the offset from the release, the plan
kind, the hop count, both landing cells, both graph edge indices, and the
candidate and admitted counts of the search that took it. No cell differs at
any decision, so the acceptance's "report exactly the first differing decision"
has nothing to report.

The seeded cells reproduce their declared build-verification figures for time
(**15,925** and **21,125 ms**) with the budget off. One observation that is not
this work's: the class-75 world returns **16 straight and 8 state** commits
here, against the 15 and 7 the standing rule quotes and against the 15 and 7
the same cell returned on build `-b` at the same release level of 3,875. Both
arms of this build agree with each other exactly, and the time is the declared
one; the commit count is not a function of the start class and the release
level alone even on the seeded graph. That is recorded, not resolved.

What the budget did in each of those worlds, from its own leg tally:

| Cell | Searches | Predictions | Pre-searches ready | Taken | Refused | Not ready |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| seeded class 125 | 17 | 17 | 10 | **10** | 0 | 7 |
| seeded class 75 | 30 | 34 | 15 | **14** | **1** (`measure`) | 15 |
| production class 125 | 15 | 17 | 13 | **12** | 0 | 3 |
| production class 75 | 16 | 16 | 12 | **12** | 0 | 4 |

The single refusal is the design working: at one class-75 boundary the
validating rollout from the actual state did not reproduce the measurement the
pre-search had recorded, the cache was refused, that frame ran the whole search
itself, and the cell still completed at 21,125 ms with the same 16 decisions.

## 2. Coverage over the floor

38 legs - the 36 floor cases and the two seeded regression cases, budget on -
aggregated over their leg tallies:

| | |
| --- | ---: |
| straight searches | **492** |
| served from an in-flight pre-search | **328** (66.7 %) |
| refused: path replanned under the prediction | **1** |
| refused: state, executor, candidates or validation | **0** |
| ran at a frame with no prediction ready | 163 |
| pre-searches abandoned when the boundary moved | 6 |
| predictions built | 585, of which 39 refused by the forecast and 26 found no boundary |

Two thirds of the leg's searches are decided from work done while the bot was
in the air, and the parity-style state compare has not once disagreed with the
forecast in 329 comparisons at real landings. That is the same thing the
harness has been saying about single frames, said about a boundary tens of
frames ahead.

## 3. Cost

`tools/movement_cost_runner.py`, four bots on one dedicated localhost server,
the production configuration **with the production graph cache**, telemetry
off, the fixture's own 800-frame / 20,000 ms window, `--search-budget 0` or
`2` and nothing else different. Two pairs, the arms alternated across
sequential runs because runs inside one batch are not independent samples.

| | off, pair 1 | on, pair 1 | off, pair 2 | on, pair 2 |
| --- | ---: | ---: | ---: | ---: |
| p50 us | 116.9 | **3,028.3** | 111.9 | **1,414.1** |
| p95 us | 71,656.9 | **104,478.1** | 90,011.8 | **80,700.6** |
| p99 us | 226,990.5 | **244,111.4** | 182,785.3 | **242,457.1** |
| worst frame us | 2,411,868.8 | **3,423,038.5** | 555,452.7 | **1,721,149.8** |
| mean us | 14,396.2 | 22,036.0 | 11,439.9 | 19,046.5 |
| frames over 5,000 us | 126 | **364** | 123 | **337** |
| frames over 25,000 us | 70 | **100** | 81 | **95** |
| Pmove-boundary total us | 8,153,549 | **17,213,737** | 8,277,825 | **13,513,091** |

Against the graph-binding smoke's p50 67.5, p95 36,800, p99 96,870, worst
272,692 and 114 of 800 over budget: **the budget-off control on the production
cache is already about twice the smoke at p95 and p99 and an order of magnitude
worse at the worst frame.** The smoke had no `--graph` and ran on the 10-edge
graph the map builds at load; the production cache has 676. Row 5's real
starting point is the left-hand columns here, not the smoke.

And against its own control, the budget **spreads the work and adds to it**.
p50 up by a factor of 12 to 27 is the design: frames that used to do nothing
now do two rollouts. Everything else is worse or inside the noise, and the
total at the boundary is 1.6 to 2.1 times the control's.

### Why the tail does not move

The unit is wrong for the thing that breaks the frame. From the paired bench
worlds at production class 125, per leg:

| | class 125 off | class 125 on | class 75 off | class 75 on |
| --- | ---: | ---: | ---: | ---: |
| `[SPIKE]` candidate lines | 339 | 349 | 350 | 371 |
| `[REROUTE]` lines | 37 | 35 | 34 | 33 |

The pre-search does not multiply the candidate rollouts - it moves them, at a
3 to 6 percent premium for the ones that are thrown away - so a **rollout** budget
has bounded almost exactly the work it was pointed at, and the frames that
exceed the server frame still exceed it. They exceed it in the control too:
the budget-off arm has a 2.41 s frame of its own. Whatever costs 100 ms to 2 s
at that boundary on the production cache is not the candidate rollout loop, and
nothing a per-rollout budget can count will divide it. Naming it is the next
measurement, and the obvious suspect is the planner call the reroute pass makes
per candidate (`Graph_PlanState` over 14,249 cells and 676 edges), which the
rollout counter does not see at all.

## What this establishes

- An in-flight pre-search from the forecast's predicted landing **can** be made
  decision-identical: four cells, two graphs, harness on and off, every
  decision, with 329 state comparisons at real boundaries and no disagreement.
  The claim the design rested on - that in a static world the prediction is the
  state - holds at a boundary tens of frames ahead and not only at the next
  frame.
- The fall-back is real and is exercised: one validation disagreement and one
  replanned path, both refused, both completing identically.
- The floor is unaffected: 36 of 36, zero deaths, movement owner only, no
  forbidden spend, plus both seeded regressions.

## What this does not establish

- **It does not meet row 5 and does not improve it.** The cvar stays default
  off. Nothing here is a reason to turn it on, and the honest reading is that
  the design was aimed at a cost the measurement now says is somewhere else.
- One pair per arm per ordering is two samples; the worst frames differ by a
  factor of four between two control runs, so only the direction and the size
  of the p50 and total changes are being claimed, not the exact percentiles.
- The suspension point is the candidate loop alone. The finish phase - the
  three walk references and the reroute pass - runs whole in one frame, because
  the walk buffers are shared statics that a second client's search would
  overwrite across a frame boundary. Bounding it needs per-pre-search walk
  buffers, which was not built and was not what the cost measurement pointed
  at.
- The diagnostic `[SPIKE]`, `[REROUTE]` and `[STRAIGHTSLOT]` lines of a taken
  search are printed while the bot is in the air rather than at the boundary,
  and carry the flight's `level=` and the rollout counter's `seq=`. That is a
  print, not behaviour, and the cells are identical with it.
- Cost is measured on one map, one workload, one bot count and one budget
  value. No sweep of `bot_moveSearchBudget` was run.
