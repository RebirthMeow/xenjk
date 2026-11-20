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

// g_labs.c -- the measurement labs' hooks (see g_labs.h).

#include "g_local.h"
#include "g_labs.h"

#ifndef XENJK_NO_GAME_LABS

#include "g_saberlab.h"
#include "g_saberbench.h"
#include "g_hitcoverage.h"

void Svcmd_LevelcheckTrace_f( void );	// g_levelcheck.c
void Svcmd_LevelcheckTeleport_f( void );
void Svcmd_LevelcheckWhere_f( void );
void Svcmd_LevelcheckFire_f( void );
void Svcmd_LevelcheckEnt_f( void );

typedef struct {
	const char	*name;
	void		(*func)( void );
} labCommand_t;

static const labCommand_t labCommands[] = {
	{ "hitcoverage",			Svcmd_HitCoverage_f },
	{ "levelcheck_ent",			Svcmd_LevelcheckEnt_f },
	{ "levelcheck_fire",		Svcmd_LevelcheckFire_f },
	{ "levelcheck_teleport",	Svcmd_LevelcheckTeleport_f },
	{ "levelcheck_trace",		Svcmd_LevelcheckTrace_f },
	{ "levelcheck_where",		Svcmd_LevelcheckWhere_f },
	{ "saberbench",				Svcmd_SaberBench_f },
	{ "saberlab",				Svcmd_SaberLab_f },
};

void G_Labs_PreFrame( int levelTime ) {
	SaberLab_PreFrame( levelTime );
}

void G_Labs_Frame( void ) {
	G_SaberBenchFrame();
}

qboolean G_Labs_RunClient( gentity_t *ent ) {
	return SaberLab_RunClient( ent );
}

qboolean G_Labs_ConsoleCommand( const char *cmd ) {
	size_t i;

	for ( i = 0; i < ARRAY_LEN( labCommands ); i++ ) {
		if ( !Q_stricmp( cmd, labCommands[i].name ) ) {
			labCommands[i].func();
			return qtrue;
		}
	}
	return qfalse;
}

void G_Labs_Shutdown( void ) {
	SaberLab_Shutdown();
	G_SaberBenchShutdown();
}

#else // XENJK_NO_GAME_LABS

void G_Labs_PreFrame( int levelTime ) { (void)levelTime; }
void G_Labs_Frame( void ) { }
qboolean G_Labs_RunClient( gentity_t *ent ) { (void)ent; return qfalse; }
qboolean G_Labs_ConsoleCommand( const char *cmd ) { (void)cmd; return qfalse; }
void G_Labs_Shutdown( void ) { }

#endif // XENJK_NO_GAME_LABS
