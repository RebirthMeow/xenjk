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

// be_combat.c -- base_enhanced fight, weapon and spawn changes, quarantined.
//
// Ported from base_enhanced 774700c (game/g_combat.c, g_weapon.c, g_items.c,
// g_active.c, g_team.c, g_main.c); each function names what it mirrors.
// Server-only. Defaults are the competitive profile; stock values:
// g_locationBasedDamage 1, g_droppedFlagSpawnProtectionRadius 0,
// g_fixWeaponChargeTime 0, g_improvedHoming 0, g_fixCorpseSniping 0,
// g_fixFreshWeaponAmmo 0, g_fixDisarmFiring 0, g_beThermalDirectDamage 0,
// g_beSaberOneshotsExplosives 0.

#include "g_local.h"

/*
================================================================================
Location damage (g_locationBasedDamage 2)
================================================================================
*/

// base_enhanced g_combat.c G_LocationBasedDamageModifier, mode 2: legs and
// feet 0.7, torso, arms and hands full, head 1.3 (stock/mode 1: feet 0.5,
// legs 0.7, arms 0.85, hands 0.6, head 1.3). Splash keeps location damage,
// as in stock (upstream g_locationBasedDamage_splash 1).
qboolean BE_LocationDamage( int hitLoc, int *damage )
{
	if ( g_locationBasedDamage.integer < 2 )
	{
		return qfalse;
	}
	switch ( hitLoc )
	{
	case HL_FOOT_RT: case HL_FOOT_LT: case HL_LEG_RT: case HL_LEG_LT:
		*damage *= 0.7;
		break;
	case HL_HEAD:
		*damage *= 1.3;
		break;
	default:
		break;	// torso, arms and hands: normal damage
	}
	return qtrue;
}

/*
================================================================================
Sabers one-shot mines and detpacks (g_beSaberOneshotsExplosives; upstream always on)
================================================================================
*/

// base_enhanced 79b1ab8, G_Damage: "fix sabers rarely not oneshotting mines/detpacks"
int BE_SaberOneshotsExplosive( gentity_t *targ, gentity_t *attacker, int mod, int damage )
{
	if ( g_beSaberOneshotsExplosives.integer && mod == MOD_SABER && targ && targ->classname &&
		( !strcmp( targ->classname, "laserTrap" ) || !strcmp( targ->classname, "detpack" ) ) &&
		attacker && attacker->client && attacker - g_entities < MAX_CLIENTS )
	{
		return 999;
	}
	return damage;
}

/*
================================================================================
Disarm firing (g_fixDisarmFiring)
================================================================================
*/

// base_enhanced g_active.c ClientThink_real: after a weapon is pulled away the
// held fire button would fire the auto-selected weapon at once. Attack is
// cancelled until the button is released, for up to 5 s; holds that only
// charge (bryar and old bryar alt, bowcaster primary, demp2 alt) are allowed.
// Upstream times it with trap_Milliseconds (real time); here level.time, so
// accelerated bot worlds keep the same 5 s and stay replayable.
static int beDisarmedTime[MAX_CLIENTS];
static int beDisarmNoAttack[MAX_CLIENTS];

void BE_Disarmed( gentity_t *self )
{
	int n = self - g_entities;

	if ( g_fixDisarmFiring.integer && n >= 0 && n < MAX_CLIENTS )
	{
		beDisarmedTime[n] = level.time;
	}
}

static qboolean BE_ChargeOnlyHold( gclient_t *client, qboolean m1, qboolean m2 )
{
	switch ( client->ps.weapon )
	{
	case WP_BRYAR_PISTOL: case WP_DEMP2: case WP_BRYAR_OLD:
		return m2 && !m1;
	case WP_BOWCASTER:
		return m1 && !m2;
	default:
		return qfalse;
	}
}

void BE_DisarmFiring( gentity_t *ent, usercmd_t *ucmd )
{
	gclient_t *client = ent->client;
	int n = ent - g_entities;
	qboolean m1, m2;

	if ( !g_fixDisarmFiring.integer || level.gametype == GT_SIEGE || n < 0 || n >= MAX_CLIENTS )
	{
		return;
	}
	m1 = !!( ucmd->buttons & BUTTON_ATTACK );
	m2 = !!( ucmd->buttons & BUTTON_ALT_ATTACK );
	if ( ent->health <= 0 || client->ps.pm_type == PM_DEAD )
	{
		return;
	}
	if ( beDisarmedTime[n] && level.time - beDisarmedTime[n] < 5000 && ( m1 || m2 ) )
	{
		if ( BE_ChargeOnlyHold( client, m1, m2 ) )
		{
			beDisarmNoAttack[n] = 0;
		}
		else
		{ // cancel until the button is really released, or 5 s pass
			beDisarmNoAttack[n] = level.time;
			ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
		}
	}
	else if ( beDisarmNoAttack[n] )
	{
		if ( BE_ChargeOnlyHold( client, m1, m2 ) )
		{
			beDisarmNoAttack[n] = 0;
		}
		else if ( level.time - beDisarmNoAttack[n] < 5000 )
		{
			if ( !m1 && !m2 )
				beDisarmNoAttack[n] = 0;
			else
				ucmd->buttons &= ~( BUTTON_ATTACK | BUTTON_ALT_ATTACK );
		}
		else
		{
			beDisarmNoAttack[n] = 0;
		}
	}
}

/*
================================================================================
Weapons
================================================================================
*/

// base_enhanced g_weapon.c (bryar, disruptor, bowcaster, demp2, thermal):
// ps.weaponChargeTime is set from the command's serverTime (bg_pmove.c), so
// measuring the charge against level.time mixes two clocks and the charge
// swings with ping and frame phase. g_fixWeaponChargeTime measures it on the
// command clock.
int BE_ChargeTime( gentity_t *ent )
{
	if ( g_fixWeaponChargeTime.integer )
	{
		return ent->client->pers.cmd.serverTime - ent->client->ps.weaponChargeTime;
	}
	return level.time - ent->client->ps.weaponChargeTime;
}

// base_enhanced g_main.c RunImprovedHoming / g_weapon.c WP_FireRocket: a full
// lock (the stage stock needs at the moment of firing) is remembered, and an
// alt rocket fired within g_improvedHomingThreshold ms still homes on it.
// Upstream's lock-stage bits in holocronBits (for newmod's HUD) are not ported.
static int beHomingLockTime[MAX_CLIENTS];
static int beHomingLockTarget[MAX_CLIENTS];

static void BE_HomingFrame( void )
{
	const float lockTimeInterval = ( ( level.gametype == GT_SIEGE ) ? 2400.0f : 1200.0f ) / 16.0f;
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		gclient_t *cl = &level.clients[i];
		float rTime;
		int dif;

		if ( !g_improvedHoming.integer || i >= level.maxclients || cl->pers.connected != CON_CONNECTED ||
			cl->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0 ||
			cl->ps.pm_type == PM_SPECTATOR || cl->ps.pm_type == PM_INTERMISSION )
		{
			beHomingLockTime[i] = 0;
			continue;
		}
		if ( cl->ps.rocketLockIndex == ENTITYNUM_NONE )
		{
			continue;
		}
		rTime = cl->ps.rocketLockTime == -1 ? cl->ps.rocketLastValidTime : cl->ps.rocketLockTime;
		dif = (int)( ( level.time - rTime ) / lockTimeInterval );
		if ( dif >= 10 )
		{
			beHomingLockTime[i] = level.time;
			beHomingLockTarget[i] = cl->ps.rocketLockIndex;
		}
	}
}

int BE_HomingForcedLock( gentity_t *ent, qboolean altFire )
{
	int n = ent - g_entities;
	int threshold;

	if ( !g_improvedHoming.integer || !altFire || !ent->client || n < 0 || n >= MAX_CLIENTS || !beHomingLockTime[n] )
	{
		return -1;
	}
	threshold = g_improvedHomingThreshold.integer > 0 ? g_improvedHomingThreshold.integer : ( level.gametype == GT_SIEGE ? 150 : 75 );
	if ( level.time - beHomingLockTime[n] > threshold || beHomingLockTarget[n] == ENTITYNUM_NONE )
	{
		return -1;
	}
	return beHomingLockTarget[n];
}

// base_enhanced g_weapon.c WP_DisruptorAltFire: a snipe continues through a corpse
qboolean BE_SnipeThroughCorpse( gentity_t *traceEnt )
{
	return g_fixCorpseSniping.integer && traceEnt && traceEnt->classname && !Q_stricmp( traceEnt->classname, "bodyque" );
}

// base_enhanced 22e025f (always on there): a thermal's direct hit does 90, the
// most its splash can, instead of 70
int BE_ThermalDirectDamage( int stockDamage )
{
	return g_beThermalDirectDamage.integer ? 90 : stockDamage;
}

// base_enhanced g_items.c Pickup_Weapon, g_fixFreshWeaponAmmo 1: stock's
// respawning-weapon rule indexes ammo by weapon tag (a bug), so the amount
// given varies; the fix gives the item's full quantity.
qboolean BE_FreshWeaponFullAmmo( void )
{
	return g_fixFreshWeaponAmmo.integer != 0;
}

/*
================================================================================
Dropped-flag spawn protection (g_droppedFlagSpawnProtectionRadius/Duration)
================================================================================
*/

// base_enhanced g_team.c TooCloseToDroppedFlag: a spawn within the radius of a
// dropped flag, and in its PVS, is not used for the duration after the drop
// (inferred, as upstream, from the 30 s auto-return think); the last spawn
// left is always kept. Upstream's other spawn filters (allied flag carrier,
// where you were just killed) default off there and are not ported.
static qboolean BE_TooCloseToDroppedFlag( gentity_t *spawnPoint )
{
	const int duration = Com_Clampi( 1000, 30000, g_droppedFlagSpawnProtectionDuration.integer );
	const float radiusSquared = (float)g_droppedFlagSpawnProtectionRadius.integer * (float)g_droppedFlagSpawnProtectionRadius.integer;
	int i;

	for ( i = MAX_CLIENTS; i < MAX_GENTITIES; i++ )
	{
		gentity_t *flag = &g_entities[i];

		if ( !flag->inuse || !flag->item || !( flag->flags & FL_DROPPED_ITEM ) )
			continue;
		if ( flag->item->giTag != PW_REDFLAG && flag->item->giTag != PW_BLUEFLAG && flag->item->giTag != PW_NEUTRALFLAG )
			continue;
		if ( level.time > flag->nextthink - 30000 + duration )
			continue;
		if ( DistanceSquared( spawnPoint->r.currentOrigin, flag->r.currentOrigin ) <= radiusSquared &&
			trap->InPVS( spawnPoint->r.currentOrigin, flag->r.currentOrigin ) )
			return qtrue;
	}
	return qfalse;
}

int BE_FilterSpawnsNearDroppedFlag( gentity_t **spots, int count )
{
	int i, kept = 0;

	if ( g_droppedFlagSpawnProtectionRadius.integer <= 0 || count <= 1 )
	{
		return count;
	}
	for ( i = 0; i < count; i++ )
	{
		if ( !BE_TooCloseToDroppedFlag( spots[i] ) )
			spots[kept++] = spots[i];
	}
	if ( !kept )
	{
		return count;	// all too close: keep them all (upstream keeps the last one in its list)
	}
	return kept;
}

/*
================================================================================
Frame
================================================================================
*/

void BE_CombatFrame( void )
{
	BE_HomingFrame();
}

void BE_CombatInit( void )
{
	memset( beDisarmedTime, 0, sizeof( beDisarmedTime ) );
	memset( beDisarmNoAttack, 0, sizeof( beDisarmNoAttack ) );
	memset( beHomingLockTime, 0, sizeof( beHomingLockTime ) );
}
