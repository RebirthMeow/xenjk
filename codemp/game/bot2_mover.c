// Resting movers as floor. See bot2_mover.h.
#include "g_local.h"
#include "bot2_mover.h"
#include "bot2_forecast.h"
#include "bot2_movecost.h"

// The binary mover's own schedule (g_mover.c) and the delayed trigger fire
// (g_trigger.c). Compared by address only; nothing here calls them.
void Reached_BinaryMover(gentity_t *ent);
void Use_BinaryMover_Go(gentity_t *ent);
void ReturnToPos1(gentity_t *ent);
void multi_trigger_run(gentity_t *ent);

static gentity_t *Master(gentity_t *ent) {
    return (ent->flags&FL_TEAMSLAVE) && ent->teammaster ? ent->teammaster : ent;
}

static qboolean BinaryMover(const gentity_t *ent) {
    return ent->inuse && ent->s.eType==ET_MOVER && ent->reached==Reached_BinaryMover;
}

// Uncached. A mover in motion, or one whose trajectory is not stationary, is
// not floor now. At rest, the next motion is the earliest of: its own
// scheduled start (Use_BinaryMover_Go after its delay) or return
// (ReturnToPos1 after its wait; a toggle waits with nextthink -1, forever),
// and each delayed trigger_multiple already counting down to use it. Spawn
// thinks (Think_MatchTeam, the door trigger spawn) move nothing and are not
// counted, so a lift is floor at map load.
static int RestUntil(gentity_t *ent) {
    gentity_t *m;int until=BOT2_MOVER_FOREVER,i;
    if(!BinaryMover(ent))return 0;
    m=Master(ent);
    if(m->moverState!=MOVER_POS1 && m->moverState!=MOVER_POS2)return 0;
    if(ent->s.pos.trType!=TR_STATIONARY || m->s.pos.trType!=TR_STATIONARY)return 0;
    if((m->think==Use_BinaryMover_Go || m->think==ReturnToPos1) && m->nextthink>0)until=m->nextthink;
    // A pending fire counts when any of its targets is a piece of this team (a
    // call aimed at a slave's targetname starts the master too, as CallsPrepare
    // resolves it).
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        gentity_t *t=&g_entities[i],*target=NULL;int at;
        if(!t->inuse || t->think!=multi_trigger_run || t->nextthink<=0 || !t->target || !t->target[0])continue;
        while((target=G_Find(target,FOFS(targetname),t->target))!=NULL)if(Master(target)==m)break;
        if(!target)continue;
        at=t->nextthink+m->delay;
        if(at<until)until=at;
    }
    return until;
}

// Memoized per level time within one real game frame (Bot2_MoverFrame): the
// world is frozen while a frame's rollouts and searches move level.time
// through simulated frames, so their answers agree. Across real frames the
// world moves, and an answer a rollout wrote at a simulated time was returned
// to the real frame at that time though the lift had been called meanwhile.
// At map load level.time and level.startTime can both be 0, so an entry is
// valid only once written.
// Keyed on the real frame and level.startTime alone: RestUntil reads entity fields and
// no clock, so the simulated level.time a rollout steps through never changes its answer, and keying on it made a
// rollout standing on a lift miss at every simulated step and rescan every entity.
// Perf counters: serial is the forecast scope the entry was written in, to count the misses a key on
// the scope serial would have answered, and whether its stored answer agreed.
static struct {int valid,time,start,frame;int value;unsigned int serial;} restMemo[MAX_GENTITIES];
static int realFrame;
void Bot2_MoverFrame(void) {realFrame++;}

int Bot2_MoverRestUntil(int entityNum) {
    if(entityNum<0 || entityNum>=ENTITYNUM_WORLD)return 0;
    BOT2_PERF_N(BOT2_PERF_MOVER);
    if(!restMemo[entityNum].valid || restMemo[entityNum].start!=level.startTime || restMemo[entityNum].frame!=realFrame) {
        unsigned int serial=bot2_perfOn?Bot2_ForecastScopeSerial():0;
        int scopeHit=bot2_perfOn && serial && restMemo[entityNum].valid && restMemo[entityNum].serial==serial &&
                     restMemo[entityNum].start==level.startTime,old=restMemo[entityNum].value;
        BOT2_PERF_BEGIN(perfRest);
        restMemo[entityNum].valid=1;restMemo[entityNum].time=level.time;restMemo[entityNum].start=level.startTime;
        restMemo[entityNum].frame=realFrame;
        restMemo[entityNum].value=RestUntil(&g_entities[entityNum]);
        restMemo[entityNum].serial=serial;
        BOT2_PERF_END(BOT2_PERF_MOVER_MISS,perfRest);
        if(scopeHit) {bot2_perf.n[BOT2_PERF_MOVER_SCOPEHIT]++;if(old!=restMemo[entityNum].value)bot2_perf.n[BOT2_PERF_MOVER_SCOPEDIFF]++;}
    }
    return restMemo[entityNum].value;
}

qboolean Bot2_StandableEntity(int entityNum) {
    return entityNum==ENTITYNUM_WORLD || Bot2_MoverRestUntil(entityNum)>level.time;
}

static struct {
    int start,total;
    signed char count[MAX_GENTITIES];       // -1 not a lift call, else its movers
    short mover[MAX_GENTITIES][BOT2_MOVER_CALL_TARGETS];
    int delay[MAX_GENTITIES][BOT2_MOVER_CALL_TARGETS];
} calls={-1,0};

static void CallsPrepare(void) {
    int i;
    if(calls.start==level.startTime)return;
    calls.start=level.startTime;calls.total=0;
    for(i=0;i<MAX_GENTITIES;i++) {
        gentity_t *t=&g_entities[i],*target=NULL;int targets=0,movers=0,n=0,overflow=0;
        calls.count[i]=-1;
        if(i<MAX_CLIENTS || i>=level.num_entities || !t->inuse || !t->classname || Q_stricmp(t->classname,"trigger_multiple") ||
           !t->target || !t->target[0])continue;
        if((t->target2 && t->target2[0]) || (t->target3 && t->target3[0]) || (t->target4 && t->target4[0]) ||
           (t->behaviorSet[BSET_USE] && t->behaviorSet[BSET_USE][0]))continue;
        // Touch_Multi's gates (g_trigger.c): a call a bot's touch does not fire
        // - use or fire button, NPC only or named, one team, head in the
        // volume, siege items, deactivated - is not a lift call. FACING (2) is
        // kept: ctf4's four lifts are called so, and a rider facing the lift
        // fires it (the ride does not aim for it yet).
        if((t->spawnflags&(4|8)) || (!(t->spawnflags&1) && ((t->spawnflags&16) || (t->NPC_targetname && t->NPC_targetname[0]))) ||
           t->alliedTeam || t->radius || t->genericValue1 || (t->flags&FL_INACTIVE))continue;
        while((target=G_Find(target,FOFS(targetname),t->target))!=NULL) {
            gentity_t *m=Master(target);int k;
            targets++;
            if(!BinaryMover(target))continue;
            movers++;
            for(k=0;k<n;k++)if(calls.mover[i][k]==m->s.number)break;
            if(k<n)continue;    // a team's master reached twice
            if(n>=BOT2_MOVER_CALL_TARGETS) {overflow=1;break;}
            calls.mover[i][n]=m->s.number;calls.delay[i][n]=t->delay+m->delay;n++;
        }
        if(targets>0 && movers==targets && !overflow) {calls.count[i]=n;calls.total++;}
    }
}

int Bot2_MoverCalls(void) {CallsPrepare();return calls.total;}

int Bot2_MoverCallTargets(int triggerNum, int *movers, int *delayMs, int max) {
    int k,n;
    if(triggerNum<0 || triggerNum>=MAX_GENTITIES)return 0;
    CallsPrepare();
    n=calls.count[triggerNum];
    if(n<=0)return 0;
    if(n>max)n=max;
    for(k=0;k<n;k++) {
        if(movers)movers[k]=calls.mover[triggerNum][k];
        if(delayMs)delayMs[k]=calls.delay[triggerNum][k];
    }
    return n;
}

// A lift call a player standing at the origin touches (the forecast's box
// overlap, its standing extents), and the least delay among those that start
// this mover: how long a bot standing there waits before it moves. -1 when
// no call reaches that spot.
int Bot2_MoverBoardDelay(int mover, const vec3_t origin) {
    int i,k,n,best=-1,movers[BOT2_MOVER_CALL_TARGETS],delays[BOT2_MOVER_CALL_TARGETS];
    if(!Bot2_MoverCalls() || mover<0 || mover>=ENTITYNUM_WORLD)return -1;
    mover=Master(&g_entities[mover])->s.number;
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        const gentity_t *t=&g_entities[i];int j;
        if(!(n=Bot2_MoverCallTargets(i,movers,delays,BOT2_MOVER_CALL_TARGETS)))continue;
        for(j=0;j<3;j++) {
            float low=origin[j]-(j==2?24.0f:15.0f),high=origin[j]+(j==2?40.0f:15.0f);
            if(high<t->r.absmin[j] || low>t->r.absmax[j])break;
        }
        if(j<3)continue;
        for(k=0;k<n;k++)if(movers[k]==mover && (best<0 || delays[k]<best))best=delays[k];
    }
    return best;
}

int Bot2_MoverMaster(int entityNum) {
    if(entityNum<0 || entityNum>=ENTITYNUM_WORLD)return entityNum;
    return Master(&g_entities[entityNum])->s.number;
}

int Bot2_MoverModel(int entityNum) {
    const gentity_t *ent;
    if(entityNum<0 || entityNum>=ENTITYNUM_WORLD)return 0;
    ent=&g_entities[entityNum];
    if(!ent->inuse || ent->s.eType!=ET_MOVER || !ent->model || ent->model[0]!='*')return 0;
    return atoi(ent->model+1);
}

// Per map: the planner asks for every relax into a lift's cell.
static struct {int start;short entity[MAX_MODELS];} modelEntity={-1};

int Bot2_MoverEntity(int model) {
    int i,m;
    if(model<=0 || model>=MAX_MODELS)return -1;
    if(modelEntity.start!=level.startTime) {
        modelEntity.start=level.startTime;
        for(i=0;i<MAX_MODELS;i++)modelEntity.entity[i]=-1;
        for(i=MAX_CLIENTS;i<level.num_entities;i++)
            if((m=Bot2_MoverModel(i))>0 && m<MAX_MODELS && modelEntity.entity[m]<0)modelEntity.entity[m]=i;
    }
    return modelEntity.entity[model];
}

void Bot2_MoverReset(void) {
    memset(restMemo,0,sizeof(restMemo));
    calls.start=-1;modelEntity.start=-1;
}
