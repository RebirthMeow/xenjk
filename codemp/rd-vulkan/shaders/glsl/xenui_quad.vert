#version 450

// xenui: a full-screen triangle for compositing and filters; the fragment shader samples by gl_FragCoord

void main() {
	vec2 p = vec2( ( gl_VertexIndex << 1 ) & 2, gl_VertexIndex & 2 );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
