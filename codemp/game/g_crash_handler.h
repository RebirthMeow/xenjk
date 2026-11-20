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

// ==============================================================================
// g_crash_handler.h - Optional Win32 minidump crash handler.
//
// Installs a Vectored Exception Handler at game DLL init that writes a
// minidump (jampgame_crash.dmp) and a one-line crash log (jampgame_crash.txt)
// when the game DLL faults.  Useful while debugging mods that touch hot paths
// like pmove, navmesh, or AI.  Compiles to no-ops on non-Windows platforms.
//
// Originally lived inline in g_main.c; extracted to keep g_main.c closer to
// stock OpenJK so this fork is easier to drop into other derivatives.
// ==============================================================================
#ifndef __G_CRASH_HANDLER_H__
#define __G_CRASH_HANDLER_H__

void G_CrashHandler_Install( void );
void G_CrashHandler_Shutdown( void );

#endif
