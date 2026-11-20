# Tick-independent gameplay

The owner decided on 2026-10-21 that game results must not depend on the server tickrate (`sv_fps`). That holds
whether or not the community servers ever leave `sv_fps 30`. This note records the decisions, the code, what was
left alone and how the change was checked.

The code is in `codemp/game/g_tick.c` and `g_tick.h`. Every hook in a stock file is marked `// tick ... (g_tick.c)`.

The work started from the tickrate report the owner reviewed, which was written against base_enhanced and
base_entranced. Four read-only searches then mapped its items onto xenjk's own game code. xenjk differs from that
report in places, and the report's base_enhanced line numbers don't apply here.

## Decisions (owner, 2026-10-21)

- **Lock to the values in the code.** A timer written as `+100` gives 100 ms at every tickrate.
- **A per-frame amount has no millisecond value**, so it is read at the 20 Hz rate the game shipped with
  (`TICK_DESIGN_MS` 50). Raven's own comment at `bg_misc.c:2942` says "default = 20".
- **Five exceptions keep today's `sv_fps 30` behaviour** (`TICK_TODAY_MS` 33, the frame the engine really runs at
  30, since 1000 / 30 rounds down to 33):
  1. Sabers.
  2. Lightning cost and Absorb's gain against it. In xenjk this needs no change: lightning already runs on a fixed
     50 ms catch-up loop (`w_force.c:4340`), 20 FP/s at any tickrate. The exception applies when this work is ported
     to base_enhanced, where lightning costs 1 FP per frame.
  3. shortBurstJetpack. This is base_entranced only and has no counterpart in xenjk.
  4. Force drains and upkeep: grip drain, Protect and Absorb upkeep, Rage HP drain.
  5. Siege healing, supply and turrets: the held use key and everything on it (stations, medic, object repair,
     dispensers), station recharge, `misc_turretG2`, `misc_turret` and the sentry gun.
- **One switch per area, on by default and not archived**, like the base_enhanced switches. Setting one to 0
  restores the stock frame-tied code for that area: `g_tickSabers`, `g_tickForce`, `g_tickWeapons`, `g_tickItems`,
  `g_tickSiege`, `g_tickMaps`, `g_tickNPCs`.

Movement needs nothing here. Client physics runs per usercmd, and with `pmove_float 1` air-strafe gain differs by
about 0.2% between 125 and 250 fps. Bot2's own frame-counted limits were converted separately (see
[2026-10-21-tickrate-horizons.md](../movement/records/2026-10-21-tickrate-horizons.md)).

## The rules

The code uses four patterns. Each helper's area-off branch is the stock expression.

| Stock pattern | Period at sv_fps F | Tick-independent form |
|---|---|---|
| Strict re-arm: `x < level.time`, then `x = level.time + N` | (floor(N/F)+1)·F | `G_TickStrictDue` / `G_TickStrictNext`: due at `x <= level.time`, re-armed from the old deadline (`G_TickRearm`) |
| Think re-arm: `nextthink = level.time + N` | ceil(N/F)·F | `G_TickNextThink(N)`: `G_RunThink` records the due time in `g_tickThinkDue` |
| Per-frame amount | proportional to sv_fps | Gated to a 50 ms grid (crushers, `trigger_multiple` with no wait, E-Web settle physics) |
| Frame count | N·F | Stated in ms (mind-trick break window: 4 frames becomes 200 ms) |

**How the re-arm works.** `G_TickRearm` keeps the timer's phase while it runs continuously, so the average period
is exactly N at any tickrate. If the timer has been idle for a full period or longer, it restarts from now, as the
stock re-arm does.

**The exceptions use 30 Hz periods.** `G_TickTodayStrict(N)` gives the period a strict timer has at 30 Hz, for
example 100 → 132 and 300 → 330. `G_TickTodayNested(N, think)` does the same for a timer that is only checked on a
think, for example sentry fire 200 inside a 100 ms think → 264. At `sv_fps 30` both reproduce stock exactly.

**Sabers.** `G_TickSaberPass` runs the whole saber pass on a 33 ms grid: the traces, damage, clashes and lock rolls
in `WP_SaberPositionUpdate`, plus the thrown saber's thinks.
- At 30 Hz or slower, every frame is a pass.
- Above 30 Hz the pass runs on the frame nearest each deadline, and previous blade positions carry over from the
  last pass.
- Per-quantity scaling was rejected. The fixed 16-unit probe, `ceil` per trace, the `dmg > 1` clash gate and
  one-pass clash suppression can't be scaled to match 30 Hz. They can only be sampled at 30 Hz.
- Also pinned to 30 Hz: the lead offset is `1.6/30` s, and saber-lock presses and pushes count once per pass
  (`G_TickSaberLockKey`).
- The server skeleton follows the pass. `G_UpdateClientAnims` and the swing smoothing in `G_G2PlayerAngles` run on
  pass frames only. With the engine's `G2API_SetTime` export (`sv_g2ClockImport 1`), each pass poses skeletons at its
  nominal grid time − 33, as a 30 Hz server's one-frame-stale Ghoul2 clock does, and animations start at the nominal
  time. Hitscan collision therefore sees 30 Hz skeletons too. Results:
  [saber lab record](records/2026-10-21-saber-lab.md).

**Force buttons.** These were read once per frame from the last usercmd, so a tap shorter than a frame was lost.
`ClientThink` now latches every usercmd's Force buttons. `WP_ForcePowersUpdate` reads presses, and releases inside
the frame, through `G_TickForceCmd`.

## What changed, by area

| Area | Change | Value |
|---|---|---|
| Force | Grip drain; Protect, Absorb upkeep; Rage HP drain | today: 132 / 330 / 627 / 165-330-462 ms |
| Force | Grip choke damage, lift and aim refresh, Force heal tick, levitation charge, seeker fire, gencmd repeat | code: 1000 / 300 / 1000 / 500 / 400-700 / 300 ms |
| Force | Mind-trick break window; button latch | 200 ms; taps inside a frame count |
| Sabers | Pass grid, lead offset, lock mashing, thrown-saber thinks | today (30 Hz) |
| Items | Use key (`useDelay`), shield/ammo/health converters, station recharge (and the per-frame model converters) | today: 132 ms; 30 points/s |
| Items | Thrown packs and limbs (ExPhys every 50 ms), E-Web settle physics, portable shield decay, item bounces keep the rest of the frame | code: 50 ms, 20 Hz, 1 s |
| Siege | Medic ammo supply, object repair, sentry and turret thinks, fire and turn | today |
| Siege | Jetpack and cloak drain and refuel; respawn waves keep the round clock's phase; siege item ExPhys | code: 200 / 150 ms; waves exactly `g_siegeRespawn` |
| Weapons | Homing-rocket steering; thermal fuse with no frame hops; DEMP2 alt ring stops at full radius (800 ms); tripwire beam; E-Web fire; missile bounces keep the rest of the frame | code: 100 ms, 3.5 s, 50 ms, 100 ms, 100 ms |
| Maps | Crushers, CRUSH_THROUGH trains, IMPACT rotators, blocked ICARUS movers; `trigger_hurt`; `target_laser`; `trigger_multiple` with no wait; lava and slime | 20 Hz (40 DPS at dmg 2); 100 / 1000 ms; 100 ms; 20/s; one hit per 700 ms |
| NPCs | NPC think (50 ms) and AI think (100 / 50 ms), which also fixes turn rate, fire cadence and per-think velocity decay; corpse physics; thinks with no command catch up to real time | code |

**Lava and slime.** These used the pain debounce, which only re-arms when a pain sound plays. When 700 divides the
frame (20 and 40 Hz) they hit twice, and when a hit was too small for a pain sound they hit every frame. They now
have their own 700 ms deadline.

**Commandless NPC thinks** (corpses, empty vehicles) used to advance 100 ms per call, which is 2x real time at
20 Hz. They now catch up to `level.time` and no further. This is a judgement call: it treats the old behaviour as a
bug, not a value.

## Left as stock, and why

- **One-shot deadlines** are never more than one frame late (8 ms at 125 Hz), and nothing accumulates. Examples:
  hand-extend expiry, toggle-off delay, respawn click, invulnerability, item respawn, hacking completion, round
  countdowns, lockouts, grip elapsed checks, thermal arming.
- **Cosmetic or stats only:** event clearing, pain-sound feedback, the team overlay, the seeker warning sound,
  stats sampling.
- **ICARUS `wait()`** (`codemp/icarus/TaskManager.cpp:1140`, strict per frame) lives in the C++ ICARUS module. It's
  a follow-up.
- **Vehicle muzzle refire** (`g_weapon.c:4268-4485`) uses cumulative per-muzzle delays. It's a follow-up.
- **Missile prestep and mixed-clock charge timing:** missiles start at the frame time
  (`MISSILE_PRESTEP_TIME` is unused), and charge measures `level.time` against a command-clock start. Both are
  within one frame.
- **Door, plat and train legs** restart at frame time, which drifts by about F/2 a leg. `func_timer`,
  `target_delay`, `fx_runner` and `trigger_space` likewise.
- **gencmd repeat after a different command:** the repeat debounce keeps its phase from the last deadline. So when
  a different gencmd fires inside the 300 ms window, a repeat of it can come up to one frame early.
- **Projectile blocking** (`WP_SaberStartMissileBlockCheck`) and kick traces still run every frame. So does stock
  bot AI.

## Verification

- **Logic.** A simulation of the helper rules (`tools/tick_rules_sim.py`) at sv_fps
  20/30/40/60/90/125/250 checked three things:
  - With an area on, strict timers average exactly their code value, for example 100.0 ms at every rate.
  - The exceptions average exactly their 30 Hz value (132, 330, 627; sentry fire 264).
  - At sv_fps 30 everything equals stock.
- **Saber pass interval.** It averages 33.0 ms at every rate of 30 Hz or above. The jitter is 25-50 ms at 40 Hz,
  32-40 ms at 125 Hz and 32-36 ms at 250 Hz.
- **Build.** The dedicated server and game DLL build clean. The only warnings are ones that were already there.
- **Native smoke.** Private localhost servers ran stock bots (a saber duel on mp/duel1, FFA on mp/ffa3) at sv_fps
  20/30/40/125/250, with the tick switches on and off. All 14 worlds ran 90 s with no errors, and saber kills
  happened at every rate. See [the 2026-10-21 record](records/2026-10-21-tick-independence.md).
- **Not yet measured:** whether saber outcomes match 30 Hz at other rates. The [saber lab](SABER_LAB.md) is the
  tool for that: scripted fighters, replayable worlds and per-rep outcome CSVs.

## Bot2

Bot2's runner and defenders read Force upkeep, use and the saber state like everyone else. The fixtures run at
sv_fps 40, so with these switches on some numbers shift at 40, for example Protect upkeep: 325 ms in stock at
40 Hz, 330 ms now. Movement gates and laps worlds are not expected to replay identically across this change.
