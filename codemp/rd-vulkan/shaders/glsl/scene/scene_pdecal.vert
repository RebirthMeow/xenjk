#version 450
// Render scenes (vk_scene.cpp, Scene_DrawPDecals): CP2077's projected decals (r_scenePDecals, maps/<map>.xdcl), a box a
// record into the DBuffer (scene_pdecal.frag). The instance is the frame's visible list's entry (records in the file's
// order: orderNo, then material); 36 vertices a box from its map's inverse (scene space)

// the records: 5 vec4 each, rows 0-2 the map from scene space to the box's [-1, 1]^3, then (material, alpha, hide
// distance, flags) and (normal threshold, orderNo, roughness scale, colour alpha); integers as their bits
layout(std430, set = 0, binding = 1) readonly buffer Records { vec4 recs[]; };
layout(std430, set = 0, binding = 2) readonly buffer Visible { uint visible[]; };
layout(push_constant) uniform Push {
	mat4 viewProj;
	vec4 eye;		// w: the farthest any decal shows (units: r_scenePDecalDistance)
	uvec4 info;		// x: the material, y: the normal test, z: 1 to take -Z, w: unused
} pc;

layout(location = 0) out vec3 vPos;
layout(location = 1) flat out uint vRec;
layout(location = 2) flat out float vFade;

const vec3 corner[8] = vec3[8]( vec3( -1, -1, -1 ), vec3( 1, -1, -1 ), vec3( -1, 1, -1 ), vec3( 1, 1, -1 ),
	vec3( -1, -1, 1 ), vec3( 1, -1, 1 ), vec3( -1, 1, 1 ), vec3( 1, 1, 1 ) );
const int faces[36] = int[36]( 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 );
const vec3 faceNormal[6] = vec3[6]( vec3( 0, 0, -1 ), vec3( 0, 0, 1 ), vec3( 0, -1, 0 ), vec3( 0, 1, 0 ), vec3( -1, 0, 0 ), vec3( 1, 0, 0 ) );

void main()
{
	const uint r = visible[gl_InstanceIndex];
	vRec = r;
	const vec4 r0 = recs[r * 5u], r1 = recs[r * 5u + 1u], r2 = recs[r * 5u + 2u];
	const mat3 M = transpose( mat3( r0.xyz, r1.xyz, r2.xyz ) ), Mi = inverse( M );
	const vec3 t = vec3( r0.w, r1.w, r2.w );
	// only the faces turned away from the eye (each pixel the box covers once): this face's outward normal in scene space
	// (normals by the map's transpose) against the eye at its centre; a face turned toward it collapses to a point
	const vec3 nb = faceNormal[gl_VertexIndex / 6];
	const vec3 centre = Mi * ( nb - t );
	if ( dot( transpose( M ) * nb, centre - pc.eye.xyz ) < 0.0 ) {
		vPos = centre;
		vRec = r;
		vFade = 0.0;
		gl_Position = vec4( 0.0, 0.0, 2.0, 1.0 );
		return;
	}
	vPos = Mi * ( corner[faces[gl_VertexIndex]] - t );
	// out by the last 15% of its hide distance (capped at the farthest any decal shows)
	const float hide = recs[r * 5u + 3u].z, reach = hide > 0.0 ? min( hide, pc.eye.w ) : pc.eye.w;
	const float d = distance( Mi * -t, pc.eye.xyz );
	vFade = 1.0 - smoothstep( 0.85 * reach, reach, d );
	gl_Position = pc.viewProj * vec4( vPos, 1.0 );
}
