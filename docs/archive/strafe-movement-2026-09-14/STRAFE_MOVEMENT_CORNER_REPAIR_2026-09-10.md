# Moving return corners and arrival repair, September 10

## Implementation

The [human trajectory study](STRAFE_MOVEMENT_HUMAN_CORNERS_2026-09-10.md)
motivated a bounded repair of stopped return turns and a separate zero-input
arrival stall. No navmesh rebake or learned input policy is involved.

`ai_bot2_supported.c` now compiles streaming returns into one append-only
curved publication. The raw compiler's mutable final point is withheld from
corner compilation until stable. Execution keeps the same follower indices
when the completed curved path becomes the ready plan. Raw preparation leaves
budget for corner checks within the existing shared 32-trace/frame limit.
Every accepted curve retains floor, tracking-envelope, enlarged-hull and hurt
checks. Rejected corners retain ordinary stopped arrivals. Mode 1 remains
grounded; mode 2 retains its existing gentle airborne-curve checks. Refusal and
body obstruction during curved-prefix execution restore the incoming command.

`bot2_hop.c` uses the existing proportional controller for low-speed grounded
arrival intents. At the observed pose, the known corner is 24.99 units away,
just outside the 24-unit arrival boundary. Binary full-Speed input can overshoot
the requested approximately 52 u/s so much that zero input wins indefinitely.
Analog ground input resolves that deadzone without enlarging the arrival radius.
Airborne commands and full-speed run-up retain the original controller.

Native corner tests exposed unsafe fast-lane joins. `ai_bot2_route.c` now
rejects rising post-launch samples as grounded joins and checks actual forward
floor support during grounded non-jump execution. Checked tap corridors remain
available with low Force. Other takeoffs require at least 50 Force and 2.5 s
of active Speed. This is conservative containment of the observed weak-flight
failures, not a forecast-proven local resource model or a full-trip reserve.
It costs speed; reducing that margin needs evidence of the local flight, not
removal of the check. The legacy full-guide follower is unchanged.

## Retained builds and measurements

All evidence paths below are under `.codex-research/`. Both builds were fresh
and source-frozen through compilation and native collection.

| Build / evidence | Result |
|---|---|
| `strafe-build-corners-20260910-a` / `corners-20260910-a` | Streaming curves and analog arrival implemented. Natural red/blue: eight captures, no deaths, 12.0125 s captured-return median. Upper red/blue corner starts each eventually fall after unsafe fast-lane joins; retained, not installed. |
| `strafe-build-corners-20260910-b` / `corners-20260910-b` | Adds join/floor/local-flight containment. Same eight declared worlds: 28 captures, no deaths, clean shutdowns and complete 120 s game-time windows. Natural returns: five captures plus one ending censored episode, captured-return median 19.575 s. |

Cases include natural starts on both teams, upper bend, ramp and original
stall location in both directions. Corner cases start at 26 Force with Speed
expiring after 1 s, except the original red stall case retains 8 s. They use
declared one-time native setup, then ordinary production movement. Outcome
collection runs at timescale 5 with no more than four isolated worlds; these
are not timing measurements. `cases.json`, `runs.json`, each world's source/
binary provenance, logs, CSVs and demos retain the declarations and outcomes.
`corner-audit.json` and `return-audit.json` retain the detailed observations.

`initial-progress-review.json` measures from the native corner intervention:
all six placed cases clear 96 units in 0.525–1.025 s and 256 units in
1.275–1.775 s, before any fast-return takeover. Four cases accept curves;
ramp-red and stall-red do not. Six supported-owned upper-red samples inside
the logged curve chords have minimum speed 250.1 u/s. These are sampled,
not continuous-clearance measurements. Residual initial low-speed spans are
0.5 s ramp-red and 0.25 s stall-red; stall-blue later briefly slows after
fast-return takeover. Useful departure is established, uninterrupted speed
through every turn is not. Other rejected/unprepared corners still brake.

The safety containment regresses natural return time against fastreturn-d's
roughly 11.8 s development median. It remains faster than the earlier
conservative pickup/pad median of 24.75 s, but those earlier cohorts are not
matched trials. Do not claim an across-the-board speed improvement from this
repair. Its intended benefit is moving corners, reliable arrival and avoiding
the two newly reproduced falls.

## Focused verification

- `supported-wait-streamcorners-20260910-b/result.json`: actual corner
  publication and dispatch with mocked geometry. Mutable tail withholding,
  follower continuity, shared trace budget, refusal and obstruction handback.
- `hop-arrival-check-20260910-a/result.json`: actual command controllers,
  simplified flat integration. Reproduces the original Speed deadzone, crosses
  the arrival boundary with Speed on/off, and preserves 32 airborne/full-run-up
  command comparisons. No native collision claim from this check.
- `fastreturn-check-corners-20260910-b/result.json`: actual admission/dispatch
  and floor helper with mocked traces. Local resource boundaries, rising-sample
  refusal, void/hazard/non-world/steep-floor refusal, unchanged commands/PS on
  refusal, and retained zero-Force checked taps.

These checks do not replace the native outcomes. At zero speed with the guide
exactly at the player, the new ground lookahead has no direction and begins
checking after acceleration; it is not a complete collision forecast.

## Delivery status

`corners-playtest-20260910-b`: both zero-Force upper-corner worlds complete
120 s with seven total captures and no deaths. Normal-speed one-enemy MP
`listen-red` and `listen-blue-retry` each complete 120 s with two captures and
one runner weapon death, no falling death. Six pickup episodes retain four
captures and two carrier deaths; captured-return median is 20.2875 s.
Longest sampled grounded idle is 1.25/1.5 s, walking 41.0%/38.7%, and neither
world has an audited five-second confinement/distant stall. These are small
unmatched cohorts, not a proven survival improvement. The original blue attempt
failed rate-command confirmation and is retained as invalid, not pooled into
behavior results. See `opposition-audit.json`, `return-audit.json`, CSV and
native `games.log` for attribution.

Build `strafe-build-corners-20260910-b` is installed with `bot_fastReturn 1`.
Nine replaced files are archived under
`.codex-archive/20260910T0541114616343Z/manifest.json`; installed hashes verify.
To restore, close the game, archive the current replacements, then move each
saved entry to its manifest's original absolute path. No files were deleted.

Next: human retest in a fresh Kejim CTF game, `exec runner_playtest.cfg`, then
`addrunner`. Check corner hesitation and the arrival deadzone first; report the
slower safe fallback candidly. K07, exhaustive body/resource qualification and
strict timing remain open. No timing improvement is claimed by this repair.
