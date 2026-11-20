// Shared aim kernel: ballistics from a perceived target, and a humanized view.
//
// The ballistics began as the classic AI's Bot2_GetLeadOrigin (since removed): the same
// projectile speeds, an iterated lead, a floor-clamped fall for an airborne
// target and a splash weapon aimed at the floor. Two things differ: the target
// is the caller's perceived state, never the entity, and a gravity projectile
// (repeater alt, flechette alt) is solved as an arc for its launch angle rather
// than raised by a hand-set constant. Projectile facts are g_weapon.c's:
//   bryar 1600, blaster 2300, bowcaster 1300, repeater 1600 (alt 1100 under
//   gravity plus 40 u/s upward, bursts on impact), demp2 1800, flechette 3500 (alt: two balls at
//   700-1400 under gravity, launched 8-12 degrees above the view, bouncing
//   off the world and bursting on touching a player or after 1.5-3.5 s, so
//   they are aimed at the body, not the floor), rocket 900, concussion 3000;
//   disruptor is hitscan.
// Gravity is DEFAULT_GRAVITY (800) for TR_GRAVITY missiles (bg_misc.c).
//
// The humanized view follows the desired angles with a first-order lag and a
// turn-rate cap and carries a slowly varying error (an Ornstein-Uhlenbeck
// process per axis). The error's randomness is a hash of client, level time
// and axis, not the game's generator, so a pinned world replays exactly and
// the aim draws nothing from the stream weapons and items use.
#include "g_local.h"
#include "bot2_aim.h"

#define AIM_GRAVITY ((float)DEFAULT_GRAVITY)
#define AIM_MAX_TOF 2.5f
#define AIM_SPLASH_FLOOR 64.0f      // a target this close to the floor at arrival takes a floor burst

typedef struct {float speed,gravity,pitchUp,boost;int splash,hitscan;} aimModel_t;   // boost: upward u/s added at launch

static int Model(int weapon,int mode,aimModel_t *m) {
    memset(m,0,sizeof(*m));
    switch(weapon) {
    case WP_BRYAR_PISTOL: case WP_BRYAR_OLD: m->speed=1600;return 1;
    case WP_BLASTER: m->speed=2300;return 1;
    case WP_BOWCASTER: m->speed=1300;return 1;
    case WP_REPEATER:
        // The alt gets 40 u/s upward on top of its launch (WP_RepeaterAltFire).
        if(mode==BOT2_AIM_ALT) {m->speed=1100;m->gravity=AIM_GRAVITY;m->boost=40;m->splash=1;} else m->speed=1600;
        return 1;
    case WP_DEMP2: m->speed=1800;return 1;
    case WP_FLECHETTE:
        // The alt's balls fly at a uniform 700-1400 (mean 1050), 8-12 degrees
        // (mean 10) above the view. They bounce off the floor (FL_BOUNCE_HALF,
        // G_MissileImpact) and burst only on a player or their timer, so no
        // floor burst: the arc goes to the body (humans aim them 2-4 degrees
        // above the centre, records/2026-09-28-human-aim-study.md).
        if(mode==BOT2_AIM_ALT) {m->speed=1050;m->gravity=AIM_GRAVITY;m->pitchUp=10;} else m->speed=3500;
        return 1;
    case WP_ROCKET_LAUNCHER: m->speed=900;m->splash=1;return 1;
    case WP_CONCUSSION:
        if(mode==BOT2_AIM_ALT) m->hitscan=1; else {m->speed=3000;m->splash=1;}
        return 1;
    case WP_DISRUPTOR: m->hitscan=1;return 1;
    default: return 0;      // saber, melee, thermal, mines: not aimed here
    }
}

// The target's hull origin t seconds ahead: straight at its velocity, falling
// under gravity while airborne, never below the floor under it. Returns 1 when
// it is on the floor then (grounded now, or landed before t).
static int Predict(const bot2_aimTarget_t *t,float s,vec3_t out) {
    VectorMA(t->origin,s,t->velocity,out);
    if(!t->grounded) {
        vec3_t top,bottom;trace_t tr;
        out[2]-=.5f*AIM_GRAVITY*s*s;
        VectorCopy(out,top);top[2]=max(out[2],t->origin[2]);
        VectorCopy(out,bottom);bottom[2]-=1;
        trap->Trace(&tr,top,t->mins,t->maxs,bottom,t->skip,MASK_SOLID,qfalse,0,0);
        if(tr.fraction<1 && !tr.startsolid) {VectorCopy(tr.endpos,out);return 1;}
        return 0;
    }
    out[2]=t->origin[2];
    return 1;
}

// A gravity arc's low solution from eye to point at speed; 0 when out of reach.
static int Arc(const vec3_t eye,const vec3_t point,float speed,float g,float *theta,float *tof) {
    float dx=sqrtf((point[0]-eye[0])*(point[0]-eye[0])+(point[1]-eye[1])*(point[1]-eye[1])),dz=point[2]-eye[2];
    float s2=speed*speed,disc=s2*s2-g*(g*dx*dx+2*dz*s2);
    if(dx<1) {*theta=dz>=0?M_PI/2:-M_PI/2;*tof=fabsf(dz)/speed;return 1;}
    if(disc<0) {*theta=M_PI/4;*tof=dx/(speed*cosf(*theta));return 0;}
    *theta=atanf((s2-sqrtf(disc))/(g*dx));
    *tof=dx/(speed*cosf(*theta));
    return 1;
}

static int Solve(const vec3_t eye,int weapon,int mode,const bot2_aimTarget_t *target,bot2_aimSolution_t *out,int floor) {
    aimModel_t m;vec3_t at,dir;float centre=(target->mins[2]+target->maxs[2])*.5f,tof=0,theta=0;int i,reach=1,floored=0;
    memset(out,0,sizeof(*out));
    if(!Model(weapon,mode,&m))return 0;
    if(m.hitscan) {
        VectorCopy(target->origin,out->point);out->point[2]+=centre;
        VectorSubtract(out->point,eye,dir);vectoangles(dir,out->angles);
        out->flags=BOT2_AIM_OK|BOT2_AIM_HITSCAN|BOT2_AIM_SYNC;return out->flags;
    }
    VectorCopy(target->origin,at);
    for(i=0;i<3;i++) {
        vec3_t point;
        VectorCopy(at,point);point[2]+=centre;
        point[2]-=m.boost*tof;   // the boost lifts the shot boost x tof by arrival
        if(m.gravity>0)reach=Arc(eye,point,m.speed,m.gravity,&theta,&tof);
        else tof=Distance(eye,point)/m.speed;
        if(tof>AIM_MAX_TOF)tof=AIM_MAX_TOF;
        floored=Predict(target,tof,at);
    }
    if(floored)out->flags|=BOT2_AIM_SYNC;
    VectorCopy(at,out->point);out->point[2]+=centre;
    if(m.splash && floor) {
        // A target on or near the floor at arrival takes the burst at its feet.
        vec3_t below;trace_t tr;
        VectorCopy(at,below);below[2]+=target->mins[2]-AIM_SPLASH_FLOOR;
        trap->Trace(&tr,at,NULL,NULL,below,target->skip,MASK_SOLID,qfalse,0,0);
        if(tr.fraction<1 && !tr.startsolid) {VectorCopy(tr.endpos,out->point);out->point[2]+=2;out->flags|=BOT2_AIM_SPLASH;}
    }
    if(m.gravity>0) {
        vec3_t aimed;int j;
        for(j=0;j<2;j++) {VectorCopy(out->point,aimed);aimed[2]-=m.boost*tof;reach=Arc(eye,aimed,m.speed,m.gravity,&theta,&tof);}
        out->angles[YAW]=atan2f(out->point[1]-eye[1],out->point[0]-eye[0])*180/M_PI;
        out->angles[PITCH]=AngleNormalize180(-(theta*180/M_PI-m.pitchUp));
        if(out->angles[PITCH]>89)out->angles[PITCH]=89;else if(out->angles[PITCH]<-89)out->angles[PITCH]=-89;   // a steep arc's pitch-up past the vertical
        out->flags|=BOT2_AIM_LOB|(reach?0:BOT2_AIM_RANGE);
    } else {
        tof=Distance(eye,out->point)/m.speed;
        VectorSubtract(out->point,eye,dir);vectoangles(dir,out->angles);
    }
    out->angles[ROLL]=0;out->tof=tof;out->flags|=BOT2_AIM_OK;
    return out->flags;
}

// ---------------------------------------------------------------------------
// The flechette alt: two balls whose speed (700-1400), lift (8-12 degrees)
// and sideways spread (+-2 degrees) are random and which burst only on a
// player, so the aim that hits is the one most rolls reach, not the mean
// ball's arc. The golan hit-envelope lesson (cg_aimfeedback.c,
// docs/golan_fixed_frame_lesson.md) sampled the rolls against a recorded
// path; here the path is the target's predicted one (Predict).

#define GOLAN_STEP 0.025f
#define GOLAN_MAX_S 1.25f
#define GOLAN_PATH ((int)(GOLAN_MAX_S/GOLAN_STEP)+1)
#define GOLAN_ROLLS 24
#define GOLAN_FINE_ROLLS 40
#define GOLAN_RADIUS 3.0f

typedef struct {float pitch,yaw,speed;} golanRoll_t;

static float Halton(int index,int base) {
    float result=0,fraction=1;
    while(index>0) {fraction/=base;result+=fraction*(index%base);index/=base;}
    return result;
}

// The lesson's rolls: a Halton sequence over the random draws of
// WP_FlechetteAltFire and WP_CreateFlechetteBouncyThing (the 1.5-3.5 s timer
// outlasts GOLAN_MAX_S, so it is not drawn).
static void GolanRoll(int i,golanRoll_t *r) {
    r->pitch=-(8+4*Halton(i+1,2));r->yaw=-2+4*Halton(i+1,3);r->speed=700+700*Halton(i+1,5);
}

static void GolanMuzzle(const vec3_t eye,const vec3_t view,vec3_t muzzle) {
    vec3_t f,r;
    AngleVectors(view,f,r,NULL);
    VectorCopy(eye,muzzle);
    VectorMA(muzzle,WP_MuzzlePoint[WP_FLECHETTE][0],f,muzzle);
    VectorMA(muzzle,WP_MuzzlePoint[WP_FLECHETTE][1],r,muzzle);
    muzzle[2]+=WP_MuzzlePoint[WP_FLECHETTE][2];
}

// Whether the segment a..b (the ball relative to the target) enters the box.
static int SegmentBox(const vec3_t a,const vec3_t b,const vec3_t mins,const vec3_t maxs,float *enter) {
    float in=0,out=1;int k;
    for(k=0;k<3;k++) {
        float d=b[k]-a[k],t1,t2;
        if(fabsf(d)<.0001f) {if(a[k]<mins[k] || a[k]>maxs[k])return 0;continue;}
        t1=(mins[k]-a[k])/d;t2=(maxs[k]-a[k])/d;
        if(t1>t2) {float s=t1;t1=t2;t2=s;}
        if(t1>in)in=t1;
        if(t2<out)out=t2;
        if(in>out)return 0;
    }
    *enter=in;return 1;
}

// The contact time of one roll fired with the view at `view`, or -1.
static float GolanContact(const vec3_t eye,const vec3_t view,const golanRoll_t *roll,const vec3_t path[],int n,
                          const vec3_t mins,const vec3_t maxs) {
    vec3_t dir,f,v,muzzle,prev;int k;
    VectorCopy(view,dir);dir[PITCH]+=roll->pitch;dir[YAW]+=roll->yaw;
    AngleVectors(dir,f,NULL,NULL);VectorScale(f,roll->speed,v);
    GolanMuzzle(eye,view,muzzle);
    VectorSubtract(muzzle,path[0],prev);
    for(k=1;k<n;k++) {
        float s=k*GOLAN_STEP,enter;vec3_t ball,rel;
        VectorMA(muzzle,s,v,ball);ball[2]-=.5f*AIM_GRAVITY*s*s;
        VectorSubtract(ball,path[k],rel);
        if(SegmentBox(prev,rel,mins,maxs,&enter))return (k-1+enter)*GOLAN_STEP;
        VectorCopy(rel,prev);
    }
    return -1;
}

static float GolanChance(const vec3_t eye,const vec3_t view,int rolls,const vec3_t path[],int n,
                         const vec3_t mins,const vec3_t maxs,float *contact) {
    int i,hits=0;float sum=0;
    for(i=0;i<rolls;i++) {
        golanRoll_t r;float c;
        GolanRoll(i,&r);
        if((c=GolanContact(eye,view,&r,path,n,mins,maxs))>=0) {hits++;sum+=c;}
    }
    if(contact)*contact=hits?sum/hits:0;
    return hits/(float)rolls;
}

// Whether the world lets a mean ball fly from the muzzle to the contact.
static int GolanClear(const vec3_t eye,const vec3_t view,float contact,int skip) {
    vec3_t dir,f,v,muzzle,prev,cur,mins={-GOLAN_RADIUS,-GOLAN_RADIUS,-GOLAN_RADIUS},maxs={GOLAN_RADIUS,GOLAN_RADIUS,GOLAN_RADIUS};
    float s;trace_t tr;
    VectorCopy(view,dir);dir[PITCH]-=10;
    AngleVectors(dir,f,NULL,NULL);VectorScale(f,1050,v);
    GolanMuzzle(eye,view,muzzle);VectorCopy(muzzle,prev);
    for(s=GOLAN_STEP;s<contact+GOLAN_STEP;s+=GOLAN_STEP) {
        float t=min(s,contact);
        VectorMA(muzzle,t,v,cur);cur[2]-=.5f*AIM_GRAVITY*t*t;
        trap->Trace(&tr,prev,mins,maxs,cur,skip,MASK_SOLID,qfalse,0,0);
        if(tr.fraction<1 || tr.startsolid)return 0;
        VectorCopy(cur,prev);
    }
    return 1;
}

// Replaces the mean ball's arc in out with the view most rolls reach: a
// 2-degree grid around it, then a 1-degree one around the best cell.
static void GolanRefine(const vec3_t eye,const bot2_aimTarget_t *target,bot2_aimSolution_t *out) {
    vec3_t path[GOLAN_PATH],mins,maxs,best,centre;int k,dp,dy;float bestP=-1,bestD=1e9f,contact=0,c;
    for(k=0;k<GOLAN_PATH;k++)Predict(target,k*GOLAN_STEP,path[k]);
    for(k=0;k<3;k++) {mins[k]=target->mins[k]-GOLAN_RADIUS;maxs[k]=target->maxs[k]+GOLAN_RADIUS;}
    VectorCopy(out->angles,centre);VectorCopy(centre,best);
    for(dp=-10;dp<=10;dp+=2)for(dy=-6;dy<=6;dy+=2) {
        vec3_t view;float p;int d=abs(dp)+abs(dy);
        VectorSet(view,centre[PITCH]+dp,centre[YAW]+dy,0);
        p=GolanChance(eye,view,GOLAN_ROLLS,path,GOLAN_PATH,mins,maxs,NULL);
        if(p>bestP || (p==bestP && d<bestD)) {bestP=p;bestD=d;VectorCopy(view,best);}
    }
    if(bestP<=0) {out->chance1=out->chance=0;return;}
    VectorCopy(best,centre);bestP=-1;
    for(dp=-1;dp<=1;dp++)for(dy=-1;dy<=1;dy++) {
        vec3_t view;float p;
        VectorSet(view,centre[PITCH]+dp,centre[YAW]+dy,0);
        p=GolanChance(eye,view,GOLAN_FINE_ROLLS,path,GOLAN_PATH,mins,maxs,&c);
        if(p>bestP) {bestP=p;VectorCopy(view,best);contact=c;}
    }
    if(!GolanClear(eye,best,contact,target->skip))bestP=0;
    {
        // The rolls are scored against the predicted path, and the runner
        // leaves it: its straight-line forecast is off by a median of 13, 51,
        // 101 and 160 u horizontally at 250, 500, 750 and 1,000 ms (its own
        // play log, golan-20260930-a). The chance is scaled by the chance a
        // Rayleigh error of that median stays within the hull's half-width
        // plus the ball, so a long lob is not taken for a sure hit.
        static const float medians[5]={0,13,51,101,160};
        float at=contact/.25f,median,sigma,reach=target->maxs[0]+GOLAN_RADIUS;int i=(int)at;
        median=i>=4?medians[4]*contact:medians[i]+(medians[i+1]-medians[i])*(at-i);
        sigma=median/1.177f;
        if(sigma>0)bestP*=1-expf(-reach*reach/(2*sigma*sigma));
    }
    out->angles[PITCH]=AngleNormalize180(best[PITCH]);out->angles[YAW]=AngleNormalize180(best[YAW]);
    out->tof=contact;
    k=(int)(contact/GOLAN_STEP);if(k>=GOLAN_PATH)k=GOLAN_PATH-1;
    VectorCopy(path[k],out->point);out->point[2]+=(target->mins[2]+target->maxs[2])*.5f;
    out->chance1=bestP;out->chance=1-(1-bestP)*(1-bestP);
}

int Bot2_AimSolve(const vec3_t eye,int weapon,int mode,const bot2_aimTarget_t *target,bot2_aimSolution_t *out) {
    int flags=Solve(eye,weapon,mode,target,out,1);
    // bot_aimGolan 1 (default 0): the rolls' best view and chance. Against the
    // runner its chances ran 2-4 times its hits and it drew the defenders to
    // long flechette lobs (records/2026-09-29-defender-gaps.md).
    if((flags&BOT2_AIM_OK) && weapon==WP_FLECHETTE && mode==BOT2_AIM_ALT && trap->Cvar_VariableIntegerValue("bot_aimGolan"))
        GolanRefine(eye,target,out);
    return flags;
}

void Bot2_AimAdvance(bot2_aimTarget_t *target,float seconds) {
    vec3_t at;
    if(seconds<=0)return;
    if(Predict(target,seconds,at)) {
        if(!target->grounded)target->velocity[2]=0;
        target->grounded=qtrue;
    } else target->velocity[2]-=AIM_GRAVITY*seconds;
    VectorCopy(at,target->origin);
}

int Bot2_AimSolveDirect(const vec3_t eye,int weapon,int mode,const bot2_aimTarget_t *target,bot2_aimSolution_t *out) {
    return Solve(eye,weapon,mode,target,out,0);
}

// ---------------------------------------------------------------------------
// The human aim point and error (records/2026-09-28-human-aim-study.md).

// Pitch offset humans add, degrees up (negative is down), by mode.
static float HumanPitch(int weapon,int mode) {
    if(weapon==WP_REPEATER && mode==BOT2_AIM_ALT)return 4;
    if(weapon==WP_FLECHETTE && mode==BOT2_AIM_ALT)return 2;
    if(weapon==WP_THERMAL)return 2;
    if(weapon==WP_ROCKET_LAUNCHER)return -2;
    return 0;
}

int Bot2_AimHumanPoint(const vec3_t eye,int weapon,int mode,const bot2_aimTarget_t *target,bot2_aimSolution_t *out) {
    aimModel_t m;vec3_t dir;
    memset(out,0,sizeof(*out));
    if(!Model(weapon,mode,&m) && weapon!=WP_THERMAL)return 0;
    VectorCopy(target->origin,out->point);out->point[2]+=(target->mins[2]+target->maxs[2])*.5f;
    VectorSubtract(out->point,eye,dir);vectoangles(dir,out->angles);
    out->angles[PITCH]=AngleNormalize180(out->angles[PITCH]-HumanPitch(weapon,mode));
    out->angles[ROLL]=0;
    out->tof=m.speed>0?Distance(eye,out->point)/m.speed:0;
    out->flags=BOT2_AIM_OK|(m.hitscan?BOT2_AIM_HITSCAN:0)|(m.gravity>0?BOT2_AIM_LOB:0);
    return out->flags;
}

// Radial error medians at a target angular speed under 30 deg/s, degrees, and
// the growth with angular speed (deg per deg/s).
float Bot2_AimHumanError(int weapon,int mode,float angularSpeed,float scale) {
    float base,k=.03f;
    switch(weapon) {
    case WP_REPEATER: base=mode==BOT2_AIM_ALT?8.7f:4.1f;break;
    case WP_ROCKET_LAUNCHER: base=10.4f;break;
    case WP_BRYAR_PISTOL: case WP_BRYAR_OLD: base=6.4f;break;
    case WP_FLECHETTE: base=mode==BOT2_AIM_ALT?8.8f:3.1f;break;
    case WP_THERMAL: base=9.0f;break;
    case WP_CONCUSSION: base=4.0f;break;
    case WP_DISRUPTOR: base=mode==BOT2_AIM_ALT?.9f:1.8f;k=.04f;break;
    default: base=8.0f;break;
    }
    if(angularSpeed<30)angularSpeed=30;
    return scale*(base+k*(angularSpeed-30))/1.177f;
}

float Bot2_AimSplashRadius(int weapon,int mode) {
    if(weapon==WP_ROCKET_LAUNCHER)return 160;
    if(weapon==WP_CONCUSSION && mode==BOT2_AIM_PRIMARY)return 200;
    if((weapon==WP_REPEATER || weapon==WP_FLECHETTE) && mode==BOT2_AIM_ALT)return 128;
    return 0;
}

float Bot2_AimDamage(int weapon,int mode) {
    if(weapon==WP_ROCKET_LAUNCHER)return 100;
    if(weapon==WP_CONCUSSION)return mode==BOT2_AIM_PRIMARY?75:25;
    if(weapon==WP_REPEATER)return mode==BOT2_AIM_ALT?60:14;
    if(weapon==WP_FLECHETTE)return mode==BOT2_AIM_ALT?120:12;   // alt: two balls
    return 20;
}

// Hit chance by reach ratio bands 0-0.5, 0.5-1, 1-1.5, 1.5-2.5, 2.5+:
// competent style, human error, fire at will, Kejim returns and pickups at
// classes 50 and 150.
float Bot2_AimHitChance(int weapon,int mode,float ratio) {
    static const float rocket[5]={.77f,.31f,.05f,0,.01f},repalt[5]={.83f,.28f,.21f,.17f,.01f},
        conc[5]={.62f,.31f,.35f,.12f,.25f},flechalt[5]={.04f,.04f,.03f,.02f,0};
    const float *t=NULL;int b=ratio<.5f?0:ratio<1?1:ratio<1.5f?2:ratio<2.5f?3:4;
    if(weapon==WP_ROCKET_LAUNCHER)t=rocket;
    else if(weapon==WP_CONCUSSION && mode==BOT2_AIM_PRIMARY)t=conc;
    else if(weapon==WP_REPEATER && mode==BOT2_AIM_ALT)t=repalt;
    else if(weapon==WP_FLECHETTE && mode==BOT2_AIM_ALT)t=flechalt;
    return t?t[b]:0;
}

// ---------------------------------------------------------------------------
// The humanized view.

static struct {int time,fresh;vec3_t view,desired;float err[2];} aimView[MAX_CLIENTS];

// A uniform in (0,1) from client, time and a stream index: splitmix64.
static double Uniform(int client,int time,int stream) {
    unsigned long long z=((unsigned long long)(unsigned)client<<40)^((unsigned long long)(unsigned)time<<8)^(unsigned)stream;
    z+=0x9E3779B97F4A7C15ULL;z=(z^(z>>30))*0xBF58476D1CE4E5B9ULL;z=(z^(z>>27))*0x94D049BB133111EBULL;z^=z>>31;
    return ((z>>11)+.5)/9007199254740992.0;
}

static float Normal(int client,int time,int axis) {
    double u1=Uniform(client,time,2*axis),u2=Uniform(client,time,2*axis+1);
    return (float)(sqrt(-2*log(u1))*cos(2*M_PI*u2));
}

void Bot2_AimHumanReset(int client,const vec3_t viewAngles) {
    if(client<0 || client>=MAX_CLIENTS)return;
    aimView[client].time=level.time;VectorCopy(viewAngles,aimView[client].view);
    aimView[client].err[0]=aimView[client].err[1]=0;aimView[client].fresh=1;
}

float Bot2_AimHumanStep(int client,const vec3_t desired,const bot2_aimHuman_t *h,vec3_t outAngles) {
    float dt,follow,cap,left=0,decay;int axis,axes[2]={PITCH,YAW};
    if(client<0 || client>=MAX_CLIENTS)return 180;
    dt=(level.time-aimView[client].time)/1000.0f;
    if(dt<0 || dt>.1f)dt=dt<0?0:.1f;
    aimView[client].time=level.time;
    follow=h->lagMs>0?1-expf(-dt*1000/h->lagMs):1;
    cap=h->turnRate>0?h->turnRate*dt:360;
    decay=h->errorMs>0?expf(-dt*1000/h->errorMs):0;
    for(axis=0;axis<2;axis++) {
        // Pursuit: the desired angle's own change since the last step, then
        // the lagged share of the gap that remains.
        int a=axes[axis];float pursuit=aimView[client].fresh?0:AngleDelta(desired[a],aimView[client].desired[a]);
        float delta=AngleDelta(desired[a],AngleNormalize180(aimView[client].view[a]+pursuit)),move=pursuit+delta*follow;
        if(move>cap)move=cap;else if(move<-cap)move=-cap;
        aimView[client].view[a]=AngleNormalize180(aimView[client].view[a]+move);
        left=max(left,fabsf(AngleDelta(desired[a],aimView[client].view[a])));
        aimView[client].err[axis]=aimView[client].err[axis]*decay+
            h->errorDeg*sqrtf(max(0.0f,1-decay*decay))*Normal(client,level.time,axis);
        outAngles[a]=AngleNormalize180(aimView[client].view[a]+aimView[client].err[axis]);
    }
    VectorCopy(desired,aimView[client].desired);aimView[client].fresh=0;
    outAngles[ROLL]=0;
    return left;
}
