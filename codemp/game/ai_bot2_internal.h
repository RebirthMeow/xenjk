// ==============================================================================
// ai_bot2_internal.h - Private header shared between ai_bot2_*.c source files.
//
// Public API lives in ai_bot2.h.  This header exposes helpers that need to be
// callable across the bot2 source files without polluting the public bot2
// surface.  The private Bot2 diagnostic logger may also include it for
// read-only telemetry.
// ==============================================================================
#ifndef __AI_BOT2_INTERNAL_H__
#define __AI_BOT2_INTERNAL_H__

#include "ai_bot2.h"

const char *Bot2_MovementOwner(int clientNum);
// Four-sample ground corridor from ai_bot2_localnav.c, shared with the
// movement rewrite (bot2_move.c). Reads the world only.
// The corridor memo's scope (row 5): one
// bot's Pmove boundary, in which the world is static, so a corridor test is a
// pure function of its entity's hull and its endpoints.
void Bot2_GroundCorridorMemoBegin(void);
void Bot2_GroundCorridorMemoEnd(void);
void Bot2_GroundCorridorMemoFlush(void);   // drop the memo, keep the scope open
qboolean Bot2_GroundCorridor(gentity_t *ent,const vec3_t start,const vec3_t end,vec3_t result,
                             const char **reason,int *sample);
// One corridor sample, the admission the rewrite's preparation runs cell to
// cell (bot2_graph.c). NULL when admitted, else the refusal name.
const char *Bot2_GroundSample(int passEntity,const vec3_t mins,const vec3_t maxs,const vec3_t previous,
                              vec3_t point,int sample,int *hit);
// The floor the last admitted sample stands on: ENTITYNUM_WORLD, or a resting
// mover (bot2_mover.h).
int Bot2_GroundSampleFloor(void);

// --- Geometry & floor probing (ai_bot2_movement.c) ---
qboolean CheckForTriggerHurt(gentity_t* ent, vec3_t start, vec3_t end, vec3_t mins, vec3_t maxs);
float    TraceFloorScore(gentity_t* ent, vec3_t start, float yaw, float dist, qboolean* hitWall);
qboolean IsInTriggerPush(gentity_t* ent);
// The next trigger_push after `after` (NULL: the first), as G_Find by classname, from a list collected once a frame.
gentity_t *Bot2_NextPush(gentity_t *after);

#endif
