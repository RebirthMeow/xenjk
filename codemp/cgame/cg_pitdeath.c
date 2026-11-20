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
===========================================================================
Pit deaths: a map's own death for its trigger_hurt pits. Display only.

A trigger_hurt in the map's entity lump carrying "xen_death" "hand" marks its volume as a pit the map dresses.
The server knows nothing of this. A kill pit (dmg -1) does not kill on touch: it marks the player as falling to
their death (ps.fallingToDeath, EF_RAG), takes their controls away, and ends it FALL_FADE_TIME later. Nothing
about that changes: the same height, the same three seconds, the same kill. What changes is what is shown for
those three seconds. Stock shows the body dropping away and the screen fading out. Here:

  falling in    a giant hologram hand (a two-frame model: open, closed) rises under the faller, timed to meet
                them at the trigger
  the touch     the hand closes on them and holds them where they were caught; their body is drawn in its grip
                (the real, unseen player falls on, as the server has it)
  held          the hand lifts them, then squeezes: it tightens and shakes harder until
  the kill      a flash, the body is gone, the hand opens empty and sinks away

The local player's camera stands off to one side for the hold instead of watching themselves fall, and the
fade to black waits for the kill.

Keys on the trigger_hurt:
  "xen_death"       "hand"
  "xen_hand_model"  the two-frame hand model (default models/kjx/hand.md3): origin in the palm, +X from the
                    forearm into the palm (the palm faces +X), fingers toward -Z when open
  "xen_hand_scale"  default 60
  "xen_hand_shader" drawn over the hand (default: the nearest misc_model_ghoul2 giant's shader)
===========================================================================
*/

#include "cg_local.h"

#define MAX_PIT_ZONES		4
#define PIT_WATCH_HEIGHT	620.0f	// start the hand when a faller is within this of the trigger's top
#define PIT_RISE_DEPTH		1100.0f	// the hand starts this far below where it will catch
#define PIT_CLOSE_MS		200		// the fingers close
#define PIT_LIFT_MS			1300	// then it lifts its catch
#define PIT_LIFT			260.0f
#define PIT_CRUSH_MS			260		// at the kill it clenches shut on what it holds
#define PIT_OPEN_MS			420		// then opens, empty
#define PIT_SINK_MS			1000	// and sinks

typedef struct pitZone_s {
	vec3_t		mins, maxs;
	qhandle_t	model;
	qhandle_t	shader;
	float		scale;
} pitZone_t;

typedef enum { PIT_IDLE, PIT_RISING, PIT_HELD, PIT_DONE } pitState_t;

typedef struct pitCatch_s {
	pitState_t	state;
	int			zone;
	int			startTime, deathTime, stateTime;
	vec3_t		target;			// where the palm meets the player
	vec3_t		fingers;		// horizontal unit vector the open fingers point along
	vec3_t		held;			// where the body is drawn this frame
	qhandle_t	shader;
	float		sinkFrom;		// how far below the catch point the hand was when it began to sink
	qboolean	killed;			// the hold ended in the kill (else the faller got away)
	qboolean	hideBody;
} pitCatch_t;

static pitZone_t	pitZones[MAX_PIT_ZONES];
static int			numPitZones;
static pitCatch_t	pitCatches[MAX_CLIENTS];
static sfxHandle_t	pitGrabSound;

void CG_PitDeath_Clear( void ) {
	numPitZones = 0;
	memset( pitCatches, 0, sizeof( pitCatches ) );
}

// cg_spawn.c: every trigger_hurt of the entity lump comes through here
void SP_xen_trigger_hurt( void ) {
	char		*style, *model, *shader;
	pitZone_t	*z;

	CG_SpawnString( "xen_death", "", &style );
	if ( Q_stricmp( style, "hand" ) || numPitZones >= MAX_PIT_ZONES ) {
		return;
	}
	CG_SpawnString( "model", "", &model );
	if ( !model[0] ) {
		return;
	}
	z = &pitZones[numPitZones];
	memset( z, 0, sizeof( *z ) );
	trap->R_ModelBounds( trap->R_RegisterModel( model ), z->mins, z->maxs );
	CG_SpawnString( "xen_hand_model", "models/kjx/hand.md3", &model );
	z->model = trap->R_RegisterModel( model );
	CG_SpawnString( "xen_hand_shader", "", &shader );
	z->shader = shader[0] ? trap->R_RegisterShader( shader ) : 0;
	CG_SpawnFloat( "xen_hand_scale", "60", &z->scale );
	if ( !z->model ) {
		trap->Print( S_COLOR_YELLOW "WARNING: pit death: hand model '%s' not found\n", model );
		return;
	}
	pitGrabSound = trap->S_RegisterSound( "sound/weapons/force/grip.mp3" );
	numPitZones++;
}

qboolean CG_PitDeath_HidesBody( int clientNum ) {
	return ( clientNum >= 0 && clientNum < MAX_CLIENTS && pitCatches[clientNum].hideBody ) ? qtrue : qfalse;
}

// a body in the hand's grip is drawn there, not where the server has it falling
qboolean CG_PitDeath_HeldOrigin( int clientNum, vec3_t out ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || pitCatches[clientNum].state != PIT_HELD ) {
		return qfalse;
	}
	VectorCopy( pitCatches[clientNum].held, out );
	return qtrue;
}

// what the giants should be looking at: someone in a hand
qboolean CG_PitDeath_Focus( vec3_t out ) {
	int i;

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( pitCatches[i].state == PIT_HELD ) {
			VectorCopy( pitCatches[i].held, out );
			return qtrue;
		}
	}
	return qfalse;
}

// the local player's view of their own end: standing off to the side of the hand, looking at it
qboolean CG_PitDeath_Camera( vec3_t org, matrix3_t axis ) {
	const pitCatch_t	*c;
	vec3_t				side, dir, ang;

	if ( !cg.snap || pitCatches[cg.snap->ps.clientNum].state != PIT_HELD ) {
		return qfalse;
	}
	c = &pitCatches[cg.snap->ps.clientNum];
	VectorSet( side, -c->fingers[1], c->fingers[0], 0.0f );
	VectorMA( c->target, 330.0f, side, org );
	VectorMA( org, 230.0f, c->fingers, org );
	org[2] += 210.0f;
	VectorSubtract( c->held, org, dir );
	vectoangles( dir, ang );
	AnglesToAxis( ang, axis );
	return qtrue;
}

// 0..1 for the black fade over the local player's screen (stock: over the first half of the fall); -1: not ours
float CG_PitDeath_Fade( void ) {
	const pitCatch_t *c;

	if ( !cg.snap ) {
		return -1.0f;
	}
	c = &pitCatches[cg.snap->ps.clientNum];
	if ( c->state == PIT_HELD ) {
		float f = ( cg.time - c->stateTime - ( FALL_FADE_TIME - 300 ) ) / 300.0f;

		return f < 0.0f ? 0.0f : ( f > 1.0f ? 1.0f : f );
	}
	if ( c->state == PIT_DONE && c->killed ) {
		return 1.0f;
	}
	return -1.0f;
}

// the giant nearest a point lends the hand its look and the side its arm comes from
static void PitNearestGiant( const vec3_t at, qhandle_t *shader, vec3_t fingers ) {
	const cg_g2model_t	*g = CG_MiscG2_Nearest( at, 8.0f );	// a giant: modelscale 8 or more
	vec3_t				d;

	VectorSet( fingers, 1.0f, 0.0f, 0.0f );
	if ( g ) {
		VectorSubtract( at, g->org, d );
		d[2] = 0.0f;
		if ( VectorNormalize( d ) > 1.0f ) {
			VectorCopy( d, fingers );		// reaching out from her: the fingers point away from her
		}
		if ( !*shader ) {
			*shader = g->shader;
		}
	}
}

// the hand with its palm at `palm`, `closed` 0 (open) to 1 (a fist), `squeeze` 0..1 tightening it further
static void PitDrawHand( const pitCatch_t *c, const pitZone_t *z, const vec3_t palm, float closed, float squeeze ) {
	refEntity_t	ent;
	vec3_t		up = { 0.0f, 0.0f, 1.0f }, side;
	float		scale = z->scale * ( 1.0f - 0.22f * squeeze );

	memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_MODEL;
	ent.hModel = z->model;
	ent.customShader = c->shader;
	ent.renderfx = RF_NOSHADOW;
	ent.frame = 1;
	ent.oldframe = 0;
	ent.backlerp = 1.0f - closed;
	ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
	VectorCopy( palm, ent.origin );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	// model +X (forearm into the palm) is up: the arm rises from below, palm up; open fingers (-Z) lie along
	// c->fingers; +Y completes a right-handed set. The forearm keeps its length while the fist tightens
	VectorScale( up, z->scale, ent.axis[0] );
	VectorScale( c->fingers, -scale, ent.axis[2] );
	CrossProduct( ent.axis[2], ent.axis[0], side );
	VectorNormalize( side );
	VectorScale( side, scale, ent.axis[1] );
	VectorSet( ent.modelScale, z->scale, z->scale, z->scale );
	ent.nonNormalizedAxes = qtrue;
	ent.radius = 14.0f * z->scale;
	trap->R_AddRefEntityToScene( &ent );
}

void CG_PitDeath_Frame( void ) {
	int i, k;

	if ( !numPitZones || !cg.snap ) {
		return;
	}
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		pitCatch_t	*c = &pitCatches[i];
		vec3_t		pos, vel, palm;
		qboolean	doomed, dead, valid, falling;	// falling: the pit trigger's own signal (a catch)
		float		gravity = 800.0f;

		if ( i == cg.snap->ps.clientNum ) {
			const playerState_t *ps = &cg.predictedPlayerState;

			valid = ( ps->persistant[PERS_TEAM] != TEAM_SPECTATOR && !( ps->pm_flags & PMF_FOLLOW ) ) ? qtrue : qfalse;
			VectorCopy( ps->origin, pos );
			VectorCopy( ps->velocity, vel );
			dead = ( ps->stats[STAT_HEALTH] <= 0 || ps->pm_type == PM_DEAD ) ? qtrue : qfalse;
			falling = ps->fallingToDeath ? qtrue : qfalse;
			doomed = ( dead || falling ) ? qtrue : qfalse;
			if ( ps->gravity > 0 ) {
				gravity = ps->gravity;
			}
		} else {
			const centity_t *cent = &cg_entities[i];

			valid = ( cent->currentValid && cent->currentState.eType == ET_PLAYER ) ? qtrue : qfalse;
			VectorCopy( cent->lerpOrigin, pos );
			VectorCopy( cent->currentState.pos.trDelta, vel );
			dead = ( cent->currentState.eFlags & EF_DEAD ) ? qtrue : qfalse;
			falling = ( cent->currentState.eFlags & EF_RAG ) ? qtrue : qfalse;	// g_trigger.c sets it with fallingToDeath
			doomed = ( dead || falling ) ? qtrue : qfalse;
		}
		if ( c->hideBody && ( !doomed || !valid ) ) {
			c->hideBody = qfalse;		// respawned, or gone from view
		}
		if ( c->state == PIT_IDLE ) {
			if ( !valid || doomed || vel[2] > -150.0f ) {
				continue;
			}
			for ( k = 0; k < numPitZones; k++ ) {
				const pitZone_t *z = &pitZones[k];
				float top = z->maxs[2] + 24.0f;		// a player's origin when their feet reach the trigger
				float dz = pos[2] - top, t;

				if ( pos[0] < z->mins[0] || pos[0] > z->maxs[0] || pos[1] < z->mins[1] || pos[1] > z->maxs[1]
					|| dz <= 0.0f || dz > PIT_WATCH_HEIGHT ) {
					continue;
				}
				// z0 + vz t - g t^2 / 2 = top
				t = ( vel[2] + sqrt( vel[2] * vel[2] + 2.0f * gravity * dz ) ) / gravity;
				c->zone = k;
				c->state = PIT_RISING;
				c->killed = qfalse;
				c->startTime = cg.time;
				c->deathTime = cg.time + (int)( t * 1000.0f );
				VectorSet( c->target, pos[0] + vel[0] * t, pos[1] + vel[1] * t, top );
				c->shader = z->shader;
				PitNearestGiant( c->target, &c->shader, c->fingers );
				if ( cg_debugEvents.integer ) trap->Print( "pitdeath: client %i falling, hand due in %i ms\n", i, c->deathTime - cg.time );
				break;
			}
			if ( c->state == PIT_IDLE ) {
				continue;
			}
		}
		{
			const pitZone_t *z = &pitZones[c->zone];

			VectorCopy( c->target, palm );
			palm[2] -= 30.0f;
			if ( c->state == PIT_RISING ) {
				float span = (float)( c->deathTime - c->startTime );
				float u = span > 1.0f ? ( cg.time - c->startTime ) / span : 1.0f;
				float below;

				if ( u > 1.0f ) u = 1.0f;
				if ( valid && !doomed ) {
					// keep under them: where they will be when they reach the trigger
					float left = ( c->deathTime - cg.time ) * 0.001f;

					if ( left < 0.0f ) left = 0.0f;
					c->target[0] = pos[0] + vel[0] * left;
					c->target[1] = pos[1] + vel[1] * left;
				}
				below = ( 1.0f - u ) * ( 1.0f - u ) * PIT_RISE_DEPTH;	// fast first, easing in under them
				if ( valid && falling ) {	// a death in the air (shot, say) is not a catch: the hand sinks unfed
					c->state = PIT_HELD;
					c->stateTime = cg.time;
					VectorCopy( c->target, c->held );
					if ( pitGrabSound ) {
						trap->S_StartSound( c->target, ENTITYNUM_NONE, CHAN_AUTO, pitGrabSound );
					}
					if ( cg_debugEvents.integer ) trap->Print( "pitdeath: client %i caught, %i ms from the forecast\n", i, cg.time - c->deathTime );
				} else if ( !valid || cg.time > c->deathTime + 350 ) {
					c->state = PIT_DONE;		// they did not come down here
					c->stateTime = cg.time;
					c->sinkFrom = below;
				}
				palm[2] -= below;
				PitDrawHand( c, z, palm, 0.0f, 0.0f );
			} else if ( c->state == PIT_HELD ) {
				int		t = cg.time - c->stateTime;
				float	closed = t / (float)PIT_CLOSE_MS;
				float	lift = ( t - PIT_CLOSE_MS ) / (float)PIT_LIFT_MS;
				float	squeeze = ( t - PIT_CLOSE_MS - PIT_LIFT_MS ) / (float)( FALL_FADE_TIME - PIT_CLOSE_MS - PIT_LIFT_MS );
				float	shake;

				if ( closed > 1.0f ) closed = 1.0f;
				if ( lift < 0.0f ) lift = 0.0f;
				if ( lift > 1.0f ) lift = 1.0f;
				if ( squeeze < 0.0f ) squeeze = 0.0f;
				if ( squeeze > 1.0f ) squeeze = 1.0f;
				lift = lift * lift * ( 3.0f - 2.0f * lift ) * PIT_LIFT;
				shake = squeeze * squeeze * 9.0f;
				palm[2] += lift;
				palm[0] += shake * sin( cg.time * 0.071f );
				palm[1] += shake * sin( cg.time * 0.093f + 1.3f );
				palm[2] += shake * 0.6f * sin( cg.time * 0.117f );
				VectorCopy( palm, c->held );
				c->held[2] += 34.0f - 26.0f * squeeze;		// in the palm; drawn into the fist as it tightens
				PitDrawHand( c, z, palm, closed, squeeze );
				if ( squeeze > 0.0f ) {
					trap->R_AddLightToScene( c->held, 150.0f + 250.0f * squeeze, 1.0f, 0.5f + 0.4f * squeeze, 0.8f );
				}
				if ( !valid || dead || !doomed || t > FALL_FADE_TIME + 400 ) {
					c->state = PIT_DONE;
					c->stateTime = cg.time;
					c->sinkFrom = -lift;
					c->killed = ( valid && dead ) ? qtrue : qfalse;
					c->hideBody = c->killed;
					if ( cg_debugEvents.integer ) trap->Print( "pitdeath: client %i %s after %i ms in the hand\n", i, c->killed ? "killed" : "released", t );
				}
			} else {	// PIT_DONE: open (after a kill, with a flash) and sink
				int		t = cg.time - c->stateTime;
				int		hold = c->killed ? PIT_CRUSH_MS + 180 : 0;		// clenched, a beat, then it lets go
				float	crush = c->killed ? t / (float)PIT_CRUSH_MS : 0.0f;
				float	open = c->killed ? ( t - hold ) / (float)PIT_OPEN_MS : 1.0f;
				float	sink = ( t - ( c->killed ? hold + PIT_OPEN_MS : 0 ) ) / (float)PIT_SINK_MS;
				float	squeeze = 0.0f;

				if ( crush > 1.0f ) crush = 1.0f;
				if ( open < 0.0f ) open = 0.0f;
				if ( open > 1.0f ) open = 1.0f;
				if ( sink >= 1.0f ) {
					c->state = PIT_IDLE;
					continue;
				}
				if ( sink < 0.0f ) sink = 0.0f;
				if ( c->killed ) {
					// from the squeeze's hold (1) it snaps down past it, then relaxes as the fingers open
					squeeze = ( 1.0f + 1.6f * ( 1.0f - ( 1.0f - crush ) * ( 1.0f - crush ) ) ) * ( 1.0f - open );
				}
				palm[2] -= c->sinkFrom + sink * sink * PIT_RISE_DEPTH;
				PitDrawHand( c, z, palm, c->killed ? 1.0f - open : 0.0f, squeeze );
				if ( c->killed && t < 420 ) {
					trap->R_AddLightToScene( palm, 800.0f * ( 1.0f - t / 420.0f ) + 100.0f, 1.0f, 0.95f, 1.0f );
				}
			}
		}
	}
}
