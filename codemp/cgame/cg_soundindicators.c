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

// cg_soundindicators.c -- directional sound indicators: records sound events near the local player and draws a
// ring on the HUD with short arcs and icons for the type and direction of each sound (cg_soundIndicators).
// The icons are stock shaders; two (the use hint and the radar arrow) are registered here.

#include "cg_local.h"

// --- tunables -------------------------------------------------------------

#define SI_MAX_ACTIVE        24      // ring buffer size
#define SI_LIFETIME_MS       1500    // how long a single indicator stays visible
#define SI_FADE_MS           500     // how long the trailing fade lasts
#define SI_MIN_SPACING_MS    60      // throttle: same-type same-source events
#define SI_RING_RADIUS       110.0f  // virtual 640x480 pixels
#define SI_ICON_SIZE         18.0f
#define SI_ARC_THICKNESS     6.0f
#define SI_ARC_HALF_LEN      26.0f   // half-length of each arc segment in px
#define SI_ARC_SEGMENTS      6       // pieces used to fake a curved arc
#define SI_MAX_RANGE         3500.0f // beyond this distance we ignore the sound

// --- state ----------------------------------------------------------------

typedef struct si_event_s {
	int       startTime;      // cg.time when the event was added (0 = slot free)
	int       sourceEntity;   // entity that produced the sound (for throttling)
	vec3_t    origin;         // world position of the sound
	soundIndicatorType_t type;
	qhandle_t customIcon;     // 0 = use default for type; non-0 = override (e.g. picked-up item icon)
} si_event_t;

static si_event_t si_events[SI_MAX_ACTIVE];

// Pre-registered icons we own (registered once at level start so we always
// have a valid handle even if the level shipped without them in cgs.media).
static qhandle_t si_doorUseShader;
static qhandle_t si_moverArrowShader;

// --- public: init ---------------------------------------------------------

void CG_SoundIndicators_Init( void ) {
	memset( si_events, 0, sizeof( si_events ) );
	si_doorUseShader   = trap->R_RegisterShaderNoMip( "gfx/hud/useablehint" );
	si_moverArrowShader = trap->R_RegisterShader( "gfx/menus/radar/arrow_w" );
}

// --- public: record an event ---------------------------------------------

void CG_SoundIndicators_AddWithIcon( const vec3_t origin, soundIndicatorType_t type, int sourceEntity, qhandle_t customIcon ) {
	int i;
	int oldestSlot = 0;
	int oldestTime = 0x7fffffff;
	vec3_t diff;
	float distSq;

	if ( !cg_soundIndicators.integer ) {
		return;
	}

	// Don't show indicators for our own sounds.
	if ( sourceEntity >= 0 && sourceEntity == cg.snap->ps.clientNum ) {
		return;
	}

	// Range cull
	VectorSubtract( origin, cg.refdef.vieworg, diff );
	distSq = VectorLengthSquared( diff );
	if ( distSq > (SI_MAX_RANGE * SI_MAX_RANGE) ) {
		return;
	}

	// Throttle: don't spam the same kind of event from the same source.
	for ( i = 0; i < SI_MAX_ACTIVE; i++ ) {
		if ( si_events[i].startTime &&
			 si_events[i].sourceEntity == sourceEntity &&
			 si_events[i].type == type &&
			 ( cg.time - si_events[i].startTime ) < SI_MIN_SPACING_MS ) {
			// refresh the position rather than adding a new entry
			VectorCopy( origin, si_events[i].origin );
			si_events[i].startTime = cg.time;
			si_events[i].customIcon = customIcon;
			return;
		}
	}

	// Find a free slot, falling back to the oldest one.
	for ( i = 0; i < SI_MAX_ACTIVE; i++ ) {
		if ( si_events[i].startTime == 0 ||
			 ( cg.time - si_events[i].startTime ) >= SI_LIFETIME_MS ) {
			oldestSlot = i;
			break;
		}
		if ( si_events[i].startTime < oldestTime ) {
			oldestTime = si_events[i].startTime;
			oldestSlot = i;
		}
	}

	si_events[oldestSlot].startTime    = cg.time;
	si_events[oldestSlot].sourceEntity = sourceEntity;
	si_events[oldestSlot].type         = type;
	si_events[oldestSlot].customIcon   = customIcon;
	VectorCopy( origin, si_events[oldestSlot].origin );
}

// Default-icon helper: forwards to *WithIcon with customIcon=0 (use type default).
void CG_SoundIndicators_Add( const vec3_t origin, soundIndicatorType_t type, int sourceEntity ) {
	CG_SoundIndicators_AddWithIcon( origin, type, sourceEntity, 0 );
}

// Convenience overload: pull origin off an entity.
void CG_SoundIndicators_AddEnt( int entityNum, soundIndicatorType_t type ) {
	centity_t *cent;

	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return;
	}
	cent = &cg_entities[entityNum];
	CG_SoundIndicators_Add( cent->lerpOrigin, type, entityNum );
}

// --- drawing helpers ------------------------------------------------------

// Pick a colour for a given indicator type. RGBA, 0-1.
static void SI_GetColor( soundIndicatorType_t type, vec4_t out ) {
	switch ( type ) {
		case SI_TYPE_WEAPON:
			out[0] = 1.00f; out[1] = 0.25f; out[2] = 0.20f; // red
			break;
		case SI_TYPE_EXPLOSION:
			out[0] = 1.00f; out[1] = 0.55f; out[2] = 0.10f; // orange
			break;
		case SI_TYPE_FOOTSTEP:
			out[0] = 1.00f; out[1] = 1.00f; out[2] = 1.00f; // white
			break;
		case SI_TYPE_FORCE:
			out[0] = 0.45f; out[1] = 0.75f; out[2] = 1.00f; // light blue
			break;
		case SI_TYPE_USE:
			out[0] = 1.00f; out[1] = 0.85f; out[2] = 0.30f; // yellow
			break;
		case SI_TYPE_MOVER:
			out[0] = 0.65f; out[1] = 0.85f; out[2] = 1.00f; // pale cyan
			break;
		default:
			out[0] = out[1] = out[2] = 1.0f;
			break;
	}
	out[3] = 1.0f;
}

// Pick a registered shader for the icon. Falls back to a sane default if
// the chosen shader hasn't been registered yet on this map.
static qhandle_t SI_GetIcon( soundIndicatorType_t type ) {
	switch ( type ) {
		case SI_TYPE_WEAPON:
		case SI_TYPE_EXPLOSION:
			if ( cgs.media.mAutomapRocketIcon ) return cgs.media.mAutomapRocketIcon;
			break;
		case SI_TYPE_FORCE:
			if ( cgs.media.forceCoronaShader ) return cgs.media.forceCoronaShader;
			break;
		case SI_TYPE_USE:
			if ( si_doorUseShader )         return si_doorUseShader;
			if ( cgs.media.hackerIconShader ) return cgs.media.hackerIconShader;
			break;
		case SI_TYPE_MOVER:
			if ( si_moverArrowShader )       return si_moverArrowShader;
			if ( cgs.media.mAutomapPlayerIcon ) return cgs.media.mAutomapPlayerIcon;
			break;
		case SI_TYPE_FOOTSTEP:
			if ( cgs.media.fsrMarkShader ) return cgs.media.fsrMarkShader;
			if ( cgs.media.balloonShader ) return cgs.media.balloonShader;
			break;
		default:
			if ( cgs.media.balloonShader ) return cgs.media.balloonShader;
			break;
	}
	return cgs.media.whiteShader;
}

// Draw a coloured arc by laying down a few rotated quads along the tangent.
// cx,cy = ring center in 640x480 virtual coords.
// angleRad = angle around ring (0 = straight ahead, +CW)
// radius = ring radius
// alpha = 0..1
static void SI_DrawArc( float cx, float cy, float angleRad, float radius,
						const vec4_t color, float alpha ) {
	int i;
	vec4_t c;
	const float halfLen = SI_ARC_HALF_LEN;
	const float thick   = SI_ARC_THICKNESS;
	const float segLen  = (halfLen * 2.0f) / SI_ARC_SEGMENTS;
	float segAngleStep;

	c[0] = color[0];
	c[1] = color[1];
	c[2] = color[2];
	c[3] = color[3] * alpha;

	// each piece subtends ~ segLen / radius radians along the ring
	segAngleStep = segLen / radius;

	trap->R_SetColor( c );
	for ( i = 0; i < SI_ARC_SEGMENTS; i++ ) {
		float t       = ( (float)i - (SI_ARC_SEGMENTS - 1) * 0.5f ) * segAngleStep;
		float a       = angleRad + t;
		float px      = cx + sinf( a ) * radius;
		float py      = cy - cosf( a ) * radius;
		// R_DrawRotatePic expects the angle in degrees; we want each piece to
		// align tangent-to-the-ring at its own slice.
		float degrees = a * (180.0f / (float)M_PI);

		// R_DrawRotatePic2 rotates around the centre of the rect, which is
		// what we want for our tangent-aligned arc segments.
		trap->R_DrawRotatePic2(
			px - segLen * 0.5f, py - thick * 0.5f,
			segLen, thick,
			0, 0, 1, 1,
			degrees,
			cgs.media.whiteShader );
	}
	trap->R_SetColor( NULL );
}

// Draw the icon at the indicator angle, slightly inside the ring.
static void SI_DrawIcon( float cx, float cy, float angleRad, float radius,
						 qhandle_t shader, const vec4_t color, float alpha ) {
	float iconR = radius - SI_ICON_SIZE * 0.85f;
	float ix    = cx + sinf( angleRad ) * iconR - SI_ICON_SIZE * 0.5f;
	float iy    = cy - cosf( angleRad ) * iconR - SI_ICON_SIZE * 0.5f;
	vec4_t c;

	c[0] = color[0];
	c[1] = color[1];
	c[2] = color[2];
	c[3] = color[3] * alpha;

	trap->R_SetColor( c );
	trap->R_DrawStretchPic( ix, iy, SI_ICON_SIZE, SI_ICON_SIZE,
		0, 0, 1, 1, shader );
	trap->R_SetColor( NULL );
}

// =========================================================================
//  Optional radar overlay mode
//
//  Projects active sound events onto the existing minimap that JKA already
//  draws in CG_DrawRadar(). Uses the *same* yaw projection that radar
//  entities use, so direction reads consistently. Out-of-range events get
//  clamped to the radar edge so the player still gets a hint about distant
//  shots, instead of disappearing entirely.
//
//  Controlled by cg_soundIndicatorRadar:
//      0 = off (default)
//      1 = mini icons (matches center-ring icon set, just smaller)
//      2 = colored dots (compact, lowest visual noise)
// =========================================================================
void CG_SoundIndicators_DrawOnRadar( float centerX, float centerY, float radius, float radarRange ) {
	int    i;
	int    mode;
	vec3_t dirLook;
	float  angleLook;

	mode = cg_soundIndicatorRadar.integer;
	if ( mode <= 0 ) {
		return;
	}
	if ( cg.snap == NULL || radarRange <= 0.0f ) {
		return;
	}

	AngleVectors( cg.predictedPlayerState.viewangles, dirLook, NULL, NULL );
	dirLook[2] = 0.0f;
	VectorNormalize( dirLook );
	angleLook = (float)atan2( dirLook[0], dirLook[1] );

	for ( i = 0; i < SI_MAX_ACTIVE; i++ ) {
		si_event_t *ev = &si_events[i];
		int    age;
		float  alpha;
		float  fadeT;
		vec3_t dirPlayer;
		float  distance, anglePlayer, angle;
		float  px, py;
		vec4_t color;
		qhandle_t icon;

		if ( ev->startTime == 0 ) continue;
		age = cg.time - ev->startTime;
		if ( age < 0 || age >= SI_LIFETIME_MS ) continue;

		// project: same math the radar entity loop uses
		VectorSubtract( cg.predictedPlayerState.origin, ev->origin, dirPlayer );
		dirPlayer[2] = 0.0f;
		distance = VectorNormalize( dirPlayer );

		// clamp to edge instead of dropping, so distant gunfire still pulses
		if ( distance > radarRange ) {
			distance = radarRange;
		}
		distance = (distance / radarRange) * radius;

		anglePlayer = (float)atan2( dirPlayer[0], dirPlayer[1] );
		angle       = angleLook - anglePlayer;

		px = centerX + (float)sin( angle ) * distance;
		py = centerY + (float)cos( angle ) * distance;

		// fade just like the center ring
		if ( age < 100 ) {
			alpha = (float)age / 100.0f;
		} else if ( age > (SI_LIFETIME_MS - SI_FADE_MS) ) {
			fadeT = (float)( SI_LIFETIME_MS - age ) / (float)SI_FADE_MS;
			alpha = fadeT;
		} else {
			alpha = 1.0f;
		}
		if ( alpha < 0.0f ) alpha = 0.0f;
		if ( alpha > 1.0f ) alpha = 1.0f;

		SI_GetColor( ev->type, color );
		color[3] *= alpha;

		if ( mode == 2 ) {
			// dot mode: tiny solid square in the indicator's color
			const float dotSize = 4.0f;
			trap->R_SetColor( color );
			trap->R_DrawStretchPic( px - dotSize * 0.5f, py - dotSize * 0.5f,
				dotSize, dotSize, 0, 0, 1, 1, cgs.media.whiteShader );
			trap->R_SetColor( NULL );
		} else {
			// icon mode: small icon (~10 px) in the type/event icon
			const float iconSize = 10.0f;
			icon = ev->customIcon ? ev->customIcon : SI_GetIcon( ev->type );
			trap->R_SetColor( color );
			trap->R_DrawStretchPic( px - iconSize * 0.5f, py - iconSize * 0.5f,
				iconSize, iconSize, 0, 0, 1, 1, icon );
			trap->R_SetColor( NULL );
		}
	}
}

void CG_SoundIndicators_Draw( void ) {
	int   i;
	float cx, cy;
	float viewYawRad;
	float radius;
	float scale;

	if ( !cg_soundIndicators.integer ) {
		return;
	}
	if ( cg.snap == NULL ) {
		return;
	}
	// Don't draw while spectating or in intermission.
	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}
	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}

	scale = cg_soundIndicatorScale.value;
	if ( scale < 0.25f ) scale = 0.25f;
	if ( scale > 4.0f  ) scale = 4.0f;

	cx     = SCREEN_WIDTH  * 0.5f;
	cy     = SCREEN_HEIGHT * 0.5f;
	radius = SI_RING_RADIUS * scale;

	viewYawRad = cg.refdef.viewangles[YAW] * (M_PI / 180.0f);

	for ( i = 0; i < SI_MAX_ACTIVE; i++ ) {
		si_event_t *ev = &si_events[i];
		int    age;
		float  alpha;
		float  fadeT;
		vec3_t diff;
		float  worldYawRad, relYaw;
		vec4_t color;
		qhandle_t icon;

		if ( ev->startTime == 0 ) {
			continue;
		}
		age = cg.time - ev->startTime;
		if ( age < 0 || age >= SI_LIFETIME_MS ) {
			ev->startTime = 0;
			continue;
		}

		// Compute vector from the player's eye to the sound, in world space.
		VectorSubtract( ev->origin, cg.refdef.vieworg, diff );
		// World yaw (radians, 0 = +X, CCW positive).
		worldYawRad = (float)atan2( diff[1], diff[0] );
		// Relative angle (radians) where 0 = directly ahead, +CW around the
		// HUD ring. JKA viewYaw is CCW-positive in world space, so the
		// "view minus world" delta gives us the CW screen offset we want.
		relYaw = viewYawRad - worldYawRad;
		// Fold into [-PI, PI].
		while ( relYaw >  M_PI ) relYaw -= 2.0f * M_PI;
		while ( relYaw < -M_PI ) relYaw += 2.0f * M_PI;

		// Fade in over first 100ms, fade out over last SI_FADE_MS.
		if ( age < 100 ) {
			alpha = (float)age / 100.0f;
		}
		else if ( age > (SI_LIFETIME_MS - SI_FADE_MS) ) {
			fadeT = (float)( SI_LIFETIME_MS - age ) / (float)SI_FADE_MS;
			alpha = fadeT;
		}
		else {
			alpha = 1.0f;
		}
		if ( alpha < 0.0f ) alpha = 0.0f;
		if ( alpha > 1.0f ) alpha = 1.0f;

		SI_GetColor( ev->type, color );
		icon = ev->customIcon ? ev->customIcon : SI_GetIcon( ev->type );

		SI_DrawArc( cx, cy, relYaw, radius, color, alpha );
		SI_DrawIcon( cx, cy, relYaw, radius, icon, color, alpha );
	}
}


// =========================================================================
//  Public observer: single integration point for the engine's event stream.
//
//  Drop ONE call to this at the top of CG_EntityEvent() in cg_event.c,
//  right after the `if (!event) return;` guard. The big switch below owns
//  ALL event-to-indicator mappings, so cg_event.c stays clean.
//
//  This is intentionally tolerant: unknown events fall through silently,
//  duplicate events get debounced by the throttle inside Add*, and events
//  that the engine ends up suppressing later (e.g. cg_footsteps 0) still
//  produce a visual (which is desirable -- the player still made the noise
//  even if the local client muted the audio).
// =========================================================================
void CG_SoundIndicators_OnEntityEvent( centity_t *cent, vec3_t position ) {
	entityState_t *es;
	int event;

	if ( !cg_soundIndicators.integer ) {
		return; // fast-path opt-out when feature is disabled
	}
	if ( !cent ) {
		return;
	}

	es    = &cent->currentState;
	event = es->event & ~EV_EVENT_BITS;

	switch ( event ) {
		// ----- movement / proximity -----
		case EV_FOOTSTEP:
		case EV_FOOTSTEP_METAL:
		case EV_FOOTSPLASH:
		case EV_FOOTWADE:
		case EV_SWIM:
		case EV_JUMP:
		case EV_ROLL:
			CG_SoundIndicators_AddEnt( es->number, SI_TYPE_FOOTSTEP );
			break;

		// ----- weapons (outgoing fire) -----
		case EV_FIRE_WEAPON:
		case EV_ALT_FIRE:
		case EV_SABER_ATTACK:
			CG_SoundIndicators_AddEnt( es->number, SI_TYPE_WEAPON );
			break;

		// ----- impacts / explosions -----
		case EV_MISSILE_HIT:
		case EV_MISSILE_MISS:
		case EV_MISSILE_MISS_METAL:
			// the missile is the source (otherEntityNum is what it hit: a rocket hitting you read as your own sound)
			CG_SoundIndicators_Add( position, SI_TYPE_EXPLOSION, es->number );
			break;

		// ----- pickups (per-item icon dispatch) -----
		case EV_ITEM_PICKUP: {
			int index = cg_entities[es->eventParm].currentState.modelindex;
			qhandle_t pickupIcon = 0;
			if ( index >= 1 && index < bg_numItems ) {
				gitem_t *item = &bg_itemlist[index];
				if ( item->giType == IT_WEAPON ) {
					pickupIcon = cgs.media.weaponIcons[item->giTag];
				} else if ( item->giType == IT_HOLDABLE ) {
					pickupIcon = cgs.media.invenIcons[item->giTag];
				} else if ( item->giType == IT_HEALTH ) {
					pickupIcon = cgs.media.invenIcons[HI_MEDPAC];
				} else if ( item->giType == IT_ARMOR ) {
					pickupIcon = cgs.media.invenIcons[HI_SHIELD];
				}
			}
			CG_SoundIndicators_AddWithIcon( cg_entities[es->number].lerpOrigin,
				SI_TYPE_USE, es->number, pickupIcon );
			break;
		}

		// ----- doors (the +use sound) -----
		case EV_PLAYDOORSOUND:
			CG_SoundIndicators_AddEnt( es->number, SI_TYPE_USE );
			break;

		// ----- elevators / brushmodel motion -----
		case EV_BMODEL_SOUND:
			CG_SoundIndicators_AddEnt( es->number, SI_TYPE_MOVER );
			break;

		// ----- force powers -----
		case EV_FORCE_DRAINED:
			CG_SoundIndicators_AddEnt( es->owner, SI_TYPE_FORCE );
			break;
		case EV_PREDEFSOUND:
			// PDSOUND_ABSORB / ABSORBHIT / PROTECT / PROTECTHIT / FORCEJUMP / FORCEGRIP: a temp entity at the user's
			// origin, so its number is never ours; one at our own feet is our own sound
			if ( DistanceSquared( position, cg.predictedPlayerState.origin ) < 32.0f * 32.0f ) {
				break;
			}
			CG_SoundIndicators_Add( position, SI_TYPE_FORCE, es->number );
			break;

		default:
			break;
	}
}
