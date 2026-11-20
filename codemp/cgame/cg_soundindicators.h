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

// cg_soundindicators.h -- directional sound indicators
//
// This is a self-contained module. To integrate into another OpenJK / idTech 3
// fork the only OpenJK files that need to be touched are:
//
//   cg_event.c    : add ONE call to CG_SoundIndicators_OnEntityEvent() at
//                   the top of CG_EntityEvent(), right after the EV_NONE
//                   early-return.
//   cg_main.c     : add ONE call to CG_SoundIndicators_Init() inside CG_Init.
//   cg_draw.c     : add ONE call to CG_SoundIndicators_Draw() near the end of
//                   CG_Draw2D, and (optionally) ONE call to
//                   CG_SoundIndicators_DrawOnRadar() inside CG_DrawRadar to
//                   enable the radar-overlay mode.
//   cg_local.h    : add `#include "cg_soundindicators.h"` (or include this
//                   header directly from the modules that use the API).
//   cg_xcvar.h    : add the three cvars below (cg_soundIndicators,
//                   cg_soundIndicatorScale, cg_soundIndicatorRadar) using the
//                   XCVAR_DEF idiom this codebase already uses.
//   CMakeLists    : add cg_soundindicators.c (and this .h) to the cgame
//                   source list.
//
// Everything else lives in cg_soundindicators.c. No changes are required to
// the renderer, server, or any vmCvar plumbing beyond the three cvar lines.

#pragma once

#include "qcommon/q_shared.h"

// Categories of sound that can be visualised on the HUD indicator. Each
// category has its own default colour + icon (see cg_soundindicators.c).
typedef enum {
	SI_TYPE_WEAPON,      // blaster / saber / rifle / etc. fire
	SI_TYPE_EXPLOSION,   // missile impact, det pack, thermal
	SI_TYPE_FOOTSTEP,    // footstep, jump, roll, swim
	SI_TYPE_FORCE,       // force power activation
	SI_TYPE_USE,         // door open, item pickup (icon varies per item)
	SI_TYPE_MOVER,       // elevator / lift / brushmodel motion sounds
	SI_TYPE_MAX
} soundIndicatorType_t;

// --- lifecycle ------------------------------------------------------------

// Call once from CG_Init. Clears the event ring and registers the icons we
// own so the radar/use indicators always have valid handles.
void CG_SoundIndicators_Init( void );

// --- main hook (the only thing cg_event.c needs to call) -----------------

// Single observer hook. Drop ONE call to this at the top of
// CG_EntityEvent(), right after the `if ( !event ) return;` guard. This
// function inspects the event and dispatches to the appropriate
// CG_SoundIndicators_Add* helper, including the per-item icon dispatch on
// EV_ITEM_PICKUP. Safe to call for any event: unknown events are ignored.
void CG_SoundIndicators_OnEntityEvent( centity_t *cent, vec3_t position );

// --- drawing --------------------------------------------------------------

// Centre-of-screen ring. Call from CG_Draw2D once per frame, near the end
// (after HUD, before scoreboard so it doesn't clip).
void CG_SoundIndicators_Draw( void );

// Radar-overlay mode. Call from inside CG_DrawRadar() right before its
// return, passing the radar's centre + radius + cg_radarRange. No-op if
// the radar-overlay cvar is disabled, so calling unconditionally is fine.
void CG_SoundIndicators_DrawOnRadar( float centerX, float centerY, float radius, float radarRange );

// --- low-level event injection (for callers who want custom hooks) -------

// Most users will never need these; CG_SoundIndicators_OnEntityEvent
// covers the standard JKA event set. These exist so mods that introduce
// custom sound events can flag them on the indicator without modifying
// this module.

void CG_SoundIndicators_Add( const vec3_t origin, soundIndicatorType_t type, int sourceEntity );
void CG_SoundIndicators_AddEnt( int entityNum, soundIndicatorType_t type );
void CG_SoundIndicators_AddWithIcon( const vec3_t origin, soundIndicatorType_t type, int sourceEntity, qhandle_t customIcon );
