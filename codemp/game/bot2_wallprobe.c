// ==============================================================================
// bot2_wallprobe.c - wall-move probe (bot_moveWallProbe), a fixture diagnostic.
//
// Wall moves (the side wall run, the run up a wall and their jump-offs) are
// native Pmove against static walls, so the forecast rolls them out exactly
// (docs/movement/records/2026-09-24-wall-moves.md). This probe measures what
// they reach from one declared pose before any planner edge is built on them:
// the bot is placed at the pose for one boundary, a fixed sweep of scripted
// inputs is rolled out from it, one [WALLPROBE] line is printed per rollout
// and the live state is restored. It never commands the live bot.
//
//   bot_moveWallProbe      "x y z yaw [speed]": the pose (a standing origin)
//                          and the view yaw; speed is an initial velocity
//                          along the yaw. Several poses are separated by ','.
//                          Runs at the bot's first normal boundary standing
//                          on the world (not a mover)
//                          and is cleared once every pose has been swept.
//   bot_moveWallProbeTrace "jump repress": also prints every frame of the
//                          rollouts with those inputs ([WALLPROBEF] lines).
//   bot_moveWallProbeMode  0 runs up the wall facing it (jump, release,
//                          re-press, forward held); 1 side wall runs along it
//                          (forward plus strafe toward it, one jump).
// ==============================================================================

#include "g_local.h"
#include "bot2_forecast.h"
#include "bot2_wallprobe.h"
#include "ai_bot2.h"

// Each probe rolls out 5000 ms (200 frames at 25 ms), within the forecast's step capacity.
#define PROBE_MS 5000
#define PROBE_STEPS BOT2_FORECAST_MAX_STEPS

typedef struct {
    int frame,mode,jump,repress,side;
    float yaw;
} probePolicy_t;

static bot2_forecastFrame_t probeFrames[PROBE_STEPS];

static bot2_forecastCommandStatus_t ProbeCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *policyExtra,int serverTime,const void *context,usercmd_t *cmd) {
    probePolicy_t *p=(probePolicy_t*)policyExtra;
    (void)policy;(void)context;
    memset(cmd,0,sizeof(*cmd));
    cmd->serverTime=serverTime;cmd->weapon=ps->weapon;
    cmd->angles[YAW]=ANGLE2SHORT(p->yaw)-ps->delta_angles[YAW];
    cmd->angles[PITCH]=-ps->delta_angles[PITCH];
    cmd->forwardmove=127;
    if(p->mode==0) {
        if(p->frame==p->jump || p->frame==p->jump+p->repress)cmd->upmove=127;
    } else {
        cmd->rightmove=p->side>0?127:-127;
        if(p->frame==p->jump)cmd->upmove=127;
    }
    p->frame++;
    return BOT2_FORECAST_COMMAND;
}

static qboolean ProbePose(const char *text,float *pose,float *speed) {
    int n;
    *speed=0;
    n=sscanf(text,"%f %f %f %f %f",&pose[0],&pose[1],&pose[2],&pose[3],speed);
    if(n<4) {trap->Print("[WALLPROBE] bad pose '%s'; want x y z yaw [speed]\n",text);return qfalse;}
    return qtrue;
}

static void ProbeOne(gentity_t *ent,pmove_t *move,const float *pose,float speed,const probePolicy_t *start) {
    bot2_forecastRequest_t request;bot2_forecastResult_t result;bot2_maneuverState_t policy;
    probePolicy_t extra=*start;
    float maxZ=-1e9f,maxZFrame=0;int i,climb=0,run=0,flip=0,firstClimb=-1;
    memset(&request,0,sizeof(request));memset(&policy,0,sizeof(policy));
    request.entity=ent;request.root=move;request.policy=policy;
    request.policyExtra=&extra;request.policyExtraSize=sizeof(extra);
    request.command=ProbeCommand;request.context=NULL;
    request.firstTime=level.time;request.stepMsec=Bot2_GameStepMsec();
    request.maxSteps=min(Bot2_StepsFor(PROBE_MS),PROBE_STEPS);request.frameCapacity=PROBE_STEPS;request.frames=probeFrames;
    request.modelForce=qtrue;
    Bot2_ForecastRun(&request,&result);
    {char text[32];int tj=-1,tr=-1;
    trap->Cvar_VariableStringBuffer("bot_moveWallProbeTrace",text,sizeof(text));
    if(text[0] && sscanf(text,"%d %d",&tj,&tr)==2 && tj==start->jump && (start->mode!=0 || tr==start->repress))
        for(i=0;i<result.count;i++) {
            const playerState_t *a=&probeFrames[i].afterPS;const usercmd_t *c=&probeFrames[i].command;
            trap->Print("[WALLPROBEF] x=%.1f y=%.1f jump=%d repress=%d i=%d fwd=%d right=%d up=%d rise=%.1f y_pos=%.1f vz=%.1f vxy=%.1f "
                "legs=%d legs_timer=%d ground=%d pm_flags=%d force=%d active=%d\n",
                pose[0],pose[1],start->jump,start->repress,i,c->forwardmove,c->rightmove,c->upmove,a->origin[2]-pose[2],a->origin[1],
                a->velocity[2],sqrtf(a->velocity[0]*a->velocity[0]+a->velocity[1]*a->velocity[1]),
                a->legsAnim,a->legsTimer,a->groundEntityNum,a->pm_flags,a->fd.forcePower,a->fd.forcePowersActive);
        }}
    for(i=0;i<result.count;i++) {
        const playerState_t *a=&probeFrames[i].afterPS;int legs=a->legsAnim;
        if(a->origin[2]>maxZ) {maxZ=a->origin[2];maxZFrame=i;}
        if(legs==BOTH_FORCEWALLRUNFLIP_START) {climb=1;if(firstClimb<0)firstClimb=i;}
        if(legs==BOTH_FORCEWALLRUNFLIP_END || legs==BOTH_FORCEWALLRUNFLIP_ALT)flip=1;
        if(legs==BOTH_WALL_RUN_LEFT || legs==BOTH_WALL_RUN_RIGHT)run=1;
    }
    trap->Print("[WALLPROBE] mode=%d x=%.1f y=%.1f z=%.1f yaw=%.1f speed=%.1f jump=%d repress=%d side=%d "
        "status=%d completion=%d frames=%d unsupported=%u climb=%d climb_frame=%d run=%d flip=%d "
        "max_rise=%.1f max_frame=%.0f end_x=%.1f end_y=%.1f end_rise=%.1f end_ground=%d end_speed=%.1f force=%d\n",
        start->mode,pose[0],pose[1],pose[2],pose[3],speed,start->jump,start->repress,start->side,
        result.status,result.completion,result.count,result.unsupported,climb,firstClimb,run,flip,
        maxZ-pose[2],maxZFrame,result.terminalPS.origin[0],result.terminalPS.origin[1],result.terminalPS.origin[2]-pose[2],
        result.terminalPS.groundEntityNum,sqrtf(result.terminalPS.velocity[0]*result.terminalPS.velocity[0]+
        result.terminalPS.velocity[1]*result.terminalPS.velocity[1]),result.terminalPS.fd.forcePower);
}

void Bot2_WallProbe(gentity_t *ent,pmove_t *move) {
    static const int jumps[]={0,2,4,6,8,10,12,14,16,20,24};
    static const int represses[]={1,2,3,4,6,8,12};
    static const int runJumps[]={0,2,4,8,12,16};
    playerState_t *ps,saved;vec3_t savedAngles,savedEntAngles,angles;float pose[4],speed;
    char text[1024],*cursor,*next;int mode,a,b,runs=0;probePolicy_t p;
    static int emptyAt=-1;   // the setting was empty this frame: read it once a frame, not at every boundary
    if(!ent || !ent->client || !(ent->r.svFlags&SVF_BOT) || emptyAt==level.time)return;
    trap->Cvar_VariableStringBuffer("bot_moveWallProbe",text,sizeof(text));
    if(!text[0]) {emptyAt=level.time;return;}
    if( ent->client->ps.pm_type!=PM_NORMAL || ent->client->ps.groundEntityNum!=ENTITYNUM_WORLD)return;
    trap->Cvar_Set("bot_moveWallProbe","");
    mode=trap->Cvar_VariableIntegerValue("bot_moveWallProbeMode");
    ps=&ent->client->ps;saved=*ps;VectorCopy(ps->viewangles,savedAngles);VectorCopy(ent->s.angles,savedEntAngles);
    for(cursor=text;cursor && *cursor;cursor=next) {
    next=strchr(cursor,',');if(next)*next++=0;
    if(!ProbePose(cursor,pose,&speed))continue;
    VectorSet(ps->origin,pose[0],pose[1],pose[2]);
    ps->velocity[0]=speed*cosf(DEG2RAD(pose[3]));ps->velocity[1]=speed*sinf(DEG2RAD(pose[3]));ps->velocity[2]=0;
    ps->groundEntityNum=ENTITYNUM_WORLD;ps->pm_flags&=~(PMF_JUMP_HELD|PMF_TIME_KNOCKBACK|PMF_TIME_WATERJUMP);ps->pm_time=0;
    VectorSet(angles,0,pose[3],0);SetClientViewAngle(ent,angles);
    memset(&p,0,sizeof(p));p.mode=mode;p.yaw=pose[3];
    if(mode==0) {
        for(a=0;a<(int)ARRAY_LEN(jumps);a++)for(b=0;b<(int)ARRAY_LEN(represses);b++) {
            p.jump=jumps[a];p.repress=represses[b];p.frame=0;ProbeOne(ent,move,pose,speed,&p);runs++;
        }
    } else {
        for(a=0;a<(int)ARRAY_LEN(runJumps);a++)for(b=-1;b<=1;b+=2) {
            p.jump=runJumps[a];p.side=b;p.frame=0;ProbeOne(ent,move,pose,speed,&p);runs++;
        }
    }
    *ps=saved;
    }
    SetClientViewAngle(ent,savedAngles);*ps=saved;VectorCopy(savedEntAngles,ent->s.angles);
    trap->Print("[WALLPROBE] done mode=%d runs=%d\n",mode,runs);
}
