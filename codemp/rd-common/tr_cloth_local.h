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
Garment cloth: state shared between the common glue (rd-common/tr_cloth.cpp) and a renderer's back end.
*/
#ifndef TR_CLOTH_LOCAL_H
#define TR_CLOTH_LOCAL_H

#include "tr_cloth.h"
#include "tr_sim.h"

extern "C" {
#include "rd-common/cloth/skirt_sim.h"
#include "rd-common/cloth/cloth_asset.h"
}

#include <vector>

#define CLOTH_GPU_MAX_CAPS 16   // = CL_MAX_CAPS (rd-vulkan/shaders/glsl/cloth_layout.h)

struct clothModelAsset_t {
	char name[MAX_QPATH];       // glm name
	bool missing;               // no sidecar / unusable
	void *buf;
	cloth_asset_t a;
	skirt_asset_t sa;
	// binding to the glm (valid for boundMod)
	const model_t *boundMod;
	bool bindOk;
	std::vector<int> surfToRS;  // glm surface index -> render surface, -1 if none
	std::vector<int> rsStart, rsCount;
	std::vector<const mdxmSurface_t *> rsLod0;
	float normalSign;
	// the render map in one form for every version (Cloth_BindAsset): render vertex v sits at
	// sum_k rW[v*3+k] * particle rTri[v*3+k], plus rOff[v] (glm units) along the normalised interpolation of those
	// particles' sim normals (drawNrm, unsigned); its normal is rSgn[v] times that normal. Version 1: one particle at
	// weight 1, no offset, the asset-wide normal sign.
	std::vector<uint32_t> rTri;
	std::vector<float> rW, rOff, rSgn;
	void *rb;                   // renderer back-end data (R_ClothRBFreeAsset)
};

struct clothInst_t {
	const CBoneCache *bc;
	clothModelAsset_t *asset;
	skirt_sim_t *sim;
	const mdxaHeader_t *aHeader;    // skeleton the bone map was built for
	const mdxaHeader_t *bindFailed; // the skeleton the bone map last failed for (not retried every frame)
	std::vector<int> boneMap;       // asset bone -> gla bone
	std::vector<unsigned char> kin; // pinned particles (the solver's set, Cloth_SyncKin)
	unsigned kinSerial;             // skirt_topology_t::buildSerial kin was copied at
	std::vector<float> L, M, Mprev, Mstep; // nBones*16 row-vector cm: local, world, last frame's world, step pose
	std::vector<float> out, relA, relB;    // N*3: solver output (world cm); the two latest states in their pelvis frames
	std::vector<float> relZ;               // the state before relA (r_clothDrawLead 2)
	std::vector<float> drawPos, drawNrm;   // N*3: model space, glm units; drawNrm unit length, not signed (rSgn)
	bool live;
	bool ready;                     // drawPos/drawNrm were built for the frame in 'gate'
	simGate_t gate;                 // the frame and scene the draw data is for (sim_frame.h)
	int lastTime;
	int drawTime;
	unsigned drawSerial;            // bumped whenever drawPos/drawNrm are rebuilt
	double acc;                     // seconds of scene time not yet simulated
	double baseLag;                 // Mprev lies this far before lastTime: 0 = that frame's pose, else the pose of the
	                                // solver's last step (it is behind and catching up, sim_frame.h)
	double prevFrame;               // the last advanced frame's length (s), for Sim_Carry
	// r_simCheck
	std::vector<float> checkPose;   // the last step's collider sample in its pelvis frame (Cloth_CheckCollider)
	double checkPoseMs;             // its scene time
	int checkInsideStep;            // steps since the instance was made: the particles inside the body are counted on some
	int lastSeenReal;
	float lastDist;   // distance from the scene's camera when last seen (the limit evicts the furthest)
	int warned;
	void *rb;                       // renderer back-end data (R_ClothRBFreeInst)
	// GPU solver (R_ClothRBUseGpu): the CPU solver runs the frame part only (SkirtSim_StepFrame)
	bool gpu;
	int relOld, relNew;             // GPU state slots of the two latest step outputs
	int relOldest;                  // the slot before relOld (three rotate; lead 2 blends all three)
};

extern cvar_t *r_cloth;

// the instance that simulates for this bone cache, or NULL
clothInst_t *R_ClothFindInst( const CBoneCache *bc );

// render vertex v of the asset follows the sim (some particle it is wrapped on is dynamic), else it is skinned like
// the body from the glm's own data
static inline bool R_ClothRenderDynamic( const clothModelAsset_t *as, const unsigned char *kin, int v ) {
	for ( int k = 0; k < 3; k++ ) {
		if ( as->rW[v * 3 + k] != 0.0f && !kin[as->rTri[v * 3 + k]] ) return true;
	}
	return false;
}

// render vertex v from the instance's draw positions / unsigned normals (model space, glm units)
static inline void R_ClothRenderVertex( const clothModelAsset_t *as, const float *dp, const float *dn, int v, float *pos, float *nrm ) {
	const uint32_t *t = &as->rTri[v * 3];
	const float *w = &as->rW[v * 3];
	float p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
	for ( int k = 0; k < 3; k++ ) {
		const float *a = dp + t[k] * 3, *b = dn + t[k] * 3;
		p[0] += w[k] * a[0]; p[1] += w[k] * a[1]; p[2] += w[k] * a[2];
		n[0] += w[k] * b[0]; n[1] += w[k] * b[1]; n[2] += w[k] * b[2];
	}
	const float l = sqrtf( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
	if ( l > 0.0f ) { n[0] /= l; n[1] /= l; n[2] /= l; } else { n[0] = 0; n[1] = 0; n[2] = 1; }
	const float off = as->rOff[v], sg = as->rSgn[v];
	for ( int r = 0; r < 3; r++ ) {
		pos[r] = p[r] + off * n[r];
		nrm[r] = sg * n[r];
	}
}

// renderer back end: the glm header of a MOD_MDXM model
const mdxmHeader_t *R_ClothModelMdxm( const model_t *mod );

// renderer back end: free its per-asset / per-instance data (called before the glue frees them)
void R_ClothRBFreeAsset( clothModelAsset_t *as );
void R_ClothRBFreeInst( clothInst_t *ci );

// renderer back end: the particle part of this instance runs on the GPU (rd-vulkan, r_clothGpu); else the CPU solver
bool R_ClothRBUseGpu( clothInst_t *ci );
// the most step records the GPU back end queues between two of its frames (the glue steps no more per frame); a fuller
// queue means the back end has not run (minimised) and the instance starts over
#define CLOTH_GPU_MAX_QUEUED	8
// queue a reset or step for the GPU: fr from SkirtSim_StepFrame, capsule ends (nCapsules*6 each), the step's
// skinning matrices (world cm), G maps the result into the step's pelvis frame (rows, translation in [3] [7] [11]);
// the output goes to state slot relSlot (0..2). Returns false if the GPU cannot take it (the glue falls back to the CPU).
bool R_ClothRBGpuStep( clothInst_t *ci, const skirt_frame_t *fr, const float *capOld, const float *capNew,
	const float *mats, const float G[12], int relSlot );
// this frame's draw: blend slot relA -> relB by t and place it with Fc, the current pelvis frame (orthonormal
// row-vector, model space, cm); bc is the instance's bone cache (the GPU skins with its reference bone). relZ is the
// state before relA; with lead2 (r_clothDrawLead 2) the draw blends from relZ -> relA carried forward to relA -> relB
// carried forward by s, the fraction of a step since relB
void R_ClothRBGpuDraw( clothInst_t *ci, CBoneCache *bc, float t, int relZ, int relA, int relB, float s, bool lead2,
	const float *Fc );

// renderer back end: its cvars (from R_ClothRegister) and its teardown (from R_ClothShutdown, after every instance
// and asset was freed)
void R_ClothRBRegister( void );
void R_ClothRBShutdown( void );

// the glue's statistics, for a back end that measures asynchronously (GPU readback)
void R_ClothAddStepStats( int contacts, int hand, int cone, int nan, double ms );

#endif // TR_CLOTH_LOCAL_H
