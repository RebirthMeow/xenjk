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

// nj_input.h -- NewJK client input changes, quarantined (nj_input.cpp).

#pragma once

void		NJ_InputInit( void );
void		NJ_GenCmdPressed( int genCmd );				// every IN_GenCMD* and IN_UseGivenForce
qboolean	NJ_FinishMoveGenCmd( usercmd_t *cmd );		// CL_FinishMove: qfalse = stock
