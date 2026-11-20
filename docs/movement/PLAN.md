# Plan, gates and acceptance

The current state and next action are in [HANDOFF.md](HANDOFF.md). This file holds the roadmap, the gates and the
comparison rules. The full plan as it stood on 2026-10-05, with every dated work order and the status log, is
archived at [archive/movement-2026-10-05/PLAN.md](../archive/movement-2026-10-05/PLAN.md); per-step evidence is in
[EVIDENCE.md](EVIDENCE.md) and `records/`.

## Roadmap (owner, 2026-09-27)

| Step | Status |
| --- | --- |
| Phases 0-3: harness, discovery, graph and planner, Speed edge, state planner at level 1 | Done 2026-09-15..24 |
| 1. Wide map compliance: 14 frozen maps, automatic preparation, no navmesh | Done 2026-09-27: native edges by default, mesh not loaded, 828/828 trips on the eight new maps ([record](records/2026-09-27-eight-maps.md)) |
| 2. Demo position pilot | Closed 2026-09-27 ([record](records/2026-09-27-position-pilot.md)) |
| 3. Milestone A: lone runner takes a defended flag and escapes | Passed 2026-10-02 ([MILESTONE_A.md](MILESTONE_A.md)) |
| 4. Milestone B: full 4v4 (offense, camper, base, chase a team) | B1, B2 done; B3 runs on Kejim and Dantooine v3 ([MILESTONE_B.md](MILESTONE_B.md)). Open: frame cost |
| 5. Level 3: held jumps | Not started; last by design (one more bidder for the Force pool) |
| Phase 5: retire legacy owners | Not started. Gated on phase 4, which step 1 met |

Map list (additions need the owner): kejim, dantooine_v3, ctf4, taloraan, carratos, metalorn_v2, talay, illimiran_v2,
telti, nelvaan, talravin, manaan, kharzet_v3, kothis_v5.

**Design rule (milestone A, fixed):** the bot's situation model separates *known* (perceived this frame), *last seen*
(with its age) and *unknown*; policy reads the model, never server state directly. What a human gets from the HUD or
console (flag status, kill feed) counts as known.

**Known gap, deferred (owner, 2026-09-27): drowning.** The forecast accepts submerged movement but models neither air
expiry nor drowning damage. When it matters, keep shallow water supported and model or refuse the drowning interval.

## Gates

Both gates are Kejim, taken by the harness in `.codex-research/gate-<date>/`: copy the last one and rebind the build
(about 30 min). Its level-1 library must be the one explored at the current strafe skill (90 since 2026-09-26)
and defaults: `reexplore-chain-20261014-b` since 2026-10-14 (owner; it met rows 2 and 4 where `skill90-chain-20260926-a`
missed row 2); the older `slowdrop-chain` library makes row 2 fail on any build. Last met: 2026-09-26
(`gate-20260926-c`); on the 2026-10-05 defaults see HANDOFF. **Owner, 2026-10-06:** the gates are taken only when
the owner asks; a change is verified by the paired arm that decided it and the cheapest check of what it risks.

**Level 1** (no Force spend, taps only):

1. Safety: floor 36/36 in the production configuration, zero deaths, owner `move` on every frame, no forbidden
   spend; a parity-harness arm with non-zero comparisons and zero mismatches, decisions identical to harness off.
2. Speed: `red_out_L1_stand` at classes 75/100/125/150 (`--corner-class`), six repetitions a class: equal-weight
   median at or under 16,500 ms, tail at or under 18,000 ms. The owner's 14.8 s run is the reference, not a gate.
3. Mechanism: at no class may the library world wait a larger grounded share than its control
   (`tools/movement_leg_budget.py`).
4. Coverage: every level-1 case timed, median and tail over three repetitions.
5. Cost: one bot, p99 inside the 25 ms frame.

**Level 2** (Force 50 with the Speed purchase): pre-gate floors on Kejim, Dantooine v3 and ctf4; rows 1, 3, 4 as
level 1 on the level-2 cases; speed row `red_out_L2_stand` at or under 13,000 ms at every class (reference 9,000).

The full row text and history: [level-1 gate](../archive/movement-2026-10-05/PLAN.md#level-1-exit-gate-2026-09-21),
[level-2 gate](../archive/movement-2026-10-05/PLAN.md#level-2-exit-gate-2026-09-24).

## Benchmark rule

Safety is binary: no green case may turn red (death, a stall past the case's bound, a forbidden spend). A rule is
added only when it fixes a named case and regresses none; aggregate campaigns find cases, they never accept rules.
Fixtures: `tools/fixtures/movement/<map>/cases.json`, run by `tools/movement_bench.py`.

### Cheap comparisons (owner order 2026-09-25)

Verification must not be the bulk of the work. A comparison of two settings or builds is, by default:

1. **Pinned, deferred:** `--corner-class 50` and `150`, one world per case, four at a time, paired per case and class.
2. **Twins skipped:** `--skip-twins` on both arms (return legs repeat their outbound twin except Kejim's stands and
   Taloraan's L3 respawns).
3. **Control reused** when the change cannot touch the control's path: run one map-class of the control on the new
   build, require every shared pair identical, then pair against the retained control. Any difference: re-run it.
4. **Scoped to the path:** only the maps and levels the change can reach; all of them for a default change.

Gates and floors keep every case, both twins, and unpinned starts where the gate says so; a default change is decided
by its paired comparison, not by a gate re-take (owner, 2026-10-06).

**Play outcomes** (defenders, 2v2, 4v4): `tools/defense_laps.py --rate 25`, paired `--seed N` worlds, at most four
worlds at once; `tools/laps_compare.py` scores them, `tools/laps_campaign.py` runs arms in rounds until decided.
Dantooine seeds replay exactly (`tools/laps_identity.py`); Kejim's do not across runs.

## Map preparation (owner, 2026-09-27)

For a new map: (1) cases from the BSP (`tools/movement_cases_from_bsp.py`, heights checked with `--graph-world`);
(2) level-1 and level-2 floors at two pinned classes, `--linger 4`; (3) a prepared seed from those worlds
(`tools/movement_graph_prepare.py <campaigns> --map <map> --out <seed dir>/<map>_<checksum>.txt`): the built graph
minus pads, drops, jumps and climbs that failed and never succeeded; (4) a third class seeded against unseeded, which
must lose no case. Seeds live in `.codex-research/mapprep-20260927-a/seeds/` and are bound to the graph build: rebuild
them when candidate discovery changes.
