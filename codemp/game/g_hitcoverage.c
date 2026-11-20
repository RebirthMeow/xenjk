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
===========================================================================
Direct-hit coverage laboratory for multiplayer projectile traces.
===========================================================================
*/

#include "g_local.h"
#include "ghoul2/G2.h"
#include "g_hitcoverage.h"

#define HITCOV_HORIZONTAL_MIN (-30.0f)
#define HITCOV_HORIZONTAL_MAX ( 30.0f)
#define HITCOV_VERTICAL_MIN   (-40.0f)
#define HITCOV_VERTICAL_MAX   ( 50.0f)
#define HITCOV_TRACE_DISTANCE ( 96.0f)
#define HITCOV_Q3_AREA        (1680.0f) /* 30 wide x 56 high */
#define HITCOV_MAX_COLUMNS    240

static void HitCoverageUsage( void ) {
	trap->Print(
		"Usage: hitcoverage <clientnum> <g2:0|1> <lod:0-3> "
		"<view-yaw> [step:0.25-4] [map:0|1] "
		"[pose:live|stand|jumpapex] [trace:parallel|camera] "
		"[distance:64-4096] [rocket-radius:0-16] [bounds:ja|q3]\n"
		"Samples the multiplayer projectile trace against the target. Camera "
		"mode fires divergent traces from one fixed source to the aim plane.\n"
	);
}

static qboolean HitCoverageSetPose( gentity_t *target, const char *pose,
	int *legsFrameOut, int *torsoFrameOut ) {
	static const char *angleBones[] = {
		"model_root", "lower_lumbar", "upper_lumbar", "thoracic",
		"cervical", "cranium"
	};
	animation_t *standAnim;
	animation_t *legsAnim;
	int legsFrame, torsoFrame;
	int i;
	int flags = BONE_ANIM_OVERRIDE_FREEZE;

	if ( !Q_stricmp( pose, "live" ) ) {
		*legsFrameOut = -1;
		*torsoFrameOut = -1;
		return qtrue;
	}
	if ( !target->ghoul2 ) {
		trap->Print( "hitcoverage: target has no server Ghoul2 instance\n" );
		return qfalse;
	}

	standAnim = &bgAllAnims[target->localAnimIndex].anims[BOTH_STAND2];
	torsoFrame = standAnim->firstFrame;
	if ( !Q_stricmp( pose, "stand" ) ) {
		legsFrame = torsoFrame;
	}
	else if ( !Q_stricmp( pose, "jumpapex" ) ) {
		/*
		 * A level-0/non-force jump reaches its physical apex after the short
		 * BOTH_JUMP1 takeoff animation has already frozen on its last frame.
		 * The torso remains in the saber-ready stance because PM_JumpForDir
		 * changes SETANIM_LEGS only.
		 */
		legsAnim = &bgAllAnims[target->localAnimIndex].anims[BOTH_JUMP1];
		legsFrame = legsAnim->firstFrame + legsAnim->numFrames - 1;
	}
	else {
		trap->Print( "hitcoverage: pose must be live, stand, or jumpapex\n" );
		return qfalse;
	}

	trap->G2API_SetBoneAnim( target->ghoul2, 0, "model_root",
		legsFrame, legsFrame + 1, flags, 1.0f, level.time, legsFrame, 0 );
	trap->G2API_SetBoneAnim( target->ghoul2, 0, "lower_lumbar",
		torsoFrame, torsoFrame + 1, flags, 1.0f, level.time, torsoFrame, 0 );
	trap->G2API_SetBoneAnim( target->ghoul2, 0, "Motion",
		torsoFrame, torsoFrame + 1, flags, 1.0f, level.time, torsoFrame, 0 );

	/* Clear live look/turn/spine overrides so the named lab pose is exact. */
	for ( i = 0; i < (int)ARRAY_LEN( angleBones ); ++i ) {
		trap->G2API_SetBoneAngles( target->ghoul2, 0, angleBones[i], vec3_origin,
			BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z,
			NULL, 0, level.time );
	}

	*legsFrameOut = legsFrame;
	*torsoFrameOut = torsoFrame;
	return qtrue;
}

void Svcmd_HitCoverage_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	gentity_t *target;
	vec3_t forward, right;
	vec3_t rocketMins;
	vec3_t rocketMaxs;
	vec3_t cameraOrigin;
	float viewYaw, worldYaw, step = 1.0f;
	float cameraDistance = 512.0f;
	float rocketRadius = 3.0f;
	int clientNum, useG2, lod, emitMap = 1;
	int poseLegsFrame = -1, poseTorsoFrame = -1;
	int columns, rows, row, column;
	int hits = 0, blocked = 0;
	int traceFlags = 0;
	qboolean overrideBounds;
	vec3_t savedMins, savedMaxs;
	vec3_t savedModelAngles;
	char bitmap[HITCOV_MAX_COLUMNS + 1];
	char pose[32] = "live";
	char traceMode[32] = "parallel";
	char boundsMode[32] = "ja";
	qboolean cameraTrace = qfalse;

	if ( trap->Argc() < 5 ) {
		HitCoverageUsage();
		return;
	}

	trap->Argv( 1, arg, sizeof( arg ) );
	clientNum = atoi( arg );
	trap->Argv( 2, arg, sizeof( arg ) );
	useG2 = atoi( arg ) != 0;
	trap->Argv( 3, arg, sizeof( arg ) );
	lod = atoi( arg );
	trap->Argv( 4, arg, sizeof( arg ) );
	viewYaw = (float)atof( arg );

	if ( trap->Argc() >= 6 ) {
		trap->Argv( 5, arg, sizeof( arg ) );
		step = (float)atof( arg );
	}
	if ( trap->Argc() >= 7 ) {
		trap->Argv( 6, arg, sizeof( arg ) );
		emitMap = atoi( arg ) != 0;
	}
	if ( trap->Argc() >= 8 ) {
		trap->Argv( 7, pose, sizeof( pose ) );
	}
	if ( trap->Argc() >= 9 ) {
		trap->Argv( 8, traceMode, sizeof( traceMode ) );
	}
	if ( trap->Argc() >= 10 ) {
		trap->Argv( 9, arg, sizeof( arg ) );
		cameraDistance = (float)atof( arg );
	}
	if ( trap->Argc() >= 11 ) {
		trap->Argv( 10, arg, sizeof( arg ) );
		rocketRadius = (float)atof( arg );
	}
	if ( trap->Argc() >= 12 ) {
		trap->Argv( 11, boundsMode, sizeof( boundsMode ) );
	}

	if ( clientNum < 0 || clientNum >= level.maxclients ||
		 !g_entities[clientNum].inuse || !g_entities[clientNum].client ) {
		trap->Print( "hitcoverage: target must be an active client number\n" );
		return;
	}
	if ( lod < 0 || lod > 3 ) {
		trap->Print( "hitcoverage: lod must be between 0 and 3\n" );
		return;
	}
	if ( step < 0.25f || step > 4.0f ) {
		trap->Print( "hitcoverage: step must be between 0.25 and 4\n" );
		return;
	}
	if ( !Q_stricmp( traceMode, "camera" ) ) {
		cameraTrace = qtrue;
	}
	else if ( Q_stricmp( traceMode, "parallel" ) ) {
		trap->Print( "hitcoverage: trace must be parallel or camera\n" );
		return;
	}
	if ( cameraDistance < 64.0f || cameraDistance > 4096.0f ) {
		trap->Print( "hitcoverage: distance must be between 64 and 4096\n" );
		return;
	}
	if ( rocketRadius < 0.0f || rocketRadius > 16.0f ) {
		trap->Print( "hitcoverage: rocket radius must be between 0 and 16\n" );
		return;
	}
	if ( Q_stricmp( boundsMode, "ja" ) && Q_stricmp( boundsMode, "q3" ) ) {
		trap->Print( "hitcoverage: bounds must be ja or q3\n" );
		return;
	}
	VectorSet( rocketMins, -rocketRadius, -rocketRadius, -rocketRadius );
	VectorSet( rocketMaxs,  rocketRadius,  rocketRadius,  rocketRadius );

	target = &g_entities[clientNum];
	if ( !HitCoverageSetPose( target, pose, &poseLegsFrame, &poseTorsoFrame ) ) {
		return;
	}
	overrideBounds = Q_stricmp( pose, "live" ) != 0 ||
		!Q_stricmp( boundsMode, "q3" );
	VectorCopy( target->r.mins, savedMins );
	VectorCopy( target->r.maxs, savedMaxs );
	VectorCopy( target->s.apos.trBase, savedModelAngles );
	if ( overrideBounds ) {
		/*
		 * Both standing and ordinary airborne players use the full MP bbox.
		 * Fix the mannequin at compass yaw 0 as well: the rocket cube remains
		 * world-axis-aligned, so otherwise absolute compass heading is another
		 * experimental variable even when view yaw is model-relative.
		 */
		trap->UnlinkEntity( (sharedEntity_t *)target );
		VectorSet( target->r.mins, -15.0f, -15.0f, DEFAULT_MINS_2 );
		VectorSet( target->r.maxs,  15.0f,  15.0f,
			!Q_stricmp( boundsMode, "q3" ) ? 32.0f : DEFAULT_MAXS_2 );
		VectorClear( target->s.apos.trBase );
		trap->LinkEntity( (sharedEntity_t *)target );
	}
	columns = (int)ceilf( (HITCOV_HORIZONTAL_MAX - HITCOV_HORIZONTAL_MIN) / step );
	rows = (int)ceilf( (HITCOV_VERTICAL_MAX - HITCOV_VERTICAL_MIN) / step );
	if ( columns > HITCOV_MAX_COLUMNS ) {
		trap->Print( "hitcoverage: selected step produces too many columns\n" );
		return;
	}

	/* View yaw is relative to the model's actual server collision yaw. */
	worldYaw = target->s.apos.trBase[YAW] + viewYaw;
	worldYaw *= (float)(M_PI * 2.0 / 360.0);
	VectorSet( forward, cosf( worldYaw ), sinf( worldYaw ), 0.0f );
	VectorSet( right, -forward[1], forward[0], 0.0f );
	VectorMA( target->r.currentOrigin, -cameraDistance, forward, cameraOrigin );
	if ( useG2 ) {
		traceFlags = G2TRFLAG_DOGHOULTRACE | G2TRFLAG_GETSURFINDEX |
			G2TRFLAG_THICK | G2TRFLAG_HITCORPSES;
	}

	trap->Print(
		"HITCOV_BEGIN client=%d name=\"%s\" g2=%d lod=%d viewYaw=%.3f "
		"modelYaw=%.3f pose=%s poseLegsFrame=%d poseTorsoFrame=%d "
		"trace=%s distance=%.3f rocketRadius=%.3f bounds=%s "
		"step=%.3f cols=%d rows=%d origin=%.3f,%.3f,%.3f "
		"camera=%.3f,%.3f,%.3f "
		"mins=%.3f,%.3f,%.3f maxs=%.3f,%.3f,%.3f legsAnim=%d "
		"legsTimer=%d torsoAnim=%d torsoTimer=%d\n",
		clientNum, target->client->pers.netname, useG2, lod, viewYaw,
		target->s.apos.trBase[YAW], pose, poseLegsFrame, poseTorsoFrame,
		traceMode, cameraDistance, rocketRadius, boundsMode,
		step, columns, rows,
		target->r.currentOrigin[0], target->r.currentOrigin[1],
		target->r.currentOrigin[2],
		cameraOrigin[0], cameraOrigin[1], cameraOrigin[2],
		target->r.mins[0], target->r.mins[1], target->r.mins[2],
		target->r.maxs[0], target->r.maxs[1], target->r.maxs[2],
		target->client->ps.legsAnim,
		target->client->ps.legsTimer, target->client->ps.torsoAnim,
		target->client->ps.torsoTimer );

	for ( row = rows - 1; row >= 0; --row ) {
		float vertical = HITCOV_VERTICAL_MIN + ((float)row + 0.5f) * step;

		for ( column = 0; column < columns; ++column ) {
			float horizontal = HITCOV_HORIZONTAL_MIN + ((float)column + 0.5f) * step;
			vec3_t aim, start, end;
			trace_t tr;

			VectorMA( target->r.currentOrigin, horizontal, right, aim );
			aim[2] += vertical;
			if ( cameraTrace ) {
				vec3_t ray;

				VectorSubtract( aim, cameraOrigin, ray );
				VectorNormalize( ray );
				VectorCopy( cameraOrigin, start );
				VectorMA( aim, HITCOV_TRACE_DISTANCE, ray, end );
			}
			else {
				VectorMA( aim, -HITCOV_TRACE_DISTANCE, forward, start );
				VectorMA( aim,  HITCOV_TRACE_DISTANCE, forward, end );
			}

			/*
			 * CONTENTS_BODY/CORPSE intentionally excludes map brushes.  This is
			 * the actual player collision path, isolated from spawn-position
			 * walls and floors that are irrelevant to target coverage.
			 */
			trap->Trace( &tr, start, rocketMins, rocketMaxs, end,
				ENTITYNUM_NONE, CONTENTS_BODY | CONTENTS_CORPSE, qfalse,
				traceFlags, useG2 ? lod : 0 );

			if ( tr.entityNum == clientNum ) {
				bitmap[column] = '#';
				++hits;
			}
			else {
				bitmap[column] = '.';
				if ( tr.fraction < 1.0f ) {
					++blocked;
				}
			}
		}
		bitmap[columns] = '\0';
		if ( emitMap ) {
			trap->Print( "HITCOV_ROW z=%.3f %s\n", vertical, bitmap );
		}
	}

	trap->Print(
		"HITCOV_END hits=%d samples=%d blockers=%d area=%.3f "
		"q3_percent=%.3f\n",
		hits, columns * rows, blocked, hits * step * step,
		(hits * step * step / HITCOV_Q3_AREA) * 100.0f );

	if ( overrideBounds ) {
		trap->UnlinkEntity( (sharedEntity_t *)target );
		VectorCopy( savedMins, target->r.mins );
		VectorCopy( savedMaxs, target->r.maxs );
		VectorCopy( savedModelAngles, target->s.apos.trBase );
		trap->LinkEntity( (sharedEntity_t *)target );
	}
}
