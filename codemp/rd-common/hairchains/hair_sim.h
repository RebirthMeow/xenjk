/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/
/* hair_sim.h -- bone-chain dynamics for hair (position-based dynamics: particles on bone heads, asymmetric
 * distance cones with angular limits, lateral links, capsule/sphere receiver-vs-repulsor collision), plus the
 * pose step that turns solved particles into bone frames and copies driver deltas onto mirrored chains.
 *
 * Clean-room: a C port of the "Route A" solver, our own position-based-dynamics reading of the BNM parameter set.
 * Plain C99, no engine dependencies; the offline harness is tools/hair/hc_replay.c.
 * hc_real is float unless built with -DHC_DOUBLE. Asset format: docs/hair/FORMAT.md.
 *
 * Frames: the solver runs in a reference frame (the character root). Every per-frame input (targets, anchors,
 * repulsor points, animated bone matrices) is expressed in that frame; HC_BeginFrame takes the frame's world matrix
 * so the solver can feel its acceleration (fictitious force) and keep gravity pointing down.
 * Matrices are 4x4 column-major (translation in [12..14]).
 */
#ifndef HAIR_SIM_H
#define HAIR_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HC_DOUBLE
typedef double hc_real;
#else
typedef float hc_real;
#endif

typedef struct {
	int bone;                 /* hair skeleton bone whose head the particle sits on */
	int group;
	float radius, gravityScale, damping, velocityDamping, constraintDamping, friction, bounciness;
	float parkRate, fictitious, centrifugal;
} hcParticle;

typedef struct {
	int child, parent;        /* particle indices; parent -1 = anchored to anchorBone */
	int anchorBone;           /* hair skeleton bone (the chain's kinematic root) or -1 */
	float limit;              /* radians */
	float inner, outer, stretch, compress, parentStrength, rest;
} hcCone;

typedef struct { int a, b; float stretch, compress, rest; } hcLink;

typedef struct { int n; int p[2]; float r[2]; } hcReceiver;     /* sphere on one particle or capsule on two */

#define HC_REP_INVERSE 1            /* repulsor flag: a containment shape (BNM bIsInverse) */
#define HC_ASSET_HONOUR_INVERSE 2   /* asset flag: keep receivers inside containment shapes (Route A pushes out) */

typedef struct {
	int n;                    /* 1 sphere, 2 capsule */
	int flags;                /* HC_REP_INVERSE */
	int gameBone[2];          /* game skeleton bone that carries each point, the game parser refuses -1 ("supplied per frame", offline reels only) */
	float bind[2][3];         /* point in bind space (asset units) when gameBone >= 0 */
	float r[2];
} hcRepulsor;

typedef struct {
	char name[32];
	int parent;               /* hair skeleton index, -1 = attached to the host bone */
	float bind[16];           /* bind (rest) world matrix, asset space */
} hcBone;

typedef struct {
	int nParticles, nCones, nLinks, nReceivers, nRepulsors, nPairs, nBones, nMirrors;
	int hostBone;             /* game skeleton bone the hair skeleton hangs from */
	int flags;                /* HC_ASSET_* */
	float dt;
	int substeps, lateralIter, collisionIter;
	float gravity[3];         /* world, asset units / s^2 */
	float unitsPerCm;         /* asset units per Bonamik centimetre */
	hcParticle *particles;
	hcCone *cones;            /* sorted root to tip */
	hcLink *links;
	hcReceiver *receivers;
	hcRepulsor *repulsors;
	int (*pairs)[2];          /* receiver, repulsor */
	hcBone *bones;            /* parents before children */
	int (*mirrors)[2];        /* target bone, driver bone */
} hcAsset;

/* How the hair feels the reference frame's own motion (the entity's runs, turns, jumps). Route A (all zero) feels
 * only each particle's authored share (fictitious 0.05-0.2) of the raw linear acceleration and no rotation; with
 * on = 1 every particle feels:
 *   linear:  -linWeight * clamp(lowpass(a_frame, linTau), maxLinAcc)    (maxLinAcc in g: units of |gravity|)
 *   turning: -eulerWeight * clamp(alpha, maxAngAcc) x r  -  centWeight * w' x (w' x r), w' = clamp(omega, maxAngVel)
 * r is the particle's position in the reference frame (origin at the entity origin), angles in radians. */
typedef struct {
	int on;
	float linWeight, maxLinAccG, linTau;
	float eulerWeight, maxAngAcc;
	float centWeight, maxAngVel;
	/* carry (> 0): instead of clamping the frame's acceleration away, a felt velocity follows the entity's velocity
	 * (and a felt spin its spin) at no more than maxLinAcc / maxAngAcc, so a one-frame JKA velocity change is felt
	 * in full but spread over time; the lag is capped at carryLag (units/s) and carryAngLag (rad/s), the rest dropped. */
	float carryLag, carryAngLag;
} hcFeel;

/* How the chains move between their constraints (all zero = Route A as ported). Route A's cone pull toward the
 * animated direction adds velocity with nothing to damp it, so the locks ring at 3-4 Hz ("springy"):
 *   springDamp   a damper beside each cone's pull: that share of the particle's constraintDamping is taken out of
 *                the child's velocity relative to its parent along the pull, at every pull (clamped to 1)
 *   innerScale   multiplies every cone's inner (restoring) strength; 0 means 1 (authored)
 *   airDrag      1/s: drag on each particle's velocity through still air (its own motion plus the entity's felt
 *                run and spin), so locks stream behind a run and trail a turn
 *   coneParent   0..1: a cone's reference direction follows its parent segment's solved swing by this much instead
 *                of the animated chain alone, so a chain bends as a whole (at 0.75 the long lock flails) */
typedef struct {
	float springDamp, innerScale, airDrag, coneParent;
} hcFlow;

typedef struct {
	const hcAsset *a;
	hcFeel feel;
	hcFlow flow;
	int *parentCone;          /* per cone: the cone whose child is this cone's parent particle, -1 at a root */
	hc_real frameVel[3];      /* reference frame: the entity's velocity through the air (felt when carrying) */
	hc_real (*pos)[3], (*prev)[3];
	/* per-frame inputs, reference frame; filled by the caller after HC_BeginFrame */
	hc_real (*target)[3];     /* where the animation puts each particle */
	hc_real (*anchor)[3];     /* per cone: the anchor bone head (root cones only) */
	hc_real (*repPoint)[2][3];
	/* frame kinematics */
	hc_real ref[16], refInv[16];
	hc_real worldScale;        /* world units per model unit (the entity's scale; 0 or 1 = none) */
	hc_real gravityLocal[3], frameAcc[3], angVel[3];
	hc_real originHist[3][3], rotHist[3][4];
	int nOrigin, nRot, lastFrame, haveLast;
	hc_real angAcc[3], prevAngVel[3], feltAcc[3];   /* game profile (feel.on) */
	int haveAngVel;
	hc_real feltVelW[3], feltOmega[3], feltAlpha[3];  /* carry mode: world felt velocity, ref-frame felt spin */
	int haveFelt;
	hc_real *poseScratch;      /* HC_PoseFrom: 2 x nBones matrices */
	unsigned char *poseFlags;  /* HC_PoseFrom: nBones */
	int *subtreeStart;         /* nBones + 1: bone b's subtree (itself and its descendants, ascending) is */
	int *subtree;              /* subtree[subtreeStart[b] .. subtreeStart[b + 1]) */
	/* bit 1 (0 after HC_Init; same results either way): HC_PoseFrom turns each cone's subtree from the lists above
	 * instead of testing every bone's ancestry */
	int fast;
	hc_real histDt[3];        /* per originHist sample: the pose time of the interval that ends at it (HC_BeginFrameDt) */
} hcSim;

int  HC_Init(hcSim *s, const hcAsset *a);
void HC_Free(hcSim *s);
/* update_reference(): new frame matrix and the frame's acceleration / angular velocity from its history.
 * A frame number that does not follow the previous one by exactly 1 drops the history (loops, teleports). */
void HC_BeginFrame(hcSim *s, const hc_real refWorld[16], int frame);
/* the reference frame is a scaled entity's: gravity and the frame's linear motion (world units) are
   divided by it into model units; a change restarts the frame history */
void HC_SetWorldScale(hcSim *s, hc_real scale);
/* The same when the reference frame took poseDt seconds to get here instead of the asset's dt (a caller that ran
 * fewer steps than the time that passed, rd-common/sim_frame.h): the frame's velocity and spin are measured over
 * poseDt, so they stay the real ones; the step still integrates dt. poseDt 0 = dt (HC_BeginFrame). */
void HC_BeginFrameDt(hcSim *s, const hc_real refWorld[16], int frame, hc_real poseDt);
/* Diagnostic: receiver/repulsor pairs overlapping by more than tol (asset units) in the current state; the deepest
 * overlap to maxDepth (may be NULL). */
int  HC_CountInside(const hcSim *s, hc_real tol, hc_real *maxDepth);
void HC_ResetToTargets(hcSim *s);
void HC_Step(hcSim *s);
/* the game profile (hcFeel); all zero = Route A */
void HC_SetFeel(hcSim *s, const hcFeel *f);
/* the chain model (hcFlow); all zero = Route A */
void HC_SetFlow(hcSim *s, const hcFlow *f);
/* Bone frames from the solved particles: each cone's parent bone is turned (minimal rotation about its head) to aim
 * at the solved child, root to tip, then each mirror target takes its driver's local rotation delta.
 * anim: nBones animated world matrices (reference frame); posed: output, same layout. */
void HC_Pose(const hcSim *s, const hc_real *anim, hc_real *posed);
/* the same from given particle positions (reference frame) instead of the solver's (render-rate interpolation) */
void HC_PoseFrom(const hcSim *s, const hc_real (*pos)[3], const hc_real *anim, hc_real *posed);

#ifdef __cplusplus
}
#endif

#endif
