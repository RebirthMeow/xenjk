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

/*
=========================================================================
Concussion-alt trace visualization

Shows the crosshair/eye axis beside the real MP concussion-alt trace,
which begins at the snapped weapon muzzle (+6 right, -6 world Z) and
uses a swept +/-1 box.
=========================================================================
*/

#pragma once

void CG_ConcTraceVisual_OnEntityEvent( centity_t *cent, int event );
void CG_ConcTraceVisual_AddWorld( void );
void CG_ConcTraceVisual_Draw( void );

