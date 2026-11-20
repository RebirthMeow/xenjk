#ifndef __AI_BOT2_H__
#define __AI_BOT2_H__

void Bot2_MovementOnlyCommand(int clientNum,int time,usercmd_t *cmd);
void Bot2_GoalChainReset(int clientNum);
// Clears the runner's absorb/protect, damage, mine and threat-model state (ai_bot2_objective.c).
void Bot2_RunnerForceReset(int clientNum);
// The runner's perception and reactive protect press for another client (the camper; ai_bot2_objective.c).
void Bot2_ProtectCommand(int clientNum,usercmd_t *cmd);
qboolean Bot2_IsMovementOnly(int clientNum);
// Self-kill requests between teammates (milestone B3, ai_bot2_objective.c): a fresh runner camping the
// enemy stand asks its team's spent carrier to /kill, which returns the flag to the stand for a fresh run.
void Bot2_RequestSelfKill(int carrier,int requester);
// The requester's client number while a request on this carrier stands (one second), else -1.
int Bot2_SelfKillRequested(int carrier);
qboolean Bot2_SelfKillHonour(gentity_t *carrier,const char *role);   // bot_carrierSkHomeMs guard
int Bot2_GameStepMsec(void);
// Frames of the current game step that cover ms (nearest, at least 1; 0 for ms<=0), and a work budget counted per
// 25 ms frame scaled to the current step (exact at 25 ms). Both exact at sv_fps 40 (ai_bot2.c).
int Bot2_StepsFor(int ms);
int Bot2_WorkPerFrame(int per25);
// The team's flag on its stand entity (not a dropped copy): what the first non-dropped match of a classname scan
// returns, cached (ai_bot2_objective.c).
gentity_t *Bot2_BaseFlag(int team);
// The team's next flag entity after `after` (NULL: the first), as G_Find by classname, from a cached list.
gentity_t *Bot2_NextFlag(int team,gentity_t *after);
// A dropped flag a bot can go for: no hazard on it and a graph route from the bot's root cell (ai_bot2_objective.c;
// the runner's test, shared with chase D and the camper under bot_defenderDropReach). Cached half a second a client.
qboolean Bot2_ReachableDrop(int clientNum,gentity_t *flag,int team);
int Bot2_DropReach(int clientNum,gentity_t *flag,int team);           // 1, 0, or -1 not answered yet
qboolean Bot2_DropOutOfReach(int clientNum,gentity_t *flag,int team); // at rest and answered no
// The bots' physics step: fixed (pmove_msec) when the server or bot_pmoveFixed says so.
int Bot2_PmoveFixed(void);
void Bot2_RunPlayFrame(void);

// The runner's role and objective as the play log records them (numbers kept
// from the classic AI, whose other roles and states were removed).
typedef enum { ROLE_OFFENSE } botRole_t;
typedef enum {
	MACRO_FETCH_FLAG = 0, MACRO_RETURN_FLAG = 1, MACRO_CAMP_REGEN = 3, MACRO_CHASE_THIEF = 6
} macroState_t;

typedef struct bot2_state_s {
	// --- OBJECTIVE (ai_bot2_objective.c) ---
	botRole_t role;
	macroState_t macroState;
	int targetEntNum;           // the goal entity, -1 for none
	vec3_t macroTargetOrigin;   // the movement goal

	// --- JUMP PAD (ai_bot2_movement.c; bot2_move.c reads it) ---
	int state, stateTimer;      // BOT_STATE_*, and when it was entered
	vec3_t tele_predPos;        // the pad arc's locked landing
} bot2_state_t;
extern bot2_state_t bot2_states[MAX_CLIENTS];

// bot2_state_t.state values
#define BOT_STATE_WALK        0   // anything but a pad arc
#define BOT_STATE_JUMPPAD     3   // trigger_push / jump pad

void Bot2_ClearState(int clientNum);
void Bot2_Think(int clientNum, int time);
void Bot2_UpdateManagedBots(void);

void Bot2_ExecuteMovement(int clientNum, usercmd_t *ucmd, vec3_t targetOrigin);
void Bot2_PrintTelemetry(int mode, const char* format, ...);

#endif
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
