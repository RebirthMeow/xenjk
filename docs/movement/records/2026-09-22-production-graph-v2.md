# Production graph cache v2: the edges real runs committed, folded back in

Recorded 2026-09-22. The [production explorer's first classes](../../archive/movement-2026-10-05/PLAN.md#next-work-order-2026-09-22-the-decision-explorer)
converged about 1,500 ms behind the research seed at every class on cache v1
(class 75 17,650 ms, 150 17,125, 100 17,900, 125 16,275; all four above row 2's
16,500 bound), and the seed's fast trips used state-plan edges v1 lacks. The
plan's answer is enrichment that keeps the graph made of real runs: resolve
every graph edge a retained world committed against that world's own saved
cache, fold the records into v1, and re-run the floor, determinism and the
explorer on the result.

> **Cache v2 is v1 plus 2,636 edges from real runs: 3,312 edges, identity
> `g1-9631cbeaef2e5ba8`.** Across twelve campaign directories, 2,806 worlds and
> 26,716 commit lines, **12,079 committed edge indices** were named and **every
> one resolved** against its world's own cache, with none refused. The seven
> seeded campaigns contribute **470 committed edges v1 did not have** (plus two
> hits already in v1). The five cache-v1 explorer campaigns committed only
> edges v1 already had (114 to 116 distinct across them, all in v1), and their saved
> caches contribute **2,194 learned edges** past the loaded 676, 2,166 of them
> not also among the committed ones. Folded with `movement_graph_merge.py`'s
> identity: 3,312 edges (3,254 strafe, 48 hop, 10 pad) over the same 14,249
> cells and 1,398 nodes, eight v1 records replaced by a cheaper measurement of
> the same maneuver. The identity is the engine's, read from `[GRAPH] map_load`
> (`cached=1 edges=3312`) in every world that loaded it.
>
> **The indices are the edges flown.** On 280 sampled commits in three
> campaigns (seeded gate, seeded explorer, v1 explorer), the resolved first
> edge's takeoff lies within **64 units** of the bot's position on the commit
> line in every case (median 36 to 50), against a median of about 2,000 units
> for an unrelated edge of the same cache.
>
> **The floor holds on v2.** 36 of 36, one bench call per case, sequential:
> zero deaths, movement owner only, no forbidden spend. **Both respawn cases
> the research seed killed pass**: `red_ret_L1_respawn4` **16,125 ms** and
> `blue_out_L1_respawn4` **16,150 ms**. Both seeded regression cases pass:
> `red_ret_L1_stand` 19,650 and `blue_out_L1_stand` 20,700.
>
> **A start class is still a cell on v2, and v2's cells are slower than v1's.**
> Six worlds at class 125 return **19,275 ms** and one 12-decision sequence,
> from release levels 3,725, 3,925, 4,325 and 4,925; four at class 75 return
> **19,450 ms** and one 13-decision sequence, from 3,475 and 3,675. On v1 the
> same cells were 19,050 and 19,150. The ranking now takes enriched edges from
> the first decision (edge pair 680/681 at both classes), and it is 225 and 300
> ms slower for it. That is a ranking result, not an explorer result; the
> explorer chain on v2 is running.
>
> 1,117 source hashes matched the working tree before the first world and
> again after the last. 52 bench calls, 725.4 seconds of bench time, every world
> retained. No C source changed.

## What was asked

1. Write `tools/movement_graph_enrich.py` with a unit test; resolve every
   `reason=sequence` commit's `index`/`index2` in every retained world of the
   listed campaigns against that world's own saved cache, confirming first that
   `Graph_Save` writes edges in index order and whether disabled or dropped
   edges keep indices aligned; also fold the learned edges of the cache-v1
   explorer worlds; fold everything into v1 with the merge tool's identity;
   report per campaign, and save v2 with a manifest binding every input by
   hash.
2. The 36-case floor unpinned and both seeded regressions on v2, library and
   harness off: zero deaths, owner `move`, no forbidden spend, both
   seed-killed respawn cases passing. Stop before 3 if any case dies.
3. Determinism on v2: six worlds at class 125, four at class 75.
4. Launch the eight-class explorer chain on v2, detached, and return.
5. This record, pointers in EVIDENCE and HANDOFF. PLAN.md is not edited.

## Binding

| Item | Value |
| --- | --- |
| Build | `.codex-research/search-budget-build-20260922-c`, manifest `20260922T132900987439Z.sources.json` sha256 `433802327bd373505f5d033f78994dc55ffcd4238212c00bf3c7635c38982c63` |
| Binaries | `openjkded.x86.exe` `5d850df20702774d5b406d1183494ecddf024aee4b6b18840c5078218ad465bd`, `jampgamex86.dll` `a69df557f65fdc5fdcda49e94c43262c9d4adc2409d97501c6f7555d08e96328` |
| Sources | 1,117 hashes matched before the first world (`manifest-before.json`) and after the last (`manifest-after.json`) |
| Base (v1) | `.codex-research/production-graph-20260922-a/production-graph.txt`, sha256 `c1c0765ff184f53d1f9c10fc7e9ffe107071948ac02af3ac55a33c5c7f59a92a`, `g1-0e5af7aba9b5e908`, 676 edges |
| **Cache v2** | `.codex-research/production-graph-20260922-b/production-graph.txt`, sha256 `e37ca150a97427800b59ee332e8c93a0e9ace38b1e9494e8a1dc0674aa34a6d4`, 1,269,723 bytes, **`g1-9631cbeaef2e5ba8`**, 3,312 edges |
| Enrichment manifest | `.codex-research/production-graph-20260922-b/manifest.json`, sha256 `86ba9d7bfd4089a2db59fa4186c98a491db49970a0e2d9ac6007cf21b3bd1ea2`: the tool's and the merge tool's sha256, the base's, the output's, and for each of 4,209 world inputs (2,806 committed, 1,403 learned) the sha256 of its `server.log`, `result.json` and saved cache |
| Tool | `tools/movement_graph_enrich.py` sha256 `bd675eaba3c42ef0c27094c6c8f60f94b4d704ee91807e0c53e6022129c10479`; `tools/movement_graph_merge.py` unchanged, `666a2171b088aebb454db469554ce34b2d9edb10b3348496a786a7285ecff48e` |
| Settings | the decision-tape record's block, `--edges`, `--graph` v2, library off, parity 0, search budget default off |

## 1. The enrichment

### Why an index resolves against the world's own save

From `codemp/game/bot2_graph.c`: `Graph_Save` writes `edge[0 .. edgeCount)` in
index order after the cells and nodes. **Disabled edges stay in the array** -
`Graph_EdgeResult` sets `disabled` after repeated failures, the flag is not a
saved field, and the planner skips them in memory - so disabling never moves an
index. Appending never moves one either. **The one removal is
`Graph_ResetLearned` at a fixture release**, which truncates the array to the
loaded count (from the production-graph build on). So the saved record at index
N is the edge a commit named, except for a commit made *before* the release that
named an appended edge in a world whose reset dropped edges: that edge is gone
and its index may have been reused after the release. The tool refuses exactly
those (`dropped_by_reset`), refuses any index past the saved count
(`beyond_cache`), and cross-checks every index that has a `[GRAPH] edge_added`
line in the world's log against the saved record's cells (`added_mismatch`).
Every world's cells and nodes must equal v1's, since an edge names cells by
index; the seeded and unseeded caches all share one floor (the same 14,249 cells
and 1,398 nodes, verified by digest), so the seeded records fold without
translation. **No index was refused on any of those grounds.**

### The table

Distinct means distinct under the fold identity (type, cells, tap, entry band
for strafe; type, cells, rounded takeoff otherwise).

| Campaign | Graph loaded | Worlds | Commit lines | Indices named | Resolved | Distinct | In v1 | New |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `gate-l1-20260922-a` | seed and map-load | 287 | 3,367 | 836 | 836 | 171 | 1 | 170 |
| `tie-break-tally-20260922-a` | seed | 38 | 322 | 151 | 151 | 47 | 0 | 47 |
| `tape-library-four-20260922-a` | seed | 45 | 427 | 234 | 234 | 49 | 0 | 49 |
| `decision-explorer-c75-20260922-a` | seed | 327 | 2,981 | 1,283 | 1,283 | 203 | 1 | 202 |
| `decision-explorer-c100-20260922-a` | seed | 317 | 2,882 | 1,604 | 1,604 | 266 | 0 | 266 |
| `decision-explorer-c150-20260922-a` | seed | 294 | 2,580 | 1,470 | 1,470 | 225 | 0 | 225 |
| `decision-explorer-c125-20260922-a` | seed | 95 | 840 | 420 | 420 | 93 | 0 | 93 |
| `decision-explorer-prod-c75-20260922-a` | v1 | 457 | 4,100 | 2,076 | 2,076 | 95 | 95 | 0 |
| `decision-explorer-prod-c150-20260922-a` | v1 | 127 | 1,129 | 545 | 545 | 49 | 49 | 0 |
| `decision-explorer-prod-c100-20260922-a` | v1 | 410 | 3,416 | 1,349 | 1,349 | 81 | 81 | 0 |
| `decision-explorer-prod-c125-20260922-a` | v1 | 341 | 3,927 | 1,778 | 1,778 | 80 | 80 | 0 |
| `decision-explorer-prod-c0-20260922-a` (partial) | v1 | 68 | 745 | 333 | 333 | 44 | 44 | 0 |
| **all committed** | | **2,806** | **26,716** | **12,079** | **12,079** | **586** | 116 | **470** |

Every resolved committed edge is a strafe edge. The seven seeded campaigns
together give 470 new edges and two hits on edges v1 already had (one in the
gate campaign, one in the class-75 explorer; whether they are the same edge was
not separated), so their union is 471 or 472 distinct edges - consistent with
the plan's count of 472. The cache-v1 explorers' commits are all v1 edges - 349 per-campaign distinct,
114 to 116 across the five campaigns (the union of 116 in-v1 edges less the
seeded hits): on v1 the ranking and the explorer never
committed an edge that the world had learned during the run.

The learned side, the saved edges past each world's loaded 676, from worlds that
loaded `g1-0e5af7aba9b5e908` (every world in these campaigns did):

| Campaign | Worlds | Appended records | Distinct | New to v1 |
| --- | ---: | ---: | ---: | ---: |
| `decision-explorer-prod-c75-20260922-a` | 457 | 3,369 | 1,077 | 1,077 |
| `decision-explorer-prod-c150-20260922-a` | 127 | 854 | 299 | 299 |
| `decision-explorer-prod-c100-20260922-a` | 410 | 2,958 | 872 | 872 |
| `decision-explorer-prod-c125-20260922-a` | 341 | 3,404 | 691 | 691 |
| `decision-explorer-prod-c0-20260922-a` (partial) | 68 | 623 | 258 | 258 |
| **all learned** | | 11,208 | **2,194** | **2,194** |

2,166 of the 2,194 are not among the committed edges. The fold: 676 v1 records
first, in their own order, then 470 committed and 2,166 learned-only new edges
= **3,312**. v1's indices 0 to 675 name the same maneuvers in v2; eight of their
records were replaced by a cheaper measurement of the same fold key, which the
merge tool's identity does by construction.

| | cells | nodes | edges | hop | pad | strafe | identity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| v1 | 14,249 | 1,398 | 676 | 16 | 10 | 650 | `g1-0e5af7aba9b5e908` |
| **v2** | 14,249 | 1,398 | **3,312** | 48 | 10 | 3,254 | **`g1-9631cbeaef2e5ba8`** |
| the 2026-09-16 research seed | 14,249 | 1,398 | 94,711 | - | - | - | `g1-98eb1e97c24a4e21` |

### The alignment check

Resolution by index is only as good as the save order. On the first 25 worlds of
three campaigns - the seeded gate (103 commits with an index), the seeded class-100
explorer (100) and the v1 class-75 explorer (77) - the resolved first edge's
takeoff is **at most 64 units** from the commit line's `x`/`y`, median 46, 36 and
50; an arbitrary edge of the same cache is a median 2,439, 2,051 and 1,966 units
away. Fifteen gate-campaign commit lines carry no `index` field at all and are
not counted as named.

### The regression

`tools/test_movement_graph_enrich.py`, seven tests on hand-built logs and
caches: an index resolves against the world's own cache and not the base's (a
seeded world where index 2 is a different edge); a record already in the base
counts as in the base and a cheaper record replaces it; a pre-release index is
refused after a reset that dropped edges while the post-release reuse of the
same index resolves; an `edge_added` line disagreeing with the save and an index
past the save are refused; learned edges are the save past the loaded count,
and a world on another graph identity is refused; a world on different cells is
not folded; and `main` writes a cache and a manifest with each input's hash.
Writing it found one defect before any real input was read: the field parser
cut `graph=g1-...` at the hyphen, which would have refused every learned world.

## 2. The floor on v2

36 cases unpinned, **one bench call per case, sequential** (`F-<case>`), then
the two seeded regression cases (`G-<case>`), v2 as `--graph`, library off,
harness off.

| | Result |
| --- | --- |
| Cases | **36 of 36** |
| Deaths | **0** |
| Owners | `move` only |
| Forbidden spend | none |
| Graph | `g1-9631cbeaef2e5ba8`, `cached=1`, `edges=3312`, in all 38 worlds |
| `red_ret_L1_respawn4` | **pass, 16,125 ms** (release 2,750) |
| `blue_out_L1_respawn4` | **pass, 16,150 ms** (release 4,625) |
| `red_ret_L1_stand` (seeded regression) | pass, 19,650 ms |
| `blue_out_L1_stand` (seeded regression) | pass, 20,700 ms |

No case died, so no trip's edges needed listing. The other level-1 floor times:
`red_out_L1_stand` 19,300, `red_out_L1_respawn5` 14,200, `red_out_L1_respawn4`
15,325, `red_ret_L1_stand` 20,075, `red_ret_L1_respawn5` 13,225,
`blue_out_L1_stand` 20,700, `blue_out_L1_respawn5` 13,975, `blue_ret_L1_stand`
16,725, `blue_ret_L1_respawn5` 15,225, `blue_ret_L1_respawn4` 15,325. The floor
is unpinned, so these are single draws of the start level and not a comparison
with v1's floor, which also ran at `--parallel 4`.

## 3. Determinism on v2

`red_out_L1_stand`, pinned with `--corner-class`, harness off, library off.

| Class | Worlds | Release levels | Completed | Leg decisions | Distinct sequences | v1, same cell |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| 125 | **6 of 6** | 3,725 / 3,925 / 4,325 / 4,925 | **19,275 ms**, all six | 12 | **1** | 19,050 |
| 75 | **4 of 4** | 3,475 / 3,675 | **19,450 ms**, all four | 13 | **1** | 19,150 |

Every decision of the leg (release to completion) matches in offset, kind, hop
count, both landing cells, both edge indices and the candidate and admitted
counts. Four class-125 worlds missed the class pin (`P-c125-r2`, `-a2`, `-a3`
overshot to 5,700 against the 3,000 to 5,500 window, and `P-c125-r4`) and are
retained; the runner replaced each.

The class-125 leg on v2:

    +450   state    hops=2 c0=886   c1=7710  idx=680/681  cand=19 adm=19
    +2950  state    hops=2 c0=9197  c1=7320  idx=782/783  cand=24 adm=24
    +4700  straight hops=2 c0=2032  c1=8182  cand=18 adm=9
    +6450  state    hops=1 c0=10586          idx=784      cand=24 adm=10
    +7350  state    hops=2 c0=6125  c1=4289  idx=902/903  cand=20 adm=7
    ...
    +15075 state    hops=1 c0=10876          idx=2505     cand=20 adm=2
    +16525 state    hops=2 c0=5568  c1=2524  idx=772/773  cand=17 adm=3

All six state decisions of the twelve fly edges v2 added (index 676 or
above), and the class-75 leg shares its first decision. The ranking takes the
enriched edges immediately and completes **225 ms (class 125) and 300 ms
(class 75) slower** than on v1. That is the ranking's choice from a larger set,
which the project has measured before to be a poor predictor of the completed
trip ([class-75 explorer](2026-09-22-decision-explorer-c75.md)); whether v2
offers faster trips is the explorer's question, below.

## 4. The explorer chain on v2, launched

The Python suite passed from the repo root before the launch - **857 tests, OK**
- and nothing under `tools/` changed after it.

| | |
| --- | --- |
| Script | `.codex-research/decision-explorer-v2-chain-20260922-a.sh`, sha256 `92ad4f6d48765853ea909eb1d9b8b4c9aa553a6de691cd010ea39a7d8541dd22`, launched by `Start-Process "C:\Program Files\Git\bin\bash.exe" -l <script>`, hidden |
| Classes | 75, 150, 100, 125, 0, 25, 50, 175, one at a time |
| Search | `--beam 3 --patience 2 --max-depth 0 --port 37800`, build `search-budget-build-20260922-c`, `--graph` v2 |
| Output | `.codex-research/decision-explorer-v2-c<class>-20260922-a/`, per-class `.codex-research/decision-explorer-v2-c<class>-20260922-a.campaign.out.txt` |
| Chain log | `.codex-research/decision-explorer-v2-chain-20260922-a.log`, a start line and an exit code per class |
| Done marker | `.codex-research/decision-explorer-v2-chain-20260922-a.done` |
| Started | 2026-09-22 17:40:57 UTC; explorer python pid 40360, bench pid 35240, first world `decision-explorer-v2-c75-20260922-a/control` |

Sources and `tools/` are frozen until the done marker exists.

## What this does not establish

- **That v2 is faster.** The ranking on v2 is slower at both measured classes.
  The explorer campaigns are the measurement, and none has reported.
- **That the learned edges are good edges.** The 2,166 learned-only edges were
  learned in flight by explorer worlds whose branches were forced; they were
  validated as flown maneuvers, not as fast ones. The committed 470 were each
  flown under live validation as a decision.
- **Anything about seeded-graph behaviour on v2.** The seeded records carry
  costs and entry states measured on a 94,711-edge graph; on v2 they are edges
  among 3,312 and the planner prices routes differently.
- **Coverage beyond `red_out_L1_stand` at classes 75 and 125.** Determinism on
  v2 is two classes of one case; the floor is one unpinned draw per case.
- **Row 1's parity half, and row 5.** The harness was off throughout. v2 has
  about five times v1's edges, and row 5's
  [cost reading](2026-09-22-search-budget.md) was taken on v1; the per-candidate
  `Graph_PlanState` pass named there as the tail suspect runs over this larger
  edge set, and that is unmeasured.
- The exact seeded union is 471 or 472; the two in-v1 hits were not separated.

## Reproduce

```powershell
python .codex-research/production-graph-20260922-b/verify_sources.py `
  .codex-research/production-graph-20260922-b/manifest-before.json
# detached, from script files and never an inline command:
#   enrich.sh (the enrichment), then run.sh FG, then run.sh P
python .codex-research/production-graph-20260922-b/analyse.py
python .codex-research/production-graph-20260922-b/verify_sources.py `
  .codex-research/production-graph-20260922-b/manifest-after.json
```

## Tools and files

Project tools: `tools/movement_graph_enrich.py` and
`tools/test_movement_graph_enrich.py`, both new; `movement_graph_merge.py`,
`movement_bench.py` and `movement_decision_explore.py` used unchanged. No C
source changed. The campaign's scripts (`enrich.sh`, `run.py`, `run.sh`,
`analyse.py`, `verify_sources.py`) are retained beside its worlds under
`.codex-research/production-graph-20260922-b/` with `progress.jsonl`,
`campaign.out.txt`, `analysis.txt` and `analysis.json`, and are not project
tools.
