// ==============================================================================
// ai_bot2.c - Bot2 dispatch: which managed bots are runners or fixture
// defenders, their one command per game frame, and the runner's kill rules.
// The classic macro AI (roles, item and weapon hunting, combat) was removed
// (docs/review/FINDINGS.md O15).
// ==============================================================================

#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "bot2_svcmds.h"
#include "bot2_probe.h"
#include "bot2_threat.h"
#include "bot2_move.h"
#include "bot2_defender.h"
#include "bot2_movecost.h"

extern void G_Kill(gentity_t* ent);

// ==============================================================================
// State Machine Tracker
// ==============================================================================
bot2_state_t bot2_states[MAX_CLIENTS] = { 0 };
static int movementFrame[MAX_CLIENTS];
static qboolean playDispatch;

// bot_runnerCapKill 1 (default 1 in play, bot_movementOnly 2, and 0 in the
// fixture benches, which measure past a capture): a movement-only runner that has just
// captured kills itself when its Force is under bot_runnerCapKillForce
// (default 100, i.e. any Force spent), so it respawns full instead of setting
// out again spent. Human offense players selfkill on their own stand 40% of
// the time against 19% baseline, after a capture or a return, and are back in
// about 1.2 s (records/2026-09-27-position-pilot.md, self-kills). The capture
// count is the native one (PERS_CAPTURES), so any capture counts; CAPKILL lines.
void G_Kill(gentity_t *ent);
static int runnerCapturesSeen[MAX_CLIENTS];   // captures + 1; 0 = not seen yet
static void RunnerCapKill(int clientNum) {
	gentity_t *ent=&g_entities[clientNum];int caps=ent->client->ps.persistant[PERS_CAPTURES];char value[16];int below;
	if(!runnerCapturesSeen[clientNum] || caps<runnerCapturesSeen[clientNum]-1) {runnerCapturesSeen[clientNum]=caps+1;return;}
	if(caps==runnerCapturesSeen[clientNum]-1)return;
	runnerCapturesSeen[clientNum]=caps+1;
	trap->Cvar_VariableStringBuffer("bot_runnerCapKill",value,sizeof(value));
	if(!(value[0]?atoi(value):trap->Cvar_VariableIntegerValue("bot_movementOnly")==2) || ent->health<=0)return;
	trap->Cvar_VariableStringBuffer("bot_runnerCapKillForce",value,sizeof(value));
	below=value[0]?atoi(value):100;
	trap->Print("[RUNNER] CAPKILL level=%d client=%d captures=%d force=%d health=%d below=%d killed=%d\n",level.time,clientNum,caps,
		ent->client->ps.fd.forcePower,ent->health,below,ent->client->ps.fd.forcePower<below);
	if(ent->client->ps.fd.forcePower<below)G_Kill(ent);
}

// bot_pitKill (default 1 in play, bot_movementOnly 2, and 0 elsewhere;
// mirroring the legacy Bot2's rule): a bot that touched a
// fall-to-blackness trigger_hurt (fallingToDeath, set only there) dies now
// instead of FALL_FADE_TIME (3 s) later. The kill is the native one
// (ClientThink_real's fade): MOD_FALLING, credited to a recent pusher, so
// kill credit and fall counts are unchanged; only the respawn comes sooner.
static void PitKill(int clientNum) {
	gentity_t *ent=&g_entities[clientNum],*killer=ent;char value[16];
	if(!ent->client->ps.fallingToDeath || ent->health<=0)return;
	trap->Cvar_VariableStringBuffer("bot_pitKill",value,sizeof(value));
	if(!(value[0]?atoi(value):trap->Cvar_VariableIntegerValue("bot_movementOnly")==2))return;
	if(ent->client->ps.otherKillerTime>level.time && ent->client->ps.otherKiller!=ENTITYNUM_NONE &&
	   g_entities[ent->client->ps.otherKiller].inuse)killer=&g_entities[ent->client->ps.otherKiller];
	trap->Print("[MOVE] PIT_KILL level=%d client=%d falling_ms=%d killer=%d\n",level.time,clientNum,
		level.time-ent->client->ps.fallingToDeath,killer->s.number);
	G_Damage(ent,killer,killer,NULL,ent->client->ps.origin,9999,DAMAGE_NO_PROTECTION,MOD_FALLING);
	G_MuteSound(ent->s.number,CHAN_VOICE);
}

// bot_strandedKillMs (default 10000 in play mode, bot_movementOnly 2; 0 off):
// a Bot2 bot that has stood stranded off the movement graph
// (Move_Stranded: grounded, no cell under it, none below within the step-off
// reach) this long takes /kill, as a human would. A non-lethal fall into a
// pocket the graph does not cover left a carrier standing 518 s with the flag
// (records/2026-10-03-hud-knowledge.md). Checked twice a second.
// The timer is per occupant: cleared at the bot's setup (Bot2_ClearState), so a bot that left stranded and alive does
// not hand it to the next.
void G_Kill(gentity_t *ent);   // g_cmds.c: /kill
static int strandedSince[MAX_CLIENTS];
static void StrandedKill(int clientNum) {
	gentity_t *ent=&g_entities[clientNum];char value[16];int ms;
	trap->Cvar_VariableStringBuffer("bot_strandedKillMs",value,sizeof(value));
	ms=value[0]?atoi(value):(trap->Cvar_VariableIntegerValue("bot_movementOnly")==2?10000:0);
	if(ms<=0 || ent->health<=0 || (level.time%500)>=Bot2_GameStepMsec()) {if(ent->health<=0)strandedSince[clientNum]=0;return;}
	if(!Move_Stranded(clientNum)) {strandedSince[clientNum]=0;return;}
	if(!strandedSince[clientNum]) {strandedSince[clientNum]=level.time;return;}
	if(level.time-strandedSince[clientNum]<ms)return;
	trap->Print("[MOVE] STRANDED_KILL level=%d client=%d ms=%d x=%.0f y=%.0f z=%.0f carrying=%d\n",level.time,clientNum,
		level.time-strandedSince[clientNum],ent->client->ps.origin[0],ent->client->ps.origin[1],ent->client->ps.origin[2],
		ent->client->ps.powerups[PW_REDFLAG]||ent->client->ps.powerups[PW_BLUEFLAG]);
	strandedSince[clientNum]=0;
	G_Kill(ent);
}

int Bot2_GameStepMsec(void) {
	return G_TickNominalFrameMsec();   // g_tick.c
}

// Tickrate independence (records/2026-10-21-tickrate-horizons.md): a duration Bot2 used to
// count in game frames is stated in ms and stepped here, rounded to the nearest frame and at least one. Every such
// duration is a multiple of 25 ms, so at the 25 ms test step (sv_fps 40) the counts are exactly the old ones.
int Bot2_StepsFor(int ms) {
	int step=Bot2_GameStepMsec();
	return ms<=0?0:max(1,(ms+step/2)/step);
}

// A per-frame work budget stated per 25 ms frame (rollouts, forecast steps, settled cells), scaled to the frame
// length so the work per second of game time does not depend on the tickrate: exactly per25 at 25 ms, at least one
// while per25 is positive, and per25 itself (0: off) otherwise.
int Bot2_WorkPerFrame(int per25) {
	if(per25<=0)return per25;
	return max(1,(int)(((long long)per25*Bot2_GameStepMsec()+12)/25));
}

// bot_pmoveFixed (set by addrunner): the Bot2 bots alone step their physics in
// fixed pmove_msec slices (client->pers.pmoveFixed, which ClientThink_real
// honours), so the server's pmove_fixed stays what human players expect - 0 on
// base_enhanced, where a fixed server step discards every other command of a
// client faster than 1000/pmove_msec fps, one-shot force presses with it.
int Bot2_PmoveFixed(void) {
	return trap->Cvar_VariableIntegerValue("pmove_fixed") || trap->Cvar_VariableIntegerValue("bot_pmoveFixed");
}

void Bot2_RunPlayFrame(void) {
	int i;
	if(trap->Cvar_VariableIntegerValue("bot_movementOnly")!=2)return;
	Bot2_UpdateManagedBots();
	Bot2_ThreatHideBegin();
	Bot2_ProbeBegin("think",-1);
	playDispatch=qtrue;
	for(i=0;i<level.maxclients;i++)if(botstates[i] && botstates[i]->inuse &&
	    botstates[i]->customThink==Bot2_Think)Bot2_Think(i,level.time);
	playDispatch=qfalse;
	Bot2_ProbeEnd();
	Bot2_ThreatHideEnd();
}

qboolean Bot2_IsMovementOnly(int clientNum) {
	int mode=trap->Cvar_VariableIntegerValue("bot_movementOnly");
	char name[MAX_NETNAME];
	if(mode!=2)return mode!=0;
	if(clientNum<0 || clientNum>=MAX_CLIENTS || !g_entities[clientNum].client ||
	   !(g_entities[clientNum].r.svFlags&SVF_BOT))return qfalse;
	{
		// A function of the name alone, kept per client by the raw netname (perf-lossless record).
		static char seen[MAX_CLIENTS][MAX_NETNAME];static qboolean runner[MAX_CLIENTS];
		const char *raw=g_entities[clientNum].client->pers.netname;
		if(seen[clientNum][0] && !strcmp(seen[clientNum],raw))return runner[clientNum];
		Q_strncpyz(name,raw,sizeof(name));Q_CleanStr(name);
		// Runner2_v2: the second runner of a two-sided game (milestone B2).
		runner[clientNum]=!Q_stricmp(name,"Runner_v2") || !Q_stricmp(name,"Runner2_v2");
		Q_strncpyz(seen[clientNum],raw,sizeof(seen[clientNum]));
		return runner[clientNum];
	}
}

void Bot2_ClearState(int clientNum) {
	if (clientNum >= 0 && clientNum < MAX_CLIENTS) {
		memset(&bot2_states[clientNum], 0, sizeof(bot2_state_t));
		// targetEntNum=0 after memset is entity 0 (world), which has no client.
		// Explicitly set to -1 so any accidental deref is caught by our guards.
		bot2_states[clientNum].targetEntNum = -1;
		Move_Reset(clientNum);
		Bot2_RunnerForceReset(clientNum);
		movementFrame[clientNum] = -1;
		// Setup alone (BotAISetupClient clears the bot state first): the corner, cost and defender fixtures and the
		// map load reset a live bot, whose stranded timer stands.
		if (!botstates[clientNum] || !botstates[clientNum]->inuse) strandedSince[clientNum] = 0;
	}
}

// Scans all connected clients and assigns the Bot2_Think hook to any bot named
// *_v2 (or any bot under bot_forcebot2) that is a runner or a fixture defender;
// any other bot keeps the stock bot AI. Only a bot with no think of its own: a
// fixture that idles one (route play's hold, the bench hold) keeps it until it
// hands the think back.
void Bot2_UpdateManagedBots(void) {
	// Every Bot2 bot steps as Bot2_PmoveFixed() predicts, whatever bot_movementOnly
	// says: it runs from BotAIStartFrame in every mode, before the bots' ClientThink.
	int fixed = trap->Cvar_VariableIntegerValue("bot_pmoveFixed") != 0;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		gentity_t* ent = &g_entities[i];
		// Make sure they are a bot and actively connected
		if (ent->inuse && ent->client && (ent->r.svFlags & SVF_BOT)) {
			if (botstates[i] && !botstates[i]->customThink) {
				char userinfo[MAX_INFO_STRING];
				trap->GetUserinfo(i, userinfo, sizeof(userinfo));
				char* name = Info_ValueForKey(userinfo, "name");
				
				// Identify Bot2 via naming convention or cvar override
				int forceBot2 = trap->Cvar_VariableIntegerValue("bot_forcebot2");
				if ((forceBot2 > 0 || (name && strstr(name, "_v2"))) && (Bot2_IsMovementOnly(i) || Bot2_DefenderIs(i))) {
					botstates[i]->customThink = Bot2_Think;
					trap->Print("Bot2 AI initialized for %s (Slot %d)\n", name ? name : "Unknown", i);
				}
			}
			if (botstates[i] && botstates[i]->inuse && botstates[i]->customThink == Bot2_Think)
				ent->client->pers.pmoveFixed = fixed;
		}
	}
}

// Telemetry Queue Filter (Bitmask via bot_telemetry CVAR)
// Bit 1 (1) = Movement/Jump, Bit 2 (2) = Combat/Aiming
void Bot2_PrintTelemetry(int mode, const char* format, ...) {
	int cvar_mode = trap->Cvar_VariableIntegerValue("bot_telemetry");
	if (!(cvar_mode & mode)) return;

	va_list argptr;
	char string[1024];

	va_start(argptr, format);
	Q_vsnprintf(string, sizeof(string), format, argptr);
	va_end(argptr);

	trap->Print("%s", string);   // the text can hold player names: never a format
}

// ==============================================================================
// Per-frame entry point installed onto a managed bot's customThink pointer by
// Bot2_UpdateManagedBots: the runner (Bot2_MovementOnlyCommand) or a fixture
// defender (Bot2_DefenderCommand), one command per game frame. A bot that is
// neither any more (bot_movementOnly changed) goes back to the stock bot AI.
// ==============================================================================
void Bot2_Think(int clientNum, int time) {
	gentity_t* ent = &g_entities[clientNum]; usercmd_t ucmd; char serverCmd[1024]; double meterBegan;
	if(trap->Cvar_VariableIntegerValue("bot_movementOnly")==2 && !playDispatch)return;
	memset(&ucmd, 0, sizeof(ucmd));

	if (!ent || !ent->inuse || !ent->client || !botstates[clientNum]) return;
	if (level.intermissiontime || level.time == 0) return;
	if (ent->client->pers.connected != CON_CONNECTED) return;

	if (Bot2_IsMovementOnly(clientNum)) {
		while (trap->BotGetServerCommand(clientNum, serverCmd, sizeof(serverCmd))) {}
		// Listen servers call bot AI at render cadence, including repeated
		// calls against the same world state. Keep one decision per game frame;
		// ClientRunThink retains and consumes that command on the native tick.
		if (movementFrame[clientNum] == level.time) return;
		RunnerCapKill(clientNum);
		PitKill(clientNum);
		StrandedKill(clientNum);
		meterBegan = Bot2_FrameMeterClock();
		Bot2_MovementOnlyCommand(clientNum, level.time, &ucmd);
		Bot2_FrameMeterAdd(clientNum, meterBegan);
		movementFrame[clientNum] = level.time;
		Bot2_PlayLogSample(clientNum, &ucmd);
		botstates[clientNum]->lastucmd = ucmd;
		trap->BotUserCommand(clientNum, &ucmd);
		return;
	}
	// The declared fixture defender (bot2_defender.c) is commanded like the
	// runner: one command per game frame, logged, nothing else steering it.
	if (Bot2_DefenderIs(clientNum)) {
		while (trap->BotGetServerCommand(clientNum, serverCmd, sizeof(serverCmd))) {}
		if (movementFrame[clientNum] == level.time) return;
		PitKill(clientNum);
		StrandedKill(clientNum);
		meterBegan = Bot2_FrameMeterClock();
		Bot2_DefenderCommand(clientNum, level.time, &ucmd);
		Bot2_FrameMeterAdd(clientNum, meterBegan);
		movementFrame[clientNum] = level.time;
		Bot2_PlayLogSample(clientNum, &ucmd);
		botstates[clientNum]->lastucmd = ucmd;
		trap->BotUserCommand(clientNum, &ucmd);
		return;
	}
	movementFrame[clientNum] = -1;
	botstates[clientNum]->customThink = NULL;
}
