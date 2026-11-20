// ==============================================================================
// bot2_svcmds.c - Bot2 server (rcon / dedicated console) commands.
//
// The play commands (addrunner, botplayrate, botleveltime, botat) and the play
// log; the svcmd table in g_svcmds.c references them by name.
// ==============================================================================

#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "ai_bot2_internal.h"
#include "bot2_svcmds.h"

static fileHandle_t playLog;
static struct {int next,health,flag,captures,objective,thinkTime,enterTime;} playLast[MAX_CLIENTS];

void Bot2_PlayLogStop(void) {
    if(playLog)trap->FS_Close(playLog);
    playLog=0;memset(playLast,0,sizeof(playLast));
}

// The archived settings addrunner sets. Cvar_Set on a CVAR_ARCHIVE or
// CVAR_ARCHIVE_ND cvar writes it into the profile's saved config, so one addrunner left "server demos for every
// client, bots included, never pruned" (and pmove_float 1) on for every later session from that home. The values
// before the first addrunner of a game are kept and put back when the game shuts down for good (a map change or a
// quit, not a map_restart: G_ShutdownGame), so the next configuration write saves what was there before. The play
// log is per game too (Bot2_PlayLogStop), so a later map runs addrunner again. bot_telemetry is no longer archived.
static const char *playArchived[]={"sv_autoDemo","sv_autoDemoBots","sv_autoDemoMaxMaps","pmove_float","pmove_msec",
                                    "g_forceRegenTime","g_forcePowerDisable","g_jediVmerc"};
static char playArchivedWas[ARRAY_LEN(playArchived)][MAX_CVAR_VALUE_STRING];
static int playArchivedKept;
static void PlayArchivedKeep(void) {
    int i;
    if(playArchivedKept)return;
    for(i=0;i<ARRAY_LEN(playArchived);i++)trap->Cvar_VariableStringBuffer(playArchived[i],playArchivedWas[i],sizeof(playArchivedWas[i]));
    playArchivedKept=1;
}
void Bot2_PlayRestoreArchived(int restart) {
    int i;
    if(restart || !playArchivedKept)return;
    for(i=0;i<ARRAY_LEN(playArchived);i++)trap->Cvar_Set(playArchived[i],playArchivedWas[i]);
    playArchivedKept=0;
    trap->Print("[BOT2 PLAY] addrunner settings restored: sv_autoDemo=%s sv_autoDemoMaxMaps=%s pmove_float=%s\n",
        playArchivedWas[0],playArchivedWas[2],playArchivedWas[3]);
}

// Fixture-only 40 Hz movement-only momentum record (bot_momentumTrace); no
// state edits. Read by tools/movement_momentum_audit.py and its kin. The
// route, branch, lane, index and group fields belonged to the pre-rewrite
// route follower (since removed) and are always -1, as they were
// whenever the rewrite ran.
static void MomentumTrace(int clientNum,const usercmd_t *cmd) {
    gentity_t *ent=&g_entities[clientNum];playerState_t *ps;
    if(!trap->Cvar_VariableIntegerValue("bot_momentumTrace") || !ent->inuse || !ent->client)return;
    ps=&ent->client->ps;
    if(!Bot2_IsMovementOnly(clientNum))return;
    Bot2_PrintTelemetry(1,"[MOMENTUM] level=%d client=%d speed=%.1f vz=%.1f ground=%d fwd=%d right=%d up=%d owner=%s force=%d speed_ms=%d route=%d branch=%d lane=%d index=%d group=%d yaw=%.2f\n",
        level.time,clientNum,sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]),
        ps->velocity[2],ps->groundEntityNum!=ENTITYNUM_NONE,cmd->forwardmove,cmd->rightmove,cmd->upmove,
        Bot2_MovementOwner(clientNum),ps->fd.forcePower,
        (ps->fd.forcePowersActive&(1<<FP_SPEED))?ps->fd.forcePowerDuration[FP_SPEED]-level.time:0,
        -1,-1,-1,-1,-1,ps->viewangles[YAW]);
}

// Native state before submission of this command. Four Hz plus every
// death/respawn, possession, capture and objective transition; no state edits.
// The route_active, route, index and release columns belonged to the
// pre-rewrite route follower (since removed); they keep the values the
// rewrite always wrote (0, 0, 0, none) so the header stays stable.
void Bot2_PlayLogSample(int clientNum,const usercmd_t *cmd) {
    gentity_t *ent;playerState_t *ps;bot2_state_t *bs;
    int flag,thinkStep;const char *movementOwner;char row[1024],rate[32],flagStatus[16];
    if(!playLog || clientNum<0 || clientNum>=MAX_CLIENTS)return;
    // Every command frame, ahead of the four-hertz gate below: a braking rule
    // that lasts one frame leaves no trace in the sampled rows.
    MomentumTrace(clientNum,cmd);
    ent=&g_entities[clientNum];ps=&ent->client->ps;bs=&bot2_states[clientNum];
    if(playLast[clientNum].enterTime!=ent->client->pers.enterTime) {
        // A new occupant of the slot: none of the last one's row state.
        memset(&playLast[clientNum],0,sizeof(playLast[clientNum]));
        playLast[clientNum].enterTime=ent->client->pers.enterTime;
    }
    thinkStep=playLast[clientNum].thinkTime?level.time-playLast[clientNum].thinkTime:0;
    playLast[clientNum].thinkTime=level.time;
    flag=!!ps->powerups[ent->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG];
    if(level.time<playLast[clientNum].next &&
       (ent->health>0)==(playLast[clientNum].health>0) && flag==playLast[clientNum].flag &&
       ps->persistant[PERS_CAPTURES]==playLast[clientNum].captures && bs->macroState==playLast[clientNum].objective)return;
    playLast[clientNum].next=level.time+250;playLast[clientNum].health=ent->health;
    playLast[clientNum].flag=flag;playLast[clientNum].captures=ps->persistant[PERS_CAPTURES];playLast[clientNum].objective=bs->macroState;
    movementOwner=Bot2_MovementOwner(clientNum);
    trap->Cvar_VariableStringBuffer("timescale",rate,sizeof(rate));
    trap->GetConfigstring(CS_FLAGSTATUS,flagStatus,sizeof(flagStatus));
    Com_sprintf(row,sizeof(row),"%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%d,%d,%s\n",
        level.time,clientNum,ent->client->sess.sessionTeam,bs->macroState,bs->targetEntNum,
        ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],
        ent->health,ps->fd.forcePower,ps->fd.forcePowersActive,ps->groundEntityNum,flag,
        ps->persistant[PERS_CAPTURES],cmd->forwardmove,cmd->rightmove,cmd->upmove,cmd->buttons,cmd->generic_cmd,
        0,0,0,ps->commandTime,ps->fd.forcePowerLevel[FP_SPEED],ps->fd.forcePowerLevel[FP_LEVITATION],"none",
        ps->viewangles[YAW],SHORT2ANGLE(cmd->angles[YAW]+ps->delta_angles[YAW]),ps->pm_flags,Bot2_IsMovementOnly(clientNum),
        bs->role,level.time-level.previousTime,thinkStep,trap->Cvar_VariableIntegerValue("sv_fps"),atof(rate),
        bs->macroTargetOrigin[0],bs->macroTargetOrigin[1],bs->macroTargetOrigin[2],
        flagStatus[ent->client->sess.sessionTeam==TEAM_RED?0:1]-'0',
        flagStatus[ent->client->sess.sessionTeam==TEAM_RED?1:0]-'0',movementOwner);
    trap->FS_Write(row,strlen(row),playLog);
}

// Research-only stream initialization before any actor exists. Ordinary play
// keeps the engine's map-start seed. Never reseed during a client's lifecycle.
static qboolean PlayDecisionSeed(int *seed) {
    char text[64];int i,value=0;
    *seed=0;
    trap->Cvar_VariableStringBuffer("bot_playDecisionSeed",text,sizeof(text));
    if(!text[0] || !strcmp(text,"0"))return qtrue;
    for(i=0;text[i];i++) {
        int digit=text[i]-'0';
        if(digit<0 || digit>9 || value>(2147483647-digit)/10) {
            trap->Print("addrunner: decision seed must be 1..2147483647.\n");return qfalse;
        }
        value=value*10+digit;
    }
    if(!value || !trap->Cvar_VariableIntegerValue("dedicated") || playLog) {
        trap->Print("addrunner: decision seed requires a fresh dedicated world before actors.\n");return qfalse;
    }
    for(i=0;i<level.maxclients;i++)if(g_entities[i].inuse ||
        (g_entities[i].client && g_entities[i].client->pers.connected!=CON_DISCONNECTED)) {
        trap->Print("addrunner: decision seed refused while a client exists.\n");return qfalse;
    }
    *seed=value;return qtrue;
}

void Svcmd_AddRunner_f(void) {
    char team[16],path[MAX_QPATH];int i,freeSlot=0,serial,decisionSeed;qtime_t now;const char *runnerName="Runner_v2";
    const char *header="level_time,client,team,objective,target,x,y,z,vx,vy,vz,health,force,powers_active,ground,carrying,captures,forward,right,up,buttons,generic,route_active,route,index,ps_time,speed_level,jump_level,release,yaw,command_yaw,pm_flags,movement_only,role,frame_msec,think_msec,sv_fps,timescale,goal_x,goal_y,goal_z,home_flag_status,enemy_flag_status,movement_owner\n";
    const char *settings[][2]={
        {"sv_fps","40"},{"timescale","1"},{"bot_pmoveFixed","1"},{"pmove_msec","8"},{"pmove_float","1"},
        {"g_speed","250"},{"g_gravity","800"},{"g_stepSlideFix","1"},{"g_forceRegenTime","200"},
        {"g_forcePowerDisable","0"},{"g_maxForceRank","7"},{"g_forceBasedTeams","0"},{"g_jediVmerc","0"},
        {"bot_movementOnly","2"},{"bot_telemetry","1"},
        {"bot_moveCost","0"},{"bot_movebench_hold","0"},
        {"sv_autoDemoMaxMaps","0"},{"sv_autoDemoBots","1"},{"sv_autoDemo","1"}
    };
    // Any CTF map. The objective is the map's own flags (or a declared goal
    // chain, bot_goalChain).
    if(level.gametype!=GT_CTF) {
        trap->Print("addrunner: Create Game -> Capture the Flag first.\n");return;
    }
    if(!trap->Cvar_VariableIntegerValue("bot_enable")) {
        trap->Print("addrunner: set bot_enable 1, then recreate the local game.\n");return;
    }
    trap->Argv(1,team,sizeof(team));
    if(!team[0]) {
        Q_strncpyz(team,"red",sizeof(team));
        for(i=0;i<level.maxclients;i++)if(g_entities[i].inuse && g_entities[i].client &&
            !(g_entities[i].r.svFlags&SVF_BOT) && g_entities[i].client->sess.sessionTeam==TEAM_RED) {
            Q_strncpyz(team,"blue",sizeof(team));break;
        }
    }
    if(Q_stricmp(team,"red") && Q_stricmp(team,"blue")) {trap->Print("Usage: addrunner [red|blue]\n");return;}
    for(i=0;i<level.maxclients;i++)if(!g_entities[i].inuse)freeSlot++;
    if(!freeSlot) {trap->Print("addrunner: no free player slot. Increase Max Players in Create Game.\n");return;}
    // A second addrunner (the other team's runner, milestone B2) adds Runner2_v2.
    for(i=0;i<level.maxclients;i++) {
        char other[MAX_NETNAME];
        if(!g_entities[i].inuse || !g_entities[i].client)continue;
        Q_strncpyz(other,g_entities[i].client->pers.netname,sizeof(other));Q_CleanStr(other);
        if(!Q_stricmp(other,"Runner_v2"))runnerName="Runner2_v2";
    }
    if(!G_GetBotInfoByName(runnerName)) {trap->Print("addrunner: %s missing from scripts/kejim_runner.bot; install it and recreate the game.\n",runnerName);return;}
    if(!PlayDecisionSeed(&decisionSeed))return;
    if(!playLog) {
        trap->RealTime(&now);
        for(serial=0;serial<100;serial++) {
            fileHandle_t existing=0;int length;
            Com_sprintf(path,sizeof(path),"bot2-play/%04d%02d%02d-%02d%02d%02d-%d-%d.csv",
                now.tm_year+1900,now.tm_mon+1,now.tm_mday,now.tm_hour,now.tm_min,now.tm_sec,level.time,serial);
            length=trap->FS_Open(path,&existing,FS_READ);
            if(existing)trap->FS_Close(existing);
            if(length<0)break;
        }
        if(serial==100) {trap->Print("addrunner: could not select a new telemetry filename.\n");return;}
        trap->FS_Open(path,&playLog,FS_APPEND_SYNC);
        if(!playLog) {trap->Print("addrunner: could not open telemetry file.\n");return;}
        trap->FS_Write(header,strlen(header),playLog);
        trap->Print("[BOT2 PLAY] telemetry: %s (4 Hz plus transitions; native state before command)\n",path);
    }
    PlayArchivedKeep();
    for(i=0;i<ARRAY_LEN(settings);i++)trap->Cvar_Set(settings[i][0],settings[i][1]);
    trap->Cvar_Update(&g_forcePowerDisable);trap->Cvar_Update(&g_maxForceRank);
    trap->Cvar_Update(&g_forceBasedTeams);trap->Cvar_Update(&g_jediVmerc);
    if(decisionSeed) {
        Rand_Init(decisionSeed);srand(decisionSeed);
        trap->Print("[BOT2 PLAY] SEED seed=%d level=%d scope=pre_runner\n",decisionSeed,level.time);
    }
    trap->SendConsoleCommand(EXEC_APPEND,va("addbot %s 3 %s 0 %s\n",runnerName,team,runnerName));
    trap->Print("[BOT2 PLAY] Adding %s flag runner. Movement only; normal damage and respawns. Server demos enabled, automatic pruning disabled.\n",team);
}

void Svcmd_BotPlayRate_f(void) {
    char arg[32],*end;double rate;int fps;
    trap->Argv(1,arg,sizeof(arg));rate=strtod(arg,&end);
    if(trap->Argc()!=2 || !arg[0] || *end || (rate!=1 && rate!=2.5 && rate!=5 && rate!=12.5 && rate!=25) ||
       trap->Cvar_VariableIntegerValue("bot_movementOnly")!=2) {
        trap->Print("Usage after addrunner: botplayrate 1|2.5|5|12.5|25\n");return;
    }
    // SV_Frame does integer division before scaling. These exact pairs all
    // produce a 25 ms game tick, including every batched accelerated tick.
    // 12.5 and 25 (cheaper laps worlds) run as fast as the CPU
    // allows; the caller waits on game time (botleveltime), not wall time.
    fps=rate==1?40:rate==2.5?100:rate==5?200:rate==12.5?500:1000;
    trap->Cvar_Set("sv_fps",va("%d",fps));trap->Cvar_Set("timescale",arg);
    trap->Print("[BOT2 PLAY] RATE timescale=%.1f sv_fps=%d game_step=25\n",rate,fps);
}

// botat <level_ms> <command...>: run a console command once the game clock
// reaches level_ms (Bot2_AtFrame, every play frame), so a fixture's setup
// lands on the same game frame in every run (with g_randomSeed, worlds that
// start alike replay alike; tools/defense_laps.py --seed). Up to 32 pending.
static struct {int time;char text[192];} botAt[32];
static int botAtCount;
void Svcmd_BotAt_f(void) {
    char arg[32],*end;int i;long due;
    if(trap->Argc()<3 || botAtCount>=32) {trap->Print("Usage: botat <level_ms> <command...> (at most 32 pending)\n");return;}
    trap->Argv(1,arg,sizeof(arg));
    due=strtol(arg,&end,10);
    if(!arg[0] || *end || due<0) {trap->Print("botat: level_ms must be a whole number of milliseconds, not \"%s\"\n",arg);return;}
    botAt[botAtCount].time=(int)due;botAt[botAtCount].text[0]=0;
    for(i=2;i<trap->Argc();i++) {
        char word[128];trap->Argv(i,word,sizeof(word));
        // A word the console split on, or one holding a separator, goes back
        // quoted, so a quoted ';' stays an argument instead of ending the command.
        Q_strcat(botAt[botAtCount].text,sizeof(botAt[0].text),
                 va(strpbrk(word," \t;")?(i>2?" \"%s\"":"\"%s\""):(i>2?" %s":"%s"),word));
    }
    if(strlen(botAt[botAtCount].text)>=sizeof(botAt[0].text)-1) {trap->Print("botat: command too long\n");return;}
    botAtCount++;
}

// botatran <due> <dispatched> (queued by Bot2_AtFrame after each command): the
// level the command actually ran at. The command buffer runs between engine
// frames, so an engine frame that catches up several game frames runs it
// late; late=0 ran before the next game frame, as intended.
void Svcmd_BotAtRan_f(void) {
    char due[16],dispatched[16];
    trap->Argv(1,due,sizeof(due));trap->Argv(2,dispatched,sizeof(dispatched));
    trap->Print("[BOT2 PLAY] AT_RAN level=%d due=%s dispatched=%s late=%d\n",level.time,due,dispatched,level.time-atoi(dispatched));
}
// bot_stateTrace "from to" (level ms, unset off): every client's and missile's
// exact state at the start of each game frame in the window, as hex floats,
// so two replays of one seed can be diffed to the first entity that differs.
static void StateTrace(void) {
    char text[64];int from=0,to=0,i;
    trap->Cvar_VariableStringBuffer("bot_stateTrace",text,sizeof(text));
    if(!text[0] || sscanf(text,"%d %d",&from,&to)!=2 || level.time<from || level.time>to)return;
    for(i=0;i<level.num_entities;i++) {
        gentity_t *e=&g_entities[i];
        if(!e->inuse)continue;
        if(e->client && i<MAX_CLIENTS) {
            const playerState_t *ps=&e->client->ps;
            trap->Print("[STATE] level=%d ent=%d client o=%a,%a,%a v=%a,%a,%a h=%d f=%d ct=%d g=%d wt=%d w=%d ws=%d yaw=%a\n",level.time,i,
                ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],e->health,ps->fd.forcePower,
                ps->commandTime,ps->groundEntityNum,ps->weaponTime,ps->weapon,ps->weaponstate,ps->viewangles[YAW]);
            if(botstates[i]) {
                const usercmd_t *c=&botstates[i]->lastucmd;
                trap->Print("[STATE] level=%d ent=%d cmd t=%d f=%d r=%d u=%d b=%d g=%d a=%d,%d,%d w=%d\n",level.time,i,c->serverTime,
                    c->forwardmove,c->rightmove,c->upmove,c->buttons,c->generic_cmd,c->angles[0],c->angles[1],c->angles[2],c->weapon);
            }
        } else if(e->s.eType==ET_MISSILE) {
            trap->Print("[STATE] level=%d ent=%d missile owner=%d w=%d tr=%d t=%d base=%a,%a,%a d=%a,%a,%a cur=%a,%a,%a\n",level.time,i,
                e->r.ownerNum,e->s.weapon,e->s.pos.trType,e->s.pos.trTime,e->s.pos.trBase[0],e->s.pos.trBase[1],e->s.pos.trBase[2],
                e->s.pos.trDelta[0],e->s.pos.trDelta[1],e->s.pos.trDelta[2],e->r.currentOrigin[0],e->r.currentOrigin[1],e->r.currentOrigin[2]);
        }
    }
}
void Bot2_AtFrame(void) {
    int i,j;
    StateTrace();
    for(i=0;i<botAtCount;) {
        if(level.time<botAt[i].time) {i++;continue;}
        trap->Print("[BOT2 PLAY] AT level=%d due=%d %s\n",level.time,botAt[i].time,botAt[i].text);
        trap->SendConsoleCommand(EXEC_APPEND,va("%s\nbotatran %d %d\n",botAt[i].text,botAt[i].time,level.time));
        for(j=i+1;j<botAtCount;j++)botAt[j-1]=botAt[j];
        botAtCount--;
    }
}

// botkill <name>: the named client kills itself (/kill, MOD_SUICIDE), at once. Fixture setup only: a seeded laps
// world's forced case (tools/defense_laps.py --kill-before-release: a defender dead at the release).
void G_Kill(gentity_t *ent);   // g_cmds.c
void Svcmd_BotKill_f(void) {
    char want[MAX_NETNAME],name[MAX_NETNAME];int i;
    if(trap->Argc()<2) {trap->Print("Usage: botkill <name>\n");return;}
    trap->Argv(1,want,sizeof(want));
    for(i=0;i<level.maxclients;i++) {
        gentity_t *ent=&g_entities[i];
        if(!ent->inuse || !ent->client || ent->client->pers.connected!=CON_CONNECTED)continue;
        Q_strncpyz(name,ent->client->pers.netname,sizeof(name));Q_CleanStr(name);
        if(Q_stricmp(name,want))continue;
        trap->Print("[BOT2 PLAY] KILL level=%d client=%d name=%s health=%d\n",level.time,i,name,ent->health);
        if(ent->health>0)G_Kill(ent);
        return;
    }
    trap->Print("botkill: no client named %s\n",want);
}

// botleveltime: the game clock, for a caller that waits on game time.
void Svcmd_BotLevelTime_f(void) {
    trap->Print("[BOT2 PLAY] LEVEL_TIME level=%d\n",level.time);
}
