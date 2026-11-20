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

// cg_bench.c -- frame-time logger for the renderer bench (tools/renderer_scenes.py).
//
// "benchview <name> <frames>" records the wall time between the next <frames>
// client frames (a high-resolution clock, so GPU waits and the fps cap's sleep
// are included: frame-to-frame time, what PresentMon's FrameTime measures with
// vsync off) and appends one line to benchframes.txt in the homepath:
//   view <name> <count> <ms> <ms> ...
// The cgame is the same DLL under every engine the bench compares, so every
// renderer is timed by the same code. Idle unless the command is used.

#include "cg_local.h"

#ifdef _WIN32
__declspec(dllimport) int __stdcall QueryPerformanceCounter( long long *count );
__declspec(dllimport) int __stdcall QueryPerformanceFrequency( long long *freq );
#endif

#define BENCH_MAX_FRAMES 8192

static struct {
	char	name[64];
	int		wanted;
	int		count;
	double	last;
	float	ms[BENCH_MAX_FRAMES];
} bench;

static double CG_Bench_NowMs( void )
{
#ifdef _WIN32
	static long long freq;
	long long now;

	if ( !freq )
		QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &now );
	return (double)now * 1000.0 / (double)freq;
#else
	return (double)trap->Milliseconds();
#endif
}

static void CG_Bench_Write( void )
{
	fileHandle_t f;
	char num[16];
	int i;

	if ( trap->FS_Open( "benchframes.txt", &f, FS_APPEND ) < 0 || !f )
	{
		trap->Print( "benchview: cannot open benchframes.txt\n" );
		return;
	}
	{
		const char *head = va( "view %s %d", bench.name, bench.count );
		trap->FS_Write( head, strlen( head ), f );
	}
	for ( i = 0; i < bench.count; i++ )
	{
		Com_sprintf( num, sizeof( num ), " %.3f", bench.ms[i] );
		trap->FS_Write( num, strlen( num ), f );
	}
	trap->FS_Write( "\n", 1, f );
	trap->FS_Close( f );
	trap->Print( "benchview %s: %d frames\n", bench.name, bench.count );
}

// once per client frame, from CG_DrawActiveFrame
void CG_Bench_Frame( void )
{
	double now;

	if ( !bench.wanted )
		return;
	now = CG_Bench_NowMs();
	if ( bench.last > 0.0 )
		bench.ms[bench.count++] = (float)( now - bench.last );
	bench.last = now;
	if ( bench.count >= bench.wanted )
	{
		CG_Bench_Write();
		bench.wanted = 0;
	}
}

void CG_Bench_View_f( void )
{
	int frames;

	if ( trap->Cmd_Argc() < 3 )
	{
		trap->Print( "usage: benchview <name> <frames>\n" );
		return;
	}
	frames = atoi( CG_Argv( 2 ) );
	if ( frames < 1 ) frames = 1;
	if ( frames > BENCH_MAX_FRAMES ) frames = BENCH_MAX_FRAMES;
	Q_strncpyz( bench.name, CG_Argv( 1 ), sizeof( bench.name ) );
	bench.wanted = frames;
	bench.count = 0;
	bench.last = 0.0;
}

// "benchcam <x> <y> <z> <yaw> <pitch>" pins the rendered camera to that exact
// origin and angles (roll 0) until "benchcam off"; "benchcam <yaw> <pitch>"
// pins only the angles and keeps the eye the view code computed (the
// setviewpos origin plus the view height). Every renderer then draws the same
// view, where a timed +lookdown left the pitch to the frame rate.
// Only cg.refdef changes; the server-side spectator stays where setviewpos put
// it (so its snapshot carries the same entities).
static struct {
	qboolean	on;
	qboolean	keepOrigin;
	vec3_t		origin;
	vec3_t		angles;
} benchcam;

// from CG_DrawActiveFrame, right after CG_CalcViewValues
void CG_Bench_Camera( void )
{
	if ( !benchcam.on )
		return;
	if ( !benchcam.keepOrigin )
		VectorCopy( benchcam.origin, cg.refdef.vieworg );
	VectorCopy( benchcam.angles, cg.refdef.viewangles );
	AnglesToAxis( cg.refdef.viewangles, cg.refdef.viewaxis );
}

void CG_Bench_Cam_f( void )
{
	int i, argc = trap->Cmd_Argc();

	if ( argc == 2 && !Q_stricmp( CG_Argv( 1 ), "off" ) )
	{
		benchcam.on = qfalse;
		return;
	}
	if ( argc != 3 && argc != 6 )
	{
		trap->Print( "usage: benchcam [<x> <y> <z>] <yaw> <pitch> | benchcam off\n" );
		return;
	}
	// a detached camera sees past walls: a demo, or a server with cheats on (the bench loads with devmap)
	if ( !cg.demoPlayback && atoi( Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_cheats" ) ) == 0 )
	{
		trap->Print( "benchcam: needs sv_cheats 1 or a demo\n" );
		return;
	}
	benchcam.keepOrigin = ( argc == 3 );
	for ( i = 0; i < 3 && !benchcam.keepOrigin; i++ )
		benchcam.origin[i] = atof( CG_Argv( i + 1 ) );
	benchcam.angles[YAW] = atof( CG_Argv( argc - 2 ) );
	benchcam.angles[PITCH] = atof( CG_Argv( argc - 1 ) );
	benchcam.angles[ROLL] = 0.0f;
	benchcam.on = qtrue;
}
