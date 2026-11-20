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

// cg_miscg2.c -- misc_model_ghoul2: a map's client-only animated Ghoul2 models (spawn, draw, cleanup and the nearest
// one for the pit death). The instances live in cgs.miscG2Models (cg_g2model_t, cg_local.h).

#include "cg_local.h"
#include "ghoul2/G2.h"

qboolean CG_CullPointAndRadius( const vec3_t pt, float radius );

/*QUAKED misc_model_ghoul2 (1 0 0) (-16 -16 -24) (16 16 40)
A client-only animated Ghoul2 model: map decoration with no collision and no server entity.
"model"			the .glm
"origin", "angle" or "angles"
"modelscale"	uniform scale (default 1)
"startframe", "endframe"	the looping frame range of the model's GLA (default 0 1)
"animspeed"		playback speed (default 1)
"shader"		a shader drawn over every surface in place of the skin (a hologram)
"track"		"1": the head and neck turn to the nearest player (to someone in the pit hand first)
"light"		"r g b intensity": two stage lights on the figure, in front and above (for one standing where
				the map's own light does not reach)
"skin"			a .skin file (when no shader is given)
*/
void SP_misc_model_ghoul2( void ) {
	char			*model, *shader, *skin;
	float			angle, speed;
	int				first, last;
	cg_g2model_t	*g;

	if ( cgs.numMiscG2Models >= MAX_MISC_G2_MODELS ) {
		trap->Print( S_COLOR_YELLOW "WARNING: MAX_MISC_G2_MODELS(%i) hit, misc_model_ghoul2 left out\n", MAX_MISC_G2_MODELS );
		return;
	}
	CG_SpawnString( "model", "", &model );
	if ( !model || !model[0] ) {
		trap->Print( S_COLOR_YELLOW "WARNING: misc_model_ghoul2 with no model\n" );
		return;
	}
	g = &cgs.miscG2Models[cgs.numMiscG2Models];
	memset( g, 0, sizeof( *g ) );
	CG_SpawnVector( "origin", "0 0 0", g->org );
	if ( !CG_SpawnVector( "angles", "0 0 0", g->angles ) ) {
		if ( CG_SpawnFloat( "angle", "0", &angle ) ) {
			g->angles[YAW] = angle;
		}
	}
	CG_SpawnFloat( "modelscale", "1", &g->scale );
	CG_SpawnInt( "startframe", "0", &first );
	CG_SpawnInt( "endframe", "1", &last );
	CG_SpawnFloat( "animspeed", "1", &speed );
	CG_SpawnString( "shader", "", &shader );
	CG_SpawnString( "skin", "", &skin );
	g->shader = ( shader && shader[0] ) ? trap->R_RegisterShader( shader ) : 0;

	g->skin = ( skin && skin[0] ) ? trap->R_RegisterSkin( skin ) : 0;
	trap->G2API_InitGhoul2Model( &g->ghoul2, model, 0, g->skin, 0, 0, 0 );
	if ( !g->ghoul2 ) {
		trap->Print( S_COLOR_YELLOW "WARNING: misc_model_ghoul2 failed to load '%s'\n", model );
		return;
	}
	if ( g->skin ) {
		trap->G2API_SetSkin( g->ghoul2, 0, g->skin, g->skin );
	}
	g->speed = speed > 0.0f ? speed : 1.0f;
	{
		int track;

		CG_SpawnInt( "track", "0", &track );
		g->track = track ? qtrue : qfalse;
	}
	{
		char *light;

		CG_SpawnString( "light", "", &light );
		if ( light[0] ) {
			sscanf( light, "%f %f %f %f", &g->light[0], &g->light[1], &g->light[2], &g->light[3] );
		}
	}
	{
		// "frames" "first last first last ...": several clips played in turn (cg_draw.c starts each); otherwise
		// the one startframe/endframe range, looping
		// (a playlist: up to MAX_MISC_G2_CLIPS pairs; a spawn value holds about 1,000 characters)
		char	*frames, *end;

		CG_SpawnString( "frames", "", &frames );
		g->numClips = 0;
		while ( frames && *frames && g->numClips < MAX_MISC_G2_CLIPS ) {
			long first2 = strtol( frames, &end, 10 ), last2;

			if ( end == frames ) {
				break;
			}
			frames = end;
			last2 = strtol( frames, &end, 10 );
			if ( end == frames ) {
				break;
			}
			frames = end;
			g->clipFirst[g->numClips] = (int)first2;
			g->clipLast[g->numClips] = (int)last2;
			g->numClips++;
		}
	}
	if ( g->numClips < 2 ) {
		if ( g->numClips == 1 ) {
			first = g->clipFirst[0];
			last = g->clipLast[0];
			g->numClips = 0;
		}
		trap->G2API_SetBoneAnim( g->ghoul2, 0, "model_root", first, last, BONE_ANIM_OVERRIDE_LOOP, g->speed, cg.time, -1, 0 );
	}
	g->radius = 72.0f * g->scale;		// a humanoid's reach from its origin
	g->renderfx = RF_NOSHADOW;
	cgs.numMiscG2Models++;
}

// every frame, from CG_DrawActive: the map's client-only animated models
void CG_DrawMiscG2Models( void ) {
	int			i, j;
	refEntity_t	ent;

	for ( i = 0; i < cgs.numMiscG2Models; i++ ) {
		cg_g2model_t *g = &cgs.miscG2Models[i];
		vec3_t mid;
		qboolean culled;

		if ( !g->ghoul2 ) {
			continue;
		}
		VectorCopy( g->org, mid );
		mid[2] += 10.0f * g->scale;
		culled = CG_CullPointAndRadius( mid, g->radius ) ? qtrue : qfalse;
		if ( g->numClips > 1 && cg.time >= g->clipEnd ) {
			// the next clip, once through, blended in over 0.6 s; the animation library runs at 20 frames a second
			g->clip = g->clipEnd ? ( g->clip + 1 ) % g->numClips : 0;
			trap->G2API_SetBoneAnim( g->ghoul2, 0, "model_root", g->clipFirst[g->clip], g->clipLast[g->clip],
				BONE_ANIM_OVERRIDE_FREEZE | BONE_ANIM_BLEND, g->speed, cg.time, -1, g->clipEnd ? 600 : 0 );
			g->clipEnd = cg.time + (int)( ( g->clipLast[g->clip] - g->clipFirst[g->clip] ) * 50.0f / g->speed );
		}
		if ( g->track && cg.snap ) {
			// look at the nearest player (someone in the pit hand first): the neck takes 40%, the head the rest,
			// over the playing animation
			vec3_t	head, at, d, a;
			float	best = -1.0f, wantYaw = 0.0f, wantPitch = 0.0f, k;
			int		n;

			VectorCopy( g->org, head );
			head[2] += 36.0f * g->scale;
			if ( CG_PitDeath_Focus( at ) ) {
				best = 0.0f;
			} else {
				for ( n = 0; n < MAX_CLIENTS; n++ ) {
					const centity_t *o = &cg_entities[n];
					vec3_t p;
					float dist;

					if ( n == cg.snap->ps.clientNum ) {
						VectorCopy( cg.predictedPlayerState.origin, p );
					} else if ( o->currentValid && o->currentState.eType == ET_PLAYER && !( o->currentState.eFlags & EF_DEAD ) ) {
						VectorCopy( o->lerpOrigin, p );
					} else {
						continue;
					}
					dist = Distance( p, head );
					if ( best < 0.0f || dist < best ) {
						best = dist;
						VectorCopy( p, at );
					}
				}
			}
			if ( best >= 0.0f ) {
				VectorSubtract( at, head, d );
				vectoangles( d, a );
				wantYaw = AngleSubtract( a[YAW], g->angles[YAW] );
				wantPitch = AngleNormalize180( a[PITCH] );
				if ( wantYaw > 65.0f ) wantYaw = 65.0f;
				if ( wantYaw < -65.0f ) wantYaw = -65.0f;
				if ( wantPitch > 25.0f ) wantPitch = 25.0f;
				if ( wantPitch < -55.0f ) wantPitch = -55.0f;
			}
			k = cg.frametime * 0.0025f;		// a slow, heavy turn
			if ( k > 1.0f ) k = 1.0f;
			g->lookYaw += ( wantYaw - g->lookYaw ) * k;
			g->lookPitch += ( wantPitch - g->lookPitch ) * k;
		}
		if ( culled ) {
			continue;	// the clip clock and the look's smoothing above keep running; bones are set only when drawn
		}
		if ( g->track && cg.snap ) {
			vec3_t a;

			VectorSet( a, g->lookPitch * 0.4f, g->lookYaw * 0.4f, 0.0f );
			trap->G2API_SetBoneAngles( g->ghoul2, 0, "cervical", a, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z, NULL, 0, cg.time );
			VectorSet( a, g->lookPitch * 0.6f, g->lookYaw * 0.6f, 0.0f );
			trap->G2API_SetBoneAngles( g->ghoul2, 0, "cranium", a, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z, NULL, 0, cg.time );
		}
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.ghoul2 = g->ghoul2;
		ent.renderfx = g->renderfx;
		ent.customShader = g->shader;
		ent.customSkin = g->skin;
		ent.radius = g->radius;
		ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
		VectorCopy( g->org, ent.origin );
		VectorCopy( g->org, ent.oldorigin );
		VectorCopy( g->org, ent.lightingOrigin );
		VectorCopy( g->angles, ent.angles );
		AnglesToAxis( g->angles, ent.axis );
		VectorSet( ent.modelScale, g->scale, g->scale, g->scale );
		if ( g->scale != 1.0f ) {
			for ( j = 0; j < 3; j++ ) {
				VectorScale( ent.axis[j], g->scale, ent.axis[j] );
			}
			ent.nonNormalizedAxes = qtrue;
		}
		trap->R_AddRefEntityToScene( &ent );
		if ( g->light[3] > 0.0f ) {
			vec3_t fwd, at;

			AngleVectors( g->angles, fwd, NULL, NULL );
			VectorMA( g->org, 30.0f * g->scale, fwd, at );
			at[2] += 20.0f * g->scale;
			trap->R_AddLightToScene( at, g->light[3], g->light[0], g->light[1], g->light[2] );
			VectorMA( g->org, -18.0f * g->scale, fwd, at );
			at[2] += 55.0f * g->scale;
			trap->R_AddLightToScene( at, g->light[3], g->light[0], g->light[1], g->light[2] );
		}
	}
}

// from CG_DestroyAllGhoul2 (cg_main.c): free the instances
void CG_MiscG2_Shutdown( void ) {
	int i;

	for ( i = 0; i < cgs.numMiscG2Models; i++ ) {
		if ( cgs.miscG2Models[i].ghoul2 && trap->G2_HaveWeGhoul2Models( cgs.miscG2Models[i].ghoul2 ) ) {
			trap->G2API_CleanGhoul2Models( &cgs.miscG2Models[i].ghoul2 );
		}
		cgs.miscG2Models[i].ghoul2 = NULL;
	}
	cgs.numMiscG2Models = 0;
}

// the model of at least minScale nearest `at` across the ground (height left out), NULL when there is none
const cg_g2model_t *CG_MiscG2_Nearest( const vec3_t at, float minScale ) {
	int		i, best = -1;
	float	bestDist = 0.0f;
	vec3_t	d;

	for ( i = 0; i < cgs.numMiscG2Models; i++ ) {
		const cg_g2model_t *g = &cgs.miscG2Models[i];
		float dist;

		if ( g->scale < minScale ) {
			continue;
		}
		VectorSubtract( at, g->org, d );
		d[2] = 0.0f;
		dist = VectorLength( d );
		if ( best < 0 || dist < bestDist ) {
			best = i;
			bestDist = dist;
		}
	}
	return best >= 0 ? &cgs.miscG2Models[best] : NULL;
}
