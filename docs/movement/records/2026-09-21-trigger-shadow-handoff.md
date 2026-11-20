# Next goal: measure false trigger refusals without changing admission

This work order supersedes the proposed direct trigger-test replacement in
the offer-limit record. It follows the parity and offer-limit review.

## Goal and decision

Determine whether the forecast's conservative trigger check rejects useful
continuations that would not touch a trigger under the native contact rules.
Start with classes 150 and 125 of `red_out_L1_stand`. Keep state preference on,
successor aim and reroute on, exit-speed weight 500, item bypass off, playback
and heading sweep off. Preserve the graph seed and all current safety gates.

The current census reports **first stopping reasons**, not independently
classified complete trajectories. A trigger refusal can hide a later fall,
another contact, a failed onward walk or a slow trip. The item-toggle null
already demonstrates this censoring. Neither the high trigger count nor a
player origin outside a trigger box establishes a false refusal or speed upside.

## Implementation boundary

1. Read `TouchesTrigger` in `bot2_forecast.c` alongside `G_TouchTriggers` in
   `g_active.c`. The forecast currently tests overlap of a swept axis-aligned
   bounding box using fixed hull extents; this is not an exact segment/brush
   collision query. Native non-item contact uses the actual player hull and
   `trap->EntityContact`; item pickup uses `BG_PlayerTouchesItem` instead.
2. Add a default-off **shadow diagnostic**, leaving the original refusal and
   candidate result unchanged. At the same simulated frame, record old-test
   and native-contact decisions, trigger identity/type, before/after position,
   actual hull bounds/posture, and the physics substep/call boundary. Do not
   invoke trigger touch callbacks, apply damage/pickups, or mutate live state.
3. Establish which native boundary is being reproduced before labelling a
   disagreement a false positive. Endpoint contact, swept-hull contact and
   native discrete trigger processing are different questions. Preserve
   conservative handling of unsupported moving triggers and unknown cases.
4. Capture diagnostic data in bounded storage and emit it outside the search.
   Existing evidence shows printing inside search changes outcomes. Count and
   report dropped records; never silently treat an overflowed census as complete.

## Verification and collection

- First verify the shadow query against small native contact fixtures: clear,
  hull-edge overlap with origin outside, diagonal near miss, standing/crouched
  hulls, hurt brushes, and item-specific pickup bounds. Include world-restoration
  checks and a deliberately wrong query as a negative control.
- Freeze measured sources and build uniquely. Declare cases, repetition counts,
  attempt budget and controls before collection. Retain all misses and failures.
  Use unique localhost ports below the host's dynamic UDP range and verify binds.
- Demonstrate diagnostic off/on preserves decisions and complete-trip outcomes
  at matched starts. Run timed comparisons sequentially, alternating arms;
  outcome floors may use at most four isolated worlds. Keep diagnostic overhead
  separate from runtime cost claims.
- Report disagreements by trigger, candidate and decision, with both pooled
  and distinct-trajectory counts. Separate pre-start, timed trip and post-trip
  activity; count every observed admission/refusal category, not just contacts.
- Measure how many disagreements affect potentially competitive candidates.
  A changed contact result alone is not proof that a full candidate is safe,
  executable, admitted, or faster. Any later continuation experiment must keep
  unknown hazards refused and have its own declared protocol.

## Exit criteria and handoff

Deliver a bound report with source/binary/settings/input hashes, raw diagnostic
records, a reproducible analyser, disagreement counts and representative cases.
Then choose explicitly: no useful false refusals -> close this branch; useful
false refusals -> propose a narrowly scoped contact-test repair with its safety
argument and fresh qualification. Do not change admission as part of this goal.

The [parity guard review fix](2026-09-21-parity-review-fix.md) passed 41 focused
checks and fresh 36+2 native cases (19,898 reported comparisons, zero mismatches
or refusals). Start from this guarded source; any new diagnostic source requires
its own fresh, bound build. Historical parity evidence remains attached
to the earlier `parity-fix-build-20260920-a`, not automatically to new sources.
No installation, production qualification or 15-second attainment is implied.
