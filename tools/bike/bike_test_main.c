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
 * bike_test_main.c -- replays the aligned tape of tools/bike/tape_harness.py
 * through codemp/game/bg_bike.c and prints the 528 ms checkpoints.
 *
 *   gcc -O2 -I codemp/game [-DBG_BIKE_DOUBLE] codemp/game/bg_bike.c
 *       tools/bike/bike_test_main.c -lm -o bike_test.exe
 *   python tools/bike/tape_harness.py --c-exe bike_test.exe [--bike FILE.bike]
 *
 * bike_test.exe [FILE.bike]: the handling file over the generic bike (BG_BikeParseParams), as the game reads
 * ext_data/vehicles/<name>.bike; without one the generic bike.
 *
 * Output lines: ground split t x y z yaw lean pitch speed (split 0: the mixed 1-41 ms sequence, cut at
 * every checkpoint; tape_harness.py generates the same one)
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bg_bike.h"

#define SEG_MS		528
#define NUM_SEGS	19

static const double tapeValues[NUM_SEGS][4] = {
	{ 1.0, 0.0, 0.0, 0 },
	{ 1.0, 0.0, 0.0, 0 },
	{ 1.0, 0.0, -1.0, 0 },
	{ 1.0, 0.0, -1.0, 0 },
	{ 1.0, 0.0, 0.0, 0 },
	{ 1.0, 0.6, 0.0, 0 },
	{ 1.0, 0.6, 0.5, 0 },
	{ 1.0, -1.0, 1.0, 0 },
	{ 1.0, -1.0, 1.0, 0 },
	{ 0.0, 0.0, 0.0, 0 },
	{ -1.0, 0.0, 0.0, 0 },
	{ -1.0, 0.3, 0.0, 1 },
	{ 1.0, 0.0, 0.0, 0 },
	{ 1.0, 0.0, -1.0, 0 },
	{ 1.0, 1.0, 0.0, 0 },
	{ 0.5, -0.5, 1.0, 0 },
	{ 1.0, 0.0, 1.0, 0 },
	{ -1.0, 0.0, 0.0, 0 },
	{ -1.0, 0.0, 0.0, 0 },
};

static int GroundFlat( void *ctx, double x, double y, double *z )
{
	(void)ctx; (void)x; (void)y;
	*z = 0.0;
	return 1;
}

static int GroundCurbs( void *ctx, double x, double y, double *z )
{
	double h;
	long a = (long)floor( x / 500.0 ), b = (long)floor( y / 500.0 ), m;

	(void)ctx;
	h = ( ( a + b ) & 1 ) ? 6.0 : 0.0;
	m = (long)floor( x / 1300.0 );
	m = ( ( m % 3 ) + 3 ) % 3;
	if ( m == 2 )
		h += 12.0;
	*z = h;
	return 1;
}

int main( int argc, char **argv )
{
	static const int splits[5] = { 3, 8, 16, 33, 0 };
	const char *names[2] = { "flat", "curbs" };
	bikeGroundHeight_f grounds[2];
	bikeParams_t bp;
	bikeState_t bs;
	bikeWorld_t world;
	bikeInput_t in;
	int g, i, t, t1, seg, end = SEG_MS * NUM_SEGS;
	unsigned int seed;

	grounds[0] = GroundFlat;
	grounds[1] = GroundCurbs;
	BG_BikeDefaultParams( &bp );
	if ( argc > 1 )
	{
		static char text[64 * 1024];
		char		bad[64];
		FILE		*fp = fopen( argv[1], "rb" );
		size_t		len;

		if ( !fp )
		{
			fprintf( stderr, "cannot open %s\n", argv[1] );
			return 2;
		}
		len = fread( text, 1, sizeof( text ) - 1, fp );
		fclose( fp );
		text[len] = 0;
		if ( BG_BikeParseParams( &bp, text, bad, sizeof( bad ) ) )
		{
			fprintf( stderr, "%s: lines not understood (first '%s')\n", argv[1], bad );
			return 2;
		}
	}

	for ( g = 0; g < 2; g++ )
	{
		world.groundHeight = grounds[g];
		world.wheelGround = NULL;
		world.translate = NULL;
		world.surface = NULL;
		world.ctx = NULL;
		for ( i = 0; i < 5; i++ )
		{
			BG_BikeInit( &bs, &bp, &world, 0.0, 0.0, 0.0, 0 );
			t = 0;
			seed = 12345;
			while ( t < end )
			{
				seg = t / SEG_MS;
				in.throttle = tapeValues[seg][0];
				in.steer = tapeValues[seg][1];
				in.lean = tapeValues[seg][2];
				in.handbrake = (int)tapeValues[seg][3];
				if ( splits[i] )
					t1 = t + splits[i];
				else
				{
					seed = seed * 1103515245u + 12345u;
					t1 = t + 1 + (int)( ( seed >> 16 ) & 0x7fff ) % 41;
					if ( t1 > ( seg + 1 ) * SEG_MS )
						t1 = ( seg + 1 ) * SEG_MS;
				}
				if ( t1 > end )
					t1 = end;
				BG_BikeRunCommand( &bs, &bp, &in, &world, t1 );
				t = t1;
				if ( t % SEG_MS == 0 )
				{
					printf( "%s %d %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", names[g], splits[i], t,
						(double)bs.origin[0], (double)bs.origin[1], (double)bs.origin[2],
						(double)bs.yaw, (double)bs.lean, (double)bs.pitch, (double)bs.speed );
				}
			}
		}
	}
	return 0;
}
