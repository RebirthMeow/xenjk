// Movement rewrite preparation (docs/movement/ARCHITECTURE.md, section 1).
// Phase 2. Node discovery: a layered flood fill over walkable floor from the
// BSP, seeded from spawn points and flag stands and admitted cell to cell by
// the walk's own ground sample. Edges: hop and strafe edges learned from
// committed plans; jump-pad, drop and basic-jump candidates found from the
// world itself (bot_moveNativeEdges, default 1) or, with it 0, seeded from the
// navmesh's off-mesh connections; wall climbs and lift rides (candidates,
// validated when executed). Searches: A* over cell links and edges by time,
// A* over cell and state, and route tables. Move_PrepareMap (bot2_move.h) builds
// or loads it at map load; move_graph rebuilds and reports from the console.
#ifndef BOT2_GRAPH_H
#define BOT2_GRAPH_H

#define GRAPH_GRID 32                   // base cell spacing in units
#define GRAPH_MAX_PATH 1024             // cells a search result may hold

// Hazards seen at a cell's refused neighbours; a node carries the union.
#define GRAPH_HAZARD_DROP    (1<<0)     // floor missing or more than a step below
#define GRAPH_HAZARD_LIQUID  (1<<1)     // lava or slime
#define GRAPH_HAZARD_HURT    (1<<2)     // trigger_hurt
#define GRAPH_HAZARD_ASCENT  (1<<3)     // floor more than a step above
#define GRAPH_HAZARD_BLOCKED (1<<4)     // sweep struck solid
#define GRAPH_HAZARD_STEEP   (1<<5)     // slope or non-world floor

typedef struct {
    short ix,iy;            // grid column; position is ix*GRAPH_GRID, iy*GRAPH_GRID
    float z;                // hull origin height, .125 above the floor
    int link[4];            // linked cell +x,+y,-x,-y or -1: the sample admitted it
    unsigned char refused;  // bit d: the sample toward neighbour d was refused
    unsigned char flags;    // GRAPH_HAZARD_* seen at refused neighbours
    unsigned char seed;     // placed by a seed rather than reached
    unsigned char moverState; // with mover: the MOVER_POS* it rested at when the floor was sampled
    short mover;            // floor is a resting mover's, by inline model number; 0 the world's
    int node[2];            // the x-run and y-run through the cell
    int firstEdge;          // first edge leaving the cell, or -1
} graphCell_t;

// A straight run of linked cells along one axis: a supported standing
// interval with two endpoints, floor height range, width margin and hazards.
typedef struct {
    int axis;               // 0 along x, 1 along y
    int first,count;        // first cell (lowest coordinate) and cells in the run
    float zMin,zMax;
    float width;            // narrowest perpendicular run through the node, in units
    unsigned char flags;
} graphNode_t;

typedef enum {
    GRAPH_EDGE_NONE,
    GRAPH_EDGE_HOP,         // a tap jump learned from a committed gap plan
    GRAPH_EDGE_PAD,         // a jump pad; the retained jumppad state executes it
    GRAPH_EDGE_DROP,        // a drop off a ledge (navmesh hint or native); executed as a hop candidate
    GRAPH_EDGE_JUMP,        // a basic jump over a gap (navmesh hint or native); executed as a hop candidate
    GRAPH_EDGE_STRAFE,      // a hop on continuous floor learned from a committed straight plan, with its state
    GRAPH_EDGE_CLIMB,       // a run up a wall to a floor above it, found from the walls at preparation
    GRAPH_EDGE_LIFT         // a ride: stand on a resting lift's boarding cell, ride it, step off at the far end
} graphEdgeType_t;
#define GRAPH_EDGE_TYPES 8

// A maneuver from one cell to another. takeoff and landing are hull origins:
// for a learned hop the measured launch and landing positions, for a hint
// the connection's start and end raised to hull height. A strafe edge also
// carries the state the rollout measured: the entry envelope (the launch
// speed and heading, and whether the tap was on the landing frame of the
// hop before, which pays no ground friction) and the exit state (the
// landing speed and heading), so the state search can chain it.
typedef struct {
    int type,from,to;
    vec3_t takeoff,landing;
    float dir[2];           // unit horizontal heading at the takeoff
    int costMs;             // time charged by the search
    int validated;          // a native rollout has executed it
    int traversals,failures;// this session
    int disabled;           // excluded from the search after repeated failures
    int offUntil;           // level time a timed off ends (Graph_EdgeFail); 0 none
    int nextFrom;           // next edge leaving the same cell, or -1
    int tap;                // strafe: tapped on the landing frame (entry is a landing state); lift: the lift's inline model
    float entrySpeed;       // strafe: horizontal speed at the launch
    float entryDir[2];      // strafe: unit horizontal velocity heading at the launch
    float exitSpeed;        // strafe: horizontal speed at the landing
    float exitDir[2];       // strafe: unit horizontal velocity heading at the landing
} graphEdge_t;

// The state the planner searches over besides the cell: horizontal speed
// and heading, whether the bot is on a landing frame (a tap there pays no
// friction), and the Speed window left. Force joins with held jumps.
typedef struct {
    float speed;
    float dir[2];
    int tap;
    int speedMs;
} graphState_t;

// The guided corridor (docs/movement, the expressiveness diagnostic). A
// coarse polyline taken from a human demonstration's geometry alone: no
// commands, timing or states. While one is loaded both searches may only
// leave the bot's own cell for cells within the radius of that polyline,
// so the corridor says roughly where to go and nothing about how. It is
// guidance for a diagnostic, never a runtime default: with none loaded
// every search is unguided and unchanged.
#define GRAPH_MAX_CORRIDOR 512
// Read bot2-corridor/<name>. Returns the points loaded, 0 when the file is
// missing, malformed, bound to another map or names no admitted cell.
int Graph_CorridorLoad(const char *name,float radius,char *why,int whySize);
void Graph_CorridorClear(void);
void Graph_Frame(void);                 // once a frame from G_RunFrame (the real frame time)
int Graph_CorridorPoints(void);         // 0 when no corridor is loaded
int Graph_CorridorCells(void);          // cells the loaded corridor admits
float Graph_CorridorRadius(void);
// Whether a cell is inside the loaded corridor; 1 when none is loaded.
int Graph_CorridorAdmits(int cell);
// Distance from a point to the loaded polyline, or -1 when none is loaded.
float Graph_CorridorDistance(const vec3_t point);

int Graph_CellCount(void);
// Whether pads, drops and jumps come from the world (bot_moveNativeEdges,
// default 1) rather than from a navmesh.
int Graph_NativeEdges(void);
int Graph_NodeCount(void);
int Graph_EdgeCount(void);
void Graph_PerfLinks(int *cells,int *opposite,int *any);   // perf counters: asymmetric links
int Graph_Reaches(int cell,int goal);   // 1 when a route leads from cell to goal (or either is unknown)
int Graph_CellClearance(int cell);
const graphCell_t *Graph_Cell(int index);
const graphNode_t *Graph_Node(int index);
const graphEdge_t *Graph_Edge(int index);
// The cell whose column contains the point and whose height is within the
// tolerance, or -1.
int Graph_CellAt(const vec3_t point,float tolerance);
// Whether a cell is floor now: always for the world's, and for a mover's only
// while that mover rests where it was sampled (bot2_mover.h).
int Graph_CellStands(int cell);
// The nearest cell within a horizontal radius and a height tolerance, or -1.
int Graph_Nearest(const vec3_t point,float radius,float tolerance);
// Whether two cells are linked by the walk (adjacent and admitted).
int Graph_Linked(int a,int b);
// Whether b is reachable from a over at most the given number of links.
int Graph_LinkedWithin(int a,int b,int steps);
// Add a learned edge and write the cache. Returns the edge index or -1.
int Graph_AddEdge(int type,int from,int to,const vec3_t takeoff,const vec3_t landing,const float *dir,int costMs,int validated);
// Add a strafe edge with its measured state, or fold it into an existing
// one from the same cells with the same tap and an entry speed within the
// state band (the lower cost is kept). Returns the edge index or -1.
int Graph_AddStrafeEdge(int from,int to,const vec3_t takeoff,const vec3_t landing,const float *dir,int costMs,int tap,
                        float entrySpeed,const float *entryDir,float exitSpeed,const float *exitDir,int *merged);
void Graph_EdgeResult(int edge,int success);
// A failure charged to what caused it. The graph is shared
// by every bot, so only what the edge itself is responsible for may take it
// out of every bot's search:
//   GRAPH_CAUSE_TRANSIENT  a player's body or a missile: not charged at all;
//                          the failing bot avoids the edge on its own (bot2_move.c).
//   GRAPH_CAUSE_MOVER      a lift, door or platform not at rest: the edge is
//                          off for GRAPH_MOVER_OFF_MS, then back.
//   GRAPH_CAUSE_WORLD      as always: GRAPH_EDGE_FAILURES of them disable an
//                          edge no bot has flown; a validated edge (flown, or
//                          proven in the prepared seed) is off for
//                          GRAPH_PROVEN_OFF_MS instead and then back, since a
//                          miss against a fixed wall is usually the approach.
// Graph_EdgeResult(edge,0) is GRAPH_CAUSE_WORLD.
#define GRAPH_CAUSE_WORLD 0
#define GRAPH_CAUSE_MOVER 1
#define GRAPH_CAUSE_TRANSIENT 2
void Graph_EdgeFail(int edge,int cause);
// Whether the search skips the edge now: disabled, or inside a timed off.
int Graph_EdgeOff(const graphEdge_t *e);
const char *Graph_CauseName(int cause);
// bot_moveFailCause: 1 (default) attributes failures as above; 0 charges every
// failure to the world and disables validated edges too, as before failures were attributed.
int Graph_FailCause(void);
// Live bodies for the next searches (Graph_Path, Graph_PlanState): a cell
// whose hull would overlap one is not admitted, the goal always is. The
// caller sets them around one replan and clears them (count 0) after; with
// none set the searches are unchanged. At most GRAPH_MAX_BODIES are kept.
#define GRAPH_MAX_BODIES 16
void Graph_BodiesSet(const vec3_t *at,int count);
// Whether the bodies set leave the searches no arc into goal from anywhere but
// body-blocked cells (and from is not one of them): a search with them must fail.
int Graph_BodiesEncloseGoal(int from,int goal);
// A forbidden volume for the next searches (records/2026-10-09-flag-avoid.md): an
// axis-aligned box of hull origins (a defender's enemy-flag pickup box plus its margin) that no walk step,
// diagonal or edge may enter. A step from a cell already inside may leave. An edge is tested from its source
// cell to its takeoff, and from its takeoff to its landing with the box reaching GRAPH_FORBID_ARC lower (the
// arc may rise that far above the chord). Set around one plan as the bodies are, cleared (NULL) after; a
// rejection is the plan's, never charged to the shared graph. Route tables are bypassed while one is set.
#define GRAPH_FORBID_ARC 96.0f
void Graph_ForbidSet(const float *mins,const float *maxs);
int Graph_ForbidActive(void);
// 1 when the step from cell `from` to cell `to` by `edge` (-1 a walk link or diagonal) enters the forbidden
// box set now; 0 with none set.
int Graph_ForbidBlocks(int from,int to,int edge);
// Graph_Path answered from a route table for the goal (bot_movePathTables, default 1): every cell's cheapest
// time to `to` under the cell search's costs, kept up to date as edges change. A route never dearer than the
// search's, sometimes cheaper (its heuristic is beaten by fast edges); under bodies or a corridor, the search.
int Graph_PathTable(int from,int to,int *cells,int *edges,int max,int *costMs);
// One frame's part of a route table being built (once a frame; Move_Command calls it).
void Graph_PathTableStep(void);
// A step's price in the reroute pass's walk model (2D length at 250 u/s, 250 ms when it is an edge).
int Graph_PriceMs(int from,int to,int edge);
// Joins for the next state searches (bot_moveRerouteJoin): cells of a route being left and the time it has left from
// each; a search ends on the best of them, joined through, when nothing it holds can beat it. Graph_PlanJoined: the
// cell the last search ended on, -1 none; with a slack it ends once nothing it holds is that many ms under the best.
// Graph_PlanStateTimes: each cell's time on the last state search's route.
void Graph_PlanJoinSet(const int *cells,const float *rest,int count,float slack);
void Graph_PlanJoinClear(void);
int Graph_PlanJoined(void);
int Graph_PlanStateTimes(float *times,int max);
// A state search split over frames (bot_movePlanSplit): the search
// Graph_PlanState would run (no required edge, no guidance), in a table of
// its own, under the bodies set now, `budget` expansions a call. Start
// returns 1 when it finished in its first part, 0 when it runs on (or did not
// start: *id 0, when no table is free); Resume continues it, 1 finished, 0
// not yet, -1 gone (cancelled, or the graph was rebuilt); Result reads a
// finished one and frees its table. An id names one search, never reused.
int Graph_PlanSplitStart(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int budget,int *id);
int Graph_PlanSplitResume(int id,int budget);
int Graph_PlanSplitResult(int id,int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow);
void Graph_PlanSplitCancel(int id);
// Whether the throw of this trigger_push from origin (where the bot entered
// it) comes down on the pad edge's landing when steered at it at wishspeed
// run, by the checks pad discovery keeps a landing by.
// pass: the entity the arc's traces ignore (the bot, whose own hull is at origin).
int Graph_PadReachable(const gentity_t *trigger,const vec3_t origin,int edge,float run,int pass);
// Force Speed's wishspeed multiplier, as the graph's searches assume it.
float Graph_SpeedFactor(void);
// Replace an edge's time with a measured one.
void Graph_EdgeCost(int edge,int costMs);
qboolean Graph_Save(void);
// The identity of the graph's own contents, frozen at map load: "g1-" and a
// 64-bit FNV-1a digest of the cell and edge records as the cache writes them,
// plus the version, grid and physics binding. The map checksum binds the map,
// not the graph: a seeded cache and an unseeded one on the same map share
// map and checksum yet hold different cells and edges, and a cell index or an
// edge index is what a decision tape names. Anything holding graph indices -
// a tape, a library index - is bound by this. Learned edges appended while a
// world runs do not change it: the identity is the graph a plan was authored
// against, which is the graph as loaded.
const char *Graph_Identity(void);
// A counter bumped by every change a planner can read - an edge added, merged,
// re-priced, validated, failed or disabled, the learned set reset, the graph
// rebuilt or loaded, the corridor loaded or cleared. Equal generations mean
// the planners would search the same graph.
unsigned int Graph_Generation(void);
// Put the learned set back to the graph as loaded: drop every edge appended
// while this world ran and restore the loaded edges a merge, a cost or a
// result mutated. The fixture's release calls this and nothing else does,
// because a fixture world is not a fresh world - the bot's own AI plays from
// its spawn until `bot_corner_start` arrives, and what it learns there changes
// the state planner's routes and so the leg's decisions. A real spawn has no
// release, so production is unchanged. The identity does not move: it is the
// graph as loaded, which is exactly what this restores.
void Graph_ResetLearned(void);
// Whether learned edges write the cache as they are added (the default);
// exploration turns it off and saves once at the end. Writes any deferred
// snapshot first, so the flag never strands one.
void Graph_Autosave(int on);
// The deferred cache write (`bot_moveGraphSaveDefer`, default on; 0 restores
// the immediate write). With it on, an autosave copies the edge records and
// the capped flag into a snapshot instead of rewriting the whole cache on the
// frame that learned; this writes the last snapshot, if one is pending. Called
// at G_ShutdownGame, from Graph_Autosave, at Move_PrepareMap and at each leg
// boundary (Move_Plan's objective change). Nothing in a running session reads the
// file, so the choice changes no decision, and the file written is the one the
// last autosave would have written. Returns qfalse only when a write failed.
qboolean Graph_SaveFlush(void);
// A* from one cell to another by time over links and enabled edges. Fills
// cells[] with the path and edges[i] with the edge used to reach cells[i]
// (-1 for a walk link); returns the count, 0 when no path.
int Graph_Path(int from,int to,int *cells,int *edges,int max,int *costMs);
// A* over cell and state: links are priced at the speed the state carries
// (friction and the run's acceleration applied over the link), a strafe
// edge is admitted only from a state inside its entry envelope and leaves
// its exit state, other edges leave the walk's state. Fills cells[] and
// edges[] as Graph_Path does, speeds[i] with the speed the plan expects at
// cells[i]; returns the count, 0 when no path or the state table overflowed
// (overflow is reported separately, so the caller can fall back).
// require, when not -1, is one search's directive: only a route that takes
// that edge reaches the goal (a bit in the state key, set on the edge and
// inherited); 0 when none does, and the caller runs the ordinary search.
// With require -1 the search is bit-identical to one without the argument.
int Graph_PlanState(int from,const graphState_t *start,int to,const int *avoid,int avoidCount,int require,
                    int *cells,int *edges,float *speeds,int max,int *costMs,int *expanded,int *overflow);
// The cell route the last Graph_PlanState kept when its search ran long (bot_movePlanBound, default off;
// records/2026-11-09-replan-budget.md): what Graph_Path(from,to) would return now, for the caller's fallback.
// 0 when there is none (no bound was taken, a state route was found, or any search, body or forbidden-volume
// change came since): the caller then calls Graph_Path as before. -1 when that search found no route, which is
// Graph_Path's answer too. Taken once.
int Graph_PlanBoundRoute(int from,int to,int *cells,int *edges,int max,int *costMs);
// The strafe edges leaving a cell or its neighbours that a state admits:
// the search's own admission test, for the executor to agree with it.
int Graph_StrafeAdmits(int edge,int cell,const graphState_t *state);
// The air steering onto a native pad edge's landing, shared by the edge's
// discovery and the jumppad state that flies it: 1 with the heading to
// accelerate along, 0 to coast (the velocity held already arrives).
int Graph_PadSteer(const vec3_t pos,const vec3_t vel,const vec3_t landing,float wish[2]);

#endif
