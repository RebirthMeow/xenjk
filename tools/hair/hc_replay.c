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

/* hc_replay.c -- offline replay of a hair-chains asset (docs/hair/FORMAT.md) against a reel: one recorded from the
 * Route-A Blender solver (research), or an in-game r_hairDump converted by hcd_to_hcr.py. Build (from the repo root):
 *   gcc -std=c99 -O2 -ffp-contract=off -o hc_replay.exe tools/hair/hc_replay.c codemp/rd-common/hairchains/hair_sim.c \
 *       codemp/rd-common/hairchains/hair_asset.c -lm
 *
 *   hc_replay <asset.chains> <reel.hcr> <out.bin> [--time N] [--honour-inverse]
 *             [--feel lin,maxG,tau,euler,maxAngAcc_deg,cent,maxAngVel_deg[,carryLag,carryAngLag_deg]]   (hcFeel)
 *             [--feel-rad the same nine values, angles in radians as the engine holds them]   (tools/hair/hcd_to_hcr.py prints this)
 *             [--flow springDamp,innerScale,airDrag,coneParent]   (hcFlow)
 *
 * Writes, as little-endian f64 after a 4 x i32 header (nSteps, nParticles, nPose, nBones):
 *   onestep[nSteps][nP][3]  Blender's pre-step state and inputs, one C step (logic parity)
 *   free[nSteps][nP][3]     C running free from the reset (drift)
 *   kin[nSteps][9]          C frame acceleration, angular velocity, local gravity
 *   pose[nPose][nB][16]     HC_Pose on Blender's solved positions
 *   posefree[nPose][nB][16] HC_Pose on the C free run
 * --time N: also time N repetitions of the free run (no output change).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../codemp/rd-common/hairchains/hair_sim.h"
#include "../../codemp/rd-common/hairchains/hair_asset.h"
#include "hc_reel.h"

static int argHonourInverse;
static double argWorldScale = 1;
static int argWorldScaleFix = 1;
static hcFeel argFeel;
static hcFlow argFlow;

/* the game's own reader (hair_asset.c: CRC, index and semantic checks, springs-only assets), so a format
   change is made in one C place; the parsed asset stays alive for the run */
static hairAsset_t replayAsset;

static void load_asset(const char *path, hcAsset *a)
{
	long n;
	char err[256];
	unsigned char *b = reel_slurp(path, &n);
	if (!HairAsset_ParseEx(b, (size_t)n, &replayAsset, err, sizeof err, HAIR_PARSE_OFFLINE)) {
		fprintf(stderr, "%s: %s\n", path, err);
		exit(1);
	}
	free(b);
	*a = replayAsset.a;
	if (argHonourInverse)
		a->flags |= HC_ASSET_HONOUR_INVERSE;
	for (int k = 0; k < 3; k++)	/* a world S times larger: its gravity too */
		a->gravity[k] *= (hc_real)argWorldScale;
}

static void set_inputs(hcSim *s, const reel_t *r, int i)
{
	for (int p = 0; p < r->nP; p++)
		for (int k = 0; k < 3; k++)
			s->target[p][k] = (hc_real)r->targets[(i * r->nP + p) * 3 + k];
	for (int c = 0; c < r->nC; c++)
		for (int k = 0; k < 3; k++)
			s->anchor[c][k] = (hc_real)r->anchors[(i * r->nC + c) * 3 + k];
	for (int q = 0; q < r->nR; q++)
		for (int e = 0; e < 2; e++)
			for (int k = 0; k < 3; k++)
				s->repPoint[q][e][k] = (hc_real)r->rep[((i * r->nR + q) * 2 + e) * 3 + k];
}

static void begin(hcSim *s, const reel_t *r, int i, int frame, int step)
{
	hc_real m[16];
	for (int k = 0; k < 16; k++)
		m[k] = (hc_real)r->ref[i * 16 + k];
	m[12] *= (hc_real)argWorldScale; m[13] *= (hc_real)argWorldScale; m[14] *= (hc_real)argWorldScale;
	if (argWorldScaleFix)
		HC_SetWorldScale(s, (hc_real)argWorldScale);
	/* a step's own pose time (a game dump's capped frames, rd-common/sim_frame.h); the reset's two calls take dt */
	HC_BeginFrameDt(s, m, frame, step ? (hc_real)(r->poseUs[i] * 1e-6) : 0);
}

static void free_run(hcSim *s, const reel_t *r, double *out)
{
	/* main(): capture_rest_lengths + reset_to_pose at frame_start (two update_reference calls), then every step */
	begin(s, r, 0, r->resetFrame, 0);
	begin(s, r, 0, r->resetFrame, 0);
	for (int p = 0; p < r->nP; p++)
		for (int k = 0; k < 3; k++)
			s->target[p][k] = (hc_real)r->init[p * 3 + k];
	HC_ResetToTargets(s);
	if (r->seedPos)	/* a version 2 reel: start from the state the game drew after its pre-roll */
		for (int p = 0; p < r->nP; p++)
			for (int k = 0; k < 3; k++) {
				s->pos[p][k] = (hc_real)r->seedPos[p * 3 + k];
				s->prev[p][k] = (hc_real)r->seedPrev[p * 3 + k];
			}
	for (int i = 0; i < r->nS; i++) {
		begin(s, r, i, r->frame[i], 1);
		set_inputs(s, r, i);
		HC_Step(s);
		if (out)
			for (int p = 0; p < r->nP; p++)
				for (int k = 0; k < 3; k++)
					out[(i * r->nP + p) * 3 + k] = s->pos[p][k];
	}
}

int main(int argc, char **argv)
{
	hcAsset a;
	reel_t r;
	hcSim s;
	FILE *f;
	int reps = 0;
	double *onestep, *freeOut, *kin, *pose, *posefree;
	hc_real *anim, *posed;

	if (argc < 4) {
		fprintf(stderr, "usage: hc_replay asset.chains reel.hcr out.bin [--time N]\n");
		return 1;
	}
	for (int i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "--time") && i + 1 < argc)
			reps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--honour-inverse"))
			argHonourInverse = 1;
		else if (!strcmp(argv[i], "--feel-rad") && i + 1 < argc) {   /* exact: radians as the engine holds them */
			float v[9] = { 0 };
			if (sscanf(argv[++i], "%f,%f,%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
				argFeel.on = 1;
				argFeel.linWeight = v[0]; argFeel.maxLinAccG = v[1]; argFeel.linTau = v[2];
				argFeel.eulerWeight = v[3]; argFeel.maxAngAcc = v[4]; argFeel.centWeight = v[5]; argFeel.maxAngVel = v[6];
				argFeel.carryLag = v[7]; argFeel.carryAngLag = v[8];
			}
		} else if (!strcmp(argv[i], "--feel") && i + 1 < argc) {
			float v[9] = { 0 };
			if (sscanf(argv[++i], "%f,%f,%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) >= 7) {
				argFeel.carryLag = v[7]; argFeel.carryAngLag = v[8] * 3.14159265f / 180;
				argFeel.on = 1;
				argFeel.linWeight = v[0]; argFeel.maxLinAccG = v[1]; argFeel.linTau = v[2];
				argFeel.eulerWeight = v[3]; argFeel.maxAngAcc = v[4] * 3.14159265f / 180;
				argFeel.centWeight = v[5]; argFeel.maxAngVel = v[6] * 3.14159265f / 180;
			}
		} else if ((!strcmp(argv[i], "--world-scale") || !strcmp(argv[i], "--world-scale-nofix")) && i + 1 < argc) {
			argWorldScaleFix = !strcmp(argv[i], "--world-scale");
			argWorldScale = atof(argv[++i]);
		} else if (!strcmp(argv[i], "--flow") && i + 1 < argc) {
			float v[4] = { 0 };
			if (sscanf(argv[++i], "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4) {
				argFlow.springDamp = v[0]; argFlow.innerScale = v[1]; argFlow.airDrag = v[2]; argFlow.coneParent = v[3];
			}
		}
	}
	load_asset(argv[1], &a);
	load_reel(argv[2], &r);
	if (r.nP != a.nParticles || r.nC != a.nCones || r.nR != a.nRepulsors || r.nB != a.nBones) {
		fprintf(stderr, "asset/reel mismatch\n");
		return 1;
	}
	if (!HC_Init(&s, &a)) {
		fprintf(stderr, "HC_Init failed\n");
		return 1;
	}
	HC_SetFeel(&s, &argFeel);
	HC_SetFlow(&s, &argFlow);
	onestep = calloc((size_t)r.nS * r.nP * 3, sizeof(double));
	freeOut = calloc((size_t)r.nS * r.nP * 3, sizeof(double));
	kin = calloc((size_t)r.nS * 9, sizeof(double));
	pose = calloc((size_t)r.nF * r.nB * 16, sizeof(double));
	posefree = calloc((size_t)r.nF * r.nB * 16, sizeof(double));
	anim = malloc(sizeof(hc_real) * 16 * r.nB);
	posed = malloc(sizeof(hc_real) * 16 * r.nB);

	/* one-step parity: frame history advances exactly as in the free run, the state comes from Blender each step */
	begin(&s, &r, 0, r.resetFrame, 0);
	begin(&s, &r, 0, r.resetFrame, 0);
	for (int i = 0; i < r.nS; i++) {
		begin(&s, &r, i, r.frame[i], 1);
		set_inputs(&s, &r, i);
		for (int p = 0; p < r.nP; p++)
			for (int k = 0; k < 3; k++) {
				s.pos[p][k] = (hc_real)r.prePos[(i * r.nP + p) * 3 + k];
				s.prev[p][k] = (hc_real)r.prePrev[(i * r.nP + p) * 3 + k];
			}
		HC_Step(&s);
		for (int p = 0; p < r.nP; p++)
			for (int k = 0; k < 3; k++)
				onestep[(i * r.nP + p) * 3 + k] = s.pos[p][k];
		for (int k = 0; k < 3; k++) {
			kin[i * 9 + k] = s.frameAcc[k];
			kin[i * 9 + 3 + k] = s.angVel[k];
			kin[i * 9 + 6 + k] = s.gravityLocal[k];
		}
	}
	HC_Free(&s);

	if (!HC_Init(&s, &a))
		return 1;
	HC_SetFeel(&s, &argFeel);
	HC_SetFlow(&s, &argFlow);
	free_run(&s, &r, freeOut);
	HC_Free(&s);

	/* pose on Blender's solved positions, then on the C free run */
	if (!HC_Init(&s, &a))
		return 1;
	HC_SetFeel(&s, &argFeel);
	HC_SetFlow(&s, &argFlow);
	for (int pass = 0; pass < 2; pass++)
		for (int fr = 0; fr < r.nF; fr++) {
			const double *sim = pass == 0 ? r.pSim + fr * r.nP * 3 : freeOut + (size_t)(r.nPreroll + fr) * r.nP * 3;
			for (int p = 0; p < r.nP; p++)
				for (int k = 0; k < 3; k++)
					s.pos[p][k] = (hc_real)sim[p * 3 + k];
			for (int k = 0; k < 16 * r.nB; k++)
				anim[k] = (hc_real)r.pAnim[fr * 16 * r.nB + k];
			HC_Pose(&s, anim, posed);
			for (int k = 0; k < 16 * r.nB; k++)
				(pass == 0 ? pose : posefree)[fr * 16 * r.nB + k] = posed[k];
		}
	HC_Free(&s);

	if (reps > 0) {
		clock_t t0 = clock();
		for (int k = 0; k < reps; k++) {
			HC_Init(&s, &a);
			HC_SetFeel(&s, &argFeel);
			HC_SetFlow(&s, &argFlow);
			free_run(&s, &r, NULL);
			HC_Free(&s);
		}
		printf("time: %.3f us per step (%d particles, %d pairs, %d substeps)\n",
		       1e6 * (double)(clock() - t0) / CLOCKS_PER_SEC / ((double)reps * r.nS), a.nParticles, a.nPairs, a.substeps);
	}

	f = fopen(argv[3], "wb");
	if (!f) {
		fprintf(stderr, "cannot write %s\n", argv[3]);
		return 1;
	}
	{
		int h[4] = { r.nS, r.nP, r.nF, r.nB };
		fwrite(h, 4, 4, f);
	}
	fwrite(onestep, sizeof(double), (size_t)r.nS * r.nP * 3, f);
	fwrite(freeOut, sizeof(double), (size_t)r.nS * r.nP * 3, f);
	fwrite(kin, sizeof(double), (size_t)r.nS * 9, f);
	fwrite(pose, sizeof(double), (size_t)r.nF * r.nB * 16, f);
	fwrite(posefree, sizeof(double), (size_t)r.nF * r.nB * 16, f);
	fclose(f);
	printf("%s: %d steps, %d particles, %d pose frames, %d bones -> %s\n", argv[2], r.nS, r.nP, r.nF, r.nB, argv[3]);
	return 0;
}
