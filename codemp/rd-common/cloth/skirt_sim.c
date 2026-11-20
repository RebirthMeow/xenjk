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
SkirtSim: portable C99 port of ClothResearch/skirt/skirt_sim.js. See skirt_sim.h
for the method, the credits and the precision notes. The structure follows the JS
function by function so the two can be diffed; JS-only conveniences (typed-array
fill/set, closures) are written out.

Float vs double: per-particle state is skirt_real. Frame-level maths (the reference
frame, its velocities and rotation, the stiffness mapping, the build-time geodesics)
is always double.
*/
#include "skirt_sim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef SKIRT_REAL_DOUBLE
#define K(x) x
#define SQRT sqrt
#define FLOOR floor
#define FABS fabs
#else
#define K(x) x##f
#define SQRT sqrtf
#define FLOOR floorf
#define FABS fabsf
#endif

#define INFL_MAX 64
#define DT_REF (1.0 / 60.0)
#define EPS K(1e-9)
#define EPSD 1e-9
#define BEND_MAX K(0.5)
#define SKIRT_PI 3.14159265358979323846
#define HASH_SIZE (1 << 16)
#define TETHER_KMAX 4

/* ---------------------------------------------------------------- fdlibm */
/* fdlibm-derived elementary functions (Sun Microsystems, freely redistributable:
"Permission to use, copy, modify, and distribute this software is freely granted, provided that this
notice is preserved."). sin, cos, atan2 and pow, bit-for-bit what the reference JavaScript engine
computes, so that the C solver and the JS reference round alike. */

typedef union { double d; struct { uint32_t lo, hi; } w; } skirt_dw_t;   /* little endian */

static int32_t fd_hi(double x) { skirt_dw_t u; u.d = x; return (int32_t)u.w.hi; }
static uint32_t fd_lo(double x) { skirt_dw_t u; u.d = x; return u.w.lo; }
static double fd_setlo(double x, uint32_t lo) { skirt_dw_t u; u.d = x; u.w.lo = lo; return u.d; }
static double fd_sethi(double x, int32_t hi) { skirt_dw_t u; u.d = x; u.w.hi = (uint32_t)hi; return u.d; }
static double fd_make(int32_t hi, uint32_t lo) { skirt_dw_t u; u.w.hi = (uint32_t)hi; u.w.lo = lo; return u.d; }

static double fd_kernel_sin(double x, double y, int iy)
{
	static const double half = 5.00000000000000000000e-01,
		S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
		S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
	double z, r, v;
	int32_t ix = fd_hi(x) & 0x7fffffff;
	if (ix < 0x3e400000) { if ((int)x == 0) return x; }
	z = x * x;
	v = z * x;
	r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
	if (iy == 0) return x + v * (S1 + z * r);
	return x - ((z * (half * y - v * r) - y) - v * S1);
}

static double fd_kernel_cos(double x, double y)
{
	static const double one = 1.0,
		C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
		C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
	double a, iz, z, r, qx;
	int32_t ix = fd_hi(x) & 0x7fffffff;
	if (ix < 0x3e400000) { if ((int)x == 0) return one; }
	z = x * x;
	r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
	if (ix < 0x3FD33333) return one - (0.5 * z - (z * r - x * y));
	if (ix > 0x3fe90000) qx = 0.28125;
	else qx = fd_make(ix - 0x00200000, 0);
	iz = 0.5 * z - qx;
	a = one - qx;
	return a - (iz - (z * r - x * y));
}

/* argument reduction for |x| <= 2^19 * pi/2; returns n mod 4 (sign applied), -1000 when out of range */
static int fd_rem_pio2(double x, double *y)
{
	static const double half = 5.00000000000000000000e-01, invpio2 = 6.36619772367581382433e-01,
		pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11,
		pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21,
		pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;
	double z, w, t, r, fn;
	int32_t n, ix, hx, i, j;
	hx = fd_hi(x);
	ix = hx & 0x7fffffff;
	if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; return 0; }
	if (ix < 0x4002d97c) {
		if (hx > 0) {
			z = x - pio2_1;
			if (ix != 0x3ff921fb) { y[0] = z - pio2_1t; y[1] = (z - y[0]) - pio2_1t; }
			else { z -= pio2_2; y[0] = z - pio2_2t; y[1] = (z - y[0]) - pio2_2t; }
			return 1;
		}
		z = x + pio2_1;
		if (ix != 0x3ff921fb) { y[0] = z + pio2_1t; y[1] = (z - y[0]) + pio2_1t; }
		else { z += pio2_2; y[0] = z + pio2_2t; y[1] = (z - y[0]) + pio2_2t; }
		return -1;
	}
	if (ix <= 0x413921fb) {
		t = fabs(x);
		n = (int32_t)(t * invpio2 + half);
		fn = (double)n;
		r = t - fn * pio2_1;
		w = fn * pio2_1t;   /* 1st round good to 85 bit */
		/* (the reference skips the cancellation test for n < 32 away from multiples of pi/2; the
		test itself never fires there, so always running it gives the same result) */
		{
			uint32_t high;
			j = ix >> 20;
			y[0] = r - w;
			high = (uint32_t)fd_hi(y[0]);
			i = j - (int32_t)((high >> 20) & 0x7ff);
			if (i > 16) {   /* 2nd iteration needed, good to 118 */
				t = r;
				w = fn * pio2_2;
				r = t - w;
				w = fn * pio2_2t - ((t - r) - w);
				y[0] = r - w;
				high = (uint32_t)fd_hi(y[0]);
				i = j - (int32_t)((high >> 20) & 0x7ff);
				if (i > 49) {   /* 3rd iteration need, 151 bits acc */
					t = r;
					w = fn * pio2_3;
					r = t - w;
					w = fn * pio2_3t - ((t - r) - w);
					y[0] = r - w;
				}
			}
		}
		y[1] = (r - y[0]) - w;
		if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
		return n;
	}
	return -1000;
}

static double fd_sin(double x)
{
	double y[2];
	int32_t ix = fd_hi(x) & 0x7fffffff;
	int n;
	if (ix <= 0x3fe921fb) return fd_kernel_sin(x, 0.0, 0);
	if (ix >= 0x7ff00000) return x - x;
	n = fd_rem_pio2(x, y);
	if (n == -1000) return sin(x);
	switch (n & 3) {
	case 0: return fd_kernel_sin(y[0], y[1], 1);
	case 1: return fd_kernel_cos(y[0], y[1]);
	case 2: return -fd_kernel_sin(y[0], y[1], 1);
	default: return -fd_kernel_cos(y[0], y[1]);
	}
}

static double fd_cos(double x)
{
	double y[2];
	int32_t ix = fd_hi(x) & 0x7fffffff;
	int n;
	if (ix <= 0x3fe921fb) return fd_kernel_cos(x, 0.0);
	if (ix >= 0x7ff00000) return x - x;
	n = fd_rem_pio2(x, y);
	if (n == -1000) return cos(x);
	switch (n & 3) {
	case 0: return fd_kernel_cos(y[0], y[1]);
	case 1: return -fd_kernel_sin(y[0], y[1], 1);
	case 2: return -fd_kernel_cos(y[0], y[1]);
	default: return fd_kernel_sin(y[0], y[1], 1);
	}
}

static double fd_atan(double x)
{
	static const double atanhi[] = { 4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00 };
	static const double atanlo[] = { 2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17 };
	static const double aT[] = { 3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01,
		9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02, -5.83357013379057348645e-02,
		4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02 };
	static const double one = 1.0, huge = 1.0e300;
	double w, s1, s2, z;
	int32_t ix, hx, id;
	hx = fd_hi(x);
	ix = hx & 0x7fffffff;
	if (ix >= 0x44100000) {
		if (ix > 0x7ff00000 || (ix == 0x7ff00000 && fd_lo(x) != 0)) return x + x;
		if (hx > 0) return atanhi[3] + atanlo[3];
		return -atanhi[3] - atanlo[3];
	}
	if (ix < 0x3fdc0000) {
		if (ix < 0x3e400000) { if (huge + x > one) return x; }
		id = -1;
	} else {
		x = fabs(x);
		if (ix < 0x3ff30000) {
			if (ix < 0x3fe60000) { id = 0; x = (2.0 * x - one) / (2.0 + x); }
			else { id = 1; x = (x - one) / (x + one); }
		} else {
			if (ix < 0x40038000) { id = 2; x = (x - 1.5) / (one + 1.5 * x); }
			else { id = 3; x = -1.0 / x; }
		}
	}
	z = x * x;
	w = z * z;
	s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
	s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
	if (id < 0) return x - x * (s1 + s2);
	z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
	return (hx < 0) ? -z : z;
}

static double fd_atan2(double y, double x)
{
	static const double tiny = 1.0e-300, zero = 0.0, pi_o_4 = 7.8539816339744827900E-01, pi_o_2 = 1.5707963267948965580E+00,
		pi = 3.1415926535897931160E+00, pi_lo = 1.2246467991473531772E-16;
	double z;
	int32_t k, m, hx, hy, ix, iy;
	uint32_t lx, ly;
	hx = fd_hi(x); lx = fd_lo(x); ix = hx & 0x7fffffff;
	hy = fd_hi(y); ly = fd_lo(y); iy = hy & 0x7fffffff;
	if (((uint32_t)ix | ((lx | (0u - lx)) >> 31)) > 0x7ff00000u || ((uint32_t)iy | ((ly | (0u - ly)) >> 31)) > 0x7ff00000u) return x + y;
	if (((uint32_t)(hx - 0x3ff00000) | lx) == 0) return fd_atan(y);
	m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
	if (((uint32_t)iy | ly) == 0) {
		switch (m) {
		case 0: case 1: return y;
		case 2: return pi + tiny;
		default: return -pi - tiny;
		}
	}
	if (((uint32_t)ix | lx) == 0) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;
	if (ix == 0x7ff00000) {
		if (iy == 0x7ff00000) {
			switch (m) {
			case 0: return pi_o_4 + tiny;
			case 1: return -pi_o_4 - tiny;
			case 2: return 3.0 * pi_o_4 + tiny;
			default: return -3.0 * pi_o_4 - tiny;
			}
		} else {
			switch (m) {
			case 0: return zero;
			case 1: return -zero;
			case 2: return pi + tiny;
			default: return -pi - tiny;
			}
		}
	}
	if (iy == 0x7ff00000) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;
	k = (iy - ix) >> 20;
	if (k > 60) { z = pi_o_2 + 0.5 * pi_lo; m &= 1; }
	else if (hx < 0 && k < -60) z = 0.0;
	else z = fd_atan(fabs(y / x));
	switch (m) {
	case 0: return z;
	case 1: return -z;
	case 2: return pi - (z - pi_lo);
	default: return (z - pi_lo) - pi;
	}
}

static double fd_pow(double x, double y)
{
	static const double bp[] = { 1.0, 1.5 },
		dp_h[] = { 0.0, 5.84962487220764160156e-01 },
		dp_l[] = { 0.0, 1.35003920212974897128e-08 },
		zero = 0.0, one = 1.0, two = 2.0, two53 = 9007199254740992.0, huge = 1.0e300, tiny = 1.0e-300,
		L1 = 5.99999999999994648725e-01, L2 = 4.28571428578550184252e-01, L3 = 3.33333329818377432918e-01,
		L4 = 2.72728123808534006489e-01, L5 = 2.30660745775561754067e-01, L6 = 2.06975017800338417784e-01,
		P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
		P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08,
		lg2 = 6.93147180559945286227e-01, lg2_h = 6.93147182464599609375e-01, lg2_l = -1.90465429995776804525e-09,
		ovt = 8.0085662595372944372e-17,
		cp = 9.61796693925975554329e-01, cp_h = 9.61796700954437255859e-01, cp_l = -7.02846165095275826516e-09,
		ivln2 = 1.44269504088896338700e+00, ivln2_h = 1.44269502162933349609e+00, ivln2_l = 1.92596299112661746887e-08;
	double z, ax, z_h, z_l, p_h, p_l;
	double y1, t1, t2, r, s, t, u, v, w;
	int32_t i, j, k, yisint, n;
	int32_t hx, hy, ix, iy;
	uint32_t lx, ly;

	hx = fd_hi(x); lx = fd_lo(x);
	hy = fd_hi(y); ly = fd_lo(y);
	ix = hx & 0x7fffffff;
	iy = hy & 0x7fffffff;

	if ((uint32_t)(iy | (int32_t)ly) == 0) return one;
	if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lx != 0) || iy > 0x7ff00000 || (iy == 0x7ff00000 && ly != 0)) return x + y;

	yisint = 0;
	if (hx < 0) {
		if (iy >= 0x43400000) yisint = 2;
		else if (iy >= 0x3ff00000) {
			k = (iy >> 20) - 0x3ff;
			if (k > 20) {
				j = (int32_t)(ly >> (52 - k));
				if ((int32_t)((uint32_t)j << (52 - k)) == (int32_t)ly) yisint = 2 - (j & 1);
			} else if (ly == 0) {
				j = iy >> (20 - k);
				if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
			}
		}
	}

	if (ly == 0) {
		if (iy == 0x7ff00000) {
			if ((uint32_t)(ix - 0x3ff00000) == 0 && lx == 0) return y - y;
			else if (ix >= 0x3ff00000) return (hy >= 0) ? y : zero;
			else return (hy < 0) ? -y : zero;
		}
		if (iy == 0x3ff00000) { if (hy < 0) return one / x; else return x; }
		if (hy == 0x40000000) return x * x;
		if (hy == 0x3fe00000) { if (hx >= 0) return sqrt(x); }
	}

	ax = fabs(x);
	if (lx == 0) {
		if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {
			z = ax;
			if (hy < 0) z = one / z;
			if (hx < 0) {
				if (((ix - 0x3ff00000) | yisint) == 0) z = (z - z) / (z - z);
				else if (yisint == 1) z = -z;
			}
			return z;
		}
	}

	n = (hx >> 31) + 1;
	if ((n | yisint) == 0) return (x - x) / (x - x);

	s = one;
	if ((n | (yisint - 1)) == 0) s = -one;

	if (iy > 0x41e00000) {
		if (iy > 0x43f00000) {
			if (ix <= 0x3fefffff) return (hy < 0) ? huge * huge : tiny * tiny;
			if (ix >= 0x3ff00000) return (hy > 0) ? huge * huge : tiny * tiny;
		}
		if (ix < 0x3fefffff) return (hy < 0) ? s * huge * huge : s * tiny * tiny;
		if (ix > 0x3ff00000) return (hy > 0) ? s * huge * huge : s * tiny * tiny;
		t = ax - one;
		w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
		u = ivln2_h * t;
		v = t * ivln2_l - w * ivln2;
		t1 = u + v;
		t1 = fd_setlo(t1, 0);
		t2 = v - (t1 - u);
	} else {
		double ss, s2, s_h, s_l, t_h, t_l;
		n = 0;
		if (ix < 0x00100000) { ax *= two53; n -= 53; ix = fd_hi(ax); }
		n += ((ix) >> 20) - 0x3ff;
		j = ix & 0x000fffff;
		ix = j | 0x3ff00000;
		if (j <= 0x3988E) k = 0;
		else if (j < 0xBB67A) k = 1;
		else { k = 0; n += 1; ix -= 0x00100000; }
		ax = fd_sethi(ax, ix);

		u = ax - bp[k];
		v = one / (ax + bp[k]);
		ss = u * v;
		s_h = ss;
		s_h = fd_setlo(s_h, 0);
		t_h = zero;
		t_h = fd_sethi(t_h, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
		t_l = ax - (t_h - bp[k]);
		s_l = v * ((u - s_h * t_h) - s_h * t_l);
		s2 = ss * ss;
		r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
		r += s_l * (s_h + ss);
		s2 = s_h * s_h;
		t_h = 3.0 + s2 + r;
		t_h = fd_setlo(t_h, 0);
		t_l = r - ((t_h - 3.0) - s2);
		u = s_h * t_h;
		v = s_l * t_h + t_l * ss;
		p_h = u + v;
		p_h = fd_setlo(p_h, 0);
		p_l = v - (p_h - u);
		z_h = cp_h * p_h;
		z_l = cp_l * p_h + p_l * cp + dp_l[k];
		t = (double)n;
		t1 = (((z_h + z_l) + dp_h[k]) + t);
		t1 = fd_setlo(t1, 0);
		t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
	}

	y1 = y;
	y1 = fd_setlo(y1, 0);
	p_l = (y - y1) * t1 + y * t2;
	p_h = y1 * t1;
	z = p_l + p_h;
	j = fd_hi(z); i = (int32_t)fd_lo(z);
	if (j >= 0x40900000) {
		if (((j - 0x40900000) | i) != 0) return s * huge * huge;
		else { if (p_l + ovt > z - p_h) return s * huge * huge; }
	} else if ((j & 0x7fffffff) >= 0x4090cc00) {
		if (((int32_t)((uint32_t)j - 0xc090cc00u) | i) != 0) return s * tiny * tiny;
		else { if (p_l <= z - p_h) return s * tiny * tiny; }
	}
	i = j & 0x7fffffff;
	k = (i >> 20) - 0x3ff;
	n = 0;
	if (i > 0x3fe00000) {
		n = j + (0x00100000 >> (k + 1));
		k = ((n & 0x7fffffff) >> 20) - 0x3ff;
		t = zero;
		t = fd_sethi(t, n & ~(0x000fffff >> k));
		n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
		if (j < 0) n = -n;
		p_h -= t;
	}
	t = p_l + p_h;
	t = fd_setlo(t, 0);
	u = t * lg2_h;
	v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
	z = u + v;
	w = v - (z - u);
	t = z * z;
	t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
	r = (z * t1) / (t1 - two) - (w + z * w);
	z = one - (r - z);
	j = fd_hi(z);
	j += (int32_t)((uint32_t)n << 20);
	if ((j >> 20) <= 0) {
		z = z * ldexp(1.0, n);   /* subnormal output (scalbn) */
	} else {
		z = fd_sethi(z, fd_hi(z) + (int32_t)((uint32_t)n << 20));
	}
	return s * z;
}

/* ---------------------------------------------------------------- params */

#define SKIRT_X_ROW(name, def) { #name, (double)(def), offsetof(skirt_params_t, name) },
static const skirt_param_info_t g_paramTable[] = {
	SKIRT_PARAM_LIST(SKIRT_X_ROW)
};
#undef SKIRT_X_ROW
#define N_PARAMS ((int)(sizeof(g_paramTable) / sizeof(g_paramTable[0])))

static const skirt_param_ui_t g_paramUi[] = {
	{ "inertiaModel", "inertia model A/B", 0, 1, 1, 0, 1 },
	{ "linVelScaleX", "lin vel scale x", 0, 1, 0.01, -1, 0 },
	{ "linVelScaleY", "lin vel scale y", 0, 1, 0.01, -1, 0 },
	{ "linVelScaleZ", "lin vel scale z", 0, 1, 0.01, -1, 0 },
	{ "angVelScale", "ang vel scale", 0, 1, 0.01, -1, 0 },
	{ "fictScale", "fictitious scale", 0, 1.5, 0.05, -1, 0 },
	{ "iterations", "iterations", 1, 10, 1, 0, 0 },
	{ "substeps", "substeps", 1, 8, 1, 0, 0 },
	{ "stiffScale_edge", "edge stiffness x", 0.05, 1, 0.01, -1, 0 },
	{ "stiffScale_bend", "bend stiffness x", 0, 1, 0.01, -1, 0 },
	{ "stiffScale_area", "area stiffness x", 0, 1, 0.01, -1, 0 },
	{ "stiffScale_tether", "tether stiffness x", 0.1, 1, 0.01, -1, 0 },
	{ "tetherScale", "tether scale", 1, 1.3, 0.01, -1, 0 },
	{ "tetherK", "tether anchors", 1, 4, 1, 0, 1 },
	{ "maxDistScale", "max distance x", 0.8, 1.2, 0.01, -1, 0 },
	{ "adStiffScale", "anim drive stiff x", 0, 4, 0.05, -1, 0 },
	{ "adDampScale", "anim drive damp x", 0, 4, 0.05, -1, 0 },
	{ "damping", "damping", 0, 0.2, 0.005, 3, 0 },
	{ "dampExp", "damping exponent", 0.25, 2, 0.05, -1, 0 },
	{ "localDamping", "local damping", 0, 0.2, 0.005, 3, 0 },
	{ "massScale", "mass x", 0.25, 4, 0.05, -1, 1 },
	{ "dragScale", "drag x", 0, 20, 0.5, 1, 0 },
	{ "liftScale", "lift x", 0, 20, 0.5, 1, 0 },
	{ "thickness", "thickness cm", 0.25, 4, 0.05, -1, 0 },
	{ "friction", "friction", 0, 20, 0.5, 1, 0 },
	{ "backDepth", "back depth cm", 1, 10, 0.1, 1, 0 },
	{ "trackContacts", "track contacts", 0, 1, 1, 0, 0 },
	{ "reacquire", "re-acquire contacts", 0, 2, 1, 0, 0 },
	{ "handCollision", "hand capsules", 0, 1, 1, 0, 0 },
	{ "handThickness", "hand thickness cm", 0, 2, 0.05, -1, 0 },
	{ "gravityScale", "gravity x", 0.9, 1.1, 0.01, -1, 0 },
	{ "maxFeltAcc", "max felt accel cm/s2", 0, 10000, 50, 0, 0 },
	{ "maxFeltAngAcc", "max felt ang accel deg/s2", 0, 20000, 100, 0, 0 },
	{ "flareCone", "flare cone deg", 0, 120, 1, 0, 0 },
};

const skirt_param_info_t *SkirtParams_Table(int *count)
{
	if (count) *count = N_PARAMS;
	return g_paramTable;
}

const skirt_param_ui_t *SkirtParams_UiTable(int *count)
{
	if (count) *count = (int)(sizeof(g_paramUi) / sizeof(g_paramUi[0]));
	return g_paramUi;
}

const skirt_param_info_t *SkirtParams_Find(const char *key)
{
	int i;
	if (!key) return NULL;
	for (i = 0; i < N_PARAMS; i++) if (strcmp(g_paramTable[i].key, key) == 0) return &g_paramTable[i];
	return NULL;
}

void SkirtParams_Defaults(skirt_params_t *p)
{
	int i;
	for (i = 0; i < N_PARAMS; i++) *(skirt_real *)((char *)p + g_paramTable[i].offset) = (skirt_real)g_paramTable[i].def;
}

void SkirtConfig_Defaults(skirt_config_t *c)
{
	c->gravity[0] = 0; c->gravity[1] = 0; c->gravity[2] = -980.665;
	c->density = 0.35; c->minPerParticleMass = 1e-4;
	c->edge = c->bend = c->area = c->tetherStiffness = 1;
	c->drag = c->lift = 0.035;
}

/* ---------------------------------------------------------------- state */

typedef struct { uint32_t *start; uint32_t *bone; float *w; } skirt_csr_t;

struct skirt_sim {
	skirt_params_t params;
	const skirt_asset_t *asset;
	int owned;           /* block came from malloc */
	int N, NT, NE, NB, NTet, M, cNT, nBones, refBone, nDyn, nIt, ready, nCaps;
	size_t blockSize;

	const float *X0, *maxDist;
	const uint32_t *tri;

	skirt_csr_t skin, cSkin;
	skirt_real *x, *v, *p, *Told, *Tnew, *Ts, *w, *mass, *push, *cNrm;
	float *adMap;
	uint8_t *kin;
	int32_t *cTri;
	uint32_t *dynList;
	skirt_real bary[3];

	skirt_real *A0;
	uint32_t *e0, *e1; skirt_real *eLen;
	uint32_t *adjStart, *adjList; double *adjLen;
	uint32_t *bq; skirt_real *bPhi0, *bLen0;
	uint32_t *tetStart, *tetAnchor; skirt_real *tetLen;

	/* collider */
	const float *cPos0; const uint32_t *cTriIdx;
	skirt_real *Cold, *Cnew, *Cs;
	uint32_t *vtStart, *vtList, *vvStart, *vvList;
	skirt_real cMaxEdge;
	int32_t *cVert;
	int32_t *sTri;       /* sidedContacts: the collider face each particle was last resolved outside of (-1 none) */
	skirt_real *coneC;   /* coneBody: per-particle cosine of the allowed flare angle (<= coneCos) */
	skirt_real *vRad;
	int32_t *hHead, *hStamp, *hNext;
	int stamp;
	skirt_real dv;       /* result of Walk */

	/* capsules */
	skirt_real *capBind, *capC, *capOld, *capNew, *capS, *capBox, *capBoxes, *capR;

	/* frame state, always double */
	double gravity[3];
	double refPose[16];
	double F[12], Fold[12];
	double tmp16[16];
	double D[9], Dfull[9];
	double VL[3], OL[3], Vs[3], Os[3], VsOld[3], OsOld[3], kickV[3], kickO[3];
	double carryV[3];                /* model B: linear kick beyond maxFeltAcc, carried rigidly */
	int carry;
	double Oc[3], OcOld[3];          /* frame angular velocity after the maxFeltAngAcc rate limit (raw scale) */
	double coneAxisL[3], coneAxisW[3];   /* flare cone axis: bind-pose down in the reference frame / component space */
	skirt_real coneCos;
	double wf[3], gL[3], aeroLin[3], aeroAng[3], dvec[3];
	skirt_real grad[12];

	/* per-step coefficients */
	skirt_real kEdge, kBend, kArea, kTether, kAD, dAD, dampF;
	int adUseMap;

	skirt_stats_t stats;
	unsigned buildSerial;            /* rebuild() count (SkirtSim_Topology) */
	double poseDt;                   /* SkirtSim_SetPoseDt: the next step's pose time (0 = its dt) */

	/* build scratch (inside the block) */
	unsigned char *scratch;
	size_t scratchSize;
};

/* ---------------------------------------------------------------- helpers */

/* V8's Math.hypot (Kahan-compensated, scaled by the largest term), so rest lengths and the frame
normalisation round the same way as the JS reference. */
static double hypot3(double a, double b, double c)
{
	double v[3], mx = 0, sum = 0, comp = 0;
	int i;
	v[0] = fabs(a); v[1] = fabs(b); v[2] = fabs(c);
	for (i = 0; i < 3; i++) { if (v[i] != v[i]) return (double)NAN; if (v[i] > mx) mx = v[i]; }
	if (mx == HUGE_VAL) return HUGE_VAL;
	if (mx == 0) return 0;
	for (i = 0; i < 3; i++) {
		double n = v[i] / mx, summand = n * n - comp, prelim = sum + summand;
		comp = (prelim - sum) - summand;
		sum = prelim;
	}
	return sqrt(sum) * mx;
}

static int iround(double v) { return (int)floor(v + 0.5); }

static int ifloor(skirt_real v)
{
	if (!(FABS(v) < K(1e9))) return 0;
	return (int)FLOOR(v);
}

static unsigned cellHash(int ix, int iy, int iz)
{
	uint32_t a = (uint32_t)((int64_t)ix * 73856093), b = (uint32_t)((int64_t)iy * 19349663), c = (uint32_t)((int64_t)iz * 83492791);
	return (unsigned)((a ^ b ^ c) & (HASH_SIZE - 1));
}

/* Closest point on triangle (a,b,c) to p (Ericson 2005, 5.1.5). Writes the
barycentrics into out[0..2] and returns the region: 0 interior, 1 edge/vertex. */
static int closestBary(skirt_real px, skirt_real py, skirt_real pz, skirt_real ax, skirt_real ay, skirt_real az,
	skirt_real bx, skirt_real by, skirt_real bz, skirt_real cx, skirt_real cy, skirt_real cz, skirt_real *out)
{
	const skirt_real abx = bx - ax, aby = by - ay, abz = bz - az;
	const skirt_real acx = cx - ax, acy = cy - ay, acz = cz - az;
	const skirt_real apx = px - ax, apy = py - ay, apz = pz - az;
	const skirt_real d1 = abx * apx + aby * apy + abz * apz, d2 = acx * apx + acy * apy + acz * apz;
	if (d1 <= 0 && d2 <= 0) { out[0] = 1; out[1] = 0; out[2] = 0; return 1; }
	{
		const skirt_real bpx = px - bx, bpy = py - by, bpz = pz - bz;
		const skirt_real d3 = abx * bpx + aby * bpy + abz * bpz, d4 = acx * bpx + acy * bpy + acz * bpz;
		if (d3 >= 0 && d4 <= d3) { out[0] = 0; out[1] = 1; out[2] = 0; return 1; }
		{
			const skirt_real vc = d1 * d4 - d3 * d2;
			if (vc <= 0 && d1 >= 0 && d3 <= 0) { const skirt_real v = d1 / (d1 - d3); out[0] = 1 - v; out[1] = v; out[2] = 0; return 1; }
			{
				const skirt_real cpx = px - cx, cpy = py - cy, cpz = pz - cz;
				const skirt_real d5 = abx * cpx + aby * cpy + abz * cpz, d6 = acx * cpx + acy * cpy + acz * cpz;
				if (d6 >= 0 && d5 <= d6) { out[0] = 0; out[1] = 0; out[2] = 1; return 1; }
				{
					const skirt_real vb = d5 * d2 - d1 * d6;
					if (vb <= 0 && d2 >= 0 && d6 <= 0) { const skirt_real w = d2 / (d2 - d6); out[0] = 1 - w; out[1] = 0; out[2] = w; return 1; }
					{
						const skirt_real va = d3 * d6 - d5 * d4;
						if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
							const skirt_real w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); out[0] = 0; out[1] = 1 - w; out[2] = w; return 1;
						}
						{
							const skirt_real den = 1 / (va + vb + vc);
							const skirt_real v = vb * den, w = vc * den;
							out[0] = 1 - v - w; out[1] = v; out[2] = w; return 0;
						}
					}
				}
			}
		}
	}
}

/* Linear blend skinning of `pos` (float n*3) into component space, then into the sim frame:
out_L[j] = axis_j . (out_C - o). */
static void skinToLocal(const float *pos, const skirt_csr_t *sk, const float *mats, int n, const double *F, skirt_real *out)
{
	const uint32_t *start = sk->start, *bone = sk->bone;
	const float *w = sk->w;
	const skirt_real a0x = (skirt_real)F[0], a0y = (skirt_real)F[1], a0z = (skirt_real)F[2], a1x = (skirt_real)F[3], a1y = (skirt_real)F[4], a1z = (skirt_real)F[5];
	const skirt_real a2x = (skirt_real)F[6], a2y = (skirt_real)F[7], a2z = (skirt_real)F[8], ox = (skirt_real)F[9], oy = (skirt_real)F[10], oz = (skirt_real)F[11];
	int v;
	for (v = 0; v < n; v++) {
		const skirt_real x = pos[v * 3], y = pos[v * 3 + 1], z = pos[v * 3 + 2];
		skirt_real sx = 0, sy = 0, sz = 0;
		uint32_t k, e;
		for (k = start[v], e = start[v + 1]; k < e; k++) {
			const float *m = mats + bone[k] * 16;
			const skirt_real ww = w[k];
			sx += ww * (x * m[0] + y * m[4] + z * m[8] + m[12]);
			sy += ww * (x * m[1] + y * m[5] + z * m[9] + m[13]);
			sz += ww * (x * m[2] + y * m[6] + z * m[10] + m[14]);
		}
		sx -= ox; sy -= oy; sz -= oz;
		out[v * 3] = a0x * sx + a0y * sy + a0z * sz;
		out[v * 3 + 1] = a1x * sx + a1y * sy + a1z * sz;
		out[v * 3 + 2] = a2x * sx + a2y * sy + a2z * sz;
	}
}

/* 4x4 row-major inverse (general, cofactor). Used once at build time. */
static void invert4(const double *a, double *out)
{
	const double b00 = a[0] * a[5] - a[1] * a[4], b01 = a[0] * a[6] - a[2] * a[4], b02 = a[0] * a[7] - a[3] * a[4];
	const double b03 = a[1] * a[6] - a[2] * a[5], b04 = a[1] * a[7] - a[3] * a[5], b05 = a[2] * a[7] - a[3] * a[6];
	const double b06 = a[8] * a[13] - a[9] * a[12], b07 = a[8] * a[14] - a[10] * a[12], b08 = a[8] * a[15] - a[11] * a[12];
	const double b09 = a[9] * a[14] - a[10] * a[13], b10 = a[9] * a[15] - a[11] * a[13], b11 = a[10] * a[15] - a[11] * a[14];
	const double det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	const double id = 1 / det;
	out[0] = (a[5] * b11 - a[6] * b10 + a[7] * b09) * id;
	out[1] = (a[2] * b10 - a[1] * b11 - a[3] * b09) * id;
	out[2] = (a[13] * b05 - a[14] * b04 + a[15] * b03) * id;
	out[3] = (a[10] * b04 - a[9] * b05 - a[11] * b03) * id;
	out[4] = (a[6] * b08 - a[4] * b11 - a[7] * b07) * id;
	out[5] = (a[0] * b11 - a[2] * b08 + a[3] * b07) * id;
	out[6] = (a[14] * b02 - a[12] * b05 - a[15] * b01) * id;
	out[7] = (a[8] * b05 - a[10] * b02 + a[11] * b01) * id;
	out[8] = (a[4] * b10 - a[5] * b08 + a[7] * b06) * id;
	out[9] = (a[1] * b08 - a[0] * b10 - a[3] * b06) * id;
	out[10] = (a[12] * b04 - a[13] * b02 + a[15] * b00) * id;
	out[11] = (a[9] * b02 - a[8] * b04 - a[11] * b00) * id;
	out[12] = (a[5] * b07 - a[4] * b09 - a[6] * b06) * id;
	out[13] = (a[0] * b09 - a[1] * b07 + a[2] * b06) * id;
	out[14] = (a[13] * b01 - a[12] * b03 - a[14] * b00) * id;
	out[15] = (a[8] * b03 - a[9] * b01 + a[10] * b00) * id;
}

static void frameGS(const double *M, double *F);

/* Rodrigues: rotation by angle `ang` about unit axis (ux,uy,uz) into out[9] (row-major, column-vector convention). */
static void rotMat(double ux, double uy, double uz, double ang, double *out)
{
	const double c = fd_cos(ang), s = fd_sin(ang), t = 1 - c;
	out[0] = c + t * ux * ux; out[1] = t * ux * uy - s * uz; out[2] = t * ux * uz + s * uy;
	out[3] = t * ux * uy + s * uz; out[4] = c + t * uy * uy; out[5] = t * uy * uz - s * ux;
	out[6] = t * ux * uz - s * uy; out[7] = t * uy * uz + s * ux; out[8] = c + t * uz * uz;
}

/* Signed dihedral angle of the hinge (x3,x4 shared edge; x1,x2 wings) and its
gradients (Bridson, Marino & Fedkiw 2003, Sec 4.2), into g[0..11] = d/dx1..x4.
Returns NaN on degenerate geometry. */
static skirt_real dihedral(const skirt_real *P, int i1, int i2, int i3, int i4, skirt_real *g)
{
	const int x1 = i1 * 3, x2 = i2 * 3, x3 = i3 * 3, x4 = i4 * 3;
	const skirt_real ex = P[x4] - P[x3], ey = P[x4 + 1] - P[x3 + 1], ez = P[x4 + 2] - P[x3 + 2];
	const skirt_real el2 = ex * ex + ey * ey + ez * ez;
	skirt_real el, a1x, a1y, a1z, b1x, b1y, b1z, a2x, a2y, a2z, b2x, b2y, b2z;
	skirt_real n1x, n1y, n1z, n2x, n2y, n2z, n1l2, n2l2, lim, i1l, i2l;
	skirt_real u1x, u1y, u1z, u2x, u2y, u2z, cosT, cx, cy, cz, sinT, theta;
	skirt_real f1, f2, g1x, g1y, g1z, g2x, g2y, g2z, h1, h2, t13, t23, t14, t24, s;
	if (el2 < EPS) return (skirt_real)NAN;
	el = SQRT(el2);
	/* N1 = (x1-x3) x (x1-x4), N2 = (x2-x4) x (x2-x3) */
	a1x = P[x1] - P[x3]; a1y = P[x1 + 1] - P[x3 + 1]; a1z = P[x1 + 2] - P[x3 + 2];
	b1x = P[x1] - P[x4]; b1y = P[x1 + 1] - P[x4 + 1]; b1z = P[x1 + 2] - P[x4 + 2];
	a2x = P[x2] - P[x4]; a2y = P[x2 + 1] - P[x4 + 1]; a2z = P[x2 + 2] - P[x4 + 2];
	b2x = P[x2] - P[x3]; b2y = P[x2 + 1] - P[x3 + 1]; b2z = P[x2 + 2] - P[x3 + 2];
	n1x = a1y * b1z - a1z * b1y; n1y = a1z * b1x - a1x * b1z; n1z = a1x * b1y - a1y * b1x;
	n2x = a2y * b2z - a2z * b2y; n2y = a2z * b2x - a2x * b2z; n2z = a2x * b2y - a2y * b2x;
	n1l2 = n1x * n1x + n1y * n1y + n1z * n1z; n2l2 = n2x * n2x + n2y * n2y + n2z * n2z;
	/* skip squashed wings (height below 2% of the hinge): the gradient is ill-conditioned there */
	lim = K(4e-4) * el2 * el2;
	if (n1l2 < lim || n2l2 < lim || n1l2 < EPS || n2l2 < EPS) return (skirt_real)NAN;
	i1l = 1 / SQRT(n1l2); i2l = 1 / SQRT(n2l2);
	u1x = n1x * i1l; u1y = n1y * i1l; u1z = n1z * i1l; u2x = n2x * i2l; u2y = n2y * i2l; u2z = n2z * i2l;
	cosT = u1x * u2x + u1y * u2y + u1z * u2z;
	cx = u1y * u2z - u1z * u2y; cy = u1z * u2x - u1x * u2z; cz = u1x * u2y - u1y * u2x;
	sinT = (cx * ex + cy * ey + cz * ez) / el;
	theta = (skirt_real)fd_atan2((double)sinT, (double)cosT);
	/* gradients of theta */
	f1 = el / n1l2; f2 = el / n2l2;
	g1x = n1x * f1; g1y = n1y * f1; g1z = n1z * f1;    /* |E| N1/|N1|^2 */
	g2x = n2x * f2; g2y = n2y * f2; g2z = n2z * f2;
	h1 = 1 / n1l2; h2 = 1 / n2l2;
	t13 = (b1x * ex + b1y * ey + b1z * ez) / el; t23 = (a2x * ex + a2y * ey + a2z * ez) / el;   /* (x1-x4).E/|E|, (x2-x4).E/|E| */
	t14 = (a1x * ex + a1y * ey + a1z * ez) / el; t24 = (b2x * ex + b2y * ey + b2z * ez) / el;   /* (x1-x3).E/|E|, (x2-x3).E/|E| */
	/* Bridson's u_i are gradients of the angle with the sign convention theta
	increasing along N1 for x1; s carries our atan2 sign. */
	s = -1;
	g[0] = s * g1x; g[1] = s * g1y; g[2] = s * g1z;
	g[3] = s * g2x; g[4] = s * g2y; g[5] = s * g2z;
	g[6] = s * (t13 * n1x * h1 + t23 * n2x * h2); g[7] = s * (t13 * n1y * h1 + t23 * n2y * h2); g[8] = s * (t13 * n1z * h1 + t23 * n2z * h2);
	g[9] = s * -(t14 * n1x * h1 + t24 * n2x * h2); g[10] = s * -(t14 * n1y * h1 + t24 * n2y * h2); g[11] = s * -(t14 * n1z * h1 + t24 * n2z * h2);
	return theta;
}

/* Compact an nInfl-influence skin into CSR form (only nonzero weights). Counts when start == NULL. */
static uint32_t compactSkinCount(const int32_t *skinB, const float *skinW, int n, int nInfl)
{
	uint32_t nnz = 0;
	int i;
	for (i = 0; i < n * nInfl; i++) if (skinB[i] >= 0 && skinW[i] != 0) nnz++;
	return nnz;
}

static void compactSkinFill(const int32_t *skinB, const float *skinW, int n, int nInfl, skirt_csr_t *out)
{
	uint32_t k = 0;
	int v, j;
	for (v = 0; v < n; v++) {
		out->start[v] = k;
		for (j = 0; j < nInfl; j++) {
			const int32_t b = skinB[v * nInfl + j];
			const float ww = skinW[v * nInfl + j];
			if (b < 0 || ww == 0) continue;
			out->bone[k] = (uint32_t)b; out->w[k] = ww; k++;
		}
	}
	out->start[n] = k;
}

/* ---------------------------------------------------------------- memory layout */

typedef struct { int64_t key; int64_t rec; } skirt_keyrec_t;

typedef struct {
	unsigned char *base;   /* NULL while measuring */
	size_t off;
} skirt_arena_t;

static void *arenaGet(skirt_arena_t *a, size_t bytes)
{
	void *r = a->base ? (void *)(a->base + a->off) : NULL;
	a->off += (bytes + 15) & ~(size_t)15;
	return r;
}
#define ARENA(a, T, n) ((T *)arenaGet((a), sizeof(T) * (size_t)((n) > 0 ? (n) : 1)))

static size_t smax(size_t a, size_t b) { return a > b ? a : b; }

static size_t heapCapMax(int N, int NT)
{
	size_t a = (size_t)N * TETHER_KMAX * 16 + (size_t)N, b = (size_t)N + (size_t)TETHER_KMAX * 6 * (size_t)NT;
	return a < b ? a : b;
}

/* Assign every array of the sim from the arena. Same call measures (base NULL) and assigns. */
static size_t layout(skirt_sim_t *S, const skirt_asset_t *as, skirt_arena_t *a, uint32_t nnzCloth, uint32_t nnzCol)
{
	const int N = as->nVerts, NT = as->nTris, M = as->nColVerts, cNT = M ? as->nColTris : 0, nc = as->nCapsules;
	const int NEmax = 3 * NT;
	a->off = (sizeof(skirt_sim_t) + 15) & ~(size_t)15;
	S->x = ARENA(a, skirt_real, N * 3); S->v = ARENA(a, skirt_real, N * 3); S->p = ARENA(a, skirt_real, N * 3);
	S->Told = ARENA(a, skirt_real, N * 3); S->Tnew = ARENA(a, skirt_real, N * 3); S->Ts = ARENA(a, skirt_real, N * 3);
	S->w = ARENA(a, skirt_real, N); S->mass = ARENA(a, skirt_real, N); S->push = ARENA(a, skirt_real, N);
	S->cNrm = ARENA(a, skirt_real, N * 3);
	S->adMap = ARENA(a, float, N); S->kin = ARENA(a, uint8_t, N);
	S->cTri = ARENA(a, int32_t, N); S->dynList = ARENA(a, uint32_t, N);
	S->skin.start = ARENA(a, uint32_t, N + 1); S->skin.bone = ARENA(a, uint32_t, nnzCloth); S->skin.w = ARENA(a, float, nnzCloth);
	S->A0 = ARENA(a, skirt_real, NT);
	S->e0 = ARENA(a, uint32_t, NEmax); S->e1 = ARENA(a, uint32_t, NEmax); S->eLen = ARENA(a, skirt_real, NEmax);
	S->adjStart = ARENA(a, uint32_t, N + 1); S->adjList = ARENA(a, uint32_t, 2 * NEmax); S->adjLen = ARENA(a, double, 2 * NEmax);
	S->bq = ARENA(a, uint32_t, 4 * NEmax); S->bPhi0 = ARENA(a, skirt_real, NEmax); S->bLen0 = ARENA(a, skirt_real, NEmax);
	S->tetStart = ARENA(a, uint32_t, N + 1); S->tetAnchor = ARENA(a, uint32_t, N * TETHER_KMAX); S->tetLen = ARENA(a, skirt_real, N * TETHER_KMAX);
	if (M) {
		S->cSkin.start = ARENA(a, uint32_t, M + 1); S->cSkin.bone = ARENA(a, uint32_t, nnzCol); S->cSkin.w = ARENA(a, float, nnzCol);
		S->Cold = ARENA(a, skirt_real, M * 3); S->Cnew = ARENA(a, skirt_real, M * 3); S->Cs = ARENA(a, skirt_real, M * 3);
		S->vtStart = ARENA(a, uint32_t, M + 1); S->vtList = ARENA(a, uint32_t, 3 * cNT);
		S->vvStart = ARENA(a, uint32_t, M + 1); S->vvList = ARENA(a, uint32_t, 6 * cNT);
		S->cVert = ARENA(a, int32_t, N); S->sTri = ARENA(a, int32_t, N); S->coneC = ARENA(a, skirt_real, N); S->vRad = ARENA(a, skirt_real, M);
		S->hHead = ARENA(a, int32_t, HASH_SIZE); S->hStamp = ARENA(a, int32_t, HASH_SIZE); S->hNext = ARENA(a, int32_t, M);
	}
	if (nc) {
		S->capBind = ARENA(a, skirt_real, as->nBones * 3);
		S->capC = ARENA(a, skirt_real, nc * 6); S->capOld = ARENA(a, skirt_real, nc * 6);
		S->capNew = ARENA(a, skirt_real, nc * 6); S->capS = ARENA(a, skirt_real, nc * 6);
		S->capBox = ARENA(a, skirt_real, 6); S->capBoxes = ARENA(a, skirt_real, nc * 6); S->capR = ARENA(a, skirt_real, nc);
	}
	/* build scratch: edge keys + bend records, or the Dijkstra heap + labels, or collider counters */
	{
		const size_t sortBytes = (size_t)NEmax * 8 + (size_t)NEmax * sizeof(skirt_keyrec_t) + 64;
		const size_t hc = heapCapMax(N, NT);
		const size_t heapBytes = hc * (8 + 4 + 4) + (size_t)N * TETHER_KMAX * (8 + 4) + (size_t)N + 64;
		const size_t colBytes = (size_t)(M + 1) * 4 + 64;
		S->scratchSize = smax(sortBytes, smax(heapBytes, colBytes));
		S->scratch = ARENA(a, unsigned char, S->scratchSize);
	}
	return a->off;
}

size_t SkirtSim_RequiredSize(const skirt_asset_t *as)
{
	skirt_sim_t tmp;
	skirt_arena_t a;
	uint32_t nnzC, nnzK = 0;
	if (!as || as->nVerts <= 0 || as->nTris <= 0 || !as->pos || !as->tri || !as->maxDist || !as->skinBone || !as->skinW
		|| !as->refInv || as->nBones <= 0 || as->refBone < 0 || as->refBone >= as->nBones || as->nInfl <= 0 || as->nInfl > INFL_MAX) return 0;
	if (as->nColVerts > 0 && (!as->colPos || !as->colTri || !as->colSkinBone || !as->colSkinW || as->nColTris <= 0)) return 0;
	if (as->nCapsules > 0 && (!as->capBoneA || !as->capBoneB || !as->capRadius)) return 0;
	memset(&tmp, 0, sizeof(tmp));
	a.base = NULL; a.off = 0;
	nnzC = compactSkinCount(as->skinBone, as->skinW, as->nVerts, as->nInfl);
	if (as->nColVerts > 0) nnzK = compactSkinCount(as->colSkinBone, as->colSkinW, as->nColVerts, as->nInfl);
	return layout(&tmp, as, &a, nnzC, nnzK);
}

/* ---------------------------------------------------------------- build time */

static double triArea(const float *P, uint32_t a, uint32_t b, uint32_t c)
{
	const double ux = (double)P[b * 3] - P[a * 3], uy = (double)P[b * 3 + 1] - P[a * 3 + 1], uz = (double)P[b * 3 + 2] - P[a * 3 + 2];
	const double vx = (double)P[c * 3] - P[a * 3], vy = (double)P[c * 3 + 1] - P[a * 3 + 1], vz = (double)P[c * 3 + 2] - P[a * 3 + 2];
	return 0.5 * hypot3(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx);
}

static int cmpI64(const void *pa, const void *pb)
{
	const int64_t a = *(const int64_t *)pa, b = *(const int64_t *)pb;
	return a < b ? -1 : a > b ? 1 : 0;
}

static int cmpKeyRec(const void *pa, const void *pb)
{
	const skirt_keyrec_t *a = (const skirt_keyrec_t *)pa, *b = (const skirt_keyrec_t *)pb;
	if (a->key != b->key) return a->key < b->key ? -1 : 1;
	return a->rec < b->rec ? -1 : a->rec > b->rec ? 1 : 0;
}

static void buildEdges(skirt_sim_t *S)
{
	const uint32_t *tri = S->tri;
	const int N = S->N, NT = S->NT;
	int64_t *keys = (int64_t *)S->scratch;
	int t, k, i, ne, e;
	uint32_t j;
	const int nk = NT * 3;
	for (t = 0; t < NT; t++) for (k = 0; k < 3; k++) {
		const int64_t a = tri[t * 3 + k], b = tri[t * 3 + (k + 1) % 3];
		keys[t * 3 + k] = (a < b ? a : b) * N + (a < b ? b : a);
	}
	qsort(keys, (size_t)nk, sizeof(int64_t), cmpI64);
	ne = 0;
	for (i = 0; i < nk; i++) if (i == 0 || keys[i] != keys[i - 1]) ne++;
	S->NE = ne;
	j = 0;
	for (i = 0; i < nk; i++) {
		int64_t a, b;
		const float *X = S->X0;
		if (i > 0 && keys[i] == keys[i - 1]) continue;
		a = keys[i] / N; b = keys[i] - a * N;
		S->e0[j] = (uint32_t)a; S->e1[j] = (uint32_t)b;
		S->eLen[j] = (skirt_real)hypot3((double)X[a * 3] - X[b * 3], (double)X[a * 3 + 1] - X[b * 3 + 1], (double)X[a * 3 + 2] - X[b * 3 + 2]);
		j++;
	}
	/* CSR adjacency. The Dijkstra uses the double rest length of the edge. */
	{
		uint32_t *deg = S->adjStart;
		memset(deg, 0, sizeof(uint32_t) * (size_t)(N + 1));
		for (e = 0; e < ne; e++) { deg[S->e0[e] + 1]++; deg[S->e1[e] + 1]++; }
		for (i = 0; i < N; i++) deg[i + 1] += deg[i];
		{
			uint32_t *fill = (uint32_t *)S->scratch;   /* reuse: keys are no longer needed */
			memset(fill, 0, sizeof(uint32_t) * (size_t)N);
			for (e = 0; e < ne; e++) {
				const uint32_t a = S->e0[e], b = S->e1[e];
				const float *X = S->X0;
				const double len = hypot3((double)X[a * 3] - X[b * 3], (double)X[a * 3 + 1] - X[b * 3 + 1], (double)X[a * 3 + 2] - X[b * 3 + 2]);
				S->adjList[deg[a] + fill[a]] = b; S->adjLen[deg[a] + fill[a]++] = len;
				S->adjList[deg[b] + fill[b]] = a; S->adjLen[deg[b] + fill[b]++] = len;
			}
		}
	}
}

static void buildBends(skirt_sim_t *S)
{
	const uint32_t *tri = S->tri;
	const int N = S->N, NT = S->NT;
	skirt_keyrec_t *keys = (skirt_keyrec_t *)S->scratch;
	const int nk = NT * 3;
	int t, k, i, b, kk, nb;
	skirt_real *X = S->x;   /* rest-pose copy, scratch until the first reset */
	for (t = 0; t < NT; t++) for (k = 0; k < 3; k++) {
		const int64_t a = tri[t * 3 + k], c = tri[t * 3 + (k + 1) % 3];
		keys[t * 3 + k].key = (a < c ? a : c) * N + (a < c ? c : a);
		keys[t * 3 + k].rec = (int64_t)t * 3 + k;
	}
	qsort(keys, (size_t)nk, sizeof(skirt_keyrec_t), cmpKeyRec);
	nb = 0;
	for (i = 0; i + 1 < nk; i++) {
		int ta, ka, tb, kb;
		uint32_t i1, i2, i3, i4;
		if (keys[i].key != keys[i + 1].key) continue;
		ta = (int)(keys[i].rec / 3); ka = (int)(keys[i].rec % 3); tb = (int)(keys[i + 1].rec / 3); kb = (int)(keys[i + 1].rec % 3);
		i3 = tri[ta * 3 + ka]; i4 = tri[ta * 3 + (ka + 1) % 3];
		i1 = tri[ta * 3 + (ka + 2) % 3]; i2 = tri[tb * 3 + (kb + 2) % 3];
		if (i1 == i2) continue;
		S->bq[nb * 4] = i1; S->bq[nb * 4 + 1] = i2; S->bq[nb * 4 + 2] = i3; S->bq[nb * 4 + 3] = i4;
		nb++;
	}
	for (i = 0; i < N * 3; i++) X[i] = (skirt_real)S->X0[i];
	kk = 0;
	for (b = 0; b < nb; b++) {
		const uint32_t i1 = S->bq[b * 4], i2 = S->bq[b * 4 + 1], i3 = S->bq[b * 4 + 2], i4 = S->bq[b * 4 + 3];
		const skirt_real phi = dihedral(X, (int)i1, (int)i2, (int)i3, (int)i4, S->grad);
		if (!isfinite(phi)) continue;
		S->bq[kk * 4] = i1; S->bq[kk * 4 + 1] = i2; S->bq[kk * 4 + 2] = i3; S->bq[kk * 4 + 3] = i4;
		S->bPhi0[kk] = phi;
		S->bLen0[kk] = (skirt_real)hypot3((double)X[i1 * 3] - X[i2 * 3], (double)X[i1 * 3 + 1] - X[i2 * 3 + 1], (double)X[i1 * 3 + 2] - X[i2 * 3 + 2]);
		kk++;
	}
	S->NB = kk;
}

/* --- Dijkstra heap in flat arrays --- */
typedef struct { double *d; int32_t *v, *s; int n; } skirt_heap_t;

static void heapPush(skirt_heap_t *h, double d, int v, int s)
{
	int i = h->n++;
	while (i > 0) {
		const int pi = (i - 1) >> 1;
		if (h->d[pi] <= d) break;
		h->d[i] = h->d[pi]; h->v[i] = h->v[pi]; h->s[i] = h->s[pi]; i = pi;
	}
	h->d[i] = d; h->v[i] = v; h->s[i] = s;
}

static void heapPop(skirt_heap_t *h, double *pd, int *pv, int *ps)
{
	const double d = h->d[0];
	const int v = h->v[0], s = h->s[0];
	const double ld = h->d[--h->n];
	const int lv = h->v[h->n], ls = h->s[h->n];
	int i = 0;
	for (;;) {
		int c = 2 * i + 1;
		if (c >= h->n) break;
		if (c + 1 < h->n && h->d[c + 1] < h->d[c]) c++;
		if (h->d[c] >= ld) break;
		h->d[i] = h->d[c]; h->v[i] = h->v[c]; h->s[i] = h->s[c]; i = c;
	}
	h->d[i] = ld; h->v[i] = lv; h->s[i] = ls;
	*pd = d; *pv = v; *ps = s;
}

static void buildTethers(skirt_sim_t *S)
{
	const int N = S->N;
	int Kt = iround((double)S->params.tetherK);
	int i, j;
	double *labD;
	int32_t *labS;
	uint8_t *labN;
	uint32_t k;
	int nt;
	if (Kt > TETHER_KMAX) Kt = TETHER_KMAX;
	if (Kt < 1) Kt = 1;
	{
		const size_t hc = heapCapMax(N, S->NT);
		unsigned char *sc = S->scratch;
		skirt_heap_t heap;
		heap.d = (double *)sc; sc += hc * 8;
		heap.v = (int32_t *)sc; sc += hc * 4;
		heap.s = (int32_t *)sc; sc += hc * 4;
		labD = (double *)sc; sc += (size_t)N * TETHER_KMAX * 8;
		labS = (int32_t *)sc; sc += (size_t)N * TETHER_KMAX * 4;
		labN = (uint8_t *)sc;
		for (i = 0; i < N * Kt; i++) { labD[i] = HUGE_VAL; labS[i] = -1; }
		memset(labN, 0, (size_t)N);
		heap.n = 0;
		if (S->params.geodesic) {
			/* multi-source Dijkstra keeping the K best distinct sources per vertex */
			size_t cap = (size_t)N * Kt * 16 + (size_t)N, tight = (size_t)N + (size_t)Kt * 2 * (size_t)S->NE;
			if (tight < cap) cap = tight;   /* the guard below can never fire past this bound */
			for (i = 0; i < N; i++) if (S->kin[i]) heapPush(&heap, 0, i, i);
			while (heap.n > 0) {
				double d; int v, s, dup, a;
				heapPop(&heap, &d, &v, &s);
				if (labN[v] >= Kt) continue;
				dup = 0;
				for (j = 0; j < labN[v]; j++) if (labS[v * Kt + j] == s) { dup = 1; break; }
				if (dup) continue;
				labD[v * Kt + labN[v]] = d; labS[v * Kt + labN[v]] = s; labN[v]++;
				for (a = (int)S->adjStart[v]; a < (int)S->adjStart[v + 1]; a++) {
					const int u = (int)S->adjList[a];
					if (labN[u] >= Kt || S->kin[u]) continue;
					if ((size_t)heap.n < cap) heapPush(&heap, d + S->adjLen[a], u, s);
				}
			}
		} else {
			const float *X = S->X0;
			int a;
			for (i = 0; i < N; i++) {
				if (S->kin[i]) continue;
				for (a = 0; a < N; a++) {
					double d;
					int n;
					if (!S->kin[a]) continue;
					d = hypot3((double)X[i * 3] - X[a * 3], (double)X[i * 3 + 1] - X[a * 3 + 1], (double)X[i * 3 + 2] - X[a * 3 + 2]);
					/* insertion into the K best */
					n = labN[i];
					if (n == Kt && d >= labD[i * Kt + Kt - 1]) continue;
					if (n < Kt) n = ++labN[i];
					j = n - 1;
					while (j > 0 && labD[i * Kt + j - 1] > d) { labD[i * Kt + j] = labD[i * Kt + j - 1]; labS[i * Kt + j] = labS[i * Kt + j - 1]; j--; }
					labD[i * Kt + j] = d; labS[i * Kt + j] = a;
				}
			}
		}
		nt = 0;
		for (i = 0; i < N; i++) if (!S->kin[i]) nt += labN[i];
		k = 0;
		for (i = 0; i < N; i++) {
			S->tetStart[i] = k;
			if (S->kin[i]) continue;
			for (j = 0; j < labN[i]; j++) { S->tetAnchor[k] = (uint32_t)labS[i * Kt + j]; S->tetLen[k] = (skirt_real)labD[i * Kt + j]; k++; }
		}
		S->tetStart[N] = k;
		S->NTet = (int)k;
		(void)nt;
	}
}

/* mass, kinematic set, tethers (anything that depends on eps_kin / massScale / tetherK / geodesic) */
static void rebuild(skirt_sim_t *S)
{
	const skirt_params_t *P = &S->params;
	const int N = S->N;
	const float *ad = S->asset->animDrive;
	const double dens = S->asset->config.density * 1e-4;   /* kg/m^2 -> kg/cm^2 */
	const double minM = S->asset->config.minPerParticleMass;
	int i, t, k;
	S->nDyn = 0;
	for (i = 0; i < N; i++) {
		S->kin[i] = (skirt_real)S->maxDist[i] <= P->eps_kin ? 1 : 0;
		if (!S->kin[i]) S->dynList[S->nDyn++] = (uint32_t)i;
		S->adMap[i] = (ad && ad[i] >= 0) ? ad[i] : 1.0f;   /* negative = map absent */
	}
	{
		double *acc = (double *)S->scratch;   /* mass accumulates in double like the JS Float64Array */
		for (i = 0; i < N; i++) acc[i] = 0;
		for (t = 0; t < S->NT; t++) {
			const double m3 = dens * (double)S->A0[t] / 3;
			for (k = 0; k < 3; k++) acc[S->tri[t * 3 + k]] += m3;
		}
		for (i = 0; i < N; i++) {
			double m = acc[i] > minM ? acc[i] : minM;
			S->mass[i] = (skirt_real)(m * (double)P->massScale);
			S->w[i] = S->kin[i] ? 0 : 1 / S->mass[i];
		}
	}
	buildTethers(S);
	S->buildSerial++;
}

/* ---------------------------------------------------------------- capsules */

/* Capsule end points in component space from skinning matrices: out[c*6 .. c*6+5] = A, B. */
static void capsulePoints(const skirt_sim_t *S, const float *mats, skirt_real *out)
{
	const skirt_asset_t *as = S->asset;
	int c, e;
	for (c = 0; c < S->nCaps; c++) for (e = 0; e < 2; e++) {
		const int bi = e == 0 ? as->capBoneA[c] : as->capBoneB[c];
		const float *m = mats + bi * 16;
		const skirt_real x = S->capBind[bi * 3], y = S->capBind[bi * 3 + 1], z = S->capBind[bi * 3 + 2];
		const int o = c * 6 + e * 3;
		out[o] = x * m[0] + y * m[4] + z * m[8] + m[12];
		out[o + 1] = x * m[1] + y * m[5] + z * m[9] + m[13];
		out[o + 2] = x * m[2] + y * m[6] + z * m[10] + m[14];
	}
}

/* ---------------------------------------------------------------- create */

skirt_sim_t *SkirtSim_CreateInPlace(const skirt_asset_t *as, const skirt_params_t *params, void *mem, size_t size)
{
	skirt_sim_t *S;
	skirt_arena_t a;
	const size_t need = SkirtSim_RequiredSize(as);
	uint32_t nnzC, nnzK = 0;
	int N, NT, M, i, t, k, v;
	if (!need || !mem || size < need) return NULL;
	memset(mem, 0, need);
	S = (skirt_sim_t *)mem;
	a.base = (unsigned char *)mem; a.off = 0;
	nnzC = compactSkinCount(as->skinBone, as->skinW, as->nVerts, as->nInfl);
	if (as->nColVerts > 0) nnzK = compactSkinCount(as->colSkinBone, as->colSkinW, as->nColVerts, as->nInfl);
	layout(S, as, &a, nnzC, nnzK);
	S->blockSize = need;
	S->asset = as;
	if (params) S->params = *params; else SkirtParams_Defaults(&S->params);
	N = S->N = as->nVerts; NT = S->NT = as->nTris;
	S->nBones = as->nBones; S->refBone = as->refBone;
	for (i = 0; i < 3; i++) S->gravity[i] = as->config.gravity[i];
	{
		double rinv[16];
		const float *r = as->refInv + as->refBone * 16;
		for (i = 0; i < 16; i++) rinv[i] = r[i];
		invert4(rinv, S->refPose);
	}
	/* flare cone axis: the asset's gravity direction ("down" at bind), in component space and in the
	reference bone's bind frame (= the sim frame's local axes, which turn with the pelvis) */
	{
		double Fb[12], g0 = S->gravity[0], g1 = S->gravity[1], g2 = S->gravity[2];
		const double gl = hypot3(g0, g1, g2);
		if (gl > 0) { g0 /= gl; g1 /= gl; g2 /= gl; } else { g0 = 0; g1 = 0; g2 = -1; }
		frameGS(S->refPose, Fb);
		S->coneAxisW[0] = g0; S->coneAxisW[1] = g1; S->coneAxisW[2] = g2;
		for (i = 0; i < 3; i++) S->coneAxisL[i] = Fb[i * 3] * g0 + Fb[i * 3 + 1] * g1 + Fb[i * 3 + 2] * g2;
	}

	/* ---- particles ---- */
	S->X0 = as->pos; S->maxDist = as->maxDist; S->tri = as->tri;
	compactSkinFill(as->skinBone, as->skinW, N, as->nInfl, &S->skin);
	for (i = 0; i < N; i++) S->cTri[i] = -1;
	S->nDyn = 0;

	/* ---- topology ---- */
	for (t = 0; t < NT; t++) S->A0[t] = (skirt_real)triArea(S->X0, S->tri[t * 3], S->tri[t * 3 + 1], S->tri[t * 3 + 2]);
	buildEdges(S);
	buildBends(S);

	/* ---- collider ---- */
	M = S->M = as->nColVerts > 0 ? as->nColVerts : 0;
	if (M) {
		const uint32_t *tm = as->colTri;
		const float *cp = as->colPos;
		uint32_t *cnt = S->vtStart, *fillp = (uint32_t *)S->scratch;
		int q;
		skirt_real me = 0;
		S->cNT = as->nColTris;
		S->cPos0 = cp; S->cTriIdx = tm;
		compactSkinFill(as->colSkinBone, as->colSkinW, M, as->nInfl, &S->cSkin);
		/* vertex -> triangle CSR */
		for (i = 0; i < S->cNT * 3; i++) cnt[tm[i] + 1]++;
		for (i = 0; i < M; i++) cnt[i + 1] += cnt[i];
		memset(fillp, 0, sizeof(uint32_t) * (size_t)M);
		for (t = 0; t < S->cNT; t++) for (k = 0; k < 3; k++) { const uint32_t vv = tm[t * 3 + k]; S->vtList[cnt[vv] + fillp[vv]++] = (uint32_t)t; }
		for (t = 0; t < S->cNT; t++) for (k = 0; k < 3; k++) {
			const uint32_t ia = tm[t * 3 + k] * 3, ib = tm[t * 3 + (k + 1) % 3] * 3;
			const skirt_real l = (skirt_real)hypot3((double)cp[ia] - cp[ib], (double)cp[ia + 1] - cp[ib + 1], (double)cp[ia + 2] - cp[ib + 2]);
			if (l > me) me = l;   /* Math.max(me, l) */
		}
		S->cMaxEdge = me;
		/* vertex -> vertex CSR (one-ring neighbours, first-seen order), for the nearest-vertex walk */
		S->vvStart[0] = 0;
		for (v = 0; v < M; v++) {
			uint32_t n = S->vvStart[v];
			for (q = (int)cnt[v]; q < (int)cnt[v + 1]; q++) {
				const uint32_t tt = S->vtList[q];
				for (k = 0; k < 3; k++) {
					const uint32_t u = tm[tt * 3 + k];
					uint32_t z;
					int dup = 0;
					if (u == (uint32_t)v) continue;
					for (z = S->vvStart[v]; z < n; z++) if (S->vvList[z] == u) { dup = 1; break; }
					if (!dup) S->vvList[n++] = u;
				}
			}
			S->vvStart[v + 1] = n;
		}
		for (i = 0; i < N; i++) S->cVert[i] = -1;
		/* per-vertex reach of its one-ring (longest incident edge, +25% for skinning stretch) */
		for (t = 0; t < S->cNT; t++) for (k = 0; k < 3; k++) {
			const uint32_t ia = tm[t * 3 + k], ib = tm[t * 3 + (k + 1) % 3];
			const skirt_real l = (skirt_real)(1.25 * hypot3((double)cp[ia * 3] - cp[ib * 3], (double)cp[ia * 3 + 1] - cp[ib * 3 + 1], (double)cp[ia * 3 + 2] - cp[ib * 3 + 2]));
			if (l > S->vRad[ia]) S->vRad[ia] = l;
			if (l > S->vRad[ib]) S->vRad[ib] = l;
		}
		S->stamp = 0;
	}

	/* ---- capsules (component-space ends -> sim frame, handled like the collider) ---- */
	S->nCaps = as->nCapsules > 0 ? as->nCapsules : 0;
	if (S->nCaps) {
		int c, e;
		for (c = 0; c < S->nCaps; c++) S->capR[c] = (skirt_real)as->capRadius[c];
		for (c = 0; c < S->nCaps; c++) for (e = 0; e < 2; e++) {
			const int bi = e == 0 ? as->capBoneA[c] : as->capBoneB[c];
			double rinv[16], bindp[16];
			int q;
			if (bi < 0 || bi >= as->nBones) return NULL;
			for (q = 0; q < 16; q++) rinv[q] = as->refInv[bi * 16 + q];
			invert4(rinv, bindp);   /* bind pose = inv(refInv) */
			S->capBind[bi * 3] = (skirt_real)bindp[12]; S->capBind[bi * 3 + 1] = (skirt_real)bindp[13]; S->capBind[bi * 3 + 2] = (skirt_real)bindp[14];
		}
	}

	S->nIt = 1;   /* the JS has no value yet; any nIt >= 1 keeps `it == nIt - 1` false for it = -1 */
	rebuild(S);
	S->ready = 0;
	return S;
}

skirt_sim_t *SkirtSim_Create(const skirt_asset_t *as, const skirt_params_t *params)
{
	const size_t need = SkirtSim_RequiredSize(as);
	void *mem;
	skirt_sim_t *S;
	if (!need) return NULL;
	mem = malloc(need);
	if (!mem) return NULL;
	S = SkirtSim_CreateInPlace(as, params, mem, need);
	if (!S) { free(mem); return NULL; }
	S->owned = 1;
	return S;
}

void SkirtSim_Destroy(skirt_sim_t *S)
{
	if (S && S->owned) free(S);
}

const skirt_params_t *SkirtSim_Params(const skirt_sim_t *S) { return &S->params; }
const skirt_stats_t *SkirtSim_Stats(const skirt_sim_t *S) { return &S->stats; }
int SkirtSim_NumVerts(const skirt_sim_t *S) { return S->N; }
int SkirtSim_IsKinematic(const skirt_sim_t *S, int i) { return i >= 0 && i < S->N ? S->kin[i] : 0; }

/* Overrides come from the console: counts that bound loops or are rounded to int stay in a sane range. */
#define PARAM_ABS_MAX 1e6
static const struct { const char *key; double lo, hi; } g_paramClamp[] = {
	{ "iterations", 0, 64 }, { "maxIterations", 1, 64 }, { "substeps", 1, 64 },
	{ "edgePasses", 0, 16 }, { "bendPasses", 0, 16 }, { "tetherK", 1, TETHER_KMAX },
};

int SkirtSim_SetParam(skirt_sim_t *S, const char *key, double value)
{
	const skirt_param_info_t *info;
	skirt_real *slot, old;
	int c;
	if (!key || !isfinite(value)) return -1;
	if (value > PARAM_ABS_MAX) value = PARAM_ABS_MAX;
	if (value < -PARAM_ABS_MAX) value = -PARAM_ABS_MAX;
	for (c = 0; c < (int)(sizeof(g_paramClamp) / sizeof(g_paramClamp[0])); c++) {
		if (strcmp(key, g_paramClamp[c].key) != 0) continue;
		if (value < g_paramClamp[c].lo) value = g_paramClamp[c].lo;
		if (value > g_paramClamp[c].hi) value = g_paramClamp[c].hi;
	}
	if (strcmp(key, "linVelScale") == 0) {
		S->params.linVelScaleX = S->params.linVelScaleY = S->params.linVelScaleZ = (skirt_real)value;
		return 0;
	}
	info = SkirtParams_Find(key);
	if (!info) return -1;
	slot = (skirt_real *)((char *)&S->params + info->offset);
	old = *slot;
	*slot = (skirt_real)value;
	if ((strcmp(key, "eps_kin") == 0 || strcmp(key, "massScale") == 0 || strcmp(key, "tetherK") == 0 || strcmp(key, "geodesic") == 0)
		&& old != *slot && S->w) rebuild(S);
	return 0;
}

int SkirtSim_GetParam(const skirt_sim_t *S, const char *key, double *value)
{
	const skirt_param_info_t *info = SkirtParams_Find(key);
	if (!info) return -1;
	*value = (double)*(const skirt_real *)((const char *)&S->params + info->offset);
	return 0;
}

/* ---------------------------------------------------------------- frame kinematics */

/* pelvis frame from the skinning matrices: P = inv(refInv_ref) * mats_ref (row-vector) */
static void frameFrom(skirt_sim_t *S, const float *mats, double *F)
{
	const double *A = S->refPose;
	const float *mb = mats + S->refBone * 16;
	double *M = S->tmp16;
	int r, c;
	for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) {
		M[r * 4 + c] = A[r * 4] * mb[c] + A[r * 4 + 1] * mb[4 + c] + A[r * 4 + 2] * mb[8 + c] + A[r * 4 + 3] * mb[12 + c];
	}
	if (!S->params.localSpace) {
		for (r = 0; r < 12; r++) F[r] = 0;
		F[0] = 1; F[4] = 1; F[8] = 1;
		return;
	}
	frameGS(M, F);
}

/* Gram-Schmidt rows 0,1 of a row-vector 4x4; row 2 = row0 x row1 (sign kept from the matrix); origin = row 3 */
static void frameGS(const double *M, double *F)
{
	{
		double x0 = M[0], y0 = M[1], z0 = M[2];
		double l = hypot3(x0, y0, z0); if (l == 0 || l != l) l = 1;
		x0 /= l; y0 /= l; z0 /= l;
		{
			double x1 = M[4], y1 = M[5], z1 = M[6];
			const double d = x0 * x1 + y0 * y1 + z0 * z1;
			double x2, y2, z2;
			x1 -= d * x0; y1 -= d * y0; z1 -= d * z0;
			l = hypot3(x1, y1, z1); if (l == 0 || l != l) l = 1;
			x1 /= l; y1 /= l; z1 /= l;
			x2 = y0 * z1 - z0 * y1; y2 = z0 * x1 - x0 * z1; z2 = x0 * y1 - y0 * x1;
			if (x2 * M[8] + y2 * M[9] + z2 * M[10] < 0) { x2 = -x2; y2 = -y2; z2 = -z2; }
			F[0] = x0; F[1] = y0; F[2] = z0; F[3] = x1; F[4] = y1; F[5] = z1; F[6] = x2; F[7] = y2; F[8] = z2;
			F[9] = M[12]; F[10] = M[13]; F[11] = M[14];
		}
	}
}

static void skinAll(skirt_sim_t *S, const float *mats)
{
	skinToLocal(S->X0, &S->skin, mats, S->N, S->F, S->Tnew);
	if (S->M) skinToLocal(S->cPos0, &S->cSkin, mats, S->M, S->F, S->Cnew);
	if (S->nCaps) {
		const double *F = S->F;
		const skirt_real f0 = (skirt_real)F[0], f1 = (skirt_real)F[1], f2 = (skirt_real)F[2], f3 = (skirt_real)F[3], f4 = (skirt_real)F[4], f5 = (skirt_real)F[5];
		const skirt_real f6 = (skirt_real)F[6], f7 = (skirt_real)F[7], f8 = (skirt_real)F[8], f9 = (skirt_real)F[9], f10 = (skirt_real)F[10], f11 = (skirt_real)F[11];
		skirt_real *C = S->capC, *O = S->capNew;
		int k;
		capsulePoints(S, mats, C);
		for (k = 0; k < S->nCaps * 6; k += 3) {
			const skirt_real x = C[k] - f9, y = C[k + 1] - f10, z = C[k + 2] - f11;
			O[k] = f0 * x + f1 * y + f2 * z; O[k + 1] = f3 * x + f4 * y + f5 * z; O[k + 2] = f6 * x + f7 * y + f8 * z;
		}
	}
}

/* Felt-acceleration clamp, model B (SPEC 4.5). The frame kick a co-moving particle feels this frame is
k = V - R prev (R: how the boundary carries the previous velocity, NULL = identity). The part of k beyond amax,
e = k (|k| - amax) / |k|, goes to out (to be added rigidly to the particle velocities: carried, never felt).
Returns 1 if anything was cut. */
static int feltExcess(const double *V, const double *prev, const double *R, double amax, double *out)
{
	double q0, q1, q2, k0, k1, k2, kn, f;
	if (R) {
		q0 = R[0] * prev[0] + R[1] * prev[1] + R[2] * prev[2];
		q1 = R[3] * prev[0] + R[4] * prev[1] + R[5] * prev[2];
		q2 = R[6] * prev[0] + R[7] * prev[1] + R[8] * prev[2];
	} else { q0 = prev[0]; q1 = prev[1]; q2 = prev[2]; }
	k0 = V[0] - q0; k1 = V[1] - q1; k2 = V[2] - q2;
	kn = hypot3(k0, k1, k2);
	out[0] = out[1] = out[2] = 0;
	if (!(kn > amax)) return 0;
	f = (kn - amax) / kn;
	out[0] = k0 * f; out[1] = k1 * f; out[2] = k2 * f;
	return 1;
}

/* Model A's kick is applied directly: scale it to at most amax. */
static void clampLen(double *V, double amax)
{
	const double n = hypot3(V[0], V[1], V[2]);
	double f;
	if (!(n > amax)) return;
	f = amax / n;
	V[0] *= f; V[1] *= f; V[2] *= f;
}

/* Angular rate limit (SPEC 4.5): V := q + (V - q) * min(1, amax / |V - q|), q = R prev (the previous limited
angular velocity in the current axes). */
static void rateLimit(double *V, const double *prev, const double *R, double amax)
{
	double q0, q1, q2, d0, d1, d2, dn, f;
	q0 = R[0] * prev[0] + R[1] * prev[1] + R[2] * prev[2];
	q1 = R[3] * prev[0] + R[4] * prev[1] + R[5] * prev[2];
	q2 = R[6] * prev[0] + R[7] * prev[1] + R[8] * prev[2];
	d0 = V[0] - q0; d1 = V[1] - q1; d2 = V[2] - q2;
	dn = hypot3(d0, d1, d2);
	if (!(dn > amax)) return;
	f = amax / dn;
	V[0] = q0 + d0 * f; V[1] = q1 + d1 * f; V[2] = q2 + d2 * f;
}

/* Frame kinematics and the frame-boundary terms. */
static void frameKinematics(skirt_sim_t *S, double dt)
{
	const skirt_params_t *P = &S->params;
	const double *F = S->F, *Fo = S->Fold;
	double *Df = S->Dfull;
	int i, j, k;
	double sx, sy, sz, sn, cs, phi, ux, uy, uz, dox, doy, doz, vm, om;
	int teleport, angLim;
	double sxl, syl, szl, sa, fsc, gs;
	double *OL = S->OL, *VL = S->VL, *Vs = S->Vs, *Os = S->Os, *wf = S->wf, *D = S->D;
	const double *Ow;
	int fb;
	/* SkirtSim_SetPoseDt: the pose took pdt to get here; the step integrates dt. Velocities are measured over pdt,
	the felt displacement at the frame boundary (model B) is the felt velocity over dt. pdt == dt: as always. */
	const double pdt = S->poseDt > 0 ? S->poseDt : dt;
	S->poseDt = 0;
	/* Dfull = R_t^T R_{t-1}: Dfull[i][j] = axis_t_i . axis_{t-1}_j */
	for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) {
		Df[i * 3 + j] = F[i * 3] * Fo[j * 3] + F[i * 3 + 1] * Fo[j * 3 + 1] + F[i * 3 + 2] * Fo[j * 3 + 2];
	}
	/* log map: Dfull rotates fixed vectors by -theta about the frame's rotation axis */
	sx = 0.5 * (Df[7] - Df[5]); sy = 0.5 * (Df[2] - Df[6]); sz = 0.5 * (Df[3] - Df[1]);
	sn = hypot3(sx, sy, sz); cs = 0.5 * (Df[0] + Df[4] + Df[8] - 1);
	phi = fd_atan2(sn, cs);   /* angle of Dfull about (sx,sy,sz)/sn */
	ux = 0; uy = 0; uz = 1;
	if (sn > 1e-12) { ux = sx / sn; uy = sy / sn; uz = sz / sn; }
	/* frame angular velocity (local axes) = -axis*phi/dt */
	OL[0] = -ux * phi / pdt; OL[1] = -uy * phi / pdt; OL[2] = -uz * phi / pdt;
	dox = F[9] - Fo[9]; doy = F[10] - Fo[10]; doz = F[11] - Fo[11];
	VL[0] = (F[0] * dox + F[1] * doy + F[2] * doz) / pdt;
	VL[1] = (F[3] * dox + F[4] * doy + F[5] * doz) / pdt;
	VL[2] = (F[6] * dox + F[7] * doy + F[8] * doz) / pdt;
	/* teleport clamps */
	vm = hypot3(VL[0], VL[1], VL[2]); om = hypot3(OL[0], OL[1], OL[2]);
	teleport = 0;
	if (vm > (double)P->maxFrameVel || om > (double)P->maxFrameAngVel) teleport = 1;
	S->stats.teleport = teleport;
	sxl = P->linVelScaleX; syl = P->linVelScaleY; szl = P->linVelScaleZ; sa = P->angVelScale;
	/* angular-acceleration limit (SPEC 4.5): the frame angular velocity the cloth uses (Os, the fictitious
	terms, model B's rotation) follows the raw one at no more than maxFeltAngAcc; the rest turns rigidly */
	Ow = OL;
	angLim = !teleport && P->maxFeltAngAcc > 0;
	if (angLim) {
		double *Oc = S->Oc;
		Oc[0] = OL[0]; Oc[1] = OL[1]; Oc[2] = OL[2];
		rateLimit(Oc, S->OcOld, Df, (double)P->maxFeltAngAcc * SKIRT_PI / 180 * dt);
		Ow = Oc;
	}
	/* felt linear velocity in local axes */
	if (P->linAxes == 1) {
		/* scale per component axis, then into local */
		const double cx = sxl * dox / pdt, cy = syl * doy / pdt, cz = szl * doz / pdt;
		Vs[0] = F[0] * cx + F[1] * cy + F[2] * cz; Vs[1] = F[3] * cx + F[4] * cy + F[5] * cz; Vs[2] = F[6] * cx + F[7] * cy + F[8] * cz;
	} else { Vs[0] = sxl * VL[0]; Vs[1] = syl * VL[1]; Vs[2] = szl * VL[2]; }
	Os[0] = sa * Ow[0]; Os[1] = sa * Ow[1]; Os[2] = sa * Ow[2];
	if (teleport) { Vs[0] = Vs[1] = Vs[2] = 0; Os[0] = Os[1] = Os[2] = 0; }
	/* fictitious angular velocity */
	fb = iround((double)P->fictBase);
	if ((double)P->fictBase != (double)fb) fb = -1;   /* JS compares with ===: only exact 0 / 1 match */
	fsc = teleport ? 0 : (double)P->fictScale;
	for (k = 0; k < 3; k++) wf[k] = fsc * (fb == 0 ? Os[k] : fb == 1 ? Ow[k] : (1 - sa) * Ow[k]);
	S->carry = 0;
	if (P->inertiaModel == 1) {
		/* Model B: apply the felt part as a displacement at the frame boundary.
		x' = Rot(felt) x - d, with d the felt translation in local axes. */
		if (teleport) {
			for (k = 0; k < 9; k++) D[k] = 0;
			D[0] = D[4] = D[8] = 1;
			S->dvec[0] = S->dvec[1] = S->dvec[2] = 0;
		} else {
			if (angLim) {
				/* rotation by the limited angular velocity: axis -Ow/|Ow|, angle |Ow| dt */
				const double on = hypot3(Ow[0], Ow[1], Ow[2]);
				if (on > 0) rotMat(-Ow[0] / on, -Ow[1] / on, -Ow[2] / on, sa * (on * dt), D);
				else rotMat(0, 0, 1, 0, D);
			} else rotMat(ux, uy, uz, pdt == dt ? sa * phi : sa * phi * (dt / pdt), D);
			S->dvec[0] = Vs[0] * dt; S->dvec[1] = Vs[1] * dt; S->dvec[2] = Vs[2] * dt;
			/* linear felt-acceleration clamp: a co-moving particle's velocity Vs is carried across the boundary
			by D (rotateVel), so the felt kick is Vs - D VsOld; its part beyond maxFeltAcc is carried */
			if (P->maxFeltAcc > 0) S->carry = feltExcess(Vs, S->VsOld, P->rotateVel ? D : NULL, (double)P->maxFeltAcc * dt, S->carryV);
			{	/* diagnostics only (no effect on the state) */
				double e[3];
				feltExcess(Vs, S->VsOld, P->rotateVel ? D : NULL, 0, e);
				S->stats.feltAcc = hypot3(e[0], e[1], e[2]) / dt;
				S->stats.feltAccCut = S->carry ? hypot3(S->carryV[0], S->carryV[1], S->carryV[2]) / dt : 0;
			}
		}
		S->kickV[0] = S->kickV[1] = S->kickV[2] = 0; S->kickO[0] = S->kickO[1] = S->kickO[2] = 0;
		S->aeroLin[0] = 1 - sxl; S->aeroLin[1] = 1 - syl; S->aeroLin[2] = 1 - szl; S->aeroAng[0] = 1 - sa;
		if (P->aeroFrameVel != 2) { S->aeroLin[0] = S->aeroLin[1] = S->aeroLin[2] = 0; S->aeroAng[0] = 0; }
	} else {
		/* Model A: the frame carries the state; the felt part re-enters as velocity change. */
		double *kv = S->kickV, *ko = S->kickO;
		const double *a = S->VsOld, *b = S->OsOld;
		double cl;
		for (k = 0; k < 9; k++) D[k] = 0;
		D[0] = D[4] = D[8] = 1;
		S->dvec[0] = S->dvec[1] = S->dvec[2] = 0;
		/* previous felt velocities rotated into the current axes */
		for (i = 0; i < 3; i++) {
			kv[i] = -(Vs[i] - (Df[i * 3] * a[0] + Df[i * 3 + 1] * a[1] + Df[i * 3 + 2] * a[2]));
			ko[i] = -(Os[i] - (Df[i * 3] * b[0] + Df[i * 3 + 1] * b[1] + Df[i * 3 + 2] * b[2]));
		}
		/* linear felt-acceleration clamp: the cut part of the kick is carried with the frame */
		if (!teleport && P->maxFeltAcc > 0) clampLen(kv, (double)P->maxFeltAcc * dt);
		{
			const skirt_real af = P->aeroFrameVel;
			cl = af == 0 ? 0 : 1;
			S->aeroLin[0] = af == 2 ? 1 : sxl * cl; S->aeroLin[1] = af == 2 ? 1 : syl * cl; S->aeroLin[2] = af == 2 ? 1 : szl * cl;
			S->aeroAng[0] = af == 2 ? 1 : sa * cl;
		}
	}
	S->stats.angVel = hypot3(OL[0], OL[1], OL[2]) * 180 / SKIRT_PI;
	S->stats.angVelUsed = hypot3(Ow[0], Ow[1], Ow[2]) * 180 / SKIRT_PI;
	for (k = 0; k < 3; k++) { S->VsOld[k] = Vs[k]; S->OsOld[k] = Os[k]; S->OcOld[k] = teleport ? 0 : Ow[k]; }
	/* gravity in the sim frame */
	gs = P->gravityScale;
	{
		const double *g = S->gravity;
		S->gL[0] = gs * (F[0] * g[0] + F[1] * g[1] + F[2] * g[2]);
		S->gL[1] = gs * (F[3] * g[0] + F[4] * g[1] + F[5] * g[2]);
		S->gL[2] = gs * (F[6] * g[0] + F[7] * g[1] + F[8] * g[2]);
	}
}

/* apply x' = D x - d to an n*3 array (velocity: rotation only) */
static void xform(const skirt_sim_t *S, skirt_real *A, int n, int translate)
{
	const double *D = S->D, *d = S->dvec;
	const skirt_real d0 = (skirt_real)D[0], d1 = (skirt_real)D[1], d2 = (skirt_real)D[2], d3 = (skirt_real)D[3], d4 = (skirt_real)D[4];
	const skirt_real d5 = (skirt_real)D[5], d6 = (skirt_real)D[6], d7 = (skirt_real)D[7], d8 = (skirt_real)D[8];
	const skirt_real tx = translate ? (skirt_real)d[0] : 0, ty = translate ? (skirt_real)d[1] : 0, tz = translate ? (skirt_real)d[2] : 0;
	int i;
	for (i = 0; i < n; i++) {
		const skirt_real x = A[i * 3], y = A[i * 3 + 1], z = A[i * 3 + 2];
		A[i * 3] = d0 * x + d1 * y + d2 * z - tx;
		A[i * 3 + 1] = d3 * x + d4 * y + d5 * z - ty;
		A[i * 3 + 2] = d6 * x + d7 * y + d8 * z - tz;
	}
}

/* ---------------------------------------------------------------- collision */

/* Greedy descent over the collider's vertex graph from v towards (px,py,pz):
returns the reached vertex, its squared distance in S->dv. */
static int walk(skirt_sim_t *S, int v, skirt_real px, skirt_real py, skirt_real pz, int maxSteps)
{
	const skirt_real *Cs = S->Cs;
	const uint32_t *vvs = S->vvStart, *vvl = S->vvList;
	int k = v * 3;
	skirt_real ex = Cs[k] - px, ey = Cs[k + 1] - py, ez = Cs[k + 2] - pz;
	skirt_real dv = ex * ex + ey * ey + ez * ez;
	int stp;
	for (stp = 0; stp < maxSteps; stp++) {
		const int v0 = v;
		uint32_t q, e;
		for (q = vvs[v0], e = vvs[v0 + 1]; q < e; q++) {
			const int u = (int)vvl[q], ku = u * 3;
			const skirt_real ux = Cs[ku] - px, uy = Cs[ku + 1] - py, uz = Cs[ku + 2] - pz;
			const skirt_real du = ux * ux + uy * uy + uz * uz;
			if (du < dv) { dv = du; v = u; }
		}
		if (v == v0) break;
	}
	S->dv = dv;
	return v;
}

/* signed distance of p_i to collider triangle t (at Cs); writes the contact normal into cNrm */
static skirt_real signedDist(skirt_sim_t *S, int i, int t, skirt_real flip)
{
	const skirt_real *p = S->p, *Cs = S->Cs;
	const uint32_t *T = S->cTriIdx;
	skirt_real *bary = S->bary, *N = S->cNrm;
	const int j = i * 3;
	const int a = (int)T[t * 3] * 3, b = (int)T[t * 3 + 1] * 3, c = (int)T[t * 3 + 2] * 3;
	const skirt_real px = p[j], py = p[j + 1], pz = p[j + 2];
	const int region = closestBary(px, py, pz, Cs[a], Cs[a + 1], Cs[a + 2], Cs[b], Cs[b + 1], Cs[b + 2], Cs[c], Cs[c + 1], Cs[c + 2], bary);
	const skirt_real cx = bary[0] * Cs[a] + bary[1] * Cs[b] + bary[2] * Cs[c];
	const skirt_real cy = bary[0] * Cs[a + 1] + bary[1] * Cs[b + 1] + bary[2] * Cs[c + 1];
	const skirt_real cz = bary[0] * Cs[a + 2] + bary[1] * Cs[b + 2] + bary[2] * Cs[c + 2];
	/* outward face normal: front faces wind clockwise seen from outside, so (c-a) x (b-a) points out */
	const skirt_real ux = Cs[b] - Cs[a], uy = Cs[b + 1] - Cs[a + 1], uz = Cs[b + 2] - Cs[a + 2];
	const skirt_real vx = Cs[c] - Cs[a], vy = Cs[c + 1] - Cs[a + 1], vz = Cs[c + 2] - Cs[a + 2];
	skirt_real nx = vy * uz - vz * uy, ny = vz * ux - vx * uz, nz = vx * uy - vy * ux;
	const skirt_real nl = SQRT(nx * nx + ny * ny + nz * nz);
	skirt_real qx, qy, qz, sd;
	if (nl < EPS) return (skirt_real)INFINITY;
	nx *= flip / nl; ny *= flip / nl; nz *= flip / nl;
	qx = px - cx; qy = py - cy; qz = pz - cz;
	sd = qx * nx + qy * ny + qz * nz;
	if (region != 0 && S->params.edgeNormal) {
		const skirt_real ql = SQRT(qx * qx + qy * qy + qz * qz);
		if (ql > K(1e-6)) {
			const skirt_real sg = sd >= 0 ? K(1.0) : K(-1.0);
			N[j] = sg * qx / ql; N[j + 1] = sg * qy / ql; N[j + 2] = sg * qz / ql;
			return sg * ql;
		}
	}
	N[j] = nx; N[j + 1] = ny; N[j + 2] = nz;
	return sd;
}

/* Seed the per-particle nearest-collider-vertex tracking (brute force, once per reset). */
static void seedTracking(skirt_sim_t *S)
{
	const skirt_real *C = S->Cnew, *x = S->x;
	const int M = S->M;
	int n, v;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		int best = -1;
		skirt_real bd = (skirt_real)INFINITY;
		for (v = 0; v < M; v++) {
			const int k = v * 3;
			const skirt_real dx = C[k] - x[j], dy = C[k + 1] - x[j + 1], dz = C[k + 2] - x[j + 2];
			const skirt_real dd = dx * dx + dy * dy + dz * dz;
			if (dd < bd) { bd = dd; best = v; }
		}
		S->cVert[i] = best;
	}
}

/* S5: hash the collider vertices of this substep, then per dynamic particle
find the two nearest collider vertices and the closest triangle of their one-rings.

Robustness to fast colliders and deep penetration:
 - every particle keeps a tracked nearest collider vertex across substeps,
   advanced by a greedy walk over the collider's one-rings (coherent
   closest-feature tracking in the spirit of Lin & Canny 1991 / Mirtich
   1998's V-Clip, on a mesh graph). The hash only sees vertices within
   `cellSize`, so a particle that a fast limb swallowed deeper than that
   would be invisible and stay inside for good; the walk keeps the
   surface it came from, which also fixes the side it is pushed out to.
 - contacts are kept speculatively up to `contactMargin` outside the
   thickness (speculative contacts, Catto 2013; Macklin et al. 2014 Sec 5),
   so a particle that the later constraint iterations drag towards the body
   still has its triangle when the projection runs. */
static void contactSearch(skirt_sim_t *S)
{
	const skirt_params_t *P = &S->params;
	const skirt_real *Cs = S->Cs, *p = S->p;
	const int M = S->M;
	const skirt_real cs = P->cellSize > K(0.5) ? P->cellSize : K(0.5), ics = 1 / cs;
	const skirt_real tau = P->thickness;
	skirt_real R;
	skirt_real x0 = (skirt_real)INFINITY, y0 = (skirt_real)INFINITY, z0 = (skirt_real)INFINITY, x1 = -(skirt_real)INFINITY, y1 = -(skirt_real)INFINITY, z1 = -(skirt_real)INFINITY;
	int st, n, v;
	int32_t *head = S->hHead, *hs = S->hStamp, *next = S->hNext;
	skirt_real R2, back, flip, keep, farLim;
	int32_t *ctri = S->cTri, *cv = S->cVert, *stri = S->sTri;
	const uint32_t *T = S->cTriIdx, *vts = S->vtStart, *vtl = S->vtList;
	const int sided = P->sidedContacts != 0;
	const skirt_real sideD = P->sideDepth, sideD2 = sideD * sideD;
	skirt_real *N = S->cNrm;
	int track, contacts;
	{
		const skirt_real lim2 = tau + S->cMaxEdge + K(0.5);
		R = cs < lim2 ? cs : lim2;   /* vertex search radius (27-cell query covers cs) */
	}
	/* cloth AABB (dynamic) to cull collider vertices */
	for (n = 0; n < S->nDyn; n++) {
		const int j = (int)S->dynList[n] * 3;
		if (p[j] < x0) { x0 = p[j]; }
		if (p[j] > x1) { x1 = p[j]; }
		if (p[j + 1] < y0) { y0 = p[j + 1]; }
		if (p[j + 1] > y1) { y1 = p[j + 1]; }
		if (p[j + 2] < z0) { z0 = p[j + 2]; }
		if (p[j + 2] > z1) { z1 = p[j + 2]; }
	}
	x0 -= R; y0 -= R; z0 -= R; x1 += R; y1 += R; z1 += R;
	st = ++S->stamp;
	for (v = 0; v < M; v++) {
		const int j = v * 3;
		const skirt_real cx = Cs[j], cy = Cs[j + 1], cz = Cs[j + 2];
		unsigned b;
		if (cx < x0 || cx > x1 || cy < y0 || cy > y1 || cz < z0 || cz > z1) continue;
		b = cellHash(ifloor(cx * ics), ifloor(cy * ics), ifloor(cz * ics));
		if (hs[b] != st) { hs[b] = st; head[b] = -1; }
		next[v] = head[b]; head[b] = v;
	}
	R2 = R * R;
	back = P->backDepth; flip = P->flipNormals ? K(-1.0) : K(1.0); keep = tau + (P->contactMargin > 0 ? P->contactMargin : 0);
	track = P->trackContacts != 0;
	{
		const skirt_real mk = keep > back ? keep : back;
		farLim = (mk + S->cMaxEdge) * (mk + S->cMaxEdge);
	}
	contacts = 0;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		const skirt_real px = p[j], py = p[j + 1], pz = p[j + 2];
		int best1 = -1, best2 = -1;
		skirt_real d1 = R2, d2 = R2;
		int bt, r, q;
		skirt_real bd, d;
		const int at = sided ? stri[i] : -1;
		if (sided) stri[i] = -1;   /* set again by an anchored contact here or a resolved one in collide() */
		if (px >= x0 && px <= x1 && py >= y0 && py <= y1 && pz >= z0 && pz <= z1) {
			const int ix = ifloor(px * ics), iy = ifloor(py * ics), iz = ifloor(pz * ics);
			int dx, dy, dz;
			for (dx = -1; dx <= 1; dx++) for (dy = -1; dy <= 1; dy++) for (dz = -1; dz <= 1; dz++) {
				const unsigned b = cellHash(ix + dx, iy + dy, iz + dz);
				int vv;
				if (hs[b] != st) continue;
				for (vv = head[b]; vv >= 0; vv = next[vv]) {
					const int k = vv * 3;
					const skirt_real ex = Cs[k] - px, ey = Cs[k + 1] - py, ez = Cs[k + 2] - pz;
					const skirt_real dd = ex * ex + ey * ey + ez * ez;
					if (dd >= d2) continue;
					if (vv == best1) continue;
					if (dd < d1) { best2 = best1; d2 = d1; best1 = vv; d1 = dd; } else { best2 = vv; d2 = dd; }
				}
			}
		}
		if (track) {
			/* greedy walk from the tracked vertex towards the particle */
			int vw = cv[i];
			if (vw >= 0) {
				skirt_real dv;
				vw = walk(S, vw, px, py, pz, 64);
				dv = S->dv;
				if (best1 < 0) {
					/* only the walk sees the body: skip the triangle tests when the particle is
					clearly outside and beyond the thickness (outward of the vertex's face normal) */
					const skirt_real dist = SQRT(dv);
					cv[i] = vw;
					if (dv > farLim) { ctri[i] = -1; continue; }
					if (dist > keep + S->vRad[vw]) {
						const uint32_t t0 = vtl[vts[vw]];
						const int a = (int)T[t0 * 3] * 3, b = (int)T[t0 * 3 + 1] * 3, c = (int)T[t0 * 3 + 2] * 3;
						const skirt_real ux = Cs[b] - Cs[a], uy = Cs[b + 1] - Cs[a + 1], uz = Cs[b + 2] - Cs[a + 2];
						const skirt_real wx = Cs[c] - Cs[a], wy = Cs[c + 1] - Cs[a + 1], wz = Cs[c + 2] - Cs[a + 2];
						const skirt_real nx = flip * (wy * uz - wz * uy), ny = flip * (wz * ux - wx * uz), nz = flip * (wx * uy - wy * ux);
						const skirt_real nl = SQRT(nx * nx + ny * ny + nz * nz);
						const int k = vw * 3;
						if (nl > EPS && ((px - Cs[k]) * nx + (py - Cs[k + 1]) * ny + (pz - Cs[k + 2]) * nz) > K(0.5) * dist * nl) { ctri[i] = -1; continue; }
					}
					best1 = vw; d1 = dv;
				}
				else if (vw != best1 && dv < d2) {
					if (dv < d1) { best2 = best1; d2 = d1; best1 = vw; d1 = dv; } else { best2 = vw; d2 = dv; }
				}
			}
			if (best1 >= 0) cv[i] = best1;
		}
		/* closest triangle among the one-rings (plus last substep's triangle, for coherence with deep points) */
		bt = -1; bd = (skirt_real)INFINITY;
		for (r = 0; r < 3; r++) {
			int lo, hi;
			if (r == 0) { if (best1 < 0) continue; lo = (int)vts[best1]; hi = (int)vts[best1 + 1]; }
			else if (r == 1) { if (best2 < 0) continue; lo = (int)vts[best2]; hi = (int)vts[best2 + 1]; }
			else { if (ctri[i] < 0) continue; lo = -1; hi = 0; }
			for (q = lo; q < hi; q++) {
				const int t = q < 0 ? ctri[i] : (int)vtl[q];
				const int a = (int)T[t * 3] * 3, b = (int)T[t * 3 + 1] * 3, c = (int)T[t * 3 + 2] * 3;
				skirt_real qx, qy, qz, dd;
				closestBary(px, py, pz, Cs[a], Cs[a + 1], Cs[a + 2], Cs[b], Cs[b + 1], Cs[b + 2], Cs[c], Cs[c + 1], Cs[c + 2], S->bary);
				qx = S->bary[0] * Cs[a] + S->bary[1] * Cs[b] + S->bary[2] * Cs[c] - px;
				qy = S->bary[0] * Cs[a + 1] + S->bary[1] * Cs[b + 1] + S->bary[2] * Cs[c + 1] - py;
				qz = S->bary[0] * Cs[a + 2] + S->bary[1] * Cs[b + 2] + S->bary[2] * Cs[c + 2] - pz;
				dd = qx * qx + qy * qy + qz * qz;
				if (dd < bd) { bd = dd; bt = t; }
			}
		}
		ctri[i] = -1;
		if (at >= 0) {
			/* sidedContacts: p_i is behind `at`, the face it was last resolved outside of, by less than sideDepth.
			It went into the limb through that face (a fast limb, or projections that lost it), so it leaves
			through it too: the closest face counts only when it faces the same way (then at any depth up to
			sideDepth); one facing away is the far side of the limb, where the plain test would push it out. */
			skirt_real nbx = 0, nby = 0, nbz = 0, dA, ex, ey, ez;
			int use = -1;
			if (bt >= 0 && bt != at) { (void)signedDist(S, i, bt, flip); nbx = N[j]; nby = N[j + 1]; nbz = N[j + 2]; }
			dA = signedDist(S, i, at, flip);
			{
				const int a = (int)T[at * 3] * 3, b = (int)T[at * 3 + 1] * 3, c = (int)T[at * 3 + 2] * 3;
				const skirt_real *ba = S->bary;
				ex = px - (ba[0] * Cs[a] + ba[1] * Cs[b] + ba[2] * Cs[c]);
				ey = py - (ba[0] * Cs[a + 1] + ba[1] * Cs[b + 1] + ba[2] * Cs[c + 1]);
				ez = pz - (ba[0] * Cs[a + 2] + ba[1] * Cs[b + 2] + ba[2] * Cs[c + 2]);
			}
			if (dA < 0 && ex * ex + ey * ey + ez * ez < sideD2) {
				if (bt < 0 || bt == at || nbx * N[j] + nby * N[j + 1] + nbz * N[j + 2] < K(-0.25)) use = at;
				else use = bt;
			}
			if (use >= 0) {
				d = use == at ? dA : signedDist(S, i, use, flip);
				if (d > -sideD) { ctri[i] = use; stri[i] = use; contacts++; continue; }
			}
		}
		if (bt < 0) continue;
		/* signed distance on the chosen triangle */
		d = signedDist(S, i, bt, flip);
		/* the plane distance of a clamped (edge/vertex) closest point can be small for a
		particle far off to the side: also bound the true distance to the triangle */
		if (track && bd > (d >= 0 ? keep * keep : back * back)) continue;
		if (d < keep && d > -back) { ctri[i] = bt; if (d < tau) contacts++; }
	}
	S->stats.contacts += contacts;
}

/* Push particles out of the hand / forearm capsules (kinematic, infinitely massive). */
static void collideCapsules(skirt_sim_t *S)
{
	const skirt_params_t *P = &S->params;
	skirt_real *p = S->p;
	const skirt_real *Sc = S->capS, *B = S->capBox, *BB = S->capBoxes, *cr = S->capR;
	const skirt_real th = P->handThickness;
	int hits = 0, n, c;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		skirt_real x = p[j], y = p[j + 1], z = p[j + 2];
		if (x < B[0] || x > B[3] || y < B[1] || y > B[4] || z < B[2] || z > B[5]) continue;
		for (c = 0; c < S->nCaps; c++) {
			const int o = c * 6;
			skirt_real ax, ay, az, ux, uy, uz, l2, t, dx, dy, dz, R, d2, d, f;
			if (x < BB[o] || x > BB[o + 3] || y < BB[o + 1] || y > BB[o + 4] || z < BB[o + 2] || z > BB[o + 5]) continue;
			ax = Sc[o]; ay = Sc[o + 1]; az = Sc[o + 2];
			ux = Sc[o + 3] - ax; uy = Sc[o + 4] - ay; uz = Sc[o + 5] - az;
			l2 = ux * ux + uy * uy + uz * uz;
			t = l2 > EPS ? ((x - ax) * ux + (y - ay) * uy + (z - az) * uz) / l2 : 0;
			t = t < 0 ? 0 : t > 1 ? 1 : t;
			dx = x - ax - t * ux; dy = y - ay - t * uy; dz = z - az - t * uz;
			R = cr[c] + th; d2 = dx * dx + dy * dy + dz * dz;
			if (d2 >= R * R || d2 < K(1e-12)) continue;
			d = SQRT(d2); f = (R - d) / d;
			x += dx * f; y += dy * f; z += dz * f; hits++;
		}
		p[j] = x; p[j + 1] = y; p[j + 2] = z;
	}
	S->stats.handContacts += hits;
}

/* Walk the tracked nearest collider vertex to p_i and return the closest triangle of
its one-ring if it lies within the thickness (true distance), else -1. */
static int reacquire(skirt_sim_t *S, int i, skirt_real tau)
{
	const skirt_real *p = S->p, *Cs = S->Cs;
	const int j = i * 3;
	const skirt_real px = p[j], py = p[j + 1], pz = p[j + 2];
	int v = S->cVert[i];
	const uint32_t *T = S->cTriIdx;
	skirt_real lim, d0, r0, r1, bd;
	int k, bt;
	uint32_t q;
	if (v < 0) return -1;
	/* only shallow re-entries matter here (deep ones were contacts at search time);
	the tracked vertex is from this substep's search, so p moved little since */
	lim = tau + 1;
	k = v * 3;
	d0 = (Cs[k] - px) * (Cs[k] - px) + (Cs[k + 1] - py) * (Cs[k + 1] - py) + (Cs[k + 2] - pz) * (Cs[k + 2] - pz);
	r0 = lim + S->vRad[v] + 3;
	if (d0 > r0 * r0) return -1;
	v = walk(S, v, px, py, pz, 16);
	r1 = lim + S->vRad[v];
	if (S->dv > r1 * r1) return -1;
	bt = -1; bd = (skirt_real)INFINITY;
	for (q = S->vtStart[v]; q < S->vtStart[v + 1]; q++) {
		const int t = (int)S->vtList[q], a = (int)T[t * 3] * 3, b = (int)T[t * 3 + 1] * 3, c = (int)T[t * 3 + 2] * 3;
		skirt_real qx, qy, qz, dd;
		closestBary(px, py, pz, Cs[a], Cs[a + 1], Cs[a + 2], Cs[b], Cs[b + 1], Cs[b + 2], Cs[c], Cs[c + 1], Cs[c + 2], S->bary);
		qx = S->bary[0] * Cs[a] + S->bary[1] * Cs[b] + S->bary[2] * Cs[c] - px;
		qy = S->bary[0] * Cs[a + 1] + S->bary[1] * Cs[b + 1] + S->bary[2] * Cs[c + 1] - py;
		qz = S->bary[0] * Cs[a + 2] + S->bary[1] * Cs[b + 2] + S->bary[2] * Cs[c + 2] - pz;
		dd = qx * qx + qy * qy + qz * qz;
		if (dd < bd) { bd = dd; bt = t; }
	}
	if (bt < 0 || bd > lim * lim) return -1;
	return bt;
}

static void collide(skirt_sim_t *S, int it)
{
	const skirt_params_t *P = &S->params;
	skirt_real *p = S->p;
	const int32_t *ctri = S->cTri;
	const skirt_real tau = P->thickness, flip = P->flipNormals ? K(-1.0) : K(1.0);
	const skirt_real *N = S->cNrm;
	int32_t *stri = S->sTri;
	const int sided = P->sidedContacts != 0;
	int re, n;
	if (S->nCaps && P->handCollision && it >= 0) collideCapsules(S);
	if (!S->M) return;   /* capsules only (no body collider) */
	if (P->contactEvery == 1 && it > 0) contactSearch(S);
	re = P->trackContacts && P->reacquire && (P->reacquire == 2 || it == S->nIt - 1);
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n];
		int t = ctri[i];
		if (t < 0) {
			/* no contact at search time: the iterations may have dragged the particle onto the body since */
			skirt_real d, f;
			int j;
			if (!re) continue;
			t = reacquire(S, i, tau);
			if (t < 0) continue;
			d = signedDist(S, i, t, flip);
			if (d >= P->reacquireDepth * tau || d <= -P->backDepth) continue;
			j = i * 3; f = tau - d;
			p[j] += f * N[j]; p[j + 1] += f * N[j + 1]; p[j + 2] += f * N[j + 2];
			if (sided) stri[i] = t;
			continue;
		}
		{
			const skirt_real d = signedDist(S, i, t, flip);
			/* an anchored contact (sidedContacts) is pushed out through its face from deeper */
			const skirt_real lim = sided && stri[i] == t ? P->sideDepth : P->backDepth;
			int j;
			skirt_real f;
			if (d <= -lim) continue;
			if (sided) stri[i] = t;
			if (d >= tau) continue;
			j = i * 3; f = tau - d;
			p[j] += f * N[j]; p[j + 1] += f * N[j + 1]; p[j + 2] += f * N[j + 2];
			S->push[i] += f;
		}
	}
}

/* S7 friction relative to the collider surface (velocity level, Bridson 2002; or position level, Macklin 2014) */
static void friction(skirt_sim_t *S, skirt_real h, double dtd)
{
	const skirt_params_t *P = &S->params;
	skirt_real *v = S->v, *p = S->p;
	const skirt_real *x = S->x, *Cs = S->Cs, *Co = S->Cold, *Cn = S->Cnew, *Nn = S->cNrm;
	const int32_t *ctri = S->cTri;
	const uint32_t *T = S->cTriIdx;
	skirt_real *bary = S->bary;
	const skirt_real mu = P->friction;
	const skirt_real inv = (skirt_real)(1 / dtd);
	int n, k;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n];
		const int t = ctri[i];
		int j, a, b, c;
		skirt_real vcx, vcy, vcz, nx, ny, nz, rx, ry, rz, vn, tx, ty, tz, vt, vnn;
		if (t < 0 || S->push[i] <= 0) continue;
		j = i * 3;
		/* barycentrics of the contact point on the current substep collider */
		a = (int)T[t * 3] * 3; b = (int)T[t * 3 + 1] * 3; c = (int)T[t * 3 + 2] * 3;
		closestBary(p[j], p[j + 1], p[j + 2], Cs[a], Cs[a + 1], Cs[a + 2], Cs[b], Cs[b + 1], Cs[b + 2], Cs[c], Cs[c + 1], Cs[c + 2], bary);
		vcx = 0; vcy = 0; vcz = 0;
		for (k = 0; k < 3; k++) {
			const int o = k == 0 ? a : k == 1 ? b : c;
			const skirt_real bk = bary[k] * inv;
			vcx += bk * (Cn[o] - Co[o]); vcy += bk * (Cn[o + 1] - Co[o + 1]); vcz += bk * (Cn[o + 2] - Co[o + 2]);
		}
		if (!P->colliderLerp) { vcx = 0; vcy = 0; vcz = 0; }
		nx = Nn[j]; ny = Nn[j + 1]; nz = Nn[j + 2];
		rx = v[j] - vcx; ry = v[j + 1] - vcy; rz = v[j + 2] - vcz;
		vn = rx * nx + ry * ny + rz * nz;
		tx = rx - vn * nx; ty = ry - vn * ny; tz = rz - vn * nz;
		vt = SQRT(tx * tx + ty * ty + tz * tz);
		if (P->frictionModel == 1) {
			/* position level: tangential displacement relative to the surface point over this substep */
			const skirt_real dpen = S->push[i];
			const skirt_real dxt = vt * h;
			skirt_real s;
			if (dxt < mu * dpen) s = 0; else { const skirt_real den = dxt > EPS ? dxt : EPS; s = 1 - mu * dpen / den; if (s < 0) s = 0; }
			tx *= s; ty *= s; tz *= s;
			vnn = vn > 0 ? vn : 0;
			v[j] = vcx + tx + vnn * nx; v[j + 1] = vcy + ty + vnn * ny; v[j + 2] = vcz + tz + vnn * nz;
			p[j] = x[j] + v[j] * h; p[j + 1] = x[j + 1] + v[j + 1] * h; p[j + 2] = x[j + 2] + v[j + 2] * h;
			continue;
		}
		{
			const skirt_real dvn = S->push[i] / h;
			skirt_real s = 0;
			if (vt > EPS) { s = 1 - mu * dvn / vt; if (s < 0) s = 0; }
			tx *= s; ty *= s; tz *= s;
			vnn = vn > 0 ? vn : 0;
			v[j] = vcx + tx + vnn * nx; v[j + 1] = vcy + ty + vnn * ny; v[j + 2] = vcz + tz + vnn * nz;
		}
	}
}

/* ---------------------------------------------------------------- forces */

static void aero(skirt_sim_t *S, skirt_real h)
{
	const skirt_params_t *P = &S->params;
	const skirt_real *x = S->x, *w = S->w;
	skirt_real *v = S->v;
	const uint32_t *tri = S->tri;
	const skirt_real rho = (skirt_real)((double)P->fluidDensity * 1e-6);   /* kg/m^3 -> kg/cm^3 */
	const skirt_asset_t *as = S->asset;
	const skirt_real cd = (skirt_real)(as->config.drag * (double)P->dragScale), cl = (skirt_real)(as->config.lift * (double)P->liftScale);
	const skirt_real aa = (skirt_real)S->aeroAng[0];
	const skirt_real fvx = (skirt_real)S->aeroLin[0] * (skirt_real)S->VL[0], fvy = (skirt_real)S->aeroLin[1] * (skirt_real)S->VL[1], fvz = (skirt_real)S->aeroLin[2] * (skirt_real)S->VL[2];
	const skirt_real ox = aa * (skirt_real)S->OL[0], oy = aa * (skirt_real)S->OL[1], oz = aa * (skirt_real)S->OL[2];
	int t, k;
	for (t = 0; t < S->NT; t++) {
		const uint32_t a = tri[t * 3], b = tri[t * 3 + 1], c = tri[t * 3 + 2];
		const int A = (int)a * 3, B = (int)b * 3, C = (int)c * 3;
		skirt_real rx, ry, rz, ux, uy, uz, q2, q, qx, qy, qz, e1x, e1y, e1z, e2x, e2y, e2z, nx, ny, nz, n2, area, cth, base, fd, fx, fy, fz, lx, ly, lz, ll, s3;
		if (w[a] == 0 && w[b] == 0 && w[c] == 0) continue;
		rx = (x[A] + x[B] + x[C]) / 3; ry = (x[A + 1] + x[B + 1] + x[C + 1]) / 3; rz = (x[A + 2] + x[B + 2] + x[C + 2]) / 3;
		/* triangle velocity relative to still air */
		ux = (v[A] + v[B] + v[C]) / 3 + fvx + (oy * rz - oz * ry);
		uy = (v[A + 1] + v[B + 1] + v[C + 1]) / 3 + fvy + (oz * rx - ox * rz);
		uz = (v[A + 2] + v[B + 2] + v[C + 2]) / 3 + fvz + (ox * ry - oy * rx);
		q2 = ux * ux + uy * uy + uz * uz;
		if (q2 < K(1e-6)) continue;
		q = SQRT(q2); qx = -ux / q; qy = -uy / q; qz = -uz / q;
		e1x = x[B] - x[A]; e1y = x[B + 1] - x[A + 1]; e1z = x[B + 2] - x[A + 2];
		e2x = x[C] - x[A]; e2y = x[C + 1] - x[A + 1]; e2z = x[C + 2] - x[A + 2];
		nx = e1y * e2z - e1z * e2y; ny = e1z * e2x - e1x * e2z; nz = e1x * e2y - e1y * e2x;
		n2 = SQRT(nx * nx + ny * ny + nz * nz);
		if (n2 < EPS) continue;
		area = K(0.5) * n2; nx /= n2; ny /= n2; nz /= n2;
		cth = nx * qx + ny * qy + nz * qz;
		base = K(0.5) * rho * area * q2;
		/* drag along q, lift along n - c q (its product with c does not depend on n's sign) */
		fd = base * cd * FABS(cth);
		fx = fd * qx; fy = fd * qy; fz = fd * qz;
		lx = nx - cth * qx; ly = ny - cth * qy; lz = nz - cth * qz;
		ll = SQRT(lx * lx + ly * ly + lz * lz);
		if (ll > K(1e-6)) {
			const skirt_real fl = base * cl * cth / ll;
			fx += fl * lx; fy += fl * ly; fz += fl * lz;
		}
		s3 = h / 3;
		for (k = 0; k < 3; k++) {
			const uint32_t i = k == 0 ? a : k == 1 ? b : c;
			const skirt_real wi = w[i] * s3;
			if (wi == 0) continue;
			v[i * 3] += wi * fx; v[i * 3 + 1] += wi * fy; v[i * 3 + 2] += wi * fz;
		}
	}
}

/* Mueller 2007 Sec 3.5: damp only the deviation from the rigid-body motion of the whole piece */
static void localDamping(skirt_sim_t *S, skirt_real k)
{
	const skirt_real *x = S->x, *m = S->mass;
	skirt_real *v = S->v;
	skirt_real M = 0, cx = 0, cy = 0, cz = 0, vx = 0, vy = 0, vz = 0;
	skirt_real Lx = 0, Ly = 0, Lz = 0, I00 = 0, I01 = 0, I02 = 0, I11 = 0, I12 = 0, I22 = 0;
	skirt_real c00, c01, c02, det, c11, c12, c22, ox, oy, oz;
	int n;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		const skirt_real mi = m[i];
		M += mi; cx += mi * x[j]; cy += mi * x[j + 1]; cz += mi * x[j + 2]; vx += mi * v[j]; vy += mi * v[j + 1]; vz += mi * v[j + 2];
	}
	if (M <= 0) return;
	cx /= M; cy /= M; cz /= M; vx /= M; vy /= M; vz /= M;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		const skirt_real mi = m[i];
		const skirt_real rx = x[j] - cx, ry = x[j + 1] - cy, rz = x[j + 2] - cz;
		Lx += mi * (ry * v[j + 2] - rz * v[j + 1]); Ly += mi * (rz * v[j] - rx * v[j + 2]); Lz += mi * (rx * v[j + 1] - ry * v[j]);
		I00 += mi * (ry * ry + rz * rz); I11 += mi * (rx * rx + rz * rz); I22 += mi * (rx * rx + ry * ry);
		I01 -= mi * rx * ry; I02 -= mi * rx * rz; I12 -= mi * ry * rz;
	}
	/* omega = I^-1 L */
	c00 = I11 * I22 - I12 * I12; c01 = I02 * I12 - I01 * I22; c02 = I01 * I12 - I02 * I11;
	det = I00 * c00 + I01 * c01 + I02 * c02;
	if (FABS(det) < K(1e-20)) return;
	c11 = I00 * I22 - I02 * I02; c12 = I01 * I02 - I00 * I12; c22 = I00 * I11 - I01 * I01;
	ox = (c00 * Lx + c01 * Ly + c02 * Lz) / det; oy = (c01 * Lx + c11 * Ly + c12 * Lz) / det; oz = (c02 * Lx + c12 * Ly + c22 * Lz) / det;
	for (n = 0; n < S->nDyn; n++) {
		const int j = (int)S->dynList[n] * 3;
		const skirt_real rx = x[j] - cx, ry = x[j + 1] - cy, rz = x[j + 2] - cz;
		v[j] += k * (vx + oy * rz - oz * ry - v[j]);
		v[j + 1] += k * (vy + oz * rx - ox * rz - v[j + 1]);
		v[j + 2] += k * (vz + ox * ry - oy * rx - v[j + 2]);
	}
}

/* ---------------------------------------------------------------- constraints */

static void edges(skirt_sim_t *S)
{
	skirt_real *p = S->p;
	const skirt_real *w = S->w, *L = S->eLen;
	const skirt_real k = S->kEdge;
	const uint32_t *e0 = S->e0, *e1 = S->e1;
	int e;
	for (e = 0; e < S->NE; e++) {
		const uint32_t a = e0[e], b = e1[e];
		const skirt_real wa = w[a], wb = w[b], ws = wa + wb;
		int A, B;
		skirt_real dx, dy, dz, d, s;
		if (ws == 0) continue;
		A = (int)a * 3; B = (int)b * 3;
		dx = p[A] - p[B]; dy = p[A + 1] - p[B + 1]; dz = p[A + 2] - p[B + 2];
		d = SQRT(dx * dx + dy * dy + dz * dz);
		if (d < EPS) continue;
		s = k * (d - L[e]) / (d * ws);
		p[A] -= wa * s * dx; p[A + 1] -= wa * s * dy; p[A + 2] -= wa * s * dz;
		p[B] += wb * s * dx; p[B + 1] += wb * s * dy; p[B + 2] += wb * s * dz;
	}
}

static void bends(skirt_sim_t *S)
{
	skirt_real *p = S->p, *g = S->grad;
	const skirt_real *w = S->w;
	const skirt_real k = S->kBend;
	const uint32_t *q = S->bq;
	int b;
	if (S->params.bendModel == 1) {
		for (b = 0; b < S->NB; b++) {
			const uint32_t i = q[b * 4], j = q[b * 4 + 1];
			const skirt_real wa = w[i], wb = w[j], ws = wa + wb;
			int A, B;
			skirt_real dx, dy, dz, d, s;
			if (ws == 0) continue;
			A = (int)i * 3; B = (int)j * 3;
			dx = p[A] - p[B]; dy = p[A + 1] - p[B + 1]; dz = p[A + 2] - p[B + 2];
			d = SQRT(dx * dx + dy * dy + dz * dz);
			if (d < EPS) continue;
			s = k * (d - S->bLen0[b]) / (d * ws);
			p[A] -= wa * s * dx; p[A + 1] -= wa * s * dy; p[A + 2] -= wa * s * dz;
			p[B] += wb * s * dx; p[B + 1] += wb * s * dy; p[B + 2] += wb * s * dz;
		}
		return;
	}
	for (b = 0; b < S->NB; b++) {
		const int i1 = (int)q[b * 4], i2 = (int)q[b * 4 + 1], i3 = (int)q[b * 4 + 2], i4 = (int)q[b * 4 + 3];
		const skirt_real w1 = w[i1], w2 = w[i2], w3 = w[i3], w4 = w[i4];
		skirt_real th, c, den, s;
		int j;
		if (w1 + w2 + w3 + w4 == 0) continue;
		th = dihedral(p, i1, i2, i3, i4, g);
		if (th != th) continue;
		c = th - S->bPhi0[b];
		if (c > SKIRT_PI) c -= (skirt_real)(2 * SKIRT_PI); else if (c < -SKIRT_PI) c += (skirt_real)(2 * SKIRT_PI);
		/* the projection is a linearisation: bound one correction so a crumpled hinge cannot overshoot */
		if (c > BEND_MAX) c = BEND_MAX; else if (c < -BEND_MAX) c = -BEND_MAX;
		den = w1 * (g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) + w2 * (g[3] * g[3] + g[4] * g[4] + g[5] * g[5])
			+ w3 * (g[6] * g[6] + g[7] * g[7] + g[8] * g[8]) + w4 * (g[9] * g[9] + g[10] * g[10] + g[11] * g[11]);
		if (den < EPS) continue;
		s = k * c / den;
		j = i1 * 3; p[j] -= s * w1 * g[0]; p[j + 1] -= s * w1 * g[1]; p[j + 2] -= s * w1 * g[2];
		j = i2 * 3; p[j] -= s * w2 * g[3]; p[j + 1] -= s * w2 * g[4]; p[j + 2] -= s * w2 * g[5];
		j = i3 * 3; p[j] -= s * w3 * g[6]; p[j + 1] -= s * w3 * g[7]; p[j + 2] -= s * w3 * g[8];
		j = i4 * 3; p[j] -= s * w4 * g[9]; p[j + 1] -= s * w4 * g[10]; p[j + 2] -= s * w4 * g[11];
	}
}

static void areas(skirt_sim_t *S)
{
	skirt_real *p = S->p;
	const skirt_real *w = S->w, *A0 = S->A0;
	const skirt_real k = S->kArea;
	const uint32_t *tri = S->tri;
	int t;
	for (t = 0; t < S->NT; t++) {
		const uint32_t a = tri[t * 3], b = tri[t * 3 + 1], c = tri[t * 3 + 2];
		const skirt_real wa = w[a], wb = w[b], wc = w[c];
		int A, B, C;
		skirt_real ux, uy, uz, vx, vy, vz, nx, ny, nz, nl, C0, gbx, gby, gbz, gcx, gcy, gcz, gax, gay, gaz, den, s;
		if (wa + wb + wc == 0) continue;
		A = (int)a * 3; B = (int)b * 3; C = (int)c * 3;
		ux = p[B] - p[A]; uy = p[B + 1] - p[A + 1]; uz = p[B + 2] - p[A + 2];
		vx = p[C] - p[A]; vy = p[C + 1] - p[A + 1]; vz = p[C + 2] - p[A + 2];
		nx = uy * vz - uz * vy; ny = uz * vx - ux * vz; nz = ux * vy - uy * vx;
		nl = SQRT(nx * nx + ny * ny + nz * nz);
		if (nl < EPS) continue;
		nx /= nl; ny /= nl; nz /= nl;
		C0 = K(0.5) * nl - A0[t];
		/* grad_b = 1/2 (v x n)?  dA/db = 1/2 * (p3-p1) x n  ;  dA/dc = 1/2 * n x (p2-p1) */
		gbx = K(0.5) * (vy * nz - vz * ny); gby = K(0.5) * (vz * nx - vx * nz); gbz = K(0.5) * (vx * ny - vy * nx);
		gcx = K(0.5) * (ny * uz - nz * uy); gcy = K(0.5) * (nz * ux - nx * uz); gcz = K(0.5) * (nx * uy - ny * ux);
		gax = -gbx - gcx; gay = -gby - gcy; gaz = -gbz - gcz;
		den = wa * (gax * gax + gay * gay + gaz * gaz) + wb * (gbx * gbx + gby * gby + gbz * gbz) + wc * (gcx * gcx + gcy * gcy + gcz * gcz);
		if (den < EPS) continue;
		s = k * C0 / den;
		p[A] -= s * wa * gax; p[A + 1] -= s * wa * gay; p[A + 2] -= s * wa * gaz;
		p[B] -= s * wb * gbx; p[B + 1] -= s * wb * gby; p[B + 2] -= s * wb * gbz;
		p[C] -= s * wc * gcx; p[C + 1] -= s * wc * gcy; p[C + 2] -= s * wc * gcz;
	}
}

static void animDrive(skirt_sim_t *S)
{
	skirt_real *p = S->p;
	const skirt_real *Ts = S->Ts;
	const skirt_real k = S->kAD;
	const int useMap = S->adUseMap;
	int n;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		const skirt_real ki = useMap ? k * S->adMap[i] : k;
		p[j] += ki * (Ts[j] - p[j]); p[j + 1] += ki * (Ts[j + 1] - p[j + 1]); p[j + 2] += ki * (Ts[j + 2] - p[j + 2]);
	}
}

static void tethers(skirt_sim_t *S)
{
	skirt_real *p = S->p;
	const skirt_real k = S->kTether, sc = S->params.tetherScale;
	const uint32_t *st = S->tetStart, *an = S->tetAnchor;
	const skirt_real *L = S->tetLen;
	int active = 0, n;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		uint32_t t;
		for (t = st[i]; t < st[i + 1]; t++) {
			const int a = (int)an[t] * 3;
			const skirt_real dx = p[j] - p[a], dy = p[j + 1] - p[a + 1], dz = p[j + 2] - p[a + 2];
			const skirt_real d = SQRT(dx * dx + dy * dy + dz * dz), lim = sc * L[t];
			skirt_real f;
			if (d <= lim || d < EPS) continue;
			active++;
			f = k * (d - lim) / d;
			p[j] -= f * dx; p[j + 1] -= f * dy; p[j + 2] -= f * dz;
		}
	}
	S->stats.tethersActive = active;
}

static void maxDist(skirt_sim_t *S)
{
	skirt_real *p = S->p;
	const skirt_real *Ts = S->Ts;
	const float *md = S->maxDist;
	const skirt_real sc = S->params.maxDistScale;
	int n;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		const skirt_real dx = p[j] - Ts[j], dy = p[j + 1] - Ts[j + 1], dz = p[j + 2] - Ts[j + 2];
		const skirt_real r = md[i] * sc, d2 = dx * dx + dy * dy + dz * dz;
		skirt_real f;
		if (d2 <= r * r) continue;
		f = r / SQRT(d2);
		p[j] = Ts[j] + dx * f; p[j + 1] = Ts[j + 1] + dy * f; p[j + 2] = Ts[j + 2] + dz * f;
	}
}

/* Flare cap (SPEC 7.6): every dynamic particle stays inside the cone about the down axis d whose apex is its
tether anchor a (the geodesically nearest pinned vertex): angle(p - a, d) <= max(flareCone, angle(T - Ta, d)), so
the cap never pulls the cloth beyond where the animation itself puts it. A violating particle is rotated about
the apex onto the cone surface in the plane of d and p - a, keeping |p - a| (no fight with tethers or edges). */
static void coneCap(skirt_sim_t *S)
{
	skirt_real *p = S->p;
	const skirt_real *Ts = S->Ts;
	const double *ax = S->params.localSpace ? S->coneAxisL : S->coneAxisW;
	const skirt_real dx = (skirt_real)ax[0], dy = (skirt_real)ax[1], dz = (skirt_real)ax[2], cMax = S->coneCos;
	const uint32_t *st = S->tetStart, *an = S->tetAnchor;
	const int body = S->params.coneBody != 0 && S->M;
	int n, capped = 0;
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n], j = i * 3;
		int a;
		skirt_real rx, ry, rz, h, L, cl, qx, qy, qz, q, sl, f;
		if (st[i] == st[i + 1]) continue;
		a = (int)an[st[i]] * 3;
		rx = p[j] - p[a]; ry = p[j + 1] - p[a + 1]; rz = p[j + 2] - p[a + 2];
		h = rx * dx + ry * dy + rz * dz;
		L = SQRT(rx * rx + ry * ry + rz * rz);
		/* coneBody: the cap is for flare the cloth gets from the motion; where the body holds it out
		(a raised thigh under the dress) it keeps that angle, and the allowance relaxes in substep() */
		if (body && S->cTri[i] >= 0 && L > EPS && h < L * S->coneC[i]) S->coneC[i] = h / L;
		if (h >= L * cMax) continue;
		{
			const skirt_real tx = Ts[j] - Ts[a], ty = Ts[j + 1] - Ts[a + 1], tz = Ts[j + 2] - Ts[a + 2];
			const skirt_real tl = SQRT(tx * tx + ty * ty + tz * tz);
			cl = body && S->coneC[i] < cMax ? S->coneC[i] : cMax;
			if (tl > EPS) {
				const skirt_real ct = (tx * dx + ty * dy + tz * dz) / tl;
				if (ct < cl) cl = ct;
			}
			if (h >= L * cl) continue;
			qx = rx - h * dx; qy = ry - h * dy; qz = rz - h * dz;
			q = SQRT(qx * qx + qy * qy + qz * qz);
			if (q <= K(1e-6) * L) {
				/* on the axis (inverted): take the azimuth of the skinned pose */
				const skirt_real th = tx * dx + ty * dy + tz * dz;
				qx = tx - th * dx; qy = ty - th * dy; qz = tz - th * dz;
				q = SQRT(qx * qx + qy * qy + qz * qz);
				if (q <= EPS) continue;
			}
		}
		sl = 1 - cl * cl;
		sl = sl > 0 ? SQRT(sl) : 0;
		f = L * sl / q;
		h = L * cl;
		p[j] = p[a] + h * dx + f * qx; p[j + 1] = p[a + 1] + h * dy + f * qy; p[j + 2] = p[a + 2] + h * dz + f * qz;
		capped++;
	}
	S->stats.coneCapped += capped;
}

/* ---------------------------------------------------------------- step */

static void setupStiffness(skirt_sim_t *S, double dt, int nSub, int nIt)
{
	const skirt_params_t *P = &S->params;
	const skirt_config_t *cfg = &S->asset->config;
	const double nTot = nIt * nSub * (DT_REF / dt);
	const int mapRaw = P->stiffMapping == 1;
	const int adMode = iround((double)P->animDriveMapMode);
	double kk, gam;
#define MAPK(out, kin, nn) do { \
		kk = (kin); kk = kk > 0 ? kk : 0; kk = kk < 1 ? kk : 1; \
		if (mapRaw) (out) = (skirt_real)kk; else if (kk >= 1) (out) = 1; else (out) = (skirt_real)(1 - fd_pow(1 - kk, 1 / (nn))); \
	} while (0)
	MAPK(S->kEdge, cfg->edge * (double)P->stiffScale_edge, nTot);
	MAPK(S->kBend, cfg->bend * (double)P->stiffScale_bend, nTot);
	MAPK(S->kArea, cfg->area * (double)P->stiffScale_area, nTot);
	MAPK(S->kTether, cfg->tetherStiffness * (double)P->stiffScale_tether, nTot);
	if (adMode == 2) S->kAD = 0;
	else MAPK(S->kAD, (double)P->animDriveStiffness * (double)P->adStiffScale, P->adPerSubstep ? nSub * DT_REF / dt : nTot);
	if (adMode == 2) S->dAD = 0;
	else MAPK(S->dAD, (double)P->animDriveDamping * (double)P->adDampScale, nSub * DT_REF / dt);
#undef MAPK
	S->adUseMap = adMode == 0;
	/* per-substep damping factor
	dampNorm 0: the raw coefficient every substep (gamma = dampExp);
	dampNorm 1: time-normalised, the coefficient per reference frame (gamma = h/dt_ref * dampExp) */
	gam = P->dampNorm ? (dt / nSub) / DT_REF * (double)P->dampExp : (double)P->dampExp;
	{
		double dmp = (double)P->damping;
		dmp = dmp > 0 ? dmp : 0; dmp = dmp < 1 ? dmp : 1;
		S->dampF = (skirt_real)fd_pow(1 - dmp, gam);
	}
	S->coneCos = (skirt_real)fd_cos((double)P->flareCone * SKIRT_PI / 180);
	S->stats.coneDeg = (double)P->flareCone;
}

static void substep(skirt_sim_t *S, int s, int nSub, double hd, double dt, int nIt)
{
	const skirt_params_t *P = &S->params;
	const int N = S->N;
	skirt_real *x = S->x, *v = S->v, *p = S->p;
	const skirt_real *w = S->w;
	const double alpha_d = (double)s / nSub;
	const skirt_real alpha = (skirt_real)alpha_d, h = (skirt_real)hd;
	const skirt_real *To = S->Told, *Tn = S->Tnew;
	skirt_real *Ts = S->Ts;
	const skirt_real ta = P->targetLerp ? alpha : 1;
	int k, n, it;
	(void)w;
	for (k = 0; k < N * 3; k++) Ts[k] = To[k] + ta * (Tn[k] - To[k]);
	if (S->M) {
		const skirt_real *Co = S->Cold, *Cn = S->Cnew;
		skirt_real *Cs = S->Cs;
		const skirt_real a = P->colliderLerp ? alpha : 1;
		for (k = 0; k < S->M * 3; k++) Cs[k] = Co[k] + a * (Cn[k] - Co[k]);
	}
	if (S->nCaps && P->handCollision) {
		const skirt_real *Co = S->capOld, *Cn = S->capNew;
		skirt_real *Cs = S->capS, *B = S->capBox, *BB = S->capBoxes;
		const skirt_real a = P->colliderLerp ? alpha : 1;
		skirt_real rm = 0, pad;
		int c, ax;
		B[0] = B[1] = B[2] = (skirt_real)INFINITY; B[3] = B[4] = B[5] = -(skirt_real)INFINITY;
		for (k = 0; k < S->nCaps * 6; k++) {
			Cs[k] = Co[k] + a * (Cn[k] - Co[k]);
			ax = k % 3;
			if (Cs[k] < B[ax]) B[ax] = Cs[k];
			if (Cs[k] > B[ax + 3]) B[ax + 3] = Cs[k];
		}
		for (c = 0; c < S->nCaps; c++) rm = rm > S->capR[c] ? rm : S->capR[c];
		pad = rm + P->handThickness;
		for (ax = 0; ax < 3; ax++) { B[ax] -= pad; B[ax + 3] += pad; }
		for (c = 0; c < S->nCaps; c++) {
			const int o = c * 6;
			const skirt_real R = S->capR[c] + P->handThickness;
			for (ax = 0; ax < 3; ax++) {
				const skirt_real l0 = Cs[o + ax], l1 = Cs[o + 3 + ax];
				BB[o + ax] = (l0 < l1 ? l0 : l1) - R;
				BB[o + 3 + ax] = (l0 > l1 ? l0 : l1) + R;
			}
		}
	}
	/* S2 external accelerations */
	{
		const skirt_real g0 = (skirt_real)S->gL[0], g1 = (skirt_real)S->gL[1], g2 = (skirt_real)S->gL[2];
		const skirt_real wx = (skirt_real)S->wf[0], wy = (skirt_real)S->wf[1], wz = (skirt_real)S->wf[2];
		const skirt_real spread = (P->inertiaModel == 0 && P->linearKick == 1) ? (skirt_real)(1 / dt) : 0;
		const skirt_real kv0 = (skirt_real)S->kickV[0], kv1 = (skirt_real)S->kickV[1], kv2 = (skirt_real)S->kickV[2];
		const skirt_real ko0 = (skirt_real)S->kickO[0], ko1 = (skirt_real)S->kickO[1], ko2 = (skirt_real)S->kickO[2];
		for (n = 0; n < S->nDyn; n++) {
			const int i = (int)S->dynList[n] * 3;
			const skirt_real rx = x[i], ry = x[i + 1], rz = x[i + 2];
			const skirt_real vx = v[i], vy = v[i + 1], vz = v[i + 2];
			skirt_real ax = g0, ay = g1, az = g2;
			/* centrifugal -w x (w x r) */
			const skirt_real cx = wy * rz - wz * ry, cy = wz * rx - wx * rz, cz = wx * ry - wy * rx;
			ax -= wy * cz - wz * cy; ay -= wz * cx - wx * cz; az -= wx * cy - wy * cx;
			/* Coriolis -2 w x v */
			ax -= 2 * (wy * vz - wz * vy); ay -= 2 * (wz * vx - wx * vz); az -= 2 * (wx * vy - wy * vx);
			if (spread != 0) {
				ax += spread * (kv0 + (ko1 * rz - ko2 * ry));
				ay += spread * (kv1 + (ko2 * rx - ko0 * rz));
				az += spread * (kv2 + (ko0 * ry - ko1 * rx));
			}
			v[i] += h * ax; v[i + 1] += h * ay; v[i + 2] += h * az;
		}
	}
	if ((P->dragScale > 0 || P->liftScale > 0) && P->fluidDensity > 0) aero(S, h);
	/* S3 damping */
	if (P->damping > 0) {
		const skirt_real f = S->dampF;
		if (P->dampRelative) {
			const skirt_real inv = (skirt_real)(1 / dt);
			for (n = 0; n < S->nDyn; n++) {
				const int i = (int)S->dynList[n] * 3;
				for (k = 0; k < 3; k++) { const skirt_real vt = (Tn[i + k] - To[i + k]) * inv; v[i + k] = vt + (v[i + k] - vt) * f; }
			}
		} else {
			for (n = 0; n < S->nDyn; n++) { const int i = (int)S->dynList[n] * 3; v[i] *= f; v[i + 1] *= f; v[i + 2] *= f; }
		}
	}
	if (P->localDamping > 0) localDamping(S, P->localDamping);
	/* S4 predict */
	for (k = 0; k < N; k++) {
		const int j = k * 3;
		if (S->kin[k]) { p[j] = Ts[j]; p[j + 1] = Ts[j + 1]; p[j + 2] = Ts[j + 2]; x[j] = p[j]; x[j + 1] = p[j + 1]; x[j + 2] = p[j + 2]; }
		else { p[j] = x[j] + h * v[j]; p[j + 1] = x[j + 1] + h * v[j + 1]; p[j + 2] = x[j + 2] + h * v[j + 2]; }
	}
	for (k = 0; k < N; k++) S->push[k] = 0;
	/* S5 contact search */
	if (S->M) contactSearch(S);
	if (S->M && P->coneBody && P->flareCone > 0) {
		/* coneBody: the allowance the body gave eases back to the cap */
		const skirt_real kr = P->coneRelax > 0 ? (skirt_real)(1 - exp(-hd / (double)P->coneRelax)) : 1;
		skirt_real *cc = S->coneC;
		const uint32_t *ts = S->tetStart;
		const skirt_real *tl = S->tetLen;
		for (n = 0; n < S->nDyn; n++) { const int i = (int)S->dynList[n]; cc[i] += (1 - cc[i]) * kr; }
		/* ... and passes down the sheet: cloth hanging below a held particle (further along its tether) shares it */
		for (k = 0; k < S->NE; k++) {
			const int a = (int)S->e0[k], b = (int)S->e1[k];
			int lo, hi;
			if (ts[a] == ts[a + 1] || ts[b] == ts[b + 1]) continue;
			if (tl[ts[a]] < tl[ts[b]]) { lo = a; hi = b; } else { lo = b; hi = a; }
			if (cc[lo] < cc[hi]) cc[hi] = cc[lo];
		}
	}
	/* S6 iterations */
	{
		const int colLast = P->order == 0, cone = P->flareCone > 0;
		if (P->adPerSubstep && S->kAD > 0) animDrive(S);
		for (it = 0; it < nIt; it++) {
			const int col = (S->M || (S->nCaps && P->handCollision)) && (P->collideEvery != 0 || it == nIt - 1);
			int r;
			if (P->order == 2) { if (!P->adPerSubstep && S->kAD > 0) animDrive(S); tethers(S); maxDist(S); if (cone) coneCap(S); if (col) collide(S, it); }
			if (P->order == 3 && !P->adPerSubstep && S->kAD > 0) animDrive(S);
			if (P->order == 2 || P->order == 3) { edges(S); if (S->kBend > 0) bends(S); if (S->kArea > 0) areas(S); }
			if (P->order == 3) { tethers(S); maxDist(S); if (cone) coneCap(S); if (col) collide(S, it); }
			if (P->order >= 2) continue;
			for (r = 0; r < P->edgePasses; r++) edges(S);
			if (S->kBend > 0) for (r = 0; r < P->bendPasses; r++) bends(S);
			if (S->kArea > 0) areas(S);
			if (!P->adPerSubstep && S->kAD > 0) animDrive(S);
			if (colLast) { tethers(S); maxDist(S); if (cone) coneCap(S); if (col) collide(S, it); }
			else { if (cone) coneCap(S); if (col) collide(S, it); maxDist(S); tethers(S); }
		}
	}
	/* S7 velocity update, friction, drive damping; S8 */
	{
		const skirt_real ih = 1 / h;
		for (n = 0; n < S->nDyn; n++) {
			const int i = (int)S->dynList[n] * 3;
			v[i] = (p[i] - x[i]) * ih; v[i + 1] = (p[i + 1] - x[i + 1]) * ih; v[i + 2] = (p[i + 2] - x[i + 2]) * ih;
		}
	}
	if (S->M && P->friction > 0) friction(S, h, dt);
	if (S->dAD > 0) {
		const skirt_real d = S->dAD, inv = (skirt_real)(1 / dt);
		const int useMap = S->adUseMap;
		for (n = 0; n < S->nDyn; n++) {
			const int i = (int)S->dynList[n], j = i * 3;
			const skirt_real di = useMap ? d * S->adMap[i] : d;
			for (k = 0; k < 3; k++) { const skirt_real vt = (Tn[j + k] - To[j + k]) * inv; v[j + k] += di * (vt - v[j + k]); }
		}
	}
	for (n = 0; n < S->nDyn; n++) {
		const int i = (int)S->dynList[n] * 3;
		x[i] = p[i]; x[i + 1] = p[i + 1]; x[i + 2] = p[i + 2];
	}
}

void SkirtSim_Reset(skirt_sim_t *S, const float *mats)
{
	int i, r;
	frameFrom(S, mats, S->F);
	memcpy(S->Fold, S->F, sizeof(S->F));
	skinAll(S, mats);
	memcpy(S->Told, S->Tnew, sizeof(skirt_real) * (size_t)S->N * 3);
	if (S->M) memcpy(S->Cold, S->Cnew, sizeof(skirt_real) * (size_t)S->M * 3);
	if (S->nCaps) { memcpy(S->capOld, S->capNew, sizeof(skirt_real) * (size_t)S->nCaps * 6); memcpy(S->capS, S->capNew, sizeof(skirt_real) * (size_t)S->nCaps * 6); }
	memcpy(S->x, S->Tnew, sizeof(skirt_real) * (size_t)S->N * 3);
	memcpy(S->p, S->Tnew, sizeof(skirt_real) * (size_t)S->N * 3);
	for (i = 0; i < S->N * 3; i++) S->v[i] = 0;
	for (i = 0; i < 3; i++) { S->VsOld[i] = 0; S->OsOld[i] = 0; S->OcOld[i] = 0; }
	for (i = 0; i < S->N; i++) S->cTri[i] = -1;
	if (S->M) {
		for (i = 0; i < S->N; i++) { S->sTri[i] = -1; S->coneC[i] = 1; }
		seedTracking(S);
		/* The skinned start pose can sit inside the legs: resolve it once before the first step. */
		if (S->params.resetProject) {
			memcpy(S->Cs, S->Cnew, sizeof(skirt_real) * (size_t)S->M * 3);
			contactSearch(S);
			for (r = 0; r < 4; r++) collide(S, -1);
			memcpy(S->x, S->p, sizeof(skirt_real) * (size_t)S->N * 3);
			for (i = 0; i < S->N; i++) S->cTri[i] = -1;
		}
	}
	S->ready = 1;
	S->stats.nan = 0;
	S->poseDt = 0;
}

void SkirtSim_Step(skirt_sim_t *S, skirt_real dtIn, const float *mats)
{
	const skirt_params_t *P = &S->params;
	const double dt = (double)dtIn;
	int nSub, nIt, s, i, bad;
	double h;
	if (!S->ready) { SkirtSim_Reset(S, mats); return; }
	if (!(dt > 0)) return;
	/* F1, F2 */
	memcpy(S->Fold, S->F, sizeof(S->F));
	frameFrom(S, mats, S->F);
	frameKinematics(S, dt);
	/* The previous targets/collider are in the previous frame's coordinates, which under
	full carry are the current ones; model B then moves them, and the particle state,
	by the felt part of the frame motion (F3). */
	memcpy(S->Told, S->Tnew, sizeof(skirt_real) * (size_t)S->N * 3);
	if (S->M) memcpy(S->Cold, S->Cnew, sizeof(skirt_real) * (size_t)S->M * 3);
	if (S->nCaps) memcpy(S->capOld, S->capNew, sizeof(skirt_real) * (size_t)S->nCaps * 6);
	if (P->inertiaModel == 1) {
		xform(S, S->x, S->N, 1); if (P->rotateVel) xform(S, S->v, S->N, 0);
		if (S->carry) {
			/* the linear kick beyond maxFeltAcc moves with the body */
			const skirt_real e0 = (skirt_real)S->carryV[0], e1 = (skirt_real)S->carryV[1], e2 = (skirt_real)S->carryV[2];
			skirt_real *v = S->v;
			int n;
			for (n = 0; n < S->nDyn; n++) {
				const int j = (int)S->dynList[n] * 3;
				v[j] += e0; v[j + 1] += e1; v[j + 2] += e2;
			}
		}
		xform(S, S->Told, S->N, 1);
		if (S->M) xform(S, S->Cold, S->M, 1);
		if (S->nCaps) xform(S, S->capOld, S->nCaps * 2, 1);
	} else if (P->linearKick == 0) {
		/* model A, impulse: frame acceleration as one velocity change per frame */
		const skirt_real kv0 = (skirt_real)S->kickV[0], kv1 = (skirt_real)S->kickV[1], kv2 = (skirt_real)S->kickV[2];
		const skirt_real ko0 = (skirt_real)S->kickO[0], ko1 = (skirt_real)S->kickO[1], ko2 = (skirt_real)S->kickO[2];
		skirt_real *x = S->x, *v = S->v;
		int n;
		for (n = 0; n < S->nDyn; n++) {
			const int j = (int)S->dynList[n] * 3;
			const skirt_real rx = x[j], ry = x[j + 1], rz = x[j + 2];
			v[j] += kv0 + (ko1 * rz - ko2 * ry);
			v[j + 1] += kv1 + (ko2 * rx - ko0 * rz);
			v[j + 2] += kv2 + (ko0 * ry - ko1 * rx);
		}
	}
	skinAll(S, mats);

	{
		const double sb = floor((double)P->substeps + 0.5);
		nSub = sb > 1 ? (int)sb : 1;
	}
	{
		const int a = iround((double)P->maxIterations), b = iround((double)P->iterations * dt / DT_REF);
		nIt = a < b ? a : b;
		if (nIt < 1) nIt = 1;
	}
	h = dt / nSub;
	setupStiffness(S, dt, nSub, nIt);
	S->nIt = nIt;
	S->stats.contacts = 0; S->stats.handContacts = 0; S->stats.coneCapped = 0;
	for (s = 1; s <= nSub; s++) substep(S, s, nSub, h, dt, nIt);
	bad = 0;
	/* every coordinate (one pass over data in cache; one float in 97 missed small sheets and lone patches),
	   and the next step resets, as the header says */
	for (i = 0; i < S->N * 3; i++) if (!isfinite(S->x[i])) { bad = 1; break; }
	if (bad) S->ready = 0;
	S->stats.nan = bad;
	S->stats.steps++;
}

void SkirtSim_Read(const skirt_sim_t *S, float *out)
{
	const double *F = S->F;
	const skirt_real f0 = (skirt_real)F[0], f1 = (skirt_real)F[1], f2 = (skirt_real)F[2], f3 = (skirt_real)F[3], f4 = (skirt_real)F[4], f5 = (skirt_real)F[5];
	const skirt_real f6 = (skirt_real)F[6], f7 = (skirt_real)F[7], f8 = (skirt_real)F[8], f9 = (skirt_real)F[9], f10 = (skirt_real)F[10], f11 = (skirt_real)F[11];
	const skirt_real *x = S->x;
	int i;
	for (i = 0; i < S->N; i++) {
		const int j = i * 3;
		const skirt_real a = x[j], b = x[j + 1], c = x[j + 2];
		out[j] = (float)(f0 * a + f3 * b + f6 * c + f9);
		out[j + 1] = (float)(f1 * a + f4 * b + f7 * c + f10);
		out[j + 2] = (float)(f2 * a + f5 * b + f8 * c + f11);
	}
}

/* ---------------------------------------------------------------- split stepping (skirt_sim.h) */

static void frameOut(const skirt_sim_t *S, skirt_frame_t *o)
{
	int k;
	for (k = 0; k < 12; k++) o->F[k] = (float)S->F[k];
	{
		const double *ax = S->params.localSpace ? S->coneAxisL : S->coneAxisW;
		o->coneAxis[0] = (float)ax[0]; o->coneAxis[1] = (float)ax[1]; o->coneAxis[2] = (float)ax[2];
	}
}

/* skinAll's capsule part: capsule ends from mats into the sim frame (capNew) */
static void capsFrame(skirt_sim_t *S, const float *mats)
{
	const double *F = S->F;
	const skirt_real f0 = (skirt_real)F[0], f1 = (skirt_real)F[1], f2 = (skirt_real)F[2], f3 = (skirt_real)F[3], f4 = (skirt_real)F[4], f5 = (skirt_real)F[5];
	const skirt_real f6 = (skirt_real)F[6], f7 = (skirt_real)F[7], f8 = (skirt_real)F[8], f9 = (skirt_real)F[9], f10 = (skirt_real)F[10], f11 = (skirt_real)F[11];
	skirt_real *C = S->capC, *O = S->capNew;
	int k;
	capsulePoints(S, mats, C);
	for (k = 0; k < S->nCaps * 6; k += 3) {
		const skirt_real x = C[k] - f9, y = C[k + 1] - f10, z = C[k + 2] - f11;
		O[k] = f0 * x + f1 * y + f2 * z; O[k + 1] = f3 * x + f4 * y + f5 * z; O[k + 2] = f6 * x + f7 * y + f8 * z;
	}
}

void SkirtSim_Invalidate(skirt_sim_t *S) { S->ready = 0; }

void SkirtSim_SetPoseDt(skirt_sim_t *S, double poseDt) { S->poseDt = poseDt > 0 ? poseDt : 0; }

/* Gram-Schmidt on rows 0..2 of a row-vector matrix (sign of row 2 kept), as the renderer glue's */
static void poseOrtho(float *m)
{
	float x[3], y[3], z[3], l, d;
	int k;
	l = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	for (k = 0; k < 3; k++) x[k] = l > 0 ? m[k] / l : 0;
	d = x[0] * m[4] + x[1] * m[5] + x[2] * m[6];
	for (k = 0; k < 3; k++) y[k] = m[4 + k] - d * x[k];
	l = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	for (k = 0; k < 3; k++) y[k] = l > 0 ? y[k] / l : 0;
	z[0] = x[1] * y[2] - x[2] * y[1]; z[1] = x[2] * y[0] - x[0] * y[2]; z[2] = x[0] * y[1] - x[1] * y[0];
	if (z[0] * m[8] + z[1] * m[9] + z[2] * m[10] < 0) for (k = 0; k < 3; k++) z[k] = -z[k];
	for (k = 0; k < 3; k++) { m[k] = x[k]; m[4 + k] = y[k]; m[8 + k] = z[k]; }
}

/* m (row-vector, rows 0..2 the axes, 12..14 the origin) in the frame F (orthonormal): m * F^-1 */
static void poseInto(float *o, const float *m, const float *F)
{
	int r, c;
	for (r = 0; r < 3; r++) for (c = 0; c < 3; c++) o[r * 4 + c] = m[r * 4] * F[c * 4] + m[r * 4 + 1] * F[c * 4 + 1] + m[r * 4 + 2] * F[c * 4 + 2];
	for (c = 0; c < 3; c++) o[12 + c] = (m[12] - F[12]) * F[c * 4] + (m[13] - F[13]) * F[c * 4 + 1] + (m[14] - F[14]) * F[c * 4 + 2];
}

void SkirtPose_Lerp(float *out, const float *a, const float *b, float t, int nBones, int refBone)
{
	float Fa[16], Fb[16], F[16], ra[16], rb[16], r[16];
	int bn, k, c;
	memcpy(Fa, a + refBone * 16, sizeof Fa); poseOrtho(Fa);
	memcpy(Fb, b + refBone * 16, sizeof Fb); poseOrtho(Fb);
	for (k = 0; k < 16; k++) F[k] = Fa[k] + t * (Fb[k] - Fa[k]);
	poseOrtho(F);
	for (bn = 0; bn < nBones; bn++) {
		float *o = out + bn * 16;
		poseInto(ra, a + bn * 16, Fa);
		poseInto(rb, b + bn * 16, Fb);
		for (k = 0; k < 15; k++) r[k] = ra[k] + t * (rb[k] - ra[k]);
		poseOrtho(r);
		for (k = 0; k < 3; k++) {
			for (c = 0; c < 3; c++) o[k * 4 + c] = r[k * 4] * F[c] + r[k * 4 + 1] * F[4 + c] + r[k * 4 + 2] * F[8 + c];
			o[k * 4 + 3] = 0.0f;
		}
		for (c = 0; c < 3; c++) o[12 + c] = r[12] * F[c] + r[13] * F[4 + c] + r[14] * F[8 + c] + F[12 + c];
		o[15] = 1.0f;
	}
}

/* Diagnostic (tools and r_simCheck only): per dynamic particle, the nearest collider vertex of the last step's pose
(brute force, as seedTracking) and the triangles around it decide. The particle is inside when it lies behind the
outward face of every one of them (so the open ends of a collider and its far side do not count), and counted when
it is more than depth and less than SKIRT_INSIDE_MAX from the closest of them. stride > 1 looks at every stride-th
dynamic particle only (the count is of those). */
#define SKIRT_INSIDE_MAX 12.0   /* cm: deeper than a limb is thick is the far field, not a particle in the body */
int SkirtSim_CountInside(const skirt_sim_t *S, double depth, int stride, double *maxDepth)
{
	const skirt_real *x = S->x, *C = S->Cnew;
	const uint32_t *T = S->cTriIdx, *vts = S->vtStart, *vtl = S->vtList;
	const skirt_real flip = S->params.flipNormals ? K(-1.0) : K(1.0);
	const int M = S->M;
	int n, v, count = 0;
	double deepest = 0;
	if (maxDepth) *maxDepth = 0;
	if (!M || !S->ready) return 0;
	if (stride < 1) stride = 1;
	for (n = 0; n < S->nDyn; n += stride) {
		const int j = (int)S->dynList[n] * 3;
		const skirt_real px = x[j], py = x[j + 1], pz = x[j + 2];
		int bestV = -1;
		skirt_real bd = (skirt_real)INFINITY;
		double best = 1e30;
		int behindAll = 1;
		uint32_t q;
		for (v = 0; v < M; v++) {
			const int k = v * 3;
			const skirt_real dx = C[k] - px, dy = C[k + 1] - py, dz = C[k + 2] - pz;
			const skirt_real dd = dx * dx + dy * dy + dz * dz;
			if (dd < bd) { bd = dd; bestV = v; }
		}
		if (bestV < 0) continue;
		for (q = vts[bestV]; q < vts[bestV + 1]; q++) {
			const int t = (int)vtl[q];
			const int a = (int)T[t * 3] * 3, b = (int)T[t * 3 + 1] * 3, c = (int)T[t * 3 + 2] * 3;
			skirt_real bary[3], qx, qy, qz, nx, ny, nz, nl;
			double d2;
			closestBary(px, py, pz, C[a], C[a + 1], C[a + 2], C[b], C[b + 1], C[b + 2], C[c], C[c + 1], C[c + 2], bary);
			qx = px - (bary[0] * C[a] + bary[1] * C[b] + bary[2] * C[c]);
			qy = py - (bary[0] * C[a + 1] + bary[1] * C[b + 1] + bary[2] * C[c + 1]);
			qz = pz - (bary[0] * C[a + 2] + bary[1] * C[b + 2] + bary[2] * C[c + 2]);
			d2 = (double)qx * qx + (double)qy * qy + (double)qz * qz;
			{	/* outward face normal, as signedDist; the plane's side, from vertex a */
				const skirt_real ux = C[b] - C[a], uy = C[b + 1] - C[a + 1], uz = C[b + 2] - C[a + 2];
				const skirt_real vx = C[c] - C[a], vy = C[c + 1] - C[a + 1], vz = C[c + 2] - C[a + 2];
				nx = vy * uz - vz * uy; ny = vz * ux - vx * uz; nz = vx * uy - vy * ux;
				nl = SQRT(nx * nx + ny * ny + nz * nz);
				if (nl < EPS) continue;
				if (flip * ((px - C[a]) * nx + (py - C[a + 1]) * ny + (pz - C[a + 2]) * nz) >= 0) { behindAll = 0; break; }
			}
			if (d2 < best) best = d2;
		}
		if (behindAll && best < 1e30) {
			const double in = sqrt(best);   /* how far behind the surface */
			if (in < SKIRT_INSIDE_MAX) {
				if (in > depth) count++;
				if (in > deepest) deepest = in;
			}
		}
	}
	if (maxDepth) *maxDepth = deepest;
	return count;
}

void SkirtSim_GetFrame(const skirt_sim_t *S, double F[12]) { memcpy(F, S->F, sizeof(S->F)); }

/* Mirrors the frame part of SkirtSim_Reset and SkirtSim_Step: keep the three in step. */
int SkirtSim_StepFrame(skirt_sim_t *S, skirt_real dtIn, const float *mats, skirt_frame_t *o, float *capOld, float *capNew)
{
	const skirt_params_t *P = &S->params;
	const double dt = (double)dtIn;
	const int nc6 = S->nCaps * 6;
	int k;
	memset(o, 0, sizeof(*o));
	if (!S->ready) {
		/* SkirtSim_Reset */
		frameFrom(S, mats, S->F);
		memcpy(S->Fold, S->F, sizeof(S->F));
		if (S->nCaps) {
			capsFrame(S, mats);
			memcpy(S->capOld, S->capNew, sizeof(skirt_real) * (size_t)nc6);
			memcpy(S->capS, S->capNew, sizeof(skirt_real) * (size_t)nc6);
		}
		for (k = 0; k < 3; k++) { S->VsOld[k] = 0; S->OsOld[k] = 0; S->OcOld[k] = 0; }
		S->ready = 1;
		S->stats.nan = 0;
		S->poseDt = 0;
		o->reset = 1;
		o->nSub = 1;
		o->nIt = S->nIt;
		frameOut(S, o);
		for (k = 0; k < nc6; k++) { if (capOld) capOld[k] = (float)S->capOld[k]; if (capNew) capNew[k] = (float)S->capNew[k]; }
		return 1;
	}
	if (!(dt > 0)) return 0;
	/* SkirtSim_Step: F1, F2 */
	memcpy(S->Fold, S->F, sizeof(S->F));
	frameFrom(S, mats, S->F);
	frameKinematics(S, dt);
	if (S->nCaps) memcpy(S->capOld, S->capNew, sizeof(skirt_real) * (size_t)nc6);
	for (k = 0; k < 9; k++) o->D[k] = k % 4 == 0 ? 1.0f : 0.0f;
	if (P->inertiaModel == 1) {
		for (k = 0; k < 9; k++) o->D[k] = (float)S->D[k];
		for (k = 0; k < 3; k++) { o->dvec[k] = (float)S->dvec[k]; o->carryV[k] = (float)S->carryV[k]; }
		o->carry = S->carry;
		if (S->nCaps) xform(S, S->capOld, S->nCaps * 2, 1);
	} else if (P->linearKick == 0) {
		o->kickImpulse = 1;
	}
	for (k = 0; k < 3; k++) { o->kickV[k] = (float)S->kickV[k]; o->kickO[k] = (float)S->kickO[k]; }
	o->spread = (P->inertiaModel == 0 && P->linearKick == 1) ? (float)(1 / dt) : 0.0f;
	if (S->nCaps) capsFrame(S, mats);
	{
		const double sb = floor((double)P->substeps + 0.5);
		o->nSub = sb > 1 ? (int)sb : 1;
	}
	{
		const int a = iround((double)P->maxIterations), b = iround((double)P->iterations * dt / DT_REF);
		o->nIt = a < b ? a : b;
		if (o->nIt < 1) o->nIt = 1;
	}
	o->dt = (float)dt;
	o->h = (float)(dt / o->nSub);
	setupStiffness(S, dt, o->nSub, o->nIt);
	S->nIt = o->nIt;
	S->stats.contacts = 0; S->stats.handContacts = 0; S->stats.coneCapped = 0;
	frameOut(S, o);
	for (k = 0; k < 3; k++) {
		o->gL[k] = (float)S->gL[k]; o->wf[k] = (float)S->wf[k];
		o->aeroLin[k] = (float)S->aeroLin[k]; o->VL[k] = (float)S->VL[k]; o->OL[k] = (float)S->OL[k];
	}
	o->aeroAng = (float)S->aeroAng[0];
	o->kEdge = (float)S->kEdge; o->kBend = (float)S->kBend; o->kArea = (float)S->kArea; o->kTether = (float)S->kTether;
	o->kAD = (float)S->kAD; o->dAD = (float)S->dAD; o->dampF = (float)S->dampF; o->adUseMap = S->adUseMap;
	o->coneCos = (float)S->coneCos;
	for (k = 0; k < nc6; k++) { if (capOld) capOld[k] = (float)S->capOld[k]; if (capNew) capNew[k] = (float)S->capNew[k]; }
	S->stats.steps++;
	return 1;
}

void SkirtSim_Topology(const skirt_sim_t *S, skirt_topology_t *o)
{
	memset(o, 0, sizeof(*o));
	o->N = S->N; o->NT = S->NT; o->NE = S->NE; o->NB = S->NB; o->NTet = S->NTet; o->M = S->M; o->cNT = S->M ? S->cNT : 0;
	o->nDyn = S->nDyn; o->nCaps = S->nCaps; o->nBones = S->nBones;
	o->buildSerial = S->buildSerial;
	o->X0 = S->X0; o->maxDist = S->maxDist; o->adMap = S->adMap; o->skinW = S->skin.w;
	o->tri = S->tri; o->e0 = S->e0; o->e1 = S->e1; o->bq = S->bq; o->tetStart = S->tetStart; o->tetAnchor = S->tetAnchor;
	o->dynList = S->dynList; o->skinStart = S->skin.start; o->skinBone = S->skin.bone;
	o->eLen = S->eLen; o->bPhi0 = S->bPhi0; o->bLen0 = S->bLen0; o->A0 = S->A0; o->tetLen = S->tetLen;
	o->w = S->w; o->mass = S->mass; o->kin = S->kin;
	if (S->M) {
		o->cPos0 = S->cPos0; o->cSkinW = S->cSkin.w; o->cSkinStart = S->cSkin.start; o->cSkinBone = S->cSkin.bone;
		o->cTri = S->cTriIdx; o->vtStart = S->vtStart; o->vtList = S->vtList; o->vvStart = S->vvStart; o->vvList = S->vvList;
		o->vRad = S->vRad; o->cMaxEdge = S->cMaxEdge;
	}
	o->capR = S->nCaps ? S->capR : NULL;
}
