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
Garment cloth on Ghoul2 models (renderer glue for rd-common/cloth, shared by rd-vanilla and rd-vulkan).

A GLM "models/x/model.glm" gets cloth when a sidecar "models/x/model.cloth"
(SKA1 or SKA2, see rd-common/cloth/cloth_asset.h) exists. Every Ghoul2 instance that
renders that model owns one solver, keyed by its CBoneCache. The front end
steps it at a fixed rate from the scene time (R_ClothAddGhoul, after the bones
are transformed) and builds a model-space draw buffer (tr_cloth_local.h). Each
renderer's back end feeds that buffer to its own Ghoul2 path:
  rd-vanilla: RB_ClothSurface replaces the CPU-skinned positions and normals in tess;
  rd-vulkan:  RB_ClothVkSurface hands RB_SurfaceGhoul a per-instance vertex stream in the
              Ghoul2 VBO layout that the GPU skinning shaders draw (vk_cloth.cpp).
*/
#ifndef TR_CLOTH_H
#define TR_CLOTH_H

#include "rd-common/tr_g2ext.h"	// R_ClothEvalBone

void R_ClothRegister( void );                         // cvars, from R_Register
void R_ClothShutdown( void );                         // frees every solver and asset, from RE_Shutdown
void R_ClothWarmModel( const model_t *mod );          // the model's cloth asset loaded and bound (model warm-up)
void R_ClothForgetBoneCache( const CBoneCache *bc );  // a Ghoul2 instance's bone cache is going away

// front end: for every view that reaches a Ghoul2 model, after its bones are transformed. personalModel: this view
// does not draw it (the first-person body); shadowView: a sun cascade or character shadow view. The first view of a
// frame that draws the model decides and steps; the others reuse that (rd-common/tr_sim.h)
void R_ClothAddGhoul( const trRefEntity_t *ent, const model_t *mod, const mdxaHeader_t *aHeader,
	CBoneCache *boneCache, qboolean personalModel, qboolean shadowView );

// rd-vanilla back end: after RB_SurfaceGhoul skinned 'surface' into tess at baseVertex
void RB_ClothSurface( const CBoneCache *boneCache, const mdxmSurface_t *surface, int baseVertex );

// load cost by stage, summed since the renderer started (one clock read per load, so always on); rd-vulkan's
// `loadprofile` prints them. asset: .cloth read and parse; inst: solver instance (topology, tethers); first: an
// instance's first front-end step (the GPU solver's setup included)
enum { CLOTHLOAD_ASSET, CLOTHLOAD_INST, CLOTHLOAD_FIRST, CLOTHLOAD_COUNT };
extern double	r_clothLoadMs[CLOTHLOAD_COUNT];
extern int		r_clothLoadCount[CLOTHLOAD_COUNT];

#endif // TR_CLOTH_H
