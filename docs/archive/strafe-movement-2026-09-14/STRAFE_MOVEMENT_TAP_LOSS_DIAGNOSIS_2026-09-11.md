# The six tap losses, diagnosed from the retained worlds (2026-09-11)

The diagnosis step 2 of the
[branch implementation plan](STRAFE_MOVEMENT_BRANCH_IMPLEMENTATION_PLAN.md#next-work-in-order)
requires before its repair: determine which caller refused each retained event
and what the launch actually was. Read from the retained candidate arms with no
native run and no source edit; the working tree still matches
`strafe-build-brb3-680c73cce06841b89886aabe6bd6ec63`.

> **Superseded 2026-09-12.** The repair this diagnosis specified is implemented,
> so the tree no longer matches that build — it matches
> `strafe-build-tapfix3-94d7f4640ef74bce82a13f833aecfbf3`. The diagnosis itself
> held: every one of the six events was the commitment, on a branch route, on an
> authored tap chain. What it got right and what the repair then found is in the
> [tap repair record](STRAFE_MOVEMENT_TAP_REPAIR_2026-09-12.md).

## All six events, and they are all the same event

Every `takeoff_flight_resources` release in candidate-r1, one per case, both
directions:

| Case | level | route | branch | index | speed | force | speed_remaining | distance to landing |
|---|---|---|---|---|---|---|---|---|
| R4-blue | 4350 | 27 | **27** | 15 | 306.1 | 9 | **0** | 98.1 |
| R4-red | 4625 | 26 | **26** | 15 | 307.1 | 10 | **0** | 102.3 |
| R5-blue | 3975 | 27 | **27** | 16 | 446.6 | 26 | **650** | 261.0 |
| R5-red | 3950 | 26 | **26** | 15 | 451.7 | 26 | **675** | 80.4 |
| R6b-blue | 7425 | 27 | **27** | 15 | 307.7 | 30 | **0** | 99.4 |
| R6b-red | 7375 | 26 | **26** | 15 | 304.3 | 30 | **0** | 106.3 |

Every one is **on a branch route** — 26 is red BR-A, 27 is blue BR-A — at branch
index 15 or 16, not on the lane. And every launch those branches contain is an
**authored tap chain**. The prepared-group lines are identical in all six
worlds:

```
[FASTRETURN] group route=27 ordinal=0 takeoff=12 landing=16 dist=304 dz=0 class=0 tap=1 gap=0
[FASTRETURN] group route=27 ordinal=1 takeoff=15 landing=19 dist=352 dz=0 class=0 tap=1 gap=0
… ordinals 2..5: dist 290, 352, 352, 277, every one class=0 tap=1 gap=0
```

`BuildReturnGroups` sets `forceClass = tapChain ? 0 : …`
(`ai_bot2_route.c:686`), so `class=0 tap=1` on all six ordinals of both branches
means **BR-A is a tap chain end to end, in both directions**: `tapSafe` holds
across every interval, and none of these launches needs a full flight's worth of
Force Speed.

## Which caller refused, and why it could

Only two sites can release with this reason: the two-sample lookahead
(`ai_bot2_route.c:1339`) and the commitment (`:1360`). The lookahead requires
`ahead->takeoff > state->index`, and at these events the group's takeoff (12 or
15) is at or behind the index (15 or 16), so it cannot have fired. **All six are
the commitment.**

The commitment passes `FastReturnTap(route,state,ps)`, which demands two things
at once (`:800`):

```c
if(dx*dx+dy*dy>24*24)return qfalse;          // the runner is within 24 units of its guide
return (group && group->tapChain)?qtrue:qfalse;
```

and the ladder then reads (`:863`):

```c
float launch;int lowJump=tap || (state && state->jumpLow);
if(ResumeRunupTakeoff(...)>=0)return "takeoff_momentum";
if(tap){if(state)state->jumpLow=1;}
else if(speedMs<2500)return "takeoff_flight_resources";
```

So a launch that **is** an authored tap chain is judged as a full flight
whenever the runner happens to be more than 24 units from its own steering
guide on the frame the kernel asks for the jump — and `lowJump`, which the two
rules below it respect, is computed and then ignored by this one. That is the
whole mechanism: `tapChain=1` and `class=0` are measured at every one of these
events, the reason string is a flight-resource reason, and the only term that
can flip between them is the 24-unit offset.

It is not a zero-Force failure and it is not a short-hop misjudgement. It costs
the branch at 9, 10, 26 and 30 Force, with 0, 650 and 675 ms of Speed left, on
launches the route itself declares tappable.

## Two published figures this corrects

- **The refused launch is not a 76-108 unit hop.** Those numbers are
  `landing_xy` in the release record, which is the distance from the runner's
  *current origin* to the landing (`:736`), not the length of the hop. The hops
  themselves span **277 to 352 units**, from the prepared-group lines above.
  The runner was 80 to 106 units short of a landing it was still running toward.
- **No retained telemetry line names the launch that was judged.** While the
  runner is on a branch, the three records disagree about which route they
  describe: `[MOMENTUM]` prints `f->route` (the lane) with `f->steering.index`
  (the branch's), `[FASTRETURN] admission` prints `f->route` while computing
  its group, takeoff, landing and class from the branch, and
  `[FASTRETURN] release` uses the current guide for both. The R5-blue admission
  line reading `route=4 … class=0` is the lane's id beside the branch's class.
  Any native validation of the repair has to fix this first, or it will not be
  readable.

## What the repair has to separate, and what it must not do

The plan's bound on this repair holds, and the evidence sharpens it:

- The question "does this launch need a full flight's resources" is a property
  of the **prepared launch** — `tapChain`, checked with `tapSafe` across the
  whole interval from the takeoff. The question "can this frame's jump be
  executed as a low tap along the authored line" is a property of the
  **runner's alignment**. One test currently answers both, and the resource
  ladder reads the wrong one.
- Do **not** widen the 24-unit envelope and do not bypass geometry on a flag:
  the envelope is what keeps a tap from being authored into a turn, and the
  500-unit fall of the first BR-A tap is what bought that rule.
- `state->jumpLow` is not trustworthy as an input here: it is set by the
  ladder itself and by the momentum tap (`:1383`), and the plan requires stale
  low-jump state to be rejected rather than believed.
- A refusal for **alignment** is not a refusal for **resources**, and releasing
  the whole lane is the wrong recovery for it. Whatever the repair does with the
  jump, the release reason must name what actually failed.
- `FastReturnGroupClass` remains telemetry. Changing it cannot fix admission,
  and its `class=0` is now known to be ambiguous between "this frame is a tap"
  and "this launch is a tap chain" — the repair should split that too.

Native validation needs a fresh unique build, the same assets, the six cases in
both directions, boundary cases at Speed expiry and around the 24-unit
threshold, negative unsafe/off-guide and held-flight cases, and the 26-body and
30-resource regressions afterwards. None of that is done here.

## Evidence

The six release lines, the prepared-group lines and the R5-blue frame sequence
are in `.codex-research/pkgD-step4-20260911/candidate-r1/R*/server.log`, which
is the collection's own retained output. Commands that reproduce the extraction:

```bash
grep -h "release.*reason=takeoff_flight_resources" .codex-research/pkgD-step4-20260911/candidate-r1/R*/server.log
grep -h "FASTRETURN] group route=2[67]" .codex-research/pkgD-step4-20260911/candidate-r1/R5-blue/server.log | sort -u
```
