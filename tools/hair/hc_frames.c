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

/* hc_frames.c -- the game's per-frame hair stepping offline (rd-common/tr_hair.cpp Hair_Advance with
 * rd-common/sim_frame.h), for the cases a step replay cannot show: long frames and off-screen gaps, where more steps
 * are owed than r_hairMaxSteps allows. Build (from the repo root):
 *   gcc -std=c99 -O2 -ffp-contract=off -o hc_frames.exe tools/hair/hc_frames.c codemp/rd-common/hairchains/hair_sim.c -lm
 *
 *   hc_frames <asset.chains> <reel.hcr> [--fps F] [--hitch atMs:lenMs[,atMs:lenMs...]] [--hitch-every periodMs:lenMs]
 *             [--max-steps N] [--mode legacy|spread|carry] [--carry-frames X] [--window ms] [--tol cm] [--csv steps.csv]
 *             [--honour-inverse] [--feel-rad ...] [--flow ...]     (as hc_replay; hcd_to_hcr.py prints them)
 *
 * The reel is the motion: its steps are samples of the entity's world matrix and the solver inputs at the asset's dt.
 * Render frames run at --fps; a hitch is one frame of lenMs starting at atMs (a hitch, or a character the main view
 * culled for that long: the front end sees the same thing). Each frame's inputs are the reel sampled at the frame
 * time; steps are planned by Sim_PlanSteps and driven by inputs lerped between the last frame's and this one's, as
 * the game does. --mode legacy is the earlier stepping (time dropped at the frame's end); carry (default)
 * is the fix, carrying up to --carry-frames (4) frames' worth of steps to the next frames; spread is carry with
 * nothing carried (the whole frame stretched over its steps).
 *
 * Reported against a run of the same frames with no step limit (nothing dropped): the per-step pose jump, the felt
 * velocity error and acceleration, overlapping collision pairs, and the drawn-state error after each hitch.
 * Record: docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../codemp/rd-common/hairchains/hair_sim.h"
#include "../../codemp/rd-common/sim_frame.h"
#include "hc_reel.h"

static int argHonourInverse;
static hcFeel argFeel;
static hcFlow argFlow;

static float getf(const unsigned char *b, long o) { float v; memcpy(&v, b + o, 4); return v; }
#define geti reel_geti

/* the solver part of an HCH1 v2 asset */
static void load_asset(const char *path, hcAsset *a)
{
	long n, o = 128;
	unsigned char *b = reel_slurp(path, &n);
	if (memcmp(b, "HCH1", 4) || geti(b, 4) != 2) {
		fprintf(stderr, "%s: not an HCH1 asset\n", path);
		exit(1);
	}
	memset(a, 0, sizeof(*a));
	a->nParticles = geti(b, 12); a->nCones = geti(b, 16); a->nLinks = geti(b, 20); a->nReceivers = geti(b, 24);
	a->nRepulsors = geti(b, 28); a->nPairs = geti(b, 32); a->nBones = geti(b, 36); a->nMirrors = geti(b, 40);
	a->hostBone = geti(b, 44); a->substeps = geti(b, 48); a->lateralIter = geti(b, 52); a->collisionIter = geti(b, 56);
	a->dt = getf(b, 60);
	for (int k = 0; k < 3; k++)
		a->gravity[k] = getf(b, 64 + 4 * k);
	a->unitsPerCm = getf(b, 76);
	a->flags = geti(b, 120);
	a->particles = calloc(a->nParticles ? a->nParticles : 1, sizeof(hcParticle));
	for (int i = 0; i < a->nParticles; i++, o += 48) {
		hcParticle *p = &a->particles[i];
		p->bone = geti(b, o); p->group = geti(b, o + 4);
		p->radius = getf(b, o + 8); p->gravityScale = getf(b, o + 12); p->damping = getf(b, o + 16);
		p->velocityDamping = getf(b, o + 20); p->constraintDamping = getf(b, o + 24); p->friction = getf(b, o + 28);
		p->bounciness = getf(b, o + 32); p->parkRate = getf(b, o + 36); p->fictitious = getf(b, o + 40);
		p->centrifugal = getf(b, o + 44);
	}
	a->cones = calloc(a->nCones ? a->nCones : 1, sizeof(hcCone));
	for (int i = 0; i < a->nCones; i++, o += 40) {
		hcCone *c = &a->cones[i];
		c->child = geti(b, o); c->parent = geti(b, o + 4); c->anchorBone = geti(b, o + 8);
		c->limit = getf(b, o + 12); c->inner = getf(b, o + 16); c->outer = getf(b, o + 20); c->stretch = getf(b, o + 24);
		c->compress = getf(b, o + 28); c->parentStrength = getf(b, o + 32); c->rest = getf(b, o + 36);
	}
	a->links = calloc(a->nLinks ? a->nLinks : 1, sizeof(hcLink));
	for (int i = 0; i < a->nLinks; i++, o += 20) {
		hcLink *l = &a->links[i];
		l->a = geti(b, o); l->b = geti(b, o + 4); l->stretch = getf(b, o + 8); l->compress = getf(b, o + 12);
		l->rest = getf(b, o + 16);
	}
	a->receivers = calloc(a->nReceivers ? a->nReceivers : 1, sizeof(hcReceiver));
	for (int i = 0; i < a->nReceivers; i++, o += 20) {
		hcReceiver *r = &a->receivers[i];
		r->n = geti(b, o); r->p[0] = geti(b, o + 4); r->p[1] = geti(b, o + 8); r->r[0] = getf(b, o + 12);
		r->r[1] = getf(b, o + 16);
	}
	a->repulsors = calloc(a->nRepulsors ? a->nRepulsors : 1, sizeof(hcRepulsor));
	for (int i = 0; i < a->nRepulsors; i++, o += 48) {
		hcRepulsor *r = &a->repulsors[i];
		r->n = geti(b, o); r->gameBone[0] = geti(b, o + 4); r->gameBone[1] = geti(b, o + 8); r->flags = geti(b, o + 12);
		for (int k = 0; k < 3; k++) {
			r->bind[0][k] = getf(b, o + 16 + 4 * k);
			r->bind[1][k] = getf(b, o + 28 + 4 * k);
		}
		r->r[0] = getf(b, o + 40); r->r[1] = getf(b, o + 44);
	}
	if (argHonourInverse)
		a->flags |= HC_ASSET_HONOUR_INVERSE;
	a->pairs = calloc(a->nPairs ? a->nPairs : 1, sizeof(*a->pairs));
	for (int i = 0; i < a->nPairs; i++, o += 8) {
		a->pairs[i][0] = geti(b, o); a->pairs[i][1] = geti(b, o + 4);
	}
	a->bones = calloc(a->nBones ? a->nBones : 1, sizeof(hcBone));
	for (int i = 0; i < a->nBones; i++, o += 100) {
		memcpy(a->bones[i].name, b + o, 32);
		a->bones[i].parent = geti(b, o + 32);
		for (int k = 0; k < 16; k++)
			a->bones[i].bind[k] = getf(b, o + 36 + 4 * k);
	}
	a->mirrors = calloc(a->nMirrors ? a->nMirrors : 1, sizeof(*a->mirrors));
	for (int i = 0; i < a->nMirrors; i++, o += 8) {
		a->mirrors[i][0] = geti(b, o); a->mirrors[i][1] = geti(b, o + 4);
	}
	free(b);
}

/* ------------------------------------------------------------------ a frame's inputs */

typedef struct {
	double W[16];
	double *targets, *anchors, *rep;   /* nP*3, nC*3, nR*6 */
} pose_t;

static void pose_alloc(pose_t *p, const reel_t *r)
{
	p->targets = malloc(sizeof(double) * (r->nP * 3 + 1));
	p->anchors = malloc(sizeof(double) * (r->nC * 3 + 1));
	p->rep = malloc(sizeof(double) * (r->nR * 6 + 1));
}

/* Gram-Schmidt on the rotation columns, as the glue's M4_Orthonormalise */
static void orthonormalise(double *m)
{
	double x[3], y[3], z[3], l, d;
	l = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	for (int k = 0; k < 3; k++) x[k] = l > 0 ? m[k] / l : 0;
	d = x[0] * m[4] + x[1] * m[5] + x[2] * m[6];
	for (int k = 0; k < 3; k++) y[k] = m[4 + k] - d * x[k];
	l = sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	for (int k = 0; k < 3; k++) y[k] = l > 0 ? y[k] / l : 0;
	z[0] = x[1] * y[2] - x[2] * y[1]; z[1] = x[2] * y[0] - x[0] * y[2]; z[2] = x[0] * y[1] - x[1] * y[0];
	if (z[0] * m[8] + z[1] * m[9] + z[2] * m[10] < 0)
		for (int k = 0; k < 3; k++) z[k] = -z[k];
	for (int k = 0; k < 3; k++) { m[k] = x[k]; m[4 + k] = y[k]; m[8 + k] = z[k]; }
}

static void lerp_n(double *o, const double *a, const double *b, double t, int n)
{
	for (int k = 0; k < n; k++)
		o[k] = a[k] + t * (b[k] - a[k]);
}

static void pose_lerp(pose_t *o, const pose_t *a, const pose_t *b, double t, const reel_t *r)
{
	lerp_n(o->W, a->W, b->W, t, 16);
	orthonormalise(o->W);
	lerp_n(o->targets, a->targets, b->targets, t, r->nP * 3);
	lerp_n(o->anchors, a->anchors, b->anchors, t, r->nC * 3);
	lerp_n(o->rep, a->rep, b->rep, t, r->nR * 6);
}

/* the reel at time t (seconds from its first step) */
static void pose_at(pose_t *o, const reel_t *r, double dt, double t)
{
	double f = t / dt;
	int i = (int)floor(f);
	pose_t a, b;
	if (i < 0) { i = 0; f = 0; }
	if (i >= r->nS - 1) { i = r->nS - 2; f = r->nS - 1; }
	a.targets = r->targets + (size_t)i * r->nP * 3; a.anchors = r->anchors + (size_t)i * r->nC * 3; a.rep = r->rep + (size_t)i * r->nR * 6;
	b.targets = r->targets + (size_t)(i + 1) * r->nP * 3; b.anchors = r->anchors + (size_t)(i + 1) * r->nC * 3; b.rep = r->rep + (size_t)(i + 1) * r->nR * 6;
	memcpy(a.W, r->ref + (size_t)i * 16, sizeof(a.W));
	memcpy(b.W, r->ref + (size_t)(i + 1) * 16, sizeof(b.W));
	pose_lerp(o, &a, &b, f - i, r);
}

static void set_inputs(hcSim *s, const pose_t *p, const reel_t *r)
{
	for (int i = 0; i < r->nP; i++)
		for (int k = 0; k < 3; k++)
			s->target[i][k] = (hc_real)p->targets[i * 3 + k];
	for (int c = 0; c < r->nC; c++)
		for (int k = 0; k < 3; k++)
			s->anchor[c][k] = (hc_real)p->anchors[c * 3 + k];
	for (int q = 0; q < r->nR; q++)
		for (int e = 0; e < 2; e++)
			for (int k = 0; k < 3; k++)
				s->repPoint[q][e][k] = (hc_real)p->rep[(q * 2 + e) * 3 + k];
}

static void begin(hcSim *s, const pose_t *p, int frame, double poseDt)
{
	hc_real m[16];
	for (int k = 0; k < 16; k++)
		m[k] = (hc_real)p->W[k];
	HC_BeginFrameDt(s, m, frame, (hc_real)poseDt);
}

/* ------------------------------------------------------------------ one run */

typedef struct {
	int frames, steps, capped;
	double dropped;
	double maxJump;        /* largest move of an animated particle target in one step, model space (units) */
	double maxRootJump;    /* largest move of the entity origin in one step (units) */
	double maxPoseMs;      /* most pose time one step covered */
	double maxVelErr;      /* |the frame velocity the solver measured - the one its poses moved at| (units/s), largest */
	double maxVelRatio;    /* the pose time a step really covered over the pose time the solver divided by, largest */
	double maxFeltAcc;     /* largest felt linear acceleration (units/s^2) */
	double maxPartStep;    /* largest particle move in one step (units) */
	long insideSum;        /* overlapping pairs, summed over steps */
	int insideMax;
	double insideDepth;    /* deepest overlap (units) */
} runStats_t;

typedef struct {
	int nFrames;
	double *t;             /* frame times (s) */
	unsigned char *hitch;  /* this frame is a hitch */
} sched_t;

/* runs the schedule; drawn[frame][nP*3] receives the newest step state of each frame in model space */
static void run(const hcAsset *a, const reel_t *r, const sched_t *sc, int maxSteps, int mode, double carryFrames, double tolUnits,
	double *drawn, runStats_t *st, FILE *csv, const char *tag)
{
	hcSim s;
	pose_t prev, cur, stp;   /* prev: the stored previous pose (the last frame's, or the solver's when it is behind) */
	const double step = a->dt;
	double acc = 0, baseLag = 0, lastT = sc->t[0], prevFrame = 0;
	int counter = 0;
	double (*prevTarget) = malloc(sizeof(double) * (r->nP * 3 + 1));
	double lastOrigin[3], lastPoseT = sc->t[0];

	memset(st, 0, sizeof(*st));
	pose_alloc(&prev, r); pose_alloc(&cur, r); pose_alloc(&stp, r);
	HC_Init(&s, a);
	HC_SetFeel(&s, &argFeel);
	HC_SetFlow(&s, &argFlow);

	/* Hair_Reset at the first frame */
	pose_at(&cur, r, step, sc->t[0]);
	counter += 2;
	begin(&s, &cur, counter, 0);
	begin(&s, &cur, counter, 0);
	set_inputs(&s, &cur, r);
	HC_ResetToTargets(&s);
	counter--;
	memcpy(prevTarget, cur.targets, sizeof(double) * r->nP * 3);
	for (int k = 0; k < 3; k++) lastOrigin[k] = cur.W[12 + k];
	for (int p = 0; p < r->nP; p++)
		for (int k = 0; k < 3; k++)
			drawn[p * 3 + k] = s.pos[p][k];
	{ pose_t tmp = prev; prev = cur; cur = tmp; }   /* Hair_Reset: the reset pose is the previous one */

	for (int f = 1; f < sc->nFrames; f++) {
		simStepPlan_t plan;
		const double frame = sc->t[f] - lastT, baseT = lastT - baseLag;
		pose_at(&cur, r, step, sc->t[f]);
		Sim_PlanSteps(&plan, acc, baseLag, frame, step, maxSteps, mode, Sim_Carry(carryFrames, maxSteps, step, frame, prevFrame));
		prevFrame = frame;
		st->frames++;
		st->capped += plan.capped;
		st->dropped += plan.dropped;
		for (int k = 0; k < plan.n; k++) {
			const double alpha = Sim_StepAlpha(&plan, k);
			const double poseDt = Sim_StepPoseDt(&plan);
			double jump = 0, root, velErr, facc, pstep = 0, depth = 0, tv[3], d;
			const double poseT = baseT + alpha * plan.span, poseMs = (poseT - lastPoseT) * 1000;
			int inside;
			pose_lerp(&stp, &prev, &cur, alpha, r);
			begin(&s, &stp, ++counter, poseDt);
			set_inputs(&s, &stp, r);
			HC_Step(&s);
			for (int i = 0; i < r->nP * 3; i += 3) {
				d = sqrt((stp.targets[i] - prevTarget[i]) * (stp.targets[i] - prevTarget[i])
					+ (stp.targets[i + 1] - prevTarget[i + 1]) * (stp.targets[i + 1] - prevTarget[i + 1])
					+ (stp.targets[i + 2] - prevTarget[i + 2]) * (stp.targets[i + 2] - prevTarget[i + 2]));
				if (d > jump) jump = d;
			}
			memcpy(prevTarget, stp.targets, sizeof(double) * r->nP * 3);
			root = sqrt((stp.W[12] - lastOrigin[0]) * (stp.W[12] - lastOrigin[0]) + (stp.W[13] - lastOrigin[1]) * (stp.W[13] - lastOrigin[1])
				+ (stp.W[14] - lastOrigin[2]) * (stp.W[14] - lastOrigin[2]));
			/* the velocity the entity really had between the solver's last pose and this one (world) */
			for (int c = 0; c < 3; c++)
				tv[c] = poseMs > 0 ? (stp.W[12 + c] - lastOrigin[c]) / (poseMs * 0.001) : 0;
			{
				/* what the solver measured for this step (world): its last two origins over the pose time it was given */
				double mv[3] = { 0, 0, 0 };
				if (s.nOrigin >= 2)
					for (int c = 0; c < 3; c++)
						mv[c] = (s.originHist[s.nOrigin - 1][c] - s.originHist[s.nOrigin - 2][c]) / s.histDt[s.nOrigin - 1];
				velErr = sqrt((mv[0] - tv[0]) * (mv[0] - tv[0]) + (mv[1] - tv[1]) * (mv[1] - tv[1]) + (mv[2] - tv[2]) * (mv[2] - tv[2]));
			}
			for (int c = 0; c < 3; c++) lastOrigin[c] = stp.W[12 + c];
			if (s.nOrigin >= 2 && poseMs * 0.001 / s.histDt[s.nOrigin - 1] > st->maxVelRatio)
				st->maxVelRatio = poseMs * 0.001 / s.histDt[s.nOrigin - 1];
			facc = sqrt(s.feltAcc[0] * s.feltAcc[0] + s.feltAcc[1] * s.feltAcc[1] + s.feltAcc[2] * s.feltAcc[2]);
			for (int p = 0; p < r->nP; p++) {
				d = sqrt((s.pos[p][0] - s.prev[p][0]) * (s.pos[p][0] - s.prev[p][0]) + (s.pos[p][1] - s.prev[p][1]) * (s.pos[p][1] - s.prev[p][1])
					+ (s.pos[p][2] - s.prev[p][2]) * (s.pos[p][2] - s.prev[p][2]));
				if (d > pstep) pstep = d;
			}
			{
				hc_real dp = 0;
				inside = HC_CountInside(&s, (hc_real)tolUnits, &dp);
				depth = dp;
			}
			st->steps++;
			if (jump > st->maxJump) st->maxJump = jump;
			if (root > st->maxRootJump) st->maxRootJump = root;
			if (poseMs > st->maxPoseMs) st->maxPoseMs = poseMs;
			lastPoseT = poseT;
			if (velErr > st->maxVelErr) st->maxVelErr = velErr;
			if (facc > st->maxFeltAcc) st->maxFeltAcc = facc;
			if (pstep > st->maxPartStep) st->maxPartStep = pstep;
			st->insideSum += inside;
			if (inside > st->insideMax) st->insideMax = inside;
			if (depth > st->insideDepth) st->insideDepth = depth;
			if (csv)
				fprintf(csv, "%s,%d,%.4f,%d,%d,%.5f,%.3f,%.4f,%.4f,%.2f,%.1f,%.4f,%d,%.4f\n", tag, f, sc->t[f] * 1000, k, plan.capped + 2 * plan.stretched, alpha,
					poseMs, jump, root, velErr, facc, pstep, inside, depth);
		}
		/* legacy stepping: the pose the solver was left at is not the pose stored as previous; what the next step
		 * jumps is measured from the solver's own last inputs (prevTarget / lastOrigin), so it shows up there */
		acc = plan.accAfter;
		baseLag = plan.baseLag;
		lastT = sc->t[f];
		if (plan.baseLag > 0) {   /* the solver is behind: keep the pose of its last step */
			pose_lerp(&stp, &prev, &cur, plan.baseAlpha, r);
			{ pose_t tmp = prev; prev = stp; stp = tmp; }
		} else {
			pose_t tmp = prev; prev = cur; cur = tmp;
		}
		for (int p = 0; p < r->nP; p++)
			for (int k = 0; k < 3; k++)
				drawn[((size_t)f * r->nP + p) * 3 + k] = s.pos[p][k];
	}
	HC_Free(&s);
	free(prevTarget);
}

static int parse_pairs(const char *s, double *at, double *len, int max)
{
	int n = 0;
	while (*s && n < max) {
		if (sscanf(s, "%lf:%lf", &at[n], &len[n]) != 2)
			break;
		n++;
		s = strchr(s, ',');
		if (!s)
			break;
		s++;
	}
	return n;
}

int main(int argc, char **argv)
{
	hcAsset a;
	reel_t r;
	sched_t sc;
	runStats_t stRef, stRun;
	double fps = 144, hitchAt[64], hitchLen[64], everyPeriod = 0, everyLen = 0, windowMs = 1000, tolCm = 0.1, lateMs = 100;
	int nHitch = 0, maxSteps = 4, mode = SIM_STEPS_CARRY;
	double carryFrames = 4;
	const char *modeName = "carry";
	const char *csvPath = NULL;
	FILE *csv = NULL;
	double *drawRef, *drawRun, total;

	if (argc < 3) {
		fprintf(stderr, "usage: hc_frames asset.chains reel.hcr [--fps F] [--hitch atMs:lenMs,...] [--hitch-every periodMs:lenMs] "
			"[--max-steps N] [--mode legacy|spread|carry] [--carry-frames X] [--window ms] [--tol cm] [--csv steps.csv] [--honour-inverse] [--feel-rad ...] [--flow ...]\n");
		return 1;
	}
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atof(argv[++i]);
		else if (!strcmp(argv[i], "--hitch") && i + 1 < argc) nHitch = parse_pairs(argv[++i], hitchAt, hitchLen, 64);
		else if (!strcmp(argv[i], "--hitch-every") && i + 1 < argc) sscanf(argv[++i], "%lf:%lf", &everyPeriod, &everyLen);
		else if (!strcmp(argv[i], "--max-steps") && i + 1 < argc) maxSteps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			modeName = argv[++i];
			if (!strcmp(modeName, "legacy")) mode = SIM_STEPS_LEGACY;
			else if (!strcmp(modeName, "spread")) { mode = SIM_STEPS_CARRY; carryFrames = 0; }
			else if (strcmp(modeName, "carry")) { fputs("--mode legacy|spread|carry", stderr); return 1; }
		}
		else if (!strcmp(argv[i], "--carry-frames") && i + 1 < argc) carryFrames = atof(argv[++i]);
		else if (!strcmp(argv[i], "--window") && i + 1 < argc) windowMs = atof(argv[++i]);
		else if (!strcmp(argv[i], "--tol") && i + 1 < argc) tolCm = atof(argv[++i]);
		else if (!strcmp(argv[i], "--csv") && i + 1 < argc) csvPath = argv[++i];
		else if (!strcmp(argv[i], "--honour-inverse")) argHonourInverse = 1;
		else if (!strcmp(argv[i], "--feel-rad") && i + 1 < argc) {
			float v[9] = { 0 };
			if (sscanf(argv[++i], "%f,%f,%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
				argFeel.on = 1;
				argFeel.linWeight = v[0]; argFeel.maxLinAccG = v[1]; argFeel.linTau = v[2];
				argFeel.eulerWeight = v[3]; argFeel.maxAngAcc = v[4]; argFeel.centWeight = v[5]; argFeel.maxAngVel = v[6];
				argFeel.carryLag = v[7]; argFeel.carryAngLag = v[8];
			}
		} else if (!strcmp(argv[i], "--flow") && i + 1 < argc) {
			float v[4] = { 0 };
			if (sscanf(argv[++i], "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4) {
				argFlow.springDamp = v[0]; argFlow.innerScale = v[1]; argFlow.airDrag = v[2]; argFlow.coneParent = v[3];
			}
		} else {
			fprintf(stderr, "unknown argument %s\n", argv[i]);
			return 1;
		}
	}
	load_asset(argv[1], &a);
	load_reel(argv[2], &r);
	if (r.nP != a.nParticles || r.nC != a.nCones || r.nR != a.nRepulsors) {
		fprintf(stderr, "asset/reel mismatch\n");
		return 1;
	}
	if (r.nS < 3) {
		fprintf(stderr, "reel too short\n");
		return 1;
	}
	if (everyPeriod > 0)
		for (double t = everyPeriod; nHitch < 64 && t + everyLen < (r.nS - 1) * a.dt * 1000; t += everyPeriod) {
			hitchAt[nHitch] = t;
			hitchLen[nHitch++] = everyLen;
		}
	for (int i = 1; i < nHitch; i++)   /* the schedule takes hitches in list order: --hitch and --hitch-every merged by start */
		for (int j = i; j > 0 && hitchAt[j - 1] > hitchAt[j]; j--) {
			const double ta = hitchAt[j], tl = hitchLen[j];
			hitchAt[j] = hitchAt[j - 1]; hitchLen[j] = hitchLen[j - 1];
			hitchAt[j - 1] = ta; hitchLen[j - 1] = tl;
		}

	/* frame times: steady frames, each hitch one long frame */
	total = (r.nS - 1) * (double)a.dt;
	{
		const int cap = (int)(total * fps) + nHitch + 8;
		double t = 0;
		int h = 0;
		sc.t = malloc(sizeof(double) * cap);
		sc.hitch = calloc(cap, 1);
		sc.nFrames = 0;
		while (t <= total && sc.nFrames < cap) {
			sc.t[sc.nFrames++] = t;
			if (h < nHitch && t * 1000 >= hitchAt[h]) {
				t += hitchLen[h++] * 0.001;
				if (t <= total && sc.nFrames < cap)
					sc.hitch[sc.nFrames] = 1;
			} else {
				t += 1.0 / fps;
			}
		}
	}
	drawRef = calloc((size_t)sc.nFrames * r.nP * 3 + 1, sizeof(double));
	drawRun = calloc((size_t)sc.nFrames * r.nP * 3 + 1, sizeof(double));
	if (csvPath) {
		csv = fopen(csvPath, "w");
		if (!csv) {
			fprintf(stderr, "cannot write %s\n", csvPath);
			return 1;
		}
		fprintf(csv, "run,frame,ms,step,capped,alpha,poseMs,targetJump,rootJump,velErr,feltAcc,particleStep,inside,depth\n");
	}
	run(&a, &r, &sc, SIM_PLAN_MAX_STEPS, SIM_STEPS_LEGACY, 0, tolCm * a.unitsPerCm, drawRef, &stRef, csv, "nolimit");
	if (stRef.dropped > 0) {   /* a gap over SIM_PLAN_MAX_STEPS steps: the reference lost time itself */
		fprintf(stderr, "the nolimit reference dropped %.0f ms (a frame owed more than %d steps): it is no reference; "
			"use shorter hitches\n", stRef.dropped * 1000, SIM_PLAN_MAX_STEPS);
		if (csv) fclose(csv);
		return 1;
	}
	run(&a, &r, &sc, maxSteps, mode, carryFrames, tolCm * a.unitsPerCm, drawRun, &stRun, csv, modeName);
	if (csv) fclose(csv);

	/* drawn-state error against the unlimited run, over the window after each hitch */
	{
		double mx = 0, sum = 0, mxAll = 0, mxLate = 0, sumLate = 0;
		long cnt = 0, cntLate = 0;
		int settleWorst = 0;
		for (int f = 0; f < sc.nFrames; f++) {
			if (!sc.hitch[f])
				continue;
			int settled = -1;
			for (int g = f; g < sc.nFrames && (sc.t[g] - sc.t[f]) * 1000 <= windowMs; g++) {
				double fm = 0;
				for (int p = 0; p < r.nP; p++) {
					const double *x = drawRun + ((size_t)g * r.nP + p) * 3, *y = drawRef + ((size_t)g * r.nP + p) * 3;
					const double d = sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
					if (d > fm) fm = d;
					sum += d * d;
					cnt++;
					if ((sc.t[g] - sc.t[f]) * 1000 >= lateMs) {
						if (d > mxLate) mxLate = d;
						sumLate += d * d;
						cntLate++;
					}
				}
				if (fm > mx) mx = fm;
				if (fm > a.unitsPerCm) settled = -1;                 /* over 1 cm */
				else if (settled < 0) settled = (int)((sc.t[g] - sc.t[f]) * 1000);
			}
			if (settled < 0) settled = (int)windowMs;
			if (settled > settleWorst) settleWorst = settled;
		}
		for (int f = 0; f < sc.nFrames; f++)
			for (int p = 0; p < r.nP; p++) {
				const double *x = drawRun + ((size_t)f * r.nP + p) * 3, *y = drawRef + ((size_t)f * r.nP + p) * 3;
				const double d = sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) + (x[2] - y[2]) * (x[2] - y[2]));
				if (d > mxAll) mxAll = d;
			}
		printf("reel %d steps (%.2f s), %d particles, %d pairs; %.0f fps, %d hitches, max steps %d, 1 cm = %.3f units\n",
			r.nS, total, r.nP, a.nPairs, fps, nHitch, maxSteps, a.unitsPerCm);
		printf("%-8s frames %d steps %d capped %d dropped %.0f ms | per step: pose %.1f ms, target jump %.2f cm, root jump %.1f units, "
			"particle move %.2f cm | solver frame velocity: x%.2f of the real one, off by %.1f u/s; felt acc %.0f u/s2 | overlapping pairs: sum %ld max %d deepest %.2f cm\n",
			"nolimit", stRef.frames, stRef.steps, stRef.capped, stRef.dropped * 1000, stRef.maxPoseMs, stRef.maxJump / a.unitsPerCm, stRef.maxRootJump,
			stRef.maxPartStep / a.unitsPerCm, stRef.maxVelRatio, stRef.maxVelErr, stRef.maxFeltAcc, stRef.insideSum, stRef.insideMax, stRef.insideDepth / a.unitsPerCm);
		printf("%-8s frames %d steps %d capped %d dropped %.0f ms | per step: pose %.1f ms, target jump %.2f cm, root jump %.1f units, "
			"particle move %.2f cm | solver frame velocity: x%.2f of the real one, off by %.1f u/s; felt acc %.0f u/s2 | overlapping pairs: sum %ld max %d deepest %.2f cm\n",
			modeName, stRun.frames, stRun.steps, stRun.capped, stRun.dropped * 1000, stRun.maxPoseMs, stRun.maxJump / a.unitsPerCm,
			stRun.maxRootJump, stRun.maxPartStep / a.unitsPerCm, stRun.maxVelRatio, stRun.maxVelErr, stRun.maxFeltAcc, stRun.insideSum, stRun.insideMax,
			stRun.insideDepth / a.unitsPerCm);
		printf("%-8s drawn state against nolimit, %.0f ms after each hitch: max %.2f cm, rms %.2f cm; from %.0f ms after: max %.2f cm, rms %.2f cm; "
			"back within 1 cm after at most %d ms; whole run max %.2f cm\n",
			modeName, windowMs, mx / a.unitsPerCm, cnt ? sqrt(sum / cnt) / a.unitsPerCm : 0.0, lateMs, mxLate / a.unitsPerCm,
			cntLate ? sqrt(sumLate / cntLate) / a.unitsPerCm : 0.0, settleWorst, mxAll / a.unitsPerCm);
	}
	return 0;
}
