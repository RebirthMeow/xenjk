// The runner (offense) policy, called by Bot2_Think for a runner each game
// frame; fixtures never supply a per-leg destination.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "ai_bot2_internal.h"
#include "g_navmesh.h"
#include "bot2_move.h"
#include "bot2_graph.h"
#include "bot2_defender.h"
#include "bot2_movecost.h"

// Review batch-2 fixes: on by default; a cvar set to 0
// turns its fix off (records/2026-11-10-review-batch2.md).
static int FixOn(const char *name) {char v[16];trap->Cvar_VariableStringBuffer(name,v,sizeof(v));return !v[0] || atoi(v)!=0;}

// Each runner caches its flag checks for half a second; all runners share
// one query per tick. A dropped flag can remain active at the bottom of a death pit
// until its native return timer expires; it is not necessarily a usable goal.
static struct { int entity,time,checked,reachable; } dropChecks[MAX_CLIENTS][2];
static int dropQueryTime;

// A hurt volume or liquid where the flag lies, tested with `ent`'s hull: the hazard half of the runner's test.
static int DropHazard(gentity_t *ent,gentity_t *flag) {
    int count,i,entities[64],hazard=0;vec3_t mins,maxs;
    VectorAdd(flag->r.currentOrigin,ent->r.mins,mins);
    VectorAdd(flag->r.currentOrigin,ent->r.maxs,maxs);
    count=trap->EntitiesInBox(mins,maxs,entities,64);
    if(count==64)hazard=1;
    for(i=0;i<count;i++) {
        gentity_t *hit=&g_entities[entities[i]];
        if(hit->inuse && (hit->r.contents&CONTENTS_TRIGGER) && hit->classname &&
           !Q_stricmp(hit->classname,"trigger_hurt") &&
           trap->EntityContact(mins,maxs,(sharedEntity_t*)hit,qfalse))hazard=1;
    }
    if(trap->PointContents(flag->r.currentOrigin,ent->s.number)&(CONTENTS_LAVA|CONTENTS_SLIME))hazard=1;
    return hazard;
}

// 1 reachable, 0 not, -1 not answered yet for this flag (another caller had this tick's query): the runner reads
// -1 as no (Bot2_ReachableDrop, as before); chase D and the camper keep going (bot_defenderDropReach).
int Bot2_DropReach(int clientNum,gentity_t *flag,int team) {
    gentity_t *ent=&g_entities[clientNum];
    int slot=team==TEAM_RED?0:1,count,hazard=0,reachable=0;
    vec3_t points[256];
    if(dropChecks[clientNum][slot].checked && dropChecks[clientNum][slot].entity==flag->s.number &&
       level.time>=dropChecks[clientNum][slot].time && level.time-dropChecks[clientNum][slot].time<500)
        return dropChecks[clientNum][slot].reachable;
    // Across all runners, at most one path query and one bounded hazard
    // neighborhood per tick. A pending check stages at the stand meanwhile.
    // A check that expired keeps its last answer until its turn comes (a
    // denied one read as unreachable and flipped the goal for a frame).
    if(dropQueryTime==level.time)return dropChecks[clientNum][slot].checked &&
        dropChecks[clientNum][slot].entity==flag->s.number?dropChecks[clientNum][slot].reachable:-1;
    dropQueryTime=level.time;
    hazard=DropHazard(ent,flag);
    if(!hazard && Graph_CellCount()>0) {
        // The movement graph answers as the planner will: from the cell the
        // bot plans from to the objective's cell. A flag off every admitted
        // cell (a pit, a hurt volume) has none. No navmesh needed.
        static int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
        int from=Move_RootCell(ent->client->ps.origin),to=Graph_Nearest(flag->r.currentOrigin,64,64);
        reachable=from>=0 && to>=0 && Graph_PathTable(from,to,cells,edges,GRAPH_MAX_PATH,NULL)>0;
    } else if(!hazard) {
        count=NavMesh_GetPath(clientNum,ent->client->ps.origin,flag->r.currentOrigin,&points[0][0],256);
        reachable=count>0 && count<256 && Distance(points[0],ent->client->ps.origin)<=128 &&
            Distance(points[count-1],flag->r.currentOrigin)<=64;
    }
    {
      if(!dropChecks[clientNum][slot].checked || dropChecks[clientNum][slot].entity!=flag->s.number ||
         reachable!=dropChecks[clientNum][slot].reachable)
        Bot2_PrintTelemetry(1,"[OBJECTIVE] dropped_flag entity=%d reachable=%d origin=%.1f,%.1f,%.1f level=%d\n",
            flag->s.number,reachable,flag->r.currentOrigin[0],flag->r.currentOrigin[1],flag->r.currentOrigin[2],level.time);
      dropChecks[clientNum][slot].reachable=reachable;
    }
    dropChecks[clientNum][slot].checked=1;dropChecks[clientNum][slot].entity=flag->s.number;dropChecks[clientNum][slot].time=level.time;
    return dropChecks[clientNum][slot].reachable;
}
qboolean Bot2_ReachableDrop(int clientNum,gentity_t *flag,int team) {return Bot2_DropReach(clientNum,flag,team)>0;}
// For chase D and the camper (bot_defenderDropReach): a flag out of reach is a property of where it
// lies, not of where the asker stands: at rest (a dropped flag bounces and falls first), and with no graph cell
// within 64 u or a hazard on it, as the fix reads. The runner's route test from the asker's own cell
// failed whenever chase D was in the air or off the graph (the second arm: 20 RETURN_SKIP a world, mostly flags on
// ordinary floor); the first arm also counted a pending query as no.
qboolean Bot2_DropOutOfReach(int clientNum,gentity_t *flag,int team) {
    (void)team;
    if(flag->s.pos.trType!=TR_STATIONARY)return qfalse;
    if(Graph_CellCount()>0 && Graph_Nearest(flag->r.currentOrigin,64,64)<0)return qtrue;
    return DropHazard(&g_entities[clientNum],flag);
}

static qboolean Available(gentity_t *flag) {
    // Picked-up items stay linked for native respawning/mover support.
    return flag && flag->r.linked && (flag->r.contents&CONTENTS_TRIGGER) && !(flag->s.eFlags&EF_NODRAW);
}

// The base flag of a team, cached (records/2026-10-03-perf-lossless.md): the runner looked it up four
// times a frame and the defenders up to eight, each a scan of every entity by classname. A team's stand flag is
// spawned once and never freed, so the first non-dropped match is always that entity; the cache holds its index,
// checks it on every use (in use, the classname, not a dropped copy) and scans again when it fails or when level
// time went back (a new map). A map with two base flags for a team is never cached: every call scans, as before.
gentity_t *Bot2_BaseFlag(int team) {
    static struct { int index,time,ambiguous; } cache[2];
    const char *name=team==TEAM_RED?"team_CTF_redflag":"team_CTF_blueflag";
    int t=team==TEAM_RED?0:1;gentity_t *flag=NULL,*first=NULL;int n=0;
    if(level.time<cache[t].time)cache[t].index=0,cache[t].ambiguous=0;
    cache[t].time=level.time;
    if(cache[t].index>0 && !cache[t].ambiguous) {
        gentity_t *e=&g_entities[cache[t].index];
        if(e->inuse && e->classname && !strcmp(e->classname,name) && !(e->flags&FL_DROPPED_ITEM))return e;
    }
    while((flag=G_Find(flag,FOFS(classname),name)))
        if(!(flag->flags&FL_DROPPED_ITEM)) {if(!first)first=flag;n++;}
    cache[t].index=first?first->s.number:0;cache[t].ambiguous=n>1;
    return first;
}

// A team's flag entities (its stand flag and any dropped copy), in entity order: the
// runner, the defenders and the keep-out box each found them by a classname scan of every entity, five or six times
// a frame for a base or chase D, about 30 scans a 4v4 frame. Collected again when the level time, the map, the
// entity count or bot2_entitySerial (any spawn or free, such as a carrier's dropped flag) changes, so what
// Bot2_NextFlag returns is what G_Find(flag,FOFS(classname),<team>flag) would.
unsigned int bot2_entitySerial;
static struct { int time,start,numEntities,count,built;unsigned int serial;int entity[8]; } flagList[2];
gentity_t *Bot2_NextFlag(int team,gentity_t *after) {
    int t=team==TEAM_RED?0:1,i,from=after?(int)(after-g_entities)+1:0;
    if(!flagList[t].built || flagList[t].time!=level.time || flagList[t].start!=level.startTime ||
       flagList[t].numEntities!=level.num_entities || flagList[t].serial!=bot2_entitySerial) {
        gentity_t *flag=NULL;
        flagList[t].count=0;flagList[t].built=1;
        while((flag=G_Find(flag,FOFS(classname),t?"team_CTF_blueflag":"team_CTF_redflag"))!=NULL) {
            if(flagList[t].count>=ARRAY_LEN(flagList[t].entity)) {flagList[t].built=0;break;}   // too many: scan each time
            flagList[t].entity[flagList[t].count++]=(int)(flag-g_entities);
        }
        flagList[t].time=level.time;flagList[t].start=level.startTime;flagList[t].numEntities=level.num_entities;
        flagList[t].serial=bot2_entitySerial;
        if(!flagList[t].built)return G_Find(after,FOFS(classname),t?"team_CTF_blueflag":"team_CTF_redflag");
    }
    for(i=0;i<flagList[t].count;i++)if(flagList[t].entity[i]>=from && g_entities[flagList[t].entity[i]].inuse)
        return &g_entities[flagList[t].entity[i]];
    return NULL;
}

static gentity_t *Flag(int team,qboolean dropped) {
    gentity_t *flag=NULL;
    if(!dropped)return Bot2_BaseFlag(team);
    while((flag=Bot2_NextFlag(team,flag)))
        if(!!(flag->flags&FL_DROPPED_ITEM)==!!dropped && (!dropped || Available(flag)))return flag;
    return NULL;
}

// Pressed-carrier melee (bot_carrierMelee): a
// movement-only runner under 20 u/s with a live
// opposing player within 48 units (bot_carrierMeleeRange) faces that player and swings the saber
// forward. Every Q3 distant stall was this standstill with a defender at
// 30 units; movement keeps its command whenever it actually moves, so only
// that dead time is spent. Prints one line per swing episode.
static int meleeSince[MAX_CLIENTS];

static void CarrierMelee(int clientNum,usercmd_t *cmd) {
    gentity_t *ent=&g_entities[clientNum],*best=NULL;playerState_t *ps=&ent->client->ps;
    float bestDistance=48,speed,yaw,pitch,horizontal;int i,team=ent->client->sess.sessionTeam,range;vec3_t delta;
    // bot_carrierMeleeRange (default 48): the one-defender pilot's body blocks
    // stop the runner 52-62 u from the defender, where its forecast refuses
    // the body (docs/movement/records/2026-09-27-one-defender.md).
    range=trap->Cvar_VariableIntegerValue("bot_carrierMeleeRange");
    if(range>0)bestDistance=(float)min(range,192);
    {   // bot_carrierMelee: unset is the production value, on; 0 off
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_carrierMelee",value,sizeof(value));
        if(value[0] && !atoi(value)) {meleeSince[clientNum]=0;return;}
    }
    if(ps->weapon!=WP_SABER ||
       ps->groundEntityNum==ENTITYNUM_NONE) {meleeSince[clientNum]=0;return;}
    speed=sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]);
    if(speed>=20 || cmd->upmove) {meleeSince[clientNum]=0;return;}
    for(i=0;i<level.maxclients;i++) {
        gentity_t *other=&g_entities[i];float distance;
        if(i==clientNum || !other->inuse || !other->client || other->health<=0 ||
           other->client->pers.connected!=CON_CONNECTED ||
           other->client->sess.sessionTeam==team ||
           (other->client->sess.sessionTeam!=TEAM_RED && other->client->sess.sessionTeam!=TEAM_BLUE))continue;
        distance=Distance(ps->origin,other->r.currentOrigin);
        if(distance<bestDistance) {bestDistance=distance;best=other;}
    }
    if(!best) {meleeSince[clientNum]=0;return;}
    VectorSubtract(best->r.currentOrigin,ps->origin,delta);
    horizontal=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]);
    yaw=atan2f(delta[1],delta[0])*180/M_PI;
    pitch=-atan2f(delta[2],max(1,horizontal))*180/M_PI;
    cmd->angles[YAW]=ANGLE2SHORT(yaw)-ps->delta_angles[YAW];
    cmd->angles[PITCH]=ANGLE2SHORT(pitch)-ps->delta_angles[PITCH];
    // Keep the native floor/hazard gate when turning a stopped runner toward
    // a defender. If the step is unsafe, it can still swing from its footing.
    cmd->forwardmove=TraceFloorScore(ent,ps->origin,yaw,48,NULL)>=0?127:0;
    cmd->rightmove=0;cmd->upmove=0;
    cmd->buttons|=BUTTON_ATTACK;
    if(!meleeSince[clientNum]) {
        meleeSince[clientNum]=level.time;
        Bot2_PrintTelemetry(1,"[MELEE] swing client=%d level=%d target=%d distance=%.1f speed=%.1f wish=%d,%d owner=%s\n",
            clientNum,level.time,best->s.number,bestDistance,speed,cmd->forwardmove,cmd->rightmove,Bot2_MovementOwner(clientNum));
    }
}

// bot_runnerAbsorb (default 0; 1 the pit-exposure rule, 2 the approach rule): raises the
// runner's absorb (FP_ABSORB, level 3 from the loadout, kejim_runner.bot/.jkb)
// when a perceived enemy could throw it into a lethal fall or hurt volume at
// a pit edge (docs/movement/MILESTONE_A.md, step 1). Activation only: the
// policy never presses while absorb is already active, because a second press
// switches it off (ForceAbsorb, w_force.c); it ends on its own after 20 s, at
// Force 0, or when protect is raised. One generic_cmd per frame: a Speed
// purchase the movement already issued this frame (GENCMD_FORCE_SPEED) is
// never clobbered, since the route planner's Speed buy matters more than a
// press that can wait one frame.
//
// Perception. A threat is an enemy (any opposing, connected, live client, not
// only the fixture defenders) the runner can presently see (PVS plus an
// unobstructed trace to its hull centre, as the defenders' own Visible does
// from their side) or saw within RUNNER_ABSORB_MEMORY_MS, and that is within
// RUNNER_ABSORB_RANGE (ForceThrow's own radius, w_force.c). The runner cannot
// read an enemy's Force pool, so it keeps a soft per-threat estimate
// (runnerThreat_t): 50 at the start of a perceived life (100 granted, 50 spent
// on Speed, an assumed starting pool), -20 per perceived throw (the hand extends
// to HANDEXTEND_FORCEPUSH/FORCEPULL while seen; push and pull both cost 20 at
// every level, forcePowerNeeded in bg_pmove.c), regenerating at the server's
// g_forceRegenTime ms per point, capped at 100. A threat is "able" at an
// estimate >= 20. Bias: any doubt raises absorb, so
// exposure only needs one able threat, not proof every threat can throw.
//
// Exposure (cheap, conservative approximation; documented cost below). For
// each able threat, two hypothetical velocities are built with ForceThrow's
// own rule (bot2_defender.c ThrowVelocity): a pull toward the threat and a
// push away from it, both at level 3 (the runner does not know the threat's
// actual level, so it assumes the worst) and without a counter credit (the
// runner's own counter is a second decision, not assumed here). Each is swept
// forward from the runner's current origin under gravity alone, no air-control
// input (also pessimistic: a real recovery option would only ever escape a
// flight this approximation calls lethal, consistent with "any doubt raises
// absorb"), until it lands on a floor, touches a lethal trigger_hurt/lava/
// slime volume or reaches RUNNER_ABSORB_HORIZON_MS. This only checks the
// runner's present state, not every point along a committed flight (the step
// 1 plan's fuller ask); a committed-flight sweep is open follow-up work once
// uptime and refusals are measured. Cost: at most two 80-step trace sweeps per
// able threat, gated to once every RUNNER_ABSORB_EVAL_MS. Threat-model
// bookkeeping (RunnerPerceive, below: one visibility trace per enemy) runs on
// every frame the runner is alive on a team, whatever bot_runnerAbsorb and
// bot_runnerProtect say, whether or not this frame's generic_cmd is already
// claimed and through either policy's press cooldown, so a throw or respawn is
// not missed between evals and protect has last-seen positions with absorb
// off. That trace per enemy is paid with both powers off too, deliberately.
//
// Force pool (bot_runnerAbsorbReserve, default 10): raises only when Force is
// at least the 10-point activation cost plus the reserve, so a Speed purchase
// the route depends on is not starved by an absorb press; a refusal for Force
// logs ABSORB_SKIP.
#define RUNNER_ABSORB_RANGE 1024.0f      // ForceThrow's radius, w_force.c
#define RUNNER_ABSORB_MEMORY_MS 1000     // how long a last sighting still counts as a threat
#define RUNNER_ABSORB_EVAL_MS 150        // exposure re-check cadence
#define RUNNER_ABSORB_HORIZON_MS 2000    // sweep horizon for the exposure forecast
#define RUNNER_ABSORB_SKIP_LOG_MS 2000   // rate limit for ABSORB_SKIP
#define RUNNER_ABSORB_THROW_COST 20      // push/pull cost at every level, forcePowerNeeded
#define RUNNER_ABSORB_ABLE_FORCE 20      // minimum estimate to count a threat as able
#define RUNNER_ABSORB_LIFE_FORCE 50      // assumed Force at the start of a perceived life
#define RUNNER_ABSORB_OFF_FORCE 50       // mode 2: absorb is dropped once Force is over this
#define RUNNER_ABSORB_APPROACH 1500.0f   // mode 2: raised within this of the flag it is going for
#define RUNNER_ABSORB_PRESS_MS 300       // at most one absorb press this often
extern int forcePowerNeeded[NUM_FORCE_POWER_LEVELS][NUM_FORCE_POWERS];   // the power costs, bg_pmove.c
extern qboolean WP_ForcePowerUsable(gentity_t *self, forcePowers_t forcePower);   // w_force.c

typedef struct {
    int estimate;          // soft model of this client's Force, -1 until its first sighting
    int lastUpdate;         // last time estimate accounted for regen
    int lastSeenTime;       // last time this client was actually visible
    vec3_t lastSeenOrigin;  // its r.currentOrigin at lastSeenTime: a perceived position, not a live one
    int lastHandExtend;     // forceHandExtend at the last visible frame, to edge-detect a throw
    qboolean alive;         // perceived: set at a sighting, cleared by a seen death or a disconnect
} runnerThreat_t;
static runnerThreat_t runnerThreat[MAX_CLIENTS][MAX_CLIENTS];   // [runner][enemy]

typedef struct {
    int nextEval;
    qboolean exposed;
    int threats,able;
    int lastSkipLog;
    int lastPress;
} runnerAbsorbState_t;
static runnerAbsorbState_t runnerAbsorb[MAX_CLIENTS];

// Mirrors the defenders' own Visible (bot2_defender.c): PVS, then an
// unobstructed trace to the target's hull centre.
static qboolean RunnerCanSee(gentity_t *self,gentity_t *other) {
    vec3_t eye,center;trace_t tr;
    VectorCopy(self->client->ps.origin,eye);eye[2]+=self->client->ps.viewheight;
    VectorAdd(other->r.absmin,other->r.absmax,center);VectorScale(center,.5f,center);
    if(!trap->InPVS(eye,center))return qfalse;
    trap->Trace(&tr,eye,NULL,NULL,center,self->s.number,MASK_SHOT,qfalse,0,0);
    return tr.fraction>=1 || tr.entityNum==other->s.number;
}

// The log prefix for a Force decision or perception line: the runner's keep
// "[RUNNER] ", a guest using the same perception and protect decision (the
// camper, Bot2_ProtectCommand) prints "[DEFENDER] CAMPER_" so the runner's
// counts stay its own.
static const char *ForceLogTag(int clientNum) {
    return Bot2_IsMovementOnly(clientNum)?"[RUNNER] ":"[DEFENDER] CAMPER_";
}

// One [runner][enemy] slot back to "never seen": no estimate, not alive.
static void RunnerThreatClear(runnerThreat_t *t) {
    memset(t,0,sizeof(*t));t->estimate=-1;t->lastHandExtend=HANDEXTEND_NONE;
}

// Perception/Force-model update for every opposing client, each frame (from
// RunnerPerceive): cheap (one trace per enemy) so a throw's hand-extend edge
// or a respawn is not missed between the costlier exposure evaluations. Death
// comes from the kill feed (bot_runnerKillFeed, default 1:
// what the HUD and console tell a human may be used): alive goes false on any
// frame the enemy is at health <= 0, seen or not, and its last-seen position is
// forgotten (it respawns elsewhere); a new life starts at the first sighting
// after that. With 0, only a death seen counts, as before. A slot
// whose client is no longer a connected opponent (disconnect, spectator, team
// change) is cleared, so whoever takes that slot next starts unseen.
static void RunnerUpdateThreats(int clientNum,gentity_t *ent) {
    int i,team=ent->client->sess.sessionTeam,regen=trap->Cvar_VariableIntegerValue("g_forceRegenTime"),killFeed;
    if(regen<=0)regen=200;
    {char text[16];trap->Cvar_VariableStringBuffer("bot_runnerKillFeed",text,sizeof(text));killFeed=!text[0] || atoi(text)!=0;}
    for(i=0;i<level.maxclients;i++) {
        gentity_t *other=&g_entities[i];runnerThreat_t *t=&runnerThreat[clientNum][i];qboolean visible;
        if(i==clientNum)continue;
        if(!other->inuse || !other->client || other->client->pers.connected!=CON_CONNECTED ||
           (other->client->sess.sessionTeam!=TEAM_RED && other->client->sess.sessionTeam!=TEAM_BLUE) ||
           other->client->sess.sessionTeam==team) {RunnerThreatClear(t);continue;}
        // A dead enemy known from the kill feed needs no sight trace (same branch as before).
        visible=other->health<=0 && killFeed?qfalse:RunnerCanSee(ent,other);
        if(other->health<=0 && (visible || killFeed)) {
            if(t->alive && killFeed)t->lastSeenTime=0;
            t->alive=qfalse;continue;
        }
        if(!t->alive) {
            // First sighting of this life (a fresh connect, or a respawn since
            // it was last seen dead): an assumed pool, 100 granted less
            // 50 spent on Speed. Until it is seen it is not perceived alive.
            if(!visible)continue;
            t->estimate=RUNNER_ABSORB_LIFE_FORCE;t->alive=qtrue;t->lastUpdate=level.time;t->lastHandExtend=HANDEXTEND_NONE;
            trap->Print("%sTHREAT_FORCE level=%d client=%d est=%d event=respawn\n",ForceLogTag(clientNum),level.time,i,t->estimate);
        }
        if(level.time>t->lastUpdate) {
            // Whole points only, and the clock advances by what they cost: a
            // per-frame (level.time-lastUpdate)/regen is 25/200 = 0 every
            // frame, and the estimate never regenerated.
            int gained=(level.time-t->lastUpdate)/regen;
            if(t->estimate>=100)t->lastUpdate=level.time;
            else if(gained>0) {t->estimate=min(100,t->estimate+gained);t->lastUpdate+=gained*regen;}
        }
        if(visible) {
            t->lastSeenTime=level.time;VectorCopy(other->r.currentOrigin,t->lastSeenOrigin);
            if((other->client->ps.forceHandExtend==HANDEXTEND_FORCEPUSH || other->client->ps.forceHandExtend==HANDEXTEND_FORCEPULL) &&
               t->lastHandExtend!=other->client->ps.forceHandExtend) {
                t->estimate=max(0,t->estimate-RUNNER_ABSORB_THROW_COST);
                trap->Print("%sTHREAT_FORCE level=%d client=%d est=%d event=throw\n",ForceLogTag(clientNum),level.time,i,t->estimate);
            }
            t->lastHandExtend=other->client->ps.forceHandExtend;
        }
    }
}

// A lethal hurt volume at `origin` (trigger_hurt, lava or slime). Any active
// trigger_hurt that does damage at all (damage > 0, or -1 for an instant kill)
// counts, whatever its per-tick damage against current health: a thrown
// runner in a low-damage volume with no exit still dies there. Deliberately
// more conservative than ReachableDrop's stand test and the defenders' damage
// floor (bot2_defender.c HurtContact): any doubt raises absorb.
static qboolean RunnerHurtContact(gentity_t *ent,const vec3_t origin) {
    int list[MAX_GENTITIES],n,i;vec3_t mins,maxs;
    VectorAdd(origin,ent->r.mins,mins);VectorAdd(origin,ent->r.maxs,maxs);
    n=trap->EntitiesInBox(mins,maxs,list,MAX_GENTITIES);
    for(i=0;i<n;i++) {
        gentity_t *hit=&g_entities[list[i]];
        if(!hit->inuse || !hit->classname || Q_stricmp(hit->classname,"trigger_hurt") || (hit->flags&FL_INACTIVE))continue;
        if(hit->damage!=-1 && hit->damage<=0)continue;
        if(trap->EntityContact(mins,maxs,(sharedEntity_t*)hit,qfalse))return qtrue;
    }
    return !!(trap->PointContents(origin,ent->s.number)&(CONTENTS_LAVA|CONTENTS_SLIME));
}

// ForceThrow's velocity rule (bot2_defender.c ThrowVelocity), at level 3 and
// without a counter credit: the worst case the runner assumes of a threat it
// cannot see the level or counter state of.
static void RunnerThrowVelocity(const vec3_t threat,const vec3_t self,float selfVz,qboolean pull,vec3_t out) {
    vec3_t dir;float length,mod;
    if(pull)VectorSubtract(threat,self,dir);else VectorSubtract(self,threat,dir);
    length=VectorNormalize(dir);
    mod=256*FORCE_LEVEL_3;
    mod-=length*.7f;if(mod<16)mod=16;
    out[0]=dir[0]*mod;out[1]=dir[1]*mod;out[2]=dir[2]*mod;
    if((int)selfVz==0 && out[2]<128)out[2]=128;
}

// Would the given velocity, applied now, end in a lethal hurt volume before it
// lands or the horizon runs out? Gravity only, no air-control input: cheaper
// than Escapes (bot2_defender.c) and pessimistic, matching the rule that any
// doubt raises absorb.
static qboolean RunnerLethalAfterImpulse(gentity_t *ent,const vec3_t impulse,int horizonMs) {
    vec3_t pos,v,next;trace_t tr;int t;const float dt=.025f;float gravity=ent->client->ps.gravity;
    VectorCopy(ent->client->ps.origin,pos);VectorCopy(impulse,v);
    for(t=25;t<=horizonMs;t+=25) {
        v[2]-=gravity*dt;
        VectorMA(pos,dt,v,next);
        trap->Trace(&tr,pos,ent->r.mins,ent->r.maxs,next,ent->s.number,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.allsolid)return qfalse;
        VectorCopy(tr.endpos,pos);
        if(RunnerHurtContact(ent,pos))return qtrue;
        if(tr.fraction<1) {
            if(tr.plane.normal[2]>=.7f)return qfalse;
            {float d=DotProduct(v,tr.plane.normal);VectorMA(v,-1.001f*d,tr.plane.normal,v);}
        }
    }
    return qfalse;
}

// Exposure: any able, perceived threat in reach whose pull-toward or
// push-away would end lethally. *threats counts every perceived threat in
// reach (able or not), *able the ones the soft Force model allows a throw.
// Reads only the perceived model (RunnerUpdateThreats, this frame): a threat
// counts while its perceived life is alive and it was seen within
// RUNNER_ABSORB_MEMORY_MS, and the throw is aimed from where it was last
// seen (its live origin on a visible frame, since the model copies it
// then). A threat that died unseen stays a threat until its death or
// respawn is seen; team is public and read directly.
static qboolean RunnerExposed(gentity_t *ent,int *threats,int *able) {
    int i,team=ent->client->sess.sessionTeam,n=0,nAble=0;qboolean exposed=qfalse;
    for(i=0;i<level.maxclients;i++) {
        gentity_t *other=&g_entities[i];runnerThreat_t *t=&runnerThreat[ent->s.number][i];float distance;
        if(i==ent->s.number || !other->inuse || !other->client)continue;
        if(other->client->sess.sessionTeam!=TEAM_RED && other->client->sess.sessionTeam!=TEAM_BLUE)continue;
        if(other->client->sess.sessionTeam==team)continue;
        if(!t->alive || level.time-t->lastSeenTime>RUNNER_ABSORB_MEMORY_MS)continue;
        distance=Distance(ent->client->ps.origin,t->lastSeenOrigin);
        if(distance>RUNNER_ABSORB_RANGE)continue;
        n++;
        if(t->estimate<RUNNER_ABSORB_ABLE_FORCE)continue;
        nAble++;
        {
            vec3_t vel;
            RunnerThrowVelocity(t->lastSeenOrigin,ent->client->ps.origin,ent->client->ps.velocity[2],qtrue,vel);
            if(RunnerLethalAfterImpulse(ent,vel,RUNNER_ABSORB_HORIZON_MS))exposed=qtrue;
            RunnerThrowVelocity(t->lastSeenOrigin,ent->client->ps.origin,ent->client->ps.velocity[2],qfalse,vel);
            if(RunnerLethalAfterImpulse(ent,vel,RUNNER_ABSORB_HORIZON_MS))exposed=qtrue;
        }
    }
    *threats=n;*able=nAble;
    return exposed;
}

// Raises absorb (GENCMD_FORCE_ABSORB) when exposed, not already active and
// Force allows it. Presses nothing when the movement already claimed this
// frame's one generic_cmd for a Speed purchase; perception is not its job
// (RunnerPerceive runs first, every frame).
static void RunnerAbsorbPolicy(int clientNum,gentity_t *ent,usercmd_t *cmd) {
    playerState_t *ps=&ent->client->ps;runnerAbsorbState_t *st=&runnerAbsorb[clientNum];int reserve,need,mode;
    {   // default 0: protect alone does better; absorb holds
        // the slot the two powers share (records/2026-10-02-runner-protect-v1.md)
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_runnerAbsorb",value,sizeof(value));
        mode=value[0]?atoi(value):0;
    }
    if(mode!=1)st->exposed=0;   // the exposure test is mode 1's; a stale one would hold protect back
    if(!mode || cmd->generic_cmd)return;
    // One press per RUNNER_ABSORB_PRESS_MS: a press that cannot raise absorb
    // (at the bottom of a pit, say) was repeated every frame.
    if(level.time-st->lastPress<RUNNER_ABSORB_PRESS_MS)return;
    // bot_runnerAbsorb 2: raise absorb on the approach,
    // within RUNNER_ABSORB_APPROACH of the flag it is going for, so it is up
    // when the carrier is pulled (95% of pulls land on the carrier,
    // absorb-20261002-a) - it lasts 20 s against a median 2.8 s carry - and
    // drop it once Force is over 50, the second Speed's price. Force does not
    // regenerate while any power is up, so past 50 comes mostly from absorbed
    // throws (a refund of 20 a pull).
    if(mode==2) {
        const gentity_t *goal=bot2_states[clientNum].targetEntNum>=0?&g_entities[bot2_states[clientNum].targetEntNum]:NULL;
        int carrying=ps->powerups[ent->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG]!=0;
        if(ps->fd.forcePowersActive&(1<<FP_ABSORB)) {
            if(ps->fd.forcePower>RUNNER_ABSORB_OFF_FORCE && ps->forceAllowDeactivateTime<level.time) {
                cmd->generic_cmd=GENCMD_FORCE_ABSORB;st->lastPress=level.time;
                trap->Print("[RUNNER] ABSORB_OFF level=%d client=%d force=%d carrying=%d\n",level.time,clientNum,ps->fd.forcePower,carrying);
            }
            return;
        }
        if(carrying || !goal || !goal->inuse || Distance(ps->origin,goal->r.currentOrigin)>RUNNER_ABSORB_APPROACH ||
           ps->fd.forcePower<forcePowerNeeded[ps->fd.forcePowerLevel[FP_ABSORB]][FP_ABSORB])return;
        cmd->generic_cmd=GENCMD_FORCE_ABSORB;st->lastPress=level.time;
        trap->Print("[RUNNER] ABSORB level=%d client=%d reason=approach distance=%.0f force=%d\n",level.time,clientNum,
            Distance(ps->origin,goal->r.currentOrigin),ps->fd.forcePower);
        return;
    }
    if(level.time>=st->nextEval) {
        st->exposed=RunnerExposed(ent,&st->threats,&st->able);
        st->nextEval=level.time+RUNNER_ABSORB_EVAL_MS;
    }
    if(!st->exposed)return;
    if(ps->fd.forcePowersActive&(1<<FP_ABSORB)) {
        if(level.time-st->lastSkipLog>=RUNNER_ABSORB_SKIP_LOG_MS) {
            st->lastSkipLog=level.time;
            trap->Print("[RUNNER] ABSORB_SKIP level=%d client=%d reason=active threats=%d able=%d exposed=1 force=%d\n",
                level.time,clientNum,st->threats,st->able,ps->fd.forcePower);
        }
        return;
    }
    {   // unset is the documented default of 10, not 0
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_runnerAbsorbReserve",value,sizeof(value));
        reserve=value[0]?atoi(value):10;if(reserve<0)reserve=0;
    }
    need=forcePowerNeeded[ps->fd.forcePowerLevel[FP_ABSORB]][FP_ABSORB]+reserve;
    if(ps->fd.forcePower<need) {
        if(level.time-st->lastSkipLog>=RUNNER_ABSORB_SKIP_LOG_MS) {
            st->lastSkipLog=level.time;
            trap->Print("[RUNNER] ABSORB_SKIP level=%d client=%d reason=force threats=%d able=%d exposed=1 force=%d need=%d\n",
                level.time,clientNum,st->threats,st->able,ps->fd.forcePower,need);
        }
        return;
    }
    // Speed first (laps, absorb-20261002-a: absorb up 34% of the time took
    // Speed while carrying from 95% to 70% and captures from 4.50 to 1.38).
    // Projected to when Speed runs out - absorb's activation and its drain
    // over that time; nothing regenerates while a power is up (w_force.c) -
    // Force must still cover the next Speed purchase; with Speed down it must
    // cover one now.
    if(ps->fd.forcePowerLevel[FP_SPEED]>0) {
        int left=0,projected,cost;
        cost=forcePowerNeeded[ps->fd.forcePowerLevel[FP_SPEED]][FP_SPEED];
        if((ps->fd.forcePowersActive&(1<<FP_SPEED)) && ps->fd.forcePowerDuration[FP_SPEED]>level.time)
            left=ps->fd.forcePowerDuration[FP_SPEED]-level.time;
        projected=ps->fd.forcePower-forcePowerNeeded[ps->fd.forcePowerLevel[FP_ABSORB]][FP_ABSORB]-left/600;
        if(projected<cost) {
            if(level.time-st->lastSkipLog>=RUNNER_ABSORB_SKIP_LOG_MS) {
                st->lastSkipLog=level.time;
                trap->Print("[RUNNER] ABSORB_SKIP level=%d client=%d reason=speed threats=%d able=%d exposed=1 force=%d projected=%d speed_cost=%d speed_left=%d\n",
                    level.time,clientNum,st->threats,st->able,ps->fd.forcePower,projected,cost,left);
            }
            return;
        }
    }
    cmd->generic_cmd=GENCMD_FORCE_ABSORB;st->lastPress=level.time;
    trap->Print("[RUNNER] ABSORB level=%d client=%d reason=pit_edge threats=%d able=%d exposed=1 force=%d\n",
        level.time,clientNum,st->threats,st->able,ps->fd.forcePower);
}

// bot_runnerProtect (default 1; Milestone A step 2): raises
// the runner's protect (FP_PROTECT, level 3 from the loadout) when it is under
// perceived fire and absorb's own exposure test (RunnerExposed, above) is not
// currently exposed - absorb always wins a pit, since a thrown runner it
// absorbs is unharmed but a protected one that gets pulled into a pit still
// falls. Activation only, the same discipline as absorb: never press while
// protect is already active (a second press switches it off, ForceProtect,
// w_force.c), at most one press every RUNNER_PROTECT_PRESS_MS, and never when
// this frame's one generic_cmd is already claimed (a Speed purchase or an
// absorb press issued first keeps priority; see RunnerAbsorbPolicy above).
// It ends on its own after 20 s, at Force 0, or when absorb is raised.
//
// Under fire (any one is enough):
//  - an incoming missile: an ET_MISSILE owned by an opposing client, within
//    RUNNER_PROTECT_MISSILE_RANGE, closing on the runner, that the runner can
//    presently see (RunnerCanSee, above; bot2_threat.c only hides young
//    missiles from movement searches and does not itself classify them as
//    threats to a client, so this is a small parallel check, not a reuse);
//  - recent damage: the runner's own health dropped in the last
//    RUNNER_PROTECT_DAMAGE_MS (RunnerUpdateDamage tracks this directly - there
//    is no ready-made hurt timestamp on gclient_t/playerState_t, only
//    lasthurt_client/lasthurt_mod, which name the attacker but not when) by a
//    ranged means of death (RunnerRangedMod: a gun or an explosive, read from
//    lasthurt_mod on the frame of the drop - the hit is what the runner
//    perceives, not the attacker's weapon now) from a connected opposing
//    client. If the runner saw that attacker within RUNNER_ABSORB_MEMORY_MS
//    (the threat model's own memory window, shared on purpose), where it last
//    saw it must be within RUNNER_PROTECT_DAMAGE_RANGE; if not, there is no
//    perceived position to gate on and the hit counts anyway (any doubt
//    raises);
//  - a seen trip mine (classname "laserTrap", the proximity and tripwire mines
//    both use it; bot2_defender.c LayMines/MineCovers) owned by an opposing
//    client, within its splash radius (LT_SPLASH_RAD, g_weapon.c) of the
//    runner, that the runner had line of sight to at some point while it was
//    in range (RunnerMineMark remembers this per mine, keyed on the entity
//    number plus the occupant's identity so a reused number starts unseen; a
//    mine behind the runner now still counts if it was seen approaching).
//
// Force pool: raises only at forcePowerNeeded[level][FP_PROTECT] (10 at level
// 3) or more, same as absorb's own activation cost, no extra reserve (the
// plan's Force budgeting is absorb's job; protect is the cheaper power and the
// rule here is simply "can it afford the press").
#define RUNNER_PROTECT_MISSILE_RANGE 600.0f   // fallback distance for a closing missile
#define RUNNER_PROTECT_DAMAGE_MS 300          // "recent" damage window
#define RUNNER_PROTECT_DAMAGE_RANGE 1200.0f   // a ranged shooter counts this close after a recent hit
#define RUNNER_PROTECT_MINE_RADIUS 256.0f     // LT_SPLASH_RAD, g_weapon.c
#define RUNNER_PROTECT_PRESS_MS 300           // at most one protect press this often
#define RUNNER_PROTECT_EVAL_MS 150            // under-fire re-check cadence
#define RUNNER_PROTECT_SKIP_LOG_MS 2000       // rate limit for PROTECT_SKIP
#define RUNNER_PROTECT_MINE_SLOTS 16          // tracked mine entities per runner, a small ring

typedef struct {
    int lastDamageTime;     // level.time of the last frame this client's health dropped
    int lastDamageAttacker; // ent->client->lasthurt_client at that frame
    int lastDamageMod;      // ent->client->lasthurt_mod at that frame (meansOfDeath_t)
    int lastHealth;         // health as of the previous frame, -1 until first seen (Bot2_RunnerForceReset)
} runnerDamage_t;
static runnerDamage_t runnerDamage[MAX_CLIENTS];

// One remembered mine: entity number plus its occupant's identity (see
// RunnerMineMark); entNum -1 is an empty slot.
typedef struct { int entNum,ownerNum,freetime; qboolean seen; } runnerMineSeen_t;
static runnerMineSeen_t runnerMineSeen[MAX_CLIENTS][RUNNER_PROTECT_MINE_SLOTS];
static int runnerMineSeenNext[MAX_CLIENTS];

typedef struct {
    int nextEval;
    qboolean underFire;
    int reason;         // 0 none, 1 missile, 2 damage, 3 mine (the reason logged)
    int lastSkipLog;
    int lastPress;
} runnerProtectState_t;
static runnerProtectState_t runnerProtect[MAX_CLIENTS];

enum {PROTECT_REASON_NONE,PROTECT_REASON_MISSILE,PROTECT_REASON_DAMAGE,PROTECT_REASON_MINE};
static const char *protectReasonNames[]={"none","missile","damage","mine"};

// A ranged means of death (meansOfDeath_t, bg_public.h): a gun, a launcher,
// a sentry or an explosive (thrown, planted or splash). Not fire: saber,
// melee, stun baton, Force lightning, vehicles, world/trigger hazards, falls.
static qboolean RunnerRangedMod(int mod) {
    switch(mod) {
    case MOD_BRYAR_PISTOL:case MOD_BRYAR_PISTOL_ALT:case MOD_BLASTER:case MOD_TURBLAST:
    case MOD_DISRUPTOR:case MOD_DISRUPTOR_SPLASH:case MOD_DISRUPTOR_SNIPER:case MOD_BOWCASTER:
    case MOD_REPEATER:case MOD_REPEATER_ALT:case MOD_REPEATER_ALT_SPLASH:case MOD_DEMP2:case MOD_DEMP2_ALT:
    case MOD_FLECHETTE:case MOD_FLECHETTE_ALT_SPLASH:case MOD_ROCKET:case MOD_ROCKET_SPLASH:
    case MOD_ROCKET_HOMING:case MOD_ROCKET_HOMING_SPLASH:case MOD_THERMAL:case MOD_THERMAL_SPLASH:
    case MOD_TRIP_MINE_SPLASH:case MOD_TIMED_MINE_SPLASH:case MOD_DET_PACK_SPLASH:
    case MOD_CONC:case MOD_CONC_ALT:case MOD_SENTRY:
        return qtrue;
    default:
        return qfalse;
    }
}

// Missiles and mines are seen with RunnerCanSee (above), not a trace to a bare
// point: a trip mine (CreateLaserTrap, g_weapon.c), a detpack and a rocket are
// r.contents MASK_SHOT, so a MASK_SHOT trace to their origin stops on their own
// hull and only the tr.entityNum test accepts them (a point trace requiring
// fraction 1 never saw a mine: 0 mine presses in 810 campaigns).
// RunnerCanSee aims at the absmin/absmax centre, which for these symmetric
// hulls is r.currentOrigin, passing the runner itself; contents-0 bolts still
// pass on fraction 1.

// Tracks the runner's own health frame to frame; a drop is damage taken,
// whoever or whatever it came from. There is no per-hit timestamp kept
// anywhere upstream (lasthurt_client/lasthurt_mod name the attacker, not
// when; G_Damage's damage_blood/damage_armor accumulators are cleared by
// P_DamageFeedback in ClientEndFrame at the end of G_RunFrame, so a hit dealt
// inside the frame (missiles, splash, bots' guns) is already gone when the bot
// thinks, while a human's hitscan between frames is not - they cannot be read
// consistently from here), so health is the signal. Not damage, and skipped:
//  - overheal decay: ClientTimerActions (g_active.c) takes exactly 1 health a
//    second while health is above STAT_MAX_HEALTH and leaves lasthurt_* stale,
//    so a 1-point drop from above max is the timer, not a hit (it would re-blame
//    the last attacker every second). A real hit while overhealed drops more
//    than 1 (with a coincident tick, its damage plus 1) and still counts; only
//    a hit taking exactly 1 health from above max is lost, which no ranged hit
//    this policy cares about does short of a splash edge;
//  - a drop to health <= 0 (a death or self-kill frame): nothing to protect.
// Armor loss is not seen, but shields absorb only half (ARMOR_PROTECTION), so a
// ranged hit still drops health.
static void RunnerUpdateDamage(int clientNum,gentity_t *ent) {
    runnerDamage_t *d=&runnerDamage[clientNum];
    qboolean decay=d->lastHealth>ent->client->ps.stats[STAT_MAX_HEALTH] && d->lastHealth-ent->health==1;
    if(d->lastHealth>=0 && ent->health<d->lastHealth && ent->health>0 && !decay) {
        d->lastDamageTime=level.time;
        d->lastDamageAttacker=ent->client->lasthurt_client;
        d->lastDamageMod=ent->client->lasthurt_mod;
    }
    d->lastHealth=ent->health;
}

// A visible ET_MISSILE owned by an opposing client, within
// RUNNER_PROTECT_MISSILE_RANGE and heading within 60 degrees of the runner
// (not a miss-distance test: a near miss counts).
static qboolean RunnerIncomingMissile(gentity_t *ent) {
    int i,team=ent->client->sess.sessionTeam;
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        gentity_t *m=&g_entities[i];vec3_t toRunner;float distance,closing,speed;
        gentity_t *owner;
        if(!m->inuse || m->s.eType!=ET_MISSILE || m->r.ownerNum<0 || m->r.ownerNum>=MAX_CLIENTS)continue;
        owner=&g_entities[m->r.ownerNum];
        if(!owner->client || owner->client->sess.sessionTeam==team ||
           (owner->client->sess.sessionTeam!=TEAM_RED && owner->client->sess.sessionTeam!=TEAM_BLUE))continue;
        VectorSubtract(ent->client->ps.origin,m->r.currentOrigin,toRunner);
        distance=VectorNormalize(toRunner);
        if(distance>RUNNER_PROTECT_MISSILE_RANGE)continue;
        speed=VectorLength(m->s.pos.trDelta);
        if(speed<=0)continue;
        closing=DotProduct(m->s.pos.trDelta,toRunner)/speed;
        if(closing<=0.5f)continue;   // moving away or mostly across, not toward
        if(!RunnerCanSee(ent,m))continue;
        return qtrue;
    }
    return qfalse;
}

// Recent ranged damage from an opposing client (see RunnerUpdateDamage for
// why health deltas are the signal). Reads the hit as perceived - its means
// of death, the attacker's (public) team and where the runner last saw the
// attacker - never the attacker's current weapon, health or origin. A
// shooter not seen within RUNNER_ABSORB_MEMORY_MS has no perceived position,
// so its hit counts without the distance test (any doubt raises).
static qboolean RunnerRecentDamage(gentity_t *ent) {
    runnerDamage_t *d=&runnerDamage[ent->s.number];gentity_t *attacker;runnerThreat_t *t;
    if(level.time-d->lastDamageTime>RUNNER_PROTECT_DAMAGE_MS)return qfalse;
    if(!RunnerRangedMod(d->lastDamageMod))return qfalse;
    if(d->lastDamageAttacker<0 || d->lastDamageAttacker>=MAX_CLIENTS || d->lastDamageAttacker==ent->s.number)return qfalse;
    attacker=&g_entities[d->lastDamageAttacker];
    if(!attacker->inuse || !attacker->client || attacker->client->pers.connected!=CON_CONNECTED)return qfalse;
    if(attacker->client->sess.sessionTeam==ent->client->sess.sessionTeam)return qfalse;
    t=&runnerThreat[ent->s.number][d->lastDamageAttacker];
    if(t->lastSeenTime<=0 || level.time-t->lastSeenTime>RUNNER_ABSORB_MEMORY_MS)return qtrue;
    return Distance(ent->client->ps.origin,t->lastSeenOrigin)<=RUNNER_PROTECT_DAMAGE_RANGE;
}

// Remembers whether the runner has ever had line of sight to this mine while
// scanning it; `visible` marks it seen now, the return is "ever seen". A slot
// is one mine, not one entity number: it matches the number plus the
// occupant's ownerNum and freetime (G_FreeEntity stamps freetime when the
// previous occupant is freed, so it is fixed for a mine's whole life and
// differs for the next one; s.pos.trTime is not fixed, a thrown mine restamps
// it when it sticks). RunnerMineThreat also forgets every slot whose entity
// is no longer that in-use laserTrap, so a freed mine's "seen" never passes to
// whatever reuses its number.
static qboolean RunnerMineMark(int clientNum,gentity_t *m,qboolean visible) {
    runnerMineSeen_t *slots=runnerMineSeen[clientNum];int i;
    for(i=0;i<RUNNER_PROTECT_MINE_SLOTS;i++) {
        if(slots[i].entNum==m->s.number && slots[i].ownerNum==m->r.ownerNum && slots[i].freetime==m->freetime) {
            if(visible)slots[i].seen=qtrue;
            return slots[i].seen;
        }
    }
    i=runnerMineSeenNext[clientNum];runnerMineSeenNext[clientNum]=(i+1)%RUNNER_PROTECT_MINE_SLOTS;
    slots[i].entNum=m->s.number;slots[i].ownerNum=m->r.ownerNum;slots[i].freetime=m->freetime;slots[i].seen=visible;
    return visible;
}

static qboolean RunnerMineSeenBefore(int clientNum,const gentity_t *m) {
    const runnerMineSeen_t *slots=runnerMineSeen[clientNum];int i;
    for(i=0;i<RUNNER_PROTECT_MINE_SLOTS;i++)
        if(slots[i].entNum==m->s.number && slots[i].ownerNum==m->r.ownerNum && slots[i].freetime==m->freetime)
            return slots[i].seen;
    return qfalse;
}

// A seen enemy trip mine within splash reach of the runner right now.
static qboolean RunnerMineThreat(gentity_t *ent) {
    gentity_t *m=NULL;int team=ent->client->sess.sessionTeam,i;qboolean found=qfalse;
    runnerMineSeen_t *slots=runnerMineSeen[ent->s.number];
    for(i=0;i<RUNNER_PROTECT_MINE_SLOTS;i++) {   // forget mines that are gone (RunnerMineMark)
        gentity_t *s;
        if(slots[i].entNum<MAX_CLIENTS || slots[i].entNum>=MAX_GENTITIES)continue;
        s=&g_entities[slots[i].entNum];
        if(!s->inuse || !s->classname || Q_stricmp(s->classname,"laserTrap") ||
           s->r.ownerNum!=slots[i].ownerNum || s->freetime!=slots[i].freetime) {slots[i].entNum=-1;slots[i].seen=qfalse;}
    }
    while((m=G_Find(m,FOFS(classname),"laserTrap"))!=NULL) {
        float distance;qboolean visible,seen;
        if(!m->inuse || m->r.ownerNum<0 || m->r.ownerNum>=MAX_CLIENTS)continue;
        if(g_entities[m->r.ownerNum].client && g_entities[m->r.ownerNum].client->sess.sessionTeam==team)continue;
        distance=Distance(ent->client->ps.origin,m->r.currentOrigin);
        visible=distance<=RUNNER_ABSORB_RANGE && RunnerCanSee(ent,m);
        // An unseen mine with no slot has nothing to remember; marking every
        // mine on the map let distant ones evict seen ones past 16.
        seen=visible?RunnerMineMark(ent->s.number,m,qtrue):RunnerMineSeenBefore(ent->s.number,m);
        if(seen && distance<=RUNNER_PROTECT_MINE_RADIUS)found=qtrue;
    }
    return found;
}

// Raises protect (GENCMD_FORCE_PROTECT) when under fire, absorb is neither
// active nor exposed, and Force allows it. Presses nothing when the frame's
// one generic_cmd is already claimed; its damage and threat inputs come from
// RunnerPerceive, which runs first every frame.
// The decision itself, for any client whose perception (RunnerPerceive) runs
// first: the runner here, the camper through Bot2_ProtectCommand. Every input
// is that client's own row of the per-client state.
static void ProtectDecide(int clientNum,gentity_t *ent,usercmd_t *cmd,int mode) {
    playerState_t *ps=&ent->client->ps;runnerProtectState_t *st=&runnerProtect[clientNum];
    runnerAbsorbState_t *absorbSt=&runnerAbsorb[clientNum];int need,fix;const char *tag=ForceLogTag(clientNum);
    if(!mode || cmd->generic_cmd)return;
    // One press per RUNNER_PROTECT_PRESS_MS: see RunnerAbsorbPolicy's own note.
    if(level.time-st->lastPress<RUNNER_PROTECT_PRESS_MS)return;
    if(level.time>=st->nextEval) {
        if(RunnerIncomingMissile(ent))st->reason=PROTECT_REASON_MISSILE;
        else if(RunnerRecentDamage(ent))st->reason=PROTECT_REASON_DAMAGE;
        else if(RunnerMineThreat(ent))st->reason=PROTECT_REASON_MINE;
        else st->reason=PROTECT_REASON_NONE;
        st->underFire=st->reason!=PROTECT_REASON_NONE;
        st->nextEval=level.time+RUNNER_PROTECT_EVAL_MS;
    }
    if(!st->underFire)return;
    // bot_protectFixBA11: a client without protect neither presses nor logs a skip.
    fix=FixOn("bot_protectFixBA11");
    if(fix && !(ps->fd.forcePowersKnown&(1<<FP_PROTECT)))return;
    if(ps->fd.forcePowersActive&(1<<FP_PROTECT))return;   // already up; a press now would turn it off
    if(ps->fd.forcePowersActive&(1<<FP_ABSORB)) {
        if(level.time-st->lastSkipLog>=RUNNER_PROTECT_SKIP_LOG_MS) {
            st->lastSkipLog=level.time;
            trap->Print("%sPROTECT_SKIP level=%d client=%d reason=absorb force=%d health=%d\n",
                tag,level.time,clientNum,ps->fd.forcePower,ent->health);
        }
        return;
    }
    // Absorb wins a genuine pit exposure even if bot_runnerAbsorb would not
    // itself press this frame (Force, press-rate, mode 2's own rules): the
    // shared exposure test (RunnerExposed, RunnerAbsorbPolicy above) is only
    // evaluated there, so this reads its last cached result. Only absorb
    // mode 1 keeps it (the default is 0); with absorb off or on mode 2 it is
    // clear, so protect is free to raise on its own perceived fire.
    if(absorbSt->exposed) {
        if(level.time-st->lastSkipLog>=RUNNER_PROTECT_SKIP_LOG_MS) {
            st->lastSkipLog=level.time;
            trap->Print("%sPROTECT_SKIP level=%d client=%d reason=absorb force=%d health=%d\n",
                tag,level.time,clientNum,ps->fd.forcePower,ent->health);
        }
        return;
    }
    need=forcePowerNeeded[ps->fd.forcePowerLevel[FP_PROTECT]][FP_PROTECT];
    if(ps->fd.forcePower<need) {
        if(level.time-st->lastSkipLog>=RUNNER_PROTECT_SKIP_LOG_MS) {
            st->lastSkipLog=level.time;
            trap->Print("%sPROTECT_SKIP level=%d client=%d reason=force force=%d health=%d need=%d\n",
                tag,level.time,clientNum,ps->fd.forcePower,ent->health,need);
        }
        return;
    }
    // a press the game would refuse now (hand extended, debounce) keeps the slot and logs a skip.
    if(fix && !WP_ForcePowerUsable(ent,FP_PROTECT)) {
        if(level.time-st->lastSkipLog>=RUNNER_PROTECT_SKIP_LOG_MS) {
            st->lastSkipLog=level.time;
            trap->Print("%sPROTECT_SKIP level=%d client=%d reason=unusable force=%d health=%d\n",
                tag,level.time,clientNum,ps->fd.forcePower,ent->health);
        }
        return;
    }
    cmd->generic_cmd=GENCMD_FORCE_PROTECT;st->lastPress=level.time;
    trap->Print("%sPROTECT level=%d client=%d reason=%s force=%d health=%d\n",
        tag,level.time,clientNum,protectReasonNames[st->reason],ps->fd.forcePower,ent->health);
}

// The runner's one per-frame perception step (threat model and its own
// damage), called once from Bot2_MovementOnlyCommand on every frame the runner
// is alive on a team, before and independent of both power policies: their
// modes, a claimed generic_cmd and their press cooldowns gate presses only,
// never what the runner perceives.
static void RunnerPerceive(int clientNum,gentity_t *ent) {
    RunnerUpdateThreats(clientNum,ent);
    RunnerUpdateDamage(clientNum,ent);
}

static void RunnerProtectPolicy(int clientNum,gentity_t *ent,usercmd_t *cmd) {
    char value[16];int mode;
    // default 1: captures 4.31 -> 6.44 and weapon deaths
    // 33 -> 21 a world with absorb off (records/2026-10-02-runner-protect-v1.md)
    trap->Cvar_VariableStringBuffer("bot_runnerProtect",value,sizeof(value));
    mode=value[0]?atoi(value):1;
    ProtectDecide(clientNum,ent,cmd,mode);
}

// The runner's protect decision for another live client on a team (the
// camper, bot_camperProtect, bot2_defender.c; records/2026-10-02-camper-force.md):
// its own perception first (threat model, own damage, seen mines), then the
// same reactive press, after whatever the caller already put in the frame's
// one generic_cmd (a Speed purchase, a throw). Every piece of state is that
// client's own row, cleared by Bot2_RunnerForceReset when it dies. Its lines
// print as [DEFENDER] CAMPER_PROTECT / CAMPER_PROTECT_SKIP / CAMPER_THREAT_FORCE.
void Bot2_ProtectCommand(int clientNum,usercmd_t *cmd) {
    gentity_t *ent=&g_entities[clientNum];int team;
    if(clientNum<0 || clientNum>=MAX_CLIENTS || !ent->client || ent->health<=0)return;
    team=ent->client->sess.sessionTeam;
    if(team!=TEAM_RED && team!=TEAM_BLUE)return;
    RunnerPerceive(clientNum,ent);
    ProtectDecide(clientNum,ent,cmd,1);
}

// Clears this client's absorb/protect state, its damage tracker (lastHealth
// -1), its mine memory and its row of the threat model (every enemy unseen,
// estimate -1). Called from Bot2_ClearState, so it runs at bot setup, map
// load, fixture resets and on every frame the runner is dead: a runner that
// dies forgets what it believed of each enemy's Force and re-estimates each
// at RUNNER_ABSORB_LIFE_FORCE on its next sighting. Other runners' slots for
// this client are left alone (clearing them would hand those runners
// knowledge of this client's reset); RunnerUpdateThreats clears them when
// this client stops being a connected opponent.
void Bot2_RunnerForceReset(int clientNum) {
    int i;
    if(clientNum<0 || clientNum>=MAX_CLIENTS)return;
    for(i=0;i<MAX_CLIENTS;i++)RunnerThreatClear(&runnerThreat[clientNum][i]);
    memset(&runnerAbsorb[clientNum],0,sizeof(runnerAbsorb[clientNum]));
    memset(&runnerProtect[clientNum],0,sizeof(runnerProtect[clientNum]));
    memset(&runnerDamage[clientNum],0,sizeof(runnerDamage[clientNum]));
    runnerDamage[clientNum].lastDamageAttacker=-1;runnerDamage[clientNum].lastHealth=-1;
    for(i=0;i<RUNNER_PROTECT_MINE_SLOTS;i++) {memset(&runnerMineSeen[clientNum][i],0,sizeof(runnerMineSeen_t));runnerMineSeen[clientNum][i].entNum=-1;}
    runnerMineSeenNext[clientNum]=0;
}

void G_Kill(gentity_t *ent);   // g_cmds.c: the /kill self-kill (MOD_SUICIDE)

// Declared goal chain (bot_goalChain "x y z;x y z;..."), a movement test
// fixture: the runner's objective is each point in turn, advancing when it
// stands within reach of it, and the native CTF objective once the chain is
// done. A point on an item is that item's pickup; the chain restarts at the
// fixture's release (bot_corner_start). Empty, the policy is unchanged.
#define GOAL_CHAIN_MAX 16
#define GOAL_CHAIN_REACH 40.0f
#define GOAL_CHAIN_REACH_Z 72.0f
static struct {char text[512];int count;vec3_t point[GOAL_CHAIN_MAX];} goalChain;
static int goalChainIndex[MAX_CLIENTS];

static int GoalChainCount(void) {
    char text[512],*p;int n=0;
    trap->Cvar_VariableStringBuffer("bot_goalChain",text,sizeof(text));
    if(strcmp(text,goalChain.text)) {
        Q_strncpyz(goalChain.text,text,sizeof(goalChain.text));
        for(p=text;*p && n<GOAL_CHAIN_MAX;) {
            vec3_t v;
            if(sscanf(p,"%f %f %f",&v[0],&v[1],&v[2])==3) {VectorCopy(v,goalChain.point[n]);n++;}
            p=strchr(p,';');if(!p)break;p++;
        }
        goalChain.count=n;
        memset(goalChainIndex,0,sizeof(goalChainIndex));
    }
    return goalChain.count;
}

void Bot2_GoalChainReset(int clientNum) {
    if(clientNum<0 || clientNum>=MAX_CLIENTS || !GoalChainCount())return;
    goalChainIndex[clientNum]=0;
    trap->Print("[GOALCHAIN] client=%d level=%d event=start points=%d\n",clientNum,level.time,goalChain.count);
}

// The chain's current point, or NULL when there is none or it is done.
static const float *GoalChainPoint(int clientNum,const playerState_t *ps) {
    int count=GoalChainCount();
    while(goalChainIndex[clientNum]<count) {
        const float *p=goalChain.point[goalChainIndex[clientNum]];
        float dx=ps->origin[0]-p[0],dy=ps->origin[1]-p[1];
        if(dx*dx+dy*dy>GOAL_CHAIN_REACH*GOAL_CHAIN_REACH || fabsf(ps->origin[2]-p[2])>GOAL_CHAIN_REACH_Z)return p;
        goalChainIndex[clientNum]++;
        trap->Print("[GOALCHAIN] client=%d level=%d event=reached index=%d of=%d x=%.1f y=%.1f z=%.1f speed=%.1f\n",
            clientNum,level.time,goalChainIndex[clientNum]-1,count,ps->origin[0],ps->origin[1],ps->origin[2],
            sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]));
    }
    return NULL;
}

// A cvar's value, or `fallback` when it is unset.
static float RunnerCvar(const char *name,float fallback) {
    char text[32];
    trap->Cvar_VariableStringBuffer(name,text,sizeof(text));
    return text[0]?(float)atof(text):fallback;
}

// Self-kill requests (Bot2_RequestSelfKill): who asked, and when.
static struct {int requester,time;} skRequest[MAX_CLIENTS];
void Bot2_RequestSelfKill(int carrier,int requester) {
    if(carrier<0 || carrier>=MAX_CLIENTS)return;
    skRequest[carrier].requester=requester;skRequest[carrier].time=level.time;
}
int Bot2_SelfKillRequested(int carrier) {
    if(carrier<0 || carrier>=MAX_CLIENTS || !skRequest[carrier].time || level.time-skRequest[carrier].time>1000)return -1;
    return skRequest[carrier].requester;
}

// bot_carrierSkHomeMs (default 5000; 0 off): a carrier declines a self-kill request while a
// capture is possible (its own flag on its stand) and its route home is shorter than this many ms: the requester's
// rule (RunnerCamp) reads only health and Force, which late in any carry are low, so a carrier a few seconds from a
// capture killed itself and the flag went back for a full carry. In a standoff (own flag away) the request stands,
// as designed. The route is the shared graph's cost (as RouteMs in bot2_defender.c), else 250 u/s; checked at most
// every 500 ms a carrier. SK_DECLINE logs a decline, at most every 2 s a carrier (as SK_REQUEST).
static struct {int time,declined,routeMs,logged;} skHonour[MAX_CLIENTS];
qboolean Bot2_SelfKillHonour(gentity_t *carrier,const char *role) {
    int n=carrier->s.number,team=carrier->client->sess.sessionTeam;
    int guard=(int)RunnerCvar("bot_carrierSkHomeMs",5000);gentity_t *own;
    if(guard<=0 || n<0 || n>=MAX_CLIENTS)return qtrue;
    if(!skHonour[n].time || level.time<skHonour[n].time || level.time-skHonour[n].time>=500) {
        own=Bot2_BaseFlag(team==TEAM_RED?TEAM_RED:TEAM_BLUE);
        skHonour[n].time=level.time;skHonour[n].declined=0;skHonour[n].routeMs=-1;
        if(own && !(own->s.eFlags&EF_NODRAW)) {   // the own flag is home: reaching the stand captures
            static int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
            const float *stand=VectorLength(own->s.origin)>0?own->s.origin:own->r.currentOrigin;
            int a=Graph_Nearest(carrier->client->ps.origin,96,96),b=Graph_Nearest(stand,96,96),cost;
            skHonour[n].routeMs=a>=0 && b>=0 && Graph_PathTable(a,b,cells,edges,GRAPH_MAX_PATH,&cost)>0?cost:
                (int)(Distance(carrier->client->ps.origin,stand)/250.0f*1000.0f);
            skHonour[n].declined=skHonour[n].routeMs<guard;
        }
    }
    if(skHonour[n].declined && (level.time>=skHonour[n].logged || skHonour[n].logged-level.time>2000)) {
        skHonour[n].logged=level.time+2000;
        trap->Print("[RUNNER] SK_DECLINE level=%d client=%d role=%s requester=%d route_ms=%d guard_ms=%d health=%d force=%d\n",
            level.time,n,role,Bot2_SelfKillRequested(n),skHonour[n].routeMs,guard,carrier->health,carrier->client->ps.fd.forcePower);
    }
    return !skHonour[n].declined;
}

// bot_runnerCamp (default 1): a runner whose teammate carries
// the enemy flag "should go to enemy base, camp around the flagstand, and if the
// enemy carrier comes around it should try to kill them OR if their FC is low
// health it should stay ready to re-grab in case the enemy returns or even ask
// for sk to start their own run if like force etc is in a good shape". Goals:
//   fc    the enemy carrying its own flag, perceived (seen, or within
//         bot_defenderHearRange) within bot_runnerCampLeash (default 1,000) of
//         the enemy stand: run at it; the carrier melee swings once it is near;
//   camp  else a spot 200-500 u from the enemy stand nearest where it is
//         (Bot2_CampSpot), standing there: a teammate carrier's death or /kill
//         returns the flag to that stand, and the runner's fetch takes it.
// And while camping within 600 u of the stand with health 80+ and Force 75+, it
// asks a teammate carrier with health under bot_runnerCampSkHealth (40), or
// Force under 20 without Speed, to /kill (Bot2_RequestSelfKill); the runner and
// the camper honour it while carrying. RUNNER_CAMP lines log goal changes.
static struct {int goal,since,spotTime,have,skNext;vec3_t spot;} runnerCamp[MAX_CLIENTS];
static qboolean RunnerCamp(int clientNum,gentity_t *ent,gentity_t *enemyFlag,usercmd_t *cmd) {
    playerState_t *ps=&ent->client->ps;int team=ent->client->sess.sessionTeam,i,goal=0;
    int ownPw=team==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG,enemyPw=team==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG;
    gentity_t *mate=NULL,*fc=NULL;vec3_t stand,to;float leash=RunnerCvar("bot_runnerCampLeash",1000);
    if(!RunnerCvar("bot_runnerCamp",1) || !enemyFlag)return qfalse;
    VectorCopy(VectorLength(enemyFlag->s.origin)>0?enemyFlag->s.origin:enemyFlag->r.currentOrigin,stand);
    for(i=0;i<level.maxclients;i++) {
        gentity_t *o=&g_entities[i];
        if(i==clientNum || !o->inuse || !o->client || o->client->pers.connected!=CON_CONNECTED || o->health<=0)continue;
        if(o->client->sess.sessionTeam==team && o->client->ps.powerups[enemyPw])mate=o;
        else if(o->client->sess.sessionTeam!=team && o->client->ps.powerups[ownPw] && Distance(o->client->ps.origin,stand)<leash &&
                (RunnerCanSee(ent,o) || Distance(o->client->ps.origin,ps->origin)<RunnerCvar("bot_defenderHearRange",1500)))fc=o;
    }
    if(fc) {
        int cell=Graph_Nearest(fc->client->ps.origin,128,128);goal=1;
        if(cell>=0) {const graphCell_t *c=Graph_Cell(cell);VectorSet(to,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);}
        else VectorCopy(fc->client->ps.origin,to);
    } else {
        if(!runnerCamp[clientNum].have || (level.time-runnerCamp[clientNum].spotTime>=1000 && Distance(ps->origin,runnerCamp[clientNum].spot)>48)) {
            runnerCamp[clientNum].have=Bot2_CampSpot(stand,200,500,ps->origin,runnerCamp[clientNum].spot);runnerCamp[clientNum].spotTime=level.time;
        }
        VectorCopy(runnerCamp[clientNum].have?runnerCamp[clientNum].spot:stand,to);
    }
    if(goal!=runnerCamp[clientNum].goal || !runnerCamp[clientNum].since) {
        trap->Print("[RUNNER] RUNNER_CAMP level=%d client=%d goal=%s from=%s ms=%d x=%.0f y=%.0f z=%.0f to=%.0f,%.0f,%.0f stand=%.0f\n",level.time,clientNum,
            goal?"fc":"camp",runnerCamp[clientNum].since?(runnerCamp[clientNum].goal?"fc":"camp"):"none",
            runnerCamp[clientNum].since?level.time-runnerCamp[clientNum].since:0,ps->origin[0],ps->origin[1],ps->origin[2],to[0],to[1],to[2],
            Distance(ps->origin,stand));
        runnerCamp[clientNum].goal=goal;runnerCamp[clientNum].since=level.time;
    }
    if(!goal && mate && Distance(ps->origin,stand)<600 && ent->health>=80 && ps->fd.forcePower>=75 &&
       (mate->health<RunnerCvar("bot_runnerCampSkHealth",40) ||
        (mate->client->ps.fd.forcePower<20 && !(mate->client->ps.fd.forcePowersActive&(1<<FP_SPEED))))) {
        Bot2_RequestSelfKill(mate->s.number,clientNum);
        if(level.time>=runnerCamp[clientNum].skNext) {
            runnerCamp[clientNum].skNext=level.time+2000;
            trap->Print("[RUNNER] SK_REQUEST level=%d client=%d carrier=%d carrier_health=%d carrier_force=%d health=%d force=%d stand=%.0f\n",
                level.time,clientNum,mate->s.number,mate->health,mate->client->ps.fd.forcePower,ent->health,ps->fd.forcePower,Distance(ps->origin,stand));
        }
    }
    bot2_states[clientNum].macroState=MACRO_CAMP_REGEN;bot2_states[clientNum].targetEntNum=-1;
    VectorCopy(to,bot2_states[clientNum].macroTargetOrigin);
    if(goal || Distance(ps->origin,to)>48)Bot2_ExecuteMovement(clientNum,cmd,bot2_states[clientNum].macroTargetOrigin);
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    RunnerAbsorbPolicy(clientNum,ent,cmd);
    RunnerProtectPolicy(clientNum,ent,cmd);
    CarrierMelee(clientNum,cmd);
    return qtrue;
}

void Bot2_MovementOnlyCommand(int clientNum,int time,usercmd_t *cmd) {
    gentity_t *ent=&g_entities[clientNum],*goal=NULL,*home,*enemy,*droppedHome,*droppedEnemy;
    playerState_t *ps=&ent->client->ps;
    int team=ent->client->sess.sessionTeam,axis;
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=time;cmd->weapon=ps->weapon;
    for(axis=0;axis<3;axis++)cmd->angles[axis]=ANGLE2SHORT(ps->viewangles[axis])-ps->delta_angles[axis];
    if(ent->health<=0) {
        // Native respawn input, without changing the native death delay.
        Bot2_ClearState(clientNum);cmd->buttons=BUTTON_ATTACK;return;
    }
    if(team!=TEAM_RED && team!=TEAM_BLUE)return;
    RunnerPerceive(clientNum,ent);   // every live frame on a team, before either power policy
    home=Flag(team,qfalse);enemy=Flag(team==TEAM_RED?TEAM_BLUE:TEAM_RED,qfalse);
    droppedHome=Flag(team,qtrue);droppedEnemy=Flag(team==TEAM_RED?TEAM_BLUE:TEAM_RED,qtrue);
    if(droppedHome && !Bot2_ReachableDrop(clientNum,droppedHome,team))droppedHome=NULL;
    if(droppedEnemy && !Bot2_ReachableDrop(clientNum,droppedEnemy,team==TEAM_RED?TEAM_BLUE:TEAM_RED))
        droppedEnemy=enemy; // Stage at its stand; never alter the native return.
    bot2_states[clientNum].role=ROLE_OFFENSE;
    {
        const float *point=GoalChainPoint(clientNum,ps);
        if(point) {
            bot2_states[clientNum].macroState=MACRO_FETCH_FLAG;bot2_states[clientNum].targetEntNum=-1;
            VectorCopy(point,bot2_states[clientNum].macroTargetOrigin);
            Bot2_ExecuteMovement(clientNum,cmd,bot2_states[clientNum].macroTargetOrigin);
            cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
            if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
            RunnerAbsorbPolicy(clientNum,ent,cmd);
            RunnerProtectPolicy(clientNum,ent,cmd);
            return;
        }
    }
    if(ps->powerups[team==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG]) {
        // A fresh teammate camping the enemy stand asked for a /kill (RunnerCamp): the flag returns there for its run.
        if(Bot2_SelfKillRequested(clientNum)>=0 && Bot2_SelfKillHonour(ent,"runner")) {
            trap->Print("[RUNNER] SK_HONOUR level=%d client=%d requester=%d health=%d force=%d\n",level.time,clientNum,
                Bot2_SelfKillRequested(clientNum),ent->health,ps->fd.forcePower);
            G_Kill(ent);return;
        }
        goal=droppedHome?droppedHome:home;
        bot2_states[clientNum].macroState=MACRO_RETURN_FLAG;
    } else if(droppedEnemy || Available(enemy)) {
        goal=droppedEnemy?droppedEnemy:enemy;
        bot2_states[clientNum].macroState=MACRO_FETCH_FLAG;
    } else if(droppedHome) {
        goal=droppedHome;bot2_states[clientNum].macroState=MACRO_CHASE_THIEF;
    } else {
        // A teammate carrying the enemy flag leaves no pickup available: camp the enemy stand (RunnerCamp).
        if(RunnerCamp(clientNum,ent,enemy,cmd))return;
        bot2_states[clientNum].macroState=MACRO_CAMP_REGEN;
    }
    bot2_states[clientNum].targetEntNum=goal?goal->s.number:-1;
    VectorCopy(goal?goal->r.currentOrigin:ps->origin,bot2_states[clientNum].macroTargetOrigin);
    if(!goal)return;
    Bot2_ExecuteMovement(clientNum,cmd,bot2_states[clientNum].macroTargetOrigin);
    // Movement owns Speed activation/deactivation, including resource waits.
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    RunnerAbsorbPolicy(clientNum,ent,cmd);
    RunnerProtectPolicy(clientNum,ent,cmd);
    CarrierMelee(clientNum,cmd);
}
