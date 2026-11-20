#version 450

// xenui: compositing and RmlUi's filters on premultiplied layers (tr_xenui.cpp). Every target is the window's size,
// so a pixel samples the source at its own position (plus an offset).
//   0 copy:         src * color (opacity: color is (a, a, a, a))
//   1 color matrix: the matrix on the premultiplied colour (RmlUi's basic filters)
//   2 blur:         one direction of a gaussian (a.xy: step in texels, a.z: sigma in steps, a.w: taps each side)
//   3 shadow:       color * the source's alpha at an offset (a.xy, pixels)
//   4 mask:         src * the mask's alpha (set 1)

layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 1, binding = 0) uniform sampler2D mask;

layout(push_constant) uniform Push {
	mat4	m;
	vec4	a;
	vec4	color;
	vec4	limits;		// the scissor region in uv: min xy, max xy
	vec2	inv_size;
	int		mode;
} pc;

layout(location = 0) out vec4 out_color;

void main() {
	vec2 uv = gl_FragCoord.xy * pc.inv_size;

	if ( pc.mode == 0 ) {
		out_color = texture( src, uv ) * pc.color;
	} else if ( pc.mode == 1 ) {	// premultiplied: the fourth column's offsets scale with alpha, as they should
		vec4 r = pc.m * texture( src, uv );
		r.a = clamp( r.a, 0.0, 1.0 );
		out_color = vec4( clamp( r.rgb, vec3( 0.0 ), vec3( r.a ) ), r.a );
	} else if ( pc.mode == 2 ) {	// samples stay inside the scissor region (limits): outside it the target is stale
		vec2 step = pc.a.xy * pc.inv_size;
		float sigma = max( pc.a.z, 0.0001 );
		int taps = int( pc.a.w );
		vec4 sum = texture( src, uv );
		float total = 1.0;
		for ( int i = 1; i <= taps; i++ ) {
			float w = exp( -float( i * i ) / ( 2.0 * sigma * sigma ) );
			sum += ( texture( src, clamp( uv + step * float( i ), pc.limits.xy, pc.limits.zw ) ) +
				texture( src, clamp( uv - step * float( i ), pc.limits.xy, pc.limits.zw ) ) ) * w;
			total += 2.0 * w;
		}
		out_color = sum / total;
	} else if ( pc.mode == 3 ) {
		vec2 at = uv - pc.a.xy * pc.inv_size;
		float inside = all( greaterThanEqual( at, pc.limits.xy ) ) && all( lessThanEqual( at, pc.limits.zw ) ) ? 1.0 : 0.0;
		out_color = pc.color * texture( src, at ).a * inside;
	} else {
		out_color = texture( src, uv ) * texture( mask, uv ).a;
	}
}
