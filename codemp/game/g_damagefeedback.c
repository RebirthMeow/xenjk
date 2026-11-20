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

// g_damagefeedback.c -- damage numbers for the attacker (see g_damagefeedback.h).

#include "g_local.h"
#include "g_damagefeedback.h"

void ScorePlum( gentity_t *ent, vec3_t origin, int score );	// g_combat.c

void G_DamageFeedback( gentity_t *targ, gentity_t *attacker, int take, int mod )
{
	if ( !attacker || !attacker->inuse || !attacker->client || take <= 0 )
		return;

	// The plum is sent only to snapshots whose ps.clientNum is the attacker (the attacker and anyone
	// following them); an NPC attacker is never one, so its plum would reach nobody.
	if ( attacker->s.number < MAX_CLIENTS ) {
		vec3_t pOrg;
		VectorCopy( targ->r.currentOrigin, pOrg );
		pOrg[2] += targ->r.maxs[2]; // Spawn slightly above the target's head
		ScorePlum( attacker, pOrg, -take );
	}

	// Send damage info to the attacker's console via custom server command 'dmg', only to a client whose
	// cg_scorePlums (userinfo) is 2 or more: one reliable command a hit overflows a lagging client's buffer and
	// stock clients print an unknown command
	if ( attacker->client->pers.damageLog )
		trap->SendServerCommand( attacker->s.number, va( "dmg \"%s\" \"%s\" %i %i",
			attacker->client->pers.netname,
			( targ->client && targ->inuse ) ? targ->client->pers.netname : ( targ->classname ? targ->classname : "unknown" ),
			take, mod ) );
}
