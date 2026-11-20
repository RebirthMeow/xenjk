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

// cg_modelwarm.c -- another player's new model loads over several frames (cg_modelWarm), from cg_players.c

#include "cg_local.h"

/*
======================
CG_ModelWarm

Another player's new model loads in steps (docs/character-loading-plan.md, M2b): the renderer's warm-up reads and
builds it over several frames while the player keeps the model they had (a player not seen before shows
DEFAULT_MODEL), then CG_ModelWarmCheck runs CG_NewClientInfo again and the model registers from loaded data. Off for
the local player (the engine holds its userinfo until the model is loaded, cl_modelWarm), during a level load, with
cg_modelWarm 0, or without the engine's R_WarmModel.
======================
*/
typedef struct cgModelWarm_s {
	int		handle;				// the warm-up; 0 none
	int		started;			// trap->Milliseconds()
	char	model[MAX_QPATH];	// "model/skin" it loads
} cgModelWarm_t;

static cgModelWarm_t cg_modelWarms[MAX_CLIENTS];

static qboolean CG_ModelWarmApi( void ) {
	char api[16];

	if ( !cg_modelWarm.integer ) {
		return qfalse;
	}
	trap->Cvar_VariableStringBuffer( "cl_cgameWarmApi", api, sizeof( api ) );	// the import table is that long
	return (qboolean)( atoi( api ) >= 1 && trap->ext.R_WarmModel && trap->ext.R_WarmState );
}

// before newInfo's model registers: qtrue when the warm-up decides (newInfo then shows the old model until it is
// ready, or the new one now that it is), qfalse to load as before
qboolean CG_ModelWarm( int clientNum, const clientInfo_t *ci, clientInfo_t *newInfo ) {
	cgModelWarm_t *w;
	char asked[MAX_QPATH];
	int state;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return qfalse;
	}
	w = &cg_modelWarms[clientNum];
	if ( !CG_ModelWarmApi() || cg.loading || clientNum == cg.clientNum || ( cg.snap && cg.snap->ps.clientNum == clientNum ) ) {
		w->handle = 0;
		return qfalse;
	}
	if ( ci->infoValid && !Q_stricmp( ci->modelName, newInfo->modelName ) && !Q_stricmp( ci->skinName, newInfo->skinName ) ) {
		w->handle = 0;	// the model they have
		return qfalse;
	}
	Com_sprintf( asked, sizeof( asked ), "%s/%s", newInfo->modelName, newInfo->skinName );
	if ( !w->handle || Q_stricmp( w->model, asked ) ) {
		w->handle = trap->ext.R_WarmModel( asked, WARMMODEL_OTHER_PLAYER );
		w->started = trap->Milliseconds();
		Q_strncpyz( w->model, asked, sizeof( w->model ) );
		if ( !w->handle ) {
			return qfalse;
		}
	}
	state = trap->ext.R_WarmState( w->handle );
	if ( state != 0 || trap->Milliseconds() - w->started > cg_modelWarmTimeout.integer ) {
		w->handle = 0;	// loaded (or failed, or given up on): it registers now
		return qtrue;
	}
	if ( ci->infoValid && ci->modelName[0] && !ci->deferred ) {	// a deferred info's model name was never loaded
		Q_strncpyz( newInfo->modelName, ci->modelName, sizeof( newInfo->modelName ) );
		Q_strncpyz( newInfo->skinName, ci->skinName, sizeof( newInfo->skinName ) );
	} else {
		Q_strncpyz( newInfo->modelName, DEFAULT_MODEL, sizeof( newInfo->modelName ) );
		Q_strncpyz( newInfo->skinName, "default", sizeof( newInfo->skinName ) );
	}
	if ( cgs.gametype >= GT_TEAM && !cgs.jediVmerc && cgs.gametype != GT_SIEGE ) {
		BG_ValidateSkinForTeam( newInfo->modelName, newInfo->skinName, newInfo->team, newInfo->colorOverride );
	}
	return qtrue;
}

// every frame: the players whose warm-up finished get their new model
void CG_ModelWarmCheck( void ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		cgModelWarm_t *w = &cg_modelWarms[i];
		if ( !w->handle ) {
			continue;
		}
		if ( !CG_ModelWarmApi() || trap->ext.R_WarmState( w->handle ) != 0
			|| trap->Milliseconds() - w->started > cg_modelWarmTimeout.integer ) {
			CG_NewClientInfo( i, qtrue );
		}
	}
}

// the player left: drop their warm-up
void CG_ModelWarm_Forget( int clientNum ) {
	if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
		cg_modelWarms[clientNum].handle = 0;
	}
}
