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

// Character shadows and self-shadows: shadow-only lights from the light grid, one tile each in the sun shadow
// image's atlas layer. RE_RenderScene calls R_SetupCharShadows, R_SetupCharSelfShadows and R_RenderCharShadows;
// R_AddDrawSurf and R_AddEntitySurface keep their shadow-view tests (tr_main.cpp)

#include "tr_local.h"
#include "ghoul2/g2_local.h"
#include <string>

/*
=============================================================================

CHARACTER SHADOWS (docs: xenjk docs/renderer-character-shadows.md)

Shadow-only lights over the baked lighting. Each Ghoul2 model near the camera gets one light from the light grid
around it: the samples' directions weighted by their directed light, the part that does not agree counted as
ambient, raised to r_charShadowMinElevation and smoothed over time. Its tile in the atlas layer (layer 3 of the sun
shadow image) is an orthographic depth view of that model alone, along the light. The lit world shaders (lightmap
and vertex light, gen_frag.tmpl charShadow) remove from the baked light the share the caster blocks:
occlusion x NL / ( ambient / directed + NL ), with NL the receiver's facing to the caster's light.

=============================================================================
*/

#define CHAR_SHADOW_RADIUS	72.0f	// around the refEntity origin (mid-body): a standing, jumping or swinging character
#define CHAR_SHADOW_REACH	160.0f	// how far past the caster, along the light, receivers are shadowed
#define CHAR_SHADOW_REACH_MAX	1536.0f	// r_charShadowFloor: the reach grown to an airborne caster's floor, at most
#define CHAR_SHADOW_FLOOR_DROP	1024.0f	// r_charShadowFloor: how far below the origin the floor is looked for
#define CHAR_SHADOW_STAND	24.0f	// a standing character's origin above its floor (the player box's mins)
#define CHAR_SHADOW_SMOOTH	250.0f	// ms: time constant of the light's smoothing
#define CHAR_SHADOW_TRACKS	32

typedef struct {
	const void	*key;			// the entity's Ghoul2 instance: one per client entity, kept across frames
	int			time;			// refdef time of the last update
	vec3_t		dir;			// toward the light
	float		ambient, directed;	// luminance
} charShadowTrack_t;

static charShadowTrack_t	s_charShadowTracks[CHAR_SHADOW_TRACKS];
static qboolean				s_charShadowDumpPending;
static viewParms_t			s_charShadowViews[MAX_CHAR_SHADOWS + MAX_CHAR_SELF_SHADOWS];
static int					s_charShadowEnts[MAX_CHAR_SHADOWS + MAX_CHAR_SELF_SHADOWS];
static int					s_numCharShadowViews;

static float R_Luminance( const vec3_t c )
{
	return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

// the light around a caster: five grid samples (centre and 40 u to each side), each direction weighted by its
// directed light; the length of the weighted sum is the light that agrees, the rest of the directed light ambient
static void R_CharShadowLight( const vec3_t center, vec3_t dir, float *ambient, float *directed )
{
	static const float	offset[5][2] = { { 0, 0 }, { 40, 0 }, { -40, 0 }, { 0, 40 }, { 0, -40 } };
	vec3_t	sum, amb, dl, ldir, p;
	float	a = 0.0f, d = 0.0f, agree;
	int		i;

	VectorClear( sum );
	for ( i = 0; i < 5; i++ ) {
		VectorSet( p, center[0] + offset[i][0], center[1] + offset[i][1], center[2] );
		R_LightGridSample( p, amb, dl, ldir );
		a += R_Luminance( amb );
		d += R_Luminance( dl );
		VectorMA( sum, R_Luminance( dl ), ldir, sum );
	}
	agree = VectorLength( sum );
	*directed = agree / 5.0f;
	*ambient = ( a + d - agree ) / 5.0f;
	if ( agree > 0.0f )
		VectorScale( sum, 1.0f / agree, dir );
	else
		VectorSet( dir, 0, 0, 1 );

	// the grid is often lit from the side or below: keep the azimuth, raise the elevation
	{
		const float minZ = sinf( DEG2RAD( r_charShadowMinElevation->value ) );
		if ( dir[2] < minZ ) {
			const float h = sqrtf( dir[0] * dir[0] + dir[1] * dir[1] );
			if ( h < 0.001f ) {
				VectorSet( dir, 0, 0, 1 );
			} else {
				const float s = sqrtf( 1.0f - minZ * minZ ) / h;
				VectorSet( dir, dir[0] * s, dir[1] * s, minZ );
			}
		}
	}
}

// smooth a caster's light over time, so its shadow turns and fades instead of jumping between grid cells
static void R_CharShadowSmooth( const void *key, vec3_t dir, float *ambient, float *directed )
{
	const int now = tr.refdef.time;
	charShadowTrack_t *t = NULL, *oldest = &s_charShadowTracks[0];
	int i;

	for ( i = 0; i < CHAR_SHADOW_TRACKS; i++ ) {
		if ( s_charShadowTracks[i].key == key ) {
			t = &s_charShadowTracks[i];
			break;
		}
		if ( s_charShadowTracks[i].time < oldest->time )
			oldest = &s_charShadowTracks[i];
	}

	if ( !t || now < t->time || now - t->time > 1000 ) {
		if ( !t )
			t = oldest;
		t->key = key;
	} else {
		const float k = 1.0f - expf( -(float)( now - t->time ) / CHAR_SHADOW_SMOOTH );
		vec3_t d;
		VectorSubtract( dir, t->dir, d );
		VectorMA( t->dir, k, d, dir );
		VectorNormalize( dir );
		*ambient = t->ambient + k * ( *ambient - t->ambient );
		*directed = t->directed + k * ( *directed - t->directed );
	}
	t->time = now;
	VectorCopy( dir, t->dir );
	t->ambient = *ambient;
	t->directed = *directed;
}

static qboolean R_CharShadowCaster( const trRefEntity_t *ent )
{
	const model_t *model;

	if ( ent->e.reType != RT_MODEL || !ent->e.ghoul2 )
		return qfalse;
	// first-person and depth-hack models never cast; third-person ones are the camera's own body (mirrors only); a
	// stage's figure casts its self-shadow whatever its RF_NOSHADOW (that flag means the world's shadows)
	if ( ent->e.renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK | RF_THIRD_PERSON ) )
		return qfalse;
	if ( ( ent->e.renderfx & RF_NOSHADOW ) && !R_IS_STAGE( tr.refdef.rdflags ) )
		return qfalse;
	model = R_GetModelByHandle( ent->e.hModel );
	if ( model && model->type != MOD_MDXM && model->type != MOD_BAD )
		return qfalse;
	return G2API_HaveWeGhoul2Models( *( (CGhoul2Info_v *)ent->e.ghoul2 ) ) ? qtrue : qfalse;
}

/*
================
R_SetupCharShadows

For the main world view of a scene, before it is rendered: picks the nearest casters and fills tr.refdef's
character shadow matrices, tiles and lights. Returns qfalse (count 0) when this scene has none.
================
*/
qboolean R_SetupCharShadows( qboolean sunCascades )
{
	trRefdef_t	*refdef = &tr.refdef;
	const int	size = vk.sunShadow.size;
	const int	tile = size / 4;
	const float	maxDist = r_charShadowDistance->value;
	float		bestDist[MAX_CHAR_SHADOWS];
	int			best[MAX_CHAR_SHADOWS];
	int			numBest = 0, i, j, n;

	s_numCharShadowViews = 0;
	Com_Memset( refdef->charShadowParams, 0, sizeof( refdef->charShadowParams ) );

	if ( !r_charShadows->integer || !vk.sunShadow.image || size < 256 || !r_drawentities->integer )
		return qfalse;
	if ( sunCascades && r_charShadows->integer == 1 )	// the cascades shadow the models (r_sunShadowModels)
		return qfalse;
	if ( refdef->rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) )
		return qfalse;
	if ( backEndData->numCharShadowCmds )	// one atlas per frame: the first world scene's
		return qfalse;
	if ( !tr.world || !tr.world->lightGridData || !tr.world->lightGridArray )
		return qfalse;

	// the nearest casters whose shadow can be in front of the camera
	for ( i = 0; i < refdef->num_entities; i++ ) {
		const trRefEntity_t *ent = &refdef->entities[i];
		vec3_t	delta;
		float	dist;

		if ( !R_CharShadowCaster( ent ) )
			continue;
		// one tile per Ghoul2 instance: an effect pass of the same model (shell, glow) draws into its owner's tile
		for ( j = 0; j < i; j++ )
			if ( refdef->entities[j].e.ghoul2 == ent->e.ghoul2 && R_CharShadowCaster( &refdef->entities[j] ) )
				break;
		if ( j < i )
			continue;
		VectorSubtract( ent->e.origin, refdef->vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > maxDist || DotProduct( delta, refdef->viewaxis[0] ) < -( CHAR_SHADOW_RADIUS + CHAR_SHADOW_REACH ) )
			continue;
		for ( j = numBest; j > 0 && bestDist[j - 1] > dist; j-- ) {
			if ( j < MAX_CHAR_SHADOWS ) {
				bestDist[j] = bestDist[j - 1];
				best[j] = best[j - 1];
			}
		}
		if ( j < MAX_CHAR_SHADOWS ) {
			bestDist[j] = dist;
			best[j] = i;
			if ( numBest < MAX_CHAR_SHADOWS )
				numBest++;
		}
	}

	for ( i = 0, n = 0; i < numBest; i++ ) {
		const trRefEntity_t	*ent = &refdef->entities[best[i]];
		viewParms_t	*parms = &s_charShadowViews[n];
		vec3_t		dir, axis[3], bounds[2], lightAt;
		float		ambient, directed, radius, scale, fade, reach = CHAR_SHADOW_REACH, drop = 0.0f;

		// r_charShadowFloor: the light where the caster would stand, on the floor below it, so a jumping
		// character's shadow keeps its direction and stays on the ground it will land on
		VectorCopy( ent->e.origin, lightAt );
		if ( r_charShadowFloor->integer ) {
			trace_t	trace;
			vec3_t	end;

			VectorSet( end, ent->e.origin[0], ent->e.origin[1], ent->e.origin[2] - CHAR_SHADOW_FLOOR_DROP );
			ri.CM_BoxTrace( &trace, ent->e.origin, end, NULL, NULL, 0, CONTENTS_SOLID | CONTENTS_TERRAIN, qfalse );
			if ( !trace.startsolid && trace.fraction < 1.0f ) {
				drop = ent->e.origin[2] - trace.endpos[2];
				lightAt[2] = MIN( ent->e.origin[2], trace.endpos[2] + CHAR_SHADOW_STAND );
			}
		}

		R_CharShadowLight( lightAt, dir, &ambient, &directed );
		R_CharShadowSmooth( ent->e.ghoul2, dir, &ambient, &directed );
		if ( directed < 1.0f )	// no light from one side: nothing to block
			continue;

		// the floor along the light lands at depth radius + drop / dir.z; reaching twice that keeps it at half the
		// depth range, short of the fade over its last 40%
		if ( drop > 0.0f )
			reach = Com_Clamp( CHAR_SHADOW_REACH, CHAR_SHADOW_REACH_MAX, 2.0f * drop / MAX( dir[2], 0.1f ) );

		scale = MAX( ent->e.modelScale[0], MAX( ent->e.modelScale[1], ent->e.modelScale[2] ) );
		radius = CHAR_SHADOW_RADIUS * ( scale > 0.0f ? scale : 1.0f );

		// the view looks along the light, from the caster's side
		VectorScale( dir, -1.0f, axis[0] );
		VectorSet( axis[2], 0, 0, 1 );
		if ( fabs( DotProduct( axis[2], axis[0] ) ) > 0.9f )
			VectorSet( axis[2], 0, 1, 0 );
		CrossProduct( axis[2], axis[0], axis[1] );
		VectorNormalize( axis[1] );
		CrossProduct( axis[0], axis[1], axis[2] );
		VectorSet( bounds[0], -radius, -radius, -radius );
		VectorSet( bounds[1], radius + reach, radius, radius );

		Com_Memset( parms, 0, sizeof( *parms ) );
		parms->viewportX = ( n % 4 ) * tile;
		parms->viewportY = ( n / 4 ) * tile;
		parms->viewportWidth = parms->viewportHeight = tile;
		parms->scissorX = parms->viewportX;
		parms->scissorY = parms->viewportY;
		parms->scissorWidth = parms->scissorHeight = tile;
		parms->fovX = parms->fovY = 90;
		parms->portalView = PV_NONE;
		parms->charShadow = n + 1;
		VectorCopy( ent->e.origin, parms->ori.origin );
		VectorCopy( axis[0], parms->ori.axis[0] );
		VectorCopy( axis[1], parms->ori.axis[1] );
		VectorCopy( axis[2], parms->ori.axis[2] );
		VectorCopy( refdef->vieworg, parms->pvsOrigin );

		R_RotateForViewer( &parms->world, parms );
		R_SetupProjectionOrthoZ01( parms, bounds );

		// the matrix the tile is drawn with (get_mvp_transform: projection with the clip y flipped)
		{
			float proj[16];
			Com_Memcpy( proj, parms->projectionMatrix, sizeof( proj ) );
			proj[5] = -proj[5];
			myGlMultMatrix( parms->world.modelViewMatrix, proj, refdef->charShadowMvp[n] );
		}

		// the tile in texture space: get_viewport_rect counts viewportY from the bottom of the layer
		fade = 1.0f - Com_Clamp( 0.0f, 1.0f, ( bestDist[i] - 0.75f * maxDist ) / ( 0.25f * maxDist ) );
		refdef->charShadowTile[n][0] = (float)parms->viewportX / (float)size;
		refdef->charShadowTile[n][1] = (float)( size - parms->viewportY - tile ) / (float)size;
		refdef->charShadowTile[n][2] = (float)tile / (float)size;
		refdef->charShadowTile[n][3] = r_charShadowStrength->value * fade;
		VectorCopy( dir, refdef->charShadowLight[n] );
		refdef->charShadowLight[n][3] = ambient / directed;
		refdef->charShadowScale[n][0] = ( (float)tile / (float)size ) / ( 2.0f * radius );
		refdef->charShadowScale[n][1] = 2.0f * radius + reach;
		refdef->charShadowScale[n][2] = 2.0f / ( 2.0f * radius + reach );	// 2 u of depth bias
		refdef->charShadowScale[n][3] = 0.0f;

		// receivers worth a lookup: the caster's box (a character with arms out, jumping) in the tile's clip x/y,
		// grown by the widest penumbra (the whole depth range x tan( size / 2 ))
		{
			const float	s = scale > 0.0f ? scale : 1.0f;
			const float	*m = refdef->charShadowMvp[n];
			const float	grow = ( 2.0f * radius + reach ) * tanf( DEG2RAD( r_charShadowLightSize->value ) * 0.5f ) / radius;
			float		*rect = refdef->charShadowRect[n];
			int			k;

			rect[0] = rect[1] = 1.0f;
			rect[2] = rect[3] = -1.0f;
			for ( k = 0; k < 8; k++ ) {
				const float x = ent->e.origin[0] + ( ( k & 1 ) ? 32.0f : -32.0f ) * s;
				const float y = ent->e.origin[1] + ( ( k & 2 ) ? 32.0f : -32.0f ) * s;
				const float z = ent->e.origin[2] + ( ( k & 4 ) ? 64.0f : -40.0f ) * s;
				const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
				const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
				rect[0] = MIN( rect[0], cx );
				rect[1] = MIN( rect[1], cy );
				rect[2] = MAX( rect[2], cx );
				rect[3] = MAX( rect[3], cy );
			}
			rect[0] -= grow;
			rect[1] -= grow;
			rect[2] += grow;
			rect[3] += grow;
		}

		if ( r_charShadowDebug->integer == 2 ) {
			static int lastPrint;
			if ( n == 0 && ( tr.refdef.time - lastPrint > 1000 || tr.refdef.time < lastPrint ) )
				lastPrint = tr.refdef.time;
			if ( lastPrint == tr.refdef.time )
				ri.Printf( PRINT_ALL, "CHARSHADOW %d ent %d org %.0f %.0f %.0f dist %.0f dir %.2f %.2f %.2f elev %.0f ambient %.1f directed %.1f strength %.2f floor %.0f reach %.0f\n",
					n, best[i], ent->e.origin[0], ent->e.origin[1], ent->e.origin[2], bestDist[i], dir[0], dir[1], dir[2],
					RAD2DEG( asinf( dir[2] ) ), ambient, directed, refdef->charShadowTile[n][3], drop, reach );
		}

		s_charShadowEnts[n] = best[i];
		n++;
	}

	if ( !n )
		return qfalse;

	s_numCharShadowViews = n;
	refdef->charShadowParams[0] = (float)n;
	refdef->charShadowParams[1] = r_charShadowSoftness->value / (float)size;
	refdef->charShadowParams[2] = 0.0f;	// unused: each caster's depth bias is charShadowScale[n][2]
	refdef->charShadowInfo[1] = tanf( DEG2RAD( r_charShadowLightSize->value ) * 0.5f );
	refdef->charShadowInfo[2] = 1.0f / (float)size;
	refdef->charShadowParams[3] = (float)r_charShadowDebug->integer;
	refdef->charShadowInfo[0] = tr.identityLight;	// 1 / ( 1 << overbrightBits ), the gamma pass's scale
	return qtrue;
}

/*
================
R_SetupCharSelfShadows

r_charSelfShadow, for the main world view of a scene after R_SetupCharShadows: the nearest characters each get a
tile in the atlas layer's lower half (the nearest half the layer's width, the next four a quarter) holding a depth view
of that character alone along its own light-grid light (refEntity lightDir, the light its lit shader uses), drawn
by R_RenderCharShadows with the character shadow tiles. Alpha-tested stages (hair, lashes) cast by their alpha test.
The lit shader (gen_frag charSelfShadow) takes the blocked share off the grid's directed light.
================
*/
// RDF_STAGE's self-shadow: the reference play settings, the ones tuned against the Cycles renders,
// at the most rays and steps the lookup takes (a menu shows one figure; play takes 8 and 9)
#define STAGE_SELF_SHADOW_RAYS		16
#define STAGE_SELF_SHADOW_STEPS		12
#define STAGE_SELF_SHADOW_BIAS		0.04f
#define STAGE_SELF_SHADOW_OFFSET	0.01f

qboolean R_SetupCharSelfShadows( void )
{
	trRefdef_t	*refdef = &tr.refdef;
	const int	size = vk.sunShadow.size;
	const float	maxDist = r_charSelfShadowDistance->value;
	float		bestDist[MAX_CHAR_SELF_SHADOWS];
	int			best[MAX_CHAR_SELF_SHADOWS];
	int			numBest = 0, i, j, n;
	const int	first = s_numCharShadowViews;

	// RDF_STAGE (a menu's character stage): on whatever r_charSelfShadow says, at full strength, softened like the
	// rig's key disk; the figure takes the first tile even when this frame's world scene has the atlas. The tiles are
	// drawn in command order in one pass and a tile over an earlier one is cleared first (RB_RenderSunShadowCascades),
	// so the stage's (later) draw wins; the world then reads the stage's tile for
	// its nearest character, but a stage's screen covers the world (the player screen's backdrop).
	const qboolean stage = R_IS_STAGE( refdef->rdflags ) ? qtrue : qfalse;

	Com_Memset( refdef->selfShadowParams, 0, sizeof( refdef->selfShadowParams ) );

	if ( ( r_charSelfShadow->value <= 0.0f && !stage ) || !vk.sunShadow.image || size < 256 || !r_drawentities->integer )
		return qfalse;
	if ( !stage ) {
		if ( refdef->rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) )
			return qfalse;
		if ( backEndData->numCharShadowCmds )	// one atlas per frame: the first world scene's
			return qfalse;
		if ( !tr.world || !tr.world->lightGridData )
			return qfalse;
	}

	for ( i = 0; i < refdef->num_entities; i++ ) {
		const trRefEntity_t *ent = &refdef->entities[i];
		vec3_t	delta;
		float	dist;

		if ( !R_CharShadowCaster( ent ) )
			continue;
		for ( j = 0; j < i; j++ )
			if ( refdef->entities[j].e.ghoul2 == ent->e.ghoul2 && R_CharShadowCaster( &refdef->entities[j] ) )
				break;
		if ( j < i )
			continue;
		VectorSubtract( ent->e.origin, refdef->vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > maxDist || DotProduct( delta, refdef->viewaxis[0] ) < -CHAR_SHADOW_RADIUS )
			continue;
		for ( j = numBest; j > 0 && bestDist[j - 1] > dist; j-- ) {
			if ( j < MAX_CHAR_SELF_SHADOWS ) {
				bestDist[j] = bestDist[j - 1];
				best[j] = best[j - 1];
			}
		}
		if ( j < MAX_CHAR_SELF_SHADOWS ) {
			bestDist[j] = dist;
			best[j] = i;
			if ( numBest < MAX_CHAR_SELF_SHADOWS )
				numBest++;
		}
	}

	for ( i = 0, n = 0; i < numBest; i++ ) {
		trRefEntity_t	*ent = &refdef->entities[best[i]];
		viewParms_t		*parms = &s_charShadowViews[first + n];
		vec3_t			dir, axis[3], bounds[2];
		float			radius, scale, fade;
		int				tile, vx, vy, k;

		// the light its lit shader uses (computed here before the main view; idempotent per frame)
		R_SetupEntityLighting( refdef, ent );
		if ( VectorLength( ent->directedLight ) < 1.0f )
			continue;
		VectorCopy( ent->lightDir, dir );
		if ( VectorNormalize( dir ) < 0.001f )
			continue;

		if ( n == 0 ) {
			tile = size / 2;
			vx = 0;
			vy = size / 2;
		} else {
			tile = size / 4;
			vx = size / 2 + ( ( n - 1 ) % 2 ) * tile;
			vy = size / 2 + ( ( n - 1 ) / 2 ) * tile;
		}

		scale = MAX( ent->e.modelScale[0], MAX( ent->e.modelScale[1], ent->e.modelScale[2] ) );
		radius = CHAR_SHADOW_RADIUS * ( scale > 0.0f ? scale : 1.0f );

		VectorScale( dir, -1.0f, axis[0] );
		VectorSet( axis[2], 0, 0, 1 );
		if ( fabs( DotProduct( axis[2], axis[0] ) ) > 0.9f )
			VectorSet( axis[2], 0, 1, 0 );
		CrossProduct( axis[2], axis[0], axis[1] );
		VectorNormalize( axis[1] );
		CrossProduct( axis[0], axis[1], axis[2] );
		VectorSet( bounds[0], -radius, -radius, -radius );
		VectorSet( bounds[1], radius, radius, radius );

		Com_Memset( parms, 0, sizeof( *parms ) );
		parms->viewportX = vx;
		parms->viewportY = vy;
		parms->viewportWidth = parms->viewportHeight = tile;
		parms->scissorX = parms->viewportX;
		parms->scissorY = parms->viewportY;
		parms->scissorWidth = parms->scissorHeight = tile;
		parms->fovX = parms->fovY = 90;
		parms->portalView = PV_NONE;
		parms->charShadow = first + n + 1;
		parms->selfShadowSlot = n + 1;
		VectorCopy( ent->e.origin, parms->ori.origin );
		VectorCopy( axis[0], parms->ori.axis[0] );
		VectorCopy( axis[1], parms->ori.axis[1] );
		VectorCopy( axis[2], parms->ori.axis[2] );
		VectorCopy( refdef->vieworg, parms->pvsOrigin );

		R_RotateForViewer( &parms->world, parms );
		R_SetupProjectionOrthoZ01( parms, bounds );

		{
			float proj[16];
			Com_Memcpy( proj, parms->projectionMatrix, sizeof( proj ) );
			proj[5] = -proj[5];
			myGlMultMatrix( parms->world.modelViewMatrix, proj, refdef->selfShadowMvp[n] );
		}

		fade = 1.0f - Com_Clamp( 0.0f, 1.0f, ( bestDist[i] - 0.75f * maxDist ) / ( 0.25f * maxDist ) );
		refdef->selfShadowTile[n][0] = (float)vx / (float)size;
		refdef->selfShadowTile[n][1] = (float)( size - vy - tile ) / (float)size;
		refdef->selfShadowTile[n][2] = (float)tile / (float)size;
		refdef->selfShadowTile[n][3] = stage ? 1.0f : r_charSelfShadow->value * fade;
		refdef->selfShadowScale[n][0] = ( (float)tile / (float)size ) / ( 2.0f * radius );
		refdef->selfShadowScale[n][1] = 2.0f * radius;
		refdef->selfShadowScale[n][2] = ( stage ? STAGE_SELF_SHADOW_BIAS : r_charSelfShadowBias->value ) / ( 2.0f * radius );
		refdef->selfShadowScale[n][3] = stage ? STAGE_SELF_SHADOW_OFFSET : r_charSelfShadowOffset->value;

		// every entity drawing this Ghoul2 instance (effect passes too) reads the tile
		for ( k = 0; k < refdef->num_entities; k++ )
			if ( refdef->entities[k].e.ghoul2 == ent->e.ghoul2 )
				refdef->entities[k].selfShadow = n + 1;

		if ( r_charSelfShadowDebug->integer == 2 ) {
			static int lastPrint;
			if ( n == 0 && ( tr.refdef.time - lastPrint > 1000 || tr.refdef.time < lastPrint ) )
				lastPrint = tr.refdef.time;
			if ( lastPrint == tr.refdef.time )
				ri.Printf( PRINT_ALL, "SELFSHADOW %d ent %d org %.0f %.0f %.0f dist %.0f dir %.3f %.3f %.3f tile %d px at %d %d strength %.2f\n",
					n, best[i], ent->e.origin[0], ent->e.origin[1], ent->e.origin[2], bestDist[i], dir[0], dir[1], dir[2],
					tile, vx, vy, refdef->selfShadowTile[n][3] );
		}

		s_charShadowEnts[first + n] = best[i];
		n++;
	}

	if ( !n )
		return qfalse;

	s_numCharShadowViews = first + n;
	refdef->selfShadowParams[0] = stage ? (float)( STAGE_SELF_SHADOW_RAYS + 32 * STAGE_SELF_SHADOW_STEPS )
		: (float)( Com_Clamp( 1, 16, r_charSelfShadowRays->integer ) + 32 * Com_Clamp( 2, 12, r_charSelfShadowSteps->integer ) );
	refdef->selfShadowParams[1] = r_charSelfShadowSoftness->value / (float)size;
	refdef->selfShadowTexel = 1.0f / (float)size;
	refdef->selfShadowParams[2] = tanf( DEG2RAD( r_charSelfShadowLightSize->value ) * 0.5f );
	if ( stage ) {
		vec3_t	toCam, dir, color;
		float	sizeDeg;
		VectorSubtract( refdef->vieworg, refdef->entities[best[0]].e.origin, toCam );
		if ( R_StageKeyLight( toCam, dir, color, &sizeDeg ) )
			refdef->selfShadowParams[2] = tanf( DEG2RAD( sizeDeg ) * 0.5f );
	}
	refdef->selfShadowParams[3] = (float)r_charSelfShadowDebug->integer;
	return qtrue;
}

int R_G2AppendSurfaceObj( const CRenderableSurface *surf, const trRefEntity_t *ent, int vertBase, std::string &out );

/*
================
R_CharShadowDump_f

charshadowdump: at the next frame with character shadows, write the casters as drawn into their tiles (skinned,
world space) to charshadow/dump.obj and the camera and each caster's light to charshadow/dump.txt in the home
directory. Reference renders (tools/charshadow_reference.py) rebuild the scene from them.
================
*/
void R_CharShadowDump_f( void )
{
	s_charShadowDumpPending = qtrue;
	ri.Printf( PRINT_ALL, "charshadowdump: writing at the next frame with character shadows\n" );
}

/*
================
R_RenderCharShadows

After the main view: each tile's draw surfaces, its caster alone (every entity drawing that Ghoul2 instance; only
their opaque surfaces reach a shadow view).
================
*/
void R_RenderCharShadows( void )
{
	const trRefdef_t *refdef = &tr.refdef;
	const qboolean dump = s_charShadowDumpPending;
	std::string obj, txt;
	int i, vertBase = 0;

	if ( dump ) {
		txt += va( "camera %.3f %.3f %.3f | %.6f %.6f %.6f | %.6f %.6f %.6f | %.6f %.6f %.6f | fov %.4f %.4f | size %d %d\n",
			refdef->vieworg[0], refdef->vieworg[1], refdef->vieworg[2],
			refdef->viewaxis[0][0], refdef->viewaxis[0][1], refdef->viewaxis[0][2],
			refdef->viewaxis[1][0], refdef->viewaxis[1][1], refdef->viewaxis[1][2],
			refdef->viewaxis[2][0], refdef->viewaxis[2][1], refdef->viewaxis[2][2],
			refdef->fov_x, refdef->fov_y, refdef->width, refdef->height );
		txt += va( "params minElevation %.2f strength %.3f softness %.3f lightSize %.2f mapSize %d radius %.1f reach %.1f\n",
			r_charShadowMinElevation->value, r_charShadowStrength->value, r_charShadowSoftness->value, r_charShadowLightSize->value,
			vk.sunShadow.size, CHAR_SHADOW_RADIUS, CHAR_SHADOW_REACH );
	}

	for ( i = 0; i < s_numCharShadowViews; i++ )
	{
		const int firstDrawSurf = tr.refdef.numDrawSurfs;
		const void *ghoul2 = tr.refdef.entities[s_charShadowEnts[i]].e.ghoul2;
		int numDrawSurfs, entityNum;

		tr.viewCount++;
		tr.viewParms = s_charShadowViews[i];
		tr.viewParms.frameSceneNum = tr.frameSceneNum;
		tr.viewParms.frameCount = tr.frameCount;
		tr.ori = tr.viewParms.world;

		for ( entityNum = s_charShadowEnts[i]; entityNum < tr.refdef.num_entities; entityNum++ ) {
			trRefEntity_t *ent = &tr.refdef.entities[entityNum];
			if ( ent->e.ghoul2 == ghoul2 && R_CharShadowCaster( ent ) )
				R_AddEntitySurface( &tr.refdef, ent, entityNum );
		}

		numDrawSurfs = tr.refdef.numDrawSurfs;
		if ( numDrawSurfs > MAX_DRAWSURFS )
			numDrawSurfs = MAX_DRAWSURFS;
		R_SortDrawSurfs( tr.refdef.drawSurfs + firstDrawSurf, numDrawSurfs - firstDrawSurf );

		if ( dump && i < MAX_CHAR_SHADOWS ) {	// self-shadow tiles (R_SetupCharSelfShadows) are not dumped
			const trRefEntity_t *ent = &tr.refdef.entities[s_charShadowEnts[i]];
			int j;
			txt += va( "caster %d entity %d origin %.3f %.3f %.3f light %.6f %.6f %.6f ratio %.6f strength %.4f\n", i,
				s_charShadowEnts[i], ent->e.origin[0], ent->e.origin[1], ent->e.origin[2],
				refdef->charShadowLight[i][0], refdef->charShadowLight[i][1], refdef->charShadowLight[i][2],
				refdef->charShadowLight[i][3], refdef->charShadowTile[i][3] );
			obj += va( "o caster%d\n", i );
			for ( j = firstDrawSurf; j < numDrawSurfs; j++ ) {
				const drawSurf_t *ds = &tr.refdef.drawSurfs[j];
				shader_t *shader;
				int entityNum, cubemap;
				if ( *ds->surface != SF_MDX )
					continue;
				R_DecomposeSort( ds->sort, &entityNum, &shader, &cubemap );
				vertBase += R_G2AppendSurfaceObj( (const CRenderableSurface *)ds->surface, &tr.refdef.entities[entityNum], vertBase, obj );
			}
		}
	}
	s_numCharShadowViews = 0;

	if ( dump ) {
		s_charShadowDumpPending = qfalse;
		ri.FS_WriteFile( "charshadow/dump.obj", obj.c_str(), (int)obj.size() );
		ri.FS_WriteFile( "charshadow/dump.txt", txt.c_str(), (int)txt.size() );
		ri.Printf( PRINT_ALL, "charshadowdump: %d casters, %d vertices: charshadow/dump.obj, charshadow/dump.txt\n", i, vertBase );
	}
}
