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

// cg_be.h -- client side of base_enhanced's server protocol, quarantined.
//
// Draws what a base_enhanced server (or xenjk's port of it, game/be_force.c)
// sends: the feature list in configstring 25, force timers in "kls -1 -1 fpt",
// and the drain rework's line and debuff. Built from base_enhanced's open
// server source only; newmod (closed) is never used or inspected. Stock
// files call in through one-line hooks marked "// base_enhanced protocol:".
// Upstream functions: docs/ports/base_enhanced.json.

#pragma once

void		CG_BE_Reset( void );
void		CG_BE_ParseFeatures( void );				// CS 25 (CS_BOTINFO) changed
qboolean	CG_BE_ServerCommand( void );				// "kls -1 -1 <verb>": qtrue when handled
qboolean	CG_BE_DrainLineOnly( void );				// rework: one line at every level, no arc
qboolean	CG_BE_DrainShown( centity_t *cent );		// draw cent's drain effect this frame
int			CG_BE_DrainedShellTime( const entityState_t *es );	// EV_FORCE_DRAINED: the red shell's length
byte		CG_BE_ShellAlpha( int remaining );			// team-power shell alpha, clamped to stock's 1 s maximum
qboolean	CG_BE_DrainDebuffed( void );				// the viewed player is drain-debuffed
void		CG_BE_DrawPowerTimer( int power, int x, int y, int size );
void		CG_BE_DrawDrainDebuff( int x, int y, int size );
