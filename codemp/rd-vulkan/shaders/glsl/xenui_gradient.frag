#version 450

// xenui: RmlUi's gradient shaders (linear, radial, conic, each repeating or not) on geometry whose texture coordinates
// are positions in the element's box. Stops are premultiplied; the colour between two stops is mixed linearly.

#define LINEAR		0
#define RADIAL		1
#define CONIC		2
#define MAX_STOPS	16
#define PI			3.14159265

layout(set = 1, binding = 0) uniform Gradient {
	int		func;
	int		repeating;
	int		num_stops;
	int		pad;
	vec2	p;			// linear: start; radial and conic: centre
	vec2	v;			// linear: end - start; radial: 1 / radius; conic: (cos, sin) of the start angle
	vec4	stop_colors[MAX_STOPS];
	vec4	stop_positions[MAX_STOPS / 4];
} g;

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_texcoord;

layout(location = 0) out vec4 out_color;

float stop_position( int i ) {
	return g.stop_positions[i / 4][i % 4];
}

vec4 stop_mix( float t ) {
	vec4 color = g.stop_colors[0];
	for ( int i = 1; i < g.num_stops; i++ ) {
		float t0 = stop_position( i - 1 ), t1 = stop_position( i );
		float f = t1 > t0 ? clamp( ( t - t0 ) / ( t1 - t0 ), 0.0, 1.0 ) : step( t1, t );
		color = mix( color, g.stop_colors[i], f );
	}
	return color;
}

void main() {
	float t = 0.0;
	vec2 d = frag_texcoord - g.p;

	if ( g.func == LINEAR ) {
		t = dot( g.v, d ) / dot( g.v, g.v );
	} else if ( g.func == RADIAL ) {
		t = length( g.v * d );
	} else {
		mat2 r = mat2( g.v.x, -g.v.y, g.v.y, g.v.x );
		vec2 rd = r * d;
		t = 0.5 + atan( -rd.x, rd.y ) / ( 2.0 * PI );
	}

	if ( g.repeating != 0 && g.num_stops > 1 ) {
		float t0 = stop_position( 0 ), t1 = stop_position( g.num_stops - 1 );
		if ( t1 > t0 )	// every stop at one position: no period to repeat
			t = t0 + mod( t - t0, t1 - t0 );
	}

	out_color = frag_color * stop_mix( t );
}
