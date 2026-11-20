// Localhost play-test probes (bot2_probe.c): missile traces, generic commands, test rockets.
#ifndef BOT2_PROBE_H
#define BOT2_PROBE_H

typedef struct {
    int on, force, handExtend, weaponTime, speedActive, usable, saberInFlight, health, debounce;
} bot2_probeGenCmd_t;

void Bot2_ProbeBegin(const char *phase, int client);
void Bot2_ProbeEnd(void);
void Bot2_ProbeGenCmdArrive(gentity_t *ent, const usercmd_t *cmd);
void Bot2_ProbeGenCmdBefore(gentity_t *ent, int cmd, bot2_probeGenCmd_t *state);
void Bot2_ProbeGenCmdAfter(gentity_t *ent, int cmd, const bot2_probeGenCmd_t *state);
void Svcmd_ProbeRocket_f(void);

#endif
