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

This module observes weapon-fire events while a demo is playing, records the
chosen target's subsequent snapshot path, and evaluates counterfactual
first-flight direct contacts.  Splash damage and post-bounce contacts are
deliberately outside the first implementation.
=========================================================================
*/

#pragma once

void CG_InitAimFeedback( void );
void CG_ShutdownAimFeedback( void );
void CG_AimFeedback_OnEntityEvent( centity_t *cent, int event );
void CG_AimFeedback_Update( void );
void CG_AimFeedback_Draw( void );
qboolean CG_AimFeedback_HidesDisconnect( void );	// CG_DrawDisconnect
qboolean CG_AimFeedback_HidesCenterPrint( void );	// CG_DrawCenterString
