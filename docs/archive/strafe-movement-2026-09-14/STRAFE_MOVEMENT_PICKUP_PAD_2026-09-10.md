# Pickup continuity and jumppad repair

Scope: the owner's first human match exposed a pickup pause and a fixed-XY
jumppad bounce. This is a bounded playable repair, not full K07 qualification.
The original capture remains in
`.codex-research/strafe-human-play-20260910-031010/`.

## Pickup implementation

Under-resourced carriers now decline the optional full-Speed cached stand
entry before it changes their movement. The full guide's admission thresholds
are preserved, including the later route-selection admission path. Supported
returns execute their first validated intervals while preparation continues,
using the same append-only path and follower. They retain ordinary stopped
turns instead of converting a moving raw path into curved indices. Native
floor, hull, headroom and execution-obstruction checks remain.

This removes waiting for a complete preparation or resource refill at the
contested stand. It does not precompute a return before pickup, make a
resource-poor return as fast as the baked incoming route, or establish Q3's
walking gate.

Focused extracted-C checks:
- `.codex-research/supported-return-check-20260909-a/result.json`
- `.codex-research/return-entry-check-20260909-a/result.json`

## First native build and retained failures

Build: `.codex-research/strafe-build-pickup-pad-20260910-a`.
Natural red/blue worlds in `pickup-pad-20260910-a` each complete four
captures with zero deaths. Across nine observed pickups, no slow zero-input
sample appears in the first three seconds; eight completed returns capture
and the final red return is censored. Return preparation starts 25–50 ms
after full-resource pickup, 625 ms after one airborne 12-Force pickup.
This is an undefended 5x comparison, not a matched human-defense benchmark.
Sampled return median speed is 250–268 u/s; returns take 24–26.5 seconds.
See `.codex-research/pickup-pad-review-20260910/pickup-comparison.json`.

The two low-resource native corner returns (26 Force, 8 seconds active Speed)
in `pickup-pad-20260910-c` each complete three captures without death.
The existing 30-case resource matrix passes:
`.codex-research/pickup-pad-resources-20260910-a-report/summary.json`.

The initial pad setup at Z=1376 is rejected for solid overlap. Z=1376.125
is admitted; both carrying states then engage horizontal pad steering but
miss the diagonal landing endpoint and die. Retain those failures in
`pickup-pad-20260910-c/pad-0` and `pad-1`.
The temporary-body regression retains 20/26 successes; six respawn routes
are interrupted by the newly prioritized pad owner. A narrower pad repair
must preserve active cached routes and choose a closer supported receiver.

The `pickup-pad-20260910-b` launch attempt used a nonexistent build path;
all four runners refuse before native collection. A subsequently corrected
orchestrator syntax error also launches no world. Neither is gameplay evidence.

## Corrected build

Final native source build: `.codex-research/strafe-build-pickup-pad-20260910-c`.
The intermediate `-b` build launched no worlds: review caught a late preparation
failure that could replace an already-moving return with raw vertex 1. The final
build instead releases that failed preparation, preserving incoming commands.
The extracted production rejection regression passes at
`.codex-research/supported-return-check-20260910-b/result.json`.

Pad steering runs before ground recovery and navigation, preserving an already
active cached route's recorded pad commands. On entry it can replace a diagonal
offmesh receiver with the nearer of two axis-projected candidates, each checked
with the native hull against world floor at the receiver elevation. The target
stays locked through the arc. This is bounded landing selection, not a complete
trajectory proof. The expanded mock passes at
`.codex-research/jumppad-dispatch-check-20260910-c/result.json`.

Both corrected pad cases run at normal speed in
`.codex-research/pickup-pad-20260910-final/`. Without the flag, the first sampled
upper-floor landing occurs at 2.175 seconds and 368.5 units horizontal departure;
carrying, at 2.025 seconds and 362.7 units. Both land 432 units above the pad,
resume supported control, survive over 28 seconds, and shut down cleanly. The
carrier also captures. No repeated pad entries or falling deaths occur. Exact
review: `.codex-research/jumppad-native-review-20260910/final-review.json`.

All 26 temporary-body regressions pass on the corrected build:
`.codex-research/pickup-pad-body-20260910-c/summary.json`.

The final 30-case resource matrix also passes all existing report gates:
`.codex-research/pickup-pad-resources-20260910-c-report/summary.json`.
Both normal MP listen worlds with one opponent complete 120 seconds at 1x:
`.codex-research/pickup-pad-listen-20260910-c/{red,blue}/`. Red scores two
captures with zero deaths; blue scores one with three deaths. Idle fractions
are 2.5%/3.4%, walking 45.3%/28.3%; longest sampled idle is 1.5/1.25 seconds.
Neither has audited confinement or distant stalls. Both shut down cleanly.
Audit: `.codex-research/pickup-pad-review-20260910/listen-audit.json`.
These are focused outcomes, not the full Q3 comparison. No new timing claim.
The final normal-speed pickup review covers seven pickups: no recurring stand
resource wait. Two isolated slow samples occur after departure on descending
corridors and resume within 250–500 ms. Captures include a 16-Force red return
and a 5-Force blue return; two carriers die from weapons and two ending returns
are censored. See `pickup-pad-review-20260910/final-listen-pickups.json`.

## Installation and handoff

The final `-c` build is installed with the existing flight payload and runner
configuration. All nine installed hashes verify. Previous files are preserved
in `.codex-archive/20260910T0345452433175Z/manifest.json`. To restore, close the
game, archive the replacement files, and move each saved entry back to the
manifest's original absolute path. No permanent deletion or push occurred.

Next: the owner retests using `exec runner_playtest.cfg`, then `addrunner`.
CSV and demo capture remain enabled. Inspect whether pickup departures stay
continuous against human interference and whether pad steering lands safely.
Do not call ordinary return speed equal to the incoming route; measured return
pace remains slower. K07's broader defended comparison, M2 and timing gates
remain open; this repair does not weaken them.
