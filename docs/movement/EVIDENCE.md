# Rewrite evidence

Latest completed experiment: [deferred save by default, planner directives, cache v3 and explorer route branching](records/2026-09-22-planner-directives-and-v3.md), 2026-09-22. Fresh build `planner-directives-build-20260922-a`, 1,117 source hashes and both binaries matched before the first world, before the smoke and after it; worlds under `.codex-research/planner-directives-20260922-a/`, smoke under `.codex-research/decision-explorer-v3-smoke-c75-20260922-a/`. **`bot_moveGraphSaveDefer` now defaults on** (0 restores the immediate write) and the snapshot is also flushed at each objective change after the census: all six reference cells decision-identical to the safety-save deferred arm, the four production caches byte-identical, the two seeded ones differing only by post-leg play. **Planner directives** `bot_movePlanAvoid`, `bot_movePlanRequire` (bit 26 of the state key, the ordinary search as fallback, `require_met=` on the plan line) and `bot_movePlanGen`, default off and inert on every cell. **The four seeded tapes replay on the seed graph at 15,950 / 16,725 / 15,975 / 15,925 ms with every entry hit**, and their leg `[ROUTE]` edges fold into v2 as **cache v3: 3,332 edges, `g1-9a85d509075ecbc1`**, with the four missing seeded edges 51453, 48185, 41945 and 59480 present as 3312 to 3315. On v3, harness and library off: **floor 36 of 36**, zero deaths, owner `move`; seeded regressions 17,500 and 20,700 ms; both respawn4 cases at class 50 **16,625 ms**; **class 125 16,375 ms** (four of four, 2,900 faster than v2) and **class 75 20,650** (four of four, 1,200 slower). v3's class-75 release plan takes 3312 and the seeded route's first eight edges unforced, then leaves it (13,281 ms planned); the in-trip replan takes 3315, 815, 816 and 750. **The explorer branches over routes** (`--route-branch`, `--reference-tape`): the depth-1 smoke at class 75 ran 16 worlds, requiring 815 and 750 took (20,650 ms), avoiding 680 gave a route confirmed at 19,625, two duplicates detected, best confirmed 17,925. The eight-class chain on v3 was prepared and not launched, by owner decision.

Preceding completed experiment: [deferred graph save, no legacy hand-off at a declined crossing, and route telemetry](records/2026-09-22-deferred-save-and-floor-safety.md), 2026-09-22. Fresh build `safety-save-build-20260922-a`, 1,117 source hashes and both binaries matched before the first world and after the last; 54 bench worlds one per call and 4 cost worlds, all retained under `.codex-research/safety-save-20260922-a/`. **`bot_moveGraphSaveDefer`** (default 0) snapshots the edge records and capped flag at each autosave and writes the last snapshot at `G_ShutdownGame`, `Graph_Autosave` and `Move_PrepareMap`. **All six reference cells are decision-identical between the arms and match their reference worlds** (seed 15,925 / 21,125 ms with the harness on and zero mismatches, v1 19,050 / 19,150, v2 19,275 / 19,450; 11, 13, 12, 13, 12 and 13 leg decisions), and **four of six saved caches are byte-identical**; the two seeded ones agree on every common record and differ only by one edge learned after the leg in the control world. Cost on v2, two alternated pairs: p99 **118,810 / 119,532 us** immediate against **63,538 / 74,001** deferred, worst **165,926 / 146,839** against **114,127 / 126,549**, dispatch total **0.52 / 0.65 s** against **0.06 / 0.07 s**; frames over 5,000 us still 103 to 132 of 800, every worst frame now a boundary search - row 5 not met. **A walk decline the movement owner can resolve (corridor, no path, miss) is now held under that owner** - the route's next crossing avoided and replanned, the walk's brake held - and never handed to legacy; no cvar, inert on every reference cell. **The eleven-death trip passes**: both respawn4 cases pinned at class 50 on the research seed complete at **19,400 ms**, zero deaths, owner `move`, no legacy line, zero parity mismatches; the hold fires where the old trip's one-frame hand-off was (+3,500 ms) and the trip never reaches the fatal lip. On v2 at class 50, first measured, both pass at **16,625 ms**. **Floor on v2, save deferred: 36 of 36**, zero deaths, owner `move`, no forbidden spend; seeded regressions 17,500 and 18,400 ms. **Every installed route prints a `[ROUTE]` line** of its edge indices outside any search (447 lines for 290 replans and 157 reroutes), parsed by `read_routes` in `tools/movement_graph_enrich.py` with none incomplete.

Before that: [production graph cache v2: the edges real runs committed, folded back in](records/2026-09-22-production-graph-v2.md), 2026-09-22. No C change; build `search-budget-build-20260922-c`, 1,117 source hashes matched before the first world and after the last. `tools/movement_graph_enrich.py` resolves every `reason=sequence` commit's `index`/`index2` against **the committing world's own saved cache** (`Graph_Save` writes edges in index order, disabled edges included; only the release's `Graph_ResetLearned` truncates, and pre-release indices it dropped are refused) and folds the records into cache v1 with `movement_graph_merge.py`'s identity. Over twelve campaign directories, 2,806 worlds, **12,079 named indices all resolved**, none refused; a sample of 280 commits puts every resolved takeoff within 64 units of the commit position. The seven seeded campaigns give **470 committed edges v1 lacked**; the cache-v1 explorers committed only v1 edges and their saves add **2,194 learned edges**. **Cache v2: 3,312 edges (3,254 strafe, 48 hop, 10 pad), identity `g1-9631cbeaef2e5ba8`**, `.codex-research/production-graph-20260922-b/production-graph.txt`. On v2, library and harness off: **floor 36 of 36**, one bench call per case, zero deaths, owner `move`, no forbidden spend; `red_ret_L1_respawn4` **16,125 ms** and `blue_out_L1_respawn4` **16,150** pass, as do both seeded regressions (19,650 and 20,700). **Determinism holds**: class 125 six of six at **19,275 ms**, class 75 four of four at **19,450**, one decision sequence each - **225 and 300 ms slower than v1's cells**, with the ranking taking enriched edges from the first decision. The eight-class explorer chain on v2 was launched detached (`.codex-research/decision-explorer-v2-chain-20260922-a.sh`) and has not reported.

Before that: [the search budget: the same decisions, spread over the flight, and no cheaper](records/2026-09-22-search-budget.md), 2026-09-22. On fresh build `search-budget-build-20260922-c`, 1,117 source hashes matched before the first world and after the last. `bot_moveSearchBudget` (default off, a per-bot per-frame **candidate rollout** count, not microseconds, so the split point is a property of the search and not of the host) predicts the boundary the next straight search will run at - the landing of the committed sequence, or the retry a failed search scheduled - installs that predicted state on the bot for a bounded chunk of the ordinary search on each frame before it, and at the boundary takes the judged candidate set only when the path, the objective, the player state (the fields the parity harness compares), the executor pair (where the landing tap bit lives), every constructed candidate and a validating rollout of the winner from the actual state all agree; anything else falls back to today's whole search. **Decisions are identical on all four declared cells**: seeded class 125 **15,925 ms / 13 / 5** and class 75 **21,125 / 16 / 8** with the harness armed, production class 125 **19,050** and class 75 **19,150** with it off, each matching the same cell on the same build with the budget off decision for decision (13 of 13, 16 of 16, 14 of 14, 14 of 14). Over the 36-case floor and both seeded regressions, **492 searches and 328 served from a set judged in flight (66.7 percent)**, one refused for a replanned path and **zero** refused on state, executor, candidates or validation; floor **36 of 36**, zero deaths, movement owner only, no forbidden spend. **It does not meet row 5 and does not move the tail**: two paired cost worlds, arms alternated, p50 **117 / 112 us** to **3,028 / 1,414** (the work being spread), p95 **71,657 / 90,012** to **104,478 / 80,701**, p99 **226,991 / 182,785** to **244,111 / 242,457**, worst frame **2.41 / 0.56 s** to **3.42 / 1.72 s**, frames over the declared 5,000 us budget **126 / 123** to **364 / 337** of 800, boundary total **1.6 to 2.1 times** the control. Two things the row inherits: the budget-off control **on the production cache** is already about twice the 2026-09-22 smoke at p95 and p99 (the smoke ran on the 10-edge map-load graph, not the 676-edge cache), so row 5's starting point is worse than it has been carrying; and the candidate rollouts are **not** the tail - the budget moves them (`[SPIKE]` 339 to 349 and 350 to 371 a leg, `[REROUTE]` 37 to 35 and 34 to 33) while frames over the server frame stay, in both arms. The cvar stays **default off**.

Before that: [the production graph cache, and the release that stops the harness deciding](records/2026-09-22-production-graph.md), 2026-09-22. On fresh build `production-graph-build-20260922-a`, 1,117 source hashes matched before the first world and after the last. **The open item of the graph-binding record is closed: pre-release learning was the whole cause.** A fixture world does not begin at the release - the bot's own AI plays from its spawn until `bot_corner_start` arrives - and every strafe edge it learns there joins the state planner's routes. On the ten retained unseeded worlds pinned at level 3,125 the pre-release learned set **predicts the trip exactly**: five worlds that learned cell 47 to 4949 all returned **19,700 ms** with 12 of 12 decisions identical, four that learned nothing or cell 61 to 6167 all returned **16,725**, and one that learned cell 50 to 4948 returned **18,475**, with both parity arms inside the groups. The first post-release route carries one strafe edge with the learned edge and none without it. **The release now restores the learned set to the graph as loaded** (`Graph_ResetLearned`, called from `bot_corner_start` and nothing else, so production - which has no release - is unchanged). **A production graph cache is prepared from the bot's own runs**: the union of the gate campaign's unseeded arms, 144 world caches folded to **676 edges** (650 strafe, 16 hop, 10 pad) over 14,249 cells, identity **`g1-0e5af7aba9b5e908`**, against the map-load graph's 10 and the research seed's 94,711. **With it a start class is a cell again**: ten worlds at class 125 return **19,050 ms** and one 12-decision sequence, six at class 75 **19,150 ms** and one 13-decision sequence, each across three different release levels of its class. **The floor is 36 of 36** on that configuration - production cache, library off, harness off - zero deaths, movement owner only, no forbidden spend, and **both respawn cases the research seed killed now pass** (16,300 and 16,150 ms), as do both seeded regression cases. Both seeded control cells still reproduce at 15,925 / 13 / 5 and 21,125 / 15 / 7.

Before that: [tapes bound to their graph, the search decoupled from the parity harness, and a cost path for row 5](records/2026-09-22-graph-binding-and-decoupling.md), 2026-09-22. On fresh build `graph-binding-build-20260922-b`, 1,117 source hashes matched before the first world and after the last. **The search no longer needs the parity harness**: the two seeded control cells reproduce with `--parity 256` at **15,925 ms / 13 straight / 5 state** and **21,125 / 15 / 7**, and with the harness off return the same times, the same leg decisions (13 of 13 and 15 of 15) and **zero** comparisons - zero because nothing was armed. An unseeded level-matched pair returns **19,700 ms** with and without it, 12 of 12 decisions identical - though ten unseeded worlds at that same release level return three different times and both arms share them, so unseeded the pair is one sample each and not a reproduction. **A tape is now bound to the graph it was recorded on**, `g1-` plus a digest of the cells and edges as loaded: the 2026-09-16 seed is `g1-98eb1e97c24a4e21` (94,711 edges) and the unseeded build of the same map `g1-301dd39cbead57f6` (10), and a seeded library staged unseeded is refused at load (`keys=0`) or, tape by tape, on the exact rung with `rung=graph_mismatch` and zero entries, falling back at the leg's first think; the same library in a seeded world still hits 11 of 11 at 15,925 ms. **Row 5 has an instrument and a first reading far outside the budget**: four bots, one server, 800 frames, p50 **67.5 us**, p95 **36,800**, p99 **96,870**, worst **272,692** against the 25,000 us frame, **114 of 800 frames over the declared 5,000 us budget and 64 over the whole frame**, 96 percent of it the Pmove-boundary search. Nothing was tuned; one run measures the instrument, not the row.

Before that: [the level-1 exit gate, rows 1 to 4 on one build](records/2026-09-22-gate-l1.md), 2026-09-22. On `tie-break-build-20260922-a` with the four-key decision-tape library, **rows 1, 2 and 3 are met**: floor 36/36 with the library off and 36/36 with it on plus both seeded regressions, zero deaths, movement owner only, zero mismatches with a non-zero comparison count in every world and no forbidden spend; equal-weight median **15,962.5 ms** and tail **16,725 ms** against the row's 16,500 and 18,000, at the six repetitions per class the row asks; waiting shares 30.4 / 55.1 / 37.9 / 35.0 percent against controls of 71.2 / 59.5 / 37.9 / 50.5, no class waiting more than its control and both rows moving together at every class. **Row 4 is met unseeded** - twelve cases, three repetitions per arm, 72 of 72 - and **not met seeded**, where `red_ret_L1_respawn4` and `blue_out_L1_respawn4` die in both arms; a four-world probe isolates that to the 2026-09-16 seeded graph, not the library. **Row 5 is not measured, so the gate is not reported passed.** A tape also turns out to be bound to the graph seed it was explored on: unseeded, an exact-rung selection hits zero entries.

Earlier completed experiment: [library-rescue arm](records/2026-09-21-library-rescue.md), **not accepted**. Offering every envelope-admitted strafe edge when the straight search admits nothing does commit library flights - 1,571 offered, 113 admitted, 45 committed over 24 worlds - but the trip gets slower: declared-weight mean 19,416.4 ms against the control's 17,427.7, a **1,988.7 ms loss**, while the waiting share falls only 2.0 points (60.3 to 58.3 percent) against the gate's 15 percent. The two rows move together class by class: both improve at classes 75 and 100 and both worsen at 125 (+3,425 ms, +18.0 points) and 150 (+2,000 ms, +9.8), which carry 77 of the 93 declared weight. 44 of 48 worlds complete, zero deaths, movement owner only, 30,406 parity comparisons with zero mismatches and refusals; four worlds gave up on the class pin. `bot_moveLibraryRescue` stays default off. Arm B does not run: the leading rescue refusal is `corridor` (726 of 1,458), not objective-flag contact, so its precondition fails.

Latest repair: [parity mixed-refusal guard](records/2026-09-21-parity-review-fix.md), with 41 actual-C checks passing and the retained original failing the mixed-refusal regression. Fresh native validation passes 36/36 plus both seeded cases: 19,898 reported comparisons, zero mismatches/refusals/deaths, movement owner only. Historical parity counts below retain their original build binding.

Current next goal (2026-09-21 owner direction, second revision): level 1 is
closed by the [level-1 exit gate](../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21), five rows
reported together on one build. Its first work item, the plan-coverage diagnostic,
has reported; the explorer its outcome points at is not built before the owner
picks one. No further candidate-fan or selection screens. Keep successor aiming
and rerouting enabled, exit-speed weight 500 and state preference enabled. This
supersedes the first 2026-09-21 direction (one recurring transition, one bounded
candidate-generation variation) and older next-action suggestions below.

The [trigger shadow measurement](records/2026-09-21-trigger-shadow.md) and
[rescue follow-up](records/2026-09-21-trigger-shadow-rescue.md) are corrected after
review: `swept` tests an enclosing axis-aligned box against brushes, not the actual
swept hull. Its 107/60 `swept_not_endpoint` cases remain unresolved; they are not
proven protective crossings. The 39 straight-search blue-flag verdicts are initial
`arrives` conversions, not final admissions or coverage of all 51 census
disagreements. Later chain/path gates and other rollout paths remain unmeasured.
Keep trigger admission conservative and the default-off diagnostic off for speed
runs. Deprioritize an admission repair because no speed payoff is demonstrated;
this is a priority decision, not proof that only 30 refusals are recoverable.
The diagnostic changes throughput, so its absolute counts describe instrumented
searches and stable observed rates are not guaranteed for uninstrumented runs.

## Would a plan the bot already has have applied where it waited?

[The plan-coverage diagnostic](records/2026-09-21-plan-coverage.md), 2026-09-21,
read-only: 60 waiting runs and 107 committed runs from the sixteen
`confirm-{0,1}-r{1..8}` worlds, each first waiting frame's reconstructed state
tested against every strafe edge of the 2026-09-16 seeded cache and of the
world's own cache under `Graph_StrafeAdmits`.

| | waiting | committed control |
| --- | ---: | ---: |
| Covered | 34 of 60 (56.7 %) | 66 of 107 (61.7 %) |
| One axis over | 26 (cone 18, reach 4, speed 4) | 41 |
| No edge near on any axis | 0 | 0 |

Admitting edges per covered run: median 20, range 1 to 93, and 30 of 34 have
one leaving the bot's own cell. Fifteen of the eighteen cone margins exceed
110 degrees, so the excluded states point away from the edges near them;
dropping the cone axis lifts waiting coverage to 55 of 60, an upper bound.
Clearing the tap bit leaves 0 of 60 covered. The heading is not retained at
frame resolution: reconstructed two ways, they differ by a 7.7-degree median
and 18.9-degree worst, which moves three cone cases and no more. The tool
reproduces the game's own logged `envelope=` on 130 of 150 records, 18 of the
20 disagreements resolving when the unlogged cell choice is relaxed and 2
remaining on the reconstructed heading. Each world's own cache is the seeded
library to within four edges, so it is not independent evidence.

Regenerated on the same worlds under LEG_BUDGET2: waiting is 993 of 1,834
grounded frames, **54.1 percent**, against gate row 3's 15 percent, and 1,374
of 1,885 candidate outcomes on those frames are `refused` with the
trigger-contact bit. Covered means the envelope admits an edge - not that its
plan would be committed, validated, aimed at the objective or faster - and
the control is covered no less often than the waiting frames.

Current approach: [the refined method](records/2026-09-19-refined-method.md)
prioritizes connected independent movement, then optional prepared-plan
integration. It changes the work order, not the measured scorecard.
[The repair follow-up review](records/2026-09-19-repair-follow-up.md) verifies
retained claims and records two remaining lifecycle/probe issues. Independent
approximately-15-second quality and production runtime cost remain unqualified.

Latest: [the trigger shadow measurement, 2026-09-21](records/2026-09-21-trigger-shadow.md), which answers [the offer limit census](records/2026-09-21-offer-limit-is-the-trigger-test.md).
Read-only census over the ablation's accepted worlds plus a declared arm at
the two heavy start classes. Of every `[SPIKE]` candidate rollout, roughly
half is refused - 52.7 % at class 75, 57.8 % at 100, 47.1 % at 125 - and
**every refusal is `unsupported=256`, the trigger contact, 5,995 of 5,995**,
with falls and failed onward walks the small buckets beneath. `bot_forecastItems=1`,
the one existing lever, is **refuted**: 12 cells of 12 over 25 native worlds,
**17,575 ms at class 150 and 15,925 at class 125, identical to its controls**,
which reproduce the preference collections exactly; refused rollouts move only
49.6 % to 49.8 % and 35.9 % to 34.0 %. Refusals are not independent - removing
59 medpak contacts raised `trigger_hurt` contacts 304 to 334 in a matched pair
- so the contact defers rather than disappears. `trigger_hurt` is 55 to 60 %
of contacts and is reached through a swept axis-aligned-box overlap test with
fixed hull extents. Only 17 of 304 logged endpoints were inside the entity
bounds, which does not establish false positives: the hull has volume and
the endpoint does not describe the entire movement interval. Later failures
are censored by these first refusals; recoverable speed is unmeasured. Those worlds also carry the
repaired parity harness: **8,510 frames compared, zero mismatches**. Nothing
is installed and no speed win is claimed.

Before it: [the parity harness compares again, 2026-09-21](records/2026-09-21-parity-repair.md).
Two source changes in the harness, both inside `bot2_move.c`: the parity build
fills its own frame buffer instead of the one every rollout shares, and a
rollout stopped only by a trigger contact arms over its prefix rather than
being discarded. [The diagnosis](records/2026-09-20-parity-zero-comparisons.md)
established the cause from retained worlds and two cvar-only worlds: every
parity build refused with `unsupported=256`, and **119 of 119 parity refusals
were the objective flag**, matched to their trigger lines by frame index. On
the repaired build the floors give **36 of 36 and both seeded named
regressions, zero deaths, movement owner only, 19,413 frames compared, zero
mismatches, zero refusals**, with 41 of 63 tallies covering a committed
sequence and all 63 truncated at a trigger. Reintroducing the shared buffer
alone returns **935 mismatches, all `after_state`** - the one comparison that
reads the buffer after the searches run - so the repaired zero is a
measurement and not an absence. `compared` is carried by the periodic tally
and is a **lower bound**; the probe world's pre-start commit differs between
builds at the same start level while every commit of the timed trip and the
completion are identical, and that difference is unexplained. Nothing is
installed; the harness runs under `bot_moveParity`, which only the bench sets.

Before it: [one commit is worth 1,900 to 5,300 ms, 2026-09-20](records/2026-09-20-preference-decisive-commit-priced.md).
**No native source change, no rebuild** - `bot_moveStatePrefer`,
`bot_moveSelectNth` and `bot_moveSelectCand` are existing cvars. The commit
the localisation found is forced in both directions at three start classes:
**36 cells of 36, 93 native worlds**, one distinct completion per cell, zero
deaths, movement owner only, and **all six controls reproduce the preference
collections to the millisecond**. At class 75 the state plan is wrong from
both sides - holding the judge's own winner saves **1,900 ms** (21,125 to
19,225) and forcing the state plan into the preference-off trip costs 1,425
(17,975 to 19,400). At class 125 it is right from both sides - holding the
winner costs **5,300 ms** (15,925 to 21,225), more than the whole class
delta, and forcing it into the off trip saves 1,825. **At class 100 the two
sides contradict**: holding the winner is 150 ms faster, while forcing the
state plan into the off trip saves 3,050 (21,150 to 18,100), the fastest
class-100 trip in the grid. The frame is worth seconds and its value is
**policy-dependent and not additive**. A rule declining the preference only at
the class-75 decision would be worth **122.6 ms** weighted (17,271.8 against
17,394.4) and 475 ms equally weighted; the best of the four policies measured
here, chosen per class, is 340.3 ms weighted but **1,106.25 ms equally
weighted** (17,393.75 against 18,500). The same combination priced two ways
differs by more than a factor of three, so that figure is mostly the borrowed
start distribution, and best of what was tested is **not a ceiling** on
selection: the other admitted candidates, the later firings and their
interactions are untested. Nothing is installed.

Before it: [where the preference acts, 2026-09-20](records/2026-09-20-preference-localisation.md).
**No native run, no source change, no rebuild** - the judge's own ranking is
reconstructed from the retained `[SPIKE]` rollouts of both preference
collections, `score = margin + ExitCredit`, exact where the walk reference is
shared and reported as approximate otherwise. Over **378 commits in 24
worlds**: 141 exact, 120 reproducing the logged score, **21 preference firings
and none unexplained**, no firing at all in a preference-off arm, and the
candidate set matching the commit line at 378 of 378. The earliest in-trip
firing is the largest at every class that has one, and at classes 75, 100 and
125 it is the **same decision**: commit 2, about 2,950 ms in, on edge 36420.
The score gap does not separate the losing class (22.1, between 14.9 and 30.8
at two winners) and neither does domination - the preferred plan is worse on
both score terms at class 100, where the preference wins 1,775 ms. What orders
all four is the **speed the bot arrives with**: 99.1 units/sec where it loses
against 250.0, 250.1 and 242.1 where it wins. Four cases, a hypothesis rather
than a rule, and it prices nothing.

Before it: [removing the planned-candidate preference is refuted, 2026-09-20](records/2026-09-20-preference-removal-refuted.md).
**No native source change, no rebuild**; `bot_moveStatePrefer` is a cvar and
the forced-candidate override is off in every arm, verified from the logs.
The two missing start classes complete the grid: **12 cells of 12, 38 native
worlds**, one distinct completion per arm. With the preference off, class 125
regresses **+3,025 ms** (15,925 to 18,950) and class 100 **+1,775** (19,375 to
21,150), against -3,150 at class 75 and +150 at class 150. **Declared
weighting 17,394.4 ms on against 18,557.3 off, a 1,162.9 ms loss**; equal
weighting 18,500 against 18,950, so the direction does not rest on the
borrowed weights. The regressing classes are 93.5 percent of starts and would
have to fall to 68.3 to break even. All four `StatePrefer=1` cells reproduce
the pinned confirmation's 500 column to the millisecond. Search cost pooled
in-trip is unchanged: median 36.50 ms on against 35.05 off. **The two-class
reading is superseded and `bot_moveStatePrefer=1` is confirmed**; the
preference is right at three classes of four, so the target is a conditional
preference at the decision, not removal.

Before it: [the planned-candidate preference costs the slow class, 2026-09-20](records/2026-09-20-state-preference-costs-the-slow-class.md).
**No native source change, no rebuild** - `bot_moveStatePrefer` is an existing
cvar and the forced-candidate override is off in every arm, so the build
behaves as the reference build. Both start classes pinned, three repetitions
an arm, **12 cells of 12, 20 native worlds**, one distinct completion per arm:
class 75 **21,125 to 17,975 ms with `bot_moveStatePrefer=0`, -3,150 ms**;
class 150 17,575 to 17,725, **+150 ms**. **Floors pass unpinned at that
setting: 36 of 36, zero deaths, movement owner only, zero parity mismatches
out of zero comparisons, and both seeded named regressions.** 17,975 ms beats
every frame-forced arm of that leg, the best of which was 18,825, because the
preference overrides at more than one commit. **Not a configuration proposal**:
classes 100 and 125 are unmeasured so the declared-weight mean cannot be
computed, class 150 regresses, search cost was not recorded, and nothing is
installed.

Before it: [selection at a second start class, 2026-09-20](records/2026-09-20-selection-second-start-class.md). Start class
150, `bot_moveExitSpeed=500`, **30 cells of 30, 71 native worlds**. Three
commits admit 3, 4 and 2 candidates and **every one was forced**, so each span
is the frame's full range: 1,000, 2,575 and 1,075 ms. **The ranking commits
the fastest candidate at 3 of 3, and it is the score maximum every time** -
the four scores at commit 5 order the four completions exactly, and commit 10
separates two candidates with identical `progress` by score alone. The
selection failure is therefore **class-specific, not general**. The `kind`
field separates the classes: at class 75 commits 4 and 5 a planned **state**
candidate executes although an admitted straight candidate scores higher and
is 1,725 and 1,150 ms faster.

Before it: [selection across frames, 2026-09-20](records/2026-09-20-selection-across-frames.md). Start class 75, the same
override at commits 2, 4 and 5, **33 cells of 33, 94 native worlds**. Every
frame tested carries 1,525 to 2,300 ms and the ranking's own choice is worst
or near-worst at 4 of 4; **commit 5 admits exactly four candidates, all four
were forced, and the ranking took the slowest**. The best trip there has the
**lowest** score, two candidates are the same point in progress and carried
speed yet complete 600 ms apart, and the executed candidate is not the score
maximum at any of the three frames. The spans are **not additive**.

Before it: [the frame under exit speed 500, and the horizon priced, 2026-09-20](records/2026-09-20-select-override-exit-speed-500.md).
**No native source change, no rebuild** - the frozen diagnostic build
`.codex-research/select-override-build-20260920-a`, every treatment a cvar.
The forced-candidate diagnostic repeated with `bot_moveExitSpeed=500` fixed in
every arm, start class 75, three repetitions an arm, **12 cells of 12, none
incomplete, 34 native worlds**, one distinct completion per arm: control
21,125 ms, forced cand 5 21,125, forced cand 13 19,950, **forced cand 1
18,825**. One frame spans **2,300 ms** and the ranking takes the worst of the
three; of the 1,025 ms that 500 loses at this class, the frame's own choice
between the two weights' candidates is worth 825 ms under the 2000 policy and
1,175 ms under 500. The same committed candidate differs by at most 200 ms between the two
policies. Zero deaths, movement owner throughout, exactly one forced commit
inside each trip; parity still makes zero comparisons. Ranking the realized
continuations by distance still to run at common elapsed-time endpoints is
stable only from **frame + 7,000 ms** under both policies - about half the
14,000 to 16,350 ms leg that follows - and at frame + 5,000 ms the true winner
ranks last. That is an oracle bound on what a bounded rank could know, not a
bounded rank, and nothing is installed.

Before it: [the start level is pinned in the harness, 2026-09-20](records/2026-09-20-pinned-start-level.md).
**Tool change only** - `movement_bench.py` and `movement_bench_lib.py`, no
native source change, no rebuild, all 1,117 manifest sources re-verified on
every run. `--corner-level MS` holds `bot_corner_start` for a chosen level,
**verifies the level the world reports**, and retains a world that misses
under `<case>.miss-N` while a fresh one replaces it. Calibration: the start
lands on the level of the last play-log row the runner has seen, 3 worlds of 3,
so the default lead is zero. Reachable: `corner_level` **3,350 in 6 worlds of
6, returning 19,050 ms every time**; **3,325 in 3 of 4, returning 16,450 every
time**, after the post-add poll was cut from 500 ms to 50. Unpinned, the same
configuration spreads 4,300 ms. Only two strata are reachable, because the
runner's clock is a play log flushed in 200 ms bursts. Unset - the default, and
what acceptance floors use - behaviour and retained layout are unchanged. Ten
new tests; the movement Python suite is 112 and passes.

Before it: [the fixture's start level decides the leg, 2026-09-20](records/2026-09-20-start-level-determines-the-leg.md).
**No source change** - 100 native worlds on the frozen reference build.
`movement_bench.py` sends `bot_corner_start` whenever its polling loop gets
there and records the resulting `corner_level` as bookkeeping. Conditioned on
that number the stand run is deterministic: **25 ms worst-case spread over 15
(start level, arm) cells and 87 worlds**, mean 16.7 ms, against **3,525-5,250
ms** unconditioned within an arm. The start level fixes the first touchdown in
**99 of 100** worlds and the same mapping holds on the 16 retained confirmation
worlds of the previous step. It is set before the runner moves, so it is
pre-treatment; the first touchdown is not, because at start level 3,400 the
credit itself changes it. This is why leg time defeated three pilots. Highest
-value next action, and a tool change rather than a movement one: **hold the
corner command until a chosen level time** - done the same day, above. Levels
are `corner_level`; the leg budget's first touchdown is one frame later. Not
understood: why a level maps to a given first flight, or whether the mapping
survives another build, map, case or level.

Before it: [the finer exit-speed sweep, 2026-09-20](records/2026-09-20-exit-speed-finer-sweep.md).
**No source change** - five arms (250, 500, 750, 1000, 2000) alternated
round-robin over 20 sequential rounds, 20 worlds an arm, the incumbent
re-measured in session. Stratified on start level and weighted by stratum
frequency: **500 at 16,976 ms against 2000 at 17,942**, with 250 at 17,128, 750
at 17,735, 1000 at 17,740. That 966 ms reproduces the paired design's -975 ms
**to within 9 ms** from a different design in a different session. The optimum
is shallow and start-dependent: at the 43-world stratum 250 and 500 tie and
everything from 750 up loses 1,500 ms; at the 34-world stratum 500/750/1000 tie
and both ends lose 505 ms. **The tail is explained** - pair 8's 4,750 ms was
about 3,800 ms of two worlds drawing different start levels plus a real 975 ms
cost of 500 at start level 3,300, reproduced here at 21,000 against the earlier
21,025, whose mechanism is a **27-frame wait** at a landing 55 units/sec faster
that the geometry admits no continuation from. Refuted on the way: landing
speed does not predict landing cost - median one frame in every band above 350
units/sec, and the most expensive band is 470-510, not the fastest. Still a
configuration value, **not installed**; floors at 500 stand from the previous
step; parity still makes zero comparisons.

Before it: [the exit-speed credit is set too high, 2026-09-20](records/2026-09-20-exit-speed-sweep.md).
**No source change** - `bot_moveExitSpeed` is a cvar on the frozen reference
build. `ExitCredit` is `landSpeed * ms/1000`, so at 2000 the credit is twice the
landing speed and dominates `score = margin + ExitCredit` (a logged commit had
`exit_credit=946.7` against `margin=539.3`). Eight fresh counterbalanced pairs at
**500** against 2000: mean **18,693.8 -> 17,718.8 ms**, median **19,025 ->
17,512.5**, best 16,275 -> 15,750, and the predicted mechanism moves with it -
waiting frames **77.9 -> 60.9**, run-up frames 32.2 -> 25.9, mean speed 387.7 ->
408.7 units/sec, search cost unchanged. Five pairs improve, three regress, and
**the worst run is 1,000 ms slower** (21,025 against 20,025), unexplained.
Screening shows the relationship is not monotonic: 0 is worse than both.
Floors **36/36, zero deaths, zero parity mismatches** (four setup failures at
`--parallel 4` re-run serially and passing). Parity still makes zero
comparisons; nothing installed; the value 500 itself is not established.

Before it: [searching more often does not help, 2026-09-20](records/2026-09-20-search-rate.md).
The waiting frames that own 54.2 % of grounded time are **availability, not
latency**. `bot_moveSearchRate` replaced the 250 ms floor in `StraightEligible`;
the treatment is visible (40 gaps of exactly 25 ms at rate 0) and the control
reproduces `confirm-1-r7` byte for byte. Three runs an arm: control median
**16,275 ms**, rate 0 **18,525**, rate 100 **19,150**. No arm improves, waiting
frames barely move (59.0 -> 52.0 and 58.7) and run-up frames rise. The reason
was already on record - during those frames the search had judged 1,029
candidates and admitted none, because the refusals are falls. A low sampling
rate over an interval whose outcome never varies is not evidence that sampling
is the constraint. Source reverted; 1,117 of 1,117 manifest sources match.

Before it: [pricing the route by flight cost: refuted, 2026-09-20](records/2026-09-20-route-pricing.md).
Two corrections and a refuted change. All **46** in-leg replans came from
`Graph_PlanState`, which already prices links from the speed carried, so "the
planner prices routes at walk speed" is withdrawn: the walk model survives only
in the reroute gate, where `RerouteInstall` has been logging both prices side
by side (`cost_ms=10142` against `walk_ms=25335`, about 31 s claimed on a
16,450 ms leg). The reach-versus-straight rule is also refuted - 55 % of
tap-now candidates are admitted where the reach overruns against 28 % where it
fits. Substituting flight pricing into the gate (`bot_moveRoutePrice`, default
off, control reproduces `confirm-1-r7` and `-r6` byte for byte) moves the
median 75 ms and the tail 2,500 ms the wrong way, and eliminates reroutes
(13 -> 0-1); removing the margin entirely does not restore them. The mechanism:
the candidate is charged a **measured** flight and the incumbent the planner's
**expectation**, so the walk model was a pessimism compensating for that
asymmetry. Source reverted, tree hash-verified back to the reference manifest.

Before it: [the trigger contacts are falls, 2026-09-20](records/2026-09-20-trigger-classification.md).
One diagnostic world on the frozen reference build with the existing
`bot_forecastTriggerLog` enabled; no source change. The run reproduces retained
world `confirm-1-r8` byte for byte (same 761-sample `trace_sha256`), so the
instrumentation did not perturb it. All 547 contacts classified: **288
`trigger_hurt`**, 172 `team_CTF_blueflag` (all in the final approach from
+15,625 ms), 53 `team_CTF_redflag` (the stand start, already `deferred`), 33
`item_medpak_instant` at two sites, 1 `trigger_push`. Pooled over the expensive
grounded runs: hurt 221, medpak 33, blueflag 47. **The refusals are correct and
there is no recoverable pool**; the 24-unit run-up is a brake in front of a
hazard, not a defect. `bot_forecastItems` would recover 33 of 288 in-leg
contacts. Shorter aim fractions cannot help - `d0` sets the hop's heading, not
its reach. The lever is the route: the planner prices reroutes at walk speed
while the bot crosses at 400-600 units/sec.

Before it: [what the outbound leg's eighteen seconds are made of, 2026-09-20](records/2026-09-20-leg-budget.md).
Read-only accounting of the retained confirmation worlds, no native run. The
route is a fixed **7,161 units** (2.2 % spread), so leg time is distance over
mean speed: 15,000 ms needs a **477 units/sec** mean against a best observed
442.1 and a peak of 582.9. The leg is already **84.7 % airborne**; of the
grounded frames, **54.2 % are the search admitting nothing** and **24.4 % are
the committed plan's own 24-unit run-up**, together 78.6 %. During those
waiting frames 1,029 candidates were judged and none admitted, **748 of them
refused on unclassified trigger contact**. Holding entry speed through those
frames is worth 16,250 → 15,690 ms on the best world, a lower bound that gives
no credit for the re-acceleration saved. Nothing installed; no admission
exception is implied.

Before it: [actual-landing successor proposals, 2026-09-20](records/2026-09-20-successor-aim.md).
Eight fresh counterbalanced pairs improve mean **18,840.625 → 17,943.75 ms**,
median **18,512.5 → 18,125**, maximum **20,350 → 19,375**; six pairs improve,
two regress. The 16,450-ms trajectory repeats three times. Six/five common
logged traces limit independence. Both 36-case floors and two seeded named
regressions pass (74/74, zero deaths). Recorded median
search cost rises 22.2 → 38.9 ms; parity makes zero comparisons. The 8-unit
run-up regresses and is reverted; component separation finds no replacement
for the reroute/credit combination. Fifteen seconds remains unachieved.

Before it: [measured speed win, 2026-09-20](records/2026-09-20-speed-win.md).
Short-landing proposals preserve the native admission checks. On that build,
validated rerouting plus exit-speed ranking wins in eight fresh counterbalanced
pairs: **median 17,900 versus 18,887.5 ms**, mean 18,493.75 versus 19,346.875,
maximum 20,275 versus 20,750. Six pairs improve, two regress; four/five distinct
common-prefix traces limit independence. Candidate floor 36/36 and two seeded
regressions pass with zero deaths. Recorded search cost increases; parity makes
zero comparisons. Experimental flags remain default off, nothing is installed,
and the roughly 15-second target and production qualification remain open.

Before it: [landing-search investigation, 2026-09-20](records/2026-09-20-landing-search.md).
All four retained missing-search states reproduce with `off_path_index`:
landing admission recognizes a future strafe chord, while live progress keeps
the takeoff index. A shared final-touchdown correction preserves existing
geometry bounds and ordinary-walking restrictions. Actual-C regressions pass
for all four states and fail against the exact baseline; the fresh native
floor is **36/36 green**. That investigation's eight alternating seeded unguided pairs
are **not a speed win**: median 20,162.5 corrected versus 19,375 ms baseline,
mean 19,796.9 versus 19,584.4. The 18,275 ms best does not meet the target or
establish improvement. Corrected diagnostics expose `straight_too_short` at
the first reproduced state. The other targeted tap refusals split into 23
selected-path rejections and 29 trigger unknowns; their safety classification
remains open. Parity makes zero comparisons and stays unqualified.

Before it: [review resolution, 2026-09-20](records/2026-09-20-review-resolution.md)
records the code regression repair and the corrected retained momentum analysis.
[What the kept speed buys](records/2026-09-19-what-the-kept-speed-buys.md)
now includes a reproducible audit with source hashes and complete 500 ms windows
strictly inside the observed outbound interval. The old 67-landings analysis
included setup and return-leg activity. Corrected: 58 pooled landings, 33
next-frame airborne / 25 grounded; grounded categories are 13 commits,
5 searches admitting nothing, 7 without a recorded straight search.

The r1/r2 safe-window traces have all 760 parsed momentum rows identical.
Counting each distinct trace once yields 39 landings, 22 airborne / 17 grounded,
with grounded categories 10/3/4. Pooled speed means are 403.7 -> 403.9 for
next-frame airborne landings and 445.4 -> 342.9 for grounded landings at +25 ms;
the latter reach 257.3 at +100 ms. Distinct-trace means support the same
association (399.0 -> 399.1 and 445.6 -> 343.5). These are group observations,
not independent per-landing trials or a proof that heading is irrelevant.

Committed plan length matters: all 26 pooled middle-of-chain landings leave
the ground next frame (18/18 distinct); sequence endings, including one-hop
plans, do so 7/32 (4/21 distinct). All 13 pooled grounded commits select a
24-unit run-up, and two have an admitted tap-now alternative. An admitted
alternative is not an executed counterfactual. Old coarse 4 Hz heading,
correlation and matched-position conclusions and median-commit ownership
inferences are withdrawn pending reproducible, correctly bounded evidence.
The corrected record links the original snapshot and hash-bound audit.

Before it: [the guided corridor diagnostic,
2026-09-19](records/2026-09-19-guided-corridor.md). The search was given a
human route's coarse polyline as the only guidance - positions, no commands,
timing or states - and generated its own connected native movement from the
matched stand. Guided onto the human's own line it completes in
**20,500-20,725 ms** against the human's **14,800**, and unguided on the same
build in **19,150-19,600**, so **the corridor is not the limit**. The bot gives
up 180-190 units/sec at about ten joins a run, roughly 1,650 in total, where
the human pays nothing (airborne 93.2% against the bot's 81%). Those refusals
are **three different things**: over 262 candidates, 92 are clean rollouts
rejected only for leaving the planned path, 39 are stopped on an item or flag
contact (unknown, not bad), 18 are genuine falls into the map's hurt volume
and 22 are mixed. `unsupported=256` is `BOT2_FORECAST_TRIGGER`, a rollout
stopped on trigger contact, not a failed landing - the first version of this
entry read it as one. By class the speed given up is about 1,028 units/sec
over four joins where the flight really does fall (**a correct refusal**),
629 over four pure selected-path rejections, 106 at the join where the
rollout is refused for touching the enemy flag, and 126 at one join never
searched. Landing-frame coverage is not auditable today: the skip diagnostic
is gated on the onward stage, which fired once all run, and counting events
instead gives 11 of 13 last-hop landings searched with 2 silent.
The 36-case floor on that build is 36/36 green with the corridor unset.
This answers the expressiveness question only: independent quality
(19,150-19,600 against roughly 15 seconds) and runtime cost stay open.
It also corrects a physics claim: the bench worlds already run
`pmove_fixed 1`, `pmove_msec 8`, `pmove_float 1` and `sv_fps 40`, set by
`addrunner` itself, which is exactly the recorder's binding. The floor on
that build records **0 parity comparisons** over all 36 cases against 6,475
refusals, so it qualifies behaviour and not parity.

The directed experiment that followed measured the selected-path rejection.
`bot_moveReroute=1`, eight sequential alternating repetitions per arm, keeps
61% of joins on the landing frame against the control's 53% and gives up
1,527 units/sec against 1,591, with **completed stand time unchanged** - mean
20,356 against 20,031, median 20,362 against 20,150. Review then found two
defects in that pass: the route installed at the commit could belong to a
different candidate than the one selected, and the pass forecast its
continuation against the path the flight leaves while executing the
replacement. Both are fixed and neither is measured, so the arm establishes
that this pass did not shorten the trip, not that the selected-path rejection
is addressed.

**Re-measured on the corrected pass**, nine sequential alternating
repetitions (one dropped from both arms for an RCON fixture refusal, an
environment failure): median **19,550 ms against the control's 19,600**, mean
19,669 against 19,569, five of eight pairs faster, per-pair deltas -900 to
+1,175 - **indistinguishable at this sample size**. The corrected pass
installs 11 alternative routes over its eight runs against the defective
pass's 18, and both new guards stayed silent (0 drift refusals, 0 mismatch
refusals, 0 deaths, 0 parity mismatches). Correcting the defects neither won
nor lost, so the continuation's validity is not what stands between kept
speed and saved time; heading is the hypothesis that survives. The
joins-kept metric is **unqualified**: the published 61% is not reproducible
from the retained logs, one natural reading gives 52% against 56% and another
reverses the sign, so only the route times carry.

Before it: [the four review repairs, measured,
2026-09-19](records/2026-09-19-review-repairs.md). All four
[review findings](records/2026-09-19-review-fixes.md) are repaired, each with a
regression shown to fail against the defect first. **The retained route drift
figures were mostly measurement error**: checkpoints were read at the first
command of their server frame rather than at the command that produced them,
and on the repaired build the three routes reproduce their recordings to
0.29-0.35 units where the retained runs reported 23.8-37.8, with every route
time identical. The first-command duration defect was real but latent in these
three assets. Candidate-result reuse was live in the extension passes: 0 stale
slots repaired against 372/200/396 on a paired defect build - so compare the
reroute and tap experiments against repaired controls before carrying their
conclusions forward. The tape and asset formats are versioned; retained assets,
worlds and approvals are untouched. Six standard floor attempts, three per
build, had no observed deaths or parity mismatches; one repaired attempt is
35/36 with a setup failure and must not be called a complete passing floor.
The seeded level-1 arm is 12/12. The follower's harm on the
line, measured correctly, is **114.2 units against open loop's 0.294**, not
the 78.3-against-37.8 the retained figures showed; it stays default off and
unqualified.

Before it: [where a route is worth taking, 2026-09-19](records/2026-09-19-route-selection.md).
The route wins at one of the three red outbound level-1 cases and loses by 6.8
and 17.1 seconds at the other two, because its start is behind those respawns
and the state at their nearest point on its line is 727-794 units/sec of
flight, which the current walking entry cannot deliver. Only the starts are
demonstrated entries for these tapes; this does not rule out all moving joins
or make more recordings a requirement for case coverage. The stand's historical
search median over 60 retained floors is 21,150 ms, not
the 19,150 ms best run this work had been quoting.

Before it: [the route prefix, 2026-09-19](records/2026-09-19-route-prefix.md).
The bot walks from the benchmark stand into a recorded route's start and plays
the tape unchanged: all three human routes reach the enemy flag from
`(152,-1160,1924.125)` in 14,875, 15,350 and 19,000 ms including the walk,
against the 19,150 ms search figure then quoted (subsequently corrected to a
best run; the retained-population median is 21,150 ms). The planner reaches the
last graph cell and stops there, so the prefix closes the final units itself
with a coasting law. Open loop after the hand-off; one map; the comparison
spans two physics settings.

Before it: [the follower, 2026-09-18](records/2026-09-18-route-follower.md).
The follower does not make a route survive a foreign start, and the
measurement taken on the way does the work: open-loop playback absorbs 28
units of along-track entry error and fails at 16 cross-track, which is what
the prefix above was built to deliver.

Before it: [route assets, 2026-09-18](records/2026-09-18-route-assets.md). A
recording is built into a tape, executed by a playback owner and approved by a
harness that requires the flag, no death and drift within 64 units. Three human
routes execute within 25 ms of their recorded times. Open loop, one map, from
each recording's own start.

Before it: [the exit-speed term,
2026-09-18](records/2026-09-18-exit-speed-term.md). The missing ranking term is
implemented and safe, and moves nothing: it can only choose among admitted
candidates, and the immediate tap is usually not admitted.

Before it: [where the stand time goes,
2026-09-18](records/2026-09-18-where-the-stand-time-goes.md). The stand runs
are 80.6 % flight; 107 of 227 landings break the chain, and the plan committed
at 81 of those breaks runs up at `Wishspeed` from a 415 units/sec landing. The
judge ranks by progress inside a horizon and has no exit-speed term.

Before it: [the tap continuation, measured correctly,
2026-09-18](records/2026-09-18-tap-onward-measurement.md). The tap pass had
been admitting landings on the evidence of the candidate's own hop; repaired,
it admits continuations at 476-500 units/sec and moves no stand median, and
its flight budget does not either. Diagnostics stay off; the stationary
sub-15 goal remains unmet.

Before it: [landing probes, 2026-09-17](records/2026-09-17-human-landing-probes.md).
Six rejected immediate candidates pass path-free physical/onward checks at
one high-speed root. Heading expansion does not fix it. Diagnostics stay off;
the stationary sub-15 goal remains unmet, with no positive parity coverage
in these probes. This result concerns selected-path admission, separately
from the earlier respawn pad-cost result.

The per-step record of each landed phase, moved out of [PLAN.md](PLAN.md)
so the plan stays the gates, a status per phase and the open list. The
text is the record as written when each step landed; the research
directories it names under `.codex-research` hold every world's files, and
the builds they name are the sources at that commit.

## Phase 0: scaffold

Landed 2026-09-15. `bot2_move.c` holds the state, the reset, the walk
executor and the parity harness; the executor's step is one function that
the live frame and the forecast both call, so the phase 1 parity gate is
already measured on the walk. Evidence in
`.codex-research/move-phase0-20260915-rewrite-b` (rewrite, parity horizon
16), `move-phase0-20260915-rewrite-c` (the same on a fresh build,
`move-build-phase0-20260915-b`, same sources and new binaries: the frozen
floor) and `move-phase0-20260915-baseline-a` (snapshot behaviour):

- All 12 level-1 cases green; 35 of 36 overall, in both campaigns. Stand
  starts take 30 s, respawn starts 22 to 26 s, at every level, since the
  walk spends nothing.
- Baseline, recorded once: the snapshot build passes all 36 under matched
  resources at 21 to 29 s per case. It never bought Speed in these solo
  worlds, so at every level it is the walk plus a few taps; the gap the
  graph must close is the strafe travel the catalog routes showed, not this.
- Parity: zero mismatches on every world, over roughly 700 to 1300 compared
  boundaries per world. Refusals are the flag-stand triggers at start and
  pickup, which the forecast declines by contract.
- One red case per campaign, always at the same legacy hand-off. The
  corridor refuses a step at x=1085, the retained legacy machine makes a
  running jump, and the walk resumes on landing with the jump's momentum.
  The landing varies with frame alignment: usually (743, 1385) or
  (831, 1175), from which the walk completes; about one run in four,
  (697, 1330) with 393 u/s pointing at an edge under 20 units away.
  `red_ret_L2_respawn5` drew that landing in rewrite-b and died;
  `blue_out_L2_respawn5`, which shares the start and the Force, drew it in
  rewrite-c and died on the same trajectory to the unit, while the other
  case passed. Three earlier repetitions of all six respawn5 return cases
  passed (`move-phase0-20260915-respawn5-rep1..3`).
- Probe (`tools/fixtures/movement/kejim/handoff_probe.json`, evidence
  `move-phase0-20260915-probe-handoff-a`): the walk started from that
  landing state dies deterministically at 393 u/s and completes at 250 u/s
  and from rest. The 25 ms frame rows show the walk's first command is
  already a full brake, the second frame is at 313 u/s, and the third is
  airborne; the stopping distance from 393 u/s is about 30 units and the
  edge is inside 20. No walk-frame rule reaches this case. The
  momentum-corridor brake proposed after rewrite-b would issue the command
  the walk already issues, so it is not case-earned and is withdrawn. The
  death is decided by the legacy jump's landing, which phase 1 replaces
  (plan on landings). Until then the floor is 35 of 36, with the red on
  whichever case sharing that start draws the far landing; the phase 1
  floor run drew it on the level-1 and level-2 twins together, so the
  lottery is the frame alignment of the corner start, not the Force.

## Phase 1: composition spike

Landed 2026-09-15. Discovery, execution and parity share the one step
function in `bot2_move.c`: on the first grounded frame the walk owns after
an objective is set, `bot_moveSpike` rolls out 36 two-hop candidates
(heading offset -15, 0, +15 degrees from the walk's waypoint heading and
run-up 32 or 128 units for the first tap; heading offset and run-up 0 or
64 for the second) from the live state, with the executor's own step as the
forecast's command source, and commits the fastest candidate that lands
twice and walks 128 units on with at least 64 units of progress toward the
objective. The parity horizon is then built from the same root over the
whole committed sequence. Declared starts:
`tools/fixtures/movement/kejim/spike_starts.json`, ten starts on the
blue-respawn-5 straight at x 2100 to 2900, from rest, at run speed, at
400 u/s, facing 90 to 200 degrees, moving 120 to 240 degrees. Evidence in
`.codex-research/move-phase1-20260915-spike-c` (build
`move-build-phase1-20260915-c`); `tools/movement_spike_report.py` reads the
per-candidate `[SPIKE]` lines.

- Composition: 10 of 10 starts obtain a sequence, execute it with no death,
  no miss and no spend, and complete useful onward travel 2.3 to 2.4 s after
  the start. Every sequence is a run-up to a tap at 250 u/s, a landing at
  308 to 379 u/s, a second tap on the landing frame, a landing at up to
  447 u/s, then the walk. On the straight all 36 candidates are admitted
  from every start.
- Parity: zero mismatches. Each completion event carries the horizon that
  covered it: built at the commit frame, still armed at completion, 91 to
  94 frames compared, the plan inside a single horizon of 129 to 256 frames.
  Discovery costs 62 to 76 ms and about 46,000 traces per plan for 36
  rollouts, charged to the commit frame and reported in the plan event.
- Spend: taps only; Force is untouched at every level, regen aside.
- The dimensions that decided the second hop, measured over the 360 lane
  candidates: none decided reachability, and the entry state decided
  nothing. Start speed 0 to 400 u/s and start heading error 0 to 70 degrees
  moved the first launch by at most three frames, because ground friction
  pins a level-1 tap to the 250 u/s run cap within the 32-unit run-up.
  What decided time and exit speed was the candidate itself: the second
  tap on the landing frame rather than 64 units later (411 versus 356 u/s
  at the second landing, 11 frames), the shorter first run-up (15 frames),
  and heading offsets to one side, which cost up to 100 u/s at the second
  landing when both hops used them. So the phase 2 entry envelope for a
  tap edge at level 1 is the takeoff position and a heading class; speed
  enters only for an edge taken on a landing frame, where the landing
  speed carries into the launch. The incidental plans discovered on the
  red-base platform before each corner start show what does decide
  reachability off a straight: the 128-unit run-up reached the flag
  trigger region and was refused, and the +15-degree first heading ran off
  the platform edge and fell, while second-hop dimensions separated
  little. Position of the takeoff relative to edges and triggers is the
  first envelope dimension; the graph's nodes carry it.
- The floor is unchanged on this build with discovery off
  (`move-phase1-20260915-floor-c`): 34 of 36 with zero parity mismatches, the two reds `blue_out_L1_respawn5` and `blue_out_L2_respawn5`; the reds are the phase 0
  legacy hand-off signature, the corridor refusal two units earlier and
  the same 393 u/s landing.

## Phase 1b: forecast extension

Landed 2026-09-15. `modelForce` on the forecast request applies the game
frame's Force update before every step after the root, as
`WP_ForcePowersUpdate` runs it: timed powers expire once their duration is
behind the clock, levitation ends on the ground with no jump start pending,
and while no power is active the pool regenerates one point per
`g_forceRegenTime` from the debounce clock, which an active power holds at
the current time. Levitation's drain is already inside Pmove. Speed and
levitation may be active; every other power still refuses, and the legacy
contract is unchanged when the flag is off. Parity now compares the pool,
the durations and the regeneration and drain clocks as well.

Two ordering facts had to be modelled exactly, and the harness found both:
a bot thinks at the top of the server frame from the previous Pmove's
state, and its retained command is consumed in the entity loop after that
frame's Force update. So the rollout issues the policy command from the
pre-update state, records the updated state as what the Pmove reads, and
runs the Pmove on it. With the update applied after the frame's snapshot
the before-state mismatched on the regeneration clock at the second frame;
with the command issued from the updated state it mismatched on the one
frame after Speed expired, when the live executor still asked for 425 u/s.

Gate, on `.codex-research/move-phase1b-20260915-spike-f` (build
`move-build-phase1b-20260915-f`), fourteen starts, the ten of phase 1 plus
four level-2 starts at 50 Force with Speed windows of 6000, 1500, 1200 and
800 ms, so one window outlasts the sequence, one expires in the second
flight and two expire in the first:

- Zero parity mismatches on all fourteen, each sequence inside one horizon
  built at its commit frame, 84 to 94 frames compared.
- With Speed active the same sequence launches at 425 u/s, lands at 628,
  taps on the landing frame and lands at 741, completing in 2.1 s against
  2.4 s at level 1. Across the expiry the executor drops to the 250 u/s walk
  on the next grounded frame and the pool starts regenerating; the forecast
  predicted both to the frame.
- Composition holds on all fourteen; all 36 candidates are admitted from
  every level-2 start as they were at level 1.
- The floor with discovery off on this build (`move-phase1b-20260915-floor-f`):
  36 of 36 with zero parity mismatches over the wider field set; the hand-off lottery was not drawn this run, so the known red stays as described under phase 0.

## Phase 2: graph nodes with walk, corner and jump edges

Node discovery landed 2026-09-15, the first step of the phase; gap edges,
the planner and straight hops followed the same day, and the gate is met on
hops, death and time (the straight-hop step below says what it does not
measure). `bot2_graph.c` runs at map load from
`BotAILoadMap`: a flood fill over 32-unit grid columns from every flag
stand, CTF spawn and deathmatch spawn, where each neighbour is admitted by
`Bot2_GroundSample`, the walk corridor's one sample (floor, step, sweep,
trigger_hurt) factored out of `ai_bot2_localnav.c` so the graph and the
walk cannot disagree about a floor. Cells are keyed by column and floor
height, so stacked floors are distinct; straight runs of linked cells along
each axis are the nodes, with height range, width margin and hazard flags.
The result is cached as text under `bot2-graph/` keyed by map checksum,
grid, gravity, speed and step, and `tools/movement_graph_report.py` reads it.
Evidence in `.codex-research/move-phase2-20260915-graph-a` (build
`move-build-phase2-20260915-a`):

- Kejim from 38 seeds, none refused: 14,009 cells in 12,035 columns, 1,974
  of them stacked; 1,414 nodes, 999 of them 256 units or longer; 26,604
  links; 2,748 boundary cells. Refusals are only `floor_missing` (1,848, the
  drop hazard) and `floor_startsolid` (980, a wall or a ledge above step
  height); no liquid, hurt or slope refusals on this map. 29,470 samples,
  under one 16 ms timer tick, so preparation runs synchronously at load.
- Coverage: all 21 fixture starts (the 36-case floor, the probe and the
  spike starts) stand on an admitted cell within 16 units of their column
  and .125 of their height.
- The floor on this build (`move-phase2-20260915-floor-a`, parity 256):
  35 of 36 with zero parity mismatches over 36 worlds. The red is
  `red_ret_L2_respawn5`, the phase 0 hand-off lottery drawn again: the walk
  resumed at (696.9, 1330.5) at 393 u/s and died on the same trajectory.

Gap edges landed 2026-09-15, the second step: the first jump the rewrite
owns, taken where the phase 0 red was decided. On grounded frames the walk
owns, `bot_moveEdges` rolls the walk itself out 64 frames ahead every
200 ms; a corridor refusal inside that window names a gap, the waypoint the
corridor was refused toward names the heading, and the lip is one corridor
sample per refused sample beyond the refusal point. Fifteen one-hop
candidates (headings -30 to +30 degrees, the tap 8, 40 or 72 units before
the lip) are rolled out from the live root with the executor's own step,
judged as the spike's candidates were plus one condition the graph now
supplies: each landing must be on an admitted cell. The fastest admitted
candidate is committed and executed by the same hop stage the spike uses,
so plans are now one or two hops and the stages are counted from the plan.
A search that admits nothing retries after 250 ms. Evidence in
`.codex-research/move-phase2-20260915-floor-c` and `-edges-b`, `-edges-rep2`,
`-edges-rep3` (build `move-build-phase2-20260915-c`); `--edges` on the runner.

- The x=1085 refusal on the blue-respawn-5 straight is seen 60 frames out
  from x=1460 (sample 3, lip 446 units ahead). 13 or 14 of 15 candidates
  are admitted; the committed one taps 374 units along the walk's heading,
  launches at x=1078 at 264 u/s, lands at (810, 1415) at 373 u/s on an
  admitted cell and completes onward travel 117 frames after the commit,
  inside one parity horizon with zero mismatches. All six cases sharing the
  start cross there identically; the legacy machine never jumps.
- Floor with edges on: 36 of 36, zero parity mismatches, six committed gap
  plans, none anywhere else. The six crossing cases are 0.5 s slower than
  the legacy jump's lucky landing (22.5 s against 22.0 s), because the hop
  lands 100 units short of it and the walk resumes at 250 u/s; the phase
  gate on time is still open, this step's gate was no death.
- Repetitions of the twelve respawn-5 cases: 36 of 36 over three
  runs, zero parity mismatches, the x=1085 crossing identical in all
  eighteen. One repetition also committed a hop on the approach to the blue
  stand in the three blue return cases: the lookahead saw a corridor
  refusal at (2377, 5361), sample 1, that the live walk in the other two
  repetitions never met, because the corner start's frame alignment put
  the walk on a different chord there. The hop landed on an admitted cell
  past the stand and cost 1.3 s (23.1 s against 21.8 s). The graph links
  the cells on both sides of that refusal, so it is a chord-specific
  refusal of the retained corridor, not a gap; choosing the walk edge the
  graph already has over a hop is the planner's decision, the next step.
- Cost: a lookahead is about 4 ms, a quarter of a millisecond of it Pmove
  and the rest the walk step's navmesh and corridor queries over 64 frames;
  at the 200 ms cadence a case spends about 200 ms looking. A search is 15
  rollouts, 13 to 28 ms and about 30,000 traces, charged to the commit
  frame.
- Discovery still runs at the Pmove boundary the parity harness owns, so it
  needs `bot_moveParity`; the benchmark always sets it. Moving the boundary
  hook out of the harness is part of the planner step.

A second refusal the lookahead finds, at (50, 109) before the off-mesh
connection near the red base, admits nothing: its candidates are refused by
the forecast (an item or trigger region) or miss, and the walk yields to the
off-mesh state as before. That is the next gap the edge templates must
cross, and the first one that needs the forecast to roll out past a trigger.

The planner landed 2026-09-15 as the third step and is the default
(`bot_movePlanner` unset or nonzero; the runner's `--no-planner` restores
the gap-edge walk). The graph carries edges: hops learned from committed
gap plans (takeoff and landing cells, the measured launch and landing
positions, heading and flight time) and pad, drop and basic-jump
candidates seeded from the navmesh's off-mesh connections as hints with
guessed times, validated and timed when executed. `Graph_Path` is A* by
time over links, diagonal steps across squares of four linked cells, and
enabled edges. The walk steers along the path's cells: a lookahead chord
that cuts corners as the navmesh waypoint did, judged by the corridor, and
when that chord is refused, nearer targets along linked cells (the corner
cell, the reached cell's successor, the reached cell), and last a permit.
The navmesh waypoint steers only where the graph has no path. Planning
runs on grounded frames when there is no path, the bot has left it or an
edge was learned; a replan is a discontinuity for the parity harness. When
the path's next hop, drop or jump edge is within 400 units, twelve
candidates around its recorded takeoff and heading are rolled out from the
live state and the fastest admitted one is committed, the commit-time
validation ARCHITECTURE requires; a search that admits nothing fails the
edge, two failures disable it and the path is searched again without it.
Pads are walked onto and timed by the retained jumppad state's flight plus
the walk from the actual landing to the edge's landing point. Learned
edges persist in the cache, and the runner's `--graph` seeds every world
with a prepared cache whose hash is recorded.

**The link rollout.** The graph's links and the walk's corridor are the
same trace admission from different origins, and they disagree beside
ledges and at ramp ends: the corridor's samples from the bot's off-grid
position refuse `floor_missing` on links the fill admitted grid to grid.
When the lookahead finds a corridor refusal and the graph links the cell
at the refusal to a distinct cell beyond it, the walk is rolled out from
the live state with a permit covering that link and the eight cells after
it, its step crossing them on the graph's word; the rollout is admitted
only if it walks past the permitted range without declining and without
dropping more than 48 units below the lowest cell it covers, and only an
admitted rollout grants the live walk the same permit. A refused rollout
falls through to the hop search. So a refusal on admitted floor is now
proven walkable by the game's own Pmove or crossed by a validated hop, and
never handed to the legacy machine.

What the planner forced into node discovery: the 32-unit grid refused every
ramp steeper than a step per chord (`floor_startsolid` and
`floor_missing`, never `ascent`), so the walk-only graph had no route from
any respawn to any flag; the fill retries such refusals as the walk's own
24-unit sample plus the remainder, which added 280 links on Kejim, all
rising or falling 19 units, and restored walk routes between every fixture
start and objective. Also learned on the way: the x=1085 site is a railing
the navmesh route cuts diagonally, while the graph's lane continues west
through it and turns south at x=1056; a 4-connected search with corner
stops walks Manhattan routes 27% longer than the navmesh's straight lines
(diagonal steps and corner cutting fixed that); and the chord classifier
must see a linked cell beyond the refusal, not the refusal's own cell.

Evidence, all on Kejim, 36 cases, `--edges --parity 256`, planner on unless
stated: `-floor-f` (path steering, build `-f`: 36 of 36, 2.3 s slower on
average, every case handing frames to the legacy machine at refusals);
`-floor-g` (corner steering, build `-g`: 30 of 36, six deaths where the
legacy walk fell into the pit); `-floor-i` (planner off, build `-i`: 36 of
36, within 75 ms of the gap-edge floor, six hops at x=1085); `-floor-j`
(link permits, build `-j`: 36 of 36, no legacy owner on any case, 1.6 s
slower); `-floor-k` (corner cutting restored over the permits, build `-k`:
36 of 36, 1.2 s slower, the stand routes 27% longer than the navmesh's);
`-floor-l` (diagonal search, build `-l`); `-floor-m` (the default build);
`-plan-e` (seeded with a learned hop, build `-f`). Zero parity mismatches
in every run.

- Floor-l, the planner as it now stands: 36 of 36, zero parity mismatches,
  no legacy owner and no jump pad on any case, 24 gap hops, and 277 ms
  faster on average than the gap-edge walk (28 of 36 cases faster, the
  respawn-4 starts by 0.7 s, the worst case 375 ms slower). The rewrite
  owns every frame of the floor for the first time.
- Floor-m, the default build with the runner's default invocation: 36 of
  36, zero parity mismatches, no legacy owner and no jump pad on any case,
  24 gap hops, 271 ms faster on average than the gap-edge walk (29 of 36
  cases faster, worst case 325 ms slower). This is the benchmark
  configuration from here on; `--no-planner` reproduces floor-i.
- The planned hop: seeded with the learned edge, the first path uses it,
  twelve candidates are rolled out 392 units before its takeoff, all
  admitted, and the committed one launches at x=1257 at 264 u/s and lands
  at (990, 1435) at 382 u/s inside one parity horizon; 0.4 s cheaper than
  discovering the same hop.
- Path search on 14,249 cells costs under a millisecond; the report tool
  runs the same search offline (`--path x y z x y z`, `--no-edges`), and
  `--ground-trace` on the runner prints every corridor refusal.

Straight hops landed 2026-09-15, the fourth step and the strafe hops the
phase gate asks for: momentum hops on continuous floor, the main source of
speed on a straight. On a grounded frame the walk owns, at a landing at
once and otherwise on the lookahead cadence, when the planned path ahead is
straight (its cells within 24 units of the chord from the progress cell,
over at most 24 cells, no edge before it) and its far cell at least 192
units away, and the walk's own lookahead meets no refusal, six one-hop
candidates (headings -6, 0 and +6 degrees around the chord, the tap after
0 or 24 units) are rolled out from the live root with the executor's step
and judged as gap candidates are, plus three conditions: the landing must
be within reach of a path cell; the rollout must be ahead of the walk's
lookahead by 16 units at the frame the sequence was done; and the rollout
runs on 48 frames past the landing, in which the walk from it may neither
decline nor fall. The fastest admitted candidate is committed, and a plan
whose hops have landed is completed by the commit, so a chain of hops is a
sequence of one-hop plans, each validated from its own landing. Straight
hops are not learned as edges. Off with `bot_moveStraight 0`
(`--no-straight` on the runner). Evidence in
`.codex-research/move-phase2-20260915-straight-a` (build
`move-build-phase2-20260915-n`, without the hold) and `-straight-b`,
`-straight-control`, `-straight-rep2` (build `move-build-phase2-20260915-o`):

- Straight-a, without the hold: 27 of 36, nine deaths with one signature,
  the phase 0 hand-off death made by the rewrite itself. A hop landed at
  390 u/s beside the ledge at (1835, 2758) or its blue mirror, the onward
  walk carried that momentum over the ledge within a second, and the
  corridor admitted every chord toward the target while the velocity
  pointed off the floor. The rollout had admitted the hop because it
  stopped judging at the 128-unit onward mark, before the fall showed; the
  one admitted candidate landed on a cell flagged beside a drop, and its
  siblings were declined by the corridor right after landing.
- Straight-b, with the hold: 36 of 36, zero parity mismatches, no legacy
  owner on any case, 736 straight plans (14 to 28 per case) beside the 24
  gap hops. Every case is faster than the planner floor and faster than
  the snapshot baseline: 5.0 s faster than floor-m on average (3.8 to
  6.7 s) and 3.6 s faster than the baseline (2.0 to 5.6 s). The stand
  cases take 24.0 s against 30.0 and 28.9, respawn-5 18.2 s against 22.1
  and 21.1, respawn-4 20.3 s against 25.3 and 23.3. The gate on time is met
  at level 1; the level 2 cases ran at the level 1 wishspeed, since Speed
  is a phase 3 edge, so the two levels sharing one law is not yet shown
  with different edges. The gate's corner clause has no dedicated
  measurement: the walk brakes only at the objective and at an
  uncommitted edge's takeoff, never at a corner.
- Straight-control, `--no-straight` on the same build: 36 of 36, within
  200 ms of floor-m on every case, 21 identical, the same 24 gap hops, zero
  mismatches. The planner step is unchanged when straight hops are off.
- Straight-rep2: 36 of 36, zero mismatches, 44 ms slower than straight-b
  on average, 15 cases identical, the worst 925 ms slower and the best
  1.25 s faster: the chain repeats within a hop.
- What the chain measures: committed hops land at 375 u/s on average (up
  to 516) but launch at 267 (199 to 331), because the one grounded frame
  between hops pays ground friction and the kernel, asked for the walk's
  250, brakes toward it; 109 of 757 live launches were at 300 u/s or more.
  A search costs 8.6 ms on average and 25.7 ms at most, six rollouts of up
  to a hundred frames on one landing frame. Of the candidates, 3,082 were
  admitted, 669 refused as off the path, 593 as slower than the walk, 286
  declined by the corridor after landing and 229 fell: the last two are the
  refusals the hold exists for.

Chains, the same day: the straight search also proposes two-hop candidates
along the chord, the second hop tapped on the first landing's frame with no
run-up. PM_WalkMove checks the jump before friction, so a jump accepted on
the landing frame pays no ground friction and the kernel is never asked to
brake; the one-hop candidates remain for straights where two do not fit.
Every landing of a candidate must be beside the path, and the admitted
candidate furthest ahead of the walk at its done frame is committed, in
place of the fewest frames, which would always prefer one hop. Evidence in
`.codex-research/move-phase2-20260915-chain-a` and `-chain-rep2` (build
`move-build-phase2-20260915-p`):

- Chain-a: 36 of 36, zero parity mismatches, no legacy owner, 301 two-hop
  and 164 one-hop plans (10 to 16 per case). Every case faster than
  straight-b, by 1.8 s on average (0.3 to 4.2 s), and 5.4 s faster than
  the snapshot baseline (3.3 to 8.7 s): the stand cases 21.2 s against
  28.9, respawn-5 16.5 s against 21.1, respawn-4 19.3 s against 23.3.
- The second hop launches at 363 u/s on average live (249 of 293 at 300 or
  more) where the chained one-hop plans launched at 267; landings average
  387 u/s and reach 598. The first hop of each plan still launches at 261,
  after the one walk frame between plans.
- The search now rolls out twelve candidates and costs 21 ms on average
  and 56 ms at most on the landing frame, twice the one-hop search.
- Chain-rep2: 36 of 36, zero mismatches, 19 ms faster than chain-a on
  average, ten cases identical, single cases up to 1.7 s either way: the
  chain's frame alignment decides which hops fit, and the spread is now a
  hop or two per case.

Two defects in the chain step, found in review and fixed the same day. The
judge compared a candidate with the gap search's 64-frame walk lookahead,
so a two-hop candidate, done at frame 80 to 110, was credited against a
walk frozen at frame 63: 294 of chain-a's 301 two-hop commits were ranked
that way, and the ranking, not the route times, was void. The straight
search now rolls the walk out to the candidates' own 256-frame horizon and
compares at the frame before done; a walk that stopped earlier is measured
where it stopped and the commit says so (`walk_stopped`), and a walk whose
horizon ran out first refuses the candidate as `unjudged`. Every commit
records the frame it was judged at, the runner audits it
(`straight.judged_short` in `result.json`, `straight_audit` in
`movement_bench_lib.py`, covered by a unit test), so a commit judged
against a frozen walk cannot pass unnoticed again. The second defect was
telemetry: the rollout's launch was detected on frames whose starting
stage was a run-up, so a second hop tapped on the landing frame, whose
step turns the landing into the next run-up, was never seen and every
two-hop commit reported a second launch of zero, with the second landing's
drop measured from the root. The launch is now the frame whose step
recorded the tap request, and the hop is the run-up that step left.
Evidence in `.codex-research/move-phase2-20260915-chain-judged-a` (build
`move-build-phase2-20260915-q`):

- 36 of 36, zero parity mismatches, no legacy owner; 340 straight commits,
  269 two-hop and 113 one-hop plans, none judged short, none unjudged,
  eleven judged against a walk that had stopped. 234 ms slower than
  chain-a on average (13 cases faster, single cases 1.3 s faster to 1.85 s
  slower, inside the repetition spread) and 5.2 s under the snapshot
  baseline: the stand cases 21.9 s, respawn-5 16.7 s, respawn-4 19.2 s.
- The rollout's second launch now reads 376 u/s on average (252 to 431)
  against 377 u/s in the live launch events, so the measurement and the
  execution agree. Search cost 22.6 ms on average and 32.7 ms at most.

The walk frame between plans, the same day. A straight candidate now
issues the boundary frame's command itself, from the continuation the
frame started with, instead of following the walk command the live frame
had submitted, so a tap on the landing frame is what the rollout
validates; a commit then re-issues the live frame from the committed plan,
writing the movement fields into the Pmove's command and the retained one
before Pmove runs. The first hop of a plan committed at a landing is
therefore tapped on the landing frame, accepted before friction, as the
second hop of a chain already was. Evidence in
`.codex-research/move-phase2-20260915-landing-a` and `-landing-rep2`
(build `move-build-phase2-20260915-r`):

- Both runs 36 of 36, zero parity mismatches, no legacy owner, no commit
  judged short. 407 commits in landing-a, 208 of them at a landing and
  135 of those re-issued as a tap on the landing frame; the other landing
  commits chose the 24-unit run-up. No miss on any re-issued frame: the
  re-issued command is the rollout's own first command.
- The first launch of a plan committed at a landing reads 323 u/s in the
  rollout (207 to 547) where it was 261; live first launches average 285
  and landings 409. The second hop is unchanged at 381.
- Route time is neutral on the whole fixture: 117 ms faster than
  chain-judged-a in landing-a and 19 ms slower in rep2, 5.3 s under the
  snapshot baseline. The families move in opposite directions in both
  runs: respawn-5 is 1.1 s faster (16.7 s to 15.6 s), the stand cases 0.3
  to 0.6 s slower (21.9 s to 22.2 and 22.5 s), respawn-4 0.4 s slower.
  The candidate outcomes at landings say why: a run-up-0 hop from a
  409 u/s landing lands off the path 492 times for 309 admissions, and
  fell 141 times, where the run-up-24 hop is admitted 590 times; the
  faster hop overshoots the stand routes' corners, so those routes get
  shorter chains and more one-hop plans (150 against 113). Search cost
  18.6 ms on average and 38 ms at most.

Aimed hops, 2026-09-16. Where a tap lands was measured from the 587 live
hops of landing-a: the horizontal length is about .695 times the launch
speed plus 76 units, and a landing is about 100 u/s faster than its
launch. Each candidate hop is now aimed at the point on the path one
predicted hop-length ahead (at .85, 1.0 and 1.15 of the prediction, in
place of the three headings around the chord), measured from where the
bot stands, the second hop from the first's aim point; the aim follows
the path around a bend, and the rollout still decides whether the flight
is clear. The straight gate on when to search is unchanged. Evidence in
`.codex-research/move-phase2-20260916-aim-a` and `-aim-rep2` (build
`move-build-phase2-20260916-s`):

- Both runs 36 of 36, zero parity mismatches, no legacy owner, no commit
  judged short, no miss. The fastest arms so far: 340 and 458 ms under
  chain-judged-a on average (22 and 21 cases faster), 5.5 and 5.6 s under
  the snapshot baseline. Respawn-4 recovered its loss (19.0 and 18.8 s
  against 19.2) and respawn-5 gained again (15.5 and 15.3 s against
  16.7); the stand cases stay 0.35 s slower than chain-judged-a (22.2 s
  in both runs against 21.9), so that part of the fast-landing regression
  remains.
- 378 commits in aim-a, 246 two-hop and 132 one-hop, 10.5 per case; 209
  at landings, 141 re-issued as landing-frame taps. The 24-unit run-up is
  chosen 237 times to 141 for the immediate tap: a hop from a 416 u/s
  landing is still refused more often than one from the walk's speed
  (1,093 candidates off the path of 5,580, 921 refused by the forecast).
  Live first launches average 293, second launches 393, landings 416 and
  reach 593. Search cost 19.1 ms on average and 50.6 ms at most.

Search pruning, the same day. Profiled on aim-a: a search rolled out
1,159 candidate frames and a 256-frame walk, two-hop candidates that
landed off the path ran to their done frame and hold before the landing
was judged (17 percent of the frames), and the walk ran to its horizon
whatever the candidates needed (18 percent). Two changes, with every
committed plan still validated in full: a candidate's rollout ends at the
frame a landing is seen off the path, since nothing after it can admit
the candidate; and the candidates are rolled out first, so the walk runs
only to the furthest admitted candidate's done frame, and not at all when
none is admitted. The restructuring also dropped the walk-ahead check
that had refused a landing search when the walk would be refused inside
the gap lookahead: at a landing the search now runs and the rollouts
decide, and the check remains only on cadence frames where the gap search
ran the lookahead this boundary. A telemetry fix went with it: the
per-candidate `[SPIKE]` line now prints the candidate's own frame count
and refusal flags, which had read the last rollout's since the judge was
split in two. The forecast refusal that dominates the refused candidates
is `BOT2_FORECAST_TRIGGER`, a flight crossing a trigger volume, which the
forecast declines by contract. Evidence in
`.codex-research/move-phase2-20260916-prune-a`, `-prune-rep2` (build
`move-build-phase2-20260916-t`, before the telemetry fix) and `-prune-b`
(build `move-build-phase2-20260916-u`, the committed sources):

- All three runs 36 of 36, zero parity mismatches, no legacy owner, no
  commit judged short, no miss. Search cost 14.0 ms on average in prune-b
  (from 18.5), the 90th percentile 19.1 ms, the worst 48.9 ms: 884
  candidate frames a search (from 1,159) and an 82-frame walk (from 237).
  The worst searches are the ones that admit all twelve candidates, each
  run to its hold; the cost left is the validation itself.
- Route time improved again, from the dropped landing check rather than
  the pruning: 698 searches and 461 plans a run against 465 and 378, and
  the chains continue where the walk ahead would have been refused. The
  three runs are 1.22, 1.37 and 1.22 s under chain-judged-a on average
  (33 to 35 cases faster) and 6.4 to 6.5 s under the snapshot baseline;
  the stand cases 20.8 to 21.2 s, below chain-judged-a's 21.9 for the
  first time, respawn-5 14.9 to 15.3 s, respawn-4 17.8 to 18.0 s.

Arrivals, the same day. The refused candidates were read: 2,178 in a run,
the forecast's `BOT2_FORECAST_TRIGGER`, clustered at the two flag stands
and their approaches. The forecast counts any entity with trigger
contents, and an item's pickup box (50 units along x, 36 along y and z)
among them, so every flight that ends at the flag, and every candidate
rolled out from a start standing on the stand, was refused at the touch.
The mid-map refusals are brush triggers with no origin, the hurt volumes
of the pits and the pads' push volumes, crossed in flight and refused
rightly. The fix reads the refusal instead of avoiding it: a candidate
the forecast refused at a trigger whose last frame is within 64 units of
the objective arrives, done at that frame with the progress it made and no
hold, since the touch is the pickup or the capture the case is judged on;
a walk refused at the same trigger is measured where it stopped, as a
walk that declined is. The search radius around the objective went from
256 to 128 units so the last hop is searched. Evidence in
`.codex-research/move-phase2-20260916-arrive-a` and `-arrive-rep2` (build
`move-build-phase2-20260916-v`):

- Both runs 36 of 36, zero parity mismatches, no legacy owner, no commit
  judged short, no miss. 45 arriving commits in arrive-a, 220 arriving
  candidates; 523 plans, 354 two-hop.
- 470 and 558 ms under prune-b on average (27 and 26 cases faster), 6.8
  and 6.9 s under the snapshot baseline, every case faster than it by 4.6
  to 9.3 s: the stand cases 20.5 s, respawn-5 14.4 to 14.7 s, respawn-4
  17.5 s. Search cost 15.3 ms on average, 21.6 ms at the 90th percentile,
  52 ms at most, with 1,445 candidates still refused a run, most of them
  from a start standing in the home flag's box, which the objective test
  does not cover.

## Phase 3: resource edges

### The Speed edge, 2026-09-16

The first resource edge. The case's spend policy reaches the game as
`bot_moveSpend` (bit 1 Speed, bit 2 held jumps), which the benchmark sets
from the case's templates, so a level-1 case starting with Force buys
nothing. On a grounded frame the walk owns, when the policy permits Speed,
the power is not active, the pool covers the power's cost (50 at every
level) and the path left from the progress cell at the walk's speed is at
least a second, the plan puts the Speed command on the frame's user
command and records the request; the executor never issues it. The game
processes that command after the frame's Pmove (`ClientThink_real`, after
`Bot2_MoveBenchPmove`), so the next frame confirms the purchase by the
active bit and reports the window, or reports a refusal and retries a
second later. The forecast models the same command: after a frame's Pmove,
a Force Speed command applies the think's debounce, `ForceSpeed`'s
conditions and `WP_ForcePowerStart`'s effects for `FP_SPEED` (the active
bit, the window by level, the debounce clock and the drain) to the
rollout's client, so the frames after a purchase are compared. The parity
command comparison now includes the generic command. Discovery is skipped
on the purchase frame, since a fresh candidate issues its own first
command without the purchase and would be judged at the wrong wishspeed.
Nothing else changed for Speed: the walk's wishspeed, the hop aim model's
launch speed and every rollout read the live player state, as the design
required.

The snapshot baseline never bought Speed on these cases (zero Speed-active
samples in every level 2 and 3 world of baseline-a), so the level 2 and 3
baselines are walk-speed times, and the phase 2 arm ran them at the
level-1 wishspeed. The comparison arm throughout is arrive-a.

Six pairs of runs were needed. Each defect below was found in one run,
classified from its world's log and play-log rows, fixed by one rule, and
the fixture rerun twice.

- **A purchase inside a horizon** (speed-a, build
  `move-build-phase3-20260916-a`). 36 of 36, level 2 and 3 cases 5.7 and
  5.9 s under arrive-a, and three parity mismatches of reason `command`:
  the second purchase of the three level 3 cases whose window expired
  before the flag, made inside an armed horizon whose step function never
  issues a purchase. A plan decision the step function does not make is a
  declared discontinuity, as a replan is: the request resets the horizon,
  and the one rebuilt at that boundary carries the purchase.
- **A stale permit** (speed-b, build `-b`). 35 of 36 and zero mismatches:
  `red_out_L3_respawn5` stalled at the bound, standing 110 s at
  (1566, 2721, 1368) pushing north into a wall the corridor had refused,
  under a permit granted 700 units earlier by a walk rollout at 425 u/s. A
  straight hop committed after the grant changed the state the walk
  reached the links with (a 642 u/s landing, then a stop two units west of
  the opening the rollout had slid into), and the forecast agreed with the
  stall frame for frame. A permit is a rollout from the state it was
  granted in; any plan decision after the grant, a committed sequence or a
  Speed purchase, now revokes it, so the corridor's refusal returns and
  the gap search rolls the links out again from the real state.
- **A fall beside a ramp** (speed-c, build `-c`). 35 of 36 and zero
  mismatches: `red_ret_L3_respawn5` died at 12.9 s. From a hop landing at
  425 u/s beside the ramp up to the red stand, the walk's command chord to
  the path's target on the ramp was clear while its momentum carried it
  along its velocity off the ramp's side, airborne within 200 ms and into
  the pit's hurt volume. No lookahead ran there: the gap lookahead was
  ineligible during the onward walk after a landing, where both this fall
  and the corner stall began. Two rules: the walk checks a momentum chord
  along its velocity for the frames a ground turn takes (100 ms, above
  stop speed) and brakes the frame when that chord is refused for a drop,
  missing or liquid floor, steep floor or a hurt volume, never for a
  solid; and the gap lookahead runs on the onward walk. The same run
  showed one legacy-owned sample in five cases at the corridor refusals
  the revoked permits had hidden; the onward lookahead removed them
  (speed-d and every run since: owners `move` only). The revoke and the
  lookahead re-grant the corner's permit from the landing state, which is
  where the stall case now passes.
- **An arrival that missed** (speed-d rep2, speed-e, speed-e rep2, builds
  `-d` and `-e`). All 36 of 36 with zero mismatches, but `red_out_L3_respawn5`
  and `blue_ret_L2_respawn5` took 18.5 s, the latter in both e runs to the
  millisecond: three plans judged `arrives`, each flying past the flag and
  on for 800 units. The forecast refuses at its own item box (50 by 36 by
  36 around the trajectory point), which is conservative for a refusal and
  not for an arrival: the first hop landed 68 units short of the flag,
  tapped on the landing frame and carried the hull over the flag's box.
  Arrival is now the game's own test, `trap->EntityContact` of the
  player's hull with the objective's entity at the rollout's last frame or
  one of the next four frames of its flight, and a plan arriving in its
  first hop commits no second. The brake event added in build `-e`
  (`event=walk reason=brake why=<refusal>`) counts the momentum rule's
  firings.

Evidence, the final pair `.codex-research/move-phase3-20260916-speed-f` and
`-speed-f-rep2` (build `move-build-phase3-20260916-f`, the committed
sources), read with `movement_campaign_compare.py` against arrive-a and the
snapshot baseline:

- Both runs 36 of 36, zero parity mismatches, no legacy owner, no commit
  judged short, no miss. 25 Speed purchases a run: one in each of the 24
  level 2 and 3 cases, and one second purchase at level 3 after the window
  expired. Every purchase confirmed on the frame after its request, the
  pool 50 lower and the window 19,975 ms.
- Route time by level, mean of the twelve cases (ms):

  | level | speed-f | speed-f rep2 | arrive-a | baseline |
  |---|---|---|---|---|
  | 1 | 17,748 | 17,781 | 17,733 | 24,390 |
  | 2 | 11,965 | 11,906 | 17,419 | 24,379 |
  | 3 | 11,929 | 11,950 | 17,521 | 24,427 |

  Level 2 is 5.45 and 5.51 s under arrive-a with all twelve cases faster
  in both runs (3.6 to 7.5 s each); level 3 is 5.59 and 5.57 s under with
  all twelve faster (3.1 to 8.2 s). Level 1 is 15 and 48 ms over, seven
  and six of twelve faster; the slowest level-1 cases against arrive-a are
  the red return respawn-4 case at 1.0 and 1.8 s over, where an onward
  permit blocks straight hops, and single cases at 1.5 to 1.8 s over in
  one run each. Every case in both runs is faster than the snapshot
  baseline, by 4.9 to 16.0 s; the whole fixture is 10.5 s under it.
- Families at level 2 and 3: the stand cases 13.5 to 13.8 s (20.1 to 20.5
  in arrive-a), respawn-5 10.4 to 10.5 s (14.5 to 14.7), respawn-4 11.6 to
  11.8 s (17.5 to 17.6).
- Live speeds across the run: first launches 375 u/s, second launches
  481 to 493, landings 527 to 532 with a maximum of 847, against 294, 376
  and 401 at level 1 in arrive-a. The momentum brake fired 5 and 18 times
  a run after the start (2 to 10 at level 1), every one for `floor_missing`;
  permits were revoked 18 and 24 times, all by commits. Arrivals 34 and 33
  a run (43 and 41 under the generous judge). Search cost 15.5 and 14.3 ms
  on average, 25.1 and 20.0 at the 90th percentile, 52.8 and 51.1 at most:
  the rollouts are longer at 425 u/s and the cost is within phase 2's.

### The state-space planner at level 1, 2026-09-16

Step 2 of phase 3, proven at level 1. Three parts landed together, each
one rule the fixture then tested.

**Strafe edges with state.** A completed straight plan is learned as one
`GRAPH_EDGE_STRAFE` edge per hop that landed: the launch cell to the
landing cell, the flight frames as the cost, the launch speed and velocity
heading as the entry envelope, the landing speed and heading as the exit
state, and whether the tap was on a landing frame (the second hop always;
the first when the plan was re-issued on the landing it was committed at).
The same maneuver seen again (same cells, same tap, entry speed within the
48 u/s band) folds into one edge and keeps the faster measurement. The
cache is version 4 and holds up to 8,192 edges; `tools/movement_graph_merge.py`
unions the caches of a campaign's worlds into one seed a later campaign is
run with (`movement_bench.py --graph`), which is how learned edges cross
worlds: a world learns on its own leg and never revisits it.

**The state search.** `Graph_PlanState` is A* over the cell and a state
key: speed in 32 u/s bands, a heading octant kept only on a landing
frame, the landing-frame bit and whether the Speed window is open. Walk
links are priced by the walk's own law from the state's speed (friction
and the run's acceleration per 25 ms frame over the link, PHYSICS.md), so
a fast landing is worth its next few cells; a landing-frame edge is
admitted only from a landing state within the speed band and a 30-degree
cone of the launch it was learned from, from a cell within 48 units of its
takeoff; a run-up edge admits any state there; the other edge kinds leave
the walk's state. The entries live in a 131,072-slot table in BSS and the
search reports overflow so `Replan` can fall back to the cell search. It
never did: 2,900 entries expanded on average, 4,400 at most, 1.3 to 1.7 ms
a search.

**Planned hops as candidates.** When the bot stands within 64 units of
the takeoff of the path's next strafe edge, the straight search adds the
planned chain's next hop to its own twelve candidates: aimed at the edge's
learned landing from where the bot stands, with the tap at once or after
the 24-unit run-up, and chained with the path's next landing-frame edge
aimed at its landing when there is one. One judge ranks all of them by
margin over the walk, so a planned hop is taken when it beats what the
search finds from the live state and never blocks it; a planned edge none
of whose candidates is admitted is refused, avoided for the objective and
the path searched again at once. A strafe chord is a flight, not a walk:
the walk's lookahead stops at a chord's takeoff cell, only progress that
reached the takeoff is projected along it, the search runs on the first
frame inside the takeoff window, a bot 64 units past the takeoff with no
sequence in progress refuses the edge, and the gap search leaves a
lookahead refusal along a planned chord to that validation.

Six builds were needed (`move-build-phase3-20260916-g` to `-l`); each
defect below was found in one run, classified from its world's log and
play-log rows, fixed by one rule, and the arms rerun.

- **A fall measured from the root** (learn-a, build `-g`). 35 of 36 and
  zero mismatches: `red_out_L3_respawn5` died walking off the side of the
  ramp to the blue stand inside a straight plan's 48-frame hold. The judge
  measured the drop from the rollout's root, 160 units below the landing
  on the ramp, so the fall read 226 units, under the 256 bound. The drop is
  now measured from the highest floor the rollout has stood on (root or
  any grounded frame since), `RolloutFell`. Not a defect of the state
  search, which planned nothing in that world; the chain that exposed it
  is the repetition spread.
- **A separate validation search that replanned on every failure**
  (state-a, build `-h`, level 1 seeded with the learn-b edges). 12 of 12
  and zero mismatches, but 2.9 s slower than the Speed edge arm with 185
  failed validations in 295 and seven paths a world: the planned edge was
  validated by its own search, on the approach, with candidates along its
  recorded heading rather than at its landing, and every failure avoided
  the edge and replanned. Worse, the walk's target along a chord was
  measured from the takeoff cell, so a bot along the chord walked back to
  it (150 units backwards in `red_out_L1_stand`). The planned hop became
  candidates of the one straight search, validated at the takeoff only,
  aimed at the learned landing, and the target is measured from the bot's
  projection onto the chord.
- **A chord walked to its lip** (state-c, build `-i`). 12 of 12 and zero
  mismatches, the respawn families neutral, the stand cases 2.7 s slower
  with 22 gap plans in twelve worlds: the learned hops cut the corners of
  the pits, and walking a chord runs to the pit's edge, where the gap
  search made a 262-unit-run-up hop, and once dropped the bot 430 units
  onto the floor below (it lived). A chord is a flight: refused at its
  takeoff if not flown, replanned at once, and never a gap.
- **A chord joined from the side** (state-d, build `-j`). 11 of 12:
  `red_out_L1_respawn5` stalled 170 s at (1646, 2845). Progress had
  jumped to a chord's takeoff index for a bot beside the chord but 80
  units north of its takeoff, the projection put the target ahead of any
  validation, the "walked" rule refused the edge, and the replanned walk
  came to rest at its own target with no decline and no event. Only
  progress that had reached the takeoff is now projected along a chord,
  and the walk names a hold (`event=walk reason=hold`) when it stands at
  its own target away from the objective.
- **A refusal nobody searched** (state-e, build `-k`). 12 of 12, 300 ms
  under the Speed edge arm, but one legacy-owned second in
  `red_out_L1_respawn5`: the gap lookahead saw the railing at x=1566
  along a planned chord and stood down, and the straight search deferred
  to the gap search on a lookahead refusal and returned before building
  the planned candidates, so the walk ran into the railing at 250 u/s and
  the corridor handed the frame to the legacy machine. The straight
  search now runs on a chord refusal and validates the planned edge before
  its short-straight return, and a chord beyond the next cell ends the
  walk's lookahead at its takeoff cell.

Evidence, the final build `move-build-phase3-20260916-l` (the committed
sources): `.codex-research/move-phase3-20260916-state-f` and
`-state-f-rep2` (level 1, seeded with `move-phase3-20260916-state-seed-L1-b.txt`,
the 223 strafe edges of the twelve level-1 worlds of `-state-learn-b`),
and `-state-learn-f` (the whole fixture, unseeded), read with
`movement_campaign_compare.py` against speed-f and the snapshot baseline:

- Both seeded runs 12 of 12, zero parity mismatches, no legacy owner, no
  hold, Force never spent and Speed never active (the level-1 spend
  check). 28 and 27 state plans a run, 21 and 20 of them two-hop chains,
  26 and 25 committed from inside the edge's envelope; 56 and 51 planned
  edges refused at their takeoff, 2 and 1 refused as walked past. 75 and
  67 paths a run, all by the state search, 1,114 and 1,053 strafe edges
  planned in them.
- Route time, mean of the twelve level-1 cases (ms):

  | arm | stand | respawn5 | respawn4 | against speed-f |
  |---|---|---|---|---|
  | state-f | 20,112 | 14,838 | 17,775 | -173 (6 of 12 faster, 1 identical) |
  | state-f rep2 | 19,719 | 14,131 | 17,438 | -652 (9 of 12 faster) |
  | speed-f | 20,381 | 15,356 | 17,506 | |

  The gate's clause is met: through the state-space plan, level 1 is no
  slower than the greedy chains, and 6.8 and 7.3 s under the snapshot
  baseline. The gain is small and inside a hop of the repetition spread;
  the state plans launch at 279 u/s and land at 378 on average, the
  greedy plans beside them at 290 and 402.
- The whole fixture unseeded on the same build (learn-f): 36 of 36, zero
  mismatches, no legacy owner, 90 ms under speed-f (22 of 36 faster),
  10.6 s under the baseline; 498 strafe edges learned. The learning arms
  on builds h to k (learn-b to learn-e) were all 36 of 36 with zero
  mismatches and 46 to 195 ms under speed-f, so the judge's fall rule and
  the chord rules cost the unseeded runner nothing.
- Cost: a straight search with the planned candidates 16.4 and 17.4 ms
  on average, 26 to 29 at the 90th percentile, 47 and 79 at most; the
  state search 1.3 to 1.7 ms. Each learned edge writes the cache
  (about 1 MB), 12 to 20 writes a world.

What this step does not show, and where it goes next: the state plans are
28 of about 145 commits a run, because a learned edge is admitted from
the cell it was learned at and the live landing that reaches its takeoff
rarely repeats the state it was learned in (the envelope held on 26 of 28
commits, but 56 planned edges were refused at their takeoff). The chain is
chosen as a whole by the path, and the greedy search still picks most of
the hops; a second round of seeding (the state arm's own learned edges
merged into the seed) is unmeasured. Level 2, with the Speed edge inside
the search so the planner knows what Speed buys, is the next gate.

### Exploration review and no-Force timing, 2026-09-16

The subsequent exploration branch's review, source fixes, fresh floor and
seeded runs, retained failures, and A*/zero-heuristic comparison are recorded
in [the search review](records/2026-09-16-search-review.md). The corrected
unseeded floor is 36/36 with zero parity mismatches and rewrite ownership;
the experimental seed's 12/12 safety result does not satisfy its ownership
or performance gate. Stationary no-Force outbound runs at 19.150 and
19.925 seconds do not meet the owner's confirmed sub-15-second human
target, and exploration completion is not fastest-time convergence.

### Paired policy replay reviewed, 2026-09-17

The [original replay record](records/2026-09-17-replay.md) retains the
campaigns and first analysis. Its blanket heading rejection, single-cause
conclusion and analyzer-v1 flight/ground tables are superseded by
[the correction record](records/2026-09-17-replay-correction.md). Native
launch-stage confirmation occurs after the jump command, so the old exact
3,013/2,800 ms ground/flight gap decomposition is not current evidence.

Analyzer-v2 retained output is
`.codex-research/move-replay-correction-20260917T061951996008Z`. With explicit
25 ms launch-confirmation correction, large red has 16,225 ms flight and
2,925 ms ground over 19,150 ms; blue has 16,525 ms flight and 3,400 ms ground
over 19,925 ms. Red's arithmetic difference from modeled walk/flight is
+2,513/+3,300 ms, not a causal decomposition.

Across retained paired roots, large-seed blue has two B-only admissions and
two censored B results, large-seed red has zero B-only and one censored B,
and small-seed red has one B-only. Censored reference comparisons are not
policy rejections. The root log contains selected diagnostics, not a full
persisted player state. Entry/exit deviations demonstrate composition error
but do not identify a unique cause or prove an end-to-end heading benefit.

The recorded default safety campaigns remain 36/36 unseeded and 12/12 seeded,
with no deaths, forbidden spend or parity mismatches. Ownership still fails
on `red_ret_L1_stand`; the seeded arm remains 1,719 ms slower than the retained
greedy reference. No sub-15-second no-Force pickup has been demonstrated.
The default-off chain-admission and landing-replan trials did not establish
a default improvement. Keep their outcomes and repetition spread separate
from causal explanation.

[HANDOFF.md](HANDOFF.md) orders inspection of every rescue, independent
same-root B reference judgement, matched successor trials, and prospective
capture of merge entrants/losers before a controlled merge/no-merge trial.
The seed retains winners only; folded-away exits are unavailable offline.

### Rescue audit, arm-B reference and the heading offer, 2026-09-17

Records: [rescue audit](records/2026-09-17-rescue-audit.md),
[arm-B reference](records/2026-09-17-arm-b-reference.md). Builds
`.codex-research/move-build-replay-20260917T065141062Z` (arm-B reference,
`no_onward`, onward diagnostics) and
`.codex-research/move-build-offer-20260917T070254469Z` (adds `launch_level`
and `bot_moveHeadingOffer`). Analyzer `tools/movement_replay.py` is version 3,
32 tests passing; re-run over the retained corpus it reproduces version 2's
verdicts exactly (`.codex-research/replay-v3-20260917T065058266Z`).

Every retained B-only admission and censored pair was audited. Two of the
three B-only admissions are second-hop heading rescues (hop-1 deltas of 6.83
and 7.87 degrees); the third, large-blue search 19, is a 0.32-degree pair
whose split came from the onward test. All five censored results were missing
measurements: arm B was judged against the control arm's walk horizon.

Arm B now has its own walk reference. Censoring is zero on all three routes
(was 2, 1, 2 of 44, 42 and 52 pairs), and `arm_isolation` is clean over 68
searches and 1,092 judged candidates: no replay commit, each candidate judged
by its own arm's reference, no control candidate judged past the control's own
horizon, nothing unjudged. Outputs at
`.codex-research/replay-v3-armb-20260917T070026774Z`.

With every pair decided, 138 pairs on three routes give 28 both-admitted,
7 A-only and 8 B-only. **Replacing the aimed heading with the stored heading
is refuted.** The supported change is to offer both constructions through the
existing admission rules and rank by margin: `bot_moveHeadingOffer`, default
off.

Every `no_second_landing` outcome in the retained corpus was a one-hop
candidate (34 large-red, 17 large-blue, 0 small-red), so none named a real
failure; the judge now reports `no_onward`. At the measured `no_onward` roots
the post-landing walk neither braked nor held (`brake_frames=0`,
`held_frames=0`, `end_stage=2`): it covered about 56 units of the required 128
in the 48-frame window, while its two-hop siblings reached `end_stage=5` and
about 312 units. The onward test was not changed.

Matched stand campaigns on the large seed, three repetitions per arm, same
build, all passing with `move`-only ownership, zero deaths and zero parity
mismatches (`.codex-research/move-offer-20260917T070254469Z-{control,offer}-r*`):

| Case | Control | Offer |
|---|---|---|
| `red_out_L1_stand` | 20,700 / 20,700 / 20,175 ms (median 20,700) | 19,575 / 19,250 / 19,075 ms (median 19,250) |
| `blue_out_L1_stand` | 19,925 / 20,650 / 19,925 ms (median 19,925) | 20,525 / 19,800 / 20,525 ms (median 20,525) |

Red separates completely at three repetitions, 925 to 1,625 ms faster. Blue
overlaps and its median is 600 ms slower. The offer is therefore **not**
qualified as an improvement, and no case is near the 15,000 ms target.

A route-level diagnostics-off against diagnostics-on identity check is not
available: the fixture start is an rcon round trip at wall-clock time, so two
runs of a case do not share roots (3425 against 3275 level, and differing
first post-corner commits). Four matched repetitions per arm on the small
clean seed share a modal 19,775 ms with 1,175 and 1,850 ms of spread.

### The stored-heading offer, qualified and failing the gate, 2026-09-17

Record: [the heading offer](records/2026-09-17-heading-offer.md). Build
`.codex-research/move-build-offer-20260917T070254469Z`, one binary for both
arms, `--edges --parity 256 --parallel 1` throughout, outputs under
`.codex-research/move-offer-20260917T070254469Z-*`.

`bot_moveHeadingOffer` (default off) makes the stored-heading construction an
ordinary proposal: same admission rules, same walk reference, counted in
`stateAdmitted`, ranked with the control alternatives by margin. No gate, band,
bound, hold, onward distance or exclusion was changed and no Force is spent.

| Campaign | Control | Offer |
|---|---|---|
| `red_out_L1_stand`, 3 repetitions | 20,700 / 20,700 / 20,175 ms | 19,575 / 19,250 / 19,075 ms |
| `blue_out_L1_stand`, 3 repetitions | 19,925 / 20,650 / 19,925 ms | 20,525 / 19,800 / 20,525 ms |
| `red_ret_L1_stand` | 22,550 ms, `move` | 18,750 ms, `move` |
| `red_ret_L1_respawn4` | 19,625 ms, `move` | 19,850 ms, `move` |
| `blue_out_L1_respawn5` | 20,825 ms, `jumppad,move` | 17,575 ms, `jumppad,move` |
| 12-case seeded level 1 | 12/12, owners `jumppad,legacy,move` | 12/12, owners `jumppad,move` |
| Seeded mean against greedy | +1,748 ms | +1,240 ms |
| Seeded mean against `move-review-20260916-seeded-b` | -338 ms | -846 ms |

Every run above passed and completed with zero deaths, zero forbidden Force
spend, zero parity mismatches and zero judged-short candidates. The offer arm
is faster than its own control on 8 of 12 seeded cases, mean -508 ms, range
-2,050 to +900 ms, and red separates completely at three repetitions by 925 to
1,625 ms. Blue does not separate and its median is 600 ms slower.

The 36-case unseeded floor in the default configuration is **36/36, zero parity
mismatches, `move` the only owner**, so the new diagnostics, the `no_onward`
label, the arm-B reference and the offer's dormant code did not move the
default.

**The offer does not pass the level-1 gate**: it is still 1,240 ms per case
slower than the retained greedy arm. Its clean ownership is not a fix for
`red_ret_L1_stand`, whose ownership varied between campaigns of this same
build (`legacy` in the seeded arm, `move`-only in the regression campaign). No
sub-15-second stationary no-Force pickup exists in either arm. The cvar stays
default off and is not installed.

The seeded arm's remaining deficit against greedy is not in the stand family
(19,944 against 20,381 ms) but in the respawn families (18,000 against 15,356
at respawn5), which is a different route problem from the one examined here.

### Independent offered references, 2026-09-17

[Correction and fresh verification](records/2026-09-17-independent-reference.md):
offered B no longer changes A's walking reference. Candidate role and actual
reference provenance are explicit. The onward turn explanation remains a
hypothesis, with travelled distance and objective progress now measured.

### Reroute admission measured; default off, 2026-09-18

`bot_moveReroute` (default off, two bits) continues a candidate rejected for
leaving the selected path without the path stop, admits it only when its
landing opens a planner route that beats the route it left, and judges it
against a third walk reference of its own. Bit 2 additionally ranks every
admitted candidate by route time.

Measured on two builds, serial worlds. The rejected candidates the
[landing probes](records/2026-09-17-human-landing-probes.md) found are
**executable**: at the jump-6 human-derived root the search commits a
rerouted two-hop landing at 735.0 units/sec where the control commits the
run-up that sheds to 486, the executor flies it, and the installed route
carries the run home with no death and no parity mismatch. Remaining pickup
there is 9.425 s with admission alone, against 12.500 and 11.775 s for the
control.

It does **not** pass the gate. With admission alone the 12-case level-1
seeded arm is 3,650 ms slower in total (median 18,913 against 19,350 ms) and
`blue_out_L1_stand` is about 2,175 ms slower at three repetitions; with route
ranking as well, blue loses about 5,250 ms. The 36-case default floor is
36/36 with the cvar unset, zero mismatches, no legacy owner.

The measured cost has a mechanism: installing a route mid-run leaves planned
strafe edges priced for the landing state, and the bot no longer carries that
state when it reaches their takeoffs. `edge=strafe reason=stale` on
`blue_out_L1_stand` goes from 1-2 per run in the control to 10-14 with the
pass on, arriving in runs of seven at 250 ms intervals, each a replan and a
retry. See [the reroute record](records/2026-09-18-reroute-admission.md) for
the arms, the limitations and the ordered repairs.

**Corrected after review, 2026-09-19, unmeasured.** Two defects in the pass
itself. The route installed at the commit was built before `StatePreferred`
could change the winner, so it could be the previous winner's or, when no
build ran that search, an earlier search's left in the shared buffer; the
build now runs after the last thing that can change the winner and the
installer refuses a route not built for the candidate being committed. And
the pass forecast each candidate's continuation against the path the flight
leaves, then constructed and installed the replacement unforecast, so the
onward stage and the hold that decided a candidate were not the ones it
would fly; the pass now re-rolls the candidate with the replacement route in
place and no permit - the state `RerouteInstall` leaves behind - and judges
that rollout. **Every reroute figure on this page predates both fixes.**

### The onward deadlock and its bound, 2026-09-18

Reroute admission surfaced two pre-existing defects, both reachable without
it. A landed plan clears its last stage only by travelling
`MOVE_ONWARD_DISTANCE` from the mark, and `Move_Command` replans only when no
sequence is in progress, so a bot that lands where it cannot cover 128 units
holds a plan that never finishes and a path that is never searched again:
measured twice, 181,850 and 184,475 ms of a bot shuffling inside ten units of
one pocket on `blue_out_L1_stand`, with no onward completion and no path
event, in arms that differ (one with `bot_moveStaleAvoid`, one without). The
planner's arrival model also disagrees with the executor often enough to
produce runs of stale planned edges - seven in a row 250 ms apart at one
takeoff, the bot at 31-148 units/sec in front of an edge needing a
417 units/sec landing-frame tap.

`bot_moveOnwardBound` (default off) releases the stage after 40 grounded
frames and replans; the hold is reported in every arm. Floor 36/36 with the
bound on and off, three named regressions pass, and the bound never fired in
those 49 worlds. Over ten repetitions of `blue_out_L1_stand` per arm the
stall did not recur, but the hold did: the unbounded arm's slowest run
(28,200 ms) held twice at (1888.8, 2592.4), and the bound arm fired at the
same point to a tenth of a unit and finished at 23,400 ms. One occurrence per
arm from different roots: the mechanism is demonstrated, a time benefit is
not. See [the reroute record](records/2026-09-18-reroute-admission.md).

The bound's own gate (`.codex-research/onward-bound-20260918-c`) left the
stands identical (red and blue medians equal, same values) and produced a
7,100 ms difference on the seeded families **with the bound never firing** -
the two arms ran the same code. That calibrates the seeded-arm aggregates
quoted above: differences of 3,650 and 5,975 ms from one repetition per arm
are inside that noise and are withdrawn as evidence. The stand repetitions,
the death and the two stalls stand. `bot_moveOnwardBound` fixes no measured
default-build failure and stays off; its hold report is always on.

### The route prefix, 2026-09-19

The entry tolerance the follower measured is a distance the movement engine
can already hit, so a route asset is made usable from the fixture by walking
into it rather than steering it. `bot_routePrefix` (default off) gives the
engine the tape's start as its objective, submits its commands, and starts
the tape when the bot is inside a gate stated on the route's own first line -
along, cross, vertical and speed - and at rest. The tape's clock starts at
the hand-off and `prefix_ms` is reported beside it, so a walk can never be
counted as route time.

Two findings. The planner arrives at a cell, not at a point: the first run
walked 12.6 units and then stood 23.1 units from the tape's start for 29.5
seconds, with no hold event, because the walk's hold detector requires the
objective to be more than 64 units away. And closing on a point needs the
right law - a full-throttle waypoint intent parks 13.3 units short on command
quantisation, a wish-limited proportional law creeps and then parks at its
dead radius, and `sqrt(2*600*d)` (the speed whose coasting distance is what
remains) arrives within 0.7 units at a standstill on all three routes.

Nine runs, three per route, from `(152,-1160,1924.125)`: every one reached the
enemy flag, every route time equals its recorded time, and the totals are
14,875, 15,350 and 19,000 ms against the search's 19,150 ms median from that
stand. The comparison is indicative, not matched: the route world runs
`pmove_fixed 1` and the search median was taken without it, and the level-1
stand case declares `force 0` where a prefix run carries the recording's pool
of 100 - which nothing in either half of the run spends. Playback with the
cvars unset is unchanged (14,825 ms and 37.832 max drift). See
[the prefix record](records/2026-09-19-route-prefix.md).

### Where a route is worth taking, 2026-09-19

Selection, measured rather than designed. The fastest asset entered at its own
start by the prefix, against `time_ms` for the same fixture case in 60
retained standard floors: `red_out_L1_stand` 14,875 against a 21,150 ms median
(route by 6,275), `red_out_L1_respawn4` 25,150 against 18,325 (search by
6,825), `red_out_L1_respawn5` 33,500 against 16,375 (search by 17,125). Every
run reached the flag; the losses are the walk, which is 12,275 and 20,625 ms
because the recording starts 2,593 and 3,543 units *behind* those respawns.

A suffix entry does not rescue them: their nearest contract mark is 1,358 and
1,598 units off the line, at recorded speeds of 727.7 and 794.2 units/sec,
and a walk's ceiling is the 250 units/sec wishspeed - the chain-breakage wall
the search work already measured, further out of reach. The enterable marks
are the ones at or near rest, which on a recording from a standing start is
the first one. So the selection rule is `cost(reach an enterable mark) +
route after it < search time`, and covering the respawn cases is a request
for recordings from those respawns, not engine work.

One format note, correcting the follower record by half: the asset's contract
already carries `velocity` and `yaw` per mark, so a mid-route entry state
exists today; it is `route_asset_build.tape_text` that drops it from the `k`
line. A suffix entry would need a tape field, not a denser contract. See
[the selection record](records/2026-09-19-route-selection.md). The `k` line has
since gained the command index each mark was recorded after
([the review repairs](records/2026-09-19-review-repairs.md)); velocity and yaw
are still only in the JSON.

### Exit-speed 500 confirmed at pinned starts, 2026-09-20

Frozen build `.codex-research/successor-aim-build-20260920-a`, cvar only.
Four declared start classes (`corner_level mod 200`), six matched pairs each,
plus a three-pair anchor pinned to the exact level 3350: 53 timed worlds out
of 141 native worlds, one cell (class 125 at 2000, pair 2) incomplete because
twelve consecutive worlds drew a ladder that never met the class.

Every cell produced one distinct completion time. 500 against 2000: class 150
17,575 against 19,050, class 125 15,925 against 16,450, class 100 19,375 both,
class 75 **21,125 against 20,100**. Weighted by the earlier cohort's declared
class frequencies (43/34/10/6): 17,394.4 against 18,202.2 ms, -807.8 ms;
equally weighted, -243.75 ms. 500's worst cell is 21,125 against 20,100.
Pooled logged rollout median inside the completed trip 36.85 against 38.70 ms
at about 23.8 and 23.3 searches a world (the first summary quoted 35.5 against
36.8 over a window that ran past completion; `summary-confirm-2.json` corrects
it). Floors at 500, unpinned: 36/36, zero deaths, movement
owner throughout, zero parity mismatches, both seeded named regressions pass.
Parity makes zero comparisons.

The strata are classes because the pre-start play log steps 250 ms: a world
starts only on its own ladder `L0 + 250k`, and exact pins at 3325, 3300 and
3275 each missed 4 worlds of 4. `corner_level mod 200` accounts for all 100
worlds of the retained sweep and was confirmed prospectively here - 53 worlds
on 14 distinct absolute levels, each landing on its class's time, and the
exact-level anchor agreeing with its class. `--corner-class` in
`tools/movement_bench.py` pins it; floors stay unpinned. See
[the pinned confirmation](records/2026-09-20-exit-speed-pinned-confirmation.md).

### The slow class is decided at one frame, 2026-09-20

Frozen build `.codex-research/successor-aim-build-20260920-a`, cvar only, start
class 75 pinned. The arms are identical for two commits and first differ at
relative level 4,775 ms, a `kind=straight` commit offering 18 candidates and
admitting 12, whose rollout list is byte-identical across arms. Three admitted
candidates win under different exit weights: cand 5 (progress 712.8, exit
444.9) 21,125 ms, cand 1 (708.9, 448.8) **19,250 ms**, cand 13 (705.5, 452.2)
20,100 ms. At weight 1000 their scores are 660.1, 660.1 and 660.0.

Over 37 worlds and 14 weights, completion is a function of that frame's
winner: the cand-13 band is exactly 20,100 across a two-fold weight range and
the cand-1 band exactly 19,250; only inside the cand-5 band do the later
decisions move the trip, by 250 ms. The 1,025 ms class-75 regression of 500
against 2000 is this frame. The best candidate is the middle point on both
terms of `score = margin + landSpeed * ms/1000`, so it wins only for weights
between 950 and 1050.

A forced-candidate diagnostic then isolated the frame. `bot_moveSelectNth` /
`bot_moveSelectCand` (default off) commit a named admitted candidate at a
named sequence commit after `StatePreferred` and before the route install.
With the policy fixed at `bot_moveExitSpeed=2000` in every arm, three
repetitions each: control 20,100 ms, forcing the ranking's own winner 20,100,
forcing cand 5 20,925, forcing cand 1 **18,775**. The frame is worth 2,150 ms
under one policy and the ranking takes the middle of three. The control
reproduces the reference build to the millisecond; the floor is 36/36 with the
override off and both seeded regressions pass.

That withdraws two weight-derived claims: later decisions are worth up to
475 ms on the same candidate (cand 1 is 19,250 under weight 1000 and 18,775
under weight 2000), not "at most 250 ms"; and nothing establishes that a
bounded continuation would fail. What is measured is the horizon - a one- or
two-decision lookahead over the realized continuations ranks the worst trip
first, and the winner leads only from the fourth decision, about six seconds
out. The fastest arm is not the most airborne (79.4% at 374.3 units/sec
against 80.8% at 362.4). See
[the selection failure](records/2026-09-20-slow-class-selection-failure.md).

## The bot route recorder, and the rescue switch out, 2026-09-21

Recording the bot's own trip needed a source change: `Arm()` in
`bot2_route_record.c` refused `SVF_BOT` by construction and nothing else did.
The console command still refuses a bot; a bot is armed only by the corner
fixture on its own release frame under `bot_routeRecord 2`, a new value of the
existing cvar rather than a new switch, and the recording's root is the release
rather than the first non-zero command. The rejected library-rescue arm and all
of its code left in the same rebuild.

The inertness check is the fixture's determinism. On
`.codex-research/bot-recorder-build-20260921-a`, `red_out_L1_stand` pinned at
class 125 completes in **15,925 ms with 13 straight commits and 5 state commits
with the recorder off, and in 15,925 ms with 13 and 5 with it armed**; the
rescue screen's class-125 control worlds are 15,925 ms with 13 and 5 at corner
levels 3325 to 4525. Time and decisions both reproduce, which is the comparison
worth making: inert code has shifted candidate counts before without changing
either. Zero deaths, movement owner throughout, 667 parity comparisons per
world with zero mismatches and zero refusals.

The recording is 636 rows, complete at the enemy flag, retained beside its
world. Its own clock reads 15,900 ms against the harness's 15,925 - one server
frame, the offset every retained route asset shows between the two observers of
the pickup.

**The self-control then failed, at one landing.** Played back through
`route_approve.py` from three releases pinned to class 125 (corner levels 5325,
4925, 4725), the tape returns `tape_end` at 15,925 ms with 31.839 max drift and
no flag, identically in all three. It is not wandering: through command 580 it
tracks the recording to **0.343 units and 0.017 units/sec**, the repaired
player's documented fidelity. At command 583 it lands 0.283 units lower and
leaves with 20.689 units/sec less than the recording kept - about what one extra
grounded 8 ms step costs at 426 units/sec - and the remaining 50 commands follow
from that. So this trip has a landing decided below the player's floor, and by
the order's own reading table neither reading of the class difference is drawn
until a repair with a failing regression closes it.

Two by-products: the replay is deterministic across absolute level within class
125 (three levels spanning 600 ms, identical to the millimetre), and the drift
is not the tape's four-decimal angles, which are worth about 5e-4 units over the
whole trip. The bot's command angles carry a constant `delta_angles` offset, so
unlike a human's its raw 16-bit angles are exactly recoverable; that is the
untested repair candidate. No gate row moves. See
[the recorder record](records/2026-09-21-bot-route-recorder.md).

## The transfer question, answered closed-loop, 2026-09-22

The open-loop replay was withdrawn and replaced by a decision tape: the
class-125 world's committed decisions, executed in another world through the
forced-candidate hook, which can only select a candidate the judge already
admitted. The bot still searches, rolls out and judges from its own live state,
so a refusal is the measurement rather than a broken run. Six defects had to be
repaired before the closed-loop self-control passed - the last two being that a
decision cannot be named by its printed aim (7 of 166 admitted straight pairs
in one judged set agree on hops, both run-ups and both aims) and that a tape
must stop at the objective or it carries the return leg. A decision is named by
where it goes: kind, hops, run-ups, and the graph edges of a state plan or the
landing cell of a straight one.
[Record](records/2026-09-22-decision-tape.md).

18 worlds, three per class with a tape-off control per class, all complete, no
pin misses, every triple identical:

| Start class | Control | Tape | Change | Hits | First miss |
| ---: | ---: | ---: | ---: | ---: | --- |
| 75 | 21,125 ms | 21,125 ms | 0 | 1 of 11 | entry 1, refused |
| 100 | 19,375 ms | **18,925 ms** | **-450** | 2 of 11 | entry 2, absent |
| 150 | 17,575 ms | **17,025 ms** | **-550** | 1 of 11 | entry 1, absent |

**Entry 0 transfers from every start** - the stand's state chain on edges
50732/26663, with aim gaps of 0.0004, 0.0663 and 0.0072 degrees. **The sequence
then stops at the second or third decision**, refused at class 75 and absent at
100 and 150, and the late roots return without matching. No class comes near
15,925 ms or hits more than 2 of 11, so **the class-125 sequence is not
available whole from any other start**: it is not the chain explorer's first
plan, and the explorer must search per start class with its library keyed by
start state. Forcing the reachable prefix is worth 450 ms at class 100 and 550
at class 150 under forced selection - a diagnostic, moving no gate row, but
evidence that the ranking's own choice at those first decisions is not the best
available from those starts.

## The decision explorer, stage 1, 2026-09-22

The four checks the explorer's work order requires before any search, on the
decision-tape build `decision-tape-build-20260922-f` with no rebuild and no
source change. [Record](records/2026-09-22-decision-explorer-stage1.md); worlds
under `.codex-research/decision-explorer-stage1-20260922-a/` and
`.codex-research/decision-explorer-smoke-20260922-{a,b}/`.

**Index forcing is reproducible to the frame within a class.** At class 75,
under the 11-entry class-125 leg tape, the commit after the prefix's first hit
is the leg's second (`commit_nth=2`, level 6850, 12 of 24 candidates admitted).
Two admitted candidates forced there, two worlds each:

| Forced candidate | Release levels | Completed | Commits | Census |
| --- | --- | ---: | ---: | --- |
| none (reference) | 5075 | 21,125 ms | 15 / 7 | - |
| 3, straight, 2 hops | 4075 and **3675** | **20,275 ms** both | 16 / 8 both | identical |
| 15, state, 1 hop | 3075 and 3075 | **19,025 ms** both | 12 / 5 both | identical |

Each pair agrees decision for decision on every field the commit line prints,
with levels taken from each world's own release - the cand-3 pair from two
release levels 400 ms apart in the same class. Forcing is also 850 and 2,100 ms
faster than the ranking's own choice at that decision, which is a diagnostic
under forced selection and moves no gate row.

**A world costs 16.9 s median** (mean 18.0, 9.5 to 35.3, 27 completed worlds at
`--timescale 5`), and a bench call whose four worlds never meet the pinned
class costs about 26 s. A branch is about 20 s expected, so a beam of 3 over a
decision offering 12 to 21 admitted candidates is 12 to 21 minutes per depth.

**All eight start classes are reachable.** The play log samples at 4 Hz from
the runner's first think, so a world's ladder is `L0 + 250k` and
`gcd(250, 200) = 50`: a world offers exactly the four classes congruent to `L0`
modulo 50, and the 40 retained play logs start at 2350, 2375 or 1900 - the two
families, about three worlds in five on the even one. Classes 0, 25, 50 and
175, never landed before, were each landed here (18,525 / 18,025 / 18,600 /
19,675 ms at release levels 3600 / 3625 / 3850 / 3575). The cost of the other
family is another world, and the bench gives a pinned class four: three of the
four forcing worlds missed class 75 that way and landed on a second call.

**`strafe_fixture_build.py` now fails a failed build.** MSBuild's own
`Build FAILED.` line and the two staged binaries are checked directly before
the manifest is written, whatever the builder's exit code was; either failing
exits 2 and leaves no manifest. Eight regressions on a stubbed builder, six of
which fail against the pre-repair script.

**The explorer is built**: `tools/movement_decision_explore.py`, 34 unit cases
on hand-built logs, resumable from its progress file, one world per bench call.
Its smoke run at class 75 to depth 1 ran four worlds, all complete, and wrote
the fastest as an 11-entry tape: 21,125 ms control against 19,725, **18,950**
and 21,000 ms for the first three admitted candidates of the first decision.
Two findings it had to be built around: the forced-candidate override is
offered on the bot's pre-release commits too, because the commit counter
restarts at the release, so a reader must take the leg's commit; and a leg
ending on an arriving hop has a last decision no tape can name (`arrives=1`
prints `c0=-1`), so the explorer reads the leg up to it and branches no further.

## The decision explorer, class 75, 2026-09-22

The stage-2 campaign's class-75 result and its three checks, on the same
`decision-tape-build-20260922-f` with all 1,117 source hashes matching the
working tree before and after and no C source touched.
[Record](records/2026-09-22-decision-explorer-c75.md); campaign at
`.codex-research/decision-explorer-c75-20260922-a/` (175 worlds, 163 usable,
49 minutes, beam 3, stopped after two depths without improvement), reproduction
worlds, leg budgets and tree tables at
`.codex-research/decision-explorer-c75-verify-20260922-a/`.

**The best tape reproduces exactly.** `c75-best.tape`, 11 entries, sha256
`401cdce9...`, written from `control-a2-d1c1-d2c3-d3c4`: three fresh class-75
worlds complete `red_out_L1_stand` in **15,950 ms** with **11 of 11 entries hit,
0 refused, 0 absent and every aim gap 0.0000**, zero deaths, the movement owner
throughout, zero parity mismatches, and agree decision for decision - one of
them released **600 ms** from the other two, a longer span than stage 1's 400 ms
pair. Three tape-off controls alternated with them return **21,125 ms** three
times. The class-125 cross-check returns **15,925 ms with and without its tape**,
13 and 5 commits, 11 of 11 hit: the self-control cell is unchanged. One control
reported **zero parity comparisons** where its identical siblings reported 905
and 70, which is a parity-sampler observation and the count gate row 1 excludes
by name.

**Row 3 on a fast trip, for the first time.** LEG_BUDGET2, waiting as
`precommit_grounded` plus `uncommitted_grounded` over grounded frames:

| Arm | Grounded | Waiting | Share | `precommit` | `uncommitted` |
| --- | ---: | ---: | ---: | ---: | ---: |
| class-75 tape, 3 worlds | 69 each | 21 each | **30.4 %** | 30.4 % | 0 |
| class-75 control, 3 worlds | 198 each | 141 each | **71.2 %** | 54.0 % | 17.2 % |
| class-125 tape, 1 world | 66 | 25 | **37.9 %** | 37.9 % | 0 |

The threshold is 15 percent and neither figure meets it, but the frame counts
moved by a factor of 6.7 (21 waiting against 141) while the denominator fell too
(69 grounded against 198), and the fast trips carry **no `uncommitted_grounded`
run at all**. At waiting frames the outcome mix is the familiar one - the
trigger contact the forecast cannot model is 67 percent of the tape arm's judged
candidates and 81 percent of the control's - and **no candidate was admitted at
a waiting frame in any of the seven worlds**. The threshold predates any trip
under 16,000 ms; these are the numbers, not a verdict.

**The judge's ranking does not predict the trip.** `tools/movement_decision_tree.py`
(22 unit cases on a hand-built campaign), read-only over the winning lineage's
first three decisions, each candidate's score taken from its own branch world's
commit line because the `[SPIKE]` line does not print one:

| Decision | Admitted / measured | Distinct times | Spearman | Judge rank-1 trip | Best branch | Winner's rank |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1, level 5325 | 21 / 21 | 14 | **0.178** | 21,125 ms | 18,125 ms | **7 of 21** |
| 2, level 6850 | 11 / 11 | 11 | **0.036** | 19,575 ms | 17,750 ms | **6 of 11** |
| 3, level 8625 | 18 / 17 | 16 | **0.335** | 19,575 ms | **15,950 ms** | **5 of 17** |

All three are weakly positive: the score separates one-hop from two-hop
candidates and then fails to order anything inside a class, which is where the
4,000 ms lives. At decision 3 the winner scored 646.0 against the top-ranked
691.3 and finished 3,625 ms sooner. **The tape's identity rule is not an identity
for the trip**: at decision 3 five candidates the rule calls one decision - same
kind, hops, run-ups and landing cells - complete in five times spanning 1,625 ms,
and six such candidates at decision 1 span 1,275 ms. Repeated times that *are*
exact duplicates of one decision occur three times at decision 1 and never at
decisions 2 or 3. No time here moves a gate row: stage 3's two conditions - the
bot selecting the tape itself, and the fallback measured - are both open.

## The tape library: the bot selects its own tape, 2026-09-22

Stage 3 of the decision-explorer work order, on a fresh build
`.codex-research/tape-library-build-20260922-b`, manifest
`20260922T081924845983Z.sources.json` sha256 `663852e1...`, all 1,117 source
hashes matching the working tree before and after collection.
[Record](records/2026-09-22-tape-library.md); eleven worlds, every one
retained, at `.codex-research/tape-library-20260922-a/` (three detached phases,
one world per bench call, sequential).

**The two conditions stage 3 names are met.** A library of tapes and one
`index.txt` sits under `bot2-decisions/<map>_<checksum>/`, bound to map and
checksum the way the graph cache is, behind the default-off
`bot_moveTapeLibrary`; `bot_moveDecisionTape` keeps its meaning and wins over
it, so every earlier campaign replays unchanged. At a leg start the bot forms
its own key - **leg `t1e83` (team and objective entity), start cell `0`
(`Move_RootCell`, the cell the planner plans from, confirmed by the first
replan's `from=0`), phase `level.time % 200`** - and runs a four-rung ladder:
the exact key, then the same leg and cell at the nearest phase, then a cell
within one graph link, then nothing. Every rung's candidate count is printed on
the census line, so the fallback is measured rather than inferred. The phase is
not chosen: nothing delays the bot's first command.

**Selection.** Three fresh class-75 worlds and one class-125 world, the bench
naming no tape: each selects its own tape **on the exact rung** and completes
`red_out_L1_stand` in **15,950 ms** and **15,925 ms** with **11 of 11 entries
hit, 0 refused, 0 absent**, every aim gap 0.0000, `matches` 6, 4, 1, 1, 3, 6,
3, 1, 1, 2, 1, zero deaths, the movement owner throughout and zero parity
mismatches over 698-727 comparisons. The class-125 world reproduces the
self-control's census line including `deferred=2`. The only difference from the
campaigns those tapes came from is **who chose the tape**.

**Inertness.** Same build, both new cvars unset: the class-125 control cell is
**15,925 ms, 13 commits, 5 state** and the class-75 control is **21,125 ms, 15
and 7** - the decision-tape and class-75 record cells, commit for commit.

**Fallback, measured.** Classes 100 and 150 have no key; the ladder falls to
the nearest phase on the same leg and cell (the class-100 tie at 25 ms is
broken by index order, which is deterministic and arbitrary):

| World | Rung | Tape | Hits | Absent | Completed | Known control | Change |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| `c100-lib-1` | phase | `c75-best` | 2 of 11 | 1 | **19,150 ms** | 19,375 ms | -225 |
| `c150-lib-1` | phase | `c125-self` | 1 of 11 | 6 | **17,025 ms** | 17,575 ms | -550 |

Both trips complete under the ranking after the miss, zero deaths, the movement
owner on every frame, zero parity mismatches over 816 and 741 comparisons.
`c150-lib-1`'s 17,025 ms is the transfer collection's own class-150 figure,
reached there with the bench naming the tape: the same trip arrived at by the
bot's own selection. Both fallback times are one world each and are not a
result about the fallback.

**The zero-comparison world has a cause.** `c75-control` reported
`horizons=0`, as one of three identical class-75 controls did on the previous
build, so it reproduces. It is **the tally's print condition, not the sampler's
coverage**: `Bot2_MoveParityAfterPmove` counts every matched frame in
`parity.compared` but prints its `reason=horizon` event only when an exhausted
horizon coincides with `parity.rollouts % 10 == 0` or a planned horizon, and
`parity_summary` reports the largest `frames=` it saw on such an event. Both
zero-comparison worlds carry `parity_armed=1` with `parity_index` up to **52**
on their own `onward` completion lines, so the comparisons happened. The repair
- one final tally per leg - needs a new parity reason that
`movement_bench_lib.verdict` and `parity_summary` both currently treat as a
mismatch, so it is three coupled changes to every world's pass/fail semantics
and is left to its own step.

**Tools.** `tools/movement_tape_library.py` builds a library and its index
(23 unit cases); `movement_bench.py --decision-library` stages one and sets the
cvar, naming no tape; `movement_decision_explore.census` now keeps a totals
field that is not a count, with a regression. The whole Python suite is **800
tests, all passing**.

## The four-class library: rows 2 and 3 with the bot selecting, 2026-09-22

Same build `.codex-research/tape-library-build-20260922-b`, manifest
`20260922T081924845983Z.sources.json` sha256 `663852e1...`, all 1,117 source
hashes matching the working tree before the first world and again after the
last, no rebuild. [Record](records/2026-09-22-four-class-library.md); 25 worlds,
every one retained, at `.codex-research/tape-library-four-20260922-a/` (one
detached campaign, one world per bench call, sequential, arms alternated within
each class).

**The library has four keys**, one per declared start class, all under leg
`t1e83` and start cell 0: `c75-best.tape` (`401cdce9...`),
`c100-best.tape` (`ca076e87...`), `c150-best.tape` (`24ce31a5...`) and
`c125-self.tape` (`f51fa0f1...`), index sha256 `64b6e5ef...`, each bound in
`manifest-tapes.json` to the explorer campaign that produced it.

**Row 2, equal weight over the four classes** (each class's time is its three
worlds' median; the row's median is the median of those four):

| Class | Library | Control | Change |
| ---: | ---: | ---: | ---: |
| 75 | **15,950 ms** | 21,125 ms | -5,175 |
| 100 | **16,875 ms** | 19,375 ms | -2,500 |
| 125 | **15,925 ms** | 15,925 ms | 0 |
| 150 | **18,675 ms** | 17,575 ms | **+1,100** |

Equal-weight median **16,412.5 ms** against the row's 16,500 - met - and tail
(worst completed library world) **18,675 ms** against 18,000 - **not met**.
Control on the same worlds: 18,475 ms and 21,125. The declared-weight mean
(4, 12, 30, 47) is **17,438 ms library against 17,428 ms control**, a change of
+11 ms, because class 150 carries weight 47 and got worse. Three repetitions
per class is half the six the row asks for.

**Every library world selected on the exact rung**, `exact=1` of `keys=4`, so
selection is not what failed. **Two of the four tapes do not reproduce**: the
class-75 and class-125 tapes hit 11 of 11 entries with every aim gap 0.0000,
while `c100-best` hits 4 of 11 (first absent at entry 4, nearest candidate
0.3110 away) and `c150-best` hits 2 of 10 (first absent at entry 2, 0.0581
away). Those two are exactly the tapes that had **never been run on a fresh
world**: an explorer's stopping rule reads a branch world's time, and a branch
world ran under a prefix plus a forced index, so an explorer result is not a
tape result until the tape has run on its own.

**Row 3 on the same worlds**, `tools/movement_leg_budget.py` under LEG_BUDGET2,
waiting being `precommit_grounded` plus `uncommitted_grounded` over the leg's
grounded frames:

| Class | Library share | Control share | Library `uncommitted` run |
| ---: | ---: | ---: | --- |
| 75 | **30.4 %** | 71.2 % | none |
| 100 | **58.3 %** | 59.5 % | yes |
| 125 | **37.9 %** | 37.9 % | none |
| 150 | **62.5 / 63.1 %** | 50.5 % | yes |

**Not met at any class in either arm** against a threshold of 35 percent, and
the row excludes any `uncommitted_grounded` run. Pooled: 581 of 1,136 grounded
frames library, 909 of 1,527 control. Row 3 moved with row 2 in both
directions - class 75 gained 5,175 ms and dropped 40.8 points, class 150 lost
1,100 ms and rose 12.0 - except at class 100, where 2,500 ms arrived with the
share unchanged and the airborne fraction 0.828 against 0.808.

**No candidate was `admitted` at any waiting frame in any of the 24 completed
worlds.** The refusal mix is the leg budget's: 825 of 1,299 judged candidates
at library waiting frames are the trigger contact the forecast cannot model,
1,629 of 2,016 in the control. One column is new - 222 out-of-corridor
candidates at library waiting frames, 61 per world at class 150 and 13 at class
100, none in any control - and those are the two classes whose tape stopped
hitting.

**Row 1 signal only, nothing repaired.** 22 of 24 completed worlds report a
non-zero parity comparison count (70 to 855); `c75-ctl-1` and `c75-ctl-3`
report zero, the same class-75 control cell that reported zero on the two
previous builds, while `c75-ctl-2` reports 70. Zero mismatches in all 24, zero
deaths, the movement owner on every frame. The cause is the tally's print
condition, already recorded; the repair is still its own step.

One pin miss (`c100-ctl-3`, four level attempts without its class), retained
and replaced under `-a2`. Rows 4 and 5 are not measured here, and this is one
case on one map.

## The tie-break and the parity tally, 2026-09-22

[Record](records/2026-09-22-tie-break-and-tally.md). Build
`.codex-research/tie-break-build-20260922-a`, worlds under
`.codex-research/tie-break-tally-20260922-a/`, 1,117 source hashes matched
before the first world and after the last. The four-class campaign's own
library, copied and bound by hash, with **no tape re-extracted**: only the
engine's tie rule changed.

**All four explorer tapes reproduce.** When several admitted candidates match a
tape entry by the landing-cell identity, the tie is now broken by the
printed-aim gap - the quantity the census already reports - and the judge's own
rank decides only an exact tie on it. A score is computed from the live root
and the explorer's branch world reaches that root displaced from a fresh
tape-only world's, so the rank was not stable between the two; the aim is a
property of the candidate and is.

| Class | Entries | Hits before | Hits now | Entries with several matches | Time before | Time now | Explorer's own |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 75 | 11 | 11 | **11** | 6 | 15,950 | **15,950** | 15,950 |
| 100 | 11 | **4** | **11** | 5 | 16,875 | **16,725** | 16,725 |
| 125 | 11 | 11 | **11** | 6 | 15,925 | **15,925** | 15,925 |
| 150 | 10 | **2** | **10** | 6 | 18,675 | **15,975** | 15,975 |

Every entry of every library world is a hit with an aim gap of **0.0000**,
every world selected on the **exact** rung, and the three worlds of each class
agree to the millisecond. The tie-break fires at 5 or 6 entries of every tape,
so the two unchanged classes are a regression passed and not a rule that never
ran.

**Row 2's two numbers are met on these worlds and the row is not reported
passed**: equal-weight median **15,962.5 ms** against 16,500 and tail **16,725
ms** against 18,000, control on the same build 18,475 and 21,125 - over three
repetitions per class where the row asks six, with one control per class.
Declared-weight mean **16,054.6 ms** against 17,427.7 control and the row's
last figure of 17,394. Rows 1, 3, 4 and 5 are not measured here, and row 3's
absence matters: the plan's own rule is that a row-2 gain with row 3 unchanged
is a selection artifact.

**A world's parity comparison count is now its own.** `Move_ParityTally` emits
one `reason=tally` event per leg where the tape census is emitted, carrying
`parity.compared`, and only when that has risen - so a world that genuinely
compared nothing still reports zero and still fails row 1.
`movement_bench_lib.parity_summary` reads the tally when present and falls back
to the largest `horizon` event when it is not, so every retained world is read
as it always was; it also reports `row1_ok`, the row's requirement outright.
`composition_verdict` exempts `tally` as it exempts `horizon`. All 18 completed
worlds report **667 to 905** comparisons with **zero** mismatches, and the
largest horizon count equals the tally in all 18.

The four-class campaign's two zero-count worlds are explained from their own
logs: all three class-75 controls carry the same six armed coverage lines
(`parity_index` 52, 10, 10, 10, 6, 3), `c75-ctl-2` printed two `horizon` events
at `rollouts=80` and `rollouts=110` because `rollouts%10==0`, and its two
siblings printed no parity event of any reason. The same cell on the repaired
build reports **905 comparisons at rollout 1,060**, so even the sibling's
reported 70 was the count one tenth of the way through the trip.

**The explorer now confirms a branch before it can be the incumbent**: one
world running that branch's whole sequence as a tape, no prefix and no forced
index, confirmed in branch-time order until the beam is full. The confirmation's
time and census are what the beam and the stopping rule read, every entry must
hit, and a branch that fails is recorded under `unconfirmed` and excluded. Nine
unit cases; no native campaign has run under it, because the tapes it exists to
protect now reproduce without one.

Zero deaths and the movement owner on every frame of all 18 completed worlds.
One fixture miss (`ctl-c75-0`, four worlds with no corner level at all),
retained and replaced under `-a2`, which returned the class-75 control cell
exactly. Both control cells reproduce on the new build with the library off:
15,925 ms with 13 commits of which 5 are state at class 125, and 21,125 ms with
15 and 7 at class 75.

## The level-1 exit gate, rows 1 to 4 on one build, 2026-09-22

[Record](records/2026-09-22-gate-l1.md). Same build
`.codex-research/tie-break-build-20260922-a`, manifest
`20260922T103836816430Z.sources.json`; **1,117 source hashes matched the
working tree before the first world and again after the last**, no rebuild and
no C source change. The tie-break campaign's own four-key library, copied and
bound by hash, no tape re-extracted. Worlds under
`.codex-research/gate-l1-20260922-a/`, every one retained, one bench call each,
sequential, three detached scripts (`run.sh` phases A to C, `run2.sh` the
unseeded row-4 arm, `probe.sh` the seed probe): 115 + 72 + 4 worlds, 34.8
minutes of bench time.

**Rows 1, 2 and 3 are met; row 4 is met in the floor's configuration and not in
row 2's; row 5 is not measured, so the gate is not reported passed.**

| Row | Verdict | The numbers |
| --- | --- | --- |
| 1. Safety | **Met** | Floor **36/36** library off and **36/36** library on, both seeded regressions **2/2** with the library on; zero deaths, owner `move` only, zero mismatches, **non-zero** comparison counts throughout (419 to 825), no forbidden spend |
| 2. Speed | **Met** | Equal-weight median **15,962.5 ms** (bound 16,500), tail **16,725 ms** (bound 18,000), at the **six** repetitions per class the row asks, two controls per class; control 18,475 and 21,125; declared-weight mean 16,054.6 against 17,427.7 |
| 3. Mechanism | **Met** | Library 30.4 / 55.1 / 37.9 / 35.0 percent against control 71.2 / 59.5 / 37.9 / 50.5 at classes 75 / 100 / 125 / 150, so no class waits more than its control, and both rows move together at every class |
| 4. Coverage | **Met unseeded, not met seeded** | Unseeded: twelve cases, three repetitions per arm, **72 of 72** complete, one case 200 ms slower in the library arm. Seeded: two cases die in both arms and lack three completions |
| 5. Cost | **Not measured** | Every world runs under `--parity 256` with one bot |

All 24 library worlds of rows 2 and 3 selected on the **exact** rung and hit
**every entry** of their tape at aim gap 0.0000, six worlds per class agreeing
to the millisecond. Pooled over the 32 row-2/3 worlds, the waiting frames'
candidate outcomes are 2,766 `refused/trigger_contact_unknown`, 574 `fell`,
74 `slower/trigger_contact_unknown`, 48 `slower_route`, 42 `slower`, 36
`no_path`, 30 `no_onward` and **no `admitted` outcome at all**, so the waiting
frames stay the candidate vocabulary's.

On the floor with the library on, the ladder fires in **11 of the 36 cases** and
harms none: the `phase` rung at the three `red_out_*_stand` cases, each hitting
**0** of 11 entries and completing under the ranking, and the `link` rung at
eight `red_ret_*` cases whose selection lands on the leg after the case's own
objective. Twenty-five cases form no key.

**What failed, and it is not the library.** Eleven worlds of the seeded row-4
arm ended in `death`, all of them `red_ret_L1_respawn4` or
`blue_out_L1_respawn4`, six library and five control, with owners `legacy`,
`move` and `none`. Both cases pass in both unseeded floors and in all three
unseeded repetitions of each arm, and a four-world probe with the 2026-09-16
seed removed and everything else held returns all four passing with zero deaths
and the movement owner. The defect is the research seed's coverage of those two
starts. The seeded configuration also puts `jumppad` beside `move` on the
respawn5 cases, which pass.

**A tape does not survive its graph seed.** Unseeded, `red_out_L1_stand`
selects on the **exact** rung and hits **zero** entries, so the unseeded row-4
arm measures an inert library. Seeded and unpinned, the same case shows the
whole ladder in three repetitions: class 75 exact, 11 of 11, **15,950 ms**;
class 50 falling to the nearest phase, 2 of 11, 21,350 ms under the ranking;
class 150 exact, 10 of 10, **15,975 ms**.

One pin miss (`c100-lib-6`, no corner level), retained and replaced under
`-a2`, which returned the class-100 cell exactly. This is one map, one case for
rows 2 and 3, four of the eight start phases; contention and row 5 are out of
scope.

## Tapes bound to their graph, and the search without the harness, 2026-09-22

[Record](records/2026-09-22-graph-binding-and-decoupling.md). Fresh build
`.codex-research/graph-binding-build-20260922-b`, manifest
`20260922T120839890335Z.sources.json`; **1,117 source hashes matched the
working tree before the first world and again after the last**. Worlds under
`.codex-research/graph-binding-20260922-b/`, every one retained, one bench call
each, sequential, detached from `run.sh`: twenty-six bench worlds in 384.8
seconds, seventeen completed, plus two 20-second cost worlds. Zero deaths and
the movement owner on every frame of every completed bench world.

**The search is decoupled from the parity harness, and the harness is inert.**
`ParityHorizon`'s four non-harness conditions - dedicated, `sv_cheats`,
`bot_moveRewrite`, `Bot2_IsMovementOnly` - move to `MoveSearchGates` and are
all kept; only `bot_moveParity > 0` leaves the search's path.

| Cell | parity | Completed | Straight / state | Leg decisions | Compared |
| --- | ---: | ---: | ---: | --- | ---: |
| seeded class 125 | 256 | **15,925 ms** | 13 / 5 | - | 667 |
| seeded class 125 | off | **15,925 ms** | 13 / 5 | **13 of 13 identical** | **0** |
| seeded class 75 | 256 | **21,125 ms** | 15 / 7 | - | 905 |
| seeded class 75 | off | **21,125 ms** | 15 / 7 | **15 of 15 identical** | **0** |
| unseeded, level 3,125 | 256 | **19,700 ms** | 12 / 0 | - | 848 |
| unseeded, level 3,125 | off | **19,700 ms** | 12 / 0 | **12 of 12 identical** | **0** |

The zeroes are the harness saying it was never armed: no world reports
`no_search_gates` and every one commits 12 to 15 sequences.

**A tape is bound to the graph it was recorded on.** `Graph_Identity()` is
`g1-` and a 64-bit FNV-1a digest over the cell and edge records as
`Graph_Save` writes them, frozen at map load and printed on `[GRAPH]
map_load`. The map checksum cannot do this job: the 2026-09-16 seed
(`g1-98eb1e97c24a4e21`, 94,711 edges) and the graph built at load
(`g1-301dd39cbead57f6`, 10 edges) share map name and checksum exactly. The
tools write it as the optional trailing header field of a tape and of a library
index, so a tape written before this declares `graph=none` and every earlier
campaign replays unchanged.

| World | Index / tapes declare | Result | Completed |
| --- | --- | --- | ---: |
| unseeded, seeded library | seeded / seeded | **refused at load**, `keys=0`, `library=graph_mismatch` | 18,475 ms |
| unseeded, unbound index | none / seeded | exact key found (`exact=1`), tape refused, census `rung=graph_mismatch entries=0` | 19,700 ms |
| **seeded**, seeded library | seeded / seeded | **`exact` rung, 11 of 11 hit**, `deferred=2` | **15,925 ms**, 13 / 5 |

**Row 5 has a measurement path and a first reading.**
`tools/movement_cost_runner.py`, new beside the archived engine's
`strafe_cost_runner.py`: four bots on one dedicated localhost server, rewrite
with edges, planner, state and the straight search on, parity harness off, no
graph seed, the fixture's 20,000 ms window, 800 recorded frames.

| | p50 | p95 | p99 | worst | frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| movement us per frame, four bots | **67.5** | **36,800.1** | **96,869.9** | **272,691.6** | 25,000 |

**114 of 800 frames exceed the declared 5,000 us budget and 64 exceed the whole
server frame**, spread across the window rather than confined to its start, and
**96 percent of the total is the Pmove-boundary search** rather than command
dispatch. Nothing was tuned toward the budget. A companion world with telemetry
on - timings therefore invalid - shows 1,402 candidate lines and 33 committed
sequences, so the searches are live. One run measures the instrument, not the
row.

**Two things this arm shows that were not asked for.** Unseeded, the graph is
built at load and edges are learned while the bot's own AI plays before the
release, so **neither the start class nor the exact release level fixes an
unseeded trip**: ten worlds pinned to level 3,125 return 16,725, 18,475 or
19,700 ms, and both the 16,725 and the 19,700 outcome appear in both arms.
Unseeded cells need repetitions; a single unseeded world is not a cell, and the
unseeded pair above is one sample from each arm rather than a reproduction.

## Deferred graph save, floor safety and route telemetry, 2026-09-22

[Record](records/2026-09-22-deferred-save-and-floor-safety.md). Build
`.codex-research/safety-save-build-20260922-a` (manifest
`20260922T185245765249Z.sources.json`, sha256 `3cfb6779702e9ea3...`), 1,117
source hashes and both binaries matched before the first world and after the
last. Worlds under `.codex-research/safety-save-20260922-a/`: `R-*` the six
reference cells at defaults, `D-*` the same with `bot_moveGraphSaveDefer=1`,
`S-*` the class-50 respawn4 regressions on the seed and on v2, `F-*` the floor
on v2 with the save deferred, `G-*` the seeded regressions, `C1-*`/`C2-*` the
cost pairs; `progress.jsonl`, `analysis.txt`, `analysis.json`,
`manifest-before.json`, `manifest-after.json`.

| Check | Result |
| --- | --- |
| Reference cells R and D | all six decision-identical to each other and to their reference worlds; 4 of 6 caches byte-identical, 2 seeded differ by a post-leg learn only |
| Class 50, seed, harness on | red_ret and blue_out respawn4 **pass 19,400 ms**, 0 deaths, owner move, 0 legacy lines, 0 mismatches |
| Class 50, v2 | both **pass 16,625 ms**, no hold |
| Floor v2, deferred | **36 of 36**, 0 deaths, 0 legacy, no forbidden spend; 2 one-frame holds |
| Cost p99 off / on | 118,810 / 119,532 us against 63,538 / 74,001 us; dispatch 0.52 / 0.65 s against 0.06 / 0.07 s |
| Route lines | 447 = 290 replans + 157 reroutes, all parsed complete |

## Planner directives, deferred save by default, cache v3, route branching, 2026-09-22

[Record](records/2026-09-22-planner-directives-and-v3.md). Build
`.codex-research/planner-directives-build-20260922-a` (manifest
`20260922T191855538886Z.sources.json`, sha256 `ad5018b2904fb07f...`), 1,117
source hashes and both binaries matched before the first world, between the
collection and the smoke, and after the smoke. Worlds under
`.codex-research/planner-directives-20260922-a/`: `R-*` the six reference
cells at defaults, `Z-v2-c125` with `bot_moveGraphSaveDefer=0`, `T-seed-*` the
four tape replays, `F-*` the floor on v3, `G-*` the seeded regressions, `S-v3-*`
the class-50 respawn4 cases, `P-v3-*` determinism; `progress.jsonl`,
`analysis.txt`, `analysis.json`, `manifest-before/mid/after.json`. Cache v3 and
its manifest under `.codex-research/production-graph-20260922-c/`. Smoke under
`.codex-research/decision-explorer-v3-smoke-c75-20260922-a/`.

| Check | Result |
| --- | --- |
| Reference cells, deferred save default | six of six decision-identical to the safety-save D arm; 4 caches byte-identical, 2 seeded differ by post-leg play |
| `bot_moveGraphSaveDefer=0` | v2 class 125 decision-identical, 19,275 ms |
| Tape replays on the seed | 15,950 / 16,725 / 15,975 / 15,925 ms, every entry hit |
| Cache v3 | 3,332 edges, `g1-9a85d509075ecbc1`, 20 new route edges; seed 51453/48185/41945/59480 = v3 3312/3313/3314/3315 |
| Floor v3 | **36 of 36**, 0 deaths, owner move, 0 legacy lines; G 17,500 / 20,700 ms; S class 50 16,625 / 16,625 ms |
| Determinism v3 | class 125 16,375 ms 4/4, class 75 20,650 ms 4/4, one sequence each |
| Route-branching smoke | 16 worlds; rq815, rq750 and av680 roots confirmed (20,650 / 20,650 / 19,625); best confirmed 17,925 ms |
| Suite | 887 tests OK |

## State planner per-cell strafe-edge lists, 2026-09-22

[Record](records/2026-09-22-planstate-lists.md). Build
`row5-build-20260922-o`; worlds in `.codex-research/planstate-20260922-a/`.
- **Identity:** all six cells are decision-identical to build n. The one
  difference is the known `traces` pair on `red_ret`.
- **Floor:** 36/36.
- **Planner cost:** 1.17-1.44 us per expansion, against 1.75. On the same
  decisions, PlanState time falls from 157 to 125 ms.
- **p99:** not measurably changed; rollouts dominate the heavy frames.

## Search budget 4 by default, 2026-09-22

Owner ruling. Build `row5-build-20260922-p`; worlds in
`.codex-research/budget-default-20260922-a/`.
- **Identity:** with the cvar unset, the six cells are decision-identical to
  budget 0 (build o), apart from the known `traces` pair on `red_ret`.
- **Floor:** 36/36.
- **Budget in use:** at class 75, 11 of 16 searches are served in flight.
- **Row 5:** met under the ruled criterion (p99 inside the frame).

## Rollout replay, 2026-09-22

[Record](records/2026-09-22-rollout-replay.md). Builds
`row5-build-20260922-q/r/s`; worlds in `.codex-research/replay-20260922-{a,b,c}/`.
- **Exactness:** `bot_forecastReplay` (default off) is exact: 0 mismatches in
  24,819 verified steps.
- **Identity:** the six cells are decision-identical in verify and replay
  modes.
- **Gain:** 17-22 percent of steps replay. Rollout time falls about 7 percent
  and Pmove about 13 percent on same-decision worlds; p99 is unchanged.

## Replay on by default, LAA engine, 2026-09-22

Owner ruling. Build `row5-build-20260922-t`; worlds in
`.codex-research/replay-default-20260922-a/`.
- **Identity:** with the defaults, the six cells are decision-identical to
  build p.
- **Floor:** 36/36.
- **Cost:** on same-decision worlds, rollout time falls 11 percent and Pmove
  15 percent.
- **Engine:** the PE header carries the LAA flag.
