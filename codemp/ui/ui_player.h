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

// The player screen's two halves: ui_player.c (sections, the draft, the commit, the stock items it drives) and
// ui_player_look.c (the look: drawing and input).

#pragma once

#define XP_DOT		"\xb7"	// the look's middle dot (its fonts have it at 0xB7)

enum { XP_CHARACTER, XP_SABER, XP_FORCE, XP_TEAM, XP_NUM_SECTIONS, XP_COMMON = -1 };

typedef struct xpContext_s {
	qboolean	connected;
	int			gametype;
	int			team;			// ui_myteam
	int			disabledForce;
	qboolean	forceBasedTeams;
} xpContext_t;

// ui_player.c
void		XP_Context( xpContext_t *c );
const char *XP_NoForceReason( const xpContext_t *c );
qboolean	XP_AllForceDisabled( int mask );
int			XP_Section( void );
void		XP_SetSection( int section );
void		XP_Step( int dir );
void		XP_Commit( const char *what );
void		XP_Back( void );
void		XP_Classic( void );
qboolean	XP_Waiting( void );			// the commit's commands wait for the model's warm-up
void		XP_Flash( const char *why );	// a refused press: the status line says why, for a moment
const char *XP_StatusLine( qboolean *warn );
void		XP_Summary( int section, char *buf, int size );
const char *XP_GameTypeName( int gametype );
const char *XP_RankName( int rank );
// the stock items the look drives (any item of the screen by name, shown or not)
itemDef_t	*XP_Item( const char *name );
qboolean	XP_PressItem( const char *name );
qboolean	XP_SelectRow( const char *name, int row );
qboolean	XP_ItemKey( const char *name, int key );

// ui_player_look.c
void		UI_PlayerLookOpen( void );
void		UI_PlayerLookDraw( int ownerDraw );
qboolean	UI_PlayerLookKey( int key, qboolean down );
