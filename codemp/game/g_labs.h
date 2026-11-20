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

// g_labs.h -- the game module's measurement labs, behind one set of hooks.
//
// The labs are console-only test fixtures: the saber lab (g_saberlab.c), the
// saber damage bench (g_saberbench.c), the direct-hit coverage probe
// (g_hitcoverage.c) and the level-port trace checks (g_levelcheck.c). Stock
// files call the five functions below and nothing else of theirs. With the
// CMake option BuildGameLabs OFF (default ON) the labs are not compiled; the
// hooks then do nothing and the lab commands are unknown.
#pragma once

// G_RunFrame, before level.time advances: the saber lab's fighter packets.
void G_Labs_PreFrame( int levelTime );
// G_RunFrame, after level.time advances: the saber bench's pinned target.
void G_Labs_Frame( void );
// G_RunClient: true when a lab owns this client's commands (skip the rest).
qboolean G_Labs_RunClient( gentity_t *ent );
// ConsoleCommand: true when `cmd` is a lab command (it has run).
qboolean G_Labs_ConsoleCommand( const char *cmd );
// G_ShutdownGame: end a lab run and release what the labs hold.
void G_Labs_Shutdown( void );
