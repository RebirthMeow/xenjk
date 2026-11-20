#ifndef BOT2_MOVECOST_H
#define BOT2_MOVECOST_H
qboolean Bot2_MoveCostStart(void);
// bot_movecost_start: starts the capture bot_moveCost declares (it was reached
// through bot_flagrun until that bench was removed).
void Svcmd_MoveCostStart_f(void);
void Bot2_MoveCostShutdown(void);   // G_ShutdownGame: ends a capture a map change cut
qboolean Bot2_MoveCostClient(gentity_t *ent);
// Opt-in section attribution; no clock query or file I/O outside this fixture.
double Bot2_MoveCostClock(void);
double Bot2_ClockUs(void);              // the clock, always read (diagnostics outside a cost capture)
void Bot2_MoveCostSection(int section,double began);

// Complete movement accounting. Command dispatch is timed by the fixture
// itself; everything below is how work that runs outside that timer - the
// rewrite's searches at the Pmove boundary inside ClientThink_real - is charged
// to the same game frame and the same client. Every entry point is inert unless
// a capture is active, so production dispatch pays nothing.
// Sections 0-3 belonged to the pre-rewrite stack (since removed): no
// writer is left; their CSV columns stay, always 0, for positional readers.
#define BOT2_MOVECOST_SECTION_PLAN 0
#define BOT2_MOVECOST_SECTION_ROUTE 1
#define BOT2_MOVECOST_SECTION_LOCALNAV 2
#define BOT2_MOVECOST_SECTION_LOAD 3
#define BOT2_MOVECOST_SECTION_PREPARE 4
// Forecast rollout time already measured by the oracle; a diagnostic subpart
// of PREPARE, never added to the frame total a second time.
#define BOT2_MOVECOST_SECTION_FORECAST 5
// Row 5 attribution of the rewrite's landing search (accounting v3), each a
// diagnostic subpart of PREPARE or dispatch, never added to the total: the
// state planner, the cell planner, the forecast rollouts the search issues
// (their own measured time, no extra clock) and the reroute pass's
// RerouteFrom calls whole (their planner time is inside it as well).
#define BOT2_MOVECOST_SECTION_PLANSTATE 6
#define BOT2_MOVECOST_SECTION_GRAPHPATH 7
#define BOT2_MOVECOST_SECTION_ROLLOUT 8
#define BOT2_MOVECOST_SECTION_REROUTE 9
// Inside every forecast rollout while a capture is active (the forecast then
// profiles itself): the world copy, the Pmove steps, the guard's baseline
// compare, the command policy and the per-step trigger test. Subparts, never
// added to the total; the profiling's own clock reads are inside them.
#define BOT2_MOVECOST_SECTION_FC_COPY 10
#define BOT2_MOVECOST_SECTION_FC_PMOVE 11
#define BOT2_MOVECOST_SECTION_FC_GUARD 12
#define BOT2_MOVECOST_SECTION_FC_POLICY 13
#define BOT2_MOVECOST_SECTION_FC_TRIGGER 14
#define BOT2_MOVECOST_SECTION_FC_TOTAL 15
// Inside Pmove during a profiled rollout (bg_pmove.c marks): setup, first
// ground trace, pre-move, the move, animation, second ground trace and water,
// pre-weapon, weapon, and the rest; then all of Pmove's own traces. And the
// walk's corridor test (ai_bot2_localnav.c) inside rollouts: whole, its traces,
// its hurt-volume area queries.
#define BOT2_MOVECOST_SECTION_PM_SETUP 16
#define BOT2_MOVECOST_SECTION_PM_TRACE 25
#define BOT2_MOVECOST_SECTION_CORRIDOR 26
#define BOT2_MOVECOST_SECTION_CORRIDOR_TRACE 27
#define BOT2_MOVECOST_SECTION_CORRIDOR_BOX 28
// Inside the policy of a profiled rollout (bot2_move.c): the whole executor
// step (MoveStep), its command search (Bot2_MoveControl), its path progress,
// and its navmesh waypoint query.
#define BOT2_MOVECOST_SECTION_POL_MOVESTEP 29
#define BOT2_MOVECOST_SECTION_POL_CONTROL 30
#define BOT2_MOVECOST_SECTION_POL_PROGRESS 31
#define BOT2_MOVECOST_SECTION_POL_NAVMESH 32
#define BOT2_MOVECOST_SECTIONS 33

#define BOT2_MOVECOST_COUNT_PREPARE_CALLS 0
#define BOT2_MOVECOST_COUNT_PREPARE_ADMITTED 1
#define BOT2_MOVECOST_COUNT_PREPARE_DEFERRED 2
#define BOT2_MOVECOST_COUNT_LIBRARY_LOADS 3
#define BOT2_MOVECOST_COUNT_PREPARE_REFUSED_FREE 4
#define BOT2_MOVECOST_COUNT_ROLLOUTS 5
#define BOT2_MOVECOST_COUNT_ROLLOUT_STEPS 6
#define BOT2_MOVECOST_COUNT_PLANSTATE_CALLS 7
#define BOT2_MOVECOST_COUNT_PLANSTATE_EXPANDED 8
#define BOT2_MOVECOST_COUNT_PLANSTATE_OVERFLOW 9
#define BOT2_MOVECOST_COUNT_GRAPHPATH_CALLS 10
#define BOT2_MOVECOST_COUNT_REROUTE_CALLS 11
// The search budget's outcome at each landing search (accounting v3): every
// search it was asked about, those served from an in-flight pre-search, those
// with none ready, those refused at the state compare, and every other refusal.
#define BOT2_MOVECOST_COUNT_BUDGET_SEARCHES 12
#define BOT2_MOVECOST_COUNT_BUDGET_HITS 13
#define BOT2_MOVECOST_COUNT_BUDGET_NOT_READY 14
#define BOT2_MOVECOST_COUNT_BUDGET_REFUSED_STATE 15
#define BOT2_MOVECOST_COUNT_BUDGET_REFUSED_OTHER 16
// The forecast's trace cache: traces it was asked
// while a scope was open, and those it answered from the cache.
#define BOT2_MOVECOST_COUNT_TRACE_CALLS 17
#define BOT2_MOVECOST_COUNT_TRACE_HITS 18
#define BOT2_MOVECOST_COUNT_PM_TRACES 19
#define BOT2_MOVECOST_COUNT_CORRIDOR_CALLS 20
#define BOT2_MOVECOST_COUNT_CORRIDOR_TRACES 21
#define BOT2_MOVECOST_COUNT_CORRIDOR_BOXES 22
// The corridor memo's hits.
#define BOT2_MOVECOST_COUNT_CORRIDOR_MEMO_HITS 23
// Rollout reuse (bot_forecastReplay): steps whose Pmove the buffer's earlier
// rollout supplied, steps the verify mode re-simulated beside the replay, and
// the verified steps that disagreed.
#define BOT2_MOVECOST_COUNT_REPLAY_STEPS 24
#define BOT2_MOVECOST_COUNT_REPLAY_VERIFIED 25
#define BOT2_MOVECOST_COUNT_REPLAY_MISMATCHES 26
#define BOT2_MOVECOST_COUNTS 27

// Charge one complete preparation boundary call - early refusal, first-use
// library load, binding, window build, prefixes, candidate selection and
// publication alike - to this frame and this client.
void Bot2_MoveCostPreparation(int client,double began);
// Add an already-measured duration to a diagnostic section.
void Bot2_MoveCostAdd(int section,double us);
void Bot2_MoveCostCount(int counter);
void Bot2_MoveCostCountAdd(int counter,int n);
// Diagnostic output that would otherwise land inside the recorded interval.
// Buffered until recording ends while a capture is active; plain trap->Print
// otherwise, so ordinary runs see the same lines in the same order.
void Bot2_MoveCostPrint(const char *format,...);

// The frame meter (bot_moveFrameMeter): per-bot movement microseconds per game
// frame in ordinary worlds. Clock returns 0 when off, and Add ignores a 0.
double Bot2_FrameMeterClock(void);
void Bot2_FrameMeterAdd(int client,double began);
void Bot2_FrameMeterReset(void);
// Work counters the frame meter attributes to the bot being timed: forecast
// Pmove steps and forecast traces (bot2_forecast.c increments them).
// And planner expansions (the state and cell searches, bot2_graph.c), for the work meter.
// Unsigned: they run the module's lifetime and wrap; read them only as differences.
extern unsigned int bot2_meterSteps,bot2_meterTraces,bot2_meterExpanded;
// Work the counters above do not see (records/2026-11-09-replan-budget.md): cells settled by guidance
// table builds and folds (bot2_graph.c), forecast traces the trace cache did not answer (bot2_forecast.c) and
// corridor evaluations the corridor memo did not answer (ai_bot2_localnav.c). Totals on [FRAMEWORK].
extern unsigned int bot2_meterGuidePops,bot2_meterEngineTraces,bot2_meterCorridors,bot2_meterGuideFoldPops,bot2_meterGuideFoldDeferred;
// Their split by caller for [FRAMEEXP]: cell-search expansions, state-search expansions inside a replan.
// bot2_meterInReplan: 1 in a replan or split part, 2 in a reroute search; cell expansions outside both (defender
// route queries, the dropped-flag check) are bot2_meterExpandedCellOther.
extern unsigned int bot2_meterExpandedCell,bot2_meterExpandedReplan,bot2_meterExpandedCellOther;
extern int bot2_meterInReplan;
// Where a slow frame's steps went (diagnostic): the movement boundary adds each search section's
// forecast steps (Bot2_FrameMeterSource) and the planner its microseconds during the timed interval; Add folds
// them into the bot's frame, and a [FRAMESLOW] frame prints them as one [FRAMESRC] line. Counts only.
enum {BOT2_SRC_KNOCK,BOT2_SRC_SPIKE,BOT2_SRC_PLANNED,BOT2_SRC_GAP,BOT2_SRC_WALKDEFER,BOT2_SRC_STRAIGHT,BOT2_SRC_BUDGET,BOT2_SRC_COUNT};
void Bot2_FrameMeterSource(int source,int steps);
void Bot2_FrameMeterPlanUs(double us);

// Perf counters (records/2026-10-04-perf-counters.md): where candidate lossless speed-ups would save
// time, measured instead of estimated. Live only while the frame meter is on (bot2_perfOn); cumulative since the
// meter's reset, printed as one [PERFCTR] line beside [FRAMECOST]. Inert to decisions: counts and clock reads only.
enum {
    BOT2_PERF_SCOPE,            // guard scopes opened; us = the Begin snapshot copy
    BOT2_PERF_SCOPE_END,        // us = the End whole-world compare
    BOT2_PERF_SCOPE_FIRSTCOPY,  // a scope's first rollout: the full forecast world copy
    BOT2_PERF_SKIP_ELIGIBLE,    // Begin in the same trace-cache stamp after a clean End (bot_forecastScopeSkip 2)
    BOT2_PERF_SKIP_SAME,        // ... and the world equal to the last baseline: the snapshot skip would be exact
    BOT2_PERF_SKIP_DIFF,        // ... and the world differs: the skip would be wrong
    BOT2_PERF_SKIP_COPYSAME,    // ... and the forecast copy also still equal (bar its last slot): first copy skippable
    BOT2_PERF_NEAR_BUILD,       // strafe near-lists built; us = their build
    BOT2_PERF_NEAR_REBUILD,     // ... of cells that had a list before the last drop
    BOT2_PERF_NEAR_DROP,        // whole-arena drops
    BOT2_PERF_MOVER,            // mover rest queries
    BOT2_PERF_MOVER_MISS,       // ... recomputed; us = the recompute
    BOT2_PERF_MOVER_SCOPEHIT,   // ... misses a scope-serial key would have answered
    BOT2_PERF_MOVER_SCOPEDIFF,  // ... of which the stored answer differed (must be 0 for that key to be exact)
    BOT2_PERF_ROLLOUT,          // forecast rollouts
    BOT2_PERF_STEP,             // forecast steps simulated (Pmove called)
    BOT2_PERF_REPLAYED,         // forecast steps replayed
    BOT2_PERF_XSCOPE,           // rollouts with a buffer that matches but for its scope; n2 = its steps (upper bound)
    BOT2_PERF_RING_WRAP,        // candidate ring slots reused while they held a rollout of the same scope
    BOT2_PERF_SWEEP,            // defender throw-path hurt sweeps; us = their time
    BOT2_PERF_CVAR,             // cvar reads by name; us = their time
    BOT2_PERF_CVAR_ROLLOUT,     // ... inside a forecast rollout
    BOT2_PERF_GFIND,            // G_Find calls; us = their time
    BOT2_PERF_REACH,            // Graph_Reaches floods; us = their time (index rebuild included)
    BOT2_PERF_COUNT
};
typedef struct {long long n[BOT2_PERF_COUNT],n2[BOT2_PERF_COUNT];double us[BOT2_PERF_COUNT];} bot2_perf_t;
extern bot2_perf_t bot2_perf;
extern int bot2_perfOn,bot2_perfInRollout;
double Bot2_PerfClock(void);    // microseconds; call only while bot2_perfOn
// Spawns and frees of game entities (G_InitGentity, G_FreeEntity), for lookups that cache entity lists within a
// frame and must see an entity spawned or freed earlier in it (Bot2_NextFlag).
extern unsigned int bot2_entitySerial;
#define BOT2_PERF_BEGIN(t) double t=bot2_perfOn?Bot2_PerfClock():0
#define BOT2_PERF_END(k,t) do{if(bot2_perfOn && (t)>0){bot2_perf.n[k]++;bot2_perf.us[k]+=Bot2_PerfClock()-(t);}}while(0)
#define BOT2_PERF_N(k) do{if(bot2_perfOn)bot2_perf.n[k]++;}while(0)
#endif
