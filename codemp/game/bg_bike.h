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

// bg_bike.h -- standalone motorbike dynamics (wishlist group V; v2)
//
// The core step calls no engine function; the world is reached only through
// bikeWorld_t callbacks, so the same code runs in game, cgame (bg_bikemove.c),
// a test main and (equation for equation) tools/bike/bike_twin.py.
//
// v2 drives a bike from CP2077-style handling records: mass and inertia,
// engine torque curve and gearbox, tyre friction by surface with the lateral
// slip curves, perfect steering with a speed-reduced lock, tilt curves and
// PID, rider COM shift, handbrake, burnout launch, air control. A bike's
// values come from its pack's ext_data/vehicles/<name>.bike
// (BG_BikeParseParams; docs/bike-handling.md; tools/bike/ct3x_params.py writes
// one from a CP2077 TweakDB dump), over BG_BikeDefaultParams' generic bike.
// Each interpretation of a CP2077 field is marked [interpreted] in
// bikeParams_t; xenjk-only additions are marked xenjk.
//
// v3: the
// ground under each wheel. Every substep each wheel finds its own ground
// (bikeWorld_t.wheelGround), its suspension (CP2077's front and back spring and
// damper presets) pushes the body, and the body heaves and pitches on those two
// springs about its centre of mass. The springs' loads are the tyres' normal
// loads in CP2077's grip, traction and braking, so a wheel off the ground has
// no grip; the drive needs the rear wheel down. A wheelie is the front spring
// with nothing under it. Heave and pitch use the state the pmove adapter
// already sends (origin z, velocity z, pitch, pitch rate): wheel travel is
// recomputed from the ground, never stored.
//
// Time: the integrator advances on a fixed BIKE_SUBSTEP_MS grid aligned to
// absolute command time. A command from t0 to t1 runs
// floor(t1/4) - floor(t0/4) substeps, so any split of the same span into
// commands visits the same grid points. Translation and contact run inside
// every substep (bikeWorld_t.translate), never once per command.
// A single command is capped at bikeParams_t.maxCommandMs; the excess is
// dropped (counted in bikeState_t.droppedMs), because the server can run a
// 100 ms step on a stale pilot command (g_active.c, "Previous owner
// disconnected, maybe").
//
// Units: parameters in CP2077's (m, kg, N m, s, deg); state positions and
// velocities in JKA units (37.5 u per metre, the levelport scale). Same
// speed on every map.
// Conventions: yaw as JKA (counter-clockwise, 0 = +x); lean > 0 = right side
// down; pitch > 0 = nose up; steer > 0 = right (D); lean input > 0 = forward
// (CP2077 Left Shift), < 0 = back (CP2077 Left Ctrl).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_SUBSTEP_MS		4
#define BIKE_MAX_GEARS		10
#define BIKE_MAX_CURVE		16
#define BIKE_NUM_SURFACES	3
#define BIKE_SHIFT_MS_MAX	4000

// BG_BIKE_DOUBLE stores state in double; used by the twin comparison only.
#ifdef BG_BIKE_DOUBLE
typedef double bikeFloat_t;
#else
typedef float bikeFloat_t;
#endif

// event bits, cleared at the start of every command
#define BIKE_EV_STEP		0x0001	// stepped up a curb
#define BIKE_EV_BLOCKED		0x0002	// rise above stepHeight: stopped dead
#define BIKE_EV_AIRBORNE	0x0004	// left the ground
#define BIKE_EV_LANDED		0x0008
#define BIKE_EV_BAIL		0x0010	// landed misaligned beyond landBailPitch
#define BIKE_EV_LOOPOUT		0x0020	// wheelie passed maxPitch
#define BIKE_EV_CAPPED		0x0040	// command longer than maxCommandMs
#define BIKE_EV_SHIFT		0x0080	// gear change started
#define BIKE_EV_BURNOUT		0x0100	// rear spinning in place (throttle + handbrake)

// surfaces (CP2077 BikeDrivingFrictionMap.Default)
#define BIKE_SURF_ASPHALT	0
#define BIKE_SURF_GROUND	1
#define BIKE_SURF_SAND		2

// Inputs, normalised. Mapping (bike module owns it; stock jump/crouch mean
// jump-off/roll-off on a speeder, so the adapter must remap):
//   throttle  W/S        forwardmove / 127; S brakes, then reverses below reverseThreshold
//   steer     A/D        rightmove / 127 (kept in m_ucmd)
//   lean      Shift/Ctrl +1 forward, -1 back (wheelie)
//   handbrake            adapter's choice; with throttle at low speed, a burnout
typedef struct bikeInput_s {
	bikeFloat_t	throttle;	// -1 brake/reverse .. +1 full throttle
	bikeFloat_t	steer;		// -1 left .. +1 right
	bikeFloat_t	lean;		// -1 back .. +1 forward
	int			handbrake;
} bikeInput_t;

typedef struct bikeCurve_s {
	int			n;
	double		pts[BIKE_MAX_CURVE][2];	// (x, y) keys, x ascending; linear between, clamped
} bikeCurve_t;

typedef struct bikeParams_s {
	int			maxCommandMs;		// longest command integrated; rest dropped

	// world (xenjk)
	double		gravity;			// m/s^2
	double		stepHeight;			// u: a wheel sees ground up to this far above its fully compressed tyre;
									// the heightfield translate stops at a rise of this above the rear contact
	double		originHeight;		// u, the state origin above the rear contact patch at rest (DEFAULT_MINS_2)
	double		originAhead;		// m, the state origin ahead of the rear axle (the model's origin, mid-bike)
	double		frameClearance;		// m, the frame's box bottom above the ground at rest (xenjk)
	double		wheelClimb;			// u: a wheel buried in something (a stair, a high curb) climbs onto its top within
									// this much above its probe; higher is a wall (xenjk)

	// chassis (CP2077 drive model)
	double		mass;				// kg (total_mass)
	double		inertiaPitch;		// kg m^2 (momentOfInertia x)
	double		wheelbase;			// m (converted model's rig)
	double		comHeight;			// m above ground (center_of_mass_offset + tyre radius + xenjk rider)
	double		comLongBack;		// m ahead of the rear axle at full lean back (bikeMaxCOMLongOffset)
	double		comLongFwd;			// m at full lean forward (bikeMinCOMLongOffset)
	double		comDamp;			// per frame (bikeCOMOffsetDampFactor)
	double		airResistance;		// N per (m/s)^2 (airResistanceFactor)
	double		forwardWeightTransfer;
	double		lowVelStopDecel;	// m/s^2 below 1 m/s, no throttle (lowVelStoppingDeceleration)
	double		brakingFriction;	// brakingFrictionFactor
	double		handbrakeTorque;	// N m (handbrakeBrakingTorque)
	double		frameRate;			// [interpreted] frames per s of CP2077's per-frame damp factors

	// steering and tilt
	double		maxWheelTurn;		// deg (maxWheelTurnDeg)
	double		wheelTurnAdd;		// deg/s (wheelTurnMaxAddPerSecond)
	double		wheelTurnSub;		// deg/s (wheelTurnMaxSubPerSecond)
	double		turnBaseSpeed;		// m/s, full lock below (turnUpdateBaseSpeedThreshold)
	double		turnMaxSpeed;		// m/s (turnUpdateMaxSpeedThreshold)
	double		turnMaxSpeedMul;	// lock multiplier at and above turnMaxSpeed
	double		maxYawRate;			// rad/s (RotationLimiter maxAngularSpeedRad)
	double		perfectSteering;	// share of the yaw the velocity follows (perfectSteeringFactor)
	double		maxTilt;			// deg (bikeMaxTilt)
	double		tiltSpeed;			// deg/s leaning in (bikeTiltSpeed)
	double		tiltReturnSpeed;	// deg/s (bikeTiltReturnSpeed)
	double		tiltKp, tiltKi;		// bikeTiltPID
	double		speedNorm;			// m/s, [interpreted] the tilt curves' speed axis (top gear maxSpeed)

	// suspension, [0] front [1] rear (the wheel presets' logical suspension; [interpreted] springStiffness is the
	// corner's natural frequency in rad/s: both give a 23 % static sag)
	double		suspTravel[2];		// m (logicalSuspensionCompressionLength)
	double		suspOmega[2];		// rad/s (springStiffness)
	double		dampBump[2], dampBumpLow[2];		// N s/m compressing, above and below the rate band
	double		dampRebound[2], dampReboundLow[2];	// N s/m extending
	double		dampLowRate[2], dampHighRate[2];	// m/s, the band between the low and high damping
	double		bumpStop;			// spring rates past full travel (xenjk)
	double		bumpTravel;			// m past full travel where the springs' push stops growing (xenjk)

	// tyres
	double		tireRadiusF;		// m
	double		tireRadiusR;		// m
	double		tireFriction;		// tireFrictionCoef
	double		latMulF, latMulR;	// frictionMulLateral
	double		longMulF, longMulR;	// frictionMulLongitudinal
	double		rollingCoef;		// tireRollingResistanceCoef
	double		hbLatR, hbLongR;	// HandbrakeFrictionModifier rear coefficients
	double		burnGrip;			// burnOutGripBonus
	double		burnLaunchSpeed;	// m/s (gripBonusMaxLaunchSpeed)
	double		burnMaxSpeed;		// m/s (maxSpeedToInitiateBurnOut)
	double		launchGripMul;		// [interpreted] gripBonusMaxSpeedMultiplier
	double		surfaces[BIKE_NUM_SURFACES][2];	// frictionLatMultiplier, frictionLongMultiplier

	// engine (vehEngineData)
	double		engineTorque;		// N m (engineMaxTorque)
	double		resistanceTorque;	// N m
	double		wheelsResistanceRatio;	// [interpreted] engine to wheel ratio
	double		finalGearScale;		// finalGearTorqueDecimationScalor
	double		minRPM, maxRPM;
	double		gearChangeTime, gearChangeCooldown;	// s
	int			numGears;			// [0] reverse, [1..] forward
	double		gears[BIKE_MAX_GEARS][5];	// minSpeed, maxSpeed, minRPM, maxRPM, torqueMultiplier
	double		reverseThreshold;	// m/s, below this S selects reverse

	// curves
	bikeCurve_t	torque;				// engine torque by rpm (0..1 over min..maxRPM)
	bikeCurve_t	speedToTilt, inputToTilt, speedToTiltSpeed;
	bikeCurve_t	latTraction, latTractionRear;	// traction by lateral slip, m/s

	// wheelie (xenjk: CP2077's is not in its data)
	double		wheelieLean;		// rad/s^2 at full lean back and throttle
	int			wheelieAssist;		// 1 = pull back before the balance point
	double		assistMargin;		// deg before the balance point
	double		assistKp;			// 1/s^2
	double		assistKd;			// 1/s
	double		pitchDamping;		// 1/s
	double		maxPitch;			// deg, loop-out
	double		landBailPitch;		// deg

	// air (vehAirControl)
	double		airPitchMaxVel;		// rad/s
	double		airPitchInputDamp;	// per frame
	int			airPitchStabilize;
	double		airPitchZeroAngle;	// deg
	double		airStabilizeRate;	// [interpreted] 1/s toward level without input
	double		airYawMaxVel;		// rad/s
	double		airYawInputDamp;	// per frame
} bikeParams_t;

typedef struct bikeState_s {
	bikeFloat_t	origin[3];		// u: originHeight above and originAhead ahead of the rear contact patch at rest, upright:
								// the body pitches about that patch (Q), the origin and its box stay upright over it
	bikeFloat_t	velocity[3];
	bikeFloat_t	yaw;			// deg, (-180, 180]
	bikeFloat_t	yawRate;		// deg/s, + = right
	bikeFloat_t	lean;			// deg
	bikeFloat_t	leanInt;		// tilt PID integral, deg s
	bikeFloat_t	pitch;			// deg, the body's (ground slope, suspension, wheelie, air)
	bikeFloat_t	pitchRate;		// deg/s
	bikeFloat_t	steer;			// slewed steer, -1..1
	bikeFloat_t	speed;			// forward speed, u/s, signed
	bikeFloat_t	comLong;		// m, COM ahead of the rear axle
	bikeFloat_t	rpm;			// derived from gear and speed each step (not state)
	int			gear;			// 0 reverse, 1..
	int			shiftMs;		// ms since the last gear change started, capped at BIKE_SHIFT_MS_MAX
	int			burnout;
	int			burnLaunch;		// launching off a burnout (its grip bonus applies)
	int			onGround;		// a wheel touches the ground
	int			commandTime;	// ms, end of the last command
	int			events;			// BIKE_EV_* of the last command
	int			droppedMs;		// total time dropped by the command cap
	// the last substep's wheels, [0] front [1] rear (outputs, recomputed every substep: not state)
	int			contact[2];
	bikeFloat_t	load[2];		// N, the spring's push
	bikeFloat_t	comp[2];		// m, suspension compression (0 = fully extended)
} bikeState_t;

struct bikeWorld_s;

// Height of the walkable surface under (x, y); returns 0 when there is none.
typedef int (*bikeGroundHeight_f)( void *ctx, double x, double y, double *z );
// BIKE_SURF_* under (x, y).
typedef int (*bikeSurface_f)( void *ctx, double x, double y );
// The ground under one wheel ([0] front, [1] rear): straight down from top (u) by depth; returns 1 and the height
// and surface normal of the first hit, 0 for none, -1 when top is inside something (the wheel is buried). NULL:
// groundHeight, its slope by central differences.
typedef int (*bikeWheelGround_f)( void *ctx, int wheel, const double top[3], double depth, double *z, double normal[3] );
// Moves bs->origin by bs->velocity for dt and resolves contact. The pmove
// adapter will supply a slide-move version; NULL selects the heightfield one.
typedef void (*bikeTranslate_f)( const struct bikeWorld_s *world, bikeState_t *bs, const bikeParams_t *bp, double dt );

typedef struct bikeWorld_s {
	bikeGroundHeight_f	groundHeight;
	bikeWheelGround_f	wheelGround;	// NULL: groundHeight, level
	bikeTranslate_f		translate;	// NULL: heightfield
	bikeSurface_f		surface;	// NULL: asphalt
	void				*ctx;
} bikeWorld_t;

void	BG_BikeDefaultParams( bikeParams_t *bp );	// a generic bike, when the pack has no handling file
// a handling file's text over *bp; returns the lines not understood (the first one's key in bad)
int		BG_BikeParseParams( bikeParams_t *bp, const char *text, char *bad, int badSize );
void	BG_BikeInit( bikeState_t *bs, const bikeParams_t *bp, const bikeWorld_t *world, double x, double y, double yaw, int commandTime );
int		BG_BikeRunCommand( bikeState_t *bs, const bikeParams_t *bp, const bikeInput_t *in, const bikeWorld_t *world, int serverTime );
void	BG_BikeSubstep( bikeState_t *bs, const bikeParams_t *bp, const bikeInput_t *in, const bikeWorld_t *world, double dt );
void	BG_BikeHeightfieldTranslate( const bikeWorld_t *world, bikeState_t *bs, const bikeParams_t *bp, double dt );
void	BG_BikeLand( bikeState_t *bs, const bikeParams_t *bp );
double	BG_BikeGearRPM( const bikeParams_t *bp, int gear, double v );	// engine rpm in a gear at v m/s (cgame's sound)

// a bike vehicle's parameters (bg_bikemove.c), read from ext_data/vehicles/<name>.bike when the vehicle is loaded
// (bg_vehicleLoad.c BG_BikeLoadParams, game and cgame from the same file); the generic bike for NULL or a vehicle
// without one
struct vehicleInfo_s;
const bikeParams_t *BG_BikeParams( const struct vehicleInfo_s *info );
const bikeParams_t *BG_BikeLoadParams( const struct vehicleInfo_s *info );

#ifdef __cplusplus
}
#endif
