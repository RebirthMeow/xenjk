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

// cg_xenanims.c -- the xenjk animation set (cg_xenAnims), from cg_players.c

#include "cg_local.h"

/*
xenjk animation set (cg_xenAnims): models/players/_humanoid_xen/animation.cfg and animevents.cfg hold the stock rows
plus replacement rows whose frames are appended after a GLA's stock frames. A humanoid model plays the set while
cg_xenAnims is on and its GLA holds those frames; any other model keeps the stock set. The set drives display only
(frames, anim events, ragdoll and corpse poses): prediction, timers and the humanoid checks keep localAnimIndex, the
stock set the server also uses, so the set's gameplay rows keep stock timing.
*/
#define XEN_ANIM_DIR	"models/players/_humanoid_xen/"

static int xenAnimSet = -1;		// bgAllAnims index (> 1) once loaded, 0 when there is no set, -1 not tried yet
static int xenAnimEvents;		// bgAllEvents index of its events, 0 (the stock events) when it has none
static int xenAnimFrames;		// frames a GLA needs to play the set: the end of its last replacement row
static char xenAnimGLAs[2048];	// glas.txt: the GLAs that hold the set's frames, when a pack pads other GLAs past them
static qboolean xenAnimGLAsTooLong;	// a glas.txt past the buffer: no GLA counts as listed (a partial list would mislead)

static qboolean CG_XenAnimLoaded( void )
{
	fileHandle_t f;
	int i, len;

	if ( xenAnimSet > 1 && bgAllAnims[xenAnimSet].anims && !Q_stricmp( bgAllAnims[xenAnimSet].filename, XEN_ANIM_DIR "animation.cfg" ) )
		return qtrue;
	if ( xenAnimSet == 0 )
		return qfalse;

	xenAnimSet = 0;
	len = trap->FS_Open( XEN_ANIM_DIR "animation.cfg", &f, FS_READ );
	if ( !f )
		return qfalse;
	trap->FS_Close( f );
	if ( len <= 0 )
		return qfalse;

	// a path outside players/_humanoid/ gets its own slot instead of the static humanoid set
	i = BG_ParseAnimationFile( XEN_ANIM_DIR "animation.cfg", NULL, qfalse );
	if ( i <= 1 )
		return qfalse;

	// only the replacement rows count: a row the set shares with the stock humanoid set (the bike rider's CIN rows,
	// which both sets carry) asks no more of a GLA than the stock set does, and padding a GLA up to it must not
	// make the GLA look as if it held the replacements
	xenAnimFrames = 0;
	for ( len = 0; len < MAX_ANIMATIONS; len++ )
	{
		const animation_t *a = &bgAllAnims[i].anims[len];
		const animation_t *s = bgAllAnims[0].anims ? &bgAllAnims[0].anims[len] : NULL;
		if ( s && s->firstFrame == a->firstFrame && s->numFrames == a->numFrames )
			continue;
		xenAnimFrames = Q_max( xenAnimFrames, a->firstFrame + a->numFrames );
	}
	xenAnimEvents = BG_ParseAnimationEvtFile( XEN_ANIM_DIR, i, bgNumAnimEvents );
	if ( xenAnimEvents < 0 )
		xenAnimEvents = 0;

	// a pack that appends a later block (the bike rider) pads every GLA up to it with stand-pose frames, so a frame
	// count no longer shows which GLAs hold the set; such a pack lists them, one GLA name (no path) per token
	xenAnimGLAs[0] = 0;
	xenAnimGLAsTooLong = qfalse;
	len = trap->FS_Open( XEN_ANIM_DIR "glas.txt", &f, FS_READ );
	if ( f )
	{
		if ( len > 0 && len < (int)sizeof( xenAnimGLAs ) )
		{
			trap->FS_Read( xenAnimGLAs, len, f );
			xenAnimGLAs[len] = 0;
		}
		else if ( len >= (int)sizeof( xenAnimGLAs ) )
		{
			trap->Print( S_COLOR_YELLOW "WARNING: " XEN_ANIM_DIR "glas.txt is %d bytes (the limit is %d): no GLA takes the xenjk animation set\n",
				len, (int)sizeof( xenAnimGLAs ) - 1 );
			xenAnimGLAsTooLong = qtrue;
		}
		trap->FS_Close( f );
	}
	xenAnimSet = i;
	return qtrue;
}

static qboolean CG_XenAnimGLAListed( const char *glaName )
{
	char name[MAX_QPATH];
	const char *p = xenAnimGLAs, *tok;

	if ( xenAnimGLAsTooLong )
		return qfalse;
	if ( !xenAnimGLAs[0] )
		return qtrue;
	COM_StripExtension( COM_SkipPath( (char *)glaName ), name, sizeof( name ) );
	while ( 1 )
	{
		tok = COM_ParseExt( &p, qtrue );
		if ( !tok[0] )
			return qfalse;
		if ( !Q_stricmp( tok, name ) )
			return qtrue;
	}
}

int CG_GLAFrameCount( const char *glaName )
{
	static struct { char name[MAX_QPATH]; int frames; } cache[32];
	static int numCached;
	char path[MAX_QPATH];
	byte header[80];
	fileHandle_t f;
	int i, frames = 0;

	for ( i = 0; i < numCached; i++ )
	{
		if ( !Q_stricmp( cache[i].name, glaName ) )
			return cache[i].frames;
	}

	Com_sprintf( path, sizeof( path ), "%s%s", glaName, Q_stricmp( COM_GetExtension( glaName ), "gla" ) ? ".gla" : "" );
	if ( trap->FS_Open( path, &f, FS_READ ) >= (int)sizeof( header ) && f )
	{
		trap->FS_Read( header, sizeof( header ), f );
		if ( !memcmp( header, "2LGA", 4 ) )
		{	// mdxaHeader_t: ident, version, name[64], fScale, numFrames
			frames = LittleLong( *(int *)( header + 76 ) );
		}
	}
	if ( f )
		trap->FS_Close( f );

	if ( numCached < (int)ARRAY_LEN( cache ) )
	{
		Q_strncpyz( cache[numCached].name, glaName, sizeof( cache[numCached].name ) );
		cache[numCached++].frames = frames;
	}
	return frames;
}

// the set a humanoid ghoul2 model can play (cg_xenAnims aside), 0 when it cannot
static int CG_XenAnimSetForModel( void *g2, int skel )
{
	char GLAName[MAX_QPATH];

	if ( skel != 0 || !g2 || !CG_XenAnimLoaded() )
		return 0;

	GLAName[0] = 0;
	trap->G2API_GetGLAName( g2, 0, GLAName );
	if ( !GLAName[0] || CG_GLAFrameCount( GLAName ) < xenAnimFrames || !CG_XenAnimGLAListed( GLAName ) )
		return 0;
	return xenAnimSet;
}

// the set a model about to go on a centity will display
int CG_AnimSetForModel( void *g2, int skel )
{
	const int set = cg_xenAnims.integer ? CG_XenAnimSetForModel( g2, skel ) : 0;

	return set ? set : skel;
}

// works the centity's set out again when its ghoul2 or stock set changed (players get ghoul2 on several paths)
static void CG_XenAnimCheck( centity_t *cent )
{
	// the GLA's name too, read once a frame: a new instance can sit at a freed one's address with the same
	// stock set and another GLA, and only some paths reset xenAnimG2 by hand
	char gla[MAX_QPATH];
	gla[0] = 0;
	if ( cent->ghoul2 && cent->xenAnimGlaTime != cg.time ) {
		trap->G2API_GetGLAName( cent->ghoul2, 0, gla );
		cent->xenAnimGlaTime = cg.time;
		if ( Q_stricmp( gla, cent->xenAnimGla ) ) {
			Q_strncpyz( cent->xenAnimGla, gla, sizeof( cent->xenAnimGla ) );
			cent->xenAnimG2 = NULL;
		}
	}
	if ( cent->xenAnimG2 == cent->ghoul2 && cent->xenAnimSkel == cent->localAnimIndex )
		return;
	cent->xenAnimG2 = cent->ghoul2;
	cent->xenAnimSkel = cent->localAnimIndex;
	cent->xenAnimIndex = CG_XenAnimSetForModel( cent->ghoul2, cent->localAnimIndex );
	cent->xenEventIndex = cent->xenAnimIndex ? xenAnimEvents : 0;

	if ( cg_debugAnim.integer && cent->ghoul2 )
	{
		char GLAName[MAX_QPATH];

		GLAName[0] = 0;
		trap->G2API_GetGLAName( cent->ghoul2, 0, GLAName );
		trap->Print( "xenAnims: %d: '%s' %s\n", cent->currentState.number, GLAName,
			cent->xenAnimIndex ? "can play the xenjk set" : "keeps the stock set" );
	}
}

// the bgAllAnims set a centity displays
int CG_AnimSet( centity_t *cent )
{
	CG_XenAnimCheck( cent );
	return ( cg_xenAnims.integer && cent->xenAnimIndex > 1 ) ? cent->xenAnimIndex : cent->localAnimIndex;
}

// the bgAllEvents set that goes with CG_AnimSet
int CG_EventSet( centity_t *cent )
{
	CG_XenAnimCheck( cent );
	return ( cg_xenAnims.integer && cent->xenAnimIndex > 1 ) ? cent->xenEventIndex : cent->eventAnimIndex;
}
