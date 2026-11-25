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
The renderer state garment cloth (tr_cloth.cpp) and hair chains (tr_hair.cpp) share: the cvars of the per-frame
decision and the step plan, and the r_simCheck counters (tr_sim.h).
*/
#include "tr_local.h"
#include "rd-common/tr_sim.h"

#include <string.h>

float r_simWindMs[3];
cvar_t *r_simWind, *r_simWindForce;

void R_SimWindMs( float out[3] ) {
	float f[3];
	if ( r_simWindForce && r_simWindForce->string[0] &&
		sscanf( r_simWindForce->string, "%f %f %f", &f[0], &f[1], &f[2] ) == 3 ) {
		out[0] = f[0]; out[1] = f[1]; out[2] = f[2];
		return;
	}
	const float k = r_simWind ? r_simWind->value : 0.0f;
	out[0] = r_simWindMs[0] * k; out[1] = r_simWindMs[1] * k; out[2] = r_simWindMs[2] * k;
}

cvar_t *r_simPerFrame;
cvar_t *r_simSteps;
cvar_t *r_simCarryFrames;
cvar_t *r_simCheck;
simCheckSys_t r_simCheckStats[SIMCHECK_SYSTEMS];

/* ------------------------------------------------------------------ r_simCheck (tr_sim.h) */

void R_SimCheckPlan( int sys, const simStepPlan_t *plan ) {
	if ( !R_SimCheckOn() ) return;
	simCheckSys_t &c = r_simCheckStats[sys];
	c.advances++;
	c.capped += plan->capped;
	c.stretched += plan->stretched;
	c.droppedMs += plan->dropped * 1000.0;
}

void R_SimCheckStep( int sys, double poseMs, double toldMs, double jump ) {
	if ( !R_SimCheckOn() ) return;
	simCheckSys_t &c = r_simCheckStats[sys];
	c.steps++;
	if ( poseMs > c.maxPoseMs ) c.maxPoseMs = poseMs;
	if ( toldMs > 0 && poseMs / toldMs > c.maxVelRatio ) c.maxVelRatio = poseMs / toldMs;
	if ( jump > c.maxJump ) c.maxJump = jump;
}

void R_SimCheckInside( int sys, int count, double deepest ) {
	if ( !R_SimCheckOn() ) return;
	simCheckSys_t &c = r_simCheckStats[sys];
	c.insideSum += count;
	c.insideSamples++;
	if ( count > c.insideMax ) c.insideMax = count;
	if ( deepest > c.insideDepth ) c.insideDepth = deepest;
}

static void SimCheck_StaleFrame( simCheckSys_t &c, int frame ) {
	if ( c.staleFrameMark != frame ) {
		c.staleFrameMark = frame;
		c.staleFrames++;
	}
}

bool R_SimDrawable( int sys, bool ready, const simGate_t *gate, int frame ) {
	if ( !ready ) return false;
	const int age = Sim_GateAge( gate, frame );
	const bool perFrame = R_SimPerFrame();
	if ( R_SimCheckOn() ) {
		simCheckSys_t &c = r_simCheckStats[sys];
		if ( age == 0 ) {
			c.draws++;
		} else if ( perFrame ) {
			c.refused++;
			SimCheck_StaleFrame( c, frame );
		} else {
			c.draws++;
			c.staleDraws++;
			if ( age > c.staleMaxAge ) c.staleMaxAge = age;
			SimCheck_StaleFrame( c, frame );
		}
	}
	return age == 0 || !perFrame;
}

void R_SimCheckFallback( int sys, int frame ) {
	if ( !R_SimCheckOn() ) return;
	simCheckSys_t &c = r_simCheckStats[sys];
	c.fallback++;
	SimCheck_StaleFrame( c, frame );
}

static void SimCheck_Print( int frames ) {
	static const char *const names[SIMCHECK_SYSTEMS] = { "hair", "spring", "cloth" };
	for ( int k = 0; k < SIMCHECK_SYSTEMS; k++ ) {
		const simCheckSys_t &c = r_simCheckStats[k];
		if ( !c.advances && !c.draws && !c.refused && !c.fallback ) continue;
		ri.Printf( PRINT_ALL, "SIMCHECK %s perFrame %d steps %d | frames %d advances %d steps %d capped %d stretched %d dropped %.0f ms | "
			"per step: pose %.1f ms (x%.2f of what the solver was told), input jump %.2f | inside: samples %d mean %.2f max %d deepest %.2f | "
			"draws %d stale %d (oldest %d frames) refused %d fallback %d, frames with any %d\n",
			names[k], R_SimPerFrame() ? 1 : 0, R_SimStepMode(), frames, c.advances, c.steps, c.capped, c.stretched, c.droppedMs,
			c.maxPoseMs, c.maxVelRatio, c.maxJump, c.insideSamples, c.insideSamples ? (double)c.insideSum / c.insideSamples : 0.0,
			c.insideMax, c.insideDepth, c.draws, c.staleDraws, c.staleMaxAge, c.refused, c.fallback, c.staleFrames );
	}
}

// once per frame, from the first front-end call of either system: every r_simCheck seconds, and whenever the cvar is
// set to another value (a scripted check closes its window that way), print the window's counters and start over
void R_SimCheckFrame( void ) {
	static int lastFrame, lastPrint, frames;
	if ( !r_simCheck || tr.frameCount == lastFrame ) return;
	lastFrame = tr.frameCount;
	const bool changed = r_simCheck->modified != qfalse;
	r_simCheck->modified = qfalse;
	if ( !R_SimCheckOn() ) {
		lastPrint = 0;
		return;
	}
	const int real = ri.Milliseconds();
	if ( !lastPrint ) {
		lastPrint = real;
		frames = 0;
		memset( r_simCheckStats, 0, sizeof( r_simCheckStats ) );
		return;
	}
	if ( !changed && real - lastPrint < (int)( r_simCheck->value * 1000.0f ) ) {
		frames++;
		return;
	}
	SimCheck_Print( frames );
	memset( r_simCheckStats, 0, sizeof( r_simCheckStats ) );
	lastPrint = real;
	frames = 1;
}

void R_SimRegister( void ) {
	r_simWindForce = ri.Cvar_Get( "r_simWindForce", "", CVAR_NONE, "Cloth and hair: a fixed wind \"x y z\" in m/s along the world axes in place of the scene's (any map; tests); empty: the scene's" );
	r_simWind = ri.Cvar_Get( "r_simWind", "1", CVAR_ARCHIVE_ND, "Cloth and hair feel the scene's wind (the map's r_sceneTreeWind, its direction and gusts) times this; 0 still air" );
	r_simPerFrame = ri.Cvar_Get( "r_simPerFrame", "1", CVAR_NONE, "Hair and cloth: 1 decided once per frame by the first view that draws the character (shadow views too), from the scene's camera; 0 per view" );
	r_simSteps = ri.Cvar_Get( "r_simSteps", "1", CVAR_NONE, "Hair and cloth after a long frame or an off-screen moment: 1 carry the steps owed to the next frames and stretch the rest over the frame, 0 drop the time at the frame's end" );
	r_simCarryFrames = ri.Cvar_Get( "r_simCarryFrames", "4", CVAR_NONE, "Hair and cloth, r_simSteps 1: catch up over about this many frames (0 = at once, stretched over the long frame)" );
	r_simCheck = ri.Cvar_Get( "r_simCheck", "0", CVAR_NONE, "Hair and cloth: print SIMCHECK counters every N seconds, and whenever this is set to another value (stale draws, capped frames, pose jump per step, particles inside the body)" );
	r_simCheck->modified = qfalse;
}
