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

// cg_gore.c -- client-side blood
//
// Flesh hits throw ballistic drops. Each drop flies under gravity, passes
// through bodies and leaves a mark where it lands: a splat on floors and
// ceilings, a downward run on walls. Hits also leave a blood mark on the body.
// Everything here is cosmetic and local; nothing goes back to the server.
//
// cg_gore       0 off, 1 on, 2 heavy
// cg_goreSaber  1 saber wounds bleed, 0 keeps the stock cauterised look
// cg_goreTime   seconds a blood mark stays on the world before it fades
//
// Also: severed limbs and stumps bleed (cg_ents.c), a blade that cut someone
// drips for a few seconds (CG_Gore_Blade), and feet that step in fresh blood
// leave a few prints (CG_Gore_Footstep). Bodies at rest bleed pools
// (cg_gore_pool.c).
//
// The art lives in zz_xenjk_gore.pk3 (tools/gore/make_gore_assets.py). Without
// it the shaders do not register and the matching pieces are skipped.

#include "cg_local.h"

#define GORE_MAX_DROPS		384
#define GORE_DROP_LIFE		4000	// ms before an unlanded drop is dropped
#define GORE_STUMPS			24
#define GORE_STUMP_LIFE		5000	// ms a stump bleeds, weakening
#define GORE_LIMB_TRAIL		2500	// ms a severed limb trails blood
#define GORE_BLADE_LIFE		4000	// ms a blade drips after a cut
#define GORE_FLOOR_SPOTS	96		// floor blood remembered for footprints
#define GORE_FLOOR_FRESH	40000	// ms floor blood stays wet enough to step in
#define GORE_PRINT_STEPS	10		// prints a foot leaves after a step in blood
#define GORE_PRINTS			3		// loads of print art, heavy to faint
#define GORE_CHUNKS			6		// meat chunk models, small to large
#define GORE_SPLATS			4
#define GORE_STREAKS		3
#define GORE_SLANTS			3
#define GORE_DRY_TIME		45000	// ms for a mark to go from fresh to dried
#define GORE_WET_TIME		30000	// ms for its shine to go

typedef struct goreDrop_s {
	qboolean		active;
	trajectory_t	pos;
	vec3_t			last;		// position last frame (the streak's tail)
	float			markRadius;	// size of the mark it leaves on landing
	int				endTime;
} goreDrop_t;

static goreDrop_t	goreDrops[GORE_MAX_DROPS];

// stumps left by dismemberment: the cut bleeds in pulses from the owner's cap bolt
typedef struct {
	int		owner;		// entity whose limb came off; start 0 = free
	int		bolt;		// the stump cap's bolt on its ghoul2
	int		start;
	int		nextPulse, nextDrip;
} goreStump_t;
static goreStump_t	goreStumps[GORE_STUMPS];
static int			goreLimbStart[MAX_GENTITIES];	// when each severed limb was cut

// recent wall runs: a spray lands many drops on one spot, and a run on top of a
// run stacks their heads into blobs, so a drop that lands near a fresh run stains
// the wall as a spot instead
#define GORE_RECENT_RUNS	16
typedef struct {
	vec3_t	origin;
	float	radius;
	int		time;
} goreRun_t;
static goreRun_t	goreRuns[GORE_RECENT_RUNS];
static int			goreNextRun;

// floor blood, for feet to step in (same record as a run: where, how big, when)
static goreRun_t	goreFloor[GORE_FLOOR_SPOTS];
static int			goreNextFloor;

// per entity: when its blade last cut flesh, its next drip, its saber tips last
// frame (to fling drops off a swing), and the bloody prints its feet have left
static int			goreBladeTime[MAX_GENTITIES];
static int			goreBladeNext[MAX_GENTITIES];
static vec3_t		goreBladeTip[MAX_GENTITIES][2];
static int			goreBladeTipTime[MAX_GENTITIES][2];
static int			goreFeetSteps[MAX_GENTITIES];
static qhandle_t	gorePrint[2][GORE_PRINTS];	// [left, right][load]
static qhandle_t	goreChunk[GORE_CHUNKS];
static int			goreNumChunks;
static int			goreNextDrop;

// one set of art per cg_goreLook: v1 flat stains, v2 thick/thin stains with
// slanted drops and a wet shine (the *_wet shaders; 0 where a look has none)
typedef struct goreLook_s {
	qhandle_t	splat[GORE_SPLATS], splatWet[GORE_SPLATS];
	int			numSplats;
	qhandle_t	streak[GORE_STREAKS], streakWet[GORE_STREAKS];
	int			numStreaks;
	qhandle_t	slant[GORE_SLANTS], slantWet[GORE_SLANTS];
	int			numSlants;
	qhandle_t	body;
	float		wallScale;	// size of a wall run against the drop's radius
	float		decalSize;	// > 0: one spaced decal of this half size per spot, not one per drop (reference art)
} goreLook_t;

#define GORE_LOOKS	3	// 1 v1, 2 v2, 3 a slot for local reference art (gfx/xen_gore/ref_*)
static goreLook_t	goreLooks[GORE_LOOKS];
static qhandle_t	goreDropShader;

// the wet shine's reflection tint: what the surroundings look like in a film of blood
static const vec3_t goreSheenTint = { 1.0f, 0.72f, 0.68f };

// blood albedo: fresh is a deep red, dried a dark brown. Marks and drops are
// alpha blended and lit from the light grid (CG_Gore_Light), so they read on
// dark and bright surfaces alike.
static const vec3_t goreFresh = { 0.40f, 0.025f, 0.02f };
static const vec3_t goreDried = { 0.16f, 0.035f, 0.025f };

/*
===============
CG_Gore_Register
===============
*/
static int Gore_RegisterKind( const char *prefix, const char *kind, int max, qboolean wet,
	qhandle_t *shaders, qhandle_t *wets ) {
	int i, n = 0;

	for ( i = 0; i < max; i++ ) {
		qhandle_t h = trap->R_RegisterShader( va( "gfx/xen_gore/%s%s%i", prefix, kind, i + 1 ) );
		if ( h ) {
			shaders[n] = h;
			wets[n] = wet ? trap->R_RegisterShader( va( "gfx/xen_gore/%s%s%i_wet", prefix, kind, i + 1 ) ) : 0;
			n++;
		}
	}
	return n;
}

void CG_Gore_Register( void ) {
	int l;

	memset( goreLooks, 0, sizeof( goreLooks ) );
	for ( l = 0; l < GORE_LOOKS; l++ ) {
		goreLook_t *look = &goreLooks[l];
		const char *prefix = l == 2 ? "ref_" : l ? "v2_" : "";

		look->numSplats = Gore_RegisterKind( prefix, "splat", GORE_SPLATS, l, look->splat, look->splatWet );
		look->numStreaks = Gore_RegisterKind( prefix, "streak", GORE_STREAKS, l, look->streak, look->streakWet );
		if ( l ) {
			look->numSlants = Gore_RegisterKind( prefix, "slant", GORE_SLANTS, l, look->slant, look->slantWet );
		}
		look->body = trap->R_RegisterShader( va( "gfx/xen_gore/%sbody", l == 2 ? "v2_" : prefix ) );	// reference art has world marks only
		look->wallScale = 1.0f;
		// v2 and the reference art are whole spots painted as one decal: floor half size in units
		// (v2 ~1 m across for an average drop, scaled by the drop; reference ~1.5 m, fixed)
		look->decalSize = l == 2 ? 27.0f : l == 1 ? 18.0f : 0.0f;	// owner 2026-11-26 tried 1.7 for v2 and liked the smaller size
	}
	goreDropShader = trap->R_RegisterShader( "gfx/xen_gore/drop" );
	CG_Gore_PoolRegister();
	goreNumChunks = 0;
	for ( l = 0; l < GORE_CHUNKS; l++ ) {
		qhandle_t h = trap->R_RegisterModel( va( "models/xen_gore/chunk%i.md3", l + 1 ) );
		if ( h ) {
			goreChunk[goreNumChunks++] = h;
		}
	}
	for ( l = 0; l < GORE_PRINTS; l++ ) {
		gorePrint[0][l] = trap->R_RegisterShader( va( "gfx/xen_gore/print_l%i", l + 1 ) );
		gorePrint[1][l] = trap->R_RegisterShader( va( "gfx/xen_gore/print_r%i", l + 1 ) );
	}

	// severed limbs: stock caps are charred with glowing embers (cauterised). Bleeding
	// cuts show raw flesh instead; nearly every model's cap surfaces use this one shader.
	if ( CG_Gore_Severed() && trap->R_RegisterShader( "gfx/xen_gore/cap" ) ) {
		trap->R_RegisterShader( "models/players/stormtrooper/caps" );
		trap->R_RemapShader( "models/players/stormtrooper/caps", "gfx/xen_gore/cap", "0" );
	}
}

// the art cg_goreLook asks for, or v1 when the pack has no v2
static const goreLook_t *Gore_Look( void ) {
	int l = Com_Clampi( 1, GORE_LOOKS, cg_goreLook.integer ) - 1;

	while ( l > 0 && !goreLooks[l].numSplats ) {
		l--;
	}
	return &goreLooks[l];
}

/*
===============
CG_Gore_Clear
===============
*/
void CG_Gore_Clear( void ) {
	memset( goreDrops, 0, sizeof( goreDrops ) );
	goreNextDrop = 0;
	memset( goreRuns, 0, sizeof( goreRuns ) );
	goreNextRun = 0;
	memset( goreStumps, 0, sizeof( goreStumps ) );
	memset( goreLimbStart, 0, sizeof( goreLimbStart ) );
	memset( goreFloor, 0, sizeof( goreFloor ) );
	goreNextFloor = 0;
	memset( goreBladeTime, 0, sizeof( goreBladeTime ) );
	memset( goreBladeNext, 0, sizeof( goreBladeNext ) );
	memset( goreBladeTipTime, 0, sizeof( goreBladeTipTime ) );
	memset( goreFeetSteps, 0, sizeof( goreFeetSteps ) );
	CG_Gore_PoolClear();
}

static float Gore_Level( void ) {
	return (float)Com_Clampi( 0, 2, cg_gore.integer );
}

int CG_Gore_MarkLife( void ) {
	return Com_Clampi( 5, 1800, cg_goreTime.integer ) * 1000;
}

/*
===============
CG_Gore_Light

Light reaching a surface at point with this normal (0..~2 per channel),
sampled a little off the surface so the grid point is not inside a wall.
===============
*/
void CG_Gore_Light( const vec3_t point, const vec3_t normal, vec3_t out, vec3_t dirOut ) {
	vec3_t p, ambient, directed, dir;
	float incoming;
	int i;

	VectorMA( point, 4.0f, normal, p );
	if ( !trap->R_LightForPoint( p, ambient, directed, dir ) ) {
		VectorSet( out, 1.0f, 1.0f, 1.0f );
		if ( dirOut ) {
			VectorSet( dirOut, 0.0f, 0.0f, 1.0f );
		}
		return;
	}
	if ( dirOut ) {
		VectorCopy( dir, dirOut );
	}
	incoming = DotProduct( normal, dir );
	if ( incoming < 0.0f ) {
		incoming = 0.0f;
	}
	for ( i = 0; i < 3; i++ ) {
		// the grid comes back after the map's overbright shift: x3 brings it near
		// what the surface shows (set by eye on mp/ctf2 and mp/duel1)
		out[i] = Com_Clamp( 0.25f, 1.3f, 3.0f * ( ambient[i] + incoming * directed[i] ) / 255.0f );
	}
}

/*
===============
CG_Gore_Wetness

How much a mark of this age still shines, 1 fresh .. 0 dry.
===============
*/
float CG_Gore_Wetness( int age ) {
	float f = (float)age / GORE_WET_TIME;

	if ( f >= 1.0f ) {
		return 0.0f;
	}
	if ( f < 0.0f ) {
		f = 0.0f;
	}
	return 1.0f - f * ( 2.0f - f );
}

/*
===============
CG_Gore_Sheen

Vertex colour of a mark's additive wet shine: a tight highlight of the
grid's main light where it reflects toward the eye, and a reddened
reflection of the surroundings that grows at grazing angles (Fresnel).
light is the mark's stored light, wet 0..1.
===============
*/
void CG_Gore_Sheen( const vec3_t xyz, const vec3_t normal, const vec3_t lightDir, const float *light, float wet, byte *out ) {
	vec3_t v, h;
	float nv, nh, spec, fres;
	int i;

	VectorSubtract( cg.refdef.vieworg, xyz, v );
	VectorNormalize( v );
	VectorAdd( v, lightDir, h );
	VectorNormalize( h );
	nv = DotProduct( normal, v );
	nh = DotProduct( normal, h );
	nv = nv > 0.0f ? nv : 0.0f;
	nh = nh > 0.0f ? nh : 0.0f;
	// per-vertex, so the highlight lights a whole small mark at once: keep it faint or drops turn pink
	spec = powf( nh, 96.0f ) * 0.07f;
	fres = 1.0f - nv;
	fres = 0.03f + 0.97f * fres * fres * fres * fres * fres;
	for ( i = 0; i < 3; i++ ) {
		float c = wet * ( spec * light[i] + fres * 0.05f * light[i] * goreSheenTint[i] );
		out[i] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * c );
	}
	out[3] = 255;
}

/*
===============
CG_Gore_MarkColor

Albedo of blood at this age.
===============
*/
void CG_Gore_MarkColor( int age, vec3_t out ) {
	float f = (float)age / GORE_DRY_TIME;
	if ( f > 1.0f ) {
		f = 1.0f;
	}
	f = f * ( 2.0f - f );	// dries fast at first, then slowly
	out[0] = goreFresh[0] + ( goreDried[0] - goreFresh[0] ) * f;
	out[1] = goreFresh[1] + ( goreDried[1] - goreFresh[1] ) * f;
	out[2] = goreFresh[2] + ( goreDried[2] - goreFresh[2] ) * f;
}

/*
===============
CG_Gore_Bleeds

Organic targets only: droids, vehicles and the like spark instead.
===============
*/
static qboolean Gore_Bleeds( int entityNum ) {
	centity_t *cent;

	if ( entityNum < 0 || entityNum >= ENTITYNUM_WORLD ) {
		return qfalse;
	}
	cent = &cg_entities[entityNum];
	if ( cent->currentState.eType == ET_NPC ) {
		switch ( cent->currentState.NPC_class ) {
		case CLASS_ATST:
		case CLASS_GONK:
		case CLASS_INTERROGATOR:
		case CLASS_MARK1:
		case CLASS_MARK2:
		case CLASS_MOUSE:
		case CLASS_PROBE:
		case CLASS_PROTOCOL:
		case CLASS_R2D2:
		case CLASS_R5D2:
		case CLASS_REMOTE:
		case CLASS_SEEKER:
		case CLASS_SENTRY:
		case CLASS_VEHICLE:
		case CLASS_GALAKMECH:
			return qfalse;
		default:
			break;
		}
		return qtrue;
	}
	return ( cent->currentState.eType == ET_PLAYER || cent->currentState.eType == ET_BODY ) ? qtrue : qfalse;
}

qboolean CG_Gore_Bleeds( int entityNum ) {
	return Gore_Bleeds( entityNum );
}

// floor blood feet can step in (CG_Gore_Footstep), noted for 40 s
void CG_Gore_FloorBlood( const vec3_t origin, float radius ) {
	goreRun_t *f = &goreFloor[goreNextFloor];

	goreNextFloor = ( goreNextFloor + 1 ) % GORE_FLOOR_SPOTS;
	VectorCopy( origin, f->origin );
	f->radius = radius;
	f->time = cg.time;
}

/*
===============
Gore_Drop

Launch one drop. Speeds are in units per second.
===============
*/
static void Gore_Drop( const vec3_t org, const vec3_t vel, float markRadius ) {
	goreDrop_t *d = &goreDrops[goreNextDrop];

	goreNextDrop = ( goreNextDrop + 1 ) % GORE_MAX_DROPS;	// reuse the oldest when full

	memset( d, 0, sizeof( *d ) );
	d->active = qtrue;
	d->pos.trType = TR_GRAVITY;
	d->pos.trTime = cg.time;
	VectorCopy( org, d->pos.trBase );
	VectorCopy( vel, d->pos.trDelta );
	VectorCopy( org, d->last );
	d->markRadius = markRadius;
	d->endTime = cg.time + GORE_DROP_LIFE;
}

/*
===============
Gore_Spray

count drops leaving org around dir (normalised): spread 0 is a tight jet,
1 a hemisphere. speed is the jet's mean speed.
===============
*/
static void Gore_Spray( const vec3_t org, const vec3_t dir, int count, float speed, float spread, float markRadius ) {
	int i;
	float level = Gore_Level();
	vec3_t vel, jitter;

	if ( cg_goreDebug.integer ) {
		Com_Printf( "gore: spray %d at (%.0f %.0f %.0f) dir (%.2f %.2f %.2f) speed %.0f (level %.0f, %d splat shaders)\n",
			count, org[0], org[1], org[2], dir[0], dir[1], dir[2], speed, level, Gore_Look()->numSplats );
	}
	if ( level <= 0.0f || !Gore_Look()->numSplats ) {
		return;
	}
	if ( level >= 2.0f ) {
		count *= 2;
		markRadius *= 1.3f;
	}
	for ( i = 0; i < count; i++ ) {
		VectorSet( jitter, flrand( -1.0f, 1.0f ), flrand( -1.0f, 1.0f ), flrand( -1.0f, 1.0f ) );
		VectorMA( dir, spread, jitter, vel );
		if ( VectorNormalize( vel ) < 0.001f ) {
			VectorCopy( dir, vel );
		}
		VectorScale( vel, speed * flrand( 0.35f, 1.25f ), vel );
		Gore_Drop( org, vel, markRadius * flrand( 0.5f, 1.4f ) );
	}
}

// qtrue when a run made in the last 2 s already covers this spot
static qboolean Gore_NearRecentRun( const vec3_t point, float radius ) {
	int i;

	for ( i = 0; i < GORE_RECENT_RUNS; i++ ) {
		const goreRun_t *r = &goreRuns[i];
		float reach;

		if ( !r->time || cg.time - r->time > 2000 || cg.time < r->time ) {
			continue;
		}
		reach = ( r->radius + radius ) * 0.9f;
		if ( DistanceSquared( point, r->origin ) < reach * reach ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void Gore_RememberRun( const vec3_t point, float radius ) {
	goreRun_t *r = &goreRuns[goreNextRun];

	goreNextRun = ( goreNextRun + 1 ) % GORE_RECENT_RUNS;
	VectorCopy( point, r->origin );
	r->radius = radius;
	r->time = cg.time;
}

/*
===============
Gore_LandMark

A drop hit the world: leave a splat, or a run down a wall.
===============
*/
/*
===============
Gore_DecalMark

Reference placement for art painted as large decals (cg_goreLook 3): one decal per
spot at the art's own size, spaced so a spray does not stack them.
===============
*/
static void Gore_DecalMark( const trace_t *tr, const vec3_t velocity, float radius, const goreLook_t *look, int life ) {
	const float *n = tr->plane.normal;
	float size = look->decalSize, speed = VectorLength( velocity );
	vec3_t dir, origin;
	int k;

	if ( look != &goreLooks[2] ) {
		size *= Com_Clamp( 0.6f, 1.5f, radius / 7.0f );	// bigger hits, bigger spots
	}
	{
		// a wall spot is wide and its drips long, so walls keep spots further apart
		float spacing = ( n[2] > -0.35f && n[2] < 0.6f ) ? size * 0.6f : size * 0.5f;

		if ( Gore_NearRecentRun( tr->endpos, spacing ) ) {
			return;
		}
		Gore_RememberRun( tr->endpos, spacing );
	}

	if ( n[2] > -0.35f && n[2] < 0.6f && look->numStreaks ) {
		VectorSet( dir, 0.0f, 0.0f, -1.0f );
		VectorMA( dir, -DotProduct( dir, n ), n, dir );
		if ( VectorNormalize( dir ) > 0.1f ) {
			float halfWidth = size * 0.8f;	// walls ~1.2 m
			VectorMA( tr->endpos, 2.0f * halfWidth * 0.6f, dir, origin );
			k = rand() % look->numStreaks;
			CG_Gore_ImpactMarkAxis( look->streak[k], look->streakWet[k], origin, n, dir,
				halfWidth, 2.0f * halfWidth, life );
			return;
		}
	}
	if ( look->numSlants && speed > 120.0f ) {
		VectorMA( velocity, -DotProduct( velocity, n ), n, dir );
		if ( VectorNormalize( dir ) / speed > 0.55f ) {
			float halfWidth = size * 1.8f;	// directional spray ~2.5-3.3 m
			VectorMA( tr->endpos, 2.0f * halfWidth * 0.8f, dir, origin );
			k = rand() % look->numSlants;
			CG_Gore_ImpactMarkAxis( look->slant[k], look->slantWet[k], origin, n, dir,
				halfWidth, 2.0f * halfWidth, life );
			return;
		}
	}
	k = rand() % look->numSplats;
	size *= flrand( 0.8f, 1.2f );
	CG_Gore_ImpactMarkAxis( look->splat[k], look->splatWet[k], tr->endpos, n, NULL, size, size, life );
}

static void Gore_LandMark( const trace_t *tr, const vec3_t velocity, float radius ) {
	const float *n = tr->plane.normal;
	const goreLook_t *look = Gore_Look();
	int life = CG_Gore_MarkLife();
	float speed = VectorLength( velocity );
	int k;

	if ( tr->surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) ) {
		return;
	}
	if ( CG_PointContents( tr->endpos, -1 ) & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		return;
	}
	if ( n[2] > 0.7f ) {
		// floor blood feet can step in: about the stain's solid middle
		CG_Gore_FloorBlood( tr->endpos,
			look->decalSize > 0.0f ? look->decalSize * 0.45f * Com_Clamp( 0.6f, 1.5f, radius / 7.0f ) : radius );
	}
	if ( look->decalSize > 0.0f ) {
		Gore_DecalMark( tr, velocity, radius, look, life );
		return;
	}

	// faster drops spread wider on impact
	radius *= 0.8f + Com_Clamp( 0.0f, 1.0f, speed / 400.0f ) * 0.6f;

	if ( cg_goreDebug.integer ) {
		vec3_t light;
		CG_Gore_Light( tr->endpos, n, light, NULL );
		Com_Printf( "gore: land (%.0f %.0f %.0f) normal (%.2f %.2f %.2f) radius %.1f light %.2f %.2f %.2f\n",
			tr->endpos[0], tr->endpos[1], tr->endpos[2], n[0], n[1], n[2], radius, light[0], light[1], light[2] );
	}

	if ( look->numSlants && speed > 120.0f ) {
		// a drop that hits at a slant leaves a stain stretched the way it flew,
		// its tail pointing on (v2 art)
		vec3_t along;
		float slant;

		VectorMA( velocity, -DotProduct( velocity, n ), n, along );
		slant = VectorNormalize( along ) / speed;
		if ( slant > flrand( 0.55f, 0.75f ) ) {
			float halfHeight = radius * ( 0.9f + 1.6f * slant );
			vec3_t origin;

			VectorMA( tr->endpos, halfHeight * 0.45f, along, origin );	// head at t = 0.275 of the texture
			k = rand() % look->numSlants;
			CG_Gore_ImpactMarkAxis( look->slant[k], look->slantWet[k], origin, n, along,
				radius * 0.55f, halfHeight, life );
			return;
		}
	}

	// on a wall only a drop with enough blood in it runs; small ones leave a spot
	if ( n[2] > -0.35f && n[2] < 0.6f && look->numStreaks && radius > flrand( 4.0f, 9.0f )
		&& !Gore_NearRecentRun( tr->endpos, radius * look->wallScale ) ) {
		// wall: a run that starts at the impact and heads down the surface
		vec3_t down = { 0.0f, 0.0f, -1.0f }, origin;
		float d = DotProduct( down, n );

		VectorMA( down, -d, n, down );
		if ( VectorNormalize( down ) > 0.1f ) {
			float height;

			radius *= look->wallScale;
			height = radius * flrand( 1.6f, 2.6f );
			VectorMA( tr->endpos, height * 0.6f, down, origin );	// impact at t = 0.2 of the texture
			Gore_RememberRun( tr->endpos, radius );
			k = rand() % look->numStreaks;
			CG_Gore_ImpactMarkAxis( look->streak[k], look->streakWet[k], origin, n, down,
				radius, height, life );
			return;
		}
	}

	if ( n[2] > -0.35f && n[2] < 0.6f && look->numSlants ) {
		// a wall spot: a small drop that sagged a little before it set, not a floor pool
		vec3_t down = { 0.0f, 0.0f, -1.0f }, origin;

		VectorMA( down, -DotProduct( down, n ), n, down );
		if ( VectorNormalize( down ) > 0.1f ) {
			float halfHeight = radius * 0.7f;

			VectorMA( tr->endpos, halfHeight * 0.45f, down, origin );
			k = rand() % look->numSlants;
			CG_Gore_ImpactMarkAxis( look->slant[k], look->slantWet[k], origin, n, down,
				radius * 0.4f, halfHeight, life );
			return;
		}
	}

	k = rand() % look->numSplats;
	CG_Gore_ImpactMarkAxis( look->splat[k], look->splatWet[k], tr->endpos, n, NULL,
		radius, radius, life );
}

/*
===============
CG_Gore_AddDrops

Move drops, land them, and draw the ones in flight. Called once per frame.
===============
*/
static void Gore_UpdateStumps( void );

void CG_Gore_AddDrops( void ) {
	int i;
	goreDrop_t *d;
	vec3_t now, vel;
	trace_t tr;
	refEntity_t re;

	Gore_UpdateStumps();
	CG_Gore_AddPools();

	for ( i = 0, d = goreDrops; i < GORE_MAX_DROPS; i++, d++ ) {
		if ( !d->active ) {
			continue;
		}
		if ( cg.time >= d->endTime || cg.time < d->pos.trTime ) {
			d->active = qfalse;
			continue;
		}

		BG_EvaluateTrajectory( &d->pos, cg.time, now );
		CG_Trace( &tr, d->last, NULL, NULL, now, ENTITYNUM_NONE, CONTENTS_SOLID | CONTENTS_WATER );
		if ( tr.startsolid ) {
			d->active = qfalse;
			continue;
		}
		if ( tr.fraction < 1.0f ) {
			BG_EvaluateTrajectoryDelta( &d->pos, cg.time, vel );
			if ( !( tr.contents & CONTENTS_WATER ) ) {
				Gore_LandMark( &tr, vel, d->markRadius );
			}
			d->active = qfalse;
			continue;
		}

		if ( goreDropShader ) {
			vec3_t light, up = { 0.0f, 0.0f, 1.0f };

			CG_Gore_Light( now, up, light, NULL );
			memset( &re, 0, sizeof( re ) );
			re.reType = RT_LINE;
			re.customShader = goreDropShader;
			VectorCopy( now, re.origin );
			VectorCopy( d->last, re.oldorigin );
			re.radius = Com_Clamp( 0.35f, 1.2f, d->markRadius * 0.12f );
			re.shaderRGBA[0] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * goreFresh[0] * light[0] * 1.4f );
			re.shaderRGBA[1] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * goreFresh[1] * light[1] * 1.4f );
			re.shaderRGBA[2] = (byte)Com_Clamp( 0.0f, 255.0f, 255.0f * goreFresh[2] * light[2] * 1.4f );
			re.shaderRGBA[3] = 230;
			trap->R_AddRefEntityToScene( &re );
		}
		VectorCopy( now, d->last );
	}
}

/*
===============
Gore_BodyMark

Blood on the victim's skin where it was hit. dir points into the body.
===============
*/
static void Gore_BodyMark( int entityNum, const vec3_t point, const vec3_t dir, float size ) {
	centity_t *cent = &cg_entities[entityNum];
	vec3_t start, end;

	qhandle_t shader = Gore_Look()->body;

	if ( !shader || !cent->ghoul2 || !cg_ghoul2Marks.integer ) {
		return;
	}
	VectorMA( point, -8.0f, dir, start );
	VectorMA( point, 8.0f, dir, end );
	CG_AddGhoul2Mark( shader, size * flrand( 0.8f, 1.3f ), start, end, entityNum,
		cent->lerpOrigin, cent->lerpAngles[YAW], cent->ghoul2, cent->modelScale,
		CG_Gore_MarkLife() );
}

/*
===============
CG_Gore_SaberHit

EV_SABER_HIT on a person, by attacker's blade. strength is the event's parm (1 normal, 3 moderate,
2 big, 16 special). bladeDir is the event's angles: the reverse of the cut.
===============
*/
void CG_Gore_SaberHit( int attacker, int victim, const vec3_t point, const vec3_t bladeDir, int strength ) {
	vec3_t cut;
	int count;

	if ( !cg_gore.integer || !cg_goreSaber.integer || !Gore_Bleeds( victim ) ) {
		return;
	}
	if ( attacker >= 0 && attacker < MAX_GENTITIES ) {
		goreBladeTime[attacker] = cg.time;		// the blade drips for a while (CG_Gore_Blade)
	}
	VectorScale( bladeDir, -1.0f, cut );
	if ( VectorNormalize( cut ) < 0.001f ) {
		VectorSet( cut, 0.0f, 0.0f, 1.0f );
	}

	switch ( strength ) {
	case 16:	count = 28; break;
	case 2:		count = 18; break;
	case 3:		count = 12; break;
	default:	count = 8; break;
	}
	// the blade flings blood along the cut; a little runs straight down
	Gore_Spray( point, cut, count, 230.0f, 0.55f, 7.0f );
	Gore_Spray( point, cut, count / 3 + 1, 40.0f, 1.0f, 6.0f );
	Gore_BodyMark( victim, point, bladeDir, 5.0f );
}

/*
===============
CG_Gore_MissileHit

EV_MISSILE_HIT on a person. normal is the hit surface's normal, so the shot
travelled along -normal: the exit spray goes on through and splashes what
is behind the target.
===============
*/
void CG_Gore_MissileHit( int victim, int weapon, const vec3_t point, const vec3_t normal ) {
	vec3_t through, back;
	int count = 6;
	float speed = 260.0f, radius = 7.0f, mark = 4.0f;

	// victim -1: a hit on some client the event does not name (disruptor)
	if ( !cg_gore.integer || ( victim >= 0 && !Gore_Bleeds( victim ) ) ) {
		return;
	}

	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
	case WP_BLASTER:
	case WP_TURRET:
		break;
	case WP_DISRUPTOR:
		count = 10; speed = 340.0f;
		break;
	case WP_BOWCASTER:
	case WP_REPEATER:
		count = 8;
		break;
	case WP_FLECHETTE:
		count = 4; radius = 5.0f;
		break;
	case WP_CONCUSSION:
	case WP_ROCKET_LAUNCHER:
	case WP_THERMAL:
		count = 22; speed = 320.0f; radius = 10.0f; mark = 9.0f;
		break;
	case WP_DEMP2:
	case WP_STUN_BATON:
		return;		// electric
	default:
		break;
	}

	VectorScale( normal, -1.0f, through );
	VectorCopy( normal, back );
	Gore_Spray( point, through, count, speed, 0.4f, radius );
	Gore_Spray( point, back, count / 3 + 1, speed * 0.4f, 0.8f, radius * 0.7f );
	if ( victim >= 0 ) {
		Gore_BodyMark( victim, point, through, mark );
	}
}

/*
===============
Gore_Chunks

Lumps of flesh thrown from a body that burst: they tumble, trail drops,
splat where they first land, bounce a little and lie there until they sink
away (LE_FRAGMENT; cg_localents.c calls back on the trail and the landing).
===============
*/
// an explosive death: blood bursts out of the body every way
static void Gore_Burst( const vec3_t org ) {
	vec3_t up = { 0.0f, 0.0f, 1.0f };

	Gore_Spray( org, up, 40, 320.0f, 1.0f, 11.0f );
	Gore_Spray( org, up, 10, 90.0f, 1.0f, 16.0f );
}

static void Gore_Chunks( const vec3_t org, int count ) {
	int i;

	if ( !goreNumChunks || !Gore_Look()->numSplats || Gore_Level() <= 0.0f ) {
		return;
	}
	if ( cg_goreDebug.integer ) {
		Com_Printf( "gore: %d chunks at (%.0f %.0f %.0f)\n", count, org[0], org[1], org[2] );
	}
	for ( i = 0; i < count; i++ ) {
		localEntity_t *le = CG_AllocLocalEntity();
		refEntity_t *re = &le->refEntity;
		vec3_t vel, angles;
		float speed = flrand( 160.0f, 380.0f );

		VectorSet( vel, flrand( -1.0f, 1.0f ), flrand( -1.0f, 1.0f ), flrand( 0.3f, 1.4f ) );
		VectorNormalize( vel );
		VectorMA( org, 4.0f, vel, re->origin );	// from the body's surface, not its middle
		VectorScale( vel, speed, vel );

		le->leType = LE_FRAGMENT;
		le->leFlags = LEF_TUMBLE;
		le->startTime = cg.time;
		le->endTime = cg.time + 12000 + Q_irand( 0, 6000 );
		le->fadeInTime = 0;		// next trail drop (Gore_ChunkTrail)
		le->bounceFactor = 0.25f;	// flesh: a dull bounce, then it stops
		le->leBounceSoundType = LEBS_NONE;
		le->leMarkType = LEMT_BLOOD;

		re->hModel = goreChunk[i < 2 ? goreNumChunks - 1 - i : rand() % goreNumChunks];	// a couple of big ones
		le->pos.trType = TR_GRAVITY;
		le->pos.trTime = cg.time;
		VectorCopy( re->origin, le->pos.trBase );
		VectorCopy( vel, le->pos.trDelta );

		VectorSet( angles, flrand( 0.0f, 360.0f ), flrand( 0.0f, 360.0f ), flrand( 0.0f, 360.0f ) );
		le->angles.trType = TR_LINEAR;
		le->angles.trTime = cg.time;
		VectorCopy( angles, le->angles.trBase );
		VectorSet( le->angles.trDelta, flrand( -540.0f, 540.0f ), flrand( -540.0f, 540.0f ), flrand( -540.0f, 540.0f ) );
		AnglesToAxis( angles, re->axis );
	}
}

/*
===============
CG_Gore_ChunkTrail

A thrown chunk in flight (cg_localents.c CG_AddFragment): a drop now and then.
===============
*/
void CG_Gore_ChunkTrail( localEntity_t *le ) {
	vec3_t dir;

	if ( cg.time < le->fadeInTime ) {
		return;
	}
	le->fadeInTime = cg.time + Q_irand( 50, 110 );
	BG_EvaluateTrajectoryDelta( &le->pos, cg.time, dir );
	VectorScale( dir, -1.0f, dir );
	if ( VectorNormalize( dir ) < 0.001f ) {
		VectorSet( dir, 0.0f, 0.0f, -1.0f );
	}
	Gore_Spray( le->refEntity.origin, dir, 1, 30.0f, 1.0f, 4.0f );
}

/*
===============
CG_Gore_ChunkLand

A thrown chunk's first landing: a splat where it hit, and a few drops.
===============
*/
void CG_Gore_ChunkLand( localEntity_t *le, trace_t *tr ) {
	vec3_t vel;

	BG_EvaluateTrajectoryDelta( &le->pos, cg.time, vel );
	Gore_LandMark( tr, vel, 9.0f );
	Gore_Spray( tr->endpos, tr->plane.normal, 4, 90.0f, 0.8f, 4.0f );
}

/*
===============
CG_Gore_Obituary

Explosive deaths burst the body (players and bots; NPCs have no obituary,
see CG_Gore_Gib).
===============
*/
void CG_Gore_Obituary( int victim, int mod ) {
	centity_t *cent;
	vec3_t org, up = { 0.0f, 0.0f, 1.0f };

	if ( !cg_gore.integer || !Gore_Bleeds( victim ) ) {
		return;
	}
	switch ( mod ) {
	case MOD_REPEATER_ALT_SPLASH:
	case MOD_FLECHETTE_ALT_SPLASH:
	case MOD_ROCKET_SPLASH:
	case MOD_ROCKET_HOMING_SPLASH:
	case MOD_THERMAL_SPLASH:
	case MOD_TRIP_MINE_SPLASH:
	case MOD_TIMED_MINE_SPLASH:
	case MOD_DET_PACK_SPLASH:
	case MOD_CONC:
	case MOD_ROCKET:
	case MOD_ROCKET_HOMING:
	case MOD_THERMAL:
		break;
	default:
		return;
	}
	cent = &cg_entities[victim];
	VectorCopy( cent->lerpOrigin, org );
	org[2] += 8.0f;		// about the chest
	Gore_Burst( org );
}

/*
===============
CG_Gore_Gib

EV_GIB_PLAYER from the server's g_gore: an explosive kill burst the body and
threw `limbs` of its limbs. Meat chunks fly with them. Players also had an
obituary (CG_Gore_Obituary) with the blood burst; NPCs have none, so theirs
comes from here.
===============
*/
void CG_Gore_Gib( int victim, const vec3_t origin, int limbs ) {
	vec3_t org;
	int count;

	if ( !cg_gore.integer || !Gore_Bleeds( victim ) ) {
		return;
	}
	VectorCopy( origin, org );
	org[2] += 8.0f;
	if ( victim >= MAX_CLIENTS ) {
		Gore_Burst( org );
	}
	count = 3 + 2 * Com_Clampi( 1, 4, limbs ) + Q_irand( 0, 2 );
	Gore_Chunks( org, cg_gore.integer >= 2 ? count * 3 / 2 : count );
}

/*
===============
CG_Gore_Severed

qtrue when severed limbs and stumps bleed instead of smoking (stock: a
cauterising puff, which cg_goreSaber 0 keeps).
===============
*/
qboolean CG_Gore_Severed( void ) {
	return ( cg_gore.integer && cg_goreSaber.integer && Gore_Look()->numSplats ) ? qtrue : qfalse;
}

/*
===============
CG_Gore_LimbCut

A limb came off: a burst from its cut end, and the trail clock starts.
dir points out of the cut.
===============
*/
void CG_Gore_LimbCut( int limbEnt, const vec3_t org, const vec3_t dir ) {
	if ( limbEnt < 0 || limbEnt >= MAX_GENTITIES ) {
		return;
	}
	goreLimbStart[limbEnt] = cg.time;
	Gore_Spray( org, dir, 8, 140.0f, 0.6f, 6.0f );
}

/*
===============
CG_Gore_LimbTrail

Called while a severed limb flies (cg_ents.c): drops from its cut end.
===============
*/
void CG_Gore_LimbTrail( int limbEnt, const vec3_t org, const vec3_t dir ) {
	int age;

	if ( limbEnt < 0 || limbEnt >= MAX_GENTITIES || !goreLimbStart[limbEnt] ) {
		return;
	}
	age = cg.time - goreLimbStart[limbEnt];
	if ( age < 0 || age > GORE_LIMB_TRAIL ) {
		return;
	}
	Gore_Spray( org, dir, 1, 35.0f, 1.0f, 3.5f + 2.5f * ( 1.0f - (float)age / GORE_LIMB_TRAIL ) );
}

/*
===============
CG_Gore_Stump

The body side of a cut: a burst, a blood mark on the stump, then pulses
that weaken over GORE_STUMP_LIFE (Gore_UpdateStumps). bolt is the stump
cap's bolt on the owner's ghoul2; dir points out of the stump.
===============
*/
void CG_Gore_Stump( int owner, int bolt, const vec3_t org, const vec3_t dir ) {
	int i, oldest = 0;
	vec3_t into;

	if ( !Gore_Bleeds( owner ) ) {
		return;
	}
	Gore_Spray( org, dir, 16, 190.0f, 0.45f, 7.0f );
	VectorScale( dir, -1.0f, into );
	Gore_BodyMark( owner, org, into, 7.0f );
	if ( bolt < 0 ) {
		return;
	}
	for ( i = 0; i < GORE_STUMPS; i++ ) {
		if ( !goreStumps[i].start ) {
			break;
		}
		if ( goreStumps[i].start < goreStumps[oldest].start ) {
			oldest = i;
		}
	}
	if ( i == GORE_STUMPS ) {
		i = oldest;
	}
	goreStumps[i].owner = owner;
	goreStumps[i].bolt = bolt;
	goreStumps[i].start = cg.time;
	goreStumps[i].nextPulse = cg.time + 250;
	goreStumps[i].nextDrip = cg.time + 120;
}

// pulses like a heartbeat that slows and weakens, with a dribble between
static void Gore_UpdateStumps( void ) {
	int i;

	for ( i = 0; i < GORE_STUMPS; i++ ) {
		goreStump_t *st = &goreStumps[i];
		centity_t *cent;
		mdxaBone_t m;
		vec3_t org, dir;
		float left;
		int age;

		if ( !st->start ) {
			continue;
		}
		age = cg.time - st->start;
		cent = &cg_entities[st->owner];
		if ( age < 0 || age > GORE_STUMP_LIFE || !cent->ghoul2 || !( cent->currentState.eFlags & EF_DEAD ) ) {
			st->start = 0;
			continue;
		}
		if ( cg.time < st->nextPulse && cg.time < st->nextDrip ) {
			continue;
		}
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, st->bolt, &m, cent->lerpAngles, cent->lerpOrigin,
			cg.time, cgs.gameModels, cent->modelScale );
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, org );
		BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, dir );
		if ( VectorNormalize( dir ) < 0.001f ) {
			VectorSet( dir, 0.0f, 0.0f, 1.0f );
		}
		left = 1.0f - (float)age / GORE_STUMP_LIFE;
		if ( cg.time >= st->nextPulse ) {
			float strength = left * left;
			Gore_Spray( org, dir, 2 + (int)( 6.0f * strength ), 40.0f + 170.0f * strength, 0.3f, 3.5f + 3.0f * strength );
			st->nextPulse = cg.time + 420 + (int)( 300.0f * ( 1.0f - left ) );
		}
		if ( cg.time >= st->nextDrip ) {
			Gore_Spray( org, dir, 1, 15.0f, 1.0f, 3.0f );
			st->nextDrip = cg.time + 140 + (int)( 200.0f * ( 1.0f - left ) );
		}
	}
}

/*
===============
CG_Gore_Blade

Each drawn saber blade, each frame (cg_players.c CG_AddSaberBlade): for a few
seconds after it cut someone, drops fall off along its length, and a hard
swing flings them off the tip. base and tip are the blade's ends.
===============
*/
static void Gore_Along( const vec3_t a, const vec3_t b, float f, vec3_t out ) {
	out[0] = a[0] + f * ( b[0] - a[0] );
	out[1] = a[1] + f * ( b[1] - a[1] );
	out[2] = a[2] + f * ( b[2] - a[2] );
}

void CG_Gore_Blade( int owner, int saberNum, int bladeNum, const vec3_t base, const vec3_t tip ) {
	vec3_t point, vel, down = { 0.0f, 0.0f, -1.0f };
	float left, swing = 0.0f;
	int age;

	if ( owner < 0 || owner >= MAX_GENTITIES || !goreBladeTime[owner] ) {
		return;
	}
	age = cg.time - goreBladeTime[owner];
	if ( age < 0 || age > GORE_BLADE_LIFE || !cg_gore.integer || !cg_goreSaber.integer ) {
		goreBladeTime[owner] = 0;
		return;
	}
	left = 1.0f - (float)age / GORE_BLADE_LIFE;

	if ( bladeNum == 0 && saberNum >= 0 && saberNum < 2 ) {
		// how fast the tip moves: a swing throws blood off the end
		int dt = cg.time - goreBladeTipTime[owner][saberNum];

		if ( dt > 0 && dt < 100 ) {
			VectorSubtract( tip, goreBladeTip[owner][saberNum], vel );
			VectorScale( vel, 1000.0f / dt, vel );
			swing = VectorLength( vel );
		}
		VectorCopy( tip, goreBladeTip[owner][saberNum] );
		goreBladeTipTime[owner][saberNum] = cg.time;
		if ( swing > 600.0f && rand() % 1000 < (int)( 1000.0f * left * left ) ) {
			VectorNormalize( vel );
			Gore_Along( base, tip, flrand( 0.6f, 1.0f ), point );
			Gore_Spray( point, vel, 1, Com_Clamp( 150.0f, 320.0f, swing * 0.3f ), 0.25f, 3.0f + 3.0f * left );
		}
	}

	if ( cg.time < goreBladeNext[owner] ) {
		return;
	}
	// drips slow as the blade runs dry; dual and staff blades share one clock
	goreBladeNext[owner] = cg.time + 70 + (int)( 380.0f * ( 1.0f - left ) );
	Gore_Along( base, tip, flrand( 0.2f, 1.0f ), point );
	Gore_Spray( point, down, left > 0.6f ? 2 : 1, 12.0f, 0.6f, 3.5f + 3.5f * left );
}

/*
===============
CG_Gore_Footstep

A footstep (cg_players.c): a foot that lands in fresh floor blood picks some
up, and the next GORE_PRINT_STEPS steps print it, fainter each time. origin
is a little above the foot; yaw is the legs' heading.
===============
*/
void CG_Gore_Footstep( int entityNum, const vec3_t origin, float yaw, qboolean right ) {
	vec3_t end, fwd;
	trace_t tr;
	int i, load;

	if ( !cg_gore.integer || entityNum < 0 || entityNum >= MAX_GENTITIES || !gorePrint[right][0] ) {
		return;
	}
	VectorCopy( origin, end );
	end[2] -= 48.0f;
	CG_Trace( &tr, origin, NULL, NULL, end, entityNum, MASK_SOLID );
	if ( tr.fraction == 1.0f || tr.startsolid || tr.plane.normal[2] < 0.7f
		|| ( tr.surfaceFlags & ( SURF_SKY | SURF_NOIMPACT | SURF_NOMARKS ) ) ) {
		return;
	}

	for ( i = 0; i < GORE_FLOOR_SPOTS; i++ ) {
		const goreRun_t *f = &goreFloor[i];
		float reach = f->radius + 3.0f, dx, dy;

		if ( !f->time || cg.time < f->time || cg.time - f->time > GORE_FLOOR_FRESH
			|| fabs( f->origin[2] - tr.endpos[2] ) > 10.0f ) {
			continue;
		}
		dx = f->origin[0] - tr.endpos[0];
		dy = f->origin[1] - tr.endpos[1];
		if ( dx * dx + dy * dy < reach * reach ) {
			// standing in it: the sole loads up (no print, it is in the blood already)
			goreFeetSteps[entityNum] = cg_gore.integer >= 2 ? GORE_PRINT_STEPS * 3 / 2 : GORE_PRINT_STEPS;
			return;
		}
	}
	if ( goreFeetSteps[entityNum] <= 0 ) {
		return;
	}

	// fresh prints for the first third of the steps, then thinner, then patchy
	load = ( GORE_PRINT_STEPS - Com_Clampi( 1, GORE_PRINT_STEPS, goreFeetSteps[entityNum] ) ) * GORE_PRINTS / GORE_PRINT_STEPS;
	goreFeetSteps[entityNum]--;

	VectorSet( fwd, cos( DEG2RAD( yaw ) ), sin( DEG2RAD( yaw ) ), 0.0f );
	VectorMA( fwd, -DotProduct( fwd, tr.plane.normal ), tr.plane.normal, fwd );
	if ( VectorNormalize( fwd ) < 0.1f ) {
		return;
	}
	VectorMA( tr.endpos, 3.0f, fwd, end );		// the bolt is the ankle; the print reaches forward of it
	{
		// feet apart: some models' foot bolts come back at the body's centre line
		centity_t *cent = &cg_entities[entityNum];
		vec3_t side, rel;
		float off;

		CrossProduct( fwd, tr.plane.normal, side );		// to the right
		VectorSubtract( end, cent->lerpOrigin, rel );
		off = DotProduct( rel, side );
		if ( fabs( off ) < 2.5f ) {
			VectorMA( end, ( right ? 4.0f : -4.0f ) - off, side, end );
		}
	}
	if ( cg_goreDebug.integer ) {
		Com_Printf( "gore: print %s load %d at (%.0f %.0f %.0f), %d left\n", right ? "r" : "l", load,
			end[0], end[1], end[2], goreFeetSteps[entityNum] );
	}
	CG_Gore_ImpactMarkAxis( gorePrint[right][load], 0, end, tr.plane.normal, fwd, 3.3f, 7.0f, CG_Gore_MarkLife() );
}

/*
===============
CG_Gore_Spray_f

gore_spray [count]: with cg_goreDebug 1, throw blood at the surface in view
from 40 units in front of it, for looking at marks without a fight.
===============
*/
void CG_Gore_Spray_f( void ) {
	trace_t tr;
	vec3_t end, org, into;
	int count = 12;

	if ( !cg_goreDebug.integer ) {
		Com_Printf( "gore_spray needs cg_goreDebug 1\n" );
		return;
	}
	if ( trap->Cmd_Argc() > 1 ) {
		count = Com_Clampi( 1, 200, atoi( CG_Argv( 1 ) ) );
	}
	VectorMA( cg.refdef.vieworg, 8192.0f, cg.refdef.viewaxis[0], end );
	CG_Trace( &tr, cg.refdef.vieworg, NULL, NULL, end, cg.predictedPlayerState.clientNum, CONTENTS_SOLID );
	if ( tr.fraction >= 1.0f ) {
		Com_Printf( "gore_spray: nothing in view\n" );
		return;
	}
	VectorMA( tr.endpos, 40.0f, tr.plane.normal, org );
	VectorScale( tr.plane.normal, -1.0f, into );
	Gore_Spray( org, into, count, 380.0f, 0.35f, 8.0f );
	if ( trap->Cmd_Argc() > 2 ) {	// gore_spray <drops> <chunks>
		Gore_Chunks( org, Com_Clampi( 0, 40, atoi( CG_Argv( 2 ) ) ) );
	}
}
