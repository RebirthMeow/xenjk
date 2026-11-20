# Seed deaths and planner reach: one trip at class 50, and a route v2 cannot plan

Recorded 2026-09-22. A read-only investigation of two questions left open by
[the level-1 gate campaign](2026-09-22-gate-l1.md) and
[production graph cache v2](2026-09-22-production-graph-v2.md). No source,
tool, build or world was changed or run. Every figure comes from retained
worlds, and the route claims come from an offline replica of
`Graph_PlanState`. That replica reproduces five printed plan lines exactly.

> **A. The eleven seeded deaths are a single trip, repeated eleven times.**
> Every `red_ret_L1_respawn4` and `blue_out_L1_respawn4` world that died on the
> 2026-09-16 seed was released at corner level **2,850 (class 50)**. They
> share every commit, landing cell, graph edge, recover line and the kill:
> `MOD_FALLING` at level 20,450, 17,600 ms after the corner level. All eight
> seeded worlds released at other classes passed: three at class 75 and five
> at class 150. **No world without the seed has run either case at class
> 50.** The unseeded row-4 arm, the probe, both gate floors and the v1 and v2
> floors all landed on classes 0, 25, 125 or 150. The probe therefore does
> not separate the seed from the start class, and the gate record's sentence
> "the probe isolates it" overstates what it measured.
>
> **What the bot was doing when it died.** After its last commit at +8,825 ms
> it went 2.25 s with no admitted candidate at a ledge above a pit. The
> planner kept routing it over four seeded pit crossings (5754, 23066, 88510,
> 9807) that the straight search could not fly: every candidate was
> `refused` with `unsupported=256` or `fell` with a 440-unit drop. On the
> last route, walking toward crossing **9807** (`10855->8881`), the walker
> declined at `floor_missing` at (671.0, 1407.8), and the movement owner
> passed the bot to **legacy** at 14,875. Legacy's stuck recovery made an
> escape jump at yaw 225 toward (507, 1130) and fell 1,776 units into the pit.
> The bot came to rest at (49.7, 975.2, 32.1), where the recover line reads
> `trigger_hurt`, and died 2.55 s later. The owner sequence was `move`, then
> `legacy`, then `none`.
>
> **Which of the named edges v2 contains.** The death trip's state pair,
> 82321/10356, is in v2 as **684/685**. Of the four crossings, only **88510**
> is in v2, as **833** with an identical record. 5754, 23066, **9807** and
> 94576 are not in v2. The crossing on the fatal route is 9807, so v2 does not
> carry the edge that killed. It does carry the site: v1 and v2 hold the same
> six southward crossings with takeoffs within 96 units of the fatal lip,
> where the seed holds 515.
>
> **B. On v2 the explorer cannot reach the seeded trip, and the cause is
> missing edges, not planner preference.** Every candidate a search offers
> comes from one installed route. State candidates are only that route's next
> strafe edge, and straight candidates aim at points along that route. The
> seeded fast trip was planned over routes that v2 cannot rebuild:
>
> - At release, **3 of the 16** edges on the seeded route are absent from v2:
>   51453, 48185 and 41945.
> - On the in-trip replan that carried 10 of the trip's 11 decisions, **1 of
>   15** is absent: **59480**. It is the only edge that lands inside the entry
>   envelope of the seed edge the tape names, 62163, which is v2's **815**.
> - None of these four was ever committed in a seeded world, because the
>   trip flew straight candidates along them. The v2 enrichment folded only
>   committed edges, so they never entered v2.
>
> Priced on v2's own records with the missing edges restored, the seeded
> release route costs **12,995 ms**, against the **14,054** that v2 plans
> today. From the in-trip root it costs **12,131**, against **12,124**. On v2
> as it stands, the cheapest route through 815 costs **+783 ms** at both
> roots. With 59480 restored, v2's own planner takes 815/816/750 unforced.
> Across the 141 v2 class-75 worlds, 815 and 816 appear in no commit and no
> edge event.
>
> **The planner does not always return its own cheapest route.** On v2 with
> the three edges restored it returns 13,281 ms, although a 12,995 ms route
> exists in the same graph. Adding one edge also raised the cost of the route
> it returned by 9 ms, at two different roots.

## What was asked

- **A.** Explain what killed `red_ret_L1_respawn4` and `blue_out_L1_respawn4`
  on the research seed: level, position, the last three commits with their
  edges resolved, the owner sequence and the fatal frame. Then find whether a
  small set of edges is present in every death and absent from the passing
  worlds without the seed, and whether cache v2 contains it.
- **B.** Explain why the explorer on v2 cannot reach the seeded fast trips:
  - how a state candidate is offered;
  - which route `Graph_PlanState` produced in the v2 class-75 control and its
    depth-1 branches, and where the seeded trip's first state edge sits
    relative to that route;
  - whether the cost model prefers v2's edges, and by how much;
  - the cheapest explorer change that branches over planner alternatives.

The work was read-only. No file under `codemp/` or `tools/` was changed,
nothing was built, no server was run, and this is the only new file.

## Binding

No project source was changed. The source files were read in the working tree
at commit `dacf00d0`, while the running campaign kept them frozen:

| File | sha256 |
| --- | --- |
| `codemp/game/bot2_move.c` | `6b7a78a1605ebb68b5dacca9a4aa0e8b8a6078beb212596bb10d46fc620d2a4d` |
| `codemp/game/bot2_graph.c` | `d0a7b1e6489654006f860c5be17b22563540e143ca699b74da8cb14a5d51027e` |
| `codemp/game/bot2_graph.h` | `41c0724cb2e72ab0d5354e761c1e05346be3225cd52030a4608b7cf246e3f75f` |
| `codemp/game/bot2_move.h` | `535a851fa43f83ec70d79c193a705cbbd09beb9b820e83dd2b678c4abce3b103` |

The graphs:

| Graph | Path | sha256 | Edges |
| --- | --- | --- | ---: |
| 2026-09-16 research seed | `.codex-research/move-explore-20260916-b/seed.txt` | `3e5b1a9dda5338542560a17bc64ebf32a3bf260c320c7541aad8020ede1306fa` | 94,711 |
| cache v1 | `.codex-research/production-graph-20260922-a/production-graph.txt` | `c1c0765ff184f53d1f9c10fc7e9ffe107071948ac02af3ac55a33c5c7f59a92a` | 676 |
| cache v2 | `.codex-research/production-graph-20260922-b/production-graph.txt` | `e37ca150a97427800b59ee332e8c93a0e9ace38b1e9494e8a1dc0674aa34a6d4` | 3,312 |

**The death worlds.** All eleven are under `.codex-research/gate-l1-20260922-a/`
and ran on build `tie-break-build-20260922-a`, as bound in the gate record.
Every world saved a **byte-identical** cache,
`base/bot2-graph/mp_ctf_kejim_-1301924053.txt`, with sha256
`0d245b76a02e27723c4ae005467d20dcd390eb98738045c84ace33339d7c8f63` and
94,715 edges: the seed plus four appended. The table gives the first 16 hex
digits of each hash.

| World | `server.log` | `result.json` |
| --- | --- | --- |
| `red_ret_L1_respawn4-lib-r1` | `37fe687fa601bc69` | `0f6116f64b955890` |
| `red_ret_L1_respawn4-lib-r1-a2` | `ae6d16920bce8e16` | `49ab0cd2ffa5f231` |
| `red_ret_L1_respawn4-lib-r2` | `721a8725589187e5` | `a42521db1a63fafc` |
| `red_ret_L1_respawn4-off-r2` | `e8cf8296251f37a9` | `508b6e78bc98b791` |
| `blue_out_L1_respawn4-lib-r2` | `5a296d23692209ba` | `55db5a0ff89b9a4a` |
| `blue_out_L1_respawn4-lib-r2-a2` | `a4108af9540da6b8` | `ef93e04c41c93b19` |
| `blue_out_L1_respawn4-lib-r3` | `30518415a995cfc5` | `77d880e2cf4bcf27` |
| `blue_out_L1_respawn4-off-r1` | `35be3d0cbc4efa33` | `3dab00de8dcde55c` |
| `blue_out_L1_respawn4-off-r1-a2` | `fd88d7da49655118` | `72685a70367011ac` |
| `blue_out_L1_respawn4-off-r3` | `b454cffda7f0a159` | `1230d8e42c112773` |
| `blue_out_L1_respawn4-off-r3-a2` | `838ec03292ef12df` | `ce3a88cc03eb62e7` |

**The comparison worlds**, all in the same campaign directory:

- the eight seeded passes of the same two cases;
- the four probe worlds (`probe-*`, `probe.jsonl` sha256
  `c25d3668aa6535f2ab6b7c4a17c2b11d7d784ba1d341151dee363bb6d88d7b98`);
- the twelve unseeded row-4 worlds of the same cases (`u-*`);
- the respawn4 cases of `floor-off` and `floor-on`;
- the respawn4 cases of the v1 floor (`production-graph-20260922-a/F-floor`)
  and the v2 floor (`production-graph-20260922-b/F-*`).

**The seeded fast trip** is class-75 world
`.codex-research/decision-explorer-c75-20260922-a/control-a2-d1c1-d2c3-d3c4/`,
whose `server.log` has sha256
`1c4716bb2277eb6fdf33817585362d832e08c8691fbebc737c59f46c13cab899`. It was
written out as tape `tapes/c75-best.tape`, sha256
`401cdce9acfc3847cd50353b1c8ad1b55dd83c8132044782b3e331fa0a52e066`. The
world's own saved cache has sha256
`c935a6eb7e99bf7aca1ad6bfd47d3ec800a20e3217277061e750eefd69415bfa`.

**The v2 class-75 explorer** is `.codex-research/decision-explorer-v2-c75-20260922-a/`
on build `search-budget-build-20260922-c`. It ran 141 worlds and confirmed
17,850 ms. Its hashes:

| File | sha256 |
| --- | --- |
| `result-c75.json` | `54ba37e917c794421c60f19f2096a4ca5e2bf0c9a6dfcbb91e16a7cc35027f7f` |
| `progress.jsonl` | `fef0af6dd076844c44fd1f6604f1473c1008837c017a5cd66e33d172b2ec3134` |
| `tapes/c75-best.tape` | `b2ff63c72f88ffba0d4032d00ac7de7f3fc7e487826b0f5a078b9ead321ab8ab` |
| `control-a2/red_out_L1_stand/server.log` | `3ad6b3e4287d1adfb62803cb540841595bc772195c9313ac058df1fb6bb2c35b` |

The world named `control` is a `runner_error` with no release level.
`control-a2` is the actual control.

**The offline planner replica** is a scratch script outside the repository,
`planner.py`, sha256
`9de6907cb351531ec1e227fba8758503c39bc8d77807ccbcccf7a95e32f4cb87`. It is a
line-for-line port of `Graph_PlanState` and covers:

- `StateKey`, `StateWalk` and `Heuristic`;
- `Graph_StrafeAdmits`;
- edges of the cell and its neighbours;
- the C binary heap;
- the prepended `firstEdge` order.

An optional `require` flag was added for the forced-inclusion prices in B. It
is off in every reproduction.

### The replica reproduces every plan line it was checked on

| Plan line | Graph | Root | Printed cells / edges / cost_ms / expanded | Replica |
| --- | --- | --- | --- | --- |
| v2 class-75 release, 126 worlds | v2 | cell 0, 0 u/s, yaw 180 | 22 / 17 / 14,054 / 171 | 22 / 17 / 14,054 / 171 |
| seeded fast world release, level 3,900 | seed | cell 0, 0 u/s, yaw 180 | 19 / 16 / 13,145 / 228 | 19 / 16 / 13,145 / 228 |
| seeded fast world replan `gen=7`, level 6,850 | seed | cell 9436, 151.7 u/s | 19 / 15 / 12,206 / 257 | 19 / 15 / 12,206 / 257 |
| death world replan `gen=7`, level 13,175 | death cache, avoid 94576 | cell 3669, 250 u/s | 13 / 7 / 6,082 / 77 | 13 / 7 / 6,082 / 77 |
| death world replan `gen=12`, level 14,125 | death cache, avoid 94576 and 5754 | cell 8485, 47.7 u/s | 19 / 6 / 6,089 / 132 | 19 / 6 / 6,089 / 132 |

A plan line prints only counts. Settings run `bot_moveReplay=0`, so no
`[REPLAY] kind=path_edge` line exists. The replica is therefore the only way
to read these routes, and the exact match on `expanded` is what licenses
reading them.

## A. The seeded respawn4 deaths

### The start class decides it

| Graph | Class 0 | Class 25 | Class 50 | Class 75 | Class 125 | Class 150 |
| --- | --- | --- | --- | --- | --- | --- |
| seed, row-4 seeded arm (19 worlds) | - | - | **11 deaths** | 3 pass | - | 5 pass |
| none: `u-*` arm, probe, `floor-off`, `floor-on` (20 worlds) | 5 pass | 14 pass | - | - | - | 1 pass |
| cache v1 floor (2 worlds) | - | 1 pass | - | - | 1 pass | - |
| cache v2 floor (2 worlds) | - | 1 pass | - | - | - | 1 pass |

The class is the corner level mod 200. The seeded worlds landed on levels
2,850, 2,875 and 3,350. The unseeded worlds landed on 2,600 and 2,625, with
probe outliers at 3,350, 6,425 and 7,225. So the seed also shifted the level
ladder, and **class 50 was never sampled without it**.

The eleven deaths are one decision sequence. They agree on every commit's
kind, cells and edge indices, every edge event, every recover line and the
kill. They differ only in `rollout_ms` and trace counts, and in one commit
`id` counter. `red_ret` and `blue_out` respawn4 share a start at
(3264, 2464, 1808) and an objective at cell 0.

### The fatal trip

The trip was released at level 2,875 from (3264, 2464, 1808). Offsets in the
table are from the release.

| Offset | Event | Graph edges, resolved in the world's own cache |
| ---: | --- | --- |
| +0 | gap hop | learned hop 94711, `4583->9289` |
| +3,625 | straight, 2 hops, landing cells 9838 then 10142 | learned 94712 `8295->9838` (new); 83088 `9838->10142` (seed, merged) |
| **+5,400** | **state, 2 hops** | **82321 `10700->6653`**: takeoff (2080, 2880), landing (1838, 2599), tap, entry 400, exit 461, 825 ms. **10356 `6069->4243`**: takeoff (1779, 2577), landing (1482, 2238), tap, entry 507, exit 548, 850 ms |
| **+7,075** | **straight, 2 hops, landing cells 6082 then 5510** | learned 94713 `2991->6082`; 83912 `6082->5510` (seed, merged) |
| **+8,825** | **straight, 1 hop, landing cell 838** | learned 94714 `4280->838`; lands at (1100.9, 1614.0) at +9,750 |
| +9,750 to +11,225 | 9 straight searches, 12 to 16 candidates each, **none admitted** | every candidate is `refused` with `unsupported=256` or `fell` with `a0_drop=440` |
| +10,300 | route edge avoided, `why=walked` | 94576 `4279->6130`: tap, entry 510 |
| +10,600 | route edge refused, then avoided | **5754** `7222->11085`: takeoff (903, 1428), landing (654, 1317) |
| +10,700 and +10,950 | route edge stale | **23066** `8109->11120`: takeoff (874, 1423), landing (648, 1255) |
| +10,975 and +11,225 | route edge stale; the walker had braked to 38-107 u/s at `floor_missing` | **88510** `7690->11304`: takeoff (871, 1428), landing (623, 1322) |
| +11,250 | replan `gen=12`: walk west along y = 1408 from (832, 1408) to (640, 1376), then a crossing | **9807** `10855->8881`: takeoff (630, 1350), landing (541, 1093) |
| **+12,000** | `recover reason=floor_missing decline=corridor` at **(671.0, 1407.8, 1808)**; owner becomes **legacy** | - |
| +12,025 | legacy `STUCK phase=2`, 360-degree escape scan, `escape jump yaw=225 -> {507 1130 1808}` | - |
| about +12,450 | jump from about (590, 1360, 1808); apex z 1,906; constant horizontal velocity (-197, -156); falls past floor height near x = 400 | 9807 later avoided `why=walked` at +12,550 |
| +15,000 | lands at z = 32.1 at (70.2, 975.1), 1,776 units below the ledge; `no_path from=-1` | - |
| +15,025 | `recover reason=trigger_hurt` at (66.3, 975.2, 32.1) | - |
| **+17,575** | **`Kill ... MOD_FALLING`** at (49.7, 975.2, 32.1): level 20,450, 17,600 ms after corner level 2,850 | - |

**The last three committed decisions are the rows in bold**:

- +5,400: state, edges 82321/10356;
- +7,075: straight;
- +8,825: straight.

**The owner sequence** is `move`, then `legacy` from 14,875 (+12,000), then
`none` at death (20,450), then `move` again after the respawn at 21,475. At
+3,475 there is also a one-frame decline, `floor_missing` at (2751.8, 2972.8),
and the walk restarts on the next frame. The blue-side telemetry records it
as `legacy` from 6,350 to 6,600, and the red-side 4 Hz samples miss it. The
seeded passes decline at the same first gap (+3,550 and +3,600), so this frame
does not distinguish the deaths.

**On the frame it died, the bot was standing still.** It lay in the pit at
(49.7, 975.2, 32.1), with speed 0 since level 18,375, under the legacy owner.
The fall happened 5 s earlier. The move module had declined the frame at the
lip, and the legacy owner's escape jump over the pit caused the fall.

### Which edges are involved

Because every death is the same trip, every edge in it is present in every
death. The question reduces to which edges the passing worlds lack.

| Edge | from->to | Role | Passing seeded worlds | Probe and unseeded worlds | v2 (same from->to, tap, entry within 48) |
| --- | --- | --- | --- | --- | --- |
| 82321 | 10700->6653 | the state commit | absent | absent | **684**, identical |
| 10356 | 6069->4243 | the state commit | absent | absent | **685**, identical |
| 83088 | 9838->10142 | straight hop, merged | absent | absent | absent |
| 83912 | 6082->5510 | straight hop, merged | absent | absent | absent |
| 94576 | 4279->6130 | route edge, avoided | absent | absent | absent |
| **5754** | 7222->11085 | pit crossing, refused | absent | absent | absent |
| **23066** | 8109->11120 | pit crossing, stale | **present**: flown by a straight commit in three class-75 passes | absent | absent |
| **88510** | 7690->11304 | pit crossing, stale | absent | absent | **833**, identical |
| **9807** | 10855->8881 | pit crossing on the fatal route | absent | absent | absent |
| 94712 / 94713 / 94714 | learned | straight hops | 8295->9838 present in five passes | absent | absent |

**No edge separates the deaths from the probe worlds in any useful sense.**
The probe worlds ran on the map-load graph plus 27 to 33 learned edges, and
share no `from->to` pair with the death trip at all. Against the passing
seeded worlds, the separating set is the ten pairs other than `8109->11120`
and `8295->9838`.

**The causal set is the pit crossings the planner kept routing through while
nothing was admissible**: 5754, 23066, 88510 and 9807, plus 94576 before
them, with **9807** on the fatal route. The passes show that these crossings
are flyable from a flight. The three class-75 passes crossed the same pit by
a straight commit from (875.7, 1415.5) that landed on cell 11120, which is
23066's geometry. The death trip arrived on foot from (1100.9, 1614.0) and
found no admissible candidate.

**Does v2 inherit it?** v2 has the state pair (684/685) and one of the four
crossings (88510 as 833). It does not have 9807, 5754, 23066 or 94576. The
site survives in a thinner form. Counting crossings whose takeoff lies within
96 units of the fatal lip (671, 1408) and whose landing lies south of
y = 1250 on the upper floor:

- the seed holds **515**;
- v1 and v2 hold the same **six**, all from v1: indices 20, 107, 350, 360,
  478 and 652.

One of the six, 360 (`10019->10075`), takes off from cell 10019 at (672, 1408),
the exact lip where the walker declined. So v2 does not carry the killing
edges, but it does carry crossings at the killing site. Whether a class-50
respawn4 trip on v2 reaches that lip on foot has not been measured.

## B. Why the v2 explorer cannot reach the seeded fast trip

### 1. How a state candidate is offered

This comes from `bot2_move.c`, in the straight search.

- **One route.** `Replan()` installs a single route: the result of
  `Graph_PlanState(from, live state, to, avoid)`, or `Graph_Path` when that
  fails. The avoid list is per objective (`m->state.avoid[32]`) and is
  cleared by `Move_Reset`.
- **State candidates come only from that route.** `PlannedStrafe()` returns
  the first path edge among the next three path entries, and only if it is a
  strafe edge, is not disabled, and has its takeoff within 64 units
  horizontally (`MOVE_STATE_TAKEOFF`) and 36 units vertically of the bot. If
  the following path edge is a tap edge, it is chained as `edge2`. For that
  single edge the search builds a candidate for each arm (one, or two with
  the replay arm or heading offer), each hop count (1, or 2 when `edge2`
  exists) and each run-up (0 and 24). These are the 2 to 6
  `state_candidates` the commit line prints. `LandingAimOffers` then
  re-aims some of them.
- **Straight candidates also follow that route.** There are 2 hop counts,
  3 aims and 2 run-ups. Each aim point is `PathPointAt(&m->path, ...)` a
  hop-length or two along the installed route.
- **The only off-route reach is a rerouted flight.** `RerouteFrom()` runs
  the same `Graph_PlanState` once from the rollout's landing cell and state,
  and installs that single best answer if the candidate is committed.
- **The explorer's force can only choose among these offers.**
  `bot_moveSelectNth` / `bot_moveSelectCand` pick an index among the
  candidates this search already offered and admitted. A named index that is
  not admitted forces nothing (`select_forced=-1`).

So **an edge the planner does not route through is never a candidate, and the
explorer cannot force it.** The per-candidate `[SPIKE]` line carries no edge
index at all. A state edge is named only on a commit line (`index`,
`index2`) or an edge event (`stale`, `refused`, `deferred`, `avoid`), so its
absence from `[SPIKE]` lines is structural. Across the 141 v2 class-75
worlds, **815 and 816 appear in 0 commits and 0 edge events**. 750 appears in
7 commits and 20 edge events.

### 2. The route v2 plans from the start, against the seeded one

All **126** v2 class-75 worlds that printed a release plan printed the same
one: 22 cells, 17 edges, 14,054 ms, 171 expanded. Every depth-1 branch took
its forced first decision under that plan. The next line after the release
plan is the commit in every depth-1 world, so the route at depth 1 is this
one. Its state candidates at decision 1 are 680/681, the route's first two
strafe edges.

| # | Seeded release route (seed graph, 13,145 ms) | In v2? | v2 release route (14,054 ms) |
| ---: | --- | --- | --- |
| 1 | 50732 `185->211` | 680 | 680 `185->211` |
| 2 | 26663 `492->8492` (1,075 ms) | 681 (975 ms) | 681 `492->8492` |
| 3 | **51453 `8826->8555`** | **absent** | 2374 `9436->9496` |
| 4 | 46450 `8553->1485` | 758 | 803 `9494->5648` |
| 5 | 35744 `2032->9487` | 759 | 1145 `5041->6815` |
| 6 | 65658 `9489->7220` | 979 | 1079 `6817->10309` |
| 7 | 62330 `7219->3042` | 996 | 1080 `10019->3045` |
| 8 | 29972 `3662->5472` | 997 | 903 `2444->7185` |
| 9 | 30908 `6078->3606` | 926 | 2693 `7659->423` |
| 10 | **48185 `4851->10422`** | **absent** | 1513 `1804->8804` |
| 11 | 67215 `10138->1590` | 867 | 893 `8466->9258` |
| 12 | 33286 `657->7417` | 868 | 891 `8955->331` |
| 13 | **41945 `7866->8842`** | **absent** | 924 `331->8624` |
| 14 | **62163 `8505->4956`**, the tape's first state edge | **815** | 2505 `9251->9153` |
| 15 | 72937 `6171->1413` (600 ms) | 816 (550 ms) | 772 `9154->4346` |
| 16 | 52256 `1415->45` | 750 | 773 `4958->937` |
| 17 | - | - | 729 `939->45` |

**The seeded trip's first state edge, 62163 (v2's 815), is not on the v2
route and cannot join it.** 815 needs:

- a tap state at a cell within 48 units (`GRAPH_STATE_REACH`) of its takeoff
  at (2326.9, 4449.2);
- a speed of 511.5 plus or minus 48 u/s;
- a heading within 30 degrees of (0.457, 0.889).

On the seeded route that state is produced by 41945, which lands on cell 8842
at (2336, 4416), 34 units from the takeoff. v2's nearest approach is 2505's
landing on cell 9153 at (2272, 4448), **55 units** away, outside the reach.
The cheapest v2 route through 815 leaves the chosen route after edge 1080 and
runs 1864, 1841, 1038, 2481, 2758, 584, 966, 863, 815, 816, 750. It costs
**14,837 ms, 783 more**.

**The seeded fast trip decided under an in-trip replan, not the release
route.** It replanned at level 6,850 from cell 9436 at 151.7 u/s (`gen=7`),
and took 10 of its 11 leg decisions under that route. That route has 15
edges. **Fourteen are in v2** (782, 783, 920, 784, 902, 903, 890, 888, 889,
891, 749, 815, 816, 750). The one that is not is **59480 `8623->8133`**,
takeoff (2177, 4041), landing (2357, 4460). It lands 34 units from 815's
takeoff and is the only link into 815 on that route.

### 3. Does the cost model prefer the v2 edges?

The comparison below prices each route with the replica, restricted where
stated to the named edges. "Restored" means the absent seed records were
appended to v2 unchanged.

| Root | Route | Planned cost | Against v2's choice |
| --- | --- | ---: | ---: |
| release (cell 0, 0 u/s) | v2's chosen route | 14,054 | 0 |
| release | seeded route on seed records | 13,145 | - |
| release | seeded route on v2 records, 3 edges restored | **12,995** | **-1,059** |
| release | v2 as is, cheapest route through 815 | 14,837 | +783 |
| release | v2 plus 51453 alone, unrestricted | 13,281 | -773 |
| release | v2 plus the 3 edges, unrestricted | 13,281 | -773 |
| release | v2 plus 41945 alone, unrestricted (takes 749, 41945, 815, 816, 750) | 14,063 | +9 |
| in-trip (cell 9436, 151.7 u/s) | v2's chosen route | 12,124 | 0 |
| in-trip | seeded route on v2 records, 59480 restored | 12,131 | +7 |
| in-trip | v2 as is, cheapest route through 815 | 12,907 | +783 |
| in-trip | v2 plus 59480, unrestricted (takes 749, 59480, 815, 816, 750) | 12,133 | +9 |

**The planner does not prefer v2's edges.** At release, the seeded route would
cost 1,059 ms less on v2's own records if its edges existed. v2 records two
of its edges more cheaply than the seed does (681 at 975 against 1,075, and
816 at 550 against 600). At the in-trip root the two routes are a near tie,
7 ms apart. Of that route's 15 edges, the only one v2 lacks is 59480, and
restoring it alone makes v2's planner take 815, 816 and 750 unforced. **The
gap is edges v2 does not have**:

- 51453, 48185 and 41945 at release;
- 59480 in trip.

**These edges never became committed edges.** The enrichment folded only the
`index`/`index2` of commit lines, plus the learned edges of the v1 explorer
worlds. v2 contains every seeded committed edge and has no record with these
four `from->to` pairs, so none was ever committed. The seeded fast trips flew
straight candidates aimed along these edges, so the edges shaped the route
without being named on any commit line.

**The planner is not exact.** Two results in the table should not happen in
an optimal search:

- On v2 plus the three restored edges, it returns 13,281 ms while a
  12,995 ms route exists in the same graph, 286 ms worse.
- Adding one edge made the returned route 9 ms more expensive, at both roots.

Both are consistent with two properties of the code. `StateKey` merges
continuous states into 32 u/s speed bands, keeps the heading octant only for
tap states, and lets the first state closed win. `Heuristic` assumes
450 u/s while edges like 815 cover ground at about 536 u/s, so it is not
admissible. Which of the two produced each case was not separated. Near-ties
of the size seen here (7 to 9 ms) are therefore inside the planner's own
resolution.

### 4. The cheapest way for the explorer to branch over planner alternatives

This is described only; nothing was implemented.

**On v2 as it stands, no planner directive can reach the seeded route**,
because four of its edges are not in v2. The prerequisite is graph content:
enrichment would also fold the edges of routes the fast worlds *planned*, not
only the edges they committed. Those routes can be reconstructed offline from
plan lines and each world's own graph, as this record did, or printed by
running the explorer with `bot_moveReplay=1`, whose `[REPLAY]
kind=path_edge` lines list every route edge. That changes the graph and is
the owner's decision. The directives below are useful either way, because
they let the beam start from a route other than the planner's single choice.

**Engine side: two exploration-only cvars, off by default, owned by Bot2.**

- **`bot_movePlanAvoid "<edge> <edge> ..."`.** `Replan()` and `RerouteFrom()`
  pass `Graph_PlanState` a local array holding `m->state.avoid` plus these
  indices. The 32-slot per-objective list and its `avoid` events are left
  untouched.
- **`bot_movePlanRequire <edge>`.** `Graph_PlanState` gains an
  `int require` argument, -1 for none:
  - `StateKey` takes bit 26 as "required edge taken";
  - `StateStrafeEdges`, and the non-strafe edge loop, set the bit on the
    successor when the relaxed edge is `require`;
  - `StateLink` and every other relaxation copy the parent's bit;
  - the goal test becomes `cell==to && (require<0 || bit)`;
  - if the constrained search finds nothing, the unconstrained search runs
    and the plan line says so.

  The offline replica ran exactly this construction to produce the 14,837 and
  12,907 figures above.
- **Scope.** An optional `bot_movePlanGen <n>` applies the directives only to
  the n-th plan of the leg, for example 1 for the release route. The default
  applies them to every plan of the leg. `Move_Reset` at the objective change
  ends them, as it ends the avoid list.
- **Telemetry.** The plan line appends `require=%d require_met=%d
  plan_avoid=%d`.
- **Identity.** With no require and an empty avoid list, the key's bit 26 is
  always 0 and the arrays are the ones passed today, so every search is
  bit-identical to the current build. Directives name edge indices, so like a
  tape they are bound to the graph identity (`g1-...`).

**Explorer side: a route branching step before the forced-index beam.**

1. Enumerate candidate routes offline from the leg's root: the release plan
   line's cell and state, or any decision's replan line. Use a replica of
   `Graph_PlanState` checked against the world's own printed plan line, which
   this record shows can match cells, edges, `cost_ms` and `expanded`
   exactly. Two generators:
   - **(a) one-edge deviations.** For each strafe edge on the chosen route
     (17 here), plan with that edge avoided.
   - **(b) named inclusions.** For each edge of a reference route (the
     seeded route, or another tape's) that the chosen route lacks, plan with
     it required.

   De-duplicate by edge sequence, and keep the K cheapest within a declared
   planned-cost window, for example 1,000 ms over the chosen route.
2. Run one world per surviving alternative, with the directive set.
3. Accept the world as that branch only if its release plan line matches the
   offline prediction (`cells`, `edges`, `cost_ms`). Otherwise record a route
   miss, retained like `select_forced=-1`.
4. Run the existing beam under each accepted route.

A k-best search inside the engine, such as Yen's algorithm over the state
graph, would give the same set. It needs K searches per plan and a path
store, and the explorer does not need it at runtime.

## What this does not establish

- **That the seed is necessary for the deaths.** The seeded graph at class 50
  died 11 of 11 times. No other graph has run these cases at class 50, so the
  seed and the start class are confounded. The gate record's probe did not
  separate them.
- **That v2 is safe on these cases.** v2 has none of the four killing
  crossings except 88510 (833). It does have six crossings at the same lip,
  one from the exact cell where the walker declined. v2's floor ran these two
  cases once each, at classes 150 and 25. Nothing here runs a world.
- **That the legacy hand-off is a graph defect.** The death needs the move
  owner to decline at the lip and the legacy stuck recovery to jump. Both are
  owner behaviour and would follow any route that strands the walker at a lip
  with nothing admissible. This record names the route, not a repair.
- **That restoring the missing edges reproduces 15,950 ms on v2.** The
  planned costs say v2's planner would take the seeded chain. Planned cost is
  not trip time: the seeded tape is nine straight flights and two state
  decisions, and the straight aims depend on the whole route geometry,
  rollouts and ranking. Only a world measures that.
- **Exact optimality of the replica's forced-inclusion prices.** They use the
  same inexact search as the engine, so the +783 figures are what the engine
  would plan, not a lower bound.
- **Anything about other classes, cases or maps.** Part B is class 75 of
  `red_out_L1_stand` on `mp/ctf_kejim`. Part A is two respawn4 cases.

## Tools and files

No project file changed except this new record. No C source, tool, library,
tape, cache or retained world was written. PLAN.md, HANDOFF.md and EVIDENCE.md
were not edited. The scratch scripts (`planner.py`, `deaths.py` and the one-off
queries that call them) are outside the repository and are not project tools,
so no regression was added for them.
