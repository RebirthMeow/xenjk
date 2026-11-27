/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Spear saber throw (g_saberSpear 1).
//
// The thrown saber flies point first in a straight line with a slight drop, cannot be steered or held out, and
// hits once. A blocked spear goes back to its owner as a blocked throw does. A kill with a steep world wall
// close behind the victim carries the body to the wall: the blade sticks in the wall through the part it hit
// (s.generic1, bg_saberspear.h), the corpse is held where its ragdoll's pelvis hangs below that part (the client
// ragdoll follows the entity origin), and its ragAttach names the saber, so cgame holds that part's ragdoll bone on
// the pin point (s.origin2) and hangs the rest below it. After g_saberSpearPinMs the saber
// pulls out and returns and the body drops. A throw that hits a wall with no victim sticks briefly, then returns.

#include "g_local.h"
#include "w_saber.h"
#include "bg_saberspear.h"

void thrownSaberTouch(gentity_t *saberent, gentity_t *other, trace_t *trace);
qboolean G_GetHitLocFromSurfName(gentity_t *ent, const char *surfName, int *hitLoc, vec3_t point, vec3_t dir, vec3_t bladeDir, int mod);
void saberFirstThrown(gentity_t *saberent);
qboolean saberCheckKnockdown_Thrown(gentity_t *saberent, gentity_t *saberOwner, gentity_t *other);
void WP_SaberBlockNonRandom(gentity_t *self, vec3_t hitloc, qboolean missileBlock);

#define SPEAR_GRAVITY_SCALE	0.15f	// share of g_gravity: ~15 units of drop over 700 units at 1400 u/s
#define SPEAR_HULL			4.0f	// half size of the flight trace box
#define SPEAR_HILT_BACK		28.0f	// hilt distance in front of the wall: blade through the body, tip in the wall
#define SPEAR_PIN_DEPTH		10.0f	// pin point (the torso) in front of the wall
#define SPEAR_WALL_MAX_NZ	0.7f	// steeper surfaces than this count as walls
#define SPEAR_STICK_MS		400		// a throw that hits a wall without a victim stays this long

#define SPEAR_MAX_LIFT		32.0f	// most the spear lifts a hit part so the body hung from it clears the floor
#define SPEAR_RELEASE_PUSH	80.0f	// a released body drops this fast away from the wall
#define SPEAR_FEET_CLEAR	14.0f	// the hung body's feet above the floor

// A pinned corpse's origin above the pin point. The client ragdoll's root (pelvis) follows the entity origin, so
// this puts the pelvis where it hangs below the pinned part (bg_saberspear.h); cgame hangs the rest from there.
static float SpearAnchorUp(int part)
{
	return SPEAR_RAG_PELVIS_DROP - spearPinPelvisBelow[part];
}

// How far the spear lifts the hit part on its way to the wall: enough for the body hung from it to clear the floor.
static float SpearLift(gentity_t *ent, int part, const vec3_t hitPoint)
{
	float floorZ = ent->r.currentOrigin[2] + ent->r.mins[2];
	float need;

	if (!SpearPinHangs(part))
	{
		return 0.0f;
	}
	need = floorZ + spearPinPelvisBelow[part] + SPEAR_PELVIS_TO_FEET + SPEAR_FEET_CLEAR - hitPoint[2];
	return need < 0.0f ? 0.0f : (need > SPEAR_MAX_LIFT ? SPEAR_MAX_LIFT : need);
}

typedef enum {
	SPEAR_NONE,
	SPEAR_FLY,		// flying, no victim yet
	SPEAR_CARRY,	// killed a victim, carrying the body to the wall point
	SPEAR_STUCK		// in the wall, with or without a pinned corpse
} spearPhase_t;

typedef struct {
	spearPhase_t	phase;
	vec3_t			vel;
	vec3_t			dir;
	vec3_t			wallPoint;
	vec3_t			wallNormal;
	int				lastTime;
	int				until;
	int				victim;		// ENTITYNUM_NONE when nothing is carried or pinned
	int				victimSpawn;
	int				bodyStamp;	// nonzero: the victim respawned and the pin holds its body-queue copy (ET_BODY)
	vec3_t			bodyOffset;	// corpse origin relative to the spear while carried and pinned
	float			bodyOffsetZ0;	// its height at the hit; it eases to the part's anchorUp over the carry
	int				part;			// spearPin_t: the body part the blade went through
	float			carryLen;
	vec3_t			pinOrigin;	// where the pinned corpse is held
} spearState_t;

static spearState_t spearStates[MAX_GENTITIES];

static void SpearLog(gentity_t *saberent, const char *what)
{ // developer 1: one console line per spear event, for scripted checks
	spearState_t *st = &spearStates[saberent->s.number];

	if (trap->Cvar_VariableIntegerValue("developer"))
	{
		trap->Print("SPEAR %d %s at %.0f %.0f %.0f victim %d t %d\n", saberent->s.number, what,
			saberent->r.currentOrigin[0], saberent->r.currentOrigin[1], saberent->r.currentOrigin[2], st->victim, level.time);
	}
}

qboolean SaberSpear_Active(void)
{
	return (qboolean)(g_saberSpear.integer != 0);
}

static int SpearSpawnCount(gentity_t *ent)
{
	return ent->client ? ent->client->ps.persistant[PERS_SPAWN_COUNT] : 0;
}

static void SpearSetAngles(gentity_t *saberent, const vec3_t dir)
{ // the blade lies along the model's up axis; pitching the flight direction by 90 points it forward
	vec3_t ang;

	vectoangles(dir, ang);
	ang[PITCH] += 90.0f;
	ang[ROLL] = 0.0f;
	saberent->s.apos.trType = TR_STATIONARY;
	saberent->s.apos.trTime = level.time;
	VectorClear(saberent->s.apos.trDelta);
	VectorCopy(ang, saberent->s.apos.trBase);
	VectorCopy(ang, saberent->r.currentAngles);
}

static void SpearPlace(gentity_t *saberent, const vec3_t org, const vec3_t vel)
{
	VectorCopy(org, saberent->r.currentOrigin);
	VectorCopy(org, saberent->s.pos.trBase);
	saberent->s.pos.trType = TR_LINEAR;
	saberent->s.pos.trTime = level.time;
	VectorCopy(vel, saberent->s.pos.trDelta);
	trap->LinkEntity((sharedEntity_t *)saberent);
}

static gentity_t *SpearVictim(spearState_t *st)
{ // the carried or pinned corpse, while it is still the same dead body
	gentity_t *v;

	if (st->victim == ENTITYNUM_NONE)
	{
		return NULL;
	}
	v = &g_entities[st->victim];
	if (st->bodyStamp)
	{
		return (v->inuse && v->s.eType == ET_BODY && v->timestamp == st->bodyStamp) ? v : NULL;
	}
	if (!v->inuse || !v->client || v->health > 0 || SpearSpawnCount(v) != st->victimSpawn)
	{
		return NULL;
	}
	return v;
}

static void SpearMoveBody(gentity_t *v, const vec3_t target, const vec3_t vel, const vec3_t wallNormal)
{ // place the corpse directly: dead NPCs run no pmove, and a client's own pmove is frozen while pinned.
  // Box traces keep it out of the wall: up or down first, then along the wall (wallNormal, when given), then
  // across, so touching the wall stops neither the height change nor the slide under the pin.
	trace_t tr;
	vec3_t pos, next;
	int step;

	VectorCopy(v->r.currentOrigin, pos);
	for (step = 0; step < 3; step++)
	{
		if (step == 0)
		{
			VectorSet(next, pos[0], pos[1], target[2]);
		}
		else if (step == 1)
		{
			vec3_t d;

			if (!wallNormal)
			{
				continue;
			}
			VectorSubtract(target, pos, d);
			d[2] = 0.0f;
			VectorMA(d, -DotProduct(d, wallNormal), wallNormal, d);
			VectorAdd(pos, d, next);
			next[2] = target[2];
		}
		else
		{
			VectorCopy(target, next);
		}
		trap->Trace(&tr, pos, v->r.mins, v->r.maxs, next, v->s.number, MASK_SOLID, qfalse, 0, 0);
		if (tr.startsolid || tr.allsolid)
		{
			if (step == 0)
			{
				return;
			}
			break;
		}
		VectorCopy(tr.endpos, pos);
	}
	if (v->client)
	{
		VectorCopy(pos, v->client->ps.origin);
		VectorCopy(vel, v->client->ps.velocity);
	}
	G_SetOrigin(v, pos);
	v->s.pos.trType = TR_INTERPOLATE;
	VectorCopy(vel, v->s.pos.trDelta);
	trap->LinkEntity((sharedEntity_t *)v);
}

static void SpearFlagBody(gentity_t *v, int ragAttach)
{ // a dead NPC's entityState is not rebuilt from its playerState, so set both
	v->client->ps.eFlags |= EF_RAG;
	v->s.eFlags |= EF_RAG;
	v->client->ps.ragAttach = ragAttach;
	v->s.ragAttach = ragAttach;
}

static void SpearReleaseVictim(spearState_t *st, gentity_t *saberent)
{
	gentity_t *v = SpearVictim(st);

	if (v && st->bodyStamp)
	{ // a respawned victim's body copy: let it fall, a little away from the wall so it does not fold back into it
		v->s.ragAttach = 0;
		v->physicsObject = qtrue;
		VectorCopy(v->r.currentOrigin, v->s.pos.trBase);
		VectorScale(st->wallNormal, SPEAR_RELEASE_PUSH, v->s.pos.trDelta);
		v->s.pos.trDelta[2] = 0.0f;
		// a body copy has no box (it normally starts on the floor); give it a dead player's, or it lands with its
		// origin on the floor and the ragdoll, which hangs below the origin, sinks into it
		VectorSet(v->r.mins, -15.0f, -15.0f, DEFAULT_MINS_2);
		VectorSet(v->r.maxs, 15.0f, 15.0f, -8.0f);
		trap->LinkEntity((sharedEntity_t *)v);
		v->s.pos.trType = TR_GRAVITY;
		v->s.pos.trTime = level.time;
	}
	else if (v)
	{
		if (v->client->ps.ragAttach == saberent->s.number)
		{
			v->client->ps.ragAttach = 0;
			v->s.ragAttach = 0;
		}
		v->client->saberPinUntil = 0;
		if (v->health <= 0)
		{
			VectorScale(st->wallNormal, SPEAR_RELEASE_PUSH, v->client->ps.velocity);
			v->client->ps.velocity[2] = 0.0f;
		}
	}
	st->victim = ENTITYNUM_NONE;
	st->bodyStamp = 0;
}

qboolean SaberSpear_BodyCopied(gentity_t *ent, gentity_t *body)
{ // g_client.c CopyToBodyQue: a pinned victim respawned; the pin moves to its body copy, which holds still
	int i;

	for (i = 0; i < MAX_GENTITIES; i++)
	{
		spearState_t *st = &spearStates[i];

		if (st->phase != SPEAR_STUCK || st->victim != ent->s.number || st->bodyStamp || SpearVictim(st) != ent)
		{
			continue;
		}
		st->victim = body->s.number;
		st->bodyStamp = body->timestamp;
		body->s.ragAttach = i;
		body->s.eFlags |= EF_RAG;
		body->physicsObject = qfalse;
		VectorClear(body->s.pos.trDelta);
		body->s.pos.trType = TR_STATIONARY;
		ent->client->saberPinUntil = 0;
		return qtrue;
	}
	return qfalse;
}

static void SpearEnd(gentity_t *saberent)
{ // hand the saber back to the stock throw code, which returns it to the owner
	spearState_t *st = &spearStates[saberent->s.number];

	SpearReleaseVictim(st, saberent);
	st->phase = SPEAR_NONE;
	saberent->s.userInt1 = 0;
	saberent->s.generic1 = 0;
	VectorClear(saberent->s.origin2);
}

static void SpearReturn(gentity_t *saberent)
{
	SpearLog(saberent, "return");
	SpearEnd(saberent);
	saberent->r.contents = CONTENTS_LIGHTSABER;
	thrownSaberTouch(saberent, saberent, NULL);
}

void SaberSpear_Launch(gentity_t *saberent, gentity_t *owner, const vec3_t dir)
{
	spearState_t *st = &spearStates[saberent->s.number];
	float speed = g_saberSpearSpeed.value > 100.0f ? g_saberSpearSpeed.value : 100.0f;

	(void)owner;
	memset(st, 0, sizeof(*st));
	st->phase = SPEAR_FLY;
	st->victim = ENTITYNUM_NONE;
	st->lastTime = level.time;
	VectorCopy(dir, st->dir);
	VectorScale(dir, speed, st->vel);

	saberent->s.userInt1 = 1; // cgame: draw with the server's angles, no spin
	saberent->s.generic1 = 0;
	VectorClear(saberent->s.origin2);
	SpearSetAngles(saberent, dir);
	VectorCopy(st->vel, saberent->s.pos.trDelta);
	saberent->s.pos.trType = TR_LINEAR;
	saberent->damage = g_saberSpearDamage.integer;
	SpearLog(saberent, "launch");
}

static void SpearStick(gentity_t *saberent, spearState_t *st, const vec3_t wallPoint, int ms)
{
	vec3_t hilt;

	VectorMA(wallPoint, -SPEAR_HILT_BACK, st->dir, hilt);
	SpearSetAngles(saberent, st->dir);
	SpearPlace(saberent, hilt, vec3_origin);
	VectorMA(wallPoint, -SPEAR_PIN_DEPTH, st->dir, saberent->s.origin2);
	saberent->r.contents = 0;
	st->phase = SPEAR_STUCK;
	st->until = level.time + ms;
	G_Sound(saberent, CHAN_AUTO, G_SoundIndex("sound/weapons/saber/saberhitwall1.wav"));
	SpearLog(saberent, SpearVictim(st) ? "pin" : "stick");

	{
		gentity_t *v = SpearVictim(st);

		if (v)
		{ // pin: hold the corpse against the wall and point its ragdoll at the blade
			vec3_t target;

			// hang it from the pin point; the box trace stops the body against the wall
			VectorCopy(saberent->s.origin2, target);
			target[2] += SpearAnchorUp(st->part);
			SpearMoveBody(v, target, vec3_origin, st->wallNormal);
			VectorCopy(v->r.currentOrigin, st->pinOrigin);
			SpearFlagBody(v, saberent->s.number);
			saberent->s.generic1 = st->part;
			v->client->saberPinUntil = st->until;
		}
	}
}

static qboolean SpearDuelBlocks(gentity_t *owner, gentity_t *ent)
{
	if (ent->client->ps.duelInProgress && ent->client->ps.duelIndex != owner->s.number)
	{
		return qtrue;
	}
	if (owner->client->ps.duelInProgress && owner->client->ps.duelIndex != ent->s.number)
	{
		return qtrue;
	}
	return qfalse;
}

static void SpearHitEvent(int ev, trace_t *tr, int victimNum, int ownerNum)
{
	gentity_t *te = G_TempEntity(tr->endpos, ev);

	VectorCopy(tr->endpos, te->s.origin);
	VectorCopy(tr->plane.normal, te->s.angles);
	if (!te->s.angles[0] && !te->s.angles[1] && !te->s.angles[2])
	{
		te->s.angles[1] = 1;
	}
	te->s.otherEntityNum = victimNum;
	te->s.otherEntityNum2 = ownerNum;
	te->s.weapon = 0;
	te->s.legsAnim = 0;
	te->s.eventParm = 1;
}

static qboolean SpearBonePos(gentity_t *ent, const vec3_t angles, const vec3_t origin, const char *bone, vec3_t pos)
{
	mdxaBone_t m;
	int bolt = trap->G2API_AddBolt(ent->ghoul2, 0, bone);

	if (bolt < 0 || !trap->G2API_GetBoltMatrix(ent->ghoul2, 0, bolt, &m, angles, origin, level.time, NULL, ent->modelScale))
	{
		return qfalse;
	}
	BG_GiveMeVectorFromMatrix(&m, ORIGIN, pos);
	return qtrue;
}

// The body part the blade goes through: a ray along the flight against the victim's model (the flight trace only
// hits its box), its surface's hit location, then the nearest of that limb's ragdoll bones. hitPoint gets the
// point on the model, or the box hit when the ray misses the model.
static int SpearPickPart(gentity_t *ent, spearState_t *st, const trace_t *tr, vec3_t hitPoint)
{
	static CollisionRecord_t g2Trace[MAX_G2_COLLISIONS];
	vec3_t angles, origin, start, end;
	int hitLoc = HL_NONE, cand[4], nCand = 0, best = SPEAR_PIN_CHEST, i;
	float bestDist = -1.0f;

	VectorCopy(tr->endpos, hitPoint);
	if (!ent->ghoul2 || !ent->client)
	{
		return SPEAR_PIN_CHEST;
	}
	VectorCopy(ent->client->ps.origin, origin);
	VectorSet(angles, 0.0f, ent->client->ps.viewangles[YAW], 0.0f);
	VectorMA(tr->endpos, -8.0f, st->dir, start);
	VectorMA(tr->endpos, 64.0f, st->dir, end);
	memset(g2Trace, 0, sizeof(g2Trace));
	for (i = 0; i < MAX_G2_COLLISIONS; i++)
	{
		g2Trace[i].mEntityNum = -1;
	}
	trap->G2API_CollisionDetect(g2Trace, ent->ghoul2, angles, origin, level.time, ent->s.number, start, end,
		ent->modelScale, 0, g_g2TraceLod.integer, SPEAR_HULL);
	if (g2Trace[0].mEntityNum == ent->s.number)
	{
		char surf[MAX_QPATH];

		VectorCopy(g2Trace[0].mCollisionPosition, hitPoint);
		surf[0] = 0;
		trap->G2API_GetSurfaceName(ent->ghoul2, g2Trace[0].mSurfaceIndex, 0, surf);
		G_GetHitLocFromSurfName(ent, surf, &hitLoc, hitPoint, st->dir, vec3_origin, MOD_SABER);
		// the damage that follows takes its hit location from this surface too
		ent->client->g2LastSurfaceHit = g2Trace[0].mSurfaceIndex;
		ent->client->g2LastSurfaceTime = level.time;
	}
	if (hitLoc == HL_NONE)
	{
		hitLoc = G_GetHitLocation(ent, hitPoint);
	}

	switch (hitLoc)
	{
	case HL_HEAD:		cand[nCand++] = SPEAR_PIN_HEAD; break;
	case HL_WAIST:		cand[nCand++] = SPEAR_PIN_WAIST; cand[nCand++] = SPEAR_PIN_CHEST; break;
	case HL_ARM_RT:		cand[nCand++] = SPEAR_PIN_RARM; cand[nCand++] = SPEAR_PIN_CHEST; break;
	case HL_ARM_LT:		cand[nCand++] = SPEAR_PIN_LARM; cand[nCand++] = SPEAR_PIN_CHEST; break;
	case HL_HAND_RT:	cand[nCand++] = SPEAR_PIN_RHAND; cand[nCand++] = SPEAR_PIN_RARM; break;
	case HL_HAND_LT:	cand[nCand++] = SPEAR_PIN_LHAND; cand[nCand++] = SPEAR_PIN_LARM; break;
	case HL_LEG_RT:
		cand[nCand++] = SPEAR_PIN_RTHIGH; cand[nCand++] = SPEAR_PIN_RSHIN; cand[nCand++] = SPEAR_PIN_WAIST;
		break;
	case HL_LEG_LT:
		cand[nCand++] = SPEAR_PIN_LTHIGH; cand[nCand++] = SPEAR_PIN_LSHIN; cand[nCand++] = SPEAR_PIN_WAIST;
		break;
	case HL_FOOT_RT:	cand[nCand++] = SPEAR_PIN_RFOOT; cand[nCand++] = SPEAR_PIN_RSHIN; break;
	case HL_FOOT_LT:	cand[nCand++] = SPEAR_PIN_LFOOT; cand[nCand++] = SPEAR_PIN_LSHIN; break;
	default:			cand[nCand++] = SPEAR_PIN_CHEST; break; // chest, back, and anything else
	}

	for (i = 0; i < nCand; i++)
	{
		vec3_t pos;
		float d;

		if (nCand == 1 || !SpearBonePos(ent, angles, origin, spearPinBone[cand[i]], pos))
		{
			if (i == 0)
			{
				best = cand[0];
			}
			continue;
		}
		d = Distance(pos, hitPoint);
		if (bestDist < 0.0f || d < bestDist)
		{
			bestDist = d;
			best = cand[i];
		}
	}
	if (trap->Cvar_VariableIntegerValue("developer"))
	{
		trap->Print("SPEAR part %s (hit location %d) at height %.0f\n", spearPinBone[best], hitLoc,
			hitPoint[2] - ent->r.currentOrigin[2]);
	}
	return best;
}

// A victim was struck. Returns qtrue when the spear keeps flying (it is carrying the body to a wall).
static qboolean SpearHitVictim(gentity_t *saberent, spearState_t *st, gentity_t *owner, gentity_t *ent, trace_t *tr)
{
	int dflags = 0;
	trace_t wt;
	vec3_t end, hitPoint;

	if (SpearDuelBlocks(owner, ent))
	{
		SpearReturn(saberent);
		return qfalse;
	}

	if (!owner->client->ps.isJediMaster && WP_SaberCanBlock(ent, tr->endpos, 0, MOD_SABER, qfalse, 999))
	{
		WP_SaberBlockNonRandom(ent, tr->endpos, qfalse);
		SpearLog(saberent, "blocked");
		SpearHitEvent(EV_SABER_BLOCK, tr, 0, 0);
		owner->client->ps.saberAttackWound = level.time + 500;
		SpearEnd(saberent);
		saberent->r.contents = CONTENTS_LIGHTSABER;
		if (saberCheckKnockdown_Thrown(saberent, owner, ent))
		{ // knocked out of the air; the knockdown code owns the saber now
			return qfalse;
		}
		thrownSaberTouch(saberent, saberent, NULL);
		return qfalse;
	}

	if (owner->client->saber[0].saberFlags2 & SFL2_NO_DISMEMBERMENT)
	{
		dflags |= DAMAGE_NO_DISMEMBER;
	}
	st->part = SpearPickPart(ent, st, tr, hitPoint);
	{
		int before = ent->health;

		G_Damage(ent, owner, owner, st->dir, hitPoint,
			owner->client->ps.isJediMaster ? saberent->damage * 2 : saberent->damage, dflags, MOD_SABER);
		if (trap->Cvar_VariableIntegerValue("developer"))
		{
			trap->Print("SPEAR %d hit %d health %d -> %d\n", saberent->s.number, ent->s.number, before, ent->health);
		}
	}
	SpearHitEvent(EV_SABER_HIT, tr, ent->s.number, owner->s.number);
	owner->client->ps.saberAttackWound = level.time + 500;

	if (ent->health > 0 || !ent->client || g_saberSpearPinRange.value <= 0.0f)
	{
		SpearReturn(saberent);
		return qfalse;
	}

	// a kill: is there a wall close behind the part that was hit, at its height (a little higher for the upper body,
	// so a body hung from it clears the floor)?
	{
		vec3_t from, flat;
		float speed = VectorLength(st->vel);

		VectorSet(flat, st->dir[0], st->dir[1], 0.0f);
		if (VectorNormalize(flat) < 0.1f)
		{ // thrown straight down or up: nothing to pin to
			SpearReturn(saberent);
			return qfalse;
		}
		VectorCopy(hitPoint, from);
		from[2] += SpearLift(ent, st->part, hitPoint);
		VectorMA(from, g_saberSpearPinRange.value, flat, end);
		trap->Trace(&wt, from, NULL, NULL, end, ent->s.number, MASK_SOLID, qfalse, 0, 0);
		if (wt.startsolid || wt.allsolid || wt.fraction >= 1.0f || wt.entityNum != ENTITYNUM_WORLD ||
			(wt.surfaceFlags & SURF_SKY) || fabsf(wt.plane.normal[2]) > SPEAR_WALL_MAX_NZ)
		{
			SpearReturn(saberent);
			return qfalse;
		}
		// the spear rises a little on its way to the wall, carrying the body up with it
		VectorSubtract(wt.endpos, tr->endpos, st->dir);
		st->carryLen = VectorNormalize(st->dir);
		VectorScale(st->dir, speed, st->vel);
	}

	VectorCopy(wt.endpos, st->wallPoint);
	VectorCopy(wt.plane.normal, st->wallNormal);
	st->victim = ent->s.number;
	st->victimSpawn = SpearSpawnCount(ent);
	st->phase = SPEAR_CARRY;
	SpearLog(saberent, "carry");
	// the body rides the spear to the wall
	VectorSubtract(ent->r.currentOrigin, tr->endpos, st->bodyOffset);
	st->bodyOffsetZ0 = st->bodyOffset[2];
	VectorCopy(st->vel, ent->client->ps.velocity);
	SpearFlagBody(ent, 0);
	return qtrue;
}

void SaberSpear_Think(gentity_t *saberent)
{
	spearState_t *st = &spearStates[saberent->s.number];
	gentity_t *owner = &g_entities[saberent->r.ownerNum];
	float dt;
	vec3_t start, end, mins, maxs;
	trace_t tr;

	if (!level.saberPassFrame)
	{ // tick: thrown sabers think with the saber pass (g_tick.c)
		saberent->nextthink = level.time;
		return;
	}
	saberent->nextthink = level.time;

	if (saberent->r.ownerNum == ENTITYNUM_NONE || !owner->inuse || !owner->client ||
		owner->client->sess.sessionTeam == TEAM_SPECTATOR || owner->health < 1 ||
		!owner->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] || st->phase == SPEAR_NONE)
	{ // the stock throw handles a lost or dead owner
		SpearEnd(saberent);
		saberent->r.contents = CONTENTS_LIGHTSABER;
		saberent->think = saberFirstThrown;
		saberFirstThrown(saberent);
		return;
	}

	if (st->phase == SPEAR_STUCK)
	{
		if (level.time >= st->until || (st->victim != ENTITYNUM_NONE && !SpearVictim(st)))
		{
			SpearReturn(saberent);
			return;
		}
		{ // hold a pinned corpse where it was put
			gentity_t *v = SpearVictim(st);

			if (v && !st->bodyStamp)
			{
				SpearMoveBody(v, st->pinOrigin, vec3_origin, st->wallNormal);
			}
		}
		return;
	}

	{
		vec3_t sub;

		VectorSubtract(owner->client->ps.origin, saberent->r.currentOrigin, sub);
		if (VectorLength(sub) >= 700.0f * owner->client->ps.fd.forcePowerLevel[FP_SABERTHROW] ||
			level.time - owner->client->ps.saberDidThrowTime > 6000)
		{ // out of range
			SpearReturn(saberent);
			return;
		}
	}

	dt = (level.time - st->lastTime) * 0.001f;
	st->lastTime = level.time;
	if (dt <= 0.0f)
	{
		return;
	}

	if (st->phase != SPEAR_CARRY)
	{ // the carry is a straight line to the wall point
		st->vel[2] -= g_gravity.value * SPEAR_GRAVITY_SCALE * dt;
	}
	VectorCopy(st->vel, st->dir);
	VectorNormalize(st->dir);

	VectorCopy(saberent->r.currentOrigin, start);
	VectorMA(start, dt, st->vel, end);
	VectorSet(mins, -SPEAR_HULL, -SPEAR_HULL, -SPEAR_HULL);
	VectorSet(maxs, SPEAR_HULL, SPEAR_HULL, SPEAR_HULL);

	if (st->phase == SPEAR_CARRY)
	{
		vec3_t toWall;

		VectorSubtract(st->wallPoint, start, toWall);
		if (DotProduct(toWall, toWall) <= Square(VectorLength(st->vel) * dt) || DotProduct(toWall, st->vel) <= 0.0f)
		{ // reaches the wall this frame
			SpearStick(saberent, st, st->wallPoint, g_saberSpearPinMs.integer);
			return;
		}
		{ // keep the body moving with the spear
			gentity_t *v = SpearVictim(st);

			if (v)
			{
				vec3_t target, left;
				float p;

				VectorSubtract(st->wallPoint, end, left);
				p = st->carryLen > 1.0f ? 1.0f - VectorLength(left) / st->carryLen : 1.0f;
				p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
				st->bodyOffset[2] = st->bodyOffsetZ0 + (SpearAnchorUp(st->part) - st->bodyOffsetZ0) * p;
				VectorAdd(end, st->bodyOffset, target);
				SpearMoveBody(v, target, st->vel, NULL);
			}
		}
	}

	// neither the saber's own box nor the carried corpse (a dead NPC keeps its body contents a while) may stop the trace
	{
		gentity_t *carried = st->phase == SPEAR_CARRY ? SpearVictim(st) : NULL;
		int carriedContents = carried ? carried->r.contents : 0;

		saberent->r.contents = 0;
		if (carried)
		{
			carried->r.contents = 0;
		}
		trap->Trace(&tr, start, mins, maxs, end, owner->s.number, MASK_SOLID | CONTENTS_BODY, qfalse, 0, 0);
		if (tr.startsolid && tr.entityNum == ENTITYNUM_WORLD)
		{ // grazing start: try the line instead of the box
			trap->Trace(&tr, start, NULL, NULL, end, owner->s.number, MASK_SOLID | CONTENTS_BODY, qfalse, 0, 0);
		}
		saberent->r.contents = CONTENTS_LIGHTSABER;
		if (carried)
		{
			carried->r.contents = carriedContents;
		}
	}

	if (tr.fraction >= 1.0f && !tr.startsolid)
	{
		SpearSetAngles(saberent, st->dir);
		SpearPlace(saberent, end, st->vel);
		return;
	}

	if (tr.entityNum < ENTITYNUM_WORLD)
	{
		gentity_t *ent = &g_entities[tr.entityNum];

		if (ent->client && ent->takedamage && ent->health > 0 && st->phase == SPEAR_FLY)
		{
			SpearPlace(saberent, tr.endpos, st->vel);
			SpearHitVictim(saberent, st, owner, ent, &tr);
			return;
		}
		if (ent->takedamage && ent->health > 0 && !ent->client)
		{ // breakables and the like
			G_Damage(ent, owner, owner, st->dir, tr.endpos, saberent->damage, 0, MOD_SABER);
		}
		if (trap->Cvar_VariableIntegerValue("developer"))
		{
			trap->Print("SPEAR %d struck entity %d (health %d)\n", saberent->s.number, ent->s.number, ent->health);
		}
		SpearPlace(saberent, tr.endpos, vec3_origin);
		SpearReturn(saberent);
		return;
	}

	// the world
	if (tr.surfaceFlags & SURF_SKY)
	{
		SpearReturn(saberent);
		return;
	}
	if (st->phase == SPEAR_CARRY)
	{ // something nearer than the wall point: pin there
		SpearStick(saberent, st, tr.endpos, g_saberSpearPinMs.integer);
		return;
	}
	SpearHitEvent(EV_SABER_BLOCK, &tr, 0, 0);
	SpearStick(saberent, st, tr.endpos, SPEAR_STICK_MS);
}

void SaberSpear_ClientPinned(gentity_t *ent)
{ // g_active: a pinned corpse holds still where the spear left it
	gclient_t *client = ent->client;

	if (!client || client->saberPinUntil <= level.time || client->ps.stats[STAT_HEALTH] > 0)
	{
		return;
	}
	client->ps.pm_type = PM_FREEZE;
	VectorClear(client->ps.velocity);
}
