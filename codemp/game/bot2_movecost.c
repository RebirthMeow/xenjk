// Isolated actual-client workload. Commands use production movement dispatch;
// only setup and explicitly declared impulses edit player state.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "ai_bot2_internal.h"
#include "bot2_movecost.h"
#include <stdint.h>
#ifdef _WIN32
#undef INFINITE
#include <windows.h>
#endif

extern void ClientThink_real(gentity_t *ent);
static struct {
    int used,active,count,start,frameTime,seen,calls,alive,moving,frames,badClocks;
    // movementUs is the complete frame total: command dispatch plus every
    // preparation charged through Bot2_MoveCostPreparation. dispatchUs and
    // prepareUs are its two parts; forecastUs is the oracle-measured subpart of
    // prepareUs and is never added to the total a second time.
    double movementUs,dispatchUs,prepareUs,forecastUs,clockReadUs;
    double clientUs[MAX_CLIENTS];
    double sectionUs[BOT2_MOVECOST_SECTIONS];
    int counters[BOT2_MOVECOST_COUNTS];
    int prints,droppedPrints;
    fileHandle_t file,traceFile;
    int trace;
    // Declared per-client start holds (`bot_moveCostHold`, "client:ms ..."):
    // a held client stands at its pose with no movement command until the
    // declared elapsed time. Every workload is deterministic relative to its
    // start, so a hold shifts one client's whole trajectory by that much and
    // can bring two clients onto the same admitting root on the same frame -
    // the synchronised contention case the one-slot schedule has never seen.
    int hold[MAX_CLIENTS];
    struct { int selected,prepared,perturbed,picked; usercmd_t command;vec3_t goal; } clients[MAX_CLIENTS];
} cost;
// Keep diagnostic file I/O outside the recorded server-frame interval.
static char captureRows[800*2048];
static char traceRows[800*MAX_CLIENTS*512];
static int captureBytes,traceBytes,flushPending;
// Console diagnostics emitted inside the recorded interval (Bot2_MoveCostPrint)
// are held here and printed after recording, so a timing frame does not include console/pipe I/O.
static char printRows[256*1024];
static int printBytes;

static void FlushPrints(void) {
    int at=0;
    while(at<printBytes) {
        int length=(int)strlen(printRows+at);
        trap->Print("%s",printRows+at);at+=length+1;
    }
    printBytes=0;
}

static qboolean BufferTraceRow(int client,int elapsed,const playerState_t *before,
    int beforeHealth,const usercmd_t *cmd,const playerState_t *after,int afterHealth) {
    char line[512];int bytes;
    bytes=Com_sprintf(line,sizeof(line),
        "%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%s\n",
        level.time,elapsed,client,
        before->origin[0],before->origin[1],before->origin[2],before->velocity[0],before->velocity[1],before->velocity[2],
        before->groundEntityNum,beforeHealth,before->fd.forcePower,(unsigned)before->fd.forcePowersActive,
        after->origin[0],after->origin[1],after->origin[2],after->velocity[0],after->velocity[1],after->velocity[2],
        after->groundEntityNum,afterHealth,after->fd.forcePower,(unsigned)after->fd.forcePowersActive,
        cmd->forwardmove,cmd->rightmove,cmd->upmove,cmd->buttons,cmd->generic_cmd,cmd->angles[0],cmd->angles[1],cmd->angles[2],
        cost.clients[client].goal[0],cost.clients[client].goal[1],cost.clients[client].goal[2],bot2_states[client].targetEntNum,
        bot2_states[client].state,bot2_states[client].macroState,
        before->fallingToDeath,after->fallingToDeath,Bot2_MovementOwner(client));
    if(bytes<=0 || bytes>=(int)sizeof(line) || traceBytes+bytes>(int)sizeof(traceRows))return qfalse;
    memcpy(traceRows+traceBytes,line,bytes);traceBytes+=bytes;return qtrue;
}

static double ClockUs(void) {
#ifdef _WIN32
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart*1000000.0/frequency.QuadPart;
#else
    return trap->Milliseconds()*1000.0;
#endif
}
static void Idle(int client,int time) {
    char command[1024];(void)time;
    while(trap->BotGetServerCommand(client,command,sizeof(command))) {}
}

// bot_moveProfile (diagnostic, records/2026-10-04-tail-composition.md): the cost capture's section
// timers and counters run in any world, not only a cost fixture, and print as [PROFILE] beside [FRAMECOST]. Read once
// a frame with the frame meter (which must be on). Timing only; the per-phase clock reads inflate what they time.
static int profileMode;
#define COST_ON (cost.active || profileMode)
double Bot2_MoveCostClock(void) {return COST_ON?ClockUs():0;}
double Bot2_ClockUs(void) {return ClockUs();}
void Bot2_MoveCostSection(int section,double began) {
    if(COST_ON && began>0 && section>=0 && section<BOT2_MOVECOST_SECTIONS)cost.sectionUs[section]+=ClockUs()-began;
}
void Bot2_MoveCostPreparation(int client,double began) {
    double us;
    if(!cost.active || began<=0 || client<0 || client>=MAX_CLIENTS || !cost.clients[client].selected)return;
    // Preparation runs inside ClientThink_real for the same client and frame
    // the fixture just dispatched, after the reset at the top of
    // Bot2_MoveCostClient and before the frame row is written.
    us=ClockUs()-began;
    cost.prepareUs+=us;cost.movementUs+=us;cost.clientUs[client]+=us;
    cost.sectionUs[BOT2_MOVECOST_SECTION_PREPARE]+=us;
    cost.counters[BOT2_MOVECOST_COUNT_PREPARE_CALLS]++;
}
void Bot2_MoveCostAdd(int section,double us) {
    if(!COST_ON || section<0 || section>=BOT2_MOVECOST_SECTIONS || us<0)return;
    cost.sectionUs[section]+=us;
    if(section==BOT2_MOVECOST_SECTION_FORECAST)cost.forecastUs+=us;
}
void Bot2_MoveCostCount(int counter) {
    if(COST_ON && counter>=0 && counter<BOT2_MOVECOST_COUNTS)cost.counters[counter]++;
}
void Bot2_MoveCostCountAdd(int counter,int n) {
    if(COST_ON && counter>=0 && counter<BOT2_MOVECOST_COUNTS)cost.counters[counter]+=n;
}
void Bot2_MoveCostPrint(const char *format,...) {
    char line[1024];va_list args;int bytes;
    va_start(args,format);bytes=Q_vsnprintf(line,sizeof(line),format,args);va_end(args);
    if(!cost.active){trap->Print("%s",line);return;}
    if(bytes<0 || bytes>=(int)sizeof(line))bytes=(int)strlen(line);
    if(printBytes+bytes+1>(int)sizeof(printRows)){cost.droppedPrints++;return;}
    memcpy(printRows+printBytes,line,bytes+1);printBytes+=bytes+1;cost.prints++;
}

void Bot2_MoveCostShutdown(void) {
    // The server-frame timer (bot2_servercost.cpp) outlives the game module:
    // a capture cut by a map change must not leave it sampling.
    if(cost.active || trap->Cvar_VariableIntegerValue("bot_moveCostRecording")) {
        trap->Cvar_Set("bot_moveCostRecording","0");
        if(cost.active)trap->Print("[MOVECOST] REFUSED capture cut by shutdown\n");
        cost.active=0;
    }
}

void Svcmd_MoveCostStart_f(void) {
    if(!Bot2_MoveCostStart())trap->Print("[MOVECOST] REFUSED bot_moveCost is 0\n");
}

qboolean Bot2_MoveCostStart(void) {
    int wanted=trap->Cvar_VariableIntegerValue("bot_moveCost"),trace=trap->Cvar_VariableIntegerValue("bot_moveCostTrace"),i,count=0,ranks[3]={0,0,0};
    // Contention case (`bot_moveCostSameSpawn` = spawn index + 1): every
    // client of a team starts at one spawn, side by side, so distinct clients
    // present the same admitting roots on the same frames. Zero keeps the
    // distinct-spawn workload. A two-client workload (one per team) is the
    // decoupled contention world: measured: eight clients couple
    // through flag state, so holding one shifts every timeline.
    // One is the level-1 gate's row 5 as it was re-scoped
    // (one bot; strafe quality before multi-bot cost); four was
    // its earlier "at least four bots on one server" and stays available.
    // The other counts are the archived strafe engine's own workloads.
    int sameSpawn=trap->Cvar_VariableIntegerValue("bot_moveCostSameSpawn");
    // Blue may use a different spawn index (`bot_moveCostSameSpawnBlue`, 0 =
    // the same as red): the two-client contention world pairs whichever
    // red/blue timelines align on a 200 ms multiple.
    int sameSpawnBlue=trap->Cvar_VariableIntegerValue("bot_moveCostSameSpawnBlue");
    char holds[256];
    fileHandle_t existing=0;
    if(!wanted)return qfalse;
    if(cost.used || !dedicated.integer || !trap->Cvar_VariableIntegerValue("sv_cheats") ||
       (wanted!=1 && wanted!=2 && wanted!=4 && wanted!=8 && wanted!=16) || (trace!=0 && trace!=1) || trap->Cvar_VariableIntegerValue("sv_fps")!=40 ||
       sameSpawn<0 || sameSpawn>6 || sameSpawnBlue<0 || sameSpawnBlue>6 || (sameSpawnBlue && !sameSpawn)) {
        trap->Print("[MOVECOST] REFUSED invalid setup\n");return qtrue;
    }
    for(i=0;i<level.maxclients;i++)if(g_entities[i].inuse && g_entities[i].client) {
        gentity_t *ent=&g_entities[i];
        if(!(ent->r.svFlags&SVF_BOT) || !botstates[i] ||
           (ent->client->sess.sessionTeam!=TEAM_RED && ent->client->sess.sessionTeam!=TEAM_BLUE)) {
            trap->Print("[MOVECOST] REFUSED unexpected client\n");return qtrue;
        }
        count++;
    }
    if(count!=wanted){trap->Print("[MOVECOST] REFUSED expected %d actual %d clients\n",wanted,count);return qtrue;}
    trap->FS_Open("movement_cost.csv",&existing,FS_READ);
    if(existing){trap->FS_Close(existing);trap->Print("[MOVECOST] REFUSED existing capture\n");return qtrue;}
    trap->FS_Open("movement_cost.meta",&existing,FS_READ);
    if(existing){trap->FS_Close(existing);trap->Print("[MOVECOST] REFUSED existing metadata\n");return qtrue;}
    if(trace){trap->FS_Open("movement_cost_trace.csv",&existing,FS_READ);
        if(existing){trap->FS_Close(existing);trap->Print("[MOVECOST] REFUSED existing trace\n");return qtrue;}}
    memset(&cost,0,sizeof(cost));captureBytes=traceBytes=flushPending=printBytes=0;cost.used=1;cost.count=count;cost.trace=trace;
    trap->Cvar_VariableStringBuffer("bot_moveCostHold",holds,sizeof(holds));
    {
        const char *cursor=holds;
        while(*cursor) {
            int client,msec;
            while(*cursor==' ' || *cursor==',')cursor++;
            if(!*cursor)break;
            if(sscanf(cursor,"%d:%d",&client,&msec)!=2 || client<0 || client>=MAX_CLIENTS || msec<0 || msec>15000 || msec%25) {
                trap->Print("[MOVECOST] REFUSED hold declaration %s\n",holds);return qtrue;
            }
            cost.hold[client]=msec;
            trap->Print("[MOVECOST] HOLD client=%d until_elapsed=%d\n",client,msec);
            while(*cursor && *cursor!=' ' && *cursor!=',')cursor++;
        }
    }
    {
        // The accounting's own overhead, measured rather than assumed: one
        // clock read costs this much, and every charged interval spends two.
        // The forecast oracle's measureTime reads clocks around its Pmove
        // steps too; those reads are inside prepare_us, not removed from it.
        double began=ClockUs(),now=began;int reads;
        for(reads=0;reads<1000;reads++)now=ClockUs();
        cost.clockReadUs=(now-began)/1000.0;
    }
    // Preamble bots can occupy all ordinary respawns before the last clients
    // join. Initialize every fixture participant through native ClientSpawn,
    // with all participants unlinked until their distinct test poses are set.
    for(i=0;i<level.maxclients;i++)if(g_entities[i].inuse && g_entities[i].client)
        trap->UnlinkEntity((sharedEntity_t*)&g_entities[i]);
    for(i=0;i<level.maxclients;i++)if(g_entities[i].inuse && g_entities[i].client) {
        gentity_t *ent=&g_entities[i],*spawn=NULL,*flag;playerState_t *ps=&ent->client->ps;
        int teamSpawn=ent->client->sess.sessionTeam==TEAM_BLUE && sameSpawnBlue?sameSpawnBlue:sameSpawn;
        int team=ent->client->sess.sessionTeam,rank=ranks[team]++,j=0,index=teamSpawn?teamSpawn-1:rank%6;
        const char *cls=team==TEAM_RED?(rank<6 || sameSpawn?"team_CTF_redspawn":"team_CTF_redplayer"):
                                                   (rank<6 || sameSpawn?"team_CTF_bluespawn":"team_CTF_blueplayer");
        vec3_t pose,offset={0,0,0};
        while((spawn=G_Find(spawn,FOFS(classname),cls))!=NULL)if(j++==index)break;
        flag=G_Find(NULL,FOFS(classname),team==TEAM_RED?"team_CTF_blueflag":"team_CTF_redflag");
        if(!spawn || !flag){trap->Print("[MOVECOST] REFUSED missing native start/goal\n");return qtrue;}
        ClientSpawn(ent);trap->UnlinkEntity((sharedEntity_t*)ent);
        Bot2_ClearState(i);botstates[i]->customThink=Idle;
        ent->health=ps->stats[STAT_HEALTH]=100;
        VectorCopy(spawn->s.origin,pose);
        if(sameSpawn) {
            // Side by side across the spawn facing, 40 units apart, each pose
            // validated as a standing position before any client is linked.
            vec3_t forward,right,top,bottom;trace_t still,down;
            AngleVectors(spawn->s.angles,forward,right,NULL);
            VectorScale(right,(rank-(wanted/2-1)*.5f)*40,offset);offset[2]=0;
            VectorAdd(pose,offset,pose);
            trap->Trace(&still,pose,ent->r.mins,ent->r.maxs,pose,i,MASK_PLAYERSOLID,qfalse,0,0);
            VectorCopy(pose,top);top[2]+=32;VectorCopy(pose,bottom);bottom[2]-=128;
            trap->Trace(&down,top,ent->r.mins,ent->r.maxs,bottom,i,MASK_PLAYERSOLID,qfalse,0,0);
            if(still.startsolid || still.allsolid || down.startsolid || down.allsolid || down.fraction==1 || down.plane.normal[2]<.7f) {
                trap->Print("[MOVECOST] REFUSED infeasible same-spawn pose client=%d origin=%.3f,%.3f,%.3f\n",i,pose[0],pose[1],pose[2]);
                return qtrue;
            }
        }
        VectorCopy(pose,ps->origin);VectorClear(ps->velocity);ps->groundEntityNum=ENTITYNUM_NONE;
        ps->weapon=WP_SABER;ps->weaponstate=WEAPON_READY;ps->weaponTime=0;ps->stats[STAT_WEAPONS]|=1<<WP_SABER;
        ps->fd.forcePowersActive=0;ps->fd.forcePowersKnown=(1<<FP_SPEED)|(1<<FP_LEVITATION);
        memset(ps->fd.forcePowerLevel,0,sizeof(ps->fd.forcePowerLevel));
        ps->fd.forcePowerLevel[FP_SPEED]=3;ps->fd.forcePowerLevel[FP_LEVITATION]=3;
        ps->fd.forcePower=ps->fd.forcePowerMax=100;
        SetClientViewAngle(ent,spawn->s.angles);VectorCopy(ps->origin,ent->r.currentOrigin);
        cost.clients[i].selected=1;VectorCopy(flag->r.currentOrigin,cost.clients[i].goal);
        trap->Print("[MOVECOST] START_POSE client=%d team=%d class=%s index=%d origin=%.3f,%.3f,%.3f yaw=%.3f offset=%.1f,%.1f same_spawn=%d\n",
            i,team,cls,index,ps->origin[0],ps->origin[1],ps->origin[2],ps->viewangles[YAW],offset[0],offset[1],sameSpawn);
    }
    for(i=0;i<level.maxclients;i++)if(cost.clients[i].selected)trap->LinkEntity((sharedEntity_t*)&g_entities[i]);
    cost.start=((level.time+600+199)/200)*200;cost.active=1;
    trap->FS_Open("movement_cost.csv",&cost.file,FS_WRITE);
    if(!cost.file){cost.active=0;trap->Print("[MOVECOST] REFUSED capture open failed\n");return qtrue;}
    {
        char header[2048],column[32];
        Q_strncpyz(header,"level_time,elapsed_ms,clients,dispatches,alive,moving,movement_us,bad_clocks,plan_us,route_us,localnav_us,load_us,"
            "dispatch_us,prepare_us,forecast_us,prepare_calls,prepare_admitted,prepare_deferred,library_loads,prepare_refused_free",sizeof(header));
        for(i=0;i<level.maxclients;i++){Com_sprintf(column,sizeof(column),",client%d_us",i);Q_strcat(header,sizeof(header),column);}
        // Accounting v3's attribution columns trail the per-client ones, so a
        // reader of v2 captures by position still finds what it expects.
        Q_strcat(header,sizeof(header),",planstate_us,graphpath_us,rollout_us,reroute_us,rollouts,rollout_steps,"
            "planstate_calls,planstate_expanded,planstate_overflow,graphpath_calls,reroute_calls,"
            "budget_searches,budget_hits,budget_not_ready,budget_refused_state,budget_refused_other,"
            "trace_cache_calls,trace_cache_hits,fc_copy_us,fc_pmove_us,fc_guard_us,fc_policy_us,fc_trigger_us,fc_total_us,"
            "pm_setup_us,pm_ground1_us,pm_premove_us,pm_move_us,pm_animate_us,pm_ground2_us,pm_preweapon_us,pm_weapon_us,pm_rest_us,"
            "pm_trace_us,pm_traces,corridor_us,corridor_trace_us,corridor_box_us,corridor_calls,corridor_traces,corridor_boxes,"
            "pol_movestep_us,pol_control_us,pol_progress_us,pol_navmesh_us,corridor_memo_hits,"
            "replay_steps,replay_verified,replay_mismatches");
        Q_strcat(header,sizeof(header),"\n");
        trap->FS_Write(header,strlen(header),cost.file);
    }
    if(cost.trace) {
        // Trailing diagnostic columns (Q1): native falling-to-death stamps
        // before/after movement and this frame's top-level movement owner.
        // Older recordings lack them; readers treat them as optional.
        const char *header="level_time,elapsed_ms,client,pre_x,pre_y,pre_z,pre_vx,pre_vy,pre_vz,pre_ground,pre_health,pre_force,pre_active_powers,post_x,post_y,post_z,post_vx,post_vy,post_vz,post_ground,post_health,post_force,post_active_powers,forward,right,up,buttons,generic_cmd,pitch_short,yaw_short,roll_short,target_x,target_y,target_z,target_entity,state,macro_state,pre_falling,post_falling,owner\n";
        trap->FS_Open("movement_cost_trace.csv",&cost.traceFile,FS_WRITE);
        if(!cost.traceFile){trap->FS_Close(cost.file);cost.file=0;cost.active=0;trap->Print("[MOVECOST] REFUSED trace open failed\n");return qtrue;}
        trap->FS_Write(header,strlen(header),cost.traceFile);
    }
    {
        fileHandle_t metadata=0;char line[MAX_OSPATH+256],value[MAX_OSPATH];
        const char *names[]={"sv_fps","g_speed","g_gravity","g_forceRegenTime","pmove_fixed","pmove_msec","pmove_float","bot_moveCostTrace","sv_mapChecksum",
            "bot_moveEdges","bot_movePlanner","bot_moveState","bot_moveStraight",
            "bot_moveSpike","bot_moveParity","bot_moveTapeLibrary","bot_moveDecisionTape","bot_moveSpend",
            "bot_movementOnly","bot_moveCost"};
        trap->FS_Open("movement_cost.meta",&metadata,FS_WRITE);
        if(!metadata){trap->FS_Close(cost.file);cost.file=0;if(cost.traceFile){trap->FS_Close(cost.traceFile);cost.traceFile=0;}cost.active=0;trap->Print("[MOVECOST] REFUSED metadata open failed\n");return qtrue;}
        {const char *scope="diagnostic_output=buffered_after_recording\n";trap->FS_Write(scope,strlen(scope),metadata);}
        {
            // What the columns mean is declared with the data, so a report
            // reading an older dispatch-only capture cannot mistake it for
            // complete accounting.
            const char *accounting=
                "accounting=complete_movement_v3\n"
                "planstate_us=Graph_PlanState_subpart\n"
                "graphpath_us=Graph_Path_subpart\n"
                "rollout_us=forecast_measured_time_of_every_RunCandidate_subpart\n"
                "reroute_us=RerouteFrom_whole_including_its_planner_subpart\n"
                "movement_us=dispatch_us+prepare_us\n"
                "dispatch_us=Bot2_ExecuteMovement_kick_jumppad_Move_Plan_Move_Command\n"
                "prepare_us=Bot2_MoveBenchPmove_boundary_searches_and_parity_complete\n"
                "plan_us,route_us,localnav_us,load_us=retired_always_0_columns_kept_for_positional_readers\n"
                "forecast_us=oracle_measured_rollout_time_subpart_of_prepare_us\n"
                "forecast_measure_time=clock_reads_inside_pmove_steps_included_in_prepare_us\n"
                "diagnostic_prints=buffered_after_recording\n";
            trap->FS_Write(accounting,strlen(accounting),metadata);
            Com_sprintf(line,sizeof(line),"clock_read_us=%.4f\n",cost.clockReadUs);trap->FS_Write(line,strlen(line),metadata);
        }
        {const char *valid=trace?"timing_claims_valid=0\ntrace_scope=per_client_per_frame_pre_dispatch_and_post_native_movement\n":"timing_claims_valid=1\ntrace_scope=disabled\n";trap->FS_Write(valid,strlen(valid),metadata);}
        for(i=0;i<ARRAY_LEN(names);i++) {
            trap->Cvar_VariableStringBuffer(names[i],value,sizeof(value));
            Com_sprintf(line,sizeof(line),"%s=%s\n",names[i],value);trap->FS_Write(line,strlen(line),metadata);
        }
#ifdef _WIN32
        {
            HMODULE module;
            if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)(uintptr_t)&Bot2_MoveCostStart,&module) && GetModuleFileNameA(module,value,sizeof(value))) {
                Com_sprintf(line,sizeof(line),"module=%s\n",value);trap->FS_Write(line,strlen(line),metadata);
            }
        }
#endif
        trap->FS_Close(metadata);
    }
    trap->Cvar_Set("bot_moveCostRecording","1");
    trap->Print("[MOVECOST] START clients=%d start=%d duration=20000 impulse=hold+2500+client*25 trace=%d same_spawn=%d same_spawn_blue=%d\n",count,cost.start,trace,sameSpawn,sameSpawnBlue);
    return qtrue;
}

qboolean Bot2_MoveCostClient(gentity_t *ent) {
    int client=ent->s.number,elapsed,beforeHealth;usercmd_t cmd;playerState_t *ps,before;double began;
    if(flushPending && level.time>cost.frameTime) {
        trap->FS_Write(captureRows,captureBytes,cost.file);trap->FS_Close(cost.file);
        if(cost.traceFile){trap->FS_Write(traceRows,traceBytes,cost.traceFile);trap->FS_Close(cost.traceFile);cost.traceFile=0;}
        cost.file=0;flushPending=0;
        FlushPrints();
        trap->Print("[MOVECOST] DONE clients=%d frames=%d alive=%d prints=%d dropped_prints=%d accounting=complete_movement_v3\n",
            cost.count,cost.frames,cost.alive,cost.prints,cost.droppedPrints);
    }
    if(!cost.active || client<0 || client>=MAX_CLIENTS || !cost.clients[client].selected)return qfalse;
    if(cost.frameTime!=level.time) {
        // A selected client that left would hold every later row (a row is
        // written once all selected clients ran): end the capture instead.
        int i;
        for(i=0;i<level.maxclients;i++)if(cost.clients[i].selected && (!g_entities[i].inuse || !g_entities[i].client)) {
            cost.active=0;trap->Cvar_Set("bot_moveCostRecording","0");
            if(cost.file){trap->FS_Close(cost.file);cost.file=0;}
            if(cost.traceFile){trap->FS_Close(cost.traceFile);cost.traceFile=0;}
            FlushPrints();trap->Print("[MOVECOST] REFUSED client %d left the capture\n",i);return qfalse;
        }
    }
    ps=&ent->client->ps;elapsed=level.time-cost.start;
    if(cost.trace){before=*ps;beforeHealth=ent->health;}
    cmd=cost.clients[client].command;cmd.serverTime=level.time;cmd.weapon=WP_SABER;cmd.buttons=0;cmd.generic_cmd=0;
    if(cost.frameTime!=level.time) {
        cost.frameTime=level.time;cost.seen=cost.calls=cost.alive=cost.moving=cost.badClocks=0;
        cost.movementUs=cost.dispatchUs=cost.prepareUs=cost.forecastUs=0;
        memset(cost.clientUs,0,sizeof(cost.clientUs));
        memset(cost.sectionUs,0,sizeof(cost.sectionUs));
        memset(cost.counters,0,sizeof(cost.counters));
    }
    if(elapsed>0 && ent->health>0 && elapsed>cost.hold[client]) {
        // The declared impulse is relative to the client's own start, so a
        // held client's trajectory is the unheld one shifted in time.
        if(!cost.clients[client].perturbed && elapsed>=cost.hold[client]+2500+client*25) {
            ps->velocity[client%2]+=(client%4<2?150:-150);cost.clients[client].perturbed=1;
            trap->Print("[MOVECOST] IMPULSE client=%d elapsed=%d\n",client,elapsed);
        }
        if(!cost.clients[client].prepared){cmd.generic_cmd=GENCMD_FORCE_SPEED;cost.clients[client].prepared=1;}
        began=ClockUs();
        Bot2_ExecuteMovement(client,&cmd,cost.clients[client].goal);
        cost.clientUs[client]=ClockUs()-began;cost.dispatchUs+=cost.clientUs[client];cost.movementUs+=cost.clientUs[client];cost.calls++;
    } else {
        int axis;cmd.forwardmove=cmd.rightmove=cmd.upmove=0;
        for(axis=0;axis<3;axis++)cmd.angles[axis]=ANGLE2SHORT(ps->viewangles[axis])-ps->delta_angles[axis];
    }
    cost.clients[client].command=cmd;
    ClientThink(client,&cmd);ClientThink_real(ent);
    if(cost.trace && elapsed>0 && !BufferTraceRow(client,elapsed,&before,beforeHealth,&cmd,ps,ent->health)) {
        cost.active=0;trap->Cvar_Set("bot_moveCostRecording","0");
        trap->FS_Close(cost.file);cost.file=0;trap->FS_Close(cost.traceFile);cost.traceFile=0;
        FlushPrints();trap->Print("[MOVECOST] REFUSED trace buffer overflow\n");return qtrue;
    }
    cost.badClocks+=ps->commandTime!=((level.time+7)/8)*8;
    cost.alive+=ent->health>0;
    cost.moving+=ent->health>0 && ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]>50*50;
    if(!cost.clients[client].picked && ps->powerups[ent->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG]) {
        cost.clients[client].picked=1;trap->Print("[MOVECOST] PICKUP client=%d elapsed=%d\n",client,elapsed);
    }
    if(++cost.seen==cost.count && elapsed>0) {
        char line[2048],column[64];int i;
        Com_sprintf(line,sizeof(line),"%d,%d,%d,%d,%d,%d,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d",level.time,elapsed,cost.count,cost.calls,cost.alive,cost.moving,cost.movementUs,cost.badClocks,
            cost.sectionUs[0],cost.sectionUs[1],cost.sectionUs[2],cost.sectionUs[3],
            cost.dispatchUs,cost.prepareUs,cost.forecastUs,
            cost.counters[BOT2_MOVECOST_COUNT_PREPARE_CALLS],cost.counters[BOT2_MOVECOST_COUNT_PREPARE_ADMITTED],
            cost.counters[BOT2_MOVECOST_COUNT_PREPARE_DEFERRED],cost.counters[BOT2_MOVECOST_COUNT_LIBRARY_LOADS],
            cost.counters[BOT2_MOVECOST_COUNT_PREPARE_REFUSED_FREE]);
        for(i=0;i<level.maxclients;i++){Com_sprintf(column,sizeof(column),",%.3f",cost.clientUs[i]);Q_strcat(line,sizeof(line),column);}
        Com_sprintf(column,sizeof(column),",%.3f,%.3f,%.3f,%.3f",
            cost.sectionUs[BOT2_MOVECOST_SECTION_PLANSTATE],cost.sectionUs[BOT2_MOVECOST_SECTION_GRAPHPATH],
            cost.sectionUs[BOT2_MOVECOST_SECTION_ROLLOUT],cost.sectionUs[BOT2_MOVECOST_SECTION_REROUTE]);
        Q_strcat(line,sizeof(line),column);
        Com_sprintf(column,sizeof(column),",%d,%d,%d,%d,%d,%d,%d",
            cost.counters[BOT2_MOVECOST_COUNT_ROLLOUTS],cost.counters[BOT2_MOVECOST_COUNT_ROLLOUT_STEPS],
            cost.counters[BOT2_MOVECOST_COUNT_PLANSTATE_CALLS],cost.counters[BOT2_MOVECOST_COUNT_PLANSTATE_EXPANDED],
            cost.counters[BOT2_MOVECOST_COUNT_PLANSTATE_OVERFLOW],cost.counters[BOT2_MOVECOST_COUNT_GRAPHPATH_CALLS],
            cost.counters[BOT2_MOVECOST_COUNT_REROUTE_CALLS]);
        Q_strcat(line,sizeof(line),column);
        Com_sprintf(column,sizeof(column),",%d,%d,%d,%d,%d",
            cost.counters[BOT2_MOVECOST_COUNT_BUDGET_SEARCHES],cost.counters[BOT2_MOVECOST_COUNT_BUDGET_HITS],
            cost.counters[BOT2_MOVECOST_COUNT_BUDGET_NOT_READY],cost.counters[BOT2_MOVECOST_COUNT_BUDGET_REFUSED_STATE],
            cost.counters[BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER]);
        Q_strcat(line,sizeof(line),column);
        Com_sprintf(column,sizeof(column),",%d,%d",
            cost.counters[BOT2_MOVECOST_COUNT_TRACE_CALLS],cost.counters[BOT2_MOVECOST_COUNT_TRACE_HITS]);
        Q_strcat(line,sizeof(line),column);
        Com_sprintf(column,sizeof(column),",%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
            cost.sectionUs[BOT2_MOVECOST_SECTION_FC_COPY],cost.sectionUs[BOT2_MOVECOST_SECTION_FC_PMOVE],
            cost.sectionUs[BOT2_MOVECOST_SECTION_FC_GUARD],cost.sectionUs[BOT2_MOVECOST_SECTION_FC_POLICY],
            cost.sectionUs[BOT2_MOVECOST_SECTION_FC_TRIGGER],cost.sectionUs[BOT2_MOVECOST_SECTION_FC_TOTAL]);
        Q_strcat(line,sizeof(line),column);
        {
            int p;
            for(p=0;p<10;p++){Com_sprintf(column,sizeof(column),",%.1f",cost.sectionUs[BOT2_MOVECOST_SECTION_PM_SETUP+p]);Q_strcat(line,sizeof(line),column);}
            Com_sprintf(column,sizeof(column),",%d,%.1f,%.1f,%.1f,%d,%d,%d",cost.counters[BOT2_MOVECOST_COUNT_PM_TRACES],
                cost.sectionUs[BOT2_MOVECOST_SECTION_CORRIDOR],cost.sectionUs[BOT2_MOVECOST_SECTION_CORRIDOR_TRACE],
                cost.sectionUs[BOT2_MOVECOST_SECTION_CORRIDOR_BOX],cost.counters[BOT2_MOVECOST_COUNT_CORRIDOR_CALLS],
                cost.counters[BOT2_MOVECOST_COUNT_CORRIDOR_TRACES],cost.counters[BOT2_MOVECOST_COUNT_CORRIDOR_BOXES]);
            Q_strcat(line,sizeof(line),column);
            Com_sprintf(column,sizeof(column),",%.1f,%.1f,%.1f,%.1f",
                cost.sectionUs[BOT2_MOVECOST_SECTION_POL_MOVESTEP],cost.sectionUs[BOT2_MOVECOST_SECTION_POL_CONTROL],
                cost.sectionUs[BOT2_MOVECOST_SECTION_POL_PROGRESS],cost.sectionUs[BOT2_MOVECOST_SECTION_POL_NAVMESH]);
            Q_strcat(line,sizeof(line),column);
            Com_sprintf(column,sizeof(column),",%d,%d,%d,%d",cost.counters[BOT2_MOVECOST_COUNT_CORRIDOR_MEMO_HITS],
                cost.counters[BOT2_MOVECOST_COUNT_REPLAY_STEPS],cost.counters[BOT2_MOVECOST_COUNT_REPLAY_VERIFIED],
                cost.counters[BOT2_MOVECOST_COUNT_REPLAY_MISMATCHES]);
            Q_strcat(line,sizeof(line),column);
        }
        Q_strcat(line,sizeof(line),"\n");
        if(captureBytes+(int)strlen(line)>sizeof(captureRows)) {
            cost.active=0;trap->Cvar_Set("bot_moveCostRecording","0");
            trap->FS_Close(cost.file);cost.file=0;
            if(cost.traceFile){trap->FS_Close(cost.traceFile);cost.traceFile=0;}
            FlushPrints();trap->Print("[MOVECOST] REFUSED capture buffer overflow\n");return qtrue;
        }
        memcpy(captureRows+captureBytes,line,strlen(line));captureBytes+=(int)strlen(line);cost.frames++;
        if(elapsed>=20000) {
            cost.active=0;flushPending=1;
            trap->Cvar_Set("bot_moveCostRecording","0");
        }
    }
    return qtrue;
}

// The frame meter (bot_moveFrameMeter 1): the per-frame movement cost of each
// bot in an ordinary world - a bench leg, a defended world - where the cost
// fixture above cannot run (it owns its clients and their poses). Each bot's
// command dispatch and its whole pre-Pmove boundary (the rewrite's searches)
// are summed per client per game frame; a closed frame
// goes into a 0.25 ms histogram per client and one for the frame's total over
// every bot. [FRAMECOST] lines print once a second outside any timed interval,
// cumulative since the fixture's release (Bot2_FrameMeterReset), so the last
// line of a world is its summary. Wall-clock: timing worlds run alone.
#define FRAMEMETER_BUCKETS 400      // 0.25 ms buckets: 0..100 ms, then overflow
#define FRAMEMETER_TOTAL MAX_CLIENTS
unsigned int bot2_meterSteps,bot2_meterTraces,bot2_meterExpanded;
unsigned int bot2_meterGuidePops,bot2_meterEngineTraces,bot2_meterCorridors;
unsigned int bot2_meterGuideFoldPops,bot2_meterGuideFoldDeferred;   // the part of bot2_meterGuidePops settled by folds
// Where a frame's expansions came from ([FRAMEEXP]): the cell search's share of bot2_meterExpanded, and
// the state search's while a replan or a split part runs (bot2_meterInReplan set by bot2_move.c); the rest is
// state searches elsewhere (reroutes from candidate landings). A frame of at least FRAMEEXP_PRINT prints them.
unsigned int bot2_meterExpandedCell,bot2_meterExpandedReplan,bot2_meterExpandedCellOther;
int bot2_meterInReplan;
#define FRAMEEXP_PRINT 10000
// The work meter ([FRAMEWORK]): per frame, the forecast steps and planner expansions over every bot,
// as histograms. Counts, not clocks: a seeded world replays them exactly whatever the machine was doing, where the
// microsecond tail of one world swung 2.4 times between two identical runs (records/2026-10-06-frame-cost.md).
// est_us weighs them at FRAMEWORK_STEP_US and FRAMEWORK_EXPAND_US, rough dev-CPU costs, for one tail number.
#define FRAMEWORK_BUCKETS 1000
#define FRAMEWORK_STEP_BUCKET 5      // steps a bucket: 0..5,000
#define FRAMEWORK_EXPAND_BUCKET 100  // expansions a bucket: 0..100,000
#define FRAMEWORK_EST_BUCKET 100     // estimated microseconds a bucket: 0..100 ms
#define FRAMEWORK_STEP_US 25
#define FRAMEWORK_EXPAND_US 3
// A settled cell of a guidance table: 152 Kejim tables of 14,249 cells built in 600.6 ms at the load,
// 0.28 us each. Added to est_us, so est_* reads a little higher than on earlier builds.
#define FRAMEWORK_GUIDE_POP_NS 280
static struct {
    int frames,steps[FRAMEWORK_BUCKETS+1],expand[FRAMEWORK_BUCKETS+1],est[FRAMEWORK_BUCKETS+1];
    int maxSteps,maxExpand,maxEst,over25;double sumSteps,sumExpand;
    int maxGuide;double sumGuide,sumEngine,sumCorridors,sumTraces;
    int maxFold,frameFold;double sumFold;unsigned int foldAt;   // fold pops a frame, inside metered calls as guide_pops
} work;
// bot_moveFrameMeterSlow (microseconds, default 0 = off): a closed frame whose
// total over every bot is at least this prints [FRAMESLOW] with each bot's
// microseconds, forecast steps and forecast traces that frame, so the over-
// budget frames can be joined by level to the [MOVE]/[DEFENDER] lines that say
// what each bot was doing (the 4v4 frame cost).
static struct {
    int on,checked,time,since,lastPrint;
    unsigned int stepsAt,tracesAt,expandAt,cellAt,replanAt,cellOtherAt;   // the counters when the timed interval began
    unsigned int guideAt,engineAt,corridorAt;int frameGuide[MAX_CLIENTS];
    int frameSteps[MAX_CLIENTS],frameTraces[MAX_CLIENTS],calls[MAX_CLIENTS];
    int frameExpand[MAX_CLIENTS],frameCell[MAX_CLIENTS],frameReplan[MAX_CLIENTS];
    int frameCellOther[MAX_CLIENTS];
    int srcPending[BOT2_SRC_COUNT],frameSrc[MAX_CLIENTS][BOT2_SRC_COUNT];double planPending,framePlanUs[MAX_CLIENTS];
    double frameUs[MAX_CLIENTS];
    unsigned char touched[MAX_CLIENTS];
    int hist[MAX_CLIENTS+1][FRAMEMETER_BUCKETS+1],frames[MAX_CLIENTS+1],over[MAX_CLIENTS+1];
    double maxUs[MAX_CLIENTS+1],sumUs[MAX_CLIENTS+1];
} meter;

static void MeterRecord(int slot,double us) {
    int bucket=(int)(us/250.0);
    if(bucket>FRAMEMETER_BUCKETS)bucket=FRAMEMETER_BUCKETS;
    meter.hist[slot][bucket]++;meter.frames[slot]++;meter.sumUs[slot]+=us;
    if(us>25000)meter.over[slot]++;
    if(us>meter.maxUs[slot])meter.maxUs[slot]=us;
}

// The upper edge of the bucket holding the q-th frame; the overflow bucket
// reports the maximum.
static double MeterQuantile(int slot,double q) {
    int i,seen=0,rank=(int)(q*meter.frames[slot]+.999);
    if(rank<1)rank=1;
    for(i=0;i<=FRAMEMETER_BUCKETS;i++) {
        seen+=meter.hist[slot][i];
        if(seen>=rank)return i==FRAMEMETER_BUCKETS?meter.maxUs[slot]:(i+1)*250.0;
    }
    return meter.maxUs[slot];
}

// Perf counters (bot2_movecost.h). The cvar reads by name are counted by routing the two by-name cvar imports
// through counting wrappers while the meter is on: the engine's table is copied once and trap points at the copy,
// so every caller in the module is counted without touching its call site; the wrappers return the engine's value.
bot2_perf_t bot2_perf;
int bot2_perfOn,bot2_perfInRollout;
static gameImport_t perfImport,*perfEngine;
static double perfTick;

double Bot2_PerfClock(void) {
#ifdef _WIN32
    LARGE_INTEGER now;
    if(perfTick<=0) {LARGE_INTEGER f;QueryPerformanceFrequency(&f);perfTick=1000000.0/f.QuadPart;}
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart*perfTick;
#else
    return trap->Milliseconds()*1000.0;
#endif
}

static int PerfCvarInt(const char *name) {
    double t=Bot2_PerfClock();int v=perfEngine->Cvar_VariableIntegerValue(name),k=bot2_perfInRollout?BOT2_PERF_CVAR_ROLLOUT:BOT2_PERF_CVAR;
    bot2_perf.n[k]++;bot2_perf.us[k]+=Bot2_PerfClock()-t;
    return v;
}

static void PerfCvarString(const char *name,char *buffer,int size) {
    double t=Bot2_PerfClock();int k=bot2_perfInRollout?BOT2_PERF_CVAR_ROLLOUT:BOT2_PERF_CVAR;
    perfEngine->Cvar_VariableStringBuffer(name,buffer,size);
    bot2_perf.n[k]++;bot2_perf.us[k]+=Bot2_PerfClock()-t;
}

static void PerfSet(int on) {
    if(on && !perfEngine) {
        perfEngine=trap;perfImport=*trap;
        perfImport.Cvar_VariableIntegerValue=PerfCvarInt;perfImport.Cvar_VariableStringBuffer=PerfCvarString;
        trap=&perfImport;
    } else if(!on && perfEngine) {trap=perfEngine;perfEngine=NULL;}
    bot2_perfOn=on;
}

static void PerfPrint(void) {
    static const char *names[BOT2_PERF_COUNT]={"scope","scope_end","scope_firstcopy","skip_eligible","skip_same","skip_diff",
        "skip_copysame","near_build","near_rebuild","near_drop","mover","mover_miss","mover_scopehit","mover_scopediff",
        "rollout","step","replayed","xscope","ring_wrap","sweep","cvar","cvar_rollout","gfind","reach"};
    char line[2048];int i,cells,opposite,any;
    void Graph_PerfLinks(int *cells,int *opposite,int *any);    // bot2_graph.c
    Graph_PerfLinks(&cells,&opposite,&any);
    Com_sprintf(line,sizeof(line),"[PERFCTR] level=%d since=%d links=%d/%d/%d",level.time,meter.since,cells,opposite,any);
    for(i=0;i<BOT2_PERF_COUNT;i++) {
        Q_strcat(line,sizeof(line),va(" %s=%lld",names[i],bot2_perf.n[i]));
        if(bot2_perf.us[i]>0)Q_strcat(line,sizeof(line),va("/%.0fus",bot2_perf.us[i]));
        if(bot2_perf.n2[i])Q_strcat(line,sizeof(line),va("/%lld",bot2_perf.n2[i]));
    }
    trap->Print("%s\n",line);
}

static int WorkQuantile(const int *hist,int bucket,int maxValue,double q) {
    int i,seen=0,rank=(int)(q*work.frames+.999);
    for(i=0;i<=FRAMEWORK_BUCKETS;i++) {seen+=hist[i];if(seen>=rank)return i<FRAMEWORK_BUCKETS?(i+1)*bucket:maxValue;}
    return maxValue;
}

static void WorkRecord(int steps,int expand,int guide) {
    int est=steps*FRAMEWORK_STEP_US+expand*FRAMEWORK_EXPAND_US+(int)((double)guide*FRAMEWORK_GUIDE_POP_NS/1000);
    work.frames++;work.sumSteps+=steps;work.sumExpand+=expand;work.sumGuide+=guide;
    if(guide>work.maxGuide)work.maxGuide=guide;
    work.steps[min(FRAMEWORK_BUCKETS,steps/FRAMEWORK_STEP_BUCKET)]++;
    work.expand[min(FRAMEWORK_BUCKETS,expand/FRAMEWORK_EXPAND_BUCKET)]++;
    work.est[min(FRAMEWORK_BUCKETS,est/FRAMEWORK_EST_BUCKET)]++;
    if(steps>work.maxSteps)work.maxSteps=steps;
    if(expand>work.maxExpand)work.maxExpand=expand;
    if(est>work.maxEst)work.maxEst=est;
    if(est>25000)work.over25++;
}

static void MeterPrint(void) {
    int i;
    if(bot2_perfOn)PerfPrint();
    if(profileMode) {
        char line[2048];int k;
        Com_sprintf(line,sizeof(line),"[PROFILE] level=%d sections_us=",level.time);
        for(k=0;k<BOT2_MOVECOST_SECTIONS;k++)Q_strcat(line,sizeof(line),va("%s%.0f",k?",":"",cost.sectionUs[k]));
        Q_strcat(line,sizeof(line)," counters=");
        for(k=0;k<BOT2_MOVECOST_COUNTS;k++)Q_strcat(line,sizeof(line),va("%s%d",k?",":"",cost.counters[k]));
        trap->Print("%s\n",line);
    }
    for(i=0;i<=MAX_CLIENTS;i++) {
        if(!meter.frames[i])continue;
        trap->Print("[FRAMECOST] level=%d since=%d client=%s frames=%d mean_us=%.0f p50_us=%.0f p99_us=%.0f max_us=%.0f over25=%d\n",
            level.time,meter.since,i==FRAMEMETER_TOTAL?"all":va("%d",i),meter.frames[i],meter.sumUs[i]/meter.frames[i],
            MeterQuantile(i,.5),MeterQuantile(i,.99),meter.maxUs[i],meter.over[i]);
    }
    if(work.frames)
        trap->Print("[FRAMEWORK] level=%d since=%d frames=%d steps_mean=%.1f steps_p99=%d steps_p999=%d steps_max=%d "
            "expand_mean=%.1f expand_p99=%d expand_p999=%d expand_max=%d est_p99_us=%d est_p999_us=%d est_max_us=%d est_over25=%d "
            "steps_sum=%.0f expand_sum=%.0f guide_pops=%.0f guide_pops_max=%d traces=%.0f engine_traces=%.0f corridors=%.0f "
            "fold_pops=%.0f fold_pops_max=%d fold_deferred=%u\n",
            level.time,meter.since,work.frames,work.sumSteps/work.frames,
            WorkQuantile(work.steps,FRAMEWORK_STEP_BUCKET,work.maxSteps,.99),WorkQuantile(work.steps,FRAMEWORK_STEP_BUCKET,work.maxSteps,.999),
            work.maxSteps,work.sumExpand/work.frames,
            WorkQuantile(work.expand,FRAMEWORK_EXPAND_BUCKET,work.maxExpand,.99),WorkQuantile(work.expand,FRAMEWORK_EXPAND_BUCKET,work.maxExpand,.999),
            work.maxExpand,WorkQuantile(work.est,FRAMEWORK_EST_BUCKET,work.maxEst,.99),
            WorkQuantile(work.est,FRAMEWORK_EST_BUCKET,work.maxEst,.999),work.maxEst,work.over25,
            work.sumSteps,work.sumExpand,work.sumGuide,work.maxGuide,work.sumTraces,work.sumEngine,work.sumCorridors,
            work.sumFold,work.maxFold,bot2_meterGuideFoldDeferred);
}

static void MeterClose(void) {
    int i,any=0,slow,steps=0,expand=0,guide=0;double total=0;
    for(i=0;i<MAX_CLIENTS;i++)if(meter.touched[i])expand+=meter.frameExpand[i];
    if(meter.on && expand>=FRAMEEXP_PRINT) {
        char line[1024];
        Com_sprintf(line,sizeof(line),"[FRAMEEXP] level=%d expand=%d",meter.time,expand);
        for(i=0;i<MAX_CLIENTS;i++)if(meter.touched[i] && meter.frameExpand[i])
            Q_strcat(line,sizeof(line),va(" c%d=%d/%d/%d/%d",i,meter.frameReplan[i],
                meter.frameExpand[i]-meter.frameReplan[i]-meter.frameCell[i],meter.frameCell[i],meter.frameCellOther[i]));
        trap->Print("%s\n",line);
    }
    expand=0;
    for(i=0;i<MAX_CLIENTS;i++)if(meter.touched[i])total+=meter.frameUs[i];
    slow=trap->Cvar_VariableIntegerValue("bot_moveFrameMeterSlow");
    if(slow>0 && total>=slow) {
        char line[1024];
        Com_sprintf(line,sizeof(line),"[FRAMESLOW] level=%d total_us=%.0f",meter.time,total);
        for(i=0;i<MAX_CLIENTS;i++)if(meter.touched[i])
            Q_strcat(line,sizeof(line),va(" c%d=%.0f/%d/%d/%d",i,meter.frameUs[i],meter.frameSteps[i],meter.frameTraces[i],meter.calls[i]));
        trap->Print("%s\n",line);
        {
            static const char *srcNames[BOT2_SRC_COUNT]={"knock","spike","planned","gap","walkdefer","straight","budget"};
            int k,named;
            Com_sprintf(line,sizeof(line),"[FRAMESRC] level=%d",meter.time);
            for(i=0;i<MAX_CLIENTS;i++) {
                if(!meter.touched[i] || (!meter.frameSteps[i] && meter.framePlanUs[i]<1000))continue;
                Q_strcat(line,sizeof(line),va(" c%d=plan_us:%.0f",i,meter.framePlanUs[i]));named=0;
                for(k=0;k<BOT2_SRC_COUNT;k++)if(meter.frameSrc[i][k]) {Q_strcat(line,sizeof(line),va(",%s:%d",srcNames[k],meter.frameSrc[i][k]));named+=meter.frameSrc[i][k];}
                if(meter.frameSteps[i]>named)Q_strcat(line,sizeof(line),va(",other:%d",meter.frameSteps[i]-named));
            }
            trap->Print("%s\n",line);
        }
    }
    for(i=0;i<MAX_CLIENTS;i++)if(meter.touched[i]) {
        MeterRecord(i,meter.frameUs[i]);any=1;steps+=meter.frameSteps[i];expand+=meter.frameExpand[i];meter.frameExpand[i]=0;
        guide+=meter.frameGuide[i];meter.frameGuide[i]=0;meter.frameCell[i]=meter.frameReplan[i]=meter.frameCellOther[i]=0;
        meter.touched[i]=0;meter.frameUs[i]=0;meter.frameSteps[i]=meter.frameTraces[i]=meter.calls[i]=0;
        memset(meter.frameSrc[i],0,sizeof(meter.frameSrc[i]));meter.framePlanUs[i]=0;
    }
    if(any) {
        int fold=work.frameFold;
        MeterRecord(FRAMEMETER_TOTAL,total);WorkRecord(steps,expand,guide);
        work.sumFold+=fold;if(fold>work.maxFold)work.maxFold=fold;
    }
    work.frameFold=0;
}

double Bot2_FrameMeterClock(void) {
    if(meter.checked!=level.time) {
        meter.checked=level.time;meter.on=trap->Cvar_VariableIntegerValue("bot_moveFrameMeter")!=0;
        profileMode=meter.on && !cost.active && trap->Cvar_VariableIntegerValue("bot_moveProfile")!=0;
        if(meter.on!=bot2_perfOn)PerfSet(meter.on);
    }
    if(!meter.on)return 0;
    meter.stepsAt=bot2_meterSteps;meter.tracesAt=bot2_meterTraces;meter.expandAt=bot2_meterExpanded;
    meter.cellAt=bot2_meterExpandedCell;meter.replanAt=bot2_meterExpandedReplan;meter.cellOtherAt=bot2_meterExpandedCellOther;
    meter.guideAt=bot2_meterGuidePops;work.foldAt=bot2_meterGuideFoldPops;meter.engineAt=bot2_meterEngineTraces;meter.corridorAt=bot2_meterCorridors;
    memset(meter.srcPending,0,sizeof(meter.srcPending));meter.planPending=0;
    return ClockUs();
}

void Bot2_FrameMeterAdd(int client,double began) {
    double now;
    if(began<=0 || client<0 || client>=MAX_CLIENTS)return;
    now=ClockUs();
    if(meter.time!=level.time) {
        MeterClose();meter.time=level.time;
        if(level.time-meter.lastPrint>=1000) {meter.lastPrint=level.time;MeterPrint();}
    }
    meter.frameUs[client]+=now-began;meter.touched[client]=1;meter.calls[client]++;
    meter.frameSteps[client]+=bot2_meterSteps-meter.stepsAt;meter.frameTraces[client]+=bot2_meterTraces-meter.tracesAt;
    meter.frameExpand[client]+=bot2_meterExpanded-meter.expandAt;
    meter.frameCell[client]+=bot2_meterExpandedCell-meter.cellAt;meter.frameReplan[client]+=bot2_meterExpandedReplan-meter.replanAt;
    meter.frameCellOther[client]+=bot2_meterExpandedCellOther-meter.cellOtherAt;
    meter.frameGuide[client]+=bot2_meterGuidePops-meter.guideAt;work.frameFold+=(int)(bot2_meterGuideFoldPops-work.foldAt);
    work.sumTraces+=bot2_meterTraces-meter.tracesAt;work.sumEngine+=bot2_meterEngineTraces-meter.engineAt;
    work.sumCorridors+=bot2_meterCorridors-meter.corridorAt;
    {int k;for(k=0;k<BOT2_SRC_COUNT;k++)meter.frameSrc[client][k]+=meter.srcPending[k];}
    meter.framePlanUs[client]+=meter.planPending;
    memset(meter.srcPending,0,sizeof(meter.srcPending));meter.planPending=0;
}

void Bot2_FrameMeterSource(int source,int steps) {
    if(meter.on && source>=0 && source<BOT2_SRC_COUNT)meter.srcPending[source]+=steps;
}

void Bot2_FrameMeterPlanUs(double us) {
    if(meter.on)meter.planPending+=us;
}

void Bot2_FrameMeterReset(void) {
    if(!trap->Cvar_VariableIntegerValue("bot_moveFrameMeter"))return;
    memset(&meter,0,sizeof(meter));memset(&bot2_perf,0,sizeof(bot2_perf));memset(&work,0,sizeof(work));
    // bot_moveProfile's sections outside a capture count from here too, as
    // [FRAMECOST] does (a capture keeps its own interval).
    if(profileMode && !cost.active) {memset(cost.sectionUs,0,sizeof(cost.sectionUs));memset(cost.counters,0,sizeof(cost.counters));}
    meter.since=meter.lastPrint=level.time;
    trap->Print("[FRAMECOST] reset level=%d\n",level.time);
}
