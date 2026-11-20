#version 450
#extension GL_GOOGLE_include_directive : require

#include "global.h"

layout(set = 0, binding = 0) uniform sampler2D ssao_texture;
layout(set = 1, binding = 0) uniform sampler2D depth_texture;

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform SSAOParams {
	vkBlurSSAO_t push;
};

#define IS_BLUR_PASS
#include "common/ssao.glsl"

float bilateralDepthWeight( float centerDepth, float sampleDepth, float sharpness )
{
	float cd	= max( centerDepth, 1e-3 );
	float delta = abs( sampleDepth - centerDepth );
	float rel	= delta / cd;

	return exp2( -rel * rel * max( sharpness, 0.01 ) );
}

// one direction of a separable depth-aware (bilateral) blur, run twice (push.direction)
void main()
{
	int radius		= int( push.depth_radius );
	vec4 centerAo	= sampleSSAO( frag_tex_coord );

	if ( radius <= 0 ) {
		out_color = centerAo;
		return;
	}

	float centerDepth = sampleDepth( frag_tex_coord );
	if ( centerDepth <= 0.0 ) {
		out_color = centerAo;
		return;
	}
	// r_charOccMask: only marked (model) pixels blur, from each other; the ring around them keeps its own value
	const bool marked = push.mask_params.x > 0.5;
	if ( marked && sampleDepthSigned( frag_tex_coord ) >= 0.0 ) {
		out_color = centerAo;
		return;
	}

	float depthSharp	= max( push.depth_sharpness, 1.0 );
	vec2 stepUV			= push.direction * vec2( push.inv_width, push.inv_height );
	float sigma			= float( radius ) * 0.5 + 0.5;
	float falloff		= 1.0 / ( 2.0 * sigma * sigma );
	vec4 sum			= centerAo;
	float weightSum		= 1.0;

	for ( int i = 1; i <= 8; ++i ) {
		if ( i > radius ) {
			break;
		}
		float spatial = exp( -float( i * i ) * falloff );
		for ( int side = -1; side <= 1; side += 2 ) {
			vec2 suv	= frag_tex_coord + stepUV * float( i * side );
			vec4 ao		= sampleSSAO( suv );
			float sd	= sampleDepth( suv );
			float w		= spatial * bilateralDepthWeight( centerDepth, sd, depthSharp ) * float( sd > 0.0 )
				* ( marked ? float( sampleDepthSigned( suv ) < 0.0 ) : 1.0 );
			sum += ao * w;
			weightSum += w;
		}
	}

	out_color = sum / weightSum;
}
