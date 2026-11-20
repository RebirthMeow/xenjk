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

// cg_enemy.c -- cg_enemyModel (one model for every enemy) and cg_enemyColors (enemy brightskins), from cg_players.c.
// Hooks: CG_NewClientInfo calls CG_Enemy_PickModel, CG_Player calls CG_Enemy_AddBrightskin, CG_DrawActiveFrame
// calls CG_CheckEnemyModels.

#include "cg_local.h"

/*
====================
CG_ParseColorString
====================
*/
static void CG_ParseColorString( const char *v, byte *color );

// cg_enemyColors, parsed once a change rather than per enemy per frame.
static void CG_EnemyColor( byte *color ) {
	static int parsed = -1;
	static byte cached[3];
	if ( parsed != cg_enemyColors.modificationCount ) {
		CG_ParseColorString( cg_enemyColors.string, cached );
		parsed = cg_enemyColors.modificationCount;
	}
	color[0] = cached[0];
	color[1] = cached[1];
	color[2] = cached[2];
}

// The client the view belongs to (the followed player while spectating one),
// and whether a client is its enemy, for cg_enemyModel and cg_enemyColors. A
// free-flying spectator has no enemies in team games.
static int CG_EnemyViewClient( void ) {
	return cg.snap ? cg.snap->ps.clientNum : cg.clientNum;
}

qboolean CG_IsEnemyClient( int clientNum, team_t team ) {
	int view = CG_EnemyViewClient();
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {	// NPCs and vehicles are not clients (they were all drawn as enemy brightskins)
		return qfalse;
	}
	if ( clientNum == view || clientNum == cg.clientNum || team == TEAM_SPECTATOR ) {
		return qfalse;
	}
	if ( cgs.gametype >= GT_TEAM ) {
		team_t viewTeam = cgs.clientinfo[view].team;
		return viewTeam != TEAM_SPECTATOR && team != viewTeam;
	}
	return qtrue;
}

// cg_enemyModel is applied when a client's info is parsed; when the view's
// side changes (a team change, following another player) every client's
// model is picked again (they kept the side they were
// parsed under).
void CG_CheckEnemyModels( void ) {
	static int lastKey = -1;
	int view, key, i;
	if ( !cg.snap ) {
		return;
	}
	if ( lastKey < 0 ) {	// the side the clients were loaded under
		lastKey = cg.clientNum * TEAM_NUM_TEAMS + cgs.clientinfo[cg.clientNum].team;
	}
	view = CG_EnemyViewClient();
	key = view * TEAM_NUM_TEAMS + cgs.clientinfo[view].team;
	if ( key == lastKey ) {
		return;
	}
	lastKey = key;
	if ( !cg_enemyModel.string[0] ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const char *clientInfo = CG_ConfigString( CS_PLAYERS + i );
		if ( VALIDSTRING( clientInfo ) ) {
			CG_NewClientInfo( i, qtrue );
		}
	}
}
static void CG_ParseColorString( const char *v, byte *color ) {
	if ( !v || !v[0] ) {
		color[0] = color[1] = color[2] = 255;
		return;
	}

	if ( v[0] == '#' || (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) ) {
		const char *hex = (v[0] == '#') ? v + 1 : v + 2;
		int len = strlen(hex);
		unsigned int val = 0;
		if ( sscanf( hex, "%x", &val ) != 1 ) {
			color[0] = color[1] = color[2] = 255;
			return;
		}

		if ( len == 3 ) {	// #rgb
			color[0] = ((val >> 8) & 0xF) * 17;
			color[1] = ((val >> 4) & 0xF) * 17;
			color[2] = (val & 0xF) * 17;
		} else if ( len >= 8 ) {
			color[0] = (val >> 24) & 0xFF;
			color[1] = (val >> 16) & 0xFF;
			color[2] = (val >> 8) & 0xFF;
		} else {
			color[0] = (val >> 16) & 0xFF;
			color[1] = (val >> 8) & 0xFF;
			color[2] = val & 0xFF;
		}
		return;
	}

	if ( !Q_stricmp( v, "red" ) ) { color[0] = 255; color[1] = 0; color[2] = 0; }
	else if ( !Q_stricmp( v, "green" ) ) { color[0] = 0; color[1] = 255; color[2] = 0; }
	else if ( !Q_stricmp( v, "blue" ) ) { color[0] = 0; color[1] = 0; color[2] = 255; }
	else if ( !Q_stricmp( v, "yellow" ) ) { color[0] = 255; color[1] = 255; color[2] = 0; }
	else if ( !Q_stricmp( v, "magenta" ) || !Q_stricmp( v, "purple" ) ) { color[0] = 255; color[1] = 0; color[2] = 255; }
	else if ( !Q_stricmp( v, "cyan" ) ) { color[0] = 0; color[1] = 255; color[2] = 255; }
	else if ( !Q_stricmp( v, "white" ) ) { color[0] = 255; color[1] = 255; color[2] = 255; }
	else if ( !Q_stricmp( v, "black" ) ) { color[0] = 0; color[1] = 0; color[2] = 0; }
	else if ( !Q_stricmp( v, "orange" ) ) { color[0] = 255; color[1] = 128; color[2] = 0; }
	else if ( !Q_stricmp( v, "pink" ) ) { color[0] = 255; color[1] = 192; color[2] = 203; }
	else {
		// Fallback to numeric 1-7
		vec3_t temp;
		CG_ColorFromString( v, temp );
		color[0] = temp[0] * 255.0f;
		color[1] = temp[1] * 255.0f;
		color[2] = temp[2] * 255.0f;
	}
}

// cg_enemyModel for an enemy's new info: the model (and in team games their own skin name, so team skins stay)
void CG_Enemy_PickModel( clientInfo_t *newInfo, const char *configModel ) {
	char modelStr[MAX_QPATH];
	char *skin;
	const char *slash;

	Q_strncpyz( modelStr, cg_enemyModel.string, sizeof( modelStr ) );
	if ( ( skin = strchr( modelStr, '/' ) ) == NULL) {
		skin = "default";
	} else {
		*skin++ = 0;
	}
	Q_strncpyz( newInfo->skinName, skin, sizeof( newInfo->skinName ) );
	Q_strncpyz( newInfo->modelName, modelStr, sizeof( newInfo->modelName ) );

	if ( cgs.gametype >= GT_TEAM ) {
		// keep skin name
		slash = strchr( configModel, '/' );
		if ( slash ) {
			Q_strncpyz( newInfo->skinName, slash + 1, sizeof( newInfo->skinName ) );
		}
	}
}

// Enemy Brightskins. A tinted copy of the model exactly as drawn (its LOD, third-person alpha and depth), so it adds
// nothing where the model was not drawn: a cloaked enemy stays cloaked.
void CG_Enemy_AddBrightskin( const centity_t *cent, const clientInfo_t *ci, refEntity_t *legs ) {
	if ( !CG_IsEnemyClient( cent->currentState.number, ci->team ) ) {
		return;
	}
	legs->renderfx |= RF_RGB_TINT;
	legs->customShader = cgs.media.whiteShader;
	CG_EnemyColor( legs->shaderRGBA );
	if ( !(legs->renderfx & RF_FORCE_ENT_ALPHA) ) {
		legs->shaderRGBA[3] = 255;
	}
	trap->R_AddRefEntityToScene( legs );
}
