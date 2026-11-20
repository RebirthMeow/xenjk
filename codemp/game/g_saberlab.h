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

// g_saberlab.h -- scripted saber fights for measurement (g_saberlab.c).
#pragma once

void Svcmd_SaberLab_f( void );
// Once a game frame, before level.time advances: deliver the fighters' usercmds.
void SaberLab_PreFrame( int levelTime );
// G_RunClient: a lab fighter takes its commands only from the lab, as a real client.
qboolean SaberLab_RunClient( gentity_t *ent );
// G_ShutdownGame: end a run cut by a map change or map_restart (closes the CSV, releases the fighters).
void SaberLab_Shutdown( void );
