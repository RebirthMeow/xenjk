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
=========================================================================
Demo aim-feedback overlay

The analysis is intentionally client/demo-side.  It treats the recorded
target path as fixed hindsight and asks for first-flight projectile contact;
it does not award floor/wall splash or contacts after a bounce.
=========================================================================
*/

#include "cg_local.h"

#define AF_MAX_SHOTS            32
#define AF_MAX_TARGET_SAMPLES   192
#define AF_MAX_PROJECTILES      2
#define AF_CAPTURE_MSEC         3600
#define AF_RESULT_DISPLAY_MSEC  1800
#define AF_SAMPLE_MIN_MSEC      20
#define AF_SAMPLE_MAX_GAP_MSEC  250
#define AF_TELEPORT_DIST        192.0f
#define AF_SEARCH_RANGE         18.0f
#define AF_HEAT_DIM             9
#define AF_HEAT_RANGE           12.0f
#define AF_GOLAN_MONTE_SAMPLES  40
#define AF_REVIEW_CACHE_PATH    "aimfeedback_review.afr"
#define AF_REVIEW_VALIDATED_PATH "aimfeedback_review_validated.afr"
#define AF_REVIEW_PROVEN_PATH   "aimfeedback_review_proven.afr"
#define AF_REVIEW_CACHE_MAGIC   0x31524641
#define AF_REVIEW_CACHE_VERSION 3
#define AF_REVIEW_RAW_MAX       2048
#define AF_REVIEW_MAX           24
#define AF_REVIEW_BURST_MSEC    700
#define AF_REVIEW_SPACING_MSEC  2400
#define AF_REVIEW_PREROLL_MSEC  1600
#define AF_REVIEW_POSTROLL_MSEC 650
#define AF_REVIEW_HOLD_REAL_MSEC 1400
#define AF_REVIEW_FAST_SCALE    "20"
#define AF_REVIEW_SLOW_SCALE    "0.30"
#define AF_REVIEW_PROOF_SCALE   "0.50"
#define AF_REVIEW_PROOF_TAIL_MSEC 300
#define AF_REVIEW_PROOF_VERIFY_MSEC 150
#define AF_REVIEW_VALIDATE_PREROLL_MSEC 500
#define AF_FC_TRACK_PATH         "aimfeedback_fc_track_v2.aft"
#define AF_FC_TRACK_MAGIC        0x31544641
#define AF_FC_TRACK_VERSION      2
#define AF_FC_TRACK_MAX          65536
#define AF_ENVELOPE_MAX_CELLS    768
#define AF_ENVELOPE_MAX_TESTED   2048
#define AF_ENVELOPE_MAX_CANDIDATES 64
#define AF_ENVELOPE_SOLVE_MSEC   100
#define AF_ENVELOPE_PREROLL_MSEC 1000
#define AF_ENVELOPE_FAST_SCALE   "12"
#define AF_ENVELOPE_NORMAL_SCALE "1"
#define AF_ENVELOPE_GOLAN_SAMPLES 16
#define AF_LESSON_DEFAULT_TIME    1217444
#define AF_LESSON_STAGE_COUNT     7
#define AF_LESSON_RANDOM_SAMPLES  64
#define AF_LESSON_FAST_SCALE      "20"
#define AF_LESSON_APPROACH_SCALE  "0.25"
#define AF_CANDIDATE_COUNT         5

typedef enum {
	AF_WEAPON_NONE,
	AF_WEAPON_PISTOL,
	AF_WEAPON_REPEATER_ALT,
	AF_WEAPON_THERMAL_ALT,
	AF_WEAPON_ROCKET,
	AF_WEAPON_GOLAN_ALT
} afWeaponClass_t;

typedef struct {
	int time;
	vec3_t origin;
	vec3_t mins;
	vec3_t maxs;
	qboolean alive;
} afTargetSample_t;

typedef struct {
	qboolean valid;
	int entityNum;
	int lastSeenTime;
	float speed;
	float pitchOffset;
	float yawOffset;
	int lifeMsec;
} afProjectileRoll_t;

typedef struct {
	qboolean found;
	float pitchDelta;
	float yawDelta;
	float contactTime;
} afCorrection_t;

typedef struct {
	qboolean ready;
	int readyTime;
	qboolean actualHit;
	afCorrection_t body;
	afCorrection_t head;
	afCorrection_t realized;
	float actualProbability;
	float bestProbability;
	float bestPitchDelta;
	float bestYawDelta;
	qboolean actualSampledGuaranteed;
	qboolean bestSampledGuaranteed;
	float heat[AF_HEAT_DIM][AF_HEAT_DIM];
	qboolean bodyHeat[AF_HEAT_DIM][AF_HEAT_DIM];
	qboolean headHeat[AF_HEAT_DIM][AF_HEAT_DIM];
	qboolean guaranteedHeat[AF_HEAT_DIM][AF_HEAT_DIM];
} afResult_t;

typedef struct {
	qboolean active;
	int shotTime;
	int shooterNum;
	int targetNum;
	int weapon;
	qboolean altFire;
	afWeaponClass_t weaponClass;
	vec3_t shooterOrigin;
	vec3_t shotAngles;
	vec3_t actualMuzzle;
	float shooterViewheight;
	float projectileSpeed;
	float projectileRadius;
	float verticalBoost;
	qboolean gravity;
	int maxFlightMsec;
	afProjectileRoll_t projectiles[AF_MAX_PROJECTILES];
	int numProjectiles;
	afTargetSample_t samples[AF_MAX_TARGET_SAMPLES];
	int numSamples;
	afResult_t result;
	qboolean cached;
} afShot_t;

typedef struct {
	int magic;
	int version;
	int entrySize;
} afReviewHeader_t;

typedef struct {
	int shotTime;
	int weaponClass;
	int targetNum;
	float score;
	int bodyFound;
	float bodyPitchDelta;
	float bodyYawDelta;
	float bodyContactTime;
	int headFound;
	float headPitchDelta;
	float headYawDelta;
	float headContactTime;
	float actualProbability;
	float bestProbability;
	float bestPitchDelta;
	float bestYawDelta;
	int bestSampledGuaranteed;
	int realizedFound;
	float realizedPitchDelta;
	float realizedYawDelta;
	float realizedContactTime;
	int proofValid;
	vec3_t proofMuzzle;
	vec3_t proofVelocity;
	int proofGravity;
	float proofRadius;
	float proofContactTime;
	vec3_t proofContactPoint;
} afReviewEntry_t;

typedef struct {
	qboolean started;
	qboolean previousValid;
	vec3_t previousRelative;
	vec3_t previousTargetOrigin;
} afValidationState_t;

typedef enum {
	AF_REVIEW_IDLE,
	AF_REVIEW_TRANSITION,
	AF_REVIEW_PREROLL,
	AF_REVIEW_HOLD,
	AF_REVIEW_POSTROLL,
	AF_REVIEW_DONE
} afReviewState_t;

typedef struct {
	float speed;
	float pitchOffset;
	float yawOffset;
	int lifeMsec;
} afGolanRoll_t;

typedef struct {
	int magic;
	int version;
	int entrySize;
} afFCTrackHeader_t;

typedef struct {
	int time;
	int carrierNum;
	vec3_t origin;
	vec3_t mins;
	vec3_t maxs;
	int alive;
	vec3_t shooterOrigin;
	vec3_t shooterAngles;
	int shooterViewheight;
	int shooterWeapon;
	int shooterHealth;
} afFCTrackEntry_t;

typedef struct {
	float pitchDelta;
	float yawDelta;
	float margin;
} afEnvelopeCandidate_t;

typedef struct {
	float pitchDelta;
	float yawDelta;
	float probability;
	float contactTime;
	int pitchKey;
	int yawKey;
	int neighbors;
} afEnvelopeCell_t;

static afShot_t af_shots[AF_MAX_SHOTS];
static int af_nextShot;
static int af_latestShot = -1;
static int af_lastTime;
static afReviewEntry_t af_reviewEntries[AF_REVIEW_MAX];
static afReviewEntry_t af_reviewRaw[AF_REVIEW_RAW_MAX];
static afValidationState_t af_validationCurrent;
static int af_reviewCount;
static int af_validationRawCount;
static int af_validationIndex;
static int af_validationCompleteCount;
static int af_validationPassedCount;
static qboolean af_validationFinished;
static int af_reviewIndex;
static int af_reviewHoldStart;
static afReviewState_t af_reviewState;
static qboolean af_reviewInitialized;
static qboolean af_reviewProofLogged;
static qhandle_t af_reviewGhostShader;
static qhandle_t af_reviewLineShader;
static qhandle_t af_reviewSphereModel;
static qhandle_t af_reviewRocketModel;
static qhandle_t af_reviewThermalModel;
static qhandle_t af_reviewGolanModel;
static int af_reviewProofTrackedIndex;
static qboolean af_reviewProofImpact;
static float af_reviewProofImpactTime;
static vec3_t af_reviewProofImpactPoint;
static qboolean af_reviewProofPreviousValid;
static float af_reviewProofPreviousTime;
static vec3_t af_reviewProofPreviousPosition;
static vec3_t af_reviewProofPreviousRelative;
static vec3_t af_reviewProofPreviousTargetOrigin;
static afFCTrackEntry_t af_fcTrack[AF_FC_TRACK_MAX];
static int af_fcTrackCount;
static int af_fcTrackCursor;
static int af_fcTrackLastWriteTime;
static int af_fcTrackWriteCount;
static fileHandle_t af_fcTrackFile;
static afShot_t af_envelopeShot;
static afEnvelopeCell_t af_envelopeCells[AF_ENVELOPE_MAX_CELLS];
static int af_envelopeCellCount;
static int af_envelopeBestCell;
static int af_envelopeTargetNum;
static int af_envelopeLastSolveTime;
static int af_envelopeLastWeapon;
static int af_envelopeScaleState;
static int af_envelopeCaptureMask;
static int af_envelopeCaptureWeapon;
static int af_envelopeCaptureScreenshotTime;
static int af_envelopeCaptureUnfreezeTime;
static int af_envelopeTestedPitch[AF_ENVELOPE_MAX_TESTED];
static int af_envelopeTestedYaw[AF_ENVELOPE_MAX_TESTED];
static qboolean af_lessonFrozen;
static int af_lessonStage;
static int af_lessonStageStart;
static qboolean af_lessonCaptured;
static qboolean af_lessonFinished;
static int af_lessonTrackIndex;
static vec3_t af_lessonTargetVelocity;
static float af_lessonDistance;
static float af_lessonTargetSpeed;
static float af_lessonLateralSpeed;
static float af_lessonAwaySpeed;
static qboolean af_lessonNominalAimFound;
static float af_lessonNominalPitchDelta;
static float af_lessonNominalYawDelta;
static float af_lessonNominalContactTime;
static int af_candidateIndex;

/* User-selected airborne frames from one Kejim 4v4 demo's
   timeline.  Candidate mode solves the deterministic 900 ups rocket baseline
   at each frame so its aim field can be checked against the recorded arc. */
static const int af_candidateTimes[AF_CANDIDATE_COUNT] = {
	884441, 965588, 1493852, 1494776, 1639250
};

/* The lab drives timescale and cl_freezeDemo only while one of its modes is
   on, and puts them back at shutdown only if it changed them: written on
   every load and shutdown, they warned "timescale can only be set by server"
   at each map change on a remote server and reset a local timescale. */
static qboolean af_drovePlayback;

static qboolean AF_LabActive( void )
{
	return cg_aimFeedbackLesson.integer != 0 || cg_aimFeedbackEnvelope.integer != 0 ||
		cg_aimFeedbackReview.integer != 0;
}

static void AF_SetPlaybackScale( const char *scale )
{
	if ( !af_drovePlayback && !AF_LabActive() ) {
		return;
	}
	af_drovePlayback = qtrue;
	trap->Cvar_Set( "timescale", scale );
}

static void AF_SetDemoFrozen( qboolean frozen )
{
	if ( !af_drovePlayback && !AF_LabActive() ) {
		return;
	}
	af_drovePlayback = qtrue;
	trap->Cvar_Set( "cl_freezeDemo", frozen ? "1" : "0" );
}

static float AF_ClampFloat( float value, float low, float high )
{
	if ( value < low ) {
		return low;
	}
	if ( value > high ) {
		return high;
	}
	return value;
}

static void AF_SetColor( vec4_t color, float red, float green, float blue, float alpha )
{
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;
}

static void AF_DecodeBounds( const entityState_t *state, vec3_t mins, vec3_t maxs )
{
	int x, zd, zu;

	if ( state->solid && state->solid != SOLID_BMODEL ) {
		x = state->solid & 255;
		zd = ( state->solid >> 8 ) & 255;
		zu = ( ( state->solid >> 16 ) & 255 ) - 32;
		VectorSet( mins, -x, -x, -zd );
		VectorSet( maxs,  x,  x,  zu );
		return;
	}

	VectorSet( mins, -15.0f, -15.0f, -24.0f );
	VectorSet( maxs,  15.0f,  15.0f,  40.0f );
}

static const entityState_t *AF_SnapshotEntity( int entityNum )
{
	int i;

	if ( !cg.snap ) {
		return NULL;
	}

	if ( entityNum == cg.snap->ps.clientNum ) {
		return &cg_entities[entityNum].currentState;
	}

	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		if ( cg.snap->entities[i].number == entityNum ) {
			return &cg.snap->entities[i];
		}
	}

	return NULL;
}

static void AF_CalcMuzzle( const afShot_t *shot, const vec3_t viewAngles, vec3_t muzzle )
{
	vec3_t forward, right, up;
	vec3_t offset;

	AngleVectors( viewAngles, forward, right, up );
	VectorCopy( shot->shooterOrigin, muzzle );
	VectorCopy( WP_MuzzlePoint[shot->weapon], offset );
	VectorMA( muzzle, offset[0], forward, muzzle );
	VectorMA( muzzle, offset[1], right, muzzle );
	muzzle[2] += shot->shooterViewheight + offset[2];
	SnapVector( muzzle );
}

static afWeaponClass_t AF_ClassifyWeapon( int weapon, qboolean altFire )
{
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return AF_WEAPON_PISTOL;
	case WP_REPEATER:
		return altFire ? AF_WEAPON_REPEATER_ALT : AF_WEAPON_NONE;
	case WP_THERMAL:
		return altFire ? AF_WEAPON_THERMAL_ALT : AF_WEAPON_NONE;
	case WP_ROCKET_LAUNCHER:
		return altFire ? AF_WEAPON_NONE : AF_WEAPON_ROCKET;
	case WP_FLECHETTE:
		return altFire ? AF_WEAPON_GOLAN_ALT : AF_WEAPON_NONE;
	default:
		return AF_WEAPON_NONE;
	}
}

static const char *AF_WeaponName( afWeaponClass_t weaponClass )
{
	switch ( weaponClass ) {
	case AF_WEAPON_PISTOL:       return "PISTOL";
	case AF_WEAPON_REPEATER_ALT: return "REPEATER ALT";
	case AF_WEAPON_THERMAL_ALT:  return "THERMAL ALT";
	case AF_WEAPON_ROCKET:       return "ROCKET";
	case AF_WEAPON_GOLAN_ALT:    return "GOLAN ALT";
	default:                     return "UNKNOWN";
	}
}

static afWeaponClass_t AF_EnvelopeClassForWeapon( int weapon )
{
	switch ( weapon ) {
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return AF_WEAPON_PISTOL;
	case WP_REPEATER:
		return AF_WEAPON_REPEATER_ALT;
	case WP_THERMAL:
		return AF_WEAPON_THERMAL_ALT;
	case WP_ROCKET_LAUNCHER:
		return AF_WEAPON_ROCKET;
	case WP_FLECHETTE:
		return AF_WEAPON_GOLAN_ALT;
	default:
		return AF_WEAPON_NONE;
	}
}

static qboolean AF_SetupEnvelopeShot( afShot_t *shot, int targetNum )
{
	memset( shot, 0, sizeof( *shot ) );
	shot->active = qtrue;
	shot->shotTime = cg.time;
	shot->shooterNum = cg.snap->ps.clientNum;
	shot->targetNum = targetNum;
	shot->weapon = cg.snap->ps.weapon;
	shot->weaponClass = AF_EnvelopeClassForWeapon( shot->weapon );
	if ( shot->weaponClass == AF_WEAPON_NONE ) {
		return qfalse;
	}
	shot->altFire = shot->weaponClass == AF_WEAPON_REPEATER_ALT ||
		shot->weaponClass == AF_WEAPON_THERMAL_ALT ||
		shot->weaponClass == AF_WEAPON_GOLAN_ALT;
	VectorCopy( cg.snap->ps.origin, shot->shooterOrigin );
	VectorCopy( cg.snap->ps.viewangles, shot->shotAngles );
	shot->shooterViewheight = cg.snap->ps.viewheight;
	AF_CalcMuzzle( shot, shot->shotAngles, shot->actualMuzzle );

	switch ( shot->weaponClass ) {
	case AF_WEAPON_PISTOL:
		shot->projectileSpeed = 1600.0f;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	case AF_WEAPON_REPEATER_ALT:
		shot->projectileSpeed = 1100.0f;
		shot->projectileRadius = 3.0f;
		shot->verticalBoost = 40.0f;
		shot->gravity = qtrue;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	case AF_WEAPON_THERMAL_ALT:
		/* The envelope represents a fully charged alternate throw. */
		shot->projectileSpeed = 900.0f;
		shot->projectileRadius = 3.0f;
		shot->verticalBoost = 120.0f;
		shot->gravity = qtrue;
		shot->maxFlightMsec = 3000;
		break;
	case AF_WEAPON_ROCKET:
		shot->projectileSpeed = 900.0f;
		shot->projectileRadius = 3.0f;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	case AF_WEAPON_GOLAN_ALT:
		shot->projectileRadius = 3.0f;
		shot->gravity = qtrue;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	default:
		return qfalse;
	}
	return qtrue;
}

static qboolean AF_IsEnemy( int shooterNum, int candidateNum )
{
	team_t shooterTeam;
	team_t candidateTeam;

	if ( candidateNum == shooterNum || candidateNum < 0 || candidateNum >= MAX_CLIENTS ) {
		return qfalse;
	}
	if ( !cgs.clientinfo[candidateNum].infoValid ) {
		return qfalse;
	}

	shooterTeam = cgs.clientinfo[shooterNum].team;
	candidateTeam = cgs.clientinfo[candidateNum].team;
	if ( candidateTeam == TEAM_SPECTATOR ) {
		return qfalse;
	}
	if ( shooterTeam == TEAM_RED || shooterTeam == TEAM_BLUE ) {
		return candidateTeam != shooterTeam;
	}

	return qtrue;
}

static qboolean AF_IsFlagCarrier( int clientNum, const entityState_t *state )
{
	int flagBits = ( 1 << PW_REDFLAG ) | ( 1 << PW_BLUEFLAG ) | ( 1 << PW_NEUTRALFLAG );

	return ( state->powerups & flagBits ) ||
		( cgs.clientinfo[clientNum].powerups & flagBits );
}

static int AF_InferTarget( int shooterNum, const vec3_t muzzle, const vec3_t shotAngles )
{
	vec3_t forward;
	vec3_t bestCenter;
	vec3_t bestFlagCenter;
	float bestScore = 99999.0f;
	float bestFlagScore = 99999.0f;
	int bestTarget = -1;
	int bestFlagTarget = -1;
	int i;

	AngleVectors( shotAngles, forward, NULL, NULL );

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const entityState_t *state;
		vec3_t origin, mins, maxs, center, toTarget;
		float distance, dot, angle, score;
		trace_t worldTrace;

		if ( !AF_IsEnemy( shooterNum, i ) ) {
			continue;
		}
		state = AF_SnapshotEntity( i );
		if ( !state || state->eType != ET_PLAYER || ( state->eFlags & EF_DEAD ) ) {
			continue;
		}

		BG_EvaluateTrajectory( &state->pos, cg.snap->serverTime, origin );
		AF_DecodeBounds( state, mins, maxs );
		VectorCopy( origin, center );
		center[2] += ( mins[2] + maxs[2] ) * 0.5f;
		VectorSubtract( center, muzzle, toTarget );
		distance = VectorNormalize( toTarget );
		if ( distance <= 1.0f ) {
			continue;
		}
		dot = DotProduct( forward, toTarget );
		if ( dot < 0.80f ) {
			continue;
		}
		angle = RAD2DEG( acosf( AF_ClampFloat( dot, -1.0f, 1.0f ) ) );
		if ( angle > 30.0f ) {
			continue;
		}

		trap->CM_Trace( &worldTrace, muzzle, center, NULL, NULL, 0, MASK_SHOT, 0 );
		if ( worldTrace.fraction < 1.0f ) {
			continue;
		}

		score = angle + distance * 0.0005f;
		if ( score < bestScore ) {
			bestScore = score;
			bestTarget = i;
			VectorCopy( center, bestCenter );
		}
		if ( AF_IsFlagCarrier( i, state ) && score < bestFlagScore ) {
			bestFlagScore = score;
			bestFlagTarget = i;
			VectorCopy( center, bestFlagCenter );
		}
	}

	/* When two plausible enemies overlap the aim lane, treat the enemy flag
	   carrier as the intended target instead of selecting an escort by a tiny
	   angular margin. */
	if ( bestFlagTarget >= 0 && ( bestTarget == bestFlagTarget ||
		( bestTarget >= 0 && bestFlagScore <= bestScore + 6.0f &&
			Distance( bestCenter, bestFlagCenter ) <= 192.0f ) ) ) {
		return bestFlagTarget;
	}

	return bestTarget;
}

static int AF_FindEnemyFlagCarrier( int shooterNum, const entityState_t **carrierState )
{
	int i;

	if ( carrierState ) {
		*carrierState = NULL;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		const entityState_t *state;

		if ( !AF_IsEnemy( shooterNum, i ) ) {
			continue;
		}
		state = AF_SnapshotEntity( i );
		if ( !state && cg_entities[i].currentValid ) {
			state = &cg_entities[i].currentState;
		}
		if ( !state || state->eType != ET_PLAYER || ( state->eFlags & EF_DEAD ) ||
			!AF_IsFlagCarrier( i, state ) ) {
			continue;
		}
		if ( carrierState ) {
			*carrierState = state;
		}
		return i;
	}

	return -1;
}

static qboolean AF_BeginFCTrackCache( void )
{
	afFCTrackHeader_t header;

	header.magic = AF_FC_TRACK_MAGIC;
	header.version = AF_FC_TRACK_VERSION;
	header.entrySize = sizeof( afFCTrackEntry_t );
	trap->FS_Open( AF_FC_TRACK_PATH, &af_fcTrackFile, FS_WRITE );
	if ( !af_fcTrackFile ) {
		trap->Print( "[aimfeedback] could not create %s\n", AF_FC_TRACK_PATH );
		return qfalse;
	}
	trap->FS_Write( &header, sizeof( header ), af_fcTrackFile );
	trap->Print( "[aimfeedback] enemy-FC track preprocessing started\n" );
	return qtrue;
}

static void AF_RecordFCTrackSample( void )
{
	afFCTrackEntry_t entry;
	const entityState_t *state = NULL;
	int shooterNum;

	if ( !af_fcTrackFile || !cg.snap ||
		cg.snap->serverTime <= af_fcTrackLastWriteTime ) {
		return;
	}
	shooterNum = cg.snap->ps.clientNum;
	memset( &entry, 0, sizeof( entry ) );
	entry.time = cg.snap->serverTime;
	entry.carrierNum = AF_FindEnemyFlagCarrier( shooterNum, &state );
	VectorCopy( cg.snap->ps.origin, entry.shooterOrigin );
	VectorCopy( cg.snap->ps.viewangles, entry.shooterAngles );
	entry.shooterViewheight = cg.snap->ps.viewheight;
	entry.shooterWeapon = cg.snap->ps.weapon;
	entry.shooterHealth = cg.snap->ps.stats[STAT_HEALTH];
	if ( entry.carrierNum >= 0 && state ) {
		BG_EvaluateTrajectory( &state->pos, entry.time, entry.origin );
		AF_DecodeBounds( state, entry.mins, entry.maxs );
		entry.alive = !( state->eFlags & EF_DEAD );
	}
	trap->FS_Write( &entry, sizeof( entry ), af_fcTrackFile );
	af_fcTrackLastWriteTime = entry.time;
	af_fcTrackWriteCount++;
}

static qboolean AF_LoadFCTrackCache( void )
{
	afFCTrackHeader_t header;
	fileHandle_t file;
	int fileLength;
	int count;

	fileLength = trap->FS_Open( AF_FC_TRACK_PATH, &file, FS_READ );
	if ( !file || fileLength < (int)sizeof( header ) ) {
		if ( file ) {
			trap->FS_Close( file );
		}
		trap->Print( "[aimfeedback] FC track not found: %s\n", AF_FC_TRACK_PATH );
		return qfalse;
	}
	trap->FS_Read( &header, sizeof( header ), file );
	if ( header.magic != AF_FC_TRACK_MAGIC ||
		header.version != AF_FC_TRACK_VERSION ||
		header.entrySize != sizeof( afFCTrackEntry_t ) ) {
		trap->FS_Close( file );
		trap->Print( "[aimfeedback] incompatible FC track: %s\n", AF_FC_TRACK_PATH );
		return qfalse;
	}
	count = ( fileLength - sizeof( header ) ) / sizeof( afFCTrackEntry_t );
	if ( count <= 0 || count > AF_FC_TRACK_MAX ) {
		trap->FS_Close( file );
		trap->Print( "[aimfeedback] invalid FC track count: %i\n", count );
		return qfalse;
	}
	trap->FS_Read( af_fcTrack, count * sizeof( afFCTrackEntry_t ), file );
	trap->FS_Close( file );
	af_fcTrackCount = count;
	af_fcTrackCursor = 0;
	trap->Print( "[aimfeedback] loaded %i enemy-FC timeline samples\n", count );
	return qtrue;
}

static int AF_FCTrackIndexAtTime( int time )
{
	if ( af_fcTrackCount <= 0 ) {
		return -1;
	}
	if ( af_fcTrackCursor >= af_fcTrackCount ) {
		af_fcTrackCursor = af_fcTrackCount - 1;
	}
	if ( time < af_fcTrack[af_fcTrackCursor].time ) {
		af_fcTrackCursor = 0;
	}
	while ( af_fcTrackCursor + 1 < af_fcTrackCount &&
		af_fcTrack[af_fcTrackCursor + 1].time <= time ) {
		af_fcTrackCursor++;
	}
	return af_fcTrackCursor;
}

static void AF_AddTargetSampleFromState( afShot_t *shot, const entityState_t *state, int time, const vec3_t origin )
{
	afTargetSample_t *sample;

	if ( shot->numSamples >= AF_MAX_TARGET_SAMPLES ) {
		return;
	}
	if ( shot->numSamples > 0 ) {
		int elapsed = time - shot->samples[shot->numSamples - 1].time;
		if ( elapsed < AF_SAMPLE_MIN_MSEC ) {
			return;
		}
	}

	sample = &shot->samples[shot->numSamples++];
	sample->time = time;
	VectorCopy( origin, sample->origin );
	AF_DecodeBounds( state, sample->mins, sample->maxs );
	sample->alive = ( state->eType == ET_PLAYER && !( state->eFlags & EF_DEAD ) );
}

static void AF_AddInitialTargetSample( afShot_t *shot )
{
	const entityState_t *state = AF_SnapshotEntity( shot->targetNum );
	vec3_t origin;

	if ( !state ) {
		return;
	}
	BG_EvaluateTrajectory( &state->pos, shot->shotTime, origin );
	AF_AddTargetSampleFromState( shot, state, shot->shotTime, origin );
}

static void AF_AddProjectile( afShot_t *shot, const entityState_t *state )
{
	afProjectileRoll_t *roll;
	vec3_t velocity, projectileAngles;
	int i;

	if ( !state || state->eType != ET_MISSILE || state->weapon != shot->weapon ) {
		return;
	}
	if ( abs( state->pos.trTime - shot->shotTime ) > 120 ) {
		return;
	}
	if ( Distance( state->pos.trBase, shot->actualMuzzle ) > 96.0f ) {
		return;
	}

	for ( i = 0; i < shot->numProjectiles; i++ ) {
		if ( shot->projectiles[i].entityNum == state->number ) {
			return;
		}
	}
	if ( shot->numProjectiles >= AF_MAX_PROJECTILES ) {
		return;
	}

	roll = &shot->projectiles[shot->numProjectiles++];
	memset( roll, 0, sizeof( *roll ) );
	roll->valid = qtrue;
	roll->entityNum = state->number;
	roll->lastSeenTime = shot->shotTime;
	roll->lifeMsec = 1500;
	VectorCopy( state->pos.trDelta, velocity );

	if ( shot->weaponClass == AF_WEAPON_THERMAL_ALT ) {
		velocity[2] -= 120.0f;
		shot->projectileSpeed = VectorLength( velocity );
	}

	roll->speed = VectorLength( velocity );
	if ( roll->speed > 0.0f ) {
		vectoangles( velocity, projectileAngles );
		roll->pitchOffset = AngleSubtract( projectileAngles[PITCH], shot->shotAngles[PITCH] );
		roll->yawOffset = AngleSubtract( projectileAngles[YAW], shot->shotAngles[YAW] );
	}

	if ( shot->weaponClass == AF_WEAPON_PISTOL && shot->altFire && state->generic1 > 0 ) {
		shot->projectileRadius = state->generic1 * 0.5f;
	}
}

static void AF_FindInitialProjectiles( afShot_t *shot )
{
	int i;

	if ( !cg.snap ) {
		return;
	}
	for ( i = 0; i < cg.snap->numEntities; i++ ) {
		AF_AddProjectile( shot, &cg.snap->entities[i] );
	}
}

static qboolean AF_SegmentAABB( const vec3_t start, const vec3_t end,
	const vec3_t mins, const vec3_t maxs, float *entryFraction )
{
	float enter = 0.0f;
	float leave = 1.0f;
	int axis;

	for ( axis = 0; axis < 3; axis++ ) {
		float delta = end[axis] - start[axis];
		float t1, t2, inverse;

		if ( fabsf( delta ) < 0.0001f ) {
			if ( start[axis] < mins[axis] || start[axis] > maxs[axis] ) {
				return qfalse;
			}
			continue;
		}

		inverse = 1.0f / delta;
		t1 = ( mins[axis] - start[axis] ) * inverse;
		t2 = ( maxs[axis] - start[axis] ) * inverse;
		if ( t1 > t2 ) {
			float swap = t1;
			t1 = t2;
			t2 = swap;
		}
		if ( t1 > enter ) enter = t1;
		if ( t2 < leave ) leave = t2;
		if ( enter > leave ) {
			return qfalse;
		}
	}

	if ( entryFraction ) {
		*entryFraction = enter;
	}
	return qtrue;
}

static void AF_EvaluateProjectile( const vec3_t muzzle, const vec3_t velocity,
	qboolean gravity, float seconds, vec3_t position )
{
	VectorMA( muzzle, seconds, velocity, position );
	if ( gravity ) {
		position[2] -= 400.0f * seconds * seconds;
	}
}

static qboolean AF_WorldClearToContact( const vec3_t muzzle,
	const vec3_t velocity, qboolean gravity, float radius, float contactSeconds )
{
	vec3_t previous, current;
	vec3_t mins, maxs;
	float time = 0.0f;

	VectorSet( mins, -radius, -radius, -radius );
	VectorSet( maxs,  radius,  radius,  radius );
	VectorCopy( muzzle, previous );

	while ( time < contactSeconds ) {
		float nextTime = time + 0.025f;
		trace_t trace;

		if ( nextTime > contactSeconds ) {
			nextTime = contactSeconds;
		}
		AF_EvaluateProjectile( muzzle, velocity, gravity, nextTime, current );
		trap->CM_Trace( &trace, previous, current, mins, maxs, 0, MASK_SHOT, 0 );
		if ( trace.fraction < 1.0f || trace.startsolid || trace.allsolid ) {
			return qfalse;
		}
		VectorCopy( current, previous );
		time = nextTime;
	}

	return qtrue;
}

static qboolean AF_TestTrajectory( const afShot_t *shot, const vec3_t viewAngles,
	float speed, float pitchOffset, float yawOffset, int lifeMsec,
	qboolean headOnly, float *contactTime )
{
	vec3_t launchAngles, forward, velocity, muzzle;
	int i;

	VectorCopy( viewAngles, launchAngles );
	launchAngles[PITCH] = AngleNormalize180( launchAngles[PITCH] + pitchOffset );
	launchAngles[YAW] = AngleNormalize180( launchAngles[YAW] + yawOffset );
	AngleVectors( launchAngles, forward, NULL, NULL );
	VectorScale( forward, speed, velocity );
	velocity[2] += shot->verticalBoost;
	AF_CalcMuzzle( shot, viewAngles, muzzle );

	for ( i = 1; i < shot->numSamples; i++ ) {
		const afTargetSample_t *previousSample = &shot->samples[i - 1];
		const afTargetSample_t *sample = &shot->samples[i];
		float previousSeconds = ( previousSample->time - shot->shotTime ) * 0.001f;
		float seconds = ( sample->time - shot->shotTime ) * 0.001f;
		vec3_t projectilePrevious, projectileCurrent;
		vec3_t relativePrevious, relativeCurrent;
		vec3_t mins, maxs, targetMove;
		float fraction;

		if ( seconds <= 0.0f || sample->time - previousSample->time > AF_SAMPLE_MAX_GAP_MSEC ) {
			continue;
		}
		if ( seconds * 1000.0f > lifeMsec || seconds * 1000.0f > shot->maxFlightMsec ) {
			break;
		}
		if ( !previousSample->alive ) {
			continue;
		}
		VectorSubtract( sample->origin, previousSample->origin, targetMove );
		if ( VectorLength( targetMove ) > AF_TELEPORT_DIST ) {
			continue;
		}

		AF_EvaluateProjectile( muzzle, velocity, shot->gravity, previousSeconds, projectilePrevious );
		AF_EvaluateProjectile( muzzle, velocity, shot->gravity, seconds, projectileCurrent );
		VectorSubtract( projectilePrevious, previousSample->origin, relativePrevious );
		VectorSubtract( projectileCurrent, sample->origin, relativeCurrent );

		if ( headOnly ) {
			VectorSet( mins, -8.0f, -8.0f, sample->maxs[2] - 14.0f );
			VectorSet( maxs,  8.0f,  8.0f, sample->maxs[2] );
		} else {
			VectorCopy( sample->mins, mins );
			VectorCopy( sample->maxs, maxs );
		}
		mins[0] -= shot->projectileRadius;
		mins[1] -= shot->projectileRadius;
		mins[2] -= shot->projectileRadius;
		maxs[0] += shot->projectileRadius;
		maxs[1] += shot->projectileRadius;
		maxs[2] += shot->projectileRadius;

		if ( AF_SegmentAABB( relativePrevious, relativeCurrent, mins, maxs, &fraction ) ) {
			float hitSeconds = previousSeconds + ( seconds - previousSeconds ) * fraction;
			if ( AF_WorldClearToContact( muzzle, velocity, shot->gravity,
				shot->projectileRadius, hitSeconds ) ) {
				if ( contactTime ) {
					*contactTime = hitSeconds;
				}
				return qtrue;
			}
			return qfalse;
		}
	}

	return qfalse;
}

static qboolean AF_TestDeterministic( const afShot_t *shot, float pitchDelta,
	float yawDelta, qboolean headOnly, float *contactTime )
{
	vec3_t angles;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );
	return AF_TestTrajectory( shot, angles, shot->projectileSpeed, 0.0f, 0.0f,
		shot->maxFlightMsec, headOnly, contactTime );
}

static qboolean AF_TestRealizedGolan( const afShot_t *shot, float pitchDelta,
	float yawDelta, float *contactTime )
{
	vec3_t angles;
	int i;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );

	for ( i = 0; i < shot->numProjectiles; i++ ) {
		float thisContact = 0.0f;
		const afProjectileRoll_t *roll = &shot->projectiles[i];
		if ( roll->valid && AF_TestTrajectory( shot, angles, roll->speed,
			roll->pitchOffset, roll->yawOffset, roll->lifeMsec, qfalse, &thisContact ) ) {
			if ( contactTime && ( *contactTime <= 0.0f || thisContact < *contactTime ) ) {
				*contactTime = thisContact;
			}
			return qtrue;
		}
	}
	return qfalse;
}

static float AF_Halton( int index, int base )
{
	float result = 0.0f;
	float fraction = 1.0f;

	while ( index > 0 ) {
		fraction /= (float)base;
		result += fraction * (float)( index % base );
		index /= base;
	}
	return result;
}

static void AF_MakeGolanRoll( int sampleIndex, afGolanRoll_t *roll )
{
	int index = sampleIndex + 1;
	roll->pitchOffset = -( 8.0f + 4.0f * AF_Halton( index, 2 ) );
	roll->yawOffset = -2.0f + 4.0f * AF_Halton( index, 3 );
	roll->speed = 700.0f + 700.0f * AF_Halton( index, 5 );
	roll->lifeMsec = 1500 + (int)( 2000.0f * AF_Halton( index, 7 ) );
}

static float AF_GolanProbability( const afShot_t *shot, float pitchDelta, float yawDelta )
{
	vec3_t angles;
	int hits = 0;
	int i;
	float singleProbability;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );

	for ( i = 0; i < AF_GOLAN_MONTE_SAMPLES; i++ ) {
		afGolanRoll_t roll;
		AF_MakeGolanRoll( i, &roll );
		if ( AF_TestTrajectory( shot, angles, roll.speed, roll.pitchOffset,
			roll.yawOffset, roll.lifeMsec, qfalse, NULL ) ) {
			hits++;
		}
	}

	singleProbability = hits / (float)AF_GOLAN_MONTE_SAMPLES;
	return 1.0f - ( 1.0f - singleProbability ) * ( 1.0f - singleProbability );
}

static qboolean AF_GolanSampledGuaranteed( const afShot_t *shot, float pitchDelta, float yawDelta )
{
	vec3_t angles;
	int pitchEdge, yawEdge, speedEdge, lifeEdge;
	int i;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );

	for ( i = 0; i < AF_GOLAN_MONTE_SAMPLES; i++ ) {
		afGolanRoll_t roll;
		AF_MakeGolanRoll( i, &roll );
		if ( !AF_TestTrajectory( shot, angles, roll.speed, roll.pitchOffset,
			roll.yawOffset, roll.lifeMsec, qfalse, NULL ) ) {
			return qfalse;
		}
	}

	for ( pitchEdge = 0; pitchEdge < 2; pitchEdge++ ) {
		for ( yawEdge = 0; yawEdge < 2; yawEdge++ ) {
			for ( speedEdge = 0; speedEdge < 2; speedEdge++ ) {
				for ( lifeEdge = 0; lifeEdge < 2; lifeEdge++ ) {
				afGolanRoll_t roll;
				roll.pitchOffset = pitchEdge ? -12.0f : -8.0f;
				roll.yawOffset = yawEdge ? 2.0f : -2.0f;
				roll.speed = speedEdge ? 1400.0f : 700.0f;
				roll.lifeMsec = lifeEdge ? 3500 : 1500;
				if ( !AF_TestTrajectory( shot, angles, roll.speed, roll.pitchOffset,
					roll.yawOffset, roll.lifeMsec, qfalse, NULL ) ) {
					return qfalse;
				}
			}
		}
	}
	}

	return qtrue;
}

static qboolean AF_AddEnvelopeTargetSamples( afShot_t *shot, int trackIndex )
{
	const afFCTrackEntry_t *current;
	afTargetSample_t *sample;
	int targetNum;
	int i;

	if ( trackIndex < 0 || trackIndex >= af_fcTrackCount ) {
		return qfalse;
	}
	current = &af_fcTrack[trackIndex];
	if ( current->time > cg.time || current->carrierNum < 0 || !current->alive ) {
		return qfalse;
	}
	targetNum = current->carrierNum;
	shot->targetNum = targetNum;
	sample = &shot->samples[shot->numSamples++];
	memset( sample, 0, sizeof( *sample ) );
	sample->time = cg.time;
	VectorCopy( current->origin, sample->origin );
	VectorCopy( current->mins, sample->mins );
	VectorCopy( current->maxs, sample->maxs );
	sample->alive = current->alive;

	if ( trackIndex + 1 < af_fcTrackCount ) {
		const afFCTrackEntry_t *next = &af_fcTrack[trackIndex + 1];
		if ( next->carrierNum == targetNum && next->alive &&
			next->time > current->time && cg.time < next->time &&
			next->time - current->time <= AF_SAMPLE_MAX_GAP_MSEC ) {
			float fraction = ( cg.time - current->time ) /
				(float)( next->time - current->time );
			vec3_t move;
			VectorSubtract( next->origin, current->origin, move );
			VectorMA( current->origin, fraction, move, sample->origin );
		}
	}

	for ( i = trackIndex + 1; i < af_fcTrackCount &&
		shot->numSamples < AF_MAX_TARGET_SAMPLES; i++ ) {
		const afFCTrackEntry_t *entry = &af_fcTrack[i];
		const afFCTrackEntry_t *previous = &af_fcTrack[i - 1];

		if ( entry->time <= cg.time ) {
			continue;
		}
		if ( entry->time > cg.time + shot->maxFlightMsec ||
			entry->carrierNum != targetNum || !entry->alive ||
			entry->time - previous->time > AF_SAMPLE_MAX_GAP_MSEC ||
			Distance( entry->origin, previous->origin ) > AF_TELEPORT_DIST ) {
			break;
		}
		sample = &shot->samples[shot->numSamples++];
		sample->time = entry->time;
		VectorCopy( entry->origin, sample->origin );
		VectorCopy( entry->mins, sample->mins );
		VectorCopy( entry->maxs, sample->maxs );
		sample->alive = entry->alive;
	}

	return shot->numSamples >= 2;
}

static qboolean AF_EnvelopeDirectionForSample( const afShot_t *shot,
	const afTargetSample_t *sample, float pitchOffset, float yawOffset,
	float *pitchDelta, float *yawDelta, float *requiredSpeed, float *margin )
{
	vec3_t center, desired, launchAngles, viewAngles, muzzle;
	float seconds;
	float targetRadius;
	float distance;
	int iteration;

	seconds = ( sample->time - shot->shotTime ) * 0.001f;
	if ( seconds <= 0.015f ) {
		return qfalse;
	}
	VectorCopy( sample->origin, center );
	center[2] += ( sample->mins[2] + sample->maxs[2] ) * 0.5f;
	VectorCopy( shot->shotAngles, viewAngles );

	for ( iteration = 0; iteration < 2; iteration++ ) {
		AF_CalcMuzzle( shot, viewAngles, muzzle );
		VectorSubtract( center, muzzle, desired );
		if ( shot->gravity ) {
			desired[2] += 400.0f * seconds * seconds -
				shot->verticalBoost * seconds;
		}
		if ( VectorLength( desired ) < 1.0f ) {
			return qfalse;
		}
		vectoangles( desired, launchAngles );
		viewAngles[PITCH] = AngleNormalize180( launchAngles[PITCH] - pitchOffset );
		viewAngles[YAW] = AngleNormalize180( launchAngles[YAW] - yawOffset );
	}

	distance = VectorLength( desired );
	*requiredSpeed = distance / seconds;
	*pitchDelta = AngleSubtract( viewAngles[PITCH], shot->shotAngles[PITCH] );
	*yawDelta = AngleSubtract( viewAngles[YAW], shot->shotAngles[YAW] );
	targetRadius = fmaxf( fabsf( sample->mins[0] ), fabsf( sample->maxs[0] ) );
	targetRadius = fmaxf( targetRadius,
		0.65f * fmaxf( fabsf( sample->mins[2] ), fabsf( sample->maxs[2] ) ) );
	targetRadius += shot->projectileRadius;
	*margin = AF_ClampFloat( RAD2DEG( atanf( targetRadius /
		fmaxf( 1.0f, Distance( muzzle, center ) ) ) ) + 1.0f, 1.25f, 6.0f );
	return qtrue;
}

static void AF_AddEnvelopeCandidate( afEnvelopeCandidate_t *candidates,
	int *candidateCount, float pitchDelta, float yawDelta, float margin )
{
	int i;
	float maxPitch = cg.refdef.fov_y * 0.60f + margin;
	float maxYaw = cg.refdef.fov_x * 0.60f + margin;

	if ( fabsf( pitchDelta ) > maxPitch || fabsf( yawDelta ) > maxYaw ) {
		return;
	}
	for ( i = 0; i < *candidateCount; i++ ) {
		if ( fabsf( candidates[i].pitchDelta - pitchDelta ) < 0.75f &&
			fabsf( candidates[i].yawDelta - yawDelta ) < 0.75f ) {
			if ( margin > candidates[i].margin ) {
				candidates[i].margin = margin;
			}
			return;
		}
	}
	if ( *candidateCount >= AF_ENVELOPE_MAX_CANDIDATES ) {
		return;
	}
	candidates[*candidateCount].pitchDelta = pitchDelta;
	candidates[*candidateCount].yawDelta = yawDelta;
	candidates[*candidateCount].margin = margin;
	( *candidateCount )++;
}

static int AF_BuildEnvelopeCandidates( const afShot_t *shot,
	afEnvelopeCandidate_t *candidates )
{
	float previousError = 0.0f;
	qboolean previousValid = qfalse;
	int candidateCount = 0;
	int i;

	for ( i = 1; i < shot->numSamples; i++ ) {
		const afTargetSample_t *sample = &shot->samples[i];
		float pitchDelta, yawDelta, requiredSpeed, margin;
		float pitchOffset = shot->weaponClass == AF_WEAPON_GOLAN_ALT ? -10.0f : 0.0f;
		float error;

		if ( !AF_EnvelopeDirectionForSample( shot, sample, pitchOffset, 0.0f,
			&pitchDelta, &yawDelta, &requiredSpeed, &margin ) ) {
			continue;
		}
		if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
			if ( requiredSpeed >= 620.0f && requiredSpeed <= 1480.0f &&
				( i % 3 == 0 || i == shot->numSamples - 1 ) ) {
				AF_AddEnvelopeCandidate( candidates, &candidateCount,
					pitchDelta, yawDelta, margin + 2.5f );
			}
			continue;
		}

		error = requiredSpeed - shot->projectileSpeed;
		if ( fabsf( error ) <= 110.0f ||
			( previousValid && ( error == 0.0f ||
				( error < 0.0f ) != ( previousError < 0.0f ) ) ) ) {
			AF_AddEnvelopeCandidate( candidates, &candidateCount,
				pitchDelta, yawDelta, margin + 1.5f );
		}
		previousError = error;
		previousValid = qtrue;
	}
	return candidateCount;
}

static float AF_EnvelopeGolanProbability( const afShot_t *shot,
	float pitchDelta, float yawDelta, float *contactTime )
{
	vec3_t angles;
	float earliest = 0.0f;
	float singleProbability;
	int hits = 0;
	int i;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );
	for ( i = 0; i < AF_ENVELOPE_GOLAN_SAMPLES; i++ ) {
		afGolanRoll_t roll;
		float thisContact = 0.0f;
		AF_MakeGolanRoll( i, &roll );
		if ( AF_TestTrajectory( shot, angles, roll.speed, roll.pitchOffset,
			roll.yawOffset, roll.lifeMsec, qfalse, &thisContact ) ) {
			hits++;
			if ( earliest <= 0.0f || thisContact < earliest ) {
				earliest = thisContact;
			}
		}
	}
	if ( hits <= 0 ) {
		return 0.0f;
	}
	if ( contactTime ) {
		*contactTime = earliest;
	}
	singleProbability = hits / (float)AF_ENVELOPE_GOLAN_SAMPLES;
	return 1.0f - ( 1.0f - singleProbability ) * ( 1.0f - singleProbability );
}

static qboolean AF_EnvelopeCellWasTested( int pitchKey, int yawKey, int testedCount )
{
	int i;
	for ( i = 0; i < testedCount; i++ ) {
		if ( af_envelopeTestedPitch[i] == pitchKey &&
			af_envelopeTestedYaw[i] == yawKey ) {
			return qtrue;
		}
	}
	return qfalse;
}

static void AF_SelectEnvelopeBestCell( void )
{
	int i, j;

	af_envelopeBestCell = -1;
	for ( i = 0; i < af_envelopeCellCount; i++ ) {
		afEnvelopeCell_t *cell = &af_envelopeCells[i];
		cell->neighbors = 0;
		for ( j = 0; j < af_envelopeCellCount; j++ ) {
			if ( abs( cell->pitchKey - af_envelopeCells[j].pitchKey ) <= 1 &&
				abs( cell->yawKey - af_envelopeCells[j].yawKey ) <= 1 ) {
				cell->neighbors++;
			}
		}
		if ( af_envelopeBestCell < 0 ) {
			af_envelopeBestCell = i;
		} else {
			afEnvelopeCell_t *best = &af_envelopeCells[af_envelopeBestCell];
			if ( cell->probability > best->probability + 0.015f ||
				( fabsf( cell->probability - best->probability ) <= 0.015f &&
					( cell->neighbors > best->neighbors ||
						( cell->neighbors == best->neighbors &&
							cell->contactTime < best->contactTime ) ) ) ) {
				af_envelopeBestCell = i;
			}
		}
	}
}

static void AF_SolveEnvelope( afShot_t *shot )
{
	afEnvelopeCandidate_t candidates[AF_ENVELOPE_MAX_CANDIDATES];
	float step = shot->weaponClass == AF_WEAPON_GOLAN_ALT ? 0.75f : 0.5f;
	float maxPitch = cg.refdef.fov_y * 0.55f + 3.0f;
	float maxYaw = cg.refdef.fov_x * 0.55f + 3.0f;
	int candidateCount;
	int testedCount = 0;
	int c;

	af_envelopeCellCount = 0;
	af_envelopeBestCell = -1;
	candidateCount = AF_BuildEnvelopeCandidates( shot, candidates );
	for ( c = 0; c < candidateCount && testedCount < AF_ENVELOPE_MAX_TESTED &&
		af_envelopeCellCount < AF_ENVELOPE_MAX_CELLS; c++ ) {
		int centerPitchKey = (int)floorf( candidates[c].pitchDelta / step + 0.5f );
		int centerYawKey = (int)floorf( candidates[c].yawDelta / step + 0.5f );
		int keyRadius = (int)ceilf( candidates[c].margin / step );
		int pitchKey, yawKey;

		for ( pitchKey = centerPitchKey - keyRadius;
			pitchKey <= centerPitchKey + keyRadius && testedCount < AF_ENVELOPE_MAX_TESTED &&
				af_envelopeCellCount < AF_ENVELOPE_MAX_CELLS;
			pitchKey++ ) {
			for ( yawKey = centerYawKey - keyRadius;
				yawKey <= centerYawKey + keyRadius && testedCount < AF_ENVELOPE_MAX_TESTED &&
					af_envelopeCellCount < AF_ENVELOPE_MAX_CELLS;
				yawKey++ ) {
				float pitchDelta = pitchKey * step;
				float yawDelta = yawKey * step;
				float probability = 0.0f;
				float contact = 0.0f;
				afEnvelopeCell_t *cell;

				if ( fabsf( pitchDelta ) > maxPitch || fabsf( yawDelta ) > maxYaw ||
					AF_EnvelopeCellWasTested( pitchKey, yawKey, testedCount ) ) {
					continue;
				}
				af_envelopeTestedPitch[testedCount] = pitchKey;
				af_envelopeTestedYaw[testedCount] = yawKey;
				testedCount++;
				if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
					probability = AF_EnvelopeGolanProbability( shot,
						pitchDelta, yawDelta, &contact );
				} else if ( AF_TestDeterministic( shot, pitchDelta, yawDelta,
					qfalse, &contact ) ) {
					probability = 1.0f;
				}
				if ( probability <= 0.0f ||
					af_envelopeCellCount >= AF_ENVELOPE_MAX_CELLS ) {
					continue;
				}
				cell = &af_envelopeCells[af_envelopeCellCount++];
				cell->pitchDelta = pitchDelta;
				cell->yawDelta = yawDelta;
				cell->probability = probability;
				cell->contactTime = contact;
				cell->pitchKey = pitchKey;
				cell->yawKey = yawKey;
				cell->neighbors = 0;
			}
		}
	}
	AF_SelectEnvelopeBestCell();
}

static float AF_LessonFixedSpeed( int stage )
{
	return stage == 2 ? 900.0f : 1050.0f;
}

static float AF_LessonFixedPitchOffset( int stage )
{
	return stage >= 4 ? -10.0f : 0.0f;
}

static qboolean AF_SetupLessonShot( afShot_t *shot, int stage, int targetNum )
{
	memset( shot, 0, sizeof( *shot ) );
	shot->active = qtrue;
	shot->shotTime = cg.time;
	shot->shooterNum = cg.snap->ps.clientNum;
	shot->targetNum = targetNum;
	shot->weapon = stage == 2 ? WP_ROCKET_LAUNCHER : WP_FLECHETTE;
	shot->weaponClass = stage == 2 ? AF_WEAPON_ROCKET : AF_WEAPON_GOLAN_ALT;
	shot->altFire = stage != 2;
	VectorCopy( cg.snap->ps.origin, shot->shooterOrigin );
	VectorCopy( cg.snap->ps.viewangles, shot->shotAngles );
	shot->shooterViewheight = cg.snap->ps.viewheight;
	shot->projectileSpeed = AF_LessonFixedSpeed( stage );
	shot->projectileRadius = 3.0f;
	shot->gravity = stage >= 4;
	shot->maxFlightMsec = 3500;
	AF_CalcMuzzle( shot, shot->shotAngles, shot->actualMuzzle );
	return qtrue;
}

static int AF_BuildLessonCandidates( const afShot_t *shot, int stage,
	afEnvelopeCandidate_t *candidates )
{
	float previousError = 0.0f;
	qboolean previousValid = qfalse;
	qboolean randomSpeed = stage >= 5;
	float pitchOffset = randomSpeed ? -10.0f : AF_LessonFixedPitchOffset( stage );
	float fixedSpeed = AF_LessonFixedSpeed( stage );
	int candidateCount = 0;
	int i;

	for ( i = 1; i < shot->numSamples; i++ ) {
		float pitchDelta, yawDelta, requiredSpeed, margin;
		float error;

		if ( !AF_EnvelopeDirectionForSample( shot, &shot->samples[i],
			pitchOffset, 0.0f, &pitchDelta, &yawDelta, &requiredSpeed, &margin ) ) {
			continue;
		}
		if ( randomSpeed ) {
			if ( requiredSpeed >= 620.0f && requiredSpeed <= 1480.0f &&
				( i % 2 == 0 || i == shot->numSamples - 1 ) ) {
				AF_AddEnvelopeCandidate( candidates, &candidateCount,
					pitchDelta, yawDelta, margin + 3.0f );
			}
			continue;
		}

		error = requiredSpeed - fixedSpeed;
		if ( fabsf( error ) <= 100.0f ||
			( previousValid && ( error == 0.0f ||
				( error < 0.0f ) != ( previousError < 0.0f ) ) ) ) {
			AF_AddEnvelopeCandidate( candidates, &candidateCount,
				pitchDelta, yawDelta, margin + 1.5f );
		}
		previousError = error;
		previousValid = qtrue;
	}
	return candidateCount;
}

static float AF_LessonCellProbability( const afShot_t *shot, int stage,
	float pitchDelta, float yawDelta, float *contactTime )
{
	vec3_t angles;
	float earliest = 0.0f;
	int hits = 0;
	int count;
	int i;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );
	if ( stage < 5 ) {
		float contact = 0.0f;
		if ( !AF_TestTrajectory( shot, angles, AF_LessonFixedSpeed( stage ),
			AF_LessonFixedPitchOffset( stage ), 0.0f, 3500, qfalse, &contact ) ) {
			return 0.0f;
		}
		if ( contactTime ) {
			*contactTime = contact;
		}
		return 1.0f;
	}

	count = AF_LESSON_RANDOM_SAMPLES;
	for ( i = 0; i < count; i++ ) {
		afGolanRoll_t roll;
		float contact = 0.0f;
		AF_MakeGolanRoll( i, &roll );
		if ( AF_TestTrajectory( shot, angles, roll.speed, roll.pitchOffset,
			roll.yawOffset, roll.lifeMsec, qfalse, &contact ) ) {
			hits++;
			if ( earliest <= 0.0f || contact < earliest ) {
				earliest = contact;
			}
		}
	}
	if ( hits <= 0 ) {
		return 0.0f;
	}
	if ( contactTime ) {
		*contactTime = earliest;
	}
	if ( stage >= 6 ) {
		float single = hits / (float)count;
		return 1.0f - ( 1.0f - single ) * ( 1.0f - single );
	}
	return hits / (float)count;
}

static void AF_SolveLessonStage( afShot_t *shot, int stage )
{
	afEnvelopeCandidate_t candidates[AF_ENVELOPE_MAX_CANDIDATES];
	float step = 0.5f;
	float maxPitch = cg.refdef.fov_y * 0.55f + 3.0f;
	float maxYaw = cg.refdef.fov_x * 0.55f + 3.0f;
	int candidateCount;
	int testedCount = 0;
	int c;

	af_envelopeCellCount = 0;
	af_envelopeBestCell = -1;
	candidateCount = AF_BuildLessonCandidates( shot, stage, candidates );
	for ( c = 0; c < candidateCount && testedCount < AF_ENVELOPE_MAX_TESTED &&
		af_envelopeCellCount < AF_ENVELOPE_MAX_CELLS; c++ ) {
		int centerPitchKey = (int)floorf( candidates[c].pitchDelta / step + 0.5f );
		int centerYawKey = (int)floorf( candidates[c].yawDelta / step + 0.5f );
		int keyRadius = (int)ceilf( candidates[c].margin / step );
		int pitchKey, yawKey;

		for ( pitchKey = centerPitchKey - keyRadius;
			pitchKey <= centerPitchKey + keyRadius && testedCount < AF_ENVELOPE_MAX_TESTED &&
				af_envelopeCellCount < AF_ENVELOPE_MAX_CELLS;
			pitchKey++ ) {
			for ( yawKey = centerYawKey - keyRadius;
				yawKey <= centerYawKey + keyRadius && testedCount < AF_ENVELOPE_MAX_TESTED &&
					af_envelopeCellCount < AF_ENVELOPE_MAX_CELLS;
				yawKey++ ) {
				float pitchDelta = pitchKey * step;
				float yawDelta = yawKey * step;
				float contact = 0.0f;
				float probability;
				afEnvelopeCell_t *cell;

				if ( fabsf( pitchDelta ) > maxPitch || fabsf( yawDelta ) > maxYaw ||
					AF_EnvelopeCellWasTested( pitchKey, yawKey, testedCount ) ) {
					continue;
				}
				af_envelopeTestedPitch[testedCount] = pitchKey;
				af_envelopeTestedYaw[testedCount] = yawKey;
				testedCount++;
				probability = AF_LessonCellProbability( shot, stage,
					pitchDelta, yawDelta, &contact );
				if ( probability <= 0.0f ||
					af_envelopeCellCount >= AF_ENVELOPE_MAX_CELLS ) {
					continue;
				}
				cell = &af_envelopeCells[af_envelopeCellCount++];
				cell->pitchDelta = pitchDelta;
				cell->yawDelta = yawDelta;
				cell->probability = probability;
				cell->contactTime = contact;
				cell->pitchKey = pitchKey;
				cell->yawKey = yawKey;
				cell->neighbors = 0;
			}
		}
	}
	AF_SelectEnvelopeBestCell();
	if ( stage == 4 && af_envelopeBestCell >= 0 ) {
		const afEnvelopeCell_t *best = &af_envelopeCells[af_envelopeBestCell];
		af_lessonNominalAimFound = qtrue;
		af_lessonNominalPitchDelta = best->pitchDelta;
		af_lessonNominalYawDelta = best->yawDelta;
		af_lessonNominalContactTime = best->contactTime;
	}
}

static afCorrection_t AF_SearchDeterministic( const afShot_t *shot, qboolean headOnly )
{
	afCorrection_t best;
	float bestMagnitude = 99999.0f;
	float pitch, yaw;

	memset( &best, 0, sizeof( best ) );
	for ( pitch = -AF_SEARCH_RANGE; pitch <= AF_SEARCH_RANGE + 0.01f; pitch += 1.0f ) {
		for ( yaw = -AF_SEARCH_RANGE; yaw <= AF_SEARCH_RANGE + 0.01f; yaw += 1.0f ) {
			float magnitude = sqrtf( pitch * pitch + yaw * yaw );
			float contact = 0.0f;
			if ( magnitude >= bestMagnitude ) {
				continue;
			}
			if ( AF_TestDeterministic( shot, pitch, yaw, headOnly, &contact ) ) {
				best.found = qtrue;
				best.pitchDelta = pitch;
				best.yawDelta = yaw;
				best.contactTime = contact;
				bestMagnitude = magnitude;
			}
		}
	}

	if ( best.found ) {
		float centerPitch = best.pitchDelta;
		float centerYaw = best.yawDelta;
		for ( pitch = centerPitch - 1.0f; pitch <= centerPitch + 1.01f; pitch += 0.2f ) {
			for ( yaw = centerYaw - 1.0f; yaw <= centerYaw + 1.01f; yaw += 0.2f ) {
				float magnitude = sqrtf( pitch * pitch + yaw * yaw );
				float contact = 0.0f;
				if ( magnitude >= bestMagnitude ) {
					continue;
				}
				if ( AF_TestDeterministic( shot, pitch, yaw, headOnly, &contact ) ) {
					best.pitchDelta = pitch;
					best.yawDelta = yaw;
					best.contactTime = contact;
					bestMagnitude = magnitude;
				}
			}
		}
	}

	return best;
}

static afCorrection_t AF_SearchRealizedGolan( const afShot_t *shot )
{
	afCorrection_t best;
	float bestMagnitude = 99999.0f;
	float pitch, yaw;

	memset( &best, 0, sizeof( best ) );
	if ( shot->numProjectiles <= 0 ) {
		return best;
	}

	for ( pitch = -AF_SEARCH_RANGE; pitch <= AF_SEARCH_RANGE + 0.01f; pitch += 1.0f ) {
		for ( yaw = -AF_SEARCH_RANGE; yaw <= AF_SEARCH_RANGE + 0.01f; yaw += 1.0f ) {
			float magnitude = sqrtf( pitch * pitch + yaw * yaw );
			float contact = 0.0f;
			if ( magnitude >= bestMagnitude ) {
				continue;
			}
			if ( AF_TestRealizedGolan( shot, pitch, yaw, &contact ) ) {
				best.found = qtrue;
				best.pitchDelta = pitch;
				best.yawDelta = yaw;
				best.contactTime = contact;
				bestMagnitude = magnitude;
			}
		}
	}

	return best;
}

static void AF_BuildDeterministicHeat( afShot_t *shot )
{
	int row, column;
	float step = ( AF_HEAT_RANGE * 2.0f ) / ( AF_HEAT_DIM - 1 );

	for ( row = 0; row < AF_HEAT_DIM; row++ ) {
		for ( column = 0; column < AF_HEAT_DIM; column++ ) {
			float pitch = -AF_HEAT_RANGE + row * step;
			float yaw = -AF_HEAT_RANGE + column * step;
			shot->result.bodyHeat[row][column] = AF_TestDeterministic( shot, pitch, yaw, qfalse, NULL );
			shot->result.headHeat[row][column] = AF_TestDeterministic( shot, pitch, yaw, qtrue, NULL );
		}
	}
}

static void AF_AnalyzeGolan( afShot_t *shot )
{
	int row, column;
	float step = ( AF_HEAT_RANGE * 2.0f ) / ( AF_HEAT_DIM - 1 );
	float bestProbability = -1.0f;
	float centerPitch, centerYaw;
	float pitch, yaw;

	for ( row = 0; row < AF_HEAT_DIM; row++ ) {
		for ( column = 0; column < AF_HEAT_DIM; column++ ) {
			float candidatePitch = -AF_HEAT_RANGE + row * step;
			float candidateYaw = -AF_HEAT_RANGE + column * step;
			float probability = AF_GolanProbability( shot, candidatePitch, candidateYaw );
			shot->result.heat[row][column] = probability;
			shot->result.guaranteedHeat[row][column] =
				probability >= 0.999f && AF_GolanSampledGuaranteed( shot, candidatePitch, candidateYaw );
			if ( probability > bestProbability ) {
				bestProbability = probability;
				shot->result.bestPitchDelta = candidatePitch;
				shot->result.bestYawDelta = candidateYaw;
			}
		}
	}

	centerPitch = shot->result.bestPitchDelta;
	centerYaw = shot->result.bestYawDelta;
	for ( pitch = centerPitch - step; pitch <= centerPitch + step + 0.01f; pitch += 0.5f ) {
		for ( yaw = centerYaw - step; yaw <= centerYaw + step + 0.01f; yaw += 0.5f ) {
			float probability = AF_GolanProbability( shot, pitch, yaw );
			if ( probability > bestProbability ) {
				bestProbability = probability;
				shot->result.bestPitchDelta = pitch;
				shot->result.bestYawDelta = yaw;
			}
		}
	}

	shot->result.actualProbability = AF_GolanProbability( shot, 0.0f, 0.0f );
	shot->result.bestProbability = bestProbability;
	shot->result.actualSampledGuaranteed =
		shot->result.actualProbability >= 0.999f && AF_GolanSampledGuaranteed( shot, 0.0f, 0.0f );
	shot->result.bestSampledGuaranteed =
		bestProbability >= 0.999f && AF_GolanSampledGuaranteed( shot,
			shot->result.bestPitchDelta, shot->result.bestYawDelta );
	shot->result.realized = AF_SearchRealizedGolan( shot );
}

static qboolean AF_FindGolanProof( const afShot_t *shot, float pitchDelta,
	float yawDelta, afGolanRoll_t *proofRoll, float *proofContact )
{
	vec3_t angles;
	float bestContact = 99999.0f;
	qboolean found = qfalse;
	int i;

	VectorCopy( shot->shotAngles, angles );
	angles[PITCH] = AngleNormalize180( angles[PITCH] + pitchDelta );
	angles[YAW] = AngleNormalize180( angles[YAW] + yawDelta );
	for ( i = 0; i < AF_GOLAN_MONTE_SAMPLES; i++ ) {
		afGolanRoll_t roll;
		float contact = 0.0f;
		AF_MakeGolanRoll( i, &roll );
		if ( AF_TestTrajectory( shot, angles, roll.speed, roll.pitchOffset,
			roll.yawOffset, roll.lifeMsec, qfalse, &contact ) && contact < bestContact ) {
			bestContact = contact;
			*proofRoll = roll;
			found = qtrue;
		}
	}
	if ( found ) {
		*proofContact = bestContact;
	}
	return found;
}

static void AF_SetReviewProof( const afShot_t *shot, afReviewEntry_t *entry,
	float pitchDelta, float yawDelta, float speed, float pitchOffset,
	float yawOffset, float contactTime )
{
	vec3_t viewAngles;
	vec3_t launchAngles;
	vec3_t forward;

	VectorCopy( shot->shotAngles, viewAngles );
	viewAngles[PITCH] = AngleNormalize180( viewAngles[PITCH] + pitchDelta );
	viewAngles[YAW] = AngleNormalize180( viewAngles[YAW] + yawDelta );
	VectorCopy( viewAngles, launchAngles );
	launchAngles[PITCH] = AngleNormalize180( launchAngles[PITCH] + pitchOffset );
	launchAngles[YAW] = AngleNormalize180( launchAngles[YAW] + yawOffset );
	AngleVectors( launchAngles, forward, NULL, NULL );

	entry->proofValid = qtrue;
	AF_CalcMuzzle( shot, viewAngles, entry->proofMuzzle );
	VectorScale( forward, speed, entry->proofVelocity );
	entry->proofVelocity[2] += shot->verticalBoost;
	entry->proofGravity = shot->gravity;
	entry->proofRadius = shot->projectileRadius;
	entry->proofContactTime = contactTime;
	AF_EvaluateProjectile( entry->proofMuzzle, entry->proofVelocity,
		entry->proofGravity, contactTime, entry->proofContactPoint );
}

static qboolean AF_BuildReviewEntry( const afShot_t *shot, afReviewEntry_t *entry )
{
	afGolanRoll_t proofRoll;
	float proofContact = 0.0f;
	float correctionMagnitude;
	float improvement;

	memset( entry, 0, sizeof( *entry ) );
	entry->shotTime = shot->shotTime;
	entry->weaponClass = shot->weaponClass;
	entry->targetNum = shot->targetNum;

	if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
		improvement = shot->result.bestProbability - shot->result.actualProbability;
		if ( improvement < 0.12f || shot->result.bestProbability < 0.25f ) {
			return qfalse;
		}
		entry->score = improvement * 100.0f;
		if ( shot->result.bestSampledGuaranteed ) {
			entry->score += 25.0f;
		}
	} else {
		if ( shot->result.actualHit || !shot->result.body.found ) {
			return qfalse;
		}
		correctionMagnitude = sqrtf( shot->result.body.pitchDelta * shot->result.body.pitchDelta +
			shot->result.body.yawDelta * shot->result.body.yawDelta );
		if ( correctionMagnitude < 0.20f ) {
			return qfalse;
		}
		entry->score = AF_ClampFloat( correctionMagnitude * 8.0f, 0.0f, 100.0f );
	}

	entry->bodyFound = shot->result.body.found;
	entry->bodyPitchDelta = shot->result.body.pitchDelta;
	entry->bodyYawDelta = shot->result.body.yawDelta;
	entry->bodyContactTime = shot->result.body.contactTime;
	entry->headFound = shot->result.head.found;
	entry->headPitchDelta = shot->result.head.pitchDelta;
	entry->headYawDelta = shot->result.head.yawDelta;
	entry->headContactTime = shot->result.head.contactTime;
	entry->actualProbability = shot->result.actualProbability;
	entry->bestProbability = shot->result.bestProbability;
	entry->bestPitchDelta = shot->result.bestPitchDelta;
	entry->bestYawDelta = shot->result.bestYawDelta;
	entry->bestSampledGuaranteed = shot->result.bestSampledGuaranteed;
	entry->realizedFound = shot->result.realized.found;
	entry->realizedPitchDelta = shot->result.realized.pitchDelta;
	entry->realizedYawDelta = shot->result.realized.yawDelta;
	entry->realizedContactTime = shot->result.realized.contactTime;

	if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
		if ( AF_FindGolanProof( shot, shot->result.bestPitchDelta,
			shot->result.bestYawDelta, &proofRoll, &proofContact ) ) {
			AF_SetReviewProof( shot, entry, shot->result.bestPitchDelta,
				shot->result.bestYawDelta, proofRoll.speed, proofRoll.pitchOffset,
				proofRoll.yawOffset, proofContact );
		}
	} else if ( shot->result.body.found ) {
		AF_SetReviewProof( shot, entry, shot->result.body.pitchDelta,
			shot->result.body.yawDelta, shot->projectileSpeed, 0.0f, 0.0f,
			shot->result.body.contactTime );
	}
	return qtrue;
}

static void AF_CacheReviewShot( afShot_t *shot )
{
	afReviewEntry_t entry;
	fileHandle_t file;

	if ( shot->cached || cg_aimFeedbackReview.integer != 1 ) {
		return;
	}
	shot->cached = qtrue;
	if ( !AF_BuildReviewEntry( shot, &entry ) ) {
		return;
	}

	trap->FS_Open( AF_REVIEW_CACHE_PATH, &file, FS_APPEND_SYNC );
	if ( !file ) {
		trap->Print( "[aimfeedback] could not append to review cache\n" );
		return;
	}
	trap->FS_Write( &entry, sizeof( entry ), file );
	trap->FS_Close( file );
}

static qboolean AF_BeginCacheFile( const char *path )
{
	afReviewHeader_t header;
	fileHandle_t file;

	header.magic = AF_REVIEW_CACHE_MAGIC;
	header.version = AF_REVIEW_CACHE_VERSION;
	header.entrySize = sizeof( afReviewEntry_t );
	trap->FS_Open( path, &file, FS_WRITE );
	if ( !file ) {
		trap->Print( "[aimfeedback] could not create %s\n", path );
		return qfalse;
	}
	trap->FS_Write( &header, sizeof( header ), file );
	trap->FS_Close( file );
	return qtrue;
}

static void AF_BeginReviewCache( void )
{
	if ( !AF_BeginCacheFile( AF_REVIEW_CACHE_PATH ) ) {
		return;
	}
	trap->Print( "[aimfeedback] preprocessing pass started\n" );
}

static qboolean AF_ReviewEntriesConflict( const afReviewEntry_t *a,
	const afReviewEntry_t *b )
{
	const afReviewEntry_t *earlier = a->shotTime <= b->shotTime ? a : b;
	int separation = abs( a->shotTime - b->shotTime );
	int proofWindow = AF_REVIEW_SPACING_MSEC;

	if ( earlier->proofValid ) {
		int required = AF_REVIEW_PREROLL_MSEC +
			(int)( earlier->proofContactTime * 1000.0f ) +
			AF_REVIEW_PROOF_VERIFY_MSEC + AF_REVIEW_PROOF_TAIL_MSEC;
		if ( required > proofWindow ) {
			proofWindow = required;
		}
	}
	return separation < proofWindow;
}

static int AF_ReviewPostrollMsec( const afReviewEntry_t *entry )
{
	int postroll = AF_REVIEW_POSTROLL_MSEC;

	if ( entry->proofValid ) {
		int proofEnd = (int)( entry->proofContactTime * 1000.0f ) +
			AF_REVIEW_PROOF_VERIFY_MSEC + AF_REVIEW_PROOF_TAIL_MSEC;
		if ( proofEnd > postroll ) {
			postroll = proofEnd;
		}
	}
	return postroll;
}

static void AF_SelectReviewEntries( int rawCount )
{
	int groupedCount = 0;
	int weaponCounts[AF_WEAPON_GOLAN_ALT + 1];
	int i, j;

	memset( weaponCounts, 0, sizeof( weaponCounts ) );

	/* Collapse rapid fire into one review beat before ranking the mistakes. */
	for ( i = 0; i < rawCount; i++ ) {
		if ( groupedCount > 0 &&
			af_reviewRaw[i].weaponClass == af_reviewRaw[groupedCount - 1].weaponClass &&
			af_reviewRaw[i].targetNum == af_reviewRaw[groupedCount - 1].targetNum &&
			af_reviewRaw[i].shotTime - af_reviewRaw[groupedCount - 1].shotTime <= AF_REVIEW_BURST_MSEC ) {
			if ( af_reviewRaw[i].score > af_reviewRaw[groupedCount - 1].score ) {
				af_reviewRaw[groupedCount - 1] = af_reviewRaw[i];
			}
			continue;
		}
		af_reviewRaw[groupedCount++] = af_reviewRaw[i];
	}

	/* Rank by usefulness, then reject near-duplicates in time. */
	for ( i = 1; i < groupedCount; i++ ) {
		afReviewEntry_t value = af_reviewRaw[i];
		j = i - 1;
		while ( j >= 0 && af_reviewRaw[j].score < value.score ) {
			af_reviewRaw[j + 1] = af_reviewRaw[j];
			j--;
		}
		af_reviewRaw[j + 1] = value;
	}

	af_reviewCount = 0;
	/* Reserve a few beats for each weapon so Golan's probability score cannot
	   crowd every deterministic weapon out of a mixed-weapon match. */
	for ( i = 0; i < groupedCount && af_reviewCount < AF_REVIEW_MAX; i++ ) {
		qboolean conflict = qfalse;
		int weaponClass = af_reviewRaw[i].weaponClass;
		if ( weaponClass <= AF_WEAPON_NONE || weaponClass > AF_WEAPON_GOLAN_ALT ||
			weaponCounts[weaponClass] >= 3 ) {
			continue;
		}
		for ( j = 0; j < af_reviewCount; j++ ) {
			if ( AF_ReviewEntriesConflict( &af_reviewRaw[i], &af_reviewEntries[j] ) ) {
				conflict = qtrue;
				break;
			}
		}
		if ( !conflict ) {
			af_reviewEntries[af_reviewCount++] = af_reviewRaw[i];
			weaponCounts[weaponClass]++;
		}
	}

	for ( i = 0; i < groupedCount && af_reviewCount < AF_REVIEW_MAX; i++ ) {
		qboolean conflict = qfalse;
		for ( j = 0; j < af_reviewCount; j++ ) {
			if ( AF_ReviewEntriesConflict( &af_reviewRaw[i], &af_reviewEntries[j] ) ) {
				conflict = qtrue;
				break;
			}
		}
		if ( !conflict ) {
			af_reviewEntries[af_reviewCount++] = af_reviewRaw[i];
		}
	}

	/* Playback is chronological even though selection was score-ranked. */
	for ( i = 1; i < af_reviewCount; i++ ) {
		afReviewEntry_t value = af_reviewEntries[i];
		j = i - 1;
		while ( j >= 0 && af_reviewEntries[j].shotTime > value.shotTime ) {
			af_reviewEntries[j + 1] = af_reviewEntries[j];
			j--;
		}
		af_reviewEntries[j + 1] = value;
	}
}

static void AF_SortRawChronologically( int rawCount )
{
	int i, j;

	for ( i = 1; i < rawCount; i++ ) {
		afReviewEntry_t value = af_reviewRaw[i];
		j = i - 1;
		while ( j >= 0 && af_reviewRaw[j].shotTime > value.shotTime ) {
			af_reviewRaw[j + 1] = af_reviewRaw[j];
			j--;
		}
		af_reviewRaw[j + 1] = value;
	}
}

static int AF_ReadCacheEntries( const char *path )
{
	afReviewHeader_t header;
	fileHandle_t file;
	int fileLength;
	int rawCount;
	int expectedBytes;

	fileLength = trap->FS_Open( path, &file, FS_READ );
	if ( !file || fileLength < (int)sizeof( header ) ) {
		if ( file ) {
			trap->FS_Close( file );
		}
		trap->Print( "[aimfeedback] cache not found: %s\n", path );
		return 0;
	}
	trap->FS_Read( &header, sizeof( header ), file );
	if ( header.magic != AF_REVIEW_CACHE_MAGIC ||
		header.version != AF_REVIEW_CACHE_VERSION ||
		header.entrySize != sizeof( afReviewEntry_t ) ) {
		trap->FS_Close( file );
		trap->Print( "[aimfeedback] incompatible cache: %s\n", path );
		return 0;
	}

	rawCount = ( fileLength - sizeof( header ) ) / sizeof( afReviewEntry_t );
	if ( rawCount > AF_REVIEW_RAW_MAX ) {
		rawCount = AF_REVIEW_RAW_MAX;
	}
	expectedBytes = rawCount * sizeof( afReviewEntry_t );
	if ( expectedBytes > 0 ) {
		trap->FS_Read( af_reviewRaw, expectedBytes, file );
	}
	trap->FS_Close( file );
	return rawCount;
}

static void AF_LoadReviewCache( const char *path, const char *description )
{
	int rawCount;

	af_reviewCount = 0;
	rawCount = AF_ReadCacheEntries( path );
	if ( rawCount <= 0 ) {
		return;
	}
	AF_SortRawChronologically( rawCount );
	AF_SelectReviewEntries( rawCount );
	trap->Print( "[aimfeedback] selected %i review shots from %i %s candidates\n",
		af_reviewCount, rawCount, description );
}

static void AF_AppendCacheEntry( const char *path, const afReviewEntry_t *entry )
{
	fileHandle_t file;

	trap->FS_Open( path, &file, FS_APPEND_SYNC );
	if ( !file ) {
		trap->Print( "[aimfeedback] could not append cache entry to %s\n", path );
		return;
	}
	trap->FS_Write( entry, sizeof( *entry ), file );
	trap->FS_Close( file );
}

static void AF_BeginValidationPass( void )
{
	af_validationRawCount = AF_ReadCacheEntries( AF_REVIEW_CACHE_PATH );
	AF_SortRawChronologically( af_validationRawCount );
	af_validationIndex = 0;
	af_validationCompleteCount = 0;
	af_validationPassedCount = 0;
	af_validationFinished = qfalse;
	memset( &af_validationCurrent, 0, sizeof( af_validationCurrent ) );
	if ( !AF_BeginCacheFile( AF_REVIEW_VALIDATED_PATH ) ) {
		af_validationRawCount = 0;
		return;
	}
	trap->Print( "[aimfeedback] live validation started for %i candidates\n",
		af_validationRawCount );
	AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
}

static void AF_UpdateValidationPass( void )
{
	const afReviewEntry_t *entry;
	centity_t *target;
	vec3_t position, relative;
	vec3_t mins, maxs;
	float elapsed;
	float fraction = 0.0f;
	qboolean overlap;

	if ( af_validationFinished || af_validationRawCount <= 0 ) {
		return;
	}

	/* Skip candidates whose entire proof window elapsed while validating an
	   earlier overlapping shot.  Approximating them at high speed is exactly
	   what this pass exists to avoid. */
	while ( af_validationIndex < af_validationRawCount ) {
		entry = &af_reviewRaw[af_validationIndex];
		if ( cg.time <= entry->shotTime +
			(int)( entry->proofContactTime * 1000.0f ) + AF_REVIEW_PROOF_VERIFY_MSEC ) {
			break;
		}
		af_validationIndex++;
		af_validationCompleteCount++;
		memset( &af_validationCurrent, 0, sizeof( af_validationCurrent ) );
	}
	if ( af_validationIndex >= af_validationRawCount ) {
		af_validationFinished = qtrue;
		AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
		trap->Print( "[aimfeedback] live validation complete: %i passed, %i examined/skipped\n",
			af_validationPassedCount, af_validationCompleteCount );
		return;
	}

	entry = &af_reviewRaw[af_validationIndex];
	if ( !af_validationCurrent.started ) {
		if ( cg.time < entry->shotTime - AF_REVIEW_VALIDATE_PREROLL_MSEC ) {
			return;
		}
		af_validationCurrent.started = qtrue;
		af_validationCurrent.previousValid = qfalse;
		AF_SetPlaybackScale( AF_REVIEW_PROOF_SCALE );
	}
	if ( cg.time < entry->shotTime ) {
		return;
	}

	elapsed = ( cg.time - entry->shotTime ) * 0.001f;
	if ( !entry->proofValid || entry->proofContactTime <= 0.0f ||
		elapsed > entry->proofContactTime + AF_REVIEW_PROOF_VERIFY_MSEC * 0.001f ) {
		af_validationIndex++;
		af_validationCompleteCount++;
		memset( &af_validationCurrent, 0, sizeof( af_validationCurrent ) );
		AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
		return;
	}

	target = &cg_entities[entry->targetNum];
	if ( !target->currentValid || target->currentState.eType != ET_PLAYER ) {
		return;
	}
	AF_EvaluateProjectile( entry->proofMuzzle, entry->proofVelocity,
		entry->proofGravity, elapsed, position );
	VectorSubtract( position, target->lerpOrigin, relative );
	AF_DecodeBounds( &target->currentState, mins, maxs );
	mins[0] -= entry->proofRadius;
	mins[1] -= entry->proofRadius;
	mins[2] -= entry->proofRadius;
	maxs[0] += entry->proofRadius;
	maxs[1] += entry->proofRadius;
	maxs[2] += entry->proofRadius;
	overlap = AF_SegmentAABB( af_validationCurrent.previousValid ?
		af_validationCurrent.previousRelative : relative, relative, mins, maxs, &fraction );
	if ( overlap && ( !af_validationCurrent.previousValid ||
		Distance( af_validationCurrent.previousTargetOrigin, target->lerpOrigin ) <= AF_TELEPORT_DIST ) ) {
		AF_AppendCacheEntry( AF_REVIEW_VALIDATED_PATH, entry );
		af_validationPassedCount++;
		af_validationCompleteCount++;
		af_validationIndex++;
		memset( &af_validationCurrent, 0, sizeof( af_validationCurrent ) );
		AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
	} else {
		af_validationCurrent.previousValid = qtrue;
		VectorCopy( relative, af_validationCurrent.previousRelative );
		VectorCopy( target->lerpOrigin, af_validationCurrent.previousTargetOrigin );
	}
}

static void AF_FinalizeShot( afShot_t *shot )
{
	if ( shot->result.ready || shot->numSamples < 2 ) {
		return;
	}
	if ( shot->weaponClass == AF_WEAPON_THERMAL_ALT && shot->projectileSpeed <= 0.0f ) {
		shot->result.ready = qtrue;
		shot->result.readyTime = cg.time;
		return;
	}

	if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
		AF_AnalyzeGolan( shot );
	} else {
		shot->result.actualHit = AF_TestDeterministic( shot, 0.0f, 0.0f, qfalse, NULL );
		shot->result.body = AF_SearchDeterministic( shot, qfalse );
		shot->result.head = AF_SearchDeterministic( shot, qtrue );
		AF_BuildDeterministicHeat( shot );
	}
	shot->result.ready = qtrue;
	shot->result.readyTime = cg.time;
	AF_CacheReviewShot( shot );

	if ( cg_aimFeedback.integer > 1 ) {
		int maxSampleGap = 0;
		int sampleIndex;
		for ( sampleIndex = 1; sampleIndex < shot->numSamples; sampleIndex++ ) {
			int gap = shot->samples[sampleIndex].time - shot->samples[sampleIndex - 1].time;
			if ( gap > maxSampleGap ) {
				maxSampleGap = gap;
			}
		}
		trap->Print( "[aimfeedback] path: %i samples over %ims, largest gap %ims\n",
			shot->numSamples,
			shot->numSamples > 0 ? shot->samples[shot->numSamples - 1].time - shot->shotTime : 0,
			maxSampleGap );
		trap->Print( "[aimfeedback] projectiles: %i, speed %.0f, radius %.1f%s\n",
			shot->numProjectiles, shot->projectileSpeed, shot->projectileRadius,
			shot->numProjectiles > 0 ? va( ", recorded offset pitch %.1f yaw %.1f",
				shot->projectiles[0].pitchOffset, shot->projectiles[0].yawOffset ) : "" );
		if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
			trap->Print( "[aimfeedback] analyzed %s at %s: actual %.0f%%, best %.0f%%, recorded rolls %s\n",
				AF_WeaponName( shot->weaponClass ), cgs.clientinfo[shot->targetNum].name,
				shot->result.actualProbability * 100.0f, shot->result.bestProbability * 100.0f,
				shot->result.realized.found ? "solved" : "unavailable" );
		} else {
			trap->Print( "[aimfeedback] analyzed %s at %s: actual %s, body %s, head %s\n",
				AF_WeaponName( shot->weaponClass ), cgs.clientinfo[shot->targetNum].name,
				shot->result.actualHit ? "hit" : "miss",
				shot->result.body.found ? "solved" : "unavailable",
				shot->result.head.found ? "solved" : "unavailable" );
		}
	}
}

static qboolean af_labStarted;	/* AF_StartLab ran for this demo */

void CG_InitAimFeedback( void )
{
	memset( af_shots, 0, sizeof( af_shots ) );
	af_nextShot = 0;
	af_latestShot = -1;
	af_lastTime = 0;
	af_reviewCount = 0;
	af_reviewIndex = 0;
	af_reviewHoldStart = 0;
	af_reviewState = AF_REVIEW_IDLE;
	af_reviewInitialized = qfalse;
	af_reviewProofLogged = qfalse;
	af_reviewGhostShader = 0;
	af_reviewLineShader = 0;
	af_reviewSphereModel = 0;
	af_reviewRocketModel = 0;
	af_reviewThermalModel = 0;
	af_reviewGolanModel = 0;
	af_reviewProofTrackedIndex = -1;
	af_reviewProofImpact = qfalse;
	af_reviewProofImpactTime = 0.0f;
	af_reviewProofPreviousValid = qfalse;
	af_validationRawCount = 0;
	af_validationCompleteCount = 0;
	af_validationPassedCount = 0;
	af_validationFinished = qfalse;
	af_fcTrackCount = 0;
	af_fcTrackCursor = 0;
	af_fcTrackLastWriteTime = -1;
	af_fcTrackWriteCount = 0;
	if ( af_fcTrackFile ) {	/* a re-init when the demo's time went back */
		trap->FS_Close( af_fcTrackFile );
	}
	af_fcTrackFile = 0;
	af_envelopeCellCount = 0;
	af_envelopeBestCell = -1;
	af_envelopeTargetNum = -1;
	af_envelopeLastSolveTime = 0;
	af_envelopeLastWeapon = WP_NONE;
	af_envelopeScaleState = -1;
	af_envelopeCaptureMask = 0;
	af_envelopeCaptureWeapon = AF_WEAPON_NONE;
	af_envelopeCaptureScreenshotTime = 0;
	af_envelopeCaptureUnfreezeTime = 0;
	af_lessonFrozen = qfalse;
	af_lessonStage = 0;
	af_lessonStageStart = 0;
	af_lessonCaptured = qfalse;
	af_lessonFinished = qfalse;
	af_lessonTrackIndex = -1;
	VectorClear( af_lessonTargetVelocity );
	af_lessonDistance = 0.0f;
	af_lessonTargetSpeed = 0.0f;
	af_lessonLateralSpeed = 0.0f;
	af_lessonAwaySpeed = 0.0f;
	af_lessonNominalAimFound = qfalse;
	af_lessonNominalPitchDelta = 0.0f;
	af_lessonNominalYawDelta = 0.0f;
	af_lessonNominalContactTime = 0.0f;
	af_candidateIndex = 0;
	af_labStarted = qfalse;		/* the modes start at the first demo frame (AF_StartLab) */
}

/* The lab's modes, from the first CG_AimFeedback_Update past its demo gate: at CG_Init the cgame does not know
   it plays a demo yet, so starting there created or truncated the caches and wrote timescale on live servers
   too. */
static void AF_StartLab( void )
{
	af_labStarted = qtrue;
	AF_SetDemoFrozen( qfalse );

	if ( cg_aimFeedbackLesson.integer ) {
		if ( AF_LoadFCTrackCache() ) {
			AF_SetPlaybackScale( AF_LESSON_FAST_SCALE );
		}
	} else if ( cg_aimFeedbackEnvelope.integer == 1 ) {
		AF_SetPlaybackScale( AF_ENVELOPE_NORMAL_SCALE );
		AF_BeginFCTrackCache();
	} else if ( cg_aimFeedbackEnvelope.integer == 2 ) {
		if ( AF_LoadFCTrackCache() ) {
			AF_SetPlaybackScale( AF_ENVELOPE_FAST_SCALE );
			af_envelopeScaleState = 1;
		}
	} else if ( cg_aimFeedbackReview.integer == 1 ) {
		AF_SetPlaybackScale( "1" );
		AF_BeginReviewCache();
	} else if ( cg_aimFeedbackReview.integer == 2 ) {
		AF_LoadReviewCache( AF_REVIEW_PROVEN_PATH, "director-proven" );
		if ( af_reviewCount > 0 ) {
			af_reviewState = AF_REVIEW_TRANSITION;
			af_reviewInitialized = qtrue;
			AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
		}
	} else if ( cg_aimFeedbackReview.integer == 3 ) {
		AF_BeginValidationPass();
	} else if ( cg_aimFeedbackReview.integer == 4 ) {
		AF_LoadReviewCache( AF_REVIEW_VALIDATED_PATH, "prefiltered" );
		if ( af_reviewCount > 0 && AF_BeginCacheFile( AF_REVIEW_PROVEN_PATH ) ) {
			af_reviewState = AF_REVIEW_TRANSITION;
			af_reviewInitialized = qtrue;
			AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
			trap->Print( "[aimfeedback] exact director validation started\n" );
		}
	}
}

void CG_ShutdownAimFeedback( void )
{
	if ( af_fcTrackFile ) {
		trap->FS_Close( af_fcTrackFile );
		af_fcTrackFile = 0;
		trap->Print( "[aimfeedback] wrote %i enemy-FC timeline samples\n",
			af_fcTrackWriteCount );
	}
	if ( af_drovePlayback ) {
		AF_SetDemoFrozen( qfalse );
		AF_SetPlaybackScale( "1" );
		af_drovePlayback = qfalse;
	}
	if ( cg.demoPlayback && af_labStarted && ( cg_aimFeedbackReview.integer != 0 ||
		cg_aimFeedbackEnvelope.integer != 0 || cg_aimFeedbackLesson.integer != 0 ) ) {
		/* The two-pass launcher waits for this process before starting the next. */
		trap->SendConsoleCommand( "quit\n" );
	}
}

void CG_AimFeedback_OnEntityEvent( centity_t *cent, int event )
{
	afShot_t *shot;
	qboolean altFire;
	int shooterNum;
	int targetNum;
	int shotTime;
	int firedWeapon;

	if ( !cg.demoPlayback || !cg_aimFeedback.integer || !cg.snap ||
		cg_aimFeedbackReview.integer >= 2 || cg_aimFeedbackEnvelope.integer != 0 ||
		cg_aimFeedbackLesson.integer != 0 ) {
		return;
	}
	if ( event != EV_FIRE_WEAPON && event != EV_ALT_FIRE ) {
		return;
	}

	shooterNum = cent->currentState.number;
	if ( shooterNum != cg.snap->ps.clientNum || shooterNum < 0 || shooterNum >= MAX_CLIENTS ) {
		return;
	}
	altFire = event == EV_ALT_FIRE;
	shotTime = cg.snap->serverTime;
	firedWeapon = cg.snap->ps.weapon;
	if ( cg_aimFeedback.integer > 1 ) {
		trap->Print( "[aimfeedback] fire event: %s, weapon %i, %s\n",
			cgs.clientinfo[shooterNum].name, firedWeapon, altFire ? "alt" : "primary" );
	}

	if ( af_latestShot >= 0 ) {
		afShot_t *previous = &af_shots[af_latestShot];
		if ( previous->shotTime == shotTime && previous->weapon == firedWeapon &&
			previous->altFire == altFire ) {
			return;
		}
	}

	shot = &af_shots[af_nextShot];
	memset( shot, 0, sizeof( *shot ) );
	shot->weapon = firedWeapon;
	shot->altFire = altFire;
	shot->weaponClass = AF_ClassifyWeapon( shot->weapon, altFire );
	if ( shot->weaponClass == AF_WEAPON_NONE ) {
		return;
	}

	shot->active = qtrue;
	shot->shotTime = shotTime;
	shot->shooterNum = shooterNum;
	VectorCopy( cg.snap->ps.origin, shot->shooterOrigin );
	VectorCopy( cg.snap->ps.viewangles, shot->shotAngles );
	shot->shooterViewheight = cg.snap->ps.viewheight;
	AF_CalcMuzzle( shot, shot->shotAngles, shot->actualMuzzle );

	switch ( shot->weaponClass ) {
	case AF_WEAPON_PISTOL:
		shot->projectileSpeed = 1600.0f;
		shot->projectileRadius = altFire ? 0.5f : 0.0f;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	case AF_WEAPON_REPEATER_ALT:
		shot->projectileSpeed = 1100.0f;
		shot->projectileRadius = 3.0f;
		shot->verticalBoost = 40.0f;
		shot->gravity = qtrue;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	case AF_WEAPON_THERMAL_ALT:
		shot->projectileRadius = 3.0f;
		shot->verticalBoost = 120.0f;
		shot->gravity = qtrue;
		shot->maxFlightMsec = 3000;
		break;
	case AF_WEAPON_ROCKET:
		shot->projectileSpeed = 900.0f;
		shot->projectileRadius = 3.0f;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	case AF_WEAPON_GOLAN_ALT:
		shot->projectileRadius = 3.0f;
		shot->gravity = qtrue;
		shot->maxFlightMsec = AF_CAPTURE_MSEC;
		break;
	default:
		break;
	}

	targetNum = AF_InferTarget( shooterNum, shot->actualMuzzle, shot->shotAngles );
	if ( targetNum < 0 ) {
		if ( cg_aimFeedback.integer > 1 ) {
			trap->Print( "[aimfeedback] ignored %s shot: no plausible visible enemy near the crosshair\n",
				AF_WeaponName( shot->weaponClass ) );
		}
		memset( shot, 0, sizeof( *shot ) );
		return;
	}
	shot->targetNum = targetNum;
	AF_AddInitialTargetSample( shot );
	AF_FindInitialProjectiles( shot );
	if ( cg_aimFeedback.integer > 1 ) {
		trap->Print( "[aimfeedback] captured %s shot at %s (%i)\n",
			AF_WeaponName( shot->weaponClass ), cgs.clientinfo[targetNum].name,
			targetNum );
	}

	af_latestShot = af_nextShot;
	af_nextShot = ( af_nextShot + 1 ) % AF_MAX_SHOTS;
}

static void AF_UpdateReviewDirector( void )
{
	afReviewEntry_t *entry;
	int realTime;

	if ( !af_reviewInitialized || af_reviewCount <= 0 ||
		af_reviewState == AF_REVIEW_DONE ) {
		return;
	}
	entry = &af_reviewEntries[af_reviewIndex];

	switch ( af_reviewState ) {
	case AF_REVIEW_TRANSITION:
		if ( cg.time >= entry->shotTime - AF_REVIEW_PREROLL_MSEC ) {
			AF_SetPlaybackScale( AF_REVIEW_SLOW_SCALE );
			af_reviewState = AF_REVIEW_PREROLL;
		}
		break;

	case AF_REVIEW_PREROLL:
		if ( cg.time >= entry->shotTime ) {
			AF_SetDemoFrozen( qtrue );
			af_reviewHoldStart = trap->Milliseconds();
			af_reviewState = AF_REVIEW_HOLD;
			if ( cg_aimFeedbackReview.integer == 2 && cg_aimFeedbackCapture.integer ) {
				trap->SendConsoleCommand( va( "screenshot xen_kejim_fov100_%02i_aim\n",
					af_reviewIndex + 1 ) );
			}
			if ( cg_aimFeedback.integer > 1 ) {
				trap->Print( "[aimfeedback] review %i/%i: %s at %ims\n",
					af_reviewIndex + 1, af_reviewCount,
					AF_WeaponName( (afWeaponClass_t)entry->weaponClass ), entry->shotTime );
			}
		}
		break;

	case AF_REVIEW_HOLD:
		realTime = trap->Milliseconds();
		if ( realTime - af_reviewHoldStart >= AF_REVIEW_HOLD_REAL_MSEC ) {
			AF_SetDemoFrozen( qfalse );
			AF_SetPlaybackScale( AF_REVIEW_PROOF_SCALE );
			af_reviewState = AF_REVIEW_POSTROLL;
		}
		break;

	case AF_REVIEW_POSTROLL:
		if ( cg.time >= entry->shotTime + AF_ReviewPostrollMsec( entry ) ) {
			af_reviewIndex++;
			if ( af_reviewIndex >= af_reviewCount ) {
				af_reviewState = AF_REVIEW_DONE;
				AF_SetDemoFrozen( qfalse );
				AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
				trap->Print( cg_aimFeedbackReview.integer == 4 ?
					"[aimfeedback] exact director validation complete\n" :
					"[aimfeedback] directed review complete\n" );
			} else {
				af_reviewState = AF_REVIEW_TRANSITION;
				AF_SetPlaybackScale( AF_REVIEW_FAST_SCALE );
			}
		}
		break;

	default:
		break;
	}
}

static void AF_AddGhostSprite( const vec3_t origin, float radius, byte alpha )
{
	refEntity_t sprite;

	if ( !af_reviewGhostShader ) {
		af_reviewGhostShader = trap->R_RegisterShader( "gfx/effects/greenFrontFlash" );
	}

	memset( &sprite, 0, sizeof( sprite ) );
	sprite.reType = RT_SPRITE;
	sprite.renderfx = RF_NODEPTH;
	VectorCopy( origin, sprite.origin );
	sprite.radius = radius;
	sprite.customShader = af_reviewGhostShader;
	sprite.shaderRGBA[0] = (byte)( 80 * alpha / 255 );
	sprite.shaderRGBA[1] = alpha;
	sprite.shaderRGBA[2] = (byte)( 100 * alpha / 255 );
	sprite.shaderRGBA[3] = 255;
	trap->R_AddRefEntityToScene( &sprite );
}

static void AF_ProofVelocityAtTime( const afReviewEntry_t *entry, float seconds,
	vec3_t velocity )
{
	VectorCopy( entry->proofVelocity, velocity );
	if ( entry->proofGravity ) {
		velocity[2] -= DEFAULT_GRAVITY * seconds;
	}
}

static void AF_AddGhostBolt( const vec3_t origin, const vec3_t velocity,
	float length, float width, byte alpha )
{
	refEntity_t bolt;
	vec3_t direction;

	if ( !af_reviewLineShader ) {
		af_reviewLineShader = trap->R_RegisterShader( "gfx/misc/whiteline2" );
	}
	if ( VectorNormalize2( velocity, direction ) == 0.0f ) {
		VectorSet( direction, 1.0f, 0.0f, 0.0f );
	}
	memset( &bolt, 0, sizeof( bolt ) );
	bolt.reType = RT_LINE;
	bolt.renderfx = RF_NODEPTH;
	VectorMA( origin, length * 0.20f, direction, bolt.origin );
	VectorMA( origin, -length * 0.80f, direction, bolt.oldorigin );
	bolt.radius = width;
	bolt.customShader = af_reviewLineShader;
	bolt.shaderRGBA[0] = (byte)( 40 * alpha / 255 );
	bolt.shaderRGBA[1] = alpha;
	bolt.shaderRGBA[2] = (byte)( 70 * alpha / 255 );
	bolt.shaderRGBA[3] = alpha;
	trap->R_AddRefEntityToScene( &bolt );
}

static void AF_AddGhostModel( qhandle_t model, const vec3_t origin,
	const vec3_t velocity, float scale, float roll, byte alpha )
{
	refEntity_t projectile;
	int axis;

	if ( !model ) {
		return;
	}
	memset( &projectile, 0, sizeof( projectile ) );
	projectile.reType = RT_MODEL;
	projectile.hModel = model;
	/* A neutral material makes the actual MD3 geometry a bright green
	   silhouette.  The previous additive flash and force-tinted stock textures
	   both obscured the rocket's elongated shape. */
	projectile.customShader = cgs.media.whiteShader;
	VectorCopy( origin, projectile.origin );
	VectorCopy( origin, projectile.oldorigin );
	if ( VectorNormalize2( velocity, projectile.axis[0] ) == 0.0f ) {
		VectorSet( projectile.axis[0], 1.0f, 0.0f, 0.0f );
	}
	PerpendicularVector( projectile.axis[1], projectile.axis[0] );
	CrossProduct( projectile.axis[0], projectile.axis[1], projectile.axis[2] );
	if ( roll != 0.0f ) {
		RotateAroundDirection( projectile.axis, roll );
	}
	if ( scale != 1.0f ) {
		for ( axis = 0; axis < 3; axis++ ) {
			VectorScale( projectile.axis[axis], scale, projectile.axis[axis] );
		}
		projectile.nonNormalizedAxes = qtrue;
	}
	projectile.renderfx = RF_NOSHADOW | RF_NODEPTH | RF_RGB_TINT | RF_MINLIGHT |
		RF_FORCE_ENT_ALPHA;
	projectile.shaderRGBA[0] = (byte)( 30 * alpha / 255 );
	projectile.shaderRGBA[1] = alpha;
	projectile.shaderRGBA[2] = (byte)( 55 * alpha / 255 );
	projectile.shaderRGBA[3] = alpha;
	trap->R_AddRefEntityToScene( &projectile );
}

static void AF_AddWeaponGhost( const afReviewEntry_t *entry, const vec3_t origin,
	float seconds, byte alpha )
{
	vec3_t velocity;

	AF_ProofVelocityAtTime( entry, seconds, velocity );
	switch ( (afWeaponClass_t)entry->weaponClass ) {
	case AF_WEAPON_PISTOL:
		AF_AddGhostBolt( origin, velocity, 26.0f, 2.5f, alpha );
		AF_AddGhostSprite( origin, 3.5f, alpha );
		break;

	case AF_WEAPON_REPEATER_ALT:
		if ( !af_reviewSphereModel ) {
			af_reviewSphereModel = trap->R_RegisterModel( "models/items/sphere.md3" );
		}
		/* sphere.md3 has an 87-unit radius.  Keep the rendered orb close to the
		   actual three-unit collision radius instead of filling the camera. */
		AF_AddGhostModel( af_reviewSphereModel, origin, velocity, 0.040f,
			seconds * 540.0f, alpha );
		AF_AddGhostSprite( origin, 1.5f, (byte)( alpha / 2 ) );
		AF_AddGhostBolt( origin, velocity, 9.0f, 2.0f, alpha );
		break;

	case AF_WEAPON_THERMAL_ALT:
		if ( !af_reviewThermalModel ) {
			af_reviewThermalModel = trap->R_RegisterModel(
				"models/weapons2/thermal/thermal_proj.md3" );
		}
		AF_AddGhostModel( af_reviewThermalModel, origin, velocity, 1.0f,
			seconds * 720.0f, alpha );
		AF_AddGhostSprite( origin, 1.5f, (byte)( alpha / 2 ) );
		break;

	case AF_WEAPON_ROCKET:
		if ( !af_reviewRocketModel ) {
			af_reviewRocketModel = trap->R_RegisterModel(
				"models/weapons2/merr_sonn/projectile.md3" );
		}
		AF_AddGhostModel( af_reviewRocketModel, origin, velocity, 1.0f, 0.0f,
			alpha );
		AF_AddGhostSprite( origin, 1.25f, (byte)( alpha / 2 ) );
		AF_AddGhostBolt( origin, velocity, 11.0f, 1.25f, (byte)( alpha * 3 / 4 ) );
		break;

	case AF_WEAPON_GOLAN_ALT:
		if ( !af_reviewGolanModel ) {
			af_reviewGolanModel = trap->R_RegisterModel(
				"models/weapons2/golan_arms/projectile.md3" );
		}
		AF_AddGhostModel( af_reviewGolanModel, origin, velocity, 1.0f,
			seconds * 600.0f, alpha );
		AF_AddGhostSprite( origin, 1.25f, (byte)( alpha / 2 ) );
		break;

	default:
		AF_AddGhostSprite( origin, 6.0f, alpha );
		break;
	}
}

static float AF_ReviewMarkerRadius( const afReviewEntry_t *entry )
{
	switch ( (afWeaponClass_t)entry->weaponClass ) {
	case AF_WEAPON_PISTOL:
		return 1.5f;
	case AF_WEAPON_REPEATER_ALT:
		return 2.25f;
	case AF_WEAPON_THERMAL_ALT:
		return 2.5f;
	case AF_WEAPON_ROCKET:
		return 1.75f;
	case AF_WEAPON_GOLAN_ALT:
		return 2.0f;
	default:
		return 2.0f;
	}
}

static void AF_AddReviewProofProjectile( void )
{
	const afReviewEntry_t *entry;
	centity_t *target;
	vec3_t position;
	vec3_t relative;
	vec3_t mins, maxs;
	float elapsed;
	float verifyEnd;
	float radius;
	int trail;

	if ( !af_reviewInitialized || af_reviewIndex >= af_reviewCount ||
		( af_reviewState != AF_REVIEW_HOLD && af_reviewState != AF_REVIEW_POSTROLL ) ) {
		return;
	}
	entry = &af_reviewEntries[af_reviewIndex];
	if ( !entry->proofValid || entry->proofContactTime <= 0.0f ) {
		return;
	}
	if ( af_reviewProofTrackedIndex != af_reviewIndex ) {
		af_reviewProofTrackedIndex = af_reviewIndex;
		af_reviewProofImpact = qfalse;
		af_reviewProofImpactTime = 0.0f;
		af_reviewProofPreviousValid = qfalse;
		af_reviewProofLogged = qfalse;
	}

	elapsed = ( cg.time - entry->shotTime ) * 0.001f;
	if ( elapsed < 0.0f ) {
		return;
	}
	/* These sprites mark sampled trajectory positions; they are not the
	   projectile body.  Keep them subordinate to the weapon-specific model. */
	radius = AF_ReviewMarkerRadius( entry );
	verifyEnd = entry->proofContactTime + AF_REVIEW_PROOF_VERIFY_MSEC * 0.001f;
	target = &cg_entities[entry->targetNum];

	/* Independently verify the cached solution against the target actually
	   rendered in slow playback.  The relative segment test catches a fast
	   projectile crossing the live hitbox between two rendered frames. */
	if ( !af_reviewProofImpact && elapsed <= verifyEnd &&
		target->currentValid && target->currentState.eType == ET_PLAYER ) {
		float fraction = 0.0f;
		qboolean overlap;
		AF_EvaluateProjectile( entry->proofMuzzle, entry->proofVelocity,
			entry->proofGravity, elapsed, position );
		VectorSubtract( position, target->lerpOrigin, relative );
		AF_DecodeBounds( &target->currentState, mins, maxs );
		mins[0] -= entry->proofRadius;
		mins[1] -= entry->proofRadius;
		mins[2] -= entry->proofRadius;
		maxs[0] += entry->proofRadius;
		maxs[1] += entry->proofRadius;
		maxs[2] += entry->proofRadius;
		overlap = AF_SegmentAABB(
			af_reviewProofPreviousValid ? af_reviewProofPreviousRelative : relative,
			relative, mins, maxs, &fraction );
		if ( overlap && ( !af_reviewProofPreviousValid ||
			Distance( af_reviewProofPreviousTargetOrigin, target->lerpOrigin ) <= AF_TELEPORT_DIST ) ) {
			af_reviewProofImpact = qtrue;
			af_reviewProofImpactTime = af_reviewProofPreviousValid ?
				af_reviewProofPreviousTime +
				( elapsed - af_reviewProofPreviousTime ) * fraction : elapsed;
			if ( af_reviewProofPreviousValid ) {
				VectorSubtract( position, af_reviewProofPreviousPosition, relative );
				VectorMA( af_reviewProofPreviousPosition, fraction, relative,
					af_reviewProofImpactPoint );
			} else {
				VectorCopy( position, af_reviewProofImpactPoint );
			}
			if ( cg_aimFeedbackReview.integer == 4 ) {
				AF_AppendCacheEntry( AF_REVIEW_PROVEN_PATH, entry );
			}
			if ( cg_aimFeedbackReview.integer == 2 && cg_aimFeedbackCapture.integer ) {
				trap->SendConsoleCommand( va( "screenshot xen_kejim_fov100_%02i_hit\n",
					af_reviewIndex + 1 ) );
			}
			if ( cg_aimFeedback.integer > 1 ) {
				trap->Print( "[aimfeedback] live proof hit: target %i %s at %.3fs (cached %.3fs)\n",
					entry->targetNum, cgs.clientinfo[entry->targetNum].name,
					af_reviewProofImpactTime, entry->proofContactTime );
			}
		} else {
			af_reviewProofPreviousValid = qtrue;
			af_reviewProofPreviousTime = elapsed;
			VectorCopy( position, af_reviewProofPreviousPosition );
			VectorCopy( relative, af_reviewProofPreviousRelative );
			VectorCopy( target->lerpOrigin, af_reviewProofPreviousTargetOrigin );
		}
	}

	if ( !af_reviewProofImpact && elapsed <= verifyEnd ) {
		for ( trail = 4; trail >= 0; trail-- ) {
			float trailTime = elapsed - trail * 0.035f;
			byte alpha;
			if ( trailTime < 0.0f ) {
				continue;
			}
			AF_EvaluateProjectile( entry->proofMuzzle, entry->proofVelocity,
				entry->proofGravity, trailTime, position );
			alpha = (byte)( 70 + ( 4 - trail ) * 42 );
			AF_AddGhostSprite( position,
				radius * ( 0.45f + ( 4 - trail ) * 0.10f ), alpha );
		}
		AF_EvaluateProjectile( entry->proofMuzzle, entry->proofVelocity,
			entry->proofGravity, elapsed, position );
		AF_AddWeaponGhost( entry, position, elapsed, 255 );
		trap->R_AddLightToScene( position, 90.0f, 0.15f, 1.0f, 0.20f );
	} else if ( af_reviewProofImpact &&
		elapsed <= af_reviewProofImpactTime + AF_REVIEW_PROOF_TAIL_MSEC * 0.001f ) {
		float fraction = ( elapsed - af_reviewProofImpactTime ) /
			( AF_REVIEW_PROOF_TAIL_MSEC * 0.001f );
		byte alpha = (byte)( 255.0f * ( 1.0f - AF_ClampFloat( fraction, 0.0f, 1.0f ) ) );
		AF_AddWeaponGhost( entry, af_reviewProofImpactPoint,
			af_reviewProofImpactTime, alpha );
		AF_AddGhostSprite( af_reviewProofImpactPoint,
			radius * ( 1.1f + fraction * 1.0f ), alpha );
		trap->R_AddLightToScene( af_reviewProofImpactPoint, 130.0f * ( 1.0f - fraction ),
			0.15f, 1.0f, 0.20f );
	} else if ( !af_reviewProofImpact && elapsed > verifyEnd &&
		cg_aimFeedback.integer > 1 && !af_reviewProofLogged ) {
		af_reviewProofLogged = qtrue;
		trap->Print( "[aimfeedback] live proof FAILED: target %i %s, cached %.3fs\n",
			entry->targetNum, cgs.clientinfo[entry->targetNum].name,
			entry->proofContactTime );
	}
}

static const char *AF_LessonCaptureName( int stage )
{
	switch ( stage ) {
	case 1:  return "golan_lesson_01_context";
	case 2:  return "golan_lesson_02_rocket_900";
	case 3:  return "golan_lesson_03_straight_1050";
	case 4:  return "golan_lesson_04_gravity_and_lift";
	case 5:  return "golan_lesson_05_one_ball_heatmap";
	case 6:  return "golan_lesson_06_two_ball_heatmap";
	default: return "golan_lesson_07_probability_analysis";
	}
}

static void AF_CalculateLessonMetrics( int trackIndex )
{
	const afFCTrackEntry_t *current;
	const afFCTrackEntry_t *after;
	vec3_t targetCenter, toTarget, fromFlag;
	float seconds;
	float radialSpeed;
	int afterIndex = trackIndex + 4;

	if ( trackIndex < 0 || trackIndex >= af_fcTrackCount ) {
		return;		/* no track (the .aft is missing) or past its end */
	}
	current = &af_fcTrack[trackIndex];
	after = current;

	if ( afterIndex < af_fcTrackCount &&
		af_fcTrack[afterIndex].carrierNum == current->carrierNum ) {
		after = &af_fcTrack[afterIndex];
	}
	/* Describe the motion immediately following the frozen frame.  A centered
	   difference can point into the past across a sudden pull, wall impact, or
	   strafe reversal even though the solver correctly follows the future. */
	seconds = ( after->time - current->time ) * 0.001f;
	if ( seconds > 0.0f ) {
		VectorSubtract( after->origin, current->origin, af_lessonTargetVelocity );
		VectorScale( af_lessonTargetVelocity, 1.0f / seconds, af_lessonTargetVelocity );
	} else {
		VectorClear( af_lessonTargetVelocity );
	}
	af_lessonTargetSpeed = VectorLength( af_lessonTargetVelocity );
	VectorCopy( current->origin, targetCenter );
	targetCenter[2] += ( current->mins[2] + current->maxs[2] ) * 0.5f;
	VectorSubtract( targetCenter, af_envelopeShot.actualMuzzle, toTarget );
	af_lessonDistance = VectorNormalize( toTarget );
	radialSpeed = DotProduct( af_lessonTargetVelocity, toTarget );
	af_lessonLateralSpeed = sqrtf( fmaxf( 0.0f,
		af_lessonTargetSpeed * af_lessonTargetSpeed - radialSpeed * radialSpeed ) );
	VectorSet( fromFlag, current->origin[0] - 152.0f,
		current->origin[1] + 1160.0f, current->origin[2] - 1920.0f );
	if ( VectorNormalize( fromFlag ) > 0.0f ) {
		af_lessonAwaySpeed = DotProduct( af_lessonTargetVelocity, fromFlag );
	} else {
		af_lessonAwaySpeed = 0.0f;
	}
}

static void AF_PrepareLessonStage( int stage )
{
	const afFCTrackEntry_t *track;

	af_envelopeCellCount = 0;
	af_envelopeBestCell = -1;
	if ( af_lessonTrackIndex < 0 || af_lessonTrackIndex >= af_fcTrackCount ) {
		return;
	}
	track = &af_fcTrack[af_lessonTrackIndex];
	if ( track->carrierNum < 0 || !track->alive ||
		!AF_SetupLessonShot( &af_envelopeShot, stage, track->carrierNum ) ||
		!AF_AddEnvelopeTargetSamples( &af_envelopeShot, af_lessonTrackIndex ) ) {
		return;
	}
	if ( stage == 1 ) {
		AF_CalculateLessonMetrics( af_lessonTrackIndex );
	} else {
		AF_SolveLessonStage( &af_envelopeShot, stage );
	}
}

static void AF_StartLessonStage( int stage )
{
	af_lessonStage = stage;
	af_lessonCaptured = qfalse;
	AF_PrepareLessonStage( stage );
	af_lessonStageStart = trap->Milliseconds();
	if ( cg_aimFeedback.integer > 1 ) {
		trap->Print( "[aimfeedback] Golan lesson stage %i/%i: %s; %i cells%s\n",
			stage, AF_LESSON_STAGE_COUNT, AF_LessonCaptureName( stage ),
			af_envelopeCellCount,
			af_envelopeBestCell >= 0 ? va( ", best %.1f/%.1f at %.3fs",
				af_envelopeCells[af_envelopeBestCell].pitchDelta,
				af_envelopeCells[af_envelopeBestCell].yawDelta,
				af_envelopeCells[af_envelopeBestCell].contactTime ) : "" );
	}
}

static void AF_UpdateLessonDirector( void )
{
	int lessonTime = cg_aimFeedbackLessonTime.integer > 0 ?
		cg_aimFeedbackLessonTime.integer : AF_LESSON_DEFAULT_TIME;
	int realTime;

	if ( af_lessonFinished ) {
		return;
	}
	if ( !af_lessonFrozen ) {
		if ( cg.time < lessonTime - 1000 ) {
			AF_SetPlaybackScale( AF_LESSON_FAST_SCALE );
			return;
		}
		if ( cg.time < lessonTime ) {
			AF_SetPlaybackScale( AF_LESSON_APPROACH_SCALE );
			return;
		}
		AF_SetDemoFrozen( qtrue );
		af_lessonFrozen = qtrue;
		af_lessonTrackIndex = AF_FCTrackIndexAtTime( cg.time );
		af_lessonNominalAimFound = qfalse;
		/* Preserve the recorded world state, but clear short-lived HUD notices
		   so they do not obscure the lesson in every capture. */
		cg.centerPrintTime = 0;
		cg.itemPickup = 0;
		cg.itemPickupTime = 0;
		cg.weaponSelectTime = 0;
		cg.invenSelectTime = 0;
		cg.forceSelectTime = 0;
		AF_StartLessonStage( 1 );
		return;
	}

	realTime = trap->Milliseconds();
	if ( !af_lessonCaptured && realTime - af_lessonStageStart >= 350 ) {
		if ( cg_aimFeedbackCapture.integer ) {
			trap->SendConsoleCommand( va( "screenshot %s\n",
				AF_LessonCaptureName( af_lessonStage ) ) );
		}
		af_lessonCaptured = qtrue;
	}
	if ( af_lessonStage < AF_LESSON_STAGE_COUNT &&
		realTime - af_lessonStageStart >= 800 ) {
		AF_StartLessonStage( af_lessonStage + 1 );
		return;
	}
	if ( af_lessonStage == AF_LESSON_STAGE_COUNT && af_lessonCaptured &&
		realTime - af_lessonStageStart >= 1000 ) {
		af_lessonFinished = qtrue;
		if ( cg_aimFeedbackCapture.integer ) {
			trap->SendConsoleCommand( "quit\n" );
		}
	}
}

static void AF_UpdateCandidateDirector( void )
{
	int candidateTime;
	int realTime;

	if ( af_lessonFinished || af_candidateIndex < 0 ||
		af_candidateIndex >= AF_CANDIDATE_COUNT ) {
		return;
	}
	candidateTime = af_candidateTimes[af_candidateIndex];
	if ( !af_lessonFrozen ) {
		if ( cg.time < candidateTime - 1000 ) {
			AF_SetPlaybackScale( AF_LESSON_FAST_SCALE );
			return;
		}
		if ( cg.time < candidateTime ) {
			AF_SetPlaybackScale( AF_LESSON_APPROACH_SCALE );
			return;
		}
		AF_SetDemoFrozen( qtrue );
		af_lessonFrozen = qtrue;
		af_lessonTrackIndex = AF_FCTrackIndexAtTime( candidateTime );
		af_lessonNominalAimFound = qfalse;
		cg.centerPrintTime = 0;
		cg.itemPickup = 0;
		cg.itemPickupTime = 0;
		cg.weaponSelectTime = 0;
		cg.invenSelectTime = 0;
		cg.forceSelectTime = 0;
		AF_StartLessonStage( 2 );
		/* Stage 1 normally initializes these metrics after setting up its shot.
		   Candidate mode begins directly at the rocket stage, so calculate only
		   after AF_StartLessonStage has populated af_envelopeShot. */
		AF_CalculateLessonMetrics( af_lessonTrackIndex );
		if ( cg_aimFeedback.integer > 1 ) {
			trap->Print( "[aimfeedback] candidate %i/%i at %i\n",
				af_candidateIndex + 1, AF_CANDIDATE_COUNT, candidateTime );
		}
		return;
	}

	realTime = trap->Milliseconds();
	if ( !af_lessonCaptured && realTime - af_lessonStageStart >= 350 ) {
		if ( cg_aimFeedbackCapture.integer ) {
			trap->SendConsoleCommand( va(
				"screenshot golan_rocket_subset_v2_%02i_t%i\n",
				af_candidateIndex + 1, candidateTime ) );
		}
		af_lessonCaptured = qtrue;
	}
	if ( af_lessonCaptured && realTime - af_lessonStageStart >= 800 ) {
		if ( af_candidateIndex + 1 >= AF_CANDIDATE_COUNT ) {
			af_lessonFinished = qtrue;
			if ( cg_aimFeedbackCapture.integer ) {
				trap->SendConsoleCommand( "quit\n" );
			}
			return;
		}
		AF_SetDemoFrozen( qfalse );
		AF_SetPlaybackScale( AF_LESSON_FAST_SCALE );
		af_lessonFrozen = qfalse;
		af_lessonStage = 0;
		af_lessonCaptured = qfalse;
		af_candidateIndex++;
	}
}

static void AF_UpdateEnvelopeDirector( void )
{
	int index = AF_FCTrackIndexAtTime( cg.time );
	qboolean normalSpeed = qfalse;
	int desiredState;
	int i;

	if ( index >= 0 && index < af_fcTrackCount &&
		af_fcTrack[index].carrierNum >= 0 && af_fcTrack[index].alive ) {
		normalSpeed = qtrue;
	} else if ( index >= 0 ) {
		for ( i = index + 1; i < af_fcTrackCount; i++ ) {
			if ( af_fcTrack[i].carrierNum >= 0 && af_fcTrack[i].alive ) {
				normalSpeed = af_fcTrack[i].time - cg.time <= AF_ENVELOPE_PREROLL_MSEC;
				break;
			}
		}
	}

	desiredState = normalSpeed ? 0 : 1;
	if ( desiredState != af_envelopeScaleState ) {
		AF_SetPlaybackScale( normalSpeed ? AF_ENVELOPE_NORMAL_SCALE :
			AF_ENVELOPE_FAST_SCALE );
		af_envelopeScaleState = desiredState;
	}
}

static void AF_UpdateEnvelope( void )
{
	const afFCTrackEntry_t *track;
	afWeaponClass_t weaponClass;
	int trackIndex;
	int targetNum;
	int weapon;

	if ( af_envelopeCaptureWeapon != AF_WEAPON_NONE ) {
		int realTime = trap->Milliseconds();
		if ( af_envelopeCaptureScreenshotTime > 0 &&
			realTime >= af_envelopeCaptureScreenshotTime ) {
			trap->SendConsoleCommand( va(
				"screenshot xen_kejim_envelope_settled_%02i\n",
				af_envelopeCaptureWeapon ) );
			af_envelopeCaptureMask |= 1 << af_envelopeCaptureWeapon;
			af_envelopeCaptureScreenshotTime = 0;
			af_envelopeCaptureUnfreezeTime = realTime + 150;
		} else if ( af_envelopeCaptureUnfreezeTime > 0 &&
			realTime >= af_envelopeCaptureUnfreezeTime ) {
			AF_SetDemoFrozen( qfalse );
			af_envelopeCaptureWeapon = AF_WEAPON_NONE;
			af_envelopeCaptureUnfreezeTime = 0;
		}
	}
	AF_UpdateEnvelopeDirector();
	trackIndex = AF_FCTrackIndexAtTime( cg.time );
	if ( trackIndex < 0 || trackIndex >= af_fcTrackCount ) {
		af_envelopeCellCount = 0;
		af_envelopeBestCell = -1;
		return;
	}
	track = &af_fcTrack[trackIndex];
	targetNum = track->carrierNum;
	weapon = cg.snap->ps.weapon;
	weaponClass = AF_EnvelopeClassForWeapon( weapon );
	if ( targetNum < 0 || !track->alive || weaponClass == AF_WEAPON_NONE ||
		cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		af_envelopeCellCount = 0;
		af_envelopeBestCell = -1;
		af_envelopeTargetNum = targetNum;
		af_envelopeLastWeapon = weapon;
		return;
	}
	if ( af_envelopeLastSolveTime > 0 &&
		cg.time - af_envelopeLastSolveTime < AF_ENVELOPE_SOLVE_MSEC &&
		af_envelopeTargetNum == targetNum && af_envelopeLastWeapon == weapon ) {
		return;
	}

	af_envelopeLastSolveTime = cg.time;
	af_envelopeTargetNum = targetNum;
	af_envelopeLastWeapon = weapon;
	if ( !AF_SetupEnvelopeShot( &af_envelopeShot, targetNum ) ||
		!AF_AddEnvelopeTargetSamples( &af_envelopeShot, trackIndex ) ) {
		af_envelopeCellCount = 0;
		af_envelopeBestCell = -1;
		return;
	}
	AF_SolveEnvelope( &af_envelopeShot );
	if ( af_envelopeCellCount > 0 && cg_aimFeedbackCapture.integer && !cg.showScores &&
		af_envelopeCaptureWeapon == AF_WEAPON_NONE &&
		!( af_envelopeCaptureMask & ( 1 << weaponClass ) ) ) {
		/* First hold the validated frame long enough to render it, then issue
		   the screenshot against that settled front buffer on a later frame. */
		AF_SetDemoFrozen( qtrue );
		af_envelopeCaptureWeapon = weaponClass;
		af_envelopeCaptureScreenshotTime = trap->Milliseconds() + 100;
	}
	if ( cg_aimFeedback.integer > 1 ) {
		trap->Print( "[aimfeedback] envelope %s -> %s: %i validated cells\n",
			AF_WeaponName( weaponClass ), cgs.clientinfo[targetNum].name,
			af_envelopeCellCount );
	}
}

void CG_AimFeedback_Update( void )
{
	int i;

	if ( !cg.demoPlayback || !cg_aimFeedback.integer || !cg.snap ) {
		return;
	}
	if ( af_lastTime && cg.time < af_lastTime ) {
		CG_InitAimFeedback();
	}
	if ( !af_labStarted ) {
		AF_StartLab();
	}
	af_lastTime = cg.time;
	if ( cg_aimFeedbackLesson.integer ) {
		if ( cg_aimFeedbackLesson.integer == 2 ) {
			AF_UpdateCandidateDirector();
		} else {
			AF_UpdateLessonDirector();
		}
		return;
	}
	if ( cg_aimFeedbackEnvelope.integer == 1 ) {
		AF_RecordFCTrackSample();
		return;
	}
	if ( cg_aimFeedbackEnvelope.integer == 2 ) {
		AF_UpdateEnvelope();
		return;
	}
	if ( cg_aimFeedbackReview.integer == 2 || cg_aimFeedbackReview.integer == 4 ) {
		AF_UpdateReviewDirector();
		AF_AddReviewProofProjectile();
		return;
	}
	if ( cg_aimFeedbackReview.integer == 3 ) {
		AF_UpdateValidationPass();
		return;
	}

	for ( i = 0; i < AF_MAX_SHOTS; i++ ) {
		afShot_t *shot = &af_shots[i];
		const entityState_t *targetState;
		vec3_t targetOrigin;
		int p;

		if ( !shot->active || shot->result.ready ) {
			continue;
		}
		if ( cg.time < shot->shotTime ) {
			continue;
		}

		/* Store snapshot endpoints, not rendered lerpOrigin.  Timedemo may not
		   have a next snapshot while normal slow playback does; using lerpOrigin
		   made preprocessing and review follow different target paths. */
		targetState = AF_SnapshotEntity( shot->targetNum );
		if ( targetState && targetState->eType == ET_PLAYER ) {
			BG_EvaluateTrajectory( &targetState->pos, cg.snap->serverTime, targetOrigin );
			AF_AddTargetSampleFromState( shot, targetState,
				cg.snap->serverTime, targetOrigin );
		}

		if ( shot->numProjectiles < AF_MAX_PROJECTILES && cg.time <= shot->shotTime + 250 ) {
			int entityNum;
			for ( entityNum = MAX_CLIENTS; entityNum < ENTITYNUM_WORLD; entityNum++ ) {
				centity_t *missile = &cg_entities[entityNum];
				if ( missile->currentValid ) {
					AF_AddProjectile( shot, &missile->currentState );
				}
			}
		}

		for ( p = 0; p < shot->numProjectiles; p++ ) {
			afProjectileRoll_t *roll = &shot->projectiles[p];
			centity_t *missile = &cg_entities[roll->entityNum];
			if ( missile->currentValid && missile->currentState.eType == ET_MISSILE ) {
				roll->lastSeenTime = cg.time;
			} else if ( roll->lastSeenTime > shot->shotTime ) {
				roll->lifeMsec = (int)AF_ClampFloat( (float)( roll->lastSeenTime - shot->shotTime ), 1500.0f, 3500.0f );
			}
		}

		if ( cg.time >= shot->shotTime + AF_CAPTURE_MSEC ) {
			AF_FinalizeShot( shot );
		}
	}
}

static void AF_DrawMarker( float x, float y, float size, const vec4_t color )
{
	CG_FillRect( x - size, y - 1.0f, size * 2.0f, 2.0f, color );
	CG_FillRect( x - 1.0f, y - size, 2.0f, size * 2.0f, color );
}

static afShot_t *AF_GetDisplayShot( void )
{
	afShot_t *latest;
	afShot_t *bestReady = NULL;
	int bestTime = -1;
	int i;

	if ( af_latestShot < 0 ) {
		return NULL;
	}
	latest = &af_shots[af_latestShot];
	if ( latest->active && latest->result.ready ) {
		return latest;
	}

	for ( i = 0; i < AF_MAX_SHOTS; i++ ) {
		if ( af_shots[i].active && af_shots[i].result.ready && af_shots[i].shotTime > bestTime ) {
			bestTime = af_shots[i].shotTime;
			bestReady = &af_shots[i];
		}
	}

	return bestReady ? bestReady : latest;
}

static void AF_FormatCorrection( char *buffer, int bufferSize, const afCorrection_t *correction )
{
	const char *horizontal;
	const char *vertical;
	float yaw, pitch;

	if ( !correction->found ) {
		Q_strncpyz( buffer, "no direct solution in search range", bufferSize );
		return;
	}

	horizontal = correction->yawDelta >= 0.0f ? "L" : "R";
	vertical = correction->pitchDelta <= 0.0f ? "U" : "D";
	yaw = fabsf( correction->yawDelta );
	pitch = fabsf( correction->pitchDelta );
	Com_sprintf( buffer, bufferSize, "%s %.1f  %s %.1f  (%.2fs)",
		horizontal, yaw, vertical, pitch, correction->contactTime );
}

static void AF_DrawHeatInset( const afShot_t *shot, float x, float y, float size )
{
	const afResult_t *result = &shot->result;
	float cell = size / AF_HEAT_DIM;
	float scale = size / ( AF_HEAT_RANGE * 2.0f );
	int row, column;
	vec4_t color;

	for ( row = 0; row < AF_HEAT_DIM; row++ ) {
		for ( column = 0; column < AF_HEAT_DIM; column++ ) {
			if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
				float probability = result->heat[row][column];
				color[0] = 1.0f - probability;
				color[1] = probability;
				color[2] = result->guaranteedHeat[row][column] ? 0.85f : 0.1f;
				color[3] = 0.16f + probability * 0.42f;
				CG_FillRect( x + column * cell, y + row * cell, cell - 0.5f, cell - 0.5f, color );
			} else if ( result->bodyHeat[row][column] ) {
				AF_SetColor( color, 0.1f, 0.85f, 0.2f, 0.30f );
				CG_FillRect( x + column * cell, y + row * cell, cell - 0.5f, cell - 0.5f, color );
				if ( result->headHeat[row][column] ) {
					AF_SetColor( color, 1.0f, 0.8f, 0.1f, 0.38f );
					CG_FillRect( x + column * cell, y + row * cell, cell - 0.5f, cell - 0.5f, color );
				}
			}
		}
	}

	AF_SetColor( color, 0.7f, 0.7f, 0.7f, 0.8f );
	CG_DrawRect( x, y, size, size, 1.0f, color );
	CG_FillRect( x + size * 0.5f - 0.5f, y, 1.0f, size, color );
	CG_FillRect( x, y + size * 0.5f - 0.5f, size, 1.0f, color );

	AF_SetColor( color, 1.0f, 1.0f, 1.0f, 1.0f );
	AF_DrawMarker( x + size * 0.5f, y + size * 0.5f, 3.0f, color );
	if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
		AF_SetColor( color, 0.1f, 1.0f, 0.25f, 1.0f );
		AF_DrawMarker( x + size * 0.5f - result->bestYawDelta * scale,
			y + size * 0.5f + result->bestPitchDelta * scale, 4.0f, color );
		if ( result->realized.found ) {
			AF_SetColor( color, 0.1f, 0.8f, 1.0f, 1.0f );
			AF_DrawMarker( x + size * 0.5f - result->realized.yawDelta * scale,
				y + size * 0.5f + result->realized.pitchDelta * scale, 3.0f, color );
		}
	} else {
		if ( result->body.found ) {
			AF_SetColor( color, 0.1f, 1.0f, 0.25f, 1.0f );
			AF_DrawMarker( x + size * 0.5f - result->body.yawDelta * scale,
				y + size * 0.5f + result->body.pitchDelta * scale, 4.0f, color );
		}
		if ( result->head.found ) {
			AF_SetColor( color, 1.0f, 0.8f, 0.1f, 1.0f );
			AF_DrawMarker( x + size * 0.5f - result->head.yawDelta * scale,
				y + size * 0.5f + result->head.pitchDelta * scale, 3.0f, color );
		}
	}
}

static qboolean AF_AngularCorrectionToScreen( float pitchDelta, float yawDelta,
	float *screenX, float *screenY )
{
	float horizontalScale;
	float verticalScale;

	if ( cg.refdef.fov_x <= 1.0f || cg.refdef.fov_y <= 1.0f ) {
		return qfalse;
	}

	horizontalScale = tanf( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	verticalScale = tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	*screenX = 320.0f + cg_crosshairX.value +
		tanf( DEG2RAD( -yawDelta ) ) * 320.0f / horizontalScale;
	*screenY = 240.0f + cg_crosshairY.value +
		tanf( DEG2RAD( pitchDelta ) ) * 240.0f / verticalScale;

	return *screenX >= 4.0f && *screenX <= 636.0f &&
		*screenY >= 4.0f && *screenY <= 476.0f;
}

static void AF_DrawCorrectionCrosshair( float pitchDelta, float yawDelta,
	float size, const vec4_t color )
{
	vec4_t shadow = { 0.0f, 0.0f, 0.0f, 0.75f };
	float x, y;
	float gap = 2.5f;

	if ( !AF_AngularCorrectionToScreen( pitchDelta, yawDelta, &x, &y ) ) {
		return;
	}

	CG_FillRect( x - size - 1.0f, y - 2.0f, size - gap + 1.0f, 4.0f, shadow );
	CG_FillRect( x + gap, y - 2.0f, size - gap + 1.0f, 4.0f, shadow );
	CG_FillRect( x - 2.0f, y - size - 1.0f, 4.0f, size - gap + 1.0f, shadow );
	CG_FillRect( x - 2.0f, y + gap, 4.0f, size - gap + 1.0f, shadow );

	CG_FillRect( x - size, y - 1.0f, size - gap, 2.0f, color );
	CG_FillRect( x + gap, y - 1.0f, size - gap, 2.0f, color );
	CG_FillRect( x - 1.0f, y - size, 2.0f, size - gap, color );
	CG_FillRect( x - 1.0f, y + gap, 2.0f, size - gap, color );
}

static void AF_DrawEnvelope( void )
{
	float step;
	float horizontalScale;
	float verticalScale;
	float cellWidth;
	float cellHeight;
	int i;

	if ( af_envelopeCellCount <= 0 || !cg.snap ||
		cg.refdef.fov_x <= 1.0f || cg.refdef.fov_y <= 1.0f ) {
		return;
	}
	step = cg_aimFeedbackLesson.integer ? 0.5f :
		( af_envelopeShot.weaponClass == AF_WEAPON_GOLAN_ALT ? 0.75f : 0.5f );
	horizontalScale = tanf( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	verticalScale = tanf( DEG2RAD( cg.refdef.fov_y * 0.5f ) );
	cellWidth = fmaxf( 1.5f,
		tanf( DEG2RAD( step * 1.08f ) ) * 320.0f / horizontalScale );
	cellHeight = fmaxf( 1.5f,
		tanf( DEG2RAD( step * 1.08f ) ) * 240.0f / verticalScale );

	for ( i = 0; i < af_envelopeCellCount; i++ ) {
		const afEnvelopeCell_t *cell = &af_envelopeCells[i];
		vec4_t color;
		float absolutePitch = AngleNormalize180( af_envelopeShot.shotAngles[PITCH] +
			cell->pitchDelta );
		float absoluteYaw = AngleNormalize180( af_envelopeShot.shotAngles[YAW] +
			cell->yawDelta );
		float pitchDelta = AngleSubtract( absolutePitch, cg.snap->ps.viewangles[PITCH] );
		float yawDelta = AngleSubtract( absoluteYaw, cg.snap->ps.viewangles[YAW] );
		float x, y;
		float timeRange = fmaxf( 0.5f, af_envelopeShot.maxFlightMsec * 0.001f - 0.25f );
		float timeWeight = 1.0f - 0.65f * AF_ClampFloat(
			( cell->contactTime - 0.25f ) / timeRange, 0.0f, 1.0f );
		float alpha = ( 0.16f + 0.38f * timeWeight ) * cell->probability;

		if ( !AF_AngularCorrectionToScreen( pitchDelta, yawDelta, &x, &y ) ) {
			continue;
		}
		AF_SetColor( color, 0.05f, 1.0f, 0.16f,
			AF_ClampFloat( alpha, 0.08f, 0.68f ) );
		CG_FillRect( x - cellWidth * 0.55f, y - cellHeight * 0.55f,
			cellWidth * 1.10f, cellHeight * 1.10f, color );
	}

	if ( af_envelopeBestCell >= 0 && af_envelopeBestCell < af_envelopeCellCount ) {
		const afEnvelopeCell_t *best = &af_envelopeCells[af_envelopeBestCell];
		vec4_t shadow = { 0.0f, 0.0f, 0.0f, 0.90f };
		vec4_t dot = { 0.20f, 1.0f, 0.30f, 1.0f };
		float absolutePitch = AngleNormalize180( af_envelopeShot.shotAngles[PITCH] +
			best->pitchDelta );
		float absoluteYaw = AngleNormalize180( af_envelopeShot.shotAngles[YAW] +
			best->yawDelta );
		float pitchDelta = AngleSubtract( absolutePitch, cg.snap->ps.viewangles[PITCH] );
		float yawDelta = AngleSubtract( absoluteYaw, cg.snap->ps.viewangles[YAW] );
		float x, y;

		if ( AF_AngularCorrectionToScreen( pitchDelta, yawDelta, &x, &y ) ) {
			CG_FillRect( x - 2.5f, y - 2.5f, 5.0f, 5.0f, shadow );
			CG_FillRect( x - 1.25f, y - 1.25f, 2.5f, 2.5f, dot );
		}
	}
}

static void AF_DrawLessonScreenLine( float x1, float y1, float x2, float y2,
	float width, const vec4_t color )
{
	float dx = x2 - x1;
	float dy = y2 - y1;
	float length = sqrtf( dx * dx + dy * dy );
	int steps = (int)ceilf( length / 2.5f );
	int i;

	if ( steps < 1 ) {
		steps = 1;
	}
	for ( i = 0; i <= steps; i++ ) {
		float fraction = i / (float)steps;
		CG_FillRect( x1 + dx * fraction - width * 0.5f,
			y1 + dy * fraction - width * 0.5f, width, width, color );
	}
}

static qboolean AF_LessonTargetCenterAt( float seconds, vec3_t center )
{
	int targetTime = af_envelopeShot.shotTime + (int)( seconds * 1000.0f );
	int i;

	for ( i = 1; i < af_envelopeShot.numSamples; i++ ) {
		const afTargetSample_t *before = &af_envelopeShot.samples[i - 1];
		const afTargetSample_t *after = &af_envelopeShot.samples[i];
		float fraction;
		vec3_t move;

		if ( targetTime > after->time ) {
			continue;
		}
		fraction = ( targetTime - before->time ) /
			(float)fmaxf( 1.0f, after->time - before->time );
		fraction = AF_ClampFloat( fraction, 0.0f, 1.0f );
		VectorSubtract( after->origin, before->origin, move );
		VectorMA( before->origin, fraction, move, center );
		center[2] += ( after->mins[2] + after->maxs[2] ) * 0.5f;
		return qtrue;
	}
	return qfalse;
}

static float AF_LessonPathDuration( void )
{
	if ( af_lessonStage >= 2 && af_envelopeBestCell >= 0 &&
		af_envelopeBestCell < af_envelopeCellCount ) {
		return af_envelopeCells[af_envelopeBestCell].contactTime;
	}
	return 0.70f;
}

static void AF_DrawLessonFuturePath( void )
{
	vec4_t shadow = { 0.0f, 0.0f, 0.0f, 0.80f };
	vec4_t cyan = { 0.05f, 0.85f, 1.0f, 0.92f };
	vec3_t previousCenter;
	float previousX = 0.0f, previousY = 0.0f;
	float duration = AF_ClampFloat( AF_LessonPathDuration(), 0.10f, 1.20f );
	qboolean previousVisible = qfalse;
	int steps = (int)ceilf( duration / 0.05f );
	int i;

	if ( !AF_LessonTargetCenterAt( 0.0f, previousCenter ) ) {
		return;
	}
	previousVisible = CG_WorldCoordToScreenCoordFloat( previousCenter,
		&previousX, &previousY );
	for ( i = 1; i <= steps; i++ ) {
		vec3_t center;
		float seconds = duration * i / (float)steps;
		float x, y;
		qboolean visible;

		if ( !AF_LessonTargetCenterAt( seconds, center ) ) {
			break;
		}
		visible = CG_WorldCoordToScreenCoordFloat( center, &x, &y );
		if ( previousVisible && visible ) {
			AF_DrawLessonScreenLine( previousX, previousY, x, y, 3.0f, shadow );
			AF_DrawLessonScreenLine( previousX, previousY, x, y, 1.25f, cyan );
		}
		if ( visible && ( i % 2 == 0 || i == steps ) ) {
			CG_FillRect( x - 1.5f, y - 1.5f, 3.0f, 3.0f, cyan );
		}
		previousX = x;
		previousY = y;
		previousVisible = visible;
	}
	if ( previousVisible ) {
		AF_DrawMarker( previousX, previousY, 6.0f, cyan );
	}
}

static void AF_DrawLessonVelocityArrow( void )
{
	vec3_t start, end;
	vec4_t shadow = { 0.0f, 0.0f, 0.0f, 0.85f };
	vec4_t cyan = { 0.05f, 0.85f, 1.0f, 0.95f };
	float x1, y1, x2, y2;
	float dx, dy, length, ux, uy;

	if ( af_lessonTrackIndex < 0 || af_lessonTrackIndex >= af_fcTrackCount ) {
		return;
	}
	if ( !AF_LessonTargetCenterAt( 0.0f, start ) ) {
		return;
	}
	VectorMA( start, 0.22f, af_lessonTargetVelocity, end );
	if ( !CG_WorldCoordToScreenCoordFloat( start, &x1, &y1 ) ||
		!CG_WorldCoordToScreenCoordFloat( end, &x2, &y2 ) ) {
		return;
	}
	AF_DrawLessonScreenLine( x1, y1, x2, y2, 4.0f, shadow );
	AF_DrawLessonScreenLine( x1, y1, x2, y2, 2.0f, cyan );
	dx = x2 - x1;
	dy = y2 - y1;
	length = sqrtf( dx * dx + dy * dy );
	if ( length < 1.0f ) {
		return;
	}
	ux = dx / length;
	uy = dy / length;
	AF_DrawLessonScreenLine( x2, y2, x2 - ux * 10.0f - uy * 6.0f,
		y2 - uy * 10.0f + ux * 6.0f, 2.0f, cyan );
	AF_DrawLessonScreenLine( x2, y2, x2 - ux * 10.0f + uy * 6.0f,
		y2 - uy * 10.0f - ux * 6.0f, 2.0f, cyan );
}

static void AF_DrawLessonImpactCloud( qboolean fullRandom )
{
	vec3_t viewAngles, targetCenter;
	vec4_t dot = { 0.20f, 1.0f, 0.35f, 0.70f };
	vec4_t target = { 0.05f, 0.85f, 1.0f, 1.0f };
	int count = fullRandom ? 96 : 48;
	int i;

	if ( !af_lessonNominalAimFound || af_lessonNominalContactTime <= 0.0f ) {
		return;
	}
	VectorCopy( af_envelopeShot.shotAngles, viewAngles );
	viewAngles[PITCH] = AngleNormalize180( viewAngles[PITCH] +
		af_lessonNominalPitchDelta );
	viewAngles[YAW] = AngleNormalize180( viewAngles[YAW] +
		af_lessonNominalYawDelta );
	for ( i = 0; i < count; i++ ) {
		afGolanRoll_t roll;
		vec3_t launchAngles, forward, velocity, position;
		float x, y;

		if ( fullRandom ) {
			AF_MakeGolanRoll( i, &roll );
		} else {
			roll.speed = 700.0f + 700.0f * AF_Halton( i + 1, 5 );
			roll.pitchOffset = -10.0f;
			roll.yawOffset = 0.0f;
			roll.lifeMsec = 3500;
		}
		VectorCopy( viewAngles, launchAngles );
		launchAngles[PITCH] = AngleNormalize180( launchAngles[PITCH] + roll.pitchOffset );
		launchAngles[YAW] = AngleNormalize180( launchAngles[YAW] + roll.yawOffset );
		AngleVectors( launchAngles, forward, NULL, NULL );
		VectorScale( forward, roll.speed, velocity );
		AF_EvaluateProjectile( af_envelopeShot.actualMuzzle, velocity,
			qtrue, af_lessonNominalContactTime, position );
		if ( CG_WorldCoordToScreenCoordFloat( position, &x, &y ) ) {
			CG_FillRect( x - 1.5f, y - 1.5f, 3.0f, 3.0f, dot );
		}
	}
	if ( AF_LessonTargetCenterAt( af_lessonNominalContactTime, targetCenter ) ) {
		float x, y;
		if ( CG_WorldCoordToScreenCoordFloat( targetCenter, &x, &y ) ) {
			AF_DrawMarker( x, y, 8.0f, target );
		}
	}
	AF_DrawCorrectionCrosshair( af_lessonNominalPitchDelta,
		af_lessonNominalYawDelta, 8.0f, target );
}

static qboolean AF_LessonCellScreen( const afEnvelopeCell_t *cell,
	float *x, float *y )
{
	float absolutePitch = AngleNormalize180( af_envelopeShot.shotAngles[PITCH] +
		cell->pitchDelta );
	float absoluteYaw = AngleNormalize180( af_envelopeShot.shotAngles[YAW] +
		cell->yawDelta );
	float pitchDelta = AngleSubtract( absolutePitch,
		cg.snap->ps.viewangles[PITCH] );
	float yawDelta = AngleSubtract( absoluteYaw, cg.snap->ps.viewangles[YAW] );

	return AF_AngularCorrectionToScreen( pitchDelta, yawDelta, x, y );
}

static void AF_DrawLessonOutcomeBar( float y, const char *label,
	float probability, const vec4_t color )
{
	vec4_t track = { 0.22f, 0.24f, 0.28f, 1.0f };
	vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
	char text[96];

	Com_sprintf( text, sizeof( text ), "%s  %.1f%%", label,
		probability * 100.0f );
	CG_Text_Paint( 438.0f, y, 0.50f, white, text, 0.0f, 0,
		0, FONT_MEDIUM );
	CG_FillRect( 438.0f, y + 13.0f, 170.0f, 12.0f, track );
	CG_FillRect( 438.0f, y + 13.0f, 170.0f * probability, 12.0f, color );
}

static int AF_LessonProbabilityZone( float threshold, int *totalCells,
	float *yawSpan, float *pitchSpan, float *pixelWidth, float *pixelHeight )
{
	qboolean included[AF_ENVELOPE_MAX_CELLS];
	int queue[AF_ENVELOPE_MAX_CELLS];
	int minPitchKey = 999999, maxPitchKey = -999999;
	int minYawKey = 999999, maxYawKey = -999999;
	float minScreenX = 9999.0f, minScreenY = 9999.0f;
	float maxScreenX = -9999.0f, maxScreenY = -9999.0f;
	int head = 0, tail = 0;
	int connectedCells = 0;
	int i;

	*totalCells = 0;
	*yawSpan = *pitchSpan = *pixelWidth = *pixelHeight = 0.0f;
	for ( i = 0; i < af_envelopeCellCount; i++ ) {
		if ( af_envelopeCells[i].probability >= threshold ) {
			( *totalCells )++;
		}
	}
	if ( af_envelopeBestCell < 0 || af_envelopeBestCell >= af_envelopeCellCount ||
		af_envelopeCells[af_envelopeBestCell].probability < threshold ) {
		return 0;
	}

	memset( included, 0, sizeof( included ) );
	included[af_envelopeBestCell] = qtrue;
	queue[tail++] = af_envelopeBestCell;
	while ( head < tail ) {
		const afEnvelopeCell_t *cell = &af_envelopeCells[queue[head++]];
		afEnvelopeCell_t corner;
		int cellIndex = (int)( cell - af_envelopeCells );
		int pitchCorner, yawCorner;

		connectedCells++;
		minPitchKey = cell->pitchKey < minPitchKey ? cell->pitchKey : minPitchKey;
		maxPitchKey = cell->pitchKey > maxPitchKey ? cell->pitchKey : maxPitchKey;
		minYawKey = cell->yawKey < minYawKey ? cell->yawKey : minYawKey;
		maxYawKey = cell->yawKey > maxYawKey ? cell->yawKey : maxYawKey;
		for ( pitchCorner = -1; pitchCorner <= 1; pitchCorner += 2 ) {
			for ( yawCorner = -1; yawCorner <= 1; yawCorner += 2 ) {
				float x, y;
				corner = *cell;
				corner.pitchDelta += pitchCorner * 0.25f;
				corner.yawDelta += yawCorner * 0.25f;
				if ( AF_LessonCellScreen( &corner, &x, &y ) ) {
					minScreenX = fminf( minScreenX, x );
					maxScreenX = fmaxf( maxScreenX, x );
					minScreenY = fminf( minScreenY, y );
					maxScreenY = fmaxf( maxScreenY, y );
				}
			}
		}
		for ( i = 0; i < af_envelopeCellCount; i++ ) {
			const afEnvelopeCell_t *neighbor = &af_envelopeCells[i];
			if ( i == cellIndex || included[i] || neighbor->probability < threshold ) {
				continue;
			}
			if ( abs( neighbor->pitchKey - cell->pitchKey ) <= 1 &&
				abs( neighbor->yawKey - cell->yawKey ) <= 1 ) {
				included[i] = qtrue;
				queue[tail++] = i;
			}
		}
	}

	*yawSpan = ( maxYawKey - minYawKey + 1 ) * 0.5f;
	*pitchSpan = ( maxPitchKey - minPitchKey + 1 ) * 0.5f;
	if ( maxScreenX >= minScreenX && maxScreenY >= minScreenY ) {
		*pixelWidth = ( maxScreenX - minScreenX ) *
			cgs.glconfig.vidWidth / 640.0f;
		*pixelHeight = ( maxScreenY - minScreenY ) *
			cgs.glconfig.vidHeight / 480.0f;
	}
	return connectedCells;
}

static void AF_DrawLessonProbabilityAnalysis( void )
{
	vec4_t background = { 0.015f, 0.02f, 0.035f, 1.0f };
	vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
	vec4_t muted = { 0.68f, 0.72f, 0.78f, 1.0f };
	vec4_t cyan = { 0.05f, 0.85f, 1.0f, 1.0f };
	vec4_t green = { 0.10f, 1.0f, 0.22f, 1.0f };
	vec4_t yellow = { 1.0f, 0.78f, 0.10f, 1.0f };
	vec4_t red = { 1.0f, 0.22f, 0.18f, 1.0f };
	vec4_t frame = { 0.35f, 0.38f, 0.44f, 0.75f };
	float minX = 9999.0f, minY = 9999.0f;
	float maxX = -9999.0f, maxY = -9999.0f;
	float scale, offsetX, offsetY;
	float plotX = 34.0f, plotY = 78.0f, plotW = 370.0f, plotH = 286.0f;
	float bestAtLeastOne, single, both, exactlyOne, neither;
	float usableYawSpan, usablePitchSpan, usablePixelWidth, usablePixelHeight;
	int usableTotalCells, usableConnectedCells;
	char text[160];
	int i;

	CG_FillRect( 0.0f, 0.0f, 640.0f, 480.0f, background );
	CG_Text_Paint( 28.0f, 36.0f, 0.78f, white,
		"TWO-BALL AIM PROBABILITY", 0.0f, 0, 0, FONT_MEDIUM );
	CG_Text_Paint( 28.0f, 57.0f, 0.48f, muted,
		"brightness = chance at least one ball directly hits", 0.0f, 0,
		0, FONT_MEDIUM );

	if ( af_envelopeCellCount <= 0 || af_envelopeBestCell < 0 || !cg.snap ) {
		CG_Text_Paint( 230.0f, 240.0f, 0.62f, red,
			"NO VALID AIM CELLS", 0.0f, 0, 0, FONT_MEDIUM );
		return;
	}

	for ( i = 0; i < af_envelopeCellCount; i++ ) {
		float x, y;
		if ( !AF_LessonCellScreen( &af_envelopeCells[i], &x, &y ) ) {
			continue;
		}
		minX = fminf( minX, x );
		maxX = fmaxf( maxX, x );
		minY = fminf( minY, y );
		maxY = fmaxf( maxY, y );
	}
	minX -= 10.0f;
	maxX += 10.0f;
	minY -= 10.0f;
	maxY += 10.0f;
	scale = fminf( plotW / fmaxf( 1.0f, maxX - minX ),
		plotH / fmaxf( 1.0f, maxY - minY ) );
	offsetX = plotX + ( plotW - ( maxX - minX ) * scale ) * 0.5f;
	offsetY = plotY + ( plotH - ( maxY - minY ) * scale ) * 0.5f;

	CG_DrawRect( plotX, plotY, plotW, plotH, 1.0f, frame );
	for ( i = 0; i < af_envelopeCellCount; i++ ) {
		const afEnvelopeCell_t *cell = &af_envelopeCells[i];
		vec4_t color;
		float x, y, drawX, drawY;
		float size = fmaxf( 2.5f, 2.2f * scale );
		if ( !AF_LessonCellScreen( cell, &x, &y ) ) {
			continue;
		}
		drawX = offsetX + ( x - minX ) * scale;
		drawY = offsetY + ( y - minY ) * scale;
		AF_SetColor( color, 0.08f, 1.0f, 0.18f,
			0.08f + 0.90f * cell->probability );
		CG_FillRect( drawX - size * 0.5f, drawY - size * 0.5f,
			size, size, color );
	}

	{
		const afEnvelopeCell_t *best = &af_envelopeCells[af_envelopeBestCell];
		float x, y;
		if ( AF_LessonCellScreen( best, &x, &y ) ) {
			float bestX = offsetX + ( x - minX ) * scale;
			float bestY = offsetY + ( y - minY ) * scale;
			AF_DrawMarker( bestX, bestY, 8.0f, green );
			CG_Text_Paint( bestX + 8.0f, bestY - 7.0f, 0.42f, white,
				"BEST", 0.0f, 0, 0, FONT_MEDIUM );
		}
	}

	CG_Text_Paint( 34.0f, 384.0f, 0.42f, white,
		"TWO-BALL DIRECT-HIT CHANCE", 0.0f, 0, 0, FONT_MEDIUM );
	for ( i = 0; i < 5; i++ ) {
		vec4_t color;
		float probability = ( i + 1 ) * 0.10f;
		AF_SetColor( color, 0.08f, 1.0f, 0.18f, 0.08f + 0.90f * probability );
		CG_FillRect( 34.0f + i * 72.0f, 395.0f, 54.0f, 13.0f, color );
		Com_sprintf( text, sizeof( text ), "%i%%", ( i + 1 ) * 10 );
		CG_Text_Paint( 47.0f + i * 72.0f, 425.0f, 0.38f, muted,
			text, 0.0f, 0, 0, FONT_MEDIUM );
	}
	CG_Text_Paint( 35.0f, 455.0f, 0.37f, cyan,
		"plot enlarged; tolerance at right uses actual FOV 100 pixels", 0.0f, 0,
		0, FONT_MEDIUM );

	bestAtLeastOne = af_envelopeCells[af_envelopeBestCell].probability;
	single = 1.0f - sqrtf( fmaxf( 0.0f, 1.0f - bestAtLeastOne ) );
	both = single * single;
	exactlyOne = 2.0f * single * ( 1.0f - single );
	neither = ( 1.0f - single ) * ( 1.0f - single );
	Com_sprintf( text, sizeof( text ), "BEST AIM: %.1f%% AT LEAST ONE HIT",
		bestAtLeastOne * 100.0f );
	CG_Text_Paint( 430.0f, 92.0f, 0.54f, green, text, 0.0f, 0,
		0, FONT_MEDIUM );
	AF_DrawLessonOutcomeBar( 132.0f, "BOTH HIT", both, green );
	AF_DrawLessonOutcomeBar( 190.0f, "EXACTLY ONE", exactlyOne, yellow );
	AF_DrawLessonOutcomeBar( 248.0f, "NEITHER", neither, red );
	Com_sprintf( text, sizeof( text ), "ONE BALL ALONE: %.1f%% HIT",
		single * 100.0f );
	CG_Text_Paint( 438.0f, 326.0f, 0.46f, cyan, text, 0.0f, 0,
		0, FONT_MEDIUM );
	CG_Text_Paint( 438.0f, 350.0f, 0.40f, muted,
		"sampled rolls; balls treated as independent", 0.0f, 0,
		0, FONT_MEDIUM );

	usableConnectedCells = AF_LessonProbabilityZone( 0.40f, &usableTotalCells,
		&usableYawSpan, &usablePitchSpan, &usablePixelWidth, &usablePixelHeight );
	if ( usableConnectedCells > 0 ) {
		Com_sprintf( text, sizeof( text ), "40%%+ AREA: %.2f SQ DEG (%i CELLS)",
			usableTotalCells * 0.25f, usableTotalCells );
		CG_Text_Paint( 430.0f, 389.0f, 0.36f, green, text, 0.0f, 0,
			0, FONT_MEDIUM );
		Com_sprintf( text, sizeof( text ), "CONNECTED AT BEST: %.1f x %.1f DEG",
			usableYawSpan, usablePitchSpan );
		CG_Text_Paint( 430.0f, 412.0f, 0.38f, white, text, 0.0f, 0,
			0, FONT_MEDIUM );
		Com_sprintf( text, sizeof( text ), "AIM ROOM: ~%.0f x %.0f SCREEN PIXELS",
			usablePixelWidth, usablePixelHeight );
		CG_Text_Paint( 430.0f, 434.0f, 0.38f, cyan, text, 0.0f, 0,
			0, FONT_MEDIUM );
	} else {
		CG_Text_Paint( 430.0f, 397.0f, 0.43f, red,
			"40%+ AIM AREA: NONE", 0.0f, 0, 0, FONT_MEDIUM );
		CG_Text_Paint( 430.0f, 423.0f, 0.38f, muted,
			"best sampled point is below 40%", 0.0f, 0, 0, FONT_MEDIUM );
	}
}

static const char *AF_LessonStageTitle( int stage )
{
	switch ( stage ) {
	case 1:  return "REAL FRAME: ENEMY FLAG CARRIER";
	case 2:  return "ROCKET BASELINE: FIXED 900 UPS, NO GRAVITY";
	case 3:  return "CHANGE SPEED ONLY: FIXED 1050 UPS, STRAIGHT";
	case 4:  return "GOLAN BASELINE: GRAVITY + FIXED 10 DEG LIFT";
	case 5:  return "REAL GOLAN HEATMAP: ONE BALL";
	case 6:  return "REAL GOLAN HEATMAP: AT LEAST ONE OF TWO";
	default: return "TWO-BALL AIM PROBABILITY";
	}
}

static const char *AF_LessonStageDetail( int stage )
{
	switch ( stage ) {
	case 1:  return "cyan arrow = immediate velocity; cyan path = recorded future";
	case 2:  return "green = every tested aim direction that directly hits";
	case 3:  return "faster projectile needs less lead";
	case 4:  return "fixed 1050 ups; gravity and the launcher's 10 deg lift mostly offset";
	case 5:  return "real randomness; every shown aim can directly hit with one ball";
	case 6:  return "two independent balls raise the chance that at least one directly hits";
	default: return "normal crosshair stays gray; validated counterfactual field is green";
	}
}

static void AF_DrawLesson( void )
{
	vec4_t panel = { 0.0f, 0.0f, 0.0f, 0.72f };
	vec4_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
	vec4_t cyan = { 0.10f, 0.90f, 1.0f, 1.0f };
	char metrics[256];
	char detail[256];
	char candidateTitle[128];
	const char *stageTitle = AF_LessonStageTitle( af_lessonStage );
	const char *detailText = AF_LessonStageDetail( af_lessonStage );

	if ( !af_lessonFrozen || af_lessonStage <= 0 ) {
		return;
	}
	if ( af_lessonStage == 7 ) {
		AF_DrawLessonProbabilityAnalysis();
		return;
	}
	if ( af_lessonStage >= 2 && af_lessonStage <= 6 ) {
		AF_DrawEnvelope();
	}
	AF_DrawLessonFuturePath();
	AF_DrawLessonVelocityArrow();
	if ( cg_aimFeedbackLesson.integer == 2 ) {
		Com_sprintf( candidateTitle, sizeof( candidateTitle ),
			"ROCKET AIM %02i/%02i: 900 UPS, DIRECT HIT, NO SPLASH",
			af_candidateIndex + 1, AF_CANDIDATE_COUNT );
		stageTitle = candidateTitle;
		Com_sprintf( detail, sizeof( detail ),
			"time %i; cyan = recorded target path to intercept; green = rocket hit field",
			af_candidateTimes[af_candidateIndex] );
		if ( af_envelopeCellCount <= 0 ) {
			Com_sprintf( detail, sizeof( detail ),
				"time %i; no unobstructed 900-ups direct intercept on recorded path",
				af_candidateTimes[af_candidateIndex] );
		}
		detailText = detail;
	}

	CG_FillRect( 8.0f, 62.0f, 475.0f, 58.0f, panel );
	CG_Text_Paint( 15.0f, 77.0f, 0.62f, white,
		stageTitle, 0.0f, 0,
		0, FONT_MEDIUM );
	if ( cg_aimFeedbackLesson.integer == 2 ) {
		Com_sprintf( metrics, sizeof( metrics ),
			"RANGE %.0fu   TARGET %.0f ups   LATERAL %.0f   VERTICAL %+.0f",
			af_lessonDistance, af_lessonTargetSpeed, af_lessonLateralSpeed,
			af_lessonTargetVelocity[2] );
	} else {
		Com_sprintf( metrics, sizeof( metrics ),
			"RANGE %.0fu   TARGET %.0f ups   LATERAL %.0f   AWAY FROM FLAG %.0f",
			af_lessonDistance, af_lessonTargetSpeed, af_lessonLateralSpeed,
			af_lessonAwaySpeed );
	}
	CG_Text_Paint( 15.0f, 94.0f, 0.58f, cyan, metrics, 0.0f, 0,
		0, FONT_MEDIUM );
	if ( af_lessonStage >= 5 && af_envelopeBestCell >= 0 &&
		af_envelopeBestCell < af_envelopeCellCount ) {
		float bestPercent = af_envelopeCells[af_envelopeBestCell].probability * 100.0f;
		if ( af_lessonStage == 5 ) {
			Com_sprintf( detail, sizeof( detail ),
				"every shown aim can hit if randomness works out; best sampled %.0f%%",
				bestPercent );
		} else {
			Com_sprintf( detail, sizeof( detail ),
				"brightness = chance at least one of two hits; best sampled %.0f%%",
				bestPercent );
		}
		detailText = detail;
	} else if ( af_lessonStage >= 2 && af_lessonStage <= 4 &&
		af_envelopeCellCount <= 0 ) {
		detailText = "no direct-hit aim found for this fixed projectile model";
	} else if ( af_lessonStage >= 5 && af_lessonStage <= 6 &&
		af_envelopeCellCount <= 0 ) {
		detailText = "no tested aim produced a direct hit in the sampled random rolls";
	}
	CG_Text_Paint( 15.0f, 111.0f, 0.52f, white,
		detailText, 0.0f, 0,
		0, FONT_MEDIUM );
}

static void AF_DrawReviewCorrection( void )
{
	afReviewEntry_t *entry;
	vec4_t black = { 0.0f, 0.0f, 0.0f, 1.0f };
	vec4_t green = { 0.15f, 1.0f, 0.25f, 1.0f };
	vec4_t yellow = { 1.0f, 0.78f, 0.08f, 1.0f };
	vec4_t cyan = { 0.1f, 0.82f, 1.0f, 1.0f };
	qboolean showCorrection;

	if ( !af_reviewInitialized || af_reviewCount <= 0 ) {
		return;
	}
	if ( af_reviewState == AF_REVIEW_TRANSITION || af_reviewState == AF_REVIEW_DONE ) {
		CG_FillRect( 0.0f, 0.0f, 640.0f, 480.0f, black );
		return;
	}
	if ( af_reviewIndex >= af_reviewCount ) {
		return;
	}

	entry = &af_reviewEntries[af_reviewIndex];
	showCorrection = af_reviewState == AF_REVIEW_HOLD ||
		( af_reviewState == AF_REVIEW_PREROLL && cg.time >= entry->shotTime - 120 ) ||
		af_reviewState == AF_REVIEW_POSTROLL;
	if ( !showCorrection ) {
		return;
	}

	if ( entry->weaponClass == AF_WEAPON_GOLAN_ALT ) {
		if ( entry->bestProbability > 0.0f ) {
			AF_DrawCorrectionCrosshair( entry->bestPitchDelta,
				entry->bestYawDelta, 11.0f, green );
		}
		if ( entry->realizedFound ) {
			AF_DrawCorrectionCrosshair( entry->realizedPitchDelta,
				entry->realizedYawDelta, 7.0f, cyan );
		}
		return;
	}

	if ( entry->bodyFound ) {
		AF_DrawCorrectionCrosshair( entry->bodyPitchDelta,
			entry->bodyYawDelta, 11.0f, green );
	}
	if ( entry->headFound ) {
		AF_DrawCorrectionCrosshair( entry->headPitchDelta,
			entry->headYawDelta, 7.0f, yellow );
	}
}

void CG_AimFeedback_Draw( void )
{
	afShot_t *shot;
	vec4_t green = { 0.15f, 1.0f, 0.25f, 1.0f };
	vec4_t yellow = { 1.0f, 0.78f, 0.08f, 1.0f };
	vec4_t cyan = { 0.1f, 0.82f, 1.0f, 1.0f };
	vec4_t black = { 0.0f, 0.0f, 0.0f, 1.0f };
	int age;
	float alpha;

	if ( !cg.demoPlayback || !cg_aimFeedback.integer ) {
		return;
	}
	if ( cg_aimFeedbackLesson.integer ) {
		AF_DrawLesson();
		return;
	}
	if ( cg_aimFeedbackEnvelope.integer == 2 ) {
		AF_DrawEnvelope();
		return;
	}
	if ( cg_aimFeedbackEnvelope.integer == 1 ) {
		return;
	}
	if ( cg_aimFeedbackReview.integer == 2 ) {
		AF_DrawReviewCorrection();
		return;
	}
	if ( cg_aimFeedbackReview.integer == 3 || cg_aimFeedbackReview.integer == 4 ) {
		CG_FillRect( 0.0f, 0.0f, 640.0f, 480.0f, black );
		return;
	}
	if ( af_latestShot < 0 ) {
		return;
	}
	shot = AF_GetDisplayShot();
	if ( !shot || !shot->active || !shot->result.ready || shot->result.readyTime <= 0 ) {
		return;
	}

	age = cg.time - shot->result.readyTime;
	if ( age < 0 || age > AF_RESULT_DISPLAY_MSEC ) {
		return;
	}
	alpha = 1.0f - AF_ClampFloat( ( age - 1100 ) / 700.0f, 0.0f, 1.0f );
	green[3] = yellow[3] = cyan[3] = alpha;

	if ( shot->weaponClass == AF_WEAPON_GOLAN_ALT ) {
		if ( shot->result.bestProbability > 0.0f ) {
			AF_DrawCorrectionCrosshair( shot->result.bestPitchDelta,
				shot->result.bestYawDelta, 11.0f, green );
		}
		if ( shot->result.realized.found ) {
			AF_DrawCorrectionCrosshair( shot->result.realized.pitchDelta,
				shot->result.realized.yawDelta, 7.0f, cyan );
		}
		return;
	}

	if ( shot->result.body.found ) {
		AF_DrawCorrectionCrosshair( shot->result.body.pitchDelta,
			shot->result.body.yawDelta, 11.0f, green );
	}
	if ( shot->result.head.found ) {
		AF_DrawCorrectionCrosshair( shot->result.head.pitchDelta,
			shot->result.head.yawDelta, 7.0f, yellow );
	}
}

/* cl_freezeDemo intentionally pauses the shot frame during an aim review.
   That is not a network interruption and should not obscure the evidence. */
qboolean CG_AimFeedback_HidesDisconnect( void )
{
	return ( cg.demoPlayback && ( cg_aimFeedbackReview.integer >= 2 ||
		cg_aimFeedbackLesson.integer ) ) ? qtrue : qfalse;
}

/* The lesson draws its own captions over the frozen frame. */
qboolean CG_AimFeedback_HidesCenterPrint( void )
{
	return cg_aimFeedbackLesson.integer ? qtrue : qfalse;
}
