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

#pragma once

// xenui: RmlUi documents in the UI module, beside the stock menus (docs/wishlist/ui-system.md). C interface for the C
// UI code; xenui.cpp is the only C++ and reaches the game through xenUIHost_t, which ui_xenui_host.c fills from trap.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xenUIHost_s {
	void	(*Print)( const char *text );
	int		(*Milliseconds)( void );
	int		(*FS_ReadFile)( const char *path, void **buffer );	// length, or -1; free with FS_FreeFile
	void	(*FS_FreeFile)( void *buffer );
	int		(*FS_WriteFile)( const char *path, const void *data, int length );
	int		(*FS_GetFileList)( const char *path, const char *extension, char *list, int size );
	void	(*Cmd_Execute)( const char *text );		// executed now
	int		(*CursorX)( void );						// the stock cursor, 640 x 480
	int		(*CursorY)( void );
	void	(*SetCatcher)( int on );				// KEYCATCH_UI for an open document
	const char *rendererModule;						// the loaded renderer's file name, e.g. rd-vulkan_x86.dll
} xenUIHost_t;

int		XenUI_Init( const xenUIHost_t *host );		// 0 when the renderer has no xenui interface (stock menus only)
void	XenUI_Shutdown( void );
int		XenUI_Active( void );						// a document is open: it takes input and draws over the menus
void	XenUI_Refresh( int realtime );
void	XenUI_FrameMark( int end );					// brackets the UI's menu drawing: ui_xenStats "ui frame", both looks
int		XenUI_KeyEvent( int key, int down );		// 1 when consumed
void	XenUI_MouseMove( void );
int		XenUI_ConsoleCommand( const char *cmd, int argc, const char **argv );	// 1 when it was a xenui command

// Data models: a tree set by path ("rows[3].word", "status.warn"), read by documents' data views ({{ rows[3].word }},
// data-for, data-class-*). Set a model's names before the documents that use it load. Events
// (data-event-click="row(3)") call onEvent with the arguments as strings and the mouse button (0 left, 1 right).
typedef void	( *xenUIEventFunc_t )( const char *event, int argc, const char **argv, int button );
int		XenUI_ModelCreate( const char *name, xenUIEventFunc_t onEvent );	// a handle, or 0
void	XenUI_ModelEvent( int model, const char *event );
void	XenUI_SetInt( int model, const char *path, int value );
void	XenUI_SetFloat( int model, const char *path, float value );
void	XenUI_SetBool( int model, const char *path, int value );
void	XenUI_SetString( int model, const char *path, const char *value );
void	XenUI_SetCount( int model, const char *path, int count );		// an array's length

// Overlays: a C screen draws a document over itself by calling this every frame it is open (1 when it shows); the
// document hides on the first frame without the call. The screen keeps the keys; XenUI_OverlayMouse gives it the
// mouse buttons (1 when an overlay took them).
int		XenUI_Overlay( const char *path );
int		XenUI_OverlayMouse( int key, int down );

// Underlays: a document drawn at the call, so under whatever the screen draws after it (its 3D stage), in a context of
// its own with its own models (no input). 1 when it drew; 0 without a version 2 renderer (the screen draws its own).
int		XenUI_UnderlayModelCreate( const char *name );
int		XenUI_Underlay( const char *path );

#ifdef __cplusplus
}
#endif
