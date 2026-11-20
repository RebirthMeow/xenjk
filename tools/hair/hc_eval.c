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

/* hc_eval.c -- the engine's hair solver and asset parser as a shared library, for offline fitting and scenario runs
 * driven from Python (tools/hair/hc_eval.py). The same C as the game: rd-common/hairchains/hair_sim.c + hair_asset.c.
 * Build (from the repo root):
 *   gcc -std=c99 -O2 -ffp-contract=off -shared -o hc_eval.dll tools/hair/hc_eval.c
 *       codemp/rd-common/hairchains/hair_sim.c codemp/rd-common/hairchains/hair_asset.c -lm
 *
 * hce_run: one free run of an HCH1 asset (bytes) over nS steps of given inputs (reference frame matrices, frame
 * numbers, particle targets, root-cone anchors, repulsor points; all in the reference frame, column-major, f64).
 * The run starts like the engine's reset (two HC_BeginFrame calls at step 0's own frame, so step 0 does not continue
 * the history: tr_hair.cpp Hair_Reset, hc_replay.c's resetFrame), then the state is
 * set to initPos / initPrev, so a run can start from a recorded moving state. Particle positions after every step
 * go to outPos [nS][nP][3]. Returns 1, or 0 with the reason in err.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../codemp/rd-common/hairchains/hair_asset.h"

#ifdef _WIN32
#define HCE_EXPORT __declspec(dllexport)
#else
#define HCE_EXPORT
#endif

/* feel: on, then the nine hcFeel values in engine units (radians); flow: the four hcFlow values */
HCE_EXPORT int hce_run(const void *asset, long len, const float *feel, const float *flow, int honourInverse,
                       int nS, const double *ref, const int *frame, const double *targets, const double *anchors,
                       const double *rep, const double *initPos, const double *initPrev, double *outPos,
                       char *err, int errLen)
{
	hairAsset_t h;
	hcSim s;
	hcFeel fe;
	hcFlow fl;
	hc_real m[16];
	int nP, nC, nR;

	if (!HairAsset_Parse(asset, (size_t)len, &h, err, (size_t)errLen))
		return 0;
	if (honourInverse)
		h.a.flags |= HC_ASSET_HONOUR_INVERSE;
	nP = h.a.nParticles; nC = h.a.nCones; nR = h.a.nRepulsors;
	if (!HC_Init(&s, &h.a)) {
		snprintf(err, (size_t)errLen, "HC_Init failed");
		HairAsset_Free(&h);
		return 0;
	}
	memset(&fe, 0, sizeof(fe));
	memset(&fl, 0, sizeof(fl));
	if (feel && feel[0] != 0) {
		fe.on = 1;
		fe.linWeight = feel[1]; fe.maxLinAccG = feel[2]; fe.linTau = feel[3];
		fe.eulerWeight = feel[4]; fe.maxAngAcc = feel[5]; fe.centWeight = feel[6]; fe.maxAngVel = feel[7];
		fe.carryLag = feel[8]; fe.carryAngLag = feel[9];
	}
	if (flow) {
		fl.springDamp = flow[0]; fl.innerScale = flow[1]; fl.airDrag = flow[2]; fl.coneParent = flow[3];
	}
	HC_SetFeel(&s, &fe);
	HC_SetFlow(&s, &fl);

	for (int k = 0; k < 16; k++)
		m[k] = (hc_real)ref[k];
	HC_BeginFrame(&s, m, frame[0]);	/* frame[0] - 1 made step 0 consecutive (a start-up kick) */
	HC_BeginFrame(&s, m, frame[0]);
	for (int p = 0; p < nP; p++)
		for (int k = 0; k < 3; k++) {
			s.pos[p][k] = (hc_real)initPos[p * 3 + k];
			s.prev[p][k] = (hc_real)initPrev[p * 3 + k];
		}
	for (int i = 0; i < nS; i++) {
		for (int k = 0; k < 16; k++)
			m[k] = (hc_real)ref[i * 16 + k];
		HC_BeginFrame(&s, m, frame[i]);
		for (int p = 0; p < nP; p++)
			for (int k = 0; k < 3; k++)
				s.target[p][k] = (hc_real)targets[((size_t)i * nP + p) * 3 + k];
		for (int c = 0; c < nC; c++)
			for (int k = 0; k < 3; k++)
				s.anchor[c][k] = (hc_real)anchors[((size_t)i * nC + c) * 3 + k];
		for (int r = 0; r < nR; r++)
			for (int e = 0; e < 2; e++)
				for (int k = 0; k < 3; k++)
					s.repPoint[r][e][k] = (hc_real)rep[(((size_t)i * nR + r) * 2 + e) * 3 + k];
		HC_Step(&s);
		for (int p = 0; p < nP; p++)
			for (int k = 0; k < 3; k++)
				outPos[((size_t)i * nP + p) * 3 + k] = (double)s.pos[p][k];
	}
	HC_Free(&s);
	HairAsset_Free(&h);
	return 1;
}
