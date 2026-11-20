# Preserve strafe movement while aim turns

Continuation of `STRAFE_MOVEMENT_AIM_FRAME_2026-09-04.md`.

Status: investigation paused by the user. Mode 2 was not promoted. The seven
fixed-aim pairs produced 6/7 pickups versus mode 1's 7/7: five faster pickups,
one slower pickup, and a death in the -50 Y impulse case. Sideways aim produced
1/2 pickups for mode 2 versus 2/2 for mode 1; rotating aim also produced 1/2
versus 2/2. Geometry correctness did not establish safe native recovery.
Artifacts are preserved under `.codex-research/strafe-wishvector-pairs-20260904`,
`strafe-wishvector-sideways-20260904` and `strafe-wishvector-rotating-20260904`.
Further aiming work is outside the current movement-only project milestone.

## Mechanism

`bot_aimMoveFrame 2` reconstructs the world movement direction of free-view
strafing, then projects it onto the submitted aim orientation. Mode 0 retains
the original implementation; mode 1 retains the preceding submitted-yaw fix.
The default remains 0 during these opt-in experiments.

There were two remaining input-level discrepancies:

* `tYaw` describes the view heading for W plus a side key (or a side key alone).
  Treating it as the world movement heading loses the 45-degree diagonal offset
  for ordinary strafing, or the 90-degree offset for hard strafing.
* `PM_CmdScale` scales requested speed by the largest movement axis. Rotating
  a fixed-length axis vector can reduce that largest value from 127 toward 90,
  throttling acceleration as view rotates. Mode 2 keeps the largest axis at
  exactly 127 and quantizes the other axis in proportion.

Both live AIRBORNE execution and phantom aim-locked prediction call the same
new helper. It does not change jump buttons, view angles, turn rates, side
selection, route assets, or planning budgets. Hard strafing now uses whatever
forward/right combination represents its original world direction; requiring
forward input to remain zero cannot preserve side-only movement under a
different view heading.

This preserves the requested airborne direction and input strength, not the
entire native trajectory under arbitrary view changes. Grounded backward
running has an engine speed penalty, jump/animation behavior can depend on
keys, and special movement states have separate rules. The simulator still
has its existing timing, animation, weapon and landing-margin approximations.

## Geometry check

`tools/strafe_steering_check.py` compiles the production steering helper and
extracts the actual `PM_CmdScale` function from `bg_pmove.c`. An independent
world-vector reconstruction checks both strafe sides and both soft/hard
controls across headings and packed command/spawn angle offsets.

All 634,880 vector checks passed, with maximum heading error 0.449097 degrees
and maximum requested-speed error 0.00003499 units/s at ps.speed 425. The
preceding mode-1 checks also passed (158,720 cases). The fixture verifies that
the helper preserves the remaining command fields. These are geometry/input
scaling checks, not native collision or combat validation.

Build: `.codex-research/strafe-build-wishvector-20260904`.
Geometry artifact: `.codex-research/strafe-wishvector-geometry-20260904`.

## Native protocol

The paired runner now accepts `--modes CONTROL CANDIDATE`, optional case
selection, and scripted horizontal aim yaw/rate. The runtime fixture records
those settings. Yaw rate starts at the declared handoff; the bot receives only
the current aim direction. This is a scripted tracking stress, not a moving
opponent or combat encounter. Vertical aim is not included.

The native setup remains Kejim red-i2, 40 Hz, fixed 8 ms floating movement,
XenJK regen 200, saber and native Speed/flag gameplay. Bounded planning uses
the same 1,024/2,048/4,096 candidate/dispatch/server-frame limits. Decision seed
1 uses separate decision and native streams. Success requires actual enemy
flag pickup alive within 20 seconds.

```powershell
python -B tools/strafe_aimframe_matrix.py --build .codex-research/strafe-build-wishvector-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/vector-pairs-NEW --port 30000 --modes 1 2
python -B tools/strafe_aimframe_matrix.py --build .codex-research/strafe-build-wishvector-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/vector-sideways-NEW --port 30020 --modes 1 2 --cases phase3 phase6 --aim-yaw 90
python -B tools/strafe_aimframe_matrix.py --build .codex-research/strafe-build-wishvector-20260904 --assets 'game_directory/Jedi Academy/GameData' --out .codex-research/vector-rotating-NEW --port 30030 --modes 1 2 --cases phase3 phase6 --aim-yaw-rate 120
```
