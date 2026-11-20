# A local second-hop fan regresses both common classes

The bounded candidate-generation experiment is complete and rejected by its
declared screen. This is **not a speed win**. `bot_moveSuccessorFan` remains
default off; no research baseline, installed game or admission rule changed.
Confirmation and candidate acceptance floors were conditional on passing the
screen and were not run after it failed.

## Named transition and construction

The retained class-150 third commit lands near (617.1,1120.9) at 514.3 units/sec;
its next committed run-up launches at 253.1. The corresponding class-125
transition lands near (608.7,1174.0) at 535.2 and taps into a two-hop continuation.
These are different roots and paths, not a causal experiment on landing position.
They motivate asking whether a nearby second-flight trajectory helps.

`LandingFanOffers` appends at most six candidates: +/-3 degrees around the
second-hop heading of an existing measured-landing aim. It runs only at an
actual landing root, only for straight two-hop candidates with zero first
run-up, and preserves the complete first hop and all other plan fields.
Existing candidates and ordering remain intact. It performs no additional
first-landing forecast; the existing full native judge validates every new
candidate from the original root. No start-class, map or edge ID enters the rule.

The experimental cvar enables the constructor only at `=1`, default 0.
`aim_offers` includes its additions; a fan candidate's `aim_source` is its
immediate measured-aim parent, which in turn names the geometric original.
The implementation is retained disabled to reproduce this result, not promoted
as an improvement.

## Declared screen

Evidence and pre-collection protocol:
`.codex-research/successor-fan-20260921-a/`.

One fresh build in both arms, frozen through collection; weight 500, preference 1,
reroute 1, successorAim 1, existing graph seed, no item bypass, no shadow diagnostic,
no playback, no generic heading sweep and no forced selection. Two alternating
pairs per class; one world at a time, unique bound ports below 49152. At most 16
attempts per cell; all attempts retained. Controls were predicted in advance.

| Start class | Fan off, ms | Fan on, ms | Difference |
| --- | --- | --- | ---: |
| 150 | 17,575 / 17,575 | 18,150 / 18,150 | +575 ms |
| 125 | 15,925 / 15,925 | 17,375 / 17,375 | +1,450 ms |

All eight cells completed over 20 native worlds; twelve missed starts are
retained. Every accepted class 150 start was 3350 and every class 125 start 3325.
All four controls reproduce their declared reference. Zero deaths, movement
owner only, **5,832 reported parity comparisons, zero mismatches or refusals**.

Restricted to these two classes with declared 43:34 weights, mean completion
regresses **961.36 ms**, from 16,846.43 to 17,807.79. Equal weighting regresses
1,012.5 ms. This is not a four-class population estimate or confirmation.
Repeated outcomes within each cell share a trajectory; they are not independent
discoveries.

Logged search rollout cost at committed in-trip decisions:

| | Samples | Median ms | P95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| Off | 48 | 51.35 | 103.0 | 105.2 |
| On | 46 | 60.55 | 153.6 | 160.2 |

This is conditional logged search work at committed decisions, not total
server-frame CPU or production runtime qualification. Medians rise about 18%.

## What happened at the target

At class 150 commit 3, the original candidate still executes in both arms.
The six appended offers are present: the three negative-angle variants are
`no_onward`, while the positive-angle variants are `rerouted` with progress
849.1, 854.5, 858.4 and landing speeds 512.7, 513.3, 512.9. The retained exact aim
has progress 861.3 and landing speed 514.3; none of these added offers displaces it.
The target's next braking action also remains unchanged.

The first changed selected offer in that matched class 150 trace is later,
commit 11, where a fan child is chosen; the completed trip is slower. The screen
tests the constructor wherever its geometric conditions apply, not a one-frame
forced intervention, so it does not attribute the entire loss to commit 11.
Do not reframe the extra admitted candidates as a speed win.

## Verification and binding

- Fresh build manifest `build/20260921T061655966401Z.sources.json`: all 1,117
  native source files match after collection.
- `tools/test_movement_successor_fan.py` compiles the actual constructor and
  result reset; tests preserve the first hop/prefix, heading normalization,
  eligibility, parent mapping, disabled/nonlanding behavior, capacity and
  nonrecursive append. Wrong-hop negative control fails as expected.
- Existing actual-C successor-aim regression also passes. Artifacts:
  `.codex-research/successor-fan-test-20260921T061710Z-b2259302` and
  `.codex-research/successor-aim-test-20260921T061711Z-6dbca4e2`.
- `screen/state.json` retains commands, every attempt and accepted-result hashes.
  `analyse.py --phase screen --out <fresh path>` recomputes the result and
  refuses to overwrite its output.
  `screen/summary.json` retains completion, safety, parity and cost summaries.

## Handoff

Keep the fan off and the prior baseline unchanged. Do not broaden this into an
angle sweep or repeat the failed 8-unit run-up experiment. The named transition
remains unresolved: this construction did not change its chosen arrival state.

Before another candidate variation, compare the existing native flight states
at that transition to identify an independently changeable input that can alter
longitudinal landing position or flight duration while preserving feasibility.
The current evidence supports rejecting this fan; it does not establish the
next input or prove that candidate generation cannot improve the trip. Any next
proposal needs its own bounded screen and full-trip result. Trigger admission
remains conservative; its corrected diagnostic conclusions are separate.
