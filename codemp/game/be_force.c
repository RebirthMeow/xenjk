/*
===========================================================================
Copyright (C) 2014 - 2026, base_enhanced contributors (TheSil, deathsythe47, Avygeil)
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative, and carries code ported from
base_enhanced (https://github.com/avygeil/base_enhanced, GPLv2).

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

// be_force.c -- base_enhanced Force-power changes, quarantined.
//
// Ported from base_enhanced HEAD 774700c (game/w_force.c, g_main.c,
// g_active.c, g_combat.c); each function names the upstream function it
// mirrors. docs/ports/base_enhanced.json lists features, cvars and commits.
//
// Every behaviour is behind a cvar (g_xcvar.h). The defaults are the
// competitive profile; stock OpenJK is g_drainRework 0,
// g_gripRework 0, g_gripAbsorbFix 0, g_gripRefreshRate 300,
// g_protect3DamageReduction 0.80, g_thTeRequiresLOS 0,
// g_redirectWrongThTeBinds 0, g_fixForceTimers 0, g_sendForceTimers 0, and
// xenjk's switches for the changes upstream made unconditionally:
// g_beGripCooldowns 0, g_beGripDistance 0, g_bePushKnockedDown 0,
// g_beTeamEnergizeLiving 0 (tools/fixtures/competitive/stock_force_profile.cfg).
//
// Left out on purpose: account "boost" assists (AutoTHTE, fake alignment),
// the account-name "meme" overrides, stats, racemode, aim practice packs,
// pause bookkeeping, unlagged time shifts (xenjk has none), the TH/TE abseil
// line-of-sight special case (CONTENTS_ABSEIL), and the d_debugThTeLOS prints.
//
// g_gripRework 2's victim slow (x0.2 in BG_AdjustClientSpeed) is shared code
// the client predicts; it is announced to the cgame as BE_PM_GRIP_SLOW
// (be_pmove.h) so both sides apply it.

#include "g_local.h"
#include "be_pmove.h"
#include "w_saber.h"

// w_force.c (non-static, not in g_local.h)
extern qboolean WP_ForcePowerAvailable( gentity_t *self, forcePowers_t forcePower, int overrideAmt );
extern qboolean WP_ForcePowerUsable( gentity_t *self, forcePowers_t forcePower );
extern void WP_ForcePowerStart( gentity_t *self, forcePowers_t forcePower, int overrideAmt );
extern int WP_AbsorbConversion( gentity_t *attacked, int atdAbsLevel, gentity_t *attacker, int atPower, int atPowerLevel, int atForceSpent );
extern void DoGripAction( gentity_t *self, forcePowers_t forcePower );

// base_enhanced w_force.c:22-45
#define BE_DRAIN_REWORK1_FORCECOST_LEVEL3		(25)
#define BE_DRAIN_REWORK1_FORCECOST_LEVEL2		(17)
#define BE_DRAIN_REWORK1_FORCECOST_LEVEL1		(8)

#define BE_DRAIN_REWORK2_SELFDMG_LEVEL3		(30)
#define BE_DRAIN_REWORK2_SELFDMG_LEVEL2		(20)
#define BE_DRAIN_REWORK2_SELFDMG_LEVEL1		(10)

#define BE_DRAIN_REWORK_FORCEREDUCE_LEVEL3		(33)
#define BE_DRAIN_REWORK_FORCEREDUCE_LEVEL2		(22)
#define BE_DRAIN_REWORK_FORCEREDUCE_LEVEL1		(11)

#define BE_DRAIN_REWORK_DEBUFFDURATION_CONSTANT	(0)
#define BE_DRAIN_REWORK_DEBUFFDURATION_PERLEVEL	(1333)

#define BE_DRAIN_REWORK2_MINIMUMFORCE			(0)

#define BE_DRAIN_REWORK_COOLDOWN				(1000)

#define BE_DRAIN_REWORK_RANGE					(512)	// ForceShootDrain: stock 2048
#define BE_DRAIN_REWORK_HANDEXTEND				(200)	// ForceDrain: stock 20000

// base_enhanced: re-grip lockout after a grip ends or a push breaks it
#define BE_GRIP_COOLDOWN						(500)

// base_enhanced w_force.c:58, the g_gripAbsorbFix debounce
#define BE_GRIP_DEBOUNCE_TIME					(3000)

// xenjk: recursion bound for BE_HaveLOSToTarget (upstream has none)
#define BE_LOS_MAX_DEPTH						(8)

/*
================================================================================
Game frame
================================================================================
*/

// The movement switches (be_pmove.h) the shared bg code reads, from cvars.
static int BE_PmoveFeaturesFromCvars( void )
{
	int f = 0;

	if ( g_beHeadFriction.integer )				f |= BE_PM_HEAD_FRICTION;
	if ( g_beRoll100.integer )					f |= BE_PM_ROLL_100;
	if ( g_fixRoll.integer )					f |= BE_PM_FIX_ROLL;
	if ( g_fixWallgrab.integer )				f |= BE_PM_FIX_WALLGRAB;
	if ( g_fixForceJumpAnimationLock.integer )	f |= BE_PM_FORCELAND_FREE;
	if ( g_beDfaJumpCheck.integer )				f |= BE_PM_DFA_NEEDS_JUMP;
	if ( g_gripRework.integer == 2 )			f |= BE_PM_GRIP_SLOW;
	if ( g_fixForceTimers.integer )				f |= BE_PM_RAGE_END;
	if ( g_fixDempSaberThrow.integer )			f |= BE_PM_DEMP_SABERTHROW;
	if ( g_fixNoAmmoShootAnimation.integer )	f |= BE_PM_NOAMMO_ANIM;
	if ( g_fix5AmmoSniping.integer )			f |= BE_PM_FIVE_AMMO_SNIPE;
	return f;
}

// base_enhanced g_client.c G_BroadcastServerFeatureList: configstring 25
// carries the drain-rework token (rwd/rwd2) and the movement switches, so
// xenjk's cgame predicts what the server runs (the rest of upstream's list
// describes features xenjk does not have). Set whenever it changes.
static void BE_UpdateFeatureList( void )
{
	static char last[MAX_STRING_CHARS];
	static qboolean written = qfalse;
	char buf[MAX_STRING_CHARS];

	be_pmoveFeatures = BE_PmoveFeaturesFromCvars();
	buf[0] = '\0';
	if ( g_drainRework.integer == 2 )
		Q_strncpyz( buf, "rwd2", sizeof( buf ) );
	else if ( g_drainRework.integer )
		Q_strncpyz( buf, "rwd", sizeof( buf ) );
	BE_PM_AppendTokens( be_pmoveFeatures, buf, sizeof( buf ) );
	if ( !written || strcmp( buf, last ) )
	{
		trap->SetConfigstring( BE_CS_SERVERFEATURELIST, buf );
		Q_strncpyz( last, buf, sizeof( last ) );
		written = qtrue;
	}
}

static void BE_ForceTimersReset( void );

void BE_InitGame( void )
{
	BE_ForceTimersReset();
	BE_CombatInit(); // be_combat.c
	BE_UpdateFeatureList();
}

/*
================================================================================
Force timers (g_sendForceTimers)
================================================================================
*/

// base_enhanced w_force.c SendForceTimers/StartForceTimer: a timed power's end
// goes to its user and the spectators following them as
//   kls -1 -1 fpt <client> {<power> <full duration> <end, msec since level start>}...
// or "kls -1 -1 sfpt <client>" when none runs; rage recovery is listed as
// FP_SABER_OFFENSE. A stock cgame ignores "kls -1 -1"; xenjk's draws them under
// the active-power icons (cgame/cg_be.c). Upstream sends from every start and
// stop; here the frame compares each client's ends with what was last sent
// (the same values, at most a frame later; the end itself is exact), so the
// stock power code carries no extra hooks. Upstream's racemode skip does not
// apply; bots are not sent to; ends are the playerState's, so a death shows
// what the server still holds (upstream clears its own copy on death).
static int beTimerSent[MAX_CLIENTS][NUM_FORCE_POWERS];
static int beTimerFollow[MAX_CLIENTS];

// base_enhanced SendForceTimers' duration table (the bar's full length)
static int BE_TimerFullDuration( gclient_t *cl, int power )
{
	int fpLevel = cl->ps.fd.forcePowerLevel[power];

	if ( fpLevel < 1 ) fpLevel = 1;
	if ( fpLevel > 3 ) fpLevel = 3;
	switch ( power )
	{
	case FP_SABER_OFFENSE:	return 10000;	// RAGE_RECOVERY_TIME
	case FP_SPEED:			return fpLevel == 1 ? 10000 : fpLevel == 2 ? 15000 : 20000;
	case FP_TELEPATHY:		return fpLevel == 1 ? 20000 : fpLevel == 2 ? 25000 : 30000;
	case FP_PROTECT:		return 20000;
	case FP_ABSORB:			return 20000;
	case FP_RAGE:			return fpLevel == 1 ? 8000 : fpLevel == 2 ? 14000 : 20000;
	case FP_SEE:			return fpLevel == 1 ? 10000 : fpLevel == 2 ? 20000 : 30000;
	default:				return 0;
	}
}

// a running timer's end in msec since level start, 0 when none
static int BE_TimerEnd( gclient_t *cl, int power )
{
	int end;

	switch ( power )
	{
	case FP_SABER_OFFENSE:
		end = cl->ps.fd.forceRageRecoveryTime;
		break;
	case FP_SPEED: case FP_TELEPATHY: case FP_PROTECT: case FP_ABSORB: case FP_RAGE: case FP_SEE:
		end = ( cl->ps.fd.forcePowersActive & ( 1 << power ) ) ? cl->ps.fd.forcePowerDuration[power] : 0;
		break;
	default:
		return 0;
	}
	return end > level.time ? end - level.startTime : 0;
}

static void BE_SendForceTimers( int user, const int *ends, int to )
{
	char buf[MAX_STRING_CHARS];
	gclient_t *cl = &level.clients[user];
	qboolean gotOne = qfalse;
	int i;

	Com_sprintf( buf, sizeof( buf ), "kls -1 -1 fpt %d", user );
	for ( i = 0; i < NUM_FORCE_POWERS; i++ )
	{
		if ( ends[i] )
		{
			Q_strcat( buf, sizeof( buf ), va( " %d %d %d", i, BE_TimerFullDuration( cl, i ), ends[i] ) );
			gotOne = qtrue;
		}
	}
	if ( !gotOne )
	{
		Com_sprintf( buf, sizeof( buf ), "kls -1 -1 sfpt %d", user );
	}
	trap->SendServerCommand( to, buf );
}

// the client a spectator is following, -1 if none (SpectatorClientEndFrame)
static int BE_FollowedClient( gclient_t *cl )
{
	int target;

	if ( cl->sess.sessionTeam != TEAM_SPECTATOR || cl->sess.spectatorState != SPECTATOR_FOLLOW )
	{
		return -1;
	}
	target = cl->sess.spectatorClient;
	if ( target == -1 ) target = level.follow1;
	else if ( target == -2 ) target = level.follow2;
	return target >= 0 && target < level.maxclients ? target : -1;
}

static void BE_ForceTimersReset( void )
{
	int i;

	memset( beTimerSent, 0, sizeof( beTimerSent ) );
	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		beTimerFollow[i] = -1;
	}
}

static void BE_ForceTimersFrame( void )
{
	int ends[NUM_FORCE_POWERS];
	int i, j, p;

	if ( !g_sendForceTimers.integer )
	{
		return;
	}
	for ( i = 0; i < level.maxclients; i++ )
	{
		gclient_t *cl = &level.clients[i];
		qboolean changed = qfalse;

		if ( cl->pers.connected != CON_CONNECTED )
		{
			memset( beTimerSent[i], 0, sizeof( beTimerSent[i] ) );
			beTimerFollow[i] = -1;
			continue;
		}
		for ( p = 0; p < NUM_FORCE_POWERS; p++ )
		{
			ends[p] = BE_TimerEnd( cl, p );
			// a timer that ran out needs no message: the client stops showing it at its end
			if ( ends[p] != beTimerSent[i][p] && !( !ends[p] && beTimerSent[i][p] <= level.time - level.startTime ) )
			{
				changed = qtrue;
			}
		}
		memcpy( beTimerSent[i], ends, sizeof( ends ) );
		if ( !changed )
		{
			continue;
		}
		for ( j = 0; j < level.maxclients; j++ )
		{
			gclient_t *to = &level.clients[j];

			if ( to->pers.connected != CON_CONNECTED || ( g_entities[j].r.svFlags & SVF_BOT ) )
			{
				continue;
			}
			if ( j == i || BE_FollowedClient( to ) == i )
			{
				BE_SendForceTimers( i, ends, j );
			}
		}
	}

	// a spectator that starts following someone gets their timers
	for ( j = 0; j < level.maxclients; j++ )
	{
		gclient_t *to = &level.clients[j];
		int target;

		if ( to->pers.connected != CON_CONNECTED || ( g_entities[j].r.svFlags & SVF_BOT ) )
		{
			continue;
		}
		target = BE_FollowedClient( to );
		if ( target != beTimerFollow[j] )
		{
			beTimerFollow[j] = target;
			if ( target >= 0 && level.clients[target].pers.connected == CON_CONNECTED )
			{
				BE_SendForceTimers( target, beTimerSent[target], j );
			}
		}
	}
}

// base_enhanced g_main.c G_RunFrame: grip cost tables by g_gripRework. Mode 0
// restores the values the module started with (stock 30/30/60).
void BE_RunFrame( void )
{
	static int stockCost[NUM_FORCE_POWER_LEVELS];
	static qboolean captured = qfalse;
	int i;

	if ( !captured )
	{
		for ( i = 0; i < NUM_FORCE_POWER_LEVELS; i++ )
		{
			stockCost[i] = forcePowerNeeded[i][FP_GRIP];
		}
		captured = qtrue;
	}

	BE_UpdateFeatureList();	// movement switches may change mid-map
	BE_ForceTimersFrame();
	BE_CombatFrame(); // be_combat.c: improved homing

	switch ( g_gripRework.integer )
	{
	case 1:
		forcePowerNeeded[FORCE_LEVEL_1][FP_GRIP] = 30;
		forcePowerNeeded[FORCE_LEVEL_2][FP_GRIP] = 30;
		forcePowerNeeded[FORCE_LEVEL_3][FP_GRIP] = 32;
		break;
	case 2:
		forcePowerNeeded[FORCE_LEVEL_1][FP_GRIP] = 20;
		forcePowerNeeded[FORCE_LEVEL_2][FP_GRIP] = 25;
		forcePowerNeeded[FORCE_LEVEL_3][FP_GRIP] = 30;
		break;
	default:
		for ( i = 0; i < NUM_FORCE_POWER_LEVELS; i++ )
		{
			forcePowerNeeded[i][FP_GRIP] = stockCost[i];
		}
		break;
	}
}

/*
================================================================================
Force timers (g_fixForceTimers)
================================================================================
*/

// base_enhanced w_force.c FixDuration (on by default there): a timed power
// lasts a whole number of game frames, less the time since the last frame (a
// power started inside the frame - a bot's command - would otherwise get that
// frame free), and ends at `<=` (BE_ForceTimerEnded) on exactly the frame its
// time runs out; stock ends it up to a frame late. The frame is the game step
// (1000 / sv_fps x timescale; base_enhanced's own g_svfps). At 40 fps the
// rounding changes nothing (every duration is a multiple of 25 ms): a bot's
// power ends two frames sooner, a human's one. Bot2's forecast mirrors both
// (bot2_forecast.c ForecastForceUpdate, ForecastGenericCommand). The same `<=`
// for rage recovery in bg_pmove.c / bg_saber.c is shared code the client
// predicts; it is announced to the cgame as BE_PM_RAGE_END (be_pmove.h).
int BE_FixDuration( int duration, int sinceLastFrame )
{
	int frame;

	if ( !g_fixForceTimers.integer || duration <= 0 )
	{
		return duration;
	}
	frame = G_TickNominalFrameMsec();
	if ( frame > 0 )
	{
		duration += ( frame - ( duration % frame ) ) % frame;
	}
	return duration - sinceLastFrame;
}

// WP_ForcePowerStart: upstream rounds only the timed powers' durations (speed,
// mind trick, rage, protect, absorb, see), not the hold powers' overrides
// (grip, lightning, drain): drain rework's 1 ms one-shot would end at `<=`
// before WP_ForcePowerRun fires it.
int BE_FixPowerDuration( int forcePower, int duration, int sinceLastFrame )
{
	switch ( forcePower )
	{
	case FP_SPEED: case FP_TELEPATHY: case FP_RAGE: case FP_PROTECT: case FP_ABSORB: case FP_SEE:
		return BE_FixDuration( duration, sinceLastFrame );
	default:
		return duration;
	}
}

qboolean BE_ForceTimerEnded( int end, int time )
{
	return g_fixForceTimers.integer ? end <= time : end < time;
}

/*
================================================================================
Grip
================================================================================
*/

// base_enhanced WP_ForcePowerStop (FP_GRIP) and ForceThrow: 500 ms instead of
// stock 3000 after a grip ends and 1000 after a push breaks one.
int BE_GripUseCooldown( int stockMs )
{
	return g_beGripCooldowns.integer ? BE_GRIP_COOLDOWN : stockMs;
}

// ForceGrip and DoGripAction are replaced whole while any grip change that
// lives inside them is on; with all of these at default the stock functions run.
qboolean BE_GripReplacesStock( void )
{
	return g_gripRework.integer || g_gripAbsorbFix.integer || g_beGripDistance.integer ||
		g_gripRefreshRate.integer != 300;
}

qboolean BE_GripAbsorbFix( void )
{
	return g_gripAbsorbFix.integer ? qtrue : qfalse;
}

// base_enhanced (g_gripRework 2): a gripped player cannot start drain or lightning
qboolean BE_GrippedCannotAttack( gentity_t *self )
{
	return g_gripRework.integer == 2 && self->client->ps.fd.forceGripBeingGripped > level.time;
}

// base_enhanced DoGripAction: velocity refresh interval, one server frame .. 500 ms
static int BE_GripRefreshMs( void )
{
	int frameMs = sv_fps.integer > 0 ? 1000 / sv_fps.integer : 50;
	return Com_Clampi( frameMs, 500, g_gripRefreshRate.integer );
}

// base_enhanced w_force.c ForceGrip, less the meme/stats/unlagged/aim-practice code
void BE_ForceGrip( gentity_t *self )
{
	trace_t tr;
	vec3_t tfrom, tto, fwd;
	gentity_t *target;
	int usable = 0;

	if ( self->health <= 0 )
	{
		return;
	}

	if ( self->client->ps.forceHandExtend != HANDEXTEND_NONE )
	{
		return;
	}

	if ( self->client->ps.weaponTime > 0 )
	{
		return;
	}

	if ( self->client->ps.fd.forceGripUseTime > level.time )
	{
		return;
	}

	if ( !WP_ForcePowerUsable( self, FP_GRIP ) )
	{
		return;
	}

	if ( g_gripRework.integer == 2 && self->client->ps.fd.forceGripBeingGripped > level.time )
	{
		return; // no grip wars; first gripper wins
	}

	VectorCopy( self->client->ps.origin, tfrom );
	tfrom[2] += self->client->ps.viewheight;
	AngleVectors( self->client->ps.viewangles, fwd, NULL, NULL );
	tto[0] = tfrom[0] + fwd[0]*MAX_GRIP_DISTANCE;
	tto[1] = tfrom[1] + fwd[1]*MAX_GRIP_DISTANCE;
	tto[2] = tfrom[2] + fwd[2]*MAX_GRIP_DISTANCE;

	trap->Trace( &tr, tfrom, NULL, NULL, tto, self->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );

	if ( tr.fraction == 1.0f || tr.entityNum == ENTITYNUM_NONE || !g_entities[tr.entityNum].client )
	{
		self->client->ps.fd.forceGripEntityNum = ENTITYNUM_NONE;
		return;
	}

	target = &g_entities[tr.entityNum];

	// with the rework a still-crippled (just released) player can be gripped again
	if ( target->client->ps.fd.forceGripBeingGripped >= level.time ||
		( !g_gripRework.integer && target->client->ps.fd.forceGripCripple ) )
	{
		self->client->ps.fd.forceGripEntityNum = ENTITYNUM_NONE;
		return;
	}

	usable = ForcePowerUsableOn( self, target, FP_GRIP );

	if ( usable > 0 &&
		( g_friendlyFire.integer || !OnSameTeam( self, target ) ) &&
		( !g_beGripDistance.integer || Distance( self->client->ps.origin, target->client->ps.origin ) <= MAX_GRIP_DISTANCE ) )
	{
		if ( target->s.number < MAX_CLIENTS && target->client->ps.m_iVehicleNum )
		{ //a player on a vehicle
			gentity_t *vehEnt = &g_entities[target->client->ps.m_iVehicleNum];
			if ( vehEnt->inuse && vehEnt->client && vehEnt->m_pVehicle )
			{
				if ( BG_VehicleRidesLikeSpeeder( vehEnt->m_pVehicle->m_pVehicleInfo ) ||
					vehEnt->m_pVehicle->m_pVehicleInfo->type == VH_ANIMAL )
				{ //push the guy off
					vehEnt->m_pVehicle->m_pVehicleInfo->Eject( vehEnt->m_pVehicle, (bgEntity_t *)target, qfalse );
				}
			}
		}
		self->client->ps.fd.forceGripEntityNum = tr.entityNum;
		target->client->ps.fd.forceGripStarted = level.time;
		BE_StatsGrip( self, target ); // base_enhanced: stats (be_stats.c)
		self->client->ps.fd.forceGripDamageDebounceTime = 0;

		if ( g_gripRework.integer == 2 && self->client->ps.fd.forcePowerLevel[FP_GRIP] < FORCE_LEVEL_3 )
		{ // levels 1 and 2 stop the victim's horizontal run at the start
			const float xyVelocityCap = 50.0f;
			const float vx = target->client->ps.velocity[0];
			const float vy = target->client->ps.velocity[1];
			const float speed = sqrtf( vx * vx + vy * vy );

			if ( speed > xyVelocityCap && speed > 0 )
			{
				const float scale = xyVelocityCap / speed;
				target->client->ps.velocity[0] *= scale;
				target->client->ps.velocity[1] *= scale;
			}
		}

		self->client->ps.forceHandExtend = HANDEXTEND_FORCE_HOLD;
		self->client->ps.forceHandExtendTime = level.time + 5000;
	}
	else
	{
		self->client->ps.fd.forceGripEntityNum = ENTITYNUM_NONE;

		if ( usable == -1 && g_gripAbsorbFix.integer )
		{ // failed to *start* a grip because the target was absorbing: absorb the force points,
		  // at most once per BE_GRIP_DEBOUNCE_TIME (stock let you try on absorbers for free)
			if ( !self->client->grippedAnAbsorberTime || level.time - self->client->grippedAnAbsorberTime >= BE_GRIP_DEBOUNCE_TIME )
			{
				self->client->grippedAnAbsorberTime = level.time;

				// absorb the force (this also plays the sound/effect)
				WP_AbsorbConversion( target, target->client->ps.fd.forcePowerLevel[FP_ABSORB], self, FP_GRIP, self->client->ps.fd.forcePowerLevel[FP_GRIP], 30 );

				// dock the force from the user
				BG_ForcePowerDrain( &self->client->ps, FP_GRIP, GRIP_DRAIN_AMOUNT );
			}
		}
	}
}

// base_enhanced w_force.c DoGripAction, less the meme code
void BE_DoGripAction( gentity_t *self, forcePowers_t forcePower )
{
	gentity_t *gripEnt;
	int gripLevel = 0;
	int usable;
	trace_t tr;
	vec3_t a;
	vec3_t fwd, fwd_o, start_o, nvel;
	qboolean tooFar;

	self->client->dangerTime = level.time;
	self->client->ps.eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;

	gripEnt = &g_entities[self->client->ps.fd.forceGripEntityNum];

	if ( !gripEnt || !gripEnt->client || !gripEnt->inuse || gripEnt->health < 1 )
	{
		WP_ForcePowerStop( self, forcePower );
		self->client->ps.fd.forceGripEntityNum = ENTITYNUM_NONE;

		if ( gripEnt && gripEnt->client && gripEnt->inuse )
		{
			gripEnt->client->ps.forceGripChangeMovetype = PM_NORMAL;
		}
		return;
	}

	usable = ForcePowerUsableOn( self, gripEnt, FP_GRIP );
	if ( usable <= 0 )
	{
		if ( usable == -1 && g_gripAbsorbFix.integer )
		{ // the victim began absorbing mid-grip: no force is absorbed here, but start the
		  // "gripped an absorber" debounce so BE_ForceGrip does not absorb right afterwards
			gentity_t *abSound;

			self->client->grippedAnAbsorberTime = level.time;

			abSound = G_PreDefSound( gripEnt->client->ps.origin, PDSOUND_ABSORBHIT );
			abSound->s.trickedentindex = gripEnt->s.number;
			gripEnt->client->forcePowerSoundDebounce = level.time + 400;
		}
		WP_ForcePowerStop( self, forcePower );
		self->client->ps.fd.forceGripEntityNum = ENTITYNUM_NONE;

		if ( gripEnt && gripEnt->client && gripEnt->inuse )
		{
			gripEnt->client->ps.forceGripChangeMovetype = PM_NORMAL;
		}
		return;
	}

	VectorSubtract( gripEnt->client->ps.origin, self->client->ps.origin, a );

	trap->Trace( &tr, self->client->ps.origin, NULL, NULL, gripEnt->client->ps.origin, self->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );

	gripLevel = WP_AbsorbConversion( gripEnt, gripEnt->client->ps.fd.forcePowerLevel[FP_ABSORB], self, FP_GRIP, self->client->ps.fd.forcePowerLevel[FP_GRIP], forcePowerNeeded[self->client->ps.fd.forcePowerLevel[FP_GRIP]][FP_GRIP] );

	if ( gripLevel == -1 )
	{
		gripLevel = self->client->ps.fd.forcePowerLevel[FP_GRIP];
	}

	if ( !gripLevel )
	{
		WP_ForcePowerStop( self, forcePower );
		return;
	}

	if ( g_beGripDistance.integer )
	{ // no distance check for maintaining grip 3 (would only punish fast strafing),
	  // and a forgiving one for maintaining grip 1/2
		tooFar = ( gripLevel != FORCE_LEVEL_3 && VectorLength( a ) > MAX_GRIP_DISTANCE * 2 );
	}
	else
	{ // stock
		tooFar = ( VectorLength( a ) > MAX_GRIP_DISTANCE );
	}
	if ( tooFar )
	{
		WP_ForcePowerStop( self, forcePower );
		return;
	}

	if ( !InFront( gripEnt->client->ps.origin, self->client->ps.origin, self->client->ps.viewangles, 0.9f ) &&
		gripLevel < FORCE_LEVEL_3 )
	{
		WP_ForcePowerStop( self, forcePower );
		return;
	}

	if ( tr.fraction != 1.0f &&
		tr.entityNum != gripEnt->s.number )
	{
		WP_ForcePowerStop( self, forcePower );
		return;
	}

	if ( G_TickStrictDue( TICK_FORCE, self->client->ps.fd.forcePowerDebounce[FP_GRIP] ) )
	{ //2 damage per second while choking, resulting in 10 damage total (not including The Squeeze<tm>)
		self->client->ps.fd.forcePowerDebounce[FP_GRIP] = G_TickStrictNext( TICK_FORCE, self->client->ps.fd.forcePowerDebounce[FP_GRIP], 1000, qfalse ); // tick (g_tick.c)
		G_Damage( gripEnt, self, self, NULL, NULL, 2, DAMAGE_NO_ARMOR, MOD_FORCE_DARK );
	}

	Jetpack_Off( gripEnt ); //make sure the guy being gripped has his jetpack off.

	if ( gripLevel == FORCE_LEVEL_1 )
	{
		gripEnt->client->ps.fd.forceGripBeingGripped = level.time + 1000;
		if ( g_gripRework.integer )
		{ // the rework holds a level-1 victim in place too
			gripEnt->client->ps.forceGripChangeMovetype = PM_FLOAT;
			gripEnt->client->ps.otherKiller = self->s.number;
			gripEnt->client->ps.otherKillerTime = level.time + 5000;
			gripEnt->client->ps.otherKillerDebounceTime = level.time + 100;
		}

		if ( ( level.time - gripEnt->client->ps.fd.forceGripStarted ) > 5000 )
		{
			WP_ForcePowerStop( self, forcePower );
		}
		return;
	}

	if ( gripLevel == FORCE_LEVEL_2 )
	{
		gripEnt->client->ps.fd.forceGripBeingGripped = level.time + 1000;

		if ( G_TickStrictDue( TICK_FORCE, gripEnt->client->ps.forceGripMoveInterval ) )
		{
			gripEnt->client->ps.velocity[2] = 30;

			gripEnt->client->ps.forceGripMoveInterval = G_TickStrictNext( TICK_FORCE, gripEnt->client->ps.forceGripMoveInterval, BE_GripRefreshMs(), qfalse ); //only update velocity every 300ms, so as to avoid heavy bandwidth usage
		}

		gripEnt->client->ps.otherKiller = self->s.number;
		gripEnt->client->ps.otherKillerTime = level.time + 5000;
		gripEnt->client->ps.otherKillerDebounceTime = level.time + 100;

		gripEnt->client->ps.forceGripChangeMovetype = PM_FLOAT;

		if ( ( level.time - gripEnt->client->ps.fd.forceGripStarted ) > 3000 && !self->client->ps.fd.forceGripDamageDebounceTime )
		{ //if we managed to lift him into the air for 2 seconds, give him a crack
			self->client->ps.fd.forceGripDamageDebounceTime = 1;
			G_Damage( gripEnt, self, self, NULL, NULL, 20, DAMAGE_NO_ARMOR, MOD_FORCE_DARK );

			//Must play custom sounds on the actual entity. Don't use G_Sound (it creates a temp entity for the sound)
			G_EntitySound( gripEnt, CHAN_VOICE, G_SoundIndex( va( "*choke%d.wav", Q_irand( 1, 3 ) ) ) );

			gripEnt->client->ps.forceHandExtend = HANDEXTEND_CHOKE;
			gripEnt->client->ps.forceHandExtendTime = level.time + 2000;

			if ( gripEnt->client->ps.fd.forcePowersActive & ( 1 << FP_GRIP ) )
			{ //choking, so don't let him keep gripping himself
				WP_ForcePowerStop( gripEnt, FP_GRIP );
			}
		}
		else if ( ( level.time - gripEnt->client->ps.fd.forceGripStarted ) > 4000 )
		{
			WP_ForcePowerStop( self, forcePower );
		}
		return;
	}

	if ( gripLevel == FORCE_LEVEL_3 )
	{
		gripEnt->client->ps.fd.forceGripBeingGripped = level.time + 1000;

		gripEnt->client->ps.otherKiller = self->s.number;
		gripEnt->client->ps.otherKillerTime = level.time + 5000;
		gripEnt->client->ps.otherKillerDebounceTime = level.time + 100;

		gripEnt->client->ps.forceGripChangeMovetype = PM_FLOAT;

		if ( G_TickStrictDue( TICK_FORCE, gripEnt->client->ps.forceGripMoveInterval ) )
		{
			float nvLen = 0;
			// the speed bands scale with the refresh interval (1.0 at the stock 300 ms)
			float factor = ( (float)BE_GripRefreshMs() ) / 300.0f;

			VectorCopy( gripEnt->client->ps.origin, start_o );
			AngleVectors( self->client->ps.viewangles, fwd, NULL, NULL );
			fwd_o[0] = self->client->ps.origin[0] + fwd[0]*128;
			fwd_o[1] = self->client->ps.origin[1] + fwd[1]*128;
			fwd_o[2] = self->client->ps.origin[2] + fwd[2]*128;
			fwd_o[2] += 16;
			VectorSubtract( fwd_o, start_o, nvel );

			nvLen = VectorLength( nvel );

			if ( nvLen < 16 * factor )
			{ //within x units of desired spot
				VectorNormalize( nvel );
				gripEnt->client->ps.velocity[0] = nvel[0]*8;
				gripEnt->client->ps.velocity[1] = nvel[1]*8;
				gripEnt->client->ps.velocity[2] = nvel[2]*8;
			}
			else if ( nvLen < 64 * factor )
			{
				VectorNormalize( nvel );
				gripEnt->client->ps.velocity[0] = nvel[0]*128;
				gripEnt->client->ps.velocity[1] = nvel[1]*128;
				gripEnt->client->ps.velocity[2] = nvel[2]*128;
			}
			else if ( nvLen < 128 * factor )
			{
				VectorNormalize( nvel );
				gripEnt->client->ps.velocity[0] = nvel[0]*256;
				gripEnt->client->ps.velocity[1] = nvel[1]*256;
				gripEnt->client->ps.velocity[2] = nvel[2]*256;
			}
			else if ( nvLen < 200 * factor )
			{
				VectorNormalize( nvel );
				gripEnt->client->ps.velocity[0] = nvel[0]*512;
				gripEnt->client->ps.velocity[1] = nvel[1]*512;
				gripEnt->client->ps.velocity[2] = nvel[2]*512;
			}
			else
			{
				VectorNormalize( nvel );
				gripEnt->client->ps.velocity[0] = nvel[0]*700;
				gripEnt->client->ps.velocity[1] = nvel[1]*700;
				gripEnt->client->ps.velocity[2] = nvel[2]*700;
			}

			gripEnt->client->ps.forceGripMoveInterval = G_TickStrictNext( TICK_FORCE, gripEnt->client->ps.forceGripMoveInterval, BE_GripRefreshMs(), qfalse ); //only update velocity every 300ms, so as to avoid heavy bandwidth usage
		}

		if ( ( level.time - gripEnt->client->ps.fd.forceGripStarted ) > 3000 && !self->client->ps.fd.forceGripDamageDebounceTime )
		{ //if we managed to lift him into the air for 2 seconds, give him a crack
			self->client->ps.fd.forceGripDamageDebounceTime = 1;
			G_Damage( gripEnt, self, self, NULL, NULL, 40, DAMAGE_NO_ARMOR, MOD_FORCE_DARK );

			//Must play custom sounds on the actual entity. Don't use G_Sound (it creates a temp entity for the sound)
			G_EntitySound( gripEnt, CHAN_VOICE, G_SoundIndex( va( "*choke%d.wav", Q_irand( 1, 3 ) ) ) );

			gripEnt->client->ps.forceHandExtend = HANDEXTEND_CHOKE;
			gripEnt->client->ps.forceHandExtendTime = level.time + 2000;

			if ( gripEnt->client->ps.fd.forcePowersActive & ( 1 << FP_GRIP ) )
			{ //choking, so don't let him keep gripping himself
				WP_ForcePowerStop( gripEnt, FP_GRIP );
			}
		}
		else if ( ( level.time - gripEnt->client->ps.fd.forceGripStarted ) > 4000 )
		{
			WP_ForcePowerStop( self, forcePower );
		}
		return;
	}
}

// base_enhanced w_force.c WP_ForcePowerRun (FP_GRIP), g_gripRework 2 only: the
// out-of-Force check runs before the per-100 ms drain and is waived while the
// first second's freebie (BE_GripStarted) lasts. qfalse: run the stock case.
qboolean BE_GripRun( gentity_t *self, forcePowers_t forcePower )
{
	if ( g_gripRework.integer != 2 )
	{
		return qfalse;
	}

	if ( self->client->ps.forceHandExtend != HANDEXTEND_FORCE_HOLD )
	{
		WP_ForcePowerStop( self, FP_GRIP );
		return qtrue;
	}

	// allow grip to continue with 0 fp during the initial second
	if ( self->client->ps.fd.forcePower < 1 && G_TickStrictDue( TICK_FORCE, self->client->ps.fd.forcePowerDebounce[FP_PULL] ) )
	{
		WP_ForcePowerStop( self, FP_GRIP );
		return qtrue;
	}

	if ( G_TickStrictDue( TICK_FORCE, self->client->ps.fd.forcePowerDebounce[FP_PULL] ) )
	{ //This is sort of not ideal. Using the debounce value reserved for pull for this because pull doesn't need it.
		BG_ForcePowerDrain( &self->client->ps, forcePower, 1 );
		// tick: drain at today's sv_fps 30 rate (g_tick.c)
		self->client->ps.fd.forcePowerDebounce[FP_PULL] = G_TickStrictNext( TICK_FORCE, self->client->ps.fd.forcePowerDebounce[FP_PULL], 100, qtrue );
	}

	DoGripAction( self, forcePower );
	return qtrue;
}

// base_enhanced w_force.c WP_DoSpecificPower (FP_GRIP): g_gripRework 2 gives a
// freebie second of grip before the per-100 ms drain starts.
void BE_GripStarted( gentity_t *self )
{
	if ( g_gripRework.integer == 2 )
	{
		self->client->ps.fd.forcePowerDebounce[FP_PULL] = level.time + 1000;
	}
}

/*
================================================================================
Push / pull
================================================================================
*/

// base_enhanced w_force.c ForcePowerUsableOn ("CHANGE 333"): knocked-down
// players can be pushed and pulled; upstream removed the stock refusal.
qboolean BE_PushKnockedDown( void )
{
	return g_bePushKnockedDown.integer ? qtrue : qfalse;
}

/*
================================================================================
Drain (g_drainRework 1 and 2)
================================================================================
*/

static int BE_DrainLevel( gentity_t *self )
{
	return self->client->ps.fd.forcePowerLevel[FP_DRAIN];
}

static int BE_DrainRework1Cost( gentity_t *self )
{
	switch ( BE_DrainLevel( self ) )
	{
	case 3: return BE_DRAIN_REWORK1_FORCECOST_LEVEL3;
	case 2: return BE_DRAIN_REWORK1_FORCECOST_LEVEL2;
	default: return BE_DRAIN_REWORK1_FORCECOST_LEVEL1;
	}
}

static int BE_DrainRework2SelfDamage( gentity_t *self )
{
	switch ( BE_DrainLevel( self ) )
	{
	case 3: return BE_DRAIN_REWORK2_SELFDMG_LEVEL3;
	case 2: return BE_DRAIN_REWORK2_SELFDMG_LEVEL2;
	default: return BE_DRAIN_REWORK2_SELFDMG_LEVEL1;
	}
}

static int BE_DrainForceReduce( gentity_t *self )
{
	switch ( BE_DrainLevel( self ) )
	{
	case 3: return BE_DRAIN_REWORK_FORCEREDUCE_LEVEL3;
	case 2: return BE_DRAIN_REWORK_FORCEREDUCE_LEVEL2;
	default: return BE_DRAIN_REWORK_FORCEREDUCE_LEVEL1;
	}
}

// base_enhanced w_force.c WP_ForcePowerAvailable (FP_DRAIN branch, rework on)
qboolean BE_DrainAvailable( gentity_t *self )
{
	if ( g_drainRework.integer >= 2 )
	{
		return self->client->ps.fd.forcePower >= BE_DRAIN_REWORK2_MINIMUMFORCE;
	}
	return self->client->ps.fd.forcePower >= BE_DrainRework1Cost( self );
}

// base_enhanced w_force.c WP_ForcePowerUsable: in drain rework 2 a drained
// (debuffed) player cannot drain back.
qboolean BE_DrainDebuffBlocks( gentity_t *self, forcePowers_t forcePower )
{
	return forcePower == FP_DRAIN && self->client->drainDebuffTime > level.time &&
		g_drainRework.integer && g_drainRework.integer != 1;
}

// base_enhanced w_force.c ForceDrain (rework on): a one-shot drain with a
// short hand extend; mode 2 refuses when the health cost would kill.
void BE_ForceDrain( gentity_t *self )
{
	if ( self->health <= 0 )
	{
		return;
	}

	if ( self->client->ps.forceHandExtend != HANDEXTEND_NONE )
	{
		return;
	}

	if ( self->client->ps.weaponTime > 0 )
	{
		return;
	}

	if ( self->client->ps.fd.forcePower < BE_DRAIN_REWORK2_MINIMUMFORCE || !WP_ForcePowerUsable( self, FP_DRAIN ) )
	{
		return;
	}
	if ( self->client->ps.fd.forcePowerDebounce[FP_DRAIN] > level.time )
	{//stops it while using it and also after using it
		return;
	}
	if ( BE_GrippedCannotAttack( self ) )
	{
		return;
	}

	if ( g_drainRework.integer >= 2 )
	{
		if ( self->health <= BE_DrainRework2SelfDamage( self ) )
		{
			return;
		}
	}

	self->client->ps.forceHandExtend = HANDEXTEND_FORCE_HOLD;
	self->client->ps.forceHandExtendTime = level.time + BE_DRAIN_REWORK_HANDEXTEND;

	G_Sound( self, CHAN_BODY, G_SoundIndex( "sound/weapons/force/drain.wav" ) );

	WP_ForcePowerStart( self, FP_DRAIN, 1 );
}

// base_enhanced w_force.c ForceDrainDamage (rework on). Returns the Force
// actually drained from the target, -1 if it was absorbed.
static int BE_ForceDrainDamage( gentity_t *self, gentity_t *traceEnt, vec3_t dir, vec3_t impactPoint )
{
	gentity_t *tent;
	int actualForceDrainedFromTarget = 0;
	qboolean absorbed = qfalse;

	self->client->dangerTime = level.time;
	self->client->ps.eFlags &= ~EF_INVULNERABLE;
	self->client->invulnerableTimer = 0;

	if ( traceEnt && traceEnt->takedamage )
	{
		// the rework drains (and debuffs) targets with no Force too
		if ( traceEnt->client && ( !OnSameTeam( self, traceEnt ) || g_friendlyFire.integer ) && self->client->ps.fd.forceDrainTime < level.time )
		{//an enemy
			if ( ForcePowerUsableOn( self, traceEnt, FP_DRAIN ) > 0 )
			{
				int modPowerLevel = -1;
				int dmg = 0;
				int reworkDebuffDuration;

				if ( BE_DrainLevel( self ) >= FORCE_LEVEL_1 && BE_DrainLevel( self ) <= FORCE_LEVEL_3 )
				{
					dmg = BE_DrainForceReduce( self );
				}

				modPowerLevel = WP_AbsorbConversion( traceEnt, traceEnt->client->ps.fd.forcePowerLevel[FP_ABSORB], self, FP_DRAIN, BE_DrainLevel( self ), dmg );
				if ( modPowerLevel != -1 )
				{ // absorbed: the absorber gains Force, nothing is drained
					absorbed = qtrue;
					dmg = 0;
				}

				if ( dmg )
				{
					actualForceDrainedFromTarget = dmg;
					BE_StatsDrain( self, traceEnt, dmg ); // base_enhanced: stats (be_stats.c)
					traceEnt->client->ps.fd.forcePower -= dmg;
				}
				if ( traceEnt->client->ps.fd.forcePower < 0 )
				{
					actualForceDrainedFromTarget -= abs( traceEnt->client->ps.fd.forcePower );
					traceEnt->client->ps.fd.forcePower = 0;
				}

				if ( g_drainRework.integer == 1 )
				{ // the drainer heals by what was drained, up to 125
					self->health += actualForceDrainedFromTarget;
					if ( self->health > 125 )
					{
						self->health = 125;
					}
					self->client->ps.stats[STAT_HEALTH] = self->health;
				}
				else
				{ // the drainer gets 60% of the drained Force back
					int restoredForce = (int)floorf( ( (float)actualForceDrainedFromTarget ) * 0.6f + 0.5f );
					self->client->ps.fd.forcePower += restoredForce;
					if ( self->client->ps.fd.forcePower > 100 )
					{
						self->client->ps.fd.forcePower = 100;
					}
				}

				reworkDebuffDuration = BE_DRAIN_REWORK_DEBUFFDURATION_CONSTANT + ( BE_DRAIN_REWORK_DEBUFFDURATION_PERLEVEL * BE_DrainLevel( self ) );
				if ( dmg )
				{ // no regeneration, no team energize, and (mode 2) no drain for the debuff
					traceEnt->client->ps.fd.forcePowerRegenDebounceTime = level.time + reworkDebuffDuration;
					traceEnt->client->drainDebuffTime = level.time + reworkDebuffDuration;
				}

				if ( traceEnt->client->forcePowerSoundDebounce < level.time && dmg )
				{
					tent = G_TempEntity( impactPoint, EV_FORCE_DRAINED );
					tent->s.eventParm = DirToByte( dir );
					tent->s.owner = traceEnt->s.number;
					tent->s.time = reworkDebuffDuration; // the debuff length, for clients that draw it

					traceEnt->client->forcePowerSoundDebounce = level.time + 400;
				}
			}
		}
	}

	if ( absorbed )
	{
		return -1;
	}

	return actualForceDrainedFromTarget;
}

// base_enhanced w_force.c ForceShootDrain (rework on): one eye-height line,
// 512 units, 3-unit bounds; no level-3 arc. Hit/absorb flags go to
// ps/s.userInt1/userInt2 for clients that draw the result. Mode 1 costs
// 8/17/25 Force; mode 2 costs health in proportion to the Force drained.
int BE_ForceShootDrain( gentity_t *self )
{
	trace_t tr;
	vec3_t end, forward, start, mins, maxs;
	gentity_t *traceEnt;
	int gotOneOrMore = 0;
	int actualAmountDrained = 0;

	if ( self->health <= 0 )
	{
		return 0;
	}
	AngleVectors( self->client->ps.viewangles, forward, NULL, NULL );
	VectorNormalize( forward );

	VectorCopy( self->client->ps.origin, start );
	start[2] += self->client->ps.viewheight;

	VectorMA( start, BE_DRAIN_REWORK_RANGE, forward, end );
	VectorSet( mins, -3, -3, -3 );
	VectorSet( maxs, 3, 3, 3 );

	trap->Trace( &tr, start, mins, maxs, end, self->s.number, MASK_SHOT, qfalse, 0, 0 );

	if ( tr.entityNum == ENTITYNUM_NONE || tr.fraction == 1.0 || tr.allsolid || tr.startsolid || !g_entities[tr.entityNum].client || !g_entities[tr.entityNum].inuse )
	{
		self->s.userInt1 = self->client->ps.userInt1 = self->s.userInt2 = self->client->ps.userInt2 = 0;
	}
	else
	{
		traceEnt = &g_entities[tr.entityNum];
		actualAmountDrained = BE_ForceDrainDamage( self, traceEnt, forward, tr.endpos );
		gotOneOrMore = 1;

		self->s.userInt1 = self->client->ps.userInt1 = 1; // hit
		if ( actualAmountDrained == -1 && traceEnt->client && ( traceEnt->client->ps.fd.forcePowersActive & ( 1 << FP_ABSORB ) ) )
			self->s.userInt2 = self->client->ps.userInt2 = 1; // absorbed
		else
			self->s.userInt2 = self->client->ps.userInt2 = 0; // not absorbed
	}

	self->client->ps.activeForcePass = self->client->ps.fd.forcePowerLevel[FP_DRAIN] + FORCE_LEVEL_3;

	if ( g_drainRework.integer < 2 )
	{
		BG_ForcePowerDrain( &self->client->ps, FP_DRAIN, BE_DrainRework1Cost( self ) );
	}
	else
	{
		int healthCost = BE_DrainRework2SelfDamage( self );
		int maxFpDrained = BE_DrainForceReduce( self );
		int selfdmg;

		if ( actualAmountDrained > 0 )
			selfdmg = (int)floor( healthCost * ( (double)actualAmountDrained / (double)maxFpDrained ) + 0.5 );
		else // a miss, an absorbed drain or an empty target costs the full amount
			selfdmg = healthCost;

		if ( selfdmg > 0 )
		{
			G_Damage( self, self, self, NULL, NULL, selfdmg, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR | DAMAGE_NO_SELF_PROTECTION, MOD_SUICIDE );
		}
	}

	return gotOneOrMore;
}

// base_enhanced w_force.c WP_ForcePowerRun (FP_DRAIN, rework on): no hold to
// extend, one shot per activation. qfalse: run the stock case.
qboolean BE_DrainRun( gentity_t *self )
{
	if ( !g_drainRework.integer )
	{
		return qfalse;
	}

	if ( self->client->ps.forceHandExtend != HANDEXTEND_FORCE_HOLD )
	{
		WP_ForcePowerStop( self, FP_DRAIN );
		return qtrue;
	}

	if ( !WP_ForcePowerAvailable( self, FP_DRAIN, 0 ) || self->client->ps.fd.forcePowerDuration[FP_DRAIN] < level.time ||
		self->client->ps.fd.forcePower < BE_DRAIN_REWORK2_MINIMUMFORCE )
	{
		WP_ForcePowerStop( self, FP_DRAIN );
	}
	else
	{
		BE_ForceShootDrain( self );
	}
	return qtrue;
}

// base_enhanced w_force.c WP_ForcePowerStop (FP_DRAIN, rework on): 1 s
// cooldown at every level; the short hand extend plays out. qfalse: stock.
qboolean BE_DrainStop( gentity_t *self )
{
	if ( !g_drainRework.integer )
	{
		return qfalse;
	}

	self->client->ps.fd.forcePowerDebounce[FP_DRAIN] = level.time + BE_DRAIN_REWORK_COOLDOWN;

	if ( self->client->ps.forceHandExtend == HANDEXTEND_FORCE_HOLD && level.time >= self->client->ps.forceHandExtendTime )
	{
		self->client->ps.forceHandExtendTime = 0; //reset hand position
	}

	self->client->ps.activeForcePass = 0;
	return qtrue;
}

/*
================================================================================
Team heal / team energize
================================================================================
*/

// base_enhanced w_force.c HaveLOSToTarget: feet, half view height and eyes to
// the same on the target; a teammate in the way passes the check on if it can
// see the target itself. Recursion is bounded here (upstream's is not).
static qboolean BE_HaveLOSToTarget( gentity_t *self, gentity_t *ent, int depth )
{
	int i, j;

	if ( !self || !self->client || !ent || !ent->client || depth > BE_LOS_MAX_DEPTH )
		return qfalse;

	for ( i = 0; i < 3; i++ )
	{
		vec3_t from;
		from[0] = self->client->ps.origin[0];
		from[1] = self->client->ps.origin[1];
		switch ( i )
		{
		case 0:
			from[2] = self->client->ps.origin[2];
			break;
		case 1:
			if ( self->client->ps.pm_flags & PMF_DUCKED || self->client->ps.pm_flags & PMF_ROLLING )
				continue;
			from[2] = self->client->ps.origin[2] + 0.5 * self->client->ps.viewheight;
			break;
		default:
			from[2] = self->client->ps.origin[2] + self->client->ps.viewheight;
			break;
		}

		for ( j = 0; j < 3; j++ )
		{
			vec3_t to;
			trace_t tr;
			to[0] = ent->client->ps.origin[0];
			to[1] = ent->client->ps.origin[1];

			switch ( j )
			{
			case 0:
				to[2] = ent->client->ps.origin[2];
				break;
			case 1:
				if ( ent->client->ps.pm_flags & PMF_DUCKED || ent->client->ps.pm_flags & PMF_ROLLING )
					continue;
				to[2] = ent->client->ps.origin[2] + 0.5 * ent->client->ps.viewheight;
				break;
			default:
				to[2] = ent->client->ps.origin[2] + ent->client->ps.viewheight;
				break;
			}

			trap->Trace( &tr, from, NULL, NULL, to, self->s.number, MASK_PLAYERSOLID, qfalse, 0, 0 );
			if ( tr.entityNum != ent->s.number )
			{
				if ( tr.entityNum < MAX_CLIENTS )
				{ // check if the entity blocking LOS has LOS to the target
					if ( BE_HaveLOSToTarget( &g_entities[tr.entityNum], ent, depth + 1 ) )
						return qtrue;
				}
				continue;
			}

			return qtrue;
		}
	}

	return qfalse;
}

// base_enhanced w_force.c ForceTeamHeal / ForceTeamForceReplenish target rules,
// called in place of the stock PVS test: stock PVS, or line of sight with
// g_thTeRequiresLOS; for energize, living teammates only
// (g_beTeamEnergizeLiving, upstream "CHANGE 60") and no drain-debuffed ones
// (g_drainRework; upstream does not skip debuffed players for team heal).
qboolean BE_TeamPowerTargetOk( gentity_t *self, gentity_t *ent, forcePowers_t forcePower )
{
	if ( forcePower == FP_TEAM_FORCE )
	{
		if ( g_beTeamEnergizeLiving.integer && ent->client->ps.stats[STAT_HEALTH] <= 0 )
		{
			return qfalse;
		}
		if ( g_drainRework.integer && ent->client->drainDebuffTime >= level.time )
		{
			return qfalse;
		}
	}

	if ( !g_thTeRequiresLOS.integer )
	{
		return trap->InPVS( self->client->ps.origin, ent->client->ps.origin );
	}

	return BE_HaveLOSToTarget( self, ent, 0 );
}

// base_enhanced w_force.c WP_DoSpecificPower: g_redirectWrongThTeBinds lets a
// team heal bind fire team energize for a player who knows only that (and back).
forcePowers_t BE_RedirectThTePower( gentity_t *self, forcePowers_t forcePower )
{
	int known;

	if ( !g_redirectWrongThTeBinds.integer || !self || !self->client )
	{
		return forcePower;
	}

	known = self->client->ps.fd.forcePowersKnown;
	if ( forcePower == FP_TEAM_HEAL && !( known & ( 1 << FP_TEAM_HEAL ) ) && ( known & ( 1 << FP_TEAM_FORCE ) ) )
	{
		return FP_TEAM_FORCE;
	}
	if ( forcePower == FP_TEAM_FORCE && !( known & ( 1 << FP_TEAM_FORCE ) ) && ( known & ( 1 << FP_TEAM_HEAL ) ) )
	{
		return FP_TEAM_HEAL;
	}
	return forcePower;
}

// base_enhanced g_active.c ClientThink_real (GENCMD_FORCE_HEALOTHER /
// GENCMD_FORCE_FORCEPOWEROTHER): the same redirect for the generic commands.
// qtrue: the redirected power ran; skip the stock call.
qboolean BE_RedirectThTeCmd( gentity_t *ent, int genericCmd )
{
	int known;

	if ( !g_redirectWrongThTeBinds.integer || !ent || !ent->client )
	{
		return qfalse;
	}

	known = ent->client->ps.fd.forcePowersKnown;
	if ( genericCmd == GENCMD_FORCE_HEALOTHER && !( known & ( 1 << FP_TEAM_HEAL ) ) && ( known & ( 1 << FP_TEAM_FORCE ) ) )
	{
		ForceTeamForceReplenish( ent );
		return qtrue;
	}
	if ( genericCmd == GENCMD_FORCE_FORCEPOWEROTHER && !( known & ( 1 << FP_TEAM_FORCE ) ) && ( known & ( 1 << FP_TEAM_HEAL ) ) )
	{
		ForceTeamHeal( ent );
		return qtrue;
	}
	return qfalse;
}

/*
================================================================================
Protect
================================================================================
*/

// base_enhanced g_combat.c G_Damage: the share of damage level-3 protect takes
// off health (stock 0.80; base_enhanced's former g_protectNerf).
float BE_Protect3DamageReduction( void )
{
	return g_protect3DamageReduction.value;
}
