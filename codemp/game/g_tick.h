/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// g_tick.h -- gameplay that does not depend on the server tickrate.
//
// Game timers and per-frame amounts give the same
// results at any sv_fps. Timers lock to the millisecond values written in the
// code; a per-frame amount with no millisecond value is read at the 20 Hz rate
// the game shipped with (TICK_DESIGN_MS). Five areas keep today's sv_fps 30
// behaviour instead (TICK_TODAY_MS): sabers, lightning cost, shortBurstJetpack,
// Force drains and upkeep, and siege healing, supply and turrets.
//
// One switch per area (g_tick* in g_xcvar.h, default on); 0 restores the stock
// frame-tied code. Stock files call in through small hooks marked
// "// tick: <area> (g_tick.c)". Design and inventory: docs/gameplay/TICK_INDEPENDENCE.md.

#pragma once

typedef enum {
	TICK_SABERS,
	TICK_FORCE,
	TICK_WEAPONS,
	TICK_ITEMS,		// use key, stations, items, client timers
	TICK_SIEGE,
	TICK_MAPS,		// movers, triggers, map entities, ICARUS
	TICK_NPCS,
	TICK_AREAS
} tickArea_t;

#define TICK_DESIGN_MS	50	// sv_fps 20, the shipped rate
#define TICK_TODAY_MS	33	// sv_fps 30 as the engine runs it (1000 / 30, integer)

qboolean G_TickOn( tickArea_t area );

// This frame's length in ms.
int G_TickFrameMsec( void );
// The configured game step in ms: 1000 / sv_fps (integer), scaled by timescale, at least 1.
int G_TickNominalFrameMsec( void );

// The next deadline of a periodic timer that has just fired at or after `deadline`.
// While the timer runs continuously it keeps its phase, so its average period is
// exactly `period` at any tickrate; after an idle gap of a period or more it restarts
// from now, as the stock re-arm does.
int G_TickRearm( int deadline, int period );

// A timer's period as sv_fps 30 runs it today: a strict re-arm (x < level.time,
// then x = level.time + ms) and a deadline check (x <= level.time).
int G_TickTodayStrict( int ms );
int G_TickTodayCeil( int ms );

// A strict timer checked only on a think every thinkMs: its period as sv_fps 30 runs
// it today, the first 30 Hz think at or after its 30 Hz strict period.
int G_TickTodayNested( int ms, int thinkMs );
// Today's nested period, deadline-kept with the area on; the stock re-arm with it off.
int G_TickTodayNestedNext( tickArea_t area, int deadline, int ms, int thinkMs );
// nextthink for a think that repeats every thinkMs: today's 30 Hz period with the
// area on, the stock level.time + thinkMs with it off.
int G_TickTodayThink( tickArea_t area, int thinkMs );

// The stock strict timer: due when deadline < level.time, re-armed level.time + ms.
// With the area on it is due at the deadline and re-armed by G_TickRearm, at the
// code's `ms` or, with `today`, at the period sv_fps 30 gives it (G_TickTodayStrict).
qboolean G_TickStrictDue( tickArea_t area, int deadline );
int G_TickStrictNext( tickArea_t area, int deadline, int ms, qboolean today );

// The think a G_RunThink call is running was due at this time (0 outside a think).
extern int g_tickThinkDue;
// nextthink for a think that repeats every `period` ms, phase kept.
int G_TickNextThink( int period );

// Force buttons are read once a frame from the last usercmd, so a tap or a
// release and re-press shorter than a frame was lost. ClientThink latches every
// usercmd's Force buttons; WP_ForcePowersUpdate reads the frame's command through
// G_TickForceCmd, which adds the latched presses and honours a latched release.
void G_TickLatchForceButtons( gclient_t *client, const usercmd_t *cmd );
usercmd_t *G_TickForceCmd( gclient_t *client, usercmd_t *cmd, usercmd_t *scratch );

// Sabers keep today's sv_fps 30 behaviour by running the whole saber pass
// (WP_SaberPositionUpdate's traces, damage and clashes) on a TICK_TODAY_MS grid:
// every frame at 30 Hz or slower, the frame nearest each 33 ms deadline above.
// Called once a frame after level.time advances.
void G_TickSaberPass( void );
// Saber collision poses skeletons at the engine's Ghoul2 clock, the previous frame's
// time: 33 ms back at 30 Hz, 8 at 125. With the engine's clock import a faster tick's
// pass poses them at its nominal grid time - 33, and new animations start at that
// nominal time, so every pass sees the pose a 30 Hz server shows. Begin/End bracket
// the pass; both are no-ops at 30 Hz, with the switch off or without the import.
void G_TickSaberClockBegin( void );
void G_TickSaberClockEnd( void );
// The start time the server skeleton gives a new animation.
int G_TickSaberAnimTime( void );
// The time key saber-lock presses register against: one press per pass.
int G_TickSaberLockKey( void );

// A mover or rotator that damages `victim` each blocked frame: true when this frame
// deals it, on a TICK_DESIGN_MS grid per victim (the shipped 20 Hz rate).
qboolean G_TickCrushDue( gentity_t *victim );
