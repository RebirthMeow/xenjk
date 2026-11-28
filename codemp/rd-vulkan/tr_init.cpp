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

// tr_init.c -- functions that are not called every frame

#include "tr_local.h"
#include "rd-common/tr_cloth.h"
#include "rd-common/tr_hair.h"

#include <algorithm>
#include "../rd-common/tr_common.h"
#include "tr_WorldEffects.h"
#include "qcommon/MiniHeap.h"
#include "ghoul2/g2_local.h"
#include "tr_cache.h"

glconfig_t	glConfig;
glconfigExt_t glConfigExt;
glstate_t	glState;
window_t	window;
glstatic_t	gls;

cvar_t	*r_verbose;
cvar_t	*r_ignore;

cvar_t	*r_detailTextures;

cvar_t	*r_znear;
cvar_t	*r_zproj;

cvar_t	*r_skipBackEnd;

cvar_t	*r_measureOverdraw;

cvar_t	*r_inGameVideo;
cvar_t	*r_fastsky;
cvar_t	*r_drawSun;
cvar_t	*r_dynamiclight;
// rjr - removed for hacking
cvar_t	*r_dlightBacks;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;
cvar_t	*r_autolodscalevalue;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_drawfog;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_cullRoofFaces; //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
cvar_t	*r_roofCullCeilDist; //ceiling distance cull tolerance -rww
cvar_t	*r_roofCullFloorDist; //floor distance cull tolerance -rww
cvar_t	*r_showcluster;
cvar_t	*r_nocurves;

cvar_t	*r_autoMap; //automap renderside toggle for debugging -rww
cvar_t	*r_autoMapBackAlpha; //alpha of automap bg -rww
cvar_t	*r_autoMapDisable; //don't calc it (since it's slow in debug) -rww

cvar_t	*r_dlightStyle;
cvar_t	*r_surfaceSprites;
cvar_t	*r_surfaceWeather;

cvar_t	*r_windSpeed;
cvar_t	*r_windAngle;
cvar_t	*r_windGust;
cvar_t	*r_windDampFactor;
cvar_t	*r_windPointForce;
cvar_t	*r_windPointX;
cvar_t	*r_windPointY;

cvar_t	*r_allowExtensions;

cvar_t	*r_ext_compressed_textures;
cvar_t	*r_ext_compressed_lightmaps;
cvar_t	*r_ext_preferred_tc_method;
cvar_t	*r_ext_gamma_control;
cvar_t	*r_ext_multitexture;
cvar_t	*r_ext_compiled_vertex_array;
cvar_t	*r_ext_texture_env_add;
cvar_t	*r_ext_texture_filter_anisotropic;
cvar_t	*r_gammaShaders;

cvar_t	*r_environmentMapping;

cvar_t	*r_DynamicGlow;
cvar_t	*r_DynamicGlowAllStages;
cvar_t	*r_DynamicGlowPasses;
cvar_t	*r_DynamicGlowDelta;
cvar_t	*r_DynamicGlowIntensity;
cvar_t	*r_DynamicGlowSoft;
cvar_t	*r_DynamicGlowWidth;
cvar_t	*r_DynamicGlowHeight;
cvar_t	*r_DynamicGlowScale;

cvar_t	*r_smartpicmip;

cvar_t	*r_ignoreGLErrors;
cvar_t	*r_logFile;

cvar_t	*r_primitives;
cvar_t	*r_texturebits;
cvar_t	*r_texturebitslm;

cvar_t	*r_lightmap;
cvar_t	*r_distanceCull;
cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_shadows;
cvar_t	*r_shadowRange;


cvar_t	*r_flares;
//cvar_t	*r_flareSize;
//cvar_t	*r_flareFade;
//cvar_t	*r_flareCoeff;

cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_markcount;
cvar_t	*r_textureMode;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_gamma;
cvar_t	*r_intensity;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;



cvar_t	*r_overBrightBits;
cvar_t	*r_mapOverBrightBits;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;
cvar_t	*r_ddsTextures;
cvar_t	*r_ddsMaxSize;
cvar_t	*r_fixSpecBlue;
cvar_t	*r_arrayDDS;
cvar_t	*r_ddsRangeRead;
cvar_t	*r_arrayDump;

cvar_t	*r_showImages;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugSort;

cvar_t	*r_marksOnTriangleMeshes;

cvar_t	*r_aspectCorrectFonts;
cvar_t	*cl_ratioFix;
cvar_t	*r_splashFit;
cvar_t	*r_patchStitching;

// Vulkan
cvar_t	*r_defaultImage;
cvar_t	*r_device;
//cvar_t	*r_stencilbits;
cvar_t	*r_ext_multisample;
cvar_t	*r_ext_supersample;
cvar_t	*r_ext_alpha_to_coverage;
cvar_t	*r_a2cDiscard;
cvar_t	*r_hdr;
cvar_t	*r_ext_max_anisotropy;
cvar_t	*r_mapGreyScale;
cvar_t	*r_greyscale;
cvar_t	*r_dither;
cvar_t	*r_presentBits;
cvar_t	*r_maxFrameLatency;
cvar_t	*r_gpuTimes;
cvar_t	*r_cpuTimes;
cvar_t	*r_g2LazyBones;
cvar_t	*r_swapchainImages;
cvar_t	*r_presentWait;
cvar_t	*r_bloom;
cvar_t	*r_bloom_threshold;
cvar_t	*r_bloom_intensity;
cvar_t	*r_bloom_threshold_mode;
cvar_t	*r_bloom_modulate;
cvar_t	*r_sunlightMode;
cvar_t	*r_sunShadows;
cvar_t	*r_forceSun;
cvar_t	*r_forceSunMapLightScale;
cvar_t	*r_forceSunLightScale;
cvar_t	*r_forceSunAmbientScale;
cvar_t	*r_shadowFilter;
cvar_t	*r_shadowMapSize;
cvar_t	*r_shadowCascadeZNear;
cvar_t	*r_shadowCascadeZFar;
cvar_t	*r_sunShadowFarCache;
cvar_t	*r_sunShadowMidCache;
cvar_t	*r_sunShadowCacheSkip;
cvar_t	*r_shadowCascadeZBias;
cvar_t	*r_sunShadowModels;
cvar_t	*r_charShadows;
cvar_t	*r_charShadowStrength;
cvar_t	*r_charShadowMinElevation;
cvar_t	*r_charShadowDistance;
cvar_t	*r_charShadowSoftness;
cvar_t	*r_charShadowLightSize;
cvar_t	*r_charShadowFloor;
cvar_t	*r_loadProfile;
cvar_t	*r_tangentCache;
cvar_t	*r_pipelineCache;
cvar_t	*r_modelWarmupBudget;
cvar_t	*r_charShadowDebug;
cvar_t	*r_sunShadowDebug;
cvar_t	*r_sunShadowInfo;
cvar_t	*r_depthPrepass;
cvar_t	*r_sunStylePasses;
cvar_t	*r_stylePassSimple;
cvar_t	*r_parallaxMapping;
cvar_t	*r_parallaxFast;
cvar_t	*r_renderWidth;
cvar_t	*r_renderHeight;
cvar_t	*r_renderScale;
cvar_t	*r_ignorehwgamma;

#ifdef HDR_DELUXE_LIGHTMAP
cvar_t	*r_deluxeMapping;
cvar_t	*r_deluxeSpecular;
#endif

#ifdef VK_DLIGHT_GPU
cvar_t	*r_dlightMethod;
#endif
#ifdef USE_PMLIGHT
cvar_t	*r_dlightMode;
cvar_t	*r_dlightScale;
cvar_t	*r_dlightIntensity;
#endif
cvar_t	*r_dlightSaturation;
cvar_t	*r_roundImagesDown;
cvar_t	*r_nomip;
#ifdef USE_VK_PBR
cvar_t  *r_normalMapping;
cvar_t  *r_specularMapping;
cvar_t  *r_baseNormalX;
cvar_t  *r_baseNormalY;
cvar_t  *r_baseParallax;
cvar_t  *r_pbrMaterials;
cvar_t  *r_toon;
cvar_t  *r_baseSpecular;
#ifdef VK_CUBEMAP
cvar_t	*r_cubeMapping;
cvar_t	*r_cubeMapBlend;
cvar_t	*r_cubeMapParallax;
cvar_t	*r_cubeMapDebug;
cvar_t	*r_cubeMapRoughCut;
cvar_t	*r_cubeMapDiffuse;
cvar_t	*r_specularOcclusion;
cvar_t	*r_specularAA;
#endif
#ifdef USE_VK_SSAO
cvar_t	*r_ssao;
cvar_t	*r_ssao_samples;
cvar_t	*r_ssao_radius;
cvar_t	*r_ssao_intensity;
cvar_t	*r_ssao_power;
cvar_t	*r_ssao_bias;
cvar_t	*r_ssao_falloff;
cvar_t	*r_ssao_halfRes;
cvar_t	*r_ssao_blurRadius;
cvar_t	*r_ssao_character;
cvar_t	*r_ssaoWorld;
cvar_t	*r_ssaoReuseExtract;
#endif
cvar_t	*r_g2SampleShading;
cvar_t	*r_g2SampleCoverage;
cvar_t	*r_g2SampleReport;
cvar_t	*r_portraitRig;
cvar_t	*r_portraitRigDist;
cvar_t	*r_portraitRigUnits;
cvar_t	*r_portraitRigLevel;
cvar_t	*r_portraitRigWorld;
cvar_t	*r_portraitRigSide;
cvar_t	*r_charLinear;
cvar_t	*r_charTerms;
cvar_t	*r_charSelfShadow;
cvar_t	*r_charShadowCull;
cvar_t	*r_charSelfShadowRays;
cvar_t	*r_charSelfShadowSteps;
cvar_t	*r_charSelfShadowLightSize;
cvar_t	*r_charSelfShadowSoftness;
cvar_t	*r_charSelfShadowBias;
cvar_t	*r_charSelfShadowOffset;
cvar_t	*r_charSelfShadowHairGain;
cvar_t	*r_charSelfShadowHairLod0;
cvar_t	*r_charSkinSoftNormals;
cvar_t	*r_charSkinSoftAlbedo;
cvar_t	*r_g2BindPose;
cvar_t	*r_charSelfShadowDebugBox;
cvar_t	*r_charSelfShadowSlopeBias;
cvar_t	*r_charSelfShadowScreen;
cvar_t	*r_charSelfShadowHairMap;
cvar_t	*r_charHairMap16;
cvar_t	*r_stageCovers;
cvar_t	*r_stageSkinSoftNormals;
cvar_t	*r_stageSkinSoftAlbedo;
cvar_t	*r_stageRigLevel;
cvar_t	*r_stageRigFill;
cvar_t	*r_stageRigRim;
cvar_t	*r_stageRigTop;
cvar_t	*r_stageRigWorld;
cvar_t	*r_stageSampleShading;
cvar_t	*r_charSelfShadowHairDensity;
cvar_t	*r_charSelfShadowHairStrandDepth;
cvar_t	*r_charSelfShadowDistance;
cvar_t	*r_charSelfShadowDebug;
cvar_t	*r_charAO;
cvar_t	*r_charContactShadows;
cvar_t	*r_charOccHair;
cvar_t	*r_charContactSoftness;
cvar_t	*r_charContactFine;
cvar_t	*r_charHairOpacity;
cvar_t	*r_charOccHairAlpha;
cvar_t	*r_charContactRays;
cvar_t	*r_charContactBlur;
cvar_t	*r_charOccRect;
cvar_t	*r_charOccRectMargin;
cvar_t	*r_charFast;
cvar_t	*r_charFill;
cvar_t	*r_charRim;
cvar_t	*r_charAmbientFloor;
cvar_t	*r_fixG2SceneTime, *r_fixG2SceneLod, *r_g2LodStats;
cvar_t	*r_fixFmaShadows;
cvar_t	*r_fmaDebug;
cvar_t	*r_charOccHalfDepth;
cvar_t	*r_charOccMask;
cvar_t	*r_charAORadiusCap;
cvar_t	*r_charContactGridShare;
cvar_t	*r_charContactMask;
cvar_t	*r_charAORadius;
cvar_t	*r_charAOPower;
cvar_t	*r_charAOSlices;
cvar_t	*r_charAOSteps;
cvar_t	*r_charContactThickness;
cvar_t	*r_charContactSteps;
cvar_t	*r_charOcclusionDebug;
#ifdef VK_COMPUTE_NORMALMAP
cvar_t	*r_genNormalMaps;
#endif
#endif

#ifdef USE_VK_LIGHTGRID
cvar_t	*r_showLightgrid;
#endif

// the limits apply to the sum of all scenes in a frame --
// the main view, all the 3D icons, etc
#define	DEFAULT_MAX_POLYS		4096	// xenjk: 600 before; blood marks are polys (cgame cg_gore.c)
#define	DEFAULT_MAX_POLYVERTS	20480
static cvar_t	*r_maxpolys;
static cvar_t	*r_maxpolyverts;
int		max_polys;
int		max_polyverts;

cvar_t	*r_modelpoolmegs; // unused

/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
cvar_t	*r_noPrecacheGLA;
#endif

cvar_t	*r_noServerGhoul2;
cvar_t	*r_Ghoul2AnimSmooth=0;
cvar_t	*r_Ghoul2UnSqashAfterSmooth=0;
cvar_t	*r_g2Blend;			// xenjk: draw-only rotation-space Ghoul2 blending (rd-common/tr_g2blend.cpp)
cvar_t	*r_g2BlendCurve;
cvar_t	*r_g2SmoothFix;
cvar_t	*r_g2Joints;		// xenjk: Ghoul2 models bent about their own joints (rd-common/tr_g2joints.cpp)
cvar_t	*r_g2BlendStats;
cvar_t	*r_g2BlendStatsModel;
//cvar_t	*r_Ghoul2UnSqash;
//cvar_t	*r_Ghoul2TimeBase=0; from single player
//cvar_t	*r_Ghoul2NoLerp;
//cvar_t	*r_Ghoul2NoBlend;
//cvar_t	*r_Ghoul2BlendMultiplier=0;

cvar_t	*broadsword=0;
cvar_t	*broadsword_kickbones=0;
cvar_t	*broadsword_kickorigin=0;
cvar_t	*broadsword_playflop=0;
cvar_t	*broadsword_dontstopanim=0;
cvar_t	*broadsword_waitforshot=0;
cvar_t	*broadsword_smallbbox=0;
cvar_t	*broadsword_extra1=0;
cvar_t	*broadsword_extra2=0;

cvar_t	*broadsword_effcorr=0;
cvar_t	*broadsword_ragtobase=0;
cvar_t	*broadsword_dircap=0;

/*
Ghoul2 Insert End
*/

cvar_t *se_language;

cvar_t *r_aviMotionJpegQuality;
cvar_t *r_videoAsync;
cvar_t *r_screenshotJpegQuality;

// Vulkan
#include "vk_local.h"
Vk_Instance vk;
Vk_World	vk_world;

#if 0
#if !defined(__APPLE__)
PFNGLSTENCILOPSEPARATEPROC qglStencilOpSeparate;
#endif

PFNGLACTIVETEXTUREARBPROC qglActiveTextureARB;
PFNGLCLIENTACTIVETEXTUREARBPROC qglClientActiveTextureARB;
PFNGLMULTITEXCOORD2FARBPROC qglMultiTexCoord2fARB;
#if !defined(__APPLE__)
PFNGLTEXIMAGE3DPROC qglTexImage3D;
PFNGLTEXSUBIMAGE3DPROC qglTexSubImage3D;
#endif

PFNGLCOMBINERPARAMETERFVNVPROC qglCombinerParameterfvNV;
PFNGLCOMBINERPARAMETERIVNVPROC qglCombinerParameterivNV;
PFNGLCOMBINERPARAMETERFNVPROC qglCombinerParameterfNV;
PFNGLCOMBINERPARAMETERINVPROC qglCombinerParameteriNV;
PFNGLCOMBINERINPUTNVPROC qglCombinerInputNV;
PFNGLCOMBINEROUTPUTNVPROC qglCombinerOutputNV;

PFNGLFINALCOMBINERINPUTNVPROC qglFinalCombinerInputNV;
PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC qglGetCombinerInputParameterfvNV;
PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC qglGetCombinerInputParameterivNV;
PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC qglGetCombinerOutputParameterfvNV;
PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC qglGetCombinerOutputParameterivNV;
PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC qglGetFinalCombinerInputParameterfvNV;
PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC qglGetFinalCombinerInputParameterivNV;

PFNGLPROGRAMSTRINGARBPROC qglProgramStringARB;
PFNGLBINDPROGRAMARBPROC qglBindProgramARB;
PFNGLDELETEPROGRAMSARBPROC qglDeleteProgramsARB;
PFNGLGENPROGRAMSARBPROC qglGenProgramsARB;
PFNGLPROGRAMENVPARAMETER4DARBPROC qglProgramEnvParameter4dARB;
PFNGLPROGRAMENVPARAMETER4DVARBPROC qglProgramEnvParameter4dvARB;
PFNGLPROGRAMENVPARAMETER4FARBPROC qglProgramEnvParameter4fARB;
PFNGLPROGRAMENVPARAMETER4FVARBPROC qglProgramEnvParameter4fvARB;
PFNGLPROGRAMLOCALPARAMETER4DARBPROC qglProgramLocalParameter4dARB;
PFNGLPROGRAMLOCALPARAMETER4DVARBPROC qglProgramLocalParameter4dvARB;
PFNGLPROGRAMLOCALPARAMETER4FARBPROC qglProgramLocalParameter4fARB;
PFNGLPROGRAMLOCALPARAMETER4FVARBPROC qglProgramLocalParameter4fvARB;
PFNGLGETPROGRAMENVPARAMETERDVARBPROC qglGetProgramEnvParameterdvARB;
PFNGLGETPROGRAMENVPARAMETERFVARBPROC qglGetProgramEnvParameterfvARB;
PFNGLGETPROGRAMLOCALPARAMETERDVARBPROC qglGetProgramLocalParameterdvARB;
PFNGLGETPROGRAMLOCALPARAMETERFVARBPROC qglGetProgramLocalParameterfvARB;
PFNGLGETPROGRAMIVARBPROC qglGetProgramivARB;
PFNGLGETPROGRAMSTRINGARBPROC qglGetProgramStringARB;
PFNGLISPROGRAMARBPROC qglIsProgramARB;

PFNGLLOCKARRAYSEXTPROC qglLockArraysEXT;
PFNGLUNLOCKARRAYSEXTPROC qglUnlockArraysEXT;
#endif

bool g_bTextureRectangleHack = false;

void RE_SetLightStyle( int style, int color );
void RE_GetBModelVerts( int bmodelIndex, vec3_t *verts, vec3_t normal );

void R_Set2DRatio( void ) {
	if (cl_ratioFix->integer)
		tr.widthRatioCoef = ((float)(SCREEN_WIDTH * gls.windowHeight) / (float)(SCREEN_HEIGHT * gls.windowWidth));
	else
		tr.widthRatioCoef = 1.0f;

	if (tr.widthRatioCoef > 1)
		tr.widthRatioCoef = 1.0f;
}

/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/

/*
==================
RB_ReadPixels

Reads an image but takes care of alignment issues for reading RGB images.

Reads a minimum offset for where the RGB data starts in the image from
integer stored at pointer offset. When the function has returned the actual
offset was written back to address offset. This address will always have an
alignment of packAlign to ensure efficient copying.

Stores the length of padding after a line of pixels to address padlen

Return value must be freed with Hunk_FreeTempMemory()
==================
*/

byte *RB_ReadPixels( int x, int y, int width, int height, size_t *offset, int *padlen, int lineAlign )
{
	byte *buffer, *bufstart;
	int bufAlign, linelen;
	int packAlign = 1;

	linelen = width * 3;

	bufAlign = MAX(packAlign, 16); // for SIMD

	// Allocate a few more bytes so that we can choose an alignment we like
	//buffer = ri.Hunk_AllocateTempMemory(padwidth * height + *offset + bufAlign - 1);
	buffer = (byte*)Hunk_AllocateTempMemory(width * height * 4 + *offset + bufAlign - 1);
	bufstart = (byte*)PADP((intptr_t)buffer + *offset, bufAlign);

	vk_read_pixels(bufstart, width, height);

	*offset = bufstart - buffer;
	*padlen = PAD(linelen, packAlign) - linelen;

	return buffer;
}

/*
==================
R_TakeScreenshot
==================
*/
void R_TakeScreenshot( int x, int y, int width, int height, char *fileName ) {
	byte *allbuf, *buffer;
	byte *srcptr, *destptr;
	byte *endline, *endmem;
	byte temp;

	int linelen, padlen;
	size_t offset = 18, memcount;

	allbuf = RB_ReadPixels(x, y, width, height, &offset, &padlen, 0);
	buffer = allbuf + offset - 18;

	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	// swap rgb to bgr and remove padding from line endings
	linelen = width * 3;

	srcptr = destptr = allbuf + offset;
	endmem = srcptr + (linelen + padlen) * height;

	while(srcptr < endmem)
	{
		endline = srcptr + linelen;

		while(srcptr < endline)
		{
			temp = srcptr[0];
			*destptr++ = srcptr[2];
			*destptr++ = srcptr[1];
			*destptr++ = temp;

			srcptr += 3;
		}

		// Skip the pad
		srcptr += padlen;
	}

	memcount = linelen * height;

	// gamma correct
	if(glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders)
		R_GammaCorrect(allbuf + offset, memcount);

	ri.FS_WriteFile(fileName, buffer, memcount + 18);

	ri.Hunk_FreeTempMemory(allbuf);
}

/*
==================
R_TakeScreenshotPNG
==================
*/
void R_TakeScreenshotPNG( int x, int y, int width, int height, char *fileName ) {
	byte *buffer=NULL;
	size_t offset=0;
	int padlen=0;

	buffer = RB_ReadPixels( x, y, width, height, &offset, &padlen, 0);
	RE_SavePNG( fileName, buffer, width, height, 3 );
	ri.Hunk_FreeTempMemory( buffer );
}

/*
==================
R_TakeScreenshotJPEG
==================
*/
void R_TakeScreenshotJPEG( int x, int y, int width, int height, char *fileName ) {
	byte *buffer;
	size_t offset = 0, memcount;
	int padlen;

	buffer = RB_ReadPixels(x, y, width, height, &offset, &padlen, 0);
	memcount = (width * 3 + padlen) * height;

	// gamma correct
	if(glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders)
		R_GammaCorrect(buffer + offset, memcount);

	RE_SaveJPG(fileName, r_screenshotJpegQuality->integer, width, height, buffer + offset, padlen);
	ri.Hunk_FreeTempMemory(buffer);
}

/*
==================
R_ScreenshotFilename
==================
*/
void R_ScreenshotFilename( char *buf, int bufSize, const char *ext ) {
	time_t rawtime;
	char timeStr[32] = {0}; // should really only reach ~19 chars

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime

	Com_sprintf( buf, bufSize, "screenshots/shot%s.%s", timeStr, ext );
}

/*
====================
R_LevelShot

levelshots are specialized 256*256 thumbnails for
the menu system, sampled down from full screen distorted images
====================
*/
#define LEVELSHOTSIZE 256
static void R_LevelShot( void ) {
	char		checkname[MAX_OSPATH];
	byte		*buffer;
	byte		*source, *allsource;
	byte		*src, *dst;
	size_t		offset = 0;
	int			padlen;
	int			x, y;
	int			r, g, b;
	float		xScale, yScale;
	int			xx, yy;

	Com_sprintf( checkname, sizeof(checkname), "levelshots/%s.tga", tr.world->baseName );

	allsource = RB_ReadPixels(0, 0, gls.captureWidth, gls.captureHeight, &offset, &padlen, 0);
	source = allsource + offset;

	buffer = (byte *)ri.Hunk_AllocateTempMemory(LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18);
	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = LEVELSHOTSIZE & 255;
	buffer[13] = LEVELSHOTSIZE >> 8;
	buffer[14] = LEVELSHOTSIZE & 255;
	buffer[15] = LEVELSHOTSIZE >> 8;
	buffer[16] = 24;	// pixel size

	// resample from source
	xScale = glConfig.vidWidth / (4.0*LEVELSHOTSIZE);
	yScale = glConfig.vidHeight / (3.0*LEVELSHOTSIZE);
	for ( y = 0 ; y < LEVELSHOTSIZE ; y++ ) {
		for ( x = 0 ; x < LEVELSHOTSIZE ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = source + 3 * ( glConfig.vidWidth * (int)( (y*3+yy)*yScale ) + (int)( (x*4+xx)*xScale ) );
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * LEVELSHOTSIZE + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	// gamma correct
	if ( ( tr.overbrightBits > 0 ) && glConfig.deviceSupportsGamma && !glConfigExt.doGammaCorrectionWithShaders ) {
		R_GammaCorrect( buffer + 18, LEVELSHOTSIZE * LEVELSHOTSIZE * 3 );
	}

	ri.FS_WriteFile( checkname, buffer, LEVELSHOTSIZE * LEVELSHOTSIZE*3 + 18 );

	ri.Hunk_FreeTempMemory( buffer );
	ri.Hunk_FreeTempMemory( allsource );

	vk_debug("[skipnotify]Wrote %s\n", checkname );
}

void R_ScreenShot_f ( void ) {
	char checkname[MAX_OSPATH] = {0};
	qboolean silent = qfalse;
	int			typeMask;
	const char *ext;

	if (ri.VK_IsMinimized() && !R_CanMinimize()) {
		ri.Printf(PRINT_WARNING, "WARNING: unable to take screenshot when minimized because FBO is not available/enabled.\n");
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if (Q_stricmp(ri.Cmd_Argv(0), "screenshot_tga") == 0) {
		typeMask = SCREENSHOT_TGA;
		ext = "tga";
	}
	else if (Q_stricmp(ri.Cmd_Argv(0), "screenshot_png") == 0) {
		typeMask = SCREENSHOT_PNG;
		ext = "png";
	}
	else {
		typeMask = SCREENSHOT_JPG;
		ext = "jpg";
	}

	// check if already scheduled
	if (backEnd.screenshotMask & typeMask)
		return;

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) )
		silent = qtrue;

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, sizeof( checkname ), "screenshots/%s.%s", ri.Cmd_Argv( 1 ), ext );
	}
	else {
		// timestamp the filename
		R_ScreenshotFilename( checkname, sizeof( checkname ), ext );

		if ( ri.FS_FileExists( checkname ) ) {
			vk_debug("ScreenShot: Couldn't create a file\n" );
			return;
 		}
	}

	// we will make the screenshot right at the end of RE_EndFrame()
	backEnd.screenshotMask |= typeMask;
	if (typeMask == SCREENSHOT_JPG) {
		backEnd.screenShotJPGsilent = silent;
		Q_strncpyz(backEnd.screenshotJPG, checkname, sizeof(backEnd.screenshotJPG));
	}
	else if (typeMask == SCREENSHOT_PNG) {
		backEnd.screenShotPNGsilent = silent;
		Q_strncpyz(backEnd.screenshotPNG, checkname, sizeof(backEnd.screenshotPNG));
	}
	else {
		backEnd.screenShotTGAsilent = silent;
		Q_strncpyz(backEnd.screenshotTGA, checkname, sizeof(backEnd.screenshotTGA));
	}
}

/*
==================
RB_TakeVideoFrameCmd
==================
*/
const void *RB_TakeVideoFrameCmd( const void *data )
{
	const videoFrameCommand_t *cmd = (const videoFrameCommand_t*)data;

	vk_read_pixels( (byte*)PADP(cmd->captureBuffer, 1), cmd->width, cmd->height );
	RB_EncodeVideoFrame( cmd );

	return (const void*)(cmd + 1);
}

/*
==================
RB_EncodeVideoFrame

Encodes a captured frame (bottom-up RGB rows in the command's capture buffer) and hands it to the client; shared by
the immediate readback above and the deferred one in vk_video.cpp.
==================
*/
void RB_EncodeVideoFrame( const videoFrameCommand_t *cmd )
{
	byte		*cBuf;
	size_t		memcount, linelen;
	int			padwidth, avipadwidth, padlen, avipadlen;
	int			packAlign;

	packAlign = 1;

	linelen = cmd->width * 3;

	// Alignment stuff for glReadPixels
	padwidth = PAD(linelen, packAlign);
	padlen = padwidth - linelen;
	// AVI line padding
	avipadwidth = PAD(linelen, AVI_LINE_PADDING);
	avipadlen = avipadwidth - linelen;

	cBuf = (byte*)PADP(cmd->captureBuffer, packAlign);

	memcount = padwidth * cmd->height;

	// gamma correct
	if (glConfig.deviceSupportsGamma)
		R_GammaCorrect(cBuf, memcount);

	if (cmd->motionJpeg)
	{
		memcount = RE_SaveJPGToBuffer(cmd->encodeBuffer, linelen * cmd->height,
			r_aviMotionJpegQuality->integer,
			cmd->width, cmd->height, cBuf, padlen);
		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, memcount);
	}
	else
	{
		byte* lineend, * memend;
		byte* srcptr, * destptr;

		srcptr = cBuf;
		destptr = cmd->encodeBuffer;
		memend = srcptr + memcount;

		// swap R and B and remove line paddings
		while (srcptr < memend)
		{
			lineend = srcptr + linelen;
			while (srcptr < lineend)
			{
				*destptr++ = srcptr[2];
				*destptr++ = srcptr[1];
				*destptr++ = srcptr[0];
				srcptr += 3;
			}

			Com_Memset(destptr, '\0', avipadlen);
			destptr += avipadlen;

			srcptr += padlen;
		}

		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, avipadwidth * cmd->height);
	}
}

void R_RemapSkyShader_f ( void ) {
	int num;

	if (ri.Cmd_Argc() != 2 || !strlen(ri.Cmd_Argv(1))) {
		vk_debug("Usage: /remapSky <new>\n");
		return;
	}

	for (num = 0; num < tr.numShaders; num++) {
		if (tr.shaders[num]->sky)
		{
			R_RemapShader(tr.shaders[num]->name, ri.Cmd_Argv(1), NULL);
		}
	}
}

void R_ClearRemaps_f( void ) {
	int num;

	for (num = 0; num < tr.numShaders; num++) {
		tr.shaders[num]->remappedShader = NULL;
	}
}

typedef struct consoleCommand_s {
	const char	*cmd;
	xcommand_t	func;
} consoleCommand_t;

static consoleCommand_t	commands[] = {
	{ "imagelist",			R_ImageList_f },
	{ "shaderlist",			R_ShaderList_f },
	{ "skinlist",			R_SkinList_f },
	{ "fontlist",			R_FontList_f },
	{ "screenshot",			R_ScreenShot_f },
	{ "charshadowdump",		R_CharShadowDump_f },
	{ "loadprofile",		R_LoadProfile_f },
	{ "warmmodel",			R_WarmModel_f },
	{ "streaminfo",			R_StreamInfo_f },
	{ "screenshot_png",		R_ScreenShot_f },
	{ "screenshot_tga",		R_ScreenShot_f },
	{ "gfxinfo",			GfxInfo_f },
	{ "r_we",				R_WorldEffect_f },
	//{ "imagecacheinfo",		RE_RegisterImages_Info_f },
	{ "modellist",			R_Modellist_f },
	//{ "modelcacheinfo",		RE_RegisterModels_Info_f },
	{ "r_cleardecals",		RE_ClearDecals },
	{ "remapSky",			R_RemapSkyShader_f },
	{ "clearRemaps",		R_ClearRemaps_f },
	{ "vkinfo",				vk_info_f },
	{ "vkmem",				vk_vram_info_f }
};

static const size_t numCommands = ARRAY_LEN( commands );

#ifdef _DEBUG
#define MIN_PRIMITIVES -1
#else
#define MIN_PRIMITIVES 0
#endif
#define MAX_PRIMITIVES 3

/*
===============
R_Register
===============
*/
void R_Register( void )
{
	// xenjk defaults (the bench preset: scenes.json vulkan-pbr + best + decided + the
	// sun preset): dynamic glow, 4x MSAA, 16x anisotropy, cubemaps, SSAO, rend2 sun with cascade shadows
	// (r_sunlightMode 2, r_sunShadows 1, 2048, filter 2, zfar 4096) and parallax; bloom 0, r_hdr 0 with dither,
	// normal/specular/deluxe mapping were already the defaults. docs/renderer-vulkan-in-xenjk.md
	// Alpha-to-coverage on: alpha-tested cards (hair, feathers, foliage) get MSAA-smoothed edges.
	//FIXME: lol badness
	se_language = ri.Cvar_Get("se_language", "english", CVAR_ARCHIVE | CVAR_NORESTART, "");
	//
	// latched and archived variables
	//
	r_allowExtensions					= ri.Cvar_Get( "r_allowExtensions",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_compressed_textures			= ri.Cvar_Get( "r_ext_compress_textures",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_compressed_lightmaps			= ri.Cvar_Get( "r_ext_compress_lightmaps",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_preferred_tc_method			= ri.Cvar_Get( "r_ext_preferred_tc_method",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_gamma_control					= ri.Cvar_Get( "r_ext_gamma_control",				"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_multitexture					= ri.Cvar_Get( "r_ext_multitexture",				"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_compiled_vertex_array			= ri.Cvar_Get( "r_ext_compiled_vertex_array",		"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_texture_env_add				= ri.Cvar_Get( "r_ext_texture_env_add",				"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ext_texture_filter_anisotropic	= ri.Cvar_Get( "r_ext_texture_filter_anisotropic",	"16",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_gammaShaders						= ri.Cvar_Get( "r_gammaShaders",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Set gamma using pixel shaders inside the game window only." );
	r_environmentMapping				= ri.Cvar_Get( "r_environmentMapping",				"1",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlow						= ri.Cvar_Get( "r_DynamicGlow",						"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Enable dynamic glow effect" );
	r_DynamicGlowAllStages				= ri.Cvar_Get( "r_DynamicGlowAllStages",			"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "In Vanilla certain glow stages are skipped, render those anyway" );
	r_DynamicGlowPasses					= ri.Cvar_Get( "r_DynamicGlowPasses",				"5",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowDelta					= ri.Cvar_Get( "r_DynamicGlowDelta",				"0.8f",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowIntensity				= ri.Cvar_Get( "r_DynamicGlowIntensity",			"1.13f",					CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowSoft					= ri.Cvar_Get( "r_DynamicGlowSoft",					"1",						CVAR_ARCHIVE_ND, "" );
	r_DynamicGlowWidth					= ri.Cvar_Get( "r_DynamicGlowWidth",				"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_DynamicGlowHeight					= ri.Cvar_Get( "r_DynamicGlowHeight",				"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_DynamicGlowScale					= ri.Cvar_Get( "r_DynamicGlowScale",				"0.5",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Size of the first dynamic glow blur level relative to the screen (0.125-1; 0.5 = the old fixed size, smaller is cheaper and wider)" );
	ri.Cvar_CheckRange( r_DynamicGlowScale, 0.125f, 1.0f, qfalse );
	r_picmip							= ri.Cvar_Get( "r_picmip",							"0",						CVAR_ARCHIVE|CVAR_LATCH, "" );
	ri.Cvar_CheckRange( r_picmip, 0, 16, qtrue );
	r_smartpicmip						= ri.Cvar_Get( "r_smartpicmip",						"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "Applies r_picmip setting to map textures only." );
	r_colorMipLevels					= ri.Cvar_Get( "r_colorMipLevels",					"0",						CVAR_LATCH, "" );
	r_detailTextures					= ri.Cvar_Get( "r_detailtextures",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_texturebits						= ri.Cvar_Get( "r_texturebits",						"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_texturebitslm						= ri.Cvar_Get( "r_texturebitslm",					"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_overBrightBits					= ri.Cvar_Get( "r_overBrightBits",					"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_mapOverBrightBits					= ri.Cvar_Get( "r_mapOverBrightBits",				"0",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_simpleMipMaps						= ri.Cvar_Get( "r_simpleMipMaps",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	r_ddsTextures						= ri.Cvar_Get( "r_ddsTextures",					"1",						CVAR_ARCHIVE_ND|CVAR_LATCH, "xenjk: load a block-compressed <texture>.dds (BC1/3/4/5/7, stored mips, past MAX_TEXTURE_SIZE) before the other image formats" );
	r_fixSpecBlue						= ri.Cvar_Get( "r_fixSpecBlue", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, "The specularMap conversion counts blue instead of green twice (default 1); 0 = green twice (the old behaviour). Every installed specularMap image is grey, so no installed pixel changes" );
	r_arrayDDS							= ri.Cvar_Get( "r_arrayDDS", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, "Layer arrays read the BC7 DDS beside their layer images when every layer has one (1); 0 = always the RGBA8 images (test)" );
	r_ddsRangeRead						= ri.Cvar_Get( "r_ddsRangeRead", "1", CVAR_ARCHIVE_ND, "A stored DDS without mipmaps reads its header and only the level it uploads by range (1); 0 = the whole file (test)" );
	r_arrayDump							= ri.Cvar_Get( "r_arrayDump", "0", CVAR_NONE, "Tool: layer arrays built from their images write each layer as uploaded (RGBA8, whole chain) to arraydump/<layer>.dds in the home folder" );
	r_ddsMaxSize						= ri.Cvar_Get( "r_ddsMaxSize",						"4096",						CVAR_ARCHIVE_ND|CVAR_LATCH, "xenjk: largest DDS texture side; bigger ones drop their top mips (0 = the device limit). Default 4096: no target resolution shows more" );
	r_vertexLight						= ri.Cvar_Get( "r_vertexLight",						"0",						CVAR_ARCHIVE|CVAR_LATCH, "" );
	r_uiFullScreen						= ri.Cvar_Get( "r_uifullscreen",					"0",						CVAR_NONE, "" );
	r_subdivisions						= ri.Cvar_Get( "r_subdivisions",					"4",						CVAR_ARCHIVE_ND|CVAR_LATCH, "" );
	ri.Cvar_CheckRange( r_subdivisions, 0, 80, qfalse );

	r_fullbright						= ri.Cvar_Get( "r_fullbright",						"0",						CVAR_ARCHIVE_ND, "" );
	r_intensity							= ri.Cvar_Get( "r_intensity",						"1",						CVAR_LATCH, "" );
	r_singleShader						= ri.Cvar_Get( "r_singleShader",					"0",						CVAR_CHEAT|CVAR_LATCH, "" );
	r_lodCurveError						= ri.Cvar_Get( "r_lodCurveError",					"250",						CVAR_ARCHIVE_ND, "" );
	r_lodbias							= ri.Cvar_Get( "r_lodbias",							"0",						CVAR_ARCHIVE_ND, "" );
	r_autolodscalevalue					= ri.Cvar_Get( "r_autolodscalevalue",				"0",						CVAR_ROM, "" );

	r_flares							= ri.Cvar_Get( "r_flares",							"1",						CVAR_ARCHIVE_ND, "" );
	//r_flareSize							= ri.Cvar_Get( "r_flareSize",						"40",						CVAR_ARCHIVE_ND, "" );
	//r_flareFade							= ri.Cvar_Get( "r_flareFade",						"10",						CVAR_ARCHIVE_ND, "" );
	//r_flareCoeff						= ri.Cvar_Get( "r_flareCoeff",						"150",						CVAR_ARCHIVE_ND, "" );
	//ri.Cvar_CheckRange(r_flareCoeff, 0.1f, 250, qfalse);

	r_znear								= ri.Cvar_Get( "r_znear",							"4",						CVAR_ARCHIVE_ND, "" );
	ri.Cvar_CheckRange( r_znear, 0.001f, 10, qfalse );
	r_zproj								= ri.Cvar_Get("r_zproj",							"64",						CVAR_ARCHIVE_ND, "" );
	r_ignoreGLErrors					= ri.Cvar_Get( "r_ignoreGLErrors",					"1",						CVAR_ARCHIVE_ND, "" );
	r_fastsky							= ri.Cvar_Get( "r_fastsky",							"0",						CVAR_ARCHIVE_ND, "" );
	r_inGameVideo						= ri.Cvar_Get( "r_inGameVideo",						"1",						CVAR_ARCHIVE_ND, "" );
	r_drawSun							= ri.Cvar_Get( "r_drawSun",							"0",						CVAR_ARCHIVE_ND, "" );
	r_dynamiclight						= ri.Cvar_Get( "r_dynamiclight",					"1",						CVAR_ARCHIVE, "" );
	// rjr - removed for hacking
	r_dlightBacks						= ri.Cvar_Get( "r_dlightBacks",						"1",						CVAR_ARCHIVE_ND, "dlight non-facing surfaces for continuity" );
	r_finish							= ri.Cvar_Get( "r_finish",							"0",						CVAR_ARCHIVE_ND, "" );
	r_textureMode						= ri.Cvar_Get( "r_textureMode",						"GL_LINEAR_MIPMAP_LINEAR",	CVAR_ARCHIVE, "" );
	r_markcount							= ri.Cvar_Get( "r_markcount",						"100",						CVAR_ARCHIVE_ND, "" );
	r_gamma								= ri.Cvar_Get( "r_gamma",							"1",						CVAR_ARCHIVE_ND, "" );
	r_facePlaneCull						= ri.Cvar_Get( "r_facePlaneCull",					"1",						CVAR_ARCHIVE_ND, "" );
	r_cullRoofFaces						= ri.Cvar_Get( "r_cullRoofFaces",					"0",						CVAR_CHEAT, "" ); //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
	r_roofCullCeilDist					= ri.Cvar_Get( "r_roofCullCeilDist",				"256",						CVAR_CHEAT, "" ); //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
	r_roofCullFloorDist					= ri.Cvar_Get( "r_roofCeilFloorDist",				"128",						CVAR_CHEAT, "" ); //attempted smart method of culling out upwards facing surfaces on roofs for automap shots -rww
	r_primitives						= ri.Cvar_Get( "r_primitives",						"0",						CVAR_ARCHIVE_ND, "" );
	ri.Cvar_CheckRange( r_primitives, MIN_PRIMITIVES, MAX_PRIMITIVES, qtrue );
	r_ambientScale						= ri.Cvar_Get( "r_ambientScale",					"0.6",						CVAR_NONE, "" );
	r_directedScale						= ri.Cvar_Get( "r_directedScale",					"1",						CVAR_NONE, "" );
	r_autoMap							= ri.Cvar_Get( "r_autoMap",							"0",						CVAR_ARCHIVE_ND, "" ); //automap renderside toggle for debugging -rww
	r_autoMapBackAlpha					= ri.Cvar_Get( "r_autoMapBackAlpha",				"0",						CVAR_NONE, "" ); //alpha of automap bg -rww
	r_autoMapDisable					= ri.Cvar_Get( "r_autoMapDisable",					"1",						CVAR_NONE, "" );
	r_showImages						= ri.Cvar_Get( "r_showImages",						"0",						CVAR_CHEAT, "" );
	r_debugLight						= ri.Cvar_Get( "r_debuglight",						"0",						CVAR_TEMP, "" );
	r_debugSort							= ri.Cvar_Get( "r_debugSort",						"0",						CVAR_CHEAT, "" );
	r_dlightStyle						= ri.Cvar_Get( "r_dlightStyle",						"1",						CVAR_TEMP, "" );
	r_surfaceSprites					= ri.Cvar_Get( "r_surfaceSprites",					"1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "" );
	r_surfaceWeather					= ri.Cvar_Get( "r_surfaceWeather",					"0",						CVAR_TEMP, "" );
	r_windSpeed							= ri.Cvar_Get( "r_windSpeed",						"0",						CVAR_NONE, "" );
	r_windAngle							= ri.Cvar_Get( "r_windAngle",						"0",						CVAR_NONE, "" );
	r_windGust							= ri.Cvar_Get( "r_windGust",						"0",						CVAR_NONE, "" );
	r_windDampFactor					= ri.Cvar_Get( "r_windDampFactor",					"0.1",						CVAR_NONE, "" );
	r_windPointForce					= ri.Cvar_Get( "r_windPointForce",					"0",						CVAR_NONE, "" );
	r_windPointX						= ri.Cvar_Get( "r_windPointX",						"0",						CVAR_NONE, "" );
	r_windPointY						= ri.Cvar_Get( "r_windPointY",						"0",						CVAR_NONE, "" );
	r_nocurves							= ri.Cvar_Get( "r_nocurves",						"0",						CVAR_CHEAT, "" );
	r_drawworld							= ri.Cvar_Get( "r_drawworld",						"1",						CVAR_CHEAT, "" );
	r_drawfog							= ri.Cvar_Get("r_drawfog",							"2",						CVAR_ARCHIVE_ND, "Fog mode\n"
		" 0 - disabled\n"
		" 1 - legacy fog\n"
		" 2 - fog collapse\n");
	r_lightmap							= ri.Cvar_Get( "r_lightmap",						"0",						CVAR_ARCHIVE_ND, "" );
	r_distanceCull						= ri.Cvar_Get( "r_distanceCull",					"0",						CVAR_ARCHIVE_ND, "Replaces the view distance (game units) of maps whose worldspawn sets distanceCull; read at map load (0 = the map's own)" );
	r_portalOnly						= ri.Cvar_Get( "r_portalOnly",						"0",						CVAR_CHEAT, "" );
	r_skipBackEnd						= ri.Cvar_Get( "r_skipBackEnd",						"0",						CVAR_CHEAT, "" );
	r_measureOverdraw					= ri.Cvar_Get( "r_measureOverdraw",					"0",						CVAR_NONE, "" );
	r_lodscale							= ri.Cvar_Get( "r_lodscale",						"5",						CVAR_ARCHIVE_ND, "" );
	r_norefresh							= ri.Cvar_Get( "r_norefresh",						"0",						CVAR_CHEAT, "" );
	r_drawentities						= ri.Cvar_Get( "r_drawentities",					"1",						CVAR_CHEAT, "" );
	r_ignore							= ri.Cvar_Get( "r_ignore",							"1",						CVAR_CHEAT, "" );
	r_nocull							= ri.Cvar_Get( "r_nocull",							"0",						CVAR_CHEAT, "" );
	r_novis								= ri.Cvar_Get( "r_novis",							"0",						CVAR_CHEAT, "" );
	r_showcluster						= ri.Cvar_Get( "r_showcluster",						"0",						CVAR_CHEAT, "" );
	r_speeds							= ri.Cvar_Get( "r_speeds",							"0",						CVAR_CHEAT, "" );
	r_verbose							= ri.Cvar_Get( "r_verbose",							"0",						CVAR_CHEAT, "" );
	r_logFile							= ri.Cvar_Get( "r_logFile",							"0",						CVAR_CHEAT, "" );
	r_debugSurface						= ri.Cvar_Get( "r_debugSurface",					"0",						CVAR_CHEAT, "" );
	r_nobind							= ri.Cvar_Get( "r_nobind",							"0",						CVAR_CHEAT, "" );
	r_showtris							= ri.Cvar_Get( "r_showtris",						"0",						CVAR_CHEAT, "" );
	r_showsky							= ri.Cvar_Get( "r_showsky",							"0",						CVAR_CHEAT, "" );
	r_shownormals						= ri.Cvar_Get( "r_shownormals",						"0",						CVAR_CHEAT, "" );
	r_clear								= ri.Cvar_Get( "r_clear",							"0",						CVAR_CHEAT, "" );
	r_offsetFactor						= ri.Cvar_Get( "r_offsetfactor",					"-1",						CVAR_CHEAT, "" );
	r_offsetUnits						= ri.Cvar_Get( "r_offsetunits",						"-2",						CVAR_CHEAT, "" );
	r_lockpvs							= ri.Cvar_Get( "r_lockpvs",							"0",						CVAR_CHEAT, "" );
	r_noportals							= ri.Cvar_Get( "r_noportals",						"0",						CVAR_CHEAT, "" );
	r_shadows							= ri.Cvar_Get( "cg_shadows",						"1",						CVAR_NONE, "" );
	r_shadowRange						= ri.Cvar_Get( "r_shadowRange",						"1000",						CVAR_NONE, "" );
	r_marksOnTriangleMeshes				= ri.Cvar_Get( "r_marksOnTriangleMeshes",			"0",						CVAR_ARCHIVE_ND, "" );
	r_aspectCorrectFonts				= ri.Cvar_Get( "r_aspectCorrectFonts",				"0",						CVAR_ARCHIVE, "" );
	r_splashFit							= ri.Cvar_Get( "r_splashFit",						"1",						CVAR_ARCHIVE, "The startup splash keeps its shape: fitted in the window, the bars filled with its corner colour (0 = stretched over the window)" );
	cl_ratioFix							= ri.Cvar_Get( "cl_ratioFix",						"1",						CVAR_ARCHIVE, "On a 16:9 window the load screen uses menu/splash_16_9 when the game has it (0 = always menu/splash)" );
	r_patchStitching					= ri.Cvar_Get( "r_patchStitching",					"1",						CVAR_ARCHIVE, "Enable stitching of neighbouring patch surfaces" );

	R_ClothRegister();
	R_HairRegister();
	r_maxpolys							= ri.Cvar_Get( "r_maxpolys",						XSTRING( DEFAULT_MAX_POLYS ),		CVAR_NONE, "" );
	r_maxpolyverts						= ri.Cvar_Get( "r_maxpolyverts",					XSTRING( DEFAULT_MAX_POLYVERTS ),	CVAR_NONE, "" );

	// Vulkan
	r_defaultImage						= ri.Cvar_Get("r_defaultImage",						"",							CVAR_ARCHIVE_ND | CVAR_LATCH, "Image drawn where a texture is missing: #rgb or #rrggbb for a solid colour, or an image path (empty = the grey grid)");
	r_device							= ri.Cvar_Get("r_device",							"-1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Select physical device to render:\n" \
		" 0+ - use explicit device index\n" \
		" -1 - first discrete GPU\n" \
		" -2 - first integrated GPU");
	ri.Cvar_CheckRange(r_device, -2, 8, qtrue);
	r_device->modified					= qfalse;

	//r_stencilbits						= ri.Cvar_Get("r_stencilbits",						"8",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_ext_multisample					= ri.Cvar_Get("r_ext_multisample",					"4",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	ri.Cvar_CheckRange(r_ext_multisample, 0, 64, qtrue);
	r_ext_supersample					= ri.Cvar_Get("r_ext_supersample",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Supersampling: render at twice the width and height and scale down to the window (sharper, about four times the pixel cost)");
	ri.Cvar_CheckRange(r_ext_supersample, 0, 1, qtrue);
	r_ext_alpha_to_coverage				= ri.Cvar_Get("r_ext_alpha_to_coverage",			"1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "With MSAA, alpha-tested stages (foliage, fences, hair) use alpha to coverage for smooth edges: 0 = off (hard alpha test), 1 = one-pixel coverage ramp on Ghoul2 models and the old coverage elsewhere, 2 = the ramp everywhere, 3 = the old coverage everywhere");
	ri.Cvar_CheckRange(r_ext_alpha_to_coverage, 0, 3, qtrue);	// 1: one-pixel coverage ramp on Ghoul2 models, the old coverage elsewhere; 2: the ramp everywhere; 3: the old coverage everywhere
	r_a2cDiscard						= ri.Cvar_Get("r_a2cDiscard",						"0",						CVAR_NONE, "Test (MSAA alpha-to-coverage stages lit by the light grid or per-pixel vertex light): 1 = fragments whose coverage is zero (alpha <= 0) discard before lighting; they write nothing either way, so the image should not change, only the shading work on transparent texels (hair cards); 0 = shade them (A/B)");
	r_hdr								= ri.Cvar_Get("r_hdr",								"0",						CVAR_ARCHIVE | CVAR_LATCH, "Render target format: 0 = 8-bit with r_dither; 1 = 16-bit UNORM, precision only; 2 = 16-bit float, range past white for a render scene's colour grade (its highlights reach the grade unclipped)");
	r_mapGreyScale						= ri.Cvar_Get("r_mapGreyScale",						"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Desaturate the map's baked light (lightmaps, vertex light) at load: 0 = off, 1 = grey, fractions = partly (-1..1, sign ignored)");
	ri.Cvar_CheckRange(r_mapGreyScale, -1, 1, qfalse);
	r_ext_max_anisotropy				= ri.Cvar_Get("r_ext_max_anisotropy",				"16",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Highest anisotropic filtering level (1..16, capped by the GPU) while r_ext_texture_filter_anisotropic is on");
	ri.Cvar_CheckRange(r_ext_max_anisotropy, 1, 16, qtrue);
	r_greyscale							= ri.Cvar_Get("r_greyscale",						"0",						CVAR_ARCHIVE_ND, "");
	ri.Cvar_CheckRange(r_greyscale, -1, 1, qfalse);
	r_dither							= ri.Cvar_Get("r_dither",							"1",						CVAR_ARCHIVE_ND, "Set dithering mode:\n 0 - disabled\n 1 - ordered");
	ri.Cvar_CheckRange(r_dither, 0, 1, qtrue);
	r_presentBits						= ri.Cvar_Get("r_presentBits",						"24",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Select color bits used for presentation surfaces");
	ri.Cvar_CheckRange(r_presentBits, 16, 30, qtrue);
	r_maxFrameLatency					= ri.Cvar_Get("r_maxFrameLatency",					"1",						CVAR_ARCHIVE_ND, "Frames the GPU may still be drawing when the next frame reads input (0-2; 2 = old behaviour)");
	ri.Cvar_CheckRange(r_maxFrameLatency, 0, NUM_COMMAND_BUFFERS, qtrue);
	r_gpuTimes							= ri.Cvar_Get("r_gpuTimes",							"0",						CVAR_NONE, "Print GPU milliseconds per render pass every frame (bench); 2 also the time between passes (VKGAP lines)");
	r_cpuTimes							= ri.Cvar_Get("r_cpuTimes",							"0",						CVAR_NONE, "Print the renderer's CPU milliseconds per frame: frontend, backend recording and its waits (fence, acquire, submit, present, frame latency), the rest of the frame (bench)");
	r_g2LazyBones						= ri.Cvar_Get("r_g2LazyBones",						"1",						CVAR_NONE, "Ghoul2 skeletons are transformed and uploaded only for entities a view draws (each model once a frame); 0 = every Ghoul2 entity of the scene, drawn or not" );
	r_swapchainImages					= ri.Cvar_Get("r_swapchainImages",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Swapchain images without vsync (immediate/mailbox): 0 = default (3), 2 = shortest presentation queue");
	ri.Cvar_CheckRange(r_swapchainImages, 0, MAX_SWAPCHAIN_IMAGES, qtrue);
	r_presentWait						= ri.Cvar_Get("r_presentWait",						"0",						CVAR_ARCHIVE_ND, "With VK_KHR_present_wait: frames that may wait for the screen when the next frame reads input (0 = off, 1 = wait for the frame just presented, 2 = for the one before)");
	ri.Cvar_CheckRange(r_presentWait, 0, 2, qtrue);
	r_bloom								= ri.Cvar_Get("r_bloom",							"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Enable bloom effect");
	ri.Cvar_CheckRange(r_bloom, 0, 1, qtrue);
	r_bloom_threshold					= ri.Cvar_Get("r_bloom_threshold",					"0.05",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Color level to extract to bloom texture, default is 0.05");
	ri.Cvar_CheckRange(r_bloom_threshold, 0.01f, 1, qfalse);
	r_bloom_threshold_mode				= ri.Cvar_Get("r_bloom_threshold_mode",				"0",						CVAR_ARCHIVE_ND, "Color extraction mode:\n 0: (r|g|b) >= threshold\n 1: (r + g + b ) / 3 >= threshold\n 2: luma(r, g, b) >= threshold");
	r_bloom_intensity					= ri.Cvar_Get("r_bloom_intensity",					"0.15",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Final bloom blend factor, default is 0.15");
	ri.Cvar_CheckRange(r_bloom_intensity, 0.01f, 2, qfalse);
	r_bloom_modulate					= ri.Cvar_Get("r_bloom_modulate",					"0",						CVAR_ARCHIVE_ND, "Modulate extracted color:\n 0: off (color = color, i.e. no changes)\n 1: by itself (color = color * color)\n 2: by intensity (color = color * luma(color))");
	// rend2's sun cvars, rend2's names and flags; off by default until the port reaches parity
	r_sunlightMode						= ri.Cvar_Get("r_sunlightMode",						"2",						CVAR_ARCHIVE | CVAR_LATCH, "Sun light from the sky material's q3gl2_sun (rend2):\n 0: off\n 1: modulates the lightmap\n 2: added as a primary light");
	ri.Cvar_CheckRange(r_sunlightMode, 0, 2, qtrue);
	r_sunShadows						= ri.Cvar_Get("r_sunShadows",						"1",						CVAR_ARCHIVE | CVAR_LATCH, "Read q3gl2_sun's map light and shadow scales and enable the sun (rend2)");
	r_forceSun							= ri.Cvar_Get("r_forceSun",							"0",						CVAR_CHEAT, "Sun shading on maps whose sky has no q3gl2_sun (from its sun, q3map_sun or q3map_sunExt), with the r_forceSun* scales in place of q3gl2_sun's (rend2)");
	r_forceSunMapLightScale				= ri.Cvar_Get("r_forceSunMapLightScale",			"1.0",						CVAR_CHEAT, "r_forceSun: scale on the world's baked light (q3gl2_sun's mapLightScale)");
	r_forceSunLightScale				= ri.Cvar_Get("r_forceSunLightScale",				"1.0",						CVAR_CHEAT, "r_forceSun: sun brightness (r_sunlightMode 2)");
	r_forceSunAmbientScale				= ri.Cvar_Get("r_forceSunAmbientScale",				"0.5",						CVAR_CHEAT, "r_forceSun: share of the sun kept in its shadows (q3gl2_sun's shadowScale)");
	// sun shadow cascades, rend2's names and defaults
	r_shadowFilter						= ri.Cvar_Get("r_shadowFilter",						"2",						CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow filter: 0 = 17-tap Poisson, 1 = 3 rotated taps, 2 = 9 rotated taps");
	ri.Cvar_CheckRange(r_shadowFilter, 0, 2, qtrue);
	r_shadowMapSize						= ri.Cvar_Get("r_shadowMapSize",					"2048",						CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow cascade size in texels");
	ri.Cvar_CheckRange(r_shadowMapSize, 256, 4096, qtrue);
	r_shadowCascadeZNear				= ri.Cvar_Get("r_shadowCascadeZNear",				"4",						CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow cascades: nearest view distance covered, game units");
	r_sunShadowFarCache					= ri.Cvar_Get("r_sunShadowFarCache",				"1",						CVAR_ARCHIVE_ND, "Sun shadow cascades: the far cascade surrounds the eye on a grid (any view direction) and keeps its static casters (the world, a render scene) until the eye leaves its cell; only moving models are drawn into it each frame");
	r_sunShadowMidCache					= ri.Cvar_Get("r_sunShadowMidCache",				"1",						CVAR_ARCHIVE_ND, "Sun shadow cascades: the middle cascade kept like the far one (r_sunShadowFarCache: around the eye on a grid, its static casters drawn only when the eye leaves its cell), so its texels are coarser (it covers every direction); 0: fitted to the view and drawn every frame. Its cache layer is made only when this is set as the renderer starts (vid_restart)");
	r_sunShadowCacheSkip				= ri.Cvar_Get("r_sunShadowCacheSkip",				"1",						CVAR_TEMP, "Sun shadow cascades: a kept cascade (r_sunShadowFarCache) whose layer still holds only its cached static casters (no moving model drawn over it since) is not copied back from the cache, nor redrawn when no moving model is in it; 0 copies it back every frame (a timing A/B; the same images)");
	r_shadowCascadeZFar					= ri.Cvar_Get("r_shadowCascadeZFar",				"4096",						CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow cascades: farthest view distance covered, game units (no sun shadows beyond it)");
	r_shadowCascadeZBias				= ri.Cvar_Get("r_shadowCascadeZBias",				"-320",						CVAR_ARCHIVE | CVAR_LATCH, "Sun shadow cascades: game units added to the two split distances between the three cascades (negative = the sharper near cascades cover less)");
	r_sunShadowDebug					= ri.Cvar_Get("r_sunShadowDebug",					"0",						CVAR_CHEAT, "Sun shadow debug view: 1 = cascade tint (red, green, blue), 2 = shadow value, 3 = cascade depth vs receiver, 4 = cascades not drawn; lighting probes 5 = light before the PI scale, 6 = its N.L, 7 = L, 8 = diffuse, 9 = ambient, 10 = N.L / N.sun, 11 = lightmap texel, 12 = stage colour");
	r_sunShadowInfo						= ri.Cvar_Get("r_sunShadowInfo",					"0",						CVAR_CHEAT, "Print the sun cascades (splits, sphere radius, world units per texel, fov, overbright) whenever the camera moves 64 units or turns");
	r_depthPrepass						= ri.Cvar_Get("r_depthPrepass",						"1",						CVAR_ARCHIVE_ND, "Draw the main view's opaque world surfaces depth-only before the lit pass, so hidden fragments (parallax, PCF, PBR) are skipped (rend2 r_depthPrepass)");
	r_sunStylePasses					= ri.Cvar_Get("r_sunStylePasses",					"0",						CVAR_ARCHIVE_ND, "1 = rend2's sun per lightmap style: each style pass of a lit surface adds the sun again, so a surface with N styles gets it N+1 times (0 = once, the base pass only)");
	r_stylePassSimple					= ri.Cvar_Get("r_stylePassSimple",					"1",						CVAR_ARCHIVE_ND, "Lightmap style passes without parallax, normal map or specular (texture x style lightmap): +0.08 ms at 4K against +0.16-0.31 for full copies, no visible difference on the bench maps; 0 = full lit copies");
	r_parallaxMapping					= ri.Cvar_Get("r_parallaxMapping",					"1",						CVAR_ARCHIVE | CVAR_LATCH, "rend2's parallax on normalHeightMap materials (parallaxDepth, parallaxBias); 2 = debug view (green, red by the offset)");
	r_parallaxFast						= ri.Cvar_Get("r_parallaxFast",						"1",						CVAR_ARCHIVE | CVAR_LATCH, "Parallax only where it can shift the image by a pixel or more, with steps scaled to that shift (0 = always 16+8 steps)");
	r_sunShadowModels					= ri.Cvar_Get("r_sunShadowModels",					"1",						CVAR_ARCHIVE_ND, "Models cast sun shadows (third-person models, not first-person or depth-hack ones)");
	r_charShadows						= ri.Cvar_Get("r_charShadows",						"1",						CVAR_ARCHIVE, "Character shadows from the light grid, darkening the baked light a character blocks: 1 = where the scene has no sun cascades, 2 = always");
	ri.Cvar_CheckRange( r_charShadows, 0, 2, qtrue );
	r_charShadowStrength				= ri.Cvar_Get("r_charShadowStrength",				"1",						CVAR_ARCHIVE_ND, "Character shadows: share of the blocked light removed (1 = all of it)");
	ri.Cvar_CheckRange( r_charShadowStrength, 0, 1, qfalse );
	r_charShadowMinElevation			= ri.Cvar_Get("r_charShadowMinElevation",			"40",						CVAR_ARCHIVE_ND, "Character shadows: lowest light elevation in degrees (the grid is often lit from the side or below)");
	ri.Cvar_CheckRange( r_charShadowMinElevation, 0, 90, qfalse );
	r_charShadowDistance				= ri.Cvar_Get("r_charShadowDistance",				"2048",						CVAR_ARCHIVE_ND, "Character shadows: camera distance at which they have faded out");
	r_charShadowSoftness				= ri.Cvar_Get("r_charShadowSoftness",				"2",						CVAR_ARCHIVE_ND, "Character shadows: smallest filter radius in atlas texels (at contact)");
	r_charShadowLightSize				= ri.Cvar_Get("r_charShadowLightSize",				"10",						CVAR_ARCHIVE_ND, "Character shadows: angular size of the light in degrees; the penumbra widens with the distance from the caster (0 = hard)");
	ri.Cvar_CheckRange( r_charShadowLightSize, 0, 45, qfalse );
	r_loadProfile						= ri.Cvar_Get("r_loadProfile",						"0",						0, "Time each map load by phase (images, pipelines, world, probes); print with loadprofile");
	r_tangentCache						= ri.Cvar_Get("r_tangentCache",						"1",						0, "Ghoul2 model tangents: 0 = computed by each user at every load, 1 = computed once and kept in cache/tangents, 2 = cache and verify against a fresh computation");
	r_pipelineCache						= ri.Cvar_Get("r_pipelineCache",					"1",						CVAR_LATCH, "Keep the Vulkan pipeline cache in cache/vk_pipelines.bin between launches");
	R_StreamRegister();
	r_modelWarmupBudget					= ri.Cvar_Get("r_modelWarmupBudget",				"6",						0, "Player model warm-up: milliseconds of loading steps a frame (one step at least; the largest is one shader's textures)");
	r_stageCovers						= ri.Cvar_Get("r_stageCovers",						"0",						CVAR_TEMP, "Set by the UI while a stage menu covers the whole screen (the player screen): the world's scenes are not drawn under it, so the stage is the frame's first view and gets the multisampled pass; honoured only while a stage scene came the frame before");
	r_stageSkinSoftNormals	= ri.Cvar_Get( "r_stageSkinSoftNormals", "1", CVAR_ARCHIVE_ND, "Stage (menus): r_charSkinSoftNormals there, whatever the play setting" );
	r_stageSkinSoftAlbedo	= ri.Cvar_Get( "r_stageSkinSoftAlbedo", "2.5", CVAR_ARCHIVE_ND, "Stage (menus): r_charSkinSoftAlbedo there, whatever the play setting" );
	r_stageRigLevel	= ri.Cvar_Get( "r_stageRigLevel", "0.9", CVAR_ARCHIVE_ND, "Stage (menus): scale on the studio rig's four lights there (r_portraitRigLevel stays the world rig's); with r_stageRigWorld 0.3 the face's tones (darkest 5 %, median, brightest 5 %) match the Cycles render of the same size; 1.15 puts the face's mids into PBR Neutral's shoulder and reads hazy" );
	r_stageRigFill	= ri.Cvar_Get( "r_stageRigFill", "0.5", CVAR_ARCHIVE_ND, "Stage (menus): scale on the studio rig's fill light (unshadowed here, so at half it stands in for the share Cycles' area light loses in the eye sockets and under the fringe)" );
	r_stageRigRim	= ri.Cvar_Get( "r_stageRigRim", "1", CVAR_ARCHIVE_ND, "Stage (menus): scale on the studio rig's rim light" );
	r_stageRigTop	= ri.Cvar_Get( "r_stageRigTop", "0.5", CVAR_ARCHIVE_ND, "Stage (menus): scale on the studio rig's top light (unshadowed, as the fill)" );
	ri.Cvar_Get( "r_xenUIRenderApi", "2", CVAR_ROM, "This renderer exports XenUI_GetRenderAPI at this version (rd-common/xenui_render.h): geometry, textures, scissor and transform (1); clip masks, layers, filters and gradients (2) for the UI module's xenui" );
	r_stageRigWorld	= ri.Cvar_Get( "r_stageRigWorld", "0.3", CVAR_ARCHIVE_ND, "Stage (menus): the flat ambient on its figure (r_portraitRigWorld stays the world rig's): Cycles' world 0.4 times the share of it a face sees there (sockets, under the brows and lashes, the body below), which r_charAO cannot resolve at a full-body figure's size" );
	r_stageSampleShading	= ri.Cvar_Get( "r_stageSampleShading", "1", CVAR_ARCHIVE_ND, "Stage (menus), with MSAA: its figure's opaque surfaces are lit at every sample, supersampling the figure only (no larger target; the world and play are unchanged; alpha-tested surfaces keep r_g2SampleShading); 0 = once per pixel" );
	ri.Cvar_Get( "r_g2TimeExactApi", "1", CVAR_ROM, "This renderer poses Ghoul2 at a caller's own time given with G2_TIME_EXACT, and draws RDF_STAGE scenes at their refdef time (the player screen's stage reads its animation ahead)" );
	ri.Cvar_Get( "r_modelWarmupApi", "2", CVAR_ROM, "This renderer exports WarmModel / WarmState (the engine holds a changed model's userinfo until it is loaded); 2: also WarmModelEx (cgame warms other players' models)" );
	r_charShadowFloor					= ri.Cvar_Get("r_charShadowFloor",					"1",						CVAR_ARCHIVE_ND, "Character shadows: 1 = take the light at the floor under the caster and reach that floor, so a jumping character keeps a steady shadow on the ground; 0 = the light at the body, fixed reach");
	r_charShadowDebug					= ri.Cvar_Get("r_charShadowDebug",					"0",						CVAR_CHEAT, "Character shadows debug: 1 = the light kept on receivers (white lit, black fully blocked), 2 = print each caster's light once a second, 3 = tiles covering the receiver (red) and occlusion (green), 4 = the first covering tile's stored depth (r/g/b >= 0.1/0.4/0.7)");
#ifdef HDR_DELUXE_LIGHTMAP
	r_deluxeMapping						= ri.Cvar_Get("r_deluxeMapping",					"1",						CVAR_ARCHIVE, "Reading deluxemaps when compiled with q3map2:\n 0: off (approximated from lightgrid)\n 1: on (compiled deluxemaps)");
	r_deluxeSpecular					= ri.Cvar_Get("r_deluxeSpecular",					"1",						CVAR_ARCHIVE, "Scale the specular response from deluxemaps");
#endif
#ifdef VK_DLIGHT_GPU
	r_dlightMethod						= ri.Cvar_Get("r_dlightMethod",						"1",						CVAR_ARCHIVE, "Dynamic light method:\n 0: CPU-based (fallback)\n 1: GPU-based (requires r_normalMapping and r_specularMapping enabled) ");
#endif
#ifdef USE_PMLIGHT
	r_dlightMode						= ri.Cvar_Get("r_dlightMode",						"2",						CVAR_ARCHIVE, "");
	ri.Cvar_CheckRange(r_dlightMode, 0, 2, qtrue);
	r_dlightScale						= ri.Cvar_Get("r_dlightScale",						"0.8",						CVAR_ARCHIVE_ND, "Dynamic light reach: scale on each light's radius (0.1..1)");
	ri.Cvar_CheckRange(r_dlightScale, 0.1f, 1, qfalse);
	r_dlightIntensity					= ri.Cvar_Get("r_dlightIntensity",					"1.0",						CVAR_ARCHIVE_ND, "Dynamic light brightness: scale on each light's colour (0.1..1)");
	ri.Cvar_CheckRange(r_dlightIntensity, 0.1f, 1, qfalse);
#endif

	r_dlightSaturation					= ri.Cvar_Get("r_dlightSaturation",					"1",						CVAR_ARCHIVE_ND, "Dynamic light colour saturation: 1 = the colour the game asks for, 0 = white at the same brightness");
	ri.Cvar_CheckRange(r_dlightSaturation, 0, 1, qfalse);

	r_roundImagesDown					= ri.Cvar_Get("r_roundImagesDown",					"1",						CVAR_ARCHIVE_ND | CVAR_LATCH, "");
	r_nomip								= ri.Cvar_Get("r_nomip",							"0",						CVAR_ARCHIVE | CVAR_LATCH, "Apply picmip only on worldspawn textures");
	ri.Cvar_CheckRange(r_nomip, 0, 1, qtrue);
#ifdef USE_VK_PBR
	r_normalMapping						= ri.Cvar_Get("r_normalMapping",					"1",						CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable normal mapping" );
	r_specularMapping					= ri.Cvar_Get("r_specularMapping",					"1",						CVAR_ARCHIVE | CVAR_LATCH, "Disable/enable specular mapping" );	
	r_baseNormalX						= ri.Cvar_Get("r_baseNormalX",						"1.0",						CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_baseNormalY						= ri.Cvar_Get("r_baseNormalY",						"1.0",						CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_baseParallax						= ri.Cvar_Get("r_baseParallax",						"0.05",						CVAR_ARCHIVE | CVAR_LATCH, "" );	
	r_pbrMaterials						= ri.Cvar_Get("r_pbrMaterials",						"1023",						CVAR_ARCHIVE | CVAR_LATCH, "Stage material features: 1 detail normal maps, 2 skin subsurface, 4 hair anisotropy, 8 cloth sheen, 16 material masks, 32 detail maps, 64 emissive maps, 128 tint masks, 256 eyes, 512 layers (0: as without the keywords)" );
	r_toon								= ri.Cvar_Get("r_toon",								"1",						CVAR_ARCHIVE | CVAR_LATCH, "Toon material stages (keyword toon): 1 cel shaded, 0 lit as without the keyword" );
	r_baseSpecular						= ri.Cvar_Get("r_baseSpecular",						"0.04",						CVAR_ARCHIVE | CVAR_LATCH, "" );
#ifdef VK_CUBEMAP
	r_cubeMapping						= ri.Cvar_Get("r_cubeMapping",						"1",						CVAR_ARCHIVE | CVAR_LATCH, "" );
	r_cubeMapBlend						= ri.Cvar_Get("r_cubeMapBlend",						"1",						CVAR_ARCHIVE_ND, "Blend each pixel's reflection across the draw's probe and its seven nearest neighbours by distance (0 = the one probe nearest the surface or model)" );
	r_cubeMapParallax					= ri.Cvar_Get("r_cubeMapParallax",					"1",						CVAR_ARCHIVE_ND, "Parallax-correct reflections so they stay fixed to the room: 1 = against a box fitted to the walls around each probe at load, 2 = against the probe's sphere (misc_cubemap \"radius\"), 0 = infinitely distant" );
	r_cubeMapDebug						= ri.Cvar_Get("r_cubeMapDebug",						"0",						CVAR_CHEAT, "Reflection probe debug view on probe-lit surfaces: 1 = blend weights (draw's probe red, its neighbours other colours by distance), 2 = flat mirror (sharpest mip), 3 = blend weights, each probe in its own colour, 4 = the probes' irradiance toward the surface normal, 5 = the ambient light after r_cubeMapDiffuse (x4)" );
	r_cubeMapRoughCut					= ri.Cvar_Get("r_cubeMapRoughCut",					"1",						CVAR_ARCHIVE_ND, "Rough surfaces (roughness 0.35 to 0.7 and up) skip the parallax correction and most of the probe blend, which their blurred reflections cannot show (0 = full work everywhere)" );
	r_cubeMapDiffuse					= ri.Cvar_Get("r_cubeMapDiffuse",					"1",						CVAR_ARCHIVE_ND, "Models lit by the light grid take the direction of their ambient light from the reflection probes' irradiance: brighter toward bright surroundings, darker toward dark ones, same average (0 = flat ambient, 0..1 blends)" );
	r_specularOcclusion					= ri.Cvar_Get("r_specularOcclusion",				"1",						CVAR_ARCHIVE_ND, "Occlude probe reflections by the material's ambient occlusion and view angle (Lagarde 2014), so creases and cavities stop glowing (0 = reflections scaled by AO at normal incidence only, as before)" );
	r_specularAA						= ri.Cvar_Get("r_specularAA",						"1",						CVAR_ARCHIVE_ND, "Geometric specular anti-aliasing: widen roughness where the shading normal changes quickly across a pixel, so detail normals and highlights sparkle less in motion (0 = off)" );
#endif
#ifdef VK_COMPUTE_NORMALMAP
	r_genNormalMaps						= ri.Cvar_Get("r_genNormalMaps",					"0",						CVAR_ARCHIVE | CVAR_LATCH, "Approximate normal maps from baked diffuse (albedo) textures" );
#endif
#endif

#ifdef USE_VK_LIGHTGRID
	r_showLightgrid						= ri.Cvar_Get("r_showLightgrid",					"0",						CVAR_ARCHIVE, "Visualize lightgrid mode:\n 0: off\n 1: ambient\n 2: directed\n 3: direction" );
	ri.Cvar_CheckRange(r_showLightgrid, LIGHTGRID_DEBUG_MODE_DISABLED, (LIGHTGRID_DEBUG_MODE_COUNT-1), qtrue);
#endif
#ifdef USE_VK_SSAO
	r_ssao								= ri.Cvar_Get("r_ssao",								 "1",						CVAR_ARCHIVE | CVAR_LATCH, "Set Ambient Occlusion mode:\n"
		"0 - disabled\n"
		"1 - SSAO\n"
		"2 - GTAO\n"
		"WIP feature");
	r_ssao_samples						= ri.Cvar_Get("r_ssao_samples",						 "6",						CVAR_ARCHIVE, "SSAO (r_ssao 1): samples per pixel (1..32)" );
	r_ssao_radius						= ri.Cvar_Get("r_ssao_radius",						 "6.0",						CVAR_ARCHIVE, "Ambient occlusion search radius, game units (r_ssao 1 and 2)" );
	r_ssao_intensity					= ri.Cvar_Get("r_ssao_intensity",					 "1.0",						CVAR_ARCHIVE, "Ambient occlusion strength, higher = darker (r_ssao 1: scale on the darkening, 2: exponent on the visibility)" );
	r_ssao_power						= ri.Cvar_Get("r_ssao_power",						 "1.5",						CVAR_ARCHIVE, "SSAO (r_ssao 1): exponent on the visibility before r_ssao_intensity (higher = darker creases)" );
	r_ssao_bias							= ri.Cvar_Get("r_ssao_bias",						 "0.15",					CVAR_ARCHIVE, "SSAO (r_ssao 1): depth difference, game units, a sample must exceed to occlude (raise if flat surfaces darken themselves)" );
	r_ssao_falloff						= ri.Cvar_Get("r_ssao_falloff",						 "0.08",					CVAR_ARCHIVE, "SSAO (r_ssao 1): pixels whose neighbours' depth changes by more than this fraction get no occlusion (depth edges; below 0.5 it is scaled by 2.5; 0 = off)" );
	r_ssao_halfRes						= ri.Cvar_Get("r_ssao_halfRes",						 "1",						CVAR_ARCHIVE | CVAR_LATCH, "Compute and blur ambient occlusion at half resolution (upsampled when it is applied)" );
	ri.Cvar_CheckRange(r_ssao_halfRes, 0, 1, qtrue);
	r_ssao_blurRadius					= ri.Cvar_Get("r_ssao_blurRadius",					 "2",						CVAR_ARCHIVE, "Ambient occlusion blur radius in AO pixels (separable, depth-aware); 0 = no blur" );
	ri.Cvar_CheckRange(r_ssao_blurRadius, 0, 8, qtrue);
	r_ssao_character					= ri.Cvar_Get("r_ssao_character",					 "0",						CVAR_ARCHIVE_ND, "Share of the ambient occlusion kept on character (Ghoul2) pixels: SSAO is multiplied over the lit image, so stacked hair cards darken each other and direct light; 1 = full AO as before" );
	ri.Cvar_CheckRange(r_ssao_character, 0, 1, qfalse);
	r_ssaoWorld							= ri.Cvar_Get("r_ssaoWorld",						 "1",						CVAR_NONE, "The world's SSAO (the post pass after the resolve: depth extract, AO, blur, blend); 0 = none, the character pass (r_charAO, r_charContactShadows) keeps running" );
	r_ssaoReuseExtract					= ri.Cvar_Get("r_ssaoReuseExtract",					 "1",						CVAR_NONE, "World SSAO: read the character pass's depth extract (taken after the depth prepass) instead of extracting again after the resolve (0 = extract again); alpha-tested and later surfaces do not occlude, and every prepass-marked model (r_charOccMask) keeps only r_ssao_character of the world's AO" );
#endif
	r_g2SampleShading					= ri.Cvar_Get("r_g2SampleShading",					"2",						CVAR_ARCHIVE_ND | CVAR_LATCH, "With MSAA: shade alpha-tested character (Ghoul2) surfaces once per sample, so hair strands thinner than a pixel get supersampled coverage instead of gaps and sparkle (costs those fragments' shading x samples; 0 = off); 2 (default) = the same per-sample alpha test as the sample mask, lit once per pixel (single-texture stages; the lighting is not supersampled); 1 = lit per sample too" );
	r_g2SampleCoverage					= ri.Cvar_Get("r_g2SampleCoverage",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Alpha-tested character stages with no sample-mask variant (two-texture stages) use the coverage ramp with hardware alpha to coverage, lit once per pixel, instead of r_g2SampleShading's per-sample shading (4x the lit shader at MSAA 4); 0 = per sample (today). r_g2SampleReport 1 names those stages" );
	r_g2SampleReport					= ri.Cvar_Get("r_g2SampleReport",					"0",						CVAR_NONE, "Test: 1 = name, once, each character stage that r_g2SampleShading still shades per sample at MSAA" );
	r_portraitRig						= ri.Cvar_Get("r_portraitRig",						"0",						CVAR_NONE, "Test lighting for character comparisons against the research Cycles renders: 1 = key, fill, rim and top dynamic lights placed around the point r_portraitRigDist ahead of the camera (the Cycles studio rig: positions, colours and power ratios), and characters lit by a flat r_portraitRigWorld ambient instead of the light grid");
	r_portraitRigDist					= ri.Cvar_Get("r_portraitRigDist",					"34",						CVAR_NONE, "Portrait rig: distance from the camera to the subject, game units");
	r_portraitRigUnits					= ri.Cvar_Get("r_portraitRigUnits",					"37",						CVAR_NONE, "Portrait rig: game units per metre (the rig is laid out in metres)");
	r_portraitRigLevel					= ri.Cvar_Get("r_portraitRigLevel",					"1",						CVAR_NONE, "Portrait rig: scale on all four lights");
	r_portraitRigWorld					= ri.Cvar_Get("r_portraitRigWorld",					"0.4",						CVAR_NONE, "Portrait rig: flat ambient on characters (Cycles world 0.8 x 0.5 grey); linear when r_charLinear is on");
	r_portraitRigSide					= ri.Cvar_Get("r_portraitRigSide",					"1",						CVAR_NONE, "Portrait rig: 1 = key on the camera's left, -1 = on its right");
	r_charLinear						= ri.Cvar_Get("r_charLinear",						"0",						CVAR_NONE, "Characters (Ghoul2 models): 1 = light in linear space (albedo, light and dynamic light colours decoded from sRGB, result encoded back), 2 = also the Khronos PBR Neutral tonemap, as the research Cycles renders; 3 = in a render scene, its light before its tone curve (after exposure) and its own tone curve after lighting, as its surfaces are toned (elsewhere 2); 0 = gamma-space lighting");
	r_charSelfShadow					= ri.Cvar_Get("r_charSelfShadow",					"0",							CVAR_NONE, "Characters shadow themselves from their light-grid light (hair, nose, brows, arms): a depth view of each nearby character along its own grid light, alpha-tested stages included, in the sun shadow image's atlas layer; the lit shader takes it off the grid's directed light. Value = strength (0 = off); needs r_shadowMapSize" );
	ri.Cvar_CheckRange( r_charSelfShadow, 0, 1, qfalse );
	r_charShadowCull					= ri.Cvar_Get("r_charShadowCull",					"0",							CVAR_ARCHIVE | CVAR_LATCH, "Test: 1 = characters keep their own face culling in the shadow maps (light-facing surfaces stored; needs the self-shadow normal offset), 0 = flipped as for the world" );
	r_charSelfShadowRays				= ri.Cvar_Get("r_charSelfShadowRays",			"4",							CVAR_NONE, "r_charSelfShadow: rays over the light's disc per pixel (1..16; half on alpha-tested stages)" );
	r_charSelfShadowSteps				= ri.Cvar_Get("r_charSelfShadowSteps",			"6",							CVAR_NONE, "r_charSelfShadow: distances tested along each ray, 0.06 u growing to 12 u (2..12)" );
	r_charSelfShadowLightSize			= ri.Cvar_Get("r_charSelfShadowLightSize",		"10",						CVAR_ARCHIVE, "r_charSelfShadow: the grid light's size in degrees (penumbra widening with the blocker's distance)" );
	r_charSelfShadowSoftness			= ri.Cvar_Get("r_charSelfShadowSoftness",		"1.5",						CVAR_ARCHIVE, "r_charSelfShadow: smallest filter radius, texels" );
	r_charSelfShadowBias				= ri.Cvar_Get("r_charSelfShadowBias",			"0.08",						CVAR_NONE, "r_charSelfShadow: depth bias, world units" );
	r_charSelfShadowOffset				= ri.Cvar_Get("r_charSelfShadowOffset",			"0.02",						CVAR_NONE, "r_charSelfShadow: receiver offset along its normal, world units (tripled at grazing light)" );
	r_charSelfShadowHairGain			= ri.Cvar_Get("r_charSelfShadowHairGain",		"1",							CVAR_NONE, "Test (r_charSelfShadow): scale on an alpha-tested character stage's alpha when it casts into the shadow maps (hashed); 1 = its own alpha" );
	r_charSelfShadowHairLod0			= ri.Cvar_Get("r_charSelfShadowHairLod0",		"0",							CVAR_NONE, "Test (r_charSelfShadow): 1 = casting alpha read from the full-size texture instead of the mip the shadow map's footprint picks" );
	r_charSkinSoftNormals				= ri.Cvar_Get("r_charSkinSoftNormals",			"0",							CVAR_NONE, "Test (skin stages): diffuse light per colour channel from a normal blended toward the smooth vertex normal, red most (the scatter colour order), so pores and fine relief fade the way light diffusing under skin blurs them; value = red's share (0 = off)" );
	r_charSkinSoftAlbedo				= ri.Cvar_Get("r_charSkinSoftAlbedo",			"0",							CVAR_NONE, "Test (skin stages): albedo red read this many mip levels blurrier, green half as many, so pores and freckles soften as under-skin scattering blurs them (0 = off)" );
	r_g2BindPose						= ri.Cvar_Get("r_g2BindPose",					"0",							CVAR_CHEAT, "Test: 1 = Ghoul2 models drawn in their stored bind pose (no animation; GPU-skinned surfaces), for comparisons with reference renders of the unposed source" );
	r_charSelfShadowDebugBox			= ri.Cvar_Get("r_charSelfShadowDebugBox",		"0.5 0.5 1",					CVAR_CHEAT, "r_charSelfShadowDebug 6: the square of the first self-shadow tile shown, \"x y size\" in tile uv (centre, side)" );
	r_charSelfShadowSlopeBias			= ri.Cvar_Get("r_charSelfShadowSlopeBias",		"0",							CVAR_NONE, "Test (r_charSelfShadow): bias in tile texels growing with the light's slope on the receiver: normal offset x (1 - N.L), depth bias x tan (light, normal) up to 4; needed with r_charShadowCull 1, whose tiles store the light-facing skin" );
	r_charSelfShadowScreen				= ri.Cvar_Get("r_charSelfShadowScreen",			"0",							CVAR_NONE, "Test (r_charSelfShadow, needs r_charAO's pass with r_charContactShadows): the nearest character's self-shadow is evaluated once per pixel of the opaque depth into the grid light's contact mask and blurred with it (r_charContactBlur), so strand patterns soften into one shadow; its opaque stages skip their own lookup, hair keeps it" );
	r_charSelfShadowHairMap				= ri.Cvar_Get("r_charSelfShadowHairMap",			"1",							CVAR_LATCH, "Test (r_charSelfShadow): the nearest character's hair shadows through a hair opacity map instead of its depth tile: the hair's filtered alpha accumulated as optical depth (with two depth moments) where it lies in front of the opaque body along the light, so the shadow is a continuous transmittance (skin: everything in front; strands: the share nearer the light). Allocates one colour image at half the tile's size" );
	r_charHairMap16 = ri.Cvar_Get( "r_charHairMap16", "0", CVAR_LATCH, "Test: 1 the hair opacity map uses its RGBA16F fallback (depth stored as -1..1 of the tile's range) on a device that blends RGBA32F; 2 the same with the old world-unit moments" );
	r_charSelfShadowHairDensity			= ri.Cvar_Get("r_charSelfShadowHairDensity",		"1",							CVAR_ARCHIVE, "r_charSelfShadowHairMap: scale on the hair's alpha before it becomes optical depth (1 = its own alpha)" );
	r_charSelfShadowHairStrandDepth		= ri.Cvar_Get("r_charSelfShadowHairStrandDepth",	"-0.25",						CVAR_ARCHIVE, "r_charSelfShadowHairMap: where a strand reads the map, world units below its own surface (negative: in front, so it never shadows itself); a pixel of hair shows strands at several depths, so a positive value darkens the hair toward its inner layers" );
	r_charSelfShadowDistance			= ri.Cvar_Get("r_charSelfShadowDistance",		"1024",						CVAR_ARCHIVE, "r_charSelfShadow: characters within this distance of the camera (faded over its last quarter)" );
	r_charSelfShadowDebug				= ri.Cvar_Get("r_charSelfShadowDebug",			"0",							CVAR_CHEAT, "r_charSelfShadow debug: 1 = the light kept (white lit, black blocked) on the characters, 2 = print each tile once a second" );
	r_charTerms							= ri.Cvar_Get("r_charTerms",						"0",							CVAR_CHEAT, "Test (characters lit in linear space, r_charLinear): bits drop lighting terms to compare against reference renders: 1 direct specular (GGX, hair strands, sheen, cornea), 2 reflections (cube maps), 4 skin wrap (plain N.L); 8 adds the reflections without decoding them from sRGB (the old behaviour)" );
	r_charAO							= ri.Cvar_Get("r_charAO",							"0",						CVAR_NONE, "Characters and models (light-grid lit): ambient occlusion computed from the opaque depth prepass before the lit pass, applied to their ambient light and reflections only (not their direct light); alpha-tested stages such as hair neither cast nor receive it. Value = strength (0 = off). Needs r_ssao and r_depthPrepass; keep r_ssao_character 0" );
	ri.Cvar_CheckRange( r_charAO, 0, 1, qfalse );
	r_charAORadius						= ri.Cvar_Get("r_charAORadius",						"8",						CVAR_NONE, "r_charAO: horizon search radius, game units (GTAO over the depth prepass)" );
	r_charAOPower						= ri.Cvar_Get("r_charAOPower",						"1",						CVAR_NONE, "r_charAO: visibility raised to this power (1 = the cosine-weighted visibility as computed)" );
	r_charAOSlices						= ri.Cvar_Get("r_charAOSlices",						"2",						CVAR_NONE, "r_charAO: horizon directions per pixel (1..8; each searched both ways)" );
	r_charAOSteps						= ri.Cvar_Get("r_charAOSteps",						"6",						CVAR_NONE, "r_charAO: depth samples along each direction (1..16)" );
	r_charContactShadows				= ri.Cvar_Get("r_charContactShadows",				"0",						CVAR_NONE, "Characters and models: screen-space contact shadows for their grid light and dynamic lights, traced this far (game units) toward each light through the opaque depth prepass (0 = off). Needs r_ssao and r_depthPrepass" );
	ri.Cvar_CheckRange( r_charContactShadows, 0, 32, qfalse );
	r_charOccHair						= ri.Cvar_Get("r_charOccHair",						"1",						CVAR_NONE, "Character occlusion: alpha-tested character stages (hair, lashes) also occlude in r_charAO and, partly (r_charHairOpacity), in the contact shadows' screen masks; 0 = only opaque surfaces (the depth prepass)" );
	r_charContactSoftness				= ri.Cvar_Get("r_charContactSoftness",				"10",						CVAR_NONE, "Contact shadows: the light's size in degrees (each pixel's ray leans within it; the pixel quad averages four)" );
	r_charContactFine					= ri.Cvar_Get("r_charContactFine",					"2",						CVAR_NONE, "Contact shadows: for the lights in the blurred screen masks (half resolution, hair included), the lit shader also traces this far (u, full resolution, opaque surfaces only) and keeps the darker; 0 = the masks alone" );
	r_charHairOpacity					= ri.Cvar_Get("r_charHairOpacity",					"0.6",						CVAR_NONE, "Contact shadows: how much of a light a hair or lash strand blocks (r_charOccHair; opaque surfaces block all of it)" );
	r_charOccHairAlpha					= ri.Cvar_Get("r_charOccHairAlpha",					"0.2",						CVAR_LATCH, "r_charOccHair: the alpha a strand's texel needs to occlude (its stage's own alpha test otherwise; latched: the occluder pipelines are built with it)" );
	r_charContactRays					= ri.Cvar_Get("r_charContactRays",					"2",						CVAR_NONE, "Contact shadows' screen masks: rays per pixel (each leaning differently within the light's size)" );
	ri.Cvar_CheckRange( r_charContactRays, 1, 16, qtrue );
	r_charContactBlur					= ri.Cvar_Get("r_charContactBlur",					"4",						CVAR_NONE, "r_charAO / contact masks: depth-aware blur radius in mask pixels (the world SSAO keeps r_ssao_blurRadius)" );
	r_charOccRect						= ri.Cvar_Get("r_charOccRect",						"1",						CVAR_NONE, "r_charAO / contact masks: compute them only inside the screen rectangle of the models that read them (bounding spheres, plus the blur's reach); full-screen when a world surface reads them; 0 = always full-screen (A/B; the image should not change); 2 = also print the rectangle, or why the pass is full-screen, once a second; 3 = also zero the masks outside it (check: a reader outside shows black)" );
	r_charOccRectMargin					= ri.Cvar_Get("r_charOccRectMargin",				"24",						CVAR_NONE, "r_charOccRect: units added around a Ghoul2 model's posed joints for its skin, cloth and hair cards" );
	r_charFast							= ri.Cvar_Get("r_charFast",							"3",						CVAR_NONE, "Character look work skipped where it cannot change the image (bits; 0 = all off, for A/B): 1 = dynamic lights' contact rays where no pixel of the 2x2 quad is lit by the light, 2 = the grid light's contact ray (whole quad), self-shadow and hair map where the surface faces away from it" );
	r_charFill							= ri.Cvar_Get("r_charFill",							"0.5",						CVAR_NONE, "Characters (Ghoul2 models lit by the light grid): a fill light from 50 deg off the camera axis, on the other side from the grid light, level; its colour this share of the grid's whole light (ambient + directed) at the model, occluded like the ambient. Default 0.5 (measured with r_charLinear 2); 0 = off" );
	r_charRim							= ri.Cvar_Get("r_charRim",							"0.6",						CVAR_NONE, "Characters (Ghoul2 models lit by the light grid): a rim light from behind them (180 to 205 deg from the camera, toward the grid light's side, 20 deg up); its colour this share of the grid's whole light at the model, unshadowed. Default 0.6; 0 = off" );
	// character review fixes that change pixels: on by default; 0 = the old
	// look, for A/B (docs/review/2026-11-09-character-rendering.md)
	r_fixG2SceneTime = ri.Cvar_Get( "r_fixG2SceneTime", "1", CVAR_ARCHIVE_ND, "The back end poses characters at the time their scene was submitted (with a menu over a live game it used the menu's clock)" );
	r_g2LodStats = ri.Cvar_Get( "r_g2LodStats", "0", CVAR_NONE, "1 prints G2LOD every 120 frames: Ghoul2 draws whose LOD differs from the one the frame's first view picked for that model (main and shadow views apart)" );
	r_fixG2SceneLod = ri.Cvar_Get( "r_fixG2SceneLod", "1", CVAR_ARCHIVE_ND, "A Ghoul2 model's LOD is measured from the scene's camera, so shadow views draw the LOD the main view draws" );
	r_fmaDebug = ri.Cvar_Get( "r_fmaDebug", "0", CVAR_NONE, "FMA body debug views: 1 the ramp input, 2 mask bias, 3 baked vertex-colour alpha, 4 half-Lambert, 5 lit/shade selector, 6 specular" );
	r_fixFmaShadows = ri.Cvar_Get( "r_fixFmaShadows", "0", CVAR_ARCHIVE_ND, "FMA stages take the grid light's shadows (contact, the character's own shadow, its hair map), moving their lit/shade step as toon stages do: 1 the body only (faces keep their SDF light), 2 body and face; 0 = none, as the source game" );
	r_charAmbientFloor					= ri.Cvar_Get("r_charAmbientFloor",					"0.45",						CVAR_NONE, "Characters (Ghoul2 models lit by the light grid): their ambient light is at least this share of the grid's whole light (ambient + directed) at the model, in its colour, so the shade side and what their own hat or hair shadows keep some light; in linear units with r_charLinear. Default 0.45; 0 = off" );
	r_charOccHalfDepth					= ri.Cvar_Get("r_charOccHalfDepth",					"1",						CVAR_NONE, "Character occlusion pass (r_charAO, r_charContactShadows): read a half-size copy of the depth (one texel of each 2x2) instead of the full-size one, a quarter of the memory per step (its masks are half size already); the lit pass's own fine rays keep full size. Default 1 (2026-10-20, Install 25); 0 = full size" );
	r_charOccMask						= ri.Cvar_Get("r_charOccMask",						"1",						CVAR_NONE, "Character occlusion pass: only near model pixels (the depth prepass marks them in the stencil); elsewhere the masks are left clear, and the blur ignores them. 0 = every pixel of r_charOccRect's rectangle" );
	r_charAORadiusCap					= ri.Cvar_Get("r_charAORadiusCap",					"128",						CVAR_NONE, "r_charAO: cap on the GTAO screen radius in occlusion-target pixels (8..255; the AO radius of a close character reaches it, and wider taps miss the cache)" );
	ri.Cvar_CheckRange( r_charContactBlur, 0, 8, qtrue );
	r_charContactGridShare				= ri.Cvar_Get("r_charContactGridShare",				"1",						CVAR_NONE, "Contact shadows on the light grid's light: 1 = scaled by its directed share of the light (directed / (directed + ambient)), the rest of the grid light counting as coming from everywhere; 0 = full strength" );
	r_charContactMask					= ri.Cvar_Get("r_charContactMask",					"1",						CVAR_NONE, "Contact shadows: 1 = up to 3 lights through the blurred screen masks, 0 = every light traced in the lit shader (A/B)" );
	r_charContactThickness				= ri.Cvar_Get("r_charContactThickness",				"2",						CVAR_NONE, "Contact shadows: how thick a surface in the depth prepass is assumed to be (game units)" );
	r_charContactSteps					= ri.Cvar_Get("r_charContactSteps",					"12",						CVAR_NONE, "Contact shadows: depth samples per light along the ray" );
	ri.Cvar_CheckRange( r_charContactSteps, 2, 32, qtrue );
	r_charOcclusionDebug				= ri.Cvar_Get("r_charOcclusionDebug",				"0",						CVAR_CHEAT, "Character occlusion debug on light-grid lit models: 1 = r_charAO's occlusion, 2 = the grid light's contact shadow, 3 = the first dynamic light's, 4 = depth reprojection error (green = the pixel finds its own depth)" );
	r_renderWidth						= ri.Cvar_Get("r_renderWidth",						"800",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Render width in pixels while r_renderScale is on (scaled to the window)");
	r_renderHeight						= ri.Cvar_Get("r_renderHeight",						"600",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Render height in pixels while r_renderScale is on (scaled to the window)");
	r_renderScale						= ri.Cvar_Get("r_renderScale",						"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Scaling mode to be used with custom render resolution:\n"
		" 0 - disabled\n"
		" 1 - nearest filtering, stretch to full size\n"
		" 2 - nearest filtering, preserve aspect ratio (black bars on sides)\n"
		" 3 - linear filtering, stretch to full size\n"
		" 4 - linear filtering, preserve aspect ratio (black bars on sides)\n");
	ri.Cvar_CheckRange(r_renderScale, 0, 4, qtrue);
	r_ignorehwgamma						= ri.Cvar_Get("r_ignorehwgamma",					"0",						CVAR_ARCHIVE_ND | CVAR_LATCH, "Overrides hardware gamma capabilities");
	ri.Cvar_CheckRange(r_ignorehwgamma, 0, 1, qtrue);


/*
Ghoul2 Insert Start
*/
#ifdef _DEBUG
	r_noPrecacheGLA						= ri.Cvar_Get( "r_noPrecacheGLA",					"0",						CVAR_CHEAT, "" );
#endif
	r_noServerGhoul2					= ri.Cvar_Get( "r_noserverghoul2",					"0",						CVAR_CHEAT, "" );
	r_Ghoul2AnimSmooth					= ri.Cvar_Get( "r_ghoul2animsmooth",				"0.3",						CVAR_NONE, "" );
	r_Ghoul2UnSqashAfterSmooth			= ri.Cvar_Get( "r_ghoul2unsqashaftersmooth",		"1",						CVAR_NONE, "" );
	r_g2Blend							= ri.Cvar_Get( "r_g2Blend",						"1",						CVAR_ARCHIVE, "xenjk: Ghoul2 bone blends drawn in rotation space (slerp about each joint); 0 = stock matrix lerp. Draw only: bolts, collision and the server are unchanged" );
	r_g2Joints							= ri.Cvar_Get( "r_g2Joints",						"1",						CVAR_ARCHIVE, "xenjk: Ghoul2 models that ship <model>.joints are bent about their own joints instead of the skeleton's (Kyle's); 0 = stock. Client side: drawing, bolts and client traces; the server is unchanged" );
	r_g2SmoothFix						= ri.Cvar_Get( "r_g2SmoothFix",					"1",							CVAR_NONE, "xenjk: 1 the back end reuses the front end's bone transform of the same frame, scene, time and root (one smoothing pass a frame for every drawn bone, as rd-vanilla; r_ghoul2animsmooth and the ragdoll factors work again), and the render-traversal flag is cleared after the back end (listen-server and cgame bolt queries evaluate as non-render, as rd-vanilla); 2 as 1 and -1 as 0, and both print G2SMOOTH (bones the smoothing blended and copied) every 120 frames" );
	r_g2BlendCurve						= ri.Cvar_Get( "r_g2BlendCurve",					"2",						CVAR_ARCHIVE, "xenjk: animation-to-animation blend curve with r_g2Blend 1: 0 linear (stock), 1 ease out, 2 smoothstep into looping animations and ease out otherwise; blend lengths unchanged" );
	r_g2BlendStats						= ri.Cvar_Get( "r_g2BlendStats",					"0",						CVAR_NONE, "xenjk: print G2BS lines (stock vs rotation-space orthonormality, pelvis and right hand) per drawn humanoid" );
	r_g2BlendStatsModel					= ri.Cvar_Get( "r_g2BlendStatsModel",				"",						CVAR_NONE, "xenjk: r_g2BlendStats only for models whose file name contains this" );
	broadsword							= ri.Cvar_Get( "broadsword",						"0",						CVAR_ARCHIVE_ND, "" );
	broadsword_kickbones				= ri.Cvar_Get( "broadsword_kickbones",				"1",						CVAR_NONE, "" );
	broadsword_kickorigin				= ri.Cvar_Get( "broadsword_kickorigin",				"1",						CVAR_NONE, "" );
	broadsword_dontstopanim				= ri.Cvar_Get( "broadsword_dontstopanim",			"0",						CVAR_NONE, "" );
	broadsword_waitforshot				= ri.Cvar_Get( "broadsword_waitforshot",			"0",						CVAR_NONE, "" );
	broadsword_playflop					= ri.Cvar_Get( "broadsword_playflop",				"1",						CVAR_NONE, "" );
	broadsword_smallbbox				= ri.Cvar_Get( "broadsword_smallbbox",				"0",						CVAR_NONE, "" );
	broadsword_extra1					= ri.Cvar_Get( "broadsword_extra1",					"0",						CVAR_NONE, "" );
	broadsword_extra2					= ri.Cvar_Get( "broadsword_extra2",					"0",						CVAR_NONE, "" );
	broadsword_effcorr					= ri.Cvar_Get( "broadsword_effcorr",				"1",						CVAR_NONE, "" );
	broadsword_ragtobase				= ri.Cvar_Get( "broadsword_ragtobase",				"2",						CVAR_NONE, "" );
	broadsword_dircap					= ri.Cvar_Get( "broadsword_dircap",					"64",						CVAR_NONE, "" );
/*
Ghoul2 Insert End
*/
	r_modelpoolmegs = ri.Cvar_Get("r_modelpoolmegs", "20", CVAR_ARCHIVE, "" );
	if (ri.Sys_LowPhysicalMemory() )
		ri.Cvar_Set("r_modelpoolmegs", "0");

	r_aviMotionJpegQuality				= ri.Cvar_Get( "r_aviMotionJpegQuality",			"100",						CVAR_ARCHIVE_ND, "" );
	r_screenshotJpegQuality				= ri.Cvar_Get( "r_screenshotJpegQuality",			"100",						CVAR_ARCHIVE_ND, "" );

	ri.Cvar_CheckRange( r_aviMotionJpegQuality, 10, 100, qtrue );
	r_videoAsync						= ri.Cvar_Get( "r_videoAsync",						"1",						CVAR_ARCHIVE_ND, "Video capture copies each frame into a buffer ring and reads it a frame later instead of waiting for the GPU; 0 reads at once" );
	ri.Cvar_CheckRange( r_screenshotJpegQuality, 10, 100, qtrue );

	for ( size_t i = 0; i < numCommands; i++ )
		ri.Cmd_AddCommand( commands[i].cmd, commands[i].func, "" );

	R_SmokeRegister();
	R_SceneRegister();
}

/*
===============
R_Init
===============
*/
extern void R_InitWorldEffects( void ); //tr_WorldEffects.cpp
void R_Init( void ) {
	int i;
	byte *ptr;

	vk_debug("----- R_Init -----\n" );
	ri.Printf(PRINT_ALL, "----- R_Init -----\n");
	// clear all our internal state
	Com_Memset( &tr, 0, sizeof( tr ) );
	Com_Memset( &backEnd, 0, sizeof( backEnd ) );
	Com_Memset( &tess, 0, sizeof( tess ) );
	//Com_Memset( &glState, 0, sizeof( glState ) );

#ifndef FINAL_BUILD
	if ( (intptr_t)tess.xyz & 15 ) {
		ri.Printf(PRINT_WARNING, "tess.xyz not 16 byte aligned\n");
	}
#endif
	//
	// init function tables
	//
	for (i = 0; i < FUNCTABLE_SIZE; i++) {
#if 0
		if (i == 0) {
			tr.sinTable[i] = EPSILON;
		}
		else if (i == (FUNCTABLE_SIZE - 1)) {
			tr.sinTable[i] = -EPSILON;
		}
		else {
			tr.sinTable[i] = sin(DEG2RAD(i * 360.0f / ((float)(FUNCTABLE_SIZE - 1))));
		}
#else
		tr.sinTable[i] = sin( DEG2RAD( i * 360.0f / FUNCTABLE_SIZE ) + 0.0001f );
#endif
		tr.squareTable[i] = (i < FUNCTABLE_SIZE / 2) ? 1.0f : -1.0f;
		if (i == 0) {
			tr.sawToothTable[i] = EPSILON;
		}
		else {
			tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		}
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];
		if (i < FUNCTABLE_SIZE / 2) {
			if (i < FUNCTABLE_SIZE / 4) {
				if (i == 0) {
					tr.triangleTable[i] = EPSILON;
				}
				else {
					tr.triangleTable[i] = (float)i / (FUNCTABLE_SIZE / 4);
				}
			}
			else {
				tr.triangleTable[i] = 1.0f - tr.triangleTable[i - FUNCTABLE_SIZE / 4];
			}
		}
		else {
			tr.triangleTable[i] = -tr.triangleTable[i - FUNCTABLE_SIZE / 2];
		}
	}

	R_InitFogTable();
	R_ImageLoader_Init();
	R_NoiseInit();
	R_Register();

	max_polys = Q_min( r_maxpolys->integer, DEFAULT_MAX_POLYS );
	max_polyverts = Q_min( r_maxpolyverts->integer, DEFAULT_MAX_POLYVERTS );

	ptr = (byte *)Hunk_Alloc( 
		sizeof( *backEndData ) + 
		sizeof(srfPoly_t) * max_polys + 
		sizeof(polyVert_t) * max_polyverts +
		sizeof(Allocator) +
		PER_FRAME_MEMORY_BYTES,
		h_low);
	backEndData = (backEndData_t *)ptr;

	ptr = (byte *)(backEndData + 1);

	backEndData->polys = (srfPoly_t *)ptr;
	ptr += sizeof(*backEndData->polys) * max_polys;

	backEndData->polyVerts = (polyVert_t *)ptr;
	ptr += sizeof(*backEndData->polyVerts) * max_polyverts;

	backEndData->perFrameMemory = new(ptr) Allocator(ptr + sizeof(*backEndData->perFrameMemory), PER_FRAME_MEMORY_BYTES);
	
	R_InitNextFrame();

	for(i = 0; i < MAX_LIGHT_STYLES; i++)
	{
		RE_SetLightStyle(i, -1);
	}

	R_InitImagesPool();

	vk_create_window();		// Vulkan

#ifdef USE_VBO
	vk_release_vbo();
	vk_release_model_vbo();
#endif

	R_Set2DRatio();
	R_InitImages();	

#ifdef _G2_GORE
	R_CreateGoreVBO();
#endif
	vk_create_pipelines();	// Vulkan

#ifdef VK_PBR_BRDFLUT
	vk_create_brfdlut();
#endif

	vk_set_clearcolor();

	R_InitShaders(qfalse);
	R_InitSkins();
	R_InitFonts();
	R_ModelInit();
	R_InitDecals();
	R_InitWorldEffects();
	RestoreGhoul2InfoArray();

	vk_debug("----- finished R_Init -----\n" );
}


// This need some tlc
/*
===============
RE_Shutdown
===============
*/
void RE_Shutdown( qboolean destroyWindow, qboolean restarting ) {
	R_XenUIShutdown();
	vk_debug("RE_Shutdown( %i, %i )\n", destroyWindow, restarting);

	for (size_t i = 0; i < numCommands; i++)
		ri.Cmd_RemoveCommand(commands[i].cmd);

	R_ShutdownWorldEffects();
	R_ClothShutdown();
	R_HairShutdown();
	R_MeshJobsShutdown();	// before the models they build go
	R_TangentCacheClear();
	R_WarmClear();
	vk_save_pipeline_cache();	// every map change too: a crash keeps what the earlier maps compiled
	R_SmokeShutdown();
	vk_sun_shadow_cache_reset();
	R_SceneShutdown();
	R_ShutdownFonts();

	// contains vulkan resources/state, reinitialized on a map change.
	//if (tr.registered) {

		if (destroyWindow){
			//vk_delete_textures();

			if (restarting)
				SaveGhoul2InfoArray();
		}

		vk_delete_textures();
		vk_release_resources();
	//}

	//vk_release_resources(); not merged yet (https://github.com/ec-/Quake3e/commit/d31b84ebf2ab702686e98dff40b7673473026b30)

	if (destroyWindow) {
		vk_shutdown();
		Com_Memset(&glState, 0, sizeof(glState));

		if (destroyWindow && !restarting) {
			ri.VK_destroyWindow();
			Com_Memset(&glConfig, 0, sizeof(glConfig));
		}
	}

	tr.registered = qfalse;
	tr.inited = qfalse;
}

/*
=============
RE_EndRegistration

Touch all images to make sure they are resident
=============
*/
void RE_EndRegistration( void ) {
	vk_wait_idle();

	// the image scratch buffers (64 MB upload and 32 MB resample at least, more after a large texture) go
	// back once the map's images are loaded; a load during play grows the one it needs again
	R_DestroyImageScratch();

	// command buffer is not in recording state at this stage
	// so we can't issue RB_ShowImages() here.
	// moved to RB_SwapBuffers
}

void RE_GetLightStyle( int style, color4ub_t color )
{
	if (style >= MAX_LIGHT_STYLES)
	{
	    Com_Error( ERR_FATAL, "RE_GetLightStyle: %d is out of range", (int)style );
		return;
	}

	byteAlias_t *baDest = (byteAlias_t *)&color, 
				*baSource = (byteAlias_t *)&styleColors[style];
	baDest->i = baSource->i;
}

void RE_SetLightStyle( int style, int color )
{
	if (style >= MAX_LIGHT_STYLES)
	{
	    Com_Error( ERR_FATAL, "RE_SetLightStyle: %d is out of range", (int)style );
		return;
	}

	byteAlias_t *ba = (byteAlias_t *)&styleColors[style];
	if ( ba->i != color) {
		ba->i = color;
	}
}

static void SetRangedFog( float range ) { tr.rangedFog = range; }

extern qboolean gG2_GBMNoReconstruct;
extern qboolean gG2_GBMUseSPMethod;
static void G2API_BoltMatrixReconstruction( qboolean reconstruct ) { gG2_GBMNoReconstruct = (qboolean)!reconstruct; }
static void G2API_BoltMatrixSPMethod( qboolean spMethod ) { gG2_GBMUseSPMethod = spMethod; }

//extern float tr_distortionAlpha; //opaque
//extern float tr_distortionStretch; //no stretch override
//extern qboolean tr_distortionPrePost; //capture before postrender phase?
//extern qboolean tr_distortionNegate; //negative blend mode
static void SetRefractionProperties( float distortionAlpha, float distortionStretch, qboolean distortionPrePost, qboolean distortionNegate ) {
	//tr_distortionAlpha = distortionAlpha;
	//tr_distortionStretch = distortionStretch;
	//tr_distortionPrePost = distortionPrePost;
	//tr_distortionNegate = distortionNegate;
}

static float GetDistanceCull( void ) { return tr.distanceCull; }

static void GetRealRes( int *w, int *h ) {
	*w = glConfig.vidWidth;
	*h = glConfig.vidHeight;
}

extern void R_SVModelInit( void ); //tr_model.cpp
extern void R_AutomapElevationAdjustment( float newHeight ); //tr_world.cpp
extern qboolean R_InitializeWireframeAutomap( void ); //tr_world.cpp

extern qhandle_t RE_RegisterServerSkin( const char *name );

void C_LevelLoadBegin(const char *psMapName, ForceReload_e eForceReload)
{
	static char sPrevMapName[MAX_QPATH]={0};
	bool bDeleteModels = eForceReload == eForceReload_MODELS || eForceReload == eForceReload_ALL;

	// the mesh worker and the warm-up read GLMs the cache may free below (devmapmdl, sv_pure's non-pure files)
	R_MeshJobsShutdown();
	R_WarmClear();
	R_MeshJobsLevelLoad( qtrue );	// models registered until the map's first world scene build at once, as before

	if( bDeleteModels )
		CModelCache->DeleteAll();
	else if( ri.Cvar_VariableIntegerValue( "sv_pure" ) )
		CModelCache->DumpNonPure();
	R_TangentCacheForgetPointers();

	tr.numBSPModels = 0;

	/* If we're switching to the same level, don't increment current level */
	if (Q_stricmp( psMapName,sPrevMapName ))
	{
		Q_strncpyz( sPrevMapName, psMapName, sizeof(sPrevMapName) );
		tr.currentLevel++;
	}
}

int C_GetLevel( void )
{
	return tr.currentLevel;
}

void C_LevelLoadEnd( void )
{
	CModelCache->LevelLoadEnd( qfalse );
	R_TangentCacheForgetPointers();	// LevelLoadEnd frees the models the level did not register
	ri.SND_RegisterAudio_LevelLoadEnd( qfalse );
	ri.S_RestartMusic();
}

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
extern "C" {
Q_EXPORT refexport_t* QDECL GetRefAPI( int apiVersion, refimport_t *rimp ) {
	static refexport_t re;

	assert( rimp );
	// Copy only what this engine's table has: the appended entries below are present when their cvar says so, and an
	// older engine passed a shorter table, so copying the whole struct would read past its end.
	memset( &ri, 0, sizeof( ri ) );
	memcpy( &ri, rimp, offsetof( refimport_t, VK_IsMinimized ) );
	if ( ri.Cvar_VariableIntegerValue( "win_vulkan" ) ) {
		memcpy( (char *)&ri + offsetof( refimport_t, VK_IsMinimized ), (const char *)rimp + offsetof( refimport_t, VK_IsMinimized ),
			offsetof( refimport_t, CL_SetVideoCaptureHooks ) - offsetof( refimport_t, VK_IsMinimized ) );
	}
	if ( ri.Cvar_VariableIntegerValue( "cl_videoCaptureHooks" ) ) {
		ri.CL_SetVideoCaptureHooks = rimp->CL_SetVideoCaptureHooks;
	}
	if ( ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) ) {
		ri.FS_PrefetchDir = rimp->FS_PrefetchDir;
		ri.FS_PrefetchDone = rimp->FS_PrefetchDone;
	}
	if ( ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) >= 2 ) {
		memcpy( (char *)&ri + offsetof( refimport_t, FS_PrefetchDirEx ), (const char *)rimp + offsetof( refimport_t, FS_PrefetchDirEx ),
			offsetof( refimport_t, FS_ReadHomeFile ) - offsetof( refimport_t, FS_PrefetchDirEx ) );
	}
	if ( ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) >= 3 ) {
		ri.FS_ReadHomeFile = rimp->FS_ReadHomeFile;
	}

	memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		vk_debug("Mismatched REF_API_VERSION: expected %i, got %i\n", REF_API_VERSION, apiVersion );
		return NULL;
	}

#ifdef USE_OPENJK
	// The Vulkan window imports were appended to refimport_t without a version change (older renderers keep working
	// with newer engines). An engine that has them says so with win_vulkan; an older one passed a shorter table, so
	// refuse it instead of calling past its end.
	if ( !ri.Cvar_VariableIntegerValue( "win_vulkan" ) || !ri.VK_IsMinimized || !ri.VK_GetInstanceProcAddress
		|| !ri.VK_createSurfaceImpl || !ri.VK_destroyWindow ) {
		ri.Printf( PRINT_ALL, "rd-vulkan: this engine has no Vulkan window support (win_vulkan); use another renderer\n" );
		return NULL;
	}
#endif

	// deferred video capture (vk_video.cpp): an engine with the hook entry says so with cl_videoCaptureHooks
	if ( ri.Cvar_VariableIntegerValue( "cl_videoCaptureHooks" ) && ri.CL_SetVideoCaptureHooks ) {
		static const videoCaptureHooks_t videoHooks = { vk_video_flush, vk_video_capture_size };
		ri.CL_SetVideoCaptureHooks( &videoHooks );
		vk_video_client_flushes = qtrue;
	} else {
		vk_video_client_flushes = qfalse;
	}

	// the RE_ functions are Renderer Entry points

	re.Shutdown = RE_Shutdown;
	re.BeginRegistration					= RE_BeginRegistration;
	re.RegisterModel						= RE_RegisterModel;
	re.RegisterServerModel					= RE_RegisterServerModel;
	re.RegisterSkin							= RE_RegisterSkin;
	re.RegisterServerSkin					= RE_RegisterServerSkin;
	re.RegisterShader						= RE_RegisterShader;
	re.RegisterShaderNoMip					= RE_RegisterShaderNoMip;
	re.ShaderNameFromIndex					= RE_ShaderNameFromIndex;
	re.LoadWorld							= RE_LoadWorldMap;
	re.SetWorldVisData						= RE_SetWorldVisData;
	re.EndRegistration						= RE_EndRegistration;
	re.BeginFrame							= RE_BeginFrame;
	re.EndFrame								= RE_EndFrame;
	re.MarkFragments						= R_MarkFragments;
	re.LerpTag								= R_LerpTag;
	re.ModelBounds							= R_ModelBounds;
	re.DrawRotatePic						= RE_RotatePic;
	re.DrawRotatePic2						= RE_RotatePic2;
	re.ClearScene							= RE_ClearScene;
	re.ClearDecals							= RE_ClearDecals;
	re.AddRefEntityToScene					= RE_AddRefEntityToScene;
	re.AddMiniRefEntityToScene				= RE_AddMiniRefEntityToScene;
	re.AddPolyToScene						= RE_AddPolyToScene;
	re.AddDecalToScene						= RE_AddDecalToScene;
	re.LightForPoint						= R_LightForPoint;
	re.AddLightToScene						= RE_AddLightToScene;
	re.AddAdditiveLightToScene				= RE_AddAdditiveLightToScene;

	re.RenderScene							= RE_RenderScene;
	re.SetColor								= RE_SetColor;
	re.DrawStretchPic						= RE_StretchPic;
	re.DrawStretchRaw						= RE_StretchRaw;
	re.UploadCinematic						= RE_UploadCinematic;

	re.RegisterFont							= RE_RegisterFont;
	re.Font_StrLenPixels					= RE_Font_StrLenPixels;
	re.Font_StrLenChars						= RE_Font_StrLenChars;
	re.Font_HeightPixels					= RE_Font_HeightPixels;
	re.Font_DrawString						= RE_Font_DrawString;
	re.Language_IsAsian						= Language_IsAsian;
	re.Language_UsesSpaces					= Language_UsesSpaces;
	re.AnyLanguage_ReadCharFromString		= AnyLanguage_ReadCharFromString;

	re.RemapShader							= R_RemapShader;
	re.GetEntityToken						= R_GetEntityToken;
	re.inPVS								= R_inPVS;
	re.GetLightStyle						= RE_GetLightStyle;
	re.SetLightStyle						= RE_SetLightStyle;
	re.GetBModelVerts						= RE_GetBModelVerts;

	// missing from 1.01
	re.SetRangedFog							= SetRangedFog;
	re.SetRefractionProperties				= SetRefractionProperties;
	re.GetDistanceCull						= GetDistanceCull;
	re.GetRealRes							= GetRealRes;
	re.AutomapElevationAdjustment			= R_AutomapElevationAdjustment; //tr_world.cpp
	re.InitializeWireframeAutomap			= R_InitializeWireframeAutomap; //tr_world.cpp
	re.AddWeatherZone						= RE_AddWeatherZone;
	re.WorldEffectCommand					= RE_WorldEffectCommand;
	re.RegisterMedia_LevelLoadBegin			= C_LevelLoadBegin;
	re.RegisterMedia_LevelLoadEnd			= C_LevelLoadEnd;
	re.RegisterMedia_GetLevel				= C_GetLevel;
	re.RegisterImages_LevelLoadEnd			= C_Images_LevelLoadEnd;
	re.RegisterModels_LevelLoadEnd			= C_Models_LevelLoadEnd;

	// AVI recording
	re.TakeVideoFrame						= RE_TakeVideoFrame;

	// G2 stuff
	re.InitSkins							= R_InitSkins;
	re.InitShaders							= R_InitShaders;
	re.SVModelInit							= R_SVModelInit;
	re.HunkClearCrap						= RE_HunkClearCrap;

	// G2API
	re.G2API_AddBolt						= G2API_AddBolt;
	re.G2API_AddBoltSurfNum					= G2API_AddBoltSurfNum;
	re.G2API_AddSurface						= G2API_AddSurface;
	re.G2API_AnimateG2ModelsRag				= G2API_AnimateG2ModelsRag;
	re.G2API_AttachEnt						= G2API_AttachEnt;
	re.G2API_AttachG2Model					= G2API_AttachG2Model;
	re.G2API_AttachInstanceToEntNum			= G2API_AttachInstanceToEntNum;
	re.G2API_AbsurdSmoothing				= G2API_AbsurdSmoothing;
	re.G2API_BoltMatrixReconstruction		= G2API_BoltMatrixReconstruction;
	re.G2API_BoltMatrixSPMethod				= G2API_BoltMatrixSPMethod;
	re.G2API_CleanEntAttachments			= G2API_CleanEntAttachments;
	re.G2API_CleanGhoul2Models				= G2API_CleanGhoul2Models;
	re.G2API_ClearAttachedInstance			= G2API_ClearAttachedInstance;
	re.G2API_CollisionDetect				= G2API_CollisionDetect;
	re.G2API_CollisionDetectCache			= G2API_CollisionDetectCache;
	re.G2API_CopyGhoul2Instance				= G2API_CopyGhoul2Instance;
	re.G2API_CopySpecificG2Model			= G2API_CopySpecificG2Model;
	re.G2API_DetachG2Model					= G2API_DetachG2Model;
	re.G2API_DoesBoneExist					= G2API_DoesBoneExist;
	re.G2API_DuplicateGhoul2Instance		= G2API_DuplicateGhoul2Instance;
	re.G2API_FreeSaveBuffer					= G2API_FreeSaveBuffer;
	re.G2API_GetAnimFileName				= G2API_GetAnimFileName;
	re.G2API_GetAnimFileNameIndex			= G2API_GetAnimFileNameIndex;
	re.G2API_GetAnimRange					= G2API_GetAnimRange;
	re.G2API_GetBoltMatrix					= G2API_GetBoltMatrix;
	re.G2API_GetBoneAnim					= G2API_GetBoneAnim;
	re.G2API_GetBoneIndex					= G2API_GetBoneIndex;
	re.G2API_GetGhoul2ModelFlags			= G2API_GetGhoul2ModelFlags;
	re.G2API_GetGLAName						= G2API_GetGLAName;
	re.G2API_GetModelName					= G2API_GetModelName;
	re.G2API_GetParentSurface				= G2API_GetParentSurface;
	re.G2API_GetRagBonePos					= G2API_GetRagBonePos;
	re.G2API_GetSurfaceIndex				= G2API_GetSurfaceIndex;
	re.G2API_GetSurfaceName					= G2API_GetSurfaceName;
	re.G2API_GetSurfaceOnOff				= G2API_GetSurfaceOnOff;
	re.G2API_GetSurfaceRenderStatus			= G2API_GetSurfaceRenderStatus;
	re.G2API_GetTime						= G2API_GetTime;
	re.G2API_Ghoul2Size						= G2API_Ghoul2Size;
	re.G2API_GiveMeVectorFromMatrix			= G2API_GiveMeVectorFromMatrix;
	re.G2API_HasGhoul2ModelOnIndex			= G2API_HasGhoul2ModelOnIndex;
	re.G2API_HaveWeGhoul2Models				= G2API_HaveWeGhoul2Models;
	re.G2API_IKMove							= G2API_IKMove;
	re.G2API_InitGhoul2Model				= G2API_InitGhoul2Model;
	re.G2API_IsGhoul2InfovValid				= G2API_IsGhoul2InfovValid;
	re.G2API_IsPaused						= G2API_IsPaused;
	re.G2API_ListBones						= G2API_ListBones;
	re.G2API_ListSurfaces					= G2API_ListSurfaces;
	re.G2API_LoadGhoul2Models				= G2API_LoadGhoul2Models;
	re.G2API_LoadSaveCodeDestructGhoul2Info	= G2API_LoadSaveCodeDestructGhoul2Info;
	re.G2API_OverrideServerWithClientData	= G2API_OverrideServerWithClientData;
	re.G2API_PauseBoneAnim					= G2API_PauseBoneAnim;
	re.G2API_PrecacheGhoul2Model			= G2API_PrecacheGhoul2Model;
	re.G2API_RagEffectorGoal				= G2API_RagEffectorGoal;
	re.G2API_RagEffectorKick				= G2API_RagEffectorKick;
	re.G2API_RagForceSolve					= G2API_RagForceSolve;
	re.G2API_RagPCJConstraint				= G2API_RagPCJConstraint;
	re.G2API_RagPCJGradientSpeed			= G2API_RagPCJGradientSpeed;
	re.G2API_RemoveBolt						= G2API_RemoveBolt;
	re.G2API_RemoveBone						= G2API_RemoveBone;
	re.G2API_RemoveGhoul2Model				= G2API_RemoveGhoul2Model;
	re.G2API_RemoveGhoul2Models				= G2API_RemoveGhoul2Models;
	re.G2API_RemoveSurface					= G2API_RemoveSurface;
	re.G2API_ResetRagDoll					= G2API_ResetRagDoll;
	re.G2API_SaveGhoul2Models				= G2API_SaveGhoul2Models;
	re.G2API_SetBoltInfo					= G2API_SetBoltInfo;
	re.G2API_SetBoneAngles					= G2API_SetBoneAngles;
	re.G2API_SetBoneAnglesIndex				= G2API_SetBoneAnglesIndex;
	re.G2API_SetBoneAnglesMatrix			= G2API_SetBoneAnglesMatrix;
	re.G2API_SetBoneAnglesMatrixIndex		= G2API_SetBoneAnglesMatrixIndex;
	re.G2API_SetBoneAnim					= G2API_SetBoneAnim;
	re.G2API_SetBoneAnimIndex				= G2API_SetBoneAnimIndex;
	re.G2API_SetBoneIKState					= G2API_SetBoneIKState;
	re.G2API_SetGhoul2ModelIndexes			= G2API_SetGhoul2ModelIndexes;
	re.G2API_SetGhoul2ModelFlags			= G2API_SetGhoul2ModelFlags;
	re.G2API_SetLodBias						= G2API_SetLodBias;
	re.G2API_SetNewOrigin					= G2API_SetNewOrigin;
	re.G2API_SetRagDoll						= G2API_SetRagDoll;
	re.G2API_SetRootSurface					= G2API_SetRootSurface;
	re.G2API_SetShader						= G2API_SetShader;
	re.G2API_SetSkin						= G2API_SetSkin;
	re.G2API_SetSurfaceOnOff				= G2API_SetSurfaceOnOff;
	re.G2API_SetTime						= G2API_SetTime;
	re.G2API_SkinlessModel					= G2API_SkinlessModel;
	re.G2API_StopBoneAngles					= G2API_StopBoneAngles;
	re.G2API_StopBoneAnglesIndex			= G2API_StopBoneAnglesIndex;
	re.G2API_StopBoneAnim					= G2API_StopBoneAnim;
	re.G2API_StopBoneAnimIndex				= G2API_StopBoneAnimIndex;

	#ifdef _G2_GORE
	re.G2API_GetNumGoreMarks				= G2API_GetNumGoreMarks;
	re.G2API_AddSkinGore					= G2API_AddSkinGore;
	re.G2API_ClearSkinGore					= G2API_ClearSkinGore;
	#endif // _SOF2

	// this is set in R_Init
	//re.G2VertSpaceServer	= G2VertSpaceServer;

	re.ext.Font_StrLenPixels				= RE_Font_StrLenPixelsNew;

	// appended entries: r_modelWarmupApi tells the engine they are there
	re.WarmModel							= R_WarmModel;
	re.WarmState							= R_WarmState;
	re.WarmModelEx							= R_WarmModelEx;

	return &re;
}

} //extern "C"
