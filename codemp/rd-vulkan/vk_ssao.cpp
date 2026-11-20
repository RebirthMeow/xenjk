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

#ifdef USE_VK_SSAO

//
// SSAO
//
// r_charAO's pass (RB_CharOcclusion, from the depth prepass): GTAO at the character settings, whatever r_ssao's mode
static qboolean ssaoCharPass;

static void vk_begin_ssao_extract_render_pass( void )
{
    VkFramebuffer frameBuffer = vk.framebuffers.ssao.extract;

    vk.renderWidth = vk.ssao.width;
    vk.renderHeight = vk.ssao.height;

    vk.renderScaleX = vk.renderScaleY = 1.0f;

    if ( ssaoCharPass )
        vk_gputime_next_label( "char occ extract" );
    vk_begin_render_pass( vk.render_pass.ssao.extract.handle, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}

// horizontal: extract image -> blur image; vertical: blur image -> extract image (both images and framebuffers
// share one format, so the blur render pass is compatible with the extract framebuffer)
static void vk_begin_ssao_blur_render_pass( qboolean vertical )
{
    VkFramebuffer frameBuffer = vertical ? vk.framebuffers.ssao.extract : vk.framebuffers.ssao.blur;

    vk.renderWidth = vk.ssao.width;
    vk.renderHeight = vk.ssao.height;

    vk.renderScaleX = vk.renderScaleY = 1.0f;

    if ( ssaoCharPass )
        vk_gputime_next_label( "char occ blur" );
    vk_begin_render_pass( vk.render_pass.ssao.blur.handle, frameBuffer, qfalse, vk.renderWidth, vk.renderHeight );
}

static qboolean vk_require_ssao_blur(void)
{
	return r_ssao_blurRadius->integer > 0 ? qtrue : qfalse;
}


// r_charOccRect: the character pass's readers' rectangle (x0 y0 x1 y1 in target pixels), grown by `grow` pixels and
// set as the scissor; the target outside it keeps stale texels nobody reads this frame
static const int *ssaoCharRect;

static void vk_ssao_char_scissor( int grow )
{
	if ( !ssaoCharPass || !ssaoCharRect )
		return;
	VkRect2D scissor;
	const int x0 = MAX( ssaoCharRect[0] - grow, 0 ), y0 = MAX( ssaoCharRect[1] - grow, 0 );
	const int x1 = MIN( ssaoCharRect[2] + grow, (int)vk.ssao.width ), y1 = MIN( ssaoCharRect[3] + grow, (int)vk.ssao.height );
	scissor.offset.x = x0;
	scissor.offset.y = y0;
	scissor.extent.width = (uint32_t)MAX( x1 - x0, 0 );
	scissor.extent.height = (uint32_t)MAX( y1 - y0, 0 );
	qvkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor );
}

// r_charOccRect 3 (check): the pass's final target zeroed whole before its scissored draw, so a reader outside the
// rectangle shows fully occluded (black) instead of last frame's texels
static void vk_ssao_char_sentinel( void )
{
	if ( !ssaoCharPass || !ssaoCharRect || r_charOccRect->integer < 3 )
		return;
	VkClearAttachment attachment;
	VkClearRect clearRect;
	attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	attachment.colorAttachment = 0;
	Com_Memset( &attachment.clearValue, 0, sizeof( attachment.clearValue ) );
	clearRect.rect.offset.x = 0;
	clearRect.rect.offset.y = 0;
	clearRect.rect.extent.width = vk.ssao.width;
	clearRect.rect.extent.height = vk.ssao.height;
	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;
	qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &attachment, 1, &clearRect );
}

// the blur's reach in target pixels (ssao_blur.frag: up to 8 taps each way)
static int vk_ssao_char_blur_reach( void )
{
	return vk_require_ssao_blur() ? MIN( MAX( r_charContactBlur->integer, 0 ), 8 ) + 1 : 0;
}

static void vk_ssao_extract( uint32_t width, uint32_t height, uint32_t screen_width, uint32_t screen_height )
{
	vk_begin_ssao_extract_render_pass();

	// renderwidth/height migh need to be checked in inspector branch
	// and use vidwidth/height
	vk_set_viewport_scissor( width, height );
	if ( !vk_require_ssao_blur() )
		vk_ssao_char_sentinel();	// the final target
	vk_ssao_char_scissor( 2 * vk_ssao_char_blur_reach() );	// both blur directions read around the rectangle

	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ssao.extract.pipeline );
	{
		// r_charSelfShadowScreen: set 1 the sun shadow atlas (the depth set stands in when there is none; the shader only
		// samples it when the push constants name a channel), set 2 this view's camera constants (its self-shadow tiles)
		uint32_t offsets[VK_DESC_UNIFORM_COUNT];
		for ( int i = 0; i < VK_DESC_UNIFORM_COUNT; i++ )
			offsets[i] = vk.cmd->descriptor_set.offset[i];
		offsets[VK_DESC_UNIFORM_CAMERA_BINDING] = vk.cmd->camera_ubo_offset;
		// r_charOccHalfDepth: the extract's half-size copy (vk_depth_extract_half) in the character pass
		VkDescriptorSet sets[3] = { vk.ssaoHalfDepth ? vk.depth.extract.halfDescriptor : vk.depth.extract.descriptor, vk.sunShadow.descriptor ? vk.sunShadow.descriptor : vk.depth.extract.descriptor,
			vk.cmd->uniform_descriptor };
		qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ssao.extract.pipeline_layout, 0, 3, sets, VK_DESC_UNIFORM_COUNT, offsets );
	}

	{
		vkExtractSSAO_t push = { 0 };

		const float *p = backEnd.viewParms.projectionMatrix;
		push.projection[0] = (p[0] != 0.0f) ? 1.0f / p[0] : 1.0f;
		push.projection[1] = (p[5] != 0.0f) ? 1.0f / p[5] : 1.0f;
		push.projection[2] = p[10];
		push.projection[3] = p[14];

#ifdef USE_VK_IMGUI
		push.texture_scale[0] = (float)width / (float)screen_width;
		push.texture_scale[1] = (float)height / (float)screen_height;
#endif

		push.radius				= r_ssao_radius->value;
		push.bias				= r_ssao_bias->value;
		push.intensity			= r_ssao_intensity->value;
		push.power				= r_ssao_power->value;

		push.samples			= int(r_ssao_samples->integer);
		push.inv_width  = width  > 0 ? 1.0f / (float)width  : 1.0f;
		push.inv_height = height > 0 ? 1.0f / (float)height : 1.0f;
		push.reverse_depth		= 0.0;

		push.depth_falloff		= r_ssao_falloff->value;
		push.character			= r_ssao_character->value;
		push.mode				= r_ssao->integer;
		push.slices				= 3;
		push.steps				= 6;

		push.frame_noise		= (float)( tr.frameCount & 63 ) * 0.0625f;

		if ( ssaoCharPass ) {
			// cosine-weighted horizon visibility, what a dome of ambient light sees; pow( visibility, intensity )
			push.mode			= 2;
			push.radius			= r_charAORadius->value;
			push.intensity		= r_charAOPower->value;
			// r_charAOSlices x 2 sides x r_charAOSteps depth taps per pixel
			push.slices			= (uint32_t)Com_Clamp( 1, 8, (float)r_charAOSlices->integer );
			push.steps			= (uint32_t)Com_Clamp( 1, 16, (float)r_charAOSteps->integer );

			// r_charContactShadows' masks: the lights in the shader's eye space, which reconstructs positions from
			// uv without the projection's translation and with y running down the screen
			const float *m = backEnd.viewParms.world.modelViewMatrix;
			const float sx = ( p[0] != 0.0f ) ? p[12] / p[0] : 0.0f, sy = ( p[5] != 0.0f ) ? p[13] / p[5] : 0.0f;
			const int count = R_CHAR_CONTACT( backEnd.refdef.rdflags ) > 0.0f ? backEnd.charOccNumLights : 0;
			for ( int i = 0; i < count; i++ ) {
				const float *l = backEnd.charOccLight[i];
				const qboolean point = l[3] >= 5.0f ? qtrue : qfalse;
				const float size = point ? l[3] - 10.0f : l[3];
				float e[3];
				for ( int k = 0; k < 3; k++ )
					e[k] = m[k] * l[0] + m[4 + k] * l[1] + m[8 + k] * l[2] + ( point ? m[12 + k] : 0.0f );
				push.lights[i][0] = e[0] + ( point ? sx : 0.0f );
				push.lights[i][1] = -( e[1] + ( point ? sy : 0.0f ) );
				push.lights[i][2] = e[2];
				push.lights[i][3] = ( point ? 10.0f : 0.0f ) + ( size > 0.0f ? size : DEG2RAD( r_charContactSoftness->value ) );
			}
			push.character		= 2.0f + (float)count;
			push.bias			= R_CHAR_CONTACT( backEnd.refdef.rdflags );
			push.samples		= r_charContactThickness->value;
			push.reverse_depth	= (float)r_charContactRays->integer;
			push.depth_falloff	= (float)r_charContactSteps->integer;
			push.power			= r_charHairOpacity->value;

			// r_charSelfShadowScreen: the grid light's channel takes the first self-shadow tile (camera constants, set 2)
			int gridChannel = 0;
			for ( int i = 0; i < count; i++ )
				if ( backEnd.charOccLightSource[i] < 0 )
					gridChannel = i + 1;
			if ( r_charSelfShadowScreen->integer && vk.sunShadow.descriptor && gridChannel )
				push.slices |= (uint32_t)gridChannel << 4;
			// r_charOccMask: only near the prepass's marked model pixels
			if ( vk.ssaoCharOccMask )
				push.slices |= 1u << 6;
			// r_charAORadiusCap: GTAO's screen radius cap (close characters reach it; fewer cache-missing taps)
			push.slices |= (uint32_t)Com_Clamp( 8, 255, (float)r_charAORadiusCap->integer ) << 8;
		}

		qvkCmdPushConstants( vk.cmd->command_buffer, vk.ssao.extract.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	}

	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk_end_render_pass();
}

static void vk_ssao_blur( uint32_t width, uint32_t height, uint32_t screen_width, uint32_t screen_height, qboolean vertical )
{
	vk_begin_ssao_blur_render_pass( vertical );
	vk_set_viewport_scissor( width, height );
	if ( vertical )
		vk_ssao_char_sentinel();	// the final target
	vk_ssao_char_scissor( vertical ? 0 : vk_ssao_char_blur_reach() );	// the vertical pass reads the horizontal one's around it
	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ssao.blur.pipeline );

	VkDescriptorSet sets[2] = {
		vertical ? vk.ssao.blur.descriptor : vk.ssao.extract.descriptor,
		vk.ssaoHalfDepth ? vk.depth.extract.halfDescriptor : vk.depth.extract.descriptor
	};
	qvkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.ssao.blur.pipeline_layout, 0, 2, sets, 0, NULL );

	{
		vkBlurSSAO_t push = { 0 };
		push.projection[0]		= (backEnd.viewParms.projectionMatrix[0] != 0.0f) ? 1.0f / backEnd.viewParms.projectionMatrix[0] : 1.0f;
		push.projection[1]		= (backEnd.viewParms.projectionMatrix[5] != 0.0f) ? 1.0f / backEnd.viewParms.projectionMatrix[5] : 1.0f;
		push.projection[2]		= backEnd.viewParms.projectionMatrix[10];
		push.projection[3]		= backEnd.viewParms.projectionMatrix[14];

#ifdef USE_VK_IMGUI
		push.texture_scale[0] = (float)width / (float)screen_width;
		push.texture_scale[1] = (float)height / (float)screen_height;
#endif

		push.depth_radius		= (float)( ssaoCharPass ? r_charContactBlur->integer : r_ssao_blurRadius->integer );
		push.depth_sharpness	= 48.0f;
		push.inv_width  = width  > 0 ? 1.0f / (float)width  : 1.0f;
		push.inv_height = height > 0 ? 1.0f / (float)height : 1.0f;
		push.direction[0]		= vertical ? 0.0f : 1.0f;
		push.direction[1]		= vertical ? 1.0f : 0.0f;
		push.mask_params[0]		= ( ssaoCharPass && vk.ssaoCharOccMask ) ? 1.0f : 0.0f;

		qvkCmdPushConstants( vk.cmd->command_buffer, vk.ssao.blur.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	}

	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );

	vk_end_render_pass();
}

void vk_ssao_extract_blur( void )
{
	vk_ssao_extract_blur_mode( qfalse, NULL );
}

void vk_ssao_extract_blur_mode( qboolean charPass, const int *charRect )
{
	if ( !vk.ssaoActive /*|| backEnd.doneSSAO*/ )
		return;

	ssaoCharPass = charPass;
	ssaoCharRect = charRect;

	uint32_t width			= vk.ssao.width;
	uint32_t height			= vk.ssao.height;
	uint32_t screen_width	= vk.ssao.width;
	uint32_t screen_height	= vk.ssao.height;

	const qboolean do_blur = vk_require_ssao_blur();

	vk_ssao_extract( width, height, screen_width, screen_height );

	if ( do_blur ) {
		// separable bilateral blur; the result ends in the extract image
		vk_ssao_blur( width, height, screen_width, screen_height, qfalse );
		vk_ssao_blur( width, height, screen_width, screen_height, qtrue );
	}

	ssaoCharPass = qfalse;
	ssaoCharRect = NULL;
}

void vk_ssao_blend( void )
{
	if ( !vk.ssaoActive /*|| backEnd.doneSSAO*/ )
		return;

	qvkCmdBindPipeline( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.renderPassIndex == RENDER_PASS_POST_1X ? vk.ssao_blend_pipeline_1x : vk.ssao.blend.pipeline );

	// the separable blur ends in the extract image, so it holds the result with or without blur
	qvkCmdBindDescriptorSets( 
		vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_blend, 0, 1, 
		&vk.ssao.extract.descriptor, 
		0, NULL 
	);

	qvkCmdDraw( vk.cmd->command_buffer, 4, 1, 0, 0 );
	//backEnd.doneSSAO = qtrue;
}
#endif //USE_VK_SSAO