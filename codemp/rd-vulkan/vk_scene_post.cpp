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
vk_scene_post.cpp: the render scene's passes after its draws: the haze's light volume (vk_scene_haze), the reflections'
and AO's capture and trace (vk_scene_capture), the auto exposure's meter (r_sceneAutoExposure), and the colour grade
(maps/<map>.xlut, vk_scene_grade). Moved from vk_scene.cpp.
*/
#include "vk_scene_local.h"

static qboolean Scene_CreateGradeCompute( void );

// maps/<name>.xlut (tools/levelport/cp2gltf.py --grade): this header, then size^3 rgba16f texels (r fastest, then g,
// then b) of display light for scene light encoded by `input` (1: ARRI LogC v3 at EI 800)
typedef struct {
	char		ident[4];	// XLUT
	uint32_t	version;	// 1
	uint32_t	size;
	uint32_t	input;
} xlutHeader_t;

static qboolean Scene_GradeImage( VkImage *image, VkDeviceMemory *mem, VkImageView *view, qboolean lut, uint32_t w, uint32_t h,
	uint32_t d, VkFormat format, const char *name )
{
	VkImageCreateInfo ic;
	Com_Memset( &ic, 0, sizeof( ic ) );
	ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ic.imageType = lut ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	ic.format = format;
	ic.extent.width = w;
	ic.extent.height = h;
	ic.extent.depth = d;
	ic.mipLevels = 1;
	ic.arrayLayers = 1;
	ic.samples = VK_SAMPLE_COUNT_1_BIT;
	ic.tiling = VK_IMAGE_TILING_OPTIMAL;
	ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( qvkCreateImage( vk.device, &ic, NULL, image ) != VK_SUCCESS )
		return qfalse;
	VkMemoryRequirements req;
	qvkGetImageMemoryRequirements( vk.device, *image, &req );
	VkMemoryAllocateInfo alloc;
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	if ( vk_vram_allocate_named( vk.device, &alloc, mem, name ) != VK_SUCCESS )
		return qfalse;
	qvkBindImageMemory( vk.device, *image, *mem, 0 );
	VK_SET_OBJECT_NAME( *image, name, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	VkImageViewCreateInfo vc;
	Com_Memset( &vc, 0, sizeof( vc ) );
	vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vc.image = *image;
	vc.viewType = lut ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
	vc.format = format;
	vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vc.subresourceRange.levelCount = 1;
	vc.subresourceRange.layerCount = 1;
	return qvkCreateImageView( vk.device, &vc, NULL, view ) == VK_SUCCESS ? qtrue : qfalse;
}

// the full-frame pass in the post 1x render pass (one sample, no depth test, no blending)
static VkPipeline Scene_GradePipeline( VkShaderModule vs, VkShaderModule fs )
{
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendAttachmentState attachment;
	VkPipelineColorBlendStateCreateInfo blendState;
	VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic;
	VkPipelineShaderStageCreateInfo stages[2];
	VkGraphicsPipelineCreateInfo info;
	VkPipeline pipeline = VK_NULL_HANDLE;

	Com_Memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	Com_Memset( &inputAssembly, 0, sizeof( inputAssembly ) );
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	Com_Memset( &viewport, 0, sizeof( viewport ) );
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	Com_Memset( &raster, 0, sizeof( raster ) );
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
	raster.lineWidth = 1.0f;
	Com_Memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	Com_Memset( &depth, 0, sizeof( depth ) );
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.maxDepthBounds = 1.0f;
	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	Com_Memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = 1;
	blendState.pAttachments = &attachment;
	Com_Memset( &dynamic, 0, sizeof( dynamic ) );
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;
	Com_Memset( stages, 0, sizeof( stages ) );
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs;
	stages[1].pName = "main";
	Com_Memset( &info, 0, sizeof( info ) );
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertexInput;
	info.pInputAssemblyState = &inputAssembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blendState;
	info.pDynamicState = &dynamic;
	info.layout = sc.gradePipeLayout;
	info.renderPass = vk.render_pass.post1x.handle;
	info.subpass = 0;
	info.basePipelineIndex = -1;
	if ( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline ) != VK_SUCCESS )
		return VK_NULL_HANDLE;
	return pipeline;
}

// maps/<name>.xlut beside the scene, if any: its LUT uploaded, the frame copy, the pass. The grade runs at 1x after
// the MSAA resolve (the post 1x pass), so it needs MSAA, as the reflections do. Returns qfalse only on a bad file.

qboolean Scene_LoadGrade( const char *scnName )
{
	char name[MAX_QPATH];
	byte *buf = NULL;

	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xlut" );
	const int len = ri.FS_ReadFile( name, (void **)&buf );
	if ( len <= 0 || !buf )
		return qtrue;
	xlutHeader_t h;
	Com_Memcpy( &h, buf, MIN( len, (int)sizeof( h ) ) );
	if ( len < (int)sizeof( h ) || memcmp( h.ident, "XLUT", 4 ) || h.version != 1 || h.input != 1 || h.size < 2 || h.size > 128
		|| (uint64_t)len < sizeof( h ) + (uint64_t)h.size * h.size * h.size * 8 ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s is not a version 1 LogC colour grade: not used\n", name );
		ri.FS_FreeFile( buf );
		return qfalse;
	}
	if ( !vk.msaaActive || !vk.render_pass.post1x.handle ) {
		ri.Printf( PRINT_ALL, "scene grade %s: needs MSAA (the post 1x pass), not used\n", name );
		ri.FS_FreeFile( buf );
		return qtrue;
	}

	const uint32_t n = h.size;
	const VkDeviceSize bytes = (VkDeviceSize)n * n * n * 8;
	qboolean ok = ( Scene_GradeImage( &sc.gradeLut, &sc.gradeLutMem, &sc.gradeLutView, qtrue, n, n, n, VK_FORMAT_R16G16B16A16_SFLOAT, "scene grade lut" )
		&& Scene_GradeImage( &sc.gradeCopy, &sc.gradeCopyMem, &sc.gradeCopyView, qfalse, glConfig.vidWidth, glConfig.vidHeight, 1, vk.color_format, "scene grade frame" ) ) ? qtrue : qfalse;
	if ( ok ) {
		sceneBuffer_t staging;
		void *p;
		ok = Scene_CreateBuffer( &staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene grade staging" );
		if ( ok ) {
			VK_CHECK( qvkMapMemory( vk.device, staging.mem, 0, VK_WHOLE_SIZE, 0, &p ) );
			memcpy( p, buf + sizeof( h ), (size_t)bytes );
			qvkUnmapMemory( vk.device, staging.mem );
			VkCommandBuffer cmd = vk_begin_command_buffer();
			Scene_ImageBarrier( cmd, sc.gradeLut, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
			VkBufferImageCopy region;
			Com_Memset( &region, 0, sizeof( region ) );
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.layerCount = 1;
			region.imageExtent.width = region.imageExtent.height = region.imageExtent.depth = n;
			qvkCmdCopyBufferToImage( cmd, staging.buf, sc.gradeLut, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
			Scene_ImageBarrier( cmd, sc.gradeLut, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );
			vk_end_command_buffer( cmd, __func__ );
			Scene_DestroyBuffer( &staging );
		}
	}
	ri.FS_FreeFile( buf );

	// two samplers (the frame copy, read texel by texel; the LUT, filtered), a push constant
	static const VkDescriptorType types[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
	static const VkShaderStageFlags stages[2] = { VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
	if ( ok )
		ok = ( sc.gradeLayout = Scene_SetLayout( types, stages, 2 ) ) != VK_NULL_HANDLE ? qtrue : qfalse;
	if ( ok ) {
		VkPushConstantRange push;
		push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		push.offset = 0;
		push.size = sizeof( vec4_t );
		VkPipelineLayoutCreateInfo pl;
		Com_Memset( &pl, 0, sizeof( pl ) );
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = 1;
		pl.pSetLayouts = &sc.gradeLayout;
		pl.pushConstantRangeCount = 1;
		pl.pPushConstantRanges = &push;
		ok = qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.gradePipeLayout ) == VK_SUCCESS ? qtrue : qfalse;
	}
	if ( ok ) {
		VkShaderModule vs = Scene_Shader( scene_grade_vert_spv, scene_grade_vert_spv_size );
		VkShaderModule fs = Scene_Shader( scene_grade_frag_spv, scene_grade_frag_spv_size );
		sc.gradePipe = ( vs && fs ) ? Scene_GradePipeline( vs, fs ) : VK_NULL_HANDLE;
		if ( vs ) qvkDestroyShaderModule( vk.device, vs, NULL );
		if ( fs ) qvkDestroyShaderModule( vk.device, fs, NULL );
		ok = sc.gradePipe ? qtrue : qfalse;
	}
	if ( ok ) {
		VkDescriptorPoolSize ps;
		ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		ps.descriptorCount = 2;
		VkDescriptorPoolCreateInfo dp;
		Com_Memset( &dp, 0, sizeof( dp ) );
		dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		dp.maxSets = 1;
		dp.poolSizeCount = 1;
		dp.pPoolSizes = &ps;
		ok = qvkCreateDescriptorPool( vk.device, &dp, NULL, &sc.gradePool ) == VK_SUCCESS ? qtrue : qfalse;
	}
	if ( ok ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.gradePool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.gradeLayout;
		ok = qvkAllocateDescriptorSets( vk.device, &da, &sc.gradeSet ) == VK_SUCCESS ? qtrue : qfalse;
	}
	if ( ok ) {
		VkDescriptorImageInfo ii[2];
		VkWriteDescriptorSet wr[2];
		ii[0].sampler = sc.nearestSampler;
		ii[0].imageView = sc.gradeCopyView;
		ii[1].sampler = sc.fogSampler;		// linear, clamped
		ii[1].imageView = sc.gradeLutView;
		for ( int i = 0; i < 2; i++ ) {
			ii[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
			wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr[i].dstSet = sc.gradeSet;
			wr[i].dstBinding = i;
			wr[i].descriptorCount = 1;
			wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			wr[i].pImageInfo = &ii[i];
		}
		qvkUpdateDescriptorSets( vk.device, 2, wr, 0, NULL );
		sc.gradeReady = qtrue;
		if ( vk.colorStorage && !Scene_CreateGradeCompute() )
			ri.Printf( PRINT_WARNING, "WARNING: %s: could not create the grade's compute pass; graded over a copy\n", name );
		ri.Printf( PRINT_ALL, "scene grade %s: %u^3 LUT (LogC in, display light out)%s\n", name, n,
			vk.color_format == VK_FORMAT_R16G16B16A16_SFLOAT ? "" : "; highlights clipped at white (r_sceneHdr 1 or r_hdr 2 keeps them)" );
		return qtrue;
	}
	ri.Printf( PRINT_WARNING, "WARNING: %s: could not create the grade's GPU resources, not used\n", name );
	return qtrue;
}

// vk_attachments, at video start: whether the resolved colour image is made a storage image too, for the grade in
// place (r_sceneGradeCompute; the float target a graded scene asks for, with storage support)
qboolean vk_scene_color_storage( void )
{
	cvar_t *on = ri.Cvar_Get( "r_sceneGradeCompute", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, SCENE_GRADE_COMPUTE_HELP );
	if ( !on->integer || vk.color_format != VK_FORMAT_R16G16B16A16_SFLOAT )
		return qfalse;
	VkFormatProperties fp;
	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, vk.color_format, &fp );
	return ( fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT ) ? qtrue : qfalse;
}

// the grade in place (vk.colorStorage): scene_grade.frag compiled as compute, its layout (the frame as a storage
// image, the LUT), per slot a set; the views are written when the pass first runs in the slot
static qboolean Scene_CreateGradeCompute( void )
{
	static const VkDescriptorType types[2] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
	static const VkShaderStageFlags stages[2] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	if ( ( sc.gradeCompLayout = Scene_SetLayout( types, stages, 2 ) ) == VK_NULL_HANDLE )
		return qfalse;
	VkPushConstantRange push;
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = sizeof( vec4_t );
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.gradeCompLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.gradeCompPipeLayout ) != VK_SUCCESS )
		return qfalse;
	VkShaderModule cs = Scene_Shader( scene_grade_comp_spv, scene_grade_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.gradeCompPipeLayout;
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.gradeCompPipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( r != VK_SUCCESS ) {
		sc.gradeCompPipe = VK_NULL_HANDLE;
		return qfalse;
	}
	VkDescriptorPoolSize ps[2];
	ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	ps[0].descriptorCount = NUM_COMMAND_BUFFERS;
	ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	ps[1].descriptorCount = NUM_COMMAND_BUFFERS;
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 2;
	dp.pPoolSizes = ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &sc.gradeCompPool ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.gradeCompPool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.gradeCompLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.gradeCompSet[f] ) != VK_SUCCESS )
			return qfalse;
		sc.gradeCompView[f] = VK_NULL_HANDLE;
	}
	sc.gradeCompReady = qtrue;
	return qtrue;
}

void Scene_FreeGrade( void )
{
	if ( sc.gradeCompPipe ) qvkDestroyPipeline( vk.device, sc.gradeCompPipe, NULL );
	if ( sc.gradeCompPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.gradeCompPipeLayout, NULL );
	if ( sc.gradeCompPool ) qvkDestroyDescriptorPool( vk.device, sc.gradeCompPool, NULL );	// frees the sets
	if ( sc.gradeCompLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.gradeCompLayout, NULL );
	if ( sc.gradePipe ) qvkDestroyPipeline( vk.device, sc.gradePipe, NULL );
	if ( sc.gradePipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.gradePipeLayout, NULL );
	if ( sc.gradePool ) qvkDestroyDescriptorPool( vk.device, sc.gradePool, NULL );	// frees the set
	if ( sc.gradeLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.gradeLayout, NULL );
	if ( sc.gradeLutView ) qvkDestroyImageView( vk.device, sc.gradeLutView, NULL );
	if ( sc.gradeLut ) qvkDestroyImage( vk.device, sc.gradeLut, NULL );
	if ( sc.gradeLutMem ) qvkFreeMemory( vk.device, sc.gradeLutMem, NULL );
	if ( sc.gradeCopyView ) qvkDestroyImageView( vk.device, sc.gradeCopyView, NULL );
	if ( sc.gradeCopy ) qvkDestroyImage( vk.device, sc.gradeCopy, NULL );
	if ( sc.gradeCopyMem ) qvkFreeMemory( vk.device, sc.gradeCopyMem, NULL );
}

/*
r_sceneAutoExposure's meter (scene_meter.comp): its pipeline and a host-visible histogram a frame slot, made when first
wanted and kept until the scene is freed. exposure[f]: the exposure slot f's frame drew with when it was metered (0:
nothing to read); ev the adapted exposure in EV over r_sceneExposure, have once a meter was read since the load
*/
#define SCENE_METER_BINS		128
#define SCENE_METER_W			320
#define SCENE_METER_H			180
static struct {
	VkDescriptorSetLayout	layout;
	VkPipelineLayout		pipeLayout;
	VkPipeline				pipe;
	VkDescriptorPool		pool;
	VkDescriptorSet			set[NUM_COMMAND_BUFFERS];
	sceneBuffer_t			hist[NUM_COMMAND_BUFFERS];
	uint32_t				*ptr[NUM_COMMAND_BUFFERS];
	float					exposure[NUM_COMMAND_BUFFERS];
	float					drawn;			// the exposure this frame draws with
	float					ev, meter, target;
	qboolean				have, failed;
	int						lastMs, reads;
} sceneMeter;

void Scene_MeterDestroy( void )
{
	if ( sceneMeter.pipe || sceneMeter.pool || sceneMeter.layout )
		qvkDeviceWaitIdle( vk.device );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( sceneMeter.ptr[f] )
			qvkUnmapMemory( vk.device, sceneMeter.hist[f].mem );
		Scene_DestroyBuffer( &sceneMeter.hist[f] );
	}
	if ( sceneMeter.pool ) qvkDestroyDescriptorPool( vk.device, sceneMeter.pool, NULL );	// (frees the sets)
	if ( sceneMeter.pipe ) qvkDestroyPipeline( vk.device, sceneMeter.pipe, NULL );
	if ( sceneMeter.pipeLayout ) qvkDestroyPipelineLayout( vk.device, sceneMeter.pipeLayout, NULL );
	if ( sceneMeter.layout ) qvkDestroyDescriptorSetLayout( vk.device, sceneMeter.layout, NULL );
	Com_Memset( &sceneMeter, 0, sizeof( sceneMeter ) );
}

static qboolean Scene_MeterCreate( void )
{
	if ( sceneMeter.failed )	// (first: a create that failed part way leaves some of it made)
		return qfalse;
	if ( sceneMeter.pipe )
		return qtrue;
	sceneMeter.failed = qtrue;	// (until made)
	static const VkDescriptorType types[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[2] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	sceneMeter.layout = Scene_SetLayout( types, stages, 2 );
	if ( !sceneMeter.layout )
		return qfalse;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sceneMeter.layout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sceneMeter.pipeLayout ) != VK_SUCCESS )
		return qfalse;
	VkShaderModule cs = Scene_Shader( scene_meter_comp_spv, scene_meter_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sceneMeter.pipeLayout;
	const VkResult made = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sceneMeter.pipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( made != VK_SUCCESS ) {
		sceneMeter.pipe = VK_NULL_HANDLE;
		return qfalse;
	}
	VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_COMMAND_BUFFERS }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NUM_COMMAND_BUFFERS } };
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 2;
	dp.pPoolSizes = ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &sceneMeter.pool ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sceneMeter.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sceneMeter.layout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sceneMeter.set[f] ) != VK_SUCCESS
			|| !Scene_CreateBuffer( &sceneMeter.hist[f], SCENE_METER_BINS * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene exposure meter" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, sceneMeter.hist[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&sceneMeter.ptr[f] ) );
		Com_Memset( sceneMeter.ptr[f], 0, SCENE_METER_BINS * 4 );
		VkDescriptorBufferInfo bi = { sceneMeter.hist[f].buf, 0, VK_WHOLE_SIZE };
		VkWriteDescriptorSet w;
		Com_Memset( &w, 0, sizeof( w ) );
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = sceneMeter.set[f];
		w.dstBinding = 1;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w.pBufferInfo = &bi;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	}
	sceneMeter.failed = qfalse;
	return qtrue;
}

// r_sceneAutoExposure: the exposure the scene draws with, r_sceneExposure x 2^ev once metered (else r_sceneExposure)
float Scene_Exposure( void )
{
	const float base = Env( r_sceneExposure )->value;
	return ( Env( r_sceneAutoExposure )->integer && sceneMeter.have ) ? base * exp2f( sceneMeter.ev ) : base;
}

// r_sceneAutoExposure: slot f's histogram (its frame two frames ago; its fence passed) read and cleared for this frame:
// the meter (its log2 mean without the darkest 2% and the brightest 3%: CP2077's exposurePercentageThresholdLow,
// High), as at r_sceneExposure; the target r_sceneExposureMeter's share of the way to its key, inside the hour's
// window; ev toward it at r_sceneExposureSpeed (snapping to the first after a load)
static void Scene_MeterRead( const int f )
{
	if ( !sceneMeter.pipe || sceneMeter.failed || !sceneMeter.ptr[f] || sceneMeter.exposure[f] <= 0.0f )
		return;
	uint32_t bins[SCENE_METER_BINS];
	Com_Memcpy( bins, sceneMeter.ptr[f], sizeof( bins ) );
	Com_Memset( sceneMeter.ptr[f], 0, sizeof( bins ) );
	const float used = sceneMeter.exposure[f];
	sceneMeter.exposure[f] = 0.0f;
	double total = 0.0;
	for ( int i = 0; i < SCENE_METER_BINS; i++ )
		total += bins[i];
	if ( total < 100.0 )
		return;
	const double lo = 0.02 * total, hi = 0.97 * total;
	double at = 0.0, sum = 0.0, weight = 0.0;
	for ( int i = 0; i < SCENE_METER_BINS; i++ ) {
		const double a = MAX( at, lo ), b = MIN( at + bins[i], hi );
		if ( b > a ) {
			sum += ( b - a ) * ( ( i + 0.5 ) / 4.0 - 24.0 );
			weight += b - a;
		}
		at += bins[i];
	}
	if ( weight <= 0.0 )
		return;
	const float base = MAX( Env( r_sceneExposure )->value, 1e-6f );
	sceneMeter.meter = (float)( sum / weight ) - log2f( used ) + log2f( base );	// (as at r_sceneExposure)
	float key = -5.175f, share = 0.325f, minEV = -1.2f, maxEV = 1.0f, up = 4.0f, down = 7.5f;
	sscanf( r_sceneExposureMeter->string, "%f %f", &key, &share );
	sscanf( Env( r_sceneExposureWindow )->string, "%f %f", &minEV, &maxEV );
	sscanf( r_sceneExposureSpeed->string, "%f %f", &up, &down );
	sceneMeter.target = Com_Clamp( minEV, MAX( minEV, maxEV ), share * ( key - sceneMeter.meter ) );
	const int now = ri.Milliseconds();
	const float dt = Com_Clamp( 0.0f, 0.25f, ( now - sceneMeter.lastMs ) * 0.001f );
	sceneMeter.lastMs = now;
	if ( !sceneMeter.have ) {
		sceneMeter.ev = sceneMeter.target;
		sceneMeter.have = qtrue;
	} else {
		const float rate = MAX( sceneMeter.target > sceneMeter.ev ? up : down, 0.0f );
		sceneMeter.ev += ( sceneMeter.target - sceneMeter.ev ) * ( 1.0f - expf( -rate * dt ) );
	}
	sceneMeter.ev = Com_Clamp( minEV, MAX( minEV, maxEV ), sceneMeter.ev );	// (the window is a bound on what is applied too)
	if ( r_sceneExposureDebug->integer && ( ++sceneMeter.reads % 60 ) == 0 )
		ri.Printf( PRINT_ALL, "scene exposure: meter %.3f (log2 at r_sceneExposure %g), target %+.2f EV, now %+.2f EV: %g%s\n", sceneMeter.meter,
			base, sceneMeter.target, sceneMeter.ev, base * exp2f( sceneMeter.ev ), Env( r_sceneAutoExposure )->integer ? "" : " (off: not applied)" );
}

void vk_scene_haze( void )
{
	vkSecTimer_t tSec( VK_SEC_SCENEHAZE );	// r_cpuTimes
	if ( !sc.hazePending )
		return;
	sc.hazePending = qfalse;
	const int f = vk.cmd_index;
	VkCommandBuffer cb = vk.cmd->command_buffer;
	const qboolean timed = vk_gputime_section_begin( "scene haze" );
	// the cascades' depth writes before the volume reads them
	Scene_Barrier( VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
	VkImageMemoryBarrier ib[3];
	const int scatter = f == 1 ? 2 : 0;
	for ( int i = 0; i < 3; i++ ) {
		const int img = i == 0 ? scatter : i == 1 ? 1 : 2 - scatter;	// 2: the other slot's scatter, last frame's
		Com_Memset( &ib[i], 0, sizeof( ib[i] ) );
		ib[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		ib[i].srcAccessMask = ( i == 2 && sc.hazeInit ) ? VK_ACCESS_SHADER_WRITE_BIT : 0;
		ib[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		ib[i].oldLayout = ( i == 2 && sc.hazeInit ) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
		ib[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ib[i].image = sc.fogImage[img];
		ib[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ib[i].subresourceRange.levelCount = 1;
		ib[i].subresourceRange.layerCount = 1;
	}
	sc.hazeInit = qtrue;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, NULL, 0, NULL, 3, ib );
	sc.hazeRan = sc.fogOn;	// the next frame's history is this one's scatter
	if ( sc.fogOn ) {
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.fogPipeLayout, 0, 1, &sc.fogSet[f], 0, NULL );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.fogPipe[0] );
		qvkCmdDispatch( cb, ( SCENE_FOG_W + 7 ) / 8, ( SCENE_FOG_H + 7 ) / 8, SCENE_FOG_D );
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.fogPipe[1] );
		qvkCmdDispatch( cb, ( SCENE_FOG_W + 7 ) / 8, ( SCENE_FOG_H + 7 ) / 8, 1 );
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
	}
	vk_gputime_section_end( timed );
}

// vk_one_resolve_pass, after the main view's MSAA resolve and depth extract (outside any render pass): the frame's
// colour and view depth into the reflections' history, with this view's camera for the next frame's rays.
// haveDepthExtract: the extract holds this frame's depth already (world AO, the character pass, smoke)
// this view's reflections want a capture (the view the haze was lit for, reflections and wet ground on, the depth
// extract available, the world drawn offscreen)
qboolean vk_scene_capture_wanted( void )
{
	const int v = backEnd.viewParms.sceneView - 1;
	if ( !sc.loaded || !r_scene->integer || !sc.capturePipe || v < 0 || v >= sc.culledViews || v != sc.fogEyeView )
		return qfalse;
	if ( !vk.depth.extract.enabled || !vk.depth.extract.image_view || !vk.fboActive )
		return qfalse;
	// the reflections, or r_sceneAO (traced in the same pass)
	const qboolean reflect = ( r_sceneReflect->value > 0.0f && ( Env( r_sceneWet )->value > 0.0f || sc.numWater ) ) ? qtrue : qfalse;
	return ( reflect || Env( r_sceneAO )->value > 0.0f ) ? qtrue : qfalse;
}

// after the main view's render pass ends: with MSAA its resolve (vk_one_resolve_pass), without it the single-sample
// pass itself (vk_postfx_pass); either leaves the colour image readable
// r_sceneAutoExposure (or r_sceneExposureDebug): the main view's resolved frame into this slot's histogram
// (scene_meter.comp), with the exposure it drew with; read when the slot comes round (Scene_MeterRead)
static void Scene_Meter( void )
{
	const int v = backEnd.viewParms.sceneView - 1;
	if ( !sc.loaded || !r_scene->integer || ( !Env( r_sceneAutoExposure )->integer && !r_sceneExposureDebug->integer ) || v < 0
		|| v >= sc.culledViews || v != sc.fogEyeView || !vk.fboActive || !sc.fogSampler || !Scene_MeterCreate() )
		return;
	const int f = vk.cmd_index;
	VkCommandBuffer cb = vk.cmd->command_buffer;
	const qboolean timed = vk_gputime_section_begin( "scene meter" );
	VkDescriptorImageInfo ii;
	ii.sampler = sc.fogSampler;
	ii.imageView = vk.color_image_view;
	ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet w;
	Com_Memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = sceneMeter.set[f];	// this slot's last use (two frames ago) is complete
	w.dstBinding = 0;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &ii;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	VkMemoryBarrier mb;
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &mb, 0, NULL, 0, NULL );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sceneMeter.pipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sceneMeter.pipeLayout, 0, 1, &sceneMeter.set[f], 0, NULL );
	const float push[4] = { (float)Env( r_sceneTonemap )->integer, (float)SCENE_METER_W, (float)SCENE_METER_H, 0.0f };
	qvkCmdPushConstants( cb, sceneMeter.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), push );
	qvkCmdDispatch( cb, ( SCENE_METER_W + 15 ) / 16, ( SCENE_METER_H + 15 ) / 16, 1 );
	mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL );
	sceneMeter.exposure[f] = MAX( sceneMeter.drawn, 1e-6f );
	vk_gputime_section_end( timed );
}

void vk_scene_capture( qboolean haveDepthExtract )
{
	vkSecTimer_t tSec( VK_SEC_SCENECAPTURE );	// r_cpuTimes
	const int f = vk.cmd_index;

	Scene_Meter();
	if ( !vk_scene_capture_wanted() )
		return;
	if ( !haveDepthExtract )
		vk_depth_extract( glConfig.vidWidth, glConfig.vidHeight );

	VkCommandBuffer cb = vk.cmd->command_buffer;
	qboolean timed = vk_gputime_section_begin( "scene capture" );
	VkDescriptorImageInfo ii[4];
	VkWriteDescriptorSet wr[4];
	ii[3].sampler = VK_NULL_HANDLE;
	ii[3].imageView = sc.historyDepthView ? sc.historyDepthView : sc.historyView;	// (no copy: unused, a valid image)
	ii[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	ii[0].sampler = sc.fogSampler;
	ii[0].imageView = vk.color_image_view;
	ii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ii[1].sampler = sc.nearestSampler;
	ii[1].imageView = vk.depth.extract.image_view;
	ii[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ii[2].sampler = VK_NULL_HANDLE;
	ii[2].imageView = sc.historyStoreView ? sc.historyStoreView : sc.historyView;
	ii[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	for ( int i = 0; i < 4; i++ ) {
		Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
		wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wr[i].dstSet = sc.captureSet[f];	// this slot's last use (two frames ago) is complete
		wr[i].dstBinding = i;
		wr[i].descriptorCount = 1;
		wr[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		wr[i].pImageInfo = &ii[i];
	}
	qvkUpdateDescriptorSets( vk.device, 4, wr, 0, NULL );

	// the resolve and the extract written, the last frame's reads of the history done; it is rewritten whole
	VkImageMemoryBarrier hb[2];
	for ( int i = 0; i < 2; i++ ) {
		Com_Memset( &hb[i], 0, sizeof( hb[i] ) );
		hb[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		hb[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		hb[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		hb[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		hb[i].srcQueueFamilyIndex = hb[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		hb[i].image = ( i && sc.historyDepthImage ) ? sc.historyDepthImage : sc.historyImage;
		hb[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		hb[i].subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		hb[i].subresourceRange.layerCount = 1;
	}
	VkMemoryBarrier mb;
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &mb, 0, NULL, sc.historyDepthImage ? 2 : 1, hb );

	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.capturePipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.capturePipeLayout, 0, 1, &sc.captureSet[f], 0, NULL );
	qvkCmdDispatch( cb, ( MAX( glConfig.vidWidth / 2, 1 ) + 7 ) / 8, ( MAX( glConfig.vidHeight / 2, 1 ) + 7 ) / 8, 1 );

	// the history's colour levels, each blitted from the one below (linear: a 2x2 mean)
	if ( sc.historyMips > 1 ) {
		VkMemoryBarrier lb;
		Com_Memset( &lb, 0, sizeof( lb ) );
		lb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		lb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		lb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &lb, 0, NULL, 0, NULL );
		int32_t lw = MAX( glConfig.vidWidth / 2, 1 ), lh = MAX( glConfig.vidHeight / 2, 1 );
		for ( uint32_t L = 1; L < sc.historyMips; L++ ) {
			VkImageBlit b;
			Com_Memset( &b, 0, sizeof( b ) );
			b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			b.srcSubresource.mipLevel = L - 1;
			b.dstSubresource.mipLevel = L;
			b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
			b.srcOffsets[1].x = lw;
			b.srcOffsets[1].y = lh;
			b.srcOffsets[1].z = 1;
			lw = MAX( lw / 2, 1 );
			lh = MAX( lh / 2, 1 );
			b.dstOffsets[1].x = lw;
			b.dstOffsets[1].y = lh;
			b.dstOffsets[1].z = 1;
			qvkCmdBlitImage( cb, sc.historyImage, VK_IMAGE_LAYOUT_GENERAL, sc.historyImage, VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_LINEAR );
			lb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			lb.dstAccessMask = L + 1 < sc.historyMips ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
			qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, L + 1 < sc.historyMips ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 1, &lb, 0, NULL, 0, NULL );
		}
	}

	// the reflections: traced on the history just written, settled into the other image with last frame's result
	const qboolean settle = ( sc.capturedAt + 1 == sc.frameNo && sc.capturedAt != 0 ) ? qtrue : qfalse;
	const int cur = sc.reflectIndex ^ 1, prev = sc.reflectIndex;
	sceneSsrParams_t *sp = (sceneSsrParams_t *)sc.ssrParamsPtr[f];
	float proj[16], viewProj[16];
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	myGlMultMatrix( backEnd.viewParms.world.modelViewMatrix, proj, viewProj );
	Com_Memcpy( sp->viewProj, viewProj, sizeof( viewProj ) );
	Com_Memcpy( sp->prevViewProj, sc.prevViewProj, sizeof( sp->prevViewProj ) );
	VectorCopy( backEnd.viewParms.ori.origin, sp->eye );
	sp->eye[3] = (float)( (uint32_t)sc.signalTime & 1023u );	// the jitter's seed: the game's clock, so a replayed frame repeats (not the frames drawn since load)
	VectorCopy( backEnd.viewParms.ori.axis[0], sp->fwd );
	sp->fwd[3] = tanf( DEG2RAD( backEnd.viewParms.fovX * 0.5f ) );
	VectorCopy( backEnd.viewParms.ori.axis[1], sp->left );
	sp->left[3] = tanf( DEG2RAD( backEnd.viewParms.fovY * 0.5f ) );
	VectorCopy( backEnd.viewParms.ori.axis[2], sp->up );
	sp->up[3] = MAX( r_sceneReflectFar->value, 1.0f ) * 37.5f;
	VectorCopy( sc.prevEye, sp->prevEye );
	sp->prevEye[3] = (float)Com_Clamp( 4, 64, r_sceneReflectSteps->integer );
	VectorCopy( sc.prevFwd, sp->prevFwd );
	sp->prevFwd[3] = settle ? 1.0f : 0.0f;
	{
		// the haze's colour as linear light before exposure (scene.frag's fromDisplay)
		vec3_t fog = { 0.0f, 0.0f, 0.0f };	// (an unparsed r_sceneFog: no haze colour)
		sscanf( Env( r_sceneFog )->string, "%f %f %f", &fog[0], &fog[1], &fog[2] );
		const float exposure = MAX( Scene_Exposure(), 1e-3f );
		for ( int k = 0; k < 3; k++ ) {
			const float y = powf( Com_Clamp( 0.0f, 0.999f, fog[k] / 255.0f ), 2.2f );
			float x;
			if ( Env( r_sceneTonemap )->integer ) {
				const float a = 2.51f - 2.43f * y, b = 0.03f - 0.59f * y, c = -0.14f * y;
				x = ( -b + sqrtf( b * b - 4.0f * a * c ) ) / ( 2.0f * a );
			} else {
				x = -logf( 1.0f - y );
			}
			sp->sky[k] = x / exposure;
		}
		sp->sky[3] = Com_Clamp( 0.02f, 1.0f, r_sceneSsrBlend->value );
		sp->wet[0] = Com_Clamp( 0.0f, 1.0f, Env( r_sceneWet )->value );
		sp->wet[1] = Com_Clamp( 0.0f, 1.0f, r_sceneWetPuddles->value );
		sp->wet[2] = exposure;
		sp->wet[3] = (float)Env( r_sceneTonemap )->integer;
		sp->opts[0] = r_sceneSsrRays->integer >= 2 ? 2.0f : 1.0f;
		// the water's band (traced at the water's gloss, also when nothing else is wet)
		sp->opts[1] = sc.waterLo;
		sp->opts[2] = sc.waterHi;
		sp->opts[3] = sc.numWater ? 1.0f : 0.0f;
		sp->opts2[0] = r_sceneSsrFlat->value > 0.0f ? Com_Clamp( 0.5f, 1.0f, r_sceneSsrFlat->value ) : 2.0f;
		// r_sceneAO: the reach (units), the samples (0 off), a new frame's share
		const qboolean ao = Env( r_sceneAO )->value > 0.0f ? qtrue : qfalse;
		sp->opts2[1] = Com_Clamp( 0.05f, 8.0f, Env( r_sceneAORadius )->value ) * 37.5f;
		sp->opts2[2] = ao ? (float)Com_Clamp( 1, 32, r_sceneAOSamples->integer ) : 0.0f;
		sp->opts2[3] = Com_Clamp( 0.02f, 1.0f, r_sceneAOBlend->value );
	}

	VkDescriptorImageInfo si[7];
	VkDescriptorBufferInfo sb;
	VkWriteDescriptorSet sw[7];
	si[5].sampler = VK_NULL_HANDLE;	// r_sceneAO's: this frame's (written), last frame's (read)
	si[5].imageView = sc.aoView[cur];
	si[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	si[6].sampler = sc.nearestSampler;
	si[6].imageView = sc.aoView[prev];
	si[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	si[4].sampler = sc.fogSampler;	// linear, as the history's
	si[4].imageView = sc.historyDepthView ? sc.historyDepthView : sc.historyView;	// (no copy: the history's alpha)
	si[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	si[0].sampler = sc.historyMips > 1 ? sc.mipSampler : sc.fogSampler;	// linear (across its levels): the history's colour where a ray lands
	si[0].imageView = sc.historyView;
	si[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	si[1].sampler = sc.nearestSampler;
	si[1].imageView = sc.reflectView[prev];
	si[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	si[2].sampler = VK_NULL_HANDLE;
	si[2].imageView = sc.reflectView[cur];
	si[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	sb.buffer = sc.ssrParams[f].buf;
	sb.offset = 0;
	sb.range = VK_WHOLE_SIZE;
	for ( int i = 0; i < 7; i++ ) {
		Com_Memset( &sw[i], 0, sizeof( sw[i] ) );
		sw[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		sw[i].dstSet = sc.ssrSet[f];
		sw[i].dstBinding = i;
		sw[i].descriptorCount = 1;
		sw[i].descriptorType = ( i < 2 || i == 4 || i == 6 ) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : ( i == 2 || i == 5 ) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		if ( i != 3 )
			sw[i].pImageInfo = &si[i];
		else
			sw[i].pBufferInfo = &sb;
	}
	qvkUpdateDescriptorSets( vk.device, 7, sw, 0, NULL );
	vk_gputime_section_end( timed );
	// async compute (r_sceneAsync): the trace on the compute queue, after this frame's commands so far are submitted
	const qboolean async = ( sc.asyncReady && r_sceneAsync->integer ) ? qtrue : qfalse;
	VkCommandBuffer traceCb = cb;
	if ( async ) {
		Scene_SplitFrame( f );
		cb = vk.cmd->command_buffer;
		// the colour image and the extract free for the passes after this (the capture read them)
		mb.srcAccessMask = 0;
		mb.dstAccessMask = 0;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 1, &mb, 0, NULL, 0, NULL );
		if ( sc.asyncFenceUsed[f] ) {
			VK_CHECK( qvkWaitForFences( vk.device, 1, &sc.asyncFence[f], VK_TRUE, 1000000000ULL * 10 ) );
			VK_CHECK( qvkResetFences( vk.device, 1, &sc.asyncFence[f] ) );
		}
		traceCb = sc.asyncCb[f];
		VkCommandBufferBeginInfo cbi;
		Com_Memset( &cbi, 0, sizeof( cbi ) );
		cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK( qvkBeginCommandBuffer( traceCb, &cbi ) );
	} else {
		timed = vk_gputime_section_begin( "scene ssr" );
	}

	// the history written; the image this writes was last read by the previous frame's fragments (rewritten whole)
	VkImageMemoryBarrier rb[4];
	int nrb = 0;
	for ( int i = 0; i < 4; i++ ) {	// 2, 3: r_sceneAO's
		if ( ( i & 1 ) != cur && sc.reflectInit )
			continue;
		Com_Memset( &rb[nrb], 0, sizeof( rb[nrb] ) );
		rb[nrb].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		rb[nrb].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		rb[nrb].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		rb[nrb].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		rb[nrb].srcQueueFamilyIndex = rb[nrb].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		rb[nrb].image = i < 2 ? sc.reflectImage[i] : sc.aoImage[i - 2];
		rb[nrb].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		rb[nrb].subresourceRange.levelCount = 1;
		rb[nrb].subresourceRange.layerCount = 1;
		nrb++;
	}
	sc.reflectInit = qtrue;
	mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	// (a compute queue knows no fragment stage: there the semaphore already ordered the last frame's reads)
	qvkCmdPipelineBarrier( traceCb, async ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, nrb, rb );
	qvkCmdBindPipeline( traceCb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.ssrPipe );
	qvkCmdBindDescriptorSets( traceCb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.ssrPipeLayout, 0, 1, &sc.ssrSet[f], 0, NULL );
	qvkCmdDispatch( traceCb, ( MAX( glConfig.vidWidth / 2, 1 ) + 7 ) / 8, ( MAX( glConfig.vidHeight / 2, 1 ) + 7 ) / 8, 1 );

	if ( async ) {
		// waits for captured, signals traced; the next submit whose fragments read the reflections waits for that
		VK_CHECK( qvkEndCommandBuffer( traceCb ) );
		const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		VkSubmitInfo s;
		Com_Memset( &s, 0, sizeof( s ) );
		s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		s.waitSemaphoreCount = 1;
		s.pWaitSemaphores = &sc.captured[f];
		s.pWaitDstStageMask = &stage;
		s.commandBufferCount = 1;
		s.pCommandBuffers = &traceCb;
		s.signalSemaphoreCount = 1;
		s.pSignalSemaphores = &sc.traced[f];
		VK_CHECK( qvkQueueSubmit( vk.compute_queue, 1, &s, sc.asyncFence[f] ) );
		sc.asyncFenceUsed[f] = qtrue;
		sc.pendingWait = sc.traced[f];
		sc.pendingFresh = qtrue;
	} else {
		// the reflections for the next frame's fragments; the colour image and the extract free for the passes after this
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 1, &mb, 0, NULL, 0, NULL );
		vk_gputime_section_end( timed );
	}

	Com_Memcpy( sc.prevViewProj, viewProj, sizeof( viewProj ) );
	VectorCopy( backEnd.viewParms.ori.origin, sc.prevEye );
	VectorCopy( backEnd.viewParms.ori.axis[0], sc.prevFwd );
	sc.reflectIndex = cur;
	sc.capturedAt = sc.frameNo;
}

// vk_one_resolve_pass, inside the post 1x pass after its SSAO, glow and smoke blends (before bloom and the HUD): the
// map's colour grade over the main view. The pass ends, the resolved frame is copied aside, and the pass begins again
// (colour loaded) with a full-frame draw reading the copy through the grade. Debug views are left raw.
void vk_scene_grade( void )
{
	vkSecTimer_t tSec( VK_SEC_SCENEGRADE );	// r_cpuTimes
	const int v = backEnd.viewParms.sceneView - 1;

	if ( !sc.loaded || !sc.gradeReady || !r_scene->integer || Env( r_sceneGrade )->value <= 0.0f || r_sceneDebug->integer
		|| v < 0 || v >= sc.culledViews || v != sc.fogEyeView || vk.renderPassIndex != RENDER_PASS_POST_1X )
		return;

	VkCommandBuffer cb = vk.cmd->command_buffer;
	vk_end_render_pass();	// the colour image back in SHADER_READ_ONLY
	const vec4_t push = { (float)Env( r_sceneTonemap )->integer, MAX( Env( r_sceneGradeExposure )->value, 0.0f ), Com_Clamp( 0.0f, 1.0f, Env( r_sceneGrade )->value ), 0.0f };

	if ( sc.gradeCompReady && vk.colorStorage && r_sceneGradeCompute->integer ) {
		// in place: each pixel graded where it is, then the pass begins again (colour loaded)
		const int f = vk.cmd_index;
		if ( sc.gradeCompView[f] != vk.color_image_view ) {	// (the slot's last use of the set is fenced)
			VkDescriptorImageInfo ii[2];
			VkWriteDescriptorSet wr[2];
			ii[0].sampler = VK_NULL_HANDLE;
			ii[0].imageView = vk.color_image_view;
			ii[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			ii[1].sampler = sc.fogSampler;		// linear, clamped
			ii[1].imageView = sc.gradeLutView;
			ii[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			for ( int i = 0; i < 2; i++ ) {
				Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
				wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wr[i].dstSet = sc.gradeCompSet[f];
				wr[i].dstBinding = i;
				wr[i].descriptorCount = 1;
				wr[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				wr[i].pImageInfo = &ii[i];
			}
			qvkUpdateDescriptorSets( vk.device, 2, wr, 0, NULL );
			sc.gradeCompView[f] = vk.color_image_view;
		}
		const qboolean timed = vk_gputime_section_begin( "grade" );
		Scene_ImageBarrier( cb, vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.gradeCompPipe );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.gradeCompPipeLayout, 0, 1, &sc.gradeCompSet[f], 0, NULL );
		qvkCmdPushConstants( cb, sc.gradeCompPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), push );
		qvkCmdDispatch( cb, ( glConfig.vidWidth + 7 ) / 8, ( glConfig.vidHeight + 7 ) / 8, 1 );
		Scene_ImageBarrier( cb, vk.color_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT );
		vk_gputime_section_end( timed );
		vk_begin_post1x_render_pass();
		return;
	}

	const qboolean timed = vk_gputime_section_begin( "grade copy" );

	Scene_ImageBarrier( cb, vk.color_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
	// last frame's reads of the copy are done (the frame's earlier passes ran before this one on the queue)
	Scene_ImageBarrier( cb, sc.gradeCopy, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
	VkImageCopy region;
	Com_Memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
	region.extent.width = glConfig.vidWidth;
	region.extent.height = glConfig.vidHeight;
	region.extent.depth = 1;
	qvkCmdCopyImage( cb, vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sc.gradeCopy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	Scene_ImageBarrier( cb, vk.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT );
	Scene_ImageBarrier( cb, sc.gradeCopy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );
	vk_gputime_section_end( timed );

	vk_gputime_next_label( "grade" );
	vk_begin_post1x_render_pass();
	VkViewport vp = { 0.0f, 0.0f, (float)glConfig.vidWidth, (float)glConfig.vidHeight, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { (uint32_t)glConfig.vidWidth, (uint32_t)glConfig.vidHeight } };
	qvkCmdSetViewport( cb, 0, 1, &vp );
	qvkCmdSetScissor( cb, 0, 1, &scissor );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.gradePipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.gradePipeLayout, 0, 1, &sc.gradeSet, 0, NULL );
	qvkCmdPushConstants( cb, sc.gradePipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), push );
	qvkCmdDraw( cb, 3, 1, 0, 0 );
	vk_gputime_split( "post 1x" );	// the HUD and the rest of the pass under the pass's own name
}

// R_SceneVkBeginFrame: slot f's histogram read, then the exposure this frame draws with (its frame block's)
float Scene_MeterFrame( int f )
{
	Scene_MeterRead( f );
	sceneMeter.drawn = Scene_Exposure();
	return sceneMeter.drawn;
}
