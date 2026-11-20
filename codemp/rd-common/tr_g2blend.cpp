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

// tr_g2blend.cpp -- xenjk: draw-only Ghoul2 bone blending in rotation space (r_g2Blend); see tr_g2blend.h.
//
// A Ghoul2 bone's local matrix L (one per frame in the GLA, a compressed quaternion plus translation) is applied as
// Final_child = Final_parent * L, and vertices are skinned with Final; L fixes the bone's joint p (the translation of
// its BasePoseMat, in model space) unless the joint itself moves. Writing L = [R | t], the joint displacement is
// d = R*p + t - p (zero for a pure rotation about the joint). The stock component lerp of two matrices keeps the
// joint but shrinks R; slerp of R with a lerp of t keeps R a rotation but swings the joint along a chord. Slerp of R
// with a lerp of d keeps both: t = p + d - R*p.

#include "tr_g2blend.h"

#include <math.h>

static void G2B_QuatToRot( const float q[4], float m[3][4] )
{
	const float w = q[0], x = q[1], y = q[2], z = q[3];
	const float tx = 2.0f * x, ty = 2.0f * y, tz = 2.0f * z;
	const float twx = tx * w, twy = ty * w, twz = tz * w;
	const float txx = tx * x, txy = ty * x, txz = tz * x;
	const float tyy = ty * y, tyz = tz * y, tzz = tz * z;

	// the same expansion as MC_UnCompressQuat
	m[0][0] = 1.0f - ( tyy + tzz );
	m[0][1] = txy - twz;
	m[0][2] = txz + twy;
	m[1][0] = txy + twz;
	m[1][1] = 1.0f - ( txx + tzz );
	m[1][2] = tyz - twx;
	m[2][0] = txz - twy;
	m[2][1] = tyz + twx;
	m[2][2] = 1.0f - ( txx + tyy );
}

static void G2B_Normalize4( float q[4] )
{
	const float l2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	if ( l2 > 1e-12f ) {
		const float inv = 1.0f / sqrtf( l2 );
		q[0] *= inv; q[1] *= inv; q[2] *= inv; q[3] *= inv;
	} else {
		q[0] = 1.0f; q[1] = q[2] = q[3] = 0.0f;
	}
}

void G2B_DecodeBone( const unsigned char *comp, const float pivot[3], g2bPose_t *out )
{
	const unsigned short *in = (const unsigned short *)comp;
	float q[4], t[3], m[3][4];
	int i;

	// the same decoding as MC_UnCompressQuat; the rotation it builds from the raw (not quite unit) quaternion
	// locates the joint exactly where the stock matrix puts it
	for ( i = 0; i < 4; i++ ) {
		q[i] = in[i];
		q[i] /= 16383.0f;
		q[i] -= 2.0f;
	}
	for ( i = 0; i < 3; i++ ) {
		t[i] = in[4 + i];
		t[i] /= 64;
		t[i] -= 512;
	}
	G2B_QuatToRot( q, m );
	for ( i = 0; i < 3; i++ ) {
		out->d[i] = m[i][0] * pivot[0] + m[i][1] * pivot[1] + m[i][2] * pivot[2] + t[i] - pivot[i];
	}
	G2B_Normalize4( q );
	out->q[0] = q[0]; out->q[1] = q[1]; out->q[2] = q[2]; out->q[3] = q[3];
}

void G2B_Blend( const g2bPose_t *a, const g2bPose_t *b, float w, g2bPose_t *out )
{
	float bq[4], wa, wb, dot;
	int i;

	dot = a->q[0] * b->q[0] + a->q[1] * b->q[1] + a->q[2] * b->q[2] + a->q[3] * b->q[3];
	for ( i = 0; i < 4; i++ ) {
		bq[i] = b->q[i];
	}
	if ( dot < 0.0f ) {
		// q and -q are the same rotation; take the short way round
		dot = -dot;
		for ( i = 0; i < 4; i++ ) {
			bq[i] = -bq[i];
		}
	}
	if ( dot > 0.9995f ) {
		// under ~3.6 degrees apart: nlerp is the same curve to float precision
		wa = 1.0f - w;
		wb = w;
	} else {
		const float theta = acosf( dot );
		const float inv = 1.0f / sinf( theta );
		wa = sinf( ( 1.0f - w ) * theta ) * inv;
		wb = sinf( w * theta ) * inv;
	}
	for ( i = 0; i < 4; i++ ) {
		out->q[i] = wa * a->q[i] + wb * bq[i];
	}
	G2B_Normalize4( out->q );
	for ( i = 0; i < 3; i++ ) {
		out->d[i] = a->d[i] + ( b->d[i] - a->d[i] ) * w;
	}
}

void G2B_PoseToMatrix( const g2bPose_t *pose, const float pivot[3], float mat[3][4] )
{
	int i;

	G2B_QuatToRot( pose->q, mat );
	for ( i = 0; i < 3; i++ ) {
		mat[i][3] = pivot[i] + pose->d[i] - ( mat[i][0] * pivot[0] + mat[i][1] * pivot[1] + mat[i][2] * pivot[2] );
	}
}

float G2B_Curve( float x, int kind )
{
	if ( x <= 0.0f ) {
		return 0.0f;
	}
	if ( x >= 1.0f ) {
		return 1.0f;
	}
	switch ( kind ) {
	case G2B_CURVE_EASEOUT:
		// full speed at the start (twice the linear rate), settles with zero speed at the end
		return 1.0f - ( 1.0f - x ) * ( 1.0f - x );
	case G2B_CURVE_SMOOTHSTEP:
		return x * x * ( 3.0f - 2.0f * x );
	default:
		return x;
	}
}

float G2B_OrthoError( const float mat[3][4], float scale )
{
	float err = 0.0f;
	int i, j;

	if ( scale <= 0.0f ) {
		return 0.0f;
	}
	for ( i = 0; i < 3; i++ ) {
		const float len = sqrtf( mat[i][0] * mat[i][0] + mat[i][1] * mat[i][1] + mat[i][2] * mat[i][2] );
		const float e = fabsf( len - scale ) / scale;
		if ( e > err ) {
			err = e;
		}
		for ( j = i + 1; j < 3; j++ ) {
			const float o = fabsf( mat[i][0] * mat[j][0] + mat[i][1] * mat[j][1] + mat[i][2] * mat[j][2] ) / ( scale * scale );
			if ( o > err ) {
				err = o;
			}
		}
	}
	return err;
}
