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
ClothAsset: reader for SKA1 / SKA2 garment assets (see the cloth asset FORMAT notes).

Portable C99, no engine headers. The parser does not copy: every array points
into the caller's buffer, which must stay alive (and unmodified) as long as the
asset or any SkirtSim built from it is in use. The buffer should be 8-byte
aligned (malloc is); the format pads the double section to 8 bytes.

Layout: header (180 bytes), bone names, refInv, cloth mesh, body collider,
capsules, render map, crc32. Units cm; positions are in the skeleton's bind
space times unitCm.

Version 2 changes only the render map and adds solver parameters: each render
vertex is wrapped on a sim triangle (three particles, barycentric weights, an
offset in glm units along the interpolated sim normal and a normal sign), so a
coarse sim mesh can drive a detailed render mesh, its reversed back shells and
cards off its edges. Version 1 maps each render vertex onto one particle.
*/
#ifndef CLOTH_ASSET_H
#define CLOTH_ASSET_H

#include <stddef.h>
#include <stdint.h>
#include "skirt_sim.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CLOTH_NAME_LEN 32

typedef struct {
	double unitCm;                  /* cm per skeleton unit */
	int nBones, refBone, nVerts, nTris, nInfl, nColVerts, nColTris, nCaps;
	int nRenderSurfaces, nRenderVerts;
	char skeleton[CLOTH_NAME_LEN + 1];
	skirt_config_t config;

	const char *boneNames;          /* nBones * CLOTH_NAME_LEN, not necessarily terminated */
	const float *refInv, *pos, *maxDist, *animDrive, *skinW, *colPos, *colSkinW;
	const uint32_t *tri, *colTri;
	const int32_t *skinBone, *colSkinBone, *capA, *capB;
	const double *capR;

	const char *rsRecords;          /* nRenderSurfaces * 36: name[32] + u32 vertex count */
	int version;                    /* 1 or 2 */
	/* version 1 */
	const uint32_t *renderSim;      /* nRenderVerts: sim vertex of each render vertex, surfaces concatenated */
	const float *renderDist;        /* nRenderVerts */
	/* version 2 */
	const uint32_t *renderTri;      /* nRenderVerts * 3: the particles each render vertex is wrapped on */
	const float *renderW;           /* nRenderVerts * 3: their barycentric weights */
	const float *renderOff;         /* nRenderVerts: offset along the interpolated sim normal (glm units) */
	const float *renderSgn;         /* nRenderVerts: +1 / -1, the render normal's side of the sim normal */
	const char *params;             /* paramsLen bytes of "key value" lines (solver parameters), may be empty */
	int paramsLen;
} cloth_asset_t;

/* Returns 1 on success; on failure 0 with a message in err. checkCrc != 0 verifies the trailing crc32. */
int ClothAsset_Parse(const void *buf, size_t size, int checkCrc, cloth_asset_t *out, char *err, size_t errSize);

/* Name of bone b (copied, terminated) */
void ClothAsset_BoneName(const cloth_asset_t *a, int b, char *out, size_t outSize);
/* Render surface s: name (copied, terminated) and vertex count; returns the start offset into renderSim. */
int ClothAsset_RenderSurface(const cloth_asset_t *a, int s, char *name, size_t nameSize, int *count);

/* Fill the solver's asset view (arrays referenced, not copied). */
void ClothAsset_ToSkirt(const cloth_asset_t *a, skirt_asset_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CLOTH_ASSET_H */
