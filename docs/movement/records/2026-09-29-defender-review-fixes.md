# Defender review fixes, before and after (2026-09-29)

A code review of `bot2_defender.c` raised four faults. The owner asked for
them to be fixed, then for the before/after defender worlds.

## Fixes

- **Rearm crash (P1).** `ItemWanted` read `item->item->giTag` before
  checking the entity. A tracked dropped weapon that expires, or is taken and
  freed, leaves `item` null. It now returns false for an entity that is not
  in use, is not `ET_ITEM`, or has no item.
- **Aim steered the movement (P1).** `Aim()` (used by throws and the idle
  aim) and `Shoot()`'s humanized view overwrote the command's yaw without
  turning `forwardmove`/`rightmove`. A defender looking at the runner walked
  sideways off its planned path. The new `SetView()` turns the movement with
  the view. The movement keeps its world direction, and its largest
  component, which sets the wish speed.
- **Dry alt fire counted as a shot (P2).** The ammo gate used the primary
  cost, so a repeater with 1-14 rounds pressed alt. `bg_pmove.c` then sets
  `WEAPON_FIRING` and a refire time without spending ammo, and `ShotConfirm`
  accepted the refire time as a shot. The gate now uses the chosen mode's
  cost. A shot that costs ammo is confirmed only by the ammo falling. The
  refire time still confirms free shots (the pistol, infinite ammo).
- **Engage delay survived cover (P2).** Out of sight, the engage timer and
  the humanized aim were kept. Both now restart.

## The crash was already in the evidence

`learn-20260929-a/laps-walk-1` (build `learn-build-20260929-g`) stopped at
game time 436 s. The [learnings record](2026-09-29-defender-learnings.md)
lists it as "10 (7.2 min)" without noting that it crashed.
- **Dump:** its `jampgame_crash.dmp` faults at DLL offset `0x60bf0`,
  reading address `0x28`.
- **The instruction:** it loads `item->item` and reads `giTag` (offset
  `0x28` in `gitem_t`), then `giType`. It then compares them against
  `IT_WEAPON` and `WP_TRIP_MINE`. That is `ItemWanted` with a null item.
- **Last log line:** base D's `REARM ... class=weapon_trip_mine`.

## Measured

Worlds, scripts and comparisons are in `.codex-research/aimfix-20260929-a/`
(`run.sh`, `compare_laps.py`, `compare_bench.py`).
- **Before:** `learn-build-20260929-g` (HEAD `31073284` sources). The bench
  checks them against `src-head/`, a copy of the checkout with HEAD's
  `bot2_defender.c`.
- **After:** `aimfix-build-20260929-a`.
- **Configuration:** the learnings campaign's. Both defenders, every
  behaviour on (rearm, reset, 3 mines, ambush, walked errands), the owner's
  loadout `8a,11,10a`.

**Laps, 10 game minutes each:**

| Arm | Worlds | Crashed | Game minutes | Captures /10 min | Runner deaths /10 min | Throws /10 min | Fires /10 min | Defender falls (base, chase) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Before | 4 | 3 | 7.2, 7.2, 5.1, 10.1 | 14.1 | 14.0 | 51.6 | 37.8 | 10, 7 |
| After | 4 | 0 | 10.1 each | 14.4 | 13.6 | 51.5 | 45.8 | 1, 1 |

- **All three "before" crashes are the rearm fault:** the same offset and
  read address. `laps-before-1` and `-2` are byte-identical game logs, the
  same world as the retained `laps-walk-1`, crashing at level 436450. So
  laps worlds can replay exactly, and the "before" arm holds three distinct
  worlds, not four. The four "after" worlds are distinct.
- **Defender falls went from 17 in 29.6 game minutes to 2 in 40.4.** The
  retained build-g walk worlds had 2-8 (base) and 1-5 (chase) per world. The
  learnings record put base D's walkway falls down to the walking planner.
  The likelier cause is the aim fault: `Shoot()` turned the view to the
  runner during guard walks and rearm walks, and the movement turned with it.
  The death lines do not say what each defender was doing, so this is
  inferred from the drop, not traced fall by fall.
- **Captures and runner deaths are unchanged** within these worlds' spread.

**Bench, class 150, both defenders:**

| Arm | Stopped before the grab | Carriers killed | Throws | Fires (rocket, repeater alt) | Crashes |
| --- | --- | --- | --- | --- | --- |
| Pickups, before | 4 | 4 | 58 | 11, 7 | 0 |
| Pickups, after | 6 | 8 | 64 | 31, 12 | 0 |
| Returns, before | 0 | 4 | 21 | 12, 4 | 0 |
| Returns, after | 0 | 7 | 26 | 25, 6 | 0 |

- **Case outcomes:** pickups 19 → 17 passed (`blue_out_L3_respawn5` and
  `red_out_L1_stand` now die). Returns 17 → 17. Times moved on most cases.
- **Fires roughly doubled** despite the stricter shot count. Defenders that
  keep to their path spend more time in range with a line of sight. One arm
  per side cannot separate that from run-to-run spread, so only the
  direction is claimed.

## Limits

- One map (Kejim), four laps worlds per arm, of which three were distinct in
  the "before" arm. The crashes shorten the "before" worlds. Rates are per
  10 game minutes.
- The dry-alt fix removes phantom `FIRE` lines, but the logs cannot count
  how many the old build had: a dry press looked like a shot.
- Fixes are uncommitted at the time of writing.
