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
Garment cloth: rd-vulkan back end (vk_cloth.cpp). The common glue is rd-common/tr_cloth.h.
*/
#ifndef VK_CLOTH_H
#define VK_CLOTH_H

class CBoneCache;

// a cloth surface's substitute draw: the instance's cloth vertices in the Ghoul2 VBO layout
typedef struct {
	VBO_t *vbo;
	IBO_t *ibo;
	int indexOffset;  // first index (in indices) of this surface in ibo
	int numIndexes;
	int minIndex, maxIndex;
	int numVertexes;
} clothVkDraw_t;

void R_ClothVkBeginFrame( void );  // vk_begin_frame: the dynamic geometry buffer starts over

// RB_SurfaceGhoul: qtrue if this LOD 0 surface of the instance is simulated cloth; fills *out
qboolean RB_ClothVkSurface( CBoneCache *boneCache, const mdxmSurface_t *surface, clothVkDraw_t *out );
// qtrue if this LOD 0 surface is simulated cloth (a cheap test: no upload)
qboolean RB_ClothVkIsSimSurface( CBoneCache *boneCache, const mdxmSurface_t *surface );

#endif // VK_CLOTH_H
