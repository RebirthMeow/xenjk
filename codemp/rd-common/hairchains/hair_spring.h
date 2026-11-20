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
/* hair_spring.h -- spring bones: body jiggle (breasts, hips, belly, twist and accessory helpers).
 *
 * Each spring moves one bone. Every fixed step dt (the asset's spring step, 1/120 s) the animation puts the bone at a
 * target point; the spring keeps its own point loc and velocity v and chases the target as a damped spring, by
 * semi-implicit Euler:
 *     a = stiffness (target - loc) - damping v          (damping held at 1/dt at most, so a step never reverses v)
 *     v += a dt;  |v dt| <= resetThresh;  next = loc + v dt
 *     source axes without a translate flag hold no offset; |next - target| <= maxDisplacement
 *     v = (next - loc) / dt;  loc = next
 * A fresh state, or a target more than resetThresh away, starts the spring over at the target at rest. The bone is
 * drawn moved by alpha (clamped to [0, 1]) times the constrained offset (loc - target); a spring with a rotate flag
 * also turns by the shortest arc from (target - pivot) to (target + offset - pivot), about its new location, keeping
 * only the Euler angles (R = Rz(yaw) Ry(pitch) Rx(roll), in the source axes) whose rotate flags are set.
 *
 * The springs run in the solver's reference frame (the entity's model space): only the animated body moves them, not
 * the entity's travel or turn. Translate / rotate flags name the source axes; the asset's basis maps its own axes onto
 * them (it may mirror). Plain C99, no engine dependencies. Asset format: docs/hair/FORMAT.md (HCH1 flag 16).
 */
#ifndef HAIR_SPRING_H
#define HAIR_SPRING_H

#ifdef __cplusplus
extern "C" {
#endif

#define HS_TRANSLATE_X 1
#define HS_TRANSLATE_Y 2
#define HS_TRANSLATE_Z 4
#define HS_ROTATE_X    8
#define HS_ROTATE_Y    16
#define HS_ROTATE_Z    32
/* flags bits 8..11: the body group a spring belongs to, for per-group user settings (ours, not the source's) */
#define HS_GROUP_SHIFT 8
#define HS_GROUP_MASK  (15 << HS_GROUP_SHIFT)
#define HS_GROUP_OTHER 0          /* accessories, wires, belly, arms */
#define HS_GROUP_CHEST 1          /* breasts */
#define HS_GROUP_HIPS  2          /* hips / buttocks and upper thighs */

typedef struct {
	int bone;                 /* hair skeleton bone (a root: it hangs from its host and moves no other spring) */
	int flags;                /* HS_* in the source axes */
	float stiffness;          /* 1/s^2 */
	float damping;            /* 1/s */
	float maxDisplacement;    /* asset units; 0 = no limit */
	float resetThresh;        /* asset units */
	float alpha;              /* output weight; clamped to [0, 1] when applied */
	float pivot[3];           /* bind space: the point a rotating spring turns about (its source parent bone's head) */
} hsSpring;

typedef struct {
	float loc[3], vel[3];     /* reference frame */
	int fresh;                /* next step starts at the target */
} hsState;

/* one fixed step of dt toward target (reference frame); basis: 9 floats, row-major, source = basis x asset */
void HS_Step(const hsSpring *sp, const float basis[9], hsState *st, const float target[3], float dt);
/* the bone's move for an offset (loc - target): clamped, scaled by alpha, and with the rotation when the spring turns.
 * delta: column-major 4x4 in the reference frame, to premultiply onto the bone's animated frame. Returns 0 when the
 * move is the identity. */
int  HS_Delta(const hsSpring *sp, const float basis[9], const float offset[3], const float target[3], const float pivot[3],
	float delta[16]);

#ifdef __cplusplus
}
#endif

#endif
