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
/* hair_spring.c -- spring bones: a damped spring per bone, chasing its animated point (see hair_spring.h). */
#include <math.h>
#include <string.h>

#include "hair_spring.h"

#define HS_TINY 1e-6

typedef double hsM3[3][3];

static double V_Dot(const double a[3], const double b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static double V_Len(const double a[3])
{
	return sqrt(V_Dot(a, a));
}

static void V_Cross(double out[3], const double a[3], const double b[3])
{
	const double x = a[1] * b[2] - a[2] * b[1];
	const double y = a[2] * b[0] - a[0] * b[2];
	const double z = a[0] * b[1] - a[1] * b[0];
	out[0] = x; out[1] = y; out[2] = z;
}

static void M_Vec(double out[3], hsM3 m, const double v[3])
{
	double r[3];
	for (int i = 0; i < 3; i++)
		r[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
	memcpy(out, r, sizeof r);
}

static void M_Mul(hsM3 out, hsM3 a, hsM3 b)
{
	hsM3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
	memcpy(out, r, sizeof r);
}

static void M_Identity(hsM3 m)
{
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m[i][j] = i == j ? 1.0 : 0.0;
}

/* the asset-to-source map and its inverse; a singular basis falls back to its transpose */
static void Basis(const float basis[9], hsM3 toSrc, hsM3 toAsset)
{
	double det;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			toSrc[i][j] = basis[i * 3 + j];
	toAsset[0][0] = toSrc[1][1] * toSrc[2][2] - toSrc[1][2] * toSrc[2][1];
	toAsset[0][1] = toSrc[0][2] * toSrc[2][1] - toSrc[0][1] * toSrc[2][2];
	toAsset[0][2] = toSrc[0][1] * toSrc[1][2] - toSrc[0][2] * toSrc[1][1];
	toAsset[1][0] = toSrc[1][2] * toSrc[2][0] - toSrc[1][0] * toSrc[2][2];
	toAsset[1][1] = toSrc[0][0] * toSrc[2][2] - toSrc[0][2] * toSrc[2][0];
	toAsset[1][2] = toSrc[0][2] * toSrc[1][0] - toSrc[0][0] * toSrc[1][2];
	toAsset[2][0] = toSrc[1][0] * toSrc[2][1] - toSrc[1][1] * toSrc[2][0];
	toAsset[2][1] = toSrc[0][1] * toSrc[2][0] - toSrc[0][0] * toSrc[2][1];
	toAsset[2][2] = toSrc[0][0] * toSrc[1][1] - toSrc[0][1] * toSrc[1][0];
	det = toSrc[0][0] * toAsset[0][0] + toSrc[0][1] * toAsset[1][0] + toSrc[0][2] * toAsset[2][0];
	if (fabs(det) < 1e-12) {
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				toAsset[i][j] = toSrc[j][i];
		return;
	}
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			toAsset[i][j] /= det;
}

/* an offset from the target with its locked source axes removed, then held within maxDisplacement */
static void Constrain(const hsSpring *sp, hsM3 toSrc, hsM3 toAsset, double off[3])
{
	static const int axis[3] = { HS_TRANSLATE_X, HS_TRANSLATE_Y, HS_TRANSLATE_Z };
	double src[3], len;
	M_Vec(src, toSrc, off);
	for (int k = 0; k < 3; k++)
		if (!(sp->flags & axis[k]))
			src[k] = 0.0;
	M_Vec(off, toAsset, src);
	len = V_Len(off);
	if (sp->maxDisplacement > 0.0f && len > sp->maxDisplacement) {
		const double s = sp->maxDisplacement / len;
		for (int k = 0; k < 3; k++)
			off[k] *= s;
	}
}

void HS_Step(const hsSpring *sp, const float basis[9], hsState *st, const float target[3], float dtf)
{
	const double dt = dtf;
	const double k = sp->stiffness, c = sp->damping, reset = sp->resetThresh;
	double t[3], loc[3], vel[3], d[3], next[3], off[3];
	hsM3 toSrc, toAsset;

	for (int i = 0; i < 3; i++) {
		t[i] = target[i];
		loc[i] = st->loc[i];
		vel[i] = st->vel[i];
		d[i] = t[i] - loc[i];
	}
	if (st->fresh || !(dt > 0.0) || (reset > 0.0 && V_Len(d) > reset)) {
		for (int i = 0; i < 3; i++) {
			st->loc[i] = target[i];
			st->vel[i] = 0.0f;
		}
		st->fresh = 0;
		return;
	}

	/* semi-implicit Euler; the damping share of the change is capped so it never reverses the velocity */
	{
		const double damp = c * dt > 1.0 ? 1.0 / dt : c;
		for (int i = 0; i < 3; i++)
			vel[i] += (k * d[i] - damp * vel[i]) * dt;
	}
	if (reset > 0.0) {
		const double travel = V_Len(vel) * dt;
		if (travel > reset) {
			const double s = reset / travel;
			for (int i = 0; i < 3; i++)
				vel[i] *= s;
		}
	}
	for (int i = 0; i < 3; i++) {
		next[i] = loc[i] + vel[i] * dt;
		off[i] = next[i] - t[i];
	}

	Basis(basis, toSrc, toAsset);
	Constrain(sp, toSrc, toAsset, off);
	for (int i = 0; i < 3; i++) {
		next[i] = t[i] + off[i];
		st->vel[i] = (float)((next[i] - loc[i]) / dt);
		st->loc[i] = (float)next[i];
	}
}

/* the rotation turning u onto v by the shortest arc; 0 when the arc is degenerate */
static int ShortestArc(hsM3 r, const double u[3], const double v[3])
{
	double ax[3], s, c, lu = V_Len(u), lv = V_Len(v), a, ca, sa, ta;
	M_Identity(r);
	if (lu < HS_TINY || lv < HS_TINY)
		return 0;
	V_Cross(ax, u, v);
	s = V_Len(ax) / (lu * lv);
	c = V_Dot(u, v) / (lu * lv);
	if (c < -1.0 + HS_TINY)   /* nearly opposite: no unique arc */
		return 0;
	if (s < 1e-12)
		return 0;
	for (int k = 0; k < 3; k++)
		ax[k] /= s * lu * lv;
	a = atan2(s, c);
	ca = cos(a); sa = sin(a); ta = 1.0 - ca;
	r[0][0] = ca + ax[0] * ax[0] * ta;
	r[0][1] = ax[0] * ax[1] * ta - ax[2] * sa;
	r[0][2] = ax[0] * ax[2] * ta + ax[1] * sa;
	r[1][0] = ax[1] * ax[0] * ta + ax[2] * sa;
	r[1][1] = ca + ax[1] * ax[1] * ta;
	r[1][2] = ax[1] * ax[2] * ta - ax[0] * sa;
	r[2][0] = ax[2] * ax[0] * ta - ax[1] * sa;
	r[2][1] = ax[2] * ax[1] * ta + ax[0] * sa;
	r[2][2] = ca + ax[2] * ax[2] * ta;
	return 1;
}

/* R = Rz(yaw) Ry(pitch) Rx(roll) */
static void EulerFrom(hsM3 r, double *roll, double *pitch, double *yaw)
{
	double sp = -r[2][0];
	if (sp > 1.0) sp = 1.0;
	if (sp < -1.0) sp = -1.0;
	*pitch = asin(sp);
	if (fabs(sp) < 1.0 - 1e-9) {
		*roll = atan2(r[2][1], r[2][2]);
		*yaw = atan2(r[1][0], r[0][0]);
	} else {   /* gimbal: put the whole turn about the vertical into yaw */
		*roll = 0.0;
		*yaw = atan2(-r[0][1], r[1][1]);
	}
}

static void EulerTo(hsM3 r, double roll, double pitch, double yaw)
{
	const double cr = cos(roll), sr = sin(roll), cp = cos(pitch), sp = sin(pitch), cy = cos(yaw), sy = sin(yaw);
	r[0][0] = cy * cp; r[0][1] = cy * sp * sr - sy * cr; r[0][2] = cy * sp * cr + sy * sr;
	r[1][0] = sy * cp; r[1][1] = sy * sp * sr + cy * cr; r[1][2] = sy * sp * cr - cy * sr;
	r[2][0] = -sp;     r[2][1] = cp * sr;                r[2][2] = cp * cr;
}

int HS_Delta(const hsSpring *sp, const float basis[9], const float offset[3], const float target[3], const float pivot[3],
	float delta[16])
{
	const double alpha = sp->alpha < 0.0f ? 0.0 : sp->alpha > 1.0f ? 1.0 : sp->alpha;
	double off[3], t[3], u[3], v[3], move[3];
	hsM3 toSrc, toAsset, rot;
	int turns = 0, moves;

	memset(delta, 0, sizeof(float) * 16);
	delta[0] = delta[5] = delta[10] = delta[15] = 1.0f;

	Basis(basis, toSrc, toAsset);
	for (int k = 0; k < 3; k++) {
		off[k] = offset[k];
		t[k] = target[k];
	}
	Constrain(sp, toSrc, toAsset, off);
	for (int k = 0; k < 3; k++)
		off[k] *= alpha;
	moves = V_Len(off) >= HS_TINY;
	if (!moves)
		off[0] = off[1] = off[2] = 0.0;

	M_Identity(rot);
	if ((sp->flags & (HS_ROTATE_X | HS_ROTATE_Y | HS_ROTATE_Z)) && moves) {
		hsM3 arc, src;
		for (int k = 0; k < 3; k++) {
			u[k] = t[k] - pivot[k];
			v[k] = t[k] + off[k] - pivot[k];
		}
		if (ShortestArc(arc, u, v)) {
			double roll, pitch, yaw;
			M_Mul(src, toSrc, arc);   /* the turn in the source axes: toSrc arc toAsset */
			M_Mul(src, src, toAsset);
			EulerFrom(src, &roll, &pitch, &yaw);
			if (!(sp->flags & HS_ROTATE_X)) roll = 0.0;
			if (!(sp->flags & HS_ROTATE_Y)) pitch = 0.0;
			if (!(sp->flags & HS_ROTATE_Z)) yaw = 0.0;
			if (roll != 0.0 || pitch != 0.0 || yaw != 0.0) {
				EulerTo(src, roll, pitch, yaw);
				M_Mul(rot, toAsset, src);
				M_Mul(rot, rot, toSrc);
				turns = 1;
			}
		}
	}
	if (!moves && !turns)
		return 0;

	/* x' = rot (x - target) + target + off: the turn about the bone's new location after the move */
	M_Vec(move, rot, t);
	for (int k = 0; k < 3; k++)
		move[k] = t[k] + off[k] - move[k];
	for (int c = 0; c < 3; c++)
		for (int r = 0; r < 3; r++)
			delta[c * 4 + r] = (float)rot[r][c];
	delta[12] = (float)move[0];
	delta[13] = (float)move[1];
	delta[14] = (float)move[2];
	return 1;
}
