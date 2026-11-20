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

// cg_signals.c -- the sounds of a level port's pedestrian crossing signals (levelport Kabuki).
//
// maps/<map>.signals (tools/levelport/cp2gltf.py SceneSignals; the renderer lights the lamps from the same file and
// clock) lists each signal's cycle: its group's program (G|Y|R, seconds) from its intersection's t = 0 plus that
// intersection's offset. A crossing plays the game's own Wwise events (sfx_container.bnk), repeated at their fixed
// trigger rates while its state lasts:
//   walk (green, and on through the yellow's blinking hand): the tick bed every 3.0 s, its chirp every 3.5 s from
//     1.5 s in, 10 times
//   don't walk (red): the tone every 3.5 s, its chirp every 3.5 s from 1.5 s in, 10 times
// The sounds are the file's own: its `sound <role> <path>` lines (walk_base, walk_layer, dontwalk_base,
// dontwalk_layer; docs/levelport-scene.md); a role the file does not name is silent, and a file without them plays
// nothing. The clock is the game's (cg.time, the renderer's refdef time) plus r_sceneSignalShift, so lamps and sounds
// agree. Only crossings within SIG_RANGE (the game's 40 m) of the view play. Idle on maps without the file.

#include "cg_local.h"

#define SIG_MAX			256
#define SIG_SEGS		8
#define SIG_RANGE		1500.0f		// 40 m: the sound component's maxPlayDistance
#define SIG_MAX_STEP	0.5f		// a longer gap (a pause, a level change) restarts the schedule instead of catching up

typedef struct {
	vec3_t	origin;
	float	cycle, offset;
	int		numSegs;
	int		colour[SIG_SEGS];		// 0 green, 1 yellow, 2 red
	float	seconds[SIG_SEGS];
	float	last;					// the cycle time sounds were scheduled up to (-1: not yet)
} cgSignal_t;

typedef struct { float delay, period; int count; int sound; } sigSeries_t;	// count 0: until the state ends

enum { SND_WALK_BASE, SND_WALK_LAYER, SND_DONTWALK_BASE, SND_DONTWALK_LAYER, SND_COUNT };
static const char *sigSoundRoles[SND_COUNT] = { "walk_base", "walk_layer", "dontwalk_base", "dontwalk_layer" };
static const sigSeries_t walkSeries[2] = { { 0.0f, 3.0f, 0, SND_WALK_BASE }, { 1.5f, 3.5f, 10, SND_WALK_LAYER } };
static const sigSeries_t dontWalkSeries[2] = { { 0.0f, 3.5f, 0, SND_DONTWALK_BASE }, { 1.5f, 3.5f, 10, SND_DONTWALK_LAYER } };

static struct {
	char			map[MAX_QPATH];		// the map the table was read for
	int				numSignals;
	cgSignal_t		signals[SIG_MAX];
	char			soundNames[SND_COUNT][MAX_QPATH];	// the file's sound lines ("" = none)
	sfxHandle_t		sounds[SND_COUNT];					// 0 = silent
	int				lastTime;
} sig;

static void CG_Signals_Load( void )
{
	static char text[65536];
	char name[MAX_QPATH], *line;
	fileHandle_t f;
	int len, named = 0;

	Q_strncpyz( sig.map, cgs.mapname, sizeof( sig.map ) );
	sig.numSignals = 0;
	memset( sig.soundNames, 0, sizeof( sig.soundNames ) );
	memset( sig.sounds, 0, sizeof( sig.sounds ) );
	COM_StripExtension( cgs.mapname, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".signals" );
	len = trap->FS_Open( name, &f, FS_READ );
	if ( len <= 0 || !f )
	{
		if ( f )
			trap->FS_Close( f );
		return;
	}
	if ( len >= (int)sizeof( text ) )
		len = sizeof( text ) - 1;
	trap->FS_Read( text, len, f );
	trap->FS_Close( f );
	text[len] = '\0';
	for ( line = strchr( text, '\n' ); line && sig.numSignals < SIG_MAX; line = strchr( line + 1, '\n' ) )
	{
		cgSignal_t *s = &sig.signals[sig.numSignals];
		char kind[16], c[4];
		int slot, n, used, k;
		const char *seg;
		char role[32], path[MAX_QPATH];

		if ( !Q_strncmp( line + 1, "sound ", 6 ) )
		{	// sound <role> <path>
			if ( sscanf( line + 7, "%31s %63s", role, path ) == 2 )
			{
				for ( k = 0; k < SND_COUNT && Q_stricmp( role, sigSoundRoles[k] ); k++ )
					;
				if ( k < SND_COUNT )
					Q_strncpyz( sig.soundNames[k], path, sizeof( sig.soundNames[k] ) );
			}
			continue;
		}
		if ( sscanf( line + 1, "%d %15s %f %f %f %f %f %d%n", &slot, kind, &s->cycle, &s->offset, &s->origin[0],
			&s->origin[1], &s->origin[2], &n, &used ) != 8 || Q_stricmp( kind, "crossing" ) || s->cycle <= 0.0f )
			continue;	// car lights make no sound
		seg = line + 1 + used;
		for ( k = 0; k < n && k < SIG_SEGS; k++ )
		{
			float sec;
			if ( sscanf( seg, " %3s %f%n", c, &sec, &used ) != 2 )
				break;
			seg += used;
			s->colour[k] = c[0] == 'G' ? 0 : c[0] == 'Y' ? 1 : 2;
			s->seconds[k] = sec;
		}
		s->numSegs = k;
		s->last = -1.0f;
		if ( k )
			sig.numSignals++;
	}
	for ( len = 0; len < SND_COUNT && sig.numSignals; len++ )
	{
		if ( sig.soundNames[len][0] && ( sig.sounds[len] = trap->S_RegisterSound( sig.soundNames[len] ) ) != 0 )
			named++;
	}
	if ( !named )
		sig.numSignals = 0;	// no sounds named: nothing to play
	trap->Print( "signals: %d crossings, %d of %d sounds (%s)\n", sig.numSignals, named, SND_COUNT, name );
}

// the walk and don't-walk stretches of one cycle, from their first segment: walk from green's start to red's, don't
// walk from red's start to green's (wrapping); fires every series event of each in (a, b] (cycle time, b - a small)
static void CG_Signals_Fire( const cgSignal_t *s, float a, float b )
{
	float start[SIG_SEGS];
	float t = 0.0f;
	int k, i, wrap;

	for ( k = 0; k < s->numSegs; k++ )
	{
		start[k] = t;
		t += s->seconds[k];
	}
	for ( k = 0; k < s->numSegs; k++ )
	{
		// a stretch begins where the walk / don't-walk side changes (yellow belongs to walk)
		const int walk = s->colour[k] != 2;
		const int prev = s->colour[( k + s->numSegs - 1 ) % s->numSegs] != 2;
		float end;
		const sigSeries_t *series;

		if ( s->numSegs > 1 && walk == prev )
			continue;
		// its end: the next segment on the other side
		end = start[k] + s->cycle;
		for ( i = 1; i < s->numSegs; i++ )
		{
			const int j = ( k + i ) % s->numSegs;
			if ( ( s->colour[j] != 2 ) != walk )
			{
				end = start[j] + ( j < k ? s->cycle : 0.0f );
				break;
			}
		}
		series = walk ? walkSeries : dontWalkSeries;
		for ( wrap = -1; wrap <= 0; wrap++ )	// a stretch begun last cycle may still be running
		{
			const float s0 = start[k] + wrap * s->cycle, e0 = end + wrap * s->cycle;
			for ( i = 0; i < 2; i++ )
			{
				const sigSeries_t *S = &series[i];
				int n;
				// the first event at or after a: s0 + delay + n * period > a
				n = (int)floorf( ( a - s0 - S->delay ) / S->period ) + 1;
				if ( n < 0 )
					n = 0;
				for ( ; S->count == 0 || n < S->count; n++ )
				{
					const float at = s0 + S->delay + n * S->period;
					if ( at > b || at >= e0 )
						break;
					if ( at > a && sig.sounds[S->sound] )
						trap->S_StartSound( s->origin, ENTITYNUM_WORLD, CHAN_AUTO, sig.sounds[S->sound] );
				}
			}
		}
	}
}

// once per client frame, from CG_DrawActiveFrame
void CG_Signals_Frame( void )
{
	char shift[32];
	double now;		// seconds of level time: a float holds 30-60 ms steps on a server up for days
	float step;
	int i;

	if ( Q_stricmp( sig.map, cgs.mapname ) )
		CG_Signals_Load();
	if ( !sig.numSignals || !cg.snap )
		return;
	trap->Cvar_VariableStringBuffer( "r_sceneSignalShift", shift, sizeof( shift ) );
	now = cg.time * 0.001 + atof( shift );	// vk_scene.cpp Scene_SignalStates keeps the same clock
	step = ( cg.time - sig.lastTime ) * 0.001f;
	sig.lastTime = cg.time;
	for ( i = 0; i < sig.numSignals; i++ )
	{
		cgSignal_t *s = &sig.signals[i];
		float t = (float)fmod( now + s->offset, s->cycle );

		if ( t < 0.0f )
			t += s->cycle;
		if ( DistanceSquared( s->origin, cg.refdef.vieworg ) > SIG_RANGE * SIG_RANGE )
		{
			s->last = -1.0f;
			continue;
		}
		if ( s->last >= 0.0f && step > 0.0f && step < SIG_MAX_STEP )
		{
			if ( t >= s->last )
				CG_Signals_Fire( s, s->last, t );
			else
			{
				CG_Signals_Fire( s, s->last, s->cycle );	// across the cycle's end
				CG_Signals_Fire( s, -0.001f, t );
			}
		}
		s->last = t;
	}
}
