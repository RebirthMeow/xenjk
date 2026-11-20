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

#include "tr_local.h"
#include <vector>
#include <queue>
#include <functional>

/*
r_smoke: a look-only prototype of CS2-style volumetric smoke (no gameplay, no network, no bots). The console
command `smoke [seconds [x y z]]` drops one where the view points (or above x y z): it settles on the floor below the hit and floods
SMOKE_CELL-unit cells outward (a Dijkstra fill with a cell budget, cheaper downhill than up, stopped by solid
brushes in the collision map), so it pours along corridors and through doorways instead of staying a sphere.
The cells go up as a GX x (GY x GZ) RGBA8 atlas (one image per slot); smoke.frag raymarches each volume after the
world's MSAA resolve against the depth extract and composites it premultiplied. Keep the constants in step with
smoke.frag.
*/

#define SMOKE_GX			64
#define SMOKE_GY			64
#define SMOKE_GZ			24
#define SMOKE_CELL			16.0f
#define SMOKE_EDGE_CELLS	8		// g: cells to the nearest open untaken cell, saturating here
#define SMOKE_RAY_CELLS		10		// b, a: occupied cells toward the light / straight up, out of this many
#define SMOKE_SEED_Z		4		// the seed's cell row: room below for stairs, the rest above
#define SMOKE_MAX			8
#define SMOKE_CELLS			( SMOKE_GX * SMOKE_GY * SMOKE_GZ )

typedef struct {
	qboolean	active;
	int			startTime;		// refdef ms
	float		life;			// s
	vec3_t		origin;			// world corner of cell (0,0,0)
	vec3_t		mins, maxs;		// occupied cells' bounds, one cell of margin
	uint32_t	ambient, sun;	// packed unorm rgb, a = intensity / 4
	vec3_t		lightDir;		// toward the light
	image_t		*image;
} smoke_t;

// smoke.frag's push constants
typedef struct {
	float		eye[4];
	float		ray00[4];
	float		rayDX[4];
	float		rayDY[4];
	float		grid[4];
	float		boxMin[3];
	uint32_t	ambient;
	float		boxMax[3];
	uint32_t	sun;
	float		light[4];
} vkSmokePush_t;

static smoke_t		smokes[SMOKE_MAX];

// the last main world view, for the console command (it runs between frames)
static qboolean		smokeViewValid;
static vec3_t		smokeViewOrigin;
static vec3_t		smokeViewForward;
static int			smokeViewTime;

cvar_t	*r_smoke;
cvar_t	*r_smokeDensity;
cvar_t	*r_smokeLife;
cvar_t	*r_smokeCells;
cvar_t	*r_smokeGrow;
cvar_t	*r_smokeFade;
cvar_t	*r_smokeLight;

static inline int SmokeIndex( int x, int y, int z )
{
	return ( z * SMOKE_GY + y ) * SMOKE_GX + x;
}

static void SmokeCellCenter( const smoke_t *s, int x, int y, int z, vec3_t out )
{
	out[0] = s->origin[0] + ( x + 0.5f ) * SMOKE_CELL;
	out[1] = s->origin[1] + ( y + 0.5f ) * SMOKE_CELL;
	out[2] = s->origin[2] + ( z + 0.5f ) * SMOKE_CELL;
}

static qboolean SmokeBlocked( const vec3_t a, const vec3_t b )
{
	trace_t tr;
	ri.CM_BoxTrace( &tr, a, b, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0 );
	return ( tr.fraction < 1.0f || tr.startsolid || tr.allsolid ) ? qtrue : qfalse;
}

static uint32_t SmokePackColor( const vec3_t c )
{
	const float m = MAX( MAX( c[0], c[1] ), c[2] );
	if ( m <= 0.0f )
		return 0;
	const float a = Com_Clamp( 0.0f, 1.0f, m / 4.0f );
	uint32_t out = 0;
	for ( int i = 0; i < 3; i++ )
		out |= (uint32_t)( Com_Clamp( 0.0f, 1.0f, c[i] / m ) * 255.0f + 0.5f ) << ( i * 8 );
	return out | ( (uint32_t)( a * 255.0f + 0.5f ) << 24 );
}

static void SmokeLighting( smoke_t *s, const vec3_t seed )
{
	vec3_t p, amb, dir, ldir;
	VectorSet( p, seed[0], seed[1], seed[2] + 32.0f );
	if ( !R_LightForPoint( p, amb, dir, ldir ) ) {
		VectorSet( amb, 96.0f, 96.0f, 96.0f );
		VectorSet( dir, 128.0f, 124.0f, 118.0f );
		VectorSet( ldir, 0.3f, 0.2f, 0.93f );
	}
	if ( VectorNormalize( ldir ) < 0.001f )
		VectorSet( ldir, 0.0f, 0.0f, 1.0f );
	const float scale = r_smokeLight->value / 255.0f;
	VectorScale( amb, scale, amb );
	VectorScale( dir, scale, dir );
	s->ambient = SmokePackColor( amb );
	s->sun = SmokePackColor( dir );
	VectorCopy( ldir, s->lightDir );
}

/*
The fill: Dijkstra over the 26 neighbours from the seed cell, a step costing its length with the vertical part
weighted (up 1.5, down 0.75: the smoke settles and spreads before it climbs), until r_smokeCells cells are taken.
A step needs the neighbour's centre outside solid brushes and a clear point trace between the centres. The
grid's outer cells stay empty, so the shader's clamped samples read nothing there.
*/
static qboolean SmokeBuild( smoke_t *s, const vec3_t seed, float life, int slot )
{
	const int t0 = ri.Milliseconds();
	const int budget = (int)Com_Clamp( 64.0f, 20000.0f, (float)r_smokeCells->integer );

	s->origin[0] = floorf( seed[0] / SMOKE_CELL ) * SMOKE_CELL - ( SMOKE_GX / 2 ) * SMOKE_CELL;
	s->origin[1] = floorf( seed[1] / SMOKE_CELL ) * SMOKE_CELL - ( SMOKE_GY / 2 ) * SMOKE_CELL;
	s->origin[2] = floorf( seed[2] / SMOKE_CELL ) * SMOKE_CELL - SMOKE_SEED_Z * SMOKE_CELL;

	std::vector<float> dist( SMOKE_CELLS, 1e9f );
	std::vector<byte> state( SMOKE_CELLS, 0 );	// 0 unknown, 1 open, 2 solid
	std::vector<byte> taken( SMOKE_CELLS, 0 );

	auto open = [&]( int x, int y, int z ) -> qboolean {
		const int idx = SmokeIndex( x, y, z );
		if ( !state[idx] ) {
			vec3_t c;
			SmokeCellCenter( s, x, y, z, c );
			state[idx] = ( ri.CM_PointContents( c, 0 ) & CONTENTS_SOLID ) ? 2 : 1;
		}
		return state[idx] == 1 ? qtrue : qfalse;
	};

	int sx = SMOKE_GX / 2, sy = SMOKE_GY / 2, sz = SMOKE_SEED_Z;
	while ( sz < SMOKE_GZ - 2 && !open( sx, sy, sz ) )
		sz++;
	if ( !open( sx, sy, sz ) ) {
		ri.Printf( PRINT_ALL, "smoke: no open cell at the landing point\n" );
		return qfalse;
	}

	typedef std::pair<float, int> entry_t;
	std::priority_queue<entry_t, std::vector<entry_t>, std::greater<entry_t> > heap;
	const int seedIdx = SmokeIndex( sx, sy, sz );
	dist[seedIdx] = 0.0f;
	heap.push( entry_t( 0.0f, seedIdx ) );

	int count = 0, traces = 0;
	float maxD = 0.0f;
	int lo[3] = { SMOKE_GX, SMOKE_GY, SMOKE_GZ }, hi[3] = { -1, -1, -1 };
	while ( !heap.empty() && count < budget ) {
		const entry_t e = heap.top();
		heap.pop();
		const int idx = e.second;
		if ( taken[idx] || e.first > dist[idx] )
			continue;
		taken[idx] = 1;
		count++;
		maxD = MAX( maxD, e.first );

		const int x = idx % SMOKE_GX, y = ( idx / SMOKE_GX ) % SMOKE_GY, z = idx / ( SMOKE_GX * SMOKE_GY );
		lo[0] = MIN( lo[0], x ); lo[1] = MIN( lo[1], y ); lo[2] = MIN( lo[2], z );
		hi[0] = MAX( hi[0], x ); hi[1] = MAX( hi[1], y ); hi[2] = MAX( hi[2], z );

		vec3_t from;
		SmokeCellCenter( s, x, y, z, from );
		for ( int dz = -1; dz <= 1; dz++ ) {
			for ( int dy = -1; dy <= 1; dy++ ) {
				for ( int dx = -1; dx <= 1; dx++ ) {
					if ( !dx && !dy && !dz )
						continue;
					const int nx = x + dx, ny = y + dy, nz = z + dz;
					if ( nx < 1 || ny < 1 || nz < 1 || nx >= SMOKE_GX - 1 || ny >= SMOKE_GY - 1 || nz >= SMOKE_GZ - 1 )
						continue;
					const int n = SmokeIndex( nx, ny, nz );
					if ( taken[n] )
						continue;
					const float vz = dz > 0 ? 1.5f : ( dz < 0 ? -0.75f : 0.0f );
					const float nd = e.first + sqrtf( (float)( dx * dx + dy * dy ) + vz * vz );
					if ( nd >= dist[n] || !open( nx, ny, nz ) )
						continue;
					vec3_t to;
					SmokeCellCenter( s, nx, ny, nz, to );
					traces++;
					if ( SmokeBlocked( from, to ) )
						continue;
					dist[n] = nd;
					heap.push( entry_t( nd, n ) );
				}
			}
		}
	}
	if ( maxD <= 0.0f )
		maxD = 1.0f;

	SmokeLighting( s, seed );

	// g: chamfer distance (in cells) from every taken cell to the nearest untaken one, by layers
	std::vector<byte> edge( SMOKE_CELLS, 0 );
	std::vector<int> layer, next;
	for ( int i = 0; i < SMOKE_CELLS; i++ ) {
		if ( !taken[i] )
			continue;
		const int x = i % SMOKE_GX, y = ( i / SMOKE_GX ) % SMOKE_GY, z = i / ( SMOKE_GX * SMOKE_GY );
		qboolean border = qfalse;
		for ( int dz = -1; dz <= 1 && !border; dz++ )
			for ( int dy = -1; dy <= 1 && !border; dy++ )
				for ( int dx = -1; dx <= 1 && !border; dx++ )
					if ( !taken[SmokeIndex( x + dx, y + dy, z + dz )] && state[SmokeIndex( x + dx, y + dy, z + dz )] != 2 )
						border = qtrue;
		if ( border ) {
			edge[i] = 1;
			layer.push_back( i );
		}
	}
	for ( int d = 2; d <= SMOKE_EDGE_CELLS && !layer.empty(); d++ ) {
		next.clear();
		for ( size_t k = 0; k < layer.size(); k++ ) {
			const int i = layer[k];
			const int x = i % SMOKE_GX, y = ( i / SMOKE_GX ) % SMOKE_GY, z = i / ( SMOKE_GX * SMOKE_GY );
			for ( int dz = -1; dz <= 1; dz++ )
				for ( int dy = -1; dy <= 1; dy++ )
					for ( int dx = -1; dx <= 1; dx++ ) {
						const int n = SmokeIndex( x + dx, y + dy, z + dz );
						if ( taken[n] && !edge[n] ) {
							edge[n] = (byte)d;
							next.push_back( n );
						}
					}
		}
		layer.swap( next );
	}

	// the atlas: row z * GY + y, column x
	std::vector<byte> pixels( SMOKE_CELLS * 4, 0 );
	for ( int i = 0; i < SMOKE_CELLS; i++ )
		pixels[i * 4 + 0] = 255;
	for ( int i = 0; i < SMOKE_CELLS; i++ ) {
		if ( !taken[i] )
			continue;
		const int x = i % SMOKE_GX, y = ( i / SMOKE_GX ) % SMOKE_GY, z = i / ( SMOKE_GX * SMOKE_GY );
		vec3_t c;
		SmokeCellCenter( s, x, y, z, c );
		int towardLight = 0, above = 0;
		for ( int k = 1; k <= SMOKE_RAY_CELLS; k++ ) {
			vec3_t q;
			VectorMA( c, k * SMOKE_CELL, s->lightDir, q );
			const int qx = (int)floorf( ( q[0] - s->origin[0] ) / SMOKE_CELL );
			const int qy = (int)floorf( ( q[1] - s->origin[1] ) / SMOKE_CELL );
			const int qz = (int)floorf( ( q[2] - s->origin[2] ) / SMOKE_CELL );
			if ( qx >= 0 && qy >= 0 && qz >= 0 && qx < SMOKE_GX && qy < SMOKE_GY && qz < SMOKE_GZ && taken[SmokeIndex( qx, qy, qz )] )
				towardLight++;
			if ( z + k < SMOKE_GZ && taken[SmokeIndex( x, y, z + k )] )
				above++;
		}
		byte *px = &pixels[i * 4];
		px[0] = (byte)( dist[i] / maxD * 255.0f + 0.5f );
		px[1] = (byte)( MIN( edge[i] ? edge[i] : SMOKE_EDGE_CELLS, SMOKE_EDGE_CELLS ) * 255 / SMOKE_EDGE_CELLS );
		px[2] = (byte)( towardLight * 255 / SMOKE_RAY_CELLS );
		px[3] = (byte)( above * 255 / SMOKE_RAY_CELLS );
	}
	// untaken cells beside the volume take their earliest neighbour's arrival and lighting, so filtering across
	// the surface does not blend in "never arrives" or "unlit"; g stays 0 there, except in solid cells, which take
	// their deepest neighbour's g so the smoke stays dense down to the floor and against walls
	for ( int i = 0; i < SMOKE_CELLS; i++ ) {
		if ( taken[i] )
			continue;
		const int x = i % SMOKE_GX, y = ( i / SMOKE_GX ) % SMOKE_GY, z = i / ( SMOKE_GX * SMOKE_GY );
		if ( x < 1 || y < 1 || z < 1 || x >= SMOKE_GX - 1 || y >= SMOKE_GY - 1 || z >= SMOKE_GZ - 1 )
			continue;
		int best = -1;
		byte deepest = 0;
		for ( int dz = -1; dz <= 1; dz++ )
			for ( int dy = -1; dy <= 1; dy++ )
				for ( int dx = -1; dx <= 1; dx++ ) {
					const int n = SmokeIndex( x + dx, y + dy, z + dz );
					if ( !taken[n] )
						continue;
					if ( best < 0 || pixels[n * 4] < pixels[best * 4] )
						best = n;
					deepest = MAX( deepest, pixels[n * 4 + 1] );
				}
		if ( best >= 0 ) {
			pixels[i * 4 + 0] = pixels[best * 4 + 0];
			pixels[i * 4 + 2] = pixels[best * 4 + 2];
			pixels[i * 4 + 3] = pixels[best * 4 + 3];
			if ( state[i] == 2 )
				pixels[i * 4 + 1] = deepest;
		}
	}

	for ( int k = 0; k < 3; k++ ) {
		s->mins[k] = s->origin[k] + ( lo[k] - 1 ) * SMOKE_CELL;
		s->maxs[k] = s->origin[k] + ( hi[k] + 2 ) * SMOKE_CELL;
	}

	const int bytes = SMOKE_CELLS * 4;
	if ( !s->image ) {
		s->image = R_CreateImage( va( "*smoke%i", slot ), pixels.data(), SMOKE_GX, SMOKE_GY * SMOKE_GZ,
			IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_LIGHTMAP | IMGFLAG_NOSCALE | IMGFLAG_CLAMPTOEDGE,
			VK_FORMAT_R8G8B8A8_UNORM, 0 );
	} else {
		vk_upload_image_data( s->image, 0, 0, SMOKE_GX, SMOKE_GY * SMOKE_GZ, 1, pixels.data(), bytes, qtrue );
	}

	s->life = life;
	s->startTime = smokeViewTime;
	s->active = qtrue;

	ri.Printf( PRINT_ALL, "smoke %i: %i cells (%.0f cubic units), %i x %i x %i cell span, %i traces, %i ms\n", slot, count,
		count * SMOKE_CELL * SMOKE_CELL * SMOKE_CELL, hi[0] - lo[0] + 1, hi[1] - lo[1] + 1, hi[2] - lo[2] + 1, traces,
		ri.Milliseconds() - t0 );
	return qtrue;
}

static void R_Smoke_f( void )
{
	if ( !tr.world || !smokeViewValid ) {
		ri.Printf( PRINT_ALL, "smoke: no world view yet\n" );
		return;
	}
	if ( !vk.msaaActive || !vk.depth.extract.enabled || !vk.smoke_pipeline_1x ) {
		ri.Printf( PRINT_ALL, "smoke: needs MSAA (r_ext_multisample) and r_ssao 1 (the depth extract)\n" );
		return;
	}

	// the view's hit (or the given point), then the floor below it: where a thrown canister would come to rest
	trace_t tr;
	vec3_t end, p, down;
	if ( ri.Cmd_Argc() >= 5 ) {
		VectorSet( p, atof( ri.Cmd_Argv( 2 ) ), atof( ri.Cmd_Argv( 3 ) ), atof( ri.Cmd_Argv( 4 ) ) );
	} else {
		VectorMA( smokeViewOrigin, 8192.0f, smokeViewForward, end );
		ri.CM_BoxTrace( &tr, smokeViewOrigin, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0 );
		if ( tr.fraction < 1.0f )
			VectorMA( tr.endpos, 12.0f, tr.plane.normal, p );
		else
			VectorCopy( tr.endpos, p );
	}
	VectorSet( down, p[0], p[1], p[2] - 2048.0f );
	ri.CM_BoxTrace( &tr, p, down, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0 );
	vec3_t seed;
	VectorSet( seed, tr.endpos[0], tr.endpos[1], tr.endpos[2] + 20.0f );

	const float life = ri.Cmd_Argc() > 1 ? atof( ri.Cmd_Argv( 1 ) ) : r_smokeLife->value;

	// a free slot, else the one closest to its end
	int slot = 0;
	float bestLeft = 1e9f;
	for ( int i = 0; i < SMOKE_MAX; i++ ) {
		if ( !smokes[i].active ) {
			slot = i;
			break;
		}
		const float left = smokes[i].life - ( smokeViewTime - smokes[i].startTime ) * 0.001f;
		if ( left < bestLeft ) {
			bestLeft = left;
			slot = i;
		}
	}
	SmokeBuild( &smokes[slot], seed, life > 0.5f ? life : 0.5f, slot );
}

static void R_SmokeClear_f( void )
{
	for ( int i = 0; i < SMOKE_MAX; i++ )
		smokes[i].active = qfalse;
}

void R_SmokeRegister( void )
{
	r_smoke				= ri.Cvar_Get( "r_smoke",			"1",	CVAR_NONE, "xenjk prototype: draw volumetric smoke spawned with the `smoke [seconds [x y z]]` command (needs MSAA and r_ssao 1); `smoke_clear` removes all. Look only: no gameplay, network or bots" );
	r_smokeDensity		= ri.Cvar_Get( "r_smokeDensity",	"0.05",	CVAR_NONE, "Smoke extinction per unit inside the volume (0.05: ~1% of the background left after 90 units)" );
	r_smokeLife			= ri.Cvar_Get( "r_smokeLife",		"18",	CVAR_NONE, "Seconds a smoke lasts when `smoke` has no argument" );
	r_smokeCells		= ri.Cvar_Get( "r_smokeCells",		"3200",	CVAR_NONE, "Smoke volume budget in 16-unit cells for the next `smoke` (3200 = about a 290-unit-wide dome in the open)" );
	r_smokeGrow			= ri.Cvar_Get( "r_smokeGrow",		"1.2",	CVAR_NONE, "Seconds a smoke takes to reach its full extent" );
	r_smokeFade			= ri.Cvar_Get( "r_smokeFade",		"3",	CVAR_NONE, "Seconds a smoke takes to thin out at the end of its life" );
	r_smokeLight		= ri.Cvar_Get( "r_smokeLight",		"1",	CVAR_NONE, "Scale on the light grid's ambient and directed light for the next `smoke`" );

	ri.Cmd_AddCommand( "smoke", R_Smoke_f, "xenjk prototype: drop a volumetric smoke where the view points (or above x y z); optional lifetime in seconds" );
	ri.Cmd_AddCommand( "smoke_clear", R_SmokeClear_f, "Remove every smoke" );
}

// RE_Shutdown frees every image; R_Init starts again with none
void R_SmokeShutdown( void )
{
	ri.Cmd_RemoveCommand( "smoke" );
	ri.Cmd_RemoveCommand( "smoke_clear" );
	Com_Memset( smokes, 0, sizeof( smokes ) );
	smokeViewValid = qfalse;
}

qboolean vk_smoke_wanted( void )
{
	if ( !r_smoke || !r_smoke->integer || !vk.smoke_pipeline_1x )
		return qfalse;
	for ( int i = 0; i < SMOKE_MAX; i++ )
		if ( smokes[i].active )
			return qtrue;
	return qfalse;
}

/*
In vk_one_resolve_pass's post 1x pass of the main world view, after the SSAO and glow blends. Each volume is a
full-screen triangle strip scissored to its bounds' screen rectangle, drawn farthest first.
*/
void vk_smoke_draw( void )
{
	const viewParms_t *vp = &backEnd.viewParms;

	VectorCopy( vp->ori.origin, smokeViewOrigin );
	VectorCopy( vp->ori.axis[0], smokeViewForward );
	smokeViewTime = backEnd.refdef.time;
	smokeViewValid = qtrue;

	if ( !vk_smoke_wanted() || !vk.depth.extract.enabled )
		return;

	const int width = glConfig.vidWidth, height = glConfig.vidHeight;
	int order[SMOKE_MAX], n = 0;
	float farDist[SMOKE_MAX];
	for ( int i = 0; i < SMOKE_MAX; i++ ) {
		smoke_t *s = &smokes[i];
		if ( !s->active || !s->image )
			continue;
		const float age = ( backEnd.refdef.time - s->startTime ) * 0.001f;
		if ( age < 0.0f || age > s->life ) {
			s->active = qfalse;		// over, or the clock went back (map restart, demo seek)
			continue;
		}
		vec3_t c;
		VectorAdd( s->mins, s->maxs, c );
		VectorScale( c, 0.5f, c );
		farDist[i] = DistanceSquared( c, vp->ori.origin );
		int k = n++;
		while ( k > 0 && farDist[order[k - 1]] < farDist[i] ) {
			order[k] = order[k - 1];
			k--;
		}
		order[k] = i;
	}
	if ( !n )
		return;

	const float tanX = tanf( DEG2RAD( vp->fovX * 0.5f ) ), tanY = tanf( DEG2RAD( vp->fovY * 0.5f ) );
	const float *fwd = vp->ori.axis[0], *left = vp->ori.axis[1], *up = vp->ori.axis[2];

	vkSmokePush_t push;
	Com_Memset( &push, 0, sizeof( push ) );
	for ( int k = 0; k < 3; k++ ) {
		push.eye[k] = vp->ori.origin[k];
		push.ray00[k] = fwd[k] + tanX * left[k] + tanY * up[k];
		push.rayDX[k] = -2.0f * tanX * left[k];
		push.rayDY[k] = -2.0f * tanY * up[k];
	}
	push.rayDX[3] = MAX( 0.05f, r_smokeGrow->value );
	push.rayDY[3] = MAX( 0.0f, r_smokeDensity->value );
	push.grid[3] = (float)( tr.frameCount & 1023 );
	push.light[3] = MAX( 0.05f, r_smokeFade->value );

	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.smoke_pipeline_1x );
	vk_set_viewport_scissor( width, height );

	for ( int j = 0; j < n; j++ ) {
		const smoke_t *s = &smokes[order[j]];

		// the bounds' screen rectangle (uv = 0.5 - 0.5 * side / (depth * tan)); whole screen once a corner is behind
		int x0 = 0, y0 = 0, x1 = width, y1 = height;
		float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
		qboolean behind = qfalse;
		for ( int c = 0; c < 8 && !behind; c++ ) {
			vec3_t corner, d;
			VectorSet( corner, ( c & 1 ) ? s->maxs[0] : s->mins[0], ( c & 2 ) ? s->maxs[1] : s->mins[1], ( c & 4 ) ? s->maxs[2] : s->mins[2] );
			VectorSubtract( corner, vp->ori.origin, d );
			const float z = DotProduct( d, fwd );
			if ( z < 1.0f ) {
				behind = qtrue;
				break;
			}
			const float u = 0.5f - 0.5f * DotProduct( d, left ) / ( z * tanX );
			const float v = 0.5f - 0.5f * DotProduct( d, up ) / ( z * tanY );
			u0 = MIN( u0, u ); u1 = MAX( u1, u );
			v0 = MIN( v0, v ); v1 = MAX( v1, v );
		}
		if ( !behind ) {
			x0 = MAX( 0, (int)floorf( u0 * width ) - 2 );
			y0 = MAX( 0, (int)floorf( v0 * height ) - 2 );
			x1 = MIN( width, (int)ceilf( u1 * width ) + 2 );
			y1 = MIN( height, (int)ceilf( v1 * height ) + 2 );
			if ( x1 <= x0 || y1 <= y0 )
				continue;
		}
		VkRect2D scissor;
		scissor.offset.x = x0;
		scissor.offset.y = y0;
		scissor.extent.width = (uint32_t)( x1 - x0 );
		scissor.extent.height = (uint32_t)( y1 - y0 );
		qvkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor );

		VkDescriptorSet sets[2] = { vk.depth.extract.descriptor, s->image->descriptor_set };
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_smoke, 0, 2, sets, 0, NULL );

		push.eye[3] = ( backEnd.refdef.time - s->startTime ) * 0.001f;
		push.ray00[3] = s->life;
		for ( int k = 0; k < 3; k++ ) {
			push.grid[k] = s->origin[k];
			push.boxMin[k] = s->mins[k];
			push.boxMax[k] = s->maxs[k];
			push.light[k] = s->lightDir[k];
		}
		push.ambient = s->ambient;
		push.sun = s->sun;
		qvkCmdPushConstants( vk.cmd->command_buffer, vk.pipeline_layout_smoke, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );

		qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	}

	vk_set_viewport_scissor( width, height );
}
