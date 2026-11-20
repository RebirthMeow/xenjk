#ifndef BOT2_MOVECONTROL_H
#define BOT2_MOVECONTROL_H

// Navigation specifies desired world velocity and jump intent, not key presses
// or view angles. The local controller resolves those from actual momentum.
typedef struct {
    vec3_t velocity;
    float turnRate;
    int upmove;
    qboolean allowStop;
    // Airborne only: 0 tracks the velocity above; +1 or -1 holds that side's
    // forward diagonal (+1 is forward+right) and picks the view that adds the
    // most speed, within the same turn and facing limits.
    int strafeSide;
    // With strafeSide: the share of the most speed a command can add that the
    // held side takes, by aiming wide of the best view (a human's usual miss).
    // 0 or 1 and above is perfect. Deterministic, so a forecast that sets the
    // same value keeps parity.
    float strafeSkill;
    // With strafeSide: the Pmove slice length in seconds the held side's
    // per-slice gain is computed for; 0 uses the command's dt. bot_moveFixBM8
    // (default 0): set to pmove_msec when the bot
    // steps fixed, so the strafe angle does not depend on the server tick.
    float strafeSlice;
} bot2_moveIntent_t;

void Bot2_MoveWaypointIntent(const playerState_t *ps, const vec3_t target,
    float speed, float radius, bot2_moveIntent_t *intent);
int Bot2_MoveControl(const playerState_t *ps, const bot2_moveIntent_t *intent,
    float yaw, float dt, qboolean fast, usercmd_t *cmd, float *outYaw);
// Optional legal analog throttle for a prepared ground-speed limit. Prediction
// and emitted input use the same quantized magnitude. Legacy callers stay full
// throttle; no player state, native speed, force or physics is changed.
int Bot2_MoveControlLimited(const playerState_t *ps, const bot2_moveIntent_t *intent,
    float yaw, float dt, qboolean fast, float wishLimit, usercmd_t *cmd, float *outYaw);
#endif
