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
Per-frame stepping shared by the simulations that hang off Ghoul2 models (hair chains, their spring bones, garment
cloth): plain C, header only, so the offline tools (tools/hair, tools/cloth) plan steps with the game's own code.
Design: docs/cloth/records/2026-11-09-hair-cloth-views-and-dropped-steps.md.

1. Step plan. A simulation runs fixed steps of 'step' seconds from an accumulator of scene time. Each step is driven
   by the pose lerped between a stored previous pose and this frame's, at Sim_StepAlpha. When more steps are owed than
   the caller allows in one frame (a hitch, or a character that was off screen for a moment):
     SIM_STEPS_LEGACY (the earlier behaviour): the steps cover the start of the frame, the rest of the time is dropped
       and the frame's pose is stored as previous. The solver stopped short of it, so its next step jumps the
       difference in one step, and measures a velocity several times the real one.
     SIM_STEPS_CARRY: the steps run as usual, one step of pose time each, and what is still owed is carried: the
       stored previous pose becomes the pose of the last step run (baseLag seconds before now), so the next frames
       continue from exactly where the solver is and catch up, 'maxSteps' a frame. Nothing is dropped. Only what is
       owed beyond 'carry' seconds is given up, by stretching this frame's steps over it: each then covers
       poseDt > step of pose time, and the solver is told (HC_BeginFrameDt, SkirtSim_SetPoseDt) so it measures the
       frame's velocity over poseDt and still integrates one step. 'carry' comes from Sim_Carry: what the frames
       can repay. Under a lasting shortage (frames longer than maxSteps steps) it is 0, and every frame's steps
       spread evenly over the frame and end on its pose: an even slow motion with no jumps and no lag.
   Not capped and not behind, both modes give the same numbers as the loops this replaces.
2. Frame gate. One decision per instance and frame, whichever view asks first (Sim_GateSeen / Sim_GateSet), and a
   distance test with an exit margin (Sim_InRange).
*/
#ifndef SIM_FRAME_H
#define SIM_FRAME_H

#include <math.h>

#define SIM_PLAN_MAX_STEPS 64

#define SIM_STEPS_LEGACY 0
#define SIM_STEPS_CARRY  1

typedef struct {
	int n;              /* steps to run this frame */
	int capped;         /* more were owed than maxSteps */
	int stretched;      /* each step covers poseDt > step of pose time */
	double accBefore;   /* time owed before this frame */
	double accStart;    /* the same with this frame added */
	double frame;       /* seconds since the last advanced frame */
	double span;        /* seconds from the stored previous pose to this frame's */
	double step;
	double poseDt;      /* pose time one step covers: step, or more when stretched */
	int poseDtUs;       /* the same in microseconds when stretched (what a dump records), else 0 */
	double accAfter;    /* time owed after the frame's steps (carry mode: may be a step or more) */
	double baseLag;     /* the pose to store as previous lies this far before now: 0 = this frame's pose */
	double baseAlpha;   /* ... and is the lerp at this alpha (1 = this frame's pose) */
	double dropped;     /* time given up this frame (seconds) */
} simStepPlan_t;

/* accBefore / baseLagBefore: the accumulator and where the stored previous pose lies (both 0 after a reset; store
   accAfter / baseLag back after the frame). carry: seconds that may stay owed (SIM_STEPS_CARRY). */
static inline void Sim_PlanSteps( simStepPlan_t *p, double accBefore, double baseLagBefore, double frame, double step,
	int maxSteps, int mode, double carry ) {
	double a;
	int owed = 0;
	if ( maxSteps < 1 ) maxSteps = 1;
	if ( maxSteps > SIM_PLAN_MAX_STEPS ) maxSteps = SIM_PLAN_MAX_STEPS;
	if ( mode == SIM_STEPS_LEGACY ) baseLagBefore = 0;
	p->accBefore = accBefore;
	p->frame = frame;
	p->span = frame + baseLagBefore;
	p->step = step;
	p->accStart = accBefore + frame;
	a = p->accStart;
	while ( a >= step && owed < maxSteps ) {   /* the same subtractions as the loops this replaces */
		a -= step;
		owed++;
	}
	p->n = owed;
	p->capped = a >= step;
	p->stretched = 0;
	p->poseDt = step;
	p->poseDtUs = 0;
	p->dropped = 0;
	p->accAfter = a;
	p->baseLag = 0;
	p->baseAlpha = 1;
	if ( !p->capped ) return;
	if ( mode == SIM_STEPS_LEGACY ) {
		p->accAfter = fmod( a, step );   /* too slow to keep up: drop time */
		p->dropped = a - p->accAfter;
		return;
	}
	if ( carry < 0 ) carry = 0;
	if ( a > carry && p->span > 0 ) {
		/* owed beyond what may be carried: this frame's steps cover that too */
		p->poseDtUs = (int)floor( ( p->accStart - carry ) / p->n * 1e6 + 0.5 );
		p->poseDt = p->poseDtUs * 1e-6;
		if ( p->poseDt > step ) {
			p->stretched = 1;
			p->dropped = p->n * ( p->poseDt - step );
			p->accAfter = p->accStart - p->n * p->poseDt;
			if ( p->accAfter < 0 ) p->accAfter = 0;
		} else {
			p->poseDt = step;
			p->poseDtUs = 0;
		}
	}
	if ( p->accAfter > 0 && p->span > 0 ) {
		p->baseLag = p->accAfter;
		p->baseAlpha = 1.0 - p->accAfter / p->span;
		if ( p->baseAlpha < 0 ) p->baseAlpha = 0;
	}
}

/* How much may stay owed after a capped frame (SIM_STEPS_CARRY): 'frames' times what a normal frame can repay,
   i.e. the steps it may run less the time it takes, the shorter of this frame and the one before it standing for
   "normal" (a hitch is long, the frames around it are not). A hitch between fast frames is then carried and caught
   up within about 'frames' frames; when the frames themselves are too long to keep up nothing is carried, so the
   steps spread evenly over each frame instead of the solver settling behind the pose. */
static inline double Sim_Carry( double frames, int maxSteps, double step, double frame, double prevFrame ) {
	const double spare = maxSteps * step - ( prevFrame > 0 && prevFrame < frame ? prevFrame : frame );   /* prevFrame 0: none yet */
	return spare > 0 && frames > 0 ? frames * spare : 0.0;
}

/* where step k's pose lies between the stored previous pose (0) and this frame's (1) */
static inline double Sim_StepAlpha( const simStepPlan_t *p, int k ) {
	double alpha, a;
	if ( p->stretched ) {
		a = p->accStart - ( k + 1 ) * p->poseDt;
		if ( a < 0 ) a = 0;
	} else {
		/* this step ends (acc - step) seconds before now */
		int i;
		a = p->accStart;
		for ( i = 0; i < k; i++ ) a -= p->step;
		a -= p->step;
	}
	alpha = 1.0 - a / p->span;
	return alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
}

/* The pose time to hand the solver for a step of this plan: 0 = one step (the solver's own dt). */
static inline double Sim_StepPoseDt( const simStepPlan_t *p ) {
	return p->stretched ? p->poseDt : 0.0;
}

/* A dump's flags word carries a stretched step's pose time above bit 8 (microseconds), 0 = the step's own dt. */
#define SIM_DUMP_FLAG_MASK 0xff
static inline int Sim_DumpFlags( int flags, const simStepPlan_t *p ) {
	return ( flags & SIM_DUMP_FLAG_MASK ) | ( p && p->stretched ? p->poseDtUs << 8 : 0 );
}
static inline double Sim_DumpPoseDt( int flags ) {
	return (double)( (unsigned)flags >> 8 ) * 1e-6;
}

/* ---- one decision per instance and frame ---- */

typedef struct {
	int frame, scene;   /* the frame and scene the instance's draw data was built for */
	int valid;
	int inRange;        /* the last distance test passed (Sim_InRange's memory) */
} simGate_t;

static inline void Sim_GateClear( simGate_t *g ) { g->frame = g->scene = 0; g->valid = 0; g->inRange = 0; }
static inline int Sim_GateSeen( const simGate_t *g, int frame, int scene ) {
	return g->valid && g->frame == frame && g->scene == scene;
}
static inline void Sim_GateSet( simGate_t *g, int frame, int scene ) { g->frame = frame; g->scene = scene; g->valid = 1; }
/* the back end: the data is this frame's (any scene of it) */
static inline int Sim_GateFrame( const simGate_t *g, int frame ) { return g->valid && g->frame == frame; }
static inline int Sim_GateAge( const simGate_t *g, int frame ) { return g->valid ? frame - g->frame : -1; }

/* in at maxDist, out 1/16 beyond it, so an instance at the limit does not alternate; maxDist <= 0 = any distance */
static inline int Sim_InRange( float dist, float maxDist, int wasIn ) {
	if ( !( maxDist > 0.0f ) ) return 1;
	return dist <= ( wasIn ? maxDist * 1.0625f : maxDist );
}

#endif /* SIM_FRAME_H */
