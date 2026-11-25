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
/*
SkirtSim: a position-based garment solver for a skinned skirt (portable C99).

This is a line-for-line port of ClothResearch/skirt/skirt_sim.js. It has no
engine or Quake headers and keeps no global state; one allocation block is
made at create time and nothing is allocated in SkirtSim_Step.

Method (all public literature):
  classic PBD (Mueller et al. 2007) with substeps (Macklin et al. 2019),
  edge / dihedral-bending / area constraints, long-range attachments
  (Kim et al. 2012) with geodesic rest lengths (Dijkstra), a max-distance
  sphere, an animation drive, global damping, drag/lift (Keckeisen 2004),
  and one-sided collision with the skinned body mesh found through a
  spatial hash (Teschner et al. 2003) and closest points on triangles
  (Ericson 2005), with Coulomb friction (Bridson et al. 2002), per-particle
  tracking of the nearest collider vertex for deep / fast contacts, and
  capsules on the forearms and hands.

The simulation runs in the frame of a reference bone (the pelvis). How much
of that bone's motion the cloth feels is the calibration problem (knobs
inertiaModel, linVelScale*, angVelScale, fictScale ...). Optional limits for
game input (all 0 = off): maxFeltAcc caps the felt linear acceleration (the
excess kick is carried rigidly with the body, never felt), maxFeltAngAcc
rate-limits the frame angular velocity the cloth uses (the rest turns rigidly), and
flareCone keeps each dynamic particle within a cone about the frame's down axis
whose apex is its tether anchor (SPEC Sec 4.5, 7.6).

Units cm, s, kg; +Z up. Matrices follow the row-vector convention: a point
maps as [x y z 1] * M, translation in elements 12..14 (the same 16 floats as
a column-major column-vector matrix).

Precision: skirt_real is float unless SKIRT_REAL_DOUBLE is defined. Per-particle
state and the constraint maths use skirt_real; the per-frame frame kinematics
(the reference frame, its velocities, build-time geodesics and the stiffness
mapping) always run in double. Do not compile with fast-math or floating point
contraction (/fp:precise, -ffp-contract=off) if you want it to match the
JavaScript reference.

The asset arrays are referenced, not copied, and must outlive the solver.
*/
#ifndef SKIRT_SIM_H
#define SKIRT_SIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SKIRT_REAL_DOUBLE
typedef double skirt_real;
#else
typedef float skirt_real;
#endif

/* ---- knobs. X(name, default). All numeric so a slider can drive any of them.
Enumerations: inertiaModel 0=A (local+fictitious) 1=B (partial carry)
  linAxes 0=local 1=component; fictBase 0=scaled 1=raw 2=removed
  linearKick 0=impulse 1=spread; stiffMapping 0=pbdN 1=raw
  bendModel 0=dihedral 1=spring; animDriveMapMode 0=map(absent->1) 1=one 2=off
  aeroFrameVel 0=none 1=felt 2=full; frictionModel 0=velocity 1=position
  contactEvery 0=substep 1=iteration; order 0=collision last 1=tether last
  2=drive/tether/sphere/collision before edges 3=drive first, collision last */
#define SKIRT_PARAM_LIST(X) \
	/* ---- frame / inertia ---- */ \
	X(inertiaModel, 1)               /* 0 = A (local + fictitious), 1 = B (partial carry) */ \
	X(linVelScaleX, 0.7) X(linVelScaleY, 0.7) X(linVelScaleZ, 0.9) \
	X(linAxes, 0) \
	X(angVelScale, 0.78)             /* 0.75 before the collision changes */ \
	X(fictScale, 0.5) \
	X(fictBase, 1)                   /* fictitious forces from the raw pelvis angular velocity */ \
	X(linearKick, 1) \
	X(maxFrameVel, 2000) X(maxFrameAngVel, 30) \
	X(maxFeltAcc, 0)                 /* cm/s^2: largest change rate of the felt frame velocity (0 = off) */ \
	X(maxFeltAngAcc, 0)              /* deg/s^2: same for the frame angular velocity, before angVelScale (0 = off) */ \
	X(rotateVel, 1)                 /* model B: rotate the velocities with the felt rotation */ \
	X(localSpace, 1) \
	/* ---- solver ---- */ \
	X(iterations, 2) X(maxIterations, 10) X(substeps, 2) \
	X(stiffScale_edge, 1) X(stiffScale_bend, 1) X(stiffScale_area, 1) X(stiffScale_tether, 1) \
	X(stiffMapping, 0) \
	X(bendModel, 0) \
	X(bendPasses, 1) X(edgePasses, 1) /* Gauss-Seidel sweeps of the bending / edge sets per iteration */ \
	X(order, 0) \
	X(targetLerp, 1)                 /* 0: kinematic targets jump to the new frame at the first substep */ \
	/* ---- attachments, drive ---- */ \
	X(tetherScale, 1.1) X(tetherK, 1) X(geodesic, 1) \
	X(maxDistScale, 0.85) X(eps_kin, 0.01) /* 0.9 before the collision changes */ \
	X(flareCone, 0)                  /* deg: flare cap, cone about the frame's down axis at each tether anchor (0 = off) */ \
	X(animDriveStiffness, 0.15) X(adStiffScale, 0.03) \
	X(animDriveDamping, 0.3) X(adDampScale, 0.015) \
	X(animDriveMapMode, 0) \
	X(adPerSubstep, 0)               /* 1: drive spring once per substep instead of every iteration */ \
	/* ---- damping, mass, air ---- */ \
	X(damping, 0.02) X(dampExp, 1) X(dampNorm, 1) X(dampRelative, 0) X(localDamping, 0) \
	X(massScale, 1) X(gravityScale, 1.05) \
	X(fluidDensity, 1.225) X(dragScale, 1) X(liftScale, 1) X(aeroFrameVel, 2) \
	/* ---- collision ---- */ \
	X(thickness, 2.2)                /* the asset's configured collision thickness */ \
	X(friction, 16) X(frictionModel, 1) X(contactEvery, 0) \
	X(backDepth, 9) X(colliderLerp, 1) /* deepest point (cm) still pushed out; safe with tracking */ \
	X(trackContacts, 1)              /* track each particle's nearest collider vertex across substeps (walk) */ \
	X(reacquire, 1)                  /* 1: re-test non-contacts in the last collision pass, 2: in every pass */ \
	X(reacquireDepth, 0)             /* ... and push those whose distance is below reacquireDepth * thickness */ \
	X(contactMargin, 0)              /* keep contacts this far (cm) outside the thickness (speculative) */ \
	X(resetProject, 1)               /* project the skinned start pose out of the body on reset */ \
	X(handCollision, 1)              /* forearm / hand / finger capsules from the arm bones */ \
	X(handThickness, 0.5)            /* cm of cloth offset around the capsules */ \
	X(collideEvery, 0)               /* 0: collision projection only in the last iteration */ \
	X(flipNormals, 0) X(edgeNormal, 0) X(cellSize, 2.5) \
	X(sidedContacts, 0)              /* 1: a particle keeps the face it was outside of; if a limb swallows it, it leaves there */ \
	X(sideDepth, 20)                 /* cm: deepest point pushed back out through its remembered face */ \
	X(coneBody, 0)                   /* 1: the flare cone yields to the body: a touching particle may keep its angle ... */ \
	X(coneRelax, 0.3)                /* ... and that allowance eases back to flareCone with this time constant (s) */

typedef struct skirt_params_s {
#define SKIRT_X_FIELD(name, def) skirt_real name;
	SKIRT_PARAM_LIST(SKIRT_X_FIELD)
#undef SKIRT_X_FIELD
} skirt_params_t;

/* Fill with the DEFAULTS table. */
void SkirtParams_Defaults(skirt_params_t *p);

/* DEFAULTS: one row per knob, in the order of SKIRT_PARAM_LIST. offset is the field's byte
offset in skirt_params_t. */
typedef struct {
	const char *key;
	double def;
	size_t offset;
} skirt_param_info_t;

/* PARAM_UI: the knobs that have a slider (digits < 0: unspecified; reset: needs a reset/rebuild). */
typedef struct {
	const char *key;
	const char *label;
	double min, max, step;
	int digits;
	int reset;
} skirt_param_ui_t;

/* *count receives the length. */
const skirt_param_info_t *SkirtParams_Table(int *count);
const skirt_param_ui_t *SkirtParams_UiTable(int *count);
/* Lookup by key; NULL when unknown. "linVelScale" is not a table row (see SkirtSim_SetParam). */
const skirt_param_info_t *SkirtParams_Find(const char *key);

/* ---- asset: skeleton agnostic ---- */

/* The solver config the JS reads from asset.meta.config. */
typedef struct {
	double gravity[3];            /* cm/s^2 in component space */
	double density;               /* kg/m^2 */
	double minPerParticleMass;    /* kg */
	double edge, bend, area;      /* stiffness [0..1] */
	double tetherStiffness;
	double drag, lift;
} skirt_config_t;

/* gravity {0,0,-980.665}, density 0.35, minPerParticleMass 1e-4, stiffnesses 1, drag = lift = 0.035 */
void SkirtConfig_Defaults(skirt_config_t *c);

typedef struct {
	/* skeleton */
	int nBones;
	const float *refInv;          /* nBones*16: inverse reference pose per bone (row-vector 4x4) */
	int refBone;                  /* bone whose motion defines the sim frame (pelvis) */

	/* cloth mesh, reference pose */
	int nVerts, nTris;
	const float *pos;             /* nVerts*3 */
	const uint32_t *tri;          /* nTris*3 */
	const float *maxDist;         /* nVerts; <= eps_kin pins the particle to its skinned pose */
	const float *animDrive;       /* nVerts or NULL (negative entry = map absent) */
	int nInfl;                    /* influences per vertex in skinBone/skinW (12 in the Chaos dump) */
	const int32_t *skinBone;      /* nVerts*nInfl, -1 = unused */
	const float *skinW;           /* nVerts*nInfl */

	/* optional body collider (nColVerts = 0 for none) */
	int nColVerts, nColTris;
	const float *colPos;          /* nColVerts*3 */
	const uint32_t *colTri;       /* nColTris*3; (c-a) x (b-a) points out of the body */
	const int32_t *colSkinBone;   /* nColVerts*nInfl */
	const float *colSkinW;

	/* optional capsules (nCapsules = 0 for none): segment between the bind-pose
	origins of bones capBoneA[i] and capBoneB[i], radius capRadius[i] cm */
	int nCapsules;
	const int32_t *capBoneA, *capBoneB;
	const double *capRadius;

	skirt_config_t config;
} skirt_asset_t;

/* ---- solver ---- */

typedef struct skirt_sim skirt_sim_t;

typedef struct {
	int contacts, handContacts, tethersActive;   /* of the last step */
	int coneCapped;                              /* flare-cone projections in the last step */
	/* diagnostics (r_clothDump; no effect on the state): */
	double coneDeg;                              /* flare-cone angle of the last step (deg, 0 = off) */
	double yawRate;                              /* reserved (0) */
	double feltAcc, feltAccCut;                  /* model B: felt linear acceleration |Vs - D VsOld|/dt and the part
	                                                carried by maxFeltAcc (cm/s^2) */
	double angVel, angVelUsed;                   /* |raw frame angular velocity|, |after maxFeltAngAcc| (deg/s) */
	double ms;                                   /* not measured here; for the caller's clock */
	int nan;                                     /* a sampled position was not finite */
	int teleport;                                /* the last step's frame was past maxFrameVel / maxFrameAngVel: its felt
	                                                motion zeroed (diagnostics only) */
	int steps;
} skirt_stats_t;

/* Bytes of the single block SkirtSim_Create allocates for this asset. 0 if the asset is invalid. */
size_t SkirtSim_RequiredSize(const skirt_asset_t *asset);
/* params may be NULL (defaults). Returns NULL on an invalid asset or out of memory. */
skirt_sim_t *SkirtSim_Create(const skirt_asset_t *asset, const skirt_params_t *params);
/* Same, in caller memory of at least SkirtSim_RequiredSize bytes (8-byte aligned or better). The
block is not freed by SkirtSim_Destroy. */
skirt_sim_t *SkirtSim_CreateInPlace(const skirt_asset_t *asset, const skirt_params_t *params, void *mem, size_t size);
void SkirtSim_Destroy(skirt_sim_t *sim);

/* mats: nBones*16 floats, skinning matrices refInv_b * pose_b (row-vector). */
void SkirtSim_Reset(skirt_sim_t *sim, const float *mats);
/* The first call (or one after a failed state) resets instead of stepping. dt in seconds. */
void SkirtSim_Step(skirt_sim_t *sim, skirt_real dt, const float *mats);
/* Particle positions in the asset's (component) space, nVerts*3 floats. */
void SkirtSim_Read(const skirt_sim_t *sim, float *out);

/* Returns 0 on success, -1 for an unknown key or a non-finite value. "linVelScale" sets X, Y and Z together.
Values are clamped to +-1e6, and the loop counts (iterations, maxIterations, substeps, edgePasses, bendPasses,
tetherK) to small ranges. Changing eps_kin, massScale, tetherK or geodesic rebuilds mass, pins and tethers
(no allocation). */
int SkirtSim_SetParam(skirt_sim_t *sim, const char *key, double value);
/* Returns 0 on success, -1 for an unknown key. */
int SkirtSim_GetParam(const skirt_sim_t *sim, const char *key, double *value);
const skirt_params_t *SkirtSim_Params(const skirt_sim_t *sim);
const skirt_stats_t *SkirtSim_Stats(const skirt_sim_t *sim);

int SkirtSim_NumVerts(const skirt_sim_t *sim);
/* 1 if particle i is pinned to its skinned pose. */
int SkirtSim_IsKinematic(const skirt_sim_t *sim, int i);

/* ---- split stepping (a GPU or other external particle solver) ----
SkirtSim_StepFrame does the frame part of SkirtSim_Step / SkirtSim_Reset on this solver (the reference frame,
its felt motion, the clamps, the stiffness mapping and the capsule ends) and writes what the particle part
needs into a skirt_frame_t; it never touches the particle state. A solver used this way must only be stepped
through SkirtSim_StepFrame (SkirtSim_Step would advance the frame twice). SkirtSim_Topology exports the arrays
built at create time (and by parameter changes that rebuild: buildSerial changes) for the external solver. */
typedef struct {
	int reset;                  /* 1: a reset; F, the capsules and mats describe the pose, the rest is unused */
	int nSub, nIt;
	float dt, h;
	float F[12];                /* sim frame: rows 0..2 the axes, 9..11 the origin (component space) */
	float D[9], dvec[3];        /* model B boundary transform x' = D x - dvec (velocities: D only) */
	int carry;                  /* model B: add carryV to the dynamic velocities after the transform */
	float carryV[3];
	int kickImpulse;            /* model A, linearKick 0: one velocity change kickV + kickO x r per frame */
	float kickV[3], kickO[3];
	float spread;               /* model A, linearKick 1: the kick spread as an acceleration (1/dt), else 0 */
	float gL[3], wf[3];         /* gravity, fictitious angular velocity (sim frame) */
	float aeroLin[3], aeroAng;  /* frame velocity fractions the air sees */
	float VL[3], OL[3];         /* raw frame velocity / angular velocity (local axes) */
	float kEdge, kBend, kArea, kTether, kAD, dAD, dampF;
	int adUseMap;
	float coneCos, coneAxis[3]; /* flare cone (axis in the frame the particles live in) */
	float windL[3];             /* the air's own velocity (wind, SkirtSim_SetWind) in the sim frame */
} skirt_frame_t;

/* The frame part of the next step. The first call (or the first after SkirtSim_Invalidate) is a reset.
capOld/capNew (nCapsules*6 each, may be NULL without capsules) receive the capsule ends at the step's start
and end in the sim frame. Returns 0 if there is nothing to do (dt <= 0 on a ready solver). */
int SkirtSim_StepFrame(skirt_sim_t *sim, skirt_real dt, const float *mats, skirt_frame_t *out, float *capOld, float *capNew);
/* Force the next SkirtSim_StepFrame to be a reset. */
void SkirtSim_Invalidate(skirt_sim_t *sim);
/* The next SkirtSim_Step / SkirtSim_StepFrame only: its pose took poseDt seconds to get here from the previous
step's, not dt (a caller that ran fewer steps than the time that passed, rd-common/sim_frame.h). The frame's linear
and angular velocity are then measured over poseDt, so they stay the real ones, and the step still integrates dt:
the felt displacement at the frame boundary is the felt velocity over dt, the rest of the motion carries the cloth
rigidly. 0 (or never called) = dt. */
void SkirtSim_SetPoseDt(skirt_sim_t *sim, double poseDt);
/* The air's velocity (wind) in component space (the frame mats are in; cm/s), from the next step on; 0 = still air.
The aero term (drag and lift) then sees each triangle's velocity relative to that air. */
void SkirtSim_SetWind(skirt_sim_t *sim, const double windW[3]);
/* A pose between two (mats as SkirtSim_Step takes them: nBones*16, row-vector, rows 0..2 the axes, 12..14 the origin)
for a step that falls between two frames: the reference bone's frame is lerped (origin straight, axes lerped and
re-orthonormalised) and every bone is lerped in that frame, then put back. Lerping the world matrices themselves
is only right for a small turn: across a long frame of a spinning body each bone's origin cuts its own chord and the
limbs fold toward the axis. out may be neither a nor b. */
void SkirtPose_Lerp(float *out, const float *a, const float *b, float t, int nBones, int refBone);
/* Diagnostic: dynamic particles behind the collider surface by more than depth (cm) in the last step's pose, by
the closest triangle around the nearest collider vertex (a brute-force vertex search: tools and r_simCheck only;
CPU solver only, the split stepping keeps no particles here). stride > 1 looks at every stride-th dynamic particle
and counts those. The deepest goes to maxDepth (may be NULL). */
int SkirtSim_CountInside(const skirt_sim_t *sim, double depth, int stride, double *maxDepth);
/* The current sim frame (rows 0..2 the axes, 9..11 the origin), as SkirtSim_Read uses it. */
void SkirtSim_GetFrame(const skirt_sim_t *sim, double F[12]);

typedef struct {
	int N, NT, NE, NB, NTet, M, cNT, nDyn, nCaps, nBones;
	unsigned buildSerial;
	const float *X0, *maxDist, *adMap, *skinW, *cPos0, *cSkinW;
	const uint32_t *tri, *e0, *e1, *bq, *tetStart, *tetAnchor, *dynList;
	const uint32_t *skinStart, *skinBone, *cSkinStart, *cSkinBone;
	const uint32_t *cTri, *vtStart, *vtList, *vvStart, *vvList;
	const skirt_real *eLen, *bPhi0, *bLen0, *A0, *tetLen, *w, *mass, *vRad, *capR;
	const uint8_t *kin;
	skirt_real cMaxEdge;
} skirt_topology_t;

void SkirtSim_Topology(const skirt_sim_t *sim, skirt_topology_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SKIRT_SIM_H */
