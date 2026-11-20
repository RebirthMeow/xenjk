# Deferred graph save, no legacy hand-off at a declined crossing, and route telemetry

Recorded 2026-09-22. The first three items of the work order in
[Seed deaths and planner reach](2026-09-22-seed-deaths-and-planner-reach.md)
and [Row 5 attributed](2026-09-22-boundary-cost-attribution.md), in the order
[PLAN.md](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-22-the-decision-explorer) gives
them. Each change was built with its own regression. After that came one fresh
build and one sequential collection.

> **1. The cache write leaves the frame, and no decision changes.** With
> `bot_moveGraphSaveDefer=1`, an autosave copies the edge records and the
> capped flag into a snapshot instead of rewriting the cache. The last
> snapshot is written at `G_ShutdownGame`, from `Graph_Autosave`, and at
> `Move_PrepareMap`. The default is 0, which keeps today's behaviour. **All
> six reference cells are decision-identical between the arms and match their
> reference worlds:**
>
> - seeded class 125: **15,925 ms**, 11 of 11 leg decisions;
> - seeded class 75: **21,125**, 13 of 13;
> - v1 class 125: **19,050**, 12 of 12;
> - v1 class 75: **19,150**, 13 of 13;
> - v2 class 125: **19,275**, 12 of 12;
> - v2 class 75: **19,450**, 13 of 13.
>
> The parity harness reported zero mismatches in every armed world. **Four of
> the six saved caches are byte-identical between arms.** In the two seeded
> cells, the saved caches match record for record over every edge both files
> hold. In each case the control world learned one more edge after completing
> its leg, before the runner's quit reached the server. That extra autosave
> also wrote one earlier cost update, which today's last autosave in the
> deferred world had not yet written. So the gap is post-leg play, not the
> arm. A compiled regression over the shipped writer shows the flushed file is
> byte for byte today's last autosave.
>
> **Cost, two alternated pairs on v2, four bots.** Deferred against
> immediate:
>
> | Measure | Immediate (off) | Deferred (on) |
> | --- | --- | --- |
> | p50 | 90.7 / 90.9 us | 61.1 / 72.7 us |
> | p95 | 48,572 / 46,497 us | 37,402 / 34,417 us |
> | p99 | 118,810 / 119,532 us | 63,538 / 74,001 us |
> | Worst frame | 165,926 / 146,839 us | 114,127 / 126,549 us |
> | Frames over 5,000 us | 119 / 132 of 800 | 103 / 117 |
> | Frames over 25,000 us | 72 / 75 | 64 / 55 |
> | Dispatch total | 0.52 / 0.65 s | 0.06 / 0.07 s |
>
> The dispatch total fell about 9 times. These off worlds happened to have no
> multi-second stall, and the on worlds cannot have one from a save. **Row 5 is
> still not met.** Every worst frame is now the Pmove-boundary search
> (`prepare_us`), which is the next item.
>
> **2. At the walk's own declines, the movement owner holds and replans;
> nothing is handed to legacy.** This change has no cvar. It is inert where
> nothing declines: none of the twelve reference worlds printed a decline,
> and all were decision-identical.
>
> **The eleven-death trip no longer dies.** On the research seed with the
> harness on, both respawn4 cases at class 50 pass:
>
> - `red_ret_L1_respawn4` at **19,400 ms**;
> - `blue_out_L1_respawn4` at **19,400**.
>
> Both had zero deaths, owner `move` only, zero `owner=legacy` lines, 824 and
> 811 parity comparisons, and zero mismatches. The hold fired where the old
> trip's one-frame hand-off was, at +3,500 ms at (2751.8, 2972.8). There the
> bot avoided the two crossings it was walking to and replanned. From that
> point the trip is a different one, and it never reaches the fatal lip at
> +12,000.
>
> **On v2 at class 50, never measured before, both cases pass at 16,625 ms.**
> No hold fired and no legacy owner appeared.
>
> **Floor on v2, save deferred, harness and library off: 36 of 36.** Zero
> deaths, owner `move`, zero legacy lines and no forbidden spend. Two L3
> stand worlds each held for one frame. Both seeded regression cases pass,
> at 17,500 and 18,400 ms.
>
> **3. Every installed route prints its edge indices.** Each route prints one
> `[ROUTE]` line: 447 in this campaign. That is one for each of the 290 replan
> plan lines and each of the 157 reroute installs. No line is printed inside
> a search. `tools/movement_graph_enrich.py`'s new parser, `read_routes`,
> reads all 447 lines with none incomplete: 4,909 edge indices, each bound to
> its graph identity.
>
> Build `.codex-research/safety-save-build-20260922-a`. Before the first world
> and after the last, 1,117 source hashes and both binaries matched. There
> were 54 bench worlds, run one per call and sequentially, and 4 cost worlds.
> All are retained under `.codex-research/safety-save-20260922-a/`.

## What was asked

1. **Deferred graph save** behind `bot_moveGraphSaveDefer`, default 0. The
   regressions: the saved cache byte-identical between arms, and the reference
   cells identical.
2. **No legacy hand-off at level 1.** On `floor_missing`, or on any walk
   decline that hands the frame to legacy, avoid the declined crossing and
   replan through the one owner. If no route exists, hold the walk's last safe
   command and retry on the cadence. The regressions:
   - both respawn4 cases pinned at class 50 on the research seed, which must
     complete with zero deaths and no legacy owner;
   - the same two cases on v2;
   - the fix shown inert on the reference cells.
3. **Route telemetry for enrichment**, with a unit test of the parser.

Then:

- one fresh build;
- the reference cells at defaults, and again with the save deferred;
- the class-50 regressions;
- the floor on v2 with the save deferred, plus both seeded regressions;
- two alternated cost pairs.

## Binding

| Item | Value |
| --- | --- |
| Build | `.codex-research/safety-save-build-20260922-a`, manifest `20260922T185245765249Z.sources.json` sha256 `3cfb6779702e9ea3dfb8c565c368f03c29f34465ec72be194f8e65ea3b0961ca`; `openjkded.x86.exe` `aa768f6f344c70d1...`, `jampgamex86.dll` `ea9ea1e11cac0b92...` |
| Sources | 1,117 hashes and both binaries matched before the first world (`manifest-before.json`) and after the last (`manifest-after.json`); the two reports are byte-identical, sha256 `b39b350980ecab6f...` |
| Changed sources | `bot2_graph.c` `d0541780be27453e...`, `bot2_graph.h` `18d4213449e4cfbd...`, `bot2_move.c` `876f9feb7950563a...`, `bot2_move.h` `46d6f672563cb7a6...`, `g_main.c` `af16c079bc07f8a3...` |
| Graphs | research seed `g1-98eb1e97c24a4e21`, cache v1 `g1-0e5af7aba9b5e908`, cache v2 `g1-9631cbeaef2e5ba8`, read from `[GRAPH] map_load` in every world |
| Settings | the decision-tape record's block (`bot_moveReplay=0`, `bot_moveHeadingSweep=0`, `bot_moveStatePrefer=1`, `bot_moveReroute=1`, `bot_moveExitSpeed=500`, `bot_moveSuccessorAim=1`, `bot_moveSuccessorFan=0`, `bot_forecastItems=0`, `bot_forecastTriggerShadow=0`), `--edges`; pinned cells use `--corner-class` with the window 3,000 to 5,500; library off; search budget off |
| Campaign | `run.py` `c4bd3029e37f02e0...`, `run.sh` `d4c91b617425ce3b...`, `cost.py` `23ea19eb6570865a...`, `analyse.py` `b2d983ae486b92d7...`, `progress.jsonl` `87b17accfb480fff...`, `analysis.txt` `b9aec21044b087ec...` |
| Launch | `Start-Process "C:\Program Files\Git\bin\bash.exe" -l run.sh`, hidden; markers `bench.done`, `cost.done` and `campaign.done` (each exit 0) |

## 1. The deferred save

### What changed

In `bot2_graph.c`:

- `Graph_Save` is now split. `SaveEdges(edges, edgeCount, capped)` holds the
  writer body. `Graph_Save` writes the live graph through it and clears any
  pending snapshot, because an explicit write supersedes older autosaves, as
  it does today.
- The five autosave sites now call `Autosave()`:
  - `Graph_AddEdge`;
  - the faster merge in `Graph_AddStrafeEdge`;
  - the append in `Graph_AddStrafeEdge`;
  - the first validation in `Graph_EdgeResult`;
  - `Graph_ResetLearned`.
- `Autosave()` is today's `Graph_Save()` when the cvar is 0. When it is 1,
  `Autosave()` instead does a `memcpy` of `edge[0..edgeCount)` and `capped`
  into a static snapshot. Cells, nodes and the rest of the header are fixed
  after the load, so the writer reads them from the graph.
- `Graph_SaveFlush()` writes a pending snapshot and prints
  `[GRAPH] cache_flush`. It is called at three points:
  - from `G_ShutdownGame` (`g_main.c`);
  - from `Graph_Autosave`, before the flag changes;
  - at the top of `Move_PrepareMap`, before `Reset`.
- `tools/movement_cost_runner.py` gains `--graph-save-defer`, and its report
  records the value.

The flush line is printed after the runner has stopped reading the log, so no
world's `server.log` shows it. The flushed file's own evidence is in the
worlds. Each deferred cost world's cache holds every edge its log printed as
added: 3,366, 3,363 and so on, all past the loaded 3,312. Its modification
time is the quit.

### Regression: the compiled writer

`tools/test_movement_graph_save_defer.py` extracts `SaveEdges`, `Autosave`,
`Graph_SaveFlush` and `Graph_Save` from the source and compiles them against
an in-memory file system. It runs one session under each arm. The session has:

- three learns;
- a validation that autosaves;
- a cost update and a disabling that do not autosave.

Results:

- The immediate arm writes 4 times.
- The deferred arm writes 0 times until the flush, then once. The flushed file
  is **byte-identical** to the immediate arm's last write, including the stale
  cost.
- A second flush writes nothing.
- An explicit `Graph_Save` supersedes the snapshot.

Source checks confirm three more facts:

- no learn calls `Graph_Save` directly;
- the flush sits in `G_ShutdownGame` and `Graph_Autosave`;
- in `Move_PrepareMap`, the flush comes before `Reset`.

### Regression: the reference cells

R is the default arm and D has the save deferred. "Leg decisions" counts the
`reason=sequence` commits from the release to completion. The bench's
straight/state counts run on until the runner quits, so they are not compared.

| Cell | Reference world | R | D | Decisions R = D = reference | Cache R vs D |
| --- | --- | --- | --- | --- | --- |
| seed class 125, harness on | `search-budget-20260922-c/V-c125-a2`, 15,925 | 15,925, 707 compared, 0 mismatches | 15,925, 667 compared, 0 | 11 of 11 | 94,716 of 94,716 common records identical; R has one post-leg edge more |
| seed class 75, harness on | `V-c75`, 21,125 | 21,125, 895 / 0 | 21,125, 905 / 0 | 13 of 13 | 94,714 of 94,714 identical; R has one post-leg edge more |
| v1 class 125 | `W-c125`, 19,050 | 19,050 | 19,050 | 12 of 12 | **byte-identical** (689 edges) |
| v1 class 75 | `W-c75`, 19,150 | 19,150 | 19,150 | 13 of 13 | **byte-identical** (690) |
| v2 class 125 | `production-graph-20260922-b/P-c125-r1`, 19,275 | 19,275 | 19,275 | 12 of 12 | **byte-identical** (3,319) |
| v2 class 75 | `P-c75-r1`, 19,450 | 19,450 | 19,450 | 13 of 13 | **byte-identical** (3,323) |

**The two seeded differences are post-leg play, and the arm does not cause
them.** Take the seeded class-125 R world. It completed its leg at level
20,050 and learned edge 94,716 at 21,375, before the quit. That append's
autosave also wrote a `Graph_EdgeCost` update, 600 to 575 ms, made to edge
59,636 when the leg's last state commit completed. The D world completed and
quit with no post-leg learn, so its file carries 600, which is the value
today's last autosave in that world would have written. Seeded class 75 is
the same case, with an append at 26,500 and no differing common record. The
four production cells learned the same post-leg set in both arms (0, 0, 1
and 1 edges), and their files are identical.

**A correction to the reference numbers carried in the work order.** The
commit counts quoted for the cells were the bench's whole-world counts, for
example "class 75 21,125 / 16 / 8". Those counts include commits after the
objective, and they vary with when the quit lands: the same class-75 cell
reads 16/8, 15/7 and 14/7 across worlds with identical legs. The leg itself
is **13 decisions at seeded class 75, 11 at seeded class 125, 12 and 13 on v1,
and 12 and 13 on v2.**

### Cost

`tools/movement_cost_runner.py` ran four bots on one dedicated localhost
server with cache v2, the library off, the harness off, telemetry off and 800
frames. The pairs were alternated: C1 ran off then on, C2 ran on then off.

| World | p50 us | p95 us | p99 us | Worst us | > 5,000 | > 25,000 | Dispatch total | Boundary total | Worst frame in |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| C1 off | 90.7 | 48,572 | 118,810 | 165,926 | 119 | 72 | 0.516 s | 4.91 s | boundary |
| C1 on | 61.1 | 37,402 | 63,538 | 114,127 | 103 | 64 | 0.059 s | 3.63 s | boundary |
| C2 on | 72.7 | 34,417 | 74,001 | 126,549 | 117 | 55 | 0.069 s | 3.64 s | boundary |
| C2 off | 90.9 | 46,497 | 119,532 | 146,839 | 132 | 75 | 0.649 s | 4.78 s | boundary |

Learned edges in the four worlds: 54, 54, 51 and 57.

**What the deferral bought:**

- It removed about 0.45 to 0.58 s of command-dispatch work per world, which
  is the ordinary saves the attribution record priced at about 12 ms each.
- p99 fell from about 119 ms to 64 to 74 ms.
- The worst frame fell by 20 to 50 ms.

**Frames still over 5 ms are 103 to 117 of 800.** Every world's worst frame
is now a boundary search. The boundary total also fell, by about 1.2 s. This
is not explained. The saves are not charged to the boundary. Cost worlds of
one configuration are not identical (LESSONS), so two pairs cannot separate
this from world-to-world spread. It is reported, not claimed.

**No multi-second frame appeared in either off world.** On v1 the off
controls had them (2.41 s and 0.56 s). These are two worlds per arm, and
nothing here shows that v2 immediate saves cannot stall.

## 2. No legacy hand-off at a declined crossing

### What changed

In `bot2_move.c`, `DeclineHeld()` runs from `Move_Command` on every frame the
walk declined. It covers the walk's own declines:

- `corridor`, any refusal reason (`floor_missing`, `trigger_hurt`, `drop`, and
  so on);
- `no_path` off the route;
- `miss`.

For these declines it proceeds as follows:

1. **Releases a sequence still in progress.** An onward walk is released as
   the onward bound releases it. A declined run-up ends as a miss does.
   Either way, the replan gate is not held shut.
2. **Avoids the route's next crossing.** This is the first graph edge within
   four path entries past the progress cell, and it is added to the
   per-objective avoid list with `StateAvoid(..., "declined")`. That call
   fails the edge once and asks for an immediate replan. It is charged at
   most once per route generation, so a route that keeps declining disables
   its edge after two routes and does not fail it on every frame. A `miss`
   has already charged its own edge and is held for the frame only.
3. **Replans on the cadence when there is no crossing to avoid.** The replan
   follows `MOVE_PATH_RETRY`.
4. **Holds the walk's brake.** This is the command the walk itself issues at
   a hazard: zero wish velocity with stop allowed, through
   `Bot2_MoveControl`, at the current view. `Move_Command` returns owned, so
   the owner stays `move`.

The frame stays **unowned** for the searches and the parity harness (a live
decline still ends an armed horizon) because the walk did not produce it. The
hold runs no search and reads no clock.

Three declines keep today's hand-off, because each yields to a different
mechanism:

- `movement_type`: the player is not in normal movement, for example dead.
- `legacy_state`: a retained non-walk state is set. Only the legacy machine
  sets one, and every `legacy_state` decline in the retained gate worlds
  followed a corridor hand-off.
- `offmesh`: an off-mesh connection is inside the yield radius. No world has
  recorded one on Kejim.

Each hold prints `[MOVE] ... event=recover reason=hold decline=...
path_index=... crossing=... avoided=...`. The new per-client fields live in
`movement_t.declineHold`.

### Regression: class 50

| World | Graph, harness | Release | Result | Deaths | Owners | Legacy lines | Holds | Crossings avoided | Parity |
| --- | --- | ---: | --- | ---: | --- | ---: | ---: | ---: | --- |
| `S-seed-red_ret_L1_respawn4-c50` | seed, on | 3,650 | **pass 19,400 ms** | 0 | move | 0 | 2 | 2 | 824 compared, 0 mismatches |
| `S-seed-blue_out_L1_respawn4-c50` | seed, on | 3,650 | **pass 19,400 ms** | 0 | move | 0 | 15 (13 before the release) | 15 | 811, 0 |
| `S-v2-red_ret_L1_respawn4-c50` | v2, off | 3,450 | **pass 16,625 ms** | 0 | move | 0 | 0 | 0 | - |
| `S-v2-blue_out_L1_respawn4-c50` | v2, off | 3,450 | **pass 16,625 ms** | 0 | move | 0 | 0 | 0 | - |

**How the seeded trip changed.** The eleven deaths had a one-frame
`floor_missing` decline at +3,475 ms at (2751.8, 2972.8). On that frame legacy
moved the bot for one frame, and the walk restarted.

On this build the same decline comes at **+3,500** at the same point. It is
held instead:

1. The route's next crossing, 83235, is avoided, and the bot replans.
2. The walk declines once more on the new route. Crossing 83322 is avoided.
3. The walk resumes.

Both cases then make the same eleven leg decisions and complete at 19,400 ms.
They never walk to the pit lip at (671, 1408), where the old trip declined at
+12,000 and legacy's escape jump fell 1,776 units.

**So the regression shows the trip completing with no hand-off anywhere, but
not the hold at the fatal lip itself.** That lip is no longer on the trip.

In the blue-side world, 13 of the 15 holds were **before the release**, in
the fixture's free play at (2345.8, 4852.8). There the bot held still for 16
frames while successive replans avoided one crossing each. `Move_Reset` at
the release clears the avoid list, and `Graph_ResetLearned` restores the
failure counts.

**Pinned window, not the death level.** The deaths were released at level
2,850. The class window here starts at 3,000, so both worlds released at
3,650, which is also class 50. Two things are established: within a class the
trip is one trip at every level measured (the decision-explorer stage-1 and
production-graph records), and every class-50 death shared its pre-hold
history exactly. **A control on the previous build at class 50 was not run.**
The bench refuses a build whose sources differ from the working tree, and the
pre-change tree was not kept apart. The claim therefore rests on the retained
eleven deaths, not on a same-session control.

**v2 at class 50 is measured for the first time: 16,625 ms for both cases**,
with no decline at all.

### Inert where nothing declines

None of the twelve R and D reference worlds printed a single `event=recover`
line, and all twelve are decision-identical to the reference worlds of the
earlier builds. The hold is reached only from a declined frame.

### The floor

The floor ran on cache v2 with the save deferred, harness and library off,
and one bench call per case.

- **36 of 36 pass.**
- Zero deaths, owner `move` in every world, zero `owner=legacy` lines, no
  forbidden spend.
- Two worlds held, one frame each:
  - `red_out_L3_stand`: `floor_missing` at (641.0, 1381.2) at 425 u/s under
    Force Speed. No crossing was within four entries, so it held for the
    cadence replan. Pass, 12,300 ms.
  - `blue_out_L3_stand`: off the route (`path_index=-1`). Pass, 10,375 ms.
- Seeded regression cases on v2: `red_ret_L1_stand` **17,500 ms**,
  `blue_out_L1_stand` **18,400**.

Unpinned floor times are not comparable across campaigns, because each world
releases at its own level. 7 of 36 times match the v2 floor record.

## 3. Route telemetry

`RouteLine()` prints, for every installed route:

```
[ROUTE] client=0 level=2525 gen=1 source=replan from=3 to=1 cells=20 cost_ms=13282 state=1 graph=g1-98eb1e97c24a4e21 total=16 first=0 count=16 edges=26722,57458,...
```

Details:

- Routes of more than 64 edges are split into parts (`first` and `count`).
  This campaign's longest route had 18.
- `graph` binds the indices to the graph they name.
- `Replan` prints the line where it installs the route.
- A reroute installed by a commit sets a pending flag. The line is printed
  **after the search has returned**, at the end of
  `Bot2_MoveParityBeforePmove` after the cost fixture's charge. There are two
  fallbacks: the top of `Move_Command`, and the top of `Replan` before the
  route is replaced.
- The line uses `Bot2_PrintTelemetry`, so a cost world with telemetry off
  prints nothing.

In this campaign the counts agree exactly: 290 replan plan lines, 157 reroute
installs, and 447 `[ROUTE]` lines.

Tests:

- `read_routes` in `tools/movement_graph_enrich.py` groups lines by client
  and generation and joins the parts in order. A route with a missing part is
  returned as incomplete and never guessed at. It is covered by six tests in
  `tools/test_movement_graph_enrich.py`. Nothing folds routes yet.
- `tools/test_movement_decline_hold.py` checks the text for three things:
  only `Replan` and the pending print call `RouteLine`; the pending print is
  absent from `StraightSearch`, `GapSearch`, `RerouteInstall` and
  `StraightCommit`; and the boundary prints after the cost charge. It also
  checks the hold's decline set.

## What this does not establish

- **Byte identity of the seeded caches in one world.** Two worlds of one cell
  differ in post-leg play, so identity is shown for the four production
  cells, as a common-record match for the seeded two, and by the compiled
  regression.
- **That the hold is safe at the fatal lip.** The changed trip no longer
  reaches it. The floor's `red_out_L3_stand` hold at (641, 1381) is the
  nearest measured case, and it passed.
- **A same-build or previous-build class-50 control.** See above.
- **Row 5.** Frames over the declared budget and the p95-p99 tail remain; they
  are whole landing searches. The next items are the planner memo and
  spreading the finish phase.
- **A periodic flush for production.** A crash loses the session's learning
  when the save is deferred. The cvar stays **default 0** until the owner
  decides otherwise.

## Reproduce

From the repo root:

```
python tools/strafe_fixture_build.py --out .codex-research/safety-save-build-20260922-a
python .codex-research/safety-save-20260922-a/verify_sources.py .codex-research/safety-save-20260922-a/manifest-before.json
# detached: Start-Process "C:\Program Files\Git\bin\bash.exe" -l .codex-research/safety-save-20260922-a/run.sh
python .codex-research/safety-save-20260922-a/analyse.py
python -m unittest discover -s tools -p "test_*.py"
```
