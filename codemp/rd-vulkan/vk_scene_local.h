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
vk_scene_local.h: the render scene's private header, shared by vk_scene.cpp (the scene itself: load, cull, draw, lights,
shadows, streaming), vk_scene_post.cpp (haze, reflections, exposure, grade), vk_scene_files.cpp (side files both level
ports write: water, particles, projected decals), vk_scene_cp77.cpp (CP2077's: haze volumes, traffic signals, rotating
meshes, screens) and vk_scene_w3.cpp (Witcher 3's: terrain, layer blends, billboard cards). Not for the rest of the
renderer: its hooks are in vk_local.h.
*/
#ifndef VK_SCENE_LOCAL_H
#define VK_SCENE_LOCAL_H

#include "tr_local.h"

#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <unordered_map>
#include <new>
#include <algorithm>

extern "C" {
extern const unsigned char scene_vert_spv[];
extern const unsigned int scene_vert_spv_size;
extern const unsigned char scene_frag_spv[];
extern const unsigned int scene_frag_spv_size;
extern const unsigned char scene_cull_comp_spv[];
extern const unsigned int scene_cull_comp_spv_size;
extern const unsigned char scene_occ_ms_comp_spv[];
extern const unsigned int scene_occ_ms_comp_spv_size;
extern const unsigned char scene_occ_1_comp_spv[];
extern const unsigned int scene_occ_1_comp_spv_size;
extern const unsigned char scene_fog_comp_spv[];
extern const unsigned int scene_fog_comp_spv_size;
extern const unsigned char scene_sky_vert_spv[];
extern const unsigned int scene_sky_vert_spv_size;
extern const unsigned char scene_fx_vert_spv[];
extern const unsigned int scene_fx_vert_spv_size;
extern const unsigned char scene_capture_comp_spv[];
extern const unsigned int scene_capture_comp_spv_size;
extern const unsigned char scene_meter_comp_spv[];
extern const unsigned int scene_meter_comp_spv_size;
extern const unsigned char scene_pdecal_vert_spv[];
extern const unsigned int scene_pdecal_vert_spv_size;
extern const unsigned char scene_pdecal_frag_spv[];
extern const unsigned int scene_pdecal_frag_spv_size;
extern const unsigned char scene_pdecal_frag_ms_spv[];
extern const unsigned int scene_pdecal_frag_ms_spv_size;
extern const unsigned char scene_capture_nocopy_comp_spv[];
extern const unsigned int scene_capture_nocopy_comp_spv_size;
extern const unsigned char scene_ssr_comp_spv[];
extern const unsigned int scene_ssr_comp_spv_size;
extern const unsigned char scene_grade_vert_spv[];
extern const unsigned int scene_grade_vert_spv_size;
extern const unsigned char scene_grade_frag_spv[];
extern const unsigned int scene_grade_frag_spv_size;
extern const unsigned char scene_grade_comp_spv[];
extern const unsigned int scene_grade_comp_spv_size;
extern const unsigned char scene_compact_comp_spv[];
extern const unsigned int scene_compact_comp_spv_size;
extern const unsigned char scene_vert_gd_spv[];
extern const unsigned int scene_vert_gd_spv_size;
extern const unsigned char scene_vert_gd_vrs_spv[];
extern const unsigned int scene_vert_gd_vrs_spv_size;
extern const unsigned char scene_frag_gd_spv[];
extern const unsigned int scene_frag_gd_spv_size;
extern const unsigned char scene_frag_gd_mask_spv[];
extern const unsigned int scene_frag_gd_mask_spv_size;
extern const unsigned char scene_frag_decal_spv[];
extern const unsigned int scene_frag_decal_spv_size;
extern const unsigned char scene_frag_decal_ms_spv[];
extern const unsigned int scene_frag_decal_ms_spv_size;
extern const unsigned char scene_frag_gdlit_spv[];
extern const unsigned int scene_frag_gdlit_spv_size;
extern const unsigned char scene_frag_gdlit_nodebug_spv[];
extern const unsigned int scene_frag_gdlit_nodebug_spv_size;
extern const unsigned char scene_frag_gdlit_cov_spv[];
extern const unsigned int scene_frag_gdlit_cov_spv_size;
extern const unsigned char scene_cluster_comp_spv[];
extern const unsigned int scene_cluster_comp_spv_size;
extern const unsigned char scene_sort_comp_spv[];
extern const unsigned int scene_sort_comp_spv_size;
extern const unsigned char scene_distant_comp_spv[];
extern const unsigned int scene_distant_comp_spv_size;
extern const unsigned char scene_lightfade_comp_spv[];
extern const unsigned int scene_lightfade_comp_spv_size;
extern const unsigned char scene_water_comp_spv[];
extern const unsigned int scene_water_comp_spv_size;
extern const unsigned char scene_scatter_comp_spv[];
extern const unsigned int scene_scatter_comp_spv_size;
extern const unsigned char scene_groupcull_comp_spv[];
extern const unsigned int scene_groupcull_comp_spv_size;
extern const unsigned char scene_rotate_comp_spv[];
extern const unsigned int scene_rotate_comp_spv_size;
extern const unsigned char scene_viewcull_comp_spv[];
extern const unsigned int scene_viewcull_comp_spv_size;
extern const unsigned char scene_want_comp_spv[];
extern const unsigned int scene_want_comp_spv_size;
}

// the frame slots alternate: the haze's scatter images (fogImage[3]) and the "the other slot is two frames ago" reads
// assume exactly two
static_assert( NUM_COMMAND_BUFFERS == 2, "the render scene assumes two frame slots" );

#define SCENE_VERSION			6		// 1-5 are read too (1: no light boxes; before 3 every light lights the haze fully; 4 is 3's layout
								// with CP2077's hide distances in pad bytes, zero where none is given: 3 reads as 4 without them;
								// 5 is 4's layout with an instance's near distance in its flags word, always 0 before; 6 adds
								// the lights' shapes, a 10th section, its header 88 bytes)
#define SCENE_MAX_VIEWS			8		// a main view, its three sun cascades, and room for more
#define SCENE_OCC_STATS_BYTES	384		// scene_cull.comp's Occ block: two cameras, the levels, eight counters
#define SCENE_MAX_WIND_TREES	1023	// r_sceneTreeWind: the SpeedTrees with wind a scene holds (maps/<map>.xw3b version 4)
#define SCENE_WIND_VEC4			16		// vec4s a tree's constants (and the header) in the frame's wind buffer (scene.vert)
#define SCENE_WIND_RECORD		320		// floats a tree's wind parameters in maps/<map>.xw3b version 4 (w3trees.py WIND_RECORD)
#define SCENE_MAX_DLIGHTS		32		// the game's dynamic lights (sabers, shots, effects) the scene is lit by
#define SCENE_MAX_SIGNALS		256		// traffic signals with a cycle (maps/<map>.signals); slot 0 is none
#define SCENE_SIGNAL_SEGS		8
#define SCENE_HISTORY_MIPS		5		// the reflection history's colour levels (half size, then /2 each)
#define SCENE_CELL_MAX_LIGHTS	48		// a cell keeps its strongest lights (power over distance)
#define SCENE_GRID_MAX			128		// cells per axis (xenjk: was 64; the merged Watson + next-area scene pack)
#define SCENE_FOG_W				240		// the haze light volume: froxels across the view (scene_fog.comp DIMS)
#define SCENE_FOG_H				135
#define SCENE_FOG_D				64		// depth slices, exponential from SCENE_FOG_NEAR to r_sceneFogFar
#define SCENE_FOG_NEAR			37.5f	// one metre
#define SCENE_CLUSTER_X			40		// the eye view's light clusters (scene_cluster.comp, scene.frag)
#define SCENE_CLUSTER_Y			24
#define SCENE_DIST_STRIDE		16		// a light grid cell's stand-ins: a count, then up to 15 (scene_distant.comp)
#define SCENE_CLUSTER_Z			32		// slices, exponential from SCENE_FOG_NEAR to r_sceneFogFar (the last open)
#define SCENE_CLUSTER_MAX		126		// lights per cluster
#define SCENE_CLUSTER_STRIDE	( 1 + SCENE_CLUSTER_MAX )	// its count, then a 32-bit light index each

enum { XS_VERTICES, XS_INDICES, XS_MESHES, XS_LODS, XS_PARTS, XS_MATERIALS, XS_INSTANCES, XS_LIGHTS, XS_BOXES, XS_SHAPES, XS_COUNT };

typedef struct { float center[3], radius; uint32_t firstLod, numLods, pad[2]; } xscnMesh_t;
typedef struct { uint32_t firstPart, numParts; float screen; uint32_t pad; } xscnLod_t;
typedef struct { uint32_t firstIndex, numIndices; int32_t vertexOffset; uint32_t material; } xscnPart_t;
// flags: bit 0 two-sided, bit 2 a masked glow (a lit material glowing where its albedo's alpha says), bit 1 a window (window_parallax_interior: tint holds its room's width and height in metres,
// the share of rooms dark, the colour temperature spread; window: its depth in metres and the atlas's rooms; zero before)
typedef struct { char image[3][64]; uint32_t kind, flags; float emissive[4], tint[4], window[2]; } xscnMaterial_t;
// hide (version 4): CP2077's forceAutoHideDistance in game units, the eye distance past which it is not drawn (0: none);
// nearDist (version 5, the flags word before, always 0): the eye distance it is not drawn within (0: none), a far proxy's
// start; slot: its traffic signal (scene.vert)
typedef struct { float m[12]; uint32_t mesh; float nearDist; uint32_t slot; float hide; } xscnInstance_t;
// type: bit 0 a cone, bit 1 it lights only the haze; boxed: confined to its box (version 2); fog: the share of its light
// the haze scatters (the source's scaleVolFog, version 3)
typedef struct { float origin[3], radius, color[3], power, dir[3], cosOuter, cosInner, type, boxed, fog; } xscnLight_t;
// version 6: a light's shape: half its tube (scene units; it runs origin - axis to origin + axis), its source's radius;
// zero: a point. Its record's radius is then its reach from the centre plus |axis|
typedef struct { float axis[3], sourceRadius; } xscnShape_t;

// SCENE_KIND_DECAL: a CP2077 mesh decal (a blend with flag 0x40, r_sceneDecals at load): in no list or pass of its own
// kind, drawn into the DBuffer after the eye's prepass (Scene_DrawDecals) and read by the lit pass
// SCENE_KIND_WATER: the converter's blend materials flagged 0x80 (r_sceneWater at load): drawn after the opaque ones and
// before the blends, over the lit bed, in no depth list or shadow (scene.frag's mode 9)
// SCENE_KIND_NONE: flag 0x2000, a stand-in the scene draws otherwise (CP2077's distant water patches, global_water_patch.mt:
// the ocean shader's far water as an opaque floor at the water's level; the scene's own water covers it): in no list
enum { SCENE_KIND_OPAQUE, SCENE_KIND_CUTOUT, SCENE_KIND_BLEND, SCENE_KIND_EMISSIVE, SCENE_KIND_DECAL, SCENE_KIND_WATER, SCENE_KIND_NONE };
enum { SCENE_PIPE_OPAQUE, SCENE_PIPE_CUTOUT, SCENE_PIPE_BLEND, SCENE_PIPE_EMISSIVE, SCENE_PIPE_DEPTH, SCENE_PIPE_DEPTH_CUTOUT, SCENE_PIPE_GLOW,
	SCENE_PIPE_SKY, SCENE_PIPE_SHADOW, SCENE_PIPE_SHADOW_CUTOUT, SCENE_PIPE_LSHADOW, SCENE_PIPE_LSHADOW_CUTOUT, SCENE_PIPE_WATER, SCENE_PIPE_COUNT };
enum { SCENE_SHADOW_NONE, SCENE_SHADOW_SUN, SCENE_SHADOW_LIGHT };	// Scene_GraphicsPipeline's target
// GPU-driven draws: each view's draw lists (scene_compact.comp's cmdInfo bits) and their pipelines
enum { SCENE_LIST_LIT, SCENE_LIST_DEPTH, SCENE_LIST_DEPTH_CUTOUT, SCENE_LIST_GLOW, SCENE_LIST_BLEND, SCENE_LIST_WATER, SCENE_LIST_COUNT };
enum { SCENE_GD_LIT, SCENE_GD_LIT_DEBUG, SCENE_GD_BLEND, SCENE_GD_DEPTH, SCENE_GD_DEPTH_CUTOUT, SCENE_GD_GLOW, SCENE_GD_SHADOW, SCENE_GD_SHADOW_CUTOUT, SCENE_GD_LIT_EQUAL,
	SCENE_GD_WATER, SCENE_GD_LSHADOW, SCENE_GD_LSHADOW_CUTOUT, SCENE_GD_COUNT };	// LSHADOW: the static light faces (optional)
#define SCENE_MODE_WATER		9		// scene.frag's kMode for water
enum { SCENE_VARIANT_BOUND, SCENE_VARIANT_GD, SCENE_VARIANT_GD_LIT, SCENE_VARIANT_GD_LIT_EQUAL };	// Scene_GraphicsPipeline: per-material binds or
																	// the GPU-driven layout (the lit list: depth tested, not written; EQUAL: r_sceneLitEqual)
#define SCENE_LISTS_HEADER		64		// a view's list buffer: the lists' counts, then their commands

#define SCENE_LIGHT_SHADOWS		4		// the light type's bit 2: the source casts shadows from it (enableLocalShadows)
#define SCENE_LS_LAYER			2048	// the light shadow atlas: a layer's size in texels
#define SCENE_LS_NEAR			9.375f	// a quarter metre: nearer than this to a light (its own fixture) casts nothing
#define SCENE_LS_MAX_FACES		8192

typedef struct {
	VkBuffer		buf;
	VkDeviceMemory	mem;
	VkDeviceSize	size;
} sceneBuffer_t;

typedef struct {
	image_t		*image[3];		// albedo, normal, ORM (white where none: imageBits says which are real)
	uint32_t	kind, imageBits;
	float		emissive[4];	// linear rgb x intensity
	float		tint[4];		// a window: its room (xscnMaterial_t)
	uint32_t	firstCmd, numCmds;
	uint32_t	window;			// a window: 2 | atlas rooms << 4 | depth in 1/16 m << 12 (scene.frag's info.w), else 0; 1 a
								// screen, 4 a masked glow, 8 | lamp << 4 a signal's lamp, 16 an ad's glow (flag 0x400), 32
								// a SpeedTree leaf (flag 0x800: r_sceneFoliage), 64 a masked glow with a hologram's dots (an
								// .xscr template-5 record: r_sceneHoloDots)
	image_t		*detail;		// a multilayered material's main layer, tiled (maps/<map>.xdet); white when none
	uint32_t	srcFlags;		// the scene file's material flags (r_sceneSkip's classes)
} sceneMaterial_t;

typedef struct {
	float		planes[5][4];
	vec3_t		origin;			// the LOD's eye (a cascade: its main view's)
	float		invTanHalfFovY;
	float		viewportHeight;
	float		lodScale;		// a cascade: r_sceneShadowLod
	float		minRadius;		// a cascade: casters smaller than this are not drawn
	int			cascade;		// 1..3: a sun cascade (orthographic), else 0
	vec3_t		axis[3];		// a main view: forward, left, up
	float		tanHalfFov[2];	// a main view: x, y
} sceneView_t;

// push constants of scene.vert / scene.frag and scene_cull.comp (128 B each)
typedef struct {
	float		viewProj[16];
	float		emissive[4];
	float		tint[4];
	uint32_t	info[4];
	float		eye[4];			// the view's origin; w: 1 for the view the haze's light and the reflections were made for
} scenePush_t;

typedef struct {
	float		planes[5][4];
	float		lod[4];
	float		cull[4];
	uint32_t	counts[4];
} sceneCullPush_t;

// scene.frag's Frame block (std140)
typedef struct {
	float		eye[4];
	float		gridOrigin[4];
	int32_t		gridDims[4];
	float		skyAmbient[4];
	float		groundAmbient[4];
	float		params[4];
	float		params2[4];		// x: glow, y: its threshold, z: tone curve, w: 1 when the light shadows are read
	float		sunMvp[3][16];	// the cascades' matrices (Vulkan clip space, y flipped)
	float		sunDir[4];		// toward the sun; w: 1 when the cascades were drawn this frame
	float		sunCol[4];		// linear rgb; w: cascade size in texels
	float		sunTexel[4];	// each cascade's texel in world units
	float		fog[4];			// the haze's display colour (sRGB 0..1); w: density per metre at the floor
	float		fog2[4];		// x: height (m) over which the density falls by e; y: the most it covers; z: local volumes; w: their scale
	float		fogFwd[4];		// the eye's forward axis; w: the light volume's near distance (units)
	float		fogLeft[4];		// w: tan(fovX / 2)
	float		fogUp[4];		// w: tan(fovY / 2)
	float		fogVol[4];		// x: log(far / near), y: light in the haze (0: the volume is off), z: phase g, w: metres
								// before the haze's colour fills in
	float		wet[4];			// x: wetness of up-facing surfaces, y: their share under puddles, z: a light's size (m),
								// w: the windows' brightness
	float		ssr[4];			// x: 1 when last frame's reflections are valid, y: reflection strength, w: which image holds them
	float		prevViewProj[16];	// last frame's camera (Vulkan clip space), which they were made with
	float		prevEye[4];
	float		prevFwd[4];
	float		fogAlbedo[4];	// the haze's albedo: tints the source's light it scatters (linear rgb); w: the moon's share
	float		rain[4];		// x: the rain view's face + 1 (0: no rain mask), y: the depth margin over a surface
	float		detail[4];		// x: the multilayered materials' tiled detail (r_sceneDetail, 0 off); y: 1 when the DBuffer
								// holds this frame's decals (Scene_DrawDecals); z, w: the eye's projection [10], [14]
	float		cluster[4];		// x: light cluster slices per unit of log depth, z: 1 when built this frame
	float		hazePrevEye[4];	// last frame's haze volume (r_sceneFogTemporal): its eye; w: 1 when the other scatter image holds it
	float		hazePrevFwd[4];	// its forward axis; w: its near distance
	float		hazePrevLeft[4];	// w: tan(fovX / 2)
	float		hazePrevUp[4];	// w: tan(fovY / 2)
	float		hazeJitter[4];	// this frame's sample point in its froxel (xyz, froxels); w: the new frame's share
	float		surface2[4];	// x: specular anti-aliasing's most (r_sceneSpecAA, 0 off); y: 1: window rooms by a 3D room cell
								// turned to the facade (r_sceneWindowCell)
	float		water[4];		// x: the water's clock (s), y, z: the water's lowest and highest z (units), w: ripple strength
	float		water2[4];		// x: clarity (scales each material's visibility), y: reflection strength, z: the ripples' shift
								// of the reflection (units)
	int32_t		waterBed[4];	// the bed's view depth under the water: the occlusion pyramid's level 0 this frame (the
								// prepass's farthest depth per 2 x 2 pixels): x its offset, y, z its size, w 1 when built
	float		dlightCount[4];	// x: the eye view's dynamic lights, y: their power (r_sceneDlights)
	float		dlights[SCENE_MAX_DLIGHTS][2][4];	// each: origin, radius; linear colour
	float		signals[SCENE_MAX_SIGNALS][4];	// per signal slot its lamps now: x red, y yellow, z green (0..1)
	float		skyZenith[4];	// r_sceneSky (a day scene's sky, linear): the zenith's colour; w 1 when on
	float		skyHorizon[4];	// the horizon's; w: the sun's disc and glow (r_sceneSkySun)
	float		screen[4];		// r_sceneScreenLook: x 1 on, y the grid's rows, z its gaps' darkness, w the layers' separation
	float		screen2[4];		// x the hue-keeping tone curve's share, y the glow threshold's divisor, z, w the layers' strengths
	float		screen3[4];		// x 1 when the screens' records animate (r_sceneScreenAnim), y their light's gain (r_sceneScreenGain),
								// z 1: their light through the hue-keeping tone curve (r_sceneScreenTone), w its share that blooms
								// (r_sceneScreenBloom)
	float		foliage[4];		// SpeedTree leaves (flag 0x800): x the canopy's share of the normal (r_sceneFoliage, 0 off), y the
								// wrap, z the sun through backlit leaves, w the inner leaves' darkening
	float		clouds[4];		// r_sceneClouds on r_sceneSky's sky: x the cover (0 off), y the clouds' size (km), z their edge's
								// softness, w their sunlit side's light (times the sun's)
	float		clouds2[4];		// x, y the wind (m/s)
	float		holo[4];		// x 1: the holograms' dots (r_sceneHoloDots), y the share of their light that blooms (r_sceneHoloBloom), z their
								// glow's gain (r_sceneHoloGain), w their mask's power (r_sceneHoloCurve)
	float		moon[4];		// r_sceneMoon: xyz toward the moon, w its angular radius (0: none)
	float		moonCol[4];		// its disc's light (linear rgb); w its glow's intensity (r_sceneMoonGlow x)
	float		moonGlow[4];	// x its glow's falloff (the power of the angle's cosine), y its rim's halo, z the halo's width in radii
	float		fx[4];			// r_sceneFx: x the particles' light gain, y the haze colour's share of their ambient, z their soft fade's
								// scale, w the local lights' share on unlit ones
	float		fx2[4];			// x the particles' opacity scale (r_sceneFxAlpha)
	float		wet2[4];		// r_sceneWetPorosity: x, y the roughness where porosity starts and ends (y <= x: off), z a soaked
								// porous surface's albedo scale, w the roughness past which nothing is mirrored
	float		waterWaves[4];	// x: 1 when binding 26 holds this frame's waves (r_sceneWaterWaves), yzw: the cascades' patches (m)
	float		waterWaves2[4];	// x: each cascade's slope variance, y: its texels across (256)
	float		skyGround[4];	// r_sceneSkyGround: rgb the distant land under the horizon (linear; all 0: the sky darkened, as
								// before), w the power of the zenith-to-horizon blend (r_sceneSkyCurve)
	float		cards[4];		// x: 1 when a SpeedTree card blends its two stored views (r_sceneCardBlend), 0 dithers them
	float		skySide[4];		// r_sceneSkySunSide: the horizon's colour toward a low sun, w its share
	float		skyGlow[4];		// r_sceneSkyGlow: the glow round the light (the moon by night), w its falloff
} sceneFrame_t;

// a traffic signal's cycle (maps/<map>.signals, cp2gltf SceneSignals): its group's program from its intersection's
// t = 0 (colour 0 green, 1 yellow, 2 red; seconds), that intersection's offset; a crossing shows its hand for red,
// blinking through yellow (the game's blink_01 curve), and its walking figure for green
typedef struct {
	qboolean	crossing;
	float		cycle, offset;
	int			numSegs;
	int			colour[SCENE_SIGNAL_SEGS];
	float		seconds[SCENE_SIGNAL_SEGS];
} sceneSignal_t;

// scene_ssr.comp's Params block (std140)
typedef struct {
	float		viewProj[16];
	float		prevViewProj[16];
	float		eye[4];			// w: frame number
	float		fwd[4];			// w: tan(fovX / 2)
	float		left[4];		// w: tan(fovY / 2)
	float		up[4];			// w: the farthest ray (units)
	float		prevEye[4];		// w: steps
	float		prevFwd[4];		// w: 1 when last frame's result is valid
	float		sky[4];			// rgb: the haze's colour as linear light; w: the share of a new frame
	float		wet[4];			// x: wetness, y: puddle share, z: exposure, w: tone curve
	float		opts[4];		// x: rays a texel with history (1 or 2)
	float		opts2[4];		// x: the cosine past which a surface is level ground (r_sceneSsrFlat; 2: never)
} sceneSsrParams_t;

typedef struct {
	qboolean		loaded;
	char			name[MAX_QPATH];
	uint32_t		numInstances, numMeshes, numCmds, numMaterials, numLights, numCellRefs, numEmissive, numBoxedLights, numFogVolumes;
	uint32_t		numAdGlows, numAdGlowsUnmasked;	// material flag 0x400 on an opaque or cutout kind; of them without a normal image
	uint32_t		numLeaves;		// material flag 0x800 (SpeedTree leaves) on an opaque or cutout kind
	uint32_t		numTerrain;		// material flag 0x1000 (terrain) on an opaque kind, pushed back (r_sceneTerrainPush)
	uint32_t		numHoloDots;	// masked glows with a hologram's dots (.xscr template 5)
	uint32_t		numHolograms;	// blend materials with a masked glow (info.w 4 or 64): also in the glow list
	uint64_t		numTriangles;
	vec3_t			mins, maxs;				// world bounds of every full instance's sphere (the light grid, the rain view)
	vec3_t			drawMins, drawMaxs;		// and with the far proxies (scene version 5) too: the views' far clip
	sceneMaterial_t	*materials;

	sceneBuffer_t	vertices, indices, instances, meshes, lods, partCmd, cmdTemplate, lights, cells, cellLights, lightBoxes,
					fogVolumes,		// the haze's local volumes (maps/<map>.xfog; one zeroed record when none)
					detailParams,	// per material: its detail's tiles per UV unit (0 none), normal, roughness, colour strength
					w3Stream;		// maps/<map>.xw3b's vertex stream (second UV, raw colour), 3 words a vertex; 16 B when none
	uint32_t		numDetailed;
	uint32_t		numW3Blend;		// the materials blending texture-array layers by vertex colour (maps/<map>.xw3b)
	sceneBuffer_t	frame[NUM_COMMAND_BUFFERS];
	void			*framePtr[NUM_COMMAND_BUFFERS];
	sceneBuffer_t	receivers[NUM_COMMAND_BUFFERS];		// scene_cull.comp's: the eye's four sides, the light's travel
	void			*receiversPtr[NUM_COMMAND_BUFFERS];
	sceneBuffer_t	wind[NUM_COMMAND_BUFFERS];			// r_sceneTreeWind: the header, then each SpeedTree's wind constants this frame
	void			*windPtr[NUM_COMMAND_BUFFERS];		// (draw set binding 30, scene.vert; SCENE_WIND_VEC4 vec4s each)
	uint32_t		windFrame;							// counts the frames drawn with wind (the kept sun cascades' key)
	sceneBuffer_t	indirect[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	sceneBuffer_t	visible[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];	// [f][0]: the slot's visible pool, every view's (the others unused)
	// the count-then-scan cull (scene_scatter.comp): per slot the pairs and touched LODs (header first), its header read
	// back for the host, the pool's capacity in pairs, the overflow last seen; the scatter's pipeline and sets
	sceneBuffer_t	cullPairs[NUM_COMMAND_BUFFERS], cullHdr[NUM_COMMAND_BUFFERS];
	uint32_t		*cullHdrPtr[NUM_COMMAND_BUFFERS];
	uint32_t		visCap[NUM_COMMAND_BUFFERS], visOverSeen[NUM_COMMAND_BUFFERS];
	VkDescriptorSetLayout	scatterLayout;
	VkPipelineLayout		scatterPipeLayout;
	VkPipeline				scatterPipe;
	VkDescriptorSet			scatterSet[NUM_COMMAND_BUFFERS];
	// the GPU's cull group lists (scene_groupcull.comp): the groups' boxes and largest radii, the pass, its set per view
	sceneBuffer_t			cullSubs;
	uint32_t				numSubs;
	VkDescriptorSetLayout	groupCullLayout;
	VkPipelineLayout		groupCullPipeLayout;
	VkPipeline				groupCullPipe;
	VkDescriptorSet			groupCullSet[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	VkDescriptorSet	cullSet[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	// the cull by instance groups (r_sceneCullGroups): per slot and view its groups in the frustum (count, then indices)
	sceneBuffer_t	cullGroups[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	uint32_t		*cullGroupsPtr[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	uint32_t		numGroups;
	// interiors (maps/<map>.xint): per slot the groups whose box holds the eye (the cull's), and the want's (preload)
	sceneBuffer_t	interiorIn[NUM_COMMAND_BUFFERS], interiorWant[NUM_COMMAND_BUFFERS];
	uint32_t		*interiorInPtr[NUM_COMMAND_BUFFERS], *interiorWantPtr[NUM_COMMAND_BUFFERS];
	sceneBuffer_t	rotPatch[NUM_COMMAND_BUFFERS];	// the rotating instances' phases at the slot's time base (maps/<map>.xrot), host
	float			*rotPtr[NUM_COMMAND_BUFFERS];
	// scene_rotate.comp: the rotating instances' placements, pivots, speeds, axes; the slot's time base and whether it
	// turned then; the pass (made on its first use)
	sceneBuffer_t	rotParams;
	double			rotT0[NUM_COMMAND_BUFFERS];
	int				rotTurn[NUM_COMMAND_BUFFERS];
	qboolean		rotBased[NUM_COMMAND_BUFFERS];
	VkDescriptorSetLayout	rotLayout;
	VkPipelineLayout		rotPipeLayout;
	VkPipeline				rotPipe;
	VkDescriptorPool		rotPool;
	VkDescriptorSet			rotSet[NUM_COMMAND_BUFFERS];
	uint32_t		numInteriors;
	// occlusion culling (r_sceneOcclusion): two far-depth pyramids in one buffer (scene_occ.comp; levels from half the
	// screen to one texel, sizes rounded up), each built from the eye's prepass depth after it (vk_scene_occlusion) in
	// alternate frames: phase 2 tests against this frame's, phase 1 of the next frame against it; phase 1's hidden
	// instances, the commands phase 2 touched, their dispatch sizes; per slot the cull's cameras, levels and counters (scene_cull.comp binding 8)
	sceneBuffer_t			occPyramid, occList, occArgs, occTouched;
	uint32_t				occLevels, occTotal, occOfs[16], occSize[16][2];
	sceneBuffer_t			occStats[NUM_COMMAND_BUFFERS];
	void					*occStatsPtr[NUM_COMMAND_BUFFERS];
	qboolean				occCounted[NUM_COMMAND_BUFFERS];	// this slot's last frame counted (its counters are read)
	qboolean				clusterCounted[NUM_COMMAND_BUFFERS];	// its light clusters' fallbacks counted (the Occ block's last two)
	uint32_t				occAt;			// sc.frameNo of the last pyramid (phase 1 reads it in the next frame)
	uint32_t				occRegion;		// its region (0, 1): this frame's is built into the other
	float					occViewProj[16];	// the camera it was made with
	vec3_t					occCamEye, occCamFwd;
	qboolean				occBuild;		// this frame splits the main pass after the eye's prepass (vk_scene_occlusion)
	// mesh decals (r_sceneDecals, SCENE_KIND_DECAL): the DBuffer, single-sample at the screen's size (1 x 1 when the scene
	// has none): 0 the decals' albedo (sRGB, blended) and coverage, 1 their world normal (0.5 + 0.5 n, blended) and its
	// weight, 2 the receiver's view depth they were drawn on (r16f; the lit pass applies them only to that surface)
	uint32_t				numDecals, decalW, decalH;
	// water (SCENE_KIND_WATER): its materials, the lowest and highest z of their instances' surfaces (scene_ssr.comp traces
	// that band when nothing else is wet), the clock its ripples run on
	uint32_t				numWater;
	float					waterLo, waterHi;
	VkImage					decalImage[5];	// albedo, normal, receiver depth; the projected decals' roughness, metalness, puddle; their emission
	VkDeviceMemory			decalMem[5];
	VkImageView				decalView[5];
	VkRenderPass			decalPass;
	VkFramebuffer			decalFb;
	VkDescriptorSetLayout	decalLayout;	// set 6 of the decal pipeline: the depth attachment
	VkPipelineLayout		decalPipeLayout;
	VkPipeline				decalPipe;
	VkShaderModule			pdecalVs;		// (while the projected decals' pipeline is made: Scene_DecalPipeline tells them by it)
	VkDescriptorSet			decalSet[NUM_COMMAND_BUFFERS];
	qboolean				decalDone;		// this frame's DBuffer is drawn (once, for the eye view)
	int						occSlot;		// this frame's phase-2 view slot (-1: no second phase)
	int						occEye;			// the eye view it belongs to
	qboolean				occDrawn;		// phase 2's lists are packed: the eye view's later passes draw them too
	qboolean				occOnly;		// vk_scene_draw draws phase 2's lists alone (the prepass's second half)
	VkDescriptorSetLayout	occLayout;
	VkPipelineLayout		occPipeLayout;
	VkPipeline				occPipeDepth;	// scene_occ.comp: level 0 from the depth attachment, the rest from the level below
	uint32_t				occBedOfs;		// r_sceneWaterBed: the water's full-size bed in the pyramids' buffer (0: none)
	VkDescriptorSet			occSetDepth[NUM_COMMAND_BUFFERS];
	VkDescriptorSet	drawSet[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	float			gridOrigin[4];
	int32_t			gridDims[4];

	VkDescriptorSetLayout	cullLayout, drawLayout;
	VkPipelineLayout		cullPipeLayout, drawPipeLayout;
	// the lit pass's views' visible lists in instance order (scene_sort.comp): per view and slot the LODs the cull
	// listed (their ranges got a second instance), and the sort's sets
	sceneBuffer_t			sortList[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	VkDescriptorSetLayout	sortLayout;
	VkPipelineLayout		sortPipeLayout;
	VkPipeline				sortPipe;
	VkDescriptorSet			sortSet[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	VkPipeline				sortTouchedPipe;	// the same shader sorting phase 2's touched commands
	VkDescriptorSet			sortTouchedSet[NUM_COMMAND_BUFFERS];
	VkDescriptorPool		pool;
	VkPipeline				cull;
	VkPipeline				pipe[SCENE_PIPE_COUNT];
	VkPipeline				pipeDepthFade, gdPipeDepthFade;	// the opaque depth prepass with the hide fade's dither (scene.frag mode 7)
	VkPipeline				gdPipeFadeMask[2];	// r_sceneHideDither 2: the GPU-driven opaque (7) and cutout (4) depth prepass, the fade as a sample mask
	VkPipeline				gdPipeLitCov;		// r_sceneCutoutCoverage: the EQUAL lit pass writing the cutouts' coverage (the prepass: gdPipeFadeMask[1])
	qboolean				cutoutCov;			// this frame takes the cutouts' coverage per sample (r_sceneCutoutCoverage)
	int						hideDither;		// this frame's r_sceneHideDither (2 only with GPU-driven draws, the EQUAL lit pass and MSAA)
	qboolean				fadeOn;			// this frame dithers the instances' hide and near distances (r_sceneHideFade)
	qboolean				shadowPipes;	// the cascade pipelines exist (the device has sun shadows)

	// the haze's light volume (scene_fog.comp): 0 the light scattered per metre, 1 integrated toward the eye, 2 the second
	// scatter (r_sceneFogTemporal: slot 1 writes it and slot 0 reads it as last frame's, and the other way round)
	VkImage					fogImage[3];
	VkDeviceMemory			fogMem[3];
	VkImageView				fogView[3];
	// r_sceneFogTemporal: last frame's haze volume (its eye, axes and depth range) and whether its scatter was written
	float					hazePrev[4][4];
	float					hazePrevLog;
	qboolean				hazeRan, hazeInit;
	VkSampler				fogSampler;
	VkDescriptorSetLayout	fogLayout;
	VkPipelineLayout		fogPipeLayout;
	VkPipeline				fogPipe[2];		// inject, integrate
	VkDescriptorSet			fogSet[NUM_COMMAND_BUFFERS];

	// screen-space reflections: last frame's colour and view depth at half size (scene_capture.comp)
	VkImage					historyImage;
	VkDeviceMemory			historyMem;
	VkImageView				historyView;
	VkImage					historyDepthImage;		// its depth alone, r16f (scene_ssr.comp's steps)
	VkDeviceMemory			historyDepthMem;
	VkImageView				historyDepthView;
	VkImageView				historyStoreView;	// level 0 alone (the capture writes it; the trace reads every level)
	uint32_t				historyMips;		// levels the trace reads a hit's colour from by its cone (1: none)
	VkSampler				mipSampler;			// linear across levels
	VkSampler				nearestSampler;
	VkDescriptorSetLayout	captureLayout;
	VkPipelineLayout		capturePipeLayout;
	VkPipeline				capturePipe;
	VkPipeline				fxPipe[2];		// r_sceneFx: the particles blended over, added (scene_fx.vert, scene.frag modes 10, 11)
	VkDescriptorSet			captureSet[NUM_COMMAND_BUFFERS];
	uint32_t				frameNo, capturedAt;	// the history is valid when it was captured in the last frame
	// device-level commands the loader does not export, fetched for this device (sc is cleared with the scene, so a
	// vid_restart's new device fetches them again)
	PFN_vkCmdUpdateBuffer	cmdUpdateBuffer;
	PFN_vkCmdBeginQuery		cmdBeginQuery;
	PFN_vkCmdEndQuery		cmdEndQuery;
	PFN_vkCmdDispatchIndirect	cmdDispatchIndirect;
	// scene_ssr.comp: the reflections traced on the history, settled over frames (two images, alternating)
	VkImage					reflectImage[2];
	VkDeviceMemory			reflectMem[2];
	VkImageView				reflectView[2];
	int						reflectIndex;			// the image the last capture wrote
	qboolean				reflectInit;			// both images are in GENERAL
	// r_sceneAO: the ambient occlusion scene_ssr.comp computes beside them (two images, alternating with the reflections')
	VkImage					aoImage[2];
	VkDeviceMemory			aoMem[2];
	VkImageView				aoView[2];
	VkDescriptorSetLayout	ssrLayout;
	VkPipelineLayout		ssrPipeLayout;
	VkPipeline				ssrPipe;
	VkDescriptorSet			ssrSet[NUM_COMMAND_BUFFERS];
	sceneBuffer_t			ssrParams[NUM_COMMAND_BUFFERS];
	void					*ssrParamsPtr[NUM_COMMAND_BUFFERS];
	float					prevViewProj[16];
	vec3_t					prevEye, prevFwd;
	qboolean				fogOn;			// this frame's volume was lit (the main pass reads it)
	qboolean				hazePending;	// culled this frame: vk_scene_haze lights the volume after the cascades
	int						fogEyeView;		// the view it was lit for

	// the colour grade (maps/<name>.xlut): its LUT, a copy of the resolved frame to read, the full-frame pass
	VkImage					gradeLut, gradeCopy;
	VkDeviceMemory			gradeLutMem, gradeCopyMem;
	VkImageView				gradeLutView, gradeCopyView;
	VkDescriptorSetLayout	gradeLayout;
	VkPipelineLayout		gradePipeLayout;
	VkPipeline				gradePipe;
	qboolean				gradeReady, gradeCompReady;	// each grade path's every resource made (a partial one is not used)
	VkDescriptorPool		gradePool;
	VkDescriptorSet			gradeSet;
	// the grade in place (r_sceneGradeCompute, a storage colour image): scene_grade.frag as compute over the frame,
	// per slot its set (the colour image's view written when it changes)
	VkDescriptorSetLayout	gradeCompLayout;
	VkPipelineLayout		gradeCompPipeLayout;
	VkPipeline				gradeCompPipe;
	VkDescriptorPool		gradeCompPool;
	VkDescriptorSet			gradeCompSet[NUM_COMMAND_BUFFERS];
	VkImageView				gradeCompView[NUM_COMMAND_BUFFERS];

	// static light shadows: the depth atlas (2D array), its render passes (clear, load), per light its first face, the faces
	VkImage					lsImage;
	VkDeviceMemory			lsMem;
	VkImageView				lsView;
	VkFormat				lsFormat;
	VkRenderPass			lsPass[2];
	VkSampler				lsSampler;
	uint32_t				lsLayers, numShadowLights, numShadowFaces;
	uint32_t				rainFace;		// the rain view's face + 1 in the same atlas (its own layer), 0 none
	float					rainMargin;		// its depth over a surface's own (in the view's depth units)
	qboolean				lsReadable;		// the atlas can be read back (r_sceneLightShadowHash was on when it was made)
	sceneBuffer_t			lsLights, lsFaces;
	// the faces' own instance lists (r_sceneLightShadowLists: occList's layout, culled with counts.z 3) and the commands'
	// lists for them (cmdInfo with only the opaque and cutout materials' depth lists: emissive ones cast nothing here)
	sceneBuffer_t			lsIds, cmdInfoLs;
	uint64_t				lsListed;		// instances listed over all faces
	float					lsListMs;		// building the lists, at load

	// GPU-driven draws (r_sceneGpuDriven): every scene image in one descriptor array (texSet), the materials (gdMaterials),
	// per command its lists and material (cmdInfo); per view and slot the packed lists (counts, then commands) and
	// each list slot's material, filled by scene_compact.comp
	qboolean				gd;				// created
	qboolean				gdFrame;		// this frame's views were packed (the draws use the lists)
	uint32_t				numTextures;
	VkDescriptorSetLayout	texLayout, compactLayout;
	VkDescriptorPool		texPool;
	VkDescriptorSet			texSet;			// this frame's (texSets[vk.cmd_index]: a streamed image's view changes)
	VkDescriptorSet			texSets[NUM_COMMAND_BUFFERS];
	VkPipelineLayout		gdPipeLayout, compactPipeLayout;
	VkPipeline				gdPipe[SCENE_GD_COUNT], compact;
	sceneBuffer_t			gdMaterials, cmdInfo, depthOrder;
	uint32_t				listBase[SCENE_LIST_COUNT], listCap[SCENE_LIST_COUNT], listTotal;
	sceneBuffer_t			lists[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS], listMaterial[NUM_COMMAND_BUFFERS][SCENE_MAX_VIEWS];
	sceneBuffer_t			blockSums[NUM_COMMAND_BUFFERS];	// the block-parallel packing's per-block list counts, then offsets
	uint32_t				compactBlocks;	// commands / 1024 rounded up (0: too many for its scan, the old packing only)
	VkDescriptorSet			compactSet[NUM_COMMAND_BUFFERS][1];	// per slot, the views' buffers in arrays

	// async compute (r_sceneAsync, a compute-only queue): the reflections traced on it beside the frame's post passes.
	// The frame's commands split after the capture: the first part is submitted (signalling captured), the rest goes
	// on in a second command buffer; the trace waits for captured and signals traced, which the next submit that reads
	// the reflections (the next frame's first) waits for
	VkCommandPool			asyncPool;
	qboolean				asyncReady;		// every async resource made (a partial set is freed with the scene, unused)
	VkCommandBuffer			asyncCb[NUM_COMMAND_BUFFERS], splitCb[NUM_COMMAND_BUFFERS], ownCb[NUM_COMMAND_BUFFERS];
	VkSemaphore				captured[NUM_COMMAND_BUFFERS], traced[NUM_COMMAND_BUFFERS];
	VkFence					asyncFence[NUM_COMMAND_BUFFERS];
	qboolean				asyncFenceUsed[NUM_COMMAND_BUFFERS];
	VkSemaphore				pendingWait;		// traced, not yet waited by a graphics submit
	qboolean				pendingFresh;		// traced this frame (the next frame's first submit waits for it)

	// the eye view's light clusters (r_sceneClusters, scene_cluster.comp; a device with subgroup operations): per slot
	// the clusters' light lists (read by scene.frag, draw set binding 16)
	sceneBuffer_t			clusters[NUM_COMMAND_BUFFERS], fogClusters[NUM_COMMAND_BUFFERS];	// the surfaces', the haze's
	sceneBuffer_t			viewLights[NUM_COMMAND_BUFFERS], viewLightCounts[NUM_COMMAND_BUFFERS];	// r_sceneClusterCull's list
	sceneBuffer_t			lightBounds, lightKinds;	// per light its sphere and kind (scene_cluster.comp's tile test)
	// scene_lightfade.comp (CP2077's light hide distances, scene version 4): per slot each light's (fade, shadow strength)
	// and its kind with the lights off masked out (the clusters' tile test reads these); (1, 1) and the kinds until it runs
	sceneBuffer_t			lightFade[NUM_COMMAND_BUFFERS], liveKinds[NUM_COMMAND_BUFFERS];
	qboolean				hasLightBoxes;
	VkDescriptorSetLayout	lightFadeLayout;
	VkPipelineLayout		lightFadePipeLayout;
	VkPipeline				lightFadePipe;
	VkDescriptorSet			lightFadeSet[NUM_COMMAND_BUFFERS];
	// the water's waves (scene_water.comp, r_sceneWaterWaves): three cascades' slopes in one rgba16f array (binding 26 of
	// every draw set; 1 x 1 without water), their spectra and the rows between the two passes
	VkImage					wavesImage;
	VkDeviceMemory			wavesMem;
	VkImageView				wavesView, wavesView0;	// every level (sampled); level 0 (scene_water.comp's storage)
	uint32_t				wavesLevels;
	VkSampler				wavesSampler;
	sceneBuffer_t			wavesH0, wavesRows;
	VkDescriptorSetLayout	wavesLayout;
	VkPipelineLayout		wavesPipeLayout;
	VkPipeline				wavesPipe;
	VkDescriptorSet			wavesSet;
	float					wavesSize[3];	// the cascades' patches (m)
	float					wavesVar;		// each cascade's slope variance (the same share each)
	// scene_distant.comp (r_sceneLightDistant): per slot each light grid cell's distant-light stand-ins (SCENE_DIST_STRIDE)
	sceneBuffer_t			distantCells[NUM_COMMAND_BUFFERS];
	uint32_t				numCells;
	qboolean				distantOn;	// this frame's stand-ins were listed (the frame's cluster.w)
	VkDescriptorSetLayout	distantLayout;
	VkPipelineLayout		distantPipeLayout;
	VkPipeline				distantPipe;
	VkDescriptorSet			distantSet[NUM_COMMAND_BUFFERS];
	VkDescriptorSetLayout	clusterLayout;
	VkPipelineLayout		clusterPipeLayout;
	VkPipeline				clusterPipe;
	VkDescriptorSet			clusterSet[NUM_COMMAND_BUFFERS];
	// the haze volumes that may reach the eye's frustum, per slot (scene_viewcull.comp, before the haze: scene_fog.comp
	// walks these; every index in order until the pass runs, or without it)
	sceneBuffer_t			volumeList[NUM_COMMAND_BUFFERS];
	sceneBuffer_t			viewCullCounts[NUM_COMMAND_BUFFERS];	// per workgroup its kept volumes (the first pass)
	uint32_t				viewCullGroups;		// workgroups of 256 over the volumes
	sceneBuffer_t			volumeSpheres;	// per haze volume its bounding sphere in scene space (centre, radius)
	VkDescriptorSetLayout	viewCullLayout;
	VkPipelineLayout		viewCullPipeLayout;
	VkPipeline				viewCullPipe;
	VkDescriptorSet			viewCullSet[NUM_COMMAND_BUFFERS];

	// the frame's registered views (front end), culled at vk_begin_frame
	int				numViews;
	sceneView_t		views[SCENE_MAX_VIEWS];
	float			dlights[SCENE_MAX_DLIGHTS][2][4];	// the eye view's dynamic lights (R_SceneAddView): origin, radius; colour
	int				numDlights;
	sceneSignal_t	signals[SCENE_MAX_SIGNALS];		// slot 1.. (maps/<map>.signals)
	int				numSignals;
	int				signalTime;						// the eye view's game time (ms), which the signals run on
	int				culledViews;			// views culled into the current command buffer
	qboolean		skyOn;					// r_sceneSky this frame: the sky triangle draws the sky
	uint32_t		skippedViews;			// of those, the kept cascades left out (r_sceneKeptCull), a bit a view
	int				eyeView;				// the first main view (its eye shades; cascades take its LOD), -1 none
	qboolean		sunOn;					// the eye view's frame drew sun cascades
	float			sunMvp[3][16];
	vec4_t			sunDir, sunCol;
	float			sunTexel[3];
} sceneState_t;

// the scene's state (vk_scene.cpp); each scene file names it sc
extern sceneState_t sceneState;
static sceneState_t &sc = sceneState;

// the material detail's vec4 indices travel as floats (materialDetail[].y, the lattice base): exact below 2^24
#define SCENE_DETAIL_EXACT	( 1u << 24 )

// r_scenePDecals: the projected decals (vk_scene_files.cpp; the DBuffer's pass in vk_scene.cpp draws them)
typedef struct {
	std::vector<image_t *>	img;
	std::vector<uint32_t>	mat;			// 28 words a material (version 2's; a version 1 material padded)
	std::vector<float>		rec;			// 20 floats a record (the file's, kept ones)
	std::vector<float>		bound;			// 5 a record: its centre, radius and hide distance (units, 0 none)
	std::vector<uint32_t>	cellStart, cellIdx;	// the records by grid cell (xy)
	std::vector<float>		cellBound;		// 4 a cell: its records' lowest and highest z, largest radius, largest hide (-1: one has none)
	float					gridLo[2];
	int						gridW, gridH;
	uint32_t				count;
	std::vector<uint32_t>	vis;			// the frame's
	sceneBuffer_t			recs, mats, visBuf[NUM_COMMAND_BUFFERS];
	uint32_t				*visPtr[NUM_COMMAND_BUFFERS];
	uint32_t				visCap;
	VkDescriptorSetLayout	layout;
	VkPipelineLayout		pipeLayout;
	VkPipeline				pipe;
	VkDescriptorPool		pool;
	VkDescriptorSet			set[NUM_COMMAND_BUFFERS];
	qboolean				written[NUM_COMMAND_BUFFERS];
	VkImageView				depthView[NUM_COMMAND_BUFFERS];	// the depth view each slot's set holds
	int						normalCount;	// r_scenePDecalNormal parsed at this modification count (0: not yet; a cvar's starts at 1)
	uint32_t				normalMode[2];	// its mode, its flip and soft bits
	double					drawn, runs, tested, cpuMs;	// r_scenePDecalDebug
	int						frames;
} scenePDecals_t;
extern scenePDecals_t scenePd;

// ---- shared between the scene files

// the cvars the other scene files read (vk_scene.cpp registers them, R_SceneRegister)
extern cvar_t *r_scene;
extern cvar_t *r_sceneAO;
extern cvar_t *r_sceneAOBlend;
extern cvar_t *r_sceneAORadius;
extern cvar_t *r_sceneAOSamples;
extern cvar_t *r_sceneAsync;
extern cvar_t *r_sceneAutoExposure;
extern cvar_t *r_sceneDebug;
extern cvar_t *r_sceneExposure;
extern cvar_t *r_sceneExposureDebug;
extern cvar_t *r_sceneExposureMeter;
extern cvar_t *r_sceneExposureSpeed;
extern cvar_t *r_sceneExposureWindow;
extern cvar_t *r_sceneEyeRelative;
extern cvar_t *r_sceneFog;
extern cvar_t *r_sceneFx;
extern cvar_t *r_sceneFxAccelDrag;
extern cvar_t *r_sceneFxColumns;
extern cvar_t *r_sceneFxDebug;
extern cvar_t *r_sceneFxDistance;
extern cvar_t *r_sceneFxMax;
extern cvar_t *r_sceneFxOnly;
extern cvar_t *r_sceneGrade;
extern cvar_t *r_sceneGradeCompute;
extern cvar_t *r_sceneGradeExposure;
extern cvar_t *r_sceneHdr;
extern cvar_t *r_scenePDecalDebug;
extern cvar_t *r_scenePDecalDistance;
extern cvar_t *r_scenePDecalMax;
extern cvar_t *r_scenePDecalNormal;
extern cvar_t *r_scenePDecalTerrain;
extern cvar_t *r_scenePDecals;
extern cvar_t *r_sceneReflect;
extern cvar_t *r_sceneReflectFar;
extern cvar_t *r_sceneReflectSteps;
extern cvar_t *r_sceneRotate;
extern cvar_t *r_sceneShadowLod;
extern cvar_t *r_sceneSignalAt;
extern cvar_t *r_sceneSignalShift;
extern cvar_t *r_sceneSignals;
extern cvar_t *r_sceneSsrBlend;
extern cvar_t *r_sceneSsrFlat;
extern cvar_t *r_sceneSsrRays;
extern cvar_t *r_sceneTonemap;
extern cvar_t *r_sceneWet;
extern cvar_t *r_sceneWetPuddles;

// vk_scene.cpp: buffers, shaders and sets
qboolean		Scene_CreateBuffer( sceneBuffer_t *b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, const char *name,
					qboolean bothQueues = qfalse );
void			Scene_DestroyBuffer( sceneBuffer_t *b );
qboolean		Scene_StaticBuffer( sceneBuffer_t *b, const void *data, VkDeviceSize size, VkBufferUsageFlags usage, const char *name );
VkShaderModule	Scene_Shader( const unsigned char *code, unsigned int size );
VkDescriptorSetLayout Scene_SetLayout( const VkDescriptorType *types, const VkShaderStageFlags *stages, int count,
					const uint32_t *arrays = NULL );
void			Scene_WriteSet( VkDescriptorSet set, const sceneBuffer_t **bufs, const VkDescriptorType *types, int count );
// vk_scene.cpp: a scene image by its path (tr.whiteImage when it is missing or empty; found says which)
image_t			*Scene_Image( const char *path, qboolean *found );
void			Scene_EyeViewProj( float out[16], const float proj[16], qboolean relative );
VkPipeline		Scene_DecalPipeline( VkShaderModule vs, VkShaderModule fs );	// (the mesh decals' and the projected decals')
void			Scene_ImageBarrier( VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
					VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage );
// vk_scene.cpp: a memory barrier in the frame's command buffer; the frame's split (r_sceneAsync)
void			Scene_Barrier( VkPipelineStageFlags src, VkAccessFlags srcAccess, VkPipelineStageFlags dst, VkAccessFlags dstAccess );
void			Scene_SplitFrame( int f );
// vk_scene.cpp: a look cvar as the map's environment (maps/<map>.xenv) sets it, while the cvar is as it was at the load
const cvar_t	*Scene_Env( const cvar_t *cv );
static inline const cvar_t *Env( const cvar_t *cv ) { return Scene_Env( cv ); }
// vk_scene.cpp: the material detail's slot images (the GPU-driven image array's start) and the .xscn's vertex count
// (0: a tiled or streamed scene)
extern std::vector<image_t *> sceneSlotImages;
extern uint32_t	sceneFileVertices;

// vk_scene_w3.cpp
void			Scene_LoadW3Blend( const char *scnName, std::vector<float> &params );
void			Scene_MarkCardMeshes( const char *scnName, xscnMesh_t *meshes, uint32_t numMeshes, const xscnLod_t *lods,
					const xscnPart_t *parts, uint32_t numMats );
uint32_t		Scene_WindTrees( void );
void			Scene_WindFrame( float *out, int timeMs, float strength, float gust, const float dirW[3] );
void			Scene_WindFree( void );

// vk_scene_cp77.cpp
void			Scene_LoadSignals( const char *scnName );
void			Scene_SignalStates( float out[SCENE_MAX_SIGNALS][4] );
qboolean		Scene_LoadFogVolumes( const char *scnName );
qboolean		Scene_LoadRotations( const char *scnName, const xscnInstance_t *inst, uint32_t numInst );
void			Scene_RotateFree( void );
void			Scene_RotateFrame( VkCommandBuffer cb, int f );
void			Scene_LoadScreens( const char *scnName, std::vector<float> &params );

// vk_scene_files.cpp
void			Scene_LoadPDecals( const char *scnName );
void			Scene_FreePDecalsGpu( void );
qboolean		Scene_CreatePDecals( void );
void			Scene_DrawPDecals( VkCommandBuffer cb, const int f, const int v, const float *viewProj );
void			Scene_LoadWater( const char *scnName, std::vector<float> &params );
void			Scene_LoadFx( const char *scnName, std::vector<float> &params );
void			Scene_DrawFx( VkCommandBuffer cb, const int f, const int v, scenePush_t *push );
void			Scene_FreeSideFilesCpu( void );
qboolean		Scene_FxPlaced( void );	// the map has particle placements (maps/<map>.xfx)

// vk_scene_post.cpp
qboolean		Scene_LoadGrade( const char *scnName );
void			Scene_FreeGrade( void );
void			Scene_MeterDestroy( void );
float			Scene_Exposure( void );
float			Scene_MeterFrame( int f );
// r_sceneGradeCompute's help (registered at video start here, and with the rest in R_SceneRegister)
#define SCENE_GRADE_COMPUTE_HELP	"Render scene: the colour grade runs in place over the resolved frame (a compute pass; the colour image a storage image too, set at video start) instead of over a copy (0)"

#endif	// VK_SCENE_LOCAL_H
