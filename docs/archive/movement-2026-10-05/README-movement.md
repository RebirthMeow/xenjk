# Bot2 movement rewrite

Started 2026-09-14. This folder replaces the strafe-movement document set that
is now frozen under [docs/archive/strafe-movement-2026-09-14](../strafe-movement-2026-09-14/README.md).
Nothing in the archive is a work order. What survives from it is written down
in [LESSONS.md](../../movement/LESSONS.md); everything else is history.

## Why a rewrite

The snapshot build has two movement engines under one think loop.

- The runner stack is seven controllers competing for each command frame with
  a release-and-retry protocol between them. Most of its logic is a correct
  local fix for one symptom seen in one dated campaign. Behavior is the union
  of exceptions, not a model, and its speed comes from hand-recorded routes and
  hand-declared branch connectors for one map.
- The legacy Bot2 machine has a real model, the game's own Pmove run as a
  phantom, but searches a tiny policy: two strafe directions, one angle law,
  one key pair held for a whole flight, no Force as a movement resource.

The project goal has not changed: a dependable, intermediate-human offense
bot that transfers to other maps through automatic preparation, with no
map-specific C, no hand-authored rescue coordinates and no growing list of
corner exceptions. Neither engine was converging on that.

## Principles

1. **One physics, variable inputs.** Strafe movement is one law. Run speed,
   Force Speed and Force Jump are inputs to that law, not separate modes. The
   controller never asks which "level" it is in; it reads the live player
   state. See [PHYSICS.md](../../movement/PHYSICS.md).
2. **Independent movement is the base; prepared plans are optional.** Generate
   map connectivity from collision geometry, with the navmesh as a hint. Human
   demonstrations and offline search may supply prepared plans, but their
   absence must not prevent useful independent travel. See
   [ARCHITECTURE.md](../../movement/ARCHITECTURE.md).
3. **One movement owner, connected plans.** Carry actual simulated successor
   states through maneuver sequences. Execute a viable committed maneuver;
   when it becomes unusable, recover from the actual state, including during
   flight. Prepared-plan entry and independent movement share this lifecycle.
   Landing-only search is a current limitation, not a permanent design rule.
4. **Validate execution from the actual state.** Native Pmove rollouts validate
   the modeled continuation; graph costs and geometric proximity do not prove
   a jump or join. A join includes the connecting maneuver and ensuing route
   continuation. The forecast's static-world limits remain explicit; it cannot
   guarantee safety against future interference.
5. **One state, one reset.** Per-client movement state is a single struct with
   one reset path.
6. **Benchmarks gate rules.** No rule is added unless it fixes a named case in
   the movement benchmark and regresses none. Aggregate campaigns inform which
   case to add; they do not justify a rule by themselves.
7. **Nothing is guarded in advance.** No admission dimension, contract field,
   planner state field or executor branch is added without a spike result or
   benchmark case that failed without it. Structural rules (one executor, one
   state, one event schema, unknown edges stay unknown, no new owner) are
   fixed here; everything case-shaped must earn its place. This is the rule
   the old runner never had, and its absence is how seven owners happened.

## The three capability levels

The owner's framing, which the design adopts:

| Level | Run speed | Force Jump spend | What changes |
|---|---|---|---|
| 1 | base (250) | none, taps only | the reference case |
| 2 | Force Speed (425) | none, taps only | same law, higher wishspeed, faster air gain, longer flights |
| 3 | Force Speed | held jumps allowed | adds a continuous resource decision per launch |

Levels 1 and 2 are the same controller with one number changed. Level 3 is a
planning extension: how much Force to spend and where. The old runner was
authored at level 3 and could not step down; the old legacy bot could only
do level 2. The rewrite must run any level from the same code, and a plan
must degrade from 3 to 2 to 1 as resources run out without changing owner.

## Documents

- [HUMAN_ROUTES.md](../../movement/HUMAN_ROUTES.md): record human flag runs in an isolated
  practice session for comparison with the search's routes.
- [PHYSICS.md](../../movement/PHYSICS.md): the movement law as the engine implements it, the
  inputs that vary it, and what each level implies.
- [ARCHITECTURE.md](../../movement/ARCHITECTURE.md): the maneuver graph, planner, executor,
  recovery, state and telemetry; current implementation and target boundaries.
- [HANDOFF.md](../../movement/HANDOFF.md): current executable work and its qualification limits.
- [Refined method](../../movement/records/2026-09-19-refined-method.md): the owner's independent
  search / optional prepared-plan direction and acceptance sequence.
- [PLAN.md](../../movement/PLAN.md): phases, the benchmark gate and acceptance, a status per
  phase and the open list.
- [EVIDENCE.md](../../movement/EVIDENCE.md): each landed step's method, defects found and
  measured evidence, by phase.
- [LESSONS.md](../../movement/LESSONS.md): measured facts and failure modes carried forward.

## Status

**Resume here:** [HANDOFF.md](../../movement/HANDOFF.md). The work order since
2026-09-27 is the [roadmap](../../movement/PLAN.md#roadmap-owner-2026-09-27): wide map
compliance on a frozen map list, a demo pilot of CTF positions, one
end-to-end offense milestone (a defended pickup and escape), chase and base
milestones, and level 3 last. The paragraphs below are the
level-1 history. The
[four review findings](../../movement/records/2026-09-19-review-fixes.md) - candidate result
reset, first-command duration, checkpoint alignment and refused-arm ownership -
are repaired and measured in
[the repair record](../../movement/records/2026-09-19-review-repairs.md), each with a
regression shown to fail against the defect first. The subsequent
[repair follow-up review](../../movement/records/2026-09-19-repair-follow-up.md) records two
bounded lifecycle/probe issues without reopening those four fixes.

The [guided expressiveness diagnostic](../../movement/records/2026-09-19-guided-corridor.md)
is done: guided onto a human route's own line the search takes 20.5-20.7 s
against the human's 14.8, unguided 19.2-19.6, so the corridor is not the
limit. Its refusal classification was corrected after review - a rollout
stopped on trigger contact is not a failed landing - and the losses divide
into a correct refusal of flights that fall into the pit, a selected-path
rejection of clean flights, and rollouts stopped on an item or the objective
itself. The [September 20 landing-search investigation](../../movement/records/2026-09-20-landing-search.md)
then identifies all four missing-search states and repairs stale path progress
at final touchdown. The floor remains 36/36 green, but the eight-pair corrected
median is 20.16 s versus 19.38 s baseline: that repair alone is not a speed win.
The [next campaign](../../movement/records/2026-09-20-speed-win.md) permits short-landing
proposals and measures validated rerouting with exit-speed ranking: eight fresh
pairs improve median **18.89 → 17.90 s** and mean **19.35 → 18.49 s**, with
six faster and two slower pairs. The candidate floor and named regressions
pass. [Actual-landing successor proposals](../../movement/records/2026-09-20-successor-aim.md)
then improve a fresh matched mean **18.84 → 17.94 s** and median
**18.51 → 18.125 s**, with a 16.45-second outcome repeated three times.
The faster construction costs more search work; both floors pass, parity and
production remain unqualified. On 2026-09-21 the owner replaced the
open-ended speed target with the
[level-1 exit gate](PLAN.md#level-1-exit-gate-2026-09-21): five rows
(safety, speed as a distribution, the waiting-frame mechanism, coverage of
every level-1 case, runtime cost) reported together on one build, with 15 s
kept as the expert reference rather than the gate. Prepared plans found
offline are promoted to the level-1 mechanism, and the first work item is the
read-only [plan-coverage diagnostic](PLAN.md#the-plan-coverage-diagnostic).
Current search is still qualification-harness gated;
production enablement must be separated from parity diagnostics.

Historical milestones (use the current plan above for next actions):
phases 0, 1 and 1b landed 2026-09-15: `bot2_move.c` (one state, one reset,
walk executor, parity harness) behind `bot_moveRewrite`, the benchmark
fixture under `tools/fixtures/movement/kejim` with `tools/movement_bench.py`,
discovery of hop sequences by rollout from the live state with the same step
function the forecast runs, and a forecast that models the frame's Force
update. Phase 2 landed in steps over 2026-09-15 and 16: `bot2_graph.c`
flood-fills walkable floor from the BSP at map load, the planner searches it
by time and validates every planned edge from the live state, and straight
hops are searched at every landing and chained. The floor is 36 of 36 with
zero parity mismatches and the rewrite owning every frame, 6.8 s under the
snapshot baseline on average. Phase 3 landed the Speed edge and, at level
1, the state-space planner: straight hops learned as strafe edges with
their state, a search over cell and state, and planned hops validated by
the straight search at their takeoff. The snapshot build is otherwise
unchanged in `codemp/game`. `tools/movement_campaign_compare.py` prints a
campaign's standard comparison against named arms;
`tools/movement_graph_merge.py` unions the graph caches of a campaign's
worlds into a seed for the next.

The 2026-09-16 review of the subsequent exploration work is tracked in
[PLAN.md](PLAN.md#review-and-fastest-time-target-2026-09-16). The owner has
confirmed a sub-15-second stationary flag-to-flag tap-only run; current
exploration does not establish fastest-time convergence. See the
[search investigation](../../movement/records/2026-09-16-search-review.md) for the measured
gap between abstract graph costs and executed runs.

The 2026-09-17 [replay correction](../../movement/records/2026-09-17-replay-correction.md)
supersedes the original record's timing split and causal conclusions.
Stored headings rescue candidates on blue and the small red seed, while
earlier B results were censored by the control's reference horizon. The
investigation remains open; a matched local rescue is not proof of a faster
route. That investigation called for paired successor trials and prospective
merge-input capture. Discarded merge records cannot be recovered
from old seeds, and observed exit spread alone cannot prove causation.
The independent no-Force sub-15-second target remains unmet; prepared tape
times are a separate result and may use different starts/physics.

The [independent-reference follow-up](../../movement/records/2026-09-17-independent-reference.md)
fixes offered-mode horizon coupling and adds mode-aware telemetry and onward
travel/progress measurements. It supersedes the offer record's shared-reference
and monotonic-ranking claims. The offer remains default off.
