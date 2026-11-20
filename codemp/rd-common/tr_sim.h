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
What garment cloth (tr_cloth.cpp) and hair chains (tr_hair.cpp) share in the renderer: the cvars of the per-frame
decision and the step plan (rd-common/sim_frame.h), and the r_simCheck counters. Defined in tr_sim.cpp, which
every renderer builds. Design: docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md.
*/
#ifndef TR_SIM_H
#define TR_SIM_H

extern "C" {
#include "rd-common/sim_frame.h"
}

// 1: an instance is decided once per frame by the first view that draws it (shadow views included), measured from
// the scene's camera, and the back end only draws data of its own frame. 0: the earlier behaviour (every non-shadow
// view decides again from its own camera, shadow views draw what was left)
extern cvar_t *r_simPerFrame;
// 1: SIM_STEPS_CARRY, 0: SIM_STEPS_LEGACY (sim_frame.h)
extern cvar_t *r_simSteps;
// Sim_Carry's 'frames'
extern cvar_t *r_simCarryFrames;
// print the counters below every N seconds, and whenever it is set to another value
extern cvar_t *r_simCheck;

enum { SIMCHECK_HAIR, SIMCHECK_SPRING, SIMCHECK_CLOTH, SIMCHECK_SYSTEMS };

struct simCheckSys_t {
	// stepping (front end)
	int advances;          // instance frames advanced
	int steps, capped, stretched;
	double droppedMs;      // simulation time given up
	double maxPoseMs;      // most pose time one step covered
	double maxVelRatio;    // that over the pose time the solver divided by (1 = the solver measured the real velocity)
	double maxJump;        // largest move of the solver's inputs in one step (hair: an animated particle target, model
	                       // space units; cloth: a bone's origin in the pelvis frame, cm)
	long insideSum;        // particles inside the body (hair: overlapping collision pairs), summed over the samples
	int insideSamples, insideMax;
	double insideDepth;    // deepest (hair: units, cloth: cm)
	// drawing (back end)
	int draws;             // uses of an instance's draw data
	int staleDraws;        // ... that were built for an earlier frame
	int staleMaxAge;       // frames
	int refused;           // draw data of an earlier frame turned down (r_simPerFrame): drawn rigid / skinned
	int fallback;          // GPU cloth: a live instance with nothing dispatched this frame, drawn skinned
	int staleFrames;       // frames with at least one of the three above
	int staleFrameMark;
};
extern simCheckSys_t r_simCheckStats[SIMCHECK_SYSTEMS];

static inline bool R_SimCheckOn( void ) { return r_simCheck && r_simCheck->value > 0.0f; }
static inline int R_SimStepMode( void ) { return r_simSteps && !r_simSteps->integer ? SIM_STEPS_LEGACY : SIM_STEPS_CARRY; }
static inline bool R_SimPerFrame( void ) { return !r_simPerFrame || r_simPerFrame->integer != 0; }
static inline double R_SimCarryFrames( void ) { return r_simCarryFrames ? r_simCarryFrames->value : 4.0; }

// a frame's plan into the counters
void R_SimCheckPlan( int sys, const simStepPlan_t *plan );
// one step: the pose time it really covered (ms) and the one the solver was given (ms), and how far its inputs moved
void R_SimCheckStep( int sys, double poseMs, double toldMs, double jump );
void R_SimCheckInside( int sys, int count, double deepest );
// The back end's test: an instance's draw data, built for 'gate', may be used in frame 'frame'. Counts what it sees.
bool R_SimDrawable( int sys, bool ready, const simGate_t *gate, int frame );
void R_SimCheckFallback( int sys, int frame );
// once per frame (the first front-end call of either system): print and clear every r_simCheck seconds, and whenever
// the cvar is set to another value
void R_SimCheckFrame( void );
void R_SimRegister( void );	// from R_ClothRegister and R_HairRegister (registering twice is harmless)

#endif // TR_SIM_H
