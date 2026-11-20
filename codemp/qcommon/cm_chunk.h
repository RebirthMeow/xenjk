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

#pragma once

// cm_chunk.h -- private to the collision model: the calls between cm_load.cpp (the lump loaders) and cm_chunk.cpp
// (collision chunks). cm_public.h has the engine's, cm_local.h the traces'.

// cm_chunk.cpp
void		CM_LoadMapChunks( const char *name, qboolean clientload );	// the map's own manifest (CM_LoadMap)
void		CM_ClearChunks( void );										// CM_ClearMap
qboolean	CM_ValidateChunk( const byte *buf, int len, char *err, int errLen );	// before CM_ParseChunk
void		CM_FreeChunk( cmChunk_t *c );

// cm_load.cpp
cmChunk_t	*CM_ParseChunk( byte *buf, int len, const char *name, const vec3_t origin, char *err, int errLen );
