#version 450
// Render scenes (vk_scene.cpp): one triangle over the frame for the colour grade (scene_grade.frag)

void main()
{
	const vec2 ndc = vec2( float( ( gl_VertexIndex & 1 ) * 4 - 1 ), float( ( gl_VertexIndex >> 1 ) * 4 - 1 ) );
	gl_Position = vec4( ndc, 0.0, 1.0 );
}
