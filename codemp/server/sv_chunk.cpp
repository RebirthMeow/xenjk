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

// sv_chunk.cpp -- the server side of collision chunks (docs/levelport-collision.md): residency around the clients
// before each game frame, and the hold of a client whose chunks are not in yet. Hooks: SV_Frame and map_restart
// call SV_ChunkFrame, SV_ClientThink tests SV_ChunkHeld, SV_ClientEnterWorld calls SV_ChunkClientEntered.

#include "server.h"
#include "qcommon/cm_public.h"

// collision chunks around a client entering play (SV_ChunkHeld): its last spawn count, when it entered, the loads
// landed and chunks missing then, and whether it is a respawn (not its first entry)
static int sv_cmSpawnCount[MAX_CLIENTS], sv_cmEnterTime[MAX_CLIENTS], sv_cmEnterLoads[MAX_CLIENTS], sv_cmWaited[MAX_CLIENTS];
static qboolean sv_cmEntering[MAX_CLIENTS], sv_cmRespawn[MAX_CLIENTS];

/*
==================
SV_ChunkHeld

qtrue while a client must not move: entering play (connect or respawn: PERS_SPAWN_COUNT moves) it waits for the chunks
around it, on its first entry those within cm_chunkSpawnNear as on a respawn (sv_chunkFirstEntryNear 1, the default:
the map load preloads that set at every spawn; 0: every one within the load distance, as
before), on a respawn only those within cm_chunkSpawnNear (the
chunk under it and the neighbours its first moves can reach; the rest stream in behind, nearest first, each a solid box
until it lands; 0: the whole region); in play it is held while the chunk it stands in is not swapped in yet, so it
neither moves nor falls through a floor still loading (docs/levelport-collision.md sections 19, 20 and 32).
SV_ClientThink skips a held client's usercmds; the frame flags it SVF_CHUNKHOLD for the game, whose own client runs
(bots) honour the flag
==================
*/
qboolean SV_ChunkHeld( int num )
{
	static cvar_t	*spawnNear, *firstNear;
	const playerState_t *ps = SV_GameClientNum( num );

	if ( !spawnNear ) {
		spawnNear = CM_ChunkSpawnNearCvar();
		firstNear = CM_ChunkFirstEntryNearCvar();
	}
	if ( ps->persistant[PERS_SPAWN_COUNT] != sv_cmSpawnCount[num] ) {
		sv_cmRespawn[num] = sv_cmSpawnCount[num] != -1 ? qtrue : qfalse;	// -1: its first think since entering
		sv_cmSpawnCount[num] = ps->persistant[PERS_SPAWN_COUNT];
		sv_cmEnterTime[num] = Sys_Milliseconds();
		sv_cmEnterLoads[num] = CM_ChunkLoadCount();
		sv_cmWaited[num] = -1;
		sv_cmEntering[num] = qtrue;
	}
	if ( sv_cmEntering[num] ) {
		const float hold = sv_cmRespawn[num] || firstNear->integer ? spawnNear->value : 0;	// 0: the whole region
		const int missing = CM_ChunksMissingNear( ps->origin, hold );
		if ( sv_cmWaited[num] < 0 ) {
			sv_cmWaited[num] = missing;
		}
		if ( missing ) {
			return qtrue;
		}
		CM_NoteSpawnHold( Sys_Milliseconds() - sv_cmEnterTime[num], sv_cmWaited[num],
			CM_ChunkLoadCount() - sv_cmEnterLoads[num], CM_ChunksMissingNear( ps->origin, spawnNear->value ),
			CM_ChunksDeadNear( ps->origin, hold ) );	// a given-up chunk releases the hold: counted apart
		sv_cmEntering[num] = qfalse;
	}
	return CM_ChunkReadyAt( ps->origin ) ? qfalse : qtrue;
}

// a client entering the world: its first think enters play, and a previous occupant's wait is not its own
void SV_ChunkClientEntered( int clientNum )
{
	sv_cmSpawnCount[clientNum] = -1;	// its first think enters play
	sv_cmEntering[clientNum] = qfalse;	// a previous occupant's wait is not this client's
}

/*
==================
SV_ChunkFrame

Collision chunk residency around the clients, before the game's frame traces (docs/levelport-collision.md sections 16
to 24), then the hold: a held client (SV_ChunkHeld) is flagged SVF_CHUNKHOLD for the game, which runs bots from their
last usercmd, and frozen in its playerstate (PM_FREEZE: its own prediction then stands still too; the game's next
think sets pm_type again). SV_Frame's game frames and map_restart's call it
==================
*/
// sv_chunkHoldFlag: 0 is the test's control, a client in a chunk still loading moves anyway (SV_ChunkFrame and
// SV_ClientThink)
qboolean SV_ChunkHoldEnabled( void ) {
	static cvar_t	*holdFlag;

	if ( !holdFlag ) {
		holdFlag = Cvar_Get( "sv_chunkHoldFlag", "1", CVAR_CHEAT, "Freeze a client while the chunk it stands in is loading (0 = the test control: it moves anyway)" );
	}
	return holdFlag->integer ? qtrue : qfalse;
}

void SV_ChunkFrame( void ) {
	vec3_t			origins[MAX_CLIENTS], velocities[MAX_CLIENTS];
	int				n = 0;
	const qboolean	hold = SV_ChunkHoldEnabled();
	for ( int ci = 0 ; ci < sv_maxclients->integer ; ci++ ) {
		if ( svs.clients[ci].state == CS_ACTIVE ) {
			VectorCopy( SV_GameClientNum( ci )->origin, origins[n] );
			VectorCopy( SV_GameClientNum( ci )->velocity, velocities[n] );
			n++;
		}
	}
	CM_ChunkResidency( origins, velocities, n );
	if ( !CM_HasChunks() ) {
		return;	// no chunks, no hold: SVF_CHUNKHOLD is left alone (a game module may use the bit for its own)
	}
	for ( int ci = 0 ; ci < sv_maxclients->integer ; ci++ ) {
		if ( svs.clients[ci].state == CS_ACTIVE ) {
			sharedEntity_t *ent = SV_GentityNum( ci );
			if ( SV_ChunkHeld( ci ) && hold ) {
				ent->r.svFlags |= SVF_CHUNKHOLD;
				SV_GameClientNum( ci )->pm_type = PM_FREEZE;
				CM_NoteClientHeld();
			} else {
				ent->r.svFlags &= ~SVF_CHUNKHOLD;
			}
		}
	}
}
