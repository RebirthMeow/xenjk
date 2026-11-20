/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
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

#pragma once

#include "cm_polylib.h"
#include "cm_public.h"
#include "qcommon/qcommon.h"

#define	MAX_SUBMODELS			512
#define	BOX_MODEL_HANDLE		(MAX_SUBMODELS-1)
#define CAPSULE_MODEL_HANDLE	(MAX_SUBMODELS-2)

struct Point
{
	long x, y;
};

// the collision structures hold 32-bit indices into their clip map's arrays, not pointers (docs/levelport-collision.md
// sections 26 and 31: a city's collision is mostly planes and brush sides; a side is 4 bytes, a plane 16, a node 16,
// a brush 40, a leaf 24)
typedef struct cNode_s {
	int			planeNum;			// into the clip map's planes
	int			children[2];		// negative numbers are leafs
	int			planeType;			// its plane's type (the tree walks read the plane in place: cmPlane_t has none)
} cNode_t;

typedef struct cLeaf_s {
	int			cluster;
	int			area;

	int			firstLeafBrush;
	int			numLeafBrushes;

	int			firstLeafSurface;
	int			numLeafSurfaces;
} cLeaf_t;

typedef struct cmodel_s {
	vec3_t		mins, maxs;
	cLeaf_t		leaf;			// submodels don't reference the main tree
	int			firstNode;		// only for cmodel[0] (for the main and bsp instances)
} cmodel_t;

// A plane is its normal and dist, 16 bytes; type and signbits follow from the normal by the loader's own rules and are
// computed when read (CM_LoadPlane rebuilds the cplane_t, bit for bit). The box hull's twelve planes, past the map's,
// keep the types CM_InitBoxHull gives them (cm_boxPlaneType). docs/levelport-collision.md section 31.
typedef struct cmPlane_s {
	vec3_t		normal;
	float		dist;
} cmPlane_t;
extern byte cm_boxPlaneType[12];

// A brush side holds its plane alone (docs/levelport-collision.md section 31). A side's shader is only read for the
// hit side's surface flags, and on a map whose every side is its brush's shader or the bevel's (noshader), bit 31
// marks the bevels; a map with any other side keeps clipMap_t.sideShaders, one a stored side.
typedef struct cbrushside_s {
	int			planeNum;			// into the clip map's planes | its signbits << CM_SIDE_SIGNS | CM_SIDE_BEVEL
} cbrushside_t;
#define CM_SIDE_BEVEL		0x80000000
#define CM_SIDE_PLANE		0x07FFFFFF		// a clip map holds 2^27 planes at most (the loaders check)
#define CM_SIDE_SIGNS		27				// the plane's signbits, so a side test does not derive them

// An implicit brush (CM_BRUSH_IMPLICIT) does not store its first six sides: they are q3map2's axial planes at its
// bounds (-x +x -y +y -z +z, the order the loader reads bounds in), bit-exact normals, built from bounds when traced
// (CM_SidePlane).
// Bit k of the mask (k < 6) says the axial side k is a face of the brush's shader, else a bevel.
typedef struct cbrush_s {
	unsigned short		shaderNum;		// the shader that determined the contents
	unsigned short		sideMask;		// CM_BRUSH_IMPLICIT | the six axial sides' face bits
	int					contents;
	vec3_t				bounds[2];
	int					firstSide;		// into the clip map's brush sides: the first stored side
	unsigned short		numsides;		// every side, the implicit six too
	unsigned short		checkcount;		// to avoid repeated testings
} cbrush_t;
#define CM_BRUSH_IMPLICIT	0x0040

class CCMShader
{
public:
	char		shader[MAX_QPATH];
	class CCMShader	*mNext;
	int			surfaceFlags;
	int			contentFlags;

	const char	*GetName(void) const { return(shader); }
	class CCMShader *GetNext(void) const { return(mNext); }
	void	SetNext(class CCMShader *next) { mNext = next; }
	void	Destroy(void) { }
};

typedef struct cPatch_s {
	int			checkcount;				// to avoid repeated testings
	int			surfaceFlags;
	int			contents;
	struct patchCollide_s	*pc;
} cPatch_t;


typedef struct cArea_s {
	int			floodnum;
	int			floodvalid;
} cArea_t;

typedef struct clipMap_s {
	char		name[MAX_QPATH];

	int			numShaders;
	CCMShader	*shaders;

	int			numBrushSides;		// stored sides
	cbrushside_t *brushsides;
	int			*sideShaders;		// a stored side's shader, on a map whose sides break the rule; else NULL
	int			bevelShader;		// the map's noshader (the bevels' shader), -1 if none

	int			numPlanes;
	cmPlane_t	*planes;

	int			numNodes;
	cNode_t		*nodes;

	int			numLeafs;
	cLeaf_t		*leafs;

	int			numLeafBrushes;
	int			*leafbrushes;

	int			numLeafSurfaces;
	int			*leafsurfaces;

	int			numSubModels;
	cmodel_t	*cmodels;

	int			numBrushes;
	cbrush_t	*brushes;

	int			numClusters;
	int			clusterBytes;
	byte		*visibility;
	qboolean	vised;			// if false, visibility is just a single cluster of ffs

	int			numEntityChars;
	char		*entityString;

	int			numAreas;
	cArea_t		*areas;
	int			*areaPortals;	// [ numAreas*numAreas ] reference counts

	int			numSurfaces;
	cPatch_t	**surfaces;			// non-patches will be NULL

	int			floodvalid;
	int			checkcount;					// incremented on each trace
} clipMap_t;


// keep 1/8 unit away to keep the position valid before network snapping
// and to avoid various numeric issues
#define	SURFACE_CLIP_EPSILON	(0.125)

extern	clipMap_t	cmg; //rwwRMG - changed from cm

// Collision chunks (xenjk, docs/levelport-collision.md): BSPs compiled around their own origin and placed at a
// whole-unit offset, each in a clip map of its own with its tree. The world trace and point contents also run over
// every chunk the query's bounds touch, the query moved into the chunk's frame (exact for floats on the chunk's
// grid), its result moved back; hits stay world hits. The main map is the hub: leaves, clusters, areas, entities.
#define MAX_CM_CHUNKS			1024
#define MAX_CM_CHUNK_ALLOCS		48
typedef struct cmChunk_s {
	clipMap_t	cm;
	vec3_t		origin;			// whole units
	vec3_t		mins, maxs;		// world bounds: the chunk's model 0 (spread a unit) moved by origin
	// the blocks its clip map was loaded into, so that residency can free the chunk alone
	void		*allocs[MAX_CM_CHUNK_ALLOCS];
	int			numAllocs;
	long long	bytes;			// 64-bit: a chunk's arrays together may pass 2 GB
} cmChunk_t;
// cm_chunks is indexed by slot: a manifest's chunks (cm_chunkmanifest) hold their manifest index whether resident or
// not (NULL when not), so traces visit resident chunks in manifest order; cm_loadchunk's chunks follow. A NULL slot is
// skipped.
extern	cmChunk_t	*cm_chunks[MAX_CM_CHUNKS];
extern	int			cm_numChunks;
void	CM_TraceChunks( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
						int brushmask, int capsule );
int		CM_PointContentsInMap( const vec3_t p, clipMap_t *local );
// the placed bounds of the manifest chunks not swapped in, listed by CM_ListUnloaded; with cm_chunkUnloadedSolid 1
// each is one solid box for traces and point contents (docs/levelport-collision.md section 23)
extern	int			cm_numUnloaded;
extern	vec3_t		cm_unloadedMins[MAX_CM_CHUNKS], cm_unloadedMaxs[MAX_CM_CHUNKS];	// by slot (cm_slotSolid)
extern	byte		cm_slotSolid[MAX_CM_CHUNKS];	// a non-resident manifest slot traced as its solid box (2: loading)
extern	int			cm_pendingStops;				// traces a loading slot's box stopped or started in (the false wall)
extern	int			cm_pendingStopsHeld;			// those while a client was held
extern	int			cm_pendingStopsShort;			// those at most 256 u long with no client held
extern	qboolean	cm_heldNow;						// a client held this server frame
// the chunk index (docs/levelport-collision.md section 25): the slots a box [lo, hi] may touch, ascending; returns
// their count. Every slot past the manifest, and a resident chunk wider than its manifest bounds, is always in
int		CM_ChunkCandidates( const vec3_t lo, const vec3_t hi, int *out );
void	CM_ClipUnloaded( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
						 const int *cand, int numCand );

extern	int			c_pointcontents;
extern	int			c_traces, c_brush_traces, c_patch_traces;
extern	cvar_t		*cm_noAreas;
extern	cvar_t		*cm_noCurves;
extern	cvar_t		*cm_playerCurveClip;
extern	cvar_t		*cm_extraVerbose;

// cm_test.c

// Used for oriented capsule collision detection
typedef struct sphere_s {
	qboolean	use;
	float		radius;
	float		halfheight;
	vec3_t		offset;
} sphere_t;

typedef struct traceWork_s { //rwwRMG - modified
	vec3_t		start;
	vec3_t		end;
	vec3_t		size[2];	// size of the box being swept through the model
	vec3_t		offsets[8];	// [signbits][x] = either size[0][x] or size[1][x]
	float		maxOffset;	// longest corner length from origin
	vec3_t		extents;	// greatest of abs(size[0]) and abs(size[1])
	vec3_t		modelOrigin;// origin of the model tracing through
	int			contents;	// ored contents of the model tracing through
	qboolean	isPoint;	// optimized case
//	trace_t		trace;		// returned from trace call
	sphere_t	sphere;		// sphere for oriendted capsule collision

	//rwwRMG - added:
	vec3pair_t		bounds;			// enclosing box of start and end surrounding by size
	vec3pair_t		localBounds;	// enclosing box of start and end surrounding by size for a segment

	float			baseEnterFrac;	// global enter fraction (before processing subsections of the brush)
	float			baseLeaveFrac;	// global leave fraction (before processing subsections of the brush)
	float			enterFrac;		// fraction where the ray enters the brush
	float			leaveFrac;		// fraction where the ray leaves the brush
	int				leadside;		// the hit side's index in its brush: its plane and shader are rebuilt at the hit
	cmPlane_t		side;			// scratch: an implicit brush's axial side being tested
	bool			startout;
	bool			getout;

	struct clipMap_s	*clipMap;	// the clip map being traced: its shader table gives a hit's surface flags

} traceWork_t;

typedef struct leafList_s {
	int		count;
	int		maxcount;
	qboolean	overflowed;
	int		*list;
	vec3_t	bounds[2];
	int		lastLeaf;		// for overflows where each leaf can't be stored individually
	void	(*storeLeafs)( struct leafList_s *ll, int nodenum );
} leafList_t;

void CM_StoreLeafs( leafList_t *ll, int nodenum );
void CM_StoreBrushes( leafList_t *ll, int nodenum );

void CM_BoxLeafnums_r( leafList_t *ll, int nodenum );

// plane i of a clip map as a cplane_t
inline void CM_LoadPlane( const clipMap_t *cm, int i, cplane_t *out ) {
	const cmPlane_t *p = &cm->planes[i];
	out->normal[0] = p->normal[0];
	out->normal[1] = p->normal[1];
	out->normal[2] = p->normal[2];
	out->dist = p->dist;
	// CMod_LoadPlanes' rules (PlaneTypeForNormal, a bit a negative component); the box hull's from its table
	out->type = i >= cm->numPlanes ? cm_boxPlaneType[i - cm->numPlanes] :
		p->normal[0] == 1.0f ? PLANE_X : p->normal[1] == 1.0f ? PLANE_Y : p->normal[2] == 1.0f ? PLANE_Z : PLANE_NON_AXIAL;
	out->signbits = ( p->normal[0] < 0 ) | ( ( p->normal[1] < 0 ) << 1 ) | ( ( p->normal[2] < 0 ) << 2 );
	out->pad[0] = out->pad[1] = 0;
}
// a plane's signbits, as the loader gives them (a bit a negative component)
inline int CM_PlaneSignbits( const cmPlane_t *p ) {
	return ( p->normal[0] < 0 ) | ( ( p->normal[1] < 0 ) << 1 ) | ( ( p->normal[2] < 0 ) << 2 );
}
// side i of brush b: a stored side's plane in place, or an implicit brush's axial plane built from its bounds in *axial;
// *signbits gets the plane's signbits
void CM_AxialPlane( const cbrush_t *b, int i, cmPlane_t *out );
extern const int cm_axialSignbits[6];
inline const cmPlane_t *CM_SidePlane( const clipMap_t *cm, const cbrush_t *b, int i, cmPlane_t *axial, int *signbits ) {
	int s;
	if ( b->sideMask & CM_BRUSH_IMPLICIT ) {
		if ( i < 6 ) {
			CM_AxialPlane( b, i, axial );
			*signbits = cm_axialSignbits[i];
			return axial;
		}
		s = cm->brushsides[b->firstSide + i - 6].planeNum;
	} else {
		s = cm->brushsides[b->firstSide + i].planeNum;
	}
	*signbits = ( s >> CM_SIDE_SIGNS ) & 7;
	return &cm->planes[s & CM_SIDE_PLANE];
}
// side i of brush b as a whole cplane_t (a hit's trace.plane)
void CM_SidePlaneFull( const clipMap_t *cm, const cbrush_t *b, int i, cplane_t *out );
// the shader of side i of brush b, for a hit's surface flags
int CM_SideShader( const clipMap_t *cm, const cbrush_t *b, int i );

// the next value of a clip map's check counter: a brush keeps 16 bits of it, so at the wrap every brush's and patch's
// mark is cleared and the count starts over (else past 65,535 the marks never match and multi-leaf brushes are
// tested once per leaf)
void CM_NextCheckCount( clipMap_t *cm );

cmodel_t	*CM_ClipHandleToModel( clipHandle_t handle, clipMap_t **clipMap = 0 );

// cm_patch.c

struct patchCollide_s	*CM_GeneratePatchCollide( int width, int height, vec3_t *points );
void CM_TraceThroughPatchCollide( traceWork_t *tw, trace_t &trace, const struct patchCollide_s *pc );
qboolean CM_PositionTestInPatchCollide( traceWork_t *tw, const struct patchCollide_s *pc );
void CM_ClearLevelPatches( void );

// cm_shader.cpp
void CM_SetupShaderProperties( void );
void CM_ShutdownShaderProperties(void);
CCMShader *CM_GetShaderInfo( const char *name );
CCMShader *CM_GetShaderInfo( int shaderNum );
void		CM_GetModelFormalName ( const char* model, const char* skin, char* name, int size );

// cm_load.cpp
void CM_GetWorldBounds ( vec3_t mins, vec3_t maxs );
