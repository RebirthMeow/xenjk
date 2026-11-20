# Walking inventory: all eight current movement cases

The owner requested an inventory of walking and its causes before choosing
broader solutions. This is retained-evidence analysis only: no controller change,
new native run, installation or change to acceptance gates.

The current build's eight recordings contain **130 observed CSV walking
episodes, totaling 172.775 seconds**. Every episode is documented, including
short transitions and observations outside the declared scoring windows.
The scored subset is **155.500 seconds**, which exactly reconciles with the
existing per-case locomotion audit. This inventory does not label all walking
unnecessary or claim the exact cause of an unrecorded decision.

## Read the inventory

Each case report lists every episode's level times, scored/full duration,
positions, speed and Force ranges, Speed state, command owners, outbound/return
time, jump requests, boundary transitions, nearby events and prior-event ages.
It links directly to CSV and console lines. Geometry associations have their own
confidence label. All repeated events remain in JSON; prose links one example
of each distinct event/reason.

| Case | Episodes | Full walking (s) | Scored walking (s) | All episodes and evidence |
| --- | ---: | ---: | ---: | --- |
| Spawn red r1 | 13 | 22.375 | 21.625 | [Report](../.codex-research/walking-inventory-20260914-final/spawn-red-r1.md) |
| Spawn blue r1 | 17 | 21.825 | 21.325 | [Report](../.codex-research/walking-inventory-20260914-final/spawn-blue-r1.md) |
| Spawn red r2 | 12 | 21.675 | 20.925 | [Report](../.codex-research/walking-inventory-20260914-final/spawn-red-r2.md) |
| Spawn blue r2 | 18 | 23.475 | 22.725 | [Report](../.codex-research/walking-inventory-20260914-final/spawn-blue-r2.md) |
| Zero-resource red r1 | 15 | 17.025 | 16.275 | [Report](../.codex-research/walking-inventory-20260914-final/zero-red-r1.md) |
| Zero-resource blue r1 | 17 | 18.575 | 17.800 | [Report](../.codex-research/walking-inventory-20260914-final/zero-blue-r1.md) |
| Zero-resource red r2 | 15 | 17.800 | 17.025 | [Report](../.codex-research/walking-inventory-20260914-final/zero-red-r2.md) |
| Zero-resource blue r2 | 23 | 30.025 | 17.800 | [Report](../.codex-research/walking-inventory-20260914-final/zero-blue-r2.md) |
| **Total** | **130** | **172.775** | **155.500** | |

The larger final blue recording includes substantially more post-window travel.
Do not compare full-recording totals as if observation durations were equal.

- [All 698 measured walking intervals, sortable CSV](../.codex-research/walking-inventory-20260914-final/walking-intervals.csv)
- [Complete episode/event/geometry inventory and evidence hashes](../.codex-research/walking-inventory-20260914-final/inventory.json)
- [Verification](../.codex-research/walking-inventory-20260914-final/verification.json)
- [Reproducible analyzer](../tools/strafe_walking_inventory.py)

## What the observations establish

Of the full recorded walking time:

| Observation | Seconds | Share of walking |
| --- | ---: | ---: |
| Supported controller owns the command | 157.225 | 91.0% |
| Native waypoint fallback owns it | 6.250 | 3.6% |
| Cached-route entry owns it | 5.050 | 2.9% |
| Fast-return controller owns it | 3.750 | 2.2% |
| Cached-route execution owns it | 0.500 | 0.3% |
| Speed inactive, across owners | 168.275 | 97.4% |
| At least 50 Force, across owners | 120.600 | 69.8% |
| Exactly 100 Force, across owners | 61.525 | 35.6% |
| Speed inactive and measured speed 225–275 u/s | 151.475 | 87.7% |

Owner rows partition walking; resource/Speed rows overlap them. These are
observed states, not proof that every upcoming maneuver was affordable or that
activating Speed would improve the trip. Only 1.250 seconds of walking is at
zero Force. Resource depletion alone cannot explain the majority of this bin.

There are 48 episodes of at most 0.5 seconds. Ground contact around a launch,
landing or arrival is not automatically a defect. Conversely, the 54 episodes
lasting at least one second account for 135.500 seconds; brief transitions do
not explain away the bulk of the walking. Outbound walking totals 94.900 seconds
and return walking 77.875 seconds. Positive jump input appears in only 1.250
seconds of sampled walking, across five episodes; this records issued input,
not whether a jump would have been safe or successful.

## Cause families supported by code and geometry

The source establishes the mechanisms. For full admitted paths with logged
nodes, the analyzer associates a sample with an interval only when exactly one
interval contains its projection within 12 lateral and eight vertical units.
It excludes unpublished prefixes, active approaches and uncertain lifecycles.
Coordinates are rounded and the actual follower index is not logged, so these
are **geometry-consistent explanations, not exact per-frame causal replay**.

| Geometry/context family | Associated walking (s) | What it tells us |
| --- | ---: | --- |
| Hop-enabled interval, but remaining consecutive hop run is too short | 65.000 | Even an upper bound on remaining hop room is below the executor's required room. Short eligible pieces can still produce sustained walking. |
| Ordinary interval marked non-hopping | 37.475 | The prepared plan withholds hops here. Native preparation can clear eligibility for elevation, ceiling or sweep failures; the precise producer reason is not assigned from proximity alone. |
| Rounded corner interval marked non-hopping | 25.250 | The prepared corner is traversed on the ground; with Speed inactive, the ordinary target is 250 u/s even where the corner allows more. |
| Pass contract requires grounded traversal | 7.500 | Pass/rejoin clearance is a ground contract. Hopping through it is deliberately disallowed. |
| Hop flag and sufficient upper-bound room present | 10.750 | Another gate may decide: actual goal/hop room, speed, jump latch, braking or adapter ownership. Exact cause unresolved. |
| Prefix or checked approach cannot be reconstructed from full-plan nodes | 7.750 | Publication/approach events supply context; full execution geometry is absent. |
| Ambiguous or outside the logged interval association | 3.500 | Do not guess an interval or its gate. |
| Other command owners | 15.550 | Their mechanisms and event context are listed separately in each episode. |

The categories sum to 172.775 seconds. They are not measurements of wasted time
or promised recoverable improvements.

The [shared executor](../codemp/game/bot2_path.c) computes hopping from the
interval's eligibility, absence of a pass contract and room greater than
`max(320, speed * 0.8)`. Otherwise its ordinary target is base speed (250), or
425 with Speed active. Turn/pass limits and distance to a stopped goal can reduce
that target further. A jump also needs sufficient current/target speed and a
released jump latch. These are distinct reasons; the old trace does not identify
which conjunction failed on each frame.

The [supported owner](../codemp/game/ai_bot2_supported.c) can also yield at a
temporary frontier or admit a deliberately slow checked approach. Its shared
path command clears `generic_cmd`; the current publication branch does not
invoke the experimental straight-ground Speed helper. The failed ground-Speed
experiment remains off. Thus abundant Force does not itself imply that this
controller will activate Speed. Enabling that old experiment is not an accepted
solution to the inventory.

[Native local navigation](../codemp/game/ai_bot2_localnav.c) follows a waypoint
on the ground, including proximity braking, temporary refusal waiting and
detours. Its ordinary grounded branch does not request a hop. Native refusal
records identify floor/body/other reasons where present. [Route entry](../codemp/game/ai_bot2_route.c)
brakes and repositions for a ready cached route; the new build no longer parks
there merely to regenerate resources. Return admission/refusal events explain
specific route eligibility decisions, not every later walking frame.

## Representative episodes to study first

**Flat ground, full resources: `spawn-red-r1-W010`.** At levels 44800–49300,
the supported controller walks for 4.500 seconds with Force exactly 100 and
Speed inactive. The matched geometry consists of 2.500 seconds on hop-enabled
pieces whose consecutive room is too short, and 2.000 seconds on non-hopping
corner intervals. The corresponding red r2 episode W009 lasts 4.250 seconds.
No resource refusal occurs in the first episode; a return refusal 24.225 seconds
earlier belongs to earlier context and must not be called its cause.
[Full episode](../.codex-research/walking-inventory-20260914-final/spawn-red-r1.md#spawn-red-r1-w010).

**Corner, pass and ascent: `spawn-blue-r1-W004`.** At levels 26250–30250,
walking lasts 4.000 seconds under supported control, Force 26–45 and Speed
inactive. Geometry association divides it into short hop room (1.500 s), ordinary
non-hopping intervals (1.500 s), a non-hopping corner (0.750 s) and a grounded
pass (0.250 s). The recorded return controller repeatedly finds no admissible
entry or reports `join_momentum`; the previous `takeoff_reserve` release occurred
6.800 seconds before this walking episode. That establishes the route-selection
context, not a fresh per-frame resource shortage.
[Full episode](../.codex-research/walking-inventory-20260914-final/spawn-blue-r1.md#spawn-blue-r1-w004).

**Symmetric approach/ascent patterns.** Long supported episodes recur on both
teams: zero-red-r1 W008 and zero-red-r2 W007 each last 3.750 seconds with Force
80–98; zero-blue-r2 W007 has the same duration and Force range. Their reports
identify the relevant prepared intervals and return-entry events. This repetition
is a reason to study shared preparation/continuation policy rather than adding
one route-specific correction.

## Coverage and uncertainty

The primary inventory retains the existing definition: alive, grounded
**3D speed 20–<300 u/s**, using the left CSV sample over each adjacent interval
of at most one second. It is a speed bin, not a gait detector. The score subset
uses the original start/end row exclusion without interpolation. Every scored
case reconciles exactly; gaps and record boundaries are not silently filled.

Each case also includes a separate **40 Hz carrier supplement**: 121 observed
walking episodes, of which 44 have no overlapping CSV walking episode. Many are
brief contacts missed by the sparse CSV. They have their own owners, Force,
Speed timer and log references, and must not be added to the CSV totals.
The carrier trace has no outbound coverage or health field; all eight worlds
survived. Its 3D speed is reconstructed from logged XY speed and vertical speed.

Another 92.500 seconds is grounded speed at least 300 u/s, outside this walking
bin. Those intervals are listed separately in each case report and JSON so fast
ground running, including Force Speed running, is not hidden or called hopping.
Idle below 20 u/s remains a distinct existing audit category.

Missing decision fields include actual supported follower/goal index, selected
target speed, exact hop room, active braking constraint and the failed hop gate.
Flight/connection adapters share the `supported` owner label. Therefore the
inventory documents every **observed** instance and the available explanations;
it cannot honestly assign a definitive root cause to every real frame.

## Questions for broad solutions

1. Can preparation validate longer connected ordinary-hop runs across the
   straight/corner/straight sequence, rather than leaving many individually
   eligible pieces too short to use?
2. Which slopes and clearance transitions truly require walking, and where does
   a conservative preparation rule unnecessarily break continuity?
3. What should shared ground-speed and acceleration policy do when a safe hop
   is unavailable but resources are plentiful? Evaluate complete-trip resource
   cost and later feasibility, not just local speed.
4. Are nearby return continuations rejected because entry geometry or momentum
   requirements are too narrow, and can a checked continuation preserve motion?
5. For the explicitly unresolved categories, which minimal decision fields
   would distinguish safety-required walking from avoidable controller behavior?

These are study questions, not implemented changes or preselected fixes. Review
the inventory before another intervention; retain the same whole-trip gates.
