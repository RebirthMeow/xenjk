// Opt-in input observation. Never issues a command or changes player state.
// Two subjects, and only one way into each: a human arms the recorder himself
// through the console command, and a bot is armed by the corner fixture on its
// own release frame under bot_routeRecord 2. The console command still refuses
// a bot, because outside a declared release a bot recording has no root.
#include "g_local.h"
#include "ai_bot2.h"
#include "bot2_route_record.h"

#define ROUTE_MAX_ROWS 30000
#define ROUTE_MAX_MS 180000
#define ROUTE_ARM_MS 600000

typedef struct {
    int active,started,pending,rows,armTime,startTime,startCommand,team,autoPickup;
    int powerSeen,positiveUpFrames,consecutiveUp,heldUp,forceDecrease;
    int stationary,armedStationary,epoch,startGround,startCarrying,teleportSeen,lastTeleport,lastForce;
    int subjectBot;
    fileHandle_t csv,summary;
    char stem[96];
    playerState_t before,after;
    usercmd_t command;
    int rawLevel,pmoveFixed,pmoveMsec,pmoveFloat;
} routeRecord_t;

static routeRecord_t records[MAX_CLIENTS];
static vmCvar_t enabled;
static int activeRecords,sequence;

static void Write(fileHandle_t file,const char *text) {
    if(file)trap->FS_Write(text,strlen(text),file);
}

static int Carrying(const playerState_t *ps) {
    return ps->powerups[PW_REDFLAG]!=0 || ps->powerups[PW_BLUEFLAG]!=0 || ps->powerups[PW_NEUTRALFLAG]!=0;
}

static int Stationary(const playerState_t *ps) {
    return ps->groundEntityNum!=ENTITYNUM_NONE && VectorLength(ps->velocity)<=1.0f;
}

static void Tell(int client,const char *message) {
    trap->Print("[ROUTE RECORD] client=%d %s\n",client,message);
    if(client>=0 && client<level.maxclients && g_entities[client].client &&
       g_entities[client].client->pers.connected==CON_CONNECTED)
        trap->SendServerCommand(client,va("print \"Route recorder: %s\n\"",message));
}

static void Finish(int client,const char *reason,int complete) {
    routeRecord_t *r=&records[client];char text[1024];int elapsed;
    if(!r->active)return;
    elapsed=r->started?level.time-r->startTime:0;
    if(elapsed<0)elapsed=0;
    Com_sprintf(text,sizeof(text),
        "{\"format\":\"HUMAN_ROUTE1\",\"complete\":%s,\"reason\":\"%s\",\"client\":%d,"
        "\"arm_level\":%d,\"start_level\":%d,\"end_level\":%d,\"start_command_time\":%d,\"elapsed_ms\":%d,\"rows\":%d,"
        "\"started\":%s,\"stationary_start\":%s,\"armed_stationary\":%s,\"active_powers_seen\":%d,"
        "\"positive_up_frames\":%d,\"consecutive_up_seen\":%s,\"force_decrease_seen\":%s,\"start_ground\":%d,\"start_carrying\":%s,\"teleport_seen\":%s}\n",
        complete?"true":"false",reason,client,r->armTime,r->started?r->startTime:-1,level.time,r->startCommand,
        elapsed,r->rows,r->started?"true":"false",r->stationary?"true":"false",r->armedStationary?"true":"false",
        r->powerSeen,r->positiveUpFrames,r->heldUp?"true":"false",r->forceDecrease?"true":"false",
        r->startGround,r->startCarrying?"true":"false",r->teleportSeen?"true":"false");
    Write(r->summary,text);
    trap->FS_Close(r->summary);trap->FS_Close(r->csv);
    r->summary=r->csv=0;r->active=0;r->pending=0;activeRecords--;
    Tell(client,va("%s: %d ms, %d commands; %s.csv",reason,elapsed,r->rows,r->stem));
    if(g_entities[client].client && g_entities[client].client->pers.connected==CON_CONNECTED)
        trap->SendServerCommand(client,va("cp \"Route recorder: %s (%0.3f seconds)\"",reason,elapsed/1000.0));
}

static int Exists(const char *path) {
    fileHandle_t file=0;int length=trap->FS_Open(path,&file,FS_READ);
    if(file)trap->FS_Close(file);
    return length>=0;
}

// Engine/map strings are quoted, rather than trusted as JSON or console text.
static void JsonString(const char *in,char *out,int size) {
    int n=0;
    while(*in && n<size-3) {
        unsigned char c=(unsigned char)*in++;
        if(c=='"' || c=='\\')out[n++]='\\';
        if(c<32)c=' ';
        out[n++]=c;
    }
    out[n]=0;
}

// The movement settings a bot recording was produced under. They are read and
// recorded, never set: a tape means nothing without the search that made it.
static const char *bindingCvars[]={
    "bot_moveEdges","bot_movePlanner","bot_moveState","bot_moveStraight",
    "bot_moveReroute","bot_moveExitSpeed","bot_moveStatePrefer"};

static void BindingSettings(char *out,int size) {
    int i,n;char value[64],safe[160];
    Q_strncpyz(out,"{",size);
    for(i=0;i<(int)ARRAY_LEN(bindingCvars);i++) {
        trap->Cvar_VariableStringBuffer(bindingCvars[i],value,sizeof(value));
        JsonString(value,safe,sizeof(safe));
        n=(int)strlen(out);
        if(n>=size-2)break;
        Com_sprintf(out+n,size-n,"%s\"%s\":\"%s\"",i?",":"",bindingCvars[i],safe);
    }
    n=(int)strlen(out);
    if(n<size-2) {out[n]='}';out[n+1]=0;}
}

static void ArmSubject(int client,int bot,int releaseLevel) {
    routeRecord_t *r=&records[client];gentity_t *ent=&g_entities[client];playerState_t *ps;
    fileHandle_t meta=0;qtime_t now;char path[128],map[128],checksum[64],safeMap[256],settings[768],text[3072];int attempt;
    memset(r,0,sizeof(*r));r->subjectBot=bot;r->epoch=trap->RealTime(&now);
    for(attempt=0;attempt<1000;attempt++) {
        Com_sprintf(r->stem,sizeof(r->stem),"bot2-route/%d-%d-c%d",r->epoch,sequence++,client);
        Com_sprintf(path,sizeof(path),"%s.csv",r->stem);if(Exists(path))continue;
        Com_sprintf(path,sizeof(path),"%s.meta.json",r->stem);if(Exists(path))continue;
        Com_sprintf(path,sizeof(path),"%s.summary.json",r->stem);if(!Exists(path))break;
    }
    if(attempt==1000) {Tell(client,"cannot allocate a new recording filename");return;}
    Com_sprintf(path,sizeof(path),"%s.csv",r->stem);trap->FS_Open(path,&r->csv,FS_WRITE);
    Com_sprintf(path,sizeof(path),"%s.meta.json",r->stem);trap->FS_Open(path,&meta,FS_WRITE);
    Com_sprintf(path,sizeof(path),"%s.summary.json",r->stem);trap->FS_Open(path,&r->summary,FS_WRITE);
    if(!r->csv || !meta || !r->summary) {
        if(r->csv)trap->FS_Close(r->csv);if(meta)trap->FS_Close(meta);if(r->summary)trap->FS_Close(r->summary);
        r->csv=r->summary=0;Tell(client,"file creation failed; partial files retained");return;
    }
    ps=&ent->client->ps;r->team=ent->client->sess.sessionTeam;r->armTime=level.time;
    r->autoPickup=!Carrying(ps) && (r->team==TEAM_RED || r->team==TEAM_BLUE) && level.gametype==GT_CTF;
    r->armedStationary=Stationary(ps);r->startCommand=-1;
    trap->Cvar_VariableStringBuffer("mapname",map,sizeof(map));JsonString(map,safeMap,sizeof(safeMap));
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",checksum,sizeof(checksum));
    BindingSettings(settings,sizeof(settings));
    Com_sprintf(text,sizeof(text),
        "{\"format\":\"HUMAN_ROUTE1\",\"client\":%d,\"human\":%s,\"subject\":\"%s\",\"corner_level\":%d,"
        "\"settings\":%s,\"realtime_unix\":%d,\"map\":\"%s\",\"map_checksum\":%d,"
        "\"team\":%d,\"gametype\":%d,\"arm_level\":%d,\"arm_origin\":[%.6f,%.6f,%.6f],\"arm_velocity\":[%.6f,%.6f,%.6f],"
        "\"armed_stationary\":%s,\"auto_enemy_pickup\":%s,\"max_rows\":%d,\"max_ms\":%d,"
        "\"sv_fps\":%d,\"pmove_fixed\":%d,\"pmove_msec\":%d,\"pmove_float\":%d,\"g_gravity\":%d,\"g_speed\":%d,"
        "\"g_forcePowerDisable\":%d,\"g_maxForceRank\":%d,\"sv_cheats\":%d,\"clock\":\"%s\"}\n",
        client,bot?"false":"true",bot?"bot":"human",bot?releaseLevel:-1,settings,
        r->epoch,safeMap,atoi(checksum),r->team,level.gametype,level.time,
        ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],
        r->armedStationary?"true":"false",r->autoPickup?"true":"false",ROUTE_MAX_ROWS,ROUTE_MAX_MS,
        trap->Cvar_VariableIntegerValue("sv_fps"),pmove_fixed.integer || ent->client->pers.pmoveFixed,
        trap->Cvar_VariableIntegerValue("pmove_msec"),trap->Cvar_VariableIntegerValue("pmove_float"),
        trap->Cvar_VariableIntegerValue("g_gravity"),trap->Cvar_VariableIntegerValue("g_speed"),
        trap->Cvar_VariableIntegerValue("g_forcePowerDisable"),trap->Cvar_VariableIntegerValue("g_maxForceRank"),sv_cheats.integer,
        bot?"level.time: corner release frame to post-touch enemy flag possession; server-frame resolution":
            "level.time: first effective movement/jump command to post-touch enemy flag possession; server-frame resolution");
    Write(meta,text);trap->FS_Close(meta);
    // An empty summary means the process ended without closing the recording.
    Write(r->csv,"row,level,elapsed_ms,cmd_time,forward,right,up,buttons,generic_cmd,weapon,cmd_pitch,cmd_yaw,cmd_roll,pmove_fixed,pmove_msec,pmove_float,");
    {
        const char *prefix[3]={"pre","post","final"};int i;
        for(i=0;i<3;i++) {
            Com_sprintf(text,sizeof(text),"%s_x,%s_y,%s_z,%s_vx,%s_vy,%s_vz,%s_pitch,%s_yaw,%s_roll,%s_ground,%s_cmd_time,%s_pm_flags,%s_pm_time,%s_pm_type,%s_health,%s_force,%s_active_powers,%s_speed_until,%s_jump_level,%s_speed_level,%s_redflag,%s_blueflag,%s_gravity,%s_speed%s",
                prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],
                prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],prefix[i],i==2?"\n":",");
            Write(r->csv,text);
        }
    }
    r->active=1;activeRecords++;
    if(bot) {
        // The release frame is the root. The fixture has already placed the bot
        // and cleared its state, so the first command it is given belongs to the
        // trip whether or not it moves the body; waiting for a non-zero command
        // would drop the frames the release itself decides. The talk catcher is
        // a human's, and a bot never sets it.
        r->started=1;r->startTime=releaseLevel;
        r->stationary=Stationary(ps);r->startGround=ps->groundEntityNum;r->startCarrying=Carrying(ps);
        r->lastTeleport=ps->eFlags&EF_TELEPORT_BIT;r->lastForce=ps->fd.forcePower;
        if(r->startCarrying)r->autoPickup=0;
        Tell(client,va("armed on the release level %d (bot subject); %s.csv",releaseLevel,r->stem));
        return;
    }
    Tell(client,va("armed; first movement/jump starts the clock; %s.csv",r->stem));
    trap->SendServerCommand(client,"cp \"Route recorder armed: move or jump to start\"");
}

static void Arm(int client) {
    gentity_t *ent=&g_entities[client];
    if(records[client].active) {Tell(client,"already armed; use stop before arming again");return;}
    if(!ent->inuse || !ent->client || ent->client->pers.connected!=CON_CONNECTED ||
       (ent->r.svFlags&SVF_BOT) || ent->client->sess.sessionTeam==TEAM_SPECTATOR ||
       ent->health<=0 || ent->client->ps.pm_type!=PM_NORMAL || ent->client->noclip || level.intermissiontime) {
        Tell(client,"arm requires a living human player in normal play (not a bot, spectator or noclip)");return;
    }
    ArmSubject(client,0,0);
}

// The one bot entry, called by the corner fixture on the release frame it has
// just placed the bot on. bot_routeRecord 2 is the declaration that this world
// records its bot; at 0 or 1 nothing here happens and the world is unchanged.
void RouteRecord_ArmBot(int client,int releaseLevel) {
    gentity_t *ent;
    if(client<0 || client>=level.maxclients)return;
    trap->Cvar_Update(&enabled);
    if(enabled.integer!=2 || !sv_cheats.integer)return;
    ent=&g_entities[client];
    if(records[client].active) {Tell(client,"already recording; the release armed nothing");return;}
    if(!ent->inuse || !ent->client || ent->client->pers.connected!=CON_CONNECTED ||
       !(ent->r.svFlags&SVF_BOT) || ent->client->sess.sessionTeam==TEAM_SPECTATOR ||
       ent->health<=0 || ent->client->ps.pm_type!=PM_NORMAL || ent->client->noclip || level.intermissiontime) {
        Tell(client,"bot arm requires a living released bot on a team in normal play");return;
    }
    ArmSubject(client,1,releaseLevel);
}

void RouteRecord_Init(void) {
    memset(records,0,sizeof(records));activeRecords=0;
    trap->Cvar_Register(&enabled,"bot_routeRecord","0",0);
}

void RouteRecord_Command(int client,const char *action) {
    if(client<0 || client>=level.maxclients)return;
    if(!Q_stricmp(action,"stop")) {if(records[client].active)Finish(client,"manual_stop",0);else Tell(client,"not armed");return;}
    if(!Q_stricmp(action,"status")) {Tell(client,records[client].active?va("%s; %d commands; %s.csv",records[client].started?"recording":"armed",records[client].rows,records[client].stem):"not armed");return;}
    trap->Cvar_Update(&enabled);
    if(!enabled.integer || !sv_cheats.integer) {Tell(client,"requires bot_routeRecord 1 and sv_cheats 1 on your practice server");return;}
    if(!Q_stricmp(action,"arm"))Arm(client);
    else Tell(client,"usage: cmd route_record arm|stop|status");
}

qboolean RouteRecord_ConsoleCommand(void) {
    char name[64],slot[32],action[32];int client;
    trap->Argv(0,name,sizeof(name));if(Q_stricmp(name,"route_record"))return qfalse;
    trap->Argv(1,slot,sizeof(slot));trap->Argv(2,action,sizeof(action));
    if(trap->Argc()!=3 || !slot[0] || strlen(slot)>3 || strspn(slot,"0123456789")!=strlen(slot)) {
        trap->Print("Usage: route_record <human client number> arm|stop|status\n");return qtrue;
    }
    client=atoi(slot);if(client<0 || client>=level.maxclients)trap->Print("route_record: invalid client\n");
    else RouteRecord_Command(client,action);
    return qtrue;
}

void RouteRecord_Disconnect(int client) {if(client>=0 && client<MAX_CLIENTS)Finish(client,"disconnect",0);}
void RouteRecord_Shutdown(void) {int i;for(i=0;i<MAX_CLIENTS;i++)Finish(i,"map_shutdown",0);}

void RouteRecord_Frame(void) {
    int i;if(!activeRecords)return;
    trap->Cvar_Update(&enabled);
    for(i=0;i<level.maxclients;i++)if(records[i].active) {
        routeRecord_t *r=&records[i];gentity_t *ent=&g_entities[i];
        if(!enabled.integer || !sv_cheats.integer)Finish(i,"disabled",0);
        else if(!ent->inuse || !ent->client || ent->client->pers.connected!=CON_CONNECTED)Finish(i,"disconnect",0);
        else if(ent->health<=0 || ent->client->ps.pm_type==PM_DEAD)Finish(i,"death",0);
        else if(ent->client->sess.sessionTeam!=r->team || ent->client->sess.sessionTeam==TEAM_SPECTATOR)Finish(i,"team_change",0);
        else if(ent->client->noclip)Finish(i,"noclip",0);
        else if(level.time-r->armTime>ROUTE_ARM_MS || (r->started && level.time-r->startTime>ROUTE_MAX_MS))Finish(i,"time_limit",0);
    }
}

void RouteRecord_Before(gentity_t *ent,const pmove_t *move) {
    routeRecord_t *r;int client=ent->s.number;
    if(!activeRecords || client<0 || client>=level.maxclients || !records[client].active)return;
    r=&records[client];
    if(ent->health<=0 || ent->client->ps.pm_type==PM_DEAD) {Finish(client,"death",0);return;}
    if(ent->client->sess.sessionTeam!=r->team) {Finish(client,"team_change",0);return;}
    if(ent->client->noclip) {Finish(client,"noclip",0);return;}
    if(move->cmd.serverTime<=move->ps->commandTime)return;
    if(r->pending) {Finish(client,"missing_post_effects",0);return;}
    if(r->rows>=ROUTE_MAX_ROWS) {Finish(client,"row_limit",0);return;}
    if(!r->started) {
        // Native Pmove suppresses movement while a menu/chat catcher is active.
        if(move->cmd.buttons&BUTTON_TALK)return;
        if(!move->cmd.forwardmove && !move->cmd.rightmove && move->cmd.upmove<=0)return;
        r->started=1;r->startTime=level.time;r->startCommand=move->cmd.serverTime;r->stationary=Stationary(move->ps);
        r->startGround=move->ps->groundEntityNum;r->startCarrying=Carrying(move->ps);
        r->lastTeleport=move->ps->eFlags&EF_TELEPORT_BIT;
        r->lastForce=move->ps->fd.forcePower;
        if(r->startCarrying)r->autoPickup=0;
        Tell(client,va("started (%s root); %s.csv",r->stationary?"stationary":"moving",r->stem));
        trap->SendServerCommand(client,"cp \"Route recorder started\"");
    }
    if(r->startCommand<0)r->startCommand=move->cmd.serverTime;
    if(move->ps->fd.forcePower<r->lastForce)r->forceDecrease=1;
    if((move->ps->eFlags&EF_TELEPORT_BIT)!=r->lastTeleport)r->teleportSeen=1;
    r->before=*move->ps;r->command=move->cmd;r->rawLevel=level.time;
    r->pmoveFixed=move->pmove_fixed;r->pmoveMsec=move->pmove_msec;r->pmoveFloat=move->pmove_float;r->pending=1;
}

void RouteRecord_AfterPmove(gentity_t *ent,const pmove_t *move) {
    int client=ent->s.number;if(!activeRecords || client<0 || client>=level.maxclients)return;
    if(records[client].active && records[client].pending)records[client].after=*move->ps;
}

static void Snapshot(fileHandle_t file,const playerState_t *ps,int last) {
    char text[1024];
    Com_sprintf(text,sizeof(text),"%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f%s",
        ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],
        ps->viewangles[0],ps->viewangles[1],ps->viewangles[2],ps->groundEntityNum,ps->commandTime,ps->pm_flags,ps->pm_time,
        ps->pm_type,ps->stats[STAT_HEALTH],ps->fd.forcePower,ps->fd.forcePowersActive,ps->fd.forcePowerDuration[FP_SPEED],
        ps->fd.forcePowerLevel[FP_LEVITATION],ps->fd.forcePowerLevel[FP_SPEED],ps->powerups[PW_REDFLAG],ps->powerups[PW_BLUEFLAG],
        ps->gravity,ps->speed,last?"\n":",");
    Write(file,text);
}

void RouteRecord_AfterEffects(gentity_t *ent) {
    int client=ent->s.number;routeRecord_t *r;const playerState_t *ps;char text[512];
    if(!activeRecords || client<0 || client>=level.maxclients)return;
    r=&records[client];if(!r->active || !r->pending)return;
    ps=&ent->client->ps;
    if((ps->eFlags&EF_TELEPORT_BIT)!=r->lastTeleport)r->teleportSeen=1;
    r->lastTeleport=ps->eFlags&EF_TELEPORT_BIT;
    Com_sprintf(text,sizeof(text),"%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,",r->rows,r->rawLevel,
        r->rawLevel-r->startTime,r->command.serverTime,r->command.forwardmove,r->command.rightmove,r->command.upmove,
        r->command.buttons,r->command.generic_cmd,r->command.weapon,r->command.angles[0],r->command.angles[1],r->command.angles[2],
        r->pmoveFixed,r->pmoveMsec,r->pmoveFloat);
    Write(r->csv,text);Snapshot(r->csv,&r->before,0);Snapshot(r->csv,&r->after,0);Snapshot(r->csv,ps,1);
    r->rows++;r->pending=0;r->powerSeen|=r->before.fd.forcePowersActive|r->after.fd.forcePowersActive|ps->fd.forcePowersActive;
    if(r->command.upmove>0) {r->positiveUpFrames++;if(r->consecutiveUp)r->heldUp=1;r->consecutiveUp=1;}else r->consecutiveUp=0;
    if(ps->fd.forcePower<r->before.fd.forcePower || r->after.fd.forcePower<r->before.fd.forcePower)r->forceDecrease=1;
    r->lastForce=ps->fd.forcePower;
    if(ent->health<=0 || ps->pm_type==PM_DEAD)Finish(client,"death",0);
    else if(r->autoPickup && ps->powerups[r->team==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG])Finish(client,"enemy_flag_pickup",1);
}
