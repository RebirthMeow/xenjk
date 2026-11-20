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

// be_combat.h -- base_enhanced fight, weapon and spawn changes, quarantined.
//
// Server-only (no shared bg code). Each is switched by a cvar in g_xcvar.h
// whose default is the competitive profile; stock values
// are in tools/fixtures/competitive/stock_force_profile.cfg. Stock files call
// in through one-line hooks marked "// base_enhanced:". Manifest:
// docs/ports/base_enhanced.json. Included at the end of g_local.h.

#pragma once

// frame (be_force.c BE_RunFrame)
void		BE_CombatInit( void );								// BE_InitGame
void		BE_CombatFrame( void );

// g_combat.c
qboolean	BE_LocationDamage( int hitLoc, int *damage );			// qtrue: mode 2 applied, skip stock's table
int			BE_SaberOneshotsExplosive( gentity_t *targ, gentity_t *attacker, int mod, int damage );
void		BE_Disarmed( gentity_t *self );							// TossClientWeapon

// g_active.c
void		BE_DisarmFiring( gentity_t *ent, usercmd_t *ucmd );

// g_weapon.c
int			BE_ChargeTime( gentity_t *ent );						// stock: level.time - ps.weaponChargeTime
int			BE_HomingForcedLock( gentity_t *ent, qboolean altFire );	// entity number, -1 none
qboolean	BE_SnipeThroughCorpse( gentity_t *traceEnt );
int			BE_ThermalDirectDamage( int stockDamage );

// g_items.c
qboolean	BE_FreshWeaponFullAmmo( void );

// g_team.c / g_client.c spawn selection
int			BE_FilterSpawnsNearDroppedFlag( gentity_t **spots, int count );
