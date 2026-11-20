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

// bg_bike.c -- motorbike dynamics (wishlist group V; v3, CP2077-style data on two sprung wheels)
// game and cgame, NOT ui; bg_bikemove.c is the pmove adapter. See bg_bike.h.
//
// Every equation here is mirrored line for line in tools/bike/bike_twin.py.
// Change both together; tools/bike/tape_harness.py --c-exe compares them.

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "bg_bike.h"

#define BIKE_PI			3.14159265358979323846
#define BIKE_D2R		(BIKE_PI / 180.0)
#define BIKE_R2D		(180.0 / BIKE_PI)
#define BIKE_UNITS		37.5	// units per metre

static double BikeClamp( double v, double lo, double hi )
{
	if ( v < lo )
		return lo;
	if ( v > hi )
		return hi;
	return v;
}

static double BikeApproach( double cur, double target, double step )
{
	if ( cur < target )
	{
		cur += step;
		return ( cur > target ) ? target : cur;
	}
	cur -= step;
	return ( cur < target ) ? target : cur;
}

static double BikeSign( double v )
{
	return ( v > 0 ) ? 1.0 : ( ( v < 0 ) ? -1.0 : 0.0 );
}

static int BikeFloorDiv( int a, int b )
{
	int q = a / b;

	if ( ( a % b ) != 0 && ( ( a < 0 ) != ( b < 0 ) ) )
		q--;
	return q;
}

// piecewise linear through the keys, clamped at both ends
static double BikeCurve( const bikeCurve_t *c, double x )
{
	int i;
	double x0, y0, x1, y1;

	if ( x <= c->pts[0][0] )
		return c->pts[0][1];
	for ( i = 1; i < c->n; i++ )
	{
		if ( x <= c->pts[i][0] )
		{
			x0 = c->pts[i - 1][0];
			y0 = c->pts[i - 1][1];
			x1 = c->pts[i][0];
			y1 = c->pts[i][1];
			if ( x1 <= x0 )
				return y1;
			return y0 + ( y1 - y0 ) * ( x - x0 ) / ( x1 - x0 );
		}
	}
	return c->pts[c->n - 1][1];
}

static void BikeSetCurve( bikeCurve_t *c, const double (*pts)[2], int n )
{
	int i;

	if ( n > BIKE_MAX_CURVE )
		n = BIKE_MAX_CURVE;
	memset( c, 0, sizeof( *c ) );
	c->n = n;
	for ( i = 0; i < n; i++ )
	{
		c->pts[i][0] = pts[i][0];
		c->pts[i][1] = pts[i][1];
	}
}

/*
===================
BG_BikeDefaultParams

A generic sport bike (xenjk's own round numbers, CP2077-style units: m, kg, N m, s, deg), used when the bike's
pack carries no handling file. A bike's own handling comes from ext_data/vehicles/<name>.bike
(BG_BikeParseParams, docs/bike-handling.md), read over these.
===================
*/
static const double bikeDefGears[7][5] = {	// minSpeed m/s, maxSpeed m/s, minRPM, maxRPM, torqueMultiplier
	{ 0.0, 3.0, 1000.0, 8000.0, 1.0 },		// reverse
	{ 0.0, 12.0, 1000.0, 8500.0, 1.2 },
	{ 10.0, 18.0, 4000.0, 8500.0, 0.85 },
	{ 16.0, 24.0, 4500.0, 8500.0, 0.65 },
	{ 22.0, 30.0, 4500.0, 8500.0, 0.5 },
	{ 28.0, 36.0, 4500.0, 8500.0, 0.4 },
	{ 34.0, 40.0, 4500.0, 9000.0, 0.3 },
};
static const double bikeDefSurfaces[BIKE_NUM_SURFACES][2] = { { 1.3, 1.0 }, { 1.1, 0.8 }, { 0.9, 0.7 } };
static const double bikeDefTorque[4][2] = { { 0.0, 0.8 }, { 0.5, 0.95 }, { 0.75, 1.0 }, { 1.0, 0.85 } };
static const double bikeDefSpeedToTilt[5][2] = { { -1.0, 0.2 }, { 0.0, 0.0 }, { 0.25, 0.6 }, { 0.5, 0.85 }, { 1.0, 1.0 } };
static const double bikeDefInputToTilt[2][2] = { { 0.0, 0.0 }, { 1.0, 1.0 } };
static const double bikeDefSpeedToTiltSpeed[3][2] = { { 0.0, 1.0 }, { 0.25, 0.95 }, { 1.0, 0.8 } };
static const double bikeDefLatTraction[5][2] = { { 0.0, 0.0 }, { 0.6, 0.45 }, { 2.0, 0.7 }, { 6.0, 1.0 }, { 10.0, 1.0 } };
static const double bikeDefLatTractionRear[4][2] = { { 0.0, 0.7 }, { 1.0, 0.9 }, { 3.0, 1.0 }, { 10.0, 1.05 } };

static void BikeDefaultGears( bikeParams_t *bp )
{
	int i, j;

	memset( bp->gears, 0, sizeof( bp->gears ) );
	bp->numGears = 7;
	for ( i = 0; i < bp->numGears; i++ )
		for ( j = 0; j < 5; j++ )
			bp->gears[i][j] = bikeDefGears[i][j];
}

void BG_BikeDefaultParams( bikeParams_t *bp )
{
	int i;

	memset( bp, 0, sizeof( *bp ) );
	bp->maxCommandMs		= 250;

	bp->gravity				= 9.81;
	bp->stepHeight			= 18.0;
	bp->originHeight		= 24.0;				// -DEFAULT_MINS_2: the tyres' ground below the state origin
	bp->originAhead			= 0.725;			// the state origin mid-wheelbase
	bp->frameClearance		= 0.10;				// the frame's belly, about 10 cm up at rest
	bp->wheelClimb			= 48.0;				// four 12 u stairs ahead of the front wheel

	bp->mass				= 450.0;
	bp->inertiaPitch		= 220.0;
	bp->wheelbase			= 1.45;
	bp->comHeight			= 0.55;				// with a JKA rider
	bp->comLongBack			= 0.65;
	bp->comLongFwd			= 0.80;
	bp->comDamp				= 0.25;
	bp->airResistance		= 0.6;
	bp->forwardWeightTransfer = 1.0;
	bp->lowVelStopDecel		= 1.0;
	bp->brakingFriction		= 1.2;
	bp->handbrakeTorque		= 3000.0;
	bp->frameRate			= 60.0;				// per-frame damp factors are per 1/60 s frame

	bp->maxWheelTurn		= 45.0;
	bp->wheelTurnAdd		= 120.0;
	bp->wheelTurnSub		= 120.0;
	bp->turnBaseSpeed		= 8.0;
	bp->turnMaxSpeed		= 30.0;
	bp->turnMaxSpeedMul		= 0.25;
	bp->maxYawRate			= 2.0;
	bp->perfectSteering		= 1.0;
	bp->maxTilt				= 40.0;
	bp->tiltSpeed			= 100.0;
	bp->tiltReturnSpeed		= 100.0;
	bp->tiltKp				= 2.0;
	bp->tiltKi				= 0.1;

	// springs as natural frequencies (rad/s): sag g / w^2 is 5.0 cm of the front's 20 cm, 5.8 cm of the back's 25 cm
	bp->suspTravel[0]		= 0.20;
	bp->suspTravel[1]		= 0.25;
	bp->suspOmega[0]		= 14.0;
	bp->suspOmega[1]		= 13.0;
	for ( i = 0; i < 2; i++ )
	{
		bp->dampBump[i]			= 2500.0;
		bp->dampBumpLow[i]		= 1800.0;
		bp->dampRebound[i]		= 2200.0;
		bp->dampReboundLow[i]	= 1600.0;
		bp->dampLowRate[i]		= 0.15;
		bp->dampHighRate[i]		= 0.35;
	}
	bp->bumpStop			= 10.0;
	bp->bumpTravel			= 0.10;

	bp->tireRadiusF			= 0.30;
	bp->tireRadiusR			= 0.32;
	bp->tireFriction		= 1.0;
	bp->latMulF				= 0.9;
	bp->latMulR				= 0.9;
	bp->longMulF			= 0.9;
	bp->longMulR			= 0.9;
	bp->rollingCoef			= 0.005;
	bp->hbLatR				= 0.7;
	bp->hbLongR				= 0.7;
	bp->burnGrip			= 5.0;
	bp->burnLaunchSpeed		= 5.0;
	bp->burnMaxSpeed		= 20.0;
	bp->launchGripMul		= 1.5;
	for ( i = 0; i < BIKE_NUM_SURFACES; i++ )
	{
		bp->surfaces[i][0] = bikeDefSurfaces[i][0];
		bp->surfaces[i][1] = bikeDefSurfaces[i][1];
	}

	bp->engineTorque		= 170.0;
	bp->resistanceTorque	= 80.0;
	bp->wheelsResistanceRatio = 12.0;
	bp->finalGearScale		= 1.0;
	bp->minRPM				= 1000.0;
	bp->maxRPM				= 9000.0;
	bp->gearChangeTime		= 0.2;
	bp->gearChangeCooldown	= 0.3;
	BikeDefaultGears( bp );
	bp->speedNorm			= bp->gears[bp->numGears - 1][1];	// the tilt curves' speed axis: top gear maxSpeed
	bp->reverseThreshold	= 0.5;

	BikeSetCurve( &bp->torque, bikeDefTorque, 4 );
	BikeSetCurve( &bp->speedToTilt, bikeDefSpeedToTilt, 5 );
	BikeSetCurve( &bp->inputToTilt, bikeDefInputToTilt, 2 );
	BikeSetCurve( &bp->speedToTiltSpeed, bikeDefSpeedToTiltSpeed, 3 );
	BikeSetCurve( &bp->latTraction, bikeDefLatTraction, 5 );
	BikeSetCurve( &bp->latTractionRear, bikeDefLatTractionRear, 4 );

	// the rider's pull on the bars (wheelies)
	bp->wheelieLean			= 4.0;
	bp->wheelieAssist		= 1;
	bp->assistMargin		= 12.0;
	bp->assistKp			= 40.0;
	bp->assistKd			= 8.0;
	bp->pitchDamping		= 1.5;
	bp->maxPitch			= 70.0;
	bp->landBailPitch		= 45.0;

	bp->airPitchMaxVel		= 6.0;
	bp->airPitchInputDamp	= 0.25;
	bp->airPitchStabilize	= 1;
	bp->airPitchZeroAngle	= 10.0;
	bp->airStabilizeRate	= 1.0;
	bp->airYawMaxVel		= 6.0;
	bp->airYawInputDamp		= 0.1;
}

/*
===================
BG_BikeParseParams

A bike handling file (ext_data/vehicles/<vehicle name>.bike; format in docs/bike-handling.md) read over *bp, which
the caller has filled (BG_BikeDefaultParams). One setting per line, // comments:
  <field> <value>               any scalar of bikeParams_t, by its name
  <field> <front> <rear>        the suspension pairs (suspTravel, suspOmega, dampBump, ...)
  gear <minSpeed> <maxSpeed> <minRPM> <maxRPM> <torqueMultiplier>
                                the first gear line replaces the gearbox; [0] reverse, then forward in order
  surface <asphalt|ground|sand> <frictionLat> <frictionLong>
  curve <name> <x> <y> ...      torque, speedToTilt, inputToTilt, speedToTiltSpeed, latTraction, latTractionRear
speedNorm follows the top gear's maxSpeed unless the file sets it. Returns the number of lines not understood
(skipped); the first one's text goes to bad.
===================
*/
typedef enum { BPF_DOUBLE, BPF_INT, BPF_PAIR } bikeParmType_t;

typedef struct {
	const char		*name;
	size_t			ofs;
	bikeParmType_t	type;
} bikeParmField_t;

#define BPD( f )	{ #f, offsetof( bikeParams_t, f ), BPF_DOUBLE }
#define BPI( f )	{ #f, offsetof( bikeParams_t, f ), BPF_INT }
#define BPP( f )	{ #f, offsetof( bikeParams_t, f ), BPF_PAIR }

static const bikeParmField_t bikeParmFields[] = {
	BPI( maxCommandMs ),
	BPD( gravity ), BPD( stepHeight ), BPD( originHeight ), BPD( originAhead ), BPD( frameClearance ),
	BPD( wheelClimb ),
	BPD( mass ), BPD( inertiaPitch ), BPD( wheelbase ), BPD( comHeight ), BPD( comLongBack ), BPD( comLongFwd ),
	BPD( comDamp ), BPD( airResistance ), BPD( forwardWeightTransfer ), BPD( lowVelStopDecel ),
	BPD( brakingFriction ), BPD( handbrakeTorque ), BPD( frameRate ),
	BPD( maxWheelTurn ), BPD( wheelTurnAdd ), BPD( wheelTurnSub ), BPD( turnBaseSpeed ), BPD( turnMaxSpeed ),
	BPD( turnMaxSpeedMul ), BPD( maxYawRate ), BPD( perfectSteering ), BPD( maxTilt ), BPD( tiltSpeed ),
	BPD( tiltReturnSpeed ), BPD( tiltKp ), BPD( tiltKi ), BPD( speedNorm ),
	BPP( suspTravel ), BPP( suspOmega ), BPP( dampBump ), BPP( dampBumpLow ), BPP( dampRebound ),
	BPP( dampReboundLow ), BPP( dampLowRate ), BPP( dampHighRate ), BPD( bumpStop ), BPD( bumpTravel ),
	BPD( tireRadiusF ), BPD( tireRadiusR ), BPD( tireFriction ), BPD( latMulF ), BPD( latMulR ), BPD( longMulF ),
	BPD( longMulR ), BPD( rollingCoef ), BPD( hbLatR ), BPD( hbLongR ), BPD( burnGrip ), BPD( burnLaunchSpeed ),
	BPD( burnMaxSpeed ), BPD( launchGripMul ),
	BPD( engineTorque ), BPD( resistanceTorque ), BPD( wheelsResistanceRatio ), BPD( finalGearScale ),
	BPD( minRPM ), BPD( maxRPM ), BPD( gearChangeTime ), BPD( gearChangeCooldown ), BPD( reverseThreshold ),
	BPD( wheelieLean ), BPI( wheelieAssist ), BPD( assistMargin ), BPD( assistKp ), BPD( assistKd ),
	BPD( pitchDamping ), BPD( maxPitch ), BPD( landBailPitch ),
	BPD( airPitchMaxVel ), BPD( airPitchInputDamp ), BPI( airPitchStabilize ), BPD( airPitchZeroAngle ),
	BPD( airStabilizeRate ), BPD( airYawMaxVel ), BPD( airYawInputDamp ),
};

#undef BPD
#undef BPI
#undef BPP

static const struct {
	const char	*name;
	size_t		ofs;
} bikeCurveFields[] = {
	{ "torque", offsetof( bikeParams_t, torque ) },
	{ "speedToTilt", offsetof( bikeParams_t, speedToTilt ) },
	{ "inputToTilt", offsetof( bikeParams_t, inputToTilt ) },
	{ "speedToTiltSpeed", offsetof( bikeParams_t, speedToTiltSpeed ) },
	{ "latTraction", offsetof( bikeParams_t, latTraction ) },
	{ "latTractionRear", offsetof( bikeParams_t, latTractionRear ) },
};

static const char *bikeSurfaceNames[BIKE_NUM_SURFACES] = { "asphalt", "ground", "sand" };

#define BIKE_PARSE_TOKENS	( 2 + 2 * BIKE_MAX_CURVE + 1 )

static int BikeStrEq( const char *a, const char *b )
{	// case-insensitive, ASCII
	for ( ; *a && *b; a++, b++ )
	{
		int ca = ( *a >= 'A' && *a <= 'Z' ) ? *a + 32 : *a, cb = ( *b >= 'A' && *b <= 'Z' ) ? *b + 32 : *b;

		if ( ca != cb )
			return 0;
	}
	return *a == *b;
}

// numbers tok[first..n-1] into v; 0 when one is not a number
static int BikeParseNumbers( char **tok, int first, int n, double *v )
{
	int		i;
	char	*end;

	for ( i = first; i < n; i++ )
	{
		v[i - first] = strtod( tok[i], &end );
		if ( end == tok[i] || *end )
			return 0;
	}
	return 1;
}

static int BikeParseLine( bikeParams_t *bp, char **tok, int n, int *gearsSeen, int *speedNormSet )
{
	double	v[BIKE_PARSE_TOKENS];
	size_t	i;
	int		k;

	if ( n < 2 || n >= BIKE_PARSE_TOKENS || !BikeParseNumbers( tok, BikeStrEq( tok[0], "surface" )
		|| BikeStrEq( tok[0], "curve" ) ? 2 : 1, n, v ) )
		return 0;
	if ( BikeStrEq( tok[0], "gear" ) )
	{
		if ( n != 6 )
			return 0;
		if ( !*gearsSeen )
		{	// the file's gearbox replaces the default one
			memset( bp->gears, 0, sizeof( bp->gears ) );
			bp->numGears = 0;
			*gearsSeen = 1;
		}
		if ( bp->numGears >= BIKE_MAX_GEARS )
			return 0;
		for ( k = 0; k < 5; k++ )
			bp->gears[bp->numGears][k] = v[k];
		bp->numGears++;
		return 1;
	}
	if ( BikeStrEq( tok[0], "surface" ) )
	{
		if ( n != 4 )
			return 0;
		for ( k = 0; k < BIKE_NUM_SURFACES; k++ )
		{
			if ( BikeStrEq( tok[1], bikeSurfaceNames[k] ) )
			{
				bp->surfaces[k][0] = v[0];
				bp->surfaces[k][1] = v[1];
				return 1;
			}
		}
		return 0;
	}
	if ( BikeStrEq( tok[0], "curve" ) )
	{
		if ( n < 4 || ( n - 2 ) % 2 || ( n - 2 ) / 2 > BIKE_MAX_CURVE )
			return 0;
		for ( i = 0; i < sizeof( bikeCurveFields ) / sizeof( bikeCurveFields[0] ); i++ )
		{
			if ( BikeStrEq( tok[1], bikeCurveFields[i].name ) )
			{
				BikeSetCurve( (bikeCurve_t *)( (char *)bp + bikeCurveFields[i].ofs ), (const double (*)[2])v,
					( n - 2 ) / 2 );
				return 1;
			}
		}
		return 0;
	}
	for ( i = 0; i < sizeof( bikeParmFields ) / sizeof( bikeParmFields[0] ); i++ )
	{
		const bikeParmField_t *f = &bikeParmFields[i];
		char *dst = (char *)bp + f->ofs;

		if ( !BikeStrEq( tok[0], f->name ) )
			continue;
		if ( f->type == BPF_PAIR )
		{
			if ( n != 3 )
				return 0;
			( (double *)dst )[0] = v[0];
			( (double *)dst )[1] = v[1];
		}
		else
		{
			if ( n != 2 )
				return 0;
			if ( f->type == BPF_INT )
				*(int *)dst = (int)v[0];
			else
				*(double *)dst = v[0];
		}
		if ( f->ofs == offsetof( bikeParams_t, speedNorm ) )
			*speedNormSet = 1;
		return 1;
	}
	return 0;
}

int BG_BikeParseParams( bikeParams_t *bp, const char *text, char *bad, int badSize )
{
	char	line[1024], *tok[BIKE_PARSE_TOKENS + 1], *c;
	int		len, n, errors = 0, gearsSeen = 0, speedNormSet = 0;

	if ( bad && badSize > 0 )
		bad[0] = 0;
	while ( text && *text )
	{
		for ( len = 0; text[len] && text[len] != '\n'; len++ )
			;
		if ( len < (int)sizeof( line ) )
		{
			memcpy( line, text, len );
			line[len] = 0;
		}
		else
		{	// too long: skipped
			line[0] = 0;
			errors++;
		}
		text += len + ( text[len] == '\n' );

		for ( c = line; *c; c++ )
		{
			if ( c[0] == '/' && c[1] == '/' )
			{
				*c = 0;
				break;
			}
		}
		n = 0;
		for ( c = line; *c; )
		{
			while ( *c == ' ' || *c == '\t' || *c == '\r' )
				*c++ = 0;
			if ( !*c )
				break;
			if ( n < BIKE_PARSE_TOKENS + 1 )
				tok[n] = c;
			n++;
			while ( *c && *c != ' ' && *c != '\t' && *c != '\r' )
				c++;
		}
		if ( !n )
			continue;
		if ( n > BIKE_PARSE_TOKENS || !BikeParseLine( bp, tok, n, &gearsSeen, &speedNormSet ) )
		{
			if ( !errors && bad && badSize > 0 )
			{
				int k = 0;

				for ( c = (char *)tok[0]; *c && k < badSize - 1; c++ )
					bad[k++] = *c;
				bad[k] = 0;
			}
			errors++;
		}
	}
	if ( bp->numGears < 2 )
	{	// a reverse and a forward gear at least: the default gearbox
		BikeDefaultGears( bp );
		errors++;
	}
	if ( gearsSeen && !speedNormSet )
		bp->speedNorm = bp->gears[bp->numGears - 1][1];
	return errors;
}

void BG_BikeInit( bikeState_t *bs, const bikeParams_t *bp, const bikeWorld_t *world, double x, double y, double yaw, int commandTime )
{
	double z = 0.0;

	if ( !world->groundHeight( world->ctx, x, y, &z ) )
		z = 0.0;
	bs->origin[0] = x;
	bs->origin[1] = y;
	bs->origin[2] = z + bp->originHeight;
	bs->velocity[0] = bs->velocity[1] = bs->velocity[2] = 0;
	bs->yaw = yaw;
	bs->yawRate = 0;
	bs->lean = bs->leanInt = 0;
	bs->pitch = bs->pitchRate = 0;
	bs->steer = 0;
	bs->speed = 0;
	bs->comLong = 0.5 * ( bp->comLongBack + bp->comLongFwd );
	bs->rpm = bp->minRPM;
	bs->gear = 1;
	bs->shiftMs = BIKE_SHIFT_MS_MAX;
	bs->burnout = 0;
	bs->burnLaunch = 0;
	bs->onGround = 1;
	bs->commandTime = commandTime;
	bs->events = 0;
	bs->droppedMs = 0;
	bs->contact[0] = bs->contact[1] = 1;
	bs->load[0] = bs->load[1] = 0;
	bs->comp[0] = bs->comp[1] = 0;
}

// CP2077 turnUpdate: full lock up to the base speed, down to turnMaxSpeedMul at the max speed (m/s)
static double BikeTurnMul( const bikeParams_t *bp, double v )
{
	double f;

	if ( v <= bp->turnBaseSpeed )
		return 1.0;
	if ( v >= bp->turnMaxSpeed )
		return bp->turnMaxSpeedMul;
	f = ( v - bp->turnBaseSpeed ) / ( bp->turnMaxSpeed - bp->turnBaseSpeed );
	return 1.0 + ( bp->turnMaxSpeedMul - 1.0 ) * f;
}

static double BikeGearRPM( const bikeParams_t *bp, int gear, double v )
{
	const double *g = bp->gears[gear];
	double f = 0.0;

	if ( g[1] > g[0] )
		f = BikeClamp( ( v - g[0] ) / ( g[1] - g[0] ), 0.0, 1.0 );
	return g[2] + ( g[3] - g[2] ) * f;
}

// engine rpm in a gear at v m/s (cgame's engine sound)
double BG_BikeGearRPM( const bikeParams_t *bp, int gear, double v )
{
	if ( gear < 0 || gear >= bp->numGears )
		return bp->minRPM;
	return BikeGearRPM( bp, gear, v );
}

/*
===================
BG_BikeLand

A wheel touches down after flight (BG_BikeSubstep); the springs take it from there.
===================
*/
void BG_BikeLand( bikeState_t *bs, const bikeParams_t *bp )
{
	if ( fabs( bs->pitch ) > bp->landBailPitch )
		bs->events |= BIKE_EV_BAIL;
	bs->events |= BIKE_EV_LANDED;
}

/*
===================
BG_BikeHeightfieldTranslate

Default translate: the body moves by its velocity; the wheels and springs keep it on the heightfield (BikeWheels).
A rise of more than stepHeight under the next rear contact is a wall: there is no slide-move here, so it stops dead.
===================
*/
void BG_BikeHeightfieldTranslate( const bikeWorld_t *world, bikeState_t *bs, const bikeParams_t *bp, double dt )
{
	double nx = bs->origin[0] + bs->velocity[0] * dt;
	double ny = bs->origin[1] + bs->velocity[1] * dt;
	double gz = 0.0;

	if ( world->groundHeight( world->ctx, nx, ny, &gz ) && gz > bs->origin[2] - bp->originHeight + bp->stepHeight )
	{
		bs->velocity[0] = bs->velocity[1] = 0;
		bs->events |= BIKE_EV_BLOCKED;
		nx = bs->origin[0];
		ny = bs->origin[1];
	}
	bs->origin[0] = nx;
	bs->origin[1] = ny;
	bs->origin[2] += bs->velocity[2] * dt;
}

// a body point (bx forward, bz up, u from Q) along the heading and up from Q at pitch (c, s) = (cos, sin)
#define BIKE_ALONG( bx, bz, c, s )	( (bx) * (c) - (bz) * (s) )
#define BIKE_UP( bx, bz, c, s )		( (bx) * (s) + (bz) * (c) )

#define BIKE_HF_STEP	1.0		// u
#define BIKE_HF_EDGE	0.7		// steeper than walkable (MIN_WALK_NORMAL) is an edge

typedef struct {
	int		contact;
	double	comp;			// m, 0 = fully extended
	double	load;			// N
	double	gz;				// u, the ground under it
	double	normal[3];
} bikeWheel_t;

/*
===================
BikeWheels

Each wheel's ground, suspension compression and spring load, from the state at the start of the substep. At rest
the axle is a tyre radius above its contact patch (the front one a wheelbase ahead of Q); compression moves the wheel
up the body. The probe runs from stepHeight above the fully compressed tyre's bottom down to the fully extended one's;
no ground in it is a wheel in the air. Spring rate: the corner's share of the mass at the mid rider position times
the natural frequency squared, so the static sag is g / w^2.
===================
*/
static void BikeWheels( const bikeState_t *bs, const bikeParams_t *bp, const bikeWorld_t *world, bikeWheel_t *w )
{
	double th = bs->pitch * BIKE_D2R, c = cos( th ), s = sin( th );
	double yawR = bs->yaw * BIKE_D2R, cy = cos( yawR ), sy = sin( yawR );
	double qz = bs->origin[2] - bp->originHeight;
	double om = bs->pitchRate * BIKE_D2R;
	double xm = 0.5 * ( bp->comLongBack + bp->comLongFwd );
	double bx, r, along, bottom, sag, travel, share, k, top[3], depth, gz, n[3], comp, vAxle, vGround, rate, a, lo, hi;
	double d, f;
	int i, hit;

	for ( i = 0; i < 2; i++ )
	{
		bx = ( i == 0 ) ? bp->wheelbase * BIKE_UNITS : 0.0;
		r = ( ( i == 0 ) ? bp->tireRadiusF : bp->tireRadiusR ) * BIKE_UNITS;
		along = BIKE_ALONG( bx, r, c, s );
		bottom = qz + BIKE_UP( bx, r, c, s ) - r;	// the tyre's bottom at the rest compression
		sag = bp->gravity / ( bp->suspOmega[i] * bp->suspOmega[i] );
		travel = bp->suspTravel[i];
		share = ( i == 0 ) ? xm / bp->wheelbase : 1.0 - xm / bp->wheelbase;
		k = bp->mass * share * bp->suspOmega[i] * bp->suspOmega[i];

		top[0] = bs->origin[0] + cy * ( along - bp->originAhead * BIKE_UNITS );
		top[1] = bs->origin[1] + sy * ( along - bp->originAhead * BIKE_UNITS );
		top[2] = bottom + ( travel - sag ) * BIKE_UNITS + bp->stepHeight;
		depth = travel * BIKE_UNITS + bp->stepHeight;
		gz = 0.0;
		n[0] = n[1] = 0.0;
		n[2] = 1.0;
		if ( world->wheelGround )
		{
			hit = world->wheelGround( world->ctx, i, top, depth, &gz, n );
			if ( hit < 0 )
			{	// buried (a stair, a curb above the probe): it climbs onto the top within wheelClimb; still inside
				// from there up is a wall
				double high[3];

				high[0] = top[0];
				high[1] = top[1];
				high[2] = top[2] + bp->wheelClimb;
				hit = world->wheelGround( world->ctx, i, high, bp->wheelClimb, &gz, n );
				if ( hit < 0 )
					hit = 0;
			}
		}
		else
		{	// the heightfield: its slope by central differences (BIKE_HF_STEP u apart)
			double zx0, zx1, zy0, zy1, len;

			hit = world->groundHeight( world->ctx, top[0], top[1], &gz );
			if ( hit && gz > top[2] + bp->wheelClimb )
				hit = 0;	// a wall
			if ( hit && gz < top[2] - depth )
				hit = 0;
			if ( hit && world->groundHeight( world->ctx, top[0] - BIKE_HF_STEP, top[1], &zx0 )
				&& world->groundHeight( world->ctx, top[0] + BIKE_HF_STEP, top[1], &zx1 )
				&& world->groundHeight( world->ctx, top[0], top[1] - BIKE_HF_STEP, &zy0 )
				&& world->groundHeight( world->ctx, top[0], top[1] + BIKE_HF_STEP, &zy1 ) )
			{
				n[0] = -( zx1 - zx0 ) / ( 2.0 * BIKE_HF_STEP );
				n[1] = -( zy1 - zy0 ) / ( 2.0 * BIKE_HF_STEP );
				len = sqrt( n[0] * n[0] + n[1] * n[1] + 1.0 );
				n[0] /= len;
				n[1] /= len;
				n[2] = 1.0 / len;
				if ( n[2] < BIKE_HF_EDGE )
				{	// a step's edge between two flat faces, not a slope: the wheel rides a face
					n[0] = n[1] = 0.0;
					n[2] = 1.0;
				}
			}
		}
		comp = hit ? sag + ( gz - bottom ) / BIKE_UNITS : 0.0;
		w[i].normal[0] = n[0];
		w[i].normal[1] = n[1];
		w[i].normal[2] = n[2];
		w[i].gz = gz;
		if ( !hit || comp <= 0.0 || n[2] <= 0.0 )
		{
			w[i].contact = 0;
			w[i].comp = 0.0;
			w[i].load = 0.0;
			continue;
		}
		// compression rate, m/s (+ compressing): the ground rising under the moving wheel, less the axle rising
		vAxle = bs->velocity[2] + om * along;
		vGround = -( n[0] * bs->velocity[0] + n[1] * bs->velocity[1] ) / n[2];
		rate = ( vGround - vAxle ) / BIKE_UNITS;
		a = fabs( rate );
		lo = ( rate >= 0.0 ) ? bp->dampBumpLow[i] : bp->dampReboundLow[i];
		hi = ( rate >= 0.0 ) ? bp->dampBump[i] : bp->dampRebound[i];
		if ( a <= bp->dampLowRate[i] )
			d = lo;
		else if ( a >= bp->dampHighRate[i] )
			d = hi;
		else
			d = lo + ( hi - lo ) * ( a - bp->dampLowRate[i] ) / ( bp->dampHighRate[i] - bp->dampLowRate[i] );
		// bottomed: the bump stop, its push capped bumpTravel past full travel (a wheel climbing a stair is pushed up
		// hard, not launched)
		a = ( comp < travel + bp->bumpTravel ) ? comp : travel + bp->bumpTravel;
		f = k * a + d * rate;
		if ( a > travel )
			f += bp->bumpStop * k * ( a - travel );
		w[i].contact = 1;
		w[i].comp = comp;
		w[i].load = ( f > 0.0 ) ? f : 0.0;
	}
}

/*
===================
BikeBody

Heave and pitch: the body is rigid about its centre of mass (comLong ahead of the rear axle, comHeight up). The
springs push up at the contacts; the tyres' longitudinal forces act at the ground under the centre of mass (the weight
transfer, m aLong h). On the rear wheel alone (a wheelie) no spring damps the pitch: the rider's pull, the assist and
pitchDamping act. In the air, vehAirControl. Q, the state origin's pivot, follows from the centre of mass's motion,
so only origin z, velocity z, pitch and pitch rate are stored. A parked bike settles to a stop (the deadband) so it
can sleep.
===================
*/
static void BikeBody( bikeState_t *bs, const bikeParams_t *bp, const bikeWheel_t *w, double throttle, double leanIn,
	double aLong, double vAbs, double dt )
{
	double m = bp->mass, inertia = bp->inertiaPitch;
	// the rider's pull and the wheelie assist are angular accelerations about the rear contact (v2 pivoted there)
	double inertiaRear = inertia + m * ( bs->comLong * bs->comLong + bp->comHeight * bp->comHeight );
	double th = bs->pitch * BIKE_D2R, om = bs->pitchRate * BIKE_D2R, c = cos( th ), s = sin( th );
	double yawR = bs->yaw * BIKE_D2R, cy = cos( yawR ), sy = sin( yawR );
	double xc = bs->comLong * BIKE_UNITS, hc = bp->comHeight * BIKE_UNITS;
	double qz = bs->origin[2] - bp->originHeight;
	double comAlong = BIKE_ALONG( xc, hc, c, s ), comUp = BIKE_UP( xc, hc, c, s );
	double fz = -m * bp->gravity, tau = 0.0, slope = 0.0, alpha, aQ, vz, wa, balance, start, target, k, f, bx, r;
	const bikeWheel_t *gw = w[1].contact ? &w[1] : &w[0];
	int i;

	for ( i = 0; i < 2; i++ )
	{
		if ( !w[i].contact )
			continue;
		bx = ( i == 0 ) ? bp->wheelbase * BIKE_UNITS : 0.0;
		r = ( ( i == 0 ) ? bp->tireRadiusF : bp->tireRadiusR ) * BIKE_UNITS;
		fz += w[i].load;
		tau += w[i].load * ( BIKE_ALONG( bx, r, c, s ) - comAlong ) / BIKE_UNITS;
	}
	if ( bs->onGround )
	{
		// the ground's slope along the heading under the rear wheel (else the front): the wheelie's zero
		slope = atan2( -( gw->normal[0] * cy + gw->normal[1] * sy ), gw->normal[2] );
		tau += m * aLong * ( qz + comUp - gw->gz ) / BIKE_UNITS;
		wa = th - slope;
		balance = atan2( bs->comLong, bp->comHeight );
		if ( w[1].contact && leanIn < 0 && throttle > 0 )
		{	// the rider's pull on the bars, fading out toward the balance point: it lifts, it does not loop
			f = 1.0 - wa / balance;
			tau += inertiaRear * -leanIn * throttle * bp->wheelieLean * ( ( f > 0.0 ) ? f : 0.0 );
		}
		if ( w[1].contact && !w[0].contact )
		{
			if ( bp->wheelieAssist )
			{
				start = balance - bp->assistMargin * BIKE_D2R;
				if ( wa > start )
					tau -= inertiaRear * ( bp->assistKp * ( wa - start ) + bp->assistKd * om );
			}
			tau -= inertiaRear * bp->pitchDamping * om;
		}
		alpha = tau / inertia;
	}
	else
	{	// vehAirControl pitch (FB): rate target from the lean input, stabilised toward level without input
		if ( leanIn != 0 )
			target = -leanIn * bp->airPitchMaxVel;
		else if ( bp->airPitchStabilize && fabs( bs->pitch ) > bp->airPitchZeroAngle )
			target = -th * bp->airStabilizeRate;
		else
			target = om;
		k = 1.0 - pow( 1.0 - bp->airPitchInputDamp, dt * bp->frameRate );
		alpha = ( target - om ) * k / dt;
	}

	// Q's vertical acceleration from the centre of mass's (zc = qz + comUp), u/s^2
	aQ = fz / m * BIKE_UNITS - ( comAlong * alpha - comUp * om * om );
	vz = bs->velocity[2] + aQ * dt;
	om += alpha * dt;
	if ( w[0].contact && w[1].contact && vAbs < 0.05 && fabs( vz ) < 0.15 && fabs( om ) < 0.0025 )
	{	// parked and settled
		vz = 0.0;
		om = 0.0;
	}
	th += om * dt;
	if ( bs->onGround && th - slope > bp->maxPitch * BIKE_D2R )
	{
		th = slope + bp->maxPitch * BIKE_D2R;
		om = 0.0;
		bs->events |= BIKE_EV_LOOPOUT;
	}
	bs->velocity[2] = vz;
	bs->pitch = th * BIKE_R2D;
	bs->pitchRate = om * BIKE_R2D;
}

/*
===================
BG_BikeSubstep

One fixed step, SI inside (m, s, kg, N); state positions and velocities in
JKA units. Order: steer slew, surface and rider COM, wheel loads, gears and
drive, resistances and brakes, lateral slip, yaw, tilt, pitch, translate.
dt is BIKE_SUBSTEP_MS except in the harness's naive control.
===================
*/
void BG_BikeSubstep( bikeState_t *bs, const bikeParams_t *bp, const bikeInput_t *in, const bikeWorld_t *world, double dt )
{
	double throttle = BikeClamp( in->throttle, -1.0, 1.0 );
	double steerIn = BikeClamp( in->steer, -1.0, 1.0 );
	double leanIn = BikeClamp( in->lean, -1.0, 1.0 );
	double m = bp->mass, L = bp->wheelbase;
	double steer, rate, yawR, cy, sy, fwd, lat, vAbs;
	double muLatS, muLongS, mid, comT, k, nR, nF, mu;
	double rpmF, ratio, wheelK, fade, launchGrip, fEng, fDrive, grip, fRes, brake, fAir, fGrade, newFwd, dv, aLong;
	double latAbs, tracF, tracR, aLat, dl, newLat, delta, r, cap, target, yaw;
	double vn, lean, err, cmd, lim, vb[2], vbLen;
	bikeWheel_t w[2];
	const double *gr;
	int surf, hb, want, top, limiter, shifting, cooling, wasOnGround, i;

	// 1. steer slew (wheelTurnMaxAdd/SubPerSecond over maxWheelTurnDeg)
	steer = bs->steer;
	if ( fabs( steerIn ) > fabs( steer ) && steerIn * steer >= 0 )
		rate = bp->wheelTurnAdd / bp->maxWheelTurn;
	else
		rate = bp->wheelTurnSub / bp->maxWheelTurn;
	steer = BikeApproach( steer, steerIn, rate * dt );
	bs->steer = steer;

	// 2. planar velocity along the heading, m/s
	yawR = bs->yaw * BIKE_D2R;
	cy = cos( yawR );
	sy = sin( yawR );
	fwd = ( bs->velocity[0] * cy + bs->velocity[1] * sy ) / BIKE_UNITS;
	lat = ( bs->velocity[0] * sy - bs->velocity[1] * cy ) / BIKE_UNITS;
	vAbs = fabs( fwd );

	// 3. rider COM (bikeMax/MinCOMLongOffset, damped by bikeCOMOffsetDampFactor per frame)
	mid = 0.5 * ( bp->comLongBack + bp->comLongFwd );
	if ( leanIn < 0 )
		comT = mid + ( bp->comLongBack - mid ) * ( -leanIn );
	else
		comT = mid + ( bp->comLongFwd - mid ) * leanIn;
	k = 1.0 - pow( 1.0 - bp->comDamp, dt * bp->frameRate );
	bs->comLong += ( comT - bs->comLong ) * k;

	mu = bp->tireFriction;
	hb = in->handbrake ? 1 : 0;

	// 4. the wheels: the ground under each, the suspension, and the springs' loads, which are the tyres' normal
	//    loads below (the weight transfer comes from the body pitching on the springs)
	BikeWheels( bs, bp, world, w );
	wasOnGround = bs->onGround;
	bs->onGround = w[0].contact || w[1].contact;
	if ( bs->onGround && !wasOnGround )
		BG_BikeLand( bs, bp );
	else if ( !bs->onGround && wasOnGround )
		bs->events |= BIKE_EV_AIRBORNE;
	for ( i = 0; i < 2; i++ )
	{
		bs->contact[i] = w[i].contact;
		bs->load[i] = w[i].load;
		bs->comp[i] = w[i].comp;
	}
	nF = w[0].load;
	nR = w[1].load;
	// the surface under the wheels as this substep found them
	surf = world->surface ? world->surface( world->ctx, bs->origin[0], bs->origin[1] ) : BIKE_SURF_ASPHALT;
	if ( surf < 0 || surf >= BIKE_NUM_SURFACES )
		surf = BIKE_SURF_ASPHALT;
	muLatS = bp->surfaces[surf][0];
	muLongS = bp->surfaces[surf][1];

	// 5. gears, rpm, drive (engine torque x curve x gear multiplier x ratio / tyre radius)
	bs->events &= ~BIKE_EV_BURNOUT;
	bs->shiftMs += (int)floor( dt * 1000.0 + 0.5 );
	if ( bs->shiftMs > BIKE_SHIFT_MS_MAX )
		bs->shiftMs = BIKE_SHIFT_MS_MAX;
	shifting = bs->shiftMs < bp->gearChangeTime * 1000.0;
	cooling = bs->shiftMs < bp->gearChangeCooldown * 1000.0;
	top = bp->numGears - 1;
	if ( throttle < 0 && fwd <= bp->reverseThreshold )
	{
		want = 0;
	}
	else if ( bs->gear == 0 && ( throttle > 0 || fwd > bp->reverseThreshold ) )
	{
		want = 1;
	}
	else
	{
		want = bs->gear;
		if ( want >= 1 && !cooling )
		{
			if ( vAbs > bp->gears[want][1] && want < top )
				want++;
			else if ( vAbs < bp->gears[want][0] && want > 1 )
				want--;
		}
	}
	if ( want != bs->gear )
	{
		if ( bs->gear != 0 && want != 0 )
		{
			bs->shiftMs = 0;
			shifting = 1;
			bs->events |= BIKE_EV_SHIFT;
		}
		bs->gear = want;
	}
	gr = bp->gears[bs->gear];
	bs->rpm = BikeGearRPM( bp, bs->gear, vAbs );
	rpmF = BikeClamp( ( bs->rpm - bp->minRPM ) / ( bp->maxRPM - bp->minRPM ), 0.0, 1.0 );
	ratio = bp->wheelsResistanceRatio * ( ( bs->gear == top ) ? bp->finalGearScale : 1.0 );
	wheelK = gr[4] * ratio / bp->tireRadiusR;	// wheel force per engine N m
	// burnout (throttle with the handbrake held, slow): the rear spins in place; releasing launches with grip
	if ( hb && throttle > 0 && vAbs < bp->burnMaxSpeed && w[1].contact )
	{
		bs->burnout = 1;
		bs->events |= BIKE_EV_BURNOUT;
	}
	else if ( bs->burnout )
	{
		bs->burnout = 0;
		if ( throttle > 0 )
			bs->burnLaunch = 1;
	}
	// launch grip: x launchGripMul fading out by burnLaunchSpeed, or a burnout's bonus fading the same way
	fade = 1.0 - vAbs / bp->burnLaunchSpeed;
	if ( fade < 0.0 )
		fade = 0.0;
	if ( bs->burnLaunch && ( throttle <= 0 || fade <= 0.0 ) )
		bs->burnLaunch = 0;
	launchGrip = 1.0 + ( ( bs->burnLaunch ? bp->burnGrip : bp->launchGripMul ) - 1.0 ) * fade;
	fEng = 0.0;
	if ( w[1].contact && !shifting && !bs->burnout )
	{
		limiter = vAbs >= gr[1] && ( bs->gear == 0 || bs->gear == top );
		if ( bs->gear == 0 && throttle < 0 && !limiter )
			fEng = throttle * bp->engineTorque * BikeCurve( &bp->torque, rpmF ) * wheelK;
		else if ( bs->gear >= 1 && throttle > 0 && fwd >= 0 && !limiter )
			fEng = throttle * bp->engineTorque * BikeCurve( &bp->torque, rpmF ) * wheelK;
	}
	brake = 0.0;
	if ( ( throttle < 0 && fwd > bp->reverseThreshold ) || ( throttle > 0 && fwd < 0 ) )
		brake = fabs( throttle );
	fAir = bp->airResistance * fwd * fabs( fwd );

	grip = mu * bp->longMulR * muLongS * nR * launchGrip;
	fDrive = BikeClamp( fEng, -grip, grip );
	// the slope: a spring's load is the vertical part of the ground's push on its tyre, which is along the ground's
	// normal, so its horizontal part pulls the bike down the slope (without it a 25 deg
	// hill climbed faster than the flat, and the acceleration BikeBody transfers weight by was 0.28 g too high on a
	// 16.5 deg road: a full-throttle hill start held a 65 deg wheelie)
	fGrade = 0.0;
	for ( i = 0; i < 2; i++ )
	{
		if ( w[i].contact )
			fGrade += w[i].load * ( w[i].normal[0] * cy + w[i].normal[1] * sy ) / w[i].normal[2];
	}

	// 6. resistances and brakes (all oppose motion)
	fRes = 0.0;
	if ( bs->onGround )
	{
		fRes += bp->rollingCoef * ( nF + nR );
		if ( throttle == 0 && bs->gear >= 1 && w[1].contact )	// engine braking ([interpreted] scaled by rpm above idle)
			fRes += bp->resistanceTorque * rpmF * wheelK;
		if ( brake > 0 )	// [interpreted] friction-limited, scaled by brakingFrictionFactor
			fRes += brake * bp->brakingFriction * mu * muLongS * ( bp->longMulF * nF + bp->longMulR * nR );
		if ( hb && !bs->burnout )
		{
			double hbMax = bp->handbrakeTorque / bp->tireRadiusR;
			double hbGrip = mu * muLongS * bp->hbLongR * bp->longMulR * nR;

			fRes += ( hbMax < hbGrip ) ? hbMax : hbGrip;
		}
		if ( vAbs < 1.0 && throttle == 0 )
		{	// stopping, and stopped: [xenjk] with the slope pulling, a bike let go on a hill holds on its brakes
			// (lowVelStoppingDeceleration alone holds 4.7 deg)
			fRes += bp->lowVelStopDecel * m;
			fRes += bp->brakingFriction * mu * muLongS * ( bp->longMulF * nF + bp->longMulR * nR );
		}
	}
	newFwd = fwd + ( fDrive - fAir + fGrade ) / m * dt;
	dv = fRes / m * dt;
	if ( fabs( newFwd ) <= dv )
		newFwd = 0.0;
	else if ( newFwd > 0 )
		newFwd -= dv;
	else
		newFwd += dv;
	aLong = ( newFwd - fwd ) / dt;

	// 7. lateral: slip decays by the surface's lateral traction curves, front and rear by load
	if ( bs->onGround )
	{
		latAbs = fabs( lat );
		tracF = BikeCurve( &bp->latTraction, latAbs ) * bp->latMulF * nF;
		tracR = BikeCurve( &bp->latTractionRear, latAbs ) * bp->latMulR * nR * ( hb ? bp->hbLatR : 1.0 );
		aLat = mu * muLatS * ( tracF + tracR ) / m;
		dl = aLat * dt;
		lat = ( latAbs <= dl ) ? 0.0 : lat - BikeSign( lat ) * dl;
	}
	newLat = lat;

	// 8. yaw: perfectSteeringFactor 1, the bike follows its front wheel; capped by lateral grip and the
	//    RotationLimiter (maxAngularSpeedRad; the handbrake lets it rotate past grip: a power slide)
	if ( bs->onGround )
	{
		delta = steer * bp->maxWheelTurn * BikeTurnMul( bp, vAbs ) * BIKE_D2R;
		r = newFwd * tan( delta ) / L;
		if ( !hb && vAbs > 0.1 )
		{
			cap = mu * muLatS * ( bp->latMulF * nF + bp->latMulR * nR ) / ( m * vAbs );
			r = BikeClamp( r, -cap, cap );
		}
		r = BikeClamp( r, -bp->maxYawRate, bp->maxYawRate );
		bs->yawRate = r * BIKE_R2D;
	}
	else
	{
		target = steer * bp->airYawMaxVel * BIKE_R2D;
		k = 1.0 - pow( 1.0 - bp->airYawInputDamp, dt * bp->frameRate );
		bs->yawRate += ( target - bs->yawRate ) * k;
	}
	yaw = bs->yaw - bs->yawRate * dt;
	if ( yaw > 180.0 )
		yaw -= 360.0;
	else if ( yaw <= -180.0 )
		yaw += 360.0;
	bs->yaw = yaw;
	// perfectSteeringFactor: the velocity turns with the heading by this share; the handbrake lets the heading
	// turn under it (a slide), which the lateral traction then pulls back
	if ( bs->onGround && !hb )
	{
		double vyaw = yawR - bs->yawRate * dt * bp->perfectSteering * BIKE_D2R;

		cy = cos( vyaw );
		sy = sin( vyaw );
	}
	bs->velocity[0] = ( newFwd * cy + newLat * sy ) * BIKE_UNITS;
	bs->velocity[1] = ( newFwd * sy - newLat * cy ) * BIKE_UNITS;

	// 9. tilt: target from speed_to_tilt x input_to_tilt, rate limited by speed_to_tilt_speed, bikeTiltPID
	vn = newFwd / bp->speedNorm;
	lean = bs->lean;
	if ( bs->onGround )
	{
		target = BikeSign( steer ) * bp->maxTilt * BikeCurve( &bp->speedToTilt, BikeClamp( vn, -1.0, 1.0 ) )
			* BikeCurve( &bp->inputToTilt, fabs( steer ) );
		err = target - lean;
		bs->leanInt = BikeClamp( bs->leanInt + err * dt, -bp->maxTilt, bp->maxTilt );
		cmd = bp->tiltKp * err + bp->tiltKi * bs->leanInt;
		lim = ( ( fabs( target ) > fabs( lean ) ) ? bp->tiltSpeed : bp->tiltReturnSpeed )
			* BikeCurve( &bp->speedToTiltSpeed, BikeClamp( fabs( vn ), 0.0, 1.0 ) );
		cmd = BikeClamp( cmd, -lim, lim );
		lean = BikeClamp( lean + cmd * dt, -bp->maxTilt, bp->maxTilt );
	}
	bs->lean = lean;

	// 10. heave and pitch on the springs (BikeBody)
	BikeBody( bs, bp, w, throttle, leanIn, aLong, vAbs, dt );

	// 11. translate inside the substep (the wheels find the ground at the start of the next)
	vb[0] = bs->velocity[0];
	vb[1] = bs->velocity[1];
	if ( world->translate )
		world->translate( world, bs, bp, dt );
	else
		BG_BikeHeightfieldTranslate( world, bs, bp, dt );

	// 12. a wall stopped what this step's drive added: its push acts at the front tyre's axle height, not at the
	//     ground, so the weight transfer BikeBody gave that acceleration did not happen (pinned against a wall with
	//     the throttle open, the bike would stand on its tail)
	vbLen = sqrt( vb[0] * vb[0] + vb[1] * vb[1] );
	if ( bs->onGround && vbLen > 0.0 )
	{
		double lost = ( vbLen - ( vb[0] * bs->velocity[0] + vb[1] * bs->velocity[1] ) / vbLen ) / BIKE_UNITS;
		double added = fabs( newFwd ) - fabs( fwd );

		if ( lost > added )
			lost = added;
		if ( lost > 0.0 )
			bs->pitchRate -= BikeSign( newFwd ) * m * lost * ( bp->comHeight - bp->tireRadiusF ) / bp->inertiaPitch
				* BIKE_R2D;
	}

	bs->speed = newFwd * BIKE_UNITS;
}

/*
===================
BG_BikeRunCommand

Advances bs from bs->commandTime to serverTime on the absolute 4 ms grid.
Returns the number of substeps run.
===================
*/
int BG_BikeRunCommand( bikeState_t *bs, const bikeParams_t *bp, const bikeInput_t *in, const bikeWorld_t *world, int serverTime )
{
	int n, cap, i;

	bs->events = 0;
	if ( serverTime <= bs->commandTime )
		return 0;

	n = BikeFloorDiv( serverTime, BIKE_SUBSTEP_MS ) - BikeFloorDiv( bs->commandTime, BIKE_SUBSTEP_MS );
	cap = bp->maxCommandMs / BIKE_SUBSTEP_MS;
	if ( n > cap )
	{	// stale or huge command: drop the excess, keep the grid
		bs->droppedMs += ( n - cap ) * BIKE_SUBSTEP_MS;
		bs->events |= BIKE_EV_CAPPED;
		n = cap;
	}
	for ( i = 0; i < n; i++ )
		BG_BikeSubstep( bs, bp, in, world, BIKE_SUBSTEP_MS / 1000.0 );

	bs->commandTime = serverTime;
	return n;
}
