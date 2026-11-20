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

// g_damagefeedback.h -- damage numbers for the attacker (g_damagefeedback.c).
#pragma once

// G_Damage, after `take` is subtracted from the target's health: a score plum
// above the target for the attacker, and the `dmg` console command for an
// attacker whose userinfo asks for it (pers.damageLog).
void G_DamageFeedback( gentity_t *targ, gentity_t *attacker, int take, int mod );
