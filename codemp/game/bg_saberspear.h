/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Spear saber throw (game g_saberspear.c, cgame cg_players.c): the body part a pinning spear went through.
// The game picks it from the blade's hit on the model and sends it in the thrown saber's s.generic1; cgame holds
// that part's ragdoll bone on the blade and lets the rest of the body hang from it.

#pragma once

typedef enum {
	SPEAR_PIN_CHEST,
	SPEAR_PIN_HEAD,
	SPEAR_PIN_WAIST,
	SPEAR_PIN_RARM,
	SPEAR_PIN_LARM,
	SPEAR_PIN_RHAND,
	SPEAR_PIN_LHAND,
	SPEAR_PIN_RTHIGH,
	SPEAR_PIN_LTHIGH,
	SPEAR_PIN_RSHIN,
	SPEAR_PIN_LSHIN,
	SPEAR_PIN_RFOOT,
	SPEAR_PIN_LFOOT,
	SPEAR_PIN_COUNT
} spearPin_t;

// Measured on the humanoid skeleton (stormtrooper, 2026-11-25): standing, the pelvis is 33 units above the floor,
// the chest (thoracic) 16 above the pelvis, the eyebrow 27, the shoulders 19, the forearm bone (rradiusX) 15 out
// from the shoulder and the hand 4 beyond it; the thigh bone is 7 below the pelvis, the knee 15 and the ankle 29.
// The ragdoll holds its pelvis 19 units below the entity origin (G2_bones.cpp: DEFAULT_MINS_2 plus the pelvis
// radius plus 2).
#define SPEAR_RAG_PELVIS_DROP	19.0f
#define SPEAR_PELVIS_TO_FEET	29.0f

// How far the pelvis hangs below the pinned part. Legs hang nothing: a leg pinned that low leaves the body slumped
// on the floor, so its pelvis sits a little above the pin.
static const float spearPinPelvisBelow[SPEAR_PIN_COUNT] = {
	16.0f, 27.0f, 0.0f, 34.0f, 34.0f, 38.0f, 38.0f, -4.0f, -4.0f, -4.0f, -4.0f, -4.0f, -4.0f
};

static inline int SpearPinHangs(int part)
{ // the body hangs below the pin (not a leg)
	return part <= SPEAR_PIN_LHAND;
}

// the ragdoll bone held on the blade for each part; the waist is the ragdoll's root, which follows the entity origin
static const char *const spearPinBone[SPEAR_PIN_COUNT] = {
	"thoracic", "ceyebrow", "pelvis", "rradiusX", "lradiusX", "rhand", "lhand",
	"rfemurX", "lfemurX", "rtibia", "ltibia", "rtalus", "ltalus"
};
