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

// xenui's host (xenui.h): the UI module's engine imports for the C++ side, and the hooks ui_main.c calls

#include "ui_local.h"
#include "ui_shared.h"
#include "xenui.h"

static vmCvar_t	ui_xenui;
static char		xenRendererModule[MAX_OSPATH];

static void XenHost_Print( const char *text ) {
	trap->Print( "%s", text );
}

static int XenHost_Milliseconds( void ) {
	return trap->Milliseconds();
}

static int XenHost_FS_ReadFile( const char *path, void **buffer ) {
	fileHandle_t	f;
	const int		length = trap->FS_Open( path, &f, FS_READ );

	*buffer = NULL;
	if ( !f )
		return -1;
	if ( length < 0 ) {
		trap->FS_Close( f );
		return -1;
	}
	*buffer = malloc( length + 1 );
	if ( !*buffer ) {
		trap->FS_Close( f );
		return -1;
	}
	trap->FS_Read( *buffer, length, f );
	( (char *)*buffer )[length] = 0;
	trap->FS_Close( f );
	return length;
}

static void XenHost_FS_FreeFile( void *buffer ) {
	free( buffer );
}

static int XenHost_FS_WriteFile( const char *path, const void *data, int length ) {
	fileHandle_t f;

	trap->FS_Open( path, &f, FS_WRITE );
	if ( !f )
		return -1;
	trap->FS_Write( data, length, f );
	trap->FS_Close( f );
	return length;
}

static int XenHost_FS_GetFileList( const char *path, const char *extension, char *list, int size ) {
	return trap->FS_GetFileList( path, extension, list, size );
}

static void XenHost_Cmd_Execute( const char *text ) {
	trap->Cmd_ExecuteText( EXEC_NOW, text );	// now: an appended command can wait behind a scripted vstr loop
}

static int XenHost_CursorX( void ) {
	return uiInfo.uiDC.cursorx;
}

static int XenHost_CursorY( void ) {
	return uiInfo.uiDC.cursory;
}

static void XenHost_SetCatcher( int on ) {
	const int catcher = trap->Key_GetCatcher();

	if ( on )
		trap->Key_SetCatcher( catcher | KEYCATCH_UI );
	else if ( !Menu_GetFocused() )	// the stock menus keep the keys while one is open
		trap->Key_SetCatcher( catcher & ~KEYCATCH_UI );
}

static xenUIHost_t xenHost = {
	XenHost_Print,
	XenHost_Milliseconds,
	XenHost_FS_ReadFile,
	XenHost_FS_FreeFile,
	XenHost_FS_WriteFile,
	XenHost_FS_GetFileList,
	XenHost_Cmd_Execute,
	XenHost_CursorX,
	XenHost_CursorY,
	XenHost_SetCatcher,
	xenRendererModule,
};

void UI_XenUIInit( void ) {
	char renderer[MAX_OSPATH];	// cl_renderer can be a long relative path

	trap->Cvar_Register( &ui_xenui, "ui_xenui", "1", CVAR_ARCHIVE | CVAR_LATCH );	// xenjk: xenui (RmlUi) loads beside the stock menus (latched); 0 = the stock menus only
	if ( !ui_xenui.integer )
		return;

	// the renderer module by file name (the engine loads cl_renderer + "_" ARCH_STRING DLL_EXT)
	trap->Cvar_VariableStringBuffer( "cl_renderer", renderer, sizeof( renderer ) );
	Com_sprintf( xenRendererModule, sizeof( xenRendererModule ), "%s_" ARCH_STRING DLL_EXT, COM_SkipPath( renderer ) );
	XenUI_Init( &xenHost );
}

void UI_XenUIShutdown( void ) {
	XenUI_Shutdown();
}

qboolean UI_XenUIConsoleCommand( void ) {
	const char	*argv[8];
	char		args[8][MAX_STRING_CHARS];
	int			argc = trap->Cmd_Argc(), i;

	if ( argc < 1 )
		return qfalse;
	trap->Cmd_Argv( 0, args[0], sizeof( args[0] ) );
	if ( Q_stricmpn( args[0], "ui_xen", 6 ) )
		return qfalse;		// every command the UI sees comes here first: copy the rest only for xenui's own
	if ( argc > 8 )
		argc = 8;
	argv[0] = args[0];
	for ( i = 1; i < argc; i++ ) {
		trap->Cmd_Argv( i, args[i], sizeof( args[i] ) );
		argv[i] = args[i];
	}
	return (qboolean)XenUI_ConsoleCommand( argv[0], argc, argv );
}
