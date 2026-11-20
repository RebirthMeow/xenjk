# Map symmetry and the graph grid

Recorded 2026-09-24. Analysis only: no source or binary changed, no native
world run. Follows the [respawn4 mirror record](2026-09-24-respawn4-mirror.md),
which traced a 2.5 s red/blue gap on Kejim to the 32-unit graph grid.
Scripts and the full scan are retained in
`.codex-research/map-symmetry-20260924-a/`.

## Method

Every installed CTF BSP was read directly. The transform taking the red flag
onto the blue flag (point reflection, axis mirror or diagonal mirror about the
flags' midpoint) was chosen by the most player-solid world brushes
(`CONTENTS_SOLID | CONTENTS_PLAYERCLIP`) matched plane for plane. Maps of
interest were then compared split-insensitively: world collision and
`trigger_hurt` voxelised at 16 units and each voxel compared with its mirror;
solid patch control grids compared exactly; spawns, items and jump pads by
position; every hotspot probed as an exact solid column. Limits: differences
under 16 units show only as one-voxel fringes, and the voxel test counts the
void outside a map as empty, so it is not meaningful where the outer hull is
asymmetric (Kejim: about 11 percent flagged, none of it shown to be play
space).

## The grid condition

Graph cells stand at multiples of `GRAPH_GRID` (32). A symmetry maps cells
onto cells when its translation is a multiple of 32 on both axes; for a point
reflection about centre C that is 2C = 0 mod 32. Kejim's C is (1308, 2076),
so 2C mod 32 = 24: blue's half is red's half shifted 8 units against the
grid. Consequences:

- The effect is not directional. The same trip gives the same time for either
  team, and at level 1 it favours the red side at respawn4 (14,550 against
  17,050 ms) and the blue side at respawn5 (about 800 ms). A constant shift
  helps or hurts each feature according to where the lip or doorway falls
  against the cell centres.
- A 16-unit grid does not fix Kejim (2616 mod 16 = 8); only 8 units would, at
  16 times the cells. A grid offset of 12 units (C mod 16 = 12 on both axes)
  makes Kejim's cells exact mirrors at the same cell count; it invalidates the
  seed graph, learned edges and library.
- Almost every other installed CTF map already has 2C = 0 mod 32.
- Even with mirrored cells, the planner's tie-breaking (link order, heap
  order) is not mirror-invariant, so equal-cost routes can still differ.

## Maps

Seven maps match every world brush and every spawn exactly: `ctf_aeten`,
`ctf_anoat`, `ctf_bilios`, `ctf_manaan`, `ctf_metalorn`, `ctf_reboam`,
`ctf_talay`.

**`ctf_dantooine` (original).** Mirror across x = 3392, grid-aligned. All
respawns, initial spawns, items and flags exact; the pit's `trigger_hurt`
matches; 662 of 668 solid patches exact. Collision differs in 0.05 percent of
voxels, all one voxel thick (1-2 unit detail-brush offsets, a diagonal wall's
sampling fringe). One real asymmetry: the jump pad at (-96, -1760), its target
and its trim sit 8 units off in y from the red pad at (6880, -1752).

**`ctf_dantooine_v3` (the one still played, owner 2026-09-24).** Same mirror
and alignment, spawns and items exact (7 respawns a side), but the lower level
near both flags is rebuilt asymmetrically: a 320 x 64 wall at x 1216-1536 on
blue against a 64 x 256 wall at x 5184-5248 on red, floor steps at z -80..-64
in unmatched places, 21 of 781 patches unmatched (one 79 units off), and the
same 8-unit jump pad. Red/blue parity cannot be expected on v3.

**`ctf4` (`assets3.pk3`).** Diagonal mirror, grid-aligned. Collision 99.5
percent symmetric; the red-only differences are a 320 x 320 player clip at
(4640, -1888) z 480-704, continuous clip caps over three pillars where blue's
have gaps at z 288-576 and 992-1280, the flag platform's clip underside (z -208
against -160) and a `trigger_hurt` extension under sealed floor slabs. All 13
initial spawns are exact; 5 of 12 respawns are 4-416 units off their mirror,
and 22 of 67 ammo pickups are placed differently.

## Owner decision, 2026-09-24

No global grid refinement. v3, the map in play, is asymmetric, so the aim is
route robustness to where cell centres fall, not red/blue parity. Noted as
open, below level 2:

- A diagnostic arm on Kejim with the grid offset by 12 units, to separate the
  grid-phase share of the respawn4 gap from the learned-edge share.
- Local refinement near drops (the phase-2 open item) if lip sensitivity
  persists after that.
- v3 as the next second-map test, each side judged against its own best-known
  times.
