// One-time declared setup intervention for isolated native corner reproductions.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "bot2_corner.h"
#include "bot2_route_record.h"
#include "bot2_move.h"
#include "bot2_graph.h"
#include "bot2_kick.h"
#include "bot2_defender.h"
#include "bot2_movecost.h"

// Declared pursuing body proxy (bot_cornerShadowSpeed>0 with block): each game
// frame it translates at most speed*dt along the floor toward a standing point
// `gap` units ahead of the runner's horizontal velocity (facing yaw when nearly
// stopped), limited by ordinary player-hull collision and a native-step floor
// check. It never teleports, is never removed, holds beyond a 512-unit leash or
// while the runner is dead, and has no AI, resources, damage or pushing.
#define SHADOW_LEASH 512
static struct {
    int client,speed,gap,nextPrint,contacts,moves,partialMoves,blockedMoves,floorRefusals,holds;
    float travelled;
} shadow;

static void ShadowTouch(gentity_t *self,gentity_t *other,trace_t *trace) {
    (void)self;(void)trace;
    if(other->s.number==shadow.client && !shadow.contacts++)
        trap->Print("[CORNER] SHADOW_CONTACT level=%d client=%d\n",level.time,other->s.number);
}

static qboolean ShadowFloor(gentity_t *self,const vec3_t dest,vec3_t standing) {
    vec3_t top,bottom;trace_t floor;
    VectorCopy(dest,top);top[2]+=STEPSIZE;VectorCopy(dest,bottom);bottom[2]-=STEPSIZE;
    trap->Trace(&floor,top,self->r.mins,self->r.maxs,bottom,self->s.number,MASK_PLAYERSOLID,qfalse,0,0);
    if(floor.startsolid || floor.allsolid || floor.fraction==1 || floor.entityNum!=ENTITYNUM_WORLD ||
       floor.plane.normal[2]<.7f)return qfalse;
    VectorCopy(floor.endpos,standing);return qtrue;
}

static void ShadowThink(gentity_t *self) {
    gentity_t *runner=&g_entities[shadow.client];playerState_t *ps=NULL;
    vec3_t current,target,dest,standing,direction;trace_t sweep;
    float dt=(level.time-level.previousTime)*.001f,dx,dy,d,step,speed2,c,s,dot;
    const char *outcome="moved";
    self->nextthink=level.time+1;
    VectorCopy(self->r.currentOrigin,current);
    if(!runner->inuse || !runner->client || runner->health<=0 || runner->client->ps.pm_type!=PM_NORMAL) {
        shadow.holds++;outcome="hold_runner";
    } else {
        ps=&runner->client->ps;
        speed2=ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1];
        if(speed2>=40*40) {d=sqrtf(speed2);c=ps->velocity[0]/d;s=ps->velocity[1]/d;}
        else {c=cosf(ps->viewangles[YAW]*M_PI/180);s=sinf(ps->viewangles[YAW]*M_PI/180);}
        dx=ps->origin[0]-current[0];dy=ps->origin[1]-current[1];
        if(dx*dx+dy*dy>SHADOW_LEASH*SHADOW_LEASH) {shadow.holds++;outcome="hold_leash";}
        else {
            target[0]=ps->origin[0]+c*shadow.gap;target[1]=ps->origin[1]+s*shadow.gap;
            dx=target[0]-current[0];dy=target[1]-current[1];d=sqrtf(dx*dx+dy*dy);
            if(d<1)outcome="in_place";
            else if(dt<=0)outcome="no_step";
            else {
                step=min(d,shadow.speed*dt);
                VectorSet(direction,dx/d,dy/d,0);
                VectorMA(current,step,direction,dest);
                trap->Trace(&sweep,current,self->r.mins,self->r.maxs,dest,self->s.number,MASK_PLAYERSOLID,qfalse,0,0);
                if(!sweep.startsolid && !sweep.allsolid && sweep.fraction<1) {
                    // One ordinary slide along the contacted plane, then stop.
                    dot=DotProduct(direction,sweep.plane.normal);
                    VectorMA(direction,-dot,sweep.plane.normal,direction);direction[2]=0;
                    VectorMA(sweep.endpos,step*(1-sweep.fraction),direction,dest);
                    VectorCopy(sweep.endpos,current);
                    trap->Trace(&sweep,current,self->r.mins,self->r.maxs,dest,self->s.number,MASK_PLAYERSOLID,qfalse,0,0);
                    outcome="partial";
                }
                if(sweep.startsolid || sweep.allsolid) {shadow.blockedMoves++;outcome="blocked";}
                else if(!ShadowFloor(self,sweep.endpos,standing)) {shadow.floorRefusals++;outcome="floor_refused";}
                else {
                    shadow.travelled+=sqrtf((standing[0]-self->r.currentOrigin[0])*(standing[0]-self->r.currentOrigin[0])+
                                            (standing[1]-self->r.currentOrigin[1])*(standing[1]-self->r.currentOrigin[1]));
                    G_SetOrigin(self,standing);trap->LinkEntity((sharedEntity_t*)self);
                    if(!strcmp(outcome,"partial"))shadow.partialMoves++;else shadow.moves++;
                }
            }
        }
    }
    if(level.time>=shadow.nextPrint) {
        shadow.nextPrint=level.time+250;
        trap->Print("[CORNER] SHADOW level=%d x=%.3f y=%.3f z=%.3f runner_distance=%.3f outcome=%s travelled=%.1f contacts=%d moves=%d partial=%d blocked=%d floor_refusals=%d holds=%d\n",
            level.time,self->r.currentOrigin[0],self->r.currentOrigin[1],self->r.currentOrigin[2],
            ps?Distance(ps->origin,self->r.currentOrigin):-1.0f,outcome,shadow.travelled,shadow.contacts,
            shadow.moves,shadow.partialMoves,shadow.blockedMoves,shadow.floorRefusals,shadow.holds);
    }
}

// A pinned start class (bot_cornerDefer "class min max") is chosen by the
// game, not by the fixture's rcon timing. Over rcon the start lands on the
// last play-log row the runner saw, and a world's rows form a 250 ms ladder
// that meets a given class of 200 in only about half the worlds, so half the
// pinned worlds were retired unstarted. Deferred, the command's arguments are
// kept and the start runs at the end of the first frame at or after `min`
// whose level is in the class: the same point an rcon command reaches, after
// frame L and before the bots think for L+25. Every class is a frame multiple
// (25 ms), so one lies within any 200 ms window.
#define CORNER_CLASS_MODULUS 200
#define CORNER_MAX_ARGS 10
static struct {
    qboolean pending,running;
    int argc,klass,minimum,maximum,requested;
    char argv[CORNER_MAX_ARGS][128];
} deferred;

static int CornerArgc(void) {return deferred.running?deferred.argc:trap->Argc();}
static void CornerArgv(int n,char *buffer,int size) {
    if(!deferred.running) {trap->Argv(n,buffer,size);return;}
    Q_strncpyz(buffer,n<deferred.argc?deferred.argv[n]:"",size);
}

static void CornerStart(void) {
    static int usedStart=-1;
    int i,client=-1,count=0,force,carrying,power,speedExpiry=0,speedDeadline=0,argc=CornerArgc();
    int shadowSpeed=0,shadowGap=36,health=0,air=0;
    qboolean drop=argc==9,block=argc==10,overlap;
    char text[128],speedText[128],yawText[128],*end,map[MAX_QPATH];double values[8],yaw=0;long parsedExpiry,parsed;
    gentity_t *ent,*flag=NULL,*dropped,*blocker;playerState_t *ps;
    vec3_t origin,bottom,angles={0,0,0},velocity={0,0,0};trace_t stationary,floor,pickup;
    vec3_t dropOrigin,dropMins,dropMaxs,dropVelocity={0,0,0};trace_t dropTrace;
    vec3_t blockOrigin;trace_t blockTrace;
    if(!dedicated.integer || !trap->Cvar_VariableIntegerValue("sv_cheats") ||
       usedStart==level.startTime || !trap->Cvar_VariableIntegerValue("bot_movementOnly") ||
       level.gametype!=GT_CTF || (argc!=6 && argc!=9 && argc!=10)) {
        trap->Print("[CORNER] REFUSED requires unused dedicated cheats CTF movement-only world; bot_corner_start x y z force carrying [drop_x drop_y drop_z | block bx by bz]\n");return;
    }
    if(block) {
        CornerArgv(6,text,sizeof(text));
        if(Q_stricmp(text,"block")) {trap->Print("[CORNER] REFUSED expected block keyword\n");return;}
    }
    trap->Cvar_VariableStringBuffer("bot_cornerYaw",yawText,sizeof(yawText));
    if(yawText[0]) {
        yaw=strtod(yawText,&end);
        if(end==yawText || *end || !(yaw>=-360 && yaw<=360)) {
            trap->Print("[CORNER] REFUSED bot_cornerYaw finite number -360..360 required\n");return;
        }
    }
    // A rest start cannot reproduce a landing turn, a post-overshoot reversal
    // or a refused takeoff: every one of those is defined by its momentum.
    // The pose checks below still decide whether the start is legal at all.
    trap->Cvar_VariableStringBuffer("bot_cornerVelocity",text,sizeof(text));
    if(text[0]) {
        char *cursor=text;
        for(i=0;i<3;i++) {
            double component=strtod(cursor,&end);
            if(end==cursor || !(component>=-1500 && component<=1500)) {
                trap->Print("[CORNER] REFUSED bot_cornerVelocity requires three finite components in -1500..1500\n");return;
            }
            velocity[i]=(float)component;cursor=end;
        }
        while(*cursor==' ' || *cursor=='\t')cursor++;
        if(*cursor || VectorLength(velocity)>1500) {
            trap->Print("[CORNER] REFUSED bot_cornerVelocity requires three components with speed at most 1500\n");return;
        }
    }
    trap->Cvar_VariableStringBuffer("bot_cornerSpeedExpiry",speedText,sizeof(speedText));
    if(speedText[0]) {
        parsedExpiry=strtol(speedText,&end,10);
        if(*end || parsedExpiry<0 || parsedExpiry>60000) {
            trap->Print("[CORNER] REFUSED bot_cornerSpeedExpiry integer 0..60000 required\n");return;
        }
        speedExpiry=(int)parsedExpiry;
    }
    trap->Cvar_VariableStringBuffer("bot_cornerShadowSpeed",text,sizeof(text));
    if(text[0]) {
        parsed=strtol(text,&end,10);
        if(*end || parsed<0 || parsed>400) {
            trap->Print("[CORNER] REFUSED bot_cornerShadowSpeed integer 0..400 required\n");return;
        }
        shadowSpeed=(int)parsed;
    }
    trap->Cvar_VariableStringBuffer("bot_cornerShadowGap",text,sizeof(text));
    if(text[0]) {
        parsed=strtol(text,&end,10);
        if(*end || parsed<24 || parsed>96) {
            trap->Print("[CORNER] REFUSED bot_cornerShadowGap integer 24..96 required\n");return;
        }
        shadowGap=(int)parsed;
    }
    // bot_cornerHealth (fixture scenarios, unset: as it is): the runner's
    // health at a recorded moment.
    trap->Cvar_VariableStringBuffer("bot_cornerHealth",text,sizeof(text));
    if(text[0]) {
        parsed=strtol(text,&end,10);
        if(*end || parsed<1 || parsed>200) {
            trap->Print("[CORNER] REFUSED bot_cornerHealth integer 1..200 required\n");return;
        }
        health=(int)parsed;
    }
    // bot_cornerAir 1 (fixture scenarios): the recorded moment is in flight;
    // the pose needs no floor under it and starts airborne.
    air=trap->Cvar_VariableIntegerValue("bot_cornerAir")==1;
    if(shadowSpeed && !block) {
        trap->Print("[CORNER] REFUSED shadow pursuit requires a declared block position\n");return;
    }
    for(i=0;i<(drop || block?8:5);i++) {
        CornerArgv(i+1+(block && i>=5),text,sizeof(text));values[i]=strtod(text,&end);
        if(!text[0] || *end || !(values[i]>=-131072 && values[i]<=131072)) {
            trap->Print("[CORNER] REFUSED invalid finite bounded number\n");return;
        }
    }
    force=(int)values[3];carrying=(int)values[4];
    if(values[3]!=force || force<0 || force>100 || values[4]!=carrying || carrying<0 || carrying>1) {
        trap->Print("[CORNER] REFUSED force 0..100 and carrying 0/1 required\n");return;
    }
    if(drop && carrying) {
        trap->Print("[CORNER] REFUSED dropped-flag setup requires carrying=0\n");return;
    }
    // A declared fixture defender (bot2_defender.c) is not the bot being placed.
    for(i=0;i<level.maxclients;i++)if(g_entities[i].inuse && g_entities[i].client &&
       g_entities[i].client->pers.connected!=CON_DISCONNECTED && !Bot2_DefenderIs(i)) {count++;client=i;}
    if(count!=1 || !(g_entities[client].r.svFlags&SVF_BOT) || !botstates[client]) {
        trap->Print("[CORNER] REFUSED expected exactly one bot besides a declared defender and no humans\n");return;
    }
    ent=&g_entities[client];ps=&ent->client->ps;
    if(ent->client->pers.connected!=CON_CONNECTED || ent->health<=0 || ps->pm_type!=PM_NORMAL ||
       (ent->client->sess.sessionTeam!=TEAM_RED && ent->client->sess.sessionTeam!=TEAM_BLUE) ||
       ps->powerups[PW_REDFLAG] || ps->powerups[PW_BLUEFLAG]) {
        trap->Print("[CORNER] REFUSED requires alive connected unladen team bot\n");return;
    }
    for(i=0;i<3;i++)origin[i]=(float)values[i];
    trap->Trace(&stationary,origin,ent->r.mins,ent->r.maxs,origin,client,MASK_PLAYERSOLID,qfalse,0,0);
    VectorCopy(origin,bottom);bottom[2]-=4;
    trap->Trace(&floor,origin,ent->r.mins,ent->r.maxs,bottom,client,MASK_PLAYERSOLID,qfalse,0,0);
    if(stationary.startsolid || stationary.allsolid || (!air && (floor.startsolid || floor.allsolid ||
       floor.fraction>=1 || floor.plane.normal[2]<.7f))) {
        trap->Print("[CORNER] REFUSED pose x=%.6f y=%.6f z=%.6f solid=%d floor_fraction=%.6f normal=%.6f\n",
            origin[0],origin[1],origin[2],stationary.startsolid||stationary.allsolid,floor.fraction,floor.plane.normal[2]);return;
    }
    if(drop) {
        for(i=0;i<3;i++)dropOrigin[i]=(float)values[i+5];
        VectorSet(dropMins,-ITEM_RADIUS,-ITEM_RADIUS,-ITEM_RADIUS);
        VectorSet(dropMaxs,ITEM_RADIUS,ITEM_RADIUS,ITEM_RADIUS);
        trap->Trace(&dropTrace,dropOrigin,dropMins,dropMaxs,dropOrigin,ENTITYNUM_NONE,
            MASK_PLAYERSOLID & ~CONTENTS_BODY,qfalse,0,0);
        if(dropTrace.startsolid || dropTrace.allsolid) {
            trap->Print("[CORNER] REFUSED dropped-flag bounding box is solid\n");return;
        }
    }
    if(block) {
        for(i=0;i<3;i++)blockOrigin[i]=(float)values[i+5];
        // The only client's old position is vacated by this declared setup.
        trap->Trace(&blockTrace,blockOrigin,ent->r.mins,ent->r.maxs,blockOrigin,client,
            MASK_PLAYERSOLID,qfalse,0,0);
        overlap=qtrue;
        for(i=0;i<3;i++)
            if(blockOrigin[i]+ent->r.maxs[i]<=origin[i]+ent->r.mins[i] ||
               blockOrigin[i]+ent->r.mins[i]>=origin[i]+ent->r.maxs[i])overlap=qfalse;
        if(blockTrace.startsolid || blockTrace.allsolid || overlap) {
            trap->Print("[CORNER] REFUSED blocker bbox solid=%d overlaps_declared_bot=%d\n",
                blockTrace.startsolid || blockTrace.allsolid,overlap);return;
        }
    }
    power=ent->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG;
    if(carrying || drop) {
        while((flag=G_Find(flag,FOFS(classname),power==PW_BLUEFLAG?"team_CTF_blueflag":"team_CTF_redflag")))
            if(!(flag->flags&FL_DROPPED_ITEM))break;
        if(!flag || !flag->item || !flag->r.linked || !(flag->r.contents&CONTENTS_TRIGGER) ||
           (flag->s.eFlags&(EF_NODRAW|EF_ITEMPLACEHOLDER)) || !BG_CanItemBeGrabbed(level.gametype,&flag->s,ps)) {
            trap->Print("[CORNER] REFUSED enemy stand unavailable for native pickup intervention\n");return;
        }
    }
    // All argument, world, pose and pickup eligibility checks precede mutation.
    usedStart=level.startTime;
    if(carrying || drop) {
        memset(&pickup,0,sizeof(pickup));Touch_Item(flag,ent,&pickup);
        if(!ps->powerups[power]) {trap->Print("[CORNER] FAILED native pickup; attempt consumed, no teleport\n");return;}
        if(drop) {
            dropped=LaunchItem(flag->item,dropOrigin,dropVelocity);
            ps->powerups[power]=0;
            trap->Print("[CORNER] DROP level=%d entity=%d power=%d x=%.6f y=%.6f z=%.6f vx=0 vy=0 vz=0 nextthink=%d native_gravity=1 native_return=1\n",
                level.time,dropped->s.number,power,dropOrigin[0],dropOrigin[1],dropOrigin[2],dropped->nextthink);
        }
    }
    trap->UnlinkEntity((sharedEntity_t*)ent);Bot2_ClearState(client);
    VectorCopy(origin,ps->origin);VectorCopy(velocity,ps->velocity);ps->groundEntityNum=air?ENTITYNUM_NONE:floor.entityNum;
    ps->fd.forcePowersActive=0;memset(ps->fd.forcePowerDuration,0,sizeof(ps->fd.forcePowerDuration));
    if(speedExpiry>0) {
        speedDeadline=level.time+speedExpiry;
        ps->fd.forcePowersActive|=1<<FP_SPEED;
        ps->fd.forcePowerDuration[FP_SPEED]=speedDeadline;
    }
    ps->fd.forcePower=force;ps->fd.forcePowerRegenDebounceTime=level.time+1000;
    if(health)ent->health=ps->stats[STAT_HEALTH]=health;
    ps->pm_flags&=~(PMF_JUMP_HELD|PMF_TIME_KNOCKBACK|PMF_TIME_WATERJUMP);ps->pm_time=0;
    angles[YAW]=AngleNormalize180((float)yaw);
    SetClientViewAngle(ent,angles);VectorCopy(origin,ent->r.currentOrigin);
    trap->LinkEntity((sharedEntity_t*)ent);
    if(block) {
        // Static collision proxy only: no client, AI, think, resources or combat.
        blocker=G_Spawn();blocker->classname="bot2_corner_blocker";
        blocker->s.eType=ET_GENERAL;
        VectorCopy(ent->r.mins,blocker->r.mins);VectorCopy(ent->r.maxs,blocker->r.maxs);
        blocker->r.contents=CONTENTS_BODY;
        G_SetOrigin(blocker,blockOrigin);trap->LinkEntity((sharedEntity_t*)blocker);
        trap->Print("[CORNER] BLOCKER level=%d entity=%d x=%.6f y=%.6f z=%.6f min_x=%.6f min_y=%.6f min_z=%.6f max_x=%.6f max_y=%.6f max_z=%.6f contents=%d static_body_proxy=%d combat_ai=0 lifetime=world\n",
            level.time,blocker->s.number,blockOrigin[0],blockOrigin[1],blockOrigin[2],
            blocker->r.mins[0],blocker->r.mins[1],blocker->r.mins[2],
            blocker->r.maxs[0],blocker->r.maxs[1],blocker->r.maxs[2],blocker->r.contents,!shadowSpeed);
        if(shadowSpeed) {
            memset(&shadow,0,sizeof(shadow));
            shadow.client=client;shadow.speed=shadowSpeed;shadow.gap=shadowGap;shadow.nextPrint=level.time+250;
            blocker->touch=ShadowTouch;blocker->think=ShadowThink;blocker->nextthink=level.time+1;
            trap->Print("[CORNER] SHADOW_START level=%d entity=%d client=%d speed=%d gap=%d leash=%d floor_step=%d pursuit=runner_velocity_or_yaw teleport=0 removal=0 combat_ai=0 resources=0\n",
                level.time,blocker->s.number,client,shadowSpeed,shadowGap,SHADOW_LEASH,STEPSIZE);
        }
    }
    trap->Cvar_VariableStringBuffer("mapname",map,sizeof(map));
    trap->Print("[CORNER] START map=%s level=%d client=%d team=%d x=%.6f y=%.6f z=%.6f velocity=%.6f vx=%.6f vy=%.6f vz=%.6f yaw=%.6f force=%d carrying=%d ground=%d health=%d speed_level=%d jump_level=%d active=%d speed_expiry_ms=%d speed_deadline=%d regen_delay=1000 native_pickup=%d reset_controllers=1 clear_jump_knockback=1\n",
        map,level.time,client,ent->client->sess.sessionTeam,origin[0],origin[1],origin[2],
        VectorLength(velocity),ps->velocity[0],ps->velocity[1],ps->velocity[2],ps->viewangles[YAW],force,carrying,
        ps->groundEntityNum,ent->health,ps->fd.forcePowerLevel[FP_SPEED],ps->fd.forcePowerLevel[FP_LEVITATION],
        !!(ps->fd.forcePowersActive&(1<<FP_SPEED)),speedExpiry,speedDeadline,carrying || drop);
    // The leg starts on the graph the world loaded, not on the graph the
    // bot's own pre-release wandering left behind: a strafe edge learned
    // before the release joins the state planner's routes and changes the
    // first post-release decision, which is why one pinned start class
    // returned three different completion times across ten unseeded worlds.
    Graph_ResetLearned();
    // A declared goal chain (bot_goalChain) starts at its first point here,
    // so pre-release wandering cannot pass any of its points.
    Bot2_GoalChainReset(client);
    // Last, after the release is declared: the recorder's root is this level,
    // and it is inert unless bot_routeRecord is 2. The decision tape's cursor
    // starts here too, so the bot's own pre-release wandering cannot consume
    // entries and a mid-trip reset cannot restart them.
    RouteRecord_ArmBot(client,level.time);
    Bot2_KickArm(client,level.time);   // inert unless bot_moveKick declares kicks
    // The leg starts here, so the tape library's key is formed here: the
    // bot's own leg, the cell this stand is in, and this frame's phase. The
    // fixture passes no tape and no key; it passes the case and the class.
    Move_DecisionTapeReset(client,Move_RootCell(origin),level.time%MOVE_TAPE_PHASE);
    // A declared defender takes its post at the same setup (none: nothing).
    Bot2_DefenderRelease();
    Bot2_FrameMeterReset();   // inert unless bot_moveFrameMeter
}

void Svcmd_BotCornerStart_f(void) {
    char text[128],*end;long values[3];int i,argc=trap->Argc();char *cursor=text;
    trap->Cvar_VariableStringBuffer("bot_cornerDefer",text,sizeof(text));
    if(!text[0]) {CornerStart();return;}
    for(i=0;i<3;i++) {
        values[i]=strtol(cursor,&end,10);
        if(end==cursor) break;
        cursor=end;
    }
    while(*cursor==' ' || *cursor=='\t')cursor++;
    if(i!=3 || *cursor || values[0]<0 || values[0]>=CORNER_CLASS_MODULUS || values[0]%25 ||
       values[1]<0 || values[2]<values[1]+CORNER_CLASS_MODULUS || values[2]>600000) {
        trap->Print("[CORNER] REFUSED bot_cornerDefer requires \"class min max\": class 0..175 in 25s, max at least min+200\n");return;
    }
    if(deferred.pending) {trap->Print("[CORNER] REFUSED a deferred start is already pending\n");return;}
    if(argc>CORNER_MAX_ARGS) {trap->Print("[CORNER] REFUSED too many arguments\n");return;}
    memset(&deferred,0,sizeof(deferred));
    deferred.argc=argc;
    for(i=0;i<argc;i++)trap->Argv(i,deferred.argv[i],sizeof(deferred.argv[i]));
    deferred.klass=(int)values[0];deferred.minimum=(int)values[1];deferred.maximum=(int)values[2];
    deferred.requested=level.time;deferred.pending=qtrue;
    trap->Print("[CORNER] DEFERRED level=%d class=%d min=%d max=%d\n",
        level.time,deferred.klass,deferred.minimum,deferred.maximum);
}

// End of G_RunFrame. The start still runs every check it runs over rcon.
void Bot2_CornerFrame(void) {
    if(!deferred.pending)return;
    if(level.time>deferred.maximum) {
        deferred.pending=qfalse;
        trap->Print("[CORNER] REFUSED deferred window passed level=%d class=%d max=%d\n",
            level.time,deferred.klass,deferred.maximum);
        return;
    }
    if(level.time<deferred.minimum || level.time%CORNER_CLASS_MODULUS!=deferred.klass)return;
    deferred.pending=qfalse;deferred.running=qtrue;
    CornerStart();
    deferred.running=qfalse;
}
