// ==============================================================================
// bot2_kick.c - declared knockback fixture (bot_moveKick), a fixture intervention.
//
// Measures how the movement recovers from being hit - a rocket's push - without
// an opponent: a declared kick is applied to the runner through the game's own
// G_ApplyKnockback, the call G_Damage makes, so the velocity it adds, the
// knockback timer it sets and the g_knockback and mass it reads are native. No
// damage is dealt: health, pain, armor and the AI state are untouched, so what
// is measured is the motion alone.
//
//   bot_moveKick  "trigger knockback angle pitch [delay_ms] [into_ms]", several
//                 separated by ',' and fired in order, each once.
//     trigger     time  - at delay_ms after the release
//                 walk  - grounded, walking with no sequence in progress
//                 hop   - airborne in a committed hop
//                 climb - airborne in a committed climb
//     knockback   G_Damage's knockback: a rocket's direct hit is 100, its
//                 splash falls off with distance. With g_knockback 1000 and the
//                 player's mass, 100 adds 400 u/s along the push.
//     angle       degrees from the bot's travel heading (its view yaw when
//                 slower than 50 u/s): 0 pushes along the travel, 90 to its
//                 left, 180 against it.
//     pitch       degrees above the horizontal. Splash off the floor pushes
//                 up and away; G_RadiusDamage's 24-unit lift gives about 20.
//     delay_ms    the kick is not considered before this long after the
//                 release (default 0).
//     into_ms     for the phase triggers, how long the phase must have run
//                 continuously first (default 0: its first frame).
//
// Armed by bot_corner_start, so a kick is relative to the fixture's release and
// a mid-trip corner start re-arms it. Each kick prints one [KICK] APPLIED line;
// the ARMED lines list what was declared, so an unfired kick is visible. They
// are printed on the first frame after the release, not by the arm: the corner
// start runs under rcon, whose prints go to the reply and never reach the log.
// Inert while the cvar is empty.
// ==============================================================================

#include "g_local.h"
#include "bot2_move.h"
#include "bot2_kick.h"

void G_ApplyKnockback(gentity_t *targ,vec3_t newDir,float knockback);   // g_combat.c

#define KICK_MAX 8

typedef enum {KICK_TIME,KICK_WALK,KICK_HOP,KICK_CLIMB} kickTrigger_t;
static const char *kickNames[]={"time","walk","hop","climb"};

typedef struct {
    int trigger;float knockback,angle,pitch;int delay,into;
} kick_t;

static struct {
    int armed,release,count,next,phaseSince,announced;
    kick_t kick[KICK_MAX];
} kicks[MAX_CLIENTS];

static int KickParse(const char *text,kick_t *k) {
    char name[16];int i,n;
    k->delay=k->into=0;
    n=sscanf(text," %15s %f %f %f %d %d",name,&k->knockback,&k->angle,&k->pitch,&k->delay,&k->into);
    if(n<4)return 0;
    for(i=0;i<(int)ARRAY_LEN(kickNames);i++)if(!Q_stricmp(name,kickNames[i])) {k->trigger=i;return 1;}
    return 0;
}

void Bot2_KickArm(int client,int releaseLevel) {
    char value[MAX_CVAR_VALUE_STRING],*at,*comma;
    if(client<0 || client>=MAX_CLIENTS)return;
    memset(&kicks[client],0,sizeof(kicks[client]));
    trap->Cvar_VariableStringBuffer("bot_moveKick",value,sizeof(value));
    if(!value[0])return;
    for(at=value;at && *at && kicks[client].count<KICK_MAX;at=comma?comma+1:NULL) {
        kick_t *k=&kicks[client].kick[kicks[client].count];
        comma=strchr(at,',');if(comma)*comma=0;
        if(!KickParse(at,k)) {trap->Print("[KICK] bad kick '%s'; want trigger knockback angle pitch [delay_ms] [into_ms]\n",at);continue;}
        kicks[client].count++;
    }
    kicks[client].armed=kicks[client].count>0;kicks[client].release=releaseLevel;kicks[client].phaseSince=-1;
}

// The phase the executor left the bot in after the last command, read against
// the state that command's Pmove produced.
static int KickPhase(int client,const playerState_t *ps,int trigger) {
    const movement_t *m=Move_State(client);
    int ground=ps->groundEntityNum!=ENTITYNUM_NONE,sequence;
    // A dead or respawning body takes no kick, a timed one included.
    if(ps->pm_type!=PM_NORMAL || g_entities[client].health<=0)return 0;
    if(trigger==KICK_TIME)return 1;
    if(!m)return 0;
    sequence=m->plan.count && m->executor.stage<m->plan.count*2;
    switch(trigger) {
    case KICK_WALK:return !sequence && ground && m->executor.walking;
    case KICK_HOP:return sequence && m->plan.kind!=MOVE_PLAN_CLIMB && (m->executor.stage&1) && !ground;
    case KICK_CLIMB:return sequence && m->plan.kind==MOVE_PLAN_CLIMB && m->executor.stage==MOVE_STAGE_FLIGHT0 && !ground;
    }
    return 0;
}

void Bot2_KickFrame(int client) {
    gentity_t *ent;playerState_t *ps;kick_t *k;
    vec3_t dir,before;float heading,speed;const movement_t *m;
    if(client<0 || client>=MAX_CLIENTS || !kicks[client].armed)return;
    ent=&g_entities[client];
    if(!ent->inuse || !ent->client)return;
    if(!kicks[client].announced) {
        int i;
        for(i=0;i<kicks[client].count;i++) {
            k=&kicks[client].kick[i];
            trap->Print("[KICK] ARMED level=%d client=%d index=%d trigger=%s knockback=%.1f angle=%.1f pitch=%.1f delay_ms=%d into_ms=%d\n",
                kicks[client].release,client,i,kickNames[k->trigger],k->knockback,k->angle,k->pitch,k->delay,k->into);
        }
        kicks[client].announced=1;
    }
    ps=&ent->client->ps;k=&kicks[client].kick[kicks[client].next];
    if(level.time-kicks[client].release<k->delay || !KickPhase(client,ps,k->trigger)) {kicks[client].phaseSince=-1;return;}
    if(kicks[client].phaseSince<0)kicks[client].phaseSince=level.time;
    if(k->trigger!=KICK_TIME && level.time-kicks[client].phaseSince<k->into)return;
    speed=sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]);
    heading=speed>=50?atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI:ps->viewangles[YAW];
    heading+=k->angle;
    dir[0]=cosf(DEG2RAD(heading))*cosf(DEG2RAD(k->pitch));
    dir[1]=sinf(DEG2RAD(heading))*cosf(DEG2RAD(k->pitch));
    dir[2]=sinf(DEG2RAD(k->pitch));
    VectorCopy(ps->velocity,before);
    G_ApplyKnockback(ent,dir,k->knockback);
    m=Move_State(client);
    trap->Print("[KICK] APPLIED level=%d rel_ms=%d client=%d index=%d trigger=%s knockback=%.1f angle=%.1f pitch=%.1f "
        "x=%.1f y=%.1f z=%.1f ground=%d plan=%d kind=%d stage=%d "
        "vx=%.1f vy=%.1f vz=%.1f speed=%.1f after_vx=%.1f after_vy=%.1f after_vz=%.1f after_speed=%.1f pm_time=%d\n",
        level.time,level.time-kicks[client].release,client,kicks[client].next,kickNames[k->trigger],k->knockback,k->angle,k->pitch,
        ps->origin[0],ps->origin[1],ps->origin[2],ps->groundEntityNum!=ENTITYNUM_NONE,
        m && m->plan.count?m->plan.id:-1,m?m->plan.kind:-1,m?m->executor.stage:-1,
        before[0],before[1],before[2],speed,ps->velocity[0],ps->velocity[1],ps->velocity[2],
        sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]),ps->pm_time);
    kicks[client].phaseSince=-1;
    if(++kicks[client].next>=kicks[client].count)kicks[client].armed=0;
}
