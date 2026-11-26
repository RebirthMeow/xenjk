#version 450
#ifdef GPU_DRIVEN
#extension GL_EXT_nonuniform_qualifier : require
#endif
#ifdef GPU_DRIVEN_LIT
// the lit pass of the GPU-driven draws tests depth before the shader (no writes; the prepass holds the nearest depth):
// EQUAL by default (r_sceneLitEqual; it keeps the 3080's coarse depth cull), which also stops a fragment the prepass's
// fade dither discarded; LESS_OR_EQUAL passes the same pixels and lets the shader discard that one again
layout(early_fragment_tests) in;
#endif
// Render scenes (vk_scene.cpp): baked material, the source's lights binned in a world grid (static), a hemisphere
// ambient, and the map's sun (a moon at night) through its shadow cascades. Images are sRGB-encoded; lighting runs
// linear and leaves through an exposure curve back to gamma space, as the rest of the frame is.

layout(constant_id = 0) const int kMode = 0;	// 0 opaque, 1 cutout, 2 blend, 3 emissive, 4 depth of a cutout, 5 glow of an emissive,
												// 6 the haze's light over the sky (scene_sky.vert, added); 7 depth with the hide fade's dither alone;
												// 8 the decal pass (the DECAL variant: mesh decals into the DBuffer); 9 water (blended over its bed)
layout(constant_id = 1) const int kSun = 0;		// 1: the device has sun cascades (set 4)

layout(push_constant) uniform Push {
	mat4 viewProj;
	vec4 emissive;	// linear rgb x intensity (kind emissive)
	vec4 tint;		// multiplies the albedo image (sRGB) and its alpha
	uvec4 info;		// x: its kind (GPU_DRIVEN: the draw list's base); y: 1 albedo, 2 normal, 4 ORM image bound; z: material
					// index; w: a window (2 | atlas rooms << 4 | depth in 1/16 m << 12; tint then holds its room: width and
					// height in metres, dark share, colour spread)
	vec4 eye;		// this view's origin; w: 1 for the view the haze's light volume and the reflections were made for
} pc;

// cosInnerType.y: bit 0 a cone, bit 1 lights only the haze (scene_fog.comp); z: has a box; w: its share in the haze
struct Light { vec4 posRadius; vec4 colorPower; vec4 dirCosOuter; vec4 cosInnerType; };
struct LightBox { vec4 r0, r1, r2, pad, shape; };	// scene space -> its interior's unit cube (rows); shape: a tube's (tubeFalloff)
layout(std430, set = 0, binding = 2) readonly buffer Lights { Light lights[]; };
layout(std430, set = 0, binding = 3) readonly buffer Cells { uvec2 cells[]; };	// first, count in cellLights
layout(std430, set = 0, binding = 4) readonly buffer CellLights { uint cellLights[]; };
layout(std430, set = 0, binding = 6) readonly buffer LightBoxes { LightBox boxes[]; };
layout(std140, set = 0, binding = 5) uniform Frame {
	vec4 eye;			// the first main view's origin (pc.eye: the drawn view's)
	vec4 gridOrigin;	// xyz: the grid's minimum corner; w: cell size
	ivec4 gridDims;
	vec4 skyAmbient;	// linear
	vec4 groundAmbient;
	vec4 params;		// x: light scale, y: exposure, z: emissive scale, w: debug view
	vec4 params2;		// x: glow scale, y: glow threshold (linear, after exposure), z: tone curve (0 exponential, 1 filmic)
	mat4 sunMvp[3];		// the cascades' matrices (Vulkan clip space)
	vec4 sunDir;		// toward the sun; w: 1 when this frame's cascades were drawn
	vec4 sunCol;		// linear rgb; w: cascade size in texels
	vec4 sunTexel;		// each cascade's texel in world units
	vec4 fog;			// the haze's display colour (sRGB 0..1); w: density per metre at the floor
	vec4 fog2;			// x: height (m) over which it thins by e; y: the most it hides
	vec4 fogFwd;		// the eye's forward axis; w: the light volume's near distance (units)
	vec4 fogLeft;		// w: tan(fovX / 2)
	vec4 fogUp;			// w: tan(fovY / 2)
	vec4 fogVol;		// x: log(far / near), y: light in the haze (0: the volume is not drawn), z: phase g,
						// w: metres before the haze's colour starts filling in (r_sceneFogStart)
	vec4 wet;			// x: wetness of up-facing surfaces (0 dry), y: the share of them under puddles, z: a light's size (m),
						// w: the windows' brightness
	vec4 ssr;			// x: 1 when last frame's reflections are valid, y: reflection strength, z: r_sceneDetailSlots (a
						// version 2 detail material's slots), w: which image holds them
	mat4 prevViewProj;	// last frame's camera (Vulkan clip space), which they were made with
	vec4 prevEye;		// its origin
	vec4 prevFwd;		// its forward axis
	vec4 fogAlbedo;		// (scene_fog.comp)
	vec4 rain;			// x: the rain view's face + 1 in shadowFaces (0: none), y: the depth margin over a surface
	vec4 detail;		// x: the multilayered materials' tiled detail (r_sceneDetail, 0 off)
	vec4 cluster;		// the eye view's light clusters (scene_cluster.comp): x slices per unit of log depth past the
						// haze's near distance (fogFwd.w), z: 1 when built this frame (r_sceneClusters)
	vec4 hazePrevEye;	// last frame's haze volume (r_sceneFogTemporal): its eye; w: 1 when the other scatter image holds it
	vec4 hazePrevFwd;	// its forward axis; w: its near distance (units)
	vec4 hazePrevLeft;	// w: tan(fovX / 2)
	vec4 hazePrevUp;	// w: tan(fovY / 2)
	vec4 hazeJitter;	// this frame's sample point in its froxel (xyz, froxels, each -0.5..0.5); w: the new frame's share
	vec4 surface2;		// x: geometric specular anti-aliasing's most (r_sceneSpecAA, 0 off); y: 1 when a window's room is
						// picked by a 3D room cell on a grid turned to the facade (r_sceneWindowCell); z: how much of the
						// sky its CP2077 probe's capture lacks a surface loses from its hemisphere (r_sceneProbeSky, 0 off); w: r_sceneWaterBed's
						// width (0: none; its offset in the pyramids' buffer is waterBed.w)
	vec4 water;			// x: the water's clock (s), y, z: its surfaces' lowest and highest z, w: ripple strength
	vec4 water2;		// x: clarity, y: reflection strength, z: the ripples' shift of the reflection (units), w: 1: the water's
						// own light on its flat surface and a third, broad ripple layer (r_sceneWaterBreakup)
	ivec4 waterBed;		// the bed's view depth: the occlusion pyramids' level 0 (binding 23) from x, y x z texels; w 1 when built
						// (past 1: also r_sceneWaterBed's full-size bed, at that offset)
	vec4 dlightCount;	// x: the eye view's dynamic lights (sabers, shots, effects), y: their power (r_sceneDlights); z: the
						// MSAA samples the cutouts' coverage comes from (r_sceneCutoutCoverage; 0 off: a pixel's one alpha test)
	vec4 dlights[64];	// SCENE_MAX_DLIGHTS x (origin, radius; linear colour)
	vec4 signals[256];	// SCENE_MAX_SIGNALS: per traffic signal slot its lamps now (x red, y yellow, z green; 0..1)
	vec4 skyZenith;		// r_sceneSky (a day scene's sky, linear): the zenith's colour; w 1 when on
	vec4 skyHorizon;	// the horizon's; w: the sun's disc and glow (r_sceneSkySun)
	vec4 screen;		// r_sceneScreenLook: x 1 on, y the grid's rows, z its gaps' darkness, w the layers' separation (UV)
	vec4 screen2;		// x the hue-keeping tone curve's share, y the glow threshold's divisor, z, w the layers' strengths
	vec4 screen3;		// x 1 when the screens' records animate (r_sceneScreenAnim), y their light's gain (r_sceneScreenGain), z 1:
						// their light through the hue-keeping tone curve (r_sceneScreenTone), w its share that blooms (r_sceneScreenBloom)
	vec4 foliage;		// SpeedTree leaves (info.w 32): x the canopy's share of the normal (r_sceneFoliage, 0 off), y the wrap,
						// z the sun through backlit leaves, w the inner leaves' darkening
	vec4 clouds;		// r_sceneClouds: x the cover (0 off), y their size (km), z their edge's softness, w their sunlit light
	vec4 clouds2;		// x, y the wind (m/s); z r_sceneAO's strength (0: off, or last frame's not valid)
	vec4 holo;			// x 1: the holograms' dots (r_sceneHoloDots), y the share of their light that blooms (r_sceneHoloBloom), z their glow's
						// gain (r_sceneHoloGain), w their mask's power (r_sceneHoloCurve)
	vec4 moon;			// r_sceneMoon: xyz toward the moon, w its angular radius (0: none)
	vec4 moonCol;		// its disc's light (linear rgb); w its glow's intensity
	vec4 moonGlow;		// x its glow's falloff (the power of the angle's cosine), y its rim's halo, z the halo's width in radii
	vec4 fx;			// r_sceneFx (scene_fx.vert): x the particles' light gain, y the haze colour's share of their ambient, z their
						// soft fade's scale (0 none), w the local lights' share on unlit ones
	vec4 fx2;			// x the particles' opacity scale (r_sceneFxAlpha); y 1 when a surface light keeps a shape (else the
						// lit loop reads none); z the hide fade's pattern (r_sceneHideDither: 0 interleaved gradient noise,
						// 1 blue noise, 2 the prepass's sample mask); w the colour samples a pixel (MSAA)
	vec4 wet2;			// r_sceneWetPorosity: x, y the roughness where porosity starts and ends (y <= x: off, every wet surface
						// one film), z a soaked porous surface's albedo scale, w the roughness past which nothing is mirrored
	vec4 waterWaves;	// x: 1 when waveTex holds this frame's waves (r_sceneWaterWaves), yzw: the cascades' patches (m)
	vec4 waterWaves2;	// x: each cascade's slope variance, y: its texels across
	vec4 skyGround;		// r_sceneSkyGround: rgb the distant land under the horizon (0: none), w r_sceneSkyCurve
	vec4 cards;			// x: 1 when a SpeedTree card blends its two stored views (r_sceneCardBlend), 0 dithers them
	vec4 skySide;		// r_sceneSkySunSide: rgb the horizon's colour on the sun's side (linear), w its share (0 none)
	vec4 skyGlow;		// r_sceneSkyGlow: rgb the glow round the scene's light (the moon by night; linear), w its
						// falloff (the power of the angle's cosine; 0 none)
	vec4 treeTint;		// r_sceneTreeTint: SpeedTree leaves' (info.w 32) albedo times rgb (linear), w their saturation
} fr;
layout(set = 0, binding = 26) uniform sampler2DArray waveTex;	// the water's waves (scene_water.comp): x, y the slopes
// scene_occ.comp's far-depth pyramids (linear view depth): the water's bed (fr.waterBed)
layout(std430, set = 0, binding = 23) readonly buffer Pyramids { float pyramids[]; };
layout(std430, set = 0, binding = 23) readonly buffer PyramidBits { uint pyramidBits[]; };	// (r_sceneWaterBed)
// scene_cluster.comp: per cluster its count, then its lights' indices two to a uint (16 bits each)
layout(std430, set = 0, binding = 16) readonly buffer Clusters { uint clusterLights[]; };
// scene_lightfade.comp: per light its fade with the eye's distance (CP2077's hide distance) and its shadow's strength
layout(std430, set = 0, binding = 17) readonly buffer LightFade { vec4 lightFade[]; };
// scene_distant.comp: per light grid cell its distant lights' stand-ins now (a count, then up to 15 lights)
layout(std430, set = 0, binding = 18) readonly buffer DistantCells { uint distantCells[]; };
const uint DIST_STRIDE = 16u;	// SCENE_DIST_STRIDE
const int CLUSTER_X = 40, CLUSTER_Y = 24, CLUSTER_Z = 32;	// SCENE_CLUSTER_X, _Y, _Z
const uint CLUSTER_STRIDE = 127u;							// 1 + SCENE_CLUSTER_MAX (a 32-bit index each)
// scene_ssr.comp, last frame (two images, alternating): rgb the light a wet surface mirrors, a its view depth
layout(set = 0, binding = 8) uniform sampler2D reflectTex0;
layout(set = 0, binding = 9) uniform sampler2D reflectTex1;
// scene_ssr.comp, last frame (r_sceneAO; alternating as the reflections): r the share of the ambient light reaching the
// surface, g its view depth
layout(set = 0, binding = 28) uniform sampler2D aoTex0;
layout(set = 0, binding = 29) uniform sampler2D aoTex1;
layout(set = 0, binding = 7) uniform sampler3D fogLightTex;	// scene_fog.comp: rgb the light scattered toward the eye

// the source's shadow-casting lights (vk_scene.cpp, "light shadows"): per light 0, or its first face + 1 with bit 31 set
// for six cube faces (+x, -x, +y, -y, +z, -z); each face is a tile of one depth atlas
struct ShadowFace { mat4 viewProj; vec4 tile; vec4 texel; };	// tile: uv scale, offset x, y, layer; texel: x its width per unit
																// of distance, y one texel in the face's uv
layout(std430, set = 0, binding = 10) readonly buffer LightShadows { uint lightShadows[]; };
layout(std430, set = 0, binding = 11) readonly buffer ShadowFaces { ShadowFace shadowFaces[]; };
layout(set = 0, binding = 12) uniform sampler2DArrayShadow lightShadowTex;

// the share of light li reaching P (d units from it, l toward it): offset off the surface (n) and toward the light by
// about a texel, then one bilinear comparison, or four (taps 4) for a softer edge
float lightShadow( const uint li, const vec3 P, const vec3 n, const vec3 l, const float d, const int taps )
{
	const uint s = lightShadows[li];
	if ( s == 0u || fr.params2.w < 0.5 )
		return 1.0;
	uint face = ( s & 0x7fffffffu ) - 1u;
	if ( ( s & 0x80000000u ) != 0u ) {
		const vec3 v = -l, a = abs( v );
		face += a.x >= a.y && a.x >= a.z ? ( v.x > 0.0 ? 0u : 1u ) : a.y >= a.z ? ( v.y > 0.0 ? 2u : 3u ) : ( v.z > 0.0 ? 4u : 5u );
	}
	const ShadowFace F = shadowFaces[face];
	const float texel = F.texel.x * d;
	const vec4 c = F.viewProj * vec4( P + n * ( 1.5 * texel ) + l * texel, 1.0 );
	if ( c.w <= 0.0 )
		return 1.0;
	const vec3 q = c.xyz / c.w;
	if ( any( greaterThan( abs( q.xy ), vec2( 1.0 ) ) ) || q.z >= 1.0 )
		return 1.0;
	const vec2 uv = q.xy * 0.5 + 0.5;
	const vec2 lo = vec2( 0.5 * F.texel.y ), hi = vec2( 1.0 - 0.5 * F.texel.y );
	if ( taps <= 1 )
		return texture( lightShadowTex, vec4( F.tile.yz + clamp( uv, lo, hi ) * F.tile.x, F.tile.w, q.z ) );
	float sum = 0.0;
	for ( int k = 0; k < 4; k++ ) {
		const vec2 o = vec2( ( k & 1 ) != 0 ? 0.75 : -0.75, ( k & 2 ) != 0 ? 0.75 : -0.75 ) * F.texel.y;
		sum += texture( lightShadowTex, vec4( F.tile.yz + clamp( uv + o, lo, hi ) * F.tile.x, F.tile.w, q.z ) );
	}
	return sum * 0.25;
}

// the share of the rain reaching P: the rain view (vk_scene.cpp, the scene from above at map load) sees nothing over P
// but its own surface (within the margin); a 3x3 tent of bilinear comparisons softens the drip lines (four taps
// further apart left the texels' steps)
float rainReach( const vec3 P )
{
	if ( fr.rain.x < 0.5 )
		return 1.0;
	const ShadowFace F = shadowFaces[uint( fr.rain.x ) - 1u];
	const vec4 c = F.viewProj * vec4( P, 1.0 );
	if ( any( greaterThan( abs( c.xy ), vec2( 1.0 ) ) ) )
		return 1.0;	// past the rain view (or, tiled, its held rectangle): wet, as without one
	// tiled (r_sceneRainTiles, texel.z: one over the layer's width in units): the world's tiles held with wrap-around
	const bool wrap = F.texel.z > 0.0;
	const vec2 uv = wrap ? fract( P.xy * F.texel.z ) : c.xy * 0.5 + 0.5;
	float sum = 0.0;
	for ( int k = 0; k < 9; k++ ) {
		const vec2 t = vec2( k % 3 - 1, k / 3 - 1 );
		const float w = ( 2.0 - abs( t.x ) ) * ( 2.0 - abs( t.y ) );
		const vec2 q = uv + t * F.texel.y;
		sum += w * texture( lightShadowTex, vec4( wrap ? fract( q ) : q, F.tile.w, c.z - fr.rain.y ) );
	}
	return sum / 16.0;
}

#ifdef GPU_DRIVEN
// GPU-driven draws (r_sceneGpuDriven): the material from the draw (scene.vert), every scene image in one array
struct Material { vec4 emissive; vec4 tint; uvec4 info; uvec4 tex; };	// as Push's; tex: albedo, normal, ORM, detail
layout(std430, set = 0, binding = 15) readonly buffer Materials { Material materials[]; };
layout(set = 1, binding = 0) uniform sampler2D sceneTex[];
layout(set = 2, binding = 0) uniform sampler2DArrayShadow sunShadowTex;	// layers 0..2: the cascades
layout(location = 8) flat in uint vMat;
#define SCENE_TEX_INDEX( i )	nonuniformEXT( i )	// a warp may hold more than one draw's fragments
#define ALBEDO_TEX	sceneTex[SCENE_TEX_INDEX( gTex.x )]
#define NORMAL_TEX	sceneTex[SCENE_TEX_INDEX( gTex.y )]
#define ORM_TEX		sceneTex[SCENE_TEX_INDEX( gTex.z )]
#define DETAIL_TEX	sceneTex[SCENE_TEX_INDEX( gTex.w )]
#else
layout(set = 1, binding = 0) uniform sampler2D albedoTex;
layout(set = 2, binding = 0) uniform sampler2D normalTex;
layout(set = 3, binding = 0) uniform sampler2D ormTex;
layout(set = 4, binding = 0) uniform sampler2DArrayShadow sunShadowTex;	// layers 0..2: the cascades
layout(set = 5, binding = 0) uniform sampler2D detailTex;	// a multilayered material's main layer, tiled (white: none)
#define ALBEDO_TEX	albedoTex
#define NORMAL_TEX	normalTex
#define ORM_TEX		ormTex
#define DETAIL_TEX	detailTex
#endif
// the drawn material's constants: the push constants', or GPU_DRIVEN its record's, read where they are used (copied
// into variables at the start they stayed in registers through the light loop: 93 against 80)
#ifdef GPU_DRIVEN
#define gEmissive	materials[vMat].emissive
#define gTint		materials[vMat].tint
#define gInfo		materials[vMat].info
#define gTex		materials[vMat].tex
#else
#define gEmissive	pc.emissive
#define gTint		pc.tint
#define gInfo		pc.info
#endif
// per material: x its detail's tiles per UV unit (0: none), y normal strength, z roughness range, w colour strength
layout(std430, set = 0, binding = 13) readonly buffer MaterialDetail { vec4 materialDetail[]; };

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec4 vTan;
layout(location = 3) in vec2 vUV;
layout(location = 4) in vec3 vLocal;		// mesh space (scene.vert)
layout(location = 5) in vec3 vLocalNrm;
layout(location = 6) flat in uint vInst;
layout(location = 7) flat in vec3 vUpW;
layout(location = 9) flat in uint vSignal;	// the instance's traffic signal slot (0: none) in bits 0-23; bit 25: no decals;
											// bits 28-31: the sky its CP2077 probe's capture lacks (0..15, tag_probesky.py)
layout(location = 10) flat in float vFade;	// the share of the instance drawn (scene.vert: its hide and near distances' fades)
layout(location = 11) in vec3 vCanopy;		// a SpeedTree leaf (info.w 32): from its mesh's bounding sphere's centre, in radii
#ifdef GPU_DRIVEN
layout(location = 12) in vec2 vUV1;	// Witcher 3 layer blends (maps/<map>.xw3b): the second UV set and the raw vertex colour
layout(location = 13) in vec4 vCol;
#endif
#ifdef GPU_DRIVEN_LIT
// r_sceneVrs: the triangle shades once per 2 x 2 pixels (scene.vert's VRS module; 0 otherwise). A cutout's coverage was
// settled at full rate by the depth prepass and the lit pass's EQUAL test keeps to it, so a coarse fragment neither
// discards nor narrows its sample mask (computed for one pixel, it left three of the block's four unwritten)
layout(location = 14) flat in uint vCoarse;
// its texture reads one mip sharper: a coarse fragment's implicit derivatives span 2 pixels, so the hardware chose a level
// up (a leaf's albedo then mixed in its atlas's dark clear texels: the canopies darkened); the terrain's
// explicit gradients are halved instead
#define COARSE_BIAS	( vCoarse != 0u ? -1.0 : 0.0 )
#define COARSE_GRAD	( vCoarse != 0u ? 0.5 : 1.0 )
#else
#define COARSE_BIAS	0.0
#define COARSE_GRAD	1.0
#endif

layout(location = 0) out vec4 outColor;
#ifdef DECAL
// mesh decals (vk_scene.cpp, Scene_DrawDecals): outColor the DBuffer's albedo and coverage, then its normal and weight,
// the receiver's view depth; the eye's depth attachment after its prepass (set 6; under MSAA, its first sample)
layout(location = 1) out vec4 outDecalNormal;
layout(location = 2) out vec4 outDecalDepth;
// the pass's surface and emission targets (the projected decals'): written zero at zero alpha, so the shared blend
// leaves them as they are (the pipeline has no independent blend: every attachment's state is the same)
layout(location = 3) out vec4 outDecalSurface;
layout(location = 4) out vec4 outDecalEmission;
#if defined( DEPTH_MS )
layout(set = 6, binding = 0) uniform sampler2DMS decalDepthTex;
#else
layout(set = 6, binding = 0) uniform sampler2D decalDepthTex;
#endif
#else
// the DBuffer (mesh decals, Scene_DrawDecals; the frame's detail.y: it holds this frame's): albedo (sRGB, blended by
// coverage) and coverage; world normal (0.5 + 0.5 n, blended by weight) and weight; the receiver's view depth
layout(set = 0, binding = 20) uniform sampler2D decalAlbedoTex;
layout(set = 0, binding = 21) uniform sampler2D decalNormalTex;
layout(set = 0, binding = 22) uniform sampler2D decalDepthTex;
layout(set = 0, binding = 24) uniform sampler2D decalSurfaceTex;	// projected decals': roughness, metalness, puddle, coverage
layout(set = 0, binding = 25) uniform sampler2D decalEmissionTex;	// their emission (linear, by its coverage; r_sceneEmissive's scale)
#endif

const float UNITS_PER_METRE = 37.5;
const float PI = 3.14159265;
const float WET_ROUGH = 0.2;	// wet ground's roughness at most; scene_ssr.comp spreads its rays by the same

vec3 toLinear( vec3 c ) { return pow( max( c, vec3( 0.0 ) ), vec3( 2.2 ) ); }

// a unit vector, or the fallback for a zero one (a degenerate stored normal would shade NaN)
vec3 safeNormalize( vec3 v, vec3 fallback ) { return dot( v, v ) > 1e-12 ? normalize( v ) : fallback; }
// t's unit part perpendicular to N; for a zero t or one along N (a mesh without UVs, a degenerate UV triangle) a
// perpendicular from the axis N leans least on
vec3 tangentOf( vec3 t, vec3 N )
{
	const vec3 t0 = t - N * dot( N, t );
	if ( dot( t0, t0 ) > 1e-8 )
		return normalize( t0 );
	return abs( N.x ) < 0.9 ? normalize( vec3( 1.0, 0.0, 0.0 ) - N * N.x ) : normalize( vec3( 0.0, 1.0, 0.0 ) - N * N.y );
}

// r_sceneHideDither 1, 2: a 32x32 blue noise tile (void and cluster, Gaussian 1.5; 256 levels of 4 pixels), 8 bits a texel
const uint BLUE_NOISE[256] = uint[256](
	0xdf7dc842u, 0xed568e31u, 0x7caa66ccu, 0xf99a88c3u, 0x8655c778u, 0x2f71ea41u, 0x46b400c0u, 0xeb0537bbu,
	0x4f09b028u, 0x28b8186cu, 0xfe4d037du, 0xd34b123fu, 0x05f11a32u, 0x83a221b1u, 0x257756e5u, 0x8ccf7ffau,
	0xbb9468e0u, 0x44d89ee5u, 0x21d7bb97u, 0xb066cb9fu, 0xdb6da382u, 0x48d45c94u, 0xa8da9c1cu, 0xa54b6113u,
	0x1ef24a12u, 0x650a7b3du, 0x598932f4u, 0x0b2ce771u, 0x4fc13fecu, 0xb509fb2fu, 0x8f3168eeu, 0x7621c4e7u,
	0xce7f32cbu, 0xc2a7fb5du, 0xf1ae1151u, 0xd98ca806u, 0x11911f5eu, 0x8b637eb8u, 0x4e08ca3bu, 0xf8b33a6fu,
	0x0599ad61u, 0x832536b6u, 0x46ce6ce2u, 0xb553347eu, 0x6ff3cb77u, 0x15cd41deu, 0xb5fe78a9u, 0x8a0398d4u,
	0xe851dd26u, 0x15db936eu, 0x1c8e2da1u, 0x14f8bee0u, 0x5634a047u, 0xf59724a3u, 0x1688592bu, 0x44ec5829u,
	0x217712c0u, 0x755ac845u, 0x5ec6eb40u, 0x89276b9bu, 0xd18001e3u, 0x4c73e70eu, 0x9c3ed6b9u, 0x6daf7ddfu,
	0xaf8cfd9fu, 0xf7a50af0u, 0xad4c02b8u, 0xa4d03b10u, 0x2cffbf63u, 0x04c360b2u, 0x6aec1d92u, 0xda0e35c1u,
	0xd55b3c29u, 0x2a4e842fu, 0xfb856794u, 0x50f178cau, 0x936d3e1fu, 0xf0398748u, 0x07ad5c7cu, 0x5292f64du,
	0x9b01ba82u, 0x11e4bf69u, 0x2033dfc6u, 0xb70c9649u, 0x1aa7df85u, 0x9b23cdedu, 0x7bc830d8u, 0xcd6722a6u,
	0x49dc72f3u, 0x5f73a81cu, 0xb9a27c40u, 0x6e2fe362u, 0xba5507c9u, 0x6aab0d78u, 0x41fc1650u, 0x12b2e18au,
	0xee8f2446u, 0x98fd38cbu, 0x05f519d4u, 0xebac7fd2u, 0x33f79a3au, 0xf442e162u, 0xda65a0beu, 0x9b35590bu,
	0x0eae60bcu, 0x2b138659u, 0x906c53aeu, 0x14562441u, 0x881dd964u, 0x842a97d2u, 0x268e3800u, 0xd56df0c2u,
	0x7539e207u, 0xe54ddb9eu, 0xde34bf85u, 0xc4fd9fc7u, 0xaa477793u, 0x70b35208u, 0x74ace9cau, 0x7c28934fu,
	0xf7c58854u, 0x64c7ac27u, 0x791eef0au, 0x4a830c5fu, 0x5deeb429u, 0xe41dfdc3u, 0xd51a475bu, 0xffa614b7u,
	0x4a17a131u, 0x7a3d036bu, 0xb39649a6u, 0xe3bb30f3u, 0x1634d302u, 0x9d378b75u, 0x64fa850du, 0xc948ea3bu,
	0x80ec62b4u, 0xf990e8beu, 0x105ad12du, 0x5a71a843u, 0xa2697e98u, 0x6bd747e0u, 0x982ca7c2u, 0x1b738904u,
	0x2ecf068eu, 0xb1541b99u, 0xe0ba8318u, 0xef15d48fu, 0x24f5c240u, 0x23ac0ab9u, 0xe376cf51u, 0xe65bd1bdu,
	0xaa4676d7u, 0xd939cc5fu, 0x6b35e76eu, 0x2c815220u, 0x3b5510afu, 0x90f85c82u, 0x57153ce6u, 0x38a42645u,
	0x0ff7b754u, 0x0c8a76e4u, 0xa5004999u, 0xd89ebff9u, 0xe6cc8f65u, 0x7031c69cu, 0xadf68402u, 0x13fa7095u,
	0x91681fa0u, 0xc0fe2542u, 0x79c7f260u, 0x4b073a5au, 0x067922ffu, 0xcd4d1b68u, 0x2066bca3u, 0x80c508ddu,
	0xdd2ec9edu, 0x1a4f9fbcu, 0x17872aa9u, 0x72e58dd0u, 0xd543a3bau, 0xe195efb3u, 0x8dd6442au, 0x3c61b436u,
	0x81507394u, 0x7bd26303u, 0x4eb7dc42u, 0x33b023f0u, 0x28eb5e17u, 0x17633a87u, 0xf20d5d7eu, 0x19e88652u,
	0xa6d60bbdu, 0xeeb236f4u, 0x376e0d92u, 0xd981629du, 0x517fc492u, 0xf4c510abu, 0x70c09ab0u, 0x4ad027a4u,
	0x2240fc65u, 0x2e138c6fu, 0xc1f9a45bu, 0x0f44cc04u, 0xcf0239efu, 0x489172fbu, 0x3f1ed330u, 0xab7a01dcu,
	0x5cb18d1eu, 0xc353e6ceu, 0x552680deu, 0x57aae27au, 0x9766b674u, 0x04db581cu, 0x95ff5779u, 0x35f257b8u,
	0x0fc474e4u, 0x71ab3d99u, 0xe9af3d06u, 0xfc2d8d1bu, 0x3ee8229cu, 0xc1a12bb2u, 0x6a0b8be9u, 0x9ec5872du,
	0xf3503208u, 0xfc198429u, 0x9669d691u, 0x1169bd3eu, 0xd78a4eceu, 0x6743ed7au, 0xca4bb627u, 0x614318e9u,
	0x7bdca9d3u, 0xbc60dab6u, 0x09c9204cu, 0x82d84cf5u, 0x5e09be37u, 0x1489c818u, 0x9d36dea9u, 0x81f6ae74u,
	0x643c9016u, 0x2ea54500u, 0xb55886e1u, 0xb123a272u, 0xf8a76ceau, 0xfa539a33u, 0xd21d5f7du, 0xbd2b540eu,
	0xa11ffe58u, 0x0f75f6c6u, 0x19ea389du, 0x015de230u, 0xd1259645u, 0xd00cb96au, 0x67f1953fu, 0x6fa0dd8bu );

#if defined( FADE_SAMPLEMASK ) || defined( CUTOUT_COVERAGE )
// r_sceneCutoutCoverage: a cutout's coverage per MSAA sample from its alpha (alpha to coverage) in place of one test per
// pixel, which MSAA cannot smooth (leaves, chain-link fences and grates had no anti-aliasing at all).
// The alpha at each sample is the pixel centre's carried along its screen gradient to the sample's place
// (Vulkan's standard sample locations), tested against 0.5: an edge's samples split where the texture's edge crosses the
// pixel, the same way every frame (no noise). The depth prepass (FADE_SAMPLEMASK variant, mode 4) keeps those samples;
// the lit pass (CUTOUT_COVERAGE) narrows its writes to the same ones, computed the same way ('precise'), so a cutout lying
// on another surface still leaves its clear texels to it. A partly covered pixel is shaded once, at its centre.
const vec2 MS2[2] = vec2[]( vec2( 0.75, 0.75 ), vec2( 0.25, 0.25 ) );
const vec2 MS4[4] = vec2[]( vec2( 0.375, 0.125 ), vec2( 0.875, 0.375 ), vec2( 0.125, 0.625 ), vec2( 0.625, 0.875 ) );
const vec2 MS8[8] = vec2[]( vec2( 0.5625, 0.3125 ), vec2( 0.4375, 0.6875 ), vec2( 0.8125, 0.5625 ), vec2( 0.3125, 0.1875 ),
	vec2( 0.1875, 0.8125 ), vec2( 0.0625, 0.4375 ), vec2( 0.6875, 0.9375 ), vec2( 0.9375, 0.0625 ) );
uint cutoutCoverage( float a )
{
	const int N = int( fr.dlightCount.z + 0.5 );
	precise const vec2 g = vec2( dFdx( a ), dFdy( a ) );
	uint m = 0u;
	for ( int i = 0; i < N; i++ ) {
		const vec2 s = ( N == 2 ? MS2[i & 1] : N == 4 ? MS4[i & 3] : MS8[i & 7] ) - 0.5;
		precise const float at = a + g.x * s.x + g.y * s.y;
		if ( at >= 0.5 )
			m |= 1u << uint( i );
	}
	return m;
}
#endif

// the hide fade's threshold at this pixel, [0, 1): interleaved gradient noise (0), else the blue noise tile
float hideNoise()
{
	if ( fr.fx2.z < 0.5 )
		return fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) );
	const ivec2 p = ivec2( gl_FragCoord.xy ) & 31;
	const uint i = uint( p.y * 32 + p.x );
	return ( float( ( BLUE_NOISE[i >> 2u] >> ( ( i & 3u ) * 8u ) ) & 255u ) + 0.5 ) / 256.0;
}

// a tube light (scene version 6; its box record's shape: xyz half its segment, w its source's radius, scene units; zero
// a point): Q its point nearest P, which stands for it (direction, cone, the falloff window); returns its falloff as a
// line source's, power over the length x (atan((L/2 - s) / h) + atan((L/2 + s) / h)) / (L h), s P's place along the
// line from its centre, h P's distance from it with near2 under it (or the source's radius squared, where larger), in
// metres: a point's 1 / (d^2 + near2) as L goes to 0
float tubeFalloff( const vec3 C, const vec4 S, const vec3 P, const float near2, out vec3 Q )
{
	const float a = length( S.xyz );
	const vec3 rel = P - C;
	const float along = dot( rel, S.xyz ) / a;
	Q = C + S.xyz * ( clamp( along, -a, a ) / a );
	const float s = along / UNITS_PER_METRE, hl = a / UNITS_PER_METRE, r = S.w / UNITS_PER_METRE;
	const float h = sqrt( max( dot( rel, rel ) / ( UNITS_PER_METRE * UNITS_PER_METRE ) - s * s, 0.0 ) + max( near2, r * r ) );
	return ( atan( ( hl - s ) / h ) + atan( ( hl + s ) / h ) ) / ( 2.0 * hl * h );
}

// The frame's encoding of light (after exposure): the tone curve and gamma 2.2 up to TONE_TOP; past it, an e-fold of
// light per TONE_LOG. A float colour target (r_hdr 2) keeps that headroom, so the colour grade (scene_grade.frag,
// the same functions) gets back the light that was drawn, neon cores included; an 8-bit one clips it where the tone
// curve alone would. The rest of the frame (models, effects) draws in 0..1 and reads as before.
const float TONE_TOP = 0.999, TONE_LOG = 0.1;
vec3 toneCurve( vec3 c )
{
	if ( fr.params2.z > 0.5 )
		c = clamp( ( c * ( 2.51 * c + 0.03 ) ) / ( c * ( 2.43 * c + 0.59 ) + 0.14 ), 0.0, 1.0 );	// ACES fit (Narkowicz): a toe and a shoulder
	else
		c = vec3( 1.0 ) - exp( -c );
	return pow( c, vec3( 1.0 / 2.2 ) );
}
vec3 toneInverse( vec3 y )
{
	y = pow( clamp( y, 0.0, TONE_TOP ), vec3( 2.2 ) );
	if ( fr.params2.z > 0.5 ) {
		const vec3 a = 2.51 - 2.43 * y, b = 0.03 - 0.59 * y, c = -0.14 * y;
		return ( -b + sqrt( b * b - 4.0 * a * c ) ) / ( 2.0 * a );
	}
	return -log( 1.0 - y );
}

// linear light (before exposure) -> the frame's encoding
vec3 toDisplay( vec3 c )
{
	c *= fr.params.y;
	const float top = toneInverse( vec3( TONE_TOP ) ).x;
	return mix( toneCurve( c ), TONE_TOP + TONE_LOG * log( max( c, vec3( 1e-6 ) ) / top ), greaterThan( c, vec3( top ) ) );
}

// the same on the light's brightest channel, its colours kept (a screen's, r_sceneScreenHue): per channel a saturated
// ad's channels all climb toward white
vec3 toDisplayHue( vec3 c )
{
	c *= fr.params.y;
	const float m = max( max( c.r, c.g ), max( c.b, 1e-6 ) );
	return pow( c * ( pow( toneCurve( vec3( m ) ).x, 2.2 ) / m ), vec3( 1.0 / 2.2 ) );
}

// r_sceneScreenLook: a screen's face (material flag 0x200: ads, UI, televisions; info.w 1) as CP2077's ad shaders draw it
// (television_ad, metal_base_ui, parallaxscreen): its image again behind itself (IntensityPerLayer at LayersSeparation:
// the screen's depth, each layer only within the image) and a grid of pixel cells (PixelsHeight, BlackLinesIntensity,
// ScanlinesDensity) that averages out to the image where a cell is under a pixel. Linear light from the albedo (sRGB)
bool screenOn() { return gInfo.w == 1u && fr.screen.x > 0.5; }

// a screen's light as a class (r_sceneScreenTone, r_sceneScreenGain, r_sceneScreenBloom): a screen (info.w 1) or an
// ad's glow (info.w 16, CP2077's billboard proxies), its light times the gain, through the hue-keeping tone curve
// (r_sceneScreenHue's share) while r_sceneScreenTone or r_sceneScreenLook is on; in the bloom pass a share of it
bool screenMat() { return gInfo.w == 1u || gInfo.w == 16u; }
float screenGain() { return screenMat() ? fr.screen3.y : 1.0; }
vec3 screenDisplay( const vec3 c )
{
	return screenMat() && ( fr.screen3.z > 0.5 || fr.screen.x > 0.5 ) ? mix( toDisplay( c ), toDisplayHue( c ), fr.screen2.x ) : toDisplay( c );
}

// a hologram's dots (info.w 64: a masked glow with an .xscr template-5 record, dots on; r_sceneHoloDots): x the share
// of its alpha a round dot leaves, on the mask's UV in square cells, the record's grid (DotsCoords) across U; a soft dot
// (CP2077's fx_dot_01: full to 0.18 of a cell, gone at 0.42, its mean 0.292), antialiased; y its glow's boost, the dots'
// mean's inverse, so the hologram keeps its light (the dots' middles bright). Where a cell is under about 2 pixels the
// dots give way to the undotted hologram (no moire; 200 across the samurai are under 2 pixels from the ref04 hill)
vec2 holoDots()
{
	if ( gInfo.w != 64u || fr.holo.x < 0.5 )
		return vec2( 1.0 );
	const float n = materialDetail[uint( gEmissive.w + 0.5 ) / 4u + 1u].x;
	const vec2 ts = vec2( textureSize( ALBEDO_TEX, 0 ) );
	const vec2 g = vUV * vec2( n, n * ts.y / max( ts.x, 1.0 ) );
	const vec2 w = fwidth( g );
	const float cell = max( w.x, w.y );		// a cell's share of a pixel
	const float aa = 0.5 * cell;
	const float d = 1.0 - smoothstep( 0.18 - aa, 0.42 + aa, length( fract( g ) - 0.5 ) );
	const float shown = 1.0 - smoothstep( 0.25, 0.5, cell );
	return vec2( mix( 1.0, d, shown ), 1.0 / mix( 1.0, 0.292, shown ) );	// (the alpha's mean times the boost: 1 throughout)
}

// a hologram: a blend material with a masked glow (info.w 4, or 64 with its dots: CP2077's particles_hologram); its mask
// (the albedo's alpha) to r_sceneHoloCurve's power
bool holoMat() { return gInfo.x == 2u && ( gInfo.w == 4u || gInfo.w == 64u ); }
float holoMask( const float a ) { return pow( clamp( a, 0.0, 1.0 ), fr.holo.w ); }

// a screen's maps/<map>.xscr record (vk_scene.cpp Scene_LoadScreens: its offset in emissive.w, 1.0 without one), four
// vec4: template, tiles across, down, frames per second; grid rows, darkness, dots, fade (m); layer separation, layers
// 2-4; blink, hue cycle, broken, additive
bool screenRec() { return gInfo.w == 1u && gEmissive.w > 1.5; }
vec4 screenParam( const int k ) { return materialDetail[uint( gEmissive.w + 0.5 ) / 4u + uint( k )]; }
// a flipbook's frame now (tiles across x down, row-major from the top left, at its frames per second while
// r_sceneScreenAnim; none or 0: the first): uv in the frame -> uv in the sheet
vec2 screenTile( const vec2 uv )
{
	const vec4 r0 = screenParam( 0 );
	const vec2 n = max( floor( r0.yz + 0.5 ), vec2( 1.0 ) );
	if ( n.x * n.y <= 1.0 )
		return uv;
	const float f = fr.screen3.x > 0.5 && r0.w > 0.0 ? mod( floor( fr.water.x * r0.w ), n.x * n.y ) : 0.0;
	return ( vec2( mod( f, n.x ), floor( f / n.x ) ) + clamp( uv, 0.0, 1.0 ) ) / n;
}

vec3 screenFace( const vec3 srgb, const bool grid )
{
	vec3 c = toLinear( srgb );
	const bool rec = screenRec();
	const vec4 r1 = rec ? screenParam( 1 ) : vec4( 0.0 ), r2 = rec ? screenParam( 2 ) : vec4( 0.0 ), r3 = rec ? screenParam( 3 ) : vec4( 0.0 );
	const float sep = rec ? r2.x : fr.screen.w;
	const vec2 layers = rec ? r2.yz : fr.screen2.zw;
	if ( sep > 0.0 && ( gInfo.y & 1u ) != 0u ) {
		const vec3 V = normalize( pc.eye.xyz - vPos );
		vec3 Ng = safeNormalize( vNrm, vec3( 0.0, 0.0, 1.0 ) );
		const float side = dot( Ng, V ) < 0.0 ? -1.0 : 1.0;
		Ng *= side;
		const vec3 T = tangentOf( vTan.xyz, Ng );
		const vec3 B = cross( Ng, T ) * vTan.w * side;
		const float cosV = dot( V, Ng );
		vec2 step = vec2( dot( V, T ), dot( V, B ) ) / max( cosV, 0.25 ) * sep;
		const float len = length( step );
		step *= min( len, 1.5 * sep ) / max( len, 1e-6 );	// at most 1.5 separations: a grazing view's slid
		const float face = smoothstep( 0.2, 0.35, cosV );	// far content across the image; fade out there
		for ( int k = 1; k <= 2; k++ ) {
			const vec2 uv = vUV - step * float( k );
			const float in01 = all( greaterThanEqual( uv, vec2( 0.0 ) ) ) && all( lessThanEqual( uv, vec2( 1.0 ) ) ) ? face : 0.0;
			c += toLinear( texture( ALBEDO_TEX, rec ? screenTile( uv ) : uv ).rgb * gTint.rgb ) * ( k == 1 ? layers.x : layers.y ) * in01;
		}
	}
	const float rows = rec && r1.x > 0.0 ? r1.x : fr.screen.y, dark = rec ? clamp( r1.y, 0.0, 1.0 ) : fr.screen.z;
	if ( grid && rows > 0.0 && dark > 0.0 ) {
		// the grid over one frame (a flipbook's sheet holds tiles of them): its aspect is a frame's
		const vec2 n = rec ? max( floor( screenParam( 0 ).yz + 0.5 ), vec2( 1.0 ) ) : vec2( 1.0 );
		const vec2 ts = vec2( textureSize( ALBEDO_TEX, 0 ) ) / n;
		const vec2 g = vUV * vec2( rows * ts.x / max( ts.y, 1.0 ), rows );
		const vec2 w = fwidth( g );
		const vec2 e = abs( fract( g ) - 0.5 ) * 2.0;	// 0 a cell's middle, 1 its edge
		const bool dots = !rec || r1.z > 0.5;			// dots: both axes; lines: the rows alone
		const vec2 lit = vec2( dots ? 1.0 - smoothstep( 0.6, 1.0, e.x ) : 1.0, 1.0 - smoothstep( 0.6, 1.0, e.y ) );	// 0.8 of a cell on average
		const float mask = 1.0 - dark * ( 1.0 - lit.x * lit.y ), mean = 1.0 - dark * ( dots ? 0.36 : 0.2 );
		float shown = 1.0 - smoothstep( 0.3, 0.7, max( w.x, w.y ) );
		if ( rec && r1.w > 1.0 )	// the record's fade distance (DistanceDivision, m)
			shown *= 1.0 - smoothstep( 0.5 * r1.w, r1.w, length( pc.eye.xyz - vPos ) / UNITS_PER_METRE );
		c *= mix( 1.0, mask / mean, shown );
	}
	if ( rec && fr.screen3.x > 0.5 ) {
		const float t = fr.water.x;
		if ( r3.x > 0.0 )	// blink
			c *= 0.7 + 0.3 * ( 0.5 + 0.5 * cos( 6.2831853 * t * r3.x ) );
		if ( r3.y > 0.0 ) {	// hue cycle: a rotation about the grey axis (YIQ)
			const float a = 6.2831853 * fract( t * r3.y ), ca = cos( a ), sa = sin( a );
			const vec3 yiq = mat3( 0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312 ) * c;
			c = max( mat3( 1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703 ) * vec3( yiq.x, ca * yiq.y - sa * yiq.z, sa * yiq.y + ca * yiq.z ), 0.0 );
		}
		if ( r3.z > 0.0 ) {	// broken: drops out now and then, its own rhythm (the material's index)
			const float h = fract( sin( dot( vec2( floor( t * 15.0 ), float( gInfo.z % 4096u ) ), vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
			c *= h < 0.3 * clamp( r3.z, 0.0, 1.0 ) ? 0.15 : 1.0;
		}
	}
	return c;
}

// the haze between the eye and P, counting the path from `start` metres on: exponential in distance, thinning with
// the height of the path's middle
float haze( const float start )
{
	if ( fr.fog.w <= 0.0 )
		return 0.0;
	const float d = max( length( vPos - pc.eye.xyz ) / UNITS_PER_METRE - start, 0.0 );
	const float h = max( 0.5 * ( vPos.z + pc.eye.z ) / UNITS_PER_METRE, 0.0 );
	return min( 1.0 - exp( -d * fr.fog.w * exp( -h / fr.fog2.x ) ), fr.fog2.y );
}

// the light the haze scatters toward the eye between it and P (scene_fog.comp's volume, at P's depth)
const float FOG_SLICES = 64.0;	// SCENE_FOG_D
vec3 hazeLight( const vec3 P )
{
	if ( fr.fogVol.y <= 0.0 || pc.eye.w < 0.5 )
		return vec3( 0.0 );
	const vec3 v = P - pc.eye.xyz;
	const float z = dot( v, fr.fogFwd.xyz );
	if ( z <= 0.0 )
		return vec3( 0.0 );
	const vec2 uv = 0.5 - 0.5 * vec2( dot( v, fr.fogLeft.xyz ) / ( z * fr.fogLeft.w ), dot( v, fr.fogUp.xyz ) / ( z * fr.fogUp.w ) );
	const float s = log( max( z / fr.fogFwd.w, 1e-3 ) ) / fr.fogVol.x * FOG_SLICES;	// slices from the near side
	// texel k holds the light up to slice k + 1; nearer than the first slice's far side, a share of it
	const vec3 L = texture( fogLightTex, vec3( uv, clamp( ( s - 0.5 ) / FOG_SLICES, 0.5 / FOG_SLICES, 1.0 - 0.5 / FOG_SLICES ) ) ).rgb;
	return L * clamp( s, 0.0, 1.0 );
}

// toDisplay's inverse (back to linear light, before exposure)
vec3 fromDisplay( const vec3 y )
{
	const float top = toneInverse( vec3( TONE_TOP ) ).x;
	return mix( toneInverse( y ), top * exp( ( y - TONE_TOP ) / TONE_LOG ), greaterThan( y, vec3( TONE_TOP ) ) ) / fr.params.y;
}

// light at P seen through the haze, in linear light: dimmed by its share f, the haze's own colour by its share past
// r_sceneFogStart (veil; the source's distant fog starts at 150 m, nearer haze shows only where light scatters in it),
// and the light the haze scatters toward the eye on top
vec3 throughHaze( const vec3 c, const float f, const float veil )
{
	return c * ( 1.0 - f ) + fromDisplay( fr.fog.rgb ) * veil + hazeLight( vPos );
}

vec4 skyClouds( const vec3 d, const vec3 horizon );	// (below, after the noise)

// r_sceneMoon (vk_scene.cpp Scene_MoonAt; fr.moon w its angular radius, 0 none): CP2077's moon image (the sky pass's
// set 1) on its disc, upright to the zenith, through the haze's column toward it (its density at the eye's height over
// the haze's height, by the path's slant; at most r_sceneFogMax hidden); its glow (r_sceneMoonGlow: the moon's colour x
// an intensity x the angle's cosine to a power, fitted to ref09's sky), and a halo at its rim for the bloom CP2077 gives
// it (the sky is not in the glow pass). In the
// sky pass only: its texture read in uniform control flow (kMode 6), masked after
vec3 moonSky( const vec3 d )
{
#ifdef GPU_DRIVEN
	return vec3( 0.0 );
#else
	const vec3 m = fr.moon.xyz;
	vec3 right = cross( m, vec3( 0.0, 0.0, 1.0 ) );
	right = dot( right, right ) > 1e-6 ? normalize( right ) : vec3( 1.0, 0.0, 0.0 );
	const vec3 up = cross( right, m );
	const float mu = dot( d, m );
	const vec2 q = vec2( dot( d, right ), dot( d, up ) ) / ( max( mu, 1e-3 ) * tan( max( fr.moon.w, 1e-4 ) ) );	// -1..1 over the disc
	const vec4 t = texture( albedoTex, vec2( 0.5 + 0.5 * q.x, 0.5 - 0.5 * q.y ) );
	if ( fr.moon.w <= 0.0 )
		return vec3( 0.0 );
	const float disc = ( mu > 0.0 && max( abs( q.x ), abs( q.y ) ) < 1.0 ) ? t.a : 0.0;
	const float h = max( pc.eye.z / UNITS_PER_METRE, 0.0 );
	const float hidden = fr.fog.w > 0.0 ? min( 1.0 - exp( -fr.fog.w * fr.fog2.x * exp( -h / fr.fog2.x ) / max( d.z, 0.02 ) ), fr.fog2.y ) : 0.0;
	const float angle = 2.0 * asin( min( 0.5 * length( d - m ), 1.0 ) );
	const float glow = fr.moonCol.w * pow( max( mu, 0.0 ), fr.moonGlow.x )
		+ fr.moonGlow.y * exp( -max( angle - fr.moon.w, 0.0 ) / ( fr.moonGlow.z * fr.moon.w ) );
	return fr.moonCol.rgb * ( toLinear( t.rgb ) * disc * ( 1.0 - hidden ) + glow );
#endif
}

// r_sceneSky (a day scene's, maps/<map>.xenv): the sky along d in the scene's linear units: zenith to horizon, darker
// below it, the haze's colour along the horizon while the haze is on (where the city's haze meets it), the sun's disc
// (about half a degree across) and its glow; the moon (moonSky's) behind the clouds
vec3 skyRadiance( const vec3 d, const vec3 moon )
{
	vec3 c = mix( fr.skyZenith.rgb, fr.skyHorizon.rgb, pow( 1.0 - clamp( d.z, 0.0, 1.0 ), fr.skyGround.w ) );
	if ( fr.skySide.w > 0.0 ) {	// the horizon toward a low sun (The Witcher 3's sunColorHorizon): by the bearing, low down
		const vec2 sh = fr.sunDir.xy / max( length( fr.sunDir.xy ), 1e-4 ), dh = d.xy / max( length( d.xy ), 1e-4 );
		const float side = pow( clamp( 0.5 + 0.5 * dot( sh, dh ), 0.0, 1.0 ), 3.0 ) * pow( 1.0 - clamp( d.z, 0.0, 1.0 ), 2.0 * fr.skyGround.w );
		c = mix( c, fr.skySide.rgb, clamp( side * fr.skySide.w, 0.0, 1.0 ) );
	}
	if ( fr.skyGlow.w > 0.0 )	// the glow round the light (by night the moon's: moonColorSky over moonAreaSkySize)
		c += fr.skyGlow.rgb * pow( max( dot( d, normalize( fr.sunDir.xyz ) ), 0.0 ), fr.skyGlow.w );
	if ( d.z < 0.0 ) {
		// under the horizon: the distant land where the map gives one (r_sceneSkyGround; the haze's band below blends
		// it into the horizon), else the sky darkened
		if ( fr.skyGround.r < 0.0 )	// (r negative: the haze's own colour times -r, land lost in the haze as the horizon
									// band shows it: no seam where the far terrain ends; the Beauclair aerials)
			c = mix( c, fromDisplay( fr.fog.rgb ) * -fr.skyGround.r, smoothstep( 0.0, 0.08, -d.z ) );
		else if ( fr.skyGround.r + fr.skyGround.g + fr.skyGround.b > 0.0 )
			c = mix( c, fr.skyGround.rgb, smoothstep( 0.0, 0.08, -d.z ) );
		else
			c *= mix( 1.0, 0.5, clamp( -d.z * 4.0, 0.0, 1.0 ) );
	}
	c += moon;
	const vec4 cloud = skyClouds( d, fr.skyHorizon.rgb );	// (r_sceneClouds; none: 0)
	c = mix( c, cloud.rgb, cloud.a );
	if ( fr.fog.w > 0.0 )
		c = mix( c, fromDisplay( fr.fog.rgb ), pow( 1.0 - abs( d.z ), 12.0 ) );
	const float mu = dot( d, normalize( fr.sunDir.xyz ) );
	const float m = max( mu, 0.0 );
	c += fr.sunCol.rgb * fr.skyHorizon.w * ( 40.0 * smoothstep( 0.99996, 0.99999, mu ) * ( 1.0 - cloud.a ) + 0.5 * pow( m, 256.0 ) * ( 1.0 - 0.8 * cloud.a ) + 0.05 * pow( m, 8.0 ) );
	return c;
}

// the share of the sun reaching P: the first cascade whose box holds it, offset along the surface normal and toward
// the light by that cascade's texels (two-sided casters, no depth bias in the cascade pass), 2x2 bilinear comparisons
// one cascade's light at P (filtered), or -1 when P is outside it; s: P in the cascade's texture space
float sunCascade( const int i, const vec3 P, const vec3 Ng, out vec3 s )
{
	const float size = fr.sunCol.w;
	{	// (was sunShadow's loop body)
		const float texel = fr.sunTexel[i];
		const vec3 p = P + Ng * ( texel * 1.5 ) + fr.sunDir.xyz * texel;
		const vec4 c = fr.sunMvp[i] * vec4( p, 1.0 );
		s = vec3( c.xy / c.w * 0.5 + 0.5, c.z / c.w );
		if ( any( greaterThan( abs( s.xy - 0.5 ), vec2( 0.5 - 2.0 / size ) ) ) || s.z > 1.0 )
			return -1.0;
		if ( fr.clouds2.w > 0.0 ) {
			// r_sceneShadowSoft: a 4 x 4 grid of filtered comparisons over that many texels each way (a wide tent): softer
			// edges for fine casters (Beauclair's leaf shadows are sharp at 1.5 texels). A fixed grid, no noise: stable
			// frame to frame. (Measured with motion compensated, the sharp ones did not crawl either)
			const float step = fr.clouds2.w / 1.5 / size;
			float acc = 0.0;
			for ( int k = 0; k < 16; k++ ) {
				const vec2 o = vec2( float( k & 3 ) - 1.5, float( k >> 2 ) - 1.5 ) * step;
				acc += texture( sunShadowTex, vec4( s.xy + o, float( i ), s.z ) );
			}
			return acc / 16.0;
		}
		const float d = 0.75 / size;
		float lit = texture( sunShadowTex, vec4( s.xy + vec2( -d, -d ), float( i ), s.z ) );
		lit += texture( sunShadowTex, vec4( s.xy + vec2( d, -d ), float( i ), s.z ) );
		lit += texture( sunShadowTex, vec4( s.xy + vec2( -d, d ), float( i ), s.z ) );
		lit += texture( sunShadowTex, vec4( s.xy + vec2( d, d ), float( i ), s.z ) );
		return lit * 0.25;
	}
}

// The sun's light at P from the cascades, each blended into the next over a band before its edge, faded out before the
// last one ends (sunTexel.w: its reach in units; its fraction 0.5 when the middle cascade is kept). A hard switch popped
// in motion: Beauclair's aerials showed a band of shadow appearing or changing sharpness whenever a kept cascade
// (r_sunShadowFarCache, r_sunShadowMidCache: centred on the eye's grid cell) moved on a cell, and shadows ended in a
// line at the reach. A kept cascade's band is measured from the eye, not its centre, so it does not jump
// with the cell (its reach around the eye holds anywhere in the cell: tr_main.cpp R_SetupSunShadowCascades).
float sunShadow( const vec3 P, const vec3 Ng )
{
	const float far = floor( fr.sunTexel.w );
	const bool midKept = fract( fr.sunTexel.w ) > 0.25;
	const float dist = distance( P, fr.eye.xyz );
	if ( far > 0.0 && dist > 0.97 * far )
		return 1.0;
	float lit = 1.0;
	for ( int i = 0; i < 3; i++ ) {
		vec3 s;
		const float v = sunCascade( i, P, Ng, s );
		if ( v < 0.0 )
			continue;
		lit = v;
		// r_sceneShadowNearReach (sunDir.w - 1, units): the near cascade holds only casters within that reach toward the
		// light; the farther ones shade from the kept middle cascade, read a reach nearer the light (what lies between
		// is the near cascade's)
		if ( i == 0 && fr.sunDir.w > 1.5 ) {
			vec3 sm;
			const float m = sunCascade( 1, P + fr.sunDir.xyz * ( fr.sunDir.w - 1.0 ), Ng, sm );
			if ( m >= 0.0 )
				lit = min( lit, m );
		}
		if ( i < 2 ) {
			float t;
			if ( i == 1 && midKept ) {
				const vec4 e = fr.sunMvp[1] * vec4( fr.eye.xyz, 1.0 );
				// in depth along the light too (the box's z spans the same 2 x radius as its x and y): a surface far
				// below the eye along the sun's ray left the cascade through its depth range, a hard switch that moved
				// with the cell
				const vec3 se = vec3( e.xy / e.w * 0.5 + 0.5, e.z / e.w );
				const vec3 de = abs( s - se );
				t = smoothstep( 0.30, 0.42, max( de.x, max( de.y, de.z ) ) );
			} else {
				const vec3 dc = abs( s - vec3( 0.5 ) );
				t = smoothstep( 0.38, 0.47, max( dc.x, max( dc.y, dc.z ) ) );
			}
			if ( t > 0.0 ) {
				vec3 s2;
				const float n = sunCascade( i + 1, P, Ng, s2 );
				if ( n >= 0.0 )
					lit = mix( lit, n, t );
			}
		}
		break;
	}
	return far > 0.0 ? mix( lit, 1.0, smoothstep( 0.8 * far, 0.97 * far, dist ) ) : lit;
}

// r_sceneWaterBreakup: the water's own light (light scattered back out of the water) on its flat surface, not on the
// ripples (lit by the rippled normal it showed the ripple image's grid as light and shade); the glints keep the ripples
bool waterFlatLight = false;
vec3 waterFlatN = vec3( 0.0, 0.0, 1.0 );

// GGX specular and Lambert diffuse for light from l. `spread`: the light's angular radius over 2 (its size over twice
// its distance), widening the lobe as a sphere light does, energy kept (Karis 2013)
vec3 brdf( const vec3 N, const vec3 V, const vec3 l, const float NdotL, const float NdotV, const vec3 F0, const vec3 diffuseColor, const float rough, const float spread )
{
	const float a = rough * rough, k = ( rough + 1.0 ) * ( rough + 1.0 ) / 8.0;
	const vec3 h = normalize( l + V );
	const float NdotH = max( dot( N, h ), 0.0 ), VdotH = max( dot( V, h ), 0.0 );
	const vec3 F = F0 + ( 1.0 - F0 ) * pow( 1.0 - VdotH, 5.0 );
	const float aw = min( a + spread, 1.0 );
	const float dd = NdotH * NdotH * ( aw * aw - 1.0 ) + 1.0;
	const float D = a * a / ( PI * dd * dd );
	const float vis = 0.25 / ( ( NdotV * ( 1.0 - k ) + k ) * ( NdotL * ( 1.0 - k ) + k ) );
	if ( waterFlatLight )	// (the caller scales by NdotL: the flat surface's own instead)
		return ( vec3( 1.0 ) - F ) * diffuseColor / PI * ( max( dot( waterFlatN, l ), 0.0 ) / max( NdotL, 1e-4 ) ) + D * vis * F;
	return ( vec3( 1.0 ) - F ) * diffuseColor / PI + D * vis * F;
}

// the split-sum environment BRDF, analytic fit (Karis, "Physically based shading on mobile", 2014): F0 scale, bias
vec2 envBRDF( const float rough, const float NdotV )
{
	const vec4 c0 = vec4( -1.0, -0.0275, -0.572, 0.022 ), c1 = vec4( 1.0, 0.0425, 1.04, -0.04 );
	const vec4 r = rough * c0 + c1;
	const float a004 = min( r.x * r.x, exp2( -9.28 * NdotV ) ) * r.x + r.y;
	return vec2( -1.04, 1.04 ) * a004 + r.zw;
}

// the light a wet surface at P mirrors: scene_ssr.comp traced it last frame and settled it over frames. A tent 1.5
// texels wide (4x4 taps; a spatial filter over the temporal one: in motion the texels have not settled yet) over the
// texels showing this surface only (their view depth within 10%); none: a quarter of the haze. reflectedLightAt: the
// light and 1, or 0 and 0 where none
vec4 reflectedLightAt( const vec3 P )
{
	const vec4 c = fr.prevViewProj * vec4( P, 1.0 );
	if ( c.w <= 1.0 )
		return vec4( 0.0 );
	const vec2 uv = c.xy / c.w * 0.5 + 0.5;
	if ( any( lessThan( uv, vec2( 0.0 ) ) ) || any( greaterThan( uv, vec2( 1.0 ) ) ) )
		return vec4( 0.0 );
	const float z = dot( P - fr.prevEye.xyz, fr.prevFwd.xyz );
	const ivec2 size = textureSize( reflectTex0, 0 );
	const vec2 st = uv * vec2( size ) - 0.5;
	const ivec2 i0 = ivec2( floor( st ) );
	vec3 sum = vec3( 0.0 );
	float wsum = 0.0;
	for ( int k = 0; k < 16; k++ ) {
		const ivec2 o = ivec2( ( k & 3 ) - 1, ( k >> 2 ) - 1 );
		const vec2 dxy = abs( vec2( i0 + o ) - st );
		const float w = max( 1.5 - dxy.x, 0.0 ) * max( 1.5 - dxy.y, 0.0 );
		if ( w <= 0.0 )
			continue;
		const ivec2 at = clamp( i0 + o, ivec2( 0 ), size - 1 );
		const vec4 h = fr.ssr.w > 0.5 ? texelFetch( reflectTex1, at, 0 ) : texelFetch( reflectTex0, at, 0 );
		if ( h.a <= 0.0 || abs( h.a - z ) > 0.1 * z + 16.0 )	// loose: at grazing angles a texel's depth runs far

			continue;
		sum += h.rgb * w;
		wsum += w;
	}
	return wsum > 0.0 ? vec4( sum / wsum, 1.0 ) : vec4( 0.0 );
}
// r_sceneAO's occlusion at P: last frame's, through its camera, a tent over the texels showing this surface (as
// reflectedLightAt); 1 where none does
float occlusionAt( const vec3 P )
{
	const vec4 c = fr.prevViewProj * vec4( P, 1.0 );
	if ( c.w <= 1.0 )
		return 1.0;
	const vec2 uv = c.xy / c.w * 0.5 + 0.5;
	if ( any( lessThan( uv, vec2( 0.0 ) ) ) || any( greaterThan( uv, vec2( 1.0 ) ) ) )
		return 1.0;
	const float z = dot( P - fr.prevEye.xyz, fr.prevFwd.xyz );
	const ivec2 size = textureSize( aoTex0, 0 );
	const vec2 st = uv * vec2( size ) - 0.5;
	const ivec2 i0 = ivec2( floor( st ) );
	float sum = 0.0, wsum = 0.0;
	for ( int k = 0; k < 16; k++ ) {
		const ivec2 o = ivec2( ( k & 3 ) - 1, ( k >> 2 ) - 1 );
		const vec2 dxy = abs( vec2( i0 + o ) - st );
		const float w = max( 1.5 - dxy.x, 0.0 ) * max( 1.5 - dxy.y, 0.0 );
		if ( w <= 0.0 )
			continue;
		const ivec2 at = clamp( i0 + o, ivec2( 0 ), size - 1 );
		const vec2 h = fr.ssr.w > 0.5 ? texelFetch( aoTex1, at, 0 ).rg : texelFetch( aoTex0, at, 0 ).rg;
		if ( h.g <= 0.0 || abs( h.g - z ) > 0.05 * z + 8.0 )
			continue;
		sum += h.r * w;
		wsum += w;
	}
	return wsum > 0.0 ? sum / wsum : 1.0;
}

vec3 reflectedLight( const vec3 P )
{
	const vec4 r = reflectedLightAt( P );
	return r.a > 0.0 ? r.rgb : fromDisplay( fr.fog.rgb ) * 0.25;
}

// gradient noise on a world plane (metres), about 0..1 (value noise thresholded into puddles drew square blots)
float hash( const vec2 p ) { return fract( sin( dot( p, vec2( 127.1, 311.7 ) ) ) * 43758.5453 ); }
vec2 gradient( const vec2 i ) { const float a = hash( i ) * 6.2831853; return vec2( cos( a ), sin( a ) ); }
float gradientNoise( const vec2 p )
{
	const vec2 i = floor( p ), f = fract( p ), u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	const float a = dot( gradient( i ), f ), b = dot( gradient( i + vec2( 1.0, 0.0 ) ), f - vec2( 1.0, 0.0 ) );
	const float c = dot( gradient( i + vec2( 0.0, 1.0 ) ), f - vec2( 0.0, 1.0 ) ), d = dot( gradient( i + 1.0 ), f - 1.0 );
	return 0.5 + 0.75 * mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

// r_sceneClouds: gradientNoise with an integer hash: the sky's coordinates reach hundreds of cells, where the sine
// hash's large arguments lose their precision and draw straight seams across the sky
float cloudHash( const vec2 i )
{
	const uvec2 q = uvec2( ivec2( i ) );
	uint h = ( q.x * 1597334677u ) ^ ( q.y * 3812015801u );
	h = ( h ^ ( h >> 16u ) ) * 2246822519u;
	h ^= h >> 13u;
	return float( h ) * ( 1.0 / 4294967296.0 );
}
vec2 cloudGradient( const vec2 i ) { const float a = cloudHash( i ) * 6.2831853; return vec2( cos( a ), sin( a ) ); }
float cloudGradientNoise( const vec2 p )
{
	const vec2 i = floor( p ), f = fract( p ), u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	const float a = dot( cloudGradient( i ), f ), b = dot( cloudGradient( i + vec2( 1.0, 0.0 ) ), f - vec2( 1.0, 0.0 ) );
	const float c = dot( cloudGradient( i + vec2( 0.0, 1.0 ) ), f - vec2( 0.0, 1.0 ) ), d = dot( cloudGradient( i + 1.0 ), f - 1.0 );
	return 0.5 + 0.75 * mix( mix( a, b, u.x ), mix( c, d, u.x ), u.y );
}

// r_sceneClouds: fractal noise over the cloud layer's plane, in its size's units (about 0.1 to 0.9)
float cloudNoise( vec2 p )
{
	float n = 0.0, a = 0.5;
	for ( int k = 0; k < 5; k++ ) {
		n += a * cloudGradientNoise( p );
		p = mat2( 1.6, 1.2, -1.2, 1.6 ) * p + 3.7;
		a *= 0.5;
	}
	return n / 0.96875;
}

// r_sceneClouds: cumulus along d over r_sceneSky's sky: a layer 1.5 km up over a flat world, cut from the noise at the
// cover, drifting with the wind (the game's clock). Lit on its sun side (the noise a step toward the sun: where it is
// denser the cloud stands in its own shade), the sky's light under it, a bright rim toward the sun, the horizon's colour
// over it with distance. rgb its light, a its share of the pixel
vec4 skyClouds( const vec3 d, const vec3 horizon )
{
	if ( fr.clouds.x <= 0.0 || d.z <= 0.005 )
		return vec4( 0.0 );
	// the layer curves down with the world (a dome): far clouds stretch less into streaks along the horizon
	const vec2 wind = fr.clouds2.xy * fr.water.x;
	const float scale = 1.0 / ( fr.clouds.y * 1000.0 );
	const float t = 1500.0 / ( d.z + 0.08 );	// metres along d to the layer
	const vec2 p = ( d.xy * t + wind ) * scale;
	// the cover's noise with a finer billow on it (cumulus' heaped edges)
	const float n = cloudNoise( p ) + 0.12 * ( cloudGradientNoise( p * 6.0 + 11.0 ) - 0.5 );
	const float cut = 1.0 - fr.clouds.x * 0.8 - 0.1;
	const float dens = smoothstep( cut, cut + fr.clouds.z, n );
	if ( dens <= 0.0 )
		return vec4( 0.0 );
	const vec3 s = normalize( fr.sunDir.xyz );
	const vec2 toSun = s.xy / max( length( s.xy ), 1e-3 ) * 0.06;
	// a heap seen from below: where the cloud thickens just above this pixel it is its base (grey), where it thins its
	// top (lit); and where it thickens toward the sun it stands in its own shade
	const vec3 du = normalize( d + vec3( 0.0, 0.0, 0.012 ) );
	const float nu = cloudNoise( ( du.xy * ( 1500.0 / ( du.z + 0.08 ) ) + wind ) * scale );
	const float base = clamp( ( nu - cloudNoise( p ) ) * 8.0 + 0.5, 0.0, 1.0 );
	const float shade = max( clamp( ( cloudNoise( p + toSun ) - n ) * 5.0 + 0.4, 0.0, 1.0 ), 0.85 * base );	// 0 lit, 1 in shade
	const float mu = max( dot( d, s ), 0.0 );
	const vec3 lit = fr.sunCol.rgb * fr.clouds.w * ( 1.0 - 0.75 * shade + 1.5 * pow( mu, 12.0 ) * ( 1.0 - dens ) );
	const vec3 under = fr.skyAmbient.rgb * 2.0 + horizon * 0.15;
	const vec3 c = mix( under, lit + under * 0.5, mix( 1.0 - shade, 1.0, 0.3 ) );
	const float far = 1.0 - exp( -t / 40000.0 );	// the air between: the horizon's colour over far clouds
	return vec4( mix( c, horizon, far * 0.8 ), dens * ( 1.0 - far * 0.5 ) );
}

// a hash of three integers to 0..1 (PCG)
uint pcg( uint v ) { const uint s = v * 747796405u + 2891336453u; const uint w = ( ( s >> ( ( s >> 28u ) + 4u ) ) ^ s ) * 277803737u; return ( w >> 22u ) ^ w; }
vec4 hash4( const ivec3 c )
{
	const uint h = pcg( uint( c.x ) + pcg( uint( c.y ) + pcg( uint( c.z ) ) ) );
	return vec4( h & 255u, ( h >> 8u ) & 255u, ( h >> 16u ) & 255u, h >> 24u ) / 255.0;
}
vec4 hash4( const ivec4 c )
{
	const uint h = pcg( uint( c.x ) + pcg( uint( c.y ) + pcg( uint( c.z ) + pcg( uint( c.w ) ) ) ) );
	return vec4( h & 255u, ( h >> 8u ) & 255u, ( h >> 16u ) & 255u, h >> 24u ) / 255.0;
}

// water (mode 9, the scene's water materials; vk_scene.cpp Scene_LoadWater): gEmissive holds its visibility and shore
// (m of path through it), its ripples' tile (m) and speed (m/s); its detail parameters (md) their heading (radians),
// its roughness (0: its ORM's), normal strength and foam band (m); gTint its colour in the water (sRGB), a the foam's
// intensity
const float WATER_F0 = 0.02;

// the view depth of what lies under the water at this pixel, the eye view's prepass (the occlusion pyramids' level 0:
// the farthest of each 2 x 2 pixels), bilinear; none (another view, or no pyramid this frame): infinitely deep
float waterBed()
{
	if ( fr.waterBed.w == 0 || pc.eye.w < 0.5 )
		return 1e30;
	const ivec2 size = fr.waterBed.yz;
	const vec2 st = gl_FragCoord.xy * 0.5 - 0.5;
	const ivec2 i0 = ivec2( floor( st ) );
	const vec2 f = st - vec2( i0 );
	vec4 d;
	for ( int k = 0; k < 4; k++ ) {
		const ivec2 q = clamp( i0 + ivec2( k & 1, k >> 1 ), ivec2( 0 ), size - 1 );
		d[k] = pyramids[fr.waterBed.x + q.y * size.x + q.x];
	}
	return mix( mix( d.x, d.y, f.x ), mix( d.z, d.w, f.x ), f.y );
}

// the ripples: the normal image twice over the world plane (metres), the second a 0.37 tile turned 110 degrees running at
// 0.7 the speed, the two slopes added (whiteout); flat without a normal image. r_sceneWaterBreakup: a third under them, 2.7
// tiles turned 250 degrees at half the speed and half the slope (one image at two tiles repeated plainly past the near
// water)
float waterLostVar = 0.0;	// r_sceneWaterWaves: the slope variance the faded cascades gave up (into the roughness)
vec3 waterNormal( const vec3 Ng, const vec4 md )
{
	if ( ( gInfo.y & 2u ) == 0u )
		return Ng;
	if ( fr.waterWaves.x > 0.5 ) {
		// r_sceneWaterWaves: the three simulated cascades, turned by the material's heading (the spectrum's wind is +x)
		// and scaled by its tile against the ripple image's 4 m (the slopes kept: the material's strength holds), each
		// sampled through its levels (a texel under a pixel averages); the slope the levels average away (the share of the
		// cascade's 8 octaves below the pixel) goes into the roughness
		const vec2 p = vPos.xy / UNITS_PER_METRE;
		const vec2 dir = vec2( cos( md.x ), sin( md.x ) ), side = vec2( -dir.y, dir.x );
		const vec2 q = vec2( dot( p, dir ), dot( p, side ) );
		const float scale = max( gEmissive.z, 0.05 ) / 4.0, px = max( length( fwidth( q ) ), 1e-5 );
		vec2 s = vec2( 0.0 );
		float lost = 0.0;
		for ( int c = 0; c < 3; c++ ) {
			const float L = fr.waterWaves[1 + c] * scale;
			s += texture( waveTex, vec3( q / L, float( c ) ) ).xy;
			lost += clamp( log2( max( px / ( L / fr.waterWaves2.y ), 1.0 ) ) / 8.0, 0.0, 1.0 );
		}
		const float k = md.z * fr.water.w;
		waterLostVar = lost * fr.waterWaves2.x * k * k;
		const vec2 g = ( s.x * dir + s.y * side ) * k;	// the height's slope in the world (x, y)
		return normalize( Ng - vec3( g, 0.0 ) * sign( Ng.z ) );
	}
	const float tile = max( gEmissive.z, 0.05 ), t = fr.water.x * gEmissive.w;
	const vec2 dir = vec2( cos( md.x ), sin( md.x ) );
	const vec2 p = vPos.xy / UNITS_PER_METRE;
	const mat2 turn = mat2( -0.34202, 0.93969, -0.93969, -0.34202 );	// 110 degrees
	const vec3 n1 = texture( NORMAL_TEX, ( p + dir * t ) / tile ).xyz * 2.0 - 1.0;
	const vec3 n2 = texture( NORMAL_TEX, ( turn * p + turn * dir * ( 0.7 * t ) ) / ( tile * 0.37 ) ).xyz * 2.0 - 1.0;
	vec2 slope = n1.xy + transpose( turn ) * n2.xy;
	float nz = n1.z * n2.z;
	if ( fr.water2.w > 0.5 ) {
		// each layer fades as its tile shrinks on screen from 40 to 12 pixels (a tile in fewer pixels shows as a grid,
		// the reflection at a grazing angle magnifying what is left of it), the fine ones first: the far water a calm
		// mirror, the broad layer carrying the middle distance
		const float px = max( length( fwidth( p ) ), 1e-5 );
		const vec3 w = 1.0 - smoothstep( vec3( 1.0 / 40.0 ), vec3( 1.0 / 12.0 ), px / ( tile * vec3( 1.0, 0.37, 2.7 ) ) );
		const mat2 turn3 = mat2( -0.34202, -0.93969, 0.93969, -0.34202 );	// 250 degrees
		const vec3 n3 = texture( NORMAL_TEX, ( turn3 * p + turn3 * dir * ( 0.5 * t ) ) / ( tile * 2.7 ) ).xyz * 2.0 - 1.0;
		slope = n1.xy * w.x + transpose( turn ) * n2.xy * w.y + transpose( turn3 ) * n3.xy * ( 0.5 * w.z );
		nz = mix( 1.0, n1.z, w.x ) * mix( 1.0, n2.z, w.y ) * mix( 1.0, n3.z, 0.5 * w.z );
	}
	slope *= md.z * fr.water.w;
	return normalize( Ng * max( nz, 0.05 ) + vec3( slope, 0.0 ) * sign( Ng.z ) );
}

// what the water mirrors where the screen holds nothing: the haze's colour at the horizon toward the sky's light overhead
vec3 waterSky( const vec3 R )
{
	return mix( fromDisplay( fr.fog.rgb ), fr.skyAmbient.rgb, smoothstep( 0.0, 0.6, R.z ) );
}

// window_parallax_interior: rooms behind the glass. The facade is cut into rooms of the material's size (along the
// facade and up, in the mesh's own space, where the artists line floors up with its origin; a world-space grid cut
// across windows); each room, told apart by its cell and its instance (r_sceneWindowCell: by a 3D room cell in mesh space on
// a grid turned to the facade, the cell holding its box's centre just inside the glass, so the panes of a corner bay share
// one room), picks one of the atlas's pre-rendered rooms, is
// dark at the material's share, and otherwise glows in a colour temperature and brightness of its own. The view ray is traced into the room's
// box and its hit on the walls, floor, ceiling or back wall is mapped onto the room's picture, a one-point
// perspective whose back wall fills ROOM_BACK of it ("fake interiors"). Returns linear light before the emissive scale
const float ROOM_BACK = 0.45;
vec3 windowDebug;	// r_sceneDebug 11: the room's hash as a colour; 12: the instance's
vec3 windowRoom()
{
	const vec3 V = normalize( pc.eye.xyz - vPos );
	vec3 Ng = normalize( vNrm );
	vec3 Nl = normalize( vLocalNrm );
	if ( dot( Ng, V ) < 0.0 ) {
		Ng = -Ng;
		Nl = -Nl;
	}
	// the room grid in mesh space (metres, z up); the ray in the world along the same axes
	const vec3 tl = abs( Nl.z ) < 0.9 ? normalize( cross( vec3( 0.0, 0.0, 1.0 ), Nl ) ) : vec3( 1.0, 0.0, 0.0 );
	const vec3 t = abs( dot( Ng, vUpW ) ) < 0.9 ? normalize( cross( vUpW, Ng ) ) : normalize( cross( Ng, vec3( 0.0, 1.0, 0.0 ) ) );
	const vec3 b = cross( Ng, t );	// up the facade
	const vec2 size = max( gTint.xy, vec2( 0.5 ) );
	const float depth = max( float( gInfo.w >> 12u ) / 16.0, 0.25 );
	const vec2 q = vec2( dot( vLocal, tl ), vLocal.z ) / size;
	const vec2 cell = floor( q );
	vec4 h;
	if ( fr.surface2.y > 0.5 ) {
		// the room's box centre in mesh space, 5 cm inside the glass, and the 3D cell that holds it on a grid turned to
		// the facade's own axes (its direction folded to within 45 degrees, so the two panes of a corner, a quarter turn
		// apart, share one grid whichever way the mesh was authored)
		const vec3 centre = vLocal + tl * ( ( cell.x + 0.5 ) * size.x - dot( vLocal, tl ) ) + vec3( 0.0, 0.0, ( cell.y + 0.5 ) * size.y - vLocal.z ) - Nl * 0.05;
		const float turn = mod( atan( Nl.y, Nl.x ) + 0.25 * PI, 0.5 * PI ) - 0.25 * PI;
		const vec2 g = mat2( cos( turn ), -sin( turn ), sin( turn ), cos( turn ) ) * centre.xy;	// turned back by turn
		h = hash4( ivec4( floor( vec3( g, centre.z ) / vec3( size.x, size.x, size.y ) ), int( vInst ) ) );
	} else {
		const int face = int( floor( atan( Nl.y, Nl.x ) * 4.0 / PI + 0.5 ) ) + 8;
		h = hash4( ivec3( cell.x + float( face * 4096 ), cell.y, int( vInst ) ) );
	}
	const bool lit = h.x >= gTint.z;
	windowDebug = int( fr.params.w ) == 12 ? hash4( ivec3( int( vInst ), 7, 11 ) ).xyz : vec3( h.y, h.z, h.w ) * ( lit ? 1.0 : 0.35 );

	// the ray into the room's box: x along the facade, y up, z into the building; it enters at the glass (z 0)
	const vec3 dir = -V;
	const vec3 r = vec3( dot( dir, t ), dot( dir, b ), max( dot( dir, -Ng ), 1e-3 ) );
	const vec3 p0 = vec3( ( q - cell ) * size, 0.0 );
	const vec3 box = vec3( size, depth );
	const float tx = r.x > 0.0 ? ( box.x - p0.x ) / r.x : r.x < 0.0 ? -p0.x / r.x : 1e9;
	const float ty = r.y > 0.0 ? ( box.y - p0.y ) / r.y : r.y < 0.0 ? -p0.y / r.y : 1e9;
	const vec3 hit = p0 + r * min( min( tx, ty ), box.z / r.z );
	// onto the picture: a camera in front of the window at c sees the glass edge to edge and the back wall at ROOM_BACK
	const float c = box.z * ROOM_BACK / ( 1.0 - ROOM_BACK );
	const vec2 img = ( hit.xy - box.xy * 0.5 ) / ( box.xy * 0.5 ) * ( c / ( hit.z + c ) );
	const vec2 uv = vec2( img.x * 0.5 + 0.5, 0.5 - img.y * 0.5 );	// the picture's rows run down
	const float tiles = float( ( gInfo.w >> 4u ) & 255u );
	vec3 room = vec3( 0.8 );
	if ( tiles > 0.0 && ( gInfo.y & 1u ) != 0u ) {
		const float tile = min( floor( h.y * tiles ), tiles - 1.0 );
		const vec2 at = vec2( ( tile + clamp( uv.x, 0.002, 0.998 ) ) / tiles, clamp( uv.y, 0.002, 0.998 ) );
		room = toLinear( textureGrad( ALBEDO_TEX, at, dFdx( uv ) / vec2( tiles, 1.0 ), dFdy( uv ) / vec2( tiles, 1.0 ) ).rgb );
	}
	if ( !lit )
		return room * 0.02;	// a dark room: only the city's light from outside
	// warm to the source's tint by the room's own share of the colour spread; brightness 0.6..1.4
	const vec3 warm = vec3( 1.0, 0.62, 0.32 );
	return room * mix( vec3( 1.0 ), warm, gTint.w * h.z ) * ( 0.6 + 0.8 * h.w ) * fr.wet.w;
}

#ifdef GPU_DRIVEN
// Witcher 3 terrain (main's -3 block): one layer, colour and normal (roughness in its alpha), read in the projections the
// surface's facing weighs (tw: x along x, y along y, z from above; each with its coordinates and gradients) and added
// at weight w; an image index under 0 is none (a flat normal, roughness 0.9). Called once per distinct layer, so no
// array is indexed at run time (an array of the taps' merged layers went to local memory: 64 B a fragment)
void w3TerrainLayer( const float ci, const float ni, const float w, const vec3 tw, const vec2 p0, const vec2 p0x, const vec2 p0y,
	const vec2 p1, const vec2 p1x, const vec2 p1y, const vec2 p2, const vec2 p2x, const vec2 p2y, const float fl, inout vec3 D, inout vec3 N,
	inout float R )
{
	if ( w <= 0.0 )
		return;
	if ( ci >= 0.0 ) {
		const uint t = uint( ci );
		vec3 c = vec3( 0.0 );
		if ( tw.z > 0.0 ) c += tw.z * textureGrad( sceneTex[SCENE_TEX_INDEX( t )], p0, p0x, p0y ).rgb;
		if ( tw.y > 0.0 ) c += tw.y * textureGrad( sceneTex[SCENE_TEX_INDEX( t )], p1, p1x, p1y ).rgb;
		if ( tw.x > 0.0 ) c += tw.x * textureGrad( sceneTex[SCENE_TEX_INDEX( t )], p2, p2x, p2y ).rgb;
		D += w * c;
	}
	vec4 q = vec4( 0.5, 0.5, 1.0, 0.9 );
	if ( ni >= 0.0 && fl < 1.0 ) {	// (past r_sceneTerrainFar, fl 1: no normal map, faded out over its band)
		const uint t = uint( ni );
		vec4 r = vec4( 0.0 );
		if ( tw.z > 0.0 ) r += tw.z * textureGrad( sceneTex[SCENE_TEX_INDEX( t )], p0, p0x, p0y );
		if ( tw.y > 0.0 ) r += tw.y * textureGrad( sceneTex[SCENE_TEX_INDEX( t )], p1, p1x, p1y );
		if ( tw.x > 0.0 ) r += tw.x * textureGrad( sceneTex[SCENE_TEX_INDEX( t )], p2, p2x, p2y );
		q = mix( r, q, fl );
	}
	N += w * ( q.xyz * 2.0 - 1.0 );
	R += w * q.a;
}
// a background layer at its UV scale: from above (world xy), along x (-x, -z) and along y (-y, -z)
void w3TerrainBg( const float ci, const float ni, const float w, const float sc, const vec3 tw, const vec2 world, const float zW,
	const vec2 gx, const vec2 gy, const vec2 gzx, const float fl, inout vec3 D, inout vec3 N, inout float R )
{
	if ( w <= 0.0 )
		return;
	w3TerrainLayer( ci, ni, w, tw, world * sc, gx * sc, gy * sc,
		vec2( -world.x, -zW ) * sc, vec2( -gx.x, -gzx.x ) * sc, vec2( -gy.x, -gzx.y ) * sc,
		vec2( -world.y, -zW ) * sc, vec2( -gx.y, -gzx.x ) * sc, vec2( -gy.y, -gzx.y ) * sc, fl, D, N, R );
}
#endif

void main()
{
#if defined( FADE_SAMPLEMASK ) || defined( CUTOUT_COVERAGE )
	gl_SampleMask[0] = -1;	// (every invocation writes it: the fade or a cutout's coverage below narrows it)
#endif
#ifdef FADE_SAMPLEMASK
	uint keep = 0xffffffffu;	// the samples the fade kept (a cutout's coverage narrows them further)
#endif
#ifdef GPU_DRIVEN_LIT
	// one pipeline for the opaque, cutout and emissive kinds, drawn in material order (an emissive window's glass lies
	// exactly on other materials: the later one wins there): the kind picks the mode (no discard: the depth test did it)
	const int mode = gInfo.x == 3u ? 3 : 0;
#else
	const int mode = kMode;
#endif
	const bool water = mode == 9;
	if ( mode == 6 ) {
		const vec3 d = normalize( vPos - pc.eye.xyz );
		const vec3 moon = moonSky( d );	// (r_sceneMoon; none: 0)
		if ( fr.skyZenith.w > 0.5 ) {	// r_sceneSky: the sky itself, over the map's (alpha 1), the haze's light in front
			outColor = vec4( toDisplay( skyRadiance( d, moon ) + hazeLight( vPos ) ), 1.0 );
			return;
		}
		// over the sky (shown about as the haze's colour): the moon and the volume's far end, added (alpha 0)
		outColor = vec4( max( toDisplay( throughHaze( vec3( 0.0 ), 1.0, 1.0 ) + moon ) - fr.fog.rgb, 0.0 ), 0.0 );
		return;
	}
	if ( mode == 10 || mode == 11 ) {
		// a particle (r_sceneFx, scene_fx.vert; 10 blended over, 11 added): blackbodyradiation.mt's colour, lerp(dark,
		// light) by the flipbook texel's value, lit by the ambient hemisphere's mean, a share of the haze's colour and
		// the local lights (none when unlit); its alpha the texel's times the particle's, fading where the prepass's
		// depth lies just behind it (soft); through the haze
		const vec4 t = texture( ALBEDO_TEX, vUV );
		// the mask: its alpha, or its value when the image's alpha is solid throughout (its last mip, the whole atlas's
		// mean, nearly 1: four of the converted sheets, dust specks and motes, vapour spray, round sparks, keep the shape
		// in the colour; their alpha drew each particle as a solid square)
		// (as the fx converter decided per sheet, cp77fx value_mask: gEmissive.y 2 the value, 1 the alpha; 0, a pack made
		// before the flag: the last mip's alpha)
		const float m = gEmissive.y;
		const float mask = m > 1.5 || ( m < 0.5 && textureLod( ALBEDO_TEX, vUV, 16.0 ).a > 0.98 ) ? max( t.r, max( t.g, t.b ) ) : t.a;
		float a = mask * vTan.x * fr.fx2.x;
		if ( vTan.y > 0.0 && fr.fx.z > 0.0 ) {
			const float z = dot( vPos - pc.eye.xyz, fr.fogFwd.xyz );
			a *= clamp( ( waterBed() - z ) / ( vTan.y * fr.fx.z ), 0.0, 1.0 );
		}
		const vec3 amb = 0.5 * ( fr.skyAmbient.rgb + fr.groundAmbient.rgb ) + fromDisplay( fr.fog.rgb ) * fr.fx.y;
		vec3 col;
		if ( ( uint( vTan.z + 0.5 ) & 32768u ) != 0u ) {
			// a normal-mapped water particle (Witcher 3's water_splash_additive: a normal map lit by a cube's reflection):
			// its normal from the image in the quad's frame (the image's green down), the sky along the reflected view
			// (Fresnel) times its reflection gain (the light colour), the sun's glint, and a little body (the dark
			// colour) lit by the ambient; the shape from the image's alpha
			const vec3 T = normalize( vCanopy ), Nq = normalize( vUpW ), B = cross( Nq, T );
			const vec3 tn = t.xyz * 2.0 - 1.0;
			const vec3 n = normalize( T * tn.x - B * tn.y + Nq * max( tn.z, 0.05 ) );
			const vec3 V = normalize( pc.eye.xyz - vPos );
			const vec3 R = reflect( -V, n );
			const float F = 0.02 + 0.98 * pow( 1.0 - clamp( dot( n, V ), 0.0, 1.0 ), 5.0 );
			const vec3 L = normalize( fr.sunDir.xyz );
			const vec3 sky = skyRadiance( R, vec3( 0.0 ) );
			col = ( sky * F * vLocalNrm + fr.sunCol.rgb * pow( max( dot( R, L ), 0.0 ), 120.0 ) * 2.0 * vLocalNrm.r
				+ vLocal * ( amb + vNrm ) ) * fr.fx.x;
		} else {
			const vec3 tint = mix( vLocal, vLocalNrm, dot( t.rgb, vec3( 1.0 / 3.0 ) ) );
			col = tint * ( amb + vNrm * ( ( uint( vTan.z + 0.5 ) & 2u ) != 0u ? fr.fx.w : 1.0 ) ) * fr.fx.x;
		}
		// (an added one adds its own light through the haze: the haze's veil and light are already behind it)
		outColor = vec4( toDisplay( mode == 11 ? col * ( 1.0 - haze( 0.0 ) ) : throughHaze( col, haze( 0.0 ), haze( fr.fogVol.w ) ) ), clamp( a, 0.0, 1.0 ) );
		return;
	}
	// an instance fading out past its hide distance keeps the dither's low values, one fading in past its near distance
	// the high ones: where a mesh fades out as its proxy fades in, each pixel shows one of them (interleaved gradient noise)
	// r_sceneHideDither 2 (the rubble under the water fading out showed as dots): the depth prepass
	// (the FADE_SAMPLEMASK variant, modes 7 and 4) keeps that share of the pixel's samples instead of the whole pixel,
	// the count dithered by the blue noise and the samples turned by the gradient noise, the fading-in one taking the
	// rest; the lit pass, testing EQUAL against it, then shades those samples only (it does not discard)
	if ( vFade < 1.0 ) {
		const float n = hideNoise();
#ifdef FADE_SAMPLEMASK
		if ( fr.fx2.z > 1.5 && ( mode == 7 || mode == 4 ) ) {
			const int N = clamp( int( fr.fx2.w + 0.5 ), 1, 8 );
			const uint full = ( 1u << uint( N ) ) - 1u;
			const int k = clamp( int( abs( vFade ) * float( N ) + n ), 0, N );
			const uint r = uint( fract( 52.9829189 * fract( dot( gl_FragCoord.xy, vec2( 0.06711056, 0.00583715 ) ) ) ) * float( N ) ) % uint( N );
			uint low = ( 1u << uint( k ) ) - 1u;
			low = ( ( low << r ) | ( low >> ( uint( N ) - r ) ) ) & full;
			const uint m = vFade >= 0.0 ? low : ( full & ~low );
			if ( m == 0u )
				discard;
			gl_SampleMask[0] = int( m );
			keep = m;
		} else
#endif
#ifdef GPU_DRIVEN_LIT
		if ( fr.fx2.z < 1.5 )
#endif
		if ( vFade >= 0.0 ? n >= vFade : n < -vFade )
			discard;
	}
#ifdef DECAL
	{
		// a mesh decal (r_sceneDecals) on what the prepass holds at its pixel: nothing nearer than it (a little behind
		// still counts: the source lays decals on, or a hair off, their surface) and something within its threshold
		// (gEmissive.x, units) behind it, fading out over the threshold's last fifth
		const float d = texelFetch( decalDepthTex, ivec2( gl_FragCoord.xy ), 0 ).r;
		if ( d >= 1.0 )
			discard;	// the sky
		const float denom = -d - fr.detail.z;
		const float rz = abs( denom ) < 1e-6 ? 1e30 : -fr.detail.w / denom;	// its view depth (scene_occ.comp's)
		const float dz = dot( vPos - fr.eye.xyz, fr.fogFwd.xyz );
		const float gap = rz - dz, thr = gEmissive.x;
		if ( gap < -( 0.5 + 0.002 * dz ) || gap > thr )
			discard;
		const vec4 tex = ( gInfo.y & 1u ) != 0u ? texture( ALBEDO_TEX, vUV ) : vec4( 1.0 );
		const float edge = 1.0 - smoothstep( 0.8 * thr, thr, gap );
		float weight = 0.0;
		vec3 N = vec3( 0.0, 0.0, 1.0 );
		if ( ( gInfo.y & 2u ) != 0u ) {	// its normal map (OpenGL), facing the eye as the lit pass's
			const vec3 V = normalize( pc.eye.xyz - vPos );
			vec3 Ng = safeNormalize( vNrm, vec3( 0.0, 0.0, 1.0 ) );
			const float side = dot( Ng, V ) < 0.0 ? -1.0 : 1.0;
			Ng *= side;
			const vec3 T = tangentOf( vTan.xyz, Ng );
			const vec3 B = cross( Ng, T ) * vTan.w * side;
			const vec3 n = texture( NORMAL_TEX, vUV ).xyz * 2.0 - 1.0;
			N = normalize( T * n.x + B * n.y + Ng * max( n.z, 0.05 ) );
			weight = clamp( tex.a * gEmissive.y * edge, 0.0, 1.0 );	// NormalAlpha
		}
		outColor = vec4( tex.rgb * gTint.rgb, clamp( tex.a * gTint.a * edge, 0.0, 1.0 ) );	// DiffuseAlpha
		outDecalNormal = vec4( N * 0.5 + 0.5, weight );
		outDecalDepth = vec4( rz, 0.0, 0.0, 1.0 );
		outDecalSurface = vec4( 0.0 );
		outDecalEmission = vec4( 0.0 );
		return;
	}
#endif
	if ( mode == 7 ) {	// the opaque depth prepass while the fades are on: the dither alone
		outColor = vec4( 0.0 );
		return;
	}
	// Witcher 3 flowing water (waterfall strips, the dams' sheets: .xw3b record kind 3, -8): the base, normal and ORM
	// images tiled and scrolled along the fall by the game's clock (block: speed u, v, tiling u, v), the sheet's opacity
	// scaled (md.z)
	vec2 mUV = vUV;
	float flowAlpha = 1.0;
	float bbFade = 0.0;	// (a billboard card's lost share of the pitch band, below)
	float bbMix = 0.0;	// (a turning card's share of its next stored view, blended: r_sceneCardBlend)
	bool bbCard = false;	// (a billboard card: the sun wraps round it, below)
	vec4 bbFix = vec4( 1.0 );	// (a billboard card's match to its 3D tree, cardmatch.py: its ambient light's rgb and its sun light's multipliers)
	{
		const vec4 fd = materialDetail[gInfo.z];
		if ( fd.x == -8.0 ) {
			const vec4 f = materialDetail[uint( fd.y )];
			mUV = vUV * f.zw + fract( fr.water.x * f.xy );
			flowAlpha = fd.z;
		}
#ifdef GPU_DRIVEN
		// Witcher 3 SpeedTree billboards (.xw3b version 3: -9 the card turned to the viewer, -10 the top card): over the
		// pitch band the top card takes the share vCol.y: each card's alpha is lowered by its lost share, so it thins
		// through its own leaves' alpha (a screen dither showed as dots); the turning card reads the stored
		// view nearest the viewer (vUV) and, for the share vCol.x, the next (vUV1): blended (r_sceneCardBlend 1),
		// or picked per pixel by a dither (0: a static checker of the two views on every far tree between them,
		// 2 x 2 under r_sceneVrs). The prepass, the cascades and the lit pass compute the same
		if ( fd.x == -9.0 || fd.x == -10.0 ) {
			bbFade = 0.51 * ( fd.x == -9.0 ? vCol.y : 1.0 - vCol.y );
			bbCard = true;
			if ( fd.z > 0.5 )
				bbFix = materialDetail[uint( fd.y ) + uint( fd.z + 0.5 )];
			if ( fd.x == -9.0 && vCol.x > 0.0 ) {
				if ( fr.cards.x > 0.5 )
					bbMix = vCol.x;
				else if ( fract( 52.9829189 * fract( dot( gl_FragCoord.xy + vec2( 17.0, 59.0 ), vec2( 0.06711056, 0.00583715 ) ) ) ) < vCol.x )
					mUV = vUV1;
			}
		}
#endif
	}
	// a screen with a maps/<map>.xscr record: its flipbook's frame now (one frame of a whole sheet)
	vec4 base = ( gInfo.y & 1u ) != 0u ? texture( ALBEDO_TEX, screenRec() ? screenTile( mUV ) : mUV, COARSE_BIAS ) : vec4( 1.0 );
#ifdef GPU_DRIVEN
	if ( bbMix > 0.0 && ( gInfo.y & 1u ) != 0u )
		base = mix( base, texture( ALBEDO_TEX, vUV1, COARSE_BIAS ), bbMix );
#endif
	base *= gTint;
	base.a *= flowAlpha;
	base.a -= bbFade;
#ifdef GPU_DRIVEN_LIT
	// a cutout's clear texels: the EQUAL test passes them where it lies exactly on another surface (the depth tests ran
	// before this shader, so this costs no early depth)
#ifdef CUTOUT_COVERAGE
	if ( gInfo.x == 1u && vCoarse == 0u ) {	// (r_sceneCutoutCoverage: its covered samples alone, as the prepass kept them)
		const uint c = cutoutCoverage( base.a );
		if ( c == 0u )
			discard;
		gl_SampleMask[0] = int( c );
	}
#else
	if ( gInfo.x == 1u && base.a < 0.5 && vCoarse == 0u )
		discard;
#endif
#endif
#ifdef FADE_SAMPLEMASK
	if ( mode == 4 && fr.dlightCount.z > 0.5 ) {	// r_sceneCutoutCoverage: the cutout's depth at its covered samples
		const uint c = cutoutCoverage( base.a ) & keep;
		if ( c == 0u )
			discard;
		gl_SampleMask[0] = int( c );
		outColor = vec4( 0.0 );
		return;
	}
#endif
	if ( mode == 1 || mode == 4 ) {
		if ( base.a < 0.5 )
			discard;
		if ( mode == 4 ) {
			outColor = vec4( 0.0 );
			return;
		}
	}
	if ( mode == 3 || mode == 5 ) {
		const bool scr = screenOn();
		vec3 e = ( ( gInfo.w & 2u ) != 0u ? windowRoom() : scr ? screenFace( base.rgb, mode == 3 ) : toLinear( base.rgb ) ) * gEmissive.rgb * fr.params.z;
		if ( holoMat() ) {
			// a hologram in the bloom pass (r_sceneHoloBloom): the light the blend pass leaves, its mask's glow times its
			// alpha (the mask again; the dots' alpha and boost), times its share that blooms (r_sceneHoloBloom)
			const vec2 dots = holoDots();
			const float mask = holoMask( base.a );
			e = vec3( mask * mask * dots.x * dots.y * fr.holo.z * fr.holo.y ) * gEmissive.rgb * fr.params.z;
		}
		e *= screenGain();	// (r_sceneScreenGain)
		if ( mode == 5 && screenMat() )
			e *= fr.screen3.w;	// the share of a screen's light that blooms (r_sceneScreenBloom)
		if ( ( gInfo.w & 8u ) != 0u )	// a signal's lamp (info.w 8 | lamp << 4): as its signal's state now
			e *= fr.signals[min( vSignal & 0xffffffu, 255u )][( gInfo.w >> 4u ) & 3u];
		const float f = haze( 0.0 ) * 0.6;	// light sources cut through the haze
#ifndef NO_DEBUG_VIEWS
		if ( mode == 3 && ( int( fr.params.w ) == 11 || int( fr.params.w ) == 12 ) && ( gInfo.w & 2u ) != 0u ) {
			outColor = vec4( windowDebug, 1.0 );	// a window's room (11) or instance (12) as a flat colour
			return;
		}
		if ( mode == 3 && int( fr.params.w ) == 8 ) {
			const uint m = gInfo.z;	// the material's index, as below
			outColor = vec4( ( vec3( float( m & 15u ), float( ( m >> 4u ) & 15u ), float( ( m >> 8u ) & 15u ) ) * 16.0 + 8.0 ) / 255.0, 1.0 );
			return;
		}
		if ( mode == 3 && ( int( fr.params.w ) == 15 || int( fr.params.w ) == 16 || int( fr.params.w ) == 20 ) ) {
			// the index views as below, for a glowing surface too (it leaves before them: a pink ad screen once read as
			// the opaque wall behind it in 16, the views' colour as its index in 20)
#ifdef GPU_DRIVEN
			const uint id = int( fr.params.w ) == 15 ? vInst : int( fr.params.w ) == 16 ? gInfo.z : gTex.x;
#else
			const uint id = int( fr.params.w ) == 15 ? vInst : gInfo.z;
#endif
			outColor = vec4( vec3( float( id & 255u ), float( ( id >> 8u ) & 255u ), float( ( id >> 16u ) & 255u ) ) / 255.0, 1.0 );
			return;
		}
#endif
		if ( mode == 3 ) {
			const vec3 c = throughHaze( e, f, haze( fr.fogVol.w ) * 0.6 );
			outColor = vec4( screenDisplay( c ), 1.0 );
			return;
		}
		// the glow pass blurs this image and adds it over the frame (r_DynamicGlow): only light past the threshold,
		// so neon blooms and dim lit windows do not haze whole facades
		const vec3 x = e * fr.params.y;
		const float m = max( x.r, max( x.g, x.b ) );
		// compressed on its brightest channel, so the halo keeps the light's hue (per channel, a red neon's blue
		// caught up with its saturated red and the halo went pink)
		const float over = max( m - fr.params2.y / ( scr ? fr.screen2.y : 1.0 ), 0.0 );	// a screen's: r_sceneScreenGlow
		outColor = vec4( x * ( ( 1.0 - exp( -over ) ) / max( m, 1e-4 ) ) * fr.params2.x * ( 1.0 - f ), 1.0 );
		return;
	}

	vec3 albedo = toLinear( base.rgb );
	vec3 orm = ( gInfo.y & 4u ) != 0u ? texture( ORM_TEX, mUV, COARSE_BIAS ).rgb : vec3( 1.0, 0.7, 0.0 );
#ifdef GPU_DRIVEN
	if ( bbMix > 0.0 && ( gInfo.y & 4u ) != 0u )
		orm = mix( orm, texture( ORM_TEX, vUV1, COARSE_BIAS ).rgb, bbMix );
#endif
	float ao = water ? 1.0 : orm.r;	// (a leaf's: times its depth in the canopy, below)
	float metal = water ? 0.0 : clamp( orm.b, 0.0, 1.0 );
	float rough = clamp( orm.g, 0.05, 1.0 );

	// a multilayered material's main layer at the source's own tiling over the bake, which holds that layer's mean and,
	// in its ORM's red, where the layer shows (vk_scene.cpp, maps/<map>.xdet): its brightness, roughness and bumps
	const vec4 md = materialDetail[gInfo.z];
	vec2 detailSlope = vec2( 0.0 );
	if ( water )	// its own roughness, else its ORM's, else glossy
		rough = md.y > 0.0 ? md.y : ( gInfo.y & 4u ) != 0u ? clamp( orm.g, 0.02, 1.0 ) : 0.04;
	else if ( md.x > 0.0 ) {
		ao = 1.0;	// the red holds the layer's share, not occlusion
		if ( fr.detail.x > 0.0 ) {
			const vec4 d = texture( DETAIL_TEX, vUV * md.x );
			const float w = orm.r * fr.detail.x;
			albedo *= max( mix( 1.0, d.b * 2.0, w * md.w ), 0.0 );
			rough = clamp( rough + w * md.z * ( d.a - 0.5 ), 0.05, 1.0 );
			detailSlope = ( d.rg * 2.0 - 1.0 ) * ( w * md.y );
		}
	}
#ifdef GPU_DRIVEN
	else if ( md.x == -1.0 && fr.detail.x > 0.0 && fr.ssr.z > 0.5 ) {	// (-2: a layer blend, below)
		// maps/<map>.xdet version 2: up to 8 of its layers (slots, by visible share) tiled over the bake, each where its
		// share map shows it, as many as r_sceneDetailSlots: its block at md.y holds the slots' tile, normal strength,
		// roughness range and colour strength, then their images' and its share maps' indices (vk_scene.cpp)
		const uint base = uint( md.y );
		const int n = min( int( md.z ), int( fr.ssr.z ) );
		const vec4 texA = materialDetail[base + 8u], texB = materialDetail[base + 9u], texS = materialDetail[base + 10u];
		const vec2 uvx = dFdx( vUV ) * COARSE_GRAD, uvy = dFdy( vUV ) * COARSE_GRAD;	// (the loop's branches differ across a quad)
		const vec4 share0 = textureGrad( sceneTex[SCENE_TEX_INDEX( uint( texS.x ) )], vUV, uvx, uvy );
		const vec4 share1 = n > 4 && texS.y >= 0.0 ? textureGrad( sceneTex[SCENE_TEX_INDEX( uint( texS.y ) )], vUV, uvx, uvy ) : vec4( 0.0 );
		for ( int j = 0; j < n; j++ ) {
			const float share = j < 4 ? share0[j] : share1[j - 4];
			const vec4 p = materialDetail[base + uint( j )];
			if ( share <= 0.0 || p.x <= 0.0 )
				continue;
			const float t = j < 4 ? texA[j] : texB[j - 4];
			const vec4 d = textureGrad( sceneTex[SCENE_TEX_INDEX( uint( t ) )], vUV * p.x, uvx * p.x, uvy * p.x );
			const float w = share * fr.detail.x;
			albedo *= max( mix( 1.0, d.b * 2.0, w * p.w ), 0.0 );
			rough = clamp( rough + w * p.z * ( d.a - 0.5 ), 0.05, 1.0 );
			detailSlope += ( d.rg * 2.0 - 1.0 ) * ( w * p.y );
		}
	}
#endif

	// Witcher 3 layer blends (maps/<map>.xw3b, the game's pbr_vert_blend_colorize as the Witcher 3 Blender tools wire
	// it): the vertex colour's green times 16 is a place along the texture array, blending its two nearest layers;
	// the layers from the tinted first to the tinted last take the tint; red darkens toward the AO tint by the AO power;
	// blue lays the pattern (second UV set) over its layers where its mask says; roughness is the normals' alpha scaled
	bool w3 = false;
	vec3 w3n = vec3( 0.0, 0.0, 1.0 );
	// a Witcher 3 material's specular colour (-5: alone; -2: in its layer block; else the default 0.04), and whether it
	// takes the sky's specular ambient (-2 layers, -5 plain; not the terrain, -3, whose layers' roughness source is
	// unverified: its grass mirrored the sky at grazing angles)
	vec3 w3F0 = vec3( 0.04 );
	const bool w3Spec = md.x == -2.0 || md.x == -5.0 || md.x == -6.0 || md.x == -7.0;
	if ( md.x == -5.0 )
		w3F0 = md.yzw;
#ifdef GPU_DRIVEN
	if ( !water && md.x == -2.0 ) {
		w3 = true;
		const uint b = uint( md.y );
		const vec4 p0 = materialDetail[b], p1 = materialDetail[b + 1u], p2 = materialDetail[b + 2u];
		const vec4 p3 = materialDetail[b + 3u], p4 = materialDetail[b + 4u];
		const vec4 cA = materialDetail[b + 5u], cB = materialDetail[b + 6u], nA = materialDetail[b + 7u], nB = materialDetail[b + 8u];
		const vec3 f0 = materialDetail[b + 9u].rgb;	// (.xw3b version 2; 0: the default)
		if ( f0.r + f0.g + f0.b > 0.0 )
			w3F0 = f0;
		const int n = max( int( p0.x ), 1 );
		const float place = vCol.g * 16.0;
		const float L = clamp( place, 0.0, float( n - 1 ) );
		const int i0 = int( floor( L ) ), i1 = min( i0 + 1, n - 1 );
		const float f = L - float( i0 );
		const vec2 uvx = dFdx( vUV ) * COARSE_GRAD, uvy = dFdy( vUV ) * COARSE_GRAD;
		const float c0i = i0 < 4 ? cA[i0] : cB[i0 - 4], c1i = i1 < 4 ? cA[i1] : cB[i1 - 4];
		const float n0i = i0 < 4 ? nA[i0] : nB[i0 - 4], n1i = i1 < 4 ? nA[i1] : nB[i1 - 4];
		vec3 c0 = albedo, c1 = albedo;
		if ( c0i >= 0.0 )
			c0 = toLinear( textureGrad( sceneTex[SCENE_TEX_INDEX( uint( c0i ) )], vUV, uvx, uvy ).rgb );
		c1 = c0;
		if ( i1 != i0 && c1i >= 0.0 && f > 0.0 )
			c1 = toLinear( textureGrad( sceneTex[SCENE_TEX_INDEX( uint( c1i ) )], vUV, uvx, uvy ).rgb );
		albedo = mix( c0, c1, f ) * gTint.rgb;
		const float layer = floor( place );
		const float tinted = clamp( layer - p0.y + 1.0, 0.0, 1.0 ) * clamp( p0.z - layer + 1.0, 0.0, 1.0 );
		albedo = mix( albedo, albedo * p2.rgb, tinted );
		albedo *= mix( vec3( 1.0 ), vCol.r * p1.rgb, clamp( p0.w, 0.0, 1.0 ) );
		if ( p4.z >= 0.0 && p3.z > 0.0 ) {
			const float gate = clamp( layer - p3.x + 1.0, 0.0, 1.0 ) * clamp( p3.y - layer + 1.0, 0.0, 1.0 );
			const vec2 puv = vUV1 * p3.w + p4.xy;
			const vec4 pt = textureGrad( sceneTex[SCENE_TEX_INDEX( uint( p4.z ) )], puv, dFdx( puv ) * COARSE_GRAD, dFdy( puv ) * COARSE_GRAD );
			const float mask = p4.w >= 0.0 ? textureGrad( sceneTex[SCENE_TEX_INDEX( uint( p4.w ) )], vUV, uvx, uvy ).r : 1.0;
			albedo = mix( albedo, toLinear( pt.rgb ), clamp( vCol.b * pt.a, 0.0, 1.0 ) * clamp( p3.z, 0.0, 1.0 ) * mask * gate );
		}
		vec4 q0 = vec4( 0.5, 0.5, 1.0, 0.5 ), q1;
		if ( n0i >= 0.0 )
			q0 = textureGrad( sceneTex[SCENE_TEX_INDEX( uint( n0i ) )], vUV, uvx, uvy );
		q1 = q0;
		if ( i1 != i0 && n1i >= 0.0 && f > 0.0 )
			q1 = textureGrad( sceneTex[SCENE_TEX_INDEX( uint( n1i ) )], vUV, uvx, uvy );
		const vec4 q = mix( q0, q1, f );
		w3n = q.xyz * 2.0 - 1.0;
		rough = clamp( q.a * p1.w + p2.w, 0.05, 1.0 );
		metal = 0.0;
		ao = 1.0;
	}
	// Witcher 3 wear-paint (pbr_std_wear_paint, the Blender tools' "W3 Wear" chain): over the tinted base (the albedo),
	// paint where the vertex colour's blue passes its range past the masks' green, then wear (the untinted base, its
	// brightness, saturation and tint) where one minus the vertex alpha passes its range, then the vertex red's occlusion.
	// Each mask: the vertex value mapped through its range, doubled, plus the masks' green (or one minus it), less 1,
	// then sharpened about the middle by its sharpness
	if ( !water && md.x == -6.0 ) {
		const uint b = uint( md.y );
		const vec4 w0 = materialDetail[b], w1 = materialDetail[b + 1u], w2 = materialDetail[b + 2u];
		const vec4 w3v = materialDetail[b + 3u], w4 = materialDetail[b + 4u], w5 = materialDetail[b + 5u];
		const vec3 f0 = materialDetail[b + 6u].rgb;
		if ( f0.r + f0.g + f0.b > 0.0 )
			w3F0 = f0;
		const vec2 uvx = dFdx( vUV ) * COARSE_GRAD, uvy = dFdy( vUV ) * COARSE_GRAD;
		const float g = w5.z >= 0.0 ? textureGrad( sceneTex[SCENE_TEX_INDEX( uint( w5.z ) )], vUV * w1.z, uvx * w1.z, uvy * w1.z ).g : 0.0;
		vec3 col = albedo;
		if ( w0.x > 0.0 ) {
			const float range = w0.z - w0.y;
			const float m = range != 0.0 ? clamp( ( vCol.b - w0.y ) / range, 0.0, 1.0 ) : 0.0;
			const float sel = g + ( 1.0 - 2.0 * g ) * w1.x;
			const float c = clamp( m * 2.0 + sel - 1.0, 0.0, 1.0 );
			const float h = w0.w * 0.5;
			const float amount = clamp( ( c - h ) / max( 1.0 - 2.0 * h, 1e-4 ), 0.0, 1.0 ) * clamp( w0.x, 0.0, 1.0 );
			vec3 paint = w2.rgb;
			if ( w5.w >= 0.0 )
				paint *= toLinear( textureGrad( sceneTex[SCENE_TEX_INDEX( uint( w5.w ) )], vUV * w1.y, uvx * w1.y, uvy * w1.y ).rgb );
			col = mix( col, paint, amount );
		}
		if ( w2.w > 0.0 ) {
			const float range = w3v.y - w3v.x;
			const float m = range != 0.0 ? clamp( ( ( 1.0 - vCol.a ) - w3v.x ) / range, 0.0, 1.0 ) : 0.0;
			const float sel = g + ( 1.0 - 2.0 * g ) * w3v.w;
			const float c = clamp( m * 2.0 + sel - 1.0, 0.0, 1.0 );
			const float h = w3v.z * 0.5;
			const float amount = clamp( ( c - h ) / max( 1.0 - 2.0 * h, 1e-4 ), 0.0, 1.0 ) * clamp( w2.w, 0.0, 1.0 );
			const vec3 raw = w5.y >= 0.0 ? toLinear( textureGrad( sceneTex[SCENE_TEX_INDEX( uint( w5.y ) )], vUV, uvx, uvy ).rgb ) : albedo;
			const vec3 bright = raw * w4.w;
			const vec3 wear = mix( vec3( dot( bright, vec3( 0.2126, 0.7152, 0.0722 ) ) ), bright, w5.x ) * w4.rgb;
			col = mix( col, wear, amount );
		}
		albedo = mix( col, col * ( 1.0 - vCol.r ), clamp( w1.w, 0.0, 1.0 ) );
	}
	// Witcher 3 slope layer (snow.w2mg: the castle's rocks with grass on top): its second layer where the surface faces
	// up, from its low to its high cosine (ThresholdMin, Max, read as the world normal's up component), at its tiling;
	// its normal blended with the base's
	if ( !water && md.x == -7.0 ) {
		const uint b = uint( md.y );
		const vec4 s0 = materialDetail[b], s1 = materialDetail[b + 1u];
		const vec3 f0 = materialDetail[b + 2u].rgb;
		if ( f0.r + f0.g + f0.b > 0.0 )
			w3F0 = f0;
		const float up = normalize( vNrm ).z;
		const float amount = smoothstep( s0.x, max( s0.y, s0.x + 1e-3 ), up );
		const vec3 baseN = ( gInfo.y & 2u ) != 0u ? texture( NORMAL_TEX, vUV, COARSE_BIAS ).xyz * 2.0 - 1.0 : vec3( 0.0, 0.0, 1.0 );
		vec3 n = baseN;
		// (the gradients before the branch: amount varies per pixel, and inside it a quad straddling the threshold had
		// undefined derivatives)
		const vec2 uv = vUV * s0.zw, ux = dFdx( uv ), uy = dFdy( uv );
		if ( amount > 0.0 ) {
			albedo = mix( albedo, toLinear( textureGrad( sceneTex[SCENE_TEX_INDEX( uint( s1.x ) )], uv, ux, uy ).rgb ), amount );
			if ( s1.y >= 0.0 ) {
				const vec4 q = textureGrad( sceneTex[SCENE_TEX_INDEX( uint( s1.y ) )], uv, ux, uy );
				n = mix( baseN, q.xyz * 2.0 - 1.0, amount );
			}
		}
		w3 = true;
		w3n = n;
	}
#endif

	// Witcher 3 terrain (maps/<map>.xw3t, vk_scene.cpp Scene_LoadW3Terrain): the game's terrain shader as the Witcher 3
	// Blender tools emulate it. Four control taps around the tile UV (vUV1), each an overlay layer on world XY at 0.333
	// and a background layer at its UV scale (from above), weighed bilinearly; overlay and background blended by the
	// slope against the taps' threshold and sharpness; the background's normal flattened by its base dampening; the
	// tile's tint as an overlay (stored-gamma colours). Holes are cut from the mesh by the converter
	bool w3World = false;
	vec3 w3Nw = vec3( 0.0, 0.0, 1.0 );
#ifdef GPU_DRIVEN
	if ( !water && md.x == -3.0 && pc.tint.w < -0.5 ) {	// r_sceneTerrainFlat (bench): one colour, no reads
		w3World = true;
		albedo = vec3( 0.12, 0.14, 0.08 );
		rough = 0.8;
	} else if ( !water && md.x == -3.0 ) {
		w3World = true;
		const uint b = uint( md.y );
		const vec4 h0 = materialDetail[b], h1 = materialDetail[b + 1u];
		const uint lb = uint( h0.x ), m = uint( h0.y );
		const int nl = int( h0.w );
		const vec2 world = vPos.xy / UNITS_PER_METRE + h1.xy;
		const vec2 grid = clamp( vUV1, 0.0, 1.0 ) * float( m - 1u );
		const uvec2 c0 = min( uvec2( floor( grid ) ), uvec2( m - 2u ) );
		const vec2 fr2 = grid - vec2( c0 );
		const float wts[4] = float[4]( ( 1.0 - fr2.x ) * ( 1.0 - fr2.y ), fr2.x * ( 1.0 - fr2.y ), ( 1.0 - fr2.x ) * fr2.y, fr2.x * fr2.y );
		const uvec2 offs[4] = uvec2[4]( uvec2( 0u, 0u ), uvec2( 1u, 0u ), uvec2( 0u, 1u ), uvec2( 1u, 1u ) );
		// the background's UV scales: the next-gen game's own (its DX12 terrain shader's table, 7 entries, read from the
		// shader cache; the Blender tools' 0.333, 0.166, 0.05, 0.025, 0.0125, 0.0075, 0.00375 repeated most
		// ground 1.2-3x less often, its stones and rock that much larger); index 7, past the game's table, keeps 0
		const float scaleLut[8] = float[8]( 0.4, 0.25, 0.15, 0.075, 0.05, 0.025, 0.00154, 0.0 );
		const float slopeLut[8] = float[8]( 0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.98 );
		const vec2 gx = dFdx( world ) * COARSE_GRAD, gy = dFdy( world ) * COARSE_GRAD;
		// the background's three projections (from above, along x, along y), weighed by the surface's facing as the
		// Blender tools weigh its normals (|n| past 0.576); its colour too, so cliffs do not streak (the tools keep the
		// colour from above)
		const vec3 nf = normalize( vNrm );
		vec3 tw = max( abs( nf ) - 0.576, 0.0 );
		// past r_sceneTerrainFar (pc.tint.w, units; 0 never): the facing's main projection only, the others faded out
		// over the next quarter of the distance, and no normal maps (w3TerrainLayer's fl); far terrain's texture
		// reads were ~1 ms of its ~2.8 at 4K (perf-20261125, r_sceneTerrainFlat)
		float fl = 0.0;
		if ( pc.tint.w > 0.0 ) {
			fl = clamp( ( length( vPos - pc.eye.xyz ) - pc.tint.w ) / ( 0.25 * pc.tint.w ), 0.0, 1.0 );
			const float tm = max( tw.x, max( tw.y, tw.z ) );
			tw = mix( tw, vec3( greaterThanEqual( tw, vec3( tm ) ) ) * tm, fl );
		}
		tw /= max( tw.x + tw.y + tw.z, 1e-4 );
		const float zW = vPos.z / UNITS_PER_METRE;
		const vec2 gzx = vec2( dFdx( zW ), dFdy( zW ) ) * COARSE_GRAD;
		vec3 ovD = vec3( 0.0 ), bgD = vec3( 0.0 ), ovN = vec3( 0.0 ), bgN = vec3( 0.0 );
		float ovR = 0.0, bgR = 0.0, thr = 0.0, sharp = 0.0, baseDamp = 0.0, normDamp = 0.0;
		vec4 spOv = vec4( 0.0 ), spBg = vec4( 0.0 );	// the layers' specular values (xw3t version 2)
		// the four taps' scalars, and their layers merged: a layer (the background's with its UV scale) that several
		// taps share is read once at their summed weight (the same image: the reads are linear in the weights). On
		// Beauclair a cell's four taps hold 1.31 distinct overlays and 1.42 distinct backgrounds; the
		// game's own shader reads every tap. Each tap's weight goes to the first tap with its layer, written out (no
		// run-time array index)
		ivec4 ovI = ivec4( 0 ), bgI = ivec4( 0 );
		vec4 wv = vec4( 0.0 ), bgSc = vec4( 0.0 );
		for ( int k = 0; k < 4; k++ ) {
			const uvec2 c = c0 + offs[k];
			const uint at = c.y * m + c.x;
			const uint word = uint( materialDetail[lb + at / 4u][at % 4u] );
			const float w = wts[k];
			const int ov = clamp( int( word & 31u ) - 1, 0, nl - 1 ), bg = clamp( int( ( word >> 5u ) & 31u ) - 1, 0, nl - 1 );
			const float sc = scaleLut[( word >> 13u ) & 7u];
			thr += w * slopeLut[( word >> 10u ) & 7u];
			sharp += w * materialDetail[b + 2u + uint( ov )].x;
			baseDamp += w * materialDetail[b + 2u + uint( bg )].y;
			normDamp += w * materialDetail[b + 2u + uint( bg )].z;
			if ( h1.z > 0.5 ) {
				spOv += w * materialDetail[b + 50u + uint( ov )];
				spBg += w * materialDetail[b + 50u + uint( bg )];
			}
			ovI[k] = ov;
			bgI[k] = bg;
			bgSc[k] = sc;
			wv[k] = w;
		}
		vec4 wo, wb;
		wo.x = wv.x + ( ovI.y == ovI.x ? wv.y : 0.0 ) + ( ovI.z == ovI.x ? wv.z : 0.0 ) + ( ovI.w == ovI.x ? wv.w : 0.0 );
		wo.y = ovI.y != ovI.x ? wv.y + ( ovI.z == ovI.y ? wv.z : 0.0 ) + ( ovI.w == ovI.y ? wv.w : 0.0 ) : 0.0;
		wo.z = ( ovI.z != ovI.x && ovI.z != ovI.y ) ? wv.z + ( ovI.w == ovI.z ? wv.w : 0.0 ) : 0.0;
		wo.w = ( ovI.w != ovI.x && ovI.w != ovI.y && ovI.w != ovI.z ) ? wv.w : 0.0;
		const bvec4 sx = bvec4( true, bgI.y == bgI.x && bgSc.y == bgSc.x, bgI.z == bgI.x && bgSc.z == bgSc.x, bgI.w == bgI.x && bgSc.w == bgSc.x );
		const bool yz = bgI.z == bgI.y && bgSc.z == bgSc.y, yw = bgI.w == bgI.y && bgSc.w == bgSc.y, zw = bgI.w == bgI.z && bgSc.w == bgSc.z;
		wb.x = wv.x + ( sx.y ? wv.y : 0.0 ) + ( sx.z ? wv.z : 0.0 ) + ( sx.w ? wv.w : 0.0 );
		wb.y = !sx.y ? wv.y + ( yz ? wv.z : 0.0 ) + ( yw ? wv.w : 0.0 ) : 0.0;
		wb.z = ( !sx.z && !yz ) ? wv.z + ( zw ? wv.w : 0.0 ) : 0.0;
		wb.w = ( !sx.w && !yw && !zw ) ? wv.w : 0.0;
		// the overlay from the same three directions as the background (the tools draw it from above only, which
		// stretched it down the cliffs of the castle hill and the lower city's slopes)
		const vec2 uo = world * 0.333;
		const vec2 os = vec2( -world.x, -zW ) * 0.333, ox = vec2( -world.y, -zW ) * 0.333;
		const vec2 osx = vec2( -gx.x, -gzx.x ) * 0.333, osy = vec2( -gy.x, -gzx.y ) * 0.333;
		const vec2 oxx = vec2( -gx.y, -gzx.x ) * 0.333, oxy = vec2( -gy.y, -gzx.y ) * 0.333;
#define W3_OVC( i )	materialDetail[b + 34u + uint( i ) / 4u][( i ) % 4]
#define W3_OVN( i )	materialDetail[b + 42u + uint( i ) / 4u][( i ) % 4]
#define W3_OV( i, w )	w3TerrainLayer( W3_OVC( i ), W3_OVN( i ), w, tw, uo, gx * 0.333, gy * 0.333, os, osx, osy, ox, oxx, oxy, fl, ovD, ovN, ovR )
#define W3_BG( i, w, sc )	w3TerrainBg( W3_OVC( i ), W3_OVN( i ), w, sc, tw, world, zW, gx, gy, gzx, fl, bgD, bgN, bgR )
		W3_OV( ovI.x, wo.x ); W3_OV( ovI.y, wo.y ); W3_OV( ovI.z, wo.z ); W3_OV( ovI.w, wo.w );
		W3_BG( bgI.x, wb.x, bgSc.x ); W3_BG( bgI.y, wb.y, bgSc.y ); W3_BG( bgI.z, wb.z, bgSc.z ); W3_BG( bgI.w, wb.w, bgSc.w );
		const vec3 n = nf;
		const vec3 Tw = tangentOf( vec3( 1.0, 0.0, 0.0 ), n ), Bw = cross( n, Tw );	// (+x, or +y for a normal along x)
		const vec3 ovT = normalize( vec3( ovN.xy, max( ovN.z, 1e-4 ) ) ), bgT = normalize( vec3( bgN.xy, max( bgN.z, 1e-4 ) ) );
		const vec3 cov = Tw * ovT.x + Bw * ovT.y + n * ovT.z, cbg = Tw * bgT.x + Bw * bgT.y + n * bgT.z;
		const vec3 biased = normalize( mix( cbg, mix( cbg, vec3( 0.0, 0.0, 1.0 ), n.z ), baseDamp ) );
		const float slope = clamp( length( biased.xy ) / max( biased.z, 1e-4 ), 0.0, 1.0 );
		const float hi = clamp( thr + sharp, 0.0, 1.0 );
		const float blend = clamp( ( slope - thr ) / max( hi - thr, 1e-4 ), 0.0, 1.0 );
		vec3 diff = mix( ovD, bgD, blend );
		if ( h0.z >= 0.0 ) {	// the tint, an overlay in stored gamma
			const vec3 t = texture( sceneTex[SCENE_TEX_INDEX( uint( h0.z ) )], clamp( vUV1, 0.0, 1.0 ) ).rgb;
			const vec3 dark = 2.0 * t * diff, light = 1.0 - 2.0 * ( 1.0 - t ) * ( 1.0 - diff );
			diff = mix( dark, light, step( 0.5, t ) );
		}
		albedo = toLinear( clamp( diff, 0.0, 1.0 ) ) * gTint.rgb;
		rough = clamp( mix( ovR, bgR, blend ), 0.05, 1.0 );
		metal = 0.0;
		if ( h1.z > 0.5 ) {
			// the layers' specular level, as the Blender tools' terrain computes it: specularity^2.2 x ((1 - roughness) x
			// falloff + (specularity base - 0.5) x 3), clamped 0..1; the default 0.04 gave the cobbles a sheen the
			// game's near-zero values do not
			const vec4 sp = mix( spOv, spBg, blend );
			w3F0 = vec3( clamp( pow( max( sp.x, 0.0 ), 2.2 ) * ( ( 1.0 - mix( ovR, bgR, blend ) ) * sp.z + ( sp.y - 0.5 ) * 3.0 ), 0.0, 1.0 ) );
		}
		ao = 1.0;
		const float sz = cbg.z >= 0.0 ? max( cbg.z, 1e-4 ) : min( cbg.z, -1e-4 ), oz = cov.z >= 0.0 ? max( cov.z, 1e-4 ) : min( cov.z, -1e-4 );
		const vec3 full = normalize( vec3( ( 1.0 - normDamp ) * cbg.xy / sz + normDamp * cov.xy / oz, 1.0 ) );
		w3Nw = normalize( mix( full, cbg, blend ) );
	}
#endif

	float decalWet = 0.0;	// (a projected puddle decal's wetness, below)
	vec3 decalE = vec3( 0.0 );	// (and its emission, added at the end)
#ifndef DECAL
	// mesh decals (r_sceneDecals): the DBuffer's albedo over this surface's (its normal below), where they were drawn on
	// this surface: its view depth, within the half float's rounding and the pixel's slope (MSAA: the first sample's)
	vec4 decalN = vec4( 0.0 );
	// (not on a loose prop: CP2077's world decals mark the static street and walls, not the boxes, cans and devices on
	// them, instance slot bit 25, tag_interiors.py; without it the graffiti slid over the debris boxes as the eye
	// moved)
	if ( fr.detail.y > 0.5 && pc.eye.w > 0.5 && mode != 2 && !water && ( vSignal & 0x2000000u ) == 0u ) {
		const ivec2 px = ivec2( gl_FragCoord.xy );
		const float z = dot( vPos - fr.eye.xyz, fr.fogFwd.xyz );
		if ( abs( texelFetch( decalDepthTex, px, 0 ).r - z ) <= 1.0 + 0.004 * z + fwidth( z ) ) {
			const vec4 a = texelFetch( decalAlbedoTex, px, 0 );
			if ( a.a > 0.0 )
				albedo = albedo * ( 1.0 - a.a ) + toLinear( a.rgb / a.a ) * a.a;
			decalN = texelFetch( decalNormalTex, px, 0 );
			// the projected decals' roughness and metalness over the surface's, their puddles (wet while it rains)
			decalE = texelFetch( decalEmissionTex, px, 0 ).rgb;
			const vec4 s = texelFetch( decalSurfaceTex, px, 0 );
			if ( s.a > 0.0 ) {
				rough = mix( rough, clamp( s.r / s.a, 0.02, 1.0 ), s.a );
				metal = mix( metal, clamp( s.g / s.a, 0.0, 1.0 ), s.a );
				decalWet = s.b;
			}
		}
	}
#endif

	const vec3 V = normalize( pc.eye.xyz - vPos );
	vec3 Ng = safeNormalize( vNrm, vec3( 0.0, 0.0, 1.0 ) );
	const float side = dot( Ng, V ) < 0.0 ? -1.0 : 1.0;	// two-sided: the face toward the eye
	Ng *= side;
	vec3 N = Ng;
	// water: its ripples; the path through it to the bed (m) and the share of the bed that shows through, the foam
	float waterPath = 0.0, waterSeen = 1.0, foam = 0.0;
	if ( water ) {
		N = waterNormal( Ng, md );
		if ( waterLostVar > 0.0 )	// (alpha^2 + 2 x the slope variance the faded cascades gave up; alpha = rough^2)
			rough = sqrt( sqrt( rough * rough * rough * rough + 2.0 * waterLostVar ) );
		waterFlatLight = fr.water2.w > 0.5;
		waterFlatN = Ng;
		const float zw = max( dot( vPos - pc.eye.xyz, fr.fogFwd.xyz ), 1.0 );
		if ( fr.waterBed.w > 1 && fr.surface2.w > 0.5 && pc.eye.w > 0.5 ) {
			// r_sceneWaterBed: this pixel's own bed, its samples' nearest and farthest (32-bit floats) and the share at the
			// nearest (a bed fading out over a few samples, an edge): the share that shows through and the path, mixed
			// over the two. The eye's view only (the bed is its pixels; another view takes waterBed()'s 1e30, as before)
			const float vis = max( gEmissive.x * fr.water2.x, 0.01 ), slant = length( vPos - pc.eye.xyz ) / zw / UNITS_PER_METRE;
			const uint at = uint( fr.waterBed.w ) + 2u * ( uint( gl_FragCoord.y ) * uint( fr.surface2.w ) + uint( gl_FragCoord.x ) );
			const uint b0 = pyramidBits[at], b1 = pyramidBits[at + 1u];
			const float share = float( b1 & 7u ) / max( fr.fx2.w, 1.0 );
			const float p0 = max( uintBitsToFloat( b0 ) - zw, 0.0 ) * slant, p1 = max( uintBitsToFloat( b1 & ~7u ) - zw, 0.0 ) * slant;
			waterPath = mix( p1, p0, share );
			waterSeen = mix( exp( -p1 / vis ), exp( -p0 / vis ), share );
		} else {	// (as before r_sceneWaterBed, to the last bit)
			waterPath = max( waterBed() - zw, 0.0 ) * ( length( vPos - pc.eye.xyz ) / zw ) / UNITS_PER_METRE;
			waterSeen = exp( -waterPath / max( gEmissive.x * fr.water2.x, 0.01 ) );
		}
		if ( md.w > 0.0 ) {	// foam near the shore: its image at the ripples' tiling, drifting with the first
			const vec2 uv = ( vPos.xy / UNITS_PER_METRE + vec2( cos( md.x ), sin( md.x ) ) * ( fr.water.x * gEmissive.w ) ) / max( gEmissive.z, 0.05 );
			foam = texture( DETAIL_TEX, uv ).r * gTint.a * ( 1.0 - smoothstep( 0.0, md.w, waterPath ) ) * smoothstep( 0.0, gEmissive.y * 0.5, waterPath );
		}
		// the water's own light: its colour, as much of it as hides the bed, lit as a diffuse surface; foam over it
		albedo = mix( albedo * ( 1.0 - waterSeen ), vec3( 0.8 ), foam );
	} else if ( w3World ) {	// the terrain's world-space normal (above)
		N = normalize( w3Nw * side );
	} else if ( w3 || ( gInfo.y & 2u ) != 0u || detailSlope != vec2( 0.0 ) ) {
		const vec3 T = tangentOf( vTan.xyz, Ng );
		const vec3 B = cross( Ng, T ) * vTan.w * side;
		vec3 n = w3 ? w3n : ( gInfo.y & 2u ) != 0u ? texture( NORMAL_TEX, mUV, COARSE_BIAS ).xyz * 2.0 - 1.0 : vec3( 0.0, 0.0, 1.0 );
#ifdef GPU_DRIVEN
		if ( bbMix > 0.0 && !w3 && ( gInfo.y & 2u ) != 0u )
			n = mix( n, texture( NORMAL_TEX, vUV1, COARSE_BIAS ).xyz * 2.0 - 1.0, bbMix );
#endif
		N = normalize( T * ( n.x + detailSlope.x ) + B * ( n.y + detailSlope.y ) + Ng * max( n.z, 0.05 ) );
	}
#ifndef DECAL
	if ( decalN.a > 0.0 )	// the decals' normals, held weighted (the sum of n w, then of w)
		N = normalize( N * ( 1.0 - decalN.a ) + ( decalN.rgb * 2.0 - decalN.a ) );
#endif
	// a SpeedTree leaf (info.w 32, r_sceneFoliage): its card's normal (each card its own way: the trees lit and shaded
	// leaf by leaf) pulled toward its tree's outward direction (scene.vert's vCanopy), so the canopy is lit and shaded as
	// one rounded mass; its inner leaves darker
	// r_sceneTreeTint: the hour's colour and saturation of the trees' leaves (The Witcher 3's m_speedTree.randomColorsTrees,
	// relative to the calibrated hour: blue-grey and darker by night); the far trees' billboard cards take it too, so a
	// tree keeps its colour across the switch from its leaves to its card
	if ( ( gInfo.w == 32u || bbCard ) && !water ) {
		const float l = dot( albedo, vec3( 0.2126, 0.7152, 0.0722 ) );
		albedo = max( mix( vec3( l ), albedo, fr.treeTint.w ), vec3( 0.0 ) ) * fr.treeTint.rgb;
	}
	const bool leaf = gInfo.w == 32u && fr.foliage.x > 0.0 && !water;
	float leafAO = 1.0;
	if ( leaf ) {
		const float r = length( vCanopy );
		const vec3 Nc = r > 1e-3 ? vCanopy / r : Ng;
		const vec3 card = normalize( vNrm );
		N = normalize( mix( dot( card, Nc ) < 0.0 ? -card : card, Nc, fr.foliage.x ) );
		leafAO = mix( 1.0, smoothstep( 0.1, 0.9, r ), fr.foliage.w );
	}
	// a billboard card (scene.vert: vCanopy.z 1 + its height share): its canopy darker toward its foot as the 3D tree's
	// inner and lower leaves are (r_sceneFoliageAO), in its ambient light here and its sun below; a card was lit evenly
	// top to bottom, the far woods flat
	float cardShade = 1.0;
	if ( bbCard && vCanopy.z > 0.5 ) {
		cardShade = mix( 1.0, smoothstep( 0.0, 0.85, vCanopy.z - 1.0 ), fr.foliage.w );
		leafAO = cardShade;
	}

	// wet ground (rain), up-facing surfaces by porosity (r_sceneWetPorosity: CP2077's RainAreaSettings; Lagarde, "Water
	// drop 3", 2013): a surface's porosity from its dry roughness (none on metal); the water a porous surface soaks up
	// darkens it and leaves its roughness (asphalt keeps its grain), a smooth one takes a film, glossy as far as it is not
	// porous; puddles, laid by noise over the world plane, are near mirrors with the surface's bumps under water. Off, every
	// wet surface one film, darkened alike
	const bool porous = fr.wet2.y > fr.wet2.x;
	const float roughDry = rough;
	float wet = 0.0, puddle = 0.0, porosity = 0.0;
	if ( fr.wet.x > 0.0 && !water ) {
		wet = fr.wet.x * smoothstep( 0.55, 0.9, Ng.z );
		if ( wet > 0.0 )
			wet *= rainReach( vPos );	// covered and indoor floors stay dry
		if ( wet > 0.0 && fr.wet.y > 0.0 ) {
			const vec2 q = vPos.xy / UNITS_PER_METRE;
			const vec2 r = mat2( 0.8, 0.6, -0.6, 0.8 ) * q;
			const float n = gradientNoise( q / 4.0 ) * 0.6 + gradientNoise( r / 1.5 + 17.0 ) * 0.3 + gradientNoise( q / 0.5 + 5.0 ) * 0.1;
			const float t = 1.0 - fr.wet.y;
			puddle = wet * smoothstep( t - 0.05, t + 0.05, n );
		}
		if ( decalWet > 0.0 )	// a projected puddle decal (r_scenePDecals), where the rain reaches (covered floors stay dry)
			puddle = max( puddle, decalWet * fr.wet.x * rainReach( vPos ) );
		wet = max( wet, puddle );
		if ( porous )
			porosity = clamp( ( rough - fr.wet2.x ) / ( fr.wet2.y - fr.wet2.x ), 0.0, 1.0 ) * ( 1.0 - metal );
		rough = mix( rough, min( rough, WET_ROUGH ), wet * ( 1.0 - porosity ) );	// a water film, not a mirror
		rough = mix( rough, 0.05, puddle );
		N = normalize( mix( N, Ng, puddle ) );
	}
	// geometric specular anti-aliasing (Kaplanyan et al. 2016; Tokuyoshi and Kaplanyan 2019): the shading normal's change
	// across the pixel widens the lobe (alpha squared, at most fr.surface2.x), so a fine normal map (diamond plate) no
	// longer sparkles a pixel at a time in highlights and reflections
	if ( fr.surface2.x > 0.0 ) {
		const vec3 du = dFdx( N ), dv = dFdy( N );
		const float a2 = rough * rough * rough * rough + min( 0.5 * ( dot( du, du ) + dot( dv, dv ) ), fr.surface2.x );
		rough = min( sqrt( sqrt( a2 ) ), 1.0 );
	}
	const vec3 albedoWet = albedo * ( ( porous ? mix( 1.0, fr.wet2.z, wet * porosity ) : mix( 1.0, 0.45, wet ) ) * mix( 1.0, 0.7, puddle ) );
	const vec3 F0 = water ? vec3( WATER_F0 ) : mix( w3F0, albedoWet, metal );
	const vec3 diffuseColor = albedoWet * ( 1.0 - metal );
	const float NdotV = max( dot( N, V ), 1e-4 );

	ao *= leafAO;
	if ( fr.clouds2.z > 0.0 && pc.eye.w > 0.5 && !water )	// r_sceneAO: the screen's occlusion on the ambient light
		ao *= mix( 1.0, occlusionAt( vPos ), fr.clouds2.z );
	// (r_sceneProbeSky: CP2077 lights ambient from the surface's reflection probes' relit captures, whose sky texels
	// give the sky's light; an enclosed room's capture holds none)
	const vec3 hemi = mix( fr.groundAmbient.rgb, fr.skyAmbient.rgb, N.z * 0.5 + 0.5 )
		* ( 1.0 - fr.surface2.z * float( vSignal >> 28u ) / 15.0 );
	// a Witcher 3 surface: the ambient mirrored with its own specular colour through the split-sum BRDF, in place of
	// the flat F0 / 4: the ambient hemisphere along the reflected ray (the game lights reflections from its probes, near
	// neutral). Not waterSky: its haze end is the fog colour as linear light, which for Beauclair's cyan fog was several
	// times the 0.2 ambient, so every wall and pavement seen at a glancing angle mirrored bright cyan (the fly-through's
	// "wet blue" shade)
	const vec2 w3ab = envBRDF( rough, NdotV );
	const vec3 Rw = reflect( -V, N );
	const vec3 hemiR = mix( fr.groundAmbient.rgb, fr.skyAmbient.rgb, Rw.z * 0.5 + 0.5 );
	const vec3 ambient = w3Spec
		? hemi * diffuseColor * ao + hemiR * ( F0 * w3ab.x + w3ab.y ) * ao
		: hemi * ( diffuseColor + ( water ? vec3( 0.0 ) : F0 * 0.25 ) ) * ao;	// (water: its reflection below)
	vec3 col = ambient * bbFix.rgb;	// (a billboard card: its 3D tree's canopy, darker inside, took less of the ambient)

	// wet surfaces mirror what was on screen (neon, screens, lamps, lit windows): scene_ssr.comp's light, with this
	// surface's Fresnel; by porosity, fading out from half r_sceneWetPorosity's roughness to it (a rough surface mirrors
	// nothing sharp: its sheen is the lights' own specular)
	vec3 reflection = vec3( 0.0 );
	const float glossy = porous && fr.wet2.w > 0.0 ? clamp( 2.0 - 2.0 * rough / fr.wet2.w, 0.0, 1.0 ) : 1.0;
	if ( fr.ssr.x > 0.5 && pc.eye.w > 0.5 && wet > 0.05 && glossy > 0.0 && mode != 2 ) {
		const vec2 ab = envBRDF( rough, NdotV );
		reflection = reflectedLight( vPos ) * ( F0 * ab.x + ab.y ) * ( wet * glossy * fr.ssr.y * ao );
	}
	if ( water ) {	// the screen's reflection where it holds this surface, nudged by the ripples, else the sky's
		const vec4 r = fr.ssr.x > 0.5 && pc.eye.w > 0.5 ? reflectedLightAt( vPos + vec3( ( N.xy - Ng.xy ) * fr.water2.z, 0.0 ) ) : vec4( 0.0 );
		const vec2 ab = envBRDF( rough, NdotV );
		reflection = ( r.rgb * r.a + waterSky( reflect( -V, N ) ) * ( 1.0 - r.a ) ) * ( F0 * ab.x + ab.y ) * ( fr.water2.y * ( 1.0 - foam ) );
	}
	col += reflection;
	vec3 sunPart = vec3( 0.0 );

	float sun = 1.0;
	if ( kSun != 0 && fr.sunDir.w > 0.5 && leaf ) {
		// a leaf: diffuse wrapped round the canopy, and the sun through it where the eye looks toward the sun, in the
		// leaf's colour deepened (light through a leaf crosses its pigment twice); the shadow looked up on the sun's side
		const vec3 l = fr.sunDir.xyz;
		const float w = fr.foliage.y;
		const float wrapL = max( ( dot( N, l ) + w ) / ( 1.0 + w ), 0.0 );
		const float back = pow( max( -dot( V, l ), 0.0 ), 4.0 ) + 0.25 * max( -dot( N, l ), 0.0 );
		if ( wrapL > 0.0 || back > 0.0 ) {
			sun = sunShadow( vPos, dot( Ng, l ) < 0.0 ? -Ng : Ng );
			const vec3 trans = min( diffuseColor * diffuseColor * 4.0, vec3( 1.0 ) );
			sunPart = ( diffuseColor * wrapL + trans * ( fr.foliage.z * back ) ) * ( fr.sunCol.rgb * ( sun / PI ) );
			col += sunPart;
		}
	} else if ( kSun != 0 && fr.sunDir.w > 0.5 ) {
		const vec3 l = fr.sunDir.xyz;
		// a billboard card stands for a whole canopy, turned to the eye: the sun wraps round it (half way) from any side,
		// its shadow looked up on the sun's side; as a sheet it took no sun at all with the sun behind the tree (the
		// 3D tree's leaves catch it from every side: cardmatch.py's sun ratios spread 20x between the two headings)
		const float NdotL = bbCard ? ( dot( N, l ) + 0.5 ) / 1.5 : dot( N, l );
		if ( NdotL > 0.0 && ( bbCard || dot( Ng, l ) > -0.1 ) ) {
			sun = sunShadow( vPos, bbCard && dot( Ng, l ) < 0.0 ? -Ng : Ng );
			if ( sun > 0.0 )
				sunPart = brdf( N, V, l, NdotL, NdotV, F0, diffuseColor, rough, 0.0 ) * fr.sunCol.rgb * ( NdotL * sun * bbFix.w * cardShade );
			col += sunPart;	// (outside the test: sunPart stays 0 when sun is 0)
		}
	}

	// the lights that may reach P: its cluster's (the eye view, r_sceneClusters), else its light grid cell's. The cell
	// decides where it kept only its strongest lights (its count's top bit: the cluster holds them all; with
	// r_sceneClusters 2, cluster.z 2, the cluster decides there too) or the cluster is full; elsewhere both list every
	// light that can reach P, in the same order
	uint count = 0u, reached = 0u, first = 0u;
	uvec2 cell = uvec2( 0u );
	const ivec3 c = ivec3( floor( ( vPos - fr.gridOrigin.xyz ) / fr.gridOrigin.w ) );
	if ( all( greaterThanEqual( c, ivec3( 0 ) ) ) && all( lessThan( c, fr.gridDims.xyz ) ) )
		cell = cells[( c.z * fr.gridDims.y + c.y ) * fr.gridDims.x + c.x];
	bool clustered = false;
	if ( fr.cluster.z > 0.5 && pc.eye.w > 0.5 && ( ( cell.y & 0x80000000u ) == 0u || fr.cluster.z > 1.5 ) ) {
		const vec3 v = vPos - pc.eye.xyz;
		const float z = max( dot( v, fr.fogFwd.xyz ), 1e-3 );
		const vec2 uv = 0.5 - 0.5 * vec2( dot( v, fr.fogLeft.xyz ) / ( z * fr.fogLeft.w ), dot( v, fr.fogUp.xyz ) / ( z * fr.fogUp.w ) );
		const ivec3 cc = clamp( ivec3( ivec2( floor( uv * vec2( CLUSTER_X, CLUSTER_Y ) ) ), int( floor( log( z / fr.fogFwd.w ) * fr.cluster.x ) ) ),
			ivec3( 0 ), ivec3( CLUSTER_X - 1, CLUSTER_Y - 1, CLUSTER_Z - 1 ) );
		const uint at = uint( ( cc.z * CLUSTER_Y + cc.y ) * CLUSTER_X + cc.x ) * CLUSTER_STRIDE;
		const uint word = clusterLights[at];
		if ( ( word & 0x80000000u ) == 0u ) {
			clustered = true;
			first = at + 1u;
			count = word;
		}
	}
	if ( !clustered ) {
		first = cell.x;
		count = cell.y & 0x7fffffffu;
	}
	if ( fr.params.x > 0.0 ) {
		for ( uint i = 0u; i < count; i++ ) {
			const uint li = clustered ? clusterLights[first + i] : cellLights[first + i];
			// the 16-byte sphere first: most listed lights fail its test, and the rest of the record is loaded only for
			// those that pass (every test must pass, so their order changes nothing)
			const vec4 posRadius = lights[li].posRadius;
			const vec3 Lv = posRadius.xyz - vPos;
			const float d2 = dot( Lv, Lv );
			if ( d2 >= posRadius.w * posRadius.w )
				continue;
			const Light L = lights[li];
			const int type = int( L.cosInnerType.y );
			if ( ( type & 2 ) != 0 )
				continue;
			// a tube (scene version 6): its point nearest P stands for it, its falloff a line source's (tubeFalloff); not
			// read when no surface light keeps a shape (r_sceneLightTubes 2: only the haze-only lights, skipped above)
			const vec4 S = fr.fx2.y > 0.5 ? boxes[li].shape : vec4( 0.0 );
			vec3 Lq = Lv;
			float reach = posRadius.w, line = 0.0;
			if ( S.x != 0.0 || S.y != 0.0 || S.z != 0.0 ) {
				vec3 Q;
				line = tubeFalloff( posRadius.xyz, S, vPos, 1.0, Q );
				Lq = Q - vPos;
				reach -= length( S.xyz );
			}
			const float d = length( Lq );
			const vec3 l = Lq / max( d, 1e-3 );
			const float NdotL = dot( N, l );
			if ( NdotL <= 0.0 )
				continue;
			const vec2 fade = lightFade[li].xy;	// (1, 1) unless it is near or past its hide distance
			if ( fade.x <= 0.0 )
				continue;
#ifndef NO_DEBUG_VIEWS
			reached++;
#endif
			if ( L.cosInnerType.z > 0.5 ) {
				// an interior light (CP light channel): only inside its volume
				const vec4 p = vec4( vPos, 1.0 );
				const LightBox B = boxes[li];
				if ( max( abs( dot( B.r0, p ) ), max( abs( dot( B.r1, p ) ), abs( dot( B.r2, p ) ) ) ) > 1.0 )
					continue;
			}
			const float x = d / max( reach, 1.0 );
			float att = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
			const float dm = d / UNITS_PER_METRE;
			att = att * att * ( line > 0.0 ? line : 1.0 / ( dm * dm + 1.0 ) );
			if ( ( type & 1 ) != 0 )
				att *= smoothstep( L.dirCosOuter.w, L.cosInnerType.x, dot( -l, L.dirCosOuter.xyz ) );
			if ( ( type & 4 ) != 0 && att > 0.0 ) {
				const float sh = lightShadow( li, vPos, Ng, l, d, 1 );
				att *= fade.y < 1.0 ? mix( 1.0, sh, fade.y ) : sh;	// (at full strength exactly as before)
			}
			att *= fade.x;
			if ( ( type & 8 ) != 0 )	// a signal's lamp (bit 3, lamp in bits 4-5, slot from bit 6): as its state now
				att *= fr.signals[min( type >> 6, 255 )][( type >> 4 ) & 3];
			if ( att <= 0.0 )
				continue;	// outside its cone or in its shadow: the BRDF would be scaled by zero (most lights are spots)
			col += brdf( N, V, l, NdotL, NdotV, F0, diffuseColor, rough, fr.wet.z / ( 2.0 * max( dm, 0.1 ) ) ) * L.colorPower.rgb * ( L.colorPower.w * fr.params.x * att * NdotL );
		}
		// CP2077's distant lights past their hide distance (r_sceneLightDistant): the cell's stand-ins as plain points,
		// with the full light's falloff, cone and state but no shadow, box or haze, at the share the full light gave up
		if ( fr.cluster.w > 0.5 && all( greaterThanEqual( c, ivec3( 0 ) ) ) && all( lessThan( c, fr.gridDims.xyz ) ) ) {
			const uint at = uint( ( c.z * fr.gridDims.y + c.y ) * fr.gridDims.x + c.x ) * DIST_STRIDE;
			const uint n = distantCells[at];
			for ( uint k = 0u; k < n; k++ ) {
				const uint li = distantCells[at + 1u + k];
				const vec4 posRadius = lights[li].posRadius;	// (the sphere first, as above)
				const vec3 Lv = posRadius.xyz - vPos;
				const float d2 = dot( Lv, Lv );
				if ( d2 >= posRadius.w * posRadius.w )
					continue;
				const Light L = lights[li];
				const int type = int( L.cosInnerType.y );
				if ( ( type & 2 ) != 0 )
					continue;
				const float d = sqrt( d2 );
				const vec3 l = Lv / max( d, 1e-3 );
				const float NdotL = dot( N, l );
				if ( NdotL <= 0.0 )
					continue;
				const float x = d / L.posRadius.w;
				float att = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
				const float dm = d / UNITS_PER_METRE;
				att = att * att / ( dm * dm + 1.0 ) * lightFade[li].z;
				if ( ( type & 1 ) != 0 )
					att *= smoothstep( L.dirCosOuter.w, L.cosInnerType.x, dot( -l, L.dirCosOuter.xyz ) );
				if ( ( type & 8 ) != 0 )
					att *= fr.signals[min( type >> 6, 255 )][( type >> 4 ) & 3];
				if ( att > 0.0 )
					col += brdf( N, V, l, NdotL, NdotV, F0, diffuseColor, rough, fr.wet.z / ( 2.0 * max( dm, 0.1 ) ) ) * L.colorPower.rgb * ( L.colorPower.w * fr.params.x * att * NdotL );
			}
		}
	}
	// the game's dynamic lights (a saber's glow, shots, effects), with the source's lights' falloff within their radius
	for ( int i = 0; i < int( fr.dlightCount.x ); i++ ) {
		const vec4 D = fr.dlights[i * 2];
		const vec3 Lv = D.xyz - vPos;
		const float d2 = dot( Lv, Lv );
		if ( d2 >= D.w * D.w )
			continue;
		const float d = sqrt( d2 );
		const vec3 l = Lv / max( d, 1e-3 );
		const float NdotL = dot( N, l );
		if ( NdotL <= 0.0 )
			continue;
		const float x = d / D.w;
		float att = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
		const float dm = d / UNITS_PER_METRE;
		att = att * att / ( dm * dm + 1.0 );
		col += brdf( N, V, l, NdotL, NdotV, F0, diffuseColor, rough, fr.wet.z / ( 2.0 * max( dm, 0.1 ) ) ) * fr.dlights[i * 2 + 1].rgb * ( fr.dlightCount.y * att * NdotL );
	}

#ifndef NO_DEBUG_VIEWS	// (the GPU-driven lit pass has a variant without them for r_sceneDebug 0: they kept ~15 values live)
	const int debugView = int( fr.params.w );
	if ( debugView == 8 ) {
		// the material's index, 4 bits per channel, centred in each step (survives JPEG screenshots)
		const uint m = gInfo.z;
		outColor = vec4( ( vec3( float( m & 15u ), float( ( m >> 4u ) & 15u ), float( ( m >> 8u ) & 15u ) ) * 16.0 + 8.0 ) / 255.0, 1.0 );
		return;
	}
	if ( debugView == 1 ) {
		outColor = vec4( pow( albedo, vec3( 1.0 / 2.2 ) ), 1.0 );	// the material's own colour, no exposure or tone curve
		return;
	}
	else if ( debugView == 2 ) {
		outColor = vec4( N * 0.5 + 0.5, 1.0 );
		return;
	} else if ( debugView == 3 ) {
		outColor = vec4( float( count ) / 128.0, float( reached ) / 128.0, 0.0, 1.0 );	// r the cell's lights, g those in reach, facing
		return;
	} else if ( debugView >= 5 && debugView <= 7 ) {
		// one share of the lighting through the same tone curve: 5 ambient, 6 the source's lights, 7 the sun
		const vec3 part = debugView == 5 ? ambient : debugView == 7 ? sunPart : col - ambient - sunPart;
		outColor = vec4( toDisplay( part ), 1.0 );
		return;
	} else if ( debugView == 4 ) {
		outColor = vec4( vec3( sun ), 1.0 );
		return;
	} else if ( debugView == 9 ) {
		outColor = vec4( wet, puddle, 0.0, 1.0 );	// red wet, yellow puddle
		return;
	} else if ( debugView == 10 ) {
		outColor = vec4( toDisplay( reflection ), 1.0 );	// the reflections alone
		return;
	} else if ( debugView == 17 ) {
		outColor = vec4( roughDry, rough, porosity, 1.0 );	// r the dry roughness, g the wet one, b the porosity
		return;
	} else if ( debugView == 13 || debugView == 14 ) {
		// the world position, 8 exact bits a channel (a PNG screenshot reads it back): 13 the game units modulo 256,
		// 14 modulo 65536 in steps of 256; the two give a pixel's position to a unit within 65536 of a known point
		const vec3 q = floor( fract( vPos / ( debugView == 13 ? 256.0 : 65536.0 ) ) * 256.0 );
		outColor = vec4( min( q, vec3( 255.0 ) ) / 255.0, 1.0 );
		return;
	} else if ( debugView == 15 || debugView == 16 ) {
		// 15 the instance's index in the scene, 16 the material's, 24 exact bits (red low, blue high; a PNG reads it)
		const uint id = debugView == 15 ? vInst : gInfo.z;
		outColor = vec4( vec3( float( id & 255u ), float( ( id >> 8u ) & 255u ), float( ( id >> 16u ) & 255u ) ) / 255.0, 1.0 );
		return;
	} else if ( debugView == 18 ) {
		// the albedo image's UV: its fraction in red and green, blue 1 where it lies outside [0, 1]
		const bool outside = any( lessThan( vUV, vec2( 0.0 ) ) ) || any( greaterThan( vUV, vec2( 1.0 ) ) );
		outColor = vec4( fract( vUV ), outside ? 1.0 : 0.0, 1.0 );
		return;
	} else if ( debugView == 19 ) {
		// the albedo image's texel as sampled (sRGB, no tint, no detail or decal); black when the material binds none
		outColor = vec4( ( gInfo.y & 1u ) != 0u ? texture( ALBEDO_TEX, vUV ).rgb : vec3( 0.0 ), 1.0 );
		return;
	}
#ifdef GPU_DRIVEN
	else if ( debugView == 20 ) {
		// the albedo image's index in the GPU-driven image array, 24 exact bits (red low, blue high)
		const uint id = gTex.x;
		outColor = vec4( vec3( float( id & 255u ), float( ( id >> 8u ) & 255u ), float( ( id >> 16u ) & 255u ) ) / 255.0, 1.0 );
		return;
	}
#endif
#endif
	// a lit material's own glow (translucent holographic screens, lanterns, lightbox faces; zero for the rest): in its own
	// colours, or a masked glow (info.w 4) where the albedo's alpha says, or an ad's glow (info.w 16, material flag 0x400:
	// CP2077's billboard proxies) in its own colours where the normal image's alpha says (none: no glow)
	if ( water ) {
		// over the bed (blended as the blends are): what the water reflects, its own light and the foam, the bed through
		// the rest; fading in from the shore
		const float F = WATER_F0 + ( 1.0 - WATER_F0 ) * pow( 1.0 - NdotV, 5.0 );
		const float a = clamp( max( 1.0 - waterSeen * ( 1.0 - F ), foam ) * smoothstep( 0.0, max( gEmissive.y, 0.01 ), waterPath ), 0.0, 1.0 );
		if ( a < 1.0 / 255.0 )
			discard;	// (it writes depth: nothing, not even depth, where the shore fade leaves it transparent)
		outColor = vec4( toDisplay( throughHaze( col / max( a, 1e-3 ), haze( 0.0 ), haze( fr.fogVol.w ) ) ), a );
		return;
	}
	const float glowMask = gInfo.w != 16u ? 1.0 : ( gInfo.y & 2u ) != 0u ? texture( NORMAL_TEX, vUV ).a : 0.0;
	// a hologram's dots (info.w 64): on its alpha (blended over, src alpha: the glow is cut once), its glow boosted by
	// their mean's inverse
	// a hologram (holoMat): its mask to r_sceneHoloCurve's power, its glow times r_sceneHoloGain
	const vec2 dots = holoDots();
	const bool holo = holoMat();
	const float mask = holo ? holoMask( base.a ) : base.a;
	col += ( gInfo.w == 4u || gInfo.w == 64u ? vec3( mask * dots.y * ( holo ? fr.holo.z : 1.0 ) ) : albedo * glowMask * screenGain() ) * gEmissive.rgb * fr.params.z;
	col += decalE * fr.params.z;	// (a projected emissive decal's, on this surface)
	outColor = vec4( screenDisplay( throughHaze( col, haze( 0.0 ), haze( fr.fogVol.w ) ) ), mode == 2 ? mask * dots.x : 1.0 );
}
