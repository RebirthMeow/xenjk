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

/*
roster_driver.c: codemp/ui/ui_roster.c outside the game, for roster_check.py.

	roster_driver <entries.txt> <cfgdir>

entries.txt holds the model grid's entries, one "folder/skin" a line, in grid order; <cfgdir>/<folder>.cfg is that
folder's character.cfg. Prints the roster in the format of the UI's ui_roster command:

	C <index> <id> <source> <order> <variants> "<name>"
	V <character> <folder> <default skin> <red skin or -> <blue skin or -> "<name>"
	T <tab> <tag> <sourceorder> "<label>" "<name>"
	S hidden <n> dropped <n>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ui/ui_roster.h"

static char cfgDir[1024];
static char entries[ROSTER_MAX_VARIANTS * 2][64];
static roster_t roster;

void QDECL Com_Error( int level, const char *error, ... ) {
	va_list ap;
	va_start( ap, error );
	vfprintf( stderr, error, ap );
	va_end( ap );
	exit( 2 );
}

void QDECL Com_Printf( const char *msg, ... ) {
	va_list ap;
	va_start( ap, msg );
	vfprintf( stderr, msg, ap );
	va_end( ap );
}

static qboolean ReadCfg( const char *folder, char *buf, int size ) {
	char path[1200];
	FILE *f;
	size_t n;

	snprintf( path, sizeof( path ), "%s/%s.cfg", cfgDir, folder );
	f = fopen( path, "rb" );
	if ( !f ) {
		return qfalse;
	}
	n = fread( buf, 1, size - 1, f );
	buf[n] = '\0';
	fclose( f );
	return qtrue;
}

int main( int argc, char **argv ) {
	char line[256];
	int n = 0, i;
	FILE *f;

	if ( argc < 3 ) {
		fprintf( stderr, "usage: roster_driver <entries.txt> <cfgdir>\n" );
		return 1;
	}
	Q_strncpyz( cfgDir, argv[2], sizeof( cfgDir ) );
	f = fopen( argv[1], "rb" );
	if ( !f ) {
		fprintf( stderr, "cannot read %s\n", argv[1] );
		return 1;
	}
	while ( fgets( line, sizeof( line ), f ) && n < (int)( sizeof( entries ) / sizeof( entries[0] ) ) ) {
		line[strcspn( line, "\r\n" )] = '\0';
		if ( line[0] ) {
			Q_strncpyz( entries[n++], line, sizeof( entries[0] ) );
		}
	}
	fclose( f );

	UI_RosterBuild( &roster, (const char ( * )[64])entries, n, ReadCfg );
	for ( i = 0; i < roster.numCharacters; i++ ) {
		const rosterCharacter_t *c = &roster.characters[i];
		int v;
		printf( "C %d %s %s %d %d \"%s\"\n", i, c->id, c->source, c->order, c->numVariants, c->name );
		for ( v = c->firstVariant; v < c->firstVariant + c->numVariants; v++ ) {
			const rosterVariant_t *rv = &roster.variants[v];
			printf( "V %d %s %s %s %s \"%s\"\n", rv->character, rv->folder, rv->skin[ROSTER_TEAM_DEFAULT],
				rv->skin[ROSTER_TEAM_RED][0] ? rv->skin[ROSTER_TEAM_RED] : "-",
				rv->skin[ROSTER_TEAM_BLUE][0] ? rv->skin[ROSTER_TEAM_BLUE] : "-", rv->name );
		}
	}
	for ( i = 0; i < roster.numSources; i++ ) {
		const rosterSource_t *s = &roster.sources[i];
		printf( "T %d %s %d \"%s\" \"%s\"\n", i, s->tag, s->order, s->label, s->name );
	}
	printf( "S hidden %d dropped %d\n", roster.numHidden, roster.numDropped );
	return 0;
}
