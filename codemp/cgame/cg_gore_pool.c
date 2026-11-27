/*
===========================================================================
Copyright (C) 2026 xenjk contributors

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

// cg_gore_pool.c -- blood pools under the dead (cg_gorePools)
//
// A corpse that has come to rest bleeds a pool that flows over the real floor.
// The floor under it is captured once as a heightfield (POOL_N x POOL_N cells,
// a few traces a frame), then blood poured at the body spreads by the
// virtual-pipe method: each cell pushes blood to its eight neighbours in
// proportion to the difference in surface height, so it runs downhill, stops
// at walls and step-ups, and pours away over drops. Blood only wets a dry cell
// once the pool stands higher than that cell's hold (surface tension, a
// millimetre or so), and a dry cell with more wet neighbours wets more easily:
// so the pool advances with a thick, rounded front and stops when its depth no
// longer beats the hold, about a millimetre deep, leaving only a thin film where
// it has drained. A smooth noise in the hold breaks the circle into lobes. Once
// settled the sim stops and the pool just dries and fades like the other marks.
//
// Drawn as a vertex grid (R_AddPolysToScene) with a wet-shine pass. Cosmetic
// and local; nothing goes back to the server.

#include "cg_local.h"

#define POOL_N			32			// cells a side
#define POOL_CELL		3.0f		// units a cell (the grid spans ~2.7 m)
#define POOL_MAX		8
#define POOL_FILM		0.0015f		// depth a wet cell keeps once drained (~40 microns)
#define POOL_HOLD		0.02f		// head a dry cell needs before it wets (~0.5 mm), by noise x0.6-1.7
#define POOL_TENSE		2.5f		// hold x this for a dry cell beside one wet cell ...
#define POOL_TENSE_FULL	0.2f		// ... down to this inside a notch (eight wet): fronts stay round
#define POOL_MOBILE		0.03f		// depth under which blood moves slower (thin blood drags)
#define POOL_M			0.027f		// metres a unit, for the colour
#define POOL_GAIN		10.0f		// pipe gain: how hard a height difference pushes
#define POOL_DAMP		0.9f		// pipe flow kept from the last step (viscosity)
#define POOL_SMOOTH		0.6f		// even spreading a step (rounds the pipes' star into lobes)
#define POOL_STEP		33			// ms a sim step
#define POOL_BLEED		8000		// ms the body pours
#define POOL_ABOVE		20.0f		// floor search above and below the body's floor
#define POOL_BELOW		30.0f
#define POOL_STEPUP		10.0f		// higher floor than this is a wall to the blood
#define POOL_TRACES		96			// floor traces a frame, all pools together
#define POOL_QUADS		420			// drawn cells a pool, at most
#define POOL_SETTLE		0.0001f		// total depth moved a step under which the pool is still
#define POOL_FADE		10000		// ms of fading at the end of its life

#define POOL_FLOOR		0
#define POOL_WALL		1			// blood does not go there
#define POOL_SINK		2			// no floor (a drop, water): blood that goes there is gone

typedef enum {
	POOL_FREE,
	POOL_CAPTURE,		// tracing the floor
	POOL_FLOW,
	POOL_STILL
} poolState_t;

typedef struct {
	poolState_t	state;
	int			owner;
	int			start;			// when it began to pour
	int			nextStep;
	int			quietSince;		// sim time it has been still since, 0 while moving
	int			nextFloorNote;
	int			captured;		// cells traced so far
	vec3_t		origin;			// the floor under the body: the grid's centre
	float		volume, poured;	// unit^3
	vec3_t		light, lightDir;
	byte		kind[POOL_N * POOL_N];
	float		ground[POOL_N * POOL_N];	// floor height relative to origin, plus roughness
	float		film[POOL_N * POOL_N];
	float		hold[POOL_N * POOL_N];
	float		depth[POOL_N * POOL_N];
	float		flux[8][POOL_N * POOL_N];	// outflow rate to each neighbour, depth/s
} gorePool_t;

static gorePool_t	gorePools[POOL_MAX];
static int			goreDeadSince[MAX_GENTITIES];	// 0 alive; when it was first seen dead
static vec3_t		goreDeadOrg[MAX_GENTITIES];
static qboolean		gorePooled[MAX_GENTITIES];
static qhandle_t	gorePoolShader, gorePoolWetShader;

static const int	poolDX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int	poolDY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

void CG_Gore_PoolRegister( void ) {
	gorePoolShader = trap->R_RegisterShader( "gfx/xen_gore/pool" );
	gorePoolWetShader = trap->R_RegisterShader( "gfx/xen_gore/pool_wet" );
}

void CG_Gore_PoolClear( void ) {
	memset( gorePools, 0, sizeof( gorePools ) );
	memset( goreDeadSince, 0, sizeof( goreDeadSince ) );
	memset( gorePooled, 0, sizeof( gorePooled ) );
}

static int Pool_Life( void ) {
	return CG_Gore_MarkLife() * 2;	// pools outlast the spatter round them
}

// a gentle, deterministic unevenness: a real floor is never flat to a millimetre
static float Pool_Rough( const gorePool_t *p, float x, float y ) {
	float s = (float)( p - gorePools ) * 1.7f + (float)p->start * 0.001f;

	// bumps 15-30 units across (5-10 cells), a fraction of a millimetre high (the hold's
	// noise makes the lobes; more than this and millimetre-deep blood just fills the dips)
	return 0.05f * 0.05f * sinf( x * 0.29f + s ) * cosf( y * 0.23f - s * 0.7f )
		+ 0.05f * ( 0.045f * sinf( x * 0.17f - y * 0.21f + s * 1.3f )
		+ 0.03f * cosf( x * 0.41f + y * 0.37f + s * 2.1f )
		+ 0.02f * sinf( y * 0.47f - x * 0.13f + s * 0.4f ) );
}

/*
===============
Pool_Start

A body at rest at org (its entity origin).
===============
*/
static void Pool_Start( int owner, const vec3_t org ) {
	trace_t tr;
	vec3_t end;
	gorePool_t *p = NULL;
	int i, oldest = -1;

	VectorCopy( org, end );
	end[2] -= 64.0f;
	CG_Trace( &tr, org, NULL, NULL, end, owner, MASK_SOLID );
	if ( tr.fraction == 1.0f || tr.startsolid || tr.plane.normal[2] < 0.7f
		|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) )
		|| ( CG_PointContents( tr.endpos, -1 ) & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
		return;
	}
	for ( i = 0; i < POOL_MAX; i++ ) {
		gorePool_t *q = &gorePools[i];

		if ( q->state == POOL_FREE ) {
			if ( !p ) {
				p = q;
			}
			continue;
		}
		if ( DistanceSquared( q->origin, tr.endpos ) < 24.0f * 24.0f && cg.time - q->start < 60000 ) {
			return;		// already bleeding here (a player's body copy, or bodies in a heap)
		}
		if ( oldest < 0 || q->start < gorePools[oldest].start ) {
			oldest = i;
		}
	}
	if ( !p ) {
		p = &gorePools[oldest];
	}
	memset( p, 0, sizeof( *p ) );
	p->state = POOL_CAPTURE;
	p->owner = owner;
	p->start = cg.time;
	VectorCopy( tr.endpos, p->origin );
	p->volume = flrand( 90.0f, 130.0f ) * ( cg_gore.integer >= 2 ? 1.5f : 1.0f );	// ~2-2.5 litres: ~1.5-1.8 m across
	{
		vec3_t up = { 0.0f, 0.0f, 1.0f };
		CG_Gore_Light( p->origin, up, p->light, p->lightDir );
	}
	if ( cg_goreDebug.integer ) {
		Com_Printf( "gore: pool %d for %d at (%.0f %.0f %.0f), %.0f u3\n", (int)( p - gorePools ), owner,
			p->origin[0], p->origin[1], p->origin[2], p->volume );
	}
}

// trace the floor under up to budget cells; returns traces used
static int Pool_Capture( gorePool_t *p, int budget ) {
	int used = 0;

	while ( p->captured < POOL_N * POOL_N && used < budget ) {
		int i = p->captured++, cx = i % POOL_N, cy = i / POOL_N;
		float x = ( cx - POOL_N / 2 + 0.5f ) * POOL_CELL, y = ( cy - POOL_N / 2 + 0.5f ) * POOL_CELL;
		vec3_t start, end;
		trace_t tr;

		VectorSet( start, p->origin[0] + x, p->origin[1] + y, p->origin[2] + POOL_ABOVE );
		VectorSet( end, start[0], start[1], p->origin[2] - POOL_BELOW );
		CG_Trace( &tr, start, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
		used++;
		p->film[i] = POOL_FILM * flrand( 0.8f, 1.25f );
		{
			// the hold varies smoothly (a floor wets unevenly): the front advances in lobes
			float s = (float)( p - gorePools ) * 1.7f + (float)p->start * 0.001f;
			float n = 0.5f + 0.5f * ( 0.5f * sinf( x * 0.11f + y * 0.05f + s ) * cosf( y * 0.13f - x * 0.04f - s )
				+ 0.3f * sinf( x * 0.23f - y * 0.19f + s * 1.3f ) + 0.2f * cosf( x * 0.07f + y * 0.29f - s * 0.6f ) );

			p->hold[i] = POOL_HOLD * powf( 3.0f, n - 0.5f );
		}
		if ( tr.startsolid || tr.allsolid ) {
			p->kind[i] = POOL_WALL;
		} else if ( tr.fraction == 1.0f
			|| ( CG_PointContents( tr.endpos, -1 ) & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) ) {
			p->kind[i] = POOL_SINK;
		} else if ( tr.plane.normal[2] < 0.55f || tr.endpos[2] - p->origin[2] > POOL_STEPUP
			|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) ) ) {
			p->kind[i] = POOL_WALL;
		} else {
			p->kind[i] = POOL_FLOOR;
			p->ground[i] = tr.endpos[2] - p->origin[2] + Pool_Rough( p, x, y );
		}
	}
	if ( p->captured >= POOL_N * POOL_N ) {
		p->kind[( POOL_N / 2 ) * POOL_N + POOL_N / 2] = POOL_FLOOR;	// the body is on a floor
		p->state = POOL_FLOW;
		p->nextStep = cg.time;
	}
	return used;
}

// one sim step of dt seconds; returns the total depth moved
static float Pool_Step( gorePool_t *p, float dt ) {
	static float out[POOL_N * POOL_N], next[POOL_N * POOL_N], extra[POOL_N * POOL_N], front[POOL_N * POOL_N];
	static qboolean wet[POOL_N * POOL_N];
	int i, k, age = p->nextStep - p->start;
	float moved = 0.0f;

	// the body pours: hard at first, tailing off
	if ( age < POOL_BLEED && p->poured < p->volume ) {
		float add = p->volume / ( POOL_BLEED / 1000.0f ) * 2.0f * ( 1.0f - (float)age / POOL_BLEED ) * dt;

		if ( add > p->volume - p->poured ) {
			add = p->volume - p->poured;
		}
		p->depth[( POOL_N / 2 ) * POOL_N + POOL_N / 2] += add / ( POOL_CELL * POOL_CELL );
		p->poured += add;
	}

	// which cells are wet, and how many wet neighbours each has (for the front's hold)
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		wet[i] = p->kind[i] == POOL_FLOOR && p->depth[i] > p->film[i] * 1.5f;
	}
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		int cx = i % POOL_N, cy = i / POOL_N, n = 0;

		for ( k = 0; k < 8; k++ ) {
			int nx = cx + poolDX[k], ny = cy + poolDY[k];

			n += nx >= 0 && ny >= 0 && nx < POOL_N && ny < POOL_N && wet[ny * POOL_N + nx];
		}
		front[i] = p->hold[i] * ( POOL_TENSE + ( POOL_TENSE_FULL - POOL_TENSE ) * n / 8.0f );
	}

	// pipe flows from each cell's surface height to its neighbours'
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		int cx = i % POOL_N, cy = i / POOL_N;
		float h, mobile;

		out[i] = 0.0f;
		if ( p->kind[i] != POOL_FLOOR || p->depth[i] <= 0.0f ) {
			for ( k = 0; k < 8; k++ ) {
				p->flux[k][i] = 0.0f;
			}
			continue;
		}
		h = p->ground[i] + p->depth[i];
		mobile = Com_Clamp( 0.05f, 1.0f, p->depth[i] / POOL_MOBILE );
		for ( k = 0; k < 8; k++ ) {
			int nx = cx + poolDX[k], ny = cy + poolDY[k], j = ny * POOL_N + nx;
			float hn, f, len = ( k >= 4 ) ? POOL_CELL * 1.4142f : POOL_CELL;

			if ( nx < 0 || ny < 0 || nx >= POOL_N || ny >= POOL_N || p->kind[j] == POOL_SINK ) {
				hn = p->ground[i] - 4.0f;	// off the grid, or over an edge: away it goes
			} else if ( p->kind[j] == POOL_WALL ) {
				p->flux[k][i] = 0.0f;
				continue;
			} else {
				hn = p->ground[j] + p->depth[j];
				if ( !wet[j] ) {
					hn += front[j];	// a dry cell takes blood only once the pool stands above its hold
				}
			}
			if ( h - hn <= 0.0f ) {
				p->flux[k][i] = 0.0f;
				continue;
			}
			f = POOL_DAMP * p->flux[k][i] + dt * POOL_GAIN * mobile * ( h - hn ) / len;
			p->flux[k][i] = f > 0.0f ? f : 0.0f;
			out[i] += p->flux[k][i];
		}
		// a cell cannot give up its film, nor more than it has
		if ( out[i] > 0.0f ) {
			float avail = p->depth[i] - p->film[i];

			if ( avail <= 0.0f ) {
				for ( k = 0; k < 8; k++ ) {
					p->flux[k][i] = 0.0f;
				}
				out[i] = 0.0f;
			} else if ( out[i] * dt > avail ) {
				float s = avail / ( out[i] * dt );

				for ( k = 0; k < 8; k++ ) {
					p->flux[k][i] *= s;
				}
				out[i] *= s;
			}
		}
	}
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		next[i] = p->depth[i] - out[i] * dt;
	}
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		int cx = i % POOL_N, cy = i / POOL_N;

		for ( k = 0; k < 8; k++ ) {
			int nx = cx + poolDX[k], ny = cy + poolDY[k], j;

			if ( p->flux[k][i] <= 0.0f || nx < 0 || ny < 0 || nx >= POOL_N || ny >= POOL_N ) {
				continue;
			}
			j = ny * POOL_N + nx;
			if ( p->kind[j] == POOL_FLOOR ) {
				next[j] += p->flux[k][i] * dt;
			}
		}
	}
	// eight fixed pipe directions grow a star of spokes: an even smoothing of the blood above
	// the film (a 9-point Laplacian between wet cells only, so it never wets a dry one past its
	// hold; it moves blood, never makes or loses it) evens the pool out
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		wet[i] = p->kind[i] == POOL_FLOOR && next[i] > p->film[i] * 1.5f;
		extra[i] = wet[i] ? next[i] - p->film[i] : 0.0f;
	}
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		int cx = i % POOL_N, cy = i / POOL_N;
		float lap = 0.0f;

		if ( !wet[i] ) {
			continue;
		}
		for ( k = 0; k < 8; k++ ) {
			int nx = cx + poolDX[k], ny = cy + poolDY[k], j = ny * POOL_N + nx;

			if ( nx < 0 || ny < 0 || nx >= POOL_N || ny >= POOL_N || !wet[j] ) {
				continue;
			}
			lap += ( k >= 4 ? 1.0f : 4.0f ) * ( extra[j] - extra[i] );
		}
		next[i] += POOL_SMOOTH * lap / 20.0f;
	}
	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		moved += fabsf( next[i] - p->depth[i] );
		p->depth[i] = next[i] > 0.0f ? next[i] : 0.0f;
	}
	return moved;
}

// cell coverage 0..1 for drawing: thin film still reads as a stain
static float Pool_Cover( const gorePool_t *p, int cx, int cy ) {
	int i;

	if ( cx < 0 || cy < 0 || cx >= POOL_N || cy >= POOL_N ) {
		return 0.0f;
	}
	i = cy * POOL_N + cx;
	if ( p->kind[i] != POOL_FLOOR ) {
		return 0.0f;
	}
	return Com_Clamp( 0.0f, 1.0f, ( p->depth[i] - POOL_FILM * 0.5f ) / ( POOL_FILM * 0.5f ) );
}

// edge raggedness 0..1 at a world point: a liquid's edge is not the grid's
static float Pool_EdgeNoise( float x, float y ) {
	float n = 0.35f * sinf( x * 0.21f + y * 0.07f ) * cosf( y * 0.19f - x * 0.05f )
		+ 0.25f * sinf( x * 0.37f - y * 0.31f + 1.3f )
		+ 0.2f * cosf( x * 0.13f + y * 0.43f + 2.1f )
		+ 0.2f * sinf( y * 0.29f + x * 0.47f + 0.6f );

	return 0.5f + 0.5f * n;
}

// the wet shine at a corner: a soft highlight of the grid's main light off the pool's own
// surface normal (its bumps and the rim's meniscus), and a Fresnel reflection of the
// surroundings that the wet shader's environment stage gives shape. Sharp highlights are
// left to that texture: per vertex they light whole cells and read as squares
static void Pool_Shine( const gorePool_t *p, const vec3_t xyz, const vec3_t normal, float wet, byte *out ) {
	vec3_t v, h;
	float nv, nh, spec, fres;
	int i;

	VectorSubtract( cg.refdef.vieworg, xyz, v );
	VectorNormalize( v );
	VectorAdd( v, p->lightDir, h );
	VectorNormalize( h );
	nv = DotProduct( normal, v );
	nh = DotProduct( normal, h );
	nv = nv > 0.0f ? nv : 0.0f;
	nh = nh > 0.0f ? nh : 0.0f;
	spec = powf( nh, 32.0f ) * 0.12f;
	fres = 1.0f - nv;
	// the room's lights are far brighter than its walls: their reflection shows even head-on
	fres = 0.75f + 0.25f * fres * fres * fres * fres * fres;
	for ( i = 0; i < 3; i++ ) {
		float c = wet * ( spec * p->light[i] + fres * ( 0.5f + 0.5f * p->light[i] ) );
		out[i] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * c );
	}
	out[3] = 255;
}

// keep the part of a convex polygon where the field f >= 0 (Sutherland-Hodgman); f is taken as
// linear along each edge, so the pool's edge runs through the cells, not along them
static int Pool_Clip( const polyVert_t *in, const float *f, int n, polyVert_t *out ) {
	int i, k, m = 0;

	for ( i = 0; i < n; i++ ) {
		int j = ( i + 1 ) % n;

		if ( f[i] >= 0.0f ) {
			out[m++] = in[i];
		}
		if ( ( f[i] >= 0.0f ) != ( f[j] >= 0.0f ) ) {
			float t = f[i] / ( f[i] - f[j] );
			polyVert_t *o = &out[m++];

			for ( k = 0; k < 3; k++ ) {
				o->xyz[k] = in[i].xyz[k] + ( in[j].xyz[k] - in[i].xyz[k] ) * t;
			}
			for ( k = 0; k < 2; k++ ) {
				o->st[k] = in[i].st[k] + ( in[j].st[k] - in[i].st[k] ) * t;
			}
			for ( k = 0; k < 4; k++ ) {
				o->modulate[k] = (byte)( in[i].modulate[k] + ( in[j].modulate[k] - in[i].modulate[k] ) * t + 0.5f );
			}
		}
	}
	return m;
}

static void Pool_Draw( gorePool_t *p ) {
	#define PN ( POOL_N + 1 )
	static polyVert_t verts[POOL_QUADS * 4], wet[POOL_QUADS * 4];
	static polyVert_t cv[PN * PN], cw[PN * PN];	// each corner's vertex: body and shine
	static float cover[PN * PN], height[PN * PN], thick[PN * PN], field[PN * PN];
	int age = cg.time - p->start, life = Pool_Life();
	int cx, cy, quads = 0, edges = 0;
	float fade = 1.0f, wetness;
	qboolean shine;
	vec3_t albedo;
	int j;

	if ( !gorePoolShader ) {
		return;
	}
	if ( age > life - POOL_FADE ) {
		fade = Com_Clamp( 0.0f, 1.0f, (float)( life - age ) / POOL_FADE );
	}
	CG_Gore_MarkColor( age, albedo );
	// a pool stays liquid far longer than a spatter: fully glossy for 40 s, matt by 100 s
	wetness = 1.0f - Com_Clamp( 0.0f, 1.0f, ( age - 40000 ) / 60000.0f );
	shine = wetness > 0.0f && gorePoolWetShader && cg_goreDebug.integer != 2;

	// corners: coverage averaged from the four cells round them, height and depth from the floor cells
	for ( cy = 0; cy < PN; cy++ ) {
		for ( cx = 0; cx < PN; cx++ ) {
			float c = 0.0f, h = 0.0f, d = 0.0f;
			int n = 0, a, b;

			for ( b = -1; b <= 0; b++ ) {
				for ( a = -1; a <= 0; a++ ) {
					int x = cx + a, y = cy + b;

					c += Pool_Cover( p, x, y );
					if ( x >= 0 && y >= 0 && x < POOL_N && y < POOL_N && p->kind[y * POOL_N + x] == POOL_FLOOR ) {
						h += p->ground[y * POOL_N + x];
						d += p->depth[y * POOL_N + x];
						n++;
					}
				}
			}
			cover[cy * PN + cx] = c * 0.25f;
			thick[cy * PN + cx] = n ? d / n : 0.0f;
			height[cy * PN + cx] = n ? h / n : 0.0f;
		}
	}

	// each corner's vertex once; field >= 0 is inside the pool. Full cover is always inside; the
	// thinning rim is cut where the coverage falls under the edge noise, and fading lowers it,
	// so the pool shrinks raggedly as it goes
	for ( cy = 0; cy < PN; cy++ ) {
		for ( cx = 0; cx < PN; cx++ ) {
			int v = cy * PN + cx;
			polyVert_t *pv = &cv[v], *wv = &cw[v];
			float t, shade, x, y;
			vec3_t normal;

			x = ( cx - POOL_N / 2 ) * POOL_CELL;
			y = ( cy - POOL_N / 2 ) * POOL_CELL;
			pv->xyz[0] = p->origin[0] + x;
			pv->xyz[1] = p->origin[1] + y;
			pv->xyz[2] = p->origin[2] + height[v] + 0.25f;
			pv->st[0] = pv->xyz[0] / 64.0f;
			pv->st[1] = pv->xyz[1] / 64.0f;
			field[v] = cover[v] * fade * ( 0.75f + 0.25f * Pool_EdgeNoise( pv->xyz[0], pv->xyz[1] ) ) - 0.5f;

			// the body multiplies the floor (GL_DST_COLOR GL_ZERO), so it is lit exactly as the floor
			// is on any map (the grid's light misjudged lightmapped floors badly). Blood is a red
			// filter: light goes through a thin layer (red most, blue least) and off the floor,
			// so a drained film is a clear red stain; thicker than half a millimetre or so it is
			// an opaque, dark pigment. The filter is worked out in linear light against a floor
			// taken as 0.25 albedo, then put back in the framebuffer's gamma.
			{
				static const float absorb[3] = { 4000.0f, 25000.0f, 36000.0f };	// per metre
				float hc = thick[v] * POOL_M, opaque, pig;

				t = Com_Clamp( 0.0f, 1.0f, ( hc - 0.00010f ) / 0.0011f );
				opaque = Com_Clamp( 0.0f, 1.0f, ( hc - 0.00012f ) / 0.0005f );
				// dark where thick: a multiply keeps the floor's own detail, darker compresses it
				pig = ( 1.3f + ( 0.45f - 1.3f ) * t * ( 2.0f - t ) );
				for ( j = 0; j < 3; j++ ) {
					float through = expf( -absorb[j] * hc ) * ( 1.0f - opaque * ( 2.0f - opaque ) );
					float pigment = powf( albedo[j], 2.2f ) * pig;
					float filter = through + pigment * ( 1.0f - through ) / 0.25f;

					pv->modulate[j] = (byte)( 255.0f * powf( Com_Clamp( 0.0f, 1.0f, filter ), 1.0f / 2.2f ) );
				}
				pv->modulate[3] = 255;
				shade = t;
			}

			if ( !shine ) {
				continue;
			}
			// the blood surface's normal, its slopes exaggerated: the rim's meniscus and the floor's
			// bumps catch the light
			{
				// two corners each way: a liquid's surface is smoother than the cells' film noise,
				// which shattered the reflections at one
				int ax = cx >= 2 ? 2 : cx, bx = POOL_N - cx >= 2 ? 2 : POOL_N - cx;
				int ay = cy >= 2 ? 2 : cy, by = POOL_N - cy >= 2 ? 2 : POOL_N - cy;
				int x0 = v - ax, x1 = v + bx, y0 = v - ay * PN, y1 = v + by * PN;
				// the floor's slope, and the blood's own thickness (a millimetre) much exaggerated so
				// the rim's meniscus catches the light
				float dx = ax + bx ? ( 2.5f * ( height[x1] - height[x0] ) + 25.0f * ( thick[x1] - thick[x0] ) ) / ( ( ax + bx ) * POOL_CELL ) : 0.0f;
				float dy = ay + by ? ( 2.5f * ( height[y1] - height[y0] ) + 25.0f * ( thick[y1] - thick[y0] ) ) / ( ( ay + by ) * POOL_CELL ) : 0.0f;

				VectorSet( normal, -dx, -dy, 1.0f );
				VectorNormalize( normal );
			}
			*wv = *pv;
			Pool_Shine( p, pv->xyz, normal, wetness, wv->modulate );
			{
				// the reflected ray's horizontal part into the dome texture (straight up its centre)
				vec3_t view, r;
				float d;

				VectorSubtract( cg.refdef.vieworg, pv->xyz, view );
				VectorNormalize( view );
				d = DotProduct( normal, view );
				VectorScale( normal, 2.0f * d, r );
				VectorSubtract( r, view, r );
				wv->st[0] = 0.5f + 0.5f * r[0];
				wv->st[1] = 0.5f + 0.5f * r[1];
			}
			wv->modulate[3] = pv->modulate[3];
		}
	}

	for ( cy = 0; cy < POOL_N; cy++ ) {
		for ( cx = 0; cx < POOL_N; cx++ ) {
			static const int cornerX[4] = { 0, 0, 1, 1 }, cornerY[4] = { 0, 1, 1, 0 };	// clockwise seen from above: Q3's front
			polyVert_t in[4], out[8];
			float f[4], lo = 9999.0f, hi = -9999.0f;
			int c, inside = 0, idx[4];

			if ( p->kind[cy * POOL_N + cx] != POOL_FLOOR ) {
				continue;
			}
			for ( c = 0; c < 4; c++ ) {
				int v = ( cy + cornerY[c] ) * PN + cx + cornerX[c];

				idx[c] = v;
				f[c] = field[v];
				inside += f[c] >= 0.0f;
				if ( height[v] < lo ) lo = height[v];
				if ( height[v] > hi ) hi = height[v];
			}
			if ( !inside || hi - lo > 3.0f ) {
				continue;	// dry, or a cell that would bridge a step
			}
			if ( inside == 4 ) {
				if ( quads >= POOL_QUADS ) {
					continue;
				}
				for ( c = 0; c < 4; c++ ) {
					verts[quads * 4 + c] = cv[idx[c]];
					wet[quads * 4 + c] = cw[idx[c]];
				}
				quads++;
				continue;
			}
			// a rim cell: the part inside, drawn on its own
			if ( edges >= POOL_QUADS ) {
				continue;
			}
			for ( c = 0; c < 4; c++ ) {
				in[c] = cv[idx[c]];
			}
			j = Pool_Clip( in, f, 4, out );
			if ( j >= 3 ) {
				trap->R_AddPolysToScene( gorePoolShader, j, out, 1 );
				if ( shine ) {
					for ( c = 0; c < 4; c++ ) {
						in[c] = cw[idx[c]];
					}
					Pool_Clip( in, f, 4, out );
					trap->R_AddPolysToScene( gorePoolWetShader, j, out, 1 );
				}
				edges++;
			}
		}
	}
	if ( quads ) {
		trap->R_AddPolysToScene( gorePoolShader, 4, verts, quads );
		if ( shine ) {
			trap->R_AddPolysToScene( gorePoolWetShader, 4, wet, quads );
		}
	}
	#undef PN
}

// feet that step in a pool pick blood up (CG_Gore_Footstep): note its wet area as floor blood
static void Pool_NoteFloor( gorePool_t *p ) {
	vec3_t mid = { 0.0f, 0.0f, 0.0f };
	int i, n = 0;

	for ( i = 0; i < POOL_N * POOL_N; i++ ) {
		if ( p->kind[i] == POOL_FLOOR && p->depth[i] > POOL_FILM * 0.5f ) {
			mid[0] += ( i % POOL_N - POOL_N / 2 + 0.5f ) * POOL_CELL;
			mid[1] += ( i / POOL_N - POOL_N / 2 + 0.5f ) * POOL_CELL;
			n++;
		}
	}
	if ( n ) {
		VectorScale( mid, 1.0f / n, mid );
		VectorAdd( mid, p->origin, mid );
		CG_Gore_FloorBlood( mid, sqrtf( n * POOL_CELL * POOL_CELL / M_PI ) );
	}
}

// bodies come to rest, then bleed
static void Pool_Watch( int num ) {
	centity_t *cent = &cg_entities[num];

	if ( !( cent->currentState.eFlags & EF_DEAD ) || !CG_Gore_Bleeds( num ) ) {
		goreDeadSince[num] = 0;
		gorePooled[num] = qfalse;
		return;
	}
	if ( gorePooled[num] ) {
		return;
	}
	if ( !goreDeadSince[num] || DistanceSquared( cent->lerpOrigin, goreDeadOrg[num] ) > 1.0f ) {
		goreDeadSince[num] = cg.time;	// still falling or sliding: wait for it to lie still
		VectorCopy( cent->lerpOrigin, goreDeadOrg[num] );
		return;
	}
	if ( cg.time - goreDeadSince[num] > 700 ) {
		vec3_t at;

		// the body's middle, not the entity origin: a death animation carries the body off it
		VectorCopy( cent->lerpOrigin, at );
		if ( cent->ghoul2 ) {
			int bolt = trap->G2API_AddBolt( cent->ghoul2, 0, "lower_lumbar" );
			mdxaBone_t m;

			vec3_t yaw = { 0.0f, 0.0f, 0.0f }, mid;

			yaw[YAW] = cent->lerpAngles[YAW];
			if ( bolt >= 0 && trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, yaw, cent->lerpOrigin,
				cg.time, cgs.gameModels, cent->modelScale ) ) {
				BG_GiveMeVectorFromMatrix( &m, ORIGIN, mid );
				if ( fabsf( mid[0] - at[0] ) < 64.0f && fabsf( mid[1] - at[1] ) < 64.0f && fabsf( mid[2] - at[2] ) < 48.0f ) {
					VectorCopy( mid, at );	// otherwise a cut-off or odd skeleton: keep the origin
					at[2] += 4.0f;
				}
			}
		}
		gorePooled[num] = qtrue;
		Pool_Start( num, at );
	}
}

/*
===============
CG_Gore_AddPools

Once a frame (from CG_Gore_AddDrops): start pools under bodies at rest,
trace floors, run the sims and draw.
===============
*/
void CG_Gore_AddPools( void ) {
	int i, budget = POOL_TRACES;

	if ( !cg_gore.integer || !cg_gorePools.integer || !cg.snap ) {
		return;
	}
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		Pool_Watch( cg.snap->entities[i].number );
	}
	Pool_Watch( cg.snap->ps.clientNum );

	for ( i = 0; i < POOL_MAX; i++ ) {
		gorePool_t *p = &gorePools[i];
		int steps = 0;

		if ( p->state == POOL_FREE ) {
			continue;
		}
		if ( cg.time - p->start > Pool_Life() || cg.time < p->start ) {
			p->state = POOL_FREE;
			continue;
		}
		if ( p->state == POOL_CAPTURE ) {
			if ( budget > 0 ) {
				budget -= Pool_Capture( p, budget );
			}
			continue;
		}
		while ( p->state == POOL_FLOW && cg.time >= p->nextStep && steps < 4 ) {
			float moved = Pool_Step( p, POOL_STEP / 1000.0f );

			p->nextStep += POOL_STEP;
			steps++;
			if ( p->nextStep - p->start > POOL_BLEED && moved < POOL_SETTLE ) {
				if ( !p->quietSince ) {
					p->quietSince = p->nextStep;
				} else if ( p->nextStep - p->quietSince > 2000 ) {
					p->state = POOL_STILL;
					if ( cg_goreDebug.integer ) {
						int c, wet = 0, kinds[3] = { 0, 0, 0 };
						float deepest = 0.0f;

						for ( c = 0; c < POOL_N * POOL_N; c++ ) {
							kinds[p->kind[c]]++;
							wet += p->depth[c] > POOL_FILM * 0.5f;
							if ( p->depth[c] > deepest ) {
								deepest = p->depth[c];
							}
						}
						Com_Printf( "gore: pool %d still after %.1f s: %d wet cells, deepest %.3f, poured %.0f, floor %d wall %d sink %d\n",
							i, ( p->nextStep - p->start ) / 1000.0f, wet, deepest, p->poured, kinds[0], kinds[1], kinds[2] );
					}
				}
			} else {
				p->quietSince = 0;
			}
		}
		if ( cg.time > p->nextStep + 500 ) {
			p->nextStep = cg.time;	// fell behind (a hitch): carry on from now, slower
		}
		if ( cg.time >= p->nextFloorNote && CG_Gore_Wetness( cg.time - p->start - 10000 ) > 0.2f ) {
			p->nextFloorNote = cg.time + 1000;
			Pool_NoteFloor( p );
		}
		Pool_Draw( p );
	}
}

/*
===============
CG_Gore_Pool_f

gore_pool (with cg_goreDebug 1): a pool on the floor in view, as if a body
lay there. cg_goreDebug 2 also leaves out the wet shine.
===============
*/
void CG_Gore_Pool_f( void ) {
	trace_t tr;
	vec3_t end;

	if ( !cg_goreDebug.integer ) {
		Com_Printf( "gore_pool needs cg_goreDebug 1\n" );
		return;
	}
	VectorMA( cg.refdef.vieworg, 8192.0f, cg.refdef.viewaxis[0], end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.predictedPlayerState.clientNum, CONTENTS_SOLID );
	if ( tr.fraction >= 1.0f ) {
		Com_Printf( "gore_pool: nothing in view\n" );
		return;
	}
	VectorMA( tr.endpos, 4.0f, tr.plane.normal, end );
	Pool_Start( ENTITYNUM_NONE, end );
}
