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
Garment cloth on Ghoul2 models: renderer glue shared by rd-vanilla and rd-vulkan. See tr_cloth.h.

Per instance and per scene time advance (front end):
  1. pose:    the render bone matrices of the gla bones the asset names (asset order), in the entity's
              model space (L, cm, row-vector) and in the world (M = L * W, W = entity axis/origin)
  2. advance: fixed steps of 1/r_clothHz s from an accumulator, planned by Sim_PlanSteps (sim_frame.h);
              each catch-up step is driven by a pose lerped between the stored previous pose and the
              current frame's (SkirtPose_Lerp). Each step's output is stored relative to that step's
              reference bone (pelvis) frame.
One decision per instance and frame: the first view that draws the model (a shadow view too) runs these
steps, the other views of the frame reuse the result, and the back end only draws data stamped with its
own frame (tr_sim.h; docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md).
  3. draw:    the newest step state is carried forward to the frame time along the last two (r_clothDrawLead
              1; 2, the default, as 1 with each step's correction blended in over the next step; 0 blends the
              last two, a step behind) in the pelvis frame and placed with the current pelvis; pinned particles
              are skinned from the current pose directly; normals from the sim mesh.
Step 2 touches only instance-owned buffers and the solver: it is the seam for a worker thread
(snapshot the pose in step 1, publish the draw buffer after step 3).

Each renderer's back end draws the draw buffer through the asset's render map, for LOD 0 surfaces only
(rd-vanilla/tr_cloth_rb.cpp, rd-vulkan/vk_cloth.cpp).
*/
#include "tr_local.h"
#include "rd-common/tr_cloth_local.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <math.h>

cvar_t *r_cloth;
static cvar_t *r_clothHz;
static cvar_t *r_clothBend;
static cvar_t *r_clothMaxDist;
static cvar_t *r_clothStats;
static cvar_t *r_clothParam;
static cvar_t *r_clothMaxInstances;
static cvar_t *r_clothMaxSteps;
static cvar_t *r_clothDebug;
static cvar_t *r_clothProfile;
static cvar_t *r_clothDump;
static cvar_t *r_clothDrawLead;
static cvar_t *r_clothSettle;
static cvar_t *r_clothFeltSpeed;

static fileHandle_t clothDumpFile;
static char clothDumpName[MAX_QPATH];   // the dump's name; kept after its instance is gone so it is not reopened (truncated)
static const clothInst_t *clothDumpInst;  // the one instance the open dump records (the replay takes one asset)

static const int CLOTH_RESET_GAP_MS = 500;      // unseen this long (or time went backwards): reset
// Teleport test (reset to rest): the reference bone moved further than legitimate motion can carry it in the
// elapsed scene time, i.e. more than CLOTH_TELEPORT_UNITS plus CLOTH_MAX_SPEED * dt. A fixed per-frame distance
// alone misfires on long frames (hitches, screenshots, a few unseen frames). Player motion (runs, Force speed,
// knockback, long falls) stays under ~2000 u/s; setviewpos, teleporters and respawns move the body 100+ units
// in one frame. A smaller jump is simulated as fast motion (the tethers bound it).
static const float CLOTH_TELEPORT_UNITS = 96.0f;
static const float CLOTH_MAX_SPEED = 2000.0f;   // units per second
static const int CLOTH_IDLE_FREE_MS = 5000;     // free a solver unseen this long (real time)

static double ClothNowMs( void ) {
	using namespace std::chrono;
	return duration<double, std::milli>( steady_clock::now().time_since_epoch() ).count();
}

double	r_clothLoadMs[CLOTHLOAD_COUNT];
int		r_clothLoadCount[CLOTHLOAD_COUNT];

// adds its scope's time to one load stage (tr_cloth.h)
struct clothLoadTimer_t {
	int slot;
	double t0;
	explicit clothLoadTimer_t( int s ) : slot( s ), t0( ClothNowMs() ) {}
	~clothLoadTimer_t() {
		r_clothLoadMs[slot] += ClothNowMs() - t0;
		r_clothLoadCount[slot]++;
	}
};

/* ------------------------------------------------------------------ assets */


static std::vector<clothModelAsset_t *> clothAssets;
static std::unordered_map<std::string, clothModelAsset_t *> clothAssetByName;   // lower-case GLM name -> clothAssets entry
// the same by model, so a model drawn in every view does not build a name key each time; an entry counts only while
// its asset has the model's name (the model cache may free a model and load another at the same address)
static std::unordered_map<const model_t *, clothModelAsset_t *> clothAssetByModel;

static void Cloth_FreeAsset( clothModelAsset_t *as ) {
	R_ClothRBFreeAsset( as );
	if ( as->buf ) free( as->buf );
	delete as;
}

static clothModelAsset_t *Cloth_AssetForModel( const model_t *mod ) {
	// a hash lookup, not a case-insensitive compare down every Ghoul2 model ever drawn, per entity and view
	std::unordered_map<const model_t *, clothModelAsset_t *>::const_iterator known = clothAssetByModel.find( mod );
	if ( known != clothAssetByModel.end() && !Q_stricmp( known->second->name, mod->name ) ) {
		return known->second->missing ? NULL : known->second;
	}
	char key[MAX_QPATH];
	Q_strncpyz( key, mod->name, sizeof( key ) );
	Q_strlwr( key );
	std::unordered_map<std::string, clothModelAsset_t *>::const_iterator found = clothAssetByName.find( key );
	if ( found != clothAssetByName.end() ) {
		clothAssetByModel[mod] = found->second;
		return found->second->missing ? NULL : found->second;
	}
	clothLoadTimer_t timer( CLOTHLOAD_ASSET );
	clothModelAsset_t *as = new clothModelAsset_t();
	Q_strncpyz( as->name, mod->name, sizeof( as->name ) );
	as->missing = true;
	as->buf = NULL;
	as->boundMod = NULL;
	as->bindOk = false;
	as->normalSign = 1.0f;
	as->rb = NULL;
	clothAssets.push_back( as );
	clothAssetByName[key] = as;
	clothAssetByModel[mod] = as;

	char path[MAX_QPATH];
	COM_StripExtension( mod->name, path, sizeof( path ) );
	Q_strcat( path, sizeof( path ), ".cloth" );
	void *fileBuf = NULL;
	long len = ri.FS_ReadFile( path, &fileBuf );
	if ( len <= 0 || !fileBuf ) {
		return NULL;
	}
	as->buf = malloc( (size_t)len + 16 );   // 8-aligned copy; the parser keeps pointers into it
	if ( !as->buf ) {
		ri.FS_FreeFile( fileBuf );
		return NULL;
	}
	memcpy( as->buf, fileBuf, (size_t)len );
	ri.FS_FreeFile( fileBuf );
	char err[128];
	if ( !ClothAsset_Parse( as->buf, (size_t)len, 1, &as->a, err, sizeof( err ) ) ) {
		ri.Printf( PRINT_WARNING, "cloth: %s: %s\n", path, err );
		free( as->buf );
		as->buf = NULL;
		return NULL;
	}
	ClothAsset_ToSkirt( &as->a, &as->sa );
	if ( !SkirtSim_RequiredSize( &as->sa ) ) {
		ri.Printf( PRINT_WARNING, "cloth: %s: the solver rejects the asset\n", path );
		free( as->buf );
		as->buf = NULL;
		return NULL;
	}
	as->missing = false;
	ri.Printf( PRINT_ALL, "cloth: %s: %d particles, %d tris, collider %d verts, %d capsules, %d render surfaces / %d verts\n",
		path, as->a.nVerts, as->a.nTris, as->a.nColVerts, as->a.nCaps, as->a.nRenderSurfaces, as->a.nRenderVerts );
	return as;
}

static const mdxmSurfHierarchy_t *Cloth_SurfHier( const mdxmHeader_t *mdxm, int index ) {
	const mdxmHierarchyOffsets_t *offs = (const mdxmHierarchyOffsets_t *)( (const byte *)mdxm + sizeof( mdxmHeader_t ) );
	return (const mdxmSurfHierarchy_t *)( (const byte *)offs + offs->offsets[index] );
}

static const mdxmSurface_t *Cloth_Lod0Surface( const mdxmHeader_t *mdxm, int index ) {
	const byte *current = (const byte *)mdxm + mdxm->ofsLODs + sizeof( mdxmLOD_t );
	const mdxmLODSurfOffset_t *indexes = (const mdxmLODSurfOffset_t *)current;
	return (const mdxmSurface_t *)( current + indexes->offsets[index] );
}

// Map the asset's render surfaces onto this glm (by name, LOD 0, vertex counts) and orient the normals.
static bool Cloth_BindAsset( clothModelAsset_t *as, const model_t *mod ) {
	if ( as->boundMod == mod ) {
		return as->bindOk;
	}
	as->boundMod = mod;
	as->bindOk = false;
	const mdxmHeader_t *mdxm = mod->type == MOD_MDXM ? R_ClothModelMdxm( mod ) : NULL;
	if ( !mdxm || mdxm->numLODs < 1 ) {
		return false;
	}
	const cloth_asset_t &a = as->a;
	as->surfToRS.assign( mdxm->numSurfaces, -1 );
	as->rsStart.assign( a.nRenderSurfaces, 0 );
	as->rsCount.assign( a.nRenderSurfaces, 0 );
	as->rsLod0.assign( a.nRenderSurfaces, (const mdxmSurface_t *)NULL );
	for ( int s = 0; s < a.nRenderSurfaces; s++ ) {
		char name[CLOTH_NAME_LEN + 1];
		int count = 0;
		as->rsStart[s] = ClothAsset_RenderSurface( &a, s, name, sizeof( name ), &count );
		as->rsCount[s] = count;
		int found = -1;
		for ( int i = 0; i < mdxm->numSurfaces; i++ ) {
			if ( !Q_stricmp( Cloth_SurfHier( mdxm, i )->name, name ) ) {
				found = i;
				break;
			}
		}
		if ( found < 0 ) {
			ri.Printf( PRINT_WARNING, "cloth: %s has no surface %s\n", mod->name, name );
			return false;
		}
		const mdxmSurface_t *surf = Cloth_Lod0Surface( mdxm, found );
		if ( surf->numVerts != count ) {
			ri.Printf( PRINT_WARNING, "cloth: %s surface %s has %d verts, the asset maps %d\n", mod->name, name, surf->numVerts, count );
			return false;
		}
		as->surfToRS[found] = s;
		as->rsLod0[s] = surf;
	}
	// normal sign: sim-mesh normals in bind space against the glm's own vertex normals (version 1; version 2 stores a
	// sign per render vertex)
	if ( a.version == 1 ) {
		std::vector<float> n( (size_t)a.nVerts * 3, 0.0f );
		for ( int t = 0; t < a.nTris; t++ ) {
			const float *p0 = a.pos + a.tri[t * 3] * 3, *p1 = a.pos + a.tri[t * 3 + 1] * 3, *p2 = a.pos + a.tri[t * 3 + 2] * 3;
			vec3_t e1, e2, c;
			VectorSubtract( p1, p0, e1 );
			VectorSubtract( p2, p0, e2 );
			CrossProduct( e1, e2, c );
			for ( int k = 0; k < 3; k++ ) {
				float *d = &n[a.tri[t * 3 + k] * 3];
				VectorAdd( d, c, d );
			}
		}
		double dot = 0;
		for ( int s = 0; s < a.nRenderSurfaces; s++ ) {
			const mdxmVertex_t *v = (const mdxmVertex_t *)( (const byte *)as->rsLod0[s] + as->rsLod0[s]->ofsVerts );
			for ( int j = 0; j < as->rsCount[s]; j++ ) {
				dot += DotProduct( v[j].normal, &n[a.renderSim[as->rsStart[s] + j] * 3] );
			}
		}
		as->normalSign = dot < 0 ? -1.0f : 1.0f;
	}
	{
		const int R = a.nRenderVerts;
		as->rTri.assign( (size_t)R * 3, 0 );
		as->rW.assign( (size_t)R * 3, 0.0f );
		as->rOff.assign( R, 0.0f );
		as->rSgn.assign( R, 1.0f );
		for ( int v = 0; v < R; v++ ) {
			if ( a.version == 1 ) {
				as->rTri[v * 3] = as->rTri[v * 3 + 1] = as->rTri[v * 3 + 2] = a.renderSim[v];
				as->rW[v * 3] = 1.0f;
				as->rSgn[v] = as->normalSign;
			} else {
				for ( int k = 0; k < 3; k++ ) {
					as->rTri[v * 3 + k] = a.renderTri[v * 3 + k];
					as->rW[v * 3 + k] = a.renderW[v * 3 + k];
				}
				as->rOff[v] = a.renderOff[v];
				as->rSgn[v] = a.renderSgn[v] < 0.0f ? -1.0f : 1.0f;
			}
		}
	}
	as->bindOk = true;
	return true;
}

/* ------------------------------------------------------------------ instances */


static std::vector<clothInst_t *> clothInsts;
static clothInst_t *clothLastHit;

struct clothOverride_t {
	std::string key;
	double value;
};
static std::vector<clothOverride_t> clothOverrides;

static struct {
	int lastPrint, lastFrameCount;
	int steps, resets, frames;
	int rFirst, rGap, rBack, rJump, rNan;
	double stepMs, stepMax, frontMs;
	int dbgSteps;
	double lastFrameMs, maxFrameMs;
	int slowFrames;
	double dbgKinMax, dbgRowDev, dbgDisp, dbgDz, dbgPelvis[3];
	int contacts, hand, cone;
} clothStats;

static void Cloth_FreeInst( clothInst_t *ci ) {
	if ( ci == clothLastHit ) clothLastHit = NULL;
	if ( ci == clothDumpInst ) {
		// the dumped instance is gone: finish its file (a new instance would need a new r_clothDump name)
		if ( clothDumpFile ) ri.FS_FCloseFile( clothDumpFile );
		clothDumpFile = 0;
		clothDumpInst = NULL;
	}
	R_ClothRBFreeInst( ci );
	if ( ci->sim ) SkirtSim_Destroy( ci->sim );
	delete ci;
}

void R_ClothForgetBoneCache( const CBoneCache *bc ) {
	for ( size_t i = 0; i < clothInsts.size(); i++ ) {
		if ( clothInsts[i]->bc == bc ) {
			Cloth_FreeInst( clothInsts[i] );
			clothInsts.erase( clothInsts.begin() + i );
			return;
		}
	}
}

void R_ClothShutdown( void ) {
	if ( clothDumpFile ) {
		ri.FS_FCloseFile( clothDumpFile );
		clothDumpFile = 0;
	}
	clothDumpInst = NULL;
	for ( size_t i = 0; i < clothInsts.size(); i++ ) Cloth_FreeInst( clothInsts[i] );
	clothInsts.clear();
	clothLastHit = NULL;
	for ( size_t i = 0; i < clothAssets.size(); i++ ) Cloth_FreeAsset( clothAssets[i] );
	clothAssets.clear();
	clothAssetByName.clear();
	clothAssetByModel.clear();
	R_ClothRBShutdown();
}

clothInst_t *R_ClothFindInst( const CBoneCache *bc ) {
	if ( clothLastHit && clothLastHit->bc == bc ) return clothLastHit;
	for ( size_t i = 0; i < clothInsts.size(); i++ ) {
		if ( clothInsts[i]->bc == bc ) {
			clothLastHit = clothInsts[i];
			return clothInsts[i];
		}
	}
	return NULL;
}

// Game tuning on top of the solver table (which stays the JS reference defaults), applied while r_clothProfile
// is 1 (default). Fitted on the eve2b_np skirt:
// damping relative to the skinned motion (no fake drag at run speed), JKA's unphysical accelerations cut to what
// the cloth may feel (linear excess carried with the body, angular rate-limited so a spin still reaches full
// speed in ~0.25 s), and a flare cap so the hem may billow out level but never rise past the waistband.
struct clothProfileEntry_t {
	const char *key;
	double value;
};
// Profile 1 (default): the first game tuning with the felt linear velocity along the
// reference bone's local Z cut to 0.2. On JKA-rigged skirts (eve2b_np) the pelvis's local Z is the body's forward axis,
// so the table's 0.9 (fitted on a Z-up UE pelvis) let the run's forward velocity in at 0.9; with the forward lean and turn
// of a run start that flipped the back panel up (in-game dump: back-hem lift p95 17.8 -> 4.2 cm; pirouette flare kept).
static const clothProfileEntry_t clothGameProfile1[] = {
	{ "dampRelative", 1 },
	{ "maxFeltAcc", 800 },        // cm/s^2
	{ "maxFeltAngAcc", 4500 },    // deg/s^2
	{ "flareCone", 82 },          // deg from the waist anchor about the pelvis down axis
	{ "linVelScaleZ", 0.2 },      // felt fraction of the frame velocity along the reference bone's local Z
};
// Profile 2: the first game tuning, kept selectable.
static const clothProfileEntry_t clothGameProfile2[] = {
	{ "dampRelative", 1 },
	{ "maxFeltAcc", 800 },
	{ "maxFeltAngAcc", 4500 },
	{ "flareCone", 82 },
};
// every key any profile sets (a profile switch returns these to their new base value)
static const char *const clothProfileKeys[] = {
	"dampRelative", "maxFeltAcc", "maxFeltAngAcc", "flareCone", "linVelScaleZ",
};
static const int CLOTH_PROFILE_KEYS = (int)( sizeof( clothProfileKeys ) / sizeof( clothProfileKeys[0] ) );

static const clothProfileEntry_t *Cloth_Profile( int *count ) {
	switch ( r_clothProfile->integer ) {
	case 1: *count = (int)( sizeof( clothGameProfile1 ) / sizeof( clothGameProfile1[0] ) ); return clothGameProfile1;
	case 2: *count = (int)( sizeof( clothGameProfile2 ) / sizeof( clothGameProfile2[0] ) ); return clothGameProfile2;
	default: *count = 0; return NULL;
	}
}

// before the model's first draw, so its first frame does not parse and bind the asset
void R_ClothWarmModel( const model_t *mod ) {
	clothModelAsset_t *as = mod ? Cloth_AssetForModel( mod ) : NULL;
	if ( as ) Cloth_BindAsset( as, mod );
}

// A solver parameter the asset sets for its garment (SKA2 parameter block, "key value" lines), e.g. a cape that turns
// the skirt's flare cone off.
static bool Cloth_AssetParam( const clothModelAsset_t *as, const char *key, double *value ) {
	if ( !as || !as->a.params || as->a.paramsLen <= 0 ) return false;
	const char *s = as->a.params, *end = s + as->a.paramsLen;
	bool found = false;
	while ( s < end ) {
		const char *eol = s;
		while ( eol < end && *eol != '\n' ) eol++;
		char line[128], k[64];
		const size_t n = (size_t)( eol - s ) < sizeof( line ) - 1 ? (size_t)( eol - s ) : sizeof( line ) - 1;
		memcpy( line, s, n );
		line[n] = 0;
		double v;
		if ( sscanf( line, "%63s %lf", k, &v ) == 2 && !Q_stricmp( k, key ) ) {
			*value = v;
			found = true;   // the last line for a key wins
		}
		s = eol + 1;
	}
	return found;
}

// apply the asset's own parameters (after the game profile, before r_clothParam overrides)
static void Cloth_ApplyAssetParams( skirt_sim_t *sim, const clothModelAsset_t *as ) {
	if ( !as || !as->a.params || as->a.paramsLen <= 0 ) return;
	const char *s = as->a.params, *end = s + as->a.paramsLen;
	while ( s < end ) {
		const char *eol = s;
		while ( eol < end && *eol != '\n' ) eol++;
		char line[128], k[64];
		const size_t n = (size_t)( eol - s ) < sizeof( line ) - 1 ? (size_t)( eol - s ) : sizeof( line ) - 1;
		memcpy( line, s, n );
		line[n] = 0;
		double v;
		if ( sscanf( line, "%63s %lf", k, &v ) == 2 ) SkirtSim_SetParam( sim, k, v );
		s = eol + 1;
	}
}

// The value a knob returns to on "reset" / profile changes: the asset's own value, else the active game profile's, else
// the solver table default.
static double Cloth_BaseValue( const char *key, const clothModelAsset_t *as ) {
	double av;
	if ( Cloth_AssetParam( as, key, &av ) ) return av;
	if ( !Q_stricmp( key, "maxFrameVel" ) && r_clothFeltSpeed && r_clothFeltSpeed->integer && as )
		return CLOTH_MAX_SPEED * as->a.unitCm;   // as Cloth_ApplyOverrides sets it
	int n;
	const clothProfileEntry_t *prof = Cloth_Profile( &n );
	for ( int i = 0; i < n; i++ ) {
		if ( !Q_stricmp( prof[i].key, key ) ) return prof[i].value;
	}
	const skirt_param_info_t *info = SkirtParams_Find( key );
	return info ? info->def : 0.0;
}

static void Cloth_ApplyOverrides( skirt_sim_t *sim, const clothModelAsset_t *as ) {
	int n;
	const clothProfileEntry_t *prof = Cloth_Profile( &n );
	for ( int i = 0; i < n; i++ ) SkirtSim_SetParam( sim, prof[i].key, prof[i].value );
	// the solver's own teleport clamp (maxFrameVel, 2000 cm/s = 750 u/s) sits below motion the glue treats as
	// normal (falls, the bike at ~940 u/s, CPM hops with a fall): past it the garment is carried rigidly. 1: the clamp is
	// the glue's own limit, CLOTH_MAX_SPEED in the asset's units, so the glue's teleport reset is the only hard cut
	if ( r_clothFeltSpeed && r_clothFeltSpeed->integer && as ) SkirtSim_SetParam( sim, "maxFrameVel", CLOTH_MAX_SPEED * as->a.unitCm );
	Cloth_ApplyAssetParams( sim, as );
	if ( r_clothBend->integer >= 0 ) SkirtSim_SetParam( sim, "bendModel", r_clothBend->integer );
	for ( size_t i = 0; i < clothOverrides.size(); i++ ) SkirtSim_SetParam( sim, clothOverrides[i].key.c_str(), clothOverrides[i].value );
}

static bool Cloth_BindBones( clothInst_t *ci, const mdxaHeader_t *aHeader ) {
	const cloth_asset_t &a = ci->asset->a;
	ci->aHeader = aHeader;
	ci->boneMap.assign( a.nBones, -1 );
	const mdxaSkelOffsets_t *offs = (const mdxaSkelOffsets_t *)( (const byte *)aHeader + sizeof( mdxaHeader_t ) );
	for ( int b = 0; b < a.nBones; b++ ) {
		char name[CLOTH_NAME_LEN + 1];
		ClothAsset_BoneName( &a, b, name, sizeof( name ) );
		for ( int g = 0; g < aHeader->numBones; g++ ) {
			const mdxaSkel_t *skel = (const mdxaSkel_t *)( (const byte *)aHeader + sizeof( mdxaHeader_t ) + offs->offsets[g] );
			if ( !Q_stricmp( skel->name, name ) ) {
				ci->boneMap[b] = g;
				break;
			}
		}
		if ( ci->boneMap[b] < 0 ) {
			if ( !ci->warned ) ri.Printf( PRINT_WARNING, "cloth: %s: skeleton has no bone %s\n", ci->asset->name, name );
			ci->warned = 1;
			return false;
		}
	}
	return true;
}

// The pinned set drawn skinned (Cloth_BuildDraw, the back end) follows the solver's: parameter changes that rebuild
// it (eps_kin, massScale, tetherK, geodesic) bump buildSerial.
static void Cloth_SyncKin( clothInst_t *ci ) {
	skirt_topology_t T;
	SkirtSim_Topology( ci->sim, &T );
	if ( (int)ci->kin.size() == T.N && ci->kinSerial == T.buildSerial ) return;
	ci->kin.resize( T.N );
	for ( int i = 0; i < T.N; i++ ) ci->kin[i] = T.kin[i];
	ci->kinSerial = T.buildSerial;
}

static clothInst_t *Cloth_CreateInst( const CBoneCache *bc, clothModelAsset_t *as, float dist ) {
	// evict the longest-unseen instance if at the limit and it has been gone a while; else the furthest
	// one, when the newcomer is clearly nearer (a quarter, so two at similar distances do not trade places)
	const int maxInst = r_clothMaxInstances->integer > 0 ? r_clothMaxInstances->integer : 1;
	if ( (int)clothInsts.size() >= maxInst ) {
		int oldest = -1;
		for ( size_t i = 0; i < clothInsts.size(); i++ ) {
			if ( oldest < 0 || clothInsts[i]->lastSeenReal < clothInsts[oldest]->lastSeenReal ) oldest = (int)i;
		}
		if ( oldest >= 0 && ri.Milliseconds() - clothInsts[oldest]->lastSeenReal < 1000 ) {
			int furthest = -1;
			for ( size_t i = 0; i < clothInsts.size(); i++ ) {
				if ( furthest < 0 || clothInsts[i]->lastDist > clothInsts[furthest]->lastDist ) furthest = (int)i;
			}
			if ( furthest < 0 || !( dist * 1.25f < clothInsts[furthest]->lastDist ) ) return NULL;
			oldest = furthest;
		}
		if ( oldest < 0 ) return NULL;
		Cloth_FreeInst( clothInsts[oldest] );
		clothInsts.erase( clothInsts.begin() + oldest );
	}
	skirt_sim_t *sim = SkirtSim_Create( &as->sa, NULL );
	if ( !sim ) {
		ri.Printf( PRINT_WARNING, "cloth: %s: solver allocation failed\n", as->name );
		return NULL;
	}
	clothInst_t *ci = new clothInst_t();
	ci->bc = bc;
	ci->asset = as;
	ci->sim = sim;
	ci->aHeader = NULL;
	ci->bindFailed = NULL;
	const int N = as->a.nVerts, NB = as->a.nBones;
	ci->kinSerial = 0;
	ci->L.assign( NB * 16, 0.0f );
	ci->M.assign( NB * 16, 0.0f );
	ci->Mprev.assign( NB * 16, 0.0f );
	ci->Mstep.assign( NB * 16, 0.0f );
	ci->out.assign( N * 3, 0.0f );
	ci->relA.assign( N * 3, 0.0f );
	ci->relB.assign( N * 3, 0.0f );
	ci->relZ.assign( N * 3, 0.0f );
	ci->drawPos.assign( N * 3, 0.0f );
	ci->drawNrm.assign( N * 3, 0.0f );
	ci->live = false;
	ci->ready = false;
	Sim_GateClear( &ci->gate );
	ci->baseLag = 0;
	ci->prevFrame = 0;
	ci->checkPoseMs = 0;
	ci->checkInsideStep = 0;
	ci->lastTime = 0;
	ci->drawTime = -1;
	ci->drawSerial = 0;
	ci->rb = NULL;
	ci->gpu = false;
	ci->relOldest = ci->relOld = ci->relNew = 0;
	ci->acc = 0;
	ci->lastSeenReal = ri.Milliseconds();
	ci->warned = 0;
	Cloth_ApplyOverrides( sim, ci->asset );
	Cloth_SyncKin( ci );
	clothInsts.push_back( ci );
	return ci;
}

/* ------------------------------------------------------------------ maths (row-vector 4x4, cm) */

// Gram-Schmidt on rows 0..2 of a row-vector matrix (sign of row 2 kept)
static void Cloth_Orthonormalise( float *m ) {
	vec3_t x, y, z;
	VectorCopy( m, x );
	VectorNormalize( x );
	float d = DotProduct( x, m + 4 );
	VectorMA( m + 4, -d, x, y );
	VectorNormalize( y );
	CrossProduct( x, y, z );
	if ( DotProduct( z, m + 8 ) < 0 ) VectorScale( z, -1.0f, z );
	VectorCopy( x, m );
	VectorCopy( y, m + 4 );
	VectorCopy( z, m + 8 );
}

// world <-> frame of a rigid row-vector matrix F: rel_r = sum_c (w_c - T_c) F[r][c]
static void Cloth_ToFrame( const float *F, const float *w, float *rel, int n ) {
	for ( int i = 0; i < n; i++, w += 3, rel += 3 ) {
		const float x = w[0] - F[12], y = w[1] - F[13], z = w[2] - F[14];
		rel[0] = x * F[0] + y * F[1] + z * F[2];
		rel[1] = x * F[4] + y * F[5] + z * F[6];
		rel[2] = x * F[8] + y * F[9] + z * F[10];
	}
}

/* ------------------------------------------------------------------ per-frame */

// 1. pose: L (model space) and M (world), asset bone order
static void Cloth_GatherPose( clothInst_t *ci, CBoneCache *bc, const trRefEntity_t *ent ) {
	const float u = (float)ci->asset->a.unitCm;
	vec3_t ax[3];
	for ( int k = 0; k < 3; k++ ) {
		VectorCopy( ent->e.axis[k], ax[k] );
		if ( VectorNormalize( ax[k] ) == 0.0f ) VectorClear( ax[k] );
	}
	const int NB = ci->asset->a.nBones;
	for ( int b = 0; b < NB; b++ ) {
		const mdxaBone_t *A = R_ClothEvalBone( bc, ci->boneMap[b] );
		float *L = &ci->L[b * 16], *M = &ci->M[b * 16];
		for ( int r = 0; r < 3; r++ ) {
			for ( int c = 0; c < 3; c++ ) L[r * 4 + c] = A->matrix[c][r];
			L[r * 4 + 3] = 0.0f;
		}
		for ( int c = 0; c < 3; c++ ) L[12 + c] = A->matrix[c][3] * u;
		L[15] = 1.0f;
		for ( int r = 0; r < 4; r++ ) {
			for ( int c = 0; c < 3; c++ ) {
				M[r * 4 + c] = L[r * 4] * ax[0][c] + L[r * 4 + 1] * ax[1][c] + L[r * 4 + 2] * ax[2][c];
			}
			M[r * 4 + 3] = L[r * 4 + 3];
		}
		for ( int c = 0; c < 3; c++ ) M[12 + c] += ent->e.origin[c] * u;
	}
}

static void Cloth_StoreRel( clothInst_t *ci, const float *mats ) {
	float F[16];
	memcpy( F, mats + ci->asset->a.refBone * 16, sizeof( F ) );
	Cloth_Orthonormalise( F );
	ci->relZ.swap( ci->relA );  // relZ = the one before (r_clothDrawLead 2)
	ci->relA.swap( ci->relB );  // relA = older, relB = newest
	SkirtSim_Read( ci->sim, &ci->out[0] );
	Cloth_ToFrame( F, &ci->out[0], &ci->relB[0], ci->asset->a.nVerts );
}

// r_clothDump: one record per step or reset, for offline replay (tools/cloth/skirt_replay --mats). File "CLD1",
// int nBones, int nVerts; record: int scene time ms, int flags (1 = reset; from bit 8 up the pose time of a stretched
// step in microseconds, 0 = dt: Sim_DumpFlags), float dt, float stats[8] (contacts,
// cone projections, cone deg, felt yaw deg/s, felt acc, felt acc carried (cm/s^2), |omega| raw, used (deg/s)),
// float mats[nBones*16] (the step's skinning matrices, world cm), float pos[nVerts*3] (solver output, world cm).
// One file records one instance: the first to step after r_clothDump is set; the file ends when that instance is
// freed, and a new name starts a new dump.
static void Cloth_DumpStep( clothInst_t *ci, int now, float dt, const float *mats, int flags ) {
	if ( !r_clothDump->string[0] ) return;
	if ( Q_stricmp( clothDumpName, r_clothDump->string ) ) {
		if ( clothDumpFile ) ri.FS_FCloseFile( clothDumpFile );
		clothDumpFile = 0;
		clothDumpInst = NULL;
		clothDumpName[0] = 0;
	}
	const int NB = ci->asset->a.nBones, N = ci->asset->a.nVerts;
	if ( !clothDumpFile ) {
		if ( clothDumpName[0] ) return;   // this name's instance is gone
		Q_strncpyz( clothDumpName, r_clothDump->string, sizeof( clothDumpName ) );
		clothDumpFile = ri.FS_FOpenFileWrite( clothDumpName, qtrue );
		if ( !clothDumpFile ) return;
		clothDumpInst = ci;
		const int head[3] = { 0x31444c43, NB, N };   // "CLD1"
		ri.FS_Write( head, sizeof( head ), clothDumpFile );
	}
	if ( ci != clothDumpInst ) return;
	const skirt_stats_t *st = SkirtSim_Stats( ci->sim );
	const int hi[2] = { now, flags };
	float hf[9];
	hf[0] = dt;
	hf[1] = ( flags & 1 ) ? 0.0f : (float)st->contacts;
	hf[2] = ( flags & 1 ) ? 0.0f : (float)st->coneCapped;
	hf[3] = (float)st->coneDeg; hf[4] = (float)st->yawRate; hf[5] = (float)st->feltAcc; hf[6] = (float)st->feltAccCut;
	hf[7] = (float)st->angVel; hf[8] = (float)st->angVelUsed;
	ri.FS_Write( hi, sizeof( hi ), clothDumpFile );
	ri.FS_Write( hf, sizeof( hf ), clothDumpFile );
	ri.FS_Write( mats, NB * 16 * (int)sizeof( float ), clothDumpFile );
	ri.FS_Write( &ci->out[0], N * 3 * (int)sizeof( float ), clothDumpFile );
}

// the scene's wind (tr_sim.h) for the next step: the solver's component space is world cm
static void Cloth_SetWind( clothInst_t *ci ) {
	float w[3];
	R_SimWindMs( w );
	const double cm[3] = { w[0] * 100.0, w[1] * 100.0, w[2] * 100.0 };
	SkirtSim_SetWind( ci->sim, cm );
}

// GPU solver: G maps the solver state (sim frame) into the pelvis frame of the step pose, as Cloth_StoreRel does
static void Cloth_GpuRelMatrix( clothInst_t *ci, const float *mats, float G[12] ) {
	float Fs[16];
	double F[12];
	memcpy( Fs, mats + ci->asset->a.refBone * 16, sizeof( Fs ) );
	Cloth_Orthonormalise( Fs );
	SkirtSim_GetFrame( ci->sim, F );
	for ( int r = 0; r < 3; r++ ) {
		for ( int k = 0; k < 3; k++ ) {
			G[r * 4 + k] = (float)( F[k * 3] * Fs[r * 4] + F[k * 3 + 1] * Fs[r * 4 + 1] + F[k * 3 + 2] * Fs[r * 4 + 2] );
		}
		G[r * 4 + 3] = (float)( ( F[9] - Fs[12] ) * Fs[r * 4] + ( F[10] - Fs[13] ) * Fs[r * 4 + 1] + ( F[11] - Fs[14] ) * Fs[r * 4 + 2] );
	}
}

// GPU solver: the frame part on the CPU, the particle part queued for the back end (reset when dt <= 0)
static bool Cloth_GpuStep( clothInst_t *ci, float dt, const float *mats, int slot ) {
	// the dump records the CPU solver's output; say so once per dump name instead of leaving an empty file
	static char gpuDumpNoted[MAX_QPATH];
	if ( r_clothDump->string[0] && Q_stricmp( gpuDumpNoted, r_clothDump->string ) ) {
		Q_strncpyz( gpuDumpNoted, r_clothDump->string, sizeof( gpuDumpNoted ) );
		ri.Printf( PRINT_WARNING, "cloth: r_clothDump records the CPU solver only; set r_clothGpu 0 to dump (garments made after it use the CPU solver)\n" );
	}
	skirt_frame_t fr;
	float capOld[CLOTH_GPU_MAX_CAPS * 6], capNew[CLOTH_GPU_MAX_CAPS * 6];
	float G[12];
	if ( dt <= 0.0f ) SkirtSim_Invalidate( ci->sim );
	Cloth_SetWind( ci );
	if ( !SkirtSim_StepFrame( ci->sim, (skirt_real)dt, mats, &fr, capOld, capNew ) ) return true;
	Cloth_GpuRelMatrix( ci, mats, G );
	return R_ClothRBGpuStep( ci, &fr, capOld, capNew, mats, G, slot );
}

static void Cloth_Reset( clothInst_t *ci, int now ) {
	if ( ci->gpu && !Cloth_GpuStep( ci, 0.0f, &ci->M[0], 0 ) ) ci->gpu = false;
	// r_clothSettle (steps): settle on the reset pose before the first draw instead of showing the skinned
	// shape relax; the GPU queue takes eight records a frame, so it settles six steps at most there
	const float settleHz = r_clothHz->value >= 10.0f ? ( r_clothHz->value <= 500.0f ? r_clothHz->value : 500.0f ) : 10.0f;
	const int settle = r_clothSettle->integer > 0 ? ( r_clothSettle->integer < 60 ? r_clothSettle->integer : 60 ) : 0;
	if ( ci->gpu ) {
		ci->relOldest = ci->relOld = ci->relNew = 0;
		// live before the steps: a new instance is not live yet, and the test below is for the back end clearing it
		// (it started over) while the steps queue; without this a new instance queued one step and stopped)
		ci->live = true;
		for ( int k = 0; k < settle && k < 6 && ci->gpu; k++ ) {
			const int slot = ci->relOld != ci->relNew ? 3 - ci->relOld - ci->relNew : ( ci->relNew + 1 ) % 3;
			if ( !Cloth_GpuStep( ci, 1.0f / settleHz, &ci->M[0], slot ) || !ci->live ) break;
			ci->relOldest = ci->relOld;
			ci->relOld = ci->relNew;
			ci->relNew = slot;
		}
	} else {
		SkirtSim_Reset( ci->sim, &ci->M[0] );
		for ( int k = 0; k < settle; k++ ) SkirtSim_Step( ci->sim, (skirt_real)( 1.0 / settleHz ), &ci->M[0] );
		Cloth_StoreRel( ci, &ci->M[0] );
		Cloth_DumpStep( ci, now, 0.0f, &ci->M[0], 1 );
		ci->relA = ci->relB;
		ci->relZ = ci->relB;
	}
	ci->acc = 0;
	ci->baseLag = 0;
	ci->prevFrame = 0;
	ci->live = true;
	ci->lastTime = now;
	ci->Mprev = ci->M;
	ci->checkPose.clear();
	ci->checkPoseMs = now;
	clothStats.resets++;
}

// r_simCheck: every CLOTH_CHECK_STRIDE-th collider vertex skinned by a pose, in that pose's pelvis frame (cm)
static const int CLOTH_CHECK_STRIDE = 8;
static const int CLOTH_CHECK_INSIDE_EVERY = 7;
static void Cloth_CheckCollider( const clothInst_t *ci, const float *mats, std::vector<float> &out ) {
	const cloth_asset_t &a = ci->asset->a;
	const int NI = a.nInfl;
	float F[16];
	memcpy( F, mats + a.refBone * 16, sizeof( F ) );
	Cloth_Orthonormalise( F );
	out.clear();
	for ( int v = 0; v < a.nColVerts; v += CLOTH_CHECK_STRIDE ) {
		const float *p = a.colPos + v * 3;
		float w[3] = { 0, 0, 0 }, rel[3];
		for ( int k = 0; k < NI; k++ ) {
			const int bn = a.colSkinBone[v * NI + k];
			const float wt = a.colSkinW[v * NI + k];
			if ( bn < 0 || wt == 0.0f ) continue;
			const float *m = mats + bn * 16;
			w[0] += wt * ( p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12] );
			w[1] += wt * ( p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13] );
			w[2] += wt * ( p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14] );
		}
		Cloth_ToFrame( F, w, rel, 1 );
		out.push_back( rel[0] ); out.push_back( rel[1] ); out.push_back( rel[2] );
	}
}

// r_simCheck: one step's counters. The pose jump is the largest move of a collider vertex in the pelvis frame between
// this step's pose and the last one's (cm). The pose time is kept whether or not the check is on, so a window that
// opens later starts from the right step.
static void Cloth_CheckStep( clothInst_t *ci, const simStepPlan_t *plan, int k, int now, const float *mats ) {
	const double poseTime = now - ( 1.0 - Sim_StepAlpha( plan, k ) ) * plan->span * 1000.0;
	const double poseMs = poseTime - ci->checkPoseMs;
	ci->checkPoseMs = poseTime;
	if ( !R_SimCheckOn() ) {
		ci->checkPose.clear();
		return;
	}
	std::vector<float> col;
	Cloth_CheckCollider( ci, mats, col );
	double jump = 0;
	if ( ci->checkPose.size() == col.size() ) {
		for ( size_t i = 0; i + 2 < col.size(); i += 3 ) {
			const double dx = col[i] - ci->checkPose[i], dy = col[i + 1] - ci->checkPose[i + 1], dz = col[i + 2] - ci->checkPose[i + 2];
			const double d = sqrt( dx * dx + dy * dy + dz * dz );
			if ( d > jump ) jump = d;
		}
	}
	R_SimCheckStep( SIMCHECK_CLOTH, poseMs, ( plan->stretched ? plan->poseDt : plan->step ) * 1000.0, jump );
	ci->checkPose.swap( col );
}

// r_clothDebug: compare the solver state with a plain skin of the step pose (world cm)
static void Cloth_DebugStep( clothInst_t *ci, const float *mats ) {
	const cloth_asset_t &a = ci->asset->a;
	const int N = a.nVerts, NI = a.nInfl;
	const float *o = &ci->out[0];
	double kinMax = 0, disp = 0, dz = 0, rowDev = 0;
	int nd = 0;
	for ( int b = 0; b < a.nBones; b++ ) {
		for ( int r = 0; r < 3; r++ ) {
			const float *m = mats + b * 16 + r * 4;
			const double l = sqrt( (double)m[0] * m[0] + (double)m[1] * m[1] + (double)m[2] * m[2] );
			if ( fabs( l - 1.0 ) > rowDev ) rowDev = fabs( l - 1.0 );
		}
	}
	for ( int i = 0; i < N; i++ ) {
		const float *p = a.pos + i * 3;
		double x = 0, y = 0, z = 0;
		for ( int k = 0; k < NI; k++ ) {
			const int b = a.skinBone[i * NI + k];
			const float w = a.skinW[i * NI + k];
			if ( b < 0 || w == 0.0f ) continue;
			const float *m = mats + b * 16;
			x += w * ( p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12] );
			y += w * ( p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13] );
			z += w * ( p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14] );
		}
		const double ex = o[i * 3] - x, ey = o[i * 3 + 1] - y, ez = o[i * 3 + 2] - z;
		const double e = sqrt( ex * ex + ey * ey + ez * ez );
		if ( ci->kin[i] ) {
			if ( e > kinMax ) kinMax = e;
		} else {
			disp += e;
			dz += ez;
			nd++;
		}
	}
	const float *T = mats + a.refBone * 16 + 12;
	clothStats.dbgSteps++;
	if ( kinMax > clothStats.dbgKinMax ) clothStats.dbgKinMax = kinMax;
	if ( rowDev > clothStats.dbgRowDev ) clothStats.dbgRowDev = rowDev;
	clothStats.dbgDisp += nd ? disp / nd : 0;
	clothStats.dbgDz += nd ? dz / nd : 0;
	clothStats.dbgPelvis[0] = T[0]; clothStats.dbgPelvis[1] = T[1]; clothStats.dbgPelvis[2] = T[2];
}

// a step's pose between the stored previous pose and this frame's. With r_simSteps 0 the world
// matrices themselves were lerped, which folds the limbs toward the axis when a spinning body's frame is long
static void Cloth_LerpPose( clothInst_t *ci, float *out, float alpha, int mode ) {
	const int NB = ci->asset->a.nBones;
	if ( mode == SIM_STEPS_LEGACY ) {
		for ( int k = 0; k < NB * 16; k++ ) out[k] = ci->Mprev[k] + alpha * ( ci->M[k] - ci->Mprev[k] );
		for ( int b = 0; b < NB; b++ ) Cloth_Orthonormalise( out + b * 16 );
	} else {
		SkirtPose_Lerp( out, &ci->Mprev[0], &ci->M[0], alpha, NB, ci->asset->a.refBone );
	}
}

// 2. advance: fixed steps from the accumulator
static void Cloth_Advance( clothInst_t *ci, int now ) {
	const int NB = ci->asset->a.nBones, ref = ci->asset->a.refBone;
	const float u = (float)ci->asset->a.unitCm;
	bool reset = !ci->live || now < ci->lastTime || now - ci->lastTime > CLOTH_RESET_GAP_MS;
	if ( !ci->live ) clothStats.rFirst++;
	else if ( now < ci->lastTime ) clothStats.rBack++;
	else if ( reset ) clothStats.rGap++;
	{
		// solver switch (r_clothGpu, GPU limits): start over on the other one
		const bool gpu = R_ClothRBUseGpu( ci );
		if ( gpu != ci->gpu ) {
			ci->gpu = gpu;
			reset = true;
		}
	}
	if ( !reset ) {
		// Mprev is the pose of the last frame this instance advanced (lastTime), so dt covers any frames in between
		const float *a = &ci->Mprev[ref * 16 + 12], *b = &ci->M[ref * 16 + 12];
		const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
		const float dist = sqrtf( dx * dx + dy * dy + dz * dz ) / u;
		const float dt = ( now - ci->lastTime ) * 0.001f + (float)ci->baseLag;   // Mprev may be the solver's older pose
		const float limit = CLOTH_TELEPORT_UNITS + CLOTH_MAX_SPEED * dt;
		if ( dist > limit ) {
			reset = true;
			clothStats.rJump++;
			ri.Printf( r_clothDebug->integer ? PRINT_ALL : PRINT_DEVELOPER, "cloth: %s: teleport reset, pelvis moved %.0f units in %d ms (limit %.0f) to %.0f %.0f %.0f\n",
				ci->asset->name, dist, now - ci->lastTime, limit, b[0] / u, b[1] / u, b[2] / u );
		}
	}
	if ( reset ) {
		Cloth_Reset( ci, now );
		return;
	}
	if ( now == ci->lastTime ) return;

	const float hz = r_clothHz->value >= 10.0f ? ( r_clothHz->value <= 500.0f ? r_clothHz->value : 500.0f ) : 10.0f;
	const double step = 1.0 / hz;
	const double frame = ( now - ci->lastTime ) * 0.001;
	int maxSteps = r_clothMaxSteps->integer > 0 ? r_clothMaxSteps->integer : 1;
	if ( ci->gpu && maxSteps > CLOTH_GPU_MAX_QUEUED ) maxSteps = CLOTH_GPU_MAX_QUEUED;   // more would overflow its queue
	const int mode = R_SimStepMode();
	simStepPlan_t plan;
	Sim_PlanSteps( &plan, ci->acc, ci->baseLag, frame, step, maxSteps, mode, Sim_Carry( R_SimCarryFrames(), maxSteps, step, frame, ci->prevFrame ) );
	R_SimCheckPlan( SIMCHECK_CLOTH, &plan );
	ci->prevFrame = frame;
	for ( int n = 0; n < plan.n; n++ ) {
		// the step's pose: lerped between the stored previous pose and this frame's
		const double alpha = Sim_StepAlpha( &plan, n );
		const float *mats = &ci->M[0];
		if ( alpha < 0.999 ) {
			Cloth_LerpPose( ci, &ci->Mstep[0], (float)alpha, mode );
			mats = &ci->Mstep[0];
		}
		Cloth_CheckStep( ci, &plan, n, now, mats );
		SkirtSim_SetPoseDt( ci->sim, Sim_StepPoseDt( &plan ) );   // a stretched step: the pose time it covers
		if ( ci->gpu ) {
			// three slots rotate, so the draw can blend the last three states (lead 2)
			const int slot = ci->relOld != ci->relNew ? 3 - ci->relOld - ci->relNew : ( ci->relNew + 1 ) % 3;
			if ( !Cloth_GpuStep( ci, (float)step, mats, slot ) ) {
				ci->gpu = false;   // the back end refused: the CPU solver takes over from rest
				Cloth_Reset( ci, now );
				return;
			}
			if ( !ci->live ) break;   // the back end started over (a rebuild, or its queue was full): reset next frame
			ci->relOldest = ci->relOld;
			ci->relOld = ci->relNew;
			ci->relNew = slot;
			clothStats.steps++;
			continue;
		}
		const double t0 = ClothNowMs();
		Cloth_SetWind( ci );
		SkirtSim_Step( ci->sim, (skirt_real)step, mats );
		const double ms = ClothNowMs() - t0;
		clothStats.steps++;
		clothStats.stepMs += ms;
		if ( ms > clothStats.stepMax ) clothStats.stepMax = ms;
		const skirt_stats_t *st = SkirtSim_Stats( ci->sim );
		clothStats.contacts += st->contacts;
		clothStats.hand += st->handContacts;
		clothStats.cone += st->coneCapped;
		if ( st->nan ) {
			ri.Printf( PRINT_DEVELOPER, "cloth: %s: non-finite state, reset\n", ci->asset->name );
			clothStats.rNan++;
			Cloth_Reset( ci, now );
			return;
		}
		Cloth_StoreRel( ci, mats );
		Cloth_DumpStep( ci, now, (float)step, mats, Sim_DumpFlags( 0, &plan ) );
		if ( r_clothDebug->integer ) Cloth_DebugStep( ci, mats );
		if ( R_SimCheckOn() && ++ci->checkInsideStep % CLOTH_CHECK_INSIDE_EVERY == 0 ) {
			// particles inside the body by over 0.5 cm (CPU solver only: its particles are here). A brute-force count,
			// so every 4th particle on every 7th step (7 so the samples fall on every step of a frame alike)
			double deepest = 0;
			const int inside = SkirtSim_CountInside( ci->sim, 0.5, 4, &deepest ) * 4;
			R_SimCheckInside( SIMCHECK_CLOTH, inside, deepest );
		}
	}
	ci->acc = plan.accAfter;
	ci->lastTime = now;
	if ( plan.baseLag > 0 ) {
		// behind and catching up: the previous pose is the one the solver's last step ran at
		Cloth_LerpPose( ci, &ci->Mstep[0], (float)plan.baseAlpha, mode );
		ci->Mprev = ci->Mstep;
	} else {
		ci->Mprev = ci->M;
	}
	ci->baseLag = plan.baseLag;
}

// 3. draw buffer in model space (glm units); the GPU solver's back end builds it on the GPU
static void Cloth_BuildDraw( clothInst_t *ci, CBoneCache *bc ) {
	const cloth_asset_t &a = ci->asset->a;
	const int N = a.nVerts, NI = a.nInfl;
	const float inv = (float)( 1.0 / a.unitCm );
	const float hz = r_clothHz->value >= 10.0f ? ( r_clothHz->value <= 500.0f ? r_clothHz->value : 500.0f ) : 10.0f;
	float t = (float)( ci->acc * hz );
	if ( t < 0 ) t = 0;
	if ( t > 1 ) t = 1;
	// r_clothDrawLead 1: the newest step carried forward by the time since it (t in 1..2 extrapolates the lerp below);
	// the blend of the last two steps is a whole step behind the body it is placed on, and in a jump tuck the knees,
	// which move with the body's pose, come out through a skirt the solver had already pushed clear
	const float s2 = t;   // r_clothDrawLead 2: the fraction of a step since the newest state
	const bool lead2 = r_clothDrawLead->integer == 2;
	if ( r_clothDrawLead->integer ) t += 1.0f;
	float F[16];
	memcpy( F, &ci->L[a.refBone * 16], sizeof( F ) );
	Cloth_Orthonormalise( F );
	if ( ci->gpu ) {
		R_ClothRBGpuDraw( ci, bc, t, ci->relOldest, ci->relOld, ci->relNew, s2, lead2, F );
		return;
	}
	const float *ra = &ci->relA[0], *rb = &ci->relB[0];
	float *dp = &ci->drawPos[0];
	for ( int i = 0; i < N; i++ ) {
		float *o = dp + i * 3;
		if ( ci->kin[i] ) {
			// pinned: skin with the current pose, exactly like the body around it
			const float *p = a.pos + i * 3;
			float x = 0, y = 0, z = 0;
			for ( int k = 0; k < NI; k++ ) {
				const int b = a.skinBone[i * NI + k];
				const float w = a.skinW[i * NI + k];
				if ( b < 0 || w == 0.0f ) continue;
				const float *m = &ci->L[b * 16];
				x += w * ( p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12] );
				y += w * ( p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13] );
				z += w * ( p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14] );
			}
			o[0] = x * inv; o[1] = y * inv; o[2] = z * inv;
		} else {
			const float *pa = ra + i * 3, *pb = rb + i * 3;
			float x, y, z;
			if ( lead2 ) {
				// the old pair's extrapolation blended into the new pair's over one step,
				// (1-s) (B + (1+s)(B-A)) + s (C + s(C-B)) with A B C = relZ relA relB: continuous at every step, still leading
				const float *pz = &ci->relZ[i * 3];
				float q[3];
				for ( int k = 0; k < 3; k++ )
					q[k] = ( 1.0f - s2 ) * ( pa[k] + ( 1.0f + s2 ) * ( pa[k] - pz[k] ) ) + s2 * ( pb[k] + s2 * ( pb[k] - pa[k] ) );
				x = q[0]; y = q[1]; z = q[2];
			} else {
				x = pa[0] + t * ( pb[0] - pa[0] ); y = pa[1] + t * ( pb[1] - pa[1] ); z = pa[2] + t * ( pb[2] - pa[2] );
			}
			o[0] = ( x * F[0] + y * F[4] + z * F[8] + F[12] ) * inv;
			o[1] = ( x * F[1] + y * F[5] + z * F[9] + F[13] ) * inv;
			o[2] = ( x * F[2] + y * F[6] + z * F[10] + F[14] ) * inv;
		}
	}
	float *nr = &ci->drawNrm[0];
	memset( nr, 0, sizeof( float ) * N * 3 );
	for ( int k = 0; k < a.nTris; k++ ) {
		const uint32_t i0 = a.tri[k * 3], i1 = a.tri[k * 3 + 1], i2 = a.tri[k * 3 + 2];
		vec3_t e1, e2, c;
		VectorSubtract( dp + i1 * 3, dp + i0 * 3, e1 );
		VectorSubtract( dp + i2 * 3, dp + i0 * 3, e2 );
		CrossProduct( e1, e2, c );
		VectorAdd( nr + i0 * 3, c, nr + i0 * 3 );
		VectorAdd( nr + i1 * 3, c, nr + i1 * 3 );
		VectorAdd( nr + i2 * 3, c, nr + i2 * 3 );
	}
	for ( int i = 0; i < N; i++ ) {
		VectorNormalize( nr + i * 3 );   // the sign is per render vertex (rSgn)
	}
}

static void Cloth_CheckCvars( void ) {
	if ( !r_clothDump->string[0] && clothDumpName[0] ) {
		if ( clothDumpFile ) ri.FS_FCloseFile( clothDumpFile );
		clothDumpFile = 0;
		clothDumpInst = NULL;
		clothDumpName[0] = 0;
	}
	if ( r_clothProfile->modified ) {
		// game profile on/off: profile keys go to their new base value, then the explicit overrides win again
		r_clothProfile->modified = qfalse;
		for ( size_t i = 0; i < clothInsts.size(); i++ ) {
			for ( int k = 0; k < CLOTH_PROFILE_KEYS; k++ ) SkirtSim_SetParam( clothInsts[i]->sim, clothProfileKeys[k], Cloth_BaseValue( clothProfileKeys[k], clothInsts[i]->asset ) );
			for ( size_t k = 0; k < clothOverrides.size(); k++ ) SkirtSim_SetParam( clothInsts[i]->sim, clothOverrides[k].key.c_str(), clothOverrides[k].value );
		}
		ri.Printf( PRINT_ALL, "cloth: game profile %d%s\n", r_clothProfile->integer, r_clothProfile->integer ? "" : " (solver defaults)" );
	}
	if ( r_clothBend->modified ) {
		r_clothBend->modified = qfalse;
		// -1 returns each instance to its own base bendModel (the asset's, else the table's)
		for ( size_t i = 0; i < clothInsts.size(); i++ )
			SkirtSim_SetParam( clothInsts[i]->sim, "bendModel", r_clothBend->integer >= 0 ? (double)r_clothBend->integer
				: Cloth_BaseValue( "bendModel", clothInsts[i]->asset ) );
	}
	if ( r_clothParam->modified ) {
		r_clothParam->modified = qfalse;
		char key[64];
		double value;
		const char *s = r_clothParam->string;
		if ( !s[0] ) return;
		if ( !Q_stricmp( s, "reset" ) ) {
			// back to the base values (game profile, else table defaults) for every overridden key
			for ( size_t k = 0; k < clothOverrides.size(); k++ ) {
				const skirt_param_info_t *info = SkirtParams_Find( clothOverrides[k].key.c_str() );
				for ( size_t i = 0; i < clothInsts.size(); i++ ) {
					if ( info ) SkirtSim_SetParam( clothInsts[i]->sim, info->key, Cloth_BaseValue( info->key, clothInsts[i]->asset ) );
					else if ( !Q_stricmp( clothOverrides[k].key.c_str(), "linVelScale" ) ) {
						// each axis back to its base value (the asset's, else profile 1's linVelScaleZ 0.2,
						// else the table's), not the table's for all three
						static const char *const axes[3] = { "linVelScaleX", "linVelScaleY", "linVelScaleZ" };
						for ( int a = 0; a < 3; a++ )
							SkirtSim_SetParam( clothInsts[i]->sim, axes[a], Cloth_BaseValue( axes[a], clothInsts[i]->asset ) );
					}
				}
			}
			clothOverrides.clear();
			ri.Printf( PRINT_ALL, "cloth: parameter overrides cleared\n" );
			return;
		}
		if ( sscanf( s, "%63s %lf", key, &value ) != 2 ) {
			ri.Printf( PRINT_ALL, "usage: r_clothParam \"<key> <value>\" or \"reset\"\n" );
			return;
		}
		if ( !SkirtParams_Find( key ) && Q_stricmp( key, "linVelScale" ) ) {
			ri.Printf( PRINT_ALL, "cloth: unknown parameter %s\n", key );
			return;
		}
		bool replaced = false;
		for ( size_t k = 0; k < clothOverrides.size(); k++ ) {
			if ( !Q_stricmp( clothOverrides[k].key.c_str(), key ) ) {
				clothOverrides[k].value = value;
				replaced = true;
			}
		}
		if ( !replaced ) {
			clothOverride_t o;
			o.key = key;
			o.value = value;
			clothOverrides.push_back( o );
		}
		for ( size_t i = 0; i < clothInsts.size(); i++ ) SkirtSim_SetParam( clothInsts[i]->sim, key, value );
		ri.Printf( PRINT_ALL, "cloth: %s = %g\n", key, value );
	}
}

static void Cloth_Housekeeping( void ) {
	if ( tr.frameCount == clothStats.lastFrameCount ) return;
	if ( clothStats.lastFrameCount && tr.frameCount > clothStats.lastFrameCount ) clothStats.frames += tr.frameCount - clothStats.lastFrameCount;
	else clothStats.frames++;
	clothStats.lastFrameCount = tr.frameCount;
	{
		const double t = ClothNowMs();
		if ( clothStats.lastFrameMs > 0 ) {
			const double ft = t - clothStats.lastFrameMs;
			if ( ft > clothStats.maxFrameMs ) clothStats.maxFrameMs = ft;
			if ( ft > 20.0 ) clothStats.slowFrames++;
		}
		clothStats.lastFrameMs = t;
	}
	const int real = ri.Milliseconds();
	for ( size_t i = 0; i < clothInsts.size(); ) {
		if ( real - clothInsts[i]->lastSeenReal > CLOTH_IDLE_FREE_MS ) {
			Cloth_FreeInst( clothInsts[i] );
			clothInsts.erase( clothInsts.begin() + i );
		} else {
			i++;
		}
	}
	if ( r_clothStats->value > 0.0f ) {
		if ( !clothStats.lastPrint ) {
			clothStats.lastPrint = real;
			clothStats.frames = 0;
		}
		const int span = real - clothStats.lastPrint;
		if ( span >= (int)( r_clothStats->value * 1000.0f ) ) {
			int live = 0;
			for ( size_t i = 0; i < clothInsts.size(); i++ ) live += clothInsts[i]->ready ? 1 : 0;
			const double sec = span * 0.001;
			ri.Printf( PRINT_ALL, "CLOTHSTATS r_cloth %d hz %.0f inst %d live %d | %.1f fps (%d frames / %.2f s) | steps %d (%.1f/s) resets %d (first %d gap %d back %d jump %d nan %d) | "
				"step ms mean %.3f max %.3f | cloth ms/frame %.3f | contacts/step %.1f hand %.2f cone %.1f | frame ms max %.1f, frames over 20 ms %d\n",
				r_cloth->integer, r_clothHz->value, (int)clothInsts.size(), live, clothStats.frames / sec, clothStats.frames, sec,
				clothStats.steps, clothStats.steps / sec, clothStats.resets,
				clothStats.rFirst, clothStats.rGap, clothStats.rBack, clothStats.rJump, clothStats.rNan,
				clothStats.steps ? clothStats.stepMs / clothStats.steps : 0.0, clothStats.stepMax,
				clothStats.frames ? clothStats.frontMs / clothStats.frames : 0.0,
				clothStats.steps ? (double)clothStats.contacts / clothStats.steps : 0.0,
				clothStats.steps ? (double)clothStats.hand / clothStats.steps : 0.0,
				clothStats.steps ? (double)clothStats.cone / clothStats.steps : 0.0, clothStats.maxFrameMs, clothStats.slowFrames );
			if ( clothStats.dbgSteps ) {
				ri.Printf( PRINT_ALL, "CLOTHDEBUG pinned-vs-skin max %.3f cm | dynamic disp mean %.2f cm dz %.2f cm | mats row-length dev %.5f | pelvis %.0f %.0f %.0f cm\n",
					clothStats.dbgKinMax, clothStats.dbgDisp / clothStats.dbgSteps, clothStats.dbgDz / clothStats.dbgSteps, clothStats.dbgRowDev,
					clothStats.dbgPelvis[0], clothStats.dbgPelvis[1], clothStats.dbgPelvis[2] );
			}
			const int keepFrame = clothStats.lastFrameCount;
			const double keepMs = clothStats.lastFrameMs;
			memset( &clothStats, 0, sizeof( clothStats ) );
			clothStats.lastPrint = real;
			clothStats.lastFrameCount = keepFrame;
			clothStats.lastFrameMs = keepMs;
		}
	} else {
		clothStats.lastPrint = 0;
	}
}

void R_ClothAddGhoul( const trRefEntity_t *ent, const model_t *mod, const mdxaHeader_t *aHeader, CBoneCache *bc, qboolean personalModel,
	qboolean shadowView ) {
	if ( !r_cloth || !bc || !mod || !aHeader ) return;
	R_SimCheckFrame();
	const bool perFrame = R_SimPerFrame();
	if ( !perFrame && shadowView ) return;   // as before: a shadow view draws what the other views left
	Cloth_Housekeeping();
	Cloth_CheckCvars();
	clothInst_t *ci = R_ClothFindInst( bc );
	// one decision per frame: a view after the one that made the instance ready neither clears nor rebuilds it
	if ( perFrame && ci && ci->ready && Sim_GateSeen( &ci->gate, tr.frameCount, tr.frameSceneNum ) ) return;
	if ( ci ) ci->ready = false;
	if ( !r_cloth->integer ) return;

	clothModelAsset_t *as = Cloth_AssetForModel( mod );
	if ( !as || !Cloth_BindAsset( as, mod ) ) return;
	if ( personalModel ) return;  // this view does not draw it (first person); a later one (a mirror) may
	if ( perFrame ) {
		// from the scene's camera, whichever view asks: a portal, mirror or shadow camera does not change the answer
		const int in = Sim_InRange( Distance( ent->e.origin, tr.refdef.vieworg ), r_clothMaxDist->value, ci ? ci->gate.inRange : 0 );
		if ( ci ) ci->gate.inRange = in;
		if ( !in ) return;
	} else if ( r_clothMaxDist->value > 0.0f && Distance( ent->e.origin, tr.viewParms.ori.origin ) > r_clothMaxDist->value ) return;

	if ( ci && ci->asset != as ) {  // the instance switched models
		R_ClothForgetBoneCache( bc );
		ci = NULL;
	}
	bool first = false;
	if ( !ci ) {
		clothLoadTimer_t timer( CLOTHLOAD_INST );
		ci = Cloth_CreateInst( bc, as, Distance( ent->e.origin, tr.refdef.vieworg ) );
		if ( !ci ) return;
		first = true;
	}
	ci->lastSeenReal = ri.Milliseconds();
	ci->lastDist = Distance( ent->e.origin, tr.refdef.vieworg );
	if ( ci->aHeader != aHeader && ( ci->bindFailed == aHeader || !Cloth_BindBones( ci, aHeader ) ) ) {
		ci->aHeader = NULL;
		ci->bindFailed = aHeader;   // tried again only for another skeleton
		return;
	}

	const double t0 = ClothNowMs();
	const int now = tr.refdef.time;
	Cloth_SyncKin( ci );   // after Cloth_CheckCvars: an r_clothParam change may have rebuilt the pinned set
	Cloth_GatherPose( ci, bc, ent );
	if ( ci->drawTime != now || !ci->live ) {
		Cloth_Advance( ci, now );
	}
	Cloth_BuildDraw( ci, bc );
	ci->drawTime = now;
	ci->drawSerial++;
	ci->ready = true;
	Sim_GateSet( &ci->gate, tr.frameCount, tr.frameSceneNum );
	ci->gate.inRange = 1;
	const double ms = ClothNowMs() - t0;
	clothStats.frontMs += ms;
	if ( first ) {
		r_clothLoadMs[CLOTHLOAD_FIRST] += ms;
		r_clothLoadCount[CLOTHLOAD_FIRST]++;
	}
}

void R_ClothAddStepStats( int contacts, int hand, int cone, int nan, double ms ) {
	clothStats.contacts += contacts;
	clothStats.hand += hand;
	clothStats.cone += cone;
	clothStats.rNan += nan;
	clothStats.stepMs += ms;
	if ( ms > clothStats.stepMax ) clothStats.stepMax = ms;
}

void R_ClothRegister( void ) {
	r_cloth = ri.Cvar_Get( "r_cloth", "1", CVAR_ARCHIVE_ND, "Simulate garment cloth on Ghoul2 models that ship a .cloth sidecar" );
	r_clothHz = ri.Cvar_Get( "r_clothHz", "60", CVAR_NONE, "Cloth simulation rate (fixed steps per second of scene time)" );
	r_clothBend = ri.Cvar_Get( "r_clothBend", "-1", CVAR_NONE, "Cloth bending model: -1 asset default, 0 dihedral, 1 spring (cheaper)" );
	r_clothMaxDist = ri.Cvar_Get( "r_clothMaxDist", "1500", CVAR_NONE, "Simulate cloth only within this distance of the viewer (0 = any)" );
	r_clothStats = ri.Cvar_Get( "r_clothStats", "0", CVAR_NONE, "Print cloth cost and frame rate every N seconds" );
	r_clothParam = ri.Cvar_Get( "r_clothParam", "", CVAR_NONE, "Set a cloth solver knob: \"<key> <value>\", or \"reset\"" );
	r_clothMaxInstances = ri.Cvar_Get( "r_clothMaxInstances", "8", CVAR_NONE, "Maximum simulated cloth instances" );
	r_clothDebug = ri.Cvar_Get( "r_clothDebug", "0", CVAR_NONE, "Cloth diagnostics in the r_clothStats print" );
	r_clothMaxSteps = ri.Cvar_Get( "r_clothMaxSteps", "4", CVAR_NONE, "Maximum catch-up cloth steps per frame" );
	r_clothProfile = ri.Cvar_Get( "r_clothProfile", "1", CVAR_NONE, "Cloth knobs: 1 game tuning, 2 the first game tuning (forward felt velocity 0.9), 0 solver defaults" );
	r_clothDump = ri.Cvar_Get( "r_clothDump", "", CVAR_NONE, "Debug: write every cloth step (pose matrices, solver output, stats) to this file under the home path" );
	r_clothFeltSpeed = ri.Cvar_Get( "r_clothFeltSpeed", "1", CVAR_NONE, "Cloth: 1 the solver feels the body's motion up to the glue's "
		"teleport speed (2000 u/s), 0 only up to 750 u/s (2000 cm/s; faster, e.g. on the bike or in a long fall, the garment is carried "
		"rigidly). Applied when a solver is made or its parameters reset" );
	r_clothSettle = ri.Cvar_Get( "r_clothSettle", "6", CVAR_NONE, "Cloth: solver steps run on the reset pose before a garment's first draw after a reset (the GPU solver takes 6 at most); 0 = draw the skinned shape and let it relax" );
	r_clothDrawLead = ri.Cvar_Get( "r_clothDrawLead", "2", CVAR_NONE, "Cloth draw: 1 the newest step carried forward to the frame time, 0 a blend of the last two steps (one step behind the body), 2 as 1 but each step's correction blended in over the next step instead of at once (both solvers)" );
	R_SimRegister();
	R_ClothRBRegister();
	r_clothProfile->modified = qfalse;
	r_clothBend->modified = qfalse;
	r_clothParam->modified = r_clothParam->string[0] ? qtrue : qfalse;
}
