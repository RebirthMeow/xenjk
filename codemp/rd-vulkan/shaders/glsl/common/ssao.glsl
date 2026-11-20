//
// math
//
float halton2( in int i ) 
{
	float r = 0.0;
	float f = 0.5;
	int n	= i;

	while ( n > 0 ) {
		r += f * float( n & 1 );
		n /= 2;
		f *= 0.5;
	}
	return r;
}

float halton3( in int i ) 
{
	float r	= 0.0;
	float f	= 1.0 / 3.0;
	int n	= i;

	while ( n > 0 ) {
		r += f * float( n % 3 );
		n /= 3;
		f *= 1.0 / 3.0;
	}
	return r;
}

float rand01( vec2 co ) 
{
	return fract( sin( dot( co, vec2( 12.9898, 78.233 ) ) ) * 43758.5453 );
}

float getScreenRadius( in float radius, in float viewZ )
{
	return radius / push.projection.x * 0.5 / (-viewZ) / push.inv_width;
}


//
// sampling
//
#ifdef IS_BLUR_PASS
// all four channels: r AO, g b a r_charContactShadows' masks (character pass)
vec4 sampleSSAO( in vec2 uv ) 
{
#ifdef USE_VK_IMGUI
	return textureLod(ssao_texture, uv * push.texture_scale, 0.0);
#else
	return textureLod(ssao_texture, uv, 0.0);
#endif
}
#endif

// signed: depth extract writes character pixels (r_ssao_character) negative
float sampleDepthSigned( in vec2 uv ) 
{
#ifdef USE_VK_IMGUI
	return textureLod(depth_texture, uv * push.texture_scale, 0.0).r;
#else
	return textureLod(depth_texture, uv, 0.0).r;
#endif
}

// the nearest surface: the depth, or r_charOccHair's hair and lashes (g) in front of it; 0 = empty
float sampleDepth( in vec2 uv )
{
#ifdef USE_VK_IMGUI
	const vec2 d = textureLod( depth_texture, uv * push.texture_scale, 0.0 ).rg;
#else
	const vec2 d = textureLod( depth_texture, uv, 0.0 ).rg;
#endif
	return d.x == 0.0 ? ( d.y < 1e8 ? d.y : 0.0 ) : min( abs( d.x ), d.y );
}

// g holds every strand (no depth test when it was drawn): strands behind the opaque surface are hidden, not there
float visibleHair( in vec2 d )
{
	return ( d.x == 0.0 || d.y < abs( d.x ) - 0.05 ) ? d.y : 1e9;
}

// opaque depth (r) and hair depth (g, 1e9 = none) apart
vec2 sampleDepthLayers( in vec2 uv )
{
#ifdef USE_VK_IMGUI
	const vec2 d = textureLod( depth_texture, uv * push.texture_scale, 0.0 ).rg;
#else
	const vec2 d = textureLod( depth_texture, uv, 0.0 ).rg;
#endif
	return vec2( abs( d.x ), visibleHair( d ) );
}

//
// depth
//
bool depthIsEmpty( in float d ) 
{
	return d <= 0.0;
}

vec3 reconstructViewPosition( in vec2 uv, in float linear_depth )
{
    vec2 ndc = uv * 2.0 - 1.0;

    float z = -linear_depth;

    return vec3(
        ndc.x * linear_depth * push.projection.x,
        ndc.y * linear_depth * push.projection.y,
        z
    );
}
