# Repair follow-up review, 2026-09-19

Read-only code and retained-evidence review after the four repairs. No new
native campaigns were run and no implementation was changed in this review.
The [original four findings](2026-09-19-review-fixes.md) remain closed by
[their repair record](2026-09-19-review-repairs.md). The two issues below are
additional bounded follow-ups, not a claim that those fixes were absent.
The [refined method](2026-09-19-refined-method.md) remains the substantive
movement work order.

## Evidence checked

- Current `bot2_move.c` and `bot2_route_play.c` match their frozen
  `.codex-research/review-fix-build-d` source bindings at review time.
- All 740 route mark records under `.codex-research/rev23-routes-20260919/`
  name the same recorded and executed command index. Completion logs support
  corrected control drift of 0.346, 0.294 and 0.322 and unchanged tape times.
- The six single-flag stale-slot arms reproduce the repair record's counts:
  repaired all zero; defect reroute 972 dirty / 197 rerouted, tap 648 dirty /
  181 tap-onward, off 784 dirty / neither extension flag.
- The gain-0.30 follower still ends without pickup at 114.205 drift. The
  correction does not qualify that steering law.
- `.codex-research/rev-floor-std-fix-r1/` is 35/36 with one setup failure and
  `green=false`, not a fully passing floor. Other repair-record limits and
  its reported unrelated Python-suite failure remain part of the evidence.

The record's "inside 0.8 units" handoff statement is component-wise:
`1789686390-1-c0-prefix-r3` has along -0.573 and cross 0.784, approximately
0.971 units horizontal displacement. Use the actual components or full norm
when reporting entry accuracy; do not call that a sub-0.8-unit radial bound.

## P2: loading a tape can leave the previous AI hold behind

Entry points: `Load` and the `load` console branch in
[bot2_route_play.c](../../../codemp/game/bot2_route_play.c).

After reading a file, `Load` clears `tape` before parsing, but does not end the
active playback or release its separately tracked `heldClient`. A load while
playing can erase `armed` and strand the old think hook, including when the
new file is rejected during parsing. Explicit stop now recovers it, thanks to
the original repair; loading alone does not.

Repair: either refuse replacement while armed/held, leaving the old state
intact, or end/release the active run before committing replacement state.
Make parsing failure follow the same documented ownership rule. Prefer
transactional parsing where practical; do not strand a client after an invalid
replacement. Test a valid and invalid load during playback, then check armed,
held and think ownership and ordinary continued control. Retain the failed
attempts and use a focused new world; a full movement campaign is not needed
merely to test this transition.

## P2: ownership probe can pass without exercising its phase

Entry points: `phase`, `free` and final verdict in
[route_ownership_probe.py](../../../tools/route_ownership_probe.py).

The final verdict requires only a nonempty phase list, no exception and free
ownership at the end of every phase. That does not establish that arming or
the expected refusal occurred. In the retained
`.codex-research/rev4-fix-own3/ownership.json`, the ordinary `valid` phase has
`arm_response="None"`, `status_after_arm.armed="0"`, the old `finished="1"`
and `released_at="never_held"`, yet passes. The preceding rearm phase does
show a successful arm and stop; this narrows the four-phase coverage claim
without invalidating the refused-arm repair.

Repair: require the expected phase set and state transitions. A valid arm
needs observed ownership for the intended client/new run before stop; a
refusal needs fresh evidence of its expected refusal and correct released
state. Use state/run identity and server logs rather than requiring a single
RCON reply, since replies can be lost. Missing or stale trigger evidence must
be inconclusive/fail, never a passing no-op. Add tests for a lost arm, stale
status, an omitted phase and a real transition. Re-run only the focused probe
in a fresh world, preserving the original result as evidence of this gap.

## Scheduling and claims

These follow-ups are prerequisites to claiming complete tape lifecycle/probe
coverage and to production prepared-plan integration. They can be fixed
independently of the connected-search diagnostic and must not turn the work
order back into a wait for recordings or universal route correction.

The documentation update specifies their fixes but does not implement them.
No new gameplay performance, production readiness or installation is claimed.
