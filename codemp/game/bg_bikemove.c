/*
===========================================================================
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

// bg_bikemove.c -- the pmove adapter of bg_bike.c (wishlist item 1): a VH_BIKE
// (its own vehicle type, BikeNPC.c) moves by BG_BikeRunCommand instead of the
// generic walk/air move. Game and cgame run the same code on the same command,
// so the bike is predicted like any vehicle.
//
// State lives in the vehicle's playerState, all of it in fields sent at full
// float precision (qcommon/msg.cpp vehPlayerStateFields), so a snapshot restores
// every value the integrator reads:
//   origin, velocity                  bikeState origin, velocity
//   vehOrientation YAW / PITCH / ROLL yaw, -pitch (Quake pitch is nose down), lean
//   speed                             speed (u/s, signed)
//   hyperSpaceAngles[0..2]            pitchRate, steer, yawRate (bikes never hyperspace)
//   moveDir[0..1]                     leanInt, comLong (bikes skip the moveDir walk move)
//   moveDir[2]                        an integer below 2^24 (exact in a float):
//                                     gear | burnout << 3 | burnLaunch << 4 | shiftMs << 5 | BIKE_PACK_VALID
//                                     | onGround << 21
//   groundEntityNum                   the rear wheel's ground entity (else the front's), set for others to read
// Every 4 ms substep: a ground probe under each wheel (BikeWheelGround: a flat box the
// size of a contact patch, straight down) and one PM_StepSlideMove of the frame's box,
// which rides frameClearance above the tyres (BG_VehicleAdjustBBoxForOrientation).

#include "qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_local.h"
#include "bg_vehicles.h"
#include "bg_bike.h"

#define BIKE_PACK_VALID		( 1 << 20 )
#define BIKE_PACK_GROUND	( 1 << 21 )
#define BIKE_UNITS_PER_M	37.5f

void PM_SetPMViewAngle( playerState_t *ps, vec3_t angle, usercmd_t *ucmd );
void PM_VehicleImpact( bgEntity_t *pEnt, trace_t *trace );
extern bgEntity_t *pm_entSelf;

#ifdef _GAME
extern vmCvar_t g_bikeDebug;
#endif

static bikeParams_t	bikeDefaultParms;		// the generic bike (BG_BikeDefaultParams)
static int			bikeDefaultParmsSet;

static const bikeParams_t *BikeDefaultParms( void )
{
	if ( !bikeDefaultParmsSet )
	{
		BG_BikeDefaultParams( &bikeDefaultParms );
		bikeDefaultParmsSet = 1;
	}
	return &bikeDefaultParms;
}

const bikeParams_t *BG_BikeParams( const vehicleInfo_t *info )
{
	if ( info && info->bike )
		return info->bike;
	return BikeDefaultParms();
}

qboolean BG_IsBike( const Vehicle_t *pVeh )
{
	return (qboolean)( pVeh && pVeh->m_pVehicleInfo && pVeh->m_pVehicleInfo->type == VH_BIKE );
}

static int BikeGroundHeightNone( void *ctx, double x, double y, double *z )
{	// unused: the pmove world answers through wheelGround
	(void)ctx; (void)x; (void)y;
	*z = 0.0;
	return 0;
}

// each wheel's ground probe in the current substep (BikeWheelGround): the ground entity, the surface and the sleep
// read them
static struct {
	int		hit;
	vec3_t	start, end;
	trace_t	trace;
} bikeProbe[2];

// a flat box the size of a contact patch (u)
static const vec3_t bikePatchMins = { -4.0f, -3.0f, 0.0f };
static const vec3_t bikePatchMaxs = { 4.0f, 3.0f, 0.0f };

static void BikeProbe( int wheel, const vec3_t start, const vec3_t end )
{
	trace_t *tr = &bikeProbe[wheel].trace;

	VectorCopy( start, bikeProbe[wheel].start );
	VectorCopy( end, bikeProbe[wheel].end );
	pm->trace( tr, start, bikePatchMins, bikePatchMaxs, end, pm->ps->clientNum, pm->tracemask );
	// starting inside something (the wheel inside a wall, or a step higher than the probe reaches) is no ground
	bikeProbe[wheel].hit = !tr->startsolid && !tr->allsolid && tr->fraction < 1.0f
		&& tr->plane.normal[2] >= MIN_WALK_NORMAL;
}

// bikeWorld_t.wheelGround
static int BikeWheelGround( void *ctx, int wheel, const double top[3], double depth, double *z, double normal[3] )
{
	vec3_t	start, end;

	(void)ctx;
	VectorSet( start, (float)top[0], (float)top[1], (float)top[2] );
	VectorSet( end, start[0], start[1], (float)( top[2] - depth ) );
	BikeProbe( wheel, start, end );
	if ( bikeProbe[wheel].trace.startsolid || bikeProbe[wheel].trace.allsolid )
		return -1;	// buried: bg_bike.c probes again from higher up
	if ( !bikeProbe[wheel].hit )
		return 0;
	*z = bikeProbe[wheel].trace.endpos[2];
	normal[0] = bikeProbe[wheel].trace.plane.normal[0];
	normal[1] = bikeProbe[wheel].trace.plane.normal[1];
	normal[2] = bikeProbe[wheel].trace.plane.normal[2];
	return 1;
}

// BIKE_SURF_* from the material under the rear wheel (else the front) in this substep (CP2077 DrivingSurface.Ground
// lists dirt, grass, sand, mud and stone)
static int BikeSurface( void *ctx, double x, double y )
{
	const trace_t *tr = bikeProbe[1].hit ? &bikeProbe[1].trace : bikeProbe[0].hit ? &bikeProbe[0].trace : NULL;

	(void)ctx; (void)x; (void)y;
	if ( !tr )
		return BIKE_SURF_ASPHALT;
	switch ( tr->surfaceFlags & MATERIAL_MASK )
	{
	case MATERIAL_SAND:
		return BIKE_SURF_SAND;
	case MATERIAL_SHORTGRASS:
	case MATERIAL_LONGGRASS:
	case MATERIAL_DIRT:
	case MATERIAL_GRAVEL:
	case MATERIAL_MUD:
	case MATERIAL_SNOW:
	case MATERIAL_DRYLEAVES:
	case MATERIAL_GREENLEAVES:
	case MATERIAL_ROCK:
		return BIKE_SURF_GROUND;
	default:
		return BIKE_SURF_ASPHALT;
	}
}

/*
The frame's box: the .veh's width and height, its bottom frameClearance above the line the tyres stand on. The box is
axis aligned and as wide as the bars, while that line rises with the bike's pitch from the rear contact, which is
originAhead behind the origin. So the bottom rises by the line's height under the box's highest corner: the pitch
along the heading, and the ground's cross slope under the wheels across it (a bike could not
make it up a slope: a 16.5 deg road lifted the flat box onto its front edge, 11 u into the slope, with the rear tyre in
the air and no drive from 4.2 deg up; a hillside taken across held it up the same way). The lift is capped, so the
box keeps BIKE_FRAME_MIN_H of height in a wheelie.
*/
#define BIKE_FRAME_MIN_H	16.0f				// u: 41 deg along the heading, 36 deg across a corner

static void BikeFrameBoxLift( const Vehicle_t *veh, float pitchDeg, float yawDeg, float crossSlope, vec3_t mins,
	vec3_t maxs )
{
	const bikeParams_t	*bp = BG_BikeParams( veh->m_pVehicleInfo );
	float				half = veh->m_pVehicleInfo->width / 2.0f, top = veh->m_pVehicleInfo->height + DEFAULT_MINS_2;
	float				bottom = (float)( DEFAULT_MINS_2 + bp->frameClearance * BIKE_UNITS_PER_M );
	float				ahead = (float)( bp->originAhead * BIKE_UNITS_PER_M ), yr = DEG2RAD( yawDeg );
	float				cy = cosf( yr ), sy = sinf( yr ), tp, lift = 0.0f, along, left, rise;
	int					i;

	pitchDeg = Com_Clamp( -75.0f, 75.0f, pitchDeg );
	tp = tanf( DEG2RAD( pitchDeg ) );
	for ( i = 0; i < 4; i++ )
	{
		float x = ( i & 1 ) ? half : -half, y = ( i & 2 ) ? half : -half;

		along = ahead + x * cy + y * sy;	// ahead of the rear contact
		left = -x * sy + y * cy;
		rise = along * tp + left * crossSlope;
		if ( rise > lift )
			lift = rise;
	}
	if ( bottom + lift > top - BIKE_FRAME_MIN_H )
		lift = top - BIKE_FRAME_MIN_H - bottom;
	if ( lift < 0.0f )
		lift = 0.0f;
	VectorSet( mins, -half, -half, bottom + lift );
	VectorSet( maxs, half, half, top );
}

// BG_VehicleAdjustBBoxForOrientation (the box before the move, and the box others collide with): the pitch only; the
// bike's own substeps add the cross slope (BikeTranslate)
void BG_BikeFrameBox( const Vehicle_t *veh, vec3_t mins, vec3_t maxs )
{
	if ( veh->m_vOrientation )
		BikeFrameBoxLift( veh, -veh->m_vOrientation[PITCH], veh->m_vOrientation[YAW], 0.0f, mins, maxs );
	else
		BikeFrameBoxLift( veh, 0.0f, 0.0f, 0.0f, mins, maxs );
}

// the ground's rise per unit to the left of the heading, under the wheels in this substep (their probes' mean normal)
static float BikeCrossSlope( float yawDeg )
{
	float	yr = DEG2RAD( yawDeg ), n[3] = { 0.0f, 0.0f, 0.0f };
	int		i, k;

	for ( i = 0; i < 2; i++ )
	{
		if ( !bikeProbe[i].hit )
			continue;
		for ( k = 0; k < 3; k++ )
			n[k] += bikeProbe[i].trace.plane.normal[k];
	}
	if ( n[2] <= 0.0f )
		return 0.0f;
	return ( n[0] * sinf( yr ) - n[1] * cosf( yr ) ) / n[2];
}

/*
Frame contact. PM_SlideMove hands every hit of a bike to PM_BikeNoteImpact instead of the speeder's PM_VehicleImpact,
which crashed the bike (sparks, damage) on any touch above 0.27 m/s, ground included: the box rides on the ground, so
slope changes, seams and curbs all counted. Now:
  - walkable ground is support, never a hit;
  - a person or vehicle hit still goes through PM_VehicleImpact (knockdown, damage to both);
  - any other hit (walls, steps, movers) is noted, and judged after the whole step move: only the speed the bike
    really lost into it counts, so a curb the step climbs costs nothing. Sparks need a press into it or a slide
    along it; damage needs a hard hit (G_BikeFrameContact).
*/
#define BIKE_CONTACT_MIN_U		1.0f				// u/s lost into the plane: touching at all
#define BIKE_SCRAPE_IN_U		( 0.5f * BIKE_UNITS_PER_M )	// sparks: a knock into it
#define BIKE_SCRAPE_ALONG_U		( 2.0f * BIKE_UNITS_PER_M )	// sparks: or a slide along it while touching

static struct {
	int		set;
	float	into;			// speed into the plane at the hit, u/s
	trace_t	trace;
	// g_bikeDebug evidence, since the last BIKE line: hits the speeder path would have sparked on (its magnitude
	// and landing tests, its 200 ms debounce), and the frame contacts that spark now
	int		oldSparks, oldTime, contacts;
} bikeHit;

void PM_BikeNoteImpact( bgEntity_t *pEnt, trace_t *trace )
{
	bgEntity_t	*hit = ( trace->entityNum < ENTITYNUM_WORLD ) ? PM_BGEntForNum( trace->entityNum ) : NULL;
	float		into;
	const float	*v = pm->ps->velocity;

	if ( VectorLength( v ) * pEnt->m_pVehicle->m_pVehicleInfo->mass / 50.0f >= 100.0f
		&& !( fabsf( v[0] ) + fabsf( v[1] ) < 100.0f && v[2] > -100.0f ) && pm->cmd.serverTime > bikeHit.oldTime )
	{
		bikeHit.oldSparks++;
		bikeHit.oldTime = pm->cmd.serverTime + 200;
	}

	if ( hit && ( hit->s.eType == ET_PLAYER || hit->s.eType == ET_NPC ) )
	{	// a body: the stock impact (knockdown, damage both ways)
		PM_VehicleImpact( pEnt, trace );
		return;
	}
	if ( trace->plane.normal[2] >= MIN_WALK_NORMAL )
		return;	// ground the bike stands or climbs on
	into = -DotProduct( pm->ps->velocity, trace->plane.normal );
	if ( !bikeHit.set || into > bikeHit.into )
	{
		bikeHit.set = 1;
		bikeHit.into = into;
		bikeHit.trace = *trace;
	}
}

// after one substep's step move: the hit noted, if the bike lost speed into it
static void BikeFrameContact( const vec3_t before )
{
	const float	*n = bikeHit.trace.plane.normal;
	float		lost, along;
	vec3_t		v, point;
	int			i;

	if ( !bikeHit.set )
		return;
	bikeHit.set = 0;
	lost = -DotProduct( pm->ps->velocity, n );
	lost = -DotProduct( before, n ) - ( lost > 0.0f ? lost : 0.0f );
	if ( lost < BIKE_CONTACT_MIN_U )
		return;	// stepped over it, or barely touched
	VectorMA( pm->ps->velocity, -DotProduct( pm->ps->velocity, n ), n, v );
	along = VectorLength( v );
	if ( lost < BIKE_SCRAPE_IN_U && along < BIKE_SCRAPE_ALONG_U )
		return;
	// the box face against the plane
	for ( i = 0; i < 3; i++ )
		point[i] = bikeHit.trace.endpos[i] + ( n[i] > 0.0f ? pm->mins[i] : pm->maxs[i] ) * fabsf( n[i] );
	bikeHit.contacts++;
#ifdef _GAME
	if ( g_bikeDebug.integer > 0 )
		Com_Printf( "BIKE contact t=%d at=(%.1f %.1f %.1f) n=(%.2f %.2f %.2f) into=%.2f along=%.2f m/s ent=%d\n",
			pm->cmd.serverTime, point[0], point[1], point[2], n[0], n[1], n[2], lost / BIKE_UNITS_PER_M,
			along / BIKE_UNITS_PER_M, bikeHit.trace.entityNum );
	if ( pm_entSelf )
		G_BikeFrameContact( pm_entSelf, &bikeHit.trace, point, lost, along );
#else
	(void)point;
#endif
}

// bikeWorld_t.translate: the frame's box moves by the body's velocity (the springs set its vertical part)
static void BikeTranslate( const bikeWorld_t *world, bikeState_t *bs, const bikeParams_t *bp, double dt )
{
	float	saveFrametime = pml.frametime;
	vec3_t	before;

	(void)world;
	(void)bp;
	VectorCopy( bs->origin, pm->ps->origin );
	VectorCopy( bs->velocity, pm->ps->velocity );
	if ( pm_entSelf && pm_entSelf->m_pVehicle )
	{	// the frame's box on the line the tyres stand on now. Lowered where something pokes above that line (a crest,
		// a curb between the wheels) it would start inside it and the slide move could not move it: it keeps its
		// highest bottom there for this substep
		const Vehicle_t	*veh = pm_entSelf->m_pVehicle;
		trace_t			tr;

		BikeFrameBoxLift( veh, (float)bs->pitch, (float)bs->yaw, BikeCrossSlope( (float)bs->yaw ), pm->mins, pm->maxs );
		pm->trace( &tr, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask );
		if ( tr.startsolid || tr.allsolid )
			BikeFrameBoxLift( veh, 75.0f, 0.0f, 0.0f, pm->mins, pm->maxs );
	}
	pml.frametime = (float)dt;
	VectorCopy( pm->ps->velocity, before );
	bikeHit.set = 0;
	PM_StepSlideMove( qfalse );
	BikeFrameContact( before );
	pml.frametime = saveFrametime;
	VectorCopy( pm->ps->origin, bs->origin );
	VectorCopy( pm->ps->velocity, bs->velocity );
}

static void BikeUnpack( const playerState_t *ps, const bikeParams_t *bp, bikeState_t *bs )
{
	int packed = (int)ps->moveDir[2];

	VectorCopy( ps->origin, bs->origin );
	VectorCopy( ps->velocity, bs->velocity );
	bs->yaw = ps->vehOrientation[YAW];
	bs->pitch = -ps->vehOrientation[PITCH];
	bs->lean = ps->vehOrientation[ROLL];
	bs->speed = ps->speed;
	bs->onGround = ( packed & BIKE_PACK_GROUND ) ? 1 : 0;
	bs->events = 0;
	bs->droppedMs = 0;
	if ( !( packed & BIKE_PACK_VALID ) )
	{	// first move of this bike: spawned standing on its wheels
		bs->onGround = 1;
		bs->yawRate = bs->pitchRate = bs->steer = 0;
		bs->leanInt = 0;
		bs->comLong = 0.5 * ( bp->comLongBack + bp->comLongFwd );
		bs->gear = 1;
		bs->burnout = bs->burnLaunch = 0;
		bs->shiftMs = BIKE_SHIFT_MS_MAX;
		bs->pitch = bs->lean = 0;
		return;
	}
	bs->pitchRate = ps->hyperSpaceAngles[0];
	bs->steer = ps->hyperSpaceAngles[1];
	bs->yawRate = ps->hyperSpaceAngles[2];
	bs->leanInt = ps->moveDir[0];
	bs->comLong = ps->moveDir[1];
	bs->gear = packed & 7;
	bs->burnout = ( packed >> 3 ) & 1;
	bs->burnLaunch = ( packed >> 4 ) & 1;
	bs->shiftMs = ( packed >> 5 ) & 0x7fff;
}

static void BikePack( const bikeState_t *bs, playerState_t *ps )
{
	int shiftMs = bs->shiftMs > BIKE_SHIFT_MS_MAX ? BIKE_SHIFT_MS_MAX : bs->shiftMs;
	float yawR = DEG2RAD( bs->yaw );

	VectorCopy( bs->origin, ps->origin );
	VectorCopy( bs->velocity, ps->velocity );
	ps->vehOrientation[YAW] = bs->yaw;
	ps->vehOrientation[PITCH] = -bs->pitch;
	ps->vehOrientation[ROLL] = bs->lean;
	// forward speed after the last slide move (bs->speed is the integrator's, from before a wall clipped it): the
	// dismount's roll-off test, the rider pose and the wheel spin read it (the integrator re-reads velocity)
	ps->speed = bs->velocity[0] * cosf( yawR ) + bs->velocity[1] * sinf( yawR );
	ps->hyperSpaceAngles[0] = bs->pitchRate;
	ps->hyperSpaceAngles[1] = bs->steer;
	ps->hyperSpaceAngles[2] = bs->yawRate;
	ps->moveDir[0] = bs->leanInt;
	ps->moveDir[1] = bs->comLong;
	ps->moveDir[2] = (float)( ( bs->gear & 7 ) | ( ( bs->burnout & 1 ) << 3 ) | ( ( bs->burnLaunch & 1 ) << 4 )
		| ( shiftMs << 5 ) | BIKE_PACK_VALID | ( bs->onGround ? BIKE_PACK_GROUND : 0 ) );
}

/*
===================
PM_BikeRiderAnimate

The rider's pose on a bike (pm is the rider's pmove), from CP2077's sportbike rider set on spare BOTH_CIN_* rows
(jka-animlib tools/gla/splice_bike.py: BIKE_ANIM_*). The mount and dismount clips (AnimateRiders, UpdateRider) and
the stop / start transitions are held and play out; the rest follows the bike:
  stopped     ride_to_idle, then idle_stand (a foot down); enter_* ends in that pose, exit_* starts from it
  moving off  idle_to_ride
  riding      lean_left / lean_right past BIKE_POSE_LEAN degrees of bike roll, else tucked (lean forward), leaning
              back (lean back), or idle_ride
===================
*/
#define BIKE_ANIM_RIDE			BOTH_CIN_1
#define BIKE_ANIM_LEAN_L		BOTH_CIN_2
#define BIKE_ANIM_LEAN_R		BOTH_CIN_3
#define BIKE_ANIM_TUCK			BOTH_CIN_4
#define BIKE_ANIM_BACK			BOTH_CIN_5
#define BIKE_ANIM_ENTER_L		BOTH_CIN_6
#define BIKE_ANIM_ENTER_R		BOTH_CIN_7
#define BIKE_ANIM_EXIT_L		BOTH_CIN_8
#define BIKE_ANIM_EXIT_R		BOTH_CIN_9
#define BIKE_ANIM_STAND			BOTH_CIN_10
#define BIKE_ANIM_RIDE_TO_STAND	BOTH_CIN_11
#define BIKE_ANIM_STAND_TO_RIDE	BOTH_CIN_12
#define BIKE_POSE_LEAN			12.0f	// deg of bike roll before the rider hangs into the turn
#define BIKE_MOVING_MS			0.75f	// m/s

void PM_BikeRiderAnimate( bgEntity_t *veh )
{
	Vehicle_t		*pVeh = veh->m_pVehicle;
	playerState_t	*vps = veh->playerState;
	usercmd_t		*uc = &pVeh->m_ucmd;
	float			speed = vps->speed / BIKE_UNITS_PER_M;
	float			lean = vps->vehOrientation[ROLL];
	int				legs = pm->ps->legsAnim;
	int				anim;
	qboolean		moving = (qboolean)( fabs( speed ) > BIKE_MOVING_MS || uc->forwardmove > 0 );

	if ( pm->ps->weapon == WP_SABER && pm->ps->saberHolstered < 2 )
	{	// both hands are on the bike: the blade goes off for the whole ride, mount and dismount included
		pm->ps->saberHolstered = 2;
	}
	if ( pVeh->m_iBoarding != 0 || vps->vehBoarding )
	{	// mounting or dismounting: its clip holds
		return;
	}
	if ( pm->animations && pm->animations[BIKE_ANIM_RIDE].numFrames <= 0 )
	{	// a set without the rider rows (an own skeleton's animation.cfg the pack did not splice): the swoop pose,
		// not whatever the rider boarded in
		if ( legs != BOTH_VS_IDLE || pm->ps->torsoAnim != BOTH_VS_IDLE )
			PM_SetAnim( SETANIM_BOTH, BOTH_VS_IDLE, SETANIM_FLAG_OVERRIDE );
		return;
	}
	if ( pm->ps->legsTimer > 0 && ( legs == BIKE_ANIM_RIDE_TO_STAND || legs == BIKE_ANIM_STAND_TO_RIDE ) )
	{	// a transition plays out
		return;
	}
	if ( !moving )
	{
		if ( legs == BIKE_ANIM_STAND || legs == BIKE_ANIM_RIDE_TO_STAND || legs == BIKE_ANIM_ENTER_L
			|| legs == BIKE_ANIM_ENTER_R )
		{
			anim = BIKE_ANIM_STAND;
		}
		else
		{	// put a foot down
			PM_SetAnim( SETANIM_BOTH, BIKE_ANIM_RIDE_TO_STAND, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
			return;
		}
	}
	else if ( legs == BIKE_ANIM_STAND || legs == BIKE_ANIM_ENTER_L || legs == BIKE_ANIM_ENTER_R )
	{	// lift the foot and go
		PM_SetAnim( SETANIM_BOTH, BIKE_ANIM_STAND_TO_RIDE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
		return;
	}
	else if ( lean < -BIKE_POSE_LEAN )
	{
		anim = BIKE_ANIM_LEAN_L;
	}
	else if ( lean > BIKE_POSE_LEAN )
	{
		anim = BIKE_ANIM_LEAN_R;
	}
	else if ( uc->buttons & BUTTON_WALKING )
	{
		anim = BIKE_ANIM_TUCK;
	}
	else if ( uc->upmove < 0 )
	{
		anim = BIKE_ANIM_BACK;
	}
	else
	{
		anim = BIKE_ANIM_RIDE;
	}
	if ( legs != anim || pm->ps->torsoAnim != anim )
		PM_SetAnim( SETANIM_BOTH, anim, SETANIM_FLAG_OVERRIDE );
}

// the stock swoop clip standing in for a rider row (-1: not a rider row)
int BG_BikeFallbackAnim( int anim )
{
	switch ( anim )
	{
	case BIKE_ANIM_ENTER_L:		return BOTH_VS_MOUNT_L;
	case BIKE_ANIM_ENTER_R:		return BOTH_VS_MOUNT_R;
	case BIKE_ANIM_EXIT_L:		return BOTH_VS_DISMOUNT_L;
	case BIKE_ANIM_EXIT_R:		return BOTH_VS_DISMOUNT_R;
	case BIKE_ANIM_RIDE:
	case BIKE_ANIM_LEAN_L:
	case BIKE_ANIM_LEAN_R:
	case BIKE_ANIM_TUCK:
	case BIKE_ANIM_BACK:
	case BIKE_ANIM_STAND:
	case BIKE_ANIM_RIDE_TO_STAND:
	case BIKE_ANIM_STAND_TO_RIDE:	return BOTH_VS_IDLE;
	default:					return -1;
	}
}

// animIndex: the rider's set; one without the row gets the stock clip (a 0 ms clip would board at once)
int BG_BikeMountAnim( int animIndex, int boardingSide )
{
	int anim = ( boardingSide == -2 ) ? BIKE_ANIM_ENTER_R : BIKE_ANIM_ENTER_L;	// -1 left, -2 right, -3 from behind

	return BG_HasAnimation( animIndex, anim ) ? anim : BG_BikeFallbackAnim( anim );
}

int BG_BikeDismountAnim( int animIndex, int right )
{
	int anim = right ? BIKE_ANIM_EXIT_R : BIKE_ANIM_EXIT_L;

	return BG_HasAnimation( animIndex, anim ) ? anim : BG_BikeFallbackAnim( anim );
}

// the fields a step reads and writes (Vehicle_t.m_bikeRest)
static void BikeRestState( const playerState_t *ps, float *out )
{
	VectorCopy( ps->origin, out );
	VectorCopy( ps->velocity, out + 3 );
	VectorCopy( ps->vehOrientation, out + 6 );
	VectorCopy( ps->hyperSpaceAngles, out + 9 );
	VectorCopy( ps->moveDir, out + 12 );
	out[15] = ps->speed;
}

// a step that left the bike where it was (origin and velocity exact, the packed gear state too) and moved its angles,
// rates, steer and lean integral by less than BIKE_REST_EPS changed nothing that shows: a parked bike's tilt integral
// and lean decay by about 1 % a think for minutes before a float fixed point
#define BIKE_REST_EPS	0.01f

static qboolean BikeRestUnchanged( const float *a, const float *b )
{
	int i;

	for ( i = 0; i < 6; i++ )
	{
		if ( a[i] != b[i] )
			return qfalse;
	}
	for ( i = 6; i < 14; i++ )
	{
		if ( !( fabsf( a[i] - b[i] ) <= BIKE_REST_EPS ) )
			return qfalse;
	}
	return (qboolean)( a[14] == b[14] && a[15] == b[15] );
}

// a parked bike whose last full step changed nothing (BikeRestUnchanged) stays put while its state is the one it went
// to sleep in and the ground under each wheel is the same plane at the same height: the two wheel probes again instead
// of the substep loop (three traces per 4 ms substep). A push, a hit, a mover, a removed or swapped chunk or a rider
// wakes it.
static qboolean BikeStillAsleep( const Vehicle_t *pVeh, const playerState_t *ps )
{
	float	cur[ARRAY_LEN( pVeh->m_bikeRest )];
	int		i;

	if ( !pVeh->m_bikeAsleep )
		return qfalse;
	BikeRestState( ps, cur );
	if ( memcmp( cur, pVeh->m_bikeRest, sizeof( cur ) ) )
		return qfalse;
	for ( i = 0; i < 2; i++ )
	{
		BikeProbe( i, pVeh->m_bikeRestStart[i], pVeh->m_bikeRestEnd[i] );
		if ( !bikeProbe[i].hit || bikeProbe[i].trace.entityNum != pVeh->m_bikeRestEnt[i]
			|| bikeProbe[i].trace.endpos[2] != pVeh->m_bikeRestZ[i]
			|| !VectorCompare( bikeProbe[i].trace.plane.normal, pVeh->m_bikeRestNormal[i] ) )
			return qfalse;
	}
	return qtrue;
}

/*
===================
PM_BikeVehicleMove

pm is the bike's own pmove. ucmd is the pilot's command (the vehicle's m_ucmd,
which keeps rightmove and upmove), or NULL when nobody rides: then the bike
stands with its handbrake on.

Input map (the bike owns it; jump and crouch no longer eject from a bike):
  forwardmove  throttle (S brakes, then reverses)   rightmove  steer
  walk button  lean forward (CP2077 Shift)          crouch     lean back (CP2077 Ctrl, wheelie)
  jump         handbrake (with throttle when slow: a burnout)
===================
*/
void PM_BikeVehicleMove( bgEntity_t *veh, usercmd_t *ucmd )
{
	Vehicle_t			*pVeh = veh ? veh->m_pVehicle : NULL;
	const bikeParams_t	*bp = BG_BikeParams( pVeh ? pVeh->m_pVehicleInfo : NULL );
	playerState_t		*ps = pm->ps;
	bikeState_t			bs;
	bikeInput_t			in;
	bikeWorld_t			world;
	float				before[ARRAY_LEN( pVeh->m_bikeRest )];
	int					substeps;

	if ( pVeh )
	{
		if ( !ucmd && BikeStillAsleep( pVeh, ps ) )
		{
			PM_SetPMViewAngle( ps, ps->vehOrientation, &pm->cmd );
			return;
		}
		pVeh->m_bikeAsleep = qfalse;
		BikeRestState( ps, before );
	}

	memset( &bs, 0, sizeof( bs ) ); // the wheel fields BikeUnpack leaves are only written by a substep
	BikeUnpack( ps, bp, &bs );
	if ( ucmd )
	{
		in.throttle = ucmd->forwardmove / 127.0f;
		in.steer = ucmd->rightmove / 127.0f;
		in.lean = 0;
		if ( ucmd->buttons & BUTTON_WALKING )
			in.lean += 1.0f;
		if ( ucmd->upmove < 0 )
			in.lean -= 1.0f;
		in.handbrake = ( ucmd->upmove > 0 );
	}
	else
	{
		in.throttle = in.steer = in.lean = 0;
		in.handbrake = 1;
	}

	world.groundHeight = BikeGroundHeightNone;
	world.wheelGround = BikeWheelGround;
	world.translate = BikeTranslate;
	world.surface = BikeSurface;
	world.ctx = NULL;

	// the span of this pmove: PmoveSingle has already clamped msec
	bs.commandTime = pm->cmd.serverTime - pml.msec;
	substeps = BG_BikeRunCommand( &bs, bp, &in, &world, pm->cmd.serverTime );
	BikePack( &bs, ps );
	if ( substeps > 0 )
	{	// the ground the wheels found in the last substep: the rear's, else the front's
		const trace_t *g = bs.contact[1] ? &bikeProbe[1].trace : bs.contact[0] ? &bikeProbe[0].trace : NULL;

		ps->groundEntityNum = g ? g->entityNum : ENTITYNUM_NONE;
		pml.groundPlane = pml.walking = ( g != NULL );
		if ( g )
			pml.groundTrace = *g;
	}
	if ( pVeh && !ucmd && substeps > 0 && bs.contact[0] && bs.contact[1] )
	{	// parked and this step changed nothing: sleep in this state on this ground (BikeStillAsleep)
		BikeRestState( ps, pVeh->m_bikeRest );
		if ( BikeRestUnchanged( before, pVeh->m_bikeRest ) )
		{
			int i;

			pVeh->m_bikeAsleep = qtrue;
			for ( i = 0; i < 2; i++ )
			{
				VectorCopy( bikeProbe[i].start, pVeh->m_bikeRestStart[i] );
				VectorCopy( bikeProbe[i].end, pVeh->m_bikeRestEnd[i] );
				VectorCopy( bikeProbe[i].trace.plane.normal, pVeh->m_bikeRestNormal[i] );
				pVeh->m_bikeRestZ[i] = bikeProbe[i].trace.endpos[2];
				pVeh->m_bikeRestEnt[i] = bikeProbe[i].trace.entityNum;
			}
		}
	}

#ifdef _GAME
	if ( g_bikeDebug.integer > 0
		&& ( pm->cmd.serverTime / g_bikeDebug.integer ) != ( ( pm->cmd.serverTime - pml.msec ) / g_bikeDebug.integer ) )
	{	// one line per interval: the evidence of a ride (qconsole.log)
		Com_Printf( "BIKE t=%d ent=%d pos=(%.1f %.1f %.1f) v=%.2f m/s gear=%d rpm=%.0f yaw=%.2f pitch=%.2f lean=%.2f "
			"steer=%.3f ground=%d ev=%x in=(%.2f %.2f %.0f %d) oldsparks=%d contacts=%d wheels=%d%d comp=(%.3f %.3f) "
			"load=(%.0f %.0f)\n", pm->cmd.serverTime,
			ps->clientNum, ps->origin[0], ps->origin[1], ps->origin[2], bs.speed / BIKE_UNITS_PER_M, bs.gear,
			(double)bs.rpm, bs.yaw, bs.pitch, bs.lean, bs.steer, bs.onGround, bs.events, in.throttle, in.steer, in.lean,
			in.handbrake, bikeHit.oldSparks, bikeHit.contacts, bs.contact[0], bs.contact[1], (double)bs.comp[0],
			(double)bs.comp[1], (double)bs.load[0], (double)bs.load[1] );
		if ( g_bikeDebug.integer <= 20 && substeps > 0 )
		{	// fine prints: the last substep's wheel probes (start z, end z, startsolid, fraction, hit z, normal z)
			int i;

			for ( i = 0; i < 2; i++ )
				Com_Printf( "BIKEP t=%d %s top=%.1f end=%.1f ss=%d f=%.3f z=%.1f nz=%.2f hit=%d\n", pm->cmd.serverTime,
					i ? "rear" : "front", bikeProbe[i].start[2], bikeProbe[i].end[2], bikeProbe[i].trace.startsolid,
					bikeProbe[i].trace.fraction, bikeProbe[i].trace.endpos[2], bikeProbe[i].trace.plane.normal[2],
					bikeProbe[i].hit );
		}
		bikeHit.oldSparks = bikeHit.contacts = 0;
	}
#endif

	// render and collide at the bike's orientation
	if ( veh && veh->m_pVehicle )
		PM_SetPMViewAngle( ps, ps->vehOrientation, &pm->cmd );
}
