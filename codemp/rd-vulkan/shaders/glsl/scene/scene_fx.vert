#version 450
// Render scenes (vk_scene.cpp, r_sceneFx): CP2077's placed particle emitters (maps/<map>.xfx), six vertices a particle slot
// of one placement's emitter, a camera-facing quad; nothing is kept between frames: a particle's birth, life, place,
// size, turn, alpha and flipbook frame come from the game's clock and hashes of the placement's seed, the slot and its
// cycle (vk_scene.cpp Scene_DrawFx draws them; scene.frag modes 10 and 11 shade them).

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
						// picked by a 3D room cell on a grid turned to the facade (r_sceneWindowCell)
	vec4 water;			// x: the water's clock (s), y, z: its surfaces' lowest and highest z, w: ripple strength
} fr;

struct Light { vec4 posRadius; vec4 colorPower; vec4 dirCosOuter; vec4 cosInnerType; };
layout(std430, set = 0, binding = 2) readonly buffer Lights { Light lights[]; };
layout(std430, set = 0, binding = 3) readonly buffer Cells { uvec2 cells[]; };
layout(std430, set = 0, binding = 4) readonly buffer CellLights { uint cellLights[]; };
// the .xfx's emitters (32 vec4 each, cp77fx.py's EMITTER) and placements (4 vec4: the 3x4 map's rows, then system,
// emission scale, hide, seed) after the materials' records
layout(std430, set = 0, binding = 13) readonly buffer MaterialDetail { vec4 materialDetail[]; };
layout(std430, set = 0, binding = 17) readonly buffer LightFade { vec4 lightFade[]; };	// scene_lightfade.comp: x its fade

// x: the placement's first vec4, y the emitter's, z its slots, w its flipbook (columns | rows << 8)
layout(push_constant) uniform Push { mat4 viewProj; vec4 emissive; vec4 tint; uvec4 info; vec4 eye; } pc;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;		// the local lights at the particle (linear, the light scale and 1 / pi in; scene.frag
										// scales an unlit one's by r_sceneFxLight's third)
layout(location = 2) out vec4 vTan;		// x alpha, y the soft fade's distance (units), z flags, w 1 alive
layout(location = 3) out vec2 vUV;
layout(location = 4) out vec3 vLocal;	// the dark colour
layout(location = 5) out vec3 vLocalNrm;	// the light colour
layout(location = 6) flat out uint vInst;
layout(location = 7) flat out vec3 vUpW;
layout(location = 9) flat out uint vSignal;
layout(location = 10) flat out float vFade;
layout(location = 11) out vec3 vCanopy;

const float UNITS_PER_METRE = 37.5;

uint pcg( uint v ) { const uint s = v * 747796405u + 2891336453u; const uint w = ( ( s >> ( ( s >> 28u ) + 4u ) ) ^ s ) * 277803737u; return ( w >> 22u ) ^ w; }
float rnd( inout uint s ) { s = pcg( s ); return float( s ) * ( 1.0 / 4294967296.0 ); }

// the emitter's float i (cp77fx.py's EMITTER, in order: 0 texture, 1 flags, 2 max particles, 3 duration, 4 birth[8],
// 12 life[2], 14 size x[2], 16 size y[2], 18 size curve[8], 26 alpha[2], 28 alpha curve[8], 36 turn[2], 38 turn
// rate[2], 40 turn rate curve[8], 48 spawn radius[2], 50 spawn box[3], 53 spawn force[2], 55 position min[3], 58 max[3],
// 61 velocity min[3], 64 max[3], 67 velocity curve[8], 75 acceleration[3], 78 drag[8], 86 frames a second,
// 87 first frame[2], 89 near fade[2], 91 far fade[2], 93 dark[3], 96 light[3], 99 max alpha, 100 soft, 101 wind,
// 102 LOD distance, 103 stretch, 104 turbulence min[3], 107 max[3], 110 interval, 111 life share, 112 noise amplitude[3],
// 115 frequency, 116 change rate[3], 119 amplitude curve[3], 122 size near start, end, multiplier, 125 far ones)
float E( const uint i ) { return materialDetail[pc.info.y + i / 4u][i % 4u]; }
vec3 E3( const uint i ) { return vec3( E( i ), E( i + 1u ), E( i + 2u ) ); }
// an 8-sample curve over [0, 1] at x, linear between
float curve( const uint at, const float x )
{
	const float p = clamp( x, 0.0, 1.0 ) * 7.0;
	const uint i = min( uint( p ), 6u );
	return mix( E( at + i ), E( at + i + 1u ), p - float( i ) );
}
// its integral from 0 to x
float curveInt( const uint at, const float x )
{
	const float p = clamp( x, 0.0, 1.0 ) * 7.0;
	float s = 0.0;
	for ( uint i = 0u; i < 7u; i++ ) {
		const float a = E( at + i ), b = E( at + i + 1u ), seg = clamp( p - float( i ), 0.0, 1.0 );
		s += ( a + 0.5 * ( b - a ) * seg ) * seg;
	}
	return s / 7.0;
}
// the distance travelled by age x of a life (in units of the starting velocity): the integral of the velocity curve
// times exp(-the drag's integral), drag a rate per second over the life (8 steps, midpoints)
float moved( const float x, const float life )
{
	float s = 0.0, D = 0.0;
	const float h = x / 8.0;
	for ( int k = 0; k < 8; k++ ) {
		const float u = ( float( k ) + 0.5 ) * h, drag = max( curve( 78u, u ), 0.0 ) * life * h;
		D += 0.5 * drag;
		s += curve( 67u, u ) * exp( -D );
		D += 0.5 * drag;
	}
	return s * h * life;
}

// the distance an acceleration carries a particle by age (x of its life), in units of the acceleration: under the same
// drag as its velocity (r_sceneFxAccelDrag, pc.emissive.x): with k the drag's mean rate over the age, t / k - (1 - e^-kt)
// / k^2 (it settles at a / k); without (or with no drag) half t^2 (system 59's steam coasted
// 231 m in a straight line that way)
float accelMoved( const float age, const float x, const float life )
{
	float D = 0.0;
	const float h = x / 8.0;
	for ( int k = 0; k < 8; k++ )
		D += max( curve( 78u, ( float( k ) + 0.5 ) * h ), 0.0 ) * life * h;
	if ( pc.emissive.x < 0.5 || D < 1e-4 || age < 1e-4 )
		return 0.5 * age * age;
	const float kr = D / age;
	return age / kr - ( 1.0 - exp( -D ) ) / ( kr * kr );
}

// a smooth 1D noise about -1..1 (gradients at the integers, quintic between)
float noise1( const float t )
{
	const float i = floor( t ), f = t - i, u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	const float g0 = fract( sin( i * 127.1 ) * 43758.5453 ) * 2.0 - 1.0, g1 = fract( sin( ( i + 1.0 ) * 127.1 ) * 43758.5453 ) * 2.0 - 1.0;
	return 2.0 * mix( g0 * f, g1 * ( f - 1.0 ), u );
}

// CP2077's VelocityTurbulize (flag 4096): every interval while the particle is within its life share, a new random
// velocity in [min, max] replacing the last, scaled by the emitter's velocity curve and drag (`speed`: their mean over
// the age so far); at most 16 intervals (more: 16 longer ones at sqrt(n) / 16 x n the speed, the same spread)
vec3 turbulence( const uint seed, const float age, const float life, const float speed )
{
	const float I = max( E( 110u ), 0.01 ), span = min( age, E( 111u ) * life );
	float n = ceil( span / I ), step = I, gain = 1.0;
	if ( n > 16.0 ) {
		gain = sqrt( 16.0 / n ) * ( n * I ) / span;	// (the spread of n intervals of I, carried by 16 of span / 16)
		step = span / 16.0;
		n = 16.0;
	}
	vec3 d = vec3( 0.0 );
	uint s = seed ^ 0x9e3779b9u;
	for ( int k = 0; k < int( n ); k++ ) {
		const vec3 v = mix( E3( 104u ), E3( 107u ), vec3( rnd( s ), rnd( s ), rnd( s ) ) );
		d += v * clamp( span - float( k ) * step, 0.0, step );
	}
	return d * ( gain * speed );
}

// CP2077's Noise on the velocity (flag 8192): per axis a smooth wander, amplitude x (n(phase + rate x age) - n(phase))
// / rate (a velocity of about the amplitude; age x n'(phase) as the rate goes to 0), its phase from the frequency x the
// spawn point and the particle's own hash; the amplitude's curve over the life at three points
vec3 noiseDrift( const vec3 P0, const uint seed, const float age, const float x )
{
	uint s = seed ^ 0x85ebca6bu;
	const vec3 ph = vec3( dot( P0, vec3( 1.0, 1.7, 2.3 ) ), dot( P0, vec3( 2.1, 0.6, 1.3 ) ), dot( P0, vec3( 0.9, 2.4, 0.7 ) ) ) * E( 115u ) +
		vec3( rnd( s ), rnd( s ), rnd( s ) ) * 64.0;
	const vec3 rate = E3( 116u );
	vec3 d;
	for ( int a = 0; a < 3; a++ ) {
		const float r = abs( rate[a] );
		d[a] = r > 1e-3 ? ( noise1( ph[a] + r * age ) - noise1( ph[a] ) ) / r : age * noise1( ph[a] );
	}
	const float amp = x < 0.5 ? mix( E( 119u ), E( 120u ), x * 2.0 ) : mix( E( 120u ), E( 121u ), x * 2.0 - 1.0 );
	return d * E3( 112u ) * amp;
}

// the light grid cell's lights at P: their falloff and cones, no shadows; boxed (interior) and haze-only lights left out,
// and those faded past their hide distance (as the surfaces); the sphere read first
vec3 localLight( const vec3 P )
{
	const ivec3 c = ivec3( floor( ( P - fr.gridOrigin.xyz ) / fr.gridOrigin.w ) );
	if ( any( lessThan( c, ivec3( 0 ) ) ) || any( greaterThanEqual( c, fr.gridDims.xyz ) ) )
		return vec3( 0.0 );
	const uvec2 cell = cells[( c.z * fr.gridDims.y + c.y ) * fr.gridDims.x + c.x];
	vec3 sum = vec3( 0.0 );
	const uint count = cell.y & 0x7fffffffu;
	for ( uint i = 0u; i < count; i++ ) {
		const uint li = cellLights[cell.x + i];
		const vec4 sphere = lights[li].posRadius;
		const vec3 Lv = sphere.xyz - P;
		const float d = length( Lv );
		if ( d >= sphere.w )
			continue;
		const float fade = lightFade[li].x;
		if ( fade <= 0.0 )
			continue;
		const Light L = lights[li];
		const int type = int( L.cosInnerType.y );
		if ( ( type & 2 ) != 0 || L.cosInnerType.z > 0.5 )
			continue;
		const float x = d / L.posRadius.w;
		float att = clamp( 1.0 - x * x * x * x, 0.0, 1.0 );
		const float dm = d / UNITS_PER_METRE;
		att = att * att / ( dm * dm + 1.0 );
		if ( ( type & 1 ) != 0 )
			att *= smoothstep( L.dirCosOuter.w, L.cosInnerType.x, dot( -Lv / max( d, 1e-3 ), L.dirCosOuter.xyz ) );
		sum += L.colorPower.rgb * ( L.colorPower.w * att * fade );
	}
	return sum * fr.params.x / 3.14159265;
}

void main()
{
	const uint slot = uint( gl_VertexIndex ) / 6u, corner = uint( gl_VertexIndex ) % 6u;
	const vec2 q = corner == 0u || corner == 3u ? vec2( -1.0, -1.0 ) : corner == 1u ? vec2( 1.0, -1.0 ) : corner == 5u ? vec2( -1.0, 1.0 ) : vec2( 1.0, 1.0 );
	vPos = vec3( 0.0 );
	vNrm = vec3( 0.0 );
	vTan = vec4( 0.0 );
	vUV = vec2( 0.0 );
	vLocal = vLocalNrm = vec3( 0.0 );
	vInst = 0u;
	vUpW = vec3( 0.0, 0.0, 1.0 );
	vSignal = 0u;
	vFade = 1.0;
	vCanopy = vec3( 0.0 );
	gl_Position = vec4( 2.0, 2.0, 2.0, 1.0 );	// (dead: off the view)

	const uint P = pc.info.x;
	const vec4 m0 = materialDetail[P], m1 = materialDetail[P + 1u], m2 = materialDetail[P + 2u], pl = materialDetail[P + 3u];
	const uint seed = floatBitsToUint( pl.w ), flags = floatBitsToUint( E( 1u ) );
	float peak = 0.0;
	for ( uint i = 0u; i < 8u; i++ )
		peak = max( peak, E( 4u + i ) );
	peak *= pl.y;
	if ( peak <= 0.0 )
		return;
	// the slot's cycle: it fires once every slots / peak seconds, slot k at k / peak into it
	const float T = float( pc.info.z ) / peak, phase = float( slot ) / peak;
	const float t = fr.water.x + float( seed & 1023u ) * 0.37;
	const float cycle = floor( ( t + phase ) / T ), born = cycle * T - phase, age = t - born;
	uint s = pcg( seed ^ pcg( pc.info.y * 7919u + slot * 104729u + uint( int( cycle ) & 0x7fffffff ) * 2654435761u ) );
	const float life = mix( E( 12u ), E( 13u ), rnd( s ) );
	if ( life <= 0.0 || age > life )
		return;
	if ( rnd( s ) * peak > curve( 4u, fract( born / max( E( 3u ), 1e-3 ) ) ) * pl.y )
		return;	// thinned to the birth curve's rate then
	const float x = age / life;

	// spawn (emitter space; lengths in game units)
	vec3 p = mix( E3( 55u ), E3( 58u ), vec3( rnd( s ), rnd( s ), rnd( s ) ) );
	vec3 out_ = vec3( 0.0 );
	if ( ( flags & 4u ) != 0u )	// a box (half extents)
		p += ( vec3( rnd( s ), rnd( s ), rnd( s ) ) * 2.0 - 1.0 ) * E3( 50u );
	if ( ( flags & 24u ) != 0u ) {	// a circle (8) or a sphere (16)
		const float a = rnd( s ) * 6.2831853;
		vec3 d;
		if ( ( flags & 16u ) != 0u ) {
			const float z = rnd( s ) * 2.0 - 1.0, r = sqrt( max( 1.0 - z * z, 0.0 ) );
			d = vec3( r * cos( a ), r * sin( a ), ( flags & 512u ) != 0u ? abs( z ) : z );
		} else {
			d = vec3( cos( a ), sin( a ), 0.0 );
		}
		p += d * mix( E( 48u ), E( 49u ), rnd( s ) );
		out_ = d;
	}
	vec3 v = mix( E3( 61u ), E3( 64u ), vec3( rnd( s ), rnd( s ), rnd( s ) ) ) + out_ * mix( E( 53u ), E( 54u ), rnd( s ) );
	vec3 acc = ( flags & 1024u ) != 0u ? E3( 75u ) : vec3( 0.0 );
	const mat3 M = mat3( m0.x, m1.x, m2.x, m0.y, m1.y, m2.y, m0.z, m1.z, m2.z );	// emitter space -> world (its scale in)
	if ( ( flags & 32u ) == 0u ) {	// velocity (and acceleration) in emitter space
		v = M * v;
		if ( ( flags & 65536u ) == 0u )	// (flag 65536: the acceleration in world space alone, Witcher 3's gravity
			acc = M * acc;				// on a fountain's jets that leave along their emitter's axis)
	}
	const float mv = moved( x, life );
	vec3 C = M * p + vec3( m0.w, m1.w, m2.w ) + v * mv + acc * accelMoved( age, x, life );
	if ( ( flags & 4096u ) != 0u )	// CP2077's velocity turbulence (world axes)
		C += turbulence( s, age, life, mv / max( age, 1e-3 ) );
	if ( ( flags & 8192u ) != 0u )	// CP2077's noise on the velocity
		C += noiseDrift( M * p + vec3( m0.w, m1.w, m2.w ), s, age, x );

	// its look
	const float scale = length( vec3( m0.x, m1.x, m2.x ) );
	float rs = rnd( s ), sl = curve( 18u, x ) * scale;
	if ( ( flags & 16384u ) != 0u ) {	// CP2077's SizeByDistance: the near multiplier up to 1 over the near range, 1 to the far one over the far range
		const float d = distance( C, pc.eye.xyz );
		sl *= d <= E( 123u ) ? mix( E( 124u ), 1.0, smoothstep( E( 122u ), E( 123u ), d ) ) : mix( 1.0, E( 127u ), smoothstep( E( 125u ), E( 126u ), d ) );
	}
	const vec2 size = vec2( mix( E( 14u ), E( 15u ), rs ), mix( E( 16u ), E( 17u ), rs ) ) * sl;
	float alpha = mix( E( 26u ), E( 27u ), rnd( s ) ) * curve( 28u, x ) * E( 99u );
	const float turn = mix( E( 36u ), E( 37u ), rnd( s ) ) + mix( E( 38u ), E( 39u ), rnd( s ) ) * life * curveInt( 40u, x );
	const uint cols = max( pc.info.w & 255u, 1u ), rows = max( ( pc.info.w >> 8u ) & 255u, 1u );
	const uint frame = uint( floor( mix( E( 87u ), E( 88u ), rnd( s ) ) + E( 86u ) * age ) ) % ( cols * rows );
	if ( ( flags & 2048u ) != 0u ) {	// AlphaByDistance: in by the near pair, out by the far pair
		const float d = distance( C, pc.eye.xyz );
		alpha *= smoothstep( E( 90u ), E( 89u ), d ) * ( 1.0 - smoothstep( E( 92u ), E( 91u ), d ) );
	}
	if ( alpha <= 0.0 || size.x <= 0.0 || size.y <= 0.0 )
		return;

	// the quad: facing the eye, turned; flag 256 (an emitter-oriented drawer: Witcher 3's foam and rings lying on the
	// water) in the emitter's own xy plane instead
	vec3 right = -fr.fogLeft.xyz, up = fr.fogUp.xyz;
	if ( ( flags & 256u ) != 0u ) {
		right = normalize( M[0] );
		up = normalize( M[1] );
	}
	const float ca = cos( turn * 6.2831853 ), sa = sin( turn * 6.2831853 );
	const vec2 o = q * 0.5 * size;	// (sized, then turned: a 2 x 1 quad turned 90 degrees is 1 x 2)
	vPos = C + ( o.x * ca - o.y * sa ) * right + ( o.x * sa + o.y * ca ) * up;
	// flag 32768 (a normal-mapped water particle, Witcher 3's splashes, foam and rings: the image a normal map with the
	// shape in its alpha): its frame for scene.frag, the turned right axis and the face's normal toward the eye
	if ( ( flags & 32768u ) != 0u ) {
		vCanopy = ca * right + sa * up;
		vec3 n = normalize( cross( right, up ) );
		vUpW = dot( n, pc.eye.xyz - C ) < 0.0 ? -n : n;
	}
	vUV = ( vec2( float( frame % cols ), float( frame / cols ) ) + vec2( 0.5 + 0.5 * q.x, 0.5 - 0.5 * q.y ) ) / vec2( float( cols ), float( rows ) );
	vNrm = localLight( C );
	vTan = vec4( alpha, max( E( 100u ), 0.0 ) * 0.25 * UNITS_PER_METRE, float( flags ), 1.0 );
	vLocal = E3( 93u );
	vLocalNrm = E3( 96u );
	gl_Position = pc.viewProj * vec4( vPos, 1.0 );
}
