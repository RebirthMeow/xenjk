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

/*
=========================================================================
Concussion-alt trace visualization

cg_concTraceVisual 1: live reconstruction at cg_concTraceDistance
cg_concTraceVisual 2: hold the most recent real server conc-alt event
cg_concTraceVisual 3: prefer the event hold, otherwise show live
=========================================================================
*/

#include "cg_local.h"

#define CTV_EVENT_HOLD_MSEC 8000
#define CTV_MIN_DISTANCE 64.0f
#define CTV_MAX_DISTANCE 2048.0f

typedef struct concTraceDisplay_s {
	qboolean valid;
	qboolean serverEvent;
	int captureTime;
	float distance;
	vec3_t forward;
	vec3_t right;
	vec3_t viewUp;
	vec3_t referenceStart;
	vec3_t actualStart;
	vec3_t referenceEnd;
	vec3_t actualEnd;
} concTraceDisplay_t;

static concTraceDisplay_t ctv_eventDisplay;
static concTraceDisplay_t ctv_frameDisplay;


static float CTV_Clamp( float value, float minimum, float maximum )
{
	if ( value < minimum ) {
		return minimum;
	}
	if ( value > maximum ) {
		return maximum;
	}
	return value;
}

static void CTV_BuildAxes( const vec3_t forward, vec3_t right, vec3_t viewUp )
{
	vec3_t angles;

	vectoangles( forward, angles );
	AngleVectors( angles, NULL, right, viewUp );
}

static void CTV_AddLine( const vec3_t start, const vec3_t end, float width,
	byte red, byte green, byte blue, byte alpha )
{
	refEntity_t line;

	memset( &line, 0, sizeof( line ) );
	line.reType = RT_LINE;
	line.renderfx = RF_NODEPTH;
	VectorCopy( start, line.origin );
	VectorCopy( end, line.oldorigin );
	line.radius = width;
	line.customShader = cgs.media.whiteShader;
	line.shaderRGBA[0] = red;
	line.shaderRGBA[1] = green;
	line.shaderRGBA[2] = blue;
	line.shaderRGBA[3] = alpha;
	trap->R_AddRefEntityToScene( &line );
}

static void CTV_AddSprite( const vec3_t origin, float radius,
	byte red, byte green, byte blue, byte alpha )
{
	refEntity_t sprite;

	memset( &sprite, 0, sizeof( sprite ) );
	sprite.reType = RT_SPRITE;
	sprite.renderfx = RF_NODEPTH;
	VectorCopy( origin, sprite.origin );
	sprite.radius = radius;
	sprite.customShader = cgs.media.whiteShader;
	sprite.shaderRGBA[0] = red;
	sprite.shaderRGBA[1] = green;
	sprite.shaderRGBA[2] = blue;
	sprite.shaderRGBA[3] = alpha;
	trap->R_AddRefEntityToScene( &sprite );
}

static void CTV_AddEndCross( const concTraceDisplay_t *display,
	const vec3_t point, byte red, byte green, byte blue )
{
	vec3_t start, end;

	VectorMA( point, -3.0f, display->right, start );
	VectorMA( point, 3.0f, display->right, end );
	CTV_AddLine( start, end, 0.55f, red, green, blue, 255 );
	VectorMA( point, -3.0f, display->viewUp, start );
	VectorMA( point, 3.0f, display->viewUp, end );
	CTV_AddLine( start, end, 0.55f, red, green, blue, 255 );
}

static qboolean CTV_BuildLiveDisplay( concTraceDisplay_t *display )
{
	vec3_t eye, toPlane;
	float alongActual;
	float distance;

	if ( !cg.snap ) {
		return qfalse;
	}

	memset( display, 0, sizeof( *display ) );
	distance = CTV_Clamp( cg_concTraceDistance.value,
		CTV_MIN_DISTANCE, CTV_MAX_DISTANCE );
	AngleVectors( cg.predictedPlayerState.viewangles,
		display->forward, display->right, display->viewUp );
	VectorCopy( cg.predictedPlayerState.origin, eye );
	eye[2] += cg.predictedPlayerState.viewheight;

	/* Match WP_MuzzlePoint[WP_CONCUSSION] and CalcMuzzlePoint. */
	VectorMA( eye, 12.0f, display->forward, display->actualStart );
	VectorMA( display->actualStart, 6.0f, display->right, display->actualStart );
	display->actualStart[2] -= 6.0f;
	SnapVector( display->actualStart );

	VectorCopy( eye, display->referenceStart );
	VectorMA( eye, distance, display->forward, display->referenceEnd );
	VectorSubtract( display->referenceEnd, display->actualStart, toPlane );
	alongActual = DotProduct( toPlane, display->forward );
	VectorMA( display->actualStart, alongActual,
		display->forward, display->actualEnd );

	display->valid = qtrue;
	display->serverEvent = qfalse;
	display->captureTime = cg.time;
	display->distance = distance;
	return qtrue;
}

static qboolean CTV_EventIsFresh( void )
{
	return ctv_eventDisplay.valid &&
		cg.time - ctv_eventDisplay.captureTime <= CTV_EVENT_HOLD_MSEC;
}

void CG_ConcTraceVisual_OnEntityEvent( centity_t *cent, int event )
{
	const entityState_t *state;
	vec3_t displacement;
	float along;

	if ( !cg_concTraceVisual.integer || event != EV_CONC_ALT_IMPACT || !cent ) {
		return;
	}

	state = &cent->currentState;
	memset( &ctv_eventDisplay, 0, sizeof( ctv_eventDisplay ) );
	VectorCopy( state->angles2, ctv_eventDisplay.forward );
	if ( VectorNormalize( ctv_eventDisplay.forward ) == 0.0f ) {
		return;
	}
	CTV_BuildAxes( ctv_eventDisplay.forward,
		ctv_eventDisplay.right, ctv_eventDisplay.viewUp );
	VectorCopy( state->origin2, ctv_eventDisplay.actualStart );
	VectorCopy( state->pos.trBase, ctv_eventDisplay.actualEnd );

	/* Remove only the lateral/vertical muzzle offsets.  The resulting cyan
	   line is the eye/crosshair axis at the same forward station. */
	VectorCopy( ctv_eventDisplay.actualStart, ctv_eventDisplay.referenceStart );
	VectorMA( ctv_eventDisplay.referenceStart, -6.0f,
		ctv_eventDisplay.right, ctv_eventDisplay.referenceStart );
	ctv_eventDisplay.referenceStart[2] += 6.0f;
	VectorSubtract( ctv_eventDisplay.actualEnd,
		ctv_eventDisplay.actualStart, displacement );
	along = DotProduct( displacement, ctv_eventDisplay.forward );
	VectorMA( ctv_eventDisplay.referenceStart, along,
		ctv_eventDisplay.forward, ctv_eventDisplay.referenceEnd );

	ctv_eventDisplay.valid = qtrue;
	ctv_eventDisplay.serverEvent = qtrue;
	ctv_eventDisplay.captureTime = cg.time;
	ctv_eventDisplay.distance = along;
}

static const concTraceDisplay_t *CTV_SelectDisplay( void )
{
	int mode = cg_concTraceVisual.integer;

	if ( mode == 2 ) {
		return CTV_EventIsFresh() ? &ctv_eventDisplay : NULL;
	}
	if ( mode >= 3 && CTV_EventIsFresh() ) {
		return &ctv_eventDisplay;
	}
	if ( mode >= 1 && CTV_BuildLiveDisplay( &ctv_frameDisplay ) ) {
		return &ctv_frameDisplay;
	}
	return NULL;
}

void CG_ConcTraceVisual_AddWorld( void )
{
	const concTraceDisplay_t *display;
	vec3_t referenceLineStart, actualLineStart, corner, toStartPlane;
	float actualStartAdvance, visualStartDistance;

	if ( !cg_concTraceVisual.integer ) {
		return;
	}
	display = CTV_SelectDisplay();
	if ( !display ) {
		ctv_frameDisplay.valid = qfalse;
		return;
	}
	if ( display != &ctv_frameDisplay ) {
		ctv_frameDisplay = *display;	/* the live display is built in place */
	}

	/* Clip both debug lines to the same plane ahead of the eye.  Starting an
	   RT_LINE at the muzzle makes a one-unit line fill the foreground through
	   ordinary perspective, obscuring the useful endpoint comparison. */
	visualStartDistance = display->distance - 128.0f;
	if ( visualStartDistance < 64.0f ) {
		visualStartDistance = 64.0f;
	}
	VectorMA( display->referenceStart, visualStartDistance,
		display->forward, referenceLineStart );
	VectorSubtract( referenceLineStart, display->actualStart, toStartPlane );
	actualStartAdvance = DotProduct( toStartPlane, display->forward );
	VectorMA( display->actualStart, actualStartAdvance,
		display->forward, actualLineStart );

	CTV_AddLine( referenceLineStart, display->referenceEnd,
		0.45f, 20, 220, 255, 230 );
	CTV_AddLine( actualLineStart, display->actualEnd,
		0.55f, 255, 90, 20, 245 );

	/* Draw the exact +6 right, -6 world-Z construction at the comparison
	   plane, rather than a generic diagonal connector. */
	VectorMA( display->referenceEnd, 6.0f, display->right, corner );
	CTV_AddLine( display->referenceEnd, corner,
		0.45f, 230, 230, 230, 210 );
	CTV_AddLine( corner, display->actualEnd,
		0.45f, 230, 230, 230, 210 );

	CTV_AddEndCross( display, display->referenceEnd, 20, 220, 255 );
	CTV_AddEndCross( display, display->actualEnd, 255, 90, 20 );
	CTV_AddSprite( display->actualEnd, 1.0f, 255, 90, 20, 190 );
}

static void CTV_DrawMarker( float x, float y, float halfSize, const vec4_t color )
{
	CG_FillRect( x - halfSize, y - 1.0f, halfSize * 2.0f, 2.0f, color );
	CG_FillRect( x - 1.0f, y - halfSize, 2.0f, halfSize * 2.0f, color );
}

static void CTV_DrawBox( float x, float y, float halfSize, const vec4_t color )
{
	CG_FillRect( x - halfSize, y - halfSize, halfSize * 2.0f, 1.0f, color );
	CG_FillRect( x - halfSize, y + halfSize - 1.0f, halfSize * 2.0f, 1.0f, color );
	CG_FillRect( x - halfSize, y - halfSize, 1.0f, halfSize * 2.0f, color );
	CG_FillRect( x + halfSize - 1.0f, y - halfSize, 1.0f, halfSize * 2.0f, color );
}

void CG_ConcTraceVisual_Draw( void )
{
	concTraceDisplay_t *display = &ctv_frameDisplay;
	vec4_t panel = { 0.015f, 0.02f, 0.03f, 0.86f };
	vec4_t cyan = { 0.08f, 0.86f, 1.0f, 1.0f };
	vec4_t orange = { 1.0f, 0.35f, 0.08f, 1.0f };
	vec4_t white = { 0.92f, 0.92f, 0.92f, 1.0f };
	float referenceX, referenceY, actualX, actualY;
	float dxVirtual, dyVirtual, dxPixels, dyPixels, diagonalPixels;
	float radiusPointX, radiusPointY, radiusPixelsVirtual;
	vec3_t radiusPoint;
	const char *modeName;

	if ( !cg_concTraceVisual.integer ) {
		return;
	}
	if ( !display->valid ) {
		CG_FillRect( 14, 72, 360, 34, panel );
		CG_DrawStringExt( 22, 80,
			"ALT CONC TRACE LAB: waiting for a server shot event",
			white, qtrue, qtrue, 7, 10, 0 );
		return;
	}
	if ( !CG_WorldCoordToScreenCoordFloat( display->referenceEnd,
		&referenceX, &referenceY ) ||
		!CG_WorldCoordToScreenCoordFloat( display->actualEnd,
		&actualX, &actualY ) ) {
		return;
	}

	dxVirtual = actualX - referenceX;
	dyVirtual = actualY - referenceY;
	dxPixels = dxVirtual * cgs.glconfig.vidWidth / 640.0f;
	dyPixels = dyVirtual * cgs.glconfig.vidHeight / 480.0f;
	diagonalPixels = sqrtf( dxPixels * dxPixels + dyPixels * dyPixels );
	modeName = display->serverEvent ? "FROZEN REAL SERVER EVENT" : "LIVE RECONSTRUCTION";

	CG_FillRect( 14, 68, 455, 82, panel );
	CG_DrawStringExt( 22, 76,
		va( "ALT CONC TRACE LAB - %s", modeName ),
		white, qtrue, qtrue, 7, 10, 0 );
	CG_DrawStringExt( 22, 90,
		"CYAN +  crosshair / eye axis", cyan, qtrue, qtrue, 7, 10, 0 );
	CG_DrawStringExt( 22, 103,
		"ORANGE +  actual MP trace center; orange box = +/-1", orange,
		qtrue, qtrue, 7, 10, 0 );
	CG_DrawStringExt( 22, 116,
		"Server construction: +6 RIGHT, -6 WORLD Z (parallel; no spread)",
		white, qtrue, qtrue, 7, 10, 0 );
	CG_DrawStringExt( 22, 129,
		va( "Range %.0fu | screen gap: %+.1f px X, %+.1f px Y (%.1f px diagonal)",
			display->distance, dxPixels, dyPixels, diagonalPixels ),
		white, qtrue, qtrue, 7, 10, 0 );

	/* Make the L-shaped offset leg readable in 2D even when the 3D lines run
	   across similarly colored map geometry. */
	CG_FillRect( Q_min( referenceX, actualX ), referenceY - 0.5f,
		fabsf( dxVirtual ), 1.0f, white );
	CG_FillRect( actualX - 0.5f, Q_min( referenceY, actualY ),
		1.0f, fabsf( dyVirtual ), white );
	CTV_DrawMarker( referenceX, referenceY, 7.0f, cyan );
	CTV_DrawMarker( actualX, actualY, 7.0f, orange );

	VectorMA( display->actualEnd, 1.0f, display->right, radiusPoint );
	if ( CG_WorldCoordToScreenCoordFloat( radiusPoint,
		&radiusPointX, &radiusPointY ) ) {
		radiusPixelsVirtual = fabsf( radiusPointX - actualX );
		if ( radiusPixelsVirtual < 1.0f ) {
			radiusPixelsVirtual = 1.0f;
		}
		CTV_DrawBox( actualX, actualY, radiusPixelsVirtual, orange );
	}
}
