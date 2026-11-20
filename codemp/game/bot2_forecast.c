// Extracted static-world native rollout. See the header's restoration limits.
#include "g_local.h"
#include "bg_local.h"
#include "bot2_forecast.h"
#include "bot2_movecost.h"
#include "bot2_finecmd.h"
#include "bot2_mover.h"
#include <stdint.h>
#include <limits.h>
#ifdef _WIN32
#undef INFINITE
#include <windows.h>
#endif

// Review batch-2 fixes: on by default; a cvar set to 0
// turns its fix off (records/2026-11-10-review-batch2.md).
static int FixOn(const char *name) {char v[16];trap->Cvar_VariableStringBuffer(name,v,sizeof(v));return !v[0] || atoi(v)!=0;}

extern bgEntity_t *pm_entSelf, *pm_entVeh;
extern qboolean gPMDoSlowFall;

// Never put an entity array, client or frame sequence on the game DLL stack.
static gentity_t forecastEntities[MAX_GENTITIES];
static gclient_t forecastClient;
// The state a bot's think reads when it issues the frame's command: the
// previous Pmove's result, before the frame's Force update.
static playerState_t forecastCommandPS;
// Double alignment allows ordinary scalar continuation structs. Callers should
// memcpy if their type requires stronger alignment; bytes contain no ownership.
static union {
    double alignment;
    byte bytes[BOT2_FORECAST_MAX_POLICY_EXTRA];
} forecastPolicyExtra;
static qboolean running;
static const bot2_forecastRequest_t *activeRequest;
static bot2_forecastResult_t *activeResult;
static unsigned int badWorld;
static int traceCount;

// Scoped guard baseline: the same bytes the legacy hash covers, held once per
// scope and compared exactly after every rollout. BSS, never the stack.
static gentity_t baselineEntities[MAX_GENTITIES];
// the scope copies and compares only the slots below level.num_entities. A slot at or above it
// has never been handed out this level (G_Spawn raises num_entities to use one; G_InitGame zeroes them all), and the
// level struct, num_entities with it, is compared too, so a rollout that spawned is still caught.
static int baselineCount;
// The rollout copy's slots that may differ from g_entities: [0, forecastHigh). Above it both are zero, so a copy of
// max(num_entities, forecastHigh) slots is the whole-array copy (forecastHigh exceeds num_entities after a restart).
static int forecastHigh;
static int LiveEntities(void) {
    int n = level.num_entities;
    return n < 0 ? 0 : n > MAX_GENTITIES ? MAX_GENTITIES : n;
}
static gclient_t baselineClients[MAX_CLIENTS];
static level_locals_t baselineLevel;
static struct {
    qboolean active, contaminated;
    const pmove_t *root;
    bot2_forecastGuard_t *guard;
    qboolean present[MAX_CLIENTS];
    // Scope reuse (row 5): inside a guard scope the world is
    // asserted equal to the baseline after every rollout, so the rollout
    // copy's source never changes. copyValid says forecastEntities holds that
    // copy except for the slot the last rollout ran in (copyClient). Promoted
    // after a verify mode ran the old full copy and guard beside it and found
    // no disagreement (records/2026-09-22-search-speed.md).
    int copyValid, copyClient;
    unsigned int serial;        // bumped by every Begin; names the scope a rollout ran in
} scope;
// What the last End left, for the snapshot-skip verify mode (bot_forecastScopeSkip 2, perf counters only).
static struct { int valid, time, copyValid, copyClient; unsigned int stamp; } lastEnd;

// Rollout replay (row 5, rollout reuse; `bot_forecastReplay`, 1 replays, 2
// verifies, 0 off; default 1). A search's rollouts from one
// root share long prefixes: candidates with the same first hop issue the
// same commands until it lands, and the reroute pass re-flies a flight it has
// already flown (records/2026-09-22-search-speed.md, 29 and 56 percent of
// steps). Every
// rollout writes its frames into a caller's buffer, and the forecast keeps a
// record of what each buffer last held; the candidate loop gives each
// candidate a buffer of its own from a ring, so the siblings a candidate
// shares its first hop with are still there when it runs. A rollout starts
// with every other buffer that holds an earlier rollout of the same scope,
// root, clock and settings. Each step whose Pmove inputs are those one of
// them recorded - the state Pmove reads, the command, the animation carried
// in - takes that buffer's Pmove result instead of calling Pmove; buffers
// drop out at the first step they disagree on, and once none is left every
// step is simulated as before. The command callback still runs at
// every step with the rollout's own policy and context, and writes its own
// policy into the frame: only its command can say the prefix holds. The
// policies themselves may differ, since Pmove never reads them.
//
// Why a matching step is Pmove's own answer: inside a scope the rollout's own
// slot (its entity and client) and the level struct are asserted unchanged
// after every rollout and the whole world once at the scope's end - Pmove
// writes no other entity - the rollout's copy of the slot is rebuilt from it
// at every start, and Pmove is deterministic in the state, the command, the
// pmove settings (the root, compared by value) and that world, apart from
// its draws on the global random stream: a step that can reach one (a
// spinning saber animation) is never replayed. Only the last frame of the recorded rollout is never replayed: a
// rollout stops on the step that set an unsupported flag, so every earlier
// step left the flags, the world and the animation guard clean. Verify mode
// calls Pmove on every replayable step as well and counts any step whose
// state, output or slot disagrees with the buffer.
#define REPLAY_BUFFERS 24
typedef struct {
    const bot2_forecastFrame_t *frames;
    int count;                  // frames the last rollout into this buffer wrote
    unsigned int used;          // when it was last written, for replacement
    unsigned int scope;         // the scope serial it ran in
    pmove_t root;               // the root it presented, by value
    int client, firstTime, stepMsec, frameOffset, framenum, policyExtraSize;
    qboolean modelForce, itemsPass, ownFlagPass, flagPass, clean;   // flagPass: Bot2_ForecastFlagPass
    unsigned int allowedContentsMask;
} replayRecord_t;
static replayRecord_t replayRecords[REPLAY_BUFFERS];
static unsigned int replayClock;
static gentity_t replaySlot;    // verify mode: the slot before Pmove

static double Microseconds(void) {
#ifdef _WIN32
    // The counter frequency is fixed at boot: read once (records/2026-10-03-perf-lossless.md).
    static LARGE_INTEGER frequency;
    LARGE_INTEGER count;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000000.0 / (double)frequency.QuadPart;
#else
    return (double)trap->Milliseconds() * 1000.0;
#endif
}

static unsigned int HashBytes(const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned int h = 2166136261u;
    while (size--) h = (h ^ *p++) * 16777619u;
    return h;
}

// Exact comparison against the scope baseline. Covers exactly what WorldHash
// covers: every entity byte, every client that was present at Begin, and the
// level struct after its clocks have been restored.
static qboolean BaselineIntact(void) {
    int i;
    if (memcmp(&level, &baselineLevel, sizeof(level))) return qfalse;   // num_entities first: it bounds the compare
    if (memcmp(g_entities, baselineEntities, sizeof(gentity_t) * baselineCount)) return qfalse;
    for (i = 0; i < baselineLevel.maxclients && i < MAX_CLIENTS; i++) {
        if (scope.present[i] != (g_entities[i].client != NULL)) return qfalse;
        if (scope.present[i] && memcmp(g_entities[i].client, &baselineClients[i], sizeof(gclient_t))) return qfalse;
    }
    return qtrue;
}

// The per-rollout part of the guard under scope reuse: the rollout's own
// entity and client and the level struct - everything a rollout is known to
// write and restore (its animation fields, the level clocks). The whole-world
// compare runs once, at the end of the scope (Bot2_ForecastGuardEnd).
static qboolean SlotIntact(int client) {
    if (memcmp(&g_entities[client], &baselineEntities[client], sizeof(gentity_t))) return qfalse;
    if (client < MAX_CLIENTS && scope.present[client] &&
        memcmp(g_entities[client].client, &baselineClients[client], sizeof(gclient_t))) return qfalse;
    return !memcmp(&level, &baselineLevel, sizeof(level));
}

// bot_forecastContaminationLog: when a rollout leaves its own slot or the
// level different from the scope baseline, one [FORECAST] contamination line
// per differing structure names the byte ranges (up to eight) and the offsets
// a reader needs to name the field. A diagnostic; it changes nothing.
static void ContaminationRanges(const char *what, int client, const void *now, const void *base, size_t size) {
    const unsigned char *a = (const unsigned char *)now, *b = (const unsigned char *)base;
    char text[512]; size_t i = 0; int ranges = 0, used = 0;
    text[0] = 0;
    while (i < size && ranges < 8) {
        size_t start;
        if (a[i] == b[i]) { i++; continue; }
        start = i;
        while (i < size && a[i] != b[i]) i++;
        used += Com_sprintf(text + used, sizeof(text) - used, " %u-%u", (unsigned)start, (unsigned)(i - 1));
        ranges++;
    }
    if (ranges) trap->Print("[FORECAST] contamination level=%d client=%d struct=%s size=%u ps_size=%u client_pers=%u entity_r=%u ranges=%s\n",
        level.time, client, what, (unsigned)size, (unsigned)sizeof(playerState_t), (unsigned)offsetof(gclient_t, pers),
        (unsigned)offsetof(gentity_t, r), text);
}
static void ContaminationLog(int client) {
    if (!trap->Cvar_VariableIntegerValue("bot_forecastContaminationLog")) return;
    ContaminationRanges("entity", client, &g_entities[client], &baselineEntities[client], sizeof(gentity_t));
    if (client < MAX_CLIENTS && scope.present[client])
        ContaminationRanges("client", client, g_entities[client].client, &baselineClients[client], sizeof(gclient_t));
    ContaminationRanges("level", client, &level, &baselineLevel, sizeof(level));
}

static unsigned int WorldHash(void) {
    unsigned int h = HashBytes(g_entities, sizeof(gentity_t) * MAX_GENTITIES);
    int i;
    for (i = 0; i < level.maxclients; i++) {
        if (g_entities[i].client)
            h = (h * 16777619u) ^ HashBytes(g_entities[i].client, sizeof(gclient_t));
    }
    return h;
}

// The trace cache. Open addressing over a power-of-two table; an entry is live
// only when its stamp is the scope's, so Begin clears it by one increment. The
// key is the trace's inputs bit for bit, so -0 and 0 are distinct keys: a miss,
// never a wrong answer. A full table stops storing and every further trace goes
// to the engine.
#define TRACE_CACHE_SIZE 65536
#define TRACE_CACHE_LOAD (TRACE_CACHE_SIZE*3/4)
typedef struct {
    float start[3], mins[3], maxs[3], end[3];
    int pass, mask, nulls;
} traceKey_t;
static struct {
    int active, count;
    unsigned int stamp;
    unsigned int entryStamp[TRACE_CACHE_SIZE];
    traceKey_t key[TRACE_CACHE_SIZE];
    trace_t value[TRACE_CACHE_SIZE];
} traceCache;

// Point contents in the trace cache's scope: PM_SetWaterLevel asks before and after every Pmove
// substep, and the end of one substep and the start of the next ask at the same origin. The world is static inside
// the scope on the same premise as the trace cache (a transient hide flushes both), and the stored value is the
// engine's own answer, so a hit returns what the call would.
#define CONTENTS_CACHE_SIZE 4096
static struct {
    unsigned int stamp[CONTENTS_CACHE_SIZE];
    float pos[CONTENTS_CACHE_SIZE][3];
    int pass[CONTENTS_CACHE_SIZE], value[CONTENTS_CACHE_SIZE];
} contentsCache;

void Bot2_ForecastTraceCacheBegin(void) {
    traceCache.active = 1;
    traceCache.count = 0;
    if (++traceCache.stamp == 0) {
        memset(traceCache.entryStamp, 0, sizeof(traceCache.entryStamp));
        memset(contentsCache.stamp, 0, sizeof(contentsCache.stamp));
        traceCache.stamp = 1;
    }
}

void Bot2_ForecastTraceCacheEnd(void) { traceCache.active = 0; }

void Bot2_ForecastTraceCacheFlush(void) {
    if (traceCache.active) Bot2_ForecastTraceCacheBegin();
}

// The hash only chooses where the probe starts; every hit is a full key compare and what is stored is decided
// by miss order and the load count, so a different hash returns the same traces. Word at a time:
// the byte loop was 60 dependent multiplies on every one of about 11 traces a step. The words are copied out
// reading the float fields through an unsigned int pointer breaks strict aliasing on GCC/Clang.
static unsigned int TraceKeyHash(const traceKey_t *k) {
    unsigned int w[sizeof(traceKey_t) / 4], h = 2166136261u; size_t i;
    memcpy(w, k, sizeof(w));
    for (i = 0; i < sizeof(w) / 4; i++) { h ^= w[i]; h *= 2654435761u; h ^= h >> 15; }
    return h;
}



static void CachedTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end, int pass, int mask) {
    traceKey_t k; unsigned int h;
    if (!traceCache.active) {
        activeRequest->root->trace(tr, start, mins, maxs, end, pass, mask);
        bot2_meterEngineTraces++;
        return;
    }
    memset(&k, 0, sizeof(k));
    VectorCopy(start, k.start); VectorCopy(end, k.end);
    if (mins) VectorCopy(mins, k.mins); else k.nulls |= 1;
    if (maxs) VectorCopy(maxs, k.maxs); else k.nulls |= 2;
    k.pass = pass; k.mask = mask;
    h = TraceKeyHash(&k) & (TRACE_CACHE_SIZE - 1);
    Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_TRACE_CALLS);
    for (;;) {
        if (traceCache.entryStamp[h] != traceCache.stamp) {
            activeRequest->root->trace(tr, start, mins, maxs, end, pass, mask);
            bot2_meterEngineTraces++;
            if (traceCache.count < TRACE_CACHE_LOAD) {
                traceCache.entryStamp[h] = traceCache.stamp; traceCache.key[h] = k; traceCache.value[h] = *tr;
                traceCache.count++;
            }
            return;
        }
        if (!memcmp(&traceCache.key[h], &k, sizeof(k))) {
            *tr = traceCache.value[h];
            Bot2_MoveCostCount(BOT2_MOVECOST_COUNT_TRACE_HITS);
            return;
        }
        h = (h + 1) & (TRACE_CACHE_SIZE - 1);
    }
}

// Row 5 diagnostic profiler state, live only inside a profiled rollout.
static struct { int on; double last; double phase[10]; double traceUs; int traces; } pmProfile;

void Bot2_ForecastPmoveMark(int phase) {
    double now;
    if (!pmProfile.on) return;
    now = Microseconds();
    if (phase > 0 && phase < 10) pmProfile.phase[phase] += now - pmProfile.last;
    pmProfile.last = now;
}

qboolean Bot2_ForecastProfiling(void) { return pmProfile.on; }

// GAME Pmove spawns temp entities of its own: PM_CrashLandEffect's landing
// dust on mud, sand, dirt, snow and gravel, and the water, slime and lava
// splashes. A rollout runs the same code, so each such landing put a real
// effect entity into the live world - raising level.num_entities, which the
// guard rightly calls contamination, refusing every later candidate of the
// search (Dantooine's dirt: 11 of 36 knock searches). While a
// rollout runs, G_TempEntity asks here and hands back a scratch entity
// instead. The material and liquid refusals are unchanged: a rollout that
// lands on them is still unsupported, it just no longer leaks.
qboolean Bot2_ForecastSuppressTempEntity(void) { return running; }

// Resting movers are floor (bot2_mover.h) until the time they can next move.
// Every mover is frozen in the forecast, which is exact for one at rest, so a
// rollout may stand on, land on or brush a lift only while level.time - the
// simulated frame's - is before that time. A lift call the rollout itself
// touches brings the time forward to the touch plus the call's delays; the
// calls are kept per rollout and registered on every step, replayed or not.
// More calls than the list holds make every mover unusable for the rest of
// the rollout.
#define FORECAST_MOVER_CALLS 16
static struct { int entity, until; } forecastCalled[FORECAST_MOVER_CALLS];
static int forecastCalledCount, forecastCalledOverflow;

// bot_forecastBodyLog (diagnostic): every BODY verdict prints the entity behind it.
static int forecastBodyLog;
static void BodyLog(const char *where, int entityNum) {
    const gentity_t *e = entityNum >= 0 && entityNum < ENTITYNUM_MAX_NORMAL ? &g_entities[entityNum] : NULL;
    const playerState_t *ps = e && e->client ? &e->client->ps : NULL;
    trap->Print("[FORECAST] body level=%d client=%d where=%s ent=%d class=%s type=%d inuse=%d rest_until=%d origin=%.1f,%.1f,%.1f"
        " ps=%.1f,%.1f,%.1f link=%.1f,%.1f,%.1f..%.1f,%.1f,%.1f\n",
        level.time, activeRequest && activeRequest->entity ? activeRequest->entity->s.number : -1, where, entityNum,
        e && e->classname ? e->classname : "-", e ? e->s.eType : -1, e ? e->inuse : 0,
        Bot2_MoverRestUntil(entityNum), e ? e->r.currentOrigin[0] : 0, e ? e->r.currentOrigin[1] : 0, e ? e->r.currentOrigin[2] : 0,
        ps ? ps->origin[0] : 0, ps ? ps->origin[1] : 0, ps ? ps->origin[2] : 0,
        e ? e->r.absmin[0] : 0, e ? e->r.absmin[1] : 0, e ? e->r.absmin[2] : 0, e ? e->r.absmax[0] : 0, e ? e->r.absmax[1] : 0, e ? e->r.absmax[2] : 0);
}

static qboolean ForecastStandable(int entityNum) {
    int until, k, master;
    if (entityNum == ENTITYNUM_WORLD) return qtrue;
    if (forecastCalledOverflow) return qfalse;
    until = Bot2_MoverRestUntil(entityNum);
    // Calls are recorded against the team master, and a call starts the whole
    // team: a multipart lift's slave stops being floor with its master.
    master = Bot2_MoverMaster(entityNum);
    for (k = 0; k < forecastCalledCount; k++)
        if (forecastCalled[k].entity == master && forecastCalled[k].until < until) until = forecastCalled[k].until;
    return until > level.time;
}

// The fall damage ClientEvents deals for the landing events one step raised
// (g_active.c, EV_FALL and EV_ROLL): Pmove's delta, already reduced for a
// Force jump and a roll, times 0.16 above 44, or times 1 above 14 in a
// knockdown and for Siege falls over 60. G_Damage applies it unarmored.
static int FallDamage(const playerState_t *before, const playerState_t *after) {
    int s, damage = 0;
    if ((dmflags.integer & DF_NO_FALLING) || after->fallingToDeath) return 0;
    // Only the last MAX_PS_EVENTS survive in the ring, as ClientEvents reads them.
    s = before->eventSequence;
    if (after->eventSequence - s > MAX_PS_EVENTS) s = after->eventSequence - MAX_PS_EVENTS;
    for (; s < after->eventSequence; s++) {
        int event = after->events[s & (MAX_PS_EVENTS - 1)] & ~EV_EVENT_BITS;
        int delta = after->eventParms[s & (MAX_PS_EVENTS - 1)];
        if (event != EV_FALL && event != EV_ROLL) continue;
        if (BG_InKnockDownOnly(after->legsAnim)) { if (delta > 14) damage += delta; }
        else if (delta > 44) damage += level.gametype == GT_SIEGE && delta > 60 ? delta : (int)(delta * 0.16f);
    }
    return damage;
}

// The movers a step's swept box starts, by the same box overlap the trigger
// test uses for everything else.
// The in-use trigger entities, in index order, once per guard scope (records/2026-10-03-perf-lossless.md):
// TouchesTrigger and ForecastMoverCalls scanned every non-client entity on every simulated step for them. Inside a
// scope the world is frozen (the guard verifies it), so the list is exactly the entities the scan would pass on to
// its other tests, in the same order; those tests still read the entities live. Outside a scope it is built each call.
static struct { unsigned int serial; int count; int index[MAX_GENTITIES]; } triggerList;
static const int *TriggerList(int *count);

static void ForecastMoverCalls(const vec3_t a, const vec3_t b) {
    int i, j, k, n, movers[BOT2_MOVER_CALL_TARGETS], delays[BOT2_MOVER_CALL_TARGETS], t, listed;
    const int *list;
    if (!Bot2_MoverCalls()) return;
    list = TriggerList(&listed);
    for (t = 0; t < listed; t++) {
        gentity_t *other = &g_entities[i = list[t]];
        if (!other->inuse || !(other->r.contents & CONTENTS_TRIGGER)) continue;
        if (!(n = Bot2_MoverCallTargets(i, movers, delays, BOT2_MOVER_CALL_TARGETS))) continue;
        // A trigger waiting out its re-arm, or already counting down to fire,
        // ignores a touch (multi_trigger); the pending fire is in the rest time.
        if (other->nextthink > level.time && !(other->spawnflags & 2048)) continue;
        for (j = 0; j < 3; j++) {
            float low = min(a[j], b[j]) - (j == 2 ? 24.0f : 15.0f);
            float high = max(a[j], b[j]) + (j == 2 ? 40.0f : 15.0f);
            if (high < other->r.absmin[j] || low > other->r.absmax[j]) break;
        }
        if (j < 3) continue;
        for (k = 0; k < n; k++) {
            int c, until = level.time + delays[k];
            for (c = 0; c < forecastCalledCount; c++) if (forecastCalled[c].entity == movers[k]) break;
            if (c == forecastCalledCount) {
                if (c == FORECAST_MOVER_CALLS) { forecastCalledOverflow = 1; continue; }
                forecastCalledCount++;
                forecastCalled[c].entity = movers[k]; forecastCalled[c].until = until;
            } else if (until < forecastCalled[c].until) forecastCalled[c].until = until;
        }
    }
}

static bot2_forecastContacts_t forecastContacts;
void Bot2_ForecastContactsReset(void) { memset(&forecastContacts, 0, sizeof(forecastContacts)); }
bot2_forecastContacts_t Bot2_ForecastContacts(void) { return forecastContacts; }
static void CountContact(int entityNum) {
    const gentity_t *other = entityNum >= 0 && entityNum < ENTITYNUM_WORLD ? &g_entities[entityNum] : NULL;
    if (!other) forecastContacts.other++;
    else if (entityNum < MAX_CLIENTS || other->s.eType == ET_MISSILE || other->s.eType == ET_NPC) forecastContacts.transient++;
    else if (other->s.eType == ET_MOVER) forecastContacts.mover++;
    else forecastContacts.other++;
}

static void ForecastTrace(trace_t *tr, const vec3_t start, const vec3_t mins,
    const vec3_t maxs, const vec3_t end, int pass, int mask) {
    double traceStarted = pmProfile.on ? Microseconds() : 0;
    CachedTrace(tr, start, mins, maxs, end, pass, mask);
    if (pmProfile.on) { pmProfile.traceUs += Microseconds() - traceStarted; pmProfile.traces++; }
    traceCount++; bot2_meterTraces++;
    if (activeRequest->allowedContentsMask && (mask & ~activeRequest->allowedContentsMask))
        badWorld |= BOT2_FORECAST_QUERY_MASK;
    if (tr->startsolid || tr->allsolid) badWorld |= BOT2_FORECAST_SOLID;
    if (tr->fraction < 1 && !ForecastStandable(tr->entityNum)) {
        badWorld |= BOT2_FORECAST_BODY;
        CountContact(tr->entityNum);
        if (forecastBodyLog) BodyLog("trace", tr->entityNum);
    }
    // Surface materials are not refused: GAME Pmove plays a
    // landing effect on mud, sand, dirt, snow and gravel, which is visual and
    // spawns nothing in a rollout (G_TempEntity, Bot2_ForecastSuppressTempEntity).
    // Refusing every contact left Talravin's dirt floor unplannable
    // (docs/movement/records/2026-09-27-eight-maps.md).
}

static int ForecastContents(const vec3_t pos, int pass) {
    int contents;
    if (traceCache.active) {
        unsigned int w[4], h;
        memcpy(w, pos, 12); w[3] = (unsigned int)pass;
        h = ((w[0] * 2654435761u) ^ (w[1] * 2246822519u) ^ (w[2] * 3266489917u) ^ (w[3] * 668265263u)) >> 20;
        if (contentsCache.stamp[h] == traceCache.stamp && contentsCache.pass[h] == pass &&
            !memcmp(contentsCache.pos[h], pos, 12)) {
            contents = contentsCache.value[h];
        } else {
            contents = activeRequest->root->pointcontents(pos, pass);
            contentsCache.stamp[h] = traceCache.stamp; contentsCache.pass[h] = pass;
            memcpy(contentsCache.pos[h], pos, 12); contentsCache.value[h] = contents;
        }
    } else contents = activeRequest->root->pointcontents(pos, pass);
    // Slime and lava hurt. Water does not: its splash is visual like the
    // landing effects above, and a pit under water is refused by its
    // trigger_hurt. Air expiry and drowning damage are
    // not modelled; deferred (docs/movement/PLAN.md, known gap: drowning).
    if (contents & (CONTENTS_SLIME | CONTENTS_LAVA)) badWorld |= BOT2_FORECAST_LIQUID;
    return contents;
}

// Preserve the legacy conservative trigger AND native item pickup exclusion.
// bot_forecastItems (default on; 0 turns it off): a weapon, ammo, armor or
// health pickup is not a refusal: touching one changes no field Pmove reads, and
// Kejim's lanes carry enough of them that a rollout's onward walk after a
// landing seldom stayed clear. On the new maps item refusals blocked whole drop
// searches (Carratos's stand drop: one weapon_demp2); on, 279 pinned pairs ran
// -136 s with zero parity mismatches (records/2026-09-25-drop-aim.md). Flags,
// powerups and holdables still refuse; so do hurt volumes and pads.
static int forecastItemsPass, forecastTriggerLog;
// bot_forecastOwnFlag (default on; 0 turns it off): the root's own team flag, standing at
// its base, does not refuse while the root carries no enemy flag. That touch
// does nothing natively (Team_TouchOurFlag returns without effect), and a
// stand start is inside its own flag's pickup box: every candidate from the
// release frame was refused, the first strafe edge from the stand was
// deferred, and at Force Speed the bot was past its takeoff before judging
// resumed (Kejim level 2). A dropped flag still refuses.
static int forecastOwnFlagPass;
static qboolean OwnFlagAtBase(const gentity_t *other) {
    const playerState_t *ps;
    int team, own;
    if (!forecastOwnFlagPass || !other->item || other->item->giType != IT_TEAM || (other->flags & FL_DROPPED_ITEM)) return qfalse;
    ps = activeRequest->root->ps;
    team = ps->persistant[PERS_TEAM];
    own = team == TEAM_RED ? PW_REDFLAG : team == TEAM_BLUE ? PW_BLUEFLAG : -1;
    if (own < 0 || other->item->giTag != own) return qfalse;
    return !ps->powerups[PW_REDFLAG] && !ps->powerups[PW_BLUEFLAG] && !ps->powerups[PW_NEUTRALFLAG];
}
static gentity_t *forecastTouched;
// Bot2_ForecastFlagPass: an arrival's continuation is rolled past the flag.
static int forecastFlagPass;
void Bot2_ForecastFlagPass(int on) { forecastFlagPass = on; }

// The forbidden volumes (records/2026-10-09-flag-avoid.md): a defender's enemy-flag
// keep-out as a constraint on every rollout it searches, instead of fake bodies in its plans. The box is in
// hull-origin space, as the native pickup test is (BG_PlayerTouchesItem compares the origin with the item's),
// so the hull's sweep is the origin's segment. Not a world property: no trigger, no graph state, no replay key
// (Pmove never reads it); each step is tested, a replayed one too.
static struct { int on; vec3_t mins, maxs; } forecastForbid[MAX_CLIENTS];
int Bot2_SegmentEntersBox(const float *a, const float *b, const float *mins, const float *maxs) {
    float t0 = 0, t1 = 1;
    int i;
    if (a[0] >= mins[0] && a[0] <= maxs[0] && a[1] >= mins[1] && a[1] <= maxs[1] && a[2] >= mins[2] && a[2] <= maxs[2])
        return 0;   // inside already: leaving is allowed
    for (i = 0; i < 3; i++) {
        float d = b[i] - a[i];
        if (d > -1e-6f && d < 1e-6f) {
            if (a[i] < mins[i] || a[i] > maxs[i]) return 0;
        } else {
            float u = (mins[i] - a[i]) / d, v = (maxs[i] - a[i]) / d;
            if (u > v) { float w = u; u = v; v = w; }
            if (u > t0) t0 = u;
            if (v < t1) t1 = v;
            if (t0 > t1) return 0;
        }
    }
    return 1;
}
void Bot2_ForecastForbidSet(int client, const float *mins, const float *maxs) {
    if (client < 0 || client >= MAX_CLIENTS) return;
    forecastForbid[client].on = mins && maxs;
    if (mins && maxs) { VectorCopy(mins, forecastForbid[client].mins); VectorCopy(maxs, forecastForbid[client].maxs); }
}
int Bot2_ForecastForbidGet(int client, vec3_t mins, vec3_t maxs) {
    if (client < 0 || client >= MAX_CLIENTS || !forecastForbid[client].on) return 0;
    if (mins) VectorCopy(forecastForbid[client].mins, mins);
    if (maxs) VectorCopy(forecastForbid[client].maxs, maxs);
    return 1;
}

// ---------------------------------------------------------------------------
// Trigger shadow diagnostic. Off unless bot_forecastTriggerShadow is nonzero.
//
// It records what the native contact rules would have decided at the same
// simulated frame and changes nothing the search acts on: the returned
// refusal, forecastTouched and every candidate result are bit-identical with
// the diagnostic on or off. No touch callback runs, no damage or pickup is
// applied, no live state is written - BG_PlayerTouchesItem and
// trap->EntityContact are both read-only queries.
//
// Four native boundaries are kept apart, because a disagreement at one of them
// is not a disagreement at another and only the last is a native effect:
//
//   old       the shipped test: the segment's axis-aligned box grown by fixed
//             standing extents, overlapped against the trigger's r.absmin and
//             r.absmax (which carry SV_LinkEntity's one-unit pad).
//   aabb      the same overlap, but grown by the hull native would actually
//             use - ClientThink_real copies pmove.mins/maxs into r.mins/maxs
//             after Pmove and G_TouchTriggers adds those to ps.origin, so a
//             crouched player's box is 24 units shorter than the old constant.
//   swept     the same swept box, resolved against the trigger's real brushes
//             through trap->EntityContact instead of its bounding box.
//   endpoint  G_TouchTriggers' own question: brush contact at the frame's
//             after origin only. Native tests once per ClientThink_real after
//             the whole Pmove, never along the path, so this is the boundary
//             the game actually evaluates - and the one a fast mover can
//             tunnel through. A swept refusal the endpoint misses is the old
//             test being safer than the game, not a false positive.
//   processed endpoint contact on an entity native would dispatch at all:
//             G_TouchTriggers skips a trigger whose touch function is NULL
//             when the toucher has none either.
//
// ET_ITEM never reaches EntityContact natively - G_TouchTriggers chooses
// BG_PlayerTouchesItem *instead*, on an if/else. The shipped test applies the
// pickup region and then falls through to the generic bounding-box overlap, so
// an item that misses its own pickup bounds can still refuse on a box native
// never consults. That divergence is counted separately as itemGenericOnly.
// ---------------------------------------------------------------------------

#define BOT2_SHADOW_OLD         1
#define BOT2_SHADOW_AABB        2
#define BOT2_SHADOW_SWEPT       4
#define BOT2_SHADOW_ENDPOINT    8
#define BOT2_SHADOW_PROCESSED   16
#define BOT2_SHADOW_STATES      32
#define BOT2_SHADOW_MAX_RECORDS 2048

typedef struct {
    int frame, boundary;            // rollout frame index; 0 step, 1 policy-complete
    // The candidate rollout this examination belongs to. `seq` is the value of
    // the caller's never-reset rollout counter, which the [SPIKE] line prints
    // too, so a disagreement can be joined to its candidate's outcome. Both
    // are -1 for a rollout that is not a candidate's - a walk lookahead, a
    // permit or a parity rollout - which is a fact worth keeping, not a gap.
    int plan, seq;
    int entity, eType, giType, flags;
    int hasEntityTouch, hasSelfTouch, ducked, dead;
    int levelTime, commandTime, pmoveFixed, pmoveMsec;
    vec3_t before, after, hullMins, hullMaxs;
    char classname[32];
} bot2_shadowRecord_t;

static struct {
    int on;
    // Complete census: every examined entity lands in exactly one cell, so the
    // totals are independent of the bounded record store and cannot overflow.
    int census[BOT2_SHADOW_STATES];
    // The same census split by trigger entity. Indexed directly, so reporting
    // disagreement by trigger needs no table search and can never overflow or
    // drop a row. Classnames are resolved from the live entity at report time.
    int perEntity[MAX_GENTITIES][BOT2_SHADOW_STATES];
    int calls[2], examined, itemsExamined, itemGenericOnly, noTouchAnywhere;
    int rollouts, records, dropped;
    // Pooled counts above are per examination: one rollout that grazes the same
    // trigger for thirty frames contributes thirty. These count the rollouts
    // themselves, so a disagreement's reach is not read off the pooled number.
    int rolloutsWithDisagreement, rolloutFlagged;
    // Refusals split by what native would have done in the SAME call. A refusal
    // is only recoverable when no examined entity reached the native rung at
    // all: one trigger disagreeing while another would genuinely dispatch
    // leaves the refusal exactly where it was. Recoverable is not competitive,
    // not executable and not faster - it is an upper bound on what a repair to
    // the contact test could return to the search, and nothing more.
    int refusals, refusalsNativeProcessed, refusalsNativeEndpoint, refusalsRecoverable;
    int refusedRolloutFlagged, rolloutsRefused, rolloutsRefusedRecoverable;
    int callProcessed, callEndpoint;
    // Set by the caller before a candidate rollout and consumed by the next
    // run, so a tag can never leak onto a rollout it does not describe.
    int tagPlan, tagSeq, runPlan, runSeq;
    int taggedRollouts, untaggedRollouts;
    // Whether the rollout in progress was stopped by a refusal no native rung
    // reached. The caller pairs its own verdict with this, so a refusal the
    // search already recovers is never counted as something a contact-test
    // repair would hand back.
    int rolloutRecoverable;
    int judged, judgedRecoverable, judgedRecoverableRescued, judgedRecoverableStood;
    // The same split by the ladder cell of the trigger that caused the refusal
    // - the first old-contact in the deciding call, which is the entity the
    // shipped test returns. Without it the rescue split is one number and
    // cannot say which mechanism the search already recovers.
    int judgedRescuedCell[BOT2_SHADOW_STATES], judgedStoodCell[BOT2_SHADOW_STATES];
    int callDecidingFlags, callDecidingSet, rolloutDecidingFlags;
    // Records are capped per disagreement kind, not first-come, so a long run
    // cannot fill the store with copies of whichever kind happened first.
    int perCellRecords[BOT2_SHADOW_STATES];
    // Context for the call in progress, set by the two call sites.
    vec3_t hullMins, hullMaxs;
    int boundary, frame, ducked, pmoveFixed, pmoveMsec, commandTime;
    bot2_shadowRecord_t record[BOT2_SHADOW_MAX_RECORDS];
} shadow;

// BG_PlayerTouchesItem reads only ps->origin, so a zeroed state with the origin
// written is exactly the native query without copying a playerState_t per
// entity per frame. It is never handed to anything that could mutate it.
static playerState_t shadowItemPS;

static void ShadowHullBox(const vec3_t at, vec3_t mins, vec3_t maxs) {
    VectorAdd(at, shadow.hullMins, mins);
    VectorAdd(at, shadow.hullMaxs, maxs);
}

static qboolean ShadowTouchesItem(gentity_t *other, const vec3_t at) {
    VectorCopy(at, shadowItemPS.origin);
    return BG_PlayerTouchesItem(&shadowItemPS, &other->s, level.time);
}

static qboolean ShadowBrushContact(gentity_t *other, const vec3_t mins, const vec3_t maxs) {
    return trap->EntityContact(mins, maxs, (sharedEntity_t *)other, qfalse);
}

// One entity, one call of the shipped test. `oldItem` and `oldGeneric` are the
// two halves of the shipped decision already computed by the caller, so this
// never recomputes - and never influences - what the search sees.
static void ShadowExamine(gentity_t *other, const vec3_t a, const vec3_t b,
                          qboolean oldItem, qboolean oldGeneric) {
    vec3_t amins, amaxs, bmins, bmaxs, smins, smaxs;
    int j, flags = 0;
    qboolean item = (qboolean)(other->s.eType == ET_ITEM);
    qboolean aabb, swept, endpoint, hasTouch;
    gentity_t *self = activeRequest->entity;

    shadow.examined++;
    if (item) shadow.itemsExamined++;
    if (oldItem || oldGeneric) flags |= BOT2_SHADOW_OLD;
    if (item && oldGeneric && !oldItem) shadow.itemGenericOnly++;

    ShadowHullBox(a, amins, amaxs);
    ShadowHullBox(b, bmins, bmaxs);
    for (j = 0; j < 3; j++) {
        smins[j] = min(amins[j], bmins[j]);
        smaxs[j] = max(amaxs[j], bmaxs[j]);
    }

    if (item) {
        // Native never applies a bounding box or a brush query to an item, so
        // both middle rungs are the pickup region itself: the segment's box
        // against it for the swept question, the after origin for the endpoint.
        aabb = swept = (qboolean)(ShadowTouchesItem(other, a) || ShadowTouchesItem(other, b) ||
                                  oldItem);
        endpoint = ShadowTouchesItem(other, b);
    } else {
        for (j = 0; j < 3; j++)
            if (smaxs[j] < other->r.absmin[j] || smins[j] > other->r.absmax[j]) break;
        aabb = (qboolean)(j == 3);
        swept = ShadowBrushContact(other, smins, smaxs);
        endpoint = ShadowBrushContact(other, bmins, bmaxs);
    }

    hasTouch = (qboolean)(other->touch != NULL || (self && self->touch != NULL));
    if (!hasTouch) shadow.noTouchAnywhere++;
    if (aabb) flags |= BOT2_SHADOW_AABB;
    if (swept) flags |= BOT2_SHADOW_SWEPT;
    if (endpoint) flags |= BOT2_SHADOW_ENDPOINT;
    if (endpoint && hasTouch) flags |= BOT2_SHADOW_PROCESSED;
    if (endpoint) shadow.callEndpoint = 1;
    if (endpoint && hasTouch) shadow.callProcessed = 1;
    shadow.census[flags]++;
    // The first old-contact of this call is the entity the shipped test
    // returns, so its rungs are what the refusal is actually made of.
    if ((oldItem || oldGeneric) && !shadow.callDecidingSet) {
        shadow.callDecidingFlags = flags; shadow.callDecidingSet = 1;
    }
    if (other->s.number >= 0 && other->s.number < MAX_GENTITIES)
        shadow.perEntity[other->s.number][flags]++;

    // Records are a bounded sample of the rungs that disagree. The census above
    // stays complete whatever happens here; dropped records are reported.
    if (!(flags & BOT2_SHADOW_OLD) == !(flags & BOT2_SHADOW_PROCESSED) &&
        !(flags & BOT2_SHADOW_OLD) == !(flags & BOT2_SHADOW_ENDPOINT) &&
        !(flags & BOT2_SHADOW_OLD) == !(flags & BOT2_SHADOW_SWEPT) &&
        !(flags & BOT2_SHADOW_OLD) == !(flags & BOT2_SHADOW_AABB)) return;
    if (!shadow.rolloutFlagged) { shadow.rolloutFlagged = 1; shadow.rolloutsWithDisagreement++; }
    if (shadow.records >= BOT2_SHADOW_MAX_RECORDS ||
        shadow.perCellRecords[flags] >= BOT2_SHADOW_MAX_RECORDS / BOT2_SHADOW_STATES) {
        shadow.dropped++; return;
    }
    shadow.perCellRecords[flags]++;
    {
        bot2_shadowRecord_t *rec = &shadow.record[shadow.records++];
        memset(rec, 0, sizeof(*rec));
        rec->frame = shadow.frame; rec->boundary = shadow.boundary;
        rec->plan = shadow.runPlan; rec->seq = shadow.runSeq;
        rec->entity = other->s.number; rec->eType = other->s.eType;
        rec->giType = (item && other->item) ? other->item->giType : -1;
        rec->flags = flags;
        rec->hasEntityTouch = other->touch != NULL;
        rec->hasSelfTouch = (self && self->touch != NULL);
        rec->ducked = shadow.ducked;
        rec->dead = (self && self->client && self->client->ps.stats[STAT_HEALTH] <= 0);
        rec->levelTime = level.time; rec->commandTime = shadow.commandTime;
        rec->pmoveFixed = shadow.pmoveFixed; rec->pmoveMsec = shadow.pmoveMsec;
        VectorCopy(a, rec->before); VectorCopy(b, rec->after);
        VectorCopy(shadow.hullMins, rec->hullMins);
        VectorCopy(shadow.hullMaxs, rec->hullMaxs);
        Q_strncpyz(rec->classname, other->classname ? other->classname : "?",
                   sizeof(rec->classname));
    }
}

// A lift call (Bot2_MoverCallTargets: a trigger_multiple whose every target is
// a binary mover, with no other output) starts a mover and does nothing to the
// toucher, so it does not refuse a rollout (ctf4's fourteen lift calls sit at
// every drop and shaft between its levels). The rollout records the call
// instead (ForecastMoverCalls), which ends the called mover's time as floor.
// A trigger with any other output - target2 on clearing, the siege
// target3/target4, a use script - is not exempt, since those effects are not
// validated.
static const int *TriggerList(int *count) {
    int i;
    if (!scope.active || triggerList.serial != scope.serial) {
        triggerList.count = 0;
        for (i = level.maxclients; i < level.num_entities; i++)
            if (g_entities[i].inuse && (g_entities[i].r.contents & CONTENTS_TRIGGER)) triggerList.index[triggerList.count++] = i;
        triggerList.serial = scope.active ? scope.serial : 0;
    }
    *count = triggerList.count;
    return triggerList.index;
}

static qboolean TouchesTrigger(const vec3_t a, const vec3_t b) {
    int i, j;
    gentity_t *firstTouched = NULL;
    qboolean result = qfalse;
    forecastTouched = NULL;
    if (shadow.on) {
        shadow.calls[shadow.boundary ? 1 : 0]++;
        shadow.callProcessed = shadow.callEndpoint = 0;
        shadow.callDecidingFlags = shadow.callDecidingSet = 0;
    }
    const int *list;int t, listed;
    list = TriggerList(&listed);
    for (t = 0; t < listed; t++) {
        gentity_t *other = &g_entities[i = list[t]];
        qboolean oldItem = qfalse, oldGeneric = qfalse;
        if (!other->inuse || !(other->r.contents & CONTENTS_TRIGGER)) continue;
        if (Bot2_MoverCallTargets(i, NULL, NULL, BOT2_MOVER_CALL_TARGETS)) continue;
        if (other->s.eType == ET_ITEM) {
            vec3_t itemOrigin;
            if (OwnFlagAtBase(other)) continue;
            if (forecastFlagPass && other->item && other->item->giType == IT_TEAM) continue;
            if (forecastItemsPass && other->item &&
                (other->item->giType == IT_WEAPON || other->item->giType == IT_AMMO ||
                 other->item->giType == IT_ARMOR || other->item->giType == IT_HEALTH)) continue;
            BG_EvaluateTrajectory(&other->s.pos, level.time, itemOrigin);
            for (j = 0; j < 3; j++) {
                float low = itemOrigin[j] - (j == 0 ? 50.0f : 36.0f);
                float high = itemOrigin[j] + (j == 0 ? 44.0f : 36.0f);
                if (max(a[j], b[j]) < low || min(a[j], b[j]) > high) break;
            }
            if (j == 3) oldItem = qtrue;
        }
        // The shipped test returns on the item hit, so the generic overlap is
        // only ever reached when the pickup region missed. Preserved exactly.
        if (!oldItem) {
            for (j = 0; j < 3; j++) {
                float low = min(a[j], b[j]) - (j == 2 ? 24.0f : 15.0f);
                float high = max(a[j], b[j]) + (j == 2 ? 40.0f : 15.0f);
                if (high < other->r.absmin[j] || low > other->r.absmax[j]) break;
            }
            if (j == 3) oldGeneric = qtrue;
        }
        if (oldItem || oldGeneric) {
            // The first contact is the shipped answer; with the diagnostic off
            // this is the original early return, unchanged.
            if (!result) { result = qtrue; firstTouched = other; }
            if (!shadow.on) break;
        }
        if (shadow.on) ShadowExamine(other, a, b, oldItem, oldGeneric);
    }
    forecastTouched = firstTouched;
    if (shadow.on && result) {
        shadow.refusals++;
        if (shadow.callProcessed) shadow.refusalsNativeProcessed++;
        else shadow.refusalsRecoverable++;
        if (shadow.callEndpoint) shadow.refusalsNativeEndpoint++;
        if (!shadow.refusedRolloutFlagged) {
            shadow.refusedRolloutFlagged = 1; shadow.rolloutsRefused++;
            // The refusal that stops a rollout is its first one; only that call
            // decides whether the whole rollout could have been returned.
            if (!shadow.callProcessed) {
                shadow.rolloutsRefusedRecoverable++; shadow.rolloutRecoverable = 1;
                shadow.rolloutDecidingFlags = shadow.callDecidingFlags;
            }
        }
    }
    return result;
}

static const char *ShadowStateName(int flags, char *buffer, int size) {
    Com_sprintf(buffer, size, "%s%s%s%s%s",
        (flags & BOT2_SHADOW_OLD) ? "old," : "",
        (flags & BOT2_SHADOW_AABB) ? "aabb," : "",
        (flags & BOT2_SHADOW_SWEPT) ? "swept," : "",
        (flags & BOT2_SHADOW_ENDPOINT) ? "endpoint," : "",
        (flags & BOT2_SHADOW_PROCESSED) ? "processed," : "");
    if (!buffer[0]) Q_strncpyz(buffer, "none", size);
    return buffer;
}

// Emitted outside the search. Printing inside a rollout has measurably changed
// outcomes before, so nothing here is ever called from the loop above.
// What the search did with the rollout it has just judged: nonzero when it
// recovered the candidate itself, as StraightArrives does by turning a trigger
// refusal at the objective into an arrival. Must be called while that rollout
// is still the last one run. Rollouts the diagnostic did not stop on a
// recoverable refusal are counted and otherwise ignored.
void Bot2_ForecastShadowRescue(int rescued) {
    if (!shadow.on) return;
    shadow.judged++;
    if (!shadow.rolloutRecoverable) return;
    shadow.judgedRecoverable++;
    if (rescued) {
        shadow.judgedRecoverableRescued++;
        shadow.judgedRescuedCell[shadow.rolloutDecidingFlags & (BOT2_SHADOW_STATES - 1)]++;
    } else {
        shadow.judgedRecoverableStood++;
        shadow.judgedStoodCell[shadow.rolloutDecidingFlags & (BOT2_SHADOW_STATES - 1)]++;
    }
}

// Names the candidate rollout about to be issued. Costs two stores when the
// diagnostic is off; the next Bot2_ForecastRun consumes it.
void Bot2_ForecastShadowTag(int plan, int seq) {
    shadow.tagPlan = plan; shadow.tagSeq = seq;
}

// Report lines go to a file when one is open: an rcon reply is redirected away
// from the server log and truncated, so a census of any size needs its own.
static fileHandle_t shadowFile;
static void ShadowWrite(const char *fmt, ...) {
    va_list argptr; char line[1024]; int length;
    va_start(argptr, fmt);
    length = Q_vsnprintf(line, sizeof(line), fmt, argptr);
    va_end(argptr);
    if (length <= 0) return;
    if (length >= (int)sizeof(line)) length = (int)sizeof(line) - 1;
    if (shadowFile) trap->FS_Write(line, length, shadowFile);
    else trap->Print("%s", line);
}

void Bot2_ForecastShadowReport(const char *path, qboolean reset) {
    char buffer[64];
    int i, totals[5], n = 0;
    if (path && *path) {
        trap->FS_Open(path, &shadowFile, FS_WRITE);
        if (!shadowFile) trap->Print("[SHADOW] could not open %s; reporting to the console\n", path);
    }
    memset(totals, 0, sizeof(totals));
    for (i = 0; i < BOT2_SHADOW_STATES; i++) {
        n += shadow.census[i];
        if (i & BOT2_SHADOW_OLD) totals[0] += shadow.census[i];
        if (i & BOT2_SHADOW_AABB) totals[1] += shadow.census[i];
        if (i & BOT2_SHADOW_SWEPT) totals[2] += shadow.census[i];
        if (i & BOT2_SHADOW_ENDPOINT) totals[3] += shadow.census[i];
        if (i & BOT2_SHADOW_PROCESSED) totals[4] += shadow.census[i];
    }
    ShadowWrite("[SHADOW] enabled=%d rollouts=%d rollouts_disagreeing=%d calls_step=%d calls_complete=%d examined=%d census=%d\n",
        shadow.on, shadow.rollouts, shadow.rolloutsWithDisagreement,
        shadow.calls[0], shadow.calls[1], shadow.examined, n);
    ShadowWrite("[SHADOW] contacts old=%d aabb=%d swept=%d endpoint=%d processed=%d\n",
        totals[0], totals[1], totals[2], totals[3], totals[4]);
    ShadowWrite("[SHADOW] refusals=%d native_processed=%d native_endpoint=%d recoverable=%d "
        "rollouts_refused=%d rollouts_refused_recoverable=%d\n",
        shadow.refusals, shadow.refusalsNativeProcessed, shadow.refusalsNativeEndpoint,
        shadow.refusalsRecoverable, shadow.rolloutsRefused, shadow.rolloutsRefusedRecoverable);
    ShadowWrite("[SHADOW] judged=%d recoverable=%d rescued=%d stood=%d\n",
        shadow.judged, shadow.judgedRecoverable,
        shadow.judgedRecoverableRescued, shadow.judgedRecoverableStood);
    for (i = 0; i < BOT2_SHADOW_STATES; i++) {
        if (!shadow.judgedRescuedCell[i] && !shadow.judgedStoodCell[i]) continue;
        ShadowWrite("[SHADOW] verdict flags=%d rescued=%d stood=%d state=%s\n",
            i, shadow.judgedRescuedCell[i], shadow.judgedStoodCell[i],
            ShadowStateName(i, buffer, sizeof(buffer)));
    }
    ShadowWrite("[SHADOW] rollouts tagged=%d untagged=%d\n",
        shadow.taggedRollouts, shadow.untaggedRollouts);
    ShadowWrite("[SHADOW] items examined=%d item_generic_only=%d no_touch_anywhere=%d records=%d dropped=%d\n",
        shadow.itemsExamined, shadow.itemGenericOnly, shadow.noTouchAnywhere,
        shadow.records, shadow.dropped);
    for (i = 0; i < BOT2_SHADOW_STATES; i++) {
        if (!shadow.census[i]) continue;
        ShadowWrite("[SHADOW] cell flags=%d count=%d state=%s\n",
            i, shadow.census[i], ShadowStateName(i, buffer, sizeof(buffer)));
    }
    for (i = 0; i < MAX_GENTITIES; i++) {
        int j, total = 0, disagreed = 0;
        for (j = 0; j < BOT2_SHADOW_STATES; j++) {
            total += shadow.perEntity[i][j];
            if (shadow.perEntity[i][j] && !(j & BOT2_SHADOW_OLD) != !(j & BOT2_SHADOW_PROCESSED))
                disagreed += shadow.perEntity[i][j];
        }
        if (!total) continue;
        ShadowWrite("[SHADOW] trigger entity=%d class=%s examined=%d old_vs_processed_disagreed=%d\n",
            i, g_entities[i].classname ? g_entities[i].classname : "?", total, disagreed);
        for (j = 0; j < BOT2_SHADOW_STATES; j++) {
            if (!shadow.perEntity[i][j]) continue;
            ShadowWrite("[SHADOW] trigger entity=%d flags=%d count=%d state=%s\n",
                i, j, shadow.perEntity[i][j], ShadowStateName(j, buffer, sizeof(buffer)));
        }
    }
    for (i = 0; i < shadow.records; i++) {
        bot2_shadowRecord_t *rec = &shadow.record[i];
        ShadowWrite("[SHADOW] rec=%d plan=%d seq=%d frame=%d boundary=%d entity=%d etype=%d gitype=%d class=%s flags=%d state=%s "
            "before=%.1f,%.1f,%.1f after=%.1f,%.1f,%.1f hull=%.0f,%.0f,%.0f/%.0f,%.0f,%.0f "
            "touch=%d selftouch=%d ducked=%d dead=%d time=%d cmdtime=%d fixed=%d msec=%d\n",
            i, rec->plan, rec->seq, rec->frame, rec->boundary, rec->entity, rec->eType, rec->giType,
            rec->classname, rec->flags, ShadowStateName(rec->flags, buffer, sizeof(buffer)),
            rec->before[0], rec->before[1], rec->before[2],
            rec->after[0], rec->after[1], rec->after[2],
            rec->hullMins[0], rec->hullMins[1], rec->hullMins[2],
            rec->hullMaxs[0], rec->hullMaxs[1], rec->hullMaxs[2],
            rec->hasEntityTouch, rec->hasSelfTouch, rec->ducked, rec->dead,
            rec->levelTime, rec->commandTime, rec->pmoveFixed, rec->pmoveMsec);
    }
    ShadowWrite("[SHADOW] END records=%d dropped=%d\n", shadow.records, shadow.dropped);
    if (shadowFile) {
        trap->FS_Close(shadowFile); shadowFile = 0;
        // Short enough to survive an rcon reply, which is redirected away from
        // the server log and truncated: the file holds the whole census.
        trap->Print("[SHADOW] WROTE %s examined=%d refusals=%d recoverable=%d "
            "rollouts=%d rollouts_refused=%d rollouts_refused_recoverable=%d records=%d dropped=%d\n",
            path, shadow.examined, shadow.refusals, shadow.refusalsRecoverable,
            shadow.rollouts, shadow.rolloutsRefused, shadow.rolloutsRefusedRecoverable,
            shadow.records, shadow.dropped);
    }
    if (reset) {
        int on = shadow.on;
        memset(&shadow, 0, sizeof(shadow));
        shadow.on = on;
    }
}

// One line per trigger refusal when bot_forecastTriggerLog is set: which
// entity, at which frame and position, so a refusal can be placed on the map.
static void TriggerLog(int frame, const vec3_t at) {
    if (!forecastTriggerLog || !forecastTouched) return;
    trap->Print("[FORECAST] trigger frame=%d entity=%d classname=%s at=%.0f,%.0f,%.0f absmin=%.0f,%.0f,%.0f absmax=%.0f,%.0f,%.0f\n",
        frame, forecastTouched->s.number, forecastTouched->classname ? forecastTouched->classname : "?", at[0], at[1], at[2],
        forecastTouched->r.absmin[0], forecastTouched->r.absmin[1], forecastTouched->r.absmin[2],
        forecastTouched->r.absmax[0], forecastTouched->r.absmax[1], forecastTouched->r.absmax[2]);
}

// The game-frame Force update, as WP_ForcePowersUpdate applies it before each
// client's Pmove under the ordinary (non-Siege, non-duel, non-holocron) rules
// the benchmark worlds run: a timed power expires once its duration is behind
// the clock, levitation ends on the ground when no jump start is pending, and
// while no power is active Force regenerates one point per g_forceRegenTime
// from the debounce clock, which an active power holds at the current time.
// Sounds, effects and the Speed powerup timer are outside Pmove's reading.
// bot_moveFixBP12, read once per level.time (a rollout steps many ticks).
static int BoonFix(void) {
    static int at = -1, on;
    if (at != level.time) { at = level.time; on = FixOn("bot_moveFixBP12") > 0; }
    return on;
}

static void ForecastForceUpdate(playerState_t *ps, int time) {
    int i;
    for (i = 0; i < NUM_FORCE_POWERS; i++) {
        // base_enhanced g_fixForceTimers ends a power at `<=` (BE_ForceTimerEnded), stock at `<`.
        if (ps->fd.forcePowerDuration[i] && BE_ForceTimerEnded(ps->fd.forcePowerDuration[i], time)) {
            ps->fd.forcePowersActive &= ~(1 << i);
            ps->fd.forcePowerDuration[i] = 0;
        }
        if (i == FP_LEVITATION && (ps->fd.forcePowersActive & (1 << i)) &&
            ps->groundEntityNum != ENTITYNUM_NONE && !ps->fd.forceJumpZStart)
            ps->fd.forcePowersActive &= ~(1 << i);
        // Absorb and protect (WP_ForcePowerRun): one point every 600 / 300 ms
        // while up, ended at no Force. Neither touches Pmove; the runner raises
        // them (bot_runnerAbsorb) and the rollout must carry them, or every
        // candidate while one is up is refused (records/2026-10-02-runner-absorb-v1.md).
        if ((i == FP_ABSORB || i == FP_PROTECT) && (ps->fd.forcePowersActive & (1 << i)) &&
            ps->fd.forcePowerDebounce[i] < time) {
            BG_ForcePowerDrain(ps, (forcePowers_t)i, 1);
            if (ps->fd.forcePower < 1) {
                ps->fd.forcePowersActive &= ~(1 << i);
                ps->fd.forcePowerDuration[i] = 0;
            }
            ps->fd.forcePowerDebounce[i] = time + (i == FP_ABSORB ? 600 : 300);
        }
    }
    if ((!ps->fd.forcePowersActive || ps->fd.forcePowersActive == (1 << FP_DRAIN)) &&
        !ps->saberInFlight && (ps->weapon != WP_SABER || !BG_SaberInSpecial(ps->saberMove))) {
        // bot_moveFixBP12 (default 1): a boon holder regenerates 6 a tick, as
        // WP_ForcePowerRegenerate(self, 6); 0 keeps one point a tick for everyone.
        int amount = ps->powerups[PW_FORCE_BOON] && BoonFix() ? 6 : 1;
        while (ps->fd.forcePowerRegenDebounceTime < time) {
            if ((ps->fd.forcePower += amount) > ps->fd.forcePowerMax) ps->fd.forcePower = ps->fd.forcePowerMax;
            ps->fd.forcePowerRegenDebounceTime += max(g_forceRegenTime.integer, 1);
        }
    } else ps->fd.forcePowerRegenDebounceTime = time;
}

// The generic command ClientThink_real processes after the frame's Pmove,
// for the one command a movement plan issues: Force Speed. Mirrors the
// think's debounce of a repeated command, then ForceSpeed with
// WP_ForcePowerStart for FP_SPEED: usable when alive, not spectating,
// allowed now, known, levelled, affordable and not already active; then the
// deactivation lockout, the active bit, the window by level, the debounce
// clock and the drain of the power's cost. Sounds are outside the reading.
// A request while Speed is active past its lockout would stop the power,
// which the rollout refuses rather than models; any other generic command
// is refused too, since nothing here models it.
static void ForecastGenericCommand(gentity_t *ent, const usercmd_t *cmd, int time) {
    gclient_t *client = ent->client;
    playerState_t *ps = &client->ps;
    int duration;
    if (!cmd->generic_cmd) return;
    if (cmd->generic_cmd != GENCMD_FORCE_SPEED) { badWorld |= BOT2_FORECAST_POLICY_REFUSED; return; }
    if (cmd->generic_cmd == client->lastGenCmd && client->lastGenCmdTime >= time) return;
    client->lastGenCmd = cmd->generic_cmd;
    client->lastGenCmdTime = time + 300;
    if (ent->health <= 0) return;
    if (ps->fd.forcePowersActive & (1 << FP_SPEED)) {
        if (ps->forceAllowDeactivateTime < time) badWorld |= BOT2_FORECAST_ACTIVE_POWER;
        return;
    }
    if (BG_HasYsalamiri(level.gametype, ps) || ps->stats[STAT_HEALTH] <= 0 || (ps->eFlags & EF_DEAD) ||
        (ps->pm_flags & PMF_FOLLOW) || client->sess.sessionTeam == TEAM_SPECTATOR || client->tempSpectate >= time ||
        !BG_CanUseFPNow(level.gametype, ps, time, FP_SPEED) || !(ps->fd.forcePowersKnown & (1 << FP_SPEED)) ||
        !ps->fd.forcePowerLevel[FP_SPEED] ||
        ps->fd.forcePower < forcePowerNeeded[ps->fd.forcePowerLevel[FP_SPEED]][FP_SPEED]) return;
    switch (ps->fd.forcePowerLevel[FP_SPEED]) {
    case FORCE_LEVEL_1: duration = 10000; break;
    case FORCE_LEVEL_2: duration = 15000; break;
    case FORCE_LEVEL_3: duration = 20000; break;
    default: return;
    }
    ps->forceAllowDeactivateTime = time + 1500;
    ps->fd.forcePowersActive |= (1 << FP_SPEED);
    // A bot's command runs inside the game frame, a step after the last one (BE_FixDuration).
    ps->fd.forcePowerDuration[FP_SPEED] = time + BE_FixDuration(duration, Bot2_GameStepMsec());
    ps->otherSoundLen = 256;
    ps->otherSoundTime = time + 100;
    ps->fd.forcePowerDebounce[FP_SPEED] = 0;
    BG_ForcePowerDrain(ps, FP_SPEED, 0);
}

// Active powers the rollout can carry: none under the legacy contract; Speed,
// levitation, absorb and protect once the Force update is modelled.
// A step that may have drawn from the random stream: the saber's spinning
// animation before or after it (bg_pmove.c, the in-air velocity stir).
static qboolean SpinningSaberStep(const playerState_t *before, const playerState_t *after) {
    return (before->weapon == WP_SABER && BG_SpinningSaberAnim(before->legsAnim)) ||
           (after->weapon == WP_SABER && BG_SpinningSaberAnim(after->legsAnim));
}

static int UnsupportedPowers(const playerState_t *ps) {
    int allowed = activeRequest->modelForce ? (1 << FP_SPEED) | (1 << FP_LEVITATION) | (1 << FP_ABSORB) | (1 << FP_PROTECT) : 0;
    return (ps->fd.forcePowersActive & ~allowed) != 0;
}

static qboolean ValidRequest(const bot2_forecastRequest_t *r) {
    int client;
    int64_t lastTime;
    if (!r || !r->entity || !r->root || !r->root->ps || !r->frames ||
        !r->root->trace || !r->root->pointcontents || r->root->trace == ForecastTrace ||
        r->root->pointcontents == ForecastContents || !r->entity->client ||
        r->root->entSize != sizeof(gentity_t) || !r->command ||
        r->maxSteps < 1 || r->maxSteps > BOT2_FORECAST_MAX_STEPS ||
        r->frameCapacity < r->maxSteps || r->stepMsec < 1 || r->stepMsec > 200 ||
        r->firstTime < r->stepMsec || r->frameOffset < 0 ||
        r->policyExtraSize < 0 || r->policyExtraSize > BOT2_FORECAST_MAX_POLICY_EXTRA ||
        (r->policyExtraSize && !r->policyExtra) ||
        (!!r->firstCommand != !!r->firstPolicyAfter) ||
        (r->firstCommand && r->policyExtraSize && !r->firstPolicyExtraAfter) ||
        (!r->firstCommand && r->firstPolicyExtraAfter) ||
        (r->root->pmove_fixed && (r->root->pmove_msec < 1 || r->root->pmove_msec > 200))) return qfalse;
    client = r->entity->s.number;
    if (client < 0 || client >= level.maxclients || r->entity != &g_entities[client] ||
        r->root->ps->clientNum != client) return qfalse;
    lastTime = (int64_t)r->firstTime + (int64_t)(r->maxSteps - 1) * r->stepMsec;
    // Leave room for fixed-step ceiling and native command-time arithmetic.
    if (lastTime > INT_MAX - 200 ||
        (int64_t)level.framenum + r->frameOffset + r->maxSteps > INT_MAX) return qfalse;
    if (r->firstCommand) {
        int nativeTime=r->firstTime;
        if(r->root->pmove_fixed)
            nativeTime=((nativeTime+r->root->pmove_msec-1)/r->root->pmove_msec)*r->root->pmove_msec;
        if(r->firstCommand->serverTime!=nativeTime)return qfalse;
    }
    return qtrue;
}

// The record of the rollout a buffer last held, or NULL.
static replayRecord_t *ReplayRecord(const bot2_forecastFrame_t *frames) {
    int i;
    for (i = 0; i < REPLAY_BUFFERS; i++) if (replayRecords[i].frames == frames) return &replayRecords[i];
    return NULL;
}

// How many leading steps a buffer may supply to this rollout: 0 unless it is
// another buffer whose last rollout ran in this scope, for this client, from
// this root by value, on this clock and under these settings, and left the
// guards clean. Inside a scope that is slot-and-level clean (SlotIntact); the
// whole world is compared once, at the scope's end.
static int ReplayLimitScope(const replayRecord_t *h, const bot2_forecastRequest_t *r, int client, int framenum,
    qboolean anyScope) {
    if (!h->frames || h->frames == r->frames || !h->clean || (!anyScope && h->scope != scope.serial) || !h->scope || h->client != client ||
        h->count < 2 || h->firstTime != r->firstTime || h->stepMsec != r->stepMsec || h->frameOffset != r->frameOffset ||
        h->framenum != framenum || h->policyExtraSize != r->policyExtraSize || h->modelForce != r->modelForce ||
        h->itemsPass != forecastItemsPass || h->ownFlagPass != forecastOwnFlagPass || h->flagPass != forecastFlagPass ||
        h->allowedContentsMask != r->allowedContentsMask ||
        memcmp(&h->root, r->root, sizeof(pmove_t))) return 0;
    return min(h->count - 1, r->maxSteps);
}

static int ReplayLimit(const replayRecord_t *h, const bot2_forecastRequest_t *r, int client, int framenum) {
    return ReplayLimitScope(h, r, client, framenum, qfalse);
}

static void ReplayRemember(const bot2_forecastRequest_t *r, const bot2_forecastResult_t *out, int client, int framenum) {
    replayRecord_t *h = ReplayRecord(r->frames);
    if (!h) {
        int i;
        h = &replayRecords[0];
        for (i = 1; i < REPLAY_BUFFERS; i++) if (replayRecords[i].used < h->used) h = &replayRecords[i];
    }
    h->frames = r->frames; h->count = out->count; h->used = ++replayClock; h->scope = scope.active ? scope.serial : 0;
    memcpy(&h->root, r->root, sizeof(pmove_t));
    h->client = client; h->firstTime = r->firstTime; h->stepMsec = r->stepMsec; h->frameOffset = r->frameOffset;
    h->framenum = framenum; h->policyExtraSize = r->policyExtraSize; h->modelForce = r->modelForce;
    h->itemsPass = forecastItemsPass; h->ownFlagPass = forecastOwnFlagPass; h->flagPass = forecastFlagPass; h->allowedContentsMask = r->allowedContentsMask;
    h->clean = !out->animationChanged && !out->worldChanged && out->restored && out->animationRestored;
}

qboolean Bot2_ForecastRun(const bot2_forecastRequest_t *r, bot2_forecastResult_t *out) {
    int replayMode, replayed = 0, verified = 0, mismatches = 0;
    // The buffers still agreeing with this rollout, and how far each may go.
    const bot2_forecastFrame_t *hints[REPLAY_BUFFERS]; int hintLimit[REPLAY_BUFFERS], hintCount = 0;
    int savedTime, savedPrevious, savedFrame, savedCount, savedLegs, savedTorso, savedRand;
    int client, i;
    pmove_t *savedPm;
    pml_t savedPml;
    bgEntity_t *savedSelf, *savedVeh;
    qboolean savedSlow, profile, timed; double triggerUs;
    unsigned int worldBefore, levelBefore;
    bot2_maneuverState_t policy;
    double started = 0, partStarted = 0, moveStarted = 0;
    if (!out) return qfalse;
    // Inputs and outputs must be disjoint. In particular, retain a terminal PS
    // separately before reusing this result as the output of another rollout.
    if (running) {
        // Do not corrupt an outer call if a callback incorrectly reuses its
        // active result. Normal nested callers receive their own BUSY result.
        if (out != activeResult) { memset(out, 0, sizeof(*out)); out->status = BOT2_FORECAST_BUSY; }
        return qfalse;
    }
    if (r && ((r->root && r->root->ps == &out->terminalPS) ||
        r->policyExtra == out->terminalPolicyExtra || r->firstPolicyExtraAfter == out->terminalPolicyExtra)) {
        out->status = BOT2_FORECAST_BAD_ARGUMENT; return qfalse;
    }
    memset(out, 0, sizeof(*out));
    if (!ValidRequest(r)) { out->status = BOT2_FORECAST_BAD_ARGUMENT; return qfalse; }
    // A root in a saber lock runs nothing: PM_SaberLocked finds the opponent through the entity copy, whose
    // playerState pointer is the opponent's live one, and its lock break writes it.
    if (r->root->ps->saberLockTime > r->firstTime) {
        out->status = BOT2_FORECAST_REFUSED; out->unsupported = BOT2_FORECAST_MOVEMENT_TYPE; return qfalse;
    }
    if (scope.active) {
        // Single fixed root per scope; a different root is a caller error.
        if (r->root != scope.root) { out->status = BOT2_FORECAST_BAD_ARGUMENT; return qfalse; }
        // A contaminated scope executes nothing further: no world copy, no
        // callback, no Pmove. The refusal is immediate and permanent.
        if (scope.contaminated) {
            out->status = BOT2_FORECAST_REFUSED; out->worldChanged = qtrue;
            scope.guard->refusedRuns++;
            return qfalse;
        }
    }
    running = qtrue; bot2_perfInRollout = 1; activeRequest = r; activeResult = out; badWorld = 0; traceCount = 0;
    forecastCalledCount = forecastCalledOverflow = 0;
    {
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_forecastItems", value, sizeof(value));
        forecastItemsPass = !value[0] || atoi(value) != 0;
        trap->Cvar_VariableStringBuffer("bot_forecastOwnFlag", value, sizeof(value));
        forecastOwnFlagPass = !value[0] || atoi(value) != 0;
        forecastTriggerLog = trap->Cvar_VariableIntegerValue("bot_forecastTriggerLog");
        forecastBodyLog = trap->Cvar_VariableIntegerValue("bot_forecastBodyLog");
        // Read once per rollout, outside the loop: the diagnostic must not be
        // able to change shape part way through a rollout it is observing.
        shadow.on = trap->Cvar_VariableIntegerValue("bot_forecastTriggerShadow");
        if (shadow.on) {
            shadow.rollouts++; shadow.rolloutFlagged = 0; shadow.refusedRolloutFlagged = 0;
            shadow.rolloutRecoverable = 0; shadow.rolloutDecidingFlags = 0;
            if (shadow.tagSeq > 0) shadow.taggedRollouts++; else shadow.untaggedRollouts++;
        }
        // Consumed whether or not the diagnostic is on, so an untagged rollout
        // can never inherit the previous candidate's identity. The caller
        // increments its counter before tagging, so a real seq is always >= 1
        // and a zeroed struct reads as untagged rather than as candidate zero.
        if (shadow.tagSeq > 0) { shadow.runPlan = shadow.tagPlan; shadow.runSeq = shadow.tagSeq; }
        else { shadow.runPlan = shadow.runSeq = -1; }
        shadow.tagPlan = shadow.tagSeq = -1;
    }
    if (shadow.on) {
        // The hull native would use, tracked across the rollout: the root's
        // hull until a Pmove replaces it, exactly as ClientThink_real copies
        // pmove.mins/maxs into r.mins/maxs before G_TouchTriggers reads them.
        VectorCopy(r->root->mins, shadow.hullMins);
        VectorCopy(r->root->maxs, shadow.hullMaxs);
        shadow.pmoveFixed = r->root->pmove_fixed; shadow.pmoveMsec = r->root->pmove_msec;
        shadow.boundary = 0; shadow.frame = 0; shadow.ducked = 0; shadow.commandTime = 0;
    }
    client = r->entity->s.number; policy = r->policy;
    // Under a cost capture every rollout profiles itself, so the capture can
    // say where a rollout's time goes (row 5 attribution). Otherwise as asked.
    profile = r->profile || Bot2_MoveCostClock() > 0; timed = r->measureTime || profile;
    triggerUs = 0;
    memset(&pmProfile, 0, sizeof(pmProfile)); pmProfile.on = profile;
    if (timed) started = Microseconds();
    savedTime = level.time; savedPrevious = level.previousTime; savedFrame = level.framenum;
    savedPm = pm; savedPml = pml; savedSelf = pm_entSelf; savedVeh = pm_entVeh;
    savedSlow = gPMDoSlowFall; savedCount = c_pmove;
    // The game's random stream (records/2026-10-04-search-budget.md): Pmove's spinning-saber stir
    // draws from it, so a rollout that is not put back moves every later draw in the world (aim, spread, spawn
    // choice) by how many rollouts ran, and any change in rollout count (the search budget) changed the game.
    savedRand = Rand_State();
    savedLegs = r->entity->s.legsAnim; savedTorso = r->entity->s.torsoAnim;
    worldBefore = levelBefore = 0;
    if (!scope.active) {
        if (profile) partStarted = Microseconds();
        worldBefore = WorldHash(); levelBefore = HashBytes(&level, sizeof(level));
        if (profile) out->timing.preGuardUs = Microseconds() - partStarted;
    }
    if (profile) partStarted = Microseconds();
    if (scope.active && scope.copyValid) {
        // Only the slot a rollout runs in is written: its own fields below and
        // whatever Pmove writes through its bgEntity. Restore that slot and
        // keep the rest.
        forecastEntities[scope.copyClient] = g_entities[scope.copyClient];
    } else {
        int live = LiveEntities(), n = live > forecastHigh ? live : forecastHigh;
        BOT2_PERF_BEGIN(perfCopy);
        memcpy(forecastEntities, g_entities, sizeof(gentity_t) * n);
        forecastHigh = live;
        BOT2_PERF_END(BOT2_PERF_SCOPE_FIRSTCOPY, perfCopy);
    }
    if (scope.active) { scope.copyValid = 1; scope.copyClient = client; }
    forecastClient = *r->entity->client;
    forecastClient.ps = *r->root->ps;
    if (r->policyExtraSize) memcpy(forecastPolicyExtra.bytes, r->policyExtra, r->policyExtraSize);
    forecastEntities[client].playerState = &forecastClient.ps;
    forecastEntities[client].client = &forecastClient;
    VectorCopy(forecastClient.ps.origin, forecastEntities[client].r.currentOrigin);
    if (r->precedingAnimation) {
        forecastEntities[client].s.legsAnim = r->precedingAnimation[0];
        forecastEntities[client].s.torsoAnim = r->precedingAnimation[1];
        forecastEntities[client].s.legsFlip = r->precedingAnimation[2];
        forecastEntities[client].s.torsoFlip = r->precedingAnimation[3];
    }
    if (profile) out->timing.worldCopyUs = Microseconds() - partStarted;
    // Replay only inside a scope, and never while a diagnostic that observes
    // each step (the trigger shadow, the trigger log) is on.
    replayMode = 0;
    if (scope.active && !shadow.on && !forecastTriggerLog) {
        char value[16];
        trap->Cvar_VariableStringBuffer("bot_forecastReplay", value, sizeof(value));
        // Default on: unset replays; 0 turns it off.
        replayMode = value[0] ? atoi(value) : 1;
        if (replayMode < 0 || replayMode > 2) replayMode = 0;
        if (replayMode) {
            int k, limit;
            for (k = 0; k < REPLAY_BUFFERS; k++)
                if ((limit = ReplayLimit(&replayRecords[k], r, client, savedFrame)) > 0) {
                    hints[hintCount] = replayRecords[k].frames; hintLimit[hintCount++] = limit;
                }
            if (bot2_perfOn) {
                // Perf counters: a buffer that matches in everything but its scope (an upper bound on what keying
                // replay on the boundary instead of the scope could supply).
                int cross = 0;
                for (k = 0; k < REPLAY_BUFFERS; k++)
                    if (!ReplayLimit(&replayRecords[k], r, client, savedFrame) &&
                        (limit = ReplayLimitScope(&replayRecords[k], r, client, savedFrame, qtrue)) > cross) cross = limit;
                if (cross) { bot2_perf.n[BOT2_PERF_XSCOPE]++; bot2_perf.n2[BOT2_PERF_XSCOPE] += cross; }
            }
        }
    }
    for (i = 0; i < r->maxSteps; i++) {
        bot2_forecastFrame_t *f = &r->frames[i];
        int roundedTime;
        bot2_forecastCommandStatus_t action;
        qboolean replayStep;
        int k, kept;
        level.time = r->firstTime + i * r->stepMsec;
        level.previousTime = level.time - r->stepMsec;
        level.framenum = savedFrame + r->frameOffset + i;
        // A bot thinks at the top of the server frame from the previous
        // Pmove's state, and its retained command is consumed by G_RunClient
        // in the entity loop after that frame's WP_ForcePowersUpdate. So the
        // policy sees the pre-update state, the Pmove the updated one, and
        // the frame's before-state records what the Pmove actually reads.
        forecastCommandPS = forecastClient.ps;
        if (i > 0 && r->modelForce) ForecastForceUpdate(&forecastClient.ps, level.time);
        // The buffers whose step i starts where this one does.
        for (k = kept = 0; k < hintCount; k++) {
            const bot2_forecastFrame_t *h = i < hintLimit[k] ? &hints[k][i] : NULL;
            if (h && h->serverTime == level.time && !memcmp(&h->beforePS, &forecastClient.ps, sizeof(playerState_t)) &&
                h->precedingAnimation[0] == forecastEntities[client].s.legsAnim &&
                h->precedingAnimation[1] == forecastEntities[client].s.torsoAnim &&
                h->precedingAnimation[2] == forecastEntities[client].s.legsFlip &&
                h->precedingAnimation[3] == forecastEntities[client].s.torsoFlip) {
                hints[kept] = hints[k]; hintLimit[kept++] = hintLimit[k];
            }
        }
        hintCount = kept;
        f->serverTime = level.time; f->beforePS = forecastClient.ps; f->beforePolicy = policy;
        if (r->policyExtraSize) memcpy(f->beforePolicyExtra, forecastPolicyExtra.bytes, r->policyExtraSize);
        f->precedingAnimation[0] = forecastEntities[client].s.legsAnim;
        f->precedingAnimation[1] = forecastEntities[client].s.torsoAnim;
        f->precedingAnimation[2] = forecastEntities[client].s.legsFlip;
        f->precedingAnimation[3] = forecastEntities[client].s.torsoFlip;
        f->contextHash = 0;
        if (r->contextHash) {
            if (profile) partStarted = Microseconds();
            f->contextHash = r->contextHash(r->root, r->context);
            if (profile) out->timing.contextHashUs += Microseconds() - partStarted;
        }
        if (profile) partStarted = Microseconds();
        if (i == 0 && r->firstCommand) {
            f->command = *r->firstCommand; policy = *r->firstPolicyAfter;
            if (r->policyExtraSize) memcpy(forecastPolicyExtra.bytes, r->firstPolicyExtraAfter, r->policyExtraSize);
        } else {
            action = r->command(&forecastCommandPS, &policy,
                r->policyExtraSize ? forecastPolicyExtra.bytes : NULL, level.time, r->context, &f->command);
            if (action == BOT2_FORECAST_POLICY_COMPLETE) {
                if (profile) out->timing.policyUs += Microseconds() - partStarted;
                // Completion cannot bypass the strict state/trigger subset just
                // because no final command is needed at this native boundary.
                if (forecastClient.ps.pm_type != PM_NORMAL) out->unsupported |= BOT2_FORECAST_MOVEMENT_TYPE;
                if (UnsupportedPowers(&forecastClient.ps)) out->unsupported |= BOT2_FORECAST_ACTIVE_POWER;
                if (forecastClient.ps.forceHandExtend) out->unsupported |= BOT2_FORECAST_HAND_EXTEND;
                if (forecastClient.ps.groundEntityNum != ENTITYNUM_NONE &&
                    !ForecastStandable(forecastClient.ps.groundEntityNum)) {
                    out->unsupported |= BOT2_FORECAST_BODY;
                    if (forecastBodyLog) BodyLog("ground", forecastClient.ps.groundEntityNum);
                }
                if (shadow.on) {
                    shadow.boundary = 1; shadow.frame = out->count;
                    shadow.ducked = (forecastClient.ps.pm_flags & PMF_DUCKED) != 0;
                    shadow.commandTime = forecastClient.ps.commandTime;
                }
                if (TouchesTrigger(forecastClient.ps.origin, forecastClient.ps.origin)) {
                    out->unsupported |= BOT2_FORECAST_TRIGGER; TriggerLog(out->count, forecastClient.ps.origin);
                }
                // A zero-step exit has no native trace/content evidence. This
                // rollout contract requires at least one checked native step.
                if(!out->count)out->unsupported |= BOT2_FORECAST_POLICY_REFUSED;
                out->completion = BOT2_FORECAST_COMPLETION_POLICY;
                break;
            }
            if (action != BOT2_FORECAST_COMMAND) {
                policy = f->beforePolicy;
                if (r->policyExtraSize) memcpy(forecastPolicyExtra.bytes, f->beforePolicyExtra, r->policyExtraSize);
                out->unsupported |= BOT2_FORECAST_POLICY_REFUSED; break;
            }
        }
        if (profile) out->timing.policyUs += Microseconds() - partStarted;
        f->afterPolicy = policy;
        if (r->policyExtraSize) memcpy(f->afterPolicyExtra, forecastPolicyExtra.bytes, r->policyExtraSize);
        // And that issued this step's command. Pmove never reads the policy,
        // so the policies may differ: siblings carry their own plans and
        // issue the same commands for a while.
        for (k = kept = 0; k < hintCount; k++)
            if (!memcmp(&hints[k][i].command, &f->command, sizeof(usercmd_t))) { hints[kept] = hints[k]; hintLimit[kept++] = hintLimit[k]; }
        hintCount = kept;
        // Pmove is deterministic in its inputs except where it draws from the
        // global random stream. The one draw a PM_NORMAL client reaches is the
        // in-air stir of a spinning saber animation; a replay would hand back
        // the sibling's draw, so such a step is always simulated. (The splash
        // draws are vehicle-only, and PM_irand_timesync is seeded by the
        // command's serverTime, which the match compares.)
        replayStep = hintCount > 0 && !SpinningSaberStep(&forecastClient.ps, &hints[0][i].afterPS);
        if (replayStep && replayMode == 1) {
            // The buffer's output and after-state are this step's own.
            const bot2_forecastFrame_t *h = &hints[0][i];
            f->output = h->output; memcpy(f->settings, h->settings, sizeof(f->settings));
            f->afterPS = h->afterPS; forecastClient.ps = f->afterPS;
            replayed++;
        } else {
            qboolean verify = replayStep && replayMode == 2;
            unsigned int badBefore = badWorld;
            if (verify) replaySlot = forecastEntities[client];
            f->output = *r->root; f->output.ps = &forecastClient.ps;
            f->output.baseEnt = (bgEntity_t *)forecastEntities; f->output.cmd = f->command;
            roundedTime = level.time;
            if (f->output.pmove_fixed)
                roundedTime = ((level.time + f->output.pmove_msec - 1) / f->output.pmove_msec) * f->output.pmove_msec;
            if ((f->command.serverTime != level.time && f->command.serverTime != roundedTime) ||
                roundedTime < forecastClient.ps.commandTime) {
                policy = f->beforePolicy;
                if (r->policyExtraSize) memcpy(forecastPolicyExtra.bytes, f->beforePolicyExtra, r->policyExtraSize);
                out->unsupported |= BOT2_FORECAST_COMMAND_TIME; break;
            }
            // An explicit first command may already carry this native ceiling.
            f->output.cmd.serverTime = roundedTime;
            f->output.trace = ForecastTrace; f->output.pointcontents = ForecastContents;
            f->settings[0] = f->output.pmove_fixed; f->settings[1] = f->output.pmove_msec;
            f->settings[2] = f->output.pmove_float; f->settings[3] = f->output.stepSlideFix;
            // GAME animation restart helpers bypass pmove.baseEnt. Present only
            // the preceding animation fields already mirrored by the old forecast.
            r->entity->s.legsAnim = forecastEntities[client].s.legsAnim;
            r->entity->s.torsoAnim = forecastEntities[client].s.torsoAnim;
            if (timed) moveStarted = Microseconds();
            Bot2_Pmove(&f->output);   // the live bot's command slicing
            bot2_meterSteps++;
            if (bot2_perfOn) bot2_perf.n[BOT2_PERF_STEP]++;
            if (timed) out->timing.pmoveUs += Microseconds() - moveStarted;
            out->animationChanged |= r->entity->s.legsAnim != forecastEntities[client].s.legsAnim ||
                r->entity->s.torsoAnim != forecastEntities[client].s.torsoAnim;
            r->entity->s.legsAnim = savedLegs; r->entity->s.torsoAnim = savedTorso;
            f->afterPS = forecastClient.ps;
            if (verify) {
                const bot2_forecastFrame_t *h = &hints[0][i];
                verified++;
                if (memcmp(&f->afterPS, &h->afterPS, sizeof(playerState_t)) || memcmp(&f->output, &h->output, sizeof(pmove_t)) ||
                    memcmp(f->settings, h->settings, sizeof(f->settings)) ||
                    memcmp(&forecastEntities[client], &replaySlot, sizeof(gentity_t)) || badWorld != badBefore ||
                    out->animationChanged) mismatches++;
            }
        }
        if (r->modelForce) ForecastGenericCommand(&forecastEntities[client], &f->command, level.time);
        VectorCopy(forecastClient.ps.origin, forecastEntities[client].r.currentOrigin);
        forecastEntities[client].s.legsAnim = forecastClient.ps.legsAnim;
        forecastEntities[client].s.torsoAnim = forecastClient.ps.torsoAnim;
        forecastEntities[client].s.legsFlip = forecastClient.ps.legsFlip;
        forecastEntities[client].s.torsoFlip = forecastClient.ps.torsoFlip;
        out->count++;
        out->unsupported |= badWorld;
        if (f->beforePS.pm_type != PM_NORMAL || f->afterPS.pm_type != PM_NORMAL)
            out->unsupported |= BOT2_FORECAST_MOVEMENT_TYPE;
        if (UnsupportedPowers(&f->beforePS) || UnsupportedPowers(&f->afterPS))
            out->unsupported |= BOT2_FORECAST_ACTIVE_POWER;
        if (f->afterPS.forceHandExtend) out->unsupported |= BOT2_FORECAST_HAND_EXTEND;
        if (shadow.on) {
            // Post-Pmove hull and substep settings of the frame just simulated.
            VectorCopy(f->output.mins, shadow.hullMins);
            VectorCopy(f->output.maxs, shadow.hullMaxs);
            shadow.pmoveFixed = f->output.pmove_fixed; shadow.pmoveMsec = f->output.pmove_msec;
            shadow.boundary = 0; shadow.frame = out->count;
            shadow.ducked = (f->afterPS.pm_flags & PMF_DUCKED) != 0;
            shadow.commandTime = f->afterPS.commandTime;
        }
        // Lift calls change what later steps may touch, so every step records
        // them, a replayed one included.
        ForecastMoverCalls(f->beforePS.origin, f->afterPS.origin);
        // A fall may hurt; one that would kill refuses the rollout.
        out->fallDamage += FallDamage(&f->beforePS, &f->afterPS);
        if (out->fallDamage > 0 && out->fallDamage >= r->entity->health) out->unsupported |= BOT2_FORECAST_FALL_DAMAGE;
        // The client's forbidden volume: every step, a replayed one too.
        if (client < MAX_CLIENTS && forecastForbid[client].on &&
            Bot2_SegmentEntersBox(f->beforePS.origin, f->afterPS.origin, forecastForbid[client].mins, forecastForbid[client].maxs))
            out->unsupported |= BOT2_FORECAST_FORBIDDEN;
        // A replayed step is not the recorded rollout's last, so it touched no trigger.
        if (!(replayStep && replayMode == 1)) {
            double triggerStarted = profile ? Microseconds() : 0;
            qboolean touched = TouchesTrigger(f->beforePS.origin, f->afterPS.origin);
            if (profile) triggerUs += Microseconds() - triggerStarted;
            if (touched) { out->unsupported |= BOT2_FORECAST_TRIGGER; TriggerLog(out->count, f->afterPS.origin); }
        }
        if (out->unsupported || (r->stopAtTime && level.time >= r->stopTime)) break;
    }
    if (profile) partStarted = Microseconds();
    level.time = savedTime; level.previousTime = savedPrevious; level.framenum = savedFrame;
    pm = savedPm; pml = savedPml; pm_entSelf = savedSelf; pm_entVeh = savedVeh;
    gPMDoSlowFall = savedSlow; c_pmove = savedCount; Rand_Init(savedRand);
    out->animationRestored = r->entity->s.legsAnim == savedLegs && r->entity->s.torsoAnim == savedTorso;
    if (profile) out->timing.restoreUs = Microseconds() - partStarted;
    if (profile || (scope.active && timed)) partStarted = Microseconds();
    if (scope.active) {
        out->worldChanged = !SlotIntact(client);
        if (out->worldChanged) {scope.contaminated = qtrue; ContaminationLog(client);}
        scope.guard->runs++;
        if (timed) { out->timing.postGuardUs = Microseconds() - partStarted; scope.guard->compareUs += out->timing.postGuardUs; }
    } else out->worldChanged = WorldHash() != worldBefore || HashBytes(&level, sizeof(level)) != levelBefore;
    out->restored = level.time == savedTime && level.previousTime == savedPrevious && level.framenum == savedFrame &&
        pm == savedPm && !memcmp(&pml, &savedPml, sizeof(pml)) && pm_entSelf == savedSelf && pm_entVeh == savedVeh &&
        gPMDoSlowFall == savedSlow && c_pmove == savedCount;
    if (profile && !scope.active) out->timing.postGuardUs = Microseconds() - partStarted;
    out->traceCount = traceCount; out->terminalPS = forecastClient.ps; out->terminalPolicy = policy;
    if (r->policyExtraSize) memcpy(out->terminalPolicyExtra, forecastPolicyExtra.bytes, r->policyExtraSize);
    out->terminalAnimation[0] = forecastEntities[client].s.legsAnim;
    out->terminalAnimation[1] = forecastEntities[client].s.torsoAnim;
    out->terminalAnimation[2] = forecastEntities[client].s.legsFlip;
    out->terminalAnimation[3] = forecastEntities[client].s.torsoFlip;
    if (timed) out->timing.totalUs = Microseconds() - started;
    if (profile) {
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_FC_COPY, out->timing.worldCopyUs + out->timing.preGuardUs);
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_FC_PMOVE, out->timing.pmoveUs);
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_FC_GUARD, out->timing.postGuardUs);
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_FC_POLICY, out->timing.policyUs + out->timing.contextHashUs);
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_FC_TRIGGER, triggerUs);
        Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_FC_TOTAL, out->timing.totalUs);
        {
            int p;
            for (p = 1; p < 10; p++) Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_PM_SETUP + p - 1, pmProfile.phase[p]);
            Bot2_MoveCostAdd(BOT2_MOVECOST_SECTION_PM_TRACE, pmProfile.traceUs);
            Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_PM_TRACES, pmProfile.traces);
        }
    }
    pmProfile.on = 0;
    out->status = !out->unsupported && !out->worldChanged && out->restored &&
        !out->animationChanged && out->animationRestored ? BOT2_FORECAST_COMPLETE : BOT2_FORECAST_REFUSED;
    ReplayRemember(r, out, client, savedFrame);
    if (bot2_perfOn) { bot2_perf.n[BOT2_PERF_ROLLOUT]++; bot2_perf.n[BOT2_PERF_REPLAYED] += replayed; }
    if (replayed) Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_REPLAY_STEPS, replayed);
    if (verified) {
        Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_REPLAY_VERIFIED, verified);
        Bot2_MoveCostCountAdd(BOT2_MOVECOST_COUNT_REPLAY_MISMATCHES, mismatches);
    }
    if (out->status != BOT2_FORECAST_COMPLETE) out->completion = BOT2_FORECAST_COMPLETION_NONE;
    else if (out->completion == BOT2_FORECAST_COMPLETION_NONE) out->completion = BOT2_FORECAST_COMPLETION_HORIZON;
    activeRequest = NULL; activeResult = NULL; running = qfalse; bot2_perfInRollout = 0;
    return out->status == BOT2_FORECAST_COMPLETE;
}

// The snapshot-skip verify mode (bot_forecastScopeSkip 2; perf counters only, it skips nothing). A
// Begin in the same trace-cache stamp and game frame as a clean End: End proved the world equal to the baseline
// still held, so if nothing outside a scope wrote to the world since, the snapshot copy (and the scope's first full
// forecast copy) could be skipped. This counts whether that holds by comparing, before the copy overwrites it.
static void ScopeSkipVerify(void) {
    char value[8];
    int i;
    if (!lastEnd.valid || !traceCache.active || lastEnd.stamp != traceCache.stamp || lastEnd.time != level.time) return;
    trap->Cvar_VariableStringBuffer("bot_forecastScopeSkip", value, sizeof(value));
    if (atoi(value) != 2) return;
    bot2_perf.n[BOT2_PERF_SKIP_ELIGIBLE]++;
    if (!BaselineIntact()) { bot2_perf.n[BOT2_PERF_SKIP_DIFF]++; return; }
    bot2_perf.n[BOT2_PERF_SKIP_SAME]++;
    if (!lastEnd.copyValid) return;
    for (i = 0; i < MAX_GENTITIES; i++)
        if (i != lastEnd.copyClient && memcmp(&forecastEntities[i], &g_entities[i], sizeof(gentity_t))) return;
    bot2_perf.n[BOT2_PERF_SKIP_COPYSAME]++;
}

unsigned int Bot2_ForecastScopeSerial(void) { return scope.active ? scope.serial : 0; }

qboolean Bot2_ForecastGuardBegin(const pmove_t *root, bot2_forecastGuard_t *guard) {
    double started;
    int i;
    // Nested (inside a rollout), reused (already open) or malformed: refuse.
    if (running || scope.active || !root || !guard) return qfalse;
    if (bot2_perfOn) ScopeSkipVerify();
    memset(guard, 0, sizeof(*guard));
    started = Microseconds();
    baselineCount = LiveEntities();
    memcpy(baselineEntities, g_entities, sizeof(gentity_t) * baselineCount);
    baselineLevel = level;
    guard->bytes = sizeof(gentity_t) * baselineCount + sizeof(level);
    for (i = 0; i < MAX_CLIENTS; i++) {
        scope.present[i] = i < level.maxclients && g_entities[i].client != NULL;
        if (scope.present[i]) { baselineClients[i] = *g_entities[i].client; guard->bytes += sizeof(gclient_t); }
    }
    guard->snapshotUs = Microseconds() - started;
    if (bot2_perfOn) { bot2_perf.n[BOT2_PERF_SCOPE]++; bot2_perf.us[BOT2_PERF_SCOPE] += guard->snapshotUs; }
    scope.active = qtrue; scope.contaminated = qfalse; scope.root = root; scope.guard = guard;
    scope.copyValid = 0;
    if (++scope.serial == 0) scope.serial = 1;
    return qtrue;
}

qboolean Bot2_ForecastGuardEnd(bot2_forecastGuard_t *guard) {
    qboolean clean;
    if (!scope.active || guard != scope.guard || running) return qfalse;
    // The whole-world compare runs here, once: a rollout that wrote outside
    // its own slot contaminates the scope as it always has.
    {
        BOT2_PERF_BEGIN(perfEnd);
        if (!scope.contaminated && !BaselineIntact()) scope.contaminated = qtrue;
        BOT2_PERF_END(BOT2_PERF_SCOPE_END, perfEnd);
    }
    clean = !scope.contaminated;
    guard->contaminated = scope.contaminated;
    lastEnd.valid = clean; lastEnd.time = level.time; lastEnd.stamp = traceCache.active ? traceCache.stamp : 0;
    lastEnd.copyValid = scope.copyValid; lastEnd.copyClient = scope.copyClient;
    scope.active = qfalse; scope.contaminated = qfalse; scope.root = NULL; scope.guard = NULL;
    return clean;
}
