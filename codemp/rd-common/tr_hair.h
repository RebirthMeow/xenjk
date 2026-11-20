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
Hair chains on Ghoul2 models (renderer glue for rd-common/hairchains; rd-vulkan only).

A GLM "models/x/model.glm" gets simulated hair when a sidecar "models/x/model.chains" (HCH1 v2, docs/hair/FORMAT.md)
exists. Every Ghoul2 instance that renders that model owns one solver, keyed by its CBoneCache. The front end steps
it at the asset's fixed rate from the scene time (R_HairAddGhoul, after the bones are transformed) and leaves this
frame's hair bone skinning matrices on the instance. rd-vulkan draws them through the GPU skinning palette: the
model's own bones first, then the hair bones. The listed vertices' bone references point at those palette slots: by
default they are written into the model's own vertex buffer (r_hairSharedVbo 1), else the listed surfaces draw from a
substitute copy of it (vk_hair.cpp).
*/
#ifndef TR_HAIR_H
#define TR_HAIR_H

class CBoneCache;

void R_HairRegister( void );                         // cvars, from R_Register
void R_HairShutdown( void );                         // frees every solver and asset, from RE_Shutdown
void R_HairWarmModel( const model_t *mod );          // the model's hair asset loaded and bound (model warm-up)
bool R_HairWarmModelGpu( const model_t *mod );       // one of its hair LOD buffers (patch or copy) for its own skeleton; true when none is left
void R_HairForgetBoneCache( const CBoneCache *bc );  // a Ghoul2 instance's bone cache is going away

// front end: for every view that reaches a Ghoul2 model, after its bones are transformed. personalModel: this view
// does not draw it (the first-person body); shadowView: a sun cascade or character shadow view. The first view of a
// frame that draws the model decides and steps; the others reuse that (rd-common/tr_sim.h)
void R_HairAddGhoul( const trRefEntity_t *ent, const model_t *mod, const mdxaHeader_t *aHeader,
	CBoneCache *boneCache, qboolean personalModel, qboolean shadowView );

// load cost by stage, summed since the renderer started (always on); rd-vulkan's `loadprofile` prints them.
// asset: .chains read and parse; inst: solver instance; first: an instance's first front-end step, the back end's
// substitute buffers included
enum { HAIRLOAD_ASSET, HAIRLOAD_INST, HAIRLOAD_FIRST, HAIRLOAD_COUNT };
extern double	r_hairLoadMs[HAIRLOAD_COUNT];
extern int		r_hairLoadCount[HAIRLOAD_COUNT];

#endif // TR_HAIR_H
