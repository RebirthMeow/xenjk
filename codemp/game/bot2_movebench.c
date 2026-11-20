// The Pmove boundary every client's movement passes through, and the
// fixtures' pre-start hold. The single-bot movement probe this file once held
// (bot_movebench) was removed with the pre-rewrite runner stack;
// its history is in docs/archive/strafe-movement-2026-09-14/.
#include "g_local.h"
#include "bot2_probe.h"
#include "bot2_threat.h"
#include "bot2_route_record.h"
#include "ai_main.h"
#include "bot2_movebench.h"
#include "bot2_movecost.h"
#include "bot2_move.h"
#include "bot2_finecmd.h"

extern void ClientThink_real(gentity_t *ent);

static int holdBeforeStart = -1, holdReadAt = -1;

static void IdleThink(int client, int time) {
    char command[1024];(void)time;
    // Retain the ordinary bot's reliable-command acknowledgement while its
    // autonomous input is paused, including long multi-client setup windows.
    while(trap->BotGetServerCommand(client,command,sizeof(command))) {}
}

// bot_movebench_hold (read once a game frame; it was read once per load, so
// addrunner's 0 could not clear a world started held): a fixture's bots stand
// idle until the fixture takes them (the cost fixture, Bot2_MoveCostClient,
// runs first). A bot this idled gets its own think back when the hold ends.
qboolean Bot2_MoveBenchRunClient(gentity_t *ent) {
    usercmd_t cmd;
    int i;
    if (holdReadAt != level.time) {
        holdReadAt = level.time;
        holdBeforeStart = trap->Cvar_VariableIntegerValue("bot_movebench_hold");
    }
    if (!holdBeforeStart && botstates[ent->s.number] && botstates[ent->s.number]->customThink == IdleThink)
        botstates[ent->s.number]->customThink = NULL;
    if (!holdBeforeStart || !(ent->r.svFlags & SVF_BOT) || !ent->client) return qfalse;
    // Opt-in solo runner preamble: no autonomous navigation before arming.
    if (botstates[ent->s.number]) botstates[ent->s.number]->customThink = IdleThink;
    memset(&cmd, 0, sizeof(cmd));
    cmd.serverTime = level.time; cmd.weapon = ent->client->ps.weapon;
    for (i = 0; i < 3; i++) cmd.angles[i] = ANGLE2SHORT(ent->client->ps.viewangles[i]) - ent->client->ps.delta_angles[i];
    ClientThink(ent->s.number, &cmd);
    ClientThink_real(ent);
    return qtrue;
}

void Bot2_MoveBenchPmove(gentity_t *ent, pmove_t *move) {
    double prepareBegan,meterBegan;
    // The whole preparation boundary is charged to the cost fixture's
    // current frame and client: refusals, first-use load, binding, window
    // build, prefixes, candidate rollouts and publication alike. Inert
    // unless a cost capture is active.
    prepareBegan=Bot2_MoveCostClock();
    meterBegan=ent->r.svFlags&SVF_BOT?Bot2_FrameMeterClock():0;
    if(ent->r.svFlags&SVF_BOT){Bot2_ThreatHideBegin();Bot2_ProbeBegin("plan",ent->s.number);}
    Bot2_MoveCostPreparation(ent->s.number,prepareBegan);
    Bot2_MoveParityBeforePmove(ent,move);
    if(ent->r.svFlags&SVF_BOT){Bot2_ProbeEnd();Bot2_ThreatHideEnd();}
    Bot2_FrameMeterAdd(ent->s.number,meterBegan);
    RouteRecord_Before(ent,move);
    if(ent->r.svFlags&SVF_BOT)Bot2_ProbeBegin("pmove",ent->s.number);
    Bot2_Pmove(move);   // 8 ms slices only for wall climbs
    if(ent->r.svFlags&SVF_BOT)Bot2_ProbeEnd();
    RouteRecord_AfterPmove(ent,move);
    Bot2_MoveParityAfterPmove(ent,move);
}
