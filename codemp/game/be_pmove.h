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

// be_pmove.h -- base_enhanced movement changes in shared bg code, quarantined.
//
// bg code runs in the game (authoritative, and Bot2's forecast through the
// real Pmove) and in the cgame (prediction), so a switch must reach both:
// the game sets be_pmoveFeatures from its cvars (be_force.c) and announces
// them in configstring 25 with base_enhanced's tokens where it has them
// (fwg, fjal, fdfa, fnas, f5as; others xenjk's own); the cgame reads that
// string (cgame/cg_be.c). With no tokens the client predicts stock.
// Stock bg files call in through hooks marked "// base_enhanced:".

#pragma once

#define BE_PM_HEAD_FRICTION		(1 << 0)	// g_beHeadFriction: normal friction on players' heads
#define BE_PM_ROLL_100			(1 << 1)	// g_beRoll100: crouch roll from 100 u/s (stock 200)
#define BE_PM_FIX_ROLL			(1 << 2)	// g_fixRoll: a roll under a low ceiling becomes a crouch
#define BE_PM_FIX_WALLGRAB		(1 << 3)	// g_fixWallgrab: forceJumpZStart 0 counts as unset
#define BE_PM_FORCELAND_FREE	(1 << 4)	// g_fixForceJumpAnimationLock: move during a force-land anim
#define BE_PM_DFA_NEEDS_JUMP	(1 << 5)	// g_beDfaJumpCheck: flip/DFA specials need jump
#define BE_PM_GRIP_SLOW			(1 << 6)	// g_gripRework 2: a gripped player always x0.2
#define BE_PM_RAGE_END			(1 << 7)	// g_fixForceTimers: rage recovery over at <=
#define BE_PM_DEMP_SABERTHROW	(1 << 8)	// g_fixDempSaberThrow: weaponTime counts down in a saber pull
#define BE_PM_NOAMMO_ANIM		(1 << 9)	// g_fixNoAmmoShootAnimation: no fire anim on an empty click
#define BE_PM_FIVE_AMMO_SNIPE	(1 << 10)	// g_fix5AmmoSniping: disruptor alt at exactly 5 ammo

extern int be_pmoveFeatures;	// be_pmove.c; game: from cvars; cgame: from CS 25

#define BE_PM( f ) ( ( be_pmoveFeatures & ( f ) ) != 0 )

qboolean	BE_PM_RageRecovered( int recoveryTime, int now );		// stock: recoveryTime < now
qboolean	BE_PM_NoAmmoAnimDeferred( int weapon );				// the fire anim waits for the ammo check
int			BE_PM_TokenFlag( const char *token );				// flag for a CS 25 token, 0 if none
void		BE_PM_AppendTokens( int flags, char *buf, int size );	// tokens for flags, space separated
