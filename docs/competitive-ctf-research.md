# Competitive CTF: base_enhanced, newmod and NewJK (research, 2026-10-08)

Why: local play against Bot2 should play like real competitive JKA CTF, which runs the base_enhanced server game mod,
with players on the newmod client mod and, often, the NewJK engine. Read-only research of the local checkouts
`..\base_enhanced` (avygeil, HEAD 774700c, 2026-01-24) and `..\NewJK` (jkanewmod, HEAD
0b17c8ef, 2026-01-24) and newmod's public site (https://jkanewmod.github.io/). No live server was contacted.

## The three pieces

| Piece | Side | Source | Base | Notes |
| --- | --- | --- | --- | --- |
| base_enhanced | server game module (qagame) | open, GPLv2 | Raven JA SDK 1.01 (2014 import of Sil's mod), legacy `vmMain`/`trap_*` API, Win32 | 1,826 commits (deathsythe47, Avygeil, TheSil); game/ only, no cgame/ui; sqlite accounts, team generator, racemode |
| newmod | client cgame/ui (`newmod.pk3`) | no public source, no license granted | - | Windows x86; works on vanilla `jamp.exe` or NewJK, and its authors do not support OpenJK-family exes |
| NewJK | client engine (`jamp.exe`) | open, GPLv2 | OpenJK as of 2017-05-14 (948b55b5), not merged since | v1.12.4; netcode, input, console, demo and file-system changes; extends the cgame/ui API with newmod-only calls |

## Server side: what base_enhanced changes (the part wanted)

Which version a hosted server runs is a configuration question: several reworks are cvars that default off. The
values a competitive server uses are not in the source and must come from a real server config.

- **Grip.** Always on: re-grip cooldown 500 ms (stock 3000) after a grip ends and after a push breaks it (stock 1000);
  a level-3 hold has no distance limit, levels 1-2 break past 512 (stock 256). `g_gripRefreshRate` (300). `g_gripRework`
  (0; 1 = costs 30/30/32; 2 = 20/25/30, first gripper wins, a gripped player cannot drain or use lightning, victim slowed
  x0.2 in shared bg). `g_gripAbsorbFix` (0).
- **Drain** (`g_drainRework` 0). 1: one shot (200 ms hand, range 512, 1 s cooldown), removes 11/22/33 Force for
  8/17/25, heals up to 125, and debuffs the target 1333 ms per level (no regeneration, cannot receive TE). 2: costs the
  drainer health instead, returns 60% of the drained Force. Stock is a channelled arc draining 2-4 a tick.
- **Team Energize / Heal.** Stock amounts and radii; only living teammates; drain-debuffed players skipped; optional
  line of sight (`g_thTeRequiresLOS` 0) and lag compensation; `g_redirectWrongThTeBinds` (1) fires the power you know.
- **Protect.** `g_protect3DamageReduction` (0.80, the stock value); generic Force toggles debounced per power (300 ms).
- **Push/Pull.** Knocked-down players can be pushed and pulled (always on; stock refuses); optional LOS fix.
- **Lightning** (`g_lightningRework` 0): a line, distance falloff; always on: a tickrate-independent 45 DPS.
- **Force timers** (`g_fixForceTimers` 1): durations rounded to server frames, powers end at `<=`.
- **Movement in shared bg** (changes routes; prediction-visible, so the client must match): friction on players'
  heads, roll from 100 u/s, `g_fixRoll`, `g_fixWallgrab`, `g_fixForceJumpAnimationLock`, jump-pad handling.
- **Fights and flags:** location damage default 2 (legs x0.7, head x1.3), several weapon fixes, nearest player gets a
  contested flag (`g_fixFlagPickup` 0), dropped-flag spawn protection.
- **Environment it treats as standard** for records: sv_fps 30, pmove_float 1, g_gravity 760, g_forceRegenTime 231.
  xenjk's worlds run sv_fps 40, gravity 800, regen 200; moving means rebuilding graphs, seeds and tapes.
- **Never port:** account "boost" assists (auto TH/TE, aim, spawn, auto self-kill) and the code paths keyed on hard-coded account names in grip, push, drain and damage.

## Client side: newmod's features and the protocol behind them

base_enhanced (open) is the server half of newmod's protocol, so the protocol is readable without touching newmod:

- Detection: userinfo `nm_ver` and `nm_flags` (base_enhanced `g_client.c:2128`).
- Feature list: configstring 25 (`CS_SERVERFEATURELIST`), e.g. `rwd`/`rwd2` (drain rework), `lrw` (lightning rework),
  `tola`/`tolf` (team overlay force/alignment), `cull=N`; locations in configstring 26.
- Messages tunnelled in the stock `kls -1 -1 <verb>` command, which a stock cgame ignores: `fpt` force timers, `ygt`
  TH/TE received, `snf2` spectator info, force loadouts, custom votes, voice chats, stats.
- Authentication (`clauth`/`svauth`) needs newmod's own keys; it only feeds accounts and stats.

**The drain visual.** Stock: the drainer shows `drain.efx`/`drainwide.efx` (the arc at level 3); the victim gets
`drained.mp3` and a 1 s red shell. With the rework the server sends only data - the feature flag and the debuff
duration in `EV_FORCE_DRAINED`'s `s.time` - and newmod draws the line effect and the debuff. A stock cgame on a rework
server shows the wrong arc and a 1 s shell. xenjk's own open cgame can draw the equivalent from that data alone.

## Options for xenjk

1. **Server: port the Force set into xenjk's game module** under base_enhanced's cvar names and defaults (w_force.c,
   g_combat.c, g_xcvar.h; xenjk has not changed stock w_force.c). Defer the force-timer rounding, the generic-command
   rewrite and the shared-bg movement fixes until Bot2's forecast mirrors them (`bot2_forecast.c` ForecastForceUpdate,
   ForecastGenericCommand), or the bots' predictions stop matching. About 1-2 days; check with one laps comparison.
2. **Client: newmod-style features in xenjk's open cgame** from the documented protocol: the feature list, the drain and
   lightning visuals, force timers, team overlay force and alignment, locations, strafe helper. No newmod binary.
3. **Not recommended:** rebasing Bot2 onto base_enhanced (legacy API, Win32 C99, 12 years apart: weeks, every gate
   invalidated); running newmod's closed DLL on xenjk (unsupported by its authors; NewJK-only calls would fail; no
   reverse engineering); adopting NewJK's unversioned cgame API changes.
4. **Worth borrowing from NewJK** (GPLv2, manual ports): larger command and packet backups, time-nudge controls, raw
   input, reconnect on overflow, server-synced cull distance, console and chat fixes.

## Ported (2026-10-08)

The owner's server profile is in: `codemp/game/be_force.c`/`.h` hold the ported logic, the stock files carry one-line
hooks marked `// base_enhanced:`, and every cvar defaults to stock. `tools/fixtures/competitive/base_enhanced_profile.cfg`
is the competitive set (drain rework 2, grip rework 2, base_enhanced's grip cooldowns and hold distance, pushes on
knocked-down players, team energize to the living, protect 0.70, the TH/TE bind redirect; lightning rework off).
`docs/ports/base_enhanced.json` records each port's upstream commits and functions, and `tools/upstream_watch.py`
(`--fetch` to update the checkouts) reports which mirrored functions upstream changed since the last review. Declined:
`g_fixForceRegenTime`. Deferred until Bot2's forecast mirrors them: the per-power command debounce and grip rework 2's victim slow
(shared movement code). `g_fixForceTimers` (owner, 2026-10-08) is ported with the forecast mirror: at 40 fps it
ends a bot's power two frames sooner and a human's one (the rounding is a no-op); its rage-recovery `<=` in shared
code is not. Off, Dantooine laps 1-4 replay; on, the 24 Kejim bench cases pass with zero parity mismatches
(`.codex-research/be-timers-20261008-a/`). The drain feature flag is written to configstring 25 (`rwd2`).

Measured (`.codex-research/be-force-20261008-a/`, `be-force-build-20261008-a`): with the profile off, Dantooine laps
seeds 1-4 replay the merge's worlds exactly. With it on, against the area 8 worlds: team captures Dantooine 5.18 -> 4.38
(-0.80 +- 0.56), Kejim 3.94 -> 3.91 (-0.03 +- 0.36); runner falls Dantooine 2.47 -> 3.70, Kejim 7.40 -> 8.81 and Kejim
runner weapon deaths 30.51 -> 32.11 (each within noise alone, all one way: a knocked-down runner can now be thrown into
a pit, and protect saves less). Bots grip, drain and energize nothing, so those parts are untested by bots.

## Client side ported (2026-10-08)

xenjk's cgame draws what a base_enhanced server sends, from the open server source only (newmod is not used or
inspected, so this is xenjk's own rendering): `codemp/cgame/cg_be.c`/`.h`, with one-line hooks marked
`// base_enhanced protocol:`. Nothing changes without the server's data; `cg_drawForceTimers 0` hides the timers and
`cg_beDrainVisual 0` keeps stock drain visuals on a rework server.

- **Force timers.** The server half is `g_sendForceTimers` (be_force.c, default 0, profile 1): base_enhanced's
  `kls -1 -1 fpt`/`sfpt` messages to the player and the spectators following them, sent when a timer changes rather
  than from hooks in the power code; bots get none. The cgame draws the seconds left and a bar under each active-power
  icon. Rage recovery is in the playerState, so its timer shows on any server.
- **Drain rework.** Configstring 25 `rwd`/`rwd2` switches the drainer's effect to one line at every level (no level-3
  arc), held for the 200 ms hand extend because the server shows the shot for one frame. The victim's red shell lasts
  the debuff in `EV_FORCE_DRAINED`'s `s.time` (up to 4 s), with its alpha held at stock's peak (stock's `/8` wraps past
  2 s), and the viewed player gets a red drain icon with the debuff's time.
- **Correction to the timer port (223c07e8).** It rounded every power's duration; upstream rounds only the six timed
  powers. With the profile on, drain rework's 1 ms one-shot ended before it fired: drain never fired. Fixed
  (`BE_FixPowerDuration`); bots use no drain, grip or lightning, so the earlier bench and laps checks were unaffected.

Checked on a private, muted local listen server (`.codex-research/be-client-test-20261008-a/`, profile on): the drain hits
an NPC, costs 30 health at level 3, shows the line for about six frames and a 4 s red shell; speed and rage show
counting timers (20 -> 19, 8 -> 7) with shrinking bars. Not seen: the victim's own debuff icon (no bot drains) and a
spectator following a player.
Server check (`.codex-research/be-ctfclient-20261008-a/`, fresh `be-ctfclient-build-20261008-a`): profile off, Dantooine
laps 1-4 replay `be-force-20261008-a/off` exactly; profile on with `g_sendForceTimers 1`, a full 600 s world, no crash.

## Profile on by default (2026-10-08)

Owner: the competitive profile is now the default (g_xcvar.h); `tools/fixtures/competitive/stock_force_profile.cfg`
restores stock OpenJK Force. NewJK's `cl_fixSimultaneousInputs` is on by default too.

## NewJK ported (2026-10-08)

Owner: skip NewJK's netcode (local play), console and chat for now. Raw input needs nothing: xenjk's `in_mouse 1`
already sets the same SDL hint as NewJK's `in_raw`. Ported: `cl_fixSimultaneousInputs` (`codemp/client/nj_input.cpp`,
hooks marked `// NewJK:`, default 1 by the owner's choice, 0 is stock; manifest
`docs/ports/newjk.json`, followed by `tools/upstream_watch.py`). A usercmd carries one generic command, so stock keeps
only the last of several pressed in one frame; the fix queues the rest, one per packet. Checked on the private listen
server with `force_speed; force_rage` on one line: off, only rage starts; on, speed and then rage one server frame
later (`.codex-research/be-client-test-20261008-a/run5/`). Client engine only: no effect on bots or the dedicated server.
Checked (`.codex-research/be-defaults-20261008-a/`, fresh `be-defaults-build-20261008-a`): Dantooine laps seed 1 with no
settings replays `be-ctfclient-20261008-a/on/dan-1` exactly; with `stock_force_profile.cfg`'s values it replays `off/dan-1`.

## Fights, weapons and spawns ported (2026-10-08)

`codemp/game/be_combat.c`/`.h`, hooks marked `// base_enhanced:`, on by default like the Force set (stock values in
`stock_force_profile.cfg`; manifest entries in `docs/ports/base_enhanced.json`):

- Location damage mode 2 (`g_locationBasedDamage 2`): legs and feet 0.7, torso, arms and hands 1.0, head 1.3.
- Dropped-flag spawn protection: no respawn within 1024 u (and PVS) of a flag dropped in the last 10 s.
- Weapons: charge measured on the command clock (`g_fixWeaponChargeTime`), alt rockets home on a lock held in the
  last 500 ms (`g_improvedHoming`), disruptor alt through corpses, full ammo from respawning weapons, held fire
  cancelled after a disarm until released (5 s, game time), thermal direct hit 90, sabers one-shot mines and detpacks.
- Declined: `g_fixFlagPickup` (nearest player gets a contested flag): the community found it broken, upstream turned it
  off without a reason, and its positions come from different commands.

Checked (`.codex-research/be-combat-20261008-a/`, fresh `be-combat-build-20261008-a`): with the new settings at stock
values, Dantooine laps seed 1 replays `be-defaults-20261008-a/defaults/dan-1` exactly; on, a full world without a crash.
Deferred to the end (owner): the 8 Dantooine + 16 Kejim comparison of the bots under these defaults (defender rockets
now home more often; a runner's pull now stops a defender's held fire).

## Movement ported (2026-10-08)

`codemp/game/be_pmove.c`/`.h`: switches in the shared bg code, which the game, Bot2's forecast (it runs the real Pmove)
and the client all run. The game sets them from cvars and announces them in configstring 25 (base_enhanced's tokens
fwg, fjal, fdfa, fnas, f5as, and xenjk's own for the rest); the cgame predicts from that string, stock without it.
On by default: friction on players' heads, roll from 100 u/s, roll-under-ceiling fix, wall-grab fix, no force-land
lock, flip/DFA need jump, DEMP during a saber pull, no fire anim on an empty click, disruptor alt at 5 ammo; grip
rework 2's victim slow and the rage-recovery `<=` follow `g_gripRework 2` and `g_fixForceTimers`. Not taken:
pmove_float/gravity 760 (owner keeps 40/800), the pad double-jump cvar (stock upstream too), newmod's move disabling.
Checked (`.codex-research/be-move-20261008-a/`, fresh `be-move-build-20261008-a`): with the nine new settings 0,
Dantooine seed 1 replays the previous build's world; on, the 24 Kejim bench cases pass with zero parity mismatches.
Longer runs are queued in [overnight-tests.md](overnight-tests.md).

## Stats ported (2026-10-08)

base_enhanced's CTF stats (`codemp/game/be_stats.c`, `be_table.c`, `be_collections.c`; 44 one-line hooks marked
`// base_enhanced: stats (be_stats.c)`): the general, force, damage, weapon-given, weapon-taken and accuracy tables
with best-on-team colouring, the capture timeline, `/stats` (`/ctfstats`, `/stat`; `gen|dmg|acc|wpn [player|all]`,
`help`) also in intermission, rcon `stats`, tables at intermission (`g_autoStats`), CTF region time and base/chase/
offense detection. Bots count (marked BOT). Players are keyed by ja_guid instead of upstream's database sessions;
accounts, pugs, ratings, racemode and the machine-readable dump are dropped. `g_beStats 0` turns it off. Checked
(`.codex-research/be-stats-20261008-a/`): with stats on, Dantooine laps seed 1 replays the previous build's world
exactly (passive); `/stats` and `/stats dmg` print on a local CTF listen server.
