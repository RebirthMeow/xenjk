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

// cg_screenpos.c -- playtest issue reports.
//
// "screenpos <description>" (e.g. /screenpos hole in floor) writes, under
// screenshots/screenpos/ in the homepath:
//   <stamp>_<words>.txt          the description, map, view and player state, what
//                                the crosshair hits (collision and shot traces),
//                                the floor below, nearby players, renderer settings
//   <stamp>_<words>.jpg          a screenshot, taken once the console has closed
//                                (.tga under rd-vanilla, whose "screenshot" writes TGA)
//   <stamp>_console.txt          a condump (recent warnings and errors; condump takes
//                                at most 63 characters, so the stamp alone names it)
// and appends one line to screenshots/screenpos/index.txt. Bound to a key it
// works without a description. Idle unless the command is used.

#include "cg_local.h"

#define SCREENPOS_FPS_FRAMES 64

static struct {
	char	stamp[16];		// YYYYMMDD-HHMMSS
	char	name[MAX_QPATH];
	int		shotAt;		// real ms; 0 = nothing pending
	int		printAt;
	int		frameMs[SCREENPOS_FPS_FRAMES];
	int		frameCount;
	int		lastFrame;
} spos;

static float CG_Screenpos_Fps( void )
{
	int i, n = Q_min( spos.frameCount, SCREENPOS_FPS_FRAMES ), sum = 0;

	for ( i = 0; i < n; i++ )
		sum += spos.frameMs[i];
	return sum > 0 ? 1000.0f * n / sum : 0.0f;
}

// once per client frame, from CG_DrawActiveFrame
void CG_Screenpos_Frame( void )
{
	int now = trap->Milliseconds();

	if ( spos.lastFrame )
		spos.frameMs[spos.frameCount++ % SCREENPOS_FPS_FRAMES] = now - spos.lastFrame;
	spos.lastFrame = now;

	if ( spos.shotAt && now >= spos.shotAt )
	{
		// the renderer prefixes screenshots/ and adds .jpg
		trap->SendConsoleCommand( va( "screenshot screenpos/%s\n", spos.name ) );
		spos.shotAt = 0;
		spos.printAt = now + 400;	// after the shot, so the notify line is not in it
	}
	if ( spos.printAt && now >= spos.printAt )
	{
		// the console log after the shot too: condump prints a notify line
		trap->SendConsoleCommand( va( "condump screenshots/screenpos/%s_console.txt\n", spos.stamp ) );
		trap->Print( "screenpos: saved screenshots/screenpos/%s (screenshot and .txt), %s_console.txt\n", spos.name,
			spos.stamp );
		spos.printAt = 0;
	}
}

static void CG_Screenpos_Write( fileHandle_t f, const char *fmt, ... )
{
	va_list ap;
	char text[1024];

	va_start( ap, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	trap->FS_Write( text, strlen( text ), f );
}

static const char *CG_Screenpos_EntityName( int num )
{
	const entityState_t *es;

	if ( num == ENTITYNUM_WORLD )
		return "world";
	if ( num == ENTITYNUM_NONE )
		return "none";
	if ( num < MAX_CLIENTS )
	{
		const clientInfo_t *ci = &cgs.clientinfo[num];
		return va( "client %d %s (model %s/%s)", num, ci->name, ci->modelName, ci->skinName );
	}
	es = &cg_entities[num].currentState;
	if ( es->modelindex > 0 && es->modelindex < MAX_MODELS && CG_ConfigString( CS_MODELS + es->modelindex )[0] )
		return va( "entity %d type %d model %s", num, es->eType, CG_ConfigString( CS_MODELS + es->modelindex ) );
	return va( "entity %d type %d", num, es->eType );
}

static void CG_Screenpos_Trace( fileHandle_t f, const char *label, const vec3_t start, const vec3_t end, int skip, int mask )
{
	trace_t tr;

	CG_Trace( &tr, start, NULL, NULL, end, skip, mask );
	if ( tr.startsolid )
		CG_Screenpos_Write( f, "%s: starts inside solid (allsolid %d, contents 0x%x)\n", label, tr.allsolid, tr.contents );
	if ( tr.fraction >= 1.0f )
	{
		CG_Screenpos_Write( f, "%s: nothing within %.0f u\n", label, Distance( start, end ) );
		return;
	}
	CG_Screenpos_Write( f, "%s: hit at %.1f u, point (%.2f %.2f %.2f), normal (%.3f %.3f %.3f), %s, surfaceFlags 0x%x, contents 0x%x\n",
		label, tr.fraction * Distance( start, end ), tr.endpos[0], tr.endpos[1], tr.endpos[2],
		tr.plane.normal[0], tr.plane.normal[1], tr.plane.normal[2], CG_Screenpos_EntityName( tr.entityNum ),
		tr.surfaceFlags, tr.contents );
}

static void CG_Screenpos_Cvars( fileHandle_t f )
{
	static const char *names[] = {
		"version", "cl_renderer", "r_mode", "r_customwidth", "r_customheight", "r_fullscreen", "r_ext_multisample",
		"com_maxfps", "r_cloth", "g_gametype", "sv_fps", "sv_mapChecksum", "timescale", "sv_cheats",
	};
	char value[MAX_CVAR_VALUE_STRING];
	size_t i;

	for ( i = 0; i < ARRAY_LEN( names ); i++ )
	{
		trap->Cvar_VariableStringBuffer( names[i], value, sizeof( value ) );
		CG_Screenpos_Write( f, "  %s \"%s\"\n", names[i], value );
	}
}

void CG_Screenpos_f( void )
{
	const playerState_t *ps = &cg.predictedPlayerState;
	const vec_t *eye = cg.refdef.vieworg;
	vec3_t ang;
	char desc[MAX_STRING_CHARS], words[25], mapname[MAX_QPATH];
	fileHandle_t f;
	qtime_t qt;
	vec3_t forward, end;
	int i, n = 0;

	if ( !cg.snap )
	{
		trap->Print( "screenpos: not in a game\n" );
		return;
	}
	for ( i = 0; i < 3; i++ )
		ang[i] = AngleNormalize180( cg.refdef.viewangles[i] );
	trap->Cmd_Args( desc, sizeof( desc ) );
	if ( !desc[0] )
		Q_strncpyz( desc, "(no description)", sizeof( desc ) );

	// file name: time stamp plus the first words of the description
	for ( i = 0; desc[i] && n < (int)sizeof( words ) - 1; i++ )
	{
		char c = tolower( (unsigned char)desc[i] );
		if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) )
			words[n++] = c;
		else if ( n && words[n - 1] != '-' )
			words[n++] = '-';
	}
	while ( n && words[n - 1] == '-' )
		n--;
	words[n] = '\0';
	trap->RealTime( &qt );
	Com_sprintf( spos.stamp, sizeof( spos.stamp ), "%04d%02d%02d-%02d%02d%02d", qt.tm_year + 1900, qt.tm_mon + 1,
		qt.tm_mday, qt.tm_hour, qt.tm_min, qt.tm_sec );
	Com_sprintf( spos.name, sizeof( spos.name ), "%s%s%s", spos.stamp, n ? "_" : "", words );
	COM_StripExtension( COM_SkipPath( (char *)cgs.mapname ), mapname, sizeof( mapname ) );

	if ( trap->FS_Open( va( "screenshots/screenpos/%s.txt", spos.name ), &f, FS_WRITE ) < 0 || !f )
	{
		trap->Print( "screenpos: cannot write screenshots/screenpos/%s.txt\n", spos.name );
		return;
	}
	CG_Screenpos_Write( f, "issue: " );
	trap->FS_Write( desc, strlen( desc ), f );	// the whole description, whatever its length, then its line end
	CG_Screenpos_Write( f, "\n" );
	CG_Screenpos_Write( f, "when: %04d-%02d-%02d %02d:%02d:%02d, level time %d ms\n", qt.tm_year + 1900, qt.tm_mon + 1,
		qt.tm_mday, qt.tm_hour, qt.tm_min, qt.tm_sec, cg.time - cgs.levelStartTime );
	CG_Screenpos_Write( f, "map: %s\n", cgs.mapname );
	CG_Screenpos_Write( f, "viewpos: %s (%i %i %i) : %i\n", cgs.mapname, (int)eye[0], (int)eye[1], (int)eye[2], (int)ang[YAW] );
	CG_Screenpos_Write( f, "eye: (%.2f %.2f %.2f), angles pitch %.2f yaw %.2f roll %.2f, fov %.1f x %.1f%s\n",
		eye[0], eye[1], eye[2], ang[PITCH], ang[YAW], ang[ROLL], cg.refdef.fov_x, cg.refdef.fov_y,
		cg.renderingThirdPerson ? ", third person" : "" );
	CG_Screenpos_Write( f, "go back: setviewpos %.1f %.1f %.1f %.1f (devmap); exact view: benchcam %.2f %.2f %.2f %.2f %.2f\n",
		ps->origin[0], ps->origin[1], ps->origin[2], ang[YAW], eye[0], eye[1], eye[2], ang[YAW], ang[PITCH] );
	CG_Screenpos_Write( f, "player: client %d, origin (%.2f %.2f %.2f), velocity (%.1f %.1f %.1f) %.0f u/s, viewheight %d\n",
		ps->clientNum, ps->origin[0], ps->origin[1], ps->origin[2], ps->velocity[0], ps->velocity[1], ps->velocity[2],
		VectorLength( ps->velocity ), ps->viewheight );
	CG_Screenpos_Write( f, "  ground %s, pm_type %d, pm_flags 0x%x, legsAnim %d, torsoAnim %d, health %d, model %s/%s\n",
		CG_Screenpos_EntityName( ps->groundEntityNum ), ps->pm_type, ps->pm_flags, ps->legsAnim, ps->torsoAnim,
		ps->stats[STAT_HEALTH], cgs.clientinfo[ps->clientNum].modelName, cgs.clientinfo[ps->clientNum].skinName );

	AngleVectors( ang, forward, NULL, NULL );
	VectorMA( eye, 8192, forward, end );
	CG_Screenpos_Trace( f, "crosshair, player collision", eye, end, ps->clientNum, MASK_PLAYERSOLID );
	CG_Screenpos_Trace( f, "crosshair, shots", eye, end, ps->clientNum, MASK_SHOT );
	VectorSet( end, ps->origin[0], ps->origin[1], ps->origin[2] - 4096 );
	CG_Screenpos_Trace( f, "below the player", ps->origin, end, ps->clientNum, MASK_PLAYERSOLID );

	CG_Screenpos_Write( f, "players and NPCs within 1024 u:\n" );
	for ( i = 0, n = 0; i < MAX_GENTITIES; i++ )
	{
		const centity_t *cent = &cg_entities[i];
		float d;

		if ( i == ps->clientNum || !cent->currentValid )
			continue;
		if ( i < MAX_CLIENTS ? !cgs.clientinfo[i].infoValid : cent->currentState.eType != ET_NPC )
			continue;
		d = Distance( cent->lerpOrigin, ps->origin );
		if ( d > 1024 )
			continue;
		if ( i < MAX_CLIENTS || !cent->npcClient || !cent->npcClient->modelName[0] )
			CG_Screenpos_Write( f, "  %s at %.0f u (%.0f %.0f %.0f)\n", CG_Screenpos_EntityName( i ), d,
				cent->lerpOrigin[0], cent->lerpOrigin[1], cent->lerpOrigin[2] );
		else
			CG_Screenpos_Write( f, "  NPC %d (model %s/%s) at %.0f u (%.0f %.0f %.0f)\n", i, cent->npcClient->modelName,
				cent->npcClient->skinName, d, cent->lerpOrigin[0], cent->lerpOrigin[1], cent->lerpOrigin[2] );
		n++;
	}
	if ( !n )
		CG_Screenpos_Write( f, "  none\n" );

	CG_Screenpos_Write( f, "video: %s, %dx%d, %.0f fps over the last %d frames\n", cgs.glconfig.renderer_string,
		cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, CG_Screenpos_Fps(), Q_min( spos.frameCount, SCREENPOS_FPS_FRAMES ) );
	CG_Screenpos_Write( f, "cvars:\n" );
	CG_Screenpos_Cvars( f );
	trap->FS_Close( f );

	if ( trap->FS_Open( "screenshots/screenpos/index.txt", &f, FS_APPEND ) >= 0 && f )
	{
		CG_Screenpos_Write( f, "%s  %s  (%i %i %i) : %i  %s\n", spos.name, mapname, (int)eye[0], (int)eye[1], (int)eye[2],
			(int)ang[YAW], desc );
		trap->FS_Close( f );
	}

	// the screenshot once the console has slid away (toggleconsole also clears the notify lines), then the console log
	if ( trap->Key_GetCatcher() & KEYCATCH_CONSOLE )
	{
		trap->SendConsoleCommand( "toggleconsole\n" );
		spos.shotAt = trap->Milliseconds() + 500;
	}
	else
	{
		spos.shotAt = trap->Milliseconds() + 1;
	}
	spos.printAt = 0;
}
