# Owner concerns and review expectations

Recorded 2026-09-13 from the owner's conversation. These are reported problems
and desired behavior, not a claim that every problem remains unimplemented in
the current working tree. Consult the current handoff and scorecard for measured
repair status. This record does not restart completed experiments.

## Problems raised

1. **Pickup wastes incoming momentum.** The bot approaches strongly, then
   hesitates or returns on foot. The reported route-5 log falls from 889.9 to
   428.9 u/s. With Force Speed ground pace at approximately 425 u/s, the latter
   is effectively ground-running speed, not successful preservation of the entry.
   The endpoint log alone does not locate the intervening loss.
2. **Return selection must reflect actual pickup state.** Outbound spawn routes
   start predictably; returns begin at varying positions and velocities, with
   less Force, a running Speed timer and closer opposition. Strong entries should
   connect to useful exits instead of requiring a fresh stationary decision.
3. **Force-heavy routes need affordable exits.** Before an unaffordable jump,
   choose a checked low-/zero-Force branch connected to the main lane. Rejoining
   only to reject the next maneuver again is not useful progress.
4. **Two depleted states need distinct behavior.** With no Force but retained
   speed, preserve momentum through feasible ordinary jumps and turns. With no
   Force and lost speed, deliberately rebuild speed and continue. Walking home
   indefinitely is insufficient in a defended match.
5. **Fallback cornering is itself a blocker.** Excessive braking and hesitation
   at corners undermine both momentum preservation and acceleration. Assess
   steering, clearance, path precision and hop admission; low navmesh resolution
   was a question, not an established cause.
6. **Jumppad exits need directional control.** Repeated vertical bouncing may
   require sustained directional input to land, rather than treating the pad as
   an unexplained trap. Earlier Bot2 behavior is a useful reference.
7. **Readiness must reflect sustained useful movement.** Captures, absence of
   long stalls and a fast median do not establish ordinary-hop competence.
   Report Force Speed usage, momentum loss, airborne travel, corner behavior and
   depleted performance separately. The owner specifically questioned how much
   of the roughly 12.1-second return depended on Force Speed.

## Direction and limits

The owner wants an 80/20 path to a competent in-game review candidate, with the
agent taking responsibility for prioritization. Preserve effective incoming
movement and focus on the largest visible failures. Existing pressed-runner
melee is relevant to blocking; this request does not authorize new combat scope.

Earlier brainstorming suggested using unavoidable delay to prepare a route,
continuing useful movement while planning, and distinguishing unavoidable
enemy-induced falls from preventable departures. Those suggestions were explicitly
brainstorming, not approval to weaken safety gates or force arbitrary movement.

## Instructions for the next implementer

1. Resume the [current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md) and its
   frozen evidence before changing code. Preserve in-progress collection and
   unrelated changes. Finish the current flight-safety verification, then the
   supported walking correction in the immediate work order.
2. Reconcile these concerns against existing branch, join and corner repairs.
   Target remaining failures; do not rebuild already implemented alternatives
   merely because the original complaint predates them.
3. For remaining return defects, trace pickup through the first landing, corner
   and next launch. Record owner, commands, contacts, entry/minimum/exit speed,
   Force and remaining Speed. Separate the point of momentum loss from the later
   resource refusal. Check actual next-maneuver feasibility before joining.
4. Verify both teams, fast and slow entries, low/zero Force and Speed active,
   expiring and inactive. Show affordable continuation and acceleration after
   depletion, plus useful corner exits and recovery. Preserve failed and censored
   trips and existing native resource/floor/body checks.
5. Report implementation, measured behavior, qualification and installation
   separately. Invite a playtest only with concrete evidence of improvement and
   a short list of remaining weaknesses. T5/T6/K07 retain their existing gates.

No code change, build, installation or commit is part of recording this handoff.
