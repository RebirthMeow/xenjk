/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

#include "tr_local.h"

enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

typedef struct {
	uint32_t target;

	VkFormat format;
	uint32_t size;
	uint32_t mipLevels;
	
	VkRenderPass		renderpass;
	VkPipeline			pipeline;
	VkPipelineLayout	pipeline_layout;

	struct {
		VkShaderModule	*vs_module;
		VkShaderModule	*gm_module;
		VkShaderModule	*fs_module;	
	} shaders;

	struct {
		VkImage			image;
		VkImageView		view;
		VkDeviceMemory	memory;
		VkFramebuffer	framebuffer;
	} offscreen;
} filterDef;

static filterDef prefilters[2];

static void set_shader_stage_desc( VkPipelineShaderStageCreateInfo *desc, VkShaderStageFlagBits stage, VkShaderModule shader_module, const char *entry ) {
    desc->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    desc->pNext = NULL;
    desc->flags = 0;
    desc->stage = stage;
    desc->module = shader_module;
    desc->pName = entry;
    desc->pSpecializationInfo = NULL;
}

static void vk_create_prefilter_renderpass( filterDef *def ) 
{
	VkAttachmentReference	color_attachment_ref;
	VkSubpassDependency		deps[2];
	VkAttachmentDescription	attachment;
	VkRenderPassCreateInfo	desc;
	VkSubpassDescription	subpass;

	// Color attachment
	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.format = def->format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	// subpass dependencies
	Com_Memset( &deps, 0, sizeof( deps ) );

	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.attachmentCount = 1;
	desc.pAttachments = &attachment;
	desc.subpassCount = 1;
	desc.pSubpasses = &subpass;
	desc.dependencyCount = 2;
	desc.pDependencies = deps;

	VK_CHECK( qvkCreateRenderPass( vk.device, &desc, nullptr, &def->renderpass ) );
}

static void vk_create_prefilter_framebuffer( filterDef *def ) {
	VkCommandBuffer			command_buffer;
	VkMemoryRequirements	memory_requirements;
	VkMemoryAllocateInfo	alloc_info;

	// create offscreen image to copy from
	{
		VkImageCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		desc.format = def->format;
		desc.extent.width = def->size;
		desc.extent.height = def->size;
		desc.extent.depth = 1;
		desc.mipLevels = 1;
		desc.arrayLayers = 6;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK( qvkCreateImage( vk.device, &desc, nullptr, &def->offscreen.image ) );
	}

	qvkGetImageMemoryRequirements( vk.device, def->offscreen.image, &memory_requirements);
	
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = vk_find_memory_type( memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
			
	VK_CHECK( vk_vram_allocate_named( vk.device, &alloc_info, &def->offscreen.memory, "cubemap" ) );
	VK_CHECK( qvkBindImageMemory( vk.device, def->offscreen.image, def->offscreen.memory, 0 ) );

	// create image view
	{
		VkImageViewCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		desc.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		desc.format = def->format;
		desc.flags = 0;
		desc.subresourceRange = {};
		desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.subresourceRange.baseMipLevel = 0;
		desc.subresourceRange.levelCount = 1;
		desc.subresourceRange.baseArrayLayer = 0;
		desc.subresourceRange.layerCount = 6;
		desc.image = def->offscreen.image;
		VK_CHECK( qvkCreateImageView( vk.device, &desc, nullptr, &def->offscreen.view ) );
	}

	// create framebuffer
	{
		VkFramebufferCreateInfo desc{};
		desc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		desc.renderPass = def->renderpass;
		desc.attachmentCount = 1;
		desc.pAttachments = &def->offscreen.view;
		desc.width = def->size;
		desc.height = def->size;
		desc.layers = 6;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &desc, nullptr, &def->offscreen.framebuffer));
	}

	command_buffer = vk_begin_command_buffer();
	vk_record_image_layout_transition( command_buffer, def->offscreen.image, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_UNDEFINED, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		0 ,0 );
	vk_end_command_buffer( command_buffer, __func__ );
}

static void vk_create_prefilter_pipeline( filterDef *def ) 
{
	VkPipelineShaderStageCreateInfo			shader_stages[3];
	VkPipelineVertexInputStateCreateInfo	vertex_input_state{};
	VkPipelineInputAssemblyStateCreateInfo	input_assembly_state;
	VkPipelineViewportStateCreateInfo		viewport_state{};
	VkPipelineRasterizationStateCreateInfo	rasterization_state{};
	VkPipelineMultisampleStateCreateInfo	multisample_state{};
	VkPipelineDepthStencilStateCreateInfo	depth_stencil_state{};
	VkPipelineColorBlendAttachmentState		attachment_blend_state{};
	VkPipelineColorBlendStateCreateInfo		blend_state{};
	VkPipelineDynamicStateCreateInfo		dynamic_state;
	VkDynamicState							dynamic_state_array[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkGraphicsPipelineCreateInfo			create_info{};
	VkPipelineLayoutCreateInfo				pipeline_layout;
	VkPushConstantRange						push_range;

	push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	push_range.offset = 0;

	pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout.pNext = NULL;
	pipeline_layout.flags = 0;
	pipeline_layout.setLayoutCount = 1;
	pipeline_layout.pSetLayouts = &vk.set_layout_sampler;

	if ( def->target == PREFILTEREDENV ) {
		push_range.size = sizeof(float);
		pipeline_layout.pushConstantRangeCount = 1;
		pipeline_layout.pPushConstantRanges = &push_range;
	} else {
		pipeline_layout.pushConstantRangeCount = 0;
		pipeline_layout.pPushConstantRanges = NULL;
	}

	VK_CHECK( qvkCreatePipelineLayout( vk.device, &pipeline_layout, nullptr, &def->pipeline_layout ) );
	
	input_assembly_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.pNext = NULL;
    input_assembly_state.flags = 0;
    input_assembly_state.primitiveRestartEnable = VK_FALSE;	
	input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	rasterization_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization_state.cullMode = VK_CULL_MODE_NONE;
	rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization_state.lineWidth = 1.0f;

	attachment_blend_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	attachment_blend_state.blendEnable = VK_FALSE;

	blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend_state.attachmentCount = 1;
	blend_state.pAttachments = &attachment_blend_state;

	depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth_stencil_state.depthTestEnable = VK_FALSE;
	depth_stencil_state.depthWriteEnable = VK_FALSE;
	depth_stencil_state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depth_stencil_state.front = depth_stencil_state.back;
	depth_stencil_state.back.compareOp = VK_COMPARE_OP_ALWAYS;

	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	multisample_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
				
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.pNext = NULL;
    dynamic_state.flags = 0;  
	dynamic_state.dynamicStateCount = ARRAY_LEN( dynamic_state_array );
    dynamic_state.pDynamicStates = dynamic_state_array;
	
	vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state.vertexBindingDescriptionCount = 0;
	vertex_input_state.pVertexBindingDescriptions = NULL;
	vertex_input_state.vertexAttributeDescriptionCount = 0;
	vertex_input_state.pVertexAttributeDescriptions = NULL;

	set_shader_stage_desc( shader_stages + 0, VK_SHADER_STAGE_VERTEX_BIT, *def->shaders.vs_module, "main" );
	set_shader_stage_desc( shader_stages + 1, VK_SHADER_STAGE_FRAGMENT_BIT, *def->shaders.fs_module, "main" );
	set_shader_stage_desc( shader_stages + 2, VK_SHADER_STAGE_GEOMETRY_BIT, *def->shaders.gm_module, "main" );

	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.layout = def->pipeline_layout;
	create_info.renderPass = def->renderpass;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pVertexInputState = &vertex_input_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pColorBlendState = &blend_state;
	create_info.pMultisampleState = &multisample_state;
	create_info.pViewportState = &viewport_state;
	create_info.pDepthStencilState = &depth_stencil_state;
	create_info.pDynamicState = &dynamic_state;
	create_info.stageCount = ARRAY_LEN(shader_stages);
	create_info.pStages = shader_stages;

	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, vk.pipelineCache, 1, &create_info, nullptr, &def->pipeline ) );	
}

void vk_create_cubemap_prefilter( void )
{
	if ( !vk.cubemapActive )
		return;

	uint32_t	i;
	filterDef	*def;

	Com_Memset( &prefilters, 0, sizeof( prefilters ) );

	for ( i = 0; i < PREFILTEREDENV + 1; i++ ) 
	{
		def = &prefilters[i];

		def->target = i;
		def->shaders.vs_module = &vk.shaders.filtercube_vs;
		def->shaders.gm_module = &vk.shaders.filtercube_gm;

		switch ( def->target ) {
			case IRRADIANCE:
				def->format = VK_FORMAT_R32G32B32A32_SFLOAT;
				def->size = 64;
				def->shaders.fs_module = &vk.shaders.irradiancecube_fs;
				def->mipLevels = static_cast<uint32_t>(floor(log2(def->size))) + 1;
				break;
			case PREFILTEREDENV:
				def->format = VK_FORMAT_R16G16B16A16_SFLOAT;
				def->size = 256;
				def->shaders.fs_module = &vk.shaders.prefilterenvmap_fs;
				def->mipLevels = static_cast<uint32_t>(floor(log2(def->size))) + 1;
				break;
		};

		vk_create_prefilter_renderpass( def );
		vk_create_prefilter_framebuffer( def );
		vk_create_prefilter_pipeline( def );
	}
}

void vk_destroy_cubemap_prefilter( void ){

	uint32_t	i;
	filterDef	*def;

	for ( i = 0; i < PREFILTEREDENV + 1; i++ ) 
	{
		def = &prefilters[i];

		qvkDestroyRenderPass( vk.device, def->renderpass, NULL );
		qvkDestroyFramebuffer( vk.device, def->offscreen.framebuffer, NULL );
		qvkFreeMemory( vk.device, def->offscreen.memory, NULL );
		qvkDestroyImageView( vk.device, def->offscreen.view, NULL );
		qvkDestroyImage( vk.device, def->offscreen.image, NULL );
		qvkDestroyPipeline( vk.device, def->pipeline, NULL );
		qvkDestroyPipelineLayout( vk.device, def->pipeline_layout, NULL );
	}

	Com_Memset( &prefilters, 0, sizeof( prefilters ) );
}

static void vk_copy_to_cubemap( filterDef *def, VkImage *image, uint32_t mipLevel, uint32_t size ) 
{	
	VkImageCopy region;
	
	// change image layout for all offsceen faces to transfer source
	vk_record_image_layout_transition( vk.cmd->command_buffer, def->offscreen.image, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		0 ,0 );

	Com_Memset( &region, 0, sizeof( VkImageCopy ) );
	region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.baseArrayLayer = 0;
	region.srcSubresource.mipLevel = 0;
	region.srcSubresource.layerCount = 6;
	region.srcOffset = { 0, 0, 0 };

	region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.dstSubresource.baseArrayLayer = 0;
	region.dstSubresource.mipLevel = mipLevel;
	region.dstSubresource.layerCount = 6;
	region.dstOffset = { 0, 0, 0 };

	region.extent.width = region.extent.height = size;
	region.extent.depth = 1;

	qvkCmdCopyImage( vk.cmd->command_buffer, def->offscreen.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, *image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	vk_record_image_layout_transition( vk.cmd->command_buffer, def->offscreen.image, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		0 ,0 );
}

void vk_generate_cubemaps( cubemap_t *cube ) 
{
	VkRenderPassBeginInfo	begin_info{};
	VkViewport				viewport;
	VkRect2D				scissor_rect;
	VkClearValue			clear_values[1];
	VkCommandBuffer			command_buffer;

	image_t		*cubemap = NULL;
	uint32_t	i, j;
	filterDef	*def;

	vk_end_render_pass();

	command_buffer = vk_begin_command_buffer();
	vk_record_image_layout_transition( command_buffer, vk.cubeMap.color_image, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		0 ,0 );
	vk_end_command_buffer( command_buffer, __func__ );

	for ( i = 0; i < PREFILTEREDENV + 1; i++ ) 
	{
		def = &prefilters[i];

		switch ( def->target ) {
			case IRRADIANCE: cubemap = cube->irradiance_image; break;
			case PREFILTEREDENV: cubemap = cube->prefiltered_image; break;
		};

		if ( !cubemap )
			continue;

		Com_Memset( clear_values, 0, sizeof( clear_values ) );
		clear_values[0].color = { { 0.75f, 0.75f, 0.75f, 0.0f } };

		begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin_info.renderPass = def->renderpass;
		begin_info.framebuffer = def->offscreen.framebuffer;
		begin_info.renderArea.extent.width = def->size;
		begin_info.renderArea.extent.height = def->size;
		begin_info.clearValueCount = 1;
		begin_info.pClearValues = clear_values;

		Com_Memset( &viewport, 0, sizeof( viewport ) );
		viewport.width = viewport.height = (float)def->size;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		Com_Memset( &scissor_rect, 0, sizeof( scissor_rect ) );
		scissor_rect.extent.width = scissor_rect.extent.height = def->size;

		// change image layout for all cubemap faces to transfer destination
		vk_record_image_layout_transition( vk.cmd->command_buffer, cubemap->handle, VK_IMAGE_ASPECT_COLOR_BIT, 
			VK_IMAGE_LAYOUT_UNDEFINED, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0 ,0 );
			
		for ( j = 0; j < def->mipLevels; j++ ) {
			qvkCmdSetViewport( vk.cmd->command_buffer, 0, 1, &viewport );
			qvkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor_rect );

			// render scene from cube face's point of view
			qvkCmdBeginRenderPass(vk.cmd->command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

			if ( def->target == PREFILTEREDENV ) {
				// perceptual roughness j / VK_CUBEMAP_ROUGH_MIPS, as gen_frag samples it (lod = roughness x 6); was
				// j / (mipLevels - 1) = j / 8, which drew every probe reflection 3/4 as rough as its material
				float roughness = Q_min( (float)j / (float)VK_CUBEMAP_ROUGH_MIPS, 1.0f );
				qvkCmdPushConstants( vk.cmd->command_buffer, def->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(roughness), &roughness );
			}

			qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, def->pipeline );
			qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, def->pipeline_layout, 0, 1, &vk.cubeMap.color_descriptor, 0, NULL );
			qvkCmdDraw( vk.cmd->command_buffer, 3, 1, 0, 0 );
			qvkCmdEndRenderPass( vk.cmd->command_buffer );

			vk_copy_to_cubemap( def, &cubemap->handle, j, static_cast<uint32_t>(viewport.width) );
		
			viewport.width /= 2;
			viewport.height /= 2;
		}

		vk_record_image_layout_transition( vk.cmd->command_buffer, cubemap->handle, VK_IMAGE_ASPECT_COLOR_BIT, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			0 ,0 );
	}

	command_buffer = vk_begin_command_buffer();
	vk_record_image_layout_transition( command_buffer, vk.cubeMap.color_image, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		0 ,0 );
	vk_end_command_buffer( command_buffer, __func__ );

	vk_begin_main_render_pass();
}

// write one probe-blend set: slot i samples images[i] (prefiltered maps are sampled with an explicit lod) at binding 0
// and irradiance[i] at binding 1
static void vk_write_cubemap_blend_set( VkDescriptorSet set, image_t **images, image_t **irradiance )
{
	Vk_Sampler_Def			sampler_def;
	VkDescriptorImageInfo	image_info[2][VK_CUBEMAP_BLEND_PROBES];
	VkWriteDescriptorSet	write[2];
	uint32_t				i, b;

	Com_Memset( &sampler_def, 0, sizeof( sampler_def ) );
	sampler_def.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_def.gl_mag_filter = GL_LINEAR;
	sampler_def.gl_min_filter = GL_LINEAR_MIPMAP_LINEAR;
	sampler_def.noAnisotropy = qtrue;

	Com_Memset( write, 0, sizeof( write ) );
	for ( b = 0; b < 2; b++ ) {
		for ( i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ ) {
			image_info[b][i].sampler = vk_find_sampler( &sampler_def );
			image_info[b][i].imageView = ( b ? irradiance : images )[i]->view;
			image_info[b][i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		write[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write[b].dstSet = set;
		write[b].dstBinding = b;
		write[b].dstArrayElement = 0;
		write[b].descriptorCount = VK_CUBEMAP_BLEND_PROBES;
		write[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write[b].pImageInfo = image_info[b];
	}
	qvkUpdateDescriptorSets( vk.device, 2, write, 0, NULL );
}

static VkDescriptorSet vk_alloc_cubemap_blend_set( void )
{
	return vk_alloc_descriptor_set( vk.set_layout_cubemap, "a probe blend" );
}

/*
=============
vk_create_cubemap_blend_descriptors

After the map's probes are rendered: each probe's set holds its own and its neighbours' prefiltered maps
(cubemap_t blendProbes); unused slots repeat the probe's own map and get no weight. The sets come from the
descriptor pool, which is reset with the level's resources, and are made again with the next map's probes.
=============
*/
void vk_create_cubemap_blend_descriptors( void )
{
	image_t	*images[VK_CUBEMAP_BLEND_PROBES], *irradiance[VK_CUBEMAP_BLEND_PROBES];
	int		i, j;

	for ( i = 0; i < tr.numCubemaps; i++ )
	{
		cubemap_t *cube = &tr.cubemaps[i];

		cube->blendDescriptor = VK_NULL_HANDLE;
		if ( !cube->prefiltered_image )
			continue;

		for ( j = 0; j < VK_CUBEMAP_BLEND_PROBES; j++ ) {
			const cubemap_t *probe = j < cube->numBlendProbes ? &tr.cubemaps[cube->blendProbes[j]] : cube;
			images[j] = probe->prefiltered_image ? probe->prefiltered_image : cube->prefiltered_image;
			irradiance[j] = probe->irradiance_image ? probe->irradiance_image :
				cube->irradiance_image ? cube->irradiance_image : tr.emptyCubemap;
		}

		cube->blendDescriptor = vk_alloc_cubemap_blend_set();
		vk_write_cubemap_blend_set( cube->blendDescriptor, images, irradiance );
	}
}

// the set bound for draws without a probe (and while the probes themselves are rendered): the empty cubemap
VkDescriptorSet vk_cubemap_empty_blend_descriptor( void )
{
	image_t	*images[VK_CUBEMAP_BLEND_PROBES];
	int		i;

	if ( vk.cubeMap.empty_blend_descriptor == VK_NULL_HANDLE )
	{
		for ( i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ )
			images[i] = tr.emptyCubemap;

		vk.cubeMap.empty_blend_descriptor = vk_alloc_cubemap_blend_set();
		vk_write_cubemap_blend_set( vk.cubeMap.empty_blend_descriptor, images, images );
	}

	return vk.cubeMap.empty_blend_descriptor;
}

// the set bound for RDF_STAGE scenes (a menu's character stage): the studio grey in every slot
VkDescriptorSet vk_cubemap_stage_blend_descriptor( void )
{
	image_t	*images[VK_CUBEMAP_BLEND_PROBES];
	int		i;

	if ( vk.cubeMap.stage_blend_descriptor == VK_NULL_HANDLE )
	{
		for ( i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ )
			images[i] = tr.stageCubemap;

		vk.cubeMap.stage_blend_descriptor = vk_alloc_cubemap_blend_set();
		vk_write_cubemap_blend_set( vk.cubeMap.stage_blend_descriptor, images, images );
	}

	return vk.cubeMap.stage_blend_descriptor;
}

// probe placement at a world load (RE_LoadWorldMap_Actual): each probe's blend neighbours and parallax box

// the BSP leaf holding p, from the world being loaded (tr.world is set only once loading completes)
static const mnode_t *R_CubemapLeaf( const world_t &worldData, const vec3_t p )
{
	const mnode_t *node = worldData.nodes;

	while ( node->contents == -1 )
		node = ( DotProduct( p, node->plane->normal ) - node->plane->dist > 0 ) ? node->children[0] : node->children[1];

	return node;
}

/*
=================
R_FitCubemapBox

The parallax box of a probe (r_cubeMapParallax 1): for each axis direction, a 5 x 5 fan of parallel rays (128 units
apart) steps out of the probe until it leaves the playable space (a leaf with no cluster: solid or outside); the
median distance places that face, so a pillar or doorway in front of the probe does not decide the box.
=================
*/
#define CUBEMAP_BOX_STEP	8.0f
#define CUBEMAP_BOX_MAX		4096.0f

static void R_FitCubemapBox( const world_t &worldData, cubemap_t *cube )
{
	int axis, sign, a, b, n, i;

	for ( axis = 0; axis < 3; axis++ )
	{
		const int u = ( axis + 1 ) % 3, v = ( axis + 2 ) % 3;

		for ( sign = -1; sign <= 1; sign += 2 )
		{
			float dist[25];

			n = 0;
			for ( a = -2; a <= 2; a++ )
			{
				for ( b = -2; b <= 2; b++ )
				{
					vec3_t start, p;
					float d;

					VectorCopy( cube->origin, start );
					start[u] += a * 128.0f;
					start[v] += b * 128.0f;

					// a ray that starts in solid (beside a wall) says nothing about this face
					if ( R_CubemapLeaf( worldData, start )->cluster < 0 )
						continue;

					for ( d = CUBEMAP_BOX_STEP; d < CUBEMAP_BOX_MAX; d += CUBEMAP_BOX_STEP ) {
						VectorCopy( start, p );
						p[axis] += sign * d;
						if ( R_CubemapLeaf( worldData, p )->cluster < 0 )
							break;
					}
					dist[n++] = d - CUBEMAP_BOX_STEP * 0.5f;
				}
			}

			// median of the rays that started in the open (the centre ray always does: the probe is in the open)
			for ( i = 1; i < n; i++ ) {
				const float x = dist[i];
				int j = i - 1;
				for ( ; j >= 0 && dist[j] > x; j-- )
					dist[j + 1] = dist[j];
				dist[j + 1] = x;
			}
			const float d = n ? dist[n / 2] : CUBEMAP_BOX_MAX;

			if ( sign < 0 )
				cube->boxMins[axis] = cube->origin[axis] - d;
			else
				cube->boxMaxs[axis] = cube->origin[axis] + d;
		}
	}
}

/*
=================
R_SetCubemapBlendProbes

Each probe's draws blend it with its nearest neighbours per pixel (gen_frag CalcIBLContribution). The draw's probe
is the one nearest the surface or model, so the probes nearest any of its pixels are almost always among these.
Also fits each probe's parallax box.
=================
*/
void R_SetCubemapBlendProbes( const world_t &worldData )
{
	int i, j, k, n;

	for ( i = 0; i < tr.numCubemaps; i++ )
	{
		cubemap_t *cube = &tr.cubemaps[i];
		float dist[VK_CUBEMAP_BLEND_PROBES];

		cube->blendProbes[0] = i;
		dist[0] = 0.0f;
		n = 1;

		for ( j = 0; j < tr.numCubemaps; j++ )
		{
			vec3_t diff;
			float d;

			if ( j == i )
				continue;

			VectorSubtract( tr.cubemaps[j].origin, cube->origin, diff );
			d = DotProduct( diff, diff );

			// insertion into the sorted list, dropping the farthest when full
			if ( n == VK_CUBEMAP_BLEND_PROBES && d >= dist[n - 1] )
				continue;
			if ( n < VK_CUBEMAP_BLEND_PROBES )
				n++;
			for ( k = n - 1; k > 1 && dist[k - 1] > d; k-- ) {
				dist[k] = dist[k - 1];
				cube->blendProbes[k] = cube->blendProbes[k - 1];
			}
			dist[k] = d;
			cube->blendProbes[k] = j;
		}

		cube->numBlendProbes = n;
		cube->blendDescriptor = VK_NULL_HANDLE;

		R_FitCubemapBox( worldData, cube );
		ri.Printf( PRINT_DEVELOPER, "cubemap %i '%s': box %.0f %.0f %.0f .. %.0f %.0f %.0f\n", i, cube->name,
			cube->boxMins[0], cube->boxMins[1], cube->boxMins[2], cube->boxMaxs[0], cube->boxMaxs[1], cube->boxMaxs[2] );
	}
}
