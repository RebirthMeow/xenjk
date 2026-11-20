// Declared fixture defender (bot2_defender.c): one enemy bot that holds a post
// and pushes or pulls the movement runner into lethal hurt volumes.
#ifndef BOT2_DEFENDER_H
#define BOT2_DEFENDER_H

// True for the declared defender client (Defender_v2) in a movement-only world.
qboolean Bot2_DefenderIs(int clientNum);
// The enemy-flag keep-out box of an armed base or chase D (1, flag origin and box in hull-origin space),
// else 0; *mode is bot_defenderFlagKeepOutRoute (1 forbidden volume, 2 the old body lattice, 0 neither).
int Bot2_DefenderKeepOutBox(int clientNum,vec3_t at,vec3_t mins,vec3_t maxs,int *mode);
// An interior graph cell on the stand's level between lo and hi from it, nearest `near` (the camper's camping spot); 0 if none.
int Bot2_CampSpot(const vec3_t stand,float lo,float hi,const vec3_t near,vec3_t out);
// The defender's command for this game frame. Idle until released.
void Bot2_DefenderCommand(int clientNum, int time, usercmd_t *cmd);
// Declared setup, from the corner start: the defender to its post, Force full.
void Bot2_DefenderRelease(void);
// Whether a declared defender runs the movement searches (hops, strafes) now:
// always, unless bot_defenderWalkErrands keeps them to its chases.
qboolean Bot2_DefenderHops(int clientNum);

#endif
