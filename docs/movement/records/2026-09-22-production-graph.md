# The production graph cache, and the release that stops the harness deciding

Recorded 2026-09-22. The [graph-binding record](2026-09-22-graph-binding-and-decoupling.md)
left one open item: *an unseeded stand run is not a cell*. Ten worlds pinned to
one release level returned three different completion times, so the explorer's
forced-index branching, tape replay and the gate's per-class cells had nothing
deterministic to stand on. This work finds the cause, removes it, and prepares a
production graph from the bot's own runs rather than from the 2026-09-16
research seed.

> **Pre-release learning was the whole cause, and it is now the whole fix.** A
> fixture world does not begin at the release: the bench brings a world up, the
> bot's own AI plays from its spawn, and only when the world reaches the pinned
> level does `bot_corner_start` place it at the case's pose. Every strafe edge
> the search commits during that free play is learned into the graph. On the ten
> retained unseeded worlds pinned at level 3,125, the pre-release learned set
> **predicts the trip exactly**: the five worlds that learned cell 47 to 4949
> all returned **19,700 ms** with the same 12 decisions in the same order, the
> four that learned nothing or cell 61 to 6167 all returned **16,725 ms** with
> the same decisions, and the one that learned cell 50 to 4948 returned
> **18,475 ms**. Both parity arms sit inside those groups, so the harness is not
> the variable. The mechanism is visible in the first post-release plan: with
> the 47-to-4949 edge the state planner's route from cell 0 is 206 cells with
> **one strafe edge** on it, without it 215 cells and **none**, and the first
> straight search then proposes from a different place - 15 candidates, 10
> admitted at level 3,575 against 15 and 3 at 4,500.
>
> **The release now puts the learned set back to the graph as loaded.**
> `Graph_ResetLearned`, called from `bot_corner_start` and from nothing else:
> appended edges are dropped, and loaded edges that a merge, a cost or a result
> mutated are restored from a copy taken before the first mutation. A real spawn
> has no release, so production is unchanged - there is nothing there to reset.
>
> **A production graph cache built from the bot's own runs.** The union of the
> learned edges of the gate campaign's **unseeded** arms - the 72 row-4 worlds
> and the two 36-case floors, 144 world caches, 3,736 edge records folded to
> **676** - on top of the map-load graph: 14,249 cells, 1,398 nodes, **676
> edges** (650 strafe, 16 hop, 10 pad), identity **`g1-0e5af7aba9b5e908`**.
> Against the map-load graph's 10 edges and the research seed's 94,711.
>
> **With that cache, a start class is a cell again.** Harness off, library off,
> sequential, on fresh build `production-graph-build-20260922-a`: **ten** worlds
> at class 125 return **19,050 ms** and **one** leg decision sequence of 12
> decisions, from release levels 3,125, 3,325 and 3,925; **six** worlds at class
> 75 return **19,150 ms** and one sequence of 13 decisions, from release levels
> 3,075, 3,675 and 4,675. Three of the sixteen worlds learned an edge before
> their release and the reset dropped it; their legs are identical to the
> worlds that learned none.
>
> **The floor holds on the production configuration.** 36 of 36 with the
> production cache, library off, harness off: zero deaths, the movement owner on
> every frame of every case, no forbidden spend. **Both respawn cases that died
> under the research seed pass** - `red_ret_L1_respawn4` at 16,300 ms and
> `blue_out_L1_respawn4` at 16,150 - and both seeded regression cases pass on
> the production cache as well.
>
> **The build is verified, and the reset does not disturb the seeded cells.**
> Both seeded control cells reproduce to the commit with the harness on:
> **15,925 ms / 13 straight / 5 state** and **21,125 / 15 / 7**. On the seeded
> graph pre-release learning only *merges*, never appends, so the reset dropped
> nothing there and restored at most two edges.
>
> 1,117 source hashes matched the working tree before the first world and again
> after the last. 26 bench calls, 589.5 seconds of bench time, every world
> retained.

## What was asked

Diagnose precisely why one unseeded start class returns three different
completion times; prepare a production graph cache from the bot's own runs with
`tools/movement_graph_merge.py` and report its size and identity; stop the
fixture's pre-release hold from changing the leg, preferring a reset at the
release over suspending learning over a cvar, with a regression and production
unchanged; then show determinism per class, and the floor and both seeded
regressions on the production configuration.

## 1. The diagnosis

Read-only over the retained worlds of
[the graph-binding campaign](2026-09-22-graph-binding-and-decoupling.md)
(`.codex-research/graph-binding-20260922-b/`) and of
[the gate campaign](2026-09-22-gate-l1.md) (`.codex-research/gate-l1-20260922-a/`).

Ten worlds there completed `red_out_L1_stand` unseeded with the corner start
landing on level **3,125**: eight of them pinned to that exact level (`R-*` and
`X-*`, four per parity arm) and the two class-125 worlds of phase `U`, which
both happened to release there. For each one, the learned edges printed before
the release (`[MOVE] reason=learn` with `level <= 3125`) and the committed leg
after it:

| Pre-release learned set | Worlds | Completed | Leg decisions |
| --- | --- | ---: | ---: |
| none | `R-l3125-noparity-r1`, `R-l3125-parity-r4-a2` | **16,725 ms** | identical |
| cell 61 to 6167 | `R-l3125-noparity-r4`, `U-c125-noparity` | **16,725 ms** | identical to the row above |
| cell 47 to 4949 | `R-l3125-noparity-r2`, `R-l3125-parity-r1`, `R-l3125-parity-r2`, `X-l3125-noparity`, `X-l3125-parity` | **19,700 ms** | 12 of 12 identical |
| cell 50 to 4948 | `U-c125-parity` | **18,475 ms** | - |

**Worlds with the same pre-release learned set ran the same trip**, decision for
decision, at the same levels, with the same candidate and admitted counts -
across both parity arms, which is the control that says the harness's own
arming is not the variable. Worlds with different sets diverged at **decision
0**. That is the confirmation the question asked for.

The mapping is not injective, and that is informative rather than a problem: the
61-to-6167 edge is learned and then never used, because it is not on any route
the planner finds from cell 0, so its worlds are identical to the worlds that
learned nothing. What matters is not that an edge was learned but whether it
enters a route.

### The mechanism

The first post-release plan, `[MOVE] event=plan reason=path` at level 3,150,
cell 0 to cell 1:

| Pre-release set | cells | edges on the route | strafe | planner cost |
| --- | ---: | ---: | ---: | ---: |
| none, or 61 to 6167 | 215 | **0** | 0 | 30,622 ms |
| 47 to 4949 | 206 | **1** | 1 | 30,450 ms |
| 50 to 4948 | 206 | **1** | 1 | 30,420 ms |

A learned strafe edge joins `Graph_PlanState`'s route; the route is what the
walk steers along; and the straight search proposes from wherever that steering
puts the bot. The first commit is then a different commit at a different level:

    none / 61->6167   L3575 straight hops=2 c0=887  c1=8495 cand=15 adm=10
    47->4949          L4500 straight hops=1 c0=3098 c1=-1   cand=15 adm=3
    50->4948          L4700 state    hops=1 c0=4333 c1=-1   cand=15 adm=2  index=10

In the 19,700 group the learned edge is **never committed** - `index=-1` on
every sequence line of the leg - so it changed the route and not the maneuver.
In the 18,475 world it is committed as well: the first decision is a
`kind=state` plan whose edge is index 10, the pre-release edge itself. Both
routes are how one edge reaches the outcome.

### A second confirmation, from the gate campaign

The gate's six unseeded `red_out_L1_stand` worlds are **unpinned**, so their
release levels differ and the start level alone already decides the leg
([start level](2026-09-20-start-level-determines-the-leg.md)). Two of them match
on **both** axes - release level 2,625 and an empty pre-release set -
`u-red_out_L1_stand-lib-r3` and `u-red_out_L1_stand-off-r3`, one in each arm.
Both return **18,400 ms**. A third world with an empty set at level 2,650
returns 17,650, which is the start level moving and not the graph.

## 2. The production cache

Built with `tools/movement_graph_merge.py` from the bot's own runs, which is
what "automatic preparation" means in the owner's standing direction: no
synthetic corridor states, no offline explorer seed, nothing hand-authored.

Inputs, all from `.codex-research/gate-l1-20260922-a`, **unseeded arms only**:
the 72 `u-*` row-4 worlds (twelve level-1 cases, three repetitions, library and
control) and the two 36-case floors `floor-off` and `floor-on`. The seeded arms
are excluded: their edges are indices into the 2026-09-16 seed's graph and
belong to it. The merge tool reads `<campaign>/<case>/base/bot2-graph/`, and the
gate campaign is a directory of bench outputs rather than one, so
`stage_caches.py` hard-links each world's cache into one flat tree and writes
every input's sha256 to `cache-inputs.json`. **144 caches from 74 worlds, 3,736
edge records**, folded by the tool the way the game folds them - one edge per
launch cell, landing cell, tap flag and entry-speed band, the lowest cost kept.

| | cells | nodes | edges | hop | pad | strafe | identity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| built at map load, no cache | 14,249 | 1,398 | **10** | 0 | 10 | 0 | `g1-301dd39cbead57f6` |
| **the production cache** | 14,249 | 1,398 | **676** | 16 | 10 | **650** | **`g1-0e5af7aba9b5e908`** |
| the 2026-09-16 research seed | 14,249 | 1,398 | 94,711 | - | - | - | `g1-98eb1e97c24a4e21` |

`.codex-research/production-graph-20260922-a/production-graph.txt`, sha256
`c1c0765ff184f53d1f9c10fc7e9ffe107071948ac02af3ac55a33c5c7f59a92a`, 895,575
bytes. The identity is the engine's own, read from the `[GRAPH] map_load` line
of every world that loaded it (`cached=1`, `edges=676`), not computed by a tool.

## 3. Why the release resets, and not the other two options

The task named three mechanisms in order of preference. The first is the one
that works, and the second turns out to be impossible without the third.

**(a) The corner release resets the learned set to the loaded cache.** Adopted.
`bot_corner_start` is a one-shot `sv_cheats`, dedicated, movement-only,
CTF-gated server command that the fixture sends once per world, and it already
resets exactly this kind of state: it clears the bot's movement state
(`Bot2_ClearState`), arms the route recorder at the release level, and resets
the decision tape's cursor so *"the bot's own pre-release wandering cannot
consume entries"*. The learned set is the same problem one layer down, and the
same place is the right place for it. **Production is unchanged because
production never calls it**: a real spawn has no release, so there is nothing to
reset, and the reset is unreachable outside the fixture's own command. A
regression asserts that - `bot2_corner.c` is the only caller outside
`bot2_graph.c` itself.

**(b) Suspend learning while the fixture holds the bot.** Not possible without
(c). There is no hold: the engine is not told that a world is a fixture world
and is not told that a release is coming. `bot_corner_start` arrives
asynchronously, at a level the bench picks by watching the play log, and until
it arrives the world is indistinguishable from a production world with one bot
in it. To suspend learning in advance the harness would have to declare the
hold, and a declaration is a cvar - which is (c), the option the task asks to
avoid. (a) needs no declaration because it acts at the moment the release
happens, which the engine does learn about.

**(c) A cvar.** Not added.

### What the reset does

`Graph_ResetLearned` in `bot2_graph.c`:

- **Appended edges are dropped** by truncating the array to the count it held at
  load. Appending never moves an existing index, which is the same property the
  graph identity relies on, so truncation is exact.
- **Mutated loaded edges are restored.** A loaded edge is mutated in three
  places - a strafe merge (`Graph_AddStrafeEdge`, which rewrites the takeoff,
  landing, heading, cost and state of the edge it folds into), `Graph_EdgeCost`
  and `Graph_EdgeResult` - and each now calls `TouchEdge` first, which copies
  the whole record once, before the first mutation. Copy-on-write rather than a
  snapshot of the array, because a seeded graph has 94,711 edges and a world
  touches a handful; the copy table holds 256 and reports its own overflow. It
  overflowed in none of the 56 worlds here.
- **The per-cell chains are rebuilt** forward, each edge prepended, which is
  exactly how the adds built them, so the chain order is the loaded graph's own
  and not merely a set-equal one.
- The identity is **not** recomputed. It is the graph as loaded, which is what
  the reset restores.

Every reset prints its own line, which lands in the `bot_corner_start` rcon
reply rather than in `server.log` (the project's standing note that rcon output
bypasses the log), so it is retained in each case's `result.json`:

    [GRAPH] reset_learned dropped=1 restored=0 overflow=0 edges=676
      graph=g1-0e5af7aba9b5e908

| Phase | Worlds | dropped | restored | overflow |
| --- | ---: | ---: | ---: | ---: |
| `V`, seeded control cells | 2 | 0 | 0 to 2 | 0 |
| `P`, determinism | 16 | 0 to 1 | 0 to 1 | 0 |
| `F`, the floor | 36 | 0 to 1 | 0 to 1 | 0 |
| `G`, seeded regression cases | 2 | 0 | 0 | 0 |

The seeded rows are the interesting ones: on a 94,711-edge graph a pre-release
commit always finds an edge to fold into, so it **merges** and never appends -
`dropped` is 0 and `restored` is up to 2. That is why the 2026-09-16 seed made
the start class deterministic, and it is the same fact from the other side: the
seed swamped the learned set instead of putting it back.

### The regression

`tools/test_movement_graph_reset.py`, three tests, in the suite:

- `TouchEdge` and `ResetLearned` are **extracted from the shipped
  `bot2_graph.c`** and compiled against a stub graph, so the test runs the
  production text. A loaded graph of four cells and three edges is mutated the
  three ways a session mutates one, two edges are appended, and after the reset
  the loaded edges' cost, validation, traversal, failure and disabled fields are
  back, the appended edges are gone, `capped` is back, and walking cell 1's
  chain reaches its two loaded edges in the loaded order and no more. A second
  reset is a no-op.
- Every one of the three functions that mutates a loaded edge calls `TouchEdge`.
- `bot2_corner.c` is the only caller outside `bot2_graph.c`, which is the
  statement that production is unchanged.

It tests the bookkeeping. That the reset makes a start class deterministic is
the native measurement below.

## 4. Determinism on the production configuration

Build `.codex-research/production-graph-build-20260922-a`, manifest
`20260922T123022581163Z.sources.json` sha256
`ce91323553e020a461a7caea4709492fe0ee7a1fef7058228cf40fa9ae93f6cc`,
`openjkded.x86.exe` sha256
`0ec135356fe4b8cfec6099422425b056e33588007a710f4a71b0fbb9ade703cb`,
`jampgamex86.dll` sha256
`0b5c86a317081c8e12b40a463f198387f3d882852d517c700257740ccbe4849a`. **All 1,117
source hashes matched the working tree before the first world and again after
the last** (`verify_sources.py`, `manifest-before.json`, `manifest-after.json`).

Worlds under `.codex-research/production-graph-20260922-a/`, one bench call
each, sequential, launched detached from a script, every world retained
including the six that missed their class pin. 26 bench calls, 589.5 seconds.
Every world runs the decision-tape record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs) and
`--edges`.

### Phase V: the build, before it is used

The project's standing rule is that a rebuild reproduces the two seeded control
cells before a campaign may use it. With `--parity 256`, the research seed and
the library off:

| Cell | Release | Completed | Straight / state | Expected |
| --- | ---: | ---: | ---: | --- |
| class 125 | 3,125 | **15,925 ms** | **13 / 5** | 15,925 / 13 / 5 |
| class 75 | 3,475 | **21,125 ms** | **15 / 7** | 21,125 / 15 / 7 |

Both reproduce to the commit, from two different absolute levels of the class,
with the reset active. The reset is therefore not disturbing the cells the
project measures rebuilds against.

### Phase P: one class, one trip

`red_out_L1_stand`, the production cache as `--graph`, the tape library **off**,
the parity harness **off**, one world per bench call.

| Class | Worlds | Release levels | Completed | Leg decisions | Distinct sequences |
| --- | ---: | --- | ---: | ---: | ---: |
| 125 | **10 of 10** | 3,125 / 3,325 / 3,925 | **19,050 ms**, all ten | 12 | **1** |
| 75 | **6 of 6** | 3,075 / 3,675 / 4,675 | **19,150 ms**, all six | 13 | **1** |

Each class returns **one** completion time and **one** decision sequence: every
decision of the leg matches in offset from the release, kind, hop count,
landing cells, graph edge index, candidate count and admitted count. Nothing
differed, so there is nothing to report as differing.

Three of the sixteen worlds learned an edge before their release - `881->6164`
at two class-125 worlds and `61->6167` at a third - and the reset dropped it;
their legs are identical to the thirteen that learned none. That is the fix
being exercised rather than merely present.

Six worlds missed the class pin and are retained (`P-c125-r1`, `r4`, `r6`, `r9`
and `P-c75-r4`, `r4-a2`); the bench replaced each with a fresh world, which is
its ordinary behaviour and not a failure of the arm.

**One engineering note a reader needs.** The bench's `commits` count is every
commit after the release, and these worlds keep running after the objective is
reached, so `commits` is 13 to 15 where the **leg** has 12 or 13 decisions. The
sequences above are compared over the leg - from the release to the completion
time - and the tails past the capture, which depend on when the world ends,
are not part of the claim. Reading `commits` alone would have said these worlds
disagreed.

## 5. The floor, on the production configuration

All 36 cases unpinned in one bench call at `--parallel 4`, the way every earlier
floor record ran it, with the production cache, the library off and the harness
off.

| | Result |
| --- | --- |
| Cases | **36 of 36** |
| Deaths | **0** |
| Owners | `move` only, on every frame of every case |
| Forbidden spend | **none** - no case returned `forbidden_speed` or `forbidden_hold` |
| Parity | **0** comparisons and 0 mismatches, because the harness was never armed |

**The two cases the research seed killed both pass**: `red_ret_L1_respawn4` at
**16,300 ms** and `blue_out_L1_respawn4` at **16,150 ms**, zero deaths, in the
same world as the other 34. Both seeded regression cases pass on the production
cache as well - `red_ret_L1_stand` at **18,825 ms** and `blue_out_L1_stand` at
**21,225** - zero deaths, movement owner, no forbidden spend.

The parity row of gate row 1 is **not** claimed here: row 1 asks for a non-zero
comparison count per world, and the harness is off by the production
configuration's own definition, so this floor reports zero by construction. What
this floor establishes is the safety half - completion, deaths, ownership,
spend - on the configuration the rewrite is now qualified in. Reconciling row 1's
parity requirement with a harness the production configuration turns off is an
open question for the gate, not something this campaign decides.

Against the gate's own unseeded floor the times move both ways (35 of 36 cases
differ, +3,700 ms summed, best -3,925 and worst +2,925). **That is not a
controlled comparison** and no direction is read from it: different build,
different graph, and an unpinned floor draws a fresh start level per case, which
by itself decides the leg.

## What this does not establish

- **The production cache is a third graph identity.** Tapes and library indices
  bind to `Graph_Identity()`, so anything explored on the map-load graph
  (`g1-301dd39cbead57f6`) or on the research seed is refused under
  `g1-0e5af7aba9b5e908`. The explorer campaigns must be run on the cache they
  will execute on. No tape was extracted or replayed here.
- **Determinism is shown at two classes on one case.** Classes 0, 25, 50, 100,
  150 and 175 and the other eleven level-1 cases are not measured for it, and a
  respawn case releases differently from a stand.
- **The reset is measured on fixture worlds only**, which is the point of it,
  and so there is no measurement of production behaviour to compare - only the
  argument, and the regression, that nothing outside the fixture's own command
  can reach it.
- The floor is **one** run of 36 cases at `--parallel 4`, and runs inside one
  parallel batch are not independent samples; it is a safety verdict, not a
  timing distribution.
- **The cache is one map's.** 676 edges from 74 Kejim worlds says nothing about
  how many runs another map needs before its cache is worth loading.
- Row 5 is untouched, and the cache changes the search's candidate set, so the
  [cost reading](2026-09-22-graph-binding-and-decoupling.md#the-smoke-reading)
  does not carry to this configuration.

## Reproduce

```powershell
python .codex-research/production-graph-20260922-a/verify_sources.py `
  .codex-research/production-graph-20260922-a/manifest-before.json
python .codex-research/production-graph-20260922-a/stage_caches.py `
  .codex-research/gate-l1-20260922-a `
  .codex-research/production-graph-20260922-a/cache-sources
python tools/movement_graph_merge.py `
  .codex-research/production-graph-20260922-a/cache-sources `
  --out .codex-research/production-graph-20260922-a/production-graph.txt
# then, detached, from a script and not an inline command: run.sh
python .codex-research/production-graph-20260922-a/analyse.py
python .codex-research/production-graph-20260922-a/verify_sources.py `
  .codex-research/production-graph-20260922-a/manifest-after.json
```

## Tools and files

C source: `codemp/game/bot2_graph.c` (`TouchEdge`, `FreezeLoaded`,
`ResetLearned`, `Graph_ResetLearned`, the three `TouchEdge` call sites, two
`FreezeLoaded` call sites), `codemp/game/bot2_graph.h` (the declaration),
`codemp/game/bot2_corner.c` (the one call, at the release). No cvar was added
and no existing cvar changed. Project tools: one new regression,
`tools/test_movement_graph_reset.py`; `tools/movement_graph_merge.py` and
`tools/movement_bench.py` are used unchanged. The campaign's own scripts
(`run.py`, `run.sh`, `stage_caches.py`, `verify_sources.py`, `analyse.py`) are
retained beside its worlds and are not project tools. The Python suite passes
from the repo root: **842 tests**.
