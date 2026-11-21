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

// cg_director.h -- shot files for filming in the engine (cg_director.c): a camera on a keyframed path with a lens,
// and client-only actors with animations and somewhere to look. Hooks: CG_Director_Camera after CG_CalcViewValues,
// CG_Director_AddActors before the scene is rendered, CG_Director_Shutdown with the other Ghoul2 cleanup, CG_Director_HidesPlayer at the top of CG_Player, and the
// "director" command in cg_consolecmds.c.

#pragma once

void CG_Director_Camera( void );
void CG_Director_AddActors( void );
void CG_Director_Shutdown( void );
qboolean CG_Director_HidesPlayer( int clientNum );
void CG_Director_f( void );			// director
