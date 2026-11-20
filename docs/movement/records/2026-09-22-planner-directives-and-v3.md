# Deferred save by default, planner directives, cache v3 and explorer route branching

Recorded 2026-09-22. Items (1) and (4) of the work order in
[Seed deaths and planner reach](2026-09-22-seed-deaths-and-planner-reach.md),
after [the deferred save, the decline hold and route telemetry](2026-09-22-deferred-save-and-floor-safety.md),
then cache v3 from planned routes and the explorer's route-branching step. One
fresh build, one sequential collection, one smoke. The eight-class chain on v3
was **not launched**: the owner stopped the session before that task. Its
script is written and its launch procedure is in
[HANDOFF](../HANDOFF.md).

> **1. The deferred graph save is the default, with a flush at every leg
> boundary.** `bot_moveGraphSaveDefer` unset now defers, and `0` restores the
> immediate write. The snapshot is also written at `Move_Plan`'s objective
> change, right after `Move_Reset` prints the census, so a crash loses at most
> the leg in progress. **All six reference cells are decision-identical to
> the deferred arm of the safety-save record**:
>
> - seeded class 125: **15,925 ms**, 11 of 11 leg decisions, 697 compared, 0 mismatches;
> - seeded class 75: **21,125**, 13 of 13, 885 compared, 0 mismatches;
> - v1 class 125: **19,050**, 12 of 12;
> - v1 class 75: **19,150**, 13 of 13;
> - v2 class 125: **19,275**, 12 of 12;
> - v2 class 75: **19,450**, 13 of 13.
>
> **The four production caches are byte-identical to that arm's.** The two
> seeded caches match on every common record except one. The reason is the
> same post-leg play the safety-save record documents. Each world's
> leg-boundary flush wrote exactly the deferred arm's edge count (94,716 and
> 94,714). The world then learned one more edge after the flag, and that
> autosave also carried a later cost update to edge 59,636. With the cvar set
> to 0, v2 class 125 is again decision-identical (19,275 ms). Its cache
> matches every common record and holds three more post-leg edges.
>
> **2. Planner directives for one search, default off.**
>
> - `bot_movePlanAvoid <edges>` adds indices to the avoid list passed to
>   `Graph_PlanState`. The bot's own list is untouched.
> - `bot_movePlanRequire <edge>` adds a required-edge bit (bit 26) to the
>   planner's state key. The bit is set on that edge, inherited, and needed at
>   the goal. If no route satisfies it, the ordinary search runs and the plan
>   line says `require_met=0`.
> - `bot_movePlanGen <n>` limits either directive to the leg's n-th plan.
>
> With none set, the six cells above are unchanged and no plan line carries
> the new fields. In the smoke, the directives change the release route
> exactly as asked.
>
> **3. Cache v3 is v2 plus the edges of the routes the four seeded fast trips
> planned.** It holds **3,332 edges** and its identity is
> **`g1-9a85d509075ecbc1`**. All four seeded tapes replay on the seed graph on
> the new build at their times, with every entry hit:
>
> | Class | Time | Entries hit |
> | ---: | ---: | --- |
> | 75 | 15,950 ms | 11 of 11 |
> | 100 | 16,725 | 11 of 11 |
> | 150 | 15,975 | 10 of 10 |
> | 125 | 15,925 | 11 of 11 |
>
> Their leg routes fold 20 new edges. **All four edges the planner-reach
> record named are now present**:
>
> | Seed index | v3 index |
> | ---: | ---: |
> | 51453 | 3312 |
> | 48185 | 3313 |
> | 41945 | 3314 |
> | 59480 | 3315 |
>
> On v3, with the harness and library off:
>
> - **The floor passes 36 of 36.** Deaths were 0, the owner was `move`
>   throughout, and there were no legacy lines and no forbidden spend.
> - **Both seeded regressions pass**, at 17,500 and 20,700 ms.
> - **Both respawn4 cases at class 50 pass at 16,625 ms.**
> - **Determinism holds.** Class 125 gave **16,375 ms** in four of four worlds
>   and class 75 gave **20,650** in four of four, one decision sequence each.
>
> Compared with v2, **class 125 is 2,900 ms faster and class 75 is 1,200 ms
> slower**.
>
> **v3's own release plan at class 75 takes only part of the seeded route
> unforced.** Its release route shares the seeded route's first eight edges,
> including 3312. It then leaves the seeded route, costing 13,281 ms. The
> in-trip replan at +2,950 takes 3315, 815, 816 and 750 unforced, but it
> replaces 890/888/889 with 2693/1513/893. The seeded route itself is not
> planned whole.
>
> **4. The explorer branches over routes.** The smoke ran at class 75 on v3
> to depth 1, capped at five route worlds, two branches and a beam of 2. It
> drove the bench through 16 worlds:
>
> - **Requiring 815 or 750 installed routes that take them.** Both returned
>   the control's 20,650 ms.
> - **Avoiding 680 gave another route**, confirmed at **19,625**.
> - **The two duplicates were detected**: 816 duplicated 815, and 681
>   duplicated 680.
> - **The depth-1 beam found 17,925 ms**, confirmed by a tape-only replay.
>
> Build `.codex-research/planner-directives-build-20260922-a`. 1,117 source
> hashes and both binaries matched before the first world and after the last.

## What was asked

1. **Deferred save by default**, with a flush at each leg boundary. Keep the
   cvar, so `0` restores immediate saves. The reference cells must be
   identical, and the caches byte-identical to the deferred arm of the
   safety-save record.
2. **Planner directives for one search**, default off:
   - `bot_movePlanAvoid`;
   - `bot_movePlanRequire`, with the required-edge bit in the state key and the
     ordinary search as the fallback;
   - `bot_movePlanGen`.

   With none set, every search must be identical to today's. The tool side
   gets unit tests; the engine regression is the cells.
3. **Cache v3 from planned edges.**
   - Replay the four seeded tapes on the seed graph. They must reproduce with
     every entry hit.
   - Fold their `[ROUTE]` edges, resolved against each world's own saved
     cache, into v2.
   - Report the four named edges, v3's edge count and its identity.
   - On v3: run the floor, both seeded regressions, both respawn4 cases at
     class 50, and determinism at classes 125 and 75.
   - Say whether v3's release plan at class 75 runs the seeded route unforced.
4. **Explorer route branching.** One world per release-route edge avoided and
   per reference state edge required, each a root with its own confirmation.
   Unit tests, then a depth-1 smoke at class 75 on v3.
5. **The eight-class chain on v3.** Withdrawn by the owner mid-session. See
   [Not done](#not-done).

## Binding

| Item | Value |
| --- | --- |
| Build | `.codex-research/planner-directives-build-20260922-a`, manifest `20260922T191855538886Z.sources.json` sha256 `ad5018b2904fb07f6989ce55413a7c8d5da17e0542e788580e267b1cd355a838`; `openjkded.x86.exe` `421853134a9ace11...`, `jampgamex86.dll` `bc22b1622416369...` |
| Sources | 1,117 hashes and both binaries matched before the first world (`manifest-before.json`), between the collection and the smoke (`manifest-mid.json`) and after the smoke (`manifest-after.json`), all under `.codex-research/planner-directives-20260922-a/` |
| Changed C sources | `bot2_graph.c`, `bot2_graph.h`, `bot2_move.c`, `bot2_move.h` |
| Graphs | seed `g1-98eb1e97c24a4e21`, v1 `g1-0e5af7aba9b5e908`, v2 `g1-9631cbeaef2e5ba8`, **v3 `g1-9a85d509075ecbc1`**, each read from `[GRAPH] map_load` |
| Cache v3 | `.codex-research/production-graph-20260922-c/production-graph.txt`, sha256 `f5f58c014caca371c96e09c27d037f7dc21c219dcbe8cf261956bca5bf085e9d`, 1,272,550 bytes; manifest `manifest.json` sha256 `73c08a6aa52d1f8d...`, which binds the base, the output, both tools and each input world's `server.log`, `result.json` and saved cache by sha256 |
| Settings | the decision-tape record's block, `--edges`; pinned cells use `--corner-class` in the window 3,000 to 5,500; library off; search budget off; the seeded cells and tape replays use `--parity 256` |
| Campaign | `run.py` `d378e50c9db7c580...`, `run.sh` `91bf0728a5e8b4d9...`, `analyse.py` `02f0934d9a827c32...`, `progress.jsonl` `f0b2302528da3cb0...`; `analysis.txt` and `analysis.json` beside them |
| Tools | `tools/movement_graph_enrich.py` `238a525ccfec9e44...`, `tools/movement_decision_explore.py` `b19f9624f629557d...` |
| Launch | `Start-Process "C:\Program Files\Git\bin\bash.exe" -l run.sh <phases> <marker>`, hidden: phases `RZT` (`rzt.done`), `R` again (`r2.done`) and `FGSP` (`fgsp.done`), each exiting 0; the smoke from `.codex-research/decision-explorer-v3-smoke-c75-20260922-a.sh` (`.done` exit 0) |

## 1. The deferred save by default

### What changed

- **In `bot2_graph.c`**, `SaveDeferred()` now reads the cvar as a string, as
  `bot_moveState` is read. Unset or positive means deferred. `0` means the
  immediate write on every autosave.
- **In `bot2_move.c`**, `Move_Plan`'s objective-change branch calls
  `Graph_SaveFlush()` right after `Move_Reset`, which prints the tape census
  and the parity tally. Nothing reads the file within a session, so no
  decision can change. With the deferral off, nothing is pending and the
  flush writes nothing.
- **In `tools/movement_cost_runner.py`**, `--graph-save-defer` defaults to 1,
  matching the engine. Before this change the runner passed 0 explicitly and
  would have silently measured the old default.
- **`tools/test_movement_graph_save_defer.py`** compiles the arm with the cvar
  unset, `"0"` and `"1"`, and checks the order of the flush in `Move_Plan`:
  after the reset and before the new objective is taken.

The engine can fire an objective change mid-trip as well as at the flag
(the goal position sliding), so a leg could flush twice. In the worlds checked
here (R-v2-c75, P-v3-c75-r1, F-red_out_L1_stand) there was exactly one
`reason=objective` line and one `cache_flush` line per world. Each flush writes
the last snapshot, and the last write of a session is always the last
autosave's content.

### The cells

The R worlds ran at every default. Each is compared with the `D-*` world of
`.codex-research/safety-save-20260922-a/`.

| Cell | R | D (safety-save) | Leg decisions | Cache |
| --- | --- | --- | --- | --- |
| seed class 125, harness on | 15,925, 697 compared, 0 mismatches | 15,925 | 11 = 11 | flush at the leg boundary wrote 94,716 edges (D's count); final file 94,717, post-leg learn at 21,175 and the cost update to 59,636 it carried |
| seed class 75, harness on | 21,125, 885 / 0 | 21,125 | 13 = 13 | flush wrote 94,714 (D's count); final 94,715, one post-leg learn, no common record differs |
| v1 class 125 | 19,050 | 19,050 | 12 = 12 | **byte-identical** |
| v1 class 75 | 19,150 (`R-v1-c75-a2`) | 19,150 | 13 = 13 | **byte-identical** |
| v2 class 125 | 19,275 | 19,275 | 12 = 12 | **byte-identical** |
| v2 class 75 | 19,450 | 19,450 | 13 = 13 | **byte-identical** |
| v2 class 125, `bot_moveGraphSaveDefer=0` (`Z`) | 19,275 | - | 12 = R's 12 | every common record identical to R's; three more post-leg edges |

**R-v1-c75 needed a second world.** The campaign's pinned-class retry rule was
copied from the safety-save runner, and it counted any reported release level
as landed. The first world's four attempts all missed class 75. It reported
the last miss's level, 4,200 (class 0), with `runner_error`, and the rule did
not retry. The rule now requires `level mod 200` to equal the class. The world
is retained, and `R-v1-c75-a2` is the cell.

## 2. Planner directives

### What changed

**In `bot2_graph.c`:**

- `Graph_PlanState` gains `int require`, which is -1 for none.
- Each state entry carries `taken`. `StateKey` puts it in bit 26.
- `StateRelax` sets it when the relaxed edge is `require`, and otherwise
  copies it from the parent. Every relaxation goes through `StateRelax`: links,
  diagonals, retained edges and strafe edges.
- The goal test is `cell==to && (require<0 || taken)`.
- An index past the graph returns 0.
- With `require` -1, `taken` is always 0. The key, the hash, the table and
  the order are then bit-identical to the previous search.

**In `bot2_move.c`:**

- `PlanDirectivesSync` parses the three cvars once per server frame, never
  inside a search.
- `PlanStateDirected` is called by `Replan` (for the leg's plan number
  `path.replans`) and by `RerouteFrom` (for `path.replans+1`, the number the
  route would carry if installed). It builds a local avoid array of the bot's
  own list plus the directive's new indices, runs the constrained search, and
  falls back to the ordinary one.
- `bot_movePlanGen 0` applies the directives to every plan. The required edge
  is then dropped for the rest of the leg once it is spent: a committed state
  plan named it, or the executor passed it on the route (`movement_t.requireSpent`,
  cleared by `Move_Reset`).
- A plan that a directive applied to appends
  `directive=<bits> plan_avoid=<n> require=<edge> require_met=<0|1>` to its
  plan line, or to the reroute's plan line. **An undirected plan line is
  byte-identical to today's.**

**The plan numbering counts from `Move_Reset`.** Plan 1 is therefore the
release route in a fixture world. It is also the first plan of the bot's free
play before the release, and a later mid-trip objective change restarts the
count. Directives never touch the graph (`Graph_EdgeResult` is not called), so
the release's learned-set reset is unaffected.

**Regression.** The six reference cells plus Z carried no directive fields,
and each matched its reference decision for decision (table above). The tool
side is tested in section 4. A directive's effect was measured in the smoke,
and every release route there shows it (section 4).

## 3. Cache v3

### The replays

Each tape ran on the seed graph with the explorer's settings (harness on) and
`--decision-tape`, one world each, sequential.

| Tape | Class | Release | Time | Census | Leg routes |
| --- | ---: | ---: | ---: | --- | ---: |
| `decision-explorer-c75-20260922-a/tapes/c75-best.tape` | 75 | 3,675 | **15,950** | 11 of 11 hits, 0 refused, 0 absent | 2 |
| `decision-explorer-c100-20260922-a/tapes/c100-best.tape` | 100 | 3,100 (`T-seed-c100-a2`) | **16,725** | 11 of 11 | 7 |
| `decision-explorer-c150-20260922-a/tapes/c150-best.tape` | 150 | 3,350 | **15,975** | 10 of 10 | 3 |
| `decision-tape-verify-20260922-g/tapes/c125-self.tape` | 125 | 3,125 | **15,925** | 11 of 11 | 4 |

Every time matches the tape's own campaign, so the new build is also proven
against the seed. `T-seed-c100` missed its class on all four attempts, and the
bench's own retry (level `None`) produced `-a2`.

The class-75 world's two leg routes are the ones the planner-reach record
reconstructed offline:

- the release route (level 3,700, 13,145 ms), which carries 51453, 48185 and
  41945;
- the in-trip replan (level 6,650, 12,206 ms), which carries 59480, 62163,
  72937 and 52256.

### The fold

`tools/movement_graph_enrich.py` gains a third input group, `--routes`. It
works as follows:

- `read_world` keeps the `[ROUTE]` lines.
- `resolve_routes` joins each route with `read_routes`.
- A route whose `graph=` is not the identity the world loaded is refused
  whole (`other_graph`), as is a route missing a part (`incomplete`).
- Each index is resolved against the world's own saved cache by the same
  rules as a commit's (`beyond_cache`, `dropped_by_reset`, `added_mismatch`),
  at the route's own level. The rules are now factored into `refusal()`.
- `--routes-leg` keeps only routes installed from the case's release to its
  recorded completion. The completion comes from the case's `result.json`, or
  from the run summary one level up. This excludes the bot's pre-release free
  play and its play after the flag.

v3 was produced with that flag, from the four case directories only (the miss
worlds are not inputs):

```
python tools/movement_graph_enrich.py .codex-research/production-graph-20260922-b/production-graph.txt \
  --out .codex-research/production-graph-20260922-c/production-graph.txt \
  --manifest .codex-research/production-graph-20260922-c/manifest.json \
  --routes <T-seed-c75, T-seed-c100-a2, T-seed-c150, T-seed-c125>/red_out_L1_stand --routes-leg
```

| World | Distinct route edges | In v2 | New | Routes outside the leg |
| --- | ---: | ---: | ---: | ---: |
| class 75 | 28 | 24 | 4 | 2 |
| class 100 | 48 | 35 | 13 | 2 |
| class 150 | 37 | 32 | 5 | 2 |
| class 125 | 43 | 33 | 10 | 6 |
| **union** | **73** | 53 | **20** | |

No index was refused. The fold uses `movement_graph_merge`'s identity, so v2's
order comes first and the 20 new edges are appended. Five v2 records were
replaced by a cheaper seed measurement of the same key. **v3 has 3,332 edges:
3,274 strafe, 48 hop and 10 pad.** Its identity, **`g1-9a85d509075ecbc1`**, was
read from `[GRAPH] map_load` in every v3 world.

**The four named edges**, matched by fold identity:

| Seed | from->to | v2 | v3 |
| ---: | --- | --- | ---: |
| 51453 | 8826->8555 | absent | **3312** |
| 48185 | 4851->10422 | absent | **3313** |
| 41945 | 7866->8842 | absent | **3314** |
| 59480 | 8623->8133 | absent | **3315** |

62163, 72937 and 52256 are v2's 815, 816 and 750, as before.

The tool's regression adds six tests on hand-built worlds:

- a route edge that no commit names resolves against the world's own cache;
- a route on another graph, and a route missing a part, are refused;
- a pre-release route index that the reset dropped is refused;
- leg-only folding keeps only the leg's routes;
- a world with no completion folds nothing under leg-only;
- `main` takes `--routes`.

The test fixtures moved into a mixin, so no test runs twice.

### v3 in the production configuration

All worlds ran on v3 with the harness and library off and the save deferred
by default.

| Check | Result |
| --- | --- |
| Floor, one bench call per case, unpinned | **36 of 36**, deaths 0, owners `move` only, 0 legacy lines, no forbidden spend (Speed samples only in the L2/L3 cases, zero in every L1 case); one hold, `red_out_L3_stand`, pass 12,300 ms |
| Seeded regressions | `red_ret_L1_stand` **17,500 ms**, `blue_out_L1_stand` **20,700** |
| respawn4 at class 50 | `red_ret_L1_respawn4` **16,625** (release 3,650), `blue_out_L1_respawn4` **16,625** (3,450); 0 deaths, owner `move`, no hold |
| Class 125, four worlds | **16,375 ms**, four of four, 11 leg decisions, one sequence; releases 3,925 / 3,125 / 3,925 / 3,925 (two worlds needed `-a2` for the class pin) |
| Class 75, four worlds | **20,650 ms**, four of four, 14 leg decisions, one sequence; releases 3,675 / 3,075 / 4,675 / 4,675 |

**Class 125 on v3 is 2,900 ms faster than on v2** (16,375 against 19,275), and
only 450 ms behind the seeded 15,925. **Class 75 is 1,200 ms slower** (20,650
against 19,450). The ranking takes the new edges at both classes, and they help
one class and cost the other. That is a ranking result; whether v3 offers
faster chains is the explorer's question.

### Does v3's release plan follow the seeded route unforced?

**Partly, not whole.** In v3 indices:

| Route | Edges | Cost |
| --- | --- | ---: |
| seeded release (T-seed-c75, level 3,700) | 680 681 3312 758 759 979 996 997 **926 3313 867 868 3314 815 816 750** | 13,145 on the seed |
| **v3 class-75 release** (P-v3-c75, level 3,700) | 680 681 3312 758 759 979 996 997 **2315 3083 3107 3275 1025 299 1057 808** | **13,281** |
| seeded in-trip (level 6,650) | 782 783 920 784 902 903 **890 888 889** 891 749 3315 815 816 750 | 12,206 on the seed |
| v3 class-75 in-trip (level 6,650) | 782 783 920 784 902 903 **2693 1513 893** 891 749 3315 815 816 750 | - |

**At the release, the unforced plan now takes 51453 (3312) and the seeded
route's first eight edges.** It then leaves the seeded route for edges v3
gained from the other classes' routes (2315, 3083, 3107, 3275). Its cost,
**13,281 ms**, is exactly the planner-reach record's offline price for "v2
plus the 3 edges, unrestricted", while the record priced the seeded route on
v2's records at 12,995. That is the planner's known inexactness, and it now
shows on the live engine.

**In the trip, the unforced replan takes 59480 (3315) into 815, 816 and 750**,
as the record predicted for v2 plus 59480. It still reaches them through
2693/1513/893 rather than 890/888/889. The trip then replans eight more times
and completes at 20,650 ms.

## 4. Explorer route branching

### What changed

`tools/movement_decision_explore.py`, all of it behind `--route-branch`:

- **`release_route(text, release)`** returns the leg's first plan: the first
  `reason=path` line at or after the release with `replans=1`. It returns that
  plan's `[ROUTE]` edges (same generation, parsed by the enrich tool's
  `read_routes`), with `cost_ms` and the directive fields. `summarize` records
  it on every world.
- **`route_taken(route, directive)`** holds when:
  - the plan line says a directive applied;
  - every avoided edge is absent (with `plan_avoid` > 0);
  - a required edge is present with `require_met=1` and the right `require`.
- **`reference_edges(tape, reference_graph, explored_graph)`** reads the
  tape's state entries, `edge` and `edge2`. It reads each record from the
  graph the tape was recorded on: by default the saved cache of the tape's own
  world, named on its `# source=` line, or `--reference-graph`. It matches
  each in the explored graph by `movement_graph_merge.edge_key`. Unmatched
  indices are reported and never guessed. For the four seeded tapes, all state
  edges resolve on v3:
  - class 75: 815, 816 and 750;
  - class 100: 680, 681 and 793 to 796;
  - class 150: 680 and 681;
  - class 125: 680, 681, 782, 783, 800, 728 and 729.
- **`Campaign.route_roots(control)`** runs one world per offer, with the
  directive for plan 1 (`bot_movePlanGen=1`). The offers are, in order:
  - each resolved reference edge the control's release route lacks
    (`<root>-rq<edge>`);
  - each edge of the control's release route (`<root>-av<edge>`).

  A world whose route did not take the directive is a **route miss** and is
  retained. A world whose release route an earlier root already took is a
  **duplicate** (`route_duplicate`). Every other world is confirmed by a fresh
  world that runs its whole leg as a tape **under the same directive**.
- **Beam and inheritance.** The control and the confirmed route roots compete
  for the depth-0 beam by confirmed time. Every world grown from a route root
  carries its directive, including its children, their prefixes and all
  confirmations. `usable()` requires `route_taken` for a directive world, and
  `score_time()` reads the confirmation for a root as it does for a branch.
- **Worlds and report.** The directive rides on the record and reaches the
  bench through `directive_settings` (`bot_movePlanAvoid`,
  `bot_movePlanRequire`, `bot_movePlanGen`). The execute callable receives it
  as a keyword only when one is set, so existing callers are unchanged. The
  report adds `route_branch`, `reference`, `reference_unresolved`,
  `route_worlds` and the best world's `directive`.
- **Budget control.** `--route-branches N` caps the route worlds for a smoke.

Without `--route-branch` the explorer runs exactly as before. The existing 44
tests pass unchanged, and one new test asserts it.

**Ten new tests** on hand-built logs:

- the release route is the leg's first plan, not the pre-release plan or a
  later replan;
- the avoid and require acceptance rules;
- the bench command carries the directive;
- reference resolution by fold identity, both through `--reference-graph` and
  through the tape's own world;
- the offer order, duplicates, route misses, and confirmations run under the
  directive;
- roots compete for the beam and children inherit the directive;
- a capped run keeps the named inclusions;
- nothing changes without the flag.

### The smoke

`.codex-research/decision-explorer-v3-smoke-c75-20260922-a/` ran class 75 on v3
with the c75 seeded tape as reference, `--max-depth 1 --max-branches 2
--beam 2 --route-branches 5`, detached from its `.sh`. It ran 16 worlds, one
at a time, and stopped at `max depth 1`.

| World | Directive | Release route (cost) | Time | Outcome |
| --- | --- | --- | ---: | --- |
| `control` | none | 680 681 3312 ... 1057 808 (13,281) | 20,650 | control |
| `control-rq815` | require 815 | ... 997 3033 3076 3077 1632 966 863 815 816 750 (13,877), `require_met=1` | 20,650 | root, confirmed 20,650 |
| `control-rq816` | require 816 | the same route | 20,650 | duplicate of rq815 |
| `control-rq750-a2` | require 750 | ... 1057 750 (13,291), `require_met=1` | 20,650 | root, confirmed 20,650 (first attempt missed the class) |
| `control-av680` | avoid 680 | 719 720 699 789 3001 2706 902 903 2693 1513 893 891 749 3315 815 816 750 (14,174) | **19,625** | root, confirmed **19,625** |
| `control-av681` | avoid 681 | the same route as av680 | 19,625 | duplicate of av680 |

- **Depth-0 beam:** `control-av680` (19,625) and `control` (20,650).
- **Depth 1:** `control-d1c1` reached **17,925 ms** and its tape-only
  confirmation reproduced it with every entry hit. `control-av680-d1c3`
  reached 19,100, confirmed.

**The engine side is exercised end to end.**

- The planner takes a required edge it would not choose: 815 at a planned
  +596 ms, and 750 at +10 ms.
- An avoided edge produces a different route.
- The plan line reports both.
- A route root's decisions reproduce under a tape plus the directive.

**Two observations for the chain.**

- **Requiring 815 does not bring in the seeded 3313/3314 approach.** The
  planner reaches 815 through 3033/3076/3077 at 13,877 ms. The chain's
  route-branching will explore what the planner offers, not the seeded route
  itself.
- **The route roots here are no faster than the control's own depth-1
  branch.** At this budget the forced-index beam still found the best time.

## Not done

- **Task 5, the eight-class chain on v3, was not launched**, by owner
  decision during the session. Its script is written but has never run:
  `.codex-research/decision-explorer-v3-chain-20260922-a.sh`, sha256
  `da79279333d80b17...`. [HANDOFF](../HANDOFF.md) gives the launch and its
  pre-launch checks.
- **A directive-carrying tape in the library.** A route root's result is a
  tape plus a directive, and the tape library selects tapes only. Until the
  library can carry a directive, such a result is an explorer measurement, not
  a library entry.
- **Row 5 on v3.** The cost was not measured. The deferred default is
  expected to hold the saving the safety-save record measured, but no cost
  world ran on this build.

## What this does not establish

- **That v3 is faster.** One class got faster and one slower under the
  ranking. The explorer campaigns are the measurement.
- **That the leg-boundary flush bounds a real crash.** No crash was induced.
  The flush's own prints show one write at the objective change in each world checked, and the
  compiled regression shows each write is the last autosave's content.
- **Whole-cache identity of the seeded cells in one world.** The post-leg play
  differs between worlds, as in the safety-save record.
- **Optimality of any route here.** The planner is not exact. With 3313 and
  3314 present it still does not return the seeded release route.

## Reproduce

From the repo root:

```
python tools/strafe_fixture_build.py --out .codex-research/planner-directives-build-20260922-a
python .codex-research/planner-directives-20260922-a/verify_sources.py .codex-research/planner-directives-20260922-a/manifest-before.json
# detached: Start-Process "C:\Program Files\Git\bin\bash.exe" -l .codex-research/planner-directives-20260922-a/run.sh RZT rzt
# then the enrichment command above, then run.sh FGSP fgsp, then the smoke .sh
python .codex-research/planner-directives-20260922-a/analyse.py
python -m unittest discover -s tools -p "test_*.py"
```

## Tools and files

- **C:** `codemp/game/bot2_graph.c`, `bot2_graph.h`, `bot2_move.c`,
  `bot2_move.h`.
- **Tools:**
  - `tools/movement_graph_enrich.py`, which gains `--routes` and `--routes-leg`;
  - `tools/movement_decision_explore.py`, which gains route branching;
  - `tools/movement_cost_runner.py`, whose save default is now 1.
- **Tests:**
  - `tools/test_movement_graph_enrich.py`;
  - `tools/test_movement_decision_explore.py`;
  - `tools/test_movement_graph_save_defer.py`;
  - `tools/test_movement_cost_runner.py`.
- **Suite:** 887 tests, OK, from the repo root.
- **Campaign scripts:** retained beside the worlds. They are not project
  tools.
