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
/* hair_asset.h -- the hair-chains sidecar (HCH1 v2, docs/hair/FORMAT.md): "models/x/model.chains" next to
 * "models/x/model.glm". Parsed into the solver's hcAsset plus what the renderer needs: the names of the game bones
 * that carry collider points, and the render map (which GLM vertices ride which hair bones, per LOD).
 * Plain C99, no engine dependencies. */
#ifndef HAIR_ASSET_H
#define HAIR_ASSET_H

#include <stddef.h>

#include "hair_sim.h"
#include "hair_spring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAIR_NAME_LEN 32

/* asset flags read by the renderer glue (the solver's own are HC_ASSET_*, hair_sim.h) */
#define HAIR_ASSET_BONE_HOSTS 4     /* a bone-host table follows the render entries: root bones hang from game bones */
#define HAIR_ASSET_OWN_SKIN   8     /* a render entry's bone -1 share follows the vertex's own GLM skinning */
#define HAIR_ASSET_SPRINGS    16    /* a spring-bone block follows the bone hosts (hair_spring.h) */
#define HAIR_ASSET_GAME_REFS  32    /* a render entry bone <= -2 is game bone -2 - bone (gameBoneNames) */

/* particle group bits read by the renderer glue (bits 0..7 are the sidecar's own group index): a body particle (breast,
   skirt, tie) is tuned by the spring-bone settings (r_hairSprings*), in its body group (bits 8..11, HS_GROUP_*); the
   others are hair (r_hairChains) */
#define HAIR_PARTICLE_BODY    ( 1 << 12 )

typedef struct {
	char name[HAIR_NAME_LEN];   /* GLM surface name */
	int numVerts;               /* its vertex count at that LOD (a binding check) */
	int lod;
} hairRenderSurf_t;

typedef struct {
	int surf;                   /* index into renderSurfs */
	int vert;                   /* vertex index within the surface at that LOD */
	int bone[4];                /* hair skeleton bone, -1 = the vertex's own GLM skinning (HAIR_ASSET_OWN_SKIN; else the host bone),
	                               <= -2 = game bone -2 - bone (HAIR_ASSET_GAME_REFS) */
	float w[4];
} hairRenderEntry_t;

typedef struct {
	hcAsset a;                  /* repulsor gameBone[] index gameBoneNames */
	char hostName[HAIR_NAME_LEN];
	int nGameBones;
	char (*gameBoneNames)[HAIR_NAME_LEN];
	int nRenderSurfs, nRenderEntries;
	hairRenderSurf_t *renderSurfs;
	hairRenderEntry_t *renderEntries;  /* grouped by surface, in file order */
	int *boneHost;              /* per hair bone: -1 = hostName, else a gameBoneNames index; a child takes its root's */
	int nSprings;               /* HAIR_ASSET_SPRINGS */
	float springDt;             /* the springs' fixed step */
	float springBasis[9];       /* row-major: source component axes = basis x asset axes */
	hsSpring *springs;
} hairAsset_t;

/* 1 on success; on failure err holds the reason. The asset owns its allocations (HairAsset_Free). */
int  HairAsset_Parse(const void *buf, size_t len, hairAsset_t *out, char *err, size_t errLen);
/* the offline tools' entry. HAIR_PARSE_OFFLINE also accepts an asset whose repulsor points are supplied per
   frame (asset flag 1, game bone -1: research reels); the game never passes it, as it could not drive such a repulsor */
#define HAIR_PARSE_OFFLINE 1
int  HairAsset_ParseEx(const void *buf, size_t len, hairAsset_t *out, char *err, size_t errLen, int options);
void HairAsset_Free(hairAsset_t *h);

#ifdef __cplusplus
}
#endif

#endif
