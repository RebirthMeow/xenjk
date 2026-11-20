// Reaction time to moving threats.
//
// A missile (a rocket's contents are MASK_SHOT: solid to every movement trace)
// is a wall the instant it exists, so without this the runner's searches and
// corridor checks steer around a rocket on the frame it is fired - faster than
// any player could. Walls and other fixed geometry a player is expected to
// know; a moving threat has to be seen first.
//
// bot_threatReactMs (default 850: a rocket's flight between the
// Kejim bolt and power cell pickups, 768 units at 900 u/s; 0 = instant): while a bot decides - the Bot2
// play frame and its own pre-Pmove movement hooks, where plans, rollouts and
// corridor checks run - every missile first seen less than this long ago is
// unlinked, so no trace can hit it, and relinked before anything else runs.
// Its native physics step is never touched: the body still collides.
#include "g_local.h"
#include "bot2_threat.h"

#define THREAT_MAX_HIDDEN 256

static struct {
    int scanned, react, top;
    int firstSeen[MAX_GENTITIES];
    unsigned char missile[MAX_GENTITIES];
    int hidden[THREAT_MAX_HIDDEN], hiddenCount, depth;
} threat;

// Every hide notes when each missile was first seen; a slot that was not a
// missile on the previous scan holds a new one (G_Spawn does not reuse a slot
// freed less than a second ago once the level is running). Every hide, not once
// a frame: a missile fired during the client loop or seen by the frame-end pool
// was missed by the frame's first scan and showed at age 0.
// The setting is read once a frame.
static void Scan(void) {
    int i, n = level.num_entities < ENTITYNUM_WORLD ? level.num_entities : ENTITYNUM_WORLD;
    if (threat.scanned != level.time) {
        char value[16];
        threat.scanned = level.time;
        trap->Cvar_VariableStringBuffer("bot_threatReactMs", value, sizeof(value));
        threat.react = value[0] ? atoi(value) : 850;
    }
    // Slots at or above level.num_entities are never in use: clear them once if the count ever drops, then walk
    // only the live range.
    for (i = n; i < threat.top; i++) threat.missile[i] = 0;
    threat.top = n;
    for (i = MAX_CLIENTS; i < n; i++) {
        int is = g_entities[i].inuse && g_entities[i].s.eType == ET_MISSILE;
        // A slot freed after its missile was first seen holds a new one: G_Spawn reuses a slot at once in a level's
        // first 2 s (or when slots run out), so it can be a missile on two scans in a row.
        if (is && (!threat.missile[i] || g_entities[i].freetime > threat.firstSeen[i])) threat.firstSeen[i] = level.time;
        threat.missile[i] = (unsigned char)is;
    }
}

void Bot2_ThreatHideBegin(void) {
    int i;
    Scan();
    if (threat.depth++ || threat.react <= 0) return;
    threat.hiddenCount = 0;
    for (i = MAX_CLIENTS; i < level.num_entities && threat.hiddenCount < THREAT_MAX_HIDDEN; i++) {
        gentity_t *ent = &g_entities[i];
        if (!threat.missile[i] || !ent->r.linked || level.time - threat.firstSeen[i] >= threat.react) continue;
        trap->UnlinkEntity((sharedEntity_t *)ent);
        threat.hidden[threat.hiddenCount++] = i;
    }
}

void Bot2_ThreatHideEnd(void) {
    int i;
    if (!threat.depth || --threat.depth) return;
    for (i = 0; i < threat.hiddenCount; i++) {
        gentity_t *ent = &g_entities[threat.hidden[i]];
        if (ent->inuse) trap->LinkEntity((sharedEntity_t *)ent);
    }
    threat.hiddenCount = 0;
}

// Failure attribution (bot2_move.c SearchFailed): the
// shared graph must not lose an edge to a body standing on it. Only what is
// linked now is unlinked, and only that is relinked, so the threat hide above
// (which may already hold some young missiles) is left as it was.
#define TRANSIENT_MAX_HIDDEN (MAX_CLIENTS + THREAT_MAX_HIDDEN)
static struct { int hidden[TRANSIENT_MAX_HIDDEN], count, active; } transient;

void Bot2_TransientHideBegin(int self) {
    int i;
    if (transient.active) return;
    transient.active = 1; transient.count = 0;
    for (i = 0; i < level.num_entities && transient.count < TRANSIENT_MAX_HIDDEN; i++) {
        gentity_t *ent = &g_entities[i];
        if (i == self || !ent->inuse || !ent->r.linked) continue;
        if (i >= MAX_CLIENTS && ent->s.eType != ET_MISSILE && ent->s.eType != ET_NPC) continue;
        trap->UnlinkEntity((sharedEntity_t *)ent);
        transient.hidden[transient.count++] = i;
    }
}

void Bot2_TransientHideEnd(void) {
    int i;
    if (!transient.active) return;
    for (i = 0; i < transient.count; i++) {
        gentity_t *ent = &g_entities[transient.hidden[i]];
        if (ent->inuse) trap->LinkEntity((sharedEntity_t *)ent);
    }
    transient.count = 0; transient.active = 0;
}
