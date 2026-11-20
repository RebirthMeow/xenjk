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
Hair chains: rd-vulkan back end (vk_hair.cpp). The common glue is rd-common/tr_hair.h.
*/
#ifndef VK_HAIR_H
#define VK_HAIR_H

class CBoneCache;

// bone upload (RB_TransformBones): the hair palette matrices this bone cache draws with this frame, appended after the
// skeleton's numGlaBones bones; 0 when the instance has no hair this frame
int RB_HairVkPaletteCount( const CBoneCache *bc, int numGlaBones );
// write them (count from RB_HairVkPaletteCount), 12 floats each (3x4 rows, as the skeleton's bones)
void RB_HairVkPaletteWrite( const CBoneCache *bc, float *dst, int count );
// r_charOccRect: extend a model-space box (mins, maxs) by the hair bones' posed joints, from the written palette
void RB_HairVkJointBox( const CBoneCache *bc, const float *palette, int count, float *mins, float *maxs );

// RB_SurfaceGhoul: this LOD's substitute vertex buffer when the surface is listed, the instance has hair this frame and
// the LOD is not patched into the model's own buffer (r_hairSharedVbo 0), else NULL (same layout and index buffer as the
// model's own LOD buffer)
VBO_t *RB_HairVkSurfaceVBO( const CBoneCache *bc, const mdxmVBOMesh_t *vboMesh, int surfaceIndex );

#endif // VK_HAIR_H
