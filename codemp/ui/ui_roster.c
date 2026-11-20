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
The character picker's roster (ui_roster.h). character.cfg, every key optional, one per line:

	character	aerith				// the character this folder is a variant of (default: the folder's own name)
	name		"Aerith"			// the character's display name (the first folder of a character that has one)
	variant		"Tux"				// this folder's default-skin variant name
	source		mygame				// the source tab's tag (lower case; default other); source mygame "Name" also
									// gives the sourcename
	sourcelabel	"MY GAME"			// the tab's label (default the tag in capitals)
	sourcename	"My Game: Remake"	// the source as the picker's details show it (default the label)
	sourceorder	10					// the tab's place (default after the ordered tabs, by its first character)
	order		40					// the character's place in the list (lowest of its folders)
	variantorder 10					// the folder's place among the character's variants (default 500)
	framing		64 18				// stage height and face-view height from the feet, in units
	idle		BOTH_STAND1			// the stage's idle animation
	skin		j2 "Jedi Master"	// the variant name of another skin of this folder
	hide							// the folder is not listed

The tabs are the source tags the installed folders declare, in sourceorder, then by their first character; the
first folder of a tag that gives a label, name or order sets it for the tag. jka (the stock models) and other are
built in and come last.

Without a character.cfg, a folder is a variant of the longest listed folder its name starts with up to an
underscore (tifa_tux -> tifa, cp_judy_2yl -> cp_judy), else a character of its own. A folder's skins red and blue,
and red_X / blue_X beside default_X or X, are the team colours of that variant; every other skin is a variant.
*/

#include "ui_roster.h"

#define ROSTER_MAX_FOLDERS		ROSTER_MAX_CHARACTERS
#define ROSTER_SKIN_NAMES		4
#define ROSTER_CFG_SIZE			4096
#define ROSTER_DEFAULT_VORDER	500

typedef struct rosterFolder_s {
	char		name[ROSTER_NAME];
	int			firstSkin, numSkins;		// into rosterSkins
	qboolean	hasCfg, hide;
	char		cfgCharacter[ROSTER_NAME];	// character.cfg's character, lower case
	char		charName[ROSTER_NAME];
	char		variant[ROSTER_NAME];
	char		source[ROSTER_SOURCE_TAG];
	char		sourceLabel[ROSTER_SOURCE_TAG];
	char		sourceName[ROSTER_NAME];
	int			sourceOrder;				// ROSTER_NO_ORDER without one
	int			order, variantOrder;
	float		stageHeight, faceHeight;
	char		idle[ROSTER_NAME];
	int			numSkinNames;
	char		skinKey[ROSTER_SKIN_NAMES][ROSTER_NAME];
	char		skinName[ROSTER_SKIN_NAMES][ROSTER_NAME];
	int			resolved;					// 0 not yet, 1 resolving, 2 id set
	char		id[ROSTER_NAME];			// the character
	int			character;					// index once the characters exist
} rosterFolder_t;

typedef struct rosterSkin_s {
	char	name[ROSTER_NAME];
	int		folder;
	int		used;						// a variant or a team skin of one
} rosterSkin_t;

static rosterFolder_t	rosterFolders[ROSTER_MAX_FOLDERS];
static int				rosterNumFolders;
static rosterSkin_t		rosterSkins[ROSTER_MAX_VARIANTS];
static int				rosterNumSkins;
static int				rosterVariantFolder[ROSTER_MAX_VARIANTS];	// the folder of each built variant (sorting)
static char				rosterCfg[ROSTER_CFG_SIZE];

static void Roster_Lower( char *s ) {
	for ( ; *s; s++ ) {
		if ( *s >= 'A' && *s <= 'Z' ) *s += 'a' - 'A';
	}
}

// "jedi_master" -> "Jedi Master"
static void Roster_Title( const char *in, char *out, int size ) {
	int i;
	qboolean start = qtrue;

	Q_strncpyz( out, in, size );
	for ( i = 0; out[i]; i++ ) {
		if ( out[i] == '_' ) {
			out[i] = ' ';
		}
		if ( out[i] == ' ' ) {
			start = qtrue;
		} else {
			if ( start && out[i] >= 'a' && out[i] <= 'z' ) out[i] += 'A' - 'a';
			start = qfalse;
		}
	}
}

static int Roster_FindFolder( const char *name ) {
	int i;
	for ( i = 0; i < rosterNumFolders; i++ ) {
		if ( !Q_stricmp( rosterFolders[i].name, name ) ) return i;
	}
	return -1;
}

static int Roster_FindSkin( const rosterFolder_t *f, const char *skin ) {
	int i;
	for ( i = f->firstSkin; i < f->firstSkin + f->numSkins; i++ ) {
		if ( !Q_stricmp( rosterSkins[i].name, skin ) ) return i;
	}
	return -1;
}

// whether a token follows on the current line (an empty COM_ParseExt( qfalse ) at the line's end consumed the next
// line's key: `source ff7r` then lost the `order` below it)
static qboolean Roster_MoreOnLine( const char *p ) {
	while ( *p == ' ' || *p == '\t' || *p == '\r' ) {
		p++;
	}
	return ( *p && *p != '\n' && !( p[0] == '/' && p[1] == '/' ) ) ? qtrue : qfalse;
}

static void Roster_ParseCfg( rosterFolder_t *f, const char *text ) {
	const char *p = text;
	const char *tok;

	COM_BeginParseSession( "character.cfg" );
	for ( ;; ) {
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] ) {
			break;
		}
		if ( !Q_stricmp( tok, "character" ) ) {
			Q_strncpyz( f->cfgCharacter, COM_ParseExt( &p, qfalse ), sizeof( f->cfgCharacter ) );
			Roster_Lower( f->cfgCharacter );
		} else if ( !Q_stricmp( tok, "name" ) ) {
			Q_strncpyz( f->charName, COM_ParseExt( &p, qfalse ), sizeof( f->charName ) );
		} else if ( !Q_stricmp( tok, "variant" ) ) {
			Q_strncpyz( f->variant, COM_ParseExt( &p, qfalse ), sizeof( f->variant ) );
		} else if ( !Q_stricmp( tok, "source" ) ) {
			Q_strncpyz( f->source, COM_ParseExt( &p, qfalse ), sizeof( f->source ) );
			Roster_Lower( f->source );
			if ( Roster_MoreOnLine( p ) ) {	// source <tag> "<name>": the sourcename on the same line
				Q_strncpyz( f->sourceName, COM_ParseExt( &p, qfalse ), sizeof( f->sourceName ) );
			}
		} else if ( !Q_stricmp( tok, "sourcelabel" ) ) {
			Q_strncpyz( f->sourceLabel, COM_ParseExt( &p, qfalse ), sizeof( f->sourceLabel ) );
		} else if ( !Q_stricmp( tok, "sourcename" ) ) {
			Q_strncpyz( f->sourceName, COM_ParseExt( &p, qfalse ), sizeof( f->sourceName ) );
		} else if ( !Q_stricmp( tok, "sourceorder" ) ) {
			f->sourceOrder = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "order" ) ) {
			f->order = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "variantorder" ) ) {
			f->variantOrder = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "framing" ) ) {
			f->stageHeight = atof( COM_ParseExt( &p, qfalse ) );
			f->faceHeight = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "idle" ) ) {
			Q_strncpyz( f->idle, COM_ParseExt( &p, qfalse ), sizeof( f->idle ) );
		} else if ( !Q_stricmp( tok, "skin" ) ) {
			char key[ROSTER_NAME];
			Q_strncpyz( key, COM_ParseExt( &p, qfalse ), sizeof( key ) );
			tok = COM_ParseExt( &p, qfalse );
			if ( key[0] && tok[0] && f->numSkinNames < ROSTER_SKIN_NAMES ) {
				Q_strncpyz( f->skinKey[f->numSkinNames], key, sizeof( f->skinKey[0] ) );
				Q_strncpyz( f->skinName[f->numSkinNames], tok, sizeof( f->skinName[0] ) );
				f->numSkinNames++;
			}
		} else if ( !Q_stricmp( tok, "hide" ) ) {
			f->hide = qtrue;
		}
		SkipRestOfLine( &p );
	}
}

// the character a folder belongs to (rosterFolders[f].id)
static const char *Roster_Resolve( int f ) {
	rosterFolder_t *fo = &rosterFolders[f];
	char prefix[ROSTER_NAME];
	int cut;

	if ( fo->resolved == 2 ) {
		return fo->id;
	}
	if ( fo->resolved == 1 ) {	// cannot happen (prefixes are shorter), kept as a guard
		return fo->name;
	}
	fo->resolved = 1;
	Q_strncpyz( fo->id, fo->name, sizeof( fo->id ) );
	if ( fo->cfgCharacter[0] ) {
		Q_strncpyz( fo->id, fo->cfgCharacter, sizeof( fo->id ) );
	} else {
		for ( cut = (int)strlen( fo->name ) - 1; cut > 0; cut-- ) {
			int g;
			if ( fo->name[cut] != '_' ) {
				continue;
			}
			Q_strncpyz( prefix, fo->name, cut + 1 < (int)sizeof( prefix ) ? cut + 1 : (int)sizeof( prefix ) );
			g = Roster_FindFolder( prefix );
			if ( g >= 0 && g != f && !rosterFolders[g].hide ) {
				Q_strncpyz( fo->id, Roster_Resolve( g ), sizeof( fo->id ) );
				break;
			}
		}
	}
	Roster_Lower( fo->id );
	fo->resolved = 2;
	return fo->id;
}

// the default-skin variant name of a folder
static void Roster_FolderVariantName( const rosterFolder_t *fo, char *out, int size ) {
	const int idLen = (int)strlen( fo->id );

	if ( fo->variant[0] ) {
		Q_strncpyz( out, fo->variant, size );
	} else if ( !Q_stricmp( fo->name, fo->id ) ) {
		Q_strncpyz( out, "Original", size );
	} else if ( !Q_stricmpn( fo->name, fo->id, idLen ) && fo->name[idLen] == '_' && fo->name[idLen + 1] ) {
		Roster_Title( fo->name + idLen + 1, out, size );		// tifa_tux -> Tux
	} else {
		int g, best = 0;
		// the part after the longest folder of the same character it starts with (eve2b_nb under eve2b -> Nb)
		for ( g = 0; g < rosterNumFolders; g++ ) {
			const int l = (int)strlen( rosterFolders[g].name );
			if ( &rosterFolders[g] != fo && !rosterFolders[g].hide && !Q_stricmp( rosterFolders[g].id, fo->id )
				&& l > best && l < (int)strlen( fo->name ) && !Q_stricmpn( fo->name, rosterFolders[g].name, l )
				&& fo->name[l] == '_' ) {
				best = l;
			}
		}
		Roster_Title( best ? fo->name + best + 1 : fo->name, out, size );
	}
}

// a team skin's base variant skin in its folder, else -1: red -> default, red_x -> default_x or x
static int Roster_TeamBase( const rosterFolder_t *fo, const char *skin, rosterTeam_t *team ) {
	const char *rest = NULL;
	int s;

	if ( !Q_stricmp( skin, "red" ) || !Q_stricmp( skin, "blue" ) ) {
		*team = ( skin[0] == 'r' || skin[0] == 'R' ) ? ROSTER_TEAM_RED : ROSTER_TEAM_BLUE;
		return Roster_FindSkin( fo, "default" );
	}
	if ( !Q_stricmpn( skin, "red_", 4 ) ) {
		*team = ROSTER_TEAM_RED;
		rest = skin + 4;
	} else if ( !Q_stricmpn( skin, "blue_", 5 ) ) {
		*team = ROSTER_TEAM_BLUE;
		rest = skin + 5;
	}
	if ( !rest || !rest[0] ) {
		return -1;
	}
	s = Roster_FindSkin( fo, va( "default_%s", rest ) );
	return s >= 0 ? s : Roster_FindSkin( fo, rest );
}

static void Roster_AddVariant( roster_t *r, int folder, int skin ) {
	rosterFolder_t *fo = &rosterFolders[folder];
	rosterVariant_t *v;
	const char *sk = rosterSkins[skin].name;
	char base[ROSTER_NAME], skinTitle[ROSTER_NAME];
	int i;

	if ( r->numVariants >= ROSTER_MAX_VARIANTS ) {
		r->numDropped++;
		return;
	}
	v = &r->variants[r->numVariants];
	memset( v, 0, sizeof( *v ) );
	Q_strncpyz( v->folder, fo->name, sizeof( v->folder ) );
	Q_strncpyz( v->skin[ROSTER_TEAM_DEFAULT], sk, sizeof( v->skin[0] ) );
	v->character = fo->character;
	v->stageHeight = fo->stageHeight;
	v->faceHeight = fo->faceHeight;
	Q_strncpyz( v->idle, fo->idle, sizeof( v->idle ) );

	Roster_FolderVariantName( fo, base, sizeof( base ) );
	if ( !Q_stricmp( sk, "default" ) ) {
		Q_strncpyz( v->name, base, sizeof( v->name ) );
	} else {
		for ( i = 0; i < fo->numSkinNames && Q_stricmp( fo->skinKey[i], sk ); i++ ) {
		}
		if ( i < fo->numSkinNames ) {
			Q_strncpyz( v->name, fo->skinName[i], sizeof( v->name ) );
		} else {
			Roster_Title( !Q_stricmpn( sk, "default_", 8 ) && sk[8] ? sk + 8 : sk, skinTitle, sizeof( skinTitle ) );
			if ( !Q_stricmp( base, "Original" ) ) {
				Q_strncpyz( v->name, skinTitle, sizeof( v->name ) );
			} else {
				Com_sprintf( v->name, sizeof( v->name ), "%s %s", base, skinTitle );
			}
		}
	}
	rosterSkins[skin].used++;
	rosterVariantFolder[r->numVariants] = folder;
	r->numVariants++;
}

// variant order within a character: the folder's variantorder, the character's own folder, folder name, default skin
static roster_t *rosterSorting;

static int Roster_CompareVariants( const void *a, const void *b ) {
	const int ia = *(const int *)a, ib = *(const int *)b;
	const rosterVariant_t *va_ = &rosterSorting->variants[ia], *vb = &rosterSorting->variants[ib];
	const rosterFolder_t *fa = &rosterFolders[rosterVariantFolder[ia]], *fb = &rosterFolders[rosterVariantFolder[ib]];
	int d;

	if ( va_->character != vb->character ) return va_->character - vb->character;
	if ( fa->variantOrder != fb->variantOrder ) return fa->variantOrder < fb->variantOrder ? -1 : 1;
	d = ( Q_stricmp( fa->name, fa->id ) != 0 ) - ( Q_stricmp( fb->name, fb->id ) != 0 );
	if ( d ) return d;
	d = Q_stricmp( fa->name, fb->name );
	if ( d ) return d;
	d = ( Q_stricmp( va_->skin[0], "default" ) != 0 ) - ( Q_stricmp( vb->skin[0], "default" ) != 0 );
	if ( d ) return d;
	d = Q_stricmp( va_->skin[0], vb->skin[0] );
	return d ? d : ia - ib;
}

// built-in tabs last: the stock models, then other
static int Roster_SourceRank( const rosterSource_t *s ) {
	if ( !Q_stricmp( s->tag, ROSTER_SOURCE_OTHER ) ) return 2;
	if ( !Q_stricmp( s->tag, ROSTER_SOURCE_STOCK ) ) return 1;
	return 0;
}

static int Roster_CompareSources( const void *a, const void *b ) {
	const rosterSource_t *sa = (const rosterSource_t *)a, *sb = (const rosterSource_t *)b;
	int d = Roster_SourceRank( sa ) - Roster_SourceRank( sb );

	if ( d ) return d;
	if ( sa->order != sb->order ) return sa->order < sb->order ? -1 : 1;
	return sa->firstCharacter - sb->firstCharacter;
}

static int Roster_FindSource( const roster_t *r, const char *tag ) {
	int i;
	for ( i = 0; i < r->numSources; i++ ) {
		if ( !Q_stricmp( r->sources[i].tag, tag ) ) return i;
	}
	return -1;
}

// the tabs of the (sorted) characters: their tags, labelled by the first folder of each tag that gives a label
static void Roster_BuildSources( roster_t *r ) {
	int j, f, s;

	r->numSources = 0;
	for ( j = 0; j < r->numCharacters; j++ ) {
		rosterCharacter_t *c = &r->characters[j];
		if ( Roster_FindSource( r, c->source ) >= 0 ) {
			continue;
		}
		if ( r->numSources >= ROSTER_MAX_SOURCES - 1 && Q_stricmp( c->source, ROSTER_SOURCE_OTHER ) ) {
			Q_strncpyz( c->source, ROSTER_SOURCE_OTHER, sizeof( c->source ) );	// the last slot is other's
			if ( Roster_FindSource( r, c->source ) >= 0 ) {
				continue;
			}
		}
		s = r->numSources++;
		memset( &r->sources[s], 0, sizeof( r->sources[s] ) );
		Q_strncpyz( r->sources[s].tag, c->source, sizeof( r->sources[s].tag ) );
		r->sources[s].order = ROSTER_NO_ORDER;
		r->sources[s].firstCharacter = j;
	}
	for ( s = 0; s < r->numSources; s++ ) {
		rosterSource_t *src = &r->sources[s];
		for ( f = 0; f < rosterNumFolders; f++ ) {
			const rosterFolder_t *fo = &rosterFolders[f];
			if ( Q_stricmp( fo->source, src->tag ) ) {
				continue;
			}
			if ( !src->label[0] && fo->sourceLabel[0] ) Q_strncpyz( src->label, fo->sourceLabel, sizeof( src->label ) );
			if ( !src->name[0] && fo->sourceName[0] ) Q_strncpyz( src->name, fo->sourceName, sizeof( src->name ) );
			if ( src->order == ROSTER_NO_ORDER ) src->order = fo->sourceOrder;
		}
		if ( !src->label[0] ) {
			Q_strncpyz( src->label, src->tag, sizeof( src->label ) );
			Q_strupr( src->label );
		}
		if ( !src->name[0] ) {
			if ( !Q_stricmp( src->tag, ROSTER_SOURCE_STOCK ) ) Q_strncpyz( src->name, "Jedi Academy", sizeof( src->name ) );
			else if ( !Q_stricmp( src->tag, ROSTER_SOURCE_OTHER ) ) Q_strncpyz( src->name, "Other", sizeof( src->name ) );
			else Q_strncpyz( src->name, src->label, sizeof( src->name ) );
		}
	}
	qsort( r->sources, r->numSources, sizeof( r->sources[0] ), Roster_CompareSources );
	for ( j = 0; j < r->numCharacters; j++ ) {
		r->characters[j].sourceIndex = Roster_FindSource( r, r->characters[j].source );
	}
}

static int Roster_CompareCharacters( const void *a, const void *b ) {
	const rosterCharacter_t *ca = &rosterSorting->characters[*(const int *)a];
	const rosterCharacter_t *cb = &rosterSorting->characters[*(const int *)b];
	int d;

	if ( ca->order != cb->order ) return ca->order < cb->order ? -1 : 1;
	d = Q_stricmp( ca->name, cb->name );
	return d ? d : Q_stricmp( ca->id, cb->id );
}

int UI_RosterBuild( roster_t *r, const char ( *entries )[64], int numEntries, rosterReadCfg_t readCfg ) {
	static int order[ROSTER_MAX_VARIANTS], remap[ROSTER_MAX_CHARACTERS];
	static rosterVariant_t sortedVariants[ROSTER_MAX_VARIANTS];
	static rosterCharacter_t sortedCharacters[ROSTER_MAX_CHARACTERS];
	int i, j, f;

	memset( r, 0, sizeof( *r ) );
	rosterNumFolders = 0;
	rosterNumSkins = 0;

	// folders and their skins, in grid order (a folder's entries are consecutive in the grid)
	for ( i = 0; i < numEntries; i++ ) {
		char folder[ROSTER_NAME];
		const char *slash = strchr( entries[i], '/' );
		const char *skin = slash ? slash + 1 : "default";
		const int len = slash ? (int)( slash - entries[i] ) : (int)strlen( entries[i] );

		Q_strncpyz( folder, entries[i], len + 1 < (int)sizeof( folder ) ? len + 1 : (int)sizeof( folder ) );
		if ( !folder[0] || !skin[0] ) {
			continue;
		}
		f = Roster_FindFolder( folder );
		if ( f < 0 ) {
			if ( rosterNumFolders >= ROSTER_MAX_FOLDERS ) {
				r->numDropped++;
				continue;
			}
			f = rosterNumFolders++;
			memset( &rosterFolders[f], 0, sizeof( rosterFolders[f] ) );
			Q_strncpyz( rosterFolders[f].name, folder, sizeof( rosterFolders[f].name ) );
			rosterFolders[f].firstSkin = rosterNumSkins;
			rosterFolders[f].order = ROSTER_NO_ORDER;
			rosterFolders[f].variantOrder = ROSTER_DEFAULT_VORDER;
			rosterFolders[f].sourceOrder = ROSTER_NO_ORDER;
		} else if ( rosterFolders[f].firstSkin + rosterFolders[f].numSkins != rosterNumSkins ) {
			r->numDropped++;	// not consecutive: the grid builds folder by folder, so this is not expected
			continue;
		}
		if ( Roster_FindSkin( &rosterFolders[f], skin ) >= 0 ) {
			continue;			// head_a1 and torso_a1 are both "a1" in the grid
		}
		if ( rosterNumSkins >= ROSTER_MAX_VARIANTS ) {
			r->numDropped++;
			continue;
		}
		Q_strncpyz( rosterSkins[rosterNumSkins].name, skin, sizeof( rosterSkins[0].name ) );
		rosterSkins[rosterNumSkins].folder = f;
		rosterSkins[rosterNumSkins].used = 0;
		rosterNumSkins++;
		rosterFolders[f].numSkins++;
	}

	for ( f = 0; f < rosterNumFolders; f++ ) {
		rosterFolder_t *fo = &rosterFolders[f];
		if ( readCfg && readCfg( fo->name, rosterCfg, sizeof( rosterCfg ) ) ) {
			fo->hasCfg = qtrue;
			Roster_ParseCfg( fo, rosterCfg );
		}
		if ( fo->hide ) {
			r->numHidden += fo->numSkins;
		}
	}

	// characters, in order of their first folder
	for ( f = 0; f < rosterNumFolders; f++ ) {
		rosterFolder_t *fo = &rosterFolders[f];
		const char *id;
		if ( fo->hide ) {
			fo->character = -1;
			continue;
		}
		id = Roster_Resolve( f );
		for ( j = 0; j < r->numCharacters && Q_stricmp( r->characters[j].id, id ); j++ ) {
		}
		if ( j == r->numCharacters ) {
			rosterCharacter_t *c;
			if ( r->numCharacters >= ROSTER_MAX_CHARACTERS ) {
				fo->character = -1;
				r->numDropped += fo->numSkins;
				continue;
			}
			c = &r->characters[r->numCharacters++];
			memset( c, 0, sizeof( *c ) );
			Q_strncpyz( c->id, id, sizeof( c->id ) );
			c->order = ROSTER_NO_ORDER;
		}
		fo->character = j;
	}
	// names, sources and order: the character's own folder first, then any folder that has one
	for ( j = 0; j < r->numCharacters; j++ ) {
		rosterCharacter_t *c = &r->characters[j];
		int pass;
		for ( pass = 0; pass < 2; pass++ ) {
			for ( f = 0; f < rosterNumFolders; f++ ) {
				const rosterFolder_t *fo = &rosterFolders[f];
				if ( fo->character != j || ( pass == 0 ) != !Q_stricmp( fo->name, c->id ) ) {
					continue;
				}
				if ( !c->name[0] && fo->charName[0] ) Q_strncpyz( c->name, fo->charName, sizeof( c->name ) );
				if ( !c->source[0] && fo->source[0] ) Q_strncpyz( c->source, fo->source, sizeof( c->source ) );
				if ( fo->order < c->order ) c->order = fo->order;
			}
		}
		if ( !c->name[0] ) Roster_Title( c->id, c->name, sizeof( c->name ) );
		if ( !c->source[0] ) Q_strncpyz( c->source, "other", sizeof( c->source ) );
	}

	// variants: every skin that is not a team colour of another
	for ( f = 0; f < rosterNumFolders; f++ ) {
		rosterFolder_t *fo = &rosterFolders[f];
		if ( fo->character < 0 ) {
			continue;
		}
		for ( i = fo->firstSkin; i < fo->firstSkin + fo->numSkins; i++ ) {
			rosterTeam_t team;
			if ( Roster_TeamBase( fo, rosterSkins[i].name, &team ) < 0 ) {
				Roster_AddVariant( r, f, i );
			}
		}
		for ( i = fo->firstSkin; i < fo->firstSkin + fo->numSkins; i++ ) {
			rosterTeam_t team;
			const int base = Roster_TeamBase( fo, rosterSkins[i].name, &team );
			if ( base < 0 ) {
				continue;
			}
			for ( j = 0; j < r->numVariants; j++ ) {
				rosterVariant_t *v = &r->variants[j];
				if ( rosterVariantFolder[j] == f && !Q_stricmp( v->skin[ROSTER_TEAM_DEFAULT], rosterSkins[base].name ) ) {
					if ( !v->skin[team][0] ) {
						Q_strncpyz( v->skin[team], rosterSkins[i].name, sizeof( v->skin[0] ) );
						rosterSkins[i].used++;
					}
					break;
				}
			}
			if ( !rosterSkins[i].used ) {
				Roster_AddVariant( r, f, i );	// no base variant, or its colour taken (red and red_default): its own
			}
		}
	}

	// characters sorted, then variants grouped by character
	rosterSorting = r;
	for ( j = 0; j < r->numCharacters; j++ ) order[j] = j;
	qsort( order, r->numCharacters, sizeof( order[0] ), Roster_CompareCharacters );
	for ( j = 0; j < r->numCharacters; j++ ) {
		sortedCharacters[j] = r->characters[order[j]];
		remap[order[j]] = j;
	}
	memcpy( r->characters, sortedCharacters, sizeof( r->characters[0] ) * r->numCharacters );
	for ( f = 0; f < rosterNumFolders; f++ ) {
		if ( rosterFolders[f].character >= 0 ) rosterFolders[f].character = remap[rosterFolders[f].character];
	}
	for ( i = 0; i < r->numVariants; i++ ) {
		r->variants[i].character = rosterFolders[rosterVariantFolder[i]].character;
		order[i] = i;
	}
	qsort( order, r->numVariants, sizeof( order[0] ), Roster_CompareVariants );
	for ( i = 0; i < r->numVariants; i++ ) sortedVariants[i] = r->variants[order[i]];
	memcpy( r->variants, sortedVariants, sizeof( r->variants[0] ) * r->numVariants );
	for ( i = 0; i < r->numVariants; i++ ) {
		rosterCharacter_t *c = &r->characters[r->variants[i].character];
		if ( !c->numVariants ) c->firstVariant = i;
		c->numVariants++;
	}
	rosterSorting = NULL;
	Roster_BuildSources( r );
	return r->numCharacters;
}

void UI_RosterModelValue( const roster_t *r, int variant, rosterTeam_t team, char *out, int size ) {
	const rosterVariant_t *v;
	const char *skin;

	if ( variant < 0 || variant >= r->numVariants ) {
		if ( size > 0 ) out[0] = '\0';
		return;
	}
	v = &r->variants[variant];
	skin = ( team > ROSTER_TEAM_DEFAULT && team < ROSTER_NUM_TEAMS && v->skin[team][0] ) ? v->skin[team] : v->skin[ROSTER_TEAM_DEFAULT];
	Com_sprintf( out, size, "%s/%s", v->folder, skin );	// as the grid sets it ("aerith/default")
}

int UI_RosterFindModel( const roster_t *r, const char *model ) {
	char value[MAX_QPATH];
	int i, t;

	for ( i = 0; i < r->numVariants; i++ ) {
		for ( t = 0; t < ROSTER_NUM_TEAMS; t++ ) {
			if ( t > ROSTER_TEAM_DEFAULT && !r->variants[i].skin[t][0] ) {
				continue;
			}
			UI_RosterModelValue( r, i, (rosterTeam_t)t, value, sizeof( value ) );
			if ( !Q_stricmp( value, model ) || ( t == ROSTER_TEAM_DEFAULT && !Q_stricmp( r->variants[i].skin[t], "default" )
				&& !Q_stricmp( r->variants[i].folder, model ) ) ) {	// a bare folder is its default skin
				return i;
			}
		}
	}
	return -1;
}
