/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

// BikeNPC.c -- VH_BIKE (xenjk wishlist item 1): a motorbike, its own vehicle type with its own function table, so
// the stock speeders, walkers, fighters and animals run none of its code (the fix must not break normal vehicles). Its movement, orientation and rider pose are pmove's (bg_bikemove.c PM_BikeVehicleMove,
// PM_BikeRiderAnimate); the shared Vehicle_t plumbing (boarding, ejecting, riders, damage, death) is the generic one
// every type gets from G_SetSharedVehicleFunctions. Game and cgame, as SpeederNPC.c.

#ifdef _GAME	// game headers are not for cgame
	#include "g_local.h"
#endif

#include "bg_public.h"
#include "bg_vehicles.h"
#include "bg_bike.h"

extern void BG_SetAnim( playerState_t *ps, animation_t *animations, int setAnimParts, int anim, int setAnimFlags );
extern int BG_GetTime( void );

#ifdef _GAME
// a think: the shared update and, when dying, the death update (the speeder's, less its move direction)
static qboolean BikeUpdate( Vehicle_t *pVeh, const usercmd_t *pUcmd )
{
	if ( !g_vehicleInfo[VEHICLE_BASE].Update( pVeh, pUcmd ) )
		return qfalse;
	if ( pVeh->m_iDieTime != 0 )
		pVeh->m_pVehicleInfo->DeathUpdate( pVeh );
	return qtrue;
}

static void BikeAnimateVehicle( Vehicle_t *pVeh )
{	// the bike's own bones are cgame's (cg_bike.c)
	(void)pVeh;
}
#endif

// moved and oriented by bg_bike.c in pmove
static void BikeProcessMoveCommands( Vehicle_t *pVeh )
{
	(void)pVeh;
}

static void BikeProcessOrientCommands( Vehicle_t *pVeh )
{
	(void)pVeh;
}

// boarding: CP2077's mount clip (or the swoop's, BG_BikeMountAnim), root at the seat, so the rider is on the tag from
// the start and it plays in full; riding poses are PM_BikeRiderAnimate's
static void BikeAnimateRiders( Vehicle_t *pVeh )
{
	animNumber_t	anim;
	int				len;

	if ( pVeh->m_iBoarding >= 0 || !pVeh->m_pPilot )
		return;
	anim = (animNumber_t)BG_BikeMountAnim( pVeh->m_pPilot->localAnimIndex, pVeh->m_iBoarding );
	len = BG_AnimLength( pVeh->m_pPilot->localAnimIndex, anim );
	pVeh->m_iBoarding = BG_GetTime() + len;
	BG_SetAnim( pVeh->m_pPilot->playerState, bgAllAnims[pVeh->m_pPilot->localAnimIndex].anims, SETANIM_BOTH, anim,
		SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD );
}

#ifndef _GAME
void AttachRidersGeneric( Vehicle_t *pVeh );
#endif

void G_SetBikeVehicleFunctions( vehicleInfo_t *pVehInfo )
{
#ifdef _GAME
	pVehInfo->AnimateVehicle			=		BikeAnimateVehicle;
	pVehInfo->AnimateRiders				=		BikeAnimateRiders;
	pVehInfo->Update					=		BikeUpdate;
#endif
	pVehInfo->ProcessMoveCommands		=		BikeProcessMoveCommands;
	pVehInfo->ProcessOrientCommands		=		BikeProcessOrientCommands;
#ifndef _GAME	// cgame prediction's attachment
	pVehInfo->AttachRiders				=		AttachRidersGeneric;
#endif
	// its handling: ext_data/vehicles/<name>.bike, read the same on game and cgame
	pVehInfo->bike						=		BG_BikeLoadParams( pVehInfo );
}

#ifdef _GAME
extern void G_AllocateVehicleObject( Vehicle_t **pVeh );
#endif

// allocate (game) or reuse (cgame) a bike's Vehicle_t, as G_CreateSpeederNPC
void G_CreateBikeNPC( Vehicle_t **pVeh, const char *strType )
{
#ifdef _GAME
	G_AllocateVehicleObject( pVeh );
#else
	if ( !*pVeh )
		*pVeh = (Vehicle_t *)BG_Alloc( sizeof( Vehicle_t ) );
#endif
	memset( *pVeh, 0, sizeof( Vehicle_t ) );
	(*pVeh)->m_pVehicleInfo = &g_vehicleInfo[BG_VehicleGetIndex( strType )];
}
