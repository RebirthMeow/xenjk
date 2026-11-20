// Movement rewrite (docs/movement). Phase 0: one state, one reset, a walk-only
// executor that reads the navmesh waypoint through the retained ground
// corridor check, and a forecast-versus-live parity harness driven by the
// same step function the live executor runs. Phase 1: the composition spike,
// a two-hop sequence discovered by rollout from the live state and executed
// by that same step function. Always on (bot_moveRewrite
// removed); the spike's discovery with bot_moveSpike.
#ifndef BOT2_MOVE_H
#define BOT2_MOVE_H

#include "bot2_graph.h"

typedef enum {
    MOVE_EVENT_NONE,
    MOVE_EVENT_PLAN,
    MOVE_EVENT_EDGE,
    MOVE_EVENT_MISS,
    MOVE_EVENT_WALK,
    MOVE_EVENT_RECOVER,
    MOVE_EVENT_PARITY
} bot2_moveEvent_t;

// Why the executor declined the frame. Zero means it issued a command.
typedef enum {
    MOVE_DECLINE_NONE,
    MOVE_DECLINE_MOVEMENT_TYPE,
    MOVE_DECLINE_LEGACY_STATE,
    MOVE_DECLINE_NO_PATH,
    MOVE_DECLINE_OFFMESH,
    MOVE_DECLINE_CORRIDOR,
    MOVE_DECLINE_MISS
} bot2_moveDecline_t;

// Where the executor is in a committed sequence. Even stages run up to a
// launch, odd stages are the flight that follows; stage 2*count (onward)
// walks from the last landing and 2*count+1 (done) marks useful onward
// travel. A plan of one hop has stages 0..3, a plan of two 0..5.
typedef enum {
    MOVE_STAGE_RUNUP0,
    MOVE_STAGE_FLIGHT0,
    MOVE_STAGE_RUNUP1,
    MOVE_STAGE_FLIGHT1
} bot2_moveStage_t;

// The spend policy: which resource edges a plan may take, independent of
// the pool. bot_moveSpend names them as bits; unset permits every edge.
#define MOVE_SPEND_SPEED 1
#define MOVE_SPEND_HOLD 2

// How a plan was discovered, for telemetry and the retry policy.
typedef enum {
    MOVE_PLAN_NONE,
    MOVE_PLAN_SPIKE,    // phase 1: two hops from the first grounded frame
    MOVE_PLAN_GAP,      // phase 2: one hop across a corridor refusal the lookahead saw
    MOVE_PLAN_EDGE,     // phase 2: a graph edge on the planned path, validated from the live state
    MOVE_PLAN_STRAIGHT, // phase 2: a hop along a straight of the planned path, ahead of the walk by rollout
    MOVE_PLAN_STATE,    // phase 3: a strafe edge chain the state search planned, validated from the live state
    MOVE_PLAN_CLIMB     // a run up a wall along a climb edge, validated from the live state
} bot2_movePlanKind_t;

// Continuation state the executor carries between command frames. The
// forecast rolls it out as its policy state, so it is small, trivially
// copyable and points at nothing. Every field is set from the player state
// and the previous frame's value; nothing here reads the live clock.
typedef struct {
    float yaw;          // controller view yaw after the last command
    int lastTime;       // server time of the last command, 0 before the first
    int walking;        // 1 when the last frame issued a command
    int decline;        // bot2_moveDecline_t of the last frame
    int corridorSample; // corridor sample (1..4) that refused the last walk frame, else 0
    int pathIndex;      // progress along the planned path, -1 off it
    int permitted;      // 1 when the last walk frame crossed a link under a permit
    int offmeshType;    // off-mesh area that caused an offmesh decline
    int braked;         // 1 when the last walk frame braked for its momentum chord
    int held;           // 1 when the last walk frame stood at its own target away from the objective
    vec3_t waypoint;    // navmesh waypoint the last command walked toward, or the corridor refused
    int stage;          // bot2_moveStage_t within the committed sequence
    int launchTime;     // server time of a requested jump awaiting acceptance
    int airborne;       // 1 once a flight stage has seen a frame off the ground
    vec3_t mark;        // last landing; the second run-up and onward travel measure from it
    int prepress;       // 1 when the last flight frame pressed the next hop's tap ahead of its landing
    int side;           // held strafe side in flight (bot_moveFlightMode): +1 right, -1 left, 0 none
    float sideVz;       // vertical velocity of the last flight frame; -1e9 after a grounded frame (mode 2's new-flight test)
    float sideOrigin[2];// where mode 2's current flight began: its hop line runs from here along the hop heading
    int climbFrame;     // commands issued by the committed climb script
    int retreatUntil;   // guests: server time until which the walk backs away from a lip it braked at
    float retreatDir[2];// guests: the direction it backs away along
    // bot_moveFixBM9 (default 0): where the frame that pressed the next tap ahead of its
    // landing (prepress) traced the touchdown; never written with the fix off, so it stays zero.
    vec3_t touch;
} bot2_moveExecutor_t;

// One hop template instance: run toward a heading, tap when the run-up
// distance along it is covered, ride the flight toward the same heading.
typedef struct {
    float dir[2];       // unit horizontal heading
    float runup;        // distance along dir from the stage's reference point before the tap
} bot2_moveHop_t;

// What a committed rollout measured at one hop: the launch and landing
// positions, speeds and velocity headings, and the flight frames.
typedef struct {
    vec3_t takeoff,landing;
    float launchSpeed,landSpeed,launchDir[2],landDir[2];
    int frames,landed;
} bot2_moveMeasured_t;

// A committed sequence. Immutable while it executes; the executor keeps its
// progress in bot2_moveExecutor_t so the forecast can roll the plan out.
typedef struct {
    int count;          // hops committed, 0 when walking
    int id;             // sequence number for telemetry
    int attempted;      // discovery ran for this objective (phase 1 plans once)
    int kind;           // bot2_movePlanKind_t
    int edge;           // graph edge the plan executes (MOVE_PLAN_EDGE), else -1
    vec3_t origin;      // grounded position the plan was discovered from
    vec3_t takeoff,landing; // the committed rollout's launch and landing positions
    int flightFrames;   // frames from launch to landing in that rollout
    bot2_moveHop_t hop[2];
    // Phase 3: what the committed rollout measured at each hop, so a
    // completed hop is learned as a strafe edge with its state: the launch
    // and landing positions, speeds and velocity headings, the flight
    // frames, and whether the first tap was on a landing frame. edge2 is
    // the second edge of a state plan, or -1.
    int edge2,tapAtLanding;
    bot2_moveMeasured_t measured[2];
    // The point hop 1's line runs through (the search's aim point, or the
    // first edge's landing), for mode 2's line steering; anchored1 0 when the
    // construction has none. Hop 0's line runs through origin.
    float anchor1[2];
    int anchored1;
    // A climb plan's script: face hop[0].dir, hold forward, press jump on
    // command climbJump and again climbRepress commands later.
    int climbJump,climbRepress;
    // A slow drop (bot_moveDropSlow): the run-up and the flight ask for
    // approach u/s along the heading instead of the speed there is (0: the
    // usual hop), and with stepOff the run-up never taps; leaving the ground
    // is the launch, and a run-up that covers hop[0].runup still grounded
    // is a miss.
    float approach;
    int stepOff;
    // Where the approach speed starts, along hop[0].dir from origin: before
    // it the run-up is the usual one (0: from the start).
    float approachAt;
    // A planned drop (bot_moveDropPlan): the run-up steers onto the line
    // through origin along hop[0].dir, as the flight follows its line,
    // instead of running along the heading from wherever it started.
    int lineRunup;
} bot2_movePlan_t;

// The planner's path: graph cells from the bot's cell to the objective's,
// with the edge used to reach each cell (-1 for a walk link). Immutable
// while the executor follows it; a replan is a declared discontinuity.
// Phase 3: the state search fills speeds[] with the speed the plan expects
// at each cell and says whether it ran (state), how many strafe edges the
// path takes and what the search cost.
typedef struct {
    int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
    float speeds[GRAPH_MAX_PATH];
    int count,from,to,costMs,time,replans,retryTime,replan;
    int state,strafe,expanded,overflow;
    // A new identity for every search result. An edge index is not an
    // identity of its own: learning, merging and measured-cost updates
    // change an edge record under a fixed index, so a candidate, an edge
    // snapshot and an executed crossing are related through this instead.
    int generation;
    // The planner directives this route was searched under (bot_movePlanAvoid,
    // bot_movePlanRequire; exploration only, all zero when none applied):
    // bit 1 avoid entries were added, bit 2 an edge was required; how many
    // avoid entries were added, the edge required (-1 none), and whether the
    // route takes it (0 when no route did and the ordinary search ran).
    int directive,directiveAvoid,require,requireMet;
    // The state search's time at each cell (timed: filled, by a state search
    // or a reroute joined onto a timed route), and where a reroute that
    // rejoined the route it left took that route's cells on (0 none).
    float times[GRAPH_MAX_PATH];
    int timed,joinedAt;
    // The Speed window the route's search started with (ms left), and whether
    // it was planned around other players' bodies (a replan's first pass).
    int speedMs,bodies;
} bot2_movePath_t;

// A permit to walk a range of the path's links that the corridor refuses
// but the graph admits, granted only by a native rollout of the walk along
// them from the live state. Immutable while it is used; bound to the path
// it was granted on.
typedef struct {
    int active,id,from,to,pathTime;
} bot2_movePermit_t;

// The knock recovery's policy (bot_moveKnockRecover): air control toward one
// heading until grounded, then the walk's brake until stopped. Rolled out and
// executed from the same state, so it carries everything its commands read.
typedef struct {
    float heading;      // air-control heading, degrees
    float yaw;          // controller view yaw, for the brake
    int coast;          // 1: no air input at all
    int landed;         // consecutive grounded commands
} bot2_moveKnockPolicy_t;

// One struct per client, one reset. Plan, recovery and budget are the
// architecture's slots; phase 1 populates the plan and the executor.
typedef struct {
    bot2_movePlan_t plan;
    bot2_moveExecutor_t executor;
    bot2_moveExecutor_t executorBefore;     // state the current frame started from
    struct { vec3_t target; int progressTime; } recovery;
    // Phase 3 resource edges: the spend policy the plan runs under
    // (MOVE_SPEND_* bits from bot_moveSpend), the frame a Speed purchase
    // was requested on and awaits confirmation, the retry after a refusal,
    // and the purchases made for this objective.
    struct { int spend, speedRequest, speedRetry, purchases; } budget;
    // Phase 2 gap discovery: the refusal the walk lookahead last saw, and
    // the cost of looking.
    struct { vec3_t refusal; int frames, searchTime, searches, lookaheads, lookaheadTime; double lookaheadUs; } gap;
    bot2_movePath_t path;
    struct { int edge, searchTime; } planned;   // the path edge last searched for a commit
    struct { int edge, startTime; } pad;        // the pad edge the jumppad state is flying, -1 none
    struct { int edge, generation; } padAhead;  // the path's pad edge last judged from its entry (PadAhead), and the path
    // A lift ride in progress (GRAPH_EDGE_LIFT): its edge (-1 none), the lift,
    // the phase (board, ride, exit), when it and the phase began, and an id.
    struct { int edge, entity, phase, startTime, phaseTime, id; } lift;
    struct { int time, searchTime, searches, plans; } straight;  // straight hops: last search frame, retry, counts
    // Phase 3 state plans: the strafe edge last searched for a commit and
    // its retry, counts, and the edges whose validation failed for this
    // objective, which the next state search avoids. avoidUntil: 0 for the
    // whole objective, else the level time the entry lapses (a failure a
    // player's body or a missile caused, which only this bot avoids and only
    // for MOVE_TRANSIENT_AVOID_MS).
    // staleEdge/staleCount: the planned edge whose entry envelope the live
    // state keeps missing at its takeoff, and how many times in a row. A run
    // of them is a chord the bot does not arrive at in the state it needs.
    struct { int edge, time, searchTime, searches, plans, avoid[32], avoidCount;
             int staleEdge, staleCount, avoidUntil[32]; } state;
    // The live Pmove's last contact with a player or missile (or a push or
    // knockback), and with a mover not at rest: level times, 0 none. What a
    // failed flight or a refused crossing is charged to (Graph_EdgeFail).
    // otherKillerTime: the last ps.otherKillerTime seen, so a new push shows.
    // body: the last Pmove that touched another client and left this one
    // airborne, and bodyClient that client (bot_moveContactKnock, default on).
    struct { int transient, mover, otherKillerTime, body, bodyClient; } contact;
    bot2_movePermit_t permit;
    struct { bot2_moveEvent_t event; char reason[40]; int time; } lastEvent;
    vec3_t objective;
    int objectiveSet;
    // A new objective is held while a committed flight is still in the air
    // (bot_moveArriveSafe): the level time the hold began, 0 when none.
    int objectiveHeld;
    // Grounded frames spent at a landed plan's onward stage without
    // clearing MOVE_ONWARD_DISTANCE from the mark. The stage is what the
    // replan gate waits on, so a bot that cannot make that distance waits
    // for a plan that will never finish.
    int onwardHeld;
    int frameTime;      // level.time of the last Move_Command call
    int owned;          // 1 when that call issued a command
    // Sequence commits since the last Move_Reset, which the fixture's corner
    // start performs. It names a decision independently of the start level,
    // which is what bot_moveSelectNth overrides against.
    int commits;
    // A walk decline the movement owner holds instead of handing the frame to
    // the legacy owner: the path generation and route crossing it last avoided
    // for a decline (so one route is charged once), whether the last frame was
    // held, and the counts of held frames and hold episodes.
    // stuckSince/stuckOrigin/bailUntil/bails: bot_moveStuckBail's state for a hold that does not move.
    struct { int generation, edge, active, frames, episodes, stepOff, stuckSince, bailUntil, bails; vec3_t stuckOrigin; } declineHold;   // stepOff: cell + 1 being walked to, 0 none
    // With bot_movePlanRequire applying to every plan of the leg
    // (bot_movePlanGen 0): set once the bot has flown or passed the required
    // edge, after which no later plan of the leg requires it again.
    int requireSpent;
    // A hit (velocity changed between the last Pmove and this command) voids
    // a committed sequence and asks the boundary for a landing that can stop
    // on the graph; while one is committed the frame is its policy's. `after`
    // is the state the last Pmove left.
    struct {
        int pending,active,id,startTime,searchTime,hitTime;
        int afterTime,afterFlags,afterJumppad;vec3_t afterVelocity;
        bot2_moveKnockPolicy_t policy;
    } knock;
} movement_t;

// The start phase a tape library key carries: level.time modulo the least
// common multiple of the 25 ms server frame and the 8 ms fixed Pmove step,
// which is the start class the decision-tape collection measured.
#define MOVE_TAPE_PHASE 200

qboolean Move_PrepareMap(void);                     // build or load the graph
int      Move_Plan(int client, const vec3_t goal);  // 0 walk, 1 plan
qboolean Move_Command(int client, usercmd_t *cmd);  // executor or recovery
void     Move_Reset(int client);
// A fixture scenario's recorded knock: arm the knock recovery after a reset.
void     Move_DeclareKnock(int client);
// The movement's own command for a guest this frame, before its caller turns
// or replaces it (bot_moveGuestTrace diagnostic).
void     Move_GuestRawCommand(int client,const usercmd_t *cmd);
// Whether this command's Pmove runs on the ground for any substep: standing,
// or touching down within the command frame.
int      Move_GroundedThisFrame(int client);
// Whether a committed sequence is between the start of its run-up and its
// last landing: plan.count>0 and the executor has not yet reached Onward.
// A guest (bot2_defender.c) uses this to decide whether its own layer may
// safely rewrite the movement's command this frame (bot_defenderKeepSequence).
qboolean Move_SequenceActive(int client);
// The deferred searches' pool chunks owed at the frame's end (bot_moveWalkPoolFrameEnd); G_RunFrame calls it last.
void     Move_PoolFrameEnd(void);
// The guest momentum brake's floor test for a guest layer's own command: 2 when
// the floor ends within the bot's stopping distance along its velocity, 1 when
// along yaw (or within 48 u), else 0 (bot2_move.c).
int      Move_GuestEdgeAhead(int client,float yaw);
int      Move_GuestReachAhead(int client,const vec3_t to);   // a guest's straight walk onto a point
// The graph cell a position plans from: the cell it stands in, or the
// nearest one within reach. One definition, because the planner's root cell
// and the tape library's leg-start cell must be the same cell.
int      Move_RootCell(const vec3_t origin);
// Grounded, on no graph cell and with no cell below within the step-off reach:
// no route can start and the step-off walk has nowhere to go (Bot2 StrandedKill).
qboolean Move_Stranded(int client);
// The decision tape's cursor and tallies, which belong to one released trip,
// and - when the tape library owns the tape - the selection for the leg that
// starts here. The caller supplies the root cell and the phase; the leg the
// key is formed from is the bot's own objective.
void     Move_DecisionTapeReset(int client,int cell,int phase);
// One line per entry at the end of a world, with its final status.
void     Move_DecisionTapeCensus(void);
// One parity event per leg carrying the comparison count the leg actually
// made. The periodic `horizon` event prints under a condition that a clean
// world can miss entirely, so it is not a count; this is.
void     Move_ParityTally(int client);
const movement_t *Move_State(int client);
// The landing of the native pad edge the path takes next (bot_moveNativeEdges),
// for the retained jumppad state to steer at; 0 when the next edge is no pad.
int      Move_PadLanding(int client, vec3_t landing);

// Parity harness and spike discovery at the actual Pmove boundary.
// bot_moveParity names the rollout horizon in command frames; zero leaves
// the harness inert. bot_moveSpike enables discovery on grounded frames.
void Bot2_MoveParityBeforePmove(gentity_t *ent, pmove_t *move);
void Bot2_MoveParityAfterPmove(gentity_t *ent, pmove_t *move);

#endif
