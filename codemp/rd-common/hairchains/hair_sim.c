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
/* hair_sim.c -- see hair_sim.h. Each function names the Route-A solver method it ports. */
#include "hair_sim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_R ((hc_real)3.14159265358979323846)
#ifndef HC_CONTACT_TOL
#define HC_CONTACT_TOL 1e-5   /* relative: a contact within this of the surface still applies friction/bounce */
#endif

/* ------------------------------------------------------------------ vector / matrix helpers */
static hc_real dot3(const hc_real *a, const hc_real *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static hc_real len3(const hc_real *a) { return (hc_real)sqrt(dot3(a, a)); }
static void sub3(hc_real *o, const hc_real *a, const hc_real *b) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static void cross3(hc_real *o, const hc_real *a, const hc_real *b)
{
	hc_real x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
	o[0] = x; o[1] = y; o[2] = z;
}
static void copy3(hc_real *o, const hc_real *a) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; }
static hc_real clampr(hc_real x, hc_real lo, hc_real hi) { return x < lo ? lo : x > hi ? hi : x; }

/* angle between unit vectors, the precise form (Blender angle_normalized_v3v3) */
static hc_real angle_unit(const hc_real *a, const hc_real *b)
{
	hc_real d[3];
	if (dot3(a, b) >= 0) {
		sub3(d, a, b);
		return 2 * (hc_real)asin(clampr(len3(d) / 2, -1, 1));
	}
	d[0] = -a[0] - b[0]; d[1] = -a[1] - b[1]; d[2] = -a[2] - b[2];
	return PI_R - 2 * (hc_real)asin(clampr(len3(d) / 2, -1, 1));
}

/* rotate v by angle about unit axis k (Rodrigues; equals Quaternion(k, angle) @ v) */
static void rotate_axis(hc_real *o, const hc_real *v, const hc_real *k, hc_real angle)
{
	hc_real c = (hc_real)cos(angle), s = (hc_real)sin(angle), kv[3], kd = dot3(k, v), r[3];
	cross3(kv, k, v);
	for (int i = 0; i < 3; i++)
		r[i] = v[i] * c + kv[i] * s + k[i] * kd * (1 - c);
	copy3(o, r);
}

/* 3x3 rotation for angle about unit axis k, column-major into a 4x4 */
static void axis_angle_mat4(hc_real *m, const hc_real *k, hc_real angle)
{
	hc_real c = (hc_real)cos(angle), s = (hc_real)sin(angle), t = 1 - c;
	memset(m, 0, sizeof(hc_real) * 16);
	m[0] = t * k[0] * k[0] + c;        m[4] = t * k[0] * k[1] - s * k[2]; m[8] = t * k[0] * k[2] + s * k[1];
	m[1] = t * k[0] * k[1] + s * k[2]; m[5] = t * k[1] * k[1] + c;        m[9] = t * k[1] * k[2] - s * k[0];
	m[2] = t * k[0] * k[2] - s * k[1]; m[6] = t * k[1] * k[2] + s * k[0]; m[10] = t * k[2] * k[2] + c;
	m[15] = 1;
}

static void mat4_mul(hc_real *o, const hc_real *a, const hc_real *b)
{
	hc_real r[16];
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++)
			r[c * 4 + rr] = a[rr] * b[c * 4] + a[4 + rr] * b[c * 4 + 1] + a[8 + rr] * b[c * 4 + 2] + a[12 + rr] * b[c * 4 + 3];
	memcpy(o, r, sizeof(r));
}

static int mat4_inv(hc_real *o, const hc_real *m)
{
	hc_real inv[16], det;
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (det == 0)
		return 0;
	det = 1 / det;
	for (int i = 0; i < 16; i++)
		o[i] = inv[i] * det;
	return 1;
}

static void mat3_apply(hc_real *o, const hc_real *m, const hc_real *v)   /* upper 3x3 of a 4x4 times v */
{
	hc_real r[3];
	for (int i = 0; i < 3; i++)
		r[i] = m[i] * v[0] + m[4 + i] * v[1] + m[8 + i] * v[2];
	copy3(o, r);
}

/* unit quaternion (w, x, y, z) of the rotation part of a rigid 4x4 */
static void mat4_to_quat(hc_real *q, const hc_real *m)
{
	hc_real m00 = m[0], m11 = m[5], m22 = m[10], tr = m00 + m11 + m22, s, n;
	if (tr > 0) {
		s = (hc_real)sqrt(tr + 1) * 2;
		q[0] = s / 4; q[1] = (m[6] - m[9]) / s; q[2] = (m[8] - m[2]) / s; q[3] = (m[1] - m[4]) / s;
	} else if (m00 > m11 && m00 > m22) {
		s = (hc_real)sqrt(1 + m00 - m11 - m22) * 2;
		q[0] = (m[6] - m[9]) / s; q[1] = s / 4; q[2] = (m[4] + m[1]) / s; q[3] = (m[8] + m[2]) / s;
	} else if (m11 > m22) {
		s = (hc_real)sqrt(1 + m11 - m00 - m22) * 2;
		q[0] = (m[8] - m[2]) / s; q[1] = (m[4] + m[1]) / s; q[2] = s / 4; q[3] = (m[9] + m[6]) / s;
	} else {
		s = (hc_real)sqrt(1 + m22 - m00 - m11) * 2;
		q[0] = (m[1] - m[4]) / s; q[1] = (m[8] + m[2]) / s; q[2] = (m[9] + m[6]) / s; q[3] = s / 4;
	}
	n = (hc_real)sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	for (int i = 0; i < 4; i++)
		q[i] /= n;
}

/* a limit of 0 lets nothing through (it used to mean no limit); a negative one is no limit */
static void clamp_len(hc_real *o, const hc_real *v, hc_real maxLen)
{
	hc_real l = len3(v), k = maxLen < 0 ? 1 : maxLen == 0 ? 0 : l > maxLen ? maxLen / l : 1;
	for (int i = 0; i < 3; i++)
		o[i] = v[i] * k;
}

/* closest points between segments p1q1 and p2q2 (closest_segment_points) */
static void closest_segments(const hc_real *p1, const hc_real *q1, const hc_real *p2, const hc_real *q2,
                             hc_real *c1, hc_real *c2, hc_real *sOut, hc_real *tOut)
{
	hc_real d1[3], d2[3], r[3], a, e, f, s, t, c, b, den;
	const hc_real eps = (hc_real)1e-12;
	sub3(d1, q1, p1); sub3(d2, q2, p2); sub3(r, p1, p2);
	a = dot3(d1, d1); e = dot3(d2, d2); f = dot3(d2, r);
	if (a < eps && e < eps) {
		s = t = 0;
	} else if (a < eps) {
		s = 0; t = clampr(f / e, 0, 1);
	} else {
		c = dot3(d1, r);
		if (e < eps) {
			t = 0; s = clampr(-c / a, 0, 1);
		} else {
			b = dot3(d1, d2);
			den = a * e - b * b;
			s = den > eps ? clampr((b * f - c * e) / den, 0, 1) : 0;
			t = (b * s + f) / e;
			if (t < 0) {
				t = 0; s = clampr(-c / a, 0, 1);
			} else if (t > 1) {
				t = 1; s = clampr((b - c) / a, 0, 1);
			}
		}
	}
	for (int i = 0; i < 3; i++) {
		c1[i] = p1[i] + d1[i] * s;
		c2[i] = p2[i] + d2[i] * t;
	}
	*sOut = s; *tOut = t;
}

static int is_descendant(const hcAsset *a, int b, int ancestor);

/* ------------------------------------------------------------------ lifecycle */
int HC_Init(hcSim *s, const hcAsset *a)
{
	memset(s, 0, sizeof(*s));
	s->a = a;
	s->pos = calloc(a->nParticles ? a->nParticles : 1, sizeof(*s->pos));
	s->prev = calloc(a->nParticles ? a->nParticles : 1, sizeof(*s->prev));
	s->target = calloc(a->nParticles ? a->nParticles : 1, sizeof(*s->target));
	s->anchor = calloc(a->nCones ? a->nCones : 1, sizeof(*s->anchor));
	s->repPoint = calloc(a->nRepulsors ? a->nRepulsors : 1, sizeof(*s->repPoint));
	s->poseScratch = calloc((size_t)(a->nBones ? a->nBones : 1) * 32, sizeof(hc_real));
	s->poseFlags = calloc(a->nBones ? a->nBones : 1, 1);
	s->parentCone = calloc(a->nCones ? a->nCones : 1, sizeof(*s->parentCone));
	s->subtreeStart = calloc((size_t)a->nBones + 1, sizeof(*s->subtreeStart));
	s->subtree = calloc(a->nBones ? (size_t)a->nBones * (size_t)a->nBones : 1, sizeof(*s->subtree));   /* size_t, not int */
	if (s->subtreeStart && s->subtree)
		for (int b = 0, n = 0; b < a->nBones; b++) {
			for (int d = 0; d < a->nBones; d++)
				if (d == b || is_descendant(a, d, b))
					s->subtree[n++] = d;
			s->subtreeStart[b + 1] = n;
		}
	for (int i = 0; i < 16; i += 5)
		s->ref[i] = s->refInv[i] = 1;
	if (s->parentCone)
		for (int c = 0; c < a->nCones; c++) {
			s->parentCone[c] = -1;
			for (int d = 0; d < a->nCones && a->cones[c].parent >= 0; d++)
				if (a->cones[d].child == a->cones[c].parent)
					s->parentCone[c] = d;
		}
	return s->pos && s->prev && s->target && s->anchor && s->repPoint && s->poseScratch && s->poseFlags && s->parentCone
		&& s->subtreeStart && s->subtree;
}

void HC_Free(hcSim *s)
{
	free(s->pos); free(s->prev); free(s->target); free(s->anchor); free(s->repPoint);
	free(s->poseScratch); free(s->poseFlags); free(s->parentCone); free(s->subtreeStart); free(s->subtree);
	memset(s, 0, sizeof(*s));
}

void HC_SetWorldScale(hcSim *s, hc_real scale)
{
	const hc_real old = s->worldScale > 0 ? s->worldScale : 1;
	if (!(scale > 0))
		scale = 1;
	/* a change within float noise (a rotation row's length is 1 or 0.99999994 by yaw) is the same scale: keep it and
	   the frame history it was measured in */
	if (fabs(scale - old) <= old * (hc_real)1e-4)
		return;
	s->nOrigin = s->nRot = 0;
	s->haveFelt = 0;
	s->worldScale = scale;
}

void HC_BeginFrame(hcSim *s, const hc_real refWorld[16], int frame)
{
	HC_BeginFrameDt(s, refWorld, frame, 0);
}

/* update_reference() + _update_frame_kinematics(). poseDt (0 = dt): the time the reference frame took to get here
 * from the previous call; the frame's velocities are measured over it, the solver still integrates dt. */
void HC_BeginFrameDt(hcSim *s, const hc_real refWorld[16], int frame, hc_real poseDt)
{
	const hcAsset *a = s->a;
	hc_real g[3] = { a->gravity[0], a->gravity[1], a->gravity[2] }, q[4];
	const hc_real dt = a->dt;
	const hc_real pdt = poseDt > 0 ? poseDt : dt;   /* divides the frame's displacements */

	memcpy(s->ref, refWorld, sizeof(s->ref));
	mat4_inv(s->refInv, s->ref);
	{	/* world lengths into model units */
		const hc_real ws = s->worldScale > 0 ? s->worldScale : 1;
		g[0] /= ws; g[1] /= ws; g[2] /= ws;
	}
	mat3_apply(s->gravityLocal, s->refInv, g);

	if (s->haveLast && frame - s->lastFrame != 1)
		s->nOrigin = s->nRot = 0;
	s->lastFrame = frame;
	s->haveLast = 1;

	if (s->nOrigin == 3) {
		memmove(s->originHist[0], s->originHist[1], sizeof(s->originHist[0]) * 2);
		memmove(s->rotHist[0], s->rotHist[1], sizeof(s->rotHist[0]) * 2);
		s->histDt[0] = s->histDt[1];
		s->histDt[1] = s->histDt[2];
		s->nOrigin = s->nRot = 2;
	}
	s->histDt[s->nOrigin] = pdt;   /* the interval that ends at this sample */
	copy3(s->originHist[s->nOrigin], &s->ref[12]);
	if (s->worldScale > 0 && s->worldScale != 1)	/* the origin in model units */
		for (int i = 0; i < 3; i++)
			s->originHist[s->nOrigin][i] /= s->worldScale;
	s->nOrigin++;
	mat4_to_quat(q, s->ref);
	memcpy(s->rotHist[s->nRot++], q, sizeof(q));

	if (s->nOrigin >= 3) {
		hc_real w[3];
		const hc_real d1 = s->histDt[1], d2 = s->histDt[2];
		if (d1 == dt && d2 == dt) {
			for (int i = 0; i < 3; i++)
				w[i] = (s->originHist[2][i] - s->originHist[1][i] * 2 + s->originHist[0][i]) / (dt * dt);
		} else {   /* uneven intervals: the change of velocity over the time between their midpoints */
			for (int i = 0; i < 3; i++)
				w[i] = ((s->originHist[2][i] - s->originHist[1][i]) / d2 - (s->originHist[1][i] - s->originHist[0][i]) / d1)
					/ ((d1 + d2) * (hc_real)0.5);
		}
		mat3_apply(s->frameAcc, s->refInv, w);
	} else {
		s->frameAcc[0] = s->frameAcc[1] = s->frameAcc[2] = 0;
	}
	if (s->nRot >= 2) {
		const hc_real *p = s->rotHist[s->nRot - 2], *c = s->rotHist[s->nRot - 1];
		/* delta = current * inverse(previous) */
		hc_real pi[4] = { p[0], -p[1], -p[2], -p[3] }, d[4], axis[3], angle, n, w[3];
		d[0] = c[0] * pi[0] - c[1] * pi[1] - c[2] * pi[2] - c[3] * pi[3];
		d[1] = c[0] * pi[1] + c[1] * pi[0] + c[2] * pi[3] - c[3] * pi[2];
		d[2] = c[0] * pi[2] - c[1] * pi[3] + c[2] * pi[0] + c[3] * pi[1];
		d[3] = c[0] * pi[3] + c[1] * pi[2] - c[2] * pi[1] + c[3] * pi[0];
		angle = 2 * (hc_real)acos(clampr(d[0], -1, 1));
		if (angle > PI_R)
			angle -= 2 * PI_R;
		n = (hc_real)sqrt(d[1] * d[1] + d[2] * d[2] + d[3] * d[3]);
		if (n > 1e-12) {
			axis[0] = d[1] / n; axis[1] = d[2] / n; axis[2] = d[3] / n;
		} else {
			axis[0] = axis[1] = 0; axis[2] = 1;
		}
		if (fabs(angle) > 1e-9)
			for (int i = 0; i < 3; i++)
				w[i] = axis[i] * (angle / pdt);
		else
			w[0] = w[1] = w[2] = 0;
		mat3_apply(s->angVel, s->refInv, w);
	} else {
		s->angVel[0] = s->angVel[1] = s->angVel[2] = 0;
	}

	/* game profile: angular acceleration from consecutive angular velocities, the felt linear acceleration low-passed */
	if (s->nRot >= 2 && s->haveAngVel)
		for (int i = 0; i < 3; i++)
			s->angAcc[i] = (s->angVel[i] - s->prevAngVel[i]) / dt;
	else
		s->angAcc[0] = s->angAcc[1] = s->angAcc[2] = 0;
	copy3(s->prevAngVel, s->angVel);
	s->haveAngVel = s->nRot >= 2;
	s->frameVel[0] = s->frameVel[1] = s->frameVel[2] = 0;
	if (s->nOrigin >= 2) {
		hc_real v[3];
		for (int i = 0; i < 3; i++)
			v[i] = (s->originHist[s->nOrigin - 1][i] - s->originHist[s->nOrigin - 2][i]) / pdt;
		mat3_apply(s->frameVel, s->refInv, v);
	}
	if (s->feel.on && s->feel.carryLag > 0) {
		/* carry: felt velocity (world) and felt spin (reference frame) follow the real ones at bounded rates */
		const hc_real g = len3(s->gravityLocal), amax = s->feel.maxLinAccG * g;
		hc_real v[3] = { 0, 0, 0 }, d[3], aw[3], lag;
		if (s->nOrigin >= 2)
			for (int i = 0; i < 3; i++)
				v[i] = (s->originHist[s->nOrigin - 1][i] - s->originHist[s->nOrigin - 2][i]) / pdt;
		if (!s->haveFelt || s->nOrigin < 2) {
			copy3(s->feltVelW, v);
			copy3(s->feltOmega, s->angVel);
			s->feltAcc[0] = s->feltAcc[1] = s->feltAcc[2] = 0;
			s->feltAlpha[0] = s->feltAlpha[1] = s->feltAlpha[2] = 0;
			s->haveFelt = s->nOrigin >= 2;
			return;
		}
		sub3(d, v, s->feltVelW);
		lag = len3(d);
		if (lag > s->feel.carryLag) {   /* drop what lags too far behind (landings, teleport-like snaps) */
			for (int i = 0; i < 3; i++)
				s->feltVelW[i] += d[i] * (1 - s->feel.carryLag / lag);
			sub3(d, v, s->feltVelW);
		}
		for (int i = 0; i < 3; i++)
			d[i] /= dt;
		clamp_len(aw, d, amax);
		for (int i = 0; i < 3; i++)
			s->feltVelW[i] += aw[i] * dt;
		mat3_apply(s->feltAcc, s->refInv, aw);
		mat3_apply(s->frameVel, s->refInv, s->feltVelW);
		sub3(d, s->angVel, s->feltOmega);
		lag = len3(d);
		if (s->feel.carryAngLag > 0 && lag > s->feel.carryAngLag) {
			for (int i = 0; i < 3; i++)
				s->feltOmega[i] += d[i] * (1 - s->feel.carryAngLag / lag);
			sub3(d, s->angVel, s->feltOmega);
		}
		for (int i = 0; i < 3; i++)
			d[i] /= dt;
		clamp_len(s->feltAlpha, d, s->feel.maxAngAcc);
		for (int i = 0; i < 3; i++)
			s->feltOmega[i] += s->feltAlpha[i] * dt;
	} else if (s->nOrigin < 3) {
		s->feltAcc[0] = s->feltAcc[1] = s->feltAcc[2] = 0;
	} else {
		const hc_real k = s->feel.linTau > 0 ? dt / (s->feel.linTau + dt) : 1;
		for (int i = 0; i < 3; i++)
			s->feltAcc[i] += (s->frameAcc[i] - s->feltAcc[i]) * k;
	}
}

void HC_SetFeel(hcSim *s, const hcFeel *f) { s->feel = *f; }
void HC_SetFlow(hcSim *s, const hcFlow *f) { s->flow = *f; }
void HC_SetWind(hcSim *s, const hc_real windW[3])
{
	for (int k = 0; k < 3; k++)
		s->windW[k] = windW && isfinite(windW[k]) ? windW[k] : 0;
}


/* reset_to_pose() */
void HC_ResetToTargets(hcSim *s)
{
	for (int i = 0; i < s->a->nParticles; i++) {
		copy3(s->pos[i], s->target[i]);
		copy3(s->prev[i], s->target[i]);
	}
}

/* ------------------------------------------------------------------ solve */
static void integrate(hcSim *s, hc_real dt)   /* integrate() */
{
	const hcAsset *a = s->a;
	const hc_real w2 = dot3(s->angVel, s->angVel);
	for (int i = 0; i < a->nParticles; i++) {
		const hcParticle *p = &a->particles[i];
		hc_real damping = (1 - p->velocityDamping * dt) * (1 - p->damping * dt), v[3];
		if (damping < 0)
			damping = 0;
		for (int k = 0; k < 3; k++) {
			v[k] = (s->pos[i][k] - s->prev[i][k]) * damping;
			v[k] += s->gravityLocal[k] * p->gravityScale * dt * dt;
		}
		if (s->feel.on) {
			/* game profile: the entity's motion, felt fully but clamped (see hcFeel) */
			hc_real pseudo[3], al[3], wc[3], t[3], u[3];
			const hc_real g = len3(s->gravityLocal);
			clamp_len(pseudo, s->feltAcc, s->feel.maxLinAccG * g);
			for (int k = 0; k < 3; k++)
				pseudo[k] *= -s->feel.linWeight;
			clamp_len(al, s->feel.carryLag > 0 ? s->feltAlpha : s->angAcc, s->feel.maxAngAcc);
			cross3(t, al, s->pos[i]);
			for (int k = 0; k < 3; k++)
				pseudo[k] -= t[k] * s->feel.eulerWeight;
			clamp_len(wc, s->feel.carryLag > 0 ? s->feltOmega : s->angVel, s->feel.maxAngVel);
			cross3(t, wc, s->pos[i]);
			cross3(u, wc, t);
			for (int k = 0; k < 3; k++) {
				pseudo[k] -= u[k] * s->feel.centWeight;
				v[k] += pseudo[k] * dt * dt;
			}
		} else {
			hc_real pseudo[3] = { 0, 0, 0 };
			if (p->fictitious != 0)
				for (int k = 0; k < 3; k++)
					pseudo[k] -= s->frameAcc[k] * p->fictitious;
			if (p->centrifugal != 0 && w2 > 1e-12) {
				hc_real t[3], u[3];
				cross3(t, s->angVel, s->pos[i]);
				cross3(u, s->angVel, t);
				for (int k = 0; k < 3; k++)
					pseudo[k] -= u[k] * p->centrifugal;
			}
			for (int k = 0; k < 3; k++)
				v[k] += pseudo[k] * dt * dt;
		}
		if (s->flow.airDrag > 0) {
			/* drag through the air: the particle's own step plus the entity's run and spin carrying it, less the
			 * air's own motion (the wind, world units/s into the reference frame in model units) */
			const hc_real k = clampr(s->flow.airDrag * dt, 0, 1);
			const hc_real ws = s->worldScale > 0 ? s->worldScale : 1;
			hc_real w[3], t[3], wl[3], ww[3];
			copy3(w, s->feel.on && s->feel.carryLag > 0 ? s->feltOmega : s->angVel);
			cross3(t, w, s->pos[i]);
			for (int c = 0; c < 3; c++)
				ww[c] = s->windW[c] / ws;
			mat3_apply(wl, s->refInv, ww);
			for (int c = 0; c < 3; c++)
				v[c] -= (v[c] + (s->frameVel[c] + t[c] - wl[c]) * dt) * k;
		}
		copy3(s->prev[i], s->pos[i]);
		for (int k = 0; k < 3; k++)
			s->pos[i][k] += v[k];
	}
	/* m_ParkRate: a direct blend toward the animated position that moves prev too (no momentum) */
	for (int i = 0; i < a->nParticles; i++) {
		hc_real rate = a->particles[i].parkRate, d;
		if (rate <= 0)
			continue;
		if (rate > 1)
			rate = 1;
		for (int k = 0; k < 3; k++) {
			d = (s->target[i][k] - s->pos[i][k]) * rate;
			s->pos[i][k] += d;
			s->prev[i][k] += d;
		}
	}
}

static void solve_cones(hcSim *s)   /* solve_cones() */
{
	const hcAsset *a = s->a;
	for (int c = 0; c < a->nCones; c++) {
		const hcCone *cn = &a->cones[c];
		const int child = cn->child;
		const hc_real *parentPos = cn->parent < 0 ? s->anchor[c] : s->pos[cn->parent];
		const hc_real *parentTarget = cn->parent < 0 ? s->anchor[c] : s->target[cn->parent];
		hc_real delta[3], length, dir[3], stiff, corr, damp, ref[3], off[3], cur[3], angle, axis[3], an, ca, rl;

		sub3(delta, s->pos[child], parentPos);
		length = len3(delta);
		if (length < 1e-9)
			continue;
		for (int k = 0; k < 3; k++)
			dir[k] = delta[k] / length;
		stiff = length > cn->rest ? cn->stretch : cn->compress;
		corr = (length - cn->rest) * stiff;
		damp = 1 - a->particles[child].constraintDamping * (hc_real)0.5;
		corr *= damp > (hc_real)0.05 ? damp : (hc_real)0.05;
		corr *= cn->parentStrength;
		for (int k = 0; k < 3; k++)
			s->pos[child][k] -= dir[k] * corr;

		/* animation_direction(): where the animated chain points */
		sub3(ref, s->target[child], parentTarget);
		rl = len3(ref);
		if (rl <= 1e-9)
			continue;
		for (int k = 0; k < 3; k++)
			ref[k] /= rl;
		if (s->flow.coneParent > 0 && s->parentCone[c] >= 0) {
			/* turn the animated direction by (a share of) the parent segment's solved swing */
			const hcCone *pc = &a->cones[s->parentCone[c]];
			const hc_real *gp = pc->parent < 0 ? s->anchor[s->parentCone[c]] : s->pos[pc->parent];
			const hc_real *gt = pc->parent < 0 ? s->anchor[s->parentCone[c]] : s->target[pc->parent];
			hc_real pa[3], ps[3], la, ls, sw;
			sub3(pa, parentTarget, gt);
			sub3(ps, parentPos, gp);
			la = len3(pa);
			ls = len3(ps);
			if (la > 1e-9 && ls > 1e-9) {
				for (int k = 0; k < 3; k++) {
					pa[k] /= la;
					ps[k] /= ls;
				}
				sw = angle_unit(pa, ps);
				cross3(axis, pa, ps);
				an = len3(axis);
				if (sw > 1e-6 && an > 1e-9) {
					for (int k = 0; k < 3; k++)
						axis[k] /= an;
					rotate_axis(ref, ref, axis, sw * clampr(s->flow.coneParent, 0, 1));
				}
			}
		}
		sub3(off, s->pos[child], parentPos);
		{
			hc_real ol = len3(off);
			if (ol <= 0)
				continue;
			for (int k = 0; k < 3; k++)
				cur[k] = off[k] / ol;
		}
		angle = angle_unit(cur, ref);
		if (angle < 1e-6)
			continue;
		cross3(axis, ref, cur);
		an = len3(axis);
		if (an < 1e-9)
			continue;
		for (int k = 0; k < 3; k++)
			axis[k] /= an;
		ca = (s->flow.innerScale > 0 ? cn->inner * s->flow.innerScale : cn->inner) * angle;
		if (angle > cn->limit)
			ca += cn->outer * (angle - cn->limit);
		if (ca > angle)
			ca = angle;
		if (ca > 1e-9) {
			hc_real step[3], sl;
			rotate_axis(off, off, axis, -ca);
			for (int k = 0; k < 3; k++) {
				const hc_real np = parentPos[k] + off[k];
				step[k] = np - s->pos[child][k];
				s->pos[child][k] = np;
			}
			sl = len3(step);
			if (s->flow.springDamp > 0 && sl > 1e-9) {
				/* damper: the child's velocity relative to its parent, along the pull, loses this share */
				const hc_real kd = clampr(a->particles[child].constraintDamping * s->flow.springDamp, 0, 1);
				hc_real vr[3], along;
				for (int k = 0; k < 3; k++)
					vr[k] = (s->pos[child][k] - s->prev[child][k]) -
						(cn->parent < 0 ? 0 : s->pos[cn->parent][k] - s->prev[cn->parent][k]);
				along = dot3(vr, step) / sl;
				for (int k = 0; k < 3; k++)
					s->prev[child][k] += step[k] / sl * along * kd;
			}
		}
	}
}

static void solve_links(hcSim *s)   /* solve_links() */
{
	const hcAsset *a = s->a;
	for (int l = 0; l < a->nLinks; l++) {
		const hcLink *lk = &a->links[l];
		hc_real delta[3], length, corr, stiff;
		sub3(delta, s->pos[lk->b], s->pos[lk->a]);
		length = len3(delta);
		if (length < 1e-9)
			continue;
		stiff = length > lk->rest ? lk->stretch : lk->compress;
		corr = (length - lk->rest) * stiff * (hc_real)0.5;
		for (int k = 0; k < 3; k++) {
			hc_real o = delta[k] / length * corr;
			s->pos[lk->a][k] += o;
			s->pos[lk->b][k] -= o;
		}
	}
}

static void solve_shape_collisions(hcSim *s)   /* solve_shape_collisions() */
{
	const hcAsset *a = s->a;
	for (int k = 0; k < a->nPairs; k++) {
		const hcReceiver *rc = &a->receivers[a->pairs[k][0]];
		const hcRepulsor *rp = &a->repulsors[a->pairs[k][1]];
		const int p0 = rc->p[0], p1 = rc->p[rc->n - 1];
		const hc_real *P = s->repPoint[a->pairs[k][1]][0], *Q = s->repPoint[a->pairs[k][1]][rp->n - 1];
		hc_real nr[3], np_[3], sp, tp, rr, pr, limit, delta[3], dist, normal[3], push[3];
		hc_real A[3], B[3];

		/* containment (BNM bIsInverse), only when the asset asks for it: each receiver particle must stay inside the
		 * shape by its own radius. Route A treats these shapes as ordinary repulsors, pushing hair out of them. */
		if ((rp->flags & HC_REP_INVERSE) && (a->flags & HC_ASSET_HONOUR_INVERSE)) {
			for (int j = 0; j < rc->n; j++) {
				const int part = rc->p[j];
				const hcParticle *pp = &a->particles[part];
				hc_real ab[3], ap[3], t, den, c[3], inside, vel[3], vn;
				sub3(ab, Q, P);
				sub3(ap, s->pos[part], P);
				den = dot3(ab, ab);
				t = den > 1e-12 ? clampr(dot3(ap, ab) / den, 0, 1) : 0;
				for (int i = 0; i < 3; i++)
					c[i] = P[i] + ab[i] * t;
				sub3(delta, s->pos[part], c);
				dist = len3(delta);
				inside = rp->r[0] * (1 - t) + rp->r[rp->n - 1] * t - rc->r[j];
				if (dist <= inside || dist < 1e-9 || inside <= 0)
					continue;
				for (int i = 0; i < 3; i++) {
					normal[i] = -delta[i] / dist;       /* inward */
					s->pos[part][i] += normal[i] * (dist - inside);
				}
				sub3(vel, s->pos[part], s->prev[part]);
				vn = dot3(vel, normal);
				for (int i = 0; i < 3; i++) {
					hc_real nrm = normal[i] * vn, tan = vel[i] - nrm;
					vel[i] = tan * (1 - pp->friction) - nrm * pp->bounciness;
					s->prev[part][i] = s->pos[part][i] - vel[i];
				}
			}
			continue;
		}
		copy3(A, s->pos[p0]);
		copy3(B, s->pos[p1]);
		closest_segments(A, B, P, Q, nr, np_, &sp, &tp);
		rr = rc->r[0] * (1 - sp) + rc->r[rc->n - 1] * sp;
		pr = rp->r[0] * (1 - tp) + rp->r[rp->n - 1] * tp;
		limit = rr + pr;
		sub3(delta, nr, np_);
		dist = len3(delta);
		/* A particle resting on the surface counts as touching: the asset repeats some shapes exactly, and after
		 * the first copy pushes a particle out it sits on the second copy's surface. The Blender solver's float32
		 * vectors land just inside there and apply the friction/bounce response again; decide it the same way
		 * every time instead of by rounding. */
		if (dist >= limit * (1 + (hc_real)HC_CONTACT_TOL) || dist < 1e-9)
			continue;
		for (int i = 0; i < 3; i++) {
			normal[i] = delta[i] / dist;
			push[i] = dist < limit ? normal[i] * (limit - dist) : 0;
		}
		for (int j = 0; j < (rc->n > 1 ? 2 : 1); j++) {
			const int part = j == 0 ? p0 : p1;
			const hc_real w = rc->n > 1 ? (j == 0 ? 1 - sp : sp) : 1;
			const hcParticle *pp = &a->particles[part];
			hc_real vel[3], vn, nrm[3], tan[3];
			if (w <= 1e-6)
				continue;
			for (int i = 0; i < 3; i++)
				s->pos[part][i] += push[i] * w;
			sub3(vel, s->pos[part], s->prev[part]);
			vn = dot3(vel, normal);
			for (int i = 0; i < 3; i++) {
				nrm[i] = normal[i] * vn;
				tan[i] = vel[i] - nrm[i];
				vel[i] = tan[i] * (1 - pp->friction) - nrm[i] * pp->bounciness;
				s->prev[part][i] = s->pos[part][i] - vel[i];
			}
		}
	}
}

/* step(): the caller has already run HC_BeginFrame and filled the frame's inputs */
void HC_Step(hcSim *s)
{
	const hcAsset *a = s->a;
	const hc_real dt = (hc_real)a->dt / a->substeps;
	for (int sub = 0; sub < a->substeps; sub++) {
		integrate(s, dt);
		for (int it = 0; it < a->lateralIter; it++) {
			solve_cones(s);
			solve_links(s);
		}
		for (int it = 0; it < a->collisionIter; it++)
			solve_shape_collisions(s);
	}
}

/* Diagnostic (no effect on the state): receiver/repulsor pairs whose shapes overlap by more than tol (asset units)
 * with the inputs of the last step, as solve_shape_collisions measures them; a containment shape counts a particle
 * that far outside it. maxDepth (may be NULL) receives the deepest overlap. */
int HC_CountInside(const hcSim *s, hc_real tol, hc_real *maxDepth)
{
	const hcAsset *a = s->a;
	int count = 0;
	hc_real deepest = 0;
	for (int k = 0; k < a->nPairs; k++) {
		const hcReceiver *rc = &a->receivers[a->pairs[k][0]];
		const hcRepulsor *rp = &a->repulsors[a->pairs[k][1]];
		const hc_real *P = s->repPoint[a->pairs[k][1]][0], *Q = s->repPoint[a->pairs[k][1]][rp->n - 1];
		hc_real depth = 0;
		if (rp->flags & HC_REP_INVERSE) {
			if (!(a->flags & HC_ASSET_HONOUR_INVERSE))
				continue;   /* Route A pushes hair out of these; the game keeps it in: neither is "inside the body" */
			for (int j = 0; j < rc->n; j++) {
				hc_real ab[3], ap[3], c[3], d[3], t, den, inside;
				sub3(ab, Q, P);
				sub3(ap, s->pos[rc->p[j]], P);
				den = dot3(ab, ab);
				t = den > 1e-12 ? clampr(dot3(ap, ab) / den, 0, 1) : 0;
				for (int i = 0; i < 3; i++)
					c[i] = P[i] + ab[i] * t;
				sub3(d, s->pos[rc->p[j]], c);
				inside = rp->r[0] * (1 - t) + rp->r[rp->n - 1] * t - rc->r[j];
				if (inside > 0 && len3(d) - inside > depth)
					depth = len3(d) - inside;
			}
		} else {
			hc_real A[3], B[3], nr[3], np_[3], sp, tp, d[3];
			copy3(A, s->pos[rc->p[0]]);
			copy3(B, s->pos[rc->p[rc->n - 1]]);
			closest_segments(A, B, P, Q, nr, np_, &sp, &tp);
			sub3(d, nr, np_);
			depth = rc->r[0] * (1 - sp) + rc->r[rc->n - 1] * sp + rp->r[0] * (1 - tp) + rp->r[rp->n - 1] * tp - len3(d);
		}
		if (depth > tol)
			count++;
		if (depth > deepest)
			deepest = depth;
	}
	if (maxDepth)
		*maxDepth = deepest;
	return count;
}

/* ------------------------------------------------------------------ pose: write_back() + apply_pose_mirrors() */
static int is_descendant(const hcAsset *a, int b, int ancestor)
{
	for (int p = a->bones[b].parent; p >= 0; p = a->bones[p].parent)
		if (p == ancestor)
			return 1;
	return 0;
}

static void local_of(const hcAsset *a, const hc_real *world, int b, hc_real *out)
{
	hc_real inv[16];
	const int p = a->bones[b].parent;
	if (p < 0) {   /* the host bone does not move while posing, so it cancels out of every delta */
		memcpy(out, &world[b * 16], sizeof(hc_real) * 16);
		return;
	}
	mat4_inv(inv, &world[p * 16]);
	mat4_mul(out, inv, &world[b * 16]);
}

void HC_Pose(const hcSim *s, const hc_real *anim, hc_real *posed)
{
	HC_PoseFrom(s, (const hc_real (*)[3])s->pos, anim, posed);
}

void HC_PoseFrom(const hcSim *s, const hc_real (*pos)[3], const hc_real *anim, hc_real *posed)
{
	const hcAsset *a = s->a;
	const int nb = a->nBones;
	hc_real *wb = s->poseScratch, *newLocal = s->poseScratch + 16 * nb;
	unsigned char *mirrored = s->poseFlags;

	memset(mirrored, 0, (size_t)nb);
	memcpy(posed, anim, sizeof(hc_real) * 16 * nb);
	/* write_back: aim each cone's parent bone at the solved child, root to tip, turning its whole subtree */
	for (int c = 0; c < a->nCones; c++) {
		const hcCone *cn = &a->cones[c];
		const int P = cn->parent < 0 ? cn->anchorBone : a->particles[cn->parent].bone;
		const int C = a->particles[cn->child].bone;
		hc_real o[3], cur[3], des[3], lc, ld, axis[3], an, ang, R[16], pivot[16], t1[16], t2[16];
		copy3(o, &posed[P * 16 + 12]);
		sub3(cur, &posed[C * 16 + 12], o);
		sub3(des, pos[cn->child], o);
		lc = len3(cur); ld = len3(des);
		if (lc < 1e-9 || ld < 1e-9)
			continue;
		for (int k = 0; k < 3; k++) {
			cur[k] /= lc; des[k] /= ld;
		}
		/* rotation_difference(cur, des) */
		cross3(axis, cur, des);
		an = len3(axis);
		ang = angle_unit(cur, des);
		if (an < 1e-12)
			continue;   /* parallel: identity (anti-parallel never occurs within a cone's limit) */
		for (int k = 0; k < 3; k++)
			axis[k] /= an;
		axis_angle_mat4(R, axis, ang);
		/* pivot = T(o) R T(-o) */
		memset(t1, 0, sizeof(t1)); t1[0] = t1[5] = t1[10] = t1[15] = 1; t1[12] = o[0]; t1[13] = o[1]; t1[14] = o[2];
		mat4_mul(t2, t1, R);
		t1[12] = -o[0]; t1[13] = -o[1]; t1[14] = -o[2];
		mat4_mul(pivot, t2, t1);
		if (s->fast & 1) {
			for (int j = s->subtreeStart[P]; j < s->subtreeStart[P + 1]; j++)
				mat4_mul(&posed[s->subtree[j] * 16], pivot, &posed[s->subtree[j] * 16]);
		} else {
			for (int b = 0; b < nb; b++)
				if (b == P || is_descendant(a, b, P))
					mat4_mul(&posed[b * 16], pivot, &posed[b * 16]);
		}
	}
	/* apply_pose_mirrors: target local = target animated local * (driver animated local)^-1 * driver solved local */
	memcpy(wb, posed, sizeof(hc_real) * 16 * nb);
	{
		for (int m = 0; m < a->nMirrors; m++) {
			const int t = a->mirrors[m][0], d = a->mirrors[m][1];
			hc_real la[16], lp[16], inv[16], D[16], lt[16];
			local_of(a, anim, d, la);
			local_of(a, wb, d, lp);
			mat4_inv(inv, la);
			mat4_mul(D, inv, lp);
			local_of(a, anim, t, lt);
			mat4_mul(&newLocal[t * 16], lt, D);
			mirrored[t] = 1;
		}
		/* re-evaluate the hierarchy: targets take their new local, everything below follows its parent */
		for (int b = 0; b < nb; b++) {
			const int p = a->bones[b].parent;
			int moved = mirrored[b];
			for (int q = p; q >= 0 && !moved; q = a->bones[q].parent)
				moved = mirrored[q];
			if (!moved)
				continue;
			if (mirrored[b]) {
				if (p < 0)
					memcpy(&posed[b * 16], &newLocal[b * 16], sizeof(hc_real) * 16);
				else
					mat4_mul(&posed[b * 16], &posed[p * 16], &newLocal[b * 16]);
			} else {
				hc_real l[16];
				local_of(a, wb, b, l);
				mat4_mul(&posed[b * 16], &posed[p * 16], l);
			}
		}
	}
}
