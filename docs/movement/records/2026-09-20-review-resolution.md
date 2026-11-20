# September 20 review resolution

The review covered the September 19 work after 14:37 Eastern. It found one
reroute execution defect and three errors in the momentum analysis: events
outside the measured leg, one-hop completions counted as internal chain
landings, and duplicate outbound traces described as distinct trajectories.

## Reroute arrival keeps its validated continuation

An off-path candidate can complete without arriving on its first continuation,
then reach the objective when revalidated with the replacement route. Previously
that second-pass `arrives` result discarded `routeCells`. The commit therefore
kept the old path, although the replacement path was what made arrival possible.

The judge now retains the replacement route for every admitted second pass,
including arrivals, and checks that its landing still matches the route root.
The existing commit installs that route and resets the path index. Arrivals
retain their existing exemption from the remaining-route gain gate; a direct
first-pass arrival still needs no replacement route. Ordinary rerouted
continuations keep their existing admission tests.

### Behavioral regression

`python tools/test_movement_reroute.py` compiles the production `StraightJudge`
and its route-install commit branch with deterministic forecast boundaries.
It checks replacement-path arrival, ordinary replacement, direct first-pass
arrival, and landing-drift rejection. The arrival case also checks that a
short baseline route does not newly reject arrival through the route-gain gate.

All four production scenarios pass. A negative control that drops the arrival
path binding fails. The exact pre-fix source was also reconstructed by reversing
this patch: its SHA-256 matches the retained `move-momentum-build-a` manifest
(`e2fa3addee0fb65cf270d6685a44e140351a64814615a033e0c1a02448521a1b`).
Running the same regression against that source fails the route-binding
assertion. These checks prove the bookkeeping/commit repair, not native physics.

Retained evidence:

- [Repaired regression](../../../.codex-research/movement-reroute-test-20260920T063612Z-e59d2ee4/result.json).
- [Pre-fix regression](../../../.codex-research/movement-reroute-test-20260920T063653Z-5ccbbc56/result.json).
- [Exact reconstructed pre-fix source](../../../.codex-research/review-arrival-before-20260920T0639.c).

### Build and native checks

A fresh dedicated-engine/game build passed with native sources frozen through
collection:
`review-arrival-build-3110d3de7b40460aaebae4e7591c8b0e`, manifest
`20260920T063448114703Z.sources.json`. No installed-game files were changed.

Four sequential isolated worlds exercised red and blue `out_L1_stand`, with
`--edges --parity 256`, the `move-explore-20260916-b` seed, timescale 5,
and `bot_moveReroute` on/off. All completed, with only the movement owner and
zero deaths. The enabled runs installed one and three replacement routes.

| Arm | Red (ms) | Blue (ms) | Cases passed |
| --- | --- | --- | --- |
| `bot_moveReroute=1` | 19,700 | 19,350 | 2/2 |
| `bot_moveReroute=0` | 20,550 | 20,900 | 2/2 |

These are focused outcome checks, not a timing comparison or a new 36-case
qualification. The parity harness made **zero comparisons**, so its zero
mismatches do not qualify parity. These worlds did not produce a replacement
second-pass arrival; that specific branch is covered by the deterministic
regression above. The native runs exercise ordinary replacement and default
execution without claiming native coverage of the repaired arrival case.

- [Enabled native results](../../../.codex-research/review-arrival-native-on-20260920T0636/result.json).
- [Default native results](../../../.codex-research/review-arrival-native-off-20260920T0638/result.json).

The existing movement-bench Python suite also passes all 22 tests.

## Momentum analysis corrections

The corrected [momentum record](2026-09-19-what-the-kept-speed-buys.md) binds
each landing and its complete observation window to the measured outbound leg,
uses the committed plan length to distinguish internal from final landings,
and explicitly reports duplicate traces. It supersedes the earlier pooled
counts and the claim that all three recorded runs were distinct trajectories.
The handoff, plan, evidence and lessons use the corrected interpretation.
Original recordings remain unchanged.

The [reproducible audit](../../../.codex-research/momentum-review-20260920-a/audit-v2.json)
contains 58 pooled landings, or 39 after keeping each distinct trace once.
The pooled airborne/grounded split is 33/25; the distinct-trace split is
22/17. True internal chain continuations are 26/26 airborne (18/18 distinct).
Grounded actions are 13 commits, 5 straight searches admitting nothing and
7 without a recorded straight search (10/3/4 distinct). The next investigation
therefore starts from four distinct unsearched landing records, with their
missing instrumentation reasons still to be collected.

All five `test_movement_momentum_audit.py` regressions pass, covering fixture
and full-window boundaries, missing intermediate frames, committed plan length,
duplicate telemetry and the engine's admitted candidate outcomes. Original
documentation snapshots and the first audit remain beside the corrected audit.
