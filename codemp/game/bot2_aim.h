// Shared aim kernel (bot2_aim.c): where a weapon must be pointed to hit a
// target as the shooter perceives it, and a humanized view that gets there.
// Policy passes the perceived target state; nothing here reads the target
// entity, so the knowledge boundary is the caller's (docs/movement/PLAN.md,
// milestone A).
#ifndef BOT2_AIM_H
#define BOT2_AIM_H

// A target as perceived: where and how fast, whether on the ground, its hull.
typedef struct {
    vec3_t origin, velocity, mins, maxs;
    qboolean grounded;
    int skip;                   // the target's entity number, ignored by traces
} bot2_aimTarget_t;

// The fire mode a solution is for.
#define BOT2_AIM_PRIMARY 0
#define BOT2_AIM_ALT 1

// Solution flags.
#define BOT2_AIM_OK 1           // a direction was found
#define BOT2_AIM_LOB 2          // a gravity arc (repeater alt, flechette alt, thermal)
#define BOT2_AIM_SPLASH 4       // aimed at the floor for the burst
#define BOT2_AIM_RANGE 8        // the arc cannot reach: the maximum-range angle was used
#define BOT2_AIM_HITSCAN 16
#define BOT2_AIM_SYNC 32        // the target is on the floor when the shot arrives (grounded now, or lands first)

typedef struct {
    int flags;
    vec3_t point;               // what the shot is meant to reach
    vec3_t angles;              // view angles that send it there
    float tof;                  // seconds to arrive
    // Flechette alt only (0 otherwise): the chance one ball hits directly and
    // that at least one of the two does, over the balls' random rolls
    // against the predicted path (the golan hit-envelope lesson,
    // cg_aimfeedback.c, docs/golan_fixed_frame_lesson.md).
    float chance1, chance;
} bot2_aimSolution_t;

// The direction for weapon/mode from eye at the perceived target.
int Bot2_AimSolve(const vec3_t eye, int weapon, int mode, const bot2_aimTarget_t *target, bot2_aimSolution_t *out);
// The same lead at the target's body, never the floor under it: the fallback
// when a splash weapon's floor point is hidden from the shooter.
int Bot2_AimSolveDirect(const vec3_t eye, int weapon, int mode, const bot2_aimTarget_t *target, bot2_aimSolution_t *out);
// Carries a perceived target `seconds` forward by the same forecast the lead
// uses (straight, falling while airborne, never below the floor): a state
// perceived that long ago, brought to now.
void Bot2_AimAdvance(bot2_aimTarget_t *target, float seconds);

// The human aim point (records/2026-09-28-human-aim-study.md): 120 demos,
// 31,617 fires. Humans aim at the target's current centre with no flight-time
// lead, pitched per mode (repeater and flechette alt and thermal 4/2/2 degrees
// high, rocket 2 low); with the disruptor they aim where the target was 50-100
// ms before (pass that perceived state as `target`). Same solution struct; the
// angles are straight at the point.
int Bot2_AimHumanPoint(const vec3_t eye, int weapon, int mode, const bot2_aimTarget_t *target, bot2_aimSolution_t *out);
// The humans' per-axis error (deg, standard deviation) for the mode at the
// target's angular speed as seen from the shooter (deg/s): the study's median
// radial error, base(mode) + k * speed, over 1.177 (a radial median of a 2D
// Gaussian). Scaled by `scale` for calibration.
float Bot2_AimHumanError(int weapon, int mode, float angularSpeed, float scale);

// A splash mode's burst radius (g_weapon.c), 0 for a mode without splash.
float Bot2_AimSplashRadius(int weapon, int mode);
// Measured hit chance of a shot against the movement runner (records/
// 2026-09-28-aim-kernel.md, fire-at-will worlds at the humans' error) by the
// reach ratio: the target's speed times the shot's flight time over the splash
// radius. 0 for modes not measured.
float Bot2_AimHitChance(int weapon, int mode, float reachRatio);
// Damage a hit does (direct, g_weapon.c), for choosing between weapons.
float Bot2_AimDamage(int weapon, int mode);

// A humanized view (per client): it matches the desired angles' own motion
// (smooth pursuit: a tracked target does not fall behind by rate x lag) and
// closes the remaining gap with a first-order lag, all under a turn-rate cap,
// plus a slowly varying aim error from a seeded hash, so pinned worlds stay
// deterministic. Parameters per call; a reset starts a new acquisition.
typedef struct {
    float lagMs;                // first-order tracking time constant
    float turnRate;             // deg/s cap
    float errorDeg;             // standard deviation of the aim error
    float errorMs;              // correlation time of the error
} bot2_aimHuman_t;

// Resets a client's view state to its current angles (a spawn, a release).
void Bot2_AimHumanReset(int client, const vec3_t viewAngles);
// Steps the client's view toward desired at this level time and writes the
// view to hold this frame; returns the angle (deg) left between the view
// (without its error) and desired, for fire gating.
float Bot2_AimHumanStep(int client, const vec3_t desired, const bot2_aimHuman_t *h, vec3_t outAngles);

#endif
