/*
===========================================================================
Sun shadow cascades (port of rend2's sun shadow maps; docs: xenjk docs/renderer-vulkan-sun-port.md).

A 4-layer D16 depth array, one depth-only render pass and a framebuffer per layer, an array view with a
comparison sampler for the lit shaders (descriptor set VK_DESC_SUN_SHADOW). Layers 0..2 are the cascades, layer 3
the character shadow atlas (tr_charshadow.cpp R_SetupCharShadows: one tile per caster, 4 x 2 tiles of a quarter layer).
The front end sets up three orthographic cascade views (tr_main.cpp R_SetupSunShadowCascades); the backend draws
them at the start of the frame, before the main render pass (tr_backend.cpp RB_RenderSunShadowCascades). The image
exists whenever the renderer runs (r_shadowMapSize layers with depth clamp, else 1x1) so the descriptor is always
valid; it is cleared to the far plane at creation, which reads as fully lit.
===========================================================================
*/

#include "tr_local.h"

#define SUN_SHADOW_FORMAT VK_FORMAT_D16_UNORM

// load: a variant that keeps the layer's depth (the far cascade's static casters copied back into it)
static VkRenderPass vk_create_sun_shadow_render_pass( qboolean load )
{
	VkRenderPass pass;
	VkAttachmentDescription attachment;
	VkAttachmentReference depth_ref;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo desc;

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.format = SUN_SHADOW_FORMAT;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = load ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	depth_ref.attachment = 0;
	depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;
	subpass.pDepthStencilAttachment = &depth_ref;

	// earlier reads of this layer (last frame's lit pass) before the depth writes
	Com_Memset( deps, 0, sizeof( deps ) );
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	// the depth writes before the lit pass samples them
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	desc.attachmentCount = 1;
	desc.pAttachments = &attachment;
	desc.subpassCount = 1;
	desc.pSubpasses = &subpass;
	desc.dependencyCount = 2;
	desc.pDependencies = deps;

	VK_CHECK( qvkCreateRenderPass( vk.device, &desc, NULL, &pass ) );
	vk_gputime_register_pass( pass, "sun shadow" );
	VK_SET_OBJECT_NAME( pass, load ? "render pass - sun shadow (load)" : "render pass - sun shadow", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );
	return pass;
}

static void vk_create_sun_shadow_view( VkImageView *view, VkImageViewType type, uint32_t layer, uint32_t count )
{
	VkImageViewCreateInfo desc;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	desc.image = vk.sunShadow.image;
	desc.viewType = type;
	desc.format = SUN_SHADOW_FORMAT;
	desc.components.r = desc.components.g = desc.components.b = desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	desc.subresourceRange.baseMipLevel = 0;
	desc.subresourceRange.levelCount = 1;
	desc.subresourceRange.baseArrayLayer = layer;
	desc.subresourceRange.layerCount = count;
	VK_CHECK( qvkCreateImageView( vk.device, &desc, NULL, view ) );
}

/*
r_charSelfShadowHairMap (test, latched): the first self-shadow tile's hair (tr_backend.cpp RB_RenderSunShadowCascades)
drawn once more with its tile's view into a colour image at half the tile's size. Each strand fragment in front of the
opaque body (the tile's depth, from which that tile then leaves hair out) adds its optical depth -ln(1 - alpha) to r
and the depth moments to g, b (world units about the tile's centre); a keeps the nearest strand (MIN). The alpha is the
stage's own, filtered at the map's footprint, so the strands' coverage averages into a continuous transmittance as in
a converged path trace, where hashing it into the depth tile left a sparse pattern.
*/
static void vk_create_hair_map( void )
{
	VkImageCreateInfo image_desc;
	VkMemoryRequirements reqs;
	VkMemoryAllocateInfo alloc;
	VkImageViewCreateInfo view;
	VkAttachmentDescription attachment;
	VkAttachmentReference color_ref;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo rp;
	VkFramebufferCreateInfo fb;
	VkSamplerCreateInfo sampler;
	VkFormatProperties props;

	if ( !r_charSelfShadowHairMap->integer || !vk.sunShadow.depthClamp || vk.sunShadow.size < 1024 )
		return;

	// 32-bit floats keep the moments' difference (variance) exact enough; 16-bit floats if the device cannot blend them
	vk.hairMap.size = vk.sunShadow.size / 4;
	vk.hairMap.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, vk.hairMap.format, &props );
	if ( ( props.optimalTilingFeatures & ( VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) )
		!= ( VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ) )
		vk.hairMap.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	if ( r_charHairMap16->integer )	// the fallback forced, for a test on a device that blends RGBA32F
		vk.hairMap.format = VK_FORMAT_R16G16B16A16_SFLOAT;

	Com_Memset( &image_desc, 0, sizeof( image_desc ) );
	image_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_desc.imageType = VK_IMAGE_TYPE_2D;
	image_desc.format = vk.hairMap.format;
	image_desc.extent.width = vk.hairMap.size;
	image_desc.extent.height = vk.hairMap.size;
	image_desc.extent.depth = 1;
	image_desc.mipLevels = 1;
	image_desc.arrayLayers = 1;
	image_desc.samples = VK_SAMPLE_COUNT_1_BIT;
	image_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &image_desc, NULL, &vk.hairMap.image ) );

	qvkGetImageMemoryRequirements( vk.device, vk.hairMap.image, &reqs );
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = reqs.size;
	alloc.memoryTypeIndex = vk_find_memory_type( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	VK_ALLOCATE_MEMORY_CHECK( vk.device, &alloc, &vk.hairMap.memory, "character hair map" );
	VK_CHECK( qvkBindImageMemory( vk.device, vk.hairMap.image, vk.hairMap.memory, 0 ) );
	VK_SET_OBJECT_NAME( vk.hairMap.image, "character hair map", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );

	Com_Memset( &view, 0, sizeof( view ) );
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = vk.hairMap.image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = vk.hairMap.format;
	view.components.r = view.components.g = view.components.b = view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &view, NULL, &vk.hairMap.view ) );

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.format = vk.hairMap.format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	color_ref.attachment = 0;
	color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;
	// last frame's lit pass read it before this frame's strands are added; they are written before this frame's reads
	Com_Memset( deps, 0, sizeof( deps ) );
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	Com_Memset( &rp, 0, sizeof( rp ) );
	rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp.attachmentCount = 1;
	rp.pAttachments = &attachment;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;
	rp.dependencyCount = 2;
	rp.pDependencies = deps;
	VK_CHECK( qvkCreateRenderPass( vk.device, &rp, NULL, &vk.hairMap.render_pass ) );
	vk_gputime_register_pass( vk.hairMap.render_pass, "char hair map" );
	VK_SET_OBJECT_NAME( vk.hairMap.render_pass, "render pass - character hair map", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	Com_Memset( &fb, 0, sizeof( fb ) );
	fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb.renderPass = vk.hairMap.render_pass;
	fb.attachmentCount = 1;
	fb.pAttachments = &vk.hairMap.view;
	fb.width = fb.height = vk.hairMap.size;
	fb.layers = 1;
	VK_CHECK( qvkCreateFramebuffer( vk.device, &fb, NULL, &vk.hairMap.framebuffer ) );

	Com_Memset( &sampler, 0, sizeof( sampler ) );
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.maxLod = 0.0f;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &vk.hairMap.sampler ) );

	// cleared (no hair) and shader-readable before the first frame
	{
		VkCommandBuffer cb = vk_begin_command_buffer();
		VkClearValue clear;
		VkRenderPassBeginInfo begin;

		clear.color.float32[0] = clear.color.float32[1] = clear.color.float32[2] = 0.0f;
		clear.color.float32[3] = 1e4f;
		Com_Memset( &begin, 0, sizeof( begin ) );
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = vk.hairMap.render_pass;
		begin.framebuffer = vk.hairMap.framebuffer;
		begin.renderArea.extent.width = begin.renderArea.extent.height = vk.hairMap.size;
		begin.clearValueCount = 1;
		begin.pClearValues = &clear;
		qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );
		qvkCmdEndRenderPass( cb );
		vk_end_command_buffer( cb, __func__ );
	}

	ri.Printf( PRINT_ALL, "character hair map: %i x %i, %s\n", vk.hairMap.size, vk.hairMap.size,
		vk.hairMap.format == VK_FORMAT_R32G32B32A32_SFLOAT ? "RGBA32F" : "RGBA16F" );
}

static void vk_destroy_hair_map( void )
{
	vk.hairMap.descriptor = VK_NULL_HANDLE;	// the pool owns it
	if ( vk.hairMap.sampler )
		qvkDestroySampler( vk.device, vk.hairMap.sampler, NULL );
	if ( vk.hairMap.framebuffer )
		qvkDestroyFramebuffer( vk.device, vk.hairMap.framebuffer, NULL );
	if ( vk.hairMap.render_pass ) {
		vk_gputime_unregister_pass( vk.hairMap.render_pass );
		qvkDestroyRenderPass( vk.device, vk.hairMap.render_pass, NULL );
	}
	if ( vk.hairMap.view )
		qvkDestroyImageView( vk.device, vk.hairMap.view, NULL );
	if ( vk.hairMap.image )
		qvkDestroyImage( vk.device, vk.hairMap.image, NULL );
	if ( vk.hairMap.memory )
		VK_FREE_MEMORY( vk.device, vk.hairMap.memory );
	Com_Memset( &vk.hairMap, 0, sizeof( vk.hairMap ) );
}

void vk_begin_hair_map_render_pass( void )
{
	VkRenderPassBeginInfo begin;
	VkClearValue clear;

	vk.renderPassIndex = RENDER_PASS_CHAR_HAIRMAP;
	vk.renderWidth = vk.renderHeight = vk.hairMap.size;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	clear.color.float32[0] = clear.color.float32[1] = clear.color.float32[2] = 0.0f;
	clear.color.float32[3] = 1e4f;	// nearest strand: MIN-blended
	Com_Memset( &begin, 0, sizeof( begin ) );
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = vk.hairMap.render_pass;
	begin.framebuffer = vk.hairMap.framebuffer;
	begin.renderArea.extent.width = begin.renderArea.extent.height = vk.hairMap.size;
	begin.clearValueCount = 1;
	begin.pClearValues = &clear;

	vk_gputime_pass_begin( vk.hairMap.render_pass );
	qvkCmdBeginRenderPass( vk.cmd->command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE );
}

// (re)allocate the cascades' descriptor set: the descriptor pool is reset when the renderer releases a level's
// resources, so this runs again with the attachment descriptors (vk_shade_geometry.cpp)
void vk_alloc_sun_shadow_descriptor( void )
{
	VkDescriptorSetAllocateInfo set_alloc;
	VkDescriptorImageInfo image_info;
	VkWriteDescriptorSet write;

	if ( vk.hairMap.image ) {
		Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
		set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		set_alloc.descriptorPool = vk.descriptor_pool;
		set_alloc.descriptorSetCount = 1;
		set_alloc.pSetLayouts = &vk.set_layout_sampler;
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.hairMap.descriptor ) );
		image_info.sampler = vk.hairMap.sampler;
		image_info.imageView = vk.hairMap.view;
		image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		Com_Memset( &write, 0, sizeof( write ) );
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = vk.hairMap.descriptor;
		write.dstBinding = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &image_info;
		qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
	}

	if ( !vk.sunShadow.image )
		return;

	Com_Memset( &set_alloc, 0, sizeof( set_alloc ) );
	set_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc.descriptorPool = vk.descriptor_pool;
	set_alloc.descriptorSetCount = 1;
	set_alloc.pSetLayouts = &vk.set_layout_sampler;
	VK_CHECK( qvkAllocateDescriptorSets( vk.device, &set_alloc, &vk.sunShadow.descriptor ) );

	image_info.sampler = vk.sunShadow.sampler;
	image_info.imageView = vk.sunShadow.array_view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	Com_Memset( &write, 0, sizeof( write ) );
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk.sunShadow.descriptor;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image_info;
	qvkUpdateDescriptorSets( vk.device, 1, &write, 0, NULL );
}

void vk_create_sun_shadow( void )
{
	VkImageCreateInfo image_desc;
	VkMemoryRequirements reqs;
	VkMemoryAllocateInfo alloc;
	VkFramebufferCreateInfo fb;
	VkSamplerCreateInfo sampler;
	uint32_t i;

	// depth clamp is required, no sun shadows without it
	const qboolean enabled = ( r_sunShadows->integer && r_sunlightMode->integer && vk.sunShadow.depthClamp ) ? qtrue : qfalse;
	// character shadows (r_charShadows, not latched) need the full size whenever depth clamp exists
	vk.sunShadow.size = vk.sunShadow.depthClamp ? r_shadowMapSize->integer : 1;

	Com_Memset( &image_desc, 0, sizeof( image_desc ) );
	image_desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_desc.imageType = VK_IMAGE_TYPE_2D;
	image_desc.format = SUN_SHADOW_FORMAT;
	image_desc.extent.width = vk.sunShadow.size;
	image_desc.extent.height = vk.sunShadow.size;
	image_desc.extent.depth = 1;
	image_desc.mipLevels = 1;
	image_desc.arrayLayers = 4;
	image_desc.samples = VK_SAMPLE_COUNT_1_BIT;
	image_desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		| VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	image_desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &image_desc, NULL, &vk.sunShadow.image ) );

	qvkGetImageMemoryRequirements( vk.device, vk.sunShadow.image, &reqs );
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = reqs.size;
	alloc.memoryTypeIndex = vk_find_memory_type( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	VK_ALLOCATE_MEMORY_CHECK( vk.device, &alloc, &vk.sunShadow.memory, "sun shadow cascades" );
	VK_CHECK( qvkBindImageMemory( vk.device, vk.sunShadow.image, vk.sunShadow.memory, 0 ) );
	VK_SET_OBJECT_NAME( vk.sunShadow.image, "sun shadow cascades", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );

	vk_create_sun_shadow_view( &vk.sunShadow.array_view, VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 4 );
	for ( i = 0; i < 4; i++ )
		vk_create_sun_shadow_view( &vk.sunShadow.layer_view[i], VK_IMAGE_VIEW_TYPE_2D, i, 1 );

	vk.sunShadow.render_pass = vk_create_sun_shadow_render_pass( qfalse );
	vk.sunShadow.load_pass = vk_create_sun_shadow_render_pass( qtrue );

	// the far cascade's static casters (r_sunShadowFarCache): one more layer's worth, a transfer image; a second layer
	// for the middle cascade's only when r_sunShadowMidCache is set now (its layer costs nothing while it is off)
	vk.sunShadow.cacheValid[0] = vk.sunShadow.cacheValid[1] = qfalse;
	vk.sunShadow.cacheLayers = 0;
	if ( enabled ) {
		vk.sunShadow.cacheLayers = r_sunShadowMidCache->integer ? 2 : 1;
		image_desc.arrayLayers = vk.sunShadow.cacheLayers;
		image_desc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		VK_CHECK( qvkCreateImage( vk.device, &image_desc, NULL, &vk.sunShadow.cache ) );
		qvkGetImageMemoryRequirements( vk.device, vk.sunShadow.cache, &reqs );
		alloc.allocationSize = reqs.size;
		alloc.memoryTypeIndex = vk_find_memory_type( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		VK_ALLOCATE_MEMORY_CHECK( vk.device, &alloc, &vk.sunShadow.cacheMemory, "sun shadow far cascade cache" );
		VK_CHECK( qvkBindImageMemory( vk.device, vk.sunShadow.cache, vk.sunShadow.cacheMemory, 0 ) );
		VK_SET_OBJECT_NAME( vk.sunShadow.cache, "sun shadow far cascade cache", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	}

	Com_Memset( &fb, 0, sizeof( fb ) );
	fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb.renderPass = vk.sunShadow.render_pass;
	fb.attachmentCount = 1;
	fb.width = vk.sunShadow.size;
	fb.height = vk.sunShadow.size;
	fb.layers = 1;
	for ( i = 0; i < 4; i++ ) {
		fb.pAttachments = &vk.sunShadow.layer_view[i];
		VK_CHECK( qvkCreateFramebuffer( vk.device, &fb, NULL, &vk.sunShadow.framebuffer[i] ) );
	}

	// hardware comparison with linear filtering (rend2: GL_COMPARE_REF_TO_TEXTURE, GL_LEQUAL, GL_LINEAR)
	Com_Memset( &sampler, 0, sizeof( sampler ) );
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.compareEnable = VK_TRUE;
	sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	sampler.minLod = 0.0f;
	sampler.maxLod = 0.0f;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &vk.sunShadow.sampler ) );
	// the descriptor set comes from vk_init_descriptors (after this, and again after every pool reset)

	// clear every layer to the far plane and leave it shader-readable
	{
		VkCommandBuffer cb = vk_begin_command_buffer();
		VkClearValue clear;
		VkRenderPassBeginInfo begin;

		clear.depthStencil.depth = 1.0f;
		clear.depthStencil.stencil = 0;
		Com_Memset( &begin, 0, sizeof( begin ) );
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = vk.sunShadow.render_pass;
		begin.renderArea.extent.width = vk.sunShadow.size;
		begin.renderArea.extent.height = vk.sunShadow.size;
		begin.clearValueCount = 1;
		begin.pClearValues = &clear;
		for ( i = 0; i < 4; i++ ) {
			begin.framebuffer = vk.sunShadow.framebuffer[i];
			qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );
			qvkCmdEndRenderPass( cb );
		}
		vk_end_command_buffer( cb, __func__ );
	}

	ri.Printf( PRINT_ALL, "sun shadow cascades: %s (3 x %i)%s\n", enabled ? "on" : ( vk.sunShadow.depthClamp ? "off" : "off, no depth clamp" ), vk.sunShadow.size,
		vk.sunShadow.depthClamp ? ", character shadow atlas layer" : "" );

	vk_create_hair_map();
}

void vk_destroy_sun_shadow( void )
{
	uint32_t i;

	vk_destroy_hair_map();
	vk.sunShadow.descriptor = VK_NULL_HANDLE;	// the pool owns it (reset or destroyed with the pool)
	if ( vk.sunShadow.sampler ) {
		qvkDestroySampler( vk.device, vk.sunShadow.sampler, NULL );
		vk.sunShadow.sampler = VK_NULL_HANDLE;
	}
	for ( i = 0; i < 4; i++ ) {
		if ( vk.sunShadow.framebuffer[i] ) {
			qvkDestroyFramebuffer( vk.device, vk.sunShadow.framebuffer[i], NULL );
			vk.sunShadow.framebuffer[i] = VK_NULL_HANDLE;
		}
		if ( vk.sunShadow.layer_view[i] ) {
			qvkDestroyImageView( vk.device, vk.sunShadow.layer_view[i], NULL );
			vk.sunShadow.layer_view[i] = VK_NULL_HANDLE;
		}
	}
	if ( vk.sunShadow.render_pass ) {
		vk_gputime_unregister_pass( vk.sunShadow.render_pass );
		qvkDestroyRenderPass( vk.device, vk.sunShadow.render_pass, NULL );
		vk.sunShadow.render_pass = VK_NULL_HANDLE;
	}
	if ( vk.sunShadow.load_pass ) {
		vk_gputime_unregister_pass( vk.sunShadow.load_pass );
		qvkDestroyRenderPass( vk.device, vk.sunShadow.load_pass, NULL );
		vk.sunShadow.load_pass = VK_NULL_HANDLE;
	}
	if ( vk.sunShadow.cache ) {
		qvkDestroyImage( vk.device, vk.sunShadow.cache, NULL );
		vk.sunShadow.cache = VK_NULL_HANDLE;
	}
	if ( vk.sunShadow.cacheMemory ) {
		VK_FREE_MEMORY( vk.device, vk.sunShadow.cacheMemory );
		vk.sunShadow.cacheMemory = VK_NULL_HANDLE;
	}
	vk.sunShadow.cacheValid[0] = vk.sunShadow.cacheValid[1] = qfalse;
	if ( vk.sunShadow.array_view ) {
		qvkDestroyImageView( vk.device, vk.sunShadow.array_view, NULL );
		vk.sunShadow.array_view = VK_NULL_HANDLE;
	}
	if ( vk.sunShadow.image ) {
		qvkDestroyImage( vk.device, vk.sunShadow.image, NULL );
		vk.sunShadow.image = VK_NULL_HANDLE;
	}
	if ( vk.sunShadow.memory ) {
		VK_FREE_MEMORY( vk.device, vk.sunShadow.memory );
		vk.sunShadow.memory = VK_NULL_HANDLE;
	}
}

static void vk_begin_sun_shadow_pass( int layer, qboolean load )
{
	VkRenderPassBeginInfo begin;
	VkClearValue clear;

	vk.renderPassIndex = RENDER_PASS_SUN_SHADOW;
	vk.renderWidth = vk.sunShadow.size;
	vk.renderHeight = vk.sunShadow.size;
	vk.renderScaleX = vk.renderScaleY = 1.0f;

	clear.depthStencil.depth = 1.0f;
	clear.depthStencil.stencil = 0;
	Com_Memset( &begin, 0, sizeof( begin ) );
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = load ? vk.sunShadow.load_pass : vk.sunShadow.render_pass;
	begin.framebuffer = vk.sunShadow.framebuffer[layer];
	begin.renderArea.extent.width = vk.sunShadow.size;
	begin.renderArea.extent.height = vk.sunShadow.size;
	begin.clearValueCount = load ? 0 : 1;
	begin.pClearValues = &clear;

	vk_gputime_pass_begin( begin.renderPass );
	qvkCmdBeginRenderPass( vk.cmd->command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE );
}

void vk_begin_sun_shadow_render_pass( int layer )
{
	vk_begin_sun_shadow_pass( layer, qfalse );
}

// the layer as its static casters left it (vk_sun_shadow_cache_copy), the moving ones drawn over them
void vk_begin_sun_shadow_render_pass_load( int layer )
{
	vk_begin_sun_shadow_pass( layer, qtrue );
}

static void vk_sun_shadow_barrier( VkImage image, uint32_t layer, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
	VkAccessFlags dst, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage )
{
	VkImageMemoryBarrier b;
	Com_Memset( &b, 0, sizeof( b ) );
	b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b.srcAccessMask = src;
	b.dstAccessMask = dst;
	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	b.subresourceRange.baseArrayLayer = layer;
	b.subresourceRange.levelCount = 1;
	b.subresourceRange.layerCount = 1;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b );
}

/*
store: the layer (its static casters just drawn, shader-readable) into the cache, then the layer ready for the
load pass. Else: the cache back into the layer (its last reads, the lit pass and the haze, done), ready for the load
pass. The cache stays in TRANSFER_SRC between uses.
*/
// a cascade layer whose static casters are kept across frames: the far one (r_sunShadowFarCache), the middle one too
// with r_sunShadowMidCache; each centred on the eye's grid cell (R_SetupSunShadowCascades)
qboolean vk_sun_shadow_kept( int layer )
{
	if ( !vk.sunShadow.cache || !r_sunShadowFarCache->integer )
		return qfalse;
	return ( layer == 2 || ( layer == 1 && r_sunShadowMidCache->integer && vk.sunShadow.cacheLayers >= 2 ) ) ? qtrue : qfalse;
}

// the kept cascades' static casters belong to the loaded world: forgotten when the renderer shuts down (map change,
// vid_restart), as the cache key cannot tell one world from the next
void vk_sun_shadow_cache_reset( void )
{
	vk.sunShadow.cacheValid[0] = vk.sunShadow.cacheValid[1] = qfalse;
}

void vk_sun_shadow_cache_copy( int layer, qboolean store )
{
	const uint32_t slot = layer == 2 ? 0u : 1u;	// the cache image's layer
	VkImageCopy region;
	const VkPipelineStageFlags readers = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

	Com_Memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
	region.extent.width = region.extent.height = vk.sunShadow.size;
	region.extent.depth = 1;
	const qboolean timed = vk_gputime_section_begin( "far cache copy" );
	if ( store ) {
		region.srcSubresource.baseArrayLayer = layer;
		vk_sun_shadow_barrier( vk.sunShadow.image, layer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
		region.dstSubresource.baseArrayLayer = slot;
		vk_sun_shadow_barrier( vk.sunShadow.cache, slot, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
		qvkCmdCopyImage( vk.cmd->command_buffer, vk.sunShadow.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.sunShadow.cache,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
		vk_sun_shadow_barrier( vk.sunShadow.cache, slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
		vk_sun_shadow_barrier( vk.sunShadow.image, layer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT );
	} else {
		region.srcSubresource.baseArrayLayer = slot;
		region.dstSubresource.baseArrayLayer = layer;
		vk_sun_shadow_barrier( vk.sunShadow.image, layer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, readers, VK_PIPELINE_STAGE_TRANSFER_BIT );
		qvkCmdCopyImage( vk.cmd->command_buffer, vk.sunShadow.cache, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.sunShadow.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
		vk_sun_shadow_barrier( vk.sunShadow.image, layer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT );
	}
	vk_gputime_section_end( timed );
}

// a kept layer that already holds its cache's contents (r_sunShadowCacheSkip), ready for the load pass: its last reads
// (the lit pass and the haze) done, its contents kept
void vk_sun_shadow_attach( int layer )
{
	vk_sun_shadow_barrier( vk.sunShadow.image, layer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT );
}

/*
=============
RB_RenderFarCascade

r_sunShadowFarCache: the far cascade's static casters (the world's surfaces and the render scene) are drawn only when
its view changes (the eye's grid cell, R_SetupSunShadowCascades; the sun; the map; the scene's shadow settings) and
kept in vk.sunShadow.cache. Every frame the layer is restored from it and the moving models (entities: characters,
movers) are drawn over it. About a millisecond of the far cascade's fill in a dense scene, for a 4096^2 D16 copy.
=============
*/
/*
RB_FarCascadeReuse: whether this frame's kept cascade (vk_sun_shadow_kept) restores its static casters from the cache
instead of drawing them: its view, the scene's shadow settings and the map as the cache was drawn. The render scene's
cull asks first (R_SceneVkBeginFrame, the same frame, before the cascades are drawn) and leaves out the views it will
not draw (r_sceneKeptCull)
*/
qboolean RB_FarCascadeReuse( const viewParms_t *vp, int layer )
{
	float mvp[16];

	if ( !vk_sun_shadow_kept( layer ) || r_sunShadowDebug->integer == 4 )
		return qfalse;
	myGlMultMatrix( vp->world.modelViewMatrix, vp->projectionMatrix, mvp );
	const uint32_t key = vk_scene_shadow_key() ^ ( (uint32_t)(uintptr_t)tr.world * 2654435761u );
	const int slot = layer == 2 ? 0 : 1;
	return ( vk.sunShadow.cacheValid[slot] && vk.sunShadow.cacheKey[slot] == key
		&& !memcmp( mvp, vk.sunShadow.cacheMvp[slot], sizeof( mvp ) ) ) ? qtrue : qfalse;
}

void RB_RenderFarCascade( const drawSurfsCommand_t *cmd, int layer )
{
	static drawSurf_t	split[MAX_DRAWSURFS];
	// the layer holds only its static casters, as the cache does: stored and no moving model drawn over it since
	// (r_sunShadowCacheSkip: then neither the restore's copy nor, with no moving model this frame, the load pass)
	static qboolean		clean[2];
	float				mvp[16];
	int					i, numStatic = 0, numMoving = 0;

	// the world's surfaces first, the entities' after them (each keeping its sorted order)
	for ( i = 0; i < cmd->numDrawSurfs; i++ ) {
		int entityNum, cubemap;
		shader_t *shader;
		R_DecomposeSort( cmd->drawSurfs[i].sort, &entityNum, &shader, &cubemap );
		if ( entityNum == REFENTITYNUM_WORLD )
			split[numStatic++] = cmd->drawSurfs[i];
	}
	for ( i = 0; i < cmd->numDrawSurfs; i++ ) {
		int entityNum, cubemap;
		shader_t *shader;
		R_DecomposeSort( cmd->drawSurfs[i].sort, &entityNum, &shader, &cubemap );
		if ( entityNum != REFENTITYNUM_WORLD )
			split[numStatic + numMoving++] = cmd->drawSurfs[i];
	}

	myGlMultMatrix( backEnd.viewParms.world.modelViewMatrix, backEnd.viewParms.projectionMatrix, mvp );
	const uint32_t key = vk_scene_shadow_key() ^ ( (uint32_t)(uintptr_t)tr.world * 2654435761u );
	const int slot = layer == 2 ? 0 : 1;	// the far cascade's cache, or the middle one's (r_sunShadowMidCache)
	const qboolean reuse = RB_FarCascadeReuse( &backEnd.viewParms, layer );
	const qboolean skip = r_sunShadowCacheSkip->integer ? qtrue : qfalse;

	if ( !reuse ) {
		vk_begin_sun_shadow_render_pass( layer );
		vk.cmd->depth_range = DEPTH_RANGE_COUNT;	// force viewport/scissor for the cascade size
		vk.cmd->last_pipeline = VK_NULL_HANDLE;
		Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );
		RB_RenderDrawSurfList( split, numStatic );
		vk_scene_draw( SCENE_PASS_SHADOW );
		vk_end_render_pass();
		vk_sun_shadow_cache_copy( layer, qtrue );
		Com_Memcpy( vk.sunShadow.cacheMvp[slot], mvp, sizeof( mvp ) );
		vk.sunShadow.cacheKey[slot] = key;
		// (the scene's cull left this view out expecting the cache (r_sceneKeptCull), so its casters are missing:
		// drawn again in full next frame; not expected, as both ask RB_FarCascadeReuse in the same frame)
		vk.sunShadow.cacheValid[slot] = vk_scene_view_skipped( backEnd.viewParms.sceneView ) ? qfalse : qtrue;
		clean[slot] = qtrue;
	} else if ( skip && clean[slot] ) {
		// the layer already holds what the copy would write
		if ( !numMoving )
			return;	// and nothing drawn over it: left as the last frame's readers had it (SHADER_READ_ONLY)
		vk_sun_shadow_attach( layer );
	} else {
		vk_sun_shadow_cache_copy( layer, qfalse );
		clean[slot] = qtrue;
	}
	if ( numMoving )
		clean[slot] = qfalse;

	vk_begin_sun_shadow_render_pass_load( layer );
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );
	RB_RenderDrawSurfList( split + numStatic, numMoving );
	vk_end_render_pass();
}
