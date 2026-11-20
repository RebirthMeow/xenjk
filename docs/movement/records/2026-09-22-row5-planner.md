# Row 5 on the seed: attribution, the planner, and a resumable finish phase

Recorded 2026-09-22. This is the first row-5 work after the owner made the
research seed the production graph ([seed gate](2026-09-22-seed-gate.md)).
In the same session the owner also accepted the respawn5 `jumppad` frames
under row 1 and removed the tape library's `link` rung. Every change here must
be decision-identical on the six reference cells:
- `red_out_L1_stand` at classes 75, 100, 125 and 150;
- `red_ret_L1_stand` and `blue_out_L1_stand` pinned at class 125.

All cells use the seed and the bound library, with the harness off.

> **1. Attribution.** The cost fixture now writes per-frame parts: state
> planner, cell planner, forecast rollouts and `RerouteFrom`, with call,
> expansion and step counts. These are accounting v3, trailing columns. On
> the seed, with four bots:
> - **Rollouts are 55 to 70 percent** of movement time at about 1.1 ms each.
> - **The state planner is 19 percent**: 3.7 to 3.8 ms a call, about 1,700
>   expansions at **2.2 us** each. On cache v3 it is 0.3 ms a call and 1
>   percent.
> - Expensive frames carry one bot's whole landing search: 50 to 100 rollouts
>   plus 10 to 19 reroute planner calls. At elapsed 50 ms all four bots run
>   their first search on the same frame (128 rollouts).
> - With the old search budget on, the seed's total rises by about 70
>   percent and the tail does not shrink (budget 2: p99 91 ms, worst 671 ms).
>
> **2. The planner, exactly.** The planner change and the memo both give
> identical decisions on the six cells, commit for commit, against each other
> and against the seed gate's worlds.
> - Changes: the cone cosine computed once, the strafe-edge scan's cheap
>   rejections first, and the slot table cleared by stamp instead of a 1 MiB
>   memset. Each expansion falls from 2.2 to **1.7 us**, and the planner
>   total per world from about 645 to 440 to 500 ms.
> - A `RerouteFrom` memo within one search saves about 10 percent of planner
>   calls. It is now unconditional.
> - The link-rung removal rides on the same builds.
>
> **3. The finish phase is resumable, and the tail is not moved by it.** The
> reroute and tap passes suspend between candidates. The three walk
> references and the ranking run in the last chunk.
> - Decisions are identical at budgets 0, 2 and 4 on every reference cell
>   (class 100 at budget 4 missed its pin in all eight attempts and is covered
>   at budget 2), and at budget 0 identical to the memo build.
> - The floor passes 36 of 36 with zero deaths on the final build.
> - But with four bots the frames over 25 ms stay at 37 to 42 at every budget.
>
> **4. Why.** New counters record the budget's outcome at each search. In a
> four-bot world:
> - only 5 (budget 2) or 14 (budget 4) of 46 landing searches are served from
>   a pre-search;
> - 16 to 27 find none ready;
> - 12 to 14 are refused for a reason other than state.
>
> The heavy frames that remain are of three kinds:
> - unserved landing searches of 20 to 60 rollouts;
> - several bots' chunks on one frame, whose reroute planner calls (about
>   7 ms each) the rollout budget does not count;
> - gap/edge searches of 15 to 21 rollouts, which run whole and outside the
>   budget.
>
> Row 5 is still not met.

## 1. Attribution

Build `row5-build-20260922-a`. It carries the link-rung removal and the v3
columns; nothing else changed. Worlds are under `.codex-research/row5-20260922-a/`
and are read by `analyse_cost.py` there.

| World | p95 | p99 | Worst | Planner | Rollouts | Planner per call | Expansions per call | us per expansion |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| K1-seed | 19.9 ms | 105.3 ms | 222.3 ms | 19 % | 55 % | 3.82 ms | 1,768 | 2.16 |
| K2-seed | 19.2 ms | 99.5 ms | 194.1 ms | 19 % | 54 % | 3.68 ms | 1,664 | 2.21 |
| K1-v3 | 33.4 ms | 55.9 ms | 102.7 ms | 1 % | 74 % | 0.30 ms | 669 | 0.44 |
| K2-v3 | 27.6 ms | 58.7 ms | 105.8 ms | 1 % | 69 % | 0.30 ms | 680 | 0.45 |

On the seed, each expansion scans nine cells' edge lists, on a graph with
140 times v3's strafe edges. That is why the planner matters there and not
on v3.

**The old search budget on the seed** (same build, `B2-seed`, `B6-seed`):
- total movement time 5.5 to 5.7 s against 3.3 to 3.4 s;
- p99 91 to 117 ms;
- worst 671 and 279 ms.

Its expensive flight frames are the pre-search's finish phase, which the
budget never divided. The 671 ms frame is the synchronized first search.

## 2. The planner, exactly

Build `row5-build-20260922-b`; worlds under `.codex-research/row5-20260922-c/`.
(The `-b` directory is one stopped world, recorded in its `STOPPED.txt`.)

**What changed** (`bot2_graph.c`, `bot2_move.c`):

- **The cone cosine** is computed once, by the expression the admission test
  used on every call, so the same float is compared.
- **`StateStrafeEdges`** keeps the same edges, the same order and the same
  conjunction. The tests are `Graph_StrafeAdmits`'s and `StateAvoided`'s,
  inlined, with the cheapest first. Every test is side-effect free, so the
  set relaxed and the heap's push order are unchanged.
- **The state search's slot table** is cleared by a stamp. The probe
  sequence and the entries are those of a zeroed table.
- **A `RerouteFrom` memo** is keyed on:
  - the landing state bit for bit, and the root's Speed time;
  - the path target and the replan ordinal;
  - the avoid list and `bot_moveState`;
  - `Graph_Generation()`, a new counter bumped by every graph change a
    planner can read.

  It is emptied at every search entry, bypassed under a planner directive,
  and stores only results the search wrote whole. `tools/test_movement_graph_reset.py`,
  which compiles `ResetLearned` from the source, now also checks that a reset
  bumps the generation. The suite passes, 887 tests.

**Identity**: `identity.py`, the seed gate's rule (committed sequences over
the leg, counters and `rollout_ms` dropped).

| Cell | Memo off | Memo on | Seed gate |
| --- | --- | --- | --- |
| c75 | 15,950, 11 commits | same | same |
| c100 | 16,725, 11 | same | same |
| c125 | 15,925, 11 | same | same |
| c150 | 15,975, 11 | same | same |
| `red_ret_L1_stand` c125 | 19,075, 13 | same | same |
| `blue_out_L1_stand` c125 | 20,400, 13 | same | same |

**Cost** (seed, four bots, alternated off/on):

| World | p95 | p99 | Worst | Planner total | Planner calls | us per expansion |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| K1 memo off | 28.4 ms | 63.9 ms | 201.5 ms | 501 ms | 136 | 1.71 |
| K1 memo on | 26.4 ms | 68.1 ms | 192.1 ms | 444 ms | 122 | 1.72 |
| K2 memo on | 19.1 ms | 86.8 ms | 193.7 ms | 396 ms | 129 | 1.72 |
| K2 memo off | 19.5 ms | 85.7 ms | 210.3 ms | 436 ms | 145 | 1.66 |

The planner is now 13 to 15 percent of movement time. The tail is unmoved,
because it is rollouts.

## 3. The finish phase, resumable

Build `row5-build-20260922-c`; worlds under `.codex-research/row5-20260922-d/`.

**What changed** (`StraightJudge`, `bot2_move.c`):
- The judge's phases are now candidates, reroute pass, tap pass, finish.
- The two passes suspend between candidates under `SearchBudgetSpent()`.
- The control walk, arm B's walk and the extension walk run together after
  both passes and before the ranking, in the one chunk that reads them. The
  shared walk buffers never cross a frame.
- A chunk's forecast contamination is carried into the next chunk, as the
  unsuspended judge would carry it.
- Every rollout is a pure function of its root and candidate: the guard
  restores the world, and the passes reset every global they set. Rolling
  the walks later therefore changes the rollout counter and nothing
  measured.

**Identity**:
- Budgets 0, 2 and 4 are identical on every cell, commit for commit,
  including the `traces` counter.
- Budget 0 on build c is identical to build b's memo arm on five cells. On
  `red_ret_L1_stand` only `traces` differs. Every candidate's and every
  reroute's measured fields are identical; the two worlds differ in their
  pre-release history (path generation 2 against 5), which is what the trace
  count follows.
- Missing pins were rerun on build d (counters only): class 150 at budgets 2
  and 4 are identical; class 100 at budget 4 did not land its pin in eight
  attempts.

**Floor**: build d, seed, library on, harness off, `--parallel 4`: **36 of
36**, zero deaths. Owners are `move`, plus `jumppad` on the respawn5 cases
(accepted by the owner).

**Cost** (seed, four bots):

| World | p95 | p99 | Worst | Over 25 ms | Total | Planner |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| K1 budget 0 | 21.4 ms | 91.9 ms | 176.0 ms | 37 | 3.13 s | 454 ms |
| K2 budget 0 | 23.1 ms | 87.0 ms | 175.6 ms | 37 | 3.09 s | 418 ms |
| K1 budget 2 | 22.5 ms | 74.8 ms | 197.2 ms | 39 | 4.76 s | 458 ms |
| K2 budget 2 | 23.3 ms | 64.6 ms | 190.9 ms | 39 | 4.64 s | 453 ms |
| K1 budget 4 | 27.7 ms | 85.5 ms | 183.4 ms | 42 | 4.97 s | 663 ms |
| K2 budget 4 | 26.8 ms | 59.9 ms | 182.3 ms | 42 | 4.84 s | 615 ms |

## 4. What stops the budget

Build `row5-build-20260922-d` adds five counters to the cost row: budget
searches, hits, not ready, refused at the state compare, and other refusals.
Worlds are under `.codex-research/row5-20260922-e/`.

| World | Searches | Served | Not ready | Refused, state | Refused, other |
| --- | ---: | ---: | ---: | ---: | ---: |
| K1 budget 2 | 46 | 5 | 27 | 2 | 12 |
| K1 budget 4 | 46 | 14 | 16 | 2 | 14 |

The frames over 25 ms at budget 4 (40 in all):
- 13 hold a search with no pre-search ready;
- 16 hold a refused one;
- 3 hold a served one;
- 11 hold no straight search at all:
  - seven are frames from 1,250 to 2,000 ms where two to four bots' chunks
    coincide, each chunk carrying reroute planner calls of about 7 ms that
    the rollout count does not see;
  - four are single-bot frames of 15 to 21 rollouts with no planner call,
    the gap/edge search, which runs whole outside the budget.

## What comes next for row 5

Decision-identical, in order:

1. **Split "refused, other"** into its causes (path generation, commit count,
   permit, tape cursor, candidates, validation), so the refusals can be
   fixed rather than guessed at.
2. **One server-wide pool per frame, not one per bot**, with a reroute call
   charged as rollout-equivalents. A frame's total is then bounded however
   many bots pre-search. The bots are served in client order, which is
   deterministic.
3. **A deadline, not a fixed rate.** A pre-search takes at least the work
   remaining divided by the frames left before its boundary, counted in
   rollouts and never read off a clock, so "not ready" becomes rare.
4. **The gap/edge search** under the same pool, or pre-searched as the
   straight search is.

Even served, a landing search that nothing predicted still runs whole. Row 5
at p99 needs at most eight such frames in 800, so the first search after a
spawn and the unpredictable landings bound what spreading alone can reach.

## 5. One bot (owner re-scope), and the deadline

The owner re-scoped row 5 to **one bot** and ranked strafe quality ahead of
multi-bot cost. Multi-threaded search is the recorded backup. The fixture
(`bot2_movecost.c`) and the runner (`--bots 1`) now accept one bot; the runner
also gained `--trace`, the fixture's buffered per-frame trace, which declares
the world's timings invalid.

**A start the fixture does not control.**
- About one no-telemetry world in five begins from an unsettled spawn pose:
  y = -704.000 and z = 1531.271, against about -700 and 1529.4 in every other
  world.
- From that pose the bot falls to `MOD_FALLING` at 7,250 ms. It is owned by
  `move`, and falling starts at 4,225 ms near (292, -650, 751).
- The death first looked like a budget effect, because it had landed only in
  budget-on arms. It is not:
  - a budget-0 world from the same pose dies too (`XB0-3`);
  - its trace matches the budget-2 death (`XB2-5`) frame for frame, all 800
    frames;
  - `XB0-2` against `XB2-6`, from a shared ordinary pose, also match on all
    800 frames.
- So the budget is decision-identical in the fixture as well, and there is a
  **spawn start that kills the bot**. That is a movement-safety item for the
  strafe-quality work, outside the benchmark's cases.
- Worlds: `.codex-research/row5-20260922-f/`.

**One bot, budget off and on** (build e; only worlds that live, which pick up
the flag at about 15 s; `.codex-research/row5-20260922-g/`):

| Budget | p99 | Worst | Frames over 25 ms | Served / searches |
| --- | ---: | ---: | ---: | ---: |
| 0 | 38.2 to 39.1 ms | 153 to 165 ms | 12 to 14 | - |
| 2 | 13.2 to 13.5 ms | 157 ms (and one 1.76 s host stall at the first frame) | 8 | 8 / 16 |
| 4 | 13.6 to 14.0 ms | 88 to 161 ms | 3 to 4 | 10 / 13 to 14 |

**The deadline** (build f, `SearchChunkBudget` in `bot2_move.c`):
- A chunk may spend at least the pre-search's remaining work divided by the
  frames left before its boundary.
- The work is an upper bound in rollouts read off the judge's resume point:
  each unjudged candidate counts three, each judged off-path candidate two,
  plus the three walks. The frames are level time. Nothing reads a clock.
- Identity: all six cells at budget 4 are identical to budget 0, commit for
  commit.
- Floor at budget 4: **36 of 36**, zero deaths.

| Build f, one bot | p99 | Worst | Frames over 25 ms | Served / searches |
| --- | ---: | ---: | ---: | ---: |
| budget 0, world 1 | 35.9 ms | 172.3 ms | 12 | - |
| budget 0, world 2 | 39.8 ms | 100.9 ms | 13 | - |
| budget 4, world 1 | **12.5 ms** | 164.0 ms | **3** | 12 / 14 |
| budget 4, world 2 | **13.9 ms** | 88.5 ms | **3** | 11 / 13 |

What still exceeds the frame at budget 4, per world:
- the first search at spawn, 43 to 50 ms, which no flight precedes;
- the search after the fixture's declared 2.5 s velocity impulse, 88 to
  164 ms, which the impulse makes unpredictable;
- at most one chunk of 26 to 30 ms.

**Tried and reverted.** Charging a planner call as three rollouts in the
budget (build g) was decision-identical on the six cells, but not better:
p99 14.7 to 15.6 ms, three to four frames over 25 ms. It left one more search
unready. The sources are back at build f.

**Where row 5 stands, one bot:**
- p99 is **12.5 to 13.9 ms**, inside the 25 ms frame, with the search budget
  at 4.
- The budget is still default off, so this is not yet the production
  configuration.
- Three frames in 800 exceed the frame. Two of them are searches nothing
  could predict.
- Whether the row is met depends on its criterion, which is the owner's call:
  p99 inside the frame, or every frame.

## What this does not establish

- **That row 5 can be met by spreading alone** with four bots. Section 4 names what remains; the owner has since re-scoped the row to one bot (section 5).
- **Timing claims on the telemetry world** (`T-b2-telemetry`); it printed no
  tallies, because no leg ended inside the 20 s window.
- **Anything off the seed.** The v3 comparison in section 1 is attribution
  only.
