/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// cg_marks.c -- wall marks

#include "cg_local.h"

/*
===================================================================

MARK POLYS

===================================================================
*/


markPoly_t	cg_activeMarkPolys;			// double linked list
markPoly_t	*cg_freeMarkPolys;			// single linked list
markPoly_t	cg_markPolys[MAX_MARK_POLYS];
static		int	markTotal;

/*
===================
CG_InitMarkPolys

This is called at startup and for tournament restarts
===================
*/
void	CG_InitMarkPolys( void ) {
	int		i;

	memset( cg_markPolys, 0, sizeof(cg_markPolys) );

	cg_activeMarkPolys.nextMark = &cg_activeMarkPolys;
	cg_activeMarkPolys.prevMark = &cg_activeMarkPolys;
	cg_freeMarkPolys = cg_markPolys;
	for ( i = 0 ; i < MAX_MARK_POLYS - 1 ; i++ ) {
		cg_markPolys[i].nextMark = &cg_markPolys[i+1];
	}
}


/*
==================
CG_FreeMarkPoly
==================
*/
void CG_FreeMarkPoly( markPoly_t *le ) {
	if ( !le->prevMark ) {
		trap->Error( ERR_DROP, "CG_FreeLocalEntity: not active" );
	}

	// remove from the doubly linked active list
	le->prevMark->nextMark = le->nextMark;
	le->nextMark->prevMark = le->prevMark;

	// the free list is only singly linked
	le->nextMark = cg_freeMarkPolys;
	cg_freeMarkPolys = le;
}

/*
===================
CG_AllocMark

Will allways succeed, even if it requires freeing an old active mark
===================
*/
markPoly_t	*CG_AllocMark( void ) {
	markPoly_t	*le;
	int time;

	if ( !cg_freeMarkPolys ) {
		// no free entities, so free the one at the end of the chain
		// remove the oldest active entity
		time = cg_activeMarkPolys.prevMark->time;
		while (cg_activeMarkPolys.prevMark && time == cg_activeMarkPolys.prevMark->time) {
			CG_FreeMarkPoly( cg_activeMarkPolys.prevMark );
		}
	}

	le = cg_freeMarkPolys;
	cg_freeMarkPolys = cg_freeMarkPolys->nextMark;

	memset( le, 0, sizeof( *le ) );

	// link into the active list
	le->nextMark = cg_activeMarkPolys.nextMark;
	le->prevMark = &cg_activeMarkPolys;
	cg_activeMarkPolys.nextMark->prevMark = le;
	cg_activeMarkPolys.nextMark = le;
	return le;
}



/*
=================
CG_ImpactMark

origin should be a point within a unit of the plane
dir should be the plane normal

temporary marks will not be stored or randomly oriented, but immediately
passed to the renderer.
=================
*/
#define	MAX_MARK_FRAGMENTS	128
#define	MAX_MARK_POINTS		384

/*
=================
CG_ProjectMark

Shared by CG_ImpactMark and CG_Gore_ImpactMarkAxis. axis[0] is the plane
normal; axis[1] and axis[2] are the texture's s and t directions, spanning
halfS and halfT units either side of origin.
=================
*/
static int CG_ProjectMark( qhandle_t markShader, const vec3_t origin, matrix3_t axis, float halfS, float halfT,
				   float red, float green, float blue, float alpha,
				   qboolean alphaFade, qboolean temporary, int lifeTime, qboolean blood,
				   qhandle_t wetShader, const vec3_t lightDir ) {
	vec3_t			originalPoints[4];
	byte			colors[4];
	int				i, j, k;
	int				numFragments;
	markFragment_t	markFragments[MAX_MARK_FRAGMENTS], *mf;
	vec3_t			markPoints[MAX_MARK_POINTS];
	vec3_t			projection;
	float			sScale = 0.5f / halfS, tScale = 0.5f / halfT;

	// create the full polygon
	for ( i = 0 ; i < 3 ; i++ ) {
		originalPoints[0][i] = origin[i] - halfS * axis[1][i] - halfT * axis[2][i];
		originalPoints[1][i] = origin[i] + halfS * axis[1][i] - halfT * axis[2][i];
		originalPoints[2][i] = origin[i] + halfS * axis[1][i] + halfT * axis[2][i];
		originalPoints[3][i] = origin[i] - halfS * axis[1][i] + halfT * axis[2][i];
	}

	// get the fragments
	VectorScale( axis[0], -20, projection );
	numFragments = trap->R_MarkFragments( 4, (const vec3_t *) originalPoints, projection, MAX_MARK_POINTS, markPoints[0], MAX_MARK_FRAGMENTS, markFragments );

	colors[0] = red * 255;
	colors[1] = green * 255;
	colors[2] = blue * 255;
	colors[3] = alpha * 255;

	for ( i = 0, mf = markFragments ; i < numFragments ; i++, mf++ ) {
		polyVert_t	*v;
		polyVert_t	verts[MAX_VERTS_ON_POLY];
		markPoly_t	*mark;

		// we have an upper limit on the complexity of polygons
		// that we store persistantly
		if ( mf->numPoints > MAX_VERTS_ON_POLY ) {
			mf->numPoints = MAX_VERTS_ON_POLY;
		}
		for ( j = 0, v = verts ; j < mf->numPoints ; j++, v++ ) {
			vec3_t		delta;

			VectorCopy( markPoints[mf->firstPoint + j], v->xyz );

			VectorSubtract( v->xyz, origin, delta );
			v->st[0] = 0.5 + DotProduct( delta, axis[1] ) * sScale;
			v->st[1] = 0.5 + DotProduct( delta, axis[2] ) * tScale;
			for ( k=0; k<4; k++ )
				v->modulate[k] = colors[k];
		}

		// if it is a temporary (shadow) mark, add it immediately and forget about it
		if ( temporary ) {
			trap->R_AddPolysToScene( markShader, mf->numPoints, verts, 1 );
			continue;
		}

		// otherwise save it persistantly
		mark = CG_AllocMark();
		mark->time = cg.time;
		mark->alphaFade = alphaFade;
		mark->lifeTime = lifeTime;
		mark->blood = blood;
		mark->wetShader = wetShader;
		if ( lightDir ) {
			VectorCopy( lightDir, mark->lightDir );
		}
		VectorCopy( axis[0], mark->normal );
		mark->markShader = markShader;
		mark->poly.numVerts = mf->numPoints;
		mark->color[0] = red;
		mark->color[1] = green;
		mark->color[2] = blue;
		mark->color[3] = alpha;
		memcpy( mark->verts, verts, mf->numPoints * sizeof( verts[0] ) );
		markTotal++;
	}
	return numFragments;
}

/*
=================
CG_ImpactMark

origin should be a point within a unit of the plane
dir should be the plane normal

temporary marks will not be stored or randomly oriented, but immediately
passed to the renderer.
=================
*/
void CG_ImpactMark( qhandle_t markShader, const vec3_t origin, const vec3_t dir,
				   float orientation, float red, float green, float blue, float alpha,
				   qboolean alphaFade, float radius, qboolean temporary ) {
	matrix3_t		axis;

	assert(markShader);

	if ( !cg_marks.integer ) {
		return;
	}
	else if (cg_marks.integer == 2)
	{
		trap->R_AddDecalToScene(markShader, origin, dir, orientation, red, green, blue, alpha,
			alphaFade, radius, temporary);
		return;
	}

	if ( radius <= 0 ) {
		trap->Error( ERR_DROP, "CG_ImpactMark called with <= 0 radius" );
	}

	// create the texture axis
	VectorNormalize2( dir, axis[0] );
	PerpendicularVector( axis[1], axis[0] );
	RotatePointAroundVector( axis[2], axis[0], axis[1], orientation );
	CrossProduct( axis[0], axis[2], axis[1] );

	CG_ProjectMark( markShader, origin, axis, radius, radius, red, green, blue, alpha,
		alphaFade, temporary, 0, qfalse, 0, NULL );
}

/*
=================
CG_Gore_ImpactMarkAxis

A blood mark (cg_gore.c). down, when given, is a direction in the plane the
texture's t axis follows (texture top at -down); otherwise the mark is
randomly rotated. The mark keeps the light at its spot in color[] and its
opacity in color[3]; CG_AddMarks multiplies in the blood's drying albedo.
=================
*/
void CG_Gore_ImpactMarkAxis( qhandle_t markShader, qhandle_t wetShader, const vec3_t origin, const vec3_t normal,
				const vec3_t down, float halfWidth, float halfHeight, int lifeTime ) {
	matrix3_t	axis;
	vec3_t		color, lightDir;

	if ( !cg_marks.integer || !markShader || halfWidth <= 0 || halfHeight <= 0 ) {
		return;
	}

	VectorNormalize2( normal, axis[0] );
	if ( down ) {
		VectorCopy( down, axis[2] );
	} else {
		PerpendicularVector( axis[1], axis[0] );
		RotatePointAroundVector( axis[2], axis[0], axis[1], flrand( 0.0f, 360.0f ) );
	}
	CrossProduct( axis[0], axis[2], axis[1] );	// the stock winding; reversed, R_MarkFragments clips everything

	CG_Gore_Light( origin, axis[0], color, lightDir );
	if ( !CG_ProjectMark( markShader, origin, axis, halfWidth, halfHeight, color[0], color[1], color[2], 0.95f,
		qtrue, qfalse, lifeTime, qtrue, wetShader, lightDir ) && cg_goreDebug.integer ) {
		Com_Printf( "gore: mark at (%.0f %.0f %.0f) found no surface\n", origin[0], origin[1], origin[2] );
	}
}

/*
===============
CG_AddMarks
===============
*/
#define	MARK_TOTAL_TIME		10000
#define	MARK_FADE_TIME		1000

void CG_AddMarks( void ) {
	int			j;
	markPoly_t	*mp, *next;
	int			t;
	int			fade;
	int			life;

	if ( !cg_marks.integer ) {
		return;
	}

	mp = cg_activeMarkPolys.nextMark;
	for ( ; mp != &cg_activeMarkPolys ; mp = next ) {
		// grab next now, so if the local entity is freed we
		// still have it
		next = mp->nextMark;

		life = mp->lifeTime ? mp->lifeTime : MARK_TOTAL_TIME;

		// see if it is time to completely remove it
		if ( cg.time > mp->time + life ) {
			CG_FreeMarkPoly( mp );
			continue;
		}

		// fade out the energy bursts
		//if ( mp->markShader == cgs.media.energyMarkShader ) {
		if (0) {

			fade = 450 - 450 * ( (cg.time - mp->time ) / 3000.0 );
			if ( fade < 255 ) {
				if ( fade < 0 ) {
					fade = 0;
				}
				if ( mp->verts[0].modulate[0] != 0 ) {
					for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
						mp->verts[j].modulate[0] = mp->color[0] * fade;
						mp->verts[j].modulate[1] = mp->color[1] * fade;
						mp->verts[j].modulate[2] = mp->color[2] * fade;
					}
				}
			}
		}

		if ( mp->blood ) {
			// blood (cg_gore.c): its drying albedo times the light kept in
			// color[], fading by alpha; 0..255 bytes, unlike the stock path below
			vec3_t	albedo;
			byte	rgba[4];
			float	f = 1.0f;

			t = mp->time + life - cg.time;
			if ( t < MARK_FADE_TIME ) {
				f = (float)t / MARK_FADE_TIME;
			}
			CG_Gore_MarkColor( cg.time - mp->time, albedo );
			for ( j = 0 ; j < 3 ; j++ ) {
				rgba[j] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * albedo[j] * mp->color[j] );
			}
			rgba[3] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * mp->color[3] * f );
			for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
				memcpy( mp->verts[j].modulate, rgba, 4 );
			}
			if ( DistanceSquared( mp->verts[0].xyz, cg.refdef.vieworg ) < 4096.0f * 4096.0f ) {
				trap->R_AddPolysToScene( mp->markShader, mp->poly.numVerts, mp->verts, 1 );
				if ( mp->wetShader ) {
					// fresh blood shines: the same shape again, added, lit per vertex from the view
					float wet = CG_Gore_Wetness( cg.time - mp->time ) * f;
					if ( wet > 0.01f ) {
						polyVert_t	wv[MAX_VERTS_ON_POLY];

						memcpy( wv, mp->verts, mp->poly.numVerts * sizeof( wv[0] ) );
						for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
							CG_Gore_Sheen( wv[j].xyz, mp->normal, mp->lightDir, mp->color, wet, wv[j].modulate );
						}
						trap->R_AddPolysToScene( mp->wetShader, mp->poly.numVerts, wv, 1 );
					}
				}
			}
			continue;
		}

		// fade all marks out with time
		t = mp->time + life - cg.time;
		if ( t < MARK_FADE_TIME ) {
			fade = 255 * t / MARK_FADE_TIME;
			if ( mp->alphaFade ) {
				for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
					mp->verts[j].modulate[3] = fade;
				}
			}
			else
			{
				float f = (float)t / MARK_FADE_TIME;
				for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
					mp->verts[j].modulate[0] = mp->color[0] * f;
					mp->verts[j].modulate[1] = mp->color[1] * f;
					mp->verts[j].modulate[2] = mp->color[2] * f;
				}
			}
		}
		else
		{
			for ( j = 0 ; j < mp->poly.numVerts ; j++ ) {
				mp->verts[j].modulate[0] = mp->color[0];
				mp->verts[j].modulate[1] = mp->color[1];
				mp->verts[j].modulate[2] = mp->color[2];
			}
		}

		trap->R_AddPolysToScene( mp->markShader, mp->poly.numVerts, mp->verts, 1 );
	}
}
