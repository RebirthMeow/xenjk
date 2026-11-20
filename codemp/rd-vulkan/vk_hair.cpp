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
Hair chains: rd-vulkan back end. See rd-common/tr_hair.h.

rd-vulkan skins every Ghoul2 surface on the GPU from the model's static per-LOD VBO (R_BuildMDXM) with the bone
palette uploaded per bone cache (vkUniformBones_t). In a model with hair, the listed vertices' bone references point
at palette slots after the skeleton's own bones (numGlaBones + hair bone) with the sidecar's weights, their host share
staying on the host bone (with HAIR_ASSET_OWN_SKIN, spread over the vertex's own bones instead, so a dress strip
partly skinned to the thighs keeps following them). The palette slots hold the hair bones' skinning matrices, which
the front end (tr_hair.cpp) leaves on the instance each frame, or the host bones' matrices when the hair does not
simulate. So a simulated hair surface keeps the whole pipeline (lighting, PBR, sun and character shadows, depth
prepass, the hair opacity map) and costs the asset's hair bones' matrices per character per frame (81 for Tifa).

r_hairSharedVbo 1 (default): the listed vertices' bone references and weights are written into the model's own LOD
buffer, so the hair draws from it with no other buffer. r_hairSharedVbo 0: each listed LOD draws its listed surfaces
from a substitute static VBO, the LOD's vertices packed exactly as R_BuildMDXM packs them with the listed ones
re-pointed (one more buffer per LOD per model); the model's own index buffer and surface offsets are reused.
*/
#include "tr_local.h"
#include "rd-common/tr_hair_local.h"
#include "vk_hair.h"

// the Ghoul2 vertex's bone refs and weights (4 bytes each, adjacent): tr_local.h's G2_VERT_OFS_*
static_assert( G2_VERT_OFS_WEIGHTS == G2_VERT_OFS_BONES + 4, "the hair patch writes the refs and weights as one 8-byte range" );

#include <string.h>

// 1 the listed vertices are patched into the model's own LOD buffer and the hair palette slots are always
// uploaded (the hair bones' host matrices when not simulating); 0 a substitute copy of each listed LOD (R_HairRBRegister)
static cvar_t *r_hairSharedVbo;

struct hairVkLod_t {
	bool tried;
	bool ok;
	bool shared;     // patched into the model's own LOD buffer (no substitute copy)
	VkBuffer buf;
	VkDeviceMemory mem;
	VBO_t vbo;
};

struct hairVkAsset_t {
	const model_t *mod;
	int base;      // palette slot of hair bone 0 (the skeleton's bone count) the buffers were built for
	int hostGla;
	std::vector<int> gameGla;   // the asset's game bones in that skeleton (HAIR_ASSET_GAME_REFS entries)
	std::vector<hairVkLod_t> lods;
};

// a render entry's bone as a palette slot: hair bone, game bone (<= -2), or -1 = the host bone
// the hair bone count of a model whose own buffer carries hair refs (0 when none, or when it was patched for
// another skeleton size; numGlaBones -1 = any), with its GPU record
static int Hair_VkSharedBones( const CBoneCache *bc, int numGlaBones, const struct hairVkAsset_t **out );

static int Hair_VkRef( const hairVkAsset_t *va, int bone ) {
	if ( bone >= 0 ) return va->base + bone;
	if ( bone <= -2 && -2 - bone < (int)va->gameGla.size() ) return va->gameGla[-2 - bone];
	return va->hostGla;
}

static bool Hair_VkCreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem, const char *name ) {
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
	if ( vk_vram_allocate_named( vk.device, &alloc, mem, "hair" ) != VK_SUCCESS ) {
		qvkDestroyBuffer( vk.device, *buf, NULL );
		*buf = VK_NULL_HANDLE;
		return false;
	}
	qvkBindBufferMemory( vk.device, *buf, *mem, 0 );
	VK_SET_OBJECT_NAME( *buf, name, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	return true;
}

// host data into the start of a device-local buffer: vk_upload.cpp's ring (no wait), else a one-shot copy
static bool Hair_VkUpload( VkBuffer dst, const void *data, VkDeviceSize size ) {
	if ( vk_upload_buffer( dst, 0, data, size ) ) return true;
	VkBuffer staging;
	VkDeviceMemory mem;
	if ( !Hair_VkCreateBuffer( size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&staging, &mem, "hair staging" ) ) return false;
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

// as vk_vbo.cpp's (R_BuildMDXM packs weights with it)
static inline float Hair_VkWeight( const mdxmVertex_t *vert, const int index ) {
	int weight = vert->BoneWeightings[index];
	weight |= ( vert->uiNmWeightsAndBoneIndexes >> ( iG2_BONEWEIGHT_TOPBITS_SHIFT + ( index * 2 ) ) ) & iG2_BONEWEIGHT_TOPBITS_AND;
	return fG2_BONEWEIGHT_RECIPROCAL_MULT * weight;
}

static void Hair_VkFreeLods( hairVkAsset_t *va ) {
	bool any = false;
	for ( size_t l = 0; l < va->lods.size(); l++ ) any = any || va->lods[l].buf != VK_NULL_HANDLE;
	if ( any && vk.device ) {   // rare: a glm rebind or shutdown
		vk_upload_flush();
		qvkDeviceWaitIdle( vk.device );
	}
	for ( size_t l = 0; l < va->lods.size(); l++ ) {
		hairVkLod_t &L = va->lods[l];
		if ( L.buf ) VK_DESTROY_BUFFER( vk.device, L.buf );
		if ( L.mem ) VK_FREE_MEMORY( vk.device, L.mem );
		L.buf = VK_NULL_HANDLE;
		L.mem = VK_NULL_HANDLE;
		L.tried = L.ok = false;
	}
}

void R_HairRBFreeAsset( hairModelAsset_t *as ) {
	hairVkAsset_t *va = (hairVkAsset_t *)as->rb;
	if ( !va ) return;
	Hair_VkFreeLods( va );
	delete va;
	as->rb = NULL;
}

void R_HairRBShutdown( void ) {
}

void R_HairRBRegister( void ) {
	r_hairSharedVbo = ri.Cvar_Get( "r_hairSharedVbo", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, "Hair vertices patched into the model's own vertex buffer (1) or drawn from a copy of each listed LOD (0: one more vertex buffer per LOD, 44 bytes a vertex)" );
}

// the LOD's vertex buffer exactly as R_BuildMDXM packs it, then the listed vertices re-pointed at the hair palette
static bool Hair_VkBuildLod( const hairModelAsset_t *as, const hairVkAsset_t *va, int lodIndex, hairVkLod_t &out ) {
	const mdxmHeader_t *mdxm = as->boundMod->data.glm->header;
	const int stride = G2_VERT_STRIDE;   // the packed vertex (tr_local.h G2_VERT_*, get_mdxm_stride)
	if ( get_mdxm_stride() != stride ) return false;
	const mdxmLOD_t *lod = (const mdxmLOD_t *)( (const byte *)mdxm + mdxm->ofsLODs );
	for ( int l = 0; l < lodIndex; l++ ) lod = (const mdxmLOD_t *)( (const byte *)lod + lod->ofsEnd );
	const mdxmSurface_t *first = (const mdxmSurface_t *)( (const byte *)lod + sizeof( mdxmLOD_t ) + mdxm->numSurfaces * sizeof( mdxmLODSurfOffset_t ) );

	std::vector<int> baseVertex( mdxm->numSurfaces + 1, 0 );
	int numVerts = 0;
	const mdxmSurface_t *surf = first;
	for ( int n = 0; n < mdxm->numSurfaces; n++ ) {
		baseVertex[n] = numVerts;
		numVerts += surf->numVerts;
		surf = (const mdxmSurface_t *)( (const byte *)surf + surf->ofsEnd );
	}
	baseVertex[mdxm->numSurfaces] = numVerts;
	std::vector<byte> data( (size_t)numVerts * stride, 0 );
	// the model's buffer has this packing (same size, refs and weights at G2_VERT_OFS_*): only the listed
	// vertices' bone refs and weights are written into it, so the tangents are not computed for it
	const mdxmVBOModel_t &vm = as->boundMod->data.glm->vboModels[lodIndex];
	const bool shared = r_hairSharedVbo && r_hairSharedVbo->integer && vm.vbo && vm.vbo->buffer
		&& vm.vbo->offsets[10] == G2_VERT_OFS_BONES && vm.vbo->offsets[11] == G2_VERT_OFS_WEIGHTS && vm.vbo->size == (int)data.size();
	// the tangents R_BuildMDXM packed this LOD with (vk_tangent_cache.cpp), else this buffer's own
	const float *prepared = R_MdxmTangents( mdxm, lodIndex, numVerts );
	std::vector<vec4_t> tangents( !prepared && !shared && numVerts > 0 ? numVerts : 1 );
	int checked = 0, differ = 0;

	surf = first;
	for ( int n = 0; n < mdxm->numSurfaces; n++ ) {
		mdxmVertex_t *v = (mdxmVertex_t *)( (byte *)surf + surf->ofsVerts );
		mdxmVertexTexCoord_t *tc = (mdxmVertexTexCoord_t *)( v + surf->numVerts );
		const int *boneRef = (const int *)( (const byte *)surf + surf->ofsBoneReferences );
		if ( !prepared && !shared ) {
			const mdxmTriangle_t *t = (const mdxmTriangle_t *)( (const byte *)surf + surf->ofsTriangles );
			std::vector<glIndex_t> idx( surf->numTriangles * 3 + 1 );
			for ( int k = 0; k < surf->numTriangles; k++ ) {
				for ( int j = 0; j < 3; j++ ) idx[k * 3 + j] = t[k].indexes[j];
			}
			if ( surf->numVerts ) vk_mikkt_mdxm_generate( surf->numTriangles, v, tc, &tangents[baseVertex[n]], &idx[0] );
		} else if ( r_tangentCache->integer == 2 ) {
			checked++;
			if ( !R_TangentCheckSurface( surf, prepared + (size_t)baseVertex[n] * 4 ) ) differ++;
		}
		for ( int k = 0; k < surf->numVerts; k++ ) {
			byte *o = &data[(size_t)( baseVertex[n] + k ) * stride];
			VectorCopy( v[k].vertCoords, (float *)o );
			R_PackSnorm16x4( v[k].normal, 0.0f, (short *)( o + G2_VERT_OFS_NRM ) );
			( (float *)( o + G2_VERT_OFS_ST ) )[0] = tc[k].texCoords[0];
			( (float *)( o + G2_VERT_OFS_ST ) )[1] = tc[k].texCoords[1];
			byte *refs = o + G2_VERT_OFS_BONES, *wts = o + G2_VERT_OFS_WEIGHTS;
			const int numWeights = G2_GetVertWeights( &v[k] );
			int lastWeight = 255;
			const int last = numWeights - 1;
			for ( int w = 0; w < last; w++ ) {
				wts[w] = (byte)( Hair_VkWeight( &v[k], w ) * 255.0f );
				refs[w] = (byte)boneRef[G2_GetVertBoneIndex( &v[k], w )];
				lastWeight -= wts[w];
			}
			wts[last] = (byte)lastWeight;
			refs[last] = (byte)boneRef[G2_GetVertBoneIndex( &v[k], last )];
			for ( int w = numWeights; w < 4; w++ ) {
				wts[w] = 0;
				refs[w] = 0;
			}
			if ( prepared || !shared ) {
				const float *tangent = prepared ? prepared + (size_t)( baseVertex[n] + k ) * 4 : tangents[baseVertex[n] + k];
				R_PackSnorm16x4( tangent, tangent[3], (short *)( o + G2_VERT_OFS_TAN ) );
			}
		}
		surf = (const mdxmSurface_t *)( (const byte *)surf + surf->ofsEnd );
	}

	// the listed vertices: hair bones at base + bone, the host share on the host bone
	const hairAsset_t &h = as->h;
	const bool ownSkin = ( h.a.flags & HAIR_ASSET_OWN_SKIN ) != 0;
	int patched = 0;
	for ( int i = 0; i < h.nRenderEntries; i++ ) {
		const hairRenderEntry_t &e = h.renderEntries[i];
		if ( h.renderSurfs[e.surf].lod != lodIndex ) continue;
		const int s = as->rsSurf[e.surf];
		if ( s < 0 ) continue;
		byte *o = &data[(size_t)( baseVertex[s] + e.vert ) * stride];
		byte *refs = o + G2_VERT_OFS_BONES, *wts = o + G2_VERT_OFS_WEIGHTS;
		if ( ownSkin ) {
			// the bone -1 share follows the vertex's own skinning (a dress strip partly on the thighs): merge the
			// hair bones with the vertex's own bones scaled by that share, keep the four heaviest, sum to 255
			int ref[8];
			float w[8];
			int n = 0;
			float own = 0.0f;
			for ( int k = 0; k < 4; k++ ) {
				const float ew = e.w[k] > 0.0f ? e.w[k] : 0.0f;
				if ( ew <= 0.0f ) continue;
				if ( e.bone[k] == -1 ) { own += ew; continue; }
				const int r = Hair_VkRef( va, e.bone[k] );
				int m = 0;
				while ( m < n && ref[m] != r ) m++;
				if ( m < n ) w[m] += ew;
				else { ref[n] = r; w[n++] = ew; }
			}
			for ( int k = 0; k < 4 && own > 0.0f; k++ ) {
				if ( !wts[k] ) continue;
				const float ow = own * wts[k] / 255.0f;
				int m = 0;
				while ( m < n && ref[m] != refs[k] ) m++;
				if ( m < n ) w[m] += ow;
				else { ref[n] = refs[k]; w[n++] = ow; }
			}
			for ( int a = 0; a < n; a++ ) {   // heaviest first
				for ( int b = a + 1; b < n; b++ ) {
					if ( w[b] > w[a] ) {
						const float tw = w[a]; w[a] = w[b]; w[b] = tw;
						const int tr = ref[a]; ref[a] = ref[b]; ref[b] = tr;
					}
				}
			}
			if ( n > 4 ) n = 4;
			float total = 0.0f;
			for ( int k = 0; k < n; k++ ) total += w[k];
			if ( n == 0 || total <= 0.0f ) continue;   // no weight: keep the vertex's own skinning
			int sum = 0;
			for ( int k = 0; k < 4; k++ ) {
				wts[k] = k < n ? (byte)( w[k] / total * 255.0f + 0.5f ) : 0;
				refs[k] = k < n ? (byte)ref[k] : 0;
				sum += wts[k];
			}
			wts[0] = (byte)( wts[0] + ( 255 - sum ) );   // the heaviest takes the rounding
			patched++;
			continue;
		}
		// hair_asset.c checks each weight to 0..1, not their sum: a sum well over 1 would wrap the rounding below
		float total = 0.0f;
		for ( int k = 0; k < 4; k++ ) total += e.w[k] > 0.0f ? e.w[k] : 0.0f;
		const float norm = total > 1.01f ? 1.0f / total : 1.0f;
		int sum = 0, big = 0;
		for ( int k = 0; k < 4; k++ ) {
			const float w = e.w[k] > 0.0f ? e.w[k] * norm : 0.0f;
			wts[k] = (byte)( w * 255.0f + 0.5f );
			refs[k] = (byte)( w > 0.0f ? Hair_VkRef( va, e.bone[k] ) : 0 );
			sum += wts[k];
			if ( wts[k] > wts[big] ) big = k;
		}
		wts[big] = (byte)( wts[big] + ( 255 - sum ) );   // the weights sum to 255, as R_BuildMDXM's do
		patched++;
	}

	if ( shared ) {
		// write the listed vertices' bone refs and weights (8 bytes each) into the model's buffer; the
		// draw then needs no other buffer
		int written = 0;
		for ( int i = 0; i < h.nRenderEntries; i++ ) {
			const hairRenderEntry_t &e = h.renderEntries[i];
			if ( h.renderSurfs[e.surf].lod != lodIndex ) continue;
			const int s = as->rsSurf[e.surf];
			if ( s < 0 ) continue;
			const size_t at = (size_t)( baseVertex[s] + e.vert ) * stride + G2_VERT_OFS_BONES;
			if ( !vk_upload_buffer( vm.vbo->buffer, (VkDeviceSize)at, &data[at], 8 ) ) {
				ri.Printf( PRINT_WARNING, "hair: %s LOD %d: could not patch the model's buffer (%d vertices written)\n", as->name, lodIndex, written );
				// the vertices already written point at the hair palette slots: keep the LOD shared so the slots are
				// uploaded (the rest keep their own skinning); with none written the model's buffer is untouched
				if ( !written ) return false;
				break;
			}
			written++;
		}
		out.shared = true;
		out.buf = VK_NULL_HANDLE;
		out.mem = VK_NULL_HANDLE;
		ri.Printf( PRINT_DEVELOPER, "hair: %s LOD %d: %d vertices patched into the model's buffer\n", as->name, lodIndex, written );
		return true;
	}
	out.shared = false;
	const VkDeviceSize size = (VkDeviceSize)data.size();
	if ( !size || !Hair_VkCreateBuffer( size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &out.buf, &out.mem, "hair vbo" ) ) return false;
	if ( !Hair_VkUpload( out.buf, &data[0], size ) ) {
		vk_upload_finish();   // pieces of it may be recorded already
		VK_DESTROY_BUFFER( vk.device, out.buf );
		VK_FREE_MEMORY( vk.device, out.mem );
		out.buf = VK_NULL_HANDLE;
		out.mem = VK_NULL_HANDLE;
		return false;
	}
	memset( &out.vbo, 0, sizeof( out.vbo ) );
	out.vbo.buffer = out.buf;
	out.vbo.memory = out.mem;
	out.vbo.offsets[0] = G2_VERT_OFS_POS;
	out.vbo.offsets[5] = G2_VERT_OFS_NRM;
	out.vbo.offsets[2] = G2_VERT_OFS_ST;
	out.vbo.offsets[10] = G2_VERT_OFS_BONES;
	out.vbo.offsets[11] = G2_VERT_OFS_WEIGHTS;
	out.vbo.offsets[8] = G2_VERT_OFS_TAN;
	out.vbo.size = (int)size;
	ri.Printf( PRINT_DEVELOPER, "hair: %s LOD %d: substitute buffer, %d verts, %d on hair bones\n", as->name, lodIndex, numVerts, patched );
	if ( checked ) ri.Printf( PRINT_ALL, "tangent cache: hair %s LOD %d: %d surfaces checked, %d differ\n", as->name, lodIndex, checked, differ );
	return true;
}

static int Hair_VkSharedBones( const CBoneCache *bc, int numGlaBones, const hairVkAsset_t **out ) {
	const model_t *mod = R_BoneCacheModel( bc );
	const hairModelAsset_t *as = mod ? R_HairAssetIfLoaded( mod ) : NULL;
	const hairVkAsset_t *va = as ? (const hairVkAsset_t *)as->rb : NULL;
	if ( !va || va->mod != mod || ( numGlaBones >= 0 && va->base != numGlaBones ) ) return 0;
	bool any = false;
	for ( size_t l = 0; l < va->lods.size() && !any; l++ ) any = va->lods[l].ok && va->lods[l].shared;
	if ( !any ) return 0;
	if ( out ) *out = va;
	return as->h.a.nBones;
}

// The instance whose palette this frame draws with. Only a palette the front end built for the frame being drawn
// (rd-common/tr_sim.h): one left from the last frame a character was on screen is never used.
static const hairInst_t *Hair_VkReadyInst( const CBoneCache *bc ) {
	if ( !r_hair || !r_hair->integer || !bc ) return NULL;
	const hairInst_t *hi = R_HairFindInst( bc );
	if ( !hi || !hi->ready || !hi->asset || !hi->asset->bindOk ) return NULL;
	if ( R_SimPerFrame() && !Sim_GateFrame( &hi->gate, backEnd.viewParms.frameCount ) ) return NULL;
	return hi;
}

int RB_HairVkPaletteCount( const CBoneCache *bc, int numGlaBones ) {
	if ( R_SimCheckOn() && r_hair && r_hair->integer && bc ) {
		// once per palette upload: count what is used, stale or turned down
		const hairInst_t *any = R_HairFindInst( bc );
		if ( any && any->asset && any->asset->bindOk ) R_SimDrawable( SIMCHECK_HAIR, any->ready, &any->gate, backEnd.viewParms.frameCount );
	}
	const hairInst_t *hi = Hair_VkReadyInst( bc );
	if ( hi && hi->numGlaBones == numGlaBones ) {
		const int n = (int)hi->palette.size();
		return numGlaBones + n <= HAIR_MAX_PALETTE ? n : 0;
	}
	// a model whose own buffer carries hair bone refs draws with them whether or not it simulates
	const int n = Hair_VkSharedBones( bc, numGlaBones, NULL );
	return numGlaBones + n <= HAIR_MAX_PALETTE ? n : 0;
}

void RB_HairVkPaletteWrite( const CBoneCache *bc, float *dst, int count ) {
	const hairInst_t *hi = Hair_VkReadyInst( bc );
	if ( !hi ) {
		// not simulating (r_hair 0, out of range, not ready): each hair bone rigid with its host, whose
		// skinning matrix then poses it as bound (H bind bind^-1 = H)
		const hairVkAsset_t *va = NULL;
		const hairModelAsset_t *as = NULL;
		if ( !Hair_VkSharedBones( bc, -1, &va ) || !va || !( as = R_HairAssetIfLoaded( R_BoneCacheModel( bc ) ) ) ) return;
		for ( int b = 0; b < count && b < as->h.a.nBones; b++ ) {
			const int host = as->h.boneHost && as->h.boneHost[b] >= 0 && as->h.boneHost[b] < (int)va->gameGla.size()
				? va->gameGla[as->h.boneHost[b]] : va->hostGla;
			const mdxaBone_t *m = R_ClothEvalBone( (CBoneCache *)bc, host );
			memcpy( dst + b * 12, m->matrix, 12 * sizeof( float ) );
		}
		return;
	}
	for ( int b = 0; b < count && b < (int)hi->palette.size(); b++ ) memcpy( dst + b * 12, hi->palette[b].matrix, 12 * sizeof( float ) );
}

// r_charOccRect: extend a model-space box by the hair bones' posed joints. Each palette matrix (3x4 rows, as written)
// skins from the bind pose, so a joint poses from its bind position: where its inverse bind matrix (column-major 4x4)
// maps to the origin, -R^-1 t. Without the asset's binds the joints are skipped (the caller's margin stays)
void RB_HairVkJointBox( const CBoneCache *bc, const float *palette, int count, float *mins, float *maxs ) {
	const hairInst_t *hi = Hair_VkReadyInst( bc );
	if ( !hi || !hi->asset || (int)hi->asset->bindInv.size() < count * 16 ) return;
	if ( !palette && (int)hi->palette.size() < count ) return;	// NULL = the instance's CPU palette (what was written)
	for ( int b = 0; b < count; b++ ) {
		const float *I = &hi->asset->bindInv[b * 16];
		// R (column-major): R[r][c] = I[c * 4 + r]; t = I[12..14]; joint = -R^-1 t (3x3 inverse by cofactors)
		const float a00 = I[0], a01 = I[4], a02 = I[8], a10 = I[1], a11 = I[5], a12 = I[9], a20 = I[2], a21 = I[6], a22 = I[10];
		const float c00 = a11 * a22 - a12 * a21, c01 = a02 * a21 - a01 * a22, c02 = a01 * a12 - a02 * a11;
		const float c10 = a12 * a20 - a10 * a22, c11 = a00 * a22 - a02 * a20, c12 = a02 * a10 - a00 * a12;
		const float c20 = a10 * a21 - a11 * a20, c21 = a01 * a20 - a00 * a21, c22 = a00 * a11 - a01 * a10;
		const float det = a00 * c00 + a01 * c10 + a02 * c20;
		if ( fabsf( det ) < 1e-12f ) continue;
		const float t0 = I[12], t1 = I[13], t2 = I[14], id = -1.0f / det;
		const float j[3] = { ( c00 * t0 + c01 * t1 + c02 * t2 ) * id, ( c10 * t0 + c11 * t1 + c12 * t2 ) * id,
			( c20 * t0 + c21 * t1 + c22 * t2 ) * id };
		const float *m = palette ? palette + b * 12 : &hi->palette[b].matrix[0][0];
		for ( int k = 0; k < 3; k++ ) {
			const float v = m[k * 4] * j[0] + m[k * 4 + 1] * j[1] + m[k * 4 + 2] * j[2] + m[k * 4 + 3];
			mins[k] = v < mins[k] ? v : mins[k];
			maxs[k] = v > maxs[k] ? v : maxs[k];
		}
	}
}

// front end, once the instance is bound: the substitute buffers of every listed LOD (built once per glm and skeleton)
bool R_HairRBPrepare( hairInst_t *hi ) {
	return R_HairRBPrepareFor( hi->asset, hi->numGlaBones, hi->hostGla, hi->gameGla, 1 << 30 ) == 1;
}

int R_HairRBPrepareFor( hairModelAsset_t *as, int base, int hostGla, const std::vector<int> &gameGla, int maxBuilds ) {
	const model_t *mod = as->boundMod;
	if ( !vk.vboGhoul2Active || !mod || !mod->data.glm || !mod->data.glm->vboModels ) return 0;
	hairVkAsset_t *va = (hairVkAsset_t *)as->rb;
	if ( va && ( va->mod != mod || va->base != base || va->hostGla != hostGla || va->gameGla != gameGla ) ) {
		R_HairRBFreeAsset( as );
		va = NULL;
	}
	if ( !va ) {
		va = new hairVkAsset_t();
		va->mod = mod;
		va->base = base;
		va->hostGla = hostGla;
		va->gameGla = gameGla;
		va->lods.assign( as->numLods, hairVkLod_t() );
		for ( size_t l = 0; l < va->lods.size(); l++ ) {
			va->lods[l].tried = va->lods[l].ok = false;
			va->lods[l].buf = VK_NULL_HANDLE;
			va->lods[l].mem = VK_NULL_HANDLE;
		}
		as->rb = va;
	}
	bool ok = true;
	for ( int l = 0; l < as->numLods; l++ ) {
		bool any = false;
		for ( size_t s = 0; s < as->listed[l].size() && !any; s++ ) any = as->listed[l][s] != 0;
		if ( !any ) continue;
		hairVkLod_t &L = va->lods[l];
		if ( !L.tried ) {
			if ( maxBuilds-- <= 0 ) return -1;
			L.tried = true;
			loadProfScope_t prof( LOADPROF_HAIRGPU );
			L.ok = Hair_VkBuildLod( as, va, l, L );
			if ( !L.ok ) ri.Printf( PRINT_WARNING, "hair: %s LOD %d: could not build the substitute buffer\n", as->name, l );
		}
		ok = ok && L.ok;
	}
	return ok ? 1 : 0;
}

VBO_t *RB_HairVkSurfaceVBO( const CBoneCache *bc, const mdxmVBOMesh_t *vboMesh, int surfaceIndex ) {
	const hairInst_t *hi = Hair_VkReadyInst( bc );
	if ( !hi || !vboMesh ) return NULL;
	const hairModelAsset_t *as = hi->asset;
	const model_t *mod = as->boundMod;
	const hairVkAsset_t *va = (const hairVkAsset_t *)as->rb;
	if ( !mod || !va || va->mod != mod || va->base != hi->numGlaBones || !mod->data.glm || !mod->data.glm->vboModels ) return NULL;
	// the LOD this mesh belongs to
	const mdxmHeader_t *mdxm = mod->data.glm->header;
	int lod = -1;
	for ( int l = 0; l < as->numLods && l < mdxm->numLODs; l++ ) {
		const mdxmVBOModel_t &vm = mod->data.glm->vboModels[l];
		if ( vboMesh >= vm.vboMeshes && vboMesh < vm.vboMeshes + vm.numVBOMeshes ) {
			lod = l;
			break;
		}
	}
	if ( lod < 0 || surfaceIndex < 0 || surfaceIndex >= mdxm->numSurfaces || !as->listed[lod][surfaceIndex] ) return NULL;
	const hairVkLod_t &L = va->lods[lod];
	return L.ok && !L.shared ? (VBO_t *)&L.vbo : NULL;	// a shared LOD draws from the model's own buffer
}
