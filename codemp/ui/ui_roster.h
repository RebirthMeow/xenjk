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

// The character picker's roster (docs/character-loading-plan.md, M4a): the model grid's folder/skin entries grouped
// into characters and their variants (outfits), from each folder's models/players/<folder>/character.cfg or, without
// one, from the folder names. Team colours are not variants: a variant carries its folder's red and blue skins.
//
// The grouping is pure (no trap calls): ui_main.c hands it the grid's entries and a reader for the cfg files, and
// tools/characters/roster_check.py builds it offline against the installed packs.

#pragma once

#include "qcommon/q_shared.h"

#define ROSTER_MAX_CHARACTERS	512
#define ROSTER_MAX_VARIANTS		1024
#define ROSTER_NAME				64

typedef enum {
	ROSTER_TEAM_DEFAULT,
	ROSTER_TEAM_RED,
	ROSTER_TEAM_BLUE,
	ROSTER_NUM_TEAMS
} rosterTeam_t;

typedef struct rosterVariant_s {
	char	folder[ROSTER_NAME];			// models/players/<folder>
	char	skin[ROSTER_NUM_TEAMS][ROSTER_NAME];	// as the grid names them ("default", "red", "default_army"); "" = none
	char	name[ROSTER_NAME];				// shown under the stage: "Original", "Tux"
	int		character;						// index into roster_t::characters
	float	stageHeight, faceHeight;		// character.cfg framing; 0 = from the model
	char	idle[ROSTER_NAME];				// character.cfg idle animation; "" = the menu default
} rosterVariant_t;

// a source tab: the tags the installed character.cfg files declare (source), with their labels; the built-in
// ROSTER_SOURCE_STOCK (stock models) and ROSTER_SOURCE_OTHER (no source, or past ROSTER_MAX_SOURCES) come last
#define ROSTER_MAX_SOURCES		32
#define ROSTER_SOURCE_TAG		16
#define ROSTER_SOURCE_STOCK		"jka"
#define ROSTER_SOURCE_OTHER		"other"

typedef struct rosterSource_s {
	char	tag[ROSTER_SOURCE_TAG];			// character.cfg source, lower case
	char	label[ROSTER_SOURCE_TAG];		// the tab: sourcelabel, else the tag in capitals
	char	name[ROSTER_NAME];				// the details line: sourcename, else the label
	int		order;							// sourceorder; ROSTER_NO_ORDER without one
	int		firstCharacter;					// its first character in the sorted list (tabs without an order follow it)
} rosterSource_t;

typedef struct rosterCharacter_s {
	char	id[ROSTER_NAME];
	char	name[ROSTER_NAME];
	char	source[ROSTER_SOURCE_TAG];		// its source tag (character.cfg source; "other" without one)
	int		sourceIndex;					// into roster_t::sources
	int		order;							// character.cfg order; ROSTER_NO_ORDER without one
	int		firstVariant, numVariants;		// variants[firstVariant .. +numVariants), the character's own folder first
} rosterCharacter_t;

#define ROSTER_NO_ORDER		( 1 << 30 )

typedef struct roster_s {
	int					numCharacters, numVariants;
	int					numHidden;			// grid entries of folders whose character.cfg says hide
	int					numDropped;			// entries past the limits
	int					numSources;			// tabs in display order, each with at least one character
	rosterSource_t		sources[ROSTER_MAX_SOURCES];
	rosterCharacter_t	characters[ROSTER_MAX_CHARACTERS];
	rosterVariant_t		variants[ROSTER_MAX_VARIANTS];
} roster_t;

// models/players/<folder>/character.cfg into buf (NUL-terminated, at most size - 1 bytes); qfalse when there is none
typedef qboolean ( *rosterReadCfg_t )( const char *folder, char *buf, int size );

// entries are the grid's "folder/skin" names (uiInfo.q3HeadNames); returns the number of characters
int		UI_RosterBuild( roster_t *r, const char ( *entries )[64], int numEntries, rosterReadCfg_t readCfg );

// the model cvar value of a variant in a team colour ("aerith_tux/red"); the default skin when it has no such colour
void	UI_RosterModelValue( const roster_t *r, int variant, rosterTeam_t team, char *out, int size );

// the variant whose model value (any team colour) is model, else -1
int		UI_RosterFindModel( const roster_t *r, const char *model );
