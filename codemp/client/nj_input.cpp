/*
===========================================================================
Copyright (C) 2016 - 2026, NewJK contributors (deathsythe47 et al.)
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative, and carries code ported from
NewJK (https://github.com/jkanewmod/NewJK, GPLv2).

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

// nj_input.cpp -- NewJK client input changes, quarantined.
//
// cl_fixSimultaneousInputs (NewJK b46ed795, cl_input.cpp CL_FinishMove and the
// IN_GenCMD* commands): a usercmd carries one generic command, so stock keeps
// only the last of several pressed in one frame (a "force_speed;
// force_protect" bind loses speed). With the fix the others are queued and
// sent in the following packets, one each, in an order of preference by Force
// side; protect+absorb together keeps protect. Default 1 (an exception to the
// stock-default rule); 0 is stock. Hooks in cl_input.cpp are marked
// "// NewJK:". Manifest: docs/ports/newjk.json.

#include "client.h"
#include "nj_input.h"

static cvar_t	*cl_fixSimultaneousInputs;
static uint32_t	njDesiredThisFrame;	// generic commands pressed since the last usercmd
static uint32_t	njPending;			// pressed together with another, still to send

static const genCmds_t njOrderLight[] = {
	GENCMD_SABERSWITCH, GENCMD_SABERATTACKCYCLE, GENCMD_FORCE_SPEED, GENCMD_FORCE_PROTECT, GENCMD_FORCE_ABSORB,
	GENCMD_FORCE_HEALOTHER, GENCMD_FORCE_DISTRACT, GENCMD_FORCE_HEAL, GENCMD_FORCE_THROW, GENCMD_FORCE_PULL,
	GENCMD_FORCE_SEEING, GENCMD_FORCE_RAGE, GENCMD_FORCE_FORCEPOWEROTHER, GENCMD_USE_BACTABIG, GENCMD_USE_BACTA,
	GENCMD_USE_JETPACK, GENCMD_USE_FIELD, GENCMD_USE_SENTRY, GENCMD_USE_SEEKER, GENCMD_USE_HEALTHDISP,
	GENCMD_USE_AMMODISP, GENCMD_USE_EWEB, GENCMD_USE_CLOAK, GENCMD_USE_ELECTROBINOCULARS, GENCMD_ZOOM,
	GENCMD_TAUNT, GENCMD_BOW, GENCMD_MEDITATE, GENCMD_FLOURISH, GENCMD_GLOAT, GENCMD_ENGAGE_DUEL,
};

static const genCmds_t njOrderDark[] = {
	GENCMD_SABERSWITCH, GENCMD_SABERATTACKCYCLE, GENCMD_FORCE_SPEED, GENCMD_FORCE_RAGE, GENCMD_FORCE_FORCEPOWEROTHER,
	GENCMD_FORCE_THROW, GENCMD_FORCE_PULL, GENCMD_FORCE_SEEING, GENCMD_FORCE_PROTECT, GENCMD_FORCE_ABSORB,
	GENCMD_FORCE_HEALOTHER, GENCMD_FORCE_DISTRACT, GENCMD_FORCE_HEAL, GENCMD_USE_BACTABIG, GENCMD_USE_BACTA,
	GENCMD_USE_JETPACK, GENCMD_USE_FIELD, GENCMD_USE_SENTRY, GENCMD_USE_SEEKER, GENCMD_USE_HEALTHDISP,
	GENCMD_USE_AMMODISP, GENCMD_USE_EWEB, GENCMD_USE_CLOAK, GENCMD_USE_ELECTROBINOCULARS, GENCMD_ZOOM,
	GENCMD_TAUNT, GENCMD_BOW, GENCMD_MEDITATE, GENCMD_FLOURISH, GENCMD_GLOAT, GENCMD_ENGAGE_DUEL,
};

void NJ_InputInit( void )
{
	cl_fixSimultaneousInputs = Cvar_Get( "cl_fixSimultaneousInputs", "1", CVAR_ARCHIVE,
		"NewJK: queue generic commands (Force powers, items) pressed in the same frame instead of dropping all but one" );
	njDesiredThisFrame = njPending = 0;
}

void NJ_GenCmdPressed( int genCmd )
{
	if ( genCmd > 0 && genCmd < 32 )
	{
		njDesiredThisFrame |= 1u << genCmd;
	}
}

// NewJK reads ui_forceAlignment (set by its UI); here the side comes from the
// player's own "forcepowers" userinfo, "<rank>-<side>-<levels>", side 2 dark
static const genCmds_t *NJ_Order( void )
{
	const char *fp = Cvar_VariableString( "forcepowers" );
	const char *dash = strchr( fp, '-' );

	return ( dash && dash[1] == '2' ) ? njOrderDark : njOrderLight;
}

qboolean NJ_FinishMoveGenCmd( usercmd_t *cmd )
{
	const genCmds_t *order;
	const int orderLen = ARRAY_LEN( njOrderLight );
	int i, numGenCmds = 0;

	if ( !cl_fixSimultaneousInputs || !cl_fixSimultaneousInputs->integer )
	{
		njDesiredThisFrame = njPending = 0;
		return qfalse;	// stock
	}

	if ( cl.gcmdSendValue && !njDesiredThisFrame && !cl.gcmdValue )
	{ // the command chosen earlier is already in a usercmd and waits for its packet: keep the pending ones
		return qtrue;
	}

	if ( cl.gcmdSendValue && njDesiredThisFrame )
	{ // a button pressed this frame replaces anything pending
		njPending = 0;
	}
	else if ( !cl.gcmdSendValue && njPending )
	{ // nothing pressed, commands pending: send the next of them
		cl.gcmdSendValue = qtrue;
		njDesiredThisFrame = njPending;
		njPending = 0;
	}

	if ( cl.gcmdSendValue )
	{
		order = NJ_Order();
		for ( i = GENCMD_SABERSWITCH; i <= GENCMD_GLOAT; i++ )
		{
			if ( njDesiredThisFrame & ( 1u << i ) )
				++numGenCmds;
		}

		// protect and absorb cancel each other: probably a panic press, keep protect
		if ( numGenCmds > 1 && ( njDesiredThisFrame & ( 1u << GENCMD_FORCE_ABSORB ) ) &&
			( njDesiredThisFrame & ( 1u << GENCMD_FORCE_PROTECT ) ) )
		{
			njDesiredThisFrame &= ~( 1u << GENCMD_FORCE_ABSORB );
			--numGenCmds;
		}

		if ( numGenCmds > 1 )
		{ // send the most preferred now, remember the others
			qboolean executed = qfalse;

			njPending = 0;
			for ( i = 0; i < orderLen; i++ )
			{
				if ( njDesiredThisFrame & ( 1u << order[i] ) )
				{
					if ( !executed )
					{
						cmd->generic_cmd = order[i];
						executed = qtrue;
					}
					else
					{
						njPending |= 1u << order[i];
					}
				}
			}
		}
		else
		{
			if ( cl.gcmdValue )
			{ // the button pressed this frame
				cmd->generic_cmd = cl.gcmdValue;
			}
			else
			{ // the last pending command
				for ( i = 0; i < orderLen; i++ )
				{
					if ( njDesiredThisFrame & ( 1u << order[i] ) )
					{
						cmd->generic_cmd = order[i];
						break;
					}
				}
			}
			njPending = 0;
		}
		cl.gcmdSentValue = qtrue;
	}
	else
	{
		cmd->generic_cmd = 0;
		njPending = 0;
	}

	njDesiredThisFrame = 0;
	cl.gcmdValue = 0;
	return qtrue;
}
