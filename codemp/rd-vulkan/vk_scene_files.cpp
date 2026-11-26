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
vk_scene_files.cpp: render scene side files that both level ports write (CP2077's converters and the Witcher 3
tools): maps/<map>.xdcl projected decals, maps/<map>.xwat water, maps/<map>.xfx particles (their load and their
draw). Moved from vk_scene.cpp.
*/
#include "vk_scene_local.h"

/*
r_scenePDecals: CP2077's projected decals, maps/<map>.xdcl version 1 or 2 (tools/levelport cp77decals.py writes them):
a 32 B header ("XDCL", version, image, material and decal counts), 64 B image paths, 80 B materials (version 2: 112 B,
with the emissive and tiled images), 80 B records (a 3x4
map from scene space to the box's [-1, 1]^3, material, alpha, hide distance, flags, normal threshold, orderNo, roughness
scale, colour alpha), the records in orderNo then material order. Drawn by Scene_DrawPDecals into the DBuffer.
*/
typedef struct { char magic[4]; uint32_t version, images, materials, decals, reserved[3]; } xdclHeader_t;
#define PDECAL_CELL		1200.0f		// the grid's cells, game units (32 m)
#define PDECAL_NONE		0xffffffffu
scenePDecals_t scenePd;	// (its type in vk_scene_local.h: vk_scene.cpp's DBuffer pass reads it)

static float Scene_PDecalFloat( const byte *p ) { float v; Com_Memcpy( &v, p, 4 ); return v; }

// the file into scenePd (CPU: records, materials, images, the grid); its buffers and pipeline come with the DBuffer
void Scene_LoadPDecals( const char *scnName )
{
	scenePd.img.clear(); scenePd.mat.clear(); scenePd.rec.clear(); scenePd.bound.clear();
	scenePd.cellStart.clear(); scenePd.cellIdx.clear();
	scenePd.count = 0;
	scenePd.gridW = scenePd.gridH = 0;
	if ( !r_scenePDecals->integer )
		return;
	char name[MAX_QPATH];
	void *buf = NULL;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xdcl" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len <= 0 || !buf )
		return;
	const xdclHeader_t *h = (const xdclHeader_t *)buf;
	const byte *b = (const byte *)buf;
	const uint32_t matSize = len >= 32 && h->version == 2 ? 112u : 80u;	// (version 2: emissive and tiled images)
	const uint64_t need = 32u + (uint64_t)( len >= 32 ? h->images : 0 ) * 64u + (uint64_t)( len >= 32 ? h->materials : 0 ) * matSize
		+ (uint64_t)( len >= 32 ? h->decals : 0 ) * 80u;
	if ( len < 32 || memcmp( h->magic, "XDCL", 4 ) || ( h->version != 1 && h->version != 2 ) || need > (uint64_t)len ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 or 2 projected decals file, ignored\n", name );
		ri.FS_FreeFile( buf );
		return;
	}
	uint32_t missing = 0, badMat = 0, badRec = 0, notStatic = 0, terrain = 0;
	const byte *pi = b + 32, *pm = pi + (size_t)h->images * 64u, *pd = pm + (size_t)h->materials * matSize;
	for ( uint32_t i = 0; i < h->images; i++ ) {
		char path[65];
		Com_Memcpy( path, pi + i * 64u, 64 );
		path[64] = '\0';
		image_t *image = path[0] ? R_FindImageFile( path, IMGFLAG_MIPMAP | IMGFLAG_NOLIGHTSCALE | IMGFLAG_CLAMPTOEDGE, 0 ) : NULL;
		if ( !image )
			missing++;
		scenePd.img.push_back( image );
	}
	scenePd.mat.resize( (size_t)h->materials * 28u );
	for ( uint32_t m = 0; m < h->materials; m++ ) {	// (version 1: no emissive or tiled images, period 0, scale 1, levels 0 1)
		uint32_t *M = &scenePd.mat[(size_t)m * 28u];
		Com_Memcpy( M, pm + (size_t)m * matSize, matSize );
		if ( matSize == 80u ) {
			const float pad[8] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f };
			M[17] = M[18] = M[19] = M[20] = PDECAL_NONE;
			Com_Memcpy( &M[21], pad, sizeof( pad ) - 4 );
		}
	}
	std::vector<uint8_t> matOk( h->materials, 1 ), matTerrain( h->materials, 0 );
	for ( uint32_t m = 0; m < h->materials; m++ ) {	// an image index past the list or missing: none; values not finite: unused
		uint32_t *M = &scenePd.mat[(size_t)m * 28u];
		for ( int k = 0; k < 21; k = k == 2 ? 17 : k + 1 )	// (the images: 0-2, 17-20)
			if ( M[k] != PDECAL_NONE && ( M[k] >= h->images || !scenePd.img[M[k]] ) )
				M[k] = PDECAL_NONE;
		for ( int k = 4; k < 28; k = k == 16 ? 21 : k + 1 ) {
			float v;
			Com_Memcpy( &v, &M[k], 4 );
			if ( !isfinite( v ) || fabsf( v ) > 1e6f )
				matOk[m] = 0;
		}
		if ( M[0] == PDECAL_NONE && M[1] == PDECAL_NONE && M[2] == PDECAL_NONE && M[17] == PDECAL_NONE && M[18] == PDECAL_NONE )
			matOk[m] = 0;
		// r_scenePDecalTerrain 0: a terrain-projected material without its tiled colour (a mask and a tint) left out
		matTerrain[m] = ( M[3] & 4u ) && M[18] == PDECAL_NONE && !r_scenePDecalTerrain->integer ? 1 : 0;
		badMat += matOk[m] ? 0u : 1u;
	}
	float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
	for ( uint32_t i = 0; i < h->decals; i++ ) {
		const byte *r = pd + (size_t)i * 80u;
		float f[20];
		Com_Memcpy( f, r, 80 );
		uint32_t mat, flags;
		Com_Memcpy( &mat, r + 48, 4 );
		Com_Memcpy( &flags, r + 60, 4 );
		if ( !( flags & ( 256 | 512 | 1024 ) ) ) {	// only vehicles or characters receive: not in the scene's pass
			notStatic++;
			continue;
		}
		if ( mat < h->materials && matTerrain[mat] ) {
			terrain++;
			continue;
		}
		qboolean ok = mat < h->materials && matOk[mat] ? qtrue : qfalse;
		for ( int k = 0; ok && k < 12; k++ )
			ok = isfinite( f[k] ) && fabsf( f[k] ) < 1e9f ? qtrue : qfalse;
		ok = ok && isfinite( f[13] ) && isfinite( f[14] ) && isfinite( f[16] ) && isfinite( f[18] ) && isfinite( f[19] ) ? qtrue : qfalse;
		// the box: scene = Mi (q - t); its centre and the half axes' combined length
		float Mi[3][3];
		if ( ok ) {
			const float a = f[0], bb = f[1], c = f[2], d = f[4], e = f[5], g = f[6], hh = f[8], ii = f[9], j = f[10];
			const float det = a * ( e * j - g * ii ) - bb * ( d * j - g * hh ) + c * ( d * ii - e * hh );
			if ( fabsf( det ) < 1e-12f ) {
				ok = qfalse;
			} else {
				const float id = 1.0f / det;
				Mi[0][0] = ( e * j - g * ii ) * id; Mi[0][1] = ( c * ii - bb * j ) * id; Mi[0][2] = ( bb * g - c * e ) * id;
				Mi[1][0] = ( g * hh - d * j ) * id; Mi[1][1] = ( a * j - c * hh ) * id; Mi[1][2] = ( c * d - a * g ) * id;
				Mi[2][0] = ( d * ii - e * hh ) * id; Mi[2][1] = ( bb * hh - a * ii ) * id; Mi[2][2] = ( a * e - bb * d ) * id;
			}
		}
		if ( !ok ) {
			badRec++;
			continue;
		}
		const float t[3] = { f[3], f[7], f[11] };
		float ctr[3], radSq = 0.0f;
		for ( int k = 0; k < 3; k++ ) {
			ctr[k] = -( Mi[k][0] * t[0] + Mi[k][1] * t[1] + Mi[k][2] * t[2] );
			radSq += Mi[k][0] * Mi[k][0] + Mi[k][1] * Mi[k][1] + Mi[k][2] * Mi[k][2];
		}
		scenePd.rec.insert( scenePd.rec.end(), f, f + 20 );
		scenePd.bound.insert( scenePd.bound.end(), { ctr[0], ctr[1], ctr[2], sqrtf( radSq ), MAX( f[14], 0.0f ) } );
		for ( int k = 0; k < 2; k++ ) {
			lo[k] = MIN( lo[k], ctr[k] );
			hi[k] = MAX( hi[k], ctr[k] );
		}
	}
	const uint32_t numDecals = h->decals, numMats = h->materials, numImages = h->images;
	ri.FS_FreeFile( buf );	// (h is in it)
	scenePd.count = (uint32_t)( scenePd.bound.size() / 5u );
	if ( scenePd.count ) {	// the grid: each record in the cell of its centre
		scenePd.gridLo[0] = lo[0];
		scenePd.gridLo[1] = lo[1];
		scenePd.gridW = MIN( (int)( ( hi[0] - lo[0] ) / PDECAL_CELL ) + 1, 4096 );
		scenePd.gridH = MIN( (int)( ( hi[1] - lo[1] ) / PDECAL_CELL ) + 1, 4096 );
		if ( hi[0] - lo[0] >= 4096.0f * PDECAL_CELL || hi[1] - lo[1] >= 4096.0f * PDECAL_CELL )	// (the last cell's sphere then misses its far ones)
			ri.Printf( PRINT_WARNING, "WARNING: %s: the decals span past the grid's 4096 cells a side; the farthest may be culled wrongly\n", name );
		auto cellOf = [&]( uint32_t i ) {
			const int cx = MIN( (int)( ( scenePd.bound[i * 5u] - lo[0] ) / PDECAL_CELL ), scenePd.gridW - 1 );
			const int cy = MIN( (int)( ( scenePd.bound[i * 5u + 1u] - lo[1] ) / PDECAL_CELL ), scenePd.gridH - 1 );
			return (uint32_t)( cy * scenePd.gridW + cx );
		};
		scenePd.cellStart.assign( (size_t)scenePd.gridW * scenePd.gridH + 1, 0 );
		for ( uint32_t i = 0; i < scenePd.count; i++ )
			scenePd.cellStart[cellOf( i ) + 1]++;
		for ( size_t c = 1; c < scenePd.cellStart.size(); c++ )
			scenePd.cellStart[c] += scenePd.cellStart[c - 1];
		std::vector<uint32_t> fill( scenePd.cellStart.begin(), scenePd.cellStart.end() - 1 );
		scenePd.cellIdx.resize( scenePd.count );
		for ( uint32_t i = 0; i < scenePd.count; i++ )
			scenePd.cellIdx[fill[cellOf( i )]++] = i;
		scenePd.cellBound.assign( (size_t)scenePd.gridW * scenePd.gridH * 4, 0.0f );
		for ( size_t c = 0; c + 1 < scenePd.cellStart.size(); c++ ) {
			float *CB = &scenePd.cellBound[c * 4];
			CB[0] = 1e30f;
			CB[1] = -1e30f;
			for ( uint32_t k = scenePd.cellStart[c]; k < scenePd.cellStart[c + 1]; k++ ) {
				const float *B = &scenePd.bound[(size_t)scenePd.cellIdx[k] * 5u];
				CB[0] = MIN( CB[0], B[2] );
				CB[1] = MAX( CB[1], B[2] );
				CB[2] = MAX( CB[2], B[3] );
				CB[3] = CB[3] < 0.0f || B[4] <= 0.0f ? -1.0f : MAX( CB[3], B[4] );
			}
		}
	}
	ri.Printf( PRINT_ALL, "scene projected decals %s: %u of %u drawn (%u only for vehicles or characters, %u terrain-projected left out, %u bad), %u materials (%u unusable), "
		"%u images (%u missing), grid %dx%d\n", name, scenePd.count, numDecals, notStatic, terrain, badRec, numMats, badMat, numImages, missing,
		scenePd.gridW, scenePd.gridH );
}

void Scene_FreePDecalsGpu( void )
{
	if ( scenePd.pipe ) qvkDestroyPipeline( vk.device, scenePd.pipe, NULL );
	if ( scenePd.pipeLayout ) qvkDestroyPipelineLayout( vk.device, scenePd.pipeLayout, NULL );
	if ( scenePd.layout ) qvkDestroyDescriptorSetLayout( vk.device, scenePd.layout, NULL );
	if ( scenePd.pool ) qvkDestroyDescriptorPool( vk.device, scenePd.pool, NULL );
	scenePd.pipe = VK_NULL_HANDLE;
	scenePd.pipeLayout = VK_NULL_HANDLE;
	scenePd.layout = VK_NULL_HANDLE;
	scenePd.pool = VK_NULL_HANDLE;
	Scene_DestroyBuffer( &scenePd.recs );
	Scene_DestroyBuffer( &scenePd.mats );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( scenePd.visPtr[f] )
			qvkUnmapMemory( vk.device, scenePd.visBuf[f].mem );
		scenePd.visPtr[f] = NULL;
		Scene_DestroyBuffer( &scenePd.visBuf[f] );
		scenePd.set[f] = VK_NULL_HANDLE;
		scenePd.written[f] = qfalse;
		scenePd.depthView[f] = VK_NULL_HANDLE;
	}
}

// the projected decals' buffers (records, materials, each slot's visible list), sets and pipeline in the DBuffer's pass
qboolean Scene_CreatePDecals( void )
{
	const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	scenePd.visCap = (uint32_t)Com_Clamp( 1024, 1 << 20, r_scenePDecalMax->integer );
	if ( !Scene_StaticBuffer( &scenePd.recs, scenePd.rec.data(), (VkDeviceSize)scenePd.rec.size() * 4, ssbo, "scene projected decals" )
		|| !Scene_StaticBuffer( &scenePd.mats, scenePd.mat.data(), (VkDeviceSize)MAX( scenePd.mat.size(), (size_t)28 ) * 4, ssbo, "scene projected decal materials" ) )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( !Scene_CreateBuffer( &scenePd.visBuf[f], (VkDeviceSize)scenePd.visCap * 4, ssbo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				"scene projected decals visible" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, scenePd.visBuf[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&scenePd.visPtr[f] ) );
	}
	static const VkDescriptorType types[5] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
	static const VkShaderStageFlags stages[5] = { VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
	scenePd.layout = Scene_SetLayout( types, stages, 5 );
	if ( !scenePd.layout )
		return qfalse;
	VkDescriptorSetLayout sets[8] = { scenePd.layout, vk.set_layout_sampler, vk.set_layout_sampler, vk.set_layout_sampler,
		vk.set_layout_sampler, vk.set_layout_sampler, vk.set_layout_sampler, vk.set_layout_sampler };	// (the material's seven images)
	VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 96 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 8;
	pl.pSetLayouts = sets;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &scenePd.pipeLayout ) != VK_SUCCESS )
		return qfalse;
	VkDescriptorPoolSize ps[3] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NUM_COMMAND_BUFFERS }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * NUM_COMMAND_BUFFERS },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_COMMAND_BUFFERS } };
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 3;
	dp.pPoolSizes = ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &scenePd.pool ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = scenePd.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &scenePd.layout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &scenePd.set[f] ) != VK_SUCCESS )
			return qfalse;
		scenePd.written[f] = qfalse;	// (the frame's buffer is made with the views: written at the first draw)
	}
	sc.pdecalVs = Scene_Shader( scene_pdecal_vert_spv, scene_pdecal_vert_spv_size );
	VkShaderModule fs = vk.msaaActive ? Scene_Shader( scene_pdecal_frag_ms_spv, scene_pdecal_frag_ms_spv_size )
		: Scene_Shader( scene_pdecal_frag_spv, scene_pdecal_frag_spv_size );
	scenePd.pipe = ( sc.pdecalVs && fs ) ? Scene_DecalPipeline( sc.pdecalVs, fs ) : VK_NULL_HANDLE;
	if ( sc.pdecalVs ) qvkDestroyShaderModule( vk.device, sc.pdecalVs, NULL );
	if ( fs ) qvkDestroyShaderModule( vk.device, fs, NULL );
	sc.pdecalVs = VK_NULL_HANDLE;
	return scenePd.pipe ? qtrue : qfalse;
}

// the eye view's projected decals: the grid's cells within r_scenePDecalDistance, the records within their hide distance
// whose sphere meets the frustum, in the file's order; drawn a run of one material at a time (36 vertices an instance)
void Scene_DrawPDecals( VkCommandBuffer cb, const int f, const int v, const float *viewProj )
{
	if ( !scenePd.pipe || !scenePd.count || !r_scenePDecals->integer || !scenePd.visPtr[f] )
		return;
	const double t0 = r_scenePDecalDebug->integer ? R_SunProfNow() : 0.0;	// (timed for r_scenePDecalDebug's report)
	const sceneView_t *view = &sc.views[v];
	const float *E0 = view->origin;
	const float tx = view->tanHalfFov[0], ty = view->tanHalfFov[1];
	const float sx = sqrtf( 1.0f + tx * tx ), sy = sqrtf( 1.0f + ty * ty );
	const float cap = MAX( r_scenePDecalDistance->value, 1.0f ) * 37.5f;
	std::vector<uint32_t> &vis = scenePd.vis;
	vis.clear();
	const int x0 = MAX( (int)floorf( ( E0[0] - cap - scenePd.gridLo[0] ) / PDECAL_CELL ), 0 ), x1 = MIN( (int)floorf( ( E0[0] + cap - scenePd.gridLo[0] ) / PDECAL_CELL ), scenePd.gridW - 1 );
	const int y0 = MAX( (int)floorf( ( E0[1] - cap - scenePd.gridLo[1] ) / PDECAL_CELL ), 0 ), y1 = MIN( (int)floorf( ( E0[1] + cap - scenePd.gridLo[1] ) / PDECAL_CELL ), scenePd.gridH - 1 );
	uint32_t tested = 0;
	for ( int y = y0; y <= y1; y++ ) {
		for ( int x = x0; x <= x1; x++ ) {
			const uint32_t c = (uint32_t)( y * scenePd.gridW + x );
			if ( scenePd.cellStart[c] == scenePd.cellStart[c + 1] )
				continue;
			{	// the cell whole: its records' sphere (the cell's xy, their z range, their largest radius) past their largest
				// reach or outside the frustum
				const float *CB = &scenePd.cellBound[(size_t)c * 4];
				const float half = PDECAL_CELL * 0.5f, hz = 0.5f * ( CB[1] - CB[0] );
				const vec3_t cc = { scenePd.gridLo[0] + ( x + 0.5f ) * PDECAL_CELL, scenePd.gridLo[1] + ( y + 0.5f ) * PDECAL_CELL, 0.5f * ( CB[0] + CB[1] ) };
				const float R = sqrtf( 2.0f * half * half + hz * hz ) + CB[2];
				const float reach = CB[3] > 0.0f ? MIN( CB[3], cap ) : cap;
				vec3_t d;
				VectorSubtract( cc, E0, d );
				const float dist = VectorLength( d );
				if ( dist - R > reach )
					continue;
				const float z = DotProduct( d, view->axis[0] );
				if ( z < -R || fabsf( DotProduct( d, view->axis[1] ) ) > z * tx + R * sx || fabsf( DotProduct( d, view->axis[2] ) ) > z * ty + R * sy )
					continue;
			}
			for ( uint32_t k = scenePd.cellStart[c]; k < scenePd.cellStart[c + 1]; k++ ) {
				const uint32_t i = scenePd.cellIdx[k];
				const float *B = &scenePd.bound[(size_t)i * 5u];
				tested++;
				vec3_t d;
				VectorSubtract( B, E0, d );
				const float reach = B[4] > 0.0f ? MIN( B[4], cap ) : cap;
				if ( DotProduct( d, d ) > reach * reach )
					continue;
				const float z = DotProduct( d, view->axis[0] ), R = B[3];
				if ( z < -R || fabsf( DotProduct( d, view->axis[1] ) ) > z * tx + R * sx || fabsf( DotProduct( d, view->axis[2] ) ) > z * ty + R * sy )
					continue;
				vis.push_back( i );
			}
		}
	}
	if ( vis.empty() )
		return;
	std::sort( vis.begin(), vis.end() );	// (the file's order: orderNo, then material)
	if ( vis.size() > scenePd.visCap )
		vis.resize( scenePd.visCap );
	Com_Memcpy( scenePd.visPtr[f], vis.data(), vis.size() * 4 );

	VkDescriptorSet set = scenePd.set[f];
	VkWriteDescriptorSet w[5];
	VkDescriptorBufferInfo bi[4] = { { sc.frame[f].buf, 0, VK_WHOLE_SIZE }, { scenePd.recs.buf, 0, VK_WHOLE_SIZE },
		{ scenePd.visBuf[f].buf, 0, VK_WHOLE_SIZE }, { scenePd.mats.buf, 0, VK_WHOLE_SIZE } };
	VkDescriptorImageInfo di;
	di.sampler = sc.nearestSampler;
	di.imageView = vk.depth.sampler_view;
	di.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	// the buffers once; the depth when its view is not the one the set holds
	const int first = scenePd.written[f] ? 4 : 0;
	const int last = ( scenePd.written[f] && scenePd.depthView[f] == di.imageView ) ? 4 : 5;
	for ( int k = first; k < last; k++ ) {
		Com_Memset( &w[k], 0, sizeof( w[k] ) );
		w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w[k].dstSet = set;
		w[k].dstBinding = k;
		w[k].descriptorCount = 1;
		w[k].descriptorType = k == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : k < 4 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		if ( k < 4 )
			w[k].pBufferInfo = &bi[k];
		else
			w[k].pImageInfo = &di;
	}
	if ( last > first )
		qvkUpdateDescriptorSets( vk.device, last - first, &w[first], 0, NULL );
	scenePd.written[f] = qtrue;
	scenePd.depthView[f] = di.imageView;

	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePd.pipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePd.pipeLayout, 0, 1, &set, 0, NULL );
	struct { float viewProj[16]; float eye[4]; uint32_t info[4]; } push;
	Com_Memcpy( push.viewProj, viewProj, 64 );
	VectorCopy( E0, push.eye );
	push.eye[3] = cap;
	if ( scenePd.normalCount != r_scenePDecalNormal->modificationCount ) {	// (parsed when it changes)
		int mode = 1, flip = 0, soft = 0;
		sscanf( r_scenePDecalNormal->string, "%d %d %d", &mode, &flip, &soft );
		scenePd.normalMode[0] = (uint32_t)Com_Clamp( 0, 2, mode );
		scenePd.normalMode[1] = ( flip ? 1u : 0u ) | ( soft ? 2u : 0u );
		scenePd.normalCount = r_scenePDecalNormal->modificationCount;
	}
	push.info[1] = scenePd.normalMode[0];
	push.info[2] = scenePd.normalMode[1];
	push.info[3] = r_scenePDecalDebug->integer == 2 ? 1u : 0u;
	uint32_t runs = 0;
	VkDescriptorSet bound[7] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
	for ( size_t k = 0; k < vis.size(); ) {
		uint32_t mat;
		Com_Memcpy( &mat, &scenePd.rec[(size_t)vis[k] * 20u + 12u], 4 );
		size_t e = k + 1;
		while ( e < vis.size() && !memcmp( &scenePd.rec[(size_t)vis[e] * 20u + 12u], &mat, 4 ) )
			e++;
		const uint32_t *M = &scenePd.mat[(size_t)mat * 28u];
		VkDescriptorSet img[7];	// albedo, normal, rough, emissive, tiled colour, normal, rough
		static const int slot[7] = { 0, 1, 2, 17, 18, 19, 20 };
		for ( int j = 0; j < 7; j++ )
			img[j] = ( M[slot[j]] != PDECAL_NONE ? scenePd.img[M[slot[j]]] : tr.whiteImage )->descriptor_set;
		if ( memcmp( img, bound, sizeof( img ) ) ) {	// (a run of another material with the same images binds none)
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePd.pipeLayout, 1, 7, img, 0, NULL );
			Com_Memcpy( bound, img, sizeof( bound ) );
		}
		push.info[0] = mat;
		qvkCmdPushConstants( cb, scenePd.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
		qvkCmdDraw( cb, 36, (uint32_t)( e - k ), 0, (uint32_t)k );
		runs++;
		k = e;
	}
	scenePd.drawn += vis.size();
	scenePd.runs += runs;
	scenePd.tested += tested;
	if ( t0 > 0.0 )
		scenePd.cpuMs += R_SunProfNow() - t0;
	if ( r_scenePDecalDebug->integer && ++scenePd.frames >= 60 ) {
		ri.Printf( PRINT_ALL, "scene projected decals: %.0f drawn in %.0f runs of %.0f tested a frame, %.3f ms CPU\n", scenePd.drawn / scenePd.frames,
			scenePd.runs / scenePd.frames, scenePd.tested / scenePd.frames, scenePd.cpuMs / scenePd.frames );
		scenePd.drawn = scenePd.runs = scenePd.tested = scenePd.cpuMs = 0.0;
		scenePd.frames = 0;
	}
}

// maps/<name>.xwat (tools/levelport/cp2gltf.py): this header, then count records of 104 bytes, one per water material:
// its index in the scene's materials, then (f32, 0 the default) its visibility (m of path through it at which the bed
// fades to 1/e), shore (m of path over which it fades in from the bed), the ripples' tile (m) and speed (m/s), their
// heading (degrees from +x), roughness, normal strength, foam band (m of path), foam intensity (0: 1), then its foam
// image (64 B path, empty: none). A water material without one keeps the defaults (Scene_Load)
typedef struct { char ident[4]; uint32_t version, count, pad; } xwatHeader_t;

void Scene_LoadWater( const char *scnName, std::vector<float> &params )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	uint32_t numRecords = 0;
	int missing = 0;

	for ( uint32_t i = 0; i < sc.numMaterials; i++ )
		if ( sc.materials[i].kind == SCENE_KIND_WATER ) {
			float *d = &params[i * 4u];
			d[0] = d[1] = d[3] = 0.0f;
			d[2] = 1.0f;
			sc.materials[i].detail = tr.whiteImage;
		}
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xwat" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len >= (int)sizeof( xwatHeader_t ) ) {
		const xwatHeader_t *h = (const xwatHeader_t *)buf;
		if ( !memcmp( h->ident, "XWAT", 4 ) && h->version >= 1 && h->version <= 3 && (uint64_t)len >= sizeof( xwatHeader_t ) + (uint64_t)h->count * 104u ) {
			for ( uint32_t r = 0; r < h->count; r++ ) {
				const byte *rec = (const byte *)buf + sizeof( xwatHeader_t ) + r * 104u;
				uint32_t i;
				float f[9];
				Com_Memcpy( &i, rec, 4 );
				Com_Memcpy( f, rec + 4, sizeof( f ) );
				if ( i >= sc.numMaterials || sc.materials[i].kind != SCENE_KIND_WATER )
					continue;
				sceneMaterial_t *m = &sc.materials[i];
				float *d = &params[i * 4u];
				for ( int k = 0; k < 4; k++ )
					if ( f[k] > 0.0f )
						m->emissive[k] = f[k];
				d[0] = DEG2RAD( f[4] );
				d[1] = f[5] > 0.0f ? Com_Clamp( 0.02f, 1.0f, f[5] ) : 0.0f;
				if ( f[6] > 0.0f )
					d[2] = f[6];
				d[3] = MAX( f[7], 0.0f );
				if ( f[8] > 0.0f )
					m->tint[3] = MIN( f[8], 1.0f );	// the foam's intensity (scene.frag reads tint.a as it)
				char path[65];
				Com_Memcpy( path, rec + 40, 64 );
				path[64] = '\0';
				if ( path[0] ) {
					qboolean found;
					image_t *image = Scene_Image( path, &found );
					if ( found )
						m->detail = image;
					else
						missing++;
				}
				if ( m->detail == tr.whiteImage )
					d[3] = 0.0f;	// no foam without its image
				numRecords++;
			}
		} else {
			ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 water file, ignored\n", name );
		}
	}
	if ( buf )
		ri.FS_FreeFile( buf );
	ri.Printf( PRINT_ALL, "scene water %s: %u of %u water materials with their own parameters (%d foam images missing)\n", name, numRecords,
		sc.numWater, missing );
}

/*
r_sceneFx: maps/<map>.xfx (cp77fx.py), version 1: "XFX\0", version, counts and offsets of its textures (80 B: a 64 B
path, columns, rows, flags), emitters (512 B: the EMITTER record), systems (16 B: first emitter, count, hide distance)
and placements (64 B: a 3x4 map's rows, system (its top two bits: 1 night only, 2 day only), emission scale, hide
distance, seed). The emitters and placements go
into the material records after the rest (scene_fx.vert reads them); the CPU keeps them to cull (Scene_DrawFx)
*/
typedef struct { char ident[4]; uint32_t version, count[4], ofs[4]; uint32_t pad[2]; } xfxHeader_t;
static struct {
	std::vector<image_t *>	tex;
	std::vector<uint32_t>	texGrid;		// columns | rows << 8
	std::vector<uint32_t>	texFlags;		// bit 0 additive; bit 1 the shape in the value, bit 31 bit 1 decided (cp77fx T_*)
	std::vector<float>		emit;			// 128 floats each (the record)
	std::vector<float>		reach;			// each emitter's bound around its placement (emitter space, game units)
	std::vector<uint32_t>	sysFirst, sysCount;
	std::vector<float>		sysHide;
	std::vector<float>		place;			// 16 floats each (the record)
	uint32_t				emitBase, placeBase;	// their first vec4 in the material records
	uint32_t				drawn, slots, frames;	// r_sceneFxDebug
} sceneFx;

void Scene_LoadFx( const char *scnName, std::vector<float> &params )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	sceneFx.tex.clear(); sceneFx.texGrid.clear(); sceneFx.texFlags.clear(); sceneFx.emit.clear(); sceneFx.reach.clear();
	sceneFx.sysFirst.clear(); sceneFx.sysCount.clear(); sceneFx.sysHide.clear(); sceneFx.place.clear();
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xfx" );
	const int len = r_sceneFx->integer ? ri.FS_ReadFile( name, &buf ) : 0;
	if ( len <= 0 || !buf )
		return;
	const xfxHeader_t *h = (const xfxHeader_t *)buf;
	static const uint32_t size[4] = { 80, 512, 16, 64 };
	qboolean ok = ( len >= (int)sizeof( xfxHeader_t ) && !memcmp( h->ident, "XFX", 4 ) && h->version == 1 ) ? qtrue : qfalse;
	for ( int k = 0; ok && k < 4; k++ )
		if ( (uint64_t)h->ofs[k] + (uint64_t)h->count[k] * size[k] > (uint64_t)len )
			ok = qfalse;
	if ( !ok ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 particle effects file, ignored\n", name );
		ri.FS_FreeFile( buf );
		return;
	}
	const byte *b = (const byte *)buf;
	uint32_t missing = 0;
	for ( uint32_t i = 0; i < h->count[0]; i++ ) {
		const byte *t = b + h->ofs[0] + i * 80u;
		char path[65];
		Com_Memcpy( path, t, 64 );
		path[64] = '\0';
		uint32_t g[3];
		Com_Memcpy( g, t + 64, 12 );
		image_t *image = path[0] ? R_FindImageFile( path, IMGFLAG_MIPMAP | IMGFLAG_NOLIGHTSCALE | IMGFLAG_CLAMPTOEDGE, 0 ) : NULL;
		if ( !image )
			missing++;
		sceneFx.tex.push_back( image );
		sceneFx.texGrid.push_back( MIN( g[0], 255u ) | MIN( g[1], 255u ) << 8 );
		sceneFx.texFlags.push_back( g[2] );
	}
	sceneFx.emit.resize( (size_t)h->count[1] * 128u );
	Com_Memcpy( sceneFx.emit.data(), b + h->ofs[1], (size_t)h->count[1] * 512u );
	uint32_t badEmit = 0, badPlace = 0;
	for ( uint32_t e = 0; e < h->count[1]; e++ ) {	// its reach: spawn box and offset, travel over its longest life, size
		const float *E = &sceneFx.emit[(size_t)e * 128u];
		const float life = MAX( E[13], 0.0f );
		float v = 0.0f, a = 0.0f, box = 0.0f, off = 0.0f;
		for ( int k = 0; k < 3; k++ ) {
			v += MAX( E[61 + k] * E[61 + k], E[64 + k] * E[64 + k] );
			a += E[75 + k] * E[75 + k];
			box += MAX( E[55 + k] * E[55 + k], E[58 + k] * E[58 + k] );
			off += E[50 + k] * E[50 + k];
		}
		uint32_t fl;
		Com_Memcpy( &fl, &E[1], 4 );
		float turb = 0.0f, noise = 0.0f, grow = 1.0f;
		for ( int k = 0; k < 3; k++ ) {
			turb += MAX( E[104 + k] * E[104 + k], E[107 + k] * E[107 + k] );
			noise += E[112 + k] * E[112 + k];
		}
		turb = ( fl & 4096u ) ? sqrtf( turb ) * life * MIN( E[111], 1.0f ) : 0.0f;	// (at most its speed over its share of the life)
		noise = ( fl & 8192u ) ? sqrtf( noise ) * MIN( life, 20.0f ) : 0.0f;
		if ( fl & 16384u )
			grow = MAX( 1.0f, MAX( E[124], E[127] ) );
		float reach = sqrtf( box ) + sqrtf( off ) + E[49] + ( sqrtf( v ) + E[54] ) * life + 0.5f * sqrtf( a ) * life * life + turb + noise +
			MAX( MAX( E[14], E[15] ), MAX( E[16], E[17] ) ) * grow;
		// the values the CPU reads (rates, life, sizes, motion): an emitter with any of them not finite is not drawn
		qboolean finite = isfinite( reach ) && reach < 1e7f ? qtrue : qfalse;
		for ( int i = 4; finite && i < 12; i++ )
			finite = isfinite( E[i] ) && fabsf( E[i] ) < 1e7f ? qtrue : qfalse;
		if ( !finite ) {
			reach = -1.0f;
			badEmit++;
		}
		sceneFx.reach.push_back( reach );
	}
	for ( uint32_t i = 0; i < h->count[2]; i++ ) {
		const byte *s = b + h->ofs[2] + i * 16u;
		uint32_t fc[2];
		float hide;
		Com_Memcpy( fc, s, 8 );
		Com_Memcpy( &hide, s + 8, 4 );
		const qboolean in = ( fc[0] <= h->count[1] && fc[1] <= h->count[1] - fc[0] ) ? qtrue : qfalse;
		sceneFx.sysFirst.push_back( in ? fc[0] : 0 );
		sceneFx.sysCount.push_back( in ? fc[1] : 0 );
		sceneFx.sysHide.push_back( hide );
	}
	sceneFx.place.resize( (size_t)h->count[3] * 16u );
	Com_Memcpy( sceneFx.place.data(), b + h->ofs[3], (size_t)h->count[3] * 64u );
	for ( uint32_t p = 0; p < h->count[3]; p++ ) {	// a placement with a map, scale or hide not finite: emits nothing
		float *P = &sceneFx.place[(size_t)p * 16u];
		qboolean finite = qtrue;
		for ( int i = 0; finite && i < 15; i++ )
			finite = i == 12 || ( isfinite( P[i] ) && fabsf( P[i] ) < 1e9f ) ? qtrue : qfalse;
		if ( !finite ) {
			P[13] = 0.0f;
			P[0] = P[1] = P[2] = P[3] = P[4] = P[5] = P[6] = P[7] = P[8] = P[9] = P[10] = P[11] = P[14] = 0.0f;
			badPlace++;
		}
	}
	if ( badEmit || badPlace )
		ri.Printf( PRINT_WARNING, "WARNING: %s: %u emitters and %u placements with values not finite or out of range, not drawn\n", name, badEmit, badPlace );
	// into the material records (a vec4 boundary first)
	params.resize( ( params.size() + 3u ) & ~(size_t)3u, 0.0f );
	sceneFx.emitBase = (uint32_t)( params.size() / 4u );
	params.insert( params.end(), sceneFx.emit.begin(), sceneFx.emit.end() );
	sceneFx.placeBase = (uint32_t)( params.size() / 4u );
	params.insert( params.end(), sceneFx.place.begin(), sceneFx.place.end() );
	ri.Printf( PRINT_ALL, "scene particles %s: %u placements, %u systems, %u emitters, %u textures (%u missing)\n", name, h->count[3],
		h->count[2], h->count[1], h->count[0], missing );
	ri.FS_FreeFile( buf );	// (h is in it)
}

// r_sceneFx: the eye view's particles with the blends (after them): every placement within its hide distance (the
// placement's, else its system's, else r_sceneFxDistance) whose emitter's reach meets the view, the nearest first up to
// r_sceneFxMax particle slots, drawn far to near; a draw of six vertices a slot each (scene_fx.vert)
typedef struct { float dist; uint32_t place, emit, slots, tex; } sceneFxDraw_t;
void Scene_DrawFx( VkCommandBuffer cb, const int f, const int v, scenePush_t *push )
{
	if ( !r_sceneFx->integer || sceneFx.place.empty() || v != sc.fogEyeView || !sc.fxPipe[0] || !sc.fxPipe[1] )
		return;
	const sceneView_t *view = &sc.views[v];
	const float *E0 = view->origin;
	const float tx = view->tanHalfFov[0], ty = view->tanHalfFov[1];
	const float sx = sqrtf( 1.0f + tx * tx ), sy = sqrtf( 1.0f + ty * ty );
	const float fallback = MAX( r_sceneFxDistance->value, 1.0f ) * 37.5f;
	static std::vector<sceneFxDraw_t> draws;
	draws.clear();
	const uint32_t numPlace = (uint32_t)( sceneFx.place.size() / 16u );
	for ( uint32_t p = 0; p < numPlace; p++ ) {
		const float *P = &sceneFx.place[(size_t)p * 16u];
		uint32_t sys;
		Com_Memcpy( &sys, &P[12], 4 );
		// (its top two bits: 1 lit at night only, 2 by day only, as the lamps: a brazier's fire burns while its light does)
		const uint32_t when = sys >> 30;
		sys &= 0x3fffffffu;
		if ( sys >= sceneFx.sysFirst.size() || P[13] <= 0.0f )
			continue;
		if ( when && Scene_FxTime( when ) < 0.5f )
			continue;
		if ( r_sceneFxOnly->integer >= 0 && (int)sys != r_sceneFxOnly->integer )
			continue;	// (a look check: one system alone)
		const float hide = P[14] > 0.0f ? P[14] : sceneFx.sysHide[sys] > 0.0f ? sceneFx.sysHide[sys] : fallback;
		const vec3_t c = { P[3], P[7], P[11] };
		vec3_t d;
		VectorSubtract( c, E0, d );
		const float dist = VectorLength( d );
		if ( dist > hide )
			continue;
		const float scale = sqrtf( P[0] * P[0] + P[4] * P[4] + P[8] * P[8] );
		for ( uint32_t k = 0; k < sceneFx.sysCount[sys]; k++ ) {
			const uint32_t e = sceneFx.sysFirst[sys] + k;
			const float *E = &sceneFx.emit[(size_t)e * 128u];
			if ( sceneFx.reach[e] < 0.0f )
				continue;
			const float R = sceneFx.reach[e] * MAX( scale, 1e-3f ) + 1.0f;
			const float z = DotProduct( d, view->axis[0] );
			if ( z < -R || fabsf( DotProduct( d, view->axis[1] ) ) > z * tx + R * sx || fabsf( DotProduct( d, view->axis[2] ) ) > z * ty + R * sy )
				continue;
			float peak = 0.0f;
			for ( int i = 0; i < 8; i++ )
				peak = MAX( peak, E[4 + i] );
			peak *= P[13];
			uint32_t tex, maxp;
			Com_Memcpy( &tex, &E[0], 4 );
			Com_Memcpy( &maxp, &E[2], 4 );
			if ( !( peak > 0.0f ) || !( E[13] > 0.0f ) || tex >= sceneFx.tex.size() || !sceneFx.tex[tex] )
				continue;
			uint32_t slots = (uint32_t)ceilf( MIN( peak * E[13], 1024.0f ) );	// (clamped before the conversion)
			if ( maxp )
				slots = (uint32_t)MIN( (uint64_t)slots, (uint64_t)MAX( maxp, 1u ) * (uint64_t)MIN( MAX( ceilf( P[13] ), 1.0f ), 1024.0f ) );
			slots = MIN( MAX( slots, 1u ), 1024u );
			sceneFxDraw_t dr = { dist, p, e, slots, tex };
			draws.push_back( dr );
		}
	}
	if ( draws.empty() )
		return;
	// the nearest under the cap, then far to near
	std::sort( draws.begin(), draws.end(), []( const sceneFxDraw_t &a, const sceneFxDraw_t &b ) { return a.dist < b.dist; } );
	const uint32_t cap = (uint32_t)MAX( r_sceneFxMax->integer, 0 );
	uint32_t total = 0, n = 0;
	while ( n < draws.size() && total + draws[n].slots <= cap )
		total += draws[n++].slots;
	if ( !n )
		return;
	VkViewport atNormal;
	get_viewport( &atNormal, DEPTH_RANGE_NORMAL );
	qvkCmdSetViewport( cb, 0, 1, &atNormal );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 0, 1, &sc.drawSet[f][v], 0, NULL );
	scenePush_t fxp;
	Com_Memcpy( &fxp, push, sizeof( fxp ) );
	{	// (scene_fx.vert takes scene space: the absolute matrix, whatever the passes' r_sceneEyeRelative)
		float proj[16];
		Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
		proj[5] = -proj[5];
		Scene_EyeViewProj( fxp.viewProj, proj, qfalse );
	}
	Com_Memset( fxp.emissive, 0, sizeof( fxp.emissive ) );
	fxp.emissive[0] = r_sceneFxAccelDrag->integer ? 1.0f : 0.0f;	// (scene_fx.vert: the acceleration under the drag)
	fxp.emissive[2] = Scene_FxEmissive();	// (scene.frag: the emissive particles' scale)
	Com_Memset( fxp.tint, 0, sizeof( fxp.tint ) );
	VectorCopy( E0, fxp.eye );
	fxp.eye[3] = 1.0f;
	int bound = -1;
	uint32_t boundTex = 0xffffffffu;
	for ( uint32_t k = n; k-- > 0; ) {
		const sceneFxDraw_t &dr = draws[k];
		const float *E = &sceneFx.emit[(size_t)dr.emit * 128u];
		uint32_t flags;
		Com_Memcpy( &flags, &E[1], 4 );
		const int pipe = ( ( flags & 1u ) || ( sceneFx.texFlags[dr.tex] & 1u ) ) ? 1 : 0;
		if ( pipe != bound ) {
			qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.fxPipe[pipe] );
			bound = pipe;
		}
		if ( dr.tex != boundTex ) {
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 1, 1, &sceneFx.tex[dr.tex]->descriptor_set, 0, NULL );
			boundTex = dr.tex;
		}
		fxp.info[0] = sceneFx.placeBase + dr.place * 4u;
		fxp.info[1] = sceneFx.emitBase + dr.emit * 32u;
		fxp.info[2] = dr.slots;
		fxp.info[3] = sceneFx.texGrid[dr.tex];
		// the mask (scene.frag modes 10, 11): 2 the texel's value, 1 its alpha, as the converter decided; 0 a pack made
		// before (bit 31 clear): the shader decides from the sheet's last mip
		const uint32_t tf = sceneFx.texFlags[dr.tex];
		fxp.emissive[1] = ( tf & 0x80000000u ) ? ( ( tf & 2u ) ? 2.0f : 1.0f ) : 0.0f;
		qvkCmdPushConstants( cb, sc.drawPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( fxp ), &fxp );
		qvkCmdDraw( cb, dr.slots * 6u, 1, 0, 0 );
	}
	if ( r_sceneFxDebug->integer ) {	// (counted only while on, so the first report covers its own frames)
		sceneFx.drawn += n;
		sceneFx.slots += total;
		if ( ++sceneFx.frames >= 60 ) {
			ri.Printf( PRINT_ALL, "scene particles: %.1f effects, %.0f slots a frame (of %u in reach)\n", sceneFx.drawn / (float)sceneFx.frames,
				sceneFx.slots / (float)sceneFx.frames, (uint32_t)draws.size() );
			sceneFx.drawn = sceneFx.slots = sceneFx.frames = 0;
		}
	}
}

// R_SceneFree (Scene_FreeCpuState): the projected decals' and the particles' CPU records
void Scene_FreeSideFilesCpu( void )
{
	scenePd.img.clear(); scenePd.mat.clear(); scenePd.rec.clear(); scenePd.bound.clear();
	scenePd.cellStart.clear(); scenePd.cellIdx.clear(); scenePd.cellBound.clear(); scenePd.vis.clear();
	scenePd.count = 0;
	scenePd.gridW = scenePd.gridH = 0;
	sceneFx.tex.clear(); sceneFx.texGrid.clear(); sceneFx.texFlags.clear(); sceneFx.emit.clear(); sceneFx.reach.clear();
	sceneFx.sysFirst.clear(); sceneFx.sysCount.clear(); sceneFx.sysHide.clear(); sceneFx.place.clear();
	sceneFx.emitBase = sceneFx.placeBase = 0;
}

qboolean Scene_FxPlaced( void )
{
	return sceneFx.place.empty() ? qfalse : qtrue;
}
