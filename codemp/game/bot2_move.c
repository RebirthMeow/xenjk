// Movement rewrite (docs/movement/PLAN.md). One state per client, one reset,
// a walk executor, the phase 1 composition spike and the parity harness.
// The executor's step is a pure function of the player state, its own
// continuation state and the immutable world, so the forecast rolls out the
// very same function: discovery, parity and live execution share it.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2_internal.h"
#include "g_navmesh.h"
#include "bot2_movecontrol.h"
#include "bot2_forecast.h"
#include "bot2_wallprobe.h"
#include "bot2_graph.h"
#include "bot2_move.h"
#include "bot2_movecost.h"
#include "bot2_mover.h"
#include "bot2_threat.h"
#include "bot2_defender.h"

// Review batch-2 fixes: on by default; a cvar set to 0
// turns its fix off (records/2026-11-10-review-batch2.md).
static int FixOn(const char *name) {char v[16];trap->Cvar_VariableStringBuffer(name,v,sizeof(v));return !v[0] || atoi(v)!=0;}

// Corridor checked ahead of the runner each frame, in four samples.
#define MOVE_WALK_LOOKAHEAD 96.0f
// Distance from an off-mesh start at which the retained off-mesh states own.
#define MOVE_OFFMESH_YIELD 128.0f
// Wallruns and elevators stay last-resort paths, as in the legacy query.
#define MOVE_OFFMESH_AVOID ((1 << OFFMESH_AREA_WALLRUN) | (1 << OFFMESH_AREA_ELEVATOR))
// The whole committed sequence and its onward walk fit one horizon: 6400 ms (256 frames at the 25 ms step before
// horizons were stated in ms; records/2026-10-21-tickrate-horizons.md). Every Bot2 horizon is stated in ms and stepped with
// MoveSteps; MOVE_PARITY_MAX_HORIZON is the frame buffers' capacity, which caps any of them.
#define MOVE_PARITY_HORIZON_MS 6400
#define MOVE_PARITY_MAX_HORIZON BOT2_FORECAST_MAX_STEPS
// Speed requested beyond the current speed while airborne, so the kernel
// picks the strafe angle that adds speed along the hop heading.
#define MOVE_FLIGHT_GAIN 40.0f
// Onward travel from the second landing that counts as useful.
#define MOVE_ONWARD_DISTANCE 128.0f
// Grounded time a landed plan may hold the onward stage without covering
// that distance before bot_moveOnwardBound releases it: one second (40
// frames at the 25 ms decision step).
#define MOVE_ONWARD_HELD_MS 1000
// A rollout that drops this far below its root is a fall, whatever it hit.
#define MOVE_FALL_DROP 256.0f
// Progress toward the objective a sequence must show at DONE.
#define MOVE_PROGRESS_MIN 64.0f
// Phase 2 gap discovery: the walk is rolled out this far ahead (64 frames at
// 25 ms) on grounded frames, and a failed search waits this long before another.
#define MOVE_GAP_LOOKAHEAD_MS 1600
#define MOVE_GAP_RETRY 250
// Milliseconds between lookaheads; the window is 1.6 s, so a refusal is
// still seen more than a second ahead.
#define MOVE_GAP_CADENCE 200
// Path following: a cell is in reach within this radius, the path is
// rejoined within this one after a flight, a replan waits this long, and a
// planned edge is searched for a commit from this far before its takeoff.
#define MOVE_PATH_REACH 48.0f
#define MOVE_PATH_REJOIN 128.0f
#define MOVE_PATH_RETRY 500
#define MOVE_EDGE_APPROACH 400.0f
// A permit covers this many path cells past the refusal, is rolled out over
// this long (128 frames at 25 ms), and its rollout may not drop this far below
// the lowest cell it covers.
#define MOVE_PERMIT_CELLS 8
#define MOVE_PERMIT_HORIZON_MS 3200
#define MOVE_PERMIT_DROP 48.0f
// Straight hops: the path ahead is a straight when its cells lie within
// this lateral distance of the chord from the progress cell, over at most
// this many cells; a hop is searched when the chord from the bot is at
// least this long and the objective further than this, and committed only
// when its rollout is ahead of the walk's by this margin at the same frame.
#define MOVE_STRAIGHT_CELLS 24
#define MOVE_STRAIGHT_LATERAL 24.0f
#define MOVE_STRAIGHT_MIN 192.0f
#define MOVE_STRAIGHT_GOAL 128.0f
// A rollout the forecast refused at a trigger it touched arrives when the
// game's own touch test says the objective's entity is reached within this
// much more of its flight (ArrivesAt; 4 frames at 25 ms), one sample a frame.
#define MOVE_ARRIVE_MS 100
#define MOVE_ARRIVE_MARGIN 8.0f
#define MOVE_STRAIGHT_GAIN 16.0f
// A candidate whose landing leaves the selected path is admitted only
// when the route from that landing, priced by the same walk model as the
// route it leaves, saves at least this much: a local margin over the walk
// is not a route-time objective, and a fast landing into a longer corridor
// is slower where it counts.
#define MOVE_REROUTE_GAIN 250
// A straight candidate's rollout runs on this long past its landing (48
// frames at 25 ms): the walk from the landing must neither decline nor fall in
// that time, so a landing whose momentum carries the walk over a ledge is refused.
#define MOVE_STRAIGHT_HOLD_MS 1200
// Where a tap lands, measured on Kejim at level 1 over 587 live hops
// (landing-a): the horizontal length is about .695 times the launch speed
// plus 76 units, and the landing is about 100 u/s faster than the launch.
// Used only to place a candidate's aim point on the path; the rollout
// still decides whether the hop is taken.
#define MOVE_HOP_SLOPE .695f
#define MOVE_HOP_OFFSET 76.0f
#define MOVE_HOP_LAND_GAIN 100.0f
// Phase 3: the Speed edge. A purchase is made on a grounded frame the walk
// owns when the policy permits it, the pool affords it and the path left at
// the walk's speed is at least this long; a purchase the game refused is
// retried after MOVE_SPEED_RETRY.
#define MOVE_SPEED_MIN_MS 1000
#define MOVE_SPEED_RETRY 1000
// The momentum chord the walk checks besides its command chord: where the
// velocity carries the bot in the frames a ground turn takes (four at the
// 25 ms step), checked only above stop speed.
// already a time (seconds), so bot_moveFixBM8 leaves it as it is.
#define MOVE_MOMENTUM_TIME .1f
#define MOVE_MOMENTUM_MIN 100.0f
// bot_moveLipGuard (default on): below that, and wherever a tenth of a second
// is shorter, the chord is MOVE_MOMENTUM_REACH long from MOVE_MOMENTUM_SLOW
// up. A floor trace with the player's box finds floor while any of the box is
// over the lip, so a chord of a few units stops seeing the drop one frame
// after it braked for it: at 100-130 u/s the brake released every other
// frame and the walk slid off the ledge beside the red base toward a refused
// crossing (two deaths at start class 175). A box width and a margin ahead
// holds the bot about 17 units short of the lip.
#define MOVE_MOMENTUM_REACH 32.0f
#define MOVE_MOMENTUM_SLOW 20.0f
#define MOVE_MOMENTUM_KEEP_TIME .25f
#define MOVE_MOMENTUM_KEEP_REACH 320.0f
// Phase 3 state plans: a planned strafe edge is validated when the bot is
// within this distance of its takeoff, and a bot this far past the takeoff
// along the chord with no sequence in progress is walking a flight, which
// refuses the edge and asks for a new path.
#define MOVE_STATE_TAKEOFF 64.0f
// Planned-edge candidates per search: one or two hops by two run-ups,
// doubled when the replay arm (bot_moveReplay bit 2) adds its
// stored-heading copies of the same set.
#define MOVE_STATE_CANDIDATES 8

typedef struct {
    vec3_t goal;
    int client;
    int stepMsec;
    const bot2_movePlan_t *plan;    // NULL or count 0: walk
    const bot2_movePath_t *path;    // NULL or count 0: the navmesh waypoint steers
    const bot2_movePermit_t *permit;// NULL or inactive: every chord must pass the corridor
} bot2_moveContext_t;

extern int forcePowerNeeded[NUM_FORCE_POWER_LEVELS][NUM_FORCE_POWERS];   // the power costs, bg_pmove.c
static movement_t movement[MAX_CLIENTS];
static void ParityReset(int client);
static void GuestHopPrint(int client,int stage);
static void GuestTraceCompare(int client);
static const char *ParityCoverage(int client);
static int ParityHorizon(int client);
static int MoveSearchGates(int client);

// The strafing chaser (bot_defenderStrafe): the declared
// fixture defender (bot2_defender.c) runs the same searches as the runner,
// hops and strafes, but as a guest. The graph is shared by every bot, so a
// guest writes nothing into it - no learned or merged edge, no validation, no
// measured cost, no failure charge; a failure it sees goes on its own avoid
// list only - and the parity harness, the decision tape, exploration and the
// fixture's plan directives stay the runner's. What the runner meets is the
// guest's body, not its learning.
static int MoveGuest(int client) {
    return Bot2_DefenderIs(client) && trap->Cvar_VariableIntegerValue("bot_defenderStrafe");
}

// Rollouts this bot has issued on this frame while a pre-search chunk runs,
// and the budget that bounds them. Zero budget means the judge is not bounded
// at all, which is exactly today's behaviour. See the search budget below.
static int searchRollouts,searchRolloutBudget;
// Frame-end pool chunks: no rollout starts once bot2_meterSteps has run searchStepBudget past searchStepFrom (0: none).
// A difference, not a deadline: the counter runs the module's lifetime and wraps.
static unsigned int searchStepFrom,searchStepBudget;
static int SearchStepsSpent(void) {return searchStepBudget && bot2_meterSteps-searchStepFrom>=searchStepBudget;}
static void SearchBudgetReset(int client);
static void FinishPlan(int client);
static int ArriveSafe(void);
static int ExploreEnabled(int client);
static void ExploreReset(int client);
static void ExploreStep(gentity_t *ent,pmove_t *move);
static float Wrap(float a) {a=fmodf(a+180,360);if(a<0)a+=360;return a-180;}
// A horizon stated in ms as rollout steps of the current game frame (ai_bot2.c, Bot2_StepsFor), within the frame
// buffers' capacity. Exactly the old frame counts at 25 ms.
static int MoveSteps(int ms) {return min(Bot2_StepsFor(ms),MOVE_PARITY_MAX_HORIZON);}

// The planner is the default: bot_movePlanner unset or nonzero turns it on,
// zero leaves the navmesh waypoint steering as the gap-edge step did.
static int PlannerEnabled(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanner",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// Straight hops are on with the planner: bot_moveStraight unset or nonzero.
static int StraightEnabled(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveStraight",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// The state search is on with the planner: bot_moveState unset or nonzero.
// Zero plans over cells alone, as phase 2 did, and validates no strafe
// edge; straight plans are still learned as edges.
static int StateEnabled(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveState",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// An admitted planned hop is committed over the search's own candidates
// when bot_moveStatePrefer is unset or nonzero: the chain was chosen as a
// whole by the state search, and a greedy hop that beats it locally lands
// in a state the next planned edge was not learned from. Zero keeps the
// one ranking by margin over the walk.
static int StatePreferred(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveStatePrefer",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// Replay instrumentation (bot_moveReplay), off by default so ordinary
// play, selection and timing are unchanged. Bit 1 prints [REPLAY]
// records: the path and a snapshot of every edge it plans, the full
// live root at a candidate boundary, every candidate with its
// construction and result, and every executed stage crossing against
// what the commit predicted and the graph priced. Bit 2 adds the
// observe-only measured-policy arm described at StraightSearch.
// Diagnostic overhead: a control run must carry the same flags.
static int ReplayFlags(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveReplay",value,sizeof(value));
    return value[0]?atoi(value):0;
}

// bot_moveReroute bit 1 (production sets it; unset is 0): a candidate whose
// landing leaves the selected path is rolled out
// again without the path stop and admitted when the landing opens a route of
// its own. Zero refuses every such candidate: the measured rejected set
// holds both viable continuations and trajectories that fall or strand, so
// the gate is replaced by a validated route rather than widened or removed.
// A rerouted candidate is ranked by the same margin as the rest: each arm's
// reference is the same walk from the same root, rolled to its own arm's
// horizon, so the frames a candidate is judged at are the same frames.
// Bits 2 (route-price ranking) and 4 (StatePreferred over a rerouted
// winner) were measured and removed; only bit 1 is read.
// Unset is the production value, 1.
static int RerouteFlags(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveReroute",value,sizeof(value));
    return value[0]?atoi(value):1;
}

// bot_moveEdges: gap discovery and planned edges at corridor refusals. Unset is
// the production value, on; 0 turns them off.
static int MoveEdgesOn(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveEdges",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// A planned strafe edge is stale when the live state at its takeoff is
// outside the entry envelope it was learned with: the plan expected to
// arrive there carrying a state the walk did not deliver. The path is then
// searched again - and the search, pricing the same links by the same
// accelerating model, plans the same chord, which is stale again on the next
// cadence. Measured: seven of those in a row on
// blue_out_L1_stand, 250 ms apart, the bot standing at 31-148 units/sec in
// front of an edge whose entry needs a 417 units/sec landing-frame tap.

// A committed sequence's last stage is cleared by travelling
// MOVE_ONWARD_DISTANCE from the landing mark, and the planner replans only
// when no sequence is in progress. A bot that lands where it cannot cover
// that distance - a pocket, a refused corridor either side - therefore holds
// a plan that will never finish and a path that will never be searched
// again. Measured on `blue_out_L1_stand`: after a landing at
// (1901, 2611) the bot shuffled inside ten units for 174 seconds, with no
// onward completion and no path event, until the case timed out. The straight
// search ran throughout and refused every candidate, so nothing recovered it.
//
// With bot_moveOnwardBound nonzero the stage is released after
// MOVE_ONWARD_HELD_MS on the ground, exactly as the distance would release
// it, and the path is searched again from where the bot stands. Unset or
// zero keeps the deadlock. This bounds the symptom; the walk that cannot
// leave the landing is its own defect.

// The judge ranks admitted candidates by the progress they make inside the
// horizon, which has no term for the speed a plan hands to the next one. The
// stand runs are 80.6 per cent flight and flight distance is bought with that
// speed, so the ranking is measuring the wrong thing: at 81 of 107 broken
// chains the committed plan ran up at the walk's wishspeed from a 415
// units/sec landing, winning on progress while shedding 165 units/sec, and it
// won honestly. bot_moveExitSpeed is that missing term, in milliseconds: the
// candidate's measured exit speed is credited as the distance it carries in
// that time and added to the margin, for ranking only. Admission is
// untouched - a refused candidate stays refused, and MOVE_STRAIGHT_GAIN is
// still tested against the margin alone - so this can only change which
// admitted candidate wins. Zero ranks by margin alone. Unset is the production
// value, 500 (the tools' PRODUCTION folded into the defaults).
static int ExitSpeedMs(void) {
    char value[16];int ms;
    trap->Cvar_VariableStringBuffer("bot_moveExitSpeed",value,sizeof(value));
    if(!value[0])return 500;
    ms=atoi(value);
    return ms>0?ms:0;
}

static int OnwardBound(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveOnwardBound",value,sizeof(value));
    return value[0] && atoi(value)!=0;
}

// The path generation: a new identity for every search result, so a
// candidate, an edge snapshot and an executed crossing can be related
// to the exact path they belong to. Edge index alone is not identity.
static int replayGeneration;

// The spend policy: bot_moveSpend unset permits every resource edge, else
// its bits name the permitted ones. The benchmark passes each case's
// templates; a level-1 case starting at 100 Force still buys nothing.
static int SpendPolicy(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveSpend",value,sizeof(value));
    return !value[0]?MOVE_SPEND_SPEED|MOVE_SPEND_HOLD:atoi(value);
}

static const char *DeclineName(int code) {
    switch(code) {
    case MOVE_DECLINE_NONE:return "none";
    case MOVE_DECLINE_MOVEMENT_TYPE:return "movement_type";
    case MOVE_DECLINE_LEGACY_STATE:return "legacy_state";
    case MOVE_DECLINE_NO_PATH:return "no_path";
    case MOVE_DECLINE_OFFMESH:return "offmesh";
    case MOVE_DECLINE_CORRIDOR:return "corridor";
    case MOVE_DECLINE_MISS:return "miss";
    }
    return "unknown";
}

static const char *EventName(bot2_moveEvent_t event) {
    switch(event) {
    case MOVE_EVENT_PLAN:return "plan";
    case MOVE_EVENT_EDGE:return "edge";
    case MOVE_EVENT_MISS:return "miss";
    case MOVE_EVENT_WALK:return "walk";
    case MOVE_EVENT_RECOVER:return "recover";
    case MOVE_EVENT_PARITY:return "parity";
    default:return "none";
    }
}

// The onward stage of a plan: the walk from its last landing.
static int Onward(const bot2_movePlan_t *plan) {return plan->count*2;}

static const char *StageEdge(int stage,const bot2_movePlan_t *plan) {
    static char name[16];
    if(stage>=Onward(plan))return "onward";
    Com_sprintf(name,sizeof(name),"hop%d",stage>>1);
    return name;
}

static const char *PlanKind(int kind) {
    switch(kind) {
    case MOVE_PLAN_SPIKE:return "spike";
    case MOVE_PLAN_GAP:return "gap";
    case MOVE_PLAN_EDGE:return "edge";
    case MOVE_PLAN_STRAIGHT:return "straight";
    case MOVE_PLAN_STATE:return "state";
    case MOVE_PLAN_CLIMB:return "climb";
    }
    return "none";
}

// Path following. Progress is the furthest path cell in reach inside a
// window ahead of the last index, else the nearest cell the path can be
// rejoined at (after a flight), else -1: off the path.
static int CellReach(const playerState_t *ps,int cell,float radius,float *distance) {
    const graphCell_t *c=Graph_Cell(cell);float dx,dy;
    if(!c || fabsf(c->z-ps->origin[2])>2*STEPSIZE)return 0;
    dx=c->ix*(float)GRAPH_GRID-ps->origin[0];dy=c->iy*(float)GRAPH_GRID-ps->origin[1];
    *distance=sqrtf(dx*dx+dy*dy);
    return *distance<=radius;
}

static void CellPoint(int cell,vec3_t out) {
    const graphCell_t *c=Graph_Cell(cell);
    out[0]=c->ix*(float)GRAPH_GRID;out[1]=c->iy*(float)GRAPH_GRID;out[2]=c->z-.125f;
}

// A strafe edge is a chord on continuous floor: the walk passes through it
// as it passes any straight, the corridor judging the chord as it judges
// every other, so every reader of the path joins the cells either side of
// it. The other edge kinds are a gap, a pad or a hint, and stop the walk.
static int PathStrafe(const bot2_movePath_t *path,int k) {
    const graphEdge_t *e;
    if(k<0 || k>=path->count || path->edges[k]<0)return 0;
    e=Graph_Edge(path->edges[k]);
    return e && e->type==GRAPH_EDGE_STRAFE;
}

// Whether a point is within reach of the chord between two floor points,
// at the chord's height there.
static int ChordReach(const vec3_t a,const vec3_t b,const vec3_t pos,float radius) {
    float dx=b[0]-a[0],dy=b[1]-a[1],len2=dx*dx+dy*dy,t=0,px,py,pz;
    if(len2>0)t=max(0.0f,min(1.0f,((pos[0]-a[0])*dx+(pos[1]-a[1])*dy)/len2));
    px=a[0]+dx*t-pos[0];py=a[1]+dy*t-pos[1];pz=a[2]+(b[2]-a[2])*t+.125f-pos[2];
    return fabsf(pz)<=2*STEPSIZE && px*px+py*py<=radius*radius;
}

static int PathProgress(const playerState_t *ps,const bot2_movePath_t *path,int index) {
    int i,best=-1,lo,hi;float d,bestD=1e9f;
    if(index<0)index=0;
    lo=max(0,index-2);hi=min(path->count,index+16);
    for(i=lo;i<hi;i++)if(CellReach(ps,path->cells[i],MOVE_PATH_REACH,&d))best=i;
    if(best>=0)return best;
    // Beside a strafe chord the progress had already reached the takeoff
    // of: progress stays at the takeoff (a flight in progress, or a landing
    // short of the chord's end). A chord ahead of the progress is not
    // joined from the side: a bot approaching its takeoff would be
    // projected onto the chord ahead of any validation and walk a flight.
    for(i=lo;i<=index && i<hi;i++)if(i+1<path->count && PathStrafe(path,i+1)) {
        vec3_t a,b;
        CellPoint(path->cells[i],a);CellPoint(path->cells[i+1],b);
        if(ChordReach(a,b,ps->origin,MOVE_PATH_REACH))best=i;
    }
    if(best>=0)return best;
    for(i=0;i<path->count;i++)if(CellReach(ps,path->cells[i],MOVE_PATH_REJOIN,&d) && d<bestD) {bestD=d;best=i;}
    return best;
}

// A sequence can land inside a later strafe chord without reaching either
// endpoint. Landing admission recognizes that chord, but the walking progress
// rule deliberately recognizes only chords whose takeoff was already reached.
// Preserve that rule for ordinary walking and every successful lookup. Only
// the last flight's touchdown may recover a missing index from the same
// forward window and geometry used by NearPathCell to admit its landing.
static int LandingPathProgress(const playerState_t *ps,const bot2_movePath_t *path,int index) {
    int k;
    if(!path || index<0 || index>=path->count || PathProgress(ps,path,index)>=0)return index;
    for(k=index+1;k<path->count && k<=index+48;k++)if(PathStrafe(path,k)) {
        vec3_t a,b;
        CellPoint(path->cells[k-1],a);CellPoint(path->cells[k],b);
        if(ChordReach(a,b,ps->origin,MOVE_PATH_REACH))return k-1;
    }
    return index;
}

// The steering point: the lookahead distance along the path from the
// progress index, cutting corners as the navmesh waypoint did, so a
// diagonal corridor is walked straight; or, when asked, the next corner,
// so the chord lies along linked cells. The corridor judges the chord
// either way. It is clamped at the takeoff cell of the next edge. A hop,
// drop or jump edge without a committed plan is a stop (the planner
// commits one on the approach or the search disables the edge); a pad is
// walked onto.
// bot_moveDropPush (units; 16 unless set, 0 off): a walked cell beside a drop, a
// liquid or a hurt volume is steered at a point moved that far inward from
// each refused side whose opposite side is linked floor. The cells sit on a
// fixed 32-unit world grid, so how far a cell centre is from a lip depends on
// where the grid falls: on Kejim the grid falls 8 units differently on the two
// sides of the map, and blue's route along the chasm ledge steered 8 units
// nearer the lip than red's mirror, braked for the drop and lost 2.5 s
// (docs/movement/records/2026-09-24-respawn4-mirror.md). At 16 units, 324
// pinned floor worlds: no death, 12 pairs faster, 2 slower, -12.1 s.
static float dropPush=-1;
static void DropPushPoint(int cell,vec3_t p) {
    static const int dirX[4]={1,0,-1,0},dirY[4]={0,1,0,-1};
    const graphCell_t *c;int d;
    if(dropPush<0) {
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_moveDropPush",value,sizeof(value));
        dropPush=value[0]?max(0.0f,(float)atof(value)):16;
    }
    if(dropPush<=0 || cell<0)return;
    c=Graph_Cell(cell);
    if(!c || !(c->flags&(GRAPH_HAZARD_DROP|GRAPH_HAZARD_LIQUID|GRAPH_HAZARD_HURT)))return;
    for(d=0;d<4;d++)
        if(c->link[d]<0 && (c->refused&(1<<d)) && c->link[(d+2)%4]>=0) {
            p[0]-=dirX[d]*dropPush;p[1]-=dirY[d]*dropPush;
        }
}

static void PathTarget(const bot2_moveContext_t *ctx,int index,const playerState_t *ps,vec3_t target,float *stop,int stopAtCorner) {
    const bot2_movePath_t *path=ctx->path;vec3_t a,b;float remaining=MOVE_WALK_LOOKAHEAD,walked=0,length;int k,sdx=0,sdy=0;
    *stop=-1;
    CellPoint(path->cells[index],a);
    if(!(index+1<path->count && PathStrafe(path,index+1)))DropPushPoint(path->cells[index],a);
    if(index+1<path->count && PathStrafe(path,index+1)) {
        // On a strafe chord the progress cell is its takeoff, which a bot
        // along the chord has left behind: the lookahead is measured from
        // the bot's own projection onto the chord, never from the takeoff,
        // or the target falls behind the bot and the walk turns back.
        float dx,dy,len2,t;
        CellPoint(path->cells[index+1],b);
        dx=b[0]-a[0];dy=b[1]-a[1];len2=dx*dx+dy*dy;
        t=len2>0?max(0.0f,min(1.0f,((ps->origin[0]-a[0])*dx+(ps->origin[1]-a[1])*dy)/len2)):0;
        a[0]+=dx*t;a[1]+=dy*t;a[2]+=(b[2]-a[2])*t;
    }
    VectorCopy(a,target);
    for(k=index+1;k<path->count;k++) {
        const graphCell_t *prev=Graph_Cell(path->cells[k-1]),*next=Graph_Cell(path->cells[k]);
        int dx=next->ix-prev->ix,dy=next->iy-prev->iy;
        if(stopAtCorner && k>index+1 && (path->edges[k]<0 || PathStrafe(path,k)) && (dx!=sdx || dy!=sdy))return;   // a corner: turn at the cell before it
        sdx=dx;sdy=dy;
        // A strafe chord further ahead ends the lookahead at its takeoff
        // cell: the chord is a flight, walked through only from its takeoff
        // (where it is validated or refused), never aimed at from before.
        // The takeoff is a gate the walk passes at speed, not a destination
        // it arrives at.
        if(k>index+1 && PathStrafe(path,k))return;
        if(path->edges[k]>=0 && !PathStrafe(path,k)) {
            const graphEdge_t *e=Graph_Edge(path->edges[k]);
            if(e && e->type!=GRAPH_EDGE_PAD) {
                float dx=target[0]-ps->origin[0],dy=target[1]-ps->origin[1];
                *stop=sqrtf(dx*dx+dy*dy);
            }
            return;
        }
        CellPoint(path->cells[k],b);
        if(path->edges[k]<0)DropPushPoint(path->cells[k],b);
        length=sqrtf((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1]));
        if(walked+length>=remaining) {
            float f=length>0?(remaining-walked)/length:0;
            target[0]=a[0]+(b[0]-a[0])*f;target[1]=a[1]+(b[1]-a[1])*f;target[2]=a[2]+(b[2]-a[2])*f;
            return;
        }
        walked+=length;VectorCopy(b,a);VectorCopy(b,target);
    }
    // The path ends at the objective's cell; finish at the objective itself.
    if(Distance(target,ctx->goal)<=64)VectorCopy(ctx->goal,target);
}

// The forbidden volume (ForbidSync): the walk cuts corners toward a target up to
// MOVE_WALK_LOOKAHEAD along the path, so a route that goes around the box could still be walked across its
// corner. A target whose chord enters the box is replaced by the next corner, then by the next cell, then by
// the reached cell (whose route steps the planner kept out of the box).
static void ForbidTarget(const bot2_moveContext_t *ctx,int index,const playerState_t *ps,vec3_t target,float *stop) {
    vec3_t mins,maxs,p;float cornerStop;int k;
    if(!Bot2_ForecastForbidGet(ctx->client,mins,maxs) || !Bot2_SegmentEntersBox(ps->origin,target,mins,maxs))return;
    PathTarget(ctx,index,ps,p,&cornerStop,1);
    if(!Bot2_SegmentEntersBox(ps->origin,p,mins,maxs)) {VectorCopy(p,target);*stop=cornerStop;return;}
    for(k=index+1;k>=index;k--) {
        if(k>=ctx->path->count || (k>index && ctx->path->edges[k]>=0))continue;
        CellPoint(ctx->path->cells[k],p);
        if(!Bot2_SegmentEntersBox(ps->origin,p,mins,maxs) || k==index) {VectorCopy(p,target);return;}
    }
}

// One telemetry schema for every movement event.
static void Event(int client,bot2_moveEvent_t event,const char *edge,const char *reason,const char *detail) {
    movement_t *m=&movement[client];
    const playerState_t *ps=&g_entities[client].client->ps;
    m->lastEvent.event=event;m->lastEvent.time=level.time;
    Q_strncpyz(m->lastEvent.reason,reason,sizeof(m->lastEvent.reason));
    Bot2_PrintTelemetry(1,"[MOVE] client=%d level=%d event=%s edge=%s reason=%s speed=%.1f force=%d speed_ms=%d x=%.1f y=%.1f z=%.1f%s%s\n",
        client,level.time,EventName(event),edge,reason,
        sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]),
        ps->fd.forcePower,max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time),
        ps->origin[0],ps->origin[1],ps->origin[2],detail?" ":"",detail?detail:"");
}

static float Speed2D(const playerState_t *ps) {
    return sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]);
}

// A permit is a rollout of the walk from the state it was granted in. A
// plan decision after the grant (a committed sequence, a Speed purchase)
// changes the state the walk reaches the links with, so the grant no
// longer describes that walk: the permit is revoked, the corridor's
// refusal returns and the gap search rolls the links out again from the
// real state. Found on Kejim at level 3, where a permit granted 700 units
// before a corner, followed by a straight hop, walked the bot to a stop
// two units west of the opening.
static void PermitRevoke(int client,const char *why) {
    movement_t *m=&movement[client];
    if(!m->permit.active)return;
    Event(client,MOVE_EVENT_EDGE,"permit","revoke",va("id=%d why=%s",m->permit.id,why));
    m->permit.active=0;
}

// Corridor refusals that are a fall or a hazard, not a solid.
static int MomentumHazard(const char *why) {
    return !strcmp(why,"drop") || !strcmp(why,"floor_missing") || !strcmp(why,"floor_liquid") ||
           !strcmp(why,"sweep_liquid") || !strcmp(why,"floor_steep") || strstr(why,"hurt")!=NULL;
}

static float Wishspeed(const playerState_t *ps) {
    return ps->basespeed*((ps->fd.forcePowersActive&(1<<FP_SPEED))?1.7f:1.0f);
}

// The walk. Reads the supplied player state, its continuation state and the
// world through traces and the navmesh; never the live clock, never the live
// entity's player state. Returns 1 with a command, 0 declined.
// Row 5 diagnostic: inside a profiled rollout the executor's parts are timed.
#define POLICY_TIME_BEGIN double polBegan=Bot2_ForecastProfiling()?Bot2_MoveCostClock():0
#define POLICY_TIME_END(section) if(polBegan>0)Bot2_MoveCostSection(section,polBegan)

// bot_moveFlightMode: how a flight is flown. 0 tracks a velocity exactly on
// the hop's heading, frame by frame; it can only hold that line by
// alternating opposite key pairs with the view parked across travel, which
// adds about a third of the speed strafing can. 1 holds one side's forward
// diagonal at the speed-best view and changes side when the travel has
// drifted MOVE_FLIGHT_BAND degrees past the heading, sweeping the view at the
// ordinary turn rate: the path weaves a few degrees about the heading and
// the speed builds near the per-frame optimum. A flight more than
// MOVE_FLIGHT_TRACK degrees off its heading is steered as before. Read once
// a frame in Move_Command, so every rollout of the frame sees one value.
#define MOVE_FLIGHT_BAND 4.0f
#define MOVE_FLIGHT_TRACK 20.0f
// bot_moveFlightSpeed: the held side only at or above this horizontal speed
// (0, the default, at every speed); slower flights track their heading.
//
// Mode 2 flies a hop the way a human strafe-jumps it: one side for the whole
// flight, chosen at takeoff as the side that turns the travel toward the
// heading, so consecutive hops alternate sides by themselves. Mode 1's 4
// degree band changes side every ten or so frames at 500 u/s, and each change
// swings the view 2*(theta-45) degrees (about 32 at 500 u/s) through views
// that add no speed; that swing, not the 25 ms cadence, is what held mode 1
// near 75-85 percent. Mode 2 changes side in flight only when the travel
// overshoots the heading by bot_moveFlightBand degrees (default 12), never
// falls back to the heading-tracking law (whose alternating opposite key
// pairs are the visible jitter), and turns the view at bot_moveFlightTurn
// degrees a second in flight (default 360). A walk frame in the air faster
// than the wishspeed holds the side toward its target the same way instead of
// back-pedalling toward the walk's wishspeed, under bot_moveWalkKeep only (see
// WalkStep: the unvalidated walk must shed speed). Mode 2 is the default: on Kejim
// it changes keys under once a second in flight against 20 for mode 0, and
// two floor pairs were 36/36 with level 1 1.4 s a case faster
// (docs/movement/records/2026-09-23-flight-mode-2.md).
// bot_moveCircleRunup (default on; 0 turns it off): a circle run-up. A hop whose
// run-up is MOVE_CIRCLE_RUNUP or longer asks the controller for
// MOVE_CIRCLE_PUSH above the speed it has, so the ground frames turn the
// wish direction off travel (a strafe diagonal) and gain past the ground
// cap instead of settling on it; and a search from at or below ground speed
// offers such run-ups. A 7,825 ms level-2 human run left the stand at
// 471 u/s, over Force Speed's 425 ground cap; the bot left at 369.
#define MOVE_CIRCLE_RUNUP 64
#define MOVE_CIRCLE_PUSH 150.0f
static const int circleRunups[]={96,160};
static int flightMode=-1,lipGuard=1,runupKeep=1,flightLine=1,walkKeep=0,walkKeepPath=1,circleRunup=0;
// Fixes, one cvar each (bot_moveFixBM5 ... bot_moveFixBX10), default 1; 0 restores the earlier behaviour.
// Latched with the flight mode (MOVE_FIX), so rollouts and the live step agree.
static int fixBM5,fixBM7,fixBM15,fixBX3,fixBX4,fixBX5,fixBX10;
static void FlightModeRead(void);
#define MOVE_FIX(id) ((flightMode<0?FlightModeRead():(void)0),fix##id)
// Later fixes: bot_moveFixBM8, bot_moveFixBM9, default 0 (off: today's behaviour); read
// with the batch-2 flags above through MOVE_FIX, but on only when the cvar is set above 0.
static int fixBM8,fixBM9;
// bot_moveFixBM8 (default 0): the executor's frame-count bounds were written as frames of
// the 25 ms decision step. With the fix they are the same time - frames x 25 ms - divided by the actual step
// (stepMsec, or Bot2_GameStepMsec() for 0), rounded, at least 1 for a positive count: identical at sv_fps 40.
// Off, or at a 25 ms step, the count is returned unchanged.
#define MOVE_FRAME_MSEC 25
static int MoveFrames(int frames,int stepMsec) {
    if(!MOVE_FIX(BM8) || frames<=0)return frames;
    if(stepMsec<=0)stepMsec=Bot2_GameStepMsec();
    if(stepMsec==MOVE_FRAME_MSEC)return frames;
    return max(1,(frames*MOVE_FRAME_MSEC+stepMsec/2)/stepMsec);
}
// Tuning (test): the push above live speed and a scale on circleRunups.
static float circlePush=MOVE_CIRCLE_PUSH,circleScale=1;
static float flightSpeed,flightBand=12,flightTurn=360;
// bot_moveStrafeSkill (percent; 90 unless set): the held-side air strafe's
// share of perfect speed gain (bot2_moveIntent_t.strafeSkill). Read once with
// the flight mode, so forecasts and execution agree.
static float strafeSkill=.9f;
// The step's other cvars: bot_defenderStrafe (StepGuest), bot_moveGuestBrake (GuestBrakeOn),
// bot_moveEdgeHull (EdgeProbe) and bot_moveLandingJump (LandingJump), parsed as they were read live. Latched
// here, and lazily once per level.time (StepCvars) for a caller that reaches the step before any Move_Command
// of the frame, so every rollout of the frame and the live step see one value.
static int stepCvarTime=-1,stepGuestStrafe,stepGuestBrake=1,stepLandingJump=1;
static float stepEdgeHull=4;
// bot_moveFixBM8 (default 0): the Pmove slice of a bot that steps fixed (Bot2_PmoveFixed,
// pmove_msec seconds), else 0; latched with the step's cvars. Used only with the fix on (StrafeSlice).
static float stepPmoveSlice;
static void StepCvarsRead(void) {
    char value[16];
    stepGuestStrafe=trap->Cvar_VariableIntegerValue("bot_defenderStrafe");
    trap->Cvar_VariableStringBuffer("bot_moveGuestBrake",value,sizeof(value));
    stepGuestBrake=!value[0] || atoi(value)!=0;
    trap->Cvar_VariableStringBuffer("bot_moveEdgeHull",value,sizeof(value));
    stepEdgeHull=value[0]?(float)atof(value):4;
    trap->Cvar_VariableStringBuffer("bot_moveLandingJump",value,sizeof(value));
    stepLandingJump=!value[0] || atoi(value)!=0;   // on unless set to 0
    stepPmoveSlice=Bot2_PmoveFixed() && pmove_msec.integer>0?pmove_msec.integer*.001f:0;   // bot_moveFixBM8
    stepCvarTime=level.time;
}
static void StepCvars(void) {if(stepCvarTime!=level.time)StepCvarsRead();}
// MoveGuest on the latched bot_defenderStrafe, for the step.
static int StepGuest(int client) {StepCvars();return Bot2_DefenderIs(client) && stepGuestStrafe;}
// bot_moveFixBM8 (default 0): the held-side strafe's slice (bot2_moveIntent_t.strafeSlice);
// 0 with the fix off or a bot that does not step fixed, which keeps the command's dt.
static float StrafeSlice(void) {if(!MOVE_FIX(BM8))return 0;StepCvars();return stepPmoveSlice;}
static void FlightModeRead(void) {
    char value[16];
    StepCvarsRead();
    trap->Cvar_VariableStringBuffer("bot_moveLipGuard",value,sizeof(value));
    lipGuard=value[0]?atoi(value)!=0:1;
    trap->Cvar_VariableStringBuffer("bot_moveFlightMode",value,sizeof(value));
    flightMode=value[0]?atoi(value):2;   // mode 2 unless set
    trap->Cvar_VariableStringBuffer("bot_moveFlightSpeed",value,sizeof(value));
    flightSpeed=value[0]?(float)atof(value):0;
    trap->Cvar_VariableStringBuffer("bot_moveFlightBand",value,sizeof(value));
    flightBand=value[0]?(float)atof(value):12;
    trap->Cvar_VariableStringBuffer("bot_moveFlightTurn",value,sizeof(value));
    flightTurn=value[0]?(float)atof(value):360;
    if(flightTurn<90)flightTurn=90;
    trap->Cvar_VariableStringBuffer("bot_moveRunupKeep",value,sizeof(value));
    runupKeep=value[0]?atoi(value)!=0:1;
    trap->Cvar_VariableStringBuffer("bot_moveWalkKeep",value,sizeof(value));
    walkKeep=value[0]?atoi(value)!=0:1;   // default on, with the long momentum chord
    trap->Cvar_VariableStringBuffer("bot_moveWalkKeepPath",value,sizeof(value));
    walkKeepPath=value[0]?atoi(value)!=0:1;
    trap->Cvar_VariableStringBuffer("bot_moveFlightLine",value,sizeof(value));
    flightLine=value[0]?atoi(value)!=0:1;
    trap->Cvar_VariableStringBuffer("bot_moveCircleRunup",value,sizeof(value));
    circleRunup=value[0]?atoi(value)!=0:1;   // default on
    trap->Cvar_VariableStringBuffer("bot_moveCirclePush",value,sizeof(value));
    circlePush=value[0]?(float)atof(value):MOVE_CIRCLE_PUSH;
    trap->Cvar_VariableStringBuffer("bot_moveCircleScale",value,sizeof(value));
    circleScale=value[0]?(float)atof(value):1;
    trap->Cvar_VariableStringBuffer("bot_moveStrafeSkill",value,sizeof(value));
    strafeSkill=value[0]?(float)atof(value)/100:.9f;
    if(strafeSkill<.05f)strafeSkill=.05f;
    fixBM5=FixOn("bot_moveFixBM5")!=0;fixBM7=FixOn("bot_moveFixBM7")!=0;
    fixBM15=FixOn("bot_moveFixBM15")!=0;fixBX3=FixOn("bot_moveFixBX3")!=0;
    fixBX4=FixOn("bot_moveFixBX4")!=0;fixBX5=FixOn("bot_moveFixBX5")!=0;
    fixBX10=FixOn("bot_moveFixBX10")!=0;
    // Default 0, so unset is off (not FixOn).
    fixBM8=trap->Cvar_VariableIntegerValue("bot_moveFixBM8")>0;
    fixBM9=trap->Cvar_VariableIntegerValue("bot_moveFixBM9")>0;
}

// Mode 2's held side for one airborne frame toward a heading (degrees).
// A new flight - the first airborne frame after a grounded one, or a launch
// inside the frame (vertical velocity rose, which gravity never does) -
// takes the side that turns the travel toward the heading.
static int FlightSide(const playerState_t *ps,bot2_moveExecutor_t *x,float heading) {
    float drift=Wrap(atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI-heading);
    // Strafing right turns the travel clockwise (drift falls).
    if(ps->velocity[2]>x->sideVz+100 || !x->side)x->side=drift>0?1:-1;
    else if(x->side>0 && drift<-flightBand)x->side=-1;
    else if(x->side<0 && drift>flightBand)x->side=1;
    x->sideVz=ps->velocity[2];
    return x->side;
}

// A planned drop's run-up (bot_moveDropPlan) aims this far or a quarter second ahead on its line.
#define MOVE_DROP_LOOK 64.0f

// bot_moveGuestBrake (default on; guests only, the runner's walk is
// untouched). Two thirds of chase D's remaining falls on Kejim (build h,
// ambush on) left the walkway from the walk or a hold, not a hop
// (records/2026-09-30-defender-falls-and-route.md, "Chase D's walk at lips"):
// - At Force Speed the momentum chord (0.1 s of travel, 43 u at 428 u/s) is
//   shorter than the distance the bot needs to stop (about 70 u), so the brake
//   came when the lip could no longer be kept. A guest's chord covers its
//   stopping distance: ground friction (6 a second above 100 u/s) plus
//   MOVE_GUEST_BRAKE_MARGIN.
// - Stopped at a lip that is the takeoff of its only onward hops, too slow to
//   take them, a guest walked back to the lip after every replan and crept over
//   it 2 u a cycle. After a hazard brake a guest backs away from the lip for
//   MOVE_GUEST_RETREAT_MS and plans again from there.
#define MOVE_GUEST_BRAKE_MARGIN 24.0f
#define MOVE_GUEST_RETREAT_MS 300
#define MOVE_GUEST_RETREAT_SPEED 150.0f
static int GuestBrakeOn(void) {StepCvars();return stepGuestBrake;}
static float GuestStopDistance(float v) {
    return (v>100?(v-100)/6.0f+100*100/1200.0f:v*v/1200.0f)+MOVE_GUEST_BRAKE_MARGIN;
}

// bot_moveEdgeHull H (default 4; 0 off). The corridor's floor samples trace the whole
// 30-unit hull, so a chord is floored while any part of the hull is over floor, and the
// momentum brake heard of an edge only once the bot's centre was up to 15 u past it.
// Base D's Kejim falls (4v4 worlds): running down the red ramp beside the
// pit, or cutting the walkway's inside corner on a walk-only route, its brake fired at
// the stopping distance but against an edge 15 u too far out; the centre crossed the lip,
// the hull slid off the ramp's edge, and the hold that followed could not climb back.
// With H > 0 the momentum chord is also sampled with a hull H units wide each side, so
// the brake measures its stopping distance to where the centre leaves the floor - but
// only on a planned route with no crossing in its next MOVE_EDGE_CROSSING_AHEAD entries.
// A walk toward a planned crossing is a run-up to a lip it means to leave from: probed on
// every walk, the runner braked at its own hop's takeoff each frame until it fell
// (blue_out_L1_respawn4, records/2026-10-05-defaults-retake-and-base-
// walkway.md).
#define MOVE_EDGE_CROSSING_AHEAD 4
static int CrossingAhead(const bot2_moveContext_t *ctx,const bot2_moveExecutor_t *x) {
    int k;
    for(k=x->pathIndex+1;k<ctx->path->count && k<=x->pathIndex+MOVE_EDGE_CROSSING_AHEAD;k++)
        if(ctx->path->edges[k]>=0)return 1;
    return 0;
}
static const char *EdgeProbe(const gentity_t *ent,const vec3_t start,const vec3_t end) {
    float h;vec3_t mins,maxs,previous,point;int i,hit;const char *value;
    StepCvars();h=stepEdgeHull;
    if(h<=0)return NULL;
    VectorSet(mins,-h,-h,ent->r.mins[2]);VectorSet(maxs,h,h,ent->r.maxs[2]);
    VectorCopy(start,previous);previous[2]+=.125f;
    for(i=1;i<=4;i++) {
        VectorSubtract(end,start,point);VectorMA(start,i*.25f,point,point);
        value=Bot2_GroundSample(ent->s.number,mins,maxs,previous,point,i,&hit);
        if(value)return value;
        VectorCopy(point,previous);
    }
    return NULL;
}

// The guest momentum brake's test, for a guest layer that writes its own command
// instead of the walk's: the defender's saber walk (Melee, bot2_defender.c), which
// replaced the walk's command and with it the brake. It walked at a carrier with a
// full-hull floor trace 48 u ahead along its heading and released the keys at the
// lip; at chase speed it slid over, base D's commonest Kejim fall
// (records/2026-10-06-base-walkway-goals.md). 2 when the floor ends (a hazard
// refusal of the corridor, or of the edge probe) within the distance the bot needs
// to stop along its velocity, 1 when it ends within that distance, or 48 u, along
// yaw, else 0. Grounded bots only.
int Move_GuestEdgeAhead(int client,float yaw) {
    gentity_t *ent=&g_entities[client];const playerState_t *ps=&ent->client->ps;
    float v=Speed2D(ps),along=GuestStopDistance(v);int k;
    if(ps->groundEntityNum==ENTITYNUM_NONE)return 0;
    for(k=0;k<2;k++) {
        vec3_t carry,standing;float dx,dy,len;const char *why="none";int at=0;
        if(!k) {
            if(v<=MOVE_MOMENTUM_SLOW)continue;
            dx=ps->velocity[0]/v;dy=ps->velocity[1]/v;len=along;
        } else {dx=cosf(DEG2RAD(yaw));dy=sinf(DEG2RAD(yaw));len=max(along,48);}
        VectorSet(carry,ps->origin[0]+dx*len,ps->origin[1]+dy*len,ps->origin[2]);
        if((!Bot2_GroundCorridor(ent,ps->origin,carry,standing,&why,&at) && MomentumHazard(why)) ||
           ((why=EdgeProbe(ent,ps->origin,carry))!=NULL && MomentumHazard(why)))return k?1:2;
    }
    return 0;
}

// The same test for a guest's straight walk onto a point (a defender's last few units to
// an item, a mine spot or a flag, which replace the walk's command as the saber walk did): 2 when the floor ends
// within the stopping distance along the velocity (the brake), 1 when the chord to `to` itself is a hazard refusal
// (the point is not walkable straight), else 0. Unlike Move_GuestEdgeAhead it does not look past the point: a
// flag or an item lying near a lip must stay reachable. Grounded bots only.
int Move_GuestReachAhead(int client,const vec3_t to) {
    gentity_t *ent=&g_entities[client];const playerState_t *ps=&ent->client->ps;
    float v=Speed2D(ps);vec3_t carry,standing,end;const char *why="none";int at=0;
    if(ps->groundEntityNum==ENTITYNUM_NONE)return 0;
    if(v>MOVE_MOMENTUM_SLOW) {
        float along=GuestStopDistance(v);
        VectorSet(carry,ps->origin[0]+ps->velocity[0]/v*along,ps->origin[1]+ps->velocity[1]/v*along,ps->origin[2]);
        if((!Bot2_GroundCorridor(ent,ps->origin,carry,standing,&why,&at) && MomentumHazard(why)) ||
           ((why=EdgeProbe(ent,ps->origin,carry))!=NULL && MomentumHazard(why)))return 2;
    }
    VectorSet(end,to[0],to[1],ps->origin[2]);why="none";
    if((!Bot2_GroundCorridor(ent,ps->origin,end,standing,&why,&at) && MomentumHazard(why)) ||
       ((why=EdgeProbe(ent,ps->origin,end))!=NULL && MomentumHazard(why)))return 1;
    return 0;
}

static int WalkStep(const playerState_t *ps,const bot2_moveContext_t *ctx,bot2_moveExecutor_t *x,
                    int serverTime,usercmd_t *cmd,const char **reason) {
    gentity_t *ent=&g_entities[ctx->client];
    NavMeshWaypoint_t wp;vec3_t target,delta,end,standing;bot2_moveIntent_t intent;
    float dist,goalDist,along,speed,nextYaw,dt,stop=-1;int decline=MOVE_DECLINE_NONE,sample=0,onPath=0,keep;
    const char *corridor="none";
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=serverTime;cmd->weapon=ps->weapon;
    *reason="none";x->offmeshType=0;x->corridorSample=0;x->permitted=0;x->held=0;
    if(ps->pm_type!=PM_NORMAL)decline=MOVE_DECLINE_MOVEMENT_TYPE;
    else if(bot2_states[ctx->client].state!=BOT_STATE_WALK)decline=MOVE_DECLINE_LEGACY_STATE;
    else {
        // The planned path steers when the bot is on it; the navmesh
        // waypoint steers otherwise, as it did before the graph existed.
        if(ctx->path && ctx->path->count) {
            {POLICY_TIME_BEGIN;
            x->pathIndex=PathProgress(ps,ctx->path,x->pathIndex);
            POLICY_TIME_END(BOT2_MOVECOST_SECTION_POL_PROGRESS);}
            if(x->pathIndex>=0) {onPath=1;PathTarget(ctx,x->pathIndex,ps,target,&stop,0);ForbidTarget(ctx,x->pathIndex,ps,target,&stop);}
        } else x->pathIndex=-1;
        if(!onPath) {
            int found;
            {POLICY_TIME_BEGIN;
            found=NavMesh_GetNextWaypointEx(ctx->client,ps->origin,ctx->goal,&wp,MOVE_OFFMESH_AVOID);
            POLICY_TIME_END(BOT2_MOVECOST_SECTION_POL_NAVMESH);}
            if(!found)decline=MOVE_DECLINE_NO_PATH;
            else {
                float dx=wp.offmeshStart[0]-ps->origin[0],dy=wp.offmeshStart[1]-ps->origin[1];
                if(wp.offmeshType && dx*dx+dy*dy<MOVE_OFFMESH_YIELD*MOVE_OFFMESH_YIELD) {
                    decline=MOVE_DECLINE_OFFMESH;x->offmeshType=wp.offmeshType;
                }
                VectorCopy(wp.position,target);
            }
        }
    }
    if(!decline) {
        VectorSubtract(target,ps->origin,delta);
        dist=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]);
        along=min(dist,MOVE_WALK_LOOKAHEAD);
        if(dist>1) {
            end[0]=ps->origin[0]+delta[0]*along/dist;end[1]=ps->origin[1]+delta[1]*along/dist;
            end[2]=ps->origin[2]+delta[2]*along/dist;
        } else VectorCopy(ps->origin,end);
        // Airborne frames keep steering; the corridor is a grounded check.
        if(dist>1 && ps->groundEntityNum!=ENTITYNUM_NONE &&
           !Bot2_GroundCorridor(ent,ps->origin,end,standing,&corridor,&sample)) {
            int fallback=0;
            if(onPath) {
                // The chord cut a corner the links never validated, or the
                // corridor refused a link the fill admitted. Nearer targets
                // along linked cells first: the corner cell, the reached
                // cell's successor, then the reached cell itself, which the
                // bot is beside but may not have crossed.
                vec3_t corner;float cornerStop,pathStop=stop;int k;
                PathTarget(ctx,x->pathIndex,ps,corner,&cornerStop,1);
                if(corner[0]!=target[0] || corner[1]!=target[1]) {
                    VectorCopy(corner,target);stop=cornerStop;
                    VectorSubtract(target,ps->origin,delta);
                    dist=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]);
                    if(dist>1 && Bot2_GroundCorridor(ent,ps->origin,target,standing,&corridor,&sample))fallback=1;
                }
                // A refused corner leaves its stop behind: the cell targets
                // below brake for the path's own stop, the edge takeoff the
                // whole target ran to.
                if(!fallback)stop=pathStop;
                for(k=x->pathIndex+1;k>=x->pathIndex && !fallback;k--) {
                    if(k>=ctx->path->count || (k>x->pathIndex && ctx->path->edges[k]>=0))continue;
                    CellPoint(ctx->path->cells[k],target);
                    VectorSubtract(target,ps->origin,delta);
                    dist=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]);
                    if(dist>1 && Bot2_GroundCorridor(ent,ps->origin,target,standing,&corridor,&sample))fallback=1;
                }
            }
            if(!fallback && onPath && ctx->permit && ctx->permit->active && ctx->permit->pathTime==ctx->path->time &&
               x->pathIndex>=ctx->permit->from && x->pathIndex<=ctx->permit->to && x->pathIndex+1<ctx->path->count &&
               ctx->path->edges[x->pathIndex+1]<0) {
                // The link is under a permit: a rollout of this walk along it
                // from the live state admitted it, so the corridor's refusal
                // of the chord does not decline the frame.
                CellPoint(ctx->path->cells[x->pathIndex+1],target);
                VectorSubtract(target,ps->origin,delta);
                dist=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]);
                fallback=1;x->permitted=1;
            }
            if(!fallback) {
                // The refused target is kept: gap discovery heads for it.
                decline=MOVE_DECLINE_CORRIDOR;*reason=corridor;x->corridorSample=sample;VectorCopy(target,x->waypoint);
            }
        }
    }
    if(decline) {
        if(decline!=MOVE_DECLINE_CORRIDOR)*reason=DeclineName(decline);
        x->decline=decline;x->walking=0;x->lastTime=serverTime;
        return 0;
    }
    // Run at the live wishspeed toward the waypoint; brake only at the
    // objective, so an intermediate corner is passed, not arrived at.
    goalDist=sqrtf((ctx->goal[0]-ps->origin[0])*(ctx->goal[0]-ps->origin[0])+
                   (ctx->goal[1]-ps->origin[1])*(ctx->goal[1]-ps->origin[1]));
    // bot_moveWalkKeep (default on): the walk asks for the speed it has
    // instead of its wishspeed; 0 brakes toward the wishspeed as it always
    // did. The walk is the one movement no rollout validates: kept at 850 u/s
    // with the 96-unit momentum chord it ran up a rise on Kejim, launched off
    // the top, crossed the chasm into the far wall and fell
    // (blue_out_L2_respawn5), so keeping speed lengthens the
    // momentum chord to a quarter second of travel (below).
    if(flightMode<0)FlightModeRead();
    // bot_moveWalkKeepPath (default on; 0 off): only a walk on the planned path
    // keeps its speed. Off it, steering by the navmesh with no route behind
    // it, the kept speed and the air strafe below ran a carrier off Dantooine's
    // flag platform at 526 to 707 u/s after the pickup reset its path
    // (blue_ret_L3_respawn4; records/2026-09-25-arrive-safe.md).
    keep=walkKeep && (onPath || !walkKeepPath);
    speed=keep?max(Wishspeed(ps),Speed2D(ps)):Wishspeed(ps);
    if(max(0,(goalDist-16)*4)<speed)speed=max(0,(goalDist-16)*4);
    if(stop>=0)speed=min(speed,max(0,(stop-16)*4));
    // The corridor above runs along the command. The bot's momentum runs
    // along its velocity for the frames a turn takes, and floor missing
    // there is a drop no command avoids: found on Kejim at level 3, where a
    // walk from a hop landing beside a ramp ran off the ramp's side into the
    // pit while the chord to the path's target on the ramp was clear. A
    // momentum chord refused for a drop, a hazard or a hurt volume brakes
    // the frame; a solid refuses nothing, since a wall stops the bot itself.
    if(ps->groundEntityNum!=ENTITYNUM_NONE) {
        float v=Speed2D(ps),along=min(v*MOVE_MOMENTUM_TIME,MOVE_WALK_LOOKAHEAD);int check=v>MOVE_MOMENTUM_MIN;
        if(flightMode<0)FlightModeRead();
        if(lipGuard && v>MOVE_MOMENTUM_SLOW && along<MOVE_MOMENTUM_REACH) {check=1;along=MOVE_MOMENTUM_REACH;}
        // A walk that keeps its speed must see as far as it takes to shed it:
        // a quarter second of travel, up to MOVE_MOMENTUM_KEEP_REACH, instead
        // of the 96 units the braking walk needs. It is the speed the bot has
        // that needs the reach, kept or not: gated on bot_moveWalkKeepPath's
        // keep, an off-path walk at 451 u/s saw 96 units and went over
        // Carratos's stand ledge twice on the way home.
        if(walkKeep && v>Wishspeed(ps))along=max(along,min(v*MOVE_MOMENTUM_KEEP_TIME,MOVE_MOMENTUM_KEEP_REACH));
        if(StepGuest(ctx->client) && GuestBrakeOn() && v>MOVE_MOMENTUM_SLOW) {check=1;along=max(along,GuestStopDistance(v));}
        if(check) {
            vec3_t carry;const char *why="none";int at=0,hazard;
            carry[0]=ps->origin[0]+ps->velocity[0]/v*along;carry[1]=ps->origin[1]+ps->velocity[1]/v*along;carry[2]=ps->origin[2];
            hazard=!Bot2_GroundCorridor(ent,ps->origin,carry,standing,&why,&at) && MomentumHazard(why);
            if(!hazard && onPath && !CrossingAhead(ctx,x)) {
                const char *edge=EdgeProbe(ent,ps->origin,carry);
                if(edge && MomentumHazard(edge)) {why=edge;hazard=1;}
            }
            if(hazard) {
                speed=0;*reason=why;x->braked=1;
                if(StepGuest(ctx->client) && GuestBrakeOn()) {
                    x->retreatUntil=serverTime+MOVE_GUEST_RETREAT_MS;
                    x->retreatDir[0]=-ps->velocity[0]/v;x->retreatDir[1]=-ps->velocity[1]/v;
                }
            }
            else x->braked=0;
        } else x->braked=0;
    } else x->braked=0;
    memset(&intent,0,sizeof(intent));intent.turnRate=360;intent.allowStop=qtrue;
    if(dist>1) {intent.velocity[0]=delta[0]/dist*speed;intent.velocity[1]=delta[1]/dist*speed;}
    if(StepGuest(ctx->client) && GuestBrakeOn() && serverTime<x->retreatUntil && ps->groundEntityNum!=ENTITYNUM_NONE) {
        intent.velocity[0]=x->retreatDir[0]*MOVE_GUEST_RETREAT_SPEED;intent.velocity[1]=x->retreatDir[1]*MOVE_GUEST_RETREAT_SPEED;
    }
    if(flightMode<0)FlightModeRead();
    if(flightMode==2 && keep && dist>1 && ps->groundEntityNum==ENTITYNUM_NONE && speed>=Wishspeed(ps) && Speed2D(ps)>Wishspeed(ps)) {
        intent.strafeSide=FlightSide(ps,x,atan2f(delta[1],delta[0])*180/M_PI);intent.strafeSkill=strafeSkill;intent.strafeSlice=StrafeSlice();   // bot_moveFixBM8 (default 0)
        intent.turnRate=flightTurn;
    }
    // A walk that stands at its own target away from the objective holds:
    // the frame issues no motion and no decline, so it is named here or it
    // is invisible (a level-1 world stalled 170 s this way, silently).
    x->held=dist<=1 && goalDist>64;
    if(!x->walking)x->yaw=ps->viewangles[YAW];
    dt=(x->lastTime && serverTime>x->lastTime?serverTime-x->lastTime:ctx->stepMsec)*.001f;
    if(MOVE_FIX(BM15) && dt>2*ctx->stepMsec*.001f)dt=2*ctx->stepMsec*.001f;   // bot_moveFixBM15: no whole-arc dt
    {POLICY_TIME_BEGIN;
    Bot2_MoveControl(ps,&intent,x->yaw,dt,qtrue,cmd,&nextYaw);
    POLICY_TIME_END(BOT2_MOVECOST_SECTION_POL_CONTROL);}
    x->yaw=Wrap(nextYaw);x->lastTime=serverTime;x->walking=1;x->decline=MOVE_DECLINE_NONE;
    VectorCopy(target,x->waypoint);
    return 1;
}

// bot_moveLandingJump: a flight whose next hop taps with no run-up presses
// that tap in the command frame it lands in. The landing is a substep inside
// the 25 ms frame; with the tap pressed only in the next command, the
// frame's remaining grounded substeps pay ground friction (about 57 u/s a
// landing at 740 u/s, which caps a straight near 700). PM_WalkMove checks
// the jump before friction, and in the air a held jump is inert without a
// Force Jump flip (PM_AirMove), so the press costs nothing before touchdown.
static int LandingJump(void) {StepCvars();return stepLandingJump;}   // read in StepCvarsRead

// Whether the player box, falling on its current velocity, meets walkable
// ground within one command frame. (bot_moveFixBM9,
// default 0): LandsWithinAt also gives where the box met it (touch, when
// not NULL and it lands), the touchdown inside the frame.
static int LandsWithinAt(const playerState_t *ps,int client,int stepMsec,vec3_t touch) {
    static vec3_t mins={-15,-15,DEFAULT_MINS_2},maxs={15,15,DEFAULT_MAXS_2};
    float dt=stepMsec*.001f;vec3_t end;trace_t tr;int lands;
    end[0]=ps->origin[0]+ps->velocity[0]*dt;end[1]=ps->origin[1]+ps->velocity[1]*dt;
    end[2]=ps->origin[2]+ps->velocity[2]*dt-.5f*ps->gravity*dt*dt-.25f;
    trap->Trace(&tr,ps->origin,mins,maxs,end,client,MASK_PLAYERSOLID,qfalse,0,0);
    lands=!tr.startsolid && !tr.allsolid && tr.fraction<1 && tr.plane.normal[2]>=.7f;
    if(lands && touch)VectorCopy(tr.endpos,touch);
    return lands;
}
static int LandsWithin(const playerState_t *ps,int client,int stepMsec) {return LandsWithinAt(ps,client,stepMsec,NULL);}

// Whether this command's Pmove runs any of its substeps on the ground: the
// client stands, or touches down within the command frame.
int Move_GroundedThisFrame(int client) {
    const playerState_t *ps;
    if(client<0 || client>=MAX_CLIENTS || !g_entities[client].client)return 0;
    ps=&g_entities[client].client->ps;
    return ps->groundEntityNum!=ENTITYNUM_NONE || LandsWithin(ps,client,Bot2_GameStepMsec());
}

// One hop of the committed sequence: the run-up to a tap, then the flight.
// Stage transitions are decided from the player state alone, so the forecast
// and the live frame move through them identically.
static int HopStep(const playerState_t *ps,const bot2_moveContext_t *ctx,bot2_moveExecutor_t *x,
                   int serverTime,usercmd_t *cmd,const char **reason) {
    const bot2_moveHop_t *hop;const float *ref;bot2_moveIntent_t intent;
    float dt,nextYaw,speed;int ground=ps->groundEntityNum!=ENTITYNUM_NONE,decline=MOVE_DECLINE_NONE;
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=serverTime;cmd->weapon=ps->weapon;
    *reason="none";x->offmeshType=0;
    if(ps->pm_type!=PM_NORMAL)decline=MOVE_DECLINE_MOVEMENT_TYPE;
    else if(bot2_states[ctx->client].state!=BOT_STATE_WALK)decline=MOVE_DECLINE_LEGACY_STATE;
    else if(x->prepress) {
        // The next hop's tap was pressed ahead of the landing. Rising now,
        // the flight landed and the hop launched inside that one frame, so
        // the landing and the launch are crossed together. Still falling, it
        // has not landed yet; grounded, the tap was not taken on the landing
        // substep. Either way the flight goes on as if nothing was pressed.
        x->prepress=0;x->launchTime=0;
        if(!ground && ps->velocity[2]>0) {
            // bot_moveFixBM9 (default 0): the landing is the touchdown the pressing frame
            // traced (x->touch), not this frame's airborne point after the relaunch.
            if(MOVE_FIX(BM9))VectorCopy(x->touch,x->mark);
            else VectorCopy(ps->origin,x->mark);
            x->stage+=2;x->airborne=1;
        }
    }
    else if(x->launchTime) {
        // The jump was requested last frame. Pmove either launched it, and
        // the state is airborne, or refused it, which is a miss.
        x->launchTime=0;
        if(ground) {decline=MOVE_DECLINE_MISS;*reason="jump_refused";}
        else {x->stage++;x->airborne=1;}
    }
    else if(ctx->plan->stepOff && x->stage==MOVE_STAGE_RUNUP0 && !ground) {x->stage++;x->airborne=1;}   // stepped off the ledge
    if(!decline && (x->stage&1)) {
        if(!ground)x->airborne=1;
        else if(x->airborne) {
            // Landed. The next stage measures from here.
            VectorCopy(ps->origin,x->mark);x->airborne=0;x->stage++;
            if(x->stage==Onward(ctx->plan))x->pathIndex=LandingPathProgress(ps,ctx->path,x->pathIndex);
        }
    }
    if(decline) {
        if(decline!=MOVE_DECLINE_MISS)*reason=DeclineName(decline);
        x->decline=decline;x->walking=0;x->lastTime=serverTime;x->prepress=0;
        return 0;
    }
    if(x->stage>=Onward(ctx->plan))return -1;   // the caller walks from here
    hop=&ctx->plan->hop[x->stage>>1];
    if(!x->walking)x->yaw=ps->viewangles[YAW];
    dt=(x->lastTime && serverTime>x->lastTime?serverTime-x->lastTime:ctx->stepMsec)*.001f;
    if(MOVE_FIX(BM15) && dt>2*ctx->stepMsec*.001f)dt=2*ctx->stepMsec*.001f;   // bot_moveFixBM15
    memset(&intent,0,sizeof(intent));intent.turnRate=360;
    if(x->stage&1) {
        // Flight: ask for more speed along the heading than there is; the
        // kernel answers with the strafe angle that adds it.
        speed=Speed2D(ps)+MOVE_FLIGHT_GAIN;
        if(flightMode<0)FlightModeRead();
        // A slow drop holds its approach speed in the air: no strafe gain,
        // and the air brake takes back what the step off carried over it.
        if(ctx->plan->approach>0)speed=ctx->plan->approach;
        else if(flightMode==2 && !ground && (ps->velocity[0] || ps->velocity[1])) {
            float heading=atan2f(hop->dir[1],hop->dir[0])*180/M_PI;
            if(ps->velocity[2]>x->sideVz+100 || !x->side) {x->sideOrigin[0]=ps->origin[0];x->sideOrigin[1]=ps->origin[1];}
            if(flightLine) {
                // Steer for the hop's line, not its angle: aim half a second
                // of travel ahead on the line, so the flight weaves across it
                // instead of curving off it (dash2: whole-hop sides landed 50
                // units wide, off the path). The line is pinned where the
                // search aimed it - hop 0 through the plan's origin, hop 1
                // through its anchor - so a hop that leaned out is steered
                // back across the same centre, one side change a hop; only a
                // hop 1 with no anchor measures from where its flight began.
                const float *at=(x->stage>>1)==0?ctx->plan->origin:ctx->plan->anchored1?ctx->plan->anchor1:x->sideOrigin;
                float along=(ps->origin[0]-at[0])*hop->dir[0]+(ps->origin[1]-at[1])*hop->dir[1];
                float look=max(128.0f,Speed2D(ps)*.5f),ax,ay;
                ax=at[0]+hop->dir[0]*(along+look)-ps->origin[0];
                ay=at[1]+hop->dir[1]*(along+look)-ps->origin[1];
                heading=atan2f(ay,ax)*180/M_PI;
            }
            intent.strafeSide=FlightSide(ps,x,heading);intent.strafeSkill=strafeSkill;intent.strafeSlice=StrafeSlice();   // bot_moveFixBM8 (default 0)
            intent.turnRate=flightTurn;
        }
        if(flightMode==1 && ctx->plan->approach<=0 && !ground && (ps->velocity[0] || ps->velocity[1])) {
            float drift=Wrap(atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI-atan2f(hop->dir[1],hop->dir[0])*180/M_PI);
            // Strafing right turns the travel clockwise (drift falls).
            if(fabsf(drift)>MOVE_FLIGHT_TRACK || Speed2D(ps)<flightSpeed)x->side=0;
            else if(!x->side)x->side=drift>0?1:-1;
            else if(x->side>0 && drift<-MOVE_FLIGHT_BAND)x->side=-1;
            else if(x->side<0 && drift>MOVE_FLIGHT_BAND)x->side=1;
            intent.strafeSide=x->side;intent.strafeSkill=strafeSkill;intent.strafeSlice=StrafeSlice();   // bot_moveFixBM8 (default 0)
        }
        if(!ground && x->airborne && ps->velocity[2]<=0 && x->stage+1<Onward(ctx->plan) &&
           ctx->plan->hop[(x->stage+1)>>1].runup<=0 && LandingJump() &&
           LandsWithinAt(ps,ctx->client,ctx->stepMsec,MOVE_FIX(BM9)?x->touch:NULL)) {   // bot_moveFixBM9: keep the touchdown
            intent.upmove=127;x->prepress=1;x->launchTime=serverTime;
        }
    } else {
        // Run-up along the heading; tap once the run-up distance is covered.
        ref=x->stage==MOVE_STAGE_RUNUP0?ctx->plan->origin:x->mark;
        // bot_moveRunupKeep (default on): the run-up asks for the speed the
        // bot has, not the walk's wishspeed. Asking for the wishspeed from a
        // fast landing makes the controller brake with the back key: on
        // dash2 a 757 u/s landing fell to 485 in the two run-up frames.
        if(flightMode<0)FlightModeRead();
        speed=runupKeep?max(Wishspeed(ps),Speed2D(ps)):Wishspeed(ps);
        if(circleRunup && hop->runup>=MOVE_CIRCLE_RUNUP)speed=max(Wishspeed(ps),Speed2D(ps))+circlePush;
        if(ctx->plan->approach>0 && (ps->origin[0]-ref[0])*hop->dir[0]+(ps->origin[1]-ref[1])*hop->dir[1]>=ctx->plan->approachAt)
            speed=ctx->plan->approach;
        if(ground && (ps->origin[0]-ref[0])*hop->dir[0]+(ps->origin[1]-ref[1])*hop->dir[1]>=hop->runup) {
            if(ctx->plan->stepOff) {
                x->decline=MOVE_DECLINE_MISS;*reason="no_ledge";x->walking=0;x->lastTime=serverTime;
                return 0;
            }
            intent.upmove=127;x->launchTime=serverTime;
        }
    }
    intent.velocity[0]=hop->dir[0]*speed;intent.velocity[1]=hop->dir[1]*speed;
    if(ctx->plan->lineRunup && !(x->stage&1)) {
        // A planned drop's run-up: aim a little ahead on the crossing line,
        // so a start beside it curves in and crosses the ledge where and
        // along the heading the search chose.
        const float *at=ctx->plan->origin;
        float along=(ps->origin[0]-at[0])*hop->dir[0]+(ps->origin[1]-at[1])*hop->dir[1];
        float look=max(MOVE_DROP_LOOK,Speed2D(ps)*.25f),ax,ay,len;
        ax=at[0]+hop->dir[0]*(along+look)-ps->origin[0];
        ay=at[1]+hop->dir[1]*(along+look)-ps->origin[1];
        len=sqrtf(ax*ax+ay*ay);
        if(len>1) {intent.velocity[0]=ax/len*speed;intent.velocity[1]=ay/len*speed;}
    }
    {POLICY_TIME_BEGIN;Bot2_MoveControl(ps,&intent,x->yaw,dt,qtrue,cmd,&nextYaw);POLICY_TIME_END(BOT2_MOVECOST_SECTION_POL_CONTROL);}
    x->yaw=Wrap(nextYaw);x->lastTime=serverTime;x->walking=1;x->decline=MOVE_DECLINE_NONE;
    return 1;
}

// A committed climb: face the wall, hold forward, press jump on command
// climbJump and again climbRepress commands later; climb commands are issued
// every 8 ms by Bot2_Pmove (bot2_finecmd.c). Stage 0 until the jump leaves
// the ground, 1 while airborne, 2 (onward) once grounded on the edge's floor.
// The landing frame is judged as the search judges a rollout's end: grounded
// anywhere else - most often back at the takeoff, which the walk would never
// leave - is a miss, and the edge is charged for it.
#define MOVE_CLIMB_MS 4000   // a climb that has not landed this long after its first command is a miss (160 frames at 25 ms)
static int ClimbReached(const playerState_t *ps,const vec3_t landing) {
    float dx=ps->origin[0]-landing[0],dy=ps->origin[1]-landing[1];
    return Bot2_StandableEntity(ps->groundEntityNum) && ps->origin[2]>=landing[2]-STEPSIZE &&
           dx*dx+dy*dy<=4*GRAPH_GRID*4*GRAPH_GRID;
}
static int ClimbStep(const playerState_t *ps,const bot2_moveContext_t *ctx,bot2_moveExecutor_t *x,
                     int serverTime,usercmd_t *cmd,const char **reason) {
    const bot2_movePlan_t *plan=ctx->plan;int ground=ps->groundEntityNum!=ENTITYNUM_NONE,f=x->climbFrame;
    float yaw=atan2f(plan->hop[0].dir[1],plan->hop[0].dir[0])*180/M_PI;
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=serverTime;cmd->weapon=ps->weapon;
    *reason="none";x->offmeshType=0;
    if(ps->pm_type!=PM_NORMAL) {x->decline=MOVE_DECLINE_MOVEMENT_TYPE;*reason=DeclineName(x->decline);x->walking=0;x->lastTime=serverTime;return 0;}
    if(x->stage==MOVE_STAGE_RUNUP0 && !ground && f>plan->climbJump) {x->stage++;x->airborne=1;}
    else if(x->stage==MOVE_STAGE_RUNUP0 && ground && f>plan->climbJump+MoveFrames(2,ctx->stepMsec)) {   // bot_moveFixBM8 (default 0)
        x->decline=MOVE_DECLINE_MISS;*reason="jump_refused";x->walking=0;x->lastTime=serverTime;return 0;
    }
    if(x->stage==MOVE_STAGE_RUNUP0+1 && ground) {
        x->airborne=0;
        if(!ClimbReached(ps,plan->landing)) {
            x->decline=MOVE_DECLINE_MISS;*reason="climb_short";x->walking=0;x->lastTime=serverTime;return 0;
        }
        VectorCopy(ps->origin,x->mark);x->stage++;
        return -1;   // the caller walks from here
    }
    if(x->stage>=Onward(plan))return -1;
    if(f>=Bot2_StepsFor(MOVE_CLIMB_MS)) {x->decline=MOVE_DECLINE_MISS;*reason="climb_timeout";x->walking=0;x->lastTime=serverTime;return 0;}
    cmd->angles[YAW]=ANGLE2SHORT(yaw)-ps->delta_angles[YAW];
    cmd->angles[PITCH]=-ps->delta_angles[PITCH];
    cmd->forwardmove=127;
    if(f==plan->climbJump || f==plan->climbJump+plan->climbRepress)cmd->upmove=127;
    x->climbFrame++;x->yaw=yaw;x->lastTime=serverTime;x->walking=1;x->decline=MOVE_DECLINE_NONE;
    return 1;
}

// bot_moveKnockRecover: a hit voids the committed sequence and the boundary
// rolls out where air control can put the landing. On unless set to 0
// (after records/2026-09-24-knock-recovery.md).
static int KnockRecover(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveKnockRecover",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// bot_moveContactKnock: a committed hop whose flight Pmove clipped against
// another client is voided as a hit's is, and recovered from where the body
// left it. The rollouts model no bodies and the clip happens inside Pmove,
// where the hit test cannot see it. On unless set to 0
// (after records/2026-10-03-runner-travel-falls.md).
static int ContactKnock(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveContactKnock",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// The knock recovery's command, for the live frame and the rollout alike:
// airborne, face the heading and hold forward, the only air control Pmove
// gives (PM_AirMove, pm_airaccelerate 1: about 240 u/s a second along it), or
// no input when coasting; grounded, the walk's brake until the bot stands.
// Returns 0 once stopped: the walk takes over from where the bot stands.
#define MOVE_KNOCK_STOP 40.0f   // u/s under which a grounded bot is stood
#define MOVE_KNOCK_MAX_MS 6000  // a recovery that has not stood the bot by then ends
#define MOVE_KNOCK_DV 100.0f    // u/s of velocity change between Pmove and command that is a hit
#define MOVE_KNOCK_LIFT_MS 150  // how long a hit on the ground waits to lift the bot
static int KnockCommand(const playerState_t *ps,bot2_moveKnockPolicy_t *p,int serverTime,int stepMsec,usercmd_t *cmd) {
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=serverTime;cmd->weapon=ps->weapon;
    if(ps->groundEntityNum==ENTITYNUM_NONE) {
        p->landed=0;
        cmd->angles[PITCH]=-ps->delta_angles[PITCH];
        if(p->coast) {cmd->angles[YAW]=ANGLE2SHORT(p->yaw)-ps->delta_angles[YAW];return 1;}
        cmd->angles[YAW]=ANGLE2SHORT(p->heading)-ps->delta_angles[YAW];
        cmd->forwardmove=127;p->yaw=p->heading;
        return 1;
    }
    if(++p->landed>=MoveFrames(2,stepMsec) && Speed2D(ps)<MOVE_KNOCK_STOP)return 0;   // bot_moveFixBM8 (default 0)
    {
        bot2_moveIntent_t intent;float nextYaw;
        memset(&intent,0,sizeof(intent));intent.turnRate=360;intent.allowStop=qtrue;
        Bot2_MoveControl(ps,&intent,p->yaw,stepMsec*.001f,qtrue,cmd,&nextYaw);
        p->yaw=Wrap(nextYaw);
    }
    return 1;
}

// The executor's step: the committed sequence while one is in progress,
// the walk otherwise. Returns 1 with a command, 0 declined.
static int MoveStepBody(const playerState_t *ps,const bot2_moveContext_t *ctx,bot2_moveExecutor_t *x,
                        int serverTime,usercmd_t *cmd,const char **reason);
static int MoveStep(const playerState_t *ps,const bot2_moveContext_t *ctx,bot2_moveExecutor_t *x,
                    int serverTime,usercmd_t *cmd,const char **reason) {
    int result;POLICY_TIME_BEGIN;
    result=MoveStepBody(ps,ctx,x,serverTime,cmd,reason);
    POLICY_TIME_END(BOT2_MOVECOST_SECTION_POL_MOVESTEP);
    return result;
}

static int MoveStepBody(const playerState_t *ps,const bot2_moveContext_t *ctx,bot2_moveExecutor_t *x,
                        int serverTime,usercmd_t *cmd,const char **reason) {
    if(ps->groundEntityNum!=ENTITYNUM_NONE)x->sideVz=-1e9f;
    if(ctx->plan && ctx->plan->count && ctx->plan->kind==MOVE_PLAN_CLIMB && x->stage<Onward(ctx->plan)) {
        int climbed=ClimbStep(ps,ctx,x,serverTime,cmd,reason);
        if(climbed>=0)return climbed;
    } else if(ctx->plan && ctx->plan->count && x->stage<Onward(ctx->plan)) {
        int hopped=HopStep(ps,ctx,x,serverTime,cmd,reason);
        if(hopped>=0)return hopped;
    }
    if(ctx->plan && ctx->plan->count && x->stage==Onward(ctx->plan)) {
        float dx=ps->origin[0]-x->mark[0],dy=ps->origin[1]-x->mark[1];
        if(dx*dx+dy*dy>=MOVE_ONWARD_DISTANCE*MOVE_ONWARD_DISTANCE)x->stage=Onward(ctx->plan)+1;
    }
    return WalkStep(ps,ctx,x,serverTime,cmd,reason);
}

// The decision tape (bot_moveDecisionTape, a file under bot2-decisions/).
// Unset, nothing is loaded and every search is exactly today's.
//
// A tape is what one world committed, in order, built read-only from its own
// telemetry by tools/movement_decision_tape.py. It is not a command tape: a
// world running one still searches, rolls out and judges from its own live
// state, and still refuses what is not admissible there. All this does is
// prefer, among the candidates the judge already admitted, the one that
// matches the next entry near the bot's root. Admission, physics, cadence
// and the ranking weights are untouched; an entry whose candidate was judged
// and refused stays refused, and the refusal is the measurement.
#define MOVE_TAPE_MAX 128
// The root window an entry is looked for in. The same 64 units the planner
// uses to find a cell for a position; a decision taken more than that from
// the tape's root is a different decision.
#define MOVE_TAPE_NEAR 64.0f
// Two aim gaps are the same gap below this. The aims are compared as printed
// (`%g`, six significant digits), so duplicates of one decision produce gaps
// that agree exactly and anything this close is an exact tie the judge's rank
// decides, not an ordering.
#define MOVE_TAPE_AIM_TIE 1e-6f

// Both hops, each as the engine printed it. Aim and run-up are text because
// the comparison is between what two worlds print, not between two floats
// agreeing to a tolerance somebody chose; aimValue is only for the aim gap a
// miss reports.
// A decision's identity is discrete: the kind, the hop count, both run-ups,
// and - for a state plan - its graph edges, or - for a straight one - the cell
// each hop lands in. The aims are carried and reported, never compared: the
// class-125 world's own judged sets hold admitted straight candidates that
// agree on hops, both run-ups and both aims, so the aim never discriminated
// inside a set and no tolerance on it could define the same decision across
// starts. `status` is 0 unseen, 1 refused, 2 absent, 3 hit.
// `aimValue` is both hops' printed aims read back as numbers: hop 0's is the
// gap a miss reports and the census prints, and the pair is the tie-break
// between admitted candidates the identity rule calls one decision.
typedef struct {
    vec3_t root;
    int kind,hops,edge,edge2,cell[2];
    float aimValue,aimValue2;
    char aim[2][24],runup[2][16];
    int status,attempts,matches,level;
    float gap;
    char why[24];
} moveTapeEntry_t;

static struct {
    char name[MAX_QPATH],why[32];
    // The graph the tape declares it was recorded on, as the header carries
    // it, or "none" for a tape written before tapes were bound to a graph.
    char graph[24];
    int count,failed,cursor,hits,refusedMisses,absentMisses,deferred,reported;
    moveTapeEntry_t entry[MOVE_TAPE_MAX];
} decisionTape;
// The tape as it stood before a cached search's selection wrote to it, so a
// winner the post-selection validation refuses leaves no answered entry.
static struct {int saved;char bytes[sizeof(decisionTape)];} tapeUndo;

// The graph a tape or an index declares, from the trailing `graph=` field of
// its header line, or "none". The field is trailing and optional so a tape
// written before the binding existed still loads and says so, and an older
// engine reading a bound tape ignores it.
static void TapeDeclaredGraph(const char *header,char *out,int size) {
    const char *at=strstr(header,"graph=");
    int i=0;
    Q_strncpyz(out,"none",size);
    if(!at)return;
    at+=6;
    for(i=0;i<size-1 && at[i] && at[i]!=' ' && at[i]!='\t' && at[i]!='\r';i++)out[i]=at[i];
    out[i]=0;
    if(!out[0])Q_strncpyz(out,"none",size);
}

// A tape names cells and edges by index, so it is meaningless on a different
// graph: measured, the class-75 tape selects on the exact rung and
// hits zero of eleven entries when the world runs the unseeded graph it was
// not explored on. A declared graph that is not the loaded one is refused
// here rather than discovered eleven misses later.
static int TapeGraphRefuses(const char *declared) {
    return strcmp(declared,"none") && strcmp(declared,Graph_Identity());
}

static int TapeKind(const char *name) {
    if(!strcmp(name,"spike"))return MOVE_PLAN_SPIKE;
    if(!strcmp(name,"gap"))return MOVE_PLAN_GAP;
    if(!strcmp(name,"edge"))return MOVE_PLAN_EDGE;
    if(!strcmp(name,"straight"))return MOVE_PLAN_STRAIGHT;
    if(!strcmp(name,"state"))return MOVE_PLAN_STATE;
    if(!strcmp(name,"climb"))return MOVE_PLAN_CLIMB;
    return MOVE_PLAN_NONE;
}

// Bound to the map it was built in, as the corridor is: a tape of positions
// and graph edges means nothing anywhere else.
static int TapeLoad(const char *name,char *why,int whySize) {
    char path[MAX_QPATH],map[64],checksum[64],liveMap[64],liveChecksum[64],*bytes,*line,*next;
    fileHandle_t file=0;int size,version,entries,count=0,ok=1;
    memset(decisionTape.entry,0,sizeof(decisionTape.entry));
    decisionTape.count=decisionTape.cursor=0;
    decisionTape.hits=decisionTape.refusedMisses=decisionTape.absentMisses=0;
    decisionTape.deferred=0;
    Q_strncpyz(decisionTape.graph,"none",sizeof(decisionTape.graph));
    if(why && whySize>0)*why=0;
    if(!name || !name[0])return 0;
    if(strchr(name,'.') && strstr(name,"..")) {if(why)Q_strncpyz(why,"bad_name",whySize);return 0;}
    Com_sprintf(path,sizeof(path),"bot2-decisions/%s",name);
    size=trap->FS_Open(path,&file,FS_READ);
    if(!file) {if(why)Q_strncpyz(why,"missing",whySize);return 0;}
    if(size<=0 || size>MOVE_TAPE_MAX*256) {
        trap->FS_Close(file);if(why)Q_strncpyz(why,"size",whySize);return 0;
    }
    bytes=(char*)malloc(size+1);
    if(!bytes) {trap->FS_Close(file);if(why)Q_strncpyz(why,"memory",whySize);return 0;}
    trap->FS_Read(bytes,size,file);trap->FS_Close(file);bytes[size]=0;
    trap->Cvar_VariableStringBuffer("mapname",liveMap,sizeof(liveMap));
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",liveChecksum,sizeof(liveChecksum));
    next=strchr(bytes,'\n');if(next)*next++=0;
    if(sscanf(bytes,"decisiontape %d map=%63s checksum=%63s entries=%d",
              &version,map,checksum,&entries)!=4 ||
       version!=3 || strcmp(map,liveMap) || atoi(checksum)!=atoi(liveChecksum) ||
       entries<1 || entries>MOVE_TAPE_MAX) {
        free(bytes);if(why)Q_strncpyz(why,"binding",whySize);return 0;
    }
    TapeDeclaredGraph(bytes,decisionTape.graph,sizeof(decisionTape.graph));
    if(TapeGraphRefuses(decisionTape.graph)) {
        char refused[24];
        Q_strncpyz(refused,decisionTape.graph,sizeof(refused));
        free(bytes);
        Q_strncpyz(decisionTape.graph,refused,sizeof(decisionTape.graph));
        if(why)Q_strncpyz(why,"graph_mismatch",whySize);
        trap->Print("[TAPE] graph_mismatch name=%s tape_graph=%s graph=%s\n",
                    name,refused,Graph_Identity());
        return 0;
    }
    for(line=next;line && ok;line=next) {
        next=strchr(line,'\n');if(next)*next++=0;
        if(line[0]=='e') {
            moveTapeEntry_t *e=&decisionTape.entry[count];
            char kind[16];int index;
            if(count>=entries ||
               sscanf(line,"e %d %f %f %f %15s %d %23s %15s %23s %15s %d %d %d %d",&index,
                      &e->root[0],&e->root[1],&e->root[2],kind,&e->hops,
                      e->aim[0],e->runup[0],e->aim[1],e->runup[1],
                      &e->edge,&e->edge2,&e->cell[0],&e->cell[1])!=14 ||
               index!=count) {ok=0;break;}
            e->kind=TapeKind(kind);
            if(e->kind==MOVE_PLAN_NONE) {ok=0;break;}
            e->aimValue=(float)atof(e->aim[0]);
            e->aimValue2=(float)atof(e->aim[1]);
            count++;
        } else if(line[0] && line[0]!='\r' && line[0]!='#')ok=0;
    }
    free(bytes);
    if(!ok || count!=entries) {
        memset(decisionTape.entry,0,sizeof(decisionTape.entry));
        if(why)Q_strncpyz(why,"malformed",whySize);return 0;
    }
    decisionTape.count=count;
    return count;
}


// The tape library (bot_moveTapeLibrary, default off; 0 is today's behaviour
// exactly). Stage 3 of the decision-explorer work order: a tape counts toward
// the gate only once the bot selects it itself, so the selection is made here,
// from the bot's own leg, its start cell and its own phase, and the harness
// names no tape.
//
// The library sits beside the graph cache and is bound the same way -
// bot2-decisions/<map>_<checksum>/ - holding tapes in today's format and one
// index.txt read once per map load. A row is the key a leg start forms:
//
//   key <leg-id> <cell> <phase> <file>
//
// The leg-id is the objective the leg runs to, team and objective entity, as
// the bot itself holds them; the cell is the graph cell the runner stands in
// at the leg start, by the same rule the planner picks its root cell; the
// phase is level.time modulo 200 on that frame. The phase is not chosen here:
// nothing delays the bot's first command, so a start lands on the class the
// world offers and the ladder below measures what that costs.
//
// bot_moveDecisionTape keeps its meaning and wins over the library, so every
// earlier campaign replays unchanged.
#define MOVE_TAPE_LIBRARY_MAX 64
#define MOVE_TAPE_LIBRARY_VERSION 1

typedef struct {
    char leg[32],file[64];
    int cell,phase;
} moveTapeKey_t;

static struct {
    char map[64],checksum[64],why[32],dir[MAX_QPATH];
    // The graph the index declares. An index holds keys naming graph cells
    // and tapes naming graph cells and edges, so the whole library is bound
    // to one graph and refused whole when it is not the loaded one.
    char graph[24];
    char leg[32],file[64],rung[24];
    int count,loaded,cell,phase,exact,phaseKeys,linkKeys;
    // A leg start whose objective the bot does not hold yet. The fixture's
    // release runs inside the rcon command, a server frame before the bot's
    // next think, and the corner start clears the controllers: measured, the
    // release frame reports no objective entity at all, so a key formed there
    // would name no objective. The cell and the phase are the leg start's and
    // are kept here; only the leg-id waits, and for one think.
    int pending,pendingClient;
    moveTapeKey_t key[MOVE_TAPE_LIBRARY_MAX];
} tapeLibrary;

// The library owns the tape only when it is enabled and no single file is
// named: the diagnostic override wins, which is what keeps the earlier
// campaigns replaying unchanged.
static int TapeLibraryOwns(void) {
    char name[MAX_QPATH];
    if(!trap->Cvar_VariableIntegerValue("bot_moveTapeLibrary"))return 0;
    trap->Cvar_VariableStringBuffer("bot_moveDecisionTape",name,sizeof(name));
    return name[0]?0:1;
}

// The leg the bot is running: its team and the objective entity it holds.
// Formed by the bot, never by the fixture, so the key a world looks up is
// not the case name the harness passed. A leg that may spend Force carries
// its spend policy as well (t1e83s1 is that leg with Speed): the same leg
// flies a different route when it may buy Speed, and measured, a
// level-1 tape selected on every level-2 stand and never matched an entry.
// A no-spend leg keeps the bare form, so level-1 libraries stay valid.
static void TapeLegId(int client,char *out,int size) {
    int team=-1,target=-1,spend=SpendPolicy();
    if(client>=0 && client<MAX_CLIENTS) {
        if(g_entities[client].client)team=g_entities[client].client->sess.sessionTeam;
        target=bot2_states[client].targetEntNum;
    }
    if(spend)Com_sprintf(out,size,"t%de%ds%d",team,target,spend);
    else Com_sprintf(out,size,"t%de%d",team,target);
}

static void TapeLibraryDir(char *out,int size) {
    char map[64],checksum[64];int i;
    trap->Cvar_VariableStringBuffer("mapname",map,sizeof(map));
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",checksum,sizeof(checksum));
    for(i=0;map[i];i++)if(map[i]=='/' || map[i]=='\\')map[i]='_';
    Com_sprintf(out,size,"%s_%s",map,checksum);
}

// Read once per map load and refused with a reason rather than repaired, as
// the corridor and the graph cache are.
static int TapeLibraryLoad(char *why,int whySize) {
    char path[MAX_QPATH],map[64],checksum[64],liveMap[64],liveChecksum[64],*bytes,*line,*next;
    fileHandle_t file=0;int size,version,keys,count=0,ok=1;
    tapeLibrary.count=0;
    if(why && whySize>0)*why=0;
    Com_sprintf(path,sizeof(path),"bot2-decisions/%s/index.txt",tapeLibrary.dir);
    size=trap->FS_Open(path,&file,FS_READ);
    if(!file) {if(why)Q_strncpyz(why,"missing",whySize);return 0;}
    if(size<=0 || size>MOVE_TAPE_LIBRARY_MAX*256) {
        trap->FS_Close(file);if(why)Q_strncpyz(why,"size",whySize);return 0;
    }
    bytes=(char*)malloc(size+1);
    if(!bytes) {trap->FS_Close(file);if(why)Q_strncpyz(why,"memory",whySize);return 0;}
    trap->FS_Read(bytes,size,file);trap->FS_Close(file);bytes[size]=0;
    trap->Cvar_VariableStringBuffer("mapname",liveMap,sizeof(liveMap));
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",liveChecksum,sizeof(liveChecksum));
    next=strchr(bytes,'\n');if(next)*next++=0;
    if(sscanf(bytes,"decisionindex %d map=%63s checksum=%63s keys=%d",
              &version,map,checksum,&keys)!=4 ||
       version!=MOVE_TAPE_LIBRARY_VERSION || strcmp(map,liveMap) ||
       atoi(checksum)!=atoi(liveChecksum) || keys<1 || keys>MOVE_TAPE_LIBRARY_MAX) {
        free(bytes);if(why)Q_strncpyz(why,"binding",whySize);return 0;
    }
    TapeDeclaredGraph(bytes,tapeLibrary.graph,sizeof(tapeLibrary.graph));
    if(TapeGraphRefuses(tapeLibrary.graph)) {
        free(bytes);if(why)Q_strncpyz(why,"graph_mismatch",whySize);return 0;
    }
    for(line=next;line && ok;line=next) {
        char *cr;
        next=strchr(line,'\n');if(next)*next++=0;
        cr=strchr(line,'\r');if(cr)*cr=0;
        if(!strncmp(line,"key ",4)) {
            moveTapeKey_t *k=&tapeLibrary.key[count];
            if(count>=keys ||
               sscanf(line,"key %31s %d %d %63s",k->leg,&k->cell,&k->phase,k->file)!=4 ||
               k->cell<0 || k->phase<0 || k->phase>=MOVE_TAPE_PHASE ||
               strstr(k->file,"..") || strchr(k->file,'/') || strchr(k->file,'\\')) {ok=0;break;}
            count++;
        } else if(line[0] && line[0]!='#')ok=0;
    }
    free(bytes);
    if(!ok || count!=keys) {
        tapeLibrary.count=0;if(why)Q_strncpyz(why,"malformed",whySize);return 0;
    }
    tapeLibrary.count=count;
    return count;
}

static void TapeLibrarySync(void) {
    char map[64],checksum[64],why[32];
    trap->Cvar_VariableStringBuffer("mapname",map,sizeof(map));
    trap->Cvar_VariableStringBuffer("sv_mapChecksum",checksum,sizeof(checksum));
    if(tapeLibrary.loaded && !strcmp(map,tapeLibrary.map) && !strcmp(checksum,tapeLibrary.checksum))return;
    memset(&tapeLibrary,0,sizeof(tapeLibrary));
    Q_strncpyz(tapeLibrary.map,map,sizeof(tapeLibrary.map));
    Q_strncpyz(tapeLibrary.checksum,checksum,sizeof(tapeLibrary.checksum));
    tapeLibrary.loaded=1;
    TapeLibraryDir(tapeLibrary.dir,sizeof(tapeLibrary.dir));
    TapeLibraryLoad(why,sizeof(why));
    Q_strncpyz(tapeLibrary.why,tapeLibrary.count?"loaded":(why[0]?why:"refused"),sizeof(tapeLibrary.why));
    trap->Print("[TAPE] library %s dir=%s keys=%d\n",tapeLibrary.why,tapeLibrary.dir,tapeLibrary.count);
}

// How far two phases are apart on the 200 ms ring.
static int TapePhaseGap(int a,int b) {
    int d=a-b;
    if(d<0)d=-d;
    return d>MOVE_TAPE_PHASE/2?MOVE_TAPE_PHASE-d:d;
}

// The ladder, in order, beside TapeLoad and calling it: the exact key; the
// same leg and cell at the nearest phase; nothing. Every rung's candidate
// count is kept so the census measures the fallback rather than asserting
// it. Keys on the same leg within one graph link of the start are still
// counted (link_keys) and never selected: the link rung was removed
// after it was neutral or slower at every case it
// answered (docs/movement/records/2026-09-22-seed-gate.md).
static int TapeSelect(const char *leg,int cell,int phase,char *why,int whySize) {
    static const char *rungName[3]={"none","exact","phase"};
    char path[MAX_QPATH];
    int i,best,rung,entries,exact=-1,nearPhase=-1,phaseGap=0;
    tapeLibrary.exact=tapeLibrary.phaseKeys=tapeLibrary.linkKeys=0;
    tapeLibrary.file[0]=0;
    Q_strncpyz(tapeLibrary.rung,rungName[0],sizeof(tapeLibrary.rung));
    if(why && whySize>0)*why=0;
    for(i=0;i<tapeLibrary.count;i++) {
        const moveTapeKey_t *k=&tapeLibrary.key[i];int gap;
        if(strcmp(k->leg,leg))continue;
        gap=TapePhaseGap(k->phase,phase);
        if(k->cell==cell) {
            if(!gap) {
                tapeLibrary.exact++;
                if(exact<0)exact=i;
            } else {
                tapeLibrary.phaseKeys++;
                if(nearPhase<0 || gap<phaseGap) {nearPhase=i;phaseGap=gap;}
            }
        } else if(Graph_Linked(k->cell,cell))tapeLibrary.linkKeys++;
    }
    if(exact>=0) {best=exact;rung=1;}
    else if(nearPhase>=0) {best=nearPhase;rung=2;}
    else {best=-1;rung=0;}
    if(best<0) {
        if(why)Q_strncpyz(why,"no_key",whySize);
        return 0;
    }
    Q_strncpyz(tapeLibrary.rung,rungName[rung],sizeof(tapeLibrary.rung));
    Q_strncpyz(tapeLibrary.file,tapeLibrary.key[best].file,sizeof(tapeLibrary.file));
    Com_sprintf(path,sizeof(path),"%s/%s",tapeLibrary.dir,tapeLibrary.key[best].file);
    entries=TapeLoad(path,why,whySize);
    // A tape refused for its graph names the refusal as the rung, so the
    // census line says why the leg fell back and the fallback happens now
    // rather than after a leg's worth of misses.
    if(!entries && why && !strcmp(why,"graph_mismatch"))
        Q_strncpyz(tapeLibrary.rung,"graph_mismatch",sizeof(tapeLibrary.rung));
    Q_strncpyz(decisionTape.name,path,sizeof(decisionTape.name));
    decisionTape.failed=entries?0:1;
    Q_strncpyz(decisionTape.why,entries?"loaded":((why && why[0])?why:"refused"),sizeof(decisionTape.why));
    return entries;
}

static void TapeSync(void) {
    char name[MAX_QPATH],why[32];
    trap->Cvar_VariableStringBuffer("bot_moveDecisionTape",name,sizeof(name));
    // The library gate. With the library enabled and no single file named,
    // the tape is the leg's own selection and this must not touch it: the
    // index is synced here, once per map load, and the tape is chosen at the
    // leg start. Unset, or with a file named, this is exactly today's code.
    if(!name[0] && trap->Cvar_VariableIntegerValue("bot_moveTapeLibrary")) {
        TapeLibrarySync();
        return;
    }
    if(!name[0]) {
        if(decisionTape.name[0]) {
            memset(&decisionTape,0,sizeof(decisionTape));
            trap->Print("[TAPE] cleared\n");
        }
        return;
    }
    if(!strcmp(name,decisionTape.name) && (decisionTape.count || decisionTape.failed))return;
    Q_strncpyz(decisionTape.name,name,sizeof(decisionTape.name));
    TapeLoad(name,why,sizeof(why));
    decisionTape.failed=decisionTape.count?0:1;
    Q_strncpyz(decisionTape.why,decisionTape.count?"loaded":(why[0]?why:"refused"),sizeof(decisionTape.why));
    trap->Print("[TAPE] %s name=%s entries=%d\n",decisionTape.why,name,decisionTape.count);
}

// The aim the commit line prints, formatted the same way, so the comparison is
// between what the two worlds print and not between two floats agreeing to a
// tolerance somebody chose.
static void TapeHop(const bot2_movePlan_t *c,int hop,const float *ref,
                    char *aim,int aimSize,char *runup,int runupSize,float *value) {
    float d=Wrap(atan2f(c->hop[hop].dir[1],c->hop[hop].dir[0])*180/M_PI-
                 atan2f(ref[1],ref[0])*180/M_PI);
    if(value)*value=d;
    Com_sprintf(aim,aimSize,"%g",d);
    Com_sprintf(runup,runupSize,"%g",c->hop[hop].runup);
}

// The first entry at or after the cursor whose root is near this one. Scanning
// forward rather than taking the cursor's own entry is what keeps an extra or
// a missing commit from misaligning everything after it.
static int TapeEntryNear(const vec3_t root) {
    int i;vec3_t delta;
    for(i=decisionTape.cursor;i<decisionTape.count;i++) {
        VectorSubtract(root,decisionTape.entry[i].root,delta);
        if(VectorLength(delta)<=MOVE_TAPE_NEAR)return i;
    }
    return -1;
}

// Every hop the plan commits, and the planned edges as well for a state
// candidate. Neither half alone identifies a decision: a plan edge pair is
// offered at two run-ups, and two candidates for one edge pair can share
// every first-hop field and fly different second hops. Both were measured -
// the first self-control ran 19,250 ms and the second 20,425 against the
// control's 15,925, each reporting a hit on a candidate that was not the
// control's.
// `landCell` is the judged rollout's landing cell per hop, which the caller
// has and this file does not yet know the type of.
static int TapeMatches(const bot2_movePlan_t *c,const int *landCell,
                       const moveTapeEntry_t *e,const float *ref) {
    char aim[24],runup[16];int hop;
    if(c->kind!=e->kind || c->count!=e->hops)return 0;
    for(hop=0;hop<c->count && hop<2;hop++) {
        TapeHop(c,hop,ref,aim,sizeof(aim),runup,sizeof(runup),NULL);
        if(strcmp(runup,e->runup[hop]))return 0;
    }
    if(e->kind==MOVE_PLAN_STATE)return c->edge==e->edge && c->edge2==e->edge2;
    // A straight plan has no edges, so it is named by where it goes. The cell
    // is a rollout result, not a plan field, so this identity is defined only
    // for candidates that were rolled out - which is every candidate the hook
    // can select, since it selects only admitted ones, and every entry on the
    // tape, since each was a committed rollout. It would not do as a plan
    // identity for an explorer; it is the right one for asking whether the
    // same hop is admissible from a different start.
    for(hop=0;hop<c->count && hop<2;hop++)
        if(landCell[hop]!=e->cell[hop])return 0;
    return 1;
}

// The tape follows one leg, so its cursor and tallies start at a leg start
// and nowhere else. They cannot ride on Move_Reset: an objective change calls
// that mid-trip - observed at level 22775 in the first self-control - and the
// cursor restarted with the flag already in reach. A leg start is the
// fixture's release, or an objective change that moves the bot to a different
// leg-id, which is the bot's own boundary and not the goal position sliding.
// One line per entry at the end of the world, with its final status and how
// many searches attempted it: the per-search lines say where and when, this
// says what each entry came to.
void Move_DecisionTapeCensus(void) {
    static const char *status[4]={"unseen","refused","absent","hit"};
    int i,attempted=0;
    // A tape refused for its graph has no entries to census, and the reason
    // is exactly what a reader needs: the totals line is printed for it, with
    // the rung naming the refusal, so the fallback is read off the world.
    int refusedGraph=!strcmp(tapeLibrary.rung,"graph_mismatch");
    if(decisionTape.reported)return;
    if(!decisionTape.count && !refusedGraph)return;
    for(i=0;i<decisionTape.count;i++)attempted+=decisionTape.entry[i].attempts;
    if(!attempted && !refusedGraph)return;
    decisionTape.reported=1;
    for(i=0;i<decisionTape.count;i++) {
        const moveTapeEntry_t *e=&decisionTape.entry[i];
        trap->Print("[TAPE] census entry=%d kind=%s status=%s attempts=%d matches=%d "
                    "aim_gap=%.4f why=%s level=%d root=%.1f,%.1f,%.1f\n",
                    i,PlanKind(e->kind),status[e->status&3],e->attempts,e->matches,
                    e->gap,e->why[0]?e->why:"none",e->level,
                    e->root[0],e->root[1],e->root[2]);
    }
    // The selection the leg made, so a census says which key was formed,
    // which rung of the ladder answered it and which file ran - and, when the
    // library is not the owner, that the file was named rather than chosen.
    trap->Print("[TAPE] census entries=%d hits=%d refused=%d absent=%d deferred=%d cursor=%d "
                "leg=%s cell=%d phase=%d rung=%s file=%s exact=%d phase_keys=%d link_keys=%d keys=%d graph=%s library_graph=%s\n",
                decisionTape.count,decisionTape.hits,decisionTape.refusedMisses,
                decisionTape.absentMisses,decisionTape.deferred,decisionTape.cursor,
                tapeLibrary.leg[0]?tapeLibrary.leg:"none",tapeLibrary.cell,tapeLibrary.phase,
                tapeLibrary.rung[0]?tapeLibrary.rung:"none",
                decisionTape.name[0]?decisionTape.name:"none",
                tapeLibrary.exact,tapeLibrary.phaseKeys,tapeLibrary.linkKeys,tapeLibrary.count,
                Graph_Identity(),tapeLibrary.graph[0]?tapeLibrary.graph:"none");
}

static void TapeTalliesReset(void) {
    int i;
    decisionTape.reported=0;
    decisionTape.cursor=decisionTape.hits=0;
    decisionTape.refusedMisses=decisionTape.absentMisses=decisionTape.deferred=0;
    for(i=0;i<decisionTape.count;i++) {
        decisionTape.entry[i].status=decisionTape.entry[i].attempts=0;
        decisionTape.entry[i].matches=decisionTape.entry[i].level=0;
        decisionTape.entry[i].gap=0;decisionTape.entry[i].why[0]=0;
    }
}

void Move_DecisionTapeReset(int client,int cell,int phase) {
    // The selection is the leg's, not the harness's: the triple is formed
    // here and the ladder runs before the cursor and the tallies start, so a
    // census belongs to the tape this leg chose. With the library off, or a
    // single file named, nothing below this block runs and the tape is
    // whatever TapeSync loaded.
    if(TapeLibraryOwns()) {
        char leg[32],why[32];int entries;
        TapeLibrarySync();
        tapeLibrary.pending=0;
        if(client>=0 && client<MAX_CLIENTS && bot2_states[client].targetEntNum<0) {
            // The leg-id is the objective the leg runs to, so there is no key
            // to form yet. The leg start's cell and phase are kept and the
            // selection is made at this leg's first think, before any search
            // of it: nothing is delayed and the phase is still the start's.
            tapeLibrary.pending=1;tapeLibrary.pendingClient=client;
            tapeLibrary.cell=cell;tapeLibrary.phase=phase;
            tapeLibrary.leg[0]=tapeLibrary.file[0]=0;
            Q_strncpyz(tapeLibrary.rung,"pending",sizeof(tapeLibrary.rung));
            memset(&decisionTape,0,sizeof(decisionTape));
            trap->Print("[TAPE] select leg=none cell=%d phase=%d rung=pending file=none "
                        "entries=0 exact=0 phase_keys=0 link_keys=0 keys=%d library=%s\n",
                        cell,phase,tapeLibrary.count,tapeLibrary.why[0]?tapeLibrary.why:"none");
            return;
        }
        TapeLegId(client,leg,sizeof(leg));
        Q_strncpyz(tapeLibrary.leg,leg,sizeof(tapeLibrary.leg));
        tapeLibrary.cell=cell;tapeLibrary.phase=phase;
        entries=tapeLibrary.count?TapeSelect(leg,cell,phase,why,sizeof(why)):0;
        if(!entries) {
            // No key, or a tape that refused to load: the leg runs under the
            // ranking and the trip must still complete. Whatever a previous
            // leg selected is cleared, so a stale tape cannot ride on.
            char keep[24];
            Q_strncpyz(keep,tapeLibrary.rung,sizeof(keep));
            memset(&decisionTape,0,sizeof(decisionTape));
            Q_strncpyz(tapeLibrary.rung,keep,sizeof(tapeLibrary.rung));
        }
        trap->Print("[TAPE] select leg=%s cell=%d phase=%d rung=%s file=%s entries=%d "
                    "exact=%d phase_keys=%d link_keys=%d keys=%d library=%s graph=%s library_graph=%s\n",
                    leg,cell,phase,tapeLibrary.rung,
                    tapeLibrary.file[0]?tapeLibrary.file:"none",entries,
                    tapeLibrary.exact,tapeLibrary.phaseKeys,tapeLibrary.linkKeys,
                    tapeLibrary.count,tapeLibrary.why[0]?tapeLibrary.why:"none",
                    Graph_Identity(),tapeLibrary.graph[0]?tapeLibrary.graph:"none");
    }
    TapeTalliesReset();
}

// The deferred half of a leg start: the bot now holds the objective the leg
// runs to, so the key is complete. Called from Move_Plan, which runs before
// this leg's first search, with the leg start's own cell and phase.
static void TapeLibraryResolve(int client) {
    char leg[32],why[32];int entries;
    if(!tapeLibrary.pending || tapeLibrary.pendingClient!=client || !TapeLibraryOwns())return;
    if(client<0 || client>=MAX_CLIENTS || bot2_states[client].targetEntNum<0)return;
    tapeLibrary.pending=0;
    TapeLegId(client,leg,sizeof(leg));
    Q_strncpyz(tapeLibrary.leg,leg,sizeof(tapeLibrary.leg));
    entries=tapeLibrary.count?TapeSelect(leg,tapeLibrary.cell,tapeLibrary.phase,why,sizeof(why)):0;
    if(!entries) {
        char keep[24];
        Q_strncpyz(keep,tapeLibrary.rung,sizeof(keep));
        memset(&decisionTape,0,sizeof(decisionTape));
        Q_strncpyz(tapeLibrary.rung,keep,sizeof(tapeLibrary.rung));
    }
    TapeTalliesReset();
    trap->Print("[TAPE] select leg=%s cell=%d phase=%d rung=%s file=%s entries=%d "
                "exact=%d phase_keys=%d link_keys=%d keys=%d library=%s graph=%s library_graph=%s\n",
                leg,tapeLibrary.cell,tapeLibrary.phase,tapeLibrary.rung,
                tapeLibrary.file[0]?tapeLibrary.file:"none",entries,
                tapeLibrary.exact,tapeLibrary.phaseKeys,tapeLibrary.linkKeys,
                tapeLibrary.count,tapeLibrary.why[0]?tapeLibrary.why:"none",
                    Graph_Identity(),tapeLibrary.graph[0]?tapeLibrary.graph:"none");
}

// A replan's state search split over frames (bot_movePlanSplit, Graph_PlanSplit*): the bot walks the cell route
// meanwhile, and the finished route is installed by its next replan if the goal and the route it walks are still
// the ones the search was started under (a commit's reroute supersedes it).
static struct {
    int id;                 // the running search, 0 none
    int to,from,began;      // its goal and root cells, the level time it started
    int gen;                // the path generation the bot walks meanwhile
    int ready;              // a finished route waits for the next replan
    int avoid[32],avoidCount;   // the edges the bot avoided when it started
    int count,costMs,expanded,overflow;
    int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];float speeds[GRAPH_MAX_PATH];
} split[MAX_CLIENTS];

// 1 while the bot avoids the edges a split search was started under: after a failure since (a guest's avoid list among
// them, which changes no graph topology) the search's route could reinstall the edge refused.
static int SplitAvoidSame(int client,const movement_t *m) {
    return split[client].avoidCount==m->state.avoidCount &&
        !memcmp(split[client].avoid,m->state.avoid,sizeof(int)*m->state.avoidCount);
}

static void SplitCancel(int client) {
    if(split[client].id)Graph_PlanSplitCancel(split[client].id);
    split[client].id=0;split[client].ready=0;
}

// bot_moveFixBX5: the planned edges (index+1, 0 none) a knock interrupted in flight, while the
// interrupted plan's id is still the plan's (no new commit); StateAvoid charges a "walked" one as transient.
static int knockEdges[MAX_CLIENTS][2],knockPlan[MAX_CLIENTS];
void Move_Reset(int client) {
    if(client<0 || client>=MAX_CLIENTS)return;
    SplitCancel(client);
    // The objective change at the flag is where the leg the tape describes
    // ends, and it is the last point this harness's log still reads.
    Move_DecisionTapeCensus();
    Move_ParityTally(client);
    ExploreReset(client);
    SearchBudgetReset(client);
    memset(&movement[client],0,sizeof(movement[client]));
    movement[client].pad.edge=-1;movement[client].lift.edge=-1;
    knockEdges[client][0]=knockEdges[client][1]=0;
    // bot_moveFixBM7: a pad arc entered before the client's last spawn ends here (a
    // defender's respawn resets only through Move_Reset); an objective change mid-arc keeps it.
    if(MOVE_FIX(BM7) && g_entities[client].client && bot2_states[client].state==BOT_STATE_JUMPPAD &&
       bot2_states[client].stateTimer<g_entities[client].client->respawnTime && g_entities[client].client->respawnTime<=level.time)
        bot2_states[client].state=BOT_STATE_WALK;
    // A reset is a declared discontinuity (death, disconnect, map change,
    // objective change, fixture start); a horizon forecast across it is void.
    ParityReset(client);
}

const movement_t *Move_State(int client) {
    return client>=0 && client<MAX_CLIENTS?&movement[client]:NULL;
}

qboolean Move_SequenceActive(int client) {
    const movement_t *m=Move_State(client);
    return m && m->plan.count && m->executor.stage<Onward(&m->plan);
}

// The planner's search: from the bot's cell to the objective's cell over
// the graph's links and edges. No path leaves the navmesh walk in charge and
// retries later; a path is a discontinuity for the parity harness.
// The live state as the state search reads it: speed and heading from the
// velocity (the view yaw when at rest), a landing frame when a flight
// stage is about to touch down on this frame, and the Speed window left.
static void LiveState(const movement_t *m,const playerState_t *ps,graphState_t *s) {
    float v=Speed2D(ps);
    s->speed=v;
    if(v>=1) {s->dir[0]=ps->velocity[0]/v;s->dir[1]=ps->velocity[1]/v;}
    else {s->dir[0]=cosf(ps->viewangles[YAW]*M_PI/180);s->dir[1]=sinf(ps->viewangles[YAW]*M_PI/180);}
    s->tap=m->plan.count && m->executor.stage<Onward(&m->plan) && (m->executor.stage&1) && m->executor.airborne &&
           ps->groundEntityNum!=ENTITYNUM_NONE;
    s->speedMs=(ps->fd.forcePowersActive&(1<<FP_SPEED))?max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time):0;
}

// A snapshot of the path and of every edge it plans, taken when the path
// is created. The complete edge record is written out because learning,
// merging and measured-cost updates can change an edge under a fixed
// index: a later comparison must be able to say whether the record it
// assumed still held. Disabling autosave freezes the disk file only.
static void ReplayPath(int client,const graphState_t *start,int elapsed) {
    movement_t *m=&movement[client];int k;
    if(!(ReplayFlags()&1))return;
    Bot2_PrintTelemetry(1,"[REPLAY] kind=path client=%d level=%d gen=%d from=%d to=%d cells=%d cost_ms=%d state=%d strafe=%d "
        "expanded=%d overflow=%d replans=%d search_ms=%d avoid=%d start_speed=%.3f start_dir=%.5f,%.5f start_tap=%d start_speed_ms=%d "
        "goal=%.3f,%.3f,%.3f edges_total=%d\n",
        client,level.time,m->path.generation,m->path.from,m->path.to,m->path.count,m->path.costMs,m->path.state,m->path.strafe,
        m->path.expanded,m->path.overflow,m->path.replans,elapsed,m->state.avoidCount,start->speed,start->dir[0],start->dir[1],
        start->tap,start->speedMs,m->objective[0],m->objective[1],m->objective[2],Graph_EdgeCount());
    for(k=0;k<m->path.count;k++) {
        const graphEdge_t *e;vec3_t point;
        if(m->path.edges[k]<0)continue;
        e=Graph_Edge(m->path.edges[k]);
        if(!e)continue;
        CellPoint(m->path.cells[k],point);
        Bot2_PrintTelemetry(1,"[REPLAY] kind=path_edge client=%d level=%d gen=%d seq=%d index=%d type=%d from=%d to=%d cost_ms=%d "
            "tap=%d entry_speed=%.3f entry_dir=%.5f,%.5f exit_speed=%.3f exit_dir=%.5f,%.5f dir=%.5f,%.5f "
            "takeoff=%.3f,%.3f,%.3f landing=%.3f,%.3f,%.3f cell=%.3f,%.3f,%.3f expect_speed=%.3f validated=%d traversals=%d failures=%d disabled=%d\n",
            client,level.time,m->path.generation,k,m->path.edges[k],e->type,e->from,e->to,e->costMs,
            e->tap,e->entrySpeed,e->entryDir[0],e->entryDir[1],e->exitSpeed,e->exitDir[0],e->exitDir[1],e->dir[0],e->dir[1],
            e->takeoff[0],e->takeoff[1],e->takeoff[2],e->landing[0],e->landing[1],e->landing[2],
            point[0],point[1],point[2],m->path.speeds[k],e->validated,e->traversals,e->failures,e->disabled);
    }
}

// The guided corridor, from bot_moveCorridor (a file under bot2-corridor/)
// and bot_moveCorridorRadius. Unset, nothing is loaded and every search is
// unguided. This is a diagnostic input, not a movement policy: the corridor
// says which cells a search may leave the bot's own cell for, and nothing
// about the commands it generates or the states it carries.
static struct {char name[MAX_QPATH];float radius;int points,cells,failed;char why[32];} corridorState;

static void CorridorSync(void) {
    char name[MAX_QPATH],value[32],why[32];float radius;
    trap->Cvar_VariableStringBuffer("bot_moveCorridorRadius",value,sizeof(value));
    radius=(float)atof(value);
    trap->Cvar_VariableStringBuffer("bot_moveCorridor",name,sizeof(name));
    if(!name[0]) {
        if(corridorState.name[0] || Graph_CorridorPoints()) {
            Graph_CorridorClear();memset(&corridorState,0,sizeof(corridorState));
            trap->Print("[CORRIDOR] cleared\n");
        }
        return;
    }
    // A rebuilt graph clears the marks, so a loaded corridor with no points
    // is reloaded against the cells the graph now has.
    if(!strcmp(name,corridorState.name) && radius==corridorState.radius &&
       (Graph_CorridorPoints() || corridorState.failed))return;
    Q_strncpyz(corridorState.name,name,sizeof(corridorState.name));
    corridorState.radius=radius;
    corridorState.points=Graph_CorridorLoad(name,radius,why,sizeof(why));
    corridorState.cells=Graph_CorridorCells();
    corridorState.failed=corridorState.points?0:1;
    Q_strncpyz(corridorState.why,corridorState.points?"loaded":(why[0]?why:"refused"),sizeof(corridorState.why));
    trap->Print("[CORRIDOR] %s name=%s points=%d cells=%d radius=%.1f graph_cells=%d\n",
        corridorState.why,name,corridorState.points,corridorState.cells,Graph_CorridorRadius(),Graph_CellCount());
}

// The cell a position plans from: the one it stands in, or the nearest one
// within reach. The tape library's leg-start cell is this cell, so a key is
// formed from the same cell the planner would plan the leg from.
int Move_RootCell(const vec3_t origin) {
    int cell=Graph_CellAt(origin,STEPSIZE+.25f);
    if(cell<0)cell=Graph_Nearest(origin,64,2*STEPSIZE);
    return cell;
}

// ---------------------------------------------------------------------------
// Planner directives for one search (exploration only, default off).
//
// The explorer can force only among the candidates a search offers, and every
// candidate comes from the one installed route, so an edge the planner does
// not route through can never be forced (docs/movement/records/2026-09-22-
// seed-deaths-and-planner-reach.md, B). These let one world start from a
// different route:
//
// - `bot_movePlanAvoid "<edge> <edge> ..."` (spaces or commas): the indices
//   are added to the avoid list passed to Graph_PlanState. The bot's own
//   per-objective list, its failures and its `avoid` events are untouched.
// - `bot_movePlanRequire <edge>`: only a route taking that edge reaches the
//   goal (a bit in the planner's state key). When none does, the ordinary
//   search runs and the plan line says `require_met=0`.
// - `bot_movePlanGen <n>`: apply them only to the n-th plan of the leg - the
//   n-th route since Move_Reset, counting replans and installed reroutes as
//   the plan line's `replans` does (1 is the release route). 0, the default,
//   applies them to every plan; a required edge is then dropped for the rest
//   of the leg once the bot has flown or passed it.
//
// Unset, nothing changes: the avoid array and its count are the bot's own and
// require is -1, which the planner treats bit-identically to a search without
// it. Directives name edge indices, so, like a tape, they bind to the graph
// identity the world loaded. Parsed once per server frame, never in a search.
// ---------------------------------------------------------------------------
#define MOVE_PLAN_AVOID_MAX 32
static struct {
    int time,parsed;
    int avoid[MOVE_PLAN_AVOID_MAX],avoidCount,require,gen;
} planDirectives;

static void PlanDirectivesSync(void) {
    char value[MAX_CVAR_VALUE_STRING];const char *p;
    if(planDirectives.parsed && planDirectives.time==level.time)return;
    planDirectives.parsed=1;planDirectives.time=level.time;planDirectives.avoidCount=0;
    trap->Cvar_VariableStringBuffer("bot_movePlanAvoid",value,sizeof(value));
    for(p=value;*p;) {
        char *end;long n;
        while(*p && (*p<'0' || *p>'9'))p++;
        if(!*p)break;
        n=strtol(p,&end,10);p=end;
        if(planDirectives.avoidCount<MOVE_PLAN_AVOID_MAX)planDirectives.avoid[planDirectives.avoidCount++]=(int)n;
    }
    trap->Cvar_VariableStringBuffer("bot_movePlanRequire",value,sizeof(value));
    planDirectives.require=value[0]?atoi(value):-1;
    if(planDirectives.require<0)planDirectives.require=-1;
    planDirectives.gen=max(0,trap->Cvar_VariableIntegerValue("bot_movePlanGen"));
}

// Whether the required edge has been flown or passed on this leg: a committed
// state plan that names it, or the executor past its entry on the route.
static int PlanRequireSpent(movement_t *m,int require) {
    int k;
    if(m->requireSpent)return 1;
    if(m->plan.kind==MOVE_PLAN_STATE && (m->plan.edge==require || m->plan.edge2==require))m->requireSpent=1;
    for(k=0;!m->requireSpent && k<m->path.count && k<=m->executor.pathIndex;k++)
        if(m->path.edges[k]==require)m->requireSpent=1;
    return m->requireSpent;
}

// The directives for the plan of ordinal `ordinal` of this leg: fills avoid[]
// with the bot's own list followed by the directive's indices not already in
// it, and returns the required edge or -1; *flags receives the directive bits
// that applied (0 none: the arrays are then exactly the bot's own).
static int PlanDirectivesFor(movement_t *m,int ordinal,int *avoid,int *avoidCount,int *flags,int *added) {
    int i,j,require=-1;
    for(i=0;i<m->state.avoidCount;i++)avoid[i]=m->state.avoid[i];
    *avoidCount=m->state.avoidCount;*flags=0;*added=0;
    if(MoveGuest((int)(m-movement)))return -1;   // the fixture's directives are the runner's
    PlanDirectivesSync();
    if(!planDirectives.avoidCount && planDirectives.require<0)return -1;
    if(planDirectives.gen>0 && planDirectives.gen!=ordinal)return -1;
    for(i=0;i<planDirectives.avoidCount;i++) {
        for(j=0;j<*avoidCount;j++)if(avoid[j]==planDirectives.avoid[i])break;
        if(j<*avoidCount)continue;
        avoid[(*avoidCount)++]=planDirectives.avoid[i];(*added)++;
    }
    if(*added)*flags|=1;
    if(planDirectives.require>=0 && (planDirectives.gen>0 || !PlanRequireSpent(m,planDirectives.require))) {
        require=planDirectives.require;*flags|=2;
    }
    return require;
}

// The state search under the directives, and the ordinary search when a
// required edge admits no route. Records on the path what applied.
static int PlanStateDirected(movement_t *m,int ordinal,int from,const graphState_t *s,int to,bot2_movePath_t *out) {
    int avoid[64],avoidCount,flags,added,require,count=0;
    require=PlanDirectivesFor(m,ordinal,avoid,&avoidCount,&flags,&added);
    out->directive=flags;out->directiveAvoid=added;out->require=require;out->requireMet=0;
    if(require>=0) {
        count=Graph_PlanState(from,s,to,avoid,avoidCount,require,out->cells,out->edges,out->speeds,
                              GRAPH_MAX_PATH,&out->costMs,&out->expanded,&out->overflow);
        out->requireMet=count>0;
    }
    if(!count)
        count=Graph_PlanState(from,s,to,avoid,avoidCount,-1,out->cells,out->edges,out->speeds,
                              GRAPH_MAX_PATH,&out->costMs,&out->expanded,&out->overflow);
    out->timed=count>0 && Graph_PlanStateTimes(out->times,GRAPH_MAX_PATH)==count;
    out->speedMs=s->speedMs;
    return count;
}

// bot_movePlanSplit (default 0, off): a replan's state search runs at most
// this many expansions a frame (option 2a, records/2026-10-06-frame-cost.md).
static int PlanSplitBudget(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanSplit",value,sizeof(value));
    return Bot2_WorkPerFrame(value[0]?max(0,atoi(value)):0);   // expansions per 25 ms of frame
}

// A replan's state search, split over frames when bot_movePlanSplit allows:
// PlanStateDirected's result when it finishes within the first part, or -1
// when it runs on in its own table (the caller then walks the cell route).
// A search already running toward the same goal, under the same avoided
// edges, is kept rather than restarted, so a bot that replans while it runs
// still gets its route.
// Directed searches (fixture-only) stay whole.
static int PlanStateSplit(int client,movement_t *m,int from,const graphState_t *s,int to,bot2_movePath_t *out) {
    int avoid[64],avoidCount,flags,added,require,budget=PlanSplitBudget(),id,count;
    if(budget<=0) {SplitCancel(client);return PlanStateDirected(m,m->path.replans,from,s,to,out);}
    if(split[client].id && split[client].to==to && SplitAvoidSame(client,m)) {
        out->directive=0;out->directiveAvoid=0;out->require=-1;out->requireMet=0;out->expanded=0;out->overflow=0;
        return -1;
    }
    SplitCancel(client);
    require=PlanDirectivesFor(m,m->path.replans,avoid,&avoidCount,&flags,&added);
    if(require>=0 || flags)return PlanStateDirected(m,m->path.replans,from,s,to,out);
    out->directive=0;out->directiveAvoid=0;out->require=-1;out->requireMet=0;out->expanded=0;out->overflow=0;
    if(Graph_PlanSplitStart(from,s,to,avoid,avoidCount,budget,&id))
        return Graph_PlanSplitResult(id,out->cells,out->edges,out->speeds,GRAPH_MAX_PATH,&out->costMs,&out->expanded,&out->overflow);
    if(!id)return PlanStateDirected(m,m->path.replans,from,s,to,out);   // no table free: whole, as before
    split[client].id=id;split[client].to=to;split[client].from=from;split[client].began=level.time;
    split[client].avoidCount=m->state.avoidCount;memcpy(split[client].avoid,m->state.avoid,sizeof(int)*m->state.avoidCount);
    count=-1;
    return count;
}

// A running split search finished at once (the cell route found nothing, so
// the replan cannot wait for it): its route, or 0.
static int SplitFinish(int client,bot2_movePath_t *out) {
    int id=split[client].id,count=0;
    split[client].id=0;
    if(id && Graph_PlanSplitResume(id,0)==1)
        count=Graph_PlanSplitResult(id,out->cells,out->edges,out->speeds,GRAPH_MAX_PATH,&out->costMs,&out->expanded,&out->overflow);
    else if(id)Graph_PlanSplitCancel(id);
    return count;
}

// One part of the bot's running split search, each frame before its replan
// check. Finished with a route, the route waits for the next replan, which is
// asked for now; finished without one, the cell route the bot walks is what
// the replan would have fallen back to.
static void SplitStep(int client) {
    movement_t *m=&movement[client];int budget,done;double clock;
    if(!split[client].id)return;
    budget=PlanSplitBudget();
    if(m->path.generation!=split[client].gen || budget<=0) {SplitCancel(client);return;}
    clock=bot2_perfOn?Bot2_PerfClock():0;
    bot2_meterInReplan=1;done=Graph_PlanSplitResume(split[client].id,budget);bot2_meterInReplan=0;
    if(done==1) {
        split[client].count=Graph_PlanSplitResult(split[client].id,split[client].cells,split[client].edges,split[client].speeds,
            GRAPH_MAX_PATH,&split[client].costMs,&split[client].expanded,&split[client].overflow);
        split[client].id=0;
        if(split[client].count>0) {split[client].ready=1;m->path.replan=1;m->path.retryTime=0;}
    } else if(done<0)split[client].id=0;
    if(clock>0)Bot2_FrameMeterPlanUs(Bot2_PerfClock()-clock);
}

// The installed route's graph edges, in order, for enrichment: a planned
// route's edges are what a fast trip flies straight candidates along without
// ever naming them on a commit, so the commit lines alone cannot fold them
// into a cache (docs/movement/records/2026-09-22-seed-deaths-and-planner-
// reach.md, B). One `[ROUTE]` line per installed route, split into parts of
// at most MOVE_ROUTE_LINE_EDGES indices: `total` is the route's edge count,
// `first` the ordinal of the part's first index and `count` the indices in
// the part; `graph` binds the indices to the graph they name. Telemetry only,
// and never printed inside a search: a replan prints it where it installs, a
// reroute installed by a commit leaves it pending for the end of that Pmove
// boundary, after the search has returned.
#define MOVE_ROUTE_LINE_EDGES 64
static int routePending[MAX_CLIENTS];      // path generation + 1 of a reroute awaiting its line; 0 none

static void RouteLine(int client,const char *source) {
    const movement_t *m=&movement[client];char list[MOVE_ROUTE_LINE_EDGES*8+8];
    int k,total=0,first=0,count=0,printed=0;
    for(k=0;k<m->path.count;k++)if(m->path.edges[k]>=0)total++;
    list[0]=0;
    for(k=0;k<=m->path.count;k++) {
        int flush=k==m->path.count || (m->path.edges[k]>=0 && count==MOVE_ROUTE_LINE_EDGES);
        if(flush && (count || !printed)) {
            Bot2_PrintTelemetry(1,"[ROUTE] client=%d level=%d gen=%d source=%s from=%d to=%d cells=%d cost_ms=%d state=%d graph=%s "
                "total=%d first=%d count=%d edges=%s\n",
                client,level.time,m->path.generation,source,m->path.from,m->path.to,m->path.count,m->path.costMs,m->path.state,
                Graph_Identity(),total,first,count,count?list:"-");
            printed=1;first+=count;count=0;list[0]=0;
        }
        if(k<m->path.count && m->path.edges[k]>=0) {
            Q_strcat(list,sizeof(list),va("%s%d",count?",":"",m->path.edges[k]));
            count++;
        }
    }
}

static void RoutePendingPrint(int client) {
    if(client<0 || client>=MAX_CLIENTS || !routePending[client])return;
    if(movement[client].path.generation==routePending[client]-1)RouteLine(client,"reroute");
    routePending[client]=0;
}

// The other live bodies a replan routes around (Graph_BodiesSet): players and
// NPCs within MOVE_BODY_PLAN_RANGE of the bot. A farther one will have moved
// by the time the bot arrives. None with bot_moveFailCause 0.
#define MOVE_BODY_PLAN_RANGE 512.0f
static int PlanBodies(int client,vec3_t *at) {
    const float *o=g_entities[client].client->ps.origin;int i,n=0;
    if(!Graph_FailCause())return 0;
    for(i=0;i<level.num_entities && n<GRAPH_MAX_BODIES;i++) {
        const gentity_t *other=&g_entities[i];const float *p=other->r.currentOrigin;
        if(i==client || !other->inuse || !other->r.linked || !other->client || other->health<=0)continue;
        if(i>=MAX_CLIENTS && other->s.eType!=ET_NPC)continue;
        if(i<MAX_CLIENTS && other->client->sess.sessionTeam==TEAM_SPECTATOR)continue;
        if(fabsf(p[2]-o[2])>256 || (p[0]-o[0])*(p[0]-o[0])+(p[1]-o[1])*(p[1]-o[1])>MOVE_BODY_PLAN_RANGE*MOVE_BODY_PLAN_RANGE)continue;
        VectorCopy(p,at[n]);n++;
    }
    // The enemy-flag keep-out is the forbidden volume (ForbidSync below), not bodies: the nine-body lattice
    // (bot_defenderFlagKeepOutRoute 2) was removed (records/2026-10-09-flag-avoid.md).
    return n;
}

// The defender's enemy-flag keep-out as a constraint, not bodies placed to steer the
// route (records/2026-10-09-flag-avoid.md). With bot_defenderFlagKeepOutRoute 1 the box (hull origins:
// the pickup box plus bot_defenderFlagKeepOutMargin) is this client's forbidden volume: every rollout it searches
// refuses a step into it (Bot2_ForecastForbidSet: gap, straight, drop, climb, walk lookahead, parity), its plans
// and reroutes admit no walk, diagonal or edge into it (Graph_ForbidSet around each), and the walk's target never
// lies across it (ForbidTarget). Per client and per plan: nothing is written to the shared graph, a refused
// rollout is the search's alone (a guest charges no failure). Synced at every entry that plans or searches for
// the client; a box that appears, moves (the flag dropped, returned or taken) or goes asks for a replan and ends
// a split search begun under the old one. A box moving less than MOVE_FORBID_MOVED (a dropped flag still settling,
// tossed, bouncing) keeps the old one: it replanned every frame for a second (first arm). The camper has
// no box (Bot2_DefenderKeepOutBox). The volume constrains the route, never the goal: FlagKeepOut moves every
// DefenderMove goal out of the box, so a movement objective inside it is a defender layer's deliberate exception
// (Melee at the empty stand while the flag is away); the volume is off while it is, or the bot stood at the box's
// edge replanning a route the volume could not give (first arm: a chase D 2.5 s at Dantooine's stand).
#define MOVE_FORBID_MOVED 16.0f
static struct { int on;vec3_t mins,maxs; } moveForbid[MAX_CLIENTS];
static int ForbidInBox(const float *p,const float *mins,const float *maxs) {
    return p[0]>=mins[0] && p[0]<=maxs[0] && p[1]>=mins[1] && p[1]<=maxs[1] && p[2]>=mins[2] && p[2]<=maxs[2];
}
// The first step of `path` at or after `from` that enters the box (walk links and diagonals cell to cell, an edge
// from its source cell to its takeoff and on to its landing, no arc allowance), or -1.
static int PathEntersBox(const bot2_movePath_t *path,int from,const vec3_t mins,const vec3_t maxs) {
    vec3_t a,b;int k;
    for(k=max(1,from);k<path->count;k++) {
        CellPoint(path->cells[k-1],a);
        if(path->edges[k]>=0) {
            const graphEdge_t *e=Graph_Edge(path->edges[k]);
            if(e && (Bot2_SegmentEntersBox(a,e->takeoff,mins,maxs) || Bot2_SegmentEntersBox(e->takeoff,e->landing,mins,maxs)))return k;
            continue;
        }
        CellPoint(path->cells[k],b);
        if(Bot2_SegmentEntersBox(a,b,mins,maxs))return k;
    }
    return -1;
}
static void ForbidSync(int client) {
    vec3_t flag,mins,maxs;int mode=0,on;movement_t *m;
    if(client<0 || client>=MAX_CLIENTS)return;
    m=&movement[client];
    on=Bot2_DefenderIs(client) && g_entities[client].health>0 && Bot2_DefenderKeepOutBox(client,flag,mins,maxs,&mode) && mode!=0 &&
       !(m->objectiveSet && ForbidInBox(m->objective,mins,maxs));
    if(on==moveForbid[client].on && (!on || Distance(mins,moveForbid[client].mins)<MOVE_FORBID_MOVED))return;
    moveForbid[client].on=on;
    if(on) {VectorCopy(mins,moveForbid[client].mins);VectorCopy(maxs,moveForbid[client].maxs);}
    Bot2_ForecastForbidSet(client,on?mins:NULL,maxs);
    SplitCancel(client);
    // Only a route the new box cuts is planned again (a replan at every drop, pickup and
    // return of the flag cost Kejim +14% planner expansions); a box gone leaves the route valid.
    if(on && m->path.count && PathEntersBox(&m->path,m->executor.pathIndex>0?m->executor.pathIndex:1,mins,maxs)>=0) {
        m->path.replan=1;m->path.retryTime=0;
    }
}
static void ForbidPlanBegin(int client) {
    if(client>=0 && client<MAX_CLIENTS && moveForbid[client].on)Graph_ForbidSet(moveForbid[client].mins,moveForbid[client].maxs);
}
static void ForbidPlanEnd(void) {Graph_ForbidSet(NULL,NULL);}

// Diagnostic (both route modes): an installed route of a defender that enters its keep-out box prints one [FLAGBOX]
// plan line (walk steps and diagonals cell to cell, an edge source to takeoff to landing, no arc allowance).
static void ForbidPlanCheck(int client,const bot2_movePath_t *path,const char *source) {
    vec3_t flag,mins,maxs;int mode=0,hit;
    if(!Bot2_DefenderIs(client) || !Bot2_DefenderKeepOutBox(client,flag,mins,maxs,&mode))return;
    if(mode!=0) {   // the box the plan was made under; none when the goal exempted it
        if(!moveForbid[client].on)return;
        VectorCopy(moveForbid[client].mins,mins);VectorCopy(moveForbid[client].maxs,maxs);
    }
    if(!path->count) {
        if(mode!=0)Bot2_PrintTelemetry(1,"[FLAGBOX] plan_none client=%d level=%d source=%s from=%d to=%d mode=%d\n",
            client,level.time,source,path->from,path->to,mode);
        return;
    }
    hit=PathEntersBox(path,1,mins,maxs);
    if(hit>=0)Bot2_PrintTelemetry(1,"[FLAGBOX] plan_cross client=%d level=%d source=%s index=%d cells=%d edge=%d mode=%d\n",
        client,level.time,source,hit,path->count,path->edges[hit],mode);
}

// bot_movePlanEnclosed (default 1; 0 off): a replan whose goal the bodies enclose
// (Graph_BodiesEncloseGoal) skips the pass around them, which can only fail, and
// plans as its fallback would. Its plan line is unchanged (body_fallback=1); a
// [BODYSKIP] line counts it. 2 checks instead: the pass runs as before and
// [BODYSKIP] records whether it found a route (found=1 would refute the test).
static int PlanEnclosedMode(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePlanEnclosed",value,sizeof(value));
    return value[0]?atoi(value):1;
}

// The frame meter's source tally (diagnostic): the forecast steps a boundary section ran.
#define METER_SRC(source,call) do {unsigned int meterAt_=bot2_meterSteps;call;Bot2_FrameMeterSource(source,bot2_meterSteps-meterAt_);} while(0)

static void Replan(int client) {
    movement_t *m=&movement[client];const playerState_t *ps=&g_entities[client].client->ps;
    int from,to,began,used=0,i,bodies,pass,bodyFallback=0,enclosedMode=0,enclosed=0,enclosedFound=0,splitRun=0,splitInstalled=0;
    int keepable,kept=0,keptIndex=0;static bot2_movePath_t keptPath;
    char detail[512];graphState_t s;vec3_t bodyAt[GRAPH_MAX_BODIES];double planClock;
    // A reroute whose line is still pending is printed before its route is
    // replaced, so every installed route has one.
    RoutePendingPrint(client);
    began=trap->Milliseconds();planClock=bot2_perfOn?Bot2_PerfClock():0;bot2_meterInReplan=1;
    // A required edge the route being replaced has already carried the bot
    // past is spent, read before that route is cleared.
    PlanDirectivesSync();
    if(planDirectives.require>=0 && !planDirectives.gen && !MoveGuest(client))PlanRequireSpent(m,planDirectives.require);
    m->path.replan=0;m->path.retryTime=level.time+MOVE_PATH_RETRY;m->path.replans++;
    CorridorSync();
    from=Move_RootCell(ps->origin);
    to=Graph_Nearest(m->objective,64,64);
    LiveState(m,ps,&s);
    // With split searches on, the route the bot is on, toward the same goal and
    // through no edge it now avoids, is what it walks while a split search
    // runs: a cross-map cell search for the meantime costs about what the split
    // saves (records/2026-10-06-frame-cost.md, "where the expansions go").
    keepable=PlanSplitBudget()>0 && m->path.count>0 && m->path.to==to && to>=0 && m->executor.pathIndex>=0;
    for(i=0;keepable && i<m->path.count;i++)if(m->path.edges[i]>=0) {
        int k;
        for(k=0;k<m->state.avoidCount;k++)if(m->state.avoid[k]==m->path.edges[i])keepable=0;
    }
    if(keepable) {keptPath=m->path;keptIndex=m->executor.pathIndex;}
    m->path.count=0;m->path.state=0;m->path.strafe=0;m->path.expanded=0;m->path.overflow=0;m->path.timed=0;m->path.joinedAt=0;m->path.bodies=0;
    m->path.directive=0;m->path.directiveAvoid=0;m->path.require=-1;m->path.requireMet=0;
    // A split search's finished route (bot_movePlanSplit), when the goal and the
    // route walked meanwhile are still the ones it began under; the executor
    // finds the bot's place on it, which may be past its first cells.
    if(split[client].ready) {
        split[client].ready=0;
        if(split[client].to==to && split[client].gen==m->path.generation && SplitAvoidSame(client,m) && from>=0) {
            m->path.count=split[client].count;m->path.costMs=split[client].costMs;
            m->path.expanded=split[client].expanded;m->path.overflow=split[client].overflow;m->path.state=1;
            memcpy(m->path.cells,split[client].cells,sizeof(int)*m->path.count);
            memcpy(m->path.edges,split[client].edges,sizeof(int)*m->path.count);
            memcpy(m->path.speeds,split[client].speeds,sizeof(float)*m->path.count);
            splitInstalled=1;
        }
    }
    bodies=from>=0 && to>=0 && !splitInstalled?PlanBodies(client,bodyAt):0;
    ForbidPlanBegin(client);
    // Around the live bodies first; when they leave no route, as before.
    for(pass=bodies?0:1;pass<2 && from>=0 && to>=0 && !m->path.count;pass++) {
        Graph_BodiesSet(bodyAt,pass?0:bodies);
        if(pass && bodies)bodyFallback=1;
        if(!pass) {
            enclosedMode=PlanEnclosedMode();
            enclosed=enclosedMode && Graph_BodiesEncloseGoal(from,to);
            if(enclosed && enclosedMode==1)continue;
        }
        // The state search first; the cell search when it finds nothing or
        // its table overflowed, so a plan is never lost to the state.
        if(StateEnabled()) {
            m->path.count=PlanStateSplit(client,m,from,&s,to,&m->path);
            if(m->path.count<0) {
                m->path.count=0;splitRun=1;
                if(keepable) {m->path=keptPath;kept=1;}
            }
            if(!kept)m->path.state=m->path.count>0;
        }
        // A long state search the cell route bounded (bot_movePlanBound) left that route: Graph_Path's own answer.
        if(!m->path.count) {
            int kept=Graph_PlanBoundRoute(from,to,m->path.cells,m->path.edges,GRAPH_MAX_PATH,&m->path.costMs);
            m->path.count=kept?max(kept,0):Graph_Path(from,to,m->path.cells,m->path.edges,GRAPH_MAX_PATH,&m->path.costMs);
        }
        // A split search with no cell route to walk meanwhile finishes now, as the whole search would have.
        if(splitRun && !m->path.count) {
            m->path.count=SplitFinish(client,&m->path);
            m->path.state=m->path.count>0;splitRun=0;
        }
        if(!pass && enclosed)enclosedFound=m->path.count>0;
        if(!pass && bodies && m->path.count>0)m->path.bodies=1;
    }
    Graph_BodiesSet(NULL,0);ForbidPlanEnd();bot2_meterInReplan=0;
    if(enclosed)
        Bot2_PrintTelemetry(1,"[BODYSKIP] client=%d level=%d from=%d to=%d bodies=%d mode=%d found=%d\n",
            client,level.time,from,to,bodies,enclosedMode,enclosedFound);
    if(planClock>0)Bot2_FrameMeterPlanUs(Bot2_PerfClock()-planClock);
    m->path.from=from;m->path.to=to;m->path.time=level.time;m->path.generation=++replayGeneration;
    if(splitInstalled)m->path.from=split[client].from;
    if(split[client].id)split[client].gen=m->path.generation;
    for(i=0;i<m->path.count;i++)if(m->path.edges[i]>=0) {used++;if(PathStrafe(&m->path,i))m->path.strafe++;}
    m->executor.pathIndex=m->path.count?(kept?keptIndex:0):-1;
    ParityReset(client);
    Com_sprintf(detail,sizeof(detail),"from=%d to=%d cells=%d edges=%d cost_ms=%d replans=%d elapsed_ms=%d state=%d strafe=%d expanded=%d overflow=%d avoid=%d start_speed=%.1f start_tap=%d gen=%d corridor=%d corridor_cells=%d corridor_from=%.1f",
        from,to,m->path.count,used,m->path.costMs,m->path.replans,trap->Milliseconds()-began,
        m->path.state,m->path.strafe,m->path.expanded,m->path.overflow,m->state.avoidCount,s.speed,s.tap,m->path.generation,
        Graph_CorridorPoints(),Graph_CorridorCells(),Graph_CorridorDistance(ps->origin));
    // Only a plan a directive applied to carries the fields, so an undirected
    // plan line is today's line byte for byte.
    if(m->path.directive)
        Q_strcat(detail,sizeof(detail),va(" directive=%d plan_avoid=%d require=%d require_met=%d",
            m->path.directive,m->path.directiveAvoid,m->path.require,m->path.requireMet));
    // Likewise only a plan made with bodies near carries theirs.
    if(bodies)Q_strcat(detail,sizeof(detail),va(" bodies=%d body_fallback=%d",bodies,bodyFallback));
    // And only a split search's routes theirs: 1 the cell route walked while it runs, 2 its route installed.
    if(splitInstalled)Q_strcat(detail,sizeof(detail),va(" split=2 split_from=%d split_level=%d",split[client].from,split[client].began));
    else if(split[client].id)Q_strcat(detail,sizeof(detail),va(" split=1 split_level=%d kept=%d",split[client].began,kept));
    Event(client,MOVE_EVENT_PLAN,"none",m->path.count?"path":"no_path",detail);
    ForbidPlanCheck(client,&m->path,"replan");
    ReplayPath(client,&s,trap->Milliseconds()-began);
    RouteLine(client,"replan");
}

// Failure attribution (docs/movement/records/2026-09-28-failure-attribution.md).
// The graph is shared by every bot on the server, so a failure is charged to
// what caused it (Graph_EdgeFail): a player's body or a missile is not the
// edge's fault and costs it nothing; only the bot that failed avoids it, for
// MOVE_TRANSIENT_AVOID_MS, so a defender standing on a landing is routed
// around now and the edge is back when it leaves. A live failure (a missed
// flight, a refused crossing) is transient when the live Pmove touched a
// player or missile, or was pushed or knocked back, within
// MOVE_CONTACT_WINDOW, or when a player or missile stands within
// MOVE_TRANSIENT_NEAR of the line from the bot to the edge's landing; a
// search failure is transient when the same search, run again with every
// other player and missile unlinked, finds a candidate (SearchFailed).
#define MOVE_TRANSIENT_AVOID_MS 3000
#define MOVE_CONTACT_WINDOW 1500
#define MOVE_TRANSIENT_NEAR 96.0f

// Adds the edge to this bot's avoid list: for the objective (untilMs 0) or
// until level.time+untilMs; an entry already there keeps the longer term.
// A timed entry never displaces another from a full list, and a timed avoid
// replans at once only when it is new: pinned against a body, every route
// the planner offers is refused, and replanning each frame through the last
// slot was a per-frame loop (records/2026-09-28-failure-attribution.md).
// bot_moveAvoidFull (default 1, the CTF standoff loop of records/2026-11-09-replan-budget.md): an
// untimed entry into a full list overwrote the last slot, which is the newest, and replanned at once. A camper
// carrying at its stand with no Force, its list full, then took turns between two 256 u climb edges that each
// failed (all 66 candidates short) and evicted the other: a replan and 12 ms of climb rollouts every frame, 2,400
// replans a minute. With 1 the oldest untimed entry makes room instead, and a replan forced through a full list
// keeps the replan cadence (MOVE_PATH_RETRY) rather than running at once.
static int AvoidFullMode(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveAvoidFull",value,sizeof(value));
    return value[0]?atoi(value):1;
}
static void AvoidEdge(int client,int edge,int untilMs) {
    movement_t *m=&movement[client];int i,until=untilMs>0?level.time+untilMs:0,added=0,full=0;
    for(i=0;i<m->state.avoidCount;i++)if(m->state.avoid[i]==edge)break;
    if(i==m->state.avoidCount) {
        if(m->state.avoidCount<(int)ARRAY_LEN(m->state.avoid))i=m->state.avoidCount++;
        else if(until) {m->path.replan=1;return;}
        else if(AvoidFullMode()) {
            // the oldest untimed entry goes; the rest keep their order and the new one is last
            int k,oldest=-1;
            for(k=0;k<m->state.avoidCount;k++)if(!m->state.avoidUntil[k]) {oldest=k;break;}
            if(oldest<0)oldest=0;
            for(k=oldest;k+1<m->state.avoidCount;k++) {m->state.avoid[k]=m->state.avoid[k+1];m->state.avoidUntil[k]=m->state.avoidUntil[k+1];}
            i=m->state.avoidCount-1;full=1;
        }
        else i=m->state.avoidCount-1;
        m->state.avoid[i]=edge;m->state.avoidUntil[i]=until;added=1;
    } else if(m->state.avoidUntil[i] && (!until || until>m->state.avoidUntil[i]))m->state.avoidUntil[i]=until;
    // At once: the chord ahead is a flight the bot will not fly.
    m->path.replan=1;
    if((added || !until) && !full)m->path.retryTime=0;
}

// Drops the timed entries that have lapsed; the path is searched again when
// one did, so the edge can come back into the route.
static void AvoidExpire(int client) {
    movement_t *m=&movement[client];int i,kept=0,dropped=0;
    for(i=0;i<m->state.avoidCount;i++) {
        if(m->state.avoidUntil[i] && m->state.avoidUntil[i]<=level.time) {dropped++;continue;}
        m->state.avoid[kept]=m->state.avoid[i];m->state.avoidUntil[kept]=m->state.avoidUntil[i];kept++;
    }
    if(!dropped)return;
    m->state.avoidCount=kept;m->path.replan=1;
    Event(client,MOVE_EVENT_EDGE,"none","avoid_lapsed",va("dropped=%d avoided=%d",dropped,kept));
}

static float SegmentDistance2D(const float *p,const float *a,const float *b) {
    float dx=b[0]-a[0],dy=b[1]-a[1],len=dx*dx+dy*dy,t=0,x,y;
    if(len>0)t=Com_Clamp(0,1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/len);
    x=a[0]+t*dx-p[0];y=a[1]+t*dy-p[1];
    return sqrtf(x*x+y*y);
}

// A live player or a missile near the line from the bot through the edge's
// takeoff to its landing.
static int TransientNear(int client,const graphEdge_t *e) {
    const float *at=g_entities[client].client->ps.origin;float low,high;int i;
    low=min(at[2],min(e->takeoff[2],e->landing[2]))-128;high=max(at[2],max(e->takeoff[2],e->landing[2]))+128;
    for(i=0;i<level.num_entities;i++) {
        const gentity_t *other=&g_entities[i];const float *p=other->r.currentOrigin;
        if(i==client || !other->inuse || !other->r.linked)continue;
        if(i<MAX_CLIENTS || other->s.eType==ET_NPC?(!other->client || other->health<=0):other->s.eType!=ET_MISSILE)continue;
        if(p[2]<low || p[2]>high)continue;
        if(SegmentDistance2D(p,at,e->takeoff)<=MOVE_TRANSIENT_NEAR ||
           SegmentDistance2D(p,e->takeoff,e->landing)<=MOVE_TRANSIENT_NEAR)return 1;
    }
    return 0;
}

static int LiveCause(int client,int edge) {
    const movement_t *m=&movement[client];const graphEdge_t *e=Graph_Edge(edge);
    if(!Graph_FailCause())return GRAPH_CAUSE_WORLD;
    if(m->contact.transient && level.time-m->contact.transient<=MOVE_CONTACT_WINDOW)return GRAPH_CAUSE_TRANSIENT;
    if(e && TransientNear(client,e))return GRAPH_CAUSE_TRANSIENT;
    if(m->contact.mover && level.time-m->contact.mover<=MOVE_CONTACT_WINDOW)return GRAPH_CAUSE_MOVER;
    return GRAPH_CAUSE_WORLD;
}

// A failure the bot has seen: a transient one only this bot avoids, for a
// while; any other is charged to the shared edge by its cause. A guest
// charges nothing and avoids the edge itself: for a while when transient, for
// its objective otherwise.
static void ChargeFailure(int client,int edge,int cause) {
    if(MoveGuest(client)) {AvoidEdge(client,edge,cause==GRAPH_CAUSE_TRANSIENT?MOVE_TRANSIENT_AVOID_MS:0);return;}
    Graph_EdgeFail(edge,cause);
    if(cause==GRAPH_CAUSE_TRANSIENT)AvoidEdge(client,edge,MOVE_TRANSIENT_AVOID_MS);
}

// bot_moveChargeFlown (default 0, off until measured;
// records/2026-11-09-edge-charge-flown.md): 1 charges the shared graph only
// for a strafe edge the bot attempted. Two avoids are of an edge no flight was
// tried on: "walked" (the bot is past the takeoff with no sequence in
// progress) and "declined" (the corridor refused the walk with the edge
// within MOVE_DECLINE_CROSSING path entries, whatever it refused). With 1
// those go on this bot's own avoid list for the objective, as a guest's
// failures do, and write nothing to the edge every bot plans over; "refused"
// (its candidates were judged from inside its entry envelope) and a missed
// flight are charged as before. So is a walked or declined edge the bot's
// last sequence flew (EdgeFlown): it came down short of the edge's landing
// and is on the rest of the chord, which is that flight's result (25 of 321
// walked charges on Kejim, 8 of 185 on Dantooine, eight retained worlds
// each). The avoid line then carries charged=0|1.
static int ChargeFlown(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveChargeFlown",value,sizeof(value));
    return value[0]?atoi(value)!=0:0;
}

// Whether the bot's last committed sequence was a planned chain that flew
// this edge to a landing (the plan stays in place after it completes, until
// the next commit or a miss).
static int EdgeFlown(const movement_t *m,int edge) {
    return m->plan.count && m->plan.kind==MOVE_PLAN_STATE && m->executor.stage>=Onward(&m->plan) &&
           (edge==m->plan.edge || (m->plan.count>1 && edge==m->plan.edge2));
}

// A strafe edge whose flight missed live is avoided by the next state
// search for this objective, and the path is searched again.
static void StateAvoid(int client,int edge,const char *why) {
    movement_t *m=&movement[client];int cause=LiveCause(client,edge),flown=ChargeFlown(),charged=0;
    int unflown=flown && (!strcmp(why,"walked") || !strcmp(why,"declined")) && !EdgeFlown(m,edge);
    // bot_moveFixBX5: an edge walked after a knock interrupted its flight is the hit's, not the world's.
    if(MOVE_FIX(BX5) && Graph_FailCause() && !strcmp(why,"walked") && edge>=0 && knockPlan[client]==m->plan.id &&
       (knockEdges[client][0]==edge+1 || knockEdges[client][1]==edge+1))cause=GRAPH_CAUSE_TRANSIENT;
    if(cause==GRAPH_CAUSE_TRANSIENT)ChargeFailure(client,edge,cause);
    else {AvoidEdge(client,edge,0);if(!MoveGuest(client) && !unflown) {Graph_EdgeFail(edge,cause);charged=1;}}
    // Only with the cvar on does the line carry the field, so the default's line is today's byte for byte.
    Event(client,MOVE_EVENT_EDGE,"strafe","avoid",va("index=%d why=%s avoided=%d cause=%s%s",edge,why,m->state.avoidCount,
        Graph_CauseName(cause),flown?va(" charged=%d",charged):""));
}

// Where the bot stands along the strafe chord that leaves its progress
// cell: the distance past the takeoff, or a negative when before it; 0 when
// the next edge is not a strafe chord (k receives its path index or -1).
static float ChordAlong(const movement_t *m,const playerState_t *ps,int *k) {
    vec3_t a,b;float dx,dy,len;
    *k=-1;
    if(!m->path.count || m->executor.pathIndex<0 || m->executor.pathIndex+1>=m->path.count)return 0;
    if(!PathStrafe(&m->path,m->executor.pathIndex+1))return 0;
    *k=m->executor.pathIndex+1;
    CellPoint(m->path.cells[*k-1],a);CellPoint(m->path.cells[*k],b);
    dx=b[0]-a[0];dy=b[1]-a[1];len=sqrtf(dx*dx+dy*dy);
    return len>0?((ps->origin[0]-a[0])*dx+(ps->origin[1]-a[1])*dy)/len:0;
}

// A completed straight plan is learned as strafe edges, one per hop that
// landed, with the state the committed rollout measured: the launch cell
// to the landing cell, the flight time, the launch speed and heading as
// the entry envelope, the landing speed and heading as the exit state, and
// whether the tap was on a landing frame (the second hop always; the first
// when the plan was re-issued on the landing it was committed at).
static void LearnStrafe(int client) {
    movement_t *m=&movement[client];int h;
    for(h=0;h<m->plan.count && h<2;h++) {
        const bot2_moveMeasured_t *x=&m->plan.measured[h];
        int from,to,edge,merged=0,tap=h>0?1:m->plan.tapAtLanding,cost;char detail[200];
        if(!x->landed || x->frames<=0)continue;
        from=Graph_CellAt(x->takeoff,STEPSIZE+.25f);
        if(from<0)from=Graph_Nearest(x->takeoff,GRAPH_GRID,2*STEPSIZE);
        to=Graph_CellAt(x->landing,STEPSIZE+.25f);
        if(to<0)to=Graph_Nearest(x->landing,GRAPH_GRID,2*STEPSIZE);
        cost=x->frames*Bot2_GameStepMsec();
        edge=from>=0 && to>=0 && from!=to?Graph_AddStrafeEdge(from,to,x->takeoff,x->landing,m->plan.hop[h].dir,cost,tap,
                                                              x->launchSpeed,x->launchDir,x->landSpeed,x->landDir,&merged):-1;
        Com_sprintf(detail,sizeof(detail),"kind=strafe hop=%d index=%d merged=%d from=%d to=%d cost_ms=%d tap=%d entry_speed=%.1f exit_speed=%.1f plan=%d",
            h,edge,merged,from,to,cost,tap,x->launchSpeed,x->landSpeed,m->plan.id);
        Event(client,MOVE_EVENT_EDGE,"strafe",edge>=0?"learn":"unlearned",detail);
    }
}

// A completed gap plan is a validated hop the graph did not have: record it
// as an edge from the launch cell to the landing cell so the next search
// plans it. A completed edge plan validates its edge.
static void PlanCompleted(int client) {
    movement_t *m=&movement[client];
    // A guest's completed plans teach the shared graph nothing; a climb still
    // replans from its top.
    if(MoveGuest(client)) {
        if(m->plan.kind==MOVE_PLAN_CLIMB) {m->path.replan=1;m->path.retryTime=0;}
        return;
    }
    if(m->plan.kind==MOVE_PLAN_CLIMB) {
        // Only a climb ClimbStep judged on its floor at the landing frame
        // reaches the onward stage; a short one ended as a miss. The rise is
        // the landing's, not wherever the onward walk has since taken it.
        Graph_EdgeResult(m->plan.edge,1);
        Event(client,MOVE_EVENT_EDGE,"climb","reached",
              va("edge=%d rise=%.1f landing_z=%.1f frames=%d",m->plan.edge,m->executor.mark[2]-m->plan.origin[2],m->plan.landing[2],m->executor.climbFrame));
        m->path.replan=1;m->path.retryTime=0;
    }
    else if(m->plan.kind==MOVE_PLAN_EDGE)Graph_EdgeResult(m->plan.edge,1);
    else if(m->plan.kind==MOVE_PLAN_STATE) {
        // A validated chain: each edge succeeds and keeps the time it took.
        int h;
        for(h=0;h<m->plan.count && h<2;h++) {
            int e=h?m->plan.edge2:m->plan.edge;
            if(e<0)continue;
            Graph_EdgeResult(e,1);
            if(m->plan.measured[h].landed && m->plan.measured[h].frames>0)Graph_EdgeCost(e,m->plan.measured[h].frames*Bot2_GameStepMsec());
        }
    } else if(m->plan.kind==MOVE_PLAN_STRAIGHT)LearnStrafe(client);
    else if(m->plan.kind==MOVE_PLAN_GAP) {
        int from=Graph_CellAt(m->plan.takeoff,STEPSIZE+.25f),to=Graph_CellAt(m->plan.landing,STEPSIZE+.25f);
        if(from<0)from=Graph_Nearest(m->plan.takeoff,GRAPH_GRID,2*STEPSIZE);
        if(from>=0 && to>=0 && from!=to && Graph_AddEdge(GRAPH_EDGE_HOP,from,to,m->plan.takeoff,m->plan.landing,m->plan.hop[0].dir,
                                                         m->plan.flightFrames*Bot2_GameStepMsec()+128,1)>=0)
            m->path.replan=1;
    }
}

// The pad edge a jumppad flight follows, chosen when the bot enters the
// trigger and held for the flight (the state's entry time names it). The
// path's next pad edge is taken only when its takeoff touches the trigger
// actually entered; a pad planned nearby is not the one that threw the bot.
// Otherwise the walk crossed an unplanned pad: with a navmesh loaded the
// jumppad state steered at the mesh's landing for it; without one it steered
// at the objective and could fall short into a pit (Dantooine v3, after the
// pickup: records/2026-09-27-navmesh-off.md). The landing is then one of the
// native pad edges leaving that trigger - each is safe floor its discovery
// flew to - preferring one that still reaches the objective, then the
// nearest to it.
static struct {int active,entered,edge,planned;vec3_t landing;} padFlight[MAX_CLIENTS];

// A pad edge's takeoff is a standing point whose hull touches its trigger.
static int PadTakesOff(const graphEdge_t *x,const gentity_t *ent,const gentity_t *push) {
    return x && x->type==GRAPH_EDGE_PAD && !Graph_EdgeOff(x) &&
           x->takeoff[0]+ent->r.maxs[0]>=push->r.absmin[0] && x->takeoff[0]+ent->r.mins[0]<=push->r.absmax[0] &&
           x->takeoff[1]+ent->r.maxs[1]>=push->r.absmin[1] && x->takeoff[1]+ent->r.mins[1]<=push->r.absmax[1] &&
           x->takeoff[2]+ent->r.maxs[2]>=push->r.absmin[2] && x->takeoff[2]+ent->r.mins[2]<=push->r.absmax[2];
}

static int PadFlightEdge(int client) {
    gentity_t *ent=&g_entities[client],*push=NULL;const movement_t *m=&movement[client];const playerState_t *ps=&ent->client->ps;
    vec3_t mins,maxs;int goal,e,k,pass,best=-1,bestReach=0,reachable=1,plannedEdge=-1;float bestD=0,run;
    if(bot2_states[client].state!=BOT_STATE_JUMPPAD) {padFlight[client].active=0;return -1;}
    if(padFlight[client].active && padFlight[client].entered==bot2_states[client].stateTimer)return padFlight[client].edge;
    padFlight[client].active=0;
    VectorAdd(ent->client->ps.origin,ent->r.mins,mins);VectorAdd(ent->client->ps.origin,ent->r.maxs,maxs);
    while((push=Bot2_NextPush(push))!=NULL) {
        if(!push->r.linked || maxs[0]<push->r.absmin[0] || mins[0]>push->r.absmax[0] || maxs[1]<push->r.absmin[1] ||
           mins[1]>push->r.absmax[1] || maxs[2]<push->r.absmin[2] || mins[2]>push->r.absmax[2])continue;
        break;
    }
    if(!push)return -1;
    // Each landing is judged from where the bot entered the trigger, at the
    // wishspeed it has: discovery flew from the trigger's centre, and a
    // carrier walking on at the edge with no Speed fell short of the landing
    // it steered at (records/2026-09-28-laps-falls.md). The path's pad edge
    // first; then the reachable edges leaving the trigger, the ones that
    // still reach the objective first, then nearest to it; with none
    // reachable, the same choice among all of them, as before.
    run=g_speed.value*((ps->fd.forcePowersActive&(1<<FP_SPEED))?Graph_SpeedFactor():1.0f);
    if(m->path.count && m->executor.pathIndex>=0)
        for(k=m->executor.pathIndex+1;k<m->path.count && k<=m->executor.pathIndex+8;k++)
            if(m->path.edges[k]>=0) {
                if(PadTakesOff(Graph_Edge(m->path.edges[k]),ent,push))plannedEdge=m->path.edges[k];
                break;
            }
    if(plannedEdge>=0 && Graph_PadReachable(push,ps->origin,plannedEdge,run,client))best=plannedEdge;
    padFlight[client].planned=best>=0;
    goal=Graph_Nearest(m->objective,64,64);
    for(pass=0;pass<2 && best<0;pass++) {
        for(e=0;e<Graph_EdgeCount();e++) {
            const graphEdge_t *x=Graph_Edge(e);float d;int reach;
            if(!PadTakesOff(x,ent,push) || (!pass && !Graph_PadReachable(push,ps->origin,e,run,client)))continue;
            reach=goal>=0 && Graph_Reaches(x->to,goal);
            d=Distance(x->landing,m->objective);
            if(best<0 || reach>bestReach || (reach==bestReach && d<bestD)) {best=e;bestReach=reach;bestD=d;}
        }
        if(best<0 && !pass)reachable=0;
    }
    if(best<0)return -1;
    VectorCopy(Graph_Edge(best)->landing,padFlight[client].landing);
    padFlight[client].edge=best;padFlight[client].active=1;padFlight[client].entered=bot2_states[client].stateTimer;
    if(!padFlight[client].planned)
        Event(client,MOVE_EVENT_EDGE,"pad",reachable?"thrown":"unreachable",va("edge=%d trigger=%d reaches=%d land=%.1f,%.1f,%.1f planned=%d entry=%.1f,%.1f,%.1f run=%.0f",best,push->s.number,bestReach,
            padFlight[client].landing[0],padFlight[client].landing[1],padFlight[client].landing[2],plannedEdge,
            ps->origin[0],ps->origin[1],ps->origin[2],run));
    return best;
}

// The path's next pad, judged once as the bot comes within MOVE_PAD_LOOK of
// its takeoff: where the bot will enter the trigger (the first point on the
// line from it to the takeoff whose hull touches it), and whether any landing
// of that trigger is reached from there at the wishspeed it has now
// (Graph_PadReachable). A pad is thrown the moment it is touched, so with
// none reachable the only safe choice is before it: this bot avoids the edge
// for the objective and the path is searched again. A carrier with Force 6
// walked onto the (160,192) pad on Kejim and fell short of every landing
// (records/2026-09-28-laps-falls.md). bot_movePadEntry 0 turns it off.
#define MOVE_PAD_LOOK 192.0f
static void PadAhead(int client) {
    movement_t *m=&movement[client];gentity_t *ent=&g_entities[client],*push=NULL;const playerState_t *ps=&ent->client->ps;
    const graphEdge_t *x;vec3_t at;char value[8];float run,dx,dy;int k,i,e,edge=-1,reachable=0;
    if(!m->path.count || m->executor.pathIndex<0 || bot2_states[client].state==BOT_STATE_JUMPPAD)return;
    trap->Cvar_VariableStringBuffer("bot_movePadEntry",value,sizeof(value));
    if(value[0] && !atoi(value))return;
    for(k=m->executor.pathIndex+1;k<m->path.count;k++)if(m->path.edges[k]>=0) {edge=m->path.edges[k];break;}
    if(edge<0 || !(x=Graph_Edge(edge)) || x->type!=GRAPH_EDGE_PAD)return;
    if(m->padAhead.edge==edge && m->padAhead.generation==m->path.generation)return;
    dx=x->takeoff[0]-ps->origin[0];dy=x->takeoff[1]-ps->origin[1];
    if(dx*dx+dy*dy>MOVE_PAD_LOOK*MOVE_PAD_LOOK)return;
    m->padAhead.edge=edge;m->padAhead.generation=m->path.generation;
    while((push=Bot2_NextPush(push))!=NULL)if(push->r.linked && PadTakesOff(x,ent,push))break;
    if(!push)return;
    for(i=0;i<=32;i++) {
        float f=i/32.0f;
        at[0]=ps->origin[0]+(x->takeoff[0]-ps->origin[0])*f;at[1]=ps->origin[1]+(x->takeoff[1]-ps->origin[1])*f;at[2]=ps->origin[2]+(x->takeoff[2]-ps->origin[2])*f;
        if(at[0]+ent->r.maxs[0]>=push->r.absmin[0] && at[0]+ent->r.mins[0]<=push->r.absmax[0] &&
           at[1]+ent->r.maxs[1]>=push->r.absmin[1] && at[1]+ent->r.mins[1]<=push->r.absmax[1] &&
           at[2]+ent->r.maxs[2]>=push->r.absmin[2] && at[2]+ent->r.mins[2]<=push->r.absmax[2])break;
    }
    run=g_speed.value*((ps->fd.forcePowersActive&(1<<FP_SPEED))?Graph_SpeedFactor():1.0f);
    // Reached from the predicted entry and from 12 and 24 units farther in
    // along the approach (the touch registered a frame later, up to about
    // 500 u/s): a landing reached from one of them alone is a knife edge
    // (1.7 units of entry turned the (160,192) pad's south-west landing from
    // reached to not).
    dx=x->takeoff[0]-ps->origin[0];dy=x->takeoff[1]-ps->origin[1];
    {float l=sqrtf(dx*dx+dy*dy);if(l>1){dx/=l;dy/=l;}else{dx=dy=0;}}
    for(e=0;e<Graph_EdgeCount() && !reachable;e++) {
        int d,all=PadTakesOff(Graph_Edge(e),ent,push);
        for(d=0;d<3 && all;d++) {
            vec3_t in;VectorCopy(at,in);in[0]+=dx*12*d;in[1]+=dy*12*d;
            all=Graph_PadReachable(push,in,e,run,client);
        }
        if(all)reachable=e+1;
    }

    if(!reachable)AvoidEdge(client,edge,0);
    Event(client,MOVE_EVENT_EDGE,"pad",reachable?"ahead":"avoid",va("edge=%d trigger=%d reachable_edge=%d entry=%.1f,%.1f,%.1f entered=%d run=%.0f avoided=%d",
        edge,push->s.number,reachable-1,at[0],at[1],at[2],i<=32,run,m->state.avoidCount));
}

// The landing of the pad edge this flight follows, for the retained jumppad
// state to steer at in the air: a native pad's landing is where the arc
// comes down under that steering (bot2_graph.c, Pads). Only with
// bot_moveNativeEdges, so a navmesh arm steers as before.
int Move_PadLanding(int client,vec3_t landing) {
    if(client<0 || client>=MAX_CLIENTS || !Graph_NativeEdges() || PadFlightEdge(client)<0)return 0;
    VectorCopy(padFlight[client].landing,landing);
    return 1;
}

// Pad edges are validated and timed by execution. When the retained
// jumppad state takes the bot through the trigger of the path's next pad
// edge, the interval until the walk resumes is the edge's measured cost.
static void PadObserve(int client,int walked) {
    movement_t *m=&movement[client];
    if(m->pad.edge<0 && !walked && bot2_states[client].state==BOT_STATE_JUMPPAD) {
        int e=PadFlightEdge(client);
        if(e>=0 && padFlight[client].planned) {m->pad.edge=e;m->pad.startTime=level.time;}
    } else if(m->pad.edge>=0 && walked && Graph_Edge(m->pad.edge)) {   // an index a learned-set reset dropped is no edge
        // The measured time is the flight plus the walk from where the bot
        // actually landed to the edge's landing point, at the run speed.
        const graphEdge_t *e=Graph_Edge(m->pad.edge);const playerState_t *ps=&g_entities[client].client->ps;
        float dx=e->landing[0]-ps->origin[0],dy=e->landing[1]-ps->origin[1];
        int cost=level.time-m->pad.startTime+(int)(sqrtf(dx*dx+dy*dy)/250*1000);char detail[128];
        if(!MoveGuest(client)) {Graph_EdgeCost(m->pad.edge,cost);Graph_EdgeResult(m->pad.edge,1);}
        Com_sprintf(detail,sizeof(detail),"kind=pad edge=%d flight_ms=%d cost_ms=%d landing_error=%.1f",m->pad.edge,level.time-m->pad.startTime,cost,sqrtf(dx*dx+dy*dy));
        Event(client,MOVE_EVENT_EDGE,"pad","complete",detail);
        m->pad.edge=-1;
    } else if(m->pad.edge>=0 && bot2_states[client].state!=BOT_STATE_JUMPPAD && level.time-m->pad.startTime>10000) m->pad.edge=-1;
}

// The path left from the progress cell at the walk's speed, as the graph
// prices its links, plus the graph's per-edge charge (Graph_PriceMs).
static int PathRemainingMs(const bot2_movePath_t *path,int index) {
    int k,ms=0;
    if(index<0 || index>=path->count)return 0;
    for(k=index+1;k<path->count;k++)ms+=Graph_PriceMs(path->cells[k-1],path->cells[k],path->edges[k]);
    return ms;
}

// The Speed edge: a purchase of the power's cost that raises the wishspeed
// of every later edge for its window. The plan takes it here, on a grounded
// frame the walk owns, when the policy permits it, the pool affords it and
// the path left is long enough to be worth the window; the executor never
// issues it. The game activates the power after this frame's Pmove, so the
// request is confirmed on the next frame by the active bit, and the
// forecast applies the same activation to a command that carries it.
static void SpeedEdge(int client,usercmd_t *cmd) {
    movement_t *m=&movement[client];const playerState_t *ps=&g_entities[client].client->ps;
    char detail[96];int remaining;
    if(m->budget.speedRequest) {
        if(level.time<=m->budget.speedRequest)return;
        if(ps->fd.forcePowersActive&(1<<FP_SPEED)) {
            m->budget.purchases++;
            Com_sprintf(detail,sizeof(detail),"kind=speed purchases=%d window_ms=%d",m->budget.purchases,ps->fd.forcePowerDuration[FP_SPEED]-level.time);
            Event(client,MOVE_EVENT_EDGE,"speed","complete",detail);
        } else {
            m->budget.speedRetry=level.time+MOVE_SPEED_RETRY;
            Event(client,MOVE_EVENT_MISS,"speed","speed_refused",NULL);
        }
        m->budget.speedRequest=0;
        return;
    }
    if(!cmd || !(m->budget.spend&MOVE_SPEND_SPEED) || (ps->fd.forcePowersActive&(1<<FP_SPEED)))return;
    if(!Bot2_StandableEntity(ps->groundEntityNum) || level.time<m->budget.speedRetry)return;
    // Not while a committed sequence has hops ahead (the condition that keeps
    // Replan out of a sequence): its hops were rolled out and admitted at the
    // wishspeed without Speed, and flown at Speed's they carry past their
    // floor. In laps play the pool regenerates across the cost mid-sequence,
    // and five pit falls were this (records/2026-09-28-laps-falls.md). The
    // purchase comes on the next frame with none ahead, and the next search
    // rolls out with Speed active.
    if(m->plan.count && m->executor.stage<Onward(&m->plan))return;
    if(ps->fd.forcePower<forcePowerNeeded[ps->fd.forcePowerLevel[FP_SPEED]][FP_SPEED])return;
    if(!m->path.count || m->executor.pathIndex<0)return;
    remaining=PathRemainingMs(&m->path,m->executor.pathIndex);
    if(remaining<MOVE_SPEED_MIN_MS)return;
    cmd->generic_cmd=GENCMD_FORCE_SPEED;
    m->budget.speedRequest=level.time;
    // A plan decision the step function does not make is a declared
    // discontinuity for the parity harness, as a replan is; the horizon
    // rebuilt at this boundary carries the purchase and is compared.
    ParityReset(client);
    PermitRevoke(client,"speed");
    Com_sprintf(detail,sizeof(detail),"kind=speed remaining_ms=%d force=%d",remaining,ps->fd.forcePower);
    Event(client,MOVE_EVENT_EDGE,"speed","request",detail);
}

int Move_Plan(int client,const vec3_t goal) {
    movement_t *m;
    if(client<0 || client>=MAX_CLIENTS)return 0;
    m=&movement[client];
    // A leg start that could not name its objective is completed here, before
    // this frame's search, so the leg's first decision is already under the
    // tape the bot selected.
    TapeLibraryResolve(client);
    // Objective change is one of the four reset paths. The executor state
    // starts over; the plan is discovered again at the next grounded boundary.
    // Under bot_moveArriveSafe the reset waits while a committed sequence is
    // between its launch and its last landing: the pickup comes mid-hop, and
    // the reset handed the flight to the walk, which kept steering and
    // strafing in the air (526 to 707 u/s on Dantooine) and carried the
    // carrier off the flag's platform into the hurt water. The flight the
    // arrival check judged safe is flown to its landing, and the new
    // objective is taken there.
    // bot_moveFixBM5: a knock recovery or a lift ride is held to its end the same way.
    if(m->objectiveSet && Distance(goal,m->objective)>64 && ArriveSafe() &&
       ((m->plan.count && m->executor.stage>MOVE_STAGE_RUNUP0 && m->executor.stage<Onward(&m->plan)) ||
        (MOVE_FIX(BM5) && (m->knock.active || m->knock.pending || m->lift.edge>=0)))) {
        if(!m->objectiveHeld) {
            m->objectiveHeld=level.time;
            Event(client,MOVE_EVENT_PLAN,"none","objective_held",NULL);
        }
    } else if(m->objectiveSet && Distance(goal,m->objective)>64) {
        Move_Reset(client);
        // The leg boundary. With bot_moveGraphSaveLeg set, a deferred cache
        // snapshot is written here, so a crash loses at most the leg in
        // progress. Off by default: in a live match every
        // pickup and capture is a leg boundary, and writing the 14 MB Kejim
        // cache on the game thread stalled the server 0.5-2.8 s each time
        // (ping 999 in play). The write at shutdown (G_ShutdownGame) still
        // keeps the session's learning. Nothing in the session reads the
        // file, so no decision changes either way.
        if(trap->Cvar_VariableIntegerValue("bot_moveGraphSaveLeg"))Graph_SaveFlush();
        VectorCopy(goal,m->objective);m->objectiveSet=1;
        Event(client,MOVE_EVENT_PLAN,"none","objective",NULL);
        // A leg boundary is the bot's own, so the library reselects here as
        // well as at the fixture's release - but only when the leg-id itself
        // changed. The cursor cannot restart on every objective change: one
        // fires mid-trip as the goal position slides, and the first
        // self-control lost its tape to exactly that.
        if(TapeLibraryOwns() && !MoveGuest(client) && g_entities[client].client) {
            char leg[32];
            TapeLegId(client,leg,sizeof(leg));
            if(strcmp(leg,tapeLibrary.leg))
                Move_DecisionTapeReset(client,Move_RootCell(g_entities[client].client->ps.origin),
                                       level.time%MOVE_TAPE_PHASE);
        }
    } else if(!m->objectiveSet) {
        VectorCopy(goal,m->objective);m->objectiveSet=1;
        Event(client,MOVE_EVENT_PLAN,"none","walk",NULL);
    }
    return m->plan.count?1:0;
}

// One record per executed stage crossing: what the live frame reached,
// what the committed rollout predicted for that hop, and what the graph
// charged for the edge the plan claims to be flying. costMs on a strafe
// edge is launch-to-landing time alone, so it is comparable with the
// flight between this crossing and the previous one and with nothing else.
static void ReplayStage(int client,int stage) {
    movement_t *m=&movement[client];const playerState_t *ps=&g_entities[client].client->ps;
    int hop=stage>>1,onward=stage>=Onward(&m->plan),edge;const graphEdge_t *e;
    if(!(ReplayFlags()&1))return;
    edge=onward?-1:(hop?m->plan.edge2:m->plan.edge);
    e=edge>=0?Graph_Edge(edge):NULL;
    Bot2_PrintTelemetry(1,"[REPLAY] kind=stage client=%d level=%d gen=%d plan=%d plan_kind=%s stage=%d hop=%d crossing=%s edge=%d "
        "pos=%.3f,%.3f,%.3f vel=%.3f,%.3f,%.3f speed=%.3f ground=%d force=%d "
        "predicted_launch_speed=%.3f predicted_land_speed=%.3f predicted_frames=%d predicted_landing=%.3f,%.3f,%.3f "
        "graph_cost_ms=%d graph_tap=%d graph_entry_speed=%.3f graph_exit_speed=%.3f graph_landing=%.3f,%.3f,%.3f\n",
        client,level.time,m->path.generation,m->plan.id,PlanKind(m->plan.kind),stage,onward?-1:hop,
        onward?"complete":(stage&1)?"land":"launch",edge,
        ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],Speed2D(ps),
        ps->groundEntityNum,ps->fd.forcePower,
        onward?0:m->plan.measured[hop].launchSpeed,onward?0:m->plan.measured[hop].landSpeed,
        onward?0:m->plan.measured[hop].frames,
        onward?0:m->plan.measured[hop].landing[0],onward?0:m->plan.measured[hop].landing[1],onward?0:m->plan.measured[hop].landing[2],
        e?e->costMs:-1,e?e->tap:-1,e?e->entrySpeed:-1,e?e->exitSpeed:-1,
        e?e->landing[0]:0,e?e->landing[1]:0,e?e->landing[2]:0);
}

// A decline the movement owner keeps. Formerly a frame the walk
// declined went to the legacy owner, whose stuck recovery jumped the bot
// 1,776 units into the Kejim pit on all eleven seeded respawn4 deaths
// (docs/movement/records/2026-09-22-seed-deaths-and-planner-reach.md): the
// planner routed over a pit crossing, the walk declined at `floor_missing` on
// the lip, and the hand-off did the rest. The walk's own declines - a
// corridor refusal of any reason, no navmesh path off the route, a missed hop
// - are now held by the one owner: the route's next crossing within reach is
// avoided and the route searched again at once, and until a route the walk
// accepts exists the bot holds the walk's brake, the command the walk itself
// issues at a hazard, and the replan retries on its cadence. The declines
// that yield to a different mechanism keep the hand-off: a non-normal player
// movement type, a retained non-walk state the legacy machine set, and an
// off-mesh connection inside the yield radius. The frame stays unowned for
// the searches and the parity harness - the walk did not produce it - and
// nothing here reads a clock or runs a search; a frame nothing declined never
// reaches it.
#define MOVE_DECLINE_CROSSING 4     // path entries ahead of the progress cell a declined crossing is looked for in
#define MOVE_STEPOFF_RADIUS 96.0f   // units: how far a stranded bot looks for a cell below to walk off to

// The nearest graph cell below a position the graph does not cover that the
// bot can walk off to: lower than a step, no further down than a fall the
// forecast allows (MOVE_FALL_DROP), within MOVE_STEPOFF_RADIUS, floor now
// (Graph_CellStands: a lift that has left is not), not a hurt or liquid cell,
// and beyond the support - the player box walks level to above the cell
// unblocked and falls from there onto it. The floor directly beneath the prop
// fails the fall and a cell behind a wall the walk; without those tests the
// nearest cell could be one the bot stands over, and the walk towards it asked
// for no velocity at all. -1 when there is none.
// bot_moveStuckBail (default on). A hold on a corridor refusal that is the
// floor probe's own geometry - floor_startsolid or floor_allsolid, the probe
// starting STEPSIZE above the bot inside an overhang - is not a hazard, but
// every replan finds the same route and the same refusal: the runner held a
// carried flag on a Kejim chasm ledge for 135 s, 272 replans
// (records/2026-10-01-knock-replays.md, "Runner stuck bail-out"). After
// MOVE_STUCK_MS within MOVE_STUCK_RADIUS of where the hold began, the bot
// walks straight at the refused target for MOVE_BAIL_MS when its hull
// reaches it unobstructed, and hops toward it from the third bail on.
#define MOVE_STUCK_MS 2000
#define MOVE_STUCK_RADIUS 16.0f
#define MOVE_BAIL_MS 400
static int StuckBailOn(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveStuckBail",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// The answer is kept per client while the bot stands at exactly the same origin on the same graph, for up to
// MOVE_STEPOFF_KEEP_MS: a stranded hold asked every frame (49 grid points, up to two hull
// traces each) and Move_Stranded twice a second, for up to 10 s before the stranded kill, from a bot standing still.
// A body walking through a probe's trace within the window is seen at the next ask after it.
#define MOVE_STEPOFF_KEEP_MS 500
static struct {vec3_t origin;unsigned int generation;int time,start,cell,valid;} stepOffMemo[MAX_CLIENTS];
static int StepOffCellSearch(int client,const vec3_t origin);
static int StepOffCell(int client,const vec3_t origin) {
    int k=client>=0 && client<MAX_CLIENTS?client:-1;
    if(k>=0 && stepOffMemo[k].valid && VectorCompare(stepOffMemo[k].origin,origin) &&
       stepOffMemo[k].generation==Graph_Generation() && stepOffMemo[k].start==level.startTime &&
       level.time>=stepOffMemo[k].time && level.time-stepOffMemo[k].time<MOVE_STEPOFF_KEEP_MS)return stepOffMemo[k].cell;
    if(k<0)return StepOffCellSearch(client,origin);
    VectorCopy(origin,stepOffMemo[k].origin);stepOffMemo[k].generation=Graph_Generation();stepOffMemo[k].start=level.startTime;
    stepOffMemo[k].time=level.time;stepOffMemo[k].valid=1;
    return stepOffMemo[k].cell=StepOffCellSearch(client,origin);
}
static int StepOffCellSearch(int client,const vec3_t origin) {
    static vec3_t mins={-15,-15,DEFAULT_MINS_2},maxs={15,15,DEFAULT_MAXS_2};
    const float half=(MOVE_FALL_DROP-STEPSIZE)*.5f;
    int cx=(int)floorf(origin[0]/GRAPH_GRID+.5f),cy=(int)floorf(origin[1]/GRAPH_GRID+.5f);
    int span=(int)ceilf(MOVE_STEPOFF_RADIUS/GRAPH_GRID),ix,iy,cell,best=-1;
    float bestD=MOVE_STEPOFF_RADIUS*MOVE_STEPOFF_RADIUS+1;
    for(ix=cx-span;ix<=cx+span;ix++)for(iy=cy-span;iy<=cy+span;iy++) {
        vec3_t probe,above,below;trace_t tr;const graphCell_t *c;float dx,dy,d;
        probe[0]=ix*(float)GRAPH_GRID;probe[1]=iy*(float)GRAPH_GRID;probe[2]=origin[2]-STEPSIZE-half;
        dx=probe[0]-origin[0];dy=probe[1]-origin[1];d=dx*dx+dy*dy;
        if(d<1 || d>=bestD)continue;
        cell=Graph_CellAt(probe,half);
        if(cell<0 || !Graph_CellStands(cell))continue;
        c=Graph_Cell(cell);
        if(c->flags&(GRAPH_HAZARD_HURT|GRAPH_HAZARD_LIQUID))continue;
        VectorSet(above,probe[0],probe[1],origin[2]);
        trap->Trace(&tr,origin,mins,maxs,above,client,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.allsolid || tr.fraction<1)continue;
        VectorSet(below,probe[0],probe[1],c->z-1);
        trap->Trace(&tr,above,mins,maxs,below,client,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.allsolid || tr.endpos[2]>c->z+STEPSIZE)continue;
        bestD=d;best=cell;
    }
    return best;
}

qboolean Move_Stranded(int client) {
    const playerState_t *ps;
    if(client<0 || client>=MAX_CLIENTS || !g_entities[client].client || Graph_CellCount()<=0)return qfalse;
    ps=&g_entities[client].client->ps;
    return ps->groundEntityNum!=ENTITYNUM_NONE && Move_RootCell(ps->origin)<0 && StepOffCell(client,ps->origin)<0;
}

static int DeclineHeld(int client,const char *reason,usercmd_t *cmd) {
    movement_t *m=&movement[client];bot2_moveExecutor_t *x=&m->executor;
    const playerState_t *ps=&g_entities[client].client->ps;
    bot2_moveIntent_t intent;float yaw;int k,edge=-1,avoided=0;char detail[160];
    // An off-mesh yield is reached only when the graph has no path and the
    // navmesh routes through an off-mesh link; handing that to the legacy
    // owner let it wander for the whole bound on ctf4, whose levels join by
    // lifts the graph cannot yet model. Held like a no-path decline.
    if(x->decline!=MOVE_DECLINE_CORRIDOR && x->decline!=MOVE_DECLINE_NO_PATH && x->decline!=MOVE_DECLINE_MISS &&
       x->decline!=MOVE_DECLINE_OFFMESH)return 0;
    // A sequence still in progress would keep the replan gate shut: its
    // onward walk is released as the onward bound releases it, and a run-up
    // the walk declined ends as a miss does.
    if(m->plan.count && x->stage<=Onward(&m->plan)) {
        if(x->stage==Onward(&m->plan))x->stage=Onward(&m->plan)+1;
        else {m->plan.count=0;x->stage=0;}
    }
    // The route's next crossing, if the walk was heading for one: that is
    // the flight the decline refuses, so the next search routes without it.
    // Charged once per route; the same route declining again waits for the
    // cadence rather than failing the edge every frame.
    // A miss has already charged its own edge and ended its sequence above,
    // so it is held for the frame and nothing more.
    if(x->decline!=MOVE_DECLINE_MISS && m->path.count && x->pathIndex>=0) {
        for(k=x->pathIndex+1;k<m->path.count && k<=x->pathIndex+MOVE_DECLINE_CROSSING;k++)
            if(m->path.edges[k]>=0) {edge=m->path.edges[k];break;}
    }
    if(edge>=0 && (edge!=m->declineHold.edge || m->path.generation!=m->declineHold.generation)) {
        m->declineHold.edge=edge;m->declineHold.generation=m->path.generation;
        StateAvoid(client,edge,"declined");
        avoided=1;
    } else if(x->decline!=MOVE_DECLINE_MISS)m->path.replan=1;   // on the cadence the last replan set
    if(!m->declineHold.active || avoided) {
        if(!m->declineHold.active)m->declineHold.episodes++;
        Com_sprintf(detail,sizeof(detail),"decline=%s path_index=%d crossing=%d avoided=%d avoid=%d gen=%d episodes=%d",
            DeclineName(x->decline),x->pathIndex,edge,avoided,m->state.avoidCount,m->path.generation,m->declineHold.episodes);
        Event(client,MOVE_EVENT_RECOVER,"none","hold",detail);
    }
    m->declineHold.active=1;m->declineHold.frames++;
    (void)reason;
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=level.time;cmd->weapon=ps->weapon;
    memset(&intent,0,sizeof(intent));intent.turnRate=360;intent.allowStop=qtrue;
    // Stranded off the graph: no cell under the bot, so no route can start
    // and the brake below would hold it for good. Seen in play: a
    // flight committed before a capture carried on onto a prop 56 units above
    // Kejim's blue-stand floor, and the runner stood there for the rest of the
    // match. Walk off towards the nearest cell below within a safe drop; on it
    // the next replan starts a route.
    if(ps->groundEntityNum!=ENTITYNUM_NONE && Move_RootCell(ps->origin)<0) {
        int below=StepOffCell(client,ps->origin);
        if(below>=0) {
            const graphCell_t *c=Graph_Cell(below);
            float dx=c->ix*(float)GRAPH_GRID-ps->origin[0],dy=c->iy*(float)GRAPH_GRID-ps->origin[1],d=sqrtf(dx*dx+dy*dy);
            if(d>1) {intent.velocity[0]=dx/d*ps->basespeed;intent.velocity[1]=dy/d*ps->basespeed;}
            if(m->declineHold.stepOff!=below+1) {
                m->declineHold.stepOff=below+1;
                Event(client,MOVE_EVENT_RECOVER,"none","step_off",va("cell=%d drop=%.1f distance=%.1f",below,ps->origin[2]-c->z,d));
            }
            Bot2_MoveControl(ps,&intent,ps->viewangles[YAW],Bot2_GameStepMsec()*.001f,qtrue,cmd,&yaw);
            return 1;
        }
    }
    if(StuckBailOn() && x->decline==MOVE_DECLINE_CORRIDOR && ps->groundEntityNum!=ENTITYNUM_NONE && reason &&
       (!strcmp(reason,"floor_startsolid") || !strcmp(reason,"floor_allsolid"))) {
        float dx=x->waypoint[0]-ps->origin[0],dy=x->waypoint[1]-ps->origin[1],d=sqrtf(dx*dx+dy*dy);
        if(!m->declineHold.stuckSince || Distance(ps->origin,m->declineHold.stuckOrigin)>MOVE_STUCK_RADIUS) {
            m->declineHold.stuckSince=level.time;m->declineHold.bailUntil=0;m->declineHold.bails=0;
            VectorCopy(ps->origin,m->declineHold.stuckOrigin);
        } else if(d>1 && (level.time<m->declineHold.bailUntil || level.time-m->declineHold.stuckSince>=MOVE_STUCK_MS)) {
            static vec3_t mins={-15,-15,DEFAULT_MINS_2},maxs={15,15,DEFAULT_MAXS_2};
            vec3_t end;trace_t tr;int hop;
            VectorSet(end,x->waypoint[0],x->waypoint[1],ps->origin[2]);
            trap->Trace(&tr,ps->origin,mins,maxs,end,client,MASK_PLAYERSOLID,qfalse,0,0);
            if(level.time>=m->declineHold.bailUntil) {
                m->declineHold.bailUntil=level.time+MOVE_BAIL_MS;m->declineHold.bails++;
                m->declineHold.stuckSince=level.time-MOVE_STUCK_MS+MOVE_BAIL_MS;   // the next bail follows this one
                Event(client,MOVE_EVENT_RECOVER,"none","stuck_bail",va("why=%s bails=%d clear=%.2f target=%.1f,%.1f,%.1f distance=%.1f",
                    reason,m->declineHold.bails,tr.fraction,x->waypoint[0],x->waypoint[1],x->waypoint[2],d));
            }
            hop=m->declineHold.bails>=3;
            // A hull touching a wall starts solid (the runner wedged 0.003 u into a corner); it can
            // still move out, so only allsolid or a blocked path refuses the walk.
            if(hop || (!tr.allsolid && tr.fraction>=1)) {
                intent.velocity[0]=dx/d*ps->basespeed;intent.velocity[1]=dy/d*ps->basespeed;
                Bot2_MoveControl(ps,&intent,ps->viewangles[YAW],Bot2_GameStepMsec()*.001f,qtrue,cmd,&yaw);
                if(hop)cmd->upmove=127;
                return 1;
            }
        }
    } else m->declineHold.stuckSince=0;
    // The walk's brake at the current view: no wish velocity, stop allowed.
    Bot2_MoveControl(ps,&intent,ps->viewangles[YAW],Bot2_GameStepMsec()*.001f,qtrue,cmd,&yaw);
    return 1;
}

// Lift rides (GRAPH_EDGE_LIFT, built by bot2_graph.c Lifts; the edge's tap is
// the lift's inline model). The walk brakes onto a ride's takeoff as it does
// onto any edge's, and from the frame the bot is there with the ride next on
// its path, the ride owns the frames:
// - up, from the boarding cell on the resting lift: board (hold on the
//   boarding point until the call the bot stands in starts the lift), ride
//   (stand still while it moves), exit (walk to the edge's landing once the
//   lift rests at its far end, before its wait runs out);
// - down, from a call beside the raised platform: call (hold in the call until
//   the lift rests at the top), board (step onto its middle and stay on it
//   until it leaves), ride, exit.
// Riding is executed, never rolled out - the forecast freezes every mover - so
// the edge is validated and timed by the ride itself. A ride that leaves the
// lift, misses it, is not started or cannot get off is a miss charged to the
// edge. The planner never routes onto a lift that is not resting where its
// cells were sampled (Graph_CellStands), and the walk refuses the empty shaft
// as a drop, which keeps the bot out from under a lift that could come down.
#define MOVE_LIFT_REACH 24.0f       // the takeoff and the landing are reached within this
#define MOVE_LIFT_BOARD_MS 1500     // past the edge's cost, a lift that has not moved is a miss
#define MOVE_LIFT_CALL_MS 8000      // a called lift that has not arrived by then is a miss (a call re-arms in 6 s on ctf4)
#define MOVE_LIFT_RIDE_MS 6000      // a ride that has not arrived by then is a miss
#define MOVE_LIFT_EXIT_MS 2500      // the lift waits 3 s at the far end on ctf4
enum {MOVE_LIFT_CALL=1,MOVE_LIFT_BOARD,MOVE_LIFT_RIDE,MOVE_LIFT_EXIT};
static const char *LiftPhaseName(int phase) {
    return phase==MOVE_LIFT_CALL?"call":phase==MOVE_LIFT_BOARD?"board":phase==MOVE_LIFT_RIDE?"ride":phase==MOVE_LIFT_EXIT?"exit":"none";
}

// success 1: the ride completed; 0: it failed, and the edge is charged; -1:
// a hit ended it, which charges no edge, as a knocked sequence charges none.
static void LiftEnd(int client,int success,const char *why) {
    movement_t *m=&movement[client];int edge=m->lift.edge;
    char detail[128];
    Com_sprintf(detail,sizeof(detail),"kind=lift id=%d edge=%d phase=%s ms=%d",m->lift.id,edge,LiftPhaseName(m->lift.phase),level.time-m->lift.startTime);
    if(success>0) {
        if(!MoveGuest(client)) {Graph_EdgeCost(edge,level.time-m->lift.startTime);Graph_EdgeResult(edge,1);}
        Event(client,MOVE_EVENT_EDGE,"lift","complete",detail);
    } else {
        if(!success && !MoveGuest(client))Graph_EdgeFail(edge,Graph_FailCause()?GRAPH_CAUSE_MOVER:GRAPH_CAUSE_WORLD);
        Event(client,MOVE_EVENT_MISS,"lift",why,detail);
    }
    m->lift.edge=-1;m->lift.phase=0;
    m->path.replan=1;m->path.retryTime=0;
}

static void LiftPhase(int client,int phase) {
    movement_t *m=&movement[client];
    m->lift.phase=phase;m->lift.phaseTime=level.time;
    Event(client,MOVE_EVENT_EDGE,"lift",LiftPhaseName(phase),va("id=%d ms=%d",m->lift.id,level.time-m->lift.startTime));
}

// Steer toward a point at up to the given speed, braking to a stop on it.
static void LiftSteer(const playerState_t *ps,const vec3_t point,float speed,usercmd_t *cmd) {
    bot2_moveIntent_t intent;float yaw,dx=point[0]-ps->origin[0],dy=point[1]-ps->origin[1],dist=sqrtf(dx*dx+dy*dy);
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=level.time;cmd->weapon=ps->weapon;
    memset(&intent,0,sizeof(intent));intent.turnRate=360;intent.allowStop=qtrue;
    speed=min(speed,max(0,(dist-4)*4));
    if(dist>1) {intent.velocity[0]=dx/dist*speed;intent.velocity[1]=dy/dist*speed;}
    Bot2_MoveControl(ps,&intent,ps->viewangles[YAW],Bot2_GameStepMsec()*.001f,qtrue,cmd,&yaw);
}

// Whether a ride goes down to the lift's rest position: its takeoff is a
// world cell (a call beside the raised platform), not a lift cell.
static int LiftDown(const graphEdge_t *e) {
    const graphCell_t *from=Graph_Cell(e->from);
    return from && !from->mover;
}

// Returns 1 when the ride owns the frame and has written the command.
static int LiftRide(int client,usercmd_t *cmd) {
    movement_t *m=&movement[client];const playerState_t *ps=&g_entities[client].client->ps;
    const graphEdge_t *e;const gentity_t *lift;usercmd_t step;float dx,dy;int k,down,rest;
    if(m->lift.edge<0) {
        int edge=-1,entity;
        if(m->knock.active || m->knock.pending || (m->plan.count && m->executor.stage<=Onward(&m->plan)) || !m->path.count || m->executor.pathIndex<0)return 0;
        for(k=m->executor.pathIndex+1;k<m->path.count && k<=m->executor.pathIndex+2;k++)
            if(m->path.edges[k]>=0) {
                e=Graph_Edge(m->path.edges[k]);
                if(e && e->type==GRAPH_EDGE_LIFT && !Graph_EdgeOff(e))edge=m->path.edges[k];
                break;
            }
        if(edge<0)return 0;
        e=Graph_Edge(edge);entity=Bot2_MoverEntity(e->tap);
        if(entity<0)return 0;
        if(LiftDown(e)) {
            // At the call, on the ground. The walk brakes onto the takeoff
            // cell's point, which need not be the takeoff: on ctf4 (*21) it
            // stopped 29.6 units from the takeoff with the ride waiting for 24,
            // and neither moved again. The call phase steers onto
            // the takeoff itself, so the ride starts from the cell or near it.
            dx=e->takeoff[0]-ps->origin[0];dy=e->takeoff[1]-ps->origin[1];
            if(ps->groundEntityNum==ENTITYNUM_NONE)return 0;
            if(dx*dx+dy*dy>4*MOVE_LIFT_REACH*MOVE_LIFT_REACH && Graph_CellAt(ps->origin,STEPSIZE+.25f)!=e->from)return 0;
            m->lift.phase=MOVE_LIFT_CALL;
        } else {
            // On the lift, from any point of it: a lift that starts before the
            // walk reaches the boarding point is ridden from where the bot stands.
            if(ps->groundEntityNum!=entity)return 0;
            m->lift.phase=Bot2_StandableEntity(entity)?MOVE_LIFT_BOARD:MOVE_LIFT_RIDE;
        }
        m->lift.edge=edge;m->lift.entity=entity;m->lift.id++;
        m->lift.startTime=m->lift.phaseTime=level.time;
        PermitRevoke(client,"lift");
        Event(client,MOVE_EVENT_PLAN,"lift","commit",va("kind=lift id=%d edge=%d entity=%d down=%d phase=%s cost_ms=%d to=%d",
            m->lift.id,edge,entity,LiftDown(e),LiftPhaseName(m->lift.phase),e->costMs,e->to));
    }
    e=Graph_Edge(m->lift.edge);lift=&g_entities[m->lift.entity];
    if(!e || ps->pm_type!=PM_NORMAL) {LiftEnd(client,0,"movement_type");return 0;}
    down=LiftDown(e);
    // Every lift cell was sampled at the rest position (MOVER_POS1): an up
    // ride leaves it, a down ride returns to it.
    rest=MOVER_POS1;
    if(m->lift.phase==MOVE_LIFT_CALL) {
        if(Bot2_StandableEntity(m->lift.entity) && lift->moverState!=rest)LiftPhase(client,MOVE_LIFT_BOARD);
        else if(level.time-m->lift.phaseTime>MOVE_LIFT_CALL_MS) {LiftEnd(client,0,"no_call");return 0;}
        else {LiftSteer(ps,e->takeoff,Wishspeed(ps),&step);*cmd=step;return 1;}
    }
    if(m->lift.phase==MOVE_LIFT_BOARD) {
        if(!Bot2_StandableEntity(m->lift.entity)) {
            // The lift has left. Down, the bot must be on it.
            if(down && ps->groundEntityNum!=m->lift.entity) {LiftEnd(client,0,"missed");return 0;}
            LiftPhase(client,MOVE_LIFT_RIDE);
        } else if(!down && ps->groundEntityNum!=m->lift.entity && ps->groundEntityNum!=ENTITYNUM_NONE) {LiftEnd(client,0,"left");return 0;}
        else if(level.time-m->lift.phaseTime>(down?max(0,(int)lift->wait):e->costMs)+MOVE_LIFT_BOARD_MS) {LiftEnd(client,0,"no_start");return 0;}
        else if(down) {
            // The middle of the raised platform.
            vec3_t middle;
            middle[0]=(lift->r.absmin[0]+lift->r.absmax[0])*.5f;middle[1]=(lift->r.absmin[1]+lift->r.absmax[1])*.5f;middle[2]=ps->origin[2];
            LiftSteer(ps,middle,Wishspeed(ps),&step);*cmd=step;return 1;
        } else {LiftSteer(ps,e->takeoff,Wishspeed(ps),&step);*cmd=step;return 1;}
    }
    if(m->lift.phase==MOVE_LIFT_RIDE) {
        if(Bot2_StandableEntity(m->lift.entity) && (lift->moverState==rest)==down)LiftPhase(client,MOVE_LIFT_EXIT);
        else if(ps->groundEntityNum!=m->lift.entity && ps->groundEntityNum!=ENTITYNUM_NONE) {LiftEnd(client,0,"left");return 0;}
        else if(level.time-m->lift.phaseTime>MOVE_LIFT_RIDE_MS) {LiftEnd(client,0,"ride_timeout");return 0;}
        else {
            // Stand still where the lift carries the bot.
            vec3_t here;VectorCopy(ps->origin,here);
            LiftSteer(ps,here,0,&step);*cmd=step;return 1;
        }
    }
    // Exit: walk off onto the landing cell. An exit cell can sit inside the
    // lift's bounds (ctf4 *17), where the bot reaches it still standing on
    // the resting lift, so any standable floor there completes the ride.
    dx=e->landing[0]-ps->origin[0];dy=e->landing[1]-ps->origin[1];
    if((dx*dx+dy*dy<=MOVE_LIFT_REACH*MOVE_LIFT_REACH || Graph_CellAt(ps->origin,STEPSIZE+.25f)==e->to) &&
       ps->groundEntityNum!=ENTITYNUM_NONE && Bot2_StandableEntity(ps->groundEntityNum)) {LiftEnd(client,1,NULL);return 0;}
    if(level.time-m->lift.phaseTime>MOVE_LIFT_EXIT_MS) {LiftEnd(client,0,"exit_timeout");return 0;}
    LiftSteer(ps,e->landing,Wishspeed(ps),&step);*cmd=step;
    return 1;
}

// A fixture's declared knock (bot2_defender.c PostState, knock=1): the client
// was placed with a recorded post-knock velocity, which the hit check cannot
// see (its last Pmove velocity is gone with the reset), so the recovery is
// armed as a detected hit would arm it.
void Move_DeclareKnock(int client) {
    movement_t *m;const playerState_t *ps;
    if(client<0 || client>=MAX_CLIENTS || !g_entities[client].client || !KnockRecover())return;
    m=&movement[client];ps=&g_entities[client].client->ps;
    Event(client,MOVE_EVENT_EDGE,"knock","declared",va("vx=%.1f vy=%.1f vz=%.1f",ps->velocity[0],ps->velocity[1],ps->velocity[2]));
    m->knock.pending=1;m->knock.hitTime=level.time;m->knock.searchTime=level.time;
}

qboolean Move_Command(int client,usercmd_t *cmd) {
    movement_t *m;bot2_moveContext_t ctx;usercmd_t step;const char *reason;int walked;
    gentity_t *ent=&g_entities[client];
    if(client<0 || client>=MAX_CLIENTS || !ent->inuse || !ent->client)return qfalse;
    m=&movement[client];
    RoutePendingPrint(client);
    ForbidSync(client);
    if(MoveGuest(client))GuestTraceCompare(client);
    FlightModeRead();
    if(!m->objectiveSet)return qfalse;
    SplitStep(client);
    Graph_PathTableStep();
    m->budget.spend=SpendPolicy();
    // Exploration owns the frame with no motion: the bot stands at its start
    // while the Pmove boundary explores from elsewhere.
    if(trap->Cvar_VariableIntegerValue("bot_moveExplore") && MoveSearchGates(client)) {
        m->executorBefore=m->executor;m->frameTime=level.time;m->owned=1;
        cmd->forwardmove=cmd->rightmove=cmd->upmove=0;
        return qtrue;
    }
    // A hit: the velocity differs from what the last Pmove left. Only the game
    // changes it between the two - G_ApplyKnockback for damage, a Force push -
    // except a teleport or respawn (the teleport bit toggles) and a jump pad
    // (touched after Pmove), which are not hits. The knockback timer is no
    // signal: G_ApplyKnockback starts it only when no other timer runs, so a
    // hit on a landing frame sets none. Whatever sequence was committed was
    // validated from a state the bot is no longer in, so it ends here - a miss
    // that charges no edge - and the boundary looks for a landing that can
    // stop on the graph.
    {
        const playerState_t *ps=&ent->client->ps;
        float dv[3];int knocked=0,teleported;
        VectorSubtract(ps->velocity,m->knock.afterVelocity,dv);
        teleported=m->knock.afterTime && level.time-m->knock.afterTime<=2*Bot2_GameStepMsec() &&
                   ((ps->eFlags^m->knock.afterFlags)&EF_TELEPORT_BIT);
        if(m->knock.afterTime && level.time-m->knock.afterTime<=2*Bot2_GameStepMsec() &&
           !teleported && ps->jumppad_ent==m->knock.afterJumppad &&
           VectorLength(dv)>MOVE_KNOCK_DV)knocked=1;
        m->knock.afterTime=0;
        // A teleport: the committed sequence's origin, line and
        // mark are on the other side. It ends as a knock ends it, a miss that
        // charges no edge, and the route is planned again from here. A respawn
        // toggles the bit too, but Bot2_ClearState and the defender's respawn
        // reset have already cleared the plan and the ride, so nothing is voided.
        if(teleported && ((m->plan.count && m->executor.stage<=Onward(&m->plan)) || m->lift.edge>=0)) {
            Event(client,MOVE_EVENT_EDGE,"teleport","void",va("stage=%d lift=%d",m->executor.stage,m->lift.edge));
            if(m->plan.count && m->executor.stage<=Onward(&m->plan)) {
                if(m->executor.stage<Onward(&m->plan))Event(client,MOVE_EVENT_MISS,StageEdge(m->executor.stage,&m->plan),"teleported",NULL);
                FinishPlan(client);m->plan.count=0;m->executor.stage=0;
            }
            PermitRevoke(client,"teleported");
            if(m->lift.edge>=0)LiftEnd(client,-1,"teleported");
            ParityReset(client);
            m->path.replan=1;m->path.retryTime=0;
        }
        // A body in the flight: logged whatever the cvar, so arms compare.
        if(m->contact.body && level.time-m->contact.body<=2*Bot2_GameStepMsec() && !knocked &&
           m->plan.count && m->executor.stage<Onward(&m->plan)) {
            Event(client,MOVE_EVENT_EDGE,"knock","contact",va("other=%d stage=%d vx=%.1f vy=%.1f vz=%.1f",
                m->contact.bodyClient,m->executor.stage,ps->velocity[0],ps->velocity[1],ps->velocity[2]));
            if(ContactKnock())knocked=1;
        }
        m->contact.body=0;
        if(knocked && KnockRecover()) {
            Event(client,MOVE_EVENT_EDGE,"knock","hit",va("dv=%.1f vx=%.1f vy=%.1f vz=%.1f",VectorLength(dv),ps->velocity[0],ps->velocity[1],ps->velocity[2]));
            if(m->plan.count && m->executor.stage<=Onward(&m->plan)) {
                if(m->executor.stage<Onward(&m->plan))Event(client,MOVE_EVENT_MISS,StageEdge(m->executor.stage,&m->plan),"knocked",NULL);
                // bot_moveFixBX5: the planned edges this hit interrupted.
                if(m->executor.stage<Onward(&m->plan) && m->plan.kind==MOVE_PLAN_STATE) {
                    knockPlan[client]=m->plan.id;knockEdges[client][0]=m->plan.edge+1;knockEdges[client][1]=m->plan.count>1?m->plan.edge2+1:0;
                }
                FinishPlan(client);m->plan.count=0;m->executor.stage=0;
            }
            PermitRevoke(client,"knocked");
            // A ride is a sequence too: it ends with the hit, or the frame
            // after the recovery resumes its stale command.
            if(m->lift.edge>=0)LiftEnd(client,-1,"knocked");
            m->knock.pending=1;m->knock.hitTime=level.time;m->knock.searchTime=level.time;
        }
        if(m->knock.active) {
            const char *end=NULL;
            if(ps->pm_type!=PM_NORMAL)end="movement_type";
            else if(level.time-m->knock.startTime>MOVE_KNOCK_MAX_MS)end="timeout";
            else if(!KnockCommand(ps,&m->knock.policy,level.time,Bot2_GameStepMsec(),&step))end="stood";
            if(end) {
                m->knock.active=0;m->path.replan=1;m->path.retryTime=0;
                Event(client,MOVE_EVENT_EDGE,"knock",end,va("id=%d ms=%d",m->knock.id,level.time-m->knock.startTime));
            } else {
                cmd->forwardmove=step.forwardmove;cmd->rightmove=step.rightmove;cmd->upmove=step.upmove;
                cmd->angles[YAW]=step.angles[YAW];cmd->angles[PITCH]=step.angles[PITCH];cmd->angles[ROLL]=step.angles[ROLL];
                m->executorBefore=m->executor;m->executor.walking=0;m->frameTime=level.time;m->owned=1;
                return qtrue;
            }
        }
    }
    // A ride in progress, or one the bot has just boarded, owns the frame (a
    // hit ended any ride above).
    if(!m->knock.active && LiftRide(client,&step)) {
        cmd->forwardmove=step.forwardmove;cmd->rightmove=step.rightmove;cmd->upmove=step.upmove;
        cmd->angles[YAW]=step.angles[YAW];cmd->angles[PITCH]=step.angles[PITCH];cmd->angles[ROLL]=step.angles[ROLL];
        m->executorBefore=m->executor;m->executor.walking=0;m->frameTime=level.time;m->owned=1;
        return qtrue;
    }
    if(m->budget.speedRequest)SpeedEdge(client,NULL);     // confirm last frame's purchase
    // A strafe chord is a flight, not a walk. A bot past its takeoff with
    // no sequence in progress is walking it: the edge is refused and the
    // path searched again from here, before the walk reaches whatever the
    // flight cleared.
    if(!(m->plan.count && m->executor.stage<=Onward(&m->plan)) && !m->path.replan) {
        int k;float along=ChordAlong(m,&ent->client->ps,&k);
        if(k>=0 && along>MOVE_STATE_TAKEOFF)StateAvoid(client,m->path.edges[k],"walked");
    }
    // A landed plan that cannot cover the onward distance is released here,
    // so the replan gate below is not waiting on a sequence that will never
    // finish. The counter runs whether or not the bound is enabled, so a
    // control run reports how long the stage was held.
    if(m->plan.count && m->executor.stage==Onward(&m->plan) &&
       Bot2_StandableEntity(ent->client->ps.groundEntityNum))m->onwardHeld++;
    else m->onwardHeld=0;
    if(m->plan.count && m->executor.stage==Onward(&m->plan) && m->onwardHeld>Bot2_StepsFor(MOVE_ONWARD_HELD_MS)) {
        Event(client,MOVE_EVENT_EDGE,"onward",OnwardBound()?"bound":"held",
            va("held=%d limit=%d bound=%d speed=%.1f",m->onwardHeld,Bot2_StepsFor(MOVE_ONWARD_HELD_MS),OnwardBound(),Speed2D(&ent->client->ps)));
        if(OnwardBound()) {
            m->executor.stage=Onward(&m->plan)+1;
            m->onwardHeld=0;m->path.replan=1;m->path.retryTime=0;
        } else m->onwardHeld=0;   // report once a second, not once a frame
    }
    PadAhead(client);
    // Plan on grounded frames with no sequence in progress: when there is
    // no path, the bot has left it, or a plan asked for one. With
    // bot_movePlanner 0 the navmesh waypoint steers as the gap-edge step did
    // and the graph only judges landings.
    if(PlannerEnabled() &&
       Bot2_StandableEntity(ent->client->ps.groundEntityNum) && level.time>=m->path.retryTime &&
       !(m->plan.count && m->executor.stage<=Onward(&m->plan)) &&
       (!m->path.count || m->executor.pathIndex<0 || m->path.replan))Replan(client);
    VectorCopy(m->objective,ctx.goal);ctx.client=client;ctx.stepMsec=Bot2_GameStepMsec();ctx.plan=&m->plan;ctx.path=&m->path;ctx.permit=&m->permit;
    {
        // bot_moveFixBM15: frames the executor did not run (a pad arc) end its walk, as knock and lift do.
        int stale=MOVE_FIX(BM15) && m->frameTime && level.time-m->frameTime>2*ctx.stepMsec;
        m->executorBefore=m->executor;m->frameTime=level.time;
        if(stale)m->executor.walking=0;
    }
    walked=MoveStep(&ent->client->ps,&ctx,&m->executor,level.time,&step,&reason);
    m->owned=walked;
    if(m->plan.count && m->executor.stage!=m->executorBefore.stage) {
        // Stage transitions are the edge events; one per boundary crossed.
        int s;
        for(s=m->executorBefore.stage;s<m->executor.stage;s++) {
            // A launch crossing is observed one command frame after the tap
            // was issued: HopStep set launchTime on the tap's own frame and
            // this frame confirms the airborne state. The command boundary
            // is carried explicitly so an analyzer never has to assume the
            // cadence; the event's own level stays the confirmation frame.
            Event(client,MOVE_EVENT_EDGE,StageEdge(s,&m->plan),s==Onward(&m->plan)?"complete":(s&1)?"land":"launch",
                  s==Onward(&m->plan)?ParityCoverage(client):
                  (s&1)?NULL:(m->executorBefore.launchTime?va("launch_level=%d",m->executorBefore.launchTime):NULL));
            ReplayStage(client,s);
            if(MoveGuest(client) && s<Onward(&m->plan))GuestHopPrint(client,s);
            if(s==Onward(&m->plan))PlanCompleted(client);
        }
    }
    if(walked) {
        // Only the movement fields are owned; buttons, force selection and
        // weapon stay with the caller.
        cmd->forwardmove=step.forwardmove;cmd->rightmove=step.rightmove;cmd->upmove=step.upmove;
        cmd->angles[YAW]=step.angles[YAW];cmd->angles[PITCH]=step.angles[PITCH];cmd->angles[ROLL]=step.angles[ROLL];
        SpeedEdge(client,cmd);
        if(!m->executorBefore.walking)Event(client,MOVE_EVENT_WALK,m->plan.count && m->executor.stage<Onward(&m->plan)?StageEdge(m->executor.stage,&m->plan):"walk","start",NULL);
        if(m->executor.braked && !m->executorBefore.braked)Event(client,MOVE_EVENT_WALK,"walk","brake",va("why=%s",reason));
        if(m->executor.held && !m->executorBefore.held) {
            int k=m->executor.pathIndex;
            Event(client,MOVE_EVENT_WALK,"walk","hold",va("index=%d cells=%d next_edge=%d strafe=%d target=%.1f,%.1f,%.1f",
                k,m->path.count,k>=0 && k+1<m->path.count?m->path.edges[k+1]:-2,k>=0 && PathStrafe(&m->path,k+1),
                m->executor.waypoint[0],m->executor.waypoint[1],m->executor.waypoint[2]));
        }
        if(trap->Cvar_VariableIntegerValue("bot_moveWalkTrace") && m->executor.stage>=(m->plan.count?Onward(&m->plan):0)) {
            // Diagnostic, every walked frame: where the walk steers and why.
            int k=m->executor.pathIndex;
            Event(client,MOVE_EVENT_WALK,"walk","trace",va("index=%d cells=%d next_edge=%d target=%.1f,%.1f,%.1f keys=%d,%d braked=%d held=%d permitted=%d vx=%.1f vy=%.1f",
                k,m->path.count,k>=0 && k+1<m->path.count?m->path.edges[k+1]:-2,
                m->executor.waypoint[0],m->executor.waypoint[1],m->executor.waypoint[2],cmd->forwardmove,cmd->rightmove,
                m->executor.braked,m->executor.held,m->executor.permitted,ent->client->ps.velocity[0],ent->client->ps.velocity[1]));
        }
        if(m->executor.permitted && !m->executorBefore.permitted)Event(client,MOVE_EVENT_EDGE,"permit","enter",va("id=%d index=%d",m->permit.id,m->executor.pathIndex));
        else if(!m->executor.permitted && m->executorBefore.permitted)Event(client,MOVE_EVENT_EDGE,"permit","leave",va("id=%d index=%d",m->permit.id,m->executor.pathIndex));
    } else if(m->executor.decline==MOVE_DECLINE_MISS) {
        // A miss ends the sequence; the walk resumes on the next frame.
        Event(client,MOVE_EVENT_MISS,StageEdge(m->executor.stage,&m->plan),reason,NULL);
        if(m->plan.kind==MOVE_PLAN_EDGE || m->plan.kind==MOVE_PLAN_CLIMB) {ChargeFailure(client,m->plan.edge,LiveCause(client,m->plan.edge));m->path.replan=1;}
        else if(m->plan.kind==MOVE_PLAN_STATE) {
            int e=(m->executor.stage>>1)>0?m->plan.edge2:m->plan.edge;
            if(e>=0)StateAvoid(client,e,reason);
        }
        m->plan.count=0;m->executor.stage=0;
    } else if(m->executorBefore.walking || m->executorBefore.decline!=m->executor.decline) {
        char detail[64];
        Com_sprintf(detail,sizeof(detail),"decline=%s offmesh=%d",DeclineName(m->executor.decline),m->executor.offmeshType);
        Event(client,MOVE_EVENT_RECOVER,"none",reason,detail);
    }
    PadObserve(client,walked);
    if(walked)m->declineHold.active=m->declineHold.stepOff=0;
    else if(DeclineHeld(client,reason,&step)) {
        cmd->forwardmove=step.forwardmove;cmd->rightmove=step.rightmove;cmd->upmove=step.upmove;
        cmd->angles[YAW]=step.angles[YAW];cmd->angles[PITCH]=step.angles[PITCH];cmd->angles[ROLL]=step.angles[ROLL];
        return qtrue;
    } else m->declineHold.active=0;
    return walked?qtrue:qfalse;
}

// ---------------------------------------------------------------------------
// Parity harness. At a Pmove boundary the live frame is compared with the
// frame the forecast predicted from an earlier root: the command, the
// executor state after the command, the player state before and after.
// A discrepancy is reported and the horizon rebuilt; nothing is tolerated.
// ---------------------------------------------------------------------------
static struct {
    int client,armed,index,count,completion,builtTime,planned,truncated;
    int mismatches,refusals,rollouts,compared;
    // The comparison count the last end-of-leg tally reported. The periodic
    // `horizon` event prints only on a lottery of the rollout counter, so a
    // world that compared hundreds of frames could report none; the tally is
    // the count a world is read by, and this is what keeps it to one per leg.
    int tallied;
    // Simulated command frames and rollout microseconds accumulated across
    // every candidate rolled out, so a search's cost can be separated from
    // the wall clock the server frame loop imposes on it. Counters only.
    int rolloutFrames;double rolloutUs;
    bot2_moveContext_t context;
    // Every rollout in these sources writes into `frames` (RolloutRequest),
    // so the search's candidates overwrite it many times a frame. The armed
    // prediction has to outlive them - it is compared against live frames
    // for as long as the horizon lasts - so the parity build fills `built`
    // and nothing else touches it. Sharing one buffer was harmless only
    // while the harness never armed.
    // The shared buffer is one slot of the rollout ring below: every rollout
    // that uses it writes the slot it points at, and a candidate rollout
    // moves it to the next slot first (RunCandidate).
    bot2_forecastFrame_t *frames;
    bot2_forecastFrame_t built[MOVE_PARITY_MAX_HORIZON];
    bot2_forecastResult_t result;
} parity;

// The candidate loop's frame buffers (row 5, rollout reuse). Candidates that
// share a first hop sit apart in the loop - the 2-hop and 1-hop candidates of
// one aim and run-up are six rollouts apart - so each candidate rollout takes
// the next slot, and the forecast can replay a new candidate's prefix from
// any earlier slot that still agrees with it (bot2_forecast.c). Everything
// that reads parity.frames reads the rollout it just ran, exactly as before.
#define MOVE_ROLLOUT_RING 12
static bot2_forecastFrame_t rolloutRing[MOVE_ROLLOUT_RING][MOVE_PARITY_MAX_HORIZON];
static int rolloutRingNext;
// Perf counters: the scope each ring slot was last taken in, to count slots reused inside one scope
// (a sibling's prefix evicted before the search finished).
static unsigned int rolloutRingScope[MOVE_ROLLOUT_RING];
static bot2_forecastFrame_t *RolloutRingTake(void) {
    bot2_forecastFrame_t *frames=rolloutRing[rolloutRingNext];
    if(bot2_perfOn) {
        unsigned int serial=Bot2_ForecastScopeSerial();
        if(serial && rolloutRingScope[rolloutRingNext]==serial)bot2_perf.n[BOT2_PERF_RING_WRAP]++;
        rolloutRingScope[rolloutRingNext]=serial;
    }
    rolloutRingNext=(rolloutRingNext+1)%MOVE_ROLLOUT_RING;
    return frames;
}

static bot2_forecastCommandStatus_t ParityCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    const char *reason;
    (void)policy;
    if(!MoveStep(ps,(const bot2_moveContext_t*)context,(bot2_moveExecutor_t*)extra,serverTime,command,&reason))
        return BOT2_FORECAST_POLICY_COMPLETE;
    return BOT2_FORECAST_COMMAND;
}

// The gates the work at the Pmove boundary runs under, and the only ones the
// discovery searches ever needed: a dedicated server and a movement-only client
// or a searching guest (the rewrite itself is unconditional;
// sv_cheats was dropped: bots hop on real servers, and every
// measured world ran under devmap, so cheats were always on there). Formerly these lived inside
// `ParityHorizon` together with `bot_moveParity`, so hop discovery ran only
// while the parity harness was armed - the coupling the refined method
// recorded as "current hop discovery also requires ParityHorizon's
// dedicated/cheats/movement-only/rewrite/parity gates", and the reason a
// bench call without `--parity` silently reported zero commits. The harness
// is now one optional diagnostic that runs under these gates; the searches
// are the other, and they no longer wait for it. Every safety gate is kept.
static int MoveSearchGates(int client) {
    return dedicated.integer &&
           (Bot2_IsMovementOnly(client) || (MoveGuest(client) && Bot2_DefenderHops(client)));
}

static int ParityHorizon(int client) {
    int horizon=trap->Cvar_VariableIntegerValue("bot_moveParity");
    if(horizon<=0 || !MoveSearchGates(client) || MoveGuest(client))return 0;
    return min(horizon,MoveSteps(MOVE_PARITY_HORIZON_MS));   // bot_moveParity counts frames (a diagnostic)
}

static void ParityEvent(int client,const char *reason,const char *detail) {
    Event(client,MOVE_EVENT_PARITY,"walk",reason,detail);
}

// Movement-state comparison. Parity is judged on the fields Pmove owns:
// position, motion, contact, view, animation and the jump resource the
// forecast models, plus the Force fields the phase 1b model owns: the pool,
// the active set, the timed durations and the regeneration and drain
// clocks. Fields ClientThink changes outside Pmove (health and armor
// timers, events, entity flags) are outside the forecast's contract.
// ps.speed is recomputed from basespeed at the start of every Pmove
// (BG_AdjustClientSpeed) and reset by ClientThink between frames, so its
// value at a boundary is residue, not state; basespeed is compared instead.
typedef struct { const char *name; size_t offset, size; } psField_t;
#define PS_FIELD(name) {#name, offsetof(playerState_t,name), sizeof(((playerState_t*)0)->name)}
static const psField_t movementFields[]={
    PS_FIELD(commandTime), PS_FIELD(pm_type), PS_FIELD(pm_flags), PS_FIELD(pm_time),
    PS_FIELD(origin), PS_FIELD(velocity), PS_FIELD(gravity), PS_FIELD(basespeed),
    PS_FIELD(delta_angles), PS_FIELD(groundEntityNum), PS_FIELD(viewangles), PS_FIELD(viewheight),
    PS_FIELD(legsTimer), PS_FIELD(legsAnim), PS_FIELD(torsoTimer), PS_FIELD(torsoAnim),
    PS_FIELD(legsFlip), PS_FIELD(torsoFlip), PS_FIELD(movementDir),
    PS_FIELD(fd.forcePowersActive), PS_FIELD(fd.forceJumpZStart), PS_FIELD(fd.forceJumpCharge),
    PS_FIELD(fd.forceJumpAddTime), PS_FIELD(forceHandExtend), PS_FIELD(saberMove), PS_FIELD(fallingToDeath),
    PS_FIELD(fd.forcePower), PS_FIELD(fd.forcePowerDuration), PS_FIELD(fd.forcePowerRegenDebounceTime),
    PS_FIELD(fd.forcePowerDebounce)
};
#undef PS_FIELD

static const char *MovementStateDiff(const playerState_t *live,const playerState_t *predicted) {
    int i;
    for(i=0;i<(int)ARRAY_LEN(movementFields);i++) {
        const psField_t *f=&movementFields[i];
        if(memcmp((const byte*)live+f->offset,(const byte*)predicted+f->offset,f->size))return f->name;
    }
    return NULL;
}

static int CommandDiff(const usercmd_t *live,const bot2_forecastFrame_t *frame,char *detail,int size) {
    const usercmd_t *p=&frame->command;
    if(live->forwardmove!=p->forwardmove || live->rightmove!=p->rightmove || live->upmove!=p->upmove) {
        Com_sprintf(detail,size,"live=%d,%d,%d predicted=%d,%d,%d",live->forwardmove,live->rightmove,live->upmove,p->forwardmove,p->rightmove,p->upmove);return 1;
    }
    if(live->angles[YAW]!=p->angles[YAW] || live->angles[PITCH]!=p->angles[PITCH] || live->angles[ROLL]!=p->angles[ROLL]) {
        Com_sprintf(detail,size,"live_yaw=%d predicted_yaw=%d",live->angles[YAW],p->angles[YAW]);return 1;
    }
    if(live->serverTime!=frame->output.cmd.serverTime) {
        Com_sprintf(detail,size,"live_time=%d predicted_time=%d",live->serverTime,frame->output.cmd.serverTime);return 1;
    }
    if(live->generic_cmd!=p->generic_cmd) {
        Com_sprintf(detail,size,"live_generic=%d predicted_generic=%d",live->generic_cmd,p->generic_cmd);return 1;
    }
    return 0;
}

static void ParityDisarm(void) {parity.armed=0;parity.index=0;parity.count=0;parity.truncated=0;}
// A search starts a new sequence for its own bot, which ends that bot's
// horizon; a guest's search leaves the runner's armed (MoveGuest).
static void SearchDisarm(const gentity_t *ent) {if(!MoveGuest(ent->s.number))ParityDisarm();}

// How much of the current horizon the live frames have matched so far, for
// the event that closes a committed sequence: the composition gate reads
// whether one horizon covered the whole plan.
static const char *ParityCoverage(int client) {
    static char detail[128];
    Com_sprintf(detail,sizeof(detail),"parity_armed=%d parity_index=%d parity_horizon=%d parity_built=%d mismatches=%d",
        parity.armed && parity.client==client,parity.index,parity.count,parity.builtTime,parity.mismatches);
    return detail;
}

static void ParityReset(int client) {
    if(parity.armed && parity.client==client)ParityDisarm();
}

// The count a world is read by. `parity.compared` is incremented on every
// matched frame and never reset, so it is the world's real total; the
// periodic `horizon` event carries it only when the horizon was exhausted and
// `rollouts%10==0` or the horizon covered a committed sequence, which is a
// lottery on an arbitrary counter - four class-75 control worlds on three
// builds compared frames (their own `parity_index` reached 52) and printed no
// such event at all, and the summary read that as zero comparisons.
//
// Printing on every exhausted horizon is the repair that is not safe: a
// per-frame-path print moved a trip by 2,000 ms once already
// (docs/movement/LESSONS.md). This prints once, at the leg boundary where the
// tape census prints, and only when there is something new to report - so a
// world that genuinely compared nothing still reports nothing and fails the
// gate's non-zero requirement, which is what that requirement is for.
void Move_ParityTally(int client) {
    char detail[192];
    if(client<0 || client>=MAX_CLIENTS || client!=parity.client)return;
    if(parity.compared<=parity.tallied)return;
    parity.tallied=parity.compared;
    Com_sprintf(detail,sizeof(detail),
        "frames=%d mismatches=%d refusals=%d rollouts=%d horizon=%d planned=%d truncated=%d index=%d",
        parity.compared,parity.mismatches,parity.refusals,parity.rollouts,
        parity.count,parity.planned,parity.truncated,parity.index);
    ParityEvent(client,"tally",detail);
}

// The route a rollout steers by, when it is not the bot's current one. The
// reroute pass sets it to the replacement route while it revalidates a
// continuation, so the onward stage and the hold are rolled out against the
// path the commit will install rather than the one the flight leaves. The
// permit goes with it: RerouteInstall revokes the permit, so a rollout that
// models execution after the install must not carry one.
static const bot2_movePath_t *rolloutPathOverride;

// Roll the executor out from this boundary: the submitted command first,
// then the step function from the executor state it left behind.
// The executor rides the forecast as its policy extra; a larger one is refused as a bad argument (bot2_moveExecutor_t.touch was added
// for bot_moveFixBM9), so the size is checked here at compile time.
typedef char moveExecutorFitsPolicyExtra[sizeof(bot2_moveExecutor_t)<=BOT2_FORECAST_MAX_POLICY_EXTRA?1:-1];
static void RolloutRequest(gentity_t *ent,pmove_t *move,bot2_moveContext_t *context,bot2_maneuverState_t *policy,
                           int horizon,bot2_forecastRequest_t *request) {
    movement_t *m=&movement[ent->s.number];
    memset(request,0,sizeof(*request));memset(policy,0,sizeof(*policy));
    VectorCopy(m->objective,context->goal);context->client=ent->s.number;context->stepMsec=Bot2_GameStepMsec();
    context->path=rolloutPathOverride?rolloutPathOverride:&m->path;
    context->permit=rolloutPathOverride?NULL:&m->permit;
    request->entity=ent;request->root=move;request->command=ParityCommand;request->context=context;
    request->policy=*policy;request->policyExtra=&m->executorBefore;request->policyExtraSize=sizeof(bot2_moveExecutor_t);
    request->firstCommand=&move->cmd;request->firstPolicyAfter=policy;request->firstPolicyExtraAfter=&m->executor;
    request->firstTime=level.time;request->stepMsec=context->stepMsec;
    if(!parity.frames)parity.frames=rolloutRing[0];
    request->maxSteps=request->frameCapacity=horizon;request->frames=parity.frames;
    // Phase 1b: Speed expiry, levitation end and regeneration are modelled,
    // so a level-2 flight rolls out and its Force fields are compared.
    request->modelForce=qtrue;
}

// A rollout outside any search's guard (the walk lookahead, the budget's
// prediction, a permit, the parity build) is checked by the forecast hashing
// the whole entity array before and after it, byte by byte - most of what a
// grounded boundary's lone rollout costs (records/2026-09-22-search-speed.md).
// It runs in a guard scope of its own instead:
// the same question - did the world change - answered by an exact compare
// rather than a hash, and a contamination the scope finds at its end is put on
// the result exactly where the hash put it. Nested (a scope already open),
// the rollout runs as before.
static qboolean ScopedForecastRun(const pmove_t *move,const bot2_forecastRequest_t *request,bot2_forecastResult_t *result) {
    bot2_forecastGuard_t guard;qboolean ok;
    if(!Bot2_ForecastGuardBegin(move,&guard))return Bot2_ForecastRun(request,result);
    ok=Bot2_ForecastRun(request,result);
    if(!Bot2_ForecastGuardEnd(&guard) && guard.contaminated)result->worldChanged=qtrue;
    return ok;
}

static void ParityBuild(gentity_t *ent,pmove_t *move,int horizon) {
    movement_t *m=&movement[ent->s.number];
    bot2_forecastRequest_t request;bot2_maneuverState_t policy;char detail[160];int truncated;
    RolloutRequest(ent,move,&parity.context,&policy,horizon,&request);
    // The armed prediction outlives this frame's searches, so it is built in
    // the harness's own buffer rather than the one every rollout shares.
    request.frames=parity.built;
    parity.context.plan=&m->plan;
    parity.rollouts++;
    ScopedForecastRun(move,&request,&parity.result);
    // A rollout that stopped on a trigger contact and on nothing else has
    // still predicted every frame up to it, and those are frames the live
    // bot is about to execute. The walk this harness forecasts is heading
    // for the objective, whose pickup box is a trigger, so refusing the
    // whole build here is what left the harness comparing nothing at all.
    // The contact frame is compared too: the live trigger is touched in
    // G_TouchTriggers, after the Pmove boundary this compares at. Nothing
    // after it is predictable, so the horizon ends there.
    truncated=parity.result.status==BOT2_FORECAST_REFUSED &&
              parity.result.unsupported==BOT2_FORECAST_TRIGGER;
    if((parity.result.status!=BOT2_FORECAST_COMPLETE && !truncated) ||
       (parity.result.unsupported && !truncated) || parity.result.worldChanged || parity.result.animationChanged ||
       !parity.result.restored || !parity.result.animationRestored || parity.result.count<1) {
        parity.refusals++;ParityDisarm();
        Com_sprintf(detail,sizeof(detail),"status=%d unsupported=%u frames=%d restored=%d animation_restored=%d world_changed=%d animation_changed=%d",
            parity.result.status,parity.result.unsupported,parity.result.count,parity.result.restored,
            parity.result.animationRestored,parity.result.worldChanged,parity.result.animationChanged);
        ParityEvent(ent->s.number,"forecast_refused",detail);
        return;
    }
    parity.client=ent->s.number;parity.armed=1;parity.index=0;parity.count=parity.result.count;
    parity.completion=parity.result.completion;parity.builtTime=level.time;
    parity.truncated=truncated;
    parity.planned=m->plan.count && m->executor.stage<=Onward(&m->plan);
}

// ---------------------------------------------------------------------------
// Phase 1 discovery. On a grounded frame the walk owns, propose two-hop
// sequences from the live state, roll each out with the executor's own step
// as the command source, admit those that land twice and travel usefully
// onward, and commit the fastest. Every candidate's outcome is reported so
// the entry envelope is designed from measurements, not assumptions.
// ---------------------------------------------------------------------------
static const float spikeHeadings[]={-15,0,15};
static const float spikeRunups0[]={32,128};
static const float spikeRunups1[]={0,64};
static int spikePlans;

typedef struct {
    int launch[2],land[2],done;             // frame indices, -1 when not reached
    float launchSpeed[2],launchError[2];    // speed and heading error at each launch
    float landSpeed[2],landDrop[2],landError[2];
    float launchDir[2][2],landDir[2][2];    // unit horizontal velocity headings at each launch and landing
    int landCell[2],landFlags[2];           // graph cell under each landing (-1 off the graph) and its hazards
    int frames;unsigned int unsupported;    // the rollout's own frame count and refusal flags
    vec3_t launchPos[2],landPos[2];         // positions at each launch and landing
    float progress;                         // objective distance reduced at DONE
    // Why the onward stage did or did not finish. A candidate is only DONE
    // once the walk after the last landing has covered MOVE_ONWARD_DISTANCE
    // from the landing mark, inside the hold. A walk that brakes or stands
    // there is refused with no decline of its own, so these name it.
    int endStage;                           // executor stage on the last frame
    int brakeFrames,heldFrames;             // frames the walk braked for its momentum chord, or stood at its target
    float onwardMax;                        // furthest the onward walk got from the landing mark
    float onwardTravel,onwardProgress;      // horizontal distance walked and objective-distance reduction
    int onwardFrames;
    const char *outcome;
} spikeMeasure_t;

static float HeadingError(const playerState_t *ps,const float *dir) {
    float speed=Speed2D(ps);
    if(speed<1)return 0;
    return acosf(max(-1.0f,min(1.0f,(ps->velocity[0]*dir[0]+ps->velocity[1]*dir[1])/speed)))*180/M_PI;
}

static float GoalDistance(const playerState_t *ps,const vec3_t goal) {
    return sqrtf((goal[0]-ps->origin[0])*(goal[0]-ps->origin[0])+(goal[1]-ps->origin[1])*(goal[1]-ps->origin[1]));
}

// The unit horizontal velocity heading, or the hop's heading at rest.
static void VelocityDir(const playerState_t *ps,const float *fallback,float *out) {
    float v=Speed2D(ps);
    if(v>=1) {out[0]=ps->velocity[0]/v;out[1]=ps->velocity[1]/v;}
    else {out[0]=fallback[0];out[1]=fallback[1];}
}

// An unmeasured measurement: every field zero and every frame index at its
// "did not happen" sentinel. A measure that is never filled must read as a
// candidate that launched nowhere and landed nowhere, whether it was never
// rolled out or the rollout was refused, so the sentinels live here rather
// than at each of the places that produce one.
static void SpikeMeasureClear(spikeMeasure_t *out) {
    memset(out,0,sizeof(*out));
    out->launch[0]=out->launch[1]=out->land[0]=out->land[1]=out->done=-1;
    out->landCell[0]=out->landCell[1]=-1;out->landFlags[0]=out->landFlags[1]=-1;
}

// Read the stage crossings out of a rollout's frames.
static void SpikeMeasure(const bot2_forecastResult_t *result,const bot2_movePlan_t *plan,const vec3_t goal,
                         const playerState_t *root,spikeMeasure_t *out) {
    int i,h;
    SpikeMeasureClear(out);
    out->frames=result->count;out->unsupported=result->unsupported;
    for(i=0;i<result->count;i++) {
        const bot2_forecastFrame_t *f=&parity.frames[i];
        const bot2_moveExecutor_t *before=(const bot2_moveExecutor_t*)f->beforePolicyExtra;
        const bot2_moveExecutor_t *after=(const bot2_moveExecutor_t*)f->afterPolicyExtra;
        // The launch is the frame whose command carried the tap: the step
        // that produced it recorded the request, and the stage it left is
        // the run-up of the hop launched, even when that step also observed
        // the previous landing. The landing and the crossings are the
        // frames whose step observed them.
        // A tap pressed ahead of its landing (bot_moveLandingJump) is issued
        // from the flight stage; the hop it launches is the next one.
        if(after->launchTime && after->stage<Onward(plan) && (after->prepress || !(after->stage&1)) &&
           ((after->stage+(after->prepress?1:0))>>1)<2) {
            h=(after->stage+(after->prepress?1:0))>>1;
            if(out->launch[h]<0) {
                out->launch[h]=i;out->launchSpeed[h]=Speed2D(&f->beforePS);
                out->launchError[h]=HeadingError(&f->beforePS,plan->hop[h].dir);
                VelocityDir(&f->beforePS,plan->hop[h].dir,out->launchDir[h]);
                VectorCopy(f->beforePS.origin,out->launchPos[h]);
                // bot_moveFixBM9 (default 0): a prepressed tap launches from the touchdown
                // this frame traced, not from the airborne point before it.
                if(MOVE_FIX(BM9) && after->prepress)VectorCopy(after->touch,out->launchPos[h]);
            }
        }
        // A step off has no tap: its launch is the frame that left the ground.
        if(plan->stepOff && before->stage==MOVE_STAGE_RUNUP0 && after->stage>MOVE_STAGE_RUNUP0 && out->launch[0]<0) {
            out->launch[0]=i;out->launchSpeed[0]=Speed2D(&f->beforePS);
            out->launchError[0]=HeadingError(&f->beforePS,plan->hop[0].dir);
            VelocityDir(&f->beforePS,plan->hop[0].dir,out->launchDir[0]);
            VectorCopy(f->beforePS.origin,out->launchPos[0]);
        }
        if(after->stage>before->stage) {
            int s;
            // bot_moveFixBM9 (default 0): the landing crossed with a prepressed relaunch is
            // the touchdown the pressing frame traced (the executor's mark), not this airborne frame's origin.
            const float *landAt=MOVE_FIX(BM9) && before->prepress && after->stage==before->stage+2?after->mark:f->beforePS.origin;
            for(s=before->stage;s<after->stage;s++) {
                if((s&1) && s<Onward(plan)) {
                    h=s>>1;out->land[h]=i;out->landSpeed[h]=Speed2D(&f->beforePS);
                    out->landDrop[h]=(out->launch[h]>=0?(MOVE_FIX(BM9)?out->launchPos[h][2]:parity.frames[out->launch[h]].beforePS.origin[2]):root->origin[2])-landAt[2];
                    out->landError[h]=HeadingError(&f->beforePS,plan->hop[h].dir);
                    VelocityDir(&f->beforePS,plan->hop[h].dir,out->landDir[h]);
                    out->landCell[h]=Graph_CellAt(landAt,STEPSIZE+.25f);
                    out->landFlags[h]=out->landCell[h]>=0?Graph_Cell(out->landCell[h])->flags:-1;
                    VectorCopy(landAt,out->landPos[h]);
                }
                if(s==Onward(plan)) {
                    out->done=i;out->progress=GoalDistance(root,goal)-GoalDistance(&f->beforePS,goal);
                }
            }
        }
        // The onward walk, measured where it runs: from the landing mark
        // the executor set, on the frames that are in the onward stage.
        out->endStage=after->stage;
        if(after->braked)out->brakeFrames++;
        if(after->held)out->heldFrames++;
        if(after->stage>=Onward(plan)) {
            float tx=f->afterPS.origin[0]-f->beforePS.origin[0],ty=f->afterPS.origin[1]-f->beforePS.origin[1];
            out->onwardTravel+=sqrtf(tx*tx+ty*ty);
            out->onwardProgress+=GoalDistance(&f->beforePS,goal)-GoalDistance(&f->afterPS,goal);
            out->onwardFrames++;
        }
        if(before->stage>=Onward(plan)) {
            float dx=f->afterPS.origin[0]-before->mark[0],dy=f->afterPS.origin[1]-before->mark[1];
            float d=sqrtf(dx*dx+dy*dy);
            if(d>out->onwardMax)out->onwardMax=d;
        }
    }
}

static bot2_forecastCommandStatus_t SpikeCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    // Discovery stops at DONE; the live executor keeps walking past it.
    if(((const bot2_moveExecutor_t*)extra)->stage>Onward(((const bot2_moveContext_t*)context)->plan))return BOT2_FORECAST_POLICY_COMPLETE;
    return ParityCommand(ps,policy,extra,serverTime,context,command);
}

// Gap and replayed hop plans need the same post-landing observation as
// straight hops. Reaching the onward distance while falling is not a safe
// exit: the blue respawn-5 regression crossed that mark 80 units below its
// landing and fell into the pit after the old rollout had stopped.
static int hopStopTime;
static bot2_forecastCommandStatus_t HopHoldCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    const bot2_moveContext_t *ctx=(const bot2_moveContext_t*)context;
    if(((const bot2_moveExecutor_t*)extra)->stage>=Onward(ctx->plan)) {
        if(!hopStopTime)hopStopTime=serverTime+MOVE_STRAIGHT_HOLD_MS;
        else if(serverTime>=hopStopTime)return BOT2_FORECAST_POLICY_COMPLETE;
    }
    return ParityCommand(ps,policy,extra,serverTime,context,command);
}

// A rollout falls when a frame is MOVE_FALL_DROP below the highest floor it
// has stood on: the root, or any grounded frame since. Measured from the
// root alone, a chain that climbed the ramp to the blue stand and then
// walked off its side inside the hold read as a 226-unit drop, under the
// bound, and was admitted (Kejim, red outbound level 3 from respawn 5).
//
// A planned drop edge declares its own descent: while its candidates are
// judged the bound is that descent plus a step's margin, never less than
// MOVE_FALL_DROP (ctf4's two base-to-floor drops are 384 units, and every
// candidate that took one read as a fall, so the edge was disabled and the
// lower floor was never reached).
static float rolloutFallDrop=MOVE_FALL_DROP;
static int RolloutFell(int frames,const playerState_t *root) {
    float high=root->origin[2];int i;
    for(i=0;i<frames;i++) {
        const playerState_t *ps=&parity.frames[i].afterPS;
        if(ps->groundEntityNum!=ENTITYNUM_NONE && ps->origin[2]>high)high=ps->origin[2];
        if(high-ps->origin[2]>rolloutFallDrop)return 1;
    }
    return 0;
}

static const char *SpikeJudge(const bot2_forecastResult_t *result,const playerState_t *root,const spikeMeasure_t *m,int hops) {
    int i;
    if(result->status!=BOT2_FORECAST_COMPLETE)return "refused";
    // A landing off the graph's admitted floor is not a landing on a node.
    for(i=0;i<hops;i++)if(m->land[i]>=0 && m->landCell[i]<0)return "off_graph";
    if(RolloutFell(result->count,root))return "fell";
    if(m->done<0) {
        const bot2_moveExecutor_t *terminal=(const bot2_moveExecutor_t*)result->terminalPolicyExtra;
        if(terminal->decline==MOVE_DECLINE_MISS)return "miss";
        if(terminal->decline)return DeclineName(terminal->decline);
        // land[1] is structurally -1 on a one-hop plan, so it cannot name
        // the failure there: such a candidate landed and then failed the
        // onward test, which is its own outcome.
        if(m->land[0]<0)return "no_first_landing";
        if(hops>1 && m->land[1]<0)return "no_second_landing";
        return "no_onward";
    }
    if(m->progress<MOVE_PROGRESS_MIN)return "no_progress";
    // A rollout that ran on past DONE and was then declined: the walk could
    // not continue from the landing.
    if(result->completion==BOT2_FORECAST_COMPLETION_POLICY) {
        const bot2_moveExecutor_t *terminal=(const bot2_moveExecutor_t*)result->terminalPolicyExtra;
        if(terminal->decline==MOVE_DECLINE_MISS)return "miss";
        if(terminal->decline)return DeclineName(terminal->decline);
    }
    return "admitted";
}

static void CommitMeasure(bot2_movePlan_t *plan,const spikeMeasure_t *m) {
    int h;
    VectorCopy(m->launchPos[0],plan->takeoff);VectorCopy(m->landPos[0],plan->landing);
    plan->flightFrames=m->land[0]>=0 && m->launch[0]>=0?m->land[0]-m->launch[0]:0;
    for(h=0;h<2;h++) {
        VectorCopy(m->launchPos[h],plan->measured[h].takeoff);VectorCopy(m->landPos[h],plan->measured[h].landing);
        plan->measured[h].launchSpeed=m->launchSpeed[h];plan->measured[h].landSpeed=m->landSpeed[h];
        plan->measured[h].launchDir[0]=m->launchDir[h][0];plan->measured[h].launchDir[1]=m->launchDir[h][1];
        plan->measured[h].landDir[0]=m->landDir[h][0];plan->measured[h].landDir[1]=m->landDir[h][1];
        plan->measured[h].landed=h<plan->count && m->land[h]>=0 && m->launch[h]>=0;
        plan->measured[h].frames=plan->measured[h].landed?m->land[h]-m->launch[h]:0;
    }
}

// chain_entry, tap_onward and tap_refused are no longer produced (their switches were removed);
// they stay so the counts keep their indices and the no-commit line its fields.
static const char *const candidateOutcomes[]={"refused","fell","miss","no_first_landing","no_second_landing","no_onward","horizon","no_progress","off_graph","off_path","slower","unjudged","arrives","chain_entry","rerouted","no_route","slower_route","tap_onward","tap_refused","lip","teammate"};
#define CANDIDATE_OUTCOMES ((int)ARRAY_LEN(candidateOutcomes))

// bot_moveGuestLip (default on; guests only, so the runner's searches are
// untouched): a guest's hop may not land on a cell beside a lethal drop. The
// strafing chaser's falls on Kejim were admitted sequences whose rollout
// landed on the chasm walkway's lip cells (GRAPH_HAZARD_DROP) at 400-500 u/s
// with no margin; the live flight missed the lip by a few units and went into
// the chasm (records/2026-09-30-defender-falls-and-route.md). A lip is lethal
// when, past one of its refused sides, no floor is within MOVE_LIP_LETHAL.
#define MOVE_LIP_LETHAL 512.0f
static int GuestLipOn(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveGuestLip",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}
static int LethalLip(int client,int cell);

// bot_moveGuestTeammate (default 1, 0 off; guests only, so the runner's searches
// are untouched): a guest's rollout traces against the world as it stands
// right now, so it never sees another guest's planned flight - two
// defenders can each judge their own candidate clear and still fly into
// each other, since neither rollout contains the other's motion
// (records/2026-10-01-errand-falls-library-test.md: 36 of 122 missed
// landings were the two defenders colliding with each other mid-hop, with
// identical commands and no discrepancy charged to either rollout; the
// runner only took 6). With this on, every candidate a guest's search
// judges admitted is also checked against each other guest defender's
// predicted path over the candidate's flight time, and refused if it would
// come within a player box of it at the same frame. The prediction is the
// other guest's last committed rollout (GuestTraceRecord keeps a light copy
// of its positions whenever this is on) while its frames last, held at the
// last one once they run out; with no committed rollout to read, its
// current velocity extrapolated for up to MOVE_TEAMMATE_HOLD_MS and held
// after. The runner is never a guest (MoveGuest), so it never predicts or
// is predicted. The copy covers at most MOVE_TEAMMATE_MS (200 frames at 25 ms).
#define MOVE_TEAMMATE_MS 5000
#define MOVE_TEAMMATE_MAX MOVE_PARITY_MAX_HORIZON
#define MOVE_TEAMMATE_HOLD_MS 500
#define MOVE_TEAMMATE_MARGIN_XY 40.0f
#define MOVE_TEAMMATE_MARGIN_Z (DEFAULT_MAXS_2-(DEFAULT_MINS_2))
typedef struct {int active,start,count,plan;vec3_t pos[MOVE_TEAMMATE_MAX];} guestPredict_t;
static guestPredict_t guestPredict[MAX_CLIENTS];
static int GuestTeammateOn(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveGuestTeammate",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// `other`'s predicted position at `serverTime` (absolute, the same clock as
// level.time), in `out`. 0 only when `other` has no body to avoid (dead or
// gone).
static int TeammatePredict(int other,int serverTime,vec3_t out) {
    const gentity_t *o=&g_entities[other];
    if(!o->inuse || !o->client || o->client->pers.connected!=CON_CONNECTED || o->health<=0)return 0;
    if(guestPredict[other].active && guestPredict[other].count>0 && movement[other].plan.id==guestPredict[other].plan) {
        int idx=(serverTime-guestPredict[other].start)/Bot2_GameStepMsec();
        if(idx<0)idx=0;
        if(idx>=guestPredict[other].count)idx=guestPredict[other].count-1;
        VectorCopy(guestPredict[other].pos[idx],out);
        return 1;
    }
    {
        int elapsed=serverTime-level.time;float t;
        if(elapsed<0)elapsed=0;
        if(elapsed>MOVE_TEAMMATE_HOLD_MS)elapsed=MOVE_TEAMMATE_HOLD_MS;
        t=elapsed*0.001f;
        VectorMA(o->client->ps.origin,t,o->client->ps.velocity,out);
        return 1;
    }
}

// True when the rollout just judged (parity.frames, parity.result.count,
// filled by the RunCandidate call this closes out) would put `self` within
// a player box of another guest defender's predicted position at the same
// frame. Cheap on purpose: it reads frames RunCandidate already rolled out,
// issuing no trace or rollout of its own.
static int TeammateBlocks(int self) {
    int i;
    for(i=0;i<parity.result.count;i++) {
        const bot2_forecastFrame_t *f=&parity.frames[i];
        int other;
        for(other=0;other<level.maxclients;other++) {
            vec3_t pos;
            // Teammates only: the camper is a guest on the other team (milestone B).
            if(other==self || !Bot2_DefenderIs(other) || !MoveGuest(other) ||
               g_entities[other].client->sess.sessionTeam!=g_entities[self].client->sess.sessionTeam)continue;
            if(!TeammatePredict(other,f->serverTime,pos))continue;
            if(fabsf(f->afterPS.origin[0]-pos[0])<MOVE_TEAMMATE_MARGIN_XY &&
               fabsf(f->afterPS.origin[1]-pos[1])<MOVE_TEAMMATE_MARGIN_XY &&
               fabsf(f->afterPS.origin[2]-pos[2])<MOVE_TEAMMATE_MARGIN_Z) {
                return 1;
            }
        }
    }
    return 0;
}

// A guest's hop as flown against the committed rollout, printed at each
// launch and landing stage crossing (guests only; diagnostic). The guest's
// hops are admitted by rollout with no parity check; this pairs each
// predicted takeoff and landing with the live one so a fall can be read from
// the flight that preceded it. A hop with a launch line and no land line
// did not land where the stage machine expected.
static void GuestHopPrint(int client,int stage) {
    const movement_t *m=&movement[client];const playerState_t *ps=&g_entities[client].client->ps;
    int h=stage>>1,land=stage&1,cell,predCell;const bot2_moveMeasured_t *x;const float *pred;float dx,dy;
    if(h<0 || h>=2 || h>=m->plan.count)return;
    x=&m->plan.measured[h];pred=land?x->landing:x->takeoff;
    dx=ps->origin[0]-pred[0];dy=ps->origin[1]-pred[1];
    cell=Graph_CellAt(ps->origin,STEPSIZE+.25f);predCell=Graph_CellAt(pred,STEPSIZE+.25f);
    Bot2_PrintTelemetry(1,"[MOVE] GUEST_HOP client=%d level=%d hop=%d stage=%s count=%d pred=%.1f,%.1f,%.1f live=%.1f,%.1f,%.1f miss=%.1f dz=%.1f "
        "pred_speed=%.1f live_speed=%.1f vz=%.1f pred_frames=%d pred_cell=%d pred_flags=%d pred_lethal=%d live_cell=%d live_flags=%d\n",
        client,level.time,h,land?"land":"launch",m->plan.count,pred[0],pred[1],pred[2],ps->origin[0],ps->origin[1],ps->origin[2],
        sqrtf(dx*dx+dy*dy),ps->origin[2]-pred[2],land?x->landSpeed:x->launchSpeed,Speed2D(ps),ps->velocity[2],x->frames,
        predCell,predCell>=0?Graph_Cell(predCell)->flags:-1,predCell>=0?LethalLip(client,predCell):-1,
        cell,cell>=0?Graph_Cell(cell)->flags:-1);
}

static int LethalLip(int client,int cell) {
    static const int dirX[4]={1,0,-1,0},dirY[4]={0,1,0,-1};
    static vec3_t mins={-15,-15,DEFAULT_MINS_2},maxs={15,15,DEFAULT_MAXS_2};
    const graphCell_t *c=Graph_Cell(cell);int d;
    if(!c || !(c->flags&(GRAPH_HAZARD_DROP|GRAPH_HAZARD_LIQUID|GRAPH_HAZARD_HURT)))return 0;
    if(c->flags&(GRAPH_HAZARD_LIQUID|GRAPH_HAZARD_HURT))return 1;
    for(d=0;d<4;d++) {
        vec3_t top,bottom;trace_t tr;
        if(c->link[d]>=0 || !(c->refused&(1<<d)))continue;
        VectorSet(top,(c->ix+dirX[d])*(float)GRAPH_GRID,(c->iy+dirY[d])*(float)GRAPH_GRID,c->z+STEPSIZE);
        VectorSet(bottom,top[0],top[1],c->z-MOVE_LIP_LETHAL);
        trap->Trace(&tr,top,mins,maxs,bottom,client,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.allsolid)continue;   // a wall, not a drop
        if(tr.fraction>=1)return 1;
    }
    return 0;
}

// A search's bound on the next RunCandidate rollout's steps (0: the full horizon). ClimbSearch keeps only a candidate
// that reaches its floor in strictly fewer frames than the best so far, so it stops later rollouts there.
static int runCandidateSteps;

// Roll one candidate plan out from the boundary root with the supplied
// executor continuation, measure it and judge it. A fresh candidate issues
// this boundary's command itself, from the continuation the frame started
// with, instead of following the command the live frame submitted: a tap
// on this very frame is then validated, and a commit re-issues the frame.
static const char *RunCandidate(gentity_t *ent,pmove_t *move,const bot2_movePlan_t *candidate,
                                const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after,
                                bot2_forecastCommand_t command,int fresh,spikeMeasure_t *measure,float *us,int *traces) {
    movement_t *m=&movement[ent->s.number];bot2_moveContext_t context;bot2_forecastRequest_t request;bot2_maneuverState_t policy;
    parity.frames=RolloutRingTake();
    RolloutRequest(ent,move,&context,&policy,MoveSteps(MOVE_PARITY_HORIZON_MS),&request);
    if(runCandidateSteps>0)request.maxSteps=min(request.maxSteps,runCandidateSteps);
    if(command==SpikeCommand && (candidate->kind==MOVE_PLAN_GAP || candidate->kind==MOVE_PLAN_EDGE)) {
        command=HopHoldCommand;hopStopTime=0;
    }
    context.plan=candidate;request.command=command;request.measureTime=qtrue;
    request.policyExtra=before;request.firstPolicyExtraAfter=after;
    if(fresh) {request.firstCommand=NULL;request.firstPolicyAfter=NULL;request.firstPolicyExtraAfter=NULL;}
    parity.rollouts++;searchRollouts++;
    // Names this rollout for the trigger shadow diagnostic. PrintCandidate
    // prints the same counter, so a disagreement joins to this candidate.
    Bot2_ForecastShadowTag(candidate->id,parity.rollouts);
    Bot2_ForecastRun(&request,&parity.result);
    *us+=parity.result.timing.totalUs;*traces+=parity.result.traceCount;
    parity.rolloutFrames+=parity.result.count;parity.rolloutUs+=parity.result.timing.totalUs;
    Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_ROLLOUT,parity.result.timing.totalUs);
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_ROLLOUTS);
    Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_ROLLOUT_STEPS,parity.result.count);
    SpikeMeasure(&parity.result,candidate,m->objective,&ent->client->ps,measure);
    if(command==HopHoldCommand && parity.result.completion==BOT2_FORECAST_COMPLETION_HORIZON)return "horizon";
    {
        const char *outcome=SpikeJudge(&parity.result,&ent->client->ps,measure,candidate->count);
        if(!strcmp(outcome,"admitted") && MoveGuest(ent->s.number)) {
            if(GuestLipOn()) {
                int h;
                for(h=0;h<candidate->count && h<2;h++)
                    if(measure->land[h]>=0 && LethalLip(ent->s.number,measure->landCell[h]))return "lip";
            }
            if(GuestTeammateOn() && TeammateBlocks(ent->s.number))return "teammate";
        }
        return outcome;
    }
}

// One [SPIKE] line per candidate, whatever discovered it; exploration prints
// its candidates only when asked, since it rolls out tens of thousands.
static int exploreQuiet;
// A counterfactual edge search in progress (SearchFailed): its candidates are
// not printed, and it reports whether it found one instead of committing.
static struct { int active, found; } counterfactual;
static void PrintCandidate(int client,const bot2_movePlan_t *c,int index,const spikeMeasure_t *measure,const char *outcome,const float *ref) {
    if(exploreQuiet || counterfactual.active)return;
    const playerState_t *ps=&g_entities[client].client->ps;float refYaw=atan2f(ref[1],ref[0])*180/M_PI;
    Bot2_PrintTelemetry(1,"[SPIKE] client=%d level=%d seq=%d kind=%s plan=%d cand=%d hops=%d d0=%g r0=%g d1=%g r1=%g outcome=%s frames=%d unsupported=%u "
        "launch0=%d l0_speed=%.1f l0_err=%.1f land0=%d a0_speed=%.1f a0_drop=%.1f a0_err=%.1f a0_cell=%d a0_flags=%d "
        "launch1=%d l1_speed=%.1f l1_err=%.1f land1=%d a1_speed=%.1f a1_drop=%.1f a1_err=%.1f a1_cell=%d a1_flags=%d done=%d progress=%.1f "
        "start_speed=%.1f start_err=%.1f approach=%g step_off=%d approach_at=%g "
        // The search's root state (diagnostic, records/2026-10-06-frame-cost.md): which states and
        // candidates fail consistently. The printing frame's player state is the root, as for start_speed.
        "root=%.0f,%.0f,%.0f root_cell=%d vel_yaw=%.1f ref_yaw=%.1f ground=%d fspeed=%d fspeed_ms=%d force=%d\n",
        client,level.time,parity.rollouts,PlanKind(c->kind),c->id,index,c->count,
        Wrap(atan2f(c->hop[0].dir[1],c->hop[0].dir[0])*180/M_PI-refYaw),c->hop[0].runup,
        c->count>1?Wrap(atan2f(c->hop[1].dir[1],c->hop[1].dir[0])*180/M_PI-refYaw):0,c->count>1?c->hop[1].runup:0,
        outcome,measure->frames,measure->unsupported,
        measure->launch[0],measure->launchSpeed[0],measure->launchError[0],measure->land[0],measure->landSpeed[0],measure->landDrop[0],measure->landError[0],measure->landCell[0],measure->landFlags[0],
        measure->launch[1],measure->launchSpeed[1],measure->launchError[1],measure->land[1],measure->landSpeed[1],measure->landDrop[1],measure->landError[1],measure->landCell[1],measure->landFlags[1],
        measure->done,measure->progress,Speed2D(ps),HeadingError(ps,c->hop[0].dir),c->approach,c->stepOff,c->approachAt,
        ps->origin[0],ps->origin[1],ps->origin[2],Graph_CellAt(ps->origin,STEPSIZE+.25f),
        atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI,refYaw,ps->groundEntityNum!=ENTITYNUM_NONE,
        (ps->fd.forcePowersActive&(1<<FP_SPEED))?1:0,
        (ps->fd.forcePowersActive&(1<<FP_SPEED))?max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time):0,
        ps->fd.forcePower);
}

static void SpikeSearch(gentity_t *ent,pmove_t *move) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    bot2_movePlan_t candidate,best;bot2_forecastGuard_t guard;spikeMeasure_t measure;
    float ref[2],length,us=0;int a,b,c,d,index=0,admitted=0,bestFrames=0,traces=0;char detail[160];
    int counts[CANDIDATE_OUTCOMES];
    memset(counts,0,sizeof(counts));memset(&best,0,sizeof(best));
    m->plan.attempted=1;
    ref[0]=m->executor.waypoint[0]-ps->origin[0];ref[1]=m->executor.waypoint[1]-ps->origin[1];
    length=sqrtf(ref[0]*ref[0]+ref[1]*ref[1]);
    if(length<1) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","reason=no_heading");return;}
    ref[0]/=length;ref[1]/=length;
    // A committed walk horizon is void once a sequence is being chosen; the
    // rollouts below reuse its frames and the commit changes the policy.
    SearchDisarm(ent);
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","reason=guard_busy");return;}
    for(a=0;a<(int)ARRAY_LEN(spikeHeadings);a++)for(b=0;b<(int)ARRAY_LEN(spikeRunups0);b++)
    for(c=0;c<(int)ARRAY_LEN(spikeHeadings);c++)for(d=0;d<(int)ARRAY_LEN(spikeRunups1);d++,index++) {
        float yaw0=atan2f(ref[1],ref[0])*180/M_PI+spikeHeadings[a],yaw1=atan2f(ref[1],ref[0])*180/M_PI+spikeHeadings[c];
        const char *outcome;int r;
        memset(&candidate,0,sizeof(candidate));
        candidate.count=2;candidate.kind=MOVE_PLAN_SPIKE;candidate.edge=-1;candidate.id=spikePlans+1;VectorCopy(ps->origin,candidate.origin);
        candidate.hop[0].dir[0]=cosf(yaw0*M_PI/180);candidate.hop[0].dir[1]=sinf(yaw0*M_PI/180);candidate.hop[0].runup=spikeRunups0[b];
        candidate.hop[1].dir[0]=cosf(yaw1*M_PI/180);candidate.hop[1].dir[1]=sinf(yaw1*M_PI/180);candidate.hop[1].runup=spikeRunups1[d];
        outcome=RunCandidate(ent,move,&candidate,&m->executorBefore,&m->executor,SpikeCommand,0,&measure,&us,&traces);
        for(r=0;r<CANDIDATE_OUTCOMES;r++)if(!strcmp(outcome,candidateOutcomes[r]))counts[r]++;
        PrintCandidate(ent->s.number,&candidate,index,&measure,outcome,ref);
        if(!strcmp(outcome,"admitted")) {
            admitted++;
            if(!best.count || measure.done<bestFrames) {best=candidate;bestFrames=measure.done;}
        }
        if(guard.contaminated)break;
    }
    Bot2_ForecastGuardEnd(&guard);
    if(best.count) {
        FinishPlan(ent->s.number);
        spikePlans++;m->plan=best;m->plan.attempted=1;PermitRevoke(ent->s.number,"commit");
        m->executor.stage=MOVE_STAGE_RUNUP0;m->executor.launchTime=0;m->executor.airborne=0;
        Com_sprintf(detail,sizeof(detail),"kind=spike id=%d candidates=%d admitted=%d frames=%d d0=%g r0=%g d1=%g r1=%g rollout_ms=%.1f traces=%d",
            best.id,index,admitted,bestFrames,
            Wrap(atan2f(best.hop[0].dir[1],best.hop[0].dir[0])*180/M_PI-atan2f(ref[1],ref[0])*180/M_PI),best.hop[0].runup,
            Wrap(atan2f(best.hop[1].dir[1],best.hop[1].dir[0])*180/M_PI-atan2f(ref[1],ref[0])*180/M_PI),best.hop[1].runup,us/1000,traces);
        Event(ent->s.number,MOVE_EVENT_PLAN,"hop0","sequence",detail);
    } else {
        Com_sprintf(detail,sizeof(detail),"kind=spike candidates=%d refused=%d fell=%d miss=%d no_first=%d no_second=%d no_onward=%d horizon=%d no_progress=%d off_graph=%d rollout_ms=%.1f contaminated=%d",
            index,counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8],us/1000,guard.contaminated);
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",detail);
    }
}

// ---------------------------------------------------------------------------
// Phase 2 gap discovery. On a grounded frame the walk owns, the walk is
// rolled out ahead; if it is refused by the corridor inside the window, a
// gap lies ahead and one-hop candidates are rolled out from this root: the
// tap a margin before the lip along headings around the walk's, judged as
// the spike's candidates are and additionally on landing on an admitted
// graph cell. The fastest admitted candidate is committed; the executor
// then validates nothing further, the rollout was from the live state.
// ---------------------------------------------------------------------------
static const float gapHeadings[]={-30,-15,0,15,30};
static const float gapMargins[]={8,40,72};
// Capacity for the lookahead at the 8 ms step of sv_fps 125; a faster tickrate looks less far ahead.
#define MOVE_GAP_LOOKAHEAD_CAPACITY (MOVE_GAP_LOOKAHEAD_MS/8)
static bot2_forecastFrame_t lookaheadFrames[MOVE_GAP_LOOKAHEAD_CAPACITY];
static bot2_forecastResult_t lookaheadResult;

// The walk's own step rolled out from this root into the lookahead frames,
// once per boundary; the searches that read it share the result.
static void WalkLookahead(gentity_t *ent,pmove_t *move) {
    movement_t *m=&movement[ent->s.number];bot2_moveContext_t context;bot2_forecastRequest_t request;bot2_maneuverState_t policy;
    RolloutRequest(ent,move,&context,&policy,min(Bot2_StepsFor(MOVE_GAP_LOOKAHEAD_MS),MOVE_GAP_LOOKAHEAD_CAPACITY),&request);
    context.plan=&m->plan;request.frames=lookaheadFrames;request.frameCapacity=MOVE_GAP_LOOKAHEAD_CAPACITY;request.measureTime=qtrue;
    m->gap.lookaheads++;m->gap.lookaheadTime=level.time;
    ScopedForecastRun(move,&request,&lookaheadResult);
    m->gap.lookaheadUs+=lookaheadResult.timing.totalUs;
}

// Whether the lookahead that ran at this boundary was refused by the
// corridor with a planned strafe chord leaving its progress cell or the
// next: the refusal is along the chord, a flight the walk never takes.
static int LookaheadOnChord(const movement_t *m) {
    const bot2_moveExecutor_t *terminal;
    if(m->gap.lookaheadTime!=level.time || lookaheadResult.status!=BOT2_FORECAST_COMPLETE ||
       lookaheadResult.completion!=BOT2_FORECAST_COMPLETION_POLICY)return 0;
    terminal=(const bot2_moveExecutor_t*)lookaheadResult.terminalPolicyExtra;
    if(terminal->decline!=MOVE_DECLINE_CORRIDOR || terminal->pathIndex<0)return 0;
    return PathStrafe(&m->path,terminal->pathIndex+1) || PathStrafe(&m->path,terminal->pathIndex+2);
}

// The link rollout. The walk is rolled out from this root with a permit
// covering the refused link and the cells after it, so its step crosses
// them on the graph's word; the rollout is admitted only if it walks past
// the permitted range without declining and without dropping below the
// lowest cell it covers. An admitted permit is committed for the live walk.
static int permitIds;

static int PermitSearch(gentity_t *ent,pmove_t *move,int fromIndex,char *why,int whySize) {
    movement_t *m=&movement[ent->s.number];bot2_movePermit_t candidate;bot2_moveContext_t context;
    bot2_forecastRequest_t request;bot2_maneuverState_t policy;const bot2_moveExecutor_t *terminal;
    float lowest=1e30f,drop=0;int i,k;
    if(!m->path.count || fromIndex<0 || fromIndex>=m->path.count-1) {Q_strncpyz(why,"no_link",whySize);return 0;}
    memset(&candidate,0,sizeof(candidate));
    candidate.active=1;candidate.id=permitIds+1;candidate.from=fromIndex;
    candidate.to=min(m->path.count-2,fromIndex+MOVE_PERMIT_CELLS);candidate.pathTime=m->path.time;
    for(k=candidate.from;k<=candidate.to+1;k++)lowest=min(lowest,Graph_Cell(m->path.cells[k])->z);
    SearchDisarm(ent);
    RolloutRequest(ent,move,&context,&policy,MoveSteps(MOVE_PERMIT_HORIZON_MS),&request);
    context.plan=&m->plan;context.permit=&candidate;request.measureTime=qtrue;
    parity.rollouts++;
    ScopedForecastRun(move,&request,&parity.result);
    if(parity.result.status!=BOT2_FORECAST_COMPLETE) {Com_sprintf(why,whySize,"refused unsupported=%u",parity.result.unsupported);return 0;}
    for(i=0;i<parity.result.count;i++)drop=max(drop,lowest-parity.frames[i].afterPS.origin[2]);
    terminal=(const bot2_moveExecutor_t*)parity.result.terminalPolicyExtra;
    if(drop>MOVE_PERMIT_DROP) {Com_sprintf(why,whySize,"fell drop=%.1f frames=%d",drop,parity.result.count);return 0;}
    if(terminal->pathIndex<=candidate.to) {
        Com_sprintf(why,whySize,"%s index=%d to=%d frames=%d",parity.result.completion==BOT2_FORECAST_COMPLETION_POLICY?
            (terminal->decline?DeclineName(terminal->decline):"stopped"):"horizon",terminal->pathIndex,candidate.to,parity.result.count);
        return 0;
    }
    permitIds++;m->permit=candidate;
    Com_sprintf(why,whySize,"admitted id=%d from=%d to=%d frames=%d drop=%.1f rollout_us=%.0f",candidate.id,candidate.from,candidate.to,
        parity.result.count,drop,parity.result.timing.totalUs);
    return 1;
}

static int GapEligible(gentity_t *ent) {
    movement_t *m=&movement[ent->s.number];
    if(!MoveEdgesOn())return 0;
    if(!m->objectiveSet || !m->owned || m->frameTime!=level.time || !m->executor.walking)return 0;
    // Hops still to fly leave nothing to look ahead of; the onward walk
    // from the last landing is a walk, and both level 3 reds (the corner
    // stall under a stale permit, the fall beside the ramp) began in it.
    if(m->plan.count && m->executor.stage<Onward(&m->plan))return 0;
    if(!Bot2_StandableEntity(ent->client->ps.groundEntityNum))return 0;
    if(m->gap.searchTime && level.time<m->gap.searchTime)return 0;
    if(m->gap.lookaheadTime && level.time<m->gap.lookaheadTime+MOVE_GAP_CADENCE)return 0;
    return 1;
}

static void GapSearch(gentity_t *ent,pmove_t *move) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    bot2_moveExecutor_t before,after;const bot2_moveExecutor_t *terminal;
    bot2_movePlan_t candidate,best;bot2_forecastGuard_t guard;spikeMeasure_t measure;
    vec3_t refusal;float ref[2],length,lip,us=0;int a,b,index=0,admitted=0,bestFrames=0,traces=0,frames;
    int counts[CANDIDATE_OUTCOMES];char detail[240];
    // Lookahead: the walk's own step rolled out from this root.
    WalkLookahead(ent,move);
    if(lookaheadResult.status!=BOT2_FORECAST_COMPLETE || lookaheadResult.completion!=BOT2_FORECAST_COMPLETION_POLICY)return;
    terminal=(const bot2_moveExecutor_t*)lookaheadResult.terminalPolicyExtra;
    if(terminal->decline!=MOVE_DECLINE_CORRIDOR)return;
    // A refusal along a planned strafe chord is the flight's, not a gap's:
    // the chord is validated at its takeoff and refused there if it is
    // not flown, so the walk never reaches this refusal. The straight
    // search, which carries that validation, runs this boundary.
    if(LookaheadOnChord(m)) {
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",va("kind=gap reason=chord_planned index=%d",terminal->pathIndex));
        return;
    }
    frames=lookaheadResult.count;
    VectorCopy(lookaheadResult.terminalPS.origin,refusal);
    // The walk's heading at the refusal: toward the waypoint it was refused
    // toward, else its motion.
    ref[0]=terminal->waypoint[0]-refusal[0];ref[1]=terminal->waypoint[1]-refusal[1];
    length=sqrtf(ref[0]*ref[0]+ref[1]*ref[1]);
    if(length<1) {ref[0]=lookaheadResult.terminalPS.velocity[0];ref[1]=lookaheadResult.terminalPS.velocity[1];length=sqrtf(ref[0]*ref[0]+ref[1]*ref[1]);}
    if(length<1)return;
    ref[0]/=length;ref[1]/=length;
    // The lip is about one corridor sample per refused sample beyond the refusal point.
    lip=(refusal[0]-ps->origin[0])*ref[0]+(refusal[1]-ps->origin[1])*ref[1]+terminal->corridorSample*(MOVE_WALK_LOOKAHEAD/4);
    {
        // The graph's verdict first: if the cell at the refusal and the cell
        // at the lip are linked within a few walk links, the corridor refused
        // one chord of walkable floor, not a gap; no hop is searched.
        vec3_t lipPoint;int at,beyond;
        lipPoint[0]=refusal[0]+ref[0]*terminal->corridorSample*(MOVE_WALK_LOOKAHEAD/4);
        lipPoint[1]=refusal[1]+ref[1]*terminal->corridorSample*(MOVE_WALK_LOOKAHEAD/4);lipPoint[2]=refusal[2];
        at=Graph_CellAt(refusal,STEPSIZE+.25f);beyond=Graph_Nearest(lipPoint,GRAPH_GRID,2*STEPSIZE);
        // Only a walk that follows the graph can take the walk around; the
        // navmesh-steered walk would be refused live and hand the frame to
        // the legacy machine, so without the planner the hop is searched.
        if(PlannerEnabled() && at>=0 && beyond>=0 && beyond!=at && Graph_LinkedWithin(at,beyond,4)) {
            // The graph says the walk continues. Prove it with a rollout
            // along the links from this root; an admitted permit lets the
            // live walk cross, a refused one falls through to the hop search.
            char why[160];int admitted=terminal->pathIndex>=0 && PermitSearch(ent,move,terminal->pathIndex,why,sizeof(why));
            Com_sprintf(detail,sizeof(detail),"kind=gap reason=chord frames=%d sample=%d at=%d beyond=%d refusal=%.1f,%.1f,%.1f permit=%s",
                frames,terminal->corridorSample,at,beyond,refusal[0],refusal[1],refusal[2],terminal->pathIndex>=0?why:"off_path");
            Event(ent->s.number,MOVE_EVENT_PLAN,"none",admitted?"permit":"none",detail);
            if(admitted)return;
        }
    }
    VectorCopy(refusal,m->gap.refusal);m->gap.frames=frames;m->gap.searches++;
    Com_sprintf(detail,sizeof(detail),"frames=%d sample=%d lip=%.1f heading=%.1f refusal=%.1f,%.1f,%.1f lookaheads=%d lookahead_ms=%.1f last_us=%.0f guard_us=%.0f copy_us=%.0f pmove_us=%.0f",
        frames,terminal->corridorSample,lip,atan2f(ref[1],ref[0])*180/M_PI,refusal[0],refusal[1],refusal[2],m->gap.lookaheads,m->gap.lookaheadUs/1000,
        lookaheadResult.timing.totalUs,lookaheadResult.timing.preGuardUs+lookaheadResult.timing.postGuardUs,lookaheadResult.timing.worldCopyUs,lookaheadResult.timing.pmoveUs);
    Event(ent->s.number,MOVE_EVENT_PLAN,"none","gap",detail);
    memset(counts,0,sizeof(counts));memset(&best,0,sizeof(best));
    SearchDisarm(ent);
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=gap reason=guard_busy");m->gap.searchTime=level.time+MOVE_GAP_RETRY;return;}
    // A finished plan leaves its stage at done; the candidates start over.
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    for(a=0;a<(int)ARRAY_LEN(gapHeadings);a++)for(b=0;b<(int)ARRAY_LEN(gapMargins);b++,index++) {
        float yaw=atan2f(ref[1],ref[0])*180/M_PI+gapHeadings[a];const char *outcome;int r;
        memset(&candidate,0,sizeof(candidate));
        candidate.count=1;candidate.kind=MOVE_PLAN_GAP;candidate.edge=-1;candidate.id=spikePlans+1;VectorCopy(ps->origin,candidate.origin);
        candidate.hop[0].dir[0]=cosf(yaw*M_PI/180);candidate.hop[0].dir[1]=sinf(yaw*M_PI/180);
        candidate.hop[0].runup=max(0,lip-gapMargins[b]);
        outcome=RunCandidate(ent,move,&candidate,&before,&after,SpikeCommand,0,&measure,&us,&traces);
        for(r=0;r<CANDIDATE_OUTCOMES;r++)if(!strcmp(outcome,candidateOutcomes[r]))counts[r]++;
        PrintCandidate(ent->s.number,&candidate,index,&measure,outcome,ref);
        if(!strcmp(outcome,"admitted")) {
            admitted++;
            if(!best.count || measure.done<bestFrames) {best=candidate;CommitMeasure(&best,&measure);bestFrames=measure.done;}
        }
        if(guard.contaminated)break;
    }
    Bot2_ForecastGuardEnd(&guard);
    if(best.count) {
        FinishPlan(ent->s.number);
        spikePlans++;m->plan=best;m->plan.attempted=1;PermitRevoke(ent->s.number,"commit");
        m->executor.stage=MOVE_STAGE_RUNUP0;m->executor.launchTime=0;m->executor.airborne=0;
        Com_sprintf(detail,sizeof(detail),"kind=gap id=%d candidates=%d admitted=%d frames=%d d0=%g r0=%g lip=%.1f rollout_ms=%.1f traces=%d",
            best.id,index,admitted,bestFrames,
            Wrap(atan2f(best.hop[0].dir[1],best.hop[0].dir[0])*180/M_PI-atan2f(ref[1],ref[0])*180/M_PI),best.hop[0].runup,lip,us/1000,traces);
        Event(ent->s.number,MOVE_EVENT_PLAN,"hop0","sequence",detail);
    } else {
        m->gap.searchTime=level.time+MOVE_GAP_RETRY;
        Com_sprintf(detail,sizeof(detail),"kind=gap candidates=%d refused=%d fell=%d miss=%d no_first=%d no_second=%d no_onward=%d horizon=%d no_progress=%d off_graph=%d rollout_ms=%.1f contaminated=%d",
            index,counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8],us/1000,guard.contaminated);
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",detail);
    }
}

// ---------------------------------------------------------------------------
// Planned edges. When the path's next edge is a hop, drop or jump inside the
// approach distance, candidates are rolled out from the live state around
// the edge's recorded takeoff and heading, judged as gap candidates are,
// and the fastest admitted one is committed: the commit-time validation the
// architecture requires, one search per edge approach. A search that admits
// nothing fails the edge; repeated failures disable it and the path is
// searched again without it. Pads are walked onto; the retained jumppad
// state flies them.
// ---------------------------------------------------------------------------
static const float edgeHeadings[]={-15,0,15};
static const float edgeOffsets[]={0,-16,-32,-48};

// bot_moveDropSlow (u/s; 150 unless set, 0 off): a drop edge whose candidates
// all fail is searched again from this approach speed, tapping at the same
// takeoffs or stepping off the ledge with no tap. Dantooine's upper walkway
// drops onto a narrow floor above a hurt water pit; entered at Force Speed
// every full-speed drop carried past the floor, and the planner tried seven
// sibling drops, about 3.5 s (records/2026-09-24-circle-runup.md).
// A step off's run-up is a miss once it covers MOVE_STEPOFF_REACH past the
// edge's takeoff still grounded.
#define MOVE_STEPOFF_REACH 64
// bot_moveDropBrake (units; 96 unless set, 0 off): the slow pass first offers
// its candidates run in at the usual speed and braked to the approach speed
// only this far before the takeoff, then the ones slowed from the start.
// Slowed from the start, the run-ups were long: half of 199 slow taps on the
// Taloraan, Carratos and Metalorn floors ran over 150 units at 150 u/s, and a
// Carratos stand trip spent 2.15 s on one 342-unit run-up
// (records/2026-09-25-new-maps.md).
static float DropBrake(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveDropBrake",value,sizeof(value));
    return value[0]?max(0.0f,(float)atof(value)):96;
}
static float DropSlow(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveDropSlow",value,sizeof(value));
    return value[0]?max(0.0f,(float)atof(value)):150;   // 150 unless set
}

static int PlannedEligible(gentity_t *ent,int *edgeOut) {
    movement_t *m=&movement[ent->s.number];int k,idx;float dist=0;vec3_t a,b;
    if(!MoveEdgesOn())return 0;
    if(!m->objectiveSet || !m->owned || m->frameTime!=level.time || !m->executor.walking)return 0;
    if(m->plan.count && m->executor.stage<=Onward(&m->plan))return 0;
    if(!Bot2_StandableEntity(ent->client->ps.groundEntityNum))return 0;
    if(!m->path.count || m->executor.pathIndex<0)return 0;
    idx=m->executor.pathIndex;
    CellPoint(m->path.cells[idx],a);
    dist=sqrtf((a[0]-ent->client->ps.origin[0])*(a[0]-ent->client->ps.origin[0])+(a[1]-ent->client->ps.origin[1])*(a[1]-ent->client->ps.origin[1]));
    for(k=idx+1;k<m->path.count && dist<=MOVE_EDGE_APPROACH;k++) {
        if(m->path.edges[k]>=0 && !PathStrafe(&m->path,k)) {
            const graphEdge_t *e=Graph_Edge(m->path.edges[k]);
            if(!e || e->type==GRAPH_EDGE_PAD || e->type==GRAPH_EDGE_LIFT || Graph_EdgeOff(e))return 0;
            if(m->planned.edge==m->path.edges[k] && level.time<m->planned.searchTime)return 0;
            *edgeOut=m->path.edges[k];
            return 1;
        }
        CellPoint(m->path.cells[k],b);
        dist+=sqrtf((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1]));VectorCopy(b,a);
    }
    return 0;
}

// The end of an edge search run as a counterfactual: record whether it found
// a candidate and return before anything is committed or charged.
static int CounterfactualEnd(int found) {
    if(!counterfactual.active)return 0;
    counterfactual.found=found;return 1;
}

// An edge search that admitted nothing. When a rollout was refused by a
// player or a missile, the same search is run again with every other player
// and missile unlinked (and the frame's trace and corridor caches dropped on
// both sides, since the world they saw differs); if it then finds a
// candidate, the failure was the body's. Otherwise a refusal by a moving
// mover charges the mover, and anything else the world. Runs only on a
// failure that saw a transient contact, so an ordinary search pays nothing.
typedef void (*edgeSearch_t)(gentity_t *ent,pmove_t *move,int edge);
static int SearchFailed(gentity_t *ent,pmove_t *move,int edge,edgeSearch_t search) {
    movement_t *m=&movement[ent->s.number];bot2_forecastContacts_t seen=Bot2_ForecastContacts();
    int cause=seen.mover && Graph_FailCause()?GRAPH_CAUSE_MOVER:GRAPH_CAUSE_WORLD;
    if(seen.transient && !counterfactual.active && Graph_FailCause()) {
        int searchTime=m->planned.searchTime;
        Bot2_ForecastTraceCacheFlush();Bot2_GroundCorridorMemoFlush();
        Bot2_TransientHideBegin(ent->s.number);
        counterfactual.active=1;counterfactual.found=0;
        search(ent,move,edge);
        counterfactual.active=0;
        Bot2_TransientHideEnd();
        Bot2_ForecastTraceCacheFlush();Bot2_GroundCorridorMemoFlush();
        m->planned.searchTime=searchTime;
        if(counterfactual.found)cause=GRAPH_CAUSE_TRANSIENT;
    }
    ChargeFailure(ent->s.number,edge,cause);
    if(cause!=GRAPH_CAUSE_TRANSIENT && Graph_EdgeOff(Graph_Edge(edge)))m->path.replan=1;
    return cause;
}

// A climb edge on the path, once the walk has brought the bot within reach of
// its takeoff: every script timing is rolled out from the live state, and the
// one that stands on the edge's floor soonest is committed. The rollout is the
// executor's own (ClimbStep through ParityCommand), and Bot2_Pmove slices the
// climb commands in the rollout as it does live.
#define MOVE_CLIMB_REACH 96.0f
// bot_moveClimbBound (default 1; 0 turns it off): stop each climb rollout at the best frame count so far.
static int ClimbBound(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveClimbBound",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}
static const int climbJumps[]={0,1,2,3,4,6,8,10,12,16,20};
static const int climbRepresses[]={2,3,4,6,8,12};
static bot2_forecastCommandStatus_t ClimbCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    if(((const bot2_moveExecutor_t*)extra)->stage>=Onward(((const bot2_moveContext_t*)context)->plan))return BOT2_FORECAST_POLICY_COMPLETE;
    return ParityCommand(ps,policy,extra,serverTime,context,command);
}
static void ClimbSearch(gentity_t *ent,pmove_t *move,int edge) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;const graphEdge_t *e=Graph_Edge(edge);
    bot2_moveExecutor_t before,after;bot2_movePlan_t candidate,best;bot2_forecastGuard_t guard;spikeMeasure_t measure;
    float us=0,dx=e->takeoff[0]-ps->origin[0],dy=e->takeoff[1]-ps->origin[1];
    int a,b,index=0,admitted=0,refused=0,shortOf=0,bestFrames=0,traces=0;char detail[240];
    if(dx*dx+dy*dy>MOVE_CLIMB_REACH*MOVE_CLIMB_REACH)return;   // the walk is still bringing it in
    m->planned.edge=edge;m->planned.searchTime=level.time+MOVE_GAP_RETRY;
    memset(&best,0,sizeof(best));
    SearchDisarm(ent);Bot2_ForecastContactsReset();
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=climb reason=guard_busy");return;}
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    before.climbFrame=after.climbFrame=0;
    for(a=0;a<(int)ARRAY_LEN(climbJumps);a++)for(b=0;b<(int)ARRAY_LEN(climbRepresses);b++,index++) {
        const playerState_t *end;
        memset(&candidate,0,sizeof(candidate));
        candidate.count=1;candidate.kind=MOVE_PLAN_CLIMB;candidate.edge=edge;candidate.edge2=-1;candidate.id=spikePlans+1;
        VectorCopy(ps->origin,candidate.origin);VectorCopy(e->takeoff,candidate.takeoff);VectorCopy(e->landing,candidate.landing);
        candidate.hop[0].dir[0]=e->dir[0];candidate.hop[0].dir[1]=e->dir[1];candidate.hop[1]=candidate.hop[0];
        candidate.climbJump=climbJumps[a];candidate.climbRepress=climbRepresses[b];
        if(MOVE_FIX(BM8)) {   // bot_moveFixBM8 (default 0): the script in 25 ms commands, at the step
            candidate.climbJump=MoveFrames(climbJumps[a],0);candidate.climbRepress=MoveFrames(climbRepresses[b],0);
        }
        // The climb bound (records/2026-10-04-tail-composition.md): a candidate wins only by reaching in
        // fewer frames than the best, and a policy completion at step c is seen at the start of step c, so a rollout
        // of bestFrames steps decides every candidate that could. One still going there is refused, not short.
        runCandidateSteps=best.count && ClimbBound()?bestFrames:0;
        RunCandidate(ent,move,&candidate,&before,&after,ClimbCommand,0,&measure,&us,&traces);
        runCandidateSteps=0;
        end=&parity.result.terminalPS;
        if(parity.result.status!=BOT2_FORECAST_COMPLETE || parity.result.completion!=BOT2_FORECAST_COMPLETION_POLICY ||
           parity.result.unsupported) refused++;
        else if(!ClimbReached(end,e->landing)) shortOf++;
        else {
            admitted++;
            if(!best.count || parity.result.count<bestFrames) {best=candidate;bestFrames=parity.result.count;}
        }
        if(guard.contaminated)break;
    }
    Bot2_ForecastGuardEnd(&guard);
    if(CounterfactualEnd(best.count>0))return;
    if(best.count) {
        FinishPlan(ent->s.number);
        spikePlans++;m->plan=best;m->plan.attempted=1;PermitRevoke(ent->s.number,"commit");
        m->executor.stage=MOVE_STAGE_RUNUP0;m->executor.launchTime=0;m->executor.airborne=0;m->executor.climbFrame=0;
        Com_sprintf(detail,sizeof(detail),"kind=climb edge=%d id=%d candidates=%d admitted=%d refused=%d short=%d frames=%d jump=%d repress=%d rise=%.1f rollout_ms=%.1f traces=%d",
            edge,best.id,index,admitted,refused,shortOf,bestFrames,best.climbJump,best.climbRepress,e->landing[2]-e->takeoff[2],us/1000,traces);
        Event(ent->s.number,MOVE_EVENT_PLAN,"hop0","sequence",detail);
    } else {
        int cause=SearchFailed(ent,move,edge,ClimbSearch);
        Com_sprintf(detail,sizeof(detail),"kind=climb edge=%d candidates=%d admitted=0 refused=%d short=%d rise=%.1f rollout_ms=%.1f contaminated=%d",
            edge,index,refused,shortOf,e->landing[2]-e->takeoff[2],us/1000,guard.contaminated);
        Q_strcat(detail,sizeof(detail),va(" cause=%s",Graph_CauseName(cause)));
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",detail);
    }
}

// The knock recovery's search: from the live state after a hit, and again
// every MOVE_KNOCK_RESEARCH while the committed one is airborne, each heading
// (eight about the travel, and a coast) is rolled out through the landing and
// the brake. Admitted: the rollout ends stood on the world's floor over a graph
// cell, having touched no trigger and left no simulation contract - so a
// landing in a pit's hurt volume, or one that slides off the ledge it lands
// on, is refused. Of those, the one that stands nearest the route it was
// knocked off wins, then the soonest.
#define MOVE_KNOCK_HORIZON_MS 5000   // the knock rollout's horizon (200 frames at 25 ms)
#define MOVE_KNOCK_RESEARCH 200
static bot2_forecastCommandStatus_t KnockRolloutCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    (void)policy;
    return KnockCommand(ps,(bot2_moveKnockPolicy_t*)extra,serverTime,((const bot2_moveContext_t*)context)->stepMsec,command)?
        BOT2_FORECAST_COMMAND:BOT2_FORECAST_POLICY_COMPLETE;
}
static float KnockRouteDistance(const movement_t *m,const vec3_t at) {
    float best=1e9f;int k;vec3_t c;
    for(k=0;k<m->path.count;k++) {
        float d;CellPoint(m->path.cells[k],c);
        d=(c[0]-at[0])*(c[0]-at[0])+(c[1]-at[1])*(c[1]-at[1])+(c[2]-at[2])*(c[2]-at[2]);
        if(d<best)best=d;
    }
    if(!m->path.count)best=(m->objective[0]-at[0])*(m->objective[0]-at[0])+(m->objective[1]-at[1])*(m->objective[1]-at[1])+
                           (m->objective[2]-at[2])*(m->objective[2]-at[2]);
    return sqrtf(best);
}
static void KnockSearch(gentity_t *ent,pmove_t *move) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    bot2_moveContext_t context;bot2_maneuverState_t policy;bot2_forecastRequest_t request;bot2_forecastGuard_t guard;
    bot2_moveKnockPolicy_t candidate,best,start;usercmd_t step;
    float travel,bestScore=0,bestAt[3]={0,0,0},us=0;int k,admitted=0,refused=0,shortOf=0,bestFrames=0,traces=0,research=m->knock.active;
    unsigned int unsupported=0;
    char detail[320];
    // A hit on the ground is still grounded on its first frame. It waits a
    // moment for the bot to leave the ground; one that never lifts it is the
    // walk's, whose momentum brake already owns it.
    if(ps->groundEntityNum!=ENTITYNUM_NONE && !research) {
        if(level.time-m->knock.hitTime>MOVE_KNOCK_LIFT_MS)m->knock.pending=0;
        return;
    }
    m->knock.pending=0;m->knock.searchTime=level.time+MOVE_KNOCK_RESEARCH;
    travel=Speed2D(ps)>=50?atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI:ps->viewangles[YAW];
    SearchDisarm(ent);
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=knock reason=guard_busy");return;}
    memset(&start,0,sizeof(start));memset(&best,0,sizeof(best));
    start.yaw=m->knock.active?m->knock.policy.yaw:ps->viewangles[YAW];
    for(k=0;k<9;k++) {
        const playerState_t *end;
        candidate=start;candidate.coast=k==8;candidate.heading=Wrap(travel+45*k);
        RolloutRequest(ent,move,&context,&policy,MoveSteps(MOVE_KNOCK_HORIZON_MS),&request);
        parity.frames=RolloutRingTake();request.frames=parity.frames;
        request.command=KnockRolloutCommand;request.policyExtra=&candidate;request.policyExtraSize=sizeof(candidate);
        request.firstCommand=NULL;request.firstPolicyAfter=NULL;request.firstPolicyExtraAfter=NULL;request.measureTime=qtrue;
        parity.rollouts++;searchRollouts++;
        Bot2_ForecastRun(&request,&parity.result);
        us+=parity.result.timing.totalUs;traces+=parity.result.traceCount;
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_ROLLOUT,parity.result.timing.totalUs);
        Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_ROLLOUTS);
        Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_ROLLOUT_STEPS,parity.result.count);
        end=&parity.result.terminalPS;
        if(parity.result.status!=BOT2_FORECAST_COMPLETE || parity.result.completion!=BOT2_FORECAST_COMPLETION_POLICY ||
           parity.result.unsupported) {refused++;unsupported|=parity.result.unsupported;}
        else if(!Bot2_StandableEntity(end->groundEntityNum) || Graph_CellAt(end->origin,STEPSIZE+.25f)<0) shortOf++;
        else {
            float score=KnockRouteDistance(m,end->origin);
            admitted++;
            if(admitted==1 || score<bestScore-1 || (fabsf(score-bestScore)<=1 && parity.result.count<bestFrames)) {
                best=candidate;bestScore=score;bestFrames=parity.result.count;VectorCopy(end->origin,bestAt);
            }
        }
        if(guard.contaminated)break;
    }
    Bot2_ForecastGuardEnd(&guard);
    if(admitted) {
        // Commit, and issue this boundary's command from the committed
        // policy: the rollout's first command, from the same state.
        if(!research) {m->knock.id++;m->knock.startTime=level.time;}
        m->knock.active=1;m->knock.policy=best;
        if(KnockCommand(ps,&m->knock.policy,level.time,Bot2_GameStepMsec(),&step)) {
            move->cmd.forwardmove=ent->client->pers.cmd.forwardmove=step.forwardmove;
            move->cmd.rightmove=ent->client->pers.cmd.rightmove=step.rightmove;
            move->cmd.upmove=ent->client->pers.cmd.upmove=step.upmove;
            move->cmd.angles[YAW]=ent->client->pers.cmd.angles[YAW]=step.angles[YAW];
            move->cmd.angles[PITCH]=ent->client->pers.cmd.angles[PITCH]=step.angles[PITCH];
            move->cmd.angles[ROLL]=ent->client->pers.cmd.angles[ROLL]=step.angles[ROLL];
        }
    } else if(!research) {
        // Nothing stands yet. The flight goes on, and a landing out of reach
        // now can come into it: search again while the bot is airborne.
        m->knock.pending=1;
    }
    Com_sprintf(detail,sizeof(detail),"kind=knock id=%d research=%d candidates=9 admitted=%d refused=%d short=%d unsupported=0x%x heading=%.1f coast=%d "
        "stand=%.1f,%.1f,%.1f route_dist=%.1f frames=%d rollout_ms=%.1f traces=%d contaminated=%d",
        m->knock.id,research,admitted,refused,shortOf,unsupported,admitted?Wrap(best.heading-travel):0,admitted?best.coast:0,
        bestAt[0],bestAt[1],bestAt[2],admitted?bestScore:-1,bestFrames,us/1000,traces,guard.contaminated);
    Event(ent->s.number,MOVE_EVENT_PLAN,admitted?"knock":"none",admitted?"commit":"none",detail);
}

// ---------------------------------------------------------------------------
// bot_moveDropPlan (default on; 0 off): a drop edge is searched from what the
// drop is for - leave the ledge, land on the floor below with nothing touched
// on the way, and carry on the route as soon as possible - instead of from
// one heading at two fixed speeds (records/2026-09-25-drop-plan.md).
//
// - Where: the ledge is sampled along its line on the grid, the takeoff and
//   MOVE_DROP_POINTS cells either side, kept where the graph has the upper
//   floor at the takeoff's height and the lower floor just beyond it.
// - Which way: across each point along the edge's heading, and along the
//   bearing from the bot when that crosses no more than MOVE_DROP_OBLIQUE
//   off it. The run-up steers onto the chosen line (plan lineRunup).
// - How fast: the lower floor ahead of each line is read from the graph to
//   the first missing or hazardous cell; with the drop's fall time for a
//   step off and for a tap, the approach speed lands MOVE_DROP_MARGIN inside
//   that floor. At or above the speed the bot can carry, it runs at that
//   speed; below it, it brakes late (bot_moveDropBrake) or from the start.
// - Which: every admitted rollout is scored by its landing time, plus the
//   route left from where it landed, less the exit-speed credit the straight
//   search uses (bot_moveExitSpeed), all in walk milliseconds. Lowest wins.
// ---------------------------------------------------------------------------
#define MOVE_DROP_POINTS 2          // ledge cells either side of the takeoff
#define MOVE_DROP_OBLIQUE 50.0f     // degrees: a bearing line further off the edge's heading is not offered
#define MOVE_DROP_MARGIN 48.0f      // units: landing target inside the far end of the floor
#define MOVE_DROP_REACH 512.0f      // units: how far ahead the lower floor is read
#define MOVE_DROP_TAP_BACK 24.0f    // units: a tapped drop jumps this far before the ledge
#define MOVE_DROP_MIN_SPEED 100.0f  // u/s: no approach slower than this is offered
static int DropPlan(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveDropPlan",value,sizeof(value));
    return !value[0] || atoi(value)!=0;   // on unless set 0
}
// The lower floor ahead of a crossing line: the distance from the ledge to the
// first floor cell at the landing's height (near) and to the last one before
// the floor ends or turns hazardous (far). The graph keeps a lower floor's
// cells under an upper one, so the far end is also where the upper floor
// resumes past the gap: a flight still above it there lands on that lip, as
// Metalorn's slot drops did at full speed. 0 when no floor is found near the
// ledge.
static int DropFloor(const float *at,const float *dir,float z,float upperZ,float *nearOut,float *farOut) {
    float s,nearS=-1,farS=-1;int gap=0;
    for(s=GRAPH_GRID*.5f;s<=MOVE_DROP_REACH;s+=GRAPH_GRID*.5f) {
        vec3_t p;int cell;
        p[0]=at[0]+dir[0]*s;p[1]=at[1]+dir[1]*s;p[2]=upperZ;
        if(Graph_CellAt(p,STEPSIZE)<0)gap=1;
        else if(gap)break;                  // the far lip
        p[2]=z;
        cell=Graph_CellAt(p,2*STEPSIZE);
        if(cell>=0 && !(Graph_Cell(cell)->flags&(GRAPH_HAZARD_HURT|GRAPH_HAZARD_LIQUID))) {
            if(nearS<0)nearS=s;
            farS=s;
        } else if(nearS>=0)break;
        else if(s>3*GRAPH_GRID)return 0;   // no floor under the flight's start
    }
    if(nearS<0)return 0;
    *nearOut=nearS;*farOut=farS;
    return 1;
}
// The path cell at or after the drop edge nearest a landing, and the walk
// milliseconds from the landing to the objective through it.
static int DropRouteMs(const bot2_movePath_t *path,int from,const vec3_t at) {
    int k,best=-1;float bestD=1e9f;vec3_t c;
    for(k=max(0,from);k<path->count && k<from+48;k++) {
        float d;CellPoint(path->cells[k],c);
        if(fabsf(c[2]-at[2])>2*STEPSIZE)continue;
        d=sqrtf((c[0]-at[0])*(c[0]-at[0])+(c[1]-at[1])*(c[1]-at[1]));
        if(d<bestD) {bestD=d;best=k;}
    }
    if(best<0)return -1;
    return (int)(bestD/250*1000)+PathRemainingMs(path,best);
}
// The drop search in three parts (records/2026-10-04-tail-composition.md), so that it can also run
// deferred (bot_moveDropDefer) from a predicted state over several frames: the candidate list is built from the
// state the bot stands in, in the order the search has always taken them; the list is rolled out and scored in that
// order; and the best is committed, or the failure charged. Run straight through, the three are the search as it was.
#define MOVE_DROP_CANDIDATES 48     // 5 points x 2 lines x 4 launches at most
typedef struct {bot2_movePlan_t plan;int p,l,v;float speed,farS;} dropCandidate_t;
typedef struct {
    int edge,edgeIndex,exitMs,count,cursor,index,admitted,points,lines,traces,contaminated;
    int bestIndex,bestFrames,bestLine,bestPoint,bestV;
    // The best admitted recipes so far, lowest score first (a deferred take tries them in turn from the live state).
    int topCount,topP[3],topL[3],topV[3];float topScore[3];
    float us,bestScore,bestSpeed,bestFar;
    bot2_movePlan_t best;
    int counts[CANDIDATE_OUTCOMES];
    bot2_forecastContacts_t contacts;
    dropCandidate_t list[MOVE_DROP_CANDIDATES];
} dropSearch_t;
static void DropBuild(gentity_t *ent,int edge,dropSearch_t *ds) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;const graphEdge_t *e=Graph_Edge(edge);
    float h,gravity,fallStep,fallTap,carry,brake,edgeYaw,side[2];
    int p,l,v,k;
    memset(ds,0,sizeof(*ds));
    ds->edge=edge;ds->edgeIndex=-1;ds->bestIndex=-1;ds->exitMs=ExitSpeedMs();
    for(k=0;k<m->path.count;k++)if(m->path.edges[k]==edge) {ds->edgeIndex=k;break;}
    h=max(1.0f,e->takeoff[2]-e->landing[2]);gravity=ps->gravity>0?ps->gravity:800;
    fallStep=sqrtf(2*h/gravity);
    fallTap=(JUMP_VELOCITY+sqrtf(JUMP_VELOCITY*JUMP_VELOCITY+2*gravity*h))/gravity;
    carry=max(Wishspeed(ps),Speed2D(ps));
    brake=DropBrake();if(brake<=0)brake=96;
    edgeYaw=atan2f(e->dir[1],e->dir[0])*180/M_PI;
    side[0]=-e->dir[1];side[1]=e->dir[0];
    for(p=-MOVE_DROP_POINTS;p<=MOVE_DROP_POINTS;p++) {
        vec3_t cross,upper;
        cross[0]=e->takeoff[0]+side[0]*GRAPH_GRID*p;cross[1]=e->takeoff[1]+side[1]*GRAPH_GRID*p;cross[2]=e->takeoff[2];
        // The upper floor at the crossing, a little back from the ledge.
        VectorCopy(cross,upper);upper[0]-=e->dir[0]*GRAPH_GRID*.5f;upper[1]-=e->dir[1]*GRAPH_GRID*.5f;
        if(p && Graph_CellAt(upper,STEPSIZE)<0)continue;
        ds->points++;
        for(l=0;l<2;l++) {
            float yaw,dir[2],bx=cross[0]-ps->origin[0],by=cross[1]-ps->origin[1],len,nearS,farS,reach;
            if(!l)yaw=edgeYaw;
            else {
                len=sqrtf(bx*bx+by*by);
                if(len<GRAPH_GRID)continue;
                yaw=atan2f(by,bx)*180/M_PI;
                if(fabsf(Wrap(yaw-edgeYaw))<5 || fabsf(Wrap(yaw-edgeYaw))>MOVE_DROP_OBLIQUE)continue;
            }
            dir[0]=cosf(yaw*M_PI/180);dir[1]=sinf(yaw*M_PI/180);
            reach=bx*dir[0]+by*dir[1];            // run-up to the crossing along the line
            if(reach<=0)continue;
            if(!DropFloor(cross,dir,e->landing[2],e->takeoff[2],&nearS,&farS))continue;
            ds->lines++;
            // Two launches (a step off at the ledge, a tap MOVE_DROP_TAP_BACK
            // before it), each at the speed that lands inside the floor ahead,
            // braked late and from the start when that is below the carry.
            for(v=0;v<4;v++) {
                int tap=v>=2,early=v&1;float target,speed;dropCandidate_t *dc;bot2_movePlan_t *candidate;
                target=max(nearS+GRAPH_GRID*.5f,farS-MOVE_DROP_MARGIN);
                speed=tap?(target+MOVE_DROP_TAP_BACK)/fallTap:target/fallStep;
                if(speed>=carry && early)continue;  // no brake: one candidate at the carry
                speed=max(speed,MOVE_DROP_MIN_SPEED);
                if(ds->count>=MOVE_DROP_CANDIDATES)continue;
                dc=&ds->list[ds->count++];candidate=&dc->plan;
                dc->p=p;dc->l=l;dc->v=v;dc->speed=speed;dc->farS=farS;
                memset(candidate,0,sizeof(*candidate));
                candidate->count=1;candidate->kind=MOVE_PLAN_EDGE;candidate->edge=edge;candidate->id=spikePlans+1;
                candidate->lineRunup=1;candidate->stepOff=!tap;
                // The line's origin: the crossing moved back to the bot's
                // position along the line, so run-up distances read from it.
                candidate->origin[0]=cross[0]-dir[0]*reach;candidate->origin[1]=cross[1]-dir[1]*reach;candidate->origin[2]=ps->origin[2];
                candidate->hop[0].dir[0]=dir[0];candidate->hop[0].dir[1]=dir[1];
                candidate->hop[0].runup=tap?max(0,reach-MOVE_DROP_TAP_BACK):reach+MOVE_STEPOFF_REACH;
                if(speed<carry) {
                    candidate->approach=speed;
                    candidate->approachAt=early?0:max(0,(tap?reach-MOVE_DROP_TAP_BACK:reach)-brake);
                }
            }
        }
    }
}
// Rolls the list out from the cursor, in order, until it is done, the guard is contaminated (the search stops there,
// as it always has) or `limit` rollouts of this frame's budget are spent (0: no limit). 1 when the list is done.
static int DropEvaluate(gentity_t *ent,pmove_t *move,dropSearch_t *ds,const bot2_moveExecutor_t *before,
                        const bot2_moveExecutor_t *after,bot2_forecastGuard_t *guard,int limit) {
    movement_t *m=&movement[ent->s.number];const graphEdge_t *e=Graph_Edge(ds->edge);spikeMeasure_t measure;
    while(ds->cursor<ds->count && !guard->contaminated) {
        dropCandidate_t *dc=&ds->list[ds->cursor];const char *outcome;int r;
        if((limit>0 && searchRollouts>=limit) || SearchStepsSpent())return 0;
        ds->cursor++;
        outcome=RunCandidate(ent,move,&dc->plan,before,after,SpikeCommand,0,&measure,&ds->us,&ds->traces);
        if(!strcmp(outcome,"admitted") && measure.land[0]>=0 &&
           fabsf(measure.landPos[0][2]-e->landing[2])>2*STEPSIZE)outcome="off_path";
        for(r=0;r<CANDIDATE_OUTCOMES;r++)if(!strcmp(outcome,candidateOutcomes[r]))ds->counts[r]++;
        PrintCandidate(ent->s.number,&dc->plan,ds->index,&measure,outcome,e->dir);
        ds->index++;
        if(!strcmp(outcome,"admitted")) {
            int routeMs=DropRouteMs(&m->path,ds->edgeIndex,measure.landPos[0]);
            float score;
            if(routeMs<0)continue;         // landed where the route does not go on
            score=measure.done*Bot2_GameStepMsec()+routeMs-measure.landSpeed[0]*ds->exitMs/250.0f;
            ds->admitted++;
            {
                int t=ds->topCount,u;     // 3 when full: a candidate no better than the third is not kept
                while(t>0 && score<ds->topScore[t-1])t--;
                if(t<3) {
                    for(u=min(ds->topCount,2);u>t;u--) {
                        ds->topScore[u]=ds->topScore[u-1];ds->topP[u]=ds->topP[u-1];ds->topL[u]=ds->topL[u-1];ds->topV[u]=ds->topV[u-1];
                    }
                    ds->topScore[t]=score;ds->topP[t]=dc->p;ds->topL[t]=dc->l;ds->topV[t]=dc->v;
                    if(ds->topCount<3)ds->topCount++;
                }
            }
            if(ds->bestIndex<0 || score<ds->bestScore) {
                ds->best=dc->plan;CommitMeasure(&ds->best,&measure);ds->bestFrames=measure.done;ds->bestScore=score;
                ds->bestSpeed=dc->speed;ds->bestFar=dc->farS;ds->bestLine=dc->l;ds->bestPoint=dc->p;ds->bestV=dc->v;
                ds->bestIndex=ds->cursor-1;
            }
        }
    }
    if(guard->contaminated)ds->contaminated=1;
    return 1;
}
static void DropPlanSearch(gentity_t *ent,pmove_t *move,int edge);
// Commits the best, or charges the failure. A deferred search's failure is charged without the counterfactual
// search: a contact with a player or missile on the way counts as transient, which costs the shared graph nothing.
static void DropFinish(gentity_t *ent,pmove_t *move,dropSearch_t *ds,int deferred) {
    movement_t *m=&movement[ent->s.number];const graphEdge_t *e=Graph_Edge(ds->edge);char detail[360];
    float edgeYaw=atan2f(e->dir[1],e->dir[0])*180/M_PI;
    if(ds->bestIndex>=0) {
        bot2_movePlan_t *best=&ds->best;
        FinishPlan(ent->s.number);
        spikePlans++;m->plan=*best;m->plan.attempted=1;PermitRevoke(ent->s.number,"commit");
        m->executor.stage=MOVE_STAGE_RUNUP0;m->executor.launchTime=0;m->executor.airborne=0;
        Com_sprintf(detail,sizeof(detail),"kind=edge edge=%d type=%d id=%d drop_plan=1 candidates=%d admitted=%d points=%d lines=%d "
            "frames=%d point=%d line=%d d0=%g r0=%g approach=%g step_off=%d approach_at=%g target_speed=%.0f floor_far=%.0f "
            "land_speed=%.1f score=%.0f rollout_ms=%.1f traces=%d",
            ds->edge,e->type,best->id,ds->index,ds->admitted,ds->points,ds->lines,ds->bestFrames,ds->bestPoint,ds->bestLine,
            Wrap(atan2f(best->hop[0].dir[1],best->hop[0].dir[0])*180/M_PI-edgeYaw),best->hop[0].runup,
            best->approach,best->stepOff,best->approachAt,ds->bestSpeed,ds->bestFar,best->measured[0].landSpeed,ds->bestScore,ds->us/1000,ds->traces);
        if(deferred)Q_strcat(detail,sizeof(detail)," deferred=1");
        Event(ent->s.number,MOVE_EVENT_PLAN,"hop0","sequence",detail);
    } else {
        int cause;
        if(!deferred)cause=SearchFailed(ent,move,ds->edge,DropPlanSearch);
        else {
            cause=ds->contacts.mover && Graph_FailCause()?GRAPH_CAUSE_MOVER:GRAPH_CAUSE_WORLD;
            if(ds->contacts.transient && Graph_FailCause())cause=GRAPH_CAUSE_TRANSIENT;
            ChargeFailure(ent->s.number,ds->edge,cause);
            if(cause!=GRAPH_CAUSE_TRANSIENT && Graph_EdgeOff(Graph_Edge(ds->edge)))m->path.replan=1;
        }
        Com_sprintf(detail,sizeof(detail),"kind=edge edge=%d type=%d drop_plan=1 candidates=%d points=%d lines=%d refused=%d fell=%d miss=%d "
            "no_onward=%d no_progress=%d off_graph=%d rollout_ms=%.1f contaminated=%d",
            ds->edge,e->type,ds->index,ds->points,ds->lines,ds->counts[0],ds->counts[1],ds->counts[2],ds->counts[5],ds->counts[7],ds->counts[8],
            ds->us/1000,ds->contaminated);
        Q_strcat(detail,sizeof(detail),va(" cause=%s",Graph_CauseName(cause)));
        if(deferred)Q_strcat(detail,sizeof(detail)," deferred=1");
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",detail);
    }
}
static void DropPlanSearch(gentity_t *ent,pmove_t *move,int edge) {
    movement_t *m=&movement[ent->s.number];const graphEdge_t *e=Graph_Edge(edge);
    bot2_moveExecutor_t before,after;bot2_forecastGuard_t guard;
    dropSearch_t ds;   // on the stack: the failure counterfactual runs this search again inside DropFinish
    m->planned.edge=edge;m->planned.searchTime=level.time+MOVE_GAP_RETRY;
    DropBuild(ent,edge,&ds);
    SearchDisarm(ent);Bot2_ForecastContactsReset();
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=drop reason=guard_busy");return;}
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    rolloutFallDrop=max(MOVE_FALL_DROP,e->takeoff[2]-e->landing[2]+2*STEPSIZE);
    DropEvaluate(ent,move,&ds,&before,&after,&guard,0);
    rolloutFallDrop=MOVE_FALL_DROP;
    Bot2_ForecastGuardEnd(&guard);
    if(CounterfactualEnd(ds.bestIndex>=0))return;
    DropFinish(ent,move,&ds,0);
}

static void PlannedSearch(gentity_t *ent,pmove_t *move,int edge) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;const graphEdge_t *e=Graph_Edge(edge);
    bot2_moveExecutor_t before,after;bot2_movePlan_t candidate,best;bot2_forecastGuard_t guard;spikeMeasure_t measure;
    float ref[2],along,us=0,slow,brake;int a,b,j,pass,index=0,admitted=0,bestFrames=0,traces=0;
    int counts[CANDIDATE_OUTCOMES];char detail[280];
    if(e->type==GRAPH_EDGE_CLIMB) {ClimbSearch(ent,move,edge);return;}
    if(e->type==GRAPH_EDGE_DROP && DropPlan()) {DropPlanSearch(ent,move,edge);return;}
    m->planned.edge=edge;m->planned.searchTime=level.time+MOVE_GAP_RETRY;
    ref[0]=e->dir[0];ref[1]=e->dir[1];
    along=(e->takeoff[0]-ps->origin[0])*ref[0]+(e->takeoff[1]-ps->origin[1])*ref[1];
    memset(counts,0,sizeof(counts));memset(&best,0,sizeof(best));
    SearchDisarm(ent);Bot2_ForecastContactsReset();
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=edge reason=guard_busy");return;}
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    if(e->type==GRAPH_EDGE_DROP)rolloutFallDrop=max(MOVE_FALL_DROP,e->takeoff[2]-e->landing[2]+2*STEPSIZE);
    slow=e->type==GRAPH_EDGE_DROP?DropSlow():0;brake=slow>0?DropBrake():0;
    for(pass=0;pass<2;pass++) {
    // Pass 1, a drop edge under bot_moveDropSlow whose full-speed candidates
    // all failed: the same taps from the approach speed, then a step off
    // along each heading with no tap - braked late (bot_moveDropBrake), then
    // slowed from the start.
    int group=(int)ARRAY_LEN(edgeHeadings)*((int)ARRAY_LEN(edgeOffsets)+1);
    int count=pass?(brake>0?2:1)*group:(int)ARRAY_LEN(edgeHeadings)*(int)ARRAY_LEN(edgeOffsets);
    if(pass && (slow<=0 || admitted || guard.contaminated))break;
    for(j=0;j<count;j++,index++) {
        int k=pass?j%group:j,late=pass && brake>0 && j<group;
        int stepOff=pass && k>=(int)ARRAY_LEN(edgeHeadings)*(int)ARRAY_LEN(edgeOffsets);
        float yaw;const char *outcome;int r;
        a=stepOff?k-(int)ARRAY_LEN(edgeHeadings)*(int)ARRAY_LEN(edgeOffsets):k/(int)ARRAY_LEN(edgeOffsets);b=k%(int)ARRAY_LEN(edgeOffsets);
        yaw=atan2f(ref[1],ref[0])*180/M_PI+edgeHeadings[a];
        memset(&candidate,0,sizeof(candidate));
        candidate.count=1;candidate.kind=MOVE_PLAN_EDGE;candidate.edge=edge;candidate.id=spikePlans+1;VectorCopy(ps->origin,candidate.origin);
        candidate.hop[0].dir[0]=cosf(yaw*M_PI/180);candidate.hop[0].dir[1]=sinf(yaw*M_PI/180);
        candidate.hop[0].runup=stepOff?max(0,along+MOVE_STEPOFF_REACH):max(0,along+edgeOffsets[b]);
        if(pass)candidate.approach=slow;
        candidate.stepOff=stepOff;
        // Braked late: the approach starts this far before the takeoff (a
        // step off's ledge is the edge's takeoff).
        if(late)candidate.approachAt=max(0,(stepOff?along:along+edgeOffsets[b])-brake);
        outcome=RunCandidate(ent,move,&candidate,&before,&after,SpikeCommand,0,&measure,&us,&traces);
        // A drop edge goes down to its landing's floor. A candidate that
        // lands anywhere else did not take the edge, whatever it did: at
        // Force Speed ctf4's base-to-floor drop flew 446 u/s across to
        // another part of the upper level, was admitted, and the edge was
        // then disabled by the failures that followed (blue_out_L2_stand,
        // red_out_L3_stand stalled on it).
        if(!strcmp(outcome,"admitted") && e->type==GRAPH_EDGE_DROP && measure.land[0]>=0 &&
           fabsf(measure.landPos[0][2]-e->landing[2])>2*STEPSIZE)outcome="off_path";
        for(r=0;r<CANDIDATE_OUTCOMES;r++)if(!strcmp(outcome,candidateOutcomes[r]))counts[r]++;
        PrintCandidate(ent->s.number,&candidate,index,&measure,outcome,ref);
        if(!strcmp(outcome,"admitted")) {
            admitted++;
            if(!best.count || measure.done<bestFrames) {best=candidate;CommitMeasure(&best,&measure);bestFrames=measure.done;}
        }
        if(guard.contaminated)break;
    }
    }
    rolloutFallDrop=MOVE_FALL_DROP;
    Bot2_ForecastGuardEnd(&guard);
    if(CounterfactualEnd(best.count>0))return;
    if(best.count) {
        FinishPlan(ent->s.number);
        spikePlans++;m->plan=best;m->plan.attempted=1;PermitRevoke(ent->s.number,"commit");
        m->executor.stage=MOVE_STAGE_RUNUP0;m->executor.launchTime=0;m->executor.airborne=0;
        Com_sprintf(detail,sizeof(detail),"kind=edge edge=%d type=%d id=%d candidates=%d admitted=%d frames=%d d0=%g r0=%g along=%.1f approach=%g step_off=%d approach_at=%g rollout_ms=%.1f traces=%d",
            edge,e->type,best.id,index,admitted,bestFrames,
            Wrap(atan2f(best.hop[0].dir[1],best.hop[0].dir[0])*180/M_PI-atan2f(ref[1],ref[0])*180/M_PI),best.hop[0].runup,along,
            best.approach,best.stepOff,best.approachAt,us/1000,traces);
        Event(ent->s.number,MOVE_EVENT_PLAN,"hop0","sequence",detail);
    } else {
        int cause=SearchFailed(ent,move,edge,PlannedSearch);
        Com_sprintf(detail,sizeof(detail),"kind=edge edge=%d type=%d candidates=%d refused=%d fell=%d miss=%d no_first=%d no_second=%d no_onward=%d horizon=%d no_progress=%d off_graph=%d rollout_ms=%.1f contaminated=%d",
            edge,e->type,index,counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8],us/1000,guard.contaminated);
        Q_strcat(detail,sizeof(detail),va(" cause=%s",Graph_CauseName(cause)));
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",detail);
    }
}

// ---------------------------------------------------------------------------
// Phase 2 straight hops. Momentum hops on continuous floor are the main
// source of speed on a straight. On a grounded frame the walk owns, at a
// landing at once and otherwise on the lookahead cadence, when the planned
// path ahead is straight and the walk's own lookahead meets no refusal,
// one-hop candidates along the chord are rolled out from this root and
// judged as gap candidates are, plus two conditions: the landing must be
// beside the path, and the rollout must be ahead of the walk's by a margin
// at the same frame. Candidates are two hops along the chord, the second
// tapped on the first landing's frame (a jump accepted on the landing frame
// pays no ground friction), and one hop where two do not fit. The admitted
// candidate furthest ahead of the walk is committed; a plan whose hops have
// landed is completed by the commit, so a chain is a sequence of short
// plans, each validated from its own landing.
// ---------------------------------------------------------------------------
// Each hop is aimed at the point on the path one predicted hop-length
// ahead, at these fractions of the prediction, so a hop from a fast
// landing follows the path around a bend instead of overshooting the
// straight's far cell.
static const float straightAims[]={.85f,1.0f,1.15f};
static const float straightRunups[]={0,24};
static const int straightHops[]={2,1};

// The point at an arc length along the path from a cell, interpolated
// between cells; the path's end, or the cell before an edge, when the
// length runs past them.
static void PathPointAt(const bot2_movePath_t *path,int index,float distance,vec3_t out) {
    vec3_t a,b;int k;float walked=0,length;
    CellPoint(path->cells[index],a);VectorCopy(a,out);
    for(k=index+1;k<path->count;k++) {
        if(path->edges[k]>=0 && !PathStrafe(path,k))return;
        CellPoint(path->cells[k],b);
        length=sqrtf((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1]));
        if(walked+length>=distance) {
            float f=length>0?(distance-walked)/length:0;
            out[0]=a[0]+(b[0]-a[0])*f;out[1]=a[1]+(b[1]-a[1])*f;out[2]=a[2]+(b[2]-a[2])*f;
            return;
        }
        walked+=length;VectorCopy(b,a);VectorCopy(b,out);
    }
}

// The walk rolled out from this root to the candidates' own horizon, so a
// candidate is compared with the walk at its own done frame, never with a
// walk frozen at a shorter window.
static bot2_forecastFrame_t straightWalkFrames[MOVE_PARITY_MAX_HORIZON];
static bot2_forecastResult_t straightWalkResult;
// The replay arm's own reference, in its own buffers. A candidate that
// completes past the control's furthest done frame cannot be judged by the
// control's walk, and lengthening that walk would let an observe-only
// candidate change the control's verdict. So arm B gets a second walk from
// the same root, rolled after the control's and never read by it.
static bot2_forecastFrame_t replayWalkFrames[MOVE_PARITY_MAX_HORIZON];
static bot2_forecastResult_t replayWalkResult;

static void StraightWalkInto(gentity_t *ent,pmove_t *move,int steps,
                             bot2_forecastFrame_t *frames,bot2_forecastResult_t *out) {
    movement_t *m=&movement[ent->s.number];bot2_moveContext_t context;bot2_forecastRequest_t request;bot2_maneuverState_t policy;
    RolloutRequest(ent,move,&context,&policy,MoveSteps(MOVE_PARITY_HORIZON_MS),&request);
    context.plan=&m->plan;request.frames=frames;request.frameCapacity=MOVE_PARITY_MAX_HORIZON;request.measureTime=qtrue;
    request.maxSteps=max(1,min(steps,MoveSteps(MOVE_PARITY_HORIZON_MS)));
    Bot2_ForecastRun(&request,out);
}

static void StraightWalk(gentity_t *ent,pmove_t *move,int steps) {
    StraightWalkInto(ent,move,steps,straightWalkFrames,&straightWalkResult);
}

// A straight candidate rolls on past its landing for the hold, so the
// judge sees the walk that follows: a decline or a fall there refuses it.
// A landing off the path ends the rollout at once: nothing after it can
// admit the candidate, so the frames are not spent.
// straightPathFree suspends the landing's path test for a single rollout:
// the reroute pass continues an already rejected candidate through the
// same physical, onward and hold checks to see what it does, and judges
// where it lands by the route that landing opens instead.
static int straightStopTime,straightRootIndex,straightLandings,straightOffPath,straightPathFree;
// Result slots a search handed to the judge still carrying a previous
// search's fields. It is a defect count, not a policy: the judge empties
// each one it finds, and a build whose constructions all reset reports zero
// for the life of the map.
static int straightStaleSlots;
static int NearPathCell(const bot2_movePath_t *path,int from,int to,const vec3_t pos);

static bot2_forecastCommandStatus_t StraightCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    const bot2_moveContext_t *ctx=(const bot2_moveContext_t*)context;
    const bot2_moveExecutor_t *x=(const bot2_moveExecutor_t*)extra;
    // stage>>1 is the landings completed, in a run-up stage or, when a tap
    // was pressed ahead of its landing, in the next flight.
    if(x->stage>0 && x->stage<=Onward(ctx->plan) && (x->stage>>1)>straightLandings) {
        straightLandings=x->stage>>1;
        if(!straightPathFree && !NearPathCell(ctx->path,straightRootIndex,straightRootIndex+48,x->mark)) {
            if(trap->Cvar_VariableIntegerValue("bot_moveOffPathLog")) {
                // Diagnostic: the nearest cell of the whole path to the landing.
                int k,bestK=-1;float best=1e9f,bestDz=0;
                for(k=0;k<ctx->path->count;k++) {
                    const graphCell_t *c=Graph_Cell(ctx->path->cells[k]);float dx,dy,d;
                    if(!c)continue;
                    dx=c->ix*(float)GRAPH_GRID-x->mark[0];dy=c->iy*(float)GRAPH_GRID-x->mark[1];d=sqrtf(dx*dx+dy*dy);
                    if(d<best){best=d;bestK=k;bestDz=c->z-x->mark[2];}
                }
                Com_Printf("[OFFPATH] root=%d count=%d land=%.0f,%.0f,%.0f nearest=%d dist=%.0f dz=%.0f strafe=%d\n",
                    straightRootIndex,ctx->path->count,x->mark[0],x->mark[1],x->mark[2],bestK,best,bestDz,bestK>0?PathStrafe(ctx->path,bestK):0);
            }
            straightOffPath=1;return BOT2_FORECAST_POLICY_COMPLETE;
        }
    }
    if(x->stage>=Onward(ctx->plan)) {
        if(!straightStopTime)straightStopTime=serverTime+MOVE_STRAIGHT_HOLD_MS;
        else if(serverTime>=straightStopTime)return BOT2_FORECAST_POLICY_COMPLETE;
    }
    return ParityCommand(ps,policy,extra,serverTime,context,command);
}

// Arrival by the game's own test: the rollout's last frame, or one of the
// next MOVE_ARRIVE_MS of its flight, brings the player's hull into
// contact with the objective's entity as G_TouchTriggers tests it. The
// forecast refuses at its own wider item box, which is conservative for a
// refusal and not for an arrival: judged by that box, a level-2 flight
// landed 68 units short of the flag, tapped on the landing frame and flew
// over the flag's box, three times in one case.
// An item - the flag - is not picked up by hull contact: G_TouchTriggers
// gathers entities in a 40x40x52 box about the origin and takes an item when
// BG_PlayerTouchesItem's region (-50..44, +-36, +-36 about the item) holds
// the origin at the end of the command. Judged by the hull, every hop that
// passed 30 units beside the blue flag was refused and the bot walked the
// last 700 units in. The rollout's last frame is the forecast's own Pmove at
// the command end the game tests, so it is judged by the game's rule exactly;
// the projected frames after it are straight-line estimates and keep the
// margin inside both boxes. With the margin on the last frame too, the
// class-150 tape's final hop - picked up 28.8 units short and 26.9 above the
// flag - was refused and the trip took 150 ms longer.
static int ItemPickupAt(const vec3_t origin,const gentity_t *goal,float margin) {
    static const vec3_t range={40,40,52},low={-50,-36,-36},high={44,36,36};
    vec3_t item;int j;
    BG_EvaluateTrajectory(&goal->s.pos,level.time,item);
    for(j=0;j<3;j++) {
        float d=origin[j]-item[j];
        if(d<low[j]+margin || d>high[j]-margin)return 0;
        if(origin[j]+range[j]-margin<goal->r.absmin[j] ||
           origin[j]-range[j]+margin>goal->r.absmax[j])return 0;
    }
    return 1;
}

static int ArrivesAt(int client,const playerState_t *last,int stepMsec) {
    int target=bot2_states[client].targetEntNum,k,frames;gentity_t *goal;vec3_t origin,mins,maxs;float dt=stepMsec*.001f;
    if(target<0 || target>=ENTITYNUM_MAX_NORMAL)return 0;
    goal=&g_entities[target];
    if(!goal->inuse || !(goal->r.contents&CONTENTS_TRIGGER))return 0;
    frames=max(1,(MOVE_ARRIVE_MS+stepMsec/2)/max(1,stepMsec));   // as Bot2_StepsFor, at the rollout's own step
    for(k=0;k<=frames;k++) {
        float t=k*dt;
        origin[0]=last->origin[0]+last->velocity[0]*t;origin[1]=last->origin[1]+last->velocity[1]*t;
        origin[2]=last->origin[2]+last->velocity[2]*t-.5f*last->gravity*t*t;
        if(goal->s.eType==ET_ITEM) {
            if(ItemPickupAt(origin,goal,k?MOVE_ARRIVE_MARGIN:0.0f))return 1;
            continue;
        }
        VectorAdd(origin,g_entities[client].r.mins,mins);VectorAdd(origin,g_entities[client].r.maxs,maxs);
        // A graze is not an arrival: the hull must overlap the goal by
        // MOVE_ARRIVE_MARGIN on every side it touches. Projected past the
        // rollout's end, a 1-unit contact at 677 u/s passed over the blue
        // flag in the live flight and stranded the bot on the ledge behind.
        mins[0]+=MOVE_ARRIVE_MARGIN;mins[1]+=MOVE_ARRIVE_MARGIN;mins[2]+=MOVE_ARRIVE_MARGIN;
        maxs[0]-=MOVE_ARRIVE_MARGIN;maxs[1]-=MOVE_ARRIVE_MARGIN;maxs[2]-=MOVE_ARRIVE_MARGIN;
        if(trap->EntityContact(mins,maxs,(sharedEntity_t*)goal,qfalse))return 1;
    }
    return 0;
}

// A plan whose hops have landed is complete when a new plan is committed
// from its landing: the onward travel is the next run-up.
static void FinishPlan(int client) {
    movement_t *m=&movement[client];
    if(m->plan.count && m->executor.stage==Onward(&m->plan)) {
        Event(client,MOVE_EVENT_EDGE,"onward","complete",ParityCoverage(client));
        PlanCompleted(client);
        m->executor.stage=Onward(&m->plan)+1;
    }
}

// Whether a point is within reach of a path cell in an index range, or of
// a strafe chord between two of them.
static int NearPathCell(const bot2_movePath_t *path,int from,int to,const vec3_t pos) {
    int k;
    for(k=max(0,from);k<path->count && k<=to;k++) {
        const graphCell_t *c=Graph_Cell(path->cells[k]);float dx,dy;
        if(!c)continue;
        if(k>0 && PathStrafe(path,k)) {
            vec3_t a,b;
            CellPoint(path->cells[k-1],a);CellPoint(path->cells[k],b);
            if(ChordReach(a,b,pos,MOVE_PATH_REACH))return 1;
        }
        if(fabsf(c->z-pos[2])>2*STEPSIZE)continue;
        dx=c->ix*(float)GRAPH_GRID-pos[0];dy=c->iy*(float)GRAPH_GRID-pos[1];
        if(dx*dx+dy*dy<=MOVE_PATH_REACH*MOVE_PATH_REACH)return 1;
    }
    return 0;
}

// The straight ahead on the path: the furthest cell within the window, with
// no edge before it, whose intermediate cells all lie within the lateral
// tolerance of the chord from the progress cell. Returns its index or -1.
static int StraightAhead(const bot2_movePath_t *path,int index) {
    vec3_t a,b,c;int k,j,best=-1;
    if(index<0 || index>=path->count)return -1;
    CellPoint(path->cells[index],a);
    for(k=index+1;k<path->count && k<=index+MOVE_STRAIGHT_CELLS;k++) {
        float dx,dy,len;int ok=1;
        if(path->edges[k]>=0 && !PathStrafe(path,k))break;
        CellPoint(path->cells[k],b);
        dx=b[0]-a[0];dy=b[1]-a[1];len=sqrtf(dx*dx+dy*dy);
        if(len<1)continue;
        for(j=index+1;j<k && ok;j++) {
            CellPoint(path->cells[j],c);
            if(fabsf(((c[0]-a[0])*dy-(c[1]-a[1])*dx)/len)>MOVE_STRAIGHT_LATERAL)ok=0;
        }
        if(!ok)break;
        best=k;
    }
    return best;
}

// A straight commit re-issues this boundary's command from the committed
// plan, so the frame the live executor already walked becomes the plan's
// first frame, as the candidate's rollout had it: on a landing frame that
// is a tap accepted before friction. Only the movement fields are owned,
// as in Move_Command; both the Pmove's copy and the retained command are
// written so the frame reads as one command everywhere.
static void Reissue(gentity_t *ent,pmove_t *move,const bot2_moveExecutor_t *before) {
    movement_t *m=&movement[ent->s.number];bot2_moveContext_t ctx;usercmd_t step;const char *reason;
    VectorCopy(m->objective,ctx.goal);ctx.client=ent->s.number;ctx.stepMsec=Bot2_GameStepMsec();ctx.plan=&m->plan;ctx.path=&m->path;ctx.permit=&m->permit;
    m->executorBefore=*before;m->executor=*before;
    if(!MoveStep(&ent->client->ps,&ctx,&m->executor,level.time,&step,&reason))return;
    move->cmd.forwardmove=ent->client->pers.cmd.forwardmove=step.forwardmove;
    move->cmd.rightmove=ent->client->pers.cmd.rightmove=step.rightmove;
    move->cmd.upmove=ent->client->pers.cmd.upmove=step.upmove;
    move->cmd.angles[YAW]=ent->client->pers.cmd.angles[YAW]=step.angles[YAW];
    move->cmd.angles[PITCH]=ent->client->pers.cmd.angles[PITCH]=step.angles[PITCH];
    move->cmd.angles[ROLL]=ent->client->pers.cmd.angles[ROLL]=step.angles[ROLL];
}

static int StraightLanding(const movement_t *m) {
    return m->plan.count && m->executor.stage==Onward(&m->plan) && m->executorBefore.stage<Onward(&m->plan);
}

// The frame a committed sequence's last flight touched down on, as the
// executor left it this frame. This is the one frame where the next tap pays
// no ground friction, so a landing that reaches no search is the whole
// question; every branch that can take such a frame names itself below, and
// coverage is then countable from the events instead of inferred from the
// absence of a line. Requires the executor to have run this frame, because
// the stage pair is stale otherwise.
static int LandedThisFrame(const movement_t *m) {
    return m->frameTime==level.time && StraightLanding(m);
}

static int PlannedStrafe(const movement_t *m,const playerState_t *ps,int *kOut,int *k2Out);

// Why a frame did not search, when the frame was a landing. A landing that
// does not search keeps no momentum: the next tap is paid for out of the
// ground run, so the condition that declined one is worth naming.
static void ReplaySkip(gentity_t *ent,const char *why) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    if(!(ReplayFlags()&1))return;
    Bot2_PrintTelemetry(1,"[REPLAY] kind=skip client=%d level=%d gen=%d why=%s plan=%d plan_kind=%s plan_count=%d "
        "stage=%d before_stage=%d onward=%d walking=%d owned=%d frame_time=%d ground=%d permitted=%d "
        "path_cells=%d path_index=%d replan=%d retry_ms=%d search_ms=%d cadence_ms=%d speed=%.3f pos=%.3f,%.3f,%.3f\n",
        ent->s.number,level.time,m->path.generation,why,m->plan.id,PlanKind(m->plan.kind),m->plan.count,
        m->executor.stage,m->executorBefore.stage,Onward(&m->plan),m->executor.walking,m->owned,m->frameTime,
        ps->groundEntityNum,m->executor.permitted,m->path.count,m->executor.pathIndex,m->path.replan,
        max(0,m->path.retryTime-level.time),max(0,m->straight.searchTime-level.time),
        m->straight.time?max(0,m->straight.time+MOVE_GAP_CADENCE-level.time):0,
        Speed2D(ps),ps->origin[0],ps->origin[1],ps->origin[2]);
}

// A frame that has just completed the last flight of a committed sequence.
// Read before the gates below, so a gate that declines such a frame can be
// reported; the gates themselves are unchanged and in their original order.
static int StraightEligible(gentity_t *ent) {
    movement_t *m=&movement[ent->s.number];int landing=StraightLanding(m);
    if(!PlannerEnabled() || !StraightEnabled()) {
        if(landing)ReplaySkip(ent,!PlannerEnabled()?"planner_off":"straight_off");
        return 0;
    }
    if(!m->objectiveSet || !m->owned || m->frameTime!=level.time || !m->executor.walking) {
        if(landing)ReplaySkip(ent,!m->objectiveSet?"no_objective":!m->owned?"not_owned":
            m->frameTime!=level.time?"frame_not_owned":"not_walking");
        return 0;
    }
    if(m->plan.count && m->executor.stage<Onward(&m->plan)) {   // hops still to fly
        if(landing)ReplaySkip(ent,"hops_to_fly");
        return 0;
    }
    if(!Bot2_StandableEntity(ent->client->ps.groundEntityNum)) {
        if(landing)ReplaySkip(ent,"not_grounded");
        return 0;
    }
    if(!m->path.count || m->executor.pathIndex<0 || m->executor.permitted) {
        if(landing)ReplaySkip(ent,!m->path.count?"no_path":m->executor.pathIndex<0?"off_path_index":"permitted");
        return 0;
    }
    // A landing searches at once, and so does the first frame inside a
    // planned strafe edge's takeoff window; any other frame on the
    // lookahead cadence, and not inside a failed search's retry.
    if(!landing) {
        int k,k2,edge=PlannedStrafe(m,&ent->client->ps,&k,&k2);
        if(edge<0 || m->state.edge==edge) {
            if(m->straight.searchTime && level.time<m->straight.searchTime)return 0;
            if(m->straight.time && level.time<m->straight.time+MOVE_GAP_CADENCE)return 0;
        }
    }
    return 1;
}

typedef struct {
    bot2_movePlan_t candidate; spikeMeasure_t measure; const char *outcome;
    float margin,walk; int walkFrame,walkStopped,walkRefFrames;
    // The reroute pass: whether this candidate was continued without the
    // path stop, the cells of the route its landing opened (0 when it
    // arrives and needs none), and the time to the objective its rollout
    // and that route cost together, by the path model's own walk speed.
    int rerouted,routeCells,routeMs;
    // 0: the control arm, which alone can be committed. 1: the
    // observe-only measured-policy arm (bot_moveReplay bit 2).
    int variant;
    float score;                            // the margin plus the exit credit: what the ranking compares
} straightResult_t;

// A result slot, emptied for the candidate about to be built in it.
//
// Both searches keep their results in static storage, shared across calls and
// across clients and never touched by Move_Reset. Clearing only the plan, the
// outcome and the variant left every other field holding the previous use's
// values, and the extension flag is the one that matters: a slot that
// carried a rerouted candidate last time would hand this candidate the
// extension pass's walk reference instead of its own arm's, so with no
// extension walk in this search it is unjudged and with one it is judged
// against a horizon it never rolled. The stale route metadata can reach
// route installation on top of that. Nothing here is allowed to
// survive a search, so the whole slot goes, and the sentinel fields - the
// measure's frame indices and the walk frame - are set rather than assumed
// from the zeroing.
static void StraightResultReset(straightResult_t *r,int variant) {
    memset(r,0,sizeof(*r));
    SpikeMeasureClear(&r->measure);
    r->outcome=NULL;
    r->walkFrame=-1;
    r->variant=variant;
}

// Anything a previous search left in a slot the caller has just built in.
// Construction empties the whole slot, so a dirty one here is a construction
// path that does not, which is the defect this was written for: the judge
// reads the extension flags to choose a candidate's walk reference, so a
// stale one sends an ordinary candidate to a horizon it never rolled.
static int StraightResultDirty(const straightResult_t *r) {
    return r->outcome!=NULL || r->rerouted || r->routeCells || r->routeMs ||
           r->margin!=0 || r->walk!=0 || r->walkFrame!=-1 || r->walkStopped || r->walkRefFrames ||
           r->score!=0 || r->measure.frames || r->measure.done!=-1 ||
           r->measure.launch[0]!=-1 || r->measure.launch[1]!=-1 ||
           r->measure.land[0]!=-1 || r->measure.land[1]!=-1 ||
           r->measure.landCell[0]!=-1 || r->measure.landCell[1]!=-1;
}

// The speed a candidate hands to its successor, as the distance it carries
// in the credited time. It is the last landing's measured speed: the state
// the next plan would search from. A candidate whose last landing was never
// measured earns nothing, so an unmeasured exit can never outrank a measured
// one. An arrival hands nothing forward and needs nothing: the route is over.
static float ExitCredit(const straightResult_t *r,int ms) {
    int last=r->candidate.count-1;
    if(ms<=0 || last<0 || last>1)return 0;
    if(!strcmp(r->outcome,"arrives"))return 0;
    if(r->measure.land[last]<0)return 0;
    return r->measure.landSpeed[last]*(ms*.001f);
}

// Diagnostic override: commit a named admitted candidate at a named
// sequence commit, and leave every other decision to the ordinary ranking.
// It exists to isolate one choice: comparing two ranking weights changes
// every later decision as well, so a difference between them cannot be
// attributed to the frame they first disagree on. Default off (`nth` 0),
// and a named candidate that is not admitted forces nothing.
static int SelectOverrideNth(void) {
    int nth=trap->Cvar_VariableIntegerValue("bot_moveSelectNth");
    return nth>0?nth:0;
}

static int SelectOverrideCand(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveSelectCand",value,sizeof(value));
    return value[0]?atoi(value):-1;
}

// Admitted, or arriving at the objective, which the judge treats alike.
// bot_moveRouteCheck (default on): refuse a landing no graph route leaves
// for the objective; see StraightJudge.
static int RouteCheck(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveRouteCheck",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

static int StraightAdmitted(const char *outcome) {return !strcmp(outcome,"admitted") || !strcmp(outcome,"arrives") ||
    !strcmp(outcome,"rerouted");}

// The verdict of a judged candidate set: the committed candidate, or -1
// when none was admitted, -2 when the guard was busy; the walk it was
// judged against.
typedef struct { int best,admitted,walked,walkFrames,walkFrame,walkStopped; float walk,margin,score; } straightVerdict_t;

// ---------------------------------------------------------------------------
// The search budget (`bot_moveSearchBudget`, default 4;
// 0 turns it off).
//
// The straight search runs to completion on the frame that triggers it, with
// no budget of any kind: four bots in the production configuration cost p50
// 67.5 us but p95 36.8 ms, p99 96.9 ms and a worst frame of 273 ms against a
// 25 ms server frame, 96 percent of it here
// (records/2026-09-22-graph-binding-and-decoupling.md). A budget that simply
// defers a landing-frame search changes decisions, because a landing keeps its
// speed only if it leaves the ground on the next frame, which this project has
// measured (LESSONS: "kept speed dies at the landing").
//
// So the work moves rather than waits. The frame the next search will run at
// is predictable - the landing boundary of the sequence just committed, or the
// retry boundary a failed search scheduled - and the forecast already predicts
// the frames in between with zero mismatches over tens of thousands of
// compared frames. The budget therefore:
//
//   - builds, once per commit or once per failed search, the horizon to that
//     boundary (the harness's own `ParityBuild`, into its own buffer);
//   - on each frame before it, installs that predicted boundary's state on the
//     bot, runs a bounded number of candidate rollouts of the same search from
//     it, and puts the live state back;
//   - at the actual boundary, compares the live state with the predicted one
//     exactly as the parity harness compares a frame, and, when they agree,
//     validates the chosen candidate from the actual state with one rollout
//     and commits it;
//   - falls back to the whole search on that frame whenever anything at all
//     disagrees, which is decision-identical by construction and only costs.
//
// The value is a **rollout count per bot per 25 ms frame** (scaled to the frame
// length, Bot2_WorkPerFrame: exact at sv_fps 40), not microseconds. A
// microsecond budget would split a search at a point that depends on the
// machine and the moment, so the same world would divide its work differently
// from run to run and a fallback would fire non-deterministically; and this
// project has already withdrawn one wall-clock reading of the search
// (LESSONS: "the earlier search's wall-clock budget reading is withdrawn")
// and has measured that reading a clock where the search runs is not free.
// A rollout count is deterministic, is the unit 96 percent of the cost is
// spent in, and makes "the same decisions" checkable rather than hoped for.
// ---------------------------------------------------------------------------
// The candidate slots one search can fill: the straight run-ups, the circle
// run-ups (bot_moveCircleRunup) and the planned chain's state candidates.
#define MOVE_SEARCH_SLOTS ((int)ARRAY_LEN(straightHops)*(int)ARRAY_LEN(straightAims)*((int)ARRAY_LEN(straightRunups)+(int)ARRAY_LEN(circleRunups))+MOVE_STATE_CANDIDATES)

// Default 4 (the production configuration, on
// which row 5 is judged): unset reads as 4; 0 turns the budget off and the
// search runs whole on the frame that triggers it.
#define MOVE_SEARCH_BUDGET_DEFAULT 4
static int SearchBudget(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveSearchBudget",value,sizeof(value));
    return Bot2_WorkPerFrame(value[0]?max(0,atoi(value)):MOVE_SEARCH_BUDGET_DEFAULT);   // per 25 ms of frame
}

static int SearchBudgetSpent(void) {
    return (searchRolloutBudget>0 && searchRollouts>=searchRolloutBudget) || SearchStepsSpent();
}

// Where a suspended judge resumes, and the locals of the judge that have to
// survive the suspension. With the budget off this is a stack local that the
// judge fills once and reads once, and every loop runs to its end.
enum { JUDGE_CANDIDATES=0, JUDGE_REROUTE, JUDGE_FINISH, JUDGE_DONE };
typedef struct {
    int phase,cursor;
    int maxDone,maxDoneB,maxDoneExt;
    int walkStopFrame,replayStopFrame,extensionStopFrame,replayWalked,extensionWalked;
    int rootSpeedMs,baseRouteMs;
    float rootGoal;
    int contaminated;
} judgeState_t;

enum { PRE_IDLE=0, PRE_PREDICTED, PRE_JUDGING, PRE_READY };

// One per client. The predicted boundary, the search built from it, and the
// fingerprint of everything the search read that is not the player state, so
// that a path replanned mid-flight refuses the cache instead of committing a
// decision taken against a path that is gone.
typedef struct {
    int stage,targetTime,targetFrames,planId,landingKind;
    // bot_moveWalkDefer: this prediction is a deferred walking search (1) or drop search (2, bot_moveDropDefer, for
    // dropEdge), taken at targetTime or restarted.
    int deferred,dropEdge;
    // bot_moveSearchWhy: the predicted frames up to the boundary (origin, velocity and the command each issues), and
    // the first live frame that left them: its index, and whether the command before it was the predicted one.
    int trackCount,trackTime[64],divergeFrame,divergeCmdSame;
    char divergeWhat[96];
    vec3_t trackOrigin[64],trackVelocity[64];
    usercmd_t trackCmd[64];
    // The boundary a prediction was last attempted for, and the plan it was
    // attempted under. A prediction is a horizon rollout; re-taking it every
    // frame for a boundary that has not moved is pure overhead, and the first
    // cost measurement of this work is what that overhead looks like.
    int attemptBoundary,attemptPlan;
    playerState_t rootPS;
    bot2_moveExecutor_t rootExec,rootExecBefore;
    usercmd_t rootCmd;
    int rootAnim[4];
    vec3_t rootOrigin,objective;
    int pathGeneration,pathCount,pathIndex,objectiveSet,commits,tapeCursor,permitActive;
    int candidates,originals,stateCandidates;
    int counts[CANDIDATE_OUTCOMES];
    // The candidate plans exactly as construction built them. The judge
    // rewrites a plan it finds arrives at the objective, so the set the
    // boundary rebuilds is compared against what was constructed and not
    // against what was judged.
    bot2_movePlan_t built[MOVE_SEARCH_SLOTS];
    straightResult_t results[MOVE_SEARCH_SLOTS];
    straightVerdict_t v;
    judgeState_t js;
    float us; int traces;
} searchCache_t;
static searchCache_t searchCache[MAX_CLIENTS];

// What the budget did over a leg, counted in state and printed once at the
// leg boundary where the parity tally prints. Never where it happens: a
// per-frame print on the search's own path measures a different bot
// (docs/movement/LESSONS.md).
static struct {
    int searches,predictions,predictRefused,predictNoTarget,chunks,ready,abandoned;
    int takes,hits,refusedContext,refusedOriginals,refusedState,refusedExec;
    int refusedCandidates,refusedValidate,missedNotReady;
    // Why not ready (records/2026-10-04-early-search-studies.md): no prediction, predicted but not
    // started, still judging, or ready for another frame (its target still ahead, or already passed).
    int notReadyIdle,notReadyPredicted,notReadyJudging,notReadyEarly,notReadyLate;
    // Frames the early search stood down for a player within bot_moveSearchAlone.
    int skippedNear;
    // bot_moveWalkDefer: deferred walking searches started, taken, refused at the take (no full search), not ready
    // at their deadline (restarted), whose prediction failed (searched whole at once), and chunk frames short of pool.
    int walkStarted,walkTaken,walkRefused,walkLate,walkPredictFailed,walkStarved;
    // bot_moveDeferGround: deferrals not started because the predicted walk leaves the ground by the take (searched whole at once).
    int walkAirborne,walkShortened;
    // Late by the stage it was in (dropped by a chunk / never chunked / still judging), and deferrals whose search
    // took one of its silent exits (nothing to judge: the cadence restarts, as after a live search's exit).
    int walkLateIdle,walkLatePredicted,walkLateJudging,walkEmpty;
    // Diagnostic: landings that found a walking deferral still in the cache, so no pre-search
    // was built for them and the landing searched whole; and of those, with no player inside bot_moveSearchAlone.
    int walkOrphan,walkOrphanAlone;
    // bot_moveWalkRescue: refused takes rescued as no hop, winners judged again from the live state, and of those committed.
    int walkRescueNone,walkRescueTried,walkRescueCommitted;
    // bot_moveDropDefer: deferred drop searches started, committed (winner run again from the live state and
    // admitted), failed and charged (no winner, state as predicted), dropped without a hop (no winner and the state
    // drifted; every ready recipe refused or no longer offered), committed on a recipe after the best (dropRescueGone),
    // refused on context, late, prediction
    // failed (searched whole at once), chunk frames short of pool.
    int dropStarted,dropTaken,dropNone,dropDrift,dropRescueFailed,dropRescueGone,dropContext,dropLate,dropPredictFailed,dropStarved;
    // Takes by the prediction's horizon (records/2026-10-04-tail-composition.md): [landing/cadence]
    // [2-4, 5-8, 9-12, 13-20, 21+ frames][taken, served, refused on own state or executor, refused on candidates/context].
    int horizon[2][5][4];
} searchTally[MAX_CLIENTS];
static char searchStateField[32];
// Set by WalkDefer when the straight search about to run is a deferred walking search's take (bot_moveWalkDefer).
static int walkDeferTake;
// Why the last SearchCacheTake refused a ready set: 1 the bot's own state, 2 its executor, 0 anything else.
static int takeRefusal;
// bot_moveWalkRescue (default 1; 0 turns it off): a deferred walking search refused only because the bot's own state
// or executor moved off the prediction rescues its result instead of deferring anew. A ready winner from the
// construction is rebuilt from the live state by the same recipe (its slot in this frame's construction) and judged
// alone - its rollout, the walk references and any reroute, a few rollouts - and committed if admitted; a ready set
// with no winner is taken as no hop, as a live search's would be. Not exact: the winner was ranked from the predicted
// state (records/2026-10-04-tail-composition.md, option 1).
static int WalkRescue(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveWalkRescue",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}

// A reset is a declared discontinuity, and a prediction across one is void.
static void SearchBudgetReset(int client) {
    if(client<0 || client>=MAX_CLIENTS)return;
    if(searchTally[client].searches)
        Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d tally searches=%d predictions=%d "
            "predict_refused=%d predict_no_target=%d chunks=%d ready=%d takes=%d hits=%d "
            "refused_context=%d refused_originals=%d refused_state=%d refused_executor=%d "
            "refused_candidates=%d refused_validate=%d not_ready=%d abandoned=%d last_field=%s\n",
            client,level.time,searchTally[client].searches,searchTally[client].predictions,
            searchTally[client].predictRefused,searchTally[client].predictNoTarget,
            searchTally[client].chunks,searchTally[client].ready,searchTally[client].takes,
            searchTally[client].hits,searchTally[client].refusedContext,
            searchTally[client].refusedOriginals,searchTally[client].refusedState,
            searchTally[client].refusedExec,searchTally[client].refusedCandidates,
            searchTally[client].refusedValidate,searchTally[client].missedNotReady,
            searchTally[client].abandoned,searchStateField[0]?searchStateField:"none");
    if(searchTally[client].searches)
        Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d notready idle=%d predicted=%d judging=%d ready_ahead=%d ready_passed=%d skipped_near=%d\n",
            client,level.time,searchTally[client].notReadyIdle,searchTally[client].notReadyPredicted,
            searchTally[client].notReadyJudging,searchTally[client].notReadyEarly,searchTally[client].notReadyLate,
            searchTally[client].skippedNear);
    if(searchTally[client].searches) {
        int k,b;
        for(k=0;k<2;k++)for(b=0;b<5;b++)if(searchTally[client].horizon[k][b][0])
            Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d horizon kind=%s bucket=%d taken=%d served=%d refused_state=%d refused_candidates=%d\n",
                client,level.time,k?"cadence":"landing",b,searchTally[client].horizon[k][b][0],searchTally[client].horizon[k][b][1],
                searchTally[client].horizon[k][b][2],searchTally[client].horizon[k][b][3]);
    }
    if(searchTally[client].walkStarted)
        Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d walk started=%d taken=%d refused=%d late=%d predict_failed=%d starved=%d "
            "late_idle=%d late_predicted=%d late_judging=%d empty=%d rescue_none=%d rescue_tried=%d rescue_committed=%d\n",
            client,level.time,searchTally[client].walkStarted,searchTally[client].walkTaken,searchTally[client].walkRefused,
            searchTally[client].walkLate,searchTally[client].walkPredictFailed,searchTally[client].walkStarved,
            searchTally[client].walkLateIdle,searchTally[client].walkLatePredicted,searchTally[client].walkLateJudging,
            searchTally[client].walkEmpty,searchTally[client].walkRescueNone,searchTally[client].walkRescueTried,
            searchTally[client].walkRescueCommitted);
    if(searchTally[client].walkAirborne || searchTally[client].walkShortened)
        Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d walk airborne_at_take=%d shortened=%d\n",client,level.time,searchTally[client].walkAirborne,searchTally[client].walkShortened);
    if(searchTally[client].walkOrphan)
        Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d walk orphan_landings=%d orphan_alone=%d\n",client,level.time,
            searchTally[client].walkOrphan,searchTally[client].walkOrphanAlone);
    if(searchTally[client].dropStarted)
        Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d drop started=%d taken=%d none=%d drift=%d rescue_failed=%d taken_later=%d "
            "context=%d late=%d predict_failed=%d starved=%d\n",
            client,level.time,searchTally[client].dropStarted,searchTally[client].dropTaken,searchTally[client].dropNone,
            searchTally[client].dropDrift,searchTally[client].dropRescueFailed,searchTally[client].dropRescueGone,
            searchTally[client].dropContext,searchTally[client].dropLate,searchTally[client].dropPredictFailed,
            searchTally[client].dropStarved);
    memset(&searchTally[client],0,sizeof(searchTally[client]));
    searchStateField[0]=0;
    searchCache[client].stage=PRE_IDLE;
    searchCache[client].deferred=0;
}
// The horizon the prediction is built into. One buffer, used inside the build
// call alone: what survives it is the boundary's own frame, copied out.
static bot2_forecastFrame_t searchBuildFrames[MOVE_PARITY_MAX_HORIZON];
static bot2_forecastResult_t searchBuildResult;

// The reroute pass's own walk reference, in its own buffers. A candidate
// the control set never judged must not lengthen the control's horizon or
// arm B's: a longer walk can end on a different status and unjudge a whole
// set. So a third reference is rolled from the same root, after both of
// them, and read by the rerouted candidates alone, to their own furthest
// done frame.
static bot2_forecastFrame_t extensionWalkFrames[MOVE_PARITY_MAX_HORIZON];
static bot2_forecastResult_t extensionWalkResult;
// The route a rerouted candidate's landing opens, while it is judged, and
// the one the committed candidate's landing opened, planned again at the
// commit and installed as the path the executor follows from that landing.
static bot2_movePath_t rerouteScratch,rerouteInstall;
// The candidate rerouteInstall was built for, or -1. The storage is static
// and shared across searches and clients, so the installer names what is in
// it rather than trusting that the build ran for the winner it has.
static int rerouteInstallCand=-1;

// A rollout the forecast refused at a trigger it touched arrives when the
// game's own touch test says so at its last frame. Read the same way
// wherever a candidate is rolled out, so a rejected candidate's path-free
// continuation is judged by the arrival test its first rollout had.
// bot_moveArriveSafe (default on; 0 off): an arrival is also rolled on past
// the flag, which then does not refuse (Bot2_ForecastFlagPass), through the
// same landing and hold, then coasting until it stops; one that touches a
// hazard, takes a lethal fall or does not end standing is refused
// `arrives_unsafe`. The rollout stopped at the flag's box, so
// nothing after the pickup was ever judged: on ctf4 (red_out_L3_respawn4) the
// pickup came mid-hop at 571 u/s and the hop carried on into the base pit,
// and Dantooine's respawn-4 cases landed in the hurt water - 6 of 2,392
// retained worlds (records/2026-09-25-arrive-safe.md). The arrival's own
// measure and frame are kept; only its admission changes.
static int ArriveSafe(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveArriveSafe",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}
// bot_moveArriveReroute (default 0, off until measured;
// records/2026-11-09-arrive-reroute.md): 1 rolls a rerouted arrival's safety
// continuation as the plan will be flown. A candidate reaches the reroute pass
// because one of its landings is off the path, and that landing comes before
// the pickup. The pass suspends StraightCommand's path stop (straightPathFree)
// for its own rollouts and restored it before the arrival check, so the
// check's continuation ended the plan's commands at that landing and coasted
// to a stop there: the second hop, or the onward walk through the flag, was
// never rolled, and 20% of arrival commits were judged safe on a flight that
// is not the one committed. With 1 the continuation keeps the path stop
// suspended, and the replacement route where the judged rollout had it.
static int ArriveReroute(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveArriveReroute",value,sizeof(value));
    return value[0]?atoi(value)!=0:0;
}
static int arrivalsChecked,arrivalsUnsafe;
// The continuation is physics, not the plan's policy: the committed commands
// run while they last, then the bot coasts (the knock recovery's coast: no
// input in the air, braked on the ground) until it has stopped. 47 of 59
// Metalorn refusals were the executor ending its commands in the air, 24
// frames in, which is not a fall.
static int arriveCoasting;
static bot2_moveKnockPolicy_t arriveKnock;
static bot2_forecastCommandStatus_t ArriveCheckCommand(const playerState_t *ps,bot2_maneuverState_t *policy,
    void *extra,int serverTime,const void *context,usercmd_t *command) {
    if(!arriveCoasting) {
        bot2_forecastCommandStatus_t status=StraightCommand(ps,policy,extra,serverTime,context,command);
        if(status==BOT2_FORECAST_COMMAND)return status;
        arriveCoasting=1;memset(&arriveKnock,0,sizeof(arriveKnock));
        arriveKnock.coast=1;arriveKnock.yaw=ps->viewangles[YAW];
    }
    return KnockCommand(ps,&arriveKnock,serverTime,((const bot2_moveContext_t*)context)->stepMsec,command)?
        BOT2_FORECAST_COMMAND:BOT2_FORECAST_POLICY_COMPLETE;
}
static void StraightArrives(gentity_t *ent,pmove_t *move,straightResult_t *r,const bot2_moveExecutor_t *before,
                            const bot2_moveExecutor_t *after,float rootGoal,float *us,int *traces) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    const playerState_t *last;
    if(strcmp(r->outcome,"refused") || r->measure.unsupported!=BOT2_FORECAST_TRIGGER || r->measure.frames<=0)return;
    last=&parity.frames[r->measure.frames-1].afterPS;
    if(RolloutFell(r->measure.frames,ps) || !ArrivesAt(ent->s.number,last,Bot2_GameStepMsec()))return;
    r->outcome="arrives";r->measure.done=r->measure.frames;r->measure.progress=rootGoal-GoalDistance(last,m->objective);
    // An arrival inside the first hop commits no second: the tap on the
    // landing frame would carry the hull over the box.
    if(r->candidate.count>1 && r->measure.launch[1]<0)r->candidate.count=1;
    if(ArriveSafe()) {
        spikeMeasure_t after2;int safe;
        straightStopTime=0;straightLandings=0;straightOffPath=0;arriveCoasting=0;
        Bot2_ForecastFlagPass(1);
        RunCandidate(ent,move,&r->candidate,before,after,ArriveCheckCommand,1,&after2,us,traces);
        Bot2_ForecastFlagPass(0);
        // Safe: no hazard, no lethal fall, and standing at the end. A drop is
        // not itself unsafe - Kejim's stand hands the carrier down 320 units -
        // so MOVE_FALL_DROP is not the test, and neither is a graph cell: 1,186
        // of 1,260 refusals stood on floor the graph has no cell for.
        safe=parity.result.status==BOT2_FORECAST_COMPLETE && !parity.result.unsupported &&
             Bot2_StandableEntity(parity.result.terminalPS.groundEntityNum);
        arrivalsChecked++;
        if(!safe) {
            arrivalsUnsafe++;r->outcome="arrives_unsafe";
            Bot2_PrintTelemetry(1,"[ARRIVE] client=%d level=%d unsafe=1 status=%d unsupported=%u frames=%d grounded=%d cell=%d end_z=%.1f drop=%.1f checked=%d unsafe_total=%d\n",
                ent->s.number,level.time,parity.result.status,parity.result.unsupported,parity.result.count,
                parity.result.terminalPS.groundEntityNum!=ENTITYNUM_NONE,Graph_CellAt(parity.result.terminalPS.origin,STEPSIZE+.25f),
                parity.result.terminalPS.origin[2],ps->origin[2]-parity.result.terminalPS.origin[2],arrivalsChecked,arrivalsUnsafe);
            // Only a rerouted arrival's continuation runs with the path stop suspended, and only under
            // bot_moveArriveReroute: the default prints nothing here.
            if(straightPathFree)Bot2_PrintTelemetry(1,"[ARRIVE] client=%d level=%d rerouted_unsafe=1 hops=%d land0=%d land1=%d frames=%d\n",
                ent->s.number,level.time,r->candidate.count,r->measure.land[0],r->measure.land[1],parity.result.count);
        }
    }
}

// The path index a point is at, within a window, or -1: where a landing
// beside the path makes progress to, so a candidate that stays on the path
// is priced by the route left from where it lands, as a rerouted one is
// priced by the route its landing opens.
static int PathIndexNear(const bot2_movePath_t *path,int from,int to,const vec3_t pos) {
    int k,best=-1;float bestD=MOVE_PATH_REACH*MOVE_PATH_REACH;
    for(k=max(0,from);k<path->count && k<=to;k++) {
        const graphCell_t *c=Graph_Cell(path->cells[k]);float dx,dy,d;
        if(!c || fabsf(c->z-pos[2])>2*STEPSIZE)continue;
        dx=c->ix*(float)GRAPH_GRID-pos[0];dy=c->iy*(float)GRAPH_GRID-pos[1];d=dx*dx+dy*dy;
        if(d<=bestD) {bestD=d;best=k;}
    }
    return best;
}

// The route a candidate's landing opens: the planner's own search from the
// cell the rollout landed on, in the state it measured there, to the cell
// the current path heads for, by the same two searches and the same avoid
// list a replan uses. Returns 0 when the landing is off the graph, when the
// objective cell is unknown, or when no route exists - a landing the
// planner cannot continue from is not an alternative, whatever its speed.
// Set once RerouteSearch has written its whole result, so an early refusal
// that leaves the caller's buffer untouched is never stored by the memo.
static int rerouteWrote;

// bot_moveRerouteJoin (default 1, with the
// slack below at the first join; 0 off; records/2026-10-06-frame-cost.md,
// "Reroute searches that rejoin"): 1 a reroute's state search may end where it
// rejoins the route being left, from the cell nearest the landing on, priced
// as its time there plus the time that route's own search had left from that
// cell, and the rest of that route is taken on; it ends when nothing it holds
// is more than the slack under the best such total, or at the goal as before.
// 2 checks: both searches run, the whole one's route is kept, and
// [REROUTEJOIN] compares them.
static int RerouteJoin(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveRerouteJoin",value,sizeof(value));
    return value[0]?atoi(value):1;
}
// bot_moveRerouteJoinSlack (ms, default 1000000: the first join the search
// reaches, the measured form): 0 is the proof above, which saves little where
// the heuristic is far under the time left (Dantooine).
#define MOVE_REROUTE_JOIN_SLACK 1000000.0f
static float RerouteJoinSlack(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveRerouteJoinSlack",value,sizeof(value));
    return value[0]?max(0.0f,(float)atof(value)):MOVE_REROUTE_JOIN_SLACK;
}

// The joins: the route's cells from the one nearest the landing on (in 2D,
// from the root's index), so a joined route never doubles back along the
// route it left, each with the time its search had left from it; none
// before an edge now off or avoided, which the route would take again. 0 when
// the route has no times or a planner directive applies.
static int rerouteJoinCells[GRAPH_MAX_PATH];
static float rerouteJoinRest[GRAPH_MAX_PATH];
static int RerouteJoins(const movement_t *m,const vec3_t land,int *first) {
    const bot2_movePath_t *p=&m->path;int k,kmin=straightRootIndex,n=0;float bestD=-1;
    PlanDirectivesSync();
    if(!p->timed || !p->state || p->count<1 || planDirectives.avoidCount || planDirectives.require>=0)return 0;
    if(kmin<0 || kmin>=p->count)return 0;
    for(k=straightRootIndex;k<p->count;k++) {
        const graphCell_t *c=Graph_Cell(p->cells[k]);float dx,dy;
        if(!c)continue;
        dx=c->ix*(float)GRAPH_GRID-land[0];dy=c->iy*(float)GRAPH_GRID-land[1];
        if(bestD<0 || dx*dx+dy*dy<bestD) {bestD=dx*dx+dy*dy;kmin=k;}
    }
    for(k=p->count-1;k>kmin;k--)if(p->edges[k]>=0) {
        const graphEdge_t *e=Graph_Edge(p->edges[k]);int a,off=!e || Graph_EdgeOff(e);
        for(a=0;!off && a<m->state.avoidCount;a++)if(m->state.avoid[a]==p->edges[k])off=1;
        if(off) {kmin=k;break;}
    }
    *first=kmin;
    for(k=kmin;k<p->count;k++) {
        rerouteJoinCells[n]=p->cells[k];rerouteJoinRest[n]=p->times[p->count-1]-p->times[k];n++;
    }
    return n;
}

// The route left, from the join on, appended to a search that ended on it.
// 0 when the whole would not fit.
static int RerouteSplice(const movement_t *m,int first,int joined,bot2_movePath_t *out) {
    const bot2_movePath_t *p=&m->path;int k,best=-1,n=out->count,j;float base;
    if(n<1 || out->cells[n-1]!=joined || !out->timed)return 0;
    for(k=first;k<p->count;k++)if(p->cells[k]==joined && (best<0 || p->times[k]>p->times[best]))best=k;
    if(best<0 || n+p->count-1-best>GRAPH_MAX_PATH)return 0;
    base=out->times[n-1];
    for(j=best+1;j<p->count;j++,n++) {
        out->cells[n]=p->cells[j];out->edges[n]=p->edges[j];out->speeds[n]=p->speeds[j];
        out->times[n]=base+p->times[j]-p->times[best];
    }
    out->joinedAt=out->count;out->bodies=p->bodies;   // its tail is the left route's, bodies and all
    out->costMs=(int)(base+p->times[p->count-1]-p->times[best]);
    out->count=n;
    return 1;
}

static bot2_movePath_t rerouteJoinCheck;
static int RerouteSearchFor(int client,const straightResult_t *r,int rootSpeedMs,bot2_movePath_t *out);
static int RerouteSearch(int client,const straightResult_t *r,int rootSpeedMs,bot2_movePath_t *out) {
    int found;
    ForbidPlanBegin(client);
    found=RerouteSearchFor(client,r,rootSpeedMs,out);
    ForbidPlanEnd();
    return found;
}
static int RerouteSearchFor(int client,const straightResult_t *r,int rootSpeedMs,bot2_movePath_t *out) {
    movement_t *m=&movement[client];const spikeMeasure_t *s=&r->measure;
    int h=r->candidate.count-1,cell,k;graphState_t exit;
    if(h<0 || h>1 || s->land[h]<0 || m->path.to<0)return 0;
    cell=s->landCell[h]>=0?s->landCell[h]:Graph_Nearest(s->landPos[h],MOVE_PATH_REACH,2*STEPSIZE);
    if(cell<0)return 0;
    exit.speed=s->landSpeed[h];exit.dir[0]=s->landDir[h][0];exit.dir[1]=s->landDir[h][1];exit.tap=1;
    exit.speedMs=rootSpeedMs>0?max(0,rootSpeedMs-s->land[h]*Bot2_GameStepMsec()):0;
    memset(out,0,sizeof(*out));out->require=-1;rerouteWrote=1;
    if(StateEnabled()) {
        int mode=RerouteJoin(),first=0,joins=mode?RerouteJoins(m,s->landPos[h],&first):0;
        int joined=-1;
        bot2_movePath_t *bounded=mode==2?&rerouteJoinCheck:out;
        if(joins) {
            if(bounded!=out) {memset(bounded,0,sizeof(*bounded));bounded->require=-1;}
            Graph_PlanJoinSet(rerouteJoinCells,rerouteJoinRest,joins,RerouteJoinSlack());
            bounded->count=PlanStateDirected(m,m->path.replans+1,cell,&exit,m->path.to,bounded);
            joined=Graph_PlanJoined();Graph_PlanJoinClear();
            if(bounded->count>0 && joined>=0 && !RerouteSplice(m,first,joined,bounded))bounded->count=0;
        }
        if(!joins || mode==2 || (!out->count && joined>=0)) {
            // Installed, this route would be the leg's next plan.
            out->count=PlanStateDirected(m,m->path.replans+1,cell,&exit,m->path.to,out);
        }
        if(joins && mode==2)
            Bot2_PrintTelemetry(1,"[REROUTEJOIN] client=%d level=%d gen=%d from=%d to=%d joins=%d joined=%d "
                "bounded_cells=%d bounded_expanded=%d bounded_cost_ms=%d bounded_price_ms=%d "
                "whole_cells=%d whole_expanded=%d whole_cost_ms=%d whole_price_ms=%d same=%d\n",
                client,level.time,m->path.generation,cell,m->path.to,joins,joined,
                bounded->count,bounded->expanded,bounded->count?bounded->costMs:-1,
                bounded->count?PathRemainingMs(bounded,0):-1,out->count,out->expanded,out->count?out->costMs:-1,
                out->count?PathRemainingMs(out,0):-1,
                bounded->count==out->count && (!out->count || (!memcmp(bounded->cells,out->cells,sizeof(int)*out->count) &&
                                                               !memcmp(bounded->edges,out->edges,sizeof(int)*out->count))));
        out->state=out->count>0;
    }
    if(!out->count) {
        int kept=Graph_PlanBoundRoute(cell,m->path.to,out->cells,out->edges,GRAPH_MAX_PATH,&out->costMs);
        out->count=kept?max(kept,0):Graph_Path(cell,m->path.to,out->cells,out->edges,GRAPH_MAX_PATH,&out->costMs);
    }
    if(!out->count)return 0;
    out->from=cell;out->to=m->path.to;
    for(k=0;k<out->count;k++)if(out->edges[k]>=0 && PathStrafe(out,k))out->strafe++;
    return 1;
}

// Within one straight search RerouteSearch is a pure function of what the
// key below holds: the landing it plans from, the root's Speed time, the cell
// the path heads for, the replan ordinal, the avoid list, whether the state
// search runs, and the graph (its generation). The reroute pass asks it once
// per off-path candidate, candidates that land identically ask it again, and
// the commit asks again for the winner (records/2026-09-22-boundary-cost-
// attribution.md, section 3). A hit returns the struct the search produced,
// byte for byte; six reference cells made identical decisions with it on and
// off before it was made unconditional (records/2026-09-22-row5-planner.md). The table is emptied at every entry to StraightSearchCore
// (RerouteMemoClear) and bypassed whenever a planner directive is set, since
// a directive's spent-state reads more of the movement than the key holds.
#define MOVE_REROUTE_MEMO 16
typedef struct {
    int h,landCell,landFrame,speedMs,to,replans,avoidCount,state;
    float landPos[3],landSpeed,landDir[2];
    unsigned int generation;
    int avoid[32];
} rerouteKey_t;
static struct {
    int count;
    rerouteKey_t key[MOVE_REROUTE_MEMO];
    int found[MOVE_REROUTE_MEMO];
    bot2_movePath_t path[MOVE_REROUTE_MEMO];
} rerouteMemo[MAX_CLIENTS];

static void RerouteMemoClear(int client) {
    if(client>=0 && client<MAX_CLIENTS)rerouteMemo[client].count=0;
}

static int RerouteKey(int client,const straightResult_t *r,int rootSpeedMs,rerouteKey_t *k) {
    const movement_t *m=&movement[client];const spikeMeasure_t *s=&r->measure;int h=r->candidate.count-1,i;
    PlanDirectivesSync();
    if(planDirectives.avoidCount || planDirectives.require>=0)return 0;
    if(h<0 || h>1 || m->state.avoidCount>32)return 0;
    memset(k,0,sizeof(*k));
    k->h=h;k->landCell=s->landCell[h];k->landFrame=s->land[h];
    k->speedMs=rootSpeedMs>0?max(0,rootSpeedMs-s->land[h]*Bot2_GameStepMsec()):0;
    k->to=m->path.to;k->replans=m->path.replans;k->state=StateEnabled();
    VectorCopy(s->landPos[h],k->landPos);k->landSpeed=s->landSpeed[h];
    k->landDir[0]=s->landDir[h][0];k->landDir[1]=s->landDir[h][1];
    k->generation=Graph_Generation();
    k->avoidCount=m->state.avoidCount;
    for(i=0;i<m->state.avoidCount;i++)k->avoid[i]=m->state.avoid[i];
    return 1;
}

// RerouteSearch as the reroute pass and the commit call it, through the memo,
// charged whole to the cost fixture's frame when a capture is active.
static int RerouteFrom(int client,const straightResult_t *r,int rootSpeedMs,bot2_movePath_t *out) {
    double began=Bot2_MoveCostClock();rerouteKey_t key;int found,i,keyed=0;
    if(client>=0 && client<MAX_CLIENTS && (keyed=RerouteKey(client,r,rootSpeedMs,&key))) {
        for(i=0;i<rerouteMemo[client].count;i++)if(!memcmp(&rerouteMemo[client].key[i],&key,sizeof(key))) {
            *out=rerouteMemo[client].path[i];found=rerouteMemo[client].found[i];
            Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_REROUTE,began);
            Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_REROUTE_CALLS);
            return found;
        }
    }
    rerouteWrote=0;
    bot2_meterInReplan=2;found=RerouteSearch(client,r,rootSpeedMs,out);bot2_meterInReplan=0;
    if(keyed && rerouteWrote && rerouteMemo[client].count<MOVE_REROUTE_MEMO) {
        i=rerouteMemo[client].count++;
        rerouteMemo[client].key[i]=key;rerouteMemo[client].found[i]=found;rerouteMemo[client].path[i]=*out;
    }
    Bot2_MoveCostSection(BOT2_MOVECOST_SECTION_REROUTE,began);
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_REROUTE_CALLS);
    return found;
}

// Carry a validated alternative route into execution. The bot stands at the
// takeoff of a flight that leaves the path it is following, so from the
// landing it follows this one instead: a new identity, a fresh retry timer,
// and a declared discontinuity for the parity harness and for any permit,
// exactly as a replan is. The progress index is set after the commit, which
// restores the executor the frame started with.
static void RerouteInstall(int client,const bot2_movePath_t *alt) {
    movement_t *m=&movement[client];char detail[288];
    int replans=m->path.replans,previous=m->path.count,edges=0,k;
    for(k=0;k<alt->count;k++)if(alt->edges[k]>=0)edges++;
    m->path=*alt;
    m->path.replans=replans+1;m->path.time=level.time;m->path.generation=++replayGeneration;
    m->path.replan=0;m->path.retryTime=level.time+MOVE_PATH_RETRY;
    PermitRevoke(client,"reroute");
    ParityReset(client);
    Com_sprintf(detail,sizeof(detail),"from=%d to=%d cells=%d edges=%d cost_ms=%d walk_ms=%d state=%d strafe=%d previous_cells=%d gen=%d",
        m->path.from,m->path.to,m->path.count,edges,m->path.costMs,PathRemainingMs(&m->path,0),
        m->path.state,m->path.strafe,previous,m->path.generation);
    if(m->path.directive)
        Q_strcat(detail,sizeof(detail),va(" directive=%d plan_avoid=%d require=%d require_met=%d",
            m->path.directive,m->path.directiveAvoid,m->path.require,m->path.requireMet));
    Event(client,MOVE_EVENT_PLAN,"reroute","path",detail);
    // Its [ROUTE] line waits for the search to return (RoutePendingPrint).
    routePending[client]=m->path.generation+1;
}

// Roll the candidates out from this root, judge each as a straight
// candidate (the hold after the landing, landings beside the path, arrival
// by the game's own touch test), then the walk to the furthest admitted
// done frame, and rank the admitted candidates by their margin over the
// walk. Shared by the straight search and the state search, so a planned
// edge is validated by exactly the judge that learned it. Prints every
// candidate; counts the outcomes.
// `js` carries the resume point and the locals that must survive a
// suspension. With the budget off the caller hands over a zeroed stack local,
// `searchRolloutBudget` is zero, every loop runs to its end and the function
// returns 1 on its first call: byte for byte the behaviour that was measured.
// Under the budget the judge may stop between two candidates of the
// candidate loop or of the reroute pass. The walk references
// are written into shared buffers and read by the ranking, so all three are
// rolled in the last chunk, after both passes, and never cross a frame. Every
// rollout is a pure function of the root and its candidate - the guard
// restores the world after each - so rolling the walks after the passes
// rather than before them changes no measurement (row 5).
static int StraightJudge(gentity_t *ent,pmove_t *move,straightResult_t *results,int candidates,int index,
                          const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after,const float *ref,
                          int *counts,float *us,int *traces,straightVerdict_t *v,judgeState_t *js) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    bot2_forecastGuard_t guard;float rootGoal;
    int i,k,maxDone,walkStopFrame,maxDoneB,replayWalked,replayStopFrame;
    int rerouting=RerouteFlags()&1,exitMs=ExitSpeedMs(),arriveReroute=ArriveReroute();
    int maxDoneExt,extensionWalked,extensionStopFrame,baseRouteMs;
    int rootSpeedMs,partway;
    int routeCheck=RouteCheck(),goalCell=routeCheck?Move_RootCell(m->objective):-1;
    int resume=js->phase!=JUDGE_CANDIDATES || js->cursor>0;
    if(!resume) {
        memset(js,0,sizeof(*js));
        js->rootGoal=GoalDistance(ps,m->objective);
        js->baseRouteMs=PathRemainingMs(&m->path,index);
        js->rootSpeedMs=(ps->fd.forcePowersActive&(1<<FP_SPEED))?max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time):0;
        js->walkStopFrame=js->replayStopFrame=js->extensionStopFrame=-1;
        memset(v,0,sizeof(*v));v->best=-1;v->walkFrame=-1;
        memset(counts,0,sizeof(int)*CANDIDATE_OUTCOMES);
    }
    rootGoal=js->rootGoal;baseRouteMs=js->baseRouteMs;rootSpeedMs=js->rootSpeedMs;
    maxDone=js->maxDone;maxDoneB=js->maxDoneB;maxDoneExt=js->maxDoneExt;
    walkStopFrame=js->walkStopFrame;replayStopFrame=js->replayStopFrame;extensionStopFrame=js->extensionStopFrame;
    replayWalked=js->replayWalked;extensionWalked=js->extensionWalked;
    // The slots as the caller handed them over. Every one must be a fresh
    // result, because the storage is static and shared across searches and
    // clients. A dirty slot is reported and then emptied, so a construction
    // path that stops resetting is named in the log rather than judged: this
    // is the audit for the defect, and it runs on the real slots the real
    // judge is about to read.
    for(i=0;!resume && i<candidates;i++)if(StraightResultDirty(&results[i])) {
        bot2_movePlan_t plan=results[i].candidate;int variant=results[i].variant;
        straightStaleSlots++;
        Bot2_PrintTelemetry(1,"[STRAIGHTSLOT] client=%d level=%d cand=%d stale=1 outcome=%s rerouted=%d "
            "tap_onward=%d route_cells=%d walk_ref_frames=%d total_stale=%d\n",
            ent->s.number,level.time,i,results[i].outcome?results[i].outcome:"none",
            results[i].rerouted,0,results[i].routeCells,results[i].walkRefFrames,
            straightStaleSlots);
        StraightResultReset(&results[i],variant);results[i].candidate=plan;
    }
    SearchDisarm(ent);
    if(!Bot2_ForecastGuardBegin(move,&guard)) {v->best=-2;js->phase=JUDGE_DONE;return 1;}
    // A chunk that found the world contaminated ended its loops there; the
    // next chunk carries that forward as the unsuspended judge would.
    if(js->contaminated)guard.contaminated=1;
    straightRootIndex=index;
    // The candidates first, so the walk is rolled out only as far as the
    // furthest admitted one needs, and not at all when none is admitted.
    for(i=js->phase==JUDGE_CANDIDATES?js->cursor:candidates;i<candidates && !guard.contaminated;i++) {
        straightResult_t *r=&results[i];bot2_movePlan_t *candidate=&r->candidate;
        if(SearchBudgetSpent()) {
            // Between two candidates: results[] is this client's own
            // storage, and nothing a later frame reads has been written yet.
            js->phase=JUDGE_CANDIDATES;js->cursor=i;
            js->maxDone=maxDone;js->maxDoneB=maxDoneB;js->maxDoneExt=maxDoneExt;
            js->contaminated=guard.contaminated;
            Bot2_ForecastGuardEnd(&guard);
            return 0;
        }
        straightStopTime=0;straightLandings=0;straightOffPath=0;
        r->outcome=RunCandidate(ent,move,candidate,before,after,StraightCommand,1,&r->measure,us,traces);
        if(straightOffPath)r->outcome="off_path";
        // The forecast declines a touched trigger by contract. When the
        // last frame is at the objective, the trigger is the flag and the
        // touch is the pickup or capture: the candidate arrives, done at
        // that frame, unless it fell on the way.
        StraightArrives(ent,move,r,before,after,rootGoal,us,traces);
        // Paired with the rollout just judged, before any other is issued.
        Bot2_ForecastShadowRescue(!strcmp(r->outcome,"arrives"));
        if(StraightAdmitted(r->outcome)) {
            for(k=0;k<candidate->count;k++)if(r->measure.land[k]>=0 && !NearPathCell(&m->path,index,index+48,r->measure.landPos[k]))r->outcome="off_path";
            // The control's walk is rolled out to the control arm's furthest
            // done frame only. An observe-only candidate must not lengthen
            // it: a longer walk can end on a different status and unjudge
            // the whole control set. Arm B's own reference is rolled after
            // it, into its own buffers, to arm B's own furthest done frame.
            // Offering B changes eligibility only, never A's reference.
            if(StraightAdmitted(r->outcome)) {
                if(r->variant)maxDoneB=max(maxDoneB,r->measure.done);
                else maxDone=max(maxDone,r->measure.done);
            }
        }
    }
    if(js->phase==JUDGE_CANDIDATES) {js->phase=JUDGE_REROUTE;js->cursor=0;}
    // The candidates whose landing left the path, continued. Each is rolled
    // out again from the same root without the path stop, through the same
    // physical, onward and hold checks, and is admitted only when every
    // landing is on the graph and the last one opens a route the planner
    // can continue from. Its reference is rolled last, into its own
    // buffers: neither the control's horizon nor arm B's is lengthened by a
    // candidate they never judged.
    if(js->phase==JUDGE_REROUTE && rerouting && !guard.contaminated) {
        for(i=js->cursor;i<candidates && !guard.contaminated;i++) {
            straightResult_t *r=&results[i];
            if(!r->outcome || strcmp(r->outcome,"off_path"))continue;
            if(SearchBudgetSpent()) {
                // Between two off-path candidates: each one's continuation is
                // its own slot and the route it priced is not kept.
                js->cursor=i;js->maxDone=maxDone;js->maxDoneB=maxDoneB;js->maxDoneExt=maxDoneExt;
                js->contaminated=guard.contaminated;
                Bot2_ForecastGuardEnd(&guard);
                return 0;
            }
            straightStopTime=0;straightLandings=0;straightOffPath=0;straightPathFree=1;
            r->outcome=RunCandidate(ent,move,&r->candidate,before,after,StraightCommand,1,&r->measure,us,traces);
            straightPathFree=arriveReroute;   // the arrival's continuation, as this rollout ran (bot_moveArriveReroute)
            StraightArrives(ent,move,r,before,after,rootGoal,us,traces);
            straightPathFree=0;
            if(StraightAdmitted(r->outcome))
                for(k=0;k<r->candidate.count;k++)if(r->measure.land[k]>=0 && r->measure.landCell[k]<0)r->outcome="off_graph";
            if(!StraightAdmitted(r->outcome))continue;
            r->routeMs=r->measure.done*Bot2_GameStepMsec();
            if(strcmp(r->outcome,"arrives")) {
                // An arrival needs no route; any other landing does.
                int last=r->candidate.count-1,pass1Cell,pass1Done;
                bot2_moveExecutor_t rerouteBefore;
                if(!RerouteFrom(ent->s.number,r,rootSpeedMs,&rerouteScratch)) {r->outcome="no_route";continue;}
                // The first pass steered its onward stage and its hold by
                // the path the flight leaves, which is not the path the
                // commit installs: it judged a continuation that will never
                // be executed. The candidate is rolled out once more, from
                // the same root, with the replacement route in place of the
                // current one and no permit - the state RerouteInstall
                // leaves behind - and it is that rollout which is judged.
                // The flight itself is plan-driven and repeats frame for
                // frame; only what follows the last landing differs. A
                // landing that does not repeat leaves the route unbacked,
                // so the candidate is refused rather than admitted on a
                // route planned from a landing it no longer makes.
                pass1Cell=last>=0?r->measure.landCell[last]:-1;pass1Done=r->measure.done;
                rerouteBefore=*before;rerouteBefore.pathIndex=0;
                straightStopTime=0;straightLandings=0;straightOffPath=0;straightPathFree=1;
                rolloutPathOverride=&rerouteScratch;
                r->outcome=RunCandidate(ent,move,&r->candidate,&rerouteBefore,after,StraightCommand,1,&r->measure,us,traces);
                if(!arriveReroute) {rolloutPathOverride=NULL;straightPathFree=0;}
                StraightArrives(ent,move,r,&rerouteBefore,after,rootGoal,us,traces);
                rolloutPathOverride=NULL;straightPathFree=0;
                if(StraightAdmitted(r->outcome))
                    for(k=0;k<r->candidate.count;k++)if(r->measure.land[k]>=0 && r->measure.landCell[k]<0)r->outcome="off_graph";
                // The replacement steers the onward walk even when that
                // walk reaches the objective. Keep its landing binding and
                // install it for arrivals too; otherwise execution follows
                // the old path instead of the continuation just validated.
                if(StraightAdmitted(r->outcome) &&
                   (last<0 || r->measure.landCell[last]!=pass1Cell)) {
                    Bot2_PrintTelemetry(1,"[REROUTE] client=%d level=%d cand=%d revalidate=drift pass1_cell=%d pass2_cell=%d "
                        "pass1_done=%d pass2_done=%d\n",ent->s.number,level.time,i,pass1Cell,
                        last>=0?r->measure.landCell[last]:-1,pass1Done,r->measure.done);
                    r->outcome="no_route";
                }
                if(!StraightAdmitted(r->outcome))continue;
                r->routeCells=rerouteScratch.count;
                if(strcmp(r->outcome,"arrives")) {
                    r->routeMs=r->measure.done*Bot2_GameStepMsec()+PathRemainingMs(&rerouteScratch,0);
                    r->outcome="rerouted";
                } else r->routeMs=r->measure.done*Bot2_GameStepMsec();
            }
            r->rerouted=1;
            maxDoneExt=max(maxDoneExt,r->measure.done);
            // pass_outcome is what the continuation itself did. The route
            // gate and the margin against this pass's own reference are
            // applied below, and can still refuse it: the candidate line
            // carries the outcome it ends on.
            Bot2_PrintTelemetry(1,"[REROUTE] client=%d level=%d gen=%d cand=%d hops=%d runup=%g pass_outcome=%s frames=%d done=%d "
                "land_cell=%d land_speed=%.3f land_pos=%.3f,%.3f,%.3f route_cells=%d route_from=%d route_to=%d "
                "route_state=%d route_cost_ms=%d route_ms=%d flight_ms=%d base_route_ms=%d route_joined=%d\n",
                ent->s.number,level.time,m->path.generation,i,r->candidate.count,r->candidate.hop[0].runup,
                r->outcome,r->measure.frames,r->measure.done,
                r->measure.landCell[r->candidate.count-1],r->measure.landSpeed[r->candidate.count-1],
                r->measure.landPos[r->candidate.count-1][0],r->measure.landPos[r->candidate.count-1][1],
                r->measure.landPos[r->candidate.count-1][2],r->routeCells,rerouteScratch.from,rerouteScratch.to,
                rerouteScratch.state,rerouteScratch.costMs,r->routeMs,r->measure.done*Bot2_GameStepMsec(),baseRouteMs,
                rerouteScratch.joinedAt);
        }
    }
    js->phase=JUDGE_FINISH;js->cursor=0;
    // The walk over the same frames, to the furthest admitted done frame.
    if(maxDone>0 && !guard.contaminated) {
        // A walk the forecast refused at a trigger stopped there, at the
        // flag: its frames up to the touch stand.
        StraightWalk(ent,move,maxDone);
        v->walked=straightWalkResult.count>=1 && (straightWalkResult.status==BOT2_FORECAST_COMPLETE || straightWalkResult.unsupported==BOT2_FORECAST_TRIGGER);
        walkStopFrame=v->walked && (straightWalkResult.completion==BOT2_FORECAST_COMPLETION_POLICY || straightWalkResult.status!=BOT2_FORECAST_COMPLETE)?straightWalkResult.count:-1;
        v->walkFrames=v->walked?straightWalkResult.count:0;
    }
    // Arm B's reference, from the same root, after the control's walk and
    // after every candidate rollout, so nothing the control reads has been
    // written since it read it. Both offered and observe-only B candidates
    // use this reference; default gameplay never pays for it.
    if(maxDoneB>0 && !guard.contaminated) {
        StraightWalkInto(ent,move,maxDoneB,replayWalkFrames,&replayWalkResult);
        replayWalked=replayWalkResult.count>=1 && (replayWalkResult.status==BOT2_FORECAST_COMPLETE || replayWalkResult.unsupported==BOT2_FORECAST_TRIGGER);
        replayStopFrame=replayWalked && (replayWalkResult.completion==BOT2_FORECAST_COMPLETION_POLICY || replayWalkResult.status!=BOT2_FORECAST_COMPLETE)?replayWalkResult.count:-1;
    }
    // The extension reference, rolled once after both passes and after both
    // controls, into its own buffers: nothing either control reads has been
    // written since it read it.
    if(maxDoneExt>0 && !guard.contaminated) {
        StraightWalkInto(ent,move,maxDoneExt,extensionWalkFrames,&extensionWalkResult);
        extensionWalked=extensionWalkResult.count>=1 && (extensionWalkResult.status==BOT2_FORECAST_COMPLETE || extensionWalkResult.unsupported==BOT2_FORECAST_TRIGGER);
        extensionStopFrame=extensionWalked && (extensionWalkResult.completion==BOT2_FORECAST_COMPLETION_POLICY || extensionWalkResult.status!=BOT2_FORECAST_COMPLETE)?extensionWalkResult.count:-1;
    }
    // Contaminated: the rollouts after it ran nothing, so candidates were never rolled and the walk
    // reference may not have been. The verdict is the guard's, not the edge's: no charge, and the search is retried.
    // The guard's flag is set by End (a rollout's contamination or End's whole-world compare); a judge resumed
    // from an earlier frame carries its earlier scope's in js->contaminated.
    partway=js->contaminated;
    Bot2_ForecastGuardEnd(&guard);
    partway|=guard.contaminated;
    for(i=0;i<candidates;i++) {
        straightResult_t *r=&results[i];float walk=0,margin=0;int walkFrame=-1,walkStopped=0;
        if(guard.contaminated && !r->outcome)r->outcome="refused";
        if(StraightAdmitted(r->outcome)) {
            // The walk's progress at the frame the sequence was done. The
            // rollouts share the root, so their frames align. A walk that
            // stopped earlier is measured where it stopped; a walk whose
            // horizon ran out first, or that was refused, cannot judge.
            // Each arm reads its own reference and only its own.
            int own=r->rerouted?2:r->variant;
            const bot2_forecastResult_t *armWalk=own==2?&extensionWalkResult:own?&replayWalkResult:&straightWalkResult;
            const bot2_forecastFrame_t *armFrames=own==2?extensionWalkFrames:own?replayWalkFrames:straightWalkFrames;
            int walked=own==2?extensionWalked:own?replayWalked:v->walked;
            int stopFrame=own==2?extensionStopFrame:own?replayStopFrame:walkStopFrame;
            walkFrame=r->measure.done-1;
            if(!walked)walkFrame=-1;
            else if(walkFrame>=armWalk->count) {
                if(stopFrame>=0) {walkFrame=armWalk->count-1;walkStopped=1;}
                else walkFrame=-1;
            }
            if(walkFrame<0)r->outcome="unjudged";
            else {
                walk=rootGoal-GoalDistance(&armFrames[walkFrame].afterPS,m->objective);
                margin=r->measure.progress-walk;
                if(margin<MOVE_STRAIGHT_GAIN)r->outcome="slower";
            }
            // A rerouted candidate keeps the local margin test against its
            // own reference and must also leave the bot on a faster route
            // to the objective than the one it left, priced for both by the
            // path model's own walk speed. The margin alone chose a landing,
            // never a route. An arrival already reaches the objective and
            // retains its arrival admission even when it needs a new path.
            if(r->rerouted && r->routeCells>0 && StraightAdmitted(r->outcome) &&
               strcmp(r->outcome,"arrives") &&
               r->routeMs+MOVE_REROUTE_GAIN>baseRouteMs)r->outcome="slower_route";
        }
        // While the reroute pass runs, every admitted candidate is priced
        // the same way for the record (route_ms): the rollout's own time plus
        // the walk model's remaining path from where it lands.
        if(rerouting && !r->rerouted && StraightAdmitted(r->outcome)) {
            int last=r->candidate.count-1,idx=-1;
            if(last>=0 && last<2 && r->measure.land[last]>=0)
                idx=PathIndexNear(&m->path,index,index+48,r->measure.landPos[last]);
            r->routeMs=r->measure.done*Bot2_GameStepMsec()+(idx>=0?PathRemainingMs(&m->path,idx):baseRouteMs);
            if(!strcmp(r->outcome,"arrives"))r->routeMs=r->measure.done*Bot2_GameStepMsec();
        }
        // A landing no route leaves is not a join (bot_moveRouteCheck, default
        // on): the continuation from it is part of the maneuver. An arrival
        // has already reached the objective.
        if(routeCheck && StraightAdmitted(r->outcome) && strcmp(r->outcome,"arrives")) {
            int last=r->candidate.count-1;
            if(last>=0 && last<2 && r->measure.land[last]>=0 && r->measure.landCell[last]>=0 &&
               !Graph_Reaches(r->measure.landCell[last],goalCell))r->outcome="no_route";
        }
        r->margin=margin;
        r->walk=walk;r->walkFrame=walkFrame;r->walkStopped=walkStopped;
        r->walkRefFrames=r->rerouted?(maxDoneExt>0?extensionWalkResult.count:0):
            r->variant?(maxDoneB>0?replayWalkResult.count:0):(maxDone>0?straightWalkResult.count:0);
        for(k=0;k<CANDIDATE_OUTCOMES;k++)if(!strcmp(r->outcome,candidateOutcomes[k]))counts[k]++;
        PrintCandidate(ent->s.number,&r->candidate,i,&r->measure,r->outcome,ref);
        if(StraightAdmitted(r->outcome)) {
            v->admitted++;
            // Only the control arm is ranked while the replay arm is
            // observe-only: it is measured from the same root and never
            // commits.
            r->score=margin+ExitCredit(r,exitMs);
            if(!r->variant && (v->best<0 || r->score>v->score)) {
                v->best=i;v->walk=walk;v->margin=margin;v->score=r->score;v->walkFrame=walkFrame;v->walkStopped=walkStopped;
            }
        }
    }
    if(partway)v->best=-2;
    js->phase=JUDGE_DONE;js->contaminated=guard.contaminated;
    return 1;
}

// Diagnose path rejection without changing selection. Continue each rejected
// rollout through native landing/onward checks, but never feed its result into
// admission, the walk-reference horizon, ranking, or graph learning.
static int AuditOffPath(gentity_t *ent,pmove_t *move,const straightResult_t *results,int candidates,
                       const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after) {
    bot2_forecastGuard_t guard;int i;
    if(!trap->Cvar_VariableIntegerValue("bot_moveOffPathAudit"))return 1;
    if(!Bot2_ForecastGuardBegin(move,&guard))return 0;
    for(i=0;i<candidates && !guard.contaminated;i++) {
        const straightResult_t *r=&results[i];spikeMeasure_t measured;
        const char *outcome;float us=0;int traces=0;
        if(!r->outcome || strcmp(r->outcome,"off_path"))continue;
        hopStopTime=0;
        outcome=RunCandidate(ent,move,&r->candidate,before,after,HopHoldCommand,1,&measured,&us,&traces);
        Bot2_PrintTelemetry(1,"[PATHAUDIT] client=%d level=%d cand=%d hops=%d runup=%g original=off_path "
            "outcome=%s frames=%d unsupported=%d done=%d progress=%.3f land0=%d speed0=%.3f "
            "pos0=%.3f,%.3f,%.3f land1=%d speed1=%.3f pos1=%.3f,%.3f,%.3f observational=1\n",
            ent->s.number,level.time,i,r->candidate.count,r->candidate.hop[0].runup,
            outcome,measured.frames,measured.unsupported,measured.done,measured.progress,
            measured.land[0],measured.landSpeed[0],measured.landPos[0][0],measured.landPos[0][1],measured.landPos[0][2],
            measured.land[1],measured.landSpeed[1],measured.landPos[1][0],measured.landPos[1][1],measured.landPos[1][2]);
    }
    Bot2_ForecastGuardEnd(&guard);
    return !guard.contaminated;
}

// bot_moveGuestTrace 1 (default 0; a diagnostic, guests only): at a guest's
// straight commit the committed plan is rolled out once more from the same
// state and command boundary as the search judged it, and its frames are
// kept; every live frame of that plan is compared with the rollout's frame
// at the same index, and the first that leaves it by more than
// MOVE_GUEST_TRACE_POS units or MOVE_GUEST_TRACE_VEL u/s is printed once
// (GUEST_DIVERGE): live and predicted position, velocity, ground contact,
// command and view, and the nearest other player. Chase D's fatal hops launch
// on the rollout and then leave it in the air; the logs had only the launch
// and the landing (records/2026-09-30-defender-falls-and-route.md).
#define MOVE_GUEST_TRACE_MAX 200
#define MOVE_GUEST_TRACE_POS 4.0f
#define MOVE_GUEST_TRACE_VEL 25.0f
typedef struct {float origin[3],velocity[3],exYaw;int ground,yaw,serverTime,exLastTime,exWalking,exStage;signed char f,r,u;} guestTraceFrame_t;
static struct {int active,start,count,plan,reported,matched,rawTime,rawYaw;signed char rawF,rawR,rawU;guestTraceFrame_t f[MOVE_GUEST_TRACE_MAX];} guestTrace[MAX_CLIENTS];
void Move_GuestRawCommand(int client,const usercmd_t *cmd) {
    if(client<0 || client>=MAX_CLIENTS)return;
    guestTrace[client].rawTime=level.time;guestTrace[client].rawYaw=cmd->angles[YAW];
    guestTrace[client].rawF=cmd->forwardmove;guestTrace[client].rawR=cmd->rightmove;guestTrace[client].rawU=cmd->upmove;
}
static int GuestTraceOn(void) {return trap->Cvar_VariableIntegerValue("bot_moveGuestTrace")!=0;}
// Keeps a light copy of this commit's rollout positions for TeammatePredict
// (only while bot_moveGuestTeammate is on, the default: one more rollout
// of the committed plan at every guest commit, the one the diagnostic
// trace below also uses).
static void GuestPredictRecord(int c,const bot2_movePlan_t *plan) {
    int i;
    guestPredict[c].count=min(parity.result.count,MoveSteps(MOVE_TEAMMATE_MS));
    for(i=0;i<guestPredict[c].count;i++)VectorCopy(parity.frames[i].afterPS.origin,guestPredict[c].pos[i]);
    guestPredict[c].active=guestPredict[c].count>0;guestPredict[c].start=level.time;guestPredict[c].plan=plan->id;
}
static void GuestTraceRecord(gentity_t *ent,pmove_t *move,const bot2_movePlan_t *plan,const bot2_moveExecutor_t *before) {
    int c=ent->s.number,i;spikeMeasure_t measure;float us=0;int traces=0;
    guestTrace[c].active=0;guestPredict[c].active=0;
    if(!MoveGuest(c) || !(GuestTraceOn() || GuestTeammateOn()))return;
    if(MOVE_FIX(BX10)) {
        // bot_moveFixBX10: the commit's rollout starts StraightCommand's stop state fresh, without the
        // path stop, inside a guard scope of its own (as ScopedForecastRun).
        bot2_forecastGuard_t guard;int scoped,pathFree=straightPathFree;
        scoped=Bot2_ForecastGuardBegin(move,&guard);
        straightStopTime=0;straightLandings=0;straightOffPath=0;straightPathFree=1;
        RunCandidate(ent,move,plan,before,before,StraightCommand,1,&measure,&us,&traces);
        straightPathFree=pathFree;
        if(scoped && !Bot2_ForecastGuardEnd(&guard) && guard.contaminated)parity.result.worldChanged=qtrue;
    } else RunCandidate(ent,move,plan,before,before,StraightCommand,1,&measure,&us,&traces);
    if(GuestTeammateOn())GuestPredictRecord(c,plan);
    if(!GuestTraceOn())return;
    guestTrace[c].count=min(parity.result.count,MOVE_GUEST_TRACE_MAX);
    for(i=0;i<guestTrace[c].count;i++) {
        const bot2_forecastFrame_t *f=&parity.frames[i];guestTraceFrame_t *g=&guestTrace[c].f[i];
        VectorCopy(f->afterPS.origin,g->origin);VectorCopy(f->afterPS.velocity,g->velocity);
        g->ground=f->afterPS.groundEntityNum;g->yaw=f->command.angles[YAW];
        g->f=f->command.forwardmove;g->r=f->command.rightmove;g->u=f->command.upmove;
        {
            const bot2_moveExecutor_t *e=(const bot2_moveExecutor_t*)f->afterPolicyExtra;
            g->serverTime=f->serverTime;g->exYaw=e->yaw;g->exLastTime=e->lastTime;g->exWalking=e->walking;g->exStage=e->stage;
        }
    }
    guestTrace[c].active=guestTrace[c].count>0;guestTrace[c].start=level.time;guestTrace[c].plan=plan->id;
    guestTrace[c].reported=0;guestTrace[c].matched=0;
}
static void GuestTraceCompare(int client) {
    const movement_t *m=&movement[client];const gentity_t *ent=&g_entities[client];const playerState_t *ps;
    const guestTraceFrame_t *g;int k,i,other=-1;float dx,dy,dz,dv,best=1e9f;
    if(!guestTrace[client].active || !ent->client)return;
    if(!m->plan.count || m->plan.id!=guestTrace[client].plan) {
        if(!guestTrace[client].reported)Bot2_PrintTelemetry(1,"[MOVE] GUEST_TRACE client=%d level=%d plan=%d end=plan_changed matched=%d\n",
            client,level.time,guestTrace[client].plan,guestTrace[client].matched);
        guestTrace[client].active=0;return;
    }
    // The live state before this frame's Pmove is the rollout's state after
    // frame k-1, k frames after the commit.
    k=(level.time-guestTrace[client].start)/Bot2_GameStepMsec()-1;
    if(k<0)return;
    if(k>=guestTrace[client].count) {
        if(!guestTrace[client].reported)Bot2_PrintTelemetry(1,"[MOVE] GUEST_TRACE client=%d level=%d plan=%d end=horizon matched=%d\n",
            client,level.time,guestTrace[client].plan,guestTrace[client].matched);
        guestTrace[client].active=0;return;
    }
    ps=&ent->client->ps;g=&guestTrace[client].f[k];
    dx=ps->origin[0]-g->origin[0];dy=ps->origin[1]-g->origin[1];dz=ps->origin[2]-g->origin[2];
    dv=sqrtf((ps->velocity[0]-g->velocity[0])*(ps->velocity[0]-g->velocity[0])+(ps->velocity[1]-g->velocity[1])*(ps->velocity[1]-g->velocity[1])+
             (ps->velocity[2]-g->velocity[2])*(ps->velocity[2]-g->velocity[2]));
    if(sqrtf(dx*dx+dy*dy+dz*dz)<=MOVE_GUEST_TRACE_POS && dv<=MOVE_GUEST_TRACE_VEL) {guestTrace[client].matched=k+1;return;}
    for(i=0;i<MAX_CLIENTS;i++) {
        const gentity_t *o=&g_entities[i];float d;
        if(i==client || !o->inuse || !o->client || o->health<=0)continue;
        d=Distance(o->client->ps.origin,ps->origin);
        if(d<best) {best=d;other=i;}
    }
    Bot2_PrintTelemetry(1,"[MOVE] GUEST_DIVERGE client=%d level=%d plan=%d frame=%d of=%d stage=%d hops=%d "
        "live=%.1f,%.1f,%.1f pred=%.1f,%.1f,%.1f dpos=%.1f,%.1f,%.1f live_v=%.1f,%.1f,%.1f pred_v=%.1f,%.1f,%.1f dv=%.1f "
        "live_ground=%d pred_ground=%d last_cmd=%d,%d,%d last_cmd_yaw=%.1f pred_cmd=%d,%d,%d pred_cmd_yaw=%.1f view_yaw=%.1f "
        "prev_pred_cmd=%d,%d,%d prev_pred_yaw=%.1f nearest=%d nearest_dist=%.0f raw_cmd=%d,%d,%d raw_yaw=%.1f raw_age=%d "
        "ex_yaw=%.1f pred_ex_yaw=%.1f ex_last=%d pred_ex_last=%d ex_walking=%d pred_ex_walking=%d ex_stage=%d pred_ex_stage=%d pred_time=%d start=%d\n",
        client,level.time,guestTrace[client].plan,k,guestTrace[client].count,m->executor.stage,m->plan.count,
        ps->origin[0],ps->origin[1],ps->origin[2],g->origin[0],g->origin[1],g->origin[2],dx,dy,dz,
        ps->velocity[0],ps->velocity[1],ps->velocity[2],g->velocity[0],g->velocity[1],g->velocity[2],dv,
        ps->groundEntityNum!=ENTITYNUM_NONE,g->ground!=ENTITYNUM_NONE,
        ent->client->pers.cmd.forwardmove,ent->client->pers.cmd.rightmove,ent->client->pers.cmd.upmove,
        Wrap(SHORT2ANGLE(ent->client->pers.cmd.angles[YAW]+ps->delta_angles[YAW])),
        g->f,g->r,g->u,Wrap(SHORT2ANGLE(g->yaw+ps->delta_angles[YAW])),ps->viewangles[YAW],
        k>0?guestTrace[client].f[k-1].f:0,k>0?guestTrace[client].f[k-1].r:0,k>0?guestTrace[client].f[k-1].u:0,
        k>0?Wrap(SHORT2ANGLE(guestTrace[client].f[k-1].yaw+ps->delta_angles[YAW])):0.0f,other,best,
        guestTrace[client].rawF,guestTrace[client].rawR,guestTrace[client].rawU,
        Wrap(SHORT2ANGLE(guestTrace[client].rawYaw+ps->delta_angles[YAW])),level.time-guestTrace[client].rawTime,
        m->executor.yaw,g->exYaw,m->executor.lastTime,g->exLastTime,m->executor.walking,g->exWalking,m->executor.stage,g->exStage,
        g->serverTime,guestTrace[client].start);
    guestTrace[client].reported=1;guestTrace[client].active=0;
}

// Commit a judged candidate: the plan in progress at its landing is
// complete, the candidate is the plan, and this boundary's command is
// re-issued from it so a landing-frame tap is what the live frame does.
static void StraightCommit(gentity_t *ent,pmove_t *move,const straightResult_t *r,const bot2_moveExecutor_t *before,int landing) {
    movement_t *m=&movement[ent->s.number];
    FinishPlan(ent->s.number);
    spikePlans++;m->plan=r->candidate;m->plan.attempted=1;CommitMeasure(&m->plan,&r->measure);PermitRevoke(ent->s.number,"commit");
    GuestTraceRecord(ent,move,&m->plan,before);
    Reissue(ent,move,before);
    m->plan.tapAtLanding=landing && move->cmd.upmove>0;
}

// ---------------------------------------------------------------------------
// Phase 3 state plans in the straight search. The state search plans strafe
// edges into the path, each a hop learned with the state it needs and the
// state it leaves. When the bot stands at the takeoff of the path's next
// strafe edge, the planned chain's next hop joins the straight search as
// candidates of its own: aimed at the edge's learned landing from where the
// bot stands (the tap at once or after the short run-up), and, when the
// edge after it is a landing-frame tap from that landing, chained as the
// second hop aimed at its landing. They are judged with the search's own
// candidates by the one judge, and the best margin over the walk wins, so a
// planned hop is taken when it beats what the search finds from the live
// state and never blocks it. A planned edge none of whose candidates is
// admitted fails; repeated failures disable it and the path is searched
// again without it. Validation happens only at the takeoff: on the way
// there the walk follows the chord as it follows any straight.
// ---------------------------------------------------------------------------
// The strafe edge the path takes next when the bot stands at its takeoff,
// or -1. k is the edge's path index; k2 the index of the landing-frame edge
// chained from its landing, or -1.
static int PlannedStrafe(const movement_t *m,const playerState_t *ps,int *kOut,int *k2Out) {
    int k,idx=m->executor.pathIndex;const graphEdge_t *e;float dx,dy;
    *kOut=*k2Out=-1;
    if(!StateEnabled() || !m->path.count || idx<0)return -1;
    for(k=idx+1;k<m->path.count && k<=idx+3;k++)if(m->path.edges[k]>=0)break;
    if(k>=m->path.count || !PathStrafe(&m->path,k))return -1;
    e=Graph_Edge(m->path.edges[k]);
    if(!e || Graph_EdgeOff(e))return -1;
    dx=e->takeoff[0]-ps->origin[0];dy=e->takeoff[1]-ps->origin[1];
    if(dx*dx+dy*dy>MOVE_STATE_TAKEOFF*MOVE_STATE_TAKEOFF || fabsf(e->takeoff[2]-ps->origin[2])>2*STEPSIZE)return -1;
    *kOut=k;
    if(k+1<m->path.count && PathStrafe(&m->path,k+1)) {
        const graphEdge_t *n=Graph_Edge(m->path.edges[k+1]);
        if(n && n->tap && !Graph_EdgeOff(n))*k2Out=k+1;
    }
    return m->path.edges[k];
}

// The unit heading from one point to another, or the fallback when they coincide.
static void AimAt(const float *from,const float *at,const float *fallback,float *dir) {
    float dx=at[0]-from[0],dy=at[1]-from[1],len=sqrtf(dx*dx+dy*dy);
    if(len<1) {dir[0]=fallback[0];dir[1]=fallback[1];} else {dir[0]=dx/len;dir[1]=dy/len;}
}

// The complete live root at a candidate boundary: the player state the
// rollouts start from, the executor continuation they inherit, and the
// context that decides what is offered. Every hypothetical arm in one
// search shares exactly this root, so a difference between arms is a
// difference of construction and nothing else.
static void ReplayRoot(gentity_t *ent,int search,int index,int edge,int edge2,int envelope,int landing) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    if(!(ReplayFlags()&1))return;
    Bot2_PrintTelemetry(1,"[REPLAY] kind=root client=%d level=%d gen=%d search=%d path_index=%d planned_edge=%d planned_edge2=%d "
        "envelope=%d landing=%d pos=%.3f,%.3f,%.3f vel=%.3f,%.3f,%.3f speed=%.3f vz=%.3f ground=%d "
        "view_yaw=%.4f ctrl_yaw=%.4f cmd_time=%d pm_flags=%d pm_time=%d jump_held=%d legs_anim=%d torso_anim=%d "
        "force=%d speed_active=%d speed_ms=%d jump_level=%d speed_level=%d gravity=%d ps_speed=%.3f "
        "ex_stage=%d ex_walking=%d ex_launch_time=%d ex_airborne=%d ex_decline=%d ex_permitted=%d ex_last_time=%d ex_braked=%d ex_held=%d "
        "ex_mark=%.3f,%.3f,%.3f plan=%d plan_kind=%s plan_count=%d permit=%d spend=%d step_ms=%d goal_distance=%.3f\n",
        ent->s.number,level.time,m->path.generation,search,index,edge,edge2,envelope,landing,
        ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],Speed2D(ps),ps->velocity[2],
        ps->groundEntityNum,ps->viewangles[YAW],m->executorBefore.yaw,ps->commandTime,ps->pm_flags,ps->pm_time,
        (ps->pm_flags&PMF_JUMP_HELD)?1:0,ps->legsAnim,ps->torsoAnim,
        ps->fd.forcePower,(ps->fd.forcePowersActive&(1<<FP_SPEED))?1:0,max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time),
        ps->fd.forcePowerLevel[FP_LEVITATION],ps->fd.forcePowerLevel[FP_SPEED],ps->gravity,ps->speed,
        m->executorBefore.stage,m->executorBefore.walking,m->executorBefore.launchTime,m->executorBefore.airborne,
        m->executorBefore.decline,m->executorBefore.permitted,m->executorBefore.lastTime,m->executorBefore.braked,m->executorBefore.held,
        m->executorBefore.mark[0],m->executorBefore.mark[1],m->executorBefore.mark[2],
        m->plan.id,PlanKind(m->plan.kind),m->plan.count,m->permit.active,m->budget.spend,Bot2_GameStepMsec(),
        GoalDistance(ps,m->objective));
}

// One [REPLAY] record per judged candidate: how its heading was built,
// what the rollout measured and whether it was committed. Printed after
// the verdict so the committed candidate is named in its own record.
static void ReplayCandidate(int client,const straightResult_t *r,int search,int index,int committed,const float *ref) {
    const bot2_movePlan_t *c=&r->candidate;float refYaw=atan2f(ref[1],ref[0])*180/M_PI;
    const spikeMeasure_t *s=&r->measure;
    if(!(ReplayFlags()&1))return;
    Bot2_PrintTelemetry(1,"[REPLAY] kind=cand client=%d level=%d gen=%d search=%d cand=%d variant=%s plan_kind=%s hops=%d "
        "aim_source=%d edge=%d edge2=%d runup0=%g runup1=%g hop0_yaw=%.4f hop1_yaw=%.4f rel0_yaw=%.4f ref_yaw=%.4f "
        "outcome=%s committed=%d margin=%.3f walk=%.3f walk_frame=%d walk_stopped=%d walk_ref=%s role=%s offer=%d walk_ref_frames=%d frames=%d unsupported=%u done=%d progress=%.3f "
        "rerouted=%d route_cells=%d route_ms=%d tap_onward=%d tap_land=%d tap_cell=%d tap_speed=%.3f "
        "end_stage=%d onward_max=%.3f brake_frames=%d held_frames=%d onward_travel=%.3f onward_progress=%.3f onward_frames=%d "
        "launch0=%d l0_speed=%.3f l0_err=%.3f l0_pos=%.3f,%.3f,%.3f land0=%d a0_speed=%.3f a0_err=%.3f a0_drop=%.3f a0_cell=%d a0_pos=%.3f,%.3f,%.3f "
        "launch1=%d l1_speed=%.3f l1_err=%.3f land1=%d a1_speed=%.3f a1_err=%.3f a1_drop=%.3f a1_cell=%d a1_pos=%.3f,%.3f,%.3f\n",
        client,level.time,movement[client].path.generation,search,index,r->variant?"b_measured_policy":"a_control",
        PlanKind(c->kind),c->count,-1,c->edge,c->edge2,c->hop[0].runup,c->hop[1].runup,
        atan2f(c->hop[0].dir[1],c->hop[0].dir[0])*180/M_PI,atan2f(c->hop[1].dir[1],c->hop[1].dir[0])*180/M_PI,
        Wrap(atan2f(c->hop[0].dir[1],c->hop[0].dir[0])*180/M_PI-refYaw),refYaw,
        r->outcome?r->outcome:"unjudged",committed,r->margin,r->walk,r->walkFrame,r->walkStopped,
        r->rerouted?"extension":r->variant?"replay":"control",
        r->variant?"observe_only":"proposal",0,r->walkRefFrames,
        s->frames,s->unsupported,s->done,s->progress,
        r->rerouted,r->routeCells,r->routeMs,0,-1,-1,0.0f,
        s->endStage,s->onwardMax,s->brakeFrames,s->heldFrames,
        s->onwardTravel,s->onwardProgress,s->onwardFrames,
        s->launch[0],s->launchSpeed[0],s->launchError[0],s->launchPos[0][0],s->launchPos[0][1],s->launchPos[0][2],
        s->land[0],s->landSpeed[0],s->landError[0],s->landDrop[0],s->landCell[0],s->landPos[0][0],s->landPos[0][1],s->landPos[0][2],
        s->launch[1],s->launchSpeed[1],s->launchError[1],
        s->land[1],s->landSpeed[1],s->landError[1],s->landDrop[1],s->landCell[1],s->landPos[1][0],s->landPos[1][1],s->landPos[1][2]);
}

// Why the straight search returned before it generated a candidate, when it
// did so on a landing frame. Recorded here and printed by the caller once the
// search has returned, because printing at the exits themselves changes the
// run it is reporting on: 22,700 ms against the arm's 20,500-20,725,
// reproducibly. The mechanism is unexplained - the rollouts are bounded by
// step counts, not by a clock - so the rule is empirical: measure first with
// the print outside the search, and treat a print inside it as a change to
// the bot.
static const char *straightExit;

static int SearchCacheTake(gentity_t *ent,pmove_t *move,straightResult_t *results,int originals,
                           int *candidates,int *stateCandidates,
                           int *counts,float *us,int *traces,straightVerdict_t *v,
                           const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after,
                           int index,const float *ref);
static int SearchCacheValidate(gentity_t *ent,pmove_t *move,straightResult_t *r,
                               const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after,
                               float rootGoal,int rootSpeedMs);

// `pre` non-NULL is a pre-search: the search runs from an installed predicted
// boundary state, may suspend between candidates, and stops once it has a
// judged candidate set instead of committing anything. Every caller of the
// live search passes NULL and gets exactly the function that was measured.
static void StraightSearchCore(gentity_t *ent,pmove_t *move,searchCache_t *pre) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    static straightResult_t liveResults[MOVE_SEARCH_SLOTS];
    int originals=0;
    straightResult_t *results=pre?pre->results:liveResults;
    int resume=pre && pre->stage==PRE_JUDGING,cached=0,cachedBest=-1,cacheStale=0,rescue=0;
    judgeState_t liveJudge,*js=pre?&pre->js:&liveJudge;
    bot2_moveExecutor_t before,after;straightVerdict_t v;graphState_t live;
    vec3_t end,next;float ref[2],length,us=0,ahead=0;
    int index=m->executor.pathIndex,far,a,b,h,i,candidates=0,traces=0,edge,k=-1,k2=-1,stateCandidates=0,stateAdmitted=0,envelope=0;
    int landing=StraightLanding(m),straight;
    int counts[CANDIDATE_OUTCOMES];char detail[768];int selectForced=0;
    char tapeDetail[256];
    tapeDetail[0]=0;
    RerouteMemoClear(ent->s.number);
    memset(&liveJudge,0,sizeof(liveJudge));
    memset(counts,0,sizeof(counts));
    memset(&v,0,sizeof(v));v.best=-1;v.walkFrame=-1;
    // The library's own bookkeeping belongs to the frame that decides, not to
    // a proposal rolled out ahead of it.
    if(!pre)TapeSync();
    m->straight.time=level.time;
    // The planned chain's next hop, when the bot is at its takeoff, is
    // validated whatever the straight ahead; the search's own candidates
    // need a straight long enough to hop along.
    edge=PlannedStrafe(m,ps,&k,&k2);
    far=StraightAhead(&m->path,index);
    // The search's own silent exits. Each one leaves a landing frame with no
    // candidate at all, which is the frame that decides whether the next tap
    // pays ground friction, so each names itself.
    if(far<0 && edge<0) {if(landing)straightExit="no_straight_ahead";return;}
    if(far>=0) {
        CellPoint(m->path.cells[far],end);
        ref[0]=end[0]-ps->origin[0];ref[1]=end[1]-ps->origin[1];length=sqrtf(ref[0]*ref[0]+ref[1]*ref[1]);
    } else {length=0;ref[0]=1;ref[1]=0;}
    // At a flight landing the nearby chord endpoint may precede a bend.
    // Offer arc-aimed continuations there; the native judge still validates
    // the complete maneuver and onward motion before it can commit.
    straight=far>=0 && (length>=MOVE_STRAIGHT_MIN || landing) && GoalDistance(ps,m->objective)>=MOVE_STRAIGHT_GOAL;
    if(!straight && edge<0) {
        if(landing)straightExit=far<0?"no_straight_ahead":length<MOVE_STRAIGHT_MIN?"straight_too_short":"goal_too_near";
        return;
    }
    if(length>=1) {ref[0]/=length;ref[1]/=length;}
    else if(edge>=0) {ref[0]=Graph_Edge(edge)->dir[0];ref[1]=Graph_Edge(edge)->dir[1];}
    // The bot's arc position past the progress cell, so aim points are
    // measured from where it stands.
    CellPoint(m->path.cells[index],end);
    if(index+1<m->path.count) {
        float dx,dy,len;
        CellPoint(m->path.cells[index+1],next);
        dx=next[0]-end[0];dy=next[1]-end[1];len=sqrtf(dx*dx+dy*dy);
        if(len>0)ahead=max(0,((ps->origin[0]-end[0])*dx+(ps->origin[1]-end[1])*dy)/len);
    }
    // A refusal inside the gap lookahead, when it ran at this boundary, is
    // the gap search's, unless it lies along a planned chord, which the
    // gap search leaves to the validation here.
    if(m->gap.lookaheadTime==level.time && lookaheadResult.status==BOT2_FORECAST_COMPLETE &&
       lookaheadResult.completion==BOT2_FORECAST_COMPLETION_POLICY &&
       ((const bot2_moveExecutor_t*)lookaheadResult.terminalPolicyExtra)->decline==MOVE_DECLINE_CORRIDOR && !LookaheadOnChord(m)) {
        if(landing)straightExit="gap_corridor_refusal";
        return;
    }
    if(!resume)m->straight.searches++;
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    if(!resume && straight)for(h=0;h<(int)ARRAY_LEN(straightHops);h++)
    for(a=0;a<(int)ARRAY_LEN(straightAims);a++)for(b=0;b<(int)ARRAY_LEN(straightRunups);b++,candidates++) {
        bot2_movePlan_t *candidate=&results[candidates].candidate;
        float v0,d0,d1;vec3_t aim0,aim1;
        StraightResultReset(&results[candidates],0);
        candidate->count=straightHops[h];candidate->kind=MOVE_PLAN_STRAIGHT;candidate->edge=-1;candidate->edge2=-1;candidate->id=spikePlans+1;VectorCopy(ps->origin,candidate->origin);
        // The first hop launches at the speed the run-up leaves: the live
        // speed when tapped at once, the walk's when a run-up sheds it.
        v0=straightRunups[b]>0 && !runupKeep?Wishspeed(ps):max(Wishspeed(ps),Speed2D(ps));
        d0=(MOVE_HOP_SLOPE*v0+MOVE_HOP_OFFSET)*straightAims[a];
        d1=MOVE_HOP_SLOPE*(v0+MOVE_HOP_LAND_GAIN)+MOVE_HOP_OFFSET;
        PathPointAt(&m->path,index,ahead+straightRunups[b]+d0,aim0);
        PathPointAt(&m->path,index,ahead+straightRunups[b]+d0+d1,aim1);
        AimAt(ps->origin,aim0,ref,candidate->hop[0].dir);candidate->hop[0].runup=straightRunups[b];
        AimAt(aim0,aim1,candidate->hop[0].dir,candidate->hop[1].dir);candidate->hop[1].runup=0;
        candidate->anchor1[0]=aim0[0];candidate->anchor1[1]=aim0[1];candidate->anchored1=1;
    }
    // Circle run-ups (bot_moveCircleRunup), from at or below ground speed
    // only: above it a long run-up sheds speed to friction.
    if(!resume && straight && circleRunup && Speed2D(ps)<=Wishspeed(ps)+25)
    for(h=0;h<(int)ARRAY_LEN(straightHops);h++)
    for(a=0;a<(int)ARRAY_LEN(straightAims);a++)for(b=0;b<(int)ARRAY_LEN(circleRunups);b++,candidates++) {
        bot2_movePlan_t *candidate=&results[candidates].candidate;
        float v0,d0,d1;vec3_t aim0,aim1;
        StraightResultReset(&results[candidates],0);
        candidate->count=straightHops[h];candidate->kind=MOVE_PLAN_STRAIGHT;candidate->edge=-1;candidate->edge2=-1;candidate->id=spikePlans+1;VectorCopy(ps->origin,candidate->origin);
        v0=Wishspeed(ps)+50;
        d0=(MOVE_HOP_SLOPE*v0+MOVE_HOP_OFFSET)*straightAims[a];
        d1=MOVE_HOP_SLOPE*(v0+MOVE_HOP_LAND_GAIN)+MOVE_HOP_OFFSET;
        PathPointAt(&m->path,index,ahead+circleRunups[b]*circleScale+d0,aim0);
        PathPointAt(&m->path,index,ahead+circleRunups[b]*circleScale+d0+d1,aim1);
        AimAt(ps->origin,aim0,ref,candidate->hop[0].dir);candidate->hop[0].runup=(int)(circleRunups[b]*circleScale);
        AimAt(aim0,aim1,candidate->hop[0].dir,candidate->hop[1].dir);candidate->hop[1].runup=0;
        candidate->anchor1[0]=aim0[0];candidate->anchor1[1]=aim0[1];candidate->anchored1=1;
    }
    // The planned chain's next hop, when the bot is at its takeoff. Two
    // constructions of the same chain are offered when the replay arm is
    // armed, differing in the heading alone: the control aims from where
    // the bot actually stands at the edge's stored landing, while the
    // measured-policy arm rides the heading the edge was learned with
    // (graphEdge_t.dir, the hop policy, not the velocity heading). Same
    // root, hop counts, run-ups, first-frame semantics, inputs, spend
    // policy, time step, walk reference, admission, post-landing hold
    // and ordering; only the heading differs, and arm B never commits.
    if(edge>=0) {
        const graphEdge_t *e=Graph_Edge(edge),*e2=k2>=0?Graph_Edge(m->path.edges[k2]):NULL;
        int arms=(ReplayFlags()&2)?2:1,arm;
        LiveState(m,ps,&live);live.tap=landing;
        envelope=Graph_StrafeAdmits(edge,m->path.cells[index],&live);
        if(!resume) {m->state.searches++;m->state.edge=edge;
        ReplayRoot(ent,m->straight.searches,index,edge,k2>=0?m->path.edges[k2]:-1,envelope,landing);}
        if(!resume)for(arm=0;arm<arms;arm++)for(h=e2?0:1;h<2;h++)for(b=0;b<(int)ARRAY_LEN(straightRunups);b++,candidates++,stateCandidates++) {
            bot2_movePlan_t *candidate=&results[candidates].candidate;
            StraightResultReset(&results[candidates],arm);
            candidate->count=h?1:2;candidate->kind=MOVE_PLAN_STATE;candidate->edge=edge;candidate->edge2=h?-1:m->path.edges[k2];
            candidate->id=spikePlans+1;VectorCopy(ps->origin,candidate->origin);
            if(arm) {
                candidate->hop[0].dir[0]=e->dir[0];candidate->hop[0].dir[1]=e->dir[1];
                if(e2) {candidate->hop[1].dir[0]=e2->dir[0];candidate->hop[1].dir[1]=e2->dir[1];}
            } else {
                AimAt(ps->origin,e->landing,e->dir,candidate->hop[0].dir);
                if(e2)AimAt(e->landing,e2->landing,e2->dir,candidate->hop[1].dir);
            }
            candidate->hop[0].runup=straightRunups[b];
            if(!e2) {candidate->hop[1].dir[0]=candidate->hop[0].dir[0];candidate->hop[1].dir[1]=candidate->hop[0].dir[1];}
            else {candidate->anchor1[0]=e->landing[0];candidate->anchor1[1]=e->landing[1];candidate->anchored1=1;}
            candidate->hop[1].runup=0;
        }
    }
    if(resume) {
        candidates=pre->candidates;originals=pre->originals;
        stateCandidates=pre->stateCandidates;us=pre->us;traces=pre->traces;v=pre->v;
        memcpy(counts,pre->counts,sizeof(counts));
    } else {
        originals=candidates;
        // A pre-search that reaches here has a candidate set; a live search
        // may take it over at the boundary it was rooted at.
        if(!pre && SearchCacheTake(ent,move,results,originals,&candidates,&stateCandidates,
                                   counts,&us,&traces,&v,&before,&after,index,ref)) {cached=1;cachedBest=v.best;}
        if(!pre && walkDeferTake) {
            // A deferred walking search: taken, rescued, or deferred anew from the next frame; never searched whole here.
            const searchCache_t *dc=&searchCache[ent->s.number];
            walkDeferTake=0;
            if(cached)searchTally[ent->s.number].walkTaken++;
            else if(takeRefusal && WalkRescue() && dc->v.best<0 && dc->v.best!=-2) {
                // The deferred search found no hop: none now either, until the cadence's next search.
                searchTally[ent->s.number].walkRescueNone++;
                Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=straight reason=deferred_none");
                return;
            } else if(takeRefusal && WalkRescue() && dc->v.best>=0 && dc->v.best<originals && dc->originals==originals) {
                static straightResult_t rescueResults[1];
                rescueResults[0]=results[dc->v.best];
                results=rescueResults;
                stateCandidates=rescueResults[0].candidate.kind==MOVE_PLAN_STATE;
                candidates=originals=1;rescue=1;
                searchTally[ent->s.number].walkRescueTried++;
            } else {searchTally[ent->s.number].walkRefused++;m->straight.time=0;return;}
        }
        if(pre) {
            pre->candidates=candidates;pre->originals=originals;
            pre->stateCandidates=stateCandidates;pre->stage=PRE_JUDGING;
            for(i=0;i<candidates;i++)pre->built[i]=results[i].candidate;
            memset(&pre->js,0,sizeof(pre->js));
        }
    }
    if(!cached) {
        if(!StraightJudge(ent,move,results,candidates,index,&before,&after,ref,counts,&us,&traces,&v,js)) {
            // Out of this frame's rollouts. The judged candidates so far stay
            // in this client's own storage and the next frame continues.
            pre->us=us;pre->traces=traces;pre->v=v;memcpy(pre->counts,counts,sizeof(counts));
            return;
        }
        if(v.best!=-2 && !AuditOffPath(ent,move,results,candidates,&before,&after))v.best=-2;
        if(rescue && v.best>=0)searchTally[ent->s.number].walkRescueCommitted++;
    }
    if(pre) {
        pre->us=us;pre->traces=traces;pre->v=v;memcpy(pre->counts,counts,sizeof(counts));
        pre->stage=PRE_READY;
        return;
    }
    // The control arm alone decides whether the planned edge stood: the
    // replay arm is a measurement rather than a proposal.
    for(i=0;i<candidates;i++)if(results[i].candidate.kind==MOVE_PLAN_STATE && !results[i].variant &&
        results[i].outcome && StraightAdmitted(results[i].outcome))stateAdmitted++;
    if(v.best==-2) {
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none","kind=straight reason=guard_busy");
        m->straight.searchTime=level.time+MOVE_GAP_RETRY;return;
    }
    if(edge>=0 && stateAdmitted) {m->state.staleEdge=-1;m->state.staleCount=0;}
    // A rescue judged only the deferred winner, so the edge's own candidates were not judged and it is not charged.
    if(edge>=0 && !stateAdmitted && !rescue) {
        const graphEdge_t *e=Graph_Edge(edge);int rootTrigger=stateCandidates>0;
        const char *detail=va("index=%d tap=%d envelope=%d landing=%d live_speed=%.1f entry_speed=%.1f candidates=%d",
            edge,e->tap,envelope,landing,Speed2D(ps),e->entrySpeed,stateCandidates);
        for(i=0;i<candidates;i++)if(results[i].candidate.kind==MOVE_PLAN_STATE && !results[i].variant &&
            !(results[i].outcome && !strcmp(results[i].outcome,"refused") && results[i].measure.unsupported==BOT2_FORECAST_TRIGGER && results[i].measure.frames<=2))rootTrigger=0;
        if(rootTrigger) {
            // The root itself is inside a trigger (a stand start inside its
            // flag's pickup box): nothing was judged. The edge stands and is
            // validated again on the cadence, once the walk has left the box.
            Event(ent->s.number,MOVE_EVENT_EDGE,"strafe","deferred",detail);
        } else if(!envelope) {
            // The live state is outside the edge's entry envelope: the bot
            // walked to a takeoff the plan expected to land on. The plan is
            // stale, not the edge; the path is searched again from here.
            if(m->state.staleEdge==edge)m->state.staleCount++;
            else {m->state.staleEdge=edge;m->state.staleCount=1;}
            Event(ent->s.number,MOVE_EVENT_EDGE,"strafe","stale",va("%s stale_count=%d",detail,m->state.staleCount));
            m->path.replan=1;m->path.retryTime=0;
        } else {
            // The planned edge failed from a state inside its envelope: the
            // chord it heads for is a flight the bot will not fly, so the
            // edge is avoided for the objective and the path searched again.
            Event(ent->s.number,MOVE_EVENT_EDGE,"strafe","refused",detail);
            StateAvoid(ent->s.number,edge,"refused");
        }
    }
    if(v.best>=0 && !results[v.best].rerouted && stateAdmitted && StatePreferred() &&
       results[v.best].candidate.kind!=MOVE_PLAN_STATE) {
        // The chain's own hop over the search's, by its margin among the
        // planned candidates.
        int best=-1;
        for(i=0;i<candidates;i++)if(results[i].candidate.kind==MOVE_PLAN_STATE && !results[i].variant && results[i].outcome && StraightAdmitted(results[i].outcome) &&
            (best<0 || results[i].margin>results[best].margin))best=i;
        if(best>=0) {
            v.best=best;v.margin=results[best].margin;v.walk=results[best].walk;
            v.walkFrame=results[best].walkFrame;v.walkStopped=results[best].walkStopped;
        }
    }
    // A rerouted winner's route is planned again from its landing at the
    // commit, and the candidate is dropped when that plan is gone: a flight
    // off the path is only admissible with the route it was admitted for.
    // This runs after the last thing that can change the winner, so the
    // installed route is the winner's own: built before StatePreferred, it
    // could be the previous winner's, or an earlier search's entirely.
    // The diagnostic override runs after StatePreferred, the last thing that
    // can change the winner, and before the route is installed, so the forced
    // candidate keeps its own route and every later check. It can only select
    // a candidate the judge already admitted.
    if(SelectOverrideNth() && !MoveGuest(ent->s.number) && m->commits+1==SelectOverrideNth()) {
        int forced=SelectOverrideCand();
        selectForced=-1;
        if(forced>=0 && forced<candidates && results[forced].outcome &&
           StraightAdmitted(results[forced].outcome)) {
            v.best=forced;v.margin=results[forced].margin;v.walk=results[forced].walk;
            v.walkFrame=results[forced].walkFrame;v.walkStopped=results[forced].walkStopped;
            v.score=results[forced].score;selectForced=1;
        }
    }
    // The decision tape, after the last thing that can change the winner and
    // before the route is installed, exactly where the forced-candidate
    // override runs and under the same rule: it can only select a candidate
    // the judge admitted. What it adds is the entry, so a decision that could
    // not be taken is named rather than silently replaced by the ranking's.
    tapeUndo.saved=0;
    if(decisionTape.count && candidates>0 && !MoveGuest(ent->s.number)) {
        int slot=TapeEntryNear(ps->origin);
        if(slot>=0 && cached) {memcpy(tapeUndo.bytes,&decisionTape,sizeof(decisionTape));tapeUndo.saved=1;}
        if(slot>=0) {
            const moveTapeEntry_t *e=&decisionTape.entry[slot];
            int match=-1,refusedMatch=-1,nearest=-1,matches=0;
            float nearestGap=0,matchGap=0,matchTie=0,value,gap;
            char aim[24],runup[16];
            for(i=0;i<candidates;i++) {
                if(!results[i].outcome)continue;
                TapeHop(&results[i].candidate,0,ref,aim,sizeof(aim),runup,sizeof(runup),&value);
                gap=fabsf(Wrap(value-e->aimValue));
                if(TapeMatches(&results[i].candidate,results[i].measure.landCell,e,ref)) {
                    float tie=gap;
                    if(!StraightAdmitted(results[i].outcome)) {
                        if(refusedMatch<0)refusedMatch=i;
                        continue;
                    }
                    matches++;
                    // Two admitted hops of the same shape landing in the same
                    // cells are the same decision by the identity rule, and
                    // the c75 campaign measured that the rule groups branches
                    // the trip does not: five candidates it calls one decision
                    // completed in five times spanning 1,625 ms. The judge's
                    // own rank does not order them - and, being a score, it is
                    // not stable between the explorer's branch world and a
                    // fresh tape-only one, which is why two of the four tapes
                    // did not reproduce. So the tie is broken first by the
                    // printed-aim gap, the same quantity the census reports:
                    // the aim stays out of the match criterion and is used
                    // only to pick the nearest of several equal matches, which
                    // is a property of the candidate and not of the ranking.
                    // The judge's rank decides only an exact tie on that gap.
                    if(e->hops>1 && results[i].candidate.count>1) {
                        char aim2[24],runup2[16];float value2;
                        TapeHop(&results[i].candidate,1,ref,aim2,sizeof(aim2),runup2,sizeof(runup2),&value2);
                        tie+=fabsf(Wrap(value2-e->aimValue2));
                    }
                    if(match<0 || tie<matchTie-MOVE_TAPE_AIM_TIE ||
                       (tie<=matchTie+MOVE_TAPE_AIM_TIE && results[i].score>results[match].score)) {
                        match=i;matchGap=gap;matchTie=tie;
                    }
                    continue;
                }
                if(nearest<0 || gap<nearestGap) {nearest=i;nearestGap=gap;}
            }
            decisionTape.entry[slot].attempts++;
            decisionTape.entry[slot].level=level.time;
            if(match>=0) {
                int j;
                v.best=match;v.margin=results[match].margin;v.walk=results[match].walk;
                v.walkFrame=results[match].walkFrame;v.walkStopped=results[match].walkStopped;
                v.score=results[match].score;
                // Only a hit establishes position on the tape, so only a hit
                // moves the cursor. Entries the hit passed over are answered
                // now, once each: the bot went by their roots and took another
                // decision instead.
                for(j=decisionTape.cursor;j<slot;j++)if(!decisionTape.entry[j].status) {
                    decisionTape.entry[j].status=2;decisionTape.absentMisses++;
                    Q_strncpyz(decisionTape.entry[j].why,"jumped",sizeof(decisionTape.entry[j].why));
                }
                decisionTape.cursor=slot+1;
                if(decisionTape.entry[slot].status!=3)decisionTape.hits++;
                decisionTape.entry[slot].status=3;decisionTape.entry[slot].matches=matches;
                decisionTape.entry[slot].gap=matchGap;
                Q_strncpyz(decisionTape.entry[slot].why,results[match].outcome,
                           sizeof(decisionTape.entry[slot].why));
                Com_sprintf(tapeDetail,sizeof(tapeDetail),
                    "entry=%d kind=%s outcome=hit cand=%d matches=%d aim_gap=%.4f",
                    slot,PlanKind(e->kind),match,matches,matchGap);
            } else if(v.best<0) {
                // No decision is taken here: the search admitted nothing and
                // the bot walks on. An entry is answered by a decision, not by
                // passing near its root. Measured, not assumed: the first
                // matching self-control consumed entries 1 and 6 at searches
                // that committed nothing and then committed those very
                // decisions 125 and 100 ms later, at the tape's own roots.
                decisionTape.deferred++;
            } else if(refusedMatch>=0) {
                // The cursor stays: refused on this frame is not proof the bot
                // has passed the site, and two frames on it may be admitted
                // from a slightly different state. That is the closed-loop
                // behaviour the diagnostic exists to observe.
                if(decisionTape.entry[slot].status!=1)decisionTape.refusedMisses++;
                decisionTape.entry[slot].status=1;
                Q_strncpyz(decisionTape.entry[slot].why,results[refusedMatch].outcome,
                           sizeof(decisionTape.entry[slot].why));
                Com_sprintf(tapeDetail,sizeof(tapeDetail),
                    "entry=%d kind=%s outcome=refused cand=%d reason=%s",
                    slot,PlanKind(e->kind),refusedMatch,results[refusedMatch].outcome);
            } else {
                if(!decisionTape.entry[slot].status)decisionTape.absentMisses++;
                if(decisionTape.entry[slot].status!=1)decisionTape.entry[slot].status=2;
                decisionTape.entry[slot].gap=nearest>=0?nearestGap:-1.0f;
                Q_strncpyz(decisionTape.entry[slot].why,
                           nearest>=0?PlanKind(results[nearest].candidate.kind):"none",
                           sizeof(decisionTape.entry[slot].why));
                Com_sprintf(tapeDetail,sizeof(tapeDetail),
                    "entry=%d kind=%s outcome=absent near_cand=%d near_kind=%s aim_gap=%.4f",
                    slot,PlanKind(e->kind),nearest,
                    nearest>=0?PlanKind(results[nearest].candidate.kind):"none",
                    nearest>=0?nearestGap:-1.0f);
            }
            if(tapeDetail[0])Q_strcat(tapeDetail,sizeof(tapeDetail),
                va(" attempts=%d candidates=%d cursor=%d hits=%d refused=%d absent=%d deferred=%d",
                   decisionTape.entry[slot].attempts,candidates,decisionTape.cursor,
                   decisionTape.hits,decisionTape.refusedMisses,
                   decisionTape.absentMisses,decisionTape.deferred));
        }
    }
    // The take validated the cached set's own winner only. StatePreferred, the
    // forced override or the tape may since have chosen another of its
    // candidates, whose flight the world may have entered after the
    // pre-search: that one is validated too before it is committed. A refusal
    // undoes what the tape recorded for the selection and hands the boundary
    // to a live search on the next frame, past the lookahead cadence too.
    if(cached && v.best>=0 && v.best!=cachedBest) {
        int rootSpeedMs=(ps->fd.forcePowersActive&(1<<FP_SPEED))?max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time):0;
        straightRootIndex=index;   // the validation's off-path test and reroute joins read it; the last judge set it
        if(!SearchCacheValidate(ent,move,&results[v.best],&before,&after,GoalDistance(ps,m->objective),rootSpeedMs)) {
            Event(ent->s.number,MOVE_EVENT_PLAN,"none","cache_stale",
                  va("cand=%d cached_best=%d field=%s tape_restored=%d",v.best,cachedBest,searchStateField,tapeUndo.saved));
            if(tapeUndo.saved) {memcpy(&decisionTape,tapeUndo.bytes,sizeof(decisionTape));tapeDetail[0]=0;}
            v.best=-1;cacheStale=1;
        }
    }
    rerouteInstallCand=-1;
    if(v.best>=0 && results[v.best].rerouted && results[v.best].routeCells>0) {
        int rootSpeedMs=(ps->fd.forcePowersActive&(1<<FP_SPEED))?max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time):0;
        if(!RerouteFrom(ent->s.number,&results[v.best],rootSpeedMs,&rerouteInstall)) {
            Event(ent->s.number,MOVE_EVENT_PLAN,"reroute","no_route",va("cand=%d route_ms=%d",v.best,results[v.best].routeMs));
            results[v.best].outcome="no_route";v.best=-1;
        } else rerouteInstallCand=v.best;
    }
    for(i=0;i<candidates;i++)ReplayCandidate(ent->s.number,&results[i],m->straight.searches,i,i==v.best,ref);
    // The winner needs a route and the built one is not its own: the build
    // above covers every path to a commit, so this cannot happen. It is
    // checked anyway, because installing another candidate's route is
    // silent - the bot follows a path from a landing it never makes.
    if(v.best>=0 && results[v.best].rerouted && results[v.best].routeCells>0 && rerouteInstallCand!=v.best) {
        Event(ent->s.number,MOVE_EVENT_PLAN,"reroute","mismatch",va("cand=%d built=%d",v.best,rerouteInstallCand));
        results[v.best].outcome="no_route";v.best=-1;
    }
    if(v.best>=0) {
        const straightResult_t *r=&results[v.best];
        // The route the landing opened becomes the path before the commit,
        // so the re-issued frame reads the path the flight ends on; the
        // progress index is set after it, because the commit restores the
        // executor this frame started with.
        if(r->rerouted && r->routeCells>0)RerouteInstall(ent->s.number,&rerouteInstall);
        StraightCommit(ent,move,r,&before,landing);
        if(r->rerouted && r->routeCells>0)m->executor.pathIndex=0;
        if(m->plan.kind==MOVE_PLAN_STATE)m->state.plans++;else m->straight.plans++;
        m->commits++;
        Com_sprintf(detail,sizeof(detail),"kind=%s id=%d hops=%d landing=%d reissued_up=%d arrives=%d candidates=%d admitted=%d frames=%d walk_frame=%d walk_stopped=%d walk_frames=%d d0=%g r0=%g d1=%g r1=%g c0=%d c1=%d chord=%.1f far=%d progress=%.1f walk=%.1f launch_speed=%.1f land_speed=%.1f launch1_speed=%.1f land1_speed=%.1f rollout_ms=%.1f traces=%d index=%d index2=%d state_candidates=%d state_admitted=%d envelope=%d",
            PlanKind(m->plan.kind),m->plan.id,m->plan.count,landing,move->cmd.upmove>0,!strcmp(r->outcome,"arrives"),candidates,v.admitted,r->measure.done,v.walkFrame,v.walkStopped,v.walkFrames,
            Wrap(atan2f(m->plan.hop[0].dir[1],m->plan.hop[0].dir[0])*180/M_PI-atan2f(ref[1],ref[0])*180/M_PI),m->plan.hop[0].runup,
            // The second hop is half the decision. Every other kind's commit
            // detail and the per-candidate [SPIKE] line already print it; this
            // one did not, and two candidates for one planned edge pair can
            // share every first-hop field and fly different chains.
            m->plan.count>1?Wrap(atan2f(m->plan.hop[1].dir[1],m->plan.hop[1].dir[0])*180/M_PI-atan2f(ref[1],ref[0])*180/M_PI):0,
            m->plan.count>1?m->plan.hop[1].runup:0,
            // Where each hop lands, as a graph cell. Discrete, and the map's
            // rather than the run's, so it names the same decision from a
            // different start where an aim derived from the live root cannot.
            r->measure.landCell[0],m->plan.count>1?r->measure.landCell[1]:-1,
            length,far,
            r->measure.progress,v.walk,r->measure.launchSpeed[0],r->measure.landSpeed[0],r->measure.launchSpeed[1],r->measure.landSpeed[1],us/1000,traces,
            m->plan.edge,m->plan.edge2,stateCandidates,stateAdmitted,envelope);
        Q_strcat(detail,sizeof(detail),va(" rerouted=%d route_cells=%d route_ms=%d",r->rerouted,r->routeCells,r->routeMs));
        Q_strcat(detail,sizeof(detail),va(" exit_ms=%d exit_credit=%.1f score=%.1f margin=%.1f stale_slots=%d",
            ExitSpeedMs(),ExitCredit(r,ExitSpeedMs()),r->score,r->margin,straightStaleSlots));
        Q_strcat(detail,sizeof(detail),va(" commit_nth=%d select_forced=%d",m->commits,selectForced));
        Q_strcat(detail,sizeof(detail),va(" teammate=%d",counts[CANDIDATE_OUTCOMES-1]));
        Event(ent->s.number,MOVE_EVENT_PLAN,"hop0","sequence",detail);
    } else {
        m->straight.searchTime=cacheStale?level.time:level.time+MOVE_GAP_RETRY;
        if(cacheStale)m->straight.time=0;
        Com_sprintf(detail,sizeof(detail),"kind=straight candidates=%d refused=%d fell=%d miss=%d no_first=%d no_second=%d no_onward=%d horizon=%d no_progress=%d off_graph=%d off_path=%d slower=%d unjudged=%d arrives=%d chain_entry=%d rerouted=%d no_route=%d slower_route=%d tap_onward=%d tap_refused=%d chord=%.1f far=%d walk_frames=%d rollout_ms=%.1f state_candidates=%d",
            candidates,counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8],counts[9],counts[10],counts[11],counts[12],counts[13],counts[14],counts[15],counts[16],counts[17],counts[18],length,far,v.walkFrames,us/1000,stateCandidates);
        Q_strcat(detail,sizeof(detail),va(" stale_slots=%d teammate=%d",straightStaleSlots,counts[CANDIDATE_OUTCOMES-1]));
        Event(ent->s.number,MOVE_EVENT_PLAN,"none","none",detail);
    }
    // After the search returns, never inside it: printing in a rollout budget
    // has cost seconds before.
    if(tapeDetail[0])Event(ent->s.number,MOVE_EVENT_PLAN,"none","tape",tapeDetail);
}

static void StraightSearch(gentity_t *ent,pmove_t *move) {StraightSearchCore(ent,move,NULL);}

// ---------------------------------------------------------------------------
// The pre-search: taking a search that was run in flight, and running one.
// ---------------------------------------------------------------------------

// Everything the search reads that is not the player state and not the
// executor. A path replanned, a permit granted or an objective changed
// between the prediction and the boundary makes the cached judgement a
// judgement of a world that no longer exists, so the cache is refused and the
// frame pays for its own search - which is what it does today.
// The progress index is deliberately not here: the executor advances it
// through the flight, and the index the search will read at the boundary is
// inside the predicted executor, which the take compares byte for byte.
static int SearchCacheContext(const movement_t *m,const searchCache_t *c) {
    return c->pathGeneration==m->path.generation && c->pathCount==m->path.count &&
           c->objectiveSet==m->objectiveSet &&
           c->commits==m->commits && c->permitActive==m->permit.active &&
           c->tapeCursor==decisionTape.cursor &&
           !memcmp(c->objective,m->objective,sizeof(vec3_t));
}

static void SearchCacheContextStore(const movement_t *m,searchCache_t *c) {
    c->pathGeneration=m->path.generation;c->pathCount=m->path.count;
    c->objectiveSet=m->objectiveSet;
    c->commits=m->commits;c->permitActive=m->permit.active;
    c->tapeCursor=decisionTape.cursor;
    VectorCopy(m->objective,c->objective);
}

// Re-run the winner's own last rollout from the actual state and keep what it
// measures. The state compare has already said the root is the predicted one,
// so this reproduces a computation whose answer is known; it is run anyway,
// because a commit installs what this rollout measured and the design is that
// the committed measurement is taken from the state the bot is really in. A
// disagreement refuses the cache.
static int SearchCacheValidate(gentity_t *ent,pmove_t *move,straightResult_t *r,
                               const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after,
                               float rootGoal,int rootSpeedMs) {
    const playerState_t *ps=&ent->client->ps;
    spikeMeasure_t expected=r->measure;const char *outcome=r->outcome,*again=r->outcome;
    bot2_forecastGuard_t guard;int ok;
    if(!Bot2_ForecastGuardBegin(move,&guard))return 0;
    if(MOVE_FIX(BX3) && r->rerouted && r->routeCells<=0) {
        // bot_moveFixBX3: an arrival the reroute pass's first continuation found is re-rolled as
        // that pass ran it - the frame's own `before`, the current path, no path stop - not along a route.
        float us=0;int traces=0;
        straightStopTime=0;straightLandings=0;straightOffPath=0;straightPathFree=1;
        r->outcome=RunCandidate(ent,move,&r->candidate,before,after,StraightCommand,1,&r->measure,&us,&traces);
        straightPathFree=ArriveReroute();
        StraightArrives(ent,move,r,before,after,rootGoal,&us,&traces);
        straightPathFree=0;
        again=outcome;
    } else if(r->rerouted) {
        bot2_moveExecutor_t rerouteBefore=*before;float us=0;int traces=0;
        if(!RerouteFrom(ent->s.number,r,rootSpeedMs,&rerouteScratch)) {Bot2_ForecastGuardEnd(&guard);return 0;}
        rerouteBefore.pathIndex=0;
        straightStopTime=0;straightLandings=0;straightOffPath=0;straightPathFree=1;
        rolloutPathOverride=&rerouteScratch;
        r->outcome=RunCandidate(ent,move,&r->candidate,&rerouteBefore,after,StraightCommand,1,&r->measure,&us,&traces);
        if(!ArriveReroute()) {rolloutPathOverride=NULL;straightPathFree=0;}
        StraightArrives(ent,move,r,&rerouteBefore,after,rootGoal,&us,&traces);
        rolloutPathOverride=NULL;straightPathFree=0;
        // The reroute pass rewrites the outcome after this rollout from the
        // route it prices; only the measurement is reproduced here, and the
        // outcome the judge settled on stands.
        again=outcome;
    } else {
        float us=0;int traces=0;
        straightStopTime=0;straightLandings=0;straightOffPath=0;
        r->outcome=RunCandidate(ent,move,&r->candidate,before,after,StraightCommand,1,&r->measure,&us,&traces);
        if(straightOffPath)r->outcome="off_path";
        StraightArrives(ent,move,r,before,after,rootGoal,&us,&traces);
        again=r->outcome;
    }
    ok=!guard.contaminated;
    Bot2_ForecastGuardEnd(&guard);
    if(!ok || strcmp(again,outcome) || memcmp(&r->measure,&expected,sizeof(expected))) {
        Q_strncpyz(searchStateField,!ok?"guard":strcmp(again,outcome)?"outcome":"measure",
                   sizeof(searchStateField));
        r->measure=expected;r->outcome=outcome;
        return 0;
    }
    r->outcome=outcome;
    (void)ps;
    return 1;
}


// bot_moveSearchWhy (diagnostic, records/2026-10-04-tail-composition.md): every refused early-search
// take prints what differed first - which context field, which player-state or executor field (with the size of an
// origin or velocity difference), or which candidate and plan field - with the prediction's kind and horizon and the
// nearest other live player. Read and printed only on a refusal; changes no decision.
#define EXEC_FIELD(f) {#f,offsetof(bot2_moveExecutor_t,f),sizeof(((bot2_moveExecutor_t*)0)->f)}
static const struct {const char *name;size_t offset,size;} execFields[]={
    EXEC_FIELD(yaw),EXEC_FIELD(lastTime),EXEC_FIELD(walking),EXEC_FIELD(decline),EXEC_FIELD(corridorSample),
    EXEC_FIELD(pathIndex),EXEC_FIELD(permitted),EXEC_FIELD(offmeshType),EXEC_FIELD(braked),EXEC_FIELD(held),
    EXEC_FIELD(waypoint),EXEC_FIELD(stage),EXEC_FIELD(launchTime),EXEC_FIELD(airborne),EXEC_FIELD(mark),
    EXEC_FIELD(prepress),EXEC_FIELD(side),EXEC_FIELD(sideVz),EXEC_FIELD(sideOrigin),EXEC_FIELD(climbFrame),
    EXEC_FIELD(retreatUntil),EXEC_FIELD(retreatDir),EXEC_FIELD(touch)
};
#undef EXEC_FIELD
static const char *ExecDiff(const bot2_moveExecutor_t *a,const bot2_moveExecutor_t *b) {
    int i;
    for(i=0;i<(int)ARRAY_LEN(execFields);i++)
        if(memcmp((const byte*)a+execFields[i].offset,(const byte*)b+execFields[i].offset,execFields[i].size))return execFields[i].name;
    return memcmp(a,b,sizeof(*a))?"padding":"none";
}
static const char *PlanDiff(const bot2_movePlan_t *a,const bot2_movePlan_t *b) {
    if(a->kind!=b->kind)return "kind";
    if(a->count!=b->count)return "count";
    if(a->edge!=b->edge)return "edge";
    if(a->edge2!=b->edge2)return "edge2";
    if(memcmp(a->origin,b->origin,sizeof(vec3_t)))return "origin";
    if(memcmp(a->hop[0].dir,b->hop[0].dir,sizeof(a->hop[0].dir)))return "hop0_dir";
    if(a->hop[0].runup!=b->hop[0].runup)return "hop0_runup";
    if(memcmp(a->hop[1].dir,b->hop[1].dir,sizeof(a->hop[1].dir)))return "hop1_dir";
    if(a->hop[1].runup!=b->hop[1].runup)return "hop1_runup";
    if(memcmp(a->anchor1,b->anchor1,sizeof(a->anchor1)) || a->anchored1!=b->anchored1)return "anchor1";
    if(a->id!=b->id || a->attempted!=b->attempted)return "id";
    return "other";
}
static float NearestPlayer(const gentity_t *ent) {
    float best=1e9f;int i;
    for(i=0;i<level.maxclients;i++) {
        const gentity_t *o=&g_entities[i];
        if(i==ent->s.number || !o->inuse || !o->client || o->health<=0 || o->client->sess.sessionTeam==TEAM_SPECTATOR)continue;
        best=min(best,Distance(o->client->ps.origin,ent->client->ps.origin));
    }
    return best;
}
static void SearchWhy(const gentity_t *ent,const searchCache_t *c,const char *why,const char *field,const char *detail) {
    if(!trap->Cvar_VariableIntegerValue("bot_moveSearchWhy"))return;
    Bot2_PrintTelemetry(1,"[BUDGET] client=%d level=%d refusal kind=%s frames=%d why=%s field=%s near=%.0f %s\n",
        ent->s.number,level.time,c->landingKind?"landing":"cadence",c->targetFrames,why,field,NearestPlayer(ent),detail?detail:"");
}

// A candidate's id is spikePlans+1, and spikePlans counts every bot's commits, so another bot committing between
// the prediction and the take changed every id and refused the take: 51% of walking and 65% of landing refusals in
// the 4v4, none with one bot (records/2026-10-04-tail-composition.md). Nothing a rollout or the judge computes reads
// the id (telemetry, guest traces and the bot's own plan checks do), so the compare leaves it out and a taken set is
// given the live one.
static int CandidateSame(const bot2_movePlan_t *live,const bot2_movePlan_t *built) {
    bot2_movePlan_t b;
    memcpy(&b,built,sizeof(b));
    b.id=live->id;
    return !memcmp(live,&b,sizeof(b));
}

// The live search's chance to take a judged candidate set that was built in
// flight. Called once the frame's own candidates have been constructed, so
// the construction is the check: the same state and the same path build the
// same candidates, and a single differing plan refuses the cache.
static int SearchCacheTake(gentity_t *ent,pmove_t *move,straightResult_t *results,int originals,
                           int *candidates,int *stateCandidates,
                           int *counts,float *us,int *traces,straightVerdict_t *v,
                           const bot2_moveExecutor_t *before,const bot2_moveExecutor_t *after,
                           int index,const float *ref) {
    int client=ent->s.number;searchCache_t *c=&searchCache[client];
    movement_t *m=&movement[client];const playerState_t *ps=&ent->client->ps;
    const char *field;int i,rootSpeedMs,*hz;
    if(!SearchBudget())return 0;
    searchTally[client].searches++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_SEARCHES);takeRefusal=0;
    if(c->stage!=PRE_READY || c->targetTime!=level.time || c->deferred==2) {
        searchTally[client].missedNotReady++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_NOT_READY);
        if(c->stage==PRE_IDLE)searchTally[client].notReadyIdle++;
        else if(c->stage==PRE_PREDICTED)searchTally[client].notReadyPredicted++;
        else if(c->stage==PRE_JUDGING)searchTally[client].notReadyJudging++;
        else if(c->targetTime>level.time)searchTally[client].notReadyEarly++;
        else searchTally[client].notReadyLate++;
        return 0;
    }
    c->stage=PRE_IDLE;takeRefusal=0;
    searchTally[client].takes++;
    hz=searchTally[client].horizon[c->landingKind?0:1][c->targetFrames<=4?0:c->targetFrames<=8?1:c->targetFrames<=12?2:c->targetFrames<=20?3:4];
    hz[0]++;
    if(!SearchCacheContext(m,c)) {
        SearchWhy(ent,c,"context",c->pathGeneration!=m->path.generation?"path_generation":c->pathCount!=m->path.count?"path_count":
            c->objectiveSet!=m->objectiveSet?"objective_set":c->commits!=m->commits?"commits":c->permitActive!=m->permit.active?"permit":
            c->tapeCursor!=decisionTape.cursor?"tape":"objective",va("objective_moved=%.0f",Distance(c->objective,m->objective)));
        hz[3]++;searchTally[client].refusedContext++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER);return 0;}
    if(c->originals!=originals) {SearchWhy(ent,c,"originals","count",va("live=%d predicted=%d",originals,c->originals));hz[3]++;searchTally[client].refusedOriginals++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER);return 0;}
    // The state compare, on the fields the parity harness compares, plus the
    // executor continuation the rollouts carry. Position, velocity, ground
    // contact and the landing tap bit are inside it; nothing here is tuned to
    // make a cell pass.
    if((field=MovementStateDiff(ps,&c->rootPS))!=NULL) {
        SearchWhy(ent,c,"state",field,va("origin_d=%.2f velocity_d=%.2f ground=%d/%d diverge=%d cmd_same=%d %s",Distance(ps->origin,c->rootPS.origin),
            Distance(ps->velocity,c->rootPS.velocity),ps->groundEntityNum,c->rootPS.groundEntityNum,c->divergeFrame,c->divergeCmdSame,
            c->divergeWhat));
        hz[2]++;takeRefusal=1;searchTally[client].refusedState++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_REFUSED_STATE);
        Q_strncpyz(searchStateField,field,sizeof(searchStateField));
        return 0;
    }
    if(memcmp(&m->executor,&c->rootExec,sizeof(m->executor)) ||
       memcmp(&m->executorBefore,&c->rootExecBefore,sizeof(m->executorBefore))) {
        int before_=memcmp(&m->executor,&c->rootExec,sizeof(m->executor))!=0;
        SearchWhy(ent,c,before_?"executor":"executor_before",before_?ExecDiff(&m->executor,&c->rootExec):ExecDiff(&m->executorBefore,&c->rootExecBefore),NULL);
        hz[2]++;takeRefusal=2;searchTally[client].refusedExec++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER);return 0;
    }
    for(i=0;i<originals;i++)if(!CandidateSame(&results[i].candidate,&c->built[i])) {
        int j,differ=0;
        for(j=i;j<originals;j++)differ+=!CandidateSame(&results[j].candidate,&c->built[j]);
        SearchWhy(ent,c,"candidates",PlanDiff(&results[i].candidate,&c->built[i]),va("first=%d differ=%d of=%d",i,differ,originals));
        hz[3]++;searchTally[client].refusedCandidates++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER);return 0;
    }
    rootSpeedMs=(ps->fd.forcePowersActive&(1<<FP_SPEED))?max(0,ps->fd.forcePowerDuration[FP_SPEED]-level.time):0;
    straightRootIndex=index;   // as the judge left it for this root: the last judge may have been another bot's
    if(c->v.best>=0 && !SearchCacheValidate(ent,move,&c->results[c->v.best],before,after,
                                            GoalDistance(ps,m->objective),rootSpeedMs)) {
        SearchWhy(ent,c,"validate","winner",NULL);
        searchTally[client].refusedValidate++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER);return 0;
    }
    hz[1]++;searchTally[client].hits++;Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_BUDGET_HITS);
    memcpy(results,c->results,sizeof(straightResult_t)*c->candidates);
    // The sequence number a search on this frame would have given its candidates (see CandidateSame).
    for(i=0;i<c->candidates;i++)results[i].candidate.id=spikePlans+1;
    memcpy(counts,c->counts,sizeof(int)*CANDIDATE_OUTCOMES);
    *candidates=c->candidates;*stateCandidates=c->stateCandidates;
    *us=c->us;*traces=c->traces;*v=c->v;
    (void)index;(void)ref;
    // The taken set stands in for StraightJudge, which disarms the harness
    // before it judges: the commit that follows may re-issue this frame's
    // command, and a horizon still armed from the previous plan would then
    // compare the new command's Pmove against the old plan's prediction.
    SearchDisarm(ent);
    return 1;
}

// The boundary the next search will run at, as this frame can know it: the
// landing of a committed sequence, or the retry a failed search scheduled.
// Zero when there is none to predict.
static int SearchNextBoundary(const movement_t *m) {
    int next;
    if(m->plan.count && m->executor.stage<Onward(&m->plan))return -1;   // a landing, time unknown
    next=max(m->straight.searchTime,m->straight.time?m->straight.time+MOVE_GAP_CADENCE:0);
    return next>level.time?next:0;
}

// Build the horizon from this boundary and keep the frame the next search
// will run at. Exactly the harness's own build, in its own buffer, and
// bounded by what the committed sequence measured rather than by the whole
// horizon, so the one rollout a commit pays for is the flight and not 256
// frames of nothing.
static void SearchPredictAt(gentity_t *ent,pmove_t *move,int boundary);
static void SearchPredict(gentity_t *ent,pmove_t *move) {SearchPredictAt(ent,move,SearchNextBoundary(&movement[ent->s.number]));}
static int searchPredictExtra;   // bot_moveFixBX4: frames past a timed boundary's horizon (WalkDefer's ground guard only)
static void SearchPredictAt(gentity_t *ent,pmove_t *move,int boundary) {
    int client=ent->s.number;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    bot2_forecastRequest_t request;bot2_maneuverState_t policy;bot2_moveContext_t context;
    int horizon,i,target=-1,onward;
    c->stage=PRE_IDLE;
    if(!boundary)return;
    searchTally[client].predictions++;
    if(boundary<0) {
        horizon=0;
        for(i=0;i<m->plan.count;i++)horizon+=max(0,m->plan.measured[i].frames);
        // The measured flight, 300 ms on (12 frames at 25 ms) and at least 200 ms (8).
        horizon=min(MoveSteps(MOVE_PARITY_HORIZON_MS),max(Bot2_StepsFor(200),horizon+Bot2_StepsFor(300)));
    } else horizon=min(MoveSteps(MOVE_PARITY_HORIZON_MS),   // to the boundary and 50 ms (2 frames) on
                       max(Bot2_StepsFor(50),(boundary-level.time)/max(1,Bot2_GameStepMsec())+Bot2_StepsFor(50)+searchPredictExtra));
    RolloutRequest(ent,move,&context,&policy,horizon,&request);
    request.frames=searchBuildFrames;request.frameCapacity=MOVE_PARITY_MAX_HORIZON;
    context.plan=&m->plan;
    parity.rollouts++;searchRollouts++;
    ScopedForecastRun(move,&request,&searchBuildResult);
    if(searchBuildResult.status!=BOT2_FORECAST_COMPLETE || searchBuildResult.unsupported ||
       searchBuildResult.worldChanged || searchBuildResult.animationChanged ||
       !searchBuildResult.restored || !searchBuildResult.animationRestored) {
        searchTally[client].predictRefused++;return;
    }
    onward=Onward(&m->plan);
    for(i=1;i<searchBuildResult.count;i++) {
        const bot2_forecastFrame_t *f=&searchBuildFrames[i];
        if(boundary<0) {
            const bot2_moveExecutor_t *b=(const bot2_moveExecutor_t*)f->beforePolicyExtra;
            const bot2_moveExecutor_t *a=(const bot2_moveExecutor_t*)f->afterPolicyExtra;
            if(m->plan.count && a->stage==onward && b->stage<onward) {target=i;break;}
        } else if(f->serverTime>=boundary) {target=i;break;}
    }
    // Two frames of head room at least, or there is nothing to spread over
    // and the boundary may as well pay for its own search.
    if(target<2) {searchTally[client].predictNoTarget++;return;}
    c->rootPS=searchBuildFrames[target].beforePS;
    memcpy(&c->rootExecBefore,searchBuildFrames[target].beforePolicyExtra,sizeof(c->rootExecBefore));
    memcpy(&c->rootExec,searchBuildFrames[target].afterPolicyExtra,sizeof(c->rootExec));
    c->rootCmd=searchBuildFrames[target].output.cmd;
    for(i=0;i<4;i++)c->rootAnim[i]=searchBuildFrames[target].precedingAnimation[i];
    VectorCopy(c->rootPS.origin,c->rootOrigin);
    c->targetTime=searchBuildFrames[target].serverTime;
    c->targetFrames=target;c->landingKind=boundary<0;c->deferred=0;
    c->trackCount=min(target+1,64);c->divergeFrame=-1;c->divergeCmdSame=-1;c->divergeWhat[0]=0;
    for(i=0;i<c->trackCount;i++) {
        c->trackTime[i]=searchBuildFrames[i].serverTime;c->trackCmd[i]=searchBuildFrames[i].command;
        VectorCopy(searchBuildFrames[i].beforePS.origin,c->trackOrigin[i]);VectorCopy(searchBuildFrames[i].beforePS.velocity,c->trackVelocity[i]);
    }
    c->planId=m->plan.id;
    SearchCacheContextStore(m,c);
    c->stage=PRE_PREDICTED;
}

// One chunk of the pre-search. The predicted boundary is installed on the
// bot, the search runs from it under this frame's rollout budget, and the
// live state is put back on every path out. Everything the search writes
// outside its own result storage is restored with it: the pre-search is a
// proposal and must leave no trace.
typedef void (*searchChunkCore_t)(gentity_t *ent,pmove_t *move,searchCache_t *c);
static void SearchChunkWith(gentity_t *ent,pmove_t *move,searchChunkCore_t core);
static void SearchChunk(gentity_t *ent,pmove_t *move) {SearchChunkWith(ent,move,StraightSearchCore);}
static void SearchChunkWith(gentity_t *ent,pmove_t *move,searchChunkCore_t core) {
    int client=ent->s.number;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    movement_t savedMovement=*m;
    playerState_t savedPS=ent->client->ps;
    usercmd_t savedCmd=move->cmd;
    vec3_t savedOrigin;
    int savedTime=level.time,savedPrevious=level.previousTime,savedFrame=level.framenum;
    int savedAnim[4],savedParity[8];
    if(!SearchCacheContext(m,c)) {c->stage=PRE_IDLE;return;}
    savedAnim[0]=ent->s.legsAnim;savedAnim[1]=ent->s.torsoAnim;
    savedAnim[2]=ent->s.legsFlip;savedAnim[3]=ent->s.torsoFlip;
    savedParity[0]=parity.armed;savedParity[1]=parity.index;savedParity[2]=parity.count;
    savedParity[3]=parity.truncated;savedParity[4]=parity.client;savedParity[5]=parity.completion;
    savedParity[6]=parity.builtTime;savedParity[7]=parity.planned;
    VectorCopy(ent->r.currentOrigin,savedOrigin);
    ent->client->ps=c->rootPS;
    m->executor=c->rootExec;m->executorBefore=c->rootExecBefore;
    m->frameTime=c->targetTime;m->owned=1;
    move->cmd=c->rootCmd;
    ent->s.legsAnim=c->rootAnim[0];ent->s.torsoAnim=c->rootAnim[1];
    ent->s.legsFlip=c->rootAnim[2];ent->s.torsoFlip=c->rootAnim[3];
    VectorCopy(c->rootOrigin,ent->r.currentOrigin);
    level.time=c->targetTime;level.previousTime=level.time-Bot2_GameStepMsec();
    level.framenum=savedFrame+max(0,(c->targetTime-savedTime)/max(1,Bot2_GameStepMsec()));
    core(ent,move,c);
    level.time=savedTime;level.previousTime=savedPrevious;level.framenum=savedFrame;
    ent->client->ps=savedPS;
    move->cmd=savedCmd;
    ent->s.legsAnim=savedAnim[0];ent->s.torsoAnim=savedAnim[1];
    ent->s.legsFlip=savedAnim[2];ent->s.torsoFlip=savedAnim[3];
    VectorCopy(savedOrigin,ent->r.currentOrigin);
    *m=savedMovement;
    parity.armed=savedParity[0];parity.index=savedParity[1];parity.count=savedParity[2];
    parity.truncated=savedParity[3];parity.client=savedParity[4];parity.completion=savedParity[5];
    parity.builtTime=savedParity[6];parity.planned=savedParity[7];
}

// Called at the Pmove boundary, after the searches. `searched` is whether a
// straight search ran on this frame, which invalidates any prediction: the
// plan and the retry it leaves behind are new.
// The deadline. A fixed rate leaves a pre-search unfinished at its boundary
// whenever the flight is short or the search is long, and an unfinished
// pre-search is a whole search on the landing frame - the frame row 5 is about
// (records/2026-09-22-row5-planner.md, one bot: every frame over 25 ms was a
// search with no pre-search ready). So a chunk may spend at least the work the
// pre-search can still need divided by the frames left before its boundary.
// Both are counts: the work is an upper bound in rollouts read off the judge's
// own resume point (each unjudged candidate may need its rollout and two more
// in the reroute pass, each judged off-path candidate two, and the three walk
// references), and the frames are level time. Nothing reads a clock, so the
// chunks and the decisions are the same on every run. Construction has not run
// before the first chunk, so that chunk budgets for a full candidate set.
#define MOVE_SEARCH_WORK_UNBUILT (3*24+3)
static int SearchWorkLeft(const searchCache_t *c) {
    int i,left=3;
    if(c->stage!=PRE_JUDGING)return MOVE_SEARCH_WORK_UNBUILT;
    if(c->js.phase==JUDGE_CANDIDATES) {
        left+=3*max(0,c->candidates-c->js.cursor);
        for(i=0;i<c->js.cursor && i<c->candidates;i++)
            if(c->results[i].outcome && !strcmp(c->results[i].outcome,"off_path"))left+=2;
    } else if(c->js.phase==JUDGE_REROUTE) {
        for(i=c->js.cursor;i<c->candidates;i++)
            if(c->results[i].outcome && !strcmp(c->results[i].outcome,"off_path"))left+=2;
    }
    return left;
}

static int SearchChunkBudget(const searchCache_t *c,int rate) {
    int frames=max(1,(c->targetTime-level.time)/max(1,Bot2_GameStepMsec()));
    int work=SearchWorkLeft(c);
    return max(rate,(work+frames-1)/frames);
}

// ---------------------------------------------------------------------------
// The deferred walking search (`bot_moveWalkDefer` K frames of 25 ms - K*25 ms at any tickrate - default 6, 0 turns it off,
// records/2026-10-04-tail-composition.md, owner list item 4).
//
// About four fifths of the slow straight-search time in the 4v4 is the search a walking bot runs on its 200 ms
// cadence, not a landing's, so no kept speed is at stake in choosing that hop a few frames later. When such a
// search falls due, the bot predicts its own walk K frames ahead (the early search's horizon build), keeps walking,
// and the search's rollouts run from the predicted state on the frames in between, out of one pool of rollouts a
// server frame shared by every bot (`bot_moveWalkPool`, default 12 per 25 ms of frame; deterministic, in client order). At the
// predicted frame the result is taken through the early search's exact checks. A refused or unfinished one is
// never replaced by a whole search on that frame - that would be the hitch again - but deferred anew from where the
// bot is. Landing searches, a planned edge's takeoff window and the gap, climb and knock searches are unchanged.
// ---------------------------------------------------------------------------
static int walkPoolTime=-1,walkPoolLeft;
// Defaults K = 6, pool 12 (after the rescue's outcome pair); bot_moveWalkDefer 0 turns it off.
static int WalkDeferFrames(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveWalkDefer",value,sizeof(value));
    return Bot2_StepsFor(25*(value[0]?max(0,atoi(value)):6));   // K counts 25 ms frames: frames of K*25 ms
}
static int WalkPoolLeft(void) {
    if(walkPoolTime!=level.time) {
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_moveWalkPool",value,sizeof(value));
        walkPoolTime=level.time;walkPoolLeft=Bot2_WorkPerFrame(value[0]?max(0,atoi(value)):12);   // per 25 ms of frame
    }
    return walkPoolLeft;
}
// ---------------------------------------------------------------------------
// The deferred drop search (`bot_moveDropDefer` K frames of 25 ms, 0: off, records/2026-10-04-tail-composition.md).
//
// Planned drop searches were about 55% of the 4v4's remaining slow frames: about 28 candidates rolled out through
// the run-up, while the bot walks toward the ledge (PlannedEligible, within MOVE_EDGE_APPROACH). As for the walking
// search, the bot predicts its own walk K frames ahead and keeps walking, and the drop search runs from the
// predicted state in chunks out of the same shared pool (bot_moveWalkPool). At the predicted frame the winner is
// rebuilt by its recipe (point, line, launch) from the live state and rolled out once; admitted, it is committed.
// No winner with the state as predicted is the search's failure, charged as a live one is (a contact with another
// player or a missile counts as transient: there is no counterfactual search). Anything else - drifted state with
// no winner, the winner refused or no longer offered, a changed path, an unfinished search - is no hop on this frame,
// and the search is offered again after MOVE_GAP_RETRY. Never a whole search on the frame.
// ---------------------------------------------------------------------------
static dropSearch_t dropDeferred[MAX_CLIENTS];
// Default K = 6 (after the solo bench); bot_moveDropDefer 0 turns it off.
static int DropDeferFrames(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveDropDefer",value,sizeof(value));
    return Bot2_StepsFor(25*(value[0]?max(0,atoi(value)):6));   // K counts 25 ms frames: frames of K*25 ms
}
static void DropChunkCore(gentity_t *ent,pmove_t *move,searchCache_t *c) {
    movement_t *m=&movement[ent->s.number];dropSearch_t *ds=&dropDeferred[ent->s.number];const graphEdge_t *e=Graph_Edge(c->dropEdge);
    bot2_moveExecutor_t before,after;bot2_forecastGuard_t guard;bot2_forecastContacts_t seen;
    if(c->stage==PRE_PREDICTED) {DropBuild(ent,c->dropEdge,ds);c->stage=PRE_JUDGING;}
    Bot2_ForecastContactsReset();
    if(!Bot2_ForecastGuardBegin(move,&guard))return;   // the next frame tries again
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    rolloutFallDrop=max(MOVE_FALL_DROP,e->takeoff[2]-e->landing[2]+2*STEPSIZE);
    if(DropEvaluate(ent,move,ds,&before,&after,&guard,searchRolloutBudget))c->stage=PRE_READY;
    rolloutFallDrop=MOVE_FALL_DROP;
    Bot2_ForecastGuardEnd(&guard);
    seen=Bot2_ForecastContacts();
    ds->contacts.transient+=seen.transient;ds->contacts.mover+=seen.mover;ds->contacts.other+=seen.other;
}
// The take (no ledge waits). The ready recipes, best first (at most three), are rebuilt from the
// live state and rolled out in turn; the first admitted is committed. A failure with the state as predicted is the
// search's and waits MOVE_GAP_RETRY as a live failure does; a stale result (changed path, drifted state with no
// winner, recipes no longer offered or all refused) defers anew on the next frame instead of waiting.
static void DropTake(gentity_t *ent,pmove_t *move,int edge) {
    int client=ent->s.number,i,t,full,exact;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    dropSearch_t *ds=&dropDeferred[client];const playerState_t *ps=&ent->client->ps;const graphEdge_t *e=Graph_Edge(edge);
    static dropSearch_t live;
    bot2_moveExecutor_t before,after;bot2_forecastGuard_t guard;
    c->deferred=0;c->stage=PRE_IDLE;
    m->planned.edge=edge;m->planned.searchTime=level.time+MOVE_GAP_RETRY;
    if(!SearchCacheContext(m,c)) {
        searchTally[client].dropContext++;m->planned.searchTime=level.time;
        Event(client,MOVE_EVENT_PLAN,"none","none",va("kind=edge edge=%d reason=deferred_context",edge));return;
    }
    exact=!MovementStateDiff(ps,&c->rootPS) && !memcmp(&m->executor,&c->rootExec,sizeof(m->executor)) &&
          !memcmp(&m->executorBefore,&c->rootExecBefore,sizeof(m->executorBefore));
    if(ds->bestIndex<0 && ds->contaminated) {
        // The search stopped at a contamination, not at its last candidate: a stale result, not a failure.
        searchTally[client].dropDrift++;m->planned.searchTime=level.time;
        Event(client,MOVE_EVENT_PLAN,"none","none",va("kind=edge edge=%d reason=deferred_contaminated",edge));
        return;
    }
    if(ds->bestIndex<0) {
        if(exact) {searchTally[client].dropNone++;DropFinish(ent,move,ds,1);}
        else {
            searchTally[client].dropDrift++;m->planned.searchTime=level.time;
            Event(client,MOVE_EVENT_PLAN,"none","none",va("kind=edge edge=%d reason=deferred_drift_none",edge));
        }
        return;
    }
    DropBuild(ent,edge,&live);full=live.count;
    SearchDisarm(ent);Bot2_ForecastContactsReset();
    if(!Bot2_ForecastGuardBegin(move,&guard)) {Event(client,MOVE_EVENT_PLAN,"none","none","kind=drop reason=guard_busy");return;}
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    rolloutFallDrop=max(MOVE_FALL_DROP,e->takeoff[2]-e->landing[2]+2*STEPSIZE);
    live.index=ds->index;
    for(t=0;t<ds->topCount && live.bestIndex<0 && !guard.contaminated;t++) {
        for(i=0;i<full;i++)
            if(live.list[i].p==ds->topP[t] && live.list[i].l==ds->topL[t] && live.list[i].v==ds->topV[t])break;
        if(i==full)continue;
        live.cursor=i;live.count=i+1;
        DropEvaluate(ent,move,&live,&before,&after,&guard,0);
    }
    rolloutFallDrop=MOVE_FALL_DROP;
    Bot2_ForecastGuardEnd(&guard);
    if(live.bestIndex<0) {
        searchTally[client].dropRescueFailed++;m->planned.searchTime=level.time;
        Event(client,MOVE_EVENT_PLAN,"none","none",va("kind=edge edge=%d reason=deferred_refused tried=%d",edge,t));return;
    }
    if(t>1)searchTally[client].dropRescueGone++;   // committed a recipe after the best
    live.points=ds->points;live.lines=ds->lines;live.admitted=ds->admitted;live.us+=ds->us;live.traces+=ds->traces;
    searchTally[client].dropTaken++;
    DropFinish(ent,move,&live,1);
}
// Called where the planned search would run. 1: handled here (pending, started or taken); 0: search now.
static int DropDefer(gentity_t *ent,pmove_t *move,int edge) {
    int client=ent->s.number;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    const graphEdge_t *e=Graph_Edge(edge);int frames=DropDeferFrames();
    if(frames<=0 || !SearchBudget() || !e || e->type!=GRAPH_EDGE_DROP || !DropPlan() || counterfactual.active) {
        if(c->deferred==2) {c->deferred=0;c->stage=PRE_IDLE;}
        return 0;
    }
    if(c->deferred==2) {
        if(c->dropEdge==edge && c->stage!=PRE_IDLE && level.time<c->targetTime)return 1;
        if(c->dropEdge==edge && c->stage==PRE_READY && level.time==c->targetTime) {DropTake(ent,move,edge);return 1;}
        searchTally[client].dropLate++;
    }
    c->deferred=0;c->stage=PRE_IDLE;   // a walking deferral gives way to the drop
    // K comes from DropDeferFrames already in steps (Bot2_StepsFor): no MoveFrames here.
    SearchPredictAt(ent,move,level.time+frames*max(1,Bot2_GameStepMsec()));
    if(c->stage!=PRE_PREDICTED) {searchTally[client].dropPredictFailed++;c->stage=PRE_IDLE;return 0;}
    c->deferred=2;c->dropEdge=edge;c->attemptBoundary=c->targetTime;c->attemptPlan=m->plan.id;
    searchTally[client].dropStarted++;
    return 1;
}

// bot_moveWindowDefer (default 1; 0 off): the first frame inside a planned strafe edge's takeoff window is deferred as
// a cadence search is. Kejim 4v4 (four default worlds): about 430 window-entry searches a world run whole on
// their frame and most find nothing; window-entry searches that find nothing are 16.4 s of the 31.1 s of slow-frame
// bot time. The window is MOVE_STATE_TAKEOFF (64 u) round the takeoff, 160-250 ms of walking, longer than K frames.
static int WindowDeferOn(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveWindowDefer",value,sizeof(value));
    return !value[0] || atoi(value)!=0;
}
// A cadence search, as StraightEligible's own non-landing branch defines it (and, with bot_moveWindowDefer, a takeoff
// window's first frame).
static int WalkDeferrable(gentity_t *ent) {
    movement_t *m=&movement[ent->s.number];int k,k2,edge;
    if(StraightLanding(m))return 0;
    edge=PlannedStrafe(m,&ent->client->ps,&k,&k2);
    return edge<0 || m->state.edge==edge || WindowDeferOn();
}
// bot_moveDeferGround N (frames of 25 ms, default 3; 0 off; records/2026-10-09-level2-speed-stale-library.md): a deferral
// whose predicted walk leaves the ground before its take frame or within N-1 frames after it is not started as it
// was asked (bot_moveDeferGroundShorten 1: its take is brought forward to N frames before the lip, when that leaves two
// frames or more; otherwise, and without Shorten, the search runs whole at once). A walking bot that steps off a ledge inside the horizon (the stand's lip, 44 u from the
// start) is airborne when the deferred result arrives: the straight search needs a grounded root, the hop is never
// committed, the planned edge is walked past (StateAvoid "walked") and the route loses its only strafe edge.
static int DeferGroundFrames(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveDeferGround",value,sizeof(value));
    return Bot2_StepsFor(25*(value[0]?max(0,atoi(value)):3));   // K counts 25 ms frames: frames of K*25 ms
}
// bot_moveDeferGroundWindow 1: the check applies only while a planned strafe edge's takeoff is within R u (TakeoffNear; the value is R, 1 means 128):
// the searches it adds are then the ones that decide that edge's launch, not every ledge a walkway passes (Kejim 4v4
// frames over 25 ms +18% with the check everywhere). Default 80; 0: every walking deferral.
// bot_moveDeferGroundShorten: default 1; 0 searches whole instead.
static int DeferGroundShorten(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveDeferGroundShorten",value,sizeof(value));
    return value[0]?atoi(value)!=0:1;
}
static float DeferGroundWindowOnly(void) {
    char value[16];int v;
    trap->Cvar_VariableStringBuffer("bot_moveDeferGroundWindow",value,sizeof(value));
    v=value[0]?atoi(value):80;
    return v<=0?0.0f:v==1?128.0f:(float)v;   // 1: 128 u; more: that many units
}
// A planned strafe edge's takeoff within `radius` u (and a step's height) of the bot, looked for as PlannedStrafe does
// but without needing the path index yet (a stand start has none) and from farther than the window.
static int TakeoffNear(gentity_t *ent,float radius) {
    movement_t *m=&movement[ent->s.number];const playerState_t *ps=&ent->client->ps;
    int k,start=max(m->executor.pathIndex,0)+1;const graphEdge_t *e;float dx,dy;
    if(!StateEnabled() || !m->path.count)return 0;
    for(k=start;k<m->path.count && k<=start+2;k++)if(m->path.edges[k]>=0)break;
    if(k>=m->path.count || !PathStrafe(&m->path,k))return 0;
    e=Graph_Edge(m->path.edges[k]);
    if(!e || Graph_EdgeOff(e))return 0;
    dx=e->takeoff[0]-ps->origin[0];dy=e->takeoff[1]-ps->origin[1];
    return dx*dx+dy*dy<=radius*radius && fabsf(e->takeoff[2]-ps->origin[2])<=2*STEPSIZE;
}
// Called where the straight search would run. 1: nothing to search on this frame (a deferral is pending or was
// just started); 0: search now - a whole search when no deferral applies, or the deferral's take (walkDeferTake).
static int SearchPlayerNear(const gentity_t *ent);
static int WalkDefer(gentity_t *ent,pmove_t *move) {
    int client=ent->s.number;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    int frames=WalkDeferFrames(),ground,windowEntry,extra;
    walkDeferTake=0;
    if(c->deferred==2) {c->deferred=0;c->stage=PRE_IDLE;}   // a drop deferral whose edge is no longer due
    if(frames<=0 || !SearchBudget()) {if(c->deferred){c->deferred=0;c->stage=PRE_IDLE;}return 0;}
    if(!WalkDeferrable(ent)) {
        if(c->deferred) {
            c->deferred=0;c->stage=PRE_IDLE;searchTally[client].abandoned++;
            if(StraightLanding(m)) {searchTally[client].walkOrphan++;if(!SearchPlayerNear(ent))searchTally[client].walkOrphanAlone++;}
        }
        return 0;
    }
    // An early search ready for this very frame is taken as a deferral's would be.
    if(!c->deferred && c->stage==PRE_READY && c->targetTime==level.time) {walkDeferTake=1;return 0;}
    if(c->deferred) {
        if(c->stage!=PRE_IDLE && level.time<c->targetTime)return 1;
        if(c->stage==PRE_READY && level.time==c->targetTime) {c->deferred=0;walkDeferTake=1;return 0;}
        searchTally[client].walkLate++;
        if(c->stage==PRE_IDLE)searchTally[client].walkLateIdle++;
        else if(c->stage==PRE_PREDICTED)searchTally[client].walkLatePredicted++;
        else if(c->stage==PRE_JUDGING)searchTally[client].walkLateJudging++;
        c->deferred=0;c->stage=PRE_IDLE;
    }
    windowEntry=DeferGroundWindowOnly()>0?TakeoffNear(ent,DeferGroundWindowOnly()):0;
    // K and N come from WalkDeferFrames and DeferGroundFrames already in steps (Bot2_StepsFor): no MoveFrames here.
    // bot_moveFixBX4: the guard's predictions reach `ground` frames past the take, so N means N
    // (with 0 they hold the take and one frame after it: N>=3 means N-1, and 1 and 2 both mean one).
    extra=MOVE_FIX(BX4) && DeferGroundFrames()>0 && (DeferGroundWindowOnly()<=0 || windowEntry)?max(0,DeferGroundFrames()-2):0;
    searchPredictExtra=extra;
    SearchPredictAt(ent,move,level.time+frames*max(1,Bot2_GameStepMsec()));
    searchPredictExtra=0;
    if(c->stage!=PRE_PREDICTED) {searchTally[client].walkPredictFailed++;c->stage=PRE_IDLE;return 0;}
    if((ground=DeferGroundFrames())>0 && (DeferGroundWindowOnly()<=0 || windowEntry)) {
        int i,pass;
        for(pass=0;pass<2;pass++) {
            // The first frame off the ground within the horizon and the frames the take is owed after it.
            for(i=1;i<c->targetFrames+ground && i<searchBuildResult.count;i++)
                if(searchBuildFrames[i].beforePS.groundEntityNum==ENTITYNUM_NONE)break;
            if(!(i<c->targetFrames+ground && i<searchBuildResult.count))break;
            // Cap the horizon so the take falls `ground` frames before it (bot_moveDeferGroundShorten), else search whole.
            if(pass==0 && DeferGroundShorten() && i-ground>=2) {
                searchPredictExtra=extra;
                SearchPredictAt(ent,move,level.time+(i-ground)*max(1,Bot2_GameStepMsec()));
                searchPredictExtra=0;
                if(c->stage!=PRE_PREDICTED) {searchTally[client].walkPredictFailed++;c->stage=PRE_IDLE;return 0;}
                searchTally[client].walkShortened++;
                continue;
            }
            searchTally[client].walkAirborne++;c->stage=PRE_IDLE;return 0;
        }
    }
    c->deferred=1;c->attemptBoundary=c->targetTime;c->attemptPlan=m->plan.id;
    searchTally[client].walkStarted++;
    return 1;
}

// bot_moveSearchAlone (records/2026-10-04-bodyms-and-search-alone.md P2; default 2048, 0 turns it
// off): no early search while another live player is within this many units. Near other players the early search
// judges candidates against where they stood frames before the boundary and most of its work is refused or overtaken
// there (4v4: 19% served, solo 78%); the search then runs whole on the boundary frame, as with bot_moveSearchBudget 0.
#define MOVE_SEARCH_ALONE_DEFAULT 2048.0f
static int SearchPlayerNear(const gentity_t *ent) {
    char value[16];float range;int i;
    trap->Cvar_VariableStringBuffer("bot_moveSearchAlone",value,sizeof(value));
    if((range=value[0]?(float)atof(value):MOVE_SEARCH_ALONE_DEFAULT)<=0)return 0;
    for(i=0;i<level.maxclients;i++) {
        const gentity_t *other=&g_entities[i];
        if(i==ent->s.number || !other->inuse || !other->client || other->health<=0 ||
           other->client->sess.sessionTeam==TEAM_SPECTATOR || other->client->ps.pm_type!=PM_NORMAL)continue;
        if(DistanceSquared(other->client->ps.origin,ent->client->ps.origin)<range*range)return 1;
    }
    return 0;
}

// bot_moveWalkPoolFrameEnd S (default 700; 0 off; records/2026-10-06-frame-cost.md,
// "S = 700"). The deferred walking
// and drop searches' chunks out of the shared pool ran in each bot's own boundary, wherever that fell in the server
// frame: in the 4v4 timing worlds pool work was in three quarters of Dantooine's frames over 25 ms, and more than
// half of them would have been under it without that work. With S > 0 a bot owed a chunk this frame runs it after
// every bot's boundary (Move_PoolFrameEnd, end of G_RunFrame), in client order, and no pool rollout starts once
// the frame's forecast steps over every bot reach S. Counts, not a clock: the chunks and decisions are the same on
// every run. A chunk the frame has no room for waits for the next frame, as a starved one does.
static pmove_t poolMove[MAX_CLIENTS];
static int poolOwed[MAX_CLIENTS];   // level time + 1 of the frame whose end owes this bot a chunk
static unsigned int poolFrameSteps; // bot2_meterSteps when the frame began (the previous frame's end)
static int PoolFrameEndSteps(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveWalkPoolFrameEnd",value,sizeof(value));
    return Bot2_WorkPerFrame(value[0]?max(0,atoi(value)):700);   // forecast steps per 25 ms of frame
}
// bot_movePoolScopes (default 1; 0 off; records/2026-11-09-replan-budget.md,
// part C): a frame-end chunk runs inside the trace cache's and the corridor memo's scopes, as the same chunk does at
// the boundary (Bot2_MoveParityBeforePmove). One scope a bot, opened after the threat hide (which changes the world
// the caches see). The caches return the engine's own answers, so the chunk's decisions are the same: worlds
// replay with it on or off, with 22-29% fewer forecast traces reaching the engine.
static int PoolScopes(void) {
    char value[16];
    trap->Cvar_VariableStringBuffer("bot_movePoolScopes",value,sizeof(value));
    return value[0]?atoi(value)!=0:1;
}

// A deferred search's chunk out of the pool; 1 when the bot has one in progress (whether or not it got rollouts).
static int PoolChunk(gentity_t *ent,pmove_t *move,int searched) {
    int client=ent->s.number;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    if(c->deferred==2) {
        if((c->stage==PRE_PREDICTED || c->stage==PRE_JUDGING) && c->targetTime>level.time) {
            const dropSearch_t *ds=&dropDeferred[client];
            int frames=max(1,(c->targetTime-level.time)/max(1,Bot2_GameStepMsec()));
            int work=c->stage==PRE_PREDICTED?MOVE_DROP_CANDIDATES:max(1,ds->count-ds->cursor);
            int give=min(max(1,(work+frames-1)/frames),WalkPoolLeft());
            if(give<=0) {searchTally[client].dropStarved++;return 1;}
            walkPoolLeft-=give;
            searchRollouts=0;searchRolloutBudget=give;
            SearchChunkWith(ent,move,DropChunkCore);
            walkPoolLeft+=max(0,give-searchRollouts);
            searchRolloutBudget=0;
            searchTally[client].chunks++;
        }
        return 1;
    }
    if(c->deferred && !searched) {
        if((c->stage==PRE_PREDICTED || c->stage==PRE_JUDGING) && c->targetTime>level.time) {
            int give=min(SearchChunkBudget(c,1),WalkPoolLeft());
            if(give<=0) {searchTally[client].walkStarved++;return 1;}
            walkPoolLeft-=give;
            searchRollouts=0;searchRolloutBudget=give;
            SearchChunk(ent,move);
            walkPoolLeft+=max(0,give-searchRollouts);
            searchRolloutBudget=0;
            if(c->stage==PRE_PREDICTED) {
                // The search took a silent exit from the predicted state (no straight ahead, too short, a gap
                // refusal): nothing to judge. The cadence restarts from here, as after a live search's exit.
                c->stage=PRE_IDLE;c->deferred=0;m->straight.time=level.time;
                searchTally[client].walkEmpty++;return 1;
            }
            searchTally[client].chunks++;
            if(c->stage==PRE_READY)searchTally[client].ready++;
        }
        return 1;
    }
    return 0;
}

// The frame's end (G_RunFrame): the chunks the boundaries owed, within what is left of the frame's step budget.
void Move_PoolFrameEnd(void) {
    int stop=PoolFrameEndSteps(),i,k,n=0,order[MAX_CLIENTS],scopes;
    // Earliest deadline first (the take frame), then client order: a chunk the frame has room for goes to the
    // deferral that is soonest due, not to the lowest client number.
    for(i=0;i<MAX_CLIENTS && stop>0;i++)if(poolOwed[i]==level.time+1) {
        for(k=n;k>0 && searchCache[order[k-1]].targetTime>searchCache[i].targetTime;k--)order[k]=order[k-1];
        order[k]=i;n++;
    }
    scopes=n>0 && PoolScopes();
    for(k=0;k<n;k++) {
        gentity_t *ent;double began;
        i=order[k];ent=&g_entities[i];
        poolOwed[i]=0;
        if(!ent->inuse || !ent->client || ent->health<=0)continue;
        ForbidSync(i);
        if(bot2_meterSteps-poolFrameSteps>=(unsigned int)stop) {
            searchCache_t *c=&searchCache[i];
            if(c->deferred==2)searchTally[i].dropStarved++;else searchTally[i].walkStarved++;
            continue;
        }
        began=Bot2_FrameMeterClock();
        searchStepFrom=poolFrameSteps;searchStepBudget=(unsigned int)stop;
        // As at the boundary (Bot2_MoveBenchPmove): a missile younger than the reaction delay is not seen yet.
        if(ent->r.svFlags&SVF_BOT)Bot2_ThreatHideBegin();
        if(scopes) {Bot2_ForecastTraceCacheBegin();Bot2_GroundCorridorMemoBegin();}
        {unsigned int meterAt_=bot2_meterSteps;PoolChunk(ent,&poolMove[i],0);Bot2_FrameMeterSource(BOT2_SRC_BUDGET,bot2_meterSteps-meterAt_);}
        if(scopes) {Bot2_GroundCorridorMemoEnd();Bot2_ForecastTraceCacheEnd();}
        if(ent->r.svFlags&SVF_BOT)Bot2_ThreatHideEnd();
        searchStepBudget=0;
        Bot2_FrameMeterAdd(i,began);
    }
    poolFrameSteps=bot2_meterSteps;
}

static void SearchBudgetStep(gentity_t *ent,pmove_t *move,int searched) {
    int client=ent->s.number;movement_t *m=&movement[client];searchCache_t *c=&searchCache[client];
    int budget=SearchBudget();
    if(!budget) {c->stage=PRE_IDLE;c->attemptBoundary=0;c->deferred=0;return;}
    // A drop deferral still here at or after its take frame was not taken (the take runs earlier in the frame): the
    // bot left the edge. It is over, as DropDefer or WalkDefer would end it when next called; left in place it
    // blocked the early pre-search below until a walking deferral cleared it.
    if(c->deferred==2 && level.time>=c->targetTime) {c->deferred=0;c->stage=PRE_IDLE;}
    if(c->deferred==2 || (c->deferred && !searched)) {
        if(PoolFrameEndSteps()>0 && (c->stage==PRE_PREDICTED || c->stage==PRE_JUDGING) && c->targetTime>level.time) {
            poolMove[client]=*move;poolOwed[client]=level.time+1;
            return;
        }
        PoolChunk(ent,move,searched);
        return;
    }
    if(c->stage!=PRE_IDLE && c->divergeFrame<0 && c->trackCount) {
        int k;const playerState_t *ps=&ent->client->ps;
        for(k=1;k<c->trackCount;k++)if(c->trackTime[k]==level.time)break;
        if(k<c->trackCount && (memcmp(ps->origin,c->trackOrigin[k],sizeof(vec3_t)) || memcmp(ps->velocity,c->trackVelocity[k],sizeof(vec3_t)))) {
            const usercmd_t *a=&ent->client->pers.cmd,*b=&c->trackCmd[k-1];
            c->divergeFrame=k;
            Com_sprintf(c->divergeWhat,sizeof(c->divergeWhat),"cmd=%d,%d,%d/%d,%d,%d yaw=%d/%d pitch=%d/%d buttons=%d/%d",
                a->forwardmove,a->rightmove,a->upmove,b->forwardmove,b->rightmove,b->upmove,a->angles[1],b->angles[1],
                a->angles[0],b->angles[0],a->buttons,b->buttons);
            c->divergeCmdSame=a->forwardmove==b->forwardmove && a->rightmove==b->rightmove && a->upmove==b->upmove &&
                a->angles[0]==b->angles[0] && a->angles[1]==b->angles[1] && a->buttons==b->buttons;
        }
    }
    if(SearchPlayerNear(ent)) {
        if(c->stage!=PRE_IDLE)searchTally[client].abandoned++;
        c->stage=PRE_IDLE;c->attemptBoundary=0;searchTally[client].skippedNear++;return;
    }
    if(searched) {
        // The search has just moved the boundary: a commit gives a new flight
        // and a refusal a new retry, so the previous attempt says nothing.
        c->stage=PRE_IDLE;c->attemptBoundary=0;c->attemptPlan=-1;
        c->attemptBoundary=SearchNextBoundary(m);c->attemptPlan=m->plan.id;
        SearchPredict(ent,move);
        return;
    }
    if(c->stage==PRE_IDLE || c->stage==PRE_READY) {
        int boundary;
        // A ready pre-search waits for the boundary it was built for.
        if(c->stage==PRE_READY && c->targetTime>level.time)return;
        boundary=SearchNextBoundary(m);
        // One prediction per boundary, and none at all when there is no next
        // boundary to predict. Without this the walk re-predicts the same
        // retry frame on every frame until it arrives.
        if(!boundary || (boundary==c->attemptBoundary && m->plan.id==c->attemptPlan))return;
        c->attemptBoundary=boundary;c->attemptPlan=m->plan.id;
        SearchPredict(ent,move);
        if(c->stage!=PRE_PREDICTED)return;
    }
    if(c->targetTime<=level.time) {c->stage=PRE_IDLE;return;}
    if(c->stage!=PRE_PREDICTED && c->stage!=PRE_JUDGING)return;
    // The boundary this pre-search was built for is no longer the one the
    // search will run at: abandon it now rather than pay for the rest of it.
    if(SearchNextBoundary(m)!=c->attemptBoundary || m->plan.id!=c->attemptPlan) {
        c->stage=PRE_IDLE;searchTally[client].abandoned++;return;
    }
    searchRollouts=0;searchRolloutBudget=SearchChunkBudget(c,budget);
    SearchChunk(ent,move);
    searchRolloutBudget=0;
    searchTally[client].chunks++;
    if(c->stage==PRE_READY)searchTally[client].ready++;
    (void)m;
}


// ---------------------------------------------------------------------------
// Offline exploration (bot_moveExplore). The straight search learns strafe
// edges only where a live run happened to land, so the state search plans
// over an accidental edge set. Exploration runs the same candidate
// rollouts and the same judge from every cell of the route corridor ahead
// of time. The bot stands idle at its start; each frame its player state is
// placed at the next cell in one of a set of entry states (at rest, or a
// landing at a speed band and a heading about the path), the path is
// searched from there, the walk's first step is taken so the executor
// stands as it would live, the straight candidates are rolled out and
// judged exactly as they are live, and every admitted hop is learned as a
// strafe edge with the state its rollout measured. The player state is
// restored before the live Pmove, so the bot never moves. The corridor is
// every cell within reach of the state plan from the start; when it is
// exhausted the plan is searched again, and a plan that leaves the explored
// corridor widens it, until it stays inside or the round limit is reached.
// The cache is written once at the end. A landing state is explored only
// with a tap at once: a landing that walks first is the rest state's.
// ---------------------------------------------------------------------------
#define EXPLORE_CORRIDOR 96.0f          // cells this close to a path cell are explored
#define EXPLORE_HEIGHT 64.0f            // and within this height of it
#define EXPLORE_PATH_AHEAD 64.0f        // the path heading at a cell is read this far along it
#define EXPLORE_MAX_CELLS 32768
#define EXPLORE_ROUNDS_DEFAULT 4
#define EXPLORE_BATCH_DEFAULT 4
static const float exploreSpeeds[]={220,280,340,400,460};
static const float exploreHeadings[]={-30,0,30};
#define EXPLORE_STATES (1+(int)ARRAY_LEN(exploreSpeeds)*(int)ARRAY_LEN(exploreHeadings))

static struct {
    int active,done,client,round,index,state,count,explored,states,searches,rollouts,admitted,learned,merged,declined,noPath,noStraight,began;
    // The cost of the search itself, against the wall clock it ran in.
    // Rollout microseconds over elapsed wall time is the whole question:
    // near 1 means the search is CPU-bound and 600 rollouts/sec is the
    // ceiling; well under 1 means it is bound by the server frame loop's
    // batch allowance and the ceiling is much higher.
    double us;int traces,simFrames;
    int cells[EXPLORE_MAX_CELLS];
    unsigned char visited[EXPLORE_MAX_CELLS];
    float pathDir[2];int pathDirOk;
} explore;

static int ExploreEnabled(int client) {
    return client>=0 && client<MAX_CLIENTS && trap->Cvar_VariableIntegerValue("bot_moveExplore")>0 &&
           MoveSearchGates(client) && !MoveGuest(client) && movement[client].objectiveSet;
}

static void ExploreReset(int client) {
    if(explore.active && explore.client==client) {
        Graph_Autosave(1);
        memset(&explore,0,sizeof(explore));
    }
}

// Every unvisited cell within the corridor of the path joins the queue.
static int ExploreCorridor(const bot2_movePath_t *path) {
    int i,k,added=0,n=Graph_CellCount();
    float radius;char value[16];
    trap->Cvar_VariableStringBuffer("bot_moveExploreRadius",value,sizeof(value));
    radius=(float)atof(value);
    if(radius<=0)radius=EXPLORE_CORRIDOR;
    for(i=0;i<n && i<EXPLORE_MAX_CELLS;i++) {
        const graphCell_t *c;
        if(explore.visited[i] || explore.count>=EXPLORE_MAX_CELLS)continue;
        c=Graph_Cell(i);
        for(k=0;k<path->count;k++) {
            const graphCell_t *p=Graph_Cell(path->cells[k]);float dx,dy;
            if(fabsf(p->z-c->z)>EXPLORE_HEIGHT)continue;
            dx=(p->ix-c->ix)*(float)GRAPH_GRID;dy=(p->iy-c->iy)*(float)GRAPH_GRID;
            if(dx*dx+dy*dy<=radius*radius) {explore.visited[i]=1;explore.cells[explore.count++]=i;added++;break;}
        }
    }
    return added;
}

// Every hop an admitted candidate's rollout landed is learned as LearnStrafe
// learns a completed plan's. The first hop is a landing-frame tap when the
// root was a landing state and the hop was tapped at once; the second always.
static void ExploreLearn(const straightResult_t *r,int rootLanding) {
    int h;
    for(h=0;h<r->candidate.count && h<2;h++) {
        const spikeMeasure_t *x=&r->measure;int from,to,edge,merged=0,tap,cost;
        if(x->land[h]<0 || x->launch[h]<0)continue;
        from=Graph_CellAt(x->launchPos[h],STEPSIZE+.25f);
        if(from<0)from=Graph_Nearest(x->launchPos[h],GRAPH_GRID,2*STEPSIZE);
        to=Graph_CellAt(x->landPos[h],STEPSIZE+.25f);
        if(to<0)to=Graph_Nearest(x->landPos[h],GRAPH_GRID,2*STEPSIZE);
        if(from<0 || to<0 || from==to)continue;
        cost=(x->land[h]-x->launch[h])*Bot2_GameStepMsec();
        tap=h>0?1:(rootLanding && r->candidate.hop[0].runup==0);
        edge=Graph_AddStrafeEdge(from,to,x->launchPos[h],x->landPos[h],r->candidate.hop[h].dir,cost,tap,
                                 x->launchSpeed[h],x->launchDir[h],x->landSpeed[h],x->landDir[h],&merged);
        if(edge>=0) {if(merged)explore.merged++;else explore.learned++;}
    }
}

// One entry state at one cell: place the player state, plan from there,
// take the walk's first step, roll the straight candidates out and judge
// them, learn the admitted ones, and restore everything the frame touched.
static void ExploreState(gentity_t *ent,pmove_t *move,int cell,int state) {
    movement_t *m=&movement[ent->s.number];playerState_t *ps=&ent->client->ps;
    static straightResult_t results[ARRAY_LEN(straightHops)*ARRAY_LEN(straightAims)*ARRAY_LEN(straightRunups)];
    playerState_t savedPS=*ps;usercmd_t savedCmd=move->cmd;bot2_moveExecutor_t savedExec=m->executor,savedBefore=m->executorBefore;
    bot2_movePlan_t savedPlan=m->plan;bot2_movePermit_t savedPermit=m->permit;
    bot2_moveContext_t ctx;usercmd_t step;const char *reason;bot2_moveExecutor_t before,after;straightVerdict_t v;
    vec3_t origin,end,point,angles,savedAngles;float ref[2],length,us=0,speed=0;double usBefore;
    int walked,far,a,b,h,i,candidates=0,traces=0,landing=state>0,simFramesBefore;
    int counts[CANDIDATE_OUTCOMES];
    VectorCopy(ent->s.angles,savedAngles);
    CellPoint(cell,origin);
    VectorCopy(origin,ps->origin);VectorClear(ps->velocity);
    ps->groundEntityNum=ENTITYNUM_WORLD;ps->pm_flags&=~(PMF_JUMP_HELD|PMF_TIME_KNOCKBACK|PMF_TIME_WATERJUMP);ps->pm_time=0;
    memset(&m->executor,0,sizeof(m->executor));m->executor.pathIndex=-1;m->plan.count=0;m->permit.active=0;
    VectorClear(angles);angles[YAW]=ps->viewangles[YAW];
    if(landing) {
        float off=exploreHeadings[(state-1)%(int)ARRAY_LEN(exploreHeadings)]*M_PI/180,c,s;
        speed=exploreSpeeds[(state-1)/(int)ARRAY_LEN(exploreHeadings)];
        if(!explore.pathDirOk)goto restore;
        c=cosf(off);s=sinf(off);
        ps->velocity[0]=speed*(explore.pathDir[0]*c-explore.pathDir[1]*s);
        ps->velocity[1]=speed*(explore.pathDir[0]*s+explore.pathDir[1]*c);
        angles[YAW]=atan2f(ps->velocity[1],ps->velocity[0])*180/M_PI;
    }
    SetClientViewAngle(ent,angles);
    Replan(ent->s.number);
    if(!m->path.count) {explore.noPath++;goto restore;}
    if(!landing) {
        // The path's heading from this cell, for its landing states.
        PathPointAt(&m->path,0,EXPLORE_PATH_AHEAD,point);
        ref[0]=point[0]-origin[0];ref[1]=point[1]-origin[1];length=sqrtf(ref[0]*ref[0]+ref[1]*ref[1]);
        explore.pathDirOk=length>=1;
        if(explore.pathDirOk) {explore.pathDir[0]=ref[0]/length;explore.pathDir[1]=ref[1]/length;}
    }
    VectorCopy(m->objective,ctx.goal);ctx.client=ent->s.number;ctx.stepMsec=Bot2_GameStepMsec();ctx.plan=&m->plan;ctx.path=&m->path;ctx.permit=&m->permit;
    m->executorBefore=m->executor;
    walked=MoveStep(ps,&ctx,&m->executor,level.time,&step,&reason);
    if(!walked) {explore.declined++;goto restore;}
    m->owned=1;m->frameTime=level.time;
    // The walk the candidates are judged against starts with this frame's
    // own walk command, as it does live.
    move->cmd.forwardmove=step.forwardmove;move->cmd.rightmove=step.rightmove;move->cmd.upmove=step.upmove;
    move->cmd.angles[YAW]=step.angles[YAW];move->cmd.angles[PITCH]=step.angles[PITCH];move->cmd.angles[ROLL]=step.angles[ROLL];
    far=StraightAhead(&m->path,0);
    if(far<0) {explore.noStraight++;goto restore;}
    CellPoint(m->path.cells[far],end);
    ref[0]=end[0]-ps->origin[0];ref[1]=end[1]-ps->origin[1];length=sqrtf(ref[0]*ref[0]+ref[1]*ref[1]);
    if(length<MOVE_STRAIGHT_MIN || GoalDistance(ps,m->objective)<MOVE_STRAIGHT_GOAL) {explore.noStraight++;goto restore;}
    ref[0]/=length;ref[1]/=length;
    before=m->executorBefore;after=m->executor;
    before.stage=after.stage=MOVE_STAGE_RUNUP0;before.launchTime=after.launchTime=0;before.airborne=after.airborne=0;
    for(h=0;h<(int)ARRAY_LEN(straightHops);h++)for(a=0;a<(int)ARRAY_LEN(straightAims);a++)for(b=0;b<(int)ARRAY_LEN(straightRunups);b++) {
        bot2_movePlan_t *candidate;float v0,d0,d1;vec3_t aim0,aim1;
        if(landing && straightRunups[b]>0)continue;
        candidate=&results[candidates].candidate;
        StraightResultReset(&results[candidates],0);
        candidate->count=straightHops[h];candidate->kind=MOVE_PLAN_STRAIGHT;candidate->edge=-1;candidate->edge2=-1;candidate->id=spikePlans+1;VectorCopy(ps->origin,candidate->origin);
        v0=straightRunups[b]>0 && !runupKeep?Wishspeed(ps):max(Wishspeed(ps),Speed2D(ps));
        d0=(MOVE_HOP_SLOPE*v0+MOVE_HOP_OFFSET)*straightAims[a];
        d1=MOVE_HOP_SLOPE*(v0+MOVE_HOP_LAND_GAIN)+MOVE_HOP_OFFSET;
        PathPointAt(&m->path,0,straightRunups[b]+d0,aim0);
        PathPointAt(&m->path,0,straightRunups[b]+d0+d1,aim1);
        AimAt(ps->origin,aim0,ref,candidate->hop[0].dir);candidate->hop[0].runup=straightRunups[b];
        AimAt(aim0,aim1,candidate->hop[0].dir,candidate->hop[1].dir);candidate->hop[1].runup=0;
        candidate->anchor1[0]=aim0[0];candidate->anchor1[1]=aim0[1];candidate->anchored1=1;
        candidates++;
    }
    explore.searches++;explore.rollouts+=candidates;
    exploreQuiet=!trap->Cvar_VariableIntegerValue("bot_moveExploreVerbose");
    simFramesBefore=parity.rolloutFrames;usBefore=parity.rolloutUs;
    {judgeState_t js;memset(&js,0,sizeof(js));
    StraightJudge(ent,move,results,candidates,0,&before,&after,ref,counts,&us,&traces,&v,&js);}
    exploreQuiet=0;
    // The us out-param is a float, and a float accumulator loses microseconds
    // once the total passes its mantissa; the run's total is taken from the
    // double counter instead.
    explore.us+=parity.rolloutUs-usBefore;explore.traces+=traces;
    explore.simFrames+=parity.rolloutFrames-simFramesBefore;
    if(v.best!=-2)for(i=0;i<candidates;i++)if(results[i].outcome && StraightAdmitted(results[i].outcome)) {explore.admitted++;ExploreLearn(&results[i],landing);}
restore:
    VectorCopy(savedAngles,ent->s.angles);
    *ps=savedPS;move->cmd=savedCmd;m->executor=savedExec;m->executorBefore=savedBefore;m->plan=savedPlan;m->permit=savedPermit;
    m->path.count=0;m->path.replan=0;m->frameTime=level.time;m->owned=1;
}

static void ExploreBegin(gentity_t *ent) {
    movement_t *m=&movement[ent->s.number];
    memset(&explore,0,sizeof(explore));
    explore.active=1;explore.client=ent->s.number;explore.began=trap->Milliseconds();
    Graph_Autosave(0);
    Replan(ent->s.number);
    ExploreCorridor(&m->path);
    trap->Print("[EXPLORE] begin client=%d cells=%d path_cells=%d path_cost_ms=%d edges=%d\n",
        explore.client,explore.count,m->path.count,m->path.costMs,Graph_EdgeCount());
    m->path.count=0;
}

static void ExploreFinish(gentity_t *ent) {
    movement_t *m=&movement[ent->s.number];
    Graph_Autosave(1);
    if(!Graph_Save())trap->Print("[EXPLORE] cache_write_failed\n");
    Replan(ent->s.number);
    explore.done=1;
    trap->Print("[EXPLORE] done client=%d rounds=%d cells=%d states=%d searches=%d rollouts=%d admitted=%d learned=%d merged=%d declined=%d no_path=%d no_straight=%d edges=%d elapsed_ms=%d rollout_us=%.0f sim_frames=%d traces=%d plan_cells=%d plan_strafe=%d plan_cost_ms=%d plan_state=%d expanded=%d overflow=%d\n",
        explore.client,explore.round,explore.explored,explore.states,explore.searches,explore.rollouts,explore.admitted,explore.learned,explore.merged,
        explore.declined,explore.noPath,explore.noStraight,Graph_EdgeCount(),trap->Milliseconds()-explore.began,
        explore.us,explore.simFrames,explore.traces,
        m->path.count,m->path.strafe,m->path.costMs,m->path.state,m->path.expanded,m->path.overflow);
    m->path.count=0;
}

static void ExploreStep(gentity_t *ent,pmove_t *move) {
    movement_t *m=&movement[ent->s.number];int n,batch=trap->Cvar_VariableIntegerValue("bot_moveExploreBatch");
    if(batch<1)batch=EXPLORE_BATCH_DEFAULT;
    if(explore.done)return;
    if(!explore.active || explore.client!=ent->s.number)ExploreBegin(ent);
    if(ent->client->ps.groundEntityNum!=ENTITYNUM_WORLD)return;
    for(n=0;n<batch && !explore.done;n++) {
        if(explore.index>=explore.count) {
            int rounds=trap->Cvar_VariableIntegerValue("bot_moveExploreRounds"),added=0;
            if(rounds<1)rounds=EXPLORE_ROUNDS_DEFAULT;
            explore.round++;
            Replan(ent->s.number);
            if(explore.round<rounds)added=ExploreCorridor(&m->path);
            trap->Print("[EXPLORE] round=%d added=%d cells=%d path_cells=%d path_strafe=%d path_cost_ms=%d edges=%d elapsed_ms=%d\n",
                explore.round,added,explore.count,m->path.count,m->path.strafe,m->path.costMs,Graph_EdgeCount(),trap->Milliseconds()-explore.began);
            m->path.count=0;
            if(!added) {ExploreFinish(ent);return;}
        }
        ExploreState(ent,move,explore.cells[explore.index],explore.state);
        explore.states++;
        if(++explore.state>=EXPLORE_STATES) {
            explore.state=0;explore.index++;explore.explored++;
            if(explore.explored%50==0)trap->Print("[EXPLORE] progress cells=%d/%d admitted=%d learned=%d merged=%d edges=%d elapsed_ms=%d rollout_us=%.0f sim_frames=%d\n",
                explore.explored,explore.count,explore.admitted,explore.learned,explore.merged,Graph_EdgeCount(),trap->Milliseconds()-explore.began,
                explore.us,explore.simFrames);
        }
    }
}

static int SpikeEligible(gentity_t *ent) {
    movement_t *m=&movement[ent->s.number];
    if(!trap->Cvar_VariableIntegerValue("bot_moveSpike"))return 0;
    if(!m->objectiveSet || m->plan.attempted || !m->owned || m->frameTime!=level.time)return 0;
    if(!m->executor.walking || m->executor.stage!=MOVE_STAGE_RUNUP0)return 0;
    if(ent->client->ps.groundEntityNum!=ENTITYNUM_WORLD)return 0;
    return 1;
}

static void MoveParityBeforePmove(gentity_t *ent,pmove_t *move);

// One bot's Pmove boundary is the trace cache's scope: every search, pre-search
// chunk and prediction below runs inside it, against a world nothing moves
// until the live Pmove that follows.
void Bot2_MoveParityBeforePmove(gentity_t *ent,pmove_t *move) {
    Bot2_WallProbe(ent,move);   // inert unless bot_moveWallProbe names a pose
    Bot2_ForecastTraceCacheBegin();Bot2_GroundCorridorMemoBegin();
    MoveParityBeforePmove(ent,move);
    Bot2_GroundCorridorMemoEnd();Bot2_ForecastTraceCacheEnd();
}

static void MoveParityBeforePmove(gentity_t *ent,pmove_t *move) {
    movement_t *m;int client=ent->s.number,horizon,searched=0,harness;char detail[320],what[96];const char *field;
    // This boundary is the movement work that runs outside the command
    // dispatch the cost fixture times, so it is charged to the same frame and
    // the same client. Inert with no capture active: the clock returns 0.
    double costBegan;
    if(client<0 || client>=MAX_CLIENTS || !ent->client)return;
    m=&movement[client];
    ForbidSync(client);
    // Discovery is dispatched from this boundary, so every return before it
    // is a frame that searched nothing. On a landing frame that matters, and
    // each of these used to be silent.
    if(!MoveSearchGates(client)) {
        if(LandedThisFrame(m))ReplaySkip(ent,"no_search_gates");
        if(parity.armed)ParityDisarm();
        return;
    }
    costBegan=Bot2_MoveCostClock();
    // The harness's own horizon. Zero means it is not armed and nothing below
    // compares or builds; the searches do not consult it.
    horizon=ParityHorizon(client);
    if(ExploreEnabled(client)) {
        if(LandedThisFrame(m))ReplaySkip(ent,"explore");
        if(parity.armed)ParityDisarm();ExploreStep(ent,move);
        Bot2_MoveCostPreparation(client,costBegan);return;
    }
    // A guest's boundary searches but never reads or ends the runner's
    // harness: every parity block below is the harness client's own.
    harness=!MoveGuest(client);
    if(harness && parity.armed && parity.client!=client)ParityDisarm();
    // A frame the executor did not own ends the horizon. The forecast must
    // have declined at the same boundary, or it predicted a walk that never
    // happened.
    if(m->frameTime!=level.time || !m->owned) {
        // A landing the executor did not own this frame still searched
        // nothing; StraightLanding alone cannot see it, so it is named here.
        if(m->frameTime==level.time && StraightLanding(m))ReplaySkip(ent,"frame_not_owned");
        if(harness && parity.armed) {
            if(parity.index<parity.count) {
                parity.mismatches++;
                Com_sprintf(detail,sizeof(detail),"frame=%d predicted_frames=%d",parity.index,parity.count);
                ParityEvent(client,"live_declined",detail);
            }
            ParityDisarm();
        }
        // A hit that knocked the bot off its route leaves the walk declining
        // (no path, an off-mesh yield), and that is where a landing matters
        // most: the pending recovery still searches, and a commit issues this
        // frame's command itself. The next frame's recovery owns the frame.
        if(m->frameTime==level.time && m->knock.pending && level.time>=m->knock.searchTime)METER_SRC(BOT2_SRC_KNOCK,KnockSearch(ent,move));
        Bot2_MoveCostPreparation(client,costBegan);return;
    }
    if(harness && parity.armed && parity.index>=parity.count) {
        if(parity.completion==BOT2_FORECAST_COMPLETION_POLICY) {
            parity.mismatches++;
            Com_sprintf(detail,sizeof(detail),"frame=%d predicted_frames=%d",parity.index,parity.count);
            ParityEvent(client,"forecast_declined",detail);
        }
        ParityDisarm();
    }
    if(harness && parity.armed) {
        const bot2_forecastFrame_t *frame=&parity.built[parity.index];
        if(frame->serverTime!=level.time) {
            parity.mismatches++;
            Com_sprintf(detail,sizeof(detail),"frame=%d live_time=%d predicted_time=%d",parity.index,level.time,frame->serverTime);
            ParityEvent(client,"boundary_time",detail);ParityDisarm();
        } else if((field=MovementStateDiff(&ent->client->ps,&frame->beforePS))!=NULL) {
            const playerState_t *l=&ent->client->ps,*p=&frame->beforePS;
            parity.mismatches++;
            Com_sprintf(detail,sizeof(detail),"frame=%d field=%s live_force=%d predicted_force=%d live_active=%d predicted_active=%d live_regen=%d predicted_regen=%d live_duration=%d predicted_duration=%d",
                parity.index,field,l->fd.forcePower,p->fd.forcePower,l->fd.forcePowersActive,p->fd.forcePowersActive,
                l->fd.forcePowerRegenDebounceTime,p->fd.forcePowerRegenDebounceTime,l->fd.forcePowerDuration[FP_SPEED],p->fd.forcePowerDuration[FP_SPEED]);
            ParityEvent(client,"before_state",detail);ParityDisarm();
        } else if(CommandDiff(&move->cmd,frame,what,sizeof(what))) {
            parity.mismatches++;
            Com_sprintf(detail,sizeof(detail),"frame=%d %s",parity.index,what);
            ParityEvent(client,"command",detail);ParityDisarm();
        } else if(memcmp(&m->executor,frame->afterPolicyExtra,sizeof(m->executor))) {
            const bot2_moveExecutor_t *p=(const bot2_moveExecutor_t*)frame->afterPolicyExtra;
            parity.mismatches++;
            Com_sprintf(detail,sizeof(detail),"frame=%d live_yaw=%.3f predicted_yaw=%.3f live_decline=%d predicted_decline=%d live_stage=%d predicted_stage=%d",
                parity.index,m->executor.yaw,p->yaw,m->executor.decline,p->decline,m->executor.stage,p->stage);
            ParityEvent(client,"executor_state",detail);ParityDisarm();
        }
    }
    // Discovery happens at the same boundary, from the same root, before the
    // committed sequence's own horizon is built. Not on the frame a Speed
    // purchase is requested: a fresh candidate issues its own first command
    // without the purchase, so it would be judged at the wrong wishspeed.
    if(m->budget.speedRequest==level.time) {
        if(LandedThisFrame(m))ReplaySkip(ent,"speed_request");
    }
    // A knock recovery pending or committed takes the boundary: no sequence
    // is chosen from a state the hit is still carrying.
    if((m->knock.pending || (m->knock.active && ent->client->ps.groundEntityNum==ENTITYNUM_NONE)) && level.time>=m->knock.searchTime)
        METER_SRC(BOT2_SRC_KNOCK,KnockSearch(ent,move));
    if(m->knock.pending || m->knock.active) {
        Bot2_MoveCostPreparation(client,costBegan);
        RoutePendingPrint(client);
        return;
    }
    searched=0;
    if(m->budget.speedRequest!=level.time) {
        int edge;
        // The spike and planned searches take the frame instead of the
        // straight search. Both are searches, but neither offers the straight
        // search's landing candidates, so a landing taken by one is reported.
        if(SpikeEligible(ent)) {if(LandedThisFrame(m))ReplaySkip(ent,"spike_took_frame");METER_SRC(BOT2_SRC_SPIKE,SpikeSearch(ent,move));}
        else if(PlannedEligible(ent,&edge)) {if(LandedThisFrame(m))ReplaySkip(ent,"planned_took_frame");METER_SRC(BOT2_SRC_PLANNED,if(!DropDefer(ent,move,edge))PlannedSearch(ent,move,edge));}
        else {
            // The gap search first: a committed gap plan or an admitted
            // permit leaves nothing for the straight search this frame.
            // The straight search carries the planned chain's next hop as
            // candidates of its own when the bot is at its takeoff.
            if(GapEligible(ent))METER_SRC(BOT2_SRC_GAP,GapSearch(ent,move));
            if(StraightEligible(ent)) {
                int deferred;
                METER_SRC(BOT2_SRC_WALKDEFER,deferred=WalkDefer(ent,move));
                if(!deferred) {
                    straightExit=NULL;
                    METER_SRC(BOT2_SRC_STRAIGHT,StraightSearch(ent,move));searched=1;walkDeferTake=0;
                    if(straightExit) {ReplaySkip(ent,straightExit);straightExit=NULL;}
                }
            }
        }
    }
    // The harness builds a prediction only when it is armed by its own cvar.
    if(harness && horizon>0 && !parity.armed)ParityBuild(ent,move,horizon);
    // And the budget carries the next search's rollouts onto the frames
    // before it. Inert with bot_moveSearchBudget 0.
    METER_SRC(BOT2_SRC_BUDGET,SearchBudgetStep(ent,move,searched));
    Bot2_MoveCostPreparation(client,costBegan);
    // After the searches have returned and outside the cost fixture's charge:
    // the [ROUTE] line of a route a commit installed at this boundary.
    RoutePendingPrint(client);
}

void Bot2_MoveParityAfterPmove(gentity_t *ent,pmove_t *move) {
    int client=ent->s.number;char detail[160];const char *field;
    if(client>=0 && client<MAX_CLIENTS && ent->client) {
        // What this Pmove left, for the next command's hit test.
        movement_t *k=&movement[client];const playerState_t *ps=&ent->client->ps;int i;
        VectorCopy(ent->client->ps.velocity,k->knock.afterVelocity);
        k->knock.afterTime=level.time;k->knock.afterFlags=ent->client->ps.eFlags;k->knock.afterJumppad=ent->client->ps.jumppad_ent;
        // What it touched, for failure attribution (LiveCause): a player or a
        // missile, or a push or knockback, is transient; a mover not at rest
        // is a mover.
        for(i=0;move && i<move->numtouch;i++) {
            int n=move->touchents[i];
            if(n<0 || n>=ENTITYNUM_WORLD || n==client)continue;
            if(n<MAX_CLIENTS || g_entities[n].s.eType==ET_MISSILE || g_entities[n].s.eType==ET_NPC)k->contact.transient=level.time;
            if(n<MAX_CLIENTS && ps->groundEntityNum==ENTITYNUM_NONE) {k->contact.body=level.time;k->contact.bodyClient=n;}
            else if(g_entities[n].s.eType==ET_MOVER && !Bot2_StandableEntity(n))k->contact.mover=level.time;
        }
        if(ps->pm_flags&PMF_TIME_KNOCKBACK)k->contact.transient=level.time;
        if(ps->otherKillerTime>k->contact.otherKillerTime) {
            if(ps->otherKillerTime>level.time)k->contact.transient=level.time;   // a new push, grip or hit
            k->contact.otherKillerTime=ps->otherKillerTime;
        }
        if(k->state.avoidCount)AvoidExpire(client);
    }
    if(!parity.armed || client!=parity.client || parity.index>=parity.count)return;
    if((field=MovementStateDiff(&ent->client->ps,&parity.built[parity.index].afterPS))!=NULL) {
        const playerState_t *live=&ent->client->ps,*predicted=&parity.built[parity.index].afterPS;
        parity.mismatches++;
        // Both values of the two fields that differ in practice, so a
        // mismatch can be placed without a rerun.
        Com_sprintf(detail,sizeof(detail),"frame=%d field=%s flags=%x/%x ground=%d/%d dz=%.3f dxy=%.3f",parity.index,field,
            live->pm_flags,predicted->pm_flags,live->groundEntityNum,predicted->groundEntityNum,
            live->origin[2]-predicted->origin[2],
            (float)sqrt((live->origin[0]-predicted->origin[0])*(live->origin[0]-predicted->origin[0])+
                        (live->origin[1]-predicted->origin[1])*(live->origin[1]-predicted->origin[1])));
        ParityEvent(client,"after_state",detail);ParityDisarm();
        return;
    }
    parity.compared++;parity.index++;
    // A periodic tally, so a clean run still leaves a measured count; every
    // horizon that covered a committed sequence reports, so the composition
    // gate can see the frames the plan was compared over.
    if(parity.index>=parity.count &&
       (parity.completion==BOT2_FORECAST_COMPLETION_HORIZON || parity.truncated) &&
       (parity.rollouts%10==0 || parity.planned)) {
        Com_sprintf(detail,sizeof(detail),"frames=%d mismatches=%d refusals=%d rollouts=%d horizon=%d planned=%d truncated=%d",
            parity.compared,parity.mismatches,parity.refusals,parity.rollouts,parity.count,parity.planned,parity.truncated);
        ParityEvent(client,"horizon",detail);
    }
}
