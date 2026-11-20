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

// tr_g2joints.h -- xenjk: Ghoul2 models bent about their own joints (r_g2Joints).
//
// A GLM carries no skeleton: every vertex is skinned with animMatrix * inverse(basePoseMat) from the GLA, so a model
// converted with its own proportions is bent about the GLA's joints (Kyle's), not its own, and every bend shears the
// limb sideways (an elbow 4 units from its pivot moves ~2 units at a 30 degree bend). A model may ship
// "<model>.joints" beside its GLM: each bone's own joint (bind pose, model space) and the bone of its own skeleton
// that carries that joint. A bone then keeps the animation's rotation but turns about its own joint, and the joint
// rides its own parent:
//   S'(j) = S'(anchor)(j) + [S(k) - S(anchor)(k)]
// with k the GLA pivot, j the model's joint and S the stock bone matrix. Bones the file omits inherit the offset of
// their nearest listed GLA ancestor. Client side only; other renderers and clients never open the file.

#pragma once

#define G2J_VERSION		1

typedef struct g2Joints_s {
	int		numBones;		// the GLA's bone count
	int		numListed;		// bones the file named that the GLA has
	float	(*delta)[3];	// per GLA bone: own joint - GLA pivot (inherited down the GLA for unlisted bones)
	int		*anchor;		// per GLA bone: the bone whose transform carries the joint; -1 = the root matrix
} g2Joints_t;

// Parse a .joints file against a skeleton. names/parents/pivots describe the GLA (pivots = the base-pose joint,
// basePoseMat's translation). Fills delta[numBones][3] and anchor[numBones]. Returns the number of bones set from
// the file (0 = nothing usable), or -1 if the text is not a version-1 joints file; warn receives the first problem.
int G2J_Parse( const char *text, int numBones, const char *const *names, const int *parents, const float (*pivots)[3],
	float (*delta)[3], int *anchor, char *warn, int warnSize );

// One bone's translation correction, applied after the stock transform, in place.
//   child:  the bone's matrix as stock built it from its (already corrected) GLA parent
//   anchor: the anchor bone's corrected matrix (the root matrix when the anchor is -1)
//   parentShift/anchorShift: the translation those corrections added to the GLA parent and the anchor
// Writes the child's own added translation to shiftOut.
void G2J_Correct( float child[3][4], const float anchor[3][4], const float parentShift[3], const float anchorShift[3],
	const float delta[3], float shiftOut[3] );
