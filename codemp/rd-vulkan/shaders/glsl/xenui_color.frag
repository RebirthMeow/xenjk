#version 450

// xenui: untextured geometry (premultiplied vertex colour)

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_texcoord;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = frag_color;
}
