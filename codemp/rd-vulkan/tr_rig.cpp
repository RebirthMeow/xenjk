/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// The portrait and stage light rig: r_portraitRig's studio rig as dynamic lights in a world scene, and the same rig
// about a menu stage's figure (RDF_STAGE). RE_RenderScene calls R_AddSceneRig before RE_BeginScene;
// R_SetupEntityLighting takes the stage key as the figure's directed light (R_StageKeyLight).

#include "tr_local.h"

extern int r_numdlights;	// tr_scene.cpp: the frame's dynamic lights so far

/*
=====================
R_AddPortraitRig

r_portraitRig: the Cycles studio rig the reference character renders were made under, as four dynamic
lights around the subject r_portraitRigDist ahead of the camera, so characters can be compared with those renders
under the same light. Layout in metres from the subject's eyes (Tifa's 1.72 m: D = 2.58 m), the subject facing the
camera; level = the light's irradiance / pi at the subject (the display value of a white Lambert surface facing it,
linear): key 38 deg off the camera axis, 0.72 m up, 0.83; fill -50 deg, -0.14 m, 0.53; rim behind (205 deg), 0.89 m
up, 0.73; top 2.19 m straight up, 0.41. Gamma-space lighting (r_charLinear 0) gets each level ^ (1 / 2.2), so one
light alone shows the same display value.
=====================
*/
static const struct { float yaw, dist, up, level, color[3], size; } s_rig[4] = {
	{   38.0f, 2.58f,  0.72f, 0.83f, { 1.00f, 0.95f, 0.90f }, 1.2f },	// key (disk diameter, m)
	{  -50.0f, 2.84f, -0.14f, 0.53f, { 0.92f, 0.94f, 1.00f }, 2.4f },	// fill
	{  205.0f, 2.32f,  0.89f, 0.73f, { 0.95f, 0.97f, 1.00f }, 0.8f },	// rim
	{    0.0f, 0.00f,  2.19f, 0.41f, { 1.00f, 1.00f, 1.00f }, 2.5f },	// top
};

static void R_AddRig( const vec3_t target, const vec3_t towardCamera, qboolean linear, qboolean skipKey );

// the subject's horizontal facing (toward the camera) and the rig's right; qfalse when the camera is straight above
static qboolean R_RigFrame( const vec3_t towardCamera, vec3_t facing, vec3_t right ) {
	VectorSet( facing, towardCamera[0], towardCamera[1], 0.0f );
	if ( VectorNormalize( facing ) < 0.01f )
		return qfalse;
	VectorSet( right, -facing[1], facing[0], 0.0f );
	VectorScale( right, r_portraitRigSide->value < 0.0f ? -1.0f : 1.0f, right );
	return qtrue;
}

/*
=====================
R_StageKeyLight

RDF_STAGE: the rig's key as the figure's directed light (R_SetupEntityLighting), so the character self-shadow, which
shadows that light, shadows the key: dir toward the light, its linear level x colour (irradiance / pi at the subject),
and its disk's angular diameter in degrees (the shadow's softness).
=====================
*/
qboolean R_StageKeyLight( const vec3_t towardCamera, vec3_t dir, vec3_t color, float *sizeDeg ) {
	vec3_t	facing, right;
	const float yaw = DEG2RAD( s_rig[0].yaw );
	const float d = sqrtf( s_rig[0].dist * s_rig[0].dist + s_rig[0].up * s_rig[0].up );

	if ( !R_RigFrame( towardCamera, facing, right ) )
		return qfalse;
	VectorScale( facing, cosf( yaw ) * s_rig[0].dist, dir );
	VectorMA( dir, sinf( yaw ) * s_rig[0].dist, right, dir );
	dir[2] += s_rig[0].up;
	VectorNormalize( dir );
	VectorScale( s_rig[0].color, s_rig[0].level * r_stageRigLevel->value, color );
	*sizeDeg = RAD2DEG( 2.0f * atanf( 0.5f * s_rig[0].size / d ) );
	return qtrue;
}

static void R_AddPortraitRig( const refdef_t *fd, int firstEntity, int endEntity ) {
	vec3_t	target, facing;
	int		i;

	VectorMA( fd->vieworg, r_portraitRigDist->value, fd->viewaxis[0], target );
	VectorSet( facing, -fd->viewaxis[0][0], -fd->viewaxis[0][1], 0.0f );	// subject toward the camera
	R_AddRig( target, facing, r_charLinear->integer ? qtrue : qfalse, qfalse );
	if ( r_portraitRig->integer == 2 ) {
		ri.Printf( PRINT_ALL, "portraitRig target %.0f %.0f %.0f\n", target[0], target[1], target[2] );
		for ( i = firstEntity; i < endEntity; i++ )
			if ( backEndData->entities[i].e.ghoul2 )
				ri.Printf( PRINT_ALL, "portraitRig ghoul2 entity at %.0f %.0f %.0f\n", backEndData->entities[i].e.origin[0],
					backEndData->entities[i].e.origin[1], backEndData->entities[i].e.origin[2] );
	}
}

/*
=====================
R_AddStageRig

RDF_STAGE (a menu's character stage): the same rig about the scene's first Ghoul2 model, centred on its lighting origin
(the stage sets it at the eyes, where the rig is measured from), facing the camera horizontally. The camera's distance
does not matter: the rig keeps its metre layout about the subject whatever the framing.
=====================
*/
static void R_AddStageRig( const refdef_t *fd, int firstEntity, int endEntity ) {
	int		i;

	for ( i = firstEntity; i < endEntity; i++ ) {
		const refEntity_t *e = &backEndData->entities[i].e;
		vec3_t	facing;
		if ( !e->ghoul2 || e->reType != RT_MODEL )
			continue;
		VectorSet( facing, fd->vieworg[0] - e->origin[0], fd->vieworg[1] - e->origin[1], 0.0f );
		// stages shade linear; the key is the figure's directed light (R_StageKeyLight), so its self-shadow falls
		R_AddRig( ( e->renderfx & RF_LIGHTING_ORIGIN ) ? e->lightingOrigin : e->origin, facing, qtrue, qtrue );
		return;
	}
}

static void R_AddRig( const vec3_t target, const vec3_t towardCamera, qboolean linear, qboolean skipKey ) {
#define rig s_rig
	vec3_t	facing, right;
	const float	units = r_portraitRigUnits->value;
	int		i;

	if ( !R_RigFrame( towardCamera, facing, right ) )
		return;

	for ( i = skipKey ? 1 : 0; i < 4 && r_numdlights < (int)ARRAY_LEN( backEndData->dlights ); i++ ) {
		dlight_t	*dl = &backEndData->dlights[r_numdlights++];
		Com_Memset( dl, 0, sizeof( *dl ) );
		const float	yaw = DEG2RAD( rig[i].yaw );
		vec3_t		dir;
		float		d, att, level;

		VectorScale( facing, cosf( yaw ), dir );
		VectorMA( dir, sinf( yaw ), right, dir );
		VectorMA( target, rig[i].dist * units, dir, dl->origin );
		dl->origin[2] += rig[i].up * units;
		d = sqrtf( rig[i].dist * rig[i].dist + rig[i].up * rig[i].up ) * units;
		// the GPU light: colour x radius / 25 x attenuation (0.5 r^2 / d^2 - 0.5, clamped to 0..1, zero at r); radius
		// sqrt(3) d puts the subject at attenuation 1 (falling 1.5x as fast as inverse-square across the head)
		dl->radius = 1.7320508f * d;
		dl->size = 2.0f * atanf( 0.5f * rig[i].size * units / d );	// contact shadows' softness
		att = 1.0f;
		level = rig[i].level * r_portraitRigLevel->value;
		if ( skipKey )	// a stage: its own level and fill, rim and top shares (r_stageRig*) instead of the world rig's
			level = rig[i].level * r_stageRigLevel->value * ( i == 1 ? r_stageRigFill : i == 2 ? r_stageRigRim : r_stageRigTop )->value;
		if ( !linear )
			level = powf( level, 1.0f / 2.2f );
		VectorScale( rig[i].color, level * 25.0f / ( dl->radius * att ), dl->color );
		dl->additive = 0;
		dl->linear = qfalse;
		if ( r_portraitRig->integer == 2 )
			ri.Printf( PRINT_ALL, "portraitRig %d: origin %.0f %.0f %.0f radius %.0f colour %.4f %.4f %.4f (d %.0f)\n", i,
				dl->origin[0], dl->origin[1], dl->origin[2], dl->radius, dl->color[0], dl->color[1], dl->color[2], d );
	}
#undef rig
}

/*
=====================
R_AddSceneRig

RE_RenderScene, before RE_BeginScene: the rig's dynamic lights for this scene (backEndData->entities firstEntity up to
endEntity are its entities): r_portraitRig's in a world scene, the stage rig in an RDF_STAGE scene
=====================
*/
void R_AddSceneRig( const refdef_t *fd, int firstEntity, int endEntity ) {
	if ( r_portraitRig->integer && !( fd->rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) )
		R_AddPortraitRig( fd, firstEntity, endEntity );
	else if ( ( fd->rdflags & ( RDF_NOWORLDMODEL | RDF_STAGE ) ) == ( RDF_NOWORLDMODEL | RDF_STAGE ) )
		R_AddStageRig( fd, firstEntity, endEntity );
}
