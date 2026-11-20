# Automatic known-map support inventory — 2026-09-06

## Implemented scope

`tools/strafe_map_preparation.py` reads an explicitly selected PK3 BSP member
and navmesh. The same parser exports support polygons, internal adjacency,
boundary/portal segments, off-mesh endpoints, CTF spawns and flags for two real
maps. It serializes no pointers and binds the exact PK3, BSP, navmesh and tool
bytes by SHA-256. The output is `strafe-known-map-support-v1` and explicitly
`UNVALIDATED_NATIVE_COLLISION`.

This begins automatic map preparation within M3a research. It is not a
movement-transfer result or M4 qualification. No native game or installed
movement code changed for this inventory.

## Format and verification

The parser follows this fork's Detour v7 aligned tile representation and MSET1
wrapper, including the stored agent height/radius/climb. It converts Recast
X/Y-up/Z meters to Quake X/Y/Z units using the existing navmesh conversion.
It validates header/lump/tile lengths, finite coordinates, polygon vertex and
neighbor indices, off-mesh ranges/types, and unique MSET tile references.
It requires exactly one matching ZIP member; package precedence is chosen by
the explicit input, not guessed from a map name. That does not establish which
PK3 a future game session loads; native map binding is still required.

Internal adjacency reciprocity is reported as a diagnostic. Cross-tile portals
are exposed without claiming a fully stitched cross-tile graph. Both measured
assets contain one tile. CTF entities are compared to expanded navigation
bounds as an axis/layout sanity check, not a floor-support test.

Six focused tests cover valid solo/MSET formats, axis roundtrip, malformed and
truncated inputs, nonfinite coordinates, invalid counts/indices, BSP entity
parsing and actual asset reparsing. Header agent parameters are observations;
they do not prove an exact native hull-clearance region.

## Two-map extraction

| Item | Kejim | Dantooine v3 |
|---|---:|---:|
| Ground polygons | 539 | 2,404 |
| Directed internal adjacency edges | 1,038 | 3,574 |
| Nonreciprocal internal edges | 0 | 0 |
| Boundary segments | 1,071 | 5,983 |
| Off-mesh connections | 27 | 1,746 |
| CTF entities | 26 | 28 |
| Entities within navigation bounds +256 units | 26 | 28 |
| Stored agent height / radius / climb, Quake units | 64 / 15 / 18 | 64 / 15 / 18 |

Inputs are `game_directory/Jedi Academy/GameData/base/ctf_kejim.pk3`, member
`maps/mp/ctf_kejim.bsp`, with `navmesh example files/ctf_kejim.navmesh`; and
`game_directory/Jedi Academy/GameData/base/ctf_dantooine_v3.pk3`, member
`maps/ctf_dantooine_v3.bsp`, with
`game_directory/Jedi Academy/GameData/base/maps/ctf_dantooine_v3.navmesh`.

The current hardened-parser evidence is:

- `.codex-research/strafe-map-preparation-20260906/kejim-v2/support.json`
- `.codex-research/strafe-map-preparation-20260906/dantooine-v3-v2/support.json`

Earlier pre-hardening outputs remain in adjacent `kejim` and `dantooine_v3`
directories; they are not separate movement trials.

## Next connection to movement

Use the inventory to propose support-to-support transitions and entry/exit
regions, then validate them with native hull traces and the shared feedback
forecast. A separated boundary can represent a wall, ledge, tile seam, patch,
hazard or mover interface; the current tool deliberately emits no jumpability
or racing-line claims. It also does not infer free space from off-mesh labels.

The bounded native support interface below now reports floor normal,
standing-hull clearance, swept obstruction, hazards and relevant moving/inline
entities. Generated transition descriptors still need native feedback
execution with explicit approach, launch, landing and onward regions. Preserve
arbitrary polygon shape and height changes; the existing three-box,
equal-height fixture cannot represent these descriptors faithfully. Validated
transitions can then enter offline maneuver discovery and compatible
continuation preparation. Preserve rejected geometry and reasons, and use
identical extraction/controller code on both maps.

## Native support probe — declared before collection

`bot2_mapbench.c` now provides an opt-in dedicated, cheats-only read-only
collision fixture. `tools/strafe_map_probe.py` selects 64 polygon-center point
probes and 32 unique internal-adjacency short sweeps per map, in stable order.
Sweeps are at most 96 units. Inputs bind the extracted support JSON, navmesh,
PK3 member bytes, BSP SHA-256, engine-compatible signed MD4 block checksum,
and exact probe payload. The same implementation is used on both maps.

The standing hull is [-15,-15,-24] to [15,15,40]. A point traces from 88 units
above the proposed surface to 104 below it. Acceptance requires WORLD floor,
normal Z at least 0.7, floor-height association within 24 units, and clear
stationary standing hull after a 0.125-unit lift. Lava/slime, hurt triggers,
movers, non-WORLD support and saturated nearby-entity inventories reject the
sample with explicit reason bits. The 24-unit association bound avoids
silently validating a different stacked floor.

A short sweep requires both endpoint checks and a clear native hull sweep,
plus a conservative swept-box hurt/mover check. It does **not** prove
continuous floor support, a traversable jump, or a useful movement policy.
Rejected navmesh samples are retained for diagnosis, not excluded from the
coverage denominator. Counts describe this sample, not all map polygons.

`tools/strafe_map_native_runner.py` stages the exact BSP and source-bound
engine/DLL in a private hidden localhost runtime. It starts no bots, issues
one probe command after map readiness, verifies the loaded DLL path and
rechecks staged artifacts and declaration hashes. The strict result audit
requires the exact ordered inventory, float32-equivalent declared inputs,
full numeric results, known reason bits, module record and matching start/end
bindings. No installed-game change is involved.

Run both positive map payloads and one checksum negative control per map
using the fresh adverse/runtime/map build. Negative controls flip one bit of
the declared checksum and must refuse with the exact actual checksum before
any probe result. Because refusal precedes the module log, those controls do
not claim logged module-path verification. Outcome probes run within the
four-world process cap; none run alongside a timing benchmark.

## Native results and calibrated query revision

The v1 declaration above is retained. Complete v1 collection and checksum
controls are in `.codex-research/strafe-map-native-v2-20260906/`: Kejim accepts
41/64 points and 29/32 short sweeps; Dantooine accepts 15/64 and 14/32.
The first collection remains separately retained after an RCON packet-capture
timeout. The runner now collects every response packet through the declared
end or refusal record.

The high initial v1 hull can overlap ceilings and skip its final occupancy
test. Query v2 therefore projects a zero-size point from surface Z+24.125 to
Z-104, requires associated WORLD floor and normal Z >=0.7, and computes the
standing hull's slope lift. It then performs a short full-hull support trace
and an unconditional final occupancy trace after finite support results,
retaining all failure bits. Hazard and 24-unit floor-association limits remain.
Native contact offsets make observed flat standing center Z=24.25 in the
calibration; the nominal 24.125 metadata is not an exact-position gate.

The sealed [analytic calibration](STRAFE_MOVEMENT_MAP_PROBE_CALIBRATION_2026-09-06.md)
passes all nine declared cases for each query version on the same fresh build.
V2 accepts the valid 72-unit ceiling and independently rejects a 56-unit
obstruction. Both versions reject missing or incorrectly associated floors.
The same v2 implementation accepts 67/96 Kejim and 30/96 Dantooine inputs;
both checksum controls refuse before any probe rows. The map comparison uses
different builds for v1/v2; the analytic comparison uses one build.

Evidence is bound in `.codex-research/strafe-map-query-qualification-20260906/`
and independently rechecked in
`.codex-research/strafe-map-query-analysis-20260906.json`. Rejections remain in
the denominator. These counts do not establish map accuracy, continuous floor
support, jumpability, useful travel or runtime cost. In particular, the low
Dantooine acceptance count cannot be explained away as a ceiling artifact.

The [automatic transition generator](STRAFE_MOVEMENT_MAP_TRANSITIONS_2026-09-06.md)
now emits 883 Kejim and 4,925 Dantooine proposals within its declared bounded
search. Every proposal remains unvalidated for movement. The next map gate is
one-transition native feedback execution before composing compatible exits.
