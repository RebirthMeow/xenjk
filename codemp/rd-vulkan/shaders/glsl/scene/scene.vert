#version 450
// Render scenes (vk_scene.cpp): instanced mesh parts. gl_InstanceIndex (the command's firstInstance + i) indexes the
// view's visible list, written by scene_cull.comp. GPU_DRIVEN (r_sceneGpuDriven): the draw is one of a list's
// (scene_compact.comp), whose material sits beside it at the list's base (info.x) + gl_DrawID. The lit pass tests
// depth EQUAL against the prepass, which runs this same module: no `invariant` on gl_Position (it changed the
// arithmetic, and with it which of two exactly coplanar surfaces wins, against the per-material path's). A
// position-only variant for the sun cascades measured no faster (the driver drops the unused outputs).
#ifdef GPU_DRIVEN
#extension GL_ARB_shader_draw_parameters : require
#endif
// VRS (the GPU-driven module where the device has VK_KHR_fragment_shading_rate's primitive rates; the prepass and the lit
// pass both run it, so their positions stay the same arithmetic): each triangle's shading rate, which the lit pipelines take
// in place of their 1 x 1 (r_sceneVrs). Far surfaces shade once per 2 x 2 pixels: coverage and depth stay per pixel (edges
// and the EQUAL test exact), the colour within the block is one fragment's, its texture mips chosen for the block
#ifdef VRS
#extension GL_EXT_fragment_shading_rate : require
#endif

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inNrm;
layout(location = 2) in vec4 inTan;
layout(location = 3) in vec2 inUV;

layout(push_constant) uniform Push {
	mat4 viewProj;
	vec4 emissive;
	vec4 tint;
	uvec4 info;
	vec4 eye;		// this view's origin; w 1 the eye's views, 2 the same with an eye-relative viewProj (r_sceneEyeRelative)
} pc;

struct Instance { vec4 r0, r1, r2; uvec4 info; };	// info: mesh, near distance (version 5), signal slot, hide distance
struct Mesh { vec4 sphere; uvec4 lods; };
layout(std430, set = 0, binding = 0) readonly buffer Visible { uint visible[]; };
layout(std430, set = 0, binding = 1) readonly buffer Instances { Instance inst[]; };
layout(std140, set = 0, binding = 5) uniform Frame { vec4 eye; vec4 gridOrigin; ivec4 gridDims; vec4 skyAmbient; } fr;
	// scene.frag's frame block, its first members: the eye, w r_sceneHideScale while the hide and near distances fade (0:
	// they pop); gridDims.w the fade's band (units, r_sceneHideBand); skyAmbient.w r_sceneProxyPush
layout(std430, set = 0, binding = 19) readonly buffer Meshes { Mesh meshes[]; };

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec4 vTan;
layout(location = 3) out vec2 vUV;
layout(location = 4) out vec3 vLocal;		// the mesh's own space (windows lay their rooms out in it)
layout(location = 5) out vec3 vLocalNrm;
layout(location = 6) flat out uint vInst;	// the instance
layout(location = 7) flat out vec3 vUpW;	// the mesh's up axis in the world
layout(location = 10) flat out float vFade;	// the share drawn, dithered by scene.frag: 1 all; [0, 1) fading out; [-1, 0) fading in
layout(location = 9) flat out uint vSignal;	// its traffic signal slot (instance info.z; scene.frag lights its lamps), bit 25 no decals
layout(location = 11) out vec3 vCanopy;		// a SpeedTree leaf (info.w 32): from its mesh's bounding sphere's centre, in radii
#ifdef GPU_DRIVEN
layout(std430, set = 0, binding = 14) readonly buffer DrawMaterial { uint drawMaterial[]; };
struct Material { vec4 emissive; vec4 tint; uvec4 info; uvec4 tex; };	// scene.frag's
layout(std430, set = 0, binding = 15) readonly buffer Materials { Material materials[]; };
layout(location = 8) flat out uint vMat;
// Witcher 3 layer blends (vk_scene.cpp, maps/<map>.xw3b): a material whose detail record is -2 reads the stream, 3
// words a vertex in the scene's vertex order (its second UV set, its colour as the game stores it)
layout(std430, set = 0, binding = 13) readonly buffer MaterialDetail { vec4 materialDetail[]; };
layout(std430, set = 0, binding = 27) readonly buffer W3Stream { uint w3Stream[]; };
layout(location = 12) out vec2 vUV1;
layout(location = 13) out vec4 vCol;
layout(location = 14) flat out uint vCoarse;	// 1: the triangle shades coarse (VRS), so scene.frag's per-pixel coverage does not apply
#endif

// an emissive layer lies exactly on another material (a poster's glowing print, a sign's face): two sets of triangles
// in one plane, so which one the prepass keeps changed pixel by pixel as the view moved and the lit pass's EQUAL test
// showed a flickering mix. Its depth is drawn this far nearer along the eye's ray, in every pass
// (this module feeds the prepass and the lit pass alike), so the emissive one wins; the pixel's position is unchanged.
const float EMISSIVE_PULL = 0.1, EMISSIVE_PULL_PER_UNIT = 0.001;	// game units, and per unit of distance
// a mesh fades out over the band before its hide distance and a far proxy in over the band before its near distance
// (scene_cull.comp draws it from there): r_sceneHideBand (40 m), or half the distance when that is nearer, times r_sceneHideScale. On
// Watson's scene-4 a building shell's hide most often equals its proxy's near (median 82 m past it): at equal distances
// the two dithers are complementary, past them both are drawn, so only a hide short of the proxy's near leaves a hole
// (as it does popped)
// The band's width is r_sceneHideBand (default 40 m), in units in fr.gridDims.w; at a long band an instance stood half
// faded wherever the camera stopped in it (a parapet whose proxy shell lacked it showed as a ghost, Beauclair 2026-11-25)
float FadeBand( float x ) { return min( float( fr.gridDims.w ), 0.5 * x ) * fr.eye.w; }

// bit 27 of info.z (w3scene.py): a proxy's member. Bits 0-23 hold the proxy's centre as a signed byte an axis (x, y, z),
// in 16-unit steps from this instance's centre: its hide distance is measured from there, as its proxy's near distance is
// from its own centre, so the two fade against one point. Measured each from its own centre (up to 41 m apart in
// Beauclair) the dithers did not complement: both half drawn from one side, both or neither from the other
vec3 HideCentre( const uint z, const vec3 centre )
{
	if ( ( z & 0x8000000u ) == 0u )
		return centre;
	return centre + 16.0 * vec3( float( int( z << 24u ) >> 24 ), float( int( z << 16u ) >> 24 ), float( int( z << 8u ) >> 24 ) );
}

void main()
{
	const uint entry = visible[gl_InstanceIndex];
	const uint id = entry & 0xffffffu;	// (the top byte: r_sceneLodFade's tag, scene_cull.comp)
	const Instance I = inst[id];
	const vec4 p = vec4( inPos, 1.0 );
	vPos = vec3( dot( I.r0, p ), dot( I.r1, p ), dot( I.r2, p ) );
	const mat3 rows = mat3( I.r0.xyz, I.r1.xyz, I.r2.xyz );	// its columns hold M's rows: v * rows = M v
	// normals: a rotation with a uniform scale turns them as it turns points (kept as M v: the same arithmetic as
	// always); a non-uniform scale or a mirror needs inverse-transpose(M), here M's cofactors (columns b x c, c x a,
	// a x b) with det's sign (scene.frag normalizes), and a mirror flips the tangent's handedness
	const vec3 a = vec3( I.r0.x, I.r1.x, I.r2.x ), b = vec3( I.r0.y, I.r1.y, I.r2.y ), c = vec3( I.r0.z, I.r1.z, I.r2.z );
	const float det = dot( a, cross( b, c ) ), aa = dot( a, a ), tol = 1e-3 * aa;
	const bool similar = det > 0.0 && abs( dot( b, b ) - aa ) <= tol && abs( dot( c, c ) - aa ) <= tol
		&& abs( dot( a, b ) ) <= tol && abs( dot( b, c ) ) <= tol && abs( dot( a, c ) ) <= tol;
	vNrm = similar ? inNrm.xyz * rows
		: sign( det ) * ( cross( b, c ) * inNrm.x + cross( c, a ) * inNrm.y + cross( a, b ) * inNrm.z );
	vTan = vec4( inTan.xyz * rows, det < 0.0 ? -inTan.w : inTan.w );
	vUV = inUV;
	vLocal = inPos;
	vLocalNrm = inNrm.xyz;
	vInst = id;
	vUpW = normalize( vec3( I.r0.z, I.r1.z, I.r2.z ) );
	// (bit 24: an interior's group, no signal; bit 25: it receives no projected or mesh decals, passed on; bit 26: it lies
	// on another surface, below; bits 28-31: the sky its CP2077 reflection probe's capture lacks, passed on:
	// tag_probesky.py)
	vSignal = ( ( I.info.z & 0x9000000u ) != 0u ? 0u : I.info.z & 0xffffffu ) | ( I.info.z & 0xf2000000u );	// (bit 27: a proxy's member)
	vFade = 1.0;
	if ( pc.eye.w > 0.5 && fr.eye.w > 0.0 && ( I.info.y | I.info.w ) != 0u ) {	// the eye's views (the cascades pop)
		const vec4 c = vec4( meshes[I.info.x].sphere.xyz, 1.0 );
		const float d = distance( HideCentre( I.info.z, vec3( dot( I.r0, c ), dot( I.r1, c ), dot( I.r2, c ) ) ), fr.eye.xyz );	// scene_cull.comp's
		const float nd = uintBitsToFloat( I.info.y ), hd = uintBitsToFloat( I.info.w );
		const float in_ = I.info.y != 0u ? clamp( ( d - nd * fr.eye.w ) / FadeBand( nd ) + 1.0, 0.0, 1.0 ) : 1.0;
		const float out_ = I.info.w != 0u ? clamp( ( hd * fr.eye.w - d ) / FadeBand( hd ), 0.0, 1.0 ) : 1.0;
		// a proxy whose hide band starts before its near band ends (234 on Watson's scene-4) draws the share both
		// leave, in the fading-in pattern, so its coverage does not jump where the near band ends
		if ( in_ < 1.0 )
			vFade = max( in_ + out_ - 1.0, 0.0 ) - 1.0;
		else
			vFade = out_;
	}
	// r_sceneLodFade: a LOD within its crossfade band (scene_cull.comp's tag: bit 31 the incoming LOD, bits 24-30 the
	// incoming one's share): the outgoing one keeps the dither's low values below 1 - share, the incoming one the rest,
	// so each pixel shows one of them (where a proxy fade is drawing too, it wins)
	if ( ( entry & 0x7f000000u ) != 0u && vFade == 1.0 ) {
		const float keep = 1.0 - float( ( entry >> 24u ) & 127u ) / 127.0;
		vFade = ( entry & 0x80000000u ) != 0u ? -max( keep, 1e-3 ) : keep;
	}
#ifdef GPU_DRIVEN
	vMat = drawMaterial[pc.info.x + uint( gl_DrawIDARB )];
	const uint kind = materials[vMat].info.x;
	const uint code = materials[vMat].info.w;
	vUV1 = vec2( 0.0 );
	vCol = vec4( 0.0 );
	const float detailKind = materialDetail[materials[vMat].info.z].x;
	vCoarse = 0u;
#ifdef VRS
	// r_sceneVrs (pc.tint on the GPU-driven draws, units; 0: never): x the distance past which terrain shades at 2 x 2
	// (W3 terrain tiles, detail -3, or a terrain flag's code 128), y SpeedTree leaves', z every other surface's. By the
	// provoking vertex's distance from the view
	{
		const float far_ = ( code == 128u || detailKind == -3.0 ) ? pc.tint.x : code == 32u ? pc.tint.y : pc.tint.z;
		vCoarse = ( far_ > 0.0 && distance( vPos, pc.eye.xyz ) > far_ ) ? 1u : 0u;
		gl_PrimitiveShadingRateEXT = vCoarse != 0u ? ( gl_ShadingRateFlag2VerticalPixelsEXT | gl_ShadingRateFlag2HorizontalPixelsEXT ) : 0;
	}
#endif
	if ( detailKind == -9.0 || detailKind == -10.0 ) {
		// Witcher 3 SpeedTree billboards (.xw3b version 3, vk_scene.cpp): -9 the card (its vertices (0, x, z) in the tree's
		// space, UV the card's own (s, t)) turned about the tree's up axis to face the viewer, its UVs the stored view
		// nearest the viewer's direction (vUV) and the next (vUV1, scene.frag's crossfade share vCol.x); -10 the top card,
		// static. Both: vCol.y the top card's share by the view's pitch (block 0: a0, the band's start and end, radians,
		// views). The viewer: the frame's main eye in every pass, the cascades included: a card turned to the light there
		// crossed the one the eye saw, which fell in its shadow (dark rectangles); one plane cannot shade itself
		const vec4 bh = materialDetail[uint( materialDetail[materials[vMat].info.z].y )];
		const uint bb = uint( materialDetail[materials[vMat].info.z].y );
		const vec3 ax = vec3( I.r0.x, I.r1.x, I.r2.x ), ay = vec3( I.r0.y, I.r1.y, I.r2.y ), az = vec3( I.r0.z, I.r1.z, I.r2.z );
		const vec3 centre = vec3( I.r0.w, I.r1.w, I.r2.w ), up = normalize( az );
		const vec3 toV = normalize( fr.eye.xyz - centre );
		const float pitch = asin( clamp( abs( dot( toV, up ) ), 0.0, 1.0 ) );
		vCol = vec4( 0.0, smoothstep( bh.y, max( bh.z, bh.y + 1e-3 ), pitch ), 0.0, 0.0 );
		if ( detailKind == -9.0 ) {
			vec3 hz = toV - up * dot( toV, up );
			hz = dot( hz, hz ) > 1e-8 ? normalize( hz ) : normalize( ax );
			const vec3 right = normalize( cross( up, hz ) );
			const float scale = length( az );
			vPos = centre + right * ( inPos.y * scale ) + up * ( inPos.z * scale );
			vNrm = hz;
			vTan = vec4( right, 1.0 );
			const float n = max( bh.w, 1.0 );
			float f = ( atan( dot( hz, normalize( ay ) ), dot( hz, normalize( ax ) ) ) - bh.x ) * ( n / 6.28318531 );
			f = mod( f, n );
			const float i0 = min( floor( f ), n - 1.0 ), i1 = mod( i0 + 1.0, n );
			const vec4 a0 = materialDetail[bb + 1u + 2u * uint( i0 )], b0 = materialDetail[bb + 2u + 2u * uint( i0 )];
			const vec4 a1 = materialDetail[bb + 1u + 2u * uint( i1 )], b1 = materialDetail[bb + 2u + 2u * uint( i1 )];
			vUV = vec2( a0.x * inUV.x + a0.y * inUV.y + a0.z, a0.w * inUV.x + b0.x * inUV.y + b0.y );
			vUV1 = vec2( a1.x * inUV.x + a1.y * inUV.y + a1.z, a1.w * inUV.x + b1.x * inUV.y + b1.y );
			vCol.x = clamp( ( f - i0 - 0.35 ) / 0.3, 0.0, 1.0 );	// (the crossfade over the middle 30% between two views)
		}
	} else if ( detailKind <= -2.0 ) {	// -2 a layer blend, -3 a terrain tile
		const uint at = uint( gl_VertexIndex ) * 3u;
		vUV1 = vec2( uintBitsToFloat( w3Stream[at] ), uintBitsToFloat( w3Stream[at + 1u] ) );
		vCol = unpackUnorm4x8( w3Stream[at + 2u] );
	}
#else
	const uint kind = pc.info.x;
	const uint code = pc.info.w;
#endif
	vCanopy = vec3( 0.0 );
	if ( code == 32u ) {
		const vec4 s = meshes[I.info.x].sphere;
		const vec4 c = vec4( s.xyz, 1.0 );
		vCanopy = ( vPos - vec3( dot( I.r0, c ), dot( I.r1, c ), dot( I.r2, c ) ) ) / max( s.w * sqrt( aa ), 1e-3 );
	}
	vec3 drawn = vPos;
	// a glowing surface, and an instance that lies on another (instance slot bit 26, tag_interiors.py: CP2077's foliage
	// litter, authored flush with the street it lies on, so the two fought for each pixel as the eye moved), pulled toward
	// the eye along its ray: only its depth changes
	if ( kind == 3u || ( pc.eye.w > 0.5 && ( I.info.z & 0x4000000u ) != 0u ) ) {
		const vec3 toEye = pc.eye.xyz - vPos;
		const float dist = length( toEye );
		drawn += toEye * ( min( EMISSIVE_PULL + EMISSIVE_PULL_PER_UNIT * dist, 0.5 * dist ) / max( dist, 1e-3 ) );
	}
	// terrain (info.w 128: material flag 0x1000, CP2077's multilayered_terrain.mt, with r_sceneTerrainPush): the streets,
	// sidewalks and plazas laid on it lie in its plane, and the two fought for each pixel as the eye moved, so it is pushed away
	// from the eye along its ray by as much as the litter is
	// pulled toward it: only its depth changes; the eye's views
	if ( code == 128u && pc.eye.w > 0.5 ) {
		const vec3 toEye = pc.eye.xyz - vPos;
		const float dist = length( toEye );
		drawn -= toEye * ( ( EMISSIVE_PULL + EMISSIVE_PULL_PER_UNIT * dist ) / max( dist, 1e-3 ) );
	}
	// r_sceneProxyPush: a far proxy (a near distance) pushed away from the eye along its ray by a share of its distance
	// that grows with its near distance (per 100 m of it, at most 2%), so the detail it stands for wins where both draw
	// (coplanar road proxies) and a proxy that starts further out sits behind one that starts nearer (a district's road
	// proxy under the splines'); only its depth changes; the eye's views
	if ( pc.eye.w > 0.5 && fr.skyAmbient.w > 0.0 && I.info.y != 0u )
		drawn += ( drawn - pc.eye.xyz ) * min( fr.skyAmbient.w * ( 1.0 + uintBitsToFloat( I.info.y ) / 3750.0 ), 0.02 );
	// r_sceneEyeRelative (eye.w 2: the eye's views): the matrix is the view's rotation and projection alone, the eye at
	// its origin, and the position is taken relative to the eye first. With the whole view's translation in the matrix
	// (tens of thousands of units out in Watson's districts) float32 lost more depth than a few centimetres in the
	// product's cancellation, so surfaces that close fought (a facade panel and its backing 4 cm behind it, flipping
	// triangle by triangle as the eye moved)
	gl_Position = pc.viewProj * vec4( pc.eye.w > 1.5 ? drawn - pc.eye.xyz : drawn, 1.0 );
}
