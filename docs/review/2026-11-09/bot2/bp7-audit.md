# BP-7 audit: every writer of what the deferred save snapshot copies

Read-only audit of worktree `claude/bot2-review-next` at `bc503e8f` plus the uncommitted working change another
session is making in `GuideFold`/`GuideFor` (+20 lines at ~2303; it touches no edge writer). Line numbers are the
working tree's `codemp/game/bot2_graph.c` unless noted; subtract 20 for HEAD past line 2303. Nothing was built or run.

## 1. What the snapshot copies and what reaches the file

`Autosave` (388-391) copies `graph.edge[0..edgeCount)` whole (`sizeof(graphEdge_t)` = 100 bytes on this layout,
so 94,711 Kejim edges ~ 9.5 MB), plus the scalars `edgeCount` and `capped`. `Graph_SaveFlush` (394-404) hands the
snapshot to `SaveEdges` -> `WriteCache` (3297-3327). Cells, nodes, reasons and every other header field are read from
the live `graph` at flush time, not from the snapshot.

`WriteCache` writes, per edge (3318-3323):

| Saved (needs marking when changed in place) | Copied but never written (runtime only) |
|---|---|
| `type from to takeoff[3] landing[3] dir[2] costMs validated tap entrySpeed entryDir[2] exitSpeed exitDir[2]` | `traversals failures disabled offUntil nextFrom` |

So 80 of the 100 bytes per edge are saved. Writers that touch only the right-hand column never change the file and
need no mark (the snapshot's copy of them goes stale, which nothing reads: `SaveEdges` is the snapshot's only reader).
`edgeCount` and `capped` are scalars copied on every `Autosave` and need no marking.

## 2. Every writer

Searched: all `graph.edge[...]` and `graphEdge_t *` (non-const) uses in `bot2_graph.c`; every `e->`/`x->`
assignment, `++`/`--`, `VectorCopy(..., e->...)`, `memset`/`memcpy`/`memmove`/`qsort` in `bot2_graph.c`; every
`graphEdge_t` and `graph.` use in all other `codemp/game/*.c,*.h`. Outside `bot2_graph.c` nothing writes edges:
`Graph_Edge()` returns `const graphEdge_t *`, no cast removes the const (only `bot2_move.c:6730` names the type, in a
comment), and the other files mutate only through the exported functions below. There is no sort, compaction,
removal, or reorder of `graph.edge[]` anywhere; the only shrink is `ResetLearned`'s truncation, the only wholesale
writes are `Reset`'s `memset(&graph)` and the cache `Load`.

### 2a. Writers of saved fields (these need a mark)

| # | Line | Function | Fields | Index known | Notes |
|---|---|---|---|---|---|
| W1 | 660-663 | `AddEdgeRecord` | whole record (`memset`), all saved fields, `nextFrom` | yes, `graph.edgeCount` before `++` | The single append point. Callers: `Graph_AddEdge` 669 (bot2_move.c:2658 hop learn), `Graph_AddStrafeEdge` 706 (bot2_move.c:2616, 7991), `Climbs` 841, `LiftEdge` 873, `NativeEdge` 1021 (`Pads`/`DropsAndJumps`), `Hints` 1268, `Load` 3474. An append lands at index >= the snapshot's `edgeCount` except after a `ResetLearned` truncation. |
| W2 | 681-682 | `SetStrafeState` (takes `graphEdge_t *`) | `tap entrySpeed entryDir exitSpeed exitDir` | via pointer (`e-graph.edge`) or at caller | Callers: 700 (merge, in place), 709 (just-appended index, covered by W1), 3475 (`Load`, covered by "all"). Only the 700 call is an in-place change. |
| W3 | 699-700 | `Graph_AddStrafeEdge`, merge branch, `costMs<x->costMs` | `takeoff landing dir costMs` + W2's fields | yes, `e` | Followed by `Autosave`. The unconditional `x->traversals++` at 695 is runtime only. |
| W4 | 723 | `Graph_EdgeCost` | `costMs` | yes, `edge` | **No `Autosave` here**, but the next autosave's snapshot carries it (the save-defer test's `Session` exercises exactly this: cost 875 on edge 0 between two autosaves is in the file). Called from bot2_move.c:2652 (each measured hop), 2812 (pad), 3185 (lift): the most frequent in-place writer, on arbitrary (mostly loaded) indices. Mark only when the value changes. |
| W5 | 732 | `Graph_EdgeResult` | `validated` 0 -> 1 (and `traversals`, runtime) | yes, `edge` | Followed by `Autosave`. Mark only inside the `!e->validated` branch. |
| W6 | 874 | `LiftEdge` | `tap` = lift model | yes, `e` returned by W1 | Same index W1 just marked; no extra mark. |
| W7 | 458, 460 | `ResetLearned` (via `Graph_ResetLearned`, only from bot2_corner.c:325 `bot_corner_start`) | restores whole records of touched loaded indices; truncates `edgeCount`; rewrites every `nextFrom` (464) | touched indices known; truncation shrinks the count | Mark each `touched.index[i]` or mark all. Rare (bench release only), so "all" is fine. Followed by `Autosave` when anything changed. |
| W8 | 480 (`Reset`: `memset(&graph,0,...)`) | `Reset` | everything | n/a | Reached by `Move_PrepareMap` 3561, `Build` 3529, `Load` failure 3488, `LoadCache` pre-lift 3503, `Svcmd_MoveGraph_f rebuild` 3595 (via `Build`). |
| W9 | 3474-3475 | `Load` | all saved fields for every edge (through W1/W2) | all | Runs after `Reset`. |
| W10 | 3573, 3576 | `Move_PrepareMap`, loaded branch: `Climbs()` with `bot_moveClimbs>=2`, `Lifts()` on a pre-ride cache | appends via W1 | yes (W1) | **No save follows**; the edges reach the file with the session's first autosave. Covered by W1 and by `Reset`'s "all". |

### 2b. Writers of runtime-only fields (no mark needed)

| Line | Function | Fields |
|---|---|---|
| 204 | `OffExpire` (via `Graph_Generation`, searches) | `offUntil=0` |
| 464 | `ResetLearned` | `nextFrom` for every edge |
| 663 | `AddEdgeRecord` | `nextFrom` of the new edge (plus `cell[from].firstEdge`, a cell field the file does not write) |
| 695 | `Graph_AddStrafeEdge` merge | `traversals++` (also when the cost is not lower: no saved change) |
| 732 | `Graph_EdgeResult` | `traversals++` |
| 762-768 | `Graph_EdgeFail` | `failures`, `disabled`, `offUntil` |
| 3572 | `Move_PrepareMap` | `disabled=1` on climbs when `bot_moveClimbs<=0` (the comment there already says it is not saved) |

Reads that looked like writes and are not: 1116-1117 (`Graph_PadReachable`), 1388-1404, 1519, 1552, 1608, 1846,
1890, 1940, 2056, 2083, 2268-2313, ~2540, ~2787-2900 (path tables read `costMs`; `pathOld`/`pathDecrease` are their own
arrays), ~3041, 3517, 3608. Lines 1802, 1824, ~2518 write state-search entries, not graph edges.

`graph.capped` writers (337, 460, 611, 658, Load 3442): scalar, copied on every `Autosave`, no mark.

Cells and nodes: the snapshot does not copy them; the flush reads them live. Their saved fields are written only by
`AddCell`/`Nodes`/`Load` during a build or load (`cell.firstEdge` changes at runtime but is not written). Unchanged by
this fix.

## 3. Recommendation

### (a) Is a low-water mark safe? Yes, but it buys little

Safe if the copy range is `[min(lo, saveSnapshot.edgeCount), graph.edgeCount)` and `lo` resets to "none" after each
copy, with marks at W3, W4, W5 (index known) and "all" at W7/W8. Appends past the snapshot's count are covered by the
range itself.

It does not fix the cost in practice. Most in-place writes land on loaded seed edges at low indices: W4
(`Graph_EdgeCost`) runs on every measured hop, pad and lift and W5 on every first validation of a loaded candidate,
so between two autosaves `lo` usually falls far below the append point and the next autosave copies most of the
array again. The finding's own count ("388 edge adds plus cheaper merges and first validations") includes the merges
and validations, which are exactly the low-index cases.

**Use a dirty list instead**, same mark sites: a `GRAPH_MAX_EDGES`-bit (16 KB) or byte bitmap plus an index list
(e.g. 1,024 entries; overflow sets "all"). `Autosave` copies each listed index below `saveSnapshot.edgeCount`, then
`[saveSnapshot.edgeCount, graph.edgeCount)`, then clears the bits it listed. Cost per autosave becomes tens of
records instead of 94,711. File output is byte-identical to today; snapshot memory differs only in the runtime-only
fields, which nothing reads.

### (b) Mark "all"

- `Reset` (W8): covers map load, cache `Load`, `Build`, `move_graph rebuild`, the post-load `Climbs`/`Lifts`
  (W10), and a stale snapshot from the previous map. Initial state must also be "all".
- `ResetLearned` (W7): truncation plus restored records; rare, so "all" (or mark the touched indices in its loop).
- List overflow.
No sort, compaction or removal exists to handle.

### (c) Count

**5 one-line call sites**: W3 (merge), W4 (`Graph_EdgeCost`, when the value changes), W5 (first validation),
plus "all" in `Reset` and `ResetLearned`. Optional 6th for robustness: a mark in `AddEdgeRecord` (W1), which makes
appends correct even if the range rule is ever dropped. W2, W6, W9, W10 are covered by these. The ~30 sites from the
first look are mostly reads and runtime-only writes (table 2b).

Plus the test: `tools/test_movement_graph_save_defer.py` extracts `Autosave` and the `saveSnapshot` struct by regex
and its `Session()` writes `graph.edge[0].costMs`, `graph.edge[1].validated` and `graph.edge[2].costMs` directly.
It must extract the mark helper and call it where it simulates `Graph_EdgeCost`/`Graph_EdgeResult`. Kept as is, it
would fail (edge 0's 875 would be missing from the deferred file), which is the right failure: it is the regression
for this class. Worth adding a case that marks a low index after an append-only autosave, and a `ResetLearned`-style
truncation followed by re-append.

### (d) Simpler alternatives considered

- **Snapshot only the saved fields**: 80 of 100 bytes, a 20% saving. Not worth a second struct.
- **Write the live graph at flush (no snapshot)**: changes the file. The design (comment at 348-363, the save-defer
  test's last case) pins the file to the last autosave's content; W4 costs and W10 appends after the last autosave
  would be written. That changes the next session's loaded graph, so it is a behaviour change, not a perf fix.
- **Undo log since the last autosave** (copy-on-write at the mutator, `TouchEdge`-style): `Autosave` becomes O(1)
  (record count and capped, clear the log); each saved-field mutator pushes the pre-change record once per index;
  the flush writes the live array truncated to the autosave's count with logged records substituted. Same five mark
  sites, cheaper autosaves, but the flush path and its test get more complex and a log overflow still needs a full
  copy. The dirty list is the simpler of the two and enough.
- **Reuse `TouchEdge`** as the mark: not directly. It covers W3/W4/W5 but also `Graph_EdgeFail` (runtime only, would
  enlarge copies), skips appended indices (`index>=loadedEdges`) and drops marks past 256 (`touched.overflow`). A mark
  call next to it is cleaner.
