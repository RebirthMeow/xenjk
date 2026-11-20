// ==============================================================================
// bot2_finecmd.c - 8 ms commands for wall climbs, 25 ms for everything else.
//
// Pmove splits one command into pmove_msec substeps that share pm->cmd. The run
// up a wall clears pm->cmd.forwardmove when it starts and on every step it
// continues, and continues only while forwardmove > 0 (bg_pmove.c,
// PM_CheckJump and PM_AdjustAngleForWallRunUp), so a bot's 25 ms command (three
// or four substeps) always cancels its own climb on the next substep
// (docs/movement/records/2026-09-24-wall-moves.md). A client sending a command
// every pmove_msec - a human at 125 fps - gets a fresh command per substep.
//
// Bots keep one command per server frame except
// while climbing a wall or entering a climb, where the same command is issued
// again every pmove_msec, exactly as a client holding the keys would send it.
// Every other command reaches Pmove unchanged, so nothing else moves. The live
// move and the forecast both call Bot2_Pmove, so a rollout slices exactly where
// the live bot does.
// ==============================================================================

#include "g_local.h"
#include "bot2_finecmd.h"

// The run-up entry's own wall test (PM_CheckJump): a box from the origin to
// 24 above it, 32 units along the view yaw, MASK_SOLID, and a wall facing back
// within about 45 degrees. The box is the client's native +-15: this runs
// before Pmove, and ClientThink leaves a client's pm->mins/maxs zero until
// PM_CheckDuck sets them inside it. Native tests on every substep after the
// view update, so this uses the yaw the command sets and looks as far ahead
// as the frame travels: a superset of native's test, and a
// frame it slices where no climb starts is what a 125 fps client sends.
static qboolean WallAhead(const pmove_t *pm) {
    vec3_t fwd,to,mins,maxs,angles,ideal;trace_t trace;float reach;
    int frameMs=pm->cmd.serverTime-pm->ps->commandTime;
    VectorSet(mins,-15,-15,0);VectorSet(maxs,15,15,24);
    VectorSet(angles,0,SHORT2ANGLE(pm->cmd.angles[YAW]+pm->ps->delta_angles[YAW]),0);
    AngleVectors(angles,fwd,NULL,NULL);
    reach=32+(frameMs>0?sqrtf(pm->ps->velocity[0]*pm->ps->velocity[0]+pm->ps->velocity[1]*pm->ps->velocity[1])*frameMs/1000.0f:0);
    VectorMA(pm->ps->origin,reach,fwd,to);
    pm->trace(&trace,pm->ps->origin,mins,maxs,to,pm->ps->clientNum,MASK_SOLID);
    if(trace.fraction>=1.0f)return qfalse;
    VectorSubtract(pm->ps->origin,to,ideal);VectorNormalize(ideal);
    return DotProduct(trace.plane.normal,ideal)>0.7f;
}

qboolean Bot2_FineCommand(const pmove_t *pm) {
    const playerState_t *ps=pm->ps;
    if(!pm->pmove_fixed || pm->pmove_msec<=0)return qfalse;
    if(ps->clientNum<0 || ps->clientNum>=MAX_CLIENTS || !(g_entities[ps->clientNum].r.svFlags&SVF_BOT))return qfalse;
    if(ps->legsAnim==BOTH_FORCEWALLRUNFLIP_START)return qtrue;
    return ps->groundEntityNum==ENTITYNUM_NONE && pm->cmd.upmove>0 && pm->cmd.forwardmove>0 &&
        !(ps->pm_flags&PMF_JUMP_HELD) && ps->fd.forcePowerLevel[FP_LEVITATION]>FORCE_LEVEL_1 &&
        ps->fd.forceRageRecoveryTime<pm->cmd.serverTime &&
        (ps->legsAnim==BOTH_JUMP1 || ps->legsAnim==BOTH_INAIR1) && WallAhead(pm);
}

void Bot2_Pmove(pmove_t *pm) {
    usercmd_t cmd;int finalTime;
    if(!Bot2_FineCommand(pm)) {Pmove(pm);return;}
    cmd=pm->cmd;finalTime=cmd.serverTime;
    while(pm->ps->commandTime<finalTime) {
        int before=pm->ps->commandTime;
        pm->cmd=cmd;
        pm->cmd.serverTime=min(finalTime,before+pm->pmove_msec);
        Pmove(pm);
        if(pm->ps->commandTime==before)break;   // Pmove refused the slice
    }
}
