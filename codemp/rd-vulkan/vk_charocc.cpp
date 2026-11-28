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

// Character occlusion: r_charAO's screen AO and r_charContactShadows' contact-shadow masks for the view that draws
// characters, computed after its depth prepass. RB_DrawSurfs calls RB_SetupCharOccLights and RB_CharOcclusion;
// vk_update_camera_constants calls RB_CharOccCameraConstants. The shaders are ssao.frag's character pass and the lit
// shader's lookups (gen_frag.tmpl).

#include "tr_local.h"
#include <vector>

/*
=============
RB_SetupCharOccLights

r_charContactShadows' screen masks: the Ghoul2 model nearest the camera picks the lights, its light grid direction
first (when it has directed light), then the dynamic lights brightest at it; three at most. Other lights (and other
models' grid light) trace their contact shadows in the lit shader.
=============
*/
static void *charOccPrevGhoul2;	// last frame's choice

void RB_SetupCharOccLights( void )
{
	const trRefdef_t *fd = &backEnd.refdef;
	const trRefEntity_t *ent = NULL;
	float bestCoverage = 0.0f, score[3];
	int i, j, n = 0;

	backEnd.charOccNumLights = 0;
	backEnd.charOccGhoul2 = NULL;
	if ( !backEnd.charOcclusion || R_CHAR_CONTACT( backEnd.refdef.rdflags ) <= 0.0f || !r_charContactMask->integer )
		return;

	for ( i = 0; i < fd->num_entities; i++ ) {
		const trRefEntity_t *e = &fd->entities[i];
		if ( e->e.reType != RT_MODEL || !e->e.ghoul2 || ( e->e.renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) ) )
			continue;
		const float dist = Distance( e->e.origin, fd->vieworg );
		// the largest on screen (its posed joints' box over the distance; a box-less model counts as
		// 32 units), last frame's choice preferred by a quarter, so a dropped weapon or prop nearer the camera does
		// not take the masks and two candidates do not swap them frame to frame
		vec3_t mins, maxs;
		float size = 32.0f;
		if ( RB_Ghoul2JointBoxEx( e, mins, maxs, qtrue ) )
			size = Q_max( Distance( mins, maxs ), 1.0f );
		float coverage = size / Q_max( dist, 1.0f );
		if ( e->e.ghoul2 == charOccPrevGhoul2 )
			coverage *= 1.25f;
		if ( !ent || coverage > bestCoverage ) {
			bestCoverage = coverage;
			ent = e;
		}
	}
	charOccPrevGhoul2 = ent ? ent->e.ghoul2 : NULL;
	if ( !ent )
		return;

	// the grid light: light-vector shaders take lightDir as the world direction (gen_vert localLightOrigin, w 0)
	if ( ent->directedLight[0] + ent->directedLight[1] + ent->directedLight[2] > 3.0f ) {
		VectorCopy( ent->lightDir, backEnd.charOccLight[n] );
		VectorNormalize( backEnd.charOccLight[n] );
		backEnd.charOccLight[n][3] = 0.0f;	// a direction, r_charContactSoftness
		backEnd.charOccLightSource[n] = -1;
		score[n] = 1e30f;
		n++;
	}

	// dynamic lights, brightest at the model first (insertion into the remaining slots)
	for ( i = 0; i < fd->num_dlights; i++ ) {
		const dlight_t *dl = &fd->dlights[i];
		const float dist = Distance( dl->origin, ent->e.origin );
		const float d2 = dist * dist;
		if ( d2 >= dl->radius * dl->radius )
			continue;
		const float s = ( dl->color[0] + dl->color[1] + dl->color[2] ) * dl->radius * dl->radius / ( d2 > 1.0f ? d2 : 1.0f );
		for ( j = n; j > 0 && score[j - 1] < s; j-- ) {
			if ( j < 3 ) {
				VectorCopy( backEnd.charOccLight[j - 1], backEnd.charOccLight[j] );
				backEnd.charOccLight[j][3] = backEnd.charOccLight[j - 1][3];
				backEnd.charOccLightSource[j] = backEnd.charOccLightSource[j - 1];
				score[j] = score[j - 1];
			}
		}
		if ( j < 3 ) {
			VectorCopy( dl->origin, backEnd.charOccLight[j] );
			backEnd.charOccLight[j][3] = 10.0f + dl->size;	// a point light: 10 + its size
			backEnd.charOccLightSource[j] = i;
			score[j] = s;
			if ( n < 3 )
				n++;
		}
	}

	backEnd.charOccNumLights = n;
	backEnd.charOccGhoul2 = ent->e.ghoul2;

	// r_charOcclusionDebug -1: the masks' model and its grid light, for a Cycles render lit the same way
	if ( r_charOcclusionDebug->integer == -1 )
		ri.Printf( PRINT_ALL, "charOccGrid origin %.1f %.1f %.1f fwd %.3f %.3f %.3f left %.3f %.3f %.3f lightDir %.3f %.3f %.3f ambient %.1f %.1f %.1f directed %.1f %.1f %.1f\n",
			ent->e.origin[0], ent->e.origin[1], ent->e.origin[2], ent->e.axis[0][0], ent->e.axis[0][1], ent->e.axis[0][2],
			ent->e.axis[1][0], ent->e.axis[1][1], ent->e.axis[1][2], ent->lightDir[0], ent->lightDir[1], ent->lightDir[2],
			ent->ambientLight[0], ent->ambientLight[1], ent->ambientLight[2], ent->directedLight[0], ent->directedLight[1], ent->directedLight[2] );
}

/*
=============
RB_CharOcclusion

r_charAO / r_charContactShadows: the main pass is ended after the depth prepass (opaque world and models; alpha-tested
stages such as hair are not in it), the prepass's linear depth is extracted and, for r_charAO, SSAO computed and
blurred from it; the main pass then resumes with everything loaded. The lit pass samples both (gen_frag: screen AO
on light-grid lit models' ambient, contact shadows toward their lights). The post-pass SSAO runs again afterwards.
=============
*/
static void RB_CharOccluders( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	vk_begin_char_occluder_render_pass();
	backEnd.isCharOccluderPass = qtrue;
	vk_restore_draw_state( qfalse );
	RB_RenderDrawSurfList( drawSurfs, numDrawSurfs );
	backEnd.isCharOccluderPass = qfalse;
	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_MAIN;
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
}

#ifdef USE_VK_SSAO
// r_charOccRect: the screen rectangle (ssao-target pixels x0 y0 x1 y1) holding every fragment that reads the
// occlusion pass this frame. Readers are light-vector stages (gen_frag ScreenAO, the grid light's contact mask) and,
// while a dynamic light has a mask channel, material stages (its contact mask). A world surface among them, an entity
// whose bounds are unknown, or a corner behind the eye leaves the pass full-screen (qfalse).
// Entities: a box in model space (Ghoul2: this frame's posed joints plus r_charOccRectMargin; models: frame 0's
// bounds, half again for animation), its eye-space box clipped at the near plane and projected
static qboolean RB_CharOccRectFind( const drawSurf_t *drawSurfs, int numDrawSurfs, int rect[4], const char **why )
{
	const viewParms_t *vp = &backEnd.viewParms;
	if ( !r_charOccRect->integer || vp->viewportX != 0 || vp->viewportY != 0
		|| vp->viewportWidth != glConfig.vidWidth || vp->viewportHeight != glConfig.vidHeight ) {
		*why = r_charOccRect->integer ? "viewport" : "off";
		return qfalse;
	}

	qboolean dlightMasks = qfalse;
	for ( int c = 0; c < backEnd.charOccNumLights; c++ )
		if ( backEnd.charOccLightSource[c] >= 0 )
			dlightMasks = qtrue;

	static byte seen[MAX_REFENTITIES];
	Com_Memset( seen, 0, sizeof( seen ) );
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	const float *m = vp->world.modelViewMatrix, *p = vp->projectionMatrix;
	const float W = (float)glConfig.vidWidth, H = (float)glConfig.vidHeight;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		int entityNum, cubemapIndex;
		shader_t *shader;
		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &cubemapIndex );

		qboolean reads = qfalse;
		for ( int s = 0; s < MAX_SHADER_STAGES && shader->stages[s] && shader->stages[s]->active && !reads; s++ ) {
			const shaderStage_t *st = shader->stages[s];
			if ( st->bundle[0].rgbGen == CGEN_LIGHTING_DIFFUSE || st->bundle[0].rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY )
				reads = qtrue;
#ifdef USE_VK_PBR
			if ( dlightMasks && st->mat.mode != 0 )
				reads = qtrue;
#endif
		}
		if ( !reads )
			continue;
		if ( entityNum == REFENTITYNUM_WORLD ) {
			*why = va( "world shader %s", shader->name );
			return qfalse;
		}
		if ( seen[entityNum] )
			continue;
		seen[entityNum] = 1;

		const trRefEntity_t *ent = &backEnd.refdef.entities[entityNum];
		if ( ent->e.reType != RT_MODEL || ( ent->e.renderfx & RF_DEPTHHACK ) ) {
			*why = va( "entity %d type %d renderfx 0x%x shader %s", entityNum, ent->e.reType, ent->e.renderfx, shader->name );
			return qfalse;
		}
		// the entity's box in its model space: a Ghoul2 entity's posed joints (skeleton and hair chains,
		// RB_TransformBones) plus r_charOccRectMargin for skin, cloth and hair cards beyond them; a model's frame 0
		// bounds, half again about their centre for animation
		vec3_t mins, maxs;
		if ( ent->e.ghoul2 ) {
			if ( !RB_Ghoul2JointBox( ent, mins, maxs ) ) {
				*why = va( "entity %d (Ghoul2) has no joint box this frame", entityNum );
				return qfalse;
			}
			const float margin = MAX( r_charOccRectMargin->value, 0.0f );
			for ( int k = 0; k < 3; k++ ) {
				mins[k] -= margin;
				maxs[k] += margin;
			}
		} else {
			R_ModelBounds( ent->e.hModel, mins, maxs );
			if ( VectorCompare( mins, maxs ) ) {
				*why = va( "entity %d no bounds (shader %s)", entityNum, shader->name );
				return qfalse;
			}
			for ( int k = 0; k < 3; k++ ) {
				const float mid = 0.5f * ( mins[k] + maxs[k] ), half = 0.75f * ( maxs[k] - mins[k] );
				mins[k] = mid - half;
				maxs[k] = mid + half;
			}
		}
		// its corners to eye space (model -> world by the entity's origin and axes, which carry a model's scale; a
		// Ghoul2 model's scale is in its bone matrices), and the eye-space box around them
		vec3_t emin = { 1e30f, 1e30f, 1e30f }, emax = { -1e30f, -1e30f, -1e30f };
		for ( int c = 0; c < 8; c++ ) {
			const vec3_t q = { ( c & 1 ) ? maxs[0] : mins[0], ( c & 2 ) ? maxs[1] : mins[1], ( c & 4 ) ? maxs[2] : mins[2] };
			vec3_t w;
			VectorCopy( ent->e.origin, w );
			for ( int k = 0; k < 3; k++ )
				VectorMA( w, q[k], ent->e.axis[k], w );
			for ( int k = 0; k < 3; k++ ) {
				const float v = m[k] * w[0] + m[4 + k] * w[1] + m[8 + k] * w[2] + m[12 + k];
				emin[k] = MIN( emin[k], v );
				emax[k] = MAX( emax[k], v );
			}
		}
		// the view looks down -z: the part nearer than the near plane is not drawn; wholly there, nothing is
		const float zNear = -r_znear->value;
		if ( emin[2] >= zNear )
			continue;
		emax[2] = MIN( emax[2], zNear );
		for ( int c = 0; c < 8; c++ ) {
			const float x = ( c & 1 ) ? emax[0] : emin[0];
			const float y = ( c & 2 ) ? emax[1] : emin[1];
			const float z = ( c & 4 ) ? emax[2] : emin[2];
			const float w = p[3] * x + p[7] * y + p[11] * z + p[15];
			if ( w <= 0.0f ) {
				*why = va( "entity %d: a corner at w %g", entityNum, w );
				return qfalse;
			}
			const float nx = Com_Clamp( -1.0f, 1.0f, ( p[0] * x + p[4] * y + p[8] * z + p[12] ) / w );
			const float ny = Com_Clamp( -1.0f, 1.0f, ( p[1] * x + p[5] * y + p[9] * z + p[13] ) / w );
			// the framebuffer's y runs down (gen_frag ScreenUV)
			x0 = MIN( x0, ( 0.5f + 0.5f * nx ) * W ); x1 = MAX( x1, ( 0.5f + 0.5f * nx ) * W );
			y0 = MIN( y0, ( 0.5f - 0.5f * ny ) * H ); y1 = MAX( y1, ( 0.5f - 0.5f * ny ) * H );
		}
	}

	// to the pass's size: one texel for the lit pass's bilinear read
	const float sx = (float)vk.ssao.width / W, sy = (float)vk.ssao.height / H;
	rect[0] = MAX( (int)floorf( x0 * sx ) - 1, 0 );
	rect[1] = MAX( (int)floorf( y0 * sy ) - 1, 0 );
	rect[2] = MIN( (int)ceilf( x1 * sx ) + 1, (int)vk.ssao.width );
	rect[3] = MIN( (int)ceilf( y1 * sy ) + 1, (int)vk.ssao.height );
	if ( rect[2] <= rect[0] || rect[3] <= rect[1] )
		rect[0] = rect[1] = rect[2] = rect[3] = 0;	// no reader: an empty rectangle
	*why = "rect";
	return qtrue;
}

// r_charOccRect 2: also print the outcome once a second (the rectangle, or why the pass stays full-screen)
static qboolean RB_CharOccRect( const drawSurf_t *drawSurfs, int numDrawSurfs, int rect[4] )
{
	const char *why = "";
	const qboolean ok = RB_CharOccRectFind( drawSurfs, numDrawSurfs, rect, &why );
	static int lastPrint;
	if ( r_charOccRect->integer >= 2 && ( backEnd.refdef.time - lastPrint >= 1000 || backEnd.refdef.time < lastPrint ) ) {
		lastPrint = backEnd.refdef.time;
		if ( ok )
			ri.Printf( PRINT_ALL, "charOccRect %d %d %d %d of %d x %d\n", rect[0], rect[1], rect[2], rect[3], vk.ssao.width, vk.ssao.height );
		else
			ri.Printf( PRINT_ALL, "charOccRect full-screen: %s\n", why );
	}
	return ok;
}
#endif

// the scene's water takes its bed from the depth prepass, which leaves alpha-tested stages out: hair under the water was
// covered as if the bed lay behind it. Without character occlusion (which draws them into the extract itself), the
// scene's occlusion calls this in its split of the main pass (outside any render pass): the alpha-tested character
// stages' linear depth into the extract's g (r cleared to 0, "empty"; nothing else reads it this view), for the water
// alone. qtrue when drawn (vk_scene_char_bed then tells the water)
qboolean RB_WaterHairBed( void )
{
	drawSurf_t *surfs = backEnd.hairBedSurfs;
	const int num = backEnd.hairBedNum;
	if ( !surfs || num <= 0 || r_charOccHair->integer <= 0 || !vk.depth.extract.enabled || !vk.depth.extract.framebuffer
		|| vk.render_pass.char_occluder.handle == VK_NULL_HANDLE || backEnd.viewParms.portalView != PV_NONE )
		return qfalse;
	// the alpha-tested opaque stages of Ghoul2 models (as RB_RenderDrawSurfList's occluder pass takes) on models with
	// water beside them, between their feet and a little over their heads; none (the crowded square), no pass
	static std::vector<drawSurf_t> bed;	// (not "near": a Windows macro)
	static byte state[MAX_REFENTITIES];	// 0 untested, 1 near water, 2 not
	Com_Memset( state, 0, sizeof( state ) );
	bed.clear();
	for ( int i = 0; i < num; i++ ) {
		int entityNum, cubemapIndex;
		shader_t *shader;
		R_DecomposeSort( surfs[i].sort, &entityNum, &shader, &cubemapIndex );
		if ( entityNum == REFENTITYNUM_WORLD || entityNum >= MAX_REFENTITIES || state[entityNum] == 2 )
			continue;
		if ( shader->sort != SS_OPAQUE || !shader->stages[0] || !( shader->stages[0]->stateBits & GLS_ATEST_BITS ) )
			continue;
		if ( !state[entityNum] ) {
			const refEntity_t *e = &backEnd.refdef.entities[entityNum].e;
			state[entityNum] = e->ghoul2 && !( e->renderfx & ( RF_DEPTHHACK | RF_FIRST_PERSON ) ) && R_SceneWaterNear( e->origin ) ? 1 : 2;
			if ( state[entityNum] == 2 )
				continue;
		}
		bed.push_back( surfs[i] );
	}
	if ( bed.empty() )
		return qfalse;

	vk_gputime_next_label( "water hair bed" );
	vk_begin_char_occluder_render_pass();
	{
		VkClearAttachment clear;
		VkClearRect rect;
		Com_Memset( &clear, 0, sizeof( clear ) );
		clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clear.colorAttachment = 0;
		clear.clearValue.color.float32[1] = 1e9f;	// (depth_extract's "none")
		Com_Memset( &rect, 0, sizeof( rect ) );
		rect.rect.extent.width = glConfig.vidWidth;
		rect.rect.extent.height = glConfig.vidHeight;
		rect.layerCount = 1;
		qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &clear, 1, &rect );
	}
	backEnd.isCharOccluderPass = qtrue;
	vk_restore_draw_state( qfalse );
	RB_RenderDrawSurfList( bed.data(), (int)bed.size() );
	backEnd.isCharOccluderPass = qfalse;
	vk_end_render_pass();
	vk.renderPassIndex = RENDER_PASS_MAIN;
	vk.cmd->last_pipeline = VK_NULL_HANDLE;

	VkMemoryBarrier mb;	// (the water's fragments read it in this frame's main pass)
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 1, &mb, 0, NULL, 0, NULL );
	vk_scene_char_bed();
	return qtrue;
}

void RB_CharOcclusion( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	// r_gpuTimes: the end of the split main pass (its MSAA resolve and stores) apart from the prepass
	vk_gputime_split( "main keep end" );
	vk_end_render_pass();

	// r_charOccMask: model pixels arrive marked from the depth prepass (negative depth in the extract)
	vk_gputime_next_label( "char occ depth extract" );
	vk_depth_extract_ex( glConfig.vidWidth, glConfig.vidHeight, backEnd.charOccMark );
	// r_charOccHair: hair and lashes go into the depth extract's second channel; they occlude in the AO and partly
	// (r_charHairOpacity) in the contact masks, which are blurred; the lit shader's own fine rays read opaque depth only
	if ( r_charOccHair->integer > 0 )
		RB_CharOccluders( drawSurfs, numDrawSurfs );
#ifdef USE_VK_SSAO
	// r_charOccHalfDepth: the pass reads a half-size copy of the extract (the lit pass's own rays keep the full one)
	vk.ssaoHalfDepth = vk_depth_extract_half();
	vk.ssaoCharOccMask = backEnd.charOccMark;
	int rect[4];
	vk_ssao_extract_blur_mode( qtrue, RB_CharOccRect( drawSurfs, numDrawSurfs, rect ) ? rect : NULL );
	vk.ssaoHalfDepth = qfalse;
	vk.ssaoCharOccMask = qfalse;
#endif
	backEnd.charOccExtracted = qtrue;
	// the hair's depth is the water's bed too (the prepass that built it left the alpha-tested hair out)
	if ( r_charOccHair->integer > 0 )
		vk_scene_char_bed();

	// r_charOccMask: the prepass's marks go, so the lit pass marks characters (r_ssao_character) as it always did;
	// only the world SSAO's own extract reads them, so they stay when it is off or reuses this extract
	qboolean clearMarks = backEnd.charOccMark;
#ifdef USE_VK_SSAO
	if ( !r_ssaoWorld->integer || ( r_ssaoReuseExtract->integer && vk.msaaActive ) )	// reuse: vk_one_resolve_pass
		clearMarks = qfalse;
#endif
	if ( clearMarks )
		vk_gputime_next_label( "char occ stencil clear" );
	vk_begin_main_reload_render_pass();
	if ( clearMarks ) {
		VkClearAttachment clear;
		VkClearRect rect;
		Com_Memset( &clear, 0, sizeof( clear ) );
		clear.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		Com_Memset( &rect, 0, sizeof( rect ) );
		rect.rect.extent.width = glConfig.vidWidth;
		rect.rect.extent.height = glConfig.vidHeight;
		rect.layerCount = 1;
		qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &clear, 1, &rect );
		vk_gputime_split( "main reload" );
	}
	vk_restore_draw_state( qfalse );
}

// the camera block's character occlusion terms (vk_update_camera_constants)
void RB_CharOccCameraConstants( vkUniformCamera_t &uniform, const trRefdef_t *refdef, const viewParms_t *viewParms )
{
	// character occlusion (RB_CharOcclusion): only for the view that computes it after its prepass
	Com_Memcpy( uniform.viewMatrix, viewParms->world.modelViewMatrix, sizeof( float ) * 16 );
	if ( backEnd.charOcclusion ) {
		uniform.charOccParams[0] = R_CHAR_AO( refdef->rdflags );
		uniform.charOccParams[1] = R_CHAR_CONTACT( refdef->rdflags );
		uniform.charOccParams[2] = r_charContactThickness->value;
		uniform.charOccFine = r_charContactFine->value;
		uniform.charOccHairOpacity = r_charOccHair->integer > 0 ? r_charHairOpacity->value : 0.0f;
		uniform.charOccGridShare = r_charContactGridShare->value;
		uniform.charOccParams[3] = (float)r_charContactSteps->integer;
	}
	uniform.charOccProj[0] = viewParms->projectionMatrix[0];
	uniform.charOccProj[1] = viewParms->projectionMatrix[5];
	uniform.charOccProj[2] = viewParms->projectionMatrix[8];
	uniform.charOccProj[3] = viewParms->projectionMatrix[9];
	uniform.charOccInfo[0] = backEnd.charOcclusion ? (float)r_charOcclusionDebug->integer : 0.0f;
	uniform.charOccInfo[1] = viewParms->projectionMatrix[12];
	uniform.charOccInfo[2] = viewParms->projectionMatrix[13];
	uniform.charOccInfo[3] = DEG2RAD( r_charContactSoftness->value );
	// the depth extract covers the whole target, but ScreenUV maps the view's clip
	// space to 0..1 of it, so a view drawn into part of the screen (a menu panel, cg_viewsize) traced its contact rays at
	// the wrong texels. ScreenUV is affine in these terms: fold the viewport's scale and offset into charOccProj (exact
	// while projection[12] and [13] are 0, as for every non-stereo view; ssao.frag reads charOccInfo.yz, left as is)
	if ( glConfig.vidWidth > 0 && glConfig.vidHeight > 0
		&& viewParms->projectionMatrix[12] == 0.0f && viewParms->projectionMatrix[13] == 0.0f ) {
		const float W = (float)glConfig.vidWidth, H = (float)glConfig.vidHeight;
		const float sx = viewParms->viewportWidth / W, sy = viewParms->viewportHeight / H;
		const float ox = viewParms->viewportX / W, oy = ( H - viewParms->viewportY - viewParms->viewportHeight ) / H;	// from the top
		if ( sx != 1.0f || sy != 1.0f || ox != 0.0f || oy != 0.0f ) {
			// uv_target = o + s uv_view; with uv = 0.5 + 0.5 ndc (x) and 0.5 - 0.5 ndc (y): ndc' = s ndc + c
			const float cx = 2.0f * ox + sx - 1.0f, cy = 1.0f - 2.0f * oy - sy;
			uniform.charOccProj[0] = sx * viewParms->projectionMatrix[0];
			uniform.charOccProj[1] = sy * viewParms->projectionMatrix[5];
			uniform.charOccProj[2] = sx * viewParms->projectionMatrix[8] - cx;
			uniform.charOccProj[3] = sy * viewParms->projectionMatrix[9] - cy;
		}
	}
	if ( backEnd.charOcclusion && r_charOcclusionDebug->integer >= 5 ) {
		const float *m = viewParms->world.modelViewMatrix, *p = viewParms->projectionMatrix;
		const float *o = refdef->vieworg;
		ri.Printf( PRINT_ALL, "charOcc proj %g %g %g %g %g %g %g %g %g %g %g %g %g %g %g %g\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15] );
		ri.Printf( PRINT_ALL, "charOcc view origin -> eye %g %g %g (viewParms origin %g %g %g, vieworg %g %g %g) viewport %d %d %d %d vid %d %d\n",
			m[0] * o[0] + m[4] * o[1] + m[8] * o[2] + m[12], m[1] * o[0] + m[5] * o[1] + m[9] * o[2] + m[13],
			m[2] * o[0] + m[6] * o[1] + m[10] * o[2] + m[14], viewParms->ori.origin[0], viewParms->ori.origin[1], viewParms->ori.origin[2],
			o[0], o[1], o[2], viewParms->viewportX, viewParms->viewportY, viewParms->viewportWidth, viewParms->viewportHeight,
			glConfig.vidWidth, glConfig.vidHeight );
	}
}
