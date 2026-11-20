#version 450

// xenui (tr_xenui.cpp): RmlUi geometry in window pixels; the push transform carries RmlUi's transform and the
// window-to-clip projection, applied after the draw's translation.

layout(push_constant) uniform Push {
	mat4 transform;
	vec2 translation;
} pc;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_color;		// premultiplied, unorm
layout(location = 2) in vec2 in_texcoord;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec2 frag_texcoord;

void main() {
	frag_color = in_color;
	frag_texcoord = in_texcoord;
	gl_Position = pc.transform * vec4( in_position + pc.translation, 0.0, 1.0 );
}
