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

/* hc_reel.h -- the HCR1 replay reel (tools/hair/hcd_to_hcr.py, the research exporters) for hc_replay.c and
 * hc_frames.c. Per step: int frame number, int pose time in microseconds (0 = the asset's dt; a game step that covered
 * more pose time than it integrated, rd-common/sim_frame.h), then the step's inputs and the recorded states. */
#ifndef HC_REEL_H
#define HC_REEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int nS, nP, nC, nR, nF, nB, resetFrame, nPreroll;
	double *init;
	double *seedPos, *seedPrev;	/* version 2: the state the reset left (after the game's pre-roll), or NULL */
	int *frame, *poseUs;
	double *ref, *targets, *anchors, *rep, *prePos, *prePrev, *postPos, *postPrev, *acc, *ang, *grav;
	double *pRef, *pAnim, *pPosed, *pSim;
} reel_t;

static unsigned char *reel_slurp(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;
	if (!f) {
		fprintf(stderr, "cannot open %s\n", path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(*size);
	if (fread(b, 1, *size, f) != (size_t)*size)
		exit(1);
	fclose(f);
	return b;
}

static int reel_geti(const unsigned char *b, long o) { int v; memcpy(&v, b + o, 4); return v; }

static void load_reel(const char *path, reel_t *r)
{
	long n, o = 40;
	unsigned char *b = reel_slurp(path, &n);
	if (n < 40 || memcmp(b, "HCR1", 4)) {
		fprintf(stderr, "%s: not an HCR1 reel\n", path);
		exit(1);
	}
	r->nS = reel_geti(b, 8); r->nP = reel_geti(b, 12); r->nC = reel_geti(b, 16); r->nR = reel_geti(b, 20); r->nF = reel_geti(b, 24);
	r->nB = reel_geti(b, 28); r->resetFrame = reel_geti(b, 32); r->nPreroll = reel_geti(b, 36);
	{	/* the size the header's counts give, checked before anything is copied out of the buffer */
		const double need = 40.0 + 8.0 * r->nP * 3 * (reel_geti(b, 4) >= 2 ? 3 : 1)
			+ (double)r->nS * (8 + 8.0 * (16 + r->nP * 3 * 5 + r->nC * 3 + r->nR * 6 + 9))
			+ (double)r->nF * 8.0 * (16 + 32.0 * r->nB + 3 * r->nP);
		if (r->nS < 0 || r->nP < 0 || r->nC < 0 || r->nR < 0 || r->nF < 0 || r->nB < 0 || need != (double)n) {
			fprintf(stderr, "%s: reel size %ld, its header's counts need %.0f\n", path, n, need);
			exit(1);
		}
	}
#define TAKE(dst, count) do { dst = malloc(sizeof(double) * (count)); memcpy(dst, b + o, sizeof(double) * (count)); o += sizeof(double) * (count); } while (0)
	TAKE(r->init, r->nP * 3);
	r->seedPos = r->seedPrev = NULL;
	if (reel_geti(b, 4) >= 2) {	/* pos and prev after the reset (and r_hairPreroll's steps) */
		TAKE(r->seedPos, r->nP * 3);
		TAKE(r->seedPrev, r->nP * 3);
	}
	r->frame = malloc(sizeof(int) * r->nS);
	r->poseUs = malloc(sizeof(int) * r->nS);
#define ALLOC(dst, per) dst = malloc(sizeof(double) * (per) * r->nS)
	ALLOC(r->ref, 16); ALLOC(r->targets, r->nP * 3); ALLOC(r->anchors, r->nC * 3); ALLOC(r->rep, r->nR * 6);
	ALLOC(r->prePos, r->nP * 3); ALLOC(r->prePrev, r->nP * 3); ALLOC(r->postPos, r->nP * 3); ALLOC(r->postPrev, r->nP * 3);
	ALLOC(r->acc, 3); ALLOC(r->ang, 3); ALLOC(r->grav, 3);
	for (int i = 0; i < r->nS; i++) {
		double *dst[] = { r->ref + i * 16, r->targets + i * r->nP * 3, r->anchors + i * r->nC * 3, r->rep + i * r->nR * 6,
		                  r->prePos + i * r->nP * 3, r->prePrev + i * r->nP * 3, r->postPos + i * r->nP * 3,
		                  r->postPrev + i * r->nP * 3, r->acc + i * 3, r->ang + i * 3, r->grav + i * 3 };
		int cnt[] = { 16, r->nP * 3, r->nC * 3, r->nR * 6, r->nP * 3, r->nP * 3, r->nP * 3, r->nP * 3, 3, 3, 3 };
		r->frame[i] = reel_geti(b, o);
		r->poseUs[i] = reel_geti(b, o + 4);
		o += 8;
		for (int k = 0; k < 11; k++) {
			memcpy(dst[k], b + o, sizeof(double) * cnt[k]);
			o += sizeof(double) * cnt[k];
		}
	}
	r->pRef = malloc(sizeof(double) * 16 * r->nF); r->pAnim = malloc(sizeof(double) * 16 * r->nB * r->nF);
	r->pPosed = malloc(sizeof(double) * 16 * r->nB * r->nF); r->pSim = malloc(sizeof(double) * 3 * r->nP * r->nF);
	for (int i = 0; i < r->nF; i++) {
		memcpy(r->pRef + i * 16, b + o, 8 * 16); o += 8 * 16;
		memcpy(r->pAnim + i * 16 * r->nB, b + o, 8 * 16 * r->nB); o += 8 * 16 * r->nB;
		memcpy(r->pPosed + i * 16 * r->nB, b + o, 8 * 16 * r->nB); o += 8 * 16 * r->nB;
		memcpy(r->pSim + i * 3 * r->nP, b + o, 8 * 3 * r->nP); o += 8 * 3 * r->nP;
	}
#undef TAKE
#undef ALLOC
	if (o != n) {
		fprintf(stderr, "reel size mismatch %ld vs %ld\n", o, n);
		exit(1);
	}
	free(b);
}

#endif /* HC_REEL_H */
