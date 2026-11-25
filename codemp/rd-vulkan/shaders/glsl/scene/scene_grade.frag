#version 450
// Render scenes (vk_scene.cpp): the map's colour grade over the resolved frame (vk_scene_grade), before the HUD. The
// grade is the source game's display transform: a 3D LUT from scene light (ARRI LogC v3, EI 800) to display light,
// its tone curve included. The frame is in display space (the engine's gamma 2.2), so each pixel goes back through
// scene.frag's tone curve to the light that curve was given, then through the LUT, and leaves sRGB-encoded. On a float
// colour target the render scene's light past white comes back too (scene.frag's toDisplay). GRADE_COMPUTE: the same
// grade as a compute shader over the resolved frame in place (r_sceneGradeCompute: a storage colour image, no copy).
// A day's grades (XLUT version 2) stack in the LUT's depth, one n^3 block per hour: the two around the clock are
// blended (p.w: the earlier one's index plus the later one's share; vk_scene_post.cpp's Scene_GradeSlice).

#ifdef GRADE_COMPUTE
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, rgba16f) uniform image2D frameImg;	// the resolved frame (a float target: vk_scene.cpp)
#else
layout(set = 0, binding = 0) uniform sampler2D frameTex;	// a copy of the resolved frame
#endif
layout(set = 0, binding = 1) uniform sampler3D lutTex;		// r along x, g along y, b along z (an hour's block each n); linear filtering

layout(push_constant) uniform Push {
	vec4 p;		// x: scene.frag's tone curve (0 exponential, 1 filmic), y: the LUT's input scale, z: strength (0 off .. 1),
				// w: the hour blocks' blend (block floor(w) to the next by fract(w); 0 with one block)
} pc;

#ifndef GRADE_COMPUTE
layout(location = 0) out vec4 outColor;
#endif

// scene.frag's toDisplay inverse: the frame's encoding -> the light its tone curve was given (after exposure); past
// TONE_TOP the headroom a float colour target keeps (r_hdr 2), e-folds of light per TONE_LOG
const float TONE_TOP = 0.999, TONE_LOG = 0.1;
vec3 toneInverse( vec3 y )
{
	y = pow( clamp( y, 0.0, TONE_TOP ), vec3( 2.2 ) );
	if ( pc.p.x > 0.5 ) {
		const vec3 a = 2.51 - 2.43 * y, b = 0.03 - 0.59 * y, c = -0.14 * y;
		return ( -b + sqrt( b * b - 4.0 * a * c ) ) / ( 2.0 * a );
	}
	return -log( 1.0 - y );
}
vec3 curveInput( const vec3 y )
{
	const float top = toneInverse( vec3( TONE_TOP ) ).x;
	return mix( toneInverse( y ), top * exp( ( y - TONE_TOP ) / TONE_LOG ), greaterThan( y, vec3( TONE_TOP ) ) );
}

// ARRI LogC v3 at EI 800 (the LUT's input mapping, CP's CMF_ArriLogC)
vec3 logC( const vec3 x )
{
	const float cut = 0.010591, a = 5.555556, b = 0.052272, c = 0.247190, d = 0.385537, e = 5.367655, f = 0.092809;
	const vec3 lin = e * x + f;
	const vec3 lg = c * log( max( a * x + b, vec3( 1e-6 ) ) ) / log( 10.0 ) + d;
	return mix( lin, lg, greaterThan( x, vec3( cut ) ) );
}

vec3 lutAt( const vec3 uvw )
{
#ifdef GRADE_COMPUTE
	return textureLod( lutTex, uvw, 0.0 ).rgb;	// (one level)
#else
	return texture( lutTex, uvw ).rgb;
#endif
}

void main()
{
#ifdef GRADE_COMPUTE
	const ivec2 at = ivec2( gl_GlobalInvocationID.xy );
	if ( any( greaterThanEqual( at, imageSize( frameImg ) ) ) )
		return;
	const vec4 src = imageLoad( frameImg, at );
#else
	const vec4 src = texelFetch( frameTex, ivec2( gl_FragCoord.xy ), 0 );
#endif
	const ivec3 size = textureSize( lutTex, 0 );
	const float n = float( size.x ), blocks = max( 1.0, floor( float( size.z ) / n + 0.5 ) );
	const vec3 t = clamp( logC( curveInput( src.rgb ) * pc.p.y ), 0.0, 1.0 );
	const vec3 cell = t * ( ( n - 1.0 ) / n ) + 0.5 / n;	// texel centres of one block
	const float k0 = clamp( floor( pc.p.w ), 0.0, blocks - 1.0 ), k1 = mod( k0 + 1.0, blocks ), share = fract( pc.p.w );
	vec3 graded = lutAt( vec3( cell.xy, ( k0 + cell.z ) / blocks ) );
	if ( blocks > 1.5 && share > 0.0 )
		graded = mix( graded, lutAt( vec3( cell.xy, ( k1 + cell.z ) / blocks ) ), share );
	// the source's frame is sRGB-encoded: near black its linear segment keeps the LUT's black (0.0034) at 11/255,
	// where gamma 2.2 would lift it to 19
	const vec3 g = clamp( graded, 0.0, 1.0 );
	const vec3 display = mix( 1.055 * pow( g, vec3( 1.0 / 2.4 ) ) - 0.055, g * 12.92, lessThanEqual( g, vec3( 0.0031308 ) ) );
#ifdef GRADE_COMPUTE
	imageStore( frameImg, at, vec4( mix( src.rgb, display, pc.p.z ), src.a ) );
#else
	outColor = vec4( mix( src.rgb, display, pc.p.z ), src.a );
#endif
}
