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

// tr_g2blend.h -- xenjk: draw-only Ghoul2 bone blending in rotation space (r_g2Blend), shared by rd-vanilla and
// rd-vulkan. Stock Ghoul2 lerps the 3x4 bone matrices component by component, both between keyframes and between
// animations; the rotation shrinks mid-blend (squash, candy-wrapping). Here a bone pose is a unit quaternion plus
// the displacement of the bone's joint (its base-pose pivot); poses blend by slerp of the rotation and lerp of the
// joint displacement, so a joint rotates about its pivot and the matrix stays orthonormal. Only the renderer's own
// draw traversals use it; bolts, collision and every server-side query keep the stock lerp. See docs/g2-anim-smoothing.md.

#pragma once

typedef struct g2bPose_s {
	float q[4];		// w x y z, unit length
	float d[3];		// joint displacement: R*p + t - p for the bone's pivot p
} g2bPose_t;

// curve kinds for the animation-to-animation blend weight
#define G2B_CURVE_LINEAR		0
#define G2B_CURVE_EASEOUT		1
#define G2B_CURVE_SMOOTHSTEP	2

// decode one compressed quaternion bone (mdxaCompQuatBone_t::Comp) to a pose about pivot p (model space)
void G2B_DecodeBone( const unsigned char *comp, const float pivot[3], g2bPose_t *out );
// out = a*(1-w) + b*w in rotation space (shortest arc); out may alias a or b
void G2B_Blend( const g2bPose_t *a, const g2bPose_t *b, float w, g2bPose_t *out );
// the bone's local 3x4 matrix (rotation about the pivot plus the joint displacement)
void G2B_PoseToMatrix( const g2bPose_t *pose, const float pivot[3], float mat[3][4] );
// eased blend weight; x is the stock linear weight of the new animation in [0,1]
float G2B_Curve( float x, int kind );
// largest |row length - scale| / scale and |row_i . row_j| / scale^2 of a 3x4 matrix's rotation part
float G2B_OrthoError( const float mat[3][4], float scale );
