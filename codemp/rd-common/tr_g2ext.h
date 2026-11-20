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
What each renderer's Ghoul2 code (its tr_ghoul2.cpp) gives the systems that ride on its bone caches: garment cloth
(tr_cloth.cpp), hair chains (tr_hair.cpp, rd-vulkan) and their back ends.
*/
#ifndef TR_G2EXT_H
#define TR_G2EXT_H

class CBoneCache;

// the render bone matrix (EvalRender) of gla bone 'index', in the entity's model space
const mdxaBone_t *R_ClothEvalBone( CBoneCache *boneCache, int index );
// the model a bone cache was built for (rd-vulkan)
const model_t *R_BoneCacheModel( const CBoneCache *boneCache );

#endif // TR_G2EXT_H
