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
Local saber damage benchmark support.

This command deliberately drives only test setup. Attacks still arrive as
ordinary client usercmds and pass through the production pmove, animation,
Ghoul2 collision, saber trace, armor, and Force Protect paths.
===========================================================================
*/

#include "g_local.h"
#include "g_saberbench.h"

extern qboolean G_SetSaber( gentity_t *ent, int saberNum, char *saberName,
	qboolean siegeOverride );
extern qboolean G_SaberModelSetup( gentity_t *ent );
extern void Bot_SetForcedMovement( int bot, int forward, int right, int up );

static int saberBenchTargetNum = -1;
static int saberBenchTargetConnectTime;
static vec3_t saberBenchTargetOrigin;
static vec3_t saberBenchTargetAngles;
static qboolean saberBenchAnchorValid = qfalse;
static vec3_t saberBenchAttackerAnchor;
static qboolean saberBenchGeometryValid = qfalse;
static vec3_t saberBenchAttackerAngles;

/* Releases the pinned target and forgets the placement (`saberbench clear`, game shutdown). */
void G_SaberBenchShutdown( void )
{
	saberBenchTargetNum = -1;
	saberBenchAnchorValid = qfalse;
	saberBenchGeometryValid = qfalse;
}

void G_SaberBenchFrame( void )
{
	gentity_t *target;

	if ( saberBenchTargetNum < 0 || saberBenchTargetNum >= level.maxclients )
	{
		return;
	}
	target = &g_entities[saberBenchTargetNum];
	if ( !target->inuse || !target->client ||
		target->client->pers.connected != CON_CONNECTED ||
		target->client->pers.connectTime != saberBenchTargetConnectTime )
	{
		/* The client it was set on left the slot: release it. */
		saberBenchTargetNum = -1;
		return;
	}
	if ( target->health <= 0 )
	{
		return;
	}
	memset( &target->client->pers.cmd, 0, sizeof( target->client->pers.cmd ) );
	VectorCopy( saberBenchTargetOrigin, target->client->ps.origin );
	VectorCopy( saberBenchTargetOrigin, target->r.currentOrigin );
	VectorCopy( saberBenchTargetAngles, target->client->ps.viewangles );
	VectorClear( target->client->ps.velocity );
	target->client->ps.weapon = WP_BLASTER;
	target->client->ps.weaponstate = WEAPON_READY;
	target->client->ps.weaponTime = 0;
	target->client->ps.saberHolstered = 2;
	target->client->ps.saberMove = LS_READY;
	trap->LinkEntity( (sharedEntity_t *)target );
}

static qboolean SaberBenchClient( int clientNum, gentity_t **out )
{
	if ( clientNum < 0 || clientNum >= level.maxclients ||
		!g_entities[clientNum].inuse || !g_entities[clientNum].client ||
		g_entities[clientNum].client->pers.connected != CON_CONNECTED )
	{
		trap->Print( "saberbench: client %d is not active\n", clientNum );
		return qfalse;
	}
	*out = &g_entities[clientNum];
	return qtrue;
}

static void SaberBenchEquipStyle( gentity_t *attacker, int style )
{
	char single[] = "single_1";
	char staff[] = "dual_1";
	char none[] = "none";

	if ( style == SS_DUAL )
	{
		G_SetSaber( attacker, 0, single, qtrue );
		G_SetSaber( attacker, 1, single, qtrue );
	}
	else if ( style == SS_STAFF )
	{
		G_SetSaber( attacker, 0, staff, qtrue );
		G_SetSaber( attacker, 1, none, qtrue );
	}
	else
	{
		G_SetSaber( attacker, 0, single, qtrue );
		G_SetSaber( attacker, 1, none, qtrue );
	}
	G_SaberModelSetup( attacker );

	attacker->client->ps.weapon = WP_SABER;
	attacker->client->ps.weaponstate = WEAPON_READY;
	attacker->client->ps.weaponTime = 0;
	attacker->client->ps.saberHolstered = 0;
	attacker->client->ps.saberMove = LS_READY;
	attacker->client->ps.saberBlocked = BLOCKED_NONE;
	attacker->client->ps.saberAttackChainCount = 0;
	attacker->client->ps.fd.saberAnimLevel = style;
	attacker->client->ps.fd.saberAnimLevelBase = style;
	attacker->client->ps.fd.saberDrawAnimLevel = style;
	attacker->client->saberCycleQueue = style;
	attacker->client->sess.saberLevel = style;
}

static void SaberBenchSetup( void )
{
	char arg[MAX_TOKEN_CHARS];
	gentity_t *attacker, *target;
	vec3_t attackerOrigin, targetOrigin, attackerAngles, targetAngles;
	float distance;
	int attackerNum, targetNum, health, armor, force, protectLevel, style;
	int directionIndex;
	qboolean foundPlacement = qfalse;

	if ( trap->Argc() != 10 )
	{
		trap->Print( "Usage: saberbench setup <attacker> <target> <distance> "
			"<health> <armor> <force> <protect-level:0-3> "
			"<style:1|2|3|6|7>\n" );
		return;
	}

	trap->Argv( 2, arg, sizeof( arg ) ); attackerNum = atoi( arg );
	trap->Argv( 3, arg, sizeof( arg ) ); targetNum = atoi( arg );
	trap->Argv( 4, arg, sizeof( arg ) ); distance = (float)atof( arg );
	trap->Argv( 5, arg, sizeof( arg ) ); health = atoi( arg );
	trap->Argv( 6, arg, sizeof( arg ) ); armor = atoi( arg );
	trap->Argv( 7, arg, sizeof( arg ) ); force = atoi( arg );
	trap->Argv( 8, arg, sizeof( arg ) ); protectLevel = atoi( arg );
	trap->Argv( 9, arg, sizeof( arg ) ); style = atoi( arg );

	if ( attackerNum == targetNum || distance < 30.0f || distance > 128.0f ||
		health < 1 || armor < 0 || force < 0 || force > 100 ||
		protectLevel < 0 || protectLevel > 3 ||
		!(style == SS_FAST || style == SS_MEDIUM || style == SS_STRONG ||
			style == SS_DUAL || style == SS_STAFF) )
	{
		trap->Print( "saberbench: invalid setup values\n" );
		return;
	}
	if ( !SaberBenchClient( attackerNum, &attacker ) ||
		!SaberBenchClient( targetNum, &target ) )
	{
		return;
	}

	/*
	 * Pin every case to the same floor position.  Movement attacks otherwise
	 * walk the setup around the map and make later cases use different geometry.
	 */
	if ( !saberBenchAnchorValid )
	{
		VectorCopy( attacker->client->ps.origin, saberBenchAttackerAnchor );
		saberBenchAnchorValid = qtrue;
	}
	VectorCopy( saberBenchAttackerAnchor, attackerOrigin );
	if ( saberBenchGeometryValid )
	{
		VectorCopy( saberBenchAttackerAngles, attackerAngles );
		VectorCopy( saberBenchTargetOrigin, targetOrigin );
		VectorCopy( saberBenchTargetAngles, targetAngles );
		foundPlacement = qtrue;
	}
	else
	{
		VectorClear( attackerAngles );
		VectorClear( targetAngles );
		for ( directionIndex = 0; directionIndex < 16; ++directionIndex )
		{
			trace_t placementTrace;
			vec3_t direction;
			float yaw = directionIndex * 22.5f;

			VectorSet( direction, cosf( DEG2RAD( yaw ) ), sinf( DEG2RAD( yaw ) ), 0.0f );
			VectorMA( attackerOrigin, distance, direction, targetOrigin );
			trap->Trace( &placementTrace, attackerOrigin, target->r.mins,
				target->r.maxs, targetOrigin, attackerNum, MASK_PLAYERSOLID,
				qfalse, 0, 0 );
			if ( !placementTrace.startsolid && !placementTrace.allsolid &&
				placementTrace.fraction >= 1.0f )
			{
				attackerAngles[YAW] = yaw;
				targetAngles[YAW] = AngleNormalize360( yaw + 180.0f );
				foundPlacement = qtrue;
				VectorCopy( attackerAngles, saberBenchAttackerAngles );
				VectorCopy( targetOrigin, saberBenchTargetOrigin );
				VectorCopy( targetAngles, saberBenchTargetAngles );
				saberBenchGeometryValid = qtrue;
				break;
			}
		}
	}
	if ( !foundPlacement )
	{
		trap->Print( "saberbench: no clear radial target placement found\n" );
		return;
	}
	/* Setup teleports must not telefrag either participant at the old position. */
	attacker->takedamage = qfalse;
	target->takedamage = qfalse;
	TeleportPlayer( attacker, attackerOrigin, attackerAngles );
	TeleportPlayer( target, targetOrigin, targetAngles );
	attacker->takedamage = qtrue;
	saberBenchTargetNum = targetNum;
	saberBenchTargetConnectTime = target->client->pers.connectTime;
	VectorCopy( targetOrigin, saberBenchTargetOrigin );
	VectorCopy( targetAngles, saberBenchTargetAngles );

	VectorClear( attacker->client->ps.velocity );
	VectorClear( target->client->ps.velocity );
	memset( &target->client->pers.cmd, 0, sizeof( target->client->pers.cmd ) );
	attacker->flags |= FL_NOTARGET;
	target->flags |= FL_NOTARGET;
	if ( target->r.svFlags & SVF_BOT )
	{
		Bot_SetForcedMovement( targetNum, 0, 0, 0 );
	}

	SaberBenchEquipStyle( attacker, style );
	target->health = health;
	target->takedamage = qtrue;
	target->client->ps.stats[STAT_ARMOR] = armor;
	target->client->ps.fd.forcePower = force;
	target->client->ps.fd.forcePowerMax = 100;
	target->client->ps.fd.forcePowerLevel[FP_PROTECT] = protectLevel;
	target->client->ps.fd.forcePowersActive &= ~(1 << FP_PROTECT);
	target->client->ps.fd.forcePowerDuration[FP_PROTECT] = 0;
	if ( protectLevel > 0 )
	{
		target->client->ps.fd.forcePowersActive |= (1 << FP_PROTECT);
		target->client->ps.fd.forcePowerDuration[FP_PROTECT] = level.time + 60000;
		target->client->ps.fd.forcePowerDebounce[FP_PROTECT] = level.time + 60000;
	}
	target->client->ps.weapon = WP_BLASTER;
	target->client->ps.weaponstate = WEAPON_READY;
	target->client->ps.weaponTime = 0;
	target->client->ps.saberHolstered = 2;
	target->client->ps.saberMove = LS_READY;
	trap->LinkEntity( (sharedEntity_t *)attacker );
	trap->LinkEntity( (sharedEntity_t *)target );

	trap->Print( "SABERBENCH_SETUP time=%d attacker=%d target=%d "
		"distance=%.1f health=%d armor=%d force=%d protect=%d style=%d "
		"origin=%.1f,%.1f,%.1f targetOrigin=%.1f,%.1f,%.1f\n",
		level.time, attackerNum, targetNum, distance, health, armor, force,
		protectLevel, style, attackerOrigin[0], attackerOrigin[1],
		attackerOrigin[2], targetOrigin[0], targetOrigin[1], targetOrigin[2] );
}

static void SaberBenchStatus( void )
{
	char arg[MAX_TOKEN_CHARS];
	gentity_t *target;
	int targetNum;

	if ( trap->Argc() != 3 )
	{
		trap->Print( "Usage: saberbench status <target>\n" );
		return;
	}
	trap->Argv( 2, arg, sizeof( arg ) );
	targetNum = atoi( arg );
	if ( !SaberBenchClient( targetNum, &target ) )
	{
		return;
	}
	trap->Print( "SABERBENCH_STATUS time=%d target=%d health=%d armor=%d "
		"force=%d protectActive=%d torsoAnim=%d torsoTimer=%d saberMove=%d "
		"style=%d weapon=%d weaponTime=%d buttons=%d fmove=%d rmove=%d\n",
		level.time, targetNum, target->health,
		target->client->ps.stats[STAT_ARMOR], target->client->ps.fd.forcePower,
		!!(target->client->ps.fd.forcePowersActive & (1 << FP_PROTECT)),
		target->client->ps.torsoAnim, target->client->ps.torsoTimer,
		target->client->ps.saberMove, target->client->ps.fd.saberAnimLevel,
		target->client->ps.weapon, target->client->ps.weaponTime,
		target->client->pers.cmd.buttons, target->client->pers.cmd.forwardmove,
		target->client->pers.cmd.rightmove );
}

static void SaberBenchModel( void )
{
	char arg[MAX_TOKEN_CHARS];
	char model[MAX_QPATH];
	char userinfo[MAX_INFO_STRING];
	gentity_t *target;
	int targetNum;

	if ( trap->Argc() != 4 )
	{
		trap->Print( "Usage: saberbench model <target> <model[/skin]>\n" );
		return;
	}
	trap->Argv( 2, arg, sizeof( arg ) );
	targetNum = atoi( arg );
	trap->Argv( 3, model, sizeof( model ) );
	if ( !SaberBenchClient( targetNum, &target ) )
	{
		return;
	}

	trap->GetUserinfo( targetNum, userinfo, sizeof( userinfo ) );
	Info_SetValueForKey( userinfo, "model", model );
	trap->SetUserinfo( targetNum, userinfo );
	if ( !ClientUserinfoChanged( targetNum ) )
	{
		trap->Print( "saberbench: failed to apply model %s to client %d\n",
			model, targetNum );
		return;
	}
	target->client->torsoAnimExecute = -1;
	target->client->legsAnimExecute = -1;
	trap->Print( "SABERBENCH_MODEL time=%d target=%d model=%s ghoul2=%d "
		"localAnimIndex=%d\n", level.time, targetNum, model,
		!!target->ghoul2, target->localAnimIndex );
}

void Svcmd_SaberBench_f( void )
{
	char subcommand[MAX_TOKEN_CHARS];

	if ( trap->Argc() < 2 )
	{
		trap->Print( "Usage: saberbench <setup|status|model|mark|clear> ...\n" );
		return;
	}
	trap->Argv( 1, subcommand, sizeof( subcommand ) );
	if ( !Q_stricmp( subcommand, "setup" ) )
	{
		SaberBenchSetup();
	}
	else if ( !Q_stricmp( subcommand, "status" ) )
	{
		SaberBenchStatus();
	}
	else if ( !Q_stricmp( subcommand, "model" ) )
	{
		SaberBenchModel();
	}
	else if ( !Q_stricmp( subcommand, "clear" ) )
	{
		G_SaberBenchShutdown();
		trap->Print( "saberbench: target released\n" );
	}
	else if ( !Q_stricmp( subcommand, "mark" ) )
	{
		char label[MAX_TOKEN_CHARS] = "unnamed";
		if ( trap->Argc() >= 3 )
		{
			trap->Argv( 2, label, sizeof( label ) );
		}
		trap->Print( "SABERBENCH_MARK time=%d label=%s\n", level.time, label );
	}
	else
	{
		trap->Print( "Usage: saberbench <setup|status|model|mark|clear> ...\n" );
	}
}
