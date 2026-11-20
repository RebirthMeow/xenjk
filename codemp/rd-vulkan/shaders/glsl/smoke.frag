#version 450

// r_smoke (vk_smoke.cpp): one smoke volume raymarched after the world resolve, composited premultiplied
// (ONE, ONE_MINUS_SRC_ALPHA). The cells are a flood fill over SMOKE_CELL-unit cells; the atlas stacks the GZ
// z slices of GX x GY cells down the texture. Channels: r arrival (0 at the seed .. 1 at the last cell), g cells to
// the nearest empty cell / SMOKE_EDGE_CELLS, b occupied cells toward the light / SMOKE_RAY_CELLS, a the same
// straight up. Keep the constants in step with vk_smoke.cpp.

layout(set = 0, binding = 0) uniform sampler2D depthTex;	// depth extract: r linear view depth (0 = sky; negative = character)
layout(set = 1, binding = 0) uniform sampler2D gridTex;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PushConst
{
	vec4	eye;		// xyz view origin; w age (s)
	vec4	ray00;		// xyz world ray through uv (0,0), unit forward component; w lifetime (s)
	vec4	rayDX;		// xyz ray change across uv.x; w growth time (s)
	vec4	rayDY;		// xyz ray change across uv.y; w extinction per unit
	vec4	grid;		// xyz world corner of cell (0,0,0); w frame number (jitter)
	vec3	boxMin;		// occupied cells' bounds
	uint	ambient;	// unorm rgb, a = intensity / 4
	vec3	boxMax;
	uint	sunColor;	// unorm rgb, a = intensity / 4
	vec4	light;		// xyz direction toward the light; w fade time (s)
} pc;

const float GX = 64.0;
const float GY = 64.0;
const float GZ = 24.0;
const float CELL = 16.0;
const float EDGE_CELLS = 8.0;
const float RAY_CELLS = 10.0;
const float ALBEDO = 0.72;	// mid grey: lit by the scene, never white on its own
const int MAX_STEPS = 160;

vec4 sampleGrid( vec3 p )
{
	vec3 g = ( p - pc.grid.xyz ) / CELL;
	float zf = clamp( g.z - 0.5, 0.0, GZ - 1.0 );
	float k0 = floor( zf );
	float k1 = min( k0 + 1.0, GZ - 1.0 );
	float x = clamp( g.x, 0.5, GX - 0.5 ) / GX;
	float y = clamp( g.y, 0.5, GY - 0.5 );
	vec4 a = textureLod( gridTex, vec2( x, ( k0 * GY + y ) / ( GY * GZ ) ), 0.0 );
	vec4 b = textureLod( gridTex, vec2( x, ( k1 * GY + y ) / ( GY * GZ ) ), 0.0 );
	return mix( a, b, zf - k0 );
}

float hash( vec3 p )
{
	p = fract( p * 0.3183099 + 0.1 );
	p *= 17.0;
	return fract( p.x * p.y * p.z * ( p.x + p.y + p.z ) );
}

float vnoise( vec3 x )
{
	vec3 i = floor( x );
	vec3 f = fract( x );
	f = f * f * ( 3.0 - 2.0 * f );
	return mix( mix( mix( hash( i + vec3( 0, 0, 0 ) ), hash( i + vec3( 1, 0, 0 ) ), f.x ),
					 mix( hash( i + vec3( 0, 1, 0 ) ), hash( i + vec3( 1, 1, 0 ) ), f.x ), f.y ),
				mix( mix( hash( i + vec3( 0, 0, 1 ) ), hash( i + vec3( 1, 0, 1 ) ), f.x ),
					 mix( hash( i + vec3( 0, 1, 1 ) ), hash( i + vec3( 1, 1, 1 ) ), f.x ), f.y ), f.z );
}

float fbm( vec3 p )
{
	return 0.50 * vnoise( p ) + 0.28 * vnoise( p * 2.03 + 17.1 ) + 0.14 * vnoise( p * 4.01 + 31.7 ) + 0.08 * vnoise( p * 8.07 + 5.3 );
}

const float BASE = 6.0;		// the surface sits this far inside the last occupied cell centre
const float AMP = 34.0;		// noise erosion depth
const float SOFT = 7.0;		// density ramp: a firm, billowed surface

// 0..1 density at p and its grid sample; edgeW is the distance (units) inside the eroded surface
float density( vec3 p, float grow, float fade, vec3 drift, out vec4 s, out float edgeW )
{
	s = sampleGrid( p );
	edgeW = -1.0;
	if ( s.g <= 0.002 )
		return 0.0;
	float front = smoothstep( s.r - 0.12, s.r + 0.02, grow );
	float erode = ( 1.0 - fade ) * ( 0.5 * EDGE_CELLS * CELL ) + ( 1.0 - front ) * 24.0;
	edgeW = s.g * EDGE_CELLS * CELL - BASE - erode;
	float d;
	if ( edgeW - AMP >= SOFT * 4.0 ) {
		d = 1.0;	// deep inside: no noise needed
	} else {
		edgeW -= AMP * fbm( ( p + drift ) / 30.0 );
		d = clamp( edgeW / SOFT, 0.0, 1.0 );
	}
	return d * front * fade * fade;	// thins out as it shrinks at the end
}

void main()
{
	float depth = abs( texelFetch( depthTex, ivec2( gl_FragCoord.xy ), 0 ).r );
	vec3 dir = pc.ray00.xyz + frag_tex_coord.x * pc.rayDX.xyz + frag_tex_coord.y * pc.rayDY.xyz;
	float tScene = depth > 0.0 ? depth : 1e7;

	// the ray's span inside the occupied bounds, in front of the opaque scene
	vec3 inv = 1.0 / dir;
	vec3 t0 = ( pc.boxMin - pc.eye.xyz ) * inv;
	vec3 t1 = ( pc.boxMax - pc.eye.xyz ) * inv;
	vec3 tmin = min( t0, t1 );
	vec3 tmax = max( t0, t1 );
	float tNear = max( max( tmin.x, tmin.y ), max( tmin.z, 0.0 ) );
	float tFar = min( min( tmax.x, tmax.y ), min( tmax.z, tScene ) );
	if ( tNear >= tFar )
		discard;

	float len = length( dir );
	vec3 vdir = dir / len;
	float stepW = max( 6.0, ( tFar - tNear ) * len / float( MAX_STEPS ) );
	float dt = stepW / len;
	float jitter = fract( 52.9829189 * fract( dot( gl_FragCoord.xy + pc.grid.w * vec2( 5.588, 3.17 ), vec2( 0.06711056, 0.00583715 ) ) ) );

	float age = pc.eye.w;
	float grow = 1.0 - pow( 1.0 - clamp( age / pc.rayDX.w, 0.0, 1.0 ), 2.0 );	// fast out of the canister, easing off
	float fade = clamp( ( pc.ray00.w - age ) / pc.light.w, 0.0, 1.0 );			// 1 .. 0 over the last seconds
	float sigma = pc.rayDY.w;

	vec4 ap = unpackUnorm4x8( pc.ambient );
	vec4 sp = unpackUnorm4x8( pc.sunColor );
	vec3 ambient = ap.rgb * ap.a * 4.0;
	vec3 sun = sp.rgb * sp.a * 4.0;

	// Henyey-Greenstein, forward scattering, normalised to 1 on average and pulled halfway to isotropic
	const float g = 0.35;
	float cosT = dot( vdir, pc.light.xyz );
	float phase = mix( 1.0, ( 1.0 - g * g ) / pow( 1.0 + g * g - 2.0 * g * cosT, 1.5 ), 0.5 );

	// the billows drift up and turn over slowly
	vec3 drift = vec3( age * 3.0, age * 2.0, -age * 9.0 );

	float T = 1.0;
	vec3 col = vec3( 0.0 );
	float t = tNear + dt * jitter;
	for ( int i = 0; i < MAX_STEPS && t < tFar; i++, t += dt ) {
		vec3 p = pc.eye.xyz + dir * t;
		vec4 s;
		float edgeW;
		float d = density( p, grow, fade, drift, s, edgeW );
		if ( d <= 0.0 )
			continue;

		// the cells' coarse shadow toward the light, softened (multiple scattering lights the shadowed side); both
		// shadows thin with the smoke at the end
		float Tl = exp( -s.b * RAY_CELLS * CELL * sigma * 0.25 * fade );
		float sky = mix( 1.0, 0.5, s.a * fade );
		// near the surface each billow shades itself: a short march toward the light through the noisy density,
		// and ambient darkens under the billow above and deeper in
		if ( edgeW < 64.0 ) {
			vec4 sl;
			float el;
			float od = density( p + pc.light.xyz * 8.0, grow, fade, drift, sl, el ) * 8.0
				+ density( p + pc.light.xyz * 20.0, grow, fade, drift, sl, el ) * 12.0
				+ density( p + pc.light.xyz * 40.0, grow, fade, drift, sl, el ) * 20.0;
			Tl *= exp( -od * sigma * 0.35 );
			float up = density( p + vec3( 0.0, 0.0, 14.0 ), grow, fade, drift, sl, el );
			sky *= mix( 1.0, 0.75, up );
		}
		sky *= mix( 0.6, 1.0, exp( -max( edgeW, 0.0 ) / 40.0 ) );	// ambient comes in from outside
		vec3 L = ALBEDO * ( ambient * sky + sun * Tl * phase );

		float a = 1.0 - exp( -sigma * d * stepW );
		col += T * a * L;
		T *= 1.0 - a;
		if ( T < 0.01 )
			break;
	}

	float alpha = 1.0 - T;
	if ( alpha < 0.002 )
		discard;
	out_color = vec4( col, alpha );
}
