#version 450
#extension GL_GOOGLE_include_directive : require

#include "global.h"

layout(set = 0, binding = 0) uniform sampler2D depth_texture;
// r_charSelfShadowScreen: the sun shadow atlas (layer 3 holds the characters' self-shadow tiles) and the camera
// constants with its tiles (the push constants stay within the 128 bytes every device has)
layout(set = 1, binding = 0) uniform sampler2DArrayShadow sun_shadow_texture;
layout(set = 2, binding = VK_DESC_UNIFORM_CAMERA_BINDING) uniform Camera {
	vkUniformCamera_t u_camera;
};

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform SSAOParams {
	vkExtractSSAO_t push;
};

#include "common/ssao.glsl"

float applyAOResponse(float visibility) 
{
	float ao = clamp(visibility, 0.0, 1.0);
	ao = pow(ao, push.power);
	ao = clamp(1.0 - (1.0 - ao) * push.intensity, 0.0, 1.0);
	return ao;
}

bool depthFalloffReject( in float depth )
{
	float depth_falloff = push.depth_falloff;

	if (depth_falloff <= 0.0)
		return false;

	vec2 invSize		= vec2(push.inv_width, push.inv_height);
	float viewCenter = max(depth, 1e-3);
	float dx			= sampleDepth(frag_tex_coord + vec2(invSize.x, 0.0));
	float dy			= sampleDepth(frag_tex_coord + vec2(0.0, invSize.y));
	float dxm			= sampleDepth(frag_tex_coord - vec2(invSize.x, 0.0));
	float dym			= sampleDepth(frag_tex_coord - vec2(0.0, invSize.y));
	float vx  = max(dx,  1e-3);
	float vy  = max(dy,  1e-3);
	float vxm = max(dxm, 1e-3);
	float vym = max(dym, 1e-3);

	float maxRel = max(
		max(abs(vx - viewCenter), abs(vxm - viewCenter)),
		max(abs(vy - viewCenter), abs(vym - viewCenter))
	) / viewCenter;

	float gradTol = depth_falloff;
	if (depth_falloff < 0.5)
		gradTol = max(depth_falloff * 2.5, 0.02);

	return maxRel > gradTol;
}

float computeSSAO( vec3 P, vec3 normal )
{
	vec3 randVec = normalize( vec3(
		rand01( frag_tex_coord * 13.1 ) * 2.0 - 1.0,
		rand01( frag_tex_coord * 31.7 ) * 2.0 - 1.0,
		rand01( frag_tex_coord * 57.3 ) * 2.0 - 1.0
	) );

	vec3 tangent = normalize( randVec - normal * dot( randVec, normal ) );
	vec3 bitangent = cross( normal, tangent );
	mat3 tbn = mat3( tangent, bitangent, normal );

	float radius = push.radius;
	float bias = push.bias;
	int samples = clamp( int( push.samples + 0.5 ), 1, 32 );

	float occlusion = 0.0;

	float proj00 = 1.0 / max( push.projection.x, 1e-6 );
	float proj11 = 1.0 / max( push.projection.y, 1e-6 );

	float screenRadius = getScreenRadius( radius, P.z );

	for ( int i = 0; i < 32; ++i )
	{
		if ( i >= samples )
			break;

		float u1 = halton2( i + 1 );
		float u2 = halton3( i + 1 );

		float phi = u1 * 6.28318530718;

		// Cosine-weighted hemisphere.
		float cosTheta = sqrt( 1.0 - u2 );
		float sinTheta = sqrt( u2 );

		vec3 sampleDir = vec3(
			cos( phi ) * sinTheta,
			sin( phi ) * sinTheta,
			cosTheta
		);

		sampleDir = tbn * sampleDir;

		float t = ( float( i ) + 0.5 ) / float( samples );
		float sampleRadius = radius * sqrt( t );

		vec3 samplePos = P + sampleDir * sampleRadius;

		if ( samplePos.z >= -0.001 )
			continue;

		vec2 sampleNdc = vec2(
			samplePos.x * proj00 / -samplePos.z,
			samplePos.y * proj11 / -samplePos.z
		);

		vec2 sampleUV = sampleNdc * 0.5 + 0.5;

		if ( sampleUV.x <= 0.0 || sampleUV.x >= 1.0 ||
			 sampleUV.y <= 0.0 || sampleUV.y >= 1.0 )
			continue;

		float sampleDepth = sampleDepth(sampleUV);

		if ( depthIsEmpty( sampleDepth ) )
			continue;

		float sampleViewZ = -sampleDepth;
		float surfaceDelta;
#ifdef USE_REAL_NORMAL
		vec3 sampleViewPos = reconstructViewPosition( sampleUV, sampleDepth );
		vec3 sampleDelta = sampleViewPos - P;

		// use 1.0 once real normals are used, currently missing interpolation with reconstructed normals
		float normalWeight = 0.5; 

		float normalSurfaceDelta = length( sampleDelta );
		float oldSurfaceDelta = abs( sampleViewZ - P.z );
		surfaceDelta = mix( oldSurfaceDelta, normalSurfaceDelta, normalWeight );

		float normalDepthDelta = dot( sampleDelta, normal );
		float oldDepthDelta = sampleViewZ - samplePos.z;
		float depthDelta = mix( oldDepthDelta, normalDepthDelta, normalWeight );

		if ( depthDelta <= bias )
			continue;
#else
		float deltaZ = sampleViewZ - samplePos.z;
		if ( deltaZ <= bias ) // Only surfaces in front of the sample point can occlude it.
			continue;

		// Reject completely unrelated geometry.
		surfaceDelta = abs( sampleViewZ - P.z );
#endif

		if ( surfaceDelta > radius )
			continue;

		float rangeWeight = 1.0 -
			clamp( surfaceDelta / radius, 0.0, 1.0 );

		occlusion += rangeWeight;
	}

	// normalize by the requested number of samples.
	occlusion /= float( samples );
	occlusion = clamp( occlusion, 0.0, 1.0 );

    float ao = applyAOResponse( 1.0 - occlusion);

	// fade AO when the world-space radius projects to too few pixels.
	return mix( 1.0, ao, smoothstep( 0.0, 8.0, screenRadius ) );
}

float computeGTAO(vec3 P, vec3 N)
{
	vec3 V = normalize(-P);

	float radius		= push.radius;
	float intensity		= push.intensity;
	float frameNoise	= push.frame_noise;
	int sliceCount		= int(push.slices & 15u);
	int stepCount		= int(push.steps);
	vec2 invScreen		= vec2(push.inv_width, push.inv_height);
	// slices bits 8..15 (r_charAORadiusCap): the screen radius's cap in target pixels, 0 = 128
	const uint radiusCap = ( push.slices >> 8 ) & 255u;
	float screenRadius	= clamp( getScreenRadius( radius, P.z ), 8.0, radiusCap != 0u ? float( radiusCap ) : 128.0 );

	vec2 pixel = gl_FragCoord.xy;
	float noise = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))) + frameNoise );

	float visibilitySum = 0.0;

	for (int slice = 0; slice < sliceCount; slice++) {
		float phi = (float(slice) + noise) * M_PI / float(sliceCount);
		vec2 omega = vec2(cos(phi), sin(phi));

		vec3 sliceDir = reconstructViewPosition(frag_tex_coord + omega * invScreen, sampleDepth(frag_tex_coord)) - P;
		float sliceDirLen = length(sliceDir);

		if (sliceDirLen < 1e-6)
			continue;

		sliceDir /= sliceDirLen;

		vec3 orthoDir = sliceDir - dot(sliceDir, V) * V;
		vec3 axis = cross(sliceDir, V);
		vec3 projN = N - axis * dot(N, axis);

		float projNLen = length(projN);

		if (projNLen < 1e-5)
			continue;

		vec3 projNn = projN / projNLen;

		float sgn = sign(dot(orthoDir, projNn));
		float n = sgn * acos(clamp(dot(projNn, V), -1.0, 1.0));

		float h[2];
		h[0] = -M_HALF_PI;
		h[1] =  M_HALF_PI;

		for (int side = 0; side < 2; side++) {
			float dirSign = (side == 0) ? -1.0 : 1.0;
			float cosHorizon = -1.0;

			for (int s = 1; s <= stepCount; s++) {
				float t = (float(s) - 0.5 + noise) / float(stepCount);
				vec2 offset = omega * dirSign * t * screenRadius * invScreen;
				vec2 sampleUV = frag_tex_coord + offset;

				if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
					break;

				// nothing drawn there (sky, or a world-less stage around its figure): no horizon from this sample; a
				// return here left every pixel within the screen radius of the empty background unoccluded
				float sampleDepth = sampleDepth(sampleUV);
				if (depthIsEmpty(sampleDepth))
					continue;

				vec3 S = reconstructViewPosition(sampleUV, sampleDepth) - P;
				float dist = length(S);
				if (dist < 1e-4)
					continue;

				float falloff = clamp(1.0 - (dist / (radius * 2.0)), 0.0, 1.0);
				float cosH = dot(S / dist, V);
				cosHorizon = max(cosHorizon, mix(-1.0, cosH, falloff));
			}

			h[side] = dirSign * acos(clamp(cosHorizon, -1.0, 1.0));
		}

		h[0] = n + max(h[0] - n, -M_HALF_PI);
		h[1] = n + min(h[1] - n,  M_HALF_PI);

		float sinN = sin(n);
		float visibility =
			0.25 * (-cos(2.0 * h[0] - n) + cos(n) + 2.0 * h[0] * sinN) +
			0.25 * (-cos(2.0 * h[1] - n) + cos(n) + 2.0 * h[1] * sinN);

		visibilitySum += projNLen * visibility;
	}

	float occlusion = clamp(visibilitySum / float(sliceCount), 0.0, 1.0);

	return pow(occlusion, intensity);
}


// character pixels keep only push.character of their AO: the AO is multiplied over the lit image, so stacked hair
// cards would darken each other and their direct light (r_ssao_character)
float characterAO( in float ao )
{
	return sampleDepthSigned( frag_tex_coord ) < 0.0 ? mix( 1.0, ao, push.character ) : ao;
}

// r_charContactShadows' mask (character pass): toward a light, steps through the depth in this shader's eye space;
// blocked when a step lies behind the stored surface by more than a small bias and less than the thickness. The ray
// leans within the light's size (interleaved-gradient noise, blurred afterwards). 1 = lit.
float contactShadowRay( in vec3 P, in vec3 N, in vec4 light, in float seed )
{
	const float len = push.bias;
	const float thick = push.samples;
	const int steps = int( push.depth_falloff + 0.5 );
	const float hairOpacity = push.power;	// r_charHairOpacity (power is SSAO-only)
	// white-noise jitter (the blur after removes it; interleaved-gradient noise left a regular pattern through it)
	const float jitter = rand01( gl_FragCoord.xy * 0.731 + 3.1 + seed );
	const bool point = light.w >= 5.0;
	const float size = point ? light.w - 10.0 : light.w;
	vec3 L = point ? normalize( light.xyz - P ) : normalize( light.xyz );
	// light grazing the surface: little of it to block, and the ray meets the surface's own facets; fade it out
	const float nlFade = smoothstep( 0.02, 0.15, dot( N, L ) );
	if ( nlFade <= 0.0 )
		return 1.0;
	if ( size > 0.0 ) {
		const float a = 6.2831853 * rand01( gl_FragCoord.yx * 1.37 + 11.0 + seed );
		const vec3 t = normalize( cross( L, abs( L.z ) < 0.9 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 1.0, 0.0, 0.0 ) ) );
		const vec3 b = cross( L, t );
		L = normalize( L + ( t * cos( a ) + b * sin( a ) ) * ( tan( 0.5 * size ) * sqrt( rand01( gl_FragCoord.xy * 2.17 + 5.3 + seed ) ) ) );
	}
	const vec3 start = P + N * 0.05;
	float shadow = 0.0;
	for ( int i = 0; i < 32; i++ ) {
		if ( i >= steps )
			break;
		const float t = ( float( i ) + jitter ) / float( steps );
		const vec3 Q = start + L * ( t * len );
		if ( Q.z > -0.1 )
			break;
		const vec2 uv = vec2( Q.x / ( push.projection.x * -Q.z ), Q.y / ( push.projection.y * -Q.z ) ) * 0.5 + 0.5;
		if ( any( lessThan( uv, vec2( 0.0 ) ) ) || any( greaterThan( uv, vec2( 1.0 ) ) ) )
			break;
		// opaque surfaces block fully, hair and lashes by r_charHairOpacity (strands let light through)
		const vec2 sd = sampleDepthLayers( uv );
		const vec2 delta = vec2( -Q.z ) - sd;
		const float fade = 1.0 - t * t;
		if ( sd.x > 0.0 )
			shadow = max( shadow, smoothstep( 0.02, 0.15, delta.x ) * ( 1.0 - smoothstep( thick * 0.5, thick, delta.x ) ) * fade );
		if ( sd.y < 1e8 )
			shadow = max( shadow, hairOpacity * smoothstep( 0.02, 0.15, delta.y ) * ( 1.0 - smoothstep( thick * 0.5, thick, delta.y ) ) * fade );
	}
	return 1.0 - shadow * nlFade;
}

// r_charSelfShadowScreen: the nearest character's own shadow toward the grid light (gen_frag charSelfShadow's march
// over the light's disc, here once per pixel of the opaque depth and blurred with the mask). 1 = lit.
float selfShadowScreen( in vec3 P, in vec3 N, in vec3 L )
{
	const vec4 tile = u_camera.selfShadowTile[0];
	const vec4 scale = u_camera.selfShadowScale[0];
	if ( u_camera.selfShadowParams.x < 0.5 || tile.w <= 0.0 )
		return 1.0;
	const float NL = clamp( dot( N, L ), 0.0, 1.0 );
	const float slopeTexel = u_camera.selfShadowSlopeBias / scale.x;
	const float slopeTan = min( sqrt( max( 1.0 - NL * NL, 0.0 ) ) / max( NL, 0.05 ), 4.0 );
	// this shader's eye space (no projection translation, y down) -> the view's eye space -> world (rigid view)
	const vec2 shift = u_camera.charOccInfo.yz * push.projection.xy;
	const vec3 Q = P + N * ( scale.w * ( 3.0 - 2.0 * NL ) + slopeTexel * ( 1.0 - NL ) );
	const vec3 e = vec3( Q.x - shift.x, -Q.y - shift.y, Q.z );
	const vec3 W = transpose( mat3( u_camera.viewMatrix ) ) * ( e - u_camera.viewMatrix[3].xyz );
	const vec4 c = u_camera.selfShadowMvp[0] * vec4( W, 1.0 );
	if ( any( greaterThan( abs( c.xy ), vec2( 1.0 ) ) ) || c.z <= 0.0 || c.z >= 1.0 )
		return 1.0;
	const vec2 tileMin = tile.xy + vec2( 1e-4 ), tileMax = tile.xy + vec2( tile.z - 1e-4 );
	const vec2 st = tile.xy + ( c.xy * 0.5 + 0.5 ) * tile.z;
	const float dref = c.z - scale.z - slopeTexel * slopeTan / scale.y;
	const int packed = int( u_camera.selfShadowParams.x + 0.5 );
	const int rays = clamp( packed % 32, 1, 16 );
	const int steps = clamp( packed / 32, 2, 12 );
	const float grow = pow( 200.0, 1.0 / float( steps - 1 ) );
	const float rot = 6.2831853 * rand01( gl_FragCoord.xy * 0.913 + 7.7 );
	const float jitter = rand01( gl_FragCoord.yx * 1.193 + 2.9 );
	float occluded = 0.0;
	for ( int d = 0; d < 16; d++ ) {
		if ( d >= rays )
			break;
		// a point on the light's disc (golden-angle spiral, rotated per pixel), lateral uv per unit toward the light
		const float r = sqrt( ( float( d ) + 0.5 ) / float( rays ) );
		const float a = rot + 2.3999632 * float( d );
		const vec2 lean = vec2( cos( a ), sin( a ) ) * ( r * u_camera.selfShadowParams.z );
		float blocked = 0.0;
		for ( int k = 0; k < 12; k++ ) {
			if ( k >= steps )
				break;
			const float t = 0.06 * pow( grow, float( k ) + jitter );
			const vec2 s = clamp( st + lean * ( t * scale.x ), tileMin, tileMax );
			blocked = max( blocked, 1.0 - texture( sun_shadow_texture, vec4( s, 3.0, dref - t / scale.y ) ) );
			if ( blocked > 0.99 )
				break;
		}
		occluded += blocked;
	}
	return 1.0 - ( occluded / float( rays ) ) * tile.w;
}

// r_charContactRays rays per pixel (push.reverse_depth), averaged before the blur
float contactShadow( in vec3 P, in vec3 N, in vec4 light )
{
	const int rays = clamp( int( push.reverse_depth + 0.5 ), 1, 16 );
	float sum = 0.0;
	for ( int r = 0; r < 16; r++ ) {
		if ( r >= rays )
			break;
		sum += contactShadowRay( P, N, light, 17.31 * float( r ) );
	}
	return sum / float( rays );
}

void main() {
	float depth = sampleDepth(frag_tex_coord);

	if ( depthIsEmpty( depth ) ) {
		out_color = vec4(1.0);
		return;
	}

	vec3 P = reconstructViewPosition( frag_tex_coord, depth );
	vec3 dx = dFdx(P);
	vec3 dy = dFdy(P);
	vec3 N	= normalize(cross(dx, dy));

	// Orient normal toward the camera.
	if ( dot(N, -P) < 0.0 )
		N = -N;

	// r_charAO / r_charContactShadows (vk_charocc.cpp RB_CharOcclusion): GTAO in r, the lights' contact shadows in g, b, a
	if ( push.character > 1.5 ) {
		// slices bit 6 (r_charOccMask): only within a texel of a marked (model) pixel, which the lit pass's bilinear
		// read and the blur reach; elsewhere clear
		if ( ( push.slices & 64u ) != 0u ) {
			bool near = false;
			for ( int y = -1; y <= 1 && !near; y++ )
				for ( int x = -1; x <= 1 && !near; x++ )
					near = sampleDepthSigned( frag_tex_coord + vec2( float( x ) * push.inv_width, float( y ) * push.inv_height ) ) < 0.0;
			if ( !near ) {
				out_color = vec4( 1.0 );
				return;
			}
		}
		const int count = int( push.character - 2.0 + 0.5 );
		vec4 m = vec4( computeGTAO( P, N ), 1.0, 1.0, 1.0 );
		for ( int i = 0; i < 3; i++ )
			if ( i < count )
				m[1 + i] = contactShadow( P, N, push.lights[i] );
		const int selfChannel = int( ( push.slices >> 4u ) & 3u );
		if ( selfChannel >= 1 && selfChannel <= 3 )
			m[selfChannel] = min( m[selfChannel], selfShadowScreen( P, N, normalize( push.lights[selfChannel - 1].xyz ) ) );
		out_color = m;
		return;
	}

	// GTAO
	if ( push.mode == 2u ) {
		float ao = characterAO( computeGTAO( P, N ) );
		out_color = vec4(ao, ao, ao, 1.0);
		return;
	}

	// SSAO
	if ( depthFalloffReject( depth ) ) {
		out_color = vec4(1.0);
		return;
	}

	float ao = characterAO( computeSSAO( P, N ) );
	out_color = vec4(ao, ao, ao, 1.0);
}