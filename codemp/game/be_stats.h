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

// be_stats.h -- base_enhanced CTF statistics, quarantined (be_stats.c).
//
// Server-only and passive: the hooks read game state and write only this
// module's own memory (no entity, playerState, configstring, RNG or timing
// change), so a world replays the same with g_beStats 1 or 0. Stock files
// call in through one-line hooks marked "// base_enhanced: stats (be_stats.c)".
// Bots are tracked like players. Included at the end of g_local.h.

#pragma once

// g_main.c
void	BE_StatsInit( void );												// G_InitGame
void	BE_StatsShutdown( void );											// G_ShutdownGame
void	BE_StatsFrame( void );												// G_RunFrame, after ClientEndFrame
void	BE_StatsIntermission( void );										// BeginIntermission: tables to everyone

// g_client.c
void	BE_StatsClientConnect( int clientNum, const char *guid, qboolean isBot );	// ClientConnect

// g_cmds.c / g_svcmds.c
void	BE_Cmd_Stats_f( gentity_t *ent );									// stats, ctfstats, stat (also in intermission)
void	BE_Svcmd_Stats_f( void );											// rcon/console stats, ctfstats

// g_combat.c
void	BE_StatsPlayerDie( gentity_t *self, gentity_t *attacker, int meansOfDeath );	// after the pit-killer attribution
void	BE_StatsProtect( gentity_t *targ, gentity_t *attacker, int mod, int avoided );
void	BE_StatsDamage( gentity_t *targ, gentity_t *attacker, int mod, int take, int asave );	// before "do the damage"

// g_team.c
void	BE_StatsFcKill( gentity_t *attacker );								// Team_FragBonuses, carrier fragged
void	BE_StatsDefend( gentity_t *attacker );								// Team_FragBonuses, PERS_DEFEND_COUNT sites
void	BE_StatsFlagReset( int team );										// Team_ResetFlag
void	BE_StatsFlagReturned( gentity_t *returner, int team );				// Team_TouchOurFlag, dropped flag
void	BE_StatsCapture( gentity_t *capper );								// Team_TouchOurFlag, capture
void	BE_StatsAssist( gentity_t *player );								// Team_TouchOurFlag, PERS_ASSIST_COUNT sites
void	BE_StatsFlagTaken( gentity_t *taker, gentity_t *flag, int team );	// Team_TouchEnemyFlag

// g_items.c
void	BE_StatsPowerup( gentity_t *other, gentity_t *item );				// Pickup_Powerup (boon)
void	BE_StatsHealthPickup( gentity_t *other, int quantity, int max );	// Pickup_Health, before the add
void	BE_StatsArmorPickup( gentity_t *other, int quantity, int max );		// Pickup_Armor, before the add

// g_weapon.c / g_missile.c (accuracy, air shots)
void	BE_StatsShot( gentity_t *ent, qboolean altFire );					// FireWeapon
void	BE_StatsPistolCharge( gentity_t *ent, int count );					// WP_FireBryarPistol alt
void	BE_StatsHitscan( gentity_t *ent, int category );					// disruptor, snipe, conc alt hits
void	BE_StatsGolanBall( gentity_t *ball );								// WP_CreateFlechetteBouncyThing
void	BE_StatsMissileHit( gentity_t *missile, gentity_t *victim );		// G_MissileImpact, before the damage
void	BE_StatsSplashHit( gentity_t *missile, gentity_t *owner, qboolean ownerMustFire );	// G_RadiusDamage hit a client

// accuracy categories for BE_StatsHitscan (mirrors upstream accuracyCategory_t)
#define BE_ACC_DISRUPTOR_PRIMARY	2
#define BE_ACC_DISRUPTOR_SNIPE		3
#define BE_ACC_CONCUSSION_ALT		8

// w_force.c / be_force.c
void	BE_StatsPushPull( gentity_t *self, qboolean pull );				// ForceThrow
void	BE_StatsTeamHeal( gentity_t *self, gentity_t *ally, int amount );	// ForceTeamHeal, before the add
void	BE_StatsTeamEnergize( gentity_t *self, gentity_t *ally, int amount );	// ForceTeamForceReplenish, before the add
void	BE_StatsTeamEnergizeDone( gentity_t *self, int amount );			// ForceTeamForceReplenish, after the loop
void	BE_StatsAbsorb( gentity_t *absorber, gentity_t *attacker, int amount );	// WP_AbsorbConversion, before the add
void	BE_StatsGrip( gentity_t *self, gentity_t *victim );					// ForceGrip / BE_ForceGrip, grip starts
void	BE_StatsDrain( gentity_t *self, gentity_t *victim, int amount );	// ForceDrainDamage / BE_ForceDrainDamage
