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
Garment cloth: rd-vulkan back end. See rd-common/tr_cloth.h.

Drawing. rd-vulkan skins every Ghoul2 surface on the GPU from the model's static VBO (one interleaved stream in
the packed mdxm layout, get_mdxm_stride, 44 bytes: pos float3, normal snorm16x4, st float2, bone refs u8x4,
weights unorm8x4, tangent snorm16x4; tr_local.h G2_VERT_*, clothVkVertGpu_t below). A cloth surface keeps that pipeline (lighting, PBR, sun shadows, depth prepass): RB_SurfaceGhoul asks
RB_ClothVkSurface for a substitute VBO/IBO holding the instance's cloth render vertices in the same layout.
  pinned vertices:  the glm's own bind-pose data, skinned by the GPU exactly like the body around them;
  dynamic vertices: the simulated position/normal in the bind space of the reference bone (the pelvis), with
                    one weight on that bone, so the GPU places them with the pelvis it draws the body with.
                    Tangent: the bind tangent made orthogonal to the simulated normal.

Simulation. With r_clothGpu 1 (default) the solver's particle part runs in compute shaders
(shaders/glsl/cloth_step.comp, cloth_draw.comp; one workgroup per instance) and the render vertices are written
on the GPU into a per-instance buffer: nothing per particle crosses the bus. The front end still runs the
solver's frame part on the CPU (SkirtSim_StepFrame, a few dozen doubles) and queues one step record per step;
vk_begin_frame records the dispatches before the frame's first render pass (R_ClothVkBeginFrame). With
r_clothGpu 0, or for knobs the kernels do not implement (localDamping, contactEvery 1, order 2/3), the CPU solver
runs and the vertices are written once per frame into the frame's dynamic geometry buffer.
*/
#include "tr_local.h"
#include "rd-common/tr_cloth_local.h"
#include "vk_cloth.h"
#include "shaders/glsl/cloth_layout.h"

#include <math.h>
#include <string.h>

extern "C" {
extern const unsigned char cloth_step_comp_spv[];
extern const unsigned int cloth_step_comp_spv_size;
extern const unsigned char cloth_draw_comp_spv[];
extern const unsigned int cloth_draw_comp_spv_size;
}

static cvar_t *r_clothGpu;
static cvar_t *r_clothGpuWG;
static cvar_t *r_clothGpuSkip;
static cvar_t *r_clothDebug;
static cvar_t *r_clothGpuPinSkin;

/* A simulated render vertex hangs from the reference bone alone. Its weights are (254, 0, 0, 1) / 255 on bones
(ref, 0, 0, ref): the same bone twice, so it skins exactly as one weight of 1, and the lit shader can tell simulated
cloth from the body (gen_vert.tmpl passes it in var_Normal.w; no GLM vertex lists one bone twice like this). Simulated
cloth then skips the character self-shadow and the hair-strand paths (gen_frag.tmpl SimCloth). */
template<typename V> static void Cloth_VkMarkSimVert( V *o, int ref ) {
	o->bones[0] = (byte)ref; o->bones[1] = o->bones[2] = 0; o->bones[3] = (byte)ref;
	o->weights[0] = 254; o->weights[1] = o->weights[2] = 0; o->weights[3] = 1;
}

typedef struct {
	float pos[4];
	float nrm[4];
	float st[2];
	byte bones[4];
	byte weights[4];
	float tangent[4];
} clothVkVert_t;

// the C side and the GLSL side share cloth_layout.h, but the struct above, the glue's capsule arrays and
// the record's capsule-dependent offsets agreed only by comment. These hold at this commit; a mismatch now fails here
// what the render buffer holds, the packed Ghoul2 vertex (tr_local.h G2_VERT_*); clothVkVert_t stays the
// float working copy
typedef struct {
	float pos[3];
	short nrm[4];
	float st[2];
	byte bones[4];
	byte weights[4];
	short tangent[4];
} clothVkVertGpu_t;
static inline clothVkVertGpu_t Cloth_VkPack( const clothVkVert_t &v ) {
	clothVkVertGpu_t o;
	VectorCopy( v.pos, o.pos );
	R_PackSnorm16x4( v.nrm, 0.0f, o.nrm );
	o.st[0] = v.st[0];
	o.st[1] = v.st[1];
	memcpy( o.bones, v.bones, 4 );
	memcpy( o.weights, v.weights, 4 );
	R_PackSnorm16x4( v.tangent, v.tangent[3], o.tangent );
	return o;
}
static_assert( sizeof( clothVkVertGpu_t ) == CL_RV_WORDS * 4 && sizeof( clothVkVertGpu_t ) == G2_VERT_STRIDE,
	"the cloth render vertex is CL_RV_WORDS words, the packed Ghoul2 vertex (cloth_draw.comp)" );
static_assert( offsetof( clothVkVertGpu_t, pos ) == G2_VERT_OFS_POS && offsetof( clothVkVertGpu_t, nrm ) == G2_VERT_OFS_NRM
	&& offsetof( clothVkVertGpu_t, st ) == G2_VERT_OFS_ST && offsetof( clothVkVertGpu_t, bones ) == G2_VERT_OFS_BONES
	&& offsetof( clothVkVertGpu_t, weights ) == G2_VERT_OFS_WEIGHTS && offsetof( clothVkVertGpu_t, tangent ) == G2_VERT_OFS_TAN,
	"cloth_draw.comp writes pos at word 0, normal at words 3-4, tangent at words 9-10" );
static_assert( CLOTH_GPU_MAX_CAPS == CL_MAX_CAPS, "tr_cloth_local.h CLOTH_GPU_MAX_CAPS must equal CL_MAX_CAPS" );
static_assert( CLR_CAPNEW == CLR_CAPOLD + CL_MAX_CAPS * 6 && CLR_SIDED == CLR_CAPNEW + CL_MAX_CAPS * 6,
	"the step record's capsule blocks are CL_MAX_CAPS * 6 words each" );
static_assert( CLR_MATS == CLR_DEBUG + 1, "CLR_MATS is the step record's fixed size" );
static_assert( CLD_A == CLD_DEBUG + 1 && CLD_IU == CLD_A + 12 && CLD_NB == CLD_IU + 1 && CLD_RELZ == CLD_NB + 1
	&& CLD_S == CLD_RELZ + 1 && CLD_LEAD2 == CLD_S + 1 && CLD_MATS == CLD_LEAD2 + 1
	&& CLD_WORDS == CLD_MATS, "CLD_WORDS is the draw record's fixed size" );

// asset template: the render vertices in mdxm VBO layout and the cloth index list (render-vertex indices)
struct clothVkAsset_t {
	const model_t *mod;
	std::vector<clothVkVert_t> base;   // nRenderVerts, glm bind pose
	std::vector<uint32_t> idx;         // all render surfaces' triangles
	std::vector<int> idxStart, idxCount;
	bool ok;
};

struct clothVkInst_t {
	// CPU solver: this frame's vertices in the dynamic geometry buffer
	VBO_t vbo;
	IBO_t ibo;
	unsigned frame;       // R_ClothVkBeginFrame serial of the upload
	unsigned drawSerial;  // clothInst_t::drawSerial of the upload
	const void *cmd;      // vk.cmd of the upload
	uint32_t indexBase;   // first index of the cloth list in ibo (in indices)
	bool valid;

	// GPU solver
	bool gpuFailed;                   // creation failed: CPU solver for good
	unsigned buildSerial;             // skirt_topology_t::buildSerial the buffers were built for
	const model_t *mod;               // and the glm
	VkBuffer buf, rvBuf;              // state + topology; render vertices + cloth index list
	VkDeviceMemory mem, rvMem;
	uint32_t rvIndexOffset;           // bytes
	VkDescriptorSet set;
	int refGla;                       // reference bone (VBO bone ref)
	uint32_t recWords;                // step record size
	uint32_t N, M, NT, NDYN, RD;      // element counts (dispatch sizes)
	std::vector<uint32_t> pending;    // step records of this frame
	uint32_t drawRec[CLD_WORDS + 256 * 16];   // CLD_WORDS + NB*16 used (this frame's bone matrices)
	uint32_t drawWords;
	bool drawPending;
	unsigned drawnFrame;              // R_ClothVkBeginFrame serial of the last GPU draw
	VBO_t gvbo;
	IBO_t gibo;
};

static unsigned clothVkFrame = 1;

/* ------------------------------------------------------------------ GPU state */

struct clothVkDead_t {
	VkBuffer buf[2];
	VkDeviceMemory mem[2];
	VkDescriptorSet set;
	unsigned frame;
};

struct clothVkSlot_t {
	VkBuffer buf;                    // host-visible: written by the CPU, read back for the statistics
	VkDeviceMemory mem;
	VkBuffer dev;                    // device-local copy the kernels read
	VkDeviceMemory devMem;
	uint32_t *ptr;
	uint32_t words;
	VkDescriptorSet set;
	std::vector<uint32_t> statsAt;   // stats records written by this slot's last use
	uint32_t steps;
	bool timed;
};

static struct {
	bool tried, ok;
	VkDescriptorSetLayout instLayout, frameLayout;
	VkPipelineLayout layout;
	VkPipeline stepWide, stepSingle, draw;
	VkDescriptorPool pool;
	uint32_t wg, wgSingle, spCap;
	clothVkSlot_t slot[NUM_COMMAND_BUFFERS];
	std::vector<clothVkDead_t> dead;
	std::vector<clothVkInst_t *> live;   // instances with GPU buffers
	VkQueryPool queries;
	float nsPerTick;
	PFN_vkCmdResetQueryPool cmdResetQueryPool;
	PFN_vkCmdWriteTimestamp cmdWriteTimestamp;
	PFN_vkGetQueryPoolResults getQueryPoolResults;
	PFN_vkCreateQueryPool createQueryPool;
	PFN_vkDestroyQueryPool destroyQueryPool;
} cg;

static void Cloth_VkFreeGpu( clothVkInst_t *vi ) {
	for ( size_t i = 0; i < cg.live.size(); i++ ) {
		if ( cg.live[i] == vi ) {
			cg.live.erase( cg.live.begin() + i );
			break;
		}
	}
	if ( vi->buf || vi->rvBuf || vi->set ) {
		clothVkDead_t d;
		d.buf[0] = vi->buf; d.buf[1] = vi->rvBuf;
		d.mem[0] = vi->mem; d.mem[1] = vi->rvMem;
		d.set = vi->set;
		d.frame = clothVkFrame;
		cg.dead.push_back( d );
	}
	vi->buf = vi->rvBuf = VK_NULL_HANDLE;
	vi->mem = vi->rvMem = VK_NULL_HANDLE;
	vi->set = VK_NULL_HANDLE;
	vi->pending.clear();
	vi->drawPending = false;
	vi->drawnFrame = 0;
}

static void Cloth_VkDestroyDead( bool all ) {
	for ( size_t i = 0; i < cg.dead.size(); ) {
		clothVkDead_t &d = cg.dead[i];
		if ( !all && clothVkFrame - d.frame <= NUM_COMMAND_BUFFERS + 1 ) {
			i++;
			continue;
		}
		for ( int k = 0; k < 2; k++ ) {
			if ( d.buf[k] ) VK_DESTROY_BUFFER( vk.device, d.buf[k] );
			if ( d.mem[k] ) VK_FREE_MEMORY( vk.device, d.mem[k] );
		}
		if ( d.set ) qvkFreeDescriptorSets( vk.device, cg.pool, 1, &d.set );
		cg.dead.erase( cg.dead.begin() + i );
	}
}

const mdxmHeader_t *R_ClothModelMdxm( const model_t *mod ) {
	return mod->data.glm ? mod->data.glm->header : NULL;
}

void R_ClothRBFreeAsset( clothModelAsset_t *as ) {
	delete (clothVkAsset_t *)as->rb;
	as->rb = NULL;
}

void R_ClothRBFreeInst( clothInst_t *ci ) {
	clothVkInst_t *vi = (clothVkInst_t *)ci->rb;
	if ( vi ) Cloth_VkFreeGpu( vi );
	delete vi;
	ci->rb = NULL;
}

static clothVkInst_t *Cloth_VkInst( clothInst_t *ci ) {
	clothVkInst_t *vi = (clothVkInst_t *)ci->rb;
	if ( !vi ) {
		vi = new clothVkInst_t();
		memset( &vi->vbo, 0, sizeof( vi->vbo ) );
		memset( &vi->ibo, 0, sizeof( vi->ibo ) );
		vi->frame = vi->drawSerial = 0;
		vi->cmd = NULL;
		vi->indexBase = 0;
		vi->valid = false;
		vi->gpuFailed = false;
		vi->buildSerial = 0;
		vi->mod = NULL;
		vi->buf = vi->rvBuf = VK_NULL_HANDLE;
		vi->mem = vi->rvMem = VK_NULL_HANDLE;
		vi->rvIndexOffset = 0;
		vi->set = VK_NULL_HANDLE;
		vi->refGla = -1;
		vi->recWords = 0;
		vi->drawPending = false;
		vi->drawnFrame = 0;
		memset( vi->drawRec, 0, sizeof( vi->drawRec ) );
		vi->drawWords = CLD_WORDS;
		memset( &vi->gvbo, 0, sizeof( vi->gvbo ) );
		memset( &vi->gibo, 0, sizeof( vi->gibo ) );
		ci->rb = vi;
	}
	return vi;
}

/* ------------------------------------------------------------------ render template */

static float Cloth_VkWeight( const mdxmVertex_t *v, int w ) {
	int t = v->BoneWeightings[w];
	t |= ( v->uiNmWeightsAndBoneIndexes >> ( iG2_BONEWEIGHT_TOPBITS_SHIFT + ( w * 2 ) ) ) & iG2_BONEWEIGHT_TOPBITS_AND;
	return fG2_BONEWEIGHT_RECIPROCAL_MULT * t;
}

// the render vertices of the asset's LOD 0 surfaces, packed like R_BuildMDXM
static clothVkAsset_t *Cloth_VkAsset( clothModelAsset_t *as ) {
	clothVkAsset_t *va = (clothVkAsset_t *)as->rb;
	if ( va && va->mod == as->boundMod ) {
		return va->ok ? va : NULL;
	}
	if ( !va ) {
		va = new clothVkAsset_t();
		as->rb = va;
	}
	va->mod = as->boundMod;
	va->ok = false;
	const int nRS = (int)as->rsLod0.size();
	const int R = as->a.nRenderVerts;
	va->base.assign( R, clothVkVert_t() );
	va->idx.clear();
	va->idxStart.assign( nRS, 0 );
	va->idxCount.assign( nRS, 0 );
	// LOD 0's tangents as R_BuildMDXM packed them (vk_tangent_cache.cpp), found by each surface's first vertex in
	// the LOD; without them every surface computes its own as before
	const mdxmHeader_t *mdxm = R_ClothModelMdxm( as->boundMod );
	const mdxmLOD_t *lod0 = mdxm ? (const mdxmLOD_t *)( (const byte *)mdxm + mdxm->ofsLODs ) : NULL;
	const mdxmSurface_t *first = lod0 ? (const mdxmSurface_t *)( (const byte *)lod0 + sizeof( mdxmLOD_t ) + mdxm->numSurfaces * sizeof( mdxmLODSurfOffset_t ) ) : NULL;
	std::vector<const mdxmSurface_t *> lodSurf;
	std::vector<int> lodBase;
	int lodVerts = 0;
	for ( int n = 0; first && n < mdxm->numSurfaces; n++ ) {
		lodSurf.push_back( first );
		lodBase.push_back( lodVerts );
		lodVerts += first->numVerts;
		first = (const mdxmSurface_t *)( (const byte *)first + first->ofsEnd );
	}
	const float *prepared = mdxm ? R_MdxmTangents( mdxm, 0, lodVerts ) : NULL;
	for ( int s = 0; s < nRS; s++ ) {
		const mdxmSurface_t *surf = as->rsLod0[s];
		const int start = as->rsStart[s], count = as->rsCount[s];
		if ( start < 0 || start + count > R ) {
			ri.Printf( PRINT_WARNING, "cloth: %s: render map out of range\n", as->name );
			return NULL;
		}
		mdxmVertex_t *v = (mdxmVertex_t *)( (const byte *)surf + surf->ofsVerts );
		mdxmVertexTexCoord_t *tc = (mdxmVertexTexCoord_t *)( v + surf->numVerts );
		const int *boneRef = (const int *)( (const byte *)surf + surf->ofsBoneReferences );
		const mdxmTriangle_t *t = (const mdxmTriangle_t *)( (const byte *)surf + surf->ofsTriangles );

		std::vector<glIndex_t> local( surf->numTriangles * 3 );
		va->idxStart[s] = (int)va->idx.size();
		va->idxCount[s] = surf->numTriangles * 3;
		for ( int k = 0; k < surf->numTriangles; k++ ) {
			for ( int j = 0; j < 3; j++ ) {
				local[k * 3 + j] = t[k].indexes[j];
				va->idx.push_back( (uint32_t)( start + t[k].indexes[j] ) );
			}
		}
		const float *surfPrepared = NULL;
		for ( size_t n = 0; prepared && n < lodSurf.size(); n++ ) {
			if ( lodSurf[n] == surf ) surfPrepared = prepared + (size_t)lodBase[n] * 4;
		}
		std::vector<vec4_t> tangents( surfPrepared ? 1 : surf->numVerts );
		if ( !surfPrepared ) vk_mikkt_mdxm_generate( surf->numTriangles, v, tc, &tangents[0], &local[0] );
		else if ( r_tangentCache->integer == 2 ) {
			ri.Printf( PRINT_ALL, "tangent cache: cloth %s surface %d: %s\n", as->name, s, R_TangentCheckSurface( surf, surfPrepared ) ? "equal" : "DIFFERS" );
		}

		for ( int k = 0; k < count; k++ ) {
			clothVkVert_t &o = va->base[start + k];
			VectorCopy( v[k].vertCoords, o.pos );
			o.pos[3] = 1.0f;
			VectorCopy( v[k].normal, o.nrm );
			o.nrm[3] = 0.0f;
			o.st[0] = tc[k].texCoords[0];
			o.st[1] = tc[k].texCoords[1];
			const int numWeights = G2_GetVertWeights( &v[k] );
			int lastWeight = 255;
			const int last = numWeights - 1;
			for ( int w = 0; w < last; w++ ) {
				o.weights[w] = (byte)( Cloth_VkWeight( &v[k], w ) * 255.0f );
				o.bones[w] = (byte)boneRef[G2_GetVertBoneIndex( &v[k], w )];
				lastWeight -= o.weights[w];
			}
			o.weights[last] = (byte)( lastWeight > 0 ? lastWeight : 0 );
			o.bones[last] = (byte)boneRef[G2_GetVertBoneIndex( &v[k], last )];
			for ( int w = numWeights; w < 4; w++ ) {
				o.weights[w] = 0;
				o.bones[w] = 0;
			}
			const float *tangent = surfPrepared ? surfPrepared + (size_t)k * 4 : tangents[k];
			VectorCopy4( tangent, o.tangent );
		}
	}
	va->ok = true;
	return va;
}

// inverse of a 3x4 bone matrix (rows = output axes): inv = A^-1, ot = -A^-1 t
static bool Cloth_VkInvertBone( const mdxaBone_t *b, float inv[3][3], float ot[3] ) {
	const float (*m)[4] = b->matrix;
	const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
	const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
	const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
	const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
	if ( fabsf( det ) < 1e-12f ) return false;
	const float id = 1.0f / det;
	inv[0][0] = c00 * id;
	inv[0][1] = ( m[0][2] * m[2][1] - m[0][1] * m[2][2] ) * id;
	inv[0][2] = ( m[0][1] * m[1][2] - m[0][2] * m[1][1] ) * id;
	inv[1][0] = c01 * id;
	inv[1][1] = ( m[0][0] * m[2][2] - m[0][2] * m[2][0] ) * id;
	inv[1][2] = ( m[0][2] * m[1][0] - m[0][0] * m[1][2] ) * id;
	inv[2][0] = c02 * id;
	inv[2][1] = ( m[0][1] * m[2][0] - m[0][0] * m[2][1] ) * id;
	inv[2][2] = ( m[0][0] * m[1][1] - m[0][1] * m[1][0] ) * id;
	for ( int r = 0; r < 3; r++ ) {
		ot[r] = -( inv[r][0] * m[0][3] + inv[r][1] * m[1][3] + inv[r][2] * m[2][3] );
	}
	return true;
}

/* ------------------------------------------------------------------ CPU solver: per-frame upload */

// reserve bytes in this frame's dynamic geometry buffer
static bool Cloth_VkReserve( uint32_t size, uint32_t *offset ) {
	const uint32_t off = PAD( (uint32_t)vk.cmd->vertex_buffer_offset, 64 );
	if ( off + size > vk.geometry_buffer_size ) {
		vk.geometry_buffer_size_new = log2pad( off + size, 1 );   // grows at the next frame
		return false;
	}
	*offset = off;
	vk.cmd->vertex_buffer_offset = (VkDeviceSize)off + size;
	return true;
}

static bool Cloth_VkUpload( clothInst_t *ci, clothVkAsset_t *va, clothVkInst_t *vi, CBoneCache *bc ) {
	const clothModelAsset_t *as = ci->asset;
	const int R = as->a.nRenderVerts;
	const int ref = ci->boneMap[as->a.refBone];   // gla bone (= the VBO bone ref)
	if ( ref < 0 || ref > 255 ) return false;
	float inv[3][3], ot[3];
	if ( !Cloth_VkInvertBone( R_ClothEvalBone( bc, ref ), inv, ot ) ) return false;

	const uint32_t vbytes = (uint32_t)( R * sizeof( clothVkVertGpu_t ) );
	const uint32_t ibytes = (uint32_t)( va->idx.size() * sizeof( uint32_t ) );
	uint32_t voff, ioff;
	if ( !Cloth_VkReserve( vbytes, &voff ) || !Cloth_VkReserve( ibytes, &ioff ) ) return false;

	clothVkVertGpu_t *dst = (clothVkVertGpu_t *)( vk.cmd->vertex_buffer_ptr + voff );
	const clothVkVert_t *base = &va->base[0];
	const float *dp = &ci->drawPos[0], *dn = &ci->drawNrm[0];
	const unsigned char *kin = &ci->kin[0];
	for ( int v = 0; v < R; v++ ) {
		const clothVkVert_t &b = base[v];
		clothVkVert_t o = b;
		if ( R_ClothRenderDynamic( as, kin, v ) ) {
			float p[3], n[3];
			R_ClothRenderVertex( as, dp, dn, v, p, n );
			for ( int r = 0; r < 3; r++ ) {
				o.pos[r] = inv[r][0] * p[0] + inv[r][1] * p[1] + inv[r][2] * p[2] + ot[r];
				o.nrm[r] = inv[r][0] * n[0] + inv[r][1] * n[1] + inv[r][2] * n[2];
			}
			VectorNormalize( o.nrm );
			const float d = DotProduct( b.tangent, o.nrm );
			vec3_t t;
			VectorMA( b.tangent, -d, o.nrm, t );
			if ( VectorNormalize( t ) > 0.0f ) VectorCopy( t, o.tangent );
			Cloth_VkMarkSimVert( &o, ref );
		}
		dst[v] = Cloth_VkPack( o );
	}
	memcpy( vk.cmd->vertex_buffer_ptr + ioff, &va->idx[0], ibytes );

	memset( &vi->vbo, 0, sizeof( vi->vbo ) );
	vi->vbo.buffer = vk.cmd->vertex_buffer;
	vi->vbo.offsets[0] = voff + offsetof( clothVkVertGpu_t, pos );
	vi->vbo.offsets[5] = voff + offsetof( clothVkVertGpu_t, nrm );
	vi->vbo.offsets[2] = voff + offsetof( clothVkVertGpu_t, st );
	vi->vbo.offsets[10] = voff + offsetof( clothVkVertGpu_t, bones );
	vi->vbo.offsets[11] = voff + offsetof( clothVkVertGpu_t, weights );
	vi->vbo.offsets[8] = voff + offsetof( clothVkVertGpu_t, tangent );
	vi->vbo.size = (int)vbytes;
	memset( &vi->ibo, 0, sizeof( vi->ibo ) );
	vi->ibo.buffer = vk.cmd->vertex_buffer;
	vi->ibo.size = (int)ibytes;
	vi->indexBase = ioff / sizeof( uint32_t );
	return true;
}

/* ------------------------------------------------------------------ GPU solver: setup */

static bool Cloth_VkCreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem, const char *name ) {
	VkBufferCreateInfo desc;
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.size = size;
	desc.usage = usage;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if ( qvkCreateBuffer( vk.device, &desc, NULL, buf ) != VK_SUCCESS ) return false;
	VkMemoryRequirements req;
	qvkGetBufferMemoryRequirements( vk.device, *buf, &req );
	VkMemoryAllocateInfo alloc;
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	// device-local buffers: system memory past the VRAM budget (vk_vram.cpp)
	alloc.memoryTypeIndex = props == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? vk_vram_memory_type( req.memoryTypeBits, req.size )
		: vk_find_memory_type( req.memoryTypeBits, props );
	if ( vk_vram_allocate_named( vk.device, &alloc, mem, "cloth" ) != VK_SUCCESS ) {
		qvkDestroyBuffer( vk.device, *buf, NULL );
		*buf = VK_NULL_HANDLE;
		return false;
	}
	qvkBindBufferMemory( vk.device, *buf, *mem, 0 );
	VK_SET_OBJECT_NAME( *buf, name, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	return true;
}

// host data into the start of a device-local buffer: vk_upload.cpp's ring (no wait), else a one-shot copy
static bool Cloth_VkUploadBuffer( VkBuffer dst, const void *data, VkDeviceSize size ) {
	if ( vk_upload_buffer( dst, 0, data, size ) ) return true;
	VkBuffer staging;
	VkDeviceMemory mem;
	if ( !Cloth_VkCreateBuffer( size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&staging, &mem, "cloth staging" ) ) return false;
	void *p;
	VK_CHECK( qvkMapMemory( vk.device, mem, 0, VK_WHOLE_SIZE, 0, &p ) );
	memcpy( p, data, (size_t)size );
	qvkUnmapMemory( vk.device, mem );
	VkCommandBuffer cmd = vk_begin_command_buffer();
	VkBufferCopy region;
	region.srcOffset = 0;
	region.dstOffset = 0;
	region.size = size;
	qvkCmdCopyBuffer( cmd, staging, dst, 1, &region );
	vk_end_command_buffer( cmd, __func__ );
	VK_DESTROY_BUFFER( vk.device, staging );
	VK_FREE_MEMORY( vk.device, mem );
	return true;
}

static VkShaderModule Cloth_VkShader( const unsigned char *code, unsigned int size ) {
	VkShaderModuleCreateInfo desc;
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.codeSize = size;
	desc.pCode = (const uint32_t *)code;
	VkShaderModule m = VK_NULL_HANDLE;
	if ( qvkCreateShaderModule( vk.device, &desc, NULL, &m ) != VK_SUCCESS ) return VK_NULL_HANDLE;
	return m;
}

static void Cloth_VkShutdownGpu( void ) {
	if ( !vk.device ) return;
	if ( cg.tried || !cg.dead.empty() ) {
		vk_upload_flush();
		qvkDeviceWaitIdle( vk.device );
	}
	while ( !cg.live.empty() ) Cloth_VkFreeGpu( cg.live.back() );
	if ( cg.pool ) Cloth_VkDestroyDead( true );
	for ( int i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		clothVkSlot_t &s = cg.slot[i];
		if ( s.ptr ) qvkUnmapMemory( vk.device, s.mem );
		if ( s.buf ) VK_DESTROY_BUFFER( vk.device, s.buf );
		if ( s.mem ) VK_FREE_MEMORY( vk.device, s.mem );
		if ( s.dev ) VK_DESTROY_BUFFER( vk.device, s.dev );
		if ( s.devMem ) VK_FREE_MEMORY( vk.device, s.devMem );
		s.buf = VK_NULL_HANDLE; s.mem = VK_NULL_HANDLE; s.dev = VK_NULL_HANDLE; s.devMem = VK_NULL_HANDLE;
		s.ptr = NULL; s.words = 0; s.set = VK_NULL_HANDLE;
		s.statsAt.clear(); s.steps = 0; s.timed = false;
	}
	if ( cg.queries && cg.destroyQueryPool ) cg.destroyQueryPool( vk.device, cg.queries, NULL );
	cg.queries = VK_NULL_HANDLE;
	if ( cg.stepWide ) qvkDestroyPipeline( vk.device, cg.stepWide, NULL );
	if ( cg.stepSingle ) qvkDestroyPipeline( vk.device, cg.stepSingle, NULL );
	if ( cg.draw ) qvkDestroyPipeline( vk.device, cg.draw, NULL );
	if ( cg.layout ) qvkDestroyPipelineLayout( vk.device, cg.layout, NULL );
	if ( cg.instLayout ) qvkDestroyDescriptorSetLayout( vk.device, cg.instLayout, NULL );
	if ( cg.frameLayout ) qvkDestroyDescriptorSetLayout( vk.device, cg.frameLayout, NULL );
	if ( cg.pool ) qvkDestroyDescriptorPool( vk.device, cg.pool, NULL );
	cg.stepWide = cg.stepSingle = cg.draw = VK_NULL_HANDLE;
	cg.layout = VK_NULL_HANDLE;
	cg.instLayout = cg.frameLayout = VK_NULL_HANDLE;
	cg.pool = VK_NULL_HANDLE;
	cg.tried = cg.ok = false;
}

static bool Cloth_VkInitGpu( void ) {
	if ( cg.tried ) return cg.ok;
	cg.tried = true;
	cg.ok = false;
	if ( !vk.device ) return false;

	VkPhysicalDeviceProperties props;
	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
	// element-parallel stages: small workgroups spread over the device; the iterations: one workgroup, as large as
	// the device allows, with the positions in shared memory
	uint32_t wg = r_clothGpuWG->integer > 0 ? (uint32_t)r_clothGpuWG->integer : 1024u;
	if ( wg > props.limits.maxComputeWorkGroupInvocations ) wg = props.limits.maxComputeWorkGroupInvocations;
	if ( wg > props.limits.maxComputeWorkGroupSize[0] ) wg = props.limits.maxComputeWorkGroupSize[0];
	cg.wgSingle = wg;
	cg.wg = 64;
	const uint32_t sharedBytes = props.limits.maxComputeSharedMemorySize;
	cg.spCap = sharedBytes > 4096 ? ( sharedBytes - 4096 ) / 4 : 4;

	VkDescriptorSetLayoutBinding bind[6];
	for ( int i = 0; i < 6; i++ ) {
		bind[i].binding = i;
		bind[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bind[i].descriptorCount = 1;
		bind[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bind[i].pImmutableSamplers = NULL;
	}
	VkDescriptorSetLayoutCreateInfo dl;
	Com_Memset( &dl, 0, sizeof( dl ) );
	dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dl.bindingCount = 6;
	dl.pBindings = bind;
	if ( qvkCreateDescriptorSetLayout( vk.device, &dl, NULL, &cg.instLayout ) != VK_SUCCESS ) return false;
	dl.bindingCount = 2;
	if ( qvkCreateDescriptorSetLayout( vk.device, &dl, NULL, &cg.frameLayout ) != VK_SUCCESS ) return false;

	VkDescriptorSetLayout sets[2] = { cg.instLayout, cg.frameLayout };
	VkPushConstantRange push;
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = 16;
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 2;
	pl.pSetLayouts = sets;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &cg.layout ) != VK_SUCCESS ) return false;

	VkDescriptorPoolSize ps;
	ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	ps.descriptorCount = 6 * 64 + 2 * NUM_COMMAND_BUFFERS;
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	dp.maxSets = 64 + NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 1;
	dp.pPoolSizes = &ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &cg.pool ) != VK_SUCCESS ) return false;

	VkSpecializationMapEntry se[2];
	for ( int i = 0; i < 2; i++ ) {
		se[i].constantID = i;
		se[i].offset = i * sizeof( uint32_t );
		se[i].size = sizeof( uint32_t );
	}
	const unsigned char *code[3] = { cloth_step_comp_spv, cloth_step_comp_spv, cloth_draw_comp_spv };
	const unsigned int size[3] = { cloth_step_comp_spv_size, cloth_step_comp_spv_size, cloth_draw_comp_spv_size };
	const uint32_t spec[3][2] = { { cg.wg, 4 }, { cg.wgSingle, cg.spCap }, { cg.wg, 4 } };
	VkPipeline *out[3] = { &cg.stepWide, &cg.stepSingle, &cg.draw };
	for ( int i = 0; i < 3; i++ ) {
		VkSpecializationInfo si;
		si.mapEntryCount = i == 2 ? 1 : 2;
		si.pMapEntries = se;
		si.dataSize = si.mapEntryCount * sizeof( uint32_t );
		si.pData = spec[i];
		VkShaderModule m = Cloth_VkShader( code[i], size[i] );
		if ( !m ) return false;
		VkComputePipelineCreateInfo ci;
		Com_Memset( &ci, 0, sizeof( ci ) );
		ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		ci.stage.module = m;
		ci.stage.pName = "main";
		ci.stage.pSpecializationInfo = &si;
		ci.layout = cg.layout;
		const VkResult res = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, out[i] );
		qvkDestroyShaderModule( vk.device, m, NULL );
		if ( res != VK_SUCCESS ) return false;
	}

	// GPU time of the cloth work (timestamps, read back with the slot's stats)
	cg.createQueryPool = (PFN_vkCreateQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCreateQueryPool" );
	cg.destroyQueryPool = (PFN_vkDestroyQueryPool)qvkGetDeviceProcAddr( vk.device, "vkDestroyQueryPool" );
	cg.cmdResetQueryPool = (PFN_vkCmdResetQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCmdResetQueryPool" );
	cg.cmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)qvkGetDeviceProcAddr( vk.device, "vkCmdWriteTimestamp" );
	cg.getQueryPoolResults = (PFN_vkGetQueryPoolResults)qvkGetDeviceProcAddr( vk.device, "vkGetQueryPoolResults" );
	cg.nsPerTick = props.limits.timestampPeriod;
	if ( cg.createQueryPool && cg.destroyQueryPool && cg.cmdResetQueryPool && cg.cmdWriteTimestamp && cg.getQueryPoolResults
		&& props.limits.timestampComputeAndGraphics && cg.nsPerTick > 0.0f ) {
		VkQueryPoolCreateInfo qp;
		Com_Memset( &qp, 0, sizeof( qp ) );
		qp.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
		qp.queryCount = 2 * NUM_COMMAND_BUFFERS;
		if ( cg.createQueryPool( vk.device, &qp, NULL, &cg.queries ) != VK_SUCCESS ) cg.queries = VK_NULL_HANDLE;
	}
	ri.Printf( PRINT_DEVELOPER, "cloth: GPU solver ready, workgroups %u / %u, %u shared position words\n", cg.wg, cg.wgSingle, cg.spCap );
	cg.ok = true;
	return true;
}

// greedy colouring: no two constraints of a colour share a dynamic particle (w != 0). Returns the colour count;
// order receives the constraints sorted by colour (original order within a colour), starts the colour offsets.
static int Cloth_VkColour( int n, int arity, const uint32_t *idx, int stride, const skirt_real *w, int N,
	std::vector<uint32_t> &order, std::vector<uint32_t> &starts ) {
	std::vector<uint64_t> used( (size_t)N * 4, 0 );
	std::vector<int> colour( n, -1 );
	int nc = 0;
	for ( int c = 0; c < n; c++ ) {
		const uint32_t *q = idx + (size_t)c * stride;
		uint64_t m[4] = { 0, 0, 0, 0 };
		bool any = false;
		for ( int k = 0; k < arity; k++ ) {
			if ( w[q[k]] == 0 ) continue;
			any = true;
			for ( int j = 0; j < 4; j++ ) m[j] |= used[q[k] * 4 + j];
		}
		if ( !any ) continue;   // all pinned: a no-op
		int col = -1;
		for ( int j = 0; j < 4 && col < 0; j++ ) {
			if ( m[j] != ~(uint64_t)0 ) {
				for ( int b = 0; b < 64; b++ ) {
					if ( !( m[j] & ( (uint64_t)1 << b ) ) ) { col = j * 64 + b; break; }
				}
			}
		}
		if ( col < 0 ) return -1;
		colour[c] = col;
		if ( col + 1 > nc ) nc = col + 1;
		for ( int k = 0; k < arity; k++ ) {
			if ( w[q[k]] == 0 ) continue;
			used[q[k] * 4 + col / 64] |= (uint64_t)1 << ( col % 64 );
		}
	}
	starts.assign( nc + 1, 0 );
	for ( int c = 0; c < n; c++ ) if ( colour[c] >= 0 ) starts[colour[c] + 1]++;
	for ( int k = 0; k < nc; k++ ) starts[k + 1] += starts[k];
	std::vector<uint32_t> fill( starts.begin(), starts.end() - 1 );
	order.assign( starts[nc], 0 );
	for ( int c = 0; c < n; c++ ) if ( colour[c] >= 0 ) order[fill[colour[c]]++] = (uint32_t)c;
	return nc;
}

static inline uint32_t Cloth_FBits( float v ) {
	uint32_t u;
	memcpy( &u, &v, 4 );
	return u;
}

// build the instance's state/topology buffer and its render buffer, upload both
static bool Cloth_VkCreateGpu( clothInst_t *ci, clothVkInst_t *vi ) {
	clothModelAsset_t *as = ci->asset;
	clothVkAsset_t *va = Cloth_VkAsset( as );
	if ( !va ) return false;
	skirt_topology_t T;
	SkirtSim_Topology( ci->sim, &T );
	const int ref = ci->boneMap[as->a.refBone];
	if ( ref < 0 || ref > 255 || T.nCaps > CL_MAX_CAPS || T.N <= 0 ) return false;
	const int N = T.N, M = T.M, R = as->a.nRenderVerts;

	// cloth vertex -> triangle CSR (aero gather, normals)
	std::vector<uint32_t> pts( N + 1, 0 ), ptl( (size_t)T.NT * 3 );
	for ( int t = 0; t < T.NT * 3; t++ ) pts[T.tri[t] + 1]++;
	for ( int i = 0; i < N; i++ ) pts[i + 1] += pts[i];
	{
		std::vector<uint32_t> fill( pts.begin(), pts.end() - 1 );
		for ( int t = 0; t < T.NT; t++ ) for ( int k = 0; k < 3; k++ ) ptl[fill[T.tri[t * 3 + k]]++] = (uint32_t)t;
	}
	// colours
	std::vector<uint32_t> eOrd, eSt, bOrd, bSt, aOrd, aSt;
	std::vector<uint32_t> ePairs( (size_t)T.NE * 2 );
	for ( int e = 0; e < T.NE; e++ ) { ePairs[e * 2] = T.e0[e]; ePairs[e * 2 + 1] = T.e1[e]; }
	const int nEC = Cloth_VkColour( T.NE, 2, &ePairs[0], 2, T.w, N, eOrd, eSt );
	const int nBC = T.NB ? Cloth_VkColour( T.NB, 4, T.bq, 4, T.w, N, bOrd, bSt ) : 0;
	const int nAC = Cloth_VkColour( T.NT, 3, T.tri, 3, T.w, N, aOrd, aSt );
	if ( nEC < 0 || nBC < 0 || nAC < 0 ) return false;
	if ( !nBC ) bSt.assign( 1, 0 );

	// layout
	uint32_t hdr[CLH_WORDS];
	memset( hdr, 0, sizeof( hdr ) );
	uint32_t words = PAD( CLH_WORDS, 4 );
	auto alloc = [&]( int slot, size_t n ) { hdr[slot] = words; words += (uint32_t)PAD( ( n > 0 ? n : 1 ), 4 ); };
	const uint32_t nnzS = T.skinStart[N], nnzC = M ? T.cSkinStart[M] : 0;
	const uint32_t nVT = M ? T.vtStart[M] : 0, nVV = M ? T.vvStart[M] : 0;
	alloc( CLH_O_X0, N * 3 ); alloc( CLH_O_SKS, N + 1 ); alloc( CLH_O_SKB, nnzS ); alloc( CLH_O_SKW, nnzS );
	alloc( CLH_O_CP0, M * 3 ); alloc( CLH_O_CSKS, M + 1 ); alloc( CLH_O_CSKB, nnzC ); alloc( CLH_O_CSKW, nnzC );
	alloc( CLH_O_CTRI, T.cNT * 3 ); alloc( CLH_O_VTS, M + 1 ); alloc( CLH_O_VTL, nVT ); alloc( CLH_O_VVS, M + 1 ); alloc( CLH_O_VVL, nVV );
	alloc( CLH_O_VRAD, M );
	alloc( CLH_O_W, N ); alloc( CLH_O_KIN, N ); alloc( CLH_O_ADMAP, N ); alloc( CLH_O_MAXD, N ); alloc( CLH_O_DYN, T.nDyn );
	alloc( CLH_O_EC, nEC + 1 ); alloc( CLH_O_E, eOrd.size() * 3 );
	alloc( CLH_O_BC, nBC + 1 ); alloc( CLH_O_B, bOrd.size() * 6 );
	alloc( CLH_O_AC, nAC + 1 ); alloc( CLH_O_A, aOrd.size() * 4 );
	alloc( CLH_O_TETS, N + 1 ); alloc( CLH_O_TETA, T.NTet ); alloc( CLH_O_TETL, T.NTet );
	alloc( CLH_O_TRI, T.NT * 3 ); alloc( CLH_O_PTS, N + 1 ); alloc( CLH_O_PTL, T.NT * 3 );
	alloc( CLH_O_CAPR, T.nCaps );
	alloc( CLH_O_X, N * 3 ); alloc( CLH_O_V, N * 3 ); alloc( CLH_O_P, N * 3 );
	alloc( CLH_O_TOLD, N * 3 ); alloc( CLH_O_TNEW, N * 3 ); alloc( CLH_O_TS, N * 3 );
	alloc( CLH_O_COLD, M * 3 ); alloc( CLH_O_CNEW, M * 3 ); alloc( CLH_O_CS, M * 3 );
	alloc( CLH_O_CTRIX, N ); alloc( CLH_O_CVERT, N ); alloc( CLH_O_PUSH, N ); alloc( CLH_O_CNRM, N * 3 );
	alloc( CLH_O_STRI, N ); alloc( CLH_O_CONEC, N );
	alloc( CLH_O_HHEAD, M ? CL_HASH : 1 ); alloc( CLH_O_HNEXT, M );
	alloc( CLH_O_TRIF, T.NT * 3 ); alloc( CLH_O_REL0, N * 3 ); alloc( CLH_O_REL1, N * 3 ); alloc( CLH_O_REL2, N * 3 );
	alloc( CLH_O_DPOS, N * 3 ); alloc( CLH_O_DNRM, N * 3 );
	std::vector<uint32_t> rdyn;
	for ( int v = 0; v < R; v++ ) if ( R_ClothRenderDynamic( as, T.kin, v ) ) rdyn.push_back( (uint32_t)v );
	alloc( CLH_O_RSIM, (size_t)R * CL_RW_WORDS ); alloc( CLH_O_RDYN, rdyn.size() ); alloc( CLH_O_RTAN, (size_t)R * 4 );
	hdr[CLH_N] = N; hdr[CLH_NT] = T.NT; hdr[CLH_M] = M; hdr[CLH_NDYN] = T.nDyn; hdr[CLH_NCAPS] = T.nCaps;
	hdr[CLH_NEC] = nEC; hdr[CLH_NBC] = nBC; hdr[CLH_NAC] = nAC; hdr[CLH_R] = R; hdr[CLH_RD] = (uint32_t)rdyn.size();
	hdr[CLH_CMAXEDGE] = Cloth_FBits( (float)T.cMaxEdge );

	std::vector<uint32_t> img( words, 0 );
	memcpy( &img[0], hdr, sizeof( hdr ) );
	auto putF = [&]( int slot, size_t i, float v ) { img[hdr[slot] + i] = Cloth_FBits( v ); };
	auto putU = [&]( int slot, size_t i, uint32_t v ) { img[hdr[slot] + i] = v; };
	for ( int i = 0; i < N * 3; i++ ) putF( CLH_O_X0, i, T.X0[i] );
	for ( int i = 0; i <= N; i++ ) putU( CLH_O_SKS, i, T.skinStart[i] );
	for ( uint32_t k = 0; k < nnzS; k++ ) { putU( CLH_O_SKB, k, T.skinBone[k] ); putF( CLH_O_SKW, k, T.skinW[k] ); }
	if ( M ) {
		for ( int i = 0; i < M * 3; i++ ) putF( CLH_O_CP0, i, T.cPos0[i] );
		for ( int i = 0; i <= M; i++ ) { putU( CLH_O_CSKS, i, T.cSkinStart[i] ); putU( CLH_O_VTS, i, T.vtStart[i] ); putU( CLH_O_VVS, i, T.vvStart[i] ); }
		for ( uint32_t k = 0; k < nnzC; k++ ) { putU( CLH_O_CSKB, k, T.cSkinBone[k] ); putF( CLH_O_CSKW, k, T.cSkinW[k] ); }
		for ( int k = 0; k < T.cNT * 3; k++ ) putU( CLH_O_CTRI, k, T.cTri[k] );
		for ( uint32_t k = 0; k < nVT; k++ ) putU( CLH_O_VTL, k, T.vtList[k] );
		for ( uint32_t k = 0; k < nVV; k++ ) putU( CLH_O_VVL, k, T.vvList[k] );
		for ( int i = 0; i < M; i++ ) putF( CLH_O_VRAD, i, (float)T.vRad[i] );
	}
	for ( int i = 0; i < N; i++ ) {
		putF( CLH_O_W, i, (float)T.w[i] ); putU( CLH_O_KIN, i, T.kin[i] );
		putF( CLH_O_ADMAP, i, T.adMap[i] ); putF( CLH_O_MAXD, i, T.maxDist[i] );
	}
	for ( int n = 0; n < T.nDyn; n++ ) putU( CLH_O_DYN, n, T.dynList[n] );
	for ( int c = 0; c <= nEC; c++ ) putU( CLH_O_EC, c, eSt[c] );
	for ( size_t k = 0; k < eOrd.size(); k++ ) {
		const uint32_t e = eOrd[k];
		putU( CLH_O_E, k * 3, T.e0[e] ); putU( CLH_O_E, k * 3 + 1, T.e1[e] ); putF( CLH_O_E, k * 3 + 2, (float)T.eLen[e] );
	}
	for ( int c = 0; c <= nBC; c++ ) putU( CLH_O_BC, c, bSt[c] );
	for ( size_t k = 0; k < bOrd.size(); k++ ) {
		const uint32_t b = bOrd[k];
		for ( int j = 0; j < 4; j++ ) putU( CLH_O_B, k * 6 + j, T.bq[b * 4 + j] );
		putF( CLH_O_B, k * 6 + 4, (float)T.bPhi0[b] ); putF( CLH_O_B, k * 6 + 5, (float)T.bLen0[b] );
	}
	for ( int c = 0; c <= nAC; c++ ) putU( CLH_O_AC, c, aSt[c] );
	for ( size_t k = 0; k < aOrd.size(); k++ ) {
		const uint32_t t = aOrd[k];
		for ( int j = 0; j < 3; j++ ) putU( CLH_O_A, k * 4 + j, T.tri[t * 3 + j] );
		putF( CLH_O_A, k * 4 + 3, (float)T.A0[t] );
	}
	for ( int i = 0; i <= N; i++ ) putU( CLH_O_TETS, i, T.tetStart[i] );
	for ( int k = 0; k < T.NTet; k++ ) { putU( CLH_O_TETA, k, T.tetAnchor[k] ); putF( CLH_O_TETL, k, (float)T.tetLen[k] ); }
	for ( int k = 0; k < T.NT * 3; k++ ) { putU( CLH_O_TRI, k, T.tri[k] ); putU( CLH_O_PTL, k, ptl[k] ); }
	for ( int i = 0; i <= N; i++ ) putU( CLH_O_PTS, i, pts[i] );
	for ( int c = 0; c < T.nCaps; c++ ) putF( CLH_O_CAPR, c, (float)T.capR[c] );
	for ( int v = 0; v < R; v++ ) {
		for ( int k = 0; k < 3; k++ ) {
			putU( CLH_O_RSIM, (size_t)v * CL_RW_WORDS + k, as->rTri[v * 3 + k] );
			putF( CLH_O_RSIM, (size_t)v * CL_RW_WORDS + 3 + k, as->rW[v * 3 + k] );
		}
		putF( CLH_O_RSIM, (size_t)v * CL_RW_WORDS + 6, as->rOff[v] );
		putF( CLH_O_RSIM, (size_t)v * CL_RW_WORDS + 7, as->rSgn[v] );
		for ( int k = 0; k < 4; k++ ) putF( CLH_O_RTAN, (size_t)v * 4 + k, va->base[v].tangent[k] );
	}
	for ( size_t k = 0; k < rdyn.size(); k++ ) putU( CLH_O_RDYN, k, rdyn[k] );

	// render buffer: the template (dynamic vertices on the reference bone), then the cloth index list
	const uint32_t vbytes = (uint32_t)( R * sizeof( clothVkVertGpu_t ) );
	const uint32_t ibytes = (uint32_t)( va->idx.size() * sizeof( uint32_t ) );
	std::vector<byte> rimg( vbytes + ibytes );
	for ( int v = 0; v < R; v++ ) {
		clothVkVert_t o = va->base[v];
		if ( R_ClothRenderDynamic( as, T.kin, v ) ) {
			Cloth_VkMarkSimVert( &o, ref );
		}
		const clothVkVertGpu_t g = Cloth_VkPack( o );
		memcpy( &rimg[v * sizeof( clothVkVertGpu_t )], &g, sizeof( g ) );
	}
	memcpy( &rimg[vbytes], &va->idx[0], ibytes );

	if ( !Cloth_VkCreateBuffer( (VkDeviceSize)words * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vi->buf, &vi->mem, "cloth state" )
		|| !Cloth_VkCreateBuffer( rimg.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
			| VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vi->rvBuf, &vi->rvMem, "cloth vertices" )
		|| !Cloth_VkUploadBuffer( vi->buf, &img[0], (VkDeviceSize)words * 4 )
		|| !Cloth_VkUploadBuffer( vi->rvBuf, &rimg[0], rimg.size() ) ) {
		Cloth_VkFreeGpu( vi );
		return false;
	}
	vi->rvIndexOffset = vbytes;

	VkDescriptorSetAllocateInfo da;
	Com_Memset( &da, 0, sizeof( da ) );
	da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	da.descriptorPool = cg.pool;
	da.descriptorSetCount = 1;
	da.pSetLayouts = &cg.instLayout;
	if ( qvkAllocateDescriptorSets( vk.device, &da, &vi->set ) != VK_SUCCESS ) {
		vi->set = VK_NULL_HANDLE;
		Cloth_VkFreeGpu( vi );
		return false;
	}
	VkDescriptorBufferInfo bi[6];
	VkWriteDescriptorSet wr[6];
	for ( int i = 0; i < 6; i++ ) {
		bi[i].buffer = i == 3 ? vi->rvBuf : vi->buf;
		bi[i].offset = 0;
		bi[i].range = VK_WHOLE_SIZE;
		Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
		wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wr[i].dstSet = vi->set;
		wr[i].dstBinding = i;
		wr[i].descriptorCount = 1;
		wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		wr[i].pBufferInfo = &bi[i];
	}
	qvkUpdateDescriptorSets( vk.device, 6, wr, 0, NULL );

	memset( &vi->gvbo, 0, sizeof( vi->gvbo ) );
	vi->gvbo.buffer = vi->rvBuf;
	vi->gvbo.offsets[0] = offsetof( clothVkVertGpu_t, pos );
	vi->gvbo.offsets[5] = offsetof( clothVkVertGpu_t, nrm );
	vi->gvbo.offsets[2] = offsetof( clothVkVertGpu_t, st );
	vi->gvbo.offsets[10] = offsetof( clothVkVertGpu_t, bones );
	vi->gvbo.offsets[11] = offsetof( clothVkVertGpu_t, weights );
	vi->gvbo.offsets[8] = offsetof( clothVkVertGpu_t, tangent );
	vi->gvbo.size = (int)vbytes;
	memset( &vi->gibo, 0, sizeof( vi->gibo ) );
	vi->gibo.buffer = vi->rvBuf;
	vi->gibo.size = (int)ibytes;

	vi->buildSerial = T.buildSerial;
	vi->mod = as->boundMod;
	vi->refGla = ref;
	vi->recWords = CLR_MATS + (uint32_t)as->a.nBones * 16;
	vi->N = N; vi->M = M; vi->NT = T.NT; vi->NDYN = T.nDyn; vi->RD = (uint32_t)rdyn.size();
	vi->pending.clear();
	vi->drawPending = false;
	vi->drawnFrame = 0;
	cg.live.push_back( vi );
	ri.Printf( PRINT_DEVELOPER, "cloth: %s on the GPU: %d particles (%d dynamic), colours edge %d hinge %d area %d, %u KB state\n",
		as->name, N, T.nDyn, nEC, nBC, nAC, words / 256 );
	return true;
}

/* ------------------------------------------------------------------ GPU solver: front end */

bool R_ClothRBUseGpu( clothInst_t *ci ) {
	if ( !r_clothGpu || !r_clothGpu->integer ) return false;
	clothVkInst_t *vi = Cloth_VkInst( ci );
	if ( vi->gpuFailed ) return false;
	const skirt_params_t *P = SkirtSim_Params( ci->sim );
	// knobs the kernels do not implement
	if ( P->localDamping > 0 || P->contactEvery == 1 || P->order >= 2 ) return false;
	if ( ci->asset->a.nCaps > CL_MAX_CAPS ) return false;
	return Cloth_VkInitGpu();
}

bool R_ClothRBGpuStep( clothInst_t *ci, const skirt_frame_t *fr, const float *capOld, const float *capNew,
	const float *mats, const float G[12], int relSlot ) {
	clothVkInst_t *vi = Cloth_VkInst( ci );
	if ( !Cloth_VkInitGpu() || vi->gpuFailed ) return false;
	skirt_topology_t T;
	SkirtSim_Topology( ci->sim, &T );
	if ( vi->buf && ( vi->buildSerial != T.buildSerial || vi->mod != ci->asset->boundMod ) ) {
		// pinned set, masses or tethers changed (eps_kin, massScale, tetherK, geodesic): rebuild from rest
		Cloth_VkFreeGpu( vi );
		if ( !fr->reset ) {
			ci->live = false;
			return true;
		}
	}
	bool haveGpu = vi->buf != VK_NULL_HANDLE;
	if ( !haveGpu ) {
		loadProfScope_t prof( LOADPROF_CLOTHGPU );
		haveGpu = Cloth_VkCreateGpu( ci, vi );
	}
	if ( !haveGpu ) {
		ri.Printf( PRINT_WARNING, "cloth: %s: GPU solver unavailable, using the CPU solver\n", ci->asset->name );
		vi->gpuFailed = true;
		return false;
	}
	if ( fr->reset ) {
		vi->pending.clear();   // a reset supersedes anything queued
	} else if ( vi->pending.size() >= (size_t)vi->recWords * CLOTH_GPU_MAX_QUEUED ) {
		// the back end has not run for a while (minimised): start over
		vi->pending.clear();
		ci->live = false;
		return true;
	}
	const skirt_params_t *P = SkirtSim_Params( ci->sim );
	const skirt_config_t &cfg = ci->asset->sa.config;
	const size_t at = vi->pending.size();
	vi->pending.resize( at + vi->recWords, 0 );
	uint32_t *r = &vi->pending[at];
	auto F = [&]( int k, float v ) { r[k] = Cloth_FBits( v ); };
	auto U = [&]( int k, uint32_t v ) { r[k] = v; };
	U( CLR_RESET, fr->reset ? 1 : 0 );
	U( CLR_NSUB, (uint32_t)fr->nSub );
	U( CLR_NIT, (uint32_t)fr->nIt );
	U( CLR_RELOUT, (uint32_t)( relSlot % 3 ) );
	F( CLR_DT, fr->dt ); F( CLR_H, fr->h );
	for ( int k = 0; k < 12; k++ ) F( CLR_F + k, fr->F[k] );
	for ( int k = 0; k < 9; k++ ) F( CLR_D + k, fr->D[k] );
	for ( int k = 0; k < 3; k++ ) {
		F( CLR_DVEC + k, fr->dvec[k] ); F( CLR_CARRYV + k, fr->carryV[k] ); F( CLR_KICKV + k, fr->kickV[k] ); F( CLR_KICKO + k, fr->kickO[k] );
		F( CLR_GL + k, fr->gL[k] ); F( CLR_WF + k, fr->wf[k] ); F( CLR_AEROLIN + k, fr->aeroLin[k] );
		F( CLR_VL + k, fr->VL[k] ); F( CLR_OL + k, fr->OL[k] ); F( CLR_CONEAXIS + k, fr->coneAxis[k] );
	}
	U( CLR_CARRY, fr->carry ? 1 : 0 );
	U( CLR_KICKIMP, fr->kickImpulse ? 1 : 0 );
	F( CLR_SPREAD, fr->spread );
	F( CLR_AEROANG, fr->aeroAng );
	F( CLR_KEDGE, fr->kEdge ); F( CLR_KBEND, fr->kBend ); F( CLR_KAREA, fr->kArea ); F( CLR_KTETHER, fr->kTether );
	F( CLR_KAD, fr->kAD ); F( CLR_DAD, fr->dAD ); F( CLR_DAMPF, fr->dampF );
	U( CLR_ADUSEMAP, fr->adUseMap ? 1 : 0 );
	F( CLR_CONECOS, fr->coneCos );
	F( CLR_TAU, P->thickness ); F( CLR_MU, P->friction ); U( CLR_FRICMODEL, P->frictionModel == 1 ? 1 : 0 );
	F( CLR_BACK, P->backDepth ); U( CLR_COLLERP, P->colliderLerp != 0 ); U( CLR_TRACK, P->trackContacts != 0 );
	U( CLR_REACQ, (uint32_t)( P->reacquire > 0 ? (int)P->reacquire : 0 ) ); F( CLR_REACQDEPTH, P->reacquireDepth );
	F( CLR_MARGIN, P->contactMargin ); U( CLR_HANDCOL, P->handCollision != 0 ); F( CLR_HANDTH, P->handThickness );
	U( CLR_COLLEVERY, P->collideEvery != 0 ); F( CLR_FLIP, P->flipNormals ? -1.0f : 1.0f ); U( CLR_EDGENORMAL, P->edgeNormal != 0 );
	F( CLR_CELL, P->cellSize ); U( CLR_ORDER, P->order == 1 ? 1 : 0 );
	U( CLR_EDGEPASSES, (uint32_t)( P->edgePasses > 0 ? (int)P->edgePasses : 0 ) );
	U( CLR_BENDPASSES, (uint32_t)( P->bendPasses > 0 ? (int)P->bendPasses : 0 ) );
	U( CLR_BENDMODEL, P->bendModel == 1 ? 1 : 0 ); U( CLR_TARGETLERP, P->targetLerp != 0 );
	F( CLR_DAMPING, P->damping ); U( CLR_DAMPREL, P->dampRelative != 0 ); U( CLR_ADPERSUB, P->adPerSubstep != 0 );
	F( CLR_TETHERSCALE, P->tetherScale ); F( CLR_MAXDSCALE, P->maxDistScale ); U( CLR_CONE, P->flareCone > 0 );
	U( CLR_INERTIA, P->inertiaModel == 1 ? 1 : 0 ); U( CLR_ROTVEL, P->rotateVel != 0 );
	U( CLR_AERO, ( ( P->dragScale > 0 || P->liftScale > 0 ) && P->fluidDensity > 0 ) ? 1 : 0 );
	F( CLR_RHO, (float)( (double)P->fluidDensity * 1e-6 ) );
	F( CLR_CD, (float)( cfg.drag * (double)P->dragScale ) ); F( CLR_CL, (float)( cfg.lift * (double)P->liftScale ) );
	U( CLR_RESETPROJ, P->resetProject != 0 );
	U( CLR_RESETRE, ( P->trackContacts && P->reacquire && ( P->reacquire == 2 || fr->nIt == 0 ) ) ? 1 : 0 );
	U( CLR_SIDED, P->sidedContacts != 0 ); F( CLR_SIDEDEPTH, P->sideDepth );
	U( CLR_CONEBODY, P->coneBody != 0 && P->flareCone > 0 );
	F( CLR_CONEKR, P->coneRelax > 0 ? (float)( 1.0 - exp( -(double)fr->h / (double)P->coneRelax ) ) : 1.0f );
	U( CLR_DEBUG, (uint32_t)r_clothGpuSkip->integer );
	for ( int k = 0; k < 12; k++ ) F( CLR_G + k, G[k] );
	for ( int k = 0; k < T.nCaps * 6; k++ ) { F( CLR_CAPOLD + k, capOld[k] ); F( CLR_CAPNEW + k, capNew[k] ); }
	memcpy( &r[CLR_MATS], mats, sizeof( float ) * 16 * ci->asset->a.nBones );
	return true;
}

void R_ClothRBGpuDraw( clothInst_t *ci, CBoneCache *bc, float t, int relZ, int relA, int relB, float s, bool lead2,
	const float *Fc ) {
	clothVkInst_t *vi = (clothVkInst_t *)ci->rb;
	if ( !vi || !vi->buf ) return;
	float inv[3][3], ot[3];
	if ( !Cloth_VkInvertBone( R_ClothEvalBone( bc, vi->refGla ), inv, ot ) ) return;
	const float iu = (float)( 1.0 / ci->asset->a.unitCm );
	uint32_t *d = vi->drawRec;
	auto F = [&]( int k, float v ) { d[k] = Cloth_FBits( v ); };
	F( CLD_T, t );
	d[CLD_RELA] = (uint32_t)( relA % 3 );
	d[CLD_RELB] = (uint32_t)( relB % 3 );
	d[CLD_RELZ] = (uint32_t)( relZ % 3 );
	F( CLD_S, s );
	d[CLD_LEAD2] = lead2 ? 1u : 0u;
	// bind = Ainv ( (rel Fc + Tc) / unitCm ) + ot
	for ( int r = 0; r < 3; r++ ) {
		for ( int k = 0; k < 3; k++ ) {
			F( CLD_K + r * 4 + k, iu * ( inv[r][0] * Fc[k * 4] + inv[r][1] * Fc[k * 4 + 1] + inv[r][2] * Fc[k * 4 + 2] ) );
		}
		F( CLD_K + r * 4 + 3, iu * ( inv[r][0] * Fc[12] + inv[r][1] * Fc[13] + inv[r][2] * Fc[14] ) + ot[r] );
	}
	d[CLD_DEBUG] = (uint32_t)r_clothDebug->integer;
	// r_clothGpuPinSkin 1: pinned particles skinned with this frame's matrices, as the CPU draw does;
	// A = the reference bone's inverse (bind = A model), the same transform K composes with the pelvis frame
	const int nb = ci->asset->a.nBones;
	const bool pinSkin = r_clothGpuPinSkin->integer && nb > 0 && nb <= 256 && (int)ci->L.size() >= nb * 16;
	for ( int r = 0; r < 3; r++ ) {
		for ( int k = 0; k < 3; k++ ) F( CLD_A + r * 4 + k, inv[r][k] );
		F( CLD_A + r * 4 + 3, ot[r] );
	}
	F( CLD_IU, iu );
	d[CLD_NB] = pinSkin ? (uint32_t)nb : 0u;
	if ( pinSkin ) memcpy( d + CLD_MATS, &ci->L[0], (size_t)nb * 16 * sizeof( float ) );
	vi->drawWords = CLD_WORDS + ( pinSkin ? (uint32_t)nb * 16u : 0u );
	vi->drawPending = true;
}

/* ------------------------------------------------------------------ GPU solver: back end */

static bool Cloth_VkSlotCapacity( clothVkSlot_t &s, uint32_t words ) {
	if ( s.buf && s.words >= words ) return true;
	if ( s.ptr ) qvkUnmapMemory( vk.device, s.mem );
	if ( s.buf ) VK_DESTROY_BUFFER( vk.device, s.buf );
	if ( s.mem ) VK_FREE_MEMORY( vk.device, s.mem );
	if ( s.dev ) VK_DESTROY_BUFFER( vk.device, s.dev );
	if ( s.devMem ) VK_FREE_MEMORY( vk.device, s.devMem );
	s.buf = VK_NULL_HANDLE; s.mem = VK_NULL_HANDLE; s.dev = VK_NULL_HANDLE; s.devMem = VK_NULL_HANDLE; s.ptr = NULL; s.words = 0;
	uint32_t want = 65536;
	while ( want < words ) want *= 2;
	if ( !Cloth_VkCreateBuffer( (VkDeviceSize)want * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &s.buf, &s.mem, "cloth frame records" ) ) return false;
	if ( !Cloth_VkCreateBuffer( (VkDeviceSize)want * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &s.dev, &s.devMem, "cloth frame records (device)" ) ) return false;
	void *p;
	if ( qvkMapMemory( vk.device, s.mem, 0, VK_WHOLE_SIZE, 0, &p ) != VK_SUCCESS ) return false;
	s.ptr = (uint32_t *)p;
	// capacity is advertised only once the descriptor set is written too: a failed allocation leaves words 0, so the
	// next frame rebuilds and retries instead of binding a null set
	if ( !s.set ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = cg.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &cg.frameLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &s.set ) != VK_SUCCESS ) { s.set = VK_NULL_HANDLE; return false; }
	}
	VkDescriptorBufferInfo bi;
	bi.buffer = s.dev;
	bi.offset = 0;
	bi.range = VK_WHOLE_SIZE;
	VkWriteDescriptorSet wr[2];
	for ( int i = 0; i < 2; i++ ) {
		Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
		wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wr[i].dstSet = s.set;
		wr[i].dstBinding = i;
		wr[i].descriptorCount = 1;
		wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		wr[i].pBufferInfo = &bi;
	}
	qvkUpdateDescriptorSets( vk.device, 2, wr, 0, NULL );
	s.words = want;
	return true;
}

static void Cloth_VkBarrier( VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst, VkAccessFlags dstAccess ) {
	VkMemoryBarrier b;
	b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	b.pNext = NULL;
	b.srcAccessMask = srcAccess;
	b.dstAccessMask = dstAccess;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer, src, dst, 0, 1, &b, 0, NULL, 0, NULL );
}

static void Cloth_VkComputeBarrier( void ) {
	Cloth_VkBarrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
}

// one dispatch of cloth_step.comp / cloth_draw.comp
struct clothVkStage_t {
	uint32_t rec, stage, sub, count;
};

enum { CLOTH_PIPE_WIDE, CLOTH_PIPE_SINGLE, CLOTH_PIPE_DRAW };

static int Cloth_VkStagePipe( uint32_t stage ) {
	return stage >= 20 ? CLOTH_PIPE_DRAW : stage == 5 ? CLOTH_PIPE_SINGLE : CLOTH_PIPE_WIDE;
}

// the stages of one step or reset (cloth_step.comp), in order
static void Cloth_VkStepStages( const clothVkInst_t *vi, uint32_t rec, bool reset, uint32_t nSub, bool resetProject, std::vector<clothVkStage_t> &out ) {
	const uint32_t N = vi->N, M = vi->M, NT = vi->NT, D = vi->NDYN;
	const uint32_t NM = N > M ? N : M;
	const uint32_t hash = M ? CL_HASH : 0;
	auto add = [&]( uint32_t stage, uint32_t sub, uint32_t count ) {
		clothVkStage_t e = { rec, stage, sub, count };
		out.push_back( e );
	};
	if ( reset ) {
		add( 10, 0, NM );
		add( 11, 0, NM > hash ? NM : hash );
		if ( M ) {
			add( 12, 0, D > M ? D : M );
			if ( resetProject ) {
				add( 4, 0, D );
				add( 14, 0, D );
			}
		}
		add( 7, 0, NM );
		return;
	}
	add( 0, 0, NM );
	for ( uint32_t s = 1; s <= nSub; s++ ) {
		add( 1, s, NM > hash ? NM : hash );
		add( 2, s, NT > M ? NT : M );
		add( 3, s, N );
		if ( M ) add( 4, s, D );
		add( 5, s, 0 );
		add( 6, s, D );
	}
	add( 7, 0, NM );
}

// vk_begin_frame, after the frame's command buffer began and before any render pass: read back the slot's last
// statistics, then record this frame's cloth steps and draws
void R_ClothVkBeginFrame( void ) {
	clothVkFrame++;
	if ( !cg.ok ) return;
	Cloth_VkDestroyDead( false );
	clothVkSlot_t &slot = cg.slot[vk.cmd_index];
	if ( slot.ptr && ( !slot.statsAt.empty() || slot.timed ) ) {
		int sum[CLS_WORDS] = { 0 };
		for ( size_t i = 0; i < slot.statsAt.size(); i++ ) {
			for ( int k = 0; k < CLS_WORDS; k++ ) sum[k] += (int)slot.ptr[slot.statsAt[i] + k];
		}
		double ms = 0;
		if ( slot.timed ) {
			uint64_t ts[2];
			if ( cg.getQueryPoolResults( vk.device, cg.queries, vk.cmd_index * 2, 2, sizeof( ts ), ts, sizeof( uint64_t ), VK_QUERY_RESULT_64_BIT ) == VK_SUCCESS
				&& ts[1] >= ts[0] ) {
				ms = (double)( ts[1] - ts[0] ) * cg.nsPerTick * 1e-6;
			}
		}
		R_ClothAddStepStats( sum[CLS_CONTACTS], sum[CLS_HAND], sum[CLS_CONE], sum[CLS_BAD], ms );
	}
	slot.statsAt.clear();
	slot.timed = false;
	slot.steps = 0;

	// this frame's work
	uint32_t words = 0;
	bool any = false;
	for ( size_t i = 0; i < cg.live.size(); i++ ) {
		clothVkInst_t *vi = cg.live[i];
		const uint32_t steps = vi->recWords ? (uint32_t)( vi->pending.size() / vi->recWords ) : 0;
		words += steps * ( vi->recWords + CLS_WORDS ) + ( vi->drawPending ? vi->drawWords : 0 );
		any |= steps > 0 || vi->drawPending;
	}
	if ( !any ) return;
	if ( !Cloth_VkSlotCapacity( slot, words ) ) {
		for ( size_t i = 0; i < cg.live.size(); i++ ) { cg.live[i]->pending.clear(); cg.live[i]->drawPending = false; }
		return;
	}
	// records into the slot buffer
	// the per-frame lists keep their storage across frames (assigned or cleared, not reallocated)
	static std::vector<uint32_t> recAt, drawAt;
	recAt.assign( cg.live.size(), 0 );
	drawAt.assign( cg.live.size(), 0 );
	uint32_t at = 0;
	for ( size_t i = 0; i < cg.live.size(); i++ ) {
		clothVkInst_t *vi = cg.live[i];
		const uint32_t steps = vi->recWords ? (uint32_t)( vi->pending.size() / vi->recWords ) : 0;
		recAt[i] = at;
		for ( uint32_t s = 0; s < steps; s++ ) {
			uint32_t *r = slot.ptr + at;
			memcpy( r, &vi->pending[(size_t)s * vi->recWords], vi->recWords * 4 );
			r[CLR_STATS] = at + vi->recWords;
			memset( slot.ptr + at + vi->recWords, 0, CLS_WORDS * 4 );
			slot.statsAt.push_back( at + vi->recWords );
			at += vi->recWords + CLS_WORDS;
		}
		drawAt[i] = at;
		if ( vi->drawPending ) {
			memcpy( slot.ptr + at, vi->drawRec, vi->drawWords * 4 );
			at += vi->drawWords;
		}
	}

	VkCommandBuffer cb = vk.cmd->command_buffer;
	if ( cg.queries ) {
		cg.cmdResetQueryPool( cb, cg.queries, vk.cmd_index * 2, 2 );
		// bottom of pipe: written once everything recorded before it is done, so the interval holds only the cloth work
		cg.cmdWriteTimestamp( cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, cg.queries, vk.cmd_index * 2 );
	}
	// the records into device-local memory (the kernels read them many times)
	{
		VkBufferCopy region;
		region.srcOffset = region.dstOffset = 0;
		region.size = (VkDeviceSize)at * 4;
		qvkCmdCopyBuffer( cb, slot.buf, slot.dev, 1, &region );
	}
	// the previous frame's draws read the render buffers; its steps wrote the state; the records just arrived
	Cloth_VkBarrier( VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
	// every instance's stage sequence (its steps, then its draw); the instances advance in rounds, one stage
	// each per round, so one barrier serves all of them and their dispatches overlap on the device
	static std::vector<std::vector<clothVkStage_t> > seq;
	if ( seq.size() < cg.live.size() ) seq.resize( cg.live.size() );
	for ( size_t i = 0; i < cg.live.size(); i++ ) seq[i].clear();
	size_t rounds = 0;
	for ( size_t i = 0; i < cg.live.size(); i++ ) {
		clothVkInst_t *vi = cg.live[i];
		const uint32_t steps = vi->recWords ? (uint32_t)( vi->pending.size() / vi->recWords ) : 0;
		for ( uint32_t s = 0; s < steps; s++ ) {
			const uint32_t rec = recAt[i] + s * ( vi->recWords + CLS_WORDS );
			const uint32_t *r = slot.ptr + rec;
			Cloth_VkStepStages( vi, rec, r[CLR_RESET] != 0, r[CLR_NSUB], r[CLR_RESETPROJ] != 0, seq[i] );
			slot.steps++;
		}
		if ( vi->drawPending ) {
			const uint32_t counts[3] = { vi->N, vi->N, vi->RD };
			for ( uint32_t st = 0; st < 3; st++ ) {
				clothVkStage_t e = { drawAt[i], 20 + st, 0, counts[st] };
				seq[i].push_back( e );
			}
			vi->drawnFrame = clothVkFrame;
		}
		if ( seq[i].size() > rounds ) rounds = seq[i].size();
	}
	VkPipeline bound = VK_NULL_HANDLE;
	for ( size_t r = 0; r < rounds; r++ ) {
		for ( size_t i = 0; i < cg.live.size(); i++ ) {
			if ( r >= seq[i].size() ) continue;
			const clothVkStage_t &e = seq[i][r];
			const int kind = Cloth_VkStagePipe( e.stage );
			const VkPipeline pipe = kind == CLOTH_PIPE_DRAW ? cg.draw : kind == CLOTH_PIPE_SINGLE ? cg.stepSingle : cg.stepWide;
			if ( pipe != bound ) {
				qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe );
				bound = pipe;
			}
			VkDescriptorSet sets[2] = { cg.live[i]->set, slot.set };
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, cg.layout, 0, 2, sets, 0, NULL );
			const uint32_t pc[4] = { e.rec, e.stage, e.sub, 0 };
			qvkCmdPushConstants( cb, cg.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
			uint32_t groups = kind == CLOTH_PIPE_SINGLE ? 1 : ( e.count + cg.wg - 1 ) / cg.wg;
			if ( groups < 1 ) groups = 1;
			qvkCmdDispatch( cb, groups, 1, 1 );
		}
		if ( r + 1 < rounds ) Cloth_VkComputeBarrier();
	}
	// render vertices -> this frame's draws; statistics -> the host (read when the slot comes round again)
	Cloth_VkBarrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT );
	{
		VkBufferCopy region;
		region.srcOffset = region.dstOffset = 0;
		region.size = (VkDeviceSize)at * 4;
		qvkCmdCopyBuffer( cb, slot.dev, slot.buf, 1, &region );
		Cloth_VkBarrier( VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT );
	}
	if ( cg.queries ) {
		cg.cmdWriteTimestamp( cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, cg.queries, vk.cmd_index * 2 + 1 );
		slot.timed = true;
	}
	for ( size_t i = 0; i < cg.live.size(); i++ ) {
		cg.live[i]->pending.clear();
		cg.live[i]->drawPending = false;
	}
}

/* ------------------------------------------------------------------ draw substitution */

qboolean RB_ClothVkSurface( CBoneCache *bc, const mdxmSurface_t *surface, clothVkDraw_t *out ) {
	if ( !r_cloth || !r_cloth->integer || !bc || !surface ) return qfalse;
	clothInst_t *ci = R_ClothFindInst( bc );
	if ( !ci || !ci->ready ) return qfalse;
	clothModelAsset_t *as = ci->asset;
	const int idx = surface->thisSurfaceIndex;
	if ( idx < 0 || idx >= (int)as->surfToRS.size() ) return qfalse;
	const int rs = as->surfToRS[idx];
	if ( rs < 0 || as->rsLod0[rs] != surface ) return qfalse;  // LOD 0 only
	clothVkAsset_t *va = Cloth_VkAsset( as );
	if ( !va ) return qfalse;
	clothVkInst_t *vi = Cloth_VkInst( ci );
	if ( ci->gpu ) {
		// written by this frame's draw dispatch; else the garment draws skinned (a live instance the front end did
		// not reach this frame: earlier, one that only a shadow view drew)
		if ( !vi->buf || vi->drawnFrame != clothVkFrame ) {
			R_SimCheckFallback( SIMCHECK_CLOTH, backEnd.viewParms.frameCount );
			return qfalse;
		}
	}
	// only data the front end built for this frame (rd-common/tr_sim.h): never an earlier frame's
	if ( !R_SimDrawable( SIMCHECK_CLOTH, true, &ci->gate, backEnd.viewParms.frameCount ) ) return qfalse;
	if ( ci->gpu ) {
		out->vbo = &vi->gvbo;
		out->ibo = &vi->gibo;
		out->indexOffset = (int)( vi->rvIndexOffset / sizeof( uint32_t ) + va->idxStart[rs] );
	} else {
		if ( vi->frame != clothVkFrame || vi->cmd != vk.cmd || vi->drawSerial != ci->drawSerial ) {
			vi->frame = clothVkFrame;
			vi->cmd = vk.cmd;
			vi->drawSerial = ci->drawSerial;
			vi->valid = Cloth_VkUpload( ci, va, vi, bc );
		}
		if ( !vi->valid ) return qfalse;
		out->vbo = &vi->vbo;
		out->ibo = &vi->ibo;
		out->indexOffset = (int)( vi->indexBase + va->idxStart[rs] );
	}
	out->numIndexes = va->idxCount[rs];
	out->minIndex = as->rsStart[rs];
	out->maxIndex = as->rsStart[rs] + as->rsCount[rs] - 1;
	out->numVertexes = as->rsCount[rs];
	return qtrue;
}

// qtrue if this LOD 0 surface is drawn from a live cloth instance this frame (no upload, unlike RB_ClothVkSurface)
qboolean RB_ClothVkIsSimSurface( CBoneCache *bc, const mdxmSurface_t *surface ) {
	if ( !r_cloth || !r_cloth->integer || !bc || !surface ) return qfalse;
	clothInst_t *ci = R_ClothFindInst( bc );
	if ( !ci || !ci->ready ) return qfalse;
	if ( R_SimPerFrame() && !Sim_GateFrame( &ci->gate, backEnd.viewParms.frameCount ) ) return qfalse;
	clothModelAsset_t *as = ci->asset;
	const int idx = surface->thisSurfaceIndex;
	if ( idx < 0 || idx >= (int)as->surfToRS.size() ) return qfalse;
	const int rs = as->surfToRS[idx];
	return rs >= 0 && as->rsLod0[rs] == surface ? qtrue : qfalse;
}

/* ------------------------------------------------------------------ registration */

void R_ClothRBRegister( void ) {
	r_clothGpu = ri.Cvar_Get( "r_clothGpu", "1", CVAR_ARCHIVE_ND, "Simulate garment cloth in compute shaders (0 = the CPU solver)" );
	r_clothGpuWG = ri.Cvar_Get( "r_clothGpuWG", "1024", CVAR_LATCH, "Cloth compute: invocations of the constraint-iteration workgroup (one per instance)" );
	r_clothGpuPinSkin = ri.Cvar_Get( "r_clothGpuPinSkin", "1", CVAR_NONE, "GPU cloth draw: 1 pinned particles placed with this frame's pose, as the CPU solver draws them, so a render vertex wrapped on them follows the body; 0 blended from the step states" );
	r_clothDebug = ri.Cvar_Get( "r_clothDebug", "0", CVAR_NONE, "Debug, GPU cloth: 1 hides the inner (reversed) shell of a garment, 2 hides the outer" );
	r_clothGpuSkip = ri.Cvar_Get( "r_clothGpuSkip", "0", CVAR_NONE, "Debug: skip GPU cloth phases (1 contact search, 2 constraints, 4 particle pass, 8 aero)" );
}

void R_ClothRBShutdown( void ) {
	Cloth_VkShutdownGpu();
}
