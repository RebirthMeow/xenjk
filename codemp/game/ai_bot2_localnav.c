// The ground corridor the movement rewrite's walk and graph admission share:
// one corridor sample (Bot2_GroundSample), four-sample corridors and their
// per-boundary memo. Reads the world only. The pre-rewrite local navigation
// and corner recovery layers (bot_moveLayers) that lived here were removed.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2_internal.h"
#include "bot2_forecast.h"
#include "bot2_movecost.h"
#include "bot2_mover.h"

// The entity that refused the last ground corridor: ENTITYNUM_WORLD for map
// geometry, ENTITYNUM_NONE when nothing refused. Same-frame callers only.
static int corridorHit=ENTITYNUM_NONE;

// Check the bounded hull occupied by one direct or native-height raised step.
// Only trigger_hurt is hazardous; flags and other benign triggers stay legal.
// Row 5 diagnostic: set while a profiled rollout's corridor test runs.
static int corridorProfile;
static void CorridorTrace(trace_t *tr,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
    double began=corridorProfile?Bot2_MoveCostClock():0;
    trap->Trace(tr,start,mins,maxs,end,pass,mask,qfalse,0,0);
    if(corridorProfile){Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_CORRIDOR_TRACE,began);Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_CORRIDOR_TRACES);}
}

static const char *GroundCorridorHurt(const vec3_t hullMins,const vec3_t hullMaxs,const vec3_t start,const vec3_t end,qboolean raised) {
    int entities[64],count,i;vec3_t mins,maxs;double began;
    for(i=0;i<3;i++) {
        mins[i]=min(start[i],end[i])+hullMins[i];
        maxs[i]=max(start[i],end[i])+hullMaxs[i];
    }
    if(raised)maxs[2]=max(start[2]+STEPSIZE,end[2])+hullMaxs[2];
    began=corridorProfile?Bot2_MoveCostClock():0;
    count=trap->EntitiesInBox(mins,maxs,entities,ARRAY_LEN(entities));
    if(count>=ARRAY_LEN(entities)) {
        if(corridorProfile){Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_CORRIDOR_BOX,began);Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_CORRIDOR_BOXES);}
        return "hurt_saturation";
    }
    for(i=0;i<count;i++) {
        gentity_t *hit=&g_entities[entities[i]];
        if(hit->inuse && (hit->r.contents&CONTENTS_TRIGGER) && hit->classname &&
           !Q_stricmp(hit->classname,"trigger_hurt") &&
           trap->EntityContact(mins,maxs,(sharedEntity_t*)hit,qfalse)) {
            if(corridorProfile){Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_CORRIDOR_BOX,began);Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_CORRIDOR_BOXES);}
            return "trigger_hurt";
        }
    }
    if(corridorProfile){Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_CORRIDOR_BOX,began);Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_CORRIDOR_BOXES);}
    return NULL;
}

// One corridor sample: the floor, step, sweep and hurt checks that admit a
// standing point beside a previous one. The sample stays within native
// STEPSIZE, traverses directly or through the bounded native raised-step
// shape, and retains conservative world-floor, liquid and steep-slope
// rejection. previous is a hull origin resting .125 above its floor; point
// arrives with the target x and y and leaves at its validated height. The
// movement rewrite's preparation (bot2_graph.c) runs the same sample cell to
// cell over the map, so the graph admits exactly what the walk admits.
// Returns NULL when admitted, else the refusal name; *hit receives the entity
// that decided it (ENTITYNUM_NONE when nothing was struck). A mover at rest
// is floor like the world's (bot2_mover.h); Bot2_GroundSampleFloor names it.
static int groundSampleFloor=ENTITYNUM_WORLD;
int Bot2_GroundSampleFloor(void) {return groundSampleFloor;}
// bot_moveFixBM10 (default 0): set when a sample stood on a floor that is not the world
// (a mover at rest, whose verdict holds only at this level.time); MemoCorridor clears it before each corridor it
// may store. Only written here and read there.
static int groundSampleMoverFloor;

const char *Bot2_GroundSample(int passEntity,const vec3_t mins,const vec3_t maxs,const vec3_t previous,
                              vec3_t point,int sample,int *hit) {
    vec3_t top,bottom,raisedStart,raisedEnd;trace_t floor,sweep;qboolean raised=qfalse;
    VectorCopy(point,top);top[2]=previous[2]+STEPSIZE;
    VectorCopy(top,bottom);bottom[2]-=2*STEPSIZE;
    CorridorTrace(&floor,top,mins,maxs,bottom,passEntity,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME);
    if(floor.startsolid || floor.allsolid || floor.fraction==1 ||
       (floor.contents&(CONTENTS_LAVA|CONTENTS_SLIME)) || !Bot2_StandableEntity(floor.entityNum) ||
       floor.plane.normal[2]<.7f) {
        const char *value=floor.startsolid?"floor_startsolid":floor.allsolid?"floor_allsolid":
            floor.fraction==1?"floor_missing":(floor.contents&(CONTENTS_LAVA|CONTENTS_SLIME))?"floor_liquid":
            !Bot2_StandableEntity(floor.entityNum)?"floor_nonworld":"floor_steep";
        if(passEntity<MAX_CLIENTS && trap->Cvar_VariableIntegerValue("bot_supportedTrace"))
            Bot2_PrintTelemetry(1,"[GROUNDTRACE] floor_refused client=%d level=%d reason=%s sample=%d start=%.3f,%.3f,%.3f end=%.3f,%.3f,%.3f hit=%d contents=%d startsolid=%d allsolid=%d fraction=%.6f normal_z=%.3f\n",
                passEntity,level.time,value,sample,top[0],top[1],top[2],bottom[0],bottom[1],bottom[2],
                floor.entityNum,floor.contents,floor.startsolid,floor.allsolid,floor.fraction,floor.plane.normal[2]);
        *hit=floor.fraction==1?ENTITYNUM_NONE:floor.entityNum;
        return value;
    }
    VectorCopy(floor.endpos,point);point[2]+=.125f;
    *hit=ENTITYNUM_WORLD;groundSampleFloor=floor.entityNum;
    if(floor.entityNum!=ENTITYNUM_WORLD)groundSampleMoverFloor=1;   // bot_moveFixBM10
    if(point[2]-previous[2]>STEPSIZE+.25f)return "ascent";
    if(previous[2]-point[2]>STEPSIZE+.25f)return "drop";
    CorridorTrace(&sweep,previous,mins,maxs,point,passEntity,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME);
    if(sweep.contents&(CONTENTS_LAVA|CONTENTS_SLIME))return "sweep_liquid";
    if(sweep.startsolid || sweep.allsolid || sweep.fraction<1) {
        *hit=sweep.entityNum;
        if(point[2]<=previous[2]) {
            // A descending chord can cut through the lip of a supported
            // step. Native movement crosses at the current height before
            // settling down; validate both parts of that bounded shape.
            // Floor, maximum drop, liquids, bodies and hurt checks remain.
            VectorCopy(point,raisedEnd);raisedEnd[2]=previous[2];
            CorridorTrace(&sweep,previous,mins,maxs,raisedEnd,passEntity,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME);
            if(sweep.startsolid || sweep.allsolid || sweep.fraction<1 ||
               (sweep.contents&(CONTENTS_LAVA|CONTENTS_SLIME))) {
                *hit=sweep.entityNum;return "descent_cross_blocked";
            }
            CorridorTrace(&sweep,raisedEnd,mins,maxs,point,passEntity,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME);
            if(sweep.startsolid || sweep.allsolid || sweep.fraction<1 ||
               (sweep.contents&(CONTENTS_LAVA|CONTENTS_SLIME))) {
                *hit=sweep.entityNum;return "descent_settle_blocked";
            }
        } else {
            raised=qtrue;VectorCopy(previous,raisedStart);raisedStart[2]+=STEPSIZE;
            CorridorTrace(&sweep,previous,mins,maxs,raisedStart,passEntity,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME);
            if(sweep.startsolid || sweep.allsolid || sweep.fraction<1) {*hit=sweep.entityNum;return "step_up_blocked";}
            VectorCopy(raisedStart,raisedEnd);raisedEnd[0]=point[0];raisedEnd[1]=point[1];
            CorridorTrace(&sweep,raisedStart,mins,maxs,raisedEnd,passEntity,MASK_PLAYERSOLID|CONTENTS_LAVA|CONTENTS_SLIME);
            if(sweep.startsolid || sweep.allsolid || sweep.fraction<1) {*hit=sweep.entityNum;return "step_cross_blocked";}
        }
    }
    return GroundCorridorHurt(mins,maxs,previous,point,raised);
}

static qboolean GroundCorridorFail(const char **reason,int *sample,const char *value,int at) {
    if(reason)*reason=value;if(sample)*sample=at;return qfalse;
}

// Four progressive floor samples, each one Bot2_GroundSample.
static qboolean GroundCorridor(gentity_t *ent,const vec3_t start,const vec3_t end,vec3_t result,
                               const char **reason,int *sample) {
    vec3_t previous,point;int i;const char *value;
    if(reason)*reason="none";if(sample)*sample=0;corridorHit=ENTITYNUM_NONE;
    VectorCopy(start,previous);previous[2]+=.125f;
    for(i=1;i<=4;i++) {
        VectorSubtract(end,start,point);VectorMA(start,i*.25f,point,point);
        value=Bot2_GroundSample(ent->s.number,ent->r.mins,ent->r.maxs,previous,point,i,&corridorHit);
        if(value)return GroundCorridorFail(reason,sample,value,i);
        VectorCopy(point,previous);
    }
    corridorHit=ENTITYNUM_NONE;
    VectorCopy(previous,result);return qtrue;
}

// The corridor memo. Open addressing with stamps, like the forecast's trace
// cache: the key is the entity, its hull and both endpoints bit for bit; the
// value is everything a call leaves behind - its verdict, the standing point,
// the refusal name, the sample index and corridorHit. A full table stops
// storing; outside a scope every call runs.
#define CORRIDOR_MEMO_SIZE 8192
#define CORRIDOR_MEMO_LOAD (CORRIDOR_MEMO_SIZE*3/4)
typedef struct { float start[3],end[3],mins[3],maxs[3];int entity; } corridorKey_t;
typedef struct { qboolean ok;vec3_t result;const char *reason;int sample,hit; } corridorValue_t;
static struct {
    int active,count;unsigned int stamp;
    int fixBM10;   // bot_moveFixBM10 (default 0), read at each scope's begin
    unsigned int entryStamp[CORRIDOR_MEMO_SIZE];
    corridorKey_t key[CORRIDOR_MEMO_SIZE];
    corridorValue_t value[CORRIDOR_MEMO_SIZE];
} corridorMemo;

void Bot2_GroundCorridorMemoBegin(void) {
    corridorMemo.active=1;
    corridorMemo.fixBM10=trap->Cvar_VariableIntegerValue("bot_moveFixBM10")>0;
    corridorMemo.count=0;
    if(++corridorMemo.stamp==0) {memset(corridorMemo.entryStamp,0,sizeof(corridorMemo.entryStamp));corridorMemo.stamp=1;}
}
void Bot2_GroundCorridorMemoEnd(void) {corridorMemo.active=0;}
void Bot2_GroundCorridorMemoFlush(void) {if(corridorMemo.active)Bot2_GroundCorridorMemoBegin();}

static unsigned int CorridorHash(const corridorKey_t *k) {
    const unsigned char *p=(const unsigned char *)k;unsigned int h=2166136261u;size_t i;
    for(i=0;i<sizeof(*k);i++)h=(h^p[i])*16777619u;
    return h;
}

static qboolean MemoCorridor(gentity_t *ent,const vec3_t start,const vec3_t end,vec3_t result,
                             const char **reason,int *sample) {
    corridorKey_t k;unsigned int h;qboolean ok;const char *why="none";int at=0;
    if(!corridorMemo.active) {bot2_meterCorridors++;return GroundCorridor(ent,start,end,result,reason,sample);}
    memset(&k,0,sizeof(k));
    VectorCopy(start,k.start);VectorCopy(end,k.end);VectorCopy(ent->r.mins,k.mins);VectorCopy(ent->r.maxs,k.maxs);
    k.entity=ent->s.number;
    h=CorridorHash(&k)&(CORRIDOR_MEMO_SIZE-1);
    for(;;) {
        if(corridorMemo.entryStamp[h]!=corridorMemo.stamp)break;
        if(!memcmp(&corridorMemo.key[h],&k,sizeof(k))) {
            const corridorValue_t *v=&corridorMemo.value[h];
            if(v->ok)VectorCopy(v->result,result);
            if(reason)*reason=v->reason;if(sample)*sample=v->sample;corridorHit=v->hit;
            Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_CORRIDOR_MEMO_HITS);
            return v->ok;
        }
        h=(h+1)&(CORRIDOR_MEMO_SIZE-1);
    }
    groundSampleMoverFloor=0;
    ok=GroundCorridor(ent,start,end,result,&why,&at);bot2_meterCorridors++;
    if(reason)*reason=why;if(sample)*sample=at;
    // bot_moveFixBM10 (default 0): the key has no simulated time, so a verdict that rested
    // on a non-world floor (a mover is floor only while at rest at this level.time) or was refused for one
    // (floor_nonworld) is not stored: a rollout at another simulated time recomputes it.
    if(corridorMemo.fixBM10 && (groundSampleMoverFloor || !strcmp(why,"floor_nonworld")))return ok;
    if(corridorMemo.count<CORRIDOR_MEMO_LOAD) {
        corridorValue_t *v=&corridorMemo.value[h];
        corridorMemo.entryStamp[h]=corridorMemo.stamp;corridorMemo.key[h]=k;corridorMemo.count++;
        v->ok=ok;v->reason=why;v->sample=at;v->hit=corridorHit;
        if(ok)VectorCopy(result,v->result);
    }
    return ok;
}

// The movement rewrite's walk executor and recovery use the same corridor.
// Inside a profiled forecast rollout (row 5 diagnostic) the call, its traces
// and its hurt-volume area queries are charged to the cost capture.
qboolean Bot2_GroundCorridor(gentity_t *ent,const vec3_t start,const vec3_t end,vec3_t result,
                             const char **reason,int *sample) {
    qboolean ok;double began;
    if(!Bot2_ForecastProfiling())return MemoCorridor(ent,start,end,result,reason,sample);
    began=Bot2_MoveCostClock();corridorProfile=1;
    ok=MemoCorridor(ent,start,end,result,reason,sample);
    corridorProfile=0;
    Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_CORRIDOR,began);
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_CORRIDOR_CALLS);
    return ok;
}
