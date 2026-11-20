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

// xenui without RmlUi (BuildXenUI off): the same hooks, stock menus only

#include "xenui.h"

int XenUI_Init( const xenUIHost_t *host ) { return 0; }
void XenUI_Shutdown( void ) {}
int XenUI_Active( void ) { return 0; }
void XenUI_Refresh( int realtime ) {}
void XenUI_FrameMark( int end ) {}
int XenUI_KeyEvent( int key, int down ) { return 0; }
void XenUI_MouseMove( void ) {}
int XenUI_ConsoleCommand( const char *cmd, int argc, const char **argv ) { return 0; }
int XenUI_ModelCreate( const char *name, xenUIEventFunc_t onEvent ) { return 0; }
void XenUI_ModelEvent( int model, const char *event ) {}
void XenUI_SetInt( int model, const char *path, int value ) {}
void XenUI_SetFloat( int model, const char *path, float value ) {}
void XenUI_SetBool( int model, const char *path, int value ) {}
void XenUI_SetString( int model, const char *path, const char *value ) {}
void XenUI_SetCount( int model, const char *path, int count ) {}
int XenUI_Overlay( const char *path ) { return 0; }
int XenUI_OverlayMouse( int key, int down ) { return 0; }
int XenUI_UnderlayModelCreate( const char *name ) { return 0; }
int XenUI_Underlay( const char *path ) { return 0; }
