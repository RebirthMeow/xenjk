// Resting movers as floor (docs/movement/records/2026-09-24-lifts.md).
//
// A binary mover (a func_door or func_plat lift) that stands at one of its
// two positions with nothing scheduled to move it is floor like the world's:
// Pmove stands on it the same way and nothing pushes the player. It stays
// floor until the level time its next movement can begin, which is what these
// functions answer from the live entities. Everything else a trace can strike
// - a mover in motion, a body, an item - is still not floor.
#ifndef BOT2_MOVER_H
#define BOT2_MOVER_H

#define BOT2_MOVER_FOREVER 0x7fffffff
#define BOT2_MOVER_CALL_TARGETS 4

// For a binary mover at rest, the level time before which it cannot start to
// move (BOT2_MOVER_FOREVER when nothing is scheduled); 0 for a mover in motion
// and anything that is not a binary mover (the world included; callers test
// ENTITYNUM_WORLD first). An absolute time: it is floor while level.time is
// earlier, which holds inside a forecast rollout's simulated frames too.
int Bot2_MoverRestUntil(int entityNum);
// Each real game frame, before anything asks (G_RunFrame): the rest memo's key.
void Bot2_MoverFrame(void);

// The world, or a binary mover at rest at level.time.
qboolean Bot2_StandableEntity(int entityNum);

// A lift call: a trigger_multiple whose every target is a binary mover and
// that has no other output. Fills the movers it would start (their team
// masters) and returns how many, or 0 when the trigger is not a lift call.
// *delayMs receives, per mover, the least time from a touch to its motion:
// the trigger's delay plus the mover's own.
int Bot2_MoverCallTargets(int triggerNum, int *movers, int *delayMs, int max);
int Bot2_MoverCalls(void);  // lift calls on this map
// The least delay of the lift calls that start this mover and that a player
// standing at the origin touches, or -1 when none reaches it.
int Bot2_MoverBoardDelay(int mover, const vec3_t origin);

// Inline model number of a mover ("*12" is 12), or 0; and the entity that
// carries an inline model now, or -1. A graph cache names movers by model,
// which does not depend on the spawn order.
int Bot2_MoverModel(int entityNum);
// The entity that moves a lift team: a team slave's master, else the entity.
// Lift calls are recorded against it.
int Bot2_MoverMaster(int entityNum);
int Bot2_MoverEntity(int model);

// Drop every per-map answer; Move_PrepareMap calls it at each map load.
void Bot2_MoverReset(void);

#endif
