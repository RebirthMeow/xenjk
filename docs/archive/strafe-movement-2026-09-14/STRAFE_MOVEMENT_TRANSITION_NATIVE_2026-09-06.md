# Automatic one-transition native preparation — 2026-09-06

This protocol selects a small deterministic cohort from each map's frozen offline transition proposals, asks the version 2 mapbench query to resolve local collision facts, and packages accepted results for a generic one-transition feedback fixture. It contains no map-specific coordinates or hand-authored route.

The cohort uses the same frozen rules on both maps: horizontal gap 64 through 192 units, absolute edge-height difference at most 32, source approach progress at least 96, destination landing progress at least 96, and onward progress at least 32. Eligible proposals rank by the larger usable minimum of approach and landing space, then the minimum including onward space, absolute height difference, gap, and stable proposal ID. At most 16 are retained per map. Every excluded proposal and every eligible overflow remains in the preparation manifest with its reason.

Each selected proposal produces four point probes—source-centroid approach, source closest-edge launch, destination-centroid landing, and adjacent-centroid onward—and two short sweeps from approach to launch and landing to onward. The destination closest-edge point remains geometry metadata rather than the landing target. A collision-prepared descriptor requires all six native records to have reason zero. The descriptor retains the complete source, destination, and onward Detour polygon vertices in original order plus a validated convex counterclockwise normalization for the later wire encoder, the original edges and closest points, measured gap and height, native resolved standing centers, all point records, and both sweep records.

Selection rejection, collision-preparation rejection, and live transition failure are separate outcomes. A reason-zero point establishes local WORLD support and occupancy under the bounded query. A reason-zero direct sweep establishes only that particular resolved-center hull segment and its hazard inventory. Neither proves that the approach is traversable from elsewhere, the open-air gap is clear, an ordinary jump can reach the destination, the landing remains supported under motion, or the feedback policy succeeds. The later native fixture must retain all attempted live outcomes and bind them to these exact preparation artifacts.

## Native feedback execution declaration

The separate `BOT2TRANSITION1` fixture preserves three convex support polygons
and four resolved standing centers. Collinear vertices and unequal heights
are retained; a triangular fan interpolates vertex heights for support
association. Native collision is authoritative. The payload binds the map
checksum; the runner additionally binds BSP bytes, compiled sources, loaded
module, geometry, helper snapshots and native settings.

The eight frozen policies combine launch insets 0/8/16/24 with airborne onward
weights 0/0.35. A fresh process runs each fixed policy, with one 160-frame
forecast and 160 actual 25 ms movement frames. This is offline candidate
discovery, not online selection. The initial neutral tick acquires native
support; ordinary jump input lasts one command. Air steering uses the
descending quadratic root at the actual landing height. Actual contact,
position and velocity drive every policy phase. Existing frozen controllers
and two-gap fixtures remain separate.

Before map execution, collect all eight candidates on each of five sealed
analytic layouts: flat, rotated 90 degrees, rotated 45 degrees, landing 32
units higher, and landing 32 units lower. Require exact forecast, replay,
feedback and authoritative movement agreement in every supported episode,
and at least one useful candidate for each layout. A useful transition has a
WORLD-supported landing in the destination polygon, at least two supported
samples, at least 32 units of onward progress from first landing, onward-plane
passage and onward projected speed at least 80. Retain the full four seconds;
later support departure, wrong support, a failed policy phase or a large fall
invalidates prior completion. No four-second case closes the confinement gate.

Run a mismatched-map payload control, a forecast-only steering-bias control
and a legacy two-gap reference on the same fresh build. The map mismatch must
refuse before movement; the bias must fail parity. Retain all failure captures.
If development changes are required, preserve the declared cases and prior
build evidence, then qualify a fresh source-bound build.

For each map, retain every collision-preparation result in the 16-proposal
denominator. Run all eight policies for every prepared descriptor using the
same controller. Unprepared proposals are refusals, never successful zero-idle
episodes. Separate supported simulation, parity, useful travel and unsupported
world interactions. Triggers, non-WORLD collisions, active force, and material
effects remain outside this static forecast contract. No timing claim follows
from these outcome runs, which use at most four isolated worlds.

After a verified probe audit, the adapter emits one `strafe-transition-v1` controller JSON per collision-prepared proposal. It converts the signed engine map checksum to its exact unsigned 32-bit representation, carries the BSP SHA-256, uses the four resolved native centers, and supplies the normalized source, destination, and onward polygons. The shared `strafe_transition_evaluation.encode_transition` implementation must accept every emitted document before it is saved; the adapter also retains proposal, collection, preparation, and audit identities as non-wire metadata. The encoder's exact native payload is stored beside each JSON and hash-bound in an index.

## Crossing-line correction after the first map campaign

The first complete map campaign executed 88 fixed policies for 11 collision-
prepared proposals, retaining all 32 selected proposals. It produced no useful
transition. Native trajectories show corner collisions, direction changes
between run-up and crossing, and centroid targets more than 320 units beyond
takeoff even where boundary separation is below 192. This is the first failed
map representation iteration; the next correction changes its crossing line
rather than adding more timing candidates to the same line.

Retain the original 16 IDs/ranks per map. Within each original boundary pair,
sample bounded interior span locations and derive a collinear supported run-up
and a near-edge interior landing. Rank geometrically before native collection;
retain every geometric option and every no-line rejection. Revalidate all four
anchors and both supported sweeps with the existing version 2 native query.
Geometry refusals, native preparation refusals, forecast refusals, parity
failures and non-useful executed travel remain separate denominator outcomes.
Use the same eight policies on every newly prepared descriptor. A second
failure triggers representation review before any further sampling refinement.

The controller now anticipates the next command's takeoff-plane crossing using
current forward velocity and command duration, while current WORLD/source
support is still mandatory. It does not widen the support polygon. The static
forecast guard additionally excludes the native item-pickup region defined by
BG_PlayerTouchesItem; the physical hull intersection alone missed ammo pickup
and caused the first campaign's eight nine-frame parity failures. No item is
removed and ammo/event differences are not added to the parity exclusions.
Qualify these changes on the same 43 calibration/reference/control cases using
a new source-bound build before collecting the revised map movement episodes.
