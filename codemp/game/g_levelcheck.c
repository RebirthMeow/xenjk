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

// g_levelcheck.c -- level-port checks (tools/levelport/levelcheck.py drop --engine): the engine's own player-box
// trace over a list of points, so the checks' offline trace is compared with the code pmove calls, not with a
// player's fall (setviewpos pushes, view offsets and drift made that comparison need allowances).

#include "g_local.h"

#define LEVELCHECK_LINE 384

// levelcheck_trace <in> <out>: each input line "x y z0 z1" traces the standing player box (bg_pmove.c playerMins,
// playerMaxs) from (x, y, z0) to (x, y, z1) with MASK_PLAYERSOLID, ignoring client 0 (the player running the check);
// each output line "x y z0 z1 endz fraction startsolid allsolid". A line of 13 numbers "sx sy sz ex ey ez minx miny
// minz maxx maxy maxz mask" is any sweep (the collision chunk and coordinate tests, docs/levelport-collision.md): its
// output line is the 13 inputs, then "endx endy endz fraction startsolid allsolid nx ny nz surfaceFlags contents
// entityNum", at full float precision. A line of 3 numbers is a point: "x y z contents". Paths are under the game
// folder of fs_homepath.
void Svcmd_LevelcheckTrace_f( void ) {
	static const vec3_t mins = { -15.0f, -15.0f, DEFAULT_MINS_2 }, maxs = { 15.0f, 15.0f, DEFAULT_MAXS_2 };
	char inName[MAX_QPATH] = { 0 }, outName[MAX_QPATH] = { 0 }, line[LEVELCHECK_LINE];
	fileHandle_t in, out;
	char *text, *p;
	int len, n = 0, t0, t1;

	trap->Argv( 1, inName, sizeof( inName ) );
	trap->Argv( 2, outName, sizeof( outName ) );
	if ( !inName[0] || !outName[0] ) {
		trap->Print( "usage: levelcheck_trace <in> <out>\n" );
		return;
	}
	len = trap->FS_Open( inName, &in, FS_READ );
	if ( !in || len <= 0 ) {
		trap->Print( "levelcheck_trace: cannot read %s\n", inName );
		return;
	}
	text = (char *)malloc( len + 1 );
	if ( !text ) {
		trap->FS_Close( in );
		trap->Print( "levelcheck_trace: cannot allocate %d bytes for %s\n", len + 1, inName );
		return;
	}
	trap->FS_Read( text, len, in );
	trap->FS_Close( in );
	text[len] = '\0';
	trap->FS_Open( outName, &out, FS_WRITE );
	if ( !out ) {
		trap->Print( "levelcheck_trace: cannot write %s\n", outName );
		free( text );
		return;
	}
	t0 = trap->Milliseconds();
	for ( p = text; *p; ) {
		char *end = strchr( p, '\n' );
		vec3_t start, stop, bmins, bmaxs;
		trace_t tr;
		int mask, got;
		if ( end )
			*end = '\0';
		got = sscanf( p, "%f %f %f %f %f %f %f %f %f %f %f %f %i", &start[0], &start[1], &start[2], &stop[0], &stop[1],
			&stop[2], &bmins[0], &bmins[1], &bmins[2], &bmaxs[0], &bmaxs[1], &bmaxs[2], &mask );
		if ( got == 13 ) {
			trap->Trace( &tr, start, bmins, bmaxs, stop, 0, mask, qfalse, 0, 0 );
			Com_sprintf( line, sizeof( line ), "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d "
				"%.9g %.9g %.9g %.9g %d %d %.9g %.9g %.9g %d %d %d\n", start[0], start[1], start[2], stop[0], stop[1],
				stop[2], bmins[0], bmins[1], bmins[2], bmaxs[0], bmaxs[1], bmaxs[2], mask, tr.endpos[0], tr.endpos[1],
				tr.endpos[2], tr.fraction, tr.startsolid, tr.allsolid, tr.plane.normal[0], tr.plane.normal[1],
				tr.plane.normal[2], tr.surfaceFlags, tr.contents, tr.entityNum );
			trap->FS_Write( line, strlen( line ), out );
			n++;
		} else if ( got == 3 ) {			// "x y z": the point's contents, as pmove and the game read them
			Com_sprintf( line, sizeof( line ), "%.9g %.9g %.9g %d\n", start[0], start[1], start[2],
				trap->PointContents( start, ENTITYNUM_NONE ) );
			trap->FS_Write( line, strlen( line ), out );
			n++;
		} else if ( got == 4 ) {
			stop[2] = stop[0];
			stop[0] = start[0];
			stop[1] = start[1];
			trap->Trace( &tr, start, mins, maxs, stop, 0, MASK_PLAYERSOLID, qfalse, 0, 0 );
			Com_sprintf( line, sizeof( line ), "%.3f %.3f %.3f %.3f %.3f %.6f %d %d\n", start[0], start[1], start[2],
				stop[2], tr.endpos[2], tr.fraction, tr.startsolid, tr.allsolid );
			trap->FS_Write( line, strlen( line ), out );
			n++;
		}
		if ( !end )
			break;
		p = end + 1;
	}
	t1 = trap->Milliseconds();          // the traces and their formatting, with file writes (buffered)
	trap->FS_Close( out );
	free( text );
	trap->Print( "levelcheck_trace: %d traces -> %s in %d ms\n", n, outName, t1 - t0 );
}

// levelcheck_teleport <client> <x> <y> <z>: puts a client (a bot) there, as a teleporter does (the chunk tests'
// held bot, docs/levelport-collision.md section 20)
void Svcmd_LevelcheckTeleport_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	vec3_t origin, angles = { 0, 0, 0 };
	int i, num;

	if ( trap->Argc() != 5 ) {
		trap->Print( "usage: levelcheck_teleport <client> <x> <y> <z>\n" );
		return;
	}
	trap->Argv( 1, arg, sizeof( arg ) );
	num = atoi( arg );
	if ( num < 0 || num >= level.maxclients || !g_entities[num].inuse || !g_entities[num].client ) {
		trap->Print( "levelcheck_teleport: no client %d\n", num );
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		trap->Argv( 2 + i, arg, sizeof( arg ) );
		origin[i] = atof( arg );
	}
	TeleportPlayer( &g_entities[num], origin, angles );
	trap->Print( "levelcheck_teleport: client %d to %.1f %.1f %.1f\n", num, origin[0], origin[1], origin[2] );
}

// levelcheck_where <client>: its origin, velocity, ground entity, health and whether the server holds it
void Svcmd_LevelcheckWhere_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *ent;
	int num;

	trap->Argv( 1, arg, sizeof( arg ) );
	num = atoi( arg );
	if ( num < 0 || num >= level.maxclients || !g_entities[num].inuse || !g_entities[num].client ) {
		trap->Print( "levelcheck_where: no client %d\n", num );
		return;
	}
	ent = &g_entities[num];
	trap->Print( "levelcheck_where: client %d time %d origin %.3f %.3f %.3f velocity %.1f %.1f %.1f ground %d health %d "
		"held %d spawns %d connection %d\n", num, level.time, ent->client->ps.origin[0], ent->client->ps.origin[1],
		ent->client->ps.origin[2], ent->client->ps.velocity[0], ent->client->ps.velocity[1], ent->client->ps.velocity[2],
		ent->client->ps.groundEntityNum, ent->health, ( ent->r.svFlags & SVF_CHUNKHOLD ) ? 1 : 0,
		ent->client->ps.persistant[PERS_SPAWN_COUNT], ( ent->client->ps.eFlags & EF_CONNECTION ) ? 1 : 0 );
}

// levelcheck_fire <x> <y> <z> <dx> <dy> <dz> <speed>: a rocket from the world, as probe_rocket makes one, living 60 s
// (the chunk tests' projectile, docs/levelport-collision.md section 23); prints its entity number
static int levelcheckLastFired = -1;

void Svcmd_LevelcheckFire_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	vec3_t from, dir;
	float v[7];
	gentity_t *missile;
	int i;

	if ( trap->Argc() != 8 ) {
		trap->Print( "usage: levelcheck_fire <x> <y> <z> <dx> <dy> <dz> <speed>\n" );
		return;
	}
	for ( i = 0; i < 7; i++ ) {
		trap->Argv( 1 + i, arg, sizeof( arg ) );
		v[i] = atof( arg );
	}
	VectorSet( from, v[0], v[1], v[2] );
	VectorSet( dir, v[3], v[4], v[5] );
	VectorNormalize( dir );
	missile = CreateMissile( from, dir, v[6], 60000, &g_entities[ENTITYNUM_WORLD], qfalse );
	missile->classname = "rocket_proj";
	missile->s.weapon = WP_ROCKET_LAUNCHER;
	VectorSet( missile->r.maxs, 3, 3, 3 );
	VectorScale( missile->r.maxs, -1, missile->r.mins );
	missile->damage = 0;
	missile->splashDamage = 0;
	missile->clipmask = MASK_SHOT;
	levelcheckLastFired = missile->s.number;
	trap->Print( "levelcheck_fire: entity %d from %.1f %.1f %.1f\n", missile->s.number, from[0], from[1], from[2] );
}

// levelcheck_ent <num> | last: an entity's state, or the last fired rocket's (the rocket test samples it every frame)
void Svcmd_LevelcheckEnt_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *ent;
	int num;

	trap->Argv( 1, arg, sizeof( arg ) );
	num = Q_stricmp( arg, "last" ) ? atoi( arg ) : levelcheckLastFired;
	if ( num < 0 || num >= ENTITYNUM_WORLD ) {
		return;
	}
	ent = &g_entities[num];
	trap->Print( "levelcheck_ent: %d time %d inuse %d etype %d origin %.3f %.3f %.3f class %s\n", num, level.time,
		ent->inuse, ent->s.eType, ent->r.currentOrigin[0], ent->r.currentOrigin[1], ent->r.currentOrigin[2],
		ent->classname ? ent->classname : "" );
}
