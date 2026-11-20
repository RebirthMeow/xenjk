#version 450
// Render scenes (vk_scene.cpp): one triangle over the view at the far plane, behind everything but the sky; scene.frag
// (mode 6) adds the light the haze scatters toward the eye over the sky. vPos lies on each pixel's ray at the volume's
// far end.

layout(std140, set = 0, binding = 5) uniform Frame {
	vec4 eye;
	vec4 gridOrigin;
	ivec4 gridDims;
	vec4 skyAmbient;
	vec4 groundAmbient;
	vec4 params;
	vec4 params2;
	mat4 sunMvp[3];
	vec4 sunDir;
	vec4 sunCol;
	vec4 sunTexel;
	vec4 fog;
	vec4 fog2;
	vec4 fogFwd;		// w: the volume's near distance (units)
	vec4 fogLeft;		// w: tan(fovX / 2)
	vec4 fogUp;			// w: tan(fovY / 2)
	vec4 fogVol;		// x: log(far / near)
} fr;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec4 vTan;
layout(location = 3) out vec2 vUV;
layout(location = 4) out vec3 vLocal;
layout(location = 5) out vec3 vLocalNrm;
layout(location = 6) flat out uint vInst;
layout(location = 7) flat out vec3 vUpW;

void main()
{
	const vec2 ndc = vec2( float( ( gl_VertexIndex & 1 ) * 4 - 1 ), float( ( gl_VertexIndex >> 1 ) * 4 - 1 ) );
	const vec2 uv = ndc * 0.5 + 0.5;	// Vulkan: y down, as the volume's rows
	const vec3 r = fr.fogFwd.xyz + ( 1.0 - 2.0 * uv.x ) * fr.fogLeft.w * fr.fogLeft.xyz + ( 1.0 - 2.0 * uv.y ) * fr.fogUp.w * fr.fogUp.xyz;
	vPos = fr.eye.xyz + r * ( fr.fogFwd.w * exp( fr.fogVol.x ) * 0.999 );
	vNrm = vec3( 0.0, 0.0, 1.0 );
	vTan = vec4( 1.0, 0.0, 0.0, 1.0 );
	vUV = uv;
	vLocal = vPos;
	vLocalNrm = vNrm;
	vInst = 0u;
	vUpW = vec3( 0.0, 0.0, 1.0 );
	gl_Position = vec4( ndc, 1.0, 1.0 );
}
