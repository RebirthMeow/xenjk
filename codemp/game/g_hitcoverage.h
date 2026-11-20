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
===========================================================================
Direct-hit coverage laboratory for multiplayer projectile traces.

This is deliberately isolated from gameplay code.  It lets a local server
sample the same swept-box trace used by rockets against a player's current
server-side pose.
===========================================================================
*/

#ifndef G_HITCOVERAGE_H
#define G_HITCOVERAGE_H

void Svcmd_HitCoverage_f( void );

#endif
