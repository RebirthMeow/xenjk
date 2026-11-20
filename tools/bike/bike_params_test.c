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
 * bike_params_test.c -- the handling-file migration check: a .bike file written by
 * tools/bike/ct3x_params.py --from-header from an old generated bg_bike_ct3x.h must parse (bg_bike.c
 * BG_BikeParseParams over BG_BikeDefaultParams) to exactly the bikeParams_t the old compiled-in defaults built from
 * that header. The header is not in the repository (it is CP2077 data); take the one the game was built with:
 *
 *   python -I tools/bike/ct3x_params.py --from-header OLD/bg_bike_ct3x.h -o ct3x.bike
 *   gcc -O2 -I codemp/game -I OLD codemp/game/bg_bike.c tools/bike/bike_params_test.c -lm -o bike_params_test.exe
 *   bike_params_test.exe ct3x.bike
 *
 * OldDefaultParams is BG_BikeDefaultParams as it was before the handling moved to the file. Exit status 0 and
 * "PASS" when every byte of the two structs matches.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bg_bike.h"
#include "bg_bike_ct3x.h"

#define BIKE_UNITS 37.5

static void SetCurve( bikeCurve_t *c, const double (*pts)[2], int n )
{
	int i;

	memset( c, 0, sizeof( *c ) );
	c->n = n;
	for ( i = 0; i < n; i++ )
	{
		c->pts[i][0] = pts[i][0];
		c->pts[i][1] = pts[i][1];
	}
}

static void OldDefaultParams( bikeParams_t *bp )
{
	int i, j;

	memset( bp, 0, sizeof( *bp ) );
	bp->maxCommandMs		= 250;
	bp->gravity				= 9.81;
	bp->stepHeight			= 18.0;
	bp->originHeight		= 24.0;
	bp->originAhead			= 26.89 / BIKE_UNITS;
	bp->frameClearance		= 0.10;
	bp->wheelClimb			= 48.0;
	bp->mass				= CT3X_mass;
	bp->inertiaPitch		= CT3X_inertiaPitch;
	bp->wheelbase			= 1.82;
	bp->comHeight			= CT3X_comOffsetUp + CT3X_tireRadiusR + 0.08;
	bp->comLongBack			= CT3X_comLongBack;
	bp->comLongFwd			= CT3X_comLongFwd;
	bp->comDamp				= CT3X_comDamp;
	bp->airResistance		= CT3X_airResistance;
	bp->forwardWeightTransfer = CT3X_forwardWeightTransfer;
	bp->lowVelStopDecel		= CT3X_lowVelStopDecel;
	bp->brakingFriction		= CT3X_brakingFriction;
	bp->handbrakeTorque		= CT3X_handbrakeTorque;
	bp->frameRate			= 60.0;
	bp->maxWheelTurn		= CT3X_maxWheelTurn;
	bp->wheelTurnAdd		= CT3X_wheelTurnAdd;
	bp->wheelTurnSub		= CT3X_wheelTurnSub;
	bp->turnBaseSpeed		= CT3X_turnBaseSpeed;
	bp->turnMaxSpeed		= CT3X_turnMaxSpeed;
	bp->turnMaxSpeedMul		= CT3X_turnMaxSpeedMul;
	bp->maxYawRate			= CT3X_maxYawRate;
	bp->perfectSteering		= CT3X_perfectSteering;
	bp->maxTilt				= CT3X_maxTilt;
	bp->tiltSpeed			= CT3X_tiltSpeed;
	bp->tiltReturnSpeed		= CT3X_tiltReturnSpeed;
	bp->tiltKp				= CT3X_tiltKp;
	bp->tiltKi				= CT3X_tiltKi;
	bp->suspTravel[0]		= CT3X_suspTravelF;
	bp->suspTravel[1]		= CT3X_suspTravelR;
	bp->suspOmega[0]		= CT3X_springStiffnessF;
	bp->suspOmega[1]		= CT3X_springStiffnessR;
	bp->dampBump[0]			= CT3X_dampBumpF;
	bp->dampBump[1]			= CT3X_dampBumpR;
	bp->dampBumpLow[0]		= CT3X_dampBumpLowF;
	bp->dampBumpLow[1]		= CT3X_dampBumpLowR;
	bp->dampRebound[0]		= CT3X_dampReboundF;
	bp->dampRebound[1]		= CT3X_dampReboundR;
	bp->dampReboundLow[0]	= CT3X_dampReboundLowF;
	bp->dampReboundLow[1]	= CT3X_dampReboundLowR;
	bp->dampLowRate[0]		= CT3X_dampLowRateF;
	bp->dampLowRate[1]		= CT3X_dampLowRateR;
	bp->dampHighRate[0]		= CT3X_dampHighRateF;
	bp->dampHighRate[1]		= CT3X_dampHighRateR;
	bp->bumpStop			= 10.0;
	bp->bumpTravel			= 0.10;
	bp->tireRadiusF			= CT3X_tireRadiusF;
	bp->tireRadiusR			= CT3X_tireRadiusR;
	bp->tireFriction		= CT3X_tireFriction;
	bp->latMulF				= CT3X_latMulF;
	bp->latMulR				= CT3X_latMulR;
	bp->longMulF			= CT3X_longMulF;
	bp->longMulR			= CT3X_longMulR;
	bp->rollingCoef			= CT3X_rollingCoef;
	bp->hbLatR				= CT3X_hbLatR;
	bp->hbLongR				= CT3X_hbLongR;
	bp->burnGrip			= CT3X_burnGrip;
	bp->burnLaunchSpeed		= CT3X_burnLaunchSpeed;
	bp->burnMaxSpeed		= CT3X_burnMaxSpeed;
	bp->launchGripMul		= CT3X_launchGripMul;
	for ( i = 0; i < BIKE_NUM_SURFACES; i++ )
	{
		bp->surfaces[i][0] = ct3xSurfaces[i][0];
		bp->surfaces[i][1] = ct3xSurfaces[i][1];
	}
	bp->engineTorque		= CT3X_engineTorque;
	bp->resistanceTorque	= CT3X_resistanceTorque;
	bp->wheelsResistanceRatio = CT3X_wheelsResistanceRatio;
	bp->finalGearScale		= CT3X_finalGearScale;
	bp->minRPM				= CT3X_minRPM;
	bp->maxRPM				= CT3X_maxRPM;
	bp->gearChangeTime		= CT3X_gearChangeTime;
	bp->gearChangeCooldown	= CT3X_gearChangeCooldown;
	bp->numGears			= CT3X_NUM_GEARS;
	for ( i = 0; i < CT3X_NUM_GEARS; i++ )
		for ( j = 0; j < 5; j++ )
			bp->gears[i][j] = ct3xGears[i][j];
	bp->speedNorm			= ct3xGears[CT3X_NUM_GEARS - 1][1];
	bp->reverseThreshold	= 0.5;
	SetCurve( &bp->torque, ct3xCurve_torque, CT3X_CURVE_TORQUE_N );
	SetCurve( &bp->speedToTilt, ct3xCurve_speedToTilt, CT3X_CURVE_SPEEDTOTILT_N );
	SetCurve( &bp->inputToTilt, ct3xCurve_inputToTilt, CT3X_CURVE_INPUTTOTILT_N );
	SetCurve( &bp->speedToTiltSpeed, ct3xCurve_speedToTiltSpeed, CT3X_CURVE_SPEEDTOTILTSPEED_N );
	SetCurve( &bp->latTraction, ct3xCurve_latTraction, CT3X_CURVE_LATTRACTION_N );
	SetCurve( &bp->latTractionRear, ct3xCurve_latTractionRear, CT3X_CURVE_LATTRACTIONREAR_N );
	bp->wheelieLean			= 4.0;
	bp->wheelieAssist		= 1;
	bp->assistMargin		= 12.0;
	bp->assistKp			= 40.0;
	bp->assistKd			= 8.0;
	bp->pitchDamping		= 1.5;
	bp->maxPitch			= 70.0;
	bp->landBailPitch		= 45.0;
	bp->airPitchMaxVel		= CT3X_airPitchMaxVel;
	bp->airPitchInputDamp	= CT3X_airPitchInputDamp;
	bp->airPitchStabilize	= (int)CT3X_airPitchStabilize;
	bp->airPitchZeroAngle	= CT3X_airPitchZeroAngle;
	bp->airStabilizeRate	= 1.0;
	bp->airYawMaxVel		= CT3X_airYawMaxVel;
	bp->airYawInputDamp		= CT3X_airYawInputDamp;
}

int main( int argc, char **argv )
{
	static bikeParams_t	oldp, newp, defp;
	static char			text[64 * 1024];
	char				bad[64];
	FILE				*f;
	size_t				len, i, diffs = 0;
	int					errors;

	if ( argc < 2 || !( f = fopen( argv[1], "rb" ) ) )
	{
		fprintf( stderr, "usage: bike_params_test FILE.bike\n" );
		return 2;
	}
	len = fread( text, 1, sizeof( text ) - 1, f );
	fclose( f );
	text[len] = 0;

	OldDefaultParams( &oldp );
	memset( &newp, 0, sizeof( newp ) );
	BG_BikeDefaultParams( &newp );
	errors = BG_BikeParseParams( &newp, text, bad, sizeof( bad ) );
	printf( "parse: %d lines not understood%s%s\n", errors, errors ? ", first " : "", errors ? bad : "" );
	for ( i = 0; i < sizeof( oldp ); i++ )
	{
		if ( ( (unsigned char *)&oldp )[i] != ( (unsigned char *)&newp )[i] )
		{
			if ( diffs < 10 )
				printf( "byte %u of %u differs\n", (unsigned)i, (unsigned)sizeof( oldp ) );
			diffs++;
		}
	}
	// the file must matter: the generic bike differs from it
	BG_BikeDefaultParams( &defp );
	printf( "generic bike differs from the file's: %s\n", memcmp( &defp, &newp, sizeof( defp ) ) ? "yes" : "NO" );
	printf( "%s: %u differing bytes\n", ( diffs || errors ) ? "FAIL" : "PASS", (unsigned)diffs );
	return ( diffs || errors ) ? 1 : 0;
}
