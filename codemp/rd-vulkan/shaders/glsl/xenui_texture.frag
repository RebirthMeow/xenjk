#version 450

// xenui: textured geometry; textures and vertex colours are premultiplied

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_texcoord;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = texture( tex, frag_texcoord ) * frag_color;
}
