"""Compile the production trigger shadow diagnostic against native contact fixtures.

Extracts the real diagnostic block from `bot2_forecast.c` - the census, the
record store, `ShadowExamine` and the production `TouchesTrigger` - together
with the real `BG_PlayerTouchesItem` from `bg_misc.c`, and drives them against
deterministic stub geometry where the brush a trigger owns is separate from the
bounding box that encloses it.

It checks two different things:

  * that the diagnostic is inert - `TouchesTrigger` returns the same refusal
    and the same touched entity with the diagnostic on and off, over every
    fixture, and the stub world is byte-identical afterwards;
  * that the ladder old -> aabb -> swept -> endpoint -> processed resolves each
    native boundary the way `G_TouchTriggers` would.

This is not native physics and not a map. It is the decision logic only, with
`trap->EntityContact` stubbed so a brush can be made to disagree with its own
bounding box on purpose. A deliberately wrong query is compiled as a negative
control and must fail.
"""
import argparse
import shutil
import unittest
import sys
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid


PRELUDE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define Q_vsnprintf vsnprintf
#define MAX_QPATH 64
#define FS_WRITE 1
typedef int fileHandle_t;

typedef int qboolean;
#define qtrue 1
#define qfalse 0
typedef float vec3_t[3];
#define Com_sprintf snprintf
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define VectorAdd(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorSet(v,x,y,z) ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#endif
static void Q_strncpyz(char *d,const char *s,int n){strncpy(d,s,n-1);d[n-1]=0;}

#define CONTENTS_TRIGGER 0x40000000
#define ET_ITEM 2
#define ET_MOVER 3
#define IT_WEAPON 1
#define IT_AMMO 2
#define IT_ARMOR 3
#define IT_HEALTH 4
#define IT_POWERUP 5
#define STAT_HEALTH 0
#define PMF_DUCKED 1
#define MAX_GENTITIES 64

typedef struct {int giType;} gitem_t;
typedef struct {vec3_t base;} trajectory_t;
typedef struct {int number,eType;trajectory_t pos;} entityState_t;
typedef struct {int contents;vec3_t absmin,absmax;} entityShared_t;
typedef struct {vec3_t origin;int stats[16],pm_flags,commandTime;} playerState_t;
typedef struct {playerState_t ps;} gclient_t;
typedef struct gentity_s {
    entityState_t s; entityShared_t r;
    int inuse; const char *classname; gitem_t *item; gclient_t *client;
    void (*touch)(struct gentity_s *,struct gentity_s *,void *);
    /* Test-only: the brush the trigger actually owns, inside its bounds. */
    vec3_t brushMins,brushMaxs; int brushed;
} gentity_t;
typedef gentity_t sharedEntity_t;
typedef struct {vec3_t mins,maxs;int pmove_fixed,pmove_msec;} pmove_t;

static gentity_t g_entities[MAX_GENTITIES];
static struct {int maxclients,num_entities,time;} level;
static int forecastItemsPass;
/* Declared outside the extracted block in bot2_forecast.c: no rollout scope, an
   empty-cached trigger list, the flag pass off, and no trigger is a lift call. */
#define IT_TEAM 6
#define BOT2_MOVER_CALL_TARGETS 4
static int forecastFlagPass;
static struct {int active;unsigned int serial;} scope;
static struct {unsigned int serial;int count;int index[MAX_GENTITIES];} triggerList;
static int Bot2_MoverCallTargets(int e,int *movers,int *delays,int n){(void)e;(void)movers;(void)delays;(void)n;return 0;}
static qboolean OwnFlagAtBase(const gentity_t *other){(void)other;return qfalse;}
static struct {gentity_t *entity;const pmove_t *root;} requestStorage,*activeRequest=&requestStorage;

/* Stub contact: brush overlap when a brush is declared, bounds otherwise. It
   records its calls so a fixture can assert the query is read-only. */
static int contactCalls;
static qboolean StubEntityContact(const vec3_t mins,const vec3_t maxs,const sharedEntity_t *ent,int capsule){
    const float *lo = ent->brushed ? ent->brushMins : ent->r.absmin;
    const float *hi = ent->brushed ? ent->brushMaxs : ent->r.absmax;
    int j; contactCalls++;
    for(j=0;j<3;j++) if(maxs[j]<lo[j]||mins[j]>hi[j]) return qfalse;
    return qtrue;
}
static void StubPrint(const char *fmt,...){(void)fmt;}
/* The report's file sink, captured so a fixture can assert what it wrote. */
static char reportBuffer[1<<20]; static int reportLength;
static int StubFSOpen(const char *name,fileHandle_t *f,int mode){*f=1;reportLength=0;return 0;}
static void StubFSWrite(const void *data,int length,fileHandle_t f){
    if(reportLength+length<(int)sizeof(reportBuffer)){
        memcpy(reportBuffer+reportLength,data,length);reportLength+=length;
        reportBuffer[reportLength]=0;
    }
}
static void StubFSClose(fileHandle_t f){(void)f;}
static struct {
    qboolean (*EntityContact)(const vec3_t,const vec3_t,const sharedEntity_t *,int);
    void (*Print)(const char *,...);
    int (*FS_Open)(const char *,fileHandle_t *,int);
    void (*FS_Write)(const void *,int,fileHandle_t);
    void (*FS_Close)(fileHandle_t);
} trapStorage={StubEntityContact,StubPrint,StubFSOpen,StubFSWrite,StubFSClose},*trap=&trapStorage;

static void BG_EvaluateTrajectory(const trajectory_t *tr,int atTime,vec3_t result){
    VectorCopy(tr->base,result);
}
'''

MAIN = r'''
static const char *caseName;
static int cases, failures;
#define CONSUME_TAG() ConsumeTag()
#define CHECK(x) do{ if(!(x)){ printf("FAIL case=%s line=%d: %s\n",caseName,__LINE__,#x); failures++; } }while(0)

static gentity_t *AddTrigger(const char *classname,float x0,float y0,float z0,
                             float x1,float y1,float z1,int hasTouch){
    gentity_t *e=&g_entities[level.num_entities];
    memset(e,0,sizeof(*e));
    e->s.number=level.num_entities; e->inuse=1; e->classname=classname;
    e->r.contents=CONTENTS_TRIGGER;
    VectorSet(e->r.absmin,x0,y0,z0); VectorSet(e->r.absmax,x1,y1,z1);
    if(hasTouch) e->touch=(void(*)(gentity_t*,gentity_t*,void*))1;
    level.num_entities++;
    return e;
}
static gentity_t *AddItem(const char *classname,int giType,float x,float y,float z){
    static gitem_t items[MAX_GENTITIES];
    gentity_t *e=AddTrigger(classname,x-16,y-16,z-16,x+16,y+16,z+16,1);
    items[e->s.number].giType=giType; e->item=&items[e->s.number];
    e->s.eType=ET_ITEM; VectorSet(e->s.pos.base,x,y,z);
    return e;
}
static gentity_t self; static gclient_t selfClient; static pmove_t root;

static void ResetWorld(int standing){
    memset(g_entities,0,sizeof(g_entities));
    memset(&shadow,0,sizeof(shadow)); memset(&self,0,sizeof(self));
    memset(&selfClient,0,sizeof(selfClient));
    level.maxclients=1; level.num_entities=1; level.time=1000;
    self.client=&selfClient; selfClient.ps.stats[STAT_HEALTH]=100;
    requestStorage.entity=&self; requestStorage.root=&root;
    root.pmove_fixed=0; root.pmove_msec=8;
    VectorSet(root.mins,-15,-15,-24);
    VectorSet(root.maxs,15,15,standing?40:16);
    VectorCopy(root.mins,shadow.hullMins); VectorCopy(root.maxs,shadow.hullMaxs);
    shadow.pmoveFixed=root.pmove_fixed; shadow.pmoveMsec=root.pmove_msec;
    forecastItemsPass=0; contactCalls=0;
}

/* The single safety property: the diagnostic changes no decision. Runs the
   shipped test twice over an identical world and compares result, touched
   entity and the world bytes. */
static int Inert(const vec3_t a,const vec3_t b){
    static gentity_t worldBefore[MAX_GENTITIES];
    gentity_t *touchedOff,*touchedOn; int off,on,same;
    memcpy(worldBefore,g_entities,sizeof(g_entities));
    shadow.on=0; off=TouchesTrigger(a,b); touchedOff=forecastTouched;
    CHECK(memcmp(worldBefore,g_entities,sizeof(g_entities))==0);
    shadow.on=1; on=TouchesTrigger(a,b); touchedOn=forecastTouched;
    same=memcmp(worldBefore,g_entities,sizeof(g_entities))==0;
    CHECK(same);
    CHECK(off==on);
    CHECK(touchedOff==touchedOn);
    return on;
}
/* Flags the diagnostic recorded for one entity in the call just made. */
static int FlagsFor(int entity){
    int i;
    for(i=0;i<shadow.records;i++) if(shadow.record[i].entity==entity) return shadow.record[i].flags;
    return -1;
}
static int CensusTotal(void){int i,n=0;for(i=0;i<BOT2_SHADOW_STATES;i++)n+=shadow.census[i];return n;}
static int ContactsWith(int bit){
    int i,n=0;for(i=0;i<BOT2_SHADOW_STATES;i++) if(i&bit) n+=shadow.census[i];return n;
}

static void Case(const char *name){caseName=name;cases++;}

int main(void){
    vec3_t a,b;

    /* ---- clear: nothing anywhere near the path ------------------------- */
    Case("clear");
    ResetWorld(1); AddTrigger("trigger_hurt",900,900,900,1000,1000,1000,1);
    VectorSet(a,0,0,0); VectorSet(b,32,0,0);
    CHECK(Inert(a,b)==qfalse);
    CHECK(shadow.examined==1); CHECK(CensusTotal()==1);
    CHECK(ContactsWith(BOT2_SHADOW_OLD)==0);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==0);
    CHECK(shadow.records==0); CHECK(shadow.dropped==0);

    /* ---- hull-edge overlap, player origin outside the trigger box ------
       The origin never enters the box; the standing hull does. Every rung
       must fire, which is what makes an origin-outside count meaningless. */
    Case("hull_edge_origin_outside");
    ResetWorld(1); AddTrigger("trigger_hurt",20,-40,-40,60,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,10,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(FlagsFor(1)==-1);   /* every rung agrees, so nothing is recorded */
    CHECK(ContactsWith(BOT2_SHADOW_OLD)==1);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==1);
    CHECK(ContactsWith(BOT2_SHADOW_PROCESSED)==1);

    /* ---- diagonal near miss: the bounding box overlaps, the brush does not */
    Case("diagonal_near_miss");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    CHECK(Inert(a,b)==qtrue);                       /* the shipped test refuses */
    CHECK(FlagsFor(1)==(BOT2_SHADOW_OLD|BOT2_SHADOW_AABB));
    CHECK(ContactsWith(BOT2_SHADOW_SWEPT)==0);      /* no brush contact */
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==0);
    CHECK(shadow.records==1); CHECK(shadow.dropped==0);

    /* ---- crouched hull: the shipped fixed extents are 24 units too tall - */
    Case("crouched_hull");
    ResetWorld(0);   /* maxs[2] = 16, the crouch height */
    AddTrigger("trigger_hurt",-40,-40,20,40,40,36,1);
    VectorSet(a,0,0,0); VectorSet(b,4,0,0);
    shadow.ducked=1;
    CHECK(Inert(a,b)==qtrue);                       /* refused on the tall box */
    CHECK(FlagsFor(1)==BOT2_SHADOW_OLD);            /* and on nothing else */
    CHECK(ContactsWith(BOT2_SHADOW_AABB)==0);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==0);

    /* ---- standing in the same place does reach it ---------------------- */
    Case("standing_hull_reaches");
    ResetWorld(1);
    AddTrigger("trigger_hurt",-40,-40,20,40,40,36,1);
    VectorSet(a,0,0,0); VectorSet(b,4,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(FlagsFor(1)==-1);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==1);

    /* ---- a trigger with no touch function is never dispatched ---------- */
    Case("hurt_brush_without_touch");
    ResetWorld(1); AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,0);
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==1);
    CHECK(ContactsWith(BOT2_SHADOW_PROCESSED)==0);
    CHECK(shadow.noTouchAnywhere==1);
    CHECK(FlagsFor(1)==(BOT2_SHADOW_OLD|BOT2_SHADOW_AABB|BOT2_SHADOW_SWEPT|BOT2_SHADOW_ENDPOINT));

    /* ---- item inside its pickup region: every rung fires --------------- */
    Case("item_inside_pickup");
    ResetWorld(1); AddItem("item_medpak_instant",IT_HEALTH,20,0,0);
    VectorSet(a,0,0,0); VectorSet(b,10,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(shadow.itemsExamined==1); CHECK(shadow.itemGenericOnly==0);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==1);

    /* ---- item outside its pickup region, inside its bounding box -------
       Native chooses BG_PlayerTouchesItem INSTEAD of a box test; the shipped
       test falls through to the box and refuses on geometry native never
       consults. This is the divergence itemGenericOnly counts. */
    Case("item_generic_fallthrough");
    ResetWorld(1);
    {
        gentity_t *e=AddItem("item_medpak_instant",IT_HEALTH,0,0,0);
        VectorSet(e->r.absmin,-16,-16,-16); VectorSet(e->r.absmax,16,16,16);
        VectorSet(e->s.pos.base,0,0,200);          /* pickup region far above */
    }
    VectorSet(a,0,0,0); VectorSet(b,4,0,0);
    CHECK(Inert(a,b)==qtrue);                       /* the shipped test refuses */
    CHECK(shadow.itemGenericOnly==1);
    CHECK(FlagsFor(1)==BOT2_SHADOW_OLD);            /* no native rung agrees */
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==0);

    /* ---- the item bypass skips the entity outright --------------------- */
    Case("item_bypass_skips_entity");
    ResetWorld(1); AddItem("item_medpak_instant",IT_HEALTH,20,0,0);
    forecastItemsPass=1;
    VectorSet(a,0,0,0); VectorSet(b,10,0,0);
    CHECK(Inert(a,b)==qfalse);
    CHECK(shadow.examined==0);                      /* not examined at all */

    /* ---- a flag is not bypassed --------------------------------------- */
    Case("powerup_not_bypassed");
    ResetWorld(1); AddItem("team_CTF_redflag",IT_POWERUP,20,0,0);
    forecastItemsPass=1;
    VectorSet(a,0,0,0); VectorSet(b,10,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(shadow.examined==1);

    /* ---- tunnelling: the sweep crosses a thin trigger the endpoint is past
       Native tests once, after the whole Pmove, so it misses this. The shipped
       swept test is SAFER than the game here - not a false positive. */
    Case("tunnelled_thin_trigger");
    ResetWorld(1); AddTrigger("trigger_push",100,-40,-40,104,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,300,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(ContactsWith(BOT2_SHADOW_SWEPT)==1);
    CHECK(ContactsWith(BOT2_SHADOW_ENDPOINT)==0);
    CHECK(FlagsFor(1)==(BOT2_SHADOW_OLD|BOT2_SHADOW_AABB|BOT2_SHADOW_SWEPT));

    /* ---- first contact wins, and later entities are still censused ----- */
    Case("first_contact_is_the_answer");
    ResetWorld(1);
    AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,1);
    AddTrigger("trigger_push",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(forecastTouched==&g_entities[1]);         /* the first, not the last */
    CHECK(shadow.examined==2);                      /* both still counted */
    CHECK(ContactsWith(BOT2_SHADOW_OLD)==2);

    /* ---- a contact on the last entity only ----------------------------- */
    Case("contact_on_last_entity");
    ResetWorld(1);
    AddTrigger("far",900,900,900,1000,1000,1000,1);
    AddTrigger("near",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(forecastTouched==&g_entities[2]);
    CHECK(shadow.examined==2);

    /* ---- non-trigger and unused entities are skipped by both ----------- */
    Case("skips_non_triggers");
    ResetWorld(1);
    AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,1);
    g_entities[1].r.contents=0;
    AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,1);
    g_entities[2].inuse=0;
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    CHECK(Inert(a,b)==qfalse);
    CHECK(shadow.examined==0); CHECK(CensusTotal()==0);

    /* ---- the census stays complete when the record store overflows ----- */
    Case("record_overflow_is_reported");
    ResetWorld(1);
    {
        int i,want=BOT2_SHADOW_MAX_RECORDS+5;
        /* More disagreeing entities than the store holds. Each is a diagonal
           near miss: box overlap, no brush. */
        if(want>MAX_GENTITIES-1) want=MAX_GENTITIES-1;
        for(i=0;i<want;i++){
            gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
            e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
        }
        VectorSet(a,0,0,0); VectorSet(b,10,10,0);
        CHECK(Inert(a,b)==qtrue);
        CHECK(shadow.examined==want);
        CHECK(CensusTotal()==want);
        /* Whatever the bound, every disagreement is either stored or counted. */
        CHECK(shadow.records+shadow.dropped==want);
    }

    /* ---- the call boundary and hull are carried into the record -------- */
    Case("record_carries_boundary_and_hull");
    ResetWorld(0);
    AddTrigger("trigger_hurt",-40,-40,20,40,40,36,1);
    shadow.boundary=1; shadow.frame=7; shadow.ducked=1; shadow.commandTime=4321;
    VectorSet(a,0,0,0); VectorSet(b,4,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(shadow.records==1);
    CHECK(shadow.record[0].boundary==1);
    CHECK(shadow.record[0].frame==7);
    CHECK(shadow.record[0].ducked==1);
    CHECK(shadow.record[0].commandTime==4321);
    CHECK(shadow.record[0].hullMaxs[2]==16);
    CHECK(shadow.record[0].dead==0);
    CHECK(strcmp(shadow.record[0].classname,"trigger_hurt")==0);
    CHECK(shadow.calls[1]==1);

    /* ---- the per-trigger census splits the pooled count ---------------- */
    Case("per_trigger_census");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    AddTrigger("trigger_push",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    CHECK(Inert(a,b)==qtrue);
    /* Entity 1 disagrees - box overlap, no brush. Entity 2 agrees outright. */
    CHECK(shadow.perEntity[1][BOT2_SHADOW_OLD|BOT2_SHADOW_AABB]==1);
    CHECK(shadow.perEntity[2][BOT2_SHADOW_OLD|BOT2_SHADOW_AABB|BOT2_SHADOW_SWEPT|
                              BOT2_SHADOW_ENDPOINT|BOT2_SHADOW_PROCESSED]==1);

    /* ---- pooled examinations against distinct rollouts ----------------- */
    Case("distinct_rollout_counting");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    shadow.on=1;
    /* Three frames of one rollout: three pooled examinations, one rollout. */
    shadow.rolloutFlagged=0;
    TouchesTrigger(a,b); TouchesTrigger(a,b); TouchesTrigger(a,b);
    CHECK(shadow.examined==3);
    CHECK(shadow.rolloutsWithDisagreement==1);
    shadow.rolloutFlagged=0;          /* a second rollout begins */
    TouchesTrigger(a,b);
    CHECK(shadow.examined==4);
    CHECK(shadow.rolloutsWithDisagreement==2);

    /* ---- records are capped per kind, and the census still totals ------ */
    Case("per_kind_record_cap");
    ResetWorld(1);
    {
        int i,cap=BOT2_SHADOW_MAX_RECORDS/BOT2_SHADOW_STATES,calls=cap+7;
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
        VectorSet(a,0,0,0); VectorSet(b,10,10,0);
        shadow.on=1;
        for(i=0;i<calls;i++){shadow.rolloutFlagged=0;TouchesTrigger(a,b);}
        CHECK(shadow.examined==calls);
        CHECK(CensusTotal()==calls);           /* the census never drops a row */
        CHECK(shadow.records==cap);            /* the store stops at the cap */
        CHECK(shadow.dropped==calls-cap);      /* and says how many it refused */
    }

    /* ---- a refusal no native rung reaches is recoverable --------------- */
    Case("refusal_recoverable");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(shadow.refusals==1);
    CHECK(shadow.refusalsRecoverable==1);
    CHECK(shadow.refusalsNativeProcessed==0);
    CHECK(shadow.refusalsNativeEndpoint==0);
    CHECK(shadow.rolloutsRefused==1);
    CHECK(shadow.rolloutsRefusedRecoverable==1);

    /* ---- a second trigger that DOES dispatch keeps the refusal standing -
       The disagreeing entity is no help when another one in the same call
       would genuinely be touched. This is why recoverability is per call. */
    Case("refusal_not_recoverable_when_another_dispatches");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    AddTrigger("trigger_push",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(shadow.refusals==1);
    CHECK(shadow.refusalsRecoverable==0);
    CHECK(shadow.refusalsNativeProcessed==1);
    CHECK(shadow.rolloutsRefusedRecoverable==0);

    /* ---- a trigger with no touch function makes a refusal recoverable --
       Native reaches the brush but dispatches nothing, so the contact has no
       effect the forecast needs to protect against. */
    Case("no_touch_refusal_is_recoverable");
    ResetWorld(1); AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,0);
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    CHECK(Inert(a,b)==qtrue);
    CHECK(shadow.refusalsNativeEndpoint==1);   /* the brush is genuinely met */
    CHECK(shadow.refusalsNativeProcessed==0);  /* but nothing is dispatched */
    CHECK(shadow.refusalsRecoverable==1);

    /* ---- no refusal, no tally ------------------------------------------ */
    Case("clear_call_tallies_nothing");
    ResetWorld(1); AddTrigger("trigger_hurt",900,900,900,1000,1000,1000,1);
    VectorSet(a,0,0,0); VectorSet(b,32,0,0);
    CHECK(Inert(a,b)==qfalse);
    CHECK(shadow.refusals==0);
    CHECK(shadow.rolloutsRefused==0);

    /* ---- the diagnostic issues no contact query when it is off --------- */
    Case("off_costs_no_queries");
    ResetWorld(1); AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    shadow.on=0; contactCalls=0; TouchesTrigger(a,b);
    CHECK(contactCalls==0);
    shadow.on=1; TouchesTrigger(a,b);
    CHECK(contactCalls>0);

    /* ---- a tag names the candidate, and is consumed exactly once -------
       The consume step lives in Bot2_ForecastRun, which is not extracted, so
       it is reproduced here exactly as the production source spells it. */
    Case("tag_is_consumed_once");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    shadow.on=1;
    Bot2_ForecastShadowTag(7,42);
    CHECK(shadow.tagPlan==7); CHECK(shadow.tagSeq==42);
    CONSUME_TAG();                       /* the tagged rollout begins */
    CHECK(shadow.runPlan==7); CHECK(shadow.runSeq==42);
    CHECK(shadow.tagSeq==-1);            /* and the tag is spent */
    shadow.rolloutFlagged=0; TouchesTrigger(a,b);
    CHECK(shadow.records==1);
    CHECK(shadow.record[0].plan==7);
    CHECK(shadow.record[0].seq==42);
    CHECK(shadow.taggedRollouts==1); CHECK(shadow.untaggedRollouts==0);

    /* ---- an untagged rollout does not inherit the last candidate ------- */
    Case("tag_does_not_leak");
    CONSUME_TAG();                       /* a walk, permit or parity rollout */
    CHECK(shadow.runPlan==-1); CHECK(shadow.runSeq==-1);
    shadow.rolloutFlagged=0; TouchesTrigger(a,b);
    CHECK(shadow.records==2);
    CHECK(shadow.record[1].plan==-1);
    CHECK(shadow.record[1].seq==-1);
    CHECK(shadow.taggedRollouts==1); CHECK(shadow.untaggedRollouts==1);

    /* ---- a zeroed census reads as untagged, not as candidate zero ------ */
    Case("zeroed_tag_is_untagged");
    ResetWorld(1); shadow.on=1;
    CHECK(shadow.tagSeq==0);             /* exactly what memset leaves */
    CONSUME_TAG();
    CHECK(shadow.runPlan==-1); CHECK(shadow.runSeq==-1);
    CHECK(shadow.untaggedRollouts==1);

    /* ---- a recoverable refusal the search rescues is not a repair's ----
       StraightArrives turns a trigger refusal at the objective into an
       arrival. Those candidates are already recovered, so a contact-test
       repair would hand back nothing new for them. */
    Case("rescued_recoverable_is_not_the_repair_s");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    shadow.on=1;
    CONSUME_TAG(); TouchesTrigger(a,b);
    CHECK(shadow.rolloutRecoverable==1);
    Bot2_ForecastShadowRescue(1);                 /* the search recovered it */
    CHECK(shadow.judged==1);
    CHECK(shadow.judgedRecoverable==1);
    CHECK(shadow.judgedRecoverableRescued==1);
    CHECK(shadow.judgedRecoverableStood==0);

    /* ---- the verdict is filed under the trigger that caused the refusal */
    CHECK(shadow.judgedRescuedCell[BOT2_SHADOW_OLD|BOT2_SHADOW_AABB]==1);
    CHECK(shadow.rolloutDecidingFlags==(BOT2_SHADOW_OLD|BOT2_SHADOW_AABB));

    /* ---- one the search did not rescue is the repair's upper bound ----- */
    Case("unrescued_recoverable_stands");
    CONSUME_TAG(); TouchesTrigger(a,b);
    CHECK(shadow.rolloutRecoverable==1);
    Bot2_ForecastShadowRescue(0);
    CHECK(shadow.judgedRecoverable==2);
    CHECK(shadow.judgedRecoverableRescued==1);
    CHECK(shadow.judgedRecoverableStood==1);
    CHECK(shadow.judgedStoodCell[BOT2_SHADOW_OLD|BOT2_SHADOW_AABB]==1);

    /* ---- the deciding trigger is the first old-contact, not the last ---
       A refusal caused by one trigger must not be filed under another that
       the same call happened to examine afterwards. */
    Case("verdict_names_the_deciding_trigger");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("decider",20,20,-40,200,200,40,1);   /* entity 1 */
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    AddTrigger("later",-60,-60,-60,60,60,60,0);                       /* entity 2 */
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    shadow.on=1;
    CONSUME_TAG(); TouchesTrigger(a,b);
    CHECK(forecastTouched==&g_entities[1]);
    /* Entity 1 is box-not-brush; entity 2 is contacted but dispatches nothing.
       Neither reaches `processed`, so the refusal is recoverable and is filed
       under entity 1's rungs, not entity 2's. */
    CHECK(shadow.rolloutRecoverable==1);
    CHECK(shadow.rolloutDecidingFlags==(BOT2_SHADOW_OLD|BOT2_SHADOW_AABB));
    Bot2_ForecastShadowRescue(0);
    CHECK(shadow.judgedStoodCell[BOT2_SHADOW_OLD|BOT2_SHADOW_AABB]==1);

    /* ---- a refusal native would dispatch is counted but never split ---- */
    Case("real_refusal_is_not_recoverable");
    ResetWorld(1); AddTrigger("trigger_hurt",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,8,0,0);
    shadow.on=1;
    CONSUME_TAG(); TouchesTrigger(a,b);
    CHECK(shadow.rolloutRecoverable==0);
    Bot2_ForecastShadowRescue(0);
    CHECK(shadow.judged==1);
    CHECK(shadow.judgedRecoverable==0);

    /* ---- the flag is cleared per rollout, not carried forward ---------- */
    Case("recoverable_flag_is_per_rollout");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    AddTrigger("far",900,900,900,1000,1000,1000,1);
    shadow.on=1;
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    CONSUME_TAG(); TouchesTrigger(a,b);
    CHECK(shadow.rolloutRecoverable==1);
    CONSUME_TAG();                                 /* a fresh rollout */
    CHECK(shadow.rolloutRecoverable==0);
    VectorSet(a,500,500,500); VectorSet(b,505,500,500);   /* touches nothing */
    TouchesTrigger(a,b);
    /* Reporting on this rollout must tally nothing: the recoverable refusal
       belonged to the previous one and is not carried into this verdict. */
    Bot2_ForecastShadowRescue(0);
    CHECK(shadow.judged==1);
    CHECK(shadow.judgedRecoverable==0);

    /* ---- the reporter is silent while the diagnostic is off ------------ */
    Case("rescue_reporter_off_is_silent");
    ResetWorld(1); shadow.on=0;
    Bot2_ForecastShadowRescue(1);
    CHECK(shadow.judged==0);

    /* ---- the report writes a whole census to its file ------------------ */
    Case("report_writes_full_census");
    ResetWorld(1);
    {
        gentity_t *e=AddTrigger("trigger_hurt",20,20,-40,200,200,40,1);
        e->brushed=1; VectorSet(e->brushMins,120,120,-40); VectorSet(e->brushMaxs,200,200,40);
    }
    AddTrigger("trigger_push",-40,-40,-40,40,40,40,1);
    VectorSet(a,0,0,0); VectorSet(b,10,10,0);
    CHECK(Inert(a,b)==qtrue);
    Bot2_ForecastShadowReport("shadow.txt",qfalse);
    CHECK(reportLength>0);
    CHECK(strstr(reportBuffer,"[SHADOW] enabled=1")!=NULL);
    CHECK(strstr(reportBuffer,"trigger entity=1 class=trigger_hurt")!=NULL);
    CHECK(strstr(reportBuffer,"trigger entity=2 class=trigger_push")!=NULL);
    CHECK(strstr(reportBuffer,"recoverable=0")!=NULL);   /* entity 2 dispatches */
    CHECK(strstr(reportBuffer,"[SHADOW] END")!=NULL);
    /* The census is not cleared unless asked. */
    CHECK(shadow.examined==2);
    Bot2_ForecastShadowReport("shadow.txt",qtrue);
    CHECK(shadow.examined==0);
    CHECK(shadow.on==1);                                  /* but stays enabled */

    printf("cases=%d failures=%d\n",cases,failures);
    return failures?1:0;
}
'''


def slice_between(source, start_marker, end_marker):
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, source.index('{', start) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC') or 'gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]

    forecast_bytes = (root / 'codemp/game/bot2_forecast.c').read_bytes()
    forecast = forecast_bytes.decode('utf-8').replace('\r\n', '\n')
    misc_bytes = (root / 'codemp/game/bg_misc.c').read_bytes()
    misc = misc_bytes.decode('utf-8').replace('\r\n', '\n')

    block = slice_between(forecast, '#define BOT2_SHADOW_OLD',
                          '// One line per trigger refusal')
    if 'static qboolean TouchesTrigger' not in block:
        raise AssertionError('Extracted block does not contain the shipped test')
    # The per-rollout preamble lives in Bot2_ForecastRun, which is not part of
    # the extracted block. Take its exact text rather than restating it, so the
    # tag-consume fixtures cannot drift from what the forecast really does.
    end = 'shadow.tagPlan = shadow.tagSeq = -1;'
    consume = slice_between(forecast, '        if (shadow.on) {\n            shadow.rollouts++;',
                            end) + end
    if consume.count('{') != consume.count('}'):
        raise AssertionError('Extracted preamble is not brace-balanced')
    block += '\nstatic void ConsumeTag(void) {\n' + consume + '\n}\n'
    # The report prints; the stub trap swallows it. forecastTouched lives just
    # above the block in production, so it is declared with the extraction.
    block = 'static gentity_t *forecastTouched;\n' + block
    touches_item = extract_function(misc, 'qboolean\tBG_PlayerTouchesItem(')

    # Negative control: the swept rung is made to ask the endpoint's question.
    # The diagonal-miss and tunnelling fixtures must then fail.
    wrong_marker = 'swept = ShadowBrushContact(other, smins, smaxs);'
    if block.count(wrong_marker) != 1:
        raise AssertionError('Expected exactly one swept query to mutate')
    negative = block.replace(wrong_marker,
                             'swept = ShadowBrushContact(other, bmins, bmaxs);', 1)

    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = root / '.codex-research' / f'trigger-shadow-test-{stamp}-{uuid.uuid4().hex[:8]}'
    output.mkdir(parents=True, exist_ok=False)
    (output / 'bot2_forecast.c').write_bytes(forecast_bytes)
    (output / 'extracted-block.c').write_text(block, encoding='utf-8')
    (output / 'negative-source.c').write_text(negative, encoding='utf-8')

    records = []
    for name, body, success in [('production', block, True),
                                ('swept-asks-endpoint', negative, False)]:
        cfile = output / (name + '.c')
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        cfile.write_text(PRELUDE + touches_item + body + MAIN, encoding='utf-8')
        built = subprocess.run([args.cc, '-std=c99', '-O0', '-w', str(cfile), '-o', str(exe)],
                               capture_output=True, text=True)
        (output / (name + '.compile.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode:
            raise RuntimeError(f'Compile failed: {output / (name + ".compile.log")}')
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        (output / (name + '.run.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
        records.append(dict(name=name, expected_success=success,
                            exit_code=run.returncode, output=run.stdout.strip()))
        if (run.returncode == 0) != success:
            raise AssertionError(f'Unexpected {name} result: {output}\n{run.stdout}{run.stderr}')
        # Collapsing swept onto the endpoint destroys exactly the tunnelling
        # distinction: the sweep crosses the trigger, the endpoint is past it.
        if not success and 'case=tunnelled_thin_trigger' not in run.stdout:
            raise AssertionError(f'Negative control failed for the wrong reason:\n{run.stdout}')

    passed = records[0]['output']
    (output / 'result.json').write_text(json.dumps(dict(
        forecast_source_sha256=hashlib.sha256(forecast_bytes).hexdigest(),
        bg_misc_sha256=hashlib.sha256(misc_bytes).hexdigest(),
        extracted_block_sha256=hashlib.sha256(block.encode('utf-8')).hexdigest(),
        negative_source_sha256=hashlib.sha256(negative.encode('utf-8')).hexdigest(),
        negative_mutation='swept rung queries the endpoint box instead of the swept box',
        compiler=args.cc,
        scope='Production shadow block and TouchesTrigger with real BG_PlayerTouchesItem; '
              'stub EntityContact geometry, not native physics',
        tests=records), indent=2) + '\n', encoding='utf-8')
    print(f'PASS trigger shadow diagnostic ({passed}); artifacts: {output}')


class ScriptTest(unittest.TestCase):
    """The script above as a suite case: it extracts production code, compiles it with stubs and runs it."""

    @unittest.skipUnless(shutil.which(os.environ.get('CC') or 'gcc') or shutil.which('clang'), 'no C compiler')
    def test_script(self):
        run = subprocess.run([sys.executable, __file__], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    main()
