// Declared fixture defender: one enemy bot (Defender_v2) that holds a post and
// pushes or pulls the movement runner when a throw would put it into a lethal
// hurt volume (a pit). Localhost movement worlds only; the runner's policy is
// untouched and learns nothing from it.
//
// The throw is the native one (ForceThrow, GENCMD_FORCE_THROW/PULL); the
// defender only chooses when. Its choice replays ForceThrow's velocity rule on
// the runner's state as it perceived it bot_defenderReactMs ago, then sweeps
// the runner's hull ballistically (no input, gravity, slides off steep planes)
// until it lands, touches a lethal trigger_hurt or reaches the horizon. It
// throws only when the throw is lethal and the runner's own flight is not.
// Whether the native throw hit and what became of the runner are logged, so
// the prediction is scored against the world, never assumed.
//
// bot_defender 0 holds the post and throws nothing (a body only); 1 throws
// when the passive flight is lethal; 2 only when no held air input escapes.
//
// bot_defenderChase 1 makes it a chaser: it holds the post until the runner
// carries its flag, then walks the runner's own movement graph
// (Bot2_ExecuteMovement) toward where it last saw the carrier, throwing by
// the same rule and turning to aim only on the throw frame. It walks: the
// hop searches stay the runner's (MoveSearchGates), so a chaser learns no
// strafe edges into the shared graph and cannot change the runner's routes.
//
// bot_defenderStrafe 1 (with bot_defenderChase 1) is the strafing chaser
//: the chaser runs the runner's own searches - hops,
// strafe chords, drops, pads - as a guest (MoveGuest, bot2_move.c) that writes
// nothing into the shared graph and never touches the runner's parity harness,
// decision tape or directives. Its goal is still the carrier where it was last
// seen, but held: every goal change is a movement reset that drops a committed
// hop, so a new sighting replaces the goal only once it is
// bot_defenderRetarget units (default 256) from it, or the chaser has come
// within DEFENDER_GOAL_REACHED of the goal it holds. bot_defenderSpeed 1 gives
// it Speed 3 at the release, as the corner start gives the runner its Speed
// (declared fixture setup; movement_bench.py --defender-speed), and it buys
// Speed as the runner does, from the pool its throws use; a lethal throw it could not afford is logged as
// STARVED (at most every DEFENDER_STARVED_MS), so the two bidders can be read.
//
// The base defender (position around the flag, shoot the enemy, chase him if he
// takes the flag, pull him if he is getting too far, ideally shooting and
// pulling), all off by default:
//   bot_defenderGuard 1     while not chasing it stands on one of its posts
//                           (bot_defenderPosts "x y z;x y z;...", else the
//                           release post): the first, or while the runner is
//                           within DEFENDER_GUARD_RANGE of the own flag stand
//                           the post nearest the runner's line to the stand,
//                           moving there by the movement owner.
//   bot_defenderWeapon N    gives it weapon N (bg_weapons.h) with full ammo at
//                           the release and shoots the perceived runner within
//                           bot_defenderShootRange (default 1500): primary, or
//                           alt with bot_defenderAlt 1, a pistol charged for
//                           bot_defenderChargeMs when set. Where to point is the
//                           shared kernel's (Bot2_AimSolve, bot2_aim.c: lead,
//                           gravity arcs, splash at the floor); how the view
//                           gets there is its humanized view (Bot2_AimHumanStep:
//                           bot_aimLagMs, bot_aimTurnRate, bot_aimErrorDeg,
//                           bot_aimErrorMs; all 0 is exact). It fires when the
//                           view is within bot_aimFireDeg (default 3) of the
//                           solution. bot_aimStyle 1 is the human style
//                           (records/2026-09-28-human-aim-study.md): the human
//                           aim point (no lead; disruptor 75 ms behind), the
//                           humans' error by mode and target angular speed
//                           (Bot2_AimHumanError, times bot_aimErrorScale),
//                           lag 300 ms and 500 deg/s unless set, fire only
//                           bot_aimEngageMs (default 1200) after the target has
//                           been continuously in sight and range, and shots
//                           paced at bot_aimPace (default 1.15) times the
//                           weapon's refire time. bot_aimStyle 2 is the
//                           competent style: the kernel's lead (Bot2_AimSolve)
//                           through the same humanized view, error, engage
//                           delay and pacing, firing a splash or lobbed weapon
//                           only when the shot is synced (the target on the
//                           floor when it arrives: BOT2_AIM_SYNC), as the old
//                           Bot2 combat held unsynced shots. bot_aimGate 2
//                           holds a splash shot instead while the runner can
//                           travel more than bot_aimReach (default 1) splash
//                           radii during its flight: the measured rule
//                           (records/2026-09-28-aim-kernel.md).
//   bot_defenderWeapons "11,8a,15" carries several weapons ("a": alt fire) and
//                           each frame the weapon not in use is chosen by the
//                           most expected damage a second: measured hit chance
//                           at its reach ratio times damage over refire time,
//                           a switch needing 50% more and no switch within
//                           DEFENDER_SWITCH_HOLD_MS of the last (WEAPON lines).
// Standing, it aims continuously; moving
//                           (chasing, changing post) its view is the movement's
//                           and it turns to aim only bot_aimFlickMs (default
//                           250) before the weapon is ready. Each damaging hit
//                           (the defender's PERS_HITS) is a DAMAGE line.
//   bot_defenderPullCarrier D  pulls the runner carrying its flag when it is
//                           between D and bot_defenderPullMax (default 700,
//                           at most the pull's 1,024) away; bot_defenderPullRate
//                           R > 0 also requires it to be receding faster than
//                           R u/s over the last 250 ms perceived (the demo study
//                           found humans pull at a median range rate of 0, so
//                           unset by default); bot_defenderComboMs M > 0 pulls
//                           only within M ms after a shot at it (shoot, then
//                           pull as soon as the refire time allows).
// Priority each frame: a lethal throw (bot_defender 1/2; default 2:
// only escape-free lethal throws), then the carrier
// pull, then a shot. Every shot is a FIRE line; the pull is a THROW line with
// reason=carrier, followed to its outcome as a lethal throw is. Parameters
// from records/2026-09-28-base-defender-study.md.
//
// Roles (a basic chase D logic and a basic base D
// logic). Two declared defenders may play at once, each with its own state:
//   Defender_v2, bot_defenderRole 1: base D. It guards its posts; after a
//     steal it fights the carrier only while the carrier is within
//     bot_defenderLeash (default 1,500) of its own stand, then goes back to
//     its posts (base players rarely cross: records/2026-09-27-position-pilot.md).
//     The leash holds only against more than one attacker: with a lone
//     attacker there is no second one to stay home for, so it chases.
//     bot_defenderRole 0 keeps the single-defender behaviour above.
//   Chaser_v2 (adddefender <team> chase): chase D. It holds its own posts
//     (bot_chaserPost, bot_chaserPosts) while its flag is home; after a steal it
//     intercepts: along the carrier's graph route to the carrier's own stand
//     (map knowledge), the first cell it can reach first (its straight-line
//     distance x 1.3 at bot_chaserSpeed, default 450, plus 0.3 s, against the
//     route distance at the carrier's perceived speed, at least 500, within
//     DEFENDER_INTERCEPT_MAX_S of the chaser) is its
//     goal, recomputed every 500 ms (INTERCEPT lines); with none, it tails.
//     Following the carrier along its own route at the same speed never closes.
//
// From a human 1v1 (records/2026-09-29-human-defense-session.md), all off
// by default:
//   bot_defenderRearm 1     a defender with no loadout weapon stocked (three
//                           shots; a respawn keeps only the spawn loadout)
//                           walks to the nearest item it can grab that gives
//                           one (BG_CanItemBeGrabbed), a drop priced at
//                           DEFENDER_ITEM_DROP_COST, never one a lethal fall
//                           was charged to; armed, it still takes a lacking
//                           loadout weapon within DEFENDER_ITEM_TOPUP; within
//                           DEFENDER_ITEM_HOME of its home (the stand; chase D's
//                           post) or DEFENDER_ITEM_NEAR of itself, unless its role is busy: base
//                           D with the runner within DEFENDER_GUARD_RANGE of the
//                           stand, chase D with its flag taken. REARM lines.
//   bot_defenderReset 1     the self-kill reset: with its own flag home and the
//                           runner dead or over DEFENDER_RESET_CLEAR from the
//                           stand, a defender farther than bot_defenderResetDist
//                           (default 1,800) from its home (the stand; chase D's
//                           post), or over 1,000 u away and missing a weapon
//                           with none of its items to grab, kills
//                           itself to respawn at base (RESET lines; at most one
//                           every DEFENDER_RESET_MS, none within 5 s of a spawn).
//   bot_defenderMines N     base D keeps a proximity mine (trip mine alt fire:
//                           arms in 2 s, fires on an enemy within 128 u, lasts
//                           30 s) on up to N of bot_defenderMineSpots "x y z;..."
//                           (the runner's approaches to the stand) while its
//                           flag is home and the runner is over
//                           DEFENDER_MINE_CLEAR from the stand: it walks onto
//                           the spot, looks down and places one (MINE lines).
//   bot_defenderPatience 1  a lethal throw the runner could still escape waits
//                           while the same throw two frames on (the runner
//                           ballistic) is lethal with fewer escapes.
//   bot_defenderWalkErrands 1  no hop searches except while chasing: the
//                           errands (posts, items, mines, ambush) are walked.
//   bot_aimEngageGraceMs G  the engage delay (bot_aimEngageMs) restarts only
//                           after the runner has been out of sight or range
//                           for G ms; a weapon not ready or a throw's hand
//                           extend no longer restarts it. 0 (default): any
//                           frame out of sight, out of range or not ready does.
//   bot_defenderFollowMs M  (default 3,000) a throw that hits lets shots skip the engage delay
//                           for M ms: carrier kills were a pull,
//                           then a shot.
//   bot_defenderThrowMin/Max  lethal throws only from this far (default 0 and
//                           1,000); outside it the defender shoots instead.
//   bot_aimViewMs V         the aim tracks the runner as seen V ms ago
//                           (default 100); decisions keep bot_defenderReactMs.
//                           V at or above the reaction time aims from that.
//   bot_defenderFlechetteMax D  no flechette alt beyond D (default 250): it
//                           is dropped for the best other weapon and not fired.
//                           0 lifts the cap.
//   bot_aimDirectRange D    within D (default 200) the aim is exact: the
//                           current runner, at its body (a splash weapon too),
//                           no view lag, error, engage delay, pacing or gate.
//   bot_defenderHearRange H the runner within H (default 1,500) is known
//                           when unseen (heard), for positioning only.
//   bot_defenderBlock 1     (default) base D stands on the runner's route to the stand
//                           (BlockGoal) while its flag is home and the runner
//                           is known within bot_defenderBlockRange (2,500);
//                           the block outranks rearming and mines.
//   bot_chaserPursue 1      (default) chase D strafes after a runner known within
//                           bot_chaserPursueRange (2,500) of the own stand
//                           while its flag is home, ahead of the grab.
//   bot_chaserPullMax D     chase D also pulls a receding carrier out to D
//                           (default 1,000; 0 turns it off).
//   bot_chaserGoal 1        (default) chase D's movement goal is where the
//                           runner is going (own stand before the grab, the
//                           runner's stand with the flag); the runner is only
//                           shot and pulled. 0: intercept, else tail it.
//   bot_chaserReturn 1      (default) chase D touches its dropped flag to
//                           return it, ahead of every other errand.
//   bot_aimGolan 1          the flechette alt is aimed by its balls' rolls
//                           (bot2_aim.c) and chosen by their chance;
//                           bot_aimGolanMin C then holds one below chance C.
//   bot_chaserAmbush 1      chase D, while its flag is home, waits on the
//                           ambush post (bot_chaserAmbushPosts, where a human
//                           threw the runner into the chasm) farthest along the
//                           runner's way to the stand that it can reach before
//                           the runner passes it, else its own post; the
//                           lethal-throw rule does the rest (AMBUSH lines).
//
// Falls and the route (records/2026-09-30-defender-falls-and-route.md):
//   Posts on the graph. At the release every post, ambush post and mine spot
//                           is snapped to the centre of its graph cell
//                           (Graph_Nearest, 128 u, 64 u tolerance; a point off
//                           the floor is first dropped onto it), or refused
//                           when none is found: POST_SNAP lines. A post with
//                           no cell was a no_path goal: Kejim's ambush posts
//                           at z 1887 were 79 u above the walkway.
//   bot_chaserPullClose M   (default 0) for M ms after chase D's carrier pull
//                           hits, its goal is the carrier's landing cell, so
//                           the exact aim inside bot_aimDirectRange can fire
//                           (CLOSE lines).
//   bot_chaserRearmChase 0 (default 1) an unarmed chase D (no loadout
//                           weapon stocked) rearms during a chase, as when
//                           idle (REARM_CHASE lines); it needs bot_defenderRearm.
//   bot_defenderKnockSafe 0 (default 1) fires a close splash shot even when
//                           its knock would carry the shooter off the floor
//                           (KnockSafe; PRE_SELF while it waits).
//   bot_defenderResetSpawnMs N (default 2000) no self-kill reset this soon
//                           after a spawn (was 5000).
//   bot_chaserRearmDetour N (default 600, 0 any) chase D takes a weapon in a
//                           chase only this far off its way (Rearm).
//   bot_defenderRearmPlan 1 (default 1; 0 is the greedy pick) Rearm's greedy
//                           nearest-item pick replaced by the soonest *usable*
//                           one (RearmPlan): present items by route time
//                           (RouteMs, the shared graph's own Graph_Path cost,
//                           not straight-line), and items seen taken by their
//                           own recorded respawn (ITEM_SEEN_TAKEN: a defender
//                           only knows a respawn when it took the item itself
//                           or had line of sight the frame it vanished; read
//                           straight off the item's own ent->nextthink, which
//                           Touch_Item, g_items.c, already schedules under the
//                           game's respawn rules - not reconstructed from
//                           them). usable(item) = max(eta, time still away);
//                           a known-respawning item is walked to and waited at
//                           rather than retargeted. The two declared defenders
//                           share item claims so they stop racing each other:
//                           a claimed item is another's only when it is more
//                           than 1,500 ms earlier. Hysteresis: a new item must
//                           beat the held one by over 1,500 ms, or the held
//                           one must have become impossible (taken unseen,
//                           outclaimed), to retarget. REARM_PLAN logs each
//                           change; 0 leaves Rearm (above) untouched.
//   bot_chaserIntercept 0   (default 1) with bot_chaserGoal 1, no intercept
//                           ahead of the carrier: the stand only.
//   bot_defenderFlagKeepOut 0 (default 1) base and chase D
//                           may walk onto the enemy flag. With 1, a movement
//                           goal within the enemy flag's pickup box (plus
//                           bot_defenderFlagKeepOutMargin, default 40) becomes
//                           the nearest graph cell outside it, and no straight
//                           item walk steps into it (FlagKeepOut); with
//                           bot_defenderFlagKeepOutRoute (default 1) the box
//                           is a forbidden volume of its plans, searches and
//                           walk, Bot2_DefenderKeepOutBox); 0
//                           none (the nine-body lattice, 2, was removed;
//                           any nonzero value is the box).
//   bot_defenderLateRelease 0 (default 1) a defender dead at the
//                           release is skipped, as before; with 1 it is
//                           cleared and released at its respawn (late=1).
//   bot_defenderClampHold 0 (default 1) an aim out of a moving defender's
//                           reach (SetView) shows the old 89 degree edge, not
//                           the movement's view.
//   bot_defenderViewLog 1   (default 0) a VIEW line for every frame whose view
//                           turns 8 degrees or more, with its writers.
//   bot_defenderCarryLog 1  (default 0) a route diagnostic: every 250 ms of a
//                           carry a CARRY_SAMPLE line (distance to the
//                           carrier, visible, in range, the shot's last
//                           PRESHOT reason, decline hold, airborne), and a
//                           CARRY summary when the carry ends.
//   bot_defenderMeleeEdge 0 (default 1) the saber walk
//                           (Melee) without the movement's guest brake test
//                           (Move_GuestEdgeAhead). On, floor ending within
//                           the stopping distance along the heading releases
//                           the keys, along the velocity pushes against it
//                           (MELEE_EDGE line per episode).
//   bot_defenderKeepSequence L  (default 2; 1 also holds the
//                           view in the air, which cost 1-1.5 captures in ten
//                           minutes) while a committed hop sequence
//                           (Move_SequenceActive) is between its run-up and
//                           its last landing, this layer leaves the
//                           movement's command alone instead of overriding it
//                           for an item/flag/mine reach or a brace, and
//                           SetView leaves the view and keys alone too
//                           (held=1, no shot). 40 of 122 missed hop landings
//                           were this layer rewriting a committed sequence's
//                           command (records/2026-10-01-errand-falls-library-
//                           test.md, "why defender hops land off their
//                           rollout", class A). L=1 guards the whole
//                           sequence; L=2 guards ground frames only, so an
//                           airborne frame may still turn the view to aim.
//                           KEEP_SEQUENCE lines log a guarded episode's length.
//
// bot_defenderShadow 1 is a route diagnostic, not a player: while the runner
// carries, every frame it asks whether a thrower standing on the carrier's
// own track 250, 500 or 1,000 ms behind it (a perfect tail at that lag, with
// line of sight) could pull or push it into the pit with no air-input escape
// (the mode-2 test). It throws nothing; it logs each window of such frames
// (SHADOW_OPEN/SHADOW_CLOSE) and a SHADOW_SUMMARY when the carry ends.
#include "g_local.h"
#include "ai_main.h"
#include "ai_bot2.h"
#include "bot2_defender.h"
#include "bot2_svcmds.h"
#include "bot2_aim.h"
#include "bot2_graph.h"
#include "bot2_move.h"
#include "bot2_movecost.h"

// Review batch-2 fixes: on by default; a cvar set to 0
// turns its fix off (records/2026-11-10-review-batch2.md).
static int FixOn(const char *name) {char v[16];trap->Cvar_VariableStringBuffer(name,v,sizeof(v));return !v[0] || atoi(v)!=0;}

// The declared bots by name, two of each role (milestone B: one per
// team for the two-sided 2v2 and the 4v4). Slot % 3 is the role: base (or
// solo), chase, camper. A slot's position cvars carry its prefix
// (bot_defender2Post, bot_chaser2Posts, ...); behaviour cvars are shared.
#define DEFENDER_MAX 6
static const char *slotNames[DEFENDER_MAX]={"Defender_v2","Chaser_v2","Camper_v2","Defender2_v2","Chaser2_v2","Camper2_v2"};
static const char *slotPrefix[DEFENDER_MAX]={"defender","chaser","camper","defender2","chaser2","camper2"};
enum {DROLE_SOLO,DROLE_BASE,DROLE_CHASE,DROLE_CAMPER};
static const char *roleNames[4]={"solo","base","chase","camper"};
#define TARGET_HOLD_MS 1000           // a chosen target is kept at least this long
#define DEFENDER_HISTORY 256         // runner frames kept: the 1,000 ms reaction cap plus 250 ms (Perceived(react+250))
                                     // down to 5 ms frames (64 covered it only at 25 ms)
#define DEFENDER_OUTCOME_MS 4000     // how long a hit throw is followed
#define DEFENDER_GOAL_REACHED 96.0f  // a strafing chaser this close to its goal takes the next sighting
#define DEFENDER_STARVED_MS 500      // STARVED lines at most this often
#define DEFENDER_POSTS 8
#define DEFENDER_GUARD_RANGE 1500.0f // a runner this close to the own stand moves the guard
#define DEFENDER_POST_REACHED 48.0f
#define DEFENDER_POST_SWITCH 96.0f   // another post must be this much better to move
#define DEFENDER_PULL_RANGE 1024.0f  // ForceThrow's radius
#define DEFENDER_SWITCH_HOLD_MS 2000 // a weapon choice is kept this long
#define DEFENDER_INTERCEPT_MAX_S 5.0f // chase D takes no intercept further than this
#define DEFENDER_ITEM_REACHED 72.0f   // closer than this to an item it steers straight onto it
#define DEFENDER_ITEM_GIVEUP_MS 8000  // an item not reached in this long is skipped
#define DEFENDER_ITEM_SKIP_MS 20000   // for this long
#define DEFENDER_ITEM_HOME 1500.0f    // items this near its home are its to grab
#define DEFENDER_ITEM_NEAR 700.0f     // and any this near where it stands
#define DEFENDER_ITEM_DROP_COST 3.0f  // units of walk each unit of drop to an item counts as
#define DEFENDER_ITEM_TOPUP 600.0f    // armed, it still takes a wanted item this near
#define DEFENDER_FALL_CHARGE_MS 12000 // a lethal fall this soon after choosing an item is charged to it
// A charged item is skipped for bot_defenderFallChargeMs (default 60,000): skipped for good, a
// chase D whose only reachable weapon had been charged stood unarmed beside the enemy carrier for eight
// minutes of a 4v4 world (records/2026-10-02-b3-stalls.md).
#define DEFENDER_RESET_MS 10000       // self-kill resets at most this often
#define DEFENDER_RESET_CLEAR 2500.0f  // the runner this far from the stand leaves time to reset
#define DEFENDER_MINE_SPOTS 6
#define DEFENDER_MINE_COVER 96.0f     // a live own proximity mine this close covers a spot
#define DEFENDER_MINE_CLEAR 1200.0f   // no mine laying with the runner this close to the stand
#define DEFENDER_MINE_GIVEUP_MS 6000
#define DEFENDER_AMBUSH_POSTS 8
#define DEFENDER_SNAP_RADIUS 128.0f   // a post's cell is looked for this far out
#define DEFENDER_SNAP_TOLERANCE 64.0f // and this far up or down
#define DEFENDER_CARRY_SAMPLE_MS 250
enum {PRE_UNSEEN,PRE_RANGE,PRE_WEAPON,PRE_BLOCKED,PRE_MOVING,PRE_REFIRE,PRE_ENGAGE,PRE_PACE,PRE_GATE,PRE_ALIGN,PRE_FIRED,PRE_SELF,PRE_OTHER,PRE_REASONS};
static const char *preNames[PRE_REASONS]={"unseen","range","weapon","blocked","moving","refire","engage","pace","gate","align","fired","self","other"};

extern int forcePowerNeeded[NUM_FORCE_POWER_LEVELS][NUM_FORCE_POWERS];
extern qboolean WP_ForcePowerUsable(gentity_t *self, forcePowers_t forcePower);

typedef struct {
    int time;
    vec3_t origin, velocity;
    qboolean visible, grounded;
} defenderSeen_t;

typedef struct {
    qboolean armed;
    int client, placedSpawn, releaseTime, role;
    defenderSeen_t seen[DEFENDER_HISTORY];
    int seenNext, seenCount;
    // The last throw, followed until its outcome is known.
    int fireTime, fireKind, fireRunner, predictedMs, outcomeDone, hit;
    // The forecast of the throw as issued, from live positions: the runner's
    // counter level and the horizontal speed it predicts (HIT compares them).
    int fireCounter; float fireModelH;
    // After a hit: the first grounded frame, and the runner's own airborne input.
    int landed, airFrames, airInputFrames, dead;
    // The chaser's goal: the carrier where it was last seen, and since when.
    int chasing, chaseSeenTime, retargets;
    vec3_t chaseGoal;
    // Shadow tails, one per lag: frames judged, passive-lethal, inescapable.
    int shadowCarrying, shadowCarryStart;
    int shadowFrames[3], shadowLethal[3], shadowTrapped[3], shadowWindows[3], shadowOpen[3], shadowOpenAt[3];
    vec3_t predictedEnd;
    int throws, hits, misses, pits, deaths, survived, refusedBaseline;
    int starved, starvedNext;
    // Guarding, shooting and the receding pull.
    vec3_t posts[DEFENDER_POSTS], stand;
    int standTeam;              // the sessionTeam stand and enemyStand were read for (Stands)
    int postCount, post, guardMoving, weapon, lastShot, shots, carrierPulls, damageHits;
    int mode, loadout[8], loadoutMode[8], loadoutCount, switches, switchTime;
    // A pressed shot, confirmed as fired when the ammo falls (a press the
    // weapon did not act on, e.g. under a throw's hand extend, is no shot).
    int pressTime, pressAmmo, pressWeaponTime, pressCarrying;float pressDistance, pressLead;
    // The shot's situation, logged on its FIRE line to learn shot selection.
    int pressSync, pressGrounded, pressLandMs, pressMode, pressCost;float pressChance, pressTof, pressSpeed, pressVz, pressAngular;
    int engageSince;            // the runner continuously in sight and range since
    int engageLast;             // the last frame it was in sight and range (bot_aimEngageGraceMs)
    int shootTime;              // the last frame Shoot ran (bot_defenderFixBA6)
    int followUntil;            // a throw hit: shots skip the engage delay until then (bot_defenderFollowMs)
    int sightState;             // the last logged SIGHT state (bot_defenderSightLog)
    // Why each frame before the pickup did not fire (PRESHOT at the chase).
    int pre[PRE_REASONS];
    // Chase D's intercept, and where the carrier was last seen (base D's leash).
    vec3_t enemyStand, lastSeen;
    int intercepting, interceptNext;
    // When lastSeen was true (a sighting's time, or the theft's), and whether
    // the own flag was on its stand last frame (the HUD's flag status).
    int knownTime, theftWasHome;
    // Rearming: the item walked to, since when, and items skipped until a time.
    int rearmItem, rearmSince, rearmSkip[MAX_GENTITIES/32], rearmSkipUntil, pickups, spawnTime, resets, resetTime;
    int fallUntil[MAX_GENTITIES], fallCharges;   // items a lethal fall was charged to, skipped until then (FallCharged)
    int patienceHolds;                              // lethal throws held for a better next frame
    // Mines: the spots, the one being laid, and a pressed placement.
    vec3_t mineSpots[DEFENDER_MINE_SPOTS];
    int mineSpotCount, mineSpot, mineSince, minePress, mineAmmo, mines, mineSkipUntil[DEFENDER_MINE_SPOTS];
    // Chase D's ambush posts and the one it holds.
    vec3_t ambush[DEFENDER_AMBUSH_POSTS];
    int ambushCount, ambushPost, ambushNext;
    // Base D's block on the runner's approach, chase D's early pursuit.
    int blocking, blockNext, pursuing;vec3_t blockGoal;
    int returning, returnSince, returns;   // chase D walking to its dropped flag
    // The movement goal latch (DefenderMove): the goal held, when it last changed, holds.
    vec3_t moveGoal;
    // The last frame's shot decision (any time, carrying or not), for CARRY.
    int lastPre, lastPreTime;
    int clampTime;   // the last frame SetView found the view out of the movement's reach
    // The frame's view writers, for bot_defenderViewLog: SetView calls and movement commands.
    int viewSets, viewMoves;
    // The last throw was a carrier pull; chase D closes on the carrier until then.
    int fireCarrier, closeUntil, closing, rearmChasing;
    // The carry diagnostic (bot_defenderCarryLog).
    int carryOn, carryStart, carryNext, carrySamples, carryVisible, carryInRange, carryHold, carryAir, carryDead;
    int carryPre[PRE_REASONS+1], carryShots, carryPulls, carryThrows, carryHits;float carryDistSum, carryDistMin;
    const char *lastWeap;   // the last PRE_WEAPON's sub-reason (Shoot)
    // Bracing against its own splash (Brace): until when,
    // the burst point, whether the knock has come, how many.
    int braceUntil, braceKnocked, braces;vec3_t braceTo;
    // bot_defenderKeepSequence: 1 while a committed sequence is being left
    // alone this frame, since when, and the frames of the episode that just
    // ended (KEEP_SEQUENCE).
    int keepActive, keepSince, keepFrames;
    // bot_defenderRearmPlan: per-item respawn memory (RearmObserve), the
    // held target and its last-logged decision, and the claim the teammate
    // reads (rpClaimItem<0: none). rpPresent/rpUnknown are bitmasks over
    // entity index, as rearmSkip is; rpRespawnAt is level.time ms, valid only
    // when the matching rpUnknown bit is clear and the item is not present.
    int rpPresent[MAX_GENTITIES/32], rpUnknown[MAX_GENTITIES/32], rpRespawnAt[MAX_GENTITIES];
    int rpItem, rpSince, rpWaiting, rpEtaMs, rpWaitMs, rpUsableMs, rpNextEval, rpDeadline;
    int rpWayEvalAt;            // level.time+1 of an evaluation bounded by a way (RearmPlan); 0 none
    int rpClaimedCheckAt;       // the top-up's verdict on a teammate-claimed item holds until then
    int rpClaimItem, rpClaimUsableMs;
    // The enemy it perceives and fights (DefenderRunner): a client number,
    // since when, and the frame it was last chosen.
    int target, targetSince, targetFrame, targetSwitches;
    // The camper's goal (CamperMove): what it is doing, since when, and counts.
    int camperGoal, camperGoalSince, takes, retakes;
    int carryingHome, carriesHome;
    int meleeOn, melees;   // the unarmed saber fallback (Melee)
    const char *meleeWhy;  // Melee's last refusal, for the carry diagnostic (CARRY_SAMPLE melee=)
    int meleeEdge;         // Melee's last edge test (bot_defenderMeleeEdge): 0, 1 hold, 2 brake
    int reachEdge;         // ReachWalk's last edge test (bot_defenderReachEdge): 0, 1 refused, 2 brake
    int returnSkip;        // the dropped flag ReturnFlag last left as unreachable (bot_defenderDropReach), else 0
    int pistols;           // switches to the pistol fallback (PistolChoose)   // holding the enemy flag, running it home (bot_defenderCarryHome)
    int campHave, campTime;vec3_t campSpot;   // the camping spot it walks to (CampSpot)
    // The enemy-flag keep-out (FlagKeepOut): the stand-off cell a goal on the
    // enemy flag becomes, when it was chosen, and counts.
    int keepOutHave, keepOutTime, keepOutActive, keepOuts;vec3_t keepOutSpot, keepOutFlag;
    int inFlagBox, flagBoxEntries;   // the origin inside the keep-out box last frame; entries ([FLAGBOX] lines)
    int pendingRelease;    // dead at its release: armed at its post when it respawns
} defenderState_t;

// Each declared defender's state; `defender` is the one being run now.
static defenderState_t defenders[DEFENDER_MAX];
static defenderState_t *defenderCur=&defenders[0];
#define defender (*defenderCur)
static int FallCharged(int item) {return level.time<defender.fallUntil[item];}

// The cvar for this slot's position: bot_<prefix><suffix> (bot_defenderPost,
// bot_chaser2Posts, bot_defenderMineSpots, ...).
static float CvarFloat(const char *name,float fallback);   // a cvar's value, or fallback when unset

static const char *RoleCvar(const char *suffix) {
    return va("bot_%s%s",slotPrefix[defenderCur-defenders],suffix);
}

// The declared bot's slot (slotNames), -1 not one.
static int DefenderSlot(int clientNum) {
    char name[MAX_NETNAME];int s;
    if(trap->Cvar_VariableIntegerValue("bot_movementOnly")!=2)return -1;
    if(clientNum<0 || clientNum>=MAX_CLIENTS || !g_entities[clientNum].inuse || !g_entities[clientNum].client ||
       !(g_entities[clientNum].r.svFlags&SVF_BOT))return -1;
    {
        // The slot is a function of the name alone: kept per client by the raw netname, so the clean and the
        // compares run once per name, not on every call (the movement asks per simulated step through MoveGuest;
        // records/2026-10-03-perf-lossless.md).
        static char seen[MAX_CLIENTS][MAX_NETNAME];static int slot[MAX_CLIENTS];
        const char *raw=g_entities[clientNum].client->pers.netname;
        if(seen[clientNum][0] && !strcmp(seen[clientNum],raw))return slot[clientNum];
        Q_strncpyz(name,raw,sizeof(name));Q_CleanStr(name);
        slot[clientNum]=-1;
        for(s=0;s<DEFENDER_MAX;s++)if(!Q_stricmp(name,slotNames[s])) {slot[clientNum]=s;break;}
        Q_strncpyz(seen[clientNum],raw,sizeof(seen[clientNum]));
        return slot[clientNum];
    }
}

// The teammate whose rearm claims a defender reads: the other armed base or
// chase D on its team; the camper (an offense) has none.
static const defenderState_t *Mate(int slot) {
    static const defenderState_t none;int s;
    if(slot<0 || slot%3==2 || !defenders[slot].armed)return &none;
    for(s=0;s<DEFENDER_MAX;s++) {
        const defenderState_t *d=&defenders[s];
        if(s==slot || s%3==2 || !d->armed || !g_entities[d->client].client || !g_entities[defenders[slot].client].client)continue;
        if(DefenderSlot(d->client)!=s)continue;   // its client number now holds another player
        if(g_entities[d->client].client->sess.sessionTeam==g_entities[defenders[slot].client].client->sess.sessionTeam)return d;
    }
    return &none;
}

qboolean Bot2_DefenderIs(int clientNum) {
    return DefenderSlot(clientNum)>=0;
}

// A defender on an errand (post, item, mine spot, ambush) walks its planned
// path: the hop searches near the Kejim walkway put both defenders into the
// chasm 3-12 times in ten laps minutes once they moved about that much, and
// every goal change restarts the search (Move_Reset clears its avoid list).
qboolean Bot2_DefenderHops(int clientNum) {
    int slot=DefenderSlot(clientNum);
    if(slot<0)return qfalse;
    // The slot's state is this client's only while it is the one armed there.
    if(!defenders[slot].armed || defenders[slot].client!=clientNum)return !trap->Cvar_VariableIntegerValue("bot_defenderWalkErrands");
    return !trap->Cvar_VariableIntegerValue("bot_defenderWalkErrands") || defenders[slot].chasing || defenders[slot].blocking ||
           defenders[slot].returning;
}

// bot_defenderKeepSequence (default 2): see the cvar block above. Level
// (the cvar value) 0 leaves this off; 1 or 2 is SequenceGuard's threshold.
static int KeepSequenceLevel(void) {return (int)CvarFloat("bot_defenderKeepSequence",2);}

// Whether a defender's own layer must leave the movement's command alone this
// frame: a committed hop sequence is between its run-up and its last landing,
// and the cvar asks for it. Covers the item/flag/mine "reached" straight
// walks and the splash brace.
static int SequenceGuard(int clientNum) {
    return KeepSequenceLevel()>0 && Move_SequenceActive(clientNum);
}

// SetView's guard: level 1 covers the whole sequence (run-up and flight), as
// SequenceGuard does; level 2 covers ground frames only, so an airborne frame
// may still turn the view to aim (Pmove's backwards-speed penalty that
// SetView's clamp exists for is a ground-only rule).
static int ViewGuard(int clientNum) {
    int keep=KeepSequenceLevel();
    if(keep<=0 || !Move_SequenceActive(clientNum))return 0;
    return keep==1 || Move_GroundedThisFrame(clientNum);
}

static qboolean Visible(gentity_t *self,gentity_t *runner);

// An enemy this one may fight: any connected client on the other team, bot
// (the runner, the declared defenders and camper, legacy Bot2 opponents) or
// human. Bots alone before: a human was fought only
// when no enemy bot existed, so a camper teammate hid a human on offense.
static qboolean TargetCandidate(gentity_t *self,int i) {
    gentity_t *ent=&g_entities[i];
    return ent->inuse && ent->client && ent->client->pers.connected==CON_CONNECTED &&
       ent->client->sess.sessionTeam!=self->client->sess.sessionTeam &&
       (ent->client->sess.sessionTeam==TEAM_RED || ent->client->sess.sessionTeam==TEAM_BLUE);
}

// The enemy it perceives, shoots and throws at, chosen once a frame among the
// candidates: the one carrying its own flag first; else the current one for
// at least TARGET_HOLD_MS, and while a throw at it is being followed; else
// the visible one nearest its own team's carrier (the camper guarding its
// runner), or nearest itself. A lone candidate (the runner, before the camper
// existed) is always the target, as before. A new target restarts the
// perception history, so it is reacted to as a new sighting.
static gentity_t *ChooseTarget(gentity_t *self) {
    int i,n=0,first=-1,carrier=-1,best=-1,cur=defender.target,ownFlag,enemyFlag;
    float bestD=0;const float *anchor=self->client->ps.origin;const char *why=NULL;
    ownFlag=self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG;
    enemyFlag=ownFlag==PW_REDFLAG?PW_BLUEFLAG:PW_REDFLAG;
    for(i=0;i<level.maxclients;i++) {
        gentity_t *ent=&g_entities[i];
        if(!ent->inuse || !ent->client || ent->client->pers.connected!=CON_CONNECTED || ent==self)continue;
        if(ent->client->sess.sessionTeam==self->client->sess.sessionTeam && ent->health>0 && ent->client->ps.powerups[enemyFlag])
            anchor=ent->client->ps.origin;
    }
    for(i=0;i<level.maxclients;i++) {
        if(!TargetCandidate(self,i))continue;
        n++;if(first<0)first=i;
        // The carrier of its own flag comes first, for chase D wherever it is (its
        // job); for the others only when perceived (seen, or within hearing):
        // base D near its stand ignored a visible chase D for an unseen carrier
        // across the map (the enemy base should be killing
        // the chase; records/2026-10-02-b3-stalls.md).
        if(carrier<0 && g_entities[i].client->ps.powerups[ownFlag] && (defender.role==DROLE_CHASE ||
           Visible(self,&g_entities[i]) || Distance(self->client->ps.origin,g_entities[i].client->ps.origin)<CvarFloat("bot_defenderHearRange",1500)))
            carrier=i;
    }
    if(!n)return NULL;
    if(cur<0 || cur>=level.maxclients || !TargetCandidate(self,cur))cur=-1;
    if(n==1)best=first,why="only";
    else if(carrier>=0)best=carrier,why="carrier";
    else if(cur>=0 && g_entities[cur].health>0 &&
            (level.time-defender.targetSince<TARGET_HOLD_MS || (defender.fireTime>=0 && !defender.outcomeDone && defender.fireRunner==cur)))best=cur;
    else {
        for(i=0;i<level.maxclients;i++) {
            gentity_t *ent=&g_entities[i];float d;
            if(!TargetCandidate(self,i) || ent->health<=0 || !Visible(self,ent))continue;
            d=Distance(anchor,ent->client->ps.origin);
            if(best<0 || d<bestD)best=i,bestD=d;
        }
        why="nearest";
        if(best<0) {
            best=cur>=0 && g_entities[cur].health>0?cur:-1;why="kept";
            for(i=0;best<0 && i<level.maxclients;i++)if(TargetCandidate(self,i) && g_entities[i].health>0)best=i;
            if(best<0)best=cur>=0?cur:first;
        }
    }
    if(best!=defender.target) {
        if(n>1 || defender.target>=0)
            trap->Print("[DEFENDER] TARGET level=%d role=%s from=%d to=%d reason=%s candidates=%d switches=%d\n",level.time,
                roleNames[defender.role],defender.target,best,why?why:"kept",n,defender.targetSwitches+1);
        if(defender.target>=0) {defender.seenCount=0;defender.seenNext=0;defender.engageSince=0;}
        defender.target=best;defender.targetSince=level.time;defender.targetSwitches++;
    }
    return &g_entities[best];
}

static gentity_t *DefenderRunner(gentity_t *self) {
    int i;
    if(defender.armed && defender.client==self->s.number) {
        gentity_t *t;
        if(defender.targetFrame==level.time && defender.target>=0 && TargetCandidate(self,defender.target))return &g_entities[defender.target];
        if((t=ChooseTarget(self))!=NULL) {defender.targetFrame=level.time;return t;}
    } else {
        for(i=0;i<level.maxclients;i++) {
            gentity_t *ent=&g_entities[i];
            if(ent->inuse && ent->client && ent->client->pers.connected==CON_CONNECTED && Bot2_IsMovementOnly(i) &&
               ent->client->sess.sessionTeam!=self->client->sess.sessionTeam)return ent;
        }
    }
    // With no runner bot in the game, a human on the other team is the runner
    // (a human playing offense, tools/play_vs_runner.py --offense).
    for(i=0;i<level.maxclients;i++) {
        gentity_t *ent=&g_entities[i];
        if(ent->inuse && ent->client && ent->client->pers.connected==CON_CONNECTED && !(ent->r.svFlags&SVF_BOT) &&
           (ent->client->sess.sessionTeam==TEAM_RED || ent->client->sess.sessionTeam==TEAM_BLUE) &&
           ent->client->sess.sessionTeam!=self->client->sess.sessionTeam)return ent;
    }
    return NULL;
}

// One "key=value" of bot_defenderPostState / bot_chaserPostState into `out`
// (empty when the key is absent).
static void PostStateValue(const char *key,char *out,int size) {
    char text[256],*c;int n=(int)strlen(key);
    out[0]=0;
    trap->Cvar_VariableStringBuffer(RoleCvar("PostState"),text,sizeof(text));
    for(c=text;*c;) {
        while(*c==' ')c++;
        if(!Q_strncmp(c,key,n) && c[n]=='=') {
            int k=0;c+=n+1;
            while(*c && *c!=' ' && k<size-1)out[k++]=*c++;
            out[k]=0;return;
        }
        while(*c && *c!=' ')c++;
    }
}

// bot_defenderPost "x y z yaw": dropped to the floor below, refused if solid.
// A post state with air=1 (a fixture scenario's defender in flight) keeps
// the declared height instead.
static qboolean DefenderPlace(gentity_t *ent) {
    char text[128];float values[4]={0,0,0,0};vec3_t origin,top,bottom,angles={0,0,0};trace_t down,stand;
    playerState_t *ps=&ent->client->ps;int air;
    trap->Cvar_VariableStringBuffer(RoleCvar("Post"),text,sizeof(text));
    if(sscanf(text,"%f %f %f %f",&values[0],&values[1],&values[2],&values[3])<3) {
        trap->Print("[DEFENDER] REFUSED %s \"x y z [yaw]\" required\n",RoleCvar("Post"));return qfalse;
    }
    PostStateValue("air",text,sizeof(text));air=atoi(text);
    VectorSet(top,values[0],values[1],values[2]+32);VectorSet(bottom,values[0],values[1],values[2]-256);
    trap->Trace(&down,top,ent->r.mins,ent->r.maxs,bottom,ent->s.number,MASK_PLAYERSOLID,qfalse,0,0);
    if(!air && (down.startsolid || down.allsolid || down.fraction>=1 || down.plane.normal[2]<.7f)) {
        trap->Print("[DEFENDER] REFUSED post x=%.1f y=%.1f z=%.1f floor_fraction=%.3f normal=%.3f solid=%d\n",
            values[0],values[1],values[2],down.fraction,down.plane.normal[2],down.startsolid||down.allsolid);return qfalse;
    }
    if(air)VectorCopy(values,origin);
    else VectorCopy(down.endpos,origin);
    trap->Trace(&stand,origin,ent->r.mins,ent->r.maxs,origin,ent->s.number,MASK_PLAYERSOLID,qfalse,0,0);
    if(stand.startsolid || stand.allsolid) {trap->Print("[DEFENDER] REFUSED post occupied\n");return qfalse;}
    trap->UnlinkEntity((sharedEntity_t*)ent);
    VectorCopy(origin,ps->origin);VectorClear(ps->velocity);ps->groundEntityNum=air?ENTITYNUM_NONE:down.entityNum;
    // bot_defenderPostVelocity / bot_chaserPostVelocity "vx vy vz" (fixture
    // scenarios, unset: at rest): the defender starts moving, as it was a
    // moment before a recorded fall (tools/defense_laps.py --scenario).
    trap->Cvar_VariableStringBuffer(RoleCvar("PostVelocity"),text,sizeof(text));
    if(text[0] && sscanf(text,"%f %f %f",&ps->velocity[0],&ps->velocity[1],&ps->velocity[2])!=3)VectorClear(ps->velocity);
    ps->pm_flags&=~(PMF_JUMP_HELD|PMF_TIME_KNOCKBACK);ps->pm_time=0;
    // A thrower only: a drawn blade would add contact damage the fixture does not declare.
    ps->saberHolstered=2;
    angles[YAW]=AngleNormalize180(values[3]);SetClientViewAngle(ent,angles);
    VectorCopy(origin,ent->r.currentOrigin);trap->LinkEntity((sharedEntity_t*)ent);
    defender.placedSpawn=ps->persistant[PERS_SPAWN_COUNT];
    trap->Print("[DEFENDER] POST level=%d client=%d team=%d x=%.3f y=%.3f z=%.3f yaw=%.1f push=%d pull=%d speed=%d force=%d mode=%d react_ms=%d\n",
        level.time,ent->s.number,ent->client->sess.sessionTeam,origin[0],origin[1],origin[2],values[3],
        ps->fd.forcePowerLevel[FP_PUSH],ps->fd.forcePowerLevel[FP_PULL],ps->fd.forcePowerLevel[FP_SPEED],ps->fd.forcePower,
        (int)CvarFloat("bot_defender",2),trap->Cvar_VariableIntegerValue("bot_defenderReactMs"));
    return qtrue;
}

// "x y z;x y z;..." from a cvar.
static int ParsePoints(const char *cvar,vec3_t *out,int max) {
    char text[512],*cursor;int n=0;
    trap->Cvar_VariableStringBuffer(cvar,text,sizeof(text));
    for(cursor=text;*cursor && n<max;) {
        if(sscanf(cursor,"%f %f %f",&out[n][0],&out[n][1],&out[n][2])==3)n++;
        while(*cursor && *cursor!=';')cursor++;
        if(*cursor==';')cursor++;
    }
    return n;
}

// Snaps each point to the centre of its graph cell, dropping a point off the
// floor onto the floor below first; refuses (removes) a point with no cell.
// A goal with no cell is a no_path plan every replan (to=-1), and a no_path
// hold taken at speed was most of the defenders' falls.
static void SnapPoints(gentity_t *ent,const char *kind,vec3_t *pts,int *count) {
    int i,k;
    if(Graph_CellCount()<=0)return;
    for(i=0;i<*count;) {
        float *p=pts[i];int cell=Graph_Nearest(p,DEFENDER_SNAP_RADIUS,DEFENDER_SNAP_TOLERANCE);float dropped=0;
        if(cell<0) {
            vec3_t top,bottom;trace_t down;
            VectorSet(top,p[0],p[1],p[2]+32);VectorSet(bottom,p[0],p[1],p[2]-256);
            trap->Trace(&down,top,ent->r.mins,ent->r.maxs,bottom,ent->s.number,MASK_PLAYERSOLID,qfalse,0,0);
            if(!down.startsolid && !down.allsolid && down.fraction<1 && down.plane.normal[2]>=.7f) {
                dropped=p[2]-down.endpos[2];
                cell=Graph_Nearest(down.endpos,DEFENDER_SNAP_RADIUS,DEFENDER_SNAP_TOLERANCE);
            }
        }
        if(cell<0) {
            trap->Print("[DEFENDER] POST_SNAP level=%d role=%s kind=%s index=%d from=%.1f,%.1f,%.1f refused=1\n",level.time,roleNames[defender.role],
                kind,i,p[0],p[1],p[2]);
            for(k=i;k+1<*count;k++)VectorCopy(pts[k+1],pts[k]);
            (*count)--;continue;
        }
        {
            const graphCell_t *c=Graph_Cell(cell);vec3_t to;
            VectorSet(to,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);
            trap->Print("[DEFENDER] POST_SNAP level=%d role=%s kind=%s index=%d from=%.1f,%.1f,%.1f to=%.1f,%.1f,%.1f cell=%d distance=%.1f dropped=%.1f refused=0\n",
                level.time,roleNames[defender.role],kind,i,p[0],p[1],p[2],to[0],to[1],to[2],cell,Distance(p,to),dropped);
            VectorCopy(to,p);
        }
        i++;
    }
}

// bot_defenderFlagKeepOut (default 1: a base D should not
// take the enemy flag except in an emergency, and for a chase holding it
// correlates with losing, so the defenders take it not at all for now;
// records/2026-10-02-chase-enemy-flag.md). With bot_chaserGoal 1 chase D's
// goal with the flag gone is the runner's own stand, or the intercept's last
// cell, which is the stand's: it walked onto the enemy flag 8 times in the 8
// ce laps worlds. A flag is taken from within its pickup box
// (BG_PlayerTouchesItem: x -50..44, y and z +-36 from the flag), so a goal in
// that box plus bot_defenderFlagKeepOutMargin (default 40) becomes the graph
// cell outside it nearest the goal (a little toward the defender), on the
// flag's level; the defender stands at the stand's edge, in the carrier's way,
// instead of on the flag. The camper's take is its job: it is exempt.
static gentity_t *EnemyFlag(gentity_t *self,qboolean dropped);
void G_Kill(gentity_t *ent);   // g_cmds.c: the /kill self-kill (MOD_SUICIDE; a carried flag returns to its stand)
static qboolean InFlagBox(const float *p,const float *flag,float margin) {
    return p[0]-flag[0]<44+margin && p[0]-flag[0]>-50-margin && fabsf(p[1]-flag[1])<36+margin && fabsf(p[2]-flag[2])<36+margin;
}
// The enemy flag a defender must keep out of, or NULL: a dropped one, else
// its stand whether the flag is on it or not. While the flag is carried it is
// neither home nor dropped, and a chase D with bot_chaserGoal 1 walked onto the
// empty stand; the flag returned under it and it was taken (the
// 4v4's pickups, records/2026-10-02-b3-stalls.md).
static const float *KeepOutFlag(gentity_t *self) {
    gentity_t *flag=NULL;
    if(defender.role==DROLE_CAMPER || !CvarFloat("bot_defenderFlagKeepOut",1))return NULL;
    if((flag=EnemyFlag(self,qtrue))!=NULL)return flag->r.currentOrigin;
    flag=Bot2_BaseFlag(self->client->sess.sessionTeam==TEAM_RED?TEAM_BLUE:TEAM_RED);
    return flag?(VectorLength(flag->s.origin)>0?flag->s.origin:flag->r.currentOrigin):NULL;
}
// 0 when a straight walk from the defender to `to` would cross the keep-out box.
static qboolean StraightOk(gentity_t *self,const float *to) {
    const float *f=KeepOutFlag(self);float margin=CvarFloat("bot_defenderFlagKeepOutMargin",40);int i;
    if(!f)return qtrue;
    for(i=0;i<=8;i++) {
        vec3_t p;VectorMA(self->client->ps.origin,i/8.0f,to,p);
        VectorMA(p,-i/8.0f,self->client->ps.origin,p);
        if(InFlagBox(p,f,margin))return qfalse;
    }
    return qtrue;
}
// 1 when `goal` is inside the keep-out box; `out` is then the cell to use instead.
static int FlagKeepOut(gentity_t *self,const float *goal,vec3_t out) {
    const float *f=KeepOutFlag(self);float margin=CvarFloat("bot_defenderFlagKeepOutMargin",40),best=0,levelZ;int i,n,have=0;
    if(!f || !InFlagBox(goal,f,margin)) {defender.keepOutActive=0;return 0;}
    if(defender.keepOutHave && level.time-defender.keepOutTime<1000 && Distance(f,defender.keepOutFlag)<8) {
        VectorCopy(defender.keepOutSpot,out);return 1;
    }
    n=Graph_CellCount();levelZ=CvarFloat("bot_defenderFlagKeepOutLevel",64);
    for(i=0;i<n;i++) {
        const graphCell_t *c=Graph_Cell(i);vec3_t p;float score;
        if(c->flags || c->mover)continue;
        VectorSet(p,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);
        if(fabsf(p[2]-f[2])>levelZ || Distance(p,f)>400 || InFlagBox(p,f,margin))continue;
        score=Distance(p,goal)+.25f*Distance(p,self->client->ps.origin);
        if(c->link[0]<0 || c->link[1]<0 || c->link[2]<0 || c->link[3]<0)score+=64;   // prefer interior cells
        if(!have || score<best) {best=score;VectorCopy(p,out);have=1;}
    }
    if(!have) {defender.keepOutActive=0;return 0;}
    VectorCopy(out,defender.keepOutSpot);VectorCopy(f,defender.keepOutFlag);defender.keepOutTime=level.time;defender.keepOutHave=1;
    if(!defender.keepOutActive) {
        defender.keepOutActive=1;defender.keepOuts++;
        trap->Print("[DEFENDER] FLAG_KEEPOUT level=%d role=%s goal=%.0f,%.0f,%.0f flag=%.0f,%.0f,%.0f spot=%.0f,%.0f,%.0f self=%.0f,%.0f,%.0f keepouts=%d\n",
            level.time,roleNames[defender.role],goal[0],goal[1],goal[2],f[0],f[1],f[2],out[0],out[1],out[2],
            self->client->ps.origin[0],self->client->ps.origin[1],self->client->ps.origin[2],defender.keepOuts);
    }
    return 1;
}

// The same keep-out for the routes: a goal outside the box can
// still be reached over it, as chase D walked off the runner's stand after a
// capture, across the flag, toward its post (b1-merge record: 9 pickups in 24
// worlds). bot_defenderFlagKeepOutRoute (default 1): 1 the box is a forbidden
// volume of the defender's plans and searches (records/2026-10-09-flag-avoid.md: Graph_ForbidSet, Bot2_ForecastForbidSet and
// the walk's target, bot2_move.c ForbidSync); 0 none. The old nine-body
// lattice (2) was removed; any nonzero value is the box.
// Returns 1 and the flag's origin and box (hull origins: the pickup box plus the margin) when this
// client is a declared defender that keeps out of it, else 0; *mode the cvar.
int Bot2_DefenderKeepOutBox(int clientNum,vec3_t at,vec3_t mins,vec3_t maxs,int *mode) {
    defenderState_t *was=defenderCur;const float *f=NULL;int slot=DefenderSlot(clientNum);float margin;
    if(mode)*mode=(int)CvarFloat("bot_defenderFlagKeepOutRoute",1);
    if(slot<0 || !defenders[slot].armed || defenders[slot].client!=clientNum)return 0;
    defenderCur=&defenders[slot];
    f=KeepOutFlag(&g_entities[clientNum]);
    defenderCur=was;
    if(!f)return 0;
    margin=CvarFloat("bot_defenderFlagKeepOutMargin",40);
    VectorCopy(f,at);
    VectorSet(mins,f[0]-50-margin,f[1]-36-margin,f[2]-36-margin);
    VectorSet(maxs,f[0]+44+margin,f[1]+36+margin,f[2]+36+margin);
    return 1;
}

// Diagnostic (both route modes): a base or chase D's origin entering the
// keep-out box prints one [FLAGBOX] line (records/2026-10-09-flag-avoid.md).
// flag= the enemy flag's state: home on its stand, dropped, or away (carried;
// the box is then the empty stand's).
static void FlagBoxWatch(gentity_t *self) {
    const float *f=KeepOutFlag(self);int in;
    in=f && self->health>0 && InFlagBox(self->client->ps.origin,f,CvarFloat("bot_defenderFlagKeepOutMargin",40));
    if(in && !defender.inFlagBox) {
        defender.flagBoxEntries++;
        trap->Print("[FLAGBOX] enter level=%d client=%d role=%s flag=%s goal_in=%d mode=%d self=%.0f,%.0f,%.0f flag_at=%.0f,%.0f,%.0f entries=%d\n",
            level.time,self->s.number,roleNames[defender.role],EnemyFlag(self,qtrue)?"dropped":EnemyFlag(self,qfalse)?"home":"away",
            InFlagBox(defender.moveGoal,f,CvarFloat("bot_defenderFlagKeepOutMargin",40)),(int)CvarFloat("bot_defenderFlagKeepOutRoute",1),
            self->client->ps.origin[0],self->client->ps.origin[1],self->client->ps.origin[2],f[0],f[1],f[2],defender.flagBoxEntries);
    }
    defender.inFlagBox=in;
}

// Every movement goal of a defender goes through here; the goal is kept for
// the carry diagnostic (CARRY_SAMPLE goal=).
static void DefenderMove(int clientNum,gentity_t *self,usercmd_t *cmd,const float *goal) {
    vec3_t spot;
    if(FlagKeepOut(self,goal,spot))goal=spot;
    VectorCopy(goal,defender.moveGoal);defender.viewMoves++;
    Bot2_ExecuteMovement(clientNum,cmd,(float *)goal);
    Move_GuestRawCommand(clientNum,cmd);
}

// The own and the enemy flag stands, for the team it is on now: read at the
// release and again after a team change (forceteam).
static void Stands(gentity_t *ent) {
    gentity_t *flag;playerState_t *ps=&ent->client->ps;
    defender.standTeam=ent->client->sess.sessionTeam;
    flag=G_Find(NULL,FOFS(classname),ent->client->sess.sessionTeam==TEAM_RED?"team_CTF_redflag":"team_CTF_blueflag");
    if(flag)VectorCopy(VectorLength(flag->s.origin)>0?flag->s.origin:flag->r.currentOrigin,defender.stand);
    else VectorCopy(ps->origin,defender.stand);
    // The carrier's home: the runner's own flag stand (map knowledge).
    flag=G_Find(NULL,FOFS(classname),ent->client->sess.sessionTeam==TEAM_RED?"team_CTF_blueflag":"team_CTF_redflag");
    if(flag)VectorCopy(VectorLength(flag->s.origin)>0?flag->s.origin:flag->r.currentOrigin,defender.enemyStand);
}

// The posts, the own flag stand and the weapon, at the release.
static void GuardSetup(gentity_t *ent) {
    char text[512],*cursor;playerState_t *ps=&ent->client->ps;int w;
    defender.postCount=0;defender.post=0;defender.lastShot=-100000;
    trap->Cvar_VariableStringBuffer(RoleCvar("Posts"),text,sizeof(text));
    for(cursor=text;*cursor && defender.postCount<DEFENDER_POSTS;) {
        float *p=defender.posts[defender.postCount];
        if(sscanf(cursor,"%f %f %f",&p[0],&p[1],&p[2])==3)defender.postCount++;
        while(*cursor && *cursor!=';')cursor++;
        if(*cursor==';')cursor++;
    }
    if(!defender.postCount) {VectorCopy(ps->origin,defender.posts[0]);defender.postCount=1;}
    Stands(ent);
    w=trap->Cvar_VariableIntegerValue("bot_defenderWeapon");
    defender.weapon=w>WP_SABER && w<WP_NUM_WEAPONS?w:0;
    defender.mode=trap->Cvar_VariableIntegerValue("bot_defenderAlt")?BOT2_AIM_ALT:BOT2_AIM_PRIMARY;
    defender.loadoutCount=0;
    {
        char list[64],*c;
        trap->Cvar_VariableStringBuffer("bot_defenderWeapons",list,sizeof(list));
        for(c=list;*c && defender.loadoutCount<8;) {
            int n;
            while(*c==' ' || *c=='\t')c++;   // "11, 8a": the alt flag follows the digits, not a space
            n=atoi(c);
            while(*c>='0' && *c<='9')c++;
            if(n>WP_SABER && n<WP_NUM_WEAPONS) {
                defender.loadout[defender.loadoutCount]=n;defender.loadoutMode[defender.loadoutCount]=*c=='a'?BOT2_AIM_ALT:BOT2_AIM_PRIMARY;
                defender.loadoutCount++;
            }
            while(*c && *c!=',')c++;
            if(*c==',')c++;
        }
        if(defender.loadoutCount) {defender.weapon=defender.loadout[0];defender.mode=defender.loadoutMode[0];}
    }
    for(w=0;w<defender.loadoutCount || (w==0 && defender.weapon);w++) {
        int give=defender.loadoutCount?defender.loadout[w]:defender.weapon;
        ps->stats[STAT_WEAPONS]|=1<<give;
        ps->ammo[weaponData[give].ammoIndex]=ammoData[weaponData[give].ammoIndex].max;
    }
    defender.damageHits=ps->persistant[PERS_HITS];
    defender.mineSpotCount=ParsePoints(RoleCvar("MineSpots"),defender.mineSpots,DEFENDER_MINE_SPOTS);
    if(defender.mineSpotCount && trap->Cvar_VariableIntegerValue("bot_defenderMines")>0 && defender.role!=DROLE_CHASE && defender.role!=DROLE_CAMPER) {
        // Declared fixture setup, as the loadout: the mines it starts with; after a death it picks them up.
        ps->stats[STAT_WEAPONS]|=1<<WP_TRIP_MINE;
        ps->ammo[weaponData[WP_TRIP_MINE].ammoIndex]=ammoData[weaponData[WP_TRIP_MINE].ammoIndex].max;
    }
    defender.ambushCount=ParsePoints(RoleCvar("AmbushPosts"),defender.ambush,DEFENDER_AMBUSH_POSTS);
    SnapPoints(ent,"post",defender.posts,&defender.postCount);
    if(!defender.postCount) {VectorCopy(ps->origin,defender.posts[0]);defender.postCount=1;}
    SnapPoints(ent,"ambush",defender.ambush,&defender.ambushCount);
    SnapPoints(ent,"mine",defender.mineSpots,&defender.mineSpotCount);
    defender.ambushPost=-1;defender.rearmItem=-1;defender.mineSpot=-1;
    defender.rpItem=-1;defender.rpClaimItem=-1;
    defender.spawnTime=level.time;defender.resetTime=-DEFENDER_RESET_MS;
    trap->Print("[DEFENDER] GUARD level=%d role=%s client=%d posts=%d stand=%.1f,%.1f,%.1f weapon=%d guard=%d pull_carrier=%d combo_ms=%d speed=%d\n",
        level.time,roleNames[defender.role],ent->s.number,defender.postCount,defender.stand[0],defender.stand[1],defender.stand[2],defender.weapon,
        trap->Cvar_VariableIntegerValue("bot_defenderGuard"),trap->Cvar_VariableIntegerValue("bot_defenderPullCarrier"),
        trap->Cvar_VariableIntegerValue("bot_defenderComboMs"),ps->fd.forcePowerLevel[FP_SPEED]);
}

// bot_defenderPostState / bot_chaserPostState "health=H force=F speed_ms=S
// weapon=W[a] air=0|1 knock=0|1" (fixture scenarios, unset: the spawn
// state; knock=1 arms the movement's knock recovery at release): the
// defender as it was a moment before a recorded fall, after the loadout is
// given. The weapon is put in hand ready and held as a fresh choice; one not
// in the loadout is refused and the loadout's first stays.
static void PostState(gentity_t *ent) {
    playerState_t *ps=&ent->client->ps;char text[32];int v,k;
    PostStateValue("health",text,sizeof(text));
    if(text[0] && (v=atoi(text))>0 && v<=200)ent->health=ps->stats[STAT_HEALTH]=v;
    PostStateValue("force",text,sizeof(text));
    if(text[0] && (v=atoi(text))>=0 && v<=100)ps->fd.forcePower=v;
    PostStateValue("speed_ms",text,sizeof(text));
    if(text[0] && (v=atoi(text))>0 && v<=60000) {
        ps->fd.forcePowersActive|=1<<FP_SPEED;ps->fd.forcePowerDuration[FP_SPEED]=level.time+v;
    }
    PostStateValue("weapon",text,sizeof(text));
    if(text[0]) {
        int mode=strchr(text,'a')?BOT2_AIM_ALT:BOT2_AIM_PRIMARY;
        v=atoi(text);
        for(k=0;k<defender.loadoutCount;k++)if(defender.loadout[k]==v && defender.loadoutMode[k]==mode)break;
        if(k<defender.loadoutCount && (ps->stats[STAT_WEAPONS]&(1<<v))) {
            defender.weapon=v;defender.mode=mode;defender.switchTime=level.time;
            ps->weapon=v;ps->weaponstate=WEAPON_READY;ps->weaponTime=0;
        } else trap->Print("[DEFENDER] REFUSED post weapon=%s not in the loadout\n",text);
    }
    trap->Cvar_VariableStringBuffer(RoleCvar("PostState"),text,sizeof(text));
    if(text[0])trap->Print("[DEFENDER] POST_STATE level=%d role=%s health=%d force=%d speed=%d weapon=%d%s ground=%d\n",level.time,
        roleNames[defender.role],ent->health,ps->fd.forcePower,!!(ps->fd.forcePowersActive&(1<<FP_SPEED)),defender.weapon,
        defender.mode==BOT2_AIM_ALT?"a":"",ps->groundEntityNum);
}

// The release of one defender (defenderCur is its slot): role, post, Force,
// loadout, then armed. `late` is a defender that was dead at its world's
// release and is armed at its respawn.
static void ReleaseOne(gentity_t *ent,int i,int slot,int late,int releasedAt) {
    memset(&defender,0,sizeof(defender));
    defender.role=slot%3==2?DROLE_CAMPER:slot%3==1?DROLE_CHASE:trap->Cvar_VariableIntegerValue("bot_defenderRole")==1?DROLE_BASE:DROLE_SOLO;
    defender.client=i;defender.releaseTime=releasedAt;defender.fireTime=-1;defender.target=-1;defender.camperGoal=-1;
    if(!DefenderPlace(ent))return;
    ent->client->ps.fd.forcePower=100;
    if(trap->Cvar_VariableIntegerValue("bot_defenderSpeed")) {
        ent->client->ps.fd.forcePowersKnown|=1<<FP_SPEED;
        ent->client->ps.fd.forcePowerLevel[FP_SPEED]=FORCE_LEVEL_3;
    }
    GuardSetup(ent);
    PostState(ent);
    Bot2_AimHumanReset(i,ent->client->ps.viewangles);
    Bot2_ClearState(i);   // a chaser's movement starts from the post
    {
        char knock[8];
        PostStateValue("knock",knock,sizeof(knock));
        if(atoi(knock)==1)Move_DeclareKnock(i);
    }
    defender.armed=qtrue;
    if(late)trap->Print("[DEFENDER] RELEASE level=%d client=%d role=%s late=1 released=%d\n",level.time,i,roleNames[defender.role],releasedAt);
    else trap->Print("[DEFENDER] RELEASE level=%d client=%d role=%s\n",level.time,i,roleNames[defender.role]);
}

// bot_defenderLateRelease (default 1, records/2026-10-09-flag-avoid.md): a defender dead at
// the release (every fixture releases once a world) has its slot cleared, so nothing of an earlier release leaks,
// and is released at its post when it respawns, with a RELEASE line marked late=1. 0: as before, skipped (never
// armed in that world, or still armed with the earlier release's state).
void Bot2_DefenderRelease(void) {
    int i,late=(int)CvarFloat("bot_defenderLateRelease",1);
    for(i=0;i<level.maxclients;i++) {
        gentity_t *ent=&g_entities[i];
        int slot=DefenderSlot(i);
        if(slot<0 || ent->client->pers.connected!=CON_CONNECTED)continue;
        if(ent->health<=0) {
            if(!late)continue;
            defenderCur=&defenders[slot];
            memset(&defender,0,sizeof(defender));
            defender.role=slot%3==2?DROLE_CAMPER:slot%3==1?DROLE_CHASE:trap->Cvar_VariableIntegerValue("bot_defenderRole")==1?DROLE_BASE:DROLE_SOLO;
            defender.client=i;defender.releaseTime=level.time;defender.pendingRelease=1;
            trap->Print("[DEFENDER] RELEASE_PENDING level=%d client=%d role=%s dead=1\n",level.time,i,roleNames[defender.role]);
            continue;
        }
        defenderCur=&defenders[slot];
        ReleaseOne(ent,i,slot,0,level.time);
    }
}

// A lethal hurt volume (fall to blackness, or damage the runner cannot take)
// that the hull at `origin` touches.
static qboolean HurtContactOf(gentity_t *runner,const vec3_t origin) {
    int list[MAX_GENTITIES],n,i;vec3_t mins,maxs;
    VectorAdd(origin,runner->r.mins,mins);VectorAdd(origin,runner->r.maxs,maxs);
    n=trap->EntitiesInBox(mins,maxs,list,MAX_GENTITIES);
    for(i=0;i<n;i++) {
        gentity_t *hit=&g_entities[list[i]];
        if(!hit->inuse || !hit->classname || Q_stricmp(hit->classname,"trigger_hurt") || (hit->flags&FL_INACTIVE))continue;
        if(hit->damage!=-1 && hit->damage<runner->health)continue;
        if(trap->EntityContact(mins,maxs,(sharedEntity_t*)hit,qfalse))return qtrue;
    }
    return qfalse;
}

// Timed for the perf counters: the hurt test each throw-sweep step makes.
static qboolean HurtContact(gentity_t *runner,const vec3_t origin) {
    qboolean hit;
    BOT2_PERF_BEGIN(perfHurt);
    hit=HurtContactOf(runner,origin);
    BOT2_PERF_END(BOT2_PERF_SWEEP,perfHurt);
    return hit;
}

enum {SWEEP_LANDS,SWEEP_LETHAL,SWEEP_HORIZON,SWEEP_SOLID};

// The runner's hull from `start`: gravity, one clip per contact with a steep
// plane, until a floor (normal z >= 0.7), a lethal hurt volume or the
// horizon. With `wishdir` the runner holds full air input that way, under
// PM_Accelerate's air rule (pm_airaccelerate 1: up to wishspeed per second,
// only while its speed along wishdir is under wishspeed); NULL is no input.
static int Sweep(gentity_t *runner,const vec3_t start,const vec3_t velocity,const vec3_t wishdir,int horizon,vec3_t end,int *atMs) {
    vec3_t pos,v,next;trace_t tr;int t;const float dt=.025f;
    float d,add,gravity=runner->client->ps.gravity,wishspeed=runner->client->ps.speed;
    VectorCopy(start,pos);VectorCopy(velocity,v);
    for(t=25;t<=horizon;t+=25) {
        if(wishdir && (add=wishspeed-DotProduct(v,wishdir))>0) {
            if(add>dt*wishspeed)add=dt*wishspeed;
            VectorMA(v,add,wishdir,v);
        }
        v[2]-=gravity*dt;
        VectorMA(pos,dt,v,next);
        trap->Trace(&tr,pos,runner->r.mins,runner->r.maxs,next,runner->s.number,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.allsolid) {VectorCopy(pos,end);*atMs=t;return SWEEP_SOLID;}
        VectorCopy(tr.endpos,pos);
        if(HurtContact(runner,pos)) {VectorCopy(pos,end);*atMs=t;return SWEEP_LETHAL;}
        if(tr.fraction<1) {
            if(tr.plane.normal[2]>=.7f) {VectorCopy(pos,end);*atMs=t;return SWEEP_LANDS;}
            d=DotProduct(v,tr.plane.normal);VectorMA(v,-1.001f*d,tr.plane.normal,v);
        }
    }
    VectorCopy(pos,end);*atMs=horizon;return SWEEP_HORIZON;
}

// How many of eight held air-input directions keep a lethal flight out of
// the hurt volume: zero means no air-control response reaches floor. With
// `any`, 1 at the first that does: for a caller that tests only for zero
// (Sweep has no side effects).
#define DEFENDER_ESCAPE_DIRECTIONS 8
static int Escapes(gentity_t *runner,const vec3_t start,const vec3_t velocity,int horizon,int any) {
    int i,n=0,ms;vec3_t wishdir,end;
    for(i=0;i<DEFENDER_ESCAPE_DIRECTIONS;i++) {
        float yaw=i*2*M_PI/DEFENDER_ESCAPE_DIRECTIONS;
        VectorSet(wishdir,cosf(yaw),sinf(yaw),0);
        if(Sweep(runner,start,velocity,wishdir,horizon,end,&ms)!=SWEEP_LETHAL && (n++,any))break;
    }
    return n;
}

// The target's counter to a throw (ForceThrow, CanCounterThrow): its own
// push or pull level, one less while it presses a move, and only on the
// ground, with its hand free, no weapon refire or charge and no counter in
// the last one. `grounded` is the frame the thrower perceives; the rest is
// the target's current state. 0 when it cannot counter.
static int CounterLevel(gentity_t *target,qboolean pull,qboolean grounded) {
    playerState_t *ps=&target->client->ps;int power=pull?FP_PULL:FP_PUSH,counter=ps->fd.forcePowerLevel[power];
    if(target->client->pers.cmd.forwardmove || target->client->pers.cmd.rightmove)counter--;
    if(counter<=0 || !grounded || ps->forceHandExtend!=HANDEXTEND_NONE || ps->weaponTime>0 || target->health<=0 ||
       ps->powerups[PW_DISINT_4]>level.time || ps->weaponstate==WEAPON_CHARGING || ps->weaponstate==WEAPON_CHARGING_ALT ||
       !WP_ForcePowerUsable(target,(forcePowers_t)power))return 0;
    return counter;
}

// ForceThrow's velocity rule for a target without absorb: 256*level, cut by
// the target's counter (all of it at an equal or higher level, else 80, 40
// or 20% for a gap of 1, 2 or 3 levels), then less 0.7*distance (at least
// 16) along the line between the origins; vertical the same, raised to 128
// when the target was not moving vertically. A countered throw still sets
// the target's velocity: it stops a runner even when it cannot move it.
static void ThrowVelocity(const vec3_t self,const vec3_t target,float targetVz,int powerLevel,int counter,qboolean pull,vec3_t out) {
    vec3_t dir;float length,mod;
    if(pull)VectorSubtract(self,target,dir);else VectorSubtract(target,self,dir);
    length=VectorNormalize(dir);
    mod=256*powerLevel;
    if(counter>=powerLevel)mod=0;
    else if(counter)mod-=mod*(powerLevel-counter>=3?.2f:powerLevel-counter==2?.4f:.8f);
    mod-=length*.7f;if(mod<16)mod=16;
    out[0]=dir[0]*mod;out[1]=dir[1]*mod;out[2]=dir[2]*mod;
    if((int)targetVz==0 && out[2]<128)out[2]=128;
}

static qboolean CanThrow(gentity_t *self,int power) {
    playerState_t *ps=&self->client->ps;int powerLevel=ps->fd.forcePowerLevel[power];
    return powerLevel>0 && ps->fd.forcePower>=forcePowerNeeded[powerLevel][power] && ps->forceHandExtend==HANDEXTEND_NONE &&
        ps->weaponTime<=0 && ps->powerups[PW_DISINT_4]<=level.time && WP_ForcePowerUsable(self,(forcePowers_t)power);
}

// Every throw condition but the Force it costs: the throw a Speed purchase
// (or an earlier throw) left it unable to afford.
static qboolean Starved(gentity_t *self,int power) {
    playerState_t *ps=&self->client->ps;int powerLevel=ps->fd.forcePowerLevel[power];
    return powerLevel>0 && ps->fd.forcePower<forcePowerNeeded[powerLevel][power] &&
        ps->forceHandExtend==HANDEXTEND_NONE && ps->weaponTime<=0 && ps->powerups[PW_DISINT_4]<=level.time;
}

static qboolean Visible(gentity_t *self,gentity_t *runner) {
    vec3_t eye,center;trace_t tr;
    VectorCopy(self->client->ps.origin,eye);eye[2]+=self->client->ps.viewheight;
    VectorAdd(runner->r.absmin,runner->r.absmax,center);VectorScale(center,.5f,center);
    if(!trap->InPVS(eye,center))return qfalse;
    trap->Trace(&tr,eye,NULL,NULL,center,self->s.number,MASK_SHOT,qfalse,0,0);
    return tr.fraction>=1 || tr.entityNum==runner->s.number;
}

// The sight state for the SIGHT log: 0 not in the eye's PVS, 1 in it but the
// line to the hull centre and to the head both blocked, 2 the head only, 3 the
// centre (what Visible, and so the defender, uses).
static int SightState(gentity_t *self,gentity_t *runner) {
    vec3_t eye,head;trace_t tr;
    if(Visible(self,runner))return 3;
    VectorCopy(self->client->ps.origin,eye);eye[2]+=self->client->ps.viewheight;
    VectorCopy(runner->client->ps.origin,head);head[2]+=runner->client->ps.viewheight;
    if(!trap->InPVS(eye,head))return 0;
    trap->Trace(&tr,eye,NULL,NULL,head,self->s.number,MASK_SHOT,qfalse,0,0);
    return tr.fraction>=1 || tr.entityNum==runner->s.number?2:1;
}

static void Remember(gentity_t *self,gentity_t *runner) {
    defenderSeen_t *s=&defender.seen[defender.seenNext];
    s->time=level.time;VectorCopy(runner->client->ps.origin,s->origin);VectorCopy(runner->client->ps.velocity,s->velocity);
    s->grounded=runner->client->ps.groundEntityNum!=ENTITYNUM_NONE;
    s->visible=runner->health>0 && Visible(self,runner);
    if(trap->Cvar_VariableIntegerValue("bot_defenderSightLog")) {
        // Every change of sight, with both positions (a diagnostic for the
        // 2D approach view; no decision reads it).
        int state=runner->health>0?SightState(self,runner):-1;
        if(state!=defender.sightState) {
            const float *d=self->client->ps.origin,*r=runner->client->ps.origin;
            defender.sightState=state;
            trap->Print("[DEFENDER] SIGHT level=%d state=%d runner=%.0f,%.0f,%.0f defender=%.0f,%.0f,%.0f distance=%.0f carrying=%d\n",
                level.time,state,r[0],r[1],r[2],d[0],d[1],d[2],Distance(d,r),
                !!runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG]);
        }
    }
    defender.seenNext=(defender.seenNext+1)%DEFENDER_HISTORY;
    if(defender.seenCount<DEFENDER_HISTORY)defender.seenCount++;
}

// The newest remembered frame at least `react` ms old.
static const defenderSeen_t *Perceived(int react) {
    int i;
    for(i=1;i<=defender.seenCount;i++) {
        const defenderSeen_t *s=&defender.seen[(defender.seenNext-i+DEFENDER_HISTORY)%DEFENDER_HISTORY];
        if(s->time<=level.time-react)return s;
    }
    return NULL;
}

// Points the command's view at `view` and turns forwardmove/rightmove with it,
// so the movement already in the command keeps its world direction and wish
// speed (the largest component) while the defender looks elsewhere.
// Unless `free`, a moving defender on the ground
// is not turned more than 89 degrees from where it is going: past 90 the
// turned command has forwardmove < 0, and BG_AdjustClientSpeed runs a
// grounded player backwards at 0.75 of its speed. The movement's rollouts
// never see that, and chase D's hops launched at 0.6-0.9 of the speed their
// rollout measured (43% of its launches, 1% of base D's) and fell short of
// Kejim's walkway (records/2026-09-30-defender-falls-and-route.md, "Second
// attempt"). Returns 1 when the view was held back, so no shot goes along it.
static int SetView(gentity_t *self,const vec3_t view,usercmd_t *cmd,int free) {
    int yaw,held=0;
    // bot_defenderKeepSequence: a committed sequence is flying or running up
    // (ViewGuard); its own view and command go to Pmove unchanged, and held=1
    // so no shot fires along a view this frame never aimed. `free` (Throw's
    // one-frame aim regardless of the move) is never held back this way.
    if(!free && ViewGuard(self->s.number))return 1;
    defender.viewSets++;
    yaw=ANGLE2SHORT(view[YAW])-self->client->ps.delta_angles[YAW];
    // On the ground, or touching down within this command: Pmove's substeps
    // after the touchdown pay the backwards speed too (the guest flight trace
    // found 53 landing frames turned backwards by the aim).
    if((cmd->forwardmove || cmd->rightmove) && !free && Move_GroundedThisFrame(self->s.number)) {
        float wish=SHORT2ANGLE(cmd->angles[YAW])+atan2f(-(float)cmd->rightmove,(float)cmd->forwardmove)*180/(float)M_PI;
        float off=AngleNormalize180(SHORT2ANGLE((short)yaw)-wish);
        // The mirror case: the movement's own command runs backwards (its
        // controller brakes and steers with the back key), and its rollout
        // ran at 0.75 speed. Turned so forwardmove >= 0, the live run went at
        // full speed, 1/0.75 faster than the rollout, and the hop launched
        // from the wrong state (the guest flight trace). That
        // frame keeps the movement's view and command; no shot goes along it.
        // bot_defenderClampHold (default 1): an out-of-reach view keeps the
        // movement's own view rather than the 89 degree edge, and the reach
        // narrows to 75 until a frame finds it in reach. The edge sits on the
        // travel direction, which the movement's controller swings from frame
        // to frame, and on whichever side the target is: aiming behind a
        // walking chase D it turned by up to 178 degrees a frame, the largest
        // group of its view flips (records/2026-10-01-golan-refit-mines-
        // jitter.md).
        float reach=CvarFloat("bot_defenderClampHold",1) && level.time-defender.clampTime<=50?75:89;
        if(cmd->forwardmove<0) {yaw=cmd->angles[YAW];held=1;}
        else if(off>reach || off<-reach) {
            yaw=CvarFloat("bot_defenderClampHold",1)?cmd->angles[YAW]:ANGLE2SHORT(wish+(off>0?89:-89));held=1;
            defender.clampTime=level.time;
        }
    }
    if(cmd->forwardmove || cmd->rightmove) {
        float d=SHORT2ANGLE((short)(yaw-cmd->angles[YAW]))*(float)M_PI/180,s=sinf(d),c=cosf(d);
        float f=cmd->forwardmove,r=cmd->rightmove,nf=f*c-r*s,nr=f*s+r*c;
        float was=max(fabsf(f),fabsf(r)),now=max(fabsf(nf),fabsf(nr)),k=now>0?was/now:0;
        cmd->forwardmove=(signed char)floorf(Com_Clamp(-127,127,nf*k)+0.5f);
        cmd->rightmove=(signed char)floorf(Com_Clamp(-127,127,nr*k)+0.5f);
    }
    cmd->angles[PITCH]=ANGLE2SHORT(view[PITCH])-self->client->ps.delta_angles[PITCH];
    cmd->angles[YAW]=yaw;
    cmd->angles[ROLL]=0;
    return held;
}

// The view at a target, held within 89 degrees of a moving defender's travel
// on the ground (SetView). Moving to or holding an ambush post never set
// guardMoving, so this idle aim turned chase D's run backwards at 0.75 speed
// until it clamped too (the guest flight trace, 182 frames in 16 worlds).
static int AimAt(gentity_t *self,const vec3_t target,usercmd_t *cmd,int free) {
    vec3_t eye,dir,angles;
    VectorCopy(self->client->ps.origin,eye);eye[2]+=self->client->ps.viewheight;
    VectorSubtract(target,eye,dir);vectoangles(dir,angles);
    return SetView(self,angles,cmd,free);
}
static void Aim(gentity_t *self,const vec3_t target,usercmd_t *cmd) {AimAt(self,target,cmd,0);}

static void Follow(gentity_t *runner) {
    playerState_t *ps;
    if(defender.fireTime<0 || defender.outcomeDone)return;
    // The client thrown at, not the target now: a carrier or a lone candidate replaces the target mid-window and
    // its push stamp and landing were scored instead.
    if(defender.fireRunner<0 || defender.fireRunner>=level.maxclients || !g_entities[defender.fireRunner].inuse ||
       !g_entities[defender.fireRunner].client) {defender.outcomeDone=1;return;}   // gone: the window closes
    runner=&g_entities[defender.fireRunner];
    ps=&runner->client->ps;
    if(!defender.hit) {
        // ForceThrow stamps the target's push effect at the throw's level time.
        // bot_defenderFixBA21: and the push named this thrower (ForceThrow stamps otherKiller with it).
        if(runner->client->pushEffectTime==defender.fireTime+600 && (!FixOn("bot_defenderFixBA21") ||
           (ps->otherKiller==defender.client && ps->otherKillerTime==defender.fireTime+5000))) {
            defender.hit=1;defender.hits++;
            // Carrier kills were a pull and then a shot: a hit throw
            // opens a window in which shots skip the engage delay.
            if(CvarFloat("bot_defenderFollowMs",3000)>0)defender.followUntil=level.time+(int)CvarFloat("bot_defenderFollowMs",3000);
            if(defender.fireCarrier && defender.fireKind==FP_PULL && defender.role==DROLE_CHASE && CvarFloat("bot_chaserPullClose",0)>0)
                defender.closeUntil=level.time+(int)CvarFloat("bot_chaserPullClose",0);
            trap->Print("[DEFENDER] HIT level=%d kind=%s runner_v=%.1f,%.1f,%.1f runner_z=%.1f ground=%d counter=%d predicted_counter=%d model_h=%.1f native_h=%.1f\n",
                level.time,defender.fireKind==FP_PULL?"pull":"push",ps->velocity[0],ps->velocity[1],ps->velocity[2],
                ps->origin[2],ps->groundEntityNum!=ENTITYNUM_NONE,
                ps->forceHandExtend==HANDEXTEND_FORCEPUSH || ps->forceHandExtend==HANDEXTEND_FORCEPULL,defender.fireCounter,
                defender.fireModelH,(float)sqrt(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]));
        } else if(level.time>defender.fireTime+50) {
            defender.misses++;defender.outcomeDone=1;
            trap->Print("[DEFENDER] MISS level=%d kind=%s throws=%d hits=%d misses=%d\n",
                level.time,defender.fireKind==FP_PULL?"pull":"push",defender.throws,defender.hits,defender.misses);
        }
        return;
    }
    if(!defender.landed) {
        if(ps->groundEntityNum==ENTITYNUM_NONE) {
            defender.airFrames++;
            if(runner->client->pers.cmd.forwardmove || runner->client->pers.cmd.rightmove)defender.airInputFrames++;
        } else {
            defender.landed=1;
            trap->Print("[DEFENDER] LANDED level=%d after_ms=%d predicted_ms=%d x=%.1f y=%.1f z=%.1f predicted_end=%.1f,%.1f,%.1f miss_xy=%.1f air_frames=%d air_input_frames=%d\n",
                level.time,level.time-defender.fireTime,defender.predictedMs,ps->origin[0],ps->origin[1],ps->origin[2],
                defender.predictedEnd[0],defender.predictedEnd[1],defender.predictedEnd[2],
                sqrtf((ps->origin[0]-defender.predictedEnd[0])*(ps->origin[0]-defender.predictedEnd[0])+
                      (ps->origin[1]-defender.predictedEnd[1])*(ps->origin[1]-defender.predictedEnd[1])),
                defender.airFrames,defender.airInputFrames);
        }
    }
    if(ps->fallingToDeath || runner->health<=0) {
        if(ps->fallingToDeath)defender.pits++;else defender.deaths++;
        defender.outcomeDone=1;
        trap->Print("[DEFENDER] OUTCOME level=%d result=%s after_ms=%d predicted_ms=%d x=%.1f y=%.1f z=%.1f throws=%d hits=%d pits=%d deaths=%d survived=%d\n",
            level.time,ps->fallingToDeath?"pit":"death",level.time-defender.fireTime,defender.predictedMs,
            ps->origin[0],ps->origin[1],ps->origin[2],defender.throws,defender.hits,defender.pits,defender.deaths,defender.survived);
    } else if(level.time>=defender.fireTime+DEFENDER_OUTCOME_MS) {
        defender.survived++;defender.outcomeDone=1;
        trap->Print("[DEFENDER] OUTCOME level=%d result=survived after_ms=%d predicted_ms=%d x=%.1f y=%.1f z=%.1f predicted_end=%.1f,%.1f,%.1f throws=%d hits=%d pits=%d deaths=%d survived=%d\n",
            level.time,level.time-defender.fireTime,defender.predictedMs,ps->origin[0],ps->origin[1],ps->origin[2],
            defender.predictedEnd[0],defender.predictedEnd[1],defender.predictedEnd[2],
            defender.throws,defender.hits,defender.pits,defender.deaths,defender.survived);
    }
}

static const int shadowLag[3]={250,500,1000};

// The runner as remembered at least `lag` ms ago, visible or not.
static const defenderSeen_t *Remembered(int lag) {
    int i;
    for(i=1;i<=defender.seenCount;i++) {
        const defenderSeen_t *s=&defender.seen[(defender.seenNext-i+DEFENDER_HISTORY)%DEFENDER_HISTORY];
        if(s->time<=level.time-lag)return s;
    }
    return NULL;
}

static void ShadowClose(int k,const char *why) {
    if(!defender.shadowOpen[k])return;
    defender.shadowOpen[k]=0;
    trap->Print("[DEFENDER] SHADOW_CLOSE level=%d lag_ms=%d open_ms=%d why=%s\n",
        level.time,shadowLag[k],level.time-defender.shadowOpenAt[k],why);
}

static void Shadow(gentity_t *self,gentity_t *runner) {
    int k,i,ms,escapes,carrying,kinds[2]={FP_PULL,FP_PUSH};
    vec3_t from,eye,center,velocity,end;trace_t tr;playerState_t *ps=&runner->client->ps;
    if(!trap->Cvar_VariableIntegerValue("bot_defenderShadow"))return;
    carrying=runner->health>0 && !ps->fallingToDeath &&
        ps->powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG];
    if(carrying && !defender.shadowCarrying) {
        defender.shadowCarrying=1;defender.shadowCarryStart=level.time;
        memset(defender.shadowFrames,0,sizeof(defender.shadowFrames));memset(defender.shadowLethal,0,sizeof(defender.shadowLethal));
        memset(defender.shadowTrapped,0,sizeof(defender.shadowTrapped));memset(defender.shadowWindows,0,sizeof(defender.shadowWindows));
    }
    if(!carrying) {
        if(defender.shadowCarrying) {
            defender.shadowCarrying=0;
            for(k=0;k<3;k++)ShadowClose(k,"carry_end");
            trap->Print("[DEFENDER] SHADOW_SUMMARY level=%d carry_ms=%d end=%s frames=%d,%d,%d lethal=%d,%d,%d trapped=%d,%d,%d windows=%d,%d,%d\n",
                level.time,level.time-defender.shadowCarryStart,runner->health<=0 || ps->fallingToDeath?"death":"no_flag",
                defender.shadowFrames[0],defender.shadowFrames[1],defender.shadowFrames[2],
                defender.shadowLethal[0],defender.shadowLethal[1],defender.shadowLethal[2],
                defender.shadowTrapped[0],defender.shadowTrapped[1],defender.shadowTrapped[2],
                defender.shadowWindows[0],defender.shadowWindows[1],defender.shadowWindows[2]);
        }
        return;
    }
    VectorAdd(runner->r.absmin,runner->r.absmax,center);VectorScale(center,.5f,center);
    for(k=0;k<3;k++) {
        const defenderSeen_t *past=Remembered(shadowLag[k]);
        int lethal=0,trapped=0,kind=-1,bestEscapes=DEFENDER_ESCAPE_DIRECTIONS;
        if(!past || past->time<defender.shadowCarryStart || Distance(past->origin,ps->origin)>=1000) {ShadowClose(k,"no_tail");continue;}
        VectorCopy(past->origin,from);VectorCopy(from,eye);eye[2]+=DEFAULT_VIEWHEIGHT;
        trap->Trace(&tr,eye,NULL,NULL,center,runner->s.number,MASK_SHOT,qfalse,0,0);
        if(tr.fraction<1) {ShadowClose(k,"no_sight");continue;}
        defender.shadowFrames[k]++;
        for(i=0;i<2;i++) {
            ThrowVelocity(from,ps->origin,ps->velocity[2],FORCE_LEVEL_3,
                CounterLevel(runner,kinds[i]==FP_PULL,ps->groundEntityNum!=ENTITYNUM_NONE),kinds[i]==FP_PULL,velocity);
            if(Sweep(runner,ps->origin,velocity,NULL,3000,end,&ms)!=SWEEP_LETHAL)continue;
            lethal=1;
            escapes=Escapes(runner,ps->origin,velocity,3000,0);
            if(escapes<bestEscapes) {bestEscapes=escapes;kind=kinds[i];}
            if(!escapes)trapped=1;
        }
        defender.shadowLethal[k]+=lethal;defender.shadowTrapped[k]+=trapped;
        if(trapped && !defender.shadowOpen[k]) {
            defender.shadowOpen[k]=1;defender.shadowOpenAt[k]=level.time;defender.shadowWindows[k]++;
            trap->Print("[DEFENDER] SHADOW_OPEN level=%d lag_ms=%d kind=%s carry_ms=%d runner=%.1f,%.1f,%.1f runner_v=%.1f,%.1f,%.1f tail=%.1f,%.1f,%.1f distance=%.1f\n",
                level.time,shadowLag[k],kind==FP_PULL?"pull":"push",level.time-defender.shadowCarryStart,
                ps->origin[0],ps->origin[1],ps->origin[2],ps->velocity[0],ps->velocity[1],ps->velocity[2],
                from[0],from[1],from[2],Distance(from,ps->origin));
        } else if(!trapped)ShadowClose(k,"escapable");
    }
}

static float SegmentDistance(const vec3_t p,const vec3_t a,const vec3_t b) {
    vec3_t ab,ap,c;float len=0,t=0;
    VectorSubtract(b,a,ab);VectorSubtract(p,a,ap);len=DotProduct(ab,ab);
    if(len>0)t=Com_Clamp(0,1,DotProduct(ap,ab)/len);
    VectorMA(a,t,ab,c);
    return Distance(p,c);
}

// The post to stand on: the first, or while the runner is near the own stand
// the one nearest its line to the stand, kept unless another is clearly
// better. Moves there by the movement owner; 1 while moving.
static int BlockMove(int clientNum,gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd);
static qboolean FlagHome(gentity_t *self);
static int GuardMove(int clientNum,gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd) {
    int i,best=defender.post;vec3_t aim={1,0,0};
    if(seen && seen->visible && Distance(seen->origin,defender.stand)<DEFENDER_GUARD_RANGE && defender.postCount>1) {
        float cur=SegmentDistance(defender.posts[defender.post],seen->origin,defender.stand),bestD=cur;
        for(i=0;i<defender.postCount;i++) {
            float d=SegmentDistance(defender.posts[i],seen->origin,defender.stand);
            if(d<bestD-DEFENDER_POST_SWITCH) {bestD=d;best=i;}
        }
    } else if(!seen || !seen->visible || Distance(seen->origin,defender.stand)>=DEFENDER_GUARD_RANGE)best=0;
    if(best!=defender.post) {
        trap->Print("[DEFENDER] POST_MOVE level=%d from=%d to=%d x=%.1f y=%.1f z=%.1f\n",level.time,defender.post,best,
            defender.posts[best][0],defender.posts[best][1],defender.posts[best][2]);
        defender.post=best;
    }
    {
        const float *p=defender.posts[defender.post];const float *o=self->client->ps.origin;
        if(sqrtf((p[0]-o[0])*(p[0]-o[0])+(p[1]-o[1])*(p[1]-o[1]))<=DEFENDER_POST_REACHED && fabsf(p[2]-o[2])<64)return 0;
    }
    DefenderMove(clientNum,self,cmd,defender.posts[defender.post]);
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    return 1;
}

// Milliseconds until the perceived airborne runner lands on the floor below it
// (ballistic, gravity 800); 0 when grounded, -1 when no floor is within 1,024.
static int LandMs(gentity_t *runner,const defenderSeen_t *seen) {
    vec3_t below;trace_t tr;float h,vz=seen->velocity[2],g=800;
    if(seen->grounded)return 0;
    VectorCopy(seen->origin,below);below[2]-=1024;
    trap->Trace(&tr,seen->origin,runner->r.mins,runner->r.maxs,below,runner->s.number,MASK_SOLID,qfalse,0,0);
    if(tr.fraction>=1 || tr.startsolid)return -1;
    h=seen->origin[2]-tr.endpos[2];
    return (int)((vz+sqrtf(vz*vz+2*g*h))/g*1000);
}

static float *eye0(gentity_t *self) {
    static vec3_t eye;
    VectorCopy(self->client->ps.origin,eye);eye[2]+=self->client->ps.viewheight;
    return eye;
}

// The weapon in hand is kept while it is firing or charging; otherwise the
// loadout entry with the most expected damage a second at the runner now
// (measured hit chance at its reach ratio, damage, refire time) is taken
// when it beats the one in hand by 25%.
// A splash shot whose burst reaches the shooter is still fired, and the
// shooter braces against it (Brace; KnockSafe may make it wait). A
// defender's own rocket or repeater alt fired at a runner 50-120 u away
// knocked it 180-400 u/s, and 22 of 77 defender falls in the laps
// arms followed one within 3 s (records/2026-09-30-defender-falls-and-
// route.md, "Second attempt"). The damage is not given up, and not the repeater's bolts, which the runner's saber reflects.
#define DEFENDER_SELF_SPLASH_MARGIN 32.0f
#define DEFENDER_BRACE_MS 400      // bracing lasts the flight time plus this
#define DEFENDER_BRACE_PUSH 150.0f // moving away from the burst faster than this is the knock
#define DEFENDER_BRACE_FLOOR 48.0f // leaning in needs floor this far towards the burst

// bot_defenderKnockSafe (default 1): a splash shot whose burst reaches the
// shooter waits (PRE_SELF, leaning into the burst) only while the knock it
// would take could not end on floor. At 40-75 u a rocket arrives in one or
// two frames and lifts the shooter (dv up to 570, vz up to 300), and no
// bracing after the press saves a shot fired running at an edge. The knock is
// the 90th percentile of the measured self-knocks by weapon and burst
// distance (BRACE against knock dv, 16 worlds of the brace arms); it lifts
// the shooter by 0.6 of it and pushes it by 0.8 of it away from the burst, on
// top of its own velocity. The flight to the height it left from and a slide
// of v/6 + 16 u after it must stay on graph floor within two steps of that
// height; a landing on a lower walkway step at 500 u/s went on over the next
// edge in the first version (build falls3 d), which only asked for any floor
// within 128 u below.
static float SelfKnock(int weapon,int mode,float d) {
    if(weapon==WP_ROCKET_LAUNCHER)return d<60?500:d<80?430:d<130?385:300;
    if(weapon==WP_REPEATER && mode==BOT2_AIM_ALT)return d<60?390:d<80?280:d<100?240:d<130?225:210;
    // The flechette alt: the 90th percentile of 75 single-blast knocks with
    // g_fixGolanDamage 1 and KnockSafe off (refit-20261001-a). The stock
    // double blast needed 1050/675/425 (records/2026-10-01-knock-replays.md).
    if(weapon==WP_FLECHETTE && mode==BOT2_AIM_ALT)return d<80?460:d<130?345:200;
    if(weapon==WP_CONCUSSION && mode==BOT2_AIM_PRIMARY)return d<100?450:350;
    return 0;
}
static int FloorAt(const vec3_t p,float z) {
    vec3_t at;int cell;const graphCell_t *c;
    VectorSet(at,p[0],p[1],z);
    cell=Graph_CellAt(at,2*STEPSIZE);
    if(cell<0)return 0;
    c=Graph_Cell(cell);
    return c && fabsf(c->z-z)<=2*STEPSIZE;
}
static int KnockSafe(gentity_t *self,const vec3_t burst,int weapon,int mode) {
    playerState_t *ps=&self->client->ps;float r=Bot2_AimSplashRadius(weapon,mode),d=Distance(ps->origin,burst),k,dx,dy,h,vx,vy,vz,t,v,slide;
    vec3_t p;int i;
    if(!CvarFloat("bot_defenderKnockSafe",1) || r<=0 || d>=r+DEFENDER_SELF_SPLASH_MARGIN)return 1;
    k=SelfKnock(weapon,mode,d);
    if(k<=0)return 1;
    dx=ps->origin[0]-burst[0];dy=ps->origin[1]-burst[1];h=sqrtf(dx*dx+dy*dy);
    if(h<1) {dx=dy=0;} else {dx/=h;dy/=h;}
    vx=ps->velocity[0]+.8f*k*dx;vy=ps->velocity[1]+.8f*k*dy;vz=max(ps->velocity[2],0.0f)+.6f*k;
    // Where it comes down at the height it left from (a gap on the way is cleared).
    t=2*vz/g_gravity.value;
    VectorSet(p,ps->origin[0]+vx*t,ps->origin[1]+vy*t,ps->origin[2]);
    if(!FloorAt(p,ps->origin[2]))return 0;
    v=sqrtf(vx*vx+vy*vy);slide=v/6+16;
    if(v>1)for(i=1;i<=3;i++) {
        vec3_t q;
        VectorSet(q,p[0]+vx/v*slide*i/3,p[1]+vy/v*slide*i/3,p[2]);
        if(!FloorAt(q,ps->origin[2]))return 0;
    }
    return 1;
}
static qboolean Stocked(gentity_t *self,int k);
static qboolean Armed(gentity_t *self);
static void ChooseWeapon(gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,const float *eye) {
    playerState_t *ps=&self->client->ps;int i,best=-1,forced,empty,fix4;float bestScore=0,current=0,speed,dist=Distance(eye,seen->origin);
    float flechetteMax=CvarFloat("bot_defenderFlechetteMax",250);
    // bot_defenderFixBA5: the pistol PistolChoose took holds until a loadout weapon is stocked (Armed).
    if(defender.weapon==WP_BRYAR_PISTOL && !Armed(self) && FixOn("bot_defenderFixBA5"))return;
    empty=ps->ammo[weaponData[defender.weapon].ammoIndex]<(defender.mode==BOT2_AIM_ALT?weaponData[defender.weapon].altEnergyPerShot:
        weaponData[defender.weapon].energyPerShot);
    // An empty weapon in hand is never held, and not waited out either: the
    // engine's own out-of-ammo switch keeps weaponTime running while the
    // command forces the empty weapon back, so the weaponTime wait never ended.
    if(defender.loadoutCount<2 || (!empty && (ps->weaponTime>0 || ps->weaponstate==WEAPON_CHARGING || ps->weaponstate==WEAPON_CHARGING_ALT)))return;
    // A flechette alt in hand beyond bot_defenderFlechetteMax is dropped for
    // the best other weapon at once: its direct hits cannot be forecast past
    // about 250 ms of flight (records/2026-09-29-defender-gaps.md).
    forced=empty || (flechetteMax>0 && defender.weapon==WP_FLECHETTE && defender.mode==BOT2_AIM_ALT && dist>flechetteMax);
    // A switch costs a lower and a raise, time it cannot fire: hold a choice
    // DEFENDER_SWITCH_HOLD_MS (the first validation switched 215 times in 48
    // worlds and fired almost only the concussion).
    if(!forced && defender.switchTime && level.time-defender.switchTime<DEFENDER_SWITCH_HOLD_MS)return;
    fix4=FixOn("bot_defenderFixBA4");
    speed=sqrtf(seen->velocity[0]*seen->velocity[0]+seen->velocity[1]*seen->velocity[1]);
    for(i=0;i<defender.loadoutCount;i++) {
        bot2_aimTarget_t t;bot2_aimSolution_t sol;float radius,score;int w=defender.loadout[i],m=defender.loadoutMode[i];
        if(!(ps->stats[STAT_WEAPONS]&(1<<w)) ||
           ps->ammo[weaponData[w].ammoIndex]<(m==BOT2_AIM_ALT?weaponData[w].altEnergyPerShot:weaponData[w].energyPerShot))continue;
        if(flechetteMax>0 && w==WP_FLECHETTE && m==BOT2_AIM_ALT && dist>flechetteMax)continue;
        VectorCopy(seen->origin,t.origin);VectorCopy(seen->velocity,t.velocity);
        VectorCopy(runner->r.mins,t.mins);VectorCopy(runner->r.maxs,t.maxs);t.grounded=seen->grounded;t.skip=runner->s.number;
        if(trap->Cvar_VariableIntegerValue("bot_aimLeadReact"))Bot2_AimAdvance(&t,(level.time-seen->time)/1000.0f);
        if(!(Bot2_AimSolve(eye,w,m,&t,&sol)&BOT2_AIM_OK))continue;
        if((sol.flags&BOT2_AIM_RANGE) && fix4)continue;   // bot_defenderFixBA4: an arc that cannot reach scores nothing
        radius=Bot2_AimSplashRadius(w,m);
        // The flechette alt's own chance (two balls, half the pair's damage
        // each) replaces the measured table, fitted on floor-aimed balls.
        score=(w==WP_FLECHETTE && m==BOT2_AIM_ALT && trap->Cvar_VariableIntegerValue("bot_aimGolan")?2*sol.chance1*Bot2_AimDamage(w,m)*.5f:
               Bot2_AimHitChance(w,m,radius>0?speed*sol.tof/radius:99)*Bot2_AimDamage(w,m))/
              ((m==BOT2_AIM_ALT?weaponData[w].altFireTime:weaponData[w].fireTime)/1000.0f);
        if(w==defender.weapon && m==defender.mode)current=score;
        if(best<0 || score>bestScore) {best=i;bestScore=score;}
    }
    // The weapon in hand cannot fire a shot and no stocked weapon solved: take
    // any stocked one. Both chase D's of a frozen 4v4 world held an
    // empty weapon beside the enemy carrier for four minutes: at 100 u nothing
    // else solved, and Armed() kept the rearm and the saber off
    // (records/2026-10-02-b3-stalls.md).
    if(best<0 && empty)for(i=0;i<defender.loadoutCount && best<0;i++)if(Stocked(self,i))best=i;
    if(best<0 || (defender.loadout[best]==defender.weapon && defender.loadoutMode[best]==defender.mode))return;
    if(!forced && bestScore<=current*1.5f)return;
    defender.switches++;defender.switchTime=level.time;
    trap->Print("[DEFENDER] WEAPON level=%d from=%d%s to=%d%s score=%.1f current=%.1f distance=%.1f switches=%d\n",level.time,
        defender.weapon,defender.mode==BOT2_AIM_ALT?"a":"",defender.loadout[best],defender.loadoutMode[best]==BOT2_AIM_ALT?"a":"",
        bestScore,current,Distance(eye,seen->origin),defender.switches);
    defender.weapon=defender.loadout[best];defender.mode=defender.loadoutMode[best];
}

// A mode that bursts at the floor, whose shot waits for a synced target in
// style 2. The flechette alt's balls bounce off the floor: aimed at the body.
static int Splashes(int weapon,int mode) {
    return weapon==WP_ROCKET_LAUNCHER || weapon==WP_CONCUSSION && mode==BOT2_AIM_PRIMARY ||
           weapon==WP_REPEATER && mode==BOT2_AIM_ALT;
}

// One frame before the pickup, by why it did or did not fire.
static void PreTally(gentity_t *self,gentity_t *runner,int reason) {
    defender.lastPre=reason;defender.lastPreTime=level.time;
    if(runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG] || runner->health<=0)return;
    defender.pre[reason]++;
}

static float CvarFloat(const char *name,float fallback) {
    char text[32];
    trap->Cvar_VariableStringBuffer(name,text,sizeof(text));
    return text[0]?(float)atof(text):fallback;
}

// Shoots the perceived runner with the declared weapon, aimed by the shared
// kernel through the humanized view. A frame that does not aim hands the view
// back to the movement, and the humanized view restarts from where it is.
static void Shoot(gentity_t *self,gentity_t *runner,const defenderSeen_t *perceived,usercmd_t *cmd) {
    playerState_t *ps=&self->client->ps;vec3_t eye,view;trace_t tr;float dist,range,left;
    bot2_aimTarget_t t;bot2_aimSolution_t sol;bot2_aimHuman_t h;int mode,cost,grace,charge,moving,charging,flick,style,hold=0,gated=0,engaging=0,pacing=0;float angular=0;
    int react=trap->Cvar_VariableIntegerValue("bot_defenderReactMs"),viewMs=(int)CvarFloat("bot_aimViewMs",100);
    // The decisions (to engage, to throw) run on the runner as perceived
    // bot_defenderReactMs ago; the aim tracks it as seen bot_aimViewMs ago
    // (default 100; humans track what they see about 50-100 ms late,
    // records/2026-09-28-human-aim-study.md). Tracking the 250 ms state held
    // repeater alt to 47% against 81% with a current one (defender-gaps).
    const defenderSeen_t *seen=perceived;
    int close,selfHeld=0,fix6=FixOn("bot_defenderFixBA6"),gap=level.time-defender.shootTime;
    float closeRange=CvarFloat("bot_aimDirectRange",200);
    defender.shootTime=level.time;
    if(viewMs>=0 && viewMs<react) {const defenderSeen_t *v=Perceived(viewMs);if(v && v->visible)seen=v;}
    // Within bot_aimDirectRange (default 200) the aim is exact: the runner as it is now, at its
    // body, with no view lag, turn cap, error, engage delay, pacing or gate.
    close=Distance(eye0(self),seen->origin)<closeRange;
    if(close) {const defenderSeen_t *v=Perceived(0);if(v && v->visible)seen=v;}
    if(!defender.weapon || runner->health<=0 || runner->client->ps.fallingToDeath) {
        Bot2_AimHumanReset(self->s.number,ps->viewangles);defender.engageSince=defender.engageLast=0;PreTally(self,runner,PRE_OTHER);return;
    }
    if(!(ps->stats[STAT_WEAPONS]&(1<<defender.weapon))) {
        int k;
        for(k=0;k<defender.loadoutCount;k++)if(ps->stats[STAT_WEAPONS]&(1<<defender.loadout[k])) {
            defender.weapon=defender.loadout[k];defender.mode=defender.loadoutMode[k];break;
        }
    }
    cmd->weapon=defender.weapon;
    ChooseWeapon(self,runner,seen,eye0(self));
    cmd->weapon=defender.weapon;
    mode=defender.mode;
    cost=mode==BOT2_AIM_ALT?weaponData[defender.weapon].altEnergyPerShot:weaponData[defender.weapon].energyPerShot;
    charge=defender.weapon==WP_BRYAR_PISTOL?(int)CvarFloat("bot_defenderPistolChargeMs",1000):trap->Cvar_VariableIntegerValue("bot_defenderChargeMs");
    charging=ps->weaponstate==WEAPON_CHARGING_ALT || ps->weaponstate==WEAPON_CHARGING;
    range=(float)trap->Cvar_VariableIntegerValue("bot_defenderShootRange");if(range<=0)range=1500;
    VectorCopy(ps->origin,eye);eye[2]+=ps->viewheight;
    dist=Distance(eye,seen->origin);
    grace=trap->Cvar_VariableIntegerValue("bot_aimEngageGraceMs");
    if(dist<range) {
        // With a grace, the engage delay restarts only after the runner has been
        // out of sight or range that long (latency was measured
        // from a sighting after 1 s out of view); a weapon not ready, a throw's
        // hand extend or one hidden frame no longer restart it.
        if(grace>0 && defender.engageSince && level.time-defender.engageLast>grace)defender.engageSince=0;
        defender.engageLast=level.time;
    }
    if(ps->weapon!=defender.weapon || ps->forceHandExtend!=HANDEXTEND_NONE || ps->weaponstate==WEAPON_DROPPING ||
       ps->weaponstate==WEAPON_RAISING || ps->ammo[weaponData[defender.weapon].ammoIndex]<cost ||
       dist>=range) {
        Bot2_AimHumanReset(self->s.number,ps->viewangles);if(grace<=0)defender.engageSince=0;
        // Which weapon refusal, for the carry diagnostic: none of the loadout
        // owned, a throw's hand extend, a switch (raising, dropping or not yet
        // selected) or the ammo.
        defender.lastWeap=!(ps->stats[STAT_WEAPONS]&(1<<defender.weapon))?"noweap":ps->forceHandExtend!=HANDEXTEND_NONE?"hand":
            ps->weapon!=defender.weapon || ps->weaponstate==WEAPON_DROPPING || ps->weaponstate==WEAPON_RAISING?"switch":"ammo";
        PreTally(self,runner,dist>=range?PRE_RANGE:PRE_WEAPON);return;
    }
    if(!defender.engageSince)defender.engageSince=level.time;
    style=trap->Cvar_VariableIntegerValue("bot_aimStyle");
    VectorCopy(seen->origin,t.origin);VectorCopy(seen->velocity,t.velocity);
    VectorCopy(runner->r.mins,t.mins);VectorCopy(runner->r.maxs,t.maxs);t.grounded=seen->grounded;t.skip=runner->s.number;
    // bot_aimLeadReact 1: the perceived state is bot_defenderReactMs old, so
    // it is carried forward that long before the lead (perceived facts only).
    if(trap->Cvar_VariableIntegerValue("bot_aimLeadReact") && style!=1)Bot2_AimAdvance(&t,(level.time-seen->time)/1000.0f);
    if(style!=1) {
        if(!(Bot2_AimSolve(eye,defender.weapon,mode,&t,&sol)&BOT2_AIM_OK)) {Bot2_AimHumanReset(self->s.number,ps->viewangles);return;}
        // bot_defenderFixBA4: an arc that cannot reach is held, not fired short.
        if((sol.flags&BOT2_AIM_RANGE) && FixOn("bot_defenderFixBA4")) {
            Bot2_AimHumanReset(self->s.number,ps->viewangles);PreTally(self,runner,PRE_RANGE);return;
        }
        if((sol.flags&BOT2_AIM_SPLASH) && close) {
            // Close in, the body instead of the floor (going for directs over ground
            // splash makes sense this close): a direct hit does the full damage, and the forecast is
            // good to about 13 u over the first 250 ms.
            bot2_aimSolution_t direct;
            if(Bot2_AimSolveDirect(eye,defender.weapon,mode,&t,&direct)&BOT2_AIM_OK) {direct.flags|=sol.flags&BOT2_AIM_SYNC;sol=direct;}
        }
        {
            vec3_t across;float along;
            VectorSubtract(t.origin,eye,across);VectorNormalize(across);along=DotProduct(t.velocity,across);
            VectorMA(t.velocity,-along,across,across);
            angular=VectorLength(across)/max(dist,1.0f)*180/M_PI;
        }
    } else {
        // Humans aim at the target as it is (the disruptor as it was 75 ms
        // before) with the error they show at its angular speed.
        const defenderSeen_t *past=defender.weapon==WP_DISRUPTOR?Perceived(react+75):NULL;
        vec3_t across;float along;
        if(past && past->visible) {VectorCopy(past->origin,t.origin);VectorCopy(past->velocity,t.velocity);}
        if(!(Bot2_AimHumanPoint(eye,defender.weapon,mode,&t,&sol)&BOT2_AIM_OK)) {Bot2_AimHumanReset(self->s.number,ps->viewangles);return;}
        VectorSubtract(t.origin,eye,across);VectorNormalize(across);along=DotProduct(t.velocity,across);
        VectorMA(t.velocity,-along,across,across);
        angular=VectorLength(across)/max(dist,1.0f)*180/M_PI;
    }
    if(!(sol.flags&BOT2_AIM_LOB)) {
        trap->Trace(&tr,eye,NULL,NULL,sol.point,self->s.number,MASK_SHOT,qfalse,0,0);
        // A floor burst the shooter cannot see (a ledge between it and the
        // floor under the target): the same lead at the body instead.
        if(tr.fraction<1 && tr.entityNum!=runner->s.number && Distance(tr.endpos,sol.point)>64 && (sol.flags&BOT2_AIM_SPLASH) && style!=1) {
            bot2_aimSolution_t direct;
            if(Bot2_AimSolveDirect(eye,defender.weapon,mode,&t,&direct)&BOT2_AIM_OK) {
                trace_t again;
                trap->Trace(&again,eye,NULL,NULL,direct.point,self->s.number,MASK_SHOT,qfalse,0,0);
                if(again.fraction>=1 || again.entityNum==runner->s.number || Distance(again.endpos,direct.point)<=64) {
                    direct.flags|=sol.flags&BOT2_AIM_SYNC;sol=direct;tr=again;
                }
            }
        }
        if(tr.fraction<1 && tr.entityNum!=runner->s.number && Distance(tr.endpos,sol.point)>64) {
            Bot2_AimHumanReset(self->s.number,ps->viewangles);PreTally(self,runner,PRE_BLOCKED);return;
        }
    }
    if(!KnockSafe(self,sol.point,defender.weapon,mode)) {
        selfHeld=1;   // lean into the burst while it waits (Brace, next frame)
        defender.braceUntil=level.time+100;VectorCopy(sol.point,defender.braceTo);defender.braceKnocked=0;
    }
    moving=defender.chasing || defender.guardMoving;
    flick=trap->Cvar_VariableIntegerValue("bot_aimFlickMs");if(flick<=0)flick=250;
    if(moving && !charging && ps->weaponTime>flick) {Bot2_AimHumanReset(self->s.number,ps->viewangles);PreTally(self,runner,PRE_MOVING);return;}
    h.lagMs=CvarFloat("bot_aimLagMs",style?300:0);h.turnRate=CvarFloat("bot_aimTurnRate",style?500:0);
    h.errorDeg=style?Bot2_AimHumanError(defender.weapon,mode,angular,CvarFloat("bot_aimErrorScale",1)):CvarFloat("bot_aimErrorDeg",0);
    h.errorMs=CvarFloat("bot_aimErrorMs",style?200:0);
    if(close)h.lagMs=h.turnRate=h.errorDeg=h.errorMs=0;
    // bot_aimGate: 1 (default) holds a splash shot until synced, 0 fires at will.
    if(style==2 && Splashes(defender.weapon,mode) && !close) {
        int gate=(int)CvarFloat("bot_aimGate",1);
        gated=1;
        float radius=Bot2_AimSplashRadius(defender.weapon,mode),travel;
        travel=sqrtf(seen->velocity[0]*seen->velocity[0]+seen->velocity[1]*seen->velocity[1])*sol.tof;
        if(gate==1 && !(sol.flags&BOT2_AIM_SYNC))hold=1;
        if(gate==2 && radius>0 && travel>CvarFloat("bot_aimReach",1)*radius)hold=1;
        gated=hold;
    }
    if(defender.weapon==WP_FLECHETTE && mode==BOT2_AIM_ALT && CvarFloat("bot_defenderFlechetteMax",250)>0 &&
       dist>CvarFloat("bot_defenderFlechetteMax",250))hold=gated=1;   // no flechette lob beyond the cap
    if(defender.weapon==WP_FLECHETTE && mode==BOT2_AIM_ALT && style!=1 && sol.chance<CvarFloat("bot_aimGolanMin",0)) {
        // bot_aimGolanMin: hold a flechette alt whose two balls are less
        // likely than this to land one direct hit.
        hold=gated=1;
    }
    if(style && !close) {
        // Engage delay and pacing: aiming continues, the trigger waits.
        int engage=(int)CvarFloat("bot_aimEngageMs",1200);
        float pace=CvarFloat("bot_aimPace",1.15f);
        int refire=mode==BOT2_AIM_ALT?weaponData[defender.weapon].altFireTime:weaponData[defender.weapon].fireTime;
        engaging=level.time-defender.engageSince<engage && level.time>=defender.followUntil;
        pacing=defender.lastShot>0 && level.time-defender.lastShot<(int)(refire*pace);
        hold=hold || engaging || pacing;
    }
    hold=hold || selfHeld;
    // bot_defenderFixBA6: after frames without Shoot the view restarts from the body's, and a held frame
    // does not advance it.
    if(fix6 && gap>100)Bot2_AimHumanReset(self->s.number,ps->viewangles);
    left=Bot2_AimHumanStep(self->s.number,sol.angles,&h,view);
    if(SetView(self,view,cmd,0)) {
        if(fix6)Bot2_AimHumanReset(self->s.number,ps->viewangles);
        if(charging)cmd->buttons|=mode==BOT2_AIM_ALT?BUTTON_ALT_ATTACK:BUTTON_ATTACK;   // keep a charge, do not release it off target
        PreTally(self,runner,PRE_MOVING);return;
    }
    // Only an alt-fire that charges (bg_pmove.c PM_Weapon: the pistols, DEMP2, thermal, the rocket lock) is held for
    // a charge; any other would fire on the press and keep firing, past the gates below.
    if(charge>0 && mode==BOT2_AIM_ALT && defender.weapon!=WP_BRYAR_PISTOL && defender.weapon!=WP_BRYAR_OLD &&
       defender.weapon!=WP_DEMP2 && defender.weapon!=WP_THERMAL && defender.weapon!=WP_ROCKET_LAUNCHER)charge=0;
    if(charge>0 && mode==BOT2_AIM_ALT) {
        // A charged shot: alt held from ready until charged and aligned; the
        // release fires it.
        int charged=ps->weaponstate==WEAPON_CHARGING_ALT?level.time-ps->weaponChargeTime:0;
        if(!charging && ps->weaponTime>0)return;
        if(charged<charge || hold || left>CvarFloat("bot_aimFireDeg",3)) {cmd->buttons|=BUTTON_ALT_ATTACK;return;}
    } else {
        if(ps->weaponTime>0 || hold || left>CvarFloat("bot_aimFireDeg",3)) {
            PreTally(self,runner,ps->weaponTime>0?PRE_REFIRE:selfHeld?PRE_SELF:engaging?PRE_ENGAGE:pacing?PRE_PACE:gated?PRE_GATE:PRE_ALIGN);
            return;
        }
        cmd->buttons|=mode==BOT2_AIM_ALT?BUTTON_ALT_ATTACK:BUTTON_ATTACK;
    }
    PreTally(self,runner,PRE_FIRED);
    if(Bot2_AimSplashRadius(defender.weapon,mode)>0 &&
       Distance(ps->origin,sol.point)<Bot2_AimSplashRadius(defender.weapon,mode)+DEFENDER_SELF_SPLASH_MARGIN) {
        defender.braceUntil=level.time+(int)(sol.tof*1000)+DEFENDER_BRACE_MS;VectorCopy(sol.point,defender.braceTo);
        defender.braceKnocked=0;defender.braces++;
        trap->Print("[DEFENDER] BRACE level=%d role=%s weapon=%d mode=%d burst=%.0f,%.0f,%.0f distance=%.1f tof_ms=%d grounded=%d braces=%d\n",
            level.time,roleNames[defender.role],defender.weapon,mode,sol.point[0],sol.point[1],sol.point[2],Distance(ps->origin,sol.point),
            (int)(sol.tof*1000),ps->groundEntityNum!=ENTITYNUM_NONE,defender.braces);
    }
    defender.pressTime=level.time;defender.pressAmmo=ps->ammo[weaponData[defender.weapon].ammoIndex];defender.pressWeaponTime=ps->weaponTime;
    defender.pressDistance=dist;defender.pressLead=Distance(sol.point,seen->origin);
    defender.pressSync=!!(sol.flags&BOT2_AIM_SYNC);defender.pressGrounded=seen->grounded;defender.pressMode=mode;
    defender.pressCost=ps->ammo[weaponData[defender.weapon].ammoIndex]<0?0:cost;defender.pressChance=sol.chance;
    defender.pressTof=sol.tof;defender.pressSpeed=sqrtf(seen->velocity[0]*seen->velocity[0]+seen->velocity[1]*seen->velocity[1]);
    defender.pressVz=seen->velocity[2];defender.pressAngular=angular;defender.pressLandMs=LandMs(runner,seen);
    defender.pressCarrying=!!runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG];
}

// The press of the last frame fired when the ammo fell since, or (a shot
// that spends none, the pistol) its refire time started: a FIRE line. A dry
// press also starts the refire time, so a shot with a cost needs the ammo.
static void ShotConfirm(gentity_t *self) {
    playerState_t *ps=&self->client->ps;
    if(!defender.weapon || !defender.pressTime)return;
    if(defender.pressCost>0?ps->ammo[weaponData[defender.weapon].ammoIndex]<defender.pressAmmo:ps->weaponTime>defender.pressWeaponTime) {
        defender.lastShot=defender.pressTime;defender.shots++;
        trap->Print("[DEFENDER] FIRE level=%d role=%s weapon=%d mode=%d distance=%.1f lead=%.1f tof_ms=%d sync=%d grounded=%d land_ms=%d speed=%.0f vz=%.0f angular=%.0f carrying=%d chasing=%d shots=%d chance=%.2f self_air=%d\n",
            defender.pressTime,roleNames[defender.role],defender.weapon,defender.pressMode,defender.pressDistance,defender.pressLead,(int)(defender.pressTof*1000),
            defender.pressSync,defender.pressGrounded,defender.pressLandMs,defender.pressSpeed,defender.pressVz,defender.pressAngular,
            defender.pressCarrying,defender.chasing,defender.shots,defender.pressChance,ps->groundEntityNum==ENTITYNUM_NONE);
    }
    defender.pressTime=0;
}

// The throw as issued: aimed at the runner, followed to its outcome.
// Bracing against its own burst, on the ground: before the knock it leans
// into the burst (full forward towards it) where the floor goes on that way,
// else it brakes against its velocity; once knocked away from the burst it
// runs against its velocity until the window ends. Ground acceleration undoes
// a 300 u/s knock in a few frames, where air control cannot. The command
// owns the move for those frames; the view is set later (SetView keeps the
// move's world direction). Airborne, the movement's knock recovery owns it.
static void Brace(gentity_t *self,usercmd_t *cmd) {
    playerState_t *ps=&self->client->ps;float dx,dy,d,vx=ps->velocity[0],vy=ps->velocity[1],v=sqrtf(vx*vx+vy*vy);
    if(!defender.braceUntil || level.time>=defender.braceUntil) {defender.braceUntil=0;return;}
    if(ps->groundEntityNum==ENTITYNUM_NONE)return;
    // Standing on a player: no brace; the movement's step-off owns the
    // move. On the enemy carrier's head the burst
    // is straight below, the lean has no direction (d<1) and stopped the move,
    // so the step-off never left: a 4v4 world froze for 247 s with chase D on
    // the carrier and the carrier refusing every hop under it
    // (records/2026-10-03-b3-dantooine.md).
    if(ps->groundEntityNum<MAX_CLIENTS)return;
    // bot_defenderKeepSequence: a committed sequence owns forwardmove/
    // rightmove/upmove this frame; braced or not, let it fly or run up.
    if(SequenceGuard(self->s.number))return;
    dx=defender.braceTo[0]-ps->origin[0];dy=defender.braceTo[1]-ps->origin[1];d=sqrtf(dx*dx+dy*dy);
    if(v>DEFENDER_BRACE_PUSH && vx*dx+vy*dy<0) {
        if(!defender.braceKnocked)trap->Print("[DEFENDER] BRACE_KNOCK level=%d role=%s speed=%.0f\n",level.time,roleNames[defender.role],v);
        defender.braceKnocked=1;
    }
    if(defender.braceKnocked || d<1) {dx=-vx;dy=-vy;d=v;}
    else {
        // Leaning in only where the floor goes on (the burst can be over a lip).
        vec3_t start,end,mins={-8,-8,0},maxs={8,8,8};trace_t tr;
        VectorSet(start,ps->origin[0]+dx/d*DEFENDER_BRACE_FLOOR,ps->origin[1]+dy/d*DEFENDER_BRACE_FLOOR,ps->origin[2]);
        VectorSet(end,start[0],start[1],start[2]-64);
        trap->Trace(&tr,start,mins,maxs,end,self->s.number,MASK_PLAYERSOLID,qfalse,0,0);
        if(tr.startsolid || tr.fraction>=1) {dx=-vx;dy=-vy;d=v;}
    }
    if(d<1) {cmd->forwardmove=cmd->rightmove=0;cmd->upmove=0;return;}
    cmd->angles[YAW]=ANGLE2SHORT(atan2f(dy,dx)*180/(float)M_PI)-ps->delta_angles[YAW];
    cmd->forwardmove=127;cmd->rightmove=0;cmd->upmove=0;
}

static void Throw(gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd,int kind,const char *reason,
                  int react,int predictedMs,const vec3_t predictedEnd,int baseline,int escapes,int mode) {
    int replaced=cmd->generic_cmd;   // a Speed purchase this frame is dropped (THROW replaced=)
    AimAt(self,seen->origin,cmd,1);   // the throw faces the runner whatever it costs one frame
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK);
    cmd->generic_cmd=kind==FP_PULL?GENCMD_FORCE_PULL:GENCMD_FORCE_THROW;
    defender.fireTime=level.time;defender.fireKind=kind;defender.fireRunner=runner->s.number;
    {
        playerState_t *rps=&runner->client->ps;vec3_t v;
        defender.fireCounter=CounterLevel(runner,kind==FP_PULL,rps->groundEntityNum!=ENTITYNUM_NONE);
        ThrowVelocity(self->client->ps.origin,rps->origin,rps->velocity[2],self->client->ps.fd.forcePowerLevel[kind],
            defender.fireCounter,kind==FP_PULL,v);
        defender.fireModelH=(float)sqrt(v[0]*v[0]+v[1]*v[1]);
    }
    defender.predictedMs=predictedMs;VectorCopy(predictedEnd,defender.predictedEnd);
    defender.fireCarrier=!strcmp(reason,"carrier");
    defender.hit=0;defender.outcomeDone=0;defender.landed=0;defender.airFrames=defender.airInputFrames=0;defender.throws++;
    trap->Print("[DEFENDER] THROW level=%d role=%s kind=%s reason=%s react_ms=%d seen_level=%d distance=%.1f runner=%.1f,%.1f,%.1f runner_v=%.1f,%.1f,%.1f predicted_ms=%d predicted_end=%.1f,%.1f,%.1f baseline=%d escapes=%d mode=%d carrying=%d chasing=%d force=%d since_shot=%d throws=%d replaced=%d self_air=%d self_speed=%.0f\n",
        level.time,roleNames[defender.role],kind==FP_PULL?"pull":"push",reason,react,seen->time,Distance(self->client->ps.origin,seen->origin),
        seen->origin[0],seen->origin[1],seen->origin[2],seen->velocity[0],seen->velocity[1],seen->velocity[2],
        predictedMs,predictedEnd[0],predictedEnd[1],predictedEnd[2],baseline,escapes,mode,
        !!runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG],defender.chasing,
        self->client->ps.fd.forcePower,level.time-defender.lastShot,defender.throws,replaced,
        self->client->ps.groundEntityNum==ENTITYNUM_NONE,
        sqrtf(self->client->ps.velocity[0]*self->client->ps.velocity[0]+self->client->ps.velocity[1]*self->client->ps.velocity[1]));
}

// The carrier pull: the runner carrying its flag between
// bot_defenderPullCarrier and bot_defenderPullMax away; receding faster than
// bot_defenderPullRate when that is set; with bot_defenderComboMs only right
// after a shot.
static int CarrierPull(gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd,int react) {
    int minDist=trap->Cvar_VariableIntegerValue("bot_defenderPullCarrier"),combo=trap->Cvar_VariableIntegerValue("bot_defenderComboMs");
    float rate=(float)trap->Cvar_VariableIntegerValue("bot_defenderPullRate"),maxDist=(float)trap->Cvar_VariableIntegerValue("bot_defenderPullMax");
    float dist;vec3_t none={0,0,0};
    if(minDist<=0 || !runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG])return 0;
    if(runner->health<=0 || runner->client->ps.fallingToDeath || !CanThrow(self,FP_PULL))return 0;
    if(combo>0 && defender.weapon && level.time-defender.lastShot>combo)return 0;
    if(maxDist<=0 || maxDist>DEFENDER_PULL_RANGE)maxDist=maxDist<=0?700:DEFENDER_PULL_RANGE;
    dist=Distance(self->client->ps.origin,seen->origin);
    if(dist>=maxDist && defender.role==DROLE_CHASE && dist<min(CvarFloat("bot_chaserPullMax",1000),DEFENDER_PULL_RANGE)) {
        // bot_chaserPullMax D: chase D also pulls a receding carrier out to D
        // (at most the pull's reach) to close on it (using pull to catch up and shoot).
        const defenderSeen_t *prev=Perceived(react+250);
        if(!prev || !prev->visible || seen->time<=prev->time ||
           Distance(self->client->ps.origin,prev->origin)>=dist)return 0;
    } else if(dist<minDist || dist>=maxDist)return 0;
    if(rate>0) {
        const defenderSeen_t *prev=Perceived(react+250);float before,dt;
        if(!prev || !prev->visible || seen->time<=prev->time)return 0;
        before=Distance(self->client->ps.origin,prev->origin);dt=(seen->time-prev->time)/1000.0f;
        if((dist-before)/dt<rate)return 0;
    }
    defender.carrierPulls++;
    Throw(self,runner,seen,cmd,FP_PULL,"carrier",react,0,none,-1,-1,(int)CvarFloat("bot_defender",2));
    return 1;
}

// Chase D's intercept: the carrier's graph route from where it was last seen
// to its own stand, and the first cell on it the chaser reaches first.
// Returns 0 when none (then it tails).
// elapsedS: seconds since the carrier was at `carrier` (bot_chaserInterceptAge,
// default 1). It has moved on along its route since; a point it
// would already have passed is no intercept, and with none left chase D runs for
// the enemy stand, where an unseen carrier waits for its own flag. Without it a
// chase D held for 171 s at an intercept on a route the carrier had finished
// (records/2026-10-03-hud-knowledge.md).
static int InterceptGoal(gentity_t *self,const vec3_t carrier,float carrierSpeed,float elapsedS,vec3_t out,float *tCarrier,float *tMe) {
    static int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
    int from=Graph_Nearest(carrier,96,96),to=Graph_Nearest(defender.enemyStand,128,128),n,i,cost;
    float along=0,vc=max(carrierSpeed,500.0f),vd=CvarFloat("bot_chaserSpeed",450);vec3_t prev,p;
    if(from<0 || to<0)return 0;
    n=Graph_PathTable(from,to,cells,edges,GRAPH_MAX_PATH,&cost);
    VectorCopy(carrier,prev);
    for(i=0;i<n;i++) {
        const graphCell_t *c=Graph_Cell(cells[i]);float me,them;
        VectorSet(p,c->ix*GRAPH_GRID,c->iy*GRAPH_GRID,c->z);
        along+=Distance(prev,p);VectorCopy(p,prev);
        me=Distance(self->client->ps.origin,p)*1.3f/vd+.3f;them=along/vc-elapsedS;
        if(them<0)continue;   // already passed
        if(me>DEFENDER_INTERCEPT_MAX_S)continue;   // a point it cannot reach soon is no intercept
        if(me<=them) {VectorCopy(p,out);*tCarrier=them;*tMe=me;return 1;}
    }
    return 0;
}

// Where the runner is known to be: as perceived (bot_defenderReactMs ago)
// when in sight, or when within bot_defenderHearRange (default 1,500) of the
// defender, heard (footsteps, jumps, Force Speed); NULL otherwise. For
// positioning only: the aim uses sight alone.
static const float *Known(gentity_t *self,const defenderSeen_t *seen) {
    if(!seen)return NULL;
    if(seen->visible || Distance(seen->origin,self->client->ps.origin)<CvarFloat("bot_defenderHearRange",1500))return seen->origin;
    return NULL;
}

// Base D's block (if it is coming on the top route, stand in its way): the runner's graph route to the own stand
// (map knowledge), and the first cell on it between bot_defenderBlockMin and
// bot_defenderBlockMax (default 200 and 700) from the stand that base D
// reaches before the runner; with none, the last such cell (nearest the stand).
static int BlockGoal(gentity_t *self,const vec3_t runner,float runnerSpeed,vec3_t out) {
    static int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
    int from=Graph_Nearest(runner,96,96),to=Graph_Nearest(defender.stand,128,128),n,i,cost,have=0;
    float along=0,vc=max(runnerSpeed,500.0f),vd=CvarFloat("bot_chaserSpeed",450);
    float lo=CvarFloat("bot_defenderBlockMin",200),hi=CvarFloat("bot_defenderBlockMax",700);vec3_t prev,p;
    if(from<0 || to<0)return 0;
    n=Graph_PathTable(from,to,cells,edges,GRAPH_MAX_PATH,&cost);
    VectorCopy(runner,prev);
    for(i=0;i<n;i++) {
        const graphCell_t *c=Graph_Cell(cells[i]);float me,them,ds;
        VectorSet(p,c->ix*GRAPH_GRID,c->iy*GRAPH_GRID,c->z);
        along+=Distance(prev,p);VectorCopy(p,prev);
        ds=Distance(p,defender.stand);
        if(ds>hi)continue;
        if(ds<lo)break;
        me=Distance(self->client->ps.origin,p)*1.3f/vd+.3f;them=along/vc;
        VectorCopy(p,out);have=1;
        if(me<=them)return 1;
    }
    return have;
}

// bot_defenderBlock 1: base D, with its flag home and the runner known within
// bot_defenderBlockRange (default 2,500) of the stand, stands on the block.
// -1 when not blocking (the posts), else 1 while moving, 0 when on it.
static int BlockMove(int clientNum,gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd) {
    const float *known=Known(self,seen),*o=self->client->ps.origin;vec3_t aim={1,0,0},goal;
    if(defender.role!=DROLE_BASE || !CvarFloat("bot_defenderBlock",1) || runner->health<=0 || !known ||
       !FlagHome(self) || Distance(known,defender.stand)>=CvarFloat("bot_defenderBlockRange",2500)) {
        if(defender.blocking)trap->Print("[DEFENDER] BLOCK level=%d end\n",level.time);
        defender.blocking=0;return -1;
    }
    if(!defender.blocking || level.time>=defender.blockNext) {
        defender.blockNext=level.time+500;
        if(BlockGoal(self,known,sqrtf(seen->velocity[0]*seen->velocity[0]+seen->velocity[1]*seen->velocity[1]),goal) &&
           (!defender.blocking || Distance(goal,defender.blockGoal)>=128)) {
            VectorCopy(goal,defender.blockGoal);
            trap->Print("[DEFENDER] BLOCK level=%d goal=%.0f,%.0f,%.0f to_stand=%.0f runner_to_stand=%.0f seen=%d\n",level.time,
                goal[0],goal[1],goal[2],Distance(goal,defender.stand),Distance(known,defender.stand),seen->visible);
        } else if(!defender.blocking)return -1;
    }
    defender.blocking=1;
    if(sqrtf((defender.blockGoal[0]-o[0])*(defender.blockGoal[0]-o[0])+(defender.blockGoal[1]-o[1])*(defender.blockGoal[1]-o[1]))<=DEFENDER_POST_REACHED &&
       fabsf(defender.blockGoal[2]-o[2])<64)return 0;
    DefenderMove(clientNum,self,cmd,defender.blockGoal);
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    return 1;
}

// ------------------------------------------------ learnings from human play

// The own flag on its stand (the base flag entity is drawn only while home).
static qboolean FlagHome(gentity_t *self) {
    gentity_t *flag=Bot2_BaseFlag(self->client->sess.sessionTeam==TEAM_RED?TEAM_RED:TEAM_BLUE);   // the first non-dropped match, cached
    return flag?!(flag->s.eFlags&EF_NODRAW):qfalse;
}

// The last few units onto a point (an item, a mine spot, a flag): the planner arrives at a cell, not a point, so
// inside DEFENDER_ITEM_REACHED the defender walks straight at it, unless a movement sequence holds the frame or
// (keepOut) the walk would cross the enemy flag's keep-out box (StraightOk). 1 when it wrote the command, else 0 and
// the caller moves by DefenderMove. pitchZero levels the view as the item and flag walks always did.
// bot_defenderReachEdge (default 1): these walks replaced the movement's command and with
// it the guest brake, as the saber walk did before bot_defenderMeleeEdge. On, the walk takes the movement's test
// (Move_GuestReachAhead): a chord to the point that is a hazard refusal is not walked straight (DefenderMove
// instead), and floor ending within the stopping distance along the velocity brakes against it. REACH_EDGE logs a
// change of the test's answer.
static int ReachWalk(int clientNum,gentity_t *self,usercmd_t *cmd,const vec3_t to,int keepOut,int pitchZero) {
    playerState_t *ps=&self->client->ps;int edge=0;
    if(Distance(ps->origin,to)>=DEFENDER_ITEM_REACHED || SequenceGuard(clientNum) || (keepOut && !StraightOk(self,to)))return 0;
    if(CvarFloat("bot_defenderReachEdge",1)) {
        edge=Move_GuestReachAhead(self->s.number,to);
        if(edge!=defender.reachEdge && edge)
            trap->Print("[DEFENDER] REACH_EDGE level=%d role=%s kind=%s speed=%.0f x=%.0f y=%.0f z=%.0f to=%.0f,%.0f,%.0f\n",level.time,
                roleNames[defender.role],edge==2?"brake":"refuse",sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]),
                ps->origin[0],ps->origin[1],ps->origin[2],to[0],to[1],to[2]);
        defender.reachEdge=edge;
        if(edge==1)return 0;
    }
    Aim(self,to,cmd);
    if(pitchZero)cmd->angles[PITCH]=ANGLE2SHORT(0)-ps->delta_angles[PITCH];
    cmd->forwardmove=127;cmd->rightmove=0;
    if(edge==2) {   // push against the velocity, as Melee's brake
        float yaw=atan2f(to[1]-ps->origin[1],to[0]-ps->origin[0])*180/(float)M_PI;
        float rel=DEG2RAD(atan2f(ps->velocity[1],ps->velocity[0])*180/(float)M_PI-yaw);
        cmd->forwardmove=(signed char)(-cosf(rel)*127);cmd->rightmove=(signed char)(sinf(rel)*127);
    }
    return 1;
}

// The own flag lying dropped (a killed carrier's), or NULL.
static gentity_t *DroppedFlag(gentity_t *self) {
    gentity_t *flag=NULL;
    while((flag=Bot2_NextFlag(self->client->sess.sessionTeam==TEAM_RED?TEAM_RED:TEAM_BLUE,flag))!=NULL)
        if((flag->flags&FL_DROPPED_ITEM) && flag->inuse && flag->r.contents)return flag;
    return NULL;
}

// bot_chaserReturn (default 1: when the carrier is killed the
// flag drops to the ground and must be returned to the stand): chase D goes to
// its dropped flag and touches it, which returns it (Team_TouchOurFlag)
// before the runner can take it again or the 40 s auto-return. 1 while going.
// bot_defenderDropReach (default 1): the runner takes a dropped flag only when
// Bot2_ReachableDrop finds it on the graph with no hazard on it, the camper only when it has a cell; chase D took any.
// A flag at the bottom of a pit was a no-path goal held until its native return, outranking every other errand.
// On, chase D and the camper leave a dropped flag that lies
// at rest with no cell or a hazard on it (Bot2_DropOutOfReach: the first two arms left flags in flight, pending ones
// and ones a chase D in the air could not route to, 20-49 skips a Kejim world); RETURN_SKIP logs a flag it leaves.
static int ReturnFlag(int clientNum,gentity_t *self,usercmd_t *cmd) {
    gentity_t *flag;vec3_t aim={1,0,0},to;
    if((flag=defender.role==DROLE_CHASE?DroppedFlag(self):NULL)!=NULL && CvarFloat("bot_defenderDropReach",1) &&
       Bot2_DropOutOfReach(clientNum,flag,self->client->sess.sessionTeam)) {
        if(defender.returnSkip!=flag->s.number)
            trap->Print("[DEFENDER] RETURN_SKIP level=%d flag=%.0f,%.0f,%.0f distance=%.0f\n",level.time,flag->r.currentOrigin[0],
                flag->r.currentOrigin[1],flag->r.currentOrigin[2],Distance(self->client->ps.origin,flag->r.currentOrigin));
        defender.returnSkip=flag->s.number;flag=NULL;
    } else defender.returnSkip=0;
    if(defender.role!=DROLE_CHASE || !CvarFloat("bot_chaserReturn",1) || flag==NULL) {
        if(defender.returning) {
            defender.returns+=FlagHome(self);
            trap->Print("[DEFENDER] RETURN level=%d done=%s ms=%d returns=%d\n",level.time,FlagHome(self)?"home":"gone",
                level.time-defender.returnSince,defender.returns);
        }
        defender.returning=0;return 0;
    }
    VectorCopy(flag->r.currentOrigin,to);
    if(!defender.returning) {
        defender.returning=1;defender.returnSince=level.time;
        trap->Print("[DEFENDER] RETURN level=%d flag=%.0f,%.0f,%.0f distance=%.0f\n",level.time,to[0],to[1],to[2],
            Distance(self->client->ps.origin,to));
    }
    if(ReachWalk(clientNum,self,cmd,to,1,1)) {
    } else {
        DefenderMove(clientNum,self,cmd,to);
        cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
        if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    }
    return 1;
}

// Loadout entry k: owned with at least three shots of its mode's ammo.
static qboolean Stocked(gentity_t *self,int k) {
    playerState_t *ps=&self->client->ps;
    int w=defender.loadoutCount?defender.loadout[k]:defender.weapon;
    int shot=defender.loadoutCount && defender.loadoutMode[k]==BOT2_AIM_ALT?weaponData[w].altEnergyPerShot:weaponData[w].energyPerShot;
    return (ps->stats[STAT_WEAPONS]&(1<<w)) && ps->ammo[weaponData[w].ammoIndex]>=3*max(shot,1);
}

// Owns a loadout weapon, stocked or not (a fresh spawn owns none).
static qboolean OwnsLoadout(gentity_t *self) {
    int k;
    for(k=0;k<defender.loadoutCount;k++)if(self->client->ps.stats[STAT_WEAPONS]&(1<<defender.loadout[k]))return qtrue;
    return !defender.loadoutCount && defender.weapon && (self->client->ps.stats[STAT_WEAPONS]&(1<<defender.weapon));
}

// Armed: any loadout weapon stocked. A human took whatever was nearest
// (repeater, flechette, rocket); a defender needs one, not all.
static qboolean Armed(gentity_t *self) {
    int k;
    for(k=0;k<defender.loadoutCount || (k==0 && defender.weapon);k++)if(Stocked(self,k))return qtrue;
    return !defender.loadoutCount && !defender.weapon;
}

// Base D laying mines with none left.
static qboolean MinesWanted(gentity_t *self) {
    playerState_t *ps=&self->client->ps;
    return defender.mineSpotCount && trap->Cvar_VariableIntegerValue("bot_defenderMines")>0 && defender.role!=DROLE_CHASE && defender.role!=DROLE_CAMPER &&
        (!(ps->stats[STAT_WEAPONS]&(1<<WP_TRIP_MINE)) || ps->ammo[weaponData[WP_TRIP_MINE].ammoIndex]<1);
}

// Whether it has to go and get something (unarmed, or out of mines).
static qboolean Wants(gentity_t *self,int unused) {
    return !Armed(self) || MinesWanted(self);
}

// Whether an item gives a loadout weapon (or ammo for one) it does not have
// stocked, or the mines it lacks.
static qboolean ItemWanted(gentity_t *self,gentity_t *item) {
    int k,tag;
    if(!item->inuse || item->s.eType!=ET_ITEM || !item->item)return qfalse;   // a tracked drop can expire or be taken and freed
    tag=item->item->giTag;
    if(item->item->giType==IT_WEAPON && tag==WP_TRIP_MINE)return MinesWanted(self);
    if(item->item->giType!=IT_WEAPON && item->item->giType!=IT_AMMO)return qfalse;
    for(k=0;k<defender.loadoutCount || (k==0 && defender.weapon);k++) {
        int w=defender.loadoutCount?defender.loadout[k]:defender.weapon;
        if(Stocked(self,k))continue;
        if(item->item->giType==IT_WEAPON?tag==w:(weaponData[w].ammoIndex==tag && (self->client->ps.stats[STAT_WEAPONS]&(1<<w))))return qtrue;
    }
    if(item->item->giType==IT_AMMO && MinesWanted(self) && (self->client->ps.stats[STAT_WEAPONS]&(1<<WP_TRIP_MINE)) &&
       weaponData[WP_TRIP_MINE].ammoIndex==tag)return qtrue;
    return qfalse;
}

// Where a defender's role keeps it: its post for chase D (the ambush post it
// holds), the stand for the others.
static const float *Home(void) {
    if(defender.role==DROLE_CHASE)return defender.ambushPost>=0?defender.ambush[defender.ambushPost]:defender.posts[0];
    if(defender.role==DROLE_CAMPER)return defender.enemyStand;
    return defender.stand;
}

// The nearest grabbable wanted item within `range` of it and within
// DEFENDER_ITEM_HOME of its home or DEFENDER_ITEM_NEAR of where it is, or -1.
static int NearestItem(gentity_t *self,float range) {
    int i,best=-1;float bestD=1e9f;
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        gentity_t *e=&g_entities[i];float d;
        if(!e->inuse || e->s.eType!=ET_ITEM || !e->item || !e->r.contents)continue;   // taken items have no contents
        if(level.time<defender.rearmSkipUntil && (defender.rearmSkip[i>>5]&(1<<(i&31))))continue;
        if(FallCharged(i))continue;   // a lethal fall was charged to it
        if(!BG_CanItemBeGrabbed(level.gametype,&e->s,&self->client->ps) || !ItemWanted(self,e))continue;
        d=Distance(self->client->ps.origin,e->r.currentOrigin);
        if(d>range)continue;
        if(Distance(Home(),e->r.currentOrigin)>DEFENDER_ITEM_HOME && d>DEFENDER_ITEM_NEAR)continue;   // not across the map
        // A drop to reach it is priced: the falls of the first laps worlds
        // followed walks to the ledges 190 u below the red stand.
        d+=DEFENDER_ITEM_DROP_COST*max(0.0f,self->client->ps.origin[2]-e->r.currentOrigin[2]-64);
        if(d<bestD) {bestD=d;best=i;}
    }
    return best;
}

// The wanted item with the least detour on the way to `goal`, within `detour`.
static int NearestItemOnWay(gentity_t *self,float range,const vec3_t goal,float detour) {
    int i,best=-1;float bestD=1e9f;
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        gentity_t *e=&g_entities[i];float d;
        if(!e->inuse || e->s.eType!=ET_ITEM || !e->item || !e->r.contents)continue;
        if(level.time<defender.rearmSkipUntil && (defender.rearmSkip[i>>5]&(1<<(i&31))))continue;
        if(FallCharged(i))continue;
        if(!BG_CanItemBeGrabbed(level.gametype,&e->s,&self->client->ps) || !ItemWanted(self,e))continue;
        if(Distance(self->client->ps.origin,e->r.currentOrigin)>range)continue;
        d=Detour(self,e->r.currentOrigin,goal)+DEFENDER_ITEM_DROP_COST*max(0.0f,self->client->ps.origin[2]-e->r.currentOrigin[2]-64);
        if(d<=detour && d<bestD) {bestD=d;best=i;}
    }
    return best;
}

// The extra walk an item costs on the way to `goal`.
static float Detour(gentity_t *self,const vec3_t item,const vec3_t goal) {
    return Distance(self->client->ps.origin,item)+Distance(item,goal)-Distance(self->client->ps.origin,goal);
}

// Walks to the nearest wanted item; 1 while rearming. With `way` (a chase),
// bot_chaserRearmDetour (default 600: a weapon is good if it is
// found along the way): only an item at most that much off
// the way to `way`, else it chases unarmed. 0 takes any item, as before.
static int NearestItemOnWay(gentity_t *self,float range,const vec3_t goal,float detour);
static int Rearm(int clientNum,gentity_t *self,usercmd_t *cmd,const float *way) {
    gentity_t *item=NULL;vec3_t aim={1,0,0},to;int want=Wants(self,0),topup=0;
    float detour=way?CvarFloat("bot_chaserRearmDetour",600):0;
    if(!want && defender.rearmItem<0) {
        // Armed: a loadout weapon it lacks this near is still worth the walk.
        int near=NearestItem(self,DEFENDER_ITEM_TOPUP);
        if(near>=0) {want=1;topup=1;}
    }
    if(defender.rearmItem>=0 && !want && ItemWanted(self,&g_entities[defender.rearmItem]) &&
       Distance(self->client->ps.origin,g_entities[defender.rearmItem].r.currentOrigin)<DEFENDER_ITEM_TOPUP)want=1;
    if(defender.rearmItem>=0) {
        item=&g_entities[defender.rearmItem];
        if(!item->inuse || !item->r.contents || !want || !ItemWanted(self,item)) {
            if(want==0 || !item->r.contents) {
                defender.pickups+=!item->r.contents;
                trap->Print("[DEFENDER] REARM level=%d role=%s done=%s item=%d ms=%d pickups=%d weapons=%d\n",level.time,roleNames[defender.role],
                    item->r.contents?"dropped":"taken",defender.rearmItem,level.time-defender.rearmSince,defender.pickups,self->client->ps.stats[STAT_WEAPONS]);
            }
            defender.rearmItem=-1;
        } else if(level.time-defender.rearmSince>DEFENDER_ITEM_GIVEUP_MS) {
            trap->Print("[DEFENDER] REARM level=%d role=%s done=giveup item=%d\n",level.time,roleNames[defender.role],defender.rearmItem);
            if(level.time>=defender.rearmSkipUntil)memset(defender.rearmSkip,0,sizeof(defender.rearmSkip));
            defender.rearmSkip[defender.rearmItem>>5]|=1<<(defender.rearmItem&31);defender.rearmSkipUntil=level.time+DEFENDER_ITEM_SKIP_MS;
            defender.rearmItem=-1;
        }
    }
    if(!want)return 0;
    if(defender.rearmItem>=0 && detour>0 && Detour(self,g_entities[defender.rearmItem].r.currentOrigin,way)>detour)defender.rearmItem=-1;
    if(defender.rearmItem<0) {
        defender.rearmItem=detour>0?NearestItemOnWay(self,4000,way,detour):NearestItem(self,topup?DEFENDER_ITEM_TOPUP:4000);
        if(defender.rearmItem<0)return 0;
        defender.rearmSince=level.time;item=&g_entities[defender.rearmItem];
        trap->Print("[DEFENDER] REARM level=%d role=%s item=%d class=%s x=%.0f y=%.0f z=%.0f distance=%.0f\n",level.time,roleNames[defender.role],
            defender.rearmItem,item->classname,item->r.currentOrigin[0],item->r.currentOrigin[1],item->r.currentOrigin[2],
            Distance(self->client->ps.origin,item->r.currentOrigin));
    }
    item=&g_entities[defender.rearmItem];VectorCopy(item->r.currentOrigin,to);
    if(ReachWalk(clientNum,self,cmd,to,1,1)) {   // the planner arrives at a cell: the last few units are walked straight
    } else {
        DefenderMove(clientNum,self,cmd,to);
        cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
        if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    }
    return 1;
}

// ------------------------------------------------ bot_defenderRearmPlan

#define RP_MAX_CANDIDATES 8   // the route-cost query is the expensive part; only the this-many nearest pay for it
#define RP_HYSTERESIS_MS 1500 // a new item must be this much better to take over an already-held one
#define RP_EVAL_MS 500        // candidates are re-scored at most this often
#define RP_STALE_MS 2000      // a known respawn this far past due with the item still gone is forgotten (wrong, or wait -1)

// Whether `self` can see `item`'s own position (PVS, then a trace, as Visible
// does for the runner): used only to learn a respawn timer, not to aim or move.
static qboolean ItemVisible(gentity_t *self,gentity_t *item) {
    vec3_t eye;trace_t tr;
    VectorCopy(self->client->ps.origin,eye);eye[2]+=self->client->ps.viewheight;
    if(!trap->InPVS(eye,item->r.currentOrigin))return qfalse;
    trap->Trace(&tr,eye,NULL,NULL,item->r.currentOrigin,self->s.number,MASK_SHOT,qfalse,0,0);
    return tr.fraction>=1 || tr.entityNum==item->s.number;
}

// The route time in ms from `from` to `to`: the shared graph's own search
// cost (Graph_Path, bot2_graph.c; the same query chase D's INTERCEPT and base
// D's BLOCK already pay for) when both ends are on the graph, else distance
// at 250 u/s, as Rearm's greedy pick effectively assumed throughout.
static int RouteMs(const vec3_t from,const vec3_t to) {
    static int cells[GRAPH_MAX_PATH],edges[GRAPH_MAX_PATH];
    int a=Graph_Nearest(from,96,96),b=Graph_Nearest(to,96,96),cost;
    if(a>=0 && b>=0 && Graph_PathTable(a,b,cells,edges,GRAPH_MAX_PATH,&cost)>0)return cost;
    return (int)(Distance(from,to)/250.0f*1000.0f);
}

// Learns respawn timers: every frame, any item that goes from present to
// taken is a known timer (ITEM_SEEN_TAKEN) when `self` just took it itself or
// had line of sight to it the frame it vanished, read straight off the item
// entity's own ent->nextthink - the exact time Touch_Item (g_items.c) schedules
// RespawnItem for, after pricing the take under the game's own respawn rules
// (RESPAWN_AMMO/RESPAWN_TEAM_WEAPON, g_weaponRespawn, adjustRespawnTime's
// g_adaptRespawn scaling, ent->wait/ent->random overrides: Pickup_Weapon,
// Pickup_Ammo, Touch_Item) - the authoritative value, not a reconstruction of
// it. Unseen, the item is unknown (unavailable) until it is seen present
// again. A dropped item taken is freed outright (no nextthink, no respawn):
// it is simply gone, as a dropped item always was to Rearm.
static void RearmObserve(gentity_t *self) {
    int i;
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        gentity_t *e=&g_entities[i];int bit=1<<(i&31),idx=i>>5;qboolean was,present;
        if(!e->inuse || e->s.eType!=ET_ITEM || !e->item)continue;
        was=!!(defender.rpPresent[idx]&bit);present=e->r.contents!=0;
        if(present) {
            defender.rpPresent[idx]|=bit;defender.rpUnknown[idx]&=~bit;defender.rpRespawnAt[i]=0;
            continue;
        }
        defender.rpPresent[idx]&=~bit;
        if(was) {
            qboolean self_took=Distance(self->client->ps.origin,e->r.currentOrigin)<DEFENDER_ITEM_REACHED;
            if((self_took || ItemVisible(self,e)) && e->nextthink>level.time) {
                defender.rpRespawnAt[i]=e->nextthink;defender.rpUnknown[idx]&=~bit;
                trap->Print("[DEFENDER] ITEM_SEEN_TAKEN level=%d role=%s item=%d respawn_ms=%d by=%s\n",level.time,
                    roleNames[defender.role],i,e->nextthink-level.time,self_took?"self":"seen");
            } else {
                defender.rpUnknown[idx]|=bit;defender.rpRespawnAt[i]=0;
            }
        } else if(defender.rpRespawnAt[i] && level.time>defender.rpRespawnAt[i]+RP_STALE_MS) {
            // Due back and still gone (picked up again before arrival, or a
            // wait -1 item that never returns): the timer was wrong, forget it.
            defender.rpUnknown[idx]|=bit;defender.rpRespawnAt[i]=0;
        }
    }
}

// usable(item) = max(eta, wait): eta the route time (RouteMs), or in chase
// mode (`way` set) the extra time the detour via the item costs over going
// straight for `way`; wait how long a known-respawning item (rpRespawnAt) is
// still away, 0 for one present now.
// direct: RouteMs(self, way), the same for every candidate of one RearmBest, so it is computed there once
// (records/2026-10-03-perf-lossless.md); unused without `way`.
static void RearmItemCost(gentity_t *self,int idx,const float *way,float direct,int *outEta,int *outWait,int *outUsable) {
    gentity_t *e=&g_entities[idx];int eta,wait;
    if(way) {
        float detourMs=RouteMs(self->client->ps.origin,e->r.currentOrigin)+RouteMs(e->r.currentOrigin,way)-direct;
        eta=(int)(detourMs<0?0:detourMs);
    } else eta=RouteMs(self->client->ps.origin,e->r.currentOrigin);
    wait=e->r.contents?0:max(0,defender.rpRespawnAt[idx]-level.time);
    *outEta=eta;*outWait=wait;*outUsable=max(eta,wait);
}

// The soonest-usable wanted item within `range` (today's filters: ItemWanted,
// BG_CanItemBeGrabbed, fallUntil, rearmSkip, home/near radius; `way` set also
// bounds a chase-mode detour exactly as NearestItemOnWay does), honoring the
// teammate's claim (Mate(slot)->rpItem) unless this defender is more
// than RP_HYSTERESIS_MS earlier. Up to RP_MAX_CANDIDATES items, nearest by
// straight line first, pay for the route-cost query. Returns the item index
// or -1; *eta/*wait/*usable/*alternatives/*teammateItem describe the winner
// (usable = max(eta, wait) - eta alone for a present item, wait=0).
// existsOnly (the armed top-up check, which reads only whether an item qualifies): the first candidate that is
// not the teammate's claim qualifies whatever its cost, so it returns that one without routing; only the claimed
// item, whose hysteresis test needs its cost, is routed. The outputs are then not the best item's. The first
// route query a full pass would make expires timed-off edges (Graph_Path's OffExpire) and prints edge_back; the
// short pass makes the same call at the same point (Graph_Generation), so the world and the log are unchanged.
static int RearmBest(gentity_t *self,float range,const float *way,int slot,
                      int *outEta,int *outWait,int *outUsable,int *outAlts,int *outTeammate,int existsOnly) {
    int cand[RP_MAX_CANDIDATES],candCount=0,i,k,best=-1,bestUsable=0,bestEta=0,bestWait=0,alts=0;
    float candD[RP_MAX_CANDIDATES];
    float detourCap=way?CvarFloat("bot_chaserRearmDetour",600):0;
    const defenderState_t *mate=slot>=0?Mate(slot):NULL;
    *outTeammate=mate && mate->armed && mate->rpItem>=0?mate->rpItem:-1;
    for(i=MAX_CLIENTS;i<level.num_entities;i++) {
        gentity_t *e=&g_entities[i];float d;qboolean present;
        if(!e->inuse || e->s.eType!=ET_ITEM || !e->item)continue;
        if(FallCharged(i))continue;
        if(level.time<defender.rearmSkipUntil && (defender.rearmSkip[i>>5]&(1<<(i&31))))continue;
        present=e->r.contents!=0;
        if(!present && (defender.rpUnknown[i>>5]&(1<<(i&31)) || !defender.rpRespawnAt[i]))continue;   // unseen taken: unavailable
        if(!BG_CanItemBeGrabbed(level.gametype,&e->s,&self->client->ps) || !ItemWanted(self,e))continue;
        d=Distance(self->client->ps.origin,e->r.currentOrigin);
        if(d>range)continue;
        if(Distance(Home(),e->r.currentOrigin)>DEFENDER_ITEM_HOME && d>DEFENDER_ITEM_NEAR)continue;
        if(way && detourCap>0 && Detour(self,e->r.currentOrigin,way)>detourCap)continue;
        // Keep the RP_MAX_CANDIDATES nearest by straight line; the route
        // query below is paid only for those.
        for(k=0;k<candCount && candD[k]<=d;k++);
        if(k<RP_MAX_CANDIDATES) {
            int m=min(candCount,RP_MAX_CANDIDATES-1);
            for(;m>k;m--) {cand[m]=cand[m-1];candD[m]=candD[m-1];}
            cand[k]=i;candD[k]=d;if(candCount<RP_MAX_CANDIDATES)candCount++;
        }
    }
    if(existsOnly) {
        // Whether any candidate is wanted: an unclaimed one at once; the teammate's claimed item only when this
        // defender would clearly beat its claim, a route query, so that verdict is kept for RP_EVAL_MS as the
        // full evaluation is (it ran every frame).
        *outEta=*outWait=*outUsable=*outAlts=0;
        if(candCount)Graph_Generation();
        for(k=0;k<candCount;k++)if(!(mate && mate->armed && mate->rpItem==cand[k]))return cand[k];
        if(!candCount || level.time<defender.rpClaimedCheckAt)return -1;
        defender.rpClaimedCheckAt=level.time+RP_EVAL_MS;
        for(k=0;k<candCount;k++) {
            int eta,wait,usable;
            RearmItemCost(self,cand[k],way,0,&eta,&wait,&usable);
            if(usable+RP_HYSTERESIS_MS<mate->rpUsableMs) {defender.rpClaimedCheckAt=0;return cand[k];}
        }
        return -1;
    }
    {
    float direct=way && candCount?RouteMs(self->client->ps.origin,way):0;
    for(k=0;k<candCount;k++) {
        int eta,wait,usable;
        RearmItemCost(self,cand[k],way,direct,&eta,&wait,&usable);
        if(mate && mate->armed && mate->rpItem==cand[k] && !(usable+RP_HYSTERESIS_MS<mate->rpUsableMs))continue;   // claimed, not clearly beaten
        alts++;
        if(best<0 || usable<bestUsable) {best=cand[k];bestUsable=usable;bestEta=eta;bestWait=wait;}
    }
    }
    *outEta=bestEta;*outWait=bestWait;*outUsable=bestUsable;*outAlts=alts;
    return best;
}

static int RearmPlan(int clientNum,gentity_t *self,usercmd_t *cmd,const float *way);

// bot_defenderRearmPlan 0: Rearm, byte-identical to before. 1 (default): RearmPlan.
static int RearmDispatch(int clientNum,gentity_t *self,usercmd_t *cmd,const float *way) {
    return CvarFloat("bot_defenderRearmPlan",1)?RearmPlan(clientNum,self,cmd,way):Rearm(clientNum,self,cmd,way);
}

static void RearmPlanClear(void) {
    defender.rpItem=-1;defender.rpClaimItem=-1;defender.rpWaiting=0;
}

// bot_defenderRearmPlan 1: Rearm's route-time, respawn-aware replacement.
// Same signature, same filters and the same arrival/pickup tail (walk
// straight on within DEFENDER_ITEM_REACHED, else DefenderMove); only the
// item choice differs (RearmBest, above) and the two declared defenders
// share item claims (rpItem) so they stop racing each other for one pickup.
static int RearmPlan(int clientNum,gentity_t *self,usercmd_t *cmd,const float *way) {
    int slot=DefenderSlot(clientNum),want,topup=0;gentity_t *item;vec3_t to;
    if(slot<0)return 0;
    RearmObserve(self);
    want=Wants(self,0);
    if(!want && defender.rpItem<0) {
        int eta,wait,usable,alts,teammateItem;
        if(RearmBest(self,DEFENDER_ITEM_TOPUP,NULL,slot,&eta,&wait,&usable,&alts,&teammateItem,1)>=0) {want=1;topup=1;}
    }
    if(defender.rpItem>=0 && !want && ItemWanted(self,&g_entities[defender.rpItem]) &&
       Distance(self->client->ps.origin,g_entities[defender.rpItem].r.currentOrigin)<DEFENDER_ITEM_TOPUP)want=1;
    if(defender.rpItem>=0) {
        qboolean present;
        item=&g_entities[defender.rpItem];
        present=item->inuse && item->r.contents!=0;
        if(present && defender.rpWaiting) {   // there to be grabbed: no longer a respawn wait
            defender.rpWaiting=0;defender.rpDeadline=level.time+max(DEFENDER_ITEM_GIVEUP_MS,2*defender.rpEtaMs);
        }
        // Unlike Rearm's rearmItem (always chosen present), rpItem may be a
        // known-respawning item chosen on purpose while absent (rpWaiting):
        // its contents==0 is the held state, not a "someone took it" signal.
        // "Taken" is a present target's contents falling to 0 (walked onto
        // it, or it beat us to it); "dropped" is a present target no longer
        // wanted or grabbable; neither applies while legitimately waiting.
        if(!item->inuse || (present && (!want || !ItemWanted(self,item))) || (!present && !defender.rpWaiting)) {
            if(want==0 || !present) {
                defender.pickups+=!present;
                trap->Print("[DEFENDER] REARM level=%d role=%s done=%s item=%d ms=%d pickups=%d weapons=%d\n",level.time,roleNames[defender.role],
                    present?"dropped":"taken",defender.rpItem,level.time-defender.rpSince,defender.pickups,self->client->ps.stats[STAT_WEAPONS]);
            }
            RearmPlanClear();
        } else if(!defender.rpWaiting && level.time>defender.rpDeadline) {
            // The giveup clock only runs while walking to a present item: a
            // known respawn can legitimately be waited on far longer. It
            // scales with the planned trip, fixed when the target is chosen and
            // restarted when a waited-for item comes back (the eta and wait are
            // refreshed every evaluation and shrink): base D's only mine source
            // is 7-9 s away by route and the flat 8 s gave it up on the way
            // (integ-20261001-a, keep2-20261001-a).
            trap->Print("[DEFENDER] REARM level=%d role=%s done=giveup item=%d\n",level.time,roleNames[defender.role],defender.rpItem);
            if(level.time>=defender.rearmSkipUntil)memset(defender.rearmSkip,0,sizeof(defender.rearmSkip));
            defender.rearmSkip[defender.rpItem>>5]|=1<<(defender.rpItem&31);defender.rearmSkipUntil=level.time+DEFENDER_ITEM_SKIP_MS;
            RearmPlanClear();
        }
    }
    if(!want) {RearmPlanClear();return 0;}
    // The chase's fallback anywhere (bot_chaserRearmAny) is asked in the same frame as the evaluation along the way
    // that found nothing, so it evaluates too; the throttle had refused it every time.
    if(level.time>=defender.rpNextEval || (!way && defender.rpWayEvalAt==level.time+1)) {
        int eta,wait,usable,alts,teammateItem,bestItem;
        defender.rpNextEval=level.time+RP_EVAL_MS;defender.rpWayEvalAt=way?level.time+1:0;
        bestItem=RearmBest(self,topup?DEFENDER_ITEM_TOPUP:4000,way,slot,&eta,&wait,&usable,&alts,&teammateItem,0);
        if(defender.rpItem>=0) {
            const defenderState_t *mate=Mate(slot);qboolean impossible=qfalse;
            int curEta,curWait,curUsable;
            // Refresh the held item's own estimate (it gets closer every
            // tick) so the comparison, and what the teammate reads, stay current.
            RearmItemCost(self,defender.rpItem,way,way?RouteMs(self->client->ps.origin,way):0,&curEta,&curWait,&curUsable);
            defender.rpEtaMs=curEta;defender.rpWaitMs=curWait;defender.rpUsableMs=curUsable;defender.rpClaimUsableMs=curUsable;
            defender.rpWaiting=g_entities[defender.rpItem].r.contents==0;
            if(mate->armed && mate->rpItem==defender.rpItem && mate->rpUsableMs+RP_HYSTERESIS_MS<curUsable)impossible=qtrue;
            if(impossible || (bestItem>=0 && bestItem!=defender.rpItem && usable+RP_HYSTERESIS_MS<curUsable)) {
                trap->Print("[DEFENDER] REARM_PLAN level=%d role=%s item=%d class=%s eta_ms=%d wait_ms=%d usable_ms=%d reason=%s alternatives=%d teammate_item=%d\n",
                    level.time,roleNames[defender.role],bestItem,bestItem>=0?g_entities[bestItem].classname:"none",eta,wait,usable,
                    impossible?"impossible":"better",alts,teammateItem);
                if(bestItem>=0) {
                    defender.rpItem=bestItem;defender.rpSince=level.time;defender.rpEtaMs=eta;defender.rpWaitMs=wait;defender.rpUsableMs=usable;
            defender.rpDeadline=level.time+max(DEFENDER_ITEM_GIVEUP_MS,2*eta+wait);
                    defender.rpWaiting=g_entities[bestItem].r.contents==0;defender.rpClaimItem=bestItem;defender.rpClaimUsableMs=usable;
                } else RearmPlanClear();
            }
        } else if(bestItem>=0) {
            trap->Print("[DEFENDER] REARM_PLAN level=%d role=%s item=%d class=%s eta_ms=%d wait_ms=%d usable_ms=%d reason=init alternatives=%d teammate_item=%d\n",
                level.time,roleNames[defender.role],bestItem,g_entities[bestItem].classname,eta,wait,usable,alts,teammateItem);
            defender.rpItem=bestItem;defender.rpSince=level.time;defender.rpEtaMs=eta;defender.rpWaitMs=wait;defender.rpUsableMs=usable;
            defender.rpDeadline=level.time+max(DEFENDER_ITEM_GIVEUP_MS,2*eta+wait);
            defender.rpWaiting=g_entities[bestItem].r.contents==0;defender.rpClaimItem=bestItem;defender.rpClaimUsableMs=usable;
        }
    }
    if(defender.rpItem<0)return 0;
    item=&g_entities[defender.rpItem];VectorCopy(item->r.currentOrigin,to);
    if(ReachWalk(clientNum,self,cmd,to,1,1)) {
    } else {
        DefenderMove(clientNum,self,cmd,to);
        cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
        if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    }
    return 1;
}

// The mean distance from the team's respawn points (CTF respawns pick one of
// them at random) to a point.
static float SpawnDistance(gentity_t *self,const float *point) {
    gentity_t *spot=NULL;float sum=0;int n=0;
    while((spot=G_Find(spot,FOFS(classname),self->client->sess.sessionTeam==TEAM_RED?"team_CTF_redspawn":"team_CTF_bluespawn"))!=NULL) {
        sum+=Distance(spot->s.origin,point);n++;
    }
    return n?sum/n:1e9f;
}

// The self-kill reset; 1 when it killed itself. Only when a respawn is
// expected to put it much nearer its home than it is (Kejim's respawn points
// spread over the team's half; the first laps worlds reset chase D 24 times
// in ten minutes, mostly for no gain, and it fell walking back from them).
static int Reset(gentity_t *self,gentity_t *runner) {
    float far=CvarFloat("bot_defenderResetDist",1800),away,spawn;
    if(level.time-defender.resetTime<DEFENDER_RESET_MS || level.time-defender.spawnTime<CvarFloat("bot_defenderResetSpawnMs",2000) ||
       !FlagHome(self))return 0;
    if(runner->health>0 && Distance(runner->client->ps.origin,defender.stand)<DEFENDER_RESET_CLEAR)return 0;
    away=Distance(self->client->ps.origin,Home());
    if(away<=far && !(away>1000 && Wants(self,0) && NearestItem(self,4000)<0))return 0;
    spawn=SpawnDistance(self,Home());
    if(spawn>=away*.6f)return 0;
    defender.resets++;defender.resetTime=level.time;
    trap->Print("[DEFENDER] RESET level=%d role=%s away=%.0f spawn=%.0f wants=%d runner_alive=%d runner_to_stand=%.0f resets=%d\n",level.time,
        roleNames[defender.role],away,spawn,Wants(self,0),runner->health>0,Distance(runner->client->ps.origin,defender.stand),defender.resets);
    G_Kill(self);
    return 1;
}

// A live own proximity mine covering the spot.
static qboolean MineCovers(gentity_t *self,const vec3_t spot) {
    gentity_t *m=NULL;
    while((m=G_Find(m,FOFS(classname),"laserTrap"))!=NULL)
        if(m->parent==self && !m->count && Distance(m->r.currentOrigin,spot)<DEFENDER_MINE_COVER)return qtrue;
    return qfalse;
}

// Base D lays proximity mines on its spots; 1 while it is doing so.
static int LayMines(int clientNum,gentity_t *self,gentity_t *runner,usercmd_t *cmd) {
    playerState_t *ps=&self->client->ps;int want=trap->Cvar_VariableIntegerValue("bot_defenderMines"),i,covered=0,best=-1;
    float bestD=1e9f;vec3_t aim={1,0,0};int ammo=ps->ammo[weaponData[WP_TRIP_MINE].ammoIndex];
    if(want<=0 || !defender.mineSpotCount || defender.role==DROLE_CHASE)return 0;
    if(defender.minePress) {   // a placement pressed: confirmed by the ammo, else dropped after a second
        if(ammo<defender.mineAmmo) {
            defender.mines++;
            trap->Print("[DEFENDER] MINE level=%d spot=%d x=%.0f y=%.0f z=%.0f ms=%d mines=%d ammo=%d\n",level.time,defender.mineSpot,
                ps->origin[0],ps->origin[1],ps->origin[2],level.time-defender.mineSince,defender.mines,ammo);
            defender.minePress=0;defender.mineSpot=-1;
        } else if(level.time-defender.minePress>1000)defender.minePress=0;
        else {cmd->weapon=WP_TRIP_MINE;return 1;}
    }
    if(!(ps->stats[STAT_WEAPONS]&(1<<WP_TRIP_MINE)) || ammo<1 || !FlagHome(self) ||
       (runner->health>0 && Distance(runner->client->ps.origin,defender.stand)<DEFENDER_MINE_CLEAR)) {defender.mineSpot=-1;return 0;}
    if(defender.mineSpot>=0 && (MineCovers(self,defender.mineSpots[defender.mineSpot]) ||
       level.time-defender.mineSince>DEFENDER_MINE_GIVEUP_MS)) {
        if(!MineCovers(self,defender.mineSpots[defender.mineSpot])) {
            defender.mineSkipUntil[defender.mineSpot]=level.time+10000;
            trap->Print("[DEFENDER] MINE level=%d spot=%d giveup\n",level.time,defender.mineSpot);
        }
        defender.mineSpot=-1;
    }
    if(defender.mineSpot<0) {
        for(i=0;i<defender.mineSpotCount;i++) {
            float d;
            if(MineCovers(self,defender.mineSpots[i])) {covered++;continue;}
            if(level.time<defender.mineSkipUntil[i])continue;
            d=Distance(ps->origin,defender.mineSpots[i]);
            if(d<bestD) {bestD=d;best=i;}
        }
        if(covered>=want || best<0)return 0;
        defender.mineSpot=best;defender.mineSince=level.time;
    }
    {
        const float *spot=defender.mineSpots[defender.mineSpot];
        if(sqrtf((spot[0]-ps->origin[0])*(spot[0]-ps->origin[0])+(spot[1]-ps->origin[1])*(spot[1]-ps->origin[1]))>32) {
            // Only the mine-spot walk skipped the keep-out test; it takes it with the edge test.
            if(ReachWalk(clientNum,self,cmd,spot,CvarFloat("bot_defenderReachEdge",1)!=0,0)) {}
            else {
                DefenderMove(clientNum,self,cmd,spot);
                cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
                if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
            }
            cmd->weapon=WP_TRIP_MINE;return 1;
        }
    }
    // On the spot: trip mine out, look at the floor, alt fire once. A
    // committed sequence still flying this close to the spot (bot_defender-
    // KeepSequence) keeps its own move; the mine waits a frame.
    if(SequenceGuard(clientNum))return 1;
    cmd->weapon=WP_TRIP_MINE;cmd->forwardmove=cmd->rightmove=0;
    cmd->angles[PITCH]=ANGLE2SHORT(80)-ps->delta_angles[PITCH];
    if(ps->weapon==WP_TRIP_MINE && ps->weaponstate==WEAPON_READY && ps->weaponTime<=0 && ps->viewangles[PITCH]>60) {
        cmd->buttons|=BUTTON_ALT_ATTACK;defender.minePress=level.time;defender.mineAmmo=ammo;
    }
    return 1;
}

// Chase D's ambush post while its flag is home: the one farthest from the
// own stand (the runner meets it first) that the runner has not yet passed
// and the chaser reaches first, else its own post (-1).
static int AmbushChoose(gentity_t *self,gentity_t *runner,const defenderSeen_t *seen) {
    int i,best=-1;float bestAhead=-1,runnerLeft,vc,vd=CvarFloat("bot_chaserSpeed",450);
    const float *from=seen && seen->visible?seen->origin:defender.lastSeen;
    if(runner->health<=0 || !defender.chaseSeenTime)from=defender.enemyStand;   // it comes from its base
    runnerLeft=Distance(from,defender.stand);
    vc=seen && seen->visible?max(sqrtf(seen->velocity[0]*seen->velocity[0]+seen->velocity[1]*seen->velocity[1]),500.0f):500.0f;
    // The post it holds is kept until the runner is well past it: the throw
    // comes as the runner crosses by (the first laps worlds left each post
    // just as the runner reached it).
    if(defender.ambushPost>=0 && runner->health>0 &&
       Distance(defender.ambush[defender.ambushPost],defender.stand)<runnerLeft+500)return defender.ambushPost;
    for(i=0;i<defender.ambushCount;i++) {
        float postLeft=Distance(defender.ambush[i],defender.stand),me,them;
        if(postLeft>runnerLeft-300)continue;   // passed, or it is on it
        me=Distance(self->client->ps.origin,defender.ambush[i])*1.3f/vd+.3f;
        them=runner->health<=0?99:(runnerLeft-postLeft)/vc;
        if(me>them)continue;
        if(postLeft>bestAhead) {bestAhead=postLeft;best=i;}
    }
    return best;
}

// Moves chase D to its ambush post; 1 while it holds one.
static int AmbushMove(int clientNum,gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd) {
    vec3_t aim={1,0,0};const float *p,*o=self->client->ps.origin;
    if(defender.role!=DROLE_CHASE || !defender.ambushCount || !trap->Cvar_VariableIntegerValue("bot_chaserAmbush"))return 0;
    if(level.time>=defender.ambushNext) {
        int pick=AmbushChoose(self,runner,seen);
        defender.ambushNext=level.time+500;
        if(pick!=defender.ambushPost) {
            trap->Print("[DEFENDER] AMBUSH level=%d post=%d x=%.0f y=%.0f z=%.0f runner_alive=%d runner_to_stand=%.0f\n",level.time,pick,
                pick>=0?defender.ambush[pick][0]:defender.posts[0][0],pick>=0?defender.ambush[pick][1]:defender.posts[0][1],
                pick>=0?defender.ambush[pick][2]:defender.posts[0][2],runner->health>0,Distance(runner->client->ps.origin,defender.stand));
            defender.ambushPost=pick;
        }
    }
    if(defender.ambushPost<0)return 0;
    p=defender.ambush[defender.ambushPost];
    if(sqrtf((p[0]-o[0])*(p[0]-o[0])+(p[1]-o[1])*(p[1]-o[1]))<=DEFENDER_POST_REACHED && fabsf(p[2]-o[2])<64) {defender.guardMoving=0;return 1;}
    DefenderMove(clientNum,self,cmd,p);
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    defender.guardMoving=1;
    return 1;
}

// bot_chaserPullClose M (L3): for M ms after chase D's carrier pull hits, its
// goal is where the carrier will land (ballistic from its perceived state),
// snapped to that cell, so the exact aim inside bot_aimDirectRange gets its shot.
static void CloseGoal(gentity_t *self,gentity_t *runner,const defenderSeen_t *seen) {
    vec3_t pt;int ms,cell;
    if(!seen)return;
    VectorCopy(seen->origin,pt);
    if(!seen->grounded && (ms=LandMs(runner,seen))>0) {
        float s=ms/1000.0f;
        pt[0]+=seen->velocity[0]*s;pt[1]+=seen->velocity[1]*s;pt[2]+=seen->velocity[2]*s-400*s*s;
    }
    if((cell=Graph_Nearest(pt,128,128))>=0) {
        const graphCell_t *c=Graph_Cell(cell);
        VectorSet(pt,c->ix*GRAPH_GRID,c->iy*GRAPH_GRID,c->z);
    }
    if(!defender.closing)
        trap->Print("[DEFENDER] CLOSE level=%d goal=%.0f,%.0f,%.0f cell=%d distance=%.0f window_ms=%d\n",level.time,pt[0],pt[1],pt[2],cell,
            Distance(self->client->ps.origin,pt),defender.closeUntil-level.time);
    defender.closing=1;
    VectorCopy(pt,defender.chaseGoal);
}

// bot_defenderCarryLog 1: the route diagnostic. Each defender samples every
// DEFENDER_CARRY_SAMPLE_MS of a carry (CARRY_SAMPLE) and sums the carry at
// its end (CARRY): how far it was from the carrier, whether it saw it and
// had it in range, why its last shot decision did not fire, and whether its
// movement was held or airborne.
static void CarryLog(int clientNum,gentity_t *self,gentity_t *runner) {
    int carrying,k;
    if(!CvarFloat("bot_defenderCarryLog",0))return;
    carrying=runner->health>0 && runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG];
    if(carrying && !defender.carryOn) {
        defender.carryOn=1;defender.carryStart=defender.carryNext=level.time;
        defender.carrySamples=defender.carryVisible=defender.carryInRange=defender.carryHold=defender.carryAir=defender.carryDead=0;
        memset(defender.carryPre,0,sizeof(defender.carryPre));defender.carryDistSum=0;defender.carryDistMin=1e9f;
        defender.carryShots=defender.shots;defender.carryPulls=defender.carrierPulls;defender.carryThrows=defender.throws;
        defender.carryHits=defender.damageHits;
    }
    if(!defender.carryOn)return;
    if(!carrying) {
        char line[320]="";
        for(k=0;k<=PRE_REASONS;k++)
            if(defender.carryPre[k])Q_strcat(line,sizeof(line),va(" pre_%s=%d",k<PRE_REASONS?preNames[k]:"none",defender.carryPre[k]));
        trap->Print("[DEFENDER] CARRY level=%d role=%s ms=%d end=%s samples=%d visible=%d in_range=%d hold=%d air=%d dead=%d dist_mean=%.0f dist_min=%.0f shots=%d pulls=%d throws=%d hits=%d%s\n",
            level.time,roleNames[defender.role],level.time-defender.carryStart,runner->health<=0?"killed":FlagHome(self)?"captured":"other",
            defender.carrySamples,defender.carryVisible,defender.carryInRange,defender.carryHold,defender.carryAir,defender.carryDead,
            defender.carrySamples?defender.carryDistSum/defender.carrySamples:0.0f,defender.carrySamples?defender.carryDistMin:0.0f,
            defender.shots-defender.carryShots,defender.carrierPulls-defender.carryPulls,defender.throws-defender.carryThrows,
            defender.damageHits-defender.carryHits,line);
        defender.carryOn=0;return;
    }
    if(level.time>=defender.carryNext) {
        const movement_t *mv=Move_State(clientNum);const float *o=self->client->ps.origin,*r=runner->client->ps.origin;
        int react=trap->Cvar_VariableIntegerValue("bot_defenderReactMs"),hold=mv && mv->declineHold.active;
        int air=self->client->ps.groundEntityNum==ENTITYNUM_NONE,dead=self->health<=0,visible,inRange,pre;
        float dist=Distance(o,r),range=CvarFloat("bot_defenderShootRange",1500);const defenderSeen_t *seen;
        if(range<=0)range=1500;
        seen=Perceived(react<0?0:react>1000?1000:react);
        visible=seen && seen->visible;inRange=visible && dist<range;
        pre=level.time-defender.lastPreTime<=60?defender.lastPre:PRE_REASONS;
        defender.carryNext=level.time+DEFENDER_CARRY_SAMPLE_MS;
        defender.carrySamples++;defender.carryVisible+=visible;defender.carryInRange+=inRange;defender.carryHold+=hold;
        defender.carryAir+=air;defender.carryDead+=dead;defender.carryPre[pre]++;defender.carryDistSum+=dist;
        if(dist<defender.carryDistMin)defender.carryDistMin=dist;
        trap->Print("[DEFENDER] CARRY_SAMPLE level=%d role=%s t=%d distance=%.0f visible=%d in_range=%d pre=%s weap=%s hold=%d air=%d dead=%d chasing=%d closing=%d returning=%d blocking=%d self=%.0f,%.0f,%.0f speed=%.0f runner=%.0f,%.0f,%.0f runner_speed=%.0f goal=%.0f,%.0f,%.0f melee=%s\n",
            level.time,roleNames[defender.role],level.time-defender.carryStart,dist,visible,inRange,pre<PRE_REASONS?preNames[pre]:"none",
            pre==PRE_WEAPON && defender.lastWeap?defender.lastWeap:"-",
            hold,air,dead,defender.chasing,defender.closing,defender.returning,defender.blocking,o[0],o[1],o[2],
            sqrtf(self->client->ps.velocity[0]*self->client->ps.velocity[0]+self->client->ps.velocity[1]*self->client->ps.velocity[1]),r[0],r[1],r[2],
            sqrtf(runner->client->ps.velocity[0]*runner->client->ps.velocity[0]+runner->client->ps.velocity[1]*runner->client->ps.velocity[1]),
            defender.moveGoal[0],defender.moveGoal[1],defender.moveGoal[2],defender.meleeWhy?defender.meleeWhy:"-");
    }
}

static void DefenderCommand(int clientNum,int time,usercmd_t *cmd);
// bot_defenderViewLog 1 (default 0), a diagnostic: a VIEW line for every frame
// whose view turns 8 degrees or more, with what wrote it (SetView calls, movement
// commands), whether it moved or chased, and the frame's shot decision.
void Bot2_DefenderCommand(int clientNum,int time,usercmd_t *cmd) {
    int slot=DefenderSlot(clientNum);gentity_t *self=&g_entities[clientNum];
    if(slot>=0) {defenders[slot].viewSets=defenders[slot].viewMoves=0;}
    DefenderCommand(clientNum,time,cmd);
    if(slot>=0 && defenders[slot].armed && self->health>0 && trap->Cvar_VariableIntegerValue("bot_defenderViewLog")) {
        const defenderState_t *d=&defenders[slot];
        float turn=AngleNormalize180(SHORT2ANGLE(cmd->angles[YAW]+self->client->ps.delta_angles[YAW])-self->client->ps.viewangles[YAW]);
        if(fabsf(turn)>=8)
            trap->Print("[DEFENDER] VIEW level=%d role=%s turn=%.1f sets=%d moves=%d moving=%d chasing=%d pre=%s ground=%d f=%d r=%d\n",level.time,
                roleNames[d->role],turn,d->viewSets,d->viewMoves,d->guardMoving,d->chasing,
                d->lastPreTime==level.time?preNames[d->lastPre]:"none",self->client->ps.groundEntityNum!=ENTITYNUM_NONE,cmd->forwardmove,cmd->rightmove);
    }
}

// Aiming, shooting and throwing at the target, after the frame's movement:
// the splash brace, the idle aim, then a lethal throw, the carrier pull or a
// shot (bot_defender's mode). Shared by every role; the camper's target is an
// enemy defender.
// bot_defenderMelee (default 1): with no stocked weapon and the
// target seen within bot_defenderMeleeRange (default 250), draw the saber, walk
// straight at it while there is floor a step below the next 48 u (traced), and
// swing within 96 u.
// In the 4v4 every bot stood still for seven minutes: both chase D's out of
// ammo beside the enemy carriers, who held on their own stands
// (records/2026-10-02-b3-stalls.md). It walks into the enemy stand's keep-out
// box only while the enemy flag is not on the stand. 1 when it took the frame.
// bot_defenderPistol (default 1: get the pistol online, an
// unlimited-ammo starting weapon; do not shoot at a reflecting saber
// but other gun users (enemy base and chase) are good targets for the
// pistol alt fire): with no stocked loadout weapon, a bot that owns
// the Bryar pistol fires its charged alt (bot_defenderPistolChargeMs, default
// 1,000) at a target not holding an active saber; at one that does, the saber
// fallback (Melee). A stocked loadout weapon takes over again through
// ChooseWeapon (the pistol is not in the loadout, so it scores nothing).
static qboolean PistolOk(gentity_t *self,gentity_t *target) {
    const playerState_t *t=&target->client->ps;
    return CvarFloat("bot_defenderPistol",1) && (self->client->ps.stats[STAT_WEAPONS]&(1<<WP_BRYAR_PISTOL)) &&
           !(t->weapon==WP_SABER && !t->saberHolstered);
}
static void PistolChoose(gentity_t *self,gentity_t *target) {
    int k;
    if(!Armed(self) && PistolOk(self,target)) {
        if(defender.weapon!=WP_BRYAR_PISTOL) {
            defender.pistols++;
            trap->Print("[DEFENDER] PISTOL level=%d role=%s target=%d distance=%.0f from=%d pistols=%d\n",level.time,roleNames[defender.role],
                target->s.number,Distance(self->client->ps.origin,target->r.currentOrigin),defender.weapon,defender.pistols);
            defender.weapon=WP_BRYAR_PISTOL;defender.mode=BOT2_AIM_ALT;defender.switchTime=level.time;
        }
    } else if(defender.weapon==WP_BRYAR_PISTOL) {
        // Back to the loadout: a stocked weapon if there is one, else its first.
        for(k=0;k<defender.loadoutCount;k++)if(Stocked(self,k))break;
        if(k>=defender.loadoutCount)k=0;
        if(defender.loadoutCount) {defender.weapon=defender.loadout[k];defender.mode=defender.loadoutMode[k];defender.switchTime=level.time;}
    }
}

static int Melee(gentity_t *self,gentity_t *target,const defenderSeen_t *seen,usercmd_t *cmd) {
    playerState_t *ps=&self->client->ps;vec3_t delta,ahead;float d,yaw,pitch,horizontal;gentity_t *home;
    defender.meleeWhy=!CvarFloat("bot_defenderMelee",1)?"off":Armed(self)?"armed":PistolOk(self,target)?"pistol":!seen || !seen->visible?"unseen":
        target->health<=0?"dead":ps->groundEntityNum==ENTITYNUM_NONE?"air":!(ps->stats[STAT_WEAPONS]&(1<<WP_SABER))?"nosaber":NULL;
    if(defender.meleeWhy) {defender.meleeOn=defender.meleeEdge=0;return 0;}
    d=Distance(ps->origin,target->r.currentOrigin);
    if(d>=CvarFloat("bot_defenderMeleeRange",250)) {defender.meleeWhy="range";defender.meleeOn=defender.meleeEdge=0;return 0;}
    defender.meleeWhy="on";
    VectorSubtract(target->r.currentOrigin,ps->origin,delta);
    horizontal=sqrtf(delta[0]*delta[0]+delta[1]*delta[1]);
    yaw=atan2f(delta[1],delta[0])*180/(float)M_PI;pitch=-atan2f(delta[2],max(1,horizontal))*180/(float)M_PI;
    VectorSet(ahead,ps->origin[0]+cosf(yaw*(float)M_PI/180)*48,ps->origin[1]+sinf(yaw*(float)M_PI/180)*48,ps->origin[2]);
    home=EnemyFlag(self,qfalse);
    ps->saberHolstered=0;cmd->weapon=WP_SABER;
    cmd->angles[YAW]=ANGLE2SHORT(yaw)-ps->delta_angles[YAW];cmd->angles[PITCH]=ANGLE2SHORT(pitch)-ps->delta_angles[PITCH];
    // Floor ahead by a trace, not the graph: the flag pedestal a carrier holds
    // on is no graph floor, and a graph check stopped the saber 97 u short of it.
    {
        vec3_t top,bottom;trace_t tr;
        VectorSet(top,ahead[0],ahead[1],ahead[2]+STEPSIZE);VectorSet(bottom,ahead[0],ahead[1],ahead[2]-2*STEPSIZE);
        trap->Trace(&tr,top,self->r.mins,self->r.maxs,bottom,self->s.number,MASK_PLAYERSOLID,qfalse,0,0);
        cmd->forwardmove=!tr.startsolid && !tr.allsolid && tr.fraction<1 && tr.plane.normal[2]>=.7f &&
            !(home && InFlagBox(ahead,home->r.currentOrigin,CvarFloat("bot_defenderFlagKeepOutMargin",40)))?127:0;
    }
    cmd->rightmove=cmd->upmove=0;
    // bot_defenderMeleeEdge (default 1; Kejim base D falls -2.06 +- 0.69 a world, chase D -2.12 +- 0.86,
    // records/2026-10-06-base-walkway-goals.md): the movement's guest brake test (Move_GuestEdgeAhead)
    // as well: floor ending along the heading within the stopping distance releases
    // the keys; along the velocity, the keys push against the velocity.
    if(CvarFloat("bot_defenderMeleeEdge",1)) {
        int edge=Move_GuestEdgeAhead(self->s.number,yaw);
        if(edge)cmd->forwardmove=0;
        if(edge==2) {
            float rel=DEG2RAD(atan2f(ps->velocity[1],ps->velocity[0])*180/(float)M_PI-yaw);
            cmd->forwardmove=(signed char)(-cosf(rel)*127);cmd->rightmove=(signed char)(sinf(rel)*127);
        }
        if(edge && !defender.meleeEdge)
            trap->Print("[DEFENDER] MELEE_EDGE level=%d role=%s kind=%s speed=%.0f x=%.0f y=%.0f z=%.0f\n",level.time,roleNames[defender.role],
                edge==2?"brake":"hold",sqrtf(ps->velocity[0]*ps->velocity[0]+ps->velocity[1]*ps->velocity[1]),ps->origin[0],ps->origin[1],ps->origin[2]);
        defender.meleeEdge=edge;
    }
    cmd->buttons&=~(BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(d<96)cmd->buttons|=BUTTON_ATTACK;
    if(!defender.meleeOn) {
        defender.meleeOn=1;defender.melees++;
        trap->Print("[DEFENDER] MELEE level=%d role=%s target=%d distance=%.0f melees=%d\n",level.time,roleNames[defender.role],
            target->s.number,d,defender.melees);
    }
    return 1;
}

static void Engage(gentity_t *self,gentity_t *runner,const defenderSeen_t *seen,usercmd_t *cmd,int react) {
    int horizon,baseline,baseMs,kinds[2]={FP_PULL,FP_PUSH},i,best=-1,bestMs=0,ms,outcome,mode,escapes,bestEscapes=0;
    int starvedKind=-1,starvedEscapes=0;
    vec3_t velocity,end,bestEnd;
    PistolChoose(self,runner);
    if(Melee(self,runner,seen,cmd))return;
    Brace(self,cmd);
    if(defender.guardMoving && cmd->weapon==WP_TRIP_MINE)return;   // laying: no shot, no throw this frame
    if(defender.weapon)cmd->weapon=defender.weapon;
    if(!seen || !seen->visible) {
        // Out of sight: the continuous-visibility engage delay starts over
        // (with bot_aimEngageGraceMs, once the runner has been gone that long).
        if(trap->Cvar_VariableIntegerValue("bot_aimEngageGraceMs")<=0)defender.engageSince=0;
        Bot2_AimHumanReset(self->s.number,self->client->ps.viewangles);
        if(defender.weapon)PreTally(self,runner,PRE_UNSEEN);return;
    }
    if(!defender.chasing && !defender.guardMoving)Aim(self,seen->origin,cmd);
    mode=(int)CvarFloat("bot_defender",2);
    if(runner->health<=0 || runner->client->ps.fallingToDeath)return;
    if(defender.fireTime>=0 && !defender.outcomeDone) {Shoot(self,runner,seen,cmd);return;}   // one throw followed at a time
    if(mode!=1 && mode!=2) {if(!CarrierPull(self,runner,seen,cmd,react))Shoot(self,runner,seen,cmd);return;}
    {
        // Lethal throws only from bot_defenderThrowMin..bot_defenderThrowMax
        // (default 0..1000): throws from 400-600 u killed 50% in the laps
        // worlds, 10-22% from elsewhere (records/2026-09-29-defender-gaps.md).
        int lo=trap->Cvar_VariableIntegerValue("bot_defenderThrowMin"),hi=trap->Cvar_VariableIntegerValue("bot_defenderThrowMax");
        float d=Distance(self->client->ps.origin,seen->origin);
        if(d>=(hi>0?hi:1000) || d<lo) {Shoot(self,runner,seen,cmd);return;}
    }
    horizon=trap->Cvar_VariableIntegerValue("bot_defenderHorizonMs");horizon=horizon<=0?3000:(horizon>6000?6000:horizon);
    baseline=Sweep(runner,seen->origin,seen->velocity,NULL,horizon,end,&baseMs);
    if(baseline==SWEEP_LETHAL) {Shoot(self,runner,seen,cmd);return;}   // already on its way down
    for(i=0;i<2;i++) {
        int starved=0;
        if(!CanThrow(self,kinds[i])) {
            if(!Starved(self,kinds[i]))continue;
            starved=1;
        }
        ThrowVelocity(self->client->ps.origin,seen->origin,seen->velocity[2],self->client->ps.fd.forcePowerLevel[kinds[i]],
            CounterLevel(runner,kinds[i]==FP_PULL,seen->grounded),kinds[i]==FP_PULL,velocity);
        outcome=Sweep(runner,seen->origin,velocity,NULL,horizon,end,&ms);
        if(outcome!=SWEEP_LETHAL)continue;
        // Mode 1 throws on the passive flight and records the escapes; mode 2
        // throws only when no held air input reaches floor, so it needs only
        // zero or not: every count it keeps or prints is 0, and patience
        // below needs bestEscapes>0.
        escapes=Escapes(runner,seen->origin,velocity,horizon,mode==2);
        if(mode==2 && escapes)continue;
        if(starved) {starvedKind=kinds[i];starvedEscapes=escapes;continue;}
        if(best<0 || ms<bestMs) {best=kinds[i];bestMs=ms;bestEscapes=escapes;VectorCopy(end,bestEnd);}
    }
    if(best>=0 && bestEscapes>0 && !seen->grounded && trap->Cvar_VariableIntegerValue("bot_defenderPatience")) {
        // Patience: the runner as it will be two frames on (ballistic), thrown
        // then. Human kills came from throws in mid-gap; the first
        // lethal frame is at the gap's edge, where the runner steers back.
        vec3_t ahead,aheadV,v2,end2;int ms2,esc2;
        VectorMA(seen->origin,.05f,seen->velocity,ahead);ahead[2]-=.5f*800*.05f*.05f;
        VectorCopy(seen->velocity,aheadV);aheadV[2]-=800*.05f;
        ThrowVelocity(self->client->ps.origin,ahead,aheadV[2],self->client->ps.fd.forcePowerLevel[best],0,best==FP_PULL,v2);
        if(Distance(self->client->ps.origin,ahead)<DEFENDER_PULL_RANGE &&
           Sweep(runner,ahead,v2,NULL,horizon,end2,&ms2)==SWEEP_LETHAL && (esc2=Escapes(runner,ahead,v2,horizon,0))<bestEscapes) {
            defender.patienceHolds++;
            Shoot(self,runner,seen,cmd);return;
        }
    }
    if(best<0) {
        if(starvedKind>=0 && level.time>=defender.starvedNext) {
            playerState_t *ps=&self->client->ps;
            defender.starved++;defender.starvedNext=level.time+DEFENDER_STARVED_MS;
            trap->Print("[DEFENDER] STARVED level=%d kind=%s force=%d need=%d speed_active=%d escapes=%d distance=%.1f carrying=%d chasing=%d starved=%d\n",
                level.time,starvedKind==FP_PULL?"pull":"push",ps->fd.forcePower,
                forcePowerNeeded[ps->fd.forcePowerLevel[starvedKind]][starvedKind],!!(ps->fd.forcePowersActive&(1<<FP_SPEED)),
                starvedEscapes,Distance(ps->origin,seen->origin),
                !!runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG],defender.chasing,defender.starved);
        }
        if(!CarrierPull(self,runner,seen,cmd,react))Shoot(self,runner,seen,cmd);
        return;
    }
    Throw(self,runner,seen,cmd,best,"lethal",react,bestMs,bestEnd,baseline,bestEscapes,mode);
}

// ------------------------------------------------ the camper (milestone B)
//
// Camper_v2 (adddefender <team> camper; docs/movement/MILESTONE_B.md, B1): the
// runner's second offense, a guest mover like chase D. Each frame its goal is,
// first that applies:
//   carry   it holds the enemy flag: its own stand (a capture needs its own flag home);
//   retake  the enemy flag lies dropped on the graph (its runner died carrying):
//           touch it before the enemy returns it;
//   take    the enemy flag is home and its own runner is dead (bot_camperTake,
//           default 1): take it while the runner respawns far away, and
//           keep going for it until it is taken;
// otherwise it camps around the enemy stand (around the
// flagstand, with some drift based on what's going on), never past
// bot_camperLeash (default 1,000) from the stand:
//   fc      the enemy carrying its own flag is perceived within
//           bot_camperPressure (default 1,500): fight it, following it only
//           while it is inside the leash ("fight it but maybe not chase it");
//   fight   another enemy (base D, chase D) is perceived that close, and it
//           has bot_camperFightHealth (default 50) health and a stocked
//           weapon: stand and fight, inside the leash;
//   rearm   the same, but no stocked weapon: fetch one (the defenders' rearm),
//           else retreat;
//   retreat the same, but hurt: move to the spot around the stand farthest
//           from that enemy ("move away");
//   camp    no pressure: grab a weapon near the stand (the defenders' rearm,
//           home being the enemy stand), else stand on a spot 200-500 u from
//           the stand, the one nearest where it is.
// Spots are interior graph cells (four linked neighbours, no hazard seen at a
// refused one, no mover) on the stand's level. Fighting is Engage, with
// ChooseTarget preferring the enemy nearest its own carrier. CAMPER lines log
// each goal change.
enum {CAMP_POST,CAMP_TAKE,CAMP_RETAKE,CAMP_CARRY,CAMP_FC,CAMP_FIGHT,CAMP_RETREAT,CAMP_REARM};
static const char *camperGoalNames[8]={"camp","take","retake","carry","fc","fight","retreat","rearm"};
#define CAMP_LEVEL_Z 160.0f      // a spot within this height of the stand's floor
#define CAMP_RING_MIN 200.0f     // idle spots: off the stand itself
#define CAMP_RING_MAX 500.0f
#define CAMP_SPOT_MS 1000        // a chosen spot is re-chosen at most this often

// The enemy flag: lying dropped, or home on its stand (drawn only while home).
static gentity_t *EnemyFlag(gentity_t *self,qboolean dropped) {
    gentity_t *flag=NULL;
    while((flag=Bot2_NextFlag(self->client->sess.sessionTeam==TEAM_RED?TEAM_BLUE:TEAM_RED,flag))!=NULL) {
        if(!flag->inuse || !flag->r.contents)continue;
        if(dropped && (flag->flags&FL_DROPPED_ITEM))return flag;
        if(!dropped && !(flag->flags&FL_DROPPED_ITEM) && !(flag->s.eFlags&EF_NODRAW))return flag;
    }
    return NULL;
}

// The interior cell on the stand's level, between lo and hi from the stand,
// nearest `near`, or with `away` the one farthest from it (less a little for
// the walk there). 0 when there is none.
static int CampSpot(const vec3_t stand,float lo,float hi,const vec3_t near,const float *away,vec3_t out) {
    int i,n=Graph_CellCount(),have=0;float best=0;
    for(i=0;i<n;i++) {
        const graphCell_t *c=Graph_Cell(i);vec3_t p;float d,score;
        if(c->flags || c->mover || c->link[0]<0 || c->link[1]<0 || c->link[2]<0 || c->link[3]<0)continue;
        if(fabsf(c->z-stand[2])>CAMP_LEVEL_Z)continue;
        VectorSet(p,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);
        d=Distance(p,stand);
        if(d<lo || d>hi)continue;
        score=away?Distance(p,near)*.5f-Distance(p,away):Distance(p,near);
        if(!have || score<best) {best=score;VectorCopy(p,out);have=1;}
    }
    return have;
}

int Bot2_CampSpot(const vec3_t stand,float lo,float hi,const vec3_t near,vec3_t out) {
    return CampSpot(stand,lo,hi,near,NULL,out);
}

// Walks to `to`, or holds when there; 1 when it moved.
static int CampMoveTo(int clientNum,gentity_t *self,usercmd_t *cmd,const vec3_t to) {
    if(Distance(self->client->ps.origin,to)<=DEFENDER_POST_REACHED)return 0;
    DefenderMove(clientNum,self,cmd,to);return 1;
}

// Returns 1 when it moved this frame (no idle aim), 2 when it carries the flag (no shots), 3 when it took /kill
// (the frame ends: no Engage or protect on the dead body).
static int CamperMove(int clientNum,gentity_t *self,gentity_t *target,const defenderSeen_t *seen,usercmd_t *cmd) {
    playerState_t *ps=&self->client->ps;gentity_t *dropped=EnemyFlag(self,qtrue),*home=EnemyFlag(self,qfalse),*mate=NULL;
    int goal,i,flagPw=self->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG,ownPw=flagPw==PW_BLUEFLAG?PW_REDFLAG:PW_BLUEFLAG;
    const float *to,*known=Known(self,seen);float leash=CvarFloat("bot_camperLeash",1000);
    for(i=0;i<level.maxclients;i++) {
        gentity_t *ent=&g_entities[i];
        if(ent->inuse && ent->client && ent->client->pers.connected==CON_CONNECTED && Bot2_IsMovementOnly(i) &&
           ent->client->sess.sessionTeam==self->client->sess.sessionTeam)mate=ent;
    }
    if(known && (target->health<=0 || Distance(known,ps->origin)>=CvarFloat("bot_camperPressure",1500)))known=NULL;
    // A flag dropped off the graph (at the bottom of a pit) is no goal.
    if(dropped && Graph_CellCount()>0 && Graph_Nearest(dropped->r.currentOrigin,64,64)<0)dropped=NULL;
    // bot_defenderDropReach: the runner's whole test, hazard and graph path, not only a cell.
    if(dropped && CvarFloat("bot_defenderDropReach",1) && Bot2_DropOutOfReach(clientNum,dropped,
       self->client->sess.sessionTeam==TEAM_RED?TEAM_BLUE:TEAM_RED))dropped=NULL;
    if(ps->powerups[flagPw] && Bot2_SelfKillRequested(clientNum)>=0 && Bot2_SelfKillHonour(self,"camper")) {
        // A fresh teammate at the enemy stand asked for it (Bot2_RequestSelfKill): the flag returns there for its run.
        trap->Print("[DEFENDER] CAMPER_SK level=%d requester=%d health=%d force=%d stand_distance=%.0f\n",level.time,
            Bot2_SelfKillRequested(clientNum),self->health,ps->fd.forcePower,Distance(ps->origin,defender.stand));
        G_Kill(self);
        return FixOn("bot_defenderFixBA10")?3:1;   // bot_defenderFixBA10: 3, the frame ends
    }
    if(ps->powerups[flagPw]) {goal=CAMP_CARRY;to=defender.stand;}
    else if(dropped) {goal=CAMP_RETAKE;to=dropped->r.currentOrigin;}
    // A take, once started, runs until the flag is gone: the runner respawns within a second, far away.
    else if(home && (!mate || mate->health<=0 || defender.camperGoal==CAMP_TAKE) && CvarFloat("bot_camperTake",1)) {goal=CAMP_TAKE;to=home->r.currentOrigin;}
    else if(known && target->client->ps.powerups[ownPw]) {goal=CAMP_FC;to=known;}
    else if(known) {goal=self->health<CvarFloat("bot_camperFightHealth",50)?CAMP_RETREAT:Armed(self)?CAMP_FIGHT:CAMP_REARM;to=known;}
    else {goal=CAMP_POST;to=defender.enemyStand;}
    if(goal!=defender.camperGoal) {
        if(goal==CAMP_CARRY && defender.camperGoal==CAMP_TAKE)defender.takes++;
        if(goal==CAMP_CARRY && defender.camperGoal==CAMP_RETAKE)defender.retakes++;
        trap->Print("[DEFENDER] CAMPER level=%d goal=%s from=%s ms=%d x=%.0f y=%.0f z=%.0f to=%.0f,%.0f,%.0f distance=%.0f stand=%.0f health=%d armed=%d mate_alive=%d takes=%d retakes=%d\n",
            level.time,camperGoalNames[goal],defender.camperGoal>=0?camperGoalNames[defender.camperGoal]:"none",
            level.time-defender.camperGoalSince,ps->origin[0],ps->origin[1],ps->origin[2],to[0],to[1],to[2],Distance(ps->origin,to),
            Distance(ps->origin,defender.enemyStand),self->health,Armed(self),mate && mate->health>0,defender.takes,defender.retakes);
        defender.camperGoal=goal;defender.camperGoalSince=level.time;defender.campHave=0;
    }
    switch(goal) {
    case CAMP_POST:
        if(trap->Cvar_VariableIntegerValue("bot_defenderRearm") && RearmDispatch(clientNum,self,cmd,NULL)) {defender.campHave=0;return 1;}
        if(!defender.campHave || (level.time-defender.campTime>=CAMP_SPOT_MS && Distance(ps->origin,defender.campSpot)>DEFENDER_POST_REACHED)) {
            defender.campHave=CampSpot(defender.enemyStand,CAMP_RING_MIN,CAMP_RING_MAX,ps->origin,NULL,defender.campSpot);defender.campTime=level.time;
        }
        return defender.campHave?CampMoveTo(clientNum,self,cmd,defender.campSpot):0;
    case CAMP_FC:
        // Inside the leash it closes on the carrier's cell; outside, it holds the leash's edge nearest it.
        if(level.time-defender.campTime>=CAMP_SPOT_MS/2 || !defender.campHave) {
            int cell=Distance(known,defender.enemyStand)<leash?Graph_Nearest(known,128,128):-1;
            defender.campTime=level.time;
            if(cell>=0) {const graphCell_t *c=Graph_Cell(cell);VectorSet(defender.campSpot,c->ix*(float)GRAPH_GRID,c->iy*(float)GRAPH_GRID,c->z);defender.campHave=1;}
            else defender.campHave=CampSpot(defender.enemyStand,CAMP_RING_MIN,leash,known,NULL,defender.campSpot);
        }
        return defender.campHave?CampMoveTo(clientNum,self,cmd,defender.campSpot):0;
    case CAMP_FIGHT:
        // Stand and fight; only drifted past the leash does it walk back in.
        if(Distance(ps->origin,defender.enemyStand)<=leash)return 0;
        if(!defender.campHave)defender.campHave=CampSpot(defender.enemyStand,CAMP_RING_MIN,leash,ps->origin,NULL,defender.campSpot);
        return defender.campHave?CampMoveTo(clientNum,self,cmd,defender.campSpot):0;
    case CAMP_REARM:
        if(trap->Cvar_VariableIntegerValue("bot_defenderRearm") && RearmDispatch(clientNum,self,cmd,NULL))return 1;
        // fall through: no weapon to fetch, so it moves away
    case CAMP_RETREAT:
        if(!defender.campHave || level.time-defender.campTime>=CAMP_SPOT_MS) {
            defender.campHave=CampSpot(defender.enemyStand,CAMP_RING_MIN,leash,ps->origin,known,defender.campSpot);defender.campTime=level.time;
        }
        return defender.campHave?CampMoveTo(clientNum,self,cmd,defender.campSpot):0;
    }
    // A flag is touched by walking onto it: the planner arrives at a cell, not a point.
    if(goal!=CAMP_CARRY && ReachWalk(clientNum,self,cmd,to,0,1)) {
    } else DefenderMove(clientNum,self,cmd,to);
    cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
    if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    return goal==CAMP_CARRY?2:1;
}

static void DefenderCommand(int clientNum,int time,usercmd_t *cmd) {
    gentity_t *self=&g_entities[clientNum],*runner;const defenderSeen_t *seen;
    int react,strafe,slot=DefenderSlot(clientNum),chaseOn;
    memset(cmd,0,sizeof(*cmd));cmd->serverTime=time;cmd->weapon=self->client->ps.weapon;
    if(slot<0)return;
    defenderCur=&defenders[slot];
    // A defender dead at the release is released at its respawn.
    if(defender.pendingRelease && defender.client==clientNum && self->health>0) {
        ReleaseOne(self,clientNum,slot,1,defender.releaseTime);
        if(!defender.armed)defender.pendingRelease=0;   // its post refused: as an alive defender's would be
    }
    if(defender.armed && defender.client==clientNum && defender.role!=DROLE_CAMPER)FlagBoxWatch(self);
    // bot_defenderKeepSequence: one KEEP_SEQUENCE line per guarded episode,
    // with the frames it covered, when the episode ends.
    {
        int guarded=SequenceGuard(clientNum);
        if(guarded) {
            if(!defender.keepActive) {defender.keepActive=1;defender.keepSince=level.time;defender.keepFrames=0;}
            defender.keepFrames++;
        } else if(defender.keepActive) {
            defender.keepActive=0;
            trap->Print("[DEFENDER] KEEP_SEQUENCE level=%d role=%s frames=%d\n",level.time,roleNames[defender.role],defender.keepFrames);
        }
    }
    cmd->angles[PITCH]=ANGLE2SHORT(self->client->ps.viewangles[PITCH])-self->client->ps.delta_angles[PITCH];
    cmd->angles[YAW]=ANGLE2SHORT(self->client->ps.viewangles[YAW])-self->client->ps.delta_angles[YAW];
    if(defender.armed && defender.client==clientNum && (runner=DefenderRunner(self))!=NULL) {
        // bot_defenderFixBA6: a corpse's sightings are not remembered.
        if(self->health>0 || !FixOn("bot_defenderFixBA6"))Remember(self,runner);
        Shadow(self,runner);
        CarryLog(clientNum,self,runner);
    }
    if(self->health<=0) {
        if(defender.armed && defender.client==clientNum && !defender.dead) {
            defender.dead=1;
            trap->Print("[DEFENDER] DIED level=%d killer=%d mod=%d\n",level.time,self->client->lasthurt_client,self->client->lasthurt_mod);
            if(self->client->lasthurt_mod==MOD_FALLING && defender.rearmItem>=0 && level.time-defender.rearmSince<DEFENDER_FALL_CHARGE_MS) {
                defender.fallUntil[defender.rearmItem]=level.time+(int)CvarFloat("bot_defenderFallChargeMs",60000);defender.fallCharges++;
                trap->Print("[DEFENDER] REARM level=%d role=%s done=fell item=%d charged=%d\n",level.time,roleNames[defender.role],
                    defender.rearmItem,defender.fallCharges);
            }
            if(self->client->lasthurt_mod==MOD_FALLING && defender.rpItem>=0 && level.time-defender.rpSince<DEFENDER_FALL_CHARGE_MS) {
                defender.fallUntil[defender.rpItem]=level.time+(int)CvarFloat("bot_defenderFallChargeMs",60000);defender.fallCharges++;
                trap->Print("[DEFENDER] REARM level=%d role=%s done=fell item=%d charged=%d\n",level.time,roleNames[defender.role],
                    defender.rpItem,defender.fallCharges);
            }
        }
        // The camper's protect perception (Bot2_ProtectCommand) forgets each
        // life, as the runner's does through Bot2_ClearState.
        if(defender.role==DROLE_CAMPER && defender.client==clientNum)Bot2_RunnerForceReset(clientNum);
        cmd->buttons=BUTTON_ATTACK;return;   // respawn
    }
    if(!defender.armed || defender.client!=clientNum)return;
    if(self->client->sess.sessionTeam!=defender.standTeam) {
        Stands(self);
        trap->Print("[DEFENDER] STANDS level=%d role=%s team=%d stand=%.1f,%.1f,%.1f enemy_stand=%.1f,%.1f,%.1f\n",level.time,
            roleNames[defender.role],defender.standTeam,defender.stand[0],defender.stand[1],defender.stand[2],
            defender.enemyStand[0],defender.enemyStand[1],defender.enemyStand[2]);
    }
    // The post is taken once, at the release: a killed defender respawns
    // where the game puts it and keeps throwing from there.
    if(self->client->ps.persistant[PERS_SPAWN_COUNT]!=defender.placedSpawn) {
        defender.placedSpawn=self->client->ps.persistant[PERS_SPAWN_COUNT];defender.dead=0;
        defender.spawnTime=level.time;defender.rearmItem=-1;defender.braceUntil=0;defender.mineSpot=-1;defender.minePress=0;defender.ambushPost=-1;
        defender.closeUntil=0;defender.closing=0;
        defender.rpItem=-1;defender.rpClaimItem=-1;defender.rpWaiting=0;
        // A defender's death does not pass through Bot2_Think's reset, and a
        // committed sequence from before it held the new objective through
        // bot_moveGuestFinish: the dead plan's run-up walked the respawned
        // defender straight into Kejim's pit every 8.9 s (falls3 build g).
        Move_Reset(self->s.number);
        // bot_defenderFixBA6: the new life starts with no sightings, engage delay or humanized view.
        if(FixOn("bot_defenderFixBA6")) {
            const defenderSeen_t *last=defender.seenCount?&defender.seen[(defender.seenNext-1+DEFENDER_HISTORY)%DEFENDER_HISTORY]:NULL;
            if(last && last->time==level.time) {defender.seen[0]=*last;defender.seenNext=defender.seenCount=1;}   // this frame's, kept
            else defender.seenCount=defender.seenNext=0;
            defender.engageSince=defender.engageLast=0;
            Bot2_AimHumanReset(self->s.number,self->client->ps.viewangles);
        }
        trap->Print("[DEFENDER] RESPAWN level=%d x=%.1f y=%.1f z=%.1f post=0 role=%s weapons=%d saber_offense=%d side=%d\n",level.time,
            self->client->ps.origin[0],self->client->ps.origin[1],self->client->ps.origin[2],roleNames[defender.role],
            self->client->ps.stats[STAT_WEAPONS],self->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE],self->client->ps.fd.forceSide);
    }
    ShotConfirm(self);
    // A base or chase D holding the enemy flag runs it home (bot_defenderCarryHome,
    // default 1). It is never sought (the keep-out), but returning
    // the own flag where the enemy carrier died on its own stand touches the
    // enemy flag too, and a defender holding it then stood there for minutes
    // (records/2026-10-02-b3-stalls.md). The camper has its own carry goal.
    if(defender.role!=DROLE_CAMPER && CvarFloat("bot_defenderCarryHome",1) &&
       self->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_BLUEFLAG:PW_REDFLAG]) {
        if(!defender.carryingHome) {
            defender.carryingHome=1;defender.carriesHome++;
            trap->Print("[DEFENDER] CARRY_HOME level=%d role=%s x=%.0f y=%.0f z=%.0f stand_distance=%.0f carries=%d\n",level.time,
                roleNames[defender.role],self->client->ps.origin[0],self->client->ps.origin[1],self->client->ps.origin[2],
                Distance(self->client->ps.origin,defender.stand),defender.carriesHome);
        }
        DefenderMove(clientNum,self,cmd,defender.stand);
        cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
        if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
        return;
    }
    defender.carryingHome=0;
    runner=DefenderRunner(self);
    if(!runner)return;
    if(self->client->ps.persistant[PERS_HITS]>defender.damageHits) {
        defender.damageHits=self->client->ps.persistant[PERS_HITS];
        trap->Print("[DEFENDER] DAMAGE level=%d hits=%d runner_health=%d carrying=%d since_shot=%d\n",level.time,defender.damageHits,
            runner->health,!!runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG],
            level.time-defender.lastShot);
    }
    Follow(runner);
    react=trap->Cvar_VariableIntegerValue("bot_defenderReactMs");react=react<0?0:(react>1000?1000:react);
    seen=Perceived(react);
    if(defender.role==DROLE_CAMPER) {
        int moved=CamperMove(clientNum,self,runner,seen,cmd);
        defender.guardMoving=moved!=0;
        if(moved==3)return;   // killed itself; no Engage or protect on the body
        if(moved!=2)Engage(self,runner,seen,cmd,react);
        // bot_camperProtect (default 1: camper deaths 18.8 -> 15.5 a world at
        // unchanged captures, records/2026-10-02-b1-merge-and-testing.md; records/2026-10-02-camper-force.md):
        // the runner's protect decision for the camper (Bot2_ProtectCommand,
        // ai_bot2_objective.c), the rules of milestone A: reactive only
        // (a seen missile closing in, ranged damage taken, a seen enemy mine
        // in splash reach), activation only, never lowered, and only when the
        // frame's one generic_cmd is free - the movement's Speed purchase and
        // Engage's throw come first. Absorb stays off.
        if(CvarFloat("bot_camperProtect",1))Bot2_ProtectCommand(clientNum,cmd);
        return;
    }
    strafe=trap->Cvar_VariableIntegerValue("bot_defenderStrafe");
    if(seen && seen->visible) {
        // The walking chaser takes every sighting; the strafing one holds its
        // goal until the sighting is far from it or the goal is reached.
        float retarget=(float)trap->Cvar_VariableIntegerValue("bot_defenderRetarget");
        if(retarget<=0)retarget=256;
        VectorCopy(seen->origin,defender.lastSeen);defender.knownTime=seen->time;
        if(defender.intercepting) {}   // chase D's goal is its intercept, not the sighting
        else if(!strafe || !defender.chaseSeenTime || !defender.chasing ||
           Distance(seen->origin,defender.chaseGoal)>=retarget ||
           Distance(self->client->ps.origin,defender.chaseGoal)<=DEFENDER_GOAL_REACHED) {
            if(defender.chasing && strafe && Distance(seen->origin,defender.chaseGoal)>1)defender.retargets++;
            VectorCopy(seen->origin,defender.chaseGoal);
        }
        defender.chaseSeenTime=seen->time;
    }
    {
        // The theft (bot_defenderTheftKnown, default 1: unseen
        // pickups can be reacted to, since the HUD and console message communicate them):
        // the HUD's flag status leaves "home" and the console names the carrier,
        // who at that moment stands on the own stand. That is a sighting of where,
        // not of the carrier: positioning and the chase use it, the aim does not.
        int home=FlagHome(self);
        if(defender.theftWasHome && !home && runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG] &&
           CvarFloat("bot_defenderTheftKnown",1)) {
            VectorCopy(defender.stand,defender.lastSeen);defender.knownTime=level.time;defender.intercepting=0;defender.interceptNext=0;
            if(!defender.chaseSeenTime)defender.chaseSeenTime=level.time;
            // bot_defenderTheftGoal (default 1): an unseen theft also moves the chase goal to the
            // stand. Chase D replaces it with its intercept below; base and solo D walked to whatever goal an
            // earlier chase left (or the origin) until the next sighting.
            if(!(seen && seen->visible) && CvarFloat("bot_defenderTheftGoal",1))VectorCopy(defender.stand,defender.chaseGoal);
            trap->Print("[DEFENDER] THEFT level=%d role=%s carrier=%d seen=%d\n",level.time,roleNames[defender.role],runner->s.number,
                seen && seen->visible);
        }
        defender.theftWasHome=home;
    }
    chaseOn=defender.chaseSeenTime && runner->client->ps.powerups[self->client->sess.sessionTeam==TEAM_RED?PW_REDFLAG:PW_BLUEFLAG];
    // Base D's leash keeps it home for a second attacker; against a lone
    // attacker there is none, so it chases.
    if(defender.role==DROLE_BASE && TeamCount(-1,runner->client->sess.sessionTeam)>1)
        chaseOn=chaseOn && Distance(defender.lastSeen,defender.stand)<CvarFloat("bot_defenderLeash",1500);
    else if(defender.role==DROLE_SOLO)chaseOn=chaseOn && trap->Cvar_VariableIntegerValue("bot_defenderChase");
    {
        // bot_chaserPursue 1: chase D, with its flag home, strafes after a runner
        // known within bot_chaserPursueRange (default 2,500) of the own stand, so
        // it is close and at speed at the grab (it needs to be
        // strafing earlier so speeds are closer).
        const float *known=Known(self,seen);
        int pursue=!chaseOn && defender.role==DROLE_CHASE && CvarFloat("bot_chaserPursue",1) && runner->health>0 &&
                   known && FlagHome(self) && Distance(known,defender.stand)<CvarFloat("bot_chaserPursueRange",2500);
        if(pursue) {
            if(!defender.pursuing || Distance(known,defender.chaseGoal)>=256 ||
               Distance(self->client->ps.origin,defender.chaseGoal)<=DEFENDER_GOAL_REACHED)VectorCopy(known,defender.chaseGoal);
            if(!defender.pursuing)trap->Print("[DEFENDER] PURSUE level=%d runner_to_stand=%.0f distance=%.0f seen=%d\n",level.time,
                Distance(known,defender.stand),Distance(known,self->client->ps.origin),seen->visible);
            if(!defender.chaseSeenTime)defender.chaseSeenTime=seen->time;
            chaseOn=1;
        }
        defender.pursuing=pursue;
    }
    if(!chaseOn)defender.intercepting=0;
    // bot_chaserIntercept (default 1: if it is ahead of the
    // enemy carrier it should try to intercept sooner rather than go to a fixed goal at the
    // flagstand): with bot_chaserGoal 1 too, chase D runs for the first cell
    // of the carrier's route home it reaches first, and for the stand when
    // there is none.
    if(chaseOn && !defender.pursuing && defender.role==DROLE_CHASE &&
       (!CvarFloat("bot_chaserGoal",1) || CvarFloat("bot_chaserIntercept",1)) && level.time>=defender.interceptNext) {
        vec3_t goal;float them=0,me=0,retarget=(float)trap->Cvar_VariableIntegerValue("bot_defenderRetarget");
        const defenderSeen_t *last=seen && seen->visible?seen:NULL;
        if(retarget<=0)retarget=256;
        defender.interceptNext=level.time+500;
        float age=CvarFloat("bot_chaserInterceptAge",1)?(level.time-defender.knownTime)/1000.0f:0;
        if(last || !defender.knownTime)age=0;   // in sight now: lastSeen is current
        if(InterceptGoal(self,defender.lastSeen,last?sqrtf(last->velocity[0]*last->velocity[0]+last->velocity[1]*last->velocity[1]):0,age,goal,&them,&me)) {
            if(!defender.intercepting || Distance(goal,defender.chaseGoal)>=retarget) {
                VectorCopy(goal,defender.chaseGoal);
                trap->Print("[DEFENDER] INTERCEPT level=%d goal=%.0f,%.0f,%.0f carrier_ms=%d chaser_ms=%d distance=%.0f\n",level.time,
                    goal[0],goal[1],goal[2],(int)(them*1000),(int)(me*1000),Distance(self->client->ps.origin,goal));
            }
            defender.intercepting=1;
        } else if(defender.intercepting) {
            defender.intercepting=0;VectorCopy(defender.lastSeen,defender.chaseGoal);
            trap->Print("[DEFENDER] INTERCEPT level=%d none tail=%.0f,%.0f,%.0f\n",level.time,
                defender.lastSeen[0],defender.lastSeen[1],defender.lastSeen[2]);
        }
    }
    if(chaseOn && defender.role==DROLE_CHASE && CvarFloat("bot_chaserGoal",1)) {
        // bot_chaserGoal 1: chase D runs for where the
        // runner is going, the own stand before the grab and the runner's stand
        // with the flag, and treats the runner as secondary: shots and pulls
        // when in reach, no steering at it. The stands are always on the
        // graph; a goal on an airborne or ledged runner was the no-path tail
        // behind chase D's falls.
        if(defender.pursuing || !defender.intercepting)VectorCopy(defender.pursuing?defender.stand:defender.enemyStand,defender.chaseGoal);
        if(defender.pursuing)defender.intercepting=0;
    }
    if(chaseOn && defender.role==DROLE_CHASE && !defender.pursuing && level.time<defender.closeUntil)CloseGoal(self,runner,seen);
    else if(defender.closing) {
        defender.closing=0;
        trap->Print("[DEFENDER] CLOSE level=%d end\n",level.time);
    }
    if(chaseOn) {
        vec3_t aim={1,0,0};
        if(!defender.chasing) {
            defender.chasing=1;
            if(defender.weapon) {
                char line[256]="";int k;
                for(k=0;k<PRE_REASONS;k++)Q_strcat(line,sizeof(line),va(" %s=%d",preNames[k],defender.pre[k]));
                trap->Print("[DEFENDER] PRESHOT level=%d%s\n",level.time,line);
                memset(defender.pre,0,sizeof(defender.pre));
            }
            defender.retargets=0;
            trap->Print("[DEFENDER] CHASE level=%d role=%s x=%.1f y=%.1f z=%.1f goal=%.1f,%.1f,%.1f strafe=%d\n",level.time,
                roleNames[defender.role],self->client->ps.origin[0],self->client->ps.origin[1],self->client->ps.origin[2],
                defender.chaseGoal[0],defender.chaseGoal[1],defender.chaseGoal[2],strafe);
        }
        // bot_chaserRearmChase 1 (L4): an unarmed chase D fetches a weapon
        // before it chases. A respawn keeps only the spawn loadout, rearming
        // waited for the chase to end, and carriers escaped 50-58% of grabs
        // with chase D unarmed against 11-23% armed.
        if(defender.role==DROLE_CHASE && CvarFloat("bot_chaserRearmChase",1) && trap->Cvar_VariableIntegerValue("bot_defenderRearm") &&
           !Armed(self) && (RearmDispatch(clientNum,self,cmd,defender.chaseGoal) ||
           // bot_chaserRearmAny (default 1): with nothing near the route, rearm anywhere
           // rather than chase unarmed (records/2026-10-02-b3-stalls.md).
           (CvarFloat("bot_chaserRearmAny",1) && RearmDispatch(clientNum,self,cmd,NULL)))) {
            if(!defender.rearmChasing)trap->Print("[DEFENDER] REARM_CHASE level=%d role=%s item=%d\n",level.time,roleNames[defender.role],
                CvarFloat("bot_defenderRearmPlan",1)?defender.rpItem:defender.rearmItem);
            defender.rearmChasing=1;
        } else {
            defender.rearmChasing=0;
            DefenderMove(clientNum,self,cmd,defender.chaseGoal);
        }
        cmd->buttons&=~(BUTTON_ATTACK|BUTTON_ALT_ATTACK|BUTTON_FORCEPOWER);
        if(cmd->generic_cmd!=GENCMD_FORCE_SPEED)cmd->generic_cmd=0;
    } else if(defender.chasing) {
        defender.chasing=0;
        trap->Print("[DEFENDER] CHASE_END level=%d role=%s x=%.1f y=%.1f z=%.1f retargets=%d\n",level.time,roleNames[defender.role],
            self->client->ps.origin[0],self->client->ps.origin[1],self->client->ps.origin[2],defender.retargets);
    }
    defender.guardMoving=0;
    if(trap->Cvar_VariableIntegerValue("bot_defenderReset") && !defender.chasing && Reset(self,runner))return;
    if(!defender.chasing) {
        // Rearm unless the role is busy: base D with the runner near the stand, chase D chasing.
        int busy=defender.role!=DROLE_CHASE && seen && seen->visible && Distance(seen->origin,defender.stand)<DEFENDER_GUARD_RANGE,block;
        // Returning the dropped flag comes first for chase D; the block
        // outranks errands: base D gets into the runner's way rather than
        // tending mines or items.
        if(ReturnFlag(clientNum,self,cmd))defender.guardMoving=1;
        else if((block=BlockMove(clientNum,self,runner,seen,cmd))>=0)defender.guardMoving=block;
        else if(!busy && trap->Cvar_VariableIntegerValue("bot_defenderRearm") && RearmDispatch(clientNum,self,cmd,NULL))defender.guardMoving=1;
        else if(LayMines(clientNum,self,runner,cmd))defender.guardMoving=1;
        else if(!AmbushMove(clientNum,self,runner,seen,cmd) &&
                (defender.role!=DROLE_SOLO || trap->Cvar_VariableIntegerValue("bot_defenderGuard")))
            defender.guardMoving=GuardMove(clientNum,self,runner,seen,cmd);
    }
    Engage(self,runner,seen,cmd,react);
}

void Svcmd_AddDefender_f(void) {
    char team[16];int i,freeSlot=0,chase=0,slot;const char *name;
    if(level.gametype!=GT_CTF || trap->Cvar_VariableIntegerValue("bot_movementOnly")!=2) {
        trap->Print("adddefender: create a CTF game and use addrunner first.\n");return;
    }
    trap->Argv(1,team,sizeof(team));
    if(trap->Argc()==3) {
        char role[16];trap->Argv(2,role,sizeof(role));
        chase=!Q_stricmp(role,"chase")?1:!Q_stricmp(role,"camper")?2:0;
        if(!chase) {trap->Print("Usage: adddefender red|blue [chase|camper]\n");return;}
    }
    if(trap->Argc()<2 || trap->Argc()>3 || (Q_stricmp(team,"red") && Q_stricmp(team,"blue"))) {trap->Print("Usage: adddefender red|blue [chase|camper]\n");return;}
    for(i=0;i<level.maxclients;i++)if(!g_entities[i].inuse)freeSlot++;
    if(!freeSlot) {trap->Print("adddefender: no free player slot.\n");return;}
    // The role's first name not already in the game: the first of a role added
    // is <Role>_v2, the second <Role>2_v2 (its position cvars bot_<role>2...).
    // Both taken: a third would share the second's name and so its state (DefenderSlot maps by name).
    {
        int first=0,second=0;
        for(i=0;i<level.maxclients;i++) {
            char other[MAX_NETNAME];
            if(!g_entities[i].inuse || !g_entities[i].client)continue;
            Q_strncpyz(other,g_entities[i].client->pers.netname,sizeof(other));Q_CleanStr(other);
            if(!Q_stricmp(other,slotNames[chase]))first=1;
            if(!Q_stricmp(other,slotNames[chase+3]))second=1;
        }
        if(first && second) {trap->Print("adddefender: %s and %s are both in the game.\n",slotNames[chase],slotNames[chase+3]);return;}
        slot=first?chase+3:chase;
    }
    name=slotNames[slot];
    if(!G_GetBotInfoByName(name)) {trap->Print("adddefender: %s missing from scripts/kejim_defender.bot.\n",name);return;}
    trap->SendConsoleCommand(EXEC_APPEND,va("addbot %s 3 %s 0 %s\n",name,team,name));
    trap->Print("[DEFENDER] Adding %s %s %s. Holds bot_%sPost from the corner start.\n",team,
        chase==2?"camper":chase?"chaser":"defender",name,slotPrefix[slot]);
}

// defenderpost: place (or move) the defender at bot_defenderPost now, as the
// corner start's release does, in a world with no corner start: a laps world
// (tools/play_vs_runner.py --no-client) where the runner runs its route over
// and over. Localhost fixture only.
void Svcmd_DefenderPost_f(void) {
    Bot2_DefenderRelease();
}
