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
=============================================================================

CHARACTERS MIRRORED IN THE WATER (r_sceneWaterChars)

The render scene's water reflects what the screen holds (scene_ssr.comp), and characters are left out of that on
purpose (scene_capture.comp: their last-frame silhouette trailed behind a turning view). This draws them as a real
mirror instead: after the eye view, the Ghoul2 models near the water (the nearest few, standing within reach of its
plane) are drawn from the eye mirrored in the plane, PV_MIRROR (culling flipped, first-person models out, the player's
own body in, the near plane cut along the water so a wading body's legs stay under it), into a half-size colour and
depth target cleared to alpha 0. vk_begin_frame draws it before any pass of the frame, after the shadow tiles; the
same frame's water (scene.frag charMirrorAt, draw set binding 35) reads it at its own pixel, nudged by a quarter of
the ripples' shift of the screen's reflection (the full shift tears a figure to shreds). The target is premultiplied,
alpha the coverage: an additive surface (a saber's blades, mirrored with the caster holding them) adds light and covers
nothing (vk_pipelines.cpp keeps its alpha). r_sceneWaterChars 2 shows the target alone on the water, 3 its alpha.

The plane is one per frame: the largest flat water below the eye (R_SceneWaterPlane); water at other heights shows
none. Nothing of the world is in the mirror view, so a character behind a wall still mirrors past it: the casters are
bounded instead (WATER_MIRROR_*). The target is sized when the device is made: a resized window turns this off until
vid_restart.

=============================================================================
*/

#include "tr_local.h"

#define WATER_MIRROR_MAX		8		// the nearest casters
#define WATER_MIRROR_REACH		4096.0f	// from the eye, across the water
#define WATER_MIRROR_ABOVE		640.0f	// a caster's origin over the plane at most (a jump off the bank still mirrors)
#define WATER_MIRROR_BELOW		64.0f	// and under it (wading, swimming)
#define WATER_MIRROR_BLADE		160.0f	// a saber blade's base from its wielder's origin at most

/*
================
R_RenderWaterMirror

After the eye view (RE_RenderScene): the mirrored view and its draw surfaces, routed to backEndData->waterMirrorCmd
================
*/
void R_RenderWaterMirror( const viewParms_t *eye )
{
	int		ents[WATER_MIRROR_MAX];
	float	dist[WATER_MIRROR_MAX];
	int		n = 0, i, k;
	float	h;

	if ( !r_sceneWaterChars || r_sceneWaterChars->value <= 0.0f || !vk.waterMirror.framebuffer || !r_drawentities->integer )
		return;
	if ( !tr.world || ( tr.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) || eye->portalView != PV_NONE || !eye->sceneView
		|| eye->targetCube != nullptr || backEndData->waterMirrorCmd )	// (one a frame: the first eye view's)
		return;
	if ( !R_SceneWaterPlane( eye->ori.origin, &h ) || eye->ori.origin[2] <= h + 1.0f )
		return;

	for ( i = 0; i < tr.refdef.num_entities; i++ ) {
		const trRefEntity_t *ent = &tr.refdef.entities[i];
		float d;

		if ( ent->e.reType != RT_MODEL || !ent->e.ghoul2 || ( ent->e.renderfx & RF_FIRST_PERSON ) )
			continue;
		if ( ent->e.origin[2] - h > WATER_MIRROR_ABOVE || ent->e.origin[2] - h < -WATER_MIRROR_BELOW )
			continue;
		d = Distance( ent->e.origin, eye->ori.origin );
		if ( d > WATER_MIRROR_REACH )
			continue;
		for ( k = n; k > 0 && dist[k - 1] > d; k-- ) {
			if ( k < WATER_MIRROR_MAX ) {
				ents[k] = ents[k - 1];
				dist[k] = dist[k - 1];
			}
		}
		if ( k < WATER_MIRROR_MAX ) {
			ents[k] = i;
			dist[k] = d;
			if ( n < WATER_MIRROR_MAX )
				n++;
		}
	}
	if ( !n )
		return;

	// the eye mirrored in the plane z = h: every axis's z negated (a left-handed view: PV_MIRROR flips the culling),
	// the near plane cut along the water (R_SetupProjectionZ's oblique clip: what lies under it is not drawn)
	viewParms_t parms = *eye;
	parms.ori.origin[2] = 2.0f * h - eye->ori.origin[2];
	for ( k = 0; k < 3; k++ )
		parms.ori.axis[k][2] = -eye->ori.axis[k][2];
	parms.portalView = PV_MIRROR;
	VectorSet( parms.portalPlane.normal, 0.0f, 0.0f, 1.0f );
	parms.portalPlane.dist = h;
	parms.portalPlane.type = PLANE_Z;
	SetPlaneSignbits( &parms.portalPlane );
	parms.waterMirror = 1;
	parms.sceneView = 0;
	parms.sunCascade = 0;
	parms.charShadow = 0;
	parms.selfShadowSlot = 0;
#ifdef USE_PMLIGHT
	parms.num_dlights = 0;	// (the lit surfaces' pass is the main view's)
#endif

	tr.viewCount++;
	tr.viewParms = parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;
	R_RotateForViewer( &tr.ori, &tr.viewParms );
	R_SetupProjection( &tr.viewParms, r_zproj->value, qtrue );
	R_SetupProjectionZ( &tr.viewParms );

	const int firstDrawSurf = tr.refdef.numDrawSurfs;
	for ( i = 0; i < n; i++ )
		R_AddEntitySurface( &tr.refdef, &tr.refdef.entities[ents[i]], ents[i] );
	// their saber blades (cgame's glow and core: entities of their own) within reach of one of them
	for ( i = 0; i < tr.refdef.num_entities; i++ ) {
		trRefEntity_t *ent = &tr.refdef.entities[i];

		if ( ent->e.reType != RT_SABER_GLOW && ent->e.reType != RT_LINE )
			continue;
		for ( k = 0; k < n; k++ ) {
			if ( DistanceSquared( ent->e.origin, tr.refdef.entities[ents[k]].e.origin ) < WATER_MIRROR_BLADE * WATER_MIRROR_BLADE ) {
				R_AddEntitySurface( &tr.refdef, ent, i );
				break;
			}
		}
	}
	int numDrawSurfs = tr.refdef.numDrawSurfs;
	if ( numDrawSurfs > MAX_DRAWSURFS )
		numDrawSurfs = MAX_DRAWSURFS;
	backEndData->waterMirrorZ = h;
	R_SortDrawSurfs( tr.refdef.drawSurfs + firstDrawSurf, numDrawSurfs - firstDrawSurf );
}

/*
=============================================================================

The target: half the screen, colour (the main view's format) and depth, one sample; lives with the device
(vk_create_sun_shadow's), sized at its creation (a later window size leaves it unused until vid_restart)

=============================================================================
*/

static void vk_water_mirror_image( VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage *image,
	VkDeviceMemory *memory, VkImageView *view, const char *name )
{
	VkImageCreateInfo desc;
	VkMemoryRequirements reqs;
	VkMemoryAllocateInfo alloc;
	VkImageViewCreateInfo vdesc;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.imageType = VK_IMAGE_TYPE_2D;
	desc.format = format;
	desc.extent.width = vk.waterMirror.width;
	desc.extent.height = vk.waterMirror.height;
	desc.extent.depth = 1;
	desc.mipLevels = 1;
	desc.arrayLayers = 1;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.tiling = VK_IMAGE_TILING_OPTIMAL;
	desc.usage = usage;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, image ) );

	qvkGetImageMemoryRequirements( vk.device, *image, &reqs );
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = reqs.size;
	alloc.memoryTypeIndex = vk_find_memory_type( reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	VK_ALLOCATE_MEMORY_CHECK( vk.device, &alloc, memory, name );
	VK_CHECK( qvkBindImageMemory( vk.device, *image, *memory, 0 ) );
	VK_SET_OBJECT_NAME( *image, name, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );

	Com_Memset( &vdesc, 0, sizeof( vdesc ) );
	vdesc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vdesc.image = *image;
	vdesc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vdesc.format = format;
	vdesc.components.r = vdesc.components.g = vdesc.components.b = vdesc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	vdesc.subresourceRange.aspectMask = aspect;
	vdesc.subresourceRange.levelCount = 1;
	vdesc.subresourceRange.layerCount = 1;
	VK_CHECK( qvkCreateImageView( vk.device, &vdesc, NULL, view ) );
}

void vk_create_water_mirror( void )
{
	VkAttachmentDescription att[2];
	VkAttachmentReference colorRef, depthRef;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo rp;
	VkFramebufferCreateInfo fb;
	VkSamplerCreateInfo sampler;
	VkImageView views[2];

	Com_Memset( &vk.waterMirror, 0, sizeof( vk.waterMirror ) );
	vk.waterMirror.width = MAX( glConfig.vidWidth / 2, 1 );
	vk.waterMirror.height = MAX( glConfig.vidHeight / 2, 1 );
	vk_water_mirror_image( vk.color_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
		&vk.waterMirror.image, &vk.waterMirror.memory, &vk.waterMirror.view, "water chars" );
	vk_water_mirror_image( vk.depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
		&vk.waterMirror.depthImage, &vk.waterMirror.depthMemory, &vk.waterMirror.depthView, "water chars depth" );

	Com_Memset( att, 0, sizeof( att ) );
	att[0].format = vk.color_format;
	att[0].samples = VK_SAMPLE_COUNT_1_BIT;
	att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	att[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	att[1] = att[0];
	att[1].format = vk.depth_format;
	att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	depthRef.attachment = 1;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;
	// last frame's water read it before this frame's characters are drawn; they are drawn before this frame's reads
	Com_Memset( deps, 0, sizeof( deps ) );
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	Com_Memset( &rp, 0, sizeof( rp ) );
	rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp.attachmentCount = 2;
	rp.pAttachments = att;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;
	rp.dependencyCount = 2;
	rp.pDependencies = deps;
	VK_CHECK( qvkCreateRenderPass( vk.device, &rp, NULL, &vk.waterMirror.render_pass ) );
	vk_gputime_register_pass( vk.waterMirror.render_pass, "water chars" );
	VK_SET_OBJECT_NAME( vk.waterMirror.render_pass, "render pass - water chars", VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT );

	views[0] = vk.waterMirror.view;
	views[1] = vk.waterMirror.depthView;
	Com_Memset( &fb, 0, sizeof( fb ) );
	fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb.renderPass = vk.waterMirror.render_pass;
	fb.attachmentCount = 2;
	fb.pAttachments = views;
	fb.width = vk.waterMirror.width;
	fb.height = vk.waterMirror.height;
	fb.layers = 1;
	VK_CHECK( qvkCreateFramebuffer( vk.device, &fb, NULL, &vk.waterMirror.framebuffer ) );

	Com_Memset( &sampler, 0, sizeof( sampler ) );
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.maxLod = 0.0f;
	VK_CHECK( qvkCreateSampler( vk.device, &sampler, NULL, &vk.waterMirror.sampler ) );

	// cleared and shader-readable before the first frame (the scene's sets bind it from their creation)
	{
		VkCommandBuffer cb = vk_begin_command_buffer();
		VkClearValue clear[2];
		VkRenderPassBeginInfo begin;

		Com_Memset( clear, 0, sizeof( clear ) );
		clear[1].depthStencil.depth = 1.0f;
		Com_Memset( &begin, 0, sizeof( begin ) );
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = vk.waterMirror.render_pass;
		begin.framebuffer = vk.waterMirror.framebuffer;
		begin.renderArea.extent.width = vk.waterMirror.width;
		begin.renderArea.extent.height = vk.waterMirror.height;
		begin.clearValueCount = 2;
		begin.pClearValues = clear;
		qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );
		qvkCmdEndRenderPass( cb );
		vk_end_command_buffer( cb, __func__ );
	}
	ri.Printf( PRINT_ALL, "water chars: %i x %i\n", vk.waterMirror.width, vk.waterMirror.height );
}

void vk_destroy_water_mirror( void )
{
	if ( vk.waterMirror.sampler )
		qvkDestroySampler( vk.device, vk.waterMirror.sampler, NULL );
	if ( vk.waterMirror.framebuffer )
		qvkDestroyFramebuffer( vk.device, vk.waterMirror.framebuffer, NULL );
	if ( vk.waterMirror.render_pass ) {
		vk_gputime_unregister_pass( vk.waterMirror.render_pass );
		qvkDestroyRenderPass( vk.device, vk.waterMirror.render_pass, NULL );
	}
	if ( vk.waterMirror.view )
		qvkDestroyImageView( vk.device, vk.waterMirror.view, NULL );
	if ( vk.waterMirror.image )
		qvkDestroyImage( vk.device, vk.waterMirror.image, NULL );
	if ( vk.waterMirror.memory )
		VK_FREE_MEMORY( vk.device, vk.waterMirror.memory );
	if ( vk.waterMirror.depthView )
		qvkDestroyImageView( vk.device, vk.waterMirror.depthView, NULL );
	if ( vk.waterMirror.depthImage )
		qvkDestroyImage( vk.device, vk.waterMirror.depthImage, NULL );
	if ( vk.waterMirror.depthMemory )
		VK_FREE_MEMORY( vk.device, vk.waterMirror.depthMemory );
	Com_Memset( &vk.waterMirror, 0, sizeof( vk.waterMirror ) );
}

// RENDER_PASS_WATER_MIRROR pipelines (1 sample, this render pass); the eye view's viewport scaled to the half size
void vk_begin_water_mirror_render_pass( void )
{
	vk.renderPassIndex = RENDER_PASS_WATER_MIRROR;
	vk.renderWidth = vk.waterMirror.width;
	vk.renderHeight = vk.waterMirror.height;
	vk.renderScaleX = (float)vk.renderWidth / (float)glConfig.vidWidth;
	vk.renderScaleY = (float)vk.renderHeight / (float)glConfig.vidHeight;
	vk_begin_render_pass( vk.waterMirror.render_pass, vk.waterMirror.framebuffer, qtrue, vk.renderWidth, vk.renderHeight );
}
