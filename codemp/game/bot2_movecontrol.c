// Local movement control: immutable intent + actual momentum -> legal command.
// Shared by runtime and forecast. No world queries, navigation, mutable state or
// I/O (the key offsets are a table of constants filled once).
#include "g_local.h"
#include "bot2_movecontrol.h"

// fmodf(x,360), exactly, without the library call for the inputs the
// controller produces. For |x| < 720 the result is x, x-360 or x+360, and each
// of those subtractions is exact in single precision (Sterbenz: 180 <= |x| <=
// 720), so the value is fmodf's to the bit - including the sign of a zero
// result, which fmodf takes from the dividend. Anything else, NaN and the
// infinities among it, goes to fmodf. tools/strafe_movecontrol_check.py checks
// this against fmodf itself and the whole controller against its fmodf form.
static float Mod360(float x) {
    if(x>=0) {
        if(x<360)return x;
        if(x<720)return x-360;
    } else if(x<0) {
        if(x>-360)return x;
        if(x>-720) {float r=x+360;return r==0?-0.0f:r;}
    }
    return fmodf(x,360);
}
static float Wrap(float a) {a=Mod360(a+180);if(a<0)a+=360;return a-180;}

static const int moveKeys[9][2]={{127,0},{127,127},{127,-127},{0,127},{0,-127},{-127,0},{-127,127},{-127,-127},{0,0}};
// The eight keys' yaw offsets, by the expression the scan evaluated on every
// call, computed once. The same float every time, so nothing downstream moves.
static const float *KeyOffsets(void) {
    static float offsets[8];static int ready;int key;
    if(!ready) {
        for(key=0;key<8;key++)offsets[key]=atan2f(-(float)moveKeys[key][1],(float)moveKeys[key][0])*180/M_PI;
        ready=1;
    }
    return offsets;
}

void Bot2_MoveWaypointIntent(const playerState_t *ps, const vec3_t target, float speed, float radius, bot2_moveIntent_t *intent) {
    vec3_t d;float length,scale;
    memset(intent,0,sizeof(*intent));intent->turnRate=360;intent->allowStop=qtrue;
    VectorSubtract(target,ps->origin,d);length=sqrtf(d[0]*d[0]+d[1]*d[1]);
    // A spatial arrival objective: slow before the point and let friction stop
    // inside its radius. Re-evaluated from actual state, never a timed replay.
    scale=min(speed,max(0,(length-radius)*4))/max(1,length);
    intent->velocity[0]=d[0]*scale;intent->velocity[1]=d[1]*scale;
}

static int MoveControl(const playerState_t *ps, const bot2_moveIntent_t *intent,
                    float yaw, float dt, qboolean fast, float wishLimit, usercmd_t *cmd, float *outYaw) {
    float baseWishspeed=ps->basespeed*((ps->fd.forcePowersActive&(1<<FP_SPEED))?1.7f:1.0f);
    float best=1e30f;const float *offsets=KeyOffsets();
    int angle,key,index,evaluations,magnitude=127,ground=ps->groundEntityNum!=ENTITYNUM_NONE;
    // Fast mode admits, per key, only the grid angles within turn+5 degrees of
    // yaw+offset. That set is one contiguous arc, so it is found once by
    // scanning outward from the nearest grid angle with the very same Wrap
    // expression the evaluation uses, instead of testing all 72 angles for
    // every key on every step. The evaluated set, its order and every value
    // computed for it are unchanged; only the rejected angles are skipped
    // before their two Wrap calls. See tools/strafe_movecontrol_check.py for
    // the bitwise equivalence check against the unpruned scan.
    //
    // Everything the scan computes that depends neither on the angle nor on
    // the key - the stop key's view and its facing test, the ground friction
    // applied to the current velocity - is computed once below, by the same
    // expressions in the same order, and every per-key factor once per key. The
    // stop key (8) is evaluated at angle 0 only, as before; it no longer pays
    // two Wraps at each of the other 71 angles on its way to being skipped.
    unsigned char admit[8][72];
    float facing=yaw,travelX=intent->velocity[0],travelY=intent->velocity[1];
    float turn=intent->turnRate*dt,facingLimit,stopView,groundX,groundY,wishspeeds[9];
    int stopFacing,keys=intent->allowStop?9:8,jumping,sideKey=-1;
    cmd->forwardmove=cmd->rightmove=0;cmd->upmove=intent->upmove;*outYaw=yaw;
    if(cmd->upmove)ground=0;
    // A held strafe side, in the air only: that side's forward diagonal, with
    // the view turned toward the angle that adds the most speed. Computed, not
    // searched: every view one frame's turn can reach may add nothing (wish
    // within acos(wishspeed/speed) of travel), so a one-frame score gives no
    // direction to turn in and the view stays where it is. The best wish
    // direction is theta off travel toward the side, cos(theta) =
    // (wishspeed - accel) / speed, straight ahead below that speed.
    if(intent->strafeSide && ps->groundEntityNum==ENTITYNUM_NONE) {
        float v=sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]);
        // bot_moveFixBM8 (default 0): a fixed-step bot's Pmove adds speed in
        // pmove_msec slices, so the gain is per slice (intent->strafeSlice), not per command; 0 keeps dt.
        float slice=intent->strafeSlice>0 && intent->strafeSlice<dt?intent->strafeSlice:dt;
        float accel=baseWishspeed*slice,c=v>1?(baseWishspeed-accel)/v:1,theta,travel,ideal,delta;
        sideKey=intent->strafeSide>0?1:2;
        // A skill k below 1 aims wider, where a command of add a along a
        // wish whose travel component is u gains 2ua + a^2 of v^2: u is set
        // so that is k times the best gain, a(2w - a), or 2va + a^2 below
        // v = w - a.
        if(intent->strafeSkill>0 && intent->strafeSkill<1 && v>1 && accel>0) {
            float gain=intent->strafeSkill*(v>=baseWishspeed-accel?accel*(2*baseWishspeed-accel):2*v*accel+accel*accel);
            c=(gain-accel*accel)/(2*accel*v);
        }
        theta=acosf(max(-1.0f,min(1.0f,c)))*180/M_PI;
        travel=v>1?atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI:yaw;
        ideal=travel+(intent->strafeSide>0?-theta:theta)-offsets[sideKey];
        delta=Wrap(ideal-yaw);
        *outYaw=yaw+max(-turn,min(turn,delta));
        cmd->forwardmove=moveKeys[sideKey][0];cmd->rightmove=moveKeys[sideKey][1];
        cmd->angles[YAW]=ANGLE2SHORT(*outYaw)-ps->delta_angles[YAW];
        cmd->angles[PITCH]=-ps->delta_angles[PITCH];cmd->angles[ROLL]=-ps->delta_angles[ROLL];
        return 1;
    }
    if(ground && wishLimit>0 && wishLimit<baseWishspeed) {
        // A full Speed pulse can overshoot a lower curve limit even when it is
        // the nearest binary command to the target. Native PM_CmdScale accepts
        // analog magnitudes; cap the requested wish speed and model that exact
        // quantized input instead of alternating full throttle and zero.
        magnitude=max(0,min(127,(int)floorf(127*wishLimit/baseWishspeed)));
        baseWishspeed*=magnitude/127.0f;
    }
    evaluations=0;
    // Face roughly along actual travel while moving; at low speed face the
    // requested departure. Backward input remains available for braking, but
    // an equivalent backward strafe must not keep the view facing behind us.
    if(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]>80*80) {
        travelX=ps->velocity[0];travelY=ps->velocity[1];
    }
    if(travelX*travelX+travelY*travelY>1)facing=atan2f(travelY,travelX)*180/M_PI;
    facingLimit=max(60,fabsf(Wrap(yaw-facing))-turn);
    // A saber jump with backward input invokes PM_CheckJump's backflip;
    // lateral inputs can invoke wall acrobatics. Launch an ordinary forward
    // jump, then resume full horizontal steering once airborne.
    jumping=cmd->upmove && ps->groundEntityNum!=ENTITYNUM_NONE;
    stopView=yaw+max(-turn,min(turn,Wrap(facing-yaw)));
    stopFacing=!(fabsf(Wrap(stopView-facing))>facingLimit+.01f);
    groundX=ps->velocity[0];groundY=ps->velocity[1];
    if(ground) {float v=sqrtf(groundX*groundX+groundY*groundY),scale=max(0,v-max(100,v)*6*dt)/max(1,v);groundX*=scale;groundY*=scale;}
    // BG_AdjustClientSpeed resets from basespeed for each new command and
    // slows backward ground commands before jumping. Do not reuse the
    // preceding command's adjusted ps.speed for all candidate key choices.
    for(key=0;key<9;key++)wishspeeds[key]=baseWishspeed*((ps->groundEntityNum!=ENTITYNUM_NONE && moveKeys[key][0]<0)?.75f:1.0f);
    if(fast) {
        float limit=intent->turnRate*dt+5;
        memset(admit,0,sizeof(admit));
        for(key=0;key<8;key++) {
            int centre=(int)floorf(Wrap(yaw+offsets[key])/5+.5f),step,direction;
            centre=((centre%72)+72)%72;
            for(direction=-1;direction<=1;direction+=2) {
                for(step=direction<0?1:0;step<=36;step++) {
                    int at=((centre+direction*step)%72+72)%72;
                    if(fabsf(Wrap((float)(at*5)-offsets[key]-yaw))>limit)break;
                    admit[key][at]=1;
                }
            }
        }
    }
    for(angle=0,index=0;angle<360;angle+=5,index++)for(key=0;key<keys;key++) {
        float offset,delta,view;
        if(jumping && key!=0)continue;
        if(key==8) {
            if(angle!=0 || !stopFacing)continue;
            offset=0;delta=0;view=stopView;
        } else {
            if(fast && !admit[key][index])continue;
            offset=offsets[key];
            delta=Wrap(angle-offset-yaw);view=yaw+max(-intent->turnRate*dt,min(intent->turnRate*dt,delta));
            if(fabsf(Wrap(view-facing))>facingLimit+.01f)continue;
            // Farther grid angles clamp to the same view and have a larger
            // yaw penalty. Only the nearest outside sample can be useful.
            if(fast && fabsf(delta)>intent->turnRate*dt+5)continue;
        }
        evaluations++;
        {
            float wishYaw=(view+offset)*M_PI/180,wx=cosf(wishYaw),wy=sinf(wishYaw);
            float nx=groundX,ny=groundY,add,accel,error,wishspeed=wishspeeds[key];
            add=wishspeed-(nx*wx+ny*wy);accel=key==8?0:max(0,min(add,(ground?10:1)*wishspeed*dt));nx+=accel*wx;ny+=accel*wy;
            error=(nx-intent->velocity[0])*(nx-intent->velocity[0])+(ny-intent->velocity[1])*(ny-intent->velocity[1])+.0001f*delta*delta+.001f*key;
            if(error<best){best=error;*outYaw=view;cmd->forwardmove=moveKeys[key][0]*magnitude/127;cmd->rightmove=moveKeys[key][1]*magnitude/127;}
        }
    }
    cmd->angles[YAW]=ANGLE2SHORT(*outYaw)-ps->delta_angles[YAW];
    cmd->angles[PITCH]=-ps->delta_angles[PITCH];cmd->angles[ROLL]=-ps->delta_angles[ROLL];
    return evaluations;
}

int Bot2_MoveControl(const playerState_t *ps,const bot2_moveIntent_t *intent,
    float yaw,float dt,qboolean fast,usercmd_t *cmd,float *outYaw) {
    return MoveControl(ps,intent,yaw,dt,fast,0,cmd,outYaw);
}

int Bot2_MoveControlLimited(const playerState_t *ps,const bot2_moveIntent_t *intent,
    float yaw,float dt,qboolean fast,float wishLimit,usercmd_t *cmd,float *outYaw) {
    return MoveControl(ps,intent,yaw,dt,fast,wishLimit,cmd,outYaw);
}
