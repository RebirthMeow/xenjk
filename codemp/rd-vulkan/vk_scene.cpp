/*
===========================================================================
Copyright (C) 2026 xenjk contributors

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

/*
Render scenes (docs/levelport-scene.md): a level's dense render layer beside its BSP. A map may carry
maps/<name>.xscn: unique meshes with their LOD chains, their placements (instances), baked materials and the source's
lights. The BSP stays the gameplay layer (collision, entities, light grid); this draws the level.

Load (RE_LoadWorldMap): one device-local vertex and index buffer; storage buffers for instances, meshes, LODs and the
part -> draw command map; one indexed indirect command per part, sorted by material, each with room for every
instance of its mesh. The lights are binned into a world grid of cells once (they are static).

Frame: the front end registers each world view that draws the scene (R_SceneAddView, viewParms.sceneView). At frame
start, before any render pass (vk_begin_frame), scene_cull.comp culls every registered view's instances into that
view's copy of the commands: frustum, a size cut in pixels, the LOD by projected size. RB_DrawSurfs then draws the
view: one vkCmdDrawIndexedIndirect per material (its images bound), opaque and cutout before the BSP's surfaces (and in
the depth prepass), blend after them; emissive again in the dynamic glow's extract pass.

GPU-driven draws (r_sceneGpuDriven, on a Vulkan 1.2 device): every scene image sits in one descriptor array and every
material in a buffer, so a view's draws need no binds between materials. After the cull, scene_compact.comp packs each
view's commands that have instances into draw lists (lit, depth, depth cutout, glow, blend), keeping their material
order, and each list is one vkCmdDrawIndexedIndirectCount: no empty commands for the GPU to walk, no per-material work
on the CPU. The lit list draws the opaque, cutout and emissive kinds in one pipeline, in material order (exactly
coplanar surfaces, a window's glass on its facade, resolve as before), testing depth before the shader without writing
it: the prepass holds the nearest depth, so LESS_OR_EQUAL passes exactly where EQUAL would and keeps the coarse depth
cull; a cutout still discards its clear texels (where it lies exactly on another surface they would pass). Its shader
has no debug views (r_sceneDebug picks a second pipeline with them: they kept ~15 values live through the light loop).

Sun (the map's q3gl2_sun, a moon at night): the sun cascade views register too (R_RenderSunShadowCascades). They cull
with the main view's LOD choice (coarser by r_sceneShadowLod) and drop casters under a few texels; the casters draw into
the cascade layers before the main render pass, and scene.frag reads the layers back (normal-offset PCF).

Light in the haze: after the cull, scene_fog.comp lights a volume over the eye view's frustum (froxels) from the light
grid and integrates it toward the eye; scene.frag adds it at each surface's depth, and over the sky in a full-view pass
(scene_sky.vert, mode 6) at the start of the blend pass, after the BSP's sky.

Wet ground: up-facing surfaces darken and turn glossy (puddles by noise) and mirror the frame. After the main view's
MSAA resolve (vk_scene_capture), scene_capture.comp packs the frame's colour and depth extract into a half-size
history (and its depth alone into an r16f copy, which the rays' steps read: a quarter of the bytes); scene_ssr.comp rebuilds each texel's surface from it, traces the reflected ray through it (a different start
and glossy spread every frame) and settles the result over frames (reprojected with the last camera). The next frame's
wet fragments read that settled light and apply their Fresnel.

Light shadows: the source's shadow-casting lights (the light type's bit 2) get static shadow maps, rendered once at
load into the tiles of a depth atlas: each face culled by scene_cull.comp like a view and drawn depth only. scene.frag
and scene_fog.comp read them (the haze's light then falls in shafts).

Colour grade: a map may carry maps/<name>.xlut, the source game's display transform as a 3D LUT (scene light in ARRI
LogC, display light out). After the main view's post 1x blends (vk_scene_grade), the resolved frame is copied aside and
a full-frame pass sends each pixel back through scene.frag's tone curve and then through the LUT, before the HUD.
scene.frag composites the haze in linear light and encodes past white (toDisplay's headroom): a float colour target
(r_hdr 2) keeps that, so the grade sees the scene's highlights unclipped; an 8-bit one clips them at the tone curve's top.
*/
#include "vk_scene_local.h"

static void Scene_SortVisible( VkCommandBuffer cb, int f, int v );

extern int gl_filter_min, gl_filter_max;	// vk_image.cpp: the mipmapped images' filter (r_textureMode)

static const uint32_t xsRecordSize[XS_COUNT] = { 28, 4, 32, 16, 16, 240, 64, 64, 64, 16 };

// the header: "XSCN", version, then the sections' counts and offsets (8 of each in version 1, 9 from version 2: 80
// bytes; 10 from version 6, the lights' shapes: 88 bytes)
static uint32_t Scene_HeaderSize( const uint32_t version ) { return version >= 6 ? 88u : 80u; }
typedef struct {
	uint32_t	version;
	uint32_t	count[XS_COUNT];
	uint32_t	ofs[XS_COUNT];
} xscnHeader_t;

static qboolean Scene_ReadHeader( const byte *buf, uint64_t len, xscnHeader_t *h )
{
	Com_Memset( h, 0, sizeof( *h ) );
	if ( len < 80 || memcmp( buf, "XSCN", 4 ) )
		return qfalse;
	memcpy( &h->version, buf + 4, 4 );
	const int n = h->version == 1 ? XS_BOXES : ( h->version >= 2 && h->version <= 5 ) ? XS_SHAPES : h->version == SCENE_VERSION ? XS_COUNT : 0;
	if ( !n || len < Scene_HeaderSize( h->version ) )
		return qfalse;
	memcpy( h->count, buf + 8, 4 * n );
	memcpy( h->ofs, buf + 8 + 4 * n, 4 * n );
	for ( int s = 0; s < n; s++ )
		if ( (uint64_t)h->ofs[s] + (uint64_t)h->count[s] * xsRecordSize[s] > (uint64_t)len )
			return qfalse;
	if ( h->count[XS_BOXES] && h->count[XS_BOXES] != h->count[XS_LIGHTS] )
		return qfalse;
	if ( h->count[XS_SHAPES] && h->count[XS_SHAPES] != h->count[XS_LIGHTS] )
		return qfalse;
	return qtrue;
}

// characters lit by the scene (r_sceneCharLight, R_SceneLightPoint): the lights and their boxes kept on the CPU, and every
// light reaching each light grid cell (Scene_BinLights: the GPU's cells keep only their strongest)
static std::vector<xscnLight_t> sceneCharLights;
static std::vector<float> sceneCharBoxes;					// 16 a light: its box's rows (scene space -> [-1, 1]); none: zeros
static std::vector<xscnShape_t> sceneCharShapes;			// a light's shape (version 6); none: empty
static std::vector<uint32_t> sceneCharCells, sceneCharRefs;	// per cell its first and count in the refs

sceneState_t sceneState;	// (the scene files name it sc: vk_scene_local.h)

cvar_t *r_scene;
static cvar_t *r_sceneLodBias;
static cvar_t *r_sceneCullPixels;
static cvar_t *r_sceneOcclusion;
static cvar_t *r_sceneOcclusionStats;
static cvar_t *r_sceneClusterStats;
static cvar_t *r_sceneHideScale;
static cvar_t *r_sceneHideFade;
static cvar_t *r_sceneHideDither;
static cvar_t *r_sceneCardBlend;
static cvar_t *r_sceneCutoutCoverage;
static cvar_t *r_sceneWaterBed;
static cvar_t *r_sceneLitEqual;
static cvar_t *r_sceneProxyDebug;
static cvar_t *r_sceneOnlyMesh;
static cvar_t *r_sceneDecals;
static cvar_t *r_sceneCompactBlocks;
static cvar_t *r_sceneKeptCull;
static cvar_t *r_sceneClusterCull;
static cvar_t *r_sceneLightHide;
static cvar_t *r_sceneLightHideDefault;
static cvar_t *r_sceneLightHideDistant;
static cvar_t *r_sceneLightDistant;
static cvar_t *r_sceneLightScale;
static cvar_t *r_sceneDlights;
cvar_t *r_sceneSignals;
cvar_t *r_sceneSignalShift;
cvar_t *r_sceneSignalAt;
static cvar_t *r_sceneDlightReach;
cvar_t *r_sceneExposure;
static cvar_t *r_sceneEmissive;
static cvar_t *r_sceneEmissiveExposure;
static cvar_t *r_sceneEmissiveRef;
static cvar_t *r_sceneGlow;
static cvar_t *r_sceneGlowThreshold;
cvar_t *r_sceneTonemap;
cvar_t *r_sceneFog;
static cvar_t *r_sceneFogDensity;
static cvar_t *r_sceneFogHeight;
static cvar_t *r_sceneFogMax;
static cvar_t *r_sceneFogLight;
static cvar_t *r_sceneFogPhase;
static cvar_t *r_sceneFogAlbedo;
static cvar_t *r_sceneFogVolumes;
static cvar_t *r_sceneFogFar;
static cvar_t *r_sceneFogTemporal;
static cvar_t *r_sceneSpecAA;
static cvar_t *r_sceneWindowCell;
static cvar_t *r_sceneFogStart;
static cvar_t *r_sceneFogMoon;
cvar_t *r_sceneWet;
cvar_t *r_sceneWetPuddles;
static cvar_t *r_sceneWetPorosity;
cvar_t *r_scenePDecals;
cvar_t *r_scenePDecalDistance;
cvar_t *r_scenePDecalNormal;
cvar_t *r_scenePDecalMax;
cvar_t *r_scenePDecalDebug;
cvar_t *r_scenePDecalTerrain;
static cvar_t *r_sceneLightSize;
cvar_t *r_sceneReflect;
cvar_t *r_sceneSsrRays;
cvar_t *r_sceneSsrFlat;
cvar_t *r_sceneAO;
static cvar_t *r_sceneShadowSoft;
static cvar_t *r_sceneShadowFar;
cvar_t *r_sceneAORadius;
cvar_t *r_sceneAOSamples;
cvar_t *r_sceneAOBlend;
cvar_t *r_sceneSsrBlend;
static cvar_t *r_sceneWindows;
static cvar_t *r_sceneEnv;
static cvar_t *r_sceneDayLamps;
static cvar_t *r_sceneScreens;
static cvar_t *r_sceneScreenLook;
static cvar_t *r_sceneScreenAnim;
cvar_t *r_sceneRotate;
static cvar_t *r_sceneScreenDepth;
static cvar_t *r_sceneScreenLayers;
static cvar_t *r_sceneScreenGrid;
static cvar_t *r_sceneScreenGridDepth;
static cvar_t *r_sceneScreenHue;
static cvar_t *r_sceneScreenGlow;
static cvar_t *r_sceneScreenTone;
static cvar_t *r_sceneScreenGain;
static cvar_t *r_sceneScreenBloom;
static cvar_t *r_sceneLightTubes;
static cvar_t *r_sceneLightTubeScale;
cvar_t *r_sceneAutoExposure;
cvar_t *r_sceneFx;
cvar_t *r_sceneFxDistance;
cvar_t *r_sceneFxMax;
static cvar_t *r_sceneFxLight;
static cvar_t *r_sceneFxSoft;
cvar_t *r_sceneFxColumns;
cvar_t *r_sceneFxDebug;
static cvar_t *r_sceneLodFade;
static cvar_t *r_sceneProbeSky;
static cvar_t *r_sceneWindowExposure;
cvar_t *r_sceneFxAccelDrag;
cvar_t *r_sceneFxOnly;
static cvar_t *r_sceneFxAlpha;
cvar_t *r_sceneExposureWindow;
cvar_t *r_sceneExposureMeter;
cvar_t *r_sceneExposureSpeed;
cvar_t *r_sceneExposureDebug;
static cvar_t *r_sceneFogLightKnee;
static cvar_t *r_sceneCharSun;
static cvar_t *r_sceneNightOnlyByDay;
static cvar_t *r_sceneDayOnlyAtNight;
static cvar_t *r_sceneLightPairs;
static cvar_t *r_sceneNonWorldLights;
static cvar_t *r_sceneSpotScale;
static cvar_t *r_sceneLightDedup;
static cvar_t *r_sceneSky;
static cvar_t *r_sceneSkyZenith;
static cvar_t *r_sceneSkyHorizon;
static cvar_t *r_sceneSkyGround;
static cvar_t *r_sceneSkyCurve;
static cvar_t *r_sceneSkySun;
static cvar_t *r_sceneFoliage;
static cvar_t *r_sceneTerrainPush;
static cvar_t *r_sceneFoliageWrap;
static cvar_t *r_sceneFoliageTrans;
static cvar_t *r_sceneFoliageAO;
static cvar_t *r_sceneClouds;
static cvar_t *r_sceneCloudScale;
static cvar_t *r_sceneCloudEdge;
static cvar_t *r_sceneCloudLight;
static cvar_t *r_sceneCloudWind;
static cvar_t *r_sceneHoloDots;
static cvar_t *r_sceneHoloBloom;
static cvar_t *r_sceneHoloGain;
static cvar_t *r_sceneHoloCurve;
static cvar_t *r_sceneMoon;
static cvar_t *r_sceneMoonSize;
static cvar_t *r_sceneMoonGlow;
static cvar_t *r_sceneMoonPath;
static cvar_t *r_sceneMoonLight;
static cvar_t *r_sceneMoonImage;
static cvar_t *r_sceneClock;
static image_t *sceneMoonImage;	// r_sceneMoonImage's, found at the scene's load (NULL: no moon)
cvar_t *r_sceneReflectSteps;
cvar_t *r_sceneReflectFar;
static cvar_t *r_sceneShadows;
cvar_t *r_sceneEyeRelative;

/*
The eye view's matrix for scene.vert (backEnd.viewParms; proj with its clip y flipped): absolute (scene space to clip),
or with r_sceneEyeRelative the view's rotation and projection alone (its translation left out: positions are taken
relative to the eye in scene.vert, eye.w 2). The view's modelview is R (p - origin) by R_RotateForViewer, so the two
agree in exact arithmetic; in float32 the absolute one cancels the view's large translation in every vertex's product.
*/
void Scene_EyeViewProj( float out[16], const float proj[16], qboolean relative )
{
	float mv[16];
	Com_Memcpy( mv, backEnd.viewParms.world.modelViewMatrix, sizeof( mv ) );
	if ( relative )
		mv[12] = mv[13] = mv[14] = 0.0f;
	myGlMultMatrix( mv, proj, out );
}
cvar_t *r_sceneShadowLod;
static cvar_t *r_sceneShadowCull;
static cvar_t *r_sceneShadowCascades;
static cvar_t *r_sceneLightShadows;
static cvar_t *r_sceneLightShadowRes;
static cvar_t *r_sceneLightShadowLists;
static cvar_t *r_sceneLightShadowHash;
static cvar_t *r_sceneLightShadowDepth;
static cvar_t *r_sceneLightShadowBudget;
static cvar_t *r_sceneVisiblePool;
static cvar_t *r_sceneCullGroups;
static cvar_t *r_sceneInteriors;
static cvar_t *r_sceneInteriorMargin;
static cvar_t *r_sceneInteriorPreload;
static cvar_t *r_sceneStream;
static cvar_t *r_sceneStreamMB;
static cvar_t *r_sceneStreamMargin;
static cvar_t *r_sceneStreamUploadMB;
static cvar_t *r_sceneStreamStats;
static cvar_t *r_sceneStreamFaces;
static cvar_t *r_sceneSettleFaces;
static void Scene_StreamFree( void );
static void Scene_StreamDetailTiles( const std::vector<float> &params );
static cvar_t *r_sceneRain;
static cvar_t *r_sceneRainFollow;
static cvar_t *r_sceneRainTiles;
static cvar_t *r_sceneDetail;
static cvar_t *r_sceneDetailSlots;
static cvar_t *r_sceneWater;
static cvar_t *r_sceneWaterRipple;
static cvar_t *r_sceneWaterClarity;
static cvar_t *r_sceneWaterReflect;
static cvar_t *r_sceneWaterShift;
static cvar_t *r_sceneWaterBreakup;
static cvar_t *r_sceneWaterWaves;
static cvar_t *r_sceneWaterWind;
static cvar_t *r_sceneWaterCascades;
static cvar_t *r_sceneSun;
static cvar_t *r_sceneAmbient;
static cvar_t *r_sceneCharLight;
static cvar_t *r_sceneProxyPush;
static cvar_t *r_sceneFogLightMax;
static cvar_t *r_sceneFogNearSamples;
static cvar_t *r_sceneFogSegment;
cvar_t *r_sceneGrade;
cvar_t *r_sceneGradeExposure;
cvar_t *r_sceneGradeCompute;
cvar_t *r_sceneHdr;
cvar_t *r_sceneDebug;
static cvar_t *r_sceneGpuDriven;
static cvar_t *r_sceneShaderStats;
static cvar_t *r_sceneClusters;
static cvar_t *r_sceneViewCull;
static cvar_t *r_sceneShadowReceivers;
static cvar_t *r_sceneInvocations;
static cvar_t *r_sceneSkip;
static cvar_t *r_sceneVrs;
static cvar_t *r_sceneTerrainFlat;
cvar_t *r_sceneAsync;
static void Scene_InvocationsFrame( VkCommandBuffer cb, int f );
static VkQueryPool	invPool[NUM_COMMAND_BUFFERS];		// r_sceneInvocations (Scene_InvocationsFrame)
static qboolean		invRecorded[NUM_COMMAND_BUFFERS];
static std::vector<image_t *> sceneTexImages;		// GPU-driven draws: the every-image set's images (Scene_WriteTextures)
static std::vector<VkImageView> sceneTexViews[NUM_COMMAND_BUFFERS];	// the view each slot's set holds for each image
static std::vector<uint32_t> sceneLodLead;		// scenecullcount: each LOD's lead command (~0: none)
struct sceneCullDbg_t { int cascade, eye; };
static sceneCullDbg_t sceneCullDbg[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
static std::vector<uint32_t> sceneDepthOrder;		// GPU-driven draws: the commands biggest mesh first (the depth lists)
#define SCENE_CULL_GROUP		4096	// instances a cull group (scene_cull.comp: 64 workgroups of 64)
#define SCENE_CULL_SUB			512		// instances a GPU-listed cull group (scene_groupcull.comp; scene_cull.comp: 8 workgroups)
static std::vector<float> sceneGroupBox;			// per cull group its instances' spheres' bounds: min xyz, max xyz
static std::vector<float> sceneInteriorBox;			// per interior group (maps/<map>.xint) its streaming box: min xyz, max xyz

/* ------------------------------------------------------------------ buffers */

qboolean Scene_CreateBuffer( sceneBuffer_t *b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, const char *name,
	qboolean bothQueues )
{
	VkBufferCreateInfo desc;
	VkMemoryRequirements req;
	VkMemoryAllocateInfo alloc;

	size = MAX( size, (VkDeviceSize)16 );
	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.size = size;
	desc.usage = usage;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	const uint32_t families[2] = { vk.queue_family_index, vk.compute_family_index };
	if ( bothQueues && vk.compute_queue ) {	// read on the compute queue when async (r_sceneAsync)
		desc.sharingMode = VK_SHARING_MODE_CONCURRENT;
		desc.queueFamilyIndexCount = 2;
		desc.pQueueFamilyIndices = families;
	}
	if ( qvkCreateBuffer( vk.device, &desc, NULL, &b->buf ) != VK_SUCCESS ) {
		b->buf = VK_NULL_HANDLE;
		b->mem = VK_NULL_HANDLE;	// (a stack buffer then frees nothing)
		b->size = 0;
		return qfalse;
	}
	qvkGetBufferMemoryRequirements( vk.device, b->buf, &req );
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, props );
	if ( vk_vram_allocate_named( vk.device, &alloc, &b->mem, name ) != VK_SUCCESS ) {
		qvkDestroyBuffer( vk.device, b->buf, NULL );
		b->buf = VK_NULL_HANDLE;
		b->mem = VK_NULL_HANDLE;
		return qfalse;
	}
	qvkBindBufferMemory( vk.device, b->buf, b->mem, 0 );
	VK_SET_OBJECT_NAME( b->buf, name, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	b->size = size;
	return qtrue;
}

void Scene_DestroyBuffer( sceneBuffer_t *b )
{
	if ( b->buf )
		qvkDestroyBuffer( vk.device, b->buf, NULL );
	if ( b->mem )
		qvkFreeMemory( vk.device, b->mem, NULL );
	b->buf = VK_NULL_HANDLE;
	b->mem = VK_NULL_HANDLE;
	b->size = 0;
}

// a device-local buffer holding `data`, through a staging copy of at most SCENE_STAGING_BYTES at a time (a joined
// scene's vertices are past 2 GB: one staging buffer that size doubled the load's host-visible peak)
#define SCENE_STAGING_BYTES	( (VkDeviceSize)64 << 20 )
qboolean Scene_StaticBuffer( sceneBuffer_t *b, const void *data, VkDeviceSize size, VkBufferUsageFlags usage, const char *name )
{
	sceneBuffer_t staging;
	void *p;

	if ( !Scene_CreateBuffer( b, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, name ) )
		return qfalse;
	if ( !size )
		return qtrue;
	const VkDeviceSize step = MIN( size, SCENE_STAGING_BYTES );
	if ( !Scene_CreateBuffer( &staging, step, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene staging" ) )
		return qfalse;
	VK_CHECK( qvkMapMemory( vk.device, staging.mem, 0, VK_WHOLE_SIZE, 0, &p ) );
	for ( VkDeviceSize at = 0; at < size; at += step ) {
		const VkDeviceSize n = MIN( step, size - at );
		memcpy( p, (const byte *)data + at, (size_t)n );
		VkCommandBuffer cmd = vk_begin_command_buffer();
		VkBufferCopy region;
		region.srcOffset = 0;
		region.dstOffset = at;
		region.size = n;
		qvkCmdCopyBuffer( cmd, staging.buf, b->buf, 1, &region );
		vk_end_command_buffer( cmd, __func__ );	// (waits: the staging memory is refilled next)
	}
	qvkUnmapMemory( vk.device, staging.mem );
	Scene_DestroyBuffer( &staging );
	return qtrue;
}

/* ------------------------------------------------------------------ pipelines */

VkShaderModule Scene_Shader( const unsigned char *code, unsigned int size )
{
	VkShaderModuleCreateInfo desc;
	VkShaderModule m = VK_NULL_HANDLE;

	Com_Memset( &desc, 0, sizeof( desc ) );
	desc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	desc.codeSize = size;
	desc.pCode = (const uint32_t *)code;
	if ( qvkCreateShaderModule( vk.device, &desc, NULL, &m ) != VK_SUCCESS )
		return VK_NULL_HANDLE;
	return m;
}

VkDescriptorSetLayout Scene_SetLayout( const VkDescriptorType *types, const VkShaderStageFlags *stages, int count,
	const uint32_t *arrays )	// arrays: each binding's descriptor count (default 1)
{
	VkDescriptorSetLayoutBinding bind[32];
	VkDescriptorSetLayoutCreateInfo dl;
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;

	if ( count > (int)ARRAY_LEN( bind ) ) {	// (the draw layout passed 24 for a while, written past the array's end)
		ri.Printf( PRINT_WARNING, "WARNING: scene: a set layout of %d bindings (at most %d)\n", count, (int)ARRAY_LEN( bind ) );
		return VK_NULL_HANDLE;
	}
	for ( int i = 0; i < count; i++ ) {
		bind[i].binding = i;
		bind[i].descriptorType = types[i];
		bind[i].descriptorCount = arrays ? arrays[i] : 1;
		bind[i].stageFlags = stages[i];
		bind[i].pImmutableSamplers = NULL;
	}
	Com_Memset( &dl, 0, sizeof( dl ) );
	dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dl.bindingCount = count;
	dl.pBindings = bind;
	if ( qvkCreateDescriptorSetLayout( vk.device, &dl, NULL, &layout ) != VK_SUCCESS )
		return VK_NULL_HANDLE;
	return layout;
}

// r_sceneShaderStats: pipelines made with their statistics captured, printed once made (VK_KHR_pipeline_executable_properties)
static VkPipelineCreateFlags Scene_StatsFlags( void )
{
	return ( vk.pipelineStats && r_sceneShaderStats && r_sceneShaderStats->integer ) ? VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : 0;
}

static void Scene_PrintStats( VkPipeline pipeline, const char *labelIn )
{
	if ( !pipeline || !Scene_StatsFlags() )
		return;
	char label[64];
	Q_strncpyz( label, labelIn, sizeof( label ) );	// a va() string: the va()s below reuse its buffer
	PFN_vkGetPipelineExecutablePropertiesKHR getProps = (PFN_vkGetPipelineExecutablePropertiesKHR)qvkGetDeviceProcAddr( vk.device, "vkGetPipelineExecutablePropertiesKHR" );
	PFN_vkGetPipelineExecutableStatisticsKHR getStats = (PFN_vkGetPipelineExecutableStatisticsKHR)qvkGetDeviceProcAddr( vk.device, "vkGetPipelineExecutableStatisticsKHR" );
	if ( !getProps || !getStats )
		return;
	VkPipelineInfoKHR pi;
	Com_Memset( &pi, 0, sizeof( pi ) );
	pi.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR;
	pi.pipeline = pipeline;
	uint32_t n = 0;
	if ( getProps( vk.device, &pi, &n, NULL ) != VK_SUCCESS || !n )
		return;
	std::vector<VkPipelineExecutablePropertiesKHR> props( n );
	for ( uint32_t i = 0; i < n; i++ ) {
		Com_Memset( &props[i], 0, sizeof( props[i] ) );
		props[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
	}
	getProps( vk.device, &pi, &n, props.data() );
	for ( uint32_t e = 0; e < n; e++ ) {
		VkPipelineExecutableInfoKHR ei;
		Com_Memset( &ei, 0, sizeof( ei ) );
		ei.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR;
		ei.pipeline = pipeline;
		ei.executableIndex = e;
		uint32_t ns = 0;
		if ( getStats( vk.device, &ei, &ns, NULL ) != VK_SUCCESS )
			continue;
		std::vector<VkPipelineExecutableStatisticKHR> st( ns );
		for ( uint32_t i = 0; i < ns; i++ ) {
			Com_Memset( &st[i], 0, sizeof( st[i] ) );
			st[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
		}
		getStats( vk.device, &ei, &ns, st.data() );
		char line[1024];
		Com_sprintf( line, sizeof( line ), "SHADERSTATS %s %s:", label, props[e].name );
		for ( uint32_t i = 0; i < ns; i++ ) {
			char v[64];
			switch ( st[i].format ) {
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: Com_sprintf( v, sizeof( v ), "%u", st[i].value.b32 ); break;
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: Com_sprintf( v, sizeof( v ), "%lld", (long long)st[i].value.i64 ); break;
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: Com_sprintf( v, sizeof( v ), "%llu", (unsigned long long)st[i].value.u64 ); break;
			default: Com_sprintf( v, sizeof( v ), "%.3f", st[i].value.f64 ); break;
			}
			Q_strcat( line, sizeof( line ), va( " %s=%s", st[i].name, v ) );
		}
		ri.Printf( PRINT_ALL, "%s\n", line );
	}
}

// scene.vert (+ scene.frag in mode `mode`, none for opaque depth) in the main render pass; the dglow extract pass is
// compatible with it (main's attachments with other load ops). `shadow`: the sun cascade pass (depth only, one sample,
// depth clamp for casters between the light and the cascade's box)
static VkPipeline Scene_GraphicsPipeline( VkShaderModule vs, VkShaderModule fs, int mode, qboolean depthOnly, qboolean blend, int shadow,
	int variant = SCENE_VARIANT_BOUND )
{
	VkVertexInputBindingDescription binding;
	VkVertexInputAttributeDescription attrs[4];
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
	VkSpecializationMapEntry specEntry[2];
	VkSpecializationInfo spec;
	VkGraphicsPipelineCreateInfo info;
	VkPipeline pipeline = VK_NULL_HANDLE;
	const int32_t specData[2] = { mode, ( !shadow && vk.sunShadow.image ) ? 1 : 0 };	// kMode, kSun
	const qboolean fragment = ( !depthOnly || mode == 4 || mode == 7 ) ? qtrue : qfalse;	// 7: the depth prepass with the fade's dither
	const qboolean sky = ( mode == 6 ) ? qtrue : qfalse;	// scene_sky.vert: no vertices, added over the sky
	const qboolean fx = ( mode == 10 || mode == 11 ) ? qtrue : qfalse;	// scene_fx.vert: no vertices, the particles (11 added)
	// water: blended over the lit bed as the blends are, but writing its depth (the frame's depth extract, and so the
	// reflections' history, holds its surface) and lying on nothing (no bias)
	const qboolean water = ( mode == SCENE_MODE_WATER ) ? qtrue : qfalse;

	binding.binding = 0;
	binding.stride = 28;
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R8G8B8A8_SNORM; attrs[1].offset = 12;
	attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R8G8B8A8_SNORM; attrs[2].offset = 16;
	attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32G32_SFLOAT; attrs[3].offset = 20;

	Com_Memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = ( sky || fx ) ? 0 : 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = ( sky || fx ) ? 0 : 4;
	vertexInput.pVertexAttributeDescriptions = attrs;

	Com_Memset( &inputAssembly, 0, sizeof( inputAssembly ) );
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	Com_Memset( &viewport, 0, sizeof( viewport ) );
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	// two-sided: the source's meshes mix windings and two-sided cards; the depth prepass rejects hidden back faces
	Com_Memset( &raster, 0, sizeof( raster ) );
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
	raster.lineWidth = 1.0f;
	// the cascades clamp casters between the light and their box; a light's faces clip at its near plane (its fixture)
	raster.depthClampEnable = ( shadow == SCENE_SHADOW_SUN && vk.sunShadow.depthClamp ) ? VK_TRUE : VK_FALSE;
	if ( blend ) {
		// decals and glass lie on other surfaces: pulled toward the eye
		raster.depthBiasEnable = VK_TRUE;
		raster.depthBiasConstantFactor = -1.0f;
		raster.depthBiasSlopeFactor = -1.0f;
	}

	Com_Memset( &multisample, 0, sizeof( multisample ) );
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = shadow ? VK_SAMPLE_COUNT_1_BIT : (VkSampleCountFlagBits)vkSamples;
	multisample.minSampleShading = 1.0f;

	Com_Memset( &depth, 0, sizeof( depth ) );
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depth.depthTestEnable = VK_TRUE;
	depth.depthWriteEnable = ( blend || mode == 5 || sky || variant == SCENE_VARIANT_GD_LIT || variant == SCENE_VARIANT_GD_LIT_EQUAL ) ? VK_FALSE : VK_TRUE;
	// (the GPU-driven lit pass: no writes, so it passes where the prepass's nearest is; EQUAL also stops, before the
	// shader, a fragment nearer than that depth, one the prepass's fade dither discarded, which LESS_OR_EQUAL lets the
	// shader discard again)
	depth.depthCompareOp = variant == SCENE_VARIANT_GD_LIT_EQUAL ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL;
	depth.maxDepthBounds = 1.0f;

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.colorWriteMask = depthOnly ? 0 : ( VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT );
	if ( blend || water ) {
		attachment.blendEnable = VK_TRUE;
		attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		attachment.colorBlendOp = VK_BLEND_OP_ADD;
		attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	if ( mode == 11 )
		attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;	// an additive particle
	if ( sky ) {	// added (alpha 0: the haze's light), or over what is there (alpha 1: r_sceneSky)
		attachment.blendEnable = VK_TRUE;
		attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		attachment.colorBlendOp = VK_BLEND_OP_ADD;
		attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	Com_Memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = shadow ? 0 : 1;
	blendState.pAttachments = &attachment;

	Com_Memset( &dynamic, 0, sizeof( dynamic ) );
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;

	for ( int i = 0; i < 2; i++ ) {
		specEntry[i].constantID = i;
		specEntry[i].offset = i * sizeof( int32_t );
		specEntry[i].size = sizeof( int32_t );
	}
	spec.mapEntryCount = 2;
	spec.pMapEntries = specEntry;
	spec.dataSize = sizeof( specData );
	spec.pData = specData;

	Com_Memset( stages, 0, sizeof( stages ) );
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs;
	stages[1].pName = "main";
	stages[1].pSpecializationInfo = &spec;

	Com_Memset( &info, 0, sizeof( info ) );
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.stageCount = fragment ? 2 : 1;
	info.pStages = stages;
	info.pVertexInputState = &vertexInput;
	info.pInputAssemblyState = &inputAssembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depth;
	info.pColorBlendState = &blendState;
	info.pDynamicState = &dynamic;
	info.layout = variant == SCENE_VARIANT_BOUND ? sc.drawPipeLayout : sc.gdPipeLayout;
	info.renderPass = shadow == SCENE_SHADOW_LIGHT ? sc.lsPass[0] : shadow ? vk.sunShadow.render_pass : vk.render_pass.main.handle;
	info.subpass = 0;
	info.basePipelineIndex = -1;
	info.flags = Scene_StatsFlags();
	// r_sceneVrs: the lit pipelines take each triangle's rate (scene.vert's VRS module) in place of their own 1 x 1
	VkPipelineFragmentShadingRateStateCreateInfoKHR rate;
	if ( vk.shadingRate && ( variant == SCENE_VARIANT_GD_LIT || variant == SCENE_VARIANT_GD_LIT_EQUAL ) ) {
		Com_Memset( &rate, 0, sizeof( rate ) );
		rate.sType = VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR;
		rate.fragmentSize.width = rate.fragmentSize.height = 1;
		rate.combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
		rate.combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
		info.pNext = &rate;
	}
	if ( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline ) != VK_SUCCESS )
		return VK_NULL_HANDLE;
	Scene_PrintStats( pipeline, va( "mode%d%s%s%s", mode, depthOnly ? "-depth" : "", shadow ? "-shadow" : "",
		variant == SCENE_VARIANT_GD_LIT ? "-gdlit" : variant == SCENE_VARIANT_GD_LIT_EQUAL ? "-gdlit-equal" : variant == SCENE_VARIANT_GD ? "-gd" : "" ) );
	return pipeline;
}

/* ------------------------------------------------------------------ mesh decals */

// CP2077's mesh decals (r_sceneDecals; the converter's blend materials flagged 0x40, SCENE_KIND_DECAL) project their
// albedo and normal onto the surface behind them, within their DepthThreshold (the source's mesh_decal*, drawn before
// lighting). Here, after the eye view's depth prepass, a pass draws them (the eye's culled commands, every decal
// material's range) into the DBuffer, each fragment testing the prepass's depth at its pixel: hidden behind a nearer
// surface, or with nothing within the threshold behind it, it is dropped. The lit pass reads the DBuffer at its pixel
// and mixes the decals in before lighting, on the surface they were drawn on only (its view depth is kept): one that
// phase 2 of occlusion draws after the pass gets none for that frame.

static qboolean Scene_DecalImage( int i, VkFormat format, uint32_t w, uint32_t h, const char *name )
{
	VkImageCreateInfo ic;
	Com_Memset( &ic, 0, sizeof( ic ) );
	ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ic.imageType = VK_IMAGE_TYPE_2D;
	ic.format = format;
	ic.extent.width = w;
	ic.extent.height = h;
	ic.extent.depth = 1;
	ic.mipLevels = 1;
	ic.arrayLayers = 1;
	ic.samples = VK_SAMPLE_COUNT_1_BIT;
	ic.tiling = VK_IMAGE_TILING_OPTIMAL;
	ic.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( qvkCreateImage( vk.device, &ic, NULL, &sc.decalImage[i] ) != VK_SUCCESS )
		return qfalse;
	VkMemoryRequirements req;
	qvkGetImageMemoryRequirements( vk.device, sc.decalImage[i], &req );
	VkMemoryAllocateInfo alloc;
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	if ( vk_vram_allocate_named( vk.device, &alloc, &sc.decalMem[i], "scene decal buffer" ) != VK_SUCCESS )
		return qfalse;
	qvkBindImageMemory( vk.device, sc.decalImage[i], sc.decalMem[i], 0 );
	VK_SET_OBJECT_NAME( sc.decalImage[i], name, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	VkImageViewCreateInfo vc;
	Com_Memset( &vc, 0, sizeof( vc ) );
	vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vc.image = sc.decalImage[i];
	vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vc.format = format;
	vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vc.subresourceRange.levelCount = 1;
	vc.subresourceRange.layerCount = 1;
	return qvkCreateImageView( vk.device, &vc, NULL, &sc.decalView[i] ) == VK_SUCCESS ? qtrue : qfalse;
}

// the decal pass's pipeline: scene.vert and scene.frag's DECAL variant, one sample, no depth attachment (the shader
// tests the prepass's), the five targets blended alike (no independent blend: the mesh decals write their fourth and
// fifth as zero at zero alpha, which leaves them): colour by its alpha, alpha accumulated
VkPipeline Scene_DecalPipeline( VkShaderModule vs, VkShaderModule fs )
{
	VkVertexInputBindingDescription binding = { 0, 28, VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attrs[4] = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R8G8B8A8_SNORM, 12 },
		{ 2, 0, VK_FORMAT_R8G8B8A8_SNORM, 16 }, { 3, 0, VK_FORMAT_R32G32_SFLOAT, 20 } };
	VkPipelineVertexInputStateCreateInfo vertexInput;
	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendAttachmentState attachment[5];
	VkPipelineColorBlendStateCreateInfo blendState;
	VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic;
	VkPipelineShaderStageCreateInfo stages[2];
	VkSpecializationMapEntry specEntry[2];
	VkSpecializationInfo spec;
	VkGraphicsPipelineCreateInfo info;
	VkPipeline pipeline = VK_NULL_HANDLE;
	const int32_t specData[2] = { 8, vk.sunShadow.image ? 1 : 0 };	// kMode (the DECAL variant ignores it), kSun
	const qboolean projected = vs == sc.pdecalVs ? qtrue : qfalse;	// (the projected decals': no vertex input, all four targets)

	Com_Memset( &vertexInput, 0, sizeof( vertexInput ) );
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 4;
	vertexInput.pVertexAttributeDescriptions = attrs;
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
	multisample.minSampleShading = 1.0f;
	Com_Memset( &depth, 0, sizeof( depth ) );
	depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	Com_Memset( attachment, 0, sizeof( attachment ) );
	for ( int i = 0; i < 5; i++ ) {
		attachment[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		attachment[i].blendEnable = VK_TRUE;
		attachment[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		attachment[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		attachment[i].colorBlendOp = VK_BLEND_OP_ADD;
		attachment[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		attachment[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		attachment[i].alphaBlendOp = VK_BLEND_OP_ADD;
	}
	Com_Memset( &blendState, 0, sizeof( blendState ) );
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = 5;
	blendState.pAttachments = attachment;
	Com_Memset( &dynamic, 0, sizeof( dynamic ) );
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;
	if ( projected ) {
		vertexInput.vertexBindingDescriptionCount = 0;
		vertexInput.vertexAttributeDescriptionCount = 0;
	}
	for ( int i = 0; i < 2; i++ ) {
		specEntry[i].constantID = i;
		specEntry[i].offset = i * sizeof( int32_t );
		specEntry[i].size = sizeof( int32_t );
	}
	spec.mapEntryCount = 2;
	spec.pMapEntries = specEntry;
	spec.dataSize = sizeof( specData );
	spec.pData = specData;
	Com_Memset( stages, 0, sizeof( stages ) );
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs;
	stages[1].pName = "main";
	stages[1].pSpecializationInfo = projected ? NULL : &spec;
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
	info.layout = projected ? scenePd.pipeLayout : sc.decalPipeLayout;
	info.renderPass = sc.decalPass;
	info.subpass = 0;
	info.basePipelineIndex = -1;
	info.flags = Scene_StatsFlags();
	if ( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline ) != VK_SUCCESS )
		return VK_NULL_HANDLE;
	Scene_PrintStats( pipeline, projected ? "projected decal" : "decal" );
	return pipeline;
}

// the DBuffer (at the screen's size when the scene has decals, else 1 x 1: the draw sets always hold it), left in
// SHADER_READ_ONLY, its pass and framebuffer, and the decal pipeline. qfalse: the images failed (the scene is not
// drawn); a failed pass or pipeline only turns the decals back into blends
static qboolean Scene_CreateDecals( void )
{
	static const VkFormat formats[5] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_R16G16B16A16_SFLOAT };
	static const char *names[5] = { "scene decal albedo", "scene decal normal", "scene decal depth", "scene decal surface", "scene decal emission" };
	const qboolean any = ( sc.numDecals || scenePd.count ) ? qtrue : qfalse;	// (mesh decal materials, projected decals)
	sc.decalW = any ? (uint32_t)glConfig.vidWidth : 1u;
	sc.decalH = any ? (uint32_t)glConfig.vidHeight : 1u;
	for ( int i = 0; i < 5; i++ )
		if ( !Scene_DecalImage( i, formats[i], sc.decalW, sc.decalH, names[i] ) )
			return qfalse;
	VkCommandBuffer cmd = vk_begin_command_buffer();
	for ( int i = 0; i < 5; i++ )
		Scene_ImageBarrier( cmd, sc.decalImage[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );
	vk_end_command_buffer( cmd, __func__ );
	if ( !any )
		return qtrue;

	qboolean ok = qtrue;
	VkAttachmentDescription att[5];
	VkAttachmentReference refs[5];
	Com_Memset( att, 0, sizeof( att ) );
	for ( int i = 0; i < 5; i++ ) {
		att[i].format = formats[i];
		att[i].samples = VK_SAMPLE_COUNT_1_BIT;
		att[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		att[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		att[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		att[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		refs[i].attachment = i;
		refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	VkSubpassDescription subpass;
	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 5;
	subpass.pColorAttachments = refs;
	VkSubpassDependency deps[2];
	Com_Memset( deps, 0, sizeof( deps ) );
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;	// the last frame's lit pass read them
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;	// this frame's lit pass reads them
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	VkRenderPassCreateInfo rp;
	Com_Memset( &rp, 0, sizeof( rp ) );
	rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp.attachmentCount = 5;
	rp.pAttachments = att;
	rp.subpassCount = 1;
	rp.pSubpasses = &subpass;
	rp.dependencyCount = 2;
	rp.pDependencies = deps;
	if ( qvkCreateRenderPass( vk.device, &rp, NULL, &sc.decalPass ) != VK_SUCCESS )
		ok = qfalse;
	if ( ok ) {
		VkFramebufferCreateInfo fb;
		Com_Memset( &fb, 0, sizeof( fb ) );
		fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb.renderPass = sc.decalPass;
		fb.attachmentCount = 5;
		fb.pAttachments = sc.decalView;
		fb.width = sc.decalW;
		fb.height = sc.decalH;
		fb.layers = 1;
		if ( qvkCreateFramebuffer( vk.device, &fb, NULL, &sc.decalFb ) != VK_SUCCESS )
			ok = qfalse;
	}
	if ( ok ) {
		static const VkDescriptorType types[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
		static const VkShaderStageFlags stagesOf[1] = { VK_SHADER_STAGE_FRAGMENT_BIT };
		sc.decalLayout = Scene_SetLayout( types, stagesOf, 1 );
		VkDescriptorSetLayout sets[7] = { sc.drawLayout, vk.set_layout_sampler, vk.set_layout_sampler, vk.set_layout_sampler,
			vk.set_layout_sampler, vk.set_layout_sampler, sc.decalLayout };	// the draw pipelines' six, then the depth
		VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( scenePush_t ) };
		VkPipelineLayoutCreateInfo pl;
		Com_Memset( &pl, 0, sizeof( pl ) );
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = 7;
		pl.pSetLayouts = sets;
		pl.pushConstantRangeCount = 1;
		pl.pPushConstantRanges = &push;
		if ( !sc.decalLayout || qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.decalPipeLayout ) != VK_SUCCESS )
			ok = qfalse;
	}
	if ( ok ) {
		VkShaderModule vs = Scene_Shader( scene_vert_spv, scene_vert_spv_size );
		VkShaderModule fs = vk.msaaActive ? Scene_Shader( scene_frag_decal_ms_spv, scene_frag_decal_ms_spv_size )
			: Scene_Shader( scene_frag_decal_spv, scene_frag_decal_spv_size );
		sc.decalPipe = ( vs && fs ) ? Scene_DecalPipeline( vs, fs ) : VK_NULL_HANDLE;
		if ( vs ) qvkDestroyShaderModule( vk.device, vs, NULL );
		if ( fs ) qvkDestroyShaderModule( vk.device, fs, NULL );
		ok = sc.decalPipe ? qtrue : qfalse;
	}
	if ( ok && scenePd.count && !Scene_CreatePDecals() ) {	// (without its pipeline the projected decals are not drawn)
		ri.Printf( PRINT_WARNING, "WARNING: scene: could not create the projected decals' pipeline or buffers; not drawn\n" );
		Scene_FreePDecalsGpu();
		scenePd.count = 0;
	}
	if ( !ok ) {
		ri.Printf( PRINT_WARNING, "WARNING: scene: could not create the decal pass; its %u decal materials are drawn as blends\n", sc.numDecals );
		for ( uint32_t i = 0; i < sc.numMaterials; i++ )
			if ( sc.materials[i].kind == SCENE_KIND_DECAL ) {
				sc.materials[i].kind = SCENE_KIND_BLEND;
				Com_Memset( sc.materials[i].emissive, 0, sizeof( sc.materials[i].emissive ) );
				sc.materials[i].emissive[3] = 1.0f;
			}
		sc.numDecals = 0;
	}
	return qtrue;
}

static void Scene_FreeDecals( void )
{
	Scene_FreePDecalsGpu();
	if ( sc.decalPipe ) qvkDestroyPipeline( vk.device, sc.decalPipe, NULL );
	if ( sc.decalPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.decalPipeLayout, NULL );
	if ( sc.decalLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.decalLayout, NULL );
	if ( sc.decalFb ) qvkDestroyFramebuffer( vk.device, sc.decalFb, NULL );
	if ( sc.decalPass ) qvkDestroyRenderPass( vk.device, sc.decalPass, NULL );
	for ( int i = 0; i < 5; i++ ) {
		if ( sc.decalView[i] ) qvkDestroyImageView( vk.device, sc.decalView[i], NULL );
		if ( sc.decalImage[i] ) qvkDestroyImage( vk.device, sc.decalImage[i], NULL );
		if ( sc.decalMem[i] ) qvkFreeMemory( vk.device, sc.decalMem[i], NULL );
	}
}

static qboolean Scene_CreatePipelines( void )
{
	// 6: the near cascades' receivers (the eye's sides, the light's travel); 7..11: occlusion's pyramids, the slot's
	// cameras and counters, phase 1's hidden list, the dispatch sizes, the commands phase 2 touched (unused); 12: the
	// LODs to sort (scene_sort.comp)
	static const VkDescriptorType cullTypes[16] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags cullStages[16] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT };
	// scene_scatter.comp: the slot's pool, the LODs, the parts' commands, every view's commands (an array), the visible pool
	static const VkDescriptorType scatterTypes[5] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const uint32_t scatterArrays[5] = { 1, 1, 1, SCENE_MAX_VIEWS, 1 };
	// scene_sort.comp: the LODs, the parts' commands, the view's commands and visible list, the LODs to sort
	static const VkDescriptorType sortTypes[5] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags sortStages[5] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	static const VkDescriptorType occTypes[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags occStages[2] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	// 10, 11, 12: the light shadows (per light its first face, the faces, the atlas); 13: the materials' detail;
	// GPU-driven draws: 14 each list slot's material, 15 the materials (written by Scene_CreateGpuDriven); 16 the light
	// clusters; 17 the lights' fades (scene_lightfade.comp); 18 the stand-ins; 19 the meshes (scene.vert: the hide fade's centre);
	// 20, 21, 22 the DBuffer (mesh decals: albedo, normal, the receiver's depth); 23 the occlusion pyramids (the water's bed)
	static const VkDescriptorType drawTypes[30] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,	// 27: maps/<map>.xw3b's vertex stream (scene.vert)
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };	// 28, 29: r_sceneAO's two images
	// (2, 3, 4, 13 and 17 also the vertex stage's: the particles' light, records and light fades, scene_fx.vert)
	static const VkShaderStageFlags drawStages[30] = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_VERTEX_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };	// 15: the materials (scene.vert reads the kind); 18: the stand-ins; 24, 25: the DBuffer's surface and emission; 26: the water's waves
	// the history's depth alone: ssr 4, capture 3
	// 5, 6: r_sceneAO's image this frame and last frame's
	static const VkDescriptorType ssrTypes[7] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
	static const VkShaderStageFlags ssrStages[7] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	static const VkDescriptorType captureTypes[4] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
	static const VkShaderStageFlags captureStages[4] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT };
	// 8, 9, 10: the light shadows, as for the draw set; 11: the sun cascades (the moon's light falls in shafts)
	// 12: the haze's light clusters; 13: the lights' fades (scene_lightfade.comp); 14: the volumes in view (scene_viewcull.comp);
	// 15: last frame's scatter (r_sceneFogTemporal)
	static const VkDescriptorType fogTypes[16] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER };
	static const VkShaderStageFlags fogStages[16] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT };
	VkPipelineLayoutCreateInfo pl;
	VkPushConstantRange push;
	VkDescriptorSetLayout sets[6];

	sc.cullLayout = Scene_SetLayout( cullTypes, cullStages, 16 );	// 13: the view's instance groups (r_sceneCullGroups); 14: the interiors the eye is in; 15: the slot's pool
	sc.scatterLayout = Scene_SetLayout( scatterTypes, cullStages, 5, scatterArrays );
	sc.groupCullLayout = Scene_SetLayout( cullTypes, cullStages, 2 );	// scene_groupcull.comp: the groups, the view's list
	sc.sortLayout = Scene_SetLayout( sortTypes, sortStages, 5 );
	sc.occLayout = Scene_SetLayout( occTypes, occStages, 2 );
	sc.drawLayout = Scene_SetLayout( drawTypes, drawStages, 30 );
	sc.ssrLayout = Scene_SetLayout( ssrTypes, ssrStages, 7 );
	sc.fogLayout = Scene_SetLayout( fogTypes, fogStages, 16 );
	sc.captureLayout = Scene_SetLayout( captureTypes, captureStages, 4 );
	if ( !sc.cullLayout || !sc.scatterLayout || !sc.groupCullLayout || !sc.sortLayout || !sc.drawLayout || !sc.fogLayout || !sc.captureLayout || !sc.ssrLayout || !sc.occLayout )
		return qfalse;

	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = sizeof( sceneCullPush_t );
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.cullLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.cullPipeLayout ) != VK_SUCCESS )
		return qfalse;
	{	// scene_scatter.comp's: its set, the view and the pass pushed
		VkPushConstantRange sp = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
		VkPipelineLayoutCreateInfo spl = pl;
		spl.setLayoutCount = 1;
		spl.pSetLayouts = &sc.scatterLayout;
		spl.pushConstantRangeCount = 1;
		spl.pPushConstantRanges = &sp;
		if ( qvkCreatePipelineLayout( vk.device, &spl, NULL, &sc.scatterPipeLayout ) != VK_SUCCESS )
			return qfalse;
		spl.pSetLayouts = &sc.groupCullLayout;	// scene_groupcull.comp's: scene_cull.comp's push
		sp.size = sizeof( sceneCullPush_t );
		if ( qvkCreatePipelineLayout( vk.device, &spl, NULL, &sc.groupCullPipeLayout ) != VK_SUCCESS )
			return qfalse;
	}
	push.size = 64;	// scene_occ.comp: the source's size and offset, up to five levels' offsets, the projection
	pl.pSetLayouts = &sc.occLayout;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.occPipeLayout ) != VK_SUCCESS )
		return qfalse;
	pl.pSetLayouts = &sc.fogLayout;
	pl.pushConstantRangeCount = 0;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.fogPipeLayout ) != VK_SUCCESS )
		return qfalse;
	pl.pSetLayouts = &sc.sortLayout;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.sortPipeLayout ) != VK_SUCCESS )
		return qfalse;
	pl.pSetLayouts = &sc.captureLayout;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.capturePipeLayout ) != VK_SUCCESS )
		return qfalse;
	pl.pSetLayouts = &sc.ssrLayout;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.ssrPipeLayout ) != VK_SUCCESS )
		return qfalse;
	pl.pushConstantRangeCount = 1;

	sets[0] = sc.drawLayout;
	sets[1] = sets[2] = sets[3] = vk.set_layout_sampler;	// albedo, normal, ORM
	sets[4] = vk.set_layout_sampler;						// the sun cascades (sampler2DArrayShadow)
	sets[5] = vk.set_layout_sampler;						// a multilayered material's tiled detail
	push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	push.size = sizeof( scenePush_t );
	pl.setLayoutCount = 6;
	pl.pSetLayouts = sets;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.drawPipeLayout ) != VK_SUCCESS )
		return qfalse;
	if ( !Scene_CreateDecals() )	// the DBuffer (its images are in every draw set); without its pass decals draw as blends
		return qfalse;

	VkShaderModule cs = Scene_Shader( scene_cull_comp_spv, scene_cull_comp_spv_size );
	VkShaderModule vs = Scene_Shader( scene_vert_spv, scene_vert_spv_size );
	VkShaderModule fs = Scene_Shader( scene_frag_spv, scene_frag_spv_size );
	VkShaderModule fogCs = Scene_Shader( scene_fog_comp_spv, scene_fog_comp_spv_size );
	VkShaderModule skyVs = Scene_Shader( scene_sky_vert_spv, scene_sky_vert_spv_size );
	VkShaderModule fxVs = Scene_Shader( scene_fx_vert_spv, scene_fx_vert_spv_size );
	VkShaderModule capCs = vk.storageR16f ? Scene_Shader( scene_capture_comp_spv, scene_capture_comp_spv_size )
		: Scene_Shader( scene_capture_nocopy_comp_spv, scene_capture_nocopy_comp_spv_size );	// (no r16f storage: no depth copy)
	VkShaderModule ssrCs = Scene_Shader( scene_ssr_comp_spv, scene_ssr_comp_spv_size );
	qboolean ok = ( cs && vs && fs && fogCs && skyVs && capCs && ssrCs ) ? qtrue : qfalse;
	if ( ok ) {
		VkComputePipelineCreateInfo ci;
		Com_Memset( &ci, 0, sizeof( ci ) );
		ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		ci.stage.module = cs;
		ci.stage.pName = "main";
		ci.layout = sc.cullPipeLayout;
		ci.flags = Scene_StatsFlags();
		if ( qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.cull ) != VK_SUCCESS )
			ok = qfalse;
		{	// the count-then-scan cull's allocate and scatter passes
			VkShaderModule scs = Scene_Shader( scene_scatter_comp_spv, scene_scatter_comp_spv_size );
			VkComputePipelineCreateInfo sci = ci;
			sci.stage.module = scs;
			sci.layout = sc.scatterPipeLayout;
			if ( !scs || qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &sci, NULL, &sc.scatterPipe ) != VK_SUCCESS )
				ok = qfalse;
			if ( scs )
				qvkDestroyShaderModule( vk.device, scs, NULL );
			VkShaderModule gcs = Scene_Shader( scene_groupcull_comp_spv, scene_groupcull_comp_spv_size );
			sci.stage.module = gcs;
			sci.layout = sc.groupCullPipeLayout;
			if ( !gcs || qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &sci, NULL, &sc.groupCullPipe ) != VK_SUCCESS )
				ok = qfalse;
			if ( gcs )
				qvkDestroyShaderModule( vk.device, gcs, NULL );
		}
		Scene_PrintStats( sc.cull, "cull" );
		VkShaderModule sortCs = Scene_Shader( scene_sort_comp_spv, scene_sort_comp_spv_size );
		ci.layout = sc.sortPipeLayout;
		ci.stage.module = sortCs;
		if ( !sortCs || qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.sortPipe ) != VK_SUCCESS )
			ok = qfalse;
		if ( sortCs )
			qvkDestroyShaderModule( vk.device, sortCs, NULL );
		Scene_PrintStats( sc.sortPipe, "sort" );
		{
			const int32_t one = 1;
			VkSpecializationMapEntry touchedEntry = { 0, 0, sizeof( int32_t ) };
			VkSpecializationInfo touchedSpec = { 1, &touchedEntry, sizeof( one ), &one };
			sortCs = Scene_Shader( scene_sort_comp_spv, scene_sort_comp_spv_size );
			ci.stage.module = sortCs;
			ci.stage.pSpecializationInfo = &touchedSpec;
			if ( !sortCs || qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.sortTouchedPipe ) != VK_SUCCESS )
				ok = qfalse;
			if ( sortCs )
				qvkDestroyShaderModule( vk.device, sortCs, NULL );
			ci.stage.pSpecializationInfo = NULL;
		}
		ci.stage.module = cs;
		ci.layout = sc.cullPipeLayout;
		// occlusion's pyramid from the depth attachment (its MSAA samples when it has them)
		VkShaderModule occCs = vk.msaaActive ? Scene_Shader( scene_occ_ms_comp_spv, scene_occ_ms_comp_spv_size )
			: Scene_Shader( scene_occ_1_comp_spv, scene_occ_1_comp_spv_size );
		ci.layout = sc.occPipeLayout;
		ci.stage.module = occCs;
		if ( !occCs || qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.occPipeDepth ) != VK_SUCCESS )
			ok = qfalse;
		if ( occCs )
			qvkDestroyShaderModule( vk.device, occCs, NULL );
		ci.layout = sc.cullPipeLayout;
		// the haze's light volume: one shader, kPass 0 (inject) and 1 (integrate)
		VkSpecializationMapEntry entry = { 0, 0, sizeof( int32_t ) };
		VkSpecializationInfo spec;
		for ( int pass = 0; pass < 2 && ok; pass++ ) {
			const int32_t kPass = pass;
			spec.mapEntryCount = 1;
			spec.pMapEntries = &entry;
			spec.dataSize = sizeof( kPass );
			spec.pData = &kPass;
			ci.stage.module = fogCs;
			ci.stage.pSpecializationInfo = &spec;
			ci.layout = sc.fogPipeLayout;
			if ( qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.fogPipe[pass] ) != VK_SUCCESS )
				ok = qfalse;
			Scene_PrintStats( sc.fogPipe[pass], pass ? "haze-integrate" : "haze-inject" );
		}
		ci.stage.module = capCs;
		ci.stage.pSpecializationInfo = NULL;
		ci.layout = sc.capturePipeLayout;
		if ( ok && qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.capturePipe ) != VK_SUCCESS )
			ok = qfalse;
		// the rays' steps read the r16f depth copy, or the history's own alpha
		const int32_t depthChannel = vk.storageR16f ? 0 : 3;
		VkSpecializationMapEntry ssrEntry = { 0, 0, sizeof( int32_t ) };
		VkSpecializationInfo ssrSpec = { 1, &ssrEntry, sizeof( depthChannel ), &depthChannel };
		ci.stage.module = ssrCs;
		ci.stage.pSpecializationInfo = &ssrSpec;
		ci.layout = sc.ssrPipeLayout;
		if ( ok && qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.ssrPipe ) != VK_SUCCESS )
			ok = qfalse;
		Scene_PrintStats( sc.ssrPipe, "ssr" );
	}
	if ( ok ) {
		sc.pipe[SCENE_PIPE_OPAQUE] = Scene_GraphicsPipeline( vs, fs, 0, qfalse, qfalse, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 1, qfalse, qfalse, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_BLEND] = Scene_GraphicsPipeline( vs, fs, 2, qfalse, qtrue, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_EMISSIVE] = Scene_GraphicsPipeline( vs, fs, 3, qfalse, qfalse, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_DEPTH] = Scene_GraphicsPipeline( vs, fs, 0, qtrue, qfalse, SCENE_SHADOW_NONE );
		sc.pipeDepthFade = Scene_GraphicsPipeline( vs, fs, 7, qtrue, qfalse, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_DEPTH_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 4, qtrue, qfalse, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_GLOW] = Scene_GraphicsPipeline( vs, fs, 5, qfalse, qfalse, SCENE_SHADOW_NONE );
		sc.pipe[SCENE_PIPE_SKY] = Scene_GraphicsPipeline( skyVs, fs, 6, qfalse, qfalse, SCENE_SHADOW_NONE );
		if ( fxVs ) {	// (none: no particles)
			sc.fxPipe[0] = Scene_GraphicsPipeline( fxVs, fs, 10, qfalse, qtrue, SCENE_SHADOW_NONE );
			sc.fxPipe[1] = Scene_GraphicsPipeline( fxVs, fs, 11, qfalse, qtrue, SCENE_SHADOW_NONE );
		}
		// the optional ones: a failure turns their feature off (the water's is reported at the load)
		if ( !sc.pipeDepthFade )
			ri.Printf( PRINT_WARNING, "WARNING: scene: no fading depth pipeline; the hide distances cut instead of fading\n" );
		if ( !fxVs || !sc.fxPipe[0] || !sc.fxPipe[1] )
			ri.Printf( PRINT_WARNING, "WARNING: scene: no particle pipelines; the effects are not drawn\n" );
		for ( int i = 0; i < SCENE_PIPE_SHADOW; i++ )
			if ( !sc.pipe[i] )
				ok = qfalse;
		if ( ok && vk.sunShadow.image && vk.sunShadow.render_pass && vk.sunShadow.depthClamp ) {
			sc.pipe[SCENE_PIPE_SHADOW] = Scene_GraphicsPipeline( vs, fs, 0, qtrue, qfalse, SCENE_SHADOW_SUN );
			sc.pipe[SCENE_PIPE_SHADOW_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 4, qtrue, qfalse, SCENE_SHADOW_SUN );
			sc.shadowPipes = ( sc.pipe[SCENE_PIPE_SHADOW] && sc.pipe[SCENE_PIPE_SHADOW_CUTOUT] ) ? qtrue : qfalse;
		}
		if ( ok )	// optional: without it the water draws as plain blends
			sc.pipe[SCENE_PIPE_WATER] = Scene_GraphicsPipeline( vs, fs, SCENE_MODE_WATER, qfalse, qfalse, SCENE_SHADOW_NONE );
		if ( ok && sc.lsPass[0] ) {
			sc.pipe[SCENE_PIPE_LSHADOW] = Scene_GraphicsPipeline( vs, fs, 0, qtrue, qfalse, SCENE_SHADOW_LIGHT );
			sc.pipe[SCENE_PIPE_LSHADOW_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 4, qtrue, qfalse, SCENE_SHADOW_LIGHT );
			if ( !sc.pipe[SCENE_PIPE_LSHADOW] || !sc.pipe[SCENE_PIPE_LSHADOW_CUTOUT] )
				ok = qfalse;
		}
	}
	if ( cs ) qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( vs ) qvkDestroyShaderModule( vk.device, vs, NULL );
	if ( fs ) qvkDestroyShaderModule( vk.device, fs, NULL );
	if ( fogCs ) qvkDestroyShaderModule( vk.device, fogCs, NULL );
	if ( skyVs ) qvkDestroyShaderModule( vk.device, skyVs, NULL );
	if ( fxVs ) qvkDestroyShaderModule( vk.device, fxVs, NULL );
	if ( capCs ) qvkDestroyShaderModule( vk.device, capCs, NULL );
	if ( ssrCs ) qvkDestroyShaderModule( vk.device, ssrCs, NULL );
	return ok;
}

void Scene_WriteSet( VkDescriptorSet set, const sceneBuffer_t **bufs, const VkDescriptorType *types, int count )
{
	VkDescriptorBufferInfo bi[16];
	VkWriteDescriptorSet wr[16];

	if ( count > (int)ARRAY_LEN( wr ) ) {	// (as Scene_SetLayout's: a release build has no assert)
		ri.Printf( PRINT_WARNING, "WARNING: scene: a set write of %d bindings (at most %d)\n", count, (int)ARRAY_LEN( wr ) );
		return;
	}
	for ( int i = 0; i < count; i++ ) {
		bi[i].buffer = bufs[i]->buf;
		bi[i].offset = 0;
		bi[i].range = VK_WHOLE_SIZE;
		Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
		wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wr[i].dstSet = set;
		wr[i].dstBinding = i;
		wr[i].descriptorCount = 1;
		wr[i].descriptorType = types[i];
		wr[i].pBufferInfo = &bi[i];
	}
	qvkUpdateDescriptorSets( vk.device, count, wr, 0, NULL );
}

// a 2D rgba16f storage and sampled image (the reflections' history and results)
static qboolean Scene_HalfImage( VkImage *image, VkDeviceMemory *mem, VkImageView *view, uint32_t w, uint32_t h, const char *name,
	VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT, uint32_t mips = 1, VkImageView *level0 = NULL )
{
	VkImageCreateInfo ic;
	Com_Memset( &ic, 0, sizeof( ic ) );
	ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ic.imageType = VK_IMAGE_TYPE_2D;
	ic.format = format;
	ic.extent.width = w;
	ic.extent.height = h;
	ic.extent.depth = 1;
	ic.mipLevels = mips;
	ic.arrayLayers = 1;
	ic.samples = VK_SAMPLE_COUNT_1_BIT;
	ic.tiling = VK_IMAGE_TILING_OPTIMAL;
	ic.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | ( mips > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0 );
	ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// the reflections' images are written on one queue and read on the other when they run async (r_sceneAsync)
	const uint32_t families[2] = { vk.queue_family_index, vk.compute_family_index };
	if ( vk.compute_queue ) {
		ic.sharingMode = VK_SHARING_MODE_CONCURRENT;
		ic.queueFamilyIndexCount = 2;
		ic.pQueueFamilyIndices = families;
	}
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
	vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vc.format = ic.format;
	vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vc.subresourceRange.levelCount = mips;
	vc.subresourceRange.layerCount = 1;
	if ( qvkCreateImageView( vk.device, &vc, NULL, view ) != VK_SUCCESS )
		return qfalse;
	if ( !level0 )
		return qtrue;
	vc.subresourceRange.levelCount = 1;
	return qvkCreateImageView( vk.device, &vc, NULL, level0 ) == VK_SUCCESS ? qtrue : qfalse;
}

// the haze's light volume: two rgba16f 3D images (GENERAL layout each frame: written by scene_fog.comp, sampled by
// scene.frag), linear filtering
static qboolean Scene_CreateFogVolume( void )
{
	for ( int i = 0; i < 3; i++ ) {
		VkImageCreateInfo ic;
		Com_Memset( &ic, 0, sizeof( ic ) );
		ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ic.imageType = VK_IMAGE_TYPE_3D;
		ic.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		ic.extent.width = SCENE_FOG_W;
		ic.extent.height = SCENE_FOG_H;
		ic.extent.depth = SCENE_FOG_D;
		ic.mipLevels = 1;
		ic.arrayLayers = 1;
		ic.samples = VK_SAMPLE_COUNT_1_BIT;
		ic.tiling = VK_IMAGE_TILING_OPTIMAL;
		ic.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if ( qvkCreateImage( vk.device, &ic, NULL, &sc.fogImage[i] ) != VK_SUCCESS )
			return qfalse;
		VkMemoryRequirements req;
		qvkGetImageMemoryRequirements( vk.device, sc.fogImage[i], &req );
		VkMemoryAllocateInfo alloc;
		Com_Memset( &alloc, 0, sizeof( alloc ) );
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.allocationSize = req.size;
		alloc.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
		if ( vk_vram_allocate_named( vk.device, &alloc, &sc.fogMem[i], "scene fog volume" ) != VK_SUCCESS )
			return qfalse;
		qvkBindImageMemory( vk.device, sc.fogImage[i], sc.fogMem[i], 0 );
		VK_SET_OBJECT_NAME( sc.fogImage[i], i == 1 ? "scene haze light" : i ? "scene haze scatter 2" : "scene haze scatter", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );

		VkImageViewCreateInfo vc;
		Com_Memset( &vc, 0, sizeof( vc ) );
		vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vc.image = sc.fogImage[i];
		vc.viewType = VK_IMAGE_VIEW_TYPE_3D;
		vc.format = ic.format;
		vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vc.subresourceRange.levelCount = 1;
		vc.subresourceRange.layerCount = 1;
		if ( qvkCreateImageView( vk.device, &vc, NULL, &sc.fogView[i] ) != VK_SUCCESS )
			return qfalse;
	}
	Vk_Sampler_Def def;
	Com_Memset( &def, 0, sizeof( def ) );
	def.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	def.gl_mag_filter = GL_LINEAR;
	def.gl_min_filter = GL_LINEAR;
	def.max_lod_1_0 = qtrue;
	def.noAnisotropy = qtrue;
	sc.fogSampler = vk_find_sampler( &def );	// the engine's cache owns it
	def.gl_mag_filter = GL_NEAREST;
	def.gl_min_filter = GL_NEAREST;
	sc.nearestSampler = vk_find_sampler( &def );
	if ( !sc.fogSampler || !sc.nearestSampler )
		return qfalse;

	// the reflections' history and their two settled images: half the frame, rgba16f
	const uint32_t w = MAX( glConfig.vidWidth / 2, 1 ), h = MAX( glConfig.vidHeight / 2, 1 );
	// the history's colour levels (a hit's colour read by its glossy cone: a thin bright thing such as a saber blade
	// mirrors as a streak, not as beads where single steps landed on it); blitted, so the format must filter
	VkFormatProperties fp;
	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, VK_FORMAT_R16G16B16A16_SFLOAT, &fp );
	const VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	sc.historyMips = ( fp.optimalTilingFeatures & blit ) == blit ? SCENE_HISTORY_MIPS : 1;
	{
		Vk_Sampler_Def def;
		Com_Memset( &def, 0, sizeof( def ) );
		def.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		def.gl_mag_filter = GL_LINEAR;
		def.gl_min_filter = GL_LINEAR_MIPMAP_LINEAR;
		def.noAnisotropy = qtrue;
		sc.mipSampler = vk_find_sampler( &def );	// the engine's cache owns it
		if ( !sc.mipSampler )
			sc.historyMips = 1;
	}
	return ( Scene_HalfImage( &sc.historyImage, &sc.historyMem, &sc.historyView, w, h, "scene reflection history", VK_FORMAT_R16G16B16A16_SFLOAT,
			sc.historyMips, &sc.historyStoreView )
		&& ( !vk.storageR16f || Scene_HalfImage( &sc.historyDepthImage, &sc.historyDepthMem, &sc.historyDepthView, w, h, "scene reflection history depth", VK_FORMAT_R16_SFLOAT ) )
		&& Scene_HalfImage( &sc.reflectImage[0], &sc.reflectMem[0], &sc.reflectView[0], w, h, "scene reflections 0" )
		&& Scene_HalfImage( &sc.reflectImage[1], &sc.reflectMem[1], &sc.reflectView[1], w, h, "scene reflections 1" )
		&& Scene_HalfImage( &sc.aoImage[0], &sc.aoMem[0], &sc.aoView[0], w, h, "scene occlusion 0" )
		&& Scene_HalfImage( &sc.aoImage[1], &sc.aoMem[1], &sc.aoView[1], w, h, "scene occlusion 1" ) ) ? qtrue : qfalse;
}

// a set's light shadow bindings (first .. first + 2): per light its first face, the faces, the atlas
static void Scene_WriteLightShadows( VkDescriptorSet set, uint32_t first )
{
	VkDescriptorBufferInfo bi[2] = { { sc.lsLights.buf, 0, VK_WHOLE_SIZE }, { sc.lsFaces.buf, 0, VK_WHOLE_SIZE } };
	VkDescriptorImageInfo ii;
	VkWriteDescriptorSet wr[3];
	ii.sampler = sc.lsSampler;	// depth comparison, linear
	ii.imageView = sc.lsView;
	ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	Com_Memset( wr, 0, sizeof( wr ) );
	for ( int i = 0; i < 3; i++ ) {
		wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wr[i].dstSet = set;
		wr[i].dstBinding = first + i;
		wr[i].descriptorCount = 1;
		wr[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		if ( i < 2 )
			wr[i].pBufferInfo = &bi[i];
		else
			wr[i].pImageInfo = &ii;
	}
	qvkUpdateDescriptorSets( vk.device, 3, wr, 0, NULL );
}

// the water's waves: an rgba16f 2D array of the three cascades (256 x 256 and its 8 lower levels with water, else 1 x 1:
// every draw set holds it), in GENERAL for good (scene_water.comp writes level 0, blits fill the rest, scene.frag samples
// it), and its repeating trilinear sampler
static qboolean Scene_CreateWavesImage( void )
{
	const uint32_t n = sc.numWater ? 256u : 1u;
	VkImageCreateInfo ic;
	Com_Memset( &ic, 0, sizeof( ic ) );
	ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ic.imageType = VK_IMAGE_TYPE_2D;
	ic.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	ic.extent.width = ic.extent.height = n;
	ic.extent.depth = 1;
	ic.mipLevels = sc.numWater ? 9u : 1u;	// (256 down to 1, blitted each frame after scene_water.comp)
	ic.arrayLayers = 3;
	ic.samples = VK_SAMPLE_COUNT_1_BIT;
	ic.tiling = VK_IMAGE_TILING_OPTIMAL;
	ic.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( qvkCreateImage( vk.device, &ic, NULL, &sc.wavesImage ) != VK_SUCCESS )
		return qfalse;
	VkMemoryRequirements req;
	qvkGetImageMemoryRequirements( vk.device, sc.wavesImage, &req );
	VkMemoryAllocateInfo alloc;
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	if ( vk_vram_allocate_named( vk.device, &alloc, &sc.wavesMem, "scene water waves" ) != VK_SUCCESS )
		return qfalse;
	qvkBindImageMemory( vk.device, sc.wavesImage, sc.wavesMem, 0 );
	VK_SET_OBJECT_NAME( sc.wavesImage, "scene water waves", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
	VkImageViewCreateInfo vc;
	Com_Memset( &vc, 0, sizeof( vc ) );
	vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vc.image = sc.wavesImage;
	vc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	vc.format = ic.format;
	vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vc.subresourceRange.levelCount = ic.mipLevels;
	vc.subresourceRange.layerCount = 3;
	if ( qvkCreateImageView( vk.device, &vc, NULL, &sc.wavesView ) != VK_SUCCESS )
		return qfalse;
	vc.subresourceRange.levelCount = 1;	// the storage view: level 0 alone
	if ( qvkCreateImageView( vk.device, &vc, NULL, &sc.wavesView0 ) != VK_SUCCESS )
		return qfalse;
	sc.wavesLevels = ic.mipLevels;
	VkCommandBuffer cmd = vk_begin_command_buffer();
	VkImageMemoryBarrier ib;
	Com_Memset( &ib, 0, sizeof( ib ) );
	ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	ib.image = sc.wavesImage;
	ib.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	ib.subresourceRange.levelCount = ic.mipLevels;
	ib.subresourceRange.layerCount = 3;
	qvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &ib );
	vk_end_command_buffer( cmd, __func__ );
	Vk_Sampler_Def def;
	Com_Memset( &def, 0, sizeof( def ) );
	def.address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	def.gl_mag_filter = GL_LINEAR;
	def.gl_min_filter = sc.numWater ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;	// (trilinear and anisotropic: a texel under a pixel averages)
	def.noAnisotropy = sc.numWater ? qfalse : qtrue;
	sc.wavesSampler = vk_find_sampler( &def );	// the engine's cache owns it
	return sc.wavesSampler ? qtrue : qfalse;
}

static void Scene_DeviceCommands( void );

// the count-then-scan cull's buffers for slot f at sc.visCap[f] pairs: the visible pool (sc.visible[f][0]), the pairs and
// touched LODs with their header (zeroed), the header's host copy (made once)
static qboolean Scene_CullPoolCreate( int f )
{
	const uint32_t cap = sc.visCap[f];
	if ( !Scene_CreateBuffer( &sc.visible[f][0], (VkDeviceSize)cap * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			"scene visible pool" )
		|| !Scene_CreateBuffer( &sc.cullPairs[f], 64 + (VkDeviceSize)cap * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene cull pairs" ) )
		return qfalse;
	if ( !sc.cullHdr[f].buf ) {
		if ( !Scene_CreateBuffer( &sc.cullHdr[f], 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				"scene cull pool readback" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, sc.cullHdr[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&sc.cullHdrPtr[f] ) );
	}
	Com_Memset( sc.cullHdrPtr[f], 0, 64 );
	Scene_DeviceCommands();
	VkCommandBuffer cmd = vk_begin_command_buffer();
	static const uint32_t zero[16] = { 0 };
	sc.cmdUpdateBuffer( cmd, sc.cullPairs[f].buf, 0, sizeof( zero ), zero );
	vk_end_command_buffer( cmd, __func__ );
	sc.visOverSeen[f] = 0;
	return qtrue;
}

// one buffer into binding b of a set
static void Scene_WriteBinding( VkDescriptorSet set, uint32_t b, const sceneBuffer_t *buf )
{
	if ( !set )
		return;
	VkDescriptorBufferInfo bi = { buf->buf, 0, VK_WHOLE_SIZE };
	VkWriteDescriptorSet w;
	Com_Memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = set;
	w.dstBinding = b;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	w.pBufferInfo = &bi;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
}

// slot f's scatter set (its pool, the LODs, the parts' commands, every view's commands, the visible pool)
static void Scene_CullPoolScatterSet( int f )
{
	Scene_WriteBinding( sc.scatterSet[f], 0, &sc.cullPairs[f] );
	Scene_WriteBinding( sc.scatterSet[f], 1, &sc.lods );
	Scene_WriteBinding( sc.scatterSet[f], 2, &sc.partCmd );
	VkDescriptorBufferInfo bi[SCENE_MAX_VIEWS];
	for ( int v = 0; v < SCENE_MAX_VIEWS; v++ )
		bi[v] = { sc.indirect[f][v].buf, 0, VK_WHOLE_SIZE };
	VkWriteDescriptorSet w;
	Com_Memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = sc.scatterSet[f];
	w.dstBinding = 3;
	w.descriptorCount = SCENE_MAX_VIEWS;
	w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	w.pBufferInfo = bi;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	Scene_WriteBinding( sc.scatterSet[f], 4, &sc.visible[f][0] );
}

static void Scene_CullPoolCheck( int f );

static qboolean Scene_CreateViews( void )
{
	VkDescriptorPoolSize ps[4];
	VkDescriptorPoolCreateInfo dp;
	const int sets = NUM_COMMAND_BUFFERS * SCENE_MAX_VIEWS;

	if ( !Scene_CreateFogVolume() || !Scene_CreateWavesImage() )
		return qfalse;
	ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	// cull 15, draw 16, sort 5 (and 4 spare) a set; per slot the packing set's 3 x SCENE_MAX_VIEWS + 4, the haze's 10, two
	// clusters' 9 (with the view's lights), the pyramid's 1, the touched sort's 5, the light fade's 5, the volume cull's
	// 3, the stand-ins' 4; then headroom for every type (a binding added to a set must not fail the scene's load)
	ps[0].descriptorCount = sets * 40 + NUM_COMMAND_BUFFERS * ( 42 + 3 * SCENE_MAX_VIEWS + 4 ) + 64
		+ NUM_COMMAND_BUFFERS * ( 4 + SCENE_MAX_VIEWS )	// (the count-then-scan cull's scatter set a slot)
		+ sets * 2;	// (the group lists' sets)
	ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	ps[1].descriptorCount = sets + NUM_COMMAND_BUFFERS * 2 + 8;
	ps[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	ps[2].descriptorCount = sets * 12 + NUM_COMMAND_BUFFERS * 12 + 16;	// (+2 a set and a slot: r_sceneAO's)	// draw 8 (the DBuffer's 3 and the waves with them); per slot 10 (the decal pass's depth, the haze's last scatter)
	ps[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	ps[3].descriptorCount = NUM_COMMAND_BUFFERS * 6 + 8;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = sets * 5 + NUM_COMMAND_BUFFERS * 11 + 16;
	dp.poolSizeCount = 4;
	dp.pPoolSizes = ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &sc.pool ) != VK_SUCCESS )
		return qfalse;

	// occlusion: the pyramids' levels, from half the screen down to one texel (sizes rounded up); phase 1's hidden list
	// (the count-then-scan cull's pool capacity: r_sceneVisiblePool pairs a slot, at least 64 K)
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		sc.visCap[f] = (uint32_t)Com_Clamp( 65536, 64 * 1024 * 1024, (float)r_sceneVisiblePool->integer );
		sc.visOverSeen[f] = 0;
	}
	{
		uint32_t w = MAX( ( glConfig.vidWidth + 1 ) / 2, 1 ), h = MAX( ( glConfig.vidHeight + 1 ) / 2, 1 ), total = 0;
		sc.occLevels = 0;
		while ( sc.occLevels < 16 ) {
			sc.occOfs[sc.occLevels] = total;
			sc.occSize[sc.occLevels][0] = w;
			sc.occSize[sc.occLevels][1] = h;
			total += w * h;
			sc.occLevels++;
			if ( w == 1 && h == 1 )
				break;
			w = ( w + 1 ) / 2;
			h = ( h + 1 ) / 2;
		}
		sc.occTotal = total;
		// r_sceneWaterBed: past the two regions, a full-size bed for the water (4 B a pixel), when the map has water
		sc.occBedOfs = ( sc.numWater && r_sceneWaterBed->integer ) ? total * 2 : 0;	// (none when off at load: no memory)
		const VkDeviceSize bed = sc.occBedOfs ? (VkDeviceSize)glConfig.vidWidth * glConfig.vidHeight * 2 : 0;	// (2 words a pixel)
		if ( !Scene_CreateBuffer( &sc.occPyramid, ( (VkDeviceSize)total * 2 + bed ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene occlusion pyramids" )
			|| !Scene_CreateBuffer( &sc.occList, 16 + (VkDeviceSize)MAX( sc.numInstances, 1u ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene occlusion list" )
			|| !Scene_CreateBuffer( &sc.occArgs, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene occlusion dispatches" )
			|| !Scene_CreateBuffer( &sc.occTouched, 16 + (VkDeviceSize)MAX( sc.numCmds, 1u ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
				| VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene occlusion commands" ) )
			return qfalse;
		sc.occSlot = -1;
	}

	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( !Scene_CreateBuffer( &sc.occStats[f], SCENE_OCC_STATS_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene occlusion cameras, counters" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, sc.occStats[f].mem, 0, VK_WHOLE_SIZE, 0, &sc.occStatsPtr[f] ) );
		Com_Memset( sc.occStatsPtr[f], 0, SCENE_OCC_STATS_BYTES );
		sc.occCounted[f] = qfalse;
		sc.clusterCounted[f] = qfalse;
		{
			VkDescriptorSetAllocateInfo da;
			Com_Memset( &da, 0, sizeof( da ) );
			da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			da.descriptorPool = sc.pool;
			da.descriptorSetCount = 1;
			da.pSetLayouts = &sc.occLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.occSetDepth[f] ) != VK_SUCCESS )
				return qfalse;
			VkDescriptorBufferInfo bi = { sc.occPyramid.buf, 0, VK_WHOLE_SIZE };
			VkWriteDescriptorSet w;
			Com_Memset( &w, 0, sizeof( w ) );
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = sc.occSetDepth[f];
			w.dstBinding = 1;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w.pBufferInfo = &bi;
			qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );	// binding 0, the depth attachment: at each build
		}
		if ( !Scene_CreateBuffer( &sc.clusters[f], (VkDeviceSize)SCENE_CLUSTER_X * SCENE_CLUSTER_Y * SCENE_CLUSTER_Z * SCENE_CLUSTER_STRIDE * 4,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene light clusters" )
			|| !Scene_CreateBuffer( &sc.fogClusters[f], (VkDeviceSize)SCENE_CLUSTER_X * SCENE_CLUSTER_Y * SCENE_CLUSTER_Z * SCENE_CLUSTER_STRIDE * 4,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene haze light clusters" ) )
			return qfalse;
		{	// the haze volumes in view: every one, in order, until scene_viewcull.comp writes them (a count, three pads,
			// the indices)
			std::vector<uint32_t> ids( 4 + MAX( sc.numFogVolumes, 1u ), 0 );
			for ( size_t i = 4; i < ids.size(); i++ )
				ids[i] = (uint32_t)( i - 4 );
			ids[0] = sc.numFogVolumes;
			if ( !Scene_StaticBuffer( &sc.volumeList[f], ids.data(), (VkDeviceSize)( 4 + MAX( sc.numFogVolumes, 1u ) ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					"scene haze volumes in view" ) )
				return qfalse;
			sc.viewCullGroups = ( MAX( sc.numFogVolumes, 1u ) + 255 ) / 256;
			if ( !Scene_CreateBuffer( &sc.viewCullCounts[f], (VkDeviceSize)sc.viewCullGroups * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene view cull counts" ) )
				return qfalse;
		}
		if ( !Scene_CreateBuffer( &sc.receivers[f], 112, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene shadow receivers" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, sc.receivers[f].mem, 0, VK_WHOLE_SIZE, 0, &sc.receiversPtr[f] ) );
		if ( !Scene_CreateBuffer( &sc.frame[f], sizeof( sceneFrame_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene frame" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, sc.frame[f].mem, 0, VK_WHOLE_SIZE, 0, &sc.framePtr[f] ) );

		{
			VkDescriptorSetAllocateInfo da;
			Com_Memset( &da, 0, sizeof( da ) );
			da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			da.descriptorPool = sc.pool;
			da.descriptorSetCount = 1;
			da.pSetLayouts = &sc.fogLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.fogSet[f] ) != VK_SUCCESS )
				return qfalse;
			static const VkDescriptorType fogTypes[5] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
			const sceneBuffer_t *fogBufs[5] = { &sc.frame[f], &sc.lights, &sc.cells, &sc.cellLights, &sc.lightBoxes };
			Scene_WriteSet( sc.fogSet[f], fogBufs, fogTypes, 5 );
			VkDescriptorImageInfo ii[2];
			VkWriteDescriptorSet wr[2];
			for ( int i = 0; i < 2; i++ ) {
				ii[i].sampler = VK_NULL_HANDLE;
				ii[i].imageView = sc.fogView[i == 0 && f == 1 ? 2 : i];	// slot 1 writes the second scatter
				ii[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				Com_Memset( &wr[i], 0, sizeof( wr[i] ) );
				wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wr[i].dstSet = sc.fogSet[f];
				wr[i].dstBinding = 5 + i;
				wr[i].descriptorCount = 1;
				wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				wr[i].pImageInfo = &ii[i];
			}
			qvkUpdateDescriptorSets( vk.device, 2, wr, 0, NULL );
			ii[0].sampler = sc.fogSampler;	// the other slot's scatter: last frame's (r_sceneFogTemporal)
			ii[0].imageView = sc.fogView[f == 1 ? 0 : 2];
			wr[0].dstBinding = 15;
			wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			qvkUpdateDescriptorSets( vk.device, 1, wr, 0, NULL );
			{
				VkDescriptorBufferInfo bi = { sc.fogVolumes.buf, 0, VK_WHOLE_SIZE };
				VkWriteDescriptorSet w;
				Com_Memset( &w, 0, sizeof( w ) );
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = sc.fogSet[f];
				w.dstBinding = 7;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w.pBufferInfo = &bi;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
			}
			Scene_WriteLightShadows( sc.fogSet[f], 8 );
			{
				// the sun cascades live as long as the device (vk_create_sun_shadow; a swapchain restart keeps them)
				VkDescriptorImageInfo si = { vk.sunShadow.sampler, vk.sunShadow.array_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
				VkWriteDescriptorSet w;
				Com_Memset( &w, 0, sizeof( w ) );
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = sc.fogSet[f];
				w.dstBinding = 11;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				w.pImageInfo = &si;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
			}
			{
				VkDescriptorBufferInfo bi = { sc.fogClusters[f].buf, 0, VK_WHOLE_SIZE };
				VkWriteDescriptorSet w;
				Com_Memset( &w, 0, sizeof( w ) );
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = sc.fogSet[f];
				w.dstBinding = 12;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w.pBufferInfo = &bi;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
				bi.buffer = sc.lightFade[f].buf;
				w.dstBinding = 13;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
				bi.buffer = sc.volumeList[f].buf;
				w.dstBinding = 14;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
			}
			da.pSetLayouts = &sc.captureLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.captureSet[f] ) != VK_SUCCESS )
				return qfalse;
			da.pSetLayouts = &sc.ssrLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.ssrSet[f] ) != VK_SUCCESS )
				return qfalse;
			if ( !Scene_CreateBuffer( &sc.ssrParams[f], sizeof( sceneSsrParams_t ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene reflection params", qtrue ) )
				return qfalse;
			VK_CHECK( qvkMapMemory( vk.device, sc.ssrParams[f].mem, 0, VK_WHOLE_SIZE, 0, &sc.ssrParamsPtr[f] ) );
		}

		if ( sc.decalLayout ) {	// the decal pass's depth (written as it runs)
			VkDescriptorSetAllocateInfo da;
			Com_Memset( &da, 0, sizeof( da ) );
			da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			da.descriptorPool = sc.pool;
			da.descriptorSetCount = 1;
			da.pSetLayouts = &sc.decalLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.decalSet[f] ) != VK_SUCCESS )
				return qfalse;
		}
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			if ( !Scene_CreateBuffer( &sc.indirect[f][v], (VkDeviceSize)sc.numCmds * sizeof( VkDrawIndexedIndirectCommand ),
					VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene indirect" )
				|| ( v == 0 && !Scene_CullPoolCreate( f ) )	// the slot's visible pool (sc.visible[f][0]) and pairs
				|| !Scene_CreateBuffer( &sc.sortList[f][v], (VkDeviceSize)( 4 + MAX( sc.numCmds, 1u ) ) * 4,	// a LOD has a part at least
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene sort list" ) )
				return qfalse;

			VkDescriptorSetAllocateInfo da;
			Com_Memset( &da, 0, sizeof( da ) );
			da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			da.descriptorPool = sc.pool;
			da.descriptorSetCount = 1;
			da.pSetLayouts = &sc.cullLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.cullSet[f][v] ) != VK_SUCCESS )
				return qfalse;
			da.pSetLayouts = &sc.drawLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.drawSet[f][v] ) != VK_SUCCESS )
				return qfalse;

			static const VkDescriptorType cullTypes[16] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
			static const VkDescriptorType drawTypes[7] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
			if ( v == 0 ) {	// the interiors the eye is in (the cull's), and near (the want's): a bit per group, per slot
				const VkDeviceSize words = 4 * (VkDeviceSize)MAX( ( sc.numInteriors + 31 ) / 32, 1u );
				const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				if ( !Scene_CreateBuffer( &sc.interiorIn[f], words, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, "scene interiors in" )
					|| !Scene_CreateBuffer( &sc.interiorWant[f], words, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, "scene interiors near" ) )
					return qfalse;
				VK_CHECK( qvkMapMemory( vk.device, sc.interiorIn[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&sc.interiorInPtr[f] ) );
				VK_CHECK( qvkMapMemory( vk.device, sc.interiorWant[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&sc.interiorWantPtr[f] ) );
				Com_Memset( sc.interiorInPtr[f], 0, (size_t)words );
				Com_Memset( sc.interiorWantPtr[f], 0, (size_t)words );
			}
			// the view's groups that may hold something it draws (r_sceneCullGroups: scene_groupcull.comp writes it each frame):
			// its count, the cull's indirect dispatch, the groups
			if ( !Scene_CreateBuffer( &sc.cullGroups[f][v], 16 + (VkDeviceSize)MAX( sc.numSubs, 1u ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
					| VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene cull groups" ) )
				return qfalse;
			{	// scene_groupcull.comp's set: the groups, this view's list
				VkDescriptorSetAllocateInfo gda = da;
				gda.pSetLayouts = &sc.groupCullLayout;
				if ( qvkAllocateDescriptorSets( vk.device, &gda, &sc.groupCullSet[f][v] ) != VK_SUCCESS )
					return qfalse;
				const sceneBuffer_t *gb[2] = { &sc.cullSubs, &sc.cullGroups[f][v] };
				Scene_WriteSet( sc.groupCullSet[f][v], gb, cullTypes, 2 );
			}
			const sceneBuffer_t *cullBufs[16] = { &sc.instances, &sc.meshes, &sc.lods, &sc.partCmd, &sc.indirect[f][v], &sc.visible[f][0], &sc.receivers[f],
				&sc.occPyramid, &sc.occStats[f], &sc.occList, &sc.occArgs, &sc.occTouched, &sc.sortList[f][v], &sc.cullGroups[f][v], &sc.interiorIn[f],
				&sc.cullPairs[f] };
			const sceneBuffer_t *drawBufs[7] = { &sc.visible[f][0], &sc.instances, &sc.lights, &sc.cells, &sc.cellLights, &sc.frame[f], &sc.lightBoxes };
			Scene_WriteSet( sc.cullSet[f][v], cullBufs, cullTypes, 16 );
			da.pSetLayouts = &sc.sortLayout;
			if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.sortSet[f][v] ) != VK_SUCCESS )
				return qfalse;
			const sceneBuffer_t *sortBufs[5] = { &sc.lods, &sc.partCmd, &sc.indirect[f][v], &sc.visible[f][0], &sc.sortList[f][v] };
			Scene_WriteSet( sc.sortSet[f][v], sortBufs, cullTypes, 5 );
			if ( v == 0 ) {	// the touched sort reads only its list (binding 4); the others are any view's
				if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.sortTouchedSet[f] ) != VK_SUCCESS )
					return qfalse;
				const sceneBuffer_t *touchedBufs[5] = { &sc.lods, &sc.partCmd, &sc.indirect[f][v], &sc.visible[f][0], &sc.occTouched };
				Scene_WriteSet( sc.sortTouchedSet[f], touchedBufs, cullTypes, 5 );
			}
			Scene_WriteSet( sc.drawSet[f][v], drawBufs, drawTypes, 7 );
			{	// 19: the meshes (scene.vert: an instance's sphere centre, from which its hide and near distances fade)
				VkDescriptorBufferInfo mb = { sc.meshes.buf, 0, VK_WHOLE_SIZE };
				VkWriteDescriptorSet mw;
				Com_Memset( &mw, 0, sizeof( mw ) );
				mw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				mw.dstSet = sc.drawSet[f][v];
				mw.dstBinding = 19;
				mw.descriptorCount = 1;
				mw.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				mw.pBufferInfo = &mb;
				qvkUpdateDescriptorSets( vk.device, 1, &mw, 0, NULL );
			}
			VkDescriptorImageInfo ii;
			VkWriteDescriptorSet wr;
			ii.sampler = sc.fogSampler;
			ii.imageView = sc.fogView[1];
			ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			Com_Memset( &wr, 0, sizeof( wr ) );
			wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr.dstSet = sc.drawSet[f][v];
			wr.dstBinding = 7;
			wr.descriptorCount = 1;
			wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			wr.pImageInfo = &ii;
			qvkUpdateDescriptorSets( vk.device, 1, &wr, 0, NULL );
			for ( int i = 0; i < 2; i++ ) {
				ii.sampler = sc.nearestSampler;	// read texel by texel (scene.frag weighs them by depth)
				ii.imageView = sc.reflectView[i];
				wr.dstBinding = 8 + i;
				qvkUpdateDescriptorSets( vk.device, 1, &wr, 0, NULL );
				ii.imageView = sc.aoView[i];	// 28, 29: r_sceneAO's, likewise
				wr.dstBinding = 28 + i;
				qvkUpdateDescriptorSets( vk.device, 1, &wr, 0, NULL );
			}
			Scene_WriteLightShadows( sc.drawSet[f][v], 10 );
			for ( int i = 0; i < 5; i++ ) {	// 20..22, 24, 25: the DBuffer (scene.frag reads it texel by texel)
				ii.sampler = sc.nearestSampler;
				ii.imageView = sc.decalView[i];
				ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				wr.dstBinding = i < 3 ? 20 + i : 21 + i;
				qvkUpdateDescriptorSets( vk.device, 1, &wr, 0, NULL );
			}
			ii.sampler = sc.wavesSampler;	// 26: the water's waves (scene_water.comp writes them in GENERAL)
			ii.imageView = sc.wavesView;
			ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			wr.dstBinding = 26;
			qvkUpdateDescriptorSets( vk.device, 1, &wr, 0, NULL );
			{
				VkDescriptorBufferInfo bi = { sc.clusters[f].buf, 0, VK_WHOLE_SIZE };
				VkWriteDescriptorSet w;
				Com_Memset( &w, 0, sizeof( w ) );
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = sc.drawSet[f][v];
				w.dstBinding = 16;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w.pBufferInfo = &bi;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
				bi.buffer = sc.lightFade[f].buf;
				w.dstBinding = 17;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
				bi.buffer = sc.distantCells[f].buf;
				w.dstBinding = 18;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
			}
			{
				VkDescriptorBufferInfo bi = { sc.detailParams.buf, 0, VK_WHOLE_SIZE };
				VkWriteDescriptorSet w;
				Com_Memset( &w, 0, sizeof( w ) );
				w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w.dstSet = sc.drawSet[f][v];
				w.dstBinding = 13;
				w.descriptorCount = 1;
				w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w.pBufferInfo = &bi;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
				bi.buffer = sc.occPyramid.buf;	// 23: the water's bed (the frame's waterBed says where)
				w.dstBinding = 23;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
				bi.buffer = sc.w3Stream.buf;	// 27: maps/<map>.xw3b's vertex stream (scene.vert, GPU-driven)
				w.dstBinding = 27;
				qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
			}
		}
	}
	return qtrue;
}

/* ------------------------------------------------------------------ light shadows */

// The source's shadow-casting lights (enableLocalShadows; the light type's bit 2) get static shadow maps, rendered once
// at load: a cone face for a spot of up to 120 degrees, six cube faces (+x, -x, +y, -y, +z, -z) for the rest. The faces
// are square tiles sized by the light's reach (r_sceneLightShadowRes texels for 15 m, powers of two from 32 to twice
// that), packed in Z order into the layers of one depth atlas. Each face is culled by scene_cull.comp like a view and
// drawn with the opaque and cutout materials; emissive ones (a lamp's glowing lens would shadow its own light) and glass
// cast nothing. scene.frag and scene_fog.comp read them back (lightShadow()).

typedef struct {
	float		viewProj[16];	// scene space -> the face's clip space (depth 0 at SCENE_LS_NEAR, 1 at the light's reach)
	float		tile[4];		// its tile: uv scale, uv offset x, y; layer
	float		texel[4];		// x: a texel's width per unit of distance (2 tan(fov / 2) / size); y: a texel in the face's uv
} sceneShadowFace_t;			// scene.frag / scene_fog.comp ShadowFace (std430)

typedef struct {
	float		planes[5][4];	// as a view's: four sides and the light's reach
	vec3_t		origin;
	float		invTanHalfFov;
	float		viewProj[16];
	uint32_t	size, x, y, layer;
	qboolean	rain;			// the rain view: orthographic from above, every LOD 0, light sources cover too
	vec3_t		lo, hi;			// a light face's bounds (its apex and far corners)
	uint32_t	first, count;	// its instances in lsIds (r_sceneLightShadowLists)
	qboolean	listed;
} sceneShadowView_t;

// a face's scene -> clip matrix (column-major, as scene.vert reads it): x along R, y along U, depth along F, from O
static void Scene_FaceMatrix( const vec3_t O, const vec3_t F, const vec3_t R, const vec3_t U, float t, float n, float f, float *m )
{
	const float a = f / ( f - n ), b = -f * n / ( f - n );
	float rows[4][4];
	for ( int k = 0; k < 3; k++ ) {
		rows[0][k] = R[k] / t;
		rows[1][k] = U[k] / t;
		rows[2][k] = a * F[k];
		rows[3][k] = F[k];
	}
	rows[0][3] = -DotProduct( R, O ) / t;
	rows[1][3] = -DotProduct( U, O ) / t;
	rows[2][3] = -a * DotProduct( F, O ) + b;
	rows[3][3] = -DotProduct( F, O );
	for ( int r = 0; r < 4; r++ )
		for ( int c = 0; c < 4; c++ )
			m[c * 4 + r] = rows[r][c];
}

static uint32_t Scene_Compact1By1( uint32_t x )
{
	x &= 0x55555555u;
	x = ( x ^ ( x >> 1 ) ) & 0x33333333u;
	x = ( x ^ ( x >> 2 ) ) & 0x0f0f0f0fu;
	x = ( x ^ ( x >> 4 ) ) & 0x00ff00ffu;
	x = ( x ^ ( x >> 8 ) ) & 0x0000ffffu;
	return x;
}

// the faces of every shadow-casting light, packed into the atlas; lsLights: per light 0, or its first face + 1 with
// bit 31 set for a cube
static void Scene_PlanLightShadows( const xscnLight_t *lights, uint32_t numLights, std::vector<uint32_t> &lsLights,
	std::vector<sceneShadowFace_t> &faces, std::vector<sceneShadowView_t> &views )
{
	static const float cubeF[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	static const float cubeU[6][3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 0, 0 } };
	const float base = Com_Clamp( 16.0f, 1024.0f, r_sceneLightShadowRes->value );
	VkPhysicalDeviceProperties props;
	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
	// the device's layers, and (vk_vram.cpp) no more than r_sceneLightShadowBudget of the VRAM budget: past them the
	// smallest faces are left out, their lights unshadowed
	const VkDeviceSize layerBytes = (VkDeviceSize)SCENE_LS_LAYER * SCENE_LS_LAYER * ( r_sceneLightShadowDepth->integer != 16 ? 4 : 2 );
	const VkDeviceSize share = (VkDeviceSize)( (double)vk_vram_budget( NULL ) * Com_Clamp( 0.0f, 1.0f, r_sceneLightShadowBudget->value ) );
	const uint32_t maxLayers = MIN( MIN( props.limits.maxImageArrayLayers, 256u ), (uint32_t)MAX( share / layerBytes, (VkDeviceSize)1 ) );
	if ( maxLayers < MIN( props.limits.maxImageArrayLayers, 256u ) )
		ri.Printf( PRINT_DEVELOPER, "scene light shadows: at most %u layers (r_sceneLightShadowBudget %.2f of %.0f MB)\n", maxLayers,
			r_sceneLightShadowBudget->value, vk_vram_budget( NULL ) / 1048576.0 );

	lsLights.assign( MAX( numLights, 1u ), 0u );
	faces.clear();
	views.clear();
	if ( !r_sceneLightShadows->integer ) {
		faces.resize( 1 );	// the buffer holds one record either way
		return;
	}
	// the most important lights first, so a full atlas drops the weakest
	std::vector<uint32_t> order;
	for ( uint32_t i = 0; i < numLights; i++ )
		if ( ( (int)lights[i].type & SCENE_LIGHT_SHADOWS ) && lights[i].radius > 2.0f * SCENE_LS_NEAR && lights[i].power > 0.0f )
			order.push_back( i );
	std::stable_sort( order.begin(), order.end(), [&]( uint32_t a, uint32_t b ) {
		return lights[a].power * lights[a].radius > lights[b].power * lights[b].radius; } );

	struct pending_t { uint32_t light, face, size; vec3_t F, R, U; float t; };
	std::vector<pending_t> pend;
	for ( uint32_t i : order ) {
		const xscnLight_t *L = &lights[i];
		const float want = base * L->radius / ( 15.0f * 37.5f );
		uint32_t size = 32;
		while ( size < want && size < (uint32_t)( 2.0f * base ) && size < 1024u )
			size <<= 1;
		const float half = ( (int)L->type & 1 ) ? acosf( Com_Clamp( -1.0f, 1.0f, L->cosOuter ) ) : (float)M_PI;
		if ( half <= DEG2RAD( 60.0f ) ) {
			pending_t q;
			q.light = i;
			q.face = 0;
			q.size = size;
			if ( VectorNormalize2( L->dir, q.F ) == 0.0f )
				continue;	// a spot without a direction: no face to draw (it would take atlas room and render nothing)
			vec3_t up;
			VectorSet( up, 0.0f, 0.0f, 1.0f );
			if ( fabsf( q.F[2] ) > 0.9f )
				VectorSet( up, 1.0f, 0.0f, 0.0f );
			CrossProduct( q.F, up, q.R );
			VectorNormalize( q.R );
			CrossProduct( q.R, q.F, q.U );
			q.t = tanf( MIN( half + DEG2RAD( 4.0f ), DEG2RAD( 80.0f ) ) );
			pend.push_back( q );
		} else {
			for ( uint32_t k = 0; k < 6; k++ ) {
				pending_t q;
				q.light = i;
				q.face = k;
				q.size = size;
				VectorCopy( cubeF[k], q.F );
				VectorCopy( cubeU[k], q.U );
				CrossProduct( q.F, q.U, q.R );
				q.t = 1.02f;	// a little over 90 degrees, so the cube's edges overlap
				pend.push_back( q );
			}
		}
		if ( pend.size() > SCENE_LS_MAX_FACES ) {
			pend.resize( pend.size() - ( half <= DEG2RAD( 60.0f ) ? 1 : 6 ) );
			break;
		}
	}
	// largest tiles first: in Z order, every tile then lands aligned to its size and none overlap
	std::vector<uint32_t> byFace( pend.size() );
	for ( uint32_t k = 0; k < pend.size(); k++ )
		byFace[k] = k;
	std::stable_sort( byFace.begin(), byFace.end(), [&]( uint32_t a, uint32_t b ) { return pend[a].size > pend[b].size; } );
	std::vector<uint32_t> tileX( pend.size() ), tileY( pend.size() ), tileLayer( pend.size() );
	uint64_t at = 0;
	const uint64_t layerArea = (uint64_t)SCENE_LS_LAYER * SCENE_LS_LAYER;
	uint32_t usedFaces = (uint32_t)pend.size();
	for ( uint32_t k = 0; k < pend.size(); k++ ) {
		const pending_t &q = pend[byFace[k]];
		const uint64_t area = (uint64_t)q.size * q.size;
		const uint32_t layer = (uint32_t)( at / layerArea );
		if ( layer >= maxLayers ) {
			usedFaces = k;
			break;
		}
		const uint32_t local = (uint32_t)( ( at % layerArea ) / area );
		tileX[byFace[k]] = Scene_Compact1By1( local ) * q.size;
		tileY[byFace[k]] = Scene_Compact1By1( local >> 1 ) * q.size;
		tileLayer[byFace[k]] = layer;
		at += area;
	}
	std::vector<char> placed( pend.size(), 0 );
	for ( uint32_t k = 0; k < usedFaces; k++ )
		placed[byFace[k]] = 1;

	// faces in light order (a cube's six together), skipping lights that lost a face to a full atlas
	for ( uint32_t k = 0; k < pend.size(); ) {
		const uint32_t light = pend[k].light;
		uint32_t n = 1;
		while ( k + n < pend.size() && pend[k + n].light == light )
			n++;
		qboolean all = qtrue;
		for ( uint32_t j = 0; j < n; j++ )
			if ( !placed[k + j] )
				all = qfalse;
		if ( all ) {
			const xscnLight_t *L = &lights[light];
			lsLights[light] = ( (uint32_t)faces.size() + 1u ) | ( n == 6 ? 0x80000000u : 0u );
			for ( uint32_t j = 0; j < n; j++ ) {
				const pending_t &q = pend[k + j];
				sceneShadowFace_t face;
				sceneShadowView_t view;
				view.rain = qfalse;	// a lamp's face (Scene_RenderLightShadows takes the rain view's path on it)
				view.listed = qfalse;
				view.first = view.count = 0;
				ClearBounds( view.lo, view.hi );
				AddPointToBounds( L->origin, view.lo, view.hi );
				for ( int c = 0; c < 4; c++ ) {
					vec3_t corner;
					for ( int a = 0; a < 3; a++ )
						corner[a] = L->origin[a] + L->radius * ( q.F[a] + ( ( c & 1 ) ? q.t : -q.t ) * q.R[a] + ( ( c & 2 ) ? q.t : -q.t ) * q.U[a] );
					AddPointToBounds( corner, view.lo, view.hi );
				}
				const float nearUnits = MIN( SCENE_LS_NEAR, L->radius * 0.25f );
				Scene_FaceMatrix( L->origin, q.F, q.R, q.U, q.t, nearUnits, L->radius, face.viewProj );
				face.tile[0] = (float)q.size / SCENE_LS_LAYER;
				face.tile[1] = (float)tileX[k + j] / SCENE_LS_LAYER;
				face.tile[2] = (float)tileY[k + j] / SCENE_LS_LAYER;
				face.tile[3] = (float)tileLayer[k + j];
				face.texel[0] = 2.0f * q.t / (float)q.size;
				face.texel[1] = 1.0f / (float)q.size;
				face.texel[2] = face.texel[3] = 0.0f;
				faces.push_back( face );

				Com_Memcpy( view.viewProj, face.viewProj, sizeof( view.viewProj ) );
				const float *side[4][2] = { { q.R, NULL }, { q.R, NULL }, { q.U, NULL }, { q.U, NULL } };
				for ( int s = 0; s < 4; s++ ) {
					// inside a side: toward F, away from the edge's axis (R or U, either sign)
					vec3_t nrm;
					const float sign = ( s & 1 ) ? 1.0f : -1.0f;
					for ( int c = 0; c < 3; c++ )
						nrm[c] = q.F[c] * q.t + sign * side[s][0][c];
					VectorNormalize( nrm );
					VectorCopy( nrm, view.planes[s] );
					view.planes[s][3] = DotProduct( nrm, L->origin );
				}
				VectorScale( q.F, -1.0f, view.planes[4] );
				view.planes[4][3] = -DotProduct( q.F, L->origin ) - L->radius;
				VectorCopy( L->origin, view.origin );
				view.invTanHalfFov = 1.0f / q.t;
				view.size = q.size;
				view.x = tileX[k + j];
				view.y = tileY[k + j];
				view.layer = tileLayer[k + j];
				views.push_back( view );
			}
			sc.numShadowLights++;
		}
		k += n;
	}
	// render a layer's faces together
	std::stable_sort( views.begin(), views.end(), []( const sceneShadowView_t &a, const sceneShadowView_t &b ) { return a.layer < b.layer; } );
	sc.numShadowFaces = (uint32_t)faces.size();
	sc.lsLayers = 0;
	for ( const sceneShadowView_t &v : views )
		sc.lsLayers = MAX( sc.lsLayers, v.layer + 1 );
	if ( faces.empty() )
		faces.resize( 1 );	// the buffer holds one record either way
}

// r_sceneLightShadowLists: each light face's instances, those whose bounding sphere meets its five planes as
// scene_cull.comp tests them (with a margin, so the host's rounding never drops one the GPU would keep), found from a
// grid of the instances' centres. The face's cull then runs over its list alone (counts.z 3) and keeps exactly what
// culling every instance keeps. Far proxies are left out: these faces never draw them.
#define SCENE_LS_CELL			1024.0f	// the grid's cells; a wider instance is tested by every face
// the ocean's plane instances (Scene_ReadTiled: the first and how many; none: 0xffffffff, 0) and whether its one plane
// covers the whole map: their bounds are their own (the instance bounds leave them out), a whole-map plane fitted to the
// rest's drawn extent; no light face lists them
static uint32_t sceneOceanInst = 0xffffffffu, sceneOceanCount = 0;
static qboolean sceneOceanWhole = qfalse;

static qboolean Scene_IsOcean( uint32_t i )
{
	return i >= sceneOceanInst && i - sceneOceanInst < sceneOceanCount ? qtrue : qfalse;
}

static void Scene_ListLightShadows( std::vector<sceneShadowView_t> &views, const xscnInstance_t *inst, const xscnMesh_t *meshes,
	uint32_t numInst, std::vector<uint32_t> &ids )
{
	const double t0 = R_SunProfNow();
	ids.assign( 4, 0u );	// scene_cull.comp's OccList header
	sc.lsListed = 0;
	// the spheres, as scene_cull.comp makes them, and the grid: (cell, instance) sorted by cell
	std::vector<float> sph( (size_t)numInst * 4 );
	std::vector<std::pair<uint64_t, uint32_t>> grid;
	std::vector<uint32_t> big;
	grid.reserve( numInst );
	for ( uint32_t i = 0; i < numInst; i++ ) {
		uint32_t nearBits;
		Com_Memcpy( &nearBits, &inst[i].nearDist, 4 );
		if ( nearBits || Scene_IsOcean( i ) )
			continue;
		const float *M = inst[i].m;
		const xscnMesh_t *mesh = &meshes[inst[i].mesh];
		float *S = &sph[(size_t)i * 4];
		float scale = 0.0f;
		for ( int r = 0; r < 3; r++ )
			S[r] = M[r * 4] * mesh->center[0] + M[r * 4 + 1] * mesh->center[1] + M[r * 4 + 2] * mesh->center[2] + M[r * 4 + 3];
		for ( int col = 0; col < 3; col++ )
			scale = MAX( scale, M[col] * M[col] + M[4 + col] * M[4 + col] + M[8 + col] * M[8 + col] );
		S[3] = mesh->radius * sqrtf( scale );
		if ( S[3] > SCENE_LS_CELL ) {
			big.push_back( i );
			continue;
		}
		const int32_t cx = (int32_t)floorf( S[0] / SCENE_LS_CELL ), cy = (int32_t)floorf( S[1] / SCENE_LS_CELL );
		grid.push_back( std::make_pair( ( (uint64_t)(uint32_t)cx << 32 ) | (uint32_t)cy, i ) );
	}
	std::sort( grid.begin(), grid.end() );

	std::vector<uint32_t> list;
	for ( sceneShadowView_t &v : views ) {
		if ( v.rain )
			continue;
		list.clear();
		auto test = [&]( uint32_t i ) {
			const float *S = &sph[(size_t)i * 4];
			const float margin = S[3] + 1.0f + 1e-4f * S[3];
			for ( int p = 0; p < 5; p++ )
				if ( S[0] * v.planes[p][0] + S[1] * v.planes[p][1] + S[2] * v.planes[p][2] - v.planes[p][3] < -margin )
					return;
			list.push_back( i );
		};
		// the cells an instance centred in can reach the face's bounds from (its radius is a cell at most)
		const int32_t x0 = (int32_t)floorf( ( v.lo[0] - SCENE_LS_CELL ) / SCENE_LS_CELL ), x1 = (int32_t)floorf( ( v.hi[0] + SCENE_LS_CELL ) / SCENE_LS_CELL );
		const int32_t y0 = (int32_t)floorf( ( v.lo[1] - SCENE_LS_CELL ) / SCENE_LS_CELL ), y1 = (int32_t)floorf( ( v.hi[1] + SCENE_LS_CELL ) / SCENE_LS_CELL );
		for ( int32_t x = x0; x <= x1; x++ ) {
			for ( int32_t y = y0; y <= y1; y++ ) {
				const uint64_t key = ( (uint64_t)(uint32_t)x << 32 ) | (uint32_t)y;
				auto it = std::lower_bound( grid.begin(), grid.end(), std::make_pair( key, 0u ) );
				for ( ; it != grid.end() && it->first == key; ++it )
					test( it->second );
			}
		}
		for ( uint32_t i : big )
			test( i );
		v.first = (uint32_t)ids.size() - 4u;
		v.count = (uint32_t)list.size();
		v.listed = qtrue;
		ids.insert( ids.end(), list.begin(), list.end() );
		sc.lsListed += list.size();
	}
	sc.lsListMs = (float)( R_SunProfNow() - t0 );
}

// the rain view: the scene seen from above, orthographic over the area its lights cover (and a margin), drawn into a
// layer of its own after the light faces. scene.frag counts a surface wet only where nothing in this view lies over
// it (rainReach): roofs, canopies, decks and interiors keep their floors dry. A map wider than the view at its most
// (a city: Watson's lights span 3.5 by 4.9 km, the view 437 m) has it follow the eye (r_sceneRainFollow,
// Scene_RainFrame): placed on the eye and drawn again in a frame's commands once the eye is a quarter of it from its
// centre, or once streamed geometry came in (at most once a second); past it every up-facing surface is wet.
#define SCENE_RAIN_MARGIN		16.0f	// units over a surface before something above it counts (curbs, litter, its own bumps)
#define SCENE_RAIN_REDRAW_MS	1000.0	// streamed geometry's redraws at most this often
#define SCENE_RAIN_MIN_RADIUS	18.75f	// the view following the eye leaves out what is under half a metre across (bottles
										// and litter keep no floor dry; 13.6 ms a redraw with them, at the finest LODs)
#define SCENE_RAIN_TILES		4		// r_sceneRainTiles: the layer's world tiles a side (SCENE_LS_LAYER / 4 texels each)
static struct {
	qboolean			follow;		// the map is wider than the view: it follows the eye
	sceneShadowFace_t	face;		// as the faces buffer holds it
	sceneShadowView_t	view;
	float				half, top, bottom;
	qboolean			drawn;		// drawn in a frame's commands where it is now
	qboolean			dirty;		// streamed geometry came in since
	double				at;			// its last draw in a frame's commands
	vec3_t				lodEye;		// drawn in a frame's commands: the eye its LODs are chosen from, as the eye's view does
	float				lodScale;
	uint32_t			draws;		// in a stats second, and the most one took to record
	double				ms;
	// r_sceneRainTiles: the window (its first tile; tiles of tileW units, the world's grid), each slot's world tile, whether
	// it holds it (drawn there) and whether streamed geometry came in since (stale); the held rectangle the face record
	// names (world tiles, hi exclusive; empty: none)
	qboolean			tiled;
	float				tileW;
	int					wx, wy;
	int					slotX[SCENE_RAIN_TILES][SCENE_RAIN_TILES], slotY[SCENE_RAIN_TILES][SCENE_RAIN_TILES];
	qboolean			held[SCENE_RAIN_TILES][SCENE_RAIN_TILES], stale[SCENE_RAIN_TILES][SCENE_RAIN_TILES];
	int					rx0, ry0, rx1, ry1;
	uint32_t			tilesDrawn;	// in a stats second
} sceneRain;
static qboolean	*sceneRainTimed;	// Scene_RainFrame's GPU section, split at its render pass (the culls, then the draw)

// the rain view over the square centred on (cx, cy); depth 0 at the top, 1 at the bottom
static void Scene_PlaceRain( float cx, float cy )
{
	const float half = sceneRain.half, top = sceneRain.top, bottom = sceneRain.bottom, range = MAX( top - bottom, 1.0f );
	const float rows[4][4] = { { 1.0f / half, 0, 0, -cx / half }, { 0, 1.0f / half, 0, -cy / half }, { 0, 0, -1.0f / range, top / range }, { 0, 0, 0, 1.0f } };
	for ( int r = 0; r < 4; r++ )
		for ( int c = 0; c < 4; c++ )
			sceneRain.face.viewProj[c * 4 + r] = rows[r][c];
	Com_Memcpy( sceneRain.view.viewProj, sceneRain.face.viewProj, sizeof( sceneRain.view.viewProj ) );
	const float planes[5][4] = { { 1, 0, 0, cx - half }, { -1, 0, 0, -( cx + half ) }, { 0, 1, 0, cy - half }, { 0, -1, 0, -( cy + half ) },
		{ 0, 0, 1, bottom } };
	Com_Memcpy( sceneRain.view.planes, planes, sizeof( planes ) );
	VectorSet( sceneRain.view.origin, cx, cy, top );
}

static void Scene_PlanRain( const xscnLight_t *lights, uint32_t numLights, std::vector<sceneShadowFace_t> &faces,
	std::vector<sceneShadowView_t> &views )
{
	VkPhysicalDeviceProperties props;
	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
	sc.rainFace = 0;
	Com_Memset( &sceneRain, 0, sizeof( sceneRain ) );
	if ( !r_sceneRain->integer || sc.lsLayers >= MIN( props.limits.maxImageArrayLayers, 256u ) )
		return;
	vec2_t lo = { sc.mins[0], sc.mins[1] }, hi = { sc.maxs[0], sc.maxs[1] };
	if ( numLights ) {
		lo[0] = lo[1] = 1e30f;
		hi[0] = hi[1] = -1e30f;
		for ( uint32_t i = 0; i < numLights; i++ )
			for ( int k = 0; k < 2; k++ ) {
				lo[k] = MIN( lo[k], lights[i].origin[k] );
				hi[k] = MAX( hi[k], lights[i].origin[k] );
			}
	}
	const float want = 0.5f * MAX( hi[0] - lo[0], hi[1] - lo[1] ) + 750.0f;	// 20 m past the lights
	sceneRain.half = Com_Clamp( 1024.0f, 8192.0f, want );
	sceneRain.top = sc.maxs[2] + 64.0f;
	sceneRain.bottom = sc.mins[2] - 64.0f;
	sceneRain.follow = want > sceneRain.half ? qtrue : qfalse;
	sceneRain.tileW = 2.0f * sceneRain.half / SCENE_RAIN_TILES;	// (a whole number of texels: SCENE_LS_LAYER / SCENE_RAIN_TILES)
	Scene_PlaceRain( 0.5f * ( lo[0] + hi[0] ), 0.5f * ( lo[1] + hi[1] ) );

	sceneShadowFace_t &face = sceneRain.face;
	sceneShadowView_t &view = sceneRain.view;
	face.tile[0] = 1.0f;
	face.tile[3] = (float)sc.lsLayers;
	face.texel[0] = 2.0f * sceneRain.half / SCENE_LS_LAYER;	// units per texel
	face.texel[1] = 1.0f / SCENE_LS_LAYER;
	faces.resize( sc.numShadowFaces );	// drops the placeholder record when no light casts
	sc.rainFace = (uint32_t)faces.size() + 1u;
	faces.push_back( face );

	view.invTanHalfFov = 1.0f;
	view.size = SCENE_LS_LAYER;
	view.layer = sc.lsLayers++;
	view.rain = qtrue;
	views.push_back( view );
	sc.rainMargin = SCENE_RAIN_MARGIN / MAX( sceneRain.top - sceneRain.bottom, 1.0f );
	ri.Printf( PRINT_ALL, "scene rain view: %.0f m across, %.0f cm a texel%s\n", 2.0f * sceneRain.half / 37.5f, face.texel[0] / 37.5f * 100.0f,
		sceneRain.follow ? va( ", following the eye (the lights span %.0f m)", 2.0f * ( want - 750.0f ) / 37.5f ) : "" );
}

static VkRenderPass Scene_LightShadowPass( VkFormat format, qboolean load )
{
	VkAttachmentDescription attachment;
	VkAttachmentReference ref;
	VkSubpassDescription subpass;
	VkSubpassDependency deps[2];
	VkRenderPassCreateInfo desc;
	VkRenderPass pass = VK_NULL_HANDLE;

	Com_Memset( &attachment, 0, sizeof( attachment ) );
	attachment.format = format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = load ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ref.attachment = 0;
	ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	Com_Memset( &subpass, 0, sizeof( subpass ) );
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.pDepthStencilAttachment = &ref;
	Com_Memset( deps, 0, sizeof( deps ) );
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
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
	if ( qvkCreateRenderPass( vk.device, &desc, NULL, &pass ) != VK_SUCCESS )
		return VK_NULL_HANDLE;
	return pass;
}

// the atlas (one small layer when nothing casts), its render passes and comparison sampler
static qboolean Scene_CreateLightShadowAtlas( void )
{
	VkFormatProperties fp;
	const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
		| VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, VK_FORMAT_D32_SFLOAT, &fp );
	// D32F where the device can, unless r_sceneLightShadowDepth asks for 16 bits (half the memory: 2 GB to 1 GB for 129 layers)
	sc.lsFormat = ( fp.optimalTilingFeatures & need ) == need && r_sceneLightShadowDepth->integer != 16 ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM;
	const uint32_t extent = ( sc.numShadowFaces || sc.rainFace ) ? SCENE_LS_LAYER : 16u;

	VkImageCreateInfo ic;
	Com_Memset( &ic, 0, sizeof( ic ) );
	ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ic.imageType = VK_IMAGE_TYPE_2D;
	ic.format = sc.lsFormat;
	ic.extent.width = ic.extent.height = extent;
	ic.extent.depth = 1;
	ic.mipLevels = 1;
	ic.arrayLayers = MAX( sc.lsLayers, 1u );
	ic.samples = VK_SAMPLE_COUNT_1_BIT;
	ic.tiling = VK_IMAGE_TILING_OPTIMAL;
	ic.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| ( r_sceneLightShadowHash->integer ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0 );	// read back for the gate's hashes
	ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( qvkCreateImage( vk.device, &ic, NULL, &sc.lsImage ) != VK_SUCCESS )
		return qfalse;
	sc.lsReadable = r_sceneLightShadowHash->integer ? qtrue : qfalse;
	VkMemoryRequirements req;
	qvkGetImageMemoryRequirements( vk.device, sc.lsImage, &req );
	VkMemoryAllocateInfo alloc;
	Com_Memset( &alloc, 0, sizeof( alloc ) );
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	if ( vk_vram_allocate_named( vk.device, &alloc, &sc.lsMem, "scene light shadows" ) != VK_SUCCESS )
		return qfalse;
	qvkBindImageMemory( vk.device, sc.lsImage, sc.lsMem, 0 );
	VK_SET_OBJECT_NAME( sc.lsImage, "scene light shadows", VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );

	VkImageViewCreateInfo vc;
	Com_Memset( &vc, 0, sizeof( vc ) );
	vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vc.image = sc.lsImage;
	vc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	vc.format = sc.lsFormat;
	vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	vc.subresourceRange.levelCount = 1;
	vc.subresourceRange.layerCount = ic.arrayLayers;
	if ( qvkCreateImageView( vk.device, &vc, NULL, &sc.lsView ) != VK_SUCCESS )
		return qfalse;

	sc.lsPass[0] = Scene_LightShadowPass( sc.lsFormat, qfalse );
	sc.lsPass[1] = Scene_LightShadowPass( sc.lsFormat, qtrue );
	if ( !sc.lsPass[0] || !sc.lsPass[1] )
		return qfalse;

	VkSamplerCreateInfo si;
	Com_Memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	si.magFilter = si.minFilter = VK_FILTER_LINEAR;
	si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	si.compareEnable = VK_TRUE;
	si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	return qvkCreateSampler( vk.device, &si, NULL, &sc.lsSampler ) == VK_SUCCESS ? qtrue : qfalse;
}

static void Scene_LsBarrier( VkCommandBuffer cb, VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst, VkAccessFlags dstAccess )
{
	VkMemoryBarrier b;
	Com_Memset( &b, 0, sizeof( b ) );
	b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	b.srcAccessMask = srcAccess;
	b.dstAccessMask = dstAccess;
	qvkCmdPipelineBarrier( cb, src, dst, 0, 1, &b, 0, NULL, 0, NULL );
}

// the count-then-scan cull's pool emptied before a batch of culls (the capacity written; 14, 15 kept: the host's
// readback). The caller's barriers order it after the last reads and before the culls (it is a transfer write)
static void Scene_CullPoolReset( VkCommandBuffer cb, int f )
{
	Scene_DeviceCommands();
	const uint32_t hdr[13] = { 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, sc.visCap[f] };
	sc.cmdUpdateBuffer( cb, sc.cullPairs[f].buf, 0, sizeof( hdr ), hdr );
}

// after a batch of culls: each culled view's LOD ranges allocated in the slot's pool, then its instances scattered into
// them (scene_scatter.comp, a dispatch a view and pass, sized by the culls' counts); the header copied for the host
static void Scene_CullPoolFinish( VkCommandBuffer cb, int f, uint32_t views )
{
	Scene_DeviceCommands();
	const PFN_vkCmdDispatchIndirect dispatchIndirect = sc.cmdDispatchIndirect;
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.scatterPipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.scatterPipeLayout, 0, 1, &sc.scatterSet[f], 0, NULL );
	for ( uint32_t mode = 0; mode < 2; mode++ ) {
		for ( uint32_t v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			if ( !( views & ( 1u << v ) ) )
				continue;
			const uint32_t push[4] = { v, mode, 0, 0 };
			qvkCmdPushConstants( cb, sc.scatterPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), push );
			dispatchIndirect( cb, sc.cullPairs[f].buf, mode ? 32 : 16 );
		}
		Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT );
	}
	if ( sc.cullHdr[f].buf ) {
		VkBufferCopy region = { 0, 0, 64 };
		qvkCmdCopyBuffer( cb, sc.cullPairs[f].buf, sc.cullHdr[f].buf, 1, &region );
		Scene_LsBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT );
	}
}

// one layer's listed faces (n of them, up to SCENE_MAX_VIEWS) in slot f's view buffers: each culled over its own list,
// the views' commands packed into their lists as a frame packs them, then the depth lists drawn, two calls a face
static void Scene_LightShadowBatch( VkCommandBuffer cb, int f, const sceneShadowView_t *const *batch, uint32_t n, VkFramebuffer fb,
	qboolean load, uint32_t extent, qboolean clearTiles = qfalse, const VkDescriptorSet *cullSets = NULL, VkDescriptorSet compactSet = VK_NULL_HANDLE )
{
	const VkDeviceSize cmdBytes = (VkDeviceSize)sc.numCmds * sizeof( VkDrawIndexedIndirectCommand );
	for ( uint32_t v = 0; v < n; v++ ) {
		VkBufferCopy region = { 0, 0, cmdBytes };
		qvkCmdCopyBuffer( cb, sc.cmdTemplate.buf, sc.indirect[f][v].buf, 1, &region );
	}
	// the rain view's tiles (r_sceneRainTiles) culled over the groups that may hold something in them (scene_groupcull.comp,
	// as the frame's views): a tile's cull then runs over its own corner of the city, not every instance
	const qboolean rainGroups = ( n && batch[0]->rain && sceneRain.tiled && r_sceneCullGroups->integer && sc.groupCullPipe && sc.numSubs
		&& sc.cmdDispatchIndirect && sc.cmdUpdateBuffer ) ? qtrue : qfalse;
	if ( rainGroups ) {	// each tile's group list emptied: no groups, then a dispatch of none
		static const uint32_t emptyList[4] = { 0, 0, 1, 1 };
		for ( uint32_t v = 0; v < n; v++ )
			sc.cmdUpdateBuffer( cb, sc.cullGroups[f][v].buf, 0, sizeof( emptyList ), emptyList );
	}
	Scene_CullPoolReset( cb, f );	// (the barrier below orders it before the culls)
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
	sceneCullPush_t pushes[SCENE_MAX_VIEWS];
	for ( uint32_t v = 0; v < n; v++ ) {
		const sceneShadowView_t *view = batch[v];
		sceneCullPush_t push;
		Com_Memcpy( push.planes, view->planes, sizeof( push.planes ) );
		VectorCopy( view->origin, push.lod );
		push.lod[3] = MAX( r_sceneLodBias->value, 0.01f ) * view->invTanHalfFov * MAX( r_sceneShadowLod->value, 0.01f );
		push.cull[0] = 1.5f / (float)view->size / view->invTanHalfFov;	// casters under a texel and a half
		push.cull[1] = push.cull[2] = push.cull[3] = 0.0f;
		push.counts[0] = view->count;
		push.counts[1] = view->first;
		push.counts[2] = 3;
		push.counts[3] = 0;
		if ( view->rain ) {	// the rain view (Scene_RainFrame): every instance tested, the interiors too, each at the LOD the
			// eye's view takes (at its finest, 13.6 ms a redraw while moving); none under half a metre
			VectorCopy( sceneRain.lodEye, push.lod );
			push.lod[3] = sceneRain.lodScale;
			push.cull[0] = 0.0f;
			push.cull[1] = MAX( 1.0f / ( view->viewProj[0] * (float)view->size ), SCENE_RAIN_MIN_RADIUS );
			push.counts[0] = sc.numInstances;
			push.counts[1] = push.counts[2] = 0;
			push.counts[3] = 8;
		}
		push.counts[3] |= v << 8;	// its commands' buffer (scene_scatter.comp)
		if ( rainGroups )
			push.counts[3] |= 4;	// over its group list (scene_cull.comp)
		pushes[v] = push;
	}
	if ( rainGroups ) {	// each tile's groups listed (the faces' cull sets bind the same lists, binding 13), then each culled
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.groupCullPipe );
		for ( uint32_t v = 0; v < n; v++ ) {
			sceneCullPush_t gp = pushes[v];
			gp.counts[0] = sc.numSubs;
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.groupCullPipeLayout, 0, 1, &sc.groupCullSet[f][v], 0, NULL );
			qvkCmdPushConstants( cb, sc.groupCullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( gp ), &gp );
			qvkCmdDispatch( cb, ( sc.numSubs + 63 ) / 64, 1, 1 );
		}
		Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT );
	}
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cull );
	for ( uint32_t v = 0; v < n; v++ ) {
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cullPipeLayout, 0, 1, cullSets ? &cullSets[v] : &sc.cullSet[f][v], 0, NULL );
		qvkCmdPushConstants( cb, sc.cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushes[v] ), &pushes[v] );
		if ( rainGroups )
			sc.cmdDispatchIndirect( cb, sc.cullGroups[f][v].buf, 4 );	// (the list's dispatch: its 2nd to 4th words)
		else if ( pushes[v].counts[0] )
			qvkCmdDispatch( cb, ( pushes[v].counts[0] + 63 ) / 64, 1, 1 );
	}
	Scene_CullPoolFinish( cb, f, ( 1u << n ) - 1u );
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
	uint32_t pc[12];
	Com_Memset( pc, 0, sizeof( pc ) );
	pc[0] = sc.numCmds;
	for ( int k = 0; k < SCENE_LIST_COUNT; k++ )
		pc[4 + k] = sc.listBase[k];
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.compact );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.compactPipeLayout, 0, 1, compactSet ? &compactSet : &sc.compactSet[f][0], 0, NULL );
	if ( r_sceneCompactBlocks->integer && sc.compactBlocks ) {
		pc[9] = sc.compactBlocks;
		for ( uint32_t mode = 2; mode <= 4; mode++ ) {
			pc[2] = mode;
			qvkCmdPushConstants( cb, sc.compactPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
			qvkCmdDispatch( cb, mode == 3 ? n : sc.compactBlocks, mode == 3 ? 1 : n, 1 );
			if ( mode < 4 )
				Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
		}
	} else {
		qvkCmdPushConstants( cb, sc.compactPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdDispatch( cb, n, 1, 1 );
	}
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT );

	if ( sceneRainTimed ) {	// the rain view's section: its culls end here, its draw starts
		vk_gputime_section_end( *sceneRainTimed );
		*sceneRainTimed = vk_gputime_section_begin( "scene rain draw" );
	}
	VkClearValue clear;
	clear.depthStencil.depth = 1.0f;
	clear.depthStencil.stencil = 0;
	VkRenderPassBeginInfo begin;
	Com_Memset( &begin, 0, sizeof( begin ) );
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = load ? sc.lsPass[1] : sc.lsPass[0];
	begin.framebuffer = fb;
	begin.renderArea.extent.width = begin.renderArea.extent.height = extent;
	begin.clearValueCount = load ? 0 : 1;
	begin.pClearValues = &clear;
	qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );
	const VkDeviceSize zero = 0;
	qvkCmdBindVertexBuffers( cb, 0, 1, &sc.vertices.buf, &zero );
	qvkCmdBindIndexBuffer( cb, sc.indices.buf, 0, VK_INDEX_TYPE_UINT32 );
	static const int lists[2] = { SCENE_LIST_DEPTH, SCENE_LIST_DEPTH_CUTOUT }, pipes[2] = { SCENE_GD_LSHADOW, SCENE_GD_LSHADOW_CUTOUT };
	for ( uint32_t v = 0; v < n; v++ ) {
		const sceneShadowView_t *view = batch[v];
		VkViewport vp = { (float)view->x, (float)view->y, (float)view->size, (float)view->size, 0.0f, 1.0f };
		VkRect2D sr = { { (int32_t)view->x, (int32_t)view->y }, { view->size, view->size } };
		qvkCmdSetViewport( cb, 0, 1, &vp );
		qvkCmdSetScissor( cb, 0, 1, &sr );
		if ( clearTiles ) {	// its own tile alone (the layer's other faces stay)
			VkClearAttachment ca;
			Com_Memset( &ca, 0, sizeof( ca ) );
			ca.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			ca.clearValue.depthStencil.depth = 1.0f;
			VkClearRect cr = { sr, 0, 1 };
			qvkCmdClearAttachments( cb, 1, &ca, 1, &cr );
		}
		VkDescriptorSet sets[2] = { sc.drawSet[f][v], sc.texSet };
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.gdPipeLayout, 0, 2, sets, 0, NULL );
		scenePush_t push;
		Com_Memset( &push, 0, sizeof( push ) );
		Com_Memcpy( push.viewProj, view->viewProj, sizeof( push.viewProj ) );
		VectorCopy( view->origin, push.eye );
		for ( int i = 0; i < 2; i++ ) {
			const int k = lists[i];
			if ( !sc.listCap[k] )
				continue;
			qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.gdPipe[pipes[i]] );
			push.info[0] = sc.listBase[k];
			qvkCmdPushConstants( cb, sc.gdPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
			qvkCmdDrawIndexedIndirectCount( cb, sc.lists[f][v].buf, SCENE_LISTS_HEADER + (VkDeviceSize)sc.listBase[k] * sizeof( VkDrawIndexedIndirectCommand ),
				sc.lists[f][v].buf, (VkDeviceSize)k * 4, sc.listCap[k], sizeof( VkDrawIndexedIndirectCommand ) );
		}
	}
	qvkCmdEndRenderPass( cb );
}

// both slots' cull sets read the faces' lists in occList's place, their packing sets cmdInfoLs in cmdInfo's (on), or
// as a frame has them (off)
static void Scene_LightShadowSets( qboolean on )
{
	VkDescriptorBufferInfo ids = { on ? sc.lsIds.buf : sc.occList.buf, 0, VK_WHOLE_SIZE };
	VkDescriptorBufferInfo info = { on ? sc.cmdInfoLs.buf : sc.cmdInfo.buf, 0, VK_WHOLE_SIZE };
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkWriteDescriptorSet w[SCENE_MAX_VIEWS + 1];
		for ( int v = 0; v <= SCENE_MAX_VIEWS; v++ ) {
			Com_Memset( &w[v], 0, sizeof( w[v] ) );
			w[v].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w[v].dstSet = v < SCENE_MAX_VIEWS ? sc.cullSet[f][v] : sc.compactSet[f][0];
			w[v].dstBinding = v < SCENE_MAX_VIEWS ? 9 : 1;
			w[v].descriptorCount = 1;
			w[v].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w[v].pBufferInfo = v < SCENE_MAX_VIEWS ? &ids : &info;
		}
		qvkUpdateDescriptorSets( vk.device, SCENE_MAX_VIEWS + 1, w, 0, NULL );
	}
}

// r_sceneLightShadowLists: the listed faces, a layer's at a time (up to SCENE_MAX_VIEWS, as the per-material path takes
// them), through the GPU-driven lists (Scene_LightShadowBatch). The batches alternate between the two frame slots'
// view buffers, SCENE_LS_CHUNK of a slot's in a submission (a barrier between them), so the host records one slot's
// while the GPU runs the other's. False: the device or the settings leave every face to the per-material path.
#define SCENE_LS_CHUNK			32
static qboolean Scene_LightShadowsListed( const std::vector<sceneShadowView_t> &views, const std::vector<VkFramebuffer> &fbs,
	std::vector<char> &cleared, uint32_t extent, qboolean clearTiles = qfalse )
{
	if ( !r_sceneLightShadowLists->integer || !sc.gd || !sc.gdPipe[SCENE_GD_LSHADOW] || !sc.gdPipe[SCENE_GD_LSHADOW_CUTOUT]
		|| !sc.lsIds.buf || !sc.cmdInfoLs.buf || NUM_COMMAND_BUFFERS < 2 )
		return qfalse;
	std::vector<const sceneShadowView_t *> todo;
	for ( const sceneShadowView_t &v : views )
		if ( v.listed )
			todo.push_back( &v );
	if ( todo.empty() )
		return qfalse;

	// an empty submission first: the recorded uploads flushed and the queue idle (any semaphore a frame left consumed), so
	// the chunks below go on their own fences
	vk_end_command_buffer( vk_begin_command_buffer(), __func__ );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		Com_Memset( sc.receiversPtr[f], 0, 112 );	// as the per-material path's: no receivers, every full mesh, no far proxy
		( (float *)sc.receiversPtr[f] )[19] = -1.0f;
	}
	Scene_LightShadowSets( qtrue );

	VkFence fences[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	VkCommandBuffer cbs[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
	for ( int f = 0; f < 2; f++ ) {
		VkFenceCreateInfo fi;
		Com_Memset( &fi, 0, sizeof( fi ) );
		fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VK_CHECK( qvkCreateFence( vk.device, &fi, NULL, &fences[f] ) );
	}
	size_t next = 0;
	for ( uint32_t chunk = 0; next < todo.size(); chunk++ ) {
		const int f = chunk & 1;
		if ( cbs[f] ) {	// the slot's last chunk done before its buffers are written again
			VK_CHECK( qvkWaitForFences( vk.device, 1, &fences[f], VK_TRUE, UINT64_MAX ) );
			VK_CHECK( qvkResetFences( vk.device, 1, &fences[f] ) );
			qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, &cbs[f] );
		}
		VkCommandBuffer cb = cbs[f] = vk_begin_command_buffer();
		for ( int b = 0; b < SCENE_LS_CHUNK && next < todo.size(); b++ ) {
			const uint32_t layer = todo[next]->layer;
			uint32_t n = 0;
			while ( next + n < todo.size() && n < SCENE_MAX_VIEWS && todo[next + n]->layer == layer )
				n++;
			if ( b )	// the batch before read these buffers (its packing, draws): done before they are written
				Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
					| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
			Scene_LightShadowBatch( cb, f, &todo[next], n, fbs[layer], ( cleared[layer] || clearTiles ) ? qtrue : qfalse, extent, clearTiles );
			cleared[layer] = 1;
			next += n;
		}
		VK_CHECK( qvkEndCommandBuffer( cb ) );
		VkSubmitInfo si;
		Com_Memset( &si, 0, sizeof( si ) );
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cb;
		VK_CHECK( qvkQueueSubmit( vk.queue, 1, &si, fences[f] ) );
	}
	for ( int f = 0; f < 2; f++ ) {
		if ( cbs[f] ) {
			VK_CHECK( qvkWaitForFences( vk.device, 1, &fences[f], VK_TRUE, UINT64_MAX ) );
			qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, &cbs[f] );
		}
		qvkDestroyFence( vk.device, fences[f], NULL );
	}
	Scene_LightShadowSets( qfalse );
	return qtrue;
}

// r_sceneLightShadowHash: the atlas read back, a layer at a time, and each face's tile hashed (FNV-1a over its rows of
// depth texels), in the faces' order, into lshash.txt with the whole atlas's: two loads' files compare the paths
static void Scene_HashLightShadows( const std::vector<sceneShadowView_t> &views, qboolean always = qfalse, std::vector<uint64_t> *hashes = NULL,
	const std::vector<char> *keepFaces = NULL, std::vector<std::vector<byte> > *kept = NULL )
{
	if ( ( !always && !r_sceneLightShadowHash->integer ) || !sc.numShadowFaces || !sc.lsReadable )	// (not readable: made without the cvar)
		return;
	const uint32_t texel = sc.lsFormat == VK_FORMAT_D32_SFLOAT ? 4 : 2;
	const VkDeviceSize layerBytes = (VkDeviceSize)SCENE_LS_LAYER * SCENE_LS_LAYER * texel;
	sceneBuffer_t rb;
	if ( !Scene_CreateBuffer( &rb, layerBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene light shadow readback" ) )
		return;
	void *mapped = NULL;
	VK_CHECK( qvkMapMemory( vk.device, rb.mem, 0, VK_WHOLE_SIZE, 0, &mapped ) );
	const byte *texels = (const byte *)mapped;
	std::vector<uint64_t> faceHash( views.size(), 0 );
	uint64_t all = 14695981039346656037ull;
	for ( uint32_t l = 0; l < sc.lsLayers; l++ ) {
		VkCommandBuffer cb = vk_begin_command_buffer();
		VkImageMemoryBarrier ib;
		Com_Memset( &ib, 0, sizeof( ib ) );
		ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		ib.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ib.image = sc.lsImage;
		ib.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		ib.subresourceRange.levelCount = 1;
		ib.subresourceRange.baseArrayLayer = l;
		ib.subresourceRange.layerCount = 1;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib );
		VkBufferImageCopy copy;
		Com_Memset( &copy, 0, sizeof( copy ) );
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		copy.imageSubresource.baseArrayLayer = l;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent.width = copy.imageExtent.height = SCENE_LS_LAYER;
		copy.imageExtent.depth = 1;
		qvkCmdCopyImageToBuffer( cb, sc.lsImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buf, 1, &copy );
		ib.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		ib.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &ib );
		vk_end_command_buffer( cb, __func__ );
		for ( size_t i = 0; i < views.size(); i++ ) {
			const sceneShadowView_t &v = views[i];
			if ( v.layer != l )
				continue;
			uint64_t h = 14695981039346656037ull;
			for ( uint32_t y = 0; y < v.size; y++ ) {
				const byte *row = texels + ( (size_t)( v.y + y ) * SCENE_LS_LAYER + v.x ) * texel;
				for ( uint32_t b = 0; b < v.size * texel; b++ )
					h = ( h ^ row[b] ) * 1099511628211ull;
			}
			faceHash[i] = h;
			if ( keepFaces && kept && i < keepFaces->size() && (*keepFaces)[i] ) {	// (scenelshash redraw: the near faces' texels)
				std::vector<byte> &t = ( *kept )[i];
				t.resize( (size_t)v.size * v.size * texel );
				for ( uint32_t y = 0; y < v.size; y++ )
					Com_Memcpy( t.data() + (size_t)y * v.size * texel, texels + ( (size_t)( v.y + y ) * SCENE_LS_LAYER + v.x ) * texel, v.size * texel );
			}
		}
		for ( VkDeviceSize b = 0; b < layerBytes; b++ )
			all = ( all ^ texels[b] ) * 1099511628211ull;
	}
	qvkUnmapMemory( vk.device, rb.mem );
	Scene_DestroyBuffer( &rb );
	std::string out = va( "atlas %016llx layers %u faces %u\n", (unsigned long long)all, sc.lsLayers, (unsigned)views.size() );
	for ( size_t i = 0; i < views.size(); i++ )
		out += va( "%u %u %u %u %u %016llx\n", (unsigned)i, views[i].layer, views[i].x, views[i].y, views[i].size, (unsigned long long)faceHash[i] );
	if ( hashes ) {	// (scenelshash redraw: compared there)
		*hashes = faceHash;
		return;
	}
	ri.FS_WriteFile( "lshash.txt", out.c_str(), (int)out.size() );
	ri.Printf( PRINT_ALL, "scene light shadows hash: %016llx (%u layers, %u faces), lshash.txt\n", (unsigned long long)all, sc.lsLayers,
		(unsigned)views.size() );
}

// draws every face into the atlas, up to SCENE_MAX_VIEWS of one layer per submission (frame slot 0's view buffers: nothing
// is in flight at load). With no faces the atlas's one layer is cleared, so the sets always read a valid image.
// r_sceneLightShadowLists takes the light faces first (Scene_LightShadowsListed); this path draws the rest.
static void Scene_RenderLightShadows( const std::vector<sceneShadowView_t> &allViews )
{
	const double t0 = R_SunProfNow();
	const uint32_t layers = MAX( sc.lsLayers, 1u );
	const uint32_t extent = ( sc.numShadowFaces || sc.rainFace ) ? SCENE_LS_LAYER : 16u;
	std::vector<VkImageView> layerViews( layers, VK_NULL_HANDLE );
	std::vector<VkFramebuffer> fbs( layers, VK_NULL_HANDLE );
	for ( uint32_t l = 0; l < layers; l++ ) {
		VkImageViewCreateInfo vc;
		Com_Memset( &vc, 0, sizeof( vc ) );
		vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vc.image = sc.lsImage;
		vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vc.format = sc.lsFormat;
		vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vc.subresourceRange.levelCount = 1;
		vc.subresourceRange.baseArrayLayer = l;
		vc.subresourceRange.layerCount = 1;
		VK_CHECK( qvkCreateImageView( vk.device, &vc, NULL, &layerViews[l] ) );
		VkFramebufferCreateInfo fb;
		Com_Memset( &fb, 0, sizeof( fb ) );
		fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb.renderPass = sc.lsPass[0];
		fb.attachmentCount = 1;
		fb.pAttachments = &layerViews[l];
		fb.width = fb.height = extent;
		fb.layers = 1;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &fb, NULL, &fbs[l] ) );
	}

	// the cull's receivers: no near cascade here, and the static views take the full meshes alone, each of them (a hide
	// distance is the eye's, not a light's) and never a far proxy (scene_cull.comp's travel.w -1); written now, as the
	// buffer holds nothing yet (or a frame of the scene before)
	Com_Memset( sc.receiversPtr[0], 0, 112 );
	( (float *)sc.receiversPtr[0] )[19] = -1.0f;

	std::vector<char> cleared( layers, 0 );
	const qboolean listed = Scene_LightShadowsListed( allViews, fbs, cleared, extent );
	const double tListed = R_SunProfNow();
	std::vector<sceneShadowView_t> views;
	for ( const sceneShadowView_t &v : allViews )
		if ( !listed || !v.listed )
			views.push_back( v.rain ? sceneRain.view : v );	// the rain view where it is now (Scene_RainFrame moves it)
	double waited = 0.0;	// in this path's submissions (the rest of its time: recording them)
	const VkDeviceSize cmdBytes = (VkDeviceSize)sc.numCmds * sizeof( VkDrawIndexedIndirectCommand );
	const VkDeviceSize zero = 0;
	size_t next = 0;
	do {
		const uint32_t layer = views.empty() ? 0u : views[next].layer;
		uint32_t n = 0;
		while ( next + n < views.size() && n < SCENE_MAX_VIEWS && views[next + n].layer == layer )
			n++;
		VkCommandBuffer cb = vk_begin_command_buffer();
		if ( n ) {
			for ( uint32_t v = 0; v < n; v++ ) {
				VkBufferCopy region = { 0, 0, cmdBytes };
				qvkCmdCopyBuffer( cb, sc.cmdTemplate.buf, sc.indirect[0][v].buf, 1, &region );
			}
			Scene_CullPoolReset( cb, 0 );	// slot 0's pool emptied (the barrier below orders it before the culls)
			VkMemoryBarrier b;
			Com_Memset( &b, 0, sizeof( b ) );
			b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &b, 0, NULL, 0, NULL );
			qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cull );
			for ( uint32_t v = 0; v < n; v++ ) {
				const sceneShadowView_t *view = &views[next + v];
				sceneCullPush_t push;
				Com_Memcpy( push.planes, view->planes, sizeof( push.planes ) );
				VectorCopy( view->origin, push.lod );
				push.lod[3] = MAX( r_sceneLodBias->value, 0.01f ) * view->invTanHalfFov * MAX( r_sceneShadowLod->value, 0.01f );
				push.cull[0] = 1.5f / (float)view->size / view->invTanHalfFov;	// casters under a texel and a half
				push.cull[1] = push.cull[2] = push.cull[3] = 0.0f;
				push.counts[0] = sc.numInstances;
				push.counts[1] = push.counts[2] = push.counts[3] = 0;
				if ( view->rain ) {
					// orthographic: every mesh at its finest LOD, none dropped for distance; only those under half a texel;
					// the interiors too (their ceilings keep their floors dry), whether or not the eye is in them
					push.lod[3] = 1e6f;
					push.cull[0] = 0.0f;
					push.cull[1] = 1.0f / ( view->viewProj[0] * (float)view->size );
					push.counts[3] = 8;
				}
				push.counts[3] |= v << 8;	// its commands' buffer (scene_scatter.comp)
				qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cullPipeLayout, 0, 1, &sc.cullSet[0][v], 0, NULL );
				qvkCmdPushConstants( cb, sc.cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
				qvkCmdDispatch( cb, ( sc.numInstances + 63 ) / 64, 1, 1 );
			}
			Scene_CullPoolFinish( cb, 0, ( 1u << n ) - 1u );	// the views' instances scattered into their LOD ranges
			b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			b.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
			qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
				0, 1, &b, 0, NULL, 0, NULL );
		}

		VkClearValue clear;
		clear.depthStencil.depth = 1.0f;
		clear.depthStencil.stencil = 0;
		VkRenderPassBeginInfo begin;
		Com_Memset( &begin, 0, sizeof( begin ) );
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = cleared[layer] ? sc.lsPass[1] : sc.lsPass[0];
		begin.framebuffer = fbs[layer];
		begin.renderArea.extent.width = begin.renderArea.extent.height = extent;
		begin.clearValueCount = cleared[layer] ? 0 : 1;
		begin.pClearValues = &clear;
		qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );
		cleared[layer] = 1;
		if ( n ) {
			qvkCmdBindVertexBuffers( cb, 0, 1, &sc.vertices.buf, &zero );
			qvkCmdBindIndexBuffer( cb, sc.indices.buf, 0, VK_INDEX_TYPE_UINT32 );
		}
		for ( uint32_t v = 0; v < n; v++ ) {
			const sceneShadowView_t *view = &views[next + v];
			VkViewport vp = { (float)view->x, (float)view->y, (float)view->size, (float)view->size, 0.0f, 1.0f };
			VkRect2D sr = { { (int32_t)view->x, (int32_t)view->y }, { view->size, view->size } };
			qvkCmdSetViewport( cb, 0, 1, &vp );
			qvkCmdSetScissor( cb, 0, 1, &sr );
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 0, 1, &sc.drawSet[0][v], 0, NULL );
			int bound = -1;
			for ( uint32_t i = 0; i < sc.numMaterials; i++ ) {
				const sceneMaterial_t *m = &sc.materials[i];
				// light sources cover from the rain too (a lit canopy), though they would shadow their own light
				if ( !m->numCmds || ( m->kind != SCENE_KIND_OPAQUE && m->kind != SCENE_KIND_CUTOUT && !( view->rain && m->kind == SCENE_KIND_EMISSIVE ) ) )
					continue;
				const int pipe = m->kind == SCENE_KIND_CUTOUT ? SCENE_PIPE_LSHADOW_CUTOUT : SCENE_PIPE_LSHADOW;
				if ( pipe != bound ) {
					qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.pipe[pipe] );
					bound = pipe;
				}
				if ( pipe == SCENE_PIPE_LSHADOW_CUTOUT ) {
					VkDescriptorSet sets[3] = { m->image[0]->descriptor_set, m->image[1]->descriptor_set, m->image[2]->descriptor_set };
					qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 1, 3, sets, 0, NULL );
				}
				scenePush_t push;
				Com_Memset( &push, 0, sizeof( push ) );
				Com_Memcpy( push.viewProj, view->viewProj, sizeof( push.viewProj ) );
				Com_Memcpy( push.tint, m->tint, sizeof( push.tint ) );
				VectorCopy( view->origin, push.eye );	// scene.vert pulls emissive layers toward it
				push.info[0] = m->kind;
				push.info[1] = m->imageBits;
				push.info[2] = i;
				qvkCmdPushConstants( cb, sc.drawPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
				const VkDeviceSize at = (VkDeviceSize)m->firstCmd * sizeof( VkDrawIndexedIndirectCommand );
				if ( vk.multiDrawIndirect )
					qvkCmdDrawIndexedIndirect( cb, sc.indirect[0][v].buf, at, m->numCmds, sizeof( VkDrawIndexedIndirectCommand ) );
				else
					for ( uint32_t c = 0; c < m->numCmds; c++ )
						qvkCmdDrawIndexedIndirect( cb, sc.indirect[0][v].buf, at + c * sizeof( VkDrawIndexedIndirectCommand ), 1, sizeof( VkDrawIndexedIndirectCommand ) );
			}
		}
		qvkCmdEndRenderPass( cb );
		const double w0 = R_SunProfNow();
		vk_end_command_buffer( cb, __func__ );
		waited += R_SunProfNow() - w0;
		next += n;
	} while ( next < views.size() );
	// a layer no face landed in (none: the faces fill layers in order) would stay undefined; the sets read every layer
	for ( uint32_t l = 0; l < layers; l++ ) {
		if ( !cleared[l] ) {
			VkCommandBuffer cb = vk_begin_command_buffer();
			VkClearValue clear;
			clear.depthStencil.depth = 1.0f;
			clear.depthStencil.stencil = 0;
			VkRenderPassBeginInfo begin;
			Com_Memset( &begin, 0, sizeof( begin ) );
			begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			begin.renderPass = sc.lsPass[0];
			begin.framebuffer = fbs[l];
			begin.renderArea.extent.width = begin.renderArea.extent.height = extent;
			begin.clearValueCount = 1;
			begin.pClearValues = &clear;
			qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );
			qvkCmdEndRenderPass( cb );
			vk_end_command_buffer( cb, __func__ );
		}
	}
	for ( uint32_t l = 0; l < layers; l++ ) {
		qvkDestroyFramebuffer( vk.device, fbs[l], NULL );
		qvkDestroyImageView( vk.device, layerViews[l], NULL );
	}
	if ( sc.numShadowFaces ) {
		const double now = R_SunProfNow();
		ri.Printf( PRINT_ALL, "scene light shadows: %u lights, %u faces in %u layers of %d (%.0f MB, %s), %.2f s; %s\n", sc.numShadowLights,
			sc.numShadowFaces, sc.lsLayers, SCENE_LS_LAYER, (double)layers * SCENE_LS_LAYER * SCENE_LS_LAYER * ( sc.lsFormat == VK_FORMAT_D32_SFLOAT ? 4 : 2 ) / 1048576.0,
			sc.lsFormat == VK_FORMAT_D32_SFLOAT ? "D32F" : "D16", ( now - t0 ) * 0.001,
			listed ? va( "listed %.2f s (%.1f M instances in the faces' lists, built in %.2f s), %u views per material %.2f s (waited %.2f s)",
				( tListed - t0 ) * 0.001, sc.lsListed / 1e6, sc.lsListMs * 0.001, (unsigned)views.size(), ( now - tListed ) * 0.001, waited * 0.001 )
			: va( "per material: %u views, recording %.2f s, waiting %.2f s", (unsigned)views.size(), ( now - t0 - waited ) * 0.001, waited * 0.001 ) );
	}
	Scene_HashLightShadows( allViews );
}

void Scene_ImageBarrier( VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
	VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage )
{
	VkImageMemoryBarrier ib;
	Com_Memset( &ib, 0, sizeof( ib ) );
	ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	ib.srcAccessMask = srcAccess;
	ib.dstAccessMask = dstAccess;
	ib.oldLayout = from;
	ib.newLayout = to;
	ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	ib.image = image;
	ib.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	ib.subresourceRange.levelCount = 1;
	ib.subresourceRange.layerCount = 1;
	qvkCmdPipelineBarrier( cb, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &ib );
}

// maps/<name>.xdet (tools/levelport/cp2gltf.py): this header, then per material (in the scene's order) 80 bytes: its
// detail image (64 B path, empty: none; the main layer of a multilayered material: rg its normal's slope, b its
// brightness over its mean / 2, a its roughness over its mean + 0.5), its tiles per UV unit, normal strength, roughness
// range and colour strength. The bake holds that layer's mean, and in its ORM's red where the layer shows.
// Version 2 (detail slots): pad is the count of slot images, 64 B paths (detail images as above) after the header;
// then per material 288 bytes: 8 slots of {u32 its slot image (0xffffffff empty), f32 tiles per UV unit, normal
// strength, roughness range, colour strength}, then two 64 B share maps (a slot's share a channel, raw: map 0 slots
// 0-3, map 1 slots 4-7, empty with 4 or fewer; UV0). The bake holds each slot layer's mean; its ORM's red is 1.
typedef struct { char ident[4]; uint32_t version, count, pad; } xdetHeader_t;
// version 2's images in the order the GPU-driven image array starts with (the indices a material's block holds), and
// per material its slot images, share maps and tiles (for the streaming; empty without version 2)
typedef struct { image_t *slot[8], *share[2]; float tile[8]; } sceneSlotMat_t;
std::vector<image_t *> sceneSlotImages;
static std::vector<sceneSlotMat_t> sceneSlotMats;

uint32_t sceneFileVertices;	// the .xscn's vertex count (0: a tiled or streamed scene)

static qboolean Scene_LoadDetail( const char *scnName )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	std::vector<float> params( 4u * MAX( sc.numMaterials, 1u ), 0.0f );
	int missing = 0;

	sc.numDetailed = 0;
	sceneSlotImages.clear();
	sceneSlotMats.clear();
	uint32_t numSlotted = 0, numSlots = 0;
	for ( uint32_t i = 0; i < sc.numMaterials; i++ )
		sc.materials[i].detail = tr.whiteImage;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xdet" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len >= (int)sizeof( xdetHeader_t ) ) {
		const xdetHeader_t *h = (const xdetHeader_t *)buf;
		if ( !memcmp( h->ident, "XDET", 4 ) && h->version == 2 && h->count == sc.numMaterials
			&& (uint64_t)len >= sizeof( xdetHeader_t ) + (uint64_t)h->pad * 64u + (uint64_t)h->count * 288u ) {
			const byte *table = (const byte *)buf + sizeof( xdetHeader_t );
			std::vector<image_t *> tableImages( h->pad, NULL );
			std::vector<char> tableTried( h->pad, 0 );
			// an image's index in the GPU-driven array's start (each once); -1 none
			auto slotIndex = [&]( image_t *image ) -> float {
				for ( size_t k = 0; k < sceneSlotImages.size(); k++ )
					if ( sceneSlotImages[k] == image )
						return (float)k;
				sceneSlotImages.push_back( image );
				return (float)( sceneSlotImages.size() - 1 );
			};
			auto pathImage = [&]( const byte *p ) -> image_t * {
				char path[65];
				Com_Memcpy( path, p, 64 );
				path[64] = '\0';
				if ( !path[0] )
					return NULL;
				qboolean found;
				image_t *image = Scene_Image( path, &found );
				if ( !found ) {
					missing++;
					return NULL;
				}
				return image;
			};
			sceneSlotMats.assign( sc.numMaterials, sceneSlotMat_t() );
			Com_Memset( sceneSlotMats.data(), 0, sizeof( sceneSlotMat_t ) * sc.numMaterials );
			for ( uint32_t i = 0; i < h->count; i++ ) {
				const byte *rec = table + (size_t)h->pad * 64u + (size_t)i * 288u;
				image_t *share0 = pathImage( rec + 160 );
				if ( !share0 )
					continue;
				image_t *share1 = pathImage( rec + 224 );
				sceneSlotMat_t *sm = &sceneSlotMats[i];
				float block[44];
				Com_Memset( block, 0, sizeof( block ) );
				int used = 0;
				for ( int j = 0; j < 8; j++ ) {
					uint32_t t;
					float p[4];
					Com_Memcpy( &t, rec + j * 20, 4 );
					Com_Memcpy( p, rec + j * 20 + 4, 16 );
					if ( t >= h->pad || p[0] <= 0.0f || ( j >= 4 && !share1 ) )
						continue;
					if ( !tableTried[t] ) {
						tableTried[t] = 1;
						tableImages[t] = pathImage( table + (size_t)t * 64u );
					}
					if ( !tableImages[t] )
						continue;
					Com_Memcpy( &block[j * 4], p, 16 );
					block[32 + j] = slotIndex( tableImages[t] );
					sm->slot[j] = tableImages[t];
					sm->tile[j] = p[0];
					used = j + 1;
					numSlots++;
				}
				if ( !used )
					continue;
				sm->share[0] = share0;
				sm->share[1] = share1;
				block[40] = slotIndex( share0 );
				block[41] = share1 ? slotIndex( share1 ) : -1.0f;
				float *d = &params[i * 4u];
				d[0] = -1.0f;	// version 2: its block (scene.frag)
				d[1] = (float)( params.size() / 4u );
				d[2] = (float)used;
				d[3] = 0.0f;
				params.insert( params.end(), block, block + 44 );
				sc.numDetailed++;
				numSlotted++;
			}
		} else if ( !memcmp( h->ident, "XDET", 4 ) && h->version == 1 && h->count == sc.numMaterials
			&& (uint64_t)len >= sizeof( xdetHeader_t ) + (uint64_t)h->count * 80u ) {
			for ( uint32_t i = 0; i < h->count; i++ ) {
				const byte *rec = (const byte *)buf + sizeof( xdetHeader_t ) + i * 80u;
				char path[65];
				Com_Memcpy( path, rec, 64 );
				path[64] = '\0';
				if ( !path[0] )
					continue;
				qboolean found;
				image_t *image = Scene_Image( path, &found );
				if ( !found ) {
					missing++;
					continue;
				}
				sc.materials[i].detail = image;
				Com_Memcpy( &params[i * 4u], rec + 64, 16 );
				sc.numDetailed++;
			}
		} else {
			ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 or 2 detail file for this scene's materials, ignored\n", name );
		}
	}
	if ( buf )
		ri.FS_FreeFile( buf );
	if ( sc.numDetailed || missing )
		ri.Printf( PRINT_ALL, "scene detail %s: %u materials (%d images missing)\n", name, sc.numDetailed, missing );
	if ( numSlotted )
		ri.Printf( PRINT_ALL, "scene detail slots: %u materials, %u slots, %u slot and share images%s\n", numSlotted, numSlots,
			(uint32_t)sceneSlotImages.size(), vk.gpuDriven ? "" : " (drawn without them: the GPU-driven draws read them)" );
	Scene_LoadW3Blend( scnName, params );	// Witcher 3 layer blends (maps/<map>.xw3b): after the detail slots, sharing their images
	Scene_StreamDetailTiles( params );	// the detail layers' tiling (stage C: a detail image wants its material's level times it)
	if ( sc.numWater )
		Scene_LoadWater( scnName, params );
	Scene_LoadScreens( scnName, params );	// the screens' records (maps/<map>.xscr) after the rest
	Scene_LoadFx( scnName, params );		// the particles' emitters and placements (maps/<map>.xfx) last
	if ( params.size() / 4u > SCENE_DETAIL_EXACT )
		ri.Printf( PRINT_WARNING, "WARNING: scene material detail: %u vec4s, past %u: the last records' indices are not exact\n",
			(uint32_t)( params.size() / 4u ), SCENE_DETAIL_EXACT );
	return Scene_StaticBuffer( &sc.detailParams, params.data(), (VkDeviceSize)params.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		"scene material detail" );
}

/* ------------------------------------------------------------------ load */

static void Scene_FreeCpuState( void );

void R_SceneFree( void )
{
	sceneMoonImage = NULL;
	if ( !vk.device ) {	// (nothing on the device: the CPU side alone)
		Scene_StreamFree();
		Scene_FreeCpuState();
		return;
	}
	Scene_MeterDestroy();
	if ( sc.pool || sc.vertices.buf || sc.cull )
		qvkDeviceWaitIdle( vk.device );

	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( sc.interiorInPtr[f] )
			qvkUnmapMemory( vk.device, sc.interiorIn[f].mem );
		if ( sc.interiorWantPtr[f] )
			qvkUnmapMemory( vk.device, sc.interiorWant[f].mem );
		Scene_DestroyBuffer( &sc.interiorIn[f] );
		Scene_DestroyBuffer( &sc.interiorWant[f] );
		if ( sc.rotPtr[f] )
			qvkUnmapMemory( vk.device, sc.rotPatch[f].mem );
		sc.rotPtr[f] = NULL;
		Scene_DestroyBuffer( &sc.rotPatch[f] );
		sc.rotBased[f] = qfalse;
		if ( sc.framePtr[f] )
			qvkUnmapMemory( vk.device, sc.frame[f].mem );
		sc.framePtr[f] = NULL;
		Scene_DestroyBuffer( &sc.frame[f] );
		if ( sc.receiversPtr[f] )
			qvkUnmapMemory( vk.device, sc.receivers[f].mem );
		sc.receiversPtr[f] = NULL;
		Scene_DestroyBuffer( &sc.receivers[f] );
		if ( sc.occStatsPtr[f] )
			qvkUnmapMemory( vk.device, sc.occStats[f].mem );
		sc.occStatsPtr[f] = NULL;
		Scene_DestroyBuffer( &sc.occStats[f] );
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			if ( sc.cullGroupsPtr[f][v] )
				qvkUnmapMemory( vk.device, sc.cullGroups[f][v].mem );
			Scene_DestroyBuffer( &sc.cullGroups[f][v] );
			Scene_DestroyBuffer( &sc.indirect[f][v] );
			Scene_DestroyBuffer( &sc.visible[f][v] );
			Scene_DestroyBuffer( &sc.sortList[f][v] );
		}
		Scene_DestroyBuffer( &sc.cullPairs[f] );
		if ( sc.cullHdrPtr[f] )
			qvkUnmapMemory( vk.device, sc.cullHdr[f].mem );
		sc.cullHdrPtr[f] = NULL;
		Scene_DestroyBuffer( &sc.cullHdr[f] );
	}
	sceneBuffer_t *bufs[] = { &sc.vertices, &sc.indices, &sc.instances, &sc.meshes, &sc.lods, &sc.partCmd, &sc.cmdTemplate,
		&sc.lights, &sc.lightBounds, &sc.lightKinds, &sc.cells, &sc.cellLights, &sc.lightBoxes, &sc.fogVolumes, &sc.detailParams, &sc.w3Stream, &sc.lsLights, &sc.lsFaces, &sc.lsIds, &sc.occPyramid, &sc.occList, &sc.occArgs, &sc.occTouched };
	for ( size_t i = 0; i < ARRAY_LEN( bufs ); i++ )
		Scene_DestroyBuffer( bufs[i] );
	if ( sc.lsView ) qvkDestroyImageView( vk.device, sc.lsView, NULL );
	if ( sc.lsImage ) qvkDestroyImage( vk.device, sc.lsImage, NULL );
	if ( sc.lsMem ) qvkFreeMemory( vk.device, sc.lsMem, NULL );
	for ( int i = 0; i < 2; i++ )
		if ( sc.lsPass[i] ) qvkDestroyRenderPass( vk.device, sc.lsPass[i], NULL );
	if ( sc.lsSampler ) qvkDestroySampler( vk.device, sc.lsSampler, NULL );

	for ( int i = 0; i < SCENE_PIPE_COUNT; i++ ) {
		if ( sc.pipe[i] )
			qvkDestroyPipeline( vk.device, sc.pipe[i], NULL );
		sc.pipe[i] = VK_NULL_HANDLE;
	}
	if ( sc.pipeDepthFade ) qvkDestroyPipeline( vk.device, sc.pipeDepthFade, NULL );
	sc.pipeDepthFade = VK_NULL_HANDLE;
	if ( sc.cull ) qvkDestroyPipeline( vk.device, sc.cull, NULL );
	if ( sc.scatterPipe ) qvkDestroyPipeline( vk.device, sc.scatterPipe, NULL );
	Scene_RotateFree();
	if ( sc.scatterPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.scatterPipeLayout, NULL );
	if ( sc.scatterLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.scatterLayout, NULL );
	if ( sc.groupCullPipe ) qvkDestroyPipeline( vk.device, sc.groupCullPipe, NULL );
	if ( sc.groupCullPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.groupCullPipeLayout, NULL );
	if ( sc.groupCullLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.groupCullLayout, NULL );
	Scene_DestroyBuffer( &sc.cullSubs );
	if ( sc.occPipeDepth ) qvkDestroyPipeline( vk.device, sc.occPipeDepth, NULL );
	for ( int i = 0; i < 3; i++ ) {
		if ( i < 2 && sc.fogPipe[i] ) qvkDestroyPipeline( vk.device, sc.fogPipe[i], NULL );
		if ( sc.fogView[i] ) qvkDestroyImageView( vk.device, sc.fogView[i], NULL );
		if ( sc.fogImage[i] ) qvkDestroyImage( vk.device, sc.fogImage[i], NULL );
		if ( sc.fogMem[i] ) qvkFreeMemory( vk.device, sc.fogMem[i], NULL );
	}
	if ( sc.fogPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.fogPipeLayout, NULL );
	if ( sc.capturePipe ) qvkDestroyPipeline( vk.device, sc.capturePipe, NULL );
	for ( int i = 0; i < 2; i++ )
		if ( sc.fxPipe[i] ) qvkDestroyPipeline( vk.device, sc.fxPipe[i], NULL );
	if ( sc.capturePipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.capturePipeLayout, NULL );
	if ( sc.captureLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.captureLayout, NULL );
	if ( sc.historyView ) qvkDestroyImageView( vk.device, sc.historyView, NULL );
	if ( sc.historyStoreView ) qvkDestroyImageView( vk.device, sc.historyStoreView, NULL );
	if ( sc.historyDepthView ) qvkDestroyImageView( vk.device, sc.historyDepthView, NULL );
	if ( sc.historyDepthImage ) qvkDestroyImage( vk.device, sc.historyDepthImage, NULL );
	if ( sc.historyDepthMem ) qvkFreeMemory( vk.device, sc.historyDepthMem, NULL );
	if ( sc.historyImage ) qvkDestroyImage( vk.device, sc.historyImage, NULL );
	if ( sc.historyMem ) qvkFreeMemory( vk.device, sc.historyMem, NULL );
	for ( int i = 0; i < 2; i++ ) {
		if ( sc.reflectView[i] ) qvkDestroyImageView( vk.device, sc.reflectView[i], NULL );
		if ( sc.reflectImage[i] ) qvkDestroyImage( vk.device, sc.reflectImage[i], NULL );
		if ( sc.reflectMem[i] ) qvkFreeMemory( vk.device, sc.reflectMem[i], NULL );
		if ( sc.aoView[i] ) qvkDestroyImageView( vk.device, sc.aoView[i], NULL );
		if ( sc.aoImage[i] ) qvkDestroyImage( vk.device, sc.aoImage[i], NULL );
		if ( sc.aoMem[i] ) qvkFreeMemory( vk.device, sc.aoMem[i], NULL );
	}
	if ( sc.ssrPipe ) qvkDestroyPipeline( vk.device, sc.ssrPipe, NULL );
	if ( sc.ssrPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.ssrPipeLayout, NULL );
	if ( sc.ssrLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.ssrLayout, NULL );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( sc.ssrParamsPtr[f] )
			qvkUnmapMemory( vk.device, sc.ssrParams[f].mem );
		Scene_DestroyBuffer( &sc.ssrParams[f] );
	}
	Scene_FreeGrade();
	Scene_FreeDecals();
	if ( sc.fogLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.fogLayout, NULL );
	if ( sc.cullPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.cullPipeLayout, NULL );
	if ( sc.sortPipe ) qvkDestroyPipeline( vk.device, sc.sortPipe, NULL );
	if ( sc.sortTouchedPipe ) qvkDestroyPipeline( vk.device, sc.sortTouchedPipe, NULL );
	if ( sc.sortPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.sortPipeLayout, NULL );
	if ( sc.sortLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.sortLayout, NULL );
	if ( sc.drawPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.drawPipeLayout, NULL );
	if ( sc.cullLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.cullLayout, NULL );
	if ( sc.occPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.occPipeLayout, NULL );
	if ( sc.occLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.occLayout, NULL );
	if ( sc.drawLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.drawLayout, NULL );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ )
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			Scene_DestroyBuffer( &sc.lists[f][v] );
			Scene_DestroyBuffer( &sc.listMaterial[f][v] );
		}
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ )
		Scene_DestroyBuffer( &sc.blockSums[f] );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		Scene_DestroyBuffer( &sc.clusters[f] );
		Scene_DestroyBuffer( &sc.viewLights[f] );
		Scene_DestroyBuffer( &sc.viewLightCounts[f] );
		Scene_DestroyBuffer( &sc.fogClusters[f] );
		Scene_DestroyBuffer( &sc.lightFade[f] );
		Scene_DestroyBuffer( &sc.liveKinds[f] );
		Scene_DestroyBuffer( &sc.distantCells[f] );
		Scene_DestroyBuffer( &sc.volumeList[f] );
		Scene_DestroyBuffer( &sc.viewCullCounts[f] );
	}
	if ( sc.distantPipe ) qvkDestroyPipeline( vk.device, sc.distantPipe, NULL );
	if ( sc.distantPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.distantPipeLayout, NULL );
	if ( sc.distantLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.distantLayout, NULL );
	Scene_DestroyBuffer( &sc.volumeSpheres );
	if ( sc.viewCullPipe ) qvkDestroyPipeline( vk.device, sc.viewCullPipe, NULL );
	if ( sc.viewCullPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.viewCullPipeLayout, NULL );
	if ( sc.viewCullLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.viewCullLayout, NULL );
	if ( sc.wavesPipe ) qvkDestroyPipeline( vk.device, sc.wavesPipe, NULL );
	if ( sc.wavesPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.wavesPipeLayout, NULL );
	if ( sc.wavesLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.wavesLayout, NULL );
	if ( sc.wavesView ) qvkDestroyImageView( vk.device, sc.wavesView, NULL );
	if ( sc.wavesView0 ) qvkDestroyImageView( vk.device, sc.wavesView0, NULL );
	if ( sc.wavesImage ) qvkDestroyImage( vk.device, sc.wavesImage, NULL );
	if ( sc.wavesMem ) qvkFreeMemory( vk.device, sc.wavesMem, NULL );
	Scene_DestroyBuffer( &sc.wavesH0 );
	Scene_DestroyBuffer( &sc.wavesRows );
	if ( sc.lightFadePipe ) qvkDestroyPipeline( vk.device, sc.lightFadePipe, NULL );
	if ( sc.lightFadePipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.lightFadePipeLayout, NULL );
	if ( sc.lightFadeLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.lightFadeLayout, NULL );
	if ( sc.clusterPipe ) qvkDestroyPipeline( vk.device, sc.clusterPipe, NULL );
	if ( sc.clusterPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.clusterPipeLayout, NULL );
	if ( sc.clusterLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.clusterLayout, NULL );
	Scene_DestroyBuffer( &sc.gdMaterials );
	Scene_DestroyBuffer( &sc.cmdInfo );
	Scene_DestroyBuffer( &sc.cmdInfoLs );
	for ( int i = 0; i < SCENE_GD_COUNT; i++ )
		if ( sc.gdPipe[i] ) qvkDestroyPipeline( vk.device, sc.gdPipe[i], NULL );
	if ( sc.gdPipeDepthFade ) qvkDestroyPipeline( vk.device, sc.gdPipeDepthFade, NULL );
	for ( int i = 0; i < 2; i++ ) {
		if ( sc.gdPipeFadeMask[i] ) qvkDestroyPipeline( vk.device, sc.gdPipeFadeMask[i], NULL );
		sc.gdPipeFadeMask[i] = VK_NULL_HANDLE;
	}
	if ( sc.gdPipeLitCov ) qvkDestroyPipeline( vk.device, sc.gdPipeLitCov, NULL );
	sc.gdPipeLitCov = VK_NULL_HANDLE;
	if ( sc.compact ) qvkDestroyPipeline( vk.device, sc.compact, NULL );
	if ( sc.gdPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.gdPipeLayout, NULL );
	if ( sc.compactPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.compactPipeLayout, NULL );
	if ( sc.texLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.texLayout, NULL );
	if ( sc.compactLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.compactLayout, NULL );
	if ( sc.texPool ) qvkDestroyDescriptorPool( vk.device, sc.texPool, NULL );
	if ( sc.pendingWait ) {
		// a trace nobody waited for: an empty submit takes it (a signalled semaphore cannot be signalled again)
		VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkSubmitInfo s;
		Com_Memset( &s, 0, sizeof( s ) );
		s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		s.waitSemaphoreCount = 1;
		s.pWaitSemaphores = &sc.pendingWait;
		s.pWaitDstStageMask = &stage;
		qvkQueueSubmit( vk.queue, 1, &s, VK_NULL_HANDLE );
		qvkQueueWaitIdle( vk.queue );
		sc.pendingWait = VK_NULL_HANDLE;
		sc.pendingFresh = qfalse;
	}
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( sc.ownCb[f] )
			vk.tess[f].command_buffer = sc.ownCb[f];	// the slot's own (it may hold the split one)
		if ( sc.splitCb[f] )
			qvkFreeCommandBuffers( vk.device, vk.command_pool, 1, &sc.splitCb[f] );
		if ( sc.captured[f] ) qvkDestroySemaphore( vk.device, sc.captured[f], NULL );
		if ( sc.traced[f] ) qvkDestroySemaphore( vk.device, sc.traced[f], NULL );
		if ( sc.asyncFence[f] ) qvkDestroyFence( vk.device, sc.asyncFence[f], NULL );
	}
	if ( sc.asyncPool ) qvkDestroyCommandPool( vk.device, sc.asyncPool, NULL );	// frees its command buffers
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( invPool[f] ) {
			PFN_vkDestroyQueryPool destroy = (PFN_vkDestroyQueryPool)qvkGetDeviceProcAddr( vk.device, "vkDestroyQueryPool" );
			if ( destroy )
				destroy( vk.device, invPool[f], NULL );
		}
		invPool[f] = VK_NULL_HANDLE;
		invRecorded[f] = qfalse;
	}
	Scene_StreamFree();
	Scene_FreeCpuState();
	R_StreamSceneImages( qfalse );
	Scene_DestroyBuffer( &sc.depthOrder );
	if ( sc.pool ) qvkDestroyDescriptorPool( vk.device, sc.pool, NULL );	// frees the sets
	if ( sc.materials ) Z_Free( sc.materials );

	Com_Memset( &sc, 0, sizeof( sc ) );
	sc.eyeView = -1;
}

image_t *Scene_Image( const char *path, qboolean *found )
{
	char name[MAX_QPATH];
	image_t *image;

	*found = qfalse;
	Q_strncpyz( name, path, MIN( (int)sizeof( name ), 64 ) );
	if ( !name[0] )
		return tr.whiteImage;
	image = R_FindImageFile( name, IMGFLAG_MIPMAP | IMGFLAG_NOLIGHTSCALE, 0 );
	if ( !image ) {
		ri.Printf( PRINT_DEVELOPER, "scene: missing image %s\n", name );
		return tr.whiteImage;
	}
	*found = qtrue;
	return image;
}

// an image's descriptor as the sets hold it (its sampler as vk_create_image chose)
static void Scene_TextureInfo( const image_t *image, VkDescriptorImageInfo *ii )
{
	Vk_Sampler_Def def;
	Com_Memset( &def, 0, sizeof( def ) );
	def.address_mode = image->wrapClampMode;
	if ( image->mipLevels > 1 ) {
		def.gl_mag_filter = gl_filter_max;
		def.gl_min_filter = gl_filter_min;
	} else {
		def.gl_mag_filter = GL_LINEAR;
		def.gl_min_filter = GL_LINEAR;
		def.noAnisotropy = qtrue;
	}
	ii->sampler = vk_find_sampler( &def );
	ii->imageView = image->view;
	ii->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// this slot's set: the images whose view changed since it was written (a streamed image's level swapped; the old view is
// destroyed only after every frame in flight, so a slot's set is rewritten before any frame binds it again)
static void Scene_RefreshTextures( int f )
{
	if ( !sc.texSets[f] || sceneTexViews[f].size() != sc.numTextures )
		return;
	std::vector<VkDescriptorImageInfo> ii;
	std::vector<VkWriteDescriptorSet> wr;
	std::vector<uint32_t> which;
	for ( uint32_t t = 0; t < sc.numTextures; t++ )
		if ( sceneTexImages[t]->view != sceneTexViews[f][t] )
			which.push_back( t );
	if ( which.empty() )
		return;
	ii.resize( which.size() );
	wr.resize( which.size() );
	for ( size_t k = 0; k < which.size(); k++ ) {
		Scene_TextureInfo( sceneTexImages[which[k]], &ii[k] );
		Com_Memset( &wr[k], 0, sizeof( wr[k] ) );
		wr[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		wr[k].dstSet = sc.texSets[f];
		wr[k].dstBinding = 0;
		wr[k].dstArrayElement = which[k];
		wr[k].descriptorCount = 1;
		wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		wr[k].pImageInfo = &ii[k];
		sceneTexViews[f][which[k]] = sceneTexImages[which[k]]->view;
	}
	qvkUpdateDescriptorSets( vk.device, (uint32_t)wr.size(), wr.data(), 0, NULL );
}

// GPU-driven draws: the scene's images (sceneTexImages) into its every-image set, each with the sampler its own
// descriptor set has (vk_update_descriptor_set); again when r_textureMode makes new samplers
static void Scene_WriteTextures( void )
{
	if ( !sc.texSets[0] || sceneTexImages.size() != sc.numTextures )
		return;
	std::vector<VkDescriptorImageInfo> ii( sc.numTextures );
	for ( uint32_t t = 0; t < sc.numTextures; t++ )
		Scene_TextureInfo( sceneTexImages[t], &ii[t] );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {	// every slot's set (made, or the samplers changed with the device idle)
		VkWriteDescriptorSet w;
		Com_Memset( &w, 0, sizeof( w ) );
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = sc.texSets[f];
		w.dstBinding = 0;
		w.descriptorCount = sc.numTextures;
		w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		w.pImageInfo = ii.data();
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		sceneTexViews[f].resize( sc.numTextures );
		for ( uint32_t t = 0; t < sc.numTextures; t++ )
			sceneTexViews[f][t] = sceneTexImages[t]->view;
	}
}

// vk_texture_mode: the samplers were made again (the device idle)
void vk_scene_samplers_changed( void )
{
	Scene_WriteTextures();
}

/*
GPU-driven draws (r_sceneGpuDriven): after the views and the per-material pipelines exist. The scene's images in one
descriptor array, the materials' constants in a buffer (scene.frag's Material), per command its lists and material
(scene_compact.comp's cmdInfo), per view and slot the packed lists and their slots' materials, the packing pipeline
and the draw pipelines on their own layout (set 0 the views' draw set, 1 the images, 2 the sun cascades). False
leaves the per-material draws (whatever was made is freed with the scene).
*/
static qboolean Scene_CreateGpuDriven( void )
{
	VkPhysicalDeviceProperties props;
	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
	// scene_compact.comp: a workgroup of 1024, every view's buffers (3 arrays of SCENE_MAX_VIEWS and one)
	if ( props.limits.maxComputeWorkGroupInvocations < 1024 || props.limits.maxComputeWorkGroupSize[0] < 1024
		|| props.limits.maxPerStageDescriptorStorageBuffers < 3 * SCENE_MAX_VIEWS + 4 )
		return qfalse;
	// the images: version 2's detail slot and share images first (their indices in the materials' blocks), then albedo,
	// normal, ORM and detail of every material, each once
	std::vector<image_t *> images( sceneSlotImages );
	std::vector<uint32_t> matTex( sc.numMaterials * 4 );
	std::unordered_map<const image_t *, uint32_t> imageIndex;	// each image's first index in images
	for ( uint32_t t = 0; t < images.size(); t++ )
		imageIndex.emplace( images[t], t );
	for ( uint32_t i = 0; i < sc.numMaterials; i++ ) {
		const sceneMaterial_t *m = &sc.materials[i];
		image_t *use[4] = { m->image[0], m->image[1], m->image[2], m->detail ? m->detail : tr.whiteImage };
		for ( int k = 0; k < 4; k++ ) {
			const auto found = imageIndex.emplace( use[k], (uint32_t)images.size() );
			if ( found.second )
				images.push_back( use[k] );
			matTex[i * 4 + k] = found.first->second;
		}
	}
	if ( images.empty() )
		return qfalse;
	sc.numTextures = (uint32_t)images.size();
	// every image in one array beside the draw set's own samplers (8 at most)
	{
		const VkPhysicalDeviceLimits *L = &props.limits;
		const uint32_t need = sc.numTextures + 8;
		if ( L->maxPerStageDescriptorSamplers < need || L->maxPerStageDescriptorSampledImages < need || L->maxDescriptorSetSamplers < need
			|| L->maxDescriptorSetSampledImages < need || L->maxPerStageResources < need + 24 )
			return qfalse;
	}
	sceneTexImages = images;

	VkDescriptorSetLayoutBinding tb;
	Com_Memset( &tb, 0, sizeof( tb ) );
	tb.binding = 0;
	tb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	tb.descriptorCount = sc.numTextures;
	tb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo dl;
	Com_Memset( &dl, 0, sizeof( dl ) );
	dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dl.bindingCount = 1;
	dl.pBindings = &tb;
	if ( qvkCreateDescriptorSetLayout( vk.device, &dl, NULL, &sc.texLayout ) != VK_SUCCESS )
		return qfalse;
	VkDescriptorPoolSize tps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sc.numTextures * NUM_COMMAND_BUFFERS };
	VkDescriptorPoolCreateInfo tp;
	Com_Memset( &tp, 0, sizeof( tp ) );
	tp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	tp.maxSets = NUM_COMMAND_BUFFERS;	// a set per slot
	tp.poolSizeCount = 1;
	tp.pPoolSizes = &tps;
	if ( qvkCreateDescriptorPool( vk.device, &tp, NULL, &sc.texPool ) != VK_SUCCESS )
		return qfalse;
	VkDescriptorSetAllocateInfo da;
	Com_Memset( &da, 0, sizeof( da ) );
	da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	da.descriptorPool = sc.texPool;
	da.descriptorSetCount = 1;
	da.pSetLayouts = &sc.texLayout;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ )
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.texSets[f] ) != VK_SUCCESS )
			return qfalse;
	sc.texSet = sc.texSets[0];
	Scene_WriteTextures();

	// the materials (scene.frag's Material: emissive, tint, info as the push constants', its images)
	std::vector<uint32_t> mats( sc.numMaterials * 16 );
	for ( uint32_t i = 0; i < sc.numMaterials; i++ ) {
		const sceneMaterial_t *m = &sc.materials[i];
		uint32_t *r = &mats[i * 16];
		Com_Memcpy( r, m->emissive, 16 );
		Com_Memcpy( r + 4, m->tint, 16 );
		r[8] = m->kind;
		r[9] = m->imageBits;
		r[10] = i;
		r[11] = m->window;
		for ( int k = 0; k < 4; k++ )
			r[12 + k] = matTex[i * 4 + k];
	}

	// per command its lists and material, the lists' room
	static const uint32_t kindLists[4] = {
		( 1u << SCENE_LIST_LIT ) | ( 1u << SCENE_LIST_DEPTH ),									// opaque
		( 1u << SCENE_LIST_LIT ) | ( 1u << SCENE_LIST_DEPTH_CUTOUT ),							// cutout
		( 1u << SCENE_LIST_BLEND ),																// blend
		( 1u << SCENE_LIST_LIT ) | ( 1u << SCENE_LIST_DEPTH ) | ( 1u << SCENE_LIST_GLOW ) };			// emissive
	std::vector<uint32_t> info( MAX( sc.numCmds, 1u ), 0 ), infoLs( MAX( sc.numCmds, 1u ), 0 );
	Com_Memset( sc.listCap, 0, sizeof( sc.listCap ) );
	for ( uint32_t i = 0; i < sc.numMaterials; i++ ) {
		const sceneMaterial_t *m = &sc.materials[i];
		uint32_t bits = m->kind <= SCENE_KIND_EMISSIVE ? kindLists[m->kind] : m->kind == SCENE_KIND_WATER ? 1u << SCENE_LIST_WATER : 0u;	// a decal: in none
		if ( m->kind == SCENE_KIND_BLEND && ( m->window == 4u || m->window == 64u ) )
			bits |= 1u << SCENE_LIST_GLOW;	// a hologram: its mask's glow blooms too (r_sceneHoloBloom)
		// the static light faces: the opaque and cutout materials' depth lists alone
		const uint32_t lsBits = m->kind == SCENE_KIND_OPAQUE || m->kind == SCENE_KIND_CUTOUT ? bits & ( ( 1u << SCENE_LIST_DEPTH ) | ( 1u << SCENE_LIST_DEPTH_CUTOUT ) ) : 0u;
		for ( uint32_t c = m->firstCmd; c < m->firstCmd + m->numCmds; c++ ) {
			info[c] = bits | ( i << 8 );
			infoLs[c] = lsBits | ( i << 8 );
			for ( int k = 0; k < SCENE_LIST_COUNT; k++ )
				sc.listCap[k] += ( bits >> k ) & 1u;
		}
	}
	sc.listTotal = 0;
	for ( int k = 0; k < SCENE_LIST_COUNT; k++ ) {
		sc.listBase[k] = sc.listTotal;
		sc.listTotal += sc.listCap[k];
	}
	const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if ( !Scene_StaticBuffer( &sc.gdMaterials, mats.data(), (VkDeviceSize)mats.size() * 4, ssbo, "scene materials" )
		|| !Scene_StaticBuffer( &sc.cmdInfo, info.data(), (VkDeviceSize)info.size() * 4, ssbo, "scene command lists" )
		|| !Scene_StaticBuffer( &sc.cmdInfoLs, infoLs.data(), (VkDeviceSize)infoLs.size() * 4, ssbo, "scene light shadow command lists" )
		|| sceneDepthOrder.size() != sc.numCmds
		|| !Scene_StaticBuffer( &sc.depthOrder, sceneDepthOrder.data(), (VkDeviceSize)MAX( sc.numCmds, 1u ) * 4, ssbo, "scene depth order" ) )
		return qfalse;

	// the packing pipeline: the views' commands, cmdInfo, their lists, their slots' materials, the depth lists' order,
	// occlusion's phase-2 commands (its append mode), the block-parallel packing's block sums
	static const VkDescriptorType compactTypes[7] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags compactStages[7] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT };
	static const uint32_t compactArrays[7] = { SCENE_MAX_VIEWS, 1, SCENE_MAX_VIEWS, SCENE_MAX_VIEWS, 1, 1, 1 };
	sc.compactLayout = Scene_SetLayout( compactTypes, compactStages, 7, compactArrays );
	if ( !sc.compactLayout )
		return qfalse;
	VkPushConstantRange push;
	VkPipelineLayoutCreateInfo pl;
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.offset = 0;
	push.size = 48;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.compactLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.compactPipeLayout ) != VK_SUCCESS )
		return qfalse;
	VkDescriptorSetLayout gdSets[3] = { sc.drawLayout, sc.texLayout, vk.set_layout_sampler };
	push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	push.size = sizeof( scenePush_t );
	pl.setLayoutCount = 3;
	pl.pSetLayouts = gdSets;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.gdPipeLayout ) != VK_SUCCESS )
		return qfalse;

	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			if ( !Scene_CreateBuffer( &sc.lists[f][v], SCENE_LISTS_HEADER + (VkDeviceSize)sc.listTotal * sizeof( VkDrawIndexedIndirectCommand ),
					VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene draw lists" )
				|| !Scene_CreateBuffer( &sc.listMaterial[f][v], (VkDeviceSize)MAX( sc.listTotal, 1u ) * 4, ssbo,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene draw list materials" ) )
				return qfalse;
			VkDescriptorBufferInfo bi[2] = { { sc.listMaterial[f][v].buf, 0, VK_WHOLE_SIZE }, { sc.gdMaterials.buf, 0, VK_WHOLE_SIZE } };
			VkWriteDescriptorSet w[2];
			for ( int i = 0; i < 2; i++ ) {
				Com_Memset( &w[i], 0, sizeof( w[i] ) );
				w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				w[i].dstSet = sc.drawSet[f][v];
				w[i].dstBinding = 14 + i;
				w[i].descriptorCount = 1;
				w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				w[i].pBufferInfo = &bi[i];
			}
			qvkUpdateDescriptorSets( vk.device, 2, w, 0, NULL );
		}
	}

	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {	// the count-then-scan cull's scatter set a slot
		VkDescriptorSetAllocateInfo sda;
		Com_Memset( &sda, 0, sizeof( sda ) );
		sda.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		sda.descriptorPool = sc.pool;
		sda.descriptorSetCount = 1;
		sda.pSetLayouts = &sc.scatterLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &sda, &sc.scatterSet[f] ) != VK_SUCCESS )
			return qfalse;
		Scene_CullPoolScatterSet( f );
	}

	// the packing's set per slot: every view's commands, lists and their materials in arrays (one dispatch, a workgroup
	// per view; or the block-parallel packing's three, a workgroup per 1024 commands and view)
	sc.compactBlocks = ( sc.numCmds + 1023u ) / 1024u;
	if ( sc.compactBlocks > 1024u )	// its scan takes a view's blocks in one workgroup of 1024
		sc.compactBlocks = 0;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ )
		if ( !Scene_CreateBuffer( &sc.blockSums[f], (VkDeviceSize)SCENE_MAX_VIEWS * MAX( sc.compactBlocks, 1u ) * SCENE_LIST_COUNT * 4, ssbo,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene packing block sums" ) )
			return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.compactLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.compactSet[f][0] ) != VK_SUCCESS )
			return qfalse;
		VkDescriptorBufferInfo bi[3 * SCENE_MAX_VIEWS + 4];
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			bi[v] = { sc.indirect[f][v].buf, 0, VK_WHOLE_SIZE };
			bi[SCENE_MAX_VIEWS + v] = { sc.lists[f][v].buf, 0, VK_WHOLE_SIZE };
			bi[2 * SCENE_MAX_VIEWS + v] = { sc.listMaterial[f][v].buf, 0, VK_WHOLE_SIZE };
		}
		bi[3 * SCENE_MAX_VIEWS] = { sc.cmdInfo.buf, 0, VK_WHOLE_SIZE };
		bi[3 * SCENE_MAX_VIEWS + 1] = { sc.depthOrder.buf, 0, VK_WHOLE_SIZE };
		bi[3 * SCENE_MAX_VIEWS + 2] = { sc.occTouched.buf, 0, VK_WHOLE_SIZE };
		bi[3 * SCENE_MAX_VIEWS + 3] = { sc.blockSums[f].buf, 0, VK_WHOLE_SIZE };
		VkWriteDescriptorSet w[7];
		const int first[7] = { 0, 3 * SCENE_MAX_VIEWS, SCENE_MAX_VIEWS, 2 * SCENE_MAX_VIEWS, 3 * SCENE_MAX_VIEWS + 1, 3 * SCENE_MAX_VIEWS + 2,
			3 * SCENE_MAX_VIEWS + 3 };
		for ( int i = 0; i < 7; i++ ) {
			Com_Memset( &w[i], 0, sizeof( w[i] ) );
			w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w[i].dstSet = sc.compactSet[f][0];
			w[i].dstBinding = i;
			w[i].descriptorCount = compactArrays[i];
			w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			w[i].pBufferInfo = &bi[first[i]];
		}
		qvkUpdateDescriptorSets( vk.device, 7, w, 0, NULL );
	}

	VkShaderModule cs = Scene_Shader( scene_compact_comp_spv, scene_compact_comp_spv_size );
	// (with primitive shading rates every GPU-driven pipeline runs the module that writes them: the prepass and the lit
	// pass keep one position arithmetic for the EQUAL test; only the lit pipelines take the rate, r_sceneVrs)
	VkShaderModule vs = vk.shadingRate ? Scene_Shader( scene_vert_gd_vrs_spv, scene_vert_gd_vrs_spv_size )
		: Scene_Shader( scene_vert_gd_spv, scene_vert_gd_spv_size );
	VkShaderModule fs = Scene_Shader( scene_frag_gd_spv, scene_frag_gd_spv_size );
	VkShaderModule fsLit = Scene_Shader( scene_frag_gdlit_nodebug_spv, scene_frag_gdlit_nodebug_spv_size );
	VkShaderModule fsMask = vk.msaaActive ? Scene_Shader( scene_frag_gd_mask_spv, scene_frag_gd_mask_spv_size ) : VK_NULL_HANDLE;
	VkShaderModule fsLitDebug = Scene_Shader( scene_frag_gdlit_spv, scene_frag_gdlit_spv_size );
	VkShaderModule fsLitCov = vk.msaaActive ? Scene_Shader( scene_frag_gdlit_cov_spv, scene_frag_gdlit_cov_spv_size ) : VK_NULL_HANDLE;
	qboolean ok = ( cs && vs && fs && fsLit && fsLitDebug ) ? qtrue : qfalse;
	if ( ok ) {
		VkComputePipelineCreateInfo ci;
		Com_Memset( &ci, 0, sizeof( ci ) );
		ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		ci.stage.module = cs;
		ci.stage.pName = "main";
		ci.layout = sc.compactPipeLayout;
		if ( qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.compact ) != VK_SUCCESS )
			ok = qfalse;
	}
	if ( ok ) {
		sc.gdPipe[SCENE_GD_LIT] = Scene_GraphicsPipeline( vs, fsLit, 0, qfalse, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD_LIT );
		sc.gdPipe[SCENE_GD_LIT_DEBUG] = Scene_GraphicsPipeline( vs, fsLitDebug, 0, qfalse, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD_LIT );
		sc.gdPipe[SCENE_GD_LIT_EQUAL] = Scene_GraphicsPipeline( vs, fsLit, 0, qfalse, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD_LIT_EQUAL );	// optional
		sc.gdPipe[SCENE_GD_BLEND] = Scene_GraphicsPipeline( vs, fs, 2, qfalse, qtrue, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		sc.gdPipe[SCENE_GD_DEPTH] = Scene_GraphicsPipeline( vs, fs, 0, qtrue, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		sc.gdPipeDepthFade = Scene_GraphicsPipeline( vs, fs, 7, qtrue, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		if ( fsMask ) {	// (optional: without them r_sceneHideDither 2 is 1)
			sc.gdPipeFadeMask[0] = Scene_GraphicsPipeline( vs, fsMask, 7, qtrue, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
			sc.gdPipeFadeMask[1] = Scene_GraphicsPipeline( vs, fsMask, 4, qtrue, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		}
		if ( fsLitCov )	// (optional: without it r_sceneCutoutCoverage is off)
			sc.gdPipeLitCov = Scene_GraphicsPipeline( vs, fsLitCov, 0, qfalse, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD_LIT_EQUAL );
		sc.gdPipe[SCENE_GD_DEPTH_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 4, qtrue, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		sc.gdPipe[SCENE_GD_GLOW] = Scene_GraphicsPipeline( vs, fs, 5, qfalse, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		if ( sc.numWater )	// (the water list is only filled when the load found the bound path's water pipeline)
			sc.gdPipe[SCENE_GD_WATER] = Scene_GraphicsPipeline( vs, fs, SCENE_MODE_WATER, qfalse, qfalse, SCENE_SHADOW_NONE, SCENE_VARIANT_GD );
		if ( sc.numWater && !sc.gdPipe[SCENE_GD_WATER] )
			ok = qfalse;
		for ( int i = 0; i <= SCENE_GD_GLOW; i++ )
			if ( !sc.gdPipe[i] )
				ok = qfalse;
		if ( ok && sc.shadowPipes ) {
			sc.gdPipe[SCENE_GD_SHADOW] = Scene_GraphicsPipeline( vs, fs, 0, qtrue, qfalse, SCENE_SHADOW_SUN, SCENE_VARIANT_GD );
			sc.gdPipe[SCENE_GD_SHADOW_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 4, qtrue, qfalse, SCENE_SHADOW_SUN, SCENE_VARIANT_GD );
			if ( !sc.gdPipe[SCENE_GD_SHADOW] || !sc.gdPipe[SCENE_GD_SHADOW_CUTOUT] )
				ok = qfalse;
		}
		if ( ok && sc.lsPass[0] ) {	// the static light faces' (without them, the per-material path draws those)
			sc.gdPipe[SCENE_GD_LSHADOW] = Scene_GraphicsPipeline( vs, fs, 0, qtrue, qfalse, SCENE_SHADOW_LIGHT, SCENE_VARIANT_GD );
			sc.gdPipe[SCENE_GD_LSHADOW_CUTOUT] = Scene_GraphicsPipeline( vs, fs, 4, qtrue, qfalse, SCENE_SHADOW_LIGHT, SCENE_VARIANT_GD );
		}
	}
	if ( cs ) qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( vs ) qvkDestroyShaderModule( vk.device, vs, NULL );
	if ( fs ) qvkDestroyShaderModule( vk.device, fs, NULL );
	if ( fsLit ) qvkDestroyShaderModule( vk.device, fsLit, NULL );
	if ( fsMask ) qvkDestroyShaderModule( vk.device, fsMask, NULL );
	if ( fsLitDebug ) qvkDestroyShaderModule( vk.device, fsLitDebug, NULL );
	if ( fsLitCov ) qvkDestroyShaderModule( vk.device, fsLitCov, NULL );
	sc.gd = ok;
	return ok;
}

// async compute (r_sceneAsync): the compute queue's pool, per slot its command buffer, the frame's second graphics
// command buffer, the semaphores and the trace's fence. False: the reflections stay in the frame
static qboolean Scene_CreateAsync( void )
{
	if ( !vk.compute_queue )
		return qfalse;
	VkCommandPoolCreateInfo cp;
	Com_Memset( &cp, 0, sizeof( cp ) );
	cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cp.queueFamilyIndex = vk.compute_family_index;
	if ( qvkCreateCommandPool( vk.device, &cp, NULL, &sc.asyncPool ) != VK_SUCCESS )
		return qfalse;
	VkCommandBufferAllocateInfo ca;
	Com_Memset( &ca, 0, sizeof( ca ) );
	ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ca.commandBufferCount = 1;
	VkSemaphoreCreateInfo si;
	Com_Memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fi;
	Com_Memset( &fi, 0, sizeof( fi ) );
	fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		ca.commandPool = sc.asyncPool;
		if ( qvkAllocateCommandBuffers( vk.device, &ca, &sc.asyncCb[f] ) != VK_SUCCESS )
			return qfalse;
		ca.commandPool = vk.command_pool;
		if ( qvkAllocateCommandBuffers( vk.device, &ca, &sc.splitCb[f] ) != VK_SUCCESS )
			return qfalse;
		sc.ownCb[f] = vk.tess[f].command_buffer;
		if ( qvkCreateSemaphore( vk.device, &si, NULL, &sc.captured[f] ) != VK_SUCCESS
			|| qvkCreateSemaphore( vk.device, &si, NULL, &sc.traced[f] ) != VK_SUCCESS
			|| qvkCreateFence( vk.device, &fi, NULL, &sc.asyncFence[f] ) != VK_SUCCESS )
			return qfalse;
	}
	sc.asyncReady = qtrue;
	return qtrue;
}

// async compute: the frame's commands so far submitted (signalling captured, waiting for the last trace before any
// fragment reads its reflections; the next capture's history writes follow those fragments), the rest recorded into
// the slot's other command buffer, whose engine state starts unbound
void Scene_SplitFrame( int f )
{
	VkCommandBuffer done = vk.cmd->command_buffer;
	VK_CHECK( qvkEndCommandBuffer( done ) );
	VkSemaphore waits[2];
	VkPipelineStageFlags stages[2];
	uint32_t numWaits = 0;
	if ( sc.pendingWait ) {
		waits[numWaits] = sc.pendingWait;
		stages[numWaits++] = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	// this part holds the swapchain image's first-use transition (vk_begin_frame's UNDEFINED one), which must follow the
	// acquire: it waits here, and the end-of-frame submit no longer does; the image's later writes, in the next part,
	// follow the transition in submission order
	if ( vk.cmd->swapchain_first_use && vk.cmd->swapchain_image_acquired && !vk.cmd->acquire_waited && !ri.VK_IsMinimized() ) {
		waits[numWaits] = vk.cmd->image_acquired;
		stages[numWaits++] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		vk.cmd->acquire_waited = qtrue;
	}
	VkSubmitInfo s;
	Com_Memset( &s, 0, sizeof( s ) );
	s.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	s.waitSemaphoreCount = numWaits;
	s.pWaitSemaphores = numWaits ? waits : NULL;
	s.pWaitDstStageMask = numWaits ? stages : NULL;
	s.commandBufferCount = 1;
	s.pCommandBuffers = &done;
	s.signalSemaphoreCount = 1;
	s.pSignalSemaphores = &sc.captured[f];
	vk_upload_flush();	// this frame's buffer uploads (vk_upload.cpp) before the draws in this part of it
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &s, VK_NULL_HANDLE ) );
	sc.pendingWait = VK_NULL_HANDLE;

	// the slot's fence (vk_end_frame's submit, after this one on the queue) covers both of its command buffers
	vk.cmd->command_buffer = ( done == sc.splitCb[f] ) ? sc.ownCb[f] : sc.splitCb[f];
	VkCommandBufferBeginInfo bi;
	Com_Memset( &bi, 0, sizeof( bi ) );
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK( qvkBeginCommandBuffer( vk.cmd->command_buffer, &bi ) );
	vk.cmd->curr_index_buffer = VK_NULL_HANDLE;
	vk.cmd->last_pipeline = VK_NULL_HANDLE;
	Com_Memset( &vk.cmd->scissor_rect, 0, sizeof( vk.cmd->scissor_rect ) );
	vk_restore_draw_state( qfalse );	// the bound descriptor sets again, viewport and scissor forced
}

// vk_end_frame: a trace an earlier frame submitted that no submit waited for yet (a frame without the scene's async
// reflections came between); this frame's own trace is left to the next frame's first submit, which reads them
VkSemaphore vk_scene_take_wait( void )
{
	if ( sc.pendingFresh ) {
		sc.pendingFresh = qfalse;
		return VK_NULL_HANDLE;
	}
	VkSemaphore s = sc.pendingWait;
	sc.pendingWait = VK_NULL_HANDLE;
	return s;
}

// the eye view's light clusters (r_sceneClusters): scene_cluster.comp and its sets (the lights, their boxes, the slot's
// clusters); a device without subgroup operations keeps the light grid's cells
static qboolean Scene_CreateClusters( void )
{
	// (scene_cluster.comp: up to 4 subgroups of its 64 threads; light indices in 32 bits: they were 16, and night-2's
	// 119,965 lights left every fragment on its grid cell's list)
	if ( !vk.subgroupOps || vk.instanceApiVersion < VK_API_VERSION_1_1 || vk.subgroupSize < 16 || vk.subgroupSize > 64 )
		return qfalse;
	static const VkDescriptorType types[8] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[8] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	// the surfaces' clusters, the haze's; the slot's Occ block (the fallbacks' counters); the lights' spheres and kinds;
	// the view's lights (r_sceneClusterCull) and their per-workgroup counts
	static const uint32_t arrays[8] = { 1, 1, 2, 1, 1, 1, 1, 1 };
	sc.clusterLayout = Scene_SetLayout( types, stages, 8, arrays );
	if ( !sc.clusterLayout )
		return qfalse;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 80 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.clusterLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.clusterPipeLayout ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.clusterLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.clusterSet[f] ) != VK_SUCCESS )
			return qfalse;
		const sceneBuffer_t *bufs[2] = { &sc.lights, &sc.lightBoxes };
		Scene_WriteSet( sc.clusterSet[f], bufs, types, 2 );
		VkDescriptorBufferInfo ob[2] = { { sc.clusters[f].buf, 0, VK_WHOLE_SIZE }, { sc.fogClusters[f].buf, 0, VK_WHOLE_SIZE } };
		VkWriteDescriptorSet w;
		Com_Memset( &w, 0, sizeof( w ) );
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = sc.clusterSet[f];
		w.dstBinding = 2;
		w.descriptorCount = 2;
		w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w.pBufferInfo = ob;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		VkDescriptorBufferInfo sb = { sc.occStats[f].buf, 0, VK_WHOLE_SIZE };
		w.dstBinding = 3;
		w.descriptorCount = 1;
		w.pBufferInfo = &sb;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		sb.buffer = sc.lightBounds.buf;
		w.dstBinding = 4;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		sb.buffer = sc.liveKinds[f].buf;	// the kinds with the lights off masked out (scene_lightfade.comp)
		w.dstBinding = 5;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		if ( !Scene_CreateBuffer( &sc.viewLights[f], 16 + (VkDeviceSize)MAX( sc.numLights, 1u ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene view lights" )
			|| !Scene_CreateBuffer( &sc.viewLightCounts[f], (VkDeviceSize)( ( MAX( sc.numLights, 1u ) + 63 ) / 64 ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene view light counts" ) )
			return qfalse;
		sb.buffer = sc.viewLights[f].buf;
		w.dstBinding = 6;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
		sb.buffer = sc.viewLightCounts[f].buf;
		w.dstBinding = 7;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	}
	VkShaderModule cs = Scene_Shader( scene_cluster_comp_spv, scene_cluster_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.clusterPipeLayout;
	ci.flags = Scene_StatsFlags();
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.clusterPipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( r != VK_SUCCESS ) {
		sc.clusterPipe = VK_NULL_HANDLE;
		return qfalse;
	}
	Scene_PrintStats( sc.clusterPipe, "clusters" );
	return qtrue;
}

// the haze volumes that may reach the eye's frustum (scene_viewcull.comp, before the haze); without it (a failed
// pipeline) the haze reads the full list (every index, in order)
static qboolean Scene_CreateViewCull( void )
{
	static const VkDescriptorType types[3] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[3] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	sc.viewCullLayout = Scene_SetLayout( types, stages, 3 );
	if ( !sc.viewCullLayout )
		return qfalse;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 80 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.viewCullLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.viewCullPipeLayout ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.viewCullLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.viewCullSet[f] ) != VK_SUCCESS )
			return qfalse;
		const sceneBuffer_t *bufs[3] = { &sc.volumeSpheres, &sc.volumeList[f], &sc.viewCullCounts[f] };
		Scene_WriteSet( sc.viewCullSet[f], bufs, types, 3 );
	}
	VkShaderModule cs = Scene_Shader( scene_viewcull_comp_spv, scene_viewcull_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.viewCullPipeLayout;
	ci.flags = Scene_StatsFlags();
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.viewCullPipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( r != VK_SUCCESS ) {
		sc.viewCullPipe = VK_NULL_HANDLE;
		return qfalse;
	}
	Scene_PrintStats( sc.viewCullPipe, "viewcull" );
	return qtrue;
}

// scene_lightfade.comp and its sets (CP2077's light hide distances); without it every light keeps (1, 1) and its kind
static qboolean Scene_CreateLightFade( void )
{
	static const VkDescriptorType types[5] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[5] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT };
	sc.lightFadeLayout = Scene_SetLayout( types, stages, 5 );
	if ( !sc.lightFadeLayout )
		return qfalse;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 48 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.lightFadeLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.lightFadePipeLayout ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.lightFadeLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.lightFadeSet[f] ) != VK_SUCCESS )
			return qfalse;
		const sceneBuffer_t *bufs[5] = { &sc.lightBounds, &sc.lightBoxes, &sc.lightKinds, &sc.lightFade[f], &sc.liveKinds[f] };
		Scene_WriteSet( sc.lightFadeSet[f], bufs, types, 5 );
	}
	VkShaderModule cs = Scene_Shader( scene_lightfade_comp_spv, scene_lightfade_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.lightFadePipeLayout;
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.lightFadePipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( r != VK_SUCCESS ) {
		sc.lightFadePipe = VK_NULL_HANDLE;
		return qfalse;
	}
	return qtrue;
}

// the waves' spectra (scene_water.comp): per cascade N x N H0(k), rows ky, columns kx, wave numbers 2 pi / L x the signed
// index; a Phillips spectrum for the wind (along +x: the material's heading turns it), in the cascade's band (the
// smallest patch from six times its base wave number up, the next from six times its own base to the smaller's start,
// the largest below), each scaled to the same share of the old ripples' RMS slope 0.31. Deterministic (its own
// generator, seeds 2077 + the cascade)
static void Scene_WaveSpectrum( const float L[3], float wind, std::vector<float> &h0, float *var )
{
	const int N = 256;
	const double g = 9.81, peak = (double)wind * wind / g, target = 0.31 * 0.31 / 3.0;
	h0.assign( (size_t)3 * N * N * 2, 0.0f );
	for ( int c = 0; c < 3; c++ ) {
		const double kmin = c < 2 ? 6.0 * 2.0 * M_PI / L[c] : 0.0;
		const double kmax = c > 0 ? 6.0 * 2.0 * M_PI / L[c - 1] : 1e30;
		uint64_t st = 0x9e3779b97f4a7c15ull * (uint64_t)( 2077 + c ) + 1;
		auto unit = [&st]() {	// xorshift64*, (0, 1]
			st ^= st >> 12; st ^= st << 25; st ^= st >> 27;
			return ( (double)( ( st * 0x2545f4914f6cdd1dull ) >> 11 ) + 1.0 ) / 9007199254740993.0;
		};
		float *H = &h0[(size_t)c * N * N * 2];
		for ( int y = 0; y < N; y++ )
			for ( int x = 0; x < N; x++ ) {
				const double kx = 2.0 * M_PI / L[c] * ( x < N / 2 ? x : x - N ), ky = 2.0 * M_PI / L[c] * ( y < N / 2 ? y : y - N );
				const double kk = sqrt( kx * kx + ky * ky );
				const double r = sqrt( -2.0 * log( unit() ) ), a = 2.0 * M_PI * unit();	// two normals (Box-Muller)
				if ( kk <= 0.0 || kk < kmin || kk >= kmax )
					continue;
				const double P = exp( -1.0 / ( kk * peak * kk * peak ) ) / ( kk * kk * kk * kk ) * ( kx / kk ) * ( kx / kk ) * exp( -( kk * 0.01 ) * ( kk * 0.01 ) );
				const double s = sqrt( P / 2.0 );
				H[( y * N + x ) * 2] = (float)( r * cos( a ) * s );
				H[( y * N + x ) * 2 + 1] = (float)( r * sin( a ) * s );
			}
		double v = 0.0;	// the slope variance: k^2 (|H0(k)|^2 + |H0(-k)|^2) over the patch
		for ( int y = 0; y < N; y++ )
			for ( int x = 0; x < N; x++ ) {
				const double kx = 2.0 * M_PI / L[c] * ( x < N / 2 ? x : x - N ), ky = 2.0 * M_PI / L[c] * ( y < N / 2 ? y : y - N );
				const float *a = &H[( y * N + x ) * 2], *b = &H[( ( ( N - y ) % N ) * N + ( N - x ) % N ) * 2];
				v += ( kx * kx + ky * ky ) * ( (double)a[0] * a[0] + (double)a[1] * a[1] + (double)b[0] * b[0] + (double)b[1] * b[1] );
			}
		const float scale = v > 0.0 ? (float)sqrt( target / v ) : 0.0f;
		for ( int i = 0; i < N * N * 2; i++ )
			H[i] *= scale;
	}
	*var = (float)target;
}

// scene_water.comp and its set (r_sceneWaterWaves, with water): the spectra, the rows, the image; without it the water
// takes the ripple image
static void Scene_CreateWaves( void )
{
	if ( !sc.numWater || !r_sceneWaterWaves->integer || !sc.wavesView )
		return;
	sscanf( r_sceneWaterCascades->string, "%f %f %f", &sc.wavesSize[0], &sc.wavesSize[1], &sc.wavesSize[2] );
	for ( int c = 0; c < 3; c++ )
		sc.wavesSize[c] = Com_Clamp( 0.5f, 2000.0f, sc.wavesSize[c] );
	std::vector<float> h0;
	Scene_WaveSpectrum( sc.wavesSize, Com_Clamp( 0.5f, 30.0f, r_sceneWaterWind->value ), h0, &sc.wavesVar );
	const std::vector<float> zeros( h0.size(), 0.0f );
	const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	if ( !Scene_StaticBuffer( &sc.wavesH0, h0.data(), (VkDeviceSize)h0.size() * 4, ssbo, "scene water spectra" )
		|| !Scene_StaticBuffer( &sc.wavesRows, zeros.data(), (VkDeviceSize)zeros.size() * 4, ssbo, "scene water rows" ) )
		return;
	static const VkDescriptorType types[3] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
	static const VkShaderStageFlags stages[3] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	sc.wavesLayout = Scene_SetLayout( types, stages, 3 );
	if ( !sc.wavesLayout )
		return;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 32 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.wavesLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.wavesPipeLayout ) != VK_SUCCESS )
		return;
	VkDescriptorSetAllocateInfo da;
	Com_Memset( &da, 0, sizeof( da ) );
	da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	da.descriptorPool = sc.pool;
	da.descriptorSetCount = 1;
	da.pSetLayouts = &sc.wavesLayout;
	if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.wavesSet ) != VK_SUCCESS )
		return;
	const sceneBuffer_t *bufs[2] = { &sc.wavesH0, &sc.wavesRows };
	Scene_WriteSet( sc.wavesSet, bufs, types, 2 );
	VkDescriptorImageInfo ii = { VK_NULL_HANDLE, sc.wavesView0, VK_IMAGE_LAYOUT_GENERAL };
	VkWriteDescriptorSet w;
	Com_Memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = sc.wavesSet;
	w.dstBinding = 2;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	w.pImageInfo = &ii;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	VkShaderModule cs = Scene_Shader( scene_water_comp_spv, scene_water_comp_spv_size );
	if ( !cs )
		return;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.wavesPipeLayout;
	if ( qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.wavesPipe ) != VK_SUCCESS )
		sc.wavesPipe = VK_NULL_HANDLE;
	qvkDestroyShaderModule( vk.device, cs, NULL );
	ri.Printf( PRINT_ALL, "scene water waves: %s, cascades %.2f %.2f %.2f m, wind %.1f m/s\n", sc.wavesPipe ? "simulated" : "no pipeline (the ripple image)",
		sc.wavesSize[0], sc.wavesSize[1], sc.wavesSize[2], r_sceneWaterWind->value );
}

// scene_distant.comp and its sets (r_sceneLightDistant); without it no light shows as a stand-in
static qboolean Scene_CreateDistant( void )
{
	static const VkDescriptorType types[4] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[4] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	if ( !sc.numCells || !sc.hasLightBoxes )
		return qfalse;
	sc.distantLayout = Scene_SetLayout( types, stages, 4 );
	if ( !sc.distantLayout )
		return qfalse;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.distantLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.distantPipeLayout ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.pool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.distantLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.distantSet[f] ) != VK_SUCCESS )
			return qfalse;
		const sceneBuffer_t *bufs[4] = { &sc.cells, &sc.cellLights, &sc.lightFade[f], &sc.distantCells[f] };
		Scene_WriteSet( sc.distantSet[f], bufs, types, 4 );
	}
	VkShaderModule cs = Scene_Shader( scene_distant_comp_spv, scene_distant_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.distantPipeLayout;
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.distantPipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( r != VK_SUCCESS ) {
		sc.distantPipe = VK_NULL_HANDLE;
		return qfalse;
	}
	return qtrue;
}

// the lights binned into a world grid over the instances' bounds; each cell keeps its SCENE_CELL_MAX_LIGHTS strongest
static void Scene_BinLights( const xscnLight_t *lights, uint32_t numLights, std::vector<uint32_t> &cellsOut, std::vector<uint32_t> &refs )
{
	vec3_t size;
	VectorSubtract( sc.maxs, sc.mins, size );
	float cell = MAX( 256.0f, MAX( size[0], MAX( size[1], size[2] ) ) / SCENE_GRID_MAX );
	for ( int k = 0; k < 3; k++ ) {
		sc.gridOrigin[k] = sc.mins[k];
		sc.gridDims[k] = MAX( 1, MIN( SCENE_GRID_MAX, (int)ceilf( size[k] / cell ) ) );
	}
	sc.gridOrigin[3] = cell;
	sc.gridDims[3] = 0;

	const int nx = sc.gridDims[0], ny = sc.gridDims[1], nz = sc.gridDims[2];
	std::vector<std::vector<uint32_t> > bins( (size_t)nx * ny * nz );
	for ( uint32_t i = 0; i < numLights; i++ ) {
		const xscnLight_t *L = &lights[i];
		int lo[3], hi[3];
		for ( int k = 0; k < 3; k++ ) {
			lo[k] = MAX( 0, (int)floorf( ( L->origin[k] - L->radius - sc.gridOrigin[k] ) / cell ) );
			hi[k] = MIN( sc.gridDims[k] - 1, (int)floorf( ( L->origin[k] + L->radius - sc.gridOrigin[k] ) / cell ) );
		}
		for ( int z = lo[2]; z <= hi[2]; z++ )
			for ( int y = lo[1]; y <= hi[1]; y++ )
				for ( int x = lo[0]; x <= hi[0]; x++ )
					bins[( (size_t)z * ny + y ) * nx + x].push_back( i );
	}
	cellsOut.resize( bins.size() * 2 );
	refs.clear();
	sceneCharCells.assign( bins.size() * 2, 0u );
	sceneCharRefs.clear();
	uint32_t numCapped = 0;
	for ( size_t c = 0; c < bins.size(); c++ ) {
		std::vector<uint32_t> &b = bins[c];
		sceneCharCells[c * 2] = (uint32_t)sceneCharRefs.size();
		sceneCharCells[c * 2 + 1] = (uint32_t)b.size();
		sceneCharRefs.insert( sceneCharRefs.end(), b.begin(), b.end() );
		const qboolean capped = b.size() > SCENE_CELL_MAX_LIGHTS ? qtrue : qfalse;
		if ( capped ) {
			numCapped++;
			const int x = (int)( c % nx ), y = (int)( ( c / nx ) % ny ), z = (int)( c / ( (size_t)nx * ny ) );
			vec3_t mid;
			VectorSet( mid, sc.gridOrigin[0] + ( x + 0.5f ) * cell, sc.gridOrigin[1] + ( y + 0.5f ) * cell, sc.gridOrigin[2] + ( z + 0.5f ) * cell );
			std::sort( b.begin(), b.end(), [&]( uint32_t a, uint32_t bb ) {
				const float da = Distance( lights[a].origin, mid ) / 37.5f, db = Distance( lights[bb].origin, mid ) / 37.5f;
				return lights[a].power / ( da * da + 1.0f ) > lights[bb].power / ( db * db + 1.0f );
			} );
			b.resize( SCENE_CELL_MAX_LIGHTS );
		}
		cellsOut[c * 2] = (uint32_t)refs.size();
		// the top bit: the cell kept only its strongest (the light clusters, which keep all, defer to it there)
		cellsOut[c * 2 + 1] = (uint32_t)b.size() | ( capped ? 0x80000000u : 0u );
		refs.insert( refs.end(), b.begin(), b.end() );
	}
	if ( refs.empty() )
		refs.push_back( 0 );
	if ( numCapped )	// those cells light by their strongest only, and the light clusters defer to them there (scene.frag)
		ri.Printf( PRINT_WARNING, "WARNING: scene light grid: %u of %u cells (%.0f m) keep only their %d strongest lights; the light "
			"clusters are not used in them (r_sceneClusters 2 uses them)\n", numCapped, (uint32_t)bins.size(), cell / 37.5f, SCENE_CELL_MAX_LIGHTS );
}

/* ------------------------------------------------------------------ geometry streaming */

/*
Geometry streaming (r_sceneStream, a tiled scene whose files its packs store): the tables (meshes, LODs, parts,
materials, instances, lights) stay resident and the geometry lives in two pools of r_sceneStreamMB together. The unit
is a geometry block: a part's indices and the vertices from its vertex offset to the next block's (the converter shares
one submesh's indices and vertices between the meshes that use it, so a mesh's LODs may draw blocks of other tiles'
files). Every SCENE_STREAM_WANT_EVERY frames scene_want.comp lists the LODs the cull would draw from the eye with the
distance taken r_sceneStreamMargin nearer, and each wanted mesh's coarsest; the host reads that back when the slot comes
round, reads the wanted blocks it lacks from the packs on the engine's prefetch worker (FS_RangeReadAsync) into a
staging ring, nearest first, copies them into the pools at most r_sceneStreamUploadMB a frame, points their parts' draw
commands at them and marks a LOD resident (its record's last word, which scene_cull.comp reads) once every block it
draws is in. Blocks no wanted LOD uses stay until a pool is full, then go least recently wanted first. The first view,
and a view that jumps, waits for its wanted set (Scene_StreamSettle), and the static light shadows are drawn again then.
*/
#define SCENE_STREAM_WANT_EVERY	4
#define SCENE_STREAM_PATCH_BYTES	( 2u << 20 )	// a frame's command and LOD patches (8 and 4 bytes each)
#define SCENE_STREAM_MAX_READS	1024			// blocks in flight on the prefetch worker

typedef struct {
	char		path[MAX_QPATH];
	int			file;							// FS_RangeOpen handle
	uint32_t	vBase, vCount, iBase, iCount;	// its vertices and indices in the scene's numbering
	uint32_t	vOfs, iOfs;						// their sections' offsets in the file
} sceneShard_t;

enum { SB_OUT, SB_READING, SB_ARRIVED, SB_RESIDENT, SB_BAD };
typedef struct {
	uint32_t	firstIndex, numIndices, vStart, vCount;	// the scene's numbering
	uint16_t	iShard, vShard;
	uint8_t		state, retried;
	uint32_t	vPool, iPool;			// resident: where in the pools (vertices, indices)
	uint32_t	refs;					// wanted LODs drawing it
	uint32_t	lastWanted;				// sc.frameNo
	uint32_t	firstPart, numParts;	// ss.blockParts
	uint32_t	firstLod, numLods;		// ss.blockLods
	float		prio;					// the least distance a wanted LOD of it was wanted at
	uint32_t	stamp;
} sceneBlock_t;

typedef struct {
	uint32_t	block, at, bytes;
	int			reqI, reqV, stI, stV;	// the requests and their states (FS_RangeState reports a finished one once)
	uint32_t	copiedFrame;			// the frame that copied it (0: none)
	qboolean	done;					// copied, dropped or failed: its space is free once copiedFrame's frame is complete
} sceneRead_t;

// best fit over the free ranges, which are kept merged (first fit split the big ranges and left the pools too
// scattered for a big block after a few jumps)
struct ScenePool {
	std::map<uint32_t, uint32_t>		freeRanges;	// start -> length
	std::multimap<uint32_t, uint32_t>	bySize;		// length -> start (the best fit's search)
	uint32_t							total = 0, used = 0;
	void add( uint32_t at, uint32_t n ) { freeRanges[at] = n; bySize.insert( std::make_pair( n, at ) ); }
	void remove( std::map<uint32_t, uint32_t>::iterator it ) {
		std::pair<std::multimap<uint32_t, uint32_t>::iterator, std::multimap<uint32_t, uint32_t>::iterator> r = bySize.equal_range( it->second );
		for ( std::multimap<uint32_t, uint32_t>::iterator s = r.first; s != r.second; ++s )
			if ( s->second == it->first ) {
				bySize.erase( s );
				break;
			}
		freeRanges.erase( it );
	}
	void reset( uint32_t n ) { freeRanges.clear(); bySize.clear(); if ( n ) add( 0, n ); total = n; used = 0; }
	bool alloc( uint32_t n, uint32_t *at ) {
		if ( !n ) {
			*at = 0;
			return true;
		}
		std::multimap<uint32_t, uint32_t>::iterator s = bySize.lower_bound( n );
		if ( s == bySize.end() )
			return false;
		const uint32_t start = s->second, len = s->first;
		remove( freeRanges.find( start ) );
		if ( len > n )
			add( start + n, len - n );
		*at = start;
		used += n;
		return true;
	}
	void release( uint32_t at, uint32_t n ) {
		if ( !n )
			return;
		used -= n;
		std::map<uint32_t, uint32_t>::iterator next = freeRanges.lower_bound( at );
		if ( next != freeRanges.begin() ) {
			std::map<uint32_t, uint32_t>::iterator prev = std::prev( next );
			if ( prev->first + prev->second == at ) {
				at = prev->first;
				n += prev->second;
				remove( prev );
			}
		}
		next = freeRanges.lower_bound( at );
		if ( next != freeRanges.end() && at + n == next->first ) {
			n += next->second;
			remove( next );
		}
		add( at, n );
	}
};

typedef struct { uint32_t frame, vAt, vN, iAt, iN; } sceneStreamFree_t;

struct sceneStream_t {
	qboolean						on = qfalse, settled = qfalse;
	std::vector<sceneShard_t>		shards;
	std::vector<sceneBlock_t>		blocks;
	std::vector<uint32_t>			blockParts, blockLods, lodFirstBlock, lodBlocks, lodMissing, partCmd;
	// stage C: each part's block and material, each LOD's parts, each block's world units per UV unit (0: not yet known),
	// the pixels per world unit at distance 1 of the last want's eye; the materials' wants (texels per UV unit)
	std::vector<uint32_t>			partBlock, partMat, lodFirstPart, lodNumParts;
	std::vector<float>				blockDensity, matWant, detailTiles;	// detailTiles: each material's detail layer's tiles per UV unit
	float							ppu = 0.0f;
	std::vector<uint8_t>			lodRes, lodWanted;
	std::vector<uint32_t>			queue;			// blocks to read, nearest first (from the last want)
	std::map<uint32_t, uint32_t>	lodChanges;		// this frame's LOD residency writes
	std::vector<sceneStreamFree_t>	pendingFree;
	std::deque<sceneRead_t>			reads;
	ScenePool						vPool, iPool;
	sceneBuffer_t					staging;
	byte							*stagingPtr = NULL;
	uint32_t						stagingSize = 0, head = 0;
	sceneBuffer_t					patch[NUM_COMMAND_BUFFERS];
	byte							*patchPtr[NUM_COMMAND_BUFFERS] = {};
	sceneBuffer_t					want, wantRead[NUM_COMMAND_BUFFERS];
	uint32_t						*wantReadPtr[NUM_COMMAND_BUFFERS] = {};
	uint32_t						wantFrame[NUM_COMMAND_BUFFERS] = {};
	VkDescriptorSetLayout			setLayout = VK_NULL_HANDLE;
	VkPipelineLayout				pipeLayout = VK_NULL_HANDLE;
	VkPipeline						pipe = VK_NULL_HANDLE;
	VkDescriptorPool				pool = VK_NULL_HANDLE;
	VkDescriptorSet					set[NUM_COMMAND_BUFFERS] = {};	// per slot (binding 4: its interiors near the eye)
	PFN_vkCmdFillBuffer				fillBuffer = NULL;
	uint32_t						numLods = 0, stamp = 0, evictIdleUntil = 0, wantedLods = 0;
	vec3_t							lastEye = { 0, 0, 0 };
	std::vector<sceneShadowView_t>	lsViews;		// the static light shadows' views, drawn again when a view settles
	// stage D: each mesh's listed faces (a face is drawn again once a LOD of a mesh in its list comes in), each LOD's mesh,
	// the faces waiting, the atlas's per-layer framebuffers for those draws
	std::vector<uint32_t>			meshFaceFirst, meshFaces, lodMesh;
	std::vector<uint8_t>			faceDirty;
	uint32_t						numDirty = 0, facesDrawn = 0;
	double							facesAt = 0.0, facesMs = 0.0;
	qboolean						facesWhole = qfalse;	// every face drawn once (the first settle): later settles may draw the dirty alone
	std::vector<VkImageView>		lsLayerViews;
	std::vector<VkFramebuffer>		lsFbs;
	// the in-frame faces' sets: per slot each view's cull set and the packing set as the light shadow path writes them,
	// made once (their own pool), and the fixed receivers they read (none, every full mesh, no far proxy)
	VkDescriptorPool				facePool = VK_NULL_HANDLE;
	VkDescriptorSet					faceCull[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS] = {};
	VkDescriptorSet					faceCompact[NUM_COMMAND_BUFFERS] = {};
	sceneBuffer_t					faceReceivers;
	int								faceSets = 0;		// 0 not tried, 1 made, -1 failed (the queue-waiting path then)
	uint64_t						totalBytes = 0, readBytes = 0, uploadBytes = 0;
	uint32_t						numReads = 0, numUploads = 0, numEvicted = 0, numBad = 0, statsFrame = 0;
	double							statsAt = 0.0;
	double							lastFrameAt = 0.0, maxGap = 0.0, maxWork = 0.0;	// the frames' spacing and this work's, the most in a stats second
	uint32_t						slowFrames = 0;		// frames over 25 ms apart in it
};
static sceneStream_t ss;

// r_sceneSkip (bench): the command template again with the skipped classes' commands drawing no indices, in every
// view (the eye's, its depth prepass, the cascades, the light shadows), so one class's whole cost shows as the frame
// time it takes away. Uploaded when the cvar changes (the device idle first: a bench switch, not a per-frame path).
static std::vector<VkDrawIndexedIndirectCommand> sceneCmdKeep;
static std::vector<uint32_t> sceneCmdMat;
static int sceneSkipApplied;
static uint32_t Scene_SkipClass( const sceneMaterial_t *m )
{
	if ( m->kind == SCENE_KIND_OPAQUE || m->kind == SCENE_KIND_CUTOUT ) {
		if ( m->srcFlags & 0x1000u )
			return 1u;
		if ( m->srcFlags & 0x800u )
			return 2u;
		return m->kind == SCENE_KIND_CUTOUT ? 4u : 8u;
	}
	return 16u;
}
static void Scene_BenchSkip( void )
{
	const int want = r_sceneSkip ? r_sceneSkip->integer : 0;
	if ( want == sceneSkipApplied || sceneCmdKeep.empty() || !sc.cmdTemplate.buf )
		return;
	if ( ss.on ) {	// (a streamed scene patches its template itself)
		ri.Printf( PRINT_WARNING, "r_sceneSkip: not with a streamed scene\n" );
		sceneSkipApplied = want;
		return;
	}
	std::vector<VkDrawIndexedIndirectCommand> cmds( sceneCmdKeep );
	uint32_t skipped = 0, tris = 0;
	for ( size_t c = 0; c < cmds.size(); c++ )
		if ( Scene_SkipClass( &sc.materials[sceneCmdMat[c]] ) & (uint32_t)want ) {
			tris += cmds[c].indexCount / 3u;
			cmds[c].indexCount = 0;
			skipped++;
		}
	qvkDeviceWaitIdle( vk.device );
	const VkDeviceSize size = (VkDeviceSize)cmds.size() * sizeof( VkDrawIndexedIndirectCommand );
	sceneBuffer_t staging;
	void *p;
	if ( !Scene_CreateBuffer( &staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene skip staging" ) )
		return;
	VK_CHECK( qvkMapMemory( vk.device, staging.mem, 0, VK_WHOLE_SIZE, 0, &p ) );
	memcpy( p, cmds.data(), (size_t)size );
	VkCommandBuffer cmd = vk_begin_command_buffer();
	VkBufferCopy region = { 0, 0, size };
	qvkCmdCopyBuffer( cmd, staging.buf, sc.cmdTemplate.buf, 1, &region );
	vk_end_command_buffer( cmd, __func__ );
	qvkUnmapMemory( vk.device, staging.mem );
	Scene_DestroyBuffer( &staging );
	sceneSkipApplied = want;
	ri.Printf( PRINT_ALL, "SCENESKIP %d: %u of %u part commands drawn empty (%u triangles at every LOD)\n", want, skipped,
		(uint32_t)cmds.size(), tris );
}

static uint32_t Scene_BlockBytes( const sceneBlock_t *b )
{
	return ( b->numIndices * 4u + b->vCount * 28u + 15u ) & ~15u;	// indices, then vertices, 16-aligned in the ring
}

// the shard (geometry file) whose range [base, base + count) of the given kind holds n
static int Scene_ShardOf( uint32_t n, qboolean vertices )
{
	int lo = 0, hi = (int)ss.shards.size() - 1, found = -1;
	while ( lo <= hi ) {
		const int mid = ( lo + hi ) / 2;
		const uint32_t base = vertices ? ss.shards[mid].vBase : ss.shards[mid].iBase;
		if ( base <= n ) {
			found = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return found;
}

static void Scene_StreamFree( void )
{
	if ( vk.device ) {
		for ( size_t i = 0; i < ss.reads.size(); i++ ) {	// the worker no longer writes the staging memory
			sceneRead_t *r = &ss.reads[i];
			if ( !r->done && ss.blocks[r->block].state == SB_READING ) {
				if ( r->reqI && !r->stI )
					ri.FS_RangeCancel( r->reqI );
				if ( r->reqV && !r->stV )
					ri.FS_RangeCancel( r->reqV );
			}
		}
		for ( size_t i = 0; i < ss.shards.size(); i++ )
			if ( ss.shards[i].file )
				ri.FS_RangeClose( ss.shards[i].file );
		if ( ss.stagingPtr )
			qvkUnmapMemory( vk.device, ss.staging.mem );
		Scene_DestroyBuffer( &ss.staging );
		for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
			if ( ss.patchPtr[f] )
				qvkUnmapMemory( vk.device, ss.patch[f].mem );
			Scene_DestroyBuffer( &ss.patch[f] );
			if ( ss.wantReadPtr[f] )
				qvkUnmapMemory( vk.device, ss.wantRead[f].mem );
			Scene_DestroyBuffer( &ss.wantRead[f] );
		}
		Scene_DestroyBuffer( &ss.want );
		for ( size_t l = 0; l < ss.lsFbs.size(); l++ ) {
			if ( ss.lsFbs[l] ) qvkDestroyFramebuffer( vk.device, ss.lsFbs[l], NULL );
			if ( ss.lsLayerViews[l] ) qvkDestroyImageView( vk.device, ss.lsLayerViews[l], NULL );
		}
		if ( ss.facePool ) qvkDestroyDescriptorPool( vk.device, ss.facePool, NULL );
		Scene_DestroyBuffer( &ss.faceReceivers );
		if ( ss.pipe ) qvkDestroyPipeline( vk.device, ss.pipe, NULL );
		if ( ss.pipeLayout ) qvkDestroyPipelineLayout( vk.device, ss.pipeLayout, NULL );
		if ( ss.setLayout ) qvkDestroyDescriptorSetLayout( vk.device, ss.setLayout, NULL );
		if ( ss.pool ) qvkDestroyDescriptorPool( vk.device, ss.pool, NULL );
	}
	ss = sceneStream_t();
}

/*
The blocks of the parts (each distinct first index, index count and vertex offset), each LOD's blocks, and the LODs'
records' last word set to whether nothing is missing (a LOD without geometry is resident from the start). Returns
qfalse when a part's indices cross a file's or reach past the scene's (not streamed: the caller refuses the scene).
*/
static qboolean Scene_StreamPlan( const xscnPart_t *parts, uint32_t numParts, xscnLod_t *lods, uint32_t numLods,
	const std::vector<uint32_t> &partCmd )
{
	std::vector<uint32_t> order;
	order.reserve( numParts );
	for ( uint32_t p = 0; p < numParts; p++ )
		if ( parts[p].numIndices )
			order.push_back( p );
	std::sort( order.begin(), order.end(), [parts]( uint32_t a, uint32_t b ) {
		if ( parts[a].firstIndex != parts[b].firstIndex ) return parts[a].firstIndex < parts[b].firstIndex;
		if ( parts[a].numIndices != parts[b].numIndices ) return parts[a].numIndices < parts[b].numIndices;
		return parts[a].vertexOffset < parts[b].vertexOffset; } );
	std::vector<uint32_t> partBlock( numParts, ~0u );
	for ( size_t k = 0; k < order.size(); k++ ) {
		const xscnPart_t *p = &parts[order[k]];
		if ( k == 0 || p->firstIndex != parts[order[k - 1]].firstIndex || p->numIndices != parts[order[k - 1]].numIndices
			|| p->vertexOffset != parts[order[k - 1]].vertexOffset ) {
			sceneBlock_t b;
			Com_Memset( &b, 0, sizeof( b ) );
			b.firstIndex = p->firstIndex;
			b.numIndices = p->numIndices;
			b.vStart = (uint32_t)MAX( p->vertexOffset, 0 );
			ss.blocks.push_back( b );
		}
		partBlock[order[k]] = (uint32_t)ss.blocks.size() - 1;
	}
	// a block's vertices: from its offset to the next block's start (the converter writes them in that order; a block
	// whose indices reach past that is read again with its vertices widened, Scene_StreamArrived), within its file
	std::vector<uint32_t> starts( ss.blocks.size() );
	for ( size_t i = 0; i < ss.blocks.size(); i++ )
		starts[i] = ss.blocks[i].vStart;
	std::sort( starts.begin(), starts.end() );
	starts.erase( std::unique( starts.begin(), starts.end() ), starts.end() );
	for ( size_t i = 0; i < ss.blocks.size(); i++ ) {
		sceneBlock_t *b = &ss.blocks[i];
		const int is = Scene_ShardOf( b->firstIndex, qfalse ), vs = Scene_ShardOf( b->vStart, qtrue );
		if ( is < 0 || vs < 0 || (uint64_t)b->firstIndex + b->numIndices > (uint64_t)ss.shards[is].iBase + ss.shards[is].iCount
			|| b->vStart >= ss.shards[vs].vBase + ss.shards[vs].vCount ) {
			ri.Printf( PRINT_WARNING, "WARNING: scene stream: a part's indices %u..%u or vertices from %u lie in no one file\n",
				b->firstIndex, b->firstIndex + b->numIndices, b->vStart );
			return qfalse;
		}
		b->iShard = (uint16_t)is;
		b->vShard = (uint16_t)vs;
		std::vector<uint32_t>::iterator next = std::upper_bound( starts.begin(), starts.end(), b->vStart );
		const uint32_t end = ss.shards[vs].vBase + ss.shards[vs].vCount;
		b->vCount = ( next == starts.end() ? end : MIN( *next, end ) ) - b->vStart;
		ss.totalBytes += Scene_BlockBytes( b );
	}
	// each block's parts (their draw commands get its place) and each LOD's distinct blocks
	ss.partCmd = partCmd;
	ss.partBlock = partBlock;
	ss.partMat.resize( numParts );
	for ( uint32_t p = 0; p < numParts; p++ )
		ss.partMat[p] = parts[p].material;
	ss.lodFirstPart.resize( numLods );
	ss.lodNumParts.resize( numLods );
	for ( uint32_t l = 0; l < numLods; l++ ) {
		ss.lodFirstPart[l] = lods[l].firstPart;
		ss.lodNumParts[l] = lods[l].numParts;
	}
	ss.blockDensity.assign( ss.blocks.size(), 0.0f );
	std::vector<uint32_t> count( ss.blocks.size() + 1, 0 );
	for ( uint32_t p = 0; p < numParts; p++ )
		if ( partBlock[p] != ~0u )
			count[partBlock[p] + 1]++;
	for ( size_t i = 0; i < ss.blocks.size(); i++ ) {
		ss.blocks[i].firstPart = count[i];
		ss.blocks[i].numParts = count[i + 1];
		count[i + 1] += count[i];
	}
	ss.blockParts.assign( order.size(), 0 );
	for ( size_t i = 0; i < ss.blocks.size(); i++ )
		ss.blocks[i].numParts = 0;
	for ( uint32_t p = 0; p < numParts; p++ )
		if ( partBlock[p] != ~0u ) {
			sceneBlock_t *b = &ss.blocks[partBlock[p]];
			ss.blockParts[b->firstPart + b->numParts++] = p;
		}
	ss.numLods = numLods;
	ss.lodFirstBlock.assign( numLods + 1, 0 );
	std::vector<uint32_t> blocksOf;
	for ( uint32_t l = 0; l < numLods; l++ ) {
		blocksOf.clear();
		for ( uint32_t p = lods[l].firstPart; p < lods[l].firstPart + lods[l].numParts; p++ )
			if ( partBlock[p] != ~0u )
				blocksOf.push_back( partBlock[p] );
		std::sort( blocksOf.begin(), blocksOf.end() );
		blocksOf.erase( std::unique( blocksOf.begin(), blocksOf.end() ), blocksOf.end() );
		ss.lodBlocks.insert( ss.lodBlocks.end(), blocksOf.begin(), blocksOf.end() );
		ss.lodFirstBlock[l + 1] = (uint32_t)ss.lodBlocks.size();
	}
	std::vector<uint32_t> lodCount( ss.blocks.size() + 1, 0 );
	for ( size_t k = 0; k < ss.lodBlocks.size(); k++ )
		lodCount[ss.lodBlocks[k] + 1]++;
	for ( size_t i = 0; i < ss.blocks.size(); i++ ) {
		ss.blocks[i].firstLod = lodCount[i];
		lodCount[i + 1] += lodCount[i];
		ss.blocks[i].numLods = 0;
	}
	ss.blockLods.assign( ss.lodBlocks.size(), 0 );
	for ( uint32_t l = 0; l < numLods; l++ )
		for ( uint32_t k = ss.lodFirstBlock[l]; k < ss.lodFirstBlock[l + 1]; k++ ) {
			sceneBlock_t *b = &ss.blocks[ss.lodBlocks[k]];
			ss.blockLods[b->firstLod + b->numLods++] = l;
		}
	ss.lodMissing.assign( numLods, 0 );
	ss.lodRes.assign( numLods, 0 );
	ss.lodWanted.assign( numLods, 0 );
	for ( uint32_t l = 0; l < numLods; l++ ) {
		ss.lodMissing[l] = ss.lodFirstBlock[l + 1] - ss.lodFirstBlock[l];
		ss.lodRes[l] = ss.lodMissing[l] ? 0 : 1;
		lods[l].pad = ss.lodRes[l];
	}
	return qtrue;
}

// a block's indices and vertices read now, on this thread (the water's band at load, a view settling)
static qboolean Scene_StreamReadNow( const sceneBlock_t *b, byte *dest )
{
	const sceneShard_t *is = &ss.shards[b->iShard], *vs = &ss.shards[b->vShard];
	const int ib = (int)( b->numIndices * 4u ), vb = (int)( b->vCount * 28u );
	return ( ri.FS_RangeRead( is->file, (int)( is->iOfs + ( b->firstIndex - is->iBase ) * 4u ), ib, dest ) == ib
		&& ri.FS_RangeRead( vs->file, (int)( vs->vOfs + ( b->vStart - vs->vBase ) * 28u ), vb, dest + ib ) == vb ) ? qtrue : qfalse;
}

// a part's indices and the vertices they reach, read now (the water's band at load); *vFirst: the first vertex read
static qboolean Scene_StreamPartNow( const xscnPart_t *p, std::vector<uint32_t> &idx, std::vector<float> &verts, int64_t *vFirst )
{
	const int is = Scene_ShardOf( p->firstIndex, qfalse );
	if ( is < 0 || !p->numIndices )
		return qfalse;
	const sceneShard_t *s = &ss.shards[is];
	const int ib = (int)( p->numIndices * 4u );
	idx.resize( p->numIndices );
	if ( ri.FS_RangeRead( s->file, (int)( s->iOfs + ( p->firstIndex - s->iBase ) * 4u ), ib, idx.data() ) != ib )
		return qfalse;
	uint32_t lo = 0xffffffffu, hi = 0;
	for ( uint32_t k = 0; k < p->numIndices; k++ ) {
		lo = MIN( lo, idx[k] );
		hi = MAX( hi, idx[k] );
	}
	const int64_t first = (int64_t)lo + p->vertexOffset, last = (int64_t)hi + p->vertexOffset;
	const int vs = first >= 0 ? Scene_ShardOf( (uint32_t)first, qtrue ) : -1;
	if ( vs < 0 || last >= (int64_t)ss.shards[vs].vBase + ss.shards[vs].vCount )
		return qfalse;
	const int vb = (int)( ( last - first + 1 ) * 28 );
	verts.resize( (size_t)( last - first + 1 ) * 7 );
	if ( ri.FS_RangeRead( ss.shards[vs].file, (int)( ss.shards[vs].vOfs + ( (uint32_t)first - ss.shards[vs].vBase ) * 28u ), vb, verts.data() ) != vb )
		return qfalse;
	*vFirst = first;
	return qtrue;
}

// the pools, the staging ring, the patch buffers, the want pass; the pools are sc.vertices and sc.indices
static qboolean Scene_StreamCreate( void )
{
	const uint64_t budget = (uint64_t)MAX( r_sceneStreamMB->integer, 64 ) << 20;
	uint64_t vTotal = 0, iTotal = 0, biggest = 0;
	for ( size_t i = 0; i < ss.shards.size(); i++ ) {
		vTotal += (uint64_t)ss.shards[i].vCount * 28u;
		iTotal += (uint64_t)ss.shards[i].iCount * 4u;
	}
	for ( size_t i = 0; i < ss.blocks.size(); i++ )
		biggest = MAX( biggest, (uint64_t)Scene_BlockBytes( &ss.blocks[i] ) );
	// the budget split as the scene's own vertices and indices are, neither past the scene's
	const double vShare = vTotal + iTotal ? (double)vTotal / (double)( vTotal + iTotal ) : 0.75;
	const uint64_t vBytes = MIN( (uint64_t)( budget * vShare ), vTotal + 28u ), iBytes = MIN( budget - (uint64_t)( budget * vShare ), iTotal + 4u );
	ss.vPool.reset( (uint32_t)( vBytes / 28u ) );
	ss.iPool.reset( (uint32_t)( iBytes / 4u ) );
	ss.stagingSize = (uint32_t)MIN( MAX( (uint64_t)256 << 20, biggest * 2u ), (uint64_t)1 << 30 );
	const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	// the want's readback in cached memory when there is any (the host reads it every few frames)
	const VkMemoryPropertyFlags readback = vk_find_memory_type_lazy( ~0u, host | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, NULL ) != ~0u
		? host | VK_MEMORY_PROPERTY_HOST_CACHED_BIT : host;
	ss.fillBuffer = (PFN_vkCmdFillBuffer)qvkGetDeviceProcAddr( vk.device, "vkCmdFillBuffer" );
	if ( !ss.fillBuffer )
		return qfalse;
	if ( !Scene_CreateBuffer( &sc.vertices, vBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene vertex pool" )
		|| !Scene_CreateBuffer( &sc.indices, iBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene index pool" )
		|| !Scene_CreateBuffer( &ss.staging, ss.stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, readback, "scene stream staging" )	// (cached: the host reads the indices back to check them; uncached reads cost up to 135 ms a frame)
		|| !Scene_CreateBuffer( &ss.want, (VkDeviceSize)MAX( ss.numLods, 1u ) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "scene stream want" ) )
		return qfalse;
	VK_CHECK( qvkMapMemory( vk.device, ss.staging.mem, 0, VK_WHOLE_SIZE, 0, (void **)&ss.stagingPtr ) );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( !Scene_CreateBuffer( &ss.patch[f], SCENE_STREAM_PATCH_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, "scene stream patches" )
			|| !Scene_CreateBuffer( &ss.wantRead[f], (VkDeviceSize)MAX( ss.numLods, 1u ) * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				readback, "scene stream want readback" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, ss.patch[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&ss.patchPtr[f] ) );
		VK_CHECK( qvkMapMemory( vk.device, ss.wantRead[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&ss.wantReadPtr[f] ) );
	}

	// scene_want.comp: the instances, meshes and LODs, the wanted distances, the slot's interiors near the eye
	static const VkDescriptorType types[5] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[5] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT };
	ss.setLayout = Scene_SetLayout( types, stages, 5 );
	if ( !ss.setLayout )
		return qfalse;
	VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * NUM_COMMAND_BUFFERS };
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 1;
	dp.pPoolSizes = &ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &ss.pool ) != VK_SUCCESS )
		return qfalse;
	VkDescriptorSetAllocateInfo da;
	Com_Memset( &da, 0, sizeof( da ) );
	da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	da.descriptorPool = ss.pool;
	da.descriptorSetCount = 1;
	da.pSetLayouts = &ss.setLayout;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( qvkAllocateDescriptorSets( vk.device, &da, &ss.set[f] ) != VK_SUCCESS )
			return qfalse;
		const sceneBuffer_t *bufs[5] = { &sc.instances, &sc.meshes, &sc.lods, &ss.want, &sc.interiorWant[f] };
		Scene_WriteSet( ss.set[f], bufs, types, 5 );
	}
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 48 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &ss.setLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &ss.pipeLayout ) != VK_SUCCESS )
		return qfalse;
	VkShaderModule cs = Scene_Shader( scene_want_comp_spv, scene_want_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = ss.pipeLayout;
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &ss.pipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	if ( r != VK_SUCCESS ) {
		ss.pipe = VK_NULL_HANDLE;
		return qfalse;
	}
	ri.Printf( PRINT_ALL, "scene stream: %u blocks (%.2f GB) in %u files, %u LODs; pools %.0f MB of vertices and %.0f MB of indices, "
		"staging %u MB\n", (uint32_t)ss.blocks.size(), ss.totalBytes / 1e9, (uint32_t)ss.shards.size(), ss.numLods,
		vBytes / 1048576.0, iBytes / 1048576.0, ss.stagingSize >> 20 );
	return qtrue;
}

// the want pass from the eye (into ss.want), its result copied to this slot's readback
static void Scene_StreamWantPass( VkCommandBuffer cb, int f, const sceneView_t *eye )
{
	float pc[12];
	VectorCopy( eye->origin, pc );
	pc[3] = MAX( r_sceneStreamMargin->value, 0.0f ) * 37.5f;
	pc[4] = MAX( r_sceneLodBias->value, 0.01f ) * eye->invTanHalfFovY * eye->lodScale;
	pc[5] = MAX( r_sceneCullPixels->value, 0.0f ) / eye->viewportHeight / eye->invTanHalfFovY;
	pc[6] = MAX( r_sceneHideScale->value, 0.0f );
	pc[7] = (float)Com_Clamp( 0, 2, r_sceneProxyDebug->integer );
	ss.ppu = eye->viewportHeight * eye->invTanHalfFovY * 0.5f;	// pixels per world unit at distance 1 (stage C)
	uint32_t counts[4] = { sc.numInstances, 0, 0, 0 };
	const float band = sc.fadeOn ? Com_Clamp( 0.0f, 2.0f, r_sceneLodFade->value ) : 0.0f;	// (scene_cull.comp's rc.look.y)
	Com_Memcpy( &counts[1], &band, 4 );
	Com_Memcpy( pc + 8, counts, sizeof( counts ) );
	ss.fillBuffer( cb, ss.want.buf, 0, VK_WHOLE_SIZE, 0xffffffffu );
	VkMemoryBarrier b;
	Com_Memset( &b, 0, sizeof( b ) );
	b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &b, 0, NULL, 0, NULL );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, ss.pipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, ss.pipeLayout, 0, 1, &ss.set[f], 0, NULL );
	qvkCmdPushConstants( cb, ss.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
	qvkCmdDispatch( cb, ( sc.numInstances + 63 ) / 64, 1, 1 );
	b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &b, 0, NULL, 0, NULL );
	VkBufferCopy region = { 0, 0, (VkDeviceSize)MAX( ss.numLods, 1u ) * 4 };
	qvkCmdCopyBuffer( cb, ss.want.buf, ss.wantRead[f].buf, 1, &region );
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &b, 0, NULL, 0, NULL );
}

static void Scene_StreamSetLod( uint32_t l, uint32_t resident )
{
	ss.lodRes[l] = (uint8_t)resident;
	ss.lodChanges[l] = resident;
	if ( resident )
		sceneRain.dirty = qtrue;	// it may lie under the rain view (Scene_RainFrame)
	// the faces whose lists hold its mesh are drawn again (stage D); with r_sceneSettleFaces also when it goes out, so a
	// face is waiting whenever what it would draw changed and a settle can leave the rest as they are
	if ( ( resident || r_sceneSettleFaces->value > 0.0f ) && l < ss.lodMesh.size() ) {
		const uint32_t m = ss.lodMesh[l];
		for ( uint32_t k = ss.meshFaceFirst[m]; k < ss.meshFaceFirst[m + 1]; k++ )
			if ( !ss.faceDirty[ss.meshFaces[k]] ) {
				ss.faceDirty[ss.meshFaces[k]] = 1;
				ss.numDirty++;
			}
	}
}

// each material's detail layer's tiles per UV unit (Scene_LoadDetail's parameters, before the water's take their place)
static void Scene_StreamDetailTiles( const std::vector<float> &params )
{
	if ( !ss.on )
		return;
	ss.detailTiles.assign( sc.numMaterials, 0.0f );
	for ( uint32_t i = 0; i < sc.numMaterials && i * 4u < params.size(); i++ )
		ss.detailTiles[i] = params[i * 4u];
}

// stage D's index: each mesh's faces from the faces' instance lists (lsIds, Scene_ListLightShadows), each LOD's mesh
static void Scene_StreamFaces( const std::vector<sceneShadowView_t> &views, const std::vector<uint32_t> &lsIds,
	const xscnInstance_t *inst, const xscnMesh_t *meshes, uint32_t numMeshes, uint32_t numLods )
{
	ss.lodMesh.assign( numLods, 0 );
	for ( uint32_t i = 0; i < numMeshes; i++ )
		for ( uint32_t l = meshes[i].firstLod; l < meshes[i].firstLod + meshes[i].numLods && l < numLods; l++ )
			ss.lodMesh[l] = i;
	std::vector<uint64_t> pairs;	// mesh << 32 | face
	for ( size_t v = 0; v < views.size(); v++ ) {
		if ( !views[v].listed )
			continue;
		for ( uint32_t k = views[v].first; k < views[v].first + views[v].count && k < lsIds.size(); k++ )
			pairs.push_back( ( (uint64_t)inst[lsIds[k]].mesh << 32 ) | (uint64_t)v );
	}
	std::sort( pairs.begin(), pairs.end() );
	pairs.erase( std::unique( pairs.begin(), pairs.end() ), pairs.end() );
	ss.meshFaceFirst.assign( numMeshes + 1, 0 );
	ss.meshFaces.resize( pairs.size() );
	for ( size_t k = 0; k < pairs.size(); k++ ) {
		ss.meshFaceFirst[( pairs[k] >> 32 ) + 1]++;
		ss.meshFaces[k] = (uint32_t)pairs[k];
	}
	for ( uint32_t i = 0; i < numMeshes; i++ )
		ss.meshFaceFirst[i + 1] += ss.meshFaceFirst[i];
	ss.faceDirty.assign( views.size(), 0 );
	ss.numDirty = 0;
}

// the in-frame faces' sets (see sceneStream_t); qfalse leaves the queue-waiting path
static qboolean Scene_StreamFaceSets( void )
{
	if ( ss.faceSets )
		return ss.faceSets > 0 ? qtrue : qfalse;
	ss.faceSets = -1;
	if ( !sc.gd || !sc.lsIds.buf || !sc.cmdInfoLs.buf || !sc.compactLayout || !sc.cullLayout || !sc.gdPipe[SCENE_GD_LSHADOW]
		|| !sc.gdPipe[SCENE_GD_LSHADOW_CUTOUT] || !r_sceneLightShadowLists->integer )
		return qfalse;
	float rv[28];
	Com_Memset( rv, 0, sizeof( rv ) );
	rv[19] = -1.0f;	// travel.w: every full mesh, no far proxy (scene_cull.comp)
	if ( !Scene_StaticBuffer( &ss.faceReceivers, rv, sizeof( rv ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene stream face receivers" ) )
		return qfalse;
	VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)( NUM_COMMAND_BUFFERS * ( SCENE_MAX_VIEWS * 16 + 3 * SCENE_MAX_VIEWS + 4 ) ) };
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = NUM_COMMAND_BUFFERS * ( SCENE_MAX_VIEWS + 1 );
	dp.poolSizeCount = 1;
	dp.pPoolSizes = &ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &ss.facePool ) != VK_SUCCESS )
		return qfalse;
	static const VkDescriptorType ssbo15[16] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const uint32_t compactArrays[7] = { SCENE_MAX_VIEWS, 1, SCENE_MAX_VIEWS, SCENE_MAX_VIEWS, 1, 1, 1 };
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = ss.facePool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.cullLayout;
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			if ( qvkAllocateDescriptorSets( vk.device, &da, &ss.faceCull[f][v] ) != VK_SUCCESS )
				return qfalse;
			// as the frame's cull set, the face lists in the occlusion list's place and the fixed receivers
			const sceneBuffer_t *bufs[16] = { &sc.instances, &sc.meshes, &sc.lods, &sc.partCmd, &sc.indirect[f][v], &sc.visible[f][0], &ss.faceReceivers,
				&sc.occPyramid, &sc.occStats[f], &sc.lsIds, &sc.occArgs, &sc.occTouched, &sc.sortList[f][v], &sc.cullGroups[f][v], &sc.interiorIn[f],
				&sc.cullPairs[f] };
			Scene_WriteSet( ss.faceCull[f][v], bufs, ssbo15, 16 );
		}
		da.pSetLayouts = &sc.compactLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &ss.faceCompact[f] ) != VK_SUCCESS )
			return qfalse;
		VkCopyDescriptorSet cp[7];	// the frame's packing set, cmdInfo then replaced by the faces'
		for ( int i = 0; i < 7; i++ ) {
			Com_Memset( &cp[i], 0, sizeof( cp[i] ) );
			cp[i].sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
			cp[i].srcSet = sc.compactSet[f][0];
			cp[i].srcBinding = i;
			cp[i].dstSet = ss.faceCompact[f];
			cp[i].dstBinding = i;
			cp[i].descriptorCount = compactArrays[i];
		}
		qvkUpdateDescriptorSets( vk.device, 0, NULL, 7, cp );
		VkDescriptorBufferInfo info = { sc.cmdInfoLs.buf, 0, VK_WHOLE_SIZE };
		VkWriteDescriptorSet w;
		Com_Memset( &w, 0, sizeof( w ) );
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = ss.faceCompact[f];
		w.dstBinding = 1;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w.pBufferInfo = &info;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	}
	ss.faceSets = 1;
	return qtrue;
}

// the atlas's layers as framebuffers for the draws after load, made once
static void Scene_StreamLayerFbs( void )
{
	if ( !ss.lsFbs.empty() )
		return;
	const uint32_t layers = MAX( sc.lsLayers, 1u );
	ss.lsLayerViews.assign( layers, VK_NULL_HANDLE );
	ss.lsFbs.assign( layers, VK_NULL_HANDLE );
	for ( uint32_t l = 0; l < layers; l++ ) {
		VkImageViewCreateInfo vc;
		Com_Memset( &vc, 0, sizeof( vc ) );
		vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vc.image = sc.lsImage;
		vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vc.format = sc.lsFormat;
		vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vc.subresourceRange.levelCount = 1;
		vc.subresourceRange.baseArrayLayer = l;
		vc.subresourceRange.layerCount = 1;
		VK_CHECK( qvkCreateImageView( vk.device, &vc, NULL, &ss.lsLayerViews[l] ) );
		VkFramebufferCreateInfo fb;
		Com_Memset( &fb, 0, sizeof( fb ) );
		fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb.renderPass = sc.lsPass[0];
		fb.attachmentCount = 1;
		fb.pAttachments = &ss.lsLayerViews[l];
		fb.width = fb.height = SCENE_LS_LAYER;
		fb.layers = 1;
		VK_CHECK( qvkCreateFramebuffer( vk.device, &fb, NULL, &ss.lsFbs[l] ) );
	}
}

/*
The waiting faces nearest the eye drawn again in the frame's own commands, before its culls (at most r_sceneStreamFaces
every 100 ms): the frame slot's view buffers first carry the faces' lists, then the frame's own (the barriers between).
Nothing waits on the queue. The atlas's earlier readers (the frames before, in queue order) come first.
*/
static qboolean Scene_StreamFacesFrame( VkCommandBuffer cb, int f, const vec3_t eye )
{
	if ( !ss.on || !ss.numDirty || r_sceneStreamFaces->integer <= 0 || ss.lsViews.empty() || !Scene_StreamFaceSets() )
		return qfalse;
	const double now = R_SunProfNow();
	if ( now - ss.facesAt < 100.0 )
		return qtrue;
	std::vector<std::pair<float, uint32_t> > byDist;
	for ( uint32_t v = 0; v < ss.faceDirty.size(); v++ )
		if ( ss.faceDirty[v] && ss.lsViews[v].listed )
			byDist.push_back( std::make_pair( Distance( eye, ss.lsViews[v].origin ), v ) );
	const size_t n = MIN( byDist.size(), (size_t)r_sceneStreamFaces->integer );
	std::partial_sort( byDist.begin(), byDist.begin() + n, byDist.end() );
	std::vector<const sceneShadowView_t *> faces;
	for ( size_t k = 0; k < n; k++ ) {
		faces.push_back( &ss.lsViews[byDist[k].second] );
		ss.faceDirty[byDist[k].second] = 0;
		ss.numDirty--;
	}
	if ( faces.empty() )
		return qtrue;
	std::sort( faces.begin(), faces.end(), []( const sceneShadowView_t *a, const sceneShadowView_t *b ) { return a->layer < b->layer; } );
	Scene_StreamLayerFbs();
	// the frames before read the atlas and this slot's buffers: their reads before these writes
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
	size_t next = 0;
	for ( int b = 0; next < faces.size(); b++ ) {
		const uint32_t layer = faces[next]->layer;
		uint32_t k = 0;
		while ( next + k < faces.size() && k < SCENE_MAX_VIEWS && faces[next + k]->layer == layer )
			k++;
		if ( b )	// the batch before read these buffers
			Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
				| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
		Scene_LightShadowBatch( cb, f, &faces[next], k, ss.lsFbs[layer], qtrue, SCENE_LS_LAYER, qtrue, ss.faceCull[f], ss.faceCompact[f] );
		next += k;
	}
	// the faces' reads of the slot's buffers before the frame writes them (its template copy, culls, packing); the atlas's
	// writes before this frame's reads of it
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
		| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
	ss.facesDrawn += (uint32_t)faces.size();
	ss.facesAt = R_SunProfNow();
	ss.facesMs = MAX( ss.facesMs, ss.facesAt - now );
	return qtrue;
}

/*
r_sceneRainFollow: the rain view placed on the eye (snapped to its texels, so what stays in it draws the same) and drawn
again in the frame's own commands, before its culls, once the eye is a quarter of it from its centre or once streamed
geometry came in (at most every SCENE_RAIN_REDRAW_MS): its record in the faces buffer written, its layer cleared and
drawn as Scene_RenderLightShadows draws it (every instance tested, the interiors too; the frame's packing, so light
sources cover too), but at the eye's LODs and nothing under half a metre. As Scene_StreamFacesFrame, nothing waits on the queue.
*/
static int Scene_RainSlot( int t )
{
	return ( ( t % SCENE_RAIN_TILES ) + SCENE_RAIN_TILES ) % SCENE_RAIN_TILES;
}

// the window's first tile along an axis: kept while the eye (e, in tiles) stays within three quarters of a tile of its
// middle, else the eye's nearest tile corner in the middle (the eye 1.25 tiles, 137 m, from its edge at the least)
static int Scene_RainWindow( float e, int w0, qboolean have )
{
	const float mid = 0.5f * SCENE_RAIN_TILES, d = e - (float)w0;
	if ( have && d >= mid - 0.75f && d <= mid + 0.75f )
		return w0;
	return (int)floorf( e + 0.5f ) - SCENE_RAIN_TILES / 2;
}

// a world tile as a view of its own: orthographic over it from the view's top to its bottom, drawn into its slot
static void Scene_RainTileView( int tx, int ty, sceneShadowView_t *v )
{
	const float w = sceneRain.tileW, h = 0.5f * w, cx = ( (float)tx + 0.5f ) * w, cy = ( (float)ty + 0.5f ) * w;
	const float top = sceneRain.top, bottom = sceneRain.bottom, range = MAX( top - bottom, 1.0f );
	*v = sceneRain.view;
	const float rows[4][4] = { { 1.0f / h, 0, 0, -cx / h }, { 0, 1.0f / h, 0, -cy / h }, { 0, 0, -1.0f / range, top / range }, { 0, 0, 0, 1.0f } };
	for ( int r = 0; r < 4; r++ )
		for ( int c = 0; c < 4; c++ )
			v->viewProj[c * 4 + r] = rows[r][c];
	const float planes[5][4] = { { 1, 0, 0, cx - h }, { -1, 0, 0, -( cx + h ) }, { 0, 1, 0, cy - h }, { 0, -1, 0, -( cy + h ) }, { 0, 0, 1, bottom } };
	Com_Memcpy( v->planes, planes, sizeof( planes ) );
	VectorSet( v->origin, cx, cy, top );
	const uint32_t texels = SCENE_LS_LAYER / SCENE_RAIN_TILES;
	v->size = texels;
	v->x = (uint32_t)Scene_RainSlot( tx ) * texels;
	v->y = (uint32_t)Scene_RainSlot( ty ) * texels;
}

static qboolean Scene_RainHeld( int tx, int ty )
{
	const int sx = Scene_RainSlot( tx ), sy = Scene_RainSlot( ty );
	return ( sceneRain.held[sx][sy] && sceneRain.slotX[sx][sy] == tx && sceneRain.slotY[sx][sy] == ty ) ? qtrue : qfalse;
}

/*
r_sceneRainTiles: the rain view as SCENE_RAIN_TILES x SCENE_RAIN_TILES world tiles, each in a fixed slot of the layer
(world tile t in slot t mod SCENE_RAIN_TILES: scene.frag samples it with wrap-around), the window around the eye
(Scene_RainWindow). A frame draws at most r_sceneRainTiles: the window's tiles not held yet, nearest the eye first,
then the ones streamed geometry left stale (all marked once it came in, at most every SCENE_RAIN_REDRAW_MS); every one
at once when none is held (the first frame, a jump). Each tile is culled over the GPU's group lists and drawn as the
whole view was (the eye's LODs, nothing under half a metre), so a texel holds what it held before. The face record names
the largest rectangle of held tiles around the eye's (2 texels in from its edge, the filter's reach); past it every
up-facing surface is wet, as past the view before.
*/
static void Scene_RainTiles( VkCommandBuffer cb, int f, const sceneView_t *eyeView, double now )
{
	const float *eye = eyeView->origin;
	const float tw = sceneRain.tileW;
	const int T = SCENE_RAIN_TILES;
	if ( !sceneRain.tiled ) {	// (a map load, or r_sceneRainTiles turned on): nothing held
		Com_Memset( sceneRain.held, 0, sizeof( sceneRain.held ) );
		Com_Memset( sceneRain.stale, 0, sizeof( sceneRain.stale ) );
		sceneRain.rx0 = sceneRain.ry0 = sceneRain.rx1 = sceneRain.ry1 = 0;
	}
	const int wx = Scene_RainWindow( eye[0] / tw, sceneRain.wx, sceneRain.tiled ), wy = Scene_RainWindow( eye[1] / tw, sceneRain.wy, sceneRain.tiled );
	const qboolean moved = ( !sceneRain.tiled || wx != sceneRain.wx || wy != sceneRain.wy ) ? qtrue : qfalse;
	sceneRain.tiled = qtrue;
	sceneRain.wx = wx;
	sceneRain.wy = wy;
	if ( sceneRain.dirty && now - sceneRain.at >= SCENE_RAIN_REDRAW_MS ) {	// streamed geometry: every held tile drawn again
		for ( int i = 0; i < T; i++ )
			for ( int j = 0; j < T; j++ )
				sceneRain.stale[i][j] = sceneRain.held[i][j];
		sceneRain.dirty = qfalse;
		sceneRain.at = now;
	}
	// the window's tiles to draw: those not held (nearest the eye first), then the stale ones (likewise)
	std::vector<std::pair<float, int> > need, stale;
	for ( int i = 0; i < T; i++ )
		for ( int j = 0; j < T; j++ ) {
			const int tx = wx + i, ty = wy + j;
			const float dx = ( (float)tx + 0.5f ) * tw - eye[0], dy = ( (float)ty + 0.5f ) * tw - eye[1];
			const float d = dx * dx + dy * dy;
			if ( !Scene_RainHeld( tx, ty ) )
				need.push_back( std::make_pair( d, i + j * T ) );
			else if ( sceneRain.stale[Scene_RainSlot( tx )][Scene_RainSlot( ty )] )
				stale.push_back( std::make_pair( d, i + j * T ) );
		}
	std::sort( need.begin(), need.end() );
	std::sort( stale.begin(), stale.end() );
	const size_t budget = need.size() == (size_t)( T * T ) ? (size_t)( T * T ) : (size_t)r_sceneRainTiles->integer;
	std::vector<int> todo;
	for ( size_t k = 0; k < need.size() && todo.size() < budget; k++ )
		todo.push_back( need[k].second );
	for ( size_t k = 0; k < stale.size() && todo.size() < budget; k++ )
		todo.push_back( stale[k].second );
	// what the slots hold once these are drawn
	for ( int t : todo ) {
		const int tx = wx + t % T, ty = wy + t / T, sx = Scene_RainSlot( tx ), sy = Scene_RainSlot( ty );
		sceneRain.slotX[sx][sy] = tx;
		sceneRain.slotY[sx][sy] = ty;
		sceneRain.held[sx][sy] = qtrue;
		sceneRain.stale[sx][sy] = qfalse;
	}
	// the largest rectangle of held window tiles around the eye's
	const int ei = MAX( 0, MIN( T - 1, (int)floorf( eye[0] / tw ) - wx ) ), ej = MAX( 0, MIN( T - 1, (int)floorf( eye[1] / tw ) - wy ) );
	int best = 0, rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
	for ( int i0 = 0; i0 <= ei; i0++ )
		for ( int i1 = ei + 1; i1 <= T; i1++ )
			for ( int j0 = 0; j0 <= ej; j0++ )
				for ( int j1 = ej + 1; j1 <= T; j1++ ) {
					const int area = ( i1 - i0 ) * ( j1 - j0 );
					if ( area <= best )
						continue;
					qboolean all = qtrue;
					for ( int i = i0; i < i1 && all; i++ )
						for ( int j = j0; j < j1 && all; j++ )
							all = Scene_RainHeld( wx + i, wy + j );
					if ( all ) {
						best = area;
						rx0 = wx + i0, ry0 = wy + j0, rx1 = wx + i1, ry1 = wy + j1;
					}
				}
	const qboolean rectChanged = ( rx0 != sceneRain.rx0 || ry0 != sceneRain.ry0 || rx1 != sceneRain.rx1 || ry1 != sceneRain.ry1 ) ? qtrue : qfalse;
	if ( todo.empty() && !rectChanged && !moved )
		return;
	sceneRain.rx0 = rx0, sceneRain.ry0 = ry0, sceneRain.rx1 = rx1, sceneRain.ry1 = ry1;
	VectorCopy( eye, sceneRain.lodEye );
	sceneRain.lodScale = MAX( r_sceneLodBias->value, 0.01f ) * eyeView->invTanHalfFovY * eyeView->lodScale;

	// the face record: the held rectangle (2 texels in) for the bounds and depth, the wrap-around's scale for the texels
	sceneShadowFace_t rec = sceneRain.face;
	{
		const float m = 2.0f * rec.texel[0], top = sceneRain.top, range = MAX( top - sceneRain.bottom, 1.0f );
		const float x0 = (float)rx0 * tw + m, x1 = (float)rx1 * tw - m, y0 = (float)ry0 * tw + m, y1 = (float)ry1 * tw - m;
		float rows[4][4] = { { 0, 0, 0, 2.0f }, { 0, 0, 0, 2.0f }, { 0, 0, -1.0f / range, top / range }, { 0, 0, 0, 1.0f } };	// none: all past it
		if ( best && x1 > x0 && y1 > y0 ) {
			rows[0][0] = 2.0f / ( x1 - x0 ), rows[0][3] = -( x0 + x1 ) / ( x1 - x0 );
			rows[1][1] = 2.0f / ( y1 - y0 ), rows[1][3] = -( y0 + y1 ) / ( y1 - y0 );
		}
		for ( int r = 0; r < 4; r++ )
			for ( int c = 0; c < 4; c++ )
				rec.viewProj[c * 4 + r] = rows[r][c];
		rec.texel[2] = 1.0f / ( (float)T * tw );
	}
	std::vector<sceneShadowView_t> tiles( todo.size() );
	std::vector<const sceneShadowView_t *> batch;
	for ( size_t k = 0; k < todo.size(); k++ ) {
		Scene_RainTileView( wx + todo[k] % T, wy + todo[k] / T, &tiles[k] );
		batch.push_back( &tiles[k] );
	}
	Scene_StreamLayerFbs();
	Scene_DeviceCommands();
	// the frames before read the atlas, the faces buffer and this slot's buffers: their reads before these writes
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
	sc.cmdUpdateBuffer( cb, sc.lsFaces.buf, (VkDeviceSize)( sc.rainFace - 1u ) * sizeof( sceneShadowFace_t ), sizeof( sceneShadowFace_t ), &rec );
	if ( !batch.empty() ) {
		qboolean timed = vk_gputime_section_begin( "scene rain cull" );
		sceneRainTimed = &timed;
		const VkFramebuffer fb = ss.lsFbs[sceneRain.view.layer];
		for ( size_t next = 0, b = 0; next < batch.size(); b++ ) {
			const uint32_t k = (uint32_t)MIN( batch.size() - next, (size_t)SCENE_MAX_VIEWS );
			if ( b )	// the batch before read these buffers
				Scene_LsBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
					| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
			Scene_LightShadowBatch( cb, f, &batch[next], k, fb, qtrue, SCENE_LS_LAYER, qtrue, ss.faceCull[f], sc.compactSet[f][0] );
			next += k;
		}
		sceneRainTimed = NULL;
		vk_gputime_section_end( timed );
	}
	// its reads of the slot's buffers before the frame writes them; the layer's and the record's writes before this frame's
	// reads of them
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
		| VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
	if ( !batch.empty() ) {
		sceneRain.draws++;
		sceneRain.tilesDrawn += (uint32_t)batch.size();
	}
	sceneRain.ms = MAX( sceneRain.ms, R_SunProfNow() - now );
}

static void Scene_RainFrame( VkCommandBuffer cb, int f, const sceneView_t *eyeView )
{
	const float *eye = eyeView->origin;
	if ( !sceneRain.follow || !sc.rainFace || !r_sceneRain->integer || !r_sceneRainFollow->integer || !sc.lsFaces.buf || !Scene_StreamFaceSets() )
		return;
	const double now = R_SunProfNow();
	if ( r_sceneRainTiles->integer > 0 && sceneRain.tileW > 0.0f ) {
		Scene_RainTiles( cb, f, eyeView, now );
		return;
	}
	if ( sceneRain.tiled ) {	// (r_sceneRainTiles turned off: the whole layer again)
		sceneRain.tiled = qfalse;
		sceneRain.drawn = qfalse;
	}
	const float step = 0.25f * sceneRain.half;
	const qboolean moved = ( !sceneRain.drawn || fabsf( eye[0] - sceneRain.view.origin[0] ) > step || fabsf( eye[1] - sceneRain.view.origin[1] ) > step ) ? qtrue : qfalse;
	if ( !moved && !( sceneRain.dirty && now - sceneRain.at >= SCENE_RAIN_REDRAW_MS ) )
		return;
	if ( moved ) {
		const float texel = sceneRain.face.texel[0];
		Scene_PlaceRain( floorf( eye[0] / texel + 0.5f ) * texel, floorf( eye[1] / texel + 0.5f ) * texel );
	}
	sceneRain.dirty = qfalse;
	VectorCopy( eye, sceneRain.lodEye );
	sceneRain.lodScale = MAX( r_sceneLodBias->value, 0.01f ) * eyeView->invTanHalfFovY * eyeView->lodScale;
	Scene_StreamLayerFbs();
	Scene_DeviceCommands();
	// the frames before read the atlas, the faces buffer and this slot's buffers: their reads before these writes
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
	sc.cmdUpdateBuffer( cb, sc.lsFaces.buf, (VkDeviceSize)( sc.rainFace - 1u ) * sizeof( sceneShadowFace_t ), sizeof( sceneShadowFace_t ), &sceneRain.face );
	const sceneShadowView_t *view = &sceneRain.view;
	qboolean timed = vk_gputime_section_begin( "scene rain cull" );
	sceneRainTimed = &timed;
	Scene_LightShadowBatch( cb, f, &view, 1, ss.lsFbs[view->layer], qfalse, SCENE_LS_LAYER, qfalse, ss.faceCull[f], sc.compactSet[f][0] );
	sceneRainTimed = NULL;
	vk_gputime_section_end( timed );
	// its reads of the slot's buffers before the frame writes them; the layer's and the record's writes before this frame's
	// reads of them
	Scene_LsBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
		| VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
	sceneRain.drawn = qtrue;
	sceneRain.draws++;
	sceneRain.at = R_SunProfNow();
	sceneRain.ms = MAX( sceneRain.ms, sceneRain.at - now );
}

// the waiting faces nearest the eye drawn again, at most r_sceneStreamFaces of them, at most every 100 ms (the queue
// waits on them: a short hitch, measured in the SCENESTREAM line)
static void Scene_StreamDrawFaces( const vec3_t eye )
{
	if ( !ss.numDirty || r_sceneStreamFaces->integer <= 0 || ss.lsViews.empty() )
		return;
	const double now = R_SunProfNow();
	if ( now - ss.facesAt < 100.0 )
		return;
	std::vector<std::pair<float, uint32_t> > byDist;
	for ( uint32_t v = 0; v < ss.faceDirty.size(); v++ )
		if ( ss.faceDirty[v] )
			byDist.push_back( std::make_pair( Distance( eye, ss.lsViews[v].origin ), v ) );
	const size_t n = MIN( byDist.size(), (size_t)r_sceneStreamFaces->integer );
	std::partial_sort( byDist.begin(), byDist.begin() + n, byDist.end() );
	std::vector<sceneShadowView_t> faces;
	for ( size_t k = 0; k < n; k++ )
		faces.push_back( ss.lsViews[byDist[k].second] );
	std::sort( faces.begin(), faces.end(), []( const sceneShadowView_t &a, const sceneShadowView_t &b ) { return a.layer < b.layer; } );
	const uint32_t layers = MAX( sc.lsLayers, 1u );
	Scene_StreamLayerFbs();
	std::vector<char> cleared( layers, 1 );
	if ( Scene_LightShadowsListed( faces, ss.lsFbs, cleared, SCENE_LS_LAYER, qtrue ) ) {	// (not drawn: they stay waiting for a settle)
		for ( size_t k = 0; k < n; k++ ) {
			ss.faceDirty[byDist[k].second] = 0;
			ss.numDirty--;
		}
		ss.facesDrawn += (uint32_t)faces.size();
	}
	ss.facesAt = R_SunProfNow();
	ss.facesMs = MAX( ss.facesMs, ss.facesAt - now );
}

// a want read back: the wanted LODs' blocks referenced, the missing ones queued nearest first
static void Scene_StreamTakeWant( const uint32_t *w )
{
	ss.stamp++;
	ss.queue.clear();
	ss.wantedLods = 0;
	for ( uint32_t l = 0; l < ss.numLods; l++ ) {
		const uint8_t now = w[l] != 0xffffffffu ? 1 : 0;
		if ( now != ss.lodWanted[l] ) {
			for ( uint32_t k = ss.lodFirstBlock[l]; k < ss.lodFirstBlock[l + 1]; k++ ) {
				sceneBlock_t *b = &ss.blocks[ss.lodBlocks[k]];
				if ( now )
					b->refs++;
				else if ( b->refs )
					b->refs--;
			}
			ss.lodWanted[l] = now;
		}
		if ( !now )
			continue;
		ss.wantedLods++;
		float d;
		Com_Memcpy( &d, &w[l], 4 );
		for ( uint32_t k = ss.lodFirstBlock[l]; k < ss.lodFirstBlock[l + 1]; k++ ) {
			const uint32_t id = ss.lodBlocks[k];
			sceneBlock_t *b = &ss.blocks[id];
			b->lastWanted = sc.frameNo;
			if ( b->state != SB_OUT )
				continue;
			if ( b->stamp != ss.stamp ) {
				b->stamp = ss.stamp;
				b->prio = d;
				ss.queue.push_back( id );
			} else {
				b->prio = MIN( b->prio, d );
			}
		}
	}
	std::sort( ss.queue.begin(), ss.queue.end(), []( uint32_t a, uint32_t b ) { return ss.blocks[a].prio < ss.blocks[b].prio; } );

	// stage C: each wanted LOD's materials want their images at its distance (texels per UV unit = world units per UV x
	// pixels per world unit there; a block not yet in counts as 2 m per UV unit), the margin's half nearer
	if ( !ss.ppu )
		return;
	ss.matWant.assign( sc.numMaterials, 0.0f );
	const float margin = MAX( r_sceneStreamMargin->value, 0.0f ) * 37.5f * 0.5f;
	for ( uint32_t l = 0; l < ss.numLods; l++ ) {
		if ( w[l] == 0xffffffffu )
			continue;
		float d;
		Com_Memcpy( &d, &w[l], 4 );
		const float ppu = ss.ppu / MAX( d - margin, 64.0f );
		for ( uint32_t p = ss.lodFirstPart[l]; p < ss.lodFirstPart[l] + ss.lodNumParts[l]; p++ ) {
			const uint32_t b = ss.partBlock[p], m = ss.partMat[p];
			// (a mesh is in metres, its instances scale it by the converter's 37.5 units a metre)
			const float density = ( b != ~0u && ss.blockDensity[b] > 0.0f ? ss.blockDensity[b] : 2.0f ) * 37.5f;
			if ( m < sc.numMaterials )
				ss.matWant[m] = MAX( ss.matWant[m], density * ppu );
		}
	}
	for ( uint32_t m = 0; m < sc.numMaterials; m++ ) {
		if ( ss.matWant[m] <= 0.0f )
			continue;
		for ( int k = 0; k < 3; k++ )
			R_StreamWantImage( sc.materials[m].image[k], ss.matWant[m] );
		// its tiled detail layer (maps/<map>.xdet): as many texels more per UV unit as it repeats
		if ( sc.materials[m].detail && m < ss.detailTiles.size() )
			R_StreamWantImage( sc.materials[m].detail, ss.matWant[m] * MAX( ss.detailTiles[m], 1.0f ) );
		if ( m < sceneSlotMats.size() ) {	// version 2: its slots likewise, its share maps at its own level
			const sceneSlotMat_t *sm = &sceneSlotMats[m];
			for ( int j = 0; j < 8; j++ )
				if ( sm->slot[j] )
					R_StreamWantImage( sm->slot[j], ss.matWant[m] * MAX( sm->tile[j], 1.0f ) );
			for ( int j = 0; j < 2; j++ )
				if ( sm->share[j] )
					R_StreamWantImage( sm->share[j], ss.matWant[m] );
		}
	}
}

// room in the staging ring (its entries free in the order they were taken)
// a block larger than the whole staging ring (widened past it, or over its 1 GB cap): marked bad, as a failed read is
static void Scene_StreamTooBig( sceneBlock_t *b )
{
	b->state = SB_BAD;
	ss.numBad++;
	ri.Printf( PRINT_WARNING, "WARNING: scene stream: a block of %s (%u bytes) is larger than the staging ring (%u), left out\n",
		ss.shards[b->iShard].path, Scene_BlockBytes( b ), ss.stagingSize );
}

static qboolean Scene_StreamRingAlloc( uint32_t n, uint32_t *at )
{
	if ( n > ss.stagingSize )
		return qfalse;
	if ( ss.reads.empty() )
		ss.head = 0;
	const uint32_t tail = ss.reads.empty() ? ss.head : ss.reads.front().at;
	if ( ss.reads.empty() || ss.head > tail || ( ss.head == tail && ss.reads.empty() ) ) {
		if ( ss.stagingSize - ss.head >= n ) {
			*at = ss.head;
			ss.head += n;
			return qtrue;
		}
		if ( !ss.reads.empty() && n < tail ) {	// wrap
			*at = 0;
			ss.head = n;
			return qtrue;
		}
		return qfalse;
	}
	if ( tail - ss.head > n ) {
		*at = ss.head;
		ss.head += n;
		return qtrue;
	}
	return qfalse;
}

// the queue's blocks put on the prefetch worker while the ring has room
static void Scene_StreamIssue( void )
{
	uint32_t inFlight = 0;
	for ( size_t i = 0; i < ss.reads.size(); i++ )
		inFlight += ss.reads[i].done ? 0 : 1;
	size_t k = 0;
	for ( ; k < ss.queue.size() && inFlight < SCENE_STREAM_MAX_READS; k++ ) {
		const uint32_t id = ss.queue[k];
		sceneBlock_t *b = &ss.blocks[id];
		if ( b->state != SB_OUT || !b->refs )
			continue;
		sceneRead_t r;
		Com_Memset( &r, 0, sizeof( r ) );
		r.block = id;
		r.bytes = Scene_BlockBytes( b );
		if ( r.bytes > ss.stagingSize ) {	// it can never fit the ring: left out (else it would hold back every block after it)
			Scene_StreamTooBig( b );
			continue;
		}
		if ( !Scene_StreamRingAlloc( r.bytes, &r.at ) )
			break;
		const sceneShard_t *is = &ss.shards[b->iShard], *vs = &ss.shards[b->vShard];
		byte *dest = ss.stagingPtr + r.at;
		r.reqI = ri.FS_RangeReadAsync( is->file, (int)( is->iOfs + ( b->firstIndex - is->iBase ) * 4u ), (int)( b->numIndices * 4u ), dest );
		r.reqV = r.reqI ? ri.FS_RangeReadAsync( vs->file, (int)( vs->vOfs + ( b->vStart - vs->vBase ) * 28u ), (int)( b->vCount * 28u ), dest + b->numIndices * 4u ) : 0;
		if ( !r.reqI || !r.reqV ) {
			if ( r.reqI )
				ri.FS_RangeCancel( r.reqI );
			b->state = SB_BAD;
			ss.numBad++;
			r.done = qtrue;
			ss.reads.push_back( r );
			continue;
		}
		b->state = SB_READING;
		ss.reads.push_back( r );
		ss.numReads++;
		ss.readBytes += r.bytes;
		inFlight++;
	}
	ss.queue.erase( ss.queue.begin(), ss.queue.begin() + k );
}

// a read block's indices checked against its vertices: qtrue to copy it; a block whose indices reach past them is read
// again once with its vertices widened to the largest index (it stays out), else given up
static qboolean Scene_StreamArrived( sceneBlock_t *b, const byte *data )
{
	const uint32_t *idx = (const uint32_t *)data;
	uint32_t hi = 0;
	for ( uint32_t k = 0; k < b->numIndices; k++ )
		hi = MAX( hi, idx[k] );
	if ( hi < b->vCount ) {
		// its world units per UV unit (stage C): the square root of the triangles' world area over their UV area
		const float *v = (const float *)( data + b->numIndices * 4u );
		double world = 0.0, uv = 0.0;
		for ( uint32_t k = 0; k + 2 < b->numIndices; k += 3 ) {
			const float *p0 = v + idx[k] * 7, *p1 = v + idx[k + 1] * 7, *p2 = v + idx[k + 2] * 7;
			vec3_t e1, e2, c;
			VectorSubtract( p1, p0, e1 );
			VectorSubtract( p2, p0, e2 );
			CrossProduct( e1, e2, c );
			world += VectorLength( c );
			uv += fabs( ( p1[5] - p0[5] ) * ( p2[6] - p0[6] ) - ( p2[5] - p0[5] ) * ( p1[6] - p0[6] ) );
		}
		ss.blockDensity[b - ss.blocks.data()] = uv > 1e-12 ? (float)sqrt( world / uv ) : 0.0f;
		return qtrue;
	}
	const sceneShard_t *vs = &ss.shards[b->vShard];
	if ( !b->retried && b->vStart + hi < vs->vBase + vs->vCount ) {
		b->retried = 1;
		b->vCount = hi + 1;
		b->state = SB_OUT;
		ss.queue.insert( ss.queue.begin(), (uint32_t)( b - ss.blocks.data() ) );
		return qfalse;
	}
	b->state = SB_BAD;
	ss.numBad++;
	ri.Printf( PRINT_WARNING, "WARNING: scene stream: block at index %u draws vertex %u of %u, given up\n", b->firstIndex, hi, b->vCount );
	return qfalse;
}

// resident blocks no wanted LOD uses, least recently wanted first, until the pools free n vertices and m indices (their
// ranges free once this frame is complete; their LODs leave now). Nothing more while earlier ones wait to be freed
static void Scene_StreamEvict( uint32_t nV, uint32_t nI )
{
	if ( sc.frameNo < ss.evictIdleUntil || !ss.pendingFree.empty() )
		return;
	std::vector<uint32_t> cand;
	for ( size_t i = 0; i < ss.blocks.size(); i++ )
		if ( ss.blocks[i].state == SB_RESIDENT && !ss.blocks[i].refs )
			cand.push_back( (uint32_t)i );
	if ( cand.empty() ) {
		ss.evictIdleUntil = sc.frameNo + 30;	// everything resident is wanted: the budget is full
		return;
	}
	// the oldest few thousand in order (sorting them all cost milliseconds a call)
	const size_t take = MIN( cand.size(), (size_t)4096 );
	std::partial_sort( cand.begin(), cand.begin() + take, cand.end(), []( uint32_t a, uint32_t b ) { return ss.blocks[a].lastWanted < ss.blocks[b].lastWanted; } );
	// free twice what is asked and 32 MB at least, so evicting is not a per-frame scan
	const uint64_t wantV = MAX( 2ull * nV, ( 24ull << 20 ) / 28u ), wantI = MAX( 2ull * nI, ( 8ull << 20 ) / 4u );
	uint64_t gotV = 0, gotI = 0;
	for ( size_t k = 0; k < take && ( gotV < wantV || gotI < wantI ); k++ ) {
		sceneBlock_t *b = &ss.blocks[cand[k]];
		for ( uint32_t j = b->firstLod; j < b->firstLod + b->numLods; j++ ) {
			const uint32_t l = ss.blockLods[j];
			if ( ss.lodRes[l] )
				Scene_StreamSetLod( l, 0 );
			ss.lodMissing[l]++;
		}
		sceneStreamFree_t fr = { sc.frameNo, b->vPool, b->vCount, b->iPool, b->numIndices };
		ss.pendingFree.push_back( fr );
		b->state = SB_OUT;
		gotV += b->vCount;
		gotI += b->numIndices;
		ss.numEvicted++;
	}
}

// pool ranges whose last user is complete
static void Scene_StreamRelease( qboolean all )
{
	size_t k = 0;
	for ( size_t i = 0; i < ss.pendingFree.size(); i++ ) {
		const sceneStreamFree_t *fr = &ss.pendingFree[i];
		if ( all || sc.frameNo >= fr->frame + NUM_COMMAND_BUFFERS ) {
			ss.vPool.release( fr->vAt, fr->vN );
			ss.iPool.release( fr->iAt, fr->iN );
		} else {
			ss.pendingFree[k++] = *fr;
		}
	}
	ss.pendingFree.resize( k );
}

/*
An arrived block into the pools: its copies recorded in cb, its parts' draw commands and its LODs' residency written
through this slot's patch buffer (*patchAt bytes used). qfalse when the pools have no room yet (it waits).
*/
static qboolean Scene_StreamPlace( VkCommandBuffer cb, int f, sceneRead_t *r, std::vector<VkBufferCopy> &cmdRegions, uint32_t *patchAt )
{
	sceneBlock_t *b = &ss.blocks[r->block];
	if ( *patchAt + b->numParts * 8u > SCENE_STREAM_PATCH_BYTES )
		return qfalse;
	uint32_t vAt, iAt;
	if ( !ss.vPool.alloc( b->vCount, &vAt ) ) {
		Scene_StreamEvict( b->vCount, b->numIndices );
		return qfalse;
	}
	if ( !ss.iPool.alloc( b->numIndices, &iAt ) ) {
		ss.vPool.release( vAt, b->vCount );
		Scene_StreamEvict( b->vCount, b->numIndices );
		return qfalse;
	}
	VkBufferCopy region;
	region.srcOffset = r->at;
	region.dstOffset = (VkDeviceSize)iAt * 4u;
	region.size = (VkDeviceSize)b->numIndices * 4u;
	qvkCmdCopyBuffer( cb, ss.staging.buf, sc.indices.buf, 1, &region );
	region.srcOffset = r->at + b->numIndices * 4u;
	region.dstOffset = (VkDeviceSize)vAt * 28u;
	region.size = (VkDeviceSize)b->vCount * 28u;
	qvkCmdCopyBuffer( cb, ss.staging.buf, sc.vertices.buf, 1, &region );
	b->vPool = vAt;
	b->iPool = iAt;
	b->state = SB_RESIDENT;
	for ( uint32_t k = b->firstPart; k < b->firstPart + b->numParts; k++ ) {
		const uint32_t cmd = ss.partCmd[ss.blockParts[k]];
		const uint32_t v[2] = { iAt, vAt };	// firstIndex, vertexOffset (its vertices start at the block's)
		Com_Memcpy( ss.patchPtr[f] + *patchAt, v, 8 );
		VkBufferCopy c = { *patchAt, (VkDeviceSize)cmd * sizeof( VkDrawIndexedIndirectCommand ) + 8, 8 };
		cmdRegions.push_back( c );
		*patchAt += 8;
	}
	for ( uint32_t j = b->firstLod; j < b->firstLod + b->numLods; j++ ) {
		const uint32_t l = ss.blockLods[j];
		if ( ss.lodMissing[l] && --ss.lodMissing[l] == 0 )
			Scene_StreamSetLod( l, 1 );
	}
	r->copiedFrame = sc.frameNo;
	r->done = qtrue;
	ss.numUploads++;
	ss.uploadBytes += r->bytes;
	return qtrue;
}

// the patches of this frame into the command template and the LOD records (after the copies of the blocks)
static void Scene_StreamPatch( VkCommandBuffer cb, int f, std::vector<VkBufferCopy> &cmdRegions, uint32_t patchAt )
{
	std::vector<VkBufferCopy> lodRegions;
	for ( std::map<uint32_t, uint32_t>::iterator it = ss.lodChanges.begin(); it != ss.lodChanges.end() && patchAt + 4 <= SCENE_STREAM_PATCH_BYTES; ) {
		Com_Memcpy( ss.patchPtr[f] + patchAt, &it->second, 4 );
		VkBufferCopy c = { patchAt, (VkDeviceSize)it->first * 16 + 12, 4 };
		lodRegions.push_back( c );
		patchAt += 4;
		it = ss.lodChanges.erase( it );
	}
	if ( !cmdRegions.empty() )
		qvkCmdCopyBuffer( cb, ss.patch[f].buf, sc.cmdTemplate.buf, (uint32_t)cmdRegions.size(), cmdRegions.data() );
	if ( !lodRegions.empty() )
		qvkCmdCopyBuffer( cb, ss.patch[f].buf, sc.lods.buf, (uint32_t)lodRegions.size(), lodRegions.data() );
}

/*
r_sceneSettleFaces: a settle after the first draws again only the waiting faces (those whose casters came in, stage D) that
lie within that many metres of the eye, through the listed path (each face's tile cleared and drawn); the rest wait for the
in-frame trickle (r_sceneStreamFaces, nearest first). Every other face holds what it held, which is what a full redraw
would draw again: a face is marked the moment a LOD in its list comes in. Before, every face (8,191 in Watson) was drawn
again on every settle: 1.1 s of a teleport's 1.7. False: the full redraw (the first settle, 0, no trickle to finish the
rest, a rain view kept where it was drawn, or the listed path unavailable).
*/
static qboolean Scene_SettleDirtyFaces( const vec3_t eye, uint32_t *drawn )
{
	const float reach = r_sceneSettleFaces->value * 37.5f;
	if ( !ss.facesWhole || reach <= 0.0f || r_sceneStreamFaces->integer <= 0 || ( sc.rainFace && !r_sceneRainFollow->integer )
		|| !r_sceneLightShadowLists->integer || !sc.gd || !sc.gdPipe[SCENE_GD_LSHADOW] || !sc.gdPipe[SCENE_GD_LSHADOW_CUTOUT]
		|| !sc.lsIds.buf || !sc.cmdInfoLs.buf )
		return qfalse;
	std::vector<sceneShadowView_t> faces;
	for ( uint32_t v = 0; v < ss.faceDirty.size(); v++ ) {
		if ( !ss.faceDirty[v] || !ss.lsViews[v].listed )
			continue;
		const sceneShadowView_t &w = ss.lsViews[v];	// the eye's distance to the face's bounds
		float d2 = 0.0f;
		for ( int c = 0; c < 3; c++ ) {
			const float o = eye[c] < w.lo[c] ? w.lo[c] - eye[c] : ( eye[c] > w.hi[c] ? eye[c] - w.hi[c] : 0.0f );
			d2 += o * o;
		}
		if ( d2 > reach * reach )
			continue;
		faces.push_back( w );
		ss.faceDirty[v] = 0;
		ss.numDirty--;
	}
	*drawn = (uint32_t)faces.size();
	if ( faces.empty() )
		return qtrue;
	std::sort( faces.begin(), faces.end(), []( const sceneShadowView_t &a, const sceneShadowView_t &b ) { return a.layer < b.layer; } );
	Scene_StreamLayerFbs();
	std::vector<char> cleared( MAX( sc.lsLayers, 1u ), 1 );
	Scene_LightShadowsListed( faces, ss.lsFbs, cleared, SCENE_LS_LAYER, qtrue );
	ss.facesDrawn += (uint32_t)faces.size();
	return qtrue;
}

/*
The view settles: what it wants is read now, on this thread, and copied before the frame goes on (the first view of the
scene, a view that jumped); then the static light shadows are drawn again with it. Runs between the frame's commands:
the queue is idle, so the staging ring and the pools' pending ranges are free.
*/
static void Scene_StreamSettle( int f, const sceneView_t *eye )
{
	const double t0 = R_SunProfNow();
	qvkQueueWaitIdle( vk.queue );
	for ( size_t i = 0; i < ss.reads.size(); i++ ) {	// what was on its way is put back
		sceneRead_t *r = &ss.reads[i];
		if ( r->done )
			continue;
		if ( r->reqI && !r->stI )
			ri.FS_RangeCancel( r->reqI );
		if ( r->reqV && !r->stV )
			ri.FS_RangeCancel( r->reqV );
		if ( ss.blocks[r->block].state == SB_READING || ss.blocks[r->block].state == SB_ARRIVED )
			ss.blocks[r->block].state = SB_OUT;
	}
	ss.reads.clear();
	ss.head = 0;
	Scene_StreamRelease( qtrue );
	ss.evictIdleUntil = 0;

	VkCommandBuffer cb = vk_begin_command_buffer();
	Scene_StreamWantPass( cb, f, eye );
	vk_end_command_buffer( cb, __func__ );
	Scene_StreamTakeWant( ss.wantReadPtr[f] );

	uint32_t placed = 0, failed = 0;
	uint64_t bytes = 0;
	size_t next = 0;
	while ( next < ss.queue.size() ) {
		// fill the ring, then copy it all in one submission
		std::vector<sceneRead_t> batch;
		uint32_t at = 0;
		while ( next < ss.queue.size() ) {
			sceneBlock_t *b = &ss.blocks[ss.queue[next]];
			if ( b->state != SB_OUT || !b->refs ) {
				next++;
				continue;
			}
			const uint32_t n = Scene_BlockBytes( b );
			if ( n > ss.stagingSize ) {	// it can never fit the ring: left out
				Scene_StreamTooBig( b );
				failed++;
				next++;
				continue;
			}
			if ( at + n > ss.stagingSize )
				break;
			if ( !Scene_StreamReadNow( b, ss.stagingPtr + at ) ) {
				b->state = SB_BAD;
				ss.numBad++;
				failed++;
				next++;
				continue;
			}
			if ( !Scene_StreamArrived( b, ss.stagingPtr + at ) ) {
				if ( b->state == SB_OUT ) {	// widened: read again now
					ss.queue.erase( ss.queue.begin() );	// (Scene_StreamArrived put it at the front)
					continue;
				}
				failed++;
				next++;
				continue;
			}
			sceneRead_t r;
			Com_Memset( &r, 0, sizeof( r ) );
			r.block = ss.queue[next];
			r.at = at;
			r.bytes = n;
			b->state = SB_ARRIVED;
			batch.push_back( r );
			at += n;
			bytes += n;
			next++;
		}
		if ( batch.empty() )
			break;
		cb = vk_begin_command_buffer();
		std::vector<VkBufferCopy> cmdRegions;
		uint32_t patchAt = 0;
		for ( size_t i = 0; i < batch.size(); i++ ) {
			if ( !Scene_StreamPlace( cb, f, &batch[i], cmdRegions, &patchAt ) ) {
				if ( patchAt + ss.blocks[batch[i].block].numParts * 8u > SCENE_STREAM_PATCH_BYTES ) {	// patches full: this batch first
					for ( size_t j = i; j < batch.size(); j++ )
						ss.blocks[batch[j].block].state = SB_OUT;
					next = 0;	// (the queue is walked again: placed blocks are skipped)
					break;
				}
				// the queue is idle: what was evicted is free now; evicted again until it fits (the free ranges may be
				// scattered) or nothing unwanted is left
				qboolean fit = qfalse;
				for ( int tries = 0; tries < 16 && !fit; tries++ ) {
					Scene_StreamRelease( qtrue );
					ss.evictIdleUntil = 0;
					fit = Scene_StreamPlace( cb, f, &batch[i], cmdRegions, &patchAt );
					if ( !fit && ss.pendingFree.empty() )
						break;	// nothing evicted: the budget is full of wanted blocks
				}
				if ( !fit ) {
					ss.blocks[batch[i].block].state = SB_OUT;
					failed++;
					continue;
				}
			}
			placed++;
		}
		VkMemoryBarrier mb;
		Com_Memset( &mb, 0, sizeof( mb ) );
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL );
		Scene_StreamPatch( cb, f, cmdRegions, patchAt );
		vk_end_command_buffer( cb, __func__ );
		Scene_StreamRelease( qtrue );
	}
	ss.queue.clear();
	ss.reads.clear();
	ss.head = 0;
	VectorCopy( eye->origin, ss.lastEye );
	ss.settled = qtrue;
	const double t1 = R_SunProfNow();
	uint32_t settleFaces = 0;
	if ( !ss.lsViews.empty() ) {
		if ( !Scene_SettleDirtyFaces( eye->origin, &settleFaces ) ) {
			Scene_RenderLightShadows( ss.lsViews );
			std::fill( ss.faceDirty.begin(), ss.faceDirty.end(), 0 );
			ss.numDirty = 0;
			ss.facesWhole = qtrue;
			settleFaces = (uint32_t)ss.lsViews.size();
		}
	}
	ri.Printf( r_sceneStreamStats->integer > 0 ? PRINT_ALL : PRINT_DEVELOPER, "scene stream: settled at %.0f %.0f %.0f: %u blocks (%.0f MB) read and placed in %.2f s (%u failed), %u LODs wanted; "
		"light shadows %.2f s (%u faces, %u left waiting); pools %.0f%% and %.0f%% used\n", eye->origin[0], eye->origin[1], eye->origin[2], placed, bytes / 1048576.0,
		( t1 - t0 ) * 0.001, failed, ss.wantedLods, ( R_SunProfNow() - t1 ) * 0.001, settleFaces, ss.numDirty,
		ss.vPool.total ? 100.0 * ss.vPool.used / ss.vPool.total : 0.0, ss.iPool.total ? 100.0 * ss.iPool.used / ss.iPool.total : 0.0 );
}

/*
A frame's streaming, before its culls (vk_begin_frame's R_SceneVkBeginFrame, this slot's fence waited): the slot's last
want taken, finished reads checked, arrived blocks copied (at most r_sceneStreamUploadMB), their commands and LODs
patched, more reads issued, a want pass recorded every SCENE_STREAM_WANT_EVERY frames. A first view or a jump settles.
*/
static void Scene_StreamFrame( VkCommandBuffer cb, int f, const sceneView_t *eye )
{
	const double workAt = R_SunProfNow();
	if ( ss.lastFrameAt > 0.0 ) {
		const double gap = workAt - ss.lastFrameAt;
		ss.maxGap = MAX( ss.maxGap, gap );
		ss.slowFrames += gap > 25.0 ? 1 : 0;
	}
	ss.lastFrameAt = workAt;
	if ( !ss.on ) {	// the frames' spacing alone (a scene that does not stream: the comparison)
		if ( r_sceneStreamStats->integer > 0 && workAt - ss.statsAt >= 1000.0 ) {
			ri.Printf( PRINT_ALL, "SCENESTREAM off: frames %u (%u over 25 ms apart, the most %.1f ms)\n", sc.frameNo - ss.statsFrame,
				ss.slowFrames, ss.maxGap );
			ss.slowFrames = 0;
			ss.maxGap = 0.0;
			ss.statsAt = workAt;
			ss.statsFrame = sc.frameNo;
		}
		return;
	}
	const float margin = MAX( r_sceneStreamMargin->value, 0.0f ) * 37.5f;
	if ( !ss.settled || Distance( eye->origin, ss.lastEye ) > MAX( margin * 0.5f, 2000.0f ) ) {
		Scene_StreamSettle( f, eye );
		for ( int k = 0; k < NUM_COMMAND_BUFFERS; k++ )	// (every slot's want: those recorded before the jump are from the old eye)
			ss.wantFrame[k] = 0;
		return;
	}
	VectorCopy( eye->origin, ss.lastEye );
	if ( ss.wantFrame[f] ) {	// this slot's want pass is complete (its fence was waited)
		Scene_StreamTakeWant( ss.wantReadPtr[f] );
		ss.wantFrame[f] = 0;
	}
	Scene_StreamRelease( qfalse );

	// earlier frames' reads of the template and the LOD records come before this frame's writes to them
	VkMemoryBarrier mb;
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL );

	// finished reads, then arrived blocks into the pools in the ring's order, as far as the frame's upload allows
	for ( size_t i = 0; i < ss.reads.size(); i++ ) {
		sceneRead_t *r = &ss.reads[i];
		sceneBlock_t *b = &ss.blocks[r->block];
		if ( r->done || b->state != SB_READING )
			continue;
		if ( !r->stI )
			r->stI = ri.FS_RangeState( r->reqI );
		if ( !r->stV )
			r->stV = ri.FS_RangeState( r->reqV );
		if ( r->stI < 0 || r->stV < 0 ) {
			if ( r->reqI && !r->stI )	// its other read, still on its way, no longer writes the ring (which may now reuse it)
				ri.FS_RangeCancel( r->reqI );
			if ( r->reqV && !r->stV )
				ri.FS_RangeCancel( r->reqV );
			b->state = SB_BAD;
			ss.numBad++;
			r->done = qtrue;
			ri.Printf( PRINT_WARNING, "WARNING: scene stream: a read of %s failed\n", ss.shards[r->stI < 0 ? b->iShard : b->vShard].path );
		} else if ( r->stI > 0 && r->stV > 0 ) {
			if ( Scene_StreamArrived( b, ss.stagingPtr + r->at ) )
				b->state = SB_ARRIVED;
			else
				r->done = qtrue;
		}
	}
	const uint64_t budget = (uint64_t)MAX( r_sceneStreamUploadMB->integer, 1 ) << 20;
	uint64_t copied = 0;
	std::vector<VkBufferCopy> cmdRegions;
	uint32_t patchAt = 0;
	for ( size_t i = 0; i < ss.reads.size() && copied < budget; i++ ) {
		sceneRead_t *r = &ss.reads[i];
		sceneBlock_t *b = &ss.blocks[r->block];
		if ( r->done || b->state != SB_ARRIVED )
			continue;
		if ( !b->refs ) {	// no longer wanted: dropped
			b->state = SB_OUT;
			r->done = qtrue;
			continue;
		}
		if ( !Scene_StreamPlace( cb, f, r, cmdRegions, &patchAt ) )
			break;
		copied += r->bytes;
	}
	if ( !cmdRegions.empty() || !ss.lodChanges.empty() ) {
		mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL );
		Scene_StreamPatch( cb, f, cmdRegions, patchAt );
	}
	// the ring's front entries free once the frame that copied them is complete
	while ( !ss.reads.empty() ) {
		const sceneRead_t *r = &ss.reads.front();
		if ( !r->done || ( r->copiedFrame && sc.frameNo < r->copiedFrame + NUM_COMMAND_BUFFERS ) )
			break;
		ss.reads.pop_front();
	}
	Scene_StreamIssue();
	if ( ss.faceSets < 0 )
		Scene_StreamDrawFaces( eye->origin );	// (else Scene_StreamFacesFrame, before the frame's culls)
	if ( sc.frameNo % SCENE_STREAM_WANT_EVERY == 0 ) {
		Scene_StreamWantPass( cb, f, eye );
		ss.wantFrame[f] = sc.frameNo;
	}
	// the pools and the patched records before the culls, the template's copy and the draws
	mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
		0, 1, &mb, 0, NULL, 0, NULL );
	ss.maxWork = MAX( ss.maxWork, R_SunProfNow() - workAt );

	if ( r_sceneStreamStats->integer > 0 ) {
		const double now = R_SunProfNow();
		if ( now - ss.statsAt >= 1000.0 ) {
			uint32_t reading = 0, arrived = 0, resident = 0;
			for ( size_t i = 0; i < ss.reads.size(); i++ )
				if ( !ss.reads[i].done )
					( ss.blocks[ss.reads[i].block].state == SB_READING ? reading : arrived )++;
			for ( size_t i = 0; i < ss.blocks.size(); i++ )
				resident += ss.blocks[i].state == SB_RESIDENT ? 1 : 0;
			const double secs = ss.statsAt > 0.0 ? ( now - ss.statsAt ) * 0.001 : 1.0;
			ri.Printf( PRINT_ALL, "SCENESTREAM resident %u blocks %.0f MB (pools %.0f%% %.0f%%), wanted LODs %u, queue %u, reading %u, arrived %u, "
				"read %.1f MB/s, uploaded %u blocks %.1f MB/s, evicted %u, bad %u, frames %u (%u over 25 ms apart, the most %.1f ms; streaming's most %.2f ms; light faces drawn %u, waiting %u, the most %.1f ms; rain view drawn %u, the most %.1f ms)\n", resident,
				( (double)ss.vPool.used * 28.0 + (double)ss.iPool.used * 4.0 ) / 1048576.0, 100.0 * ss.vPool.used / MAX( ss.vPool.total, 1u ),
				100.0 * ss.iPool.used / MAX( ss.iPool.total, 1u ), ss.wantedLods, (uint32_t)ss.queue.size(), reading, arrived,
				ss.readBytes / 1048576.0 / secs, ss.numUploads, ss.uploadBytes / 1048576.0 / secs, ss.numEvicted, ss.numBad, sc.frameNo - ss.statsFrame,
				ss.slowFrames, ss.maxGap, ss.maxWork, ss.facesDrawn, ss.numDirty, ss.facesMs, sceneRain.draws, sceneRain.ms );
			if ( sceneRain.tiled )
				ri.Printf( PRINT_ALL, "SCENESTREAM rain tiles drawn %u\n", sceneRain.tilesDrawn );
			sceneRain.draws = 0;
			sceneRain.tilesDrawn = 0;
			sceneRain.ms = 0.0;
			ss.facesDrawn = 0;
			ss.facesMs = 0.0;
			ss.slowFrames = 0;
			ss.maxGap = ss.maxWork = 0.0;
			ss.readBytes = ss.uploadBytes = 0;
			ss.numUploads = ss.numEvicted = 0;
			ss.statsAt = now;
			ss.statsFrame = sc.frameNo;
		}
	}
}

/*
Tiled scenes (docs/levelport-scene.md, "Tiled scenes, version 1"): maps/<map>.xman lists geometry shards ("lib"), the one
material table ("mat") and the tiles ("tile"), each an ordinary version 3 .xscn holding some sections. Every index in
them is global: the shards' sections end to end, then the material table, then the tiles' instances, lights and light
boxes, are a single scene's sections. They are joined here into one buffer laid out as a .xscn (sized from the
manifest's counts, each file's header checked against its line), which the loader then reads as it reads a .xscn.
The buffer is the heap's, not the zone's (whose sizes are ints): a header's 32-bit offsets reach 4 GB, and the joined
Watson and next-area scene is 3.6 GB. Returns the buffer (delete[] it) and its length, or NULL.
With stream (r_sceneStream) and every file stored in its pack, the buffer holds no vertices or indices: the tables are
range-read and the geometry files stay open in ss.shards, their geometry streamed (ss.on).
*/
static byte *Scene_ReadTiled( const char *manName, uint64_t *lenOut, qboolean stream )
{
	char *text = NULL;
	const int textLen = ri.FS_ReadFile( manName, (void **)&text );
	if ( textLen <= 0 || !text )
		return NULL;

	struct manFile_t { char path[MAX_QPATH]; uint32_t count[XS_COUNT]; };
	std::vector<manFile_t> files;
	std::string body( text, textLen );
	ri.FS_FreeFile( text );
	qboolean header = qfalse, bad = qfalse;	// the version line seen; a malformed line (sticky: the manifest is refused)
	size_t at = 0;
	while ( at < body.size() && !bad ) {
		size_t end = body.find( '\n', at );
		if ( end == std::string::npos )
			end = body.size();
		const std::string line = body.substr( at, end - at );
		at = end + 1;
		char kind[16], path[MAX_QPATH], id[64];
		manFile_t mf;
		Com_Memset( &mf, 0, sizeof( mf ) );
		unsigned int c[5] = { 0, 0, 0, 0, 0 };
		unsigned int shapes = 0;
		float bounds[6];
		int version = 0;
		if ( sscanf( line.c_str(), "%15s", kind ) != 1 )
			continue;
		if ( !strcmp( kind, "xman" ) ) {
			if ( sscanf( line.c_str(), "xman %d", &version ) == 1 && version == 1 )
				header = qtrue;
			else
				bad = qtrue;
		} else if ( !strcmp( kind, "lib" ) ) {
			if ( sscanf( line.c_str(), "lib %63s %u %u %u %u %u", path, &c[0], &c[1], &c[2], &c[3], &c[4] ) != 6 ) {
				bad = qtrue;
				break;
			}
			Q_strncpyz( mf.path, path, sizeof( mf.path ) );
			mf.count[XS_MESHES] = c[0];
			mf.count[XS_LODS] = c[1];
			mf.count[XS_PARTS] = c[2];
			mf.count[XS_VERTICES] = c[3];
			mf.count[XS_INDICES] = c[4];
			files.push_back( mf );
		} else if ( !strcmp( kind, "mat" ) ) {
			if ( sscanf( line.c_str(), "mat %63s %u", path, &c[0] ) != 2 ) {
				bad = qtrue;
				break;
			}
			Q_strncpyz( mf.path, path, sizeof( mf.path ) );
			mf.count[XS_MATERIALS] = c[0];
			files.push_back( mf );
		} else if ( !strcmp( kind, "tile" ) ) {
			if ( sscanf( line.c_str(), "tile %63s %63s %u %u %u", id, path, &c[0], &c[1], &c[2] ) != 5 ) {
				bad = qtrue;
				break;
			}
			// (then its bounds, six numbers, and a version 6 tile's shapes count: none, 0)
			if ( sscanf( line.c_str(), "tile %*s %*s %*u %*u %*u %f %f %f %f %f %f %u", &bounds[0], &bounds[1], &bounds[2], &bounds[3],
				&bounds[4], &bounds[5], &shapes ) != 7 )
				shapes = 0;
			Q_strncpyz( mf.path, path, sizeof( mf.path ) );
			mf.count[XS_INSTANCES] = c[0];
			mf.count[XS_LIGHTS] = c[1];
			mf.count[XS_BOXES] = c[2];
			mf.count[XS_SHAPES] = shapes;
			files.push_back( mf );
		}
	}
	if ( bad || !header || files.empty() ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s is not a version 1 scene manifest\n", manName );
		return NULL;
	}

	/*
	maps/<map>.xwat version 2: after its count of 104-byte records (version 1's), one 32-byte ocean record: u32 the ocean
	patch's mesh (its first LOD's first part gives the plane's geometry and material; 0xffffffff none), f32 the sea's level
	(game units), f32 min x, min y, max x, max y of the plane (game units; min >= max: the whole map), u32 pad[2]. The
	plane is that mesh scaled over the box (its mesh is the converter's 10 m patch, +-5 mesh units), 2 units under the
	level: appended as one more mesh, LOD, part and instance.
	Version 3: then the sea mask: u32 width, u32 height, f32 cell, f32 origin x, f32 origin y (game units, its min corner),
	width x height u8 (rows +y, columns +x; nonzero: sea). Its sea cells (within the box, unless that is the whole map)
	merged into maximal rectangles, a plane instance each (the ripples tile in world metres: no seams).
	*/
	uint32_t oceanMesh = 0xffffffffu;
	float ocean[5] = { 0, 0, 0, 0, 0 };	// level, min x, min y, max x, max y
	std::vector<float> oceanRects;	// v3: min x, min y, max x, max y of each plane
	uint32_t maskDims[2] = { 0, 0 }, maskSea = 0;
	float maskCell = 0.0f;
	{
		char wat[MAX_QPATH];
		COM_StripExtension( manName, wat, sizeof( wat ) );
		Q_strcat( wat, sizeof( wat ), ".xwat" );
		byte *wb = NULL;
		const int wl = ri.FS_ReadFile( wat, (void **)&wb );
		uint32_t hv[3] = { 0, 0, 0 };
		if ( wl >= 16 && wb && !memcmp( wb, "XWAT", 4 ) ) {
			Com_Memcpy( hv, wb + 4, 12 );
			const uint64_t oceanAt64 = 16 + (uint64_t)hv[1] * 104u;	// the record after the water records (64 bits: a count cannot wrap it)
			const size_t oceanAt = (size_t)oceanAt64;	// (used once the file is known to hold it)
			if ( ( hv[0] == 2 || hv[0] == 3 ) && (uint64_t)wl >= oceanAt64 + 32 ) {
				Com_Memcpy( &oceanMesh, wb + oceanAt, 4 );
				Com_Memcpy( ocean, wb + oceanAt + 4, sizeof( ocean ) );
			}
			if ( hv[0] == 3 && oceanMesh != 0xffffffffu ) {
				float mf[3] = { 0, 0, 0 };	// cell, origin x, origin y
				if ( (uint64_t)wl >= oceanAt64 + 52 ) {
					Com_Memcpy( maskDims, wb + oceanAt + 32, 8 );
					Com_Memcpy( mf, wb + oceanAt + 40, 12 );
				}
				maskCell = mf[0];
				const uint32_t mw = maskDims[0], mh = maskDims[1];
				if ( !mw || !mh || !( mf[0] > 0.0f ) || (uint64_t)mw * mh > (uint64_t)wl - MIN( (uint64_t)wl, oceanAt64 + 52 ) ) {
					ri.Printf( PRINT_WARNING, "WARNING: %s: version 3 without a whole sea mask (%ux%u, cell %g): no ocean\n", wat, mw, mh, mf[0] );
					oceanMesh = 0xffffffffu;
				} else {
					const byte *mk = wb + oceanAt + 52;
					const qboolean boxed = ocean[1] < ocean[3] && ocean[2] < ocean[4] ? qtrue : qfalse;
					std::vector<byte> used( (size_t)mw * mh, 0 );
					auto sea = [&]( uint32_t c, uint32_t r ) -> bool {
						const size_t k = (size_t)r * mw + c;
						if ( !mk[k] || used[k] )
							return false;
						const float x0 = mf[1] + c * mf[0], y0 = mf[2] + r * mf[0];
						return !boxed || ( x0 < ocean[3] && x0 + mf[0] > ocean[1] && y0 < ocean[4] && y0 + mf[0] > ocean[2] );
					};
					for ( uint32_t r = 0; r < mh; r++ )
						for ( uint32_t c = 0; c < mw; c++ ) {
							if ( !sea( c, r ) )
								continue;
							uint32_t w = 1, h = 1;
							while ( c + w < mw && sea( c + w, r ) )
								w++;
							for ( ; r + h < mh; h++ ) {
								uint32_t x = c;
								while ( x < c + w && sea( x, r + h ) )
									x++;
								if ( x < c + w )
									break;
							}
							for ( uint32_t y = r; y < r + h; y++ )
								Com_Memset( &used[(size_t)y * mw + c], 1, w );
							maskSea += w * h;
							float q[4] = { mf[1] + c * mf[0], mf[2] + r * mf[0], mf[1] + ( c + w ) * mf[0], mf[2] + ( r + h ) * mf[0] };
							if ( boxed ) {
								q[0] = MAX( q[0], ocean[1] );
								q[1] = MAX( q[1], ocean[2] );
								q[2] = MIN( q[2], ocean[3] );
								q[3] = MIN( q[3], ocean[4] );
							}
							oceanRects.insert( oceanRects.end(), q, q + 4 );
						}
					if ( oceanRects.empty() ) {
						ri.Printf( PRINT_WARNING, "WARNING: %s: the sea mask has no sea within the ocean's box: no ocean\n", wat );
						oceanMesh = 0xffffffffu;
					}
				}
			}
		}
		if ( wb )
			ri.FS_FreeFile( wb );
		if ( wl > 0 )
			ri.Printf( PRINT_DEVELOPER, "scene %s: version %u, %u records, %d bytes%s\n", wat, hv[0], hv[1], wl,
				oceanMesh != 0xffffffffu ? va( ", an ocean of mesh %u at z %.0f", oceanMesh, ocean[0] ) : "" );
	}

	// streaming: every file stored in its pack (the engine's range reads take only those), else everything is read
	std::vector<int> handles( files.size(), 0 ), lengths( files.size(), 0 );
	for ( size_t i = 0; i < files.size() && stream; i++ ) {
		handles[i] = ri.FS_RangeOpen( files[i].path, &lengths[i] );
		if ( !handles[i] ) {
			ri.Printf( PRINT_ALL, "scene %s: %s is not stored in its pack: the geometry is not streamed\n", manName, files[i].path );
			for ( size_t j = 0; j < i; j++ ) {
				ri.FS_RangeClose( handles[j] );
				handles[j] = 0;	// (closeAll below must not close it again)
			}
			stream = qfalse;
		}
	}

	// the joined layout: the header, then each section's records from every file in manifest order (streaming: no
	// vertices or indices)
	uint64_t total[XS_COUNT], joined[XS_COUNT], ofs[XS_COUNT], size = Scene_HeaderSize( SCENE_VERSION );
	for ( int k = 0; k < XS_COUNT; k++ ) {
		total[k] = 0;
		for ( size_t i = 0; i < files.size(); i++ )
			total[k] += files[i].count[k];
	}
	if ( oceanMesh != 0xffffffffu && oceanMesh >= total[XS_MESHES] ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: the ocean's mesh %u is past the scene's %llu: no ocean\n", manName, oceanMesh,
			(unsigned long long)total[XS_MESHES] );
		oceanMesh = 0xffffffffu;
	}
	const qboolean hasOcean = oceanMesh != 0xffffffffu ? qtrue : qfalse;
	const uint32_t numPlanes = !hasOcean ? 0 : oceanRects.empty() ? 1 : (uint32_t)( oceanRects.size() / 4 );
	for ( int k = 0; k < XS_COUNT; k++ ) {
		const uint64_t extra = !hasOcean ? 0 : k == XS_INSTANCES ? numPlanes : ( k == XS_MESHES || k == XS_LODS || k == XS_PARTS ) ? 1 : 0;
		// (streaming: no vertices or indices; whole-file scenes join every section, the ocean's records after the files')
		joined[k] = stream && ( k == XS_VERTICES || k == XS_INDICES ) ? 0 : total[k] + extra;
	}
	joined[XS_SHAPES] = total[XS_SHAPES] ? total[XS_LIGHTS] : 0;	// one a light once any file has them (a version 5 file's: points)
	for ( int k = 0; k < XS_COUNT; k++ ) {
		ofs[k] = size;
		size += joined[k] * xsRecordSize[k];
	}
	auto closeAll = [&]() { for ( size_t i = 0; i < handles.size(); i++ ) if ( handles[i] ) ri.FS_RangeClose( handles[i] ); };
	if ( total[XS_BOXES] ) {	// light boxes are one a light in every file or in none (the joined scene is checked as one)
		for ( size_t i = 0; i < files.size(); i++ ) {
			if ( files[i].count[XS_BOXES] != files[i].count[XS_LIGHTS] ) {
				ri.Printf( PRINT_WARNING, "WARNING: %s: %s has %u light boxes for its %u lights, while other tiles have them\n", manName,
					files[i].path, files[i].count[XS_BOXES], files[i].count[XS_LIGHTS] );
				closeAll();
				return NULL;
			}
		}
	}
	if ( size > 0xffffffffu || total[XS_VERTICES] > 0x7fffffffu || total[XS_INDICES] > 0xffffffffu ) {	// (a part's vertex offset is signed)
		ri.Printf( PRINT_WARNING, "WARNING: %s: the joined scene is %llu bytes, past what one scene buffer holds\n", manName,
			(unsigned long long)size );
		closeAll();
		return NULL;
	}
	byte *buf = new ( std::nothrow ) byte[(size_t)size];
	if ( !buf ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: no memory for the joined scene's %llu bytes\n", manName, (unsigned long long)size );
		closeAll();
		return NULL;
	}
	Com_Memset( buf, 0, Scene_HeaderSize( SCENE_VERSION ) );
	Com_Memcpy( buf, "XSCN", 4 );
	const uint32_t version = SCENE_VERSION;	// the layout of 3; a 3 file's pad bytes are zero (no hides), a 3 or 4 file's flags word too (no near distances); 6 the shapes
	Com_Memcpy( buf + 4, &version, 4 );
	for ( int k = 0; k < XS_COUNT; k++ ) {
		const uint32_t c = (uint32_t)joined[k], o = (uint32_t)ofs[k];
		Com_Memcpy( buf + 8 + 4 * k, &c, 4 );
		Com_Memcpy( buf + 8 + 4 * XS_COUNT + 4 * k, &o, 4 );
	}
	uint64_t filled[XS_COUNT];
	Com_Memset( filled, 0, sizeof( filled ) );
	qboolean ok = qtrue;	// every listed file read, its counts as its line says
	uint32_t geomV = 0, geomI = 0;	// streaming: the next file's first vertex and index in the scene's numbering
	ss.shards.clear();
	for ( size_t i = 0; i < files.size() && ok; i++ ) {
		byte *fb = NULL;
		byte hdr[88];
		const int hlen = lengths[i] >= 88 ? 88 : 80;	// (a version 6 header's; 5's is 80)
		const int flen = stream ? ( lengths[i] >= 80 && ri.FS_RangeRead( handles[i], 0, hlen, hdr ) == hlen ? lengths[i] : 0 )
			: ri.FS_ReadFile( files[i].path, (void **)&fb );
		xscnHeader_t fh;
		if ( flen <= 0 || ( !stream && !fb ) || !Scene_ReadHeader( stream ? hdr : fb, flen, &fh ) || fh.version < 3 ) {	// 3 to 6: one layout (6 adds the shapes)
			ri.Printf( PRINT_WARNING, "WARNING: %s: %s is missing or not a version 3 to 6 render scene\n", manName, files[i].path );
			ok = qfalse;
		} else {
			for ( int k = 0; k < XS_COUNT && ok; k++ ) {
				if ( k == XS_SHAPES && !fh.count[k] && !files[i].count[k] ) {	// a file without shapes: its lights points
					const uint64_t none = joined[k] ? (uint64_t)fh.count[XS_LIGHTS] * xsRecordSize[k] : 0;
					Com_Memset( buf + ofs[k] + filled[k], 0, (size_t)none );
					filled[k] += none;
					continue;
				}
				if ( fh.count[k] != files[i].count[k] ) {
					ri.Printf( PRINT_WARNING, "WARNING: %s: %s holds %u records of section %d, its line says %u\n", manName,
						files[i].path, fh.count[k], k, files[i].count[k] );
					ok = qfalse;
					break;
				}
				const uint64_t bytes = (uint64_t)fh.count[k] * xsRecordSize[k];
				if ( stream && ( k == XS_VERTICES || k == XS_INDICES ) )
					continue;	// streamed
				if ( stream ) {
					if ( bytes && ri.FS_RangeRead( handles[i], (int)fh.ofs[k], (int)bytes, buf + ofs[k] + filled[k] ) != (int)bytes ) {
						ri.Printf( PRINT_WARNING, "WARNING: %s: %s could not be read\n", manName, files[i].path );
						ok = qfalse;
						break;
					}
				} else {
					Com_Memcpy( buf + ofs[k] + filled[k], fb + fh.ofs[k], (size_t)bytes );
				}
				filled[k] += bytes;
			}
			if ( ok && stream && ( fh.count[XS_VERTICES] || fh.count[XS_INDICES] ) ) {	// a geometry file: kept open
				sceneShard_t sh;
				Com_Memset( &sh, 0, sizeof( sh ) );
				Q_strncpyz( sh.path, files[i].path, sizeof( sh.path ) );
				sh.file = handles[i];
				handles[i] = 0;
				sh.vBase = geomV;
				sh.vCount = fh.count[XS_VERTICES];
				sh.iBase = geomI;
				sh.iCount = fh.count[XS_INDICES];
				sh.vOfs = fh.ofs[XS_VERTICES];
				sh.iOfs = fh.ofs[XS_INDICES];
				ss.shards.push_back( sh );
			}
			geomV += fh.count[XS_VERTICES];
			geomI += fh.count[XS_INDICES];
		}
		if ( fb )
			ri.FS_FreeFile( fb );
	}
	closeAll();
	if ( !ok ) {
		delete[] buf;
		for ( size_t i = 0; i < ss.shards.size(); i++ )
			ri.FS_RangeClose( ss.shards[i].file );
		ss.shards.clear();
		return NULL;
	}
	ss.on = stream && !ss.shards.empty() ? qtrue : qfalse;
	if ( hasOcean ) {	// the plane: the patch mesh's first part over the box, 2 units under the level, as the sections' last records
		xscnMesh_t *meshes = (xscnMesh_t *)( buf + ofs[XS_MESHES] );
		xscnLod_t *lods = (xscnLod_t *)( buf + ofs[XS_LODS] );
		xscnPart_t *parts = (xscnPart_t *)( buf + ofs[XS_PARTS] );
		xscnInstance_t *inst = (xscnInstance_t *)( buf + ofs[XS_INSTANCES] );
		const uint32_t nm = (uint32_t)total[XS_MESHES], nl = (uint32_t)total[XS_LODS], np = (uint32_t)total[XS_PARTS], ni = (uint32_t)total[XS_INSTANCES];
		const xscnMesh_t src = meshes[oceanMesh];
		if ( !src.numLods || src.firstLod >= nl || !lods[src.firstLod].numParts || lods[src.firstLod].firstPart >= np ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s: the ocean's mesh %u has no part: the plane is a patch of nothing\n", manName, oceanMesh );
			Com_Memset( &parts[np], 0, sizeof( parts[np] ) );	// (no indices: nothing drawn; not read before the arrays)
		} else {
			parts[np] = parts[lods[src.firstLod].firstPart];
		}
		lods[nl].firstPart = np;
		lods[nl].numParts = 1;
		lods[nl].screen = 0.0f;
		lods[nl].pad = 0;
		meshes[nm] = src;
		meshes[nm].firstLod = nl;
		meshes[nm].numLods = 1;
		float lo[2] = { ocean[1], ocean[2] }, hi[2] = { ocean[3], ocean[4] };
		sceneOceanWhole = oceanRects.empty() && ( lo[0] >= hi[0] || lo[1] >= hi[1] ) ? qtrue : qfalse;
		if ( sceneOceanWhole ) {	// the whole map: the instance bounds fit it to the rest's drawn extent
			lo[0] = lo[1] = -1.0f;
			hi[0] = hi[1] = 1.0f;
		}
		if ( oceanRects.empty() )
			oceanRects.insert( oceanRects.end(), { lo[0], lo[1], hi[0], hi[1] } );
		for ( uint32_t j = 0; j < numPlanes; j++ ) {
			const float *q = &oceanRects[(size_t)j * 4];
			xscnInstance_t *in = &inst[ni + j];
			Com_Memset( in, 0, sizeof( *in ) );
			in->m[0] = ( q[2] - q[0] ) / 10.0f;	// the patch spans 10 mesh units
			in->m[3] = ( q[0] + q[2] ) * 0.5f;
			in->m[5] = ( q[3] - q[1] ) / 10.0f;
			in->m[7] = ( q[1] + q[3] ) * 0.5f;
			in->m[10] = 1.0f;
			in->m[11] = ocean[0] - 2.0f;
			in->mesh = nm;
		}
		sceneOceanInst = ni;
		sceneOceanCount = numPlanes;
		if ( sceneOceanWhole )
			ri.Printf( PRINT_ALL, "scene ocean: mesh %u's part (material %u) as a plane at z %.0f over the whole map\n", oceanMesh,
				parts[np].material, ocean[0] );
		else if ( maskDims[0] )
			ri.Printf( PRINT_ALL, "scene ocean: mesh %u's part (material %u) at z %.0f: %u planes over the %u sea cells of a %ux%u "
				"mask of %.0f-unit cells\n", oceanMesh, parts[np].material, ocean[0], numPlanes, maskSea, maskDims[0], maskDims[1], maskCell );
		else
			ri.Printf( PRINT_ALL, "scene ocean: mesh %u's part (material %u) as a plane at z %.0f over x %.0f..%.0f, y %.0f..%.0f\n",
				oceanMesh, parts[np].material, ocean[0], lo[0], hi[0], lo[1], hi[1] );
	}
	ri.Printf( PRINT_ALL, "scene %s: %u files joined, %llu bytes%s\n", manName, (unsigned)files.size(), (unsigned long long)size,
		ss.on ? " (tables alone: the geometry streams)" : "" );
	*lenOut = size;
	return buf;
}

/*
Scene environments: maps/<map>.xenv, version 1 (or the file r_sceneEnv names), read as the map loads. Text lines of
`key value...`, '#' starting a comment:
  xenv 1
  scene <map>       the render scene of maps/<map> in place of this map's own (a day map sharing the night map's packs)
  time day|night    a day scene drops the lights whose type has bit 20 (night only) and scales the emissives whose
                    material has flag 0x100 (night only: lamp bulbs, lit rooms) by r_sceneDayLamps; a night scene
                    (the default) drops the lights with bit 21 (day only). (Bits 4-5 are a signal lamp's index, 6 up its slot)
  sun <r> <g> <b> <intensity> <yaw> <elevation>   the sun in place of the map's q3gl2_sun (its order; degrees)
  <cvar> <value>    one of the look cvars (Scene_LoadEnv's list): its value while the map is loaded; set again after
                    the load, the cvar's own wins (the console's)
The look values are read through Env( cvar ), which gives the environment's while the cvar is as it was at the load.
*/
#define SCENE_ENV_VARS	64	// (more than the look values, 50: a line each; was 32)
typedef struct {
	cvar_t	*cv;
	int		count;		// the cvar's modificationCount at the load: changed since, its own value wins
	cvar_t	shadow;		// the environment's value, read as the cvar
	char	str[96];
} sceneEnvVar_t;
static sceneEnvVar_t	sceneEnvVars[SCENE_ENV_VARS];
static int				sceneEnvCount;
static qboolean			sceneEnvDay;

const cvar_t *Scene_Env( const cvar_t *cv )
{
	for ( int i = 0; i < sceneEnvCount; i++ )
		if ( sceneEnvVars[i].cv == cv )
			return cv->modificationCount == sceneEnvVars[i].count ? &sceneEnvVars[i].shadow : cv;
	return cv;
}

// the map's environment, if any: its look values, time and sun set; the render scene's map (its own, or the one its
// `scene` names) in sceneMap
static void Scene_LoadEnv( const char *bspName, char *sceneMap, int size )
{
	static cvar_t **const look[] = { &r_sceneSun, &r_sceneAmbient, &r_sceneExposure, &r_sceneEmissive, &r_sceneGlow, &r_sceneGlowThreshold, &r_sceneTonemap, &r_sceneFog, &r_sceneFogDensity, &r_sceneFogHeight, &r_sceneFogMax, &r_sceneFogLight, &r_sceneFogPhase, &r_sceneFogAlbedo, &r_sceneFogFar, &r_sceneFogStart, &r_sceneFogMoon, &r_sceneGradeExposure, &r_sceneLightScale, &r_sceneGrade, &r_sceneFogLightMax, &r_sceneDayLamps, &r_sceneSky, &r_sceneSkyZenith, &r_sceneSkyHorizon, &r_sceneSkySun, &r_sceneSkyGround, &r_sceneSkyCurve, &r_sceneCharSun, &r_sceneNightOnlyByDay, &r_sceneDayOnlyAtNight, &r_sceneWet, &r_sceneFoliage, &r_sceneFoliageWrap, &r_sceneFoliageTrans, &r_sceneFoliageAO, &r_sceneAO, &r_sceneAORadius, &r_sceneShadowSoft, &r_sceneShadowFar, &r_sceneClouds, &r_sceneCloudScale, &r_sceneCloudEdge, &r_sceneCloudLight, &r_sceneCloudWind, &r_sceneEmissiveExposure, &r_sceneEmissiveRef, &r_sceneMoon, &r_sceneMoonSize, &r_sceneMoonGlow, &r_sceneMoonPath, &r_sceneMoonLight, &r_sceneClock, &r_sceneAutoExposure, &r_sceneExposureWindow, &r_sceneWindowExposure };
	char name[MAX_QPATH];
	char *buf = NULL;

	sceneEnvCount = 0;
	sceneEnvDay = qfalse;
	Q_strncpyz( sceneMap, bspName, size );
	if ( r_sceneEnv->string[0] ) {
		Q_strncpyz( name, r_sceneEnv->string, sizeof( name ) );
	} else {
		COM_StripExtension( bspName, name, sizeof( name ) );
		Q_strcat( name, sizeof( name ), ".xenv" );
	}
	const int len = ri.FS_ReadFile( name, (void **)&buf );
	if ( len <= 0 || !buf )
		return;
	std::string text( buf, len );
	ri.FS_FreeFile( buf );

	int version = 0, lineNum = 0, bad = 0;
	size_t at = 0;
	while ( at < text.size() ) {
		size_t end = text.find( '\n', at );
		if ( end == std::string::npos )
			end = text.size();
		std::string line = text.substr( at, end - at );
		at = end + 1;
		lineNum++;
		const size_t hash = line.find( '#' );
		if ( hash != std::string::npos )
			line.resize( hash );
		while ( !line.empty() && ( line.back() == '\r' || line.back() == ' ' || line.back() == '\t' ) )
			line.pop_back();
		size_t k = line.find_first_not_of( " \t" );
		if ( k == std::string::npos )
			continue;
		line = line.substr( k );
		k = line.find_first_of( " \t" );
		const std::string key = line.substr( 0, k );
		const std::string value = k == std::string::npos ? std::string() : line.substr( line.find_first_not_of( " \t", k ) );
		if ( !version ) {
			if ( key != "xenv" || atoi( value.c_str() ) != 1 ) {
				ri.Printf( PRINT_WARNING, "WARNING: %s is not a version 1 scene environment: ignored\n", name );
				return;
			}
			version = 1;
			continue;
		}
		if ( key == "scene" ) {
			Com_sprintf( sceneMap, size, "maps/%s.bsp", value.c_str() );
		} else if ( key == "time" ) {
			sceneEnvDay = !Q_stricmp( value.c_str(), "day" ) ? qtrue : qfalse;
		} else if ( key == "sun" ) {
			vec3_t c;
			float intensity, yaw, elevation;
			if ( sscanf( value.c_str(), "%f %f %f %f %f %f", &c[0], &c[1], &c[2], &intensity, &yaw, &elevation ) == 6 ) {
				// (tr_shader.cpp's q3gl2_sun)
				VectorNormalize2( c, tr.sunLight );
				VectorScale( tr.sunLight, intensity, tr.sunLight );
				const float a = DEG2RAD( yaw ), b = DEG2RAD( elevation );
				VectorSet( tr.sunDirection, cosf( a ) * cosf( b ), sinf( a ) * cosf( b ), sinf( b ) );
			} else {
				ri.Printf( PRINT_WARNING, "WARNING: %s line %d: sun takes r g b intensity yaw elevation\n", name, lineNum );
				bad++;
			}
		} else {
			int i = 0;
			const int numLook = (int)ARRAY_LEN( look );
			while ( i < numLook && Q_stricmp( ( *look[i] )->name, key.c_str() ) )
				i++;
			if ( i == numLook || sceneEnvCount == SCENE_ENV_VARS ) {
				if ( i == numLook )
					ri.Printf( PRINT_WARNING, "WARNING: %s line %d: %s is not a look value a scene environment sets\n", name, lineNum, key.c_str() );
				else
					ri.Printf( PRINT_WARNING, "WARNING: %s line %d: %s left out: an environment holds %d look values at most\n", name, lineNum, key.c_str(), SCENE_ENV_VARS );
				bad++;
				continue;
			}
			sceneEnvVar_t *e = &sceneEnvVars[sceneEnvCount++];
			e->cv = *look[i];
			e->count = e->cv->modificationCount;
			Q_strncpyz( e->str, value.c_str(), sizeof( e->str ) );
			Com_Memcpy( &e->shadow, e->cv, sizeof( e->shadow ) );
			e->shadow.string = e->str;
			e->shadow.value = (float)atof( e->str );
			e->shadow.integer = atoi( e->str );
		}
	}
	const qboolean other = Q_stricmp( sceneMap, bspName ) ? qtrue : qfalse;
	ri.Printf( PRINT_ALL, "scene environment %s: %s, %d look values%s%s%s\n", name, sceneEnvDay ? "day" : "night", sceneEnvCount,
		other ? ", the scene of " : "", other ? sceneMap : "", bad ? " (lines ignored: see the warnings)" : "" );
}

/*
r_sceneMoon: CP2077's moon. Its path (r_sceneMoonPath: latitude, declination, its transit on the game's clock): CP2077
does not set its moon opposite its sun; ref09's disc at 00:57 (elevation 49.4, azimuth 206) lies on the celestial
equator 1.1 h past its transit, at LightDirectionSettings' latitude 35: "35 -2.5 23.85", one setting for every hour
(ref14 and ref15 at 00:11-00:12 then have it above their frames, as CP2077 shows). Its colour: AtmosphereAreaSettings'
moonColor, linear between its points round the clock (alpha 2 from 20:00 to 03:57, 0.005 from 07:00 to 18:00). The
clock: r_sceneClock, -1 the scene's own (01:00 at night, 13:00 by day).
*/
static float Scene_Clock( void )
{
	const float c = Env( r_sceneClock )->value;
	return c >= 0.0f ? fmodf( c, 24.0f ) : sceneEnvDay ? 13.0f : 1.0f;
}

static void Scene_MoonAt( const float clock, vec3_t dir, vec4_t col )
{
	float lat = 35.0f, dec = -2.5f, transit = 23.85f;
	sscanf( Env( r_sceneMoonPath )->string, "%f %f %f", &lat, &dec, &transit );
	// its hour angle (west of the meridian); on the sky's sphere x toward the equator's meridian point, y west, z the
	// pole; the world's x east, y north (the sun's: q3gl2_sun's yaw is 90 - its azimuth)
	const float H = DEG2RAD( 15.0f * ( fmodf( clock - transit + 36.0f, 24.0f ) - 12.0f ) ), d = DEG2RAD( dec ), p = DEG2RAD( lat );
	const float xs = cosf( d ) * cosf( H ), yw = cosf( d ) * sinf( H ), z = sinf( d );
	VectorSet( dir, -yw, -xs * sinf( p ) + z * cosf( p ), xs * cosf( p ) + z * sinf( p ) );
	static const float curve[5][5] = {	// hour, rgb, alpha
		{ 3.953f, 0.695f, 0.727f, 0.785f, 2.0f }, { 7.0f, 0.785f, 0.785f, 0.785f, 0.005f }, { 18.0f, 0.785f, 0.785f, 0.785f, 0.005f },
		{ 20.0f, 0.695f, 0.727f, 0.785f, 2.0f }, { 27.953f, 0.695f, 0.727f, 0.785f, 2.0f } };
	const float t = clock < curve[0][0] ? clock + 24.0f : clock;
	int i = 0;
	while ( i < 3 && t > curve[i + 1][0] )
		i++;
	const float f = Com_Clamp( 0.0f, 1.0f, ( t - curve[i][0] ) / ( curve[i + 1][0] - curve[i][0] ) );
	for ( int k = 0; k < 4; k++ )
		col[k] = curve[i][k + 1] + ( curve[i + 1][k + 1] - curve[i][k + 1] ) * f;
}

// r_sceneLightTubes: whether a light of this type keeps its shape (a version 6 scene's): 1 every light, 2 the haze-only
// lights (type bit 1: no surface reads them), 0 none
static uint32_t sceneSurfaceTubes;	// the lights a surface reads that keep a shape (the frame's fx2.y)

// R_SceneFree: the per-map CPU state the loaders fill (image pointers, counts, the environment's look values), so that a
// map without a scene, r_scene 0 or a failed load keeps nothing of the last one
static void Scene_FreeCpuState( void )
{
	sceneTexImages.clear();
	sceneGroupBox.clear();
	sceneInteriorBox.clear();
	sceneCharLights.clear();
	sceneSlotImages.clear();
	sceneSlotMats.clear();
	sceneCharBoxes.clear();
	sceneCharShapes.clear();
	sceneCharCells.clear();
	sceneCharRefs.clear();
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ )
		sceneTexViews[f].clear();
	sceneDepthOrder.clear();
	Scene_FreeSideFilesCpu();	// the projected decals' and the particles' records (vk_scene_files.cpp)
	sceneCmdKeep.clear();
	sceneCmdMat.clear();
	sceneSkipApplied = 0;
	sceneLodLead.clear();
	Com_Memset( &sceneRain, 0, sizeof( sceneRain ) );
	sceneFileVertices = 0;
	sceneSurfaceTubes = 0;
	sceneOceanInst = 0xffffffffu;
	sceneOceanCount = 0;
	sceneOceanWhole = qfalse;
	sceneEnvCount = 0;
	sceneEnvDay = qfalse;
}

static qboolean Scene_TubeShaped( const float type )
{
	const int mode = r_sceneLightTubes->integer;
	return ( mode == 1 || ( mode == 2 && ( (int)type & 2 ) ) ) ? qtrue : qfalse;
}

void R_SceneLoad( const char *mapBsp )
{
	char name[MAX_QPATH];
	char bspName[MAX_QPATH];	// the map whose scene is loaded (its environment's `scene`, else its own)
	byte *buf = NULL;
	const double t0 = R_SunProfNow();

	R_SceneFree();
	if ( !r_scene || !r_scene->integer || !vk.device ) {
		if ( vk.device )
			vk_set_scene_range( qfalse );
		return;
	}
	Scene_LoadEnv( mapBsp, bspName, sizeof( bspName ) );
	// r_sceneMoon's image (CP2077's moon: a pack supplies it); none: no moon
	if ( r_sceneMoonImage->string[0] ) {
		sceneMoonImage = R_FindImageFile( r_sceneMoonImage->string, IMGFLAG_MIPMAP | IMGFLAG_NOLIGHTSCALE | IMGFLAG_CLAMPTOEDGE, 0 );
		ri.Printf( PRINT_ALL, "scene moon: %s%s\n", r_sceneMoonImage->string, sceneMoonImage ? "" : " not found (no moon)" );
	}

	// a scene with a colour grade gets a float colour target (r_sceneHdr), set before its pipelines are made: the
	// formats are part of their render pass
	// a tiled scene (maps/<map>.xman) before a single .xscn: a build writes one of the two, and an older single scene
	// still in an installed pack must not hide a newer tiled one
	char manName[MAX_QPATH];
	COM_StripExtension( bspName, manName, sizeof( manName ) );
	Q_strcat( manName, sizeof( manName ), ".xman" );
	COM_StripExtension( bspName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xscn" );
	const qboolean tiled = ri.FS_ReadFile( manName, NULL ) > 0 ? qtrue : qfalse;
	qboolean range = r_sceneHdr->integer && ( tiled || ri.FS_ReadFile( name, NULL ) > 0 ) ? qtrue : qfalse;
	COM_StripExtension( bspName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xlut" );
	vk_set_scene_range( range && ri.FS_ReadFile( name, NULL ) > 0 ? qtrue : qfalse );

	COM_StripExtension( bspName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xscn" );	// (a tiled scene's too: its .xfog, .xdet, .signals are named by it)
	uint64_t tiledLen = 0;
	// geometry streaming: r_sceneStream, the engine's range reads, the GPU-driven device (the cull's LOD residency)
	const qboolean wantStream = ( r_sceneStream->integer && ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) >= 2 && ri.FS_RangeOpen
		&& ri.FS_RangeRead && ri.FS_RangeReadAsync && ri.FS_RangeState && ri.FS_RangeCancel && ri.FS_RangeClose && vk.gpuDriven ) ? qtrue : qfalse;
	sceneOceanInst = 0xffffffffu;
	sceneOceanCount = 0;
	sceneOceanWhole = qfalse;
	if ( tiled )
		buf = Scene_ReadTiled( manName, &tiledLen, wantStream );
	const uint64_t len = tiled ? tiledLen : (uint64_t)MAX( ri.FS_ReadFile( name, (void **)&buf ), 0 );
	if ( !len || !buf )
		return;
	// the scene's buffer goes as it came: the filesystem's, or the joined one
	auto freeScene = [tiled]( byte *b ) { if ( tiled ) delete[] b; else ri.FS_FreeFile( b ); };
	// a scene refused before anything is made on the device: the buffer freed and what the read set up undone (a tiled
	// scene's streaming state and its shards' handles, the interiors, the float colour target)
	auto failLoad = [&freeScene]( byte *b ) { freeScene( b ); R_SceneFree(); vk_set_scene_range( qfalse ); };

	xscnHeader_t header;
	const xscnHeader_t *h = &header;
	qboolean valid = Scene_ReadHeader( buf, len, &header );
	if ( !valid ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s is not a version 1..%d render scene\n", name, SCENE_VERSION );
		failLoad( buf );
		return;
	}
	if ( !vk.drawIndirectFirstInstance ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s needs drawIndirectFirstInstance, which this device lacks: not drawn\n", name );
		failLoad( buf );
		return;
	}

	const xscnMesh_t *meshes = (const xscnMesh_t *)( buf + h->ofs[XS_MESHES] );
	const xscnLod_t *lods = (const xscnLod_t *)( buf + h->ofs[XS_LODS] );
	const xscnPart_t *parts = (const xscnPart_t *)( buf + h->ofs[XS_PARTS] );
	const xscnMaterial_t *mats = (const xscnMaterial_t *)( buf + h->ofs[XS_MATERIALS] );
	const xscnInstance_t *inst = (const xscnInstance_t *)( buf + h->ofs[XS_INSTANCES] );
	xscnLight_t *lights = (xscnLight_t *)( buf + h->ofs[XS_LIGHTS] );
	if ( h->version < 3 )
		for ( uint32_t i = 0; i < h->count[XS_LIGHTS]; i++ )
			lights[i].fog = 1.0f;
	{	// the scene's time (maps/<map>.xenv): in a day scene the night-only lights (type bit 20) at
		// r_sceneNightOnlyByDay of their power, at night the day-only ones (bit 21) at r_sceneDayOnlyAtNight; 0 drops
		// them, their boxes with them; before anything reads the lights
		const int out = sceneEnvDay ? 1 << 20 : 1 << 21;
		const float scale = MAX( Env( sceneEnvDay ? r_sceneNightOnlyByDay : r_sceneDayOnlyAtNight )->value, 0.0f );
		const uint32_t numIn = h->count[XS_LIGHTS];
		byte *boxes = h->count[XS_BOXES] == numIn ? buf + h->ofs[XS_BOXES] : NULL;
		byte *shapes = h->count[XS_SHAPES] == numIn ? buf + h->ofs[XS_SHAPES] : NULL;
		const float tubeScale = MAX( r_sceneLightTubeScale->value, 0.0f );	// (r_sceneLightTubeScale: the shaped lights' level)
		uint32_t tubeScaled = 0;
		// and the class switches: a placement's template and instance lights (bits 22 and 23: r_sceneLightPairs
		// 1 drops the template's, 2 the instance's), the non-world channel lights (bit 19) and every spot, scaled
		const int pairs = r_sceneLightPairs->integer, pairOut = pairs == 1 ? 1 << 22 : pairs == 2 ? 1 << 23 : 0;
		const float nwScale = MAX( r_sceneNonWorldLights->value, 0.0f ), spotScale = MAX( r_sceneSpotScale->value, 0.0f );
		uint32_t kept = 0, scaled = 0, paired = 0, nws = 0, spots = 0, dups = 0;
		// r_sceneLightDedup (game units): a light whose record past its origin up to its type equals another's and whose
		// origin is within this on every axis is dropped (one entity placed twice at one point: Misty's wall lamps, 0.375 u
		// apart); sorted by those bytes then x, each light looks back while x is in reach, the first of a pair kept
		std::vector<uint8_t> dup;
		const float dedup = r_sceneLightDedup->value;
		if ( dedup > 0.0f && numIn > 1 ) {
			// (the record past its origin, its box's interior rows (not its hides: one lamp's two sectors' copies may
			// differ there) and its shape)
			const size_t k0 = offsetof( xscnLight_t, radius ), k1 = sizeof( xscnLight_t );
			auto key = [&]( uint32_t a, uint32_t b ) {
				int c = memcmp( (const byte *)&lights[a] + k0, (const byte *)&lights[b] + k0, k1 - k0 );
				if ( !c && boxes )
					c = memcmp( boxes + (size_t)a * xsRecordSize[XS_BOXES], boxes + (size_t)b * xsRecordSize[XS_BOXES], 48 );
				if ( !c && shapes )
					c = memcmp( shapes + (size_t)a * sizeof( xscnShape_t ), shapes + (size_t)b * sizeof( xscnShape_t ), sizeof( xscnShape_t ) );
				return c;
			};
			std::vector<uint32_t> order( numIn );
			for ( uint32_t i = 0; i < numIn; i++ )
				order[i] = i;
			// (x as the sort takes it: a NaN after every number, so the order stays strict for a corrupt record)
			auto sortX = [&]( uint32_t i ) { return Q_isnan( lights[i].origin[0] ) ? INFINITY : lights[i].origin[0]; };
			std::sort( order.begin(), order.end(), [&]( uint32_t a, uint32_t b ) {
				const int c = key( a, b );
				if ( c )
					return c < 0;
				const float xa = sortX( a ), xb = sortX( b );
				if ( xa != xb )
					return xa < xb;
				return a < b;
			} );
			dup.assign( numIn, 0 );
			for ( uint32_t k = 1; k < numIn; k++ ) {
				const xscnLight_t &L = lights[order[k]];
				for ( uint32_t j = k; j-- > 0; ) {
					const xscnLight_t &M = lights[order[j]];
					if ( key( order[j], order[k] ) || L.origin[0] - M.origin[0] > dedup )
						break;
					if ( !dup[order[j]] && fabsf( L.origin[1] - M.origin[1] ) <= dedup && fabsf( L.origin[2] - M.origin[2] ) <= dedup ) {
						dup[order[k]] = 1;
						break;
					}
				}
			}
		}
		for ( uint32_t i = 0; i < numIn; i++ ) {
			if ( !dup.empty() && dup[i] ) {
				dups++;
				continue;
			}
			if ( (int)lights[i].type & out ) {
				if ( scale <= 0.0f )
					continue;
				lights[i].power *= scale;
				scaled++;
			}
			if ( pairOut && ( (int)lights[i].type & pairOut ) ) {
				paired++;
				continue;
			}
			if ( nwScale != 1.0f && ( (int)lights[i].type & ( 1 << 19 ) ) ) {
				nws++;
				if ( nwScale <= 0.0f )
					continue;
				lights[i].power *= nwScale;
			}
			if ( shapes ) {
				const float *a = (const float *)( shapes + (size_t)i * 16 );
				if ( a[0] != 0.0f || a[1] != 0.0f || a[2] != 0.0f ) {
					if ( tubeScale != 1.0f ) {
						lights[i].power *= tubeScale;
						tubeScaled++;
					}
					if ( !Scene_TubeShaped( lights[i].type ) )	// a point as before: its reach from the centre (the record's less |axis|)
						lights[i].radius = MAX( lights[i].radius - sqrtf( a[0] * a[0] + a[1] * a[1] + a[2] * a[2] ), 1.0f );
				}
			}
			if ( spotScale != 1.0f && ( (int)lights[i].type & 1 ) ) {
				if ( spotScale <= 0.0f )
					continue;
				lights[i].power *= spotScale;
				spots++;
			}
			if ( kept != i ) {
				lights[kept] = lights[i];
				if ( boxes )
					Com_Memcpy( boxes + (size_t)kept * 64, boxes + (size_t)i * 64, 64 );
				if ( shapes )
					Com_Memcpy( shapes + (size_t)kept * 16, shapes + (size_t)i * 16, 16 );
			}
			kept++;
		}
		if ( kept != numIn ) {
			header.count[XS_LIGHTS] = kept;
			if ( boxes )
				header.count[XS_BOXES] = kept;
			if ( shapes )
				header.count[XS_SHAPES] = kept;
		}
		if ( kept != numIn || ( scaled && scale != 1.0f ) )
			ri.Printf( PRINT_ALL, "scene time %s: %u of %u lights kept, %u of them out of their time at %.3g of their power\n",
				sceneEnvDay ? "day" : "night", kept, numIn, scaled, scale );
		if ( pairOut || nwScale != 1.0f || spotScale != 1.0f )
			ri.Printf( PRINT_ALL, "scene light classes: %u %s lights of a pair dropped (r_sceneLightPairs %d), %u non-world lights at %.3g "
				"(r_sceneNonWorldLights), %u spots at %.3g (r_sceneSpotScale)\n", paired, pairs == 1 ? "template" : "instance", pairs,
				nws, nwScale, spots, spotScale );
		if ( !dup.empty() )
			ri.Printf( PRINT_ALL, "scene lights: %u duplicates dropped (r_sceneLightDedup)\n", dups );
		if ( tubeScaled )
			ri.Printf( PRINT_ALL, "scene lights: %u shaped lights at %.3g of their power (r_sceneLightTubeScale)\n", tubeScaled, tubeScale );
	}
	const uint32_t numMeshes = h->count[XS_MESHES], numLods = h->count[XS_LODS], numParts = h->count[XS_PARTS];
	const uint32_t numMats = h->count[XS_MATERIALS], numInst = h->count[XS_INSTANCES];
	// the scene's indices and vertices (streaming: in its geometry files, not the buffer)
	const uint64_t numIdxAll = ss.on ? (uint64_t)ss.shards.back().iBase + ss.shards.back().iCount : h->count[XS_INDICES];

	// references in range, else nothing is drawn (spans summed in 64 bits: a first and a count may not wrap past the end)
	for ( uint32_t i = 0; valid && i < numMeshes; i++ )
		if ( !meshes[i].numLods || (uint64_t)meshes[i].firstLod + meshes[i].numLods > numLods )
			valid = qfalse;
	for ( uint32_t i = 0; valid && i < numLods; i++ )
		if ( (uint64_t)lods[i].firstPart + lods[i].numParts > numParts )
			valid = qfalse;
	{	// a part in one LOD at most: a LOD's parts share its visible range
		std::vector<byte> used( numParts, 0 );
		for ( uint32_t i = 0; valid && i < numLods; i++ )
			for ( uint32_t p = lods[i].firstPart; valid && p < lods[i].firstPart + lods[i].numParts; p++ )
				valid = used[p]++ ? qfalse : qtrue;
	}
	for ( uint32_t i = 0; valid && i < numParts; i++ )
		if ( parts[i].material >= numMats || (uint64_t)parts[i].firstIndex + parts[i].numIndices > numIdxAll )
			valid = qfalse;
	if ( valid && !ss.on ) {	// every index a part draws, with its vertex offset, inside the vertices (streaming: each block as it arrives)
		const uint32_t *idx = (const uint32_t *)( buf + h->ofs[XS_INDICES] );
		const int64_t numVerts = h->count[XS_VERTICES];
		for ( uint32_t i = 0; valid && i < numParts; i++ ) {
			uint32_t lo = 0xffffffffu, hi = 0;
			for ( uint32_t k = parts[i].firstIndex; k < parts[i].firstIndex + parts[i].numIndices; k++ ) {
				lo = MIN( lo, idx[k] );
				hi = MAX( hi, idx[k] );
			}
			if ( parts[i].numIndices && ( (int64_t)parts[i].vertexOffset + lo < 0 || (int64_t)parts[i].vertexOffset + hi >= numVerts ) )
				valid = qfalse;
		}
	}
	for ( uint32_t i = 0; valid && i < numInst; i++ )
		if ( inst[i].mesh >= numMeshes )
			valid = qfalse;
	if ( valid ) {	// a near or hide distance that is not a finite number of at least 0 means none (scene_cull.comp and
		// scene.vert test the bits, the bounds the sign; a NaN reaches clamp())
		uint32_t bad = 0;
		for ( uint32_t i = 0; i < numInst; i++ ) {
			xscnInstance_t *in = (xscnInstance_t *)&inst[i];
			if ( !( in->nearDist >= 0.0f && in->nearDist < 1e30f ) || ( in->nearDist == 0.0f && signbit( in->nearDist ) ) ) {
				in->nearDist = 0.0f;
				bad++;
			}
			if ( !( in->hide >= 0.0f && in->hide < 1e30f ) || ( in->hide == 0.0f && signbit( in->hide ) ) ) {
				in->hide = 0.0f;
				bad++;
			}
		}
		if ( bad )
			ri.Printf( PRINT_WARNING, "WARNING: scene %s: %u near or hide distances negative or not finite, read as none\n", name, bad );
	}
	if ( valid ) {	// interiors (maps/<map>.xint v1): the groups' boxes; an instance naming no group of them is not an interior's
		char intName[MAX_QPATH];
		COM_StripExtension( name, intName, sizeof( intName ) );
		Q_strcat( intName, sizeof( intName ), ".xint" );
		void *ib = NULL;
		const int ilen = ri.FS_ReadFile( intName, &ib );
		uint32_t count = 0;
		if ( ilen >= 16 && !memcmp( ib, "XINT", 4 ) && ( (const uint32_t *)ib )[1] == 1 ) {
			count = ( (const uint32_t *)ib )[2];
			if ( (uint64_t)ilen < 16 + (uint64_t)count * 32 || count > 0xffffffu ) {
				ri.Printf( PRINT_WARNING, "WARNING: %s: %u groups do not fit its %d bytes: ignored\n", intName, count, ilen );
				count = 0;
			}
		} else if ( ilen > 0 ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s is not a version 1 interior file: ignored\n", intName );
		}
		sceneInteriorBox.assign( (size_t)count * 6, 0.0f );
		for ( uint32_t g = 0; g < count; g++ )
			Com_Memcpy( &sceneInteriorBox[g * 6], (const byte *)ib + 16 + g * 32, 24 );
		if ( ib )
			ri.FS_FreeFile( ib );
		uint32_t interiors = 0, stray = 0;
		for ( uint32_t i = 0; i < numInst; i++ ) {
			xscnInstance_t *in = (xscnInstance_t *)&inst[i];
			if ( !( in->slot & 0x1000000u ) )
				continue;
			if ( ( in->slot & 0xffffffu ) < count ) {
				interiors++;
			} else {
				in->slot = 0;	// (no such group: drawn as an exterior)
				stray++;
			}
		}
		sc.numInteriors = count;
		if ( count || stray )
			ri.Printf( PRINT_ALL, "scene interiors %s: %u groups, %u instances drawn only from inside their boxes%s\n", intName, count, interiors,
				stray ? va( " (%u named no group: drawn as before)", stray ) : "" );
	}
	if ( valid ) {	// the placements whose normals scene.vert turns by inverse-transpose(M) (its test, counted for the record)
		uint32_t uneven = 0, mirrored = 0;
		for ( uint32_t i = 0; i < numInst; i++ ) {
			const float *m = inst[i].m;
			const vec3_t a = { m[0], m[4], m[8] }, b = { m[1], m[5], m[9] }, c = { m[2], m[6], m[10] };
			vec3_t bc;
			CrossProduct( b, c, bc );
			const float det = DotProduct( a, bc ), aa = DotProduct( a, a ), tol = 1e-3f * aa;
			if ( det < 0.0f )
				mirrored++;
			else if ( fabsf( DotProduct( b, b ) - aa ) > tol || fabsf( DotProduct( c, c ) - aa ) > tol || fabsf( DotProduct( a, b ) ) > tol
				|| fabsf( DotProduct( b, c ) ) > tol || fabsf( DotProduct( a, c ) ) > tol )
				uneven++;
		}
		if ( uneven || mirrored )
			ri.Printf( PRINT_ALL, "scene %s: %u instances scaled unevenly, %u mirrored (normals by inverse-transpose)\n", name, uneven, mirrored );
	}
	if ( !valid ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s has references out of range (or a part in two LODs): not drawn\n", name );
		failLoad( buf );
		return;
	}
	{	// the device's limits: the instances' buffer is bound whole (64 bytes each) and the instance-wide passes (cull,
		// wants, rotation) dispatch a workgroup of 64 per 64 instances along x
		VkPhysicalDeviceProperties props;
		qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
		if ( (uint64_t)numInst * xsRecordSize[XS_INSTANCES] > props.limits.maxStorageBufferRange
			|| ( (uint64_t)numInst + 63u ) / 64u > props.limits.maxComputeWorkGroupCount[0] ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s: %u instances pass this device's limits (a %u-byte storage range, %u workgroups): not drawn\n",
				name, numInst, props.limits.maxStorageBufferRange, props.limits.maxComputeWorkGroupCount[0] );
			failLoad( buf );
			return;
		}
	}
	if ( numInst > 0x1000000u ) {	// the visible lists keep an instance's index in 24 bits (its top byte r_sceneLodFade's
		// tag; scene_cull.comp, scene.vert, scene_sort.comp): past them the draws would take other instances
		ri.Printf( PRINT_WARNING, "WARNING: %s: %u instances pass the visible lists' 24-bit index (16,777,216): not drawn\n", name, numInst );
		failLoad( buf );
		return;
	}

	Q_strncpyz( sc.name, name, sizeof( sc.name ) );
	sc.numInstances = numInst;
	sc.numMeshes = numMeshes;
	sc.numMaterials = numMats;
	sc.numLights = h->count[XS_LIGHTS];
	sc.numTriangles = numIdxAll / 3;

	// materials and their images (streamed when the geometry is: its wants say which levels, R_StreamWantImage)
	R_StreamSceneImages( ss.on );
	sc.materials = (sceneMaterial_t *)Z_Malloc( sizeof( sceneMaterial_t ) * MAX( numMats, 1u ), TAG_GENERAL, qtrue );
	int missingImages = 0;
	uint32_t numStandIns = 0;
	for ( uint32_t i = 0; i < numMats; i++ ) {
		sceneMaterial_t *m = &sc.materials[i];
		m->kind = MIN( mats[i].kind, (uint32_t)SCENE_KIND_EMISSIVE );
		m->srcFlags = mats[i].flags;
		for ( int k = 0; k < 3; k++ ) {
			qboolean found;
			m->image[k] = Scene_Image( mats[i].image[k], &found );
			if ( found )
				m->imageBits |= 1u << k;
			else if ( mats[i].image[k][0] )
				missingImages++;
		}
		for ( int k = 0; k < 3; k++ )
			m->emissive[k] = powf( MAX( mats[i].emissive[k], 0.0f ), 2.2f ) * mats[i].emissive[3];
		m->emissive[3] = 1.0f;
		if ( sceneEnvDay && ( mats[i].flags & 0x100u ) )	// a night-only emissive in a day scene (maps/<map>.xenv)
			VectorScale( m->emissive, MAX( Env( r_sceneDayLamps )->value, 0.0f ), m->emissive );
		if ( mats[i].flags & 0x200u )	// a screen (ads, UI, televisions): r_sceneScreens
			VectorScale( m->emissive, MAX( r_sceneScreens->value, 0.0f ), m->emissive );
		Com_Memcpy( m->tint, mats[i].tint, sizeof( m->tint ) );
		if ( m->kind == SCENE_KIND_EMISSIVE )
			sc.numEmissive++;
		if ( ( mats[i].flags & 2u ) && m->kind == SCENE_KIND_EMISSIVE ) {
			const uint32_t tiles = (uint32_t)Com_Clamp( 0.0f, 255.0f, mats[i].window[1] );
			const uint32_t depth = (uint32_t)Com_Clamp( 1.0f, 1023.0f, mats[i].window[0] * 16.0f );
			m->window = 2u | ( tiles << 4 ) | ( depth << 12 );
			if ( sceneEnvDay )	// AmountTurnOffAtNight and TintColorAtNight are the night's: by day every room lit, untinted
				m->tint[2] = m->tint[3] = 0.0f;
		} else if ( ( mats[i].flags & 0x400u ) && ( m->kind == SCENE_KIND_OPAQUE || m->kind == SCENE_KIND_CUTOUT ) ) {
			m->window = 16u;	// an ad's glow: in the albedo's own colours where the normal image's alpha says (scene.frag)
			sc.numAdGlows++;
			if ( !( m->imageBits & 2u ) )
				sc.numAdGlowsUnmasked++;
		} else if ( ( mats[i].flags & 0x800u ) && ( m->kind == SCENE_KIND_OPAQUE || m->kind == SCENE_KIND_CUTOUT ) ) {
			m->window = 32u;	// a SpeedTree leaf: shaded as its canopy with r_sceneFoliage (scene.frag)
			sc.numLeaves++;
		} else if ( ( mats[i].flags & 0x1000u ) && m->kind == SCENE_KIND_OPAQUE ) {
			sc.numTerrain++;
			if ( r_sceneTerrainPush->integer )
				m->window = 128u;	// terrain: pushed back along the eye's ray (scene.vert)
		} else if ( ( mats[i].flags & 4u ) && m->kind != SCENE_KIND_EMISSIVE ) {
			m->window = 4u;		// a masked glow: the albedo's alpha says where the lit surface glows
			if ( m->kind == SCENE_KIND_BLEND )
				sc.numHolograms++;	// (a hologram: blooms with r_sceneHoloBloom)
		} else if ( ( mats[i].flags & 8u ) && m->kind == SCENE_KIND_EMISSIVE ) {
			m->window = 8u | ( mats[i].flags & 0x30u );	// a signal's lamp (scene.frag: lit by its signal's state)
		}
		if ( ( mats[i].flags & 0x200u ) && !m->window && ( m->kind == SCENE_KIND_EMISSIVE || m->kind == SCENE_KIND_BLEND ) )
			m->window = 1u;		// a screen (scene.frag: r_sceneScreenLook)
		// a CP2077 mesh decal (flag 0x40): emissive holds its DepthThreshold (metres to units; none given: 0.5 m) and
		// NormalAlpha for the decal pass; tint.a is its DiffuseAlpha
		if ( m->kind == SCENE_KIND_BLEND && ( mats[i].flags & 0x40u ) && r_sceneDecals->integer ) {
			m->kind = SCENE_KIND_DECAL;
			m->emissive[0] = ( mats[i].window[0] > 0.0f ? mats[i].window[0] : 0.5f ) * 37.5f;
			m->emissive[1] = Com_Clamp( 0.0f, 1.0f, mats[i].window[1] );
			m->emissive[2] = m->emissive[3] = 0.0f;
			m->window = 0;
			sc.numDecals++;
		}
		// water (flag 0x80): tint holds its colour in the water (sRGB), the normal image its ripples; the defaults
		// here, a record in maps/<map>.xwat over them (Scene_LoadWater). emissive: visibility and shore (m), the
		// ripples' tile (m) and speed (m/s); its detail parameters (scene.frag's md): heading (radians), roughness
		// (0: its ORM's), normal strength, foam band (m); tint.a the foam's intensity
		if ( m->kind == SCENE_KIND_BLEND && ( mats[i].flags & 0x80u ) && r_sceneWater->integer ) {
			m->kind = SCENE_KIND_WATER;
			m->emissive[0] = 1.5f;
			m->emissive[1] = 0.25f;
			m->emissive[2] = 4.0f;
			m->emissive[3] = 0.3f;
			m->tint[3] = 1.0f;
			m->window = 0;
			sc.numWater++;
		}
		if ( mats[i].flags & 0x2000u ) {	// a stand-in the scene draws otherwise (SCENE_KIND_NONE)
			m->kind = SCENE_KIND_NONE;
			numStandIns++;
		}
	}
	if ( numStandIns )
		ri.Printf( PRINT_ALL, "scene: %u stand-in materials not drawn (flag 0x2000)\n", numStandIns );

	// the water's band: the lowest and highest z of its surfaces (each water mesh's box of its water parts' vertices, as
	// each instance places it), 16 units over and under; none: an empty band
	sc.waterLo = 1e30f;
	sc.waterHi = -1e30f;
	if ( sc.numWater ) {
		const uint32_t *idx = (const uint32_t *)( buf + h->ofs[XS_INDICES] );
		const float *verts = (const float *)( buf + h->ofs[XS_VERTICES] );
		std::vector<float> box( (size_t)numMeshes * 6, 0.0f );
		std::vector<byte> wet( numMeshes, 0 );
		std::vector<uint32_t> partIdx;	// streaming: the part's indices, and its vertices from vFirst
		std::vector<float> partVerts;
		int64_t vFirst = 0;
		for ( uint32_t i = 0; i < numMeshes; i++ ) {
			float *b = &box[(size_t)i * 6];
			for ( uint32_t l = meshes[i].firstLod; l < meshes[i].firstLod + meshes[i].numLods; l++ )
				for ( uint32_t p = lods[l].firstPart; p < lods[l].firstPart + lods[l].numParts; p++ ) {
					if ( sc.materials[parts[p].material].kind != SCENE_KIND_WATER )
						continue;
					if ( ss.on && !Scene_StreamPartNow( &parts[p], partIdx, partVerts, &vFirst ) )
						continue;
					for ( uint32_t k = 0; k < parts[p].numIndices; k++ ) {
						const float *v = ss.on ? &partVerts[(size_t)( ( (int64_t)partIdx[k] + parts[p].vertexOffset - vFirst ) * 7 )]
							: verts + ( (int64_t)idx[parts[p].firstIndex + k] + parts[p].vertexOffset ) * 7;
						for ( int a = 0; a < 3; a++ ) {
							b[a] = wet[i] ? MIN( b[a], v[a] ) : v[a];
							b[3 + a] = wet[i] ? MAX( b[3 + a], v[a] ) : v[a];
						}
						wet[i] = 1;
					}
				}
		}
		for ( uint32_t i = 0; i < numInst; i++ ) {
			if ( !wet[inst[i].mesh] )
				continue;
			const float *b = &box[(size_t)inst[i].mesh * 6], *m = inst[i].m;
			for ( int c = 0; c < 8; c++ ) {
				const float z = m[8] * b[( c & 1 ) ? 3 : 0] + m[9] * b[( c & 2 ) ? 4 : 1] + m[10] * b[( c & 4 ) ? 5 : 2] + m[11];
				sc.waterLo = MIN( sc.waterLo, z - 16.0f );
				sc.waterHi = MAX( sc.waterHi, z + 16.0f );
			}
		}
	}

	// draw commands: one per part, sorted by material. A LOD's parts draw the same instances, so they share one range
	// of the slot's visible pool (scene_cull.comp counts on its first part's command and raises the others' counts to it;
	// scene_scatter.comp allocates the ranges each frame)
	std::vector<uint32_t> partLod( numParts, ~0u ), order( numParts ), partCmd( numParts );
	for ( uint32_t l = 0; l < numLods; l++ )
		for ( uint32_t p = lods[l].firstPart; p < lods[l].firstPart + lods[l].numParts; p++ )
			partLod[p] = l;
	for ( uint32_t p = 0; p < numParts; p++ )
		order[p] = p;
	std::stable_sort( order.begin(), order.end(), [&]( uint32_t a, uint32_t b ) { return parts[a].material < parts[b].material; } );
	std::vector<VkDrawIndexedIndirectCommand> cmds( MAX( numParts, 1u ) );
	for ( uint32_t c = 0; c < numParts; c++ ) {
		const xscnPart_t *p = &parts[order[c]];
		cmds[c].indexCount = p->numIndices;
		cmds[c].instanceCount = 0;
		cmds[c].firstIndex = p->firstIndex;
		cmds[c].vertexOffset = p->vertexOffset;
		cmds[c].firstInstance = 0;	// its LOD's range in the slot's pool, allocated each frame (scene_scatter.comp)
		partCmd[order[c]] = c;
		sceneMaterial_t *m = &sc.materials[p->material];
		if ( !m->numCmds )
			m->firstCmd = c;
		m->numCmds++;
	}
	sc.numCmds = numParts;
	sceneCmdKeep.assign( cmds.begin(), cmds.begin() + numParts );	// r_sceneSkip's source
	sceneCmdMat.resize( numParts );
	for ( uint32_t c = 0; c < numParts; c++ )
		sceneCmdMat[c] = parts[order[c]].material;
	sceneSkipApplied = 0;
	sceneLodLead.assign( numLods, ~0u );
	for ( uint32_t l = 0; l < numLods; l++ )
		if ( lods[l].numParts )
			sceneLodLead[l] = partCmd[lods[l].firstPart];
	if ( ss.on ) {
		if ( !Scene_StreamPlan( parts, numParts, (xscnLod_t *)lods, numLods, partCmd ) ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s: its geometry cannot be streamed: not drawn\n", name );
			freeScene( buf );
			R_SceneFree();
			return;
		}
	} else {
		for ( uint32_t l = 0; l < numLods; l++ )
			( (xscnLod_t *)lods )[l].pad = 1;	// every LOD resident (scene_cull.comp)
	}
	// the depth lists' order (GPU-driven draws): biggest mesh first, else material order (a part's mesh by its LODs)
	{
		std::vector<float> partRadius( numParts, 0.0f );
		for ( uint32_t i = 0; i < numMeshes; i++ )
			for ( uint32_t l = meshes[i].firstLod; l < meshes[i].firstLod + meshes[i].numLods; l++ )
				for ( uint32_t p = lods[l].firstPart; p < lods[l].firstPart + lods[l].numParts; p++ )
					partRadius[p] = meshes[i].radius;
		sceneDepthOrder.resize( numParts );
		for ( uint32_t c = 0; c < numParts; c++ )
			sceneDepthOrder[c] = c;
		std::stable_sort( sceneDepthOrder.begin(), sceneDepthOrder.end(), [&]( uint32_t a, uint32_t b ) {
			return partRadius[order[a]] > partRadius[order[b]]; } );
	}

	// instance bounds: the full meshes' for the light grid and the rain view (a far proxy lies past them, in no light's
	// reach), everything's for the views' far clip
	ClearBounds( sc.mins, sc.maxs );
	ClearBounds( sc.drawMins, sc.drawMaxs );
	sc.numGroups = ( numInst + SCENE_CULL_GROUP - 1 ) / SCENE_CULL_GROUP;
	sceneGroupBox.assign( (size_t)sc.numGroups * 6, 0.0f );
	for ( uint32_t g = 0; g < sc.numGroups; g++ )
		ClearBounds( &sceneGroupBox[g * 6], &sceneGroupBox[g * 6 + 3] );
	for ( uint32_t i = 0; i < numInst; i++ ) {
		if ( Scene_IsOcean( i ) )	// (their own boxes, below)
			continue;
		const float *M = inst[i].m;
		const xscnMesh_t *mesh = &meshes[inst[i].mesh];
		vec3_t c, lo, hi;
		float scale = 0.0f;
		for ( int r = 0; r < 3; r++ )
			c[r] = M[r * 4] * mesh->center[0] + M[r * 4 + 1] * mesh->center[1] + M[r * 4 + 2] * mesh->center[2] + M[r * 4 + 3];
		for ( int col = 0; col < 3; col++ )
			scale = MAX( scale, M[col] * M[col] + M[4 + col] * M[4 + col] + M[8 + col] * M[8 + col] );
		const float r = mesh->radius * sqrtf( scale );
		VectorSet( lo, c[0] - r, c[1] - r, c[2] - r );
		VectorSet( hi, c[0] + r, c[1] + r, c[2] + r );
		AddPointToBounds( lo, sc.drawMins, sc.drawMaxs );
		AddPointToBounds( hi, sc.drawMins, sc.drawMaxs );
		float *gb = &sceneGroupBox[( i / SCENE_CULL_GROUP ) * 6];
		AddPointToBounds( lo, gb, gb + 3 );
		AddPointToBounds( hi, gb, gb + 3 );
		if ( inst[i].nearDist <= 0.0f ) {
			AddPointToBounds( lo, sc.mins, sc.maxs );
			AddPointToBounds( hi, sc.mins, sc.maxs );
		}
	}
	for ( uint32_t i = sceneOceanInst; i < numInst && Scene_IsOcean( i ); i++ ) {	// the ocean's planes: each its flat box (the
		// patch spans +-5 mesh units), in the far clip's and its group's bounds, not the light grid's; a whole-map plane
		// over the rest's drawn extent
		xscnInstance_t *in = (xscnInstance_t *)&inst[i];
		if ( sceneOceanWhole && sc.drawMins[0] <= sc.drawMaxs[0] ) {
			in->m[0] = ( sc.drawMaxs[0] - sc.drawMins[0] ) / 10.0f;
			in->m[3] = ( sc.drawMins[0] + sc.drawMaxs[0] ) * 0.5f;
			in->m[5] = ( sc.drawMaxs[1] - sc.drawMins[1] ) / 10.0f;
			in->m[7] = ( sc.drawMins[1] + sc.drawMaxs[1] ) * 0.5f;
		}
		vec3_t lo, hi;
		VectorSet( lo, in->m[3] - in->m[0] * 5.0f, in->m[7] - in->m[5] * 5.0f, in->m[11] - 1.0f );
		VectorSet( hi, in->m[3] + in->m[0] * 5.0f, in->m[7] + in->m[5] * 5.0f, in->m[11] + 1.0f );
		AddPointToBounds( lo, sc.drawMins, sc.drawMaxs );
		AddPointToBounds( hi, sc.drawMins, sc.drawMaxs );
		float *gb = &sceneGroupBox[( i / SCENE_CULL_GROUP ) * 6];
		AddPointToBounds( lo, gb, gb + 3 );
		AddPointToBounds( hi, gb, gb + 3 );
	}
	{	// the GPU-listed cull groups (scene_groupcull.comp): SCENE_CULL_SUB instances' spheres' bounds and their largest
		// radius (an ocean's: never too small)
		sc.numSubs = ( numInst + SCENE_CULL_SUB - 1 ) / SCENE_CULL_SUB;
		std::vector<float> subs( (size_t)MAX( sc.numSubs, 1u ) * 8, 0.0f );
		for ( uint32_t g = 0; g < sc.numSubs; g++ ) {
			ClearBounds( &subs[g * 8], &subs[g * 8 + 4] );
			subs[g * 8 + 3] = 0.0f;
		}
		for ( uint32_t i = 0; i < numInst; i++ ) {
			float *gb = &subs[( i / SCENE_CULL_SUB ) * 8];
			const float *M = inst[i].m;
			vec3_t c, lo, hi;
			float r;
			if ( Scene_IsOcean( i ) ) {	// its own box, as above
				VectorSet( lo, M[3] - M[0] * 5.0f, M[7] - M[5] * 5.0f, M[11] - 1.0f );
				VectorSet( hi, M[3] + M[0] * 5.0f, M[7] + M[5] * 5.0f, M[11] + 1.0f );
				r = 1e30f;
			} else {
				const xscnMesh_t *mesh = &meshes[inst[i].mesh];
				float scale = 0.0f;
				for ( int k = 0; k < 3; k++ )
					c[k] = M[k * 4] * mesh->center[0] + M[k * 4 + 1] * mesh->center[1] + M[k * 4 + 2] * mesh->center[2] + M[k * 4 + 3];
				for ( int col = 0; col < 3; col++ )
					scale = MAX( scale, M[col] * M[col] + M[4 + col] * M[4 + col] + M[8 + col] * M[8 + col] );
				r = mesh->radius * sqrtf( scale );
				VectorSet( lo, c[0] - r, c[1] - r, c[2] - r );
				VectorSet( hi, c[0] + r, c[1] + r, c[2] + r );
			}
			AddPointToBounds( lo, gb, gb + 4 );
			AddPointToBounds( hi, gb, gb + 4 );
			gb[3] = MAX( gb[3], r );
		}
		if ( !Scene_StaticBuffer( &sc.cullSubs, subs.data(), (VkDeviceSize)subs.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene cull groups' boxes" ) ) {
			freeScene( buf );
			R_SceneFree();
			return;
		}
	}
	if ( sc.mins[0] > sc.maxs[0] ) {	// none (or only proxies)
		VectorCopy( sc.drawMins, sc.mins );
		VectorCopy( sc.drawMaxs, sc.maxs );
	}
	if ( !numInst ) {
		VectorClear( sc.mins );
		VectorClear( sc.maxs );
		VectorClear( sc.drawMins );
		VectorClear( sc.drawMaxs );
	}

	std::vector<uint32_t> cells, refs;
	Scene_BinLights( lights, sc.numLights, cells, refs );
	sc.numCellRefs = (uint32_t)refs.size();

	sceneFileVertices = ss.on ? 0u : h->count[XS_VERTICES];	// (maps/<map>.xw3b's stream follows this order)
	for ( uint32_t i = 0; i < numMeshes; i++ )	// word 6, the card-caster mark (scene_cull.comp's lods.z), none unless marked below
		( (xscnMesh_t *)meshes )[i].pad[0] = 0;
	if ( vk.gpuDriven )	// (the scene file's own buffer: the ocean's records are written into it above too)
		Scene_MarkCardMeshes( name, (xscnMesh_t *)meshes, numMeshes, lods, parts, numMats );
	const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	qboolean ok = ( ( ss.on || ( Scene_StaticBuffer( &sc.vertices, buf + h->ofs[XS_VERTICES], (VkDeviceSize)h->count[XS_VERTICES] * 28, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "scene vertices" )
		&& Scene_StaticBuffer( &sc.indices, buf + h->ofs[XS_INDICES], (VkDeviceSize)h->count[XS_INDICES] * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "scene indices" ) ) )
		&& Scene_StaticBuffer( &sc.instances, inst, (VkDeviceSize)numInst * 64, ssbo, "scene instances" )
		&& Scene_StaticBuffer( &sc.meshes, meshes, (VkDeviceSize)numMeshes * 32, ssbo, "scene meshes" )
		&& Scene_StaticBuffer( &sc.lods, lods, (VkDeviceSize)numLods * 16, ssbo, "scene lods" )
		&& Scene_StaticBuffer( &sc.partCmd, partCmd.data(), (VkDeviceSize)numParts * 4, ssbo, "scene part commands" )
		&& Scene_StaticBuffer( &sc.cmdTemplate, cmds.data(), (VkDeviceSize)cmds.size() * sizeof( VkDrawIndexedIndirectCommand ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "scene command template" )
		&& Scene_StaticBuffer( &sc.lights, lights, (VkDeviceSize)sc.numLights * 64, ssbo, "scene lights" )
		&& Scene_StaticBuffer( &sc.cells, cells.data(), (VkDeviceSize)cells.size() * 4, ssbo, "scene light cells" )
		&& Scene_StaticBuffer( &sc.cellLights, refs.data(), (VkDeviceSize)refs.size() * 4, ssbo, "scene cell lights" ) ) ? qtrue : qfalse;
	// (the stream's pools are made after the views: its want sets take their slots' interior bits)
	if ( ok ) {	// scene_cluster.comp's tile test: each light's sphere and kind, from its record (the same floats; the kind as
		// the shader read it from the record)
		std::vector<float> bounds( 4 * MAX( sc.numLights, 1u ), 0.0f );
		std::vector<uint32_t> kinds( MAX( sc.numLights, 1u ), 0 );
		for ( uint32_t i = 0; i < sc.numLights; i++ ) {
			VectorCopy( lights[i].origin, &bounds[i * 4] );
			bounds[i * 4 + 3] = lights[i].radius;
			kinds[i] = ( ( (int)lights[i].type & 2 ) == 0 ? 1u : 0u ) | ( lights[i].fog > 0.0f ? 2u : 0u );
		}
		ok = ( Scene_StaticBuffer( &sc.lightBounds, bounds.data(), (VkDeviceSize)bounds.size() * 4, ssbo, "scene light bounds" )
			&& Scene_StaticBuffer( &sc.lightKinds, kinds.data(), (VkDeviceSize)kinds.size() * 4, ssbo, "scene light kinds" ) ) ? qtrue : qfalse;
		std::vector<float> ones( 4 * kinds.size(), 1.0f );	// (fade, shadow strength, stand-in, 0)
		for ( size_t i = 0; i < kinds.size(); i++ )
			ones[i * 4 + 2] = ones[i * 4 + 3] = 0.0f;
		sc.numCells = (uint32_t)sc.gridDims[0] * (uint32_t)sc.gridDims[1] * (uint32_t)sc.gridDims[2];
		std::vector<uint32_t> noStandIns( (size_t)MAX( sc.numCells, 1u ) * SCENE_DIST_STRIDE, 0u );
		for ( int f = 0; ok && f < NUM_COMMAND_BUFFERS; f++ )
			ok = ( Scene_StaticBuffer( &sc.lightFade[f], ones.data(), (VkDeviceSize)ones.size() * 4, ssbo, "scene light fades" )
				&& Scene_StaticBuffer( &sc.liveKinds[f], kinds.data(), (VkDeviceSize)kinds.size() * 4, ssbo, "scene live light kinds" )
				&& Scene_StaticBuffer( &sc.distantCells[f], noStandIns.data(), (VkDeviceSize)noStandIns.size() * 4, ssbo, "scene distant lights" ) ) ? qtrue : qfalse;
		sc.hasLightBoxes = ( h->count[XS_BOXES] && h->version >= 4 ) ? qtrue : qfalse;	// version 4 gives the hides in the boxes
		uint32_t surf = 0, haze = 0;
		for ( uint32_t i = 0; i < sc.numLights; i++ ) {
			surf += kinds[i] & 1u;
			haze += ( kinds[i] >> 1 ) & 1u;
		}
		ri.Printf( PRINT_ALL, "scene lights: %u light surfaces, %u scatter in the haze (of %u)\n", surf, haze, sc.numLights );
	}
	// interior lights' boxes (version 2; a light without one never reads it: the light's flag is 0) and the lights'
	// shapes (version 6; none: zeros, a point): 80 bytes a light, the box's rows and hides, then the shape
	if ( ok ) {
		const void *boxes = buf + h->ofs[XS_BOXES];
		const xscnShape_t *shapes = ( r_sceneLightTubes->integer && h->count[XS_SHAPES] == sc.numLights ) ? (const xscnShape_t *)( buf + h->ofs[XS_SHAPES] ) : NULL;
		std::vector<xscnShape_t> kept;	// (r_sceneLightTubes 2: the haze-only lights' shapes alone)
		if ( shapes ) {
			kept.assign( shapes, shapes + sc.numLights );
			for ( uint32_t i = 0; i < sc.numLights; i++ )
				if ( !Scene_TubeShaped( lights[i].type ) )
					Com_Memset( &kept[i], 0, sizeof( kept[i] ) );
			shapes = kept.data();
		}
		std::vector<float> rec( (size_t)MAX( sc.numLights, 1u ) * 20, 0.0f );
		uint32_t tubes = 0;
		sceneSurfaceTubes = 0;
		for ( uint32_t i = 0; i < sc.numLights; i++ ) {
			if ( h->count[XS_BOXES] == sc.numLights )
				Com_Memcpy( &rec[(size_t)i * 20], (const byte *)boxes + (size_t)i * 64, 64 );
			if ( shapes ) {
				Com_Memcpy( &rec[(size_t)i * 20 + 16], &shapes[i], 16 );
				const uint32_t shaped = ( shapes[i].axis[0] != 0.0f || shapes[i].axis[1] != 0.0f || shapes[i].axis[2] != 0.0f ) ? 1u : 0u;
				tubes += shaped;
				sceneSurfaceTubes += ( shaped && !( (int)lights[i].type & 2 ) ) ? 1u : 0u;
			}
		}
		ok = Scene_StaticBuffer( &sc.lightBoxes, rec.data(), (VkDeviceSize)rec.size() * 4, ssbo, "scene light boxes" );
		sceneCharLights.assign( lights, lights + sc.numLights );
		sceneCharBoxes.assign( (size_t)sc.numLights * 16, 0.0f );
		if ( h->count[XS_BOXES] == sc.numLights )
			Com_Memcpy( sceneCharBoxes.data(), boxes, (size_t)sc.numLights * 64 );
		if ( shapes )
			sceneCharShapes.assign( shapes, shapes + sc.numLights );
		if ( shapes )
			ri.Printf( PRINT_ALL, "scene lights: %u of %u tubes (version 6 shapes), lit as line sources\n", tubes, sc.numLights );
		for ( uint32_t i = 0; i < h->count[XS_BOXES]; i++ )
			if ( lights[i].boxed != 0.0f )
				sc.numBoxedLights++;
	}
	// the shadow-casting lights' faces, their atlas (rendered once the views exist)
	std::vector<uint32_t> lsLights;
	std::vector<sceneShadowFace_t> lsFaces;
	std::vector<sceneShadowView_t> lsViews;
	if ( ok ) {
		Scene_PlanLightShadows( lights, sc.numLights, lsLights, lsFaces, lsViews );
		Scene_PlanRain( lights, sc.numLights, lsFaces, lsViews );
		std::vector<uint32_t> lsIds;	// the faces' instance lists (the GPU-driven path's, when the device has it)
		if ( r_sceneLightShadowLists->integer && vk.gpuDriven && sc.numShadowFaces )
			Scene_ListLightShadows( lsViews, inst, meshes, numInst, lsIds );
		if ( ss.on )
			Scene_StreamFaces( lsViews, lsIds, (const xscnInstance_t *)inst, (const xscnMesh_t *)meshes, numMeshes, numLods );
		ok = ( Scene_StaticBuffer( &sc.lsLights, lsLights.data(), (VkDeviceSize)lsLights.size() * 4, ssbo, "scene light shadow index" )
			&& ( lsIds.empty() || Scene_StaticBuffer( &sc.lsIds, lsIds.data(), (VkDeviceSize)lsIds.size() * 4, ssbo, "scene light shadow lists" ) )
			&& Scene_StaticBuffer( &sc.lsFaces, lsFaces.data(), (VkDeviceSize)lsFaces.size() * sizeof( sceneShadowFace_t ), ssbo, "scene light shadow faces" )
			&& Scene_CreateLightShadowAtlas() ) ? qtrue : qfalse;
	}
	if ( ok )
		ok = Scene_LoadRotations( name, inst, numInst );	// the rotating meshes' placements (maps/<map>.xrot)
	freeScene( buf );
	if ( ok )
		ok = Scene_LoadFogVolumes( name );
	if ( ok )
		Scene_LoadSignals( name );
	if ( ok )
		ok = Scene_LoadDetail( name );
	if ( ok )
		Scene_LoadPDecals( name );	// (before the DBuffer: it is made when there are any)

	if ( ok )
		ok = ( Scene_CreatePipelines() && Scene_CreateViews() ) ? qtrue : qfalse;
	if ( ok && ss.on )
		ok = Scene_StreamCreate();	// the pools as sc.vertices and sc.indices, filled as views want them
	if ( ok && sc.numWater && !sc.pipe[SCENE_PIPE_WATER] ) {	// no water pipeline: plain blends, as without r_sceneWater
		for ( uint32_t i = 0; i < numMats; i++ )
			if ( sc.materials[i].kind == SCENE_KIND_WATER ) {
				sc.materials[i].kind = SCENE_KIND_BLEND;
				Com_Memset( sc.materials[i].emissive, 0, sizeof( sc.materials[i].emissive ) );
			}
		ri.Printf( PRINT_WARNING, "WARNING: %s: no water pipeline; its %u water materials drawn as blends\n", name, sc.numWater );
		sc.numWater = 0;
	}
	if ( !ok ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: could not create its GPU resources, not drawn\n", name );
		R_SceneFree();
		return;
	}
	// the GPU-driven draws before the light shadows, which take their lists (r_sceneLightShadowLists)
	if ( vk.gpuDriven && !Scene_CreateGpuDriven() )
		ri.Printf( PRINT_WARNING, "WARNING: %s: could not create the GPU-driven draws; drawn per material\n", name );
	if ( ss.on )
		ss.lsViews = lsViews;	// drawn when the first view has settled (its geometry in)
	else
		Scene_RenderLightShadows( lsViews );
	sc.loaded = qtrue;
	Scene_CreateClusters();
	Scene_CreateLightFade();
	Scene_CreateWaves();
	Scene_CreateDistant();
	Scene_CreateViewCull();
	Scene_CreateAsync();
	ri.Printf( PRINT_ALL, "scene %s: %u instances of %u meshes, %llu triangles (all LODs), %u materials (%d images missing), "
		"%u lights (%u confined to interiors) in %dx%dx%d cells of %.0f (%u refs), %u draw commands, %u images%s, %.1f s\n", name, numInst, numMeshes,
		(unsigned long long)sc.numTriangles, numMats, missingImages, sc.numLights, sc.numBoxedLights, sc.gridDims[0], sc.gridDims[1], sc.gridDims[2],
		sc.gridOrigin[3], sc.numCellRefs, sc.numCmds, sc.numTextures, sc.gd ? " (GPU-driven)" : "", ( R_SunProfNow() - t0 ) * 0.001 );
	if ( sc.numDecals )
		ri.Printf( PRINT_ALL, "scene decals: %u mesh decal materials into the DBuffer (%ux%u)\n", sc.numDecals, sc.decalW, sc.decalH );
	if ( sc.numWater )
		ri.Printf( PRINT_ALL, "scene water: %u materials, surfaces from z %.0f to %.0f\n", sc.numWater, sc.waterLo + 16.0f, sc.waterHi - 16.0f );
	if ( sc.numAdGlows )
		ri.Printf( PRINT_ALL, "scene ad glows: %u materials (flag 0x400), %u without a normal image (no glow)\n", sc.numAdGlows, sc.numAdGlowsUnmasked );
	if ( sc.numLeaves )
		ri.Printf( PRINT_ALL, "scene leaves: %u materials (flag 0x800; r_sceneFoliage)\n", sc.numLeaves );
	if ( sc.numTerrain )
		ri.Printf( PRINT_ALL, "scene terrain: %u materials (flag 0x1000; r_sceneTerrainPush %d)\n", sc.numTerrain, r_sceneTerrainPush->integer );
	if ( sc.numHoloDots )
		ri.Printf( PRINT_ALL, "scene hologram dots: %u masked glows (.xscr template 5; r_sceneHoloDots)\n", sc.numHoloDots );
	if ( sc.numHolograms )
		ri.Printf( PRINT_ALL, "scene holograms: %u blend masked glows, blooming (r_sceneHoloBloom; the GPU-driven draws)\n", sc.numHolograms );
	Scene_LoadGrade( name );
}

// R_SetupSunShadowCascades: the far cascade's reach from this map's look (r_sceneShadowFar; 0: none of its own)
float vk_scene_shadow_far( void )
{
	if ( !sc.loaded || !r_scene->integer || !r_sceneShadows->integer )
		return 0.0f;
	return MAX( Env( r_sceneShadowFar )->value, 0.0f );
}

/* ------------------------------------------------------------------ frame */

int R_SceneAddView( viewParms_t *vp )
{
	if ( !sc.loaded || !r_scene->integer )
		return 0;
	if ( vp->portalView != PV_NONE || vp->charShadow
#ifdef VK_CUBEMAP
		|| vp->targetCube
#endif
		)
		return 0;
	if ( tr.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) )
		return 0;
	if ( sc.numViews >= SCENE_MAX_VIEWS )
		return 0;
	// a cascade casts for the main view it was set up for (R_SetupSunShadowCascades runs before that view)
	const int cascade = vp->sunCascade;
	if ( cascade && ( cascade > MIN( r_sceneShadowCascades->integer, 3 ) || !sc.shadowPipes || sc.eyeView < 0 || !sc.sunOn || !r_sceneShadows->integer ) )
		return 0;

	sceneView_t *v = &sc.views[sc.numViews];
	for ( int i = 0; i < 5; i++ ) {
		VectorCopy( vp->frustum[i].normal, v->planes[i] );
		v->planes[i][3] = vp->frustum[i].dist;
	}
	v->cascade = cascade;

	if ( cascade ) {
		// the main view's LOD (coarser), so the shadow matches what is drawn; casters under a few texels dropped
		const sceneView_t *eye = &sc.views[sc.eyeView];
		const float texel = 2.0f * -vp->zNear / (float)MAX( vp->viewportHeight, 1 );
		VectorCopy( eye->origin, v->origin );
		// the far cascade kept across frames (r_sunShadowFarCache) is centred on the eye's grid cell and keeps its
		// casters while the eye moves inside it: it takes its LODs, hides and near distances (scene_cull.comp reads
		// this origin for every view but the eye's) from that centre, so what it keeps matches one point, not the eye
		// of the frame it was drawn in
		if ( vk_sun_shadow_kept( cascade - 1 ) )	// (the middle one too with r_sunShadowMidCache)
			VectorCopy( vp->ori.origin, v->origin );
		v->invTanHalfFovY = eye->invTanHalfFovY;
		v->viewportHeight = eye->viewportHeight;
		v->lodScale = MAX( r_sceneShadowLod->value, 0.01f );
		v->minRadius = MAX( r_sceneShadowCull->value, 0.0f ) * texel;
		sc.sunTexel[cascade - 1] = texel;
		return ++sc.numViews;
	}

	VectorCopy( vp->ori.origin, v->origin );
	v->invTanHalfFovY = 1.0f / tanf( DEG2RAD( vp->fovY * 0.5f ) );
	for ( int k = 0; k < 3; k++ )
		VectorCopy( vp->ori.axis[k], v->axis[k] );
	v->tanHalfFov[0] = tanf( DEG2RAD( vp->fovX * 0.5f ) );
	v->tanHalfFov[1] = tanf( DEG2RAD( vp->fovY * 0.5f ) );
	v->viewportHeight = (float)MAX( vp->viewportHeight, 1 );
	v->lodScale = 1.0f;
	v->minRadius = 0.0f;

	if ( sc.eyeView < 0 ) {
		// the frame's sun, as the cascades about to be drawn for this view were set up
		sc.eyeView = sc.numViews;
		sc.sunOn = ( tr.refdef.sunShadowParams[0] != 0.0f && tr.refdef.sunDir[3] != 0.0f ) ? qtrue : qfalse;
		if ( sc.sunOn ) {
			Com_Memcpy( sc.sunMvp, tr.refdef.sunShadowMvp, sizeof( sc.sunMvp ) );
			VectorNormalize2( tr.refdef.sunDir, sc.sunDir );
			// the sun's hue from q3gl2_sun, its strength from r_sceneSun (the scene's own light units); in r_sunlightMode 2
			// from tr.sunLight (refdef.sunCol's hue there), as refdef.sunCol may hold the models' sun (r_sceneCharSun)
			const float *hue = r_sunlightMode->integer == 2 ? tr.sunLight : tr.refdef.sunCol;
			const float m = MAX( hue[0], MAX( hue[1], hue[2] ) );
			if ( m > 0.0f )
				VectorScale( hue, 1.0f / m, sc.sunCol );
			else
				VectorSet( sc.sunCol, 1.0f, 1.0f, 1.0f );
			sc.sunCol[3] = tr.refdef.sunShadowParams[1];
		}
		sc.signalTime = tr.refdef.time;	// the signals' clock: the game's, as the cgame's sounds (cg_signals.c)
		// the view's dynamic lights (a saber's glow, shots, effects): positive ones, their colour as linear light
		sc.numDlights = 0;
		for ( int i = 0; i < tr.refdef.num_dlights && sc.numDlights < SCENE_MAX_DLIGHTS; i++ ) {
			const dlight_t *dl = &tr.refdef.dlights[i];
			if ( dl->radius <= 0.0f || dl->color[0] < 0.0f || dl->color[1] < 0.0f || dl->color[2] < 0.0f
				|| dl->color[0] + dl->color[1] + dl->color[2] <= 0.0f )
				continue;
			float *d = sc.dlights[sc.numDlights++][0];
			VectorCopy( dl->origin, d );
			d[3] = dl->radius * MAX( r_sceneDlightReach->value, 0.0f );	// (sized for lightmapped maps)
			for ( int k = 0; k < 3; k++ )
				d[4 + k] = powf( MIN( dl->color[k], 1.0f ), 2.2f );
			d[7] = 0.0f;
		}
	}

	// the far clip comes from the visible bounds: they must cover the scene
	AddPointToBounds( sc.drawMins, vp->visBounds[0], vp->visBounds[1] );
	AddPointToBounds( sc.drawMaxs, vp->visBounds[0], vp->visBounds[1] );
	return ++sc.numViews;
}

// sc's device-level commands (see their fields), fetched for the current device on first use
static void Scene_DeviceCommands( void )
{
	if ( sc.cmdUpdateBuffer )
		return;
	sc.cmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)qvkGetDeviceProcAddr( vk.device, "vkCmdUpdateBuffer" );
	sc.cmdBeginQuery = (PFN_vkCmdBeginQuery)qvkGetDeviceProcAddr( vk.device, "vkCmdBeginQuery" );
	sc.cmdEndQuery = (PFN_vkCmdEndQuery)qvkGetDeviceProcAddr( vk.device, "vkCmdEndQuery" );
	sc.cmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)qvkGetDeviceProcAddr( vk.device, "vkCmdDispatchIndirect" );
}

void Scene_Barrier( VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst, VkAccessFlags dstAccess )
{
	VkMemoryBarrier b;
	Com_Memset( &b, 0, sizeof( b ) );
	b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	b.srcAccessMask = srcAccess;
	b.dstAccessMask = dstAccess;
	qvkCmdPipelineBarrier( vk.cmd->command_buffer, src, dst, 0, 1, &b, 0, NULL, 0, NULL );
}

// scene_compact.comp's push: each list's first slot (lists 0..3 in base0, 4 in base1.x, 5 in base1.z; base1.y is the
// block-parallel packing's blocks)
static void Scene_CompactBases( uint32_t pc[12] )
{
	for ( int k = 0; k < 5; k++ )
		pc[4 + k] = sc.listBase[k];
	pc[10] = sc.listBase[SCENE_LIST_WATER];
}

// slot f's pool as its last frame left it (its fence was waited): an overflow is printed and the slot's pools doubled,
// every set that binds them rewritten (none is in use: the slot's last frame is done and nothing of this one is recorded)
static void Scene_CullPoolCheck( int f )
{
	const uint32_t *h = sc.cullHdrPtr[f];
	if ( !h || h[15] <= sc.visOverSeen[f] )
		return;
	const uint32_t lost = h[15] - sc.visOverSeen[f];
	const uint32_t was = sc.visCap[f];
	const uint32_t limit = (uint32_t)MIN( (uint64_t)sc.numInstances * ( SCENE_MAX_VIEWS + 1 ), (uint64_t)64 * 1024 * 1024 );
	if ( was >= limit ) {	// as large as it gets: reported, not grown
		ri.Printf( PRINT_WARNING, "scene cull: slot %d's visible pool (%u pairs) left out %u instances (at its largest)\n", f, was, lost );
		sc.visOverSeen[f] = h[15];
		return;
	}
	vk_wait_idle();
	Scene_DestroyBuffer( &sc.visible[f][0] );
	Scene_DestroyBuffer( &sc.cullPairs[f] );
	sc.visCap[f] = MIN( was * 2, limit );
	if ( !Scene_CullPoolCreate( f ) ) {	// no room for the larger one: the one it had again (the overflow reported)
		Scene_DestroyBuffer( &sc.visible[f][0] );
		Scene_DestroyBuffer( &sc.cullPairs[f] );
		sc.visCap[f] = was;
		if ( !Scene_CullPoolCreate( f ) ) {
			ri.Error( ERR_DROP, "scene cull: no memory for slot %d's visible pool of %u pairs", f, was );
			return;
		}
		ri.Printf( PRINT_WARNING, "scene cull: slot %d's visible pool (%u pairs) left out %u instances for a frame; no memory to grow it\n", f, was, lost );
	} else {
		ri.Printf( PRINT_WARNING, "scene cull: slot %d's visible pool (%u pairs) left out %u instances for a frame; grown to %u (%.1f MB)\n",
			f, was, lost, sc.visCap[f], sc.visCap[f] * 20.0 / 1048576.0 );
	}
	for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
		Scene_WriteBinding( sc.cullSet[f][v], 5, &sc.visible[f][0] );
		Scene_WriteBinding( sc.cullSet[f][v], 15, &sc.cullPairs[f] );
		Scene_WriteBinding( sc.sortSet[f][v], 3, &sc.visible[f][0] );
		Scene_WriteBinding( sc.drawSet[f][v], 0, &sc.visible[f][0] );
		Scene_WriteBinding( ss.faceCull[f][v], 5, &sc.visible[f][0] );
		Scene_WriteBinding( ss.faceCull[f][v], 15, &sc.cullPairs[f] );
	}
	Scene_WriteBinding( sc.sortTouchedSet[f], 3, &sc.visible[f][0] );
	Scene_CullPoolScatterSet( f );
}

// vk_begin_frame, before any render pass: cull every view the front end registered into this frame's commands
void R_SceneVkBeginFrame( void )
{
	const int f = vk.cmd_index;
	const int n = sc.numViews;

	const int eyeView = sc.eyeView;
	const qboolean sunOn = sc.sunOn;

	sc.numViews = 0;
	sc.culledViews = 0;
	sc.eyeView = -1;
	sc.sunOn = qfalse;
	sc.fogOn = qfalse;
	sc.occBuild = sc.occDrawn = sc.occOnly = qfalse;	// (occlusion: set below for a frame with an eye view)
	sc.decalDone = qfalse;
	sc.occSlot = -1;
	if ( !sc.loaded || !n || eyeView < 0 ) {
		sc.hazeRan = qfalse;	// a frame without the scene's eye view: the haze history no longer follows on
		return;
	}
	Scene_CullPoolCheck( f );	// this slot's last overflow (its fence was waited): its pools grown before anything binds them
	const double tPre = vk_cputime_active() ? R_SunProfNow() : 0.0;	// r_cpuTimes: scenepre
	if ( sc.texSets[f] ) {	// this slot's image set as the streamed images now are
		Scene_RefreshTextures( f );
		sc.texSet = sc.texSets[f];
	}

	// the frame's shading constants
	sceneFrame_t *fr = (sceneFrame_t *)sc.framePtr[f];
	Com_Memset( fr, 0, sizeof( *fr ) );
	VectorCopy( sc.views[eyeView].origin, fr->eye );
	// the instances' hide (v4) and near (v5) distances dithered in the eye's views (scene.vert, scene.frag): their scale
	sc.fadeOn = ( r_sceneHideFade->integer && r_sceneProxyDebug->integer != 2 && r_sceneHideScale->value > 0.0f && sc.pipeDepthFade && ( !sc.gd || sc.gdPipeDepthFade ) ) ? qtrue : qfalse;
	// r_sceneHideDither 2 needs the masked prepass and a lit pass that tests EQUAL against it (it shades only the kept
	// samples): GPU-driven draws, r_sceneLitEqual, no debug view, MSAA; else the blue noise (1)
	sc.hideDither = (int)Com_Clamp( 0, 2, r_sceneHideDither->integer );
	if ( sc.hideDither == 2 && !( sc.gd && sc.gdPipeFadeMask[0] && sc.gdPipeFadeMask[1] && r_sceneLitEqual->integer && sc.gdPipe[SCENE_GD_LIT_EQUAL] &&
			!r_sceneDebug->integer && vkSamples > 1 && vkSamples <= 8 ) )	// (scene.frag's mask covers 8 samples)
		sc.hideDither = 1;
	// r_sceneCutoutCoverage needs the same: the masked cutout prepass and the EQUAL lit pass writing the same mask, with 2,
	// 4 or 8 samples (the standard locations scene.frag carries)
	sc.cutoutCov = ( r_sceneCutoutCoverage->integer && sc.gd && sc.gdPipeFadeMask[1] && sc.gdPipeLitCov && r_sceneLitEqual->integer &&
		sc.gdPipe[SCENE_GD_LIT_EQUAL] && !r_sceneDebug->integer && ( vkSamples == 2 || vkSamples == 4 || vkSamples == 8 ) ) ? qtrue : qfalse;
	fr->eye[3] = sc.fadeOn ? r_sceneHideScale->value : 0.0f;
	Com_Memcpy( fr->gridOrigin, sc.gridOrigin, sizeof( fr->gridOrigin ) );
	Com_Memcpy( fr->gridDims, sc.gridDims, sizeof( fr->gridDims ) );
	sscanf( Env( r_sceneAmbient )->string, "%f %f %f", &fr->skyAmbient[0], &fr->skyAmbient[1], &fr->skyAmbient[2] );
	VectorScale( fr->skyAmbient, 0.35f, fr->groundAmbient );
	fr->groundAmbient[3] = Com_Clamp( 0.0f, 0.95f, r_sceneFogLightKnee->value );	// (scene_fog.comp)
	fr->skyAmbient[3] = Com_Clamp( 0.0f, 0.05f, r_sceneProxyPush->value );	// (scene.vert)
	fr->params[0] = Env( r_sceneLightScale )->value;
	fr->params[1] = Scene_MeterFrame( f );	// (r_sceneAutoExposure: vk_scene_post.cpp)
	// emissives against the exposure (CP2077's EmissiveColorSettings.exposureScale): as authored at r_sceneEmissiveRef's
	// exposure, following the share r_sceneEmissiveExposure of the exposure's change from it (1: all, as before)
	{
		const float exposure = MAX( Scene_Exposure(), 1e-4f ), ref = MAX( Env( r_sceneEmissiveRef )->value, 1e-4f );
		const float share = Com_Clamp( 0.0f, 1.0f, Env( r_sceneEmissiveExposure )->value );
		fr->params[2] = Env( r_sceneEmissive )->value * ( share < 1.0f ? powf( exposure / ref, share - 1.0f ) : 1.0f );
	}
	fr->params[3] = (float)r_sceneDebug->integer;
	fr->params2[0] = MAX( Env( r_sceneGlow )->value, 0.0f );
	fr->params2[1] = MAX( Env( r_sceneGlowThreshold )->value, 0.0f );
	fr->params2[2] = (float)Env( r_sceneTonemap )->integer;
	fr->params2[3] = ( sc.numShadowFaces && r_sceneLightShadows->integer ) ? 1.0f : 0.0f;
	sscanf( Env( r_sceneFog )->string, "%f %f %f", &fr->fog[0], &fr->fog[1], &fr->fog[2] );
	VectorScale( fr->fog, 1.0f / 255.0f, fr->fog );
	fr->fog[3] = MAX( Env( r_sceneFogDensity )->value, 0.0f );
	fr->fog2[0] = MAX( Env( r_sceneFogHeight )->value, 1.0f );
	fr->fog2[1] = Com_Clamp( 0.0f, 1.0f, Env( r_sceneFogMax )->value );
	fr->fog2[2] = (float)sc.numFogVolumes;
	fr->fog2[3] = MAX( r_sceneFogVolumes->value, 0.0f );
	{
		const sceneView_t *eye = &sc.views[eyeView];
		const float farUnits = MAX( Env( r_sceneFogFar )->value, 10.0f ) * 37.5f;
		VectorCopy( eye->axis[0], fr->fogFwd );
		VectorCopy( eye->axis[1], fr->fogLeft );
		VectorCopy( eye->axis[2], fr->fogUp );
		fr->fogFwd[3] = SCENE_FOG_NEAR;
		fr->fogLeft[3] = eye->tanHalfFov[0];
		fr->fogUp[3] = eye->tanHalfFov[1];
		fr->fogVol[0] = logf( farUnits / SCENE_FOG_NEAR );
		sc.fogOn = ( Env( r_sceneFogLight )->value > 0.0f && fr->fog[3] > 0.0f && r_sceneDebug->integer == 0 ) ? qtrue : qfalse;
		fr->fogVol[1] = sc.fogOn ? Env( r_sceneFogLight )->value : 0.0f;
		fr->fogVol[2] = Com_Clamp( -0.95f, 0.95f, Env( r_sceneFogPhase )->value );
		fr->fogVol[3] = MAX( Env( r_sceneFogStart )->value, 0.0f );
		fr->fogAlbedo[0] = fr->fogAlbedo[1] = fr->fogAlbedo[2] = 1.0f;
		sscanf( Env( r_sceneFogAlbedo )->string, "%f %f %f", &fr->fogAlbedo[0], &fr->fogAlbedo[1], &fr->fogAlbedo[2] );
		fr->fogAlbedo[3] = MAX( Env( r_sceneFogMoon )->value, 0.0f );
		sc.fogEyeView = eyeView;
		// r_sceneFogTemporal: this frame's sample point in each froxel (Halton 2, 3, 5 over 16 frames) and last frame's
		// volume, valid when its scatter was written in the last frame over the same depth range
		const qboolean temporal = ( r_sceneFogTemporal->integer && sc.fogOn ) ? qtrue : qfalse;
		Com_Memset( fr->hazeJitter, 0, sizeof( fr->hazeJitter ) );
		Com_Memset( fr->hazePrevEye, 0, sizeof( fr->hazePrevEye ) );
		if ( temporal ) {
			static const int bases[3] = { 2, 3, 5 };
			const uint32_t k = ( sc.frameNo & 15u ) + 1u;
			for ( int a = 0; a < 3; a++ ) {
				float h = 0.0f, inv = 1.0f / bases[a];
				for ( uint32_t n = k; n; n /= bases[a], inv /= bases[a] )
					h += (float)( n % bases[a] ) * inv;
				fr->hazeJitter[a] = h - 0.5f;
			}
			fr->hazeJitter[3] = 0.15f;
			if ( sc.hazeRan && sc.hazePrevLog == fr->fogVol[0] ) {
				VectorCopy( sc.hazePrev[0], fr->hazePrevEye );
				fr->hazePrevEye[3] = 1.0f;
				Com_Memcpy( fr->hazePrevFwd, sc.hazePrev[1], sizeof( fr->hazePrevFwd ) );
				Com_Memcpy( fr->hazePrevLeft, sc.hazePrev[2], sizeof( fr->hazePrevLeft ) );
				Com_Memcpy( fr->hazePrevUp, sc.hazePrev[3], sizeof( fr->hazePrevUp ) );
			}
		}
		VectorCopy( eye->origin, sc.hazePrev[0] );
		Com_Memcpy( sc.hazePrev[1], fr->fogFwd, sizeof( fr->fogFwd ) );
		Com_Memcpy( sc.hazePrev[2], fr->fogLeft, sizeof( fr->fogLeft ) );
		Com_Memcpy( sc.hazePrev[3], fr->fogUp, sizeof( fr->fogUp ) );
		sc.hazePrevLog = fr->fogVol[0];
		sc.hazeRan = qfalse;	// set again when this frame's haze runs
	}
	fr->wet[0] = Com_Clamp( 0.0f, 1.0f, Env( r_sceneWet )->value );
	fr->wet[1] = Com_Clamp( 0.0f, 1.0f, r_sceneWetPuddles->value );
	fr->wet[2] = MAX( r_sceneLightSize->value, 0.0f );
	fr->wet[3] = MAX( r_sceneWindows->value, 0.0f );
	if ( Env( r_sceneWindowExposure )->value >= 0.0f ) {	// the lit window rooms' own share of the exposure's change (params[2]'s)
		const float exposure = MAX( Scene_Exposure(), 1e-4f ), ref = MAX( Env( r_sceneEmissiveRef )->value, 1e-4f );
		const float share = Com_Clamp( 0.0f, 1.0f, Env( r_sceneEmissiveExposure )->value );
		fr->wet[3] *= powf( exposure / ref, Com_Clamp( 0.0f, 1.0f, Env( r_sceneWindowExposure )->value ) - share );
	}
	// the reflections' history: captured in the last frame (vk_scene_capture)
	const qboolean history = ( sc.capturedAt == sc.frameNo && sc.capturedAt != 0 ) ? qtrue : qfalse;
	// occlusion (r_sceneOcclusion, the GPU-driven draws: phase 2's lists go to a spare view slot): this frame splits the
	// main pass after the eye's prepass to build a pyramid; phase 1 runs when the last frame built one
	const qboolean gdFrame = ( sc.gd && r_sceneGpuDriven->integer && r_depthPrepass->integer ) ? qtrue : qfalse;	// (the lit list shades only where the prepass is nearest)
	// (the split resumes in main_reload, created only with SSAO active: without it no split, so phase 1 hides nothing that
	// phase 2 could not draw)
	sc.occBuild = ( r_sceneOcclusion->integer > 0 && gdFrame && sc.occPipeDepth && sc.occLevels && vk.render_pass.main_reload.handle ) ? qtrue : qfalse;
	const qboolean occPhase1 = ( sc.occBuild && n < SCENE_MAX_VIEWS && sc.occAt == sc.frameNo && sc.occAt != 0 ) ? qtrue : qfalse;
	const qboolean occStats = r_sceneOcclusionStats->integer > 0 ? qtrue : qfalse;
	sc.occSlot = occPhase1 ? n : -1;
	sc.occEye = eyeView;
	sc.occDrawn = sc.occOnly = qfalse;
	if ( occPhase1 )
		sc.views[n] = sc.views[eyeView];	// phase 2 culls the eye view again into this slot
	sc.frameNo++;
	fr->ssr[0] = ( history && r_sceneReflect->value > 0.0f && ( fr->wet[0] > 0.0f || sc.numWater ) && r_sceneDebug->integer != 9 ) ? 1.0f : 0.0f;
	fr->ssr[1] = MAX( r_sceneReflect->value, 0.0f );
	fr->ssr[3] = (float)sc.reflectIndex;
	fr->rain[0] = r_sceneRain->integer ? (float)sc.rainFace : 0.0f;
	fr->rain[1] = sc.rainMargin;
	fr->rain[2] = MAX( Env( r_sceneFogLightMax )->value, 0.0f );			// (scene_fog.comp)
	fr->rain[3] = r_sceneFogSegment->integer ? 2.0f : r_sceneFogNearSamples->integer ? 1.0f : 0.0f;
	fr->detail[0] = sc.numDetailed ? Com_Clamp( 0.0f, 2.0f, r_sceneDetail->value ) : 0.0f;
	fr->ssr[2] = (float)Com_Clamp( 0, 8, r_sceneDetailSlots->integer );	// (scene.frag: a version 2 material's slots)
	fr->water[0] = (float)( ( (uint32_t)sc.signalTime % 86400000u ) * 0.001 );	// the game's clock (a replayed frame repeats), a day round (an hour made every effect jump at once; 8 ms steps at its end)
	fr->water[1] = sc.waterLo;
	fr->water[2] = sc.waterHi;
	fr->water[3] = MAX( r_sceneWaterRipple->value, 0.0f );
	fr->water2[0] = MAX( r_sceneWaterClarity->value, 0.01f );
	fr->water2[1] = MAX( r_sceneWaterReflect->value, 0.0f );
	fr->water2[2] = r_sceneWaterShift->value;
	fr->water2[3] = r_sceneWaterBreakup->integer ? 1.0f : 0.0f;
	Com_Memset( fr->waterWaves, 0, sizeof( fr->waterWaves ) );	// (set again below when the waves run)
	Com_Memset( fr->waterWaves2, 0, sizeof( fr->waterWaves2 ) );
	fr->waterBed[3] = 0;	// (vk_scene_occlusion: the pyramid built after the eye's prepass)
	fr->dlightCount[0] = r_sceneDlights->value > 0.0f ? (float)sc.numDlights : 0.0f;
	fr->dlightCount[1] = MAX( r_sceneDlights->value, 0.0f );
	fr->dlightCount[2] = sc.cutoutCov ? (float)vkSamples : 0.0f;	// (scene.frag cutoutCoverage)
	Com_Memcpy( fr->dlights, sc.dlights, sizeof( sc.dlights[0] ) * sc.numDlights );
	Scene_SignalStates( fr->signals );
	sc.skyOn = Env( r_sceneSky )->integer && !r_sceneDebug->integer ? qtrue : qfalse;
	Com_Memset( fr->skyZenith, 0, sizeof( fr->skyZenith ) );
	Com_Memset( fr->skyHorizon, 0, sizeof( fr->skyHorizon ) );
	Com_Memset( fr->skyGround, 0, sizeof( fr->skyGround ) );
	fr->skyGround[3] = 4.0f;
	if ( sc.skyOn ) {
		sscanf( Env( r_sceneSkyGround )->string, "%f %f %f", &fr->skyGround[0], &fr->skyGround[1], &fr->skyGround[2] );
		fr->skyGround[3] = Com_Clamp( 0.25f, 16.0f, Env( r_sceneSkyCurve )->value );
		sscanf( Env( r_sceneSkyZenith )->string, "%f %f %f", &fr->skyZenith[0], &fr->skyZenith[1], &fr->skyZenith[2] );
		sscanf( Env( r_sceneSkyHorizon )->string, "%f %f %f", &fr->skyHorizon[0], &fr->skyHorizon[1], &fr->skyHorizon[2] );
		fr->skyZenith[3] = 1.0f;
		fr->skyHorizon[3] = MAX( Env( r_sceneSkySun )->value, 0.0f );
	}
	// a screen's face (material flag 0x200) as CP2077's ad shaders draw it (scene.frag's screenFace, r_sceneScreenLook)
	fr->screen[0] = r_sceneScreenLook->integer ? 1.0f : 0.0f;
	fr->screen[1] = MAX( r_sceneScreenGrid->value, 0.0f );
	fr->screen[2] = Com_Clamp( 0.0f, 1.0f, r_sceneScreenGridDepth->value );
	fr->screen[3] = Com_Clamp( 0.0f, 0.2f, r_sceneScreenDepth->value );
	fr->screen2[0] = Com_Clamp( 0.0f, 1.0f, r_sceneScreenHue->value );
	fr->screen2[1] = MAX( r_sceneScreenGlow->value, 1.0f );
	fr->screen2[2] = fr->screen2[3] = 0.0f;
	sscanf( r_sceneScreenLayers->string, "%f %f", &fr->screen2[2], &fr->screen2[3] );
	fr->screen3[0] = r_sceneScreenAnim->integer ? 1.0f : 0.0f;
	fr->screen3[1] = MAX( r_sceneScreenGain->value, 0.0f );
	fr->screen3[2] = r_sceneScreenTone->integer ? 1.0f : 0.0f;
	fr->screen3[3] = MAX( r_sceneScreenBloom->value, 0.0f );
	fr->foliage[0] = Com_Clamp( 0.0f, 1.0f, Env( r_sceneFoliage )->value );
	fr->foliage[1] = Com_Clamp( 0.0f, 1.0f, Env( r_sceneFoliageWrap )->value );
	fr->foliage[2] = MAX( Env( r_sceneFoliageTrans )->value, 0.0f );
	fr->foliage[3] = Com_Clamp( 0.0f, 1.0f, Env( r_sceneFoliageAO )->value );
	fr->clouds[0] = sc.skyOn ? Com_Clamp( 0.0f, 1.0f, Env( r_sceneClouds )->value ) : 0.0f;
	fr->clouds[1] = MAX( Env( r_sceneCloudScale )->value, 0.05f );
	fr->clouds[2] = Com_Clamp( 0.01f, 1.0f, Env( r_sceneCloudEdge )->value );
	fr->clouds[3] = MAX( Env( r_sceneCloudLight )->value, 0.0f );
	fr->clouds2[0] = fr->clouds2[1] = fr->clouds2[2] = fr->clouds2[3] = 0.0f;
	sscanf( Env( r_sceneCloudWind )->string, "%f %f", &fr->clouds2[0], &fr->clouds2[1] );
	fr->clouds2[3] = MAX( Env( r_sceneShadowSoft )->value, 0.0f );	// r_sceneShadowSoft (scene.frag sunShadow)
	fr->clouds2[2] = ( history && r_sceneDebug->integer != 9 ) ? Com_Clamp( 0.0f, 1.0f, Env( r_sceneAO )->value ) : 0.0f;	// r_sceneAO (last frame's)
	fr->holo[0] = r_sceneHoloDots->integer ? 1.0f : 0.0f;
	fr->holo[1] = MAX( r_sceneHoloBloom->value, 0.0f );
	fr->holo[2] = MAX( r_sceneHoloGain->value, 0.0f );
	fr->holo[3] = Com_Clamp( 0.1f, 8.0f, r_sceneHoloCurve->value );
	// r_sceneMoon: the moon on the sky (scene.frag moonSky) while its image is found and its disc is up
	Com_Memset( fr->moon, 0, sizeof( fr->moon ) );
	Com_Memset( fr->moonCol, 0, sizeof( fr->moonCol ) );
	Com_Memset( fr->moonGlow, 0, sizeof( fr->moonGlow ) );
	if ( sceneMoonImage && Env( r_sceneMoon )->value > 0.0f ) {
		vec4_t col;
		Scene_MoonAt( Scene_Clock(), fr->moon, col );
		const float radius = Com_Clamp( 0.001f, 0.2f, Env( r_sceneMoonSize )->value );
		if ( fr->moon[2] > -radius ) {
			fr->moon[3] = radius;
			VectorScale( col, col[3] * Env( r_sceneMoon )->value, fr->moonCol );
			float g[4] = { 0.006f, 100.0f, 0.0f, 1.0f };
			sscanf( Env( r_sceneMoonGlow )->string, "%f %f %f %f", &g[0], &g[1], &g[2], &g[3] );
			fr->moonCol[3] = MAX( g[0], 0.0f );
			fr->moonGlow[0] = MAX( g[1], 1.0f );
			fr->moonGlow[1] = MAX( g[2], 0.0f );
			fr->moonGlow[2] = MAX( g[3], 0.05f );
		}
	}
	fr->fx[0] = fr->fx[1] = fr->fx[3] = 1.0f;
	sscanf( r_sceneFxLight->string, "%f %f %f", &fr->fx[0], &fr->fx[1], &fr->fx[3] );
	fr->fx[0] = MAX( fr->fx[0], 0.0f );
	fr->fx[1] = MAX( fr->fx[1], 0.0f );
	fr->fx[2] = MAX( r_sceneFxSoft->value, 0.0f );
	fr->fx[3] = MAX( fr->fx[3], 0.0f );
	fr->fx2[0] = MAX( r_sceneFxAlpha->value, 0.0f );
	fr->fx2[1] = sceneSurfaceTubes ? 1.0f : 0.0f;
	fr->fx2[2] = (float)sc.hideDither;
	fr->fx2[3] = (float)vkSamples;
	fr->cards[0] = r_sceneCardBlend->integer ? 1.0f : 0.0f;
	fr->cards[1] = fr->cards[2] = fr->cards[3] = 0.0f;
	fr->surface2[0] = Com_Clamp( 0.0f, 1.0f, r_sceneSpecAA->value );
	fr->surface2[1] = r_sceneWindowCell->integer ? 1.0f : 0.0f;
	fr->surface2[2] = Com_Clamp( 0.0f, 1.0f, r_sceneProbeSky->value );
	fr->surface2[3] = 0.0f;
	fr->wet2[0] = fr->wet2[1] = fr->wet2[2] = fr->wet2[3] = 0.0f;
	sscanf( r_sceneWetPorosity->string, "%f %f %f %f", &fr->wet2[0], &fr->wet2[1], &fr->wet2[2], &fr->wet2[3] );
	fr->wet2[2] = Com_Clamp( 0.0f, 1.0f, fr->wet2[2] );
	fr->wet2[3] = MAX( fr->wet2[3], 0.0f );
	// the eye view's light clusters: slices from the haze's near distance to its far one (the last open)
	const qboolean clusters = ( sc.clusterPipe && r_sceneClusters->integer ) ? qtrue : qfalse;
	const float clusterLog = logf( MAX( Env( r_sceneFogFar )->value, 10.0f ) * 37.5f / SCENE_FOG_NEAR ) / SCENE_CLUSTER_Z;
	fr->cluster[0] = 1.0f / clusterLog;
	// 2 (r_sceneClusters 2): also in the cells that kept only their strongest lights
	const float clusterMode = r_sceneClusters->integer >= 2 ? 2.0f : 1.0f;
	fr->cluster[2] = clusters ? clusterMode : 0.0f;
	fr->cluster[1] = ( clusters && sc.fogOn ) ? clusterMode : 0.0f;	// the haze's (scene_fog.comp)
	fr->cluster[3] = ( r_sceneLightDistant->integer && sc.distantPipe && r_sceneLightHide->value > 0.0f ) ? 1.0f : 0.0f;	// the stand-ins (scene_distant.comp)
	Com_Memcpy( fr->prevViewProj, sc.prevViewProj, sizeof( fr->prevViewProj ) );
	VectorCopy( sc.prevEye, fr->prevEye );
	VectorCopy( sc.prevFwd, fr->prevFwd );
	if ( sunOn && r_sceneShadows->integer ) {
		Com_Memcpy( fr->sunMvp, sc.sunMvp, sizeof( fr->sunMvp ) );
		VectorCopy( sc.sunDir, fr->sunDir );
		fr->sunDir[3] = 1.0f;
		VectorScale( sc.sunCol, MAX( Env( r_sceneSun )->value, 0.0f ), fr->sunCol );
		fr->sunCol[3] = sc.sunCol[3];
		for ( int k = 0; k < 3; k++ )
			fr->sunTexel[k] = sc.sunTexel[k];
		// scene.frag sunShadow: the last cascade's reach (the shadows fade out before it), and + 0.5 when the middle
		// cascade is kept (its blend into the far one is measured from the eye)
		fr->sunTexel[3] = floorf( MAX( r_shadowCascadeZFar->value, vk_scene_shadow_far() ) ) + ( vk_sun_shadow_kept( 1 ) ? 0.5f : 0.0f );
	}

	VkCommandBuffer cb = vk.cmd->command_buffer;
	const VkDeviceSize cmdBytes = (VkDeviceSize)sc.numCmds * sizeof( VkDrawIndexedIndirectCommand );
	Scene_InvocationsFrame( cb, f );

	// occlusion: this slot's last counters (its fence was waited: r_sceneOcclusionStats), then camera 0 of the cull's Occ
	// block, the last pyramid's (camera 1, this frame's, is written by vk_scene_occlusion before the frame is submitted)
	if ( sc.occStatsPtr[f] ) {
		float *op = (float *)sc.occStatsPtr[f];
		uint32_t *ou = (uint32_t *)op;
		if ( sc.occCounted[f] )
			ri.Printf( PRINT_ALL, "SCENEOCC instances %u hidden %u indices %u hidden %u phase2 instances %u indices %u\n",
				ou[84], ou[85], ou[86], ou[87], ou[88], ou[89] );
		sc.occCounted[f] = ( occPhase1 && occStats ) ? qtrue : qfalse;
		if ( sc.clusterCounted[f] )
			ri.Printf( PRINT_ALL, "SCENECLUSTERS full tiles %u clusters %u (surfaces and haze)\n", ou[90], ou[91] );
		ou[90] = ou[91] = 0;
		sc.clusterCounted[f] = ( sc.clusterPipe && r_sceneClusters->integer && r_sceneClusterStats->integer > 0 ) ? qtrue : qfalse;
		if ( occPhase1 ) {
			Com_Memcpy( op, sc.occViewProj, 64 );
			VectorCopy( sc.occCamEye, op + 32 );
			VectorCopy( sc.occCamFwd, op + 40 );
			ou[48] = glConfig.vidWidth;
			ou[49] = glConfig.vidHeight;
			ou[50] = sc.occLevels;
			for ( uint32_t L = 0; L < 16; L++ )
				ou[52 + L] = L < sc.occLevels ? sc.occOfs[L] + sc.occRegion * sc.occTotal : 0;
			Com_Memset( ou + 84, 0, 32 );
		}
	}
	// each light's fade with the eye's distance (r_sceneLightHide), before the clusters, the haze and the lit pass read it
	if ( sc.lightFadePipe && sc.numLights && eyeView >= 0 ) {
		float pc[12];
		VectorCopy( sc.views[eyeView].origin, pc );
		pc[3] = MAX( r_sceneLightHide->value, 0.0f );
		pc[4] = (float)sc.numLights;
		pc[5] = sc.hasLightBoxes ? 1.0f : 0.0f;
		pc[6] = 0.15f;	// the fade over the last 15% of the hide distance
		pc[7] = MAX( r_sceneLightHideDefault->value, 0.0f );
		pc[8] = 80.0f * 37.5f;	// that default's most: 80 m
		pc[9] = ( r_sceneLightDistant->integer && sc.distantPipe ) ? 1.0f : 0.0f;
		pc[10] = r_sceneLightHideDistant->integer ? 1.0f : 0.0f;
		pc[11] = 0.0f;
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.lightFadePipe );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.lightFadePipeLayout, 0, 1, &sc.lightFadeSet[f], 0, NULL );
		qvkCmdPushConstants( cb, sc.lightFadePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdDispatch( cb, ( sc.numLights + 63 ) / 64, 1, 1 );
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT );
		if ( pc[9] > 0.5f ) {	// each cell's stand-ins (scene_distant.comp), read by the lit pass
			const uint32_t counts[4] = { sc.numCells, 0, 0, 0 };
			qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.distantPipe );
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.distantPipeLayout, 0, 1, &sc.distantSet[f], 0, NULL );
			qvkCmdPushConstants( cb, sc.distantPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( counts ), counts );
			qvkCmdDispatch( cb, ( sc.numCells + 63 ) / 64, 1, 1 );
			Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		}
		sc.distantOn = pc[9] > 0.5f ? qtrue : qfalse;
	}
	// the water's waves (scene_water.comp): rows, then columns, after last frame's lit pass read the image
	if ( sc.wavesPipe && r_sceneWaterWaves->integer ) {
		const double loop = 256.0;	// (w quantised to 2 pi / loop: the clock modulo it is seamless)
		float pc[8] = { sc.wavesSize[0], sc.wavesSize[1], sc.wavesSize[2], 0.0f, (float)fmod( (double)sc.signalTime * 0.001, loop ), (float)( 2.0 * M_PI / loop ), 0.0f, 0.0f };
		Scene_Barrier( VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.wavesPipe );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.wavesPipeLayout, 0, 1, &sc.wavesSet, 0, NULL );
		qvkCmdPushConstants( cb, sc.wavesPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdDispatch( cb, 256, 3, 1 );
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		pc[3] = 1.0f;
		qvkCmdPushConstants( cb, sc.wavesPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdDispatch( cb, 256, 3, 1 );
		// the lower levels, each blitted from the one above (all three layers at once), in GENERAL
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
		for ( uint32_t l = 1; l < sc.wavesLevels; l++ ) {
			VkImageBlit bl;
			Com_Memset( &bl, 0, sizeof( bl ) );
			bl.srcSubresource.aspectMask = bl.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bl.srcSubresource.mipLevel = l - 1;
			bl.dstSubresource.mipLevel = l;
			bl.srcSubresource.layerCount = bl.dstSubresource.layerCount = 3;
			bl.srcOffsets[1].x = bl.srcOffsets[1].y = (int32_t)( 256u >> ( l - 1 ) );
			bl.dstOffsets[1].x = bl.dstOffsets[1].y = (int32_t)( 256u >> l );
			bl.srcOffsets[1].z = bl.dstOffsets[1].z = 1;
			qvkCmdBlitImage( cb, sc.wavesImage, VK_IMAGE_LAYOUT_GENERAL, sc.wavesImage, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR );
			Scene_Barrier( VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
		}
		Scene_Barrier( VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		fr->waterWaves[0] = 1.0f;
		fr->waterWaves[1] = sc.wavesSize[0];
		fr->waterWaves[2] = sc.wavesSize[1];
		fr->waterWaves[3] = sc.wavesSize[2];
		fr->waterWaves2[0] = sc.wavesVar;
		fr->waterWaves2[1] = 256.0f;
	}
	// the haze volumes that may reach the eye's frustum (r_sceneViewCull): first each workgroup's count, then the indices
	// in order; they depend on the eye alone, so before the culls
	if ( sc.viewCullPipe && eyeView >= 0 && sc.fogOn && sc.numFogVolumes ) {
		const qboolean vcTimed = vk_gputime_section_begin( "scene view cull" );
		const sceneView_t *eye = &sc.views[eyeView];
		float pc[20];
		VectorCopy( eye->origin, pc );
		pc[3] = 0.0f;
		VectorCopy( eye->axis[0], pc + 4 );
		pc[7] = (float)sc.numFogVolumes;
		VectorCopy( eye->axis[1], pc + 8 );
		pc[11] = eye->tanHalfFov[0];
		VectorCopy( eye->axis[2], pc + 12 );
		pc[15] = eye->tanHalfFov[1];
		pc[16] = r_sceneViewCull->integer ? 0.0f : 1.0f;	// 1: every one, in order (the list stays whole while it is off)
		pc[18] = pc[19] = 0.0f;
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.viewCullPipe );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.viewCullPipeLayout, 0, 1, &sc.viewCullSet[f], 0, NULL );
		for ( int pass = 0; pass < 2; pass++ ) {
			pc[17] = (float)pass;
			qvkCmdPushConstants( cb, sc.viewCullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
			qvkCmdDispatch( cb, sc.viewCullGroups, 1, 1 );
			Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		}
		vk_gputime_section_end( vcTimed );
	}
	if ( sc.numInteriors && sc.interiorInPtr[f] ) {	// interiors: the groups whose box holds the eye, and those near it
		const float inside = MAX( r_sceneInteriorMargin->value, 0.0f ) * 37.5f, preload = MAX( r_sceneInteriorPreload->value, 0.0f ) * 37.5f;
		const float *e = sc.views[eyeView].origin;
		const uint32_t words = ( sc.numInteriors + 31 ) / 32;
		Com_Memset( sc.interiorInPtr[f], 0, words * 4 );
		Com_Memset( sc.interiorWantPtr[f], 0, words * 4 );
		for ( uint32_t g = 0; g < sc.numInteriors; g++ ) {
			const float *b = &sceneInteriorBox[g * 6];
			const float d = MAX( MAX( MAX( b[0] - e[0], e[0] - b[3] ), MAX( b[1] - e[1], e[1] - b[4] ) ), MAX( b[2] - e[2], e[2] - b[5] ) );
			if ( d <= inside || !r_sceneInteriors->integer )
				sc.interiorInPtr[f][g >> 5] |= 1u << ( g & 31 );
			if ( d <= preload || !r_sceneInteriors->integer )
				sc.interiorWantPtr[f][g >> 5] |= 1u << ( g & 31 );
		}
	}
	if ( tPre > 0.0 )
		vk_cputime_section( VK_SEC_SCENEPRE, R_SunProfNow() - tPre );
	{
		vkSecTimer_t tFaces( VK_SEC_SCENEFACES );
		Scene_RainFrame( cb, f, &sc.views[eyeView] );	// the rain view on the eye (r_sceneRainFollow)
		Scene_StreamFacesFrame( cb, f, sc.views[eyeView].origin );	// streamed geometry's light faces, in this frame's commands
	}
	vkSecTimer_t tCull( VK_SEC_SCENECULL );	// (to the function's end: the culls, the sort, the packing, the clusters)

	qboolean timed = vk_gputime_section_begin( "scene cull" );
	if ( occPhase1 ) {	// the last pyramid written (its frame's commands came first on this queue); the list emptied
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT );
		Scene_DeviceCommands();
		const PFN_vkCmdUpdateBuffer updateBuffer = sc.cmdUpdateBuffer;
		static const uint32_t empty[SCENE_LISTS_HEADER / 4] = { 0 }, noGroups[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
		updateBuffer( cb, sc.occList.buf, 0, 16, empty );
		updateBuffer( cb, sc.occTouched.buf, 0, 16, empty );
		updateBuffer( cb, sc.occArgs.buf, 0, sizeof( noGroups ), noGroups );
		updateBuffer( cb, sc.lists[f][n].buf, 0, SCENE_LISTS_HEADER, empty );	// the phase-2 slot's list counts
	}

	Scene_BenchSkip();	// (r_sceneSkip: a bench switch; nothing unless it changed)
	{ vkSecTimer_t tStream( VK_SEC_SCENESTREAM ); Scene_StreamFrame( cb, f, &sc.views[eyeView] ); }
	{ vkSecTimer_t tRot( VK_SEC_SCENEROTATE ); Scene_RotateFrame( cb, f ); }	// the rotating meshes turned (maps/<map>.xrot)

	// the previous use of these buffers (this slot's last frame) is complete: the fence was waited on
	for ( int v = 0; v < n + ( occPhase1 ? 1 : 0 ); v++ ) {
		VkBufferCopy region;
		region.srcOffset = 0;
		region.dstOffset = 0;
		region.size = cmdBytes;
		qvkCmdCopyBuffer( cb, sc.cmdTemplate.buf, sc.indirect[f][v].buf, 1, &region );
	}
	Scene_CullPoolReset( cb, f );	// the count-then-scan cull's pool (phase 2 appends to it)
	const qboolean gpuGroups = ( r_sceneCullGroups->integer && sc.groupCullPipe && sc.numSubs ) ? qtrue : qfalse;
	if ( gpuGroups ) {	// each view's group list emptied: no groups, then a dispatch of none (scene_groupcull.comp fills them)
		static const uint32_t emptyList[4] = { 0, 0, 1, 1 };
		for ( int v = 0; v < n; v++ )
			sc.cmdUpdateBuffer( cb, sc.cullGroups[f][v].buf, 0, sizeof( emptyList ), emptyList );
	}
	{	// the lit pass's views' LODs to sort emptied: the eye's and the phase-2 slot's (no workgroups, then 1, 1, no count)
		Scene_DeviceCommands();
		const PFN_vkCmdUpdateBuffer updateSort = sc.cmdUpdateBuffer;
		static const uint32_t noSort[4] = { 0, 1, 1, 0 };
		if ( eyeView >= 0 )
			updateSort( cb, sc.sortList[f][eyeView].buf, 0, sizeof( noSort ), noSort );
		if ( occPhase1 )
			updateSort( cb, sc.sortList[f][n].buf, 0, sizeof( noSort ), noSort );
	}
	Scene_Barrier( VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );

	// the near cascades' receivers: the eye's sides (inward normals, planes as the cull reads them), the light's travel
	{
		float *rv = (float *)sc.receiversPtr[f];
		for ( int i = 0; i < 4; i++ )
			Com_Memcpy( rv + i * 4, sc.views[eyeView].planes[i], 16 );
		rv[16] = -sc.sunDir[0];
		rv[17] = -sc.sunDir[1];
		rv[18] = -sc.sunDir[2];
		rv[19] = sc.fadeOn ? 1.0f : 0.0f;	// a far proxy drawn from within its near distance, fading in
		VectorCopy( sc.views[eyeView].origin, rv + 20 );	// every view's hide distances from the eye (r_sceneHideScale)
		rv[23] = MAX( r_sceneHideScale->value, 0.0f );
		rv[24] = (float)Com_Clamp( 0, 2, r_sceneProxyDebug->integer );	// a look check: 1 no far proxy, 2 every one at any distance
		// (scene_cull.comp's rc.look.y: the LOD crossfade's band; only while the depth prepass dithers, sc.fadeOn)
		rv[25] = sc.fadeOn ? Com_Clamp( 0.0f, 2.0f, r_sceneLodFade->value ) : 0.0f;
		// (rc.look.z: the casters' share of the eye's projected size, r_sceneShadowLod: a mesh ending in a billboard
		// card casts its coarsest 3D LOD while the eye's LOD is 3D, Scene_MarkCardMeshes)
		rv[26] = MAX( r_sceneShadowLod->value, 0.01f );
		rv[27] = (float)MAX( r_sceneOnlyMesh->integer, 0 );	// (rc.look.w: a measuring view's one mesh, + 1; 0 all)
	}
	const qboolean receivers = ( r_sceneShadowReceivers->integer && sunOn ) ? qtrue : qfalse;
	// the kept cascades whose static casters come from their cache this frame (RB_RenderFarCascade asks the same, after
	// this, in the same frame): nothing draws their lists, so they are left empty (their commands' counts as copied)
	sc.skippedViews = 0;
	if ( r_sceneKeptCull->integer ) {
		for ( int i = 0; i < backEndData->numSunCascadeCmds; i++ ) {
			const viewParms_t *vp = &backEndData->sunCascadeCmds[i]->viewParms;
			const int sv = vp->sceneView - 1, layer = vp->sunCascade - 1;
			if ( sv >= 0 && sv < n && sv != eyeView && sc.views[sv].cascade == vp->sunCascade && layer >= 0 && layer <= 2
				&& RB_FarCascadeReuse( vp, layer ) )
				sc.skippedViews |= 1u << sv;
		}
	}
	const double tRec = vk_cputime_active() ? R_SunProfNow() : 0.0;	// r_cpuTimes: scenecullrec
	sceneCullPush_t pushes[SCENE_MAX_VIEWS];
	for ( int v = 0; v < n; v++ ) {
		const sceneView_t *view = &sc.views[v];
		if ( sc.skippedViews & ( 1u << v ) )
			continue;
		sceneCullPush_t push;
		Com_Memcpy( push.planes, view->planes, sizeof( push.planes ) );
		VectorCopy( view->origin, push.lod );
		push.lod[3] = MAX( r_sceneLodBias->value, 0.01f ) * view->invTanHalfFovY * view->lodScale;
		push.cull[0] = view->cascade ? 0.0f : MAX( r_sceneCullPixels->value, 0.0f ) / view->viewportHeight / view->invTanHalfFovY;
		push.cull[1] = view->minRadius;
		push.cull[2] = push.cull[3] = 0.0f;
		push.counts[0] = sc.numInstances;
		// the near cascades only: the far one's casters are kept across frames while the view turns (r_sunShadowFarCache),
		// and the middle one's with r_sunShadowMidCache
		push.counts[1] = ( receivers && view->cascade >= 1 && view->cascade <= 2 && !vk_sun_shadow_kept( view->cascade - 1 ) ) ? 1 : 0;
		push.counts[2] = ( occPhase1 && v == eyeView ) ? 1 : 0;
		push.counts[3] = ( occStats ? 1 : 0 ) | ( v == eyeView ? 2 : 0 ) | ( (uint32_t)v << 8 );	// the eye's draws: its LODs to sort listed; the view
		push.cull[2] = push.counts[1] ? 6.0f * sc.sunTexel[view->cascade - 1] : 0.0f;	// (the world shaders' PCF reaches ~4.4 texels)
		push.cull[3] = v == eyeView ? 1.0f : 0.0f;	// the view the hide fade dithers (scene.vert's eye.w): its proxies from a band early
		// the groups that may hold something the view draws (r_sceneCullGroups: scene_groupcull.comp, below): the cull runs
		// over theirs alone
		if ( gpuGroups )
			push.counts[3] |= 4;
		sceneCullDbg[f][v].cascade = view->cascade;
		sceneCullDbg[f][v].eye = v == eyeView;
		pushes[v] = push;
	}
	if ( gpuGroups ) {	// every view's groups listed, then every view culled over its list
		Scene_DeviceCommands();
		const PFN_vkCmdDispatchIndirect dispatchIndirect = sc.cmdDispatchIndirect;
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.groupCullPipe );
		for ( int v = 0; v < n; v++ ) {
			if ( sc.skippedViews & ( 1u << v ) )
				continue;
			sceneCullPush_t gp = pushes[v];
			gp.counts[0] = sc.numSubs;
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.groupCullPipeLayout, 0, 1, &sc.groupCullSet[f][v], 0, NULL );
			qvkCmdPushConstants( cb, sc.groupCullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( gp ), &gp );
			qvkCmdDispatch( cb, ( sc.numSubs + 63 ) / 64, 1, 1 );
		}
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cull );
		for ( int v = 0; v < n; v++ ) {
			if ( sc.skippedViews & ( 1u << v ) )
				continue;
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cullPipeLayout, 0, 1, &sc.cullSet[f][v], 0, NULL );
			qvkCmdPushConstants( cb, sc.cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushes[v] ), &pushes[v] );
			dispatchIndirect( cb, sc.cullGroups[f][v].buf, 4 );	// (the list's dispatch: its 2nd to 4th words)
		}
	} else {
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cull );
		for ( int v = 0; v < n; v++ ) {
			if ( sc.skippedViews & ( 1u << v ) )
				continue;
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cullPipeLayout, 0, 1, &sc.cullSet[f][v], 0, NULL );
			qvkCmdPushConstants( cb, sc.cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushes[v] ), &pushes[v] );
			qvkCmdDispatch( cb, ( sc.numInstances + 63 ) / 64, 1, 1 );
		}
	}
	if ( tRec > 0.0 )
		vk_cputime_section( VK_SEC_SCENECULLREC, R_SunProfNow() - tRec );
	{	// the views' ranges allocated and their instances placed (the views the kept cull left out included: nothing there)
		uint32_t culled = 0;
		for ( int v = 0; v < n; v++ )
			if ( !( sc.skippedViews & ( 1u << v ) ) )
				culled |= 1u << v;
		Scene_CullPoolFinish( cb, f, culled );
	}
	// GPU-driven draws: each view's commands with instances packed into its lists (the phase-2 slot's after phase 2)
	sc.gdFrame = gdFrame;
	vk_gputime_section_end( timed );
	if ( eyeView >= 0 ) {	// the eye's visible list in instance order (scene_sort.comp: a workgroup a listed LOD)
		timed = vk_gputime_section_begin( "scene sort" );
		Scene_SortVisible( cb, f, eyeView );
		vk_gputime_section_end( timed );
	}
	vkSecTimer_t tPack( VK_SEC_SCENEPACK );
	timed = vk_gputime_section_begin( "scene pack, clusters" );
	if ( sc.gdFrame ) {
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		uint32_t pc[12];
		Com_Memset( pc, 0, sizeof( pc ) );
		pc[0] = sc.numCmds;
		Scene_CompactBases( pc );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.compact );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.compactPipeLayout, 0, 1, &sc.compactSet[f][0], 0, NULL );
		if ( r_sceneCompactBlocks->integer && sc.compactBlocks ) {
			// the block-parallel packing: count, scan, write (scene_compact.comp's modes 2-4), a workgroup per 1024
			// commands and view, the same lists as the one below
			pc[9] = sc.compactBlocks;
			pc[11] = sc.skippedViews;	// (r_sceneKeptCull)
			for ( uint32_t mode = 2; mode <= 4; mode++ ) {
				pc[2] = mode;
				qvkCmdPushConstants( cb, sc.compactPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
				if ( mode == 3 )
					qvkCmdDispatch( cb, n, 1, 1 );
				else
					qvkCmdDispatch( cb, sc.compactBlocks, n, 1 );
				if ( mode < 4 )
					Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
			}
		} else {
			qvkCmdPushConstants( cb, sc.compactPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
			qvkCmdDispatch( cb, n, 1, 1 );
		}
	}
	// the clusters need no cull: after the barrier they run beside the packing
	if ( clusters ) {
		const sceneView_t *eye = &sc.views[eyeView];
		float pc[20];
		VectorCopy( eye->origin, pc );
		pc[3] = (float)sc.numLights;
		VectorCopy( eye->axis[0], pc + 4 );
		pc[7] = SCENE_FOG_NEAR;
		VectorCopy( eye->axis[1], pc + 8 );
		pc[11] = eye->tanHalfFov[0];
		VectorCopy( eye->axis[2], pc + 12 );
		pc[15] = eye->tanHalfFov[1];
		pc[16] = clusterLog;
		pc[17] = r_sceneClusterStats->integer > 0 ? 1.0f : 0.0f;	// count the fallbacks
		pc[18] = r_sceneClusterCull->integer ? 1.0f : 0.0f;
		pc[19] = 0.0f;
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.clusterPipe );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.clusterPipeLayout, 0, 1, &sc.clusterSet[f], 0, NULL );
		if ( r_sceneClusterCull->integer ) {
			// the lights live and in the frustum, in order (scene_cluster.comp's modes 1 and 2), which the tiles walk
			const uint32_t groups = ( MAX( sc.numLights, 1u ) + 63 ) / 64;
			for ( int mode = 1; mode <= 2; mode++ ) {
				pc[19] = (float)mode;
				qvkCmdPushConstants( cb, sc.clusterPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
				qvkCmdDispatch( cb, groups, 1, 1 );
				Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );
			}
			pc[19] = 0.0f;
		}
		qvkCmdPushConstants( cb, sc.clusterPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdDispatch( cb, SCENE_CLUSTER_X, SCENE_CLUSTER_Y, sc.fogOn ? 2 : 1 );	// the haze's in the second z
	}
	Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
		| VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT );
	sc.culledViews = n;
	vk_gputime_section_end( timed );

	sc.hazePending = qtrue;
}

// the haze's light, after the sun cascades are drawn (vk_begin_frame, before the main render pass): the moon's light in
// it reads them. The volume to GENERAL (after the last frame's reads; its contents are rewritten), lit, then
// integrated toward the eye. Left in GENERAL unwritten when off (scene.frag does not read it then).
// what the far sun cascade's cached casters depend on besides its view (RB_RenderFarCascade): the scene and its shadow
// settings
// whether this frame's cull left view sceneView (viewParms_t's, 1-based) out (r_sceneKeptCull)
qboolean vk_scene_view_skipped( int sceneView )
{
	const int v = sceneView - 1;
	return ( sc.loaded && v >= 0 && v < sc.culledViews && ( sc.skippedViews & ( 1u << v ) ) ) ? qtrue : qfalse;
}

uint32_t vk_scene_shadow_key( void )
{
	uint32_t h = 2166136261u;
	const float v[] = { (float)sc.loaded, (float)sc.numInstances, (float)r_scene->integer, (float)r_sceneShadows->integer,
		(float)r_sceneShadowCascades->integer, r_sceneShadowLod->value, r_sceneShadowCull->value, r_sceneLodBias->value,
		r_sceneHideScale->value };
	const byte *b = (const byte *)v;
	for ( size_t i = 0; i < sizeof( v ); i++ )
		h = ( h ^ b[i] ) * 16777619u;
	return h;
}

/*
Characters lit by the scene (r_sceneCharLight; tr_light.cpp's R_SetupEntityLighting, Ghoul2 models): the light at P in
the light grid's units (0..255, the display's encoding), in place of the BSP's grid (q3map2's, near white everywhere on
a converted map). The scene's ambient (its hemisphere's mean) and every light reaching P's cell (scene.frag's falloff,
cone, interior box and light scale; unshadowed; lights only in the haze left out), gathered into one direction: what
spreads past it goes to the ambient. Both go through the scene's exposure and tone curve, so a white diffuse surface
there shows as the scene would show it; r_sceneCharLight scales the result. qfalse without a scene (the grid then).
*/
/*
r_sceneMoonLight: at night (no day environment) the map's sun is the moon (its shadow cascades, the haze's moonlight,
r_sceneFogMoon): it comes from the moon's disc (r_sceneMoonPath at r_sceneClock) while that stands 6 degrees up or more,
else from the map's q3gl2_sun as before. tr_scene.cpp, each scene: refdef's sun direction (models keep tr.sunDirection).
*/
void R_SceneSunDir( vec4_t sunDir )
{
	if ( !sc.loaded || !r_scene->integer || sceneEnvDay || !Env( r_sceneMoonLight )->integer )
		return;
	vec3_t dir;
	vec4_t col;
	Scene_MoonAt( Scene_Clock(), dir, col );
	if ( dir[2] >= 0.1f )
		VectorCopy( dir, sunDir );
}

/*
r_sceneCharSun: the sun the generic shader adds to models (r_sunlightMode 2, tr_scene.cpp's refdef.sunCol) as the scene
shows it, the way R_SceneLightPoint gives them its lights: the scene's ambient (its hemisphere's mean) with and
without the sun's light on a white diffuse surface (its hue, r_sceneSun, Lambert's 1 / pi), through the exposure and
tone curve, the difference the sun's share; times pi (the generic shader scales its grid light by pi before the same
diffuse, not the sun), r_sceneCharLight and r_sceneCharSun. qfalse: the BSP's sun as before.
*/
qboolean R_SceneCharSun( vec3_t col )
{
	if ( !sc.loaded || !r_scene->integer || r_sunlightMode->integer != 2 || Env( r_sceneCharSun )->value <= 0.0f
		|| r_sceneCharLight->value <= 0.0f )
		return qfalse;
	const float m = MAX( tr.sunLight[0], MAX( tr.sunLight[1], tr.sunLight[2] ) );
	if ( m <= 0.0f )
		return qfalse;
	vec3_t sky;
	VectorClear( sky );
	sscanf( Env( r_sceneAmbient )->string, "%f %f %f", &sky[0], &sky[1], &sky[2] );
	const float exposure = Scene_Exposure();
	const qboolean aces = Env( r_sceneTonemap )->integer ? qtrue : qfalse;
	auto display = [exposure, aces]( float v ) {	// (R_SceneLightPoint's)
		v = MAX( v * exposure, 0.0f );
		v = aces ? Com_Clamp( 0.0f, 1.0f, ( v * ( 2.51f * v + 0.03f ) ) / ( v * ( 2.43f * v + 0.59f ) + 0.14f ) ) : 1.0f - expf( -v );
		return powf( v, 1.0f / 2.2f );
	};
	const float strength = MAX( Env( r_sceneSun )->value, 0.0f ) / (float)M_PI;
	const float s = (float)M_PI * r_sceneCharLight->value * Env( r_sceneCharSun )->value;
	for ( int k = 0; k < 3; k++ ) {
		const float amb = sky[k] * 0.5f * ( 1.0f + 0.35f );
		col[k] = s * MAX( display( amb + tr.sunLight[k] / m * strength ) - display( amb ), 0.0f );
	}
	return qtrue;
}

qboolean R_SceneLightPoint( const vec3_t P, vec3_t ambient, vec3_t directed, vec3_t lightDir )
{
	if ( !sc.loaded || !r_scene->integer || r_sceneCharLight->value <= 0.0f || sceneCharLights.empty() || sceneCharCells.empty() )
		return qfalse;
	vec3_t sky, amb, sum, dirSum;
	VectorClear( sky );
	sscanf( Env( r_sceneAmbient )->string, "%f %f %f", &sky[0], &sky[1], &sky[2] );
	VectorScale( sky, 0.5f * ( 1.0f + 0.35f ), amb );	// the mean of the sky's and the ground's (scene.frag's hemi)
	VectorClear( sum );
	VectorClear( dirSum );
	float total = 0.0f;
	int c[3];
	qboolean inside = qtrue;
	for ( int k = 0; k < 3; k++ ) {
		c[k] = (int)floorf( ( P[k] - sc.gridOrigin[k] ) / sc.gridOrigin[3] );
		if ( c[k] < 0 || c[k] >= sc.gridDims[k] )
			inside = qfalse;
	}
	if ( inside ) {
		const size_t cell = ( (size_t)c[2] * sc.gridDims[1] + c[1] ) * sc.gridDims[0] + c[0];
		const uint32_t first = sceneCharCells[cell * 2], count = sceneCharCells[cell * 2 + 1];
		const float scale = Env( r_sceneLightScale )->value / (float)M_PI;	// scene.frag's Lambert: diffuse / pi
		for ( uint32_t i = 0; i < count; i++ ) {
			const uint32_t li = sceneCharRefs[first + i];
			const xscnLight_t *L = &sceneCharLights[li];
			const int type = (int)L->type;
			if ( type & 2 )
				continue;
			vec3_t Lv;
			VectorSubtract( L->origin, P, Lv );
			const float d2 = DotProduct( Lv, Lv );
			if ( d2 >= L->radius * L->radius )
				continue;
			if ( L->boxed != 0.0f ) {	// an interior light: only inside its volume
				const float *B = &sceneCharBoxes[(size_t)li * 16];
				float m = 0.0f;
				for ( int r = 0; r < 3; r++ )
					m = MAX( m, fabsf( B[r * 4] * P[0] + B[r * 4 + 1] * P[1] + B[r * 4 + 2] * P[2] + B[r * 4 + 3] ) );
				if ( m > 1.0f )
					continue;
			}
			// a tube (version 6): its point nearest P stands for it, its falloff a line source's (scene.frag tubeFalloff)
			float reach = L->radius, line = 0.0f;
			if ( !sceneCharShapes.empty() ) {
				const xscnShape_t *S = &sceneCharShapes[li];
				const float a2 = DotProduct( S->axis, S->axis );
				if ( a2 > 0.0f ) {
					const float a = sqrtf( a2 );
					vec3_t rel;
					VectorSubtract( P, L->origin, rel );
					const float along = DotProduct( rel, S->axis ) / a;
					const float t = Com_Clamp( -a, a, along );
					VectorMA( L->origin, t / a, S->axis, Lv );
					VectorSubtract( Lv, P, Lv );
					const float s = along / 37.5f, hl = a / 37.5f, r = S->sourceRadius / 37.5f;
					const float h2 = MAX( DotProduct( rel, rel ) / ( 37.5f * 37.5f ) - s * s, 0.0f ) + MAX( 1.0f, r * r ), h = sqrtf( h2 );
					line = ( atanf( ( hl - s ) / h ) + atanf( ( hl + s ) / h ) ) / ( 2.0f * hl * h );
					reach -= a;
				}
			}
			const float d = VectorLength( Lv );
			vec3_t l;
			VectorScale( Lv, 1.0f / MAX( d, 1e-3f ), l );
			const float x = d / MAX( reach, 1.0f );
			float att = Com_Clamp( 0.0f, 1.0f, 1.0f - x * x * x * x );
			const float dm = d / 37.5f;
			att = att * att * ( line > 0.0f ? line : 1.0f / ( dm * dm + 1.0f ) );
			if ( type & 1 ) {	// a cone: smoothstep from its outer to its inner cosine
				const float t = Com_Clamp( 0.0f, 1.0f, ( -DotProduct( l, L->dir ) - L->cosOuter ) / MAX( L->cosInner - L->cosOuter, 1e-4f ) );
				att *= t * t * ( 3.0f - 2.0f * t );
			}
			if ( att <= 0.0f )
				continue;
			const float e = L->power * att * scale;
			const float w = e * ( 0.2126f * L->color[0] + 0.7152f * L->color[1] + 0.0722f * L->color[2] );
			VectorMA( sum, e, L->color, sum );
			VectorMA( dirSum, w, l, dirSum );
			total += w;
		}
	}
	// one direction: the share of the light along the weighted mean; the rest lights the character all round
	const float len = VectorLength( dirSum );
	const float coherent = total > 0.0f ? Com_Clamp( 0.0f, 1.0f, len / total ) : 0.0f;
	vec3_t dirLight;
	VectorScale( sum, coherent, dirLight );
	VectorMA( amb, 0.5f * ( 1.0f - coherent ), sum, amb );
	if ( len > 0.0f )
		VectorScale( dirSum, 1.0f / len, lightDir );
	else
		VectorSet( lightDir, 0.0f, 0.0f, 1.0f );
	// the display's encoding (scene.frag's toDisplay below its top): exposure, tone curve, gamma
	const float exposure = Scene_Exposure();
	const qboolean aces = Env( r_sceneTonemap )->integer ? qtrue : qfalse;
	auto display = [exposure, aces]( float v ) {
		v = MAX( v * exposure, 0.0f );
		v = aces ? Com_Clamp( 0.0f, 1.0f, ( v * ( 2.51f * v + 0.03f ) ) / ( v * ( 2.43f * v + 0.59f ) + 0.14f ) ) : 1.0f - expf( -v );
		return powf( v, 1.0f / 2.2f );
	};
	const float s = 255.0f * r_sceneCharLight->value;
	for ( int k = 0; k < 3; k++ ) {
		const float a = display( amb[k] );
		ambient[k] = s * a;
		directed[k] = s * MAX( display( amb[k] + dirLight[k] ) - a, 0.0f );
	}
	return qtrue;
}

static int Scene_Pipe( uint32_t kind, int pass )
{
	if ( kind == SCENE_KIND_NONE )
		return -1;	// (flag 0x2000: not drawn)
	if ( kind == SCENE_KIND_DECAL )
		return -1;	// the DBuffer's (Scene_DrawDecals)
	if ( kind == SCENE_KIND_WATER )
		return pass == SCENE_PASS_BLEND ? SCENE_PIPE_WATER : -1;	// (vk_scene_draw: before the blends)
	if ( pass == SCENE_PASS_SHADOW ) {
		if ( kind == SCENE_KIND_BLEND )
			return -1;
		return kind == SCENE_KIND_CUTOUT ? SCENE_PIPE_SHADOW_CUTOUT : SCENE_PIPE_SHADOW;
	}
	if ( pass == SCENE_PASS_DEPTH ) {
		if ( kind == SCENE_KIND_BLEND )
			return -1;
		return kind == SCENE_KIND_CUTOUT ? SCENE_PIPE_DEPTH_CUTOUT : SCENE_PIPE_DEPTH;
	}
	if ( pass == SCENE_PASS_BLEND )
		return kind == SCENE_KIND_BLEND ? SCENE_PIPE_BLEND : -1;
	if ( pass == SCENE_PASS_GLOW )
		return kind == SCENE_KIND_EMISSIVE ? SCENE_PIPE_GLOW : -1;
	switch ( kind ) {
	case SCENE_KIND_CUTOUT:		return SCENE_PIPE_CUTOUT;
	case SCENE_KIND_EMISSIVE:	return SCENE_PIPE_EMISSIVE;
	case SCENE_KIND_BLEND:		return -1;
	default:					return SCENE_PIPE_OPAQUE;
	}
}

// r_sceneInvocations (bench): a pipeline statistics query around the eye view's lit pass, read back when its slot
// comes round again (its fence waited)

static void Scene_InvocationsFrame( VkCommandBuffer cb, int f )
{
	if ( !r_sceneInvocations->integer ) {
		invRecorded[f] = qfalse;
		return;
	}
	if ( !vk.pipelineStatistics )
		return;
	if ( !invPool[f] ) {
		VkQueryPoolCreateInfo qi;
		Com_Memset( &qi, 0, sizeof( qi ) );
		qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		qi.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		qi.queryCount = 1;
		qi.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT
			| VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
		PFN_vkCreateQueryPool create = (PFN_vkCreateQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCreateQueryPool" );
		if ( !create || create( vk.device, &qi, NULL, &invPool[f] ) != VK_SUCCESS )
			return;
	}
	if ( invRecorded[f] ) {
		uint64_t r[3];
		PFN_vkGetQueryPoolResults get = (PFN_vkGetQueryPoolResults)qvkGetDeviceProcAddr( vk.device, "vkGetQueryPoolResults" );
		if ( get && get( vk.device, invPool[f], 0, 1, sizeof( r ), r, sizeof( r ), VK_QUERY_RESULT_64_BIT ) == VK_SUCCESS )
			ri.Printf( PRINT_ALL, "SCENEINV vs %llu prims %llu fs %llu pixels %d\n", (unsigned long long)r[0], (unsigned long long)r[1],
				(unsigned long long)r[2], glConfig.vidWidth * glConfig.vidHeight );
	}
	invRecorded[f] = qfalse;
	PFN_vkCmdResetQueryPool reset = (PFN_vkCmdResetQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCmdResetQueryPool" );
	if ( reset )
		reset( cb, invPool[f], 0, 1 );
}

// GPU-driven draws: the pass's lists of view v (packed this frame), one call each; push holds the view's matrix and eye.
// The vertex and index buffers and the viewport are set.
static void Scene_DrawLists( int pass, int v, scenePush_t *push )
{
	const int f = vk.cmd_index;
	VkCommandBuffer cb = vk.cmd->command_buffer;
	int lists[2], pipes[2], count = 0;

	switch ( pass ) {
	case SCENE_PASS_SHADOW:
		lists[0] = SCENE_LIST_DEPTH; pipes[0] = SCENE_GD_SHADOW;
		lists[1] = SCENE_LIST_DEPTH_CUTOUT; pipes[1] = SCENE_GD_SHADOW_CUTOUT;
		count = 2;
		break;
	case SCENE_PASS_DEPTH:
		lists[0] = SCENE_LIST_DEPTH; pipes[0] = SCENE_GD_DEPTH;
		lists[1] = SCENE_LIST_DEPTH_CUTOUT; pipes[1] = SCENE_GD_DEPTH_CUTOUT;
		count = 2;
		break;
	case SCENE_PASS_GLOW:
		lists[0] = SCENE_LIST_GLOW; pipes[0] = SCENE_GD_GLOW;
		count = 1;
		break;
	case SCENE_PASS_BLEND:
		// the water over its bed first, then the blends (glass over the water)
		lists[0] = SCENE_LIST_WATER; pipes[0] = SCENE_GD_WATER;
		lists[1] = SCENE_LIST_BLEND; pipes[1] = SCENE_GD_BLEND;
		count = 2;
		if ( ( sc.fogOn || sc.skyOn ) && v == sc.fogEyeView ) {
			// the haze's light over the sky first (the per-material path's sky triangle); r_sceneSky: the sky itself
			VkViewport atFar, viewport;
			get_viewport( &viewport, DEPTH_RANGE_NORMAL );
			get_viewport( &atFar, DEPTH_RANGE_ONE );
			qvkCmdSetViewport( cb, 0, 1, &atFar );
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 0, 1, &sc.drawSet[f][v], 0, NULL );
			qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.pipe[SCENE_PIPE_SKY] );
			// r_sceneMoon's image as its albedo
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 1, 1, sceneMoonImage ? &sceneMoonImage->descriptor_set : &tr.whiteImage->descriptor_set, 0, NULL );
			scenePush_t sky;
			Com_Memset( &sky, 0, sizeof( sky ) );
			VectorCopy( sc.views[v].origin, sky.eye );
			sky.eye[3] = 1.0f;
			qvkCmdPushConstants( cb, sc.drawPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( sky ), &sky );
			qvkCmdDraw( cb, 3, 1, 0, 0 );
			qvkCmdSetViewport( cb, 0, 1, &viewport );
		}
		break;
	default:
		lists[0] = SCENE_LIST_LIT;
		pipes[0] = r_sceneDebug->integer ? SCENE_GD_LIT_DEBUG : ( r_sceneLitEqual->integer && sc.gdPipe[SCENE_GD_LIT_EQUAL] ) ? SCENE_GD_LIT_EQUAL : SCENE_GD_LIT;
		count = 1;
		break;
	}
	VkDescriptorSet sets[3] = { sc.drawSet[f][v], sc.texSet, vk.sunShadow.descriptor };
	// the cascades, read by the lit modes (not while the cascade pass writes them)
	const uint32_t numSets = ( pass != SCENE_PASS_SHADOW && vk.sunShadow.descriptor ) ? 3 : 2;
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.gdPipeLayout, 0, numSets, sets, 0, NULL );
	for ( int i = 0; i < count; i++ ) {
		const int k = lists[i];
		VkPipeline gp = ( pipes[i] == SCENE_GD_DEPTH && sc.fadeOn ) ? sc.gdPipeDepthFade : sc.gdPipe[pipes[i]];
		if ( sc.fadeOn && sc.hideDither == 2 && ( pipes[i] == SCENE_GD_DEPTH || pipes[i] == SCENE_GD_DEPTH_CUTOUT ) )
			gp = sc.gdPipeFadeMask[pipes[i] == SCENE_GD_DEPTH ? 0 : 1];
		if ( sc.cutoutCov && pipes[i] == SCENE_GD_DEPTH_CUTOUT )
			gp = sc.gdPipeFadeMask[1];	// (r_sceneCutoutCoverage: the cutouts' depth at their covered samples)
		if ( sc.cutoutCov && pipes[i] == SCENE_GD_LIT_EQUAL )
			gp = sc.gdPipeLitCov;
		if ( !sc.listCap[k] || !gp )
			continue;
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp );
		push->info[0] = sc.listBase[k];
		qvkCmdPushConstants( cb, sc.gdPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( *push ), push );
		qvkCmdDrawIndexedIndirectCount( cb, sc.lists[f][v].buf, SCENE_LISTS_HEADER + (VkDeviceSize)sc.listBase[k] * sizeof( VkDrawIndexedIndirectCommand ),
			sc.lists[f][v].buf, (VkDeviceSize)k * 4, sc.listCap[k], sizeof( VkDrawIndexedIndirectCommand ) );
	}
}

// RB_DrawSurfs: the view's scene in the main render pass (SCENE_PASS_DEPTH in the depth prepass, SCENE_PASS_GLOW in the
// dynamic glow's extract pass: emissive materials only, against the main pass's depth)

void vk_scene_draw( int pass )
{
	vkSecTimer_t tSec( VK_SEC_SCENEDRAW );	// r_cpuTimes
	const int v = backEnd.viewParms.sceneView - 1;
	const int f = vk.cmd_index;

	if ( !sc.loaded || v < 0 || v >= sc.culledViews || !r_scene->integer )
		return;
	if ( pass == SCENE_PASS_SHADOW ) {
		if ( vk.renderPassIndex != RENDER_PASS_SUN_SHADOW || !backEnd.viewParms.sunCascade || !sc.shadowPipes )
			return;
	} else if ( pass == SCENE_PASS_GLOW ) {
		if ( vk.renderPassIndex != RENDER_PASS_DGLOW || !backEnd.isGlowPass || !sc.numEmissive || Env( r_sceneGlow )->value <= 0.0f )
			return;
	} else {
		if ( vk.renderPassIndex != RENDER_PASS_MAIN && vk.renderPassIndex != RENDER_PASS_DEPTH_PREPASS )
			return;
		if ( backEnd.isGlowPass || backEnd.refractionFill )
			return;
		// the emissive materials glow: RB_DrawSurfs runs the glow pass when a view has glowing surfaces
		if ( pass == SCENE_PASS_OPAQUE && vk.dglowActive && sc.numEmissive && Env( r_sceneGlow )->value > 0.0f )
			backEnd.hasGlowSurfaces = qtrue;
	}

	// r_gpuTimes: the scene's share of the main render pass under its own names
	vk_gputime_split( pass == SCENE_PASS_DEPTH ? "scene depth" : pass == SCENE_PASS_BLEND ? "scene blend" : pass == SCENE_PASS_GLOW ? "scene glow"
		: pass == SCENE_PASS_SHADOW ? "scene shadow" : "scene" );

	VkCommandBuffer cb = vk.cmd->command_buffer;
	scenePush_t push;
	VkViewport viewport;
	VkRect2D scissor;
	float proj[16];

	Com_Memset( &push, 0, sizeof( push ) );	// (the GPU-driven draws push fields their shaders do not read)
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	// (the eye's view only: this draws the sun cascades too, whose modelview carries a texel-snapped translation that
	// is not -R origin, so dropping it moved their shadows: the far-south ground went dark)
	const qboolean rel = ( v == sc.fogEyeView && r_sceneEyeRelative->integer ) ? qtrue : qfalse;
	Scene_EyeViewProj( push.viewProj, proj, rel );
	VectorCopy( sc.views[v].origin, push.eye );
	push.eye[3] = v == sc.fogEyeView ? ( rel ? 2.0f : 1.0f ) : 0.0f;
	get_viewport( &viewport, DEPTH_RANGE_NORMAL );
	get_scissor_rect( &scissor );
	qvkCmdSetViewport( cb, 0, 1, &viewport );
	qvkCmdSetScissor( cb, 0, 1, &scissor );

	const VkDeviceSize zero = 0;
	qvkCmdBindVertexBuffers( cb, 0, 1, &sc.vertices.buf, &zero );
	qvkCmdBindIndexBuffer( cb, sc.indices.buf, 0, VK_INDEX_TYPE_UINT32 );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 0, 1, &sc.drawSet[f][v], 0, NULL );
	// the cascades, read by the lit modes (not while the cascade pass writes them)
	if ( pass != SCENE_PASS_SHADOW && vk.sunShadow.descriptor )
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 4, 1, &vk.sunShadow.descriptor, 0, NULL );

	// r_sceneVrs (metres: terrain, leaves, the rest; 0 never): scene.vert's VRS module reads the distances, in units, from
	// the GPU-driven draws' tint (their materials' own come from the materials buffer)
	if ( sc.gdFrame )
		push.tint[3] = r_sceneTerrainFlat->integer ? -1.0f : 0.0f;	// (bench: scene.frag's terrain in one colour)
	if ( vk.shadingRate && sc.gdFrame ) {
		float d[3] = { 0.0f, 0.0f, 0.0f };
		sscanf( r_sceneVrs->string, "%f %f %f", &d[0], &d[1], &d[2] );
		for ( int i = 0; i < 3; i++ )
			push.tint[i] = d[i] > 0.0f ? d[i] * 37.5f : 0.0f;
	}
	if ( sc.gdFrame ) {
		// occlusion: the phase-2 slot's lists with the eye view's (after phase 2; alone for the prepass's second half)
		const qboolean slot = ( sc.occDrawn && v == sc.occEye && sc.occSlot >= 0 && pass != SCENE_PASS_SHADOW ) ? qtrue : qfalse;
		// r_sceneInvocations: the eye's lit pass, its own lists and phase 2's together
		const qboolean inv = ( pass == SCENE_PASS_OPAQUE && v == sc.fogEyeView && invPool[f] && r_sceneInvocations->integer && !invRecorded[f] ) ? qtrue : qfalse;
		Scene_DeviceCommands();
		const PFN_vkCmdBeginQuery beginQuery = sc.cmdBeginQuery;
		const PFN_vkCmdEndQuery endQuery = sc.cmdEndQuery;
		if ( inv && beginQuery )
			beginQuery( cb, invPool[f], 0, 0 );
		if ( !sc.occOnly )
			Scene_DrawLists( pass, v, &push );
		if ( slot )
			Scene_DrawLists( pass, sc.occSlot, &push );
		if ( inv && endQuery ) {
			endQuery( cb, invPool[f], 0 );
			invRecorded[f] = qtrue;
		}
		if ( pass == SCENE_PASS_BLEND )
			Scene_DrawFx( cb, f, v, &push );	// (r_sceneFx)
		vk.cmd->curr_index_buffer = VK_NULL_HANDLE;
		vk_restore_draw_state( qfalse );
		vk_gputime_split( pass == SCENE_PASS_GLOW ? "glow after scene" : pass == SCENE_PASS_SHADOW ? "cascade after scene" : "main after scene" );
		return;
	}

	int bound = -1;
	if ( pass == SCENE_PASS_BLEND && ( sc.fogOn || sc.skyOn ) && v == sc.fogEyeView ) {
		// the haze's light over the sky: one triangle at the far plane (depth 1: only where the sky or nothing is);
		// r_sceneSky: the sky itself
		VkViewport atFar;
		get_viewport( &atFar, DEPTH_RANGE_ONE );
		qvkCmdSetViewport( cb, 0, 1, &atFar );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.pipe[SCENE_PIPE_SKY] );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 1, 1, sceneMoonImage ? &sceneMoonImage->descriptor_set : &tr.whiteImage->descriptor_set, 0, NULL );	// (r_sceneMoon's image)
		Com_Memset( &push, 0, sizeof( push ) );
		VectorCopy( sc.views[v].origin, push.eye );
		push.eye[3] = 1.0f;
		qvkCmdPushConstants( cb, sc.drawPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
		qvkCmdDraw( cb, 3, 1, 0, 0 );
		qvkCmdSetViewport( cb, 0, 1, &viewport );
		bound = SCENE_PIPE_SKY;
		Scene_EyeViewProj( push.viewProj, proj, rel );	// the materials below need it again
		VectorCopy( sc.views[v].origin, push.eye );
		push.eye[3] = rel ? 2.0f : 1.0f;
	}
	// the blend pass: the water first (over its bed), then the blends
	const int rounds = ( pass == SCENE_PASS_BLEND && sc.numWater ) ? 2 : 1;
	for ( uint32_t n = 0; n < sc.numMaterials * rounds; n++ ) {
		const uint32_t i = n % sc.numMaterials;
		const sceneMaterial_t *m = &sc.materials[i];
		if ( rounds == 2 && ( m->kind == SCENE_KIND_WATER ) != ( n < sc.numMaterials ) )
			continue;
		const int pipe = m->numCmds ? Scene_Pipe( m->kind, pass ) : -1;
		if ( pipe < 0 )
			continue;
		if ( pipe != bound ) {
			qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ( pipe == SCENE_PIPE_DEPTH && sc.fadeOn ) ? sc.pipeDepthFade : sc.pipe[pipe] );
			bound = pipe;
		}
		if ( pipe != SCENE_PIPE_DEPTH && pipe != SCENE_PIPE_SHADOW ) {
			VkDescriptorSet sets[3] = { m->image[0]->descriptor_set, m->image[1]->descriptor_set, m->image[2]->descriptor_set };
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 1, 3, sets, 0, NULL );
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.drawPipeLayout, 5, 1, &m->detail->descriptor_set, 0, NULL );
		}
		Com_Memcpy( push.emissive, m->emissive, sizeof( push.emissive ) );
		Com_Memcpy( push.tint, m->tint, sizeof( push.tint ) );
		push.info[0] = m->kind;
		push.info[1] = m->imageBits;
		push.info[2] = i;	// r_sceneDebug 8 shows it
		push.info[3] = m->window;
		qvkCmdPushConstants( cb, sc.drawPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );

		const VkDeviceSize at = (VkDeviceSize)m->firstCmd * sizeof( VkDrawIndexedIndirectCommand );
		if ( vk.multiDrawIndirect )
			qvkCmdDrawIndexedIndirect( cb, sc.indirect[f][v].buf, at, m->numCmds, sizeof( VkDrawIndexedIndirectCommand ) );
		else
			for ( uint32_t c = 0; c < m->numCmds; c++ )
				qvkCmdDrawIndexedIndirect( cb, sc.indirect[f][v].buf, at + c * sizeof( VkDrawIndexedIndirectCommand ), 1, sizeof( VkDrawIndexedIndirectCommand ) );
	}

	if ( pass == SCENE_PASS_BLEND )
		Scene_DrawFx( cb, f, v, &push );	// (r_sceneFx)
	// the engine's cached binds are stale now
	vk.cmd->curr_index_buffer = VK_NULL_HANDLE;
	vk_restore_draw_state( qfalse );
	vk_gputime_split( pass == SCENE_PASS_GLOW ? "glow after scene" : pass == SCENE_PASS_SHADOW ? "cascade after scene" : "main after scene" );
}

// view v's visible list in instance order (scene_sort.comp), after its cull and before its draws: the cull's writes
// seen (the LODs it listed sized the dispatch), the sorted list seen by the packing and the draws
static void Scene_SortVisible( VkCommandBuffer cb, int f, int v )
{
	Scene_DeviceCommands();
	const PFN_vkCmdDispatchIndirect dispatchIndirect = sc.cmdDispatchIndirect;
	Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.sortPipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.sortPipeLayout, 0, 1, &sc.sortSet[f][v], 0, NULL );
	dispatchIndirect( cb, sc.sortList[f][v].buf, 0 );
	Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
}

// mesh decals: the eye view (v) once a frame, in the main pass right after its depth prepass, when the scene has them
static qboolean Scene_DecalsWanted( int v )
{
	return ( sc.loaded && ( sc.numDecals || scenePd.count ) && sc.decalPipe && !sc.decalDone && v >= 0 && v == sc.fogEyeView && v < sc.culledViews
		&& r_scene->integer && vk.renderPassIndex == RENDER_PASS_MAIN && sc.decalW == (uint32_t)glConfig.vidWidth
		&& sc.decalH == (uint32_t)glConfig.vidHeight
		&& vk.render_pass.main_reload.handle && vk.mainPassKeeps ) ? qtrue : qfalse;	// (the split resumes in main_reload)
}

// the decal pass (outside the main pass, its depth attachment readable by fragment shaders): view v's culled decal
// commands, and phase 2's (slot, when it ran) into the DBuffer, which the lit pass then reads (the frame's detail.y)
static void Scene_DrawDecals( VkCommandBuffer cb, int f, int v, int slot )
{
	sc.decalDone = qtrue;
	const qboolean timed = vk_gputime_section_begin( "scene decals" );

	VkDescriptorImageInfo di;
	di.sampler = sc.nearestSampler;
	di.imageView = vk.depth.sampler_view;
	di.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet w;
	Com_Memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = sc.decalSet[f];	// this slot's last use (two frames ago) is complete
	w.dstBinding = 0;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &di;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );

	VkClearValue clear[5];
	Com_Memset( clear, 0, sizeof( clear ) );
	VkRenderPassBeginInfo begin;
	Com_Memset( &begin, 0, sizeof( begin ) );
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = sc.decalPass;
	begin.framebuffer = sc.decalFb;
	begin.renderArea.extent.width = sc.decalW;
	begin.renderArea.extent.height = sc.decalH;
	begin.clearValueCount = 5;
	begin.pClearValues = clear;
	qvkCmdBeginRenderPass( cb, &begin, VK_SUBPASS_CONTENTS_INLINE );

	scenePush_t push;
	float proj[16];
	Com_Memset( &push, 0, sizeof( push ) );
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	const qboolean rel = r_sceneEyeRelative->integer ? qtrue : qfalse;
	Scene_EyeViewProj( push.viewProj, proj, rel );
	VectorCopy( sc.views[v].origin, push.eye );
	push.eye[3] = rel ? 2.0f : 1.0f;	// the eye's: its instances' fades (2: eye-relative, scene.vert)
	VkViewport viewport;
	VkRect2D scissor;
	get_viewport( &viewport, DEPTH_RANGE_NORMAL );
	get_scissor_rect( &scissor );
	qvkCmdSetViewport( cb, 0, 1, &viewport );
	qvkCmdSetScissor( cb, 0, 1, &scissor );
	const VkDeviceSize zero = 0;
	qvkCmdBindVertexBuffers( cb, 0, 1, &sc.vertices.buf, &zero );
	qvkCmdBindIndexBuffer( cb, sc.indices.buf, 0, VK_INDEX_TYPE_UINT32 );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.decalPipe );
	if ( vk.sunShadow.descriptor )
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.decalPipeLayout, 4, 1, &vk.sunShadow.descriptor, 0, NULL );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.decalPipeLayout, 6, 1, &sc.decalSet[f], 0, NULL );
	for ( int k = 0; k < 2; k++ ) {
		const int view = k ? slot : v;	// phase 2's commands index its own visible list (its draw set's)
		if ( view < 0 )
			continue;
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.decalPipeLayout, 0, 1, &sc.drawSet[f][view], 0, NULL );
		for ( uint32_t i = 0; i < sc.numMaterials; i++ ) {
			const sceneMaterial_t *m = &sc.materials[i];
			if ( m->kind != SCENE_KIND_DECAL || !m->numCmds )
				continue;
			VkDescriptorSet sets[3] = { m->image[0]->descriptor_set, m->image[1]->descriptor_set, m->image[2]->descriptor_set };
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.decalPipeLayout, 1, 3, sets, 0, NULL );
			qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc.decalPipeLayout, 5, 1, &m->detail->descriptor_set, 0, NULL );
			Com_Memcpy( push.emissive, m->emissive, sizeof( push.emissive ) );	// a decal's threshold and normal share
			Com_Memcpy( push.tint, m->tint, sizeof( push.tint ) );
			push.info[0] = m->kind;
			push.info[1] = m->imageBits;
			push.info[2] = i;
			push.info[3] = 0;
			qvkCmdPushConstants( cb, sc.decalPipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
			const VkDeviceSize at = (VkDeviceSize)m->firstCmd * sizeof( VkDrawIndexedIndirectCommand );
			if ( vk.multiDrawIndirect )
				qvkCmdDrawIndexedIndirect( cb, sc.indirect[f][view].buf, at, m->numCmds, sizeof( VkDrawIndexedIndirectCommand ) );
			else
				for ( uint32_t c = 0; c < m->numCmds; c++ )
					qvkCmdDrawIndexedIndirect( cb, sc.indirect[f][view].buf, at + c * sizeof( VkDrawIndexedIndirectCommand ), 1, sizeof( VkDrawIndexedIndirectCommand ) );
		}
	}
	float vpAbs[16];	// (scene_pdecal.vert takes scene space)
	Scene_EyeViewProj( vpAbs, proj, qfalse );
	Scene_DrawPDecals( cb, f, v, vpAbs );	// (r_scenePDecals: after the mesh decals, in the file's order)
	qvkCmdEndRenderPass( cb );
	vk.cmd->curr_index_buffer = VK_NULL_HANDLE;
	vk_gputime_section_end( timed );

	// the lit pass mixes them in (the frame's constants are read after this submit)
	float *detail = ( (sceneFrame_t *)sc.framePtr[f] )->detail;
	detail[1] = 1.0f;
	detail[2] = backEnd.viewParms.projectionMatrix[10];
	detail[3] = backEnd.viewParms.projectionMatrix[14];
}

// RB_DrawSurfs, right after the eye view's depth prepass when occlusion did not split the pass (vk_scene_occlusion
// draws them in its split): ends the main pass, draws the decals into the DBuffer and resumes it. qtrue when it did
// a loaded scene may end and reload the main pass after the eye's prepass (occlusion's second phase,
// the decals' DBuffer), so the pass is begun keeping its MSAA colour (vk_begin_main_render_pass)
qboolean vk_scene_splits_main( void )
{
	return ( sc.loaded && r_scene->integer ) ? qtrue : qfalse;
}

qboolean vk_scene_decals( void )
{
	vkSecTimer_t tSec( VK_SEC_SCENEDECALS );	// r_cpuTimes
	const int v = backEnd.viewParms.sceneView - 1;
	if ( !Scene_DecalsWanted( v ) )
		return qfalse;
	VkCommandBuffer cb = vk.cmd->command_buffer;
	vk_gputime_split( "main keep end" );
	vk_end_render_pass();
	VkImageMemoryBarrier db;
	Com_Memset( &db, 0, sizeof( db ) );
	db.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	db.srcQueueFamilyIndex = db.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	db.image = vk.depth.image;
	db.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | ( glConfig.stencilBits > 0 ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 );
	db.subresourceRange.levelCount = 1;
	db.subresourceRange.layerCount = 1;
	db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	db.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	db.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	db.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &db );
	Scene_DrawDecals( cb, vk.cmd_index, v, -1 );
	db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	db.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	db.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	db.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		0, 0, NULL, 0, NULL, 1, &db );
	vk_gputime_next_label( "main reload" );
	vk_begin_main_reload_render_pass();
	vk_restore_draw_state( qfalse );
	return qtrue;
}

// occlusion's second phase (RB_DrawSurfs, inside the main render pass right after the eye view's depth prepass, when
// R_SceneVkBeginFrame set it up): ends the pass; builds this frame's far-depth pyramid from the depth attachment (every
// MSAA sample, the farthest: nothing visible may be hidden) into the region phase 1 did not read; when phase 1 ran, tests
// the instances it hid against that pyramid with this frame's camera, packs the ones still visible into the spare slot's
// lists and resumes the pass drawing their depth, so the lit pass and the view's later passes draw them too in this
// frame (vk_scene_draw). Returns qtrue when it ended and resumed the pass.
qboolean vk_scene_occlusion( void )
{
	vkSecTimer_t tSec( VK_SEC_SCENEOCC );	// r_cpuTimes
	const int v = backEnd.viewParms.sceneView - 1;
	const int f = vk.cmd_index;
	const int slot = sc.occSlot;

	if ( !sc.loaded || !sc.occBuild || v < 0 || v != sc.occEye || sc.occDrawn || !sc.gdFrame || vk.renderPassIndex != RENDER_PASS_MAIN )
		return qfalse;
	sc.occBuild = qfalse;	// once a frame

	VkCommandBuffer cb = vk.cmd->command_buffer;
	const uint32_t region = sc.occRegion ^ 1u;
	float proj[16], viewProj[16];
	Com_Memcpy( proj, backEnd.viewParms.projectionMatrix, sizeof( proj ) );
	proj[5] = -proj[5];
	myGlMultMatrix( backEnd.viewParms.world.modelViewMatrix, proj, viewProj );	// as vk_scene_draw's
	if ( slot >= 0 ) {	// camera 1 of the cull's Occ block: this frame's (host writes before the submit are seen)
		float *op = (float *)sc.occStatsPtr[f];
		uint32_t *ou = (uint32_t *)op;
		Com_Memcpy( op + 16, viewProj, 64 );
		VectorCopy( sc.views[v].origin, op + 36 );
		VectorCopy( sc.views[v].axis[0], op + 44 );
		for ( uint32_t L = 0; L < 16; L++ )
			ou[68 + L] = L < sc.occLevels ? sc.occOfs[L] + region * sc.occTotal : 0;
	}

	vk_gputime_split( "main keep end" );
	vk_end_render_pass();
	qboolean timed = vk_gputime_section_begin( "scene occ pyramid" );

	// the depth attachment, read by the pyramid's first level
	VkImageMemoryBarrier db;
	Com_Memset( &db, 0, sizeof( db ) );
	db.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	db.srcQueueFamilyIndex = db.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	db.image = vk.depth.image;
	db.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | ( glConfig.stencilBits > 0 ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 );
	db.subresourceRange.levelCount = 1;
	db.subresourceRange.layerCount = 1;
	db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	db.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	db.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	db.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	VkMemoryBarrier mb;	// and the region's last readers (phase 1 of this frame, two frames of culls ago) done
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	const qboolean decals = Scene_DecalsWanted( v );	// (the decal pass reads it too, after phase 2)
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
		| ( sc.numWater || ( r_sceneFx->integer && Scene_FxPlaced() ) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0 ),	// (the water and the particles read this region two frames ago)
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | ( decals ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0 ), 0, 1, &mb, 0, NULL, 1, &db );

	VkDescriptorImageInfo di;
	di.sampler = sc.nearestSampler;
	di.imageView = vk.depth.sampler_view;
	di.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet w;
	Com_Memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = sc.occSetDepth[f];	// this slot's last use (two frames ago) is complete
	w.dstBinding = 0;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &di;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.occPipeDepth );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.occPipeLayout, 0, 1, &sc.occSetDepth[f], 0, NULL );
	// r_sceneWaterBed: level 0's dispatch also writes the water's full-size bed (the water reads it this frame)
	const qboolean bedOn = ( r_sceneWaterBed->integer && sc.occBedOfs && sc.numWater ) ? qtrue : qfalse;
	// up to five levels a dispatch (scene_occ.comp: a workgroup reduces 32 x 32 source texels through shared memory)
	for ( uint32_t L = 0; L < sc.occLevels; L += 5 ) {
		if ( L )
			Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		const uint32_t count = MIN( sc.occLevels - L, 5u );
		uint32_t pc[16];
		Com_Memset( pc, 0, sizeof( pc ) );
		pc[0] = L ? sc.occSize[L - 1][0] : (uint32_t)glConfig.vidWidth;
		pc[1] = L ? sc.occSize[L - 1][1] : (uint32_t)glConfig.vidHeight;
		pc[2] = count;
		pc[3] = (uint32_t)vkSamples;
		pc[4] = L ? sc.occOfs[L - 1] + region * sc.occTotal : 0;
		pc[5] = L ? 0 : 1;
		for ( uint32_t k = 0; k < count; k++ )
			pc[6 + k] = sc.occOfs[L + k] + region * sc.occTotal;
		Com_Memcpy( &pc[12], &backEnd.viewParms.projectionMatrix[10], 4 );
		Com_Memcpy( &pc[13], &backEnd.viewParms.projectionMatrix[14], 4 );
		pc[11] = ( !L && bedOn ) ? sc.occBedOfs : 0u;
		qvkCmdPushConstants( cb, sc.occPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdDispatch( cb, ( sc.occSize[L][0] + 15 ) / 16, ( sc.occSize[L][1] + 15 ) / 16, 1 );
	}
	// (and the water's fragments, which read level 0 as its bed, and the particles', their soft fade)
	const qboolean fragRead = ( sc.numWater || ( r_sceneFx->integer && Scene_FxPlaced() ) ) ? qtrue : qfalse;
	Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | ( fragRead ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0 ), VK_ACCESS_SHADER_READ_BIT );
	sc.occAt = sc.frameNo;
	sc.occRegion = region;
	{	// the water's bed: this level 0 (the frame's constants are read after this submit)
		int32_t *bed = ( (sceneFrame_t *)sc.framePtr[f] )->waterBed;
		bed[0] = (int32_t)( sc.occOfs[0] + region * sc.occTotal );
		bed[1] = (int32_t)sc.occSize[0][0];
		bed[2] = (int32_t)sc.occSize[0][1];
		bed[3] = bedOn ? (int32_t)sc.occBedOfs : 1;	// (past 1: r_sceneWaterBed's bed at that offset; occBedOfs > 1 when allocated)
		( (sceneFrame_t *)sc.framePtr[f] )->surface2[3] = bedOn ? (float)glConfig.vidWidth : 0.0f;
	}
	Com_Memcpy( sc.occViewProj, viewProj, sizeof( viewProj ) );
	VectorCopy( sc.views[v].origin, sc.occCamEye );
	VectorCopy( sc.views[v].axis[0], sc.occCamFwd );

	if ( slot >= 0 ) {
		vk_gputime_section_end( timed );
		timed = vk_gputime_section_begin( "scene occ phase 2" );
		// phase 2: phase 1's hidden instances against this pyramid, the visible ones into the slot's commands
		const sceneView_t *view = &sc.views[slot];
		sceneCullPush_t push;
		Com_Memcpy( push.planes, view->planes, sizeof( push.planes ) );
		VectorCopy( view->origin, push.lod );
		push.lod[3] = MAX( r_sceneLodBias->value, 0.01f ) * view->invTanHalfFovY * view->lodScale;
		push.cull[0] = MAX( r_sceneCullPixels->value, 0.0f ) / view->viewportHeight / view->invTanHalfFovY;
		push.cull[1] = view->minRadius;
		push.cull[2] = push.cull[3] = 0.0f;
		push.counts[0] = sc.numInstances;
		push.cull[3] = 1.0f;	// phase 2 is the eye view's (its slot is the spare one past the views): proxies from the band
		push.counts[1] = 0;
		push.counts[2] = 2;
		push.counts[3] = ( r_sceneOcclusionStats->integer > 0 ? 1 : 0 ) | 2 | ( (uint32_t)slot << 8 );	// its LODs to sort listed; its view
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cull );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.cullPipeLayout, 0, 1, &sc.cullSet[f][slot], 0, NULL );
		qvkCmdPushConstants( cb, sc.cullPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
		Scene_DeviceCommands();
		const PFN_vkCmdDispatchIndirect dispatchIndirect = sc.cmdDispatchIndirect;
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT );
		dispatchIndirect( cb, sc.occArgs.buf, 0 );	// one thread a listed instance (phase 1 sized it)
		Scene_CullPoolFinish( cb, f, 1u << slot );	// its LODs' ranges after the frame's, in the same pool
		// its visible list in instance order; the commands it touched in command order, appended to its lists in that
		// order (scene_compact.comp's append mode, one workgroup)
		Scene_SortVisible( cb, f, slot );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.sortTouchedPipe );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.sortPipeLayout, 0, 1, &sc.sortTouchedSet[f], 0, NULL );
		qvkCmdDispatch( cb, 1, 1, 1 );
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
		uint32_t pc[12];
		Com_Memset( pc, 0, sizeof( pc ) );
		pc[0] = sc.numCmds;
		pc[1] = (uint32_t)slot;
		pc[2] = 1;
		Scene_CompactBases( pc );
		qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.compact );
		qvkCmdPushConstants( cb, sc.compactPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pc ), pc );
		qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.compactPipeLayout, 0, 1, &sc.compactSet[f][0], 0, NULL );
		qvkCmdDispatch( cb, 1, 1, 1 );
		Scene_Barrier( VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT );
	}

	vk_gputime_section_end( timed );
	// mesh decals (r_sceneDecals): the DBuffer from this prepass and phase 2's commands (their depth comes after it)
	if ( decals )
		Scene_DrawDecals( cb, f, v, slot );

	// the depth attachment back for the pass
	db.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	db.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	db.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	db.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | ( decals ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0 ),
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1, &db );

	vk_gputime_next_label( "main reload" );
	vk_begin_main_reload_render_pass();
	vk_restore_draw_state( qfalse );
	if ( slot >= 0 ) {
		sc.occDrawn = qtrue;
		sc.occOnly = qtrue;	// the prepass's second half: the slot's depth
		vk_scene_draw( SCENE_PASS_DEPTH );
		sc.occOnly = qfalse;
	}
	return qtrue;
}

/* ------------------------------------------------------------------ cvars */

// sceneimage <index>: the GPU-driven image array's image at an index (r_sceneDebug 20 reads a pixel's albedo index);
// sceneimage m <material>: the images a material bound at load (albedo, normal, ORM, detail)
// scenelshash: the light shadow atlas hashed per face now, into lshash.txt (r_sceneLightShadowHash's file), with the
// faces still waiting. scenelshash redraw: r_sceneSettleFaces' gate, in one run (what is resident is the same for both):
// the atlas as the settles and the trickle left it, then every face drawn again now as a settle did before, each face's
// hash compared (lshash_gate.txt: face, waiting, the eye's distance to it in metres, the two hashes); the faces drawn
// again since their casters came in must match, the waiting ones may not. The full redraw stays.
static void R_SceneLsHash_f( void )
{
	if ( !sc.loaded || ss.lsViews.empty() ) {
		ri.Printf( PRINT_ALL, "scenelshash: no streamed light shadows\n" );
		return;
	}
	if ( !sc.lsReadable ) {
		ri.Printf( PRINT_ALL, "scenelshash: the light shadow atlas cannot be read back; set r_sceneLightShadowHash 1 and load the map again\n" );
		return;
	}
	vk_wait_idle();
	if ( Q_stricmp( ri.Cmd_Argv( 1 ), "redraw" ) ) {
		Scene_HashLightShadows( ss.lsViews, qtrue );
		ri.Printf( PRINT_ALL, "scenelshash: %u faces waiting\n", ss.numDirty );
		return;
	}
	std::vector<uint64_t> kept, full;
	std::vector<char> nearFace( ss.lsViews.size(), 0 );	// the faces within 150 m: their texels compared too
	for ( size_t i = 0; i < ss.lsViews.size(); i++ ) {
		const sceneShadowView_t &w = ss.lsViews[i];
		float d2 = 0.0f;
		for ( int c = 0; c < 3; c++ ) {
			const float o = ss.lastEye[c] < w.lo[c] ? w.lo[c] - ss.lastEye[c] : ( ss.lastEye[c] > w.hi[c] ? ss.lastEye[c] - w.hi[c] : 0.0f );
			d2 += o * o;
		}
		nearFace[i] = d2 < 5625.0f * 5625.0f ? 1 : 0;
	}
	std::vector<std::vector<byte> > texKept( ss.lsViews.size() ), texFull( ss.lsViews.size() );
	Scene_HashLightShadows( ss.lsViews, qtrue, &kept, &nearFace, &texKept );
	const std::vector<uint8_t> waiting = ss.faceDirty;
	Scene_RenderLightShadows( ss.lsViews );
	vk_wait_idle();
	Scene_HashLightShadows( ss.lsViews, qtrue, &full, &nearFace, &texFull );
	const uint32_t texel = sc.lsFormat == VK_FORMAT_D32_SFLOAT ? 4 : 2;
	std::fill( ss.faceDirty.begin(), ss.faceDirty.end(), 0 );
	ss.numDirty = 0;
	uint32_t numWaiting = 0, waitingDiffer = 0, drawnDiffer = 0;
	float nearestDiffer = 1e30f;
	std::string out;
	for ( size_t i = 0; i < ss.lsViews.size() && i < kept.size() && i < full.size(); i++ ) {
		const sceneShadowView_t &w = ss.lsViews[i];
		float d2 = 0.0f;
		for ( int c = 0; c < 3; c++ ) {
			const float o = ss.lastEye[c] < w.lo[c] ? w.lo[c] - ss.lastEye[c] : ( ss.lastEye[c] > w.hi[c] ? ss.lastEye[c] - w.hi[c] : 0.0f );
			d2 += o * o;
		}
		const float dist = sqrtf( d2 ) / 37.5f;
		const qboolean wait = ( i < waiting.size() && waiting[i] ) ? qtrue : qfalse;
		const qboolean differ = kept[i] != full[i] ? qtrue : qfalse;
		numWaiting += wait ? 1 : 0;
		if ( differ && wait )
			waitingDiffer++;
		if ( differ && !wait ) {
			drawnDiffer++;
			nearestDiffer = MIN( nearestDiffer, dist );
		}
		// near faces: texels whose depth differs, and those that differ by over 1% of the face's range (a caster moved, came
		// or went, not a rounding)
		uint32_t texDiff = 0, texBig = 0, texAll = 0;
		if ( nearFace[i] && texel == 2 && texKept[i].size() == texFull[i].size() ) {
			const uint16_t *a = (const uint16_t *)texKept[i].data(), *b = (const uint16_t *)texFull[i].data();
			texAll = (uint32_t)( texKept[i].size() / 2 );
			for ( uint32_t t = 0; t < texAll; t++ ) {
				const int dd = abs( (int)a[t] - (int)b[t] );
				texDiff += dd ? 1 : 0;
				texBig += dd > 655 ? 1 : 0;
			}
		}
		out += va( "%u %d %.1f %016llx %016llx %u %u %u\n", (unsigned)i, wait ? 1 : 0, dist, (unsigned long long)kept[i], (unsigned long long)full[i],
			texAll, texDiff, texBig );
	}
	ri.FS_WriteFile( "lshash_gate.txt", out.c_str(), (int)out.size() );
	ri.Printf( PRINT_ALL, "scenelshash redraw: %u faces; drawn (not waiting) %u, of them %u differ from a full redraw (nearest %.0f m); waiting %u, of them %u differ; lshash_gate.txt\n",
		(unsigned)ss.lsViews.size(), (unsigned)ss.lsViews.size() - numWaiting, drawnDiffer, drawnDiffer ? nearestDiffer : 0.0f, numWaiting, waitingDiffer );
}

// scenecullcount: per frame slot and view, what the cull drew (the measurement for the visible lists' sizing)
static void R_SceneCullCount_f( void )
{
	if ( !sc.numCmds || sceneLodLead.empty() ) {
		ri.Printf( PRINT_ALL, "scenecullcount: no scene\n" );
		return;
	}
	vk_wait_idle();
	const VkDeviceSize bytes = (VkDeviceSize)sc.numCmds * sizeof( VkDrawIndexedIndirectCommand );
	sceneBuffer_t host;
	Com_Memset( &host, 0, sizeof( host ) );
	if ( !Scene_CreateBuffer( &host, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			"scene cull count readback" ) )
		return;
	void *ptr = NULL;
	VK_CHECK( qvkMapMemory( vk.device, host.mem, 0, VK_WHOLE_SIZE, 0, &ptr ) );
	ri.Printf( PRINT_ALL, "scenecullcount: %u instances, %u LODs, %u commands\n", sc.numInstances, (uint32_t)sceneLodLead.size(), sc.numCmds );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		for ( int v = 0; v < SCENE_MAX_VIEWS; v++ ) {
			if ( !sc.indirect[f][v].buf )
				continue;
			VkCommandBuffer cmd = vk_begin_command_buffer();
			VkBufferCopy region = { 0, 0, bytes };
			qvkCmdCopyBuffer( cmd, sc.indirect[f][v].buf, host.buf, 1, &region );
			vk_end_command_buffer( cmd, __func__ );
			const VkDrawIndexedIndirectCommand *c = (const VkDrawIndexedIndirectCommand *)ptr;
			uint64_t drawn = 0;
			uint32_t touched = 0, maxLod = 0;
			for ( size_t l = 0; l < sceneLodLead.size(); l++ ) {
				if ( sceneLodLead[l] == ~0u )
					continue;
				const uint32_t n = c[sceneLodLead[l]].instanceCount;
				drawn += n;
				touched += n ? 1 : 0;
				maxLod = MAX( maxLod, n );
			}
			const sceneCullDbg_t &d = sceneCullDbg[f][v];
			ri.Printf( PRINT_ALL, "scenecullcount: slot %d view %d %s: drew %llu instances in %u LODs (largest %u), %.2f%% of the slot's pool\n",
				f, v, d.eye ? "eye" : d.cascade ? va( "cascade %d", d.cascade ) : "other", (unsigned long long)drawn, touched, maxLod,
				sc.visCap[f] ? 100.0 * drawn / sc.visCap[f] : 0.0 );
		}
	}
	qvkUnmapMemory( vk.device, host.mem );
	Scene_DestroyBuffer( &host );
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		const uint32_t *h = sc.cullHdrPtr[f];
		if ( h )
			ri.Printf( PRINT_ALL, "scenecullcount: slot %d's pool: %u pairs (%.1f MB with the visible pool); last batch %u pairs, %u LODs, %u placed, %u left out; most %u, left out %u since made\n",
				f, sc.visCap[f], sc.visCap[f] * 20.0 / 1048576.0, h[0], h[1], h[2], h[3], h[14], h[15] );
	}
}

static void R_SceneImage_f( void )
{
	if ( ri.Cmd_Argc() >= 3 && !Q_stricmp( ri.Cmd_Argv( 1 ), "m" ) ) {
		const uint32_t m = (uint32_t)atoi( ri.Cmd_Argv( 2 ) );
		if ( !sc.materials || m >= sc.numMaterials ) {
			ri.Printf( PRINT_ALL, "sceneimage: no material %u (%u)\n", m, sc.numMaterials );
			return;
		}
		const sceneMaterial_t *mt = &sc.materials[m];
		ri.Printf( PRINT_ALL, "sceneimage m %u: kind %u, albedo %s, normal %s, ORM %s, detail %s\n", m, mt->kind,
			mt->image[0] ? mt->image[0]->imgName : "-", mt->image[1] ? mt->image[1]->imgName : "-",
			mt->image[2] ? mt->image[2]->imgName : "-", mt->detail ? mt->detail->imgName : "-" );
		return;
	}
	if ( ri.Cmd_Argc() < 2 ) {
		ri.Printf( PRINT_ALL, "usage: sceneimage <index> | sceneimage m <material>\n" );
		return;
	}
	const uint32_t t = (uint32_t)atoi( ri.Cmd_Argv( 1 ) );
	ri.Printf( PRINT_ALL, "sceneimage %u: %s (of %u)\n", t, t < sceneTexImages.size() && sceneTexImages[t] ? sceneTexImages[t]->imgName : "-",
		(unsigned)sceneTexImages.size() );
}

static void R_SceneInfo_f( void )
{
	if ( !sc.loaded ) {
		ri.Printf( PRINT_ALL, "no render scene loaded\n" );
		return;
	}
	ri.Printf( PRINT_ALL, "scene %s: %u instances, %u meshes, %llu triangles (all LODs), %u materials, %u draw commands, %u lights; "
		"bounds %.0f %.0f %.0f .. %.0f %.0f %.0f\n", sc.name, sc.numInstances, sc.numMeshes, (unsigned long long)sc.numTriangles,
		sc.numMaterials, sc.numCmds, sc.numLights, sc.mins[0], sc.mins[1], sc.mins[2], sc.maxs[0], sc.maxs[1], sc.maxs[2] );
}

void R_SceneRegister( void )
{
	r_scene				= ri.Cvar_Get( "r_scene",				"1",					CVAR_ARCHIVE_ND, "Draw a map's render scene (maps/<name>.xscn) beside its BSP (rd-vulkan; docs/levelport-scene.md); loaded only when 1 at map load" );
	r_sceneWindowExposure = ri.Cvar_Get( "r_sceneWindowExposure", "-1",					CVAR_ARCHIVE_ND, "Render scene: the share of the exposure's change from r_sceneEmissiveRef that the lit rooms behind windows (window_parallax_interior) follow, apart from other emissives (-1 r_sceneEmissiveExposure's, as before; 1 all, a physical light: CP2077's EmissiveEV 4 rooms read nearly black by day; CP2077's exposureScale is 0.3)" );
	r_sceneProbeSky		= ri.Cvar_Get( "r_sceneProbeSky",		"0",					CVAR_ARCHIVE_ND, "Render scene: how much of the sky a surface's CP2077 reflection probe's capture lacks (instance slot bits 28-31, tag_probesky.py) it loses from its sky and ground ambient; CP2077 lights ambient from the relit captures, so an enclosed room seen through a window was lit like the street; 0 off, 1 CP2077's" );
	r_sceneLodFade		= ri.Cvar_Get( "r_sceneLodFade",		"0.25",					CVAR_ARCHIVE_ND, "Render scene: a mesh's LODs crossfade (dithered, as CP2077's do) over this share of the projected size above each switch, instead of popping (a facade near its threshold flickered between LODs as the eye moved); the band's instances draw twice; 0 off" );
	r_sceneLodBias		= ri.Cvar_Get( "r_sceneLodBias",		"1",					CVAR_ARCHIVE_ND, "Render scene: multiplies the projected size the LOD is chosen by (higher: finer LODs farther)" );
	r_sceneCullPixels	= ri.Cvar_Get( "r_sceneCullPixels",		"1.5",					CVAR_ARCHIVE_ND, "Render scene: placements whose bounding sphere is smaller than this many pixels are not drawn" );
	r_sceneOcclusion	= ri.Cvar_Get( "r_sceneOcclusion",		"1",					CVAR_ARCHIVE_ND, "Render scene: occlusion culling of the eye view's instances (GPU-driven draws): those behind the last frame's depth are left out, then those of them this frame's depth prepass does not hide are drawn after it (nothing visible waits a frame)" );
	r_sceneOcclusionStats = ri.Cvar_Get( "r_sceneOcclusionStats", "0",				CVAR_TEMP, "Render scene (bench): print each frame the occlusion counts (SCENEOCC: instances tested, hidden by the last frame's depth, their indices; drawn by the second phase)" );
	r_sceneClusterStats	= ri.Cvar_Get( "r_sceneClusterStats",	"0",				CVAR_TEMP, "Render scene (bench): print each frame the light clusters' fallbacks to the grid cells (SCENECLUSTERS: tiles whose lights overflowed a run, clusters marked full)" );
	r_sceneHideScale	= ri.Cvar_Get( "r_sceneHideScale",		"1",						CVAR_ARCHIVE_ND, "Render scene: multiplies each instance's hide distance (CP2077's forceAutoHideDistance, scene version 4): past it from the eye it is not drawn in any view; and its near distance (a far proxy, version 5): within it, likewise (0: none hidden)" );
	r_sceneHideFade		= ri.Cvar_Get( "r_sceneHideFade",		"1",						CVAR_ARCHIVE_ND, "Render scene: an instance fades out over the 40 m before its hide distance and a far proxy (scene version 5) in over the 40 m before its near distance (half the distance when nearer), dithered, in the eye's views, scaled with r_sceneHideScale: a mesh and its proxy at one distance complement each other; 0: they pop" );
	r_sceneWaterBed		= ri.Cvar_Get( "r_sceneWaterBed",		"0",						CVAR_ARCHIVE_ND, "Render scene: the water's bed (its clarity and shore) from a full-size depth of each pixel's samples, their nearest and farthest and the share at the nearest, written by the occlusion pyramid's first dispatch (8 B a pixel: 32-bit floats, allocated when on at map load; the eye's view only); 0: the pyramid's half-size farthest" );
	r_sceneCutoutCoverage = ri.Cvar_Get( "r_sceneCutoutCoverage", "1",				CVAR_ARCHIVE_ND, "Render scene: cutouts (leaves, chain-link fences, grates) cover each MSAA sample by their alpha there (alpha to coverage, from its gradient: softer, steadier edges) instead of one alpha test per pixel, which MSAA cannot smooth (GPU-driven draws, r_sceneLitEqual, MSAA 2, 4 or 8); the sun and light shadows keep the test. 0: one test per pixel" );
	r_sceneHideDither	= ri.Cvar_Get( "r_sceneHideDither",		"2",						CVAR_ARCHIVE_ND, "Render scene, r_sceneHideFade: the fade's pattern; 0 interleaved gradient noise, whole pixels; 1 a blue noise tile, whole pixels; 2 (default) the depth prepass keeps that share of each pixel's MSAA samples, so the resolve blends the two (GPU-driven draws, r_sceneLitEqual and MSAA, else 1; whole-pixel patterns showed as a static checker on every mesh and proxy in a fade band)" );
	r_sceneCardBlend	= ri.Cvar_Get( "r_sceneCardBlend",		"1",						CVAR_ARCHIVE_ND, "Render scene: a Witcher 3 SpeedTree billboard card turning between two of its stored views blends them (1) instead of picking one per pixel by a dither (0: a static checker of the two views on far trees, 2 x 2 under r_sceneVrs)" );
	r_sceneLitEqual		= ri.Cvar_Get( "r_sceneLitEqual",		"1",						CVAR_ARCHIVE_ND, "Render scene: the GPU-driven lit pass tests depth EQUAL against the prepass (the same pixels as LESS_OR_EQUAL, bit for bit; a fragment the fade's dither discarded in the prepass is not shaded: Watson 0.14 ms a frame, up to 0.43); 0 LESS_OR_EQUAL" );
	r_sceneDecals		= ri.Cvar_Get( "r_sceneDecals",		"1",						CVAR_ARCHIVE_ND, "Render scene: CP2077's mesh decals (the scene's blend materials flagged 0x40) project onto the surface behind them within their depth threshold, their albedo and normal blended into it before it is lit (a DBuffer after the eye's prepass); 0 drawn as blends over it. Read at map load" );
	r_sceneProxyDebug	= ri.Cvar_Get( "r_sceneProxyDebug",	"0",						CVAR_TEMP | CVAR_CHEAT, "Render scene, a look check: 1 draws no far proxy (scene version 5), 2 draws every one at any distance (its near distance ignored, the hide fade off); 0 as authored" );
	r_sceneOnlyMesh		= ri.Cvar_Get( "r_sceneOnlyMesh",		"0",						CVAR_TEMP | CVAR_CHEAT, "Render scene, a measuring view: N > 0 draws only the instances of mesh N - 1, in every view and its shadows (the billboard cards matched to their 3D trees, w3cardmatch.py); 0 all" );
	r_sceneKeptCull		= ri.Cvar_Get( "r_sceneKeptCull",		"1",						CVAR_TEMP, "Render scene: a kept sun cascade (r_sunShadowFarCache, r_sunShadowMidCache) whose static casters come from its cache this frame is not culled (its lists stay empty; nothing draws them); 0 culls it every frame (a timing A/B; the same images)" );
	r_sceneCompactBlocks	= ri.Cvar_Get( "r_sceneCompactBlocks",	"1",						CVAR_TEMP, "Render scene: the GPU-driven lists packed a workgroup per 1024 commands (three passes; the same lists); 0 the one-workgroup-a-view packing (a timing A/B)" );
	r_sceneClusterCull	= ri.Cvar_Get( "r_sceneClusterCull",		"1",						CVAR_TEMP, "Render scene: the light clusters' tiles walk the lights live and in the frustum (listed once a frame; Watson 0.9 ms a frame) instead of every light; 0 every light (the old path, for one release)" );
	r_sceneLightHide	= ri.Cvar_Get( "r_sceneLightHide",		"1",						CVAR_ARCHIVE_ND, "Render scene: multiplies each light's hide distance (CP2077's autoHideDistance, scene version 4): past it from the eye the light is off, fading over the last 15%; its shadow fades by shadowFade + shadowRange (0: no light hidden)" );
	r_sceneLightHideDistant = ri.Cvar_Get( "r_sceneLightHideDistant", "0",						CVAR_ARCHIVE_ND, "Render scene: lights the data flags allowDistantLight (CP2077 draws them far off as distant lights) keep shining past their hide distance, in place of r_sceneLightDistant's stand-in (0: every light hides)" );
	r_sceneLightHideDefault = ri.Cvar_Get( "r_sceneLightHideDefault", "0",						CVAR_ARCHIVE_ND, "Render scene: a hide distance for a light the data gives none, in units of its radius, at most 80 m (0: such lights never hide)" );
	r_sceneLightDistant = ri.Cvar_Get( "r_sceneLightDistant",	"1",						CVAR_ARCHIVE_ND, "Render scene: a light the data flags allowDistantLight shows past its hide distance as a stand-in (CP2077's distant lights): a plain point, no shadow, haze or box, outside the light clusters, fading in as the full light fades out (0: such lights hide like any other)" );
	r_sceneDlightReach	= ri.Cvar_Get( "r_sceneDlightReach",	"2.5",					CVAR_ARCHIVE_ND, "Render scene: the reach of the game's dynamic lights on its surfaces, times their own radius (sized for lightmapped maps)" );
	r_sceneSignals		= ri.Cvar_Get( "r_sceneSignals",		"1",					CVAR_ARCHIVE_ND, "Render scene: traffic signals cycle as their intersections' programs say (maps/<map>.signals: car lights, WALK / DON'T WALK, the game's own timings); 0: every lamp lit" );
	r_sceneSignalAt		= ri.Cvar_Get( "r_sceneSignalAt",		"-1",					CVAR_CHEAT, "Render scene: every signal held at this many seconds into its cycle (checks; -1 off)" );
	r_sceneSignalShift	= ri.Cvar_Get( "r_sceneSignalShift",	"0",					CVAR_CHEAT, "Render scene: seconds added to the signals' clock (looks at a given phase)" );
	r_sceneDlights		= ri.Cvar_Get( "r_sceneDlights",		"150",					CVAR_ARCHIVE_ND, "Render scene: the power of the game's dynamic lights (sabers, shots, effects) on its surfaces; 0 off" );
	r_sceneLightScale	= ri.Cvar_Get( "r_sceneLightScale",		"0.03",					CVAR_ARCHIVE_ND, "Render scene: multiplies the source's light power" );
	r_sceneExposure		= ri.Cvar_Get( "r_sceneExposure",		"1.3",					CVAR_ARCHIVE_ND, "Render scene: exposure of the linear lighting before it goes back to gamma space" );
	r_sceneEmissive		= ri.Cvar_Get( "r_sceneEmissive",		"1",					CVAR_ARCHIVE_ND, "Render scene: multiplies emissive materials" );
	r_sceneEmissiveExposure = ri.Cvar_Get( "r_sceneEmissiveExposure", "1",					CVAR_ARCHIVE_ND, "Render scene: the share of the exposure's change from r_sceneEmissiveRef that emissives follow (1 all of it, a physical light, as before; 0 none, the same on screen at every hour; CP2077's exposureScale is 0.3)" );
	r_sceneEmissiveRef	= ri.Cvar_Get( "r_sceneEmissiveRef",	"1.3",					CVAR_ARCHIVE_ND, "Render scene, r_sceneEmissiveExposure: the exposure at which emissives show as authored (1.3, the night's)" );
	r_sceneGlow			= ri.Cvar_Get( "r_sceneGlow",			"3",					CVAR_ARCHIVE_ND, "Render scene: emissive materials in the dynamic glow (r_DynamicGlow); multiplies what they add, 0 off" );
	r_sceneGlowThreshold = ri.Cvar_Get( "r_sceneGlowThreshold",	"0.5",					CVAR_ARCHIVE_ND, "Render scene: emissive light (linear, after exposure) below this does not glow" );
	r_sceneEyeRelative	= ri.Cvar_Get( "r_sceneEyeRelative",	"1",					CVAR_ARCHIVE_ND, "Render scene: the eye's views draw positions relative to the eye (the matrix without the view's translation): far from the map's origin float32 lost more depth than a few centimetres and surfaces that close fought (Watson's districts: a facade's panels and their backing); 0: scene space through the whole matrix, as before" );
	r_sceneShadows		= ri.Cvar_Get( "r_sceneShadows",		"1",					CVAR_ARCHIVE_ND, "Render scene: casts into and receives the map's sun cascades (q3gl2_sun, r_sunShadows)" );
	r_sceneShadowLod	= ri.Cvar_Get( "r_sceneShadowLod",		"0.25",					CVAR_ARCHIVE_ND, "Render scene: casters take the main view's LOD at this share of its projected size (lower: coarser)" );
	r_sceneShadowCull	= ri.Cvar_Get( "r_sceneShadowCull",		"2",					CVAR_ARCHIVE_ND, "Render scene: casters whose bounding radius is under this many cascade texels are not drawn" );
	r_sceneLightShadows	= ri.Cvar_Get( "r_sceneLightShadows",	"1",					CVAR_ARCHIVE_ND, "Render scene: static shadow maps for the source's shadow-casting lights, rendered at map load (0: none built, or not read when set later)" );
	r_sceneLightShadowRes = ri.Cvar_Get( "r_sceneLightShadowRes", "128",				CVAR_ARCHIVE_ND, "Render scene: a light shadow face's texels across for a light reaching 15 m (others scale with their reach, 32 to twice this; at map load)" );
	r_sceneLightShadowLists = ri.Cvar_Get( "r_sceneLightShadowLists", "1",				CVAR_TEMP, "Render scene: the light shadow faces culled from their own instance lists and drawn from the GPU-driven lists (at map load; 0 every instance culled and every material drawn per face, an A/B)" );
	r_sceneVisiblePool = ri.Cvar_Get( "r_sceneVisiblePool", "2097152",					CVAR_ARCHIVE_ND, "Render scene: the instances a frame's views may draw together (the count-then-scan cull's pool: 20 bytes each, a pool a frame slot; grown when a frame leaves some out; read at map load)" );
	r_sceneLightShadowBudget = ri.Cvar_Get( "r_sceneLightShadowBudget", "0.15",		CVAR_ARCHIVE_ND, "Render scene: the most of the VRAM budget the light shadow atlas takes (at map load; past it the smallest faces are left out)" );
	r_sceneLightShadowDepth = ri.Cvar_Get( "r_sceneLightShadowDepth", "16",			CVAR_ARCHIVE_ND, "Render scene: the light shadow atlas's depth bits, 16 (the default: half the memory) or 32 (float; at map load)" );
	r_sceneInteriors	= ri.Cvar_Get( "r_sceneInteriors",		"1",					CVAR_ARCHIVE_ND, "Render scene: CP2077's interiors (maps/<map>.xint) drawn only from inside their streaming boxes, as the game streams them (0: always, an A/B)" );
	r_sceneInteriorMargin = ri.Cvar_Get( "r_sceneInteriorMargin", "4",				CVAR_ARCHIVE_ND, "Render scene: an interior is drawn from this many metres outside its streaming box" );
	r_sceneInteriorPreload = ri.Cvar_Get( "r_sceneInteriorPreload", "32",			CVAR_ARCHIVE_ND, "Render scene: geometry streaming reads an interior from this many metres outside its streaming box" );
	r_sceneCullGroups	= ri.Cvar_Get( "r_sceneCullGroups",		"1",					CVAR_ARCHIVE_ND, "Render scene: the cull runs only over the groups of 4,096 instances whose bounds are in the view (0: over every instance, an A/B)" );
	r_sceneLightShadowHash = ri.Cvar_Get( "r_sceneLightShadowHash", "0",				CVAR_TEMP, "Render scene: at map load, the light shadow atlas read back and hashed per face into lshash.txt (a gate)" );
	r_sceneStream		= ri.Cvar_Get( "r_sceneStream",			"1",					CVAR_ARCHIVE_ND, "Render scene: a tiled scene whose files are stored in their packs keeps only the geometry its views want, read from the packs as they come near (0: all of it, read at map load). Read at map load" );
	r_sceneStreamMB		= ri.Cvar_Get( "r_sceneStreamMB",		"1536",					CVAR_ARCHIVE_ND, "Render scene: the streamed geometry's pools, MB of vertices and indices together. Read at map load" );
	r_sceneStreamMargin	= ri.Cvar_Get( "r_sceneStreamMargin",	"256",					CVAR_ARCHIVE_ND, "Render scene: streaming reads the geometry the cull would draw from this many metres nearer than the eye is (the ring loaded ahead)" );
	r_sceneStreamUploadMB = ri.Cvar_Get( "r_sceneStreamUploadMB", "48",				CVAR_ARCHIVE_ND, "Render scene: the most streamed geometry copied into the pools a frame, MB" );
	r_sceneStreamFaces	= ri.Cvar_Get( "r_sceneStreamFaces",	"16",					CVAR_ARCHIVE_ND, "Render scene: streaming draws again at most this many light shadow faces whose casters came in, nearest first, every tenth of a second (0: only when a view settles; 64 stalled the frame up to 29 ms, 16 about 8)" );
	r_sceneSettleFaces	= ri.Cvar_Get( "r_sceneSettleFaces",	"150",					CVAR_ARCHIVE_ND, "Render scene: when a view settles (a teleport), the light shadow faces whose casters came in are drawn again only within this many metres of the eye; the rest are drawn by the trickle (r_sceneStreamFaces), nearest first. 0: every face drawn again (slower: 1.1 s in Watson)" );
	r_sceneStreamStats	= ri.Cvar_Get( "r_sceneStreamStats",	"0",					CVAR_TEMP, "Render scene: a SCENESTREAM line a second (resident, wanted, reads, uploads, evictions)" );
	r_sceneDetailSlots	= ri.Cvar_Get( "r_sceneDetailSlots",	"8",					CVAR_ARCHIVE_ND, "Render scene: how many of a multilayered material's layers (maps/<map>.xdet version 2's slots, by visible share) are tiled over its bake, each where its share map shows it: 0, 1 (its first, as version 1), 4 or 8 (the default, as the converter bakes); GPU-driven draws" );
	r_sceneDetail		= ri.Cvar_Get( "r_sceneDetail",			"1",					CVAR_ARCHIVE_ND, "Render scene: multilayered materials' main layer tiled at the source's own rate over their bakes (maps/<map>.xdet; 0 off, between them weaker)" );
	r_sceneWater		= ri.Cvar_Get( "r_sceneWater",			"1",					CVAR_ARCHIVE_ND, "Render scene: water (the scene's blend materials flagged 0x80, maps/<map>.xwat): scrolling ripples, Fresnel reflections (the screen's, else the sky's), the bed fading with depth into the water's colour, a soft shore; 0 drawn as plain blends. Read at map load" );
	r_sceneWaterRipple	= ri.Cvar_Get( "r_sceneWaterRipple",	"1",					CVAR_ARCHIVE_ND, "Render scene: the water's ripples, times each material's normal strength (0 flat)" );
	r_sceneWaterClarity	= ri.Cvar_Get( "r_sceneWaterClarity",	"1",					CVAR_ARCHIVE_ND, "Render scene: how far into the water the bed shows, times each material's visibility" );
	r_sceneWaterReflect	= ri.Cvar_Get( "r_sceneWaterReflect",	"1",					CVAR_ARCHIVE_ND, "Render scene: the water's reflections, times their Fresnel" );
	r_sceneWaterShift	= ri.Cvar_Get( "r_sceneWaterShift",		"24",					CVAR_ARCHIVE_ND, "Render scene: how far the ripples shift what the water reflects (game units at full tilt)" );
	r_sceneSsrFlat		= ri.Cvar_Get( "r_sceneSsrFlat",		"0.95",					CVAR_ARCHIVE_ND, "Render scene: the reflections take a surface whose rebuilt normal points up past this cosine as level ground (the half-size depth rounds, and the normal tilted row by row: reflected uprights in sideways-shifted strips; 0: off)" );
	r_sceneWaterBreakup	= ri.Cvar_Get( "r_sceneWaterBreakup",	"1",					CVAR_ARCHIVE_ND, "Render scene: the water's own light on its flat surface (the ripples shaded it in their image's grid) and a third, broad ripple layer under the two (one image at two tiles repeated plainly in the middle distance); 0: neither" );
	r_sceneWaterWaves	= ri.Cvar_Get( "r_sceneWaterWaves",		"1",					CVAR_ARCHIVE_ND, "Render scene: the water's waves simulated every frame (three FFT cascades of a wind's spectrum, scene_water.comp; made at map load when 1) instead of the ripple image's copies (one still pattern repeats however it is scaled or turned); 0: the ripple image" );
	r_sceneWaterWind	= ri.Cvar_Get( "r_sceneWaterWind",		"4",					CVAR_ARCHIVE_ND, "Render scene, r_sceneWaterWaves: the wind (m/s) whose spectrum the waves follow (their shape within each cascade; their strength stays the ripples'); read at map load" );
	r_sceneWaterCascades = ri.Cvar_Get( "r_sceneWaterCascades",	"5.13 37.1 211.7",		CVAR_ARCHIVE_ND, "Render scene, r_sceneWaterWaves: the three cascades' patch sizes (m; incommensurate, so no period lines up); read at map load" );
	r_sceneRain			= ri.Cvar_Get( "r_sceneRain",			"1",					CVAR_ARCHIVE_ND, "Render scene: wet ground only where the rain reaches (a view of the scene from above, read while 1); 0: every up-facing surface" );
	r_sceneRainTiles	= ri.Cvar_Get( "r_sceneRainTiles",		"0",					CVAR_ARCHIVE_ND, "Render scene, r_sceneRainFollow: the rain view's tiles drawn in a frame at most (it holds 4 x 4 of the world, 109 m a side, with wrap-around: the ones the eye moved towards first, then those streamed geometry changed); every one when it holds none of the window (a jump); 0 (the default: the tiles have two known faults, parked; the kits set 0 too): the whole view drawn again in one frame as before (one 2-4 ms frame each time)" );
	r_sceneRainFollow	= ri.Cvar_Get( "r_sceneRainFollow",	"1",					CVAR_ARCHIVE_ND, "Render scene: on a map wider than the rain view (437 m at most), the view follows the eye, drawn again in the frame as it moves or as geometry streams in; 0: where it was drawn at load (past it every up-facing surface is wet)" );
	r_sceneShadowCascades = ri.Cvar_Get( "r_sceneShadowCascades", "3",					CVAR_ARCHIVE_ND, "Render scene: casts into the first this many sun cascades (the far ones cover the most)" );
	r_sceneSun			= ri.Cvar_Get( "r_sceneSun",			"0.25",					CVAR_ARCHIVE_ND, "Render scene: the map sun's (or moon's) light, linear, in q3gl2_sun's hue" );
	r_sceneTonemap		= ri.Cvar_Get( "r_sceneTonemap",		"1",					CVAR_ARCHIVE_ND, "Render scene: tone curve, 0 exponential, 1 filmic (ACES fit: darker shadows, a softer shoulder)" );
	r_sceneFog			= ri.Cvar_Get( "r_sceneFog",			"51 38 32",				CVAR_ARCHIVE_ND, "Render scene: the haze's colour (sRGB 0-255) before the colour grade (r_sceneGrade), matched to the map's sky near the horizon" );
	r_sceneFogDensity	= ri.Cvar_Get( "r_sceneFogDensity",		"0.005",				CVAR_ARCHIVE_ND, "Render scene: haze density per metre at the floor (0 off; 0.005 hides half of what is 140 m away)" );
	r_sceneFogHeight	= ri.Cvar_Get( "r_sceneFogHeight",		"80",					CVAR_ARCHIVE_ND, "Render scene: metres over which the haze thins by e" );
	r_sceneFogMax		= ri.Cvar_Get( "r_sceneFogMax",			"0.85",					CVAR_ARCHIVE_ND, "Render scene: the most of a surface the haze hides" );
	r_sceneFogLight		= ri.Cvar_Get( "r_sceneFogLight",		"1.6",					CVAR_ARCHIVE_ND, "Render scene: multiplies the source's light the haze scatters toward the eye (glows around lamps and signs; 0 off)" );
	r_sceneFogPhase		= ri.Cvar_Get( "r_sceneFogPhase",		"0.5",					CVAR_ARCHIVE_ND, "Render scene: the haze's forward scattering (Henyey-Greenstein g: 0 even, toward 1 a tighter glow looking toward a light)" );
	r_sceneFogAlbedo	= ri.Cvar_Get( "r_sceneFogAlbedo",		"1 0.925 0.794",		CVAR_ARCHIVE_ND, "Render scene: the haze's albedo (linear r g b), which tints the light it scatters (Kabuki: the pollution weather's VolumetricFogAreaSettings.albedo)" );
	r_sceneFogVolumes	= ri.Cvar_Get( "r_sceneFogVolumes",		"1",					CVAR_ARCHIVE_ND, "Render scene: multiplies the haze's local volumes (the source's fog volumes and steam columns, maps/<map>.xfog; 0 off)" );
	r_sceneFogTemporal	= ri.Cvar_Get( "r_sceneFogTemporal",	"0",						CVAR_ARCHIVE_ND, "Render scene: the haze's light sampled at a point that moves within each froxel every frame and settled over frames (one sample at the centre showed the froxel grid as a checker on lamp-lit ground; with r_sceneFogSegment it moves across the view only); 0: one sample at the centre" );
	r_sceneSpecAA		= ri.Cvar_Get( "r_sceneSpecAA",		"0",						CVAR_ARCHIVE_ND, "Render scene: geometric specular anti-aliasing, the most the shading normal's change across a pixel widens the highlight (alpha squared; 0.18 was tried; 0: off)" );
	r_sceneWindowCell	= ri.Cvar_Get( "r_sceneWindowCell",		"0",						CVAR_ARCHIVE_ND, "Render scene: a window's room picked by a 3D room cell on a grid turned to the facade, so the panes of a corner bay share one room (0: by the pane's cell and facing; every window's room changes)" );
	r_sceneFogFar		= ri.Cvar_Get( "r_sceneFogFar",			"600",					CVAR_ARCHIVE_ND, "Render scene: metres the haze's light volume reaches (its 64 slices grow exponentially from 1 m)" );
	r_sceneFogStart		= ri.Cvar_Get( "r_sceneFogStart",		"150",				CVAR_ARCHIVE_ND, "Render scene: metres before the haze's colour starts filling in; nearer, the haze only dims and shows the light scattered in it (the source's distant fog starts at 150 m)" );
	r_sceneFogMoon		= ri.Cvar_Get( "r_sceneFogMoon",		"1",					CVAR_ARCHIVE_ND, "Render scene: the moon's (the map sun's) light in the haze, through its shadow cascades (0 off)" );
	r_sceneWet			= ri.Cvar_Get( "r_sceneWet",			"1",					CVAR_ARCHIVE_ND, "Render scene: how wet up-facing surfaces are (rain: darker, glossy; 0 dry)" );
	r_scenePDecals		= ri.Cvar_Get( "r_scenePDecals",		"1",					CVAR_ARCHIVE_ND, "Render scene: CP2077's projected decals (maps/<map>.xdcl: grime, dirt, markings, puddles) as boxes into the DBuffer; 0 none; read at map load" );
	r_scenePDecalDistance = ri.Cvar_Get( "r_scenePDecalDistance", "300",			CVAR_ARCHIVE_ND, "Render scene, r_scenePDecals: metres past which no projected decal is drawn (each also hides past its own distance)" );
	r_scenePDecalNormal	= ri.Cvar_Get( "r_scenePDecalNormal",	"1 0",					CVAR_ARCHIVE_ND, "Render scene, r_scenePDecals: the normal threshold's test (1: a receiver turned more than 90 - threshold degrees from the box's +Z is left out; 2: more than threshold degrees; 0 none), 1 to take -Z, and 1 for a soft edge (about 2 degrees each side of the limit; 0, the default, cuts there). A receiver whose normal the depth cannot give (a ridge under a pixel, a dithered fade, the screen's edge) keeps the decal" );
	r_scenePDecalMax	= ri.Cvar_Get( "r_scenePDecalMax",		"32768",				CVAR_ARCHIVE_ND, "Render scene, r_scenePDecals: the most drawn in a frame (the visible list's size; read at map load)" );
	r_scenePDecalTerrain = ri.Cvar_Get( "r_scenePDecalTerrain",	"0",					CVAR_ARCHIVE_ND, "Render scene, r_scenePDecals: 1 draws every terrain-projected decal (flag 4); 0 leaves out those without a tiled colour (an .xdcl version 1's: a mask and a tint, flat dark patches); read at map load" );
	r_scenePDecalDebug	= ri.Cvar_Get( "r_scenePDecalDebug",	"0",					CVAR_TEMP, "Render scene, r_scenePDecals: 1 prints the decals drawn, runs and records tested a frame, and the CPU time, every 60 frames; 2 also writes each decal's material index into the albedo (r_sceneDebug 1, screenshot_png: red low, blue high)" );
	r_sceneWetPorosity	= ri.Cvar_Get( "r_sceneWetPorosity",	"0.224096 0.787952 0.5 0.4",	CVAR_ARCHIVE_ND, "Render scene, r_sceneWet: wetness by porosity (CP2077's RainAreaSettings; Lagarde, Water drop 3): the roughness where porosity starts and ends, a soaked porous surface's albedo scale, and the roughness past which wet ground mirrors nothing (porous ground darkens and keeps its grain; smooth ground takes the film); 0 every wet surface one film" );
	r_sceneWetPuddles	= ri.Cvar_Get( "r_sceneWetPuddles",		"0.3",					CVAR_ARCHIVE_ND, "Render scene: the share of wet ground under puddles (near mirrors)" );
	r_sceneLightSize	= ri.Cvar_Get( "r_sceneLightSize",		"0.3",					CVAR_ARCHIVE_ND, "Render scene: the source's lights' size in metres, for their highlights on glossy and wet surfaces" );
	r_sceneEnv			= ri.Cvar_Get( "r_sceneEnv",			"",						CVAR_TEMP, "Render scene: a scene environment file read at map load in place of maps/<map>.xenv (its look values, scene, time and sun), for A/Bs of looks on one map; empty: the map's own" );
	r_sceneSky			= ri.Cvar_Get( "r_sceneSky",			"0",					CVAR_ARCHIVE_ND, "Render scene: 1 draws an analytic sky (zenith to horizon, the haze's colour along the horizon, the sun's disc) over the map's own; a day scene's maps/<map>.xenv sets it (0: the map's sky)" );
	r_sceneSkyZenith	= ri.Cvar_Get( "r_sceneSkyZenith",		"0.08 0.16 0.4",		CVAR_ARCHIVE_ND, "Render scene, r_sceneSky: the zenith's colour, linear, in the scene's units (r_sceneAmbient's)" );
	r_sceneSkyGround	= ri.Cvar_Get( "r_sceneSkyGround",		"0 0 0",				CVAR_ARCHIVE_ND, "Render scene, r_sceneSky: the distant land's colour under the horizon, linear, in the scene's units, for a map whose ground ends short of it (0 0 0: the sky continues below the horizon, darkened). A look value (.xenv)" );
	r_sceneSkyCurve		= ri.Cvar_Get( "r_sceneSkyCurve",		"4",					CVAR_ARCHIVE_ND, "Render scene, r_sceneSky: how close to the horizon its colour stays (the power of one minus the view's height; 1 a broad blend, 4 a band). A look value (.xenv)" );
	r_sceneSkyHorizon	= ri.Cvar_Get( "r_sceneSkyHorizon",		"0.45 0.55 0.65",		CVAR_ARCHIVE_ND, "Render scene, r_sceneSky: the horizon's colour, linear, in the scene's units" );
	r_sceneSkySun		= ri.Cvar_Get( "r_sceneSkySun",			"1",					CVAR_ARCHIVE_ND, "Render scene, r_sceneSky: the sun's disc and glow, times the scene's sun (r_sceneSun and its colour); 0 none" );
	r_sceneTerrainPush	= ri.Cvar_Get( "r_sceneTerrainPush",	"0",					CVAR_ARCHIVE_ND, "Render scene: 1: terrain (material flag 0x1000, CP2077's multilayered_terrain.mt) drawn a little behind its depth along the eye's ray, so the streets, sidewalks and plazas laid in its plane win instead of fighting it as the eye moves. Off by default: with the eye view drawn eye-relative it changed 5,549 of 101 M pixels over 49 Watson views, the largest a light glinting through the terrain, and fixed no fighting. 0: at its depth. Read at map load" );
	r_sceneFoliage		= ri.Cvar_Get( "r_sceneFoliage",		"0",					CVAR_ARCHIVE_ND, "Render scene: SpeedTree leaves (material flag 0x800) shaded as canopies: the share of the leaf's normal taken from its tree's rounded shape (0 off: each leaf card's own, as before; 0.7 a dense canopy)" );
	r_sceneFoliageWrap	= ri.Cvar_Get( "r_sceneFoliageWrap",	"0.4",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFoliage: how far the sun wraps round the canopy's shaded side (0 none, 1 all the way)" );
	r_sceneFoliageTrans	= ri.Cvar_Get( "r_sceneFoliageTrans",	"1",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFoliage: the sun through backlit leaves, in a deeper and warmer colour than their own (0 none)" );
	r_sceneShadowSoft	= ri.Cvar_Get( "r_sceneShadowSoft",	"0",					CVAR_ARCHIVE_ND, "Render scene: the sun shadow's filter radius in cascade texels (a 4 x 4 grid of filtered comparisons over it; 0: the 2 x 2 at 0.75 texel, as before): softer edges for fine casters (leaves). A look value (.xenv)" );
	r_sceneShadowFar	= ri.Cvar_Get( "r_sceneShadowFar",		"0",					CVAR_ARCHIVE_ND, "Render scene: the far sun cascade reaches this far (units) when beyond r_shadowCascadeZFar; the near two keep their splits (coarser far texels: a wide map's long views keep their shadows). A look value (.xenv)" );
	r_sceneAO			= ri.Cvar_Get( "r_sceneAO",				"0",					CVAR_ARCHIVE_ND, "Render scene: screen-space ambient occlusion on the scene's ambient light (sky and ground fill, and its mirrored sky), not on the sun or the lamps; traced on last frame's half-size depth beside the reflections (scene_ssr.comp). Value = strength (0 off, 1 full). A look value (.xenv)" );
	r_sceneAORadius		= ri.Cvar_Get( "r_sceneAORadius",		"1.5",					CVAR_ARCHIVE_ND, "Render scene, r_sceneAO: how far round a surface occluders count (metres)" );
	r_sceneAOSamples	= ri.Cvar_Get( "r_sceneAOSamples",		"8",					CVAR_ARCHIVE_ND, "Render scene, r_sceneAO: samples a half-size texel a frame (1..32; settled over frames)" );
	r_sceneAOBlend		= ri.Cvar_Get( "r_sceneAOBlend",		"0.15",					CVAR_ARCHIVE_ND, "Render scene, r_sceneAO: the share of a new frame in the settled occlusion (lower: smoother, slower to follow motion)" );
	r_sceneFoliageAO	= ri.Cvar_Get( "r_sceneFoliageAO",		"0.6",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFoliage: how much darker the canopy's inner leaves are (0 none, 1 black at its centre)" );
	r_sceneClouds		= ri.Cvar_Get( "r_sceneClouds",			"0",					CVAR_ARCHIVE_ND, "Render scene, r_sceneSky: cumulus 1.5 km up over the share of the sky given (0 off; 0.35 scattered)" );
	r_sceneCloudScale	= ri.Cvar_Get( "r_sceneCloudScale",		"2",					CVAR_ARCHIVE_ND, "Render scene, r_sceneClouds: the clouds' size (km)" );
	r_sceneCloudEdge	= ri.Cvar_Get( "r_sceneCloudEdge",		"0.15",					CVAR_ARCHIVE_ND, "Render scene, r_sceneClouds: how soft their edges are (0.01 hard, 1 a haze)" );
	r_sceneCloudLight	= ri.Cvar_Get( "r_sceneCloudLight",		"1.5",				CVAR_ARCHIVE_ND, "Render scene, r_sceneClouds: the light of their sunlit side, times the scene's sun" );
	r_sceneCloudWind	= ri.Cvar_Get( "r_sceneCloudWind",		"6 2",					CVAR_ARCHIVE_ND, "Render scene, r_sceneClouds: their drift, m/s (x y)" );
	r_sceneMoon			= ri.Cvar_Get( "r_sceneMoon",			"4",					CVAR_ARCHIVE_ND, "Render scene: CP2077's moon on the sky (r_sceneMoonImage), its light times this over its moonColor (alpha 2 at night, 0.005 by day); 0 none" );
	r_sceneMoonSize		= ri.Cvar_Get( "r_sceneMoonSize",		"0.0166",				CVAR_ARCHIVE_ND, "Render scene, r_sceneMoon: the disc's angular radius (radians; CP2077's moonSize)" );
	r_sceneMoonGlow		= ri.Cvar_Get( "r_sceneMoonGlow",		"0.006 100 0.0075 0.3",	CVAR_ARCHIVE_ND, "Render scene, r_sceneMoon: its glow's intensity and falloff (the power of the angle's cosine), then a halo at its rim (strength, width in radii) for CP2077's bloom; fitted to ref09's sky round the disc" );
	r_sceneMoonPath		= ri.Cvar_Get( "r_sceneMoonPath",		"35 -2.5 23.85",		CVAR_ARCHIVE_ND, "Render scene, r_sceneMoon: its path, latitude, declination (degrees) and its transit's hour on r_sceneClock (CP2077's, from ref09)" );
	r_sceneMoonLight	= ri.Cvar_Get( "r_sceneMoonLight",		"0",					CVAR_ARCHIVE_ND, "Render scene: 1: at night the map's sun (the moon: its shadows, its light in the haze) comes from the moon's disc (r_sceneMoonPath at r_sceneClock; with r_sceneFogMoon 0.4 to keep ref09's haze); 0 the map's q3gl2_sun" );
	r_sceneMoonImage	= ri.Cvar_Get( "r_sceneMoonImage",		"textures/skies/moon",	CVAR_ARCHIVE_ND, "Render scene, r_sceneMoon: the moon's image (alpha its disc), at the map's load; none found: no moon" );
	r_sceneClock		= ri.Cvar_Get( "r_sceneClock",			"-1",					CVAR_ARCHIVE_ND, "Render scene: the hour on the game's clock (0..24) the moon's place and colour follow; -1 the scene's own (01:00 at night, 13:00 by day)" );
	r_sceneHoloBloom	= ri.Cvar_Get( "r_sceneHoloBloom",		"0.3",					CVAR_ARCHIVE_ND, "Render scene: the share of the holograms' light (a blend material with a masked glow: CP2077's particles_hologram) that goes into the bloom pass, so their bright strokes halo as neons do; 0 none, as before" );
	r_sceneHoloGain		= ri.Cvar_Get( "r_sceneHoloGain",		"1",					CVAR_ARCHIVE_ND, "Render scene: multiplies the holograms' glow (lit and bloom alike); a calibration aid, 1 as the data says" );
	r_sceneHoloCurve	= ri.Cvar_Get( "r_sceneHoloCurve",		"1",					CVAR_ARCHIVE_ND, "Render scene: the holograms' mask to this power (colour and alpha; above 1 the faint fill between the strokes fades); a calibration aid, 1 as the data says" );
	r_sceneHoloDots		= ri.Cvar_Get( "r_sceneHoloDots",		"1",					CVAR_ARCHIVE_ND, "Render scene: the vector-board holograms (a masked glow with a maps/<map>.xscr template-5 record, dots on) drawn as CP2077's grid of round dots; 0 their mask alone, as before" );
	r_sceneNightOnlyByDay = ri.Cvar_Get( "r_sceneNightOnlyByDay", "0",					CVAR_ARCHIVE_ND, "Render scene, a day scene (maps/<map>.xenv time day): the power of the night-only lights (light type bit 20: street and wall lamps on fuses); 0 drops them. Read at map load" );
	r_sceneLightPairs	= ri.Cvar_Get( "r_sceneLightPairs",		"0",					CVAR_ARCHIVE_ND, "Render scene: a placement's template light and its instance data's light (light type bits 22 and 23): 0 both, 1 drops the template's, 2 the instance's. Read at map load" );
	r_sceneNonWorldLights = ri.Cvar_Get( "r_sceneNonWorldLights", "1",				CVAR_ARCHIVE_ND, "Render scene: the power of the lights whose channels leave out the world, no volume holding them (light type bit 19); 0 drops them. Read at map load" );
	r_sceneLightDedup	= ri.Cvar_Get( "r_sceneLightDedup",		"4",					CVAR_ARCHIVE_ND, "Render scene: drops a light equal to another (its record past its origin, its interior box and shape) within this many units (one entity placed twice at one point; 0 off). Read at map load" );
	r_sceneSpotScale	= ri.Cvar_Get( "r_sceneSpotScale",		"1",					CVAR_ARCHIVE_ND, "Render scene: the power of every spot light (light type bit 0); 0 drops them. Read at map load" );
	r_sceneDayOnlyAtNight = ri.Cvar_Get( "r_sceneDayOnlyAtNight", "1",					CVAR_ARCHIVE_ND, "Render scene, a night scene: the power of the day-only lights (light type bit 21: CP2077's sky portal fills, about 0.04 of their power there at night); 0 drops them. Read at map load" );
	r_sceneCharSun		= ri.Cvar_Get( "r_sceneCharSun",		"0",					CVAR_ARCHIVE_ND, "Render scene: the sun on models (r_sunlightMode 2, its cascades' shadows kept) as the scene shows its sun (r_sceneSun, its exposure and tone curve, as r_sceneCharLight gives them its lights), times this; a day scene's xenv sets it (0: the BSP's sun)" );
	r_sceneScreens		= ri.Cvar_Get( "r_sceneScreens",		"1",					CVAR_ARCHIVE_ND, "Render scene: multiplies the emissive of screens (material flag 0x200: ads, UI, televisions); read at map load" );
	r_sceneScreenLook	= ri.Cvar_Get( "r_sceneScreenLook",		"0",					CVAR_ARCHIVE_ND, "Render scene: 1 draws a screen's face (material flag 0x200: ads, UI, televisions) as CP2077's ad shaders do: depth layers, a pixel grid, its colours kept by the tone curve, more glow (r_sceneScreen*)" );
	r_sceneScreenAnim	= ri.Cvar_Get( "r_sceneScreenAnim",		"1",					CVAR_ARCHIVE_ND, "Render scene: screens with a maps/<map>.xscr record play their flipbooks at its frames per second (and, with r_sceneScreenLook, blink, cycle hue and flicker as it says); 0 holds each on its first frame" );
	r_sceneRotate		= ri.Cvar_Get( "r_sceneRotate",			"1",					CVAR_ARCHIVE_ND, "Render scene: CP2077's rotating meshes (maps/<map>.xrot: fans, propellers, the ad rings) turn on the game's clock; 0 holds them at their placement" );
	r_sceneScreenDepth	= ri.Cvar_Get( "r_sceneScreenDepth",	"0.02",					CVAR_ARCHIVE_ND, "Render scene, r_sceneScreenLook: the separation of a screen's image layers behind its face (UV units, as CP2077's LayersSeparation; 0 none)" );
	r_sceneScreenLayers	= ri.Cvar_Get( "r_sceneScreenLayers",	"0.25 0.06",			CVAR_ARCHIVE_ND, "Render scene, r_sceneScreenLook: the second and third image layers' strengths (CP2077's IntensityPerLayer)" );
	r_sceneScreenGrid	= ri.Cvar_Get( "r_sceneScreenGrid",		"360",					CVAR_ARCHIVE_ND, "Render scene, r_sceneScreenLook: a screen's pixel rows over its image (columns by the image's aspect; 0 no grid)" );
	r_sceneScreenGridDepth = ri.Cvar_Get( "r_sceneScreenGridDepth", "0.5",				CVAR_ARCHIVE_ND, "Render scene, r_sceneScreenLook: how dark the gaps between a screen's pixels are (0..1); the grid averages out where a cell is under a pixel" );
	r_sceneScreenHue	= ri.Cvar_Get( "r_sceneScreenHue",		"1",					CVAR_ARCHIVE_ND, "Render scene, r_sceneScreenLook: 1 runs a screen's light through the tone curve on its brightest channel, keeping its colours (0: per channel, as the rest)" );
	r_sceneScreenTone	= ri.Cvar_Get( "r_sceneScreenTone",		"1",					CVAR_ARCHIVE_ND, "Render scene: 1 runs the screens' light (ads, UI, televisions: material flag 0x200; ads' glows, 0x400) through the tone curve on its brightest channel (r_sceneScreenHue's share), keeping its colours, with or without r_sceneScreenLook; 0 per channel, as the rest" );
	r_sceneScreenGain	= ri.Cvar_Get( "r_sceneScreenGain",		"4.5",					CVAR_ARCHIVE_ND, "Render scene: multiplies the screens' light (material flags 0x200 and 0x400), live (r_sceneScreens, at load, multiplies their emissive before it)" );
	r_sceneFx			= ri.Cvar_Get( "r_sceneFx",				"1",					CVAR_ARCHIVE_ND, "Render scene: CP2077's placed particle emitters (maps/<map>.xfx: steam, smoke, dust), drawn with the blends; 0 none" );
	r_sceneFxDistance	= ri.Cvar_Get( "r_sceneFxDistance",		"60",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFx: metres an effect is drawn within when neither its placement nor its system gives a hide distance" );
	r_sceneFxMax		= ri.Cvar_Get( "r_sceneFxMax",			"20000",				CVAR_ARCHIVE_ND, "Render scene, r_sceneFx: the most particle slots drawn a frame (the nearest effects first)" );
	r_sceneFxLight		= ri.Cvar_Get( "r_sceneFxLight",		"1.5 0.5 1",				CVAR_ARCHIVE_ND, "Render scene, r_sceneFx: the particles' light gain, the haze colour's share of their ambient, and the local lights' share on unlit particles (CP2077's ref05 steam takes the street's red)" );
	r_sceneFxSoft		= ri.Cvar_Get( "r_sceneFxSoft",			"1",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFx: scales the particles' soft fade where they meet the world (0 none)" );
	r_sceneFxColumns	= ri.Cvar_Get( "r_sceneFxColumns",		"0",					CVAR_ARCHIVE_ND, "Render scene: with an .xfx's particles, 1 keeps the .xfog's steam columns (kind 1, their stand-ins) too; read at map load" );
	r_sceneFxAlpha		= ri.Cvar_Get( "r_sceneFxAlpha",		"0.5",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFx: scales the particles' opacity (CP2077's steam at ref05 and ref12 is a few faint plumes)" );
	r_sceneFxOnly		= ri.Cvar_Get( "r_sceneFxOnly",			"-1",					CVAR_TEMP, "Render scene, r_sceneFx: a look check: only the placements of this system index of maps/<map>.xfx drawn (-1: every one)" );
	r_sceneFxAccelDrag	= ri.Cvar_Get( "r_sceneFxAccelDrag",	"1",					CVAR_ARCHIVE_ND, "Render scene, r_sceneFx: a particle's acceleration moves it under the same drag as its velocity (it settles at the acceleration over the drag), as CP2077's steam rises and slows; 0: undamped (a vent's puffs can coast hundreds of metres in a straight line)" );
	r_sceneFxDebug		= ri.Cvar_Get( "r_sceneFxDebug",		"0",					CVAR_TEMP, "Render scene, r_sceneFx: 1 prints the effects and particle slots drawn every 60 frames" );
	r_sceneAutoExposure	= ri.Cvar_Get( "r_sceneAutoExposure",	"1",					CVAR_ARCHIVE_ND, "Render scene: eye adaptation: the frame metered (a log2 luminance histogram) and r_sceneExposure moved inside the hour's window (r_sceneExposureWindow) toward r_sceneExposureMeter's key, as CP2077 does; 0 the fixed r_sceneExposure" );
	r_sceneExposureWindow = ri.Cvar_Get( "r_sceneExposureWindow", "-1.2 1",				CVAR_ARCHIVE_ND, "Render scene, r_sceneAutoExposure: the EV range around r_sceneExposure (CP2077's exposureMin and exposureMax for the hour; an environment sets it: 06:05 -1.2 4.44, 13:00 -1.2 5.38)" );
	r_sceneExposureMeter = ri.Cvar_Get( "r_sceneExposureMeter", "-5.175 0.325",			CVAR_ARCHIVE_ND, "Render scene, r_sceneAutoExposure: the key (log2 luminance at r_sceneExposure) and the share of the way toward it the exposure moves, fitted to CP2077's matched views" );
	r_sceneExposureSpeed = ri.Cvar_Get( "r_sceneExposureSpeed", "4 7.5",				CVAR_ARCHIVE_ND, "Render scene, r_sceneAutoExposure: how fast the exposure adapts up and down, per second (CP2077's exposureAdaptationSpeedUp, Down)" );
	r_sceneExposureDebug = ri.Cvar_Get( "r_sceneExposureDebug", "0",					CVAR_TEMP, "Render scene: 1 prints the exposure's meter, target and state every 60 frames (and meters with r_sceneAutoExposure 0)" );
	r_sceneLightTubes	= ri.Cvar_Get( "r_sceneLightTubes",		"2",					CVAR_ARCHIVE_ND, "Render scene: CP2077's area lights (a version 6 scene's shapes) lit as line sources at their size: 2 the haze-only lights alone (the ring's halo; surfaces keep points), 1 every light, 0 none; read at map load" );
	r_sceneLightTubeScale = ri.Cvar_Get( "r_sceneLightTubeScale", "1",					CVAR_ARCHIVE_ND, "Render scene: multiplies the power of the lights with a shape (a version 6 scene's tubes: CP2077's area lights), tubes or points; read at map load (an A/B of their level)" );
	r_sceneScreenBloom	= ri.Cvar_Get( "r_sceneScreenBloom",	"0.25",					CVAR_ARCHIVE_ND, "Render scene: the share of the screens' light (material flags 0x200 and 0x400) that goes into the bloom pass (1 all, as before): their colours, not a white bloom, at a brighter r_sceneScreenGain" );
	r_sceneScreenGlow	= ri.Cvar_Get( "r_sceneScreenGlow",		"1",					CVAR_ARCHIVE_ND, "Render scene, r_sceneScreenLook: divides the glow's threshold for screens (1: as the rest)" );
	r_sceneFogLightKnee	= ri.Cvar_Get( "r_sceneFogLightKnee",	"0",					CVAR_ARCHIVE_ND, "Render scene: r_sceneFogLightMax's ceiling starts at this share of it (0..0.95); below it the haze's light is unchanged (0: from the start)" );
	r_sceneDayLamps		= ri.Cvar_Get( "r_sceneDayLamps",		"0",					CVAR_ARCHIVE_ND, "Render scene: in a day scene (maps/<map>.xenv time day), the scale on night-only emissives (material flag 0x100: lamp bulbs, lit rooms); read at map load" );
	r_sceneWindows		= ri.Cvar_Get( "r_sceneWindows",		"1",					CVAR_ARCHIVE_ND, "Render scene: multiplies the lit rooms behind windows (window_parallax_interior: rooms, dark share and colours from the source)" );
	r_sceneReflect		= ri.Cvar_Get( "r_sceneReflect",		"3",					CVAR_ARCHIVE_ND, "Render scene: screen-space reflections on wet surfaces through last frame, times their Fresnel (3 matched by eye to wet-street references; needs MSAA and the depth extract; 0 off)" );
	r_sceneSsrRays		= ri.Cvar_Get( "r_sceneSsrRays",		"2",						CVAR_ARCHIVE_ND, "Render scene: reflection rays a half-size texel traces each frame where it has history (1 or 2; without history always 2): one jittered ray settled at a 15% share showed as a dither on the wet ground" );
	r_sceneSsrBlend		= ri.Cvar_Get( "r_sceneSsrBlend",		"0.08",					CVAR_ARCHIVE_ND, "Render scene: the share of a new frame in the settled reflections (lower settles the noise more and trails more after a change)" );
	r_sceneReflectSteps	= ri.Cvar_Get( "r_sceneReflectSteps",	"32",					CVAR_ARCHIVE_ND, "Render scene: steps along each reflected ray (they grow toward r_sceneReflectFar; at half resolution, settled over frames)" );
	r_sceneReflectFar	= ri.Cvar_Get( "r_sceneReflectFar",		"150",					CVAR_ARCHIVE_ND, "Render scene: metres a reflected ray reaches" );
	r_sceneAmbient		= ri.Cvar_Get( "r_sceneAmbient",		"0.015 0.018 0.027",		CVAR_ARCHIVE_ND, "Render scene: linear sky ambient (r g b); the ground side is a third of it" );
	r_sceneGrade		= ri.Cvar_Get( "r_sceneGrade",			"1",					CVAR_ARCHIVE_ND, "Render scene: the map's colour grade (maps/<name>.xlut, the source game's LUT) over the frame, before the HUD (0 off .. 1 full; needs MSAA)" );
	r_sceneGradeCompute	= ri.Cvar_Get( "r_sceneGradeCompute",	"1",					CVAR_ARCHIVE_ND | CVAR_LATCH, SCENE_GRADE_COMPUTE_HELP );
	r_sceneGradeExposure = ri.Cvar_Get( "r_sceneGradeExposure",	"1.5",					CVAR_ARCHIVE_ND, "Render scene: scales the light the colour grade is given (the tone curve's input, after r_sceneExposure; 1.5 matches Kabuki's reference brightness)" );
	r_sceneHdr			= ri.Cvar_Get( "r_sceneHdr",			"1",					CVAR_ARCHIVE_ND, "Render scene: a map with a colour grade turns r_hdr 0's 8-bit colour target into r_hdr 2's float one at load, so its highlights reach the grade unclipped (0 keeps r_hdr's; read at map load)" );
	r_sceneAsync		= ri.Cvar_Get( "r_sceneAsync",			"1",					CVAR_ARCHIVE_ND, "Render scene: the wet ground's reflections traced on the compute queue beside the frame's post passes (a device with a compute-only queue; 0: in the frame)" );
	r_sceneInvocations	= ri.Cvar_Get( "r_sceneInvocations",	"0",					CVAR_TEMP, "Render scene (bench): print each frame the eye view's lit pass's vertex and fragment shader invocations and clipped primitives (SCENEINV lines)" );
	r_sceneShadowReceivers = ri.Cvar_Get( "r_sceneShadowReceivers", "1",				CVAR_ARCHIVE_ND, "Render scene: the near sun cascades leave out casters outside the eye's view whose shadows fall away from it (0: every caster in the cascade)" );
	r_sceneFogLightMax	= ri.Cvar_Get( "r_sceneFogLightMax",	"0.3",					CVAR_ARCHIVE_ND, "Render scene: a soft ceiling on the source's light the haze sends the eye (linear, before exposure), x / (1 + x / max) by its luminance, for the few lights of millions (signs, floodlights) that wash the view out; dim glows are hardly changed (0: none)" );
	r_sceneFogSegment	= ri.Cvar_Get( "r_sceneFogSegment",		"1",					CVAR_ARCHIVE_ND, "Render scene: each light's haze light averaged exactly along the eye's ray through each froxel (far lights at their full energy, near ones as before; r_sceneFogNearSamples no longer applies); 0 taken at the froxel's centre" );
	r_sceneFogNearSamples = ri.Cvar_Get( "r_sceneFogNearSamples", "1",				CVAR_ARCHIVE_ND, "Render scene: the haze's light within two slice lengths of a light taken as the mean of four points inside its cell instead of its centre (the steps at a light's apex; 0: the centre)" );
	r_sceneProxyPush	= ri.Cvar_Get( "r_sceneProxyPush",		"0.001",					CVAR_ARCHIVE_ND, "Render scene: far proxies (a near distance) drawn pushed away from the eye along its rays by this share of their distance times (1 + their near distance / 100 m), at most 2%, so the detail they stand for wins the depth test where both draw and a proxy starting further out sits behind one starting nearer (coplanar road proxies); 0.001 with a 150 m near is 42 cm at 170 m (0: as authored)" );
	r_sceneCharLight	= ri.Cvar_Get( "r_sceneCharLight",		"1",					CVAR_ARCHIVE_ND, "Render scene: characters (Ghoul2 models) lit by the scene's own lights and ambient at them, through its exposure and tone curve, instead of the BSP's light grid; the value scales it (0: the grid)" );
	r_sceneClusters		= ri.Cvar_Get( "r_sceneClusters",		"2",					CVAR_ARCHIVE_ND, "Render scene: the eye view's lights sorted into clusters of its frustum each frame (40 x 24 tiles, 32 depth slices), each fragment looping over its cluster's instead of its light grid cell's (0: the cells); 2 also where a cell kept only its strongest lights (a full cluster still uses its cell)" );
	r_sceneViewCull		= ri.Cvar_Get( "r_sceneViewCull",		"1",						CVAR_ARCHIVE_ND, "Render scene: the haze volumes that may reach the eye's view found once a frame, so the haze walks only those (0: every one, in order; the same image)" );
	r_sceneShaderStats	= ri.Cvar_Get( "r_sceneShaderStats",	"0",					CVAR_TEMP, "Render scene: print the driver's statistics (registers, occupancy) of its pipelines as they are made at map load" );
	r_sceneGpuDriven	= ri.Cvar_Get( "r_sceneGpuDriven",		"1",					CVAR_ARCHIVE_ND, "Render scene: GPU-driven draws (a Vulkan 1.2 device): each view's culled commands packed into lists drawn with one call each, every image in one array (0: a call per material)" );
	r_sceneDebug		= ri.Cvar_Get( "r_sceneDebug",			"0",					CVAR_TEMP | CVAR_CHEAT, "Render scene: 1 albedo, 2 normals, 3 lights per cell, 4 sun shadow; lighting shares 5 ambient, 6 the source's lights, 7 sun; 8 material index (4 bits per channel); 9 wet (red) and puddles (yellow); 10 reflections; 11 window rooms (dim: unlit), 12 window instances; 17 roughness dry (red) and wet (green), porosity (blue); 13 and 14 the world position modulo 256 and 65536 game units, 8 exact bits a channel; 15 the instance index, 16 the material index, 24 exact bits; 18 the albedo UV (fraction; blue outside [0, 1]), 19 the albedo texel as sampled, 20 the albedo's GPU-driven image index (24 bits; sceneimage names it)" );
	r_sceneVrs			= ri.Cvar_Get( "r_sceneVrs",			"0 0 0",				CVAR_ARCHIVE_ND, "Render scene: coarse shading of far surfaces, the distances in metres past which they shade once per 2 x 2 pixels: terrain, SpeedTree leaves, every other surface (0: never). The output stays at full resolution: edges and depth per pixel, only the colour within a block shared. Needs VK_KHR_fragment_shading_rate's primitive rates and the GPU-driven draws" );
	r_sceneTerrainFlat	= ri.Cvar_Get( "r_sceneTerrainFlat",	"0",					CVAR_TEMP | CVAR_CHEAT, "Render scene (bench): 1 draws Witcher 3 terrain in one colour, no texture reads (its geometry unchanged), to time its shading" );
	r_sceneSkip			= ri.Cvar_Get( "r_sceneSkip",			"0",					CVAR_TEMP | CVAR_CHEAT, "Render scene (bench): classes of materials not drawn in any view, a sum of 1 terrain (flag 0x1000), 2 SpeedTree leaves (0x800), 4 other cutouts, 8 other opaque, 16 the rest (blends, glass, water, emissives, decals); for timing what each costs" );
	ri.Cmd_AddCommand( "sceneinfo", R_SceneInfo_f, "Print the loaded render scene's counts" );
	ri.Cmd_AddCommand( "sceneimage", R_SceneImage_f, "Print a scene image by its GPU-driven index (r_sceneDebug 20), or a material's images (m <material>)" );
	ri.Cmd_AddCommand( "scenelshash", R_SceneLsHash_f, "Hash the streamed light shadow atlas per face now, into lshash.txt (with the faces still waiting to be drawn); redraw: draw every face again and compare, into lshash_gate.txt (needs r_sceneLightShadowHash 1 at map load)" );
	ri.Cmd_AddCommand( "scenecullcount", R_SceneCullCount_f, "Print per view what the scene's cull drew last frame (instances, LODs) against its slot's visible pool" );
}

void R_SceneShutdown( void )
{
	ri.Cmd_RemoveCommand( "sceneinfo" );
	ri.Cmd_RemoveCommand( "sceneimage" );
	ri.Cmd_RemoveCommand( "scenecullcount" );
	ri.Cmd_RemoveCommand( "scenelshash" );
	R_SceneFree();
}
