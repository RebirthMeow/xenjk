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

// cg_be.c -- client side of base_enhanced's server protocol (see cg_be.h).
//
// What the server sends (base_enhanced 774700c; xenjk game/be_force.c):
// - configstring 25 (CS_SERVERFEATURELIST there, CS_BOTINFO here): space
//   separated tokens; "rwd" is drain rework 1, "rwd2" rework 2
//   (g_client.c G_BroadcastServerFeatureList).
// - "kls -1 -1 fpt <client> {<power> <full ms> <end ms since level start>}..."
//   and "kls -1 -1 sfpt <client>" (w_force.c SendForceTimers); rage recovery
//   is FP_SABER_OFFENSE. Stock kls ignores the -1 entity numbers.
// - drain rework: the drainer's activeForcePass is level + 3 for the shot
//   (ForceShootDrain), at level 3 too, which stock draws as the wide arc; and
//   EV_FORCE_DRAINED carries the victim's debuff (no regeneration, no team
//   energize) in s.time (ForceDrainDamage).
//
// cg_drawForceTimers 0 hides the timers; cg_beDrainVisual 0 draws stock drain
// visuals on a rework server. Without the server's data nothing changes.

#include "cg_local.h"
#include "cg_be.h"
#include "game/be_pmove.h"

#define BE_SHELL_ALPHA_MAX	( 1000 / 8 )	// stock's 1 s shell starts at 125
#define BE_DRAIN_LINE_MS	200				// the shot's hand extend (be_force.c BE_DRAIN_REWORK_HANDEXTEND)

static struct {
	int		drainRework;							// 0, 1 or 2, from CS 25
	int		timerEnd[MAX_CLIENTS][NUM_FORCE_POWERS];	// msec since level start, 0 = none
	int		timerFull[MAX_CLIENTS][NUM_FORCE_POWERS];
	int		debuffEnd[MAX_CLIENTS];					// cg.time
	int		debuffFull[MAX_CLIENTS];
	int		drainShotTime[MAX_CLIENTS];				// cg.time the rework's shot was last seen
} cgBe;

void CG_BE_Reset( void )
{
	memset( &cgBe, 0, sizeof( cgBe ) );
	CG_BE_ParseFeatures();
}

void CG_BE_ParseFeatures( void )
{
	const char *s = CG_ConfigString( CS_BOTINFO );
	char *token;

	cgBe.drainRework = 0;
	be_pmoveFeatures = 0;	// the shared bg code predicts stock unless the server says otherwise
	while ( s && *s )
	{
		token = COM_ParseExt( &s, qfalse );
		if ( !token[0] )
		{
			break;
		}
		if ( !Q_stricmp( token, "rwd2" ) )
		{
			cgBe.drainRework = 2;
		}
		else if ( !Q_stricmp( token, "rwd" ) && !cgBe.drainRework )
		{
			cgBe.drainRework = 1;
		}
		else
		{
			be_pmoveFeatures |= BE_PM_TokenFlag( token );	// movement switches (game/be_pmove.h)
		}
	}
}

qboolean CG_BE_ServerCommand( void )
{
	char verb[8];
	int client, i, argc = trap->Cmd_Argc();

	if ( argc < 5 || strcmp( CG_Argv( 1 ), "-1" ) || strcmp( CG_Argv( 2 ), "-1" ) )
	{
		return qfalse;
	}
	Q_strncpyz( verb, CG_Argv( 3 ), sizeof( verb ) );	// CG_Argv's buffer is shared
	if ( strcmp( verb, "fpt" ) && strcmp( verb, "sfpt" ) )
	{
		return qfalse;
	}
	client = atoi( CG_Argv( 4 ) );
	if ( client < 0 || client >= MAX_CLIENTS )
	{
		return qtrue;
	}
	memset( cgBe.timerEnd[client], 0, sizeof( cgBe.timerEnd[client] ) );
	for ( i = 5; !strcmp( verb, "fpt" ) && i + 2 < argc; i += 3 )
	{
		int power = atoi( CG_Argv( i ) );

		if ( power >= 0 && power < NUM_FORCE_POWERS )
		{
			cgBe.timerFull[client][power] = atoi( CG_Argv( i + 1 ) );
			cgBe.timerEnd[client][power] = atoi( CG_Argv( i + 2 ) );
		}
	}
	return qtrue;
}

static qboolean CG_BE_DrainVisual( void )
{
	return cgBe.drainRework && cg_beDrainVisual.integer;
}

qboolean CG_BE_DrainLineOnly( void )
{
	return CG_BE_DrainVisual();
}

// stock draws drain while activeForcePass is above level 3, the whole time the
// arc is held; the rework's shot sets it for one server frame (ForceShootDrain,
// then WP_ForcePowerStop), too short to see, so the line is held for the shot's
// hand extend.
qboolean CG_BE_DrainShown( centity_t *cent )
{
	int n = cent->currentState.number;

	if ( cent->currentState.activeForcePass > FORCE_LEVEL_3 )
	{
		if ( n >= 0 && n < MAX_CLIENTS )
		{
			cgBe.drainShotTime[n] = cg.time;
		}
		return qtrue;
	}
	// the hold gives way to lightning (activeForcePass 1-3), which the caller draws in the else branch
	return CG_BE_DrainVisual() && !cent->currentState.activeForcePass && n >= 0 && n < MAX_CLIENTS && cgBe.drainShotTime[n] &&
		cg.time - cgBe.drainShotTime[n] >= 0 && cg.time - cgBe.drainShotTime[n] < BE_DRAIN_LINE_MS;
}

int CG_BE_DrainedShellTime( const entityState_t *es )
{
	if ( !CG_BE_DrainVisual() || es->time <= 0 )
	{
		return 1000;	// stock
	}
	if ( es->owner >= 0 && es->owner < MAX_CLIENTS )
	{
		cgBe.debuffEnd[es->owner] = cg.time + es->time;
		cgBe.debuffFull[es->owner] = es->time;
	}
	return es->time;
}

// stock is remaining / 8 into a byte, which wraps past 2 s (a 4 s debuff would
// flicker); held at stock's peak it fades over the last second as stock does.
byte CG_BE_ShellAlpha( int remaining )
{
	int alpha = remaining / 8;

	if ( alpha > BE_SHELL_ALPHA_MAX )
	{
		alpha = BE_SHELL_ALPHA_MAX;
	}
	return (byte)( alpha < 0 ? 0 : alpha );
}

qboolean CG_BE_DrainDebuffed( void )
{
	int client = cg.snap ? cg.snap->ps.clientNum : -1;

	return client >= 0 && client < MAX_CLIENTS && CG_BE_DrainVisual() && cgBe.debuffEnd[client] > cg.time;
}

// seconds left (rounded up) over the bottom of an active-power icon, and a bar
// of the time left under it
static void CG_BE_DrawTimeLeft( int left, int full, int x, int y, int size, const vec4_t barColor )
{
	vec4_t back = { 0.0f, 0.0f, 0.0f, 0.5f };
	float frac;

	if ( !cg_drawForceTimers.integer || left <= 0 )
	{
		return;
	}
	CG_DrawProportionalString( x + size / 2, y + size - 18, va( "%d", ( left + 999 ) / 1000 ),
		UI_SMALLFONT | UI_CENTER | UI_DROPSHADOW, colorTable[CT_WHITE] );
	if ( full > 0 )
	{
		frac = (float)left / (float)full;
		if ( frac > 1.0f )
		{
			frac = 1.0f;
		}
		CG_FillRect( x, y + size + 1, size, 3, back );
		CG_FillRect( x, y + size + 1, size * frac, 3, barColor );
	}
	trap->R_SetColor( NULL );
}

void CG_BE_DrawPowerTimer( int power, int x, int y, int size )
{
	vec4_t bar = { 0.4f, 0.7f, 1.0f, 0.9f };
	int client = cg.snap->ps.clientNum;
	int left, full;

	if ( client < 0 || client >= MAX_CLIENTS || power < 0 || power >= NUM_FORCE_POWERS )
	{
		return;
	}
	if ( power == FP_SABER_OFFENSE )
	{ // rage recovery is in the playerState: exact on any server
		left = cg.snap->ps.fd.forceRageRecoveryTime - cg.time;
		full = 10000;	// RAGE_RECOVERY_TIME
		bar[0] = 0.6f; bar[1] = 0.6f; bar[2] = 0.6f;
	}
	else
	{
		if ( !cgBe.timerEnd[client][power] )
		{
			return;
		}
		left = cgBe.timerEnd[client][power] + cgs.levelStartTime - cg.time;
		full = cgBe.timerFull[client][power];
	}
	CG_BE_DrawTimeLeft( left, full, x, y, size, bar );
}

// the viewed player's drain debuff: the drain icon, tinted red, with its time
void CG_BE_DrawDrainDebuff( int x, int y, int size )
{
	vec4_t red = { 1.0f, 0.25f, 0.25f, 1.0f };
	int client = cg.snap->ps.clientNum;

	if ( !CG_BE_DrainDebuffed() || !cgs.media.forcePowerIcons[FP_DRAIN] )
	{
		return;
	}
	trap->R_SetColor( red );
	CG_DrawPic( x, y, size, size, cgs.media.forcePowerIcons[FP_DRAIN] );
	trap->R_SetColor( NULL );
	CG_BE_DrawTimeLeft( cgBe.debuffEnd[client] - cg.time, cgBe.debuffFull[client], x, y, size, red );
}
