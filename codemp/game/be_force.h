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

// be_force.h -- base_enhanced Force-power changes, quarantined.
//
// Every behaviour here is switched by a cvar in g_xcvar.h; the defaults are the
// competitive profile, 0/stock values restore OpenJK (see be_force.c).
// Stock files only call in through small hooks marked
// "// base_enhanced: <feature> (be_force.c)". Upstream snapshot, commits and
// mirrored functions: docs/ports/base_enhanced.json. The competitive
// profile is a cvar set: tools/fixtures/competitive/base_enhanced_profile.cfg.
//
// Included at the end of g_local.h. The port needs g_tick.c: the force timers use
// G_TickNominalFrameMsec and the G_TickStrict* helpers.

#pragma once

// base_enhanced bg_public.h: CS_SERVERFEATURELIST is CS_BOTINFO (25), unused by xenjk
#define BE_CS_SERVERFEATURELIST		CS_BOTINFO

// game frame (g_main.c)
void		BE_InitGame( void );
void		BE_RunFrame( void );

// grip (w_force.c)
int			BE_GripUseCooldown( int stockMs );
// g_fixForceTimers: a timed power's duration as base_enhanced sets it, and its end test.
int			BE_FixDuration( int duration, int sinceLastFrame );
int			BE_FixPowerDuration( int forcePower, int duration, int sinceLastFrame );	// timed powers only
extern int	g_LastFrameTime;	// g_main.c: level.time at the end of the last G_RunFrame
qboolean	BE_ForceTimerEnded( int end, int time );
qboolean	BE_GripReplacesStock( void );
void		BE_ForceGrip( gentity_t *self );
void		BE_DoGripAction( gentity_t *self, forcePowers_t forcePower );
qboolean	BE_GripRun( gentity_t *self, forcePowers_t forcePower );
void		BE_GripStarted( gentity_t *self );
qboolean	BE_GrippedCannotAttack( gentity_t *self );
qboolean	BE_GripAbsorbFix( void );

// push/pull (w_force.c ForcePowerUsableOn)
qboolean	BE_PushKnockedDown( void );

// drain (w_force.c)
void		BE_ForceDrain( gentity_t *self );
int			BE_ForceShootDrain( gentity_t *self );
qboolean	BE_DrainAvailable( gentity_t *self );
qboolean	BE_DrainDebuffBlocks( gentity_t *self, forcePowers_t forcePower );
qboolean	BE_DrainRun( gentity_t *self );
qboolean	BE_DrainStop( gentity_t *self );

// team heal / team energize (w_force.c, g_active.c)
qboolean		BE_TeamPowerTargetOk( gentity_t *self, gentity_t *ent, forcePowers_t forcePower );
forcePowers_t	BE_RedirectThTePower( gentity_t *self, forcePowers_t forcePower );
qboolean		BE_RedirectThTeCmd( gentity_t *ent, int genericCmd );

// protect (g_combat.c G_Damage)
float		BE_Protect3DamageReduction( void );
