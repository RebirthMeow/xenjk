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
The player screen (docs/wishlist/player-screen.md): character, saber, Force and team on one screen, with one commit.
The screen is ui/jamp/xen_player.menu (menu "xen_player"; loaded beside every menu set when a pack or the loose
ui/jamp folder has it). This file is its logic; ui_player_look.c draws it (the look, 10.11) and takes its input.

- Sections. The menu's items are in four runs, each opened by a marker decoration named xp_section_character,
  _saber, _force or _team; items after xp_section_common (and before the first marker) belong to every section.
- Items. Only the look's items are shown (the backdrop, the picker's stage, the look). The rest are the stock items
  the look drives (XP_PressItem, XP_SelectRow, XP_ItemKey): the Saber and Force items have the stock names, feeders,
  ownerdraws and uiScripts of ingame_saber and ingame_playerforce, so they edit the stock UI state (ui_saber*,
  uiForce*), and stock code that looks its menus up by name (ingame_player, ingame_playerforce) never touches this
  one. An item is reachable while its section is open and its group's rule holds (the side's powers, Jedi / Non-Jedi,
  the commit buttons for the context): the scripted checks (ui_menuitem) take only reachable items.
- Character is the picker (ui_picker.c): the look draws its roster and details and drives its selection.
- The draft. Nothing reaches the userinfo until a commit: the picker keeps its selection, the saber items edit the
  ui_ copies, the Force items the UI's ranks, the name field ui_Name. Back drops it all (the Force ranks are read
  back from forcepowers).
- The commit (xpCommit <apply|free|red|blue|s|auto|single|double|class>), design 3.5, in one frame: name, model (only
  when the draft changed it), sabers (updatesabercvars), forcepowers, then the team command:
    Force changed:   forcechanged "<team>" (stock), or forcechanged then team / duelteam in Duel and Power Duel
    Force unchanged: team <team> (stock sends nothing: quirk 1)
    apply:           forcechanged when Force changed (the stock hub's Apply)
  When the model changed and the engine has the warm-up, the commands wait in this module until the new model is
  warm (and two frames more, so the held userinfo goes first), at most cl_modelWarmTimeout (quirk 3). The screen
  stays open meanwhile ("Loading your model..."): the UI module runs frames only while a menu is open.
- Opening. With ui_playerScreen 1 (default) Menus_ActivateByName opens this screen in place of ingame_player,
  playerMenu, ingame_saber, saberMenu, ingame_playerforce and ingame_join, at the matching section. "Classic" opens
  the stock hub; the stock screens then open as themselves until none of them is open.
*/

#include "ui_local.h"
#include "ui_force.h"
#include "ui_player.h"


#define XP_MENU				"xen_player"
#define XP_MENU_FILE		"ui/jamp/xen_player.menu"

#define UI_XP_SECTIONS		416		// draws nothing: keeps the items' visibility (first item of the menu)
#define UI_XP_LOOK_BG		417		// the look's backdrop (ui_player_look.c), under the stage
#define UI_XP_LOOK			418		// the look: everything over the stage

#define XP_MAX_ITEMS		MAX_MENUITEMS
#define XP_QUEUE			4

static const char *xpSectionMarkers[XP_NUM_SECTIONS] = {
	"xp_section_character", "xp_section_saber", "xp_section_force", "xp_section_team"
};

// stock menus the screen stands in for, and the section each opens at
static const struct { const char *name; int section; } xpStock[] = {
	{ "ingame_player", XP_CHARACTER }, { "playerMenu", XP_CHARACTER }, { "ingame_saber", XP_SABER },
	{ "saberMenu", XP_SABER }, { "ingame_playerforce", XP_FORCE }, { "ingame_join", XP_TEAM },
};

typedef struct xpState_s {
	menuDef_t	*menu;
	int			itemSection[XP_MAX_ITEMS];	// the section each item belongs to (XP_COMMON: every one)
	int			section;
	int			openSection;	// where the next open starts
	qboolean	classic;		// the stock screens open as themselves
	char		status[128];	// a one-off line (a refused press), until statusUntil
	int			statusUntil;

	// commands held until the committed model is warm (quirk 3)
	char		queue[XP_QUEUE][64];
	int			numQueued;
	int			warmHandle;
	int			warmStart;
	int			readyFrames;
	qboolean	leaveAfter;		// the screen closes once the held commands are sent

	// the Force state at open, which Back restores
	int			forceRank, forceSide, jediNonJedi, forcePowers[NUM_FORCE_POWERS];
	qboolean	touchedForce;
} xpState_t;

static xpState_t xp;

/* ------------------------------------------------------------------ context */

void XP_Context( xpContext_t *c ) {
	uiClientState_t cs;
	char info[MAX_INFO_STRING];

	memset( c, 0, sizeof( *c ) );
	trap->GetClientState( &cs );
	c->connected = (qboolean)( cs.connState >= CA_CONNECTED );
	c->team = TEAM_SPECTATOR;
	if ( !c->connected ) {
		return;
	}
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	c->gametype = atoi( Info_ValueForKey( info, "g_gametype" ) );
	c->disabledForce = atoi( Info_ValueForKey( info, "g_forcePowerDisable" ) );
	c->forceBasedTeams = (qboolean)( atoi( Info_ValueForKey( info, "g_forceBasedTeams" ) ) != 0 );
	c->team = (int)trap->Cvar_VariableValue( "ui_myteam" );
}

qboolean XP_AllForceDisabled( int mask ) {
	int i;

	if ( !mask ) {
		return qfalse;
	}
	for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
		if ( !( mask & ( 1 << i ) ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

// the reason the Force section has no allocation, or NULL
const char *XP_NoForceReason( const xpContext_t *c ) {
	if ( c->connected && XP_AllForceDisabled( c->disabledForce ) ) return "Force powers are disabled on this server";
	if ( c->connected && c->gametype == GT_HOLOCRON ) return "Holocron: powers come from holocrons you pick up";
	if ( c->connected && c->gametype == GT_JEDIMASTER ) return "Jedi Master: only the Jedi Master has the Force";
	if ( c->connected && UI_TrueJediEnabled() && uiJediNonJedi == FORCE_NONJEDI ) return "Non-Jedi: no Force powers";
	return NULL;
}

/* ------------------------------------------------------------------ visibility and reach */

static qboolean XP_GroupIs( const itemDef_t *item, const char *group ) {
	return (qboolean)( item->window.group && !Q_stricmp( item->window.group, group ) );
}

static qboolean XP_NameIs( const itemDef_t *item, const char *name ) {
	return (qboolean)( item->window.name && !Q_stricmp( item->window.name, name ) );
}

// the item's group rule (stock's UpdateForceStatus and UI_ForceSide rules, and the commit row by context, 3.6)
static qboolean XP_ItemRule( const itemDef_t *item, const xpContext_t *c ) {
	const qboolean team = (qboolean)( c->gametype >= GT_TEAM && c->gametype != GT_SIEGE );
	const qboolean spectator = (qboolean)( c->team == TEAM_SPECTATOR );
	const qboolean noForce = (qboolean)( XP_NoForceReason( c ) != NULL );
	const qboolean saberAttack = (qboolean)( uiForcePowersRank[FP_SABER_OFFENSE] > 0 || ui_freeSaber.integer );

	if ( XP_GroupIs( item, "lightpowers" ) || XP_GroupIs( item, "lightpowers_team" ) ) return (qboolean)( !noForce && uiForceSide == FORCE_LIGHTSIDE );
	if ( XP_GroupIs( item, "darkpowers" ) || XP_GroupIs( item, "darkpowers_team" ) ) return (qboolean)( !noForce && uiForceSide == FORCE_DARKSIDE );
	if ( XP_GroupIs( item, "yesforce" ) ) return (qboolean)!noForce;
	if ( XP_GroupIs( item, "jedinonjedi" ) ) return (qboolean)( c->connected && UI_TrueJediEnabled() && !XP_AllForceDisabled( c->disabledForce ) );
	if ( XP_NameIs( item, "setfp_saberdefend" ) || XP_NameIs( item, "setfp_saberthrow" ) ) return (qboolean)( !noForce && saberAttack );

	// the commit row and the Team cards
	if ( XP_GroupIs( item, "xp_apply" ) ) return (qboolean)( !c->connected || !spectator );
	if ( XP_GroupIs( item, "xp_back" ) ) return qtrue;
	if ( XP_GroupIs( item, "xp_ingame" ) ) return c->connected;
	if ( XP_GroupIs( item, "xp_spec" ) ) return (qboolean)( c->connected && c->gametype != GT_SIEGE );
	if ( XP_GroupIs( item, "xp_join" ) ) return (qboolean)( c->connected && spectator && c->gametype < GT_TEAM && c->gametype != GT_POWERDUEL );
	if ( XP_GroupIs( item, "xp_auto" ) ) return (qboolean)( c->connected && spectator && team );
	if ( XP_GroupIs( item, "xp_red" ) ) return (qboolean)( c->connected && team && c->team != TEAM_RED );
	if ( XP_GroupIs( item, "xp_blue" ) ) return (qboolean)( c->connected && team && c->team != TEAM_BLUE );
	if ( XP_GroupIs( item, "xp_duel" ) ) return (qboolean)( c->connected && c->gametype == GT_POWERDUEL );
	if ( XP_GroupIs( item, "xp_class" ) ) return (qboolean)( c->connected && c->gametype == GT_SIEGE );
	return qtrue;
}

// the look's own items are the only ones shown
static qboolean XP_LookItem( const itemDef_t *item ) {
	return (qboolean)( XP_NameIs( item, "xp_sections" ) || XP_NameIs( item, "xp_look_bg" ) || XP_NameIs( item, "stage" )
		|| XP_NameIs( item, "xp_look" ) );
}

static void XP_Visibility( void ) {
	int i;

	if ( !xp.menu ) {
		return;
	}
	for ( i = 0; i < xp.menu->itemCount && i < XP_MAX_ITEMS; i++ ) {
		itemDef_t *item = xp.menu->items[i];
		if ( XP_LookItem( item ) ) {
			item->window.flags |= WINDOW_VISIBLE;
		} else {
			item->window.flags &= ~( WINDOW_VISIBLE | WINDOW_HASFOCUS | WINDOW_MOUSEOVER );
		}
	}
}

// what a player can reach through the look: the item's section is open and its rule holds
static qboolean XP_Reachable( int index ) {
	xpContext_t c;
	itemDef_t *item;
	int s;

	if ( !xp.menu || index < 0 || index >= xp.menu->itemCount || index >= XP_MAX_ITEMS ) {
		return qfalse;
	}
	item = xp.menu->items[index];
	s = xp.itemSection[index];
	if ( s != XP_COMMON && s != xp.section ) {
		return qfalse;
	}
	XP_Context( &c );
	return (qboolean)( XP_ItemRule( item, &c ) && Item_EnableShowViaCvar( item, CVAR_SHOW ) );
}

// each item's section, from the markers (once per load)
static void XP_MapSections( menuDef_t *menu ) {
	int i, s, current = XP_COMMON;

	for ( i = 0; i < menu->itemCount && i < XP_MAX_ITEMS; i++ ) {
		const itemDef_t *item = menu->items[i];
		for ( s = 0; s < XP_NUM_SECTIONS; s++ ) {
			if ( XP_NameIs( item, xpSectionMarkers[s] ) ) {
				current = s;
			}
		}
		if ( XP_NameIs( item, "xp_section_common" ) ) {
			current = XP_COMMON;
		}
		xp.itemSection[i] = current;
	}
}

/* ------------------------------------------------------------------ texts the look shows */

static const char *xpRankNames[NUM_FORCE_MASTERY_LEVELS] = {
	"Uninitiated", "Initiate", "Padawan", "Jedi", "Jedi Adept", "Jedi Guardian", "Jedi Knight", "Jedi Master"
};

const char *XP_RankName( int rank ) {
	return xpRankNames[rank >= 0 && rank < NUM_FORCE_MASTERY_LEVELS ? rank : 0];
}

const char *XP_GameTypeName( int gametype ) {
	static const char *names[GT_MAX_GAME_TYPE] = {
		"FFA", "Holocron", "Jedi Master", "Duel", "Power Duel", "Single Player", "Team FFA", "Siege", "CTF", "CTY"
	};
	return gametype >= 0 && gametype < GT_MAX_GAME_TYPE ? names[gametype] : "";
}

static const char *XP_TeamWord( int team ) {
	switch ( team ) {
	case TEAM_RED: return "Red team";
	case TEAM_BLUE: return "Blue team";
	case TEAM_FREE: return "Playing";
	default: return "Spectator";
	}
}

// a tab's live summary
void XP_Summary( int section, char *buf, int size ) {
	xpContext_t c;
	char a[MAX_QPATH], b[MAX_QPATH];

	XP_Context( &c );
	buf[0] = 0;
	switch ( section ) {
	case XP_CHARACTER:
		UI_PickerDraftName( buf, size );
		break;
	case XP_SABER:
		trap->Cvar_VariableStringBuffer( "ui_saber_type", a, sizeof( a ) );
		trap->Cvar_VariableStringBuffer( "ui_saber_color", b, sizeof( b ) );
		Com_sprintf( buf, size, "%s " XP_DOT " %s", a[0] ? a : "single", b );
		if ( !Q_stricmp( a, "dual" ) ) {
			trap->Cvar_VariableStringBuffer( "ui_saber2_color", b, sizeof( b ) );
			Q_strcat( buf, size, va( " / %s", b ) );
		}
		break;
	case XP_FORCE:
		if ( XP_NoForceReason( &c ) ) {
			Q_strncpyz( buf, "Locked", size );
		} else {
			const int rank = uiForceRank >= 0 && uiForceRank < NUM_FORCE_MASTERY_LEVELS ? uiForceRank : 0;
			Com_sprintf( buf, size, "%s " XP_DOT " %d of %d left", uiForceSide == FORCE_DARKSIDE ? "Dark" : "Light", uiForceAvailable,
				forceMasteryPoints[rank] );
		}
		break;
	case XP_TEAM:
		Q_strncpyz( buf, c.connected ? XP_TeamWord( c.team ) : "Offline", size );
		break;
	}
}

// what applies when, or why something is refused (design 3.7)
const char *XP_StatusLine( qboolean *warn ) {
	xpContext_t c;
	const char *no;

	*warn = qfalse;
	if ( xp.status[0] && uiInfo.uiDC.realTime < xp.statusUntil ) {
		*warn = qtrue;
		return xp.status;
	}
	if ( xp.numQueued ) {
		return "Loading your model " XP_DOT " the join goes when it is ready";
	}
	XP_Context( &c );
	if ( !c.connected ) {
		return xp.section == XP_FORCE ? "Each server sets your rank and trims this build to fit"
			: "Saved for your next game " XP_DOT " nothing is sent until you connect";
	}
	switch ( xp.section ) {
	case XP_CHARACTER:
		if ( c.gametype >= GT_TEAM && c.gametype != GT_SIEGE ) return "Your team sets your colour on this server";
		return c.team == TEAM_SPECTATOR ? "Pick a team " XP_DOT " your character applies at once" : "Model changes show at once";
	case XP_SABER:
		return "Saber changes apply at your next spawn";
	case XP_FORCE:
		no = XP_NoForceReason( &c );
		if ( no ) {
			*warn = qtrue;
			return no;
		}
		if ( c.forceBasedTeams && ( c.team == TEAM_RED || c.team == TEAM_BLUE ) ) {
			return c.team == TEAM_RED ? "Red team is dark side on this server" : "Blue team is light side on this server";
		}
		return "Force changes apply when you respawn";
	case XP_TEAM:
		if ( c.gametype == GT_POWERDUEL ) return "Power Duel assigns teams " XP_DOT " pick a preference";
		if ( c.gametype == GT_SIEGE ) return "Choose a class to join";
		return "Pick a team " XP_DOT " your character applies at once";
	}
	return "";
}

void XP_Flash( const char *why ) {
	if ( why && why[0] ) {
		Q_strncpyz( xp.status, why, sizeof( xp.status ) );
		xp.statusUntil = uiInfo.uiDC.realTime + 2500;
	}
}

qboolean XP_Waiting( void ) {
	return (qboolean)( xp.numQueued > 0 );
}

qboolean UI_PlayerOwnerDraw( const rectDef_t *rect, int ownerDraw, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	switch ( ownerDraw ) {
	case UI_XP_SECTIONS:
		XP_Visibility();
		return qtrue;
	case UI_XP_LOOK_BG:
	case UI_XP_LOOK:
		UI_PlayerLookDraw( ownerDraw - UI_XP_LOOK_BG );
		return qtrue;
	default:
		return qfalse;
	}
}

/* ------------------------------------------------------------------ the commit */

static void XP_Send( const char *cmd, qboolean hold ) {
	if ( trap->Cvar_VariableValue( "developer" ) ) {
		Com_Printf( "player screen: %s %s\n", hold ? "holds" : "sends", cmd );
	}
	if ( hold && xp.numQueued < XP_QUEUE ) {
		Q_strncpyz( xp.queue[xp.numQueued++], cmd, sizeof( xp.queue[0] ) );
		return;
	}
	trap->Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", cmd ) );
}

static void XP_Leave( void ) {
	xpContext_t c;
	int i;

	for ( i = 0; i < xp.numQueued; i++ ) {	// closing early (Back, a second commit): what waits goes now
		trap->Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", xp.queue[i] ) );
	}
	xp.numQueued = 0;
	xp.leaveAfter = qfalse;
	XP_Context( &c );
	if ( c.connected ) {	// as closeingame
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
		trap->Key_ClearStates();
		trap->Cvar_Set( "cl_paused", "0" );
		Menus_CloseAll();
	} else {
		Menus_CloseByName( XP_MENU );
		Menus_ActivateByName( "main" );
	}
}

static qboolean XP_WarmApi( void ) {
	char api[16];

	trap->Cvar_VariableStringBuffer( "cl_uiWarmApi", api, sizeof( api ) );
	return (qboolean)( atoi( api ) >= 1 && trap->ext.R_WarmModel && trap->ext.R_WarmState
		&& trap->Cvar_VariableValue( "cl_modelWarm" ) != 0 );
}

// model cvar values name the same model and skin ("folder" is "folder/default")
static qboolean XP_SameModel( const char *a, const char *b ) {
	char x[MAX_QPATH], y[MAX_QPATH];

	Q_strncpyz( x, a, sizeof( x ) );
	Q_strncpyz( y, b, sizeof( y ) );
	if ( !strchr( x, '/' ) ) Q_strcat( x, sizeof( x ), "/default" );
	if ( !strchr( y, '/' ) ) Q_strcat( y, sizeof( y ), "/default" );
	return (qboolean)!Q_stricmp( x, y );
}

void XP_Commit( const char *what ) {
	xpContext_t c;
	char model[MAX_QPATH], draft[MAX_QPATH], name[MAX_NETNAME];
	qboolean touched, hold = qfalse;
	const char *team = NULL;
	int i;

	XP_Context( &c );
	for ( i = 0; i < xp.numQueued; i++ ) {	// a second commit while the first one's commands wait: those go first
		trap->Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", xp.queue[i] ) );
	}
	xp.numQueued = 0;
	if ( !Q_stricmp( what, "red" ) || !Q_stricmp( what, "blue" ) || !Q_stricmp( what, "free" ) || !Q_stricmp( what, "s" ) ) {
		team = what;
	} else if ( !Q_stricmp( what, "auto" ) ) {
		team = "free";		// the server picks in team modes, as ingame_join's Auto
	}

	// name and model: at once
	trap->Cvar_VariableStringBuffer( "ui_Name", name, sizeof( name ) );
	trap->Cvar_Set( "name", name );
	trap->Cvar_VariableStringBuffer( "model", model, sizeof( model ) );
	if ( UI_PickerDraftModel( draft, sizeof( draft ) ) && !XP_SameModel( draft, model ) ) {
		trap->Cvar_Set( "model", draft );		// as the grid (FEEDER_Q3HEADS) sets it
		trap->Cvar_Set( "char_color_red", "255" );
		trap->Cvar_Set( "char_color_green", "255" );
		trap->Cvar_Set( "char_color_blue", "255" );
		if ( c.connected && XP_WarmApi() ) {
			xp.warmHandle = trap->ext.R_WarmModel( draft, 0 );
			xp.warmStart = uiInfo.uiDC.realTime;
			xp.readyFrames = 0;
			// not for Siege's class screen, which may close before the warm-up ends (the class sets Force anyway)
			hold = (qboolean)( xp.warmHandle != 0 && Q_stricmp( what, "class" ) );
		}
	}

	// sabers and Force: the stock cvars
	UI_UpdateSaberCvars();
	touched = gTouchedForce;
	gTouchedForce = qfalse;
	UI_UpdateClientForcePowers( NULL );		// forcepowers only: the commands follow
	gTouchedForce = qfalse;

	if ( !c.connected ) {
		XP_Leave();
		return;
	}
	if ( !Q_stricmp( what, "class" ) ) {	// Siege: the stock class screen chooses the team
		if ( touched ) XP_Send( "forcechanged", hold );
		xp.classic = qtrue;
		Menus_CloseAll();
		Menus_ActivateByName( "ingame_siegeclass" );
		return;
	}
	if ( !Q_stricmp( what, "single" ) || !Q_stricmp( what, "double" ) ) {
		if ( touched ) XP_Send( "forcechanged", hold );
		XP_Send( va( "duelteam %s", what ), hold );
	} else if ( team ) {
		if ( c.gametype == GT_DUEL || c.gametype == GT_POWERDUEL ) {
			// the server's forcechanged changes no team in Duel (quirk 2)
			if ( touched ) XP_Send( "forcechanged", hold );
			XP_Send( va( "team %s", team ), hold );
		} else if ( touched ) {
			XP_Send( va( "forcechanged \"%s\"", team ), hold );
		} else {
			XP_Send( va( "team %s", team ), hold );		// stock sends nothing here (quirk 1)
		}
	} else if ( touched ) {		// apply
		XP_Send( "forcechanged", hold );
	}
	if ( xp.numQueued ) {
		xp.leaveAfter = qtrue;	// UI_PlayerFrame sends them, then closes
		return;
	}
	XP_Leave();
}

// every UI frame: the held commands go once the committed model is warm; the stock screens' bypass ends with them
void UI_PlayerFrame( void ) {
	if ( xp.numQueued ) {
		const int state = xp.warmHandle ? trap->ext.R_WarmState( xp.warmHandle ) : 1;
		const int timeout = (int)trap->Cvar_VariableValue( "cl_modelWarmTimeout" );
		if ( state != 0 ) {
			xp.readyFrames++;
		}
		if ( xp.readyFrames > 2 || uiInfo.uiDC.realTime - xp.warmStart > ( timeout > 0 ? timeout : 10000 ) + 500 ) {
			int i;
			for ( i = 0; i < xp.numQueued; i++ ) {
				trap->Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", xp.queue[i] ) );
			}
			xp.numQueued = 0;
			xp.warmHandle = 0;
			if ( xp.leaveAfter ) {
				xp.leaveAfter = qfalse;
				XP_Leave();
			}
		}
	}
	if ( xp.classic ) {
		int i;
		qboolean open = qfalse;
		for ( i = 0; i < (int)ARRAY_LEN( xpStock ) && !open; i++ ) {
			const menuDef_t *m = Menus_FindByName( xpStock[i].name );
			open = (qboolean)( m && ( m->window.flags & WINDOW_VISIBLE ) );
		}
		for ( i = 0; !open && i < 3; i++ ) {
			static const char *others[] = { "ingame_player2", "playerMenu2", "ingame_siegeclass" };
			const menuDef_t *m = Menus_FindByName( others[i] );
			open = (qboolean)( m && ( m->window.flags & WINDOW_VISIBLE ) );
		}
		if ( !open ) {
			xp.classic = qfalse;
		}
	}
}

/* ------------------------------------------------------------------ opening */

int XP_Section( void ) {
	return xp.section;
}

void XP_SetSection( int section ) {
	if ( section < 0 || section >= XP_NUM_SECTIONS ) {
		return;
	}
	if ( section == XP_TEAM ) {
		xpContext_t c;
		XP_Context( &c );
		if ( !c.connected ) {
			return;		// no Team section on the main menu
		}
	}
	xp.section = section;
	XP_Visibility();
}

// the next section that may open, in steps of dir
void XP_Step( int dir ) {
	int s = xp.section, k;

	for ( k = 0; k < XP_NUM_SECTIONS; k++ ) {
		s = ( s + dir + XP_NUM_SECTIONS ) % XP_NUM_SECTIONS;
		XP_SetSection( s );
		if ( xp.section == s ) {
			return;
		}
	}
}

// Menus_ActivateByName's redirect: this screen in place of the stock player screens
const char *UI_PlayerRedirect( const char *name ) {
	int i;

	// the cvar itself, not the per-frame copy: a script may set it and open a menu in one frame
	if ( !trap->Cvar_VariableValue( "ui_playerScreen" ) || xp.classic || !name ) {
		return NULL;
	}
	for ( i = 0; i < (int)ARRAY_LEN( xpStock ); i++ ) {
		if ( !Q_stricmp( name, xpStock[i].name ) ) {
			menuDef_t *menu = Menus_FindByName( XP_MENU );
			uiClientState_t cs;
			if ( !menu ) {
				return NULL;	// no screen file: the stock screen
			}
			trap->GetClientState( &cs );
			menu->fullScreen = (qboolean)( cs.connState < CA_CONNECTED );	// see ui_picker.c
			menu->window.backColor[3] = menu->fullScreen ? 1.0f : 0.6f;
			xp.openSection = xpStock[i].section;
			return XP_MENU;
		}
	}
	return NULL;
}

static void XP_Open( void ) {
	xp.menu = Menus_FindByName( XP_MENU );
	if ( xp.menu ) {
		XP_MapSections( xp.menu );
	}
	xp.status[0] = 0;
	xp.forceRank = uiForceRank;
	xp.forceSide = uiForceSide;
	xp.jediNonJedi = uiJediNonJedi;
	memcpy( xp.forcePowers, uiForcePowersRank, sizeof( xp.forcePowers ) );
	xp.touchedForce = gTouchedForce;
	xp.section = XP_CHARACTER;
	XP_SetSection( xp.openSection );
	xp.openSection = XP_CHARACTER;
	UI_PlayerLookOpen();
}

void XP_Back( void ) {
	// drop the Force draft (the picker, saber and name drafts reload on the next open); a rank the server sent while
	// the screen was open (nfr) stays, as it was there at open
	uiForceRank = xp.forceRank;
	uiForceSide = xp.forceSide;
	uiJediNonJedi = xp.jediNonJedi;
	memcpy( uiForcePowersRank, xp.forcePowers, sizeof( xp.forcePowers ) );
	gTouchedForce = xp.touchedForce;
	UpdateForceUsed();
	XP_Leave();
}

void XP_Classic( void ) {
	xpContext_t c;

	XP_Context( &c );
	xp.classic = qtrue;
	Menus_CloseByName( XP_MENU );
	if ( c.connected ) {	// as UIMENU_PLAYERCONFIG
		Menus_CloseAll();
		Menus_ActivateByName( "ingame_player" );
		UpdateForceUsed();
	} else {
		Menus_ActivateByName( "playerMenu" );
	}
}

qboolean UI_PlayerScript( const char *name, char **args ) {
	const char *arg;
	int n;

	if ( Q_stricmpn( name, "xp", 2 ) ) {
		return qfalse;
	}
	if ( !Q_stricmp( name, "xpOpen" ) ) {
		XP_Open();
	} else if ( !Q_stricmp( name, "xpSection" ) ) {
		if ( Int_Parse( args, &n ) ) XP_SetSection( n );
	} else if ( !Q_stricmp( name, "xpCommit" ) ) {
		if ( String_Parse( args, &arg ) ) XP_Commit( arg );
	} else if ( !Q_stricmp( name, "xpBack" ) ) {
		XP_Back();
	} else if ( !Q_stricmp( name, "xpClassic" ) ) {
		XP_Classic();
	} else {
		return qfalse;
	}
	return qtrue;
}

// the screen's keys: all of them go to the look (ui_player_look.c)
qboolean UI_PlayerMenuKey( menuDef_t *menu, int key, qboolean down ) {
	if ( !menu || !menu->window.name || Q_stricmp( menu->window.name, XP_MENU ) ) {
		return qfalse;
	}
	return UI_PlayerLookKey( key, down );
}

void UI_PlayerLoadMenu( void ) {
	fileHandle_t f;
	const int len = trap->FS_Open( XP_MENU_FILE, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	if ( len > 0 && !Menus_FindByName( XP_MENU ) ) {
		UI_ParseMenu( XP_MENU_FILE );
	}
	xp.menu = Menus_FindByName( XP_MENU );
	if ( xp.menu ) {
		XP_MapSections( xp.menu );
	}
}

// the UI's menu-level key hook (displayContextDef_t::ext.menuHandleKey): this screen, then the picker
qboolean UI_XenMenuKey( menuDef_t *menu, int key, qboolean down ) {
	return (qboolean)( UI_PickerMenuKey( menu, key, down ) || UI_PlayerMenuKey( menu, key, down ) );
}

/* ------------------------------------------------------------------ the stock items the look drives */

itemDef_t *XP_Item( const char *name ) {
	int i;

	if ( !xp.menu ) {
		return NULL;
	}
	for ( i = 0; i < xp.menu->itemCount; i++ ) {
		if ( XP_NameIs( xp.menu->items[i], name ) ) {
			return xp.menu->items[i];
		}
	}
	return NULL;
}

// a button's action, as a click on it
static void XP_RunPress( itemDef_t *item ) {
	Item_RunScript( item, item->action );
}

// a list row picked, as a click on it: the feeder's selection, then the list's action
static qboolean XP_RunSelect( itemDef_t *item, int row ) {
	listBoxDef_t *list = item->typeData.listbox;

	if ( row < 0 || row >= uiInfo.uiDC.feederCount( item->special ) ) {
		return qfalse;
	}
	item->cursorPos = row;
	if ( list ) list->cursorPos = row;
	uiInfo.uiDC.feederSelection( item->special, row, item );
	Item_RunScript( item, item->action );
	return qtrue;
}

// a key on an ownerdraw (raise = A_MOUSE1, lower = A_MOUSE2 on the Force rows), then its action (the sound)
static qboolean XP_RunKey( itemDef_t *item, int key ) {
	if ( !uiInfo.uiDC.ownerDrawHandleKey( item->window.ownerDraw, item->window.ownerDrawFlags, &item->special, key ) ) {
		return qfalse;
	}
	Item_RunScript( item, item->action );
	return qtrue;
}

// the named item that does the job: a button with an action, a list, or an ownerdraw (stock names repeat: the Force
// rows have a label and an ownerdraw of one name)
static itemDef_t *XP_Find( const char *name, int kind ) {
	int i;

	if ( !xp.menu ) {
		return NULL;
	}
	for ( i = 0; i < xp.menu->itemCount; i++ ) {
		itemDef_t *it = xp.menu->items[i];
		if ( !XP_NameIs( it, name ) ) continue;
		if ( kind == 1 ? it->type == ITEM_TYPE_LISTBOX : kind == 2 ? it->window.ownerDraw != 0
			: ( it->action != NULL && !( it->window.flags & WINDOW_DECORATION ) ) ) {
			return it;
		}
	}
	return NULL;
}

qboolean XP_PressItem( const char *name ) {
	itemDef_t *item = XP_Find( name, 0 );
	if ( item ) XP_RunPress( item );
	return (qboolean)( item != NULL );
}

qboolean XP_SelectRow( const char *name, int row ) {
	itemDef_t *item = XP_Find( name, 1 );
	return (qboolean)( item && XP_RunSelect( item, row ) );
}

qboolean XP_ItemKey( const char *name, int key ) {
	itemDef_t *item = XP_Find( name, 2 );
	return (qboolean)( item && XP_RunKey( item, key ) );
}

/* ------------------------------------------------------------------ scripted checks */

/*
ui_menuitem <menu> <item>[#n] [press | select <row> | key <raise|lower|number> | menukey <number> | click <x> <y>]

Works one item of an open menu as a player's click would (scripted client checks; the player screen's command parity
check): press runs a button's action; select picks a list row (the feeder's selection, then the action); key sends a
key to an ownerdraw (raise = left click, lower = right click, as the stock Force rows take them); menukey sends a key
to the menu's own key hook (the player screen's keys; the item is ignored); click puts the cursor at x y (640 x 480)
and sends a left click to the menu's key hook (the item is ignored). #n takes the n-th (from 0) of the reachable
items of that name (stock ingame_join names every button "team"). The menu is opened first when it is not (through
Menus_ActivateByName, so the player screen's redirect applies). An item a player could not reach is not taken: one
hidden, or hidden by its cvar test; on the player screen, whose stock items the look drives, one whose section is
closed or whose rule fails (XP_Reachable).
*/
void UI_MenuItem_f( void ) {
	char menuName[MAX_QPATH], itemName[MAX_QPATH], mode[16], arg[16], arg2[16], *hash;
	menuDef_t *menu;
	itemDef_t *item = NULL;
	int i, key = 0, nth = 0;

	if ( trap->Cmd_Argc() < 3 ) {
		Com_Printf( "usage: ui_menuitem <menu> <item>[#n] [press | select <row> | key <raise|lower|number> | menukey <number> | click <x> <y>]\n" );
		return;
	}
	trap->Cmd_Argv( 1, menuName, sizeof( menuName ) );
	trap->Cmd_Argv( 2, itemName, sizeof( itemName ) );
	trap->Cmd_Argv( 3, mode, sizeof( mode ) );
	trap->Cmd_Argv( 4, arg, sizeof( arg ) );
	trap->Cmd_Argv( 5, arg2, sizeof( arg2 ) );
	if ( !mode[0] ) {
		Q_strncpyz( mode, "press", sizeof( mode ) );
	}
	if ( ( hash = strchr( itemName, '#' ) ) != NULL ) {
		*hash = 0;
		nth = atoi( hash + 1 );
	}
	menu = Menus_FindByName( menuName );
	if ( !menu || !( menu->window.flags & WINDOW_VISIBLE ) ) {
		menu = Menus_ActivateByName( menuName );
		if ( !menu ) {
			Com_Printf( "ui_menuitem: no menu %s\n", menuName );
			return;
		}
		trap->Key_SetCatcher( KEYCATCH_UI );
	}
	if ( menu == xp.menu ) {
		XP_Visibility();
	}
	if ( !Q_stricmp( mode, "click" ) ) {
		uiInfo.uiDC.cursorx = atoi( arg );
		uiInfo.uiDC.cursory = atoi( arg2 );
		Com_Printf( "ui_menuitem: %s click %s %s %s\n", menu->window.name, arg, arg2,
			uiInfo.uiDC.ext.menuHandleKey && uiInfo.uiDC.ext.menuHandleKey( menu, A_MOUSE1, qtrue ) ? "taken" : "not taken" );
		return;
	}
	if ( !Q_stricmp( mode, "menukey" ) ) {
		key = atoi( arg );
		Com_Printf( "ui_menuitem: %s key %d %s\n", menu->window.name, key,
			uiInfo.uiDC.ext.menuHandleKey && uiInfo.uiDC.ext.menuHandleKey( menu, key, qtrue ) ? "taken" : "not taken" );
		return;
	}
	for ( i = 0; i < menu->itemCount; i++ ) {
		itemDef_t *it = menu->items[i];
		if ( !it->window.name || Q_stricmp( it->window.name, itemName ) ) continue;
		if ( menu == xp.menu ? !XP_Reachable( i ) : ( !( it->window.flags & WINDOW_VISIBLE ) || !Item_EnableShowViaCvar( it, CVAR_SHOW ) ) ) continue;
		if ( !Q_stricmp( mode, "select" ) ? it->type == ITEM_TYPE_LISTBOX
			: !Q_stricmp( mode, "key" ) ? it->window.ownerDraw != 0
			: ( it->action != NULL && !( it->window.flags & WINDOW_DECORATION ) ) ) {
			if ( nth-- == 0 ) {
				item = it;
				break;
			}
		}
	}
	if ( !item ) {
		Com_Printf( "ui_menuitem: %s has no reachable item %s to %s\n", menu->window.name, itemName, mode );
		return;
	}
	if ( !Q_stricmp( mode, "select" ) ) {
		const int row = atoi( arg );
		if ( !XP_RunSelect( item, row ) ) {
			Com_Printf( "ui_menuitem: %s/%s has no row %d\n", menu->window.name, itemName, row );
			return;
		}
	} else if ( !Q_stricmp( mode, "key" ) ) {
		key = !Q_stricmp( arg, "raise" ) ? A_MOUSE1 : !Q_stricmp( arg, "lower" ) ? A_MOUSE2 : atoi( arg );
		if ( !XP_RunKey( item, key ) ) {
			Com_Printf( "ui_menuitem: %s/%s did not take key %d\n", menu->window.name, itemName, key );
			return;
		}
	} else {
		XP_RunPress( item );
	}
	Com_Printf( "ui_menuitem: %s/%s %s %s\n", menu->window.name, itemName, mode, arg );
}
