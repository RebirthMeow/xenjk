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

// g_tick.c -- gameplay that does not depend on the server tickrate (see g_tick.h).

#include "g_local.h"
#include "g_tick.h"

int g_tickThinkDue;

qboolean G_TickOn( tickArea_t area ) {
	switch ( area ) {
	case TICK_SABERS:	return g_tickSabers.integer != 0;
	case TICK_FORCE:	return g_tickForce.integer != 0;
	case TICK_WEAPONS:	return g_tickWeapons.integer != 0;
	case TICK_ITEMS:	return g_tickItems.integer != 0;
	case TICK_SIEGE:	return g_tickSiege.integer != 0;
	case TICK_MAPS:		return g_tickMaps.integer != 0;
	case TICK_NPCS:		return g_tickNPCs.integer != 0;
	default:			return qfalse;
	}
}

int G_TickFrameMsec( void ) {
	return Q_max( 1, level.time - level.previousTime );
}

int G_TickNominalFrameMsec( void ) {
	char value[32];
	int fps = trap->Cvar_VariableIntegerValue( "sv_fps" );

	trap->Cvar_VariableStringBuffer( "timescale", value, sizeof( value ) );
	return Q_max( 1, (int)( ( 1000 / Q_max( 1, fps ) ) * atof( value ) ) );
}

int G_TickRearm( int deadline, int period ) {
	if ( period < 1 )
		period = 1;
	if ( deadline > 0 && level.time - deadline < period )
		return deadline + period;
	return level.time + period;
}

int G_TickTodayStrict( int ms ) {
	return ( ms / TICK_TODAY_MS + 1 ) * TICK_TODAY_MS;
}

int G_TickTodayCeil( int ms ) {
	return ( ( ms + TICK_TODAY_MS - 1 ) / TICK_TODAY_MS ) * TICK_TODAY_MS;
}

int G_TickTodayNested( int ms, int thinkMs ) {
	int think = Q_max( TICK_TODAY_MS, G_TickTodayCeil( thinkMs ) );
	int strict = G_TickTodayStrict( ms );

	return ( ( strict + think - 1 ) / think ) * think;
}

int G_TickTodayNestedNext( tickArea_t area, int deadline, int ms, int thinkMs ) {
	if ( !G_TickOn( area ) )
		return level.time + ms;
	return G_TickRearm( deadline, G_TickTodayNested( ms, thinkMs ) );
}

int G_TickTodayThink( tickArea_t area, int thinkMs ) {
	if ( !G_TickOn( area ) )
		return level.time + thinkMs;
	return G_TickNextThink( G_TickTodayCeil( thinkMs ) );
}

qboolean G_TickStrictDue( tickArea_t area, int deadline ) {
	if ( G_TickOn( area ) )
		return deadline <= level.time;
	return deadline < level.time;
}

int G_TickStrictNext( tickArea_t area, int deadline, int ms, qboolean today ) {
	if ( !G_TickOn( area ) )
		return level.time + ms;
	return G_TickRearm( deadline, today ? G_TickTodayStrict( ms ) : ms );
}

int G_TickNextThink( int period ) {
	return G_TickRearm( g_tickThinkDue, period );
}

#define TICK_FORCE_BUTTONS ( BUTTON_FORCEGRIP | BUTTON_FORCE_LIGHTNING | BUTTON_FORCE_DRAIN | BUTTON_FORCEPOWER )

void G_TickLatchForceButtons( gclient_t *client, const usercmd_t *cmd ) {
	client->tickForcePressed |= cmd->buttons & TICK_FORCE_BUTTONS;
	client->tickForceReleased |= ~cmd->buttons & TICK_FORCE_BUTTONS;
}

usercmd_t *G_TickForceCmd( gclient_t *client, usercmd_t *cmd, usercmd_t *scratch ) {
	int pressed = client->tickForcePressed, released = client->tickForceReleased;

	client->tickForcePressed = client->tickForceReleased = 0;
	if ( !G_TickOn( TICK_FORCE ) || ( !pressed && !released ) )
		return cmd;
	// a release inside the frame ends the hold even if the button is down again
	if ( released & BUTTON_FORCEPOWER )
		client->ps.fd.forceButtonNeedRelease = 0;
	*scratch = *cmd;
	scratch->buttons |= pressed;
	return scratch;
}

void G_TickSaberPass( void ) {
	int frame = G_TickFrameMsec();

	if ( !G_TickOn( TICK_SABERS ) || frame >= TICK_TODAY_MS || !level.saberPassNext
		|| level.time - level.saberPassNext > 4 * TICK_TODAY_MS || level.saberPassNext - level.time > 4 * TICK_TODAY_MS ) {
		// every frame is a pass; a stalled or restarted clock starts a new grid
		level.saberPassFrame = qtrue;
		level.saberPassNext = level.time + TICK_TODAY_MS;
	} else if ( level.time + frame / 2 >= level.saberPassNext ) {
		level.saberPassFrame = qtrue;
		level.saberPassNominal = level.saberPassNext;
		level.saberPassNext += TICK_TODAY_MS;
	} else {
		level.saberPassFrame = qfalse;
	}
	if ( level.saberPassFrame && ( !G_TickOn( TICK_SABERS ) || frame >= TICK_TODAY_MS ) )
		level.saberPassNominal = level.time;
	if ( level.saberPassFrame )
		level.saberPassTime = level.time;
}

// The engine's G2 clock during a frame is the previous frame's time, so a 30 Hz
// pass poses skeletons 33 ms back. On a faster tick, with the engine's clock
// import (sv_g2ClockImport), the pass poses them at its nominal grid time - 33.
static qboolean G2ClockImport( void ) {
	static int known = -1;

	if ( known < 0 )
		known = trap->Cvar_VariableIntegerValue( "sv_g2ClockImport" ) == 1;
	return known == 1;
}

static qboolean SaberClockShifted( void ) {
	return G_TickOn( TICK_SABERS ) && G_TickFrameMsec() < TICK_TODAY_MS && G2ClockImport();
}

void G_TickSaberClockBegin( void ) {
	if ( SaberClockShifted() )
		trap->G2API_SetTime( level.saberPassNominal - TICK_TODAY_MS, 0 );
}

void G_TickSaberClockEnd( void ) {
	if ( SaberClockShifted() )
		trap->G2API_SetTime( level.previousTime, 0 );
}

int G_TickSaberAnimTime( void ) {
	return SaberClockShifted() ? level.saberPassNominal : level.time;
}

int G_TickSaberLockKey( void ) {
	if ( G_TickOn( TICK_SABERS ) && level.saberPassTime )
		return level.saberPassTime;
	return level.time;
}

qboolean G_TickCrushDue( gentity_t *victim ) {
	if ( !G_TickOn( TICK_MAPS ) || !victim )
		return qtrue;
	if ( victim->tickCrushDue > level.time )
		return qfalse;
	victim->tickCrushDue = G_TickRearm( victim->tickCrushDue, TICK_DESIGN_MS );
	return qtrue;
}
