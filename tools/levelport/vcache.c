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

/* vcache.c -- triangle order for the GPU's post-transform vertex cache (Tom Forsyth's "Linear-Speed Vertex Cache
   Optimisation", 2006): the same triangles, reordered so a vertex's later uses come soon after its first. Built and
   called by vcache.py (ctypes); no other dependency.

   int vcache_optimize(const unsigned *idx, unsigned tris, unsigned verts, unsigned *out)
     idx: tris * 3 indices < verts; out: the reordered indices (same count). Returns 0, or -1 when out of memory or
     an index is not below verts. */
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CACHE 32
#define CACHE_DECAY 1.5f
#define LAST_TRI 0.75f
#define VALENCE_SCALE 2.0f
#define VALENCE_POWER 0.5f

#ifdef _WIN32
#define VCACHE_EXPORT __declspec( dllexport )
#else
#define VCACHE_EXPORT __attribute__(( visibility( "default" ) ))
#endif

static float score_of( int pos, int remaining )
{
	float s = 0.0f;
	if ( remaining == 0 )
		return -1.0f;
	if ( pos >= 0 ) {
		if ( pos < 3 )
			s = LAST_TRI;
		else
			s = powf( 1.0f - ( float )( pos - 3 ) / ( float )( CACHE - 3 ), CACHE_DECAY );
	}
	return s + VALENCE_SCALE * powf( ( float )remaining, -VALENCE_POWER );
}

VCACHE_EXPORT int vcache_optimize( const unsigned *idx, unsigned tris, unsigned verts, unsigned *out )
{
	unsigned *count = calloc( verts + 1, sizeof( unsigned ) ), *start = NULL, *adj = NULL, *fill = NULL;
	int *remaining = NULL, *cpos = NULL;
	float *vscore = NULL, *tscore = NULL;
	unsigned char *emitted = NULL;
	int cache[ CACHE + 3 ], csize = 0, ret = -1;
	unsigned t, v, k, done = 0, best = 0, scan = 0;
	float bestScore;

	if ( !count )
		return -1;
	for ( k = 0; k < tris * 3; k++ ) {
		if ( idx[ k ] >= verts )
			goto end;
		count[ idx[ k ] ]++;
	}
	start = malloc( ( verts + 1 ) * sizeof( unsigned ) );
	fill = calloc( verts, sizeof( unsigned ) );
	adj = malloc( ( size_t )tris * 3 * sizeof( unsigned ) );
	remaining = malloc( verts * sizeof( int ) );
	cpos = malloc( verts * sizeof( int ) );
	vscore = malloc( verts * sizeof( float ) );
	tscore = malloc( tris * sizeof( float ) );
	emitted = calloc( tris, 1 );
	if ( !start || !fill || !adj || !remaining || !cpos || !vscore || !tscore || !emitted )
		goto end;
	start[ 0 ] = 0;
	for ( v = 0; v < verts; v++ ) {
		start[ v + 1 ] = start[ v ] + count[ v ];
		remaining[ v ] = ( int )count[ v ];
		cpos[ v ] = -1;
	}
	for ( t = 0; t < tris; t++ )
		for ( k = 0; k < 3; k++ ) {
			v = idx[ t * 3 + k ];
			adj[ start[ v ] + fill[ v ]++ ] = t;
		}
	for ( v = 0; v < verts; v++ )
		vscore[ v ] = score_of( -1, remaining[ v ] );
	bestScore = -1.0f;
	for ( t = 0; t < tris; t++ ) {
		tscore[ t ] = vscore[ idx[ t * 3 ] ] + vscore[ idx[ t * 3 + 1 ] ] + vscore[ idx[ t * 3 + 2 ] ];
		if ( tscore[ t ] > bestScore ) {
			bestScore = tscore[ t ];
			best = t;
		}
	}
	while ( done < tris ) {
		int ncache[ CACHE + 3 ], n = 0, i, j;
		if ( bestScore < 0.0f ) {	/* nothing in reach (a new island): the next triangle not yet emitted */
			while ( emitted[ scan ] )
				scan++;
			best = scan;
		}
		emitted[ best ] = 1;
		memcpy( out + done * 3, idx + best * 3, 3 * sizeof( unsigned ) );
		done++;
		/* its vertices to the cache's front, one use fewer each, the triangle out of their lists */
		for ( k = 0; k < 3; k++ ) {
			unsigned *a;
			v = idx[ best * 3 + k ];
			ncache[ n++ ] = ( int )v;
			remaining[ v ]--;
			a = adj + start[ v ];
			for ( j = 0; j < ( int )fill[ v ]; j++ )
				if ( a[ j ] == best ) {
					a[ j ] = a[ --fill[ v ] ];
					break;
				}
		}
		for ( i = 0; i < csize; i++ ) {
			v = ( unsigned )cache[ i ];
			if ( v != idx[ best * 3 ] && v != idx[ best * 3 + 1 ] && v != idx[ best * 3 + 2 ] )
				ncache[ n++ ] = ( int )v;
		}
		/* new positions and scores; the vertices pushed out leave the cache */
		for ( i = 0; i < n; i++ ) {
			v = ( unsigned )ncache[ i ];
			cpos[ v ] = i < CACHE ? i : -1;
			vscore[ v ] = score_of( cpos[ v ], remaining[ v ] );
		}
		csize = n < CACHE ? n : CACHE;
		memcpy( cache, ncache, csize * sizeof( int ) );
		/* the triangles touching the cache rescored; the best of them is next */
		bestScore = -1.0f;
		for ( i = 0; i < n; i++ ) {
			const unsigned *a;
			v = ( unsigned )ncache[ i ];
			a = adj + start[ v ];
			for ( j = 0; j < ( int )fill[ v ]; j++ ) {
				t = a[ j ];
				tscore[ t ] = vscore[ idx[ t * 3 ] ] + vscore[ idx[ t * 3 + 1 ] ] + vscore[ idx[ t * 3 + 2 ] ];
				if ( tscore[ t ] > bestScore ) {
					bestScore = tscore[ t ];
					best = t;
				}
			}
		}
	}
	ret = 0;
end:
	free( count ); free( start ); free( fill ); free( adj ); free( remaining ); free( cpos ); free( vscore );
	free( tscore ); free( emitted );
	return ret;
}
