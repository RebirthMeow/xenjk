# Lossless speed-ups of the bot stack (2026-10-03)

Owner (2026-10-03): "thread is a bit of a nuclear option, can we study the algos and methods used for the whole bot
stack and see if there are any performance wins without any quality loss?"

**Rule:** a change ships only if the bots decide exactly what they decided before. `tools/laps_identity.py` compares
two runs of one seeded Dantooine 4v4 world: the game log, and every decision line (`[MOVE]`, `[ROUTE]`, `[DEFENDER]`,
`[RUNNER]`, `[GRAPH]`, `[SPIKE]`) with machine measurements (timings, cache counters) masked, up to the frame both
runs reached. Dantooine seeds replay exactly; Kejim seeds do not across runs (records/2026-10-03-hud-knowledge.md).
A negative control (two different builds' worlds) fails it at the first diverging decision.

## Study

Four read-only studies (forecast engine, movement searches, graph planners, per-frame logic) and the slow-frame
profile ([B3 record](2026-10-03-b3-dantooine.md#where-the-slow-frames-go-profile-build-d)). The planner's slow replans
are not failed searches (`body_fallback=0`): they are successful searches of 28,000-36,000 states at about 6.5 us a
state. Candidates that would change a result were rejected: a tighter or admissible heuristic, a different heap (tie
order), bounding candidates against the current best, search budgets, caching traces across frames.

## Changes (all exact)

Graph planners (bot2_graph.c):
- the heuristic memoised per search and goal;
- `BodyBlocks` memoised per cell, dropped when the bodies are set;
- `StateWalk` (the walk law, 6-15 iterations, once per neighbour link) memoised by the bits of its inputs;
- the avoid list marked per edge at search start instead of scanned per edge;
- each listed strafe edge's tap, entry speed and entry direction copied into the near-list arena (dropped with it),
  so the per-state filter reads one record instead of the 9.5 MB edge array;
- the state table's slot holds stamp, index and key in one record, the heap key beside its entry;
- `Graph_Reaches`: reachability sets for the last 8 goals, keyed on a topology counter that only edge adds, fails,
  expiries, toggles and loads bump (it was rebuilt, 95k edges and a 14k-cell flood, on nearly every search).

Forecast (bot2_forecast.c): point contents cached in the trace cache's scope; the trace-cache hash word at a time;
the performance-counter frequency read once; the in-use trigger entities listed once per guard scope.

Per frame: the bot's role (`DefenderSlot`, `Bot2_IsMovementOnly`) memoised by its raw name (the movement asked per
simulated step); the base flag entity cached (`Bot2_BaseFlag`; re-validated per use, never cached on a map with two);
the armed top-up rearm check stops at the first qualifying item instead of routing up to eight; the direct route of
a chase-mode rearm computed once per evaluation instead of once per candidate.

## Measured

Identity: batches 1 and 2 identical on Dantooine seeds 1, 4, 8, 12 (about 820,000 decision lines each batch) and in
the timing pairs.

Paired frame cost, build h against perf build b (batches 1+2), one world at a time, 25x, same seeds (the owner's Bot3
training ran on the same machine throughout; pairs alternate, so they share the load):

| Dantooine seed | mean per frame, before -> after | p99 | frames over 25 ms |
|---|---|---|---|
| 1 | 7.86 -> 6.69 ms (-15%) | 57.5 -> 49.5 ms | 1,865 -> 1,423 (-24%) |
| 2 | 8.80 -> 7.93 ms (-10%) | 55.0 -> 49.3 ms | 1,821 -> 1,533 (-16%) |
| 3 | 7.09 -> 6.38 ms (-10%) | 52.5 -> 47.0 ms | 1,642 -> 1,371 (-17%) |

Batch 3 (perf build c: the planner's table and heap layout, the per-scope trigger list) is identical on seeds 1, 2,
3 and 4; its own gain is small, inside the run-to-run drift of the machine (a second before-run of seed 1 an hour
later was 8% cheaper than the first). Final pairs, build h against perf build c: below.

## Seeded replays and setup stalls

A perf build c world of seed 1 differed from line 2: its first bot connected at 0:03 instead of 0:02, after a 3.1 s
`Hitch warning` during setup (the first world of a fresh build, its binaries read cold from the HDD). The seeded
setup's `botat` commands fire at fixed game times, but a stall of seconds during setup moves them to other frames,
and the world diverges from there. Worlds whose setup did not stall replayed exactly. This is the first lead for
the Kejim non-replay (a larger map, longer loads): make the seeded setup wait until loading has finished, or treat
a world whose setup stalled as unreplayable.

| Dantooine seed (build h -> perf build c) | mean per frame | p99 | worst frame | frames over 25 ms |
|---|---|---|---|---|
| 2 | 8.68 -> 7.77 ms (-10%) | 53.5 -> 48.0 ms | 132 -> 118 ms | 1,787 -> 1,440 (-19%) |
| 3 | 7.09 -> 6.23 ms (-12%) | 52.8 -> 46.0 ms | 167 -> 118 ms | 1,632 -> 1,289 (-21%) |

Both pairs identical (294,010 and 201,062 decision lines).

## Where this leaves the hitching

Lossless work takes 10-15% off the mean and a fifth of the over-budget frames, but the p99 is still about 46-48 ms
against the 25 ms frame: the tail is single long searches, and making each a little cheaper does not bring a
150 ms search under 25 ms. Studied and not yet done, each exact but each smaller or riskier: rollouts of duplicate
candidates (about 10% of rollout frames; skipping one must also keep the search budget's count and the shared last
result as they were), cvar reads by name in the per-frame paths, the cell planner's parallel arrays, the whole-world
copy per guard scope, the autosave's full edge copy on learn frames. The tail itself needs a search to finish
outside the frame it starts in (the owner calls a worker thread the nuclear option; spreading a search over frames
changes when decisions land, which is a quality question for the owner).
