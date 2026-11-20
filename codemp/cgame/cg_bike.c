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

// cg_bike.c -- bike visuals in cgame (wishlist item 1): wheels spin with the ground
// speed and the handlebar turns with the steer, on the CT-3X's own bones
// (tools/bike/ct3x_model.py: the CP2077 rig, local frames along the model axes:
// x left, y back, z up); the bike is drawn pitched and leaned about its rear tyre's
// contact patch and its rider follows. Purely visual; nothing here feeds back into
// movement, prediction or the server's boxes.

#include "cg_local.h"
#include "ghoul2/G2.h"
#include "game/bg_bike.h"

#define BIKE_TIRE_FRONT_U	( 0.305f * 37.5f )	// CP2077 VehicleWheelDimensionsDefaultYaibaKusanagi
#define BIKE_TIRE_REAR_U	( 0.34f * 37.5f )


static float	bikeSpinF[MAX_GENTITIES], bikeSpinR[MAX_GENTITIES];
static int		bikeSpinTime[MAX_GENTITIES];

#define BIKE_FRONT_ARM_U	14.27f	// the front swingarm's reach, suspension_front to wheel_front along the bike (the bolts)

/*
===============
CG_BikeSound

The bike's sound from CP2077's own (tools/bike/ct3x_audio.py renders them; the pack's .veh carries no engine loop):
  engine   one of BIKE_ENGINE_STEPS loops of one length, picked by rpm, on load (throttle) or off. The loops sit on a
           firing-frequency ladder from BIKE_FIRE_LO to BIKE_FIRE_HI; rpm maps onto it linearly (idle..max). Loops
           of one length mix from the same position, so a step is a pitch step, not a restart.
           My bike: its predicted gear and speed, my throttle. Others: the gear its speed falls in, on load while it
           speeds up.
  tyres    a rolling loop on the ground above BIKE_ROLL_MS (CP2077's terrain one on dirt, grass, sand and the like);
           a squeal loop while sliding sideways past BIKE_SKID_MS, or in a burnout
  events   boarding: the ignition, the engine start BIKE_START_MS later (the engine runs from then while someone
           rides); getting off: the engine stop, CP2077's standby BIKE_STANDBY_MS later. A gear change (my bike), a
           landing after a fall faster than BIKE_LAND_U
Looping sounds have no pitch or volume control here, hence the pre-rendered steps.
===============
*/
#define BIKE_ENGINE_STEPS	48
#define BIKE_FIRE_LO		40.0f	// Hz at minRPM and maxRPM (ct3x_audio.py FIRE_LO, FIRE_HI)
#define BIKE_FIRE_HI		110.0f
#define BIKE_ROLL_MS		1.5f
#define BIKE_SKID_MS		2.5f	// sideways speed
#define BIKE_LAND_U			220.0f	// u/s down at touchdown
#define BIKE_START_MS		700
#define BIKE_STANDBY_MS		2500

static struct {
	qboolean		registered;
	sfxHandle_t		engOn[BIKE_ENGINE_STEPS], engOff[BIKE_ENGINE_STEPS];
	sfxHandle_t		roll, rollTerrain, skid, gearUp, gearDown, land[3], ignition, start, stop, standby;
} bikeSfx;

static struct {
	float	speed, vz;
	int		onGround, gear, time;
	int		piloted, startAt, standbyAt;	// the engine runs from startAt while piloted
} bikeSnd[MAX_GENTITIES];

// at load, from CG_CacheG2AnimInfo for a VH_BIKE vehicle in the model configstrings, so the first bike drawn does not
// register them mid-game; CG_BikeSound still registers on first use for a bike that was not precached
void CG_BikeRegisterSounds( void )
{
	int i;

	if ( bikeSfx.registered )
		return;
	for ( i = 0; i < BIKE_ENGINE_STEPS; i++ )
	{
		bikeSfx.engOn[i] = trap->S_RegisterSound( va( "sound/vehicles/ct3x/eng_on_%02d.wav", i ) );
		bikeSfx.engOff[i] = trap->S_RegisterSound( va( "sound/vehicles/ct3x/eng_off_%02d.wav", i ) );
	}
	bikeSfx.roll = trap->S_RegisterSound( "sound/vehicles/ct3x/roll.wav" );
	bikeSfx.rollTerrain = trap->S_RegisterSound( "sound/vehicles/ct3x/roll_terrain.wav" );
	bikeSfx.ignition = trap->S_RegisterSound( "sound/vehicles/ct3x/ignition.wav" );
	bikeSfx.start = trap->S_RegisterSound( "sound/vehicles/ct3x/start.wav" );
	bikeSfx.stop = trap->S_RegisterSound( "sound/vehicles/ct3x/stop.wav" );
	bikeSfx.standby = trap->S_RegisterSound( "sound/vehicles/ct3x/standby.wav" );
	bikeSfx.skid = trap->S_RegisterSound( "sound/vehicles/ct3x/skid.wav" );
	bikeSfx.gearUp = trap->S_RegisterSound( "sound/vehicles/ct3x/gear_up.wav" );
	bikeSfx.gearDown = trap->S_RegisterSound( "sound/vehicles/ct3x/gear_down.wav" );
	for ( i = 0; i < 3; i++ )
		bikeSfx.land[i] = trap->S_RegisterSound( va( "sound/vehicles/ct3x/land%d.wav", i ) );
	bikeSfx.registered = qtrue;
}

static void CG_BikeSound( centity_t *cent, const bikeParams_t *bp )
{
	const float	U = 37.5f;
	int			n = cent->currentState.number, gear, step, burnout = 0, onGround, mine, piloted, terrain = 0;
	float		speed, lat, rpm, fire, dt, yaw;
	qboolean	onLoad;
	vec3_t		vel, end;
	usercmd_t	cmd;
	trace_t		tr;

	if ( !bikeSfx.registered )
		CG_BikeRegisterSounds();
	mine = ( cg.predictedPlayerState.m_iVehicleNum == n );
	yaw = DEG2RAD( cent->lerpAngles[YAW] );
	if ( mine )
	{	// my bike: its predicted state
		const playerState_t *vps = &cg.predictedVehicleState;
		int packed = (int)vps->moveDir[2];

		VectorCopy( vps->velocity, vel );
		speed = vps->speed / U;
		gear = packed & 7;
		burnout = ( packed >> 3 ) & 1;
		onGround = ( vps->groundEntityNum != ENTITYNUM_NONE );
		trap->GetUserCmd( trap->GetCurrentCmdNumber(), &cmd );
		onLoad = ( cmd.forwardmove > 0 );
		if ( gear != bikeSnd[n].gear && bikeSnd[n].time && gear > 0 && bikeSnd[n].gear > 0 )
			trap->S_StartSound( NULL, n, CHAN_AUTO, gear > bikeSnd[n].gear ? bikeSfx.gearUp : bikeSfx.gearDown );
	}
	else
	{	// others: the trajectory; the gear its speed falls in
		VectorCopy( cent->currentState.pos.trDelta, vel );
		speed = ( vel[0] * cosf( yaw ) + vel[1] * sinf( yaw ) ) / U;
		for ( gear = 1; gear < bp->numGears - 1 && fabsf( speed ) > bp->gears[gear][1]; gear++ )
			;
		onGround = ( cent->currentState.groundEntityNum != ENTITYNUM_NONE );
		dt = ( cg.time - bikeSnd[n].time ) * 0.001f;
		onLoad = ( dt > 0.0f && dt < 0.5f && fabsf( speed ) > fabsf( bikeSnd[n].speed ) + 0.5f * dt );
	}
	lat = fabsf( vel[0] * sinf( yaw ) - vel[1] * cosf( yaw ) ) / U;

	// someone getting on or off (the vehicle's m_iVehicleNum is its pilot + 1)
	piloted = ( cent->currentState.m_iVehicleNum != 0 );
	if ( piloted && !bikeSnd[n].piloted )
	{
		trap->S_StartSound( NULL, n, CHAN_AUTO, bikeSfx.ignition );
		bikeSnd[n].startAt = cg.time + BIKE_START_MS;
		bikeSnd[n].standbyAt = 0;
	}
	else if ( !piloted && bikeSnd[n].piloted )
	{
		trap->S_StartSound( NULL, n, CHAN_AUTO, bikeSfx.stop );
		bikeSnd[n].standbyAt = cg.time + BIKE_STANDBY_MS;
	}
	bikeSnd[n].piloted = piloted;
	if ( bikeSnd[n].startAt && cg.time >= bikeSnd[n].startAt )
	{
		trap->S_StartSound( NULL, n, CHAN_AUTO, bikeSfx.start );
		bikeSnd[n].startAt = 0;
	}
	if ( bikeSnd[n].standbyAt && cg.time >= bikeSnd[n].standbyAt )
	{
		trap->S_StartSound( NULL, n, CHAN_AUTO, bikeSfx.standby );
		bikeSnd[n].standbyAt = 0;
	}

	// the engine, while ridden and started
	rpm = burnout ? (float)bp->maxRPM * 0.8f : (float)BG_BikeGearRPM( bp, gear, fabsf( speed ) );
	fire = BIKE_FIRE_LO + ( BIKE_FIRE_HI - BIKE_FIRE_LO ) * ( rpm - (float)bp->minRPM ) / (float)( bp->maxRPM - bp->minRPM );
	step = (int)( logf( fire / BIKE_FIRE_LO ) / logf( BIKE_FIRE_HI / BIKE_FIRE_LO ) * ( BIKE_ENGINE_STEPS - 1 ) + 0.5f );
	if ( step < 0 )
		step = 0;
	else if ( step > BIKE_ENGINE_STEPS - 1 )
		step = BIKE_ENGINE_STEPS - 1;
	if ( piloted && !bikeSnd[n].startAt )
		trap->S_AddLoopingSound( n, cent->lerpOrigin, vel, onLoad ? bikeSfx.engOn[step] : bikeSfx.engOff[step] );

	// the tyres, and the ground's material under the bike
	if ( onGround && fabsf( speed ) > BIKE_ROLL_MS )
	{
		VectorCopy( cent->lerpOrigin, end );
		end[2] -= 48.0f;
		CG_Trace( &tr, cent->lerpOrigin, NULL, NULL, end, n, MASK_PLAYERSOLID );
		switch ( tr.fraction < 1.0f ? tr.surfaceFlags & MATERIAL_MASK : MATERIAL_NONE )
		{
		case MATERIAL_SAND:
		case MATERIAL_SHORTGRASS:
		case MATERIAL_LONGGRASS:
		case MATERIAL_DIRT:
		case MATERIAL_GRAVEL:
		case MATERIAL_MUD:
		case MATERIAL_SNOW:
		case MATERIAL_DRYLEAVES:
		case MATERIAL_GREENLEAVES:
		case MATERIAL_ROCK:
			terrain = 1;
			break;
		default:
			break;
		}
	}
	if ( onGround && ( lat > BIKE_SKID_MS || burnout ) )
		trap->S_AddLoopingSound( n, cent->lerpOrigin, vel, bikeSfx.skid );
	else if ( onGround && fabsf( speed ) > BIKE_ROLL_MS )
		trap->S_AddLoopingSound( n, cent->lerpOrigin, vel, terrain ? bikeSfx.rollTerrain : bikeSfx.roll );

	// a landing
	if ( onGround && !bikeSnd[n].onGround && bikeSnd[n].time && bikeSnd[n].vz < -BIKE_LAND_U )
		trap->S_StartSound( NULL, n, CHAN_AUTO, bikeSfx.land[( cg.time >> 4 ) % 3] );

	bikeSnd[n].speed = speed;
	bikeSnd[n].vz = vel[2];
	bikeSnd[n].onGround = onGround;
	bikeSnd[n].gear = gear;
	bikeSnd[n].time = cg.time;
}

/*
===============
CG_BikeFrontArm

The front wheel on its own ground: the hub-centre swingarm (suspension_front) swings the wheel up by the suspension's
compression past its sag, as bg_bike.c BikeWheels finds it (the same probe, under the bike as drawn), so the tyre stays
on the road in a dive and drops onto it off a crest. The rear has no bone that moves its wheel up (suspension_back
sits straight above the axle and cgame can only turn bones), so the rear tyre moves with the body.
===============
*/
static void CG_BikeFrontArm( centity_t *cent, const bikeParams_t *bp )
{
	static const vec3_t	patchMins = { -4.0f, -3.0f, 0.0f }, patchMaxs = { 4.0f, 3.0f, 0.0f };
	const float	U = 37.5f;
	float		th = DEG2RAD( -cent->lerpAngles[PITCH] ), c = cosf( th ), s = sinf( th );
	float		yaw = DEG2RAD( cent->lerpAngles[YAW] );
	float		r = (float)bp->tireRadiusF * U, L = (float)bp->wheelbase * U;
	float		sag = (float)( bp->gravity / ( bp->suspOmega[0] * bp->suspOmega[0] ) ), travel = (float)bp->suspTravel[0];
	float		along, bottom, comp;
	vec3_t		start, end, ang;
	trace_t		tr;

	if ( !cg_bikeFrontArm.integer )
		return;
	along = L * c - r * s - (float)bp->originAhead * U;
	bottom = cent->lerpOrigin[2] - (float)bp->originHeight + L * s + r * c - r;
	VectorSet( start, cent->lerpOrigin[0] + cosf( yaw ) * along, cent->lerpOrigin[1] + sinf( yaw ) * along,
		bottom + ( travel - sag ) * U + (float)bp->stepHeight );
	VectorSet( end, start[0], start[1], bottom - sag * U );
	CG_Trace( &tr, start, patchMins, patchMaxs, end, cent->currentState.number, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid )
		comp = travel;
	else if ( tr.fraction < 1.0f )
		comp = sag + ( tr.endpos[2] - bottom ) / U;
	else
		comp = 0.0f;
	if ( comp < 0.0f )
		comp = 0.0f;
	if ( comp > travel )
		comp = travel;
	// a forward arm turned the wheel's way (top forward) drops its front end: up is the other way
	VectorSet( ang, -RAD2DEG( ( comp - sag ) * U / BIKE_FRONT_ARM_U ), 0, 0 );
	trap->G2API_SetBoneAngles( cent->ghoul2, 0, "suspension_front", ang, BONE_ANGLES_POSTMULT, NEGATIVE_Z, POSITIVE_X,
		POSITIVE_Y, NULL, 0, cg.time );
	if ( cg_bikeSeatDebug.integer > 0 && ( cg.time / cg_bikeSeatDebug.integer ) != ( cg.oldTime / cg_bikeSeatDebug.integer ) )
	{
		mdxaBone_t	m;
		vec3_t		p;
		int			bolt = trap->G2API_AddBolt( cent->ghoul2, 0, "wheel_front" );

		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, vec3_origin, vec3_origin, cg.time, cgs.gameModels,
			cent->modelScale );
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, p );
		trap->Print( "BIKEARM t=%d comp=%.3f angle=%.2f wheel_front=(%.2f %.2f %.2f) axis0=(%.2f %.2f %.2f)\n", cg.time,
			comp, ang[0], p[0], p[1], p[2], m.matrix[0][0], m.matrix[1][0], m.matrix[2][0] );
	}
}

// the handling the bike's pmove integrates (its vehicle's .bike, or the generic bike)
static const bikeParams_t *CG_BikeParamsFor( const centity_t *cent )
{
	return BG_BikeParams( ( cent->m_pVehicle ) ? cent->m_pVehicle->m_pVehicleInfo : NULL );
}

void CG_BikeBones( centity_t *cent )
{
	const bikeParams_t *bp = CG_BikeParamsFor( cent );
	int		n = cent->currentState.number;
	float	speed, steer, dt, turn;
	vec3_t	fwd, ang;

	if ( !cent->ghoul2 || n < 0 || n >= MAX_GENTITIES )
		return;
	if ( cg.predictedPlayerState.m_iVehicleNum == n )
	{	// my own bike: its predicted state
		speed = cg.predictedVehicleState.speed;
		steer = cg.predictedVehicleState.hyperSpaceAngles[1];
	}
	else
	{	// others: forward ground speed from the trajectory; the steer is not in the entity state
		AngleVectors( cent->lerpAngles, fwd, NULL, NULL );
		speed = DotProduct( cent->currentState.pos.trDelta, fwd );
		steer = 0.0f;
	}
	dt = ( cg.time - bikeSpinTime[n] ) * 0.001f;
	if ( dt < 0.0f || dt > 0.2f )
		dt = 0.0f;
	bikeSpinTime[n] = cg.time;
	bikeSpinF[n] = AngleNormalize360( bikeSpinF[n] + speed * dt / BIKE_TIRE_FRONT_U * ( 180.0f / M_PI ) );
	bikeSpinR[n] = AngleNormalize360( bikeSpinR[n] + speed * dt / BIKE_TIRE_REAR_U * ( 180.0f / M_PI ) );

	// pitch rotates about the bone's left axis (model x), yaw about up (model z)
	VectorSet( ang, bikeSpinF[n], 0, 0 );
	trap->G2API_SetBoneAngles( cent->ghoul2, 0, "wheel_front", ang, BONE_ANGLES_POSTMULT, POSITIVE_Z, POSITIVE_X, NEGATIVE_Y, NULL, 0, cg.time );
	// wheel_front_spring is wheel_front's child on the same base frame, and the fork (axle_f: the hub carrier and the
	// fork's lower legs) is weighted to it: turned back, only the wheel and tyre parts (wheel_f, tire_f) spin.
	// With this axis mapping
	// G2_Generate_Matrix builds Euler (180, 0, a): a half turn times the spin, which is its own inverse, so the same
	// angle cancels it (a negated one doubled the fork's spin)
	VectorSet( ang, bikeSpinF[n], 0, 0 );
	trap->G2API_SetBoneAngles( cent->ghoul2, 0, "wheel_front_spring", ang, BONE_ANGLES_POSTMULT, POSITIVE_Z, POSITIVE_X, NEGATIVE_Y, NULL, 0, cg.time );
	VectorSet( ang, bikeSpinR[n], 0, 0 );
	trap->G2API_SetBoneAngles( cent->ghoul2, 0, "wheel_back", ang, BONE_ANGLES_POSTMULT, POSITIVE_Z, POSITIVE_X, NEGATIVE_Y, NULL, 0, cg.time );
	// the handlebar: the slewed steer at full lock (the lock the physics uses shrinks with speed; the bars show the input)
	turn = -steer * (float)bp->maxWheelTurn * 0.5f;
	VectorSet( ang, 0, turn, 0 );
	trap->G2API_SetBoneAngles( cent->ghoul2, 0, "steering_wheel_turn", ang, BONE_ANGLES_POSTMULT, POSITIVE_Z, POSITIVE_X, NEGATIVE_Y, NULL, 0, cg.time );
	CG_BikeFrontArm( cent, bp );
	CG_BikeSound( cent, bp );
}

/*
===============
CG_BikePose

The bike as drawn: its full orientation (the wheelie's pitch and the lean's roll) about the rear tyre's contact patch,
which stays on the ground where the physics has it (bg_bike.c pivots the wheelie there). The stock speeder path draws
yaw and roll about the origin, 24 u up, and no pitch. ent is the refEntity CG_G2PlayerAngles set up (the origin, the
upright frame); the motion is kept for the riders (CG_BikeRiderPose), which cg_ents.c adds after their vehicle.
===============
*/
static struct {
	int			time;			// cg.time of the frame it holds
	vec3_t		from, to;		// the origin upright, and as drawn
	matrix3_t	upright, axis;	// yaw only, and the full orientation
} bikePose[MAX_GENTITIES];

void CG_BikePose( centity_t *cent, refEntity_t *ent )
{
	int			n = cent->currentState.number, bolt, i;
	vec3_t		angles, local, contact;
	mdxaBone_t	m;

	if ( !cent->ghoul2 || n < 0 || n >= MAX_GENTITIES )
		return;
	VectorSet( angles, 0, cent->lerpAngles[YAW], 0 );
	AnglesToAxis( angles, bikePose[n].upright );
	AnglesToAxis( cent->lerpAngles, bikePose[n].axis );

	// the contact patch in the bike's frame: under the rear axle, on the model's ground (the box bottom,
	// ct3x_model.py BOX_BOTTOM_U); a model without the bone pivots under its origin
	VectorClear( local );
	bolt = trap->G2API_AddBolt( cent->ghoul2, 0, "wheel_back" );
	if ( bolt >= 0 )
	{
		trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, vec3_origin, vec3_origin, cg.time, cgs.gameModels,
			cent->modelScale );
		BG_GiveMeVectorFromMatrix( &m, ORIGIN, local );
	}
	local[2] = DEFAULT_MINS_2 * ( cent->modelScale[2] ? cent->modelScale[2] : 1.0f );

	// the contact where the upright bike has it, then the origin that puts it there in the full orientation
	VectorCopy( ent->origin, bikePose[n].from );
	VectorCopy( ent->origin, contact );
	for ( i = 0; i < 3; i++ )
		VectorMA( contact, local[i], bikePose[n].upright[i], contact );
	VectorCopy( contact, bikePose[n].to );
	for ( i = 0; i < 3; i++ )
		VectorMA( bikePose[n].to, -local[i], bikePose[n].axis[i], bikePose[n].to );
	bikePose[n].time = cg.time;

	VectorCopy( bikePose[n].to, ent->origin );
	VectorCopy( bikePose[n].to, ent->oldorigin );
	AxisCopy( bikePose[n].axis, ent->axis );

	if ( cg_bikeSeatDebug.integer > 0 && ( cg.time / cg_bikeSeatDebug.integer ) != ( cg.oldTime / cg_bikeSeatDebug.integer ) )
	{	// the wheels on screen (640x480), for tools/bike/wheel_gate.py: centre and tyre radius
		static const char	*wheels[2] = { "wheel_front", "wheel_back" };
		const float			radius[2] = { (float)CG_BikeParamsFor( cent )->tireRadiusF * 37.5f,
			(float)CG_BikeParamsFor( cent )->tireRadiusR * 37.5f };
		float				x, y, xt, yt;
		vec3_t				w, top;
		int					k;

		for ( k = 0; k < 2; k++ )
		{
			bolt = trap->G2API_AddBolt( cent->ghoul2, 0, wheels[k] );
			trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, vec3_origin, vec3_origin, cg.time, cgs.gameModels,
				cent->modelScale );
			BG_GiveMeVectorFromMatrix( &m, ORIGIN, local );
			VectorCopy( ent->origin, w );
			for ( i = 0; i < 3; i++ )
				VectorMA( w, local[i], ent->axis[i], w );
			VectorMA( w, radius[k], ent->axis[2], top );
			if ( CG_WorldCoordToScreenCoordFloat( w, &x, &y ) && CG_WorldCoordToScreenCoordFloat( top, &xt, &yt ) )
				trap->Print( "BIKESCREEN t=%d %s x=%.1f y=%.1f r=%.1f\n", cg.time, wheels[k], x, y, fabsf( yt - y ) );
		}
	}
}

// the bike's motion applied to a vector of the upright frame
static void CG_BikeRotate( int n, const vec3_t in, vec3_t out )
{
	vec3_t	v;
	int		i;

	VectorClear( v );
	for ( i = 0; i < 3; i++ )
		VectorMA( v, DotProduct( in, bikePose[n].upright[i] ), bikePose[n].axis[i], v );
	VectorCopy( v, out );
}

// a bolt's world position at an upright pose (yaw only)
static qboolean CG_BikeBoltAt( centity_t *cent, const char *name, const vec3_t angles, const vec3_t origin, vec3_t out )
{
	mdxaBone_t	m;
	int			bolt;

	if ( !cent->ghoul2 || ( bolt = trap->G2API_AddBolt( cent->ghoul2, 0, name ) ) < 0 )
		return qfalse;
	trap->G2API_GetBoltMatrix( cent->ghoul2, 0, bolt, &m, angles, origin, cg.time, cgs.gameModels, cent->modelScale );
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, out );
	return qtrue;
}

/*
===============
CG_BikeSeatDebug

cg_bikeSeatDebug: every that many ms, how the rider sits on the bike, in the bike's upright frame (forward, left, up,
in u): the rider's hands from the handlebar grips (*bar_l, *bar_r), its pelvis from the seat tag (*driver, where
AttachRiders puts its origin). A rider set too far back shows as hands behind the grips (forward < 0).
===============
*/
static void CG_BikeSeatDebug( centity_t *rider, centity_t *bike, const vec3_t riderAngles, const vec3_t riderOrigin,
	int n )
{
	static int	next;
	vec3_t		bikeAngles, barL, barR, handL, handR, seat, pelvis, d;
	const char	*label[2] = { "hands-grips", "pelvis-seat" };
	int			k;

	if ( cg.time < next && next - cg.time <= cg_bikeSeatDebug.integer )
		return;
	if ( !next )
	{	// once: the bike's bones in model space (zero angles, origin at 0): the wheel and suspension geometry
		static const char *bones[] = { "wheel_back", "wheel_front", "axle_back", "suspension_back", "suspension_front",
			"*driver", "*bar_l", "*bar_r" };
		int b;

		for ( b = 0; b < (int)ARRAY_LEN( bones ); b++ )
		{
			vec3_t p;

			if ( CG_BikeBoltAt( bike, bones[b], vec3_origin, vec3_origin, p ) )
				trap->Print( "BIKEBONE %s fwd=%.2f left=%.2f up=%.2f\n", bones[b], p[0], p[1], p[2] );
		}
	}
	next = cg.time + cg_bikeSeatDebug.integer;
	VectorSet( bikeAngles, 0, bike->lerpAngles[YAW], 0 );
	if ( !CG_BikeBoltAt( bike, "*bar_l", bikeAngles, bikePose[n].from, barL ) || !CG_BikeBoltAt( bike, "*bar_r", bikeAngles,
			bikePose[n].from, barR ) || !CG_BikeBoltAt( bike, "*driver", bikeAngles, bikePose[n].from, seat )
		|| !CG_BikeBoltAt( rider, "lhand", riderAngles, riderOrigin, handL ) || !CG_BikeBoltAt( rider, "rhand", riderAngles,
			riderOrigin, handR ) || !CG_BikeBoltAt( rider, "pelvis", riderAngles, riderOrigin, pelvis ) )
		return;
	for ( k = 0; k < 2; k++ )
	{
		if ( k == 0 )
		{
			VectorAdd( handL, handR, d );
			VectorSubtract( d, barL, d );
			VectorSubtract( d, barR, d );
			VectorScale( d, 0.5f, d );
		}
		else
		{
			VectorSubtract( pelvis, seat, d );
		}
		trap->Print( "BIKESEAT t=%d rider=%d anim=%d %s fwd=%.2f left=%.2f up=%.2f\n", cg.time, rider->currentState.number,
			rider->currentState.legsAnim, label[k], DotProduct( d, bikePose[n].upright[0] ),
			DotProduct( d, bikePose[n].upright[1] ), DotProduct( d, bikePose[n].upright[2] ) );
	}
	if ( rider->currentState.number == cg.predictedPlayerState.clientNum && n == cg.predictedPlayerState.m_iVehicleNum )
	{	// my own ride: the drawn origins against the predicted states, along the bike (u): which one is late
		trap->Print( "BIKESYNC t=%d rider-ps=%.2f bike-vps=%.2f ps-vps=%.2f rider-bike=%.2f\n", cg.time,
			DotProduct( riderOrigin, bikePose[n].upright[0] ) - DotProduct( cg.predictedPlayerState.origin, bikePose[n].upright[0] ),
			DotProduct( bike->lerpOrigin, bikePose[n].upright[0] ) - DotProduct( cg.predictedVehicleState.origin, bikePose[n].upright[0] ),
			DotProduct( cg.predictedPlayerState.origin, bikePose[n].upright[0] ) - DotProduct( cg.predictedVehicleState.origin, bikePose[n].upright[0] ),
			DotProduct( riderOrigin, bikePose[n].upright[0] ) - DotProduct( bike->lerpOrigin, bikePose[n].upright[0] ) );
	}
	VectorSubtract( handL, barL, d );
	trap->Print( "BIKESEAT t=%d grip L fwd=%.2f left=%.2f up=%.2f", cg.time, DotProduct( d, bikePose[n].upright[0] ),
		DotProduct( d, bikePose[n].upright[1] ), DotProduct( d, bikePose[n].upright[2] ) );
	VectorSubtract( handR, barR, d );
	trap->Print( " R fwd=%.2f left=%.2f up=%.2f\n", DotProduct( d, bikePose[n].upright[0] ),
		DotProduct( d, bikePose[n].upright[1] ), DotProduct( d, bikePose[n].upright[2] ) );
}

/*
===============
CG_BikeRiderPose

A bike's rider, placed at the upright seat (AttachRiders: the *driver tag with yaw only, which prediction and the
server use), drawn with the bike's motion from CG_BikePose: it pitches and leans with the bike about the same contact
patch, so hands and feet stay on the bars and pegs. Its own roll (the speeder lean on its view angles) is replaced.
===============
*/
// a player or NPC riding a VH_BIKE (cg_players.c leaves it out of cg_smoothClients: it is drawn on the seat)
qboolean CG_BikeRider( const centity_t *cent )
{
	int n = cent->currentState.m_iVehicleNum;

	return (qboolean)( cent->currentState.eType != ET_NPC || cent->currentState.NPC_class != CLASS_VEHICLE )
		&& n > 0 && n < MAX_GENTITIES && BG_IsBike( cg_entities[n].m_pVehicle );
}

void CG_BikeRiderPose( centity_t *rider, refEntity_t *ent )
{
	int		n = rider->currentState.m_iVehicleNum, i;
	vec3_t	angles, d;
	matrix3_t upright;

	if ( n <= 0 || n >= MAX_GENTITIES || bikePose[n].time != cg.time )
		return;	// not on a bike drawn this frame
	vectoangles( ent->axis[0], angles );
	VectorSet( angles, 0, angles[YAW], 0 );
	AnglesToAxis( angles, upright );
	if ( cg_bikeSeatDebug.integer > 0 )
		CG_BikeSeatDebug( rider, &cg_entities[n], angles, ent->origin, n );

	VectorSubtract( ent->origin, bikePose[n].from, d );
	CG_BikeRotate( n, d, d );
	VectorAdd( bikePose[n].to, d, ent->origin );
	VectorCopy( ent->origin, ent->oldorigin );
	for ( i = 0; i < 3; i++ )
		CG_BikeRotate( n, upright[i], ent->axis[i] );
}
