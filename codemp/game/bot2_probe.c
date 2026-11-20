// Localhost probes for play-test questions. All off unless set.
//
//   bot_probeMissileTrace 1  While the Bot2 play frame thinks, every trace the
//                            runner makes goes through a counter; a trace that
//                            hits a missile (a rocket's contents are MASK_SHOT,
//                            solid to MASK_PLAYERSOLID) is printed after the
//                            frame as [PROBE] missile_hit.
//   bot_probeGenCmd 1        Every push, pull or speed command a human client's
//                            usercmd carries is printed on arrival
//                            ([PROBE] gencmd_arrive) and where the game acts on
//                            it ([PROBE] gencmd), with the state that decides
//                            whether it fires and whether it did.
//   probe_rocket <client> <cross|head> [distance]
//                            Spawns a rocket with no damage and no splash
//                            ahead of that client: crossing its path, or flying
//                            back along it at it.
#include "g_local.h"
#include "bot2_probe.h"

#define PROBE_ROCKET_SPEED 900.0f

qboolean WP_ForcePowerUsable(gentity_t *self, forcePowers_t forcePower);   // w_force.c

static struct {
    int on, traces, hits, firstEnt, firstMask, client;
    const char *phase;
    char firstClass[32];
    void (*engineTrace)(trace_t *, const vec3_t, const vec3_t, const vec3_t, const vec3_t, int, int, int, int, int);
} missileProbe;

static void ProbeTrace(trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
    const vec3_t end, int passEntityNum, int contentmask, int capsule, int traceFlags, int useLod) {
    missileProbe.engineTrace(results, start, mins, maxs, end, passEntityNum, contentmask, capsule, traceFlags, useLod);
    missileProbe.traces++;
    if (results->entityNum >= MAX_CLIENTS && results->entityNum < ENTITYNUM_WORLD &&
        g_entities[results->entityNum].inuse && g_entities[results->entityNum].s.eType == ET_MISSILE) {
        if (!missileProbe.hits++) {
            missileProbe.firstEnt = results->entityNum; missileProbe.firstMask = contentmask;
            Q_strncpyz(missileProbe.firstClass, g_entities[results->entityNum].classname ?
                g_entities[results->entityNum].classname : "?", sizeof(missileProbe.firstClass));
        }
    }
}

// phase: think (the Bot2 play frame), plan (a bot's own pre-Pmove movement hooks, where its
// searches and rollouts run) or pmove (its native physics step).
void Bot2_ProbeBegin(const char *phase, int client) {
    missileProbe.on = trap->Cvar_VariableIntegerValue("bot_probeMissileTrace") != 0;
    if (!missileProbe.on) return;
    missileProbe.traces = missileProbe.hits = 0;
    missileProbe.phase = phase; missileProbe.client = client;
    missileProbe.engineTrace = trap->Trace;
    trap->Trace = ProbeTrace;
}

void Bot2_ProbeEnd(void) {
    int i, missiles = 0;
    if (!missileProbe.on) return;
    trap->Trace = missileProbe.engineTrace;
    for (i = MAX_CLIENTS; i < level.num_entities; i++)
        if (g_entities[i].inuse && g_entities[i].s.eType == ET_MISSILE) missiles++;
    if (missileProbe.hits)
        trap->Print("[PROBE] missile_hit level=%d phase=%s client=%d traces=%d hits=%d ent=%d class=%s mask=0x%x missiles=%d\n",
            level.time, missileProbe.phase, missileProbe.client, missileProbe.traces, missileProbe.hits,
            missileProbe.firstEnt, missileProbe.firstClass, missileProbe.firstMask, missiles);
    else if (missiles)
        trap->Print("[PROBE] missile_clear level=%d phase=%s client=%d traces=%d missiles=%d\n", level.time,
            missileProbe.phase, missileProbe.client, missileProbe.traces, missiles);
}

static const char *GenCmdName(int cmd) {
    switch (cmd) {
    case GENCMD_FORCE_THROW: return "push";
    case GENCMD_FORCE_PULL: return "pull";
    case GENCMD_FORCE_SPEED: return "speed";
    default: return NULL;
    }
}

static int GenCmdProbed(const gentity_t *ent, int cmd) {
    return ent->client && !(ent->r.svFlags & SVF_BOT) && GenCmdName(cmd) &&
        trap->Cvar_VariableIntegerValue("bot_probeGenCmd") != 0;
}

void Bot2_ProbeGenCmdArrive(gentity_t *ent, const usercmd_t *cmd) {
    if (!cmd || !GenCmdProbed(ent, cmd->generic_cmd)) return;
    trap->Print("[PROBE] gencmd_arrive level=%d client=%d cmd=%s serverTime=%d commandTime=%d\n",
        level.time, (int)(ent - g_entities), GenCmdName(cmd->generic_cmd), cmd->serverTime,
        ent->client->ps.commandTime);
}

void Bot2_ProbeGenCmdBefore(gentity_t *ent, int cmd, bot2_probeGenCmd_t *state) {
    playerState_t *ps = &ent->client->ps;
    int power = cmd == GENCMD_FORCE_PULL ? FP_PULL : cmd == GENCMD_FORCE_SPEED ? FP_SPEED : FP_PUSH;
    state->on = GenCmdProbed(ent, cmd);
    if (!state->on) return;
    state->force = ps->fd.forcePower; state->handExtend = ps->forceHandExtend;
    state->weaponTime = ps->weaponTime; state->speedActive = (ps->fd.forcePowersActive & (1 << FP_SPEED)) != 0;
    state->usable = WP_ForcePowerUsable(ent, (forcePowers_t)power);
    state->saberInFlight = ps->saberInFlight; state->health = ent->health;
    state->debounce = ps->fd.forcePowerDebounce[power] - level.time;
}

void Bot2_ProbeGenCmdAfter(gentity_t *ent, int cmd, const bot2_probeGenCmd_t *state) {
    playerState_t *ps = &ent->client->ps;
    int fired;
    if (!state->on) return;
    if (cmd == GENCMD_FORCE_SPEED)
        fired = ((ps->fd.forcePowersActive & (1 << FP_SPEED)) != 0) != state->speedActive;
    else
        fired = ps->forceHandExtend != state->handExtend || ps->fd.forcePower < state->force;
    trap->Print("[PROBE] gencmd level=%d client=%d cmd=%s fired=%d force=%d->%d weaponTime=%d handExtend=%d->%d "
        "usable=%d saberInFlight=%d health=%d debounce=%d speedActive=%d\n",
        level.time, (int)(ent - g_entities), GenCmdName(cmd), fired, state->force, ps->fd.forcePower,
        state->weaponTime, state->handExtend, ps->forceHandExtend, state->usable, state->saberInFlight,
        state->health, state->debounce, state->speedActive);
}

void Svcmd_ProbeRocket_f(void) {
    char arg[32];
    gentity_t *target, *missile;
    vec3_t along, side, at, from, dir;
    float speed, distance;
    int client, head;
    trap->Argv(1, arg, sizeof(arg)); client = atoi(arg);
    trap->Argv(2, arg, sizeof(arg)); head = !Q_stricmp(arg, "head");
    trap->Argv(3, arg, sizeof(arg)); distance = arg[0] ? (float)atof(arg) : 300;
    if (client < 0 || client >= MAX_CLIENTS || !g_entities[client].inuse || !g_entities[client].client) {
        trap->Print("[PROBE] rocket refused: no client %d\n", client);
        return;
    }
    target = &g_entities[client];
    VectorSet(along, target->client->ps.velocity[0], target->client->ps.velocity[1], 0);
    speed = VectorNormalize(along);
    if (speed < 50) {  // standing: use the view direction
        AngleVectors(target->client->ps.viewangles, along, NULL, NULL);
        along[2] = 0; VectorNormalize(along);
    }
    VectorSet(side, -along[1], along[0], 0);
    // A point the client will pass about a third of a second from now, at chest height.
    VectorMA(target->client->ps.origin, max(speed, 300.0f) * .35f, along, at);
    at[2] += 16;
    if (head) VectorMA(at, distance, along, from);
    else VectorMA(at, distance, side, from);
    VectorSubtract(at, from, dir); VectorNormalize(dir);
    missile = CreateMissile(from, dir, PROBE_ROCKET_SPEED, 10000, &g_entities[ENTITYNUM_WORLD], qfalse);
    missile->classname = "rocket_proj";
    missile->s.weapon = WP_ROCKET_LAUNCHER;
    VectorSet(missile->r.maxs, 3, 3, 3); VectorScale(missile->r.maxs, -1, missile->r.mins);
    // A rocket's collision (g_weapon.c WP_FireRocket) with nothing that can hurt or push.
    missile->damage = 0; missile->splashDamage = 0; missile->splashRadius = 0;
    missile->methodOfDeath = MOD_ROCKET; missile->splashMethodOfDeath = MOD_ROCKET_SPLASH;
    missile->r.contents = MASK_SHOT; missile->clipmask = MASK_SHOT;
    missile->bounceCount = 0;
    trap->LinkEntity((sharedEntity_t *)missile);
    trap->Print("[PROBE] rocket level=%d ent=%d mode=%s client=%d from=%.0f,%.0f,%.0f at=%.0f,%.0f,%.0f "
        "client_origin=%.0f,%.0f,%.0f client_speed=%.0f\n", level.time, missile->s.number, head ? "head" : "cross",
        client, from[0], from[1], from[2], at[0], at[1], at[2], target->client->ps.origin[0],
        target->client->ps.origin[1], target->client->ps.origin[2], speed);
}
