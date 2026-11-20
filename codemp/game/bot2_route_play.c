// ==============================================================================
// bot2_route_play.c - executing a recorded route tape.
//
// A human route recording is one command per authoritative Pmove call, taken
// with pmove_fixed and an 8 ms step. The physics is a function of that command
// sequence and the state it starts from, so replaying the sequence from the
// recorded start reproduces the run. This module is that replay: it submits the
// tape's commands, at the tape's own time deltas, to one bot.
//
// It is a playback owner, not a follower. It does not steer, correct, or react
// to anything: if the world differs from the recording, the replay diverges and
// says so at the next checkpoint. That is the point - the divergence is the
// measurement. A follower that steers back to the recorded states is a separate
// piece of work, and it needs this one first to know what "the recorded states"
// are worth.
//
// The bot's own AI is idled while a tape is held or plays, as the bench idles
// it, so nothing else writes the client's movement or its movement state.
// ==============================================================================

#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "ai_bot2_internal.h"
#include "bot2_route_play.h"
#include "bot2_move.h"
#include "bot2_movecontrol.h"
#include "bot2_svcmds.h"

extern void ClientThink_real(gentity_t *ent);

#define ROUTEPLAY_MAX_COMMANDS 8192
#define ROUTEPLAY_MAX_MARKS 1024
// A replay that is this far from the recorded position has not reproduced the
// run, whatever it does afterwards. It is reported, never corrected.
#define ROUTEPLAY_DRIFT_REPORT 64.0f
// The start the tape assumes. Beyond this the tape is being played from
// somewhere it was not recorded, and the line it traces is a different line
// through different geometry, so playback refuses rather than producing a
// number that looks like a route time.
#define ROUTEPLAY_START_TOLERANCE 4.0f

// ---------------------------------------------------------------------------
// The follower (bot_routeFollow, default off).
//
// Playback above is open loop: it reproduces a run from the run's own start
// and reacts to nothing. That makes a route an asset only where it was
// recorded, and the benchmark stand is 9 to 393 units from the three
// recordings' starts, so no recorded route is usable from the fixture.
//
// The follower steers the tape back onto its recorded line. What it may not
// do is aim at the line: a strafe run's view yaw is deliberately offset from
// its velocity, and that offset is what accelerates it, so replacing the
// recorded yaw with a bearing to the next mark would throw the run away to
// correct it. It rotates the recorded frame instead - the whole command,
// yaw and keys together - by an angle proportional to how far off the
// recorded line the bot is, so the strafe offset the recording carries is
// preserved and only its heading moves.
//
// The rotation is applied on grounded frames alone. In the air the command
// is the recording's, untouched: air control is what buys a strafe jump its
// speed, and a correction applied in flight changes the jump rather than
// correcting it. A flight's error is set at its takeoff, which is a grounded
// frame, and that is where the follower has its say.
// ---------------------------------------------------------------------------
#define ROUTEFOLLOW_GAIN 0.30f          // degrees of rotation per unit off the line
#define ROUTEFOLLOW_MAX_DEG 15.0f       // and no more than this in one command
#define ROUTEFOLLOW_START 64.0f         // a start this far from the tape's is followable
#define ROUTEFOLLOW_ABORT 192.0f        // beyond this the route is not being followed

static int FollowEnabled(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_routeFollow",value,sizeof(value));
    return value[0] && atoi(value)!=0;
}

// The rotation per unit off the line. Zero arms the follower - the off-start
// placement and the divergence abort - with no steering at all, which is how
// open-loop playback's tolerance to a wrong entry state is measured.
static float FollowGain(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_routeFollowGain",value,sizeof(value));
    return value[0]?(float)atof(value):ROUTEFOLLOW_GAIN;
}

// ---------------------------------------------------------------------------
// The prefix (bot_routePrefix, default off).
//
// The follower measurement said what a route entry costs: open-loop playback
// completes from 28 units of along-track error either way and fails at 16
// units cross-track. Those are distances the movement engine's own planner
// can hit, so the cheaper way to run a recorded route from the benchmark
// stand is to walk there and then play the tape unchanged - no steering, no
// asset format change, and the route time is still the recorded route's.
//
// The prefix is that walk. It owns the frames before the tape starts: it
// gives the movement engine the tape's start as its objective, submits the
// engine's commands, and hands off when the bot is inside the tolerance and
// at rest, because the tape's first commands assume a standing start. The
// walk's own time is reported separately from the tape's, so a prefix run
// never reads as a faster route than the recording.
//
// It corrects nothing after the hand-off. If the bot cannot arrive, the run
// says so and no tape is played: a tape submitted from outside the tolerance
// produces a number for a run that is not the recorded run.
// ---------------------------------------------------------------------------
#define ROUTEPREFIX_ALONG 24.0f         // along the route's first line, inside the measured 28
#define ROUTEPREFIX_CROSS 12.0f         // across it, inside the measured 16 cliff
#define ROUTEPREFIX_VERTICAL 24.0f      // and on the same floor as the recording
#define ROUTEPREFIX_SPEED 40.0f         // the tape starts standing; this is "at rest"
#define ROUTEPREFIX_SETTLE 0            // ms held inside the gate before the tape starts
#define ROUTEPREFIX_TIMEOUT 30000       // a walk that has not arrived by now will not
#define ROUTEPREFIX_REPORT 500          // ms between progress lines
// Inside this distance the prefix steers itself. The planner's business is
// getting to a place; its path ends on a graph cell and its walk stands there
// when it arrives, which the first prefix run measured: the bot stopped 23.1
// units from the tape's start and held that for 28 seconds, because the goal
// is not a node and nothing in the engine closes the last cell. Arriving on a
// point is this module's problem, not the planner's, and solving it here
// leaves the movement floor untouched.
#define ROUTEPREFIX_CLOSE 96.0f
#define ROUTEPREFIX_APPROACH 250.0f     // the closing wishspeed ceiling
#define ROUTEPREFIX_RADIUS 0.5f         // inside this the bot is there
#define ROUTEPREFIX_BRAKE 1200.0f       // twice the ground deceleration, in units/sec/sec
#define ROUTEPREFIX_FLOOR 20.0f         // a command below this loses to friction; coast instead

static int PrefixEnabled(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_routePrefix",value,sizeof(value));
    return value[0] && atoi(value)!=0;
}

static float PrefixFloat(const char *name,float fallback) {
    char value[24];
    trap->Cvar_VariableStringBuffer(name,value,sizeof(value));
    return value[0]?(float)atof(value):fallback;
}

typedef struct {
    int dt;                                 // ms since the previous command
    int buttons;
    signed char forward,right,up;
    float yaw,pitch;                        // absolute view angles in degrees
} routeCommand_t;

typedef struct {
    // The command this state was recorded after. A server frame can run
    // several commands, so the recording's server clock cannot say which one a
    // mark belongs to, and reading marks by elapsed time let the first command
    // of a frame consume marks belonging to commands not yet executed - their
    // drift measured against a position the replay had not reached yet, and
    // never correctable afterwards. The command index can say, exactly, and it
    // does not care how the replay batches its commands into frames.
    int command;
    int elapsed;                            // the recording's server clock, for reporting only
    vec3_t origin;
    float speed;
    int kind;                               // 0 checkpoint, 1 takeoff, 2 landing
} routeMark_t;

static struct {
    int loaded,armed,client;
    char name[MAX_QPATH];
    int checksum,pmoveFixed,pmoveMsec;
    vec3_t start,startVelocity;
    float startYaw,startPitch;
    int startForce,startCarrying;
    int count,marks;
    int duration;
    routeCommand_t command[ROUTEPLAY_MAX_COMMANDS];
    routeMark_t mark[ROUTEPLAY_MAX_MARKS];
    // live
    int index,nextMark,startTime,submitted;
    int tapeMs;                             // physics time the submitted commands asked for
    float maxDrift;
    int pickup,finished;
    // follower
    int following,corrected;            // armed with the follower; grounded commands rotated
    float correction,maxCorrection;     // the last rotation applied, and the largest
    int followStartSet;                 // bot_routeFollowStart named a start
    vec3_t followStart;
    // prefix
    vec2_t entryDir;                    // the route's first line, from the tape's own marks
    int prefixing;                      // armed with the prefix; the walk owns the frames
    int prefixStartSet;                 // bot_routePrefixStart named where the walk begins
    vec3_t prefixStart;
    int prefixTime,prefixReport,prefixArrived,prefixMs,prefixFrames,handedOff;
    float entryAlong,entryCross,entryVertical,entrySpeed;   // the hand-off state, once
    int entryForce;
} tape;

static char tapeText[1024*1024];

// The held bot's think: it still acknowledges its reliable commands (a long
// hold otherwise overflows them, as the bench's idle knows). Its play log row,
// which route_approve's pinned start reads as the game clock, is written from
// the game frame (Bot2_RoutePlayClient), where the runner's own rows were.
static void Idle(int client,int time) {
    char command[1024];(void)time;
    while(trap->BotGetServerCommand(client,command,sizeof(command))) {}
}

// ---------------------------------------------------------------------------
// Ownership of the bot's AI.
//
// This module idles a bot's own think while it drives the client, and it has
// to give it back. Arming used to take the AI first and validate afterwards,
// so a refusal - a follower start out of range, or (in an earlier version) a
// prefix without the movement rewrite - left the bot idled with `armed` still false, and stop, which only
// acts on an armed tape, could not release it: the bot stood there for the
// rest of the map.
//
// So the hold is tracked on its own. `heldClient` is the client this module
// actually idled, which is not `tape.client`: a refused arm never sets that,
// and releasing whatever it names would idle a bot nothing asked about while
// leaving this one held. The think that was there is kept and restored, rather
// than cleared to NULL, so a bot that had a custom think keeps it.
// ---------------------------------------------------------------------------
static int heldClient=-1;
static void (*heldThink)(int clientNum,int time);
static playerState_t placeSaved;            // the state Place overwrote, for a refusal's rollback
static vec3_t placeSavedAngles;
static int placed;

static void Release(void) {
    gentity_t *ent;
    if(heldClient<0)return;
    ent=&g_entities[heldClient];
    if(ent->inuse && ent->client && botstates[heldClient] && botstates[heldClient]->customThink==Idle)
        botstates[heldClient]->customThink=heldThink;
    trap->Print("[ROUTEPLAY] RELEASE client=%d think=%s\n",heldClient,heldThink?"restored":"none");
    heldClient=-1;heldThink=NULL;
}

static qboolean Number(const char **cursor,float *out) {
    const char *token=COM_Parse((char**)cursor);
    if(!token[0])return qfalse;
    *out=atof(token);
    return qtrue;
}

// Integers are parsed as integers. A map checksum is a full 32-bit value and a
// float carries 24 bits of mantissa, so reading one through a float quietly
// rounds it - -1301924053 arrives as -1301924096 - and the tape is then refused
// against the map it was recorded on.
static qboolean Integer(const char **cursor,int *out) {
    const char *token=COM_Parse((char**)cursor);
    if(!token[0])return qfalse;
    *out=atoi(token);
    return qtrue;
}

static qboolean Vector(const char **cursor,vec3_t out) {
    float v;int i;
    for(i=0;i<3;i++) {if(!Number(cursor,&v))return qfalse;out[i]=v;}
    return qtrue;
}

// A tape is rejected whole or not at all: a prefix of a route is not a route,
// and a half-loaded one would replay into the map with no end.
static qboolean Load(const char *name) {
    fileHandle_t file;int length,i;const char *cursor;const char *token;
    char path[MAX_QPATH];
    if(!name[0] || strstr(name,"..") || strchr(name,':') || strchr(name,'\\') || name[0]=='/' ||
       strlen(name)+strlen("bot2-route/.tape")>=sizeof(path))return qfalse;
    Com_sprintf(path,sizeof(path),"bot2-route/%s.tape",name);
    length=trap->FS_Open(path,&file,FS_READ);
    if(!file)return qfalse;
    if(length<=0 || length>=(int)sizeof(tapeText)) {trap->FS_Close(file);return qfalse;}
    trap->FS_Read(tapeText,length,file);trap->FS_Close(file);tapeText[length]=0;
    cursor=tapeText;
    memset(&tape,0,sizeof(tape));
    // The format tag is a version, and an older tape is refused rather than
    // read as this one. BOTROUTE1's first command carries a zero where its
    // physics duration belongs and its marks name no command, so the same
    // fields would mean different things; the conversion is rebuilding the
    // asset from its retained recording CSV, which leaves the old asset and
    // its approval standing as the evidence they are.
    token=COM_Parse((char**)&cursor);
    if(strcmp(token,"BOTROUTE2")) {
        if(!strcmp(token,"BOTROUTE1"))
            trap->Print("[ROUTEPLAY] REFUSED tape format BOTROUTE1; rebuild the asset from its "
                "recording CSV with tools/route_asset_build.py\n");
        else trap->Print("[ROUTEPLAY] REFUSED tape format=%s expected=BOTROUTE2\n",token);
        return qfalse;
    }
    if(!Integer(&cursor,&tape.checksum))return qfalse;
    if(!Integer(&cursor,&tape.pmoveFixed))return qfalse;
    if(!Integer(&cursor,&tape.pmoveMsec))return qfalse;
    if(!Vector(&cursor,tape.start) || !Vector(&cursor,tape.startVelocity))return qfalse;
    if(!Number(&cursor,&tape.startYaw))return qfalse;
    if(!Number(&cursor,&tape.startPitch))return qfalse;
    if(!Integer(&cursor,&tape.startForce))return qfalse;
    if(!Integer(&cursor,&tape.startCarrying))return qfalse;
    if(!Integer(&cursor,&tape.count))return qfalse;
    if(tape.count<1 || tape.count>ROUTEPLAY_MAX_COMMANDS)return qfalse;
    if(!Integer(&cursor,&tape.marks))return qfalse;
    if(tape.marks<0 || tape.marks>ROUTEPLAY_MAX_MARKS)return qfalse;
    for(i=0;i<tape.count;i++) {
        routeCommand_t *c=&tape.command[i];int axis;
        if(strcmp(COM_Parse((char**)&cursor),"c"))return qfalse;
        if(!Integer(&cursor,&c->dt))return qfalse;
        // A command's delta is the physics Pmove ran for it, and playback
        // submits exactly that. Zero is not a command, and nothing here
        // substitutes a nominal step for one.
        if(c->dt<1 || c->dt>1000) {
            trap->Print("[ROUTEPLAY] REFUSED command=%d dt=%d ms; a command's duration is 1..1000 ms\n",i,c->dt);
            return qfalse;
        }
        if(!Integer(&cursor,&axis))return qfalse;c->forward=(signed char)axis;
        if(!Integer(&cursor,&axis))return qfalse;c->right=(signed char)axis;
        if(!Integer(&cursor,&axis))return qfalse;c->up=(signed char)axis;
        if(!Integer(&cursor,&c->buttons))return qfalse;
        if(!Number(&cursor,&c->yaw))return qfalse;
        if(!Number(&cursor,&c->pitch))return qfalse;
        tape.duration+=c->dt;
    }
    for(i=0;i<tape.marks;i++) {
        routeMark_t *k=&tape.mark[i];
        if(strcmp(COM_Parse((char**)&cursor),"k"))return qfalse;
        if(!Integer(&cursor,&k->command))return qfalse;
        if(!Integer(&cursor,&k->elapsed))return qfalse;
        if(k->command<0 || k->command>=tape.count || (i>0 && k->command<tape.mark[i-1].command)) {
            trap->Print("[ROUTEPLAY] REFUSED mark=%d command=%d of %d commands; marks name their own "
                "command, in order\n",i,k->command,tape.count);
            return qfalse;
        }
        if(!Vector(&cursor,k->origin))return qfalse;
        if(!Number(&cursor,&k->speed))return qfalse;
        if(!Integer(&cursor,&k->kind))return qfalse;
    }
    token=COM_Parse((char**)&cursor);
    if(strcmp(token,"END"))return qfalse;      // a truncated tape ends here, not in the map
    // The route's first line, which is the axis the entry tolerance was
    // measured on: along it the tape absorbs a phase offset, across it the
    // recorded takeoffs meet the wrong geometry. It is taken from the first
    // mark the recording actually travelled to, not from the start yaw, which
    // on a strafe run is deliberately off the direction of travel.
    tape.entryDir[0]=1;tape.entryDir[1]=0;
    for(i=0;i<tape.marks;i++) {
        float dx=tape.mark[i].origin[0]-tape.start[0],dy=tape.mark[i].origin[1]-tape.start[1];
        float len=sqrtf(dx*dx+dy*dy);
        if(len>32) {tape.entryDir[0]=dx/len;tape.entryDir[1]=dy/len;break;}
    }
    Q_strncpyz(tape.name,name,sizeof(tape.name));
    tape.loaded=1;
    return qtrue;
}

static void Finish(const char *reason) {
    gentity_t *ent=&g_entities[tape.client];
    // Stopping an unarmed module still has work to do: a refused arm leaves a
    // held client and no tape, and this is what gives that bot back.
    if(!tape.armed || !ent->client) {Release();return;}
    tape.armed=0;tape.finished=1;
    trap->Print("[ROUTEPLAY] DONE name=%s reason=%s elapsed_ms=%d tape_ms=%d commands=%d of=%d "
        "pickup=%d max_drift=%.3f reference_ms=%d follow=%d corrected=%d max_correction=%.3f "
        "prefix=%d prefix_ms=%d prefix_frames=%d entry_along=%.3f entry_cross=%.3f "
        "entry_vertical=%.3f entry_speed=%.3f entry_force=%d tape_force=%d "
        "origin=%.3f,%.3f,%.3f speed=%.3f\n",
        tape.name,reason,tape.startTime?level.time-tape.startTime:0,tape.tapeMs,tape.submitted,tape.count,tape.pickup,tape.maxDrift,
        tape.duration,tape.following,tape.corrected,tape.maxCorrection,
        tape.prefixing,tape.prefixMs,tape.prefixFrames,tape.entryAlong,tape.entryCross,
        tape.entryVertical,tape.entrySpeed,tape.entryForce,tape.startForce,
        ent->client->ps.origin[0],ent->client->ps.origin[1],ent->client->ps.origin[2],
        sqrtf(ent->client->ps.velocity[0]*ent->client->ps.velocity[0]+
              ent->client->ps.velocity[1]*ent->client->ps.velocity[1]));
    Release();
}

// What the replay is doing against what was recorded, at the recording's own
// marks. Drift is the whole measurement: a tape that reproduces the run stays
// near zero, and one that does not says where it left.
// `command` is the index of the command just executed, not a clock: a mark is
// read after the command it was recorded after, whether that command shared a
// server frame with three others or had one to itself.
static void Marks(const playerState_t *ps,int command) {
    int elapsed=tape.startTime?level.time-tape.startTime:0;
    while(tape.nextMark<tape.marks && tape.mark[tape.nextMark].command<=command) {
        const routeMark_t *k=&tape.mark[tape.nextMark];
        float dx=ps->origin[0]-k->origin[0],dy=ps->origin[1]-k->origin[1],dz=ps->origin[2]-k->origin[2];
        float drift=sqrtf(dx*dx+dy*dy+dz*dz);
        float speed=sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]);
        if(drift>tape.maxDrift)tape.maxDrift=drift;
        Bot2_PrintTelemetry(1,"[ROUTEPLAY] MARK name=%s i=%d kind=%d mark_command=%d command=%d "
            "mark_ms=%d elapsed_ms=%d tape_ms=%d drift=%.3f "
            "speed=%.3f recorded_speed=%.3f ground=%d diverged=%d follow=%d correction=%.3f "
            "origin=%.3f,%.3f,%.3f recorded=%.3f,%.3f,%.3f\n",
            tape.name,tape.nextMark,k->kind,k->command,command,
            k->elapsed,elapsed,tape.tapeMs,drift,speed,k->speed,
            ps->groundEntityNum!=ENTITYNUM_NONE,drift>ROUTEPLAY_DRIFT_REPORT,
            tape.following,tape.correction,
            ps->origin[0],ps->origin[1],ps->origin[2],k->origin[0],k->origin[1],k->origin[2]);
        tape.nextMark++;
        // A follower that is this far off is not following the route any
        // more, and the commands it keeps submitting belong to a line the
        // bot is not on. It stops and says so, rather than playing a tape
        // into geometry it was not recorded in.
        if(tape.following && drift>ROUTEFOLLOW_ABORT) {Finish("diverged");return;}
    }
}

// How far the bot is to the left of the recorded line, and the rotation that
// reduces it. The line is the segment between the mark just passed and the
// one coming, which is where the recording was over this stretch; cross-track
// is a distance from that line and needs no interpolation along it. The bot's
// own start stands in for the segment's first point until a mark has passed.
// `command` is the index of the command about to be submitted. The follower
// places itself along the segment by executed tape progress, not by the
// server clock: commands batched into one frame all share that clock, so it
// cannot say how far through the segment the replay is, and it is the same
// alignment error the marks had.
static float FollowCorrection(const playerState_t *ps,int command) {
    const routeMark_t *a,*b;const float *from,*to;
    float seg[2],len,ex,ey,cross,corr,span,t,was[2];
    if(!tape.following || tape.nextMark>=tape.marks)return 0;
    b=&tape.mark[tape.nextMark];to=b->origin;
    if(tape.nextMark>0) {a=&tape.mark[tape.nextMark-1];from=a->origin;span=(float)(b->command-a->command);t=span>0?(command-a->command)/span:0;}
    else {a=NULL;from=tape.start;span=(float)b->command;t=span>0?command/span:0;}
    seg[0]=to[0]-from[0];seg[1]=to[1]-from[1];
    len=sqrtf(seg[0]*seg[0]+seg[1]*seg[1]);
    if(len<1)return 0;                      // a mark the recording stood still at steers nothing
    seg[0]/=len;seg[1]/=len;
    // Where the recording was at this instant, not where the segment starts.
    // Measuring the offset from the segment's first mark reads the recording's
    // own curve as error and steers against it: on the route measured here that
    // produced a 0.86 degree correction with the bot exactly on the line, and
    // doubled the drift of open-loop playback. Interpolated in time, a bot on
    // the recorded line has an error of zero whatever the line is doing.
    if(t<0)t=0; if(t>1)t=1;
    was[0]=from[0]+(to[0]-from[0])*t;was[1]=from[1]+(to[1]-from[1])*t;
    ex=ps->origin[0]-was[0];ey=ps->origin[1]-was[1];
    cross=ex*-seg[1]+ey*seg[0];             // positive to the left of the recorded heading
    corr=-FollowGain()*cross;               // left of the line turns right, and yaw grows leftward
    if(corr>ROUTEFOLLOW_MAX_DEG)corr=ROUTEFOLLOW_MAX_DEG;
    if(corr<-ROUTEFOLLOW_MAX_DEG)corr=-ROUTEFOLLOW_MAX_DEG;
    return corr;
}

// Where the bot is relative to the tape's start, on the axes the entry
// tolerance was measured on: along the route's first line, across it, and
// vertically. The signs are kept - a bot short of the start and one past it
// are different states, even where the tape survives both.
static void EntryError(const playerState_t *ps,float *along,float *cross,float *vertical) {
    float dx=ps->origin[0]-tape.start[0],dy=ps->origin[1]-tape.start[1];
    *along=dx*tape.entryDir[0]+dy*tape.entryDir[1];
    *cross=dx*-tape.entryDir[1]+dy*tape.entryDir[0];
    *vertical=ps->origin[2]-tape.start[2];
}

// The walk into the tape's start. The movement engine plans and steers; this
// only decides when the bot has arrived, and submits what the engine asks for.
static qboolean PrefixFrame(gentity_t *ent) {
    usercmd_t cmd;playerState_t *ps=&ent->client->ps;
    float along,cross,vertical,speed;int i,elapsed=level.time-tape.prefixTime,arrived;
    if(ent->health<=0) {Finish("prefix_death");return qtrue;}
    EntryError(ps,&along,&cross,&vertical);
    speed=sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]);
    arrived=ps->groundEntityNum!=ENTITYNUM_NONE &&
            fabsf(along)<=PrefixFloat("bot_routePrefixAlong",ROUTEPREFIX_ALONG) &&
            fabsf(cross)<=PrefixFloat("bot_routePrefixCross",ROUTEPREFIX_CROSS) &&
            fabsf(vertical)<=PrefixFloat("bot_routePrefixVertical",ROUTEPREFIX_VERTICAL) &&
            speed<=PrefixFloat("bot_routePrefixSpeed",ROUTEPREFIX_SPEED);
    if(!arrived)tape.prefixArrived=0;
    else if(!tape.prefixArrived)tape.prefixArrived=level.time;
    if(level.time>=tape.prefixReport) {
        tape.prefixReport=level.time+ROUTEPREFIX_REPORT;
        Bot2_PrintTelemetry(1,"[ROUTEPLAY] PREFIX name=%s level=%d elapsed_ms=%d frames=%d "
            "along=%.3f cross=%.3f vertical=%.3f speed=%.3f ground=%d force=%d arrived=%d "
            "origin=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f\n",
            tape.name,level.time,elapsed,tape.prefixFrames,along,cross,vertical,speed,
            ps->groundEntityNum!=ENTITYNUM_NONE,ps->fd.forcePower,arrived,
            ps->origin[0],ps->origin[1],ps->origin[2],tape.start[0],tape.start[1],tape.start[2]);
    }
    if(tape.prefixArrived &&
       level.time-tape.prefixArrived>=(int)PrefixFloat("bot_routePrefixSettle",ROUTEPREFIX_SETTLE)) {
        // The hand-off. Everything the tape's time depends on and the walk
        // decided is recorded here, once, because after this line the tape
        // reacts to none of it.
        tape.prefixMs=elapsed;tape.handedOff=1;
        tape.entryAlong=along;tape.entryCross=cross;tape.entryVertical=vertical;
        tape.entrySpeed=speed;tape.entryForce=ps->fd.forcePower;
        tape.startTime=level.time;tape.index=0;tape.nextMark=0;tape.submitted=0;tape.tapeMs=0;
        Bot2_PrintTelemetry(1,"[ROUTEPLAY] HANDOFF name=%s level=%d prefix_ms=%d frames=%d "
            "along=%.3f cross=%.3f vertical=%.3f speed=%.3f force=%d tape_force=%d "
            "origin=%.3f,%.3f,%.3f start=%.3f,%.3f,%.3f yaw=%.3f tape_yaw=%.3f\n",
            tape.name,level.time,tape.prefixMs,tape.prefixFrames,along,cross,vertical,speed,
            ps->fd.forcePower,tape.startForce,
            ps->origin[0],ps->origin[1],ps->origin[2],tape.start[0],tape.start[1],tape.start[2],
            ps->viewangles[YAW],tape.startYaw);
        return qtrue;                   // the tape's first command is next frame's
    }
    if(elapsed>(int)PrefixFloat("bot_routePrefixTimeout",ROUTEPREFIX_TIMEOUT)) {
        tape.prefixMs=elapsed;
        tape.entryAlong=along;tape.entryCross=cross;tape.entryVertical=vertical;
        tape.entrySpeed=speed;tape.entryForce=ps->fd.forcePower;
        Finish("prefix_timeout");return qtrue;
    }
    memset(&cmd,0,sizeof(cmd));
    cmd.serverTime=level.time;cmd.weapon=ps->weapon;
    for(i=0;i<3;i++)cmd.angles[i]=ANGLE2SHORT(ps->viewangles[i])-ps->delta_angles[i];
    if(sqrtf((tape.start[0]-ps->origin[0])*(tape.start[0]-ps->origin[0])+
             (tape.start[1]-ps->origin[1])*(tape.start[1]-ps->origin[1]))<=
       PrefixFloat("bot_routePrefixClose",ROUTEPREFIX_CLOSE)) {
        // The last cell, closed on the point itself: slow into a radius and
        // let friction stop the bot, which is what the tape's first command
        // assumes it is standing in.
        bot2_moveIntent_t intent;float yaw=ps->viewangles[YAW],nextYaw=yaw;
        float d[2],length,want,radius;
        d[0]=tape.start[0]-ps->origin[0];d[1]=tape.start[1]-ps->origin[1];
        length=sqrtf(d[0]*d[0]+d[1]*d[1]);
        // Approach law: the speed whose coasting distance is what is left.
        // Ground friction below the stop speed takes about 600 units/sec/sec
        // off the bot, so a command of sqrt(2*600*d) arrives at d with
        // nothing to spare, and the bot stops on the point rather than in
        // front of it. Two laws were measured and neither worked: a
        // proportional one creeps, because friction eats a 20 units/sec
        // command and the bot spent 3.8 seconds on its last nine units; the
        // same law with a floor and a 3-unit dead radius parks the bot at
        // exactly 3.0 units, which one of the three routes does not survive.
        // Below the minimum a command cannot beat friction at all, so the
        // last fraction of a unit is coasted, not commanded.
        radius=PrefixFloat("bot_routePrefixRadius",ROUTEPREFIX_RADIUS);
        want=length<=radius?0:min(PrefixFloat("bot_routePrefixApproach",ROUTEPREFIX_APPROACH),
                                  sqrtf(PrefixFloat("bot_routePrefixBrake",ROUTEPREFIX_BRAKE)*(length-radius)));
        if(want<PrefixFloat("bot_routePrefixFloor",ROUTEPREFIX_FLOOR))want=0;
        memset(&intent,0,sizeof(intent));intent.turnRate=360;intent.allowStop=qtrue;
        if(length>1 && want>0) {
            intent.velocity[0]=d[0]/length*want;intent.velocity[1]=d[1]/length*want;
            // The wish limit is what makes a slow close possible at all. With
            // a full-throttle command the nearest legal speed to a 20-unit
            // intent is zero, so the bot stands where it is: the first run
            // with this controller stopped 13.3 units short and held it for
            // 30 seconds. Capping the requested wish speed lets PM_CmdScale's
            // analog magnitude issue the small command the intent asks for.
            Bot2_MoveControlLimited(ps,&intent,yaw,Bot2_GameStepMsec()*.001f,qtrue,want,&cmd,&nextYaw);
        }
    } else {
        // The engine owns the movement fields or it does not; a frame it
        // declines is a frame the bot stands still, which is the walk-only
        // floor's own behaviour and not something this module second-guesses.
        Move_Plan(ent->s.number,tape.start);
        Move_Command(ent->s.number,&cmd);
    }
    tape.prefixFrames++;
    ClientThink(ent->s.number,&cmd);ClientThink_real(ent);
    return qtrue;
}

qboolean Bot2_RoutePlayClient(gentity_t *ent) {
    usercmd_t cmd;playerState_t *ps;int enemyFlag;
    if(!tape.armed && ent->client && ent->s.number==heldClient)Bot2_PlayLogSample(heldClient,&ent->client->pers.cmd);
    if(!tape.armed || !ent->client || ent->s.number!=tape.client)return qfalse;
    ps=&ent->client->ps;
    // The armed bot left and a human took its slot: the tape is not theirs.
    if(!(ent->r.svFlags&SVF_BOT)) {Finish("client_gone");return qfalse;}
    if(ent->health<=0) {Finish("death");return qtrue;}
    if(tape.prefixing && !tape.handedOff)return PrefixFrame(ent);
    // Submit every command whose time has come, and no more: Pmove is not run
    // past the server frame, so a command dated beyond it waits for the next.
    while(tape.index<tape.count) {
        const routeCommand_t *c=&tape.command[tape.index];
        // The command's own recorded physics duration, added to the bot's own
        // command time. Load refuses a tape with any other kind of delta, so
        // there is nothing to substitute here: the first command gets the
        // physics it was given when recorded, like every command after it.
        int when=ps->commandTime+c->dt;
        if(when>level.time)break;
        memset(&cmd,0,sizeof(cmd));
        cmd.serverTime=when;
        cmd.forwardmove=c->forward;cmd.rightmove=c->right;cmd.upmove=c->up;
        cmd.buttons=c->buttons;cmd.weapon=ps->weapon;
        // The follower rotates the recorded frame on grounded commands; the
        // keys are the recording's either way, so the strafe offset it was
        // flown with is carried into the rotated heading rather than undone.
        tape.correction=0;
        if(tape.following && ps->groundEntityNum!=ENTITYNUM_NONE) {
            tape.correction=FollowCorrection(ps,tape.index);
            if(tape.correction!=0) {
                tape.corrected++;
                if(fabsf(tape.correction)>fabsf(tape.maxCorrection))tape.maxCorrection=tape.correction;
            }
        }
        cmd.angles[YAW]=ANGLE2SHORT(c->yaw+tape.correction)-ps->delta_angles[YAW];
        cmd.angles[PITCH]=ANGLE2SHORT(c->pitch)-ps->delta_angles[PITCH];
        cmd.angles[ROLL]=-ps->delta_angles[ROLL];
        ClientThink(ent->s.number,&cmd);ClientThink_real(ent);
        tape.index++;tape.submitted++;tape.tapeMs+=c->dt;
        if(ent->health<=0) {Finish("death");return qtrue;}
        Marks(ps,tape.index-1);
        if(!tape.armed)return qtrue;        // a mark ended the run; submit nothing more
        enemyFlag=ps->powerups[ent->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG]!=0;
        if(enemyFlag && !tape.pickup) {tape.pickup=1;Finish("pickup");return qtrue;}
    }
    if(tape.index>=tape.count) {Finish("tape_end");return qtrue;}
    return qtrue;
}

// Arming refuses more than it accepts. Every refusal names the thing that
// disagreed, because each one means the replay would produce a time for a run
// that is not the recorded run.
static qboolean Hold(int client);

// The tape's start, set on the bot. Placement lives inside arming because
// every way of doing it from outside leaves a gap: bot_corner_start clears the
// bot state, which releases the hold, and the AI then walks the bot off the
// mark before the next command arrives.
static void Place(gentity_t *ent) {
    playerState_t *ps=&ent->client->ps;vec3_t angles={0,0,0};
    // Everything placement overwrites, kept so a validation that can only run
    // after placing has something to roll back to. A refused arm must leave
    // the bot where it was standing, not on a tape start it will never play.
    placeSaved=*ps;VectorCopy(ent->s.angles,placeSavedAngles);placed=1;
    trap->UnlinkEntity((sharedEntity_t*)ent);
    // Playback is placed on the tape's start, because a tape played from
    // anywhere else traces a different line and its time means nothing. The
    // follower may start somewhere else - that is its whole point - but it is
    // still placed here, inside arming, and not left where the bot happens to
    // be standing: bot_corner_start clears the bot state, which releases the
    // hold issued before it, and the bot's own AI then walks it off the mark
    // in the gap between the two round trips. Measured at 47.9 units when
    // playback was written, and again at 40.8 units with a speed of 206 when
    // the follower first skipped this, which is a start no tape assumes.
    // bot_routeFollowStart names where; unset, the tape's own start.
    // The prefix starts where the walk starts - the benchmark stand - and
    // placement is inside arming here for the same reason the follower's is.
    if(tape.prefixing && tape.prefixStartSet) {
        VectorCopy(tape.prefixStart,ps->origin);VectorClear(ps->velocity);
    } else if(tape.following && tape.followStartSet) {
        VectorCopy(tape.followStart,ps->origin);VectorClear(ps->velocity);
    } else {
        VectorCopy(tape.start,ps->origin);VectorCopy(tape.startVelocity,ps->velocity);
    }
    // The recordings start standing on the world, which is the state their
    // first commands assume.
    ps->groundEntityNum=ENTITYNUM_WORLD;
    ps->fd.forcePowersActive=0;memset(ps->fd.forcePowerDuration,0,sizeof(ps->fd.forcePowerDuration));
    ps->fd.forcePower=tape.startForce;ps->fd.forcePowerRegenDebounceTime=level.time+1000;
    ps->pm_flags&=~(PMF_JUMP_HELD|PMF_TIME_KNOCKBACK|PMF_TIME_WATERJUMP);ps->pm_time=0;
    angles[YAW]=AngleNormalize180(tape.startYaw);angles[PITCH]=AngleNormalize180(tape.startPitch);
    SetClientViewAngle(ent,angles);
    VectorCopy(ps->origin,ent->r.currentOrigin);
    trap->LinkEntity((sharedEntity_t*)ent);
}

// The rollback. Placement is the only side effect arming has before it is
// committed, so undoing it and releasing the AI is the whole of a refusal's
// cleanup.
static void Unplace(gentity_t *ent) {
    playerState_t *ps;
    if(!placed || !ent->client)return;
    ps=&ent->client->ps;
    trap->UnlinkEntity((sharedEntity_t*)ent);
    *ps=placeSaved;
    VectorCopy(placeSavedAngles,ent->s.angles);
    VectorCopy(ps->origin,ent->r.currentOrigin);
    trap->LinkEntity((sharedEntity_t*)ent);
    placed=0;
}

static qboolean Arm(int client) {
    gentity_t *ent=&g_entities[client];const playerState_t *ps;
    char checksum[32];float dx,dy,dz,offset,limit;
    int fixed=Bot2_PmoveFixed(),msec=trap->Cvar_VariableIntegerValue("pmove_msec");
    if(!tape.loaded) {trap->Print("[ROUTEPLAY] REFUSED no tape loaded\n");return qfalse;}
    if(client<0 || client>=MAX_CLIENTS || !ent->inuse || !ent->client) {
        trap->Print("[ROUTEPLAY] REFUSED client=%d not in use\n",client);return qfalse;}
    if(!(ent->r.svFlags&SVF_BOT)) {
        trap->Print("[ROUTEPLAY] REFUSED client=%d is not a bot\n",client);return qfalse;}
    ps=&ent->client->ps;
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",checksum,sizeof(checksum));
    if(tape.checksum && atoi(checksum)!=tape.checksum) {
        trap->Print("[ROUTEPLAY] REFUSED map_checksum=%s tape=%d\n",checksum,tape.checksum);return qfalse;}
    if(fixed!=tape.pmoveFixed || msec!=tape.pmoveMsec) {
        trap->Print("[ROUTEPLAY] REFUSED pmove_fixed=%d pmove_msec=%d tape=%d,%d\n",
            fixed,msec,tape.pmoveFixed,tape.pmoveMsec);return qfalse;}
    // A tape already running is ended before another is armed, so a
    // replacement cannot strand the client the old one held.
    if(tape.armed)Finish("rearmed");
    // Everything that can be decided without touching the bot is decided
    // first. Arming used to take the AI and then refuse, which left the bot
    // idled with nothing able to release it.
    tape.following=FollowEnabled();
    tape.followStartSet=0;
    tape.prefixing=PrefixEnabled();
    tape.prefixStartSet=0;
    if(tape.prefixing) {
        char where[96];
        trap->Cvar_VariableStringBuffer("bot_routePrefixStart",where,sizeof(where));
        if(where[0] && sscanf(where,"%f %f %f",&tape.prefixStart[0],&tape.prefixStart[1],
                              &tape.prefixStart[2])==3)tape.prefixStartSet=1;
    }
    if(tape.following) {
        char where[96];
        trap->Cvar_VariableStringBuffer("bot_routeFollowStart",where,sizeof(where));
        if(where[0] && sscanf(where,"%f %f %f",&tape.followStart[0],&tape.followStart[1],
                              &tape.followStart[2])==3)tape.followStartSet=1;
    }
    // Only now is the bot touched: the hold, then the placement the start
    // check needs. Both are undone together if that check refuses.
    if(!Hold(client))return qfalse;
    Place(ent);
    // The offset is checked after placing, so it reports a placement that did
    // not take rather than one that was never attempted.
    dx=ps->origin[0]-tape.start[0];dy=ps->origin[1]-tape.start[1];dz=ps->origin[2]-tape.start[2];
    offset=sqrtf(dx*dx+dy*dy+dz*dz);
    // The prefix's whole point is a start the tape was not recorded at, so the
    // start check is not the arming gate for it; the arrival gate is, and it
    // runs at the hand-off where it can be measured rather than assumed.
    limit=tape.prefixing?1e9f:tape.following?ROUTEFOLLOW_START:ROUTEPLAY_START_TOLERANCE;
    if(offset>limit) {
        trap->Print("[ROUTEPLAY] REFUSED start_offset=%.3f limit=%.3f follow=%d at=%.3f,%.3f,%.3f tape=%.3f,%.3f,%.3f\n",
            offset,limit,tape.following,ps->origin[0],ps->origin[1],ps->origin[2],
            tape.start[0],tape.start[1],tape.start[2]);
        Unplace(ent);Release();
        return qfalse;
    }
    placed=0;                               // the placement is the run's now, not a rollback's
    tape.armed=1;tape.client=client;tape.index=0;tape.nextMark=0;tape.submitted=0;tape.tapeMs=0;
    tape.maxDrift=0;tape.pickup=0;tape.finished=0;
    // The tape's clock starts at the hand-off, not at arming: the walk's time
    // is the walk's, and a route time that included it would not be the
    // recorded route's time.
    tape.startTime=tape.prefixing?0:level.time;
    tape.prefixTime=tape.prefixing?level.time:0;
    tape.prefixReport=tape.prefixTime;tape.prefixArrived=0;tape.prefixMs=0;tape.prefixFrames=0;
    tape.handedOff=0;
    tape.entryAlong=tape.entryCross=tape.entryVertical=tape.entrySpeed=0;tape.entryForce=0;
    if(botstates[client])botstates[client]->customThink=Idle;
    if(tape.prefixing)Move_Reset(client);
    Bot2_PrintTelemetry(1,"[ROUTEPLAY] START name=%s client=%d level=%d commands=%d marks=%d duration_ms=%d "
        "follow=%d prefix=%d start_offset=%.3f entry_dir=%.4f,%.4f origin=%.3f,%.3f,%.3f yaw=%.3f pmove=%d,%d\n",
        tape.name,client,level.time,tape.count,tape.marks,tape.duration,tape.following,tape.prefixing,offset,
        tape.entryDir[0],tape.entryDir[1],
        ps->origin[0],ps->origin[1],ps->origin[2],ps->viewangles[YAW],fixed,msec);
    return qtrue;
}

// Stop the world moving the bot. Placement and arming are separate commands
// and the bot's own AI walks it off the mark between them - measured at 47.9
// units in about a second - so a tape is placed and armed on a held bot. The
// retained command is cleared as well as the think: a bot whose AI stops
// thinking still has its last command applied every frame.
static qboolean Hold(int client) {
    gentity_t *ent=&g_entities[client];
    if(client<0 || client>=MAX_CLIENTS || !ent->inuse || !ent->client) {
        trap->Print("[ROUTEPLAY] REFUSED hold client=%d not in use\n",client);return qfalse;}
    // One client at a time, and the think that was there is remembered once:
    // holding an already-held client must not record this module's own Idle
    // as the think to restore.
    if(heldClient>=0 && heldClient!=client)Release();
    if(heldClient!=client) {
        heldThink=botstates[client]?botstates[client]->customThink:NULL;
        if(heldThink==Idle)heldThink=NULL;
        heldClient=client;
    }
    if(botstates[client])botstates[client]->customThink=Idle;
    ent->client->pers.cmd.forwardmove=0;ent->client->pers.cmd.rightmove=0;
    ent->client->pers.cmd.upmove=0;ent->client->pers.cmd.buttons=0;
    trap->Print("[ROUTEPLAY] HOLD client=%d origin=%.3f,%.3f,%.3f\n",
        client,ent->client->ps.origin[0],ent->client->ps.origin[1],ent->client->ps.origin[2]);
    return qtrue;
}

qboolean Bot2_RoutePlayConsoleCommand(void) {
    char cmd[MAX_TOKEN_CHARS],arg[MAX_QPATH];
    trap->Argv(0,cmd,sizeof(cmd));
    if(Q_stricmp(cmd,"bot_route_play"))return qfalse;
    trap->Argv(1,arg,sizeof(arg));
    if(!Q_stricmp(arg,"stop")) {Finish("stopped");return qtrue;}
    if(!Q_stricmp(arg,"status")) {
        trap->Print("[ROUTEPLAY] STATUS name=%s loaded=%d armed=%d client=%d index=%d of=%d "
            "marks=%d/%d max_drift=%.3f pickup=%d finished=%d held=%d tape_ms=%d\n",
            tape.name,tape.loaded,tape.armed,tape.client,tape.index,tape.count,
            tape.nextMark,tape.marks,tape.maxDrift,tape.pickup,tape.finished,heldClient,tape.tapeMs);
        return qtrue;
    }
    if(!Q_stricmp(arg,"load")) {
        char name[MAX_QPATH];
        trap->Argv(2,name,sizeof(name));
        if(tape.armed) {trap->Print("[ROUTEPLAY] REFUSED load %s while a tape plays (stop first)\n",name);return qtrue;}
        if(!Load(name)) {trap->Print("[ROUTEPLAY] REFUSED load %s\n",name);tape.loaded=0;return qtrue;}
        trap->Print("[ROUTEPLAY] LOAD name=%s commands=%d marks=%d duration_ms=%d checksum=%d "
            "start=%.3f,%.3f,%.3f\n",
            tape.name,tape.count,tape.marks,tape.duration,tape.checksum,
            tape.start[0],tape.start[1],tape.start[2]);
        return qtrue;
    }
    // Read-only, and deliberately so: a probe that wants to know whether the
    // bot's own AI is running cannot ask with `hold`, which would stop it.
    if(!Q_stricmp(arg,"where")) {
        char who[8];int client;gentity_t *ent;
        trap->Argv(2,who,sizeof(who));client=who[0]?atoi(who):0;
        ent=&g_entities[client];
        if(client<0 || client>=MAX_CLIENTS || !ent->inuse || !ent->client) {
            trap->Print("[ROUTEPLAY] WHERE client=%d in_use=0\n",client);return qtrue;}
        trap->Print("[ROUTEPLAY] WHERE client=%d in_use=1 held=%d idled=%d think=%d origin=%.3f,%.3f,%.3f speed=%.3f\n",
            client,heldClient==client,
            botstates[client]!=NULL && botstates[client]->customThink==Idle,
            botstates[client]!=NULL && botstates[client]->customThink!=NULL,
            ent->client->ps.origin[0],ent->client->ps.origin[1],ent->client->ps.origin[2],
            sqrtf(ent->client->ps.velocity[0]*ent->client->ps.velocity[0]+
                  ent->client->ps.velocity[1]*ent->client->ps.velocity[1]));
        return qtrue;
    }
    if(!Q_stricmp(arg,"hold")) {
        char who[8];
        trap->Argv(2,who,sizeof(who));
        Hold(who[0]?atoi(who):0);
        return qtrue;
    }
    if(!Q_stricmp(arg,"arm")) {
        char who[8];
        trap->Argv(2,who,sizeof(who));
        Arm(who[0]?atoi(who):0);
        return qtrue;
    }
    trap->Print("usage: bot_route_play load <name> | hold [client] | arm [client] | where [client] | status | stop\n");
    return qtrue;
}

void Bot2_RoutePlayShutdown(void) {
    if(tape.armed)Finish("shutdown");
    Release();
    memset(&tape,0,sizeof(tape));
    placed=0;
}
