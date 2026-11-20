# Bot2 movement rewrite

Started 2026-09-14. This folder replaces the strafe-movement document set that
is now frozen under [docs/archive/strafe-movement-2026-09-14](../archive/strafe-movement-2026-09-14/README.md).
Nothing in the archive is a work order. What survives from it is written down
in [LESSONS.md](LESSONS.md); everything else is history.

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
   state. See [PHYSICS.md](PHYSICS.md).
2. **Independent movement is the base; prepared plans are optional.** Generate
   map connectivity from collision geometry, with the navmesh as a hint. Human
   demonstrations and offline search may supply prepared plans, but their
   absence must not prevent useful independent travel. See
   [ARCHITECTURE.md](ARCHITECTURE.md).
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

- [HANDOFF.md](HANDOFF.md): **resume here.** Current state, open items, and what is closed.
- [PLAN.md](PLAN.md): roadmap with status, the gates, comparison rules, map preparation.
- [MILESTONE_A.md](MILESTONE_A.md), [MILESTONE_B.md](MILESTONE_B.md): the offense and 4v4 milestones.
- [ARCHITECTURE.md](ARCHITECTURE.md): graph, planner, executor, recovery, state and telemetry.
- [PHYSICS.md](PHYSICS.md): the movement law and the inputs that vary it.
- [LESSONS.md](LESSONS.md): measured facts and failure modes carried forward.
- [EVIDENCE.md](EVIDENCE.md) and `records/`: dated evidence, one record per step.
- [HUMAN_ROUTES.md](HUMAN_ROUTES.md): recording human flag runs for comparison.
- History: the plan, handoff and status narrative as of 2026-10-05 are in
  [archive/movement-2026-10-05](../archive/movement-2026-10-05/README.md).
