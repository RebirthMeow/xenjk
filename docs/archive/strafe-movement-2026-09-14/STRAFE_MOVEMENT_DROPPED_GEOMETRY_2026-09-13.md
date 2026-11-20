# Dropped flag geometry diagnosis

September 13, 2026. The owner clarified that retrieval requiring passage through
`trigger_hurt` is out of scope. Such flags should not require deliberate pit
dives. Ordinary safe retrieval remains a movement objective.

## Finding

The retained dropped-flag case is **not a flag resting in a hurt pit**. It is
on the lower floor at `(1691,3696,1360)`. Five native support probes at its XY
position and offsets of 32 units find flat world floor, standing hull clearance,
and no hazard refusal. Floor surface is approximately z=1344 and resolved
standing centers are z=1368.25. The flag's item origin is not the bot's standing
center.

The bound Kejim BSP contains one authored `trigger_hurt`, model `*4`, entity
index 11, bounds `(-768,-1592,8)` to `(3392,5752,704)`, damage -1, spawnflags 12.
Its upper bound is 640 units below the flag's supporting floor. This inventory
is read from the exact BSP whose SHA-256 matches the existing flight binding;
native hazard checks at the five floor points also pass.

The bot's later stop is elsewhere: around `(1925,5451,1772)`. A native point
probe there finds world support with normal z=0.859855 and clear standing hull.
At the recorded refused XY `(1919.163,5403.721)`, a lower surface query at z=1728
also finds clear world support, normal z=0.925793, with no rejection. Both normals
exceed the existing walkable threshold of 0.7. The earlier localnav corridor
trace only searched from z=1788.567 to z=1752.567 and returned no hit.

These results identify a sloped descent/support-envelope investigation rather
than a requirement to enter the death volume. They **do not prove a complete
traversable route** from the stopped bot to the flag. Static endpoint support
does not establish continuous support, intervening hull clearance or native
Pmove behavior. Vertical scan probes at other heights retain failures; sloped
projection results depend on their query height and are not exact settled
player positions. Do not simply increase the floor search or waive its checks.

## Existing objective behavior

`ai_bot2_objective.c:ReachableDrop` already checks contact between the flag's
player-sized neighborhood and native `trigger_hurt`, and rejects lava/slime.
It then requires a bounded navmesh path whose endpoints are near the bot and
flag. An unsuitable enemy drop stages the runner at its stand while the native
flag timer continues. This is existing code, not a repair made by this diagnosis.

That endpoint hazard test does not establish that the **path** avoids every
hurt volume. Preserve the owner's broader boundary: a retrieval that requires
passing through hurt is not required. Do not claim the existing endpoint test
alone fully implements that boundary.

Retained `[OBJECTIVE]` logs keep the flag at `(1691,3696,1360)` and report it
reachable at level 2200, briefly unreachable at 11700 and reachable again at
13200. The lower-floor goal persisted through the later stop until native
return. This was the deliberately declared dropped-flag fixture, not evidence
of a death-produced flag in a pit.

## Evidence and next action

Evidence root:
`.codex-research/dropped-geometry-b4d342e7c43a43c1a87c7f539f2800d4`.

- `hazard-inventory.json`: BSP binding and authored hurt bounds.
- `request.json`, `input/manifest.json`: all 14 declared native point probes,
  including unsuccessful vertical queries. No selected failures are omitted.
- `native/audit.json`, `native/report.json`: complete native query audit,
  staged loaded-module path and artifact integrity all verified.
- `support/support.json`: bound BSP/navmesh support used to declare queries.

The unchanged nav-continue build is reused after native source/binary checks;
one isolated hidden native world runs on port 36600. No production edits,
installation, resource interventions, movement gate changes or deletions occur.

Next, reproduce the stopped ramp descent from actual state and inspect how
the floor samples relate to the sloped surface and chosen next path point.
Seek checked ordinary descent or another safe route. Avoid pit-dive work and
retain all prior full-trip outcomes. See the
[current handoff](STRAFE_MOVEMENT_IMPLEMENTATION_GUIDE.md).
