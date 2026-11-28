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
#include "tr_allocator.h"
#include "tr_WorldEffects.h"
#include "vk_cloth.h"
#include <algorithm>

backEndData_t	*backEndData;
backEndState_t	backEnd;

//bool tr_stencilled = false;
//extern qboolean tr_distortionPrePost;
//extern qboolean tr_distortionNegate; 
//extern void RB_CaptureScreenImage(void);
//extern void RB_DistortionFill(void);

#define	MAC_EVENT_PUMP_MSEC		5

#if 0
//used by RF_DISTORTION
static inline bool R_WorldCoordToScreenCoordFloat( vec3_t worldCoord, float *x, float *y )
{
	int	xcenter, ycenter;
	vec3_t	local, transformed;
	vec3_t	vfwd;
	vec3_t	vright;
	vec3_t	vup;
	float xzi;
	float yzi;

	xcenter = glConfig.vidWidth / 2;
	ycenter = glConfig.vidHeight / 2;

	//AngleVectors (tr.refdef.viewangles, vfwd, vright, vup);
	VectorCopy(tr.refdef.viewaxis[0], vfwd);
	VectorCopy(tr.refdef.viewaxis[1], vright);
	VectorCopy(tr.refdef.viewaxis[2], vup);

	VectorSubtract (worldCoord, tr.refdef.vieworg, local);

	transformed[0] = DotProduct(local,vright);
	transformed[1] = DotProduct(local,vup);
	transformed[2] = DotProduct(local,vfwd);

	// Make sure Z is not negative.
	if(transformed[2] < 0.01)
	{
		return false;
	}

	xzi = xcenter / transformed[2] * (90.0/tr.refdef.fov_x);
	yzi = ycenter / transformed[2] * (90.0/tr.refdef.fov_y);

	*x = xcenter + xzi * transformed[0];
	*y = ycenter - yzi * transformed[1];

	return true;
}

//used by RF_DISTORTION
static inline bool R_WorldCoordToScreenCoord( vec3_t worldCoord, int *x, int *y )
{
	float	xF, yF;
	bool retVal = R_WorldCoordToScreenCoordFloat( worldCoord, &xF, &yF );
	*x = (int)xF;
	*y = (int)yF;
	return retVal;
}

//number of possible surfs we can postrender.
//note that postrenders lack much of the optimization that the standard sort-render crap does,
//so it's slower.
#define MAX_POST_RENDERS	128

typedef struct postRender_s {
	int			fogNum;
	int			entNum;
	int			dlighted;
	int			depthRange;
	drawSurf_t	*drawSurf;
	shader_t	*shader;
	qboolean	eValid;
} postRender_t;

static postRender_t g_postRenders[MAX_POST_RENDERS];
static int g_numPostRenders = 0;
#endif

/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace(void) {
	color4ub_t c;

	if ( !backEnd.isHyperspace ) {
		// do initialization shit
	}

	if ( tess.shader != tr.whiteShader ) {
		RB_EndSurface();
		vk_set_2d();
		RB_BeginSurface( tr.whiteShader, 0, 0 );
	}

#ifdef USE_VBO
	VBO_UnBind();
#endif

	vk_set_2d();

	c[0] = c[1] = c[2] = ( backEnd.refdef.time & 255 );
	c[3] = 255;

	RB_AddQuadStamp2( backEnd.refdef.x, backEnd.refdef.y, backEnd.refdef.width, backEnd.refdef.height,
		0.0, 0.0, 0.0, 0.0, c );

	RB_EndSurface();

	tess.numIndexes = 0;
	tess.numVertexes = 0;

	backEnd.isHyperspace = qtrue;
}

#ifdef USE_PMLIGHT
static void RB_LightingPass(void);
#endif

/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
static void RB_BeginDrawingView( void ) {

	// sync with gl if needed
	if ( r_finish->integer == 1 && !glState.finishCalled ) {
		vk_queue_wait_idle();

		glState.finishCalled = qtrue;
	} else if ( r_finish->integer == 0 ) {
		glState.finishCalled = qtrue;
	}

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	// ensures that depth writes are enabled for the depth clear
	//vk_clear_depthstencil_attachments(r_shadows->integer == 2 ? qtrue : qfalse);
	vk_clear_depthstencil_attachments(qtrue);

	if ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) {
		RB_Hyperspace();

		backEnd.projection2D = qfalse;

		// force depth range and viewport/scissor updates
		vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	} else {
		backEnd.isHyperspace = qfalse;
	}

	glState.faceCulling = -1;		// force face culling to set next time

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;
}

static void RB_PrepareForEntity( int entityNum, float originalTime )
{
	Vk_Depth_Range depthRange = DEPTH_RANGE_NORMAL;

	if ( entityNum != REFENTITYNUM_WORLD )
	{
		backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
		backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime;

		// set up the transformation matrix
		R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.ori );
				
		// No depth at all, very rare but some things for seeing through walls
		if ( backEnd.currentEntity->e.renderfx & RF_NODEPTH )
			depthRange = DEPTH_RANGE_ZERO;
					
		// hack the depth range to prevent view model from poking into walls
		if (backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
			depthRange = DEPTH_RANGE_WEAPON;
	}
	else
	{
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.refdef.floatTime = originalTime;

		backEnd.ori  = backEnd.viewParms.world;
	}

	// we have to reset the shaderTime as well otherwise image animations on
	// the world (like water) continue with the wrong frame
	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	
	vk_set_depthrange( depthRange );
}


static void RB_SubmitDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs, float originalTime ) 
{
	uint32_t		i; 
	drawSurf_t		*drawSurf;	
	shader_t *shader;
	int entityNum, fogNum, cubemapIndex, reType;

	shader_t	*oldShader = nullptr;
	int			oldEntityNum = -1;
	uint32_t	oldSort = MAX_UINT;
	float		oldShaderSort = -1;
	int			oldFogNum = -1;
	int			oldReType = -1;
	int			oldCubemapIndex = -1;
	CBoneCache *oldBoneCache = nullptr;

	qboolean	push_constant;

	for ( i = 0, drawSurf = drawSurfs; i < numDrawSurfs; i++, drawSurf++ )
	{
		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &cubemapIndex );
		fogNum = drawSurf->fogIndex;

		if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[entityNum].e.renderfx & RF_DEPTHHACK )
			continue;

		// check if we have any dynamic glow surfaces before dglow pass
		if( !backEnd.hasGlowSurfaces && vk.dglowActive && !backEnd.isGlowPass && shader->hasGlow )
			backEnd.hasGlowSurfaces = qtrue;

		// if we're rendering glowing objects, but this shader has no stages with glow, skip it!
		if ( backEnd.isGlowPass && !shader->hasGlow )
			continue;

		// r_charOccHair, r_charSelfShadowHairMap: alpha-tested stages of Ghoul2 models only (hair, lashes)
		if ( ( backEnd.isCharOccluderPass || backEnd.isHairMapPass ) && ( entityNum == REFENTITYNUM_WORLD || !backEnd.refdef.entities[entityNum].e.ghoul2
			|| ( backEnd.refdef.entities[entityNum].e.renderfx & ( RF_DEPTHHACK | RF_FIRST_PERSON ) )
			|| shader->sort != SS_OPAQUE || !shader->stages[0] || !( shader->stages[0]->stateBits & GLS_ATEST_BITS ) ) )
			continue;
		// ... and not simulated cloth: an alpha-tested garment (lace edges) is fabric, not strands
		if ( ( backEnd.isCharOccluderPass || backEnd.isHairMapPass ) && *drawSurf->surface == SF_MDX
			&& RB_ClothVkIsSimSurface( ((CRenderableSurface*)drawSurf->surface)->boneCache,
				(const mdxmSurface_t *)((CRenderableSurface*)drawSurf->surface)->surfaceData ) )
			continue;

		// the depth prepass draws opaque world surfaces and models, not first-person or depth-hack ones
		if ( backEnd.isDepthPrepass && ( !shader->depthPrepass || ( entityNum != REFENTITYNUM_WORLD
			&& ( backEnd.refdef.entities[entityNum].e.reType != RT_MODEL
				|| ( backEnd.refdef.entities[entityNum].e.renderfx & ( RF_DEPTHHACK | RF_FIRST_PERSON ) ) ) ) ) )
			continue;

		if ( *drawSurf->surface == SF_MDX )
		{
			if ( ((CRenderableSurface*)drawSurf->surface)->boneCache != oldBoneCache )
			{
				RB_EndSurface();
				RB_BeginSurface(shader, fogNum, cubemapIndex);
				oldBoneCache = ((CRenderableSurface*)drawSurf->surface)->boneCache;
				vk.cmd->bones_ubo_offset = RB_GetBoneUboOffset((CRenderableSurface*)drawSurf->surface);
			}
		}

		if (shader == oldShader &&
			fogNum == oldFogNum &&
			cubemapIndex == oldCubemapIndex &&
			entityNum == oldEntityNum && 
			backEnd.refractionFill == shader->useDistortion )
		{
			// fast path, same as previous sort
			rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
			continue;
		}
		//oldSort = drawSurf->sort;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		
		push_constant = qfalse;
		reType = (entityNum == REFENTITYNUM_WORLD) ? -1 : backEnd.refdef.entities[entityNum].e.reType;
		//if (((oldSort ^ drawSurf->sort) & ~QSORT_REFENTITYNUM_MASK) || !shader->entityMergable) {
		if (shader != oldShader ||
			fogNum != oldFogNum ||
			cubemapIndex != oldCubemapIndex 
			|| ( entityNum != oldEntityNum && ( !tess.entityMergable || reType != oldReType ) ) )
		{		
			//if ( oldShader != NULL )
				RB_EndSurface();

#ifdef USE_PMLIGHT
			//#define INSERT_POINT SS_FOG

			/*if ( backEnd.refdef.numLitSurfs && oldShaderSort < INSERT_POINT && shader->sort >= INSERT_POINT ) {
				RB_LightingPass();

				oldEntityNum = -1; // force matrix setup
			}*/
			oldShaderSort = shader->sort;
#endif

			RB_BeginSurface( shader, fogNum, cubemapIndex );
			oldShader = shader;
			oldFogNum = fogNum;
			oldReType = reType;
			oldCubemapIndex = cubemapIndex;

			push_constant = qtrue;
		}

		//oldSort = drawSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum )
		{
			RB_PrepareForEntity( entityNum, originalTime );

			if ( push_constant ) {
				Com_Memcpy(vk_world.modelview_transform, backEnd.ori.modelViewMatrix, 64);
				vk_update_mvp(NULL);
			}

			oldEntityNum = entityNum;
		}

		qboolean isDistortionShader = (qboolean)
			((shader->useDistortion == qtrue) || (backEnd.currentEntity && backEnd.currentEntity->e.renderfx & RF_DISTORTION));

		if ( backEnd.refractionFill != isDistortionShader ) {
			if ( vk.refractionActive && vk.renderPassIndex != RENDER_PASS_REFRACTION && !backEnd.hasRefractionSurfaces )
				backEnd.hasRefractionSurfaces = qtrue;

			// skip refracted surfaces in main pass, 
			// and non-refracted surfaces in refraction pass 
			continue;	
		}

		// add the triangles for this surface
		rb_surfaceTable[*drawSurf->surface]( drawSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL )
		RB_EndSurface();
}

static void vk_update_camera_constants( const trRefdef_t *refdef, const viewParms_t *viewParms ) 
{
	// set
	vkUniformCamera_t uniform = {};

	Com_Memcpy( uniform.viewOrigin, refdef->vieworg, sizeof( vec3_t) );
	uniform.viewOrigin[3] = refdef->floatTime;
	Com_Memcpy( uniform.sunDir, refdef->sunDir, sizeof( vec4_t ) );
	Com_Memcpy( uniform.sunCol, refdef->sunCol, sizeof( vec4_t ) );
	Com_Memcpy( uniform.sunAmbCol, refdef->sunAmbCol, sizeof( vec4_t ) );
	Com_Memcpy( uniform.sunShadowMvp0, refdef->sunShadowMvp[0], sizeof( float ) * 16 );
	Com_Memcpy( uniform.sunShadowMvp1, refdef->sunShadowMvp[1], sizeof( float ) * 16 );
	Com_Memcpy( uniform.sunShadowMvp2, refdef->sunShadowMvp[2], sizeof( float ) * 16 );
	Com_Memcpy( uniform.sunShadowParams, refdef->sunShadowParams, sizeof( vec4_t ) );
	Com_Memcpy( uniform.charShadowMvp, refdef->charShadowMvp, sizeof( refdef->charShadowMvp ) );
	Com_Memcpy( uniform.charShadowTile, refdef->charShadowTile, sizeof( refdef->charShadowTile ) );
	Com_Memcpy( uniform.charShadowLight, refdef->charShadowLight, sizeof( refdef->charShadowLight ) );
	Com_Memcpy( uniform.charShadowScale, refdef->charShadowScale, sizeof( refdef->charShadowScale ) );
	Com_Memcpy( uniform.charShadowRect, refdef->charShadowRect, sizeof( refdef->charShadowRect ) );
	Com_Memcpy( uniform.charShadowParams, refdef->charShadowParams, sizeof( vec4_t ) );
	Com_Memcpy( uniform.charShadowInfo, refdef->charShadowInfo, sizeof( vec4_t ) );
	Com_Memcpy( uniform.selfShadowMvp, refdef->selfShadowMvp, sizeof( refdef->selfShadowMvp ) );
	Com_Memcpy( uniform.selfShadowTile, refdef->selfShadowTile, sizeof( refdef->selfShadowTile ) );
	Com_Memcpy( uniform.selfShadowScale, refdef->selfShadowScale, sizeof( refdef->selfShadowScale ) );
	Com_Memcpy( uniform.selfShadowParams, refdef->selfShadowParams, sizeof( vec4_t ) );
	uniform.selfShadowHairGain = r_charSelfShadowHairGain->value;
	uniform.selfShadowHairLod0 = (float)r_charSelfShadowHairLod0->integer;
	// a stage (menus) has its own skin settings (r_stageSkinSoft*), whatever the play settings
	uniform.skinSoftNormals = R_IS_STAGE( refdef->rdflags ) ? r_stageSkinSoftNormals->value : r_charSkinSoftNormals->value;
	uniform.skinSoftAlbedo = R_IS_STAGE( refdef->rdflags ) ? r_stageSkinSoftAlbedo->value : r_charSkinSoftAlbedo->value;
	{	// parsed when the cvar changes, not per view
		static float	debugBox[3];
		static int		debugBoxCount = -1;
		if ( debugBoxCount != r_charSelfShadowDebugBox->modificationCount ) {
			debugBoxCount = r_charSelfShadowDebugBox->modificationCount;
			debugBox[0] = 0.5f; debugBox[1] = 0.5f; debugBox[2] = 1.0f;
			sscanf( r_charSelfShadowDebugBox->string, "%f %f %f", &debugBox[0], &debugBox[1], &debugBox[2] );
		}
		uniform.selfShadowDebugBox[0] = debugBox[0]; uniform.selfShadowDebugBox[1] = debugBox[1]; uniform.selfShadowDebugBox[2] = debugBox[2];
	}
	uniform.selfShadowSlopeBias = 0.0f;
	uniform.selfShadowSlopeBias = r_charSelfShadowSlopeBias->value * refdef->selfShadowTexel;	// layer uv; / selfShadowScale.x = world units
	uniform.selfShadowInMask = ( ( r_charSelfShadowScreen->integer || R_IS_STAGE( refdef->rdflags ) ) && R_CHAR_CONTACT( refdef->rdflags ) > 0.0f && vk.sunShadow.descriptor ) ? 1.0f : 0.0f;
	// r_charSelfShadowHairMap: the first self-shadow tile's depth view leaves its hair to the map (RB_RenderSunShadowCascades)
	{
		const qboolean hairMap = ( vk.hairMap.descriptor && refdef->selfShadowTile[0][3] > 0.0f ) ? qtrue : qfalse;
		uniform.hairMapDepthView = ( hairMap && viewParms->selfShadowSlot == 1 ) ? 1.0f : 0.0f;
		uniform.hairMapTexel = hairMap ? 1.0f / (float)vk.hairMap.size : 0.0f;
		uniform.hairMapDensity = r_charSelfShadowHairDensity->value;
		uniform.hairStrandDepth = r_charSelfShadowHairStrandDepth->value;
	}
	uniform.a2cDiscard = r_a2cDiscard->integer ? 1.0f : 0.0f;
	uniform.charFastBits = (float)( r_charFast->integer & 3 );
	uniform.charAmbientFloor = Q_max( r_charAmbientFloor->value, 0.0f );
	R_SceneCharTone( uniform.charTone, uniform.charSky, uniform.charGround );
	// shading switches (global.h charReviewFix)
	uniform.charReviewFix = (float)( 0
		// the RGBA16F hair map holds normalised depth (r_charHairMap16 2: the old world units, to show the overflow)
		| ( vk.hairMap.format == VK_FORMAT_R16G16B16A16_SFLOAT && r_charHairMap16->integer != 2 ? 64 : 0 )
		| ( r_fixFmaShadows->integer ? 128 : 0 ) | ( r_fixFmaShadows->integer >= 2 ? 256 : 0 )
		| ( ( r_fmaDebug->integer & 7 ) << 9 ) );

	RB_CharOccCameraConstants( uniform, refdef, viewParms );	// vk_charocc.cpp

	/*
	const float* p = viewParms->projectionMatrix;
	float proj[16];
	Com_Memcpy(proj, p, 64);

	proj[5] = -p[5];
	//myGlMultMatrix(vk_world.modelview_transform, proj, uniform.mvp);
	myGlMultMatrix(viewParms->world.modelViewMatrix, proj, uniform.mvp);
	*/

	vk.cmd->camera_ubo_offset = vk_append_uniform( &uniform, sizeof(uniform), vk.uniform_camera_item_size );
}

static void vk_update_light_constants( const trRefdef_t *refdef ) {
	vkUniformLight_t uniform = {};

#ifdef VK_DLIGHT_GPU
	uint32_t i;
	size_t size;

	size = sizeof(vec4_t);

	if ( vk.useGPUDLight )
	{
		uniform.num_lights = refdef->num_dlights;

		for ( i = 0; i < refdef->num_dlights; i++ )
		{
			const dlight_t* light = refdef->dlights + i;

			VectorCopy(light->origin, uniform.light[i].origin);
			// r_charContactShadows: 1 + the screen mask channel holding this light's contact shadow, 0 = none
			// and 4 x its size in whole degrees (0 = r_charContactSoftness)
			uniform.light[i].origin[3] = 4.0f * (float)(int)( RAD2DEG( light->size ) + 0.5f );
			for ( int c = 0; c < backEnd.charOccNumLights; c++ )
				if ( backEnd.charOccLightSource[c] == (int)i )
					uniform.light[i].origin[3] += (float)( c + 1 );
			VectorScale(light->color, light->radius / 25.f, uniform.light[i].color);
			uniform.light[i].radius = light->radius;
		}

		size += (i * sizeof(vkUniformLightEntry_t));
	} 
	else 
	{
		uniform.num_lights = 0;
	}

	vk.cmd->light_ubo_offset = vk_append_uniform( &uniform, size, vk.uniform_light_item_size );
#else	
	uniform.item[0] = 0.0f;
	uniform.item[1] = 1.0f;
	uniform.item[2] = 2.0f;
	uniform.item[3] = 3.0f;

	vk.cmd->light_ubo_offset = vk_append_uniform( &uniform, sizeof(uniform), vk.uniform_light_item_size );
#endif
}

static void vk_update_entity_light_constants( vkUniformEntity_t &uniform, const trRefEntity_t *refEntity, const trRefdef_t *refdef )
{
	static const float normalizeFactor = 1.0f / 255.0f;

	VectorScale(refEntity->ambientLight, normalizeFactor, uniform.ambientLight);
	VectorScale(refEntity->directedLight, normalizeFactor, uniform.directedLight);
	VectorCopy(refEntity->lightDir, uniform.localLightOrigin);
	// r_charLinear: lighting mode for characters (frag: 0 gamma space, 1 linear, 2 linear + PBR Neutral);
	// r_charTerms (linear only) rides in the tens: w = mode + 10 x bits. A stage (RDF_STAGE) is always 2, the mode its
	// rig was matched to the Cycles renders in, whatever the play setting.
	if ( refEntity->e.ghoul2 && refdef && ( refdef->rdflags & RDF_STAGE ) )
		uniform.ambientLight[3] = 2.0f + 10.0f * (float)( r_charTerms->integer & 15 );
	else
		uniform.ambientLight[3] = ( refEntity->e.ghoul2 && r_charLinear->integer > 0 ) ? ( r_charLinear->integer >= 3 && R_SceneCharToneOn() ? 3.0f
			: r_charLinear->integer >= 2 ? 2.0f : 1.0f )
			+ 10.0f * (float)( r_charTerms->integer & 15 ) : 0.0f;

	uniform.localLightOrigin[3] = 0.0f;

	// r_charContactShadows: 1 + the screen mask channel of this model's grid light (the masks' model), 0 = none
	uniform.directedLight[3] = 0.0f;
	if ( refEntity->e.ghoul2 && refEntity->e.ghoul2 == backEnd.charOccGhoul2 )
		for ( int c = 0; c < backEnd.charOccNumLights; c++ )
			if ( backEnd.charOccLightSource[c] == -1 )
				uniform.directedLight[3] = (float)( c + 1 );

	// r_charFill / r_charRim / r_charAmbientFloor: Ghoul2 models lit by the light grid in a world scene (charFill.w =
	// 1 + the fill level; 0 = not a character, so a zeroed uniform takes none of it).
	// The layout follows the portrait rig (R_AddPortraitRig) about the camera's view of the model, with the grid light as
	// the key: the fill 50 deg off the camera axis on the other side from it, level; the rim from behind (180 to 205 deg) on its
	// side, 20 deg up (a grid light straight above or below picks a side arbitrarily).
	uniform.charFill[3] = 0.0f;
	uniform.charRim[3] = 0.0f;
	if ( refdef && refEntity->e.ghoul2 && !( refdef->rdflags & RDF_NOWORLDMODEL ) ) {
		vec3_t toCam;
		VectorSet( toCam, refdef->vieworg[0] - refEntity->e.origin[0], refdef->vieworg[1] - refEntity->e.origin[1], 0.0f );
		if ( VectorNormalize( toCam ) < 0.001f )
			VectorSet( toCam, 1.0f, 0.0f, 0.0f );
		const float cross = toCam[0] * refEntity->lightDir[1] - toCam[1] * refEntity->lightDir[0];
		// the side follows the sine of the angle between the camera and the light (full within about
		// 15 degrees of it), so the fill turns through the camera axis and the rim through straight behind instead
		// of jumping; a light near vertical (no horizontal part) gives the centred placement
		const float horiz = sqrtf( refEntity->lightDir[0] * refEntity->lightDir[0] + refEntity->lightDir[1] * refEntity->lightDir[1] );
		const float side = horiz > 1e-3f ? Q_min( Q_max( cross / horiz / 0.25f, -1.0f ), 1.0f ) : 0.0f;
		const float rimYawDeg = 180.0f + 25.0f * side;
		const float fillYaw = DEG2RAD( -50.0f ) * side;
		const float rimYaw = DEG2RAD( rimYawDeg ), rimUp = DEG2RAD( 20.0f );
		uniform.charFill[0] = toCam[0] * cosf( fillYaw ) - toCam[1] * sinf( fillYaw );
		uniform.charFill[1] = toCam[0] * sinf( fillYaw ) + toCam[1] * cosf( fillYaw );
		uniform.charFill[2] = 0.0f;
		uniform.charFill[3] = 1.0f + Q_max( r_charFill->value, 0.0f );
		uniform.charRim[0] = ( toCam[0] * cosf( rimYaw ) - toCam[1] * sinf( rimYaw ) ) * cosf( rimUp );
		uniform.charRim[1] = ( toCam[0] * sinf( rimYaw ) + toCam[1] * cosf( rimYaw ) ) * cosf( rimUp );
		uniform.charRim[2] = sinf( rimUp );
		uniform.charRim[3] = Q_max( r_charRim->value, 0.0f );
	}
}

static void vk_update_entity_matrix_constants( vkUniformEntity_t &uniform, const trRefEntity_t *refEntity ) 
{
	orientationr_t ori;

	// backend ref cant be right
	/*if ( refEntity == &tr.worldEntity ) {
		ori = backEnd.viewParms.world;
		Matrix16Identity( uniform.modelMatrix );
	}else{
		R_RotateForEntity( refEntity, &backEnd.viewParms, &ori );
		Matrix16Copy( ori.modelMatrix, uniform.modelMatrix );
	}*/

	R_RotateForEntity(refEntity, &backEnd.viewParms, &ori);
	Matrix16Copy(ori.modelMatrix, uniform.modelMatrix);
	VectorCopy(ori.viewOrigin, uniform.localViewOrigin);

	Com_Memcpy( &uniform.localViewOrigin, ori.viewOrigin, sizeof( vec3_t) );
	// r_charSelfShadow: 1 + the entity's self-shadow tile (camera selfShadow*), 0 = none
	uniform.localViewOrigin[3] = (float)refEntity->selfShadow;
}

static void vk_update_entity_constants( const trRefdef_t *refdef ) {
	uint32_t i;
	Com_Memset( vk.cmd->entity_ubo_offset, 0, sizeof(vk.cmd->entity_ubo_offset) );

	for ( i = 0; i < refdef->num_entities; i++ ) {
		trRefEntity_t *ent = &refdef->entities[i];

		R_SetupEntityLighting( refdef, ent );

		vkUniformEntity_t uniform = {};
		vk_update_entity_light_constants( uniform, ent, refdef );
		vk_update_entity_matrix_constants( uniform, ent );

		vk.cmd->entity_ubo_offset[i] = vk_append_uniform( &uniform, sizeof(uniform), vk.uniform_entity_item_size );
	}

	const trRefEntity_t *ent = &tr.worldEntity;
	vkUniformEntity_t uniform = {};
	vk_update_entity_light_constants( uniform, ent, refdef );
	vk_update_entity_matrix_constants( uniform, ent );

	vk.cmd->entity_ubo_offset[REFENTITYNUM_WORLD] = vk_append_uniform( &uniform, sizeof(uniform), vk.uniform_entity_item_size );
}

// r_g2LazyBones: only the entities with surfaces in the view's draw list are transformed and uploaded (each model once
// a frame, by the first view that draws it; RB_TransformBones' cache). Every Ghoul2 entity of the scene was before:
// off-screen players, corpses and items behind walls cost a full skeleton each (~0.03 ms for a hero model)
static void vk_update_ghoul2_constants( const trRefdef_t *refdef, const drawSurf_t *drawSurfs, int numDrawSurfs ) {
	static byte drawn[REFENTITYNUM_WORLD];
	uint32_t i;

	const qboolean lazy = ( r_g2LazyBones->integer && drawSurfs ) ? qtrue : qfalse;
	if ( lazy ) {
		Com_Memset( drawn, 0, MIN( refdef->num_entities, (uint32_t)REFENTITYNUM_WORLD ) );
		for ( int k = 0; k < numDrawSurfs; k++ ) {
			int entityNum, cubemap;
			shader_t *shader;
			R_DecomposeSort( drawSurfs[k].sort, &entityNum, &shader, &cubemap );
			if ( entityNum >= 0 && entityNum < (int)refdef->num_entities && entityNum < REFENTITYNUM_WORLD )
				drawn[entityNum] = 1;
		}
	}

	for ( i = 0; i < refdef->num_entities; i++ )
	{
		const trRefEntity_t *ent = &refdef->entities[i];
		if (ent->e.reType != RT_MODEL)
			continue;
		if ( lazy && ( i >= (uint32_t)REFENTITYNUM_WORLD || !drawn[i] ) )
			continue;

		model_t *model = R_GetModelByHandle(ent->e.hModel);
		if (!model)
			continue;

		switch (model->type)
		{
		case MOD_MDXM:
		case MOD_BAD:
		{
			// Transform Bones and upload them
			RB_TransformBones( ent, refdef );
		}
		break;

		default:
			break;
		}
	}
}

static void vk_update_fog_constants(const trRefdef_t* refdef)
{
	uint32_t i;
	size_t size;
	vkUniformFog_t uniform = {};

	uniform.num_fogs = tr.world ? ( tr.world->numfogs - 1 ) : 0;

	size = sizeof(vec4_t);

	for ( i = 0; i < MIN(uniform.num_fogs, 16); ++i )
	{
		const fog_t *fog = tr.world->fogs + i + 1;
		vkUniformFogEntry_t *fogData = uniform.fogs + i;

		VectorCopy4( fog->surface, fogData->plane );
		VectorCopy4( fog->color, fogData->color );
		fogData->depthToOpaque = sqrtf(-logf(1.0f / 255.0f)) / fog->parms.depthForOpaque;
		fogData->hasPlane = fog->hasSurface;
	}

	size += (i * sizeof(vkUniformFogEntry_t));

	vk.cmd->fogs_ubo_offset = vk_append_uniform( &uniform, size, vk.uniform_fogs_item_size );
}

static void RB_UpdateUniformConstants( const trRefdef_t *refdef, const viewParms_t *viewParms, const drawSurf_t *drawSurfs, int numDrawSurfs ) 
{
	const double tSec = vk_cputime_active() ? R_SunProfNow() : 0.0;	// r_cpuTimes
	vk_update_camera_constants( refdef, viewParms );
	vk_update_light_constants( refdef );
	vk_update_entity_constants( refdef );
	const double tG2 = tSec > 0.0 ? R_SunProfNow() : 0.0;
	vk_update_ghoul2_constants( refdef, drawSurfs, numDrawSurfs );
	if ( tSec > 0.0 )
		vk_cputime_section( VK_SEC_G2CONST, R_SunProfNow() - tG2 );
	vk_update_fog_constants( refdef );
	if ( tSec > 0.0 )
		vk_cputime_section( VK_SEC_UNIFORMS, R_SunProfNow() - tSec );
}

void RB_BindDescriptorSets( const DrawItem& drawItem ) 
{
	uint32_t offsets[VK_DESC_UNIFORM_COUNT + 1], offset_count;
	uint32_t start, end;

	start = drawItem.descriptor_set.start;
	if (start == ~0U)
		return;

	end = drawItem.descriptor_set.end;

	offset_count = 0;
	if (start == VK_DESC_UNIFORM) { // uniform offset or storage offset
		offsets[offset_count++] = drawItem.descriptor_set.offset[start];
		offsets[offset_count++] = drawItem.descriptor_set.offset[VK_DESC_UNIFORM_CAMERA_BINDING];
		offsets[offset_count++] = drawItem.descriptor_set.offset[VK_DESC_UNIFORM_LIGHT_BINDING];
		offsets[offset_count++] = drawItem.descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING];
		offsets[offset_count++] = drawItem.descriptor_set.offset[VK_DESC_UNIFORM_BONES_BINDING];
		offsets[offset_count++] = drawItem.descriptor_set.offset[VK_DESC_UNIFORM_FOGS_BINDING];
		offsets[offset_count++] = drawItem.descriptor_set.offset[VK_DESC_UNIFORM_GLOBAL_BINDING];
		
		// ~sunny, bit of a hack
		if ( drawItem.pipeline_layout == vk.pipeline_layout_surface_sprite )
		{
			offsets[offset_count] = drawItem.descriptor_set.offset[offset_count];
			offset_count++;
		}
	}

	vk_bind_main_descriptor_sets( drawItem.pipeline_layout, drawItem.descriptor_set.current, start, end, offset_count, offsets, qfalse );
}

static Pass *RB_CreatePass( Allocator& allocator, int capacity )
{
	Pass *pass = ojkAlloc<Pass>( allocator );
	if ( !pass )
		return nullptr;	// RB_AddDrawItem then draws immediately
	*pass = {};

	// the estimate (draw surfaces x stages) outgrows the per-frame memory on maps with tens of thousands of
	// visible surfaces, and a failed allocation is a null array: reserve what fits, RB_AddDrawItem flushes when full
	const size_t fit = allocator.Remaining() / sizeof( DrawItem );
	pass->maxDrawItems = (int)Q_min( (size_t)Q_max( capacity, 0 ), fit > 0 ? fit - 1 : 0 );
	pass->drawItems = pass->maxDrawItems > 0 ? ojkAllocArray<DrawItem>( allocator, pass->maxDrawItems ) : nullptr;
	if ( !pass->drawItems )
		pass->maxDrawItems = 0;
	return pass;
}

static void RB_DrawItems( int numDrawItems, const DrawItem *drawItems ) 
{
	uint32_t i;
	VkViewport viewport;
	VkRect2D scissor_rect;

	for ( i = 0; i < numDrawItems; ++i )
	{
		const DrawItem& drawItem = drawItems[i];

		RB_BindDescriptorSets( drawItem );

		vk_bind_pipeline( drawItem.pipeline );

		if( drawItem.bind_count )
			qvkCmdBindVertexBuffers( vk.cmd->command_buffer, 
			drawItem.bind_base, drawItem.bind_count, 
			drawItem.shade_buffers, drawItem.shade_offset + drawItem.bind_base );

		if ( vk.cmd->depth_range != drawItem.depthRange ) 
		{
			vk.cmd->depth_range = drawItem.depthRange;

			get_scissor_rect(&scissor_rect);

			if (memcmp(&vk.cmd->scissor_rect, &scissor_rect, sizeof(scissor_rect)) != 0) {
				qvkCmdSetScissor(vk.cmd->command_buffer, 0, 1, &scissor_rect);
				vk.cmd->scissor_rect = scissor_rect;
			}

			get_viewport(&viewport, drawItem.depthRange);
			qvkCmdSetViewport(vk.cmd->command_buffer, 0, 1, &viewport);
		}

		if ( drawItem.polygonOffset )
			qvkCmdSetDepthBias( vk.cmd->command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value );

		// push constants
		// use push constants for materials as well?
		vk_update_mvp( drawItem.mvp );
		
		// model vbo
		if ( drawItem.ibo != nullptr ) 
		{		
			if ( drawItem.indexedIndirect ) {

				// ~sunny, bit of a hack
				if ( drawItem.pipeline_layout == vk.pipeline_layout_surface_sprite )
					vk_bind_index_buffer( drawItem.ibo->buffer, 0, VK_INDEX_TYPE_UINT16 );
				else
					vk_bind_index_buffer( drawItem.ibo->buffer, 0 );

				qvkCmdDrawIndexedIndirect( 
					vk.cmd->command_buffer, 
					vk.cmd->indirect_buffer,
					drawItem.draw.params.indexedIndirect.offset,
					drawItem.draw.params.indexedIndirect.numDraws,
					sizeof(VkDrawIndexedIndirectCommand)
					);
				continue;
			}

			vk_bind_index_buffer( drawItem.ibo->buffer,  drawItem.draw.params.indexed.index_offset );
			qvkCmdDrawIndexed( vk.cmd->command_buffer, drawItem.draw.params.indexed.num_indexes, 1, 0, 0, 0 );
		}

		// world vbo
		else if ( drawItem.vbo_world_index ) 
		{
			uint32_t j;

			// can render both ibo items and soft buffer items consecutive
			if ( drawItem.draw.params.indexed.world.ibo_items_count ) {
				vk_bind_index_buffer( vk.vbo.vertex_buffer, drawItem.draw.params.indexed.world.ibo_offset );

				for ( j = 0; j < drawItem.draw.params.indexed.world.ibo_items_count; ++j )
					qvkCmdDrawIndexed( vk.cmd->command_buffer,  drawItem.draw.params.indexed.world.ibo_items[j].indexCount, 1, drawItem.draw.params.indexed.world.ibo_items[j].firstIndex, 0, 0 );
			}

			if ( drawItem.draw.params.indexed.world.soft_buffer_indexes ) {
				vk_bind_index_buffer( vk.cmd->vertex_buffer, drawItem.draw.params.indexed.world.soft_buffer_offset );
				qvkCmdDrawIndexed( vk.cmd->command_buffer, drawItem.draw.params.indexed.world.soft_buffer_indexes, 1, 0, 0, 0 );	
			}
		}

		// draw from vertex buffer
		else 
		{
			if ( drawItem.indexed ) {
				vk_bind_index_buffer( vk.cmd->vertex_buffer, drawItem.draw.params.indexed.index_offset );
				qvkCmdDrawIndexed( vk.cmd->command_buffer, drawItem.draw.params.indexed.num_indexes, 1, 0, 0, 0 ) ;
				continue;
			}

			qvkCmdDraw( vk.cmd->command_buffer, drawItem.draw.params.arrays.num_vertexes, 1, 0, 0 );
		}
	}
}

void RB_AddDrawItem( Pass *pass, const DrawItem& drawItem )
{
	// there will be no pass if we are drawing a 2D object, 
	// flare, shadow or debug item.
	if ( pass && pass->maxDrawItems > 0 )
	{
		if ( pass->numDrawItems >= pass->maxDrawItems )
		{
			// full: draw what is queued, in order, and reuse the storage
			RB_DrawItems( pass->numDrawItems, pass->drawItems );
			pass->numDrawItems = 0;
		}

		pass->drawItems[pass->numDrawItems++] = drawItem;
		return;
	}

	RB_DrawItems( 1, &drawItem ); // draw immidiate	
}

static void RB_SubmitRenderPass( Pass& renderPass, Allocator& allocator )
{
	RB_DrawItems ( renderPass.numDrawItems, renderPass.drawItems );
}

/*
==================
RB_RenderDrawSurfList
==================
*/
// r_cpuTimes: the next walk's kind when its render pass index does not tell it (character shadow tiles)
static int rb_walkSlot = -1;

static int RB_WalkSlot( void )
{
	if ( rb_walkSlot >= 0 )
		return rb_walkSlot;
	switch ( vk.renderPassIndex ) {
	case RENDER_PASS_MAIN:			return backEnd.isGlowPass ? VK_WALK_GLOW : backEnd.refractionFill ? VK_WALK_REFRACTION : VK_WALK_LIT;
	case RENDER_PASS_DGLOW:			return VK_WALK_GLOW;
	case RENDER_PASS_DEPTH_PREPASS:	return VK_WALK_PREPASS;
	case RENDER_PASS_SUN_SHADOW:	return VK_WALK_CASCADE;
	case RENDER_PASS_CHAR_OCCLUDER:	return VK_WALK_CHAROCC;
	case RENDER_PASS_CHAR_HAIRMAP:	return VK_WALK_HAIRMAP;
	case RENDER_PASS_REFRACTION:	return VK_WALK_REFRACTION;
	case RENDER_PASS_POST_1X:		return VK_WALK_POST1X;
	default:						return VK_WALK_OTHER;
	}
}

void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int numDrawSurfs ) 
{
	void *allocMark = nullptr;
	const double tWalk = vk_cputime_active() ? R_SunProfNow() : 0.0;	// r_cpuTimes
	double tIssue = 0.0;


	const int estimatedNumShaderStages = 4;

	allocMark = backEndData->perFrameMemory->Mark();
	backEndData->currentPass = RB_CreatePass( *backEndData->perFrameMemory, 
		numDrawSurfs * estimatedNumShaderStages );

	// save original time for entity shader offsets
	float originalTime = backEnd.refdef.floatTime;
	
	backEnd.currentEntity	= &tr.worldEntity;
	backEnd.pc.c_surfaces	+= numDrawSurfs;

	RB_SubmitDrawSurfs( drawSurfs, numDrawSurfs, originalTime );

	if ( tWalk > 0.0 )
		tIssue = R_SunProfNow();
	if ( backEndData->currentPass )
		RB_SubmitRenderPass(
			*backEndData->currentPass,
			*backEndData->perFrameMemory );
	if ( tWalk > 0.0 ) {
		const double tEnd = R_SunProfNow();
		vk_cputime_walk( RB_WalkSlot(), tIssue - tWalk, tEnd - tIssue, numDrawSurfs );
	}

	backEndData->perFrameMemory->ResetTo( allocMark );
	backEndData->currentPass = nullptr;

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );

	vk_set_depthrange( DEPTH_RANGE_NORMAL );
}

#ifdef USE_PMLIGHT
/*
=================
RB_BeginDrawingLitView
=================
*/
static void RB_BeginDrawingLitSurfs( void )
{
	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	glState.faceCulling = -1;		// force face culling to set next time
}

/*
==================
RB_RenderLitSurfList
==================
*/
static void RB_RenderLitSurfList( dlight_t *dl ) 
{
	// keep using fast path sort compare on litSurfs for now, ignore fog
	const litSurf_t *litSurf;
	float			originalTime;

	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	backEnd.currentEntity	= &tr.worldEntity;

	tess.dlightUpdateParams = qtrue;
	
	shader_t *shader;
	int entityNum, fogNum;

	shader_t	*oldShader = nullptr;
	int			oldEntityNum = -1;
	uint32_t	oldSort = MAX_UINT;
	//float		oldShaderSort = -1;
	//int		oldFogNum = -1;	

	for ( litSurf = dl->head; litSurf; litSurf = litSurf->next ) 
	{
		if ( litSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[*litSurf->surface](litSurf->surface);
			continue;
		}

		R_DecomposeLitSort( litSurf->sort, &entityNum, &shader );
		fogNum = litSurf->fogIndex;

		if ( vk.renderPassIndex == RENDER_PASS_SCREENMAP && entityNum != REFENTITYNUM_WORLD && backEnd.refdef.entities[entityNum].e.renderfx & RF_DEPTHHACK )
			continue;

		// anything BEFORE opaque is sky/portal, anything AFTER it should never have been added
		//assert( shader->sort == SS_OPAQUE );
		// !!! but MIRRORS can trip that assert, so just do this for now
		//if ( shader->sort < SS_OPAQUE )
		//	continue;

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( ((oldSort ^ litSurf->sort) & ~QSORT_ENTITYNUM_MASK) || !tess.entityMergable ) 
		{
			if ( oldShader != NULL )
				RB_EndSurface();

			RB_BeginSurface( shader, fogNum, 0 );
			oldShader = shader;
		}

		oldSort = litSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) 
		{
			RB_PrepareForEntity( entityNum, originalTime );

			// set up the dynamic lighting
			R_TransformDlights( 1, dl, &backEnd.ori );
			tess.dlightUpdateParams = qtrue;

			Com_Memcpy( vk_world.modelview_transform, backEnd.ori.modelViewMatrix, 64 );
			vk_update_mvp( NULL );

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[*litSurf->surface](litSurf->surface);
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL )
		RB_EndSurface();

	backEnd.refdef.floatTime = originalTime;

	// go back to the world modelview matrix
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );

	vk_set_depthrange( DEPTH_RANGE_NORMAL );
}
#endif // USE_PMLIGHT

/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw ( int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty )
{
	int			i, j;
	int			start, end;

	if (!tr.registered) {
		return;
	}

	start = 0;
	if (r_speeds->integer) {
		start = ri.Milliseconds() * ri.Cvar_VariableValue("timescale");
	}

	// make sure rows and cols are powers of 2
	for (i = 0; (1 << i) < cols; i++)
	{
		;
	}
	for (j = 0; (1 << j) < rows; j++)
	{
		;
	}

	// xenjk (rd-vanilla parity): any size is fine (Theora videos are not powers of 2); the image is created at the
	// frame's own size (IMGFLAG_NOSCALE)
	if ((1 << i) != cols || (1 << j) != rows) {
		ri.Printf(PRINT_DEVELOPER, "Draw_StretchRaw: NPOT texture %i by %i\n", cols, rows);
	}

	RE_UploadCinematic( cols, rows, (byte*)data, client, dirty ); 

	if (r_speeds->integer) {
		end = ri.Milliseconds() * ri.Cvar_VariableValue("timescale");
		ri.Printf(PRINT_ALL, "RE_UploadCinematic( %i, %i ): %i msec\n", cols, rows, end - start);
	}

	tr.cinematicShader->stages[0]->bundle[0].image[0] = tr.scratchImage[client];
	RE_StretchPic(x, y, w, h, 0.5f / cols, 0.5f / rows, 1.0f - 0.5f / cols, 1.0f - 0.5 / rows, tr.cinematicShader->index);
}

/*
=============
RB_SetColor

=============
*/
const void	*RB_SetColor( const void *data ) {
	const setColorCommand_t	*cmd;

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0] * 255;
	backEnd.color2D[1] = cmd->color[1] * 255;
	backEnd.color2D[2] = cmd->color[2] * 255;
	backEnd.color2D[3] = cmd->color[3] * 255;

	return (const void *)(cmd + 1);
}

/*
=============
RB_StretchPic
=============
*/
const void *RB_StretchPic ( const void *data ) {
	const stretchPicCommand_t	*cmd;
	shader_t *shader;

	cmd = (const stretchPicCommand_t *)data;
	
	shader = cmd->shader;
	if ( shader != tess.shader ) {
		RB_EndSurface();
		backEnd.currentEntity = &backEnd.entity2D;
		vk_set_2d(); // set correct shader time before RB_BeginSurface() on 3D->2D transition
		RB_BeginSurface( shader, 0, 0 );
	}

#ifdef USE_VBO
	VBO_UnBind();
#endif

	vk_set_2d();

	if ( vk.bloomActive ) {
		vk_bloom();
	}

	RB_AddQuadStamp2( cmd->x, cmd->y, cmd->w, cmd->h, cmd->s1, cmd->t1, 
		cmd->s2, cmd->t2, backEnd.color2D );

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawRotatePic
=============
*/
const void *RB_RotatePic ( const void *data )
{
	const rotatePicCommand_t	*cmd;
	image_t *image;
	shader_t *shader;

	cmd = (const rotatePicCommand_t *)data;

	shader = cmd->shader;
	image = shader->stages[0]->bundle[0].image[0];

	if ( image ) {
		shader = cmd->shader;
		if ( shader != tess.shader ) {
			RB_EndSurface();
			backEnd.currentEntity = &backEnd.entity2D;
			vk_set_2d(); // set correct shader time before RB_BeginSurface() on 3D->2D transition
			RB_BeginSurface( shader, 0, 0 );
		}

		vk_set_2d();

		RB_CHECKOVERFLOW( 4, 6 );
		int numVerts = tess.numVertexes;
		int numIndexes = tess.numIndexes;

		float angle = DEG2RAD( cmd-> a );
		float s = sinf( angle );
		float c = cosf( angle );

		matrix3_t m = {
			{ c, s, 0.0f },
			{ -s, c, 0.0f },
			{ cmd->x + cmd->w, cmd->y, 1.0f }
		};

		tess.numVertexes += 4;
		tess.numIndexes += 6;

		tess.indexes[ numIndexes + 0 ] = numVerts + 3;
		tess.indexes[ numIndexes + 1 ] = numVerts + 0;
		tess.indexes[ numIndexes + 2 ] = numVerts + 2;
		tess.indexes[ numIndexes + 3 ] = numVerts + 2;
		tess.indexes[ numIndexes + 4 ] = numVerts + 0;
		tess.indexes[ numIndexes + 5 ] = numVerts + 1;

		byteAlias_t *baDest = NULL, *baSource = (byteAlias_t *)&backEnd.color2D;
		baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 0]; baDest->ui = baSource->ui;
		baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 1]; baDest->ui = baSource->ui;
		baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 2]; baDest->ui = baSource->ui;
		baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 3]; baDest->ui = baSource->ui;

		tess.xyz[ numVerts + 0 ][0] = m[0][0] * (-cmd->w) + m[2][0];
		tess.xyz[ numVerts + 0 ][1] = m[0][1] * (-cmd->w) + m[2][1];
		tess.xyz[ numVerts + 0 ][2] = 0;

		tess.xyz[ numVerts + 1 ][0] = m[2][0];
		tess.xyz[ numVerts + 1 ][1] = m[2][1];
		tess.xyz[ numVerts + 1 ][2] = 0;

		tess.xyz[ numVerts + 2 ][0] = m[1][0] * (cmd->h) + m[2][0];
		tess.xyz[ numVerts + 2 ][1] = m[1][1] * (cmd->h) + m[2][1];
		tess.xyz[ numVerts + 2 ][2] = 0;

		tess.xyz[ numVerts + 3 ][0] = m[0][0] * (-cmd->w) + m[1][0] * (cmd->h) + m[2][0];
		tess.xyz[ numVerts + 3 ][1] = m[0][1] * (-cmd->w) + m[1][1] * (cmd->h) + m[2][1];
		tess.xyz[ numVerts + 3 ][2] = 0;

		tess.texCoords[0][ numVerts + 0 ][0] = cmd->s1;
		tess.texCoords[0][ numVerts + 0 ][1] = cmd->t1;
		tess.texCoords[0][ numVerts + 1 ][0] = cmd->s2;
		tess.texCoords[0][ numVerts + 1 ][1] = cmd->t1;
		tess.texCoords[0][ numVerts + 2 ][0] = cmd->s2;
		tess.texCoords[0][ numVerts + 2 ][1] = cmd->t2;
		tess.texCoords[0][ numVerts + 3 ][0] = cmd->s1;
		tess.texCoords[0][ numVerts + 3 ][1] = cmd->t2;

		return (const void *)(cmd + 1);

	}

	return (const void *)(cmd + 1);
}

/*
=============
RB_DrawRotatePic2
=============
*/
const void *RB_RotatePic2 ( const void *data )
{
	const rotatePicCommand_t	*cmd;
	image_t *image;
	shader_t *shader;

	cmd = (const rotatePicCommand_t *)data;

	shader = cmd->shader;

	if ( shader->numUnfoggedPasses )
	{
		image = shader->stages[0]->bundle[0].image[0];

		if ( image )
		{
			shader = cmd->shader;
			if ( shader != tess.shader ) {
				RB_EndSurface();
				backEnd.currentEntity = &backEnd.entity2D;
				vk_set_2d(); // set correct shader time before RB_BeginSurface() on 3D->2D transition
				RB_BeginSurface( shader, 0, 0 );
			}

			vk_set_2d();

			RB_CHECKOVERFLOW( 4, 6 );
			int numVerts = tess.numVertexes;
			int numIndexes = tess.numIndexes;

			float angle = DEG2RAD( cmd-> a );
			float s = sinf( angle );
			float c = cosf( angle );

			matrix3_t m = {
				{ c, s, 0.0f },
				{ -s, c, 0.0f },
				{ cmd->x, cmd->y, 1.0f }
			};

			tess.numVertexes += 4;
			tess.numIndexes += 6;

			tess.indexes[ numIndexes + 0 ] = numVerts + 3;
			tess.indexes[ numIndexes + 1 ] = numVerts + 0;
			tess.indexes[ numIndexes + 2 ] = numVerts + 2;
			tess.indexes[ numIndexes + 3 ] = numVerts + 2;
			tess.indexes[ numIndexes + 4 ] = numVerts + 0;
			tess.indexes[ numIndexes + 5 ] = numVerts + 1;

			byteAlias_t *baDest = NULL, *baSource = (byteAlias_t *)&backEnd.color2D;
			baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 0]; baDest->ui = baSource->ui;
			baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 1]; baDest->ui = baSource->ui;
			baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 2]; baDest->ui = baSource->ui;
			baDest = (byteAlias_t *)&tess.vertexColors[numVerts + 3]; baDest->ui = baSource->ui;

			tess.xyz[ numVerts + 0 ][0] = m[0][0] * (-cmd->w * 0.5f) + m[1][0] * (-cmd->h * 0.5f) + m[2][0];
			tess.xyz[ numVerts + 0 ][1] = m[0][1] * (-cmd->w * 0.5f) + m[1][1] * (-cmd->h * 0.5f) + m[2][1];
			tess.xyz[ numVerts + 0 ][2] = 0;

			tess.xyz[ numVerts + 1 ][0] = m[0][0] * (cmd->w * 0.5f) + m[1][0] * (-cmd->h * 0.5f) + m[2][0];
			tess.xyz[ numVerts + 1 ][1] = m[0][1] * (cmd->w * 0.5f) + m[1][1] * (-cmd->h * 0.5f) + m[2][1];
			tess.xyz[ numVerts + 1 ][2] = 0;

			tess.xyz[ numVerts + 2 ][0] = m[0][0] * (cmd->w * 0.5f) + m[1][0] * (cmd->h * 0.5f) + m[2][0];
			tess.xyz[ numVerts + 2 ][1] = m[0][1] * (cmd->w * 0.5f) + m[1][1] * (cmd->h * 0.5f) + m[2][1];
			tess.xyz[ numVerts + 2 ][2] = 0;

			tess.xyz[ numVerts + 3 ][0] = m[0][0] * (-cmd->w * 0.5f) + m[1][0] * (cmd->h * 0.5f) + m[2][0];
			tess.xyz[ numVerts + 3 ][1] = m[0][1] * (-cmd->w * 0.5f) + m[1][1] * (cmd->h * 0.5f) + m[2][1];
			tess.xyz[ numVerts + 3 ][2] = 0;

			tess.texCoords[0][ numVerts + 0 ][0] = cmd->s1;
			tess.texCoords[0][ numVerts + 0 ][1] = cmd->t1;
			tess.texCoords[0][ numVerts + 1 ][0] = cmd->s2;
			tess.texCoords[0][ numVerts + 1 ][1] = cmd->t1;
			tess.texCoords[0][ numVerts + 2 ][0] = cmd->s2;
			tess.texCoords[0][ numVerts + 2 ][1] = cmd->t2;
			tess.texCoords[0][ numVerts + 3 ][0] = cmd->s1;
			tess.texCoords[0][ numVerts + 3 ][1] = cmd->t2;

			return (const void *)(cmd + 1);
		}
	}

	return (const void *)(cmd + 1);
}

#ifdef USE_PMLIGHT
static void RB_LightingPass( void )
{
	dlight_t* dl;
	int	i;

#ifdef USE_VBO
	//VBO_Flush();
	//tess.allowVBO = qfalse; // for now
#endif

	tess.dlightPass = qtrue;

	for (i = 0; i < backEnd.viewParms.num_dlights; i++)
	{
		dl = &backEnd.viewParms.dlights[i];
		if (dl->head)
		{
			tess.light = dl;
			RB_RenderLitSurfList(dl);
		}
	}

	tess.dlightPass = qfalse;

	backEnd.viewParms.num_dlights = 0;
}
#endif

#ifdef USE_VK_PBR
/*
=============
vk_one_resolve_pass

With MSAA, the main world view ends its MSAA pass once (one resolve). SSAO and glow are extracted from it, then
composited onto the resolved image at 1x in the post 1x pass, where the rest of the frame (bloom, HUD, later
non-world views) is drawn at 1x. Before this each composite reloaded and re-resolved the MSAA image.
=============
*/
static qboolean vk_is_main_world_view( void )
{
	return ( tr.world && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && !( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL )
		&& backEnd.viewParms.portalView == PV_NONE ) ? qtrue : qfalse;
}

static void vk_one_resolve_pass( const drawSurfsCommand_t *cmd )
{
	// refraction draws into the MSAA image, so it comes before the resolve
	if ( backEnd.hasRefractionSurfaces ) {
		vk_end_render_pass();
		vk_refraction_extract();
		backEnd.refractionFill = qtrue;
		vk_begin_post_refraction_extract_render_pass();
		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		backEnd.refractionFill = qfalse;
	}

	vk_end_render_pass();	// the single MSAA resolve

#ifdef USE_VK_SSAO
	// r_ssaoWorld 0: no world SSAO; r_ssaoReuseExtract: the character pass's extract (after the prepass) stands in
	const qboolean worldAO = ( vk.depth.extract.enabled && r_ssaoWorld->integer ) ? qtrue : qfalse;
	if ( worldAO ) {
		if ( !( r_ssaoReuseExtract->integer && backEnd.charOccExtracted ) )
			vk_depth_extract( glConfig.vidWidth, glConfig.vidHeight );
		vk_ssao_extract_blur();
	}
#else
	if ( vk.depth.extract.enabled )
		vk_depth_extract( glConfig.vidWidth, glConfig.vidHeight );
#endif

	// r_smoke reads the depth extract; without world AO or the character pass nothing took one this frame
#ifdef USE_VK_SSAO
	if ( vk_smoke_wanted() && vk.depth.extract.enabled && !worldAO && !backEnd.charOccExtracted )
		vk_depth_extract( glConfig.vidWidth, glConfig.vidHeight );
	// render scene reflections: the resolved world and its depth, kept for the next frame
	vk_scene_capture( ( worldAO || backEnd.charOccExtracted || vk_smoke_wanted() ) ? qtrue : qfalse );
#else
	vk_scene_capture( vk.depth.extract.enabled );
#endif

	const qboolean glow = ( vk.dglowActive && backEnd.hasGlowSurfaces ) ? qtrue : qfalse;
	if ( glow ) {
		backEnd.isGlowPass = qtrue;
		vk_begin_dglow_extract_render_pass();
		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		vk_scene_draw( SCENE_PASS_GLOW );
		vk_dglow_blur_passes();
		backEnd.isGlowPass = qfalse;
	}

	vk_begin_post1x_render_pass();
#ifdef USE_VK_SSAO
	if ( worldAO )
		vk_ssao_blend();
#endif
	if ( glow )
		vk_dglow_blend_1x();
	vk_smoke_draw();	// r_smoke; also notes the view for the `smoke` command
	vk_scene_grade();	// a render scene's colour grade over the main view (re-begins this pass)

	vk_restore_draw_state( qfalse );

#ifdef USE_VK_IMGUI
	vk_set_viewport_scissor( glConfig.vidWidth, glConfig.vidHeight );
#endif
}

static void vk_postfx_pass(void)
{
	if ( !vk.depth.extract.enabled )
		return;

	if ( !tr.world )
		return;

	// skip non-world, skyportals and mirrors etc
	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) || 
		 ( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) || 
		 backEnd.viewParms.portalView != PV_NONE )
		return;

	// without MSAA the render scene's reflections capture here (vk_one_resolve_pass with it)
	const qboolean capture = vk_scene_capture_wanted();
#ifdef USE_VK_SSAO
	const qboolean ao = r_ssaoWorld->integer ? qtrue : qfalse;
#else
	const qboolean ao = qtrue;
#endif
	if ( !ao && !capture )
		return;

	vk_end_render_pass();

	// ~sunny, transition to z-prepass later
	vk_depth_extract( glConfig.vidWidth, glConfig.vidHeight );

	// extract
#ifdef USE_VK_SSAO
	if ( ao )
		vk_ssao_extract_blur();
#endif
	if ( capture )
		vk_scene_capture( qtrue );

	// blend
	vk_begin_post_blend_render_pass( vk.render_pass.postfx.blend.handle, qfalse );
#ifdef USE_VK_SSAO
	if ( ao )
		vk_ssao_blend();
#endif

#ifdef USE_VK_IMGUI
	vk_set_viewport_scissor( glConfig.vidWidth, glConfig.vidHeight );
#endif
}
#endif

/*
=============
RB_RenderSunShadowCascades

Called by vk_begin_frame before the main (or screen-map) render pass begins: draws this frame's sun cascade
views into their depth layers. RB_DrawSurfs skips these commands later.
=============
*/
void RB_RenderSunShadowCascades( void )
{
	const double t0 = r_sunShadowInfo->integer >= 2 ? R_SunProfNow() : 0.0;
	int i;

	for ( i = 0; i < backEndData->numSunCascadeCmds; i++ )
	{
		const drawSurfsCommand_t *cmd = backEndData->sunCascadeCmds[i];
		const int layer = cmd->viewParms.sunCascade - 1;

		if ( layer < 0 || layer > 2 )
			continue;

		backEnd.refdef = cmd->refdef;
		backEnd.viewParms = cmd->viewParms;
		backEnd.hasGlowSurfaces = qfalse;
		backEnd.isGlowPass = qfalse;
		backEnd.hasRefractionSurfaces = qfalse;
		backEnd.projection2D = qfalse;

		RB_UpdateUniformConstants( &backEnd.refdef, &backEnd.viewParms, cmd->drawSurfs, cmd->numDrawSurfs );

		if ( vk_sun_shadow_kept( layer ) && r_sunShadowDebug->integer != 4 ) {
			RB_RenderFarCascade( cmd, layer );
			continue;
		}
		if ( layer >= 1 )
			vk.sunShadow.cacheValid[layer == 2 ? 0 : 1] = qfalse;	// drawn in full: the cache may no longer match a later frame's
		vk_begin_sun_shadow_render_pass( layer );
		vk.cmd->depth_range = DEPTH_RANGE_COUNT;	// force viewport/scissor for the cascade size
		vk.cmd->last_pipeline = VK_NULL_HANDLE;
		Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );

		if ( r_sunShadowDebug->integer != 4 ) {	// 4: cleared cascades only (tests the lookup)
			RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
			vk_scene_draw( SCENE_PASS_SHADOW );
		}

		vk_end_render_pass();
	}

	backEndData->numSunCascadeCmds = 0;

	// character shadows: every tile in one pass over the atlas layer (cleared to the far plane: lit)
	if ( backEndData->numCharShadowCmds )
	{
		backEnd.viewParms = backEndData->charShadowCmds[0]->viewParms;
		vk_begin_sun_shadow_render_pass( 3 );
		for ( i = 0; i < backEndData->numCharShadowCmds; i++ )
		{
			const drawSurfsCommand_t *cmd = backEndData->charShadowCmds[i];

			backEnd.refdef = cmd->refdef;
			backEnd.viewParms = cmd->viewParms;
			backEnd.hasGlowSurfaces = qfalse;
			backEnd.isGlowPass = qfalse;
			backEnd.hasRefractionSurfaces = qfalse;
			backEnd.projection2D = qfalse;

			RB_UpdateUniformConstants( &backEnd.refdef, &backEnd.viewParms, cmd->drawSurfs, cmd->numDrawSurfs );

			vk.cmd->depth_range = DEPTH_RANGE_COUNT;	// force the tile's viewport/scissor
			vk.cmd->last_pipeline = VK_NULL_HANDLE;
			Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );

			// a later scene's tile over a rectangle an earlier scene drew this frame (a menu stage's self-shadow
			// over the world's tile 0): cleared first, so the later figure's depth replaces the earlier one's
			// instead of merging with it under the depth test
			for ( int j = 0; j < i; j++ ) {
				const viewParms_t *a = &backEndData->charShadowCmds[j]->viewParms, *b = &cmd->viewParms;
				if ( a->viewportX < b->viewportX + b->viewportWidth && b->viewportX < a->viewportX + a->viewportWidth
					&& a->viewportY < b->viewportY + b->viewportHeight && b->viewportY < a->viewportY + a->viewportHeight ) {
					VkClearAttachment clear;
					VkClearRect rect;
					Com_Memset( &clear, 0, sizeof( clear ) );
					clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
					clear.clearValue.depthStencil.depth = 1.0f;	// as the pass clears the layer
					Com_Memset( &rect, 0, sizeof( rect ) );
					get_scissor_rect( &rect.rect );
					rect.layerCount = 1;
					qvkCmdClearAttachments( vk.cmd->command_buffer, 1, &clear, 1, &rect );
					break;
				}
			}

			rb_walkSlot = VK_WALK_CHARTILE;
			RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
			rb_walkSlot = -1;
		}
		vk_end_render_pass();

		// r_charSelfShadowHairMap: the first self-shadow tile's hair once more, with its view over the whole map, after
		// the tile's depth (which left that hair out) so the shader keeps the strands in front of the opaque body. The
		// frame's last such tile: a menu stage's, drawn over the world's (R_SetupCharSelfShadows)
		for ( i = backEndData->numCharShadowCmds - 1; vk.hairMap.descriptor && i >= 0; i-- )
		{
			const drawSurfsCommand_t *cmd = backEndData->charShadowCmds[i];

			if ( cmd->viewParms.selfShadowSlot != 1 )
				continue;
			backEnd.refdef = cmd->refdef;
			backEnd.viewParms = cmd->viewParms;
			backEnd.viewParms.viewportX = backEnd.viewParms.viewportY = 0;
			backEnd.viewParms.viewportWidth = backEnd.viewParms.viewportHeight = vk.hairMap.size;
			backEnd.viewParms.scissorX = backEnd.viewParms.scissorY = 0;
			backEnd.viewParms.scissorWidth = backEnd.viewParms.scissorHeight = vk.hairMap.size;
			RB_UpdateUniformConstants( &backEnd.refdef, &backEnd.viewParms, cmd->drawSurfs, cmd->numDrawSurfs );

			vk_begin_hair_map_render_pass();
			backEnd.isHairMapPass = qtrue;
			vk.cmd->depth_range = DEPTH_RANGE_COUNT;
			vk.cmd->last_pipeline = VK_NULL_HANDLE;
			Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );
			RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
			backEnd.isHairMapPass = qfalse;
			vk_end_render_pass();
			break;
		}
		backEndData->numCharShadowCmds = 0;
	}

	if ( t0 > 0.0 )
		R_SunProfAdd( SUNPROF_CASCADES_BE, R_SunProfNow() - t0 );
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.renderPassIndex = RENDER_PASS_MAIN;
}

/*
================
RB_RenderWaterMirror

vk_begin_frame, before any pass of the frame (after the shadow tiles the characters' shading reads)
================
*/
void RB_RenderWaterMirror( void )
{
	const drawSurfsCommand_t *cmd = backEndData->waterMirrorCmd;

	backEndData->waterMirrorCmd = NULL;
	if ( !cmd || !vk.waterMirror.framebuffer )
		return;

	const qboolean timed = vk_gputime_section_begin( "water chars" );
	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;
	backEnd.hasGlowSurfaces = qfalse;
	backEnd.isGlowPass = qfalse;
	backEnd.hasRefractionSurfaces = qfalse;
	backEnd.projection2D = qfalse;
	RB_UpdateUniformConstants( &backEnd.refdef, &backEnd.viewParms, cmd->drawSurfs, cmd->numDrawSurfs );

	vk_begin_water_mirror_render_pass();
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;	// force the target's viewport/scissor
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	Com_Memcpy( vk_world.modelview_transform, backEnd.viewParms.world.modelViewMatrix, 64 );
	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
	vk_end_render_pass();

	vk.cmd->depth_range = DEPTH_RANGE_COUNT;
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	vk.renderPassIndex = RENDER_PASS_MAIN;
	vk_gputime_section_end( timed );
}

/*
=============
RB_DrawSurfs

=============
*/
const void	*RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t	*cmd;

	RB_EndSurface(); // finish any 2D drawing if needed

	cmd = (const drawSurfsCommand_t *)data;

	// sun cascades, character shadow tiles and the characters mirrored in the water were drawn before the main render pass began
	if ( R_IS_SHADOW_VIEW( cmd->viewParms ) || cmd->viewParms.waterMirror )
		return (const void*)(cmd + 1);

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	backEnd.hasGlowSurfaces = qfalse;
	backEnd.isGlowPass = qfalse;

	backEnd.hasRefractionSurfaces = qfalse;

	// r_charAO / r_charContactShadows: the main world view computes them from its depth prepass (RB_CharOcclusion);
	// so does a stage (menus) that opens the frame's main pass (r_stageCovers), with the values used in play
	backEnd.charOcclusion = ( ( R_CHAR_AO( backEnd.refdef.rdflags ) > 0.0f || R_CHAR_CONTACT( backEnd.refdef.rdflags ) > 0.0f )
		&& vk.ssaoActive && vk.depth.extract.enabled && vk.render_pass.main_reload.handle && r_depthPrepass->integer
		&& vk.renderPassIndex == RENDER_PASS_MAIN && vk.mainPassKeeps	// else the reload loads undefined MSAA colour
		&& ( ( tr.world && !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) || R_IS_STAGE( backEnd.refdef.rdflags ) )
		&& backEnd.viewParms.portalView == PV_NONE
#ifdef VK_CUBEMAP
		&& backEnd.viewParms.targetCube == nullptr
#endif
		) ? qtrue : qfalse;
	RB_SetupCharOccLights();
	backEnd.charOccMark = ( backEnd.charOcclusion && r_charOccMask->integer && vk.ssaoCharMask && vk.depth.stencil_view ) ? qtrue : qfalse;
	backEnd.charOccExtracted = qfalse;

#ifdef USE_VBO
	VBO_UnBind();
#endif

	RB_UpdateUniformConstants( &backEnd.refdef, &backEnd.viewParms, cmd->drawSurfs, cmd->numDrawSurfs );

	// clear the z buffer, set the modelview, etc
	{
		const double tSec = vk_cputime_active() ? R_SunProfNow() : 0.0;
		RB_BeginDrawingView();
		if ( tSec > 0.0 )
			vk_cputime_section( VK_SEC_BEGINVIEW, R_SunProfNow() - tSec );
	}

#ifdef VK_CUBEMAP
	if ( backEnd.viewParms.targetCube != nullptr ) 
	{
		vk_end_render_pass();
		vk_begin_cubemap_render_pass();

		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );

		backEnd.doneSurfaces = qtrue; // for bloom

		return (const void*)(cmd + 1);
	}
#endif

	// r_depthPrepass: the opaque world's depth first (same vertex shaders, no fragment stage, no colour writes),
	// so the lit pass below shades only the visible fragment of each pixel
	if ( r_depthPrepass->integer && vk.renderPassIndex == RENDER_PASS_MAIN && ( !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) || backEnd.charOcclusion ) ) {
		backEnd.isDepthPrepass = qtrue;
		vk.renderPassIndex = RENDER_PASS_DEPTH_PREPASS;
		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		vk_scene_draw( SCENE_PASS_DEPTH );
		vk.renderPassIndex = RENDER_PASS_MAIN;
		backEnd.isDepthPrepass = qfalse;
		vk.cmd->last_pipeline = VK_NULL_HANDLE;
		// render scene occlusion (r_sceneOcclusion): the pass split for its second phase, which draws the depth of what it
		// finds visible; the scene's mesh decals (r_sceneDecals) go into their DBuffer in that split, or in one of their own
		// (and, without character occlusion, the hair's depth for the water's bed in that split: RB_WaterHairBed)
		backEnd.hairBedSurfs = backEnd.charOcclusion ? NULL : cmd->drawSurfs;
		backEnd.hairBedNum = cmd->numDrawSurfs;
		if ( vk_scene_occlusion() || vk_scene_decals() )
			vk.cmd->last_pipeline = VK_NULL_HANDLE;
		backEnd.hairBedSurfs = NULL;

		if ( backEnd.charOcclusion )
		{
			const double tSec = vk_cputime_active() ? R_SunProfNow() : 0.0;
			RB_CharOcclusion( cmd->drawSurfs, cmd->numDrawSurfs );
			if ( tSec > 0.0 )
				vk_cputime_section( VK_SEC_CHAROCC, R_SunProfNow() - tSec );
		}
	}

	vk_scene_draw( SCENE_PASS_OPAQUE );		// render scene: before the BSP's surfaces (sky, blends)
	RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
	vk_scene_draw( SCENE_PASS_BLEND );

#ifdef USE_VBO
	VBO_UnBind();
#endif

	{
		const double tSec = vk_cputime_active() ? R_SunProfNow() : 0.0;
		RB_DrawSun( 0.1f, tr.sunShader );
		RB_ShadowFinish();
		RB_RenderFlares();
		if ( tSec > 0.0 )
			vk_cputime_section( VK_SEC_SUNFLARES, R_SunProfNow() - tSec );
	}

#ifdef USE_PMLIGHT
	// revisit this when implementing gpu dlights
	if ( /*vk.useFastLight &&*/ backEnd.refdef.numLitSurfs ) {
		{
			const double tSec = vk_cputime_active() ? R_SunProfNow() : 0.0;
			RB_BeginDrawingLitSurfs();
			RB_LightingPass();
			if ( tSec > 0.0 )
				vk_cputime_section( VK_SEC_DLIGHTS, R_SunProfNow() - tSec );
		}
	}
#endif

#ifdef USE_VK_LIGHTGRID
	if ( r_showLightgrid->integer )
		vk_render_lightgrid();
#endif

	// draw main system development information (surface outlines, etc)
	R_DebugGraphics();

	if ( cmd->refdef.switchRenderPass ) {
		vk_end_render_pass();
		vk_begin_main_render_pass();
		backEnd.screenMapDone = qtrue;
	}

#ifdef USE_VK_PBR
	if ( vk.renderPassIndex == RENDER_PASS_POST_1X ) {
		// after the frame's resolve (HUD models, later views): stay at 1x, no MSAA-based post passes
		backEnd.doneSurfaces = qtrue;
		return (const void*)(cmd + 1);
	}

	if ( vk.msaaActive && vk_is_main_world_view() ) {
		{
			const double tSec = vk_cputime_active() ? R_SunProfNow() : 0.0;
			vk_one_resolve_pass( cmd );
			if ( tSec > 0.0 )
				vk_cputime_section( VK_SEC_RESOLVE, R_SunProfNow() - tSec );
		}
		backEnd.doneSurfaces = qtrue; // for bloom
		return (const void*)(cmd + 1);
	}

	vk_postfx_pass();
#endif

	// refraction / distortion pass
	if ( backEnd.hasRefractionSurfaces ) {
		vk_end_render_pass();
	
		// extract/copy offscreen color attachment to make it usable as input
		vk_refraction_extract();

		backEnd.refractionFill = qtrue;	
		vk_begin_post_refraction_extract_render_pass();

		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		backEnd.refractionFill = qfalse;
	}

	// checked in previous RB_RenderDrawSurfList() if there is at least one glowing surface
	if ( vk.dglowActive && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) && backEnd.hasGlowSurfaces )
	{
		vk_end_render_pass();
		
		backEnd.isGlowPass = qtrue;
		vk_begin_dglow_extract_render_pass();

		RB_RenderDrawSurfList( cmd->drawSurfs, cmd->numDrawSurfs );
		vk_scene_draw( SCENE_PASS_GLOW );
		
		vk_begin_dglow_blur();
		backEnd.isGlowPass = qfalse;
	}
	
	//TODO Maybe check for rdf_noworld stuff but q3mme has full 3d ui
	backEnd.doneSurfaces = qtrue; // for bloom

	return (const void*)(cmd + 1);
}

/*
=============
RB_DrawBuffer

=============
*/
const void	*RB_DrawBuffer( const void *data ) {
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;

	{ vkSecTimer_t t( VK_SEC_BEGINFRAME ); vk_begin_frame(); }

	vk_set_depthrange(DEPTH_RANGE_NORMAL);

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	if ( r_clear->integer && vk.clearAttachment ) {
		const vec4_t color = { 1, 0, 0.5, 1 };

		backEnd.projection2D = qtrue; // to ensure we have viewport that occupies entire window
		vk_clear_color_attachments( color );
		backEnd.projection2D = qfalse;
	}
	return (const void *)(cmd + 1);
}

/*
=============
RB_SwapBuffers

=============
*/
const void	*RB_SwapBuffers( const void *data ) {
	const swapBuffersCommand_t	*cmd;

	// finish any 2D drawing if needed
	RB_EndSurface();

	ResetGhoul2RenderableSurfaceHeap();

	// texture swapping test
	if ( r_showImages->integer ) {
		RB_ShowImages(tr.images.items, tr.images.count);
	}

	cmd = (const swapBuffersCommand_t *)data;

	tr.needScreenMap = 0;

	vk_end_frame();

	if ( backEnd.doneSurfaces && !glState.finishCalled ) {
		vk_queue_wait_idle();
	}

	if (backEnd.screenshotMask && vk.cmd->waitForFence) {
		if (backEnd.screenshotMask & SCREENSHOT_TGA && backEnd.screenshotTGA[0]) {
			R_TakeScreenshot(0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotTGA);
			if (!backEnd.screenShotTGAsilent) {
				ri.Printf(PRINT_ALL, "Wrote %s\n", backEnd.screenshotTGA);
			}
		}
		if (backEnd.screenshotMask & SCREENSHOT_JPG && backEnd.screenshotJPG[0]) {
			R_TakeScreenshotJPEG(0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotJPG);
			if (!backEnd.screenShotJPGsilent) {
				ri.Printf(PRINT_ALL, "Wrote %s\n", backEnd.screenshotJPG);
			}
		}
		if (backEnd.screenshotMask & SCREENSHOT_PNG && backEnd.screenshotPNG[0]) {
			R_TakeScreenshotPNG(0, 0, gls.captureWidth, gls.captureHeight, backEnd.screenshotPNG);
			if (!backEnd.screenShotPNGsilent) {
				ri.Printf(PRINT_ALL, "Wrote %s\n", backEnd.screenshotPNG);
			}
		}
		if (backEnd.screenshotMask & SCREENSHOT_AVI) {
			RB_TakeVideoFrameCmd(&backEnd.vcmd);
		}

		backEnd.screenshotJPG[0] = '\0';
		backEnd.screenshotTGA[0] = '\0';
		backEnd.screenshotPNG[0] = '\0';
		backEnd.screenshotMask = 0;
	}

	vk_present_frame();

	backEnd.projection2D = qfalse;
	backEnd.doneSurfaces = qfalse;
	backEnd.doneBloom = qfalse;
	//backEnd.drawConsole = qfalse;

	return (const void *)(cmd + 1);
}

const void	*RB_WorldEffects( const void *data )
{
	const drawBufferCommand_t	*cmd;

	cmd = (const drawBufferCommand_t *)data;

	// Always flush the tess buffer
	if ( tess.shader && tess.numIndexes )
		RB_EndSurface();

	RB_RenderWorldEffects();

	if ( tess.shader )
		RB_BeginSurface( tess.shader, tess.fogNum, 0 );

	return (const void *)(cmd + 1);
}

/*
=============
RB_ClearColor
=============
*/
static const void *RB_ClearColor( const void *data )
{
	const clearColorCommand_t* cmd = (const clearColorCommand_t*)data;

	backEnd.projection2D = qtrue;

	if ( r_fastsky->integer )
		vk_clear_color_attachments( (float*)tr.clearColor );
	else
		vk_clear_color_attachments( (float*)tr.world->fogs[tr.world->globalFog].color );

	backEnd.projection2D = qfalse;

	return (const void*)(cmd + 1);
}

#ifdef VK_CUBEMAP
/*
=============
RB_PrefilterEnvMap
=============
*/
static const void *RB_PrefilterEnvMap( const void *data )
{
	const convolveCubemapCommand_t *cmd = (const convolveCubemapCommand_t *)data;

	// finish any 2D drawing if needed
	if ( tess.numIndexes )
		RB_EndSurface();

	vk_set_2d();

	Com_Printf("prefilter cubemaps\n");

	if ( !cmd->cubemap->prefiltered_image )
		cmd->cubemap->prefiltered_image = R_CreateImage( 
			va("cubemap prefitlered - %s", cmd->cubemap->name ), 
			NULL, REF_CUBEMAP_SIZE, REF_CUBEMAP_SIZE, 
			IMGFLAG_CUBEMAP | IMGFLAG_MIPMAP, 
			VK_FORMAT_R16G16B16A16_SFLOAT, 0 );
	
	if ( !cmd->cubemap->irradiance_image )
		cmd->cubemap->irradiance_image = R_CreateImage( 
			va("cubemap irradiance - %s", cmd->cubemap->name ), 
			NULL, REF_CUBEMAP_IRRADIANCE_SIZE, REF_CUBEMAP_IRRADIANCE_SIZE, 
			IMGFLAG_CUBEMAP | IMGFLAG_MIPMAP, 
			VK_FORMAT_R32G32B32A32_SFLOAT, 0 );

#ifdef _DEBUG
	assert( cmd->cubemap->irradiance_image );
	assert( cmd->cubemap->prefiltered_image );
#endif

	vk_generate_cubemaps( cmd->cubemap );

	return (const void *)(cmd + 1);
}
#endif
/*
====================
RB_ExecuteRenderCommands
====================
*/
extern const void *R_DrawWireframeAutomap( const void *data ); //tr_world.cpp
void RB_ExecuteRenderCommands( const void *data ) {
	int		t1, t2;
	const double tProf = r_sunShadowInfo->integer >= 2 ? R_SunProfNow() : 0.0;
	const double tCpu = vk_cputime_active() ? R_SunProfNow() : 0.0;	// r_cpuTimes: the backend, waits included

	t1 = ri.Milliseconds()*ri.Cvar_VariableValue( "timescale" );

	while ( 1 ) {
		data = PADP(data, sizeof(void *));

		// r_cpuTimes: each render command's CPU time by kind (VKSEC)
		const int cmdId = *(const int *)data;
		const double tCmd = ( tCpu > 0.0 && cmdId != RC_END_OF_LIST ) ? R_SunProfNow() : 0.0;
		struct cmdTimer_t {
			int id; double t0;
			~cmdTimer_t() {
				if ( t0 <= 0.0 )
					return;
				const int slot = ( id == RC_SET_COLOR || id == RC_STRETCH_PIC || id == RC_ROTATE_PIC || id == RC_ROTATE_PIC2 || id == RC_XENUI ) ? VK_SEC_2D
					: id == RC_DRAW_SURFS ? VK_SEC_DRAWSURFS : id == RC_DRAW_BUFFER ? VK_SEC_DRAWBUFFER
					: id == RC_SWAP_BUFFERS ? VK_SEC_SWAP : id == RC_WORLD_EFFECTS ? VK_SEC_WORLDFX : VK_SEC_OTHERCMD;
				vk_cputime_section( slot, R_SunProfNow() - t0 );
			}
		} cmdTimer = { cmdId, tCmd };

		switch ( cmdId ) {
		case RC_XENUI:
			data = RB_XenUI( data );
			break;
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			data = RB_StretchPic( data );
			break;
		case RC_ROTATE_PIC:
			data = RB_RotatePic( data );
			break;
		case RC_ROTATE_PIC2:
			data = RB_RotatePic2( data );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_DRAW_BUFFER:
			data = RB_DrawBuffer( data );
			break;
		case RC_SWAP_BUFFERS:
			data = RB_SwapBuffers( data );
			break;
		case RC_VIDEOFRAME:
			data = RB_TakeVideoFrameCmd( data );
			break;
		case RC_WORLD_EFFECTS:
			data = RB_WorldEffects( data );
			break;
		case RC_AUTO_MAP:
			data = R_DrawWireframeAutomap(data);
			break;
#ifdef VK_CUBEMAP
		case RC_CONVOLVECUBEMAP:
			data = RB_PrefilterEnvMap( data );
			break;
#endif
		case RC_CLEARCOLOR:
			data = RB_ClearColor(data);
			break;
		case RC_END_OF_LIST:
		default:
			// stop rendering
			vk_end_frame();
			t2 = ri.Milliseconds()*ri.Cvar_VariableValue( "timescale" );
			backEnd.pc.msec = t2 - t1;
			if ( tProf > 0.0 )
				R_SunProfAdd( SUNPROF_BACKEND, R_SunProfNow() - tProf );
			if ( tCpu > 0.0 )
				vk_cputime_add( VK_CPUT_BE, R_SunProfNow() - tCpu );
			return;
		}
	}

}