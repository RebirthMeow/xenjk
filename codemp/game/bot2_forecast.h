#ifndef BOT2_FORECAST_H
#define BOT2_FORECAST_H

// A rollout's caller-owned policy state. The layout is the one the archived
// maneuver probe defined (bot2_maneuver.h, since removed); rollouts and
// their parity records keep it unchanged.
typedef struct {
    float yaw;
    int lastTime, jumpTime, lastUpmove, phase;
} bot2_maneuverState_t;

// Diagnostic native Pmove rollout, NOT a whole-server simulator or live planner.
// Include g_local.h first. Call synchronously at an actual Pmove boundary. The
// entity's animation state (or explicit precedingAnimation) must describe the
// supplied root PS at that boundary.
// Caller owns root, policy and output storage; place the large outputs in BSS or
// heap, never on the native x86 game stack. Internal world scratch is BSS and
// single-use: recursive/concurrent calls are refused.
// The step capacity: Bot2's longest horizon (6400 ms, MOVE_PARITY_HORIZON_MS) at the 8 ms step of sv_fps 125.
// 256 before horizons became ms (records/2026-10-21-tickrate-horizons.md); a faster tickrate caps
// at this many steps.
#define BOT2_FORECAST_MAX_STEPS 800
#define BOT2_FORECAST_MAX_POLICY_EXTRA 128

// Keep the existing field prefix identical to the legacy movebench frame;
// only the private extension arrays are appended. output contains
// native contact results; its pointers are borrowed scratch, NOT an executable
// pmove or a serialized world. Read PS from beforePS/afterPS instead.
typedef struct {
    usercmd_t command;
    bot2_maneuverState_t beforePolicy, afterPolicy;
    playerState_t beforePS, afterPS;
    pmove_t output;
    int serverTime;
    int precedingAnimation[4];
    int settings[4];
    unsigned int contextHash;
    // Private extension follows the legacy fields; never added to its capture
    // ABI. Only request.policyExtraSize bytes are meaningful in each array.
    byte beforePolicyExtra[BOT2_FORECAST_MAX_POLICY_EXTRA];
    byte afterPolicyExtra[BOT2_FORECAST_MAX_POLICY_EXTRA];
} bot2_forecastFrame_t;

typedef struct {
    double totalUs, preGuardUs, worldCopyUs, contextHashUs;
    double policyUs, pmoveUs, restoreUs, postGuardUs;
} bot2_forecastTiming_t;

typedef enum {
    BOT2_FORECAST_COMPLETE,
    BOT2_FORECAST_BAD_ARGUMENT,
    BOT2_FORECAST_BUSY,
    BOT2_FORECAST_REFUSED
} bot2_forecastStatus_t;

enum {
    BOT2_FORECAST_SOLID = 1 << 0,
    BOT2_FORECAST_BODY = 1 << 1,
    BOT2_FORECAST_MATERIAL = 1 << 2,    // no longer set; the bit keeps its meaning in old logs
    BOT2_FORECAST_LIQUID = 1 << 3,      // slime or lava; water is not refused
    BOT2_FORECAST_QUERY_MASK = 1 << 4,
    BOT2_FORECAST_MOVEMENT_TYPE = 1 << 5,
    BOT2_FORECAST_ACTIVE_POWER = 1 << 6,
    BOT2_FORECAST_HAND_EXTEND = 1 << 7,
    BOT2_FORECAST_TRIGGER = 1 << 8,
    BOT2_FORECAST_POLICY_REFUSED = 1 << 9,
    BOT2_FORECAST_COMMAND_TIME = 1 << 10,
    BOT2_FORECAST_FALL_DAMAGE = 1 << 11,  // the landings' fall damage would kill (result.fallDamage)
    BOT2_FORECAST_FORBIDDEN = 1 << 12     // a step entered the client's forbidden box (Bot2_ForecastForbidSet)
};

// 1 when the segment a->b enters the box [mins,maxs]: it intersects the box and a is outside it (a segment
// that starts inside may leave). Slab test, a few multiplies an axis.
int Bot2_SegmentEntersBox(const float *a, const float *b, const float *mins, const float *maxs);
// A per-client forbidden volume of hull origins (a defender's enemy-flag keep-out):
// every rollout of that client refuses a step that enters it (BOT2_FORECAST_FORBIDDEN), replayed steps
// included. NULL clears. Get returns 1 and the box while one is set.
void Bot2_ForecastForbidSet(int client, const float *mins, const float *maxs);
int Bot2_ForecastForbidGet(int client, vec3_t mins, vec3_t maxs);

typedef enum {
    BOT2_FORECAST_COMMAND_REFUSED = 0,
    BOT2_FORECAST_COMMAND = 1,
    BOT2_FORECAST_POLICY_COMPLETE = 2
} bot2_forecastCommandStatus_t;

typedef enum {
    BOT2_FORECAST_COMPLETION_NONE,
    BOT2_FORECAST_COMPLETION_HORIZON,
    BOT2_FORECAST_COMPLETION_POLICY
} bot2_forecastCompletion_t;

// Callbacks may only write the supplied policy/command. Their context and the
// world are immutable; the helper cannot undo arbitrary callback side effects.
// POLICY_COMPLETE retains the current PS and updated policy, emits no command
// and does not execute a stopped Pmove tick. REFUSED retains the prior policy.
typedef bot2_forecastCommandStatus_t (*bot2_forecastCommand_t)(const playerState_t *ps,
    bot2_maneuverState_t *policy, void *policyExtra, int serverTime, const void *context,
    usercmd_t *command);
typedef unsigned int (*bot2_forecastContextHash_t)(const pmove_t *root,
    const void *context);

typedef struct {
    gentity_t *entity;
    const pmove_t *root;
    // Optional terminal animation from a preceding rollout, to seed a successor
    // without modifying the real entity. NULL copies the actual entity state.
    const int *precedingAnimation; // Four ints: legs/torso anim, legs/torso flip.
    bot2_maneuverState_t policy;
    // Optional trivially copied caller continuation state, not world pointers.
    // Callback receives NULL when size is zero; its context defines the type.
    const void *policyExtra;
    int policyExtraSize;        // 0..BOT2_FORECAST_MAX_POLICY_EXTRA.
    bot2_forecastCommand_t command;
    const void *context;
    // Optional already-submitted command: never regenerate/replace it. Its time
    // must already equal the native fixed-step ceiling (raw time when unfixed).
    // Its
    // post-command policy is mandatory and is copied without reinterpretation.
    const usercmd_t *firstCommand;
    const bot2_maneuverState_t *firstPolicyAfter;
    const void *firstPolicyExtraAfter; // Required with firstCommand when size>0.
    int firstTime;              // Unrounded server/world time of first boundary.
    int frameOffset;            // Nonnegative offset from current level.framenum.
    int stepMsec;               // Explicit native command cadence, 1..200 ms.
    int maxSteps;               // 1..BOT2_FORECAST_MAX_STEPS.
    qboolean stopAtTime;
    int stopTime;               // Stop AFTER first frame at/after this raw time.
    // Phase 1b (docs/movement): apply the game-frame Force update before every
    // step after the first, as WP_ForcePowersUpdate runs before each client's
    // Pmove: timed powers expire, levitation ends on the ground, and Force
    // regenerates only while no power is active. Force Speed and levitation
    // may then be active; every other active power still refuses. Off keeps
    // the legacy contract: any active power refuses and nothing regenerates.
    qboolean modelForce;
    bot2_forecastFrame_t *frames;
    int frameCapacity;
    // Optional diagnostic context, evaluated at each preceding frame. It does
    // not influence motion. Nonzero mask enables the old cache query guard.
    bot2_forecastContextHash_t contextHash;
    unsigned int allowedContentsMask;
    qboolean measureTime, profile;
} bot2_forecastRequest_t;

typedef struct {
    bot2_forecastStatus_t status;
    bot2_forecastCompletion_t completion;
    int count, traceCount;       // count includes only calls actually made to Pmove.
    unsigned int unsupported;
    int fallDamage;              // what ClientEvents would deal for the rollout's EV_FALL/EV_ROLL landings
    qboolean worldChanged, restored, animationChanged, animationRestored;
    playerState_t terminalPS;
    bot2_maneuverState_t terminalPolicy;
    byte terminalPolicyExtra[BOT2_FORECAST_MAX_POLICY_EXTRA];
    int terminalAnimation[4];  // legsAnim, torsoAnim, legsFlip, torsoFlip.
    bot2_forecastTiming_t timing;
} bot2_forecastResult_t;

// Success means the finite requested horizon completed inside the old static
// subset. Strict trigger/item-region, liquid, body, material and active-power
// refusals remain. No ClientThink events, item pickups or moving-world
// evolution are simulated; Force expiry and regeneration only under modelForce. Only named Pmove globals, level
// clocks and the temporary animation projection are restored. World hashing
// DETECTS other mutations; it is not rollback. Refused frames are retained.
// Policy completion requires at least one checked Pmove step in this rollout.
// Inputs and outputs must not overlap; on bad arguments only status is valid.
qboolean Bot2_ForecastRun(const bot2_forecastRequest_t *request,
    bot2_forecastResult_t *result);

// Opt-in scoped world guard. Outside a scope every rollout hashes the whole
// world before and after itself (the legacy path above, unchanged). Inside a
// scope one immutable baseline copy of the same data - every entity, every
// present client, the level struct - is taken once at Begin, and every rollout
// is compared byte-for-byte against it after it finishes. A difference refuses
// that rollout exactly as the legacy hash does and contaminates the scope: no
// later rollout in the scope executes, its callbacks are never called, and End
// reports the contamination. Detection is not rollback; nothing here restores
// a damaged live world.
//
// A scope is synchronous, single-root and non-interleaving: Begin refuses while
// a rollout is running or another scope is open, every rollout must present
// the scope's root, and End must be reached on every path out of the caller,
// early failures included. Baselines live in BSS, never on the stack; the
// sizes are reported so memory cost is a measured number.
typedef struct {
    double snapshotUs, compareUs;   // Begin copy; sum of per-rollout compares.
    int runs, refusedRuns;          // Rollouts compared; rollouts refused unexecuted.
    unsigned int bytes;             // Baseline bytes compared per rollout.
    qboolean contaminated;
} bot2_forecastGuard_t;

qboolean Bot2_ForecastGuardBegin(const pmove_t *root, bot2_forecastGuard_t *guard);
// True when the scope closed clean. False when it was contaminated, was not
// open for this guard, or a rollout is still running (the scope stays open).
qboolean Bot2_ForecastGuardEnd(bot2_forecastGuard_t *guard);
// The open scope's serial, 0 outside a scope (perf counters).
unsigned int Bot2_ForecastScopeSerial(void);

// Default-off trigger shadow diagnostic (bot_forecastTriggerShadow). It only
// observes: with it on, every refusal, candidate result and forecastTouched is
// identical to the same rollout with it off. Emit outside any search - printing
// inside one has changed outcomes before. `reset` clears the census and the
// bounded record store, keeping the enabled flag.
void Bot2_ForecastShadowReport(const char *path, qboolean reset);

// Names the candidate rollout about to be issued, so a shadow record can be
// joined to that candidate's [SPIKE] outcome. The next run consumes the tag;
// a rollout issued without one records plan and seq as -1.
void Bot2_ForecastShadowTag(int plan, int seq);

// Reports what the search did with the rollout it has just judged: nonzero
// when the search recovered the candidate itself. Called while that rollout is
// still the last one run, so no join is needed and nothing can be
// mis-attributed. A refusal the search already recovers is not a refusal a
// contact-test repair would hand back.
void Bot2_ForecastShadowRescue(int rescued);

// The per-scope collision-trace cache (row 5).
// Between Begin and End the world a rollout traces against does not change -
// one bot's Pmove boundary, whose rollouts all exclude that bot's own body and
// restore the world after each - so a trace is a pure function of its inputs,
// and a repeated one (a shared command prefix, a reroute pass re-flying a
// candidate, a duplicate trace inside one Pmove step) is answered from the
// first result, byte for byte. Begin empties the cache; outside a scope every
// trace goes to the engine exactly as before.
// True inside a rollout the forecast is profiling (a cost capture is active).
qboolean Bot2_ForecastProfiling(void);
// While on, a team flag's pickup box does not refuse a rollout: the caller is
// checking what follows an arrival at the objective (bot2_move.c,
// StraightArrives). Off again before any other rollout is issued.
void Bot2_ForecastFlagPass(int on);
// True while a rollout runs: G_TempEntity spawns nothing into the live world.
qboolean Bot2_ForecastSuppressTempEntity(void);
void Bot2_ForecastTraceCacheBegin(void);
void Bot2_ForecastTraceCacheEnd(void);
// Drops every cached trace and keeps the scope open: the world a later trace
// sees has changed (bodies unlinked for a counterfactual search, or relinked).
void Bot2_ForecastTraceCacheFlush(void);
// What the rollouts' refused contacts (BOT2_FORECAST_BODY) struck since the
// last reset: players, NPCs and missiles (transient), movers not at rest, anything
// else that is not the world.
typedef struct { int transient, mover, other; } bot2_forecastContacts_t;
void Bot2_ForecastContactsReset(void);
bot2_forecastContacts_t Bot2_ForecastContacts(void);

#endif
