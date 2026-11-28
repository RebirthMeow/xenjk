#ifndef SHADER_SHARED_H
#define SHADER_SHARED_H

#ifndef GLOBAL_SHADER_C
	#define GLSL
    #define USE_VK_PBR
    //#define USE_VK_IMGUI
#endif

#ifdef GLSL
    #define VK_DLIGHT_GPU

    #define M_PI 3.1415926535897932384626433832795
    #define M_HALF_PI 1.57079632679

    #if defined(USE_LIGHTMAP) || defined(USE_LIGHT_VECTOR) || defined(USE_LIGHT_VERTEX)
	    #define USE_LIGHT
    #endif

    #if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	    #define PER_PIXEL_LIGHTING 
    #endif
#endif

#if defined(USE_VBO_GHOUL2) || defined(USE_VBO_MDV)
	#define USE_VBO_MODEL
#endif

// descriptor idx
#define VK_DESC_STORAGE					0
#define VK_DESC_UNIFORM					0
#define VK_DESC_TEXTURE0				1
#define VK_DESC_TEXTURE1				2
#define VK_DESC_TEXTURE2				3
#define VK_DESC_FOG_COLLAPSE			4
#ifdef USE_VK_PBR
#define VK_DESC_PBR_BRDFLUT				5
#define VK_DESC_PBR_NORMAL				6
#define VK_DESC_PBR_PHYSICAL			7
#define VK_DESC_PBR_CUBEMAP				8
#define VK_DESC_PBR_DELUXE				9
#define VK_DESC_SUN_SHADOW				10	// sun shadow cascades (sampler2DArrayShadow)
#define VK_DESC_PBR_DETAIL				11	// detailNormalMap (tiling detail normal)
#define VK_DESC_PBR_MASK				12	// materialMask
#define VK_DESC_PBR_DETAILMAP			13	// detailMap (tiling detail albedo/roughness)
#define VK_DESC_PBR_EMISSIVE			14	// emissiveMap
#define VK_DESC_PBR_TINT				15	// tintMask
#define VK_DESC_PBR_LAYERMASK			16	// layerMask
#define VK_DESC_PBR_LAYERALBEDO			17	// layer albedo/roughness array
#define VK_DESC_PBR_LAYERNORMAL			18	// layer normal/metalness array
#define VK_DESC_SCREEN_AO				19	// r_charAO: the opaque depth prepass's ambient occlusion (white when off)
#define VK_DESC_SCREEN_DEPTH			20	// r_charContactShadows: the prepass's linear depth (white when off)
#define VK_DESC_HAIR_MAP				21	// r_charSelfShadowHairMap: the first self-shadow tile's hair map (black when off)
#define VK_DESC_COUNT					22
// the sets actually bound. 0 uniform, 1..3 textures, 4 collapsed fog, then ONE push-descriptor set
// holding every material and screen map (the VK_DESC_PBR_* / SUN / SCREEN / HAIR slots above stay as the C side's
// slot numbers and map to its bindings, VK_MAT_*), and the reflection probes' set: 7 sets instead of 22, within
// the 8 an older integrated GPU binds
#define VK_DESC_MATERIAL				5
#define VK_DESC_PROBES					6
#define VK_DESC_PHYSICAL_COUNT			7
#define VK_MAT_BRDFLUT					0
#define VK_MAT_NORMAL					1
#define VK_MAT_PHYSICAL					2
#define VK_MAT_DELUXE					3
#define VK_MAT_SUN_SHADOW				4
#define VK_MAT_DETAIL					5
#define VK_MAT_MASK						6
#define VK_MAT_DETAILMAP				7
#define VK_MAT_EMISSIVE					8
#define VK_MAT_TINT						9
#define VK_MAT_LAYERMASK				10
#define VK_MAT_LAYERALBEDO				11
#define VK_MAT_LAYERNORMAL				12
#define VK_MAT_SCREEN_AO				13
#define VK_MAT_SCREEN_DEPTH				14
#define VK_MAT_HAIR_MAP					15
#define VK_MAT_COUNT					16
#else
#define VK_DESC_COUNT					5
#define VK_DESC_PHYSICAL_COUNT			5
#endif

#define VK_DESC_TEXTURE_BASE			VK_DESC_TEXTURE0
#define VK_DESC_FOG_ONLY				VK_DESC_TEXTURE1
#define VK_DESC_FOG_DLIGHT				VK_DESC_TEXTURE1

// uniform binding idx
#define VK_DESC_UNIFORM_MAIN_BINDING		0
#define VK_DESC_UNIFORM_CAMERA_BINDING		1
#define VK_DESC_UNIFORM_LIGHT_BINDING		2
#define VK_DESC_UNIFORM_ENTITY_BINDING		3
#define VK_DESC_UNIFORM_BONES_BINDING		4
#define VK_DESC_UNIFORM_FOGS_BINDING		5
#define VK_DESC_UNIFORM_GLOBAL_BINDING		6
#define VK_DESC_UNIFORM_COUNT				7

// reflection probes blended per pixel: a draw's probe and its nearest neighbours (gen_frag env_texture[])
#define VK_CUBEMAP_BLEND_PROBES				8

// reflection probes: mip j of the prefiltered cubemap holds GGX at perceptual roughness j / VK_CUBEMAP_ROUGH_MIPS
// (vk_cubemap.cpp), which gen_frag samples at lod = roughness x VK_CUBEMAP_ROUGH_MIPS; the mips past it (2 x 2 and
// 1 x 1 on a 256 cubemap) repeat roughness 1 and are not sampled (rend2's CUBE_MAP_ROUGHNESS_MIPS, same split)
#define VK_CUBEMAP_ROUGH_MIPS				6

#ifdef GLSL
    #ifdef USE_TX2
	    #define USE_TX1
    #endif

    #ifdef USE_CL2
	    #define USE_CL1
    #endif

    #define STRUCT(content, name) struct name { content };
    #define INT(n)		int n;
    #define UINT(n)		uint n;
    #define FLOAT(n)	float n;
    #define VEC2(n)		vec2 n;
    #define VEC3(n)		vec3 n;
    #define VEC4(n)		vec4 n;
    #define MAT4(n)		mat4 n;
    #define MAT4X3(n)	mat4x3 n;
    #define MAT3X4(n)	mat3x4 n;
    #define UVEC2(n)	uvec2 n;
    #define UVEC3(n)	uvec3 n;
    #define UVEC4(n)	uvec4 n;
	#define IVEC3(n)	ivec3 n;
	#define IVEC4(n)	ivec4 n;
    #define PAD1(n)
    #define PAD2(n)
    #define PAD3(n)
#else
    #define STRUCT(content, name) typedef struct { content } name;
    #define INT(n)		int32_t n;
    #define UINT(n)		unsigned int n;
    #define FLOAT(n)	float n;
    #define VEC2(n)		vec2_t n;
    #define VEC3(n)		vec3_t n;
    #define VEC4(n)		vec4_t n;
    #define MAT4(n)		mat4_t n;
    #define MAT3X4(n)	mat3x4_t n;
    #define UVEC2(n)	unsigned int n[2];
    #define UVEC3(n)	unsigned int n[3];
    #define UVEC4(n)	unsigned int n[4];
	#define IVEC3(n)	ivec3_t n;
	#define IVEC4(n)	ivec4_t n;
    #define PAD1(n)     int n;
    #define PAD2(n)     vec2_t n;
    #define PAD3(n)     vec3_t n;
#endif

// entity
STRUCT (  
    VEC4	( ambientLight )
    VEC4	( directedLight )
    VEC4	( localLightOrigin )
    VEC4	( localViewOrigin )
    MAT4	( modelMatrix )
    // r_charFill / r_charRim (tr_backend.cpp vk_update_entity_light_constants): Ghoul2 models lit by the light grid get
    // two extra directional lights, coloured as a share of the grid's whole light (ambient + directed)
    VEC4	( charFill )    // xyz = toward the fill (world), w = 1 + level; 0 = not a character (no fill, rim or ambient floor)
    VEC4	( charRim )     // xyz = toward the rim (world), w = level
, vkUniformEntity_t )

// non-vbo entity fallback
STRUCT(
    MAT4(modelMatrix)
, vkEntityMatrix_t)

// bones
STRUCT(
    MAT3X4( boneMatrices[256] )   // the skeleton bones, then hair chain bones (vk_hair.cpp); 256 = byte bone refs
, vkUniformBones_t)

// camera
STRUCT(
    VEC4( viewOrigin )
    VEC4( sunDir )      // rend2 sun: direction, w = r_sunlightMode when on (0 = off)
    VEC4( sunCol )
    VEC4( sunAmbCol )   // w = colorScale (q3gl2_sun mapLightScale) for world scenes, else 1
    MAT4( sunShadowMvp0 )   // world to cascade clip space, as each cascade is drawn
    MAT4( sunShadowMvp1 )
    MAT4( sunShadowMvp2 )
    VEC4( sunShadowParams ) // x = cascades on for this scene, y = map size, z = r_shadowCascadeZFar, w = r_shadowFilter
    // character shadows (tr_charshadow.cpp R_SetupCharShadows): shadow-only lights from the light grid, one atlas tile each
    MAT4( charShadowMvp[8] )    // world to the tile's clip space, as the tile is drawn
    VEC4( charShadowTile[8] )   // xy = tile origin in the layer (0..1), z = tile size (0..1), w = strength
    VEC4( charShadowLight[8] )  // xyz = toward the light, w = ambient / directed light at the caster
    VEC4( charShadowScale[8] )  // x = layer uv per world unit across the tile, y = world units per clip z (depth)
    VEC4( charShadowRect[8] )   // the caster's box plus its widest penumbra, in the tile's clip x/y: min xy, max xy
    VEC4( charShadowParams )    // x = count, y = filter radius (layer uv), z = depth bias (clip z), w = r_charShadowDebug
    VEC4( charShadowInfo )      // x = 1 / the final pass's overbright scale (debug views come out linear on screen),
                                // y = tan( r_charShadowLightSize / 2 ): penumbra radius per unit of blocker distance,
                                // z = one texel of the layer (uv)
    // character occlusion (vk_charocc.cpp RB_CharOcclusion): AO and contact shadows from the opaque depth prepass
    MAT4( viewMatrix )          // world to eye (GL eye space, looking down -z)
    VEC4( charOccParams )       // x = r_charAO (0 off), y = contact ray length (u, 0 off), z = thickness (u), w = steps
    VEC4( charOccProj )         // projection[0], [5], [8], [9]: eye to the prepass depth's uv
    VEC4( charOccInfo )         // x = r_charOcclusionDebug, y, z = projection[12], [13], w = r_charContactSoftness (radians)
    FLOAT( charOccFine )        // r_charContactFine: the lit shader's own ray length for lights with a screen mask (u)
    FLOAT( charOccHairOpacity ) // r_charHairOpacity (0 when hair is not in the depth's g channel)
    FLOAT( charOccGridShare )   // r_charContactGridShare
    FLOAT( charReviewFix )      // shading switches, bits (1 to 32 unused: those fixes are always on now):
                                // 64 the hair map holds depth as -1..1 of the tile's depth range (its RGBA16F
                                // fallback: world-unit moments overflow half floats), 128 FMA stages take the
                                // grid light's shadows, 256 FMA faces take them too
    // character self-shadows (tr_charshadow.cpp R_SetupCharSelfShadows): each nearby character's own grid light, one
    // atlas tile each (layer 3, the half below the character shadow tiles); u_entity.localViewOrigin.w = 1 + tile
    MAT4( selfShadowMvp[5] )    // world to the tile's clip space, as the tile is drawn
    VEC4( selfShadowTile[5] )   // xy = tile origin in the layer (0..1), z = tile size (0..1), w = strength
    VEC4( selfShadowScale[5] )  // x = layer uv per world unit, y = world units per clip z, z = depth bias (clip z),
                                // w = normal offset (u)
    VEC4( selfShadowParams )    // x = rays + 32 x steps, y = tile edge kept clear (layer uv), z = tan( light size / 2 ), w = debug
    // named fields, at the offsets of the four test vec4s they replace (same std140 layout)
    FLOAT( selfShadowHairGain ) // r_charSelfShadowHairGain: alpha scale when hair casts
    FLOAT( selfShadowHairLod0 ) // r_charSelfShadowHairLod0: 1 = the cast alpha from the full-size texture
    FLOAT( skinSoftNormals )    // r_charSkinSoftNormals: skin diffuse per channel toward the vertex normal
    FLOAT( skinSoftAlbedo )     // r_charSkinSoftAlbedo: skin albedo mip bias (red; green half)
    VEC3( selfShadowDebugBox )  // r_charSelfShadowDebugBox (r_charSelfShadowDebug 6): tile uv centre, side
    FLOAT( selfShadowSlopeBias )    // r_charSelfShadowSlopeBias x one layer texel (layer uv)
    FLOAT( selfShadowInMask )   // 1: the first tile's self-shadow is in the grid light's screen mask (r_charSelfShadowScreen);
                                // opaque stages of that character skip their own lookup
    FLOAT( hairMapDepthView )   // r_charSelfShadowHairMap: 1 in the first tile's depth view (its hair goes to the map only)
    FLOAT( hairMapTexel )       // r_charSelfShadowHairMap: 1 / the map's size when this view reads it (0 = no map)
    FLOAT( hairMapDensity )     // r_charSelfShadowHairDensity
    FLOAT( hairStrandDepth )    // r_charSelfShadowHairStrandDepth: u below a strand's surface at which it reads the hair
                                // map (negative = in front of it)
    FLOAT( a2cDiscard )         // r_a2cDiscard: alpha-to-coverage fragments with zero coverage discard before lighting
    FLOAT( charFastBits )       // r_charFast bits
    FLOAT( charAmbientFloor )   // r_charAmbientFloor: characters' ambient at least this share of the grid's light
    VEC4( charTone )            // r_charLinear 3 (R_SceneCharTone): x the render scene's tone curve + 1 (0 off), y, z
                                // r_sceneToneAdjust's gain and gamma (0 none)
    VEC4( charSky )             // r_charLinear 3: the scene's sky and ground ambient after exposure (linear), what its
    VEC4( charGround )          // own surfaces mirror (scene.frag hemiR); the characters' reflections in place of the probes
, vkUniformCamera_t)

// fog
#define FOG_ENTRY_T(n) vkUniformFogEntry_t n;

STRUCT (  
    VEC4	( plane )
    VEC4	( color )
    FLOAT	( depthToOpaque )
    INT 	( hasPlane )
    PAD2	( pad0 )
, vkUniformFogEntry_t )

STRUCT (  
	INT		        ( num_fogs )
    PAD3	        ( pad0 )
    FOG_ENTRY_T     ( fogs[16] )
, vkUniformFog_t )

#undef FOG_ENTRY_T

// surface sprites
STRUCT (  
    VEC2	( fxGrow )
    FLOAT	( fxDuration )
    FLOAT	( fadeStartDistance )
    FLOAT	( fadeEndDistance )
    FLOAT	( fadeScale )
    FLOAT	( wind )
    FLOAT	( windIdle )
    FLOAT	( fxAlphaStart )
    FLOAT	( fxAlphaEnd )
    PAD2	( pad0 )
, SurfaceSpriteBlock )

// global
STRUCT (  
    VEC4	( matrix )
    VEC4	( offTurb )
, vktcMod_t )

STRUCT (  
    VEC3	( vector0 )
    PAD1    ( pad0 )
    VEC3	( vector1 )
    INT	    ( type )
, vktcGen_t )

#if defined(PER_PIXEL_LIGHTING) || defined(USE_LIGHT_VECTOR) || defined(USE_VBO_MODEL) || defined(IS_REFRACTION_GLSL)
    #define TCMOD_T(n)          vktcMod_t n;
    #define TCGEN_T(n)          vktcGen_t n;
    #define BUMDLE_T(n)         vkBundle_t n;
    #define DISINTEGRATION_T(n) vkDisintegration_t n;
    #define DEFORM_T(n)         vkDeform_t n;

    STRUCT (  
        VEC4	( baseColor )
        VEC4	( vertColor )
        TCMOD_T	( tcMod )
        TCGEN_T	( tcGen )
        INT	    ( rgbGen )
        INT	    ( alphaGen )
        INT	    ( numTexMods )
        PAD1    ( pad0 )
    , vkBundle_t )

    STRUCT (  
        VEC3	( origin )
        FLOAT	( threshold )
    , vkDisintegration_t )

    STRUCT (  
	    FLOAT	( base )
	    FLOAT	( amplitude )
	    FLOAT	( phase )
	    FLOAT	( frequency )

	    VEC3	( vector )
	    FLOAT	( time )

	    INT		( type )
	    INT		( func )
        PAD2    ( pad0 )
    , vkDeform_t )

    STRUCT (  
        BUMDLE_T            ( bundle[3] )
        DISINTEGRATION_T    ( disintegration )
        DEFORM_T            ( deform )
        FLOAT               ( portalRange )
        FLOAT               ( parallaxBias )
        PAD2                ( pad0 )
		VEC4				( specularScale )
		VEC4				( normalScale )
		VEC4				( detailParams )	// detailNormalMap: tile, strength
		VEC4				( subsurfaceParams )	// subsurface: colour, strength
		VEC4				( hairParams )		// hairAnisotropy: axis, shift, exponents
		VEC4				( sheenParams )		// sheen: colour, roughness
		VEC4				( eyeParams )		// eye: iris radius, depth, centre u/v
		VEC4				( eyeCorneaParams )	// eye: ior, cornea roughness, cornea strength, limbus width
		VEC4				( detailMapParams )	// detailMap: tile, strength
		VEC4				( emissiveParams )	// emissiveMap: colour x intensity, w 1 in the glow pass (emissive only)
		VEC4				( tintParams[3] )	// tintMask: the colours of its r, g, b
		VEC4				( layerParams[4] )	// layer: tile, normal strength, roughness scale, metalness scale
		VEC4				( layerTint[4] )	// layer: colour, opacity
		VEC4				( cubeProbes[VK_CUBEMAP_BLEND_PROBES] )	// env_texture[i]'s probe: origin, parallax radius (0 = unused)
		VEC4				( cubeBoxMins[VK_CUBEMAP_BLEND_PROBES] )	// env_texture[i]'s fitted parallax box (r_cubeMapParallax 1)
		VEC4				( cubeBoxMaxs[VK_CUBEMAP_BLEND_PROBES] )
		VEC4				( cubeParams )		// x: r_cubeMapParallax (1 box, 2 sphere), y: r_cubeMapDebug, z: r_cubeMapRoughCut
		VEC4				( lightParams )		// x: r_cubeMapDiffuse, y: r_specularOcclusion, z: r_specularAA
		VEC4				( toonParams )		// toon: threshold, softness, highlight size, highlight strength
		VEC4				( toonShade )		// toon: shade colour, highlight softness
		VEC4				( toonSkinShade )	// toon: skin shade colour, rim softness
		VEC4				( toonRim )			// toon: rim colour, width
		VEC4				( toonLight )		// toon: the main light's colour share, compression knee and range
		VEC4				( fmaBody )			// split tendency, split saturation, rear light
		VEC4				( fmaDodge )			// rim dodge RGB, burn range
		VEC4				( fmaBurn )			// rim burn RGB, burn boost
		VEC4				( fmaSpec )			// specular RGB
		VEC4				( fmaForward )		// entity forward (world); w 1 with a baked vertex map
		VEC4				( fmaRight )			// entity right (world); w 1 with a detail map
    , vkUniformGlobal_t )

    #undef TCMOD_T
    #undef TCGEN_T
    #undef BUMDLE_T
    #undef DISINTEGRATION_T
    #undef DEFORM_T
#endif

// light
#ifdef VK_DLIGHT_GPU
    #define LIGHT_ENTRY_T(n) vkUniformLightEntry_t n;

    STRUCT (  
	    VEC4				( origin )
	    VEC3				( color )
	    FLOAT				( radius )
    , vkUniformLightEntry_t )

    STRUCT (  
	    UINT				( num_lights )
        PAD3                ( pad0 )
	    LIGHT_ENTRY_T		( light[64] )
    , vkUniformLight_t )

    #undef LIGHT_ENTRY_T
#else
    STRUCT (  
	    VEC4				( item )
    , vkUniformLight_t )
#endif


// depth extract
// ~sunny, implement z-prepass later. see issue: #68
STRUCT (
	VEC4	( projection )
    VEC2    ( inv_size )
    VEC2    ( pad0 )
, vkExtractDepth_t )

// SSAO
STRUCT (
	VEC4	( projection )

    VEC2    ( texture_scale )   // aka pad0
    FLOAT   ( frame_noise )
	FLOAT	( radius )

	FLOAT	( bias )
	FLOAT	( intensity )
	FLOAT	( power )
	FLOAT	( samples )

	FLOAT	( reverse_depth )
	FLOAT	( inv_width )
	FLOAT	( inv_height )
	FLOAT	( depth_falloff )

	UINT	( mode )
	UINT	( slices )		// GTAO slices in the low 4 bits; r_charAO's pass: bits 4..5 = the grid light's mask channel
							// (1..3) that takes r_charSelfShadowScreen, 0 = none; bit 6 r_charOccMask; bits 8..15 the
							// GTAO screen radius cap in target pixels (r_charAORadiusCap, 0 = 128)
	UINT	( steps )
	FLOAT	( character )	// r_ssao_character: share of the AO kept on character pixels; >= 2: r_charAO's pass with
							// character - 2 contact-shadow lights (bias = ray length, samples = thickness,
							// reverse_depth = rays per pixel, depth_falloff = steps; each light's size is its w)
	VEC4	( lights[3] )	// that pass's lights in this shader's eye space: xyz position (w 10 + size in radians) or
							// direction to it (w = size)
, vkExtractSSAO_t )

STRUCT (  
    VEC4	( projection )

    VEC2    ( texture_scale )   // aka pad0
    FLOAT	( depth_radius )
    FLOAT	( depth_sharpness )

    FLOAT	( inv_width )
    FLOAT	( inv_height )
    VEC2	( direction )		// (1,0) horizontal, (0,1) vertical pass
    VEC4	( mask_params )		// x = 1: r_charOccMask, only marked (negative-depth) pixels blur, and only with each other
, vkBlurSSAO_t )

// lightgrid
#define LIGHTGRID_DEBUG_MODE_DISABLED   0
#define LIGHTGRID_DEBUG_MODE_AMBIENT    1
#define LIGHTGRID_DEBUG_MODE_DIRECTED   2
#define LIGHTGRID_DEBUG_MODE_DIRECTION  3
#define LIGHTGRID_DEBUG_MODE_COUNT      4

STRUCT (  
    VEC4	( ambientLight )
    VEC4	( directedLight )
    VEC4	( lightDir )
, vkLightGridSample_t )
#endif
