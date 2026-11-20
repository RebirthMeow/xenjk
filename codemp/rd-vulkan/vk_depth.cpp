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

static void vk_transition_depth_for_graphics(VkCommandBuffer cmd, VkImage image, qboolean toShader)
{
	VkImageMemoryBarrier barrier;
	Com_Memset(&barrier, 0, sizeof(barrier));

	VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (glConfig.stencilBits > 0)
		aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;

	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;

	barrier.subresourceRange.aspectMask = aspects;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	if (toShader)
	{
		// Depth attachment -> fragment shader sampling.
		barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		qvkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &barrier);
	}
	else
	{
		// Fragment shader sampling -> depth attachment.
		barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		qvkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &barrier);
	}
}

static void vk_begin_depth_extract_render_pass( void )
{
    VkFramebuffer frameBuffer = vk.depth.extract.framebuffer;

#ifdef USE_VK_IMGUI
    vk.renderWidth = glConfig.vidWidth;
    vk.renderHeight = glConfig.vidHeight;
#else
    vk.renderWidth = gls.captureWidth;
    vk.renderHeight = gls.captureHeight;
#endif

    vk.renderScaleX = vk.renderScaleY = 1.0f;

    vk_begin_render_pass( vk.depth.extract.render_pass.handle, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}

// charMask: character pixels (stencil VK_STENCIL_AO_MASK) get a negative depth (r_ssao_character); the pre-lit
// extract after the depth prepass (r_charAO) runs before any character set the bit and passes qfalse
void vk_depth_extract_ex( uint32_t width, uint32_t height, qboolean charMask )
{
    vk_transition_depth_for_graphics( vk.cmd->command_buffer, vk.depth.image, qtrue );
    vk_begin_depth_extract_render_pass();

    qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.depth.extract.pipeline );
    {
        VkDescriptorSet sets[2] = { vk.depth.sampler_descriptor, vk.depth.stencil_descriptor };
        qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.depth.extract.pipeline_layout, 0, 2, sets, 0, NULL );
    }

	{
		vkExtractDepth_t push = { 0 };

		const float *p = backEnd.viewParms.projectionMatrix;
		push.projection[0] = (p[0] != 0.0f) ? 1.0f / p[0] : 1.0f;
		push.projection[1] = (p[5] != 0.0f) ? 1.0f / p[5] : 1.0f;
		push.projection[2] = p[10];
		push.projection[3] = p[14];

		push.inv_size[0] = width > 0 ? 1.0f / width : 1.0f;
		push.inv_size[1] = height > 0 ? 1.0f / height : 1.0f;
		// pad0.x: 1 = character pixels (stencil VK_STENCIL_AO_MASK) are written with a negative depth
		push.pad0[0] = ( charMask && vk.ssaoCharMask && vk.depth.stencil_view ) ? 1.0f : 0.0f;

		qvkCmdPushConstants( vk.cmd->command_buffer, vk.depth.extract.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( vkExtractDepth_t ), &push );
	}

    qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

    vk_end_render_pass();
    vk_transition_depth_for_graphics( vk.cmd->command_buffer, vk.depth.image,  qfalse );
}

// r_charOccHalfDepth: the extract (opaque depth, and the hair in g after RB_CharOccluders) copied to the character
// occlusion pass's half size, one texel of each 2x2 (a nearest blit), so its extract and blurs read a quarter of the
// memory. Returns qfalse (nothing copied) when off or unsupported
qboolean vk_depth_extract_half( void )
{
	VkImageMemoryBarrier b[2];

	if ( !r_charOccHalfDepth->integer || !vk.depth.extract.halfImage || !vk.depth.extract.halfDescriptor )
		return qfalse;

	Com_Memset( b, 0, sizeof( b ) );
	for ( int i = 0; i < 2; i++ ) {
		b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b[i].srcQueueFamilyIndex = b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		b[i].subresourceRange.levelCount = 1;
		b[i].subresourceRange.layerCount = 1;
	}
	// the extract as its render passes left it; the copy once the last frame's occlusion pass has read it
	b[0].image = vk.depth.extract.image;
	b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b[1].image = vk.depth.extract.halfImage;
	b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, b );

	VkImageBlit region;
	Com_Memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
	region.srcOffsets[1].x = glConfig.vidWidth;
	region.srcOffsets[1].y = glConfig.vidHeight;
	region.dstOffsets[1].x = vk.depth.extract.halfWidth;
	region.dstOffsets[1].y = vk.depth.extract.halfHeight;
	region.srcOffsets[1].z = region.dstOffsets[1].z = 1;
	qvkCmdBlitImage( vk.cmd->command_buffer, vk.depth.extract.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vk.depth.extract.halfImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST );

	b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 2, b );
	return qtrue;
}

void vk_depth_extract( uint32_t width, uint32_t height )
{
    vk_depth_extract_ex( width, height, qtrue );
}