# Tapes bound to their graph, the search decoupled from the parity harness, and a cost path for row 5

Recorded 2026-09-22. Three changes, measured together on one fresh build:
a decision tape is now bound to the graph it was recorded on and refused on any
other; hop discovery no longer runs only while the parity harness is armed; and
the level-1 gate's [row 5](../../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21) has a
measurement path with its first smoke reading.

> **The search no longer needs the parity harness, and the harness proves
> inert.** With `--parity 256` the two seeded control cells reproduce to the
> commit on the new build - class 125 **15,925 ms with 13 straight and 5 state
> commits**, class 75 **21,125 ms with 15 and 7**. With `--parity` unset the
> same two cells return **15,925** and **21,125 ms**, the **same leg decisions
> in the same order at the same levels** (13 of 13 and 15 of 15), the same
> commit counts, and **zero** parity comparisons - zero because nothing was
> armed, not because nothing searched. Unseeded, a level-matched pair returns
> **19,700 ms** either way, 12 of 12 decisions identical.
>
> **A tape is bound to its graph and refuses on another.** The engine computes
> a graph identity at map load - `g1-` and a 64-bit FNV-1a digest of the cells
> and edges as loaded - and the 2026-09-16 research seed and the unseeded build
> of the same map, which share map name and checksum exactly, are
> **`g1-98eb1e97c24a4e21`** (94,711 edges) and **`g1-301dd39cbead57f6`** (10).
> A library bound to the seeded graph, staged in an unseeded world, is refused
> **at load**: `keys=0`, `library=graph_mismatch`, the leg never forms a key
> and completes under the ranking. A library whose index is unbound but whose
> tapes are bound is refused tape by tape: the exact key answers (`exact=1`),
> `TapeLoad` refuses, and the census line reads
> **`entries=0 ... rung=graph_mismatch`** - the fallback at the leg's first
> think rather than after eleven misses. The same library in a **seeded** world
> still selects on the exact rung and hits **11 of 11** entries at 15,925 ms
> with 13 and 5, so the binding does not break the working case.
>
> **Row 5 has an instrument and its first reading is not good.** Four bots on
> one dedicated localhost server, unseeded, library off, parity harness off,
> 800 recorded server frames over a 20,000 ms window: movement work summed over
> the four bots is **p50 67.5 us, p95 36,800 us, p99 96,870 us, worst
> 272,692 us** against the 25,000 us server frame. **114 of 800 frames (14.3
> percent) exceed the declared 5,000 us budget and 64 (8.0 percent) exceed the
> whole frame**, and the spikes are spread across the run, not confined to the
> first second. **96 percent of the total is the Pmove-boundary search**, not
> command dispatch. Nothing was tuned to meet the budget. One run measures the
> instrument, not the row.
>
> **An unseeded stand run is not a cell.** Unseeded the graph is built at load
> and edges are learned while the bot's own AI plays before the release, so the
> start class does not fix the trip and neither does the exact release level:
> ten worlds pinned to level 3,125 return **16,725, 18,475 or 19,700 ms**, and
> **both** the 16,725 and the 19,700 outcome appear in **both** arms. The
> seeded cells are deterministic and are where the decoupling is shown.
>
> Zero deaths and the movement owner on every frame of all seventeen completed
> bench worlds; zero parity mismatches in the six armed ones.

## What was asked

Bind tapes to their graph and say which identity was chosen and why; make the
Python tools write it and the engine refuse a mismatch with the reason on the
census line; decouple the straight search from the parity harness while keeping
every safety gate, showing both the harness's inertness and the decoupling on
named cells; give row 5 a cost measurement path for the rewrite's production
configuration and smoke it once; build fresh and verify the source manifest
before and after every native collection.

## Binding

Build `.codex-research/graph-binding-build-20260922-b`, manifest
`20260922T120839890335Z.sources.json` sha256
`8fb81d0e8c9b2f196c7c184be0e1c2fc74397aaaa541dc3ada1a998856233034`,
`openjkded.x86.exe` sha256
`bd8e836b35141f3f55cf34346e914ec8a92b30d0606fcd72b887b1ead44eea59`. **All
1,117 source hashes matched the working tree before the first world and again
after the last** (`verify_sources.py`, `manifest-before.json`,
`manifest-after.json`), so no C source moved while worlds ran.

Worlds under `.codex-research/graph-binding-20260922-b/`, every one retained,
one bench call each, sequential, launched detached from a script (`run.sh` with
`run.py`, `progress.jsonl`, `campaign.out.txt`, `campaign.done`; `cost.sh` and
`cost2.sh` for the two cost worlds). **Twenty-six bench worlds in 384.8 seconds
of bench time**, seventeen completed and nine retained pin misses, plus two
20-second cost worlds.

Every bench world runs the decision-tape record's own
[settings block](2026-09-22-decision-tape.md#the-settings-every-world-runs) and
`--edges`, case `red_out_L1_stand`. `--parity 256` or nothing is the arm.

> **An earlier build of this same work, `graph-binding-build-20260922-a`, ran
> the same campaign** under `.codex-research/graph-binding-20260922-a/` and is
> retained. Its digest was wrong - the FNV-64 prime is 2^40 + 0x1b3 and the
> half-word decomposition treated it as 2^32 + 0x1b3, with the offset basis's
> halves swapped - so its identity strings are not this engine's. Every
> behavioural result it reports reproduces here. The arithmetic is now checked
> against a reference 64-bit FNV-1a on 2,000 strings.

The libraries of phase M are written by `build_library.py` from the tie-break
campaign's own four tapes
(`.codex-research/tie-break-tally-20260922-a/library/mp_ctf_kejim_-1301924053/`),
copied and re-emitted with the header field this work adds and nothing else
changed; the graph identities they declare are read from this campaign's own
`[GRAPH] map_load` lines, not typed in. `libraries.json` carries every hash.

## 1. The graph identity, and why this one

**The map checksum binds the map, not the graph.** A decision tape names graph
cells (`c0`, `c1`) and graph edges (`index`, `index2`) by **index**, and the
library index's key names the start **cell**. Those indices belong to one
graph's contents. Two graphs of the same map share `mapname` and
`sv_mapChecksum` exactly, and this campaign measures how far apart they can be:

| Graph | Identity | cells | edges | `cached` |
| --- | --- | ---: | ---: | ---: |
| the 2026-09-16 research seed | **`g1-98eb1e97c24a4e21`** | 14,249 | **94,711** | 1 |
| built at load, no seed | **`g1-301dd39cbead57f6`** | 14,249 | **10** | 0 |

**What was chosen: a digest over the graph's own contents, frozen at load.**
`Graph_Identity()` in `bot2_graph.c` returns `g1-` and a 64-bit FNV-1a digest
taken over the cell records and edge records **exactly as `Graph_Save` writes
them**, preceded by the version, the grid and the physics binding the cache
header already refuses on. It is computed once, at the end of `Move_PrepareMap`
and again after a `move_graph rebuild`, and printed on the `[GRAPH] map_load`
line.

Three reasons for this rather than the alternatives:

- **The cache's own binding will not do.** That is map, checksum, grid,
  gravity, speed and step - all of which the two graphs above agree on. It is a
  binding on the *world*; what a tape needs is a binding on the *contents*.
- **A hash of the cache file will not do either.** A graph is as often built at
  load as read from a cache (`cached=0` above), and a built graph has no file
  to hash until it writes one. Digesting the records means the cache a build
  wrote and the load that read it back agree by construction, because the
  digest is taken over the same `%.3f` and `%.6f` text the cache round-trips.
  Measured: fourteen unseeded worlds all built their graph at load and all
  report the identical `g1-301dd39cbead57f6`.
- **It is frozen at load on purpose.** Edges are learned while a world runs and
  appended to the array; appending never moves an existing index, and the
  identity a plan is bound to is the graph it was authored against. Freezing it
  is also what makes a mid-leg refusal impossible.

The field is written by the tools as the **last** header field of a tape and of
a library index (`tools/movement_decision_tape.py`,
`tools/movement_tape_library.py`), and read by the engine with `strstr` rather
than by position, so that:

- a tape or index written before this work declares `graph=none`, loads as an
  **unbound** tape and says so on the selection line, and every earlier
  campaign replays unchanged;
- an engine that predates the field parses the four header fields before it and
  ignores it, so a bound tape is not a new format.

`movement_tape_library.py` refuses a library whose tapes declare two different
graphs, and `--graph` declares one for tapes that carry none but is refused
when a tape already declares a different one, because the tape knows what it
was recorded on and the tool does not. Unit tests cover all of that
(`test_movement_tape_library.py`, `test_movement_decision_tape.py`).

## 2. The refusal, on a world

Three worlds, all with the parity harness off.

| World | Graph the world loaded | Index declares | Tapes declare | Library | Rung | Entries | Completed |
| --- | --- | --- | --- | --- | --- | ---: | ---: |
| `M-refused` | `g1-301dd39cbead57f6` | `g1-98eb1e97c24a4e21` | seeded | **refused at load**, `keys=0` | - | 0 | **18,475 ms** |
| `M-tape-refused` | `g1-301dd39cbead57f6` | `none` | seeded | loaded, `keys=4` | **`graph_mismatch`** | 0 | **19,700 ms** |
| `M-control` | `g1-98eb1e97c24a4e21` | `g1-98eb1e97c24a4e21` | seeded | loaded, `keys=4` | **`exact`** | 11 | **15,925 ms**, 13 / 5 |

`M-refused` is the production case - a library built on the research seed,
staged in an unseeded world - and the whole library refuses before any leg
forms a key:

    [TAPE] library graph_mismatch dir=mp_ctf_kejim_-1301924053 keys=0
    [TAPE] select leg=t1e83 cell=0 phase=125 rung=pending file=none entries=0
      exact=0 phase_keys=0 link_keys=0 keys=0 library=graph_mismatch
      graph=g1-301dd39cbead57f6 library_graph=g1-98eb1e97c24a4e21

`M-tape-refused` is the hand-assembled case - an unbound index over bound
tapes - and is the only way to reach the per-tape refusal, which is the one the
census reports:

    [TAPE] graph_mismatch name=mp_ctf_kejim_-1301924053/c125-self.tape
      tape_graph=g1-98eb1e97c24a4e21 graph=g1-301dd39cbead57f6
    [TAPE] select leg=t1e83 cell=0 phase=125 rung=graph_mismatch
      file=c125-self.tape entries=0 exact=1 phase_keys=3 link_keys=0 keys=4
      library=loaded graph=g1-301dd39cbead57f6 library_graph=none
    [TAPE] census entries=0 hits=0 refused=0 absent=0 deferred=0 cursor=0
      leg=t1e83 cell=0 phase=125 rung=graph_mismatch file=none exact=1
      phase_keys=3 link_keys=0 keys=4 graph=g1-301dd39cbead57f6
      library_graph=none

`exact=1` is the point: the key matched, and the tape was refused anyway. Under
the previous build that world would have selected on the exact rung and run
eleven entries into zero hits, which is what the
[gate campaign](2026-09-22-gate-l1.md) measured and could not explain from the
world. The census now names it, and the leg falls back at its first think.

`M-control` is the regression: the same seeded library in a seeded world
selects on the **exact** rung and hits **11 of 11** entries, `deferred=2`,
returning the class-125 cell's own 15,925 ms with 13 straight and 5 state
commits. The binding refuses what does not belong and changes nothing that
does.

One engineering note a reader needs: the `[TAPE] library` line still prints
`why` without the two graph fields - the reason is on it (`graph_mismatch`)
but the identities are not. They are on the selection line and on the census
line. That is a print, not behaviour, and it is not repaired here because
repairing it means a C source change and a rebuild after the collection.

## 3. The gates that were removed, and the gates that were not

`ParityHorizon` held five conditions and returned a horizon: `bot_moveParity`
greater than zero, a dedicated server, `sv_cheats`, `bot_moveRewrite`, and
`Bot2_IsMovementOnly`. `Bot2_MoveParityBeforePmove` returned immediately when
that horizon was zero - and **hop discovery is dispatched from that boundary**,
after the comparison and before `ParityBuild`. So a bench call without
`--parity` searched nothing at all, which is the coupling the
[refined method](2026-09-19-refined-method.md) recorded and the reason the
project memory says bench defaults "silently give commits=0".

The four conditions that are not about the harness now live in
`MoveSearchGates(client)`, and **all four are kept**:

| Gate | Kept | Why |
| --- | --- | --- |
| `dedicated.integer` | yes | the fixtures are dedicated-server only |
| `sv_cheats` | yes | the whole rewrite is a cheats-gated diagnostic |
| `bot_moveRewrite` | yes | the owner switch |
| `Bot2_IsMovementOnly(client)` | yes | only the movement-only client is driven |
| `bot_moveParity > 0` | **removed from the search's path** | it arms the harness and nothing else |

`ParityHorizon` is now `MoveSearchGates` plus the cvar, so the harness itself is
unchanged. The boundary returns early on `!MoveSearchGates` (recording
`no_search_gates` on a landing frame, where it recorded `no_parity_horizon`),
the comparison blocks run only while `parity.armed`, which needs a horizon, and
`ParityBuild` is called only when `horizon > 0`. `ExploreEnabled` and the
exploration stand in `Move_Command`, which used `ParityHorizon(client) > 0` for
the same historical reason, now use `MoveSearchGates`; `bot_moveExplore` is
default off, so that is inert unless a campaign asks for it.

### Inertness, with the harness on

Both seeded control cells on the new build, `--parity 256`, library off:

| Cell | Release | Completed | Straight / state | Compared | Mismatches | Expected |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| class 125 | 3,125 | **15,925 ms** | **13 / 5** | 667 | 0 | 15,925 / 13 / 5 |
| class 75 | 3,875 | **21,125 ms** | **15 / 7** | 905 | 0 | 21,125 / 15 / 7 |

Both cells are the ones the [tape-library record](2026-09-22-tape-library.md)
requires before a rebuild may be used, and both reproduce to the commit.

### The decoupling, with the harness off

The same two cells with `--parity` unset:

| Cell | Release | Completed | Straight / state | Leg decisions identical | Compared | `no_search_gates` |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| class 125 | 3,125 | **15,925 ms** | **13 / 5** | **13 of 13** | **0** | 0 |
| class 75 | 3,875 | **21,125 ms** | **15 / 7** | **15 of 15** | **0** | 0 |

Time, straight count and state count are identical to the armed arm, and the
**leg** decisions are identical too: every committed sequence after the release
matches its armed sibling in level, kind, landing cell and graph edge.

The comparison count is **zero in both**, and the bench's `row1_ok` is
correspondingly false: that is the harness saying it was never armed, and the
worlds show it did not have to be armed for the search to run. A world where
the search had *not* run would report `no_search_gates` on its landing frames
and commit nothing; these report none and commit 13 and 15.

### Unseeded, with and without the harness

| World | Release | parity | Completed | Straight / state | Leg decisions | Compared |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| `X-l3125-parity` | 3,125 | 256 | **19,700 ms** | 12 / 0 | - | 848 |
| `X-l3125-noparity` | 3,125 | off | **19,700 ms** | 12 / 0 | **12 of 12 identical** | **0** |

**But an unseeded world is not a cell, and that is the finding here.** Ten
worlds released at level 3,125 on the unseeded graph return three different
times, and the two arms share the outcomes:

| parity | worlds at level 3,125 | outcomes |
| --- | ---: | --- |
| 256 | 5 | 16,725 / 18,475 / 19,700 / 19,700 / 19,700 |
| off | 5 | 16,725 / 16,725 / 16,725 / 19,700 / 19,700 |

Unseeded the graph is built at load and edges are **learned while the bot's own
AI plays before the release**, so neither the start class nor the exact release
level fixes the graph the leg starts from. The class-pinned pair of phase `U`
makes the same point from the other side: both worlds released at level 3,125
anyway, and they returned 18,475 and 16,725 ms. **The matched pair above is one
sample from each arm of the same distribution, not a reproduction**, and the
decoupling is established on the seeded cells, which are deterministic. How
many repetitions an unseeded cell needs is the open item the unseeded campaigns
inherit.

## 4. Row 5: the cost path

`tools/movement_cost_runner.py`, new beside `tools/strafe_cost_runner.py`. A
new tool rather than a flag, because that runner drives the archived strafe
engine's workload - supported hops, a required route catalog, a baked flight
artifact, a forecast guard, rendezvous and hold cases - none of which the
rewrite reads. The two share the engine's `bot2_movecost.c` fixture and nothing
else, so they share the fixture and not the command line.

Usage:

    python tools/movement_cost_runner.py \
        --build .codex-research/graph-binding-build-20260922-b \
        --out .codex-research/<campaign>/<world> \
        --bots 4 --port 30680 [--budget-us 5000] [--parity 0] \
        [--library <dir>] [--graph <cache>] [--telemetry]

It stages the build against its own manifest, writes a `start.cfg` with
`bot_moveRewrite` and edges, planner, state and the straight search on, the
library on only when one is staged, `bot_moveParity` **unset**, no graph seed,
four bots on one dedicated localhost server at `sv_fps` 40, and runs the
fixture's fixed 20,000 ms window. Two engine changes were needed: the fixture
accepted only 2, 8 or 16 clients and now accepts **4**, which is the count the
row names; and the Pmove boundary is now charged to the frame and the client
through `Bot2_MoveCostPreparation`, the hook the fixture already had for work
that runs outside the command dispatch it times. Both are inert with no capture
active.

**The declared budget is 5,000 us per frame for all four bots together**, a
fifth of the 25,000 us server frame. It was declared before the run and nothing
was tuned toward it.

### The smoke reading

`cost-smoke`, telemetry off, 800 recorded frames, four bots alive throughout,
`bad_clocks` zero on every frame, two flag pickups during the window.

| | us |
| --- | ---: |
| p50 | **67.5** |
| p95 | **36,800.1** |
| p99 | **96,869.9** |
| worst frame | **272,691.6** (level 8,450, 50 ms into the window) |
| mean | 6,028.8 |
| server frame | 25,000 |
| declared budget | 5,000 |

| | frames | share |
| --- | ---: | ---: |
| over the declared 5,000 us budget | **114** | 14.3 % |
| over the whole 25,000 us frame | **64** | 8.0 % |

**Where the time goes**: 4,634,811 us of the 4,823,028 us total is
`prepare_us`, the Pmove-boundary searches - **96 percent** - against 188,217 us
of command dispatch. **The spikes are not a startup artefact**: by window
segment, frames over the server frame are 5 in the first 2 s, 8 in 2-5 s, 20 in
5-10 s and 31 in 10-20 s, while the median in each segment stays between 43 and
83 us. The distribution is bimodal - most frames are nearly free, and a frame on
which a search actually runs is one to eleven times the whole server frame.

**That the searches ran is shown by a companion world, not by the timing
world.** A timing world has telemetry off, because printing inside the search
measures a different bot, so it prints no `[MOVE]` lines and its `commits`
count is zero by construction. `cost-telemetry`, the same configuration with
`bot_telemetry=1` and its timings therefore **declared invalid**
(`timing_claims_valid: false` in its own report), shows **1,402 candidate lines
and 33 committed sequences** over the four bots in the same 20 s window (and,
for reference only, p50 115.5, p95 40,809, p99 100,557, worst 271,273 us, 116
frames over budget).

### What this does and does not establish

It establishes that the path exists and produces the row's numbers, and that
the first reading is **far outside the declared budget**. It does not establish
row 5: one run is one sample on one map with one workload, the budget has never
been re-declared by the owner, the "results invalidated on root/goal/world
change" and "no idle wait induced by the controller" clauses of the row are not
measured here at all, and no cost work has been attempted. A reading this far
out is a design question about the search's per-frame budget, not a tuning
question, and the plan does not answer it here.

## What this does not establish

The graph identity is frozen at load, so two worlds that learn different edges
while they run still report one identity; a tape authored from a world's
*learned* graph is not protected by it, and the unseeded non-reproducibility
above is exactly that gap showing. The refusal is measured on one map, one leg
and one library. The decoupling is measured on two deterministic seeded cells
and one unseeded pair; the 36-case floor was **not** re-run with the harness
off, so "the floor passes without the harness" is not claimed. Row 5 has an
instrument and one reading, not a verdict.
