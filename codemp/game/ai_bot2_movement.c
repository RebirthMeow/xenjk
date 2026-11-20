// ==============================================================================
// ai_bot2_movement.c - Bot2_ExecuteMovement, the movement entry of the runner,
// the fixture defenders and the cost fixture: a declared kick, a native jump
// pad, then the movement rewrite (bot2_move.c), which owns the frame or
// declines it. A declined frame (alive but not PM_NORMAL - a Force grip's
// PM_FLOAT, say) stands still. Also the floor and pit probes carrier melee
// uses. The classic off-mesh state machine and its simulations were removed
// (docs/review/FINDINGS.md O15).
// ==============================================================================

#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "ai_bot2_internal.h"
#include "bot2_move.h"
#include "bot2_graph.h"
#include "g_navmesh.h"
#include "bot2_movecost.h"
#include "bot2_kick.h"

// ==============================================================================
// Floor & Geometry Helpers
// ==============================================================================

// Prevents bots from jumping into death pits: a trigger_hurt touching the
// hull's box over the whole segment (exact for the vertical drops this is
// mostly given, conservative for a slanted one). A trace for triggers stops
// at the first one, so a hurt below another trigger (a push, a multiple)
// was missed; a full box (64 entities) counts as hurt.
qboolean CheckForTriggerHurt(gentity_t* ent, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs) {
	int entities[64], count, i; vec3_t lo, hi;
	(void)ent;
	for (i = 0; i < 3; i++) {
		lo[i] = (start[i] < end[i] ? start[i] : end[i]) + mins[i];
		hi[i] = (start[i] > end[i] ? start[i] : end[i]) + maxs[i];
	}
	count = trap->EntitiesInBox(lo, hi, entities, ARRAY_LEN(entities));
	if (count >= ARRAY_LEN(entities)) return qtrue;
	for (i = 0; i < count; i++) {
		gentity_t* hit = &g_entities[entities[i]];
		if (hit->inuse && (hit->r.contents & CONTENTS_TRIGGER) && hit->classname &&
		    !Q_stricmp(hit->classname, "trigger_hurt") &&
		    trap->EntityContact(lo, hi, (sharedEntity_t*)hit, qfalse)) return qtrue;
	}
	return qfalse;
}

// Probes the floor along a forward direction and returns a "safety score":
//
//   -1.0  — DANGER.  Either the floor drops away (>400u fall), is missing
//           entirely (open void), or contains a trigger_hurt volume.  The
//           caller treats this as "do NOT walk this way".
//   0.0–1.0 — fraction of the requested distance that's clear before the
//             forward bbox trace hits a wall (1.0 = nothing in front).
//             Higher is safer — used to compare candidate strafe yaws etc.
//
// The function does ONE forward bbox trace (dist along yaw) and THREE
// downward probes (at 33%, 66%, 100% of the cleared distance).  Any of the
// downward probes hitting a deep pit / trigger_hurt forces the -1.0 return.
//
// hitWall (optional out): set to qtrue iff the forward trace hit a near-
// vertical surface (normal.z < 0.7).  Used by the wallrun staging logic.
float TraceFloorScore(gentity_t* ent, vec3_t start, float yaw, float dist, qboolean* hitWall) {
	vec3_t forward, end, downEnd, tStart; trace_t tr, trDrop;
	vec3_t mins = { -15, -15, -24 }, maxs = { 15, 15, 32 };
	vec3_t yaw_angles = { 0, yaw, 0 };
	AngleVectors(yaw_angles, forward, NULL, NULL);

	VectorCopy(start, tStart); tStart[2] += 24.0f;
	VectorMA(tStart, dist, forward, end);
	trap->Trace(&tr, tStart, mins, maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (hitWall) *hitWall = (tr.fraction < 1.0f && tr.plane.normal[2] < 0.7f);

	float actual_dist = dist * tr.fraction;
	for (int i = 1; i <= 3; i++) {
		vec3_t probe; VectorMA(tStart, actual_dist * (i / 3.0f), forward, probe);
		VectorCopy(probe, downEnd); downEnd[2] -= 512.0f; // Use a more reasonable 512 unit pit check
		trap->Trace(&trDrop, probe, mins, maxs, downEnd, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);
		if (trDrop.fraction == 1.0f || (probe[2] - trDrop.endpos[2]) > 400.0f || CheckForTriggerHurt(ent, probe, trDrop.endpos, mins, maxs)) return -1.0f;
	}
	return tr.fraction;
}

// The map's trigger_push entities, in entity order: every bot found them by a classname
// scan of every entity, here each frame and twice more in bot2_move.c's pad code. The list is collected again when
// the level time, the map (level.startTime) or level.num_entities changes, so a frame scans once for every bot; what
// Bot2_NextPush returns is what G_Find(push,FOFS(classname),"trigger_push") would.
static struct { int time,start,numEntities,count,built;int entity[MAX_GENTITIES]; } pushList;
gentity_t *Bot2_NextPush(gentity_t *after) {
	int i,from=after?(int)(after-g_entities)+1:0;
	if(!pushList.built || pushList.time!=level.time || pushList.start!=level.startTime || pushList.numEntities!=level.num_entities) {
		gentity_t *push=NULL;
		pushList.count=0;
		while ((push = G_Find(push, FOFS(classname), "trigger_push")) != NULL)pushList.entity[pushList.count++]=(int)(push-g_entities);
		pushList.time=level.time;pushList.start=level.startTime;pushList.numEntities=level.num_entities;pushList.built=1;
	}
	for(i=0;i<pushList.count;i++)if(pushList.entity[i]>=from && g_entities[pushList.entity[i]].inuse)return &g_entities[pushList.entity[i]];
	return NULL;
}

// Bounding box check for jump pads
qboolean IsInTriggerPush(gentity_t* ent) {
	gentity_t* push = NULL;
	while ((push = Bot2_NextPush(push)) != NULL) {
		if (push->r.linked) {
			vec3_t mins, maxs; VectorAdd(ent->client->ps.origin, ent->r.mins, mins); VectorAdd(ent->client->ps.origin, ent->r.maxs, maxs);
			if (maxs[0] >= push->r.absmin[0] && mins[0] <= push->r.absmax[0] && maxs[1] >= push->r.absmin[1] && mins[1] <= push->r.absmax[1] && maxs[2] >= push->r.absmin[2] && mins[2] <= push->r.absmax[2])
				return qtrue;
		}
	}
	return qfalse;
}

static struct {int time;const char *name;} movementOwner[MAX_CLIENTS];

static void Bot2_SetMovementOwner(int clientNum,const char *name) {
	if(clientNum>=0 && clientNum<MAX_CLIENTS) {
		movementOwner[clientNum].time=level.time;
		movementOwner[clientNum].name=name;
	}
}

const char *Bot2_MovementOwner(int clientNum) {
	if(clientNum<0 || clientNum>=MAX_CLIENTS || movementOwner[clientNum].time!=level.time ||
	   !movementOwner[clientNum].name)return "none";
	return movementOwner[clientNum].name;
}

// A native pad owns steering before the movement rewrite and the off-mesh states.
// Keep its locked landing target throughout the arc; movement reacquires after landing.
static void Bot2_JumppadNearLanding(gentity_t *ent, vec3_t landing) {
	vec3_t original;
	VectorCopy(landing, original);
	float dx = original[0] - ent->client->ps.origin[0];
	float dy = original[1] - ent->client->ps.origin[1];
	float bestDistance = sqrtf(dx * dx + dy * dy);
	int axis;
	// Offmesh endpoints may sit diagonally along a ledge. From rest on a pad,
	// spending air control on that lateral travel can miss the ledge entirely.
	// Try the two nearer axis projections, but only at the same supported height.
	for (axis = 0; axis < 2; ++axis) {
		vec3_t start, end;
		trace_t floor;
		VectorCopy(original, start);
		start[axis] = ent->client->ps.origin[axis];
		dx = start[0] - ent->client->ps.origin[0];
		dy = start[1] - ent->client->ps.origin[1];
		float distance = sqrtf(dx * dx + dy * dy);
		if (distance < 64.0f || distance + 16.0f >= bestDistance) continue;
		VectorCopy(start, end);
		start[2] += 64.0f;
		end[2] -= 64.0f;
		trap->Trace(&floor, start, ent->r.mins, ent->r.maxs, end,
		    ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);
		if (floor.startsolid || floor.allsolid || floor.fraction >= 1.0f ||
		    floor.plane.normal[2] < 0.7f || floor.entityNum != ENTITYNUM_WORLD ||
		    (floor.contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP))) continue;
		VectorCopy(floor.endpos, landing);
		bestDistance = distance;
	}
}

static qboolean Bot2_JumppadMovement(int clientNum, usercmd_t *ucmd, vec3_t targetOrigin) {
	gentity_t *ent = &g_entities[clientNum];
	if (bot2_states[clientNum].state != BOT_STATE_JUMPPAD && !IsInTriggerPush(ent))
		return qfalse;
	NavMeshWaypoint_t padWp = {0};
	// Recover the navmesh connection once on entry, never retarget an already
	// airborne pad arc.
	if (bot2_states[clientNum].state != BOT_STATE_JUMPPAD && VectorLength(targetOrigin) > 1.0f) {
		if (!NavMesh_GetNextWaypointEx(ent->s.number, ent->client->ps.origin, targetOrigin,
		    &padWp, (1 << OFFMESH_AREA_WALLRUN) | (1 << OFFMESH_AREA_ELEVATOR)))
			padWp.offmeshType = 0;
	}
	int offmeshType = padWp.offmeshType;
	float *offmeshEnd = padWp.offmeshEnd;
	if (bot2_states[clientNum].state != BOT_STATE_JUMPPAD) {
		bot2_states[clientNum].state = BOT_STATE_JUMPPAD;
		bot2_states[clientNum].stateTimer = level.time;

		const char* lockSrc;
		vec3_t padLanding;
		// The movement rewrite's own pad edge, found from the world with no
		// navmesh (bot_moveNativeEdges): steer at its landing.
		if (Move_PadLanding(clientNum, padLanding)) {
			VectorCopy(padLanding, bot2_states[clientNum].tele_predPos);
			lockSrc = "native pad edge";
		}
		// FALLBACK 1: the navmesh's offmesh landing for this pad, the upper
		// ledge the trajectory the trigger_push imparts can reach.
		else if (offmeshType == OFFMESH_AREA_JUMPPAD
		         && (offmeshEnd[0] != 0.0f || offmeshEnd[1] != 0.0f || offmeshEnd[2] != 0.0f)) {
			VectorCopy(offmeshEnd, bot2_states[clientNum].tele_predPos);
			Bot2_JumppadNearLanding(ent, bot2_states[clientNum].tele_predPos);
			lockSrc = "current-tick offmeshEnd";
		}
		// FALLBACK 2: no offmesh data — face the macro goal (e.g. the flag) and
		// hold W.  This is intentionally the ONLY fallback: trigger_push
		// target_position is often straight overhead on vertical pads (useless
		// for steering), and Detour's post-jumppad waypoint is the very thing
		// whose value-function drift caused the original bug.  Aiming at the
		// end goal at least gives the bot a coherent direction to lean toward
		// for whatever in-air control is available, and it's correct on average
		// even when the offmesh connection is missing.
		else if (VectorLength(targetOrigin) > 1.0f) {
			VectorCopy(targetOrigin, bot2_states[clientNum].tele_predPos);
			lockSrc = "macro goal (no offmesh)";
		}
		// LAST RESORT: no goal either (shouldn't happen in normal play).  Keep
		// whatever tele_predPos was already set to so we don't snap to <0,0,0>.
		else {
			lockSrc = "preserved (no goal)";
		}

		Bot2_PrintTelemetry(1,"[%s] STATE Transition: Entered Jump Pad (State 3) - Locked Target {%.0f, %.0f, %.0f} (src=%s)\n",
			ent->client->pers.netname, bot2_states[clientNum].tele_predPos[0], bot2_states[clientNum].tele_predPos[1], bot2_states[clientNum].tele_predPos[2], lockSrc);
	}

	// --- STATE 3: JUMP PAD / PUSHED ---
	if (bot2_states[clientNum].state == BOT_STATE_JUMPPAD) {

		vec3_t padDir, padAngles, nativeLanding;
		float padWish[2];
		int coast = 0;
		VectorSubtract(bot2_states[clientNum].tele_predPos, ent->client->ps.origin, padDir);
		// A native pad edge (bot_moveNativeEdges) is flown by the law its
		// discovery checked: accelerate toward the velocity that arrives on
		// the landing, coast once it is held (Graph_PadSteer).
		if (Move_PadLanding(clientNum, nativeLanding) &&
		    VectorCompare(nativeLanding, bot2_states[clientNum].tele_predPos)) {
			if (Graph_PadSteer(ent->client->ps.origin, ent->client->ps.velocity, nativeLanding, padWish)) {
				padDir[0] = padWish[0]; padDir[1] = padWish[1]; padDir[2] = 0;
			} else coast = 1;
		}
		vectoangles(padDir, padAngles);

		ucmd->angles[PITCH] = ANGLE2SHORT(AngleNormalize360(padAngles[PITCH])) - ent->client->ps.delta_angles[PITCH];
		ucmd->angles[YAW] = ANGLE2SHORT(AngleNormalize360(padAngles[YAW])) - ent->client->ps.delta_angles[YAW];

		ucmd->forwardmove = coast ? 0 : 127;
		ucmd->rightmove = ucmd->upmove = 0;

		if (ent->client->ps.groundEntityNum != ENTITYNUM_NONE && (level.time - bot2_states[clientNum].stateTimer > 500)) {
			bot2_states[clientNum].state = BOT_STATE_WALK;
			Bot2_PrintTelemetry(1,"[%s] STATE Transition: Landed from Jump Pad -> Walk\n", ent->client->pers.netname);
		}
	}
	return qtrue;
}

void Bot2_ExecuteMovement(int clientNum, usercmd_t* ucmd, vec3_t targetOrigin) {
	gentity_t* ent;
	if (clientNum < 0 || clientNum >= level.maxclients) return;
	ent = &g_entities[clientNum];
	Bot2_SetMovementOwner(clientNum,"none");
	if (!ent->inuse || !ent->client || level.intermissiontime || level.time == 0) return;
	// A declared fixture kick (bot_moveKick) lands between frames, as a hit
	// would: before anything this frame plans or commands from the state.
	Bot2_KickFrame(clientNum);
	if (Bot2_JumppadMovement(clientNum, ucmd, targetOrigin)) {
		Bot2_SetMovementOwner(clientNum,"jumppad");
		return;
	}
	// Movement rewrite (docs/movement). The executor owns the frame or
	// declines it; a declined frame stands still.
	Move_Plan(clientNum,targetOrigin);
	if(Move_Command(clientNum,ucmd)) {Bot2_SetMovementOwner(clientNum,"move");return;}
	Bot2_SetMovementOwner(clientNum,"declined");
	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	{
		int i;
		for (i = 0; i < 3; i++) ucmd->angles[i] = ANGLE2SHORT(ent->client->ps.viewangles[i]) - ent->client->ps.delta_angles[i];
	}
}
