# Plan review checks: can v3 carry the seeded trips, and can the bot choose its phase?

Recorded 2026-09-22. These are the two cheap checks the owner asked for after
a review of [PLAN.md](../PLAN.md), before the eight-class explorer chain on
cache v3 ([planner directives and v3](2026-09-22-planner-directives-and-v3.md)).
Both are native runs on `red_out_L1_stand`. Worlds ran one at a time, with
the explorer's settings (`BASE` in `tools/movement_decision_explore.py`), and
nothing is installed.

> **1. Cache v3 does not carry the seeded fast trips whole.** Each seeded
> explorer tape was remapped onto v3 and run at its own class.
>
> | Class | Seeded trip (seed graph) | Tape on v3 | Hits | First miss |
> | ---: | ---: | ---: | ---: | --- |
> | 75 | 15,950 | **17,250** | 6 of 11 | entry 5, absent straight |
> | 100 | 16,725 | **16,950** | 7 of 11 | entry 6, absent straight |
> | 125 | 15,925 | **16,375** | 5 of 11 | entry 4, absent straight |
> | 150 | 15,975 | **17,000** | 4 of 10 | entry 4, absent straight |
>
> - Row-2 arithmetic over the four: equal-weight median **16,975 ms**
>   (bound 16,500) and tail **17,250** (bound 18,000).
> - At class 125, v3's own ranking already gives 16,375, so there the tape
>   buys nothing.
> - Requiring 3313 or 3314 on the release plan changes no time and no
>   census, at any class.
> - Every first miss is a straight candidate the search did not offer. The
>   tape's landing cells are not on the path the v3 planner installs, so the
>   miss is the planner-reach limit of
>   [seed deaths and planner reach](2026-09-22-seed-deaths-and-planner-reach.md) (B).
>   It is not a refusal.
>
> **2. The bot cannot buy the class-125 trip by waiting for its phase.**
> A default-off hold, `bot_moveStartPhase`, stood the released bot still
> until `level.time % 200` reached 125, then let it move. Held that way,
> every class completes in **19,175 to 19,350 ms**, deterministic in both
> repetitions:
>
> | Class | Time | Wait |
> | ---: | ---: | ---: |
> | 75 | 19,200 | 50 |
> | 100 | 19,175 | 25 |
> | 125 | 19,350 | 200 |
> | 150 | 19,325 | 175 |
>
> The unheld class-125 release gives **16,375**. So what makes a class fast
> is tied to the fixture's release. It is not the phase of the bot's own
> first move. The option is refuted as a row-2 mechanism. The switch and its
> code are removed, and the sources are byte-identical again to the
> planner-directives build.

## Check 1: seeded tapes on v3

**Method.**
- `.codex-research/v3-feasibility-20260922-a/remap.py` rewrites the four seeded
  tapes onto v3:
  - `c75-best`, `c100-best` and `c150-best` from the explorer campaigns;
  - `c125-self` from the decision-tape verification.
- Cells are identical in every cache of the map, because
  `movement_graph_merge` refuses otherwise. Each state edge was resolved
  through the fold identity that the explorer's `--reference-tape` uses. All
  18 resolved and none was missing; `remap.json` holds the mapping. The
  header is bound to `graph=g1-9a85d509075ecbc1`.
- Straight entries were copied unchanged.
- Per class, three arms on build `planner-directives-build-20260922-a`, with
  v3 passed as `--graph`:
  - the tape alone;
  - the tape plus `bot_movePlanRequire=3313 bot_movePlanGen=1`;
  - the tape plus `bot_movePlanRequire=3314 bot_movePlanGen=1`.

**Result.** The table above; `analysis.json` beside the worlds.
- Every world completed with owner `move`, zero deaths, zero parity
  mismatches and zero refusals.
- Two arms missed the class pin on all four attempts:
  - class-125 tape: rerun as `c125-tape-a2`, which gave 16,375;
  - class-100 require-3313: missed again on the rerun and is not replaced,
    because the other two class-100 arms agree.
- The missed worlds are retained.

**Reading.** The explorer on v3 starts from these numbers. It is not bound
by them, because it can find decisions the seeded tapes never took. But the
seeded decisions are not available on v3 past the fourth to sixth commit,
and the route directive does not restore them. The class-75 smoke's best on
v3 was 17,925, and v1's four-class explorer median was 17,387.5. Nothing
measured on v3 so far puts two declared classes near or under 16,500, and
the row-2 median needs that.

## Check 2: the start-phase hold

**Method.**
- The hold was armed at the corner release, after the tape key.
- While holding, the bot owned each frame and issued no motion.
- It released at the first frame on phase 125, and dropped the hold if the
  bot was airborne or moving.
- Build `start-phase-build-20260922-a` zeroed the whole command. That
  snapped the view to `delta_angles`, which differs by world, so identical
  worlds diverged: 17,925 against 18,800 at class 75, with the same release
  level.
- Build `start-phase-build-20260922-b` zeroes only the motion fields, as the
  exploration hold does, and is deterministic.
- Worlds:
  - a control at classes 125 and 75: **16,375** and **20,650**, which
    reproduce the v3 record;
  - two repetitions of the hold at each declared class.
- Both builds and all 20 worlds are retained:
  - `.codex-research/start-phase-20260922-a/`, the defective hold;
  - `.codex-research/start-phase-20260922-b/`.

**What the release carries.** Holding the class-125 release to its next
phase-125 frame costs 200 ms of standing, and the trip then takes 19,350
rather than 16,575. The fixture's release is anchored to the Force-regen
debounce, the cleared state and the first plan's timing. At least one of
those decides the leg as much as the phase does. It is not located here.

**Disposition.** Rejected. Under principle 7 the cvar and its code are
removed rather than parked: `Move_StartHoldArm`, `StartHold`, the call in
`bot2_corner.c` and the hook in `Move_Command`. Afterwards,
`verify_sources.py` against the v3 chain's `manifest-before.json` reports
`mismatched: []`.

## What this does not establish

- **That the v3 explorer cannot reach row 2.** It explores decisions the
  seeded tapes did not take. What check 1 bounds is the seeded decisions.
- **That no phase choice helps.** Only a hold to phase 125 was measured. A
  hold to some other phase might land on a faster trip from some class. The
  mechanism the plan hoped for, "wait into class 125's trip", is what is
  refuted.
- **Why the release decides the leg.** The candidates are named in check 2
  but not measured.

## Reproduce

```
python .codex-research/v3-feasibility-20260922-a/remap.py
# detached: .codex-research/v3-feasibility-20260922-a/run.sh, then rerun.sh
python .codex-research/v3-feasibility-20260922-a/analyse.py
# start-phase: .codex-research/start-phase-20260922-b/run.sh on build b; the code is removed from the tree
```
