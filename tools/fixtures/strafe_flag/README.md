# Kejim movement references

These small text fixtures let the native benchmark run without the Unreal
workspace or development output directory. The game assets are still needed.

`kejim_red_i2_guide.csv` is a byte-for-byte copy of the Unreal workspace's
`GlobalRoutes/ctf_kejim_spawn_t1_i2_goal1.jkafrugal.csv`.
It is an existing Unreal TAS spatial/control proposal, reporting 6.207997 s
to its broader goal region. World Y and yaw require conversion for XenJK.
It is not a native-exact command tape.

`kejim_red_i2_planned.csv` is a byte-for-byte copy of
`.codex-research/strafe-flag-frugal-feedback40-aligned-20260904/home/base/flag_live.csv`.
It records an earlier successful native feedback development run. Its packed
angles and movement axes form the frozen 6.600 s planned-replay baseline,
with actual enemy flag pickup. This baseline predates the final controller
and matrix. It is not an independently optimized or optimal TAS claim.
Timing-cost columns describe the original development run only; replay uses
the recorded commands, not those cost measurements.

See `docs/STRAFE_MOVEMENT_FLAG_RUN_2026-09-04.md` for settings, initial state,
scope, results, and reproduction. Neither file is a human demo or a training
label. Keep them frozen; create separately named fixtures for other routes.

`kejim_red_i2_runtime.txt` is the native-axis guide in `STRAFEROUTE1` format,
bound to Kejim checksum -1301924053 and team red. It feeds the optional
`bot_strafeRoute` live movement adapter. It contains guidance, not timed
execution commands; see `docs/STRAFE_MOVEMENT_LIVE_RUNTIME_2026-09-04.md`.

## Blue respawn i3

`kejim_blue_i3_guide.csv` is a half-turn of the red spatial guide about world
XY (1312,2080), matching Kejim's opposing spawn layout. Its unchanged creation
comment describes the candidate stage; native validation is recorded in
`.codex-research/strafe-blue-spawn-qualification-20260904` and
`docs/STRAFE_MOVEMENT_COVERAGE_2026-09-04.md`. Collision symmetry was tested,
not assumed. The controller approaches the actual live red flag at
(152,-1160,1900.125), which differs from a perfectly reflected blue flag.

`kejim_blue_i3_planned.csv` is the actual native planned-reference capture
from that qualification. It picks up in 6.600 s, starting from native blue
respawn 3 with yaw 270 degrees after settling. It is not an independently
optimal TAS. `kejim_blue_i3_runtime.txt` binds the native-axis guide to team
blue and the same Kejim checksum. Runtime from the native pose takes 6.800 s
nominally; all eight timing phases pick up, with worst time 6.800 s.

## Shared route catalog

`kejim_catalog.txt` bundles the unchanged red and blue runtime guides, in that
order, as `STRAFECATALOG1`. One `bot_strafeRoute` value now supports both
entries, selecting by team and nearby position. The catalog's SHA-256 is
`a108e1683eef2b9db8f5e3a5ce54526dc1c2241f894fe67095456bddce452da8`.
The native catalog trials preserve all 3,248 original red and 2,148 blue
movement/command checkpoints exactly. A separate red native-heading test
qualifies respawn 2 at 6.775 s nominal / 7.900 s worst, versus 6.600 s planned.
See `docs/STRAFE_MOVEMENT_ROUTE_CATALOG_2026-09-04.md` for the limits and audit.
The other spawn entries and both fast flagstand routes remain work in progress.
No installed configuration was changed.
