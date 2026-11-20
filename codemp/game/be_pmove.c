/*
===========================================================================
Copyright (C) 2014 - 2026, base_enhanced contributors (TheSil, deathsythe47, Avygeil)
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative, and carries code ported from
base_enhanced (https://github.com/avygeil/base_enhanced, GPLv2).

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

// be_pmove.c -- base_enhanced movement changes in shared bg code (see be_pmove.h).
// Compiled into the game, cgame and ui modules.

#include "qcommon/q_shared.h"
#include "bg_public.h"
#include "be_pmove.h"

int be_pmoveFeatures = 0;

static const struct {
	const char *token;
	int flag;
} beTokens[] = {
	{ "fwg",	BE_PM_FIX_WALLGRAB },		// base_enhanced's tokens
	{ "fjal",	BE_PM_FORCELAND_FREE },
	{ "fdfa",	BE_PM_DFA_NEEDS_JUMP },
	{ "fnas",	BE_PM_NOAMMO_ANIM },
	{ "f5as",	BE_PM_FIVE_AMMO_SNIPE },
	{ "xhfr",	BE_PM_HEAD_FRICTION },		// xenjk's (base_enhanced does these always or without a token)
	{ "xr100",	BE_PM_ROLL_100 },
	{ "xfrl",	BE_PM_FIX_ROLL },
	{ "xgrs",	BE_PM_GRIP_SLOW },
	{ "xrre",	BE_PM_RAGE_END },
	{ "xdst",	BE_PM_DEMP_SABERTHROW },
};

// base_enhanced bg_pmove.c/bg_saber.c (g_fixForceTimers): wall runs, wall
// flips and the staff/dual specials unlock on the frame rage recovery ends
qboolean BE_PM_RageRecovered( int recoveryTime, int now )
{
	return BE_PM( BE_PM_RAGE_END ) ? recoveryTime <= now : recoveryTime < now;
}

// base_enhanced bg_pmove.c PM_Weapon (g_fixNoAmmoShootAnimation): melee and
// det packs keep stock's order; every other weapon's fire anim starts after
// the ammo check, so an empty click shows no shot
qboolean BE_PM_NoAmmoAnimDeferred( int weapon )
{
	return BE_PM( BE_PM_NOAMMO_ANIM ) && weapon != WP_MELEE && weapon != WP_DET_PACK;
}

int BE_PM_TokenFlag( const char *token )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( beTokens ); i++ )
	{
		if ( !Q_stricmp( token, beTokens[i].token ) )
			return beTokens[i].flag;
	}
	return 0;
}

void BE_PM_AppendTokens( int flags, char *buf, int size )
{
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( beTokens ); i++ )
	{
		if ( flags & beTokens[i].flag )
		{
			if ( buf[0] )
				Q_strcat( buf, size, " " );
			Q_strcat( buf, size, beTokens[i].token );
		}
	}
}
