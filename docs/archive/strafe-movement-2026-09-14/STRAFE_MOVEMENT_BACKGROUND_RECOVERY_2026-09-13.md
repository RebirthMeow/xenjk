# Background landing recovery

## Decision and behavior

The landing query is now prepared while ordinary fallback retains movement.
The [first attempt](STRAFE_MOVEMENT_LANDING_REQUERY_2026-09-13.md) queried promptly
but executed a short stopped prefix immediately, braking R4-red to roughly
100 u/s before its return join. That attempt is superseded.

The eligibility conditions remain narrow and map-independent: a preparation
lateral refusal at an ordinary vertex without a positive speed limit, world
ground within 250 ms of an observed airborne state, at least 64 units from the
old query root, and a one-second cooldown. The refused path is discarded and a
fresh query starts after 25 ms. Its unfinished preparation cannot take the
command. Readiness restores ordinary execution and refusal contracts; preparation
that has not finished within one second releases with normal retry. Death,
objective/clock changes and external ownership still reset the private state.

This gives recovery a current query without forcing the runner onto an immature
replacement. It does not eliminate the original lateral miss, waive a speed
limit, widen the corridor, grant resources or encode a map/route exception.
No dedicated thread or installed-game change is involved.

## Measured behavior

Evidence root: `.codex-research/landing-background-1c665f0490dc44cf971d32c0cb37b4ab`.
Behavior build: `.codex-research/strafe-build-background-7fadfd9e59b343868c68379c7c10755b`.
Baseline remains the preceding takeover arm at
`.codex-research/takeover-a3fa9a86cffa4f16b928d6bb3af25c63`.

The same twenty solo cases were collected three times, with all settings, assets,
recordings and failures retained. These are case comparisons, not deterministic
paired native trajectories.

- 60/60 worlds captured, no deaths/falls/lower-floor departures, health floor 90,
  churn zero. All 18 eligible landing releases queried again after 25 ms.
- Average of twenty case mean capture-time deltas: **-0.013 s**. Eleven case
  means improve; twenty individual time pairs regress. This is effectively
  neutral trip time, not a meaningful speed win.
- R4-red mean delta +0.067 s; R4-blue +0.025 s. The first trial's immediate
  supported braking is gone, but startup/entry variability and other costs remain.
- First-return idle totals 600 ms baseline and 575 ms candidate; walking totals
  223,875 ms and 223,450 ms. Eight pairs have worse idle fraction and 23 worse
  walking fraction. Both arms have zero uncovered first-return trace time.
- All six R5 approaches admit; none abort. No later-lap speed refusal occurred
  in this arm. That non-recurrence does not prove the previously observed
  overspeed arrival was repaired.
- T5 stays **0/8** in all three repetitions. No all-cases idle/walking or broader
  movement qualification claim is made.

Full-world counts have differing later-lap/end-of-world coverage and are not
equal-duration rates:

| Measure | Baseline r1/r2/r3 | Background r1/r2/r3 |
|---|---|---|
| Localnav frames | 35 / 82 / 49 | 36 / 52 / 59 |
| Stationary frames, speed <20 | 60 / 83 / 44 | 45 / 48 / 59 |
| Zero-input frames | 208 / 240 / 204 | 182 / 207 / 200 |
| Branch entries without terminal event at end | 7 / 2 / 1 | 2 / 0 / 1 |

The frozen behavior build's raw audit reports 12 contract-tagged releases per
repetition: six actual landing refusals and six external resets carrying the
previous refusal's stale metadata. `landing-events.json` preserves all 36 records;
none are omitted to pass a gate. The final source clears that diagnostic record
on acquisition, so a later external reset cannot report a previous query's
contract failure.

## Verification and limits

The supported-dispatch check verifies background command preservation, query
origin, readiness, one-second preparation timeout, cooldown and unchanged
refusal behavior. The ownership counterfactual removes only the new background
dispatch guard and fails the query-frame assertion. Corner-span, seam,
path-release and fast-return checks pass.

The failed draft check is retained. It demanded command restoration after a
ready executor refusal even though its mock always writes a command on refusal;
that was not a valid assertion about native behavior. The final check retains
the refusal and retry assertion and directly checks command preservation during
the new background preparation.

Affected native regressions pass: 26/26 temporary-body cases meet their subset
thresholds and 30/30 resource cases complete without refusal. All 1104 measured
sources and both binaries match at the end of behavior collection in
`verification-behavior-final.json`.

Final build:
`.codex-research/strafe-build-background-final-12c1564c4fdb4f8684cd19f2c946304a`.
Its only subsequent C change clears refusal metadata on a new query; execution
policy is unchanged. The supported check includes this reset. Six affected
native worlds on this fresh build all captured and stayed alive, with exactly
six actual landing releases, no stale contract-tagged resets, and all six new
queries after 25 ms. `final/verification.json` binds their results to all 1104
current sources and both final binaries.

The sixty-world performance figures belong to the frozen behavior build, not
the final binary. The diagnostic-only correction was verified on its affected
six cases rather than relabeling earlier recordings. No full qualification or
installation is implied.

## Defended discovery and next safety work

The two 600-second normal-speed worlds ran on the preceding frozen landing trial,
not this final behavior build. They had no landing-requery events. Their complete
results and source bindings are in the [first-trial record](STRAFE_MOVEMENT_LANDING_REQUERY_2026-09-13.md#defended-discovery):
13/9 captures, no confinement, and one blue plunge ending in `MOD_FALLING`.

The flight adapter, window 11 candidate 2, owned that launch under the CSV's
`supported` label. A nearby opponent was overhead when upward velocity collapsed.
Contact with that body is plausible; native collision identity is still missing.
Neither an assumed self-steered map departure nor Speed expiry alone is an
established explanation. Do not add an unproved gate or suppress that map window.

Three normal-speed solo probes start from retained grounded positions, velocities,
yaws, Force and expiry values shortly before the departure. All captured and
stayed alive, with no plunge; they exercised flight entry. They acquire fresh
paths and omit combat, cached controller and subframe state, so they are not
replays and their non-recurrence is not proof of repair. Commands, limitations
and results are in `departure-cases.json`, `departure-scope.txt`,
`departure-probes/` and `departure-plunges.json`.

Next: reproduce that adapter window with its cached path and native pre-hop state,
record contact entity and compare forecast/live motion around levels
525425–525975. Then repair the general admission or recovery failure the evidence
identifies. The other large target is supported outbound walking without Speed:
the defended runs include long intervals at full Force, so resource shortage is
not a sufficient explanation. Preserve the original T5/T6/K07 gates.
