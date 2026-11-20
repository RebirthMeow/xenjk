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
Hair chains on Ghoul2 models: renderer glue (rd-vulkan). See tr_hair.h.

Per instance and per scene time advance (front end):
  1. pose:    the entity's world matrix W, the host bone's skinning matrix H (cranium, model space) and the
              skinning matrices of the game bones that carry collider points, from the render bone cache
  2. advance: fixed steps of the asset's dt (1/30 s, Bonamik's authored rate) from an accumulator, planned by
              Sim_PlanSteps (sim_frame.h); each catch-up step is driven by inputs lerped (and re-orthonormalised)
              between the stored previous pose and the current frame's.
              The solver runs in the entity's frame (model space): targets and anchors are H x the hair bones'
              bind heads, collider points their game bone's matrix x their bind points. Each step's particles are
              stored relative to that step's host frame. An asset with HAIR_ASSET_BONE_HOSTS hangs some root bones
              from game bones instead (a dress from the pelvis and thighs): those bones and their particles use
              that game bone's frame wherever this comment says H.
  3. draw:    the last two step states are blended in the host frame and placed with the current H (the roots stay
              on the head at any frame rate); HC_PoseFrom turns them into hair bone frames; each bone's skinning
              matrix K = posed x bind^-1 goes to the GPU palette after the skeleton's own bones (vk_hair.cpp).
Spring bones (HAIR_ASSET_SPRINGS, hair_spring.h) step beside the solver at their own fixed rate (1/120 s), in model
space, toward their animated heads lerped within the frame like the solver's inputs; each step keeps its offset
(simulated - animated). At draw the last two offsets are blended and move the spring bone and the bones hanging from it.
One decision per instance and frame: the first view that draws the model (a shadow view too) runs these steps, the
other views of the frame reuse the result, and the back end only uses a palette stamped with its own frame (tr_sim.h;
docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md).
Resets follow the Route-A solver's run: two HC_BeginFrame calls at the reset pose, then the first step reuses the
reset's frame number (the frame history starts over), so an r_hairDump replays bit for bit offline (tools/hair).
*/
#include "tr_local.h"
#include "rd-common/tr_hair_local.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include <math.h>

cvar_t *r_hair;
static cvar_t *r_hairInverse;
static cvar_t *r_hairFeel;
static cvar_t *r_hairFeelLin, *r_hairFeelMaxG, *r_hairFeelLag;
static cvar_t *r_hairFeelEuler, *r_hairFeelAngAcc;
static cvar_t *r_hairFeelCent, *r_hairFeelAngVel, *r_hairFeelAngLag;
static cvar_t *r_hairFlow, *r_hairFlowDamp, *r_hairFlowInner, *r_hairFlowDrag, *r_hairFlowParent;
static cvar_t *r_hairMaxDist;
static cvar_t *r_hairFade;
static cvar_t *r_hairWorldScale;
static cvar_t *r_hairPreroll;
static cvar_t *r_hairMaxSteps;
static cvar_t *r_hairMaxInstances;
static cvar_t *r_hairFast;
static cvar_t *r_hairStats;
static cvar_t *r_hairDump;
static cvar_t *r_hairSprings;
static cvar_t *r_hairSpringLimit;
static cvar_t *r_hairSpringChest, *r_hairSpringHips, *r_hairSpringOther;
static cvar_t *r_hairChains;

static const int HAIR_SPRING_MAX_STEPS = 16;  // catch-up spring steps per frame (133 ms at 1/120 s); the rest is dropped

static const int HAIR_RESET_GAP_MS = 500;     // unseen this long (or time went backwards): reset
static const float HAIR_TELEPORT_UNITS = 96.0f; // as cloth: farther than this plus HAIR_MAX_SPEED x dt in one frame
static const float HAIR_MAX_SPEED = 2000.0f;
static const int HAIR_IDLE_FREE_MS = 5000;    // free a solver unseen this long (real time)

static std::vector<hairModelAsset_t *> hairAssets;
static std::unordered_map<std::string, hairModelAsset_t *> hairAssetByName;   // lower-case GLM name -> hairAssets entry
// the same by model, so a model drawn in every view does not build a name key each time; an entry counts only while
// its asset has the model's name (the model cache may free a model and load another at the same address)
static std::unordered_map<const model_t *, hairModelAsset_t *> hairAssetByModel;

static hairModelAsset_t *Hair_AssetByModel( const model_t *mod ) {
	std::unordered_map<const model_t *, hairModelAsset_t *>::const_iterator found = hairAssetByModel.find( mod );
	return found != hairAssetByModel.end() && !Q_stricmp( found->second->name, mod->name ) ? found->second : NULL;
}
static std::vector<hairInst_t *> hairInsts;
static hairInst_t *hairLastHit;

static fileHandle_t hairDumpFile;
static char hairDumpName[MAX_QPATH];
static const hairInst_t *hairDumpInst;

static struct {
	int lastFrameCount, lastPrint, frames;
	int steps, resets, rFirst, rGap, rBack, rJump, rNan;
	double stepMs, stepMax, frontMs;
	int springSteps, springSamples;
	double springSumCm, springMaxCm;   // the offsets the spring bones are drawn with (after the limit and alpha)
} hairStats;

static double HairNowMs( void ) {
	using namespace std::chrono;
	return duration<double, std::milli>( steady_clock::now().time_since_epoch() ).count();
}

double	r_hairLoadMs[HAIRLOAD_COUNT];
int		r_hairLoadCount[HAIRLOAD_COUNT];

// adds its scope's time to one load stage (tr_hair.h)
struct hairLoadTimer_t {
	int slot;
	double t0;
	explicit hairLoadTimer_t( int s ) : slot( s ), t0( HairNowMs() ) {}
	~hairLoadTimer_t() {
		r_hairLoadMs[slot] += HairNowMs() - t0;
		r_hairLoadCount[slot]++;
	}
};

/* ------------------------------------------------------------------ small maths (column-major 4x4) */

static void M4_Mul( float *o, const float *a, const float *b ) {
	float r[16];
	for ( int c = 0; c < 4; c++ ) {
		for ( int k = 0; k < 4; k++ ) {
			r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
		}
	}
	memcpy( o, r, sizeof( r ) );
}

// inverse of a rigid (rotation + translation) column-major matrix
static void M4_InvRigid( float *o, const float *m ) {
	float r[16];
	for ( int i = 0; i < 3; i++ ) {
		for ( int j = 0; j < 3; j++ ) r[j * 4 + i] = m[i * 4 + j];
		r[i * 4 + 3] = 0.0f;
	}
	for ( int i = 0; i < 3; i++ ) r[12 + i] = -( r[i] * m[12] + r[4 + i] * m[13] + r[8 + i] * m[14] );
	r[15] = 1.0f;
	memcpy( o, r, sizeof( r ) );
}

static void M4_Point( float *o, const float *m, const float *p ) {
	const float x = p[0], y = p[1], z = p[2];
	for ( int i = 0; i < 3; i++ ) o[i] = m[i] * x + m[4 + i] * y + m[8 + i] * z + m[12 + i];
}

// Gram-Schmidt on the rotation columns (sign of column 2 kept)
static void M4_Orthonormalise( float *m ) {
	vec3_t x, y, z;
	VectorCopy( m, x );
	VectorNormalize( x );
	const float d = DotProduct( x, m + 4 );
	VectorMA( m + 4, -d, x, y );
	VectorNormalize( y );
	CrossProduct( x, y, z );
	if ( DotProduct( z, m + 8 ) < 0 ) VectorScale( z, -1.0f, z );
	VectorCopy( x, m );
	VectorCopy( y, m + 4 );
	VectorCopy( z, m + 8 );
}

// a render bone matrix (3x4 rows, EvalRender) as column-major 4x4
static void M4_FromBone( float *o, const mdxaBone_t *b ) {
	for ( int r = 0; r < 3; r++ ) {
		for ( int c = 0; c < 4; c++ ) o[c * 4 + r] = b->matrix[r][c];
		o[r * 4 + 3] = 0.0f;
	}
	o[15] = 1.0f;
}

static void M4_Lerp( float *o, const float *a, const float *b, float t ) {
	for ( int k = 0; k < 16; k++ ) o[k] = a[k] + t * ( b[k] - a[k] );
	M4_Orthonormalise( o );
}

/* ------------------------------------------------------------------ assets */

static void Hair_FreeAsset( hairModelAsset_t *as ) {
	R_HairRBFreeAsset( as );
	if ( as->parsed ) HairAsset_Free( &as->h );
	delete as;
}

// the asset of a model if it was loaded already (no file read)
hairModelAsset_t *R_HairAssetIfLoaded( const model_t *mod ) {
	if ( !mod ) return NULL;
	if ( hairModelAsset_t *known = Hair_AssetByModel( mod ) ) return known->missing ? NULL : known;
	char key[MAX_QPATH];
	Q_strncpyz( key, mod->name, sizeof( key ) );
	Q_strlwr( key );
	std::unordered_map<std::string, hairModelAsset_t *>::const_iterator found = hairAssetByName.find( key );
	return found != hairAssetByName.end() && !found->second->missing ? found->second : NULL;
}

static hairModelAsset_t *Hair_AssetForModel( const model_t *mod ) {
	// a hash lookup, not a case-insensitive compare down every model ever drawn (sabers, NPCs, weapons)
	if ( hairModelAsset_t *known = Hair_AssetByModel( mod ) ) return known->missing ? NULL : known;
	char key[MAX_QPATH];
	Q_strncpyz( key, mod->name, sizeof( key ) );
	Q_strlwr( key );
	std::unordered_map<std::string, hairModelAsset_t *>::const_iterator found = hairAssetByName.find( key );
	if ( found != hairAssetByName.end() ) {
		hairAssetByModel[mod] = found->second;
		return found->second->missing ? NULL : found->second;
	}
	hairLoadTimer_t timer( HAIRLOAD_ASSET );
	hairModelAsset_t *as = new hairModelAsset_t();
	Q_strncpyz( as->name, mod->name, sizeof( as->name ) );
	as->missing = true;
	as->parsed = false;
	as->boundMod = NULL;
	as->bindOk = false;
	as->numLods = 0;
	as->rb = NULL;
	hairAssets.push_back( as );
	hairAssetByName[key] = as;
	hairAssetByModel[mod] = as;

	char path[MAX_QPATH];
	COM_StripExtension( mod->name, path, sizeof( path ) );
	Q_strcat( path, sizeof( path ), ".chains" );
	void *fileBuf = NULL;
	const long len = ri.FS_ReadFile( path, &fileBuf );
	if ( len <= 0 || !fileBuf ) {
		return NULL;
	}
	char err[128];
	const int ok = HairAsset_Parse( fileBuf, (size_t)len, &as->h, err, sizeof( err ) );
	ri.FS_FreeFile( fileBuf );
	if ( !ok ) {
		ri.Printf( PRINT_WARNING, "hair: %s: %s\n", path, err );
		return NULL;
	}
	as->parsed = true;
	const hcAsset &a = as->h.a;
	as->bindInv.assign( (size_t)a.nBones * 16, 0.0f );
	for ( int b = 0; b < a.nBones; b++ ) M4_InvRigid( &as->bindInv[b * 16], a.bones[b].bind );
	as->missing = false;
	as->bodyParticles = 0;
	for ( int i = 0; i < a.nParticles; i++ ) as->bodyParticles += ( a.particles[i].group & HAIR_PARTICLE_BODY ) != 0;
	ri.Printf( PRINT_ALL, "hair: %s: %d particles (%d body), %d bones, %d colliders (%d pairs), %d render entries on %d surfaces, %d springs\n",
		path, a.nParticles, as->bodyParticles, a.nBones, a.nRepulsors, a.nPairs, as->h.nRenderEntries, as->h.nRenderSurfs, as->h.nSprings );
	return as;
}

static const mdxmSurfHierarchy_t *Hair_SurfHier( const mdxmHeader_t *mdxm, int index ) {
	const mdxmHierarchyOffsets_t *offs = (const mdxmHierarchyOffsets_t *)( (const byte *)mdxm + sizeof( mdxmHeader_t ) );
	return (const mdxmSurfHierarchy_t *)( (const byte *)offs + offs->offsets[index] );
}

static const mdxmSurface_t *Hair_LodSurface( const mdxmHeader_t *mdxm, int lodIndex, int index ) {
	const mdxmLOD_t *lod = (const mdxmLOD_t *)( (const byte *)mdxm + mdxm->ofsLODs );
	for ( int l = 0; l < lodIndex; l++ ) lod = (const mdxmLOD_t *)( (const byte *)lod + lod->ofsEnd );
	const mdxmLODSurfOffset_t *indexes = (const mdxmLODSurfOffset_t *)( (const byte *)lod + sizeof( mdxmLOD_t ) );
	return (const mdxmSurface_t *)( (const byte *)indexes + indexes->offsets[index] );
}

// map the asset's render surfaces onto this glm (by name, LOD, vertex count)
static bool Hair_BindAsset( hairModelAsset_t *as, const model_t *mod );

// before the model's first draw, so its first frame does not parse and bind the asset
void R_HairWarmModel( const model_t *mod ) {
	hairModelAsset_t *as = mod ? Hair_AssetForModel( mod ) : NULL;
	if ( as ) Hair_BindAsset( as, mod );
}

static bool Hair_BindAsset( hairModelAsset_t *as, const model_t *mod ) {
	if ( as->boundMod == mod ) return as->bindOk;
	as->boundMod = mod;
	as->bindOk = false;
	R_HairRBFreeAsset( as );   // the substitute buffers belong to the old glm
	const mdxmHeader_t *mdxm = mod->type == MOD_MDXM && mod->data.glm ? mod->data.glm->header : NULL;
	if ( !mdxm || mdxm->numLODs < 1 ) return false;
	const hairAsset_t &h = as->h;
	as->numLods = mdxm->numLODs;
	as->rsSurf.assign( h.nRenderSurfs, -1 );
	as->listed.assign( mdxm->numLODs, std::vector<unsigned char>( mdxm->numSurfaces, 0 ) );
	for ( int s = 0; s < h.nRenderSurfs; s++ ) {
		const hairRenderSurf_t &rs = h.renderSurfs[s];
		if ( rs.lod < 0 || rs.lod >= mdxm->numLODs ) {
			ri.Printf( PRINT_WARNING, "hair: %s has no LOD %d (surface %s)\n", mod->name, rs.lod, rs.name );
			return false;
		}
		int found = -1;
		for ( int i = 0; i < mdxm->numSurfaces; i++ ) {
			if ( !Q_stricmp( Hair_SurfHier( mdxm, i )->name, rs.name ) ) {
				found = i;
				break;
			}
		}
		if ( found < 0 ) {
			ri.Printf( PRINT_WARNING, "hair: %s has no surface %s\n", mod->name, rs.name );
			return false;
		}
		const mdxmSurface_t *surf = Hair_LodSurface( mdxm, rs.lod, found );
		if ( surf->numVerts != rs.numVerts ) {
			ri.Printf( PRINT_WARNING, "hair: %s surface %s LOD %d has %d verts, the asset maps %d\n", mod->name, rs.name, rs.lod,
				surf->numVerts, rs.numVerts );
			return false;
		}
		as->rsSurf[s] = found;
		as->listed[rs.lod][found] = 1;
	}
	as->bindOk = true;
	return true;
}

/* ------------------------------------------------------------------ instances */

static void Hair_CloseDump( void ) {
	if ( hairDumpFile ) ri.FS_FCloseFile( hairDumpFile );
	hairDumpFile = 0;
	hairDumpInst = NULL;
}

static void Hair_FreeInst( hairInst_t *hi ) {
	if ( hi == hairLastHit ) hairLastHit = NULL;
	if ( hi == hairDumpInst ) Hair_CloseDump();
	if ( hi->simOk ) HC_Free( &hi->sim );
	delete hi;
}

void R_HairForgetBoneCache( const CBoneCache *bc ) {
	for ( size_t i = 0; i < hairInsts.size(); i++ ) {
		if ( hairInsts[i]->bc == bc ) {
			Hair_FreeInst( hairInsts[i] );
			hairInsts.erase( hairInsts.begin() + i );
			return;
		}
	}
}

void R_HairShutdown( void ) {
	Hair_CloseDump();
	hairDumpName[0] = 0;
	for ( size_t i = 0; i < hairInsts.size(); i++ ) Hair_FreeInst( hairInsts[i] );
	hairInsts.clear();
	hairLastHit = NULL;
	for ( size_t i = 0; i < hairAssets.size(); i++ ) Hair_FreeAsset( hairAssets[i] );
	hairAssets.clear();
	hairAssetByName.clear();
	hairAssetByModel.clear();
	R_HairRBShutdown();
}

hairInst_t *R_HairFindInst( const CBoneCache *bc ) {
	if ( hairLastHit && hairLastHit->bc == bc ) return hairLastHit;
	for ( size_t i = 0; i < hairInsts.size(); i++ ) {
		if ( hairInsts[i]->bc == bc ) {
			hairLastHit = hairInsts[i];
			return hairInsts[i];
		}
	}
	return NULL;
}

static void Hair_ApplySettings( hairInst_t *hi ) {
	hcFeel f;
	memset( &f, 0, sizeof( f ) );
	if ( r_hairFeel->integer ) {
		f.on = 1;
		f.linWeight = r_hairFeelLin->value;
		f.maxLinAccG = r_hairFeelMaxG->value;
		f.linTau = 0.0f;
		f.eulerWeight = r_hairFeelEuler->value;
		f.maxAngAcc = DEG2RAD( r_hairFeelAngAcc->value );
		f.centWeight = r_hairFeelCent->value;
		f.maxAngVel = DEG2RAD( r_hairFeelAngVel->value );
		f.carryLag = r_hairFeelLag->value;
		f.carryAngLag = DEG2RAD( r_hairFeelAngLag->value );
	}
	HC_SetFeel( &hi->sim, &f );
	hcFlow fl;
	memset( &fl, 0, sizeof( fl ) );
	if ( r_hairFlow->integer ) {
		fl.springDamp = r_hairFlowDamp->value;
		fl.innerScale = r_hairFlowInner->value;
		fl.airDrag = r_hairFlowDrag->value;
		fl.coneParent = r_hairFlowParent->value;
	}
	HC_SetFlow( &hi->sim, &fl );
	hcAsset &a = hi->asset->h.a;   // the flag is read per step; one asset, one setting
	if ( r_hairInverse->integer ) a.flags |= HC_ASSET_HONOUR_INVERSE;
	else a.flags &= ~HC_ASSET_HONOUR_INVERSE;
}

static hairInst_t *Hair_CreateInst( const CBoneCache *bc, hairModelAsset_t *as, float dist ) {
	if ( as->initFailed )   // not retried (and warned about) every frame; tested before a live
		return NULL;         // instance is evicted for it
	const int maxInst = r_hairMaxInstances->integer > 0 ? r_hairMaxInstances->integer : 1;
	if ( (int)hairInsts.size() >= maxInst ) {
		int oldest = -1;
		for ( size_t i = 0; i < hairInsts.size(); i++ ) {
			if ( oldest < 0 || hairInsts[i]->lastSeenReal < hairInsts[oldest]->lastSeenReal ) oldest = (int)i;
		}
		if ( oldest >= 0 && ri.Milliseconds() - hairInsts[oldest]->lastSeenReal < 1000 ) {
			// all seen recently; a newcomer clearly nearer (by a quarter) than the furthest takes its slot
			int furthest = -1;
			for ( size_t i = 0; i < hairInsts.size(); i++ ) {
				if ( furthest < 0 || hairInsts[i]->lastDist > hairInsts[furthest]->lastDist ) furthest = (int)i;
			}
			if ( furthest < 0 || !( dist * 1.25f < hairInsts[furthest]->lastDist ) ) return NULL;
			oldest = furthest;
		}
		if ( oldest < 0 ) return NULL;
		Hair_FreeInst( hairInsts[oldest] );
		hairInsts.erase( hairInsts.begin() + oldest );
	}
	hairInst_t *hi = new hairInst_t();
	hi->bc = bc;
	hi->asset = as;
	hi->simOk = HC_Init( &hi->sim, &as->h.a ) != 0;
	if ( !hi->simOk ) {
		ri.Printf( PRINT_WARNING, "hair: %s: solver allocation failed\n", as->name );
		HC_Free( &hi->sim );   // the arrays that did allocate
		as->initFailed = true;
		delete hi;
		return NULL;
	}
	const hcAsset &a = as->h.a;
	hi->aHeader = NULL;
	hi->hostGla = -1;
	hi->numGlaBones = 0;
	hi->G.assign( (size_t)as->h.nGameBones * 16, 0.0f );
	hi->Gprev = hi->G;
	hi->relA.assign( (size_t)a.nParticles * 3, 0.0f );
	hi->relB = hi->relA;
	hi->anim.assign( (size_t)a.nBones * 16, 0.0f );
	hi->posed = hi->anim;
	hi->drawPos.assign( (size_t)a.nParticles * 3, 0.0f );
	hi->palette.assign( a.nBones, mdxaBone_t() );
	const hairAsset_t &h = as->h;
	hi->springSt.assign( h.nSprings, hsState() );
	hi->springOffA.assign( (size_t)h.nSprings * 3, 0.0f );
	hi->springOffB = hi->springOffA;
	hi->springAcc = 0;
	hi->springOf.assign( a.nBones, -1 );
	for ( int s = 0; s < h.nSprings; s++ ) hi->springOf[h.springs[s].bone] = s;
	for ( int b = 0; b < a.nBones; b++ ) {   // parents first: a bone hanging from a spring bone moves with it
		if ( a.bones[b].parent >= 0 && hi->springOf[b] < 0 ) hi->springOf[b] = hi->springOf[a.bones[b].parent];
	}
	hi->springBones.clear();
	hi->springBoneStart.assign( h.nSprings + 1, 0 );
	for ( int s = 0; s < h.nSprings; s++ ) {
		hi->springBoneStart[s] = (int)hi->springBones.size();
		for ( int b = 0; b < a.nBones; b++ ) if ( hi->springOf[b] == s ) hi->springBones.push_back( b );
	}
	hi->springBoneStart[h.nSprings] = (int)hi->springBones.size();
	hi->live = false;
	hi->ready = false;
	Sim_GateClear( &hi->gate );
	hi->baseLag = hi->springBaseLag = 0;
	hi->prevFrame = 0;
	hi->GprevS = hi->G;
	hi->checkPoseMs = hi->checkSpringMs = 0;
	hi->lastTime = 0;
	hi->drawTime = -1;
	hi->acc = 0;
	hi->stepCounter = 0;
	hi->lastSeenReal = ri.Milliseconds();
	hi->warned = 0;
	Hair_ApplySettings( hi );
	hairInsts.push_back( hi );
	return hi;
}

static int Hair_GlaBone( const mdxaHeader_t *aHeader, const char *name ) {
	const mdxaSkelOffsets_t *offs = (const mdxaSkelOffsets_t *)( (const byte *)aHeader + sizeof( mdxaHeader_t ) );
	for ( int g = 0; g < aHeader->numBones; g++ ) {
		const mdxaSkel_t *skel = (const mdxaSkel_t *)( (const byte *)aHeader + sizeof( mdxaHeader_t ) + offs->offsets[g] );
		if ( !Q_stricmp( skel->name, name ) ) return g;
	}
	return -1;
}

static bool Hair_BindBones( hairInst_t *hi, const mdxaHeader_t *aHeader ) {
	const hairAsset_t &h = hi->asset->h;
	hi->aHeader = aHeader;
	hi->numGlaBones = aHeader->numBones;
	hi->hostGla = Hair_GlaBone( aHeader, h.hostName );
	hi->gameGla.assign( h.nGameBones, -1 );
	bool ok = hi->hostGla >= 0;
	for ( int k = 0; k < h.nGameBones && ok; k++ ) {
		hi->gameGla[k] = Hair_GlaBone( aHeader, h.gameBoneNames[k] );
		if ( hi->gameGla[k] < 0 ) {
			if ( !hi->warned ) ri.Printf( PRINT_WARNING, "hair: %s: skeleton has no bone %s\n", hi->asset->name, h.gameBoneNames[k] );
			ok = false;
		}
	}
	if ( ok && hi->numGlaBones + h.a.nBones > HAIR_MAX_PALETTE ) {
		if ( !hi->warned ) ri.Printf( PRINT_WARNING, "hair: %s: %d skeleton + %d hair bones exceed the %d-bone palette\n",
			hi->asset->name, hi->numGlaBones, h.a.nBones, HAIR_MAX_PALETTE );
		ok = false;
	}
	if ( !ok && !hi->warned ) {
		if ( hi->hostGla < 0 ) ri.Printf( PRINT_WARNING, "hair: %s: skeleton has no host bone %s\n", hi->asset->name, h.hostName );
		hi->warned = 1;
	}
	return ok;
}

/* ------------------------------------------------------------------ per frame */

// 1. pose: W (world), H (host, model space), G (game bones, model space)
static void Hair_GatherPose( hairInst_t *hi, CBoneCache *bc, const trRefEntity_t *ent ) {
	for ( int k = 0; k < 3; k++ ) {
		vec3_t ax;
		VectorCopy( ent->e.axis[k], ax );
		const float len = VectorNormalize( ax );
		if ( len == 0.0f ) VectorClear( ax );
		// the entity's scale (its axes carry it); an unscaled axis is 1 or 0.99999994 by yaw: exactly 1
		if ( k == 0 ) hi->worldScale = len > 0.0f ? ( fabsf( len - 1.0f ) <= 1e-3f ? 1.0f : len ) : 1.0f;
		hi->W[k * 4] = ax[0]; hi->W[k * 4 + 1] = ax[1]; hi->W[k * 4 + 2] = ax[2]; hi->W[k * 4 + 3] = 0.0f;
	}
	hi->W[12] = ent->e.origin[0]; hi->W[13] = ent->e.origin[1]; hi->W[14] = ent->e.origin[2]; hi->W[15] = 1.0f;
	M4_FromBone( hi->H, R_ClothEvalBone( bc, hi->hostGla ) );
	M4_Orthonormalise( hi->H );
	for ( size_t k = 0; k < hi->gameGla.size(); k++ ) M4_FromBone( &hi->G[k * 16], R_ClothEvalBone( bc, hi->gameGla[k] ) );
}

// HAIR_ASSET_BONE_HOSTS: root bones hang from game bones (a dress from the pelvis and thighs) instead of the host.
// Their frames are the game bones' matrices, orthonormalised as H is; without the flag every bone uses H.
static bool Hair_HasHosts( const hairInst_t *hi ) {
	return ( hi->asset->h.a.flags & HAIR_ASSET_BONE_HOSTS ) != 0;
}

static void Hair_HostFrames( hairInst_t *hi, const float *G ) {
	const size_t n = hi->gameGla.size() * 16;
	hi->hostM.assign( G, G + n );
	for ( size_t k = 0; k < n; k += 16 ) M4_Orthonormalise( &hi->hostM[k] );
}

// a bone's frame: H, or its host game bone's (after Hair_HostFrames)
static const float *Hair_BoneFrame( const hairInst_t *hi, int bone, const float *H ) {
	const int host = hi->asset->h.boneHost[bone];
	return host < 0 ? H : &hi->hostM[host * 16];
}

// the solver's per-step inputs from a pose (model space = the solver's reference frame)
static void Hair_SetInputs( hairInst_t *hi, const float *H, const float *G ) {
	const hcAsset &a = hi->asset->h.a;
	hcSim &s = hi->sim;
	const bool hosts = Hair_HasHosts( hi );
	if ( hosts ) Hair_HostFrames( hi, G );
	float p[3];
	for ( int i = 0; i < a.nParticles; i++ ) {
		const int bone = a.particles[i].bone;
		M4_Point( p, hosts ? Hair_BoneFrame( hi, bone, H ) : H, a.bones[bone].bind + 12 );
		s.target[i][0] = p[0]; s.target[i][1] = p[1]; s.target[i][2] = p[2];
	}
	for ( int c = 0; c < a.nCones; c++ ) {
		if ( a.cones[c].parent >= 0 ) continue;
		const int bone = a.cones[c].anchorBone;
		M4_Point( p, hosts ? Hair_BoneFrame( hi, bone, H ) : H, a.bones[bone].bind + 12 );
		s.anchor[c][0] = p[0]; s.anchor[c][1] = p[1]; s.anchor[c][2] = p[2];
	}
	for ( int r = 0; r < a.nRepulsors; r++ ) {
		const hcRepulsor &rp = a.repulsors[r];
		for ( int e = 0; e < 2; e++ ) {
			const int k = e < rp.n ? e : rp.n - 1;
			M4_Point( p, G + rp.gameBone[k] * 16, rp.bind[k] );
			s.repPoint[r][e][0] = p[0]; s.repPoint[r][e][1] = p[1]; s.repPoint[r][e][2] = p[2];
		}
	}
}

// keep the step's particles in their bone's host frame (relA = older, relB = newest)
static void Hair_StoreRel( hairInst_t *hi, const float *H, const float *G ) {
	const hcAsset &a = hi->asset->h.a;
	const int N = a.nParticles;
	float inv[16], p[3];
	M4_InvRigid( inv, H );
	const bool hosts = Hair_HasHosts( hi );
	if ( hosts ) {
		Hair_HostFrames( hi, G );
		hi->hostInv.resize( hi->hostM.size() );
		for ( size_t k = 0; k < hi->hostM.size(); k += 16 ) M4_InvRigid( &hi->hostInv[k], &hi->hostM[k] );
	}
	hi->relA.swap( hi->relB );
	for ( int i = 0; i < N; i++ ) {
		p[0] = hi->sim.pos[i][0]; p[1] = hi->sim.pos[i][1]; p[2] = hi->sim.pos[i][2];
		const int host = hosts ? hi->asset->h.boneHost[a.particles[i].bone] : -1;
		M4_Point( &hi->relB[i * 3], host < 0 ? inv : &hi->hostInv[host * 16], p );
	}
}

/* r_hairDump: "HCD1", int version 2, nParticles, nCones, nRepulsors, asset flags, then the feel (int on, float
   linWeight maxLinAccG linTau eulerWeight maxAngAcc centWeight maxAngVel carryLag carryAngLag) and the flow (float
   springDamp innerScale airDrag coneParent); records: int counter, int flags (1 = reset; from bit 8 up the pose time
   of a stretched step in microseconds, 0 = dt: Sim_DumpFlags), float ref[16], targets[nP*3], anchors[nC*3] (NaN off roots), repulsor points
   [nR*6], pre pos/prev, post pos/prev [nP*3] each. One instance per file (the first to step after the cvar is set). */
static void Hair_Dump( hairInst_t *hi, int flags, const float *W, const float *prePos, const float *prePrev ) {
	if ( !r_hairDump->string[0] ) return;
	if ( Q_stricmp( hairDumpName, r_hairDump->string ) ) {
		Hair_CloseDump();
		hairDumpName[0] = 0;
	}
	const hcAsset &a = hi->asset->h.a;
	if ( !hairDumpFile ) {
		if ( hairDumpName[0] ) return;   // this name's instance is gone
		Q_strncpyz( hairDumpName, r_hairDump->string, sizeof( hairDumpName ) );
		hairDumpFile = ri.FS_FOpenFileWrite( hairDumpName, qtrue );
		if ( !hairDumpFile ) return;
		hairDumpInst = hi;
		const int head[6] = { 0x31444348, 2, a.nParticles, a.nCones, a.nRepulsors, a.flags };   // "HCD1" version 2
		const hcFeel &f = hi->sim.feel;
		const hcFlow &fl = hi->sim.flow;
		const int on = f.on;
		const float ff[9] = { f.linWeight, f.maxLinAccG, f.linTau, f.eulerWeight, f.maxAngAcc, f.centWeight, f.maxAngVel,
			f.carryLag, f.carryAngLag };
		const float fw[4] = { fl.springDamp, fl.innerScale, fl.airDrag, fl.coneParent };
		ri.FS_Write( head, sizeof( head ), hairDumpFile );
		ri.FS_Write( &on, sizeof( on ), hairDumpFile );
		ri.FS_Write( ff, sizeof( ff ), hairDumpFile );
		ri.FS_Write( fw, sizeof( fw ), hairDumpFile );
	}
	if ( hi != hairDumpInst ) return;
	const hcSim &s = hi->sim;
	const int hd[2] = { hi->stepCounter, flags };
	ri.FS_Write( hd, sizeof( hd ), hairDumpFile );
	ri.FS_Write( W, 16 * sizeof( float ), hairDumpFile );
	ri.FS_Write( s.target, a.nParticles * 3 * sizeof( float ), hairDumpFile );
	std::vector<float> an( (size_t)a.nCones * 3 );
	for ( int c = 0; c < a.nCones; c++ ) {
		for ( int k = 0; k < 3; k++ ) an[c * 3 + k] = a.cones[c].parent < 0 ? s.anchor[c][k] : std::numeric_limits<float>::quiet_NaN();
	}
	if ( !an.empty() ) ri.FS_Write( &an[0], (int)( an.size() * sizeof( float ) ), hairDumpFile );   // no cones, no &an[0]
	ri.FS_Write( s.repPoint, a.nRepulsors * 6 * sizeof( float ), hairDumpFile );
	ri.FS_Write( prePos, a.nParticles * 3 * sizeof( float ), hairDumpFile );
	ri.FS_Write( prePrev, a.nParticles * 3 * sizeof( float ), hairDumpFile );
	ri.FS_Write( s.pos, a.nParticles * 3 * sizeof( float ), hairDumpFile );
	ri.FS_Write( s.prev, a.nParticles * 3 * sizeof( float ), hairDumpFile );
}

static void Hair_Reset( hairInst_t *hi, int now ) {
	hcSim &s = hi->sim;
	if ( hi->asset->h.a.nParticles > 0 ) {   // a springs-only asset has no chains to solve
		hi->stepCounter += 2;   // not consecutive: the frame history starts over
		HC_BeginFrame( &s, hi->W, hi->stepCounter );
		HC_BeginFrame( &s, hi->W, hi->stepCounter );
		Hair_SetInputs( hi, hi->H, hi->G.data() );
		HC_ResetToTargets( &s );
		// r_hairPreroll (steps): settle on the reset pose before the first draw, so the hair does not
		// visibly fall into place from its animated shape (about 74 us a step; once per reset)
		const int preroll = r_hairPreroll->integer > 0 ? ( r_hairPreroll->integer < 60 ? r_hairPreroll->integer : 60 ) : 0;
		for ( int k = 0; k < preroll; k++ ) {
			HC_BeginFrame( &s, hi->W, ++hi->stepCounter );
			Hair_SetInputs( hi, hi->H, hi->G.data() );
			HC_Step( &s );
		}
		Hair_StoreRel( hi, hi->H, hi->G.data() );
		hi->relA = hi->relB;
		Hair_Dump( hi, 1, hi->W, &s.pos[0][0], &s.prev[0][0] );   // after the pre-roll: a replay starts from what is drawn
		if ( !preroll )
			hi->stepCounter--;      // the first step reuses the reset's number, as the Route-A solver's run does
		else
			hi->stepCounter++;      // not consecutive either: the pre-roll's history is the reset origin held still, so a
		                            // character moving at reset got a start-up kick from it
	}
	hi->acc = 0;
	for ( size_t k = 0; k < hi->springSt.size(); k++ ) hi->springSt[k].fresh = 1;   // springs start at their targets
	std::fill( hi->springOffA.begin(), hi->springOffA.end(), 0.0f );
	std::fill( hi->springOffB.begin(), hi->springOffB.end(), 0.0f );
	hi->springAcc = 0;
	hi->baseLag = hi->springBaseLag = 0;
	hi->prevFrame = 0;
	hi->live = true;
	hi->lastTime = now;
	memcpy( hi->Wprev, hi->W, sizeof( hi->W ) );
	memcpy( hi->Hprev, hi->H, sizeof( hi->H ) );
	hi->Gprev = hi->G;
	memcpy( hi->HprevS, hi->H, sizeof( hi->H ) );
	hi->GprevS = hi->G;
	hi->checkPoseMs = hi->checkSpringMs = now;
	hi->checkTarget.clear();
	hairStats.resets++;
}

// a spring as the sidecar authors it, its travel limit times r_hairSpringLimit and its group's setting; false when that
// scale is 0 (the spring is off: its bone follows the animation)
static bool Hair_Spring( const hairAsset_t &h, int s, hsSpring &sp ) {
	sp = h.springs[s];
	const int group = ( sp.flags & HS_GROUP_MASK ) >> HS_GROUP_SHIFT;
	const cvar_t *g = group == HS_GROUP_CHEST ? r_hairSpringChest : group == HS_GROUP_HIPS ? r_hairSpringHips : r_hairSpringOther;
	const float k = r_hairSpringLimit->value * g->value;
	if ( !( k > 0.0f ) ) return false;
	if ( sp.maxDisplacement > 0.0f ) sp.maxDisplacement *= k;
	return true;
}

// a chain particle's drawn-offset scale: body particles (breasts, skirts, ties; HAIR_PARTICLE_BODY) follow the spring-bone
// settings, so r_hairSprings / r_hairSpringLimit / r_hairSpringChest|Hips|Other tune FF7R's Bonamik bodies and Stellar
// Blade's spring bones alike; hair follows r_hairChains
static float Hair_ParticleScale( int group ) {
	if ( !( group & HAIR_PARTICLE_BODY ) ) return r_hairChains->integer ? 1.0f : 0.0f;
	if ( !r_hairSprings->integer ) return 0.0f;
	const int g = ( group & HS_GROUP_MASK ) >> HS_GROUP_SHIFT;
	const cvar_t *c = g == HS_GROUP_CHEST ? r_hairSpringChest : g == HS_GROUP_HIPS ? r_hairSpringHips : r_hairSpringOther;
	const float k = r_hairSpringLimit->value * c->value;
	return k > 0.0f ? k : 0.0f;
}

// spring bones: fixed steps of springDt from their own accumulator, toward their animated heads lerped between their
// stored previous pose and this frame's
static void Hair_AdvanceSprings( hairInst_t *hi, double frame, int now, int mode, double prevFrame ) {
	const hairAsset_t &h = hi->asset->h;
	if ( !h.nSprings || !r_hairSprings->integer ) {
		hi->springAcc = hi->springBaseLag = 0;
		memcpy( hi->HprevS, hi->H, sizeof( hi->H ) );
		hi->GprevS = hi->G;
		return;
	}
	const double step = h.springDt;
	const int NG = (int)hi->gameGla.size();
	const bool hosts = Hair_HasHosts( hi );
	std::vector<float> &Gs = hi->scratchGs;   // kept on the instance
	Gs.resize( hi->G.size() + 16 );
	float Hs[16], target[3];
	simStepPlan_t plan;
	Sim_PlanSteps( &plan, hi->springAcc, hi->springBaseLag, frame, step, HAIR_SPRING_MAX_STEPS, mode,
		Sim_Carry( R_SimCarryFrames(), HAIR_SPRING_MAX_STEPS, step, frame, prevFrame ) );
	R_SimCheckPlan( SIMCHECK_SPRING, &plan );
	for ( int n = 0; n < plan.n; n++ ) {
		const double alpha = Sim_StepAlpha( &plan, n );
		const float fa = (float)alpha;
		M4_Lerp( Hs, hi->HprevS, hi->H, fa );
		for ( int k = 0; k < NG; k++ ) M4_Lerp( &Gs[k * 16], &hi->GprevS[k * 16], &hi->G[k * 16], fa );
		if ( hosts ) Hair_HostFrames( hi, &Gs[0] );
		hi->springOffA.swap( hi->springOffB );
		for ( int s = 0; s < h.nSprings; s++ ) {
			hsSpring sp;
			if ( !Hair_Spring( h, s, sp ) ) {   // off: start over at the target when it is turned back on
				hi->springSt[s].fresh = 1;
				for ( int k = 0; k < 3; k++ ) hi->springOffB[s * 3 + k] = 0.0f;
				continue;
			}
			M4_Point( target, hosts ? Hair_BoneFrame( hi, sp.bone, Hs ) : Hs, h.a.bones[sp.bone].bind + 12 );
			HS_Step( &sp, h.springBasis, &hi->springSt[s], target, (float)step );
			for ( int k = 0; k < 3; k++ ) hi->springOffB[s * 3 + k] = hi->springSt[s].loc[k] - target[k];
		}
		hairStats.springSteps++;
		{
			const double poseTime = now - ( 1.0 - alpha ) * plan.span * 1000.0;
			R_SimCheckStep( SIMCHECK_SPRING, poseTime - hi->checkSpringMs, step * 1000.0, 0.0 );
			hi->checkSpringMs = poseTime;   // kept whether or not the check is on: a later window starts from the right step
		}
	}
	hi->springAcc = plan.accAfter;
	hi->springBaseLag = plan.baseLag;
	if ( plan.baseLag > 0 ) {   // behind and catching up: the previous pose is the one the last step ran at
		memcpy( hi->HprevS, Hs, sizeof( Hs ) );
		for ( int k = 0; k < NG; k++ ) M4_Lerp( &hi->GprevS[k * 16], &hi->GprevS[k * 16], &hi->G[k * 16], (float)plan.baseAlpha );
	} else {
		memcpy( hi->HprevS, hi->H, sizeof( hi->H ) );
		hi->GprevS = hi->G;
	}
}

// 2. advance: fixed steps from the accumulator
static void Hair_Advance( hairInst_t *hi, int now ) {
	const hcAsset &a = hi->asset->h.a;
	bool reset = !hi->live || now < hi->lastTime || now - hi->lastTime > HAIR_RESET_GAP_MS;
	if ( !hi->live ) hairStats.rFirst++;
	else if ( now < hi->lastTime ) hairStats.rBack++;
	else if ( reset ) hairStats.rGap++;
	if ( !reset ) {
		const float dx = hi->W[12] - hi->Wprev[12], dy = hi->W[13] - hi->Wprev[13], dz = hi->W[14] - hi->Wprev[14];
		const float dist = sqrtf( dx * dx + dy * dy + dz * dz );
		// Wprev may be the solver's older pose (baseLag)
		const float limit = HAIR_TELEPORT_UNITS + HAIR_MAX_SPEED * ( ( now - hi->lastTime ) * 0.001f + (float)hi->baseLag );
		if ( dist > limit ) {
			reset = true;
			hairStats.rJump++;
		}
	}
	if ( reset ) {
		Hair_Reset( hi, now );
		return;
	}
	if ( now == hi->lastTime ) return;

	const double step = a.dt;
	const double frame = ( now - hi->lastTime ) * 0.001;
	const int maxSteps = r_hairMaxSteps->integer > 0 ? r_hairMaxSteps->integer : 1;
	const int NG = (int)hi->gameGla.size();
	const int mode = R_SimStepMode();
	std::vector<float> &Gs = hi->scratchGs;   // kept on the instance
	Gs.resize( hi->G.size() + 16 );
	std::vector<float> &prePos = hi->scratchPre, &prePrev = hi->scratchPrev;   // r_hairDump only
	if ( r_hairDump->string[0] ) {
		prePos.resize( (size_t)a.nParticles * 3 );
		prePrev.resize( prePos.size() );
	}
	float Ws[16], Hs[16];
	Hair_AdvanceSprings( hi, frame, now, mode, hi->prevFrame );
	if ( !a.nParticles ) {   // a springs-only asset: no chain steps
		hi->acc = hi->baseLag = 0;
		hi->prevFrame = frame;
		hi->lastTime = now;
		memcpy( hi->Wprev, hi->W, sizeof( hi->W ) );
		memcpy( hi->Hprev, hi->H, sizeof( hi->H ) );
		hi->Gprev = hi->G;
		return;
	}
	simStepPlan_t plan;
	Sim_PlanSteps( &plan, hi->acc, hi->baseLag, frame, step, maxSteps, mode, Sim_Carry( R_SimCarryFrames(), maxSteps, step, frame, hi->prevFrame ) );
	R_SimCheckPlan( SIMCHECK_HAIR, &plan );
	hi->prevFrame = frame;
	for ( int n = 0; n < plan.n; n++ ) {
		// the step's inputs: lerped between the stored previous pose and this frame's
		const double alpha = Sim_StepAlpha( &plan, n );
		const float fa = (float)alpha;
		M4_Lerp( Ws, hi->Wprev, hi->W, fa );
		M4_Lerp( Hs, hi->Hprev, hi->H, fa );
		for ( int k = 0; k < NG; k++ ) M4_Lerp( &Gs[k * 16], &hi->Gprev[k * 16], &hi->G[k * 16], fa );
		hcSim &s = hi->sim;
		// a stretched step covers more pose time than it integrates: the solver measures the frame's velocity over it
		HC_SetWorldScale( &s, r_hairWorldScale->integer ? (hc_real)hi->worldScale : 1 );
		HC_BeginFrameDt( &s, Ws, ++hi->stepCounter, (hc_real)Sim_StepPoseDt( &plan ) );
		Hair_SetInputs( hi, Hs, &Gs[0] );
		{
			const double poseTime = now - ( 1.0 - alpha ) * plan.span * 1000.0;
			hi->checkStepMs = poseTime - hi->checkPoseMs;   // kept whether or not the check is on
			hi->checkPoseMs = poseTime;
		}
		if ( !R_SimCheckOn() ) {
			hi->checkTarget.clear();
		} else {
			// the largest move of an animated particle target since the last step (model space)
			const size_t nt = (size_t)a.nParticles * 3;
			double jump = 0;
			if ( hi->checkTarget.size() == nt ) {
				for ( int i = 0; i < a.nParticles; i++ ) {
					const float *o = &hi->checkTarget[i * 3];
					const double d = sqrt( ( s.target[i][0] - o[0] ) * ( s.target[i][0] - o[0] ) + ( s.target[i][1] - o[1] ) * ( s.target[i][1] - o[1] )
						+ ( s.target[i][2] - o[2] ) * ( s.target[i][2] - o[2] ) );
					if ( d > jump ) jump = d;
				}
			}
			hi->checkTarget.assign( &s.target[0][0], &s.target[0][0] + nt );
			R_SimCheckStep( SIMCHECK_HAIR, hi->checkStepMs, ( plan.stretched ? plan.poseDt : plan.step ) * 1000.0, jump );
		}
		if ( r_hairDump->string[0] ) {
			memcpy( &prePos[0], s.pos, prePos.size() * sizeof( float ) );
			memcpy( &prePrev[0], s.prev, prePrev.size() * sizeof( float ) );
		}
		const double t0 = HairNowMs();
		HC_Step( &s );
		const double ms = HairNowMs() - t0;
		hairStats.steps++;
		hairStats.stepMs += ms;
		if ( ms > hairStats.stepMax ) hairStats.stepMax = ms;
		bool finite = true;
		for ( int i = 0; i < a.nParticles && finite; i++ ) {
			for ( int k = 0; k < 3; k++ ) {
				const float v = s.pos[i][k];
				if ( v != v || fabsf( v ) > 1e9f ) finite = false;
			}
		}
		if ( !finite ) {
			ri.Printf( PRINT_DEVELOPER, "hair: %s: non-finite state, reset\n", hi->asset->name );
			hairStats.rNan++;
			Hair_Reset( hi, now );
			return;
		}
		if ( R_SimCheckOn() ) {
			hc_real deepest = 0;
			const int inside = HC_CountInside( &s, 0.1f * a.unitsPerCm, &deepest );
			R_SimCheckInside( SIMCHECK_HAIR, inside, deepest );
		}
		if ( r_hairDump->string[0] ) Hair_Dump( hi, Sim_DumpFlags( 0, &plan ), Ws, &prePos[0], &prePrev[0] );
		Hair_StoreRel( hi, Hs, &Gs[0] );
	}
	hi->acc = plan.accAfter;
	hi->baseLag = plan.baseLag;
	hi->lastTime = now;
	if ( plan.baseLag > 0 ) {
		// behind and catching up: the previous pose is the one the solver's last step ran at
		const float fb = (float)plan.baseAlpha;
		M4_Lerp( hi->Wprev, hi->Wprev, hi->W, fb );
		M4_Lerp( hi->Hprev, hi->Hprev, hi->H, fb );
		for ( int k = 0; k < NG; k++ ) M4_Lerp( &hi->Gprev[k * 16], &hi->Gprev[k * 16], &hi->G[k * 16], fb );
	} else {
		memcpy( hi->Wprev, hi->W, sizeof( hi->W ) );
		memcpy( hi->Hprev, hi->H, sizeof( hi->H ) );
		hi->Gprev = hi->G;
	}
}

// 3. this frame's hair bone skinning matrices
static void Hair_BuildPalette( hairInst_t *hi ) {
	const hairModelAsset_t *as = hi->asset;
	const hcAsset &a = as->h.a;
	const int N = a.nParticles, NB = a.nBones;
	const bool hosts = Hair_HasHosts( hi );
	if ( hosts ) Hair_HostFrames( hi, hi->G.data() );
	for ( int b = 0; b < NB; b++ ) M4_Mul( &hi->anim[b * 16], hosts ? Hair_BoneFrame( hi, b, hi->H ) : hi->H, a.bones[b].bind );
	// r_hairFade: 1 inside, falling to 0 at r_hairMaxDist over its last share; beyond it the hair is not
	// simulated and draws as the animation, so the fade ends where the cut was
	float fade = 1.0f;
	if ( r_hairFade->value > 0.0f && r_hairMaxDist->value > 0.0f ) {
		const float band = r_hairFade->value * r_hairMaxDist->value;
		fade = ( r_hairMaxDist->value - hi->lastDist ) / band;
		fade = fade < 0.0f ? 0.0f : fade > 1.0f ? 1.0f : fade;
	}
	if ( r_hair->integer == 2 || !N || ( !r_hairChains->integer && !as->bodyParticles ) ) {
		hi->posed = hi->anim;   // r_hair 2: the identity check, no simulation; no particles (springs only) or r_hairChains 0: the chains follow the animation
	} else {
		float t = (float)( hi->acc / a.dt );
		if ( t < 0 ) t = 0;
		if ( t > 1 ) t = 1;
		for ( int i = 0; i < N; i++ ) {
			float r[3];
			for ( int k = 0; k < 3; k++ ) r[k] = hi->relA[i * 3 + k] + t * ( hi->relB[i * 3 + k] - hi->relA[i * 3 + k] );
			M4_Point( &hi->drawPos[i * 3], hosts ? Hair_BoneFrame( hi, a.particles[i].bone, hi->H ) : hi->H, r );
			// body particles take the spring-bone settings (one set of controls across models), hair takes r_hairChains:
			// the drawn offset from the animated pose times that scale (0 = the animation, 1 = as simulated); the solver's
			// own state is untouched, so a dump replays the same whatever the settings
			const float s = Hair_ParticleScale( a.particles[i].group ) * fade;
			if ( s != 1.0f ) {
				const float *T = &hi->anim[a.particles[i].bone * 16 + 12];
				for ( int k = 0; k < 3; k++ ) hi->drawPos[i * 3 + k] = T[k] + s * ( hi->drawPos[i * 3 + k] - T[k] );
			}
		}
		HC_PoseFrom( &hi->sim, (const hc_real (*)[3]) & hi->drawPos[0], &hi->anim[0], &hi->posed[0] );
	}
	if ( r_hair->integer != 2 ) {
		const hairAsset_t &h = as->h;
		if ( h.nSprings && r_hairSprings->integer ) {
			// spring bones: the last two steps' offsets blended, moving the bone and those hanging from it
			float st = (float)( hi->springAcc / h.springDt );
			if ( st < 0 ) st = 0;
			if ( st > 1 ) st = 1;
			for ( int s = 0; s < h.nSprings; s++ ) {
				hsSpring sp;
				if ( !Hair_Spring( h, s, sp ) ) continue;
				const float *F = hosts ? Hair_BoneFrame( hi, sp.bone, hi->H ) : hi->H;
				float off[3], pivot[3], delta[16];
				for ( int k = 0; k < 3; k++ ) off[k] = fade * ( hi->springOffA[s * 3 + k] + st * ( hi->springOffB[s * 3 + k] - hi->springOffA[s * 3 + k] ) );
				M4_Point( pivot, F, sp.pivot );
				if ( r_hairStats->value > 0.0f ) {
					float len = sqrtf( off[0] * off[0] + off[1] * off[1] + off[2] * off[2] );
					if ( sp.maxDisplacement > 0.0f && len > sp.maxDisplacement ) len = sp.maxDisplacement;
					len *= sp.alpha < 0.0f ? 0.0f : sp.alpha > 1.0f ? 1.0f : sp.alpha;
					const double cm = len / h.a.unitsPerCm;
					hairStats.springSamples++;
					hairStats.springSumCm += cm;
					if ( cm > hairStats.springMaxCm ) hairStats.springMaxCm = cm;
				}
				if ( !HS_Delta( &sp, h.springBasis, off, &hi->anim[sp.bone * 16 + 12], pivot, delta ) ) continue;
				for ( int k = hi->springBoneStart[s]; k < hi->springBoneStart[s + 1]; k++ ) {   // precomputed, same order
					const int b = hi->springBones[k];
					M4_Mul( &hi->posed[b * 16], delta, &hi->posed[b * 16] );
				}
			}
		}
	}
	for ( int b = 0; b < NB; b++ ) {
		float K[16];
		M4_Mul( K, &hi->posed[b * 16], &as->bindInv[b * 16] );
		mdxaBone_t &o = hi->palette[b];
		for ( int r = 0; r < 3; r++ ) {
			for ( int c = 0; c < 4; c++ ) o.matrix[r][c] = K[c * 4 + r];
		}
	}
}

static void Hair_CheckCvars( void ) {
	if ( !r_hairDump->string[0] && hairDumpName[0] ) {
		Hair_CloseDump();
		hairDumpName[0] = 0;
	}
	cvar_t *knobs[] = { r_hairInverse, r_hairFeel, r_hairFeelLin, r_hairFeelMaxG, r_hairFeelLag, r_hairFeelEuler,
		r_hairFeelAngAcc, r_hairFeelCent, r_hairFeelAngVel, r_hairFeelAngLag, r_hairFlow, r_hairFlowDamp, r_hairFlowInner,
		r_hairFlowDrag, r_hairFlowParent };
	bool changed = false;
	for ( size_t k = 0; k < ARRAY_LEN( knobs ); k++ ) {
		if ( knobs[k]->modified ) {
			knobs[k]->modified = qfalse;
			changed = true;
		}
	}
	if ( changed ) {
		for ( size_t i = 0; i < hairInsts.size(); i++ ) Hair_ApplySettings( hairInsts[i] );
	}
}

static void Hair_Housekeeping( void ) {
	if ( tr.frameCount == hairStats.lastFrameCount ) return;
	hairStats.frames++;
	hairStats.lastFrameCount = tr.frameCount;
	const int real = ri.Milliseconds();
	for ( size_t i = 0; i < hairInsts.size(); ) {
		if ( real - hairInsts[i]->lastSeenReal > HAIR_IDLE_FREE_MS ) {
			Hair_FreeInst( hairInsts[i] );
			hairInsts.erase( hairInsts.begin() + i );
		} else {
			i++;
		}
	}
	if ( r_hairStats->value > 0.0f ) {
		if ( !hairStats.lastPrint ) {
			hairStats.lastPrint = real;
			hairStats.frames = 0;
		}
		const int span = real - hairStats.lastPrint;
		if ( span >= (int)( r_hairStats->value * 1000.0f ) ) {
			int live = 0;
			for ( size_t i = 0; i < hairInsts.size(); i++ ) live += hairInsts[i]->ready ? 1 : 0;
			const double sec = span * 0.001;
			ri.Printf( PRINT_ALL, "HAIRSTATS r_hair %d feel %d flow %d inverse %d inst %d live %d | %.1f fps | steps %d (%.1f/s) resets %d (first %d gap %d back %d jump %d nan %d) | "
				"step ms mean %.3f max %.3f | hair ms/frame %.3f\n",
				r_hair->integer, r_hairFeel->integer, r_hairFlow->integer, r_hairInverse->integer, (int)hairInsts.size(), live, hairStats.frames / sec,
				hairStats.steps, hairStats.steps / sec, hairStats.resets, hairStats.rFirst, hairStats.rGap, hairStats.rBack,
				hairStats.rJump, hairStats.rNan, hairStats.steps ? hairStats.stepMs / hairStats.steps : 0.0, hairStats.stepMax,
				hairStats.frames ? hairStats.frontMs / hairStats.frames : 0.0 );
			if ( hairStats.springSamples ) {
				ri.Printf( PRINT_ALL, "HAIRSPRINGS r_hairSprings %d limit x%g chest x%g hips x%g other x%g chains %d | steps %d (%.1f/s) | drawn offset cm mean %.3f max %.3f\n",
					r_hairSprings->integer, r_hairSpringLimit->value, r_hairSpringChest->value, r_hairSpringHips->value,
					r_hairSpringOther->value, r_hairChains->integer, hairStats.springSteps, hairStats.springSteps / sec,
					hairStats.springSumCm / hairStats.springSamples, hairStats.springMaxCm );
			}
			for ( size_t i = 0; i < hairInsts.size(); i++ )	// each instance's camera distance (r_hairFade checks)
				ri.Printf( PRINT_ALL, "HAIRDIST %s %.1f ready %d\n", hairInsts[i]->asset->name, hairInsts[i]->lastDist, hairInsts[i]->ready ? 1 : 0 );
			const int keep = hairStats.lastFrameCount;
			memset( &hairStats, 0, sizeof( hairStats ) );
			hairStats.lastPrint = real;
			hairStats.lastFrameCount = keep;
		}
	} else {
		hairStats.lastPrint = 0;
	}
}

void R_HairAddGhoul( const trRefEntity_t *ent, const model_t *mod, const mdxaHeader_t *aHeader, CBoneCache *bc, qboolean personalModel,
	qboolean shadowView ) {
	if ( !r_hair || !bc || !mod || !aHeader ) return;
	R_SimCheckFrame();
	const bool perFrame = R_SimPerFrame();
	if ( !perFrame && shadowView ) return;   // as before: a shadow view draws what the other views left
	Hair_Housekeeping();
	Hair_CheckCvars();
	hairInst_t *hi = R_HairFindInst( bc );
	// one decision per frame: a view after the one that made the instance ready neither clears nor rebuilds it
	if ( perFrame && hi && hi->ready && Sim_GateSeen( &hi->gate, tr.frameCount, tr.frameSceneNum ) ) return;
	if ( hi ) hi->ready = false;
	if ( !r_hair->integer ) return;

	hairModelAsset_t *as = Hair_AssetForModel( mod );
	if ( !as || !Hair_BindAsset( as, mod ) ) return;
	if ( personalModel ) return;  // this view does not draw it (first person); a later one (a mirror) may
	if ( perFrame ) {
		// from the scene's camera, whichever view asks: a portal, mirror or shadow camera does not change the answer
		const int in = Sim_InRange( Distance( ent->e.origin, tr.refdef.vieworg ), r_hairMaxDist->value, hi ? hi->gate.inRange : 0 );
		if ( hi ) hi->gate.inRange = in;
		if ( !in ) return;
	} else if ( r_hairMaxDist->value > 0.0f && Distance( ent->e.origin, tr.viewParms.ori.origin ) > r_hairMaxDist->value ) return;

	if ( hi && hi->asset != as ) {  // the instance switched models
		R_HairForgetBoneCache( bc );
		hi = NULL;
	}
	bool first = false;
	if ( !hi ) {
		hairLoadTimer_t timer( HAIRLOAD_INST );
		hi = Hair_CreateInst( bc, as, Distance( ent->e.origin, tr.refdef.vieworg ) );
		if ( !hi ) return;
		first = true;
	}
	hi->lastSeenReal = ri.Milliseconds();
	hi->lastDist = Distance( ent->e.origin, tr.refdef.vieworg );
	if ( hi->aHeader != aHeader && ( hi->bindFailed == aHeader || !Hair_BindBones( hi, aHeader ) ) ) {
		hi->aHeader = NULL;
		hi->bindFailed = aHeader;   // tried again only for another skeleton
		return;
	}
	const double tPrep = HairNowMs();
	const bool prepared = R_HairRBPrepare( hi );
	if ( first ) {
		r_hairLoadMs[HAIRLOAD_FIRST] += HairNowMs() - tPrep;
		r_hairLoadCount[HAIRLOAD_FIRST]++;
	}
	if ( !prepared ) return;

	const double t0 = HairNowMs();
	const int now = tr.refdef.time;
	hi->sim.fast = r_hairFast->integer & 1;
	Hair_GatherPose( hi, bc, ent );
	if ( hi->drawTime != now || !hi->live ) {
		Hair_Advance( hi, now );
	}
	Hair_BuildPalette( hi );
	hi->drawTime = now;
	hi->ready = true;
	Sim_GateSet( &hi->gate, tr.frameCount, tr.frameSceneNum );
	hi->gate.inRange = 1;
	hairStats.frontMs += HairNowMs() - t0;
}

// model warm-up, after R_HairWarmModel: the substitute buffers the first draw would build, for the model's own
// skeleton (its GLA), one LOD a call. A draw with another skeleton rebuilds them for that one, as before
bool R_HairWarmModelGpu( const model_t *mod ) {
	if ( !r_hair || !r_hair->integer || !mod || mod->type != MOD_MDXM || !mod->data.glm ) return true;
	hairModelAsset_t *as = Hair_AssetForModel( mod );
	if ( !as || !Hair_BindAsset( as, mod ) ) return true;
	const model_t *anim = R_GetModelByHandle( mod->data.glm->header->animIndex );
	const mdxaHeader_t *aHeader = anim && anim->type == MOD_MDXA ? anim->data.gla : NULL;
	if ( !aHeader ) return true;
	const hairAsset_t &h = as->h;
	const int host = Hair_GlaBone( aHeader, h.hostName );
	if ( host < 0 || aHeader->numBones + h.a.nBones > HAIR_MAX_PALETTE ) return true;   // the draw warns
	std::vector<int> game( h.nGameBones, -1 );
	for ( int k = 0; k < h.nGameBones; k++ ) {
		game[k] = Hair_GlaBone( aHeader, h.gameBoneNames[k] );
		if ( game[k] < 0 ) return true;
	}

	return R_HairRBPrepareFor( as, aHeader->numBones, host, game, 1 ) != -1;
}

void R_HairRegister( void ) {
	R_SimRegister();
	r_hair = ri.Cvar_Get( "r_hair", "1", CVAR_ARCHIVE_ND, "Hair chains on Ghoul2 models that ship a .chains sidecar: 0 rigid, 1 simulate, 2 posed without simulation (check)" );
	r_hairInverse = ri.Cvar_Get( "r_hairInverse", "1", CVAR_NONE, "Hair: keep hair inside the asset's containment shapes (0 = push out of them, as the reference solver does)" );
	r_hairFeel = ri.Cvar_Get( "r_hairFeel", "1", CVAR_NONE, "Hair: 1 feel the entity's runs, turns and jumps (game profile), 0 the asset's own weights only" );
	r_hairFeelLin = ri.Cvar_Get( "r_hairFeelLin", "1", CVAR_NONE, "Hair game profile: linear inertia weight" );
	r_hairFeelMaxG = ri.Cvar_Get( "r_hairFeelMaxG", "1.5", CVAR_NONE, "Hair game profile: most linear acceleration felt, in units of gravity (0 none, negative no limit)" );
	r_hairFeelLag = ri.Cvar_Get( "r_hairFeelLag", "300", CVAR_NONE, "Hair game profile: velocity change carried over at most (units/s); the rest is dropped" );
	r_hairFeelEuler = ri.Cvar_Get( "r_hairFeelEuler", "1", CVAR_NONE, "Hair game profile: turn drag weight (angular acceleration)" );
	r_hairFeelAngAcc = ri.Cvar_Get( "r_hairFeelAngAcc", "9000", CVAR_NONE, "Hair game profile: most angular acceleration felt (deg/s^2; 0 none, negative no limit)" );
	r_hairFeelCent = ri.Cvar_Get( "r_hairFeelCent", "1", CVAR_NONE, "Hair game profile: spin flare weight (centrifugal)" );
	r_hairFeelAngVel = ri.Cvar_Get( "r_hairFeelAngVel", "720", CVAR_NONE, "Hair game profile: most spin felt for the flare (deg/s; 0 none, negative no limit)" );
	r_hairFeelAngLag = ri.Cvar_Get( "r_hairFeelAngLag", "1080", CVAR_NONE, "Hair game profile: spin change carried over at most (deg/s)" );
	r_hairFlow = ri.Cvar_Get( "r_hairFlow", "1", CVAR_NONE, "Hair: 1 damp the cone springs and add air drag so locks swing and settle instead of ringing (r_hairFlow*), 0 the asset's springs alone" );
	r_hairFlowDamp = ri.Cvar_Get( "r_hairFlowDamp", "2", CVAR_NONE, "Hair flow: damper beside each cone spring, times the asset's constraint damping (0 = none)" );
	r_hairFlowInner = ri.Cvar_Get( "r_hairFlowInner", "0.5", CVAR_NONE, "Hair flow: cone spring strength, times the asset's (1 = authored)" );
	r_hairFlowDrag = ri.Cvar_Get( "r_hairFlowDrag", "3", CVAR_NONE, "Hair flow: air drag (1/s) on the hair's own motion and the entity's run and spin" );
	r_hairFlowParent = ri.Cvar_Get( "r_hairFlowParent", "0", CVAR_NONE, "Hair flow: 0..1 cones follow their parent segment's swing (experimental; 0.75 flails)" );
	r_hairPreroll = ri.Cvar_Get( "r_hairPreroll", "12", CVAR_NONE, "Hair: solver steps run on the reset pose before the hair's first draw after a reset (spawn, teleport, back in range); 0 = it falls into place visibly" );
	r_hairWorldScale = ri.Cvar_Get( "r_hairWorldScale", "1", CVAR_NONE, "Hair on an entity drawn scaled (modelScale) feels gravity and the entity's motion in its own units; 0 = in world units (1/scale times too strongly)" );
	r_hairMaxDist = ri.Cvar_Get( "r_hairMaxDist", "1500", CVAR_NONE, "Simulate hair only within this distance of the viewer (0 = any)" );
	r_hairFade = ri.Cvar_Get( "r_hairFade", "0.2", CVAR_ARCHIVE_ND, "Hair: the drawn hair and spring offsets blend to the animation over the last share of r_hairMaxDist (default 0.2 = the last 20%), so hair does not snap rigid at the limit; 0 = a hard cut. Free: it scales drawn offsets only, the simulated set is unchanged" );
	r_hairMaxSteps = ri.Cvar_Get( "r_hairMaxSteps", "4", CVAR_NONE, "Maximum catch-up hair steps per frame" );
	r_hairMaxInstances = ri.Cvar_Get( "r_hairMaxInstances", "8", CVAR_NONE, "Maximum simulated hair instances" );
	r_hairFast = ri.Cvar_Get( "r_hairFast", "1", CVAR_NONE, "Hair solver shortcuts with the same results (bits; 0 = off, for A/B): 1 = each cone turns its bone subtree from lists made once" );
	r_hairStats = ri.Cvar_Get( "r_hairStats", "0", CVAR_NONE, "Print hair cost every N seconds" );
	r_hairDump = ri.Cvar_Get( "r_hairDump", "", CVAR_NONE, "Debug: write every hair step of one instance to this file under the home path (tools/hair)" );
	r_hairSprings = ri.Cvar_Get( "r_hairSprings", "1", CVAR_ARCHIVE_ND, "Body physics (spring bones, and the body chains of FF7R models: breasts, skirts, ties) on models whose .chains sidecar has them: 0 off (the animated pose), 1 on" );
	r_hairSpringLimit = ri.Cvar_Get( "r_hairSpringLimit", "1", CVAR_ARCHIVE_ND, "Body physics: how far it may move, times the source game's (1 = as the game: spring bones breasts 1 cm, hips 2 cm; body chains as simulated; 2 = twice that; 0 = off)" );
	r_hairSpringChest = ri.Cvar_Get( "r_hairSpringChest", "1", CVAR_ARCHIVE_ND, "Body physics, breasts: times this, on top of r_hairSpringLimit (0 = off)" );
	r_hairSpringHips = ri.Cvar_Get( "r_hairSpringHips", "1", CVAR_ARCHIVE_ND, "Body physics, hips / buttocks and upper thighs: times this, on top of r_hairSpringLimit (0 = off)" );
	r_hairSpringOther = ri.Cvar_Get( "r_hairSpringOther", "1", CVAR_ARCHIVE_ND, "Body physics, the rest (skirts, ties, accessories, wires, belly, arms): times this, on top of r_hairSpringLimit (0 = off)" );
	r_hairChains = ri.Cvar_Get( "r_hairChains", "1", CVAR_ARCHIVE_ND, "Hair chains (hair and what hangs from the head): 1 simulate, 0 follow the animation while the body physics still moves" );
	R_HairRBRegister();
	r_hairInverse->modified = qfalse;
	r_hairFeel->modified = qfalse;
}
