#version 450
// Render scenes (vk_scene.cpp, Scene_DrawPDecals): a projected decal's box (scene_pdecal.vert) on what the eye's prepass
// holds at each pixel: the receiver's position from its depth, kept inside the box's [-1, 1]^3, facing the box's +Z
// within the record's threshold; UV from the box's x and y. Into the DBuffer as the mesh decals (scene.frag's DECAL):
// albedo (sRGB) and coverage, normal and weight, the receiver's view depth, then roughness, metalness and puddle
// wetness and their coverage (the fourth target)

layout(std140, set = 0, binding = 0) uniform Frame {
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
						// picked by a 3D room cell on a grid turned to the facade (r_sceneWindowCell)
	vec4 water;			// x: the water's clock (s), y, z: its surfaces' lowest and highest z, w: ripple strength
	vec4 water2;		// x: clarity, y: reflection strength, z: the ripples' shift of the reflection (units)
	ivec4 waterBed;		// the bed's view depth: the occlusion pyramids' level 0 (binding 23) from x, y x z texels; w 1 when built
	vec4 dlightCount;	// x: the eye view's dynamic lights (sabers, shots, effects), y: their power (r_sceneDlights)
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
	vec4 clouds2;		// x, y the wind (m/s)
	vec4 holo;			// x 1: the holograms' dots (r_sceneHoloDots), y the share of their light that blooms (r_sceneHoloBloom), z their glow's
						// gain (r_sceneHoloGain), w their mask's power (r_sceneHoloCurve)
	vec4 moon;			// r_sceneMoon: xyz toward the moon, w its angular radius (0: none)
	vec4 moonCol;		// its disc's light (linear rgb); w its glow's intensity
	vec4 moonGlow;		// x its glow's falloff (the power of the angle's cosine), y its rim's halo, z the halo's width in radii
	vec4 fx;			// r_sceneFx (scene_fx.vert): x the particles' light gain, y the haze colour's share of their ambient, z their
						// soft fade's scale (0 none), w the local lights' share on unlit ones
	vec4 fx2;			// x the particles' opacity scale (r_sceneFxAlpha); y 1 when a surface light keeps a shape (else the
						// lit loop reads none)
	vec4 wet2;			// r_sceneWetPorosity: x, y the roughness where porosity starts and ends (y <= x: off, every wet surface
						// one film), z a soaked porous surface's albedo scale, w the roughness past which nothing is mirrored
} fr;

layout(std430, set = 0, binding = 1) readonly buffer Records { vec4 recs[]; };
// the materials: 7 vec4 each, (albedo, normal, rough image or ~0, flags), (albedo, normal, roughness alpha, roughness
// bias), the UV rectangle, (tint linear rgb, emissive scale), (height, emissive, tiled colour, tiled normal image),
// (tiled rough image, tile period m, tiled colour scale rg), (its b, roughness levels low, high, unused)
layout(std430, set = 0, binding = 3) readonly buffer Materials { vec4 mats[]; };
#if defined( DEPTH_MS )
layout(set = 0, binding = 4) uniform sampler2DMS depthTex;
#else
layout(set = 0, binding = 4) uniform sampler2D depthTex;
#endif
layout(set = 1, binding = 0) uniform sampler2D albedoTex;
layout(set = 2, binding = 0) uniform sampler2D normalTex;
layout(set = 3, binding = 0) uniform sampler2D roughTex;
layout(set = 4, binding = 0) uniform sampler2D emissiveTex;
layout(set = 5, binding = 0) uniform sampler2D tiledColourTex;
layout(set = 6, binding = 0) uniform sampler2D tiledNormalTex;
layout(set = 7, binding = 0) uniform sampler2D tiledRoughTex;
layout(push_constant) uniform Push {
	mat4 viewProj;
	vec4 eye;
	uvec4 info;		// x: the material, y: the normal test (0 none, 1 past 90 - threshold degrees, 2 past threshold), z: 1 -Z,
					// w: 1 the material's index as the albedo (r_scenePDecalDebug 2)
} pc;

layout(location = 0) in vec3 vPos;
layout(location = 1) flat in uint vRec;
layout(location = 2) flat in float vFade;
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepth;
layout(location = 3) out vec4 outSurface;
layout(location = 4) out vec4 outEmission;

vec3 toLinear( const vec3 c ) { return pow( max( c, vec3( 0.0 ) ), vec3( 2.2 ) ); }

const uint NONE = 0xffffffffu;

// the view depth (scene_occ.comp's) of the prepass at a pixel; -1 off the image (no neighbour there)
float viewDepthAt( ivec2 px )
{
#if defined( DEPTH_MS )
	const ivec2 sz = textureSize( depthTex );
#else
	const ivec2 sz = textureSize( depthTex, 0 );
#endif
	if ( any( lessThan( px, ivec2( 0 ) ) ) || any( greaterThanEqual( px, sz ) ) )
		return -1.0;
	const float d = texelFetch( depthTex, px, 0 ).r;
	const float denom = -d - fr.detail.z;
	return abs( denom ) < 1e-6 ? 1e30 : -fr.detail.w / denom;
}

void main()
{
	const float d = texelFetch( depthTex, ivec2( gl_FragCoord.xy ), 0 ).r;
	// the receiver: its view depth (scene_occ.comp's), its position on this pixel's ray, in the box
	const float denom = -d - fr.detail.z;
	const float rz = abs( denom ) < 1e-6 ? 1e30 : -fr.detail.w / denom;
	const vec3 ray = vPos - fr.eye.xyz;
	// D: this pixel's ray at unit view depth, affine across the screen (its step to the next pixel is exact anywhere)
	const vec3 D = ray / max( dot( ray, fr.fogFwd.xyz ), 1e-3 );
	const vec3 P = fr.eye.xyz + D * rz;
	const vec4 r0 = recs[vRec * 5u], r1 = recs[vRec * 5u + 1u], r2 = recs[vRec * 5u + 2u];
	const vec4 r3 = recs[vRec * 5u + 3u], r4 = recs[vRec * 5u + 4u];
	const vec4 p = vec4( P, 1.0 );
	const vec3 q = vec3( dot( r0, p ), dot( r1, p ), dot( r2, p ) );
	const uint flags = floatBitsToUint( r3.w );
	const uint m = pc.info.x * 7u;
	const vec4 m0 = mats[m], m1 = mats[m + 1u], m2 = mats[m + 2u], m3 = mats[m + 3u], m4 = mats[m + 4u], m5 = mats[m + 5u], m6 = mats[m + 6u];
	vec2 s = q.xy * 0.5 + 0.5;
	if ( ( flags & 1u ) != 0u ) s.x = 1.0 - s.x;
	if ( ( flags & 2u ) != 0u ) s.y = 1.0 - s.y;
	const vec2 uv = mix( m2.xy, m2.zw, s );
	// the tiled images' UV: world-planar, (x, y) in metres over the period
	const vec2 tuv = P.xy / ( 37.5 * max( m5.y, 1e-3 ) );
	// (derivatives before any discard)
	const vec2 gx = dFdx( uv ), gy = dFdy( uv ), tgx = dFdx( tuv ), tgy = dFdy( tuv );
	const vec3 dDx = dFdx( D ), dDy = dFdy( D );
	if ( d >= 1.0 || any( greaterThan( abs( q ), vec3( 1.0 ) ) ) )
		discard;	// the sky, or outside the box
	// the receiver's normal from the prepass's depth at the four neighbours, each axis from the side whose depth is
	// nearer this pixel's (the same surface): a 2x2 quad's derivative across a depth edge (a shutter's slats, a
	// kerb) made a normal of neither surface, so the threshold's test cut the decal in dots and lines along every edge,
	// which moved with the view
	const ivec2 px = ivec2( gl_FragCoord.xy );
	const float zl = viewDepthAt( px - ivec2( 1, 0 ) ), zr = viewDepthAt( px + ivec2( 1, 0 ) );
	const float zd = viewDepthAt( px - ivec2( 0, 1 ) ), zu = viewDepthAt( px + ivec2( 0, 1 ) );
	// a neighbour is on this pixel's surface when its depth steps no more than a surface turned 85 degrees from the eye
	// would over one pixel: off the image, or past that on both sides of an axis (a ridge
	// narrower than a pixel, a dithered LOD or hide fade alternating two surfaces), the normal is not known here and the
	// threshold's test keeps the decal rather than cutting it on a guess
	const float tol = 12.0 * rz * max( length( dDx ), length( dDy ) ) + 0.5;
	const float el = zl < 0.0 ? 1e30 : abs( zl - rz ), er = zr < 0.0 ? 1e30 : abs( zr - rz );
	const float ed = zd < 0.0 ? 1e30 : abs( zd - rz ), eu = zu < 0.0 ? 1e30 : abs( zu - rz );
	const bool known = min( el, er ) <= tol && min( ed, eu ) <= tol;
	const vec3 Tx = er <= el ? ( D + dDx ) * zr - D * rz : D * rz - ( D - dDx ) * zl;
	const vec3 Ty = eu <= ed ? ( D + dDy ) * zu - D * rz : D * rz - ( D - dDy ) * zd;
	vec3 Nr = known ? cross( Tx, Ty ) : vec3( 0.0 );
	Nr = dot( Nr, Nr ) > 0.0 ? normalize( Nr ) : -normalize( ray );
	if ( dot( Nr, fr.eye.xyz - P ) < 0.0 )
		Nr = -Nr;
	const vec3 Zw = normalize( r2.xyz ) * ( ( pc.info.z & 1u ) != 0u ? -1.0 : 1.0 );
	float normalFade = 1.0;
	if ( ( flags & 4u ) != 0u && pc.info.y != 0u && known ) {
		const float c = dot( Nr, Zw ), th = radians( clamp( r4.x, 0.0, 90.0 ) );
		const float lim = pc.info.y == 1u ? sin( th ) : cos( th );
		// r_scenePDecalNormal's third value (info.z bit 1): a soft edge over about 2 degrees each side of the limit
		normalFade = ( pc.info.z & 2u ) != 0u ? smoothstep( lim - 0.035, lim + 0.035, c ) : ( c < lim ? 0.0 : 1.0 );
		if ( normalFade <= 0.0 )
			discard;
	}
	if ( pc.info.w != 0u ) {
		const uint id = pc.info.x;
		outAlbedo = vec4( vec3( float( id & 255u ), float( ( id >> 8u ) & 255u ), float( ( id >> 16u ) & 255u ) ) / 255.0, 1.0 );
		outNormal = vec4( 0.0 );
		outDepth = vec4( rz, 0.0, 0.0, 1.0 );
		outSurface = vec4( 0.0 );
		outEmission = vec4( 0.0 );
		return;
	}
	const uint ai = floatBitsToUint( m0.x ), ni = floatBitsToUint( m0.y ), ri = floatBitsToUint( m0.z ), mf = floatBitsToUint( m0.w );
	const uint ei = floatBitsToUint( m4.y ), tci = floatBitsToUint( m4.z ), tni = floatBitsToUint( m4.w ), tri = floatBitsToUint( m5.x );
	const vec4 rough = ri != NONE ? textureGrad( roughTex, uv, gx, gy ) : vec4( 1.0 );
	const vec4 tex = ai != NONE ? textureGrad( albedoTex, uv, gx, gy ) : vec4( 1.0 );
	const float edge = 1.0 - smoothstep( 0.9, 1.0, abs( q.z ) );	// (its depth's ends)
	const float cover = ( ( mf & 8u ) != 0u ? rough.a : tex.a ) * r3.y * vFade * edge * normalFade;
	// albedo: the image in sRGB times the tint (linear), its coverage times the material's, the node's and its colour
	// scale's alphas
	// (a terrain-projected one: times the tiled colour and its scale, the image the mask)
	vec3 lin = toLinear( tex.rgb ) * max( m3.rgb, vec3( 0.0 ) );
	if ( tci != NONE )
		lin *= toLinear( textureGrad( tiledColourTex, tuv, tgx, tgy ).rgb ) * max( vec3( m5.zw, m6.x ), vec3( 0.0 ) );
	outAlbedo = ai != NONE ? vec4( pow( clamp( lin, 0.0, 1.0 ), vec3( 1.0 / 2.2 ) ), clamp( cover * m1.x * r4.w, 0.0, 1.0 ) ) : vec4( 0.0 );
	// the normal: the image's RG in the box's frame (x along U, y along V, z the box's +Z), or reoriented onto the
	// receiver's (flag 8)
	vec3 N = Nr;
	float wN = 0.0;
	if ( ni != NONE ) {
		vec2 nxy = textureGrad( normalTex, uv, gx, gy ).rg * 2.0 - 1.0;
		const float nz = sqrt( max( 1.0 - dot( nxy, nxy ), 0.0 ) );
		vec3 Tw = normalize( r0.xyz ) * ( ( flags & 1u ) != 0u ? -1.0 : 1.0 );
		vec3 Bw = normalize( r1.xyz ) * ( ( flags & 2u ) != 0u ? -1.0 : 1.0 );
		vec3 Z = Zw;
		if ( ( flags & 8u ) != 0u ) {
			Tw = normalize( Tw - Nr * dot( Tw, Nr ) );
			Bw = cross( Nr, Tw ) * ( dot( cross( Nr, Tw ), Bw ) < 0.0 ? -1.0 : 1.0 );
			Z = Nr;
		}
		N = normalize( Tw * nxy.x + Bw * nxy.y + Z * nz );
		wN = clamp( cover * m1.y, 0.0, 1.0 );
	} else if ( tni != NONE ) {	// the tiled normal: U along +x, V along +y, on the box's +Z (or the receiver's, flag 8)
		const vec2 nxy = textureGrad( tiledNormalTex, tuv, tgx, tgy ).rg * 2.0 - 1.0;
		const float nz = sqrt( max( 1.0 - dot( nxy, nxy ), 0.0 ) );
		const vec3 Z = ( flags & 8u ) != 0u ? Nr : Zw;
		const vec3 Tx = vec3( 1.0, 0.0, 0.0 ) - Z * Z.x;	// (+x; +y for a box or receiver facing along x)
		const vec3 Tw = dot( Tx, Tx ) > 1e-8 ? normalize( Tx ) : normalize( vec3( 0.0, 1.0, 0.0 ) - Z * Z.y );
		const vec3 Bw = cross( Z, Tw );
		N = normalize( Tw * nxy.x + Bw * nxy.y + Z * nz );
		wN = clamp( cover * max( m1.y, 1.0 ), 0.0, 1.0 );
	}
	outNormal = vec4( N * 0.5 + 0.5, wN );
	outDepth = vec4( rz, 0.0, 0.0, 1.0 );
	// roughness and metalness (its image's R and G, the node's scale, the material's bias), puddle wetness
	const bool puddle = ( mf & 2u ) != 0u;
	float wR = ri != NONE || tri != NONE ? cover * m1.z : 0.0;
	if ( puddle )
		wR = max( wR, cover );
	float rv = ri != NONE ? clamp( rough.r * r4.z + m1.w, 0.02, 1.0 ) : 0.05;
	if ( ri == NONE && tri != NONE )	// the tiled roughness through its levels (high may pass 1: clamped)
		rv = clamp( mix( m6.y, m6.z, textureGrad( tiledRoughTex, tuv, tgx, tgy ).r ) * r4.z + m1.w, 0.02, 1.0 );
	outSurface = vec4( rv, ri != NONE ? rough.g : 0.0, puddle ? 1.0 : 0.0, clamp( wR, 0.0, 1.0 ) );
	// the emission: its image's RGB x the emissive scale x the node's and colour scale's alphas, by its A
	outEmission = vec4( 0.0 );
	if ( ei != NONE ) {
		const vec4 e = textureGrad( emissiveTex, uv, gx, gy );
		outEmission = vec4( toLinear( e.rgb ) * max( m3.w, 0.0 ), clamp( e.a * r3.y * r4.w * vFade * edge, 0.0, 1.0 ) );
	}
}
