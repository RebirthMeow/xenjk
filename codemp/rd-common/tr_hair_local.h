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
Hair chains: state shared between the common glue (rd-common/tr_hair.cpp) and rd-vulkan's back end (vk_hair.cpp).
*/
#ifndef TR_HAIR_LOCAL_H
#define TR_HAIR_LOCAL_H

#include "tr_hair.h"
#include "tr_sim.h"
#include "tr_g2ext.h"

extern "C" {
#include "rd-common/hairchains/hair_asset.h"
}

#include <vector>

#define HAIR_MAX_PALETTE 256   // = vkUniformBones_t boneMatrices[] (rd-vulkan/shaders/glsl/global.h); vertex bone refs are bytes

struct hairModelAsset_t {
	char name[MAX_QPATH];          // glm name
	bool missing;                  // no sidecar / unusable
	bool initFailed;               // the solver could not be made for it once; not retried every frame
	hairAsset_t h;
	bool parsed;
	// binding to the glm (valid for boundMod)
	const model_t *boundMod;
	bool bindOk;
	int numLods;
	std::vector<int> rsSurf;                        // render surface -> glm surface index
	std::vector<std::vector<unsigned char>> listed; // [lod][glm surface]: draws from the substitute buffer
	std::vector<float> bindInv;                     // nBones x 16, inverse bind matrices (column-major)
	int bodyParticles;                              // particles tuned by the spring-bone settings (HAIR_PARTICLE_BODY)
	void *rb;                                       // renderer back-end data (R_HairRBFreeAsset)
};

struct hairInst_t {
	const CBoneCache *bc;
	hairModelAsset_t *asset;
	hcSim sim;
	bool simOk;
	const mdxaHeader_t *aHeader;    // skeleton the bone map was built for
	const mdxaHeader_t *bindFailed; // the skeleton the bone map last failed for (not retried every frame)
	int hostGla;                    // gla index of the host bone (cranium)
	int numGlaBones;                // the skeleton's bone count: hair palette slots start here
	std::vector<int> gameGla;       // asset game bone -> gla index
	// this frame's and the last advanced frame's inputs (column-major, model space / world)
	float W[16], Wprev[16];         // entity: world
	float H[16], Hprev[16];         // host skinning matrix: model space
	std::vector<float> G, Gprev;    // game bones' skinning matrices (nGameBones x 16)
	std::vector<float> scratchGs, scratchPre, scratchPrev;   // per-step working copies kept, not reallocated each frame
	// The previous pose is the last advanced frame's, or, while the solver is behind and catching up (sim_frame.h),
	// the pose of its last step, baseLag seconds before lastTime. The spring bones step from their own accumulator,
	// so they keep their own previous pose.
	double baseLag, springBaseLag;
	float HprevS[16];
	std::vector<float> GprevS;
	double prevFrame;               // the last advanced frame's length (s), for Sim_Carry
	std::vector<float> relA, relB;  // the two latest step states in their step's host frame (nParticles x 3)
	std::vector<float> hostM, hostInv;  // HAIR_ASSET_BONE_HOSTS scratch: game bone frames (orthonormal) and inverses
	std::vector<hc_real> anim, posed, drawPos;
	// spring bones (HAIR_ASSET_SPRINGS): their state, the last two steps' offsets (loc - target, model space, nSprings x 3),
	// the time not yet stepped, and per hair bone the spring whose bone it is or hangs from (-1 none)
	std::vector<hsState> springSt;
	std::vector<float> springOffA, springOffB;
	double springAcc;
	std::vector<int> springOf;
	float worldScale;   // the entity's scale (Hair_GatherPose)
	std::vector<int> springBones, springBoneStart;   // each spring's bones (springOf grouped), start per spring + end
	std::vector<mdxaBone_t> palette;  // nBones hair skinning matrices for the GPU (model space, 3x4)
	bool live;
	bool ready;                     // the palette was built for the frame in 'gate'
	simGate_t gate;                 // the frame and scene the palette is for (sim_frame.h)
	// r_simCheck: the scene time (ms) of the pose of the last chain / spring step
	double checkPoseMs, checkSpringMs, checkStepMs;
	std::vector<float> checkTarget; // the last chain step's particle targets
	int lastTime;
	int drawTime;
	double acc;                     // seconds of scene time not yet simulated
	int stepCounter;                // HC_BeginFrame frame numbers (a jump drops the frame history)
	int lastSeenReal;
	float lastDist;   // distance from the scene's camera when last seen (the limit evicts the furthest)
	int warned;
};

extern cvar_t *r_hair;

// the instance that simulates for this bone cache, or NULL
hairInst_t *R_HairFindInst( const CBoneCache *bc );

// renderer back end: free its per-asset data (called before the glue frees the asset); its teardown
void R_HairRBFreeAsset( hairModelAsset_t *as );
void R_HairRBShutdown( void );
void R_HairRBRegister( void );	// its cvars, from R_HairRegister
// renderer back end, from the front end once an instance is bound: build what it draws with (false: draw rigid)
bool R_HairRBPrepare( hairInst_t *hi );
hairModelAsset_t *R_HairAssetIfLoaded( const model_t *mod );
// the same for a skeleton (palette base = its bone count, the host and game bones in it), at most maxBuilds of the
// per-LOD buffers this call: 1 all built, 0 one failed, -1 more to build (the model warm-up's steps)
int R_HairRBPrepareFor( hairModelAsset_t *as, int base, int hostGla, const std::vector<int> &gameGla, int maxBuilds );

#endif // TR_HAIR_LOCAL_H
